$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory = & .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler = Join-Path $compilerDirectory 'clang-cl.exe'
    foreach ($isa in @('sse4.1','avx2')) {
        & $compiler /nologo /O2 /W0 "-m$isa" /Ips3recomp/include /Ips3recomp/runtime/platform /Foanalysis/ ps3recomp/runtime/spu/tests/test_spu_shufb.c "/Feanalysis/shuffle-$isa.exe"
        if ($LASTEXITCODE) { throw 'Shuffle test build failed' }
        & ".\analysis\shuffle-$isa.exe"
        if ($LASTEXITCODE) { throw 'Shuffle test failed' }
    }
    & $compiler /nologo /O2 /W0 /Foanalysis/ tests/camera-pattern-test.c /Feanalysis/camera-pattern-test.exe
    if ($LASTEXITCODE) { throw 'Camera test build failed' }
    & .\analysis\camera-pattern-test.exe
    if ($LASTEXITCODE) { throw 'Camera test failed' }
} finally { Pop-Location }
