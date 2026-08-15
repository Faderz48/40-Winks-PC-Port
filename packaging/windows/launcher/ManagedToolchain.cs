using System.IO.Compression;
using System.Net.Http.Headers;
using System.Security.Cryptography;
using System.Text;

namespace FortyWinksSetup;

internal sealed record ManagedToolPaths(
    string Git,
    string CMake,
    string Ninja,
    string Python);

internal sealed class ManagedToolchain
{
    private sealed record ToolArchive(
        string Id,
        string Name,
        string ArchiveName,
        string Url,
        string Sha256,
        string ExecutablePath,
        int ProgressStart,
        int ProgressEnd);

    private const string ToolchainVersion = "windows-x64-v1";
    private static readonly ToolArchive[] Archives =
    {
        new(
            "git",
            "portable Git",
            "MinGit-2.55.0.4-64-bit.zip",
            "https://github.com/git-for-windows/git/releases/download/v2.55.0.windows.4/MinGit-2.55.0.4-64-bit.zip",
            "4e03f94c2ffbf70be337e005cee02661c732dbfc81031a078bda9299b9a7d644",
            Path.Combine("cmd", "git.exe"),
            3,
            20),
        new(
            "cmake",
            "portable CMake",
            "cmake-3.31.10-windows-x86_64.zip",
            "https://github.com/Kitware/CMake/releases/download/v3.31.10/cmake-3.31.10-windows-x86_64.zip",
            "13d1a463d7130df5339baedd63d8ae990aaf385062b2f42f372796143ae94086",
            Path.Combine("cmake-3.31.10-windows-x86_64", "bin", "cmake.exe"),
            20,
            43),
        new(
            "ninja",
            "portable Ninja",
            "ninja-win-1.13.2.zip",
            "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip",
            "07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65",
            "ninja.exe",
            43,
            46),
        new(
            "python",
            "portable Python",
            "python-3.13.14-embeddable-amd64.zip",
            "https://www.python.org/ftp/python/3.13.14/python-3.13.14-embeddable-amd64.zip",
            "2dcbfaca0c0742b6201209e5dbfc129ef5602e44544d6313b09eabc3931cdf65",
            "python.exe",
            46,
            58),
    };

    private static readonly ToolArchive CompilerBootstrapper = new(
        "visual-studio-bootstrapper",
        "Microsoft C++ compiler setup",
        "vs_BuildTools-17.exe",
        "https://download.visualstudio.microsoft.com/download/pr/00d9d26c-2727-42c2-aa9e-eda63b03e1ee/15df9d3b4c2b2eaf44704d5e938c895341b9cd8ba40a9a18610f8d18cbe01b53/vs_BuildTools.exe",
        "15df9d3b4c2b2eaf44704d5e938c895341b9cd8ba40a9a18610f8d18cbe01b53",
        "vs_BuildTools-17.exe",
        59,
        65);

    private static readonly HttpClient HttpClient = CreateHttpClient();

    private string ToolRoot => Path.Combine(
        Program.BuildCacheDirectory, "tools", ToolchainVersion);
    private string DownloadRoot => Path.Combine(
        Program.BuildCacheDirectory, "downloads", ToolchainVersion);

    public event Action<int, string>? ProgressChanged;
    public event Action<string>? OutputReceived;

    public ManagedToolPaths? FindInstalled()
    {
        Dictionary<string, string> executables = new(StringComparer.OrdinalIgnoreCase);
        foreach (ToolArchive archive in Archives)
        {
            string targetDirectory = Path.Combine(ToolRoot, archive.Id);
            string markerPath = Path.Combine(targetDirectory, ".forty-winks-tool-sha256");
            string executable = Path.Combine(targetDirectory, archive.ExecutablePath);
            if (!File.Exists(executable) ||
                !File.Exists(markerPath) ||
                !File.ReadAllText(markerPath).Trim().Equals(
                    archive.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                return null;
            }
            executables[archive.Id] = executable;
        }

        return new ManagedToolPaths(
            executables["git"],
            executables["cmake"],
            executables["ninja"],
            executables["python"]);
    }

    public async Task<ManagedToolPaths> InstallPortableToolsAsync(
        CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(ToolRoot);
        Directory.CreateDirectory(DownloadRoot);
        foreach (ToolArchive archive in Archives)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (ToolIsInstalled(archive))
            {
                Report(archive.ProgressEnd, $"{archive.Name} ready");
                continue;
            }

            string downloadedArchive = await DownloadVerifiedAsync(
                archive, cancellationToken);
            Report(archive.ProgressEnd - 1, $"Preparing {archive.Name}");
            ExtractTool(archive, downloadedArchive);
            Report(archive.ProgressEnd, $"{archive.Name} ready");
        }

        return FindInstalled() ?? throw new InvalidOperationException(
            "The portable Windows tools did not finish installing.");
    }

    public Task<string> DownloadCompilerBootstrapperAsync(
        CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(DownloadRoot);
        return DownloadVerifiedAsync(CompilerBootstrapper, cancellationToken);
    }

    private bool ToolIsInstalled(ToolArchive archive)
    {
        string targetDirectory = Path.Combine(ToolRoot, archive.Id);
        string executable = Path.Combine(targetDirectory, archive.ExecutablePath);
        string markerPath = Path.Combine(targetDirectory, ".forty-winks-tool-sha256");
        return File.Exists(executable) &&
            File.Exists(markerPath) &&
            File.ReadAllText(markerPath).Trim().Equals(
                archive.Sha256, StringComparison.OrdinalIgnoreCase);
    }

    private async Task<string> DownloadVerifiedAsync(
        ToolArchive archive,
        CancellationToken cancellationToken)
    {
        string destination = Path.Combine(DownloadRoot, archive.ArchiveName);
        if (File.Exists(destination) &&
            await HashMatchesAsync(destination, archive.Sha256, cancellationToken))
        {
            Report(archive.ProgressEnd - 1, $"Using verified {archive.Name}");
            return destination;
        }
        File.Delete(destination);

        const int attempts = 3;
        for (int attempt = 1; attempt <= attempts; attempt++)
        {
            string partial = destination + ".part";
            File.Delete(partial);
            try
            {
                OutputReceived?.Invoke($"Downloading {archive.Name} ({attempt}/{attempts})");
                await DownloadOnceAsync(archive, partial, cancellationToken);
                File.Move(partial, destination, true);
                return destination;
            }
            catch when (attempt < attempts && !cancellationToken.IsCancellationRequested)
            {
                File.Delete(partial);
                OutputReceived?.Invoke($"Retrying {archive.Name} download...");
                await Task.Delay(TimeSpan.FromSeconds(attempt * 2), cancellationToken);
            }
        }

        throw new InvalidOperationException($"Could not download {archive.Name}.");
    }

    private async Task DownloadOnceAsync(
        ToolArchive archive,
        string destination,
        CancellationToken cancellationToken)
    {
        using HttpRequestMessage request = new(HttpMethod.Get, archive.Url);
        using HttpResponseMessage response = await HttpClient.SendAsync(
            request, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
        response.EnsureSuccessStatusCode();

        long? totalBytes = response.Content.Headers.ContentLength;
        await using Stream source = await response.Content.ReadAsStreamAsync(cancellationToken);
        await using FileStream target = new(
            destination, FileMode.Create, FileAccess.Write, FileShare.None, 1024 * 128, true);
        using IncrementalHash hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        byte[] buffer = new byte[1024 * 128];
        long received = 0;
        int lastProgress = -1;
        while (true)
        {
            int count = await source.ReadAsync(buffer, cancellationToken);
            if (count == 0)
            {
                break;
            }
            await target.WriteAsync(buffer.AsMemory(0, count), cancellationToken);
            hash.AppendData(buffer, 0, count);
            received += count;

            if (totalBytes > 0)
            {
                int progress = archive.ProgressStart + (int)(
                    (archive.ProgressEnd - archive.ProgressStart - 1L) * received /
                    totalBytes.Value);
                if (progress > lastProgress)
                {
                    lastProgress = progress;
                    ProgressChanged?.Invoke(
                        Math.Clamp(progress, archive.ProgressStart, archive.ProgressEnd - 1),
                        $"Downloading {archive.Name}");
                }
            }
        }
        await target.FlushAsync(cancellationToken);

        string actualHash = Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
        if (!actualHash.Equals(archive.Sha256, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException(
                $"The {archive.Name} download failed its security check. " +
                $"Expected {archive.Sha256}, got {actualHash}.");
        }
    }

    private void ExtractTool(ToolArchive archive, string archivePath)
    {
        string targetDirectory = Path.Combine(ToolRoot, archive.Id);
        string stagingDirectory = targetDirectory + ".new";
        DeleteDirectory(stagingDirectory);
        Directory.CreateDirectory(stagingDirectory);
        try
        {
            using ZipArchive zip = ZipFile.OpenRead(archivePath);
            string stagingPrefix = Path.GetFullPath(stagingDirectory)
                .TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            foreach (ZipArchiveEntry entry in zip.Entries)
            {
                string targetPath = Path.GetFullPath(Path.Combine(
                    stagingDirectory,
                    entry.FullName.Replace('/', Path.DirectorySeparatorChar)));
                if (!targetPath.StartsWith(
                        stagingPrefix, StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidDataException(
                        $"The {archive.Name} archive contains an unsafe path.");
                }

                if (string.IsNullOrEmpty(entry.Name))
                {
                    Directory.CreateDirectory(targetPath);
                    continue;
                }
                Directory.CreateDirectory(Path.GetDirectoryName(targetPath)!);
                entry.ExtractToFile(targetPath, true);
            }

            string executable = Path.Combine(stagingDirectory, archive.ExecutablePath);
            if (!File.Exists(executable))
            {
                throw new InvalidDataException(
                    $"The {archive.Name} archive did not contain {archive.ExecutablePath}.");
            }
            File.WriteAllText(
                Path.Combine(stagingDirectory, ".forty-winks-tool-sha256"),
                archive.Sha256 + Environment.NewLine,
                Encoding.ASCII);

            DeleteDirectory(targetDirectory);
            Directory.Move(stagingDirectory, targetDirectory);
        }
        catch
        {
            DeleteDirectory(stagingDirectory);
            throw;
        }
    }

    private static async Task<bool> HashMatchesAsync(
        string path,
        string expected,
        CancellationToken cancellationToken)
    {
        await using FileStream stream = new(
            path, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 128, true);
        byte[] hash = await SHA256.HashDataAsync(stream, cancellationToken);
        return Convert.ToHexString(hash).Equals(expected, StringComparison.OrdinalIgnoreCase);
    }

    private void Report(int percent, string message)
    {
        OutputReceived?.Invoke(message);
        ProgressChanged?.Invoke(Math.Clamp(percent, 0, 100), message);
    }

    private static HttpClient CreateHttpClient()
    {
        HttpClient client = new()
        {
            Timeout = Timeout.InfiniteTimeSpan,
        };
        client.DefaultRequestHeaders.UserAgent.Add(
            new ProductInfoHeaderValue("FortyWinksSetup", "0.1"));
        return client;
    }

    private static void DeleteDirectory(string path)
    {
        if (!Directory.Exists(path))
        {
            return;
        }
        foreach (string file in Directory.EnumerateFiles(path, "*", SearchOption.AllDirectories))
        {
            File.SetAttributes(file, FileAttributes.Normal);
        }
        Directory.Delete(path, true);
    }
}
