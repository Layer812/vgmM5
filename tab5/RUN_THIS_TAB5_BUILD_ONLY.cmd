@echo off
cd /d "%~dp0"
python "%~dp0build_tab5.py"
exit /b %ERRORLEVEL%
