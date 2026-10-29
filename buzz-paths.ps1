# Locations of the user's own game files, from buzz-paths.json (not in git).
#   . (Join-Path $PSScriptRoot 'buzz-paths.ps1'); $paths = Get-BuzzPaths
function Get-BuzzPaths {
    param([string]$ConfigPath = '')
    $file = if ($ConfigPath) { (Resolve-Path -LiteralPath $ConfigPath -ErrorAction Stop).Path } else { Join-Path $PSScriptRoot 'buzz-paths.json' }
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Missing $file. Copy buzz-paths.example.json to buzz-paths.json and set the paths to your own game dump, dev_flash and decrypted update 1.02 EBOOT.elf."
    }
    $paths = Get-Content -LiteralPath $file -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($key in 'game_root','dev_flash','eboot_elf') {
        if (-not $paths.$key) { throw "buzz-paths.json: '$key' is not set" }
    }
    $paths
}
