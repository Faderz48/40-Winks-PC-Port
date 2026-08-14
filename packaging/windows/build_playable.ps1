[CmdletBinding()]
param(
    [string]$RomPath,
    [string]$OutputDirectory,
    [int]$Jobs = 2,
    [switch]$CheckOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$ExpectedSha256 = "057232ef7618e25f5645df50d3cd45f08cb5a2cccb3e2fdf48faa8755c4ddb1a"
$RootDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$ExternalDir = Join-Path $RootDir "work\external"
$BuildDir = Join-Path $RootDir "build\windows-release"
$InstallDir = Join-Path $RootDir "build\windows-install"

$Dependencies = @(
    @{
        Name = "N64Recomp"
        Url = "https://github.com/N64Recomp/N64Recomp.git"
        Revision = "ffb39cdad1da5de07eaaa48bd1db4a89a7986771"
    },
    @{
        Name = "N64ModernRuntime"
        Url = "https://github.com/N64Recomp/N64ModernRuntime.git"
        Revision = "ae1ffbb909d9f93c88c41830deb539f7feef5ed2"
    },
    @{
        Name = "rt64"
        Url = "https://github.com/rt64/rt64.git"
        Revision = "f0728a2520d5aa735886240de3fee75cc805f6d6"
    }
)

function Write-Stage {
    param([int]$Percent, [string]$Message)
    Write-Output ("FORTY_WINKS_PROGRESS|{0}|{1}" -f $Percent, $Message)
}

function Refresh-ProcessPath {
    $machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = (@($machinePath, $userPath) | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_)
    }) -join ';'
}

function Import-VisualStudioEnvironment {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        return $false
    }

    $installation = (& $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($installation)) {
        return $false
    }

    $devCommand = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $devCommand)) {
        return $false
    }

    $command = '"{0}" -no_logo -arch=x64 -host_arch=x64 >nul && set' -f $devCommand
    $environmentLines = & $env:ComSpec /s /c $command
    if ($LASTEXITCODE -ne 0) {
        return $false
    }

    foreach ($line in $environmentLines) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            [Environment]::SetEnvironmentVariable($name, $value, "Process")
        }
    }
    return $true
}

function Find-Python {
    if (Get-Command py.exe -ErrorAction SilentlyContinue) {
        & py.exe -3 --version *> $null
        if ($LASTEXITCODE -eq 0) {
            return @{ File = "py.exe"; Prefix = @("-3") }
        }
    }
    if (Get-Command python.exe -ErrorAction SilentlyContinue) {
        & python.exe --version *> $null
        if ($LASTEXITCODE -eq 0) {
            return @{ File = "python.exe"; Prefix = @() }
        }
    }
    return $null
}

function Assert-BuildRequirements {
    Refresh-ProcessPath
    $missing = New-Object System.Collections.Generic.List[string]
    if (-not (Import-VisualStudioEnvironment)) {
        $missing.Add("Visual Studio 2022 C++ Build Tools")
    }

    foreach ($tool in @("git.exe", "cmake.exe", "ninja.exe", "clang-cl.exe")) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            $missing.Add($tool.Replace(".exe", ""))
        }
    }

    $script:Python = Find-Python
    if ($null -eq $script:Python) {
        $missing.Add("Python 3")
    }

    if ($missing.Count -ne 0) {
        $missingText = $missing -join ", "
        Write-Output ("FORTY_WINKS_MISSING_TOOLS|{0}" -f $missingText)
        throw "Required Windows build tools are missing: $missingText"
    }
}

function Invoke-Native {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory = $RootDir,
        [int]$ProgressBase = -1,
        [int]$ProgressSpan = 0,
        [string]$ProgressMessage = "Building"
    )

    Push-Location $WorkingDirectory
    try {
        $lastReported = -1
        & $FilePath @Arguments 2>&1 | ForEach-Object {
            $line = "$_"
            Write-Output $line
            if ($ProgressBase -ge 0 -and $line -match '^\[\s*(\d+)\s*/\s*(\d+)\]') {
                $current = [int]$Matches[1]
                $total = [Math]::Max([int]$Matches[2], 1)
                $mapped = $ProgressBase + [int][Math]::Floor(
                    $ProgressSpan * $current / $total)
                if ($mapped -gt $lastReported) {
                    Write-Stage $mapped $ProgressMessage
                    $lastReported = $mapped
                }
            }
        }
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0) {
            throw "$FilePath failed with exit code $exitCode."
        }
    }
    finally {
        Pop-Location
    }
}

function Checkout-Dependency {
    param([hashtable]$Dependency, [int]$Progress)

    $name = $Dependency.Name
    $directory = Join-Path $ExternalDir $name
    Write-Stage $Progress ("Downloading {0}" -f $name)

    if (-not (Test-Path -LiteralPath (Join-Path $directory ".git"))) {
        if (Test-Path -LiteralPath $directory) {
            Remove-Item -LiteralPath $directory -Recurse -Force
        }
        Invoke-Native "git.exe" @(
            "-c", "core.autocrlf=false", "-c", "core.longpaths=true",
            "clone", "--filter=blob:none",
            $Dependency.Url, $directory
        )
    }

    Invoke-Native "git.exe" @("-C", $directory, "config", "core.autocrlf", "false")
    Invoke-Native "git.exe" @("-C", $directory, "config", "core.longpaths", "true")
    $head = (& git.exe -C $directory rev-parse HEAD 2>$null | Select-Object -First 1)
    if ($null -eq $head -or $head.Trim() -ne $Dependency.Revision) {
        $status = ((& git.exe -C $directory status --porcelain) -join "`n").Trim()
        if (-not [string]::IsNullOrWhiteSpace($status)) {
            throw "$name has unexpected local changes in the private build cache."
        }
        Invoke-Native "git.exe" @(
            "-C", $directory, "fetch", "--depth", "1", "origin", $Dependency.Revision
        )
        Invoke-Native "git.exe" @(
            "-C", $directory, "checkout", "--detach", $Dependency.Revision
        )
    }

    Invoke-Native "git.exe" @(
        "-C", $directory, "submodule", "update", "--init", "--recursive", "--depth", "1"
    )
}

function Apply-PatchOnce {
    param([string]$Name, [string]$PatchPath)

    $directory = Join-Path $ExternalDir $Name
    & git.exe -C $directory apply --reverse --check $PatchPath *> $null
    if ($LASTEXITCODE -eq 0) {
        Write-Output "$Name compatibility patch is already applied."
        return
    }

    & git.exe -C $directory apply --check $PatchPath *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "The $Name compatibility patch does not apply to the pinned revision."
    }
    Invoke-Native "git.exe" @("-C", $directory, "apply", $PatchPath)
    Write-Output "Applied $Name compatibility patch."
}

Assert-BuildRequirements
if ($CheckOnly) {
    Write-Output "FORTY_WINKS_REQUIREMENTS_OK"
    exit 0
}

if ([string]::IsNullOrWhiteSpace($RomPath) -or
        [string]::IsNullOrWhiteSpace($OutputDirectory)) {
    throw "RomPath and OutputDirectory are required."
}

$RomPath = [IO.Path]::GetFullPath($RomPath)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$rootWithSeparator = $RootDir.TrimEnd('\') + '\'
$outputWithSeparator = $OutputDirectory.TrimEnd('\') + '\'
if ($outputWithSeparator.StartsWith($rootWithSeparator,
        [StringComparison]::OrdinalIgnoreCase) -or
        $rootWithSeparator.StartsWith($outputWithSeparator,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Choose a playable build folder outside the private source cache."
}

if (-not (Test-Path -LiteralPath $RomPath -PathType Leaf)) {
    throw "The selected ROM was not found: $RomPath"
}

Write-Stage 2 "Verifying ROM"
$actualHash = (Get-FileHash -LiteralPath $RomPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $ExpectedSha256) {
    throw "Unsupported ROM revision. Expected SHA-256 $ExpectedSha256, got $actualHash."
}

$Jobs = [Math]::Max(1, [Math]::Min($Jobs, 16))
New-Item -ItemType Directory -Path $ExternalDir -Force | Out-Null

Checkout-Dependency $Dependencies[0] 5
Checkout-Dependency $Dependencies[1] 12
Checkout-Dependency $Dependencies[2] 20

Write-Stage 32 "Applying compatibility fixes"
Apply-PatchOnce "N64ModernRuntime" (Join-Path $RootDir "patches\N64ModernRuntime.patch")
Apply-PatchOnce "rt64" (Join-Path $RootDir "patches\RT64.patch")

$N64RecompDir = Join-Path $ExternalDir "N64Recomp"
$N64RecompBuildDir = Join-Path $N64RecompDir "build-windows"
Write-Stage 35 "Configuring CPU translator"
Invoke-Native "cmake.exe" @(
    "-S", $N64RecompDir,
    "-B", $N64RecompBuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_C_COMPILER=clang-cl",
    "-DCMAKE_CXX_COMPILER=clang-cl"
)

Write-Stage 40 "Building CPU translator"
Invoke-Native "cmake.exe" @(
    "--build", $N64RecompBuildDir,
    "--parallel", "$Jobs",
    "--target", "N64RecompCLI"
) $RootDir 40 10 "Building CPU translator"

$N64Recomp = Join-Path $N64RecompBuildDir "N64Recomp.exe"
if (-not (Test-Path -LiteralPath $N64Recomp)) {
    throw "N64Recomp did not produce $N64Recomp."
}

$GeneratedDir = Join-Path $RootDir "recomp\generated"
$BaseRom = Join-Path $RootDir "recomp\baserom.z64"
if (Test-Path -LiteralPath $GeneratedDir) {
    Remove-Item -LiteralPath $GeneratedDir -Recurse -Force
}
New-Item -ItemType Directory -Path $GeneratedDir -Force | Out-Null
if (Test-Path -LiteralPath $BaseRom) {
    Remove-Item -LiteralPath $BaseRom -Force
}
try {
    New-Item -ItemType HardLink -Path $BaseRom -Target $RomPath -ErrorAction Stop | Out-Null
}
catch {
    Copy-Item -LiteralPath $RomPath -Destination $BaseRom -Force
}

Write-Stage 51 "Mapping game functions"
$pythonArguments = @($script:Python.Prefix) + @(
    (Join-Path $RootDir "tools\generate_recomp_symbols.py"),
    $RomPath
)
Invoke-Native $script:Python.File $pythonArguments $RootDir

Write-Stage 55 "Generating native game CPU"
try {
    Invoke-Native $N64Recomp @("40winks.toml") (Join-Path $RootDir "recomp")
}
finally {
    if (Test-Path -LiteralPath $BaseRom) {
        Remove-Item -LiteralPath $BaseRom -Force
    }
}

Write-Stage 60 "Configuring playable game"
Invoke-Native "cmake.exe" @(
    "-S", (Join-Path $RootDir "recomp-port"),
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_C_COMPILER=clang-cl",
    "-DCMAKE_CXX_COMPILER=clang-cl",
    "-DBUILD_TESTING=OFF"
)

Write-Stage 66 "Compiling playable game"
Invoke-Native "cmake.exe" @(
    "--build", $BuildDir,
    "--parallel", "$Jobs"
) $RootDir 66 27 "Compiling playable game"

Write-Stage 94 "Installing playable game"
if (Test-Path -LiteralPath $InstallDir) {
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
}
Invoke-Native "cmake.exe" @(
    "--install", $BuildDir,
    "--prefix", $InstallDir,
    "--config", "Release"
)

$InstalledBin = Join-Path $InstallDir "bin"
$InstalledExe = Join-Path $InstalledBin "forty-winks-recomp.exe"
if (-not (Test-Path -LiteralPath $InstalledExe)) {
    throw "The native build completed without producing forty-winks-recomp.exe."
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Get-ChildItem -LiteralPath $InstalledBin | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $OutputDirectory -Recurse -Force
}

$readme = @"
40 Winks PC Port - private playable build

Start the game with 40-Winks-PC-Port.exe when it is present. For a direct
script build, run forty-winks-recomp.exe with --rom followed by your ROM path.
Keep your legally dumped ROM in a location you control. It is not copied here.

F1 opens the debug and display menu.
"@
Set-Content -LiteralPath (Join-Path $OutputDirectory "README.txt") `
    -Value $readme -Encoding UTF8

Write-Stage 100 "Playable build ready"
Write-Output ("FORTY_WINKS_OUTPUT|{0}" -f $OutputDirectory)
