# Start the game: the copy prepared by BuzzSetup (dist\), otherwise this
# checkout's developer build (scripts\Build-Buzz.ps1) through run-windows.ps1,
# otherwise open BuzzSetup to prepare it.
$projectRoot = (Split-Path -Parent $PSScriptRoot)

$ErrorActionPreference='Stop'
$prepared=Join-Path $projectRoot 'dist\BuzzQuizWorldRecomp.exe'
if(Test-Path -LiteralPath $prepared) {
    Start-Process -FilePath $prepared -WorkingDirectory (Split-Path -Parent $prepared)
    return
}
foreach($build in 'build-vulkan-windows','build-windows') {
    if(Test-Path -LiteralPath (Join-Path $projectRoot "$build\BuzzQuizWorldRecomp.exe")) {
        & (Join-Path $PSScriptRoot 'run-windows.ps1') -Seconds 0 -MenuBootstrap -BuildDirectory $build
        return
    }
}
& (Join-Path $PSScriptRoot 'Prepare-Buzz.ps1')
