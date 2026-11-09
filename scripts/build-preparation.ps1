# Developer helper; BuzzPrepare itself is a compiled program without Python.
param([string]$QtPrefix = $env:QT_ROOT_DIR, [ValidateRange(1,64)][int]$Jobs = 6)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $QtPrefix -or -not (Test-Path -LiteralPath (Join-Path $QtPrefix 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw 'Pass -QtPrefix <Qt6 MSVC x64 directory>, or set QT_ROOT_DIR.'
}
$QtPrefix = (Resolve-Path -LiteralPath $QtPrefix).Path
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = if (Test-Path -LiteralPath $vswhere) {
    & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if (-not $vsRoot) { throw 'Install Visual Studio C++ Build Tools.' }
    & (Join-Path $vsRoot 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}
$cmake = 'cmake.exe'
$ctest = 'ctest.exe'
$ninjaOption = @()
if ($vsRoot) {
    $vsCmake = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake'
    $nativeCmake = Join-Path $vsCmake 'CMake/bin/cmake.exe'
    $nativeNinja = Join-Path $vsCmake 'Ninja/ninja.exe'
    if (Test-Path -LiteralPath $nativeCmake) {
        $cmake = $nativeCmake
        $ctest = Join-Path $vsCmake 'CMake/bin/ctest.exe'
    }
    if (Test-Path -LiteralPath $nativeNinja) {
        $ninjaOption = @("-DCMAKE_MAKE_PROGRAM=$nativeNinja")
    }
}
$env:PATH = "$(Join-Path $QtPrefix 'bin');$env:PATH"
$build = Join-Path $projectRoot 'build-preparation/windows'
& $cmake -S (Join-Path $projectRoot 'preparation') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtPrefix" @ninjaOption
if ($LASTEXITCODE) { throw 'Preparation configuration failed.' }
& $cmake --build $build --parallel $Jobs
if ($LASTEXITCODE) { throw 'Preparation build failed.' }
& $ctest --test-dir $build --output-on-failure
if ($LASTEXITCODE) { throw 'Preparation tests failed.' }

$dist = Join-Path $projectRoot 'dist-preparation'
& $cmake --install $build --prefix $dist
if ($LASTEXITCODE) { throw 'Preparation installation failed.' }
$wizard = Join-Path $dist 'bin/BuzzSetup.exe'
& (Join-Path $QtPrefix 'bin/windeployqt.exe') --release --no-translations --no-compiler-runtime $wizard
if ($LASTEXITCODE) { throw 'Qt deployment failed.' }
if (-not $env:VCToolsRedistDir) { throw 'The Visual C++ redistributable files are missing from the C++ workload.' }
$crt = Join-Path $env:VCToolsRedistDir 'x64/Microsoft.VC143.CRT'
foreach ($dll in @('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
    Copy-Item -LiteralPath (Join-Path $crt $dll) -Destination (Split-Path -Parent $wizard)
}
Write-Output "Preparation ready: $wizard"
