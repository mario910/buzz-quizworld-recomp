param([string]$Snapshot = '')
$ErrorActionPreference = 'Stop'
$geometryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
try {
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-phase2.py
    if ($LASTEXITCODE) { throw 'Phase 2 generation failed' }
    $geometryVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $geometryCompiler = & .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $geometryCompiler 'clang-cl.exe') /nologo /O2 /W0 /Ips3recomp/include /Ips3recomp/runtime/spu -mavx2 -mfma /DSPU_NATIVE_RTZ /clang:-frounding-math /clang:-ffp-contract=off tests/geometry-phase2-test.cpp /Foanalysis/native-geometry/phase2/test.obj /Feanalysis/native-geometry/phase2/test.exe
    if ($LASTEXITCODE) { throw 'Phase 2 test build failed' }
    if ($Snapshot) { & .\analysis\native-geometry\phase2\test.exe $Snapshot }
    else { & .\analysis\native-geometry\phase2\test.exe }
    if ($LASTEXITCODE) { throw 'Phase 2 test failed' }
} finally { Pop-Location }
