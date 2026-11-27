# Developer build/package helper; the installed application runs without PowerShell or Python.
# The release workflow runs it after build-preparation.ps1 and ships dist-studio/bin
# next to BuzzSetup, sharing its Qt libraries (see tools/package-preparation.py).
param(
    [string]$QtPrefix = $env:QT_ROOT_DIR,
    [string]$FFmpeg,
    [ValidateRange(1,64)][int]$Jobs = 6,
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $QtPrefix -or -not (Test-Path -LiteralPath (Join-Path $QtPrefix 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw 'Pass -QtPrefix <Qt6 MSVC x64 directory>, or set QT_ROOT_DIR. Qt 6.2+ (Base, Multimedia and Tools) is required.'
}
$QtPrefix = (Resolve-Path -LiteralPath $QtPrefix).Path
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = if (Test-Path -LiteralPath $vswhere) {
    & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if (-not $vsRoot) { throw 'Install Visual Studio 2022 C++ Build Tools.' }
    & (Join-Path $vsRoot 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}
$tools = if ($env:BUZZ_TOOL_ENV) { Join-Path $env:BUZZ_TOOL_ENV 'Scripts' } else { Join-Path $projectRoot 'toolenv/Scripts' }
if (Test-Path -LiteralPath (Join-Path $tools 'cmake.exe')) { $env:PATH = "$tools;$env:PATH" }
# Fall back to the CMake and Ninja that ship with Visual Studio (as build-preparation.ps1 does).
$cmake = 'cmake'
$ctest = 'ctest'
$ninjaOption = @()
if (-not (Get-Command cmake -ErrorAction SilentlyContinue) -and $vsRoot) {
    $vsCmake = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake'
    $cmake = Join-Path $vsCmake 'CMake/bin/cmake.exe'
    $ctest = Join-Path $vsCmake 'CMake/bin/ctest.exe'
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue) -and $vsRoot) {
    $vsNinja = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
    if (Test-Path -LiteralPath $vsNinja) { $ninjaOption = @("-DCMAKE_MAKE_PROGRAM=$vsNinja") }
}
$env:PATH = "$(Join-Path $QtPrefix 'bin');$env:PATH"
$build = Join-Path $projectRoot 'build-studio/windows'
$dist = Join-Path $projectRoot 'dist-studio'
& $cmake -S (Join-Path $projectRoot 'studio') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtPrefix" @ninjaOption
if ($LASTEXITCODE) { throw 'Studio configuration failed.' }
& $cmake --build $build --parallel $Jobs
if ($LASTEXITCODE) { throw 'Studio build failed.' }
if (-not $SkipTests) {
    & $ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE) { throw 'Studio tests failed.' }
}
# A fresh folder: the release package takes everything in dist-studio/bin.
if (Test-Path -LiteralPath $dist) { Remove-Item -LiteralPath $dist -Recurse -Force }
& $cmake --install $build --prefix $dist
if ($LASTEXITCODE) { throw 'Studio installation failed.' }
$studio = Join-Path $dist 'bin/BuzzQuestionStudio.exe'
# Use the Windows Media Foundation backend for the shared Windows package.
& (Join-Path $QtPrefix 'bin/windeployqt.exe') --release --no-translations --no-compiler-runtime --no-opengl-sw `
    --no-system-d3d-compiler --no-system-dxc-compiler --no-ffmpeg --exclude-plugins ffmpegmediaplugin `
    --include-plugins windowsmediaplugin $studio
if ($LASTEXITCODE) { throw 'Qt deployment failed.' }
if ($env:VCToolsRedistDir) {
    $crt = Join-Path $env:VCToolsRedistDir 'x64/Microsoft.VC143.CRT'
    foreach ($dll in @('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
        Copy-Item -LiteralPath (Join-Path $crt $dll) -Destination (Split-Path -Parent $studio)
    }
} else {
    Write-Warning 'VCToolsRedistDir is not set: the Visual C++ runtime DLLs were not copied next to Question Studio.'
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/question-studio.md') -Destination (Join-Path $dist 'README.md')
if ($FFmpeg) {
    $mediaExecutable = (Resolve-Path -LiteralPath $FFmpeg).Path
    & $mediaExecutable -version | Out-Null
    if ($LASTEXITCODE) { throw 'The specified FFmpeg executable failed.' }
    Copy-Item -LiteralPath $mediaExecutable -Destination (Join-Path $dist 'bin/ffmpeg.exe')
    Write-Output 'FFmpeg included. Include its license notices and corresponding source when distributing this package.'
}
Write-Output "Question Studio ready: $studio"
