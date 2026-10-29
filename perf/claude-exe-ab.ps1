# A/B two saved EXEs by copying each into build-windows before its run.
#   claude-exe-ab.ps1 -A <exe> -B <exe> -Order A,B,A,B -Mode jobcost|fps -Tag name
param([string]$A, [string]$B, [string[]]$Order = @('A','B','A','B'),
      [ValidateSet('jobcost','fps')][string]$Mode = 'jobcost', [string]$Tag = 'exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
$target = Join-Path $root 'build-windows\BuzzQuizWorldRecomp.exe'
$n = 0
foreach ($which in $Order) {
    $n++
    $source = if ($which -eq 'A') { $A } else { $B }
    Copy-Item -LiteralPath $source -Destination $target -Force
    "=== run $n variant $which ($((Get-FileHash $target).Hash.Substring(0,12)))"
    if ($Mode -eq 'jobcost') { & (Join-Path $PSScriptRoot 'claude-jobcost.ps1') -Variants @("$which=") }
    else { & (Join-Path $PSScriptRoot 'claude-menu-variants.ps1') -Tag "$Tag$n" -Start 100 -Variants @("$which=") }
}
