$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

cmake --preset mingw-release
cmake --build --preset mingw-release
.\build\zeman.exe
