# Supported Windows build. Research variants remain in scripts/build-windows.ps1.
# Vulkan (build-vulkan-windows) is the default renderer; D3D12 (build-windows)
# is built beside it as the fallback. -Renderer picks one; -VulkanExperimental
# is the older spelling of -Renderer Vulkan.
param([ValidateRange(1,64)][int]$Jobs = 6, [switch]$SkipGeneration, [switch]$SkipLauncher,
    [ValidateSet('Both','Vulkan','D3D12')][string]$Renderer = 'Both', [switch]$VulkanExperimental)
$projectRoot = (Split-Path -Parent $PSScriptRoot)

$ErrorActionPreference = 'Stop'
$root = $projectRoot
$toolEnvironment = if($env:BUZZ_TOOL_ENV){$env:BUZZ_TOOL_ENV}else{Join-Path $root 'toolenv'}
$python = Join-Path $toolEnvironment 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Run .\scripts\Setup-Buzz.ps1 first.' }
if($VulkanExperimental){$Renderer='Vulkan'}
$variants=@(); if($Renderer -ne 'D3D12'){$variants+=$true}; if($Renderer -ne 'Vulkan'){$variants+=$false}
foreach($vulkan in $variants) {
    $exe = Join-Path $root ($(if($vulkan){'build-vulkan-windows'}else{'build-windows'})+'\BuzzQuizWorldRecomp.exe')
    if (Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) {
        throw 'Close this checkout''s running game before generating/building. It will not be stopped for you.'
    }
}
$profileOptions = & $python (Join-Path $root 'tools/verify-game.py') --build-options
if ($LASTEXITCODE) { throw 'Game validation failed; no generation/build was started.' }
$nativeOptions = @{}
foreach($option in ($profileOptions | ConvertFrom-Json).PSObject.Properties) {
    $nativeOptions[$option.Name] = [bool]$option.Value
}
if (-not $SkipGeneration) { & (Join-Path $root 'scripts/generate.ps1') -Jobs $Jobs }
foreach($vulkan in $variants) {
    & (Join-Path $root 'scripts/build-windows.ps1') -Jobs $Jobs -VulkanExperimental:$vulkan @nativeOptions
}
if(-not $SkipLauncher){& (Join-Path $root 'scripts/build-launcher.ps1') -Jobs $Jobs}
Write-Output ("Build ready ({0}). Start with Launch-Buzz.cmd; the launcher prefers Vulkan unless PC settings select D3D12." -f $Renderer)
