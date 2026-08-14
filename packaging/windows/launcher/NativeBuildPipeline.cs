using System.Collections;
using System.Diagnostics;
using System.Text;
using System.Text.RegularExpressions;

namespace FortyWinksSetup;

internal sealed class MissingBuildToolsException : Exception
{
    public MissingBuildToolsException(IReadOnlyList<string> tools)
        : base($"Required Windows build tools are missing: {string.Join(", ", tools)}")
    {
        Tools = tools;
    }

    public IReadOnlyList<string> Tools { get; }
}

internal sealed class NativeBuildPipeline
{
    private sealed record Dependency(string Name, string Url, string Revision);
    private sealed record ProcessResult(int ExitCode, string Output);
    private sealed record PythonCommand(string FileName, string[] Prefix);

    private static readonly Dependency[] Dependencies =
    {
        new(
            "N64Recomp",
            "https://github.com/N64Recomp/N64Recomp.git",
            "ffb39cdad1da5de07eaaa48bd1db4a89a7986771"),
        new(
            "N64ModernRuntime",
            "https://github.com/N64Recomp/N64ModernRuntime.git",
            "ae1ffbb909d9f93c88c41830deb539f7feef5ed2"),
        new(
            "rt64",
            "https://github.com/rt64/rt64.git",
            "f0728a2520d5aa735886240de3fee75cc805f6d6"),
    };

    private static readonly Regex NinjaProgress = new(
        @"^\[\s*(\d+)\s*/\s*(\d+)\]",
        RegexOptions.Compiled | RegexOptions.CultureInvariant);

    private readonly Dictionary<string, string> environment =
        new(StringComparer.OrdinalIgnoreCase);
    private readonly object processLock = new();
    private readonly object logLock = new();
    private Process? activeProcess;
    private StreamWriter? log;
    private string git = "git.exe";
    private string cmake = "cmake.exe";
    private PythonCommand? python;

    public event Action<int, string>? ProgressChanged;
    public event Action<string>? OutputReceived;

    public async Task CheckBuildToolsAsync(CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Program.LogPath)!);
        using StreamWriter checkLog = new(Program.LogPath, true, new UTF8Encoding(false));
        log = checkLog;
        try
        {
            await FindBuildRequirementsAsync(cancellationToken);
        }
        finally
        {
            log = null;
        }
    }

    public async Task BuildAsync(
        string sourceDirectory,
        string romPath,
        string outputDirectory,
        int jobs,
        CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Program.LogPath)!);
        using StreamWriter buildLog = new(Program.LogPath, false, new UTF8Encoding(false));
        log = buildLog;
        try
        {
            sourceDirectory = Path.GetFullPath(sourceDirectory);
            romPath = Path.GetFullPath(romPath);
            outputDirectory = Path.GetFullPath(outputDirectory);
            ValidatePaths(sourceDirectory, romPath, outputDirectory);
            jobs = Math.Clamp(jobs, 1, 16);

            await FindBuildRequirementsAsync(cancellationToken);

            string externalDirectory = Path.Combine(sourceDirectory, "work", "external");
            string buildDirectory = Path.Combine(sourceDirectory, "build", "windows-release");
            string installDirectory = Path.Combine(sourceDirectory, "build", "windows-install");
            Directory.CreateDirectory(externalDirectory);

            await CheckoutDependencyAsync(
                Dependencies[0], externalDirectory, sourceDirectory, 5, cancellationToken);
            await CheckoutDependencyAsync(
                Dependencies[1], externalDirectory, sourceDirectory, 12, cancellationToken);
            await CheckoutDependencyAsync(
                Dependencies[2], externalDirectory, sourceDirectory, 20, cancellationToken);

            Report(32, "Applying compatibility fixes");
            await ApplyPatchOnceAsync(
                "N64ModernRuntime",
                Path.Combine(sourceDirectory, "patches", "N64ModernRuntime.patch"),
                externalDirectory,
                sourceDirectory,
                cancellationToken);
            await ApplyPatchOnceAsync(
                "rt64",
                Path.Combine(sourceDirectory, "patches", "RT64.patch"),
                externalDirectory,
                sourceDirectory,
                cancellationToken);

            string n64RecompDirectory = Path.Combine(externalDirectory, "N64Recomp");
            string n64RecompBuildDirectory = Path.Combine(n64RecompDirectory, "build-windows");
            Report(35, "Configuring CPU translator");
            await RunRequiredAsync(
                cmake,
                new[]
                {
                    "-S", n64RecompDirectory,
                    "-B", n64RecompBuildDirectory,
                    "-G", "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DCMAKE_C_COMPILER=clang-cl",
                    "-DCMAKE_CXX_COMPILER=clang-cl",
                },
                sourceDirectory,
                cancellationToken);

            Report(40, "Building CPU translator");
            await RunRequiredAsync(
                cmake,
                new[]
                {
                    "--build", n64RecompBuildDirectory,
                    "--parallel", jobs.ToString(),
                    "--target", "N64RecompCLI",
                },
                sourceDirectory,
                cancellationToken,
                40,
                10,
                "Building CPU translator");

            string n64Recomp = Path.Combine(n64RecompBuildDirectory, "N64Recomp.exe");
            if (!File.Exists(n64Recomp))
            {
                throw new InvalidOperationException(
                    $"N64Recomp did not produce the expected program at {n64Recomp}.");
            }

            string generatedDirectory = Path.Combine(sourceDirectory, "recomp", "generated");
            string baseRom = Path.Combine(sourceDirectory, "recomp", "baserom.z64");
            DeleteDirectory(generatedDirectory);
            Directory.CreateDirectory(generatedDirectory);
            File.Delete(baseRom);
            File.Copy(romPath, baseRom, true);
            try
            {
                Report(51, "Mapping game functions");
                List<string> pythonArguments = new(python!.Prefix)
                {
                    Path.Combine(sourceDirectory, "tools", "generate_recomp_symbols.py"),
                    romPath,
                };
                await RunRequiredAsync(
                    python.FileName,
                    pythonArguments,
                    sourceDirectory,
                    cancellationToken);

                Report(55, "Generating native game CPU");
                await RunRequiredAsync(
                    n64Recomp,
                    new[] { "40winks.toml" },
                    Path.Combine(sourceDirectory, "recomp"),
                    cancellationToken);
            }
            finally
            {
                File.Delete(baseRom);
            }

            Report(60, "Configuring playable game");
            await RunRequiredAsync(
                cmake,
                new[]
                {
                    "-S", Path.Combine(sourceDirectory, "recomp-port"),
                    "-B", buildDirectory,
                    "-G", "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DCMAKE_C_COMPILER=clang-cl",
                    "-DCMAKE_CXX_COMPILER=clang-cl",
                    "-DBUILD_TESTING=OFF",
                },
                sourceDirectory,
                cancellationToken);

            Report(66, "Compiling playable game");
            await RunRequiredAsync(
                cmake,
                new[]
                {
                    "--build", buildDirectory,
                    "--parallel", jobs.ToString(),
                },
                sourceDirectory,
                cancellationToken,
                66,
                27,
                "Compiling playable game");

            Report(94, "Installing playable game");
            DeleteDirectory(installDirectory);
            await RunRequiredAsync(
                cmake,
                new[]
                {
                    "--install", buildDirectory,
                    "--prefix", installDirectory,
                    "--config", "Release",
                },
                sourceDirectory,
                cancellationToken);

            string installedBin = Path.Combine(installDirectory, "bin");
            string installedExecutable = Path.Combine(installedBin, Program.GameExecutableName);
            if (!File.Exists(installedExecutable))
            {
                throw new InvalidOperationException(
                    "The native build completed without producing forty-winks-recomp.exe.");
            }

            Directory.CreateDirectory(outputDirectory);
            CopyDirectory(installedBin, outputDirectory);
            File.WriteAllText(
                Path.Combine(outputDirectory, "README.txt"),
                """
                40 Winks PC Port - private playable build

                Start the game with 40-Winks-PC-Port.exe when it is present.
                Keep your legally dumped ROM in a location you control. It is not copied here.

                F1 opens the debug and display menu.
                """ + Environment.NewLine,
                new UTF8Encoding(false));

            Report(100, "Playable build ready");
        }
        finally
        {
            log = null;
        }
    }

    public async Task InstallBuildToolsAsync(CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Program.LogPath)!);
        using StreamWriter installLog = new(Program.LogPath, true, new UTF8Encoding(false));
        log = installLog;
        try
        {
            LoadCurrentEnvironment();
            string? winget = FindExecutable("winget.exe");
            if (winget is null)
            {
                throw new InvalidOperationException(
                    "Windows Package Manager was not found. Install 'App Installer' from Microsoft, then reopen this setup.");
            }

            (string Name, string Id, string[] ExtraArguments)[] packages =
            {
                ("Git", "Git.Git", Array.Empty<string>()),
                ("Python", "Python.Python.3.13", Array.Empty<string>()),
                ("CMake", "Kitware.CMake", Array.Empty<string>()),
                ("Ninja", "Ninja-build.Ninja", Array.Empty<string>()),
                (
                    "Visual Studio C++ Build Tools",
                    "Microsoft.VisualStudio.2022.BuildTools",
                    new[]
                    {
                        "--override",
                        "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --includeRecommended",
                    }),
            };

            for (int index = 0; index < packages.Length; index++)
            {
                (string name, string id, string[] extraArguments) = packages[index];
                int percent = 5 + (index * 85 / packages.Length);
                Report(percent, $"Installing {name}");
                List<string> arguments = new()
                {
                    "install",
                    "--id", id,
                    "--exact",
                    "--source", "winget",
                    "--silent",
                    "--disable-interactivity",
                    "--accept-package-agreements",
                    "--accept-source-agreements",
                };
                arguments.AddRange(extraArguments);
                await RunWithRetriesAsync(
                    winget,
                    arguments,
                    Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
                    cancellationToken,
                    $"Installing {name}",
                    attempts: 2);
            }

            Report(100, "Build tools installed");
        }
        finally
        {
            log = null;
        }
    }

    public void Cancel()
    {
        lock (processLock)
        {
            if (activeProcess is null)
            {
                return;
            }

            try
            {
                if (!activeProcess.HasExited)
                {
                    activeProcess.Kill(true);
                }
            }
            catch
            {
                // The process may finish while the window is closing.
            }
        }
    }

    private async Task FindBuildRequirementsAsync(CancellationToken cancellationToken)
    {
        LoadCurrentEnvironment();
        bool hasVisualStudio = await ImportVisualStudioEnvironmentAsync(cancellationToken);
        List<string> missing = new();
        if (!hasVisualStudio)
        {
            missing.Add("Visual Studio 2022 C++ Build Tools");
        }

        git = RequireExecutable("git.exe", "Git", missing);
        cmake = RequireExecutable("cmake.exe", "CMake", missing);
        RequireExecutable("ninja.exe", "Ninja", missing);
        RequireExecutable("clang-cl.exe", "Clang compiler", missing);
        python = await FindPythonAsync(cancellationToken);
        if (python is null)
        {
            missing.Add("Python 3");
        }

        if (missing.Count != 0)
        {
            throw new MissingBuildToolsException(missing.Distinct().ToArray());
        }
    }

    private void LoadCurrentEnvironment()
    {
        environment.Clear();
        foreach (DictionaryEntry entry in Environment.GetEnvironmentVariables())
        {
            if (entry.Key is string key && entry.Value is string value)
            {
                environment[key] = value;
            }
        }

        string? machinePath = Environment.GetEnvironmentVariable(
            "Path", EnvironmentVariableTarget.Machine);
        string? userPath = Environment.GetEnvironmentVariable(
            "Path", EnvironmentVariableTarget.User);
        environment["Path"] = string.Join(
            Path.PathSeparator,
            new[] { machinePath, userPath, environment.GetValueOrDefault("Path") }
                .Where(value => !string.IsNullOrWhiteSpace(value)));
    }

    private async Task<bool> ImportVisualStudioEnvironmentAsync(
        CancellationToken cancellationToken)
    {
        string vswhere = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            "Microsoft Visual Studio",
            "Installer",
            "vswhere.exe");
        if (!File.Exists(vswhere))
        {
            return false;
        }

        ProcessResult locationResult = await RunProcessAsync(
            vswhere,
            new[]
            {
                "-latest",
                "-products", "*",
                "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property", "installationPath",
            },
            Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
            cancellationToken,
            false);
        string? installation = locationResult.Output
            .Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
            .Select(line => line.Trim())
            .FirstOrDefault(line => line.Length != 0);
        if (locationResult.ExitCode != 0 || string.IsNullOrWhiteSpace(installation))
        {
            return false;
        }

        string devCommand = Path.Combine(installation, "Common7", "Tools", "VsDevCmd.bat");
        if (!File.Exists(devCommand))
        {
            return false;
        }

        string commandInterpreter = environment.GetValueOrDefault("ComSpec") ?? "cmd.exe";
        string temporaryDirectory = Path.GetTempPath();
        string scriptName = $"forty-winks-vs-env-{Guid.NewGuid():N}.cmd";
        string scriptPath = Path.Combine(temporaryDirectory, scriptName);
        File.WriteAllText(
            scriptPath,
            $"@echo off\r\ncall \"{devCommand}\" -no_logo -arch=x64 -host_arch=x64 >nul\r\n" +
            "if errorlevel 1 exit /b 1\r\nset\r\n",
            new UTF8Encoding(false));
        ProcessResult environmentResult;
        try
        {
            environmentResult = await RunProcessAsync(
                commandInterpreter,
                new[] { "/d", "/q", "/c", scriptName },
                temporaryDirectory,
                cancellationToken,
                false);
        }
        finally
        {
            File.Delete(scriptPath);
        }
        if (environmentResult.ExitCode != 0)
        {
            return false;
        }

        foreach (string line in environmentResult.Output.Split(new[] { '\r', '\n' }))
        {
            int separator = line.IndexOf('=');
            if (separator > 0)
            {
                environment[line[..separator]] = line[(separator + 1)..];
            }
        }
        return true;
    }

    private async Task<PythonCommand?> FindPythonAsync(CancellationToken cancellationToken)
    {
        string? py = FindExecutable("py.exe");
        if (py is not null)
        {
            ProcessResult result = await RunProcessAsync(
                py,
                new[] { "-3", "--version" },
                Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
                cancellationToken,
                false);
            if (result.ExitCode == 0)
            {
                return new PythonCommand(py, new[] { "-3" });
            }
        }

        string? pythonExecutable = FindExecutable("python.exe");
        if (pythonExecutable is not null)
        {
            ProcessResult result = await RunProcessAsync(
                pythonExecutable,
                new[] { "--version" },
                Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
                cancellationToken,
                false);
            if (result.ExitCode == 0)
            {
                return new PythonCommand(pythonExecutable, Array.Empty<string>());
            }
        }
        return null;
    }

    private string RequireExecutable(string name, string displayName, List<string> missing)
    {
        string? executable = FindExecutable(name);
        if (executable is null)
        {
            missing.Add(displayName);
            return name;
        }
        return executable;
    }

    private string? FindExecutable(string name)
    {
        if (Path.IsPathRooted(name))
        {
            return File.Exists(name) ? name : null;
        }

        string? path = environment.GetValueOrDefault("Path");
        if (string.IsNullOrWhiteSpace(path))
        {
            return null;
        }

        foreach (string directory in path.Split(Path.PathSeparator))
        {
            string cleanDirectory = directory.Trim().Trim('"');
            if (cleanDirectory.Length == 0)
            {
                continue;
            }
            try
            {
                string candidate = Path.Combine(cleanDirectory, name);
                if (File.Exists(candidate))
                {
                    return candidate;
                }
            }
            catch (ArgumentException)
            {
                // Ignore malformed PATH entries from third-party installers.
            }
        }
        return null;
    }

    private async Task CheckoutDependencyAsync(
        Dependency dependency,
        string externalDirectory,
        string sourceDirectory,
        int progress,
        CancellationToken cancellationToken)
    {
        string directory = Path.Combine(externalDirectory, dependency.Name);
        Report(progress, $"Downloading {dependency.Name}");

        if (!Directory.Exists(Path.Combine(directory, ".git")))
        {
            DeleteDirectory(directory);
            await RunWithRetriesAsync(
                git,
                new[]
                {
                    "-c", "core.autocrlf=false",
                    "-c", "core.longpaths=true",
                    "clone", "--filter=blob:none",
                    dependency.Url,
                    directory,
                },
                sourceDirectory,
                cancellationToken,
                $"Downloading {dependency.Name}",
                attempt =>
                {
                    if (attempt > 1)
                    {
                        DeleteDirectory(directory);
                    }
                });
        }

        await RunRequiredAsync(
            git,
            new[] { "-C", directory, "config", "core.autocrlf", "false" },
            sourceDirectory,
            cancellationToken);
        await RunRequiredAsync(
            git,
            new[] { "-C", directory, "config", "core.longpaths", "true" },
            sourceDirectory,
            cancellationToken);

        ProcessResult headResult = await RunProcessAsync(
            git,
            new[] { "-C", directory, "rev-parse", "HEAD" },
            sourceDirectory,
            cancellationToken,
            false);
        string head = headResult.Output.Trim();
        if (headResult.ExitCode != 0 ||
            !head.Equals(dependency.Revision, StringComparison.OrdinalIgnoreCase))
        {
            ProcessResult statusResult = await RunProcessAsync(
                git,
                new[] { "-C", directory, "status", "--porcelain" },
                sourceDirectory,
                cancellationToken,
                false);
            if (statusResult.ExitCode != 0)
            {
                throw new InvalidOperationException($"Could not inspect {dependency.Name}.");
            }
            if (!string.IsNullOrWhiteSpace(statusResult.Output))
            {
                throw new InvalidOperationException(
                    $"{dependency.Name} has unexpected local changes in the private build cache.");
            }

            await RunWithRetriesAsync(
                git,
                new[]
                {
                    "-C", directory,
                    "fetch", "--depth", "1", "origin", dependency.Revision,
                },
                sourceDirectory,
                cancellationToken,
                $"Downloading {dependency.Name}");
            await RunRequiredAsync(
                git,
                new[] { "-C", directory, "checkout", "--detach", dependency.Revision },
                sourceDirectory,
                cancellationToken);
        }

        await RunWithRetriesAsync(
            git,
            new[]
            {
                "-C", directory,
                "submodule", "update", "--init", "--recursive", "--depth", "1",
            },
            sourceDirectory,
            cancellationToken,
            $"Downloading {dependency.Name} components");
    }

    private async Task ApplyPatchOnceAsync(
        string name,
        string patchPath,
        string externalDirectory,
        string sourceDirectory,
        CancellationToken cancellationToken)
    {
        string directory = Path.Combine(externalDirectory, name);
        ProcessResult alreadyApplied = await RunProcessAsync(
            git,
            new[] { "-C", directory, "apply", "--reverse", "--check", patchPath },
            sourceDirectory,
            cancellationToken,
            false);
        if (alreadyApplied.ExitCode == 0)
        {
            WriteOutput($"{name} compatibility patch is already applied.");
            return;
        }

        ProcessResult canApply = await RunProcessAsync(
            git,
            new[] { "-C", directory, "apply", "--check", patchPath },
            sourceDirectory,
            cancellationToken,
            false);
        if (canApply.ExitCode != 0)
        {
            throw new InvalidOperationException(
                $"The {name} compatibility patch does not apply to the pinned revision.");
        }

        await RunRequiredAsync(
            git,
            new[] { "-C", directory, "apply", patchPath },
            sourceDirectory,
            cancellationToken);
        WriteOutput($"Applied {name} compatibility patch.");
    }

    private async Task RunWithRetriesAsync(
        string fileName,
        IReadOnlyList<string> arguments,
        string workingDirectory,
        CancellationToken cancellationToken,
        string operation,
        Action<int>? prepareAttempt = null,
        int attempts = 3)
    {
        for (int attempt = 1; attempt <= attempts; attempt++)
        {
            prepareAttempt?.Invoke(attempt);
            ProcessResult result = await RunProcessAsync(
                fileName, arguments, workingDirectory, cancellationToken, true);
            if (result.ExitCode == 0)
            {
                return;
            }
            if (attempt == attempts)
            {
                throw new InvalidOperationException(
                    $"{operation} failed after {attempts} attempts. See the build log at {Program.LogPath}.");
            }

            WriteOutput($"{operation} was interrupted; retrying ({attempt + 1}/{attempts})...");
            await Task.Delay(TimeSpan.FromSeconds(attempt * 2), cancellationToken);
        }
    }

    private async Task RunRequiredAsync(
        string fileName,
        IReadOnlyList<string> arguments,
        string workingDirectory,
        CancellationToken cancellationToken,
        int progressBase = -1,
        int progressSpan = 0,
        string progressMessage = "Building")
    {
        ProcessResult result = await RunProcessAsync(
            fileName,
            arguments,
            workingDirectory,
            cancellationToken,
            true,
            progressBase,
            progressSpan,
            progressMessage);
        if (result.ExitCode != 0)
        {
            throw new InvalidOperationException(
                $"{Path.GetFileName(fileName)} stopped with exit code {result.ExitCode}. See the build log at {Program.LogPath}.");
        }
    }

    private async Task<ProcessResult> RunProcessAsync(
        string fileName,
        IReadOnlyList<string> arguments,
        string workingDirectory,
        CancellationToken cancellationToken,
        bool forwardOutput,
        int progressBase = -1,
        int progressSpan = 0,
        string progressMessage = "Building")
    {
        cancellationToken.ThrowIfCancellationRequested();
        ProcessStartInfo startInfo = new(fileName)
        {
            WorkingDirectory = workingDirectory,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        foreach (string argument in arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }
        startInfo.Environment.Clear();
        foreach ((string key, string value) in environment)
        {
            startInfo.Environment[key] = value;
        }

        WriteLog($"> {Path.GetFileName(fileName)} {FormatArguments(arguments)}");
        StringBuilder output = new();
        object outputLock = new();
        int lastReported = -1;
        void ReceiveLine(string? line)
        {
            if (line is null)
            {
                return;
            }
            lock (outputLock)
            {
                output.AppendLine(line);
            }
            WriteLog(line);
            if (forwardOutput)
            {
                OutputReceived?.Invoke(line);
            }
            if (progressBase >= 0)
            {
                Match match = NinjaProgress.Match(line);
                if (match.Success &&
                    int.TryParse(match.Groups[1].Value, out int current) &&
                    int.TryParse(match.Groups[2].Value, out int total))
                {
                    int mapped = progressBase +
                        (progressSpan * current / Math.Max(total, 1));
                    if (mapped > lastReported)
                    {
                        lastReported = mapped;
                        Report(mapped, progressMessage);
                    }
                }
            }
        }

        using Process process = new() { StartInfo = startInfo };
        process.OutputDataReceived += (_, eventArgs) => ReceiveLine(eventArgs.Data);
        process.ErrorDataReceived += (_, eventArgs) => ReceiveLine(eventArgs.Data);
        try
        {
            process.Start();
            lock (processLock)
            {
                activeProcess = process;
            }
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            try
            {
                await process.WaitForExitAsync(cancellationToken);
            }
            catch (OperationCanceledException)
            {
                try
                {
                    process.Kill(true);
                }
                catch
                {
                    // The process may have exited at the same time as cancellation.
                }
                throw;
            }
            process.WaitForExit();
            lock (outputLock)
            {
                return new ProcessResult(process.ExitCode, output.ToString());
            }
        }
        finally
        {
            lock (processLock)
            {
                if (ReferenceEquals(activeProcess, process))
                {
                    activeProcess = null;
                }
            }
        }
    }

    private static string FormatArguments(IReadOnlyList<string> arguments)
    {
        return string.Join(" ", arguments.Select(argument =>
            argument.Any(char.IsWhiteSpace) ? $"\"{argument.Replace("\"", "\\\"")}\"" : argument));
    }

    private void Report(int percent, string message)
    {
        WriteOutput($"{percent}% - {message}");
        ProgressChanged?.Invoke(Math.Clamp(percent, 0, 100), message);
    }

    private void WriteOutput(string line)
    {
        WriteLog(line);
        OutputReceived?.Invoke(line);
    }

    private void WriteLog(string line)
    {
        lock (logLock)
        {
            log?.WriteLine(line);
            log?.Flush();
        }
    }

    private static void ValidatePaths(
        string sourceDirectory,
        string romPath,
        string outputDirectory)
    {
        if (!File.Exists(romPath))
        {
            throw new FileNotFoundException("The selected ROM was not found.", romPath);
        }
        if (PathsOverlap(sourceDirectory, outputDirectory))
        {
            throw new InvalidOperationException(
                "Choose a playable build folder outside the private source cache.");
        }
    }

    private static bool PathsOverlap(string first, string second)
    {
        string firstWithSeparator = Path.TrimEndingDirectorySeparator(first) +
            Path.DirectorySeparatorChar;
        string secondWithSeparator = Path.TrimEndingDirectorySeparator(second) +
            Path.DirectorySeparatorChar;
        return firstWithSeparator.StartsWith(
                   secondWithSeparator, StringComparison.OrdinalIgnoreCase) ||
               secondWithSeparator.StartsWith(
                   firstWithSeparator, StringComparison.OrdinalIgnoreCase);
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

    private static void CopyDirectory(string source, string destination)
    {
        foreach (string directory in Directory.EnumerateDirectories(
                     source, "*", SearchOption.AllDirectories))
        {
            Directory.CreateDirectory(Path.Combine(
                destination, Path.GetRelativePath(source, directory)));
        }
        foreach (string file in Directory.EnumerateFiles(
                     source, "*", SearchOption.AllDirectories))
        {
            string target = Path.Combine(destination, Path.GetRelativePath(source, file));
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(file, target, true);
        }
    }
}
