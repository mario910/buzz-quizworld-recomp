# Bridge owned by the GUI: keep the original CLI's performance defaults.
param(
    [Parameter(Mandatory=$true)][string]$InputConfig,
    [Parameter(Mandatory=$true)][string]$PathsConfig,
    [switch]$Background
)
$ErrorActionPreference='Stop'
# Windows PowerShell 5.1 otherwise emits the console code page into the GUI pipe.
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
& (Join-Path $PSScriptRoot '..\run-windows.ps1') -Seconds 0 -MenuBootstrap -InputConfig $InputConfig -PathsConfig $PathsConfig -Background:$Background
