[CmdletBinding()]
param([Parameter(Mandatory)][string]$Executable,
      [Parameter(Mandatory)][string]$OutputDirectory,
      [int]$Launches=50)
$ErrorActionPreference='Stop'
if (!$env:ES2_DATA_ROOT) { throw 'ES2_DATA_ROOT is required.' }
$exe=[IO.Path]::GetFullPath($Executable)
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (!(Test-Path -LiteralPath $output -PathType Container)) { throw 'An existing scratch directory is required.' }
$serial=0
function Start-Harness([string]$Selection) {
    $script:serial++
    $info=[Diagnostics.ProcessStartInfo]::new($exe,$Selection)
    $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($info)
    return @{ Process=$process; Out=$process.StandardOutput.ReadToEndAsync(); Err=$process.StandardError.ReadToEndAsync();
              Selection=$Selection; Id=$script:serial }
}
function Finish-Harness($Run) {
    $Run.Process.WaitForExit()
    $text=$Run.Out.Result+$Run.Err.Result
    [IO.File]::WriteAllText((Join-Path $output ('launch-{0:D3}-{1}.log' -f $Run.Id,$Run.Selection)),$text)
    $status=$Run.Process.ExitCode
    $Run.Process.Dispose()
    # Both threads have 16 MiB stacks. The image may live in the bottom of the
    # main stack's reservation (mode main-stack-bottom), never in the worker's.
    $mode=[regex]::Match($text,'STARTUP reservation=00400000 mode=(main-stack-bottom|free-range)')
    if(!$mode.Success) { throw "Missing startup diagnostics: launch $($Run.Id)" }
    foreach($thread in @('main','worker')) {
        $match=[regex]::Match($text,"STACK $thread base=([0-9a-fA-F]+) limit=([0-9a-fA-F]+) allocation=([0-9a-fA-F]+) reserve=([0-9]+)")
        if(!$match.Success) { throw "Missing stack diagnostics: launch $($Run.Id), $thread" }
        $top=[Convert]::ToUInt64($match.Groups[1].Value,16)
        $limit=[Convert]::ToUInt64($match.Groups[2].Value,16)
        $bottom=[Convert]::ToUInt64($match.Groups[3].Value,16)
        if($top-$bottom -ne 0x1000000) { throw "Unexpected stack size: launch $($Run.Id), $thread" }
        $overlaps=$bottom -lt 0x6d8000 -and $top -gt 0x400000
        $inPlace=$thread -eq 'main' -and $mode.Groups[1].Value -eq 'main-stack-bottom' -and $bottom -eq 0x400000 -and $limit -gt 0x6d9000
        if($overlaps -and !$inPlace) { throw "Unexpected stack layout: launch $($Run.Id), $thread" }
    }
    if ($Run.Selection -eq 'unknown-startup-fixture') {
        if ($status -ne 1 -or $text -notmatch 'Mapped SHA-256 verified DBSIM at 0x00400000' -or
            $text -notmatch 'FAIL: unknown function selection' -or $text -match 'base collision|preferred image base unavailable') {
            throw "Startup failed: launch $($Run.Id), status $status"
        }
    } elseif ($status -ne 0 -or $text -notmatch 'PASS: in-vitro differential run') {
        throw "Concurrent worker failed: launch $($Run.Id), status $status"
    }
}
foreach ($parallel in @(0,6)) {
    $workers=@()
    for ($j=0; $j -lt $parallel; $j++) { $workers+=Start-Harness euler_matrix }
    for ($i=0; $i -lt $Launches; $i++) {
        # Keep six live handles at each foreground launch; completed work is
        # verified before replacement, and all remaining workers are joined.
        for ($j=0; $j -lt $workers.Count; $j++) {
            if ($workers[$j].Process.HasExited) { Finish-Harness $workers[$j]; $workers[$j]=Start-Harness euler_matrix }
        }
        Finish-Harness (Start-Harness unknown-startup-fixture)
    }
    foreach ($worker in $workers) { Finish-Harness $worker }
    Write-Output "PASS: $Launches/$Launches mapped launches with $parallel concurrent euler_matrix workers; zero collisions"
}
