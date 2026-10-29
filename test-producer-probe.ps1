$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    foreach($test in @('test_producer_probe.py','test_producer_intervals.py','test_dependency_intervals.py')) {
        & .\toolenv\Scripts\python.exe "tests/$test"
        if($LASTEXITCODE){throw "Producer diagnostic test failed: $test"}
    }
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $compilerDirectory 'clang-cl.exe') /nologo /O2 /W0 /Ips3recomp/include tests/producer-probe-scope-test.cpp /Foanalysis/producer-probe-scope-test.obj /Feanalysis/producer-probe-scope-test.exe
    if($LASTEXITCODE){throw 'Producer scope test build failed'}
    & .\analysis\producer-probe-scope-test.exe
    if($LASTEXITCODE){throw 'Producer scope test failed'}
    & (Join-Path $compilerDirectory 'clang-cl.exe') /nologo /O2 /W0 tests/ftrace-deferred-test.c /Foanalysis/ftrace-deferred-test.obj /Feanalysis/ftrace-deferred-test.exe
    if($LASTEXITCODE){throw 'Deferred capture test build failed'}
    & .\analysis\ftrace-deferred-test.exe (Join-Path $PSScriptRoot 'analysis\ftrace-deferred-test.trace')
    if($LASTEXITCODE){throw 'Deferred capture test failed'}
    & .\analysis\ftrace-deferred-test.exe (Join-Path $PSScriptRoot 'analysis\ftrace-immediate-test.trace') --immediate
    if($LASTEXITCODE){throw 'Immediate capture regression test failed'}
} finally {Pop-Location}
