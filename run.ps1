Set-Location "$PSScriptRoot\desktop"
cmake --preset mingw-release
cmake --build --preset mingw-release
.\build\calendar.exe
Set-Location "$PSScriptRoot"
