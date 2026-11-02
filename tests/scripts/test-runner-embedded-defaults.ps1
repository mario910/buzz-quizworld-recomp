# Intercept the actual runner's process boundary; never start the game.
param([string]$BuildDirectory='build-vulkan-embedded-default-v1')
$ErrorActionPreference='Stop'
$runnerTestRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$runnerTestKeys=@(Get-Content "$runnerTestRoot/port/runtime_defaults.inc" | ForEach-Object {if($_ -match '^X\((\w+),'){$Matches[1]}})
$runnerTestSaved=@{}
$runnerTestExpected=@{}
$runnerTestState=@{Calls=0}
function Set-RunnerTestOption($Name,$Value) {
    if($null -eq $Value){Remove-Item "Env:$Name" -ErrorAction SilentlyContinue}
    else{[Environment]::SetEnvironmentVariable($Name,$Value,'Process')}
}
function Start-Process {
    param($FilePath,$ArgumentList,$WorkingDirectory,$WindowStyle,$RedirectStandardOutput,$RedirectStandardError,[switch]$PassThru)
    foreach($key in $runnerTestKeys) {
        if([Environment]::GetEnvironmentVariable($key,'Process') -cne $runnerTestExpected[$key]){throw "Runner injected or overwrote $key"}
    }
    $runnerTestState.Calls++
    [pscustomobject]@{Id=12345}
}
try {
    foreach($key in $runnerTestKeys){$runnerTestSaved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
    foreach($case in @('absent','overrides')) {
        foreach($key in $runnerTestKeys) {
            $value=if($case -eq 'absent'){$null}else{'0'}
            Set-RunnerTestOption $key $value
            $runnerTestExpected[$key]=$value
        }
        & "$runnerTestRoot/scripts/run-windows.ps1" -BuildDirectory $BuildDirectory -MenuBootstrap -Seconds 0 | Out-Null
    }
    if($runnerTestState.Calls -ne 2){throw 'Runner did not reach the intercepted process boundary'}
    "PASS actual PowerShell runner: all $($runnerTestKeys.Count) absent options and explicit OFF overrides preserved at process launch; no game started"
} finally {
    foreach($key in $runnerTestSaved.Keys){Set-RunnerTestOption $key $runnerTestSaved[$key]}
}
