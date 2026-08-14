[CmdletBinding()]
param(
    [string]$Version = "0.1.2-alpha",
    [string]$BuildId = "dev"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$RootDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$Project = Join-Path $PSScriptRoot "launcher\FortyWinksSetup.csproj"
$BuildDir = Join-Path $RootDir "build\windows-launcher"
$PublishDir = Join-Path $BuildDir "publish"
$Payload = Join-Path $BuildDir "public-source.zip"
$BuildIdFile = Join-Path $BuildDir "build-id.txt"
$DistDir = Join-Path $RootDir "dist\windows"
$OutputName = "40-Winks-PC-Port-Windows-x64.exe"
$Output = Join-Path $DistDir $OutputName

if (Test-Path -LiteralPath $BuildDir) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
New-Item -ItemType Directory -Path $BuildDir, $DistDir -Force | Out-Null

& git.exe -C $RootDir archive --format=zip --output=$Payload HEAD
if ($LASTEXITCODE -ne 0) {
    throw "Could not create the public source payload from the current commit."
}
Set-Content -LiteralPath $BuildIdFile -Value $BuildId -Encoding ASCII

& dotnet.exe publish $Project `
    --configuration Release `
    --runtime win-x64 `
    --self-contained true `
    --output $PublishDir `
    "-p:PayloadZip=$Payload" `
    "-p:BuildIdFile=$BuildIdFile" `
    "-p:Version=$Version"
if ($LASTEXITCODE -ne 0) {
    throw "The Windows setup application did not compile."
}

$PublishedExecutable = Join-Path $PublishDir "40-Winks-PC-Port.exe"
if (-not (Test-Path -LiteralPath $PublishedExecutable)) {
    throw "The Windows setup executable was not produced."
}
Copy-Item -LiteralPath $PublishedExecutable -Destination $Output -Force

$hash = (Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$Output.sha256" `
    -Value "$hash  $OutputName" -Encoding ASCII

Write-Output $Output
