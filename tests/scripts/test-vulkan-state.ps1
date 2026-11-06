param([Parameter(Mandatory=$true)][string]$Workspace,[string]$Output='analysis/vulkan-state',[switch]$UnitOnly)
$ErrorActionPreference='Stop'
$Workspace=(Resolve-Path -LiteralPath $Workspace).Path
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out=[IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path $out | Out-Null
if(!$UnitOnly -and !(Get-Content "$Workspace/build-vulkan-windows/buzz_rsx_vulkan_upload.c" -Raw).Contains('#include "vulkan_draw_state.h"')) {
    throw 'Configure and build this workspace with -DBUZZ_VK_DRAW_STATE=ON before running GPU state tests.'
}
if(!$env:VSCMD_VER) {
    $vs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$cc='C:/Users/Mariusz/.clang-tool-chain/clang/win/x86_64/bin/clang-cl.exe'
$previous=[Environment]::GetEnvironmentVariable('BUZZ_VK_DRAW_STATE','Process')
try {
    $env:BUZZ_VK_DRAW_STATE='1'
    & $cc /nologo /TC /std:c17 /O2 /MD /UNDEBUG /W0 "/I$root/port" "/I$Workspace/build-vulkan-windows/_deps/ps3_vk_headers-src/include" "$root/tests/vulkan-draw-state-test.c" "/Fo$out/unit.obj" "/Fe$out/unit.exe" *> "$out/unit-build.log"
    if($LASTEXITCODE){throw 'Draw-state unit compilation failed'}
    & "$out/unit.exe" *> "$out/unit.log"
    if($LASTEXITCODE){throw 'Draw-state unit failed'}
    if(!$UnitOnly){& "$PSScriptRoot/test-vulkan-uniforms.ps1" -Workspace $Workspace -Output "$out/gpu"}
} finally {
    [Environment]::SetEnvironmentVariable('BUZZ_VK_DRAW_STATE',$previous,'Process')
}
Write-Output 'PASS draw-state checks'
