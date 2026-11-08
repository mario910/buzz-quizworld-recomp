param([Parameter(Mandatory)][string]$Workspace,
      [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$workspaceRoot=(Resolve-Path -LiteralPath $Workspace).Path
$build=Join-Path $workspaceRoot 'build-vulkan-windows'
$sdk=Join-Path $workspaceRoot 'ps3recomp'
$out=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $out){throw 'Use a fresh evidence directory'}
New-Item -ItemType Directory -Path $out -Force | Out-Null
$vs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$python=Join-Path $root 'toolenv/Scripts/python.exe'
$compilerDir=& $python -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
$cc=Join-Path $compilerDir 'clang-cl.exe'
$includes=@("/I$root/port","/I$sdk/include","/I$sdk/libs/video","/I$build/_deps/ps3_vk_headers-src/include")
$ninja=Get-Content -LiteralPath (Join-Path $build 'build.ninja') -Raw
$line=[regex]::Match($ninja,'(?m)^  LINK_LIBRARIES = (.+)$').Groups[1].Value.Trim()
if(!$line){throw 'Missing prepared Ninja link dependencies'}
$libraries=$line.Split(' ',[StringSplitOptions]::RemoveEmptyEntries) | ForEach-Object {
    $local=Join-Path $build $_
    if(Test-Path -LiteralPath $local){$local}else{$_}
}
& $cc /nologo /TC /std:c17 /O2 /MD /W0 @includes /c (Join-Path $sdk 'runtime/host/host_stubs.c') "/Fo$out/stubs.obj" *> "$out/stubs-build.log"
if($LASTEXITCODE){throw 'Host stub compile failed'}
foreach($variant in 'original','fixed') {
    $backend=if($variant -eq 'original'){Join-Path $sdk 'libs/video/rsx_vulkan_backend.c'}else{Join-Path $build 'buzz_rsx_vulkan_upload.c'}
    $source="#define BUZZ_VULKAN_UPLOAD_SOURCE `"$($backend.Replace('\','/'))`"`n#include `"$($root.Replace('\','/'))/tests/vulkan-dpi-test.c`"`n"
    [IO.File]::WriteAllText("$out/$variant.c",$source,[Text.UTF8Encoding]::new($false))
    & $cc /nologo /TC /std:c17 /O2 /MD /W0 @includes /c "$out/$variant.c" "/Fo$out/$variant.obj" *> "$out/$variant-build.log"
    if($LASTEXITCODE){throw "DPI fixture compile failed: $variant"}
    & $cc /nologo /MD "$out/$variant.obj" "$out/stubs.obj" "/Fe$out/$variant.exe" /link @libraries *>> "$out/$variant-build.log"
    if($LASTEXITCODE){throw "DPI fixture link failed: $variant"}
    $p=Start-Process -FilePath "$out/$variant.exe" -WorkingDirectory $out -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput "$out/$variant.stdout.log" -RedirectStandardError "$out/$variant.stderr.log"
    if($p.ExitCode -eq 77){throw 'Thread DPI APIs unavailable; test not exercised'}
    if($variant -eq 'fixed' -and $p.ExitCode){throw "Fixed physical-pixel test failed ($($p.ExitCode))"}
    if($variant -eq 'original') {
        $text=Get-Content -LiteralPath "$out/$variant.stderr.log" -Raw
        if($text -match 'window DPI=(\d+)' -and [int]$Matches[1] -ne 96) {
            if(!$p.ExitCode -or $text -notmatch 'FAIL swapchain must equal physical client') {
                throw 'Scaled-desktop baseline did not reproduce the original mismatch'
            }
            'PASS original scaled-desktop mismatch reproduced'
        } else {'NOTE current desktop is 100%; original mismatch cannot be required here'}
    }
    Get-Content -LiteralPath "$out/$variant.stdout.log"
    Select-String -LiteralPath "$out/$variant.stderr.log" -Pattern '^window DPI=|^initial |^resized |^flexible-extent |^bilinear |^FSR1 |^borderless |^windowed-again |^FAIL ' | ForEach-Object Line
}
