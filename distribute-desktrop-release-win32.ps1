# Packs what the Windows desktop app needs to run into a zip file, from the build in
# desktop\build\release-win32 (made by `cmake --preset mingw-release`, see README.md):
#
#   zeman.exe             The app, statically linked: its only imports are Windows system DLLs
#                         and the Universal CRT, which Windows 10 and 11 already have.
#   fonts\Rubik-*.ttf     The fonts the app loads from beside the executable.
#   fonts\OFL.txt         The fonts' license, which must travel with them.
#
# Everything else in the build folder is left out: CMake's files, libraries, the test programs and
# settings.ini (the building computer's own settings, with its database path).
#
# Usage: .\distribute-desktrop-release-win32.ps1 [-BuildDirectory PATH] [-OutputPath ZIP]

param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'desktop\build\release-win32'),
    # The time the zip was made is in its name, so that each run keeps the earlier zips.
    [string]$OutputPath = (Join-Path $PSScriptRoot (
        "dist\zeman-desktop-win32-$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').zip"))
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$executable = Join-Path $BuildDirectory 'zeman.exe'
$fontDirectory = Join-Path $BuildDirectory 'fonts'
if (-not (Test-Path $executable)) {
    throw "$executable not found. Build it first: cd desktop; cmake --preset mingw-release; cmake --build --preset mingw-release"
}
if (-not (Test-Path $fontDirectory)) {
    throw "$fontDirectory not found. Build the app again; the build copies the fonts there."
}

# Entry name inside the zip, and the file it comes from.
$files = [ordered]@{ 'zeman.exe' = $executable }
$fontFiles = @(Get-ChildItem $fontDirectory -File | Where-Object { $_.Extension -in '.ttf', '.txt' })
if (-not ($fontFiles | Where-Object { $_.Extension -eq '.ttf' })) {
    throw "No fonts in $fontDirectory."
}
foreach ($font in $fontFiles) {
    $files["fonts/$($font.Name)"] = $font.FullName
}

New-Item -ItemType Directory -Force (Split-Path -Parent $OutputPath) | Out-Null
if (Test-Path $OutputPath) { Remove-Item -Force $OutputPath }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::Open($OutputPath, 'Create')
try {
    foreach ($name in $files.Keys) {
        # Entry names are given with forward slashes, which every unzip tool understands.
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, $files[$name], $name, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally {
    $zip.Dispose()
}

Write-Host "Wrote $OutputPath"
foreach ($name in $files.Keys) {
    Write-Host "  $name"
}
Write-Host ''
Write-Host 'Unzip it anywhere on Windows 10 or 11 and run zeman.exe: nothing else is needed.'
Write-Host 'This build keeps its database at the path it was built with; on another computer, choose'
Write-Host 'a database file in Settings. For a self-contained folder that keeps zeman.db beside'
Write-Host 'zeman.exe, use the mingw-package preset (see README.md).'
