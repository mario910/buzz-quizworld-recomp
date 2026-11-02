param([string]$OutputDirectory='analysis/native-geometry/phase4/embedded-defaults-v1/profile-tests')
$ErrorActionPreference='Stop'
$profileTestRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $profileTestRoot
$profileTestPrevious=@{}
function Set-ProfileTestOption($Name,$Value) {
    if($null -eq $Value){Remove-Item "Env:$Name" -ErrorAction SilentlyContinue}
    else{[Environment]::SetEnvironmentVariable($Name,$Value,'Process')}
}
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    if(Test-Path $OutputDirectory){throw 'Evidence exists'}
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
    $profileTestVs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $profileTestVs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $profileTestCompiler=& ./toolenv/Scripts/python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $profileTestOptions=@(Get-Content port/runtime_defaults.inc | ForEach-Object {
        if($_ -match '^X\((\w+), "([^"]+)"\)'){@{Name=$Matches[1];Value=$Matches[2]}}
    })
    foreach($profileTestOption in $profileTestOptions) {
        $profileTestPrevious[$profileTestOption.Name]=[Environment]::GetEnvironmentVariable($profileTestOption.Name,'Process')
    }
    foreach($profileTestBuild in @(0,1)) {
        $profileTestCase="$OutputDirectory/$profileTestBuild"
        New-Item -ItemType Directory -Path $profileTestCase | Out-Null
        & (Join-Path $profileTestCompiler 'clang-cl.exe') /nologo /std:c++17 /O2 /W0 "/DBUZZ_RUNTIME_DEFAULTS=$profileTestBuild" tests/runtime-defaults-test.cpp port/runtime_defaults.cpp "/Fo$profileTestCase/" "/Fe$profileTestCase/test.exe" *> "$profileTestCase/build.log"
        if($LASTEXITCODE){throw 'Profile test compile failed'}
        foreach($profileTestOverride in @('unset','0','2','mixed')) {
            $profileTestExpected=@()
            $profileTestIndex=0
            foreach($profileTestOption in $profileTestOptions) {
                $profileTestValue=if($profileTestOverride -eq 'mixed'){if(($profileTestIndex++ % 2) -eq 0){'0'}else{$null}}elseif($profileTestOverride -eq 'unset'){$null}else{$profileTestOverride}
                Set-ProfileTestOption $profileTestOption.Name $profileTestValue
                $profileTestExpected+=if($null -ne $profileTestValue){$profileTestValue}elseif($profileTestBuild){$profileTestOption.Value}else{'<unset>'}
            }
            $profileTestExpected | Set-Content "$profileTestCase/$profileTestOverride.expected" -Encoding ascii
            & "$profileTestCase/test.exe" "$profileTestCase/$profileTestOverride.expected" *> "$profileTestCase/$profileTestOverride.log"
            if($LASTEXITCODE){throw 'Profile constructor/default/override test failed'}
        }
    }
    'PASS enabled/disabled build profile, absent/0/2/mixed overrides, before constructors'
} finally {
    foreach($profileTestKey in $profileTestPrevious.Keys){Set-ProfileTestOption $profileTestKey $profileTestPrevious[$profileTestKey]}
    Pop-Location
}
