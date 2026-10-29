# Exercise the real launcher, replacing ONLY process creation. No native game
# or emulator is started, and all environment mutations are process-local.
$ErrorActionPreference='Stop'
$experimentRoot=$PSScriptRoot
$expectedExe=Join-Path $experimentRoot 'build-windows\BuzzQuizWorldRecomp.exe'
if(Get-Process BuzzQuizWorldRecomp -ErrorAction SilentlyContinue | Where-Object Path -eq $expectedExe) {
    throw 'Close the native experiment before running this launcher fixture'
}
$savedEnvironment=@{}
foreach($entry in Get-ChildItem Env:){$savedEnvironment[$entry.Name]=$entry.Value}
try {
$script:snapshots=[System.Collections.Generic.List[object]]::new()
$global:buzzLauncherFixtureCaptures=$script:snapshots
$global:buzzLauncherFixtureFailure=$false
function Start-Process {
    param($FilePath,$ArgumentList,$WorkingDirectory,$WindowStyle,$RedirectStandardOutput,$RedirectStandardError,[switch]$PassThru)
    if($FilePath -ne $expectedExe -or $WindowStyle -ne 'Hidden' -or -not $PassThru){throw 'Unexpected process request'}
    $global:buzzLauncherFixtureCaptures.Add(@{
        Workers=$env:SPURS_JOB_LIST_WORKERS; Image=$env:SPURS_JOB_LIST_IMAGE
        JobCache=$env:SPURS_JOB_CACHE_ENV; RsxCache=$env:RSX_CACHE_ENV
        Wake=$env:SPURS_JTS_WAKE; Guard=$env:SPURS_GUARD_POLL_ONLY; Pulse=$env:BUZZ_PULSE_MS
        Fifo=$env:GCM_FIFO_STOP_AT_FLIP; Flip=$env:RSX_FLIP_BOUNDARY; Verbose=$env:PS3_VERBOSE
        Merge=$env:SPURS_MERGE_READY_LISTS; ExtraFP=$env:SPURS_NATIVE_EXTRA_FP
        Samplers=$env:RSX_GUEST_SAMPLERS; Pixel=$env:RSX_PIXEL_WINDOW
        NoFmv=$env:BUZZ_NO_FMV; SecondV=$env:GCM_SECOND_V; Mute=$env:PS3_MUTE
        InputConfig=$env:BUZZ_INPUT_CONFIG
        DiscRoot=$env:PS3_DISC_ROOT; ElfArguments=$ArgumentList
    })
    if($global:buzzLauncherFixtureFailure){throw 'Intentional fixture process-creation failure'}
    [pscustomobject]@{Id=424242}
}
function Check($condition,$message){if(-not $condition){throw $message}}
$keys=@('SPURS_JOB_LIST_WORKERS','SPURS_JOB_LIST_IMAGE','SPURS_JOB_CACHE_ENV','RSX_CACHE_ENV',
    'SPURS_JTS_WAKE','SPURS_GUARD_POLL_ONLY','BUZZ_PULSE_MS','GCM_FIFO_STOP_AT_FLIP','RSX_FLIP_BOUNDARY','PS3_VERBOSE',
    'SPURS_MERGE_READY_LISTS','SPURS_NATIVE_EXTRA_FP','RSX_GUEST_SAMPLERS','RSX_PIXEL_WINDOW','BUZZ_NO_FMV','GCM_SECOND_V')
foreach($key in $keys){Remove-Item "Env:$key" -ErrorAction SilentlyContinue}
for($i=0;$i -lt 3;$i++) {
    & (Join-Path $experimentRoot 'run-windows.ps1') -Seconds 0 -MenuBootstrap | Out-Null
    $s=$script:snapshots[$i]
    Check ($s.Workers -eq '4' -and $s.Image -eq '41') 'Consecutive launches lost four-worker geometry'
    Check ($s.JobCache -eq '1' -and $s.RsxCache -eq '1') 'Consecutive launches lost option caches'
    Check ($s.Wake -eq '1' -and $s.Guard -eq '0' -and $s.Pulse -eq '250') 'Consecutive launches lost wake/input defaults'
    Check ($s.Merge -eq '1' -and $s.ExtraFP -eq '1' -and $s.Samplers -eq '1' -and $s.Pixel -eq '1') 'Verified title settings missing from normal launch'
    Check ($s.NoFmv -eq '0') 'Default bootstrap skipped the startup films'
    Check ($s.SecondV -eq '1') 'Startup skip suppressed native menu movie callbacks'
    Check (-not(Test-Path Env:SPURS_JOB_LIST_WORKERS)) 'Unset worker option was restored as an empty variable'
}
foreach($key in $keys){[Environment]::SetEnvironmentVariable($key,'','Process')}
& (Join-Path $experimentRoot 'run-windows.ps1') -Seconds 0 -MenuBootstrap | Out-Null
$s=$script:snapshots[3]
Check ($s.Workers -eq '4' -and $s.JobCache -eq '1' -and $s.RsxCache -eq '1') 'Empty legacy environment disabled defaults'
Check ($s.Fifo -eq '1' -and $s.Flip -eq '1' -and $s.Verbose -eq '0') 'Empty frame/log options disabled defaults'
$env:SPURS_JOB_LIST_WORKERS='2';$env:SPURS_JOB_CACHE_ENV='0';$env:RSX_CACHE_ENV='0';$env:BUZZ_PULSE_MS='80'
$env:SPURS_MERGE_READY_LISTS='0';$env:RSX_PIXEL_WINDOW='0';$env:GCM_SECOND_V='0'
& (Join-Path $experimentRoot 'run-windows.ps1') -Seconds 0 -MenuBootstrap | Out-Null
$s=$script:snapshots[4]
Check ($s.Workers -eq '2' -and $s.JobCache -eq '0' -and $s.RsxCache -eq '0' -and $s.Pulse -eq '80') 'Explicit overrides not preserved'
Check ($s.Merge -eq '0' -and $s.Pixel -eq '0') 'Explicit reference/render overrides not preserved'
Check ($s.SecondV -eq '0' -and $env:GCM_SECOND_V -eq '0') 'Explicit video isolation or restoration lost'
Check ($env:SPURS_JOB_LIST_WORKERS -eq '2' -and $env:SPURS_JOB_CACHE_ENV -eq '0') 'Override restoration failed'
Remove-Item Env:BUZZ_NO_FMV -ErrorAction SilentlyContinue
Remove-Item Env:GCM_SECOND_V -ErrorAction SilentlyContinue
& (Join-Path $experimentRoot 'run-windows.ps1') -Seconds 0 -MenuBootstrap -SkipVideo | Out-Null
Check ($script:snapshots[5].NoFmv -eq '1') 'SkipVideo still played the startup films'
Check ($script:snapshots[5].SecondV -eq '1') 'SkipVideo suppressed the menu movie interrupt'
Check (-not(Test-Path Env:BUZZ_NO_FMV)) 'SkipVideo leaked its bootstrap override'
Check (-not(Test-Path Env:GCM_SECOND_V)) 'SkipVideo leaked its interrupt override'
Remove-Item Env:PS3_MUTE -ErrorAction SilentlyContinue
& (Join-Path $experimentRoot 'run-windows.ps1') -Seconds 0 -MenuBootstrap -Mute | Out-Null
Check ($script:snapshots[6].Mute -eq '1') 'Mute did not reach the game process'
Check (-not(Test-Path Env:PS3_MUTE)) 'Mute leaked into the calling shell'
& (Join-Path $experimentRoot 'run-windows.ps1') -Seconds 0 -MenuBootstrap | Out-Null
Check (-not $script:snapshots[7].Mute) 'Launch without -Mute was muted'
Remove-Item Env:SPURS_JOB_LIST_WORKERS
$global:buzzLauncherFixtureFailure=$true
$caught=$false
try {& (Join-Path $experimentRoot 'run-windows.ps1') -Seconds 0 -MenuBootstrap | Out-Null}
catch {if($_ -notmatch 'Intentional fixture'){throw};$caught=$true}
Check $caught 'Fixture failed to exercise process-creation failure'
Check (-not(Test-Path Env:SPURS_JOB_LIST_WORKERS)) 'Creation failure left bootstrap options behind'
Write-Output 'PASS: real launcher, three consecutive starts, null/empty restoration, explicit overrides, failure cleanup; no game started'
$global:buzzLauncherFixtureFailure=$false
Remove-Item Env:BUZZ_PULSE_MS,Env:BUZZ_INPUT_CONFIG -ErrorAction SilentlyContinue
$configPath=Join-Path $experimentRoot 'buzz-input.example.ini'
& (Join-Path $experimentRoot 'run-windows.ps1') -Seconds 0 -MenuBootstrap -InputConfig $configPath | Out-Null
$s=$script:snapshots[$script:snapshots.Count-1]
Check ($s.InputConfig -eq $configPath) 'Explicit input config not resolved/inherited'
Check ([string]::IsNullOrEmpty($s.Pulse)) 'Launcher default overrode config pulse_ms'
Check (-not(Test-Path Env:BUZZ_INPUT_CONFIG)) 'Input config leaked into caller environment'
$env:BUZZ_INPUT_CONFIG='prior-config-value';$env:BUZZ_PULSE_MS='80'
$global:buzzLauncherFixtureFailure=$true
$caught=$false
try {& (Join-Path $experimentRoot 'run-windows.ps1') -Seconds 0 -MenuBootstrap -InputConfig $configPath | Out-Null}
catch {if($_ -notmatch 'Intentional fixture'){throw};$caught=$true}
Check $caught 'Input fixture did not exercise creation failure'
Check ($env:BUZZ_INPUT_CONFIG -eq 'prior-config-value' -and $env:BUZZ_PULSE_MS -eq '80') 'Input environment not restored on creation failure'
Remove-Item Env:SPURS_JOB_LIST_WORKERS -ErrorAction SilentlyContinue
$countBefore=$script:snapshots.Count;$caught=$false
try {& (Join-Path $experimentRoot 'run-windows.ps1') -Seconds 0 -MenuBootstrap -InputConfig '' | Out-Null}
catch {if($_ -notmatch 'InputConfig must'){throw};$caught=$true}
Check ($caught -and $countBefore -eq $script:snapshots.Count -and -not(Test-Path Env:SPURS_JOB_LIST_WORKERS)) 'Invalid config mutated launch state or started a process'
Write-Output 'PASS: input config inheritance/restoration, file pulse priority and invalid-config early rejection'
$global:buzzLauncherFixtureFailure=$false
$pathsFile=Join-Path $experimentRoot 'buzz-paths.example.json'
$pathsData=Get-Content -LiteralPath $pathsFile -Raw | ConvertFrom-Json
& (Join-Path $experimentRoot 'run-windows.ps1') -Seconds 0 -MenuBootstrap -InputConfig $configPath -PathsConfig $pathsFile | Out-Null
$s=$script:snapshots[$script:snapshots.Count-1]
Check ($s.DiscRoot -eq $pathsData.game_root -and $s.ElfArguments -eq ('"'+$pathsData.eboot_elf+'"')) 'Alternate paths snapshot not used literally'
Write-Output 'PASS: alternate paths snapshot routed to VFS/ELF without editing default path config'
} finally {
    foreach($entry in Get-ChildItem Env:){if(-not $savedEnvironment.ContainsKey($entry.Name)){Remove-Item "Env:$($entry.Name)"}}
    foreach($key in $savedEnvironment.Keys){[Environment]::SetEnvironmentVariable($key,$savedEnvironment[$key],'Process')}
    Remove-Variable buzzLauncherFixtureCaptures,buzzLauncherFixtureFailure -Scope Global -ErrorAction SilentlyContinue
}
