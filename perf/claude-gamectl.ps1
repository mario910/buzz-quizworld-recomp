# Drive the running experiment window WITHOUT taking focus: key taps are posted
# as WM_KEYDOWN/WM_KEYUP (the virtual buzzer queues them as presses) and
# screenshots use PrintWindow(PW_RENDERFULLCONTENT).
#   claude-gamectl.ps1 shot <png>
#   claude-gamectl.ps1 key <VK hex/decimal> [-HoldMs n]
param([string]$Action, [string]$Arg, [int]$HoldMs = 120)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class GameWin {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][GameWin]::SetProcessDPIAware()
$p = Get-Process -Name BuzzQuizWorldRecomp -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { throw 'no game window' }
$h = $p.MainWindowHandle
switch ($Action) {
  'shot' {
    $r = New-Object GameWin+RECT; [void][GameWin]::GetWindowRect($h, [ref]$r)
    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $ok = [GameWin]::PrintWindow($h, $hdc, 2)   # PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc)
    $bmp.Save($Arg, [System.Drawing.Imaging.ImageFormat]::Png)
    "saved $Arg $($bmp.Width)x$($bmp.Height) printwindow=$ok"
  }
  'key' {
    $vk = [IntPtr][Convert]::ToInt32($Arg, $(if ($Arg -like '0x*') {16} else {10}))
    [void][GameWin]::PostMessage($h, 0x0100, $vk, [IntPtr]1)            # WM_KEYDOWN
    Start-Sleep -Milliseconds $HoldMs
    [void][GameWin]::PostMessage($h, 0x0101, $vk, [IntPtr]0xC0000001)   # WM_KEYUP
    "posted vk=$vk"
  }
}
