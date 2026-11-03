@echo off
setlocal
if exist "%~dp0dist-studio\bin\BuzzQuestionStudio.exe" (
    start "" "%~dp0dist-studio\bin\BuzzQuestionStudio.exe" %*
    exit /b 0
)
echo Build Question Studio first with scripts\build-question-studio.ps1 -QtPrefix ^<Qt6 directory^>.
exit /b 1
