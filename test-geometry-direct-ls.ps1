$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    & .\toolenv\Scripts\python.exe generate-geometry-native.py --direct-ls
    if($LASTEXITCODE){throw 'Geometry generation failed'}
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    & $compiler /nologo /O2 /W0 /Ips3recomp/include /Ips3recomp/runtime/spu -mavx2 -mfma /DSPU_NATIVE_RTZ /clang:-frounding-math /clang:-ffp-contract=off tests/geometry-direct-ls-test.c /Foanalysis/geometry-direct-ls-test.obj /Feanalysis/geometry-direct-ls-test.exe
    if($LASTEXITCODE){throw 'Direct-LS geometry test build failed'}
    & .\analysis\geometry-direct-ls-test.exe
    if($LASTEXITCODE){throw 'Direct-LS geometry test failed'}
} finally {Pop-Location}
