# Installs the Real-ESRGAN texture upscaler for automatic texture upscaling
# (texture_auto_upscale) into runtime\textures\upscaler. Downloads the pinned
# release (45 MB, GitHub) once, verified by SHA-256; -Archive installs from a
# local copy of that release instead. Preparation can do the same: tick the
# upscaler option in Prepare-Buzz.
param([string]$Runtime = '', [string]$Archive = '')
$projectRoot = (Split-Path -Parent $PSScriptRoot)

$ErrorActionPreference = 'Stop'
$toolEnvironment = if($env:BUZZ_TOOL_ENV){$env:BUZZ_TOOL_ENV}else{Join-Path $projectRoot 'toolenv'}
$python = Join-Path $toolEnvironment 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Run .\scripts\Setup-Buzz.ps1 first.' }
$arguments = @('-m', 'launcher', '--install-upscaler')
if ($Runtime) { $arguments += @('--runtime', (Resolve-Path -LiteralPath $Runtime).Path) }
if ($Archive) { $arguments += @('--archive', (Resolve-Path -LiteralPath $Archive).Path) }
Push-Location $projectRoot
try { & $python @arguments; if ($LASTEXITCODE) { throw 'Upscaler install failed.' } }
finally { Pop-Location }
