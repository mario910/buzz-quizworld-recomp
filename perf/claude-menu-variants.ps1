# Menu variants, each "name=ENV1=v;ENV2=v". Same script, window, EXE.
param([string[]]$Variants, [string]$Tag, [int]$Start = 70, [int]$Length = 20, [long]$Affinity = 0, [switch]$MeasureCpu)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
if (Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue) { throw 'Game already running.' }
"exe sha256=$((Get-FileHash build-windows\BuzzQuizWorldRecomp.exe -Algorithm SHA256).Hash)"
$n = 0
foreach ($spec in $Variants) {
    $n++
    $name, $rest = $spec -split '=', 2
    $trace = Join-Path $root "analysis\claude-$Tag-$n-$name.trace"
    if (Test-Path -LiteralPath $trace) { throw "exists: $trace" }
    $env:PS3_VERBOSE = '0'; $env:RSX_PACK_AA_COLOR = '0'; $env:SPURS_NATIVE_IMAGE39_FP = '0'
    Remove-Item Env:BUZZ_COMPLETION_QUEUE,Env:BUZZ_NATIVE_GEOMETRY -ErrorAction SilentlyContinue
    $env:RSX_COMPACT_VERTEX_UPLOAD = '0'; $env:RSX_VERTEX_FETCH_PLAN = '0'; $env:PS3_SHORT_SLEEP_SPIN_US = '0'
    Remove-Item Env:BUZZ_TRACE_TRIGGER,Env:SPURS_JOB_TRACE_ALL -ErrorAction SilentlyContinue
    $set = @()
    if ($rest) { foreach ($kv in $rest -split ';') { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v; $set += $k } }
    $env:BUZZ_SCRIPT = '12:0x01'   # language only: the offline frontend has no PSN dialog
    $env:PS3_FTRACE_WAIT = '0'; $env:PS3_FTRACE = "${Start}:${Length}:$trace"
    "run $n $spec -> $trace"
    if ($Affinity -or $MeasureCpu) {
        $out = & .\scripts\run-windows.ps1 -Seconds 0 -MenuBootstrap -SkipVideo -Mute -Background | Out-String
        $pidMatch = [regex]::Match($out, 'PID=(\d+)')
        if (-not $pidMatch.Success) { throw 'Missing benchmark process ID' }
        $gamePid = [int]$pidMatch.Groups[1].Value
        $game = Get-Process -Id $gamePid
        $expectedExe = (Resolve-Path -LiteralPath build-windows\BuzzQuizWorldRecomp.exe).Path
        if ($game.Path -ne $expectedExe) { throw 'Benchmark process path mismatch; left it untouched' }
        # Full CPU unless -Affinity was explicitly requested. CPU window is
        # approximately the trace window (process launch vs first trace hook).
        $watch = [Diagnostics.Stopwatch]::StartNew()
        function Wait-BuzzBenchUntil([double]$seconds) {
            while ($watch.Elapsed.TotalSeconds -lt $seconds) {
                $ms=[int][Math]::Max(1,[Math]::Min(10000,1000*($seconds-$watch.Elapsed.TotalSeconds)))
                if ($game.WaitForExit($ms)) { throw "Benchmark process exited early: $($game.ExitCode)" }
            }
        }
        try {
            if ($Affinity) { $game.ProcessorAffinity = [IntPtr]$Affinity }
            Wait-BuzzBenchUntil $Start
            $game.Refresh(); $cpu0 = $game.TotalProcessorTime.TotalSeconds; $wall0=$watch.Elapsed.TotalSeconds
            Wait-BuzzBenchUntil ($Start+$Length)
            $game.Refresh(); $cpu1 = $game.TotalProcessorTime.TotalSeconds; $wall=$watch.Elapsed.TotalSeconds-$wall0
            "cpu variant={0}: {1:N3} s over {2:N3} s = {3:N3} cores busy; affinity={4}" -f $name,($cpu1-$cpu0),$wall,(($cpu1-$cpu0)/$wall),$Affinity
            Wait-BuzzBenchUntil ($Start+$Length+8)
        } finally {
            if (-not $game.HasExited) { Stop-Process -Id $gamePid -Force }
        }
        $log = [regex]::Match($out, 'stderr=(\S.*?\.stderr\.log)').Groups[1].Value
        Get-Content -LiteralPath $log | Select-String -Pattern 'ftrace\] wrote' | ForEach-Object { $_.Line }
    } else {
        & .\scripts\run-windows.ps1 -Seconds ($Start + $Length + 8) -MenuBootstrap -SkipVideo -Mute -Background | Select-String -Pattern 'ftrace\] wrote' | ForEach-Object { $_.Line }
    }
    foreach ($k in $set + @('BUZZ_SCRIPT','PS3_FTRACE','PS3_FTRACE_WAIT')) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 3
}
