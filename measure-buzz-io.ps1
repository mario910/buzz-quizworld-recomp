param(
    [Parameter(Mandatory=$true)][ValidateRange(1,2147483647)][int]$ProcessId,
    [ValidateRange(1,60)][int]$Seconds = 20,
    [ValidateRange(1,10)][int]$IntervalSeconds = 5
)
$ErrorActionPreference = 'Stop'
$expectedExe = Join-Path $PSScriptRoot 'build-windows\BuzzQuizWorldRecomp.exe'
$processInfo = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId"
if (-not $processInfo -or $processInfo.ExecutablePath -ne $expectedExe) {
    throw 'Target must be the running Quiz World experiment from this project.'
}
$logicalProcessors = [Environment]::ProcessorCount
$clock = [Diagnostics.Stopwatch]::StartNew()
while ($clock.Elapsed.TotalSeconds -lt $Seconds) {
    $before = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId"
    if (-not $before -or $before.CreationDate -ne $processInfo.CreationDate) {
        throw 'Experimental process exited or its PID was reused.'
    }
    $started = $clock.Elapsed.TotalSeconds
    $remaining = $Seconds - $started
    if ($remaining -le 0) { break }
    Start-Sleep -Milliseconds ([int](1000 * [Math]::Min($IntervalSeconds, $remaining)))
    $after = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId"
    if (-not $after -or $after.CreationDate -ne $processInfo.CreationDate) {
        throw 'Experimental process exited or its PID was reused.'
    }
    $elapsed = $clock.Elapsed.TotalSeconds - $started
    $cpuTicks = ([double]$after.KernelModeTime - [double]$before.KernelModeTime) +
                ([double]$after.UserModeTime - [double]$before.UserModeTime)
    # These are process I/O counters, including pipes, NOT physical disk
    # bandwidth. Mapped-file paging is not fully represented by these fields.
    [pscustomobject]@{
        Time = Get-Date -Format o
        ProcessId = $ProcessId
        Seconds = [Math]::Round($elapsed, 3)
        ReadKiBps = [Math]::Round(([double]$after.ReadTransferCount - [double]$before.ReadTransferCount) / $elapsed / 1KB, 3)
        WriteKiBps = [Math]::Round(([double]$after.WriteTransferCount - [double]$before.WriteTransferCount) / $elapsed / 1KB, 3)
        ReadOpsPerSecond = [Math]::Round(([double]$after.ReadOperationCount - [double]$before.ReadOperationCount) / $elapsed, 2)
        WriteOpsPerSecond = [Math]::Round(([double]$after.WriteOperationCount - [double]$before.WriteOperationCount) / $elapsed, 2)
        CpuPercentAllProcessors = [Math]::Round(100 * $cpuTicks / 1e7 / $elapsed / $logicalProcessors, 2)
        WorkingSetMiB = [Math]::Round([double]$after.WorkingSetSize / 1MB, 1)
    } | ConvertTo-Json -Compress
}
