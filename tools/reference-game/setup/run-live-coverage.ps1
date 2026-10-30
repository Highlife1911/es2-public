[CmdletBinding()]
param(
    [string]$DataRoot='',
    [string]$InstallName='timing-r111',
    [string]$GoldenRoot='',
    [string]$OutputDirectory='',
    [string[]]$Suite=@(),
    [string]$Hooks='',
    [string]$CoverageHooks='all',
    [string]$ProbeEntries='',
    [ValidateSet('saturate','count')][string]$ProbeMode='count',
    [ValidateRange(1,2147483647)][int]$SaturateAfter=1,
    [switch]$ProbesOnly,
    # VOL registration/lookup probes armed from DllMain (proxy vol_probes.h).
    [switch]$VolProbes,
    # Registration sites only (lookups=0): fallback if lookup probes perturb a suite.
    [switch]$VolProbesRegistrationOnly,
    [ValidateSet('saturate','count','edge')][string]$CoverageMode='saturate',
    [ValidateRange(0,1000000)][int]$ReportEveryTicks=32,
    [switch]$FirstHitTicks,
    [string]$Python='',
    [switch]$DryRun,
    [string]$MockDirectory=''
)
if (!$DataRoot -or !$GoldenRoot -or !$Python) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$GoldenRoot) { $GoldenRoot = (Join-Path $es2.DataRepository 'oracle\golden') }
    if (!$Python) { $Python = $es2.Python }
}
$ErrorActionPreference='Stop'
# Use an explicit waited process, including under Bazel's non-console Windows
# test launcher. Do not report success merely because a native command returned.
function Quote-NativeArgument([string]$Value) {
    '"'+[regex]::Replace([regex]::Replace($Value,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'
}
function Invoke-CoverageProcess([string]$Executable,[string[]]$ArgumentValues) {
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$Executable
    $start.Arguments=($ArgumentValues | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    $process.WaitForExit();$status=$process.ExitCode;$process.Dispose()
    return @{Status=$status;Out=$stdout.Result;Err=$stderr.Result}
}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$tools=Join-Path $repo 'tools/reference-game'
if($InstallName -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]*$' -or $InstallName -ieq 'native-r111') { throw 'Only a named measurement clone is allowed; native-r111 is protected.' }
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')   # Get-CloneInstallRoot
$install=Join-Path (Get-CloneInstallRoot $DataRoot) $InstallName
if(!$MockDirectory -and !$DryRun -and (Test-Path -LiteralPath $install) -and ((Get-Item -LiteralPath $install).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'A measurement clone cannot be a link/junction.' }
$registry=[IO.File]::ReadAllText((Join-Path $tools 'proxy/hook_registry.h'))
$names=@([regex]::Matches($registry,'ES2_HOOK\((\w+),') | ForEach-Object { $_.Groups[1].Value })
$selected=if($CoverageHooks -ceq 'all') { $names } else { @($CoverageHooks.Split(',') | ForEach-Object { $_.Trim() }) }
if(!$selected.Count -or @($selected | Select-Object -Unique).Count -ne $selected.Count -or @($selected | Where-Object { $_ -cnotin $names }).Count) { throw 'CoverageHooks must be all or unique registry names.' }
if($VolProbesRegistrationOnly) { $VolProbes=[switch]::new($true) }
if($ProbesOnly -and !$ProbeEntries -and !$VolProbes) { throw 'ProbesOnly requires ProbeEntries or VolProbes.' }
$expectedEntries=@();$probeVas=@{}
if($ProbeEntries) {
    foreach($item in $ProbeEntries.Split(',')) {
        if($item.Trim() -cnotmatch '^(?:0[xX])?([0-9a-fA-F]{1,8})(?::rcv=(esp4|eax|ecx|edx|ebx|esp|ebp|esi|edi|ret))?$') { throw "Invalid probe entry: $item" }
        $va='0x{0:x8}' -f [Convert]::ToUInt32($Matches[1],16)
        if($probeVas.ContainsKey($va)) { throw "Duplicate probe entry: $va" };$probeVas[$va]=$true
        $receiver=if($Matches[2]) {$Matches[2]} else {''}
        $expectedEntries+=@{va=$va;receiver=$receiver}
    }
    if($expectedEntries.Count -gt 512) { throw 'At most 512 probe entries.' }
}
$modes=@{};foreach($name in $names) { $modes[$name]=0 }
$specified=@{}
if($Hooks) { foreach($item in $Hooks.Split(',')) { $pair=$item.Trim().Split('=');if($pair.Count -ne 2 -or $pair[0] -cnotin $names -or $pair[1] -cnotin @('original','replaced','compare') -or $specified.ContainsKey($pair[0])) { throw "Invalid hook mode: $item" };$specified[$pair[0]]=$true;$modes[$pair[0]]=@{original=0;replaced=1;compare=2}[$pair[1]] } }
$suites=@((Get-Content -LiteralPath (Join-Path $tools 'oracle-suites.json') -Raw | ConvertFrom-Json).suites)
if($Suite.Count) {if(@($Suite | Where-Object { $_ -notin $suites.name }).Count) { throw 'Unknown oracle suite.' };$suites=@($suites | Where-Object { $_.name -in $Suite })}
if(!$suites.Count) { throw 'No suite selected.' }
if(!$OutputDirectory) { $OutputDirectory=Join-Path $DataRoot ('logs/live-coverage-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')) }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$command="& '"+(Join-Path $PSScriptRoot 'run-live-coverage.ps1').Replace("'","''")+"'"
foreach($pair in @(@('DataRoot',$DataRoot),@('InstallName',$InstallName),@('GoldenRoot',$GoldenRoot),@('OutputDirectory',$OutputDirectory),@('CoverageMode',$CoverageMode),@('CoverageHooks',$CoverageHooks),@('Python',$Python))) {
    $command+=" -$($pair[0]) '"+([string]$pair[1]).Replace("'","''")+"'"
}
$command+=" -ReportEveryTicks $ReportEveryTicks"
if($ProbeEntries) { $command+=" -ProbeEntries '"+$ProbeEntries.Replace("'","''")+"' -ProbeMode $ProbeMode -SaturateAfter $SaturateAfter" }
if($ProbesOnly) { $command+=' -ProbesOnly' }
if($VolProbes) { $command+=' -VolProbes' }
if($VolProbesRegistrationOnly) { $command+=' -VolProbesRegistrationOnly' }
if($FirstHitTicks) { $command+=' -FirstHitTicks' }
if($Hooks) { $command+=" -Hooks '"+$Hooks.Replace("'","''")+"'" }
if($Suite.Count) { $command+=" -Suite @("+(($Suite | ForEach-Object { "'"+$_.Replace("'","''")+"'" }) -join ',')+")" }
Write-Output "Live measurement command: $command"
if($DryRun) {
    [ordered]@{dry_run=$true;install=$install;mode=$CoverageMode;probes_only=[bool]$ProbesOnly;vol_probes=[bool]$VolProbes;vol_probe_lookups=![bool]$VolProbesRegistrationOnly;probe_entries=$expectedEntries;probe_mode=$ProbeMode;saturate_after=$SaturateAfter;hooks=$Hooks;coverage_hooks=$selected;first_hit_ticks=[bool]$FirstHitTicks;output=$OutputDirectory;suites=@($suites | ForEach-Object { @{suite=$_.name;frame_limit=$_.frame_limit;golden=(Join-Path $GoldenRoot ($_.name+'/normalized.json'))} })} | ConvertTo-Json -Depth 6
    return
}
if(Test-Path -LiteralPath $OutputDirectory) { throw 'OutputDirectory must be fresh (no earlier report can be mistaken for this run).' }
if(!$MockDirectory) {
    $logs=[IO.Path]::GetFullPath((Join-Path $DataRoot 'logs'))+[IO.Path]::DirectorySeparatorChar
    if(!$OutputDirectory.StartsWith($logs,[StringComparison]::OrdinalIgnoreCase)) { throw 'Live output must stay in DataRoot/logs.' }
    foreach($s in $suites) { if(!(Test-Path -LiteralPath (Join-Path $GoldenRoot ($s.name+'/normalized.json')) -PathType Leaf)) { throw "Missing golden: $($s.name)" } }
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$manifestPath=Join-Path $OutputDirectory 'coverage-manifest.json'
if($MockDirectory) {
    # No launch, configuration/deployment, process lookup/stop, environment write,
    # original data or installation access occurs on this path.
    if(!$ProbesOnly) { Copy-Item -LiteralPath (Join-Path $MockDirectory 'coverage-manifest.json') -Destination $manifestPath }
    if($ProbeEntries) { Copy-Item -LiteralPath (Join-Path $MockDirectory 'probes-manifest.json') -Destination (Join-Path $OutputDirectory 'probes-manifest.json') }
    if($VolProbes) { Copy-Item -LiteralPath (Join-Path $MockDirectory 'vol-probes-manifest.json') -Destination (Join-Path $OutputDirectory 'vol-probes-manifest.json') }
} else {
    $entries=@();$expectedHooks=@{};foreach($name in $selected) { $expectedHooks[$name]=$modes[$name] }
    $expectedSites=@()
    foreach($m in [regex]::Matches([IO.File]::ReadAllText((Join-Path $tools 'invitro/branch_sites.h')),'BRANCH\(\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*\d+\s*,\s*(\w+)\s*,\s*0x[0-9a-fA-F]+\s*\)')) {
        if($m.Groups[1].Value -in $selected) { $expectedSites+=@{hook=$m.Groups[1].Value;va=('0x{0:x8}' -f [Convert]::ToUInt32($m.Groups[2].Value.Substring(2),16));condition=$m.Groups[3].Value} }
    }
    $previous=$env:ES2_LIVE_COVERAGE_CONFIG
    try {
        foreach($s in $suites) {
            $run=Join-Path $OutputDirectory $s.name
            $coverage=Join-Path $run 'coverage.json'
            $configuration=Join-Path $OutputDirectory ($s.name+'-coverage.ini')
            # A suite with hold ends parked after its tick count (no exit): the window is frozen at that tick.
            $limit=if($s.frame_limit) { $s.frame_limit } elseif($s.PSObject.Properties['hold'] -and $s.hold -eq $true) { $s.ticks } else { 0 }
            $text="[coverage]`nenabled=$([int](!$ProbesOnly))`nmode=$CoverageMode`nhooks=$CoverageHooks`nreport=$coverage`nreport_every_ticks=$ReportEveryTicks`nframe_limit=$limit`nfirst_hit_ticks=$([int][bool]$FirstHitTicks)`n"
            $probes=Join-Path $run 'probes.json'
            # probes-load.json holds the counts at the first tick (arming to first frame).
            $probesLoad=Join-Path $run 'probes-load.json'
            if($ProbeEntries) { $text+="[probes]`nenabled=1`nmode=$ProbeMode`nsaturate_after=$SaturateAfter`nentries=$ProbeEntries`nreport=$probes`nload_report=$probesLoad`nframe_limit=$limit`n" }
            $volReport=Join-Path $run 'vol-probes.json'
            if($VolProbes) { $text+="[vol_probes]`nenabled=1`nlookups=$([int]![bool]$VolProbesRegistrationOnly)`nreport=$volReport`nframe_limit=$limit`nreport_every_ticks=32`n" }
            [IO.File]::WriteAllText($configuration,$text,[Text.UnicodeEncoding]::new($false,$true))
            $env:ES2_LIVE_COVERAGE_CONFIG=$configuration
            # Delegate the actual launch, path/start-time checked stop, normalization
            # and check-mode golden comparison to the existing runner unchanged.
            $arguments=@('-NoProfile','-File',(Join-Path $PSScriptRoot 'run-oracle-regression.ps1'),'-Mode','check','-DataRoot',$DataRoot,'-InstallName',$InstallName,'-GoldenRoot',$GoldenRoot,'-RepositoryRoot',$repo,'-OutputDirectory',$run,'-Suite',$s.name,'-Python',$Python)
            if($Hooks) { $arguments+=@('-Hooks',$Hooks) }
            $check=Invoke-CoverageProcess -Executable (Get-Process -Id $PID).Path -ArgumentValues $arguments
            [IO.File]::WriteAllText((Join-Path $OutputDirectory ($s.name+'-runner.log')),$check.Out+$check.Err)
            if($check.Status -ne 0) { throw "Oracle check failed; coverage invalid: $($s.name). See $OutputDirectory" }
            $entry=Get-Content -LiteralPath (Join-Path $run ($s.name+'-entry.json')) -Raw | ConvertFrom-Json
            if($entry.result -ne 'identical') { throw "Oracle check was not identical: $($s.name)" }
            $entries+=@{suite=$s.name;golden=(Join-Path $GoldenRoot ($s.name+'/normalized.json'));normalized=(Join-Path $run ($s.name+'-normalized.json'));coverage=$coverage;probes=$probes;probes_load=$probesLoad;vol_probes=$volReport;pid=$entry.launch.pid;proxy_sha256=$entry.proxy_sha256;hold_frame=$entry.launch.hold_frame}
        }
    } catch {
        @{valid=$false;error=$_.Exception.Message} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'coverage-summary.json')
        if($ProbeEntries) {
            @{valid=$false;error=$_.Exception.Message} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'probes-summary.json')
            ('Probes invalid: '+$_.Exception.Message) | Set-Content -LiteralPath (Join-Path $OutputDirectory 'probes-summary.md')
        }
        ('Coverage invalid: '+$_.Exception.Message) | Set-Content -LiteralPath (Join-Path $OutputDirectory 'coverage-summary.md')
        throw
    } finally { $env:ES2_LIVE_COVERAGE_CONFIG=$previous }
    if($ProbeEntries) { @{suites=$entries;expected_entries=$expectedEntries;probe_mode=$ProbeMode;saturate_after=$SaturateAfter} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'probes-manifest.json') }
    if($VolProbes) { @{suites=$entries} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'vol-probes-manifest.json') }
    @{suites=$entries;expected_hooks=$expectedHooks;expected_sites=$expectedSites;expected_first_hit_ticks=[bool]$FirstHitTicks} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath
}
if(!$ProbesOnly) {
$arguments=@('-B',(Join-Path $tools 'scripts/live_coverage_report.py'),$manifestPath,'--output',$OutputDirectory)
$validation=Invoke-CoverageProcess -Executable $Python -ArgumentValues $arguments
Write-Output $validation.Out
if($validation.Status -ne 0) { throw "Coverage validation failed: $($validation.Err) $OutputDirectory" }
if(!(Test-Path -LiteralPath (Join-Path $OutputDirectory 'coverage-summary.json'))) { throw 'Coverage helper produced no summary.' }
Write-Output "Validated coverage: $OutputDirectory\coverage-summary.json"

}
if($ProbeEntries) {
    $arguments=@('-B',(Join-Path $tools 'scripts/live_probe_report.py'),(Join-Path $OutputDirectory 'probes-manifest.json'),'--output',$OutputDirectory)
    $validation=Invoke-CoverageProcess -Executable $Python -ArgumentValues $arguments
    Write-Output $validation.Out
    if($validation.Status -ne 0) { throw "Probe validation failed: $($validation.Err) $OutputDirectory" }
    if(!(Test-Path -LiteralPath (Join-Path $OutputDirectory 'probes-summary.json'))) { throw 'Probe helper produced no summary.' }
    Write-Output "Validated probes: $OutputDirectory\probes-summary.json"
}
if($VolProbes) {
    # Per-suite VOL registration order from vol-probes.json (schema 1,
    # docs/formats/vol.md). Raw records stay in the reports; this lists the
    # registration-phase roles, the forwarding boundary and the final slots.
    $summary=@()
    foreach($s in @((Get-Content -LiteralPath (Join-Path $OutputDirectory 'vol-probes-manifest.json') -Raw | ConvertFrom-Json).suites)) {
        if(!(Test-Path -LiteralPath $s.vol_probes -PathType Leaf)) { throw "Missing VOL probe report: $($s.suite)" }
        $r=Get-Content -LiteralPath $s.vol_probes -Raw | ConvertFrom-Json
        if($r.kind -ne 'vol_probes' -or $r.schema -ne 1 -or !$r.armed -or $r.module_verified -ne $true) { throw "VOL probe report not armed/verified: $($s.suite)" }
        $names=@{};foreach($a in @($r.archives)) { $names[$a.ptr]="$($a.name)@$($a.priority)" }
        $registration=@($r.events | Where-Object { $_.role -and $_.role -notin @('stream_open','lookup','member_lookup') } | ForEach-Object { if($_.text -and $_.text[0]) { "$($_.seq):$($_.role):$($_.text[0])" } else { "$($_.seq):$($_.role)" } })
        $final=@(if($r.final -and $r.final.slots) { $r.final.slots | ForEach-Object { if($names.ContainsKey($_)) { $names[$_] } else { $_ } } })
        $summary+=[ordered]@{suite=$s.suite;reason=$r.reason;complete=$r.complete;error=$r.error;forwarding_event=$r.forwarding_event;events_total=$r.events_total;events_dropped=$r.events_dropped;other_thread=$r.other_thread;registration=$registration;final_slots=$final}
    }
    $summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'vol-probes-summary.json')
    Write-Output "VOL probe summary: $OutputDirectory\vol-probes-summary.json"
}
