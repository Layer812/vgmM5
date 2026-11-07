@echo off
setlocal
cd /d "%~dp0"
python tools\build_r67.py --classic
if errorlevel 1 (
    echo Cardputer R67 Classic build FAILED. No flash performed.
    exit /b 1
)
echo Cardputer R67 Classic build PASS. No flash performed.
