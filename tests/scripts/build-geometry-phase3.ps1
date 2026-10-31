param([ValidatePattern('^build-[A-Za-z0-9_-]+$')][string]$BuildDirectory = 'build-vulkan-windows')
$ErrorActionPreference = 'Stop'
$geometryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-phase3.py
    if ($LASTEXITCODE) { throw 'Phase 3 generation failed' }
    $geometryOut = Join-Path $geometryRoot 'analysis/native-geometry/phase3'
    $geometryVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    & .\toolenv\Scripts\cmake.exe -S port -B $BuildDirectory "-DPORT_GEOMETRY_SOURCE=$geometryOut/spu_recomp.c" *> "$geometryOut/configure.log"
    if ($LASTEXITCODE) { throw 'Phase 3 configure failed' }
    & .\toolenv\Scripts\cmake.exe --build $BuildDirectory --parallel 6 *> "$geometryOut/build.log"
    if ($LASTEXITCODE) { throw 'Phase 3 build failed; see analysis/native-geometry/phase3/build.log' }
    $geometryManifestPath = "$BuildDirectory/game-build.json"
    $geometryManifest = Get-Content $geometryManifestPath -Raw | ConvertFrom-Json
    $geometryManifest.exe_sha256 = (Get-FileHash "$BuildDirectory/BuzzQuizWorldRecomp.exe").Hash.ToLowerInvariant()
    $geometryManifest | ConvertTo-Json -Depth 6 | Set-Content $geometryManifestPath -Encoding utf8
    "Phase 3 audit build ready: $BuildDirectory"
} finally { Pop-Location }
