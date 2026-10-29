@echo off
setlocal DisableDelayedExpansion
set "BUZZ_LAUNCH_DIR=%~dp0"
if not exist "%BUZZ_LAUNCH_DIR%toolenv\Scripts\pythonw.exe" (
    call "%BUZZ_LAUNCH_DIR%Prepare-Buzz.cmd"
    exit /b
)
start "" /D "%BUZZ_LAUNCH_DIR%" "%BUZZ_LAUNCH_DIR%toolenv\Scripts\pythonw.exe" "%BUZZ_LAUNCH_DIR%launcher\start.pyw"
