# Package a Release build from a PowerShell prompt:
#   .\packaging\windows\package.ps1 -BuildDir .\build -Configuration Release
# Override the detected CMake version or output location with -Version and -OutputDir.
# Qt's bin directory must contain windeployqt.exe and be available on this process's PATH.

[CmdletBinding()]
param(
    [string]$BuildDir = "build",
    [string]$Configuration = "Release",
    [string]$Version = "",
    [string]$OutputDir = "dist"
)

$ErrorActionPreference = "Stop"

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

function Resolve-RepositoryPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    $projectLine = Select-String -Path (Join-Path $repositoryRoot "CMakeLists.txt") `
        -Pattern '^\s*project\s*\(\s*ArcadeWheel\s+VERSION\s+([^\s\)]+)' -AllMatches |
        Select-Object -First 1
    if (-not $projectLine) {
        throw "Could not read the Arcade Wheel version from CMakeLists.txt. Pass -Version explicitly."
    }
    $Version = $projectLine.Matches[0].Groups[1].Value
}
if ($Version -notmatch '^[0-9A-Za-z][0-9A-Za-z.+-]*$') {
    throw "Version '$Version' is not a safe filename component."
}

$buildPath = Resolve-RepositoryPath $BuildDir
$outputPath = Resolve-RepositoryPath $OutputDir
$binaryCandidates = @(
    (Join-Path (Join-Path $buildPath $Configuration) "arcade-wheel.exe"),
    (Join-Path $buildPath "arcade-wheel.exe")
)
$executable = $binaryCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $executable) {
    throw "Could not find arcade-wheel.exe in '$buildPath' or its '$Configuration' subdirectory. Build the Windows target first."
}

$deployTool = Get-Command "windeployqt.exe" -CommandType Application -ErrorAction SilentlyContinue
if (-not $deployTool) {
    $deployTool = Get-Command "windeployqt" -CommandType Application -ErrorAction SilentlyContinue
}
if (-not $deployTool) {
    throw "windeployqt was not found on PATH. Add the matching Qt 6 bin directory to this PowerShell process's PATH and rerun."
}

$qmlDirectory = Join-Path $repositoryRoot "qml"
if (-not (Test-Path -LiteralPath $qmlDirectory -PathType Container)) {
    throw "The QML source directory is missing: '$qmlDirectory'."
}

New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$packageName = "ArcadeWheel-$Version-Windows"
$archivePath = Join-Path $outputPath "$packageName.zip"
$packageId = [guid]::NewGuid().ToString("N")
$stagePath = Join-Path $outputPath (".stage-" + $packageId)
$temporaryArchivePath = Join-Path $outputPath (".$packageName-$packageId.zip")
$qtBuildMode = if ($Configuration -ieq "Debug") { "--debug" } else { "--release" }

try {
    New-Item -ItemType Directory -Path $stagePath -Force | Out-Null
    $stagedExecutable = Join-Path $stagePath "arcade-wheel.exe"
    Copy-Item -LiteralPath $executable -Destination $stagedExecutable
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "README.md") -Destination $stagePath
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "LICENSE") -Destination $stagePath

    Write-Host "Deploying Qt runtime and QML imports with $($deployTool.Source)..."
    & $deployTool.Source $qtBuildMode --compiler-runtime --no-translations `
        --qmldir $qmlDirectory --dir $stagePath $stagedExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE."
    }

    Compress-Archive -Path (Join-Path $stagePath "*") -DestinationPath $temporaryArchivePath `
        -CompressionLevel Optimal
    if (-not (Test-Path -LiteralPath $temporaryArchivePath -PathType Leaf) -or
        (Get-Item -LiteralPath $temporaryArchivePath).Length -eq 0) {
        throw "The package ZIP was not created successfully."
    }
    Move-Item -LiteralPath $temporaryArchivePath -Destination $archivePath -Force

    Write-Host "Created $archivePath"
}
finally {
    if (Test-Path -LiteralPath $stagePath) {
        Remove-Item -LiteralPath $stagePath -Recurse -Force
    }
    if (Test-Path -LiteralPath $temporaryArchivePath) {
        Remove-Item -LiteralPath $temporaryArchivePath -Force
    }
}
