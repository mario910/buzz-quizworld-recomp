# Synthetic addresses only: exercise the exact bound expression used in scripts/generate.ps1.
$ErrorActionPreference='Stop'
$scriptText=Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\scripts\generate.ps1') -Raw
$boundLine=($scriptText -split "`n" | Where-Object { $_ -match '^\$codeEnd = ' })
if(@($boundLine).Count -ne 1){throw 'Expected one generation bound expression'}
$stubAddresses=@([uint32]0x12340,[uint32]0x34560)
Invoke-Expression $boundLine
if($codeEnd -ne '0x34580'){throw "Incorrect import bound: $codeEnd"}
'PASS: generation code bound formats correctly after Measure-Object on this PowerShell.'
