[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

if (-not (Get-Command winget.exe -ErrorAction SilentlyContinue)) {
    throw "Windows Package Manager (winget) is required. Install App Installer from Microsoft, then run this setup again."
}

function Install-Package {
    param([string]$Id, [string[]]$ExtraArguments = @())

    Write-Host "Installing $Id..."
    $arguments = @(
        "install", "--id", $Id, "--exact", "--silent",
        "--accept-package-agreements", "--accept-source-agreements"
    ) + $ExtraArguments
    & winget.exe @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "winget could not install $Id (exit code $LASTEXITCODE)."
    }
}

Install-Package "Git.Git"
Install-Package "Python.Python.3.13"
Install-Package "Kitware.CMake"
Install-Package "Ninja-build.Ninja"
Install-Package "Microsoft.VisualStudio.2022.BuildTools" @(
    "--override",
    "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --includeRecommended"
)

Write-Host "Required Windows build tools are installed."
