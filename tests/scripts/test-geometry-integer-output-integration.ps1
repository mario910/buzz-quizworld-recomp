$ErrorActionPreference = 'Stop'
$geometryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-integer-output-integration-test.py
    if ($LASTEXITCODE) { throw 'Integer integration oracle generation failed' }
    $geometryVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $geometryCompiler = & .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $geometryCompiler 'clang-cl.exe') /nologo /std:c++17 /MD /O2 /UNDEBUG /W0 /arch:AVX2 /DSPU_NATIVE_RTZ=1 /DSPU_RTZ_RESULT_GUARD=1 /clang:-frounding-math /clang:-ffp-contract=off /Ips3recomp/include /Ips3recomp/runtime/spu tests/geometry-integer-output-integration-test.cpp /Foanalysis/native-geometry/phase3/integer-output-integration-test.obj /Feanalysis/native-geometry/phase3/integer-output-integration-test.exe
    if ($LASTEXITCODE) { throw 'Integer integration test build failed' }
    & .\analysis\native-geometry\phase3\integer-output-integration-test.exe 2>&1 | Tee-Object -FilePath analysis/native-geometry/phase3/integer-output-integration-test.log
    if ($LASTEXITCODE) { throw 'Integer integration test failed' }
} finally { Pop-Location }
