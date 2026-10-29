$ErrorActionPreference = 'Stop'
$experimentRoot = $PSScriptRoot
$sdk = Join-Path $experimentRoot 'ps3recomp'
$python = Join-Path $experimentRoot 'toolenv\Scripts\python.exe'
. (Join-Path $experimentRoot 'buzz-paths.ps1')
$elf = (Resolve-Path -LiteralPath (Get-BuzzPaths).eboot_elf).Path
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
$codeEnd = '0x{0:x}' -f (($stubAddresses | Measure-Object -Maximum).Maximum + 32)
Run-Python (Join-Path $sdk 'tools\ppu_lifter.py') $elf --functions (Join-Path $analysis 'functions-full.json') --hle-stubs (Join-Path $analysis 'EBOOT.imports.json') --output $recompiled --jobs 6 --chunks 96 --code-end $codeEnd
$spuImages = Join-Path $analysis 'spu-images'
$spu = Join-Path $experimentRoot 'port\spu'
Run-Python (Join-Path $sdk 'tools\extract_spu_images.py') $elf --output $spuImages
Run-Python (Join-Path $sdk 'tools\build_spu_workloads.py') --images $spuImages --lifted $spu --out (Join-Path $spu 'spu_workloads.c') --constructor --title BuzzQuizWorld102
Run-Python (Join-Path $experimentRoot 'generate-raw-jobs.py')
