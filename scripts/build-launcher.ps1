# Developer-only build helper. The resulting launcher does not run this script.
param([ValidateRange(1,64)][int]$Jobs=6)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if(-not(Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if(-not(Test-Path -LiteralPath $vswhere)){throw 'Install Visual Studio C++ Build Tools, or use CMake from a configured developer shell.'}
    $vsRoot=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(-not $vsRoot){throw 'Visual Studio C++ Build Tools were not found.'}
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}
$tools=if($env:BUZZ_TOOL_ENV){Join-Path $env:BUZZ_TOOL_ENV 'Scripts'}else{Join-Path $projectRoot 'toolenv\Scripts'}
if(Test-Path -LiteralPath (Join-Path $tools 'cmake.exe')){$env:PATH="$tools;$env:PATH"}
if(-not(Get-Command cmake -ErrorAction SilentlyContinue)){throw 'Install CMake 3.31+ and Ninja, or run the developer Setup-Buzz tool first.'}
& cmake -S (Join-Path $projectRoot 'launcher/native') -B (Join-Path $projectRoot 'build-launcher') -G Ninja -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE){throw 'Native launcher configuration failed.'}
& cmake --build (Join-Path $projectRoot 'build-launcher') --parallel $Jobs
if($LASTEXITCODE){throw 'Native launcher build failed.'}
Write-Output 'Native launcher ready: build-launcher\BuzzLauncher.exe. Play via Launch-Buzz.cmd; no Python/PowerShell subprocess.'
