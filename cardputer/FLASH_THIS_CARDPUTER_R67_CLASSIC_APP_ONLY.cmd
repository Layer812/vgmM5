@echo off
setlocal
set "PORT=%~1"
if not defined PORT (
    echo Cardputer serial COM port is required ^(example COM9^).
    set /p "PORT=Cardputer COM port: "
)
if not defined PORT (
    echo ERROR: no serial port given. Nothing flashed.
    pause
    exit /b 2
)
echo Cardputer R67 CLASSIC: checking real partition layout, OTA slot and SHA-256.
echo This command writes APP ONLY; it does not erase user data.
python "%~dp0tools\FLASH_R67_APP_ONLY.py" --port "%PORT%" --variant classic
if errorlevel 1 (
    echo FAIL: Flash was not confirmed. Read the message above.
    pause
    exit /b 1
)
echo PASS: Cardputer R67 CLASSIC was flashed and reset.
pause
