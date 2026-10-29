# A/B/A/B menu runs of one EXE: SPURS_JTS_DMA_WAKE off/on. Same scene window,
# no job-trace ALL, no samplers. Each run stops itself after the trace window.
param([string[]]$Order = @('0','1','0','1'), [string]$Tag = 'dmawake', [string]$Script = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
if (Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue) { throw 'Game already running.' }
$hash = (Get-FileHash build-windows\BuzzQuizWorldRecomp.exe -Algorithm SHA256).Hash
"exe sha256=$hash"
$n = 0
foreach ($v in $Order) {
    $n++
    $trace = Join-Path $root "analysis\claude-$Tag-$n-v$v.trace"
    if (Test-Path -LiteralPath $trace) { throw "exists: $trace" }
    $env:PS3_VERBOSE = '0'; $env:RSX_PACK_AA_COLOR = '0'; $env:SPURS_NATIVE_IMAGE39_FP = '0'
    $env:BUZZ_COMPLETION_QUEUE = '0'; $env:BUZZ_NATIVE_GEOMETRY = '0'
    $env:RSX_COMPACT_VERTEX_UPLOAD = '0'; $env:RSX_VERTEX_FETCH_PLAN = '0'; $env:PS3_SHORT_SLEEP_SPIN_US = '0'
    Remove-Item Env:BUZZ_TRACE_TRIGGER,Env:BUZZ_SCRIPT,Env:SPURS_JOB_TRACE_ALL -ErrorAction SilentlyContinue
    if ($Script) { $env:BUZZ_SCRIPT = $Script }
    $env:PS3_FTRACE_WAIT = '0'
    $env:PS3_FTRACE = "100:20:$trace"
    $env:SPURS_JTS_DMA_WAKE = $v
    "run $n variant SPURS_JTS_DMA_WAKE=$v script=$Script -> $trace"
    & .\run-windows.ps1 -Seconds 128 -MenuBootstrap -SkipVideo -Mute | Select-String -Pattern 'PID=|ftrace|Stopped|ExitCode' | ForEach-Object { $_.Line }
    Remove-Item Env:SPURS_JTS_DMA_WAKE,Env:PS3_FTRACE,Env:PS3_FTRACE_WAIT -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 3
}
