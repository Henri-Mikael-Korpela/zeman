@echo off
rem Builds android\build\calendar.apk. See build.ps1 for what it does and its options.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%
