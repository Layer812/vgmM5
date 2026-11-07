@echo off
setlocal
cd /d "%~dp0"
python tools\build_r67.py
if errorlevel 1 (
    echo Cardputer R67 Standard build FAILED. No flash performed.
    exit /b 1
)
echo Cardputer R67 Standard build PASS. No flash performed.
