param([string]$Corpus = '')
$ErrorActionPreference = 'Stop'
$geometryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-driver-test.py
    if ($LASTEXITCODE) { throw 'Driver oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-half-stream-test.py
    if ($LASTEXITCODE) { throw 'Half-stream routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-float32-stream-test.py
    if ($LASTEXITCODE) { throw 'Float32-stream routing oracle generation failed' }
    $geometryVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $geometryCompiler = & .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $geometryCompiler 'clang-cl.exe') /nologo /std:c++17 /MD /O2 /UNDEBUG /W0 /arch:AVX2 /DSPU_NATIVE_RTZ=1 /DSPU_RTZ_RESULT_GUARD=1 /clang:-frounding-math /clang:-ffp-contract=off /Ips3recomp/include /Ips3recomp/runtime/spu /Ithird_party/zlib /Ibuild-vulkan-windows/zlib tests/geometry-driver-flow-test.cpp port/native_geometry_job_flow.cpp /Foanalysis/native-geometry/phase3/ /Feanalysis/native-geometry/phase3/driver-flow-test.exe /link build-vulkan-windows/zlib/libzs.lib
    if ($LASTEXITCODE) { throw 'Native driver flow test build failed' }
    $geometryLog = 'analysis/native-geometry/phase3/driver-flow-test.log'
    if ($Corpus) { & .\analysis\native-geometry\phase3\driver-flow-test.exe (Resolve-Path -LiteralPath $Corpus).Path 2>&1 | Tee-Object -FilePath $geometryLog }
    else { & .\analysis\native-geometry\phase3\driver-flow-test.exe 2>&1 | Tee-Object -FilePath $geometryLog }
    if ($LASTEXITCODE) { throw 'Native driver flow test failed' }
} finally { Pop-Location }
