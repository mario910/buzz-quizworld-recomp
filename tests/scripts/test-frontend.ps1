$projectRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
Push-Location $projectRoot
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    & $compiler /nologo /O2 /UNDEBUG /std:c++17 /Iport/recompiled /DPPU_NO_INLINE_LOADS tests/frontend-policy-test.cpp port/frontend_policy.cpp /Foanalysis/ /Feanalysis/frontend-policy-test.exe
    if($LASTEXITCODE){throw 'frontend policy build failed'}
    & .\analysis\frontend-policy-test.exe
    if($LASTEXITCODE){throw 'frontend policy failed'}
    & $compiler /nologo /O2 /UNDEBUG /std:c++17 /Iport/recompiled tests/boot-hooks-test.cpp port/boot_trace.cpp /Foanalysis/ /Feanalysis/boot-hooks-test.exe
    if($LASTEXITCODE){throw 'boot hooks build failed'}
    & .\analysis\boot-hooks-test.exe
    if($LASTEXITCODE){throw 'boot hooks failed'}
} finally {Pop-Location}
