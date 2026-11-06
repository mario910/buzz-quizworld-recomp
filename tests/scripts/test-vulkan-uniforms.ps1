param([Parameter(Mandatory=$true)][string]$Workspace,[string]$Output='analysis/vulkan-uniforms')
$ErrorActionPreference='Stop'
$Workspace=(Resolve-Path -LiteralPath $Workspace).Path
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out=[IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$vs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$cc='C:/Users/Mariusz/.clang-tool-chain/clang/win/x86_64/bin/clang-cl.exe'
$names=@('BUZZ_VK_UNIFORM_REUSE','BUZZ_VK_UNIFORM_SETS','BUZZ_VK_DESCRIPTOR_BATCH','BUZZ_VK_DESCRIPTOR_WRITES')
$previous=@{}
foreach($name in $names){$previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try {
    $env:BUZZ_VK_UNIFORM_REUSE='1';$env:BUZZ_VK_UNIFORM_SETS='1'
    foreach($unit in @('reuse','sets')) {
        & $cc /nologo /TC /std:c17 /O2 /MD /UNDEBUG /W0 "/I$root/port" "/I$Workspace/build-vulkan-windows/_deps/ps3_vk_headers-src/include" "$root/tests/vulkan-uniform-$unit-test.c" "/Fo$out/unit-$unit.obj" "/Fe$out/unit-$unit.exe" *> "$out/unit-$unit-build.log"
        if($LASTEXITCODE){throw "Uniform $unit fixture compilation failed"}
        & "$out/unit-$unit.exe" *> "$out/unit-$unit.log"
        if($LASTEXITCODE){throw "Uniform $unit fixture failed"}
    }
    foreach($batch in @('0','1')) {
        $env:BUZZ_VK_DESCRIPTOR_BATCH=$batch
        foreach($writes in @('0','1')) {
            $env:BUZZ_VK_DESCRIPTOR_WRITES=$writes
            & "$PSScriptRoot/test-vulkan-upload.ps1" -Workspace $Workspace -Output "$out/gpu-$batch-$writes"
        }
    }
} finally {
    foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')}
}
Write-Output 'PASS immutable uniform/descriptor sharing: payload/lifetime units and four real GPU configurations'
