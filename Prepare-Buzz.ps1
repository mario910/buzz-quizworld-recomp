param([switch]$Smoke)
$ErrorActionPreference='Stop'
$python=Join-Path $PSScriptRoot 'toolenv\Scripts\python.exe'
if(-not(Test-Path -LiteralPath $python)) { & (Join-Path $PSScriptRoot 'Setup-Buzz.ps1') }
& $python -c 'import pycdlib, capstone, clang_tool_chain'
if($LASTEXITCODE) { & (Join-Path $PSScriptRoot 'Setup-Buzz.ps1') }
if($Smoke) {
    Push-Location $PSScriptRoot
    try { & $python -m launcher --prepare --smoke; if($LASTEXITCODE){throw 'Preparation UI smoke failed'} }
    finally { Pop-Location }
} else {
    $pythonw=Join-Path $PSScriptRoot 'toolenv\Scripts\pythonw.exe'
    $entry=Join-Path $PSScriptRoot 'launch-buzz.pyw'
    Start-Process -FilePath $pythonw -ArgumentList ('"'+$entry+'" --prepare') -WorkingDirectory $PSScriptRoot -WindowStyle Hidden
}
