# Supported Windows build. Research variants remain in build-windows.ps1.
param([ValidateRange(1,64)][int]$Jobs = 6, [switch]$SkipGeneration)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$python = Join-Path $root 'toolenv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Run .\Setup-Buzz.ps1 first.' }
$exe = Join-Path $root 'build-windows\BuzzQuizWorldRecomp.exe'
if (Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) {
    throw 'Close this checkout''s running game before generating/building. It will not be stopped for you.'
}
& $python (Join-Path $root 'verify-game.py')
if ($LASTEXITCODE) { throw 'Game validation failed; no generation/build was started.' }
if (-not $SkipGeneration) { & (Join-Path $root 'generate.ps1') -Jobs $Jobs }
& (Join-Path $root 'build-windows.ps1') -Jobs $Jobs -NativeCompletion -NativeImage40 -NativeImage39 -NativeImage36 -ResultGuardFP -StageF -NativeImage7 -MStreamModules
Write-Output 'Build ready. Start with Launch-Buzz.cmd or .\Launch-Buzz.ps1.'
