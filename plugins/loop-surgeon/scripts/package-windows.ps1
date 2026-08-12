param(
    [string] $Version = "0.13.0-local-test"
)

$ErrorActionPreference = "Stop"

if ($Version -notmatch '^[0-9A-Za-z.-]+$') {
    throw "Version contains unsupported characters: $Version"
}

$expectedPluginVersion = ($Version -split '-', 2)[0]
if ($expectedPluginVersion -notmatch '^\d+\.\d+\.\d+$') {
    throw "Version must begin with a semantic plug-in version: $Version"
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "build"))
$pluginPath = Join-Path $buildRoot "vs2022\plugins\loop-surgeon\LoopSurgeon_artefacts\Release\VST3\Loop Surgeon.vst3"
$guidePath = Join-Path $repositoryRoot "plugins\loop-surgeon\WINDOWS_REAPER_TEST.md"
$packageRoot = Join-Path $buildRoot "packages"
$packageName = "Loop-Surgeon-$Version-Windows-x64-VST3"
$stagePath = [IO.Path]::GetFullPath((Join-Path $packageRoot $packageName))
$zipPath = [IO.Path]::GetFullPath((Join-Path $packageRoot "$packageName.zip"))

if (-not $stagePath.StartsWith($packageRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to stage outside build/packages: $stagePath"
}

if (-not (Test-Path -LiteralPath $pluginPath -PathType Container)) {
    throw "Release VST3 bundle not found: $pluginPath"
}

if (-not (Test-Path -LiteralPath $guidePath -PathType Leaf)) {
    throw "PC test guide not found: $guidePath"
}

New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null

if (Test-Path -LiteralPath $stagePath) {
    Remove-Item -LiteralPath $stagePath -Recurse -Force
}

if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

New-Item -ItemType Directory -Path $stagePath | Out-Null
Copy-Item -LiteralPath $pluginPath -Destination $stagePath -Recurse
Copy-Item -LiteralPath $guidePath -Destination (Join-Path $stagePath "README-PC-REAPER.md")

Compress-Archive -Path (Join-Path $stagePath "*") -DestinationPath $zipPath -CompressionLevel Optimal

$pluginBinary = Join-Path $stagePath "Loop Surgeon.vst3\Contents\x86_64-win\Loop Surgeon.vst3"
if (-not (Test-Path -LiteralPath $pluginBinary -PathType Leaf)) {
    throw "Packaged VST3 binary is missing: $pluginBinary"
}

$moduleInfoPath = Join-Path $stagePath "Loop Surgeon.vst3\Contents\Resources\moduleinfo.json"
$moduleInfo = Get-Content -LiteralPath $moduleInfoPath -Raw
$manifestVersion = [regex]::Match($moduleInfo, '"Version"\s*:\s*"([^\"]+)"').Groups[1].Value
$binaryVersion = (Get-Item -LiteralPath $pluginBinary).VersionInfo.ProductVersion
if ($manifestVersion -ne $expectedPluginVersion -or $binaryVersion -ne $expectedPluginVersion) {
    throw "Packaged version mismatch: expected $expectedPluginVersion; manifest $manifestVersion; binary $binaryVersion"
}

$zip = Get-Item -LiteralPath $zipPath
Write-Output "Package: $($zip.FullName)"
Write-Output "Bytes:   $($zip.Length)"
