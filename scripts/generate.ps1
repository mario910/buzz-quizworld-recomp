param([ValidateRange(1,64)][int]$Jobs = 6)
$projectRoot = (Split-Path -Parent $PSScriptRoot)

$ErrorActionPreference = 'Stop'
$experimentRoot = $projectRoot
$sdk = Join-Path $experimentRoot 'ps3recomp'
$toolEnvironment = if($env:BUZZ_TOOL_ENV){$env:BUZZ_TOOL_ENV}else{Join-Path $experimentRoot 'toolenv'}
$python = Join-Path $toolEnvironment 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Run .\scripts\Setup-Buzz.ps1 first.' }
if (-not (Test-Path -LiteralPath (Join-Path $sdk 'tools\ppu_loader.py'))) { throw 'Initialize the ps3recomp submodule first.' }
& $python (Join-Path $experimentRoot 'tools/verify-game.py') --elf-only
if ($LASTEXITCODE) { throw 'Unsupported generation input; no generated files were written.' }
. (Join-Path $experimentRoot 'scripts/buzz-paths.ps1')
$elf = (Resolve-Path -LiteralPath (Get-BuzzPaths).eboot_elf).Path
$identity = Get-BuzzFileSha256 -Path $elf
if($identity -in @('6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8',
                   'fb78b9e6d30ba5eaa76b7e31bc3afa838736dcb5cde40fc602a628f2305ebb5d')) {
    & $python (Join-Path $experimentRoot 'tools\codegen\build-disc100-reference.py') --elf $elf --in-place --generate-only --jobs $Jobs
    if($LASTEXITCODE){throw 'Original-disc 1.00 generation failed'}
    return
}
if(Test-Path -LiteralPath (Join-Path $experimentRoot 'port\spu\spu_0006_at_007FF780')) {
    throw 'This workspace contains 1.00 translations. Use a separate preparation session; existing translations are preserved.'
}
$analysis = Join-Path $experimentRoot 'analysis'
$recompiled = Join-Path $experimentRoot 'port\recompiled'
New-Item -ItemType Directory -Force -Path $analysis | Out-Null
function Run-Python {
    & $python @args
    if ($LASTEXITCODE) { throw "Generation failed: $LASTEXITCODE" }
}
Run-Python (Join-Path $sdk 'tools\ppu_loader.py') $elf -o $analysis
Run-Python (Join-Path $sdk 'tools\find_functions.py') $elf --seed-json (Join-Path $analysis 'EBOOT.functions.json') --output (Join-Path $analysis 'functions-full.json')
# The retail ELF has no section headers. The end of its firmware import
# trampolines bounds executable code before the read-only data in PT_LOAD.
$imports = Get-Content -LiteralPath (Join-Path $analysis 'EBOOT.imports.json') -Raw | ConvertFrom-Json
$stubAddresses = $imports | ForEach-Object { [Convert]::ToUInt32($_.stub.Substring(2),16) }
if (-not $stubAddresses) { throw 'No firmware import trampolines were found in the supported ELF.' }
# Measure-Object returns Double on supported PowerShell versions. Hex format
# requires an integer even when all measured inputs are UInt32.
$codeEnd = '0x{0:x}' -f ([uint64](($stubAddresses | Measure-Object -Maximum).Maximum + 32))
Run-Python (Join-Path $sdk 'tools\ppu_lifter.py') $elf --functions (Join-Path $analysis 'functions-full.json') --hle-stubs (Join-Path $analysis 'EBOOT.imports.json') --output $recompiled --jobs $Jobs --chunks 96 --code-end $codeEnd
$spuImages = Join-Path $analysis 'spu-images'
$spu = Join-Path $experimentRoot 'port\spu'
Run-Python (Join-Path $sdk 'tools\extract_spu_images.py') $elf --output $spuImages
Run-Python (Join-Path $sdk 'tools\build_spu_workloads.py') --images $spuImages --lifted $spu --out (Join-Path $spu 'spu_workloads.c') --constructor --title BuzzQuizWorld102
Run-Python (Join-Path $experimentRoot 'tools/codegen/generate-raw-jobs.py')
@{family='quizworld-102-verified';elf_sha256=$identity} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $analysis 'game-generation.json') -Encoding UTF8
