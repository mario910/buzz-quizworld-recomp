$projectRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
Push-Location $projectRoot
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $compilerDirectory 'clang-cl.exe') /nologo /O2 tests/frame-boundary-test.c /Foanalysis/frame-boundary-test.obj /Feanalysis/frame-boundary-test.exe
    if($LASTEXITCODE){throw 'Frame boundary build failed'}
    & .\analysis\frame-boundary-test.exe
    if($LASTEXITCODE){throw 'Frame boundary test failed'}
    & .\toolenv\Scripts\python.exe tests/frame-boundary-report-test.py
    if($LASTEXITCODE){throw 'Frame boundary report test failed'}
    & .\toolenv\Scripts\python.exe tests/ftrace-present-report-test.py
    if($LASTEXITCODE){throw 'Present interval report test failed'}
} finally {Pop-Location}
