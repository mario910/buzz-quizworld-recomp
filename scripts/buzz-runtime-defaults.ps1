# Build-bound defaults. Validate the whole map before mutating the caller's
# environment; the launcher's finally block restores the supplied snapshot.
function Test-BuzzEmbeddedRuntimeProfile {
    param($Binding)
    if(-not $Binding -or -not $Binding.PSObject.Properties['embedded_runtime_profile']){return $false}
    $eu=$Binding.family -ceq 'quizworld-eu100' -and $Binding.elf_sha256 -ceq '6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8'
    $us=$Binding.family -ceq 'quizworld-us100' -and $Binding.elf_sha256 -ceq 'fb78b9e6d30ba5eaa76b7e31bc3afa838736dcb5cde40fc602a628f2305ebb5d'
    $profile=if($us){'us100-vulkan-native-v1'}else{'eu100-vulkan-native-v1'}
    if(-not ($eu -or $us) -or $Binding.embedded_runtime_profile -cne $profile -or
       $Binding.renderer -cne 'vulkan' -or
       $Binding.geometry_job_profile -cne 'host-groups-v1' -or $Binding.animation_job_profile -cne 'host-soa-v1' -or
       $Binding.runtime_defaults -isnot [pscustomobject] -or @($Binding.runtime_defaults.PSObject.Properties).Count -ne 0) {
        throw 'Unknown or unqualified embedded runtime profile'
    }
    return $true
}
function Set-BuzzBuildRuntimeDefaults {
    param($Binding, [hashtable]$Previous)
    if(Test-BuzzEmbeddedRuntimeProfile -Binding $Binding){return}
    if (-not $Binding -or -not $Binding.PSObject.Properties['runtime_defaults']) { return }
    if ($Binding.runtime_defaults -isnot [pscustomobject]) { throw 'Invalid build runtime defaults' }
    $options=@($Binding.runtime_defaults.PSObject.Properties)
    $hostProfile=(($Binding.family -ceq 'quizworld-eu100' -and
        $Binding.elf_sha256 -ceq '6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8') -or
        ($Binding.family -ceq 'quizworld-us100' -and
        $Binding.elf_sha256 -ceq 'fb78b9e6d30ba5eaa76b7e31bc3afa838736dcb5cde40fc602a628f2305ebb5d')) -and $Binding.renderer -ceq 'vulkan' -and
        $Binding.geometry_job_profile -ceq 'host-groups-v1'
    foreach($option in $options) {
        $completion=$option.Name -ceq 'BUZZ_NATIVE_COMPLETION' -and $option.Value -is [string] -and $option.Value -ceq '2'
        $geometry=$option.Name -ceq 'BUZZ_GEOMETRY_JOB' -and $option.Value -is [string] -and $option.Value -ceq '1' -and $hostProfile
        $animation=$option.Name -ceq 'BUZZ_ANIMATION_JOB' -and $option.Value -is [string] -and $option.Value -ceq '1' -and
            $hostProfile -and $Binding.animation_job_profile -ceq 'host-soa-v1'
        if(-not ($completion -or $geometry -or $animation)) { throw 'Unknown or unqualified build-bound runtime option' }
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
