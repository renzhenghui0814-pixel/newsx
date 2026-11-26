@echo off
setlocal
for %%I in ("%~dp0.") do set "SOURCE_DIR=%%~fI"
set "BUILD_DIR=%SOURCE_DIR%\build"
set "QT_DIR=%SOURCE_DIR%\qt\6.8.3\msvc2022_64"
set "VS_VCVARS=D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
  echo Qt SDK is missing at "%QT_DIR%"
  exit /b 1
)
if not exist "%VS_VCVARS%" (
  echo Visual Studio C++ tools not found at "%VS_VCVARS%"
  exit /b 1
)
call "%VS_VCVARS%" >nul
if errorlevel 1 exit /b 1

cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%" -DNEWSX_RUNTIME_OUTPUT_DIR="%BUILD_DIR%\app"
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR%" --target NewsXNative --clean-first --parallel 4
if errorlevel 1 exit /b 1
echo Built "%BUILD_DIR%\app\NewsXNative.exe"
