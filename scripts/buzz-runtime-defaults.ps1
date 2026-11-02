# Build-bound defaults. Validate the whole map before mutating the caller's
# environment; the launcher's finally block restores the supplied snapshot.
function Set-BuzzBuildRuntimeDefaults {
    param($Binding, [hashtable]$Previous)
    if (-not $Binding -or -not $Binding.PSObject.Properties['runtime_defaults']) { return }
    if ($Binding.runtime_defaults -isnot [pscustomobject]) { throw 'Invalid build runtime defaults' }
    $options=@($Binding.runtime_defaults.PSObject.Properties)
    foreach($option in $options) {
        $completion=$option.Name -ceq 'BUZZ_NATIVE_COMPLETION' -and $option.Value -is [string] -and $option.Value -ceq '2'
        $geometry=$option.Name -ceq 'BUZZ_GEOMETRY_JOB' -and $option.Value -is [string] -and $option.Value -ceq '1' -and
            $Binding.family -ceq 'quizworld-eu100' -and $Binding.renderer -ceq 'vulkan' -and
            $Binding.elf_sha256 -ceq '6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8' -and
            $Binding.geometry_job_profile -ceq 'host-groups-v1'
        if(-not ($completion -or $geometry)) { throw 'Unknown or unqualified build-bound runtime option' }
    }
    foreach($option in $options) {
        if(-not $Previous.ContainsKey($option.Name)) {
            $Previous[$option.Name]=[Environment]::GetEnvironmentVariable($option.Name,'Process')
        }
        if([string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable($option.Name,'Process'))) {
            [Environment]::SetEnvironmentVariable($option.Name,$option.Value,'Process')
        }
    }
}
