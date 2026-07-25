@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Invoke-Build.ps1" %*
exit /b %ERRORLEVEL%
