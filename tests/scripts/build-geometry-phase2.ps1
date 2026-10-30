param([ValidatePattern('^build-[A-Za-z0-9_-]+$')][string]$BuildDirectory = 'build-vulkan-windows')
$ErrorActionPreference = 'Stop'
$geometryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
try {
    $geometryExe = Join-Path $geometryRoot "$BuildDirectory/BuzzQuizWorldRecomp.exe"
    if (Get-Process BuzzQuizWorldRecomp -ErrorAction SilentlyContinue | Where-Object Path -EQ $geometryExe) {
        throw 'Selected game build is running; finish that test before rebuilding.'
    }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-phase2.py
    if ($LASTEXITCODE) { throw 'Phase 2 generation failed' }
    $geometryOut = Join-Path $geometryRoot 'analysis/native-geometry/phase2'
    $geometryVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    & .\toolenv\Scripts\cmake.exe -S port -B $BuildDirectory "-DPORT_GEOMETRY_SOURCE=$geometryOut/spu_recomp.c" *> "$geometryOut/configure.log"
    if ($LASTEXITCODE) { throw "Phase 2 configure failed; see $geometryOut/configure.log" }
    & .\toolenv\Scripts\cmake.exe --build $BuildDirectory --parallel 6 *> "$geometryOut/build.log"
    if ($LASTEXITCODE) { throw "Phase 2 build failed; see $geometryOut/build.log" }
    $geometryManifestPath = "$BuildDirectory/game-build.json"
    $geometryManifest = Get-Content $geometryManifestPath -Raw | ConvertFrom-Json
    $geometryManifest.exe_sha256 = (Get-FileHash -LiteralPath $geometryExe -Algorithm SHA256).Hash.ToLowerInvariant()
    $geometryManifest | ConvertTo-Json -Depth 6 | Set-Content $geometryManifestPath -Encoding utf8
    "Phase 2 build ready: $geometryExe"
} finally { Pop-Location }
