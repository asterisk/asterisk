@echo off
setlocal EnableExtensions
set "SCRIPT_ROOT=%~dp0"
set "SILENT_MODE=0"
if /I "%~1"=="/s" set "SILENT_MODE=1"
if /I "%~1"=="--silent" set "SILENT_MODE=1"
if "%SILENT%"=="1" set "SILENT_MODE=1"

if "%SILENT_MODE%"=="0" (
  powershell.exe -NoLogo -NoProfile -Command "$p=New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent()); if($p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){exit 0}else{exit 1}"
  if errorlevel 1 (
    echo [installer] Requesting elevation before any packaging work begins.
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "try { $p=Start-Process -FilePath '%ComSpec%' -ArgumentList '/d','/c','\"%~f0\"' -Verb RunAs -Wait -PassThru; exit $p.ExitCode } catch { Write-Error $_; exit 1 }"
    exit /b %ERRORLEVEL%
  )
)

set "BUILD_SILENT="
if "%SILENT_MODE%"=="1" set "BUILD_SILENT=-Silent"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_ROOT%console\scripts\build-installer.ps1" %BUILD_SILENT%
exit /b %ERRORLEVEL%
