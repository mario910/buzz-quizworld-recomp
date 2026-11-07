param([string]$CompilerDirectory='', [string]$Python='python')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$vs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
if(!$vs){throw 'Visual Studio C++ Build Tools were not found'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
if(!$CompilerDirectory){
 $CompilerDirectory=& $Python -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
 if($LASTEXITCODE){throw 'Pass CompilerDirectory or install clang-tool-chain'}
}
$cc=Join-Path $CompilerDirectory 'clang-cl.exe'
$out=Join-Path $root 'analysis/publication-watch-test'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$flags=@('/nologo','/O2','/MD','/W0','/UNDEBUG',"/I$root/port","/I$root/ps3recomp/include")
& $cc @flags /TC /c "$root/tests/publication-watch-c.c" "/Fo$out/c.obj"
if($LASTEXITCODE){throw 'C publication observer compilation failed'}
& $cc @flags /std:c++17 /c "$root/tests/publication-watch-test.cpp" "/Fo$out/test.obj"
if($LASTEXITCODE){throw 'C++ publication observer compilation failed'}
& $cc /nologo /MD "$out/test.obj" "$out/c.obj" "/Fe$out/test.exe"
if($LASTEXITCODE){throw 'C/C++ publication observer link failed'}
& "$out/test.exe"
if($LASTEXITCODE){throw 'C/C++ publication observers do not share registrations'}
