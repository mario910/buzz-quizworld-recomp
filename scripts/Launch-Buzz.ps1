param([switch]$Smoke)
$projectRoot = (Split-Path -Parent $PSScriptRoot)

$ErrorActionPreference='Stop'
$python=Join-Path $projectRoot 'toolenv\Scripts\python.exe'
$pythonWindowed=Join-Path $projectRoot 'toolenv\Scripts\pythonw.exe'
if(-not $Smoke -and -not(Test-Path -LiteralPath $python)) {
    & (Join-Path $projectRoot 'scripts/Prepare-Buzz.ps1')
    return
}
if (-not (Test-Path -LiteralPath $python)) { throw 'Missing toolenv Python. See README setup instructions.' }
if ($Smoke) {
    Push-Location $projectRoot
    try { & $python -m launcher --smoke; if($LASTEXITCODE){throw 'Launcher UI smoke test failed'} }
    finally { Pop-Location }
} else {
    if (-not (Test-Path -LiteralPath $pythonWindowed)) {throw 'Missing pythonw.exe in toolenv; repair the Python environment.'}
    $entry=Join-Path $projectRoot 'launcher/start.pyw'
    Start-Process -FilePath $pythonWindowed -ArgumentList ('"'+$entry+'"') -WorkingDirectory $projectRoot -WindowStyle Hidden
}
