@echo off
setlocal DisableDelayedExpansion
set "BUZZ_LAUNCH_DIR=%~dp0"
if exist "%BUZZ_LAUNCH_DIR%BuzzLauncher.exe" (
    start "" /D "%BUZZ_LAUNCH_DIR%" "%BUZZ_LAUNCH_DIR%BuzzLauncher.exe"
    exit /b
)
if exist "%BUZZ_LAUNCH_DIR%build-launcher\BuzzLauncher.exe" (
    start "" /D "%BUZZ_LAUNCH_DIR%" "%BUZZ_LAUNCH_DIR%build-launcher\BuzzLauncher.exe"
    exit /b
)
echo The native launcher has not been built yet.
echo Developer setup: see README, then run scripts\build-launcher.ps1.
echo Importing/building and the legacy Python UI are separate developer tools.
pause
