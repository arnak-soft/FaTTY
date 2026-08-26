@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "VCPKG_ROOT=%~dp0third_party\vcpkg"
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
  echo Cloning vcpkg...
  if not exist "third_party" mkdir third_party
  git clone --depth 1 https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
  call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
  if errorlevel 1 exit /b 1
)

set "GENERATOR=Ninja"
set "TRIPLET="
set "PRESET="

where cl >nul 2>nul
if not errorlevel 1 (
  set "TRIPLET=x64-windows-static"
  set "PRESET=msvc-static"
) else (
  where g++ >nul 2>nul
  if errorlevel 1 (
    echo Neither MSVC (cl) nor MinGW (g++) is in PATH.
    exit /b 1
  )
  set "TRIPLET=x64-mingw-static"
  set "PRESET=mingw-static"
)

echo Configuring ^(triplet %TRIPLET%^)...
cmake --preset %PRESET%
if errorlevel 1 (
  echo Fallback configure without preset...
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=%TRIPLET%
  if errorlevel 1 exit /b 1
)

echo Building...
cmake --build build --config Release
if errorlevel 1 exit /b 1

echo Tests...
ctest --test-dir build --output-on-failure -C Release
if errorlevel 1 exit /b 1

for /f "usebackq delims=" %%V in (`cmake -S . -B build -N -L 2^>nul ^| findstr FATTY_VERSION:`) do (
  rem unused: version read from generated file
)
set "VER="
if exist "build\generated\_version.txt" (
  set /p VER=<build\generated\_version.txt
)
if "%VER%"=="" set "VER=0.0.0-dev"
for /f "tokens=* delims= " %%A in ("%VER%") do set "VER=%%A"

set "EXE="
if exist "build\FaTTY.exe" set "EXE=build\FaTTY.exe"
if exist "build\Release\FaTTY.exe" set "EXE=build\Release\FaTTY.exe"
if "%EXE%"=="" (
  echo FaTTY.exe not found
  exit /b 1
)

if exist dist (
  del /q "dist\FaTTY*.exe" 2>nul
  for /d %%D in ("dist\FaTTY*Portable") do rd /s /q "%%D"
)
mkdir dist 2>nul
set "PORTABLE=dist\FaTTY %VER% Portable"
mkdir "%PORTABLE%" 2>nul
copy /y "%EXE%" "%PORTABLE%\FaTTY.exe" >nul
copy /y "build\generated\_version.txt" "%PORTABLE%\_version.txt" >nul
if exist "assets\app.ico" copy /y "assets\app.ico" "%PORTABLE%\app.ico" >nul
copy /y "%EXE%" "dist\FaTTY %VER% OneFile.exe" >nul

echo.
echo Ready:
echo   dist\FaTTY %VER% OneFile.exe
echo   %PORTABLE%\FaTTY.exe

set "ISCC="
if defined ISCC goto :has_iscc
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
:has_iscc
if "%ISCC%"=="" (
  echo WARNING: Setup.exe not built — install Inno Setup 6 or set ISCC.
  exit /b 0
)

for /f "tokens=1-4 delims=.,-" %%a in ("%VER%") do (
  set "V1=%%a"
  set "V2=%%b"
  set "V3=%%c"
  set "V4=%%d"
)
if "%V2%"=="" set "V2=0"
if "%V3%"=="" set "V3=0"
if "%V4%"=="" set "V4=0"
echo Building installer...
"%ISCC%" "/DMyAppVersion=%VER%" "/DMyVersionInfo=%V1%.%V2%.%V3%.%V4%" "/DPortableDirName=FaTTY %VER% Portable" fatty.iss
if errorlevel 1 exit /b 1
echo   dist\FaTTY %VER% Setup.exe
exit /b 0
