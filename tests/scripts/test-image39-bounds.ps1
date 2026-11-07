param(
    [Parameter(Mandatory=$true)][string]$ReferenceSource,
    [string[]]$Corpus=@(),
    [string]$CompilerDirectory='',
    [string]$Python='python',
    [switch]$Bench
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$vsRoot=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
if(!$vsRoot){throw 'Visual Studio C++ Build Tools were not found.'}
& (Join-Path $vsRoot 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
if(!$CompilerDirectory){
    $CompilerDirectory=& $Python -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    if($LASTEXITCODE){throw 'Pass CompilerDirectory or install clang-tool-chain for this Python.'}
}
$cc=Join-Path $CompilerDirectory 'clang-cl.exe'
$out=Join-Path $root 'analysis/image39-bounds-tests'
New-Item -ItemType Directory -Path $out -Force | Out-Null
& $Python "$root/tools/codegen/image39_bounds.py" --reference $ReferenceSource --output "$out/original.c"
if($LASTEXITCODE){throw 'Pinned original bounds extraction failed.'}
$flags=@('/nologo','/O2','/MD','/UNDEBUG','/D_CRT_SECURE_NO_WARNINGS','/DSPU_LS_DEBUG_HOOKS=0','/arch:AVX2','/Gy',
    '/clang:-frounding-math','/clang:-ffp-contract=off',"/I$root/port","/I$root/ps3recomp/include",
    "/I$root/ps3recomp/runtime/spu","/I$root/ps3recomp/runtime/platform")
& $cc @flags /TC /c "$out/original.c" "/Fo$out/original.obj"
if($LASTEXITCODE){throw 'Original bounds test compilation failed.'}
& $cc @flags /std:c++17 /c "$root/tests/image39-bounds-test.cpp" "/Fo$out/test.obj"
if($LASTEXITCODE){throw 'Bounds fixture compilation failed.'}
& $cc @flags /std:c++17 /c "$root/port/native_image39_bounds.cpp" "/Fo$out/native.obj"
if($LASTEXITCODE){throw 'Production bounds driver compilation failed.'}
& $cc /nologo /MD "$out/test.obj" "$out/native.obj" "$out/original.obj" "/Fe$out/test.exe" /link /OPT:REF
if($LASTEXITCODE){throw 'Bounds test link failed.'}
$cases=@()
foreach($directory in $Corpus){
    $files=@(Get-ChildItem -LiteralPath $directory -Filter 'stagef_k3_*.bin' | Sort-Object Name | Select-Object -ExpandProperty FullName)
    if(!$files.Count){throw "No Image 39 Stage F captures in $directory"}
    $cases+=$files
}
if($Bench){$cases=@('--bench')+$cases}
& "$out/test.exe" @cases
if($LASTEXITCODE){throw 'Production/original bounds comparison failed.'}
