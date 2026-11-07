@echo off
setlocal
cd /d "%~dp0"
python tools\build_vgmm5lib.py
if errorlevel 1 (
    echo Cardputer R7 native build FAILED. No device was flashed.
    exit /b 1
)
echo Cardputer R7 native build PASSED. No device was flashed.
