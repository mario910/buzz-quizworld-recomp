# Prepare tools only; never downloads, decrypts or modifies game/firmware files.
param([string]$Python = 'python', [switch]$SkipInstall)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
foreach ($file in @('ps3recomp\tools\ppu_loader.py','third_party\zlib\CMakeLists.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $file))) {
        throw 'Submodules are missing. Run: git submodule update --init --recursive'
    }
}
$venvPython = Join-Path $root 'toolenv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $venvPython)) {
    & $Python -c 'import sys; assert sys.version_info >= (3,11), "Python 3.11 or newer is required"'
    if ($LASTEXITCODE) { throw 'Install Python 3.11+ (with Tcl/Tk) and put python on PATH, or use -Python with its full path.' }
    & $Python -m venv (Join-Path $root 'toolenv')
    if ($LASTEXITCODE) { throw 'Could not create the local Python environment.' }
}
& $venvPython -c 'import sys, tkinter; assert sys.version_info >= (3,11), "Python 3.11+ required"'
if ($LASTEXITCODE) { throw 'The local Python needs Python 3.11+ and Tcl/Tk. Repair it before continuing.' }
if (-not $SkipInstall) {
    & $venvPython -m pip install -r (Join-Path $root 'requirements.txt')
    if ($LASTEXITCODE) { throw 'Tool dependency installation failed.' }
}
$pathsFile = Join-Path $root 'buzz-paths.json'
if (-not (Test-Path -LiteralPath $pathsFile)) {
    Copy-Item -LiteralPath (Join-Path $root 'buzz-paths.example.json') -Destination $pathsFile
}
Write-Output 'Tools ready. Edit buzz-paths.json to point at your own game/firmware files.'
Write-Output 'Next: .\Build-Buzz.ps1, then .\Launch-Buzz.ps1 (or Launch-Buzz.cmd).'
