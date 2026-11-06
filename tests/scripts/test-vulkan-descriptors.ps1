param([Parameter(Mandatory=$true)][string]$Workspace,[string]$Output='analysis/vulkan-descriptors')
$ErrorActionPreference='Stop'
$Workspace=(Resolve-Path -LiteralPath $Workspace).Path
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out=[IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$vs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$cc='C:/Users/Mariusz/.clang-tool-chain/clang/win/x86_64/bin/clang-cl.exe'
$previous=$env:BUZZ_VK_DESCRIPTOR_BATCH
$previousWrites=$env:BUZZ_VK_DESCRIPTOR_WRITES
try {
    $env:BUZZ_VK_DESCRIPTOR_BATCH='1'
    $env:BUZZ_VK_DESCRIPTOR_WRITES='1'
    foreach($unit in @('batch','writes')) {
        & $cc /nologo /TC /std:c17 /O2 /MD /UNDEBUG /W0 "/I$root/port" "/I$Workspace/build-vulkan-windows/_deps/ps3_vk_headers-src/include" "$root/tests/vulkan-descriptor-$unit-test.c" "/Fo$out/unit-$unit.obj" "/Fe$out/unit-$unit.exe" *> "$out/unit-$unit-build.log"
        if($LASTEXITCODE){throw "Descriptor $unit fixture compilation failed"}
        & "$out/unit-$unit.exe" *> "$out/unit-$unit.log"
        if($LASTEXITCODE){throw "Descriptor $unit fixture failed"}
    }
    foreach($mode in @('0','1')) {
        $env:BUZZ_VK_DESCRIPTOR_BATCH=$mode
        foreach($writes in @('0','1')) {
            $env:BUZZ_VK_DESCRIPTOR_WRITES=$writes
            & "$PSScriptRoot/test-vulkan-upload.ps1" -Workspace $Workspace -Output "$out/gpu-$mode-$writes"
        }
    }
} finally {
    if($null -eq $previous){Remove-Item Env:BUZZ_VK_DESCRIPTOR_BATCH -ErrorAction SilentlyContinue}
    else{$env:BUZZ_VK_DESCRIPTOR_BATCH=$previous}
    if($null -eq $previousWrites){Remove-Item Env:BUZZ_VK_DESCRIPTOR_WRITES -ErrorAction SilentlyContinue}
    else{$env:BUZZ_VK_DESCRIPTOR_WRITES=$previousWrites}
}
Write-Output 'PASS descriptor recycling: lifetime/failure unit test and OFF/ON real GPU suites'
