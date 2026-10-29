param([Parameter(Mandatory=$true)][int]$ProcessId)
$projectRoot = (Split-Path -Parent $PSScriptRoot)

$ErrorActionPreference='Stop'
Push-Location $projectRoot
try {
    $expected=Join-Path $projectRoot 'build-windows\BuzzQuizWorldRecomp.exe'
    $game=Get-Process -Id $ProcessId
    if($game.Path -ne $expected){throw 'PID is not this experiment; no threads will be touched'}
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $compilerDirectory 'clang-cl.exe') /nologo /O2 /W0 tests/sample-live-stacks.c /Foanalysis/sample-live-stacks.obj /Feanalysis/sample-live-stacks.exe /link dbghelp.lib psapi.lib
    if($LASTEXITCODE){throw 'Unwind sampler build failed'}
    $stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
    $raw="analysis/live-stacks-$stamp.raw.txt"
    $report="analysis/live-stacks-$stamp.txt"
    # The native helper revalidates the PID/path immediately before sampling.
    & .\analysis\sample-live-stacks.exe $ProcessId $expected > $raw
    if($LASTEXITCODE){throw 'Unwind sampler failed; inspect the raw log'}
    .\toolenv\Scripts\python.exe perf/symbolize-live-stacks.py $raw > $report
    if($LASTEXITCODE){throw 'Unwind symbolization failed'}
    Write-Output "Diagnostic report: $report (sampling perturbs scheduling; not a benchmark)"
    Get-Content -LiteralPath $report
} finally {Pop-Location}
