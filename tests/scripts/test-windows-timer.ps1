param([Parameter(Mandatory=$true)][int]$GameProcessId, [switch]$HonorTimerResolution, [switch]$SystemManagedTimers,
      [switch]$HighPerformanceProcess, [switch]$SystemManagedPerformance)
$projectRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference = 'Stop'
if ($HonorTimerResolution -and $SystemManagedTimers) { throw 'Choose one timer policy.' }
if ($HighPerformanceProcess -and $SystemManagedPerformance) { throw 'Choose one execution policy.' }
$expected = Join-Path $projectRoot 'build-windows\BuzzQuizWorldRecomp.exe'
$game = Get-Process -Id $GameProcessId
if ($game.Path -ne $expected) { throw 'Target is not this experiment executable.' }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BuzzTimerPolicy {
    [StructLayout(LayoutKind.Sequential)]
    public struct State { public uint Version, ControlMask, StateMask; }
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenProcess(uint access, bool inherit, uint id);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool GetProcessInformation(IntPtr process, int kind, ref State state, uint size);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool SetProcessInformation(IntPtr process, int kind, ref State state, uint size);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
}
'@
$handle = [BuzzTimerPolicy]::OpenProcess(0x600, $false, $GameProcessId)
if ($handle -eq [IntPtr]::Zero) { throw 'Could not open experiment process.' }
try {
    $state = New-Object BuzzTimerPolicy+State
    $state.Version = 1
    if (-not [BuzzTimerPolicy]::GetProcessInformation($handle, 4, [ref]$state, 12)) {
        throw "GetProcessInformation failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    Write-Output "Before: control=$($state.ControlMask) state=$($state.StateMask)"
    if ($HighPerformanceProcess -or $SystemManagedPerformance) {
        if ($HighPerformanceProcess) { $state.ControlMask = $state.ControlMask -bor 1 }
        else { $state.ControlMask = $state.ControlMask -band (-bnot 1) }
        $state.StateMask = $state.StateMask -band (-bnot 1)
        if (-not [BuzzTimerPolicy]::SetProcessInformation($handle, 4, [ref]$state, 12)) {
            throw "SetProcessInformation failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
        }
        Write-Output "Execution policy updated for this experiment only: control=$($state.ControlMask) state=$($state.StateMask)"
    }
    if ($HonorTimerResolution) {
        # Process-scoped only; no global timer, priority or power-plan change.
        $state.ControlMask = $state.ControlMask -bor 4
        $state.StateMask = $state.StateMask -band (-bnot 4)
        if (-not [BuzzTimerPolicy]::SetProcessInformation($handle, 4, [ref]$state, 12)) {
            throw "SetProcessInformation failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
        }
        Write-Output 'This experiment will honor its existing timeBeginPeriod request until exit.'
    } elseif ($SystemManagedTimers) {
        $state.ControlMask = $state.ControlMask -band (-bnot 4)
        $state.StateMask = $state.StateMask -band (-bnot 4)
        if (-not [BuzzTimerPolicy]::SetProcessInformation($handle, 4, [ref]$state, 12)) {
            throw "SetProcessInformation failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
        }
        Write-Output 'Restored system-managed timer throttling for this experiment.'
    }
} finally {
    [void][BuzzTimerPolicy]::CloseHandle($handle)
}
