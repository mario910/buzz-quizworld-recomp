param([Parameter(Mandatory=$true)][string]$Workspace,[string]$Output='analysis/vulkan-upload')
$ErrorActionPreference='Stop'
$Workspace=(Resolve-Path -LiteralPath $Workspace).Path
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out=[IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path $out | Out-Null
if(!$env:VSCMD_VER) {
    $vs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$build=Join-Path $Workspace 'build-vulkan-windows'
$sdk=Join-Path $Workspace 'ps3recomp'
# Give public entry points internal linkage so this core-device test need not
# link the game, window UI or shader compiler. All tested function bodies are
# exactly those in the CMake-generated production adapter.
$header=Get-Content "$sdk/libs/video/rsx_vulkan_backend.h" -Raw
$prototypes=[regex]::Matches($header,'(?m)^(?:int|void|u32|u64)\s+rsx_vulkan_\w+\s*\([^;]*?\);')
if($prototypes.Count -lt 10){throw 'Vulkan header no longer matches fixture'}
$private="#define PS3RECOMP_RSX_VULKAN_BACKEND_H`n#include `"rsx_commands.h`"`n"
foreach($prototype in $prototypes){$private+='static '+$prototype.Value+"`n"}
$private+="static void rsx_vulkan_backend_redisplay(void);`n"
$private+=Get-Content "$build/buzz_rsx_vulkan_upload.c" -Raw
Set-Content "$out/vulkan-upload-private.c" $private -Encoding utf8
$cc='C:/Users/Mariusz/.clang-tool-chain/clang/win/x86_64/bin/clang-cl.exe'
& $cc -fuse-ld=lld /nologo /TC /std:c17 /O2 /MD /Gy /Gw /UNDEBUG /W0 /DBUZZ_VK_BATCH_UPLOAD_DEFAULT=0 "/I$out" "/I$root/port" "/I$sdk/include" "/I$sdk/libs/video" "/I$build/_deps/ps3_vk_headers-src/include" "$root/tests/vulkan-upload-test.c" "/Fo$out/test.obj" "/Fe$out/test.exe" /link /OPT:REF *> "$out/build.log"
if($LASTEXITCODE){throw "Fixture build failed: $out/build.log"}
foreach($mode in @('0','1')) {
    $env:BUZZ_VK_BATCH_UPLOAD=$mode
    & "$out/test.exe" *> "$out/mode-$mode.log"
    if($LASTEXITCODE){throw "GPU fixture failed (mode $mode): $out/mode-$mode.log"}
}
$drawSource="#define BUZZ_VULKAN_UPLOAD_SOURCE `"$($build.Replace('\','/'))/buzz_rsx_vulkan_upload.c`"`n#include `"$($root.Replace('\','/'))/tests/vulkan/vulkan-batch-test.c`"`n"
Set-Content "$out/draw.c" $drawSource -Encoding utf8
$includes=@("/I$root/port","/I$sdk/include","/I$sdk/libs/video","/I$build/_deps/ps3_vk_headers-src/include")
& $cc /nologo /TC /std:c17 /O2 /MD /W0 /DBUZZ_VK_BATCH_UPLOAD_DEFAULT=0 @includes /c "$out/draw.c" "/Fo$out/draw.obj" *> "$out/draw-build.log"
if($LASTEXITCODE){throw 'Draw fixture compilation failed'}
& $cc /nologo /TC /std:c17 /O2 /MD /W0 @includes /c "$sdk/runtime/host/host_stubs.c" "/Fo$out/stubs.obj" *>> "$out/draw-build.log"
if($LASTEXITCODE){throw 'Host stub compilation failed'}
# Reuse the prepared target's exact dependency libraries (including translator).
$ninja=Get-Content "$build/build.ninja" -Raw
$libraryLine=[regex]::Match($ninja,'(?m)^  LINK_LIBRARIES = (.+)$').Groups[1].Value.Trim()
if(!$libraryLine){throw 'Prepared Ninja link dependencies unavailable'}
$libraries=$libraryLine.Split(' ',[StringSplitOptions]::RemoveEmptyEntries) | ForEach-Object {
    $local=Join-Path $build $_
    if(Test-Path -LiteralPath $local){$local}else{$_}
}
& $cc /nologo /MD "$out/draw.obj" "$out/stubs.obj" "/Fe$out/draw.exe" /link @libraries *>> "$out/draw-build.log"
if($LASTEXITCODE){throw "Draw fixture link failed: $out/draw-build.log"}
$env:PS3RECOMP_VK_DRAW_RING='1';$env:PS3RECOMP_VK_DRAW_PASSES='1';$env:PS3RECOMP_VK_DRAW_CLEARS='1'
$env:PS3RECOMP_VK_DRAW_ASYNC='1';$env:PS3RECOMP_VK_GPU_TIMESTAMPS='0';$env:PS3RECOMP_VK_TEXTURE_REUSE='1'
foreach($mode in @('0','1')) {
    $env:BUZZ_VK_BATCH_UPLOAD=$mode
    foreach($batch in @(1,3,512)) {
        & "$out/draw.exe" $batch *> "$out/draw-$mode-$batch.log"
        if($LASTEXITCODE){throw "Draw fixture failed (mode $mode, batch $batch)"}
    }
}
Write-Output 'PASS: original and batched uploads, six draw/lifetime/texture-reuse fixtures'
