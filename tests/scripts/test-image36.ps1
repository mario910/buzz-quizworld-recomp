# Standalone leaf correctness tests; no generated/game-derived data is tracked.
param([string[]]$Dumps = @())
$projectRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference='Stop'
Set-Location -LiteralPath $projectRoot
$vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$toolPython=Join-Path $(if($env:BUZZ_TOOL_ENV){$env:BUZZ_TOOL_ENV}else{Join-Path $projectRoot 'toolenv'}) 'Scripts\python.exe'
$compiler=& $toolPython -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
$cc=Join-Path $compiler 'clang-cl.exe'
& $toolPython tools/codegen/generate-image36-native.py
if($LASTEXITCODE) { throw 'Generator failed' }
& $toolPython tests\image36-generator-test.py
if($LASTEXITCODE) { throw 'Generator checks failed' }
$flags=@('/nologo','/O2','/D_CRT_SECURE_NO_WARNINGS','/DSPU_LS_DEBUG_HOOKS=0','/clang:-mavx2','/clang:-ffp-contract=off','/Ianalysis\native-image36','/Ips3recomp\runtime\spu','/Ips3recomp\include')
$units=@(
    @{Source='analysis\native-image36\reference_06d8.c';Object='analysis\image36-reference-test.obj';Extra=@()},
    @{Source='analysis\native-image36\oracle_06d8.c';Object='analysis\image36-oracle-test.obj';Extra=@()},
    @{Source='analysis\native-image36\kernel_06d8.c';Object='analysis\image36-native-test.obj';Extra=@('/clang:-mfma','/clang:-frounding-math','/DSPU_NATIVE_RTZ=1','/DSPU_RTZ_RESULT_GUARD=1')},
    @{Source='tests\image36-native-test.c';Object='analysis\image36-fixture-test.obj';Extra=@()},
    @{Source='port\native_image36.cpp';Object='analysis\image36-adapter-test.obj';Extra=@('/EHsc','/std:c++17')}
)
foreach($unit in $units) {
    $extra=$unit.Extra
    & $cc @flags @extra /c "/Fo$($unit.Object)" $unit.Source
    if($LASTEXITCODE) { throw "Compile failed: $($unit.Source)" }
}
$objects=@($units | ForEach-Object { $_.Object })
& $cc /nologo /Fe:analysis\image36-test.exe @objects
if($LASTEXITCODE) { throw 'Link failed' }
& .\analysis\image36-test.exe @Dumps
if($LASTEXITCODE) { throw 'Full-state test failed' }
