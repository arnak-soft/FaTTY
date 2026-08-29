@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "VCPKG_ROOT=%~dp0third_party\vcpkg"
if not defined VCPKG_DEFAULT_BINARY_CACHE set "VCPKG_DEFAULT_BINARY_CACHE=%~dp0.vcpkg-cache"
if not exist "%VCPKG_DEFAULT_BINARY_CACHE%" mkdir "%VCPKG_DEFAULT_BINARY_CACHE%"
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
  echo Cloning vcpkg...
  if not exist "third_party" mkdir third_party
  git clone --depth 1 https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
  call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
  if errorlevel 1 exit /b 1
)

call :ensure_msvc
call :ensure_cmake_ninja
if errorlevel 1 exit /b 1

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
    echo No compiler in PATH.
    echo Install Visual Studio 2022 with "Desktop development with C++",
    echo then run build.bat from a regular cmd — it loads MSVC itself.
    echo MinGW g++ also works if it is on PATH.
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

call :find_iscc
if "%ISCC%"=="" (
  echo WARNING: Setup.exe not built — install Inno Setup 6 or set ISCC to ISCC.exe.
  exit /b 0
)

set "VERINFO="
if exist "build\generated\_version_info.txt" (
  set /p VERINFO=<build\generated\_version_info.txt
)
for /f "tokens=* delims= " %%A in ("%VERINFO%") do set "VERINFO=%%A"
if not "%VERINFO%"=="" goto :got_verinfo

rem Fallback: 1.5.18-dirty -> 1.5.18.0; 1.5.18-3-gabc -> 1.5.18.3
set "V1=0" & set "V2=0" & set "V3=0" & set "V4=0"
for /f "tokens=1-4 delims=.-" %%a in ("%VER%") do (
  set "V1=%%a"
  set "V2=%%b"
  set "V3=%%c"
  set "V4=%%d"
)
if "%V2%"=="" set "V2=0"
if "%V3%"=="" set "V3=0"
if "%V4%"=="" set "V4=0"
echo(%V1%| findstr /r "[^0-9]" >nul && set "V1=0"
echo(%V2%| findstr /r "[^0-9]" >nul && set "V2=0"
echo(%V3%| findstr /r "[^0-9]" >nul && set "V3=0"
echo(%V4%| findstr /r "[^0-9]" >nul && set "V4=0"
set "VERINFO=%V1%.%V2%.%V3%.%V4%"

:got_verinfo
echo Building installer...
"%ISCC%" "/DMyAppVersion=%VER%" "/DMyVersionInfo=%VERINFO%" "/DPortableDirName=FaTTY %VER% Portable" fatty.iss
if errorlevel 1 exit /b 1
echo   dist\FaTTY %VER% Setup.exe
exit /b 0

:find_iscc
if defined ISCC if exist "%ISCC%" exit /b 0
set "PF86=%ProgramFiles(x86)%"
if exist "%PF86%\Inno Setup 6\ISCC.exe" (
  set "ISCC=%PF86%\Inno Setup 6\ISCC.exe"
  exit /b 0
)
if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" (
  set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
  exit /b 0
)
where ISCC >nul 2>nul
if not errorlevel 1 (
  for /f "delims=" %%I in ('where ISCC') do set "ISCC=%%I"
)
exit /b 0

:ensure_msvc
where cl >nul 2>nul
if not errorlevel 1 exit /b 0
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 0
set "VSINSTALL="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if "%VSINSTALL%"=="" exit /b 0
if not exist "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" exit /b 0
echo Loading MSVC x64 from "%VSINSTALL%"...
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b 0

:ensure_cmake_ninja
if defined VSINSTALLDIR (
  if exist "%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "PATH=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
  )
)
if exist "%~dp0third_party\cmake\bin\cmake.exe" set "PATH=%~dp0third_party\cmake\bin;%PATH%"
if exist "%~dp0third_party\ninja\ninja.exe" set "PATH=%~dp0third_party\ninja;%PATH%"
where cmake >nul 2>nul
if errorlevel 1 call :install_cmake
if errorlevel 1 exit /b 1
where ninja >nul 2>nul
if errorlevel 1 call :install_ninja
if errorlevel 1 exit /b 1
exit /b 0

:install_cmake
echo CMake not in PATH — downloading portable CMake 3.31.8...
if not exist "%~dp0third_party" mkdir "%~dp0third_party"
curl.exe -L --fail --retry 3 -o "%TEMP%\fatty-cmake.zip" "https://github.com/Kitware/CMake/releases/download/v3.31.8/cmake-3.31.8-windows-x86_64.zip"
if errorlevel 1 (
  echo Failed to download CMake. Install CMake 3.24+ and Ninja, then retry.
  exit /b 1
)
if exist "%~dp0third_party\_cmake_extract" rd /s /q "%~dp0third_party\_cmake_extract"
mkdir "%~dp0third_party\_cmake_extract"
tar.exe -xf "%TEMP%\fatty-cmake.zip" -C "%~dp0third_party\_cmake_extract"
if errorlevel 1 (
  echo Failed to unpack CMake zip.
  exit /b 1
)
if exist "%~dp0third_party\cmake" rd /s /q "%~dp0third_party\cmake"
move "%~dp0third_party\_cmake_extract\cmake-3.31.8-windows-x86_64" "%~dp0third_party\cmake" >nul
rd /s /q "%~dp0third_party\_cmake_extract"
if not exist "%~dp0third_party\cmake\bin\cmake.exe" (
  echo CMake layout unexpected after unpack.
  exit /b 1
)
set "PATH=%~dp0third_party\cmake\bin;%PATH%"
exit /b 0

:install_ninja
echo Ninja not in PATH — downloading Ninja 1.12.1...
if not exist "%~dp0third_party" mkdir "%~dp0third_party"
if not exist "%~dp0third_party\ninja" mkdir "%~dp0third_party\ninja"
curl.exe -L --fail --retry 3 -o "%TEMP%\fatty-ninja.zip" "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip"
if errorlevel 1 (
  echo Failed to download Ninja. Install Ninja, then retry.
  exit /b 1
)
tar.exe -xf "%TEMP%\fatty-ninja.zip" -C "%~dp0third_party\ninja"
if errorlevel 1 (
  echo Failed to unpack Ninja zip.
  exit /b 1
)
if not exist "%~dp0third_party\ninja\ninja.exe" (
  echo ninja.exe missing after unpack.
  exit /b 1
)
set "PATH=%~dp0third_party\ninja;%PATH%"
exit /b 0
