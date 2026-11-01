$ErrorActionPreference='Stop'
$ledgerRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $ledgerRoot
try {
    & "$PSScriptRoot/assert-geometry-build-idle.ps1"
    $ledgerOut='analysis/native-geometry/phase3/phase3a-v2'
    New-Item -ItemType Directory -Path $ledgerOut -Force | Out-Null
    $ledgerVs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $ledgerVs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $ledgerCc=& ./toolenv/Scripts/python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir;print(get_platform_binary_dir())'
    & (Join-Path $ledgerCc 'clang-cl.exe') /nologo /std:c++17 /MD /O2 /UNDEBUG /W0 tests/geometry-ledger-test.cpp "/Fo$ledgerOut/ledger-test.obj" "/Fe$ledgerOut/ledger-test.exe"
    if($LASTEXITCODE){throw 'Ledger fixture build failed'}
    & "./$ledgerOut/ledger-test.exe" *> "$ledgerOut/ledger-test.log"
    if($LASTEXITCODE){throw 'Ledger concurrency fixture failed'}
    Get-Content "$ledgerOut/ledger-test.log"
} finally {Pop-Location}
