# Controlled A/B of texture storage, not native MSAA. Debug/IQ enabled in BOTH
# cases; do not compare these FPS directly with prior non-debug measurements.
param([ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Tag='launcher-fixed')
$projectRoot = (Split-Path -Parent $PSScriptRoot)

$ErrorActionPreference='Stop'
Push-Location $projectRoot
try {
    $env:SPURS_NATIVE_EXTRA_FP='1'
    $env:SPURS_NATIVE_IMAGE39_FP='0'
    $env:SPU_MFC_SHARDED='0'
    $env:RSX_GUEST_SAMPLERS='1'
    $env:RSX_PIXEL_WINDOW='1'
    $env:SPURS_MERGE_READY_LISTS='1'
    $env:BUZZ_NATIVE_COMPLETION='0'
    $env:BUZZ_COMPLETION_QUEUE='0'
    $env:BUZZ_SCRIPT='12:0x01,45:0x01,70:0x01,90:0x01'
    $env:RSX_TRACK_ZETA='1'
    $env:PERF='1'
    $env:D3D12_DBG='1'
    $env:D3D12_IQ='1'
    $env:PS3_FTRACE_MIN_US='20'
    $env:PS3_FTRACE_CAP='8388608'
    foreach($key in @('SPURS_JOB_TRACE','RSX_SKIP_FULLSCREEN_FP','RSX_CAP_TRIGGER_FILE','SPURS_JOB_PHASEPROF','RSX_CAP','RSX_CAP_AFTER_MS')) {
        Remove-Item "Env:$key" -ErrorAction SilentlyContinue
    }
    foreach($variant in @('A','B')) {
        $env:RSX_PACK_AA_COLOR=if($variant -eq 'B'){'1'}else{'0'}
        $trace=Join-Path $projectRoot "analysis\color-grid-$Tag-$variant-present.trace"
        if(Test-Path -LiteralPath $trace){throw "Preserve existing evidence: $trace already exists"}
        $env:PS3_FTRACE="120:30:$trace"
        if($variant -eq 'B') {
            $env:RSX_CAP='1:1'
            $env:RSX_CAP_MIN_DRAWS='1'
            $env:RSX_CAP_AFTER_MS='152000'
            $env:RSX_CAP_DIR=Join-Path $projectRoot "analysis\color-grid-$Tag-capture"
        }
        Write-Output "START variant=$variant pack=$env:RSX_PACK_AA_COLOR"
        $launchOutput=@(& .\scripts\run-windows.ps1 -Seconds 155 -MenuBootstrap -SkipVideo -Mute)
        $launchOutput | Write-Output
        $pidLine=$launchOutput | Where-Object {$_ -match '^PID='} | Select-Object -First 1
        if($pidLine -notmatch '^PID=\d+ stdout=.* stderr=(.+)$'){throw "Missing launch identity for $variant"}
        $stderrPath=$Matches[1]
        if(-not(Select-String -LiteralPath $stderrPath -Pattern '\[job-pool\].*workers=4')) {
            throw "INVALID benchmark $variant`: four-worker pool was not actually started"
        }
        if(Select-String -LiteralPath $stderrPath -Pattern '\[D3D12-IQ\]\[sev=[01]') {
            throw "D3D12 errors in $variant; inspect $stderrPath"
        }
        & .\toolenv\Scripts\python.exe tests/check-boot-smoke.py $stderrPath
        if($LASTEXITCODE){throw "Boot checks failed for $variant"}
        if(-not(Test-Path -LiteralPath $trace)){throw "Missing timeline: $trace"}
        & .\toolenv\Scripts\python.exe ps3recomp/tools/ftrace_report.py $trace --frames 0 --frame-source present
        if($LASTEXITCODE){throw "Trace report failed for $variant"}
    }
} finally {Pop-Location}
