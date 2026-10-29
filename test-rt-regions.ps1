$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $toolPython=Join-Path $(if($env:BUZZ_TOOL_ENV){$env:BUZZ_TOOL_ENV}else{Join-Path $PSScriptRoot 'toolenv'}) 'Scripts/python.exe'
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $compiler=& $toolPython -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $compiler 'clang-cl.exe') /nologo /O2 /UNDEBUG tests/rt-region-test.c /Foanalysis/rt-region-test.obj /Feanalysis/rt-region-test.exe
    if($LASTEXITCODE){throw 'RT region test compilation failed'}
    & .\analysis\rt-region-test.exe
    if($LASTEXITCODE){throw 'RT region tests failed'}
} finally {Pop-Location}
