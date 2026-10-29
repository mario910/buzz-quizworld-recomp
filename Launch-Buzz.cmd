@echo off
setlocal DisableDelayedExpansion
set "BUZZ_LAUNCH_DIR=%~dp0"
if not exist "%BUZZ_LAUNCH_DIR%toolenv\Scripts\pythonw.exe" (
    echo Missing toolenv\Scripts\pythonw.exe. Follow the setup in README.md first.
    pause
    exit /b 1
)
start "" /D "%BUZZ_LAUNCH_DIR%" "%BUZZ_LAUNCH_DIR%toolenv\Scripts\pythonw.exe" "%BUZZ_LAUNCH_DIR%launch-buzz.pyw"
