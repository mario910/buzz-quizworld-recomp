$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    foreach($test in @('frame-refs','pipeline-index')) {
        & $compiler /nologo /O2 /W0 "tests/$test-test.c" "/Foanalysis/$test-test.obj" "/Feanalysis/$test-test.exe"
        if($LASTEXITCODE){throw "Build failed: $test"}
        & ".\analysis\$test-test.exe"
        if($LASTEXITCODE){throw "Test failed: $test"}
    }
} finally {Pop-Location}
