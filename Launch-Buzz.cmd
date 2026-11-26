@echo off
setlocal DisableDelayedExpansion
rem Start the game prepared by BuzzSetup; otherwise open BuzzSetup to prepare it.
set "BUZZ_LAUNCH_DIR=%~dp0"
if exist "%BUZZ_LAUNCH_DIR%dist\BuzzQuizWorldRecomp.exe" (
    start "" "%BUZZ_LAUNCH_DIR%dist\BuzzQuizWorldRecomp.exe"
    exit /b
)
echo No prepared game yet: opening BuzzSetup.
echo Developers can start a build from scripts\Build-Buzz.ps1 with scripts\run-windows.ps1 -Seconds 0 -MenuBootstrap.
call "%BUZZ_LAUNCH_DIR%Prepare-Buzz.cmd"
