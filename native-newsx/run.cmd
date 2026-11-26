@echo off
setlocal
for %%I in ("%~dp0.") do set "SOURCE_DIR=%%~fI"
set "QT_DIR=%SOURCE_DIR%\qt\6.8.3\msvc2022_64"
set "PATH=%QT_DIR%\bin;%PATH%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SOURCE_DIR%\run.ps1" %*
set "LAUNCH_RESULT=%ERRORLEVEL%"
if not "%LAUNCH_RESULT%"=="0" (
  echo NewsX could not be launched. See the error above.
  pause
)
exit /b %LAUNCH_RESULT%
