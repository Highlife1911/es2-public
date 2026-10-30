[CmdletBinding()]
param(
    [string]$Mode='check', [string]$DataRoot, [string]$InstallName,
    [string]$GoldenRoot, [string]$RepositoryRoot, [string]$OutputDirectory,
    [string[]]$Suite, [string]$Python, [string]$Hooks='', [switch]$DryRun,
    [string]$MockDirectory=''
)
# Copied next to bridge.json in an isolated driver tree by run-draft-coverage.
# The unchanged coverage wrapper supplies its own tree as RepositoryRoot;
# redirect only that argument and registry to the actual reviewed workspace.
$ErrorActionPreference='Stop'
$bridge=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'bridge.json') -Raw | ConvertFrom-Json
if($Mode -ne 'check') { throw 'Draft coverage bridge only permits check mode.' }
$forward=@{}; foreach($key in $PSBoundParameters.Keys) { $forward[$key]=$PSBoundParameters[$key] }
$forward.RepositoryRoot=$bridge.repository
$forward.SuitesFile=$bridge.suites_file
& $bridge.runner @forward
