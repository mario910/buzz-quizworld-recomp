$ErrorActionPreference = 'Stop'
$geometryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-skin-flow-test.py
    if ($LASTEXITCODE) { throw 'Skin-flow oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-rigid-skin-test.py
    if ($LASTEXITCODE) { throw 'Rigid skin integration oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-skin-cofactor-test.py
    if ($LASTEXITCODE) { throw 'Cofactor integration oracle generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-skin-normalized-test.py
    if ($LASTEXITCODE) { throw 'Normalized direction integration oracle generation failed' }
    $geometryVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $geometryCompiler = & .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $geometryCompiler 'clang-cl.exe') /nologo /std:c++17 /MD /O2 /UNDEBUG /W0 /arch:AVX2 /Ips3recomp/include /Ips3recomp/runtime/spu tests/geometry-skin-flow-test.cpp /Foanalysis/native-geometry/phase3/skin-flow-test.obj /Feanalysis/native-geometry/phase3/skin-flow-test.exe
    if ($LASTEXITCODE) { throw 'Native skin-flow test build failed' }
    & .\analysis\native-geometry\phase3\skin-flow-test.exe 2>&1 | Tee-Object -FilePath analysis/native-geometry/phase3/skin-flow-test.log
    if ($LASTEXITCODE) { throw 'Native skin-flow test failed' }
} finally { Pop-Location }
