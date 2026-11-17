@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" "%~dp0payload.zip"
if errorlevel 1 (
  echo NewsX installation failed.
  pause
  exit /b 1
)
exit /b 0
