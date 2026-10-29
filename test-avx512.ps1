$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    & $compiler /nologo /O2 /W0 tests/cpu-isa-test.c /Foanalysis/cpu-isa-test.obj /Feanalysis/cpu-isa-test.exe
    if($LASTEXITCODE){throw 'CPU gate build failed'}
    & .\analysis\cpu-isa-test.exe
    if($LASTEXITCODE){throw 'AVX512 unavailable; do not execute optional-ISA tests'}
    foreach($reject in @(0,1)) {
        $flags=@();if($reject){$flags+='/DTEST_REJECT_ISA'}
        & $compiler /nologo /O2 /W0 @flags tests/cpu-isa-gate-test.cpp "/Foanalysis/cpu-isa-gate-$reject.obj" "/Feanalysis/cpu-isa-gate-$reject.exe"
        if($LASTEXITCODE){throw 'Early gate test build failed'}
        & ".\analysis\cpu-isa-gate-$reject.exe"
        $expected=if($reject){2}else{0}
        if($LASTEXITCODE -ne $expected){throw "Early gate ordering failed: reject=$reject exit=$LASTEXITCODE"}
    }
    & .\toolenv\Scripts\python.exe generate-geometry-native.py
    if($LASTEXITCODE){throw 'Geometry test generation failed'}
    $digests=@()
    foreach($isa in @('avx2','avx512')) {
        $flags=@('-mavx2');if($isa -eq 'avx512'){$flags+=@('-mavx512f','-mavx512bw','-mavx512vl')}
        & $compiler /nologo /O2 /W0 /Ips3recomp/include /Ips3recomp/runtime/spu @flags -mfma /DSPU_NATIVE_RTZ /DSPU_RTZ_RESULT_GUARD /DTEST_GEOMETRY_DIGEST /clang:-frounding-math /clang:-ffp-contract=off tests/native-geometry-test.c "/Foanalysis/geometry-$isa.obj" "/Feanalysis/geometry-$isa.exe"
        if($LASTEXITCODE){throw "Geometry test build failed: $isa"}
        $result=& ".\analysis\geometry-$isa.exe"
        if($LASTEXITCODE){throw "Geometry test failed: $isa"}
        $result | Write-Output
        $digest=@($result | Select-String '^GEOMETRY_DIGEST=')
        if($digest.Count -ne 1){throw 'Missing state digest'}
        $digests+=$digest[0].Line
    }
    if($digests[0] -ne $digests[1]){throw 'AVX2 and AVX512 architectural states differ'}
    Write-Output 'PASS: same full-state digest across separately compiled AVX2 and AVX512 kernels'
} finally {Pop-Location}
