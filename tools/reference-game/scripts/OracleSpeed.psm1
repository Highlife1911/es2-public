# Shared pure runner operations, exercised with synthetic files/process mocks.
function New-TickCursor {
    param([string]$Path,[switch]$CompactRng)
    $kind=if($CompactRng){'tick_compact'}else{'tick'}
    return @{Path=$Path;Offset=[long]0;Count=[long]0;Tail='';BytesRead=[long]0;Pattern=('(?m)^\{"event":"'+$kind+'"[,}]')}
}
function Read-TickCursor {
    param([hashtable]$Cursor)
    if (!(Test-Path -LiteralPath $Cursor.Path)) { return $Cursor.Count }
    $file = [IO.File]::Open($Cursor.Path,'Open','Read',([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    try {
        if ($file.Length -lt $Cursor.Offset) { throw 'Tick log was truncated during a run.' }
        [void]$file.Seek($Cursor.Offset,'Begin')
        $buffer = [byte[]]::new(65536)
        # Read only the size observed at poll entry; a fast writer cannot keep
        # the polling call alive indefinitely. Keep incomplete lines for later.
        $remaining = $file.Length-$Cursor.Offset
        while ($remaining -gt 0) {
            $n = $file.Read($buffer,0,[int][Math]::Min($buffer.Length,$remaining))
            if (!$n) { break }
            $remaining -= $n; $Cursor.Offset += $n; $Cursor.BytesRead += $n
            $text = $Cursor.Tail + [Text.Encoding]::ASCII.GetString($buffer,0,$n)
            $end = $text.LastIndexOf("`n")
            if ($end -lt 0) {
                $Cursor.Tail=$text
                if ($Cursor.Tail.Length -gt 131072) { throw 'Unbounded incomplete tick-log line.' }
                continue
            }
            $complete=$text.Substring(0,$end+1);$Cursor.Tail=$text.Substring($end+1)
            $Cursor.Count += [regex]::Matches($complete,$Cursor.Pattern).Count
            if ($Cursor.Tail.Length -gt 131072) { throw 'Unbounded incomplete tick-log line.' }
        }
    } finally { $file.Dispose() }
    return $Cursor.Count
}
function Update-TickProgress {
    param([hashtable]$State,[long]$Count,[datetime]$Now,[int]$StallSeconds)
    if ($Count -ne $State.Previous) { $State.Previous=$Count;$State.Progress=$Now }
    elseif ($Count -gt 0 -and ($Now-$State.Progress).TotalSeconds -ge $StallSeconds) { $State.Stalled=$true }
}
function Request-TickFlush {
    param([int]$ProcessId,[int]$TimeoutMs=3000)
    $request=$null;$ack=$null
    try {
        $request=[Threading.EventWaitHandle]::OpenExisting("Local\es2-tick-$ProcessId-flush")
        $ack=[Threading.EventWaitHandle]::OpenExisting("Local\es2-tick-$ProcessId-ack")
        [void]$ack.Reset();[void]$request.Set()
        if (!$ack.WaitOne($TimeoutMs)) { throw 'Proxy flush acknowledgement timed out; process was not stopped.' }
    } finally { if ($request) {$request.Dispose()};if ($ack) {$ack.Dispose()} }
}
function Assert-MeasurementName {
    param([string]$Name)
    if ($Name -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]*$' -or $Name -ieq 'native-r111') { throw 'Only a named measurement clone is allowed; native-r111 is protected.' }
}
function Assert-NoLinks {
    param([string]$Path)
    $item=Get-Item -LiteralPath $Path
    while ($item) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked path refused: $($item.FullName)" }
        $item=$item.Parent
    }
}
# DBSIM arguments that start-timing-mission.ps1 always passes ("-eggplant -X2
# -R0" from ES.EXE and the language character; docs/reverse-engineering/launcher.md).
function Get-DefaultGameArguments {
    return @('-eggplant','-X2','-R0','-G')
}
# Validates extra DBSIM arguments (-GameArguments) before anything is configured
# or launched. They are appended to the fixed arguments exactly as given, so an
# argument is refused when it would repeat or contradict one of them: the
# -eggplant word, the exit-code option -X, the counter option -R (case-sensitive:
# -r<name> records a tape and stays allowed) or a language letter -E/-F/-G
# (main parser 0x45e758 switches on the first letter after the dash). An
# argument with whitespace or a double quote is refused too: Start-Process
# joins the array without quoting, so it would reach DBSIM as several arguments.
function Assert-GameArguments {
    param([AllowEmptyCollection()][AllowNull()][string[]]$Arguments)
    $values=@($Arguments | Where-Object { $null -ne $_ })
    for ($i=0;$i -lt $values.Count;$i++) {
        $value=$values[$i]
        if ($value.Length -eq 0) { throw "GameArguments[$i] is empty." }
        if ($value -match '[\s"]') { throw "GameArguments[$i] '$value' contains whitespace or a double quote; the launch would split it into several DBSIM arguments." }
        foreach ($fixed in (Get-DefaultGameArguments)) {
            if ($value -ieq $fixed) { throw "GameArguments[$i] '$value' is already passed to DBSIM by the launch scripts ($((Get-DefaultGameArguments) -join ' '))." }
        }
        $body=$value -replace '^[-/]',''
        if ($body.Length -eq $value.Length) { continue }
        if ($body -ieq 'eggplant') { throw "GameArguments[$i] '$value' collides with the fixed DBSIM argument -eggplant." }
        if ($body.Length -eq 0) { continue }
        $letter=[string]$body[0]
        $reason=$null
        if ($letter -ceq 'X') { $reason='the exit-code option -X (the launch scripts pass -X2)' }
        elseif ($letter -ceq 'R') { $reason='the counter option -R (the launch scripts pass -R0)' }
        elseif ($letter -cin 'E','F','G') { $reason='a language option (the launch scripts pass -G)' }
        if ($reason) { throw "GameArguments[$i] '$value' collides with $reason." }
    }
}
# The JSON form is how run-oracle-parallel.ps1 and run-oracle-regression.ps1
# hand the list to a child "powershell -File": -File cannot bind an array whose
# elements start with a dash.
function ConvertFrom-GameArgumentsJson {
    param([string]$Json)
    if (!$Json) { return ,@() }
    # Windows PowerShell 5.1 returns the array as one object, PowerShell 7 unrolls
    # it: collect element by element so that both give the same list.
    $parsed=ConvertFrom-Json $Json
    $list=@()
    foreach ($item in $parsed) {
        if ($item -isnot [string]) { throw 'GameArgumentsJson must be a JSON array of strings.' }
        $list+=$item
    }
    return ,$list
}
# Per-suite DBSIM arguments (registry field "game_arguments", format 3).
# Returns the suite's own list (empty when the field is absent, null or []), after
# the same checks as -GameArguments. The field must be a JSON array of strings.
function Get-SuiteGameArguments {
    param($Suite)
    $property=$Suite.PSObject.Properties['game_arguments']
    if (!$property -or $null -eq $property.Value) { return ,@() }
    if ($property.Value -is [string] -or $property.Value -isnot [System.Collections.IEnumerable]) { throw "Suite '$($Suite.name)': game_arguments must be a JSON array of strings." }
    $list=@()
    foreach ($item in $property.Value) {
        if ($item -isnot [string]) { throw "Suite '$($Suite.name)': game_arguments must be a JSON array of strings." }
        $list+=$item
    }
    try { Assert-GameArguments $list } catch { throw "Suite '$($Suite.name)': game_arguments: $($_.Exception.Message)" }
    return ,$list
}
# The option letter of a DBSIM argument ("-pFILE" -> "p"); case-sensitive, empty
# for an argument without a leading dash or slash.
function Get-GameArgumentLetter {
    param([string]$Argument)
    $body=$Argument -replace '^[-/]',''
    if ($body.Length -eq $Argument.Length -or $body.Length -eq 0) { return '' }
    return [string]$body[0]
}
# The list a suite runs with: its own arguments first, then the command-line
# ones (-GameArguments). A command-line argument that repeats one of the suite's
# or uses the same option letter is refused (two values for one option would be
# ambiguous); the combined list must also pass Assert-GameArguments.
function Merge-GameArguments {
    param([string]$SuiteName,[AllowEmptyCollection()][string[]]$SuiteArguments,[AllowEmptyCollection()][string[]]$ExtraArguments)
    $own=@($SuiteArguments | Where-Object { $null -ne $_ })
    $extra=@($ExtraArguments | Where-Object { $null -ne $_ })
    for ($i=0;$i -lt $extra.Count;$i++) {
        $letter=Get-GameArgumentLetter $extra[$i]
        foreach ($mine in $own) {
            if ($extra[$i] -ieq $mine) { throw "GameArguments[$i] '$($extra[$i])' repeats an argument of suite '$SuiteName' (game_arguments: $($own -join ' '))." }
            if ($letter -and $letter -ceq (Get-GameArgumentLetter $mine)) { throw "GameArguments[$i] '$($extra[$i])' uses the option letter of the argument '$mine' of suite '$SuiteName' (game_arguments: $($own -join ' '))." }
        }
    }
    $combined=@($own+$extra)
    Assert-GameArguments $combined
    return ,$combined
}
# A check refuses when the suite's registry arguments are not the ones recorded
# in its golden manifest (manifest.json "game_arguments", absent = none): the
# golden belongs to one command line. A suite without a golden manifest is
# left to the comparison, which fails on the missing golden anyway.
function Assert-GoldenGameArguments {
    param([string]$GoldenRoot,[string]$SuiteName,[AllowEmptyCollection()][string[]]$SuiteArguments)
    $manifestPath=Join-Path (Join-Path $GoldenRoot $SuiteName) 'manifest.json'
    if (!(Test-Path -LiteralPath $manifestPath -PathType Leaf)) { return }
    $manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $recorded=@()
    $property=$manifest.PSObject.Properties['game_arguments']
    if ($property -and $null -ne $property.Value) { foreach ($item in $property.Value) { $recorded+=[string]$item } }
    $wanted=@($SuiteArguments | Where-Object { $null -ne $_ })
    $same=$recorded.Count -eq $wanted.Count
    for ($i=0;$same -and $i -lt $wanted.Count;$i++) { if ($recorded[$i] -cne $wanted[$i]) { $same=$false } }
    if (!$same) {
        $show={ param($list) if ($list.Count) { '[' + ($list -join ' ') + ']' } else { '[none]' } }
        throw "Suite '$SuiteName': the registry game_arguments $(& $show $wanted) differ from the DBSIM arguments recorded in its golden manifest $(& $show $recorded) ($manifestPath). The golden belongs to the recorded command line: record it again (-Mode record -Force) or restore the registry entry."
    }
}
Export-ModuleMember -Function New-TickCursor,Read-TickCursor,Update-TickProgress,Request-TickFlush,Assert-MeasurementName,Assert-NoLinks,Get-DefaultGameArguments,Assert-GameArguments,ConvertFrom-GameArgumentsJson,Get-SuiteGameArguments,Merge-GameArguments,Assert-GoldenGameArguments
