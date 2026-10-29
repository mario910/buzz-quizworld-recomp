# Supported Windows build. Research variants remain in scripts/build-windows.ps1.
param([ValidateRange(1,64)][int]$Jobs = 6, [switch]$SkipGeneration)
$projectRoot = (Split-Path -Parent $PSScriptRoot)

$ErrorActionPreference = 'Stop'
$root = $projectRoot
$toolEnvironment = if($env:BUZZ_TOOL_ENV){$env:BUZZ_TOOL_ENV}else{Join-Path $root 'toolenv'}
$python = Join-Path $toolEnvironment 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Run .\scripts\Setup-Buzz.ps1 first.' }
$exe = Join-Path $root 'build-windows\BuzzQuizWorldRecomp.exe'
if (Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) {
    throw 'Close this checkout''s running game before generating/building. It will not be stopped for you.'
}
$profileOptions = & $python (Join-Path $root 'tools/verify-game.py') --build-options
if ($LASTEXITCODE) { throw 'Game validation failed; no generation/build was started.' }
$nativeOptions = @{}
foreach($option in ($profileOptions | ConvertFrom-Json).PSObject.Properties) {
    $nativeOptions[$option.Name] = [bool]$option.Value
}
if (-not $SkipGeneration) { & (Join-Path $root 'scripts/generate.ps1') -Jobs $Jobs }
& (Join-Path $root 'scripts/build-windows.ps1') -Jobs $Jobs @nativeOptions
Write-Output 'Build ready. Start with Launch-Buzz.cmd or .\scripts\Launch-Buzz.ps1.'
