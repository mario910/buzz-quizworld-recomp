$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try{
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    foreach($isa in @('sse2','sse4.1','avx2')){
        & $compiler /nologo /O2 /W0 "-m$isa" /Ips3recomp/include /Ips3recomp/runtime/spu tests/fi-simd-test.c /Foanalysis/fi-simd-test.obj "/Feanalysis/fi-$isa-test.exe"
        if($LASTEXITCODE){throw "FI build failed: $isa"}
        Write-Output "ISA=$isa"
        & ".\analysis\fi-$isa-test.exe"
        if($LASTEXITCODE){throw "FI test failed: $isa"}
    }
}finally{Pop-Location}
