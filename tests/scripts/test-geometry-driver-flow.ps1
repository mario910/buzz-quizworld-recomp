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
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-i16-stream-test.py
    if ($LASTEXITCODE) { throw 'I16 stream routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-cmp-stream-test.py
    if ($LASTEXITCODE) { throw 'CMP stream routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-u8-integer-stream-test.py
    if ($LASTEXITCODE) { throw 'U8_INTEGER stream routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-blend-scatter-test.py
    if ($LASTEXITCODE) { throw 'Blend scatter routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-run-lists-test.py
    if ($LASTEXITCODE) { throw 'Run-list routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-custom-stream-test.py
    if ($LASTEXITCODE) { throw 'Custom-stream routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-snorm-stream-test.py
    if ($LASTEXITCODE) { throw 'SNORM stream routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-fixed-stream-test.py
    if ($LASTEXITCODE) { throw 'Fixed stream reference generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-float32-stream-test.py
    if ($LASTEXITCODE) { throw 'Float32-stream routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-skin-dispatch-test.py
    if ($LASTEXITCODE) { throw 'Skin dispatch routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-weighted-skin-test.py
    if ($LASTEXITCODE) { throw 'Weighted skin routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-skin-directions-test.py
    if ($LASTEXITCODE) { throw 'Skin directions routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-skin-flow-test.py
    if ($LASTEXITCODE) { throw 'Skin flow routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-rigid-skin-test.py
    if ($LASTEXITCODE) { throw 'Rigid skin routing oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-skin-cofactor-test.py
    if ($LASTEXITCODE) { throw 'Skin cofactor routing oracle generation failed' }
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
