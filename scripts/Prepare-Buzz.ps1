# Open BuzzSetup, the preparation wizard (same as Prepare-Buzz.cmd).
$projectRoot = (Split-Path -Parent $PSScriptRoot)

$ErrorActionPreference='Stop'
$wizard=@('dist-preparation\bin\BuzzSetup.exe','dist-preparation\bin\BuzzPrepare.exe') |
    ForEach-Object { Join-Path $projectRoot $_ } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if(-not $wizard) { throw 'BuzzSetup is missing. Extract the BuzzPrepare package here, or build it with scripts\build-preparation.ps1.' }
Start-Process -FilePath $wizard -WorkingDirectory $projectRoot
