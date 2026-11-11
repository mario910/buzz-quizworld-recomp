# CPU use per named thread of the running game over a window (diagnostic).
#   claude-thread-cpu.ps1 [-Seconds 20] [-ProcessName BuzzQuizWorldRecomp]
# Prints each thread's share of one core (kernel+user time delta / wall time),
# with the runtime's thread descriptions (thread_name.h).
param([int]$Seconds = 20, [string]$ProcessName = 'BuzzQuizWorldRecomp')
$ErrorActionPreference = 'Stop'
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class ThreadName {
  [DllImport("kernel32.dll")] static extern IntPtr OpenThread(int access, bool inherit, int id);
  [DllImport("kernel32.dll")] static extern int GetThreadDescription(IntPtr thread, out IntPtr text);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
  [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);
  public static string Of(int id) {
    IntPtr h = OpenThread(0x0800, false, id); if (h == IntPtr.Zero) return "";
    IntPtr d; string s = ""; if (GetThreadDescription(h, out d) >= 0) { s = Marshal.PtrToStringUni(d); LocalFree(d); }
    CloseHandle(h); return s; } }
'@
function Snapshot { $t = @{}; foreach ($x in (Get-Process -Name $ProcessName).Threads) { $t[$x.Id] = $x.TotalProcessorTime.TotalMilliseconds }; $t }
$a = Snapshot; $t0 = Get-Date; Start-Sleep -Seconds $Seconds; $b = Snapshot; $wall = ((Get-Date) - $t0).TotalMilliseconds
$rows = foreach ($id in $b.Keys) { if ($a.ContainsKey($id)) { [pscustomobject]@{ Pct = 100 * ($b[$id] - $a[$id]) / $wall; Name = [ThreadName]::Of($id); Id = $id } } }
$rows | Where-Object Pct -ge 1 | Sort-Object Pct -Descending | ForEach-Object { '{0,6:N1}%  {1} ({2})' -f $_.Pct, $_.Name, $_.Id }
'total {0:N1}% of one core' -f (($rows | Measure-Object Pct -Sum).Sum)
