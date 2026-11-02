param([Parameter(Mandatory=$true)][string]$Corpus)
$ErrorActionPreference='Stop'
$geometryRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    & ./toolenv/Scripts/python.exe tools/codegen/generate-geometry-host-skin-job-test.py
    if($LASTEXITCODE){throw 'Host skin oracle generation failed'}
    $geometryVs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $geometryCompiler=& ./toolenv/Scripts/python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    if(-not(Test-Path -LiteralPath build-vulkan-windows/zlib/libzs.lib)){throw 'Build Vulkan first to provide zlib'}
    & (Join-Path $geometryCompiler 'clang-cl.exe') /nologo /std:c++17 /MD /O2 /UNDEBUG /W0 /arch:AVX2 /DSPU_NATIVE_RTZ=1 /DSPU_RTZ_RESULT_GUARD=1 /clang:-mfma /clang:-frounding-math /clang:-ffp-contract=off /Ips3recomp/include /Ips3recomp/runtime/spu /Ithird_party/zlib /Ibuild-vulkan-windows/zlib tests/geometry-host-skin-job-test.cpp /Foanalysis/native-geometry/phase3/host-skin-job-v1/test.obj /Feanalysis/native-geometry/phase3/host-skin-job-v1/test.exe /link build-vulkan-windows/zlib/libzs.lib
    if($LASTEXITCODE){throw 'Host skin test build failed'}
    & ./analysis/native-geometry/phase3/host-skin-job-v1/test.exe (Resolve-Path -LiteralPath $Corpus).Path 2>&1 | Tee-Object -FilePath analysis/native-geometry/phase3/host-skin-job-v1/test.log
    if($LASTEXITCODE){throw 'Host skin test failed'}
} finally { Pop-Location }
