# Test the real bridge inside Windows PowerShell; process creation alone is mocked.
param([string]$InputConfig,[string]$PathsConfig)
$ErrorActionPreference='Stop'
$expected=Get-Content -LiteralPath $PathsConfig -Raw -Encoding UTF8 | ConvertFrom-Json
$global:buzzBridgeExpected=$expected
$global:buzzBridgeStarted=$false
function Start-Process {
    param($FilePath,$ArgumentList,$WorkingDirectory,$WindowStyle,$RedirectStandardOutput,$RedirectStandardError,[switch]$PassThru)
    if($env:PS3_DISC_ROOT -cne $global:buzzBridgeExpected.game_root){throw 'Unicode VFS path mismatch'}
    if($ArgumentList -cne ('"'+$global:buzzBridgeExpected.eboot_elf+'"')){throw 'Literal ELF argument mismatch'}
    if($WindowStyle -ne 'Hidden' -or $env:RSX_WINDOW_BACKGROUND -ne '1'){throw 'Bridge changed background launch policy'}
    $global:buzzBridgeStarted=$true
    [pscustomobject]@{Id=424242}
}
& (Join-Path $PSScriptRoot '..\launcher\start-game.ps1') -InputConfig $InputConfig -PathsConfig $PathsConfig -Background
if(-not $global:buzzBridgeStarted){throw 'Bridge did not reach mocked creation'}
Write-Output 'PASS: Windows PowerShell bridge, UTF8 paths, literal args, background policy; no game launched'
