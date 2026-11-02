$ErrorActionPreference='Stop'
$geometryRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $geometryRoot 'scripts/buzz-runtime-defaults.ps1')
$geometrySaved=@{}
foreach($key in @('BUZZ_GEOMETRY_JOB','BUZZ_NATIVE_COMPLETION','BUZZ_ANIMATION_JOB')){$geometrySaved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
function Check($ok,$message){if(-not $ok){throw $message}}
function Restore($snapshot){foreach($key in $snapshot.Keys){if($null -eq $snapshot[$key]){Remove-Item "Env:$key" -ErrorAction SilentlyContinue}else{[Environment]::SetEnvironmentVariable($key,$snapshot[$key],'Process')}}}
function GoodManifest {
    '{"family":"quizworld-eu100","elf_sha256":"6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8","renderer":"vulkan","geometry_job_profile":"host-groups-v1","animation_job_profile":"host-soa-v1","runtime_defaults":{"BUZZ_NATIVE_COMPLETION":"2","BUZZ_GEOMETRY_JOB":"1","BUZZ_ANIMATION_JOB":"1"}}' | ConvertFrom-Json
}
try {
    Remove-Item Env:BUZZ_ANIMATION_JOB -ErrorAction SilentlyContinue
    foreach($original in @($null,'','0','1','2')) {
        if($null -eq $original){Remove-Item Env:BUZZ_GEOMETRY_JOB -ErrorAction SilentlyContinue}else{[Environment]::SetEnvironmentVariable('BUZZ_GEOMETRY_JOB',$original,'Process')}
        Remove-Item Env:BUZZ_NATIVE_COMPLETION -ErrorAction SilentlyContinue
        $before=[Environment]::GetEnvironmentVariable('BUZZ_GEOMETRY_JOB','Process')
        $snapshot=@{}
        try {
            Set-BuzzBuildRuntimeDefaults -Binding (GoodManifest) -Previous $snapshot
            $expected=if([string]::IsNullOrEmpty($original)){'1'}else{$original}
            Check ($env:BUZZ_GEOMETRY_JOB -eq $expected) 'Geometry default overwrote an explicit override'
            Check ($env:BUZZ_NATIVE_COMPLETION -eq '2') 'Completion default lost'
            Check ($env:BUZZ_ANIMATION_JOB -eq '1') 'Animation default lost'
            Check ($snapshot.ContainsKey('BUZZ_GEOMETRY_JOB')) 'Geometry default was not snapshotted before applying'
        } finally {Restore $snapshot}
        Check ([Environment]::GetEnvironmentVariable('BUZZ_GEOMETRY_JOB','Process') -ceq $before) 'Geometry default leaked into the parent'
        Check ([string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable('BUZZ_NATIVE_COMPLETION','Process'))) 'Completion default leaked'
    }
    foreach($original in @($null,'','0','1','2')) {
        if($null -eq $original){Remove-Item Env:BUZZ_ANIMATION_JOB -ErrorAction SilentlyContinue}else{[Environment]::SetEnvironmentVariable('BUZZ_ANIMATION_JOB',$original,'Process')}
        $before=[Environment]::GetEnvironmentVariable('BUZZ_ANIMATION_JOB','Process');$snapshot=@{}
        try {
            Set-BuzzBuildRuntimeDefaults -Binding (GoodManifest) -Previous $snapshot
            $expected=if([string]::IsNullOrEmpty($original)){'1'}else{$original}
            Check ($env:BUZZ_ANIMATION_JOB -ceq $expected) 'Animation default overwrote explicit override'
            Check ($snapshot.ContainsKey('BUZZ_ANIMATION_JOB')) 'Animation option not snapshotted'
        } finally {Restore $snapshot}
        Check ([Environment]::GetEnvironmentVariable('BUZZ_ANIMATION_JOB','Process') -ceq $before) 'Animation default leaked into parent'
    }
    $badCases=@(
        {param($m)$m.geometry_job_profile='legacy'},
        {param($m)$m.family='quizworld-us100'},
        {param($m)$m.elf_sha256='unqualified'},
        {param($m)$m.renderer='d3d12'},
        {param($m)$m.runtime_defaults.BUZZ_GEOMETRY_JOB='0'},
        {param($m)$m.runtime_defaults.BUZZ_GEOMETRY_JOB='2'},
        {param($m)$m.runtime_defaults.BUZZ_GEOMETRY_JOB=1},
        {param($m)$m.runtime_defaults.BUZZ_GEOMETRY_JOB=$true},
        {param($m)$m.runtime_defaults.BUZZ_GEOMETRY_JOB=$null},
        {param($m)$m.runtime_defaults | Add-Member unexpected_option '1'},
        {param($m)$m.runtime_defaults.BUZZ_NATIVE_COMPLETION='1'},
        {param($m)$m.runtime_defaults='invalid-map'},
        {param($m)$m.animation_job_profile='unqualified'},
        {param($m)$m.PSObject.Properties.Remove('animation_job_profile')},
        {param($m)$m.runtime_defaults.BUZZ_ANIMATION_JOB='0'},
        {param($m)$m.runtime_defaults.BUZZ_ANIMATION_JOB='2'},
        {param($m)$m.runtime_defaults.BUZZ_ANIMATION_JOB=1},
        {param($m)$m.runtime_defaults.BUZZ_ANIMATION_JOB=$true},
        {param($m)$m.runtime_defaults.BUZZ_ANIMATION_JOB=$null}
    )
    foreach($edit in $badCases) {
        Remove-Item Env:BUZZ_GEOMETRY_JOB,Env:BUZZ_NATIVE_COMPLETION,Env:BUZZ_ANIMATION_JOB -ErrorAction SilentlyContinue
        $manifest=GoodManifest;& $edit $manifest;$snapshot=@{};$rejected=$false
        try {Set-BuzzBuildRuntimeDefaults -Binding $manifest -Previous $snapshot} catch {$rejected=$true}
        Check $rejected 'Unqualified runtime defaults accepted'
        Check ($snapshot.Count -eq 0 -and [string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable('BUZZ_GEOMETRY_JOB','Process')) -and [string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable('BUZZ_NATIVE_COMPLETION','Process'))) 'Rejected defaults partially mutated the parent'
        Check ([string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable('BUZZ_ANIMATION_JOB','Process'))) 'Rejected animation default mutated parent'
    }
    # Exercise the builder's actual selection expression without running codegen
    # or CMake. An unqualified build must never acquire the JOB1 capability tag.
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $geometryRoot 'scripts/build-windows.ps1'),[ref]$tokens,[ref]$errors)
    Check (-not $errors.Count) 'Builder syntax invalid'
    $assignments=@($ast.FindAll({param($node)$node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -eq '$geometryJobProfile' -and $node.Right.Extent.Text.Contains('$geometryPhase2')},$true))
    Check ($assignments.Count -eq 1) 'Missing unique qualified build selection'
    $select=[scriptblock]::Create($assignments[0].Right.Extent.Text)
    function Select-Profile($change) {
        $geometryPhase2=$true;$VulkanExperimental=$true;$SpuIsa='avx2';$SpuOpt='-O2'
        $VideoTrace=$false;$ProducerTrace=$false;$SpuLsDebugHooks=$false;$MergeGeometry=$false
        $ResultGuardFP=$true;$HotPpu=$true;$NativeCompletion=$true;$NativeImage40=$true
        $NativeImage39=$true;$NativeImage36=$true;$NativeImage7=$true;$MStreamModules=$true
        $generation=GoodManifest
        foreach($entry in $change.GetEnumerator()) {
            if($entry.Key -in @('family','elf_sha256')){$generation.($entry.Key)=$entry.Value}
            else{Set-Variable -Name $entry.Key -Value $entry.Value}
        }
        & $select
    }
    Check (Select-Profile @{}) 'Qualified standard build lost host profile'
    $unqualified=@(
        @{geometryPhase2=$false},@{VulkanExperimental=$false},@{family='quizworld-us100'},
        @{elf_sha256='unqualified'},@{SpuIsa='sse2'},@{SpuIsa='avx512'},@{SpuOpt='-O3'},
        @{VideoTrace=$true},@{ProducerTrace=$true},@{SpuLsDebugHooks=$true},@{MergeGeometry=$true},
        @{ResultGuardFP=$false},@{HotPpu=$false},@{NativeCompletion=$false},@{NativeImage40=$false},
        @{NativeImage39=$false},@{NativeImage36=$false},@{NativeImage7=$false},@{MStreamModules=$false}
    )
    foreach($change in $unqualified){Check (-not (Select-Profile $change)) 'Unqualified build acquired host profile'}
    "PASS geometry/animation defaults: qualified launch, explicit overrides, parent restoration, $($badCases.Count) invalid manifests and $($unqualified.Count+1) build selections"
} finally {Restore $geometrySaved}
