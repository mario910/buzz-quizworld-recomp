$ErrorActionPreference='Stop'
$geometryRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    New-Item -ItemType Directory -Path analysis/native-geometry/phase3/host-input-v2 -Force | Out-Null
    & ./toolenv/Scripts/python.exe tools/codegen/generate-geometry-unit-vectors-test.py
    if($LASTEXITCODE){throw 'Host unit oracle generation failed'}
    $geometryVs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $geometryCompiler=& ./toolenv/Scripts/python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $geometryCompiler 'clang-cl.exe') /nologo /std:c++17 /MD /O2 /UNDEBUG /W0 /arch:AVX2 /DSPU_NATIVE_RTZ=1 /DSPU_RTZ_RESULT_GUARD=1 /clang:-mfma /clang:-frounding-math /clang:-ffp-contract=off /Ips3recomp/include /Ips3recomp/runtime/spu tests/geometry-host-unit-vectors-test.cpp /Foanalysis/native-geometry/phase3/host-input-v2/host-unit-test.obj /Feanalysis/native-geometry/phase3/host-input-v2/host-unit-test.exe
    if($LASTEXITCODE){throw 'Host unit test build failed'}
    & ./analysis/native-geometry/phase3/host-input-v2/host-unit-test.exe 2>&1 | Tee-Object -FilePath analysis/native-geometry/phase3/host-input-v2/host-unit-test.log
    if($LASTEXITCODE){throw 'Host unit output test failed'}
} finally { Pop-Location }
