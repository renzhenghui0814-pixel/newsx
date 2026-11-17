@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "%~dp0install.ps1" "%~dp0payload.zip"
exit /b %ERRORLEVEL%
