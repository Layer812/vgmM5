@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if not errorlevel 1 (
    py -3 -u "%~dp0tools\build_only.py"
) else (
    python -u "%~dp0tools\build_only.py"
)
exit /b %errorlevel%
