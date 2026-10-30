# Build a private replay-only tracer, then restore the selected game build.
param([ValidatePattern('^build-[A-Za-z0-9_-]+$')][string]$BuildDirectory = 'build-vulkan-windows')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $root
try {
    $exe = Join-Path $root "$BuildDirectory/BuzzQuizWorldRecomp.exe"
    if (Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue | Where-Object Path -EQ $exe) {
        throw 'The selected game build is running; close the test before building.'
    }
    $cache = Get-Content -LiteralPath "$BuildDirectory/CMakeCache.txt"
    $originalSource = ($cache | Where-Object { $_ -match '^PORT_GEOMETRY_SOURCE:FILEPATH=' }) -replace '^PORT_GEOMETRY_SOURCE:FILEPATH=', ''
    if ($null -eq $originalSource) { throw 'Geometry source cache entry is missing' }
    $out = Join-Path $root 'analysis/native-geometry'
    New-Item -ItemType Directory -Force $out | Out-Null
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-trace.py
    if ($LASTEXITCODE) { throw 'Trace generation failed' }
    $vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $cmake = Join-Path $root 'toolenv/Scripts/cmake.exe'
    try {
        & $cmake -S port -B $BuildDirectory "-DPORT_GEOMETRY_SOURCE=$out/trace-lift/spu_recomp.c" *> "$out/trace-configure.log"
        if ($LASTEXITCODE) { throw "Trace configure failed; inspect $out/trace-configure.log" }
        & $cmake --build $BuildDirectory --parallel 6 *> "$out/trace-build.log"
        if ($LASTEXITCODE) { throw "Trace build failed; inspect $out/trace-build.log" }
        Copy-Item -LiteralPath $exe -Destination "$out/trace-game.exe"
    } finally {
        & $cmake -S port -B $BuildDirectory "-DPORT_GEOMETRY_SOURCE=$originalSource" *> "$out/restore-configure.log"
        if ($LASTEXITCODE) { throw "Restore configure failed; inspect $out/restore-configure.log" }
        & $cmake --build $BuildDirectory --parallel 6 *> "$out/restore-build.log"
        if ($LASTEXITCODE) { throw "Restore build failed; inspect $out/restore-build.log" }
        $manifestPath = Join-Path $root "$BuildDirectory/game-build.json"
        if (Test-Path -LiteralPath $manifestPath) {
            $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
            $manifest.exe_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
            $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
        }
    }
    Write-Output "Replay-only tracer: $out/trace-game.exe; original game build restored"
} finally { Pop-Location }
