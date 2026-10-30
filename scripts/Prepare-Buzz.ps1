param([switch]$Smoke)
$projectRoot = (Split-Path -Parent $PSScriptRoot)

$ErrorActionPreference='Stop'
$python=Join-Path $projectRoot 'toolenv\Scripts\python.exe'
if(-not(Test-Path -LiteralPath $python)) { & (Join-Path $projectRoot 'scripts/Setup-Buzz.ps1') }
& $python -c 'import pycdlib, capstone, clang_tool_chain; from Crypto.Cipher import AES'
if($LASTEXITCODE) { & (Join-Path $projectRoot 'scripts/Setup-Buzz.ps1') }
if($Smoke) {
    Push-Location $projectRoot
    try { & $python -m launcher --prepare --smoke; if($LASTEXITCODE){throw 'Preparation UI smoke failed'} }
    finally { Pop-Location }
} else {
    $pythonw=Join-Path $projectRoot 'toolenv\Scripts\pythonw.exe'
    $entry=Join-Path $projectRoot 'launcher/start.pyw'
    Start-Process -FilePath $pythonw -ArgumentList ('"'+$entry+'" --prepare') -WorkingDirectory $projectRoot -WindowStyle Hidden
}
