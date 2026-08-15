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
    private readonly ManagedToolchain managedToolchain = new();
    private readonly object processLock = new();
    private readonly object logLock = new();
    private Process? activeProcess;
    private StreamWriter? log;
    private string git = "git.exe";
    private string cmake = "cmake.exe";
    private string ninja = "ninja.exe";
    private string cCompiler = "x86_64-w64-mingw32-clang.exe";
    private string cxxCompiler = "x86_64-w64-mingw32-clang++.exe";
    private string resourceCompiler = "x86_64-w64-mingw32-windres.exe";
    private string archiver = "llvm-ar.exe";
    private string ranlib = "llvm-ranlib.exe";
    private PythonCommand? python;

    public event Action<int, string>? ProgressChanged;
    public event Action<string>? OutputReceived;

    public NativeBuildPipeline()
    {
        managedToolchain.ProgressChanged += (percent, message) =>
            ProgressChanged?.Invoke(percent, message);
        managedToolchain.OutputReceived += WriteOutput;
    }

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

    public async Task InstallPortableToolsAsync(CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Program.LogPath)!);
        using StreamWriter installLog = new(Program.LogPath, true, new UTF8Encoding(false));
        log = installLog;
        try
        {
            await managedToolchain.InstallPortableToolsAsync(cancellationToken);
            Report(100, "Portable build tools ready");
        }
        finally
        {
            log = null;
        }
    }

    public async Task RunToolchainSmokeTestAsync(CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Program.LogPath)!);
        using StreamWriter testLog = new(Program.LogPath, true, new UTF8Encoding(false));
        log = testLog;
        try
        {
            await FindBuildRequirementsAsync(cancellationToken);
            string sourceDirectory = Path.Combine(Program.BuildCacheDirectory, "toolchain-test");
            string buildDirectory = Path.Combine(sourceDirectory, "build");
            DeleteDirectory(sourceDirectory);
            Directory.CreateDirectory(sourceDirectory);
            File.WriteAllText(
                Path.Combine(sourceDirectory, "CMakeLists.txt"),
                """
                cmake_minimum_required(VERSION 3.20)
                project(forty_winks_toolchain_test LANGUAGES C CXX RC)
                add_library(forty-winks-toolchain-c STATIC smoke.c)
                add_executable(forty-winks-toolchain-test main.cpp resource.rc)
                target_link_libraries(forty-winks-toolchain-test PRIVATE
                    forty-winks-toolchain-c d3d12 dxgi dxguid)
                if(MINGW)
                    target_link_options(forty-winks-toolchain-test PRIVATE -static)
                endif()
                """ + Environment.NewLine,
                Encoding.ASCII);
            File.WriteAllText(
                Path.Combine(sourceDirectory, "smoke.c"),
                "int forty_winks_smoke_c(void) { return 40; }" + Environment.NewLine,
                Encoding.ASCII);
            File.WriteAllText(
                Path.Combine(sourceDirectory, "main.cpp"),
                """
                #include <windows.h>
                #include <d3d12.h>
                #include <dxgi1_6.h>

                extern "C" int forty_winks_smoke_c(void);

                int main() {
                    IDXGIFactory1* factory = nullptr;
                    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
                        factory->Release();
                    }
                    return forty_winks_smoke_c() == 40 ? 0 : 1;
                }
                """ + Environment.NewLine,
                Encoding.ASCII);
            File.WriteAllText(
                Path.Combine(sourceDirectory, "resource.rc"),
                """
                1 VERSIONINFO
                FILEVERSION 1,0,0,0
                PRODUCTVERSION 1,0,0,0
                FILEOS 0x40004
                FILETYPE 0x1
                BEGIN
                    BLOCK "StringFileInfo"
                    BEGIN
                        BLOCK "040904b0"
                        BEGIN
                            VALUE "ProductName", "40 Winks toolchain test"
                        END
                    END
                END
                """ + Environment.NewLine,
                Encoding.ASCII);

            List<string> configureArguments = new()
            {
                "-S", sourceDirectory,
                "-B", buildDirectory,
                "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
            };
            configureArguments.AddRange(PortableCMakeArguments());
            await RunRequiredAsync(
                cmake,
                configureArguments,
                sourceDirectory,
                cancellationToken);
            await RunRequiredAsync(
                cmake,
                new[] { "--build", buildDirectory, "--parallel", "2" },
                sourceDirectory,
                cancellationToken);
            await RunRequiredAsync(
                Path.Combine(buildDirectory, "forty-winks-toolchain-test.exe"),
                Array.Empty<string>(),
                buildDirectory,
                cancellationToken);
            Report(100, "Managed Windows toolchain test passed");
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
            string buildDirectory = Path.Combine(
                sourceDirectory, "build", "windows-portable-v2-release");
            string installDirectory = Path.Combine(
                sourceDirectory, "build", "windows-portable-v2-install");
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
                Path.Combine(externalDirectory, "N64ModernRuntime"),
                sourceDirectory,
                cancellationToken);
            await ApplyPatchOnceAsync(
                "N64ModernRuntime portable Windows",
                Path.Combine(
                    sourceDirectory,
                    "patches",
                    "N64ModernRuntime-portable-windows.patch"),
                Path.Combine(externalDirectory, "N64ModernRuntime"),
                sourceDirectory,
                cancellationToken);
            await ApplyPatchOnceAsync(
                "rt64",
                Path.Combine(sourceDirectory, "patches", "RT64.patch"),
                Path.Combine(externalDirectory, "rt64"),
                sourceDirectory,
                cancellationToken);
            await ApplyPatchOnceAsync(
                "rt64 portable Windows",
                Path.Combine(sourceDirectory, "patches", "RT64-portable-windows.patch"),
                Path.Combine(externalDirectory, "rt64"),
                sourceDirectory,
                cancellationToken);
            await ApplyPatchOnceAsync(
                "rt64 SDL2",
                Path.Combine(sourceDirectory, "patches", "SDL2.patch"),
                Path.Combine(
                    externalDirectory,
                    "rt64",
                    "src",
                    "contrib",
                    "mupen64plus-win32-deps"),
                sourceDirectory,
                cancellationToken);
            await ApplyPatchOnceAsync(
                "rt64 Plume portable Windows",
                Path.Combine(sourceDirectory, "patches", "Plume-portable-windows.patch"),
                Path.Combine(externalDirectory, "rt64", "src", "contrib", "plume"),
                sourceDirectory,
                cancellationToken);
            await ApplyPatchOnceAsync(
                "rt64 D3D12 allocator portable Windows",
                Path.Combine(
                    sourceDirectory,
                    "patches",
                    "D3D12MemoryAllocator-portable-windows.patch"),
                Path.Combine(
                    externalDirectory,
                    "rt64",
                    "src",
                    "contrib",
                    "plume",
                    "contrib",
                    "D3D12MemoryAllocator"),
                sourceDirectory,
                cancellationToken);

            string n64RecompDirectory = Path.Combine(externalDirectory, "N64Recomp");
            string n64RecompBuildDirectory = Path.Combine(
                n64RecompDirectory, "build-windows-portable-v2");
            Report(35, "Configuring CPU translator");
            List<string> n64RecompConfigureArguments = new()
            {
                "-S", n64RecompDirectory,
                "-B", n64RecompBuildDirectory,
                "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
            };
            n64RecompConfigureArguments.AddRange(PortableCMakeArguments());
            await RunRequiredAsync(
                cmake,
                n64RecompConfigureArguments,
                sourceDirectory,
                cancellationToken);

            Report(40, "Building CPU translator");
            await RunRequiredAsync(
                cmake,
                new[]
                {
                    "--build", n64RecompBuildDirectory,
                    "--parallel", jobs.ToString(),
                    "--target", "N64RecompCLI", "RSPRecomp",
                },
                sourceDirectory,
                cancellationToken,
                40,
                10,
                "Building CPU translator");

            string n64Recomp = Path.Combine(n64RecompBuildDirectory, "N64Recomp.exe");
            string rspRecomp = Path.Combine(n64RecompBuildDirectory, "RSPRecomp.exe");
            if (!File.Exists(n64Recomp))
            {
                throw new InvalidOperationException(
                    $"N64Recomp did not produce the expected program at {n64Recomp}.");
            }
            if (!File.Exists(rspRecomp))
            {
                throw new InvalidOperationException(
                    $"RSPRecomp did not produce the expected program at {rspRecomp}.");
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

                Report(58, "Generating native audio processor");
                Directory.CreateDirectory(Path.Combine(generatedDirectory, "rsp"));
                await RunRequiredAsync(
                    rspRecomp,
                    new[] { "aspMain.toml" },
                    Path.Combine(sourceDirectory, "recomp"),
                    cancellationToken);
            }
            finally
            {
                File.Delete(baseRom);
            }

            Report(60, "Configuring playable game");
            List<string> gameConfigureArguments = new()
            {
                "-S", Path.Combine(sourceDirectory, "recomp-port"),
                "-B", buildDirectory,
                "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DBUILD_TESTING=OFF",
            };
            gameConfigureArguments.AddRange(PortableCMakeArguments());
            await RunRequiredAsync(
                cmake,
                gameConfigureArguments,
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
            await managedToolchain.InstallPortableToolsAsync(cancellationToken);
            Report(96, "Checking private portable toolchain");
            await FindBuildRequirementsAsync(cancellationToken);
            Report(100, "Private portable build tools ready");
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
        ManagedToolPaths? managedTools = managedToolchain.FindInstalled();
        if (managedTools is not null)
        {
            ConfigureManagedTools(managedTools);
        }

        List<string> missing = new();
        if (managedTools is null)
        {
            missing.Add("private portable Git, CMake, Ninja, Python, compiler, and Windows SDK");
        }
        else
        {
            await ValidateManagedToolAsync(
                git, new[] { "--version" }, "portable Git", missing, cancellationToken);
            await ValidateManagedToolAsync(
                cmake, new[] { "--version" }, "portable CMake", missing, cancellationToken);
            await ValidateManagedToolAsync(
                ninja, new[] { "--version" }, "portable Ninja", missing, cancellationToken);
            await ValidateManagedToolAsync(
                python!.FileName,
                new[] { "--version" },
                "portable Python",
                missing,
                cancellationToken);
            await ValidateManagedToolAsync(
                cCompiler,
                new[] { "--version" },
                "portable C compiler",
                missing,
                cancellationToken);
            await ValidateManagedToolAsync(
                cxxCompiler,
                new[] { "--version" },
                "portable C++ compiler",
                missing,
                cancellationToken);
            await ValidateManagedToolAsync(
                resourceCompiler,
                new[] { "--version" },
                "portable Windows resource compiler",
                missing,
                cancellationToken);
            await ValidateManagedToolAsync(
                archiver,
                new[] { "--version" },
                "portable archiver",
                missing,
                cancellationToken);
            await ValidateManagedToolAsync(
                ranlib,
                new[] { "--version" },
                "portable archive indexer",
                missing,
                cancellationToken);
        }

        if (missing.Count != 0)
        {
            throw new MissingBuildToolsException(missing.Distinct().ToArray());
        }
    }

    private async Task ValidateManagedToolAsync(
        string fileName,
        IReadOnlyList<string> arguments,
        string displayName,
        List<string> missing,
        CancellationToken cancellationToken)
    {
        ProcessResult result = await RunProcessAsync(
            fileName,
            arguments,
            Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
            cancellationToken,
            false);
        if (result.ExitCode != 0)
        {
            missing.Add(displayName);
        }
    }

    private void ConfigureManagedTools(ManagedToolPaths paths)
    {
        git = paths.Git;
        cmake = paths.CMake;
        ninja = paths.Ninja;
        python = new PythonCommand(paths.Python, Array.Empty<string>());
        cCompiler = paths.CCompiler;
        cxxCompiler = paths.CxxCompiler;
        resourceCompiler = paths.ResourceCompiler;
        archiver = paths.Archiver;
        ranlib = paths.Ranlib;

        string windowsDirectory = environment.GetValueOrDefault("SystemRoot") ??
            Environment.GetFolderPath(Environment.SpecialFolder.Windows);
        string systemDirectory = Environment.SystemDirectory;
        environment["Path"] = string.Join(
            Path.PathSeparator,
            new[]
            {
                paths.CompilerBinDirectory,
                Path.GetDirectoryName(paths.Git),
                Path.GetDirectoryName(paths.CMake),
                Path.GetDirectoryName(paths.Ninja),
                Path.GetDirectoryName(paths.Python),
                systemDirectory,
                windowsDirectory,
            }
            .Where(directory => !string.IsNullOrWhiteSpace(directory))
            .Distinct(StringComparer.OrdinalIgnoreCase));
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
        environment["Path"] = "";
    }

    private string[] PortableCMakeArguments() =>
        new[]
        {
            $"-DCMAKE_C_COMPILER={CMakePath(cCompiler)}",
            $"-DCMAKE_CXX_COMPILER={CMakePath(cxxCompiler)}",
            $"-DCMAKE_RC_COMPILER={CMakePath(resourceCompiler)}",
            $"-DCMAKE_AR={CMakePath(archiver)}",
            $"-DCMAKE_RANLIB={CMakePath(ranlib)}",
            $"-DCMAKE_MAKE_PROGRAM={CMakePath(ninja)}",
            "-DCMAKE_EXE_LINKER_FLAGS=-static",
        };

    private static string CMakePath(string path) => path.Replace('\\', '/');

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
        string directory,
        string sourceDirectory,
        CancellationToken cancellationToken)
    {
        ProcessResult alreadyApplied = await RunProcessAsync(
            git,
            new[]
            {
                "-C", directory, "apply", "--ignore-whitespace",
                "--reverse", "--check", patchPath,
            },
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
            new[]
            {
                "-C", directory, "apply", "--ignore-whitespace", "--check", patchPath,
            },
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
            new[] { "-C", directory, "apply", "--ignore-whitespace", patchPath },
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
