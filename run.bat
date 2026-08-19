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
    pause
    exit /b 1
  )
  "%PY%" -m pip install --upgrade pip
  "%PY%" -m pip install -r requirements.txt
  if errorlevel 1 (
    echo Failed to install dependencies.
    pause
    exit /b 1
  )
)

"%PY%" -m fatty %*
if errorlevel 1 pause
