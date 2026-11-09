@echo off
setlocal DisableDelayedExpansion
set "wizard=%~dp0dist-preparation\bin\BuzzPrepare.exe"
if not exist "%wizard%" (
    echo BuzzPrepare.exe is missing. Extract the BuzzPrepare package here.
    echo Developers can build it with scripts\build-preparation.ps1.
    pause
    exit /b 1
)
start "" "%wizard%"
