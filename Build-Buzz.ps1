# Supported Windows build. Research variants remain in build-windows.ps1.
param([ValidateRange(1,64)][int]$Jobs = 6, [switch]$SkipGeneration)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$toolEnvironment = if($env:BUZZ_TOOL_ENV){$env:BUZZ_TOOL_ENV}else{Join-Path $root 'toolenv'}
$python = Join-Path $toolEnvironment 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Run .\Setup-Buzz.ps1 first.' }
$exe = Join-Path $root 'build-windows\BuzzQuizWorldRecomp.exe'
if (Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) {
    throw 'Close this checkout''s running game before generating/building. It will not be stopped for you.'
}
$profileOptions = & $python (Join-Path $root 'verify-game.py') --build-options
if ($LASTEXITCODE) { throw 'Game validation failed; no generation/build was started.' }
$nativeOptions = @{}
foreach($option in ($profileOptions | ConvertFrom-Json).PSObject.Properties) {
    $nativeOptions[$option.Name] = [bool]$option.Value
}
if (-not $SkipGeneration) { & (Join-Path $root 'generate.ps1') -Jobs $Jobs }
& (Join-Path $root 'build-windows.ps1') -Jobs $Jobs @nativeOptions
Write-Output 'Build ready. Start with Launch-Buzz.cmd or .\Launch-Buzz.ps1.'
