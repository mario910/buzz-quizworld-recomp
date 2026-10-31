# Geometry compilation/tests share the benchmark CPU. Reject them before
# generation or compiler launch while a game from this workspace is alive.
$geometryGuardRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$geometryGuardPrefix = [IO.Path]::GetFullPath($geometryGuardRoot).TrimEnd('\') + '\'
$geometryGuardGames = @(Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($geometryGuardPrefix, [StringComparison]::OrdinalIgnoreCase) })
if ($geometryGuardGames.Count) {
    throw "Geometry compilation deferred: workspace game PID $($geometryGuardGames.Id -join ', ') is running. Finish the measurement/replay before compiling or running CPU tests."
}
