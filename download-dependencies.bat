@echo off
setlocal EnableExtensions
set "SCRIPT_ROOT=%~dp0"
set "SILENT_ARG="
if /I "%~1"=="/s" set "SILENT_ARG=-Silent"
if /I "%~1"=="--silent" set "SILENT_ARG=-Silent"
if "%SILENT%"=="1" set "SILENT_ARG=-Silent"

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_ROOT%console\scripts\download-dependencies.ps1" %SILENT_ARG%
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" echo [dependency bootstrap] Failed with exit code %RESULT%.
exit /b %RESULT%
