# Developer build/package helper; the installed application runs without PowerShell or Python.
param(
    [string]$QtPrefix = $env:QT_ROOT_DIR,
    [string]$FFmpeg,
    [ValidateRange(1,64)][int]$Jobs = 6,
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $QtPrefix -or -not (Test-Path -LiteralPath (Join-Path $QtPrefix 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw 'Pass -QtPrefix <Qt6 MSVC x64 directory>, or set QT_ROOT_DIR. Qt 6.2+ with Declarative, Tools and SVG is required.'
}
$QtPrefix = (Resolve-Path -LiteralPath $QtPrefix).Path
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 C++ Build Tools.' }
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsRoot) { throw 'Visual Studio C++ tools were not found.' }
    & (Join-Path $vsRoot 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}
$tools = if ($env:BUZZ_TOOL_ENV) { Join-Path $env:BUZZ_TOOL_ENV 'Scripts' } else { Join-Path $projectRoot 'toolenv/Scripts' }
if (Test-Path -LiteralPath (Join-Path $tools 'cmake.exe')) { $env:PATH = "$tools;$env:PATH" }
$env:PATH = "$(Join-Path $QtPrefix 'bin');$env:PATH"
$build = Join-Path $projectRoot 'build-studio/windows'
$dist = Join-Path $projectRoot 'dist-studio'
cmake -S (Join-Path $projectRoot 'studio') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtPrefix"
if ($LASTEXITCODE) { throw 'Studio configuration failed.' }
cmake --build $build --parallel $Jobs
if ($LASTEXITCODE) { throw 'Studio build failed.' }
if (-not $SkipTests) {
    ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE) { throw 'Studio tests failed.' }
}
cmake --install $build --prefix $dist
if ($LASTEXITCODE) { throw 'Studio installation failed.' }
& (Join-Path $QtPrefix 'bin/windeployqt.exe') --release --qmldir (Join-Path $projectRoot 'studio/ui') (Join-Path $dist 'bin/BuzzQuestionStudio.exe')
if ($LASTEXITCODE) { throw 'Qt deployment failed.' }
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/question-studio.md') -Destination (Join-Path $dist 'README.md')
if ($FFmpeg) {
    $mediaExecutable = (Resolve-Path -LiteralPath $FFmpeg).Path
    & $mediaExecutable -version | Out-Null
    if ($LASTEXITCODE) { throw 'The specified FFmpeg executable failed.' }
    Copy-Item -LiteralPath $mediaExecutable -Destination (Join-Path $dist 'bin/ffmpeg.exe')
    Write-Output 'FFmpeg included. Include its license notices and corresponding source when distributing this package.'
}
Write-Output "Question Studio ready: $(Join-Path $dist 'bin/BuzzQuestionStudio.exe')"
