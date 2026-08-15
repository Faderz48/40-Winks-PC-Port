using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;

namespace FortyWinksSetup;

internal static class Program
{
    internal const string ExpectedRomSha256 =
        "057232ef7618e25f5645df50d3cd45f08cb5a2cccb3e2fdf48faa8755c4ddb1a";
    internal const string GameExecutableName = "forty-winks-recomp.exe";
    internal const string LauncherExecutableName = "40-Winks-PC-Port.exe";

    private const string PayloadResource = "FortyWinks.Setup.SourcePayload";
    private const string BuildIdResource = "FortyWinks.Setup.BuildId";

    internal static string BuildId { get; } = ReadBuildId();
    internal static string AppDataDirectory { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "40-winks-pc-port");
    internal static string BuildCacheDirectory { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "40WinksBuild");
    internal static string ConfigPath { get; } = Path.Combine(
        AppDataDirectory, "launcher.json");
    internal static string LogPath { get; } = Path.Combine(
        AppDataDirectory, "logs", "build.log");

    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            if (args.Contains("--diagnostics", StringComparer.OrdinalIgnoreCase))
            {
                return RunDiagnostics(args);
            }
            if (args.Contains("--requirements-check", StringComparer.OrdinalIgnoreCase))
            {
                return RunRequirementsCheck(args);
            }
            if (args.Contains("--install-build-tools", StringComparer.OrdinalIgnoreCase))
            {
                return RunBuildToolInstall(args);
            }
            if (args.Contains("--install-portable-tools", StringComparer.OrdinalIgnoreCase))
            {
                return RunPortableToolInstall(args);
            }
            if (args.Contains("--download-compiler-setup", StringComparer.OrdinalIgnoreCase))
            {
                return RunCompilerSetupDownload(args);
            }
            if (args.Contains("--toolchain-smoke-test", StringComparer.OrdinalIgnoreCase))
            {
                return RunToolchainSmokeTest(args);
            }

            Directory.CreateDirectory(AppDataDirectory);
            LauncherConfig config = LauncherConfig.Load(ConfigPath);
            bool forceSetup = args.Contains("--setup", StringComparer.OrdinalIgnoreCase);
            if (!forceSetup && TryFastLaunch(config))
            {
                return 0;
            }

            ApplicationConfiguration.Initialize();
            Application.Run(new SetupForm(config));
            return 0;
        }
        catch (Exception exception)
        {
            MessageBox.Show(
                exception.Message,
                "40 Winks PC Port",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return 1;
        }
    }

    private static bool TryFastLaunch(LauncherConfig config)
    {
        if (config.BuildId != BuildId ||
            string.IsNullOrWhiteSpace(config.RomPath) ||
            string.IsNullOrWhiteSpace(config.OutputDirectory))
        {
            return false;
        }

        string gamePath = Path.Combine(config.OutputDirectory, GameExecutableName);
        string markerPath = Path.Combine(config.OutputDirectory, ".builder-version");
        if (!File.Exists(gamePath) || !File.Exists(config.RomPath) ||
            !File.Exists(markerPath) || File.ReadAllText(markerPath).Trim() != BuildId)
        {
            return false;
        }

        if (!RomHashMatches(config.RomPath))
        {
            return false;
        }

        LaunchGame(config.OutputDirectory, config.RomPath);
        return true;
    }

    internal static bool RomHashMatches(string romPath)
    {
        if (!File.Exists(romPath))
        {
            return false;
        }

        using FileStream stream = File.OpenRead(romPath);
        string hash = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
        return hash == ExpectedRomSha256;
    }

    internal static void LaunchGame(string outputDirectory, string romPath)
    {
        string executable = Path.Combine(outputDirectory, GameExecutableName);
        ProcessStartInfo startInfo = new(executable)
        {
            WorkingDirectory = outputDirectory,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        startInfo.ArgumentList.Add("--rom");
        startInfo.ArgumentList.Add(romPath);
        Process.Start(startInfo);
    }

    internal static string ExtractSourcePayload()
    {
        string safeBuildId = string.Concat(BuildId.Select(character =>
            char.IsLetterOrDigit(character) || character is '-' or '_'
                ? character
                : '_'));
        if (safeBuildId.Length > 12)
        {
            safeBuildId = safeBuildId[..12];
        }
        string sourceDirectory = Path.Combine(BuildCacheDirectory, "src", safeBuildId);
        string markerPath = Path.Combine(sourceDirectory, ".builder-version");
        if (File.Exists(markerPath) && File.ReadAllText(markerPath).Trim() == BuildId)
        {
            return sourceDirectory;
        }

        string stagingDirectory = sourceDirectory + ".new";
        if (Directory.Exists(stagingDirectory))
        {
            Directory.Delete(stagingDirectory, true);
        }
        Directory.CreateDirectory(stagingDirectory);

        using Stream payload = Assembly.GetExecutingAssembly()
            .GetManifestResourceStream(PayloadResource)
            ?? throw new InvalidOperationException("The public source payload is missing.");
        using ZipArchive archive = new(payload, ZipArchiveMode.Read);
        string stagingPrefix = Path.GetFullPath(stagingDirectory)
            .TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        foreach (ZipArchiveEntry entry in archive.Entries)
        {
            string targetPath = Path.GetFullPath(
                Path.Combine(stagingDirectory, entry.FullName.Replace('/', Path.DirectorySeparatorChar)));
            if (!targetPath.StartsWith(stagingPrefix, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException("The embedded source archive contains an unsafe path.");
            }

            if (string.IsNullOrEmpty(entry.Name))
            {
                Directory.CreateDirectory(targetPath);
                continue;
            }

            Directory.CreateDirectory(Path.GetDirectoryName(targetPath)!);
            entry.ExtractToFile(targetPath, true);
        }
        File.WriteAllText(Path.Combine(stagingDirectory, ".builder-version"), BuildId);

        if (Directory.Exists(sourceDirectory))
        {
            Directory.Delete(sourceDirectory, true);
        }
        Directory.Move(stagingDirectory, sourceDirectory);
        return sourceDirectory;
    }

    internal static void InstallLauncherCopy(string outputDirectory)
    {
        string? currentExecutable = Environment.ProcessPath;
        if (string.IsNullOrWhiteSpace(currentExecutable) || !File.Exists(currentExecutable))
        {
            return;
        }

        string target = Path.Combine(outputDirectory, LauncherExecutableName);
        if (string.Equals(
                Path.GetFullPath(currentExecutable),
                Path.GetFullPath(target),
                StringComparison.OrdinalIgnoreCase))
        {
            return;
        }
        File.Copy(currentExecutable, target, true);
    }

    private static string ReadBuildId()
    {
        using Stream stream = Assembly.GetExecutingAssembly()
            .GetManifestResourceStream(BuildIdResource)
            ?? throw new InvalidOperationException("The setup build identifier is missing.");
        using StreamReader reader = new(stream);
        return reader.ReadToEnd().Trim();
    }

    private static int RunDiagnostics(string[] args)
    {
        try
        {
            using Stream payload = Assembly.GetExecutingAssembly()
                .GetManifestResourceStream(PayloadResource)
                ?? throw new InvalidOperationException("Source payload resource is missing.");
            using ZipArchive archive = new(payload, ZipArchiveMode.Read);
            HashSet<string> entries = archive.Entries
                .Select(entry => entry.FullName.Replace('\\', '/'))
                .ToHashSet(StringComparer.Ordinal);
            string[] required =
            {
                "recomp-port/CMakeLists.txt",
                "packaging/windows/launcher/NativeBuildPipeline.cs",
                "packaging/windows/launcher/ManagedToolchain.cs",
                "tools/generate_recomp_symbols.py",
                "recomp/40winks.toml",
            };
            foreach (string requiredEntry in required)
            {
                if (!entries.Contains(requiredEntry))
                {
                    throw new InvalidDataException($"Source payload is missing {requiredEntry}.");
                }
            }

            string[] forbiddenExtensions =
            {
                ".z64", ".n64", ".v64", ".rom", ".ips", ".dll", ".exe", ".AppImage", ".ps1",
            };
            foreach (string entry in entries)
            {
                if (entry.StartsWith("recomp/generated/", StringComparison.OrdinalIgnoreCase) ||
                    entry.StartsWith("work/", StringComparison.OrdinalIgnoreCase) ||
                    entry.StartsWith("build/", StringComparison.OrdinalIgnoreCase) ||
                    forbiddenExtensions.Any(extension =>
                        entry.EndsWith(extension, StringComparison.OrdinalIgnoreCase)))
                {
                    throw new InvalidDataException($"Forbidden payload entry: {entry}");
                }
            }

            string message = $"PowerShell-free Windows launcher diagnostics passed: {entries.Count} public files; build {BuildId}.";
            int outputIndex = Array.FindIndex(args, argument =>
                argument.Equals("--diagnostics-output", StringComparison.OrdinalIgnoreCase));
            if (outputIndex >= 0 && outputIndex + 1 < args.Length)
            {
                File.WriteAllText(args[outputIndex + 1], message + Environment.NewLine);
            }
            return 0;
        }
        catch (Exception exception)
        {
            int outputIndex = Array.FindIndex(args, argument =>
                argument.Equals("--diagnostics-output", StringComparison.OrdinalIgnoreCase));
            if (outputIndex >= 0 && outputIndex + 1 < args.Length)
            {
                File.WriteAllText(args[outputIndex + 1], exception.ToString());
            }
            return 1;
        }
    }

    private static int RunRequirementsCheck(string[] args)
    {
        try
        {
            NativeBuildPipeline pipeline = new();
            pipeline.CheckBuildToolsAsync(CancellationToken.None).GetAwaiter().GetResult();
            WriteDiagnosticsOutput(
                args,
                $"Native Windows build-tool discovery passed; build {BuildId}.{Environment.NewLine}");
            return 0;
        }
        catch (Exception exception)
        {
            WriteDiagnosticsOutput(args, exception + Environment.NewLine);
            return 1;
        }
    }

    private static int RunBuildToolInstall(string[] args)
    {
        try
        {
            NativeBuildPipeline pipeline = new();
            pipeline.InstallBuildToolsAsync(CancellationToken.None).GetAwaiter().GetResult();
            WriteDiagnosticsOutput(
                args,
                $"Self-managed Windows build tools are ready; build {BuildId}." +
                Environment.NewLine);
            return 0;
        }
        catch (Exception exception)
        {
            WriteDiagnosticsOutput(args, exception + Environment.NewLine);
            return 1;
        }
    }

    private static int RunPortableToolInstall(string[] args)
    {
        try
        {
            NativeBuildPipeline pipeline = new();
            pipeline.InstallPortableToolsAsync(CancellationToken.None).GetAwaiter().GetResult();
            WriteDiagnosticsOutput(
                args,
                $"Portable Windows build tools are ready; build {BuildId}." +
                Environment.NewLine);
            return 0;
        }
        catch (Exception exception)
        {
            WriteDiagnosticsOutput(args, exception + Environment.NewLine);
            return 1;
        }
    }

    private static int RunCompilerSetupDownload(string[] args)
    {
        try
        {
            NativeBuildPipeline pipeline = new();
            pipeline.DownloadCompilerSetupAsync(CancellationToken.None).GetAwaiter().GetResult();
            WriteDiagnosticsOutput(
                args,
                $"Microsoft compiler setup download is verified; build {BuildId}." +
                Environment.NewLine);
            return 0;
        }
        catch (Exception exception)
        {
            WriteDiagnosticsOutput(args, exception + Environment.NewLine);
            return 1;
        }
    }

    private static int RunToolchainSmokeTest(string[] args)
    {
        try
        {
            NativeBuildPipeline pipeline = new();
            pipeline.RunToolchainSmokeTestAsync(CancellationToken.None)
                .GetAwaiter().GetResult();
            WriteDiagnosticsOutput(
                args,
                $"Managed Windows compiler smoke test passed; build {BuildId}." +
                Environment.NewLine);
            return 0;
        }
        catch (Exception exception)
        {
            WriteDiagnosticsOutput(args, exception + Environment.NewLine);
            return 1;
        }
    }

    private static void WriteDiagnosticsOutput(string[] args, string text)
    {
        int outputIndex = Array.FindIndex(args, argument =>
            argument.Equals("--diagnostics-output", StringComparison.OrdinalIgnoreCase));
        if (outputIndex >= 0 && outputIndex + 1 < args.Length)
        {
            File.WriteAllText(args[outputIndex + 1], text);
        }
    }
}

internal sealed class LauncherConfig
{
    public string RomPath { get; set; } = "";
    public string OutputDirectory { get; set; } = "";
    public string BuildId { get; set; } = "";

    public static LauncherConfig Load(string path)
    {
        try
        {
            return File.Exists(path)
                ? JsonSerializer.Deserialize<LauncherConfig>(File.ReadAllText(path)) ?? new()
                : new();
        }
        catch
        {
            return new();
        }
    }

    public void Save(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        string temporaryPath = path + ".new";
        File.WriteAllText(temporaryPath, JsonSerializer.Serialize(
            this, new JsonSerializerOptions { WriteIndented = true }));
        File.Move(temporaryPath, path, true);
    }
}
