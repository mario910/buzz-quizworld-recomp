$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    & .\toolenv\Scripts\python.exe generate-geometry-native.py --shuffle-plans
    if($LASTEXITCODE){throw 'Geometry plan generation failed'}
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    foreach($case in @('geometry-shuffle-plan-test','native-geometry-test')) {
        & $compiler /nologo /O2 /W0 /Ips3recomp/include /Ips3recomp/runtime/spu -mavx2 -mfma /DSPU_NATIVE_RTZ /DSPU_RTZ_RESULT_GUARD /DTEST_GEOMETRY_SHUFFLE_PLANS /clang:-frounding-math /clang:-ffp-contract=off "tests/$case.c" "/Foanalysis/$case-plans.obj" "/Feanalysis/$case-plans.exe"
        if($LASTEXITCODE){throw "Plan test build failed: $case"}
        & ".\analysis\$case-plans.exe"
        if($LASTEXITCODE){throw "Plan test failed: $case"}
    }
} finally {Pop-Location}
