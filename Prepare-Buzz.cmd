@echo off
setlocal DisableDelayedExpansion
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Prepare-Buzz.ps1"
if errorlevel 1 pause
