param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$EvidenceDirectory
)
$ErrorActionPreference='Stop'
$embeddedTestRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $embeddedTestRoot
$embeddedTestPrevious=@{}
function Set-EmbeddedTestOption($Name,$Value) {
    if($null -eq $Value){Remove-Item "Env:$Name" -ErrorAction SilentlyContinue}
    else{[Environment]::SetEnvironmentVariable($Name,$Value,'Process')}
}
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    if(Test-Path $EvidenceDirectory){throw 'Evidence exists'}
    New-Item -ItemType Directory -Path $EvidenceDirectory | Out-Null
    $embeddedTestExe=(Resolve-Path "$BuildDirectory/BuzzQuizWorldRecomp.exe").Path
    $embeddedTestBinding=Get-Content "$BuildDirectory/game-build.json" -Raw | ConvertFrom-Json
    . ./scripts/buzz-runtime-defaults.ps1
    if(-not (Test-BuzzEmbeddedRuntimeProfile $embeddedTestBinding) -or
       (Get-FileHash $embeddedTestExe).Hash.ToLowerInvariant() -ne $embeddedTestBinding.exe_sha256){throw 'Wrong embedded-profile EXE'}
    $embeddedTestOptions=@(Get-Content port/runtime_defaults.inc | ForEach-Object {
        if($_ -match '^X\((\w+), "([^"]+)"\)'){@{Name=$Matches[1];Value=$Matches[2]}}
    })
    $embeddedTestKeys=@(@($embeddedTestOptions | ForEach-Object Name)+@(
        Get-ChildItem Env: | Where-Object {$_.Name -match '^BUZZ_(GEOMETRY|ANIMATION|STAGEF|IMAGE|FUNCTION_CHECK|REPLAY)'} | ForEach-Object Name
    )+@('BUZZ_REPLAY_CORPUS','BUZZ_REPLAY_CONTRACT') | Select-Object -Unique)
    foreach($embeddedTestKey in $embeddedTestKeys) {
        $embeddedTestPrevious[$embeddedTestKey]=[Environment]::GetEnvironmentVariable($embeddedTestKey,'Process')
        Set-EmbeddedTestOption $embeddedTestKey $null
    }
    function Invoke-EmbeddedTest($Name,$Argument) {
        $embeddedTestLog=Join-Path $EvidenceDirectory $Name
        $embeddedTestProcess=New-Object Diagnostics.Process
        $embeddedTestProcess.StartInfo=New-Object Diagnostics.ProcessStartInfo
        $embeddedTestProcess.StartInfo.FileName=$embeddedTestExe
        $embeddedTestProcess.StartInfo.Arguments=$Argument
        $embeddedTestProcess.StartInfo.WorkingDirectory=$embeddedTestRoot
        $embeddedTestProcess.StartInfo.UseShellExecute=$false
        $embeddedTestProcess.StartInfo.CreateNoWindow=$true
        $embeddedTestProcess.StartInfo.RedirectStandardOutput=$true
        $embeddedTestProcess.StartInfo.RedirectStandardError=$true
        $null=$embeddedTestProcess.Start()
        $embeddedTestStdout=$embeddedTestProcess.StandardOutput.ReadToEndAsync()
        $embeddedTestStderr=$embeddedTestProcess.StandardError.ReadToEndAsync()
        $embeddedTestProcess.WaitForExit()
        [IO.File]::WriteAllText("$embeddedTestLog.stdout",$embeddedTestStdout.Result)
        [IO.File]::WriteAllText("$embeddedTestLog.stderr",$embeddedTestStderr.Result)
        [IO.File]::WriteAllText("$embeddedTestLog.exit",[string]$embeddedTestProcess.ExitCode)
        if($embeddedTestProcess.ExitCode){throw "$Name failed: $($embeddedTestProcess.ExitCode)"}
        return ((Get-Content "$embeddedTestLog.stdout" -Raw)+(Get-Content "$embeddedTestLog.stderr" -Raw))
    }
    $embeddedTestText=Invoke-EmbeddedTest 'bare-defaults' '--runtime-defaults'
    foreach($embeddedTestOption in $embeddedTestOptions) {
        if($embeddedTestText -notmatch "(?m)^$($embeddedTestOption.Name)=$([regex]::Escape($embeddedTestOption.Value))\r?$"){throw "Missing bare default $($embeddedTestOption.Name)"}
    }
    foreach($embeddedTestPattern in @('\[stagef\] mode=1','\[image7\].*mode=1','\[image36\].*mode=1','\[geometry-native\] mode=1 kernels=0x33B')) {
        if($embeddedTestText -notmatch $embeddedTestPattern){throw "Constructor did not see defaults: $embeddedTestPattern"}
    }
    'PASS bare EXE defaults and real native constructors'
    foreach($embeddedTestOption in $embeddedTestOptions){[Environment]::SetEnvironmentVariable($embeddedTestOption.Name,'0','Process')}
    $embeddedTestText=Invoke-EmbeddedTest 'explicit-off' '--runtime-defaults'
    foreach($embeddedTestOption in $embeddedTestOptions) {
        if($embeddedTestText -notmatch "(?m)^$($embeddedTestOption.Name)=0\r?$"){throw "Lost override $($embeddedTestOption.Name)"}
        Set-EmbeddedTestOption $embeddedTestOption.Name $null
    }
    'PASS explicit OFF overrides for every profile option'
    $env:BUZZ_REPLAY_CONTRACT='1'
    foreach($embeddedTestArm in @(
        @{Name='mixed-default';Corpus='analysis/native-geometry/phase4/default-launch-smoke-v1/corpus';Jobs=20;Transfers=0;Mode=$null},
        @{Name='mixed-off';Corpus='analysis/native-geometry/phase4/default-launch-smoke-v1/corpus';Jobs=20;Transfers=0;Mode='0'},
        @{Name='mixed-checked';Corpus='analysis/native-geometry/phase4/default-launch-smoke-v1/corpus';Jobs=20;Transfers=0;Mode='2'},
        @{Name='image40-default';Corpus='analysis/native-geometry/replay-final/all/image40';Jobs=17714;Transfers=115162;Mode=$null},
        @{Name='image41-default';Corpus='analysis/native-geometry/replay-final/all/image41';Jobs=19508;Transfers=58195;Mode=$null}
    )) {
        Set-EmbeddedTestOption 'BUZZ_GEOMETRY_JOB' $embeddedTestArm.Mode
        Set-EmbeddedTestOption 'BUZZ_ANIMATION_JOB' $embeddedTestArm.Mode
        # Image-40-only corpus cannot meet the image-41 controller coverage gate.
        if($embeddedTestArm.Name -eq 'image40-default'){$env:BUZZ_GEOMETRY_JOB='0'}
        $env:BUZZ_REPLAY_CORPUS=(Resolve-Path $embeddedTestArm.Corpus).Path
        $embeddedTestText=Invoke-EmbeddedTest $embeddedTestArm.Name 'unused-ELF-offline-replay'
        if(-not $embeddedTestText.Contains("[replay] $($embeddedTestArm.Jobs) exact, 0 with concurrent input changes, 0 FAILED")){throw 'Replay failed'}
        if($embeddedTestArm.Transfers -and -not $embeddedTestText.Contains("($($embeddedTestArm.Transfers) transfers)")){throw 'Incomplete replay'}
        if($embeddedTestArm.Name -eq 'image40-default' -and -not $embeddedTestText.Contains('attempted=17714 native=17714 compared=0 fallback=0 audit-skipped=0')){throw 'Native animation default did not execute'}
        if($embeddedTestArm.Name -eq 'image41-default' -and -not $embeddedTestText.Contains('mode=1 attempted=19508 executed=17437 checked=0 fallback=2071 failed=0')){throw 'Native geometry default did not execute'}
        "PASS $($embeddedTestArm.Name): $($embeddedTestArm.Jobs) exact jobs"
    }
    Copy-Item "$BuildDirectory/game-build.json" "$EvidenceDirectory/game-build.json"
} finally {
    foreach($embeddedTestKey in $embeddedTestPrevious.Keys){Set-EmbeddedTestOption $embeddedTestKey $embeddedTestPrevious[$embeddedTestKey]}
    Pop-Location
}
