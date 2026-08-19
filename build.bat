@echo off
setlocal
cd /d "%~dp0"

set "PY=.venv\Scripts\python.exe"
set "REBUILD_VENV=0"
if not exist "%PY%" (
  set "REBUILD_VENV=1"
) else (
  "%PY%" -c "import pathlib, sys; raise SystemExit(0 if pathlib.Path(sys.prefix).resolve() == pathlib.Path(r'%~dp0.venv').resolve() else 1)"
  if errorlevel 1 set "REBUILD_VENV=1"
)

if "%REBUILD_VENV%"=="1" (
  echo Recreating virtual environment...
  if exist ".venv" rmdir /s /q ".venv"
  python -m venv .venv
  if errorlevel 1 (
    echo Python is not in PATH. Install Python 3 and retry.
    exit /b 1
  )
  "%PY%" -m pip install --upgrade pip
)

"%PY%" -m pip install -r requirements.txt pyinstaller
if errorlevel 1 (
  echo Failed to install build dependencies.
  exit /b 1
)

"%PY%" -c "from fatty import __version__; print('Version (git tag): ' + __version__)"
if errorlevel 1 (
  echo Failed to read version from git.
  exit /b 1
)

if exist dist (
  del /q "dist\FaTTY*.exe" 2>nul
)

"%PY%" -m PyInstaller --noconfirm --clean fatty.spec
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

"%PY%" -c "from fatty import exe_filename; print(); print('Ready: dist\\' + exe_filename()); print('Copy that file anywhere — Python is not required on the target PC.')"
