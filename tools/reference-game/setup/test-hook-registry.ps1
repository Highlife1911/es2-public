[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if (!(Test-Path -LiteralPath $OutputDirectory -PathType Container)) { throw 'Existing scratch directory required.' }
$source=Join-Path $PSScriptRoot 'configure-tick-log.ps1'
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw 'Configuration script parse error.' }
$definition=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-TickLogHookConfiguration'},$true)
# Extract only the pure configuration function: never execute deployment code.
# A newly parsed ScriptBlock has no script root; bind that token to this file's
# setup directory explicitly before evaluating the extracted function.
$definitionText=$definition.Extent.Text.Replace('$PSScriptRoot', "'"+$PSScriptRoot.Replace("'","''")+"'")
. ([scriptblock]::Create($definitionText))
$names=Get-Content -LiteralPath (Join-Path $PSScriptRoot '../proxy/hook-names.txt')
$tick=Join-Path $OutputDirectory ('registry-'+[Guid]::NewGuid().ToString('N')+'.jsonl')
$empty=Get-TickLogHookConfiguration -Specification '' -MismatchLog '' -TickLog $tick
if ($empty.Text -ne '' -or $empty.Modes.Count) { throw 'No-hooks default changed.' }
foreach ($name in $names) {
    foreach ($mode in @('original','replaced','compare')) {
        $v=Get-TickLogHookConfiguration -Specification " $($name.ToUpperInvariant()) = $mode " -MismatchLog '' -TickLog $tick
        if ($v.Modes[$name] -ne $mode -or !$v.Text.Contains("$name=$mode")) { throw "Selection failed: $name" }
    }
}
foreach ($bad in @('unknown=replaced', "$($names[0])=bad", "$($names[0])=compare,$($names[0])=original")) {
    $rejected=$false
    try { $null=Get-TickLogHookConfiguration -Specification $bad -MismatchLog '' -TickLog $tick } catch { $rejected=$true }
    if (!$rejected) { throw "Invalid selection accepted: $bad" }
}
$registry=Get-Content -LiteralPath (Join-Path $PSScriptRoot '../proxy/hook_registry.h') -Raw
$registered=@([regex]::Matches($registry,'ES2_HOOK\((\w+),') | ForEach-Object {$_.Groups[1].Value})
if (($registered -join ',') -ne ($names -join ',')) { throw 'Generated name list is stale.' }
$docs=Get-Content -LiteralPath (Join-Path $PSScriptRoot '../../../docs/reference-game/hooks.md')
$marker='<!-- H4 registry names: '+($names -join ',')+' -->'
$selection='-Hooks "'+(($names | ForEach-Object {"$_=replaced"}) -join ',')+'"'
if (@($docs | Where-Object {$_ -ceq $marker}).Count -ne 1 -or @($docs | Where-Object {$_ -ceq $selection}).Count -ne 1) {
    throw 'Documented registry/replacement selection is stale.'
}
Write-Output "PASS: $($names.Count) registry names in all three modes; documented names/replaced selection; absent hooks and malformed selections; no deployment."
