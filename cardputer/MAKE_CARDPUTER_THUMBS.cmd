@echo off
setlocal
if "%~1"=="" (
  echo Usage: %~nx0 E:\
  echo Pass the microSD card drive or a folder containing music.
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\MAKE_CARDPUTER_THUMBS.ps1" "%~1"
if errorlevel 1 exit /b 1
echo Cardputer R67 album thumbnails cached. Music files were not changed.
