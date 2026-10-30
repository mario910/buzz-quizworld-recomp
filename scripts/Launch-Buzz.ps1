param([switch]$Smoke, [switch]$PythonLauncher)
$projectRoot = (Split-Path -Parent $PSScriptRoot)

$ErrorActionPreference='Stop'
$native=Join-Path $projectRoot 'build-launcher\BuzzLauncher.exe'
if(-not $PythonLauncher -and (Test-Path -LiteralPath $native)) {
    if($Smoke) {
        $check=Start-Process -FilePath $native -ArgumentList ('--project "'+$projectRoot+'" --smoke') -WindowStyle Hidden -Wait -PassThru
        if($check.ExitCode){throw 'Native launcher smoke failed'}
    }
    else {Start-Process -FilePath $native -WorkingDirectory $projectRoot -WindowStyle Hidden}
    return
}
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
