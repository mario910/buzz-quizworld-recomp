# Test profile -> compiler parameter compatibility, without game data/building.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$python=Join-Path $root 'toolenv\Scripts\python.exe'
Push-Location $root
try {
    $options=& $python -c 'import json; from launcher.game_profiles import PROFILES; print(json.dumps(PROFILES[0].compiler_options()))'
    if($LASTEXITCODE){throw 'Profile query failed'}
    $command=Get-Command (Join-Path $root 'scripts/build-windows.ps1')
    $properties=@(($options|ConvertFrom-Json).PSObject.Properties)
    if($properties.Count -ne 9){throw 'Unexpected reference native adapter plan'}
    foreach($property in $properties) {
        $name=$property.Name
        if($property.Value -isnot [bool]) {throw 'Compiler switch must be a JSON boolean'}
        if(-not $command.Parameters.ContainsKey($name) -or $command.Parameters[$name].ParameterType -ne [System.Management.Automation.SwitchParameter]) {
            throw "Profile requests an unknown/non-switch compiler option: $name"
        }
    }
    'PASS: profile adapter plan matches compiler switch parameters, with explicit native-float policy.'
} finally {Pop-Location}
