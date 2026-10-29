param([switch]$Smoke)
$ErrorActionPreference='Stop'
$python=Join-Path $PSScriptRoot 'toolenv\Scripts\python.exe'
$pythonWindowed=Join-Path $PSScriptRoot 'toolenv\Scripts\pythonw.exe'
if(-not $Smoke -and -not(Test-Path -LiteralPath $python)) {
    & (Join-Path $PSScriptRoot 'Prepare-Buzz.ps1')
    return
}
if (-not (Test-Path -LiteralPath $python)) { throw 'Missing toolenv Python. See README setup instructions.' }
if ($Smoke) {
    Push-Location $PSScriptRoot
    try { & $python -m launcher --smoke; if($LASTEXITCODE){throw 'Launcher UI smoke test failed'} }
    finally { Pop-Location }
} else {
    if (-not (Test-Path -LiteralPath $pythonWindowed)) {throw 'Missing pythonw.exe in toolenv; repair the Python environment.'}
    $entry=Join-Path $PSScriptRoot 'launch-buzz.pyw'
    Start-Process -FilePath $pythonWindowed -ArgumentList ('"'+$entry+'"') -WorkingDirectory $PSScriptRoot -WindowStyle Hidden
}
