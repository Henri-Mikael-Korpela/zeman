$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# cmake --preset needs CMakePresets.json in the working directory, so run from desktop/ and
# change back afterwards, even if a step above fails.
Set-Location "$PSScriptRoot\desktop"
try {
    cmake --preset mingw-release
    cmake --build --preset mingw-release
    .\build\release-win32\zeman.exe
} finally {
    Set-Location $PSScriptRoot
}
