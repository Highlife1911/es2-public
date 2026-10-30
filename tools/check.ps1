# Acceptance script: the coordinator's per-batch test protocol in one command
# (docs/reference-game/oracle_regression.md).
#
#   & tools\check.ps1                  # quick tier + signatures + compare audit
#   & tools\check.ps1 -Full            # ... plus the full in-vitro tier
#   & tools\check.ps1 -Live            # ... plus one replaced-mode regression on the five clones
#   & tools\check.ps1 -Live -InstallName timing-r111,timing-r111-p1,timing-r111-p2,timing-r111-p4   # one to six clones
#   & tools\check.ps1 -SkipQuick -Live -HookModes modes.txt -RtlConfig rtl.ini
#   & tools\check.ps1 -Live -Overlap   # live stage and quick tier at the same time (see below)
#   & tools\check.ps1 -Live -Runtime original   # the original CW3220.DLL (default: the own cw3220.dll)
#   & tools\check.ps1 -Live -Bwcc32 load        # DBSIM loads BWCC32.DLL (default: refuse)
#   & tools\check.ps1 -Live -Suite acceptance-60s,c1_01-success   # only these suites of the registry
#   & tools\check.ps1 -Live -NewFail 46162b:1   # DBSIM's `new` answers NULL once at that site
#   & tools\check.ps1 -Live -OwnShell           # ... plus one unattended mission through the own shell
#                                                             (text menu, then es2shell headless; -OwnShellShell text|gui|both)
#   & tools\check.ps1 -Live -OwnSim             # ... plus the suites and the unattended chain through es2sim
#
# Stages, in this order (each can be left out by its switch; the live stage needs -Live):
#   quick       bazel test //...                                   (-SkipQuick)
#   full        bazel test //tools/reference-game/invitro:differential_full //tools:scripts   (-Full; the full in-vitro
#               tier and the script tests of the PowerShell tools that `bazel test //...` leaves out)
#   signatures  invitro/check_signatures.py with ES2_DATA_ROOT     (-SkipSignatures)
#   audit       scripts/compare_audit.py --hooks-doc hooks.md                         (-SkipAudit)
#   evidence    tools/evidence_budget.py, after the live stage      (-SkipEvidence)
#               read-only report of the es2-data repository (-Es2Data): refuses staged raw tick logs of identical
#               runs, mutation workspaces, listings/code images/pickles/minidumps and oversized files; sizes of logs,
#               .git and .git/lfs; uncited old evidence directories. FAILED only when something forbidden is staged;
#               SKIPPED when -Es2Data does not exist. Never stages, deletes or commits anything.
#   live        prepared proxy (New-Es2PreparedProxy), then run-oracle-parallel.ps1 on the five clones
#               (-InstallName: one to six; the runner starts the suites longest first, results stay in registry order)
#               with every name of proxy/hook-names.txt set to replaced (-HookModes name=mode lines
#               override single names; -RtlConfig sets ES2_RTL_CONFIG for the run), then
#               hook-counters.json (tools/check_counters.py), the nonzero *_skipped/*_bypassed counters,
#               and, if every suite is identical, deletion of the raw <suite>.jsonl tick logs.
#               Runs only when no earlier stage failed (sequential default; see -Overlap).
#               -Suite <name[,name...]>: only these suites of tools/reference-game/oracle-suites.json (the runner's
#               -Suite; the hook counters, the verdict and the own-sim replays then cover these suites only); a name
#               the registry does not have, or a name given twice, is refused. Default: every suite of the registry.
#               The summary names the selection (live details, summary.json `suites`).
#               -HiddenWindow: the game window stays hidden and the cursor untouched
#               (ES2_ORACLE_HIDDEN_WINDOW for the runner); -HiddenWindow:$false forces the visible
#               window; not given, the runner's default holds.
#               -Runtime own|original (default own): with `own` the own cw3220.dll (//rtl:cw3220.dll,
#               built from the clean tree after the proxy, or -RuntimeSource) is deployed on every clone of the run
#               with setup/configure-runtime.ps1 -Apply right before the runner starts and removed again
#               (-Remove) in a `finally` after it, also when the runner, a deployment or the stage fails; the
#               clones' resting state is the original CW3220.DLL. So every regression runs DBSIM without CW3220
#               code. With `original` nothing is deployed (the shim modes of -RtlConfig that work on CW3220's
#               internals need it). Before any stage, configure-runtime.ps1's preview must accept every clone
#               (separate measurement clones only; native-r111 and unexpected runtime files are refused); with
#               `original` a clone that still has the own runtime deployed is refused. A clone found with the own
#               runtime (left over by an interrupted run) is redeployed with this build and restored afterwards.
#               The summary names the runtime (header line, live details, summary.json `runtime`).
#               -Bwcc32 refuse|load (default refuse): `refuse` sets
#               ES2_BWCC32=refuse for the runner, so the proxy answers DBSIM's LoadLibraryA("BWCC32.DLL") (0x465140)
#               with failure (proxy/bwcc32_switch.h; no measured effect, the 40 suites stay identical); `load`
#               clears ES2_BWCC32 for the run, so DBSIM loads the DLL.
#               -NewFail <spec> (default off): <site>:<n> or any:<n> (site: hexadecimal call site of DBSIM's
#               0x479cfc, e.g. 46162b; n: decimal 0..1000000), the spec of proxy/new_fail_switch.h. It sets
#               ES2_NEW_FAIL=<spec> and ES2_NEW_FAIL_LOG=<EvidenceRoot>\new-fail-{suite}.ndjson for the oracle runs
#               this script starts (the proxy expands {suite} to the clone's suite: one NDJSON per suite) and
#               restores both afterwards (a value already in the environment is replaced for the run); without
#               -NewFail the script touches neither (an inherited value is passed on and noted in the live details).
#               The n-th counted request of every clone is
#               answered with NULL, so the suites are expected to end in DBSIM's fault, not IDENTICAL: the live
#               result is a record (the NDJSON files, the exit path of the exit probe), not a regression verdict.
#               Refused together with -OwnShell and -OwnSim (their chains would run under the injection).
#               A warning (console, live details, summary header, summary.json `new_fail.warning`) when the site
#               lies in a function whose hook is replaced in this run (proxy/hook_registry.h, the modes of -HookModes):
#               the own code runs there and the request never comes from the site, so nothing is injected; set that
#               hook to original with -HookModes. Also when the site is none of the six call sites of the switch.
#               any:<n> gets no warning.
#   own-shell   (-OwnShell, needs -Live) after the suites, still inside the live stage (same prepared proxy, same
#               hook selection, the clone still carrying the runtime the live stage deployed): one unattended mission
#               through the own shell, setup/start-own-menu.ps1 -Unattended on one clone of the run (-OwnShellInstall,
#               default the first of -InstallName): es2launch -> es2menu (scripted input) -> DBSIM with the oracle
#               settings of c1_01-success -> es2menu -X3. PASSED when the script exits 0 and its evidence\unattended.json
#               says PASSED: launcher events of one mission, the pinned C1_01 launch files, DBSIM's tick log and outputs
#               identical to the c1_01-success golden, chain_real_test with ES2_CHAIN_VERIFY with every preparation equal.
#               Its session (backup, evidence, -RestoreOnly) is <EvidenceRoot>\own-shell, its output own-shell.log.
#               -OwnShellShell text|gui|both (default both) picks the shell of the chain: text (es2menu, the
#               session above), gui (es2shell headless under es2launch, start-own-menu.ps1 -Shell gui; session
#               own-shell-gui, output own-shell-gui.log; its verdict also needs the headless reports X1 exit 2 and X3
#               exit 0 without script errors), both (text, then gui). One stage own-shell with a sub-verdict per shell;
#               PASSED only when every sub-run passed.
#               The live stage's duration includes it; NOT RUN when the live stage stopped before its runner.
#   own-sim     (-OwnSim, needs -Live) after the live stage (its runtime already restored): the own DBSIM
#               process es2sim (//sim:es2sim_window and //sim:sdl_runtime, built from the clean tree into
#               <EvidenceRoot>\own-sim\bin and deleted after the stage, hashes kept) in place of DBSIM.EXE.
#               (a) every suite of the registry (or of -Suite) through sim/tests/replay_suite.py --frames (working directories
#               prepared from -DataRoot's CD, as the clones' files; tick fields, watches, return files and exit code
#               against the golden), -OwnSimJobs at a time (default the clone count), then for each IDENTICAL suite
#               sim/tests/frames_suite.py --capture against the frame references (-OwnSimFramesReference sets
#               ES2_FRAMES_REFERENCE; default frames_known.REFERENCE below es2-data\logs); logs own-sim\replay\<suite>.log
#               and own-sim\frames\<suite>.log. (b) the unattended chain es2shell -> es2launch -> es2sim -> es2shell:
#               setup/start-own-menu.ps1 -Unattended -Shell gui -Sim own on one clone (-OwnSimInstall, default the
#               first of -InstallName; session own-sim-chain, output own-sim-chain.log), es2sim deployed by
#               configure-sim.ps1 and removed by the script's restore. PASSED when every replay is IDENTICAL or
#               SKIP (not all SKIP), every frame compare PASS or SKIP, and the chain's unattended.json says PASSED
#               with exit 0. NOT RUN when the live stage did not run.
#               -OwnCdRoot <dir> hands both chains (own-shell and own-sim (b)) start-own-menu.ps1 -CdRoot, the
#               CD root the own executables read (menu.ini cd_root), e.g. a copy without VER95\DBSIM.EXE and
#               VER95\VSHELL.EXE; the oracle side (replays' working directories, the chain verification) keeps -DataRoot.
#
# -Overlap (needs -Live, not -SkipQuick): the live stage and the quick tier run at the same
# time. The live stage goes first; as soon as it has its prepared proxy (the proxy build needs the
# Bazel server, so it must come first; with -PreparedProxy at once) the quick tier's `bazel test //...`
# starts and runs beside the oracle runner (the clone guard ignores the stand-in dbsim.EXE of
# //shell:chain_real_test). Both Bazel commands use the same output base, so a Bazel command
# of this script waits for the other's server lock; only the oracle run itself overlaps. The
# stages full, signatures and audit then run one after another. Reporting is the same as without
# -Overlap (same stages, same order, same numbers); the quick duration is the wall time from its start.
# "Live runs only when no earlier stage failed" becomes: the live result counts only when no other
# stage failed. If quick, full, signatures or audit FAILED, a live stage that PASSED is re-labelled
# DISCARDED (its numbers and details stay in the summary, it counts neither as passed nor failed);
# a live stage that FAILED stays FAILED. The exit status is then 1 through the stage that failed.
#
# Evidence: -EvidenceRoot (default <es2-data>\logs\check-<utc>) must not exist. It receives
# summary.md (and summary.json), one log per stage, and for the live stage live\ (the runner's output),
# hook-counters.json, hooks-argument.txt and copies of the -HookModes and -RtlConfig files. The own runtime
# built for the run (runtime\cw3220.dll) is deleted after the restore; its hash stays in the summary.
#
# Exit status: 0 only if every executed stage passed; 1 if a stage failed; 2 if the call was refused
# (existing evidence directory, uncommitted changes under the source-stamp paths with -Live (and rtl/ with
# -Runtime own), bad parameters, a clone that configure-runtime.ps1's preview refuses) or no stage was selected. A live suite whose runner.log shows the clone guard refusing a
# running game (a process of an installation under Get-CloneInstallRoot, or one whose path cannot be
# read; never the stand-in of //shell:chain_real_test) is reported as
# "refused, rerun", not as DIFFERENT. Never run two live sessions at once.
#
# The path parameters below the first block exist for the test (tools/test_check.py).
[CmdletBinding()]
param(
    [switch]$Full,
    [switch]$Live,
    [switch]$SkipQuick,
    [switch]$SkipSignatures,
    [switch]$SkipAudit,
    [switch]$SkipEvidence,
    [switch]$Overlap,
    [string]$EvidenceRoot = '',
    # Live stage.
    [string[]]$InstallName = @('timing-r111', 'timing-r111-p1', 'timing-r111-p2', 'timing-r111-p4', 'timing-r111-p5'),
    [string[]]$Suite = @(),
    [string]$PreparedProxy = '',
    [string]$HookModes = '',
    [string]$RtlConfig = '',
    [string]$OracleDataRoot = '',
    [switch]$HiddenWindow,
    [string]$Runtime = 'own',
    [string]$RuntimeSource = '',
    [string]$Bwcc32 = 'refuse',
    # ES2_NEW_FAIL=<spec> and ES2_NEW_FAIL_LOG=<EvidenceRoot>/new-fail-{suite}.ndjson for the oracle runs.
    [string]$NewFail = '',
    # The unattended own-shell chain after the suites (see "own-shell" above).
    [switch]$OwnShell,
    [string]$OwnShellInstall = '',
    # The shell(s) of the own-shell chain: text, gui or both (text first).
    [string]$OwnShellShell = 'both',
    # The own DBSIM process es2sim after the live stage (see "own-sim" above).
    [switch]$OwnSim,
    [string]$OwnSimInstall = '',
    [int]$OwnSimJobs = 0,
    [string]$OwnSimFramesReference = '',
    # The CD root of the own executables in the own-shell and own-sim chains (start-own-menu.ps1 -CdRoot),
    # e.g. a copy without VER95\DBSIM.EXE and VER95\VSHELL.EXE; default the chains' extracted CD.
    [string]$OwnCdRoot = '',
    # Tools and locations.
    [string]$Bazel = '',
    [string]$BazelOutputBase = '',
    [string]$BazelContentsCache = '',
    [string[]]$BazelArgs = @(),
    [string]$Python = '',
    [string]$DataRoot = '',
    [string]$Es2Data = '',
    [string]$Repo = '',
    [string]$LiveRunner = '',
    [string]$SetupModule = '',
    [string]$RuntimeScript = '',
    [string]$OwnShellScript = '',
    [string]$OwnSimReplayScript = '',
    [string]$OwnSimFramesScript = ''
)
$ErrorActionPreference = 'Stop'
$hiddenWindowGiven = $PSBoundParameters.ContainsKey('HiddenWindow')
if (!$Repo) { $Repo = Split-Path -Parent $PSScriptRoot }
$Repo = [IO.Path]::GetFullPath($Repo)
if (!$Bazel) { $Bazel = if ($env:ES2_BAZEL) { $env:ES2_BAZEL } else { 'bazel' } }
if (!$Python -or !$DataRoot -or !$Es2Data -or !$EvidenceRoot) {
    Import-Module (Join-Path $PSScriptRoot 'reference-game\setup\CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$Python) { $Python = if (Test-Path -LiteralPath $es2.Python) { $es2.Python } else { 'python' } }
    if (!$DataRoot) { $DataRoot = if ($env:ES2_DATA_ROOT) { $env:ES2_DATA_ROOT } else { Join-Path $es2.Extracted 'cd' } }
    if (!$Es2Data) { $Es2Data = $es2.DataRepository }
    if (!$EvidenceRoot) { $EvidenceRoot = Join-Path $es2.DataRepository ('logs\check-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')) }
}
if (!$LiveRunner) { $LiveRunner = Join-Path $Repo 'tools\reference-game\setup\run-oracle-parallel.ps1' }
if (!$SetupModule) { $SetupModule = Join-Path $Repo 'tools\reference-game\setup\Es2Bazel.psm1' }
if (!$RuntimeScript) { $RuntimeScript = Join-Path $Repo 'tools\reference-game\setup\configure-runtime.ps1' }
if (!$OwnShellScript) { $OwnShellScript = Join-Path $Repo 'tools\reference-game\setup\start-own-menu.ps1' }
if (!$OwnSimReplayScript) { $OwnSimReplayScript = Join-Path $Repo 'sim\tests\replay_suite.py' }
if (!$OwnSimFramesScript) { $OwnSimFramesScript = Join-Path $Repo 'sim\tests\frames_suite.py' }
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$Started = [DateTime]::UtcNow

# Paths whose content is part of the prepared proxy's source stamp (Get-Es2SourceStamp in Es2Bazel.psm1).
$StampPaths = @('core', 'hook', 'media', 'bazel/defs', 'bazel/toolchains', 'tools/reference-game/proxy', 'tools/reference-game/setup',
    'tools/reference-game/scripts', 'tools/reference-game/invitro/branch_sites.h', 'MODULE.bazel', 'MODULE.bazel.lock', '.bazelrc')
$RefusedPatterns = @('Close the game in this clone', 'is in use by another session', 'leaves it alone', 'Nothing was deployed or started',
    'outside the permitted', 'It is left alone')
$HookModeNames = @('original', 'replaced', 'compare')

function Stop-Refused([string]$Message) {
    [Console]::Error.WriteLine("check.ps1: refused: $Message")
    exit 2
}

# ---- Refusals before anything is created ------------------------------------------------------
if (Test-Path -LiteralPath $EvidenceRoot) { Stop-Refused "evidence directory exists: $EvidenceRoot" }
if (!$Live) {
    foreach ($name in @('Suite', 'PreparedProxy', 'HookModes', 'RtlConfig', 'OracleDataRoot', 'Runtime', 'RuntimeSource', 'Bwcc32', 'NewFail', 'OwnShell', 'OwnShellInstall', 'OwnShellShell',
                       'OwnSim', 'OwnSimInstall', 'OwnSimJobs', 'OwnSimFramesReference', 'OwnCdRoot')) {
        if ($PSBoundParameters.ContainsKey($name)) { Stop-Refused "-$name is a parameter of the live stage: add -Live" }
    }
}
$Runtime = "$Runtime".Trim().ToLowerInvariant()
if ($Runtime -notin @('own', 'original')) { Stop-Refused "-Runtime is own or original, not '$Runtime'" }
$Bwcc32 = "$Bwcc32".Trim().ToLowerInvariant()
if ($Bwcc32 -notin @('load', 'refuse')) { Stop-Refused "-Bwcc32 is load or refuse, not '$Bwcc32'" }
if ($PSBoundParameters.ContainsKey('NewFail')) {
    # The spec of proxy/new_fail_switch.h: <hex site>:<n> or any:<n>, n decimal 0..1000000.
    $NewFail = "$NewFail".Trim().ToLowerInvariant()
    if ($NewFail -notmatch '^(any|(0x)?[0-9a-f]{1,8}):([0-9]{1,7})$' -or [int]$Matches[3] -gt 1000000) {
        Stop-Refused "-NewFail is <hex site>:<n> or any:<n> with n 0..1000000 (e.g. 46162b:1), not '$NewFail'"
    }
    if ($OwnShell -or $OwnSim) { Stop-Refused '-NewFail injects a failure into the oracle runs: the own-shell and own-sim chains would run under it, drop -OwnShell/-OwnSim' }
}
if ($RuntimeSource -and $Runtime -ne 'own') { Stop-Refused '-RuntimeSource is the own cw3220.dll: it needs -Runtime own' }
if ($RuntimeSource -and !(Test-Path -LiteralPath $RuntimeSource -PathType Leaf)) { Stop-Refused "own runtime not found: $RuntimeSource" }
if ($RuntimeSource) { $RuntimeSource = [IO.Path]::GetFullPath($RuntimeSource) }
if (!($Live -or $Full -or !$SkipQuick -or !$SkipSignatures -or !$SkipAudit)) { Stop-Refused 'no stage selected' }
if ($Overlap -and !$Live) { Stop-Refused '-Overlap overlaps the live stage with the quick tier: add -Live' }
if ($Overlap -and $SkipQuick) { Stop-Refused '-Overlap overlaps the live stage with the quick tier: it cannot be combined with -SkipQuick' }
if ($Live) {
    # "pwsh -File" cannot bind an array: a comma-separated list arrives as one element.
    $InstallName = @($InstallName | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    # One to six distinct measurement clones (timing-r111, -p1 .. -p5); native-r111 never.
    if ($InstallName.Count -lt 1 -or $InstallName.Count -gt 6) { Stop-Refused "-InstallName takes one to six clones, not $($InstallName.Count)" }
    if (@($InstallName | Select-Object -Unique).Count -ne $InstallName.Count) { Stop-Refused '-InstallName names a clone twice' }
    foreach ($name in $InstallName) {
        if ($name -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]*$' -or $name -ieq 'native-r111') { Stop-Refused "-InstallName: not a measurement clone name: $name" }
    }
    $suiteRegistry = Join-Path $Repo 'tools\reference-game\oracle-suites.json'
    if ($PSBoundParameters.ContainsKey('Suite')) {
        $Suite = @($Suite | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        if (!$Suite.Count) { Stop-Refused '-Suite names no suite' }
        if (@($Suite | Select-Object -Unique).Count -ne $Suite.Count) { Stop-Refused '-Suite names a suite twice' }
        $registered = @()
        try { $registered = @((Get-Content -LiteralPath $suiteRegistry -Raw | ConvertFrom-Json).suites | ForEach-Object { [string]$_.name }) } catch {}
        if (!$registered.Count) { Stop-Refused "suite registry not readable: $suiteRegistry" }
        $unknown = @($Suite | Where-Object { $_ -notin $registered })
        if ($unknown.Count) { Stop-Refused ("-Suite: not in {0}: {1} (registered: {2})" -f $suiteRegistry, ($unknown -join ', '), ($registered -join ', ')) }
    }
    if ($OwnShellInstall -and !$OwnShell) { Stop-Refused '-OwnShellInstall names the clone of the own-shell run: add -OwnShell' }
    if ($PSBoundParameters.ContainsKey('OwnShellShell') -and !$OwnShell) { Stop-Refused '-OwnShellShell picks the shell of the own-shell run: add -OwnShell' }
    $OwnShellShell = "$OwnShellShell".Trim().ToLowerInvariant()
    if ($OwnShellShell -notin @('text', 'gui', 'both')) { Stop-Refused "-OwnShellShell is text, gui or both, not '$OwnShellShell'" }
    if ($OwnShell) {
        # One clone of the run, so that it carries the live stage's runtime and no other session runs on it meanwhile.
        if (!$OwnShellInstall) { $OwnShellInstall = $InstallName[0] }
        if ($OwnShellInstall -notin $InstallName) { Stop-Refused "-OwnShellInstall $OwnShellInstall is not one of the clones of the live stage ($($InstallName -join ', '))" }
        if (!(Test-Path -LiteralPath $OwnShellScript -PathType Leaf)) { Stop-Refused "own-shell script not found: $OwnShellScript" }
    }
    foreach ($name in @('OwnSimInstall', 'OwnSimJobs', 'OwnSimFramesReference')) {
        if ($PSBoundParameters.ContainsKey($name) -and !$OwnSim) { Stop-Refused "-$name belongs to the own-sim stage: add -OwnSim" }
    }
    if ($OwnSim) {
        # The chain runs on one clone of the run (no other session of this script runs on it then).
        if (!$OwnSimInstall) { $OwnSimInstall = $InstallName[0] }
        if ($OwnSimInstall -notin $InstallName) { Stop-Refused "-OwnSimInstall $OwnSimInstall is not one of the clones of the live stage ($($InstallName -join ', '))" }
        if (!$PSBoundParameters.ContainsKey('OwnSimJobs')) { $OwnSimJobs = $InstallName.Count }
        if ($OwnSimJobs -lt 1 -or $OwnSimJobs -gt 16) { Stop-Refused "-OwnSimJobs takes 1 to 16 suites at a time, not $OwnSimJobs" }
        if ($OwnSimFramesReference -and !(Test-Path -LiteralPath $OwnSimFramesReference -PathType Container)) { Stop-Refused "frame reference directory not found: $OwnSimFramesReference" }
        if ($OwnSimFramesReference) { $OwnSimFramesReference = [IO.Path]::GetFullPath($OwnSimFramesReference) }
        foreach ($path in @($OwnShellScript, $OwnSimReplayScript, $OwnSimFramesScript)) {
            if (!(Test-Path -LiteralPath $path -PathType Leaf)) { Stop-Refused "own-sim script not found: $path" }
        }
    }
}
if ($PreparedProxy -and !(Test-Path -LiteralPath $PreparedProxy -PathType Leaf)) { Stop-Refused "prepared proxy manifest not found: $PreparedProxy" }
if ($RtlConfig -and !(Test-Path -LiteralPath $RtlConfig -PathType Leaf)) { Stop-Refused "RTL config not found: $RtlConfig" }
if ($HookModes -and !(Test-Path -LiteralPath $HookModes -PathType Leaf)) { Stop-Refused "hook modes file not found: $HookModes" }

function Invoke-Git([string[]]$GitArguments) {
    $saved = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { $out = & git -C $Repo @GitArguments 2>&1; $code = $LASTEXITCODE } finally { $ErrorActionPreference = $saved }
    return @{ Code = $code; Lines = @($out | ForEach-Object { "$_" }) }
}
$commit = ''
$head = Invoke-Git @('rev-parse', 'HEAD')
if ($head.Code -eq 0 -and $head.Lines.Count) { $commit = $head.Lines[0].Trim() }
$dirtyStamp = @()
# The own runtime is built from rtl/ (and core/) of the same tree, so rtl/ must be clean as well.
if ($Live -and $Runtime -eq 'own') { $StampPaths += 'rtl' }
# es2sim is built from sim/ (with rtl/ and core/) of the same tree.
if ($Live -and $OwnSim) { $StampPaths += @(@('sim', 'rtl') | Where-Object { $_ -notin $StampPaths }) }
if ($Live) {
    # The child runners refuse a stale prepared proxy anyway; refuse here, before any stage ran.
    $status = Invoke-Git (@('status', '--porcelain', '--untracked-files=all', '--') + $StampPaths)
    if ($status.Code -ne 0) { Stop-Refused ("cannot read the git status of {0}: {1}" -f $Repo, ($status.Lines -join ' ')) }
    $dirtyStamp = @($status.Lines | Where-Object { $_.Trim() })
    if ($dirtyStamp.Count) {
        Stop-Refused ("-Live needs a clean tree under the source-stamp paths; uncommitted: {0}" -f (($dirtyStamp | Select-Object -First 8) -join '; '))
    }
}

# Hook arguments of the live stage (checked now so that a bad modes file refuses before the long stages).
$hooksArgument = ''
$modeCounts = @{}
if ($Live) {
    $namesFile = Join-Path $Repo 'tools\reference-game\proxy\hook-names.txt'
    if (!(Test-Path -LiteralPath $namesFile -PathType Leaf)) { Stop-Refused "hook names not found: $namesFile" }
    $names = @(Get-Content -LiteralPath $namesFile | ForEach-Object { $_.Trim() } | Where-Object { $_ -and !$_.StartsWith('#') })
    if (!$names.Count -or @($names | Select-Object -Unique).Count -ne $names.Count) { Stop-Refused "hook-names.txt is empty or has duplicates" }
    $modes = [ordered]@{}
    foreach ($name in $names) { $modes[$name] = 'replaced' }
    if ($HookModes) {
        $number = 0
        foreach ($raw in (Get-Content -LiteralPath $HookModes)) {
            $number++
            $line = $raw.Trim()
            if (!$line -or $line.StartsWith('#')) { continue }
            if ($line -notmatch '^([A-Za-z0-9_]+)\s*=\s*([A-Za-z]+)$') { Stop-Refused "${HookModes}:${number}: expected name=mode" }
            $hookName = $Matches[1]; $mode = $Matches[2].ToLowerInvariant()
            if (!$modes.Contains($hookName)) { Stop-Refused "${HookModes}:${number}: unknown hook '$hookName' (not in hook-names.txt)" }
            if ($mode -notin $HookModeNames) { Stop-Refused "${HookModes}:${number}: mode '$mode' is not original, replaced or compare" }
            $modes[$hookName] = $mode
        }
    }
    $hooksArgument = (($modes.Keys | ForEach-Object { "$_=$($modes[$_])" }) -join ',')
    foreach ($mode in $modes.Values) { $modeCounts[$mode] = 1 + [int]$modeCounts[$mode] }
}

# -NewFail <site>: the switch counts a request only when it comes from the site in the original's machine code. The
# function containing each call site of 0x479cfc (proxy/new_fail_switch.h), by the function index (docs/functions).
$NewFailSiteFunctions = [ordered]@{ '46162b' = 0x461608; '4779e5' = 0x4779d5; '48063f' = 0x4805e0; '480b23' = 0x480af0; '480f70' = 0x480f58; '480fbc' = 0x480fa4 }
$newFailWarning = ''
if ($Live -and $NewFail -and !$NewFail.StartsWith('any:')) {
    $site = [Convert]::ToUInt32(($NewFail.Split(':')[0] -replace '^0x', ''), 16).ToString('x')
    if (!$NewFailSiteFunctions.Contains($site)) {
        $newFailWarning = "-NewFail site 0x$site is not one of the call sites of 0x479cfc ($($NewFailSiteFunctions.Keys -join ', ')): the switch leaves everything as it was, nothing is injected"
    } else {
        $registryFile = Join-Path $Repo 'tools\reference-game\proxy\hook_registry.h'
        if (!(Test-Path -LiteralPath $registryFile -PathType Leaf)) { Stop-Refused "hook registry not found: $registryFile" }
        $function = $NewFailSiteFunctions[$site]
        $replacedHere = @()
        foreach ($line in (Get-Content -LiteralPath $registryFile)) {
            # ES2_HOOK(name, RVA, ...): image base 0x400000.
            if ($line -match '^\s*ES2_HOOK\(\s*([A-Za-z0-9_]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,' -and
                0x400000 + [Convert]::ToUInt32($Matches[2], 16) -eq $function -and $modes[$Matches[1]] -eq 'replaced') {
                $replacedHere += $Matches[1]
            }
        }
        if ($replacedHere.Count) {
            $newFailWarning = ("-NewFail site 0x{0} lies in 0x{1:x} ({2}), replaced in this run: the own code runs there and the request never comes " +
                "from the site, so nothing is injected; set {3} with -HookModes") -f $site, $function, ($replacedHere -join ', '),
                (($replacedHere | ForEach-Object { "$_=original" }) -join ', ')
        }
    }
    if ($newFailWarning) { [Console]::Error.WriteLine("check.ps1: warning: $newFailWarning") }
}

# The runtime of the live stage. configure-runtime.ps1 (in this session) prints one JSON object per call;
# its refusals are exceptions. Its DataRoot is the runner's (-OracleDataRoot, else both scripts' default).
function Invoke-RuntimeScript([string]$Name, [hashtable]$Extra = @{}) {
    $splat = @{ InstallName = $Name }
    foreach ($key in $Extra.Keys) { $splat[$key] = $Extra[$key] }
    if ($OracleDataRoot) { $splat.DataRoot = $OracleDataRoot }
    $global:LASTEXITCODE = 0
    $lines = @(& $RuntimeScript @splat | ForEach-Object { "$_" })
    $text = ($lines -join "`n").Trim()
    $record = $null
    try { $record = $text | ConvertFrom-Json } catch { $record = $null }
    if (!$record -or !$record.PSObject.Properties['state']) { throw "configure-runtime.ps1 gave no state for ${Name}: $text" }
    return $record
}
# Preview of every clone before anything is created: the script refuses anything but a separate measurement
# clone with the expected files; `original` refuses a clone that still runs the own runtime.
$runtimeFound = [ordered]@{}
if ($Live) {
    if (!(Test-Path -LiteralPath $RuntimeScript -PathType Leaf)) { Stop-Refused "runtime script not found: $RuntimeScript" }
    foreach ($name in $InstallName) {
        $preview = $null
        try { $preview = Invoke-RuntimeScript $name } catch { Stop-Refused ("-Runtime ${Runtime}: clone ${name} refused by configure-runtime.ps1: " + $_.Exception.Message) }
        $state = [string]$preview.state
        if ($state -notin @('original', 'own')) { Stop-Refused "-Runtime ${Runtime}: clone $name is in runtime state '$state'" }
        if ($Runtime -eq 'original' -and $state -eq 'own') {
            Stop-Refused ("-Runtime original: clone $name has the own runtime deployed; restore it with " +
                "configure-runtime.ps1 -InstallName $name -Remove or run with -Runtime own")
        }
        $runtimeFound[$name] = $state
    }
}

New-Item -ItemType Directory -Path $EvidenceRoot | Out-Null
$NewFailLog = Join-Path $EvidenceRoot 'new-fail-{suite}.ndjson'   # the proxy expands {suite}
$script:RuntimeSummary = [ordered]@{ runtime = $Runtime; bwcc32 = $Bwcc32; source = ''; sha256 = ''; found = $runtimeFound
    deployed = @(); restored = @(); not_restored = @() }

# ---- Helpers ----------------------------------------------------------------------------------
$Stages = [Collections.Generic.List[object]]::new()
function New-Stage([string]$Name, [string]$Result, [string]$Reason = '') {
    $stage = [ordered]@{ name = $Name; result = $Result; seconds = 0.0; numbers = ''; details = [Collections.Generic.List[string]]::new(); log = '' }
    if ($Reason) { $stage.numbers = $Reason }
    $Stages.Add($stage)
    return $stage
}
function Join-NativeArguments([string[]]$Values) {
    ($Values | ForEach-Object { '"' + [regex]::Replace([regex]::Replace($_, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"' }) -join ' '
}
function Resolve-Executable([string]$Name) {
    if (Test-Path -LiteralPath $Name -PathType Leaf) { return [IO.Path]::GetFullPath($Name) }
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (!$command) { throw "Command not found: $Name" }
    return $command.Source
}
function Write-Text([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Invoke-Native([string]$Exe, [string[]]$Arguments, [string]$Log) {
    return Wait-Native (Start-Native $Exe $Arguments $Log)
}
function Start-Native([string]$Exe, [string[]]$Arguments, [string]$Log) {
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = Resolve-Executable $Exe
    $psi.Arguments = Join-NativeArguments $Arguments
    $psi.WorkingDirectory = $Repo
    $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = [Text.Encoding]::UTF8; $psi.StandardErrorEncoding = [Text.Encoding]::UTF8
    $process = [Diagnostics.Process]::Start($psi)
    $out = $process.StandardOutput.ReadToEndAsync(); $err = $process.StandardError.ReadToEndAsync()
    return @{ Process = $process; Out = $out; Err = $err; Psi = $psi; Log = $Log; Clock = [Diagnostics.Stopwatch]::StartNew() }
}
function Wait-Native($Started) {
    $process = $Started.Process; $out = $Started.Out; $err = $Started.Err; $psi = $Started.Psi; $Log = $Started.Log
    $process.WaitForExit()
    $code = $process.ExitCode; $process.Dispose()
    $text = $out.Result + $(if ($err.Result) { "`n--- stderr ---`n" + $err.Result } else { '' })
    Write-Text $Log ("> {0} {1}`n{2}" -f $psi.FileName, $psi.Arguments, $text)
    return @{ Code = $code; Stdout = $out.Result; Text = $out.Result + "`n" + $err.Result }
}
function Format-Seconds([double]$Seconds) {
    if ($Seconds -ge 60) { return ('{0}m {1:00}s' -f [int][Math]::Floor($Seconds / 60), [int]($Seconds % 60)) }
    return ('{0:0.0}s' -f $Seconds)
}
function Get-BazelStartup { if ($BazelOutputBase) { return @("--output_base=$BazelOutputBase") } else { return @() } }
function Get-BazelOptions {
    $options = @()
    if ($BazelContentsCache) { $options += "--repo_contents_cache=$BazelContentsCache" }
    return $options + $BazelArgs
}
function Get-Head([string]$Path, [int]$Lines = 15, [int]$Width = 300) {
    if (!(Test-Path -LiteralPath $Path)) { return @("($Path is missing)") }
    return @(Get-Content -LiteralPath $Path -TotalCount $Lines | ForEach-Object { if ($_.Length -gt $Width) { $_.Substring(0, $Width) + ' ...' } else { $_ } })
}

# ---- Stages -----------------------------------------------------------------------------------
function Invoke-BazelStage([string]$Name, [string[]]$Targets) {
    Complete-BazelStage $Name (Start-BazelTest $Name $Targets)
}
function Start-BazelTest([string]$Name, [string[]]$Targets) {
    return Start-Native $Bazel (@(Get-BazelStartup) + @("test") + @(Get-BazelOptions) + $Targets) (Join-Path $EvidenceRoot "$Name.log")
}
function Complete-BazelStage([string]$Name, $Started) {
    $stage = New-Stage $Name 'FAILED'
    $stage.log = "$Name.log"
    $run = Wait-Native $Started
    $pass = 0; $fail = @()
    foreach ($line in ($run.Text -split "`r?`n")) {
        if ($line -match '^(//\S+)\s+(?:\(cached\)\s+)?(PASSED|FLAKY|FAILED|TIMEOUT|NO STATUS|INCOMPLETE)\b') {
            if ($Matches[2] -in @('PASSED', 'FLAKY')) { $pass++ } else { $fail += $Matches[1] }
        }
    }
    $executed = ''
    if ($run.Text -match '(?m)^(?:INFO: )?(Executed \d+ out of \d+ tests?: .*)$') { $executed = $Matches[1].Trim() }
    $stage.numbers = "tests passed $pass, failed $($fail.Count)"
    if ($executed) { $stage.details.Add($executed) }
    foreach ($target in $fail) { $stage.details.Add("failed: $target") }
    $stage.exit = $run.Code
    if ($run.Code -eq 0) { $stage.result = 'PASSED' } else { $stage.numbers += "; bazel exit $($run.Code)" }
}
function Invoke-SignaturesStage {
    $stage = New-Stage 'signatures' 'FAILED'
    $stage.log = 'signatures.log'
    $script = Join-Path $Repo 'tools\reference-game\invitro\check_signatures.py'
    $run = Invoke-Native $Python @('-B', $script) (Join-Path $EvidenceRoot $stage.log)
    if ($run.Text -match '(?m)^PASS:\s*(\d+)/(\d+) registry signatures') {
        $stage.numbers = "$($Matches[1]) of $($Matches[2]) registry signatures"
        if ($run.Code -eq 0 -and $Matches[1] -eq $Matches[2]) { $stage.result = 'PASSED' }
    } elseif ($run.Text -match '(?m)^SKIP:') {
        $stage.numbers = 'skipped by the script (ES2_DATA_ROOT)'
    } else {
        $stage.numbers = "no PASS line, exit $($run.Code)"
    }
}
function Invoke-AuditStage {
    $stage = New-Stage 'audit' 'FAILED'
    $stage.log = 'audit.log'
    $script = Join-Path $Repo 'tools\reference-game\scripts\compare_audit.py'
    $run = Invoke-Native $Python @('-B', $script, '--hooks-doc', 'docs/reference-game/hooks.md') (Join-Path $EvidenceRoot $stage.log)
    if ($run.Text -match '(?m)^(\d+) hooks checked, (\d+) problems') {
        $stage.numbers = "$($Matches[1]) hooks checked, $($Matches[2]) problems"
        if ($run.Code -eq 0 -and [int]$Matches[2] -eq 0) { $stage.result = 'PASSED' }
    } else {
        $stage.numbers = "no result line, exit $($run.Code)"
    }
    if ($stage.result -ne 'PASSED') {
        foreach ($line in (Get-Head (Join-Path $EvidenceRoot $stage.log) 12)) { $stage.details.Add($line) }
    }
}

function Invoke-EvidenceStage {
    $stage = New-Stage 'evidence' 'FAILED'
    $stage.log = 'evidence.log'
    if (!(Test-Path -LiteralPath $Es2Data -PathType Container)) {
        $stage.result = 'SKIPPED'; $stage.numbers = "no es2-data at $Es2Data"
        return
    }
    $script = Join-Path $Repo 'tools\evidence_budget.py'
    $run = Invoke-Native $Python @('-B', $script, '--data-root', $Es2Data, '--repo', $Repo) (Join-Path $EvidenceRoot $stage.log)
    $lines = @($run.Text -split "`r?`n" | Where-Object { $_.Trim() })
    $last = if ($lines.Count) { $lines[$lines.Count - 1] } else { '' }
    $stage.numbers = if ($last -match '^(OK|REFUSED): (.*)$') { $Matches[2] } else { "no result line, exit $($run.Code)" }
    if ($run.Code -eq 0) { $stage.result = 'PASSED' }
    foreach ($line in @($lines | Where-Object { $_ -like 'REFUSED *' -or $_ -like 'evidence directories older*' } | Select-Object -First 12)) { $stage.details.Add($line) }
}

function Invoke-LiveStage {
    $stage = New-Stage 'live' 'FAILED'
    $stage.log = 'live.log'
    $liveLog = Join-Path $EvidenceRoot $stage.log
    $liveDir = Join-Path $EvidenceRoot 'live'
    Write-Text $liveLog ''
    Write-Text (Join-Path $EvidenceRoot 'hooks-argument.txt') $hooksArgument
    if ($HookModes) { Copy-Item -LiteralPath $HookModes -Destination (Join-Path $EvidenceRoot 'hook-modes.txt') }
    if ($RtlConfig) { Copy-Item -LiteralPath $RtlConfig -Destination (Join-Path $EvidenceRoot 'rtl-config.ini') }
    $stage.details.Add(("hooks: {0}" -f (($modeCounts.Keys | Sort-Object | ForEach-Object { "$($modeCounts[$_]) $_" }) -join ', ')))
    $stage.details.Add("clones: $($InstallName -join ', ')")
    if ($Suite.Count) { $stage.details.Add("suites: $($Suite -join ', ') (-Suite; not the whole registry)") }
    if ($RtlConfig) { $stage.details.Add("ES2_RTL_CONFIG: $RtlConfig") }
    if ($hiddenWindowGiven) { $stage.details.Add("window: $(if ($HiddenWindow) { 'hidden' } else { 'visible' }) (-HiddenWindow)") }
    if ($Bwcc32 -eq 'refuse') { $stage.details.Add('BWCC32: LoadLibraryA("BWCC32.DLL") answered with failure in DBSIM (-Bwcc32 refuse, the default; ES2_BWCC32=refuse)') }
    else { $stage.details.Add('BWCC32: loaded in DBSIM (-Bwcc32 load; ES2_BWCC32 cleared)') }
    if ($NewFail) {
        $stage.details.Add("ES2_NEW_FAIL=$NewFail (-NewFail; proxy/new_fail_switch.h), ES2_NEW_FAIL_LOG=$NewFailLog")
        if ($newFailWarning) { $stage.details.Add("WARNING: $newFailWarning") }
    } elseif ($env:ES2_NEW_FAIL) {
        $stage.details.Add("ES2_NEW_FAIL=$($env:ES2_NEW_FAIL) is set in the environment (not by -NewFail) and reaches the runner")
    }
    $savedRtl = $env:ES2_RTL_CONFIG
    $savedHidden = $env:ES2_ORACLE_HIDDEN_WINDOW
    $savedBwcc = $env:ES2_BWCC32
    $savedNewFail = $env:ES2_NEW_FAIL
    $savedNewFailLog = $env:ES2_NEW_FAIL_LOG
    $deployed = [Collections.Generic.List[string]]::new()   # clones to restore, in deployment order
    $builtRuntime = ''
    try {
        if ($RtlConfig) { $env:ES2_RTL_CONFIG = [IO.Path]::GetFullPath($RtlConfig) }
        if ($hiddenWindowGiven) { $env:ES2_ORACLE_HIDDEN_WINDOW = if ($HiddenWindow) { '1' } else { '0' } }
        $env:ES2_BWCC32 = if ($Bwcc32 -eq 'refuse') { 'refuse' } else { $null }
        if ($NewFail) {
            $env:ES2_NEW_FAIL = $NewFail
            $env:ES2_NEW_FAIL_LOG = $NewFailLog
        }
        $proxy = $PreparedProxy
        if (!$proxy) {
            # Builds the proxy and the helpers from the clean tree; the module runs the own-code fixture tests first.
            Import-Module $SetupModule -Force
            $buildOutput = @(New-Es2PreparedProxy -Directory (Join-Path $EvidenceRoot 'prepared') *>&1 | ForEach-Object { "$_" })
            Add-Content -LiteralPath $liveLog -Value ($buildOutput -join "`n")
            $proxy = $buildOutput | Select-Object -Last 1
            if (!$proxy -or !(Test-Path -LiteralPath $proxy -PathType Leaf)) { throw "New-Es2PreparedProxy returned no manifest: $proxy" }
        }
        $stage.details.Add("prepared proxy: $proxy")
        if ($Runtime -eq 'own') {
            # The own cw3220.dll on every clone of the run; removed in the finally below.
            $source = $RuntimeSource
            if (!$source) {
                Import-Module $SetupModule -Force
                $source = Copy-Es2BazelOutput -Target '//rtl:cw3220.dll' -Destination (Join-Path $EvidenceRoot 'runtime') | Select-Object -Last 1
                if (!$source -or !(Test-Path -LiteralPath $source -PathType Leaf)) { throw "the own runtime was not built: $source" }
                $builtRuntime = [IO.Path]::GetFullPath($source)
                $source = $builtRuntime
            }
            $script:RuntimeSummary.sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
            $script:RuntimeSummary.source = if ($RuntimeSource) { $RuntimeSource } else { '//rtl:cw3220.dll' }
            $stage.details.Add("runtime: own cw3220.dll $($script:RuntimeSummary.sha256) ($($script:RuntimeSummary.source))")
            foreach ($name in $InstallName) {
                $deployed.Add($name)   # before -Apply: a failed deployment is restored as well
                $record = Invoke-RuntimeScript $name @{ Source = $source; Apply = $true }
                if ([string]$record.state -ne 'own' -or [string]$record.own_sha256 -ne $script:RuntimeSummary.sha256) {
                    throw "configure-runtime.ps1 did not deploy the own runtime on ${name}: state $($record.state)"
                }
                $script:RuntimeSummary.deployed += $name
                $stage.details.Add(("runtime deployed on {0} (found {1})" -f $name, $runtimeFound[$name]))
            }
        } else {
            $stage.details.Add('runtime: original CW3220.DLL (-Runtime original; nothing deployed)')
        }
        if ($script:OverlapStart) { & $script:OverlapStart }  # -Overlap: the quick tier starts beside the oracle run
        $splat = @{ InstallName = $InstallName; PreparedProxy = $proxy; Hooks = $hooksArgument; OutputDirectory = $liveDir }
        if ($OracleDataRoot) { $splat.DataRoot = $OracleDataRoot }
        if ($Suite.Count) { $splat.Suite = $Suite }
        $global:LASTEXITCODE = 0
        $exit = 0
        try {
            $runnerOutput = @(& $LiveRunner @splat *>&1 | ForEach-Object { "$_" })
            $exit = $global:LASTEXITCODE
        } catch {
            $runnerOutput = @("runner failed: $($_.Exception.Message)")
            $exit = 1
        }
        Add-Content -LiteralPath $liveLog -Value ($runnerOutput | ForEach-Object { if ($_.Length -gt 2000) { $_.Substring(0, 2000) + ' ...' } else { $_ } })
        if ($NewFail) {
            # The proxy writes one NDJSON per suite; name what arrived (none means the switch never ran).
            $logs = @(Get-ChildItem -LiteralPath $EvidenceRoot -Filter 'new-fail-*.ndjson' -File -ErrorAction SilentlyContinue | Sort-Object Name)
            $logText = if ($logs.Count) { ($logs | ForEach-Object { "$($_.Name) ($($_.Length) bytes)" }) -join ', ' } else { 'none written' }
            $stage.details.Add("new fail logs: $logText")
        }
        # The unattended own-shell chain, with the proxy, hooks and runtime of the suites (its stage follows the live one).
        if ($OwnShell) { Invoke-OwnShellRun $proxy }
        if (!$PreparedProxy) {
            # The run is over: keep the prepared proxy's manifest as evidence, not the built binaries
            # (es2-data commits small files only).
            $built = @(Get-ChildItem -LiteralPath (Join-Path $EvidenceRoot 'prepared') -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Extension -in '.dll', '.exe' })
            foreach ($file in $built) { $file.IsReadOnly = $false; Remove-Item -LiteralPath $file.FullName -Force }
            if ($built.Count) { $stage.details.Add("removed $($built.Count) prepared binaries after the run (manifest kept)") }
        }
        $stage.exit = $exit
    } finally {
        # Back to the original CW3220.DLL on every clone this stage touched, whatever happened above.
        foreach ($name in $deployed) {
            try {
                # A deployment that was refused (clone lock, running game) left the original: nothing to remove.
                $record = Invoke-RuntimeScript $name
                if ([string]$record.state -ne 'original') { $record = Invoke-RuntimeScript $name @{ Remove = $true } }
                if ([string]$record.state -ne 'original') { throw "state $($record.state) after -Remove" }
                $script:RuntimeSummary.restored += $name
            } catch {
                $script:RuntimeSummary.not_restored += $name
                $stage.details.Add("runtime NOT restored on ${name}: $($_.Exception.Message)")
            }
        }
        if ($deployed.Count) {
            $stage.details.Add(("runtime restored to the original CW3220.DLL on {0} of {1} clone(s)" -f @($script:RuntimeSummary.restored).Count, $deployed.Count))
        }
        if ($builtRuntime -and (Test-Path -LiteralPath $builtRuntime)) {
            # es2-data keeps small files only: the built DLL goes, its hash stays in the summary.
            $file = Get-Item -LiteralPath $builtRuntime
            $file.IsReadOnly = $false
            Remove-Item -LiteralPath $builtRuntime -Force
        }
        $env:ES2_RTL_CONFIG = $savedRtl
        $env:ES2_ORACLE_HIDDEN_WINDOW = $savedHidden
        $env:ES2_BWCC32 = $savedBwcc
        $env:ES2_NEW_FAIL = $savedNewFail
        $env:ES2_NEW_FAIL_LOG = $savedNewFailLog
    }

    # Per-suite results.
    $rows = @()
    $summaryPath = Join-Path $liveDir 'summary.json'
    $summary = $null
    if (Test-Path -LiteralPath $summaryPath) { try { $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json } catch {} }
    if (!$summary -or !@($summary.results).Count) {
        $stage.numbers = "runner exit $exit, no readable summary.json"
        foreach ($line in (Get-Head $liveLog 12)) { $stage.details.Add($line) }
        return
    }
    if ($summary.hidden_window) { $stage.details.Add('hidden window: yes (runner summary)') }
    foreach ($entry in @($summary.results)) {
        $suiteName = [string]$entry.suite
        $runnerLog = Join-Path (Join-Path $liveDir $suiteName) 'runner.log'
        $label = [string]$entry.result
        $refusedLine = ''
        if ($label -ne 'identical') {
            $label = 'DIFFERENT'
            if (Test-Path -LiteralPath $runnerLog) {
                foreach ($line in (Get-Content -LiteralPath $runnerLog)) {
                    if (@($RefusedPatterns | Where-Object { $line.Contains($_) }).Count) { $refusedLine = $line; break }
                }
            }
            if ($refusedLine) { $label = 'refused, rerun' }
        }
        $rows += [pscustomobject]@{ Suite = $suiteName; Label = $label; Log = $runnerLog; RefusedLine = $refusedLine }
    }
    $identical = @($rows | Where-Object { $_.Label -eq 'identical' })
    $refused = @($rows | Where-Object { $_.Label -eq 'refused, rerun' })
    $different = @($rows | Where-Object { $_.Label -eq 'DIFFERENT' })
    $stage.numbers = "suites identical $($identical.Count) of $($rows.Count)"
    if ($refused.Count) { $stage.numbers += ", refused (rerun) $($refused.Count)" }
    if ($different.Count) { $stage.numbers += ", DIFFERENT $($different.Count)" }
    foreach ($row in $rows) { $stage.details.Add(("suite {0}: {1}" -f $row.Suite, $row.Label)) }
    # The exit code of each suite's own DBSIM against the golden's (run-oracle-regression.ps1). A difference
    # already made the suite DIFFERENT; 'no golden exit code' = the golden predates the field (reported only).
    $exitRows = @($summary.results | Where-Object { $_.PSObject.Properties['exit_code_state'] })
    if ($exitRows.Count) {
        foreach ($entry in $exitRows) {
            $codeText = if ($null -ne $entry.exit_code) { " $($entry.exit_code)" } else { '' }
            $checkText = if ($entry.PSObject.Properties['exit_code_check']) { [string]$entry.exit_code_check } else { 'not compared' }
            $stage.details.Add(("exit code {0}: {1}{2} ({3})" -f $entry.suite, $entry.exit_code_state, $codeText, $checkText))
        }
        $exitSame = @($exitRows | Where-Object { $_.exit_code_check -eq 'same' }).Count
        $exitNoGolden = @($exitRows | Where-Object { $_.exit_code_check -eq 'no golden exit code' }).Count
        $exitDifferent = @($exitRows | Where-Object { $_.exit_code_check -eq 'different' }).Count
        $stage.numbers += "; exit code same $exitSame, no golden exit code $exitNoGolden"
        if ($exitDifferent) { $stage.numbers += ", DIFFERENT $exitDifferent" }
    }
    # A compare-mode hook leaves a zero-byte mismatch log (<suite>.jsonl.hooks.jsonl) when nothing mismatched;
    # the runner already removes it, this removes any that remain. Logs with content stay.
    $emptyHookLogs = @(Get-ChildItem -LiteralPath $liveDir -Recurse -File -Filter '*.hooks.jsonl' -ErrorAction SilentlyContinue | Where-Object { $_.Length -eq 0 })
    foreach ($file in $emptyHookLogs) { Remove-Item -LiteralPath $file.FullName -Force }
    if ($emptyHookLogs.Count) { $stage.details.Add("removed $($emptyHookLogs.Count) empty hook mismatch log(s)") }

    # Hook counters of every suite that left a run directory.
    $counterSuites = @($rows | Where-Object { Test-Path -LiteralPath (Join-Path (Join-Path (Join-Path $liveDir $_.Suite) 'run') "$($_.Suite)-comparison.json") } | ForEach-Object { $_.Suite })
    $countersOk = $false
    $nonzeroTotal = 0
    if ($counterSuites.Count) {
        $helper = Join-Path $PSScriptRoot 'check_counters.py'
        $countersPath = Join-Path $EvidenceRoot 'hook-counters.json'
        $run = Invoke-Native $Python @('-B', $helper, $liveDir, '--output', $countersPath, '--suites', ($counterSuites -join ',')) (Join-Path $EvidenceRoot 'counters.log')
        $report = $null
        try { $report = $run.Stdout | ConvertFrom-Json } catch {}
        if ($run.Code -eq 0 -and $report -and (Test-Path -LiteralPath $countersPath)) { $countersOk = $true }
        if ($report) {
            foreach ($property in $report.suites.PSObject.Properties) {
                $info = $property.Value
                $exitText = if ($info.has_exit_counters) { 'exit record' } else { 'no exit record' }
                $stage.details.Add(("counters {0}: {1} ticks, {2}" -f $property.Name, $info.ticks, $exitText))
                foreach ($counter in $info.nonzero.PSObject.Properties) {
                    $nonzeroTotal++
                    $stage.details.Add(("NONZERO {0}: {1} = {2}" -f $property.Name, $counter.Name, $counter.Value))
                }
                foreach ($counter in $info.mismatches.PSObject.Properties) {
                    $nonzeroTotal++
                    $stage.details.Add(("NONZERO {0}: {1} = {2}" -f $property.Name, $counter.Name, $counter.Value))
                }
                foreach ($problem in @($info.problems)) { $stage.details.Add(("counter problem {0}: {1}" -f $property.Name, $problem)) }
            }
        } else {
            $stage.details.Add("counter extraction failed (exit $($run.Code)); see counters.log")
        }
    } else {
        $stage.details.Add('no suite left a comparison; no hook-counters.json')
    }
    $stage.numbers += "; nonzero skipped/bypassed/mismatch counters $nonzeroTotal"

    # Evidence policy: raw tick logs go only when every suite is identical and the counters are safe.
    $allIdentical = ($identical.Count -eq $rows.Count)
    if ($allIdentical -and $countersOk) {
        $freed = 0; $removed = 0
        foreach ($row in $rows) {
            $jsonl = Join-Path (Join-Path (Join-Path $liveDir $row.Suite) 'run') "$($row.Suite).jsonl"
            if (Test-Path -LiteralPath $jsonl -PathType Leaf) {
                $freed += (Get-Item -LiteralPath $jsonl).Length
                Remove-Item -LiteralPath $jsonl -Force
                $removed++
            }
        }
        $stage.details.Add("deleted $removed raw tick log(s), $freed bytes (all suites identical)")
    } else {
        $stage.details.Add('raw tick logs kept')
    }
    foreach ($row in @($rows | Where-Object { $_.Label -ne 'identical' })) {
        $stage.details.Add("--- first lines of $($row.Suite)\runner.log ---")
        if ($row.RefusedLine) {
            $stage.details.Add(("refusal: " + $(if ($row.RefusedLine.Length -gt 400) { $row.RefusedLine.Substring(0, 400) + ' ...' } else { $row.RefusedLine })))
        }
        foreach ($line in (Get-Head $row.Log 15)) { $stage.details.Add($line) }
    }
    $stage.numbers += "; runtime $Runtime"
    $restoreFailed = @($script:RuntimeSummary.not_restored).Count
    if ($restoreFailed) { $stage.numbers += "; runtime NOT restored on $restoreFailed clone(s)" }
    if ($exit -eq 0 -and $allIdentical -and $countersOk -and !$restoreFailed) { $stage.result = 'PASSED' }
    elseif ($allIdentical -and !$countersOk) { $stage.numbers += '; hook counters not extracted' }
}

$script:OwnShellResult = $null
function Invoke-OwnShellRun([string]$Proxy) {
    # start-own-menu.ps1 -Unattended in a PowerShell process of its own per shell (it ends with exit codes; no window).
    # text keeps the session own-shell and own-shell.log; gui runs in own-shell-gui with own-shell-gui.log.
    $shells = if ($OwnShellShell -eq 'both') { @('text', 'gui') } else { @($OwnShellShell) }
    $script:OwnShellResult = New-Object System.Collections.Generic.List[object]
    foreach ($shell in $shells) {
        $suffix = if ($shell -eq 'text') { '' } else { "-$shell" }
        $session = Join-Path $EvidenceRoot "own-shell$suffix"
        $arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $OwnShellScript, '-Unattended', '-Shell', $shell,
            '-InstallName', $OwnShellInstall, '-PreparedProxy', $Proxy, '-SessionDirectory', $session)
        if ($hooksArgument) { $arguments += @('-Hooks', $hooksArgument) }
        if ($OracleDataRoot) { $arguments += @('-DataRoot', $OracleDataRoot) }
        if ($OwnCdRoot) { $arguments += @('-CdRoot', [IO.Path]::GetFullPath($OwnCdRoot)) }
        if ($BazelOutputBase) { $arguments += @('-BazelOutputBase', $BazelOutputBase) }
        if (Test-Path -LiteralPath $Python -PathType Leaf) { $arguments += @('-Python', [IO.Path]::GetFullPath($Python)) }
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $run = Invoke-Native ((Get-Process -Id $PID).Path) $arguments (Join-Path $EvidenceRoot "own-shell$suffix.log")
        $script:OwnShellResult.Add(@{ Shell = $shell; Log = "own-shell$suffix.log"; Code = $run.Code; Seconds = $clock.Elapsed.TotalSeconds; Session = $session; Text = $run.Text })
    }
}
function Complete-OwnShellStage {
    $stage = New-Stage 'own-shell' 'FAILED'
    $stage.log = 'own-shell.log'
    $results = $script:OwnShellResult
    if (!$results -or !$results.Count) {
        $stage.result = 'NOT RUN'; $stage.numbers = 'the live stage ended before its runner'
        Write-Host ("[{0}] {1} {2}" -f $stage.name, $stage.result, $stage.numbers)
        return
    }
    $runtimeText = if ($Runtime -eq 'own') { 'own cw3220.dll (deployed by the live stage)' } else { 'original CW3220.DLL' }
    $stage.log = (@($results | ForEach-Object { $_.Log }) -join ', ')
    $stage.seconds = 0
    $parts = New-Object System.Collections.Generic.List[string]
    $allPassed = $true
    foreach ($r in $results) {
        # One sub-verdict per shell, each judged like a single-shell run.
        $stage.seconds += $r.Seconds
        $tag = "$($r.Shell): "
        $stage.details.Add("${tag}clone: $OwnShellInstall; suite c1_01-success; runtime: $runtimeText; session: $($r.Session)")
        $sub = 'FAILED'
        $verdict = $null
        $path = Join-Path $r.Session 'evidence\unattended.json'
        if (Test-Path -LiteralPath $path -PathType Leaf) { try { $verdict = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json } catch { $verdict = $null } }
        if (!$verdict) {
            $numbers = "start-own-menu.ps1 exit $($r.Code), no unattended.json"
            foreach ($line in @(($r.Text -split "`r?`n") | Where-Object { $_.Trim() } | Select-Object -Last 12)) { $stage.details.Add("$tag$line") }
        } else {
            $cv = $verdict.chain_verify
            $verifyText = if ($cv -is [string]) { "chain verify $cv" } elseif ($cv -and $null -ne $cv.results_steps) {
                "chain verify $($cv.preparations_equal) of $($cv.preparations) preparations equal, $($cv.results_steps) results step(s)" } else { 'chain verify: no result' }
            $numbers = "DBSIM $($verdict.dbsim) to the c1_01-success golden; $verifyText; launcher sequence $($verdict.launcher_sequence); launch files $($verdict.launch_files)"
            if ($r.Shell -eq 'gui') {
                $sr = $verdict.PSObject.Properties['shell_reports']
                $numbers += if ($sr -and $sr.Value) { "; headless X1 exit $($sr.Value.x1.exit), X3 exit $($sr.Value.x3.exit), $([int]$sr.Value.x1.errors + [int]$sr.Value.x3.errors) script error(s)" } else { '; no headless reports' }
            }
            if ($verdict.timed_out) { $numbers += '; TIMED OUT' }
            if ($verdict.comparison) {
                $c = $verdict.comparison
                $stage.details.Add("${tag}comparison: common ticks $($c.common_ticks) (golden $($c.golden_ticks), run $($c.run_ticks)), watch changes $($c.run_watch_changes) (golden $($c.golden_watch_changes))")
            }
            foreach ($p in @($verdict.problems)) { if ($p) { $stage.details.Add("${tag}problem: $p") } }
            if ($r.Code -eq 0 -and $verdict.verdict -eq 'PASSED') { $sub = 'PASSED' }
            else { $numbers += "; start-own-menu.ps1 exit $($r.Code)" }
        }
        if ($r.Code -eq 2) { $stage.details.Add("${tag}restore deferred: a game process still ran; start-own-menu.ps1 -RestoreOnly $($r.Session)") }
        if ($sub -ne 'PASSED') { $allPassed = $false }
        $parts.Add("$($r.Shell) ${sub}: $numbers")
    }
    $stage.numbers = $parts -join ' / '
    if ($allPassed) { $stage.result = 'PASSED' }
    Write-Host ("[{0}] {1} ({2}) {3}" -f $stage.name, $stage.result, (Format-Seconds $stage.seconds), $stage.numbers)
}

# ---- The own DBSIM process (es2sim) -------------------------------------------------------------
function Get-DriverWord([string]$Text, [string]$Prefix) {
    # The first word after "<prefix>" on the last line that starts with it (replay_suite.py, frames_suite.py).
    $line = @(($Text -split "`r?`n") | Where-Object { $_.StartsWith($Prefix) }) | Select-Object -Last 1
    if (!$line) { return '' }
    return @($line.Substring($Prefix.Length).Trim() -split '\s+')[0]
}
function Format-Counts($Words) {
    $groups = @($Words | Group-Object | Sort-Object Name)
    return (($groups | ForEach-Object { "$($_.Name) $($_.Count)" }) -join ', ')
}
function Invoke-OwnSimStage {
    $stage = New-Stage 'own-sim' 'FAILED'
    $stage.log = 'own-sim-chain.log'
    $root = Join-Path $EvidenceRoot 'own-sim'
    $bin = Join-Path $root 'bin'
    $replayDir = Join-Path $root 'replay'
    $replayLogs = Join-Path $root 'replay-logs'
    $framesLogs = Join-Path $root 'frames-logs'
    foreach ($dir in @($root, $replayDir, $replayLogs, $framesLogs)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $registry = Join-Path $Repo 'tools\reference-game\oracle-suites.json'
    $oracleScript = Join-Path $Repo 'tools\reference-game\scripts\oracle_regression.py'
    $framesTool = Join-Path $Repo 'tools\reference-game\scripts\dbsim_frames.py'
    $savedFrames = $env:ES2_FRAMES_REFERENCE
    $savedOracle = $env:ES2_ORACLE_DIR
    $built = @()
    try {
        if ($OwnSimFramesReference) { $env:ES2_FRAMES_REFERENCE = $OwnSimFramesReference }
        if ($OracleDataRoot) { $env:ES2_ORACLE_DIR = Join-Path ([IO.Path]::GetFullPath($OracleDataRoot)) 'oracle' }
        # es2sim and SDL3.dll side by side (the window build runs headless with --headless; it imports SDL3).
        Import-Module $SetupModule -Force
        $sim = Copy-Es2BazelOutput -Target '//sim:es2sim_window' -Destination $bin | Select-Object -Last 1
        $sdl = Copy-Es2BazelOutput -Target '//sim:sdl_runtime' -Destination $bin | Select-Object -Last 1
        foreach ($file in @($sim, $sdl)) {
            if (!$file -or !(Test-Path -LiteralPath $file -PathType Leaf)) { throw "es2sim was not built: $file" }
            $built += [IO.Path]::GetFullPath($file)
        }
        $sim, $sdl = $built
        $stage.details.Add(("es2sim: {0} {1} (//sim:es2sim_window); SDL3.dll {2} (//sim:sdl_runtime)" -f
            [IO.Path]::GetFileName($sim), (Get-FileHash -LiteralPath $sim -Algorithm SHA256).Hash, (Get-FileHash -LiteralPath $sdl -Algorithm SHA256).Hash))
        $frameBase = if ($env:ES2_FRAMES_REFERENCE) { $env:ES2_FRAMES_REFERENCE } else { 'frames_known.REFERENCE below es2-data\logs' }
        $stage.details.Add("suites: $OwnSimJobs at a time; working directories from the CD of $DataRoot; frame references: $frameBase")

        # (a) every suite of the registry (or of -Suite) through replay_suite.py --frames, then frames_suite.py on the capture.
        $suites = @((Get-Content -LiteralPath $registry -Raw | ConvertFrom-Json).suites | ForEach-Object { [string]$_.name } |
            Where-Object { !$Suite.Count -or $_ -in $Suite })
        $pending = [Collections.Generic.Queue[string]]::new([string[]]$suites)
        $running = [Collections.Generic.List[object]]::new()
        $results = @{}
        while ($pending.Count -or $running.Count) {
            while ($pending.Count -and $running.Count -lt $OwnSimJobs) {
                $name = $pending.Dequeue()
                $arguments = @('-B', $OwnSimReplayScript, '--sim', $sim, '--registry', $registry, '--oracle-script', $oracleScript,
                    '--suite', $name, '--frames', '--output-dir', $replayDir)
                $running.Add(@{ Name = $name; Run = (Start-Native $Python $arguments (Join-Path $replayLogs "$name.log")) })
            }
            $done = $null
            while (!$done) {
                foreach ($entry in $running) { if ($entry.Run.Process.HasExited) { $done = $entry; break } }
                if (!$done) { Start-Sleep -Milliseconds 200 }
            }
            [void]$running.Remove($done)
            $name = $done.Name
            $replay = Wait-Native $done.Run
            $word = Get-DriverWord $replay.Stdout "replay ${name}:"
            if (!$word) { $word = "NO-RESULT (exit $($replay.Code))" }
            $frames = 'not compared'
            $capture = Join-Path (Join-Path $replayDir $name) 'frames.ndjson'
            if ($word -eq 'IDENTICAL') {
                if (Test-Path -LiteralPath $capture -PathType Leaf) {
                    $compare = Invoke-Native $Python @('-B', $OwnSimFramesScript, '--frames-script', $framesTool, '--suite', $name, '--capture', $capture) (Join-Path $framesLogs "$name.log")
                    $frames = Get-DriverWord $compare.Stdout "frames ${name}:"
                    if (!$frames) { $frames = "NO-RESULT (exit $($compare.Code))" }
                    if ($frames -ne 'PASS' -and $frames -ne 'SKIP') {
                        foreach ($line in @(($compare.Stdout -split "`r?`n") | Where-Object { $_.Trim() } | Select-Object -Last 6)) { $stage.details.Add("frames ${name}: $line") }
                    }
                } else {
                    $frames = 'no capture'
                }
            } elseif ($word -ne 'SKIP') {
                foreach ($line in @(($replay.Text -split "`r?`n") | Where-Object { $_.Trim() } | Select-Object -Last 4)) { $stage.details.Add("replay ${name}: $line") }
            }
            if ($word -eq 'IDENTICAL' -and $frames -in @('PASS', 'SKIP')) {
                # es2-data keeps small files only: a passing suite keeps its result.json, INI and outputs, not the raw logs.
                foreach ($raw in @((Join-Path (Join-Path $replayDir $name) 'tick-log.jsonl'), $capture)) {
                    if (Test-Path -LiteralPath $raw -PathType Leaf) { Remove-Item -LiteralPath $raw -Force }
                }
            }
            $results[$name] = @{ Replay = $word; Frames = $frames }
        }
        $replayWords = @($suites | ForEach-Object { $results[$_].Replay })
        $frameWords = @($suites | Where-Object { $results[$_].Replay -eq 'IDENTICAL' } | ForEach-Object { $results[$_].Frames })
        $replayOk = $suites.Count -and !@($replayWords | Where-Object { $_ -notin @('IDENTICAL', 'SKIP') }).Count -and @($replayWords | Where-Object { $_ -eq 'IDENTICAL' }).Count
        $framesOk = !@($frameWords | Where-Object { $_ -notin @('PASS', 'SKIP') }).Count
        $numbers = "replay of $($suites.Count) suites: $(Format-Counts $replayWords)"
        $numbers += if ($frameWords.Count) { "; frames: $(Format-Counts $frameWords)" } else { '; frames: none compared' }
        if (!@($replayWords | Where-Object { $_ -eq 'IDENTICAL' }).Count) { $numbers += '; no suite ran' }

        # (b) the unattended chain es2shell -> es2launch -> es2sim -> es2shell on one clone.
        $session = Join-Path $EvidenceRoot 'own-sim-chain'
        $arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $OwnShellScript, '-Unattended', '-Shell', 'gui', '-Sim', 'own',
            '-InstallName', $OwnSimInstall, '-SessionDirectory', $session, '-SimExe', $sim, '-SimSdl', $sdl)
        if ($OracleDataRoot) { $arguments += @('-DataRoot', $OracleDataRoot) }
        if ($OwnCdRoot) { $arguments += @('-CdRoot', [IO.Path]::GetFullPath($OwnCdRoot)) }
        if ($BazelOutputBase) { $arguments += @('-BazelOutputBase', $BazelOutputBase) }
        if (Test-Path -LiteralPath $Python -PathType Leaf) { $arguments += @('-Python', [IO.Path]::GetFullPath($Python)) }
        $run = Invoke-Native ((Get-Process -Id $PID).Path) $arguments (Join-Path $EvidenceRoot $stage.log)
        $stage.details.Add("chain: clone $OwnSimInstall; suite c1_01-success; session: $session")
        if ($OwnCdRoot) { $stage.details.Add("chain CD root of the own executables: $([IO.Path]::GetFullPath($OwnCdRoot))") }
        $verdict = $null
        $path = Join-Path $session 'evidence\unattended.json'
        if (Test-Path -LiteralPath $path -PathType Leaf) { try { $verdict = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json } catch { $verdict = $null } }
        $chainOk = $false
        if (!$verdict) {
            $chainText = "start-own-menu.ps1 exit $($run.Code), no unattended.json"
            foreach ($line in @(($run.Text -split "`r?`n") | Where-Object { $_.Trim() } | Select-Object -Last 12)) { $stage.details.Add("chain: $line") }
        } else {
            $chainText = "DBSIM (es2sim) $($verdict.dbsim) to the c1_01-success golden; launcher sequence $($verdict.launcher_sequence); launch files $($verdict.launch_files)"
            $sr = $verdict.PSObject.Properties['shell_reports']
            $chainText += if ($sr -and $sr.Value) { "; headless X1 exit $($sr.Value.x1.exit), X3 exit $($sr.Value.x3.exit)" } else { '; no headless reports' }
            if ($verdict.timed_out) { $chainText += '; TIMED OUT' }
            foreach ($p in @($verdict.problems)) { if ($p) { $stage.details.Add("chain: problem: $p") } }
            if ([string]$verdict.sim -ne 'own') { $stage.details.Add("chain: problem: the verdict names sim '$($verdict.sim)', not own") }
            if ($run.Code -eq 0 -and $verdict.verdict -eq 'PASSED' -and [string]$verdict.sim -eq 'own') { $chainOk = $true }
            else { $chainText += "; start-own-menu.ps1 exit $($run.Code)" }
        }
        if ($run.Code -eq 2) { $stage.details.Add("chain: restore deferred: a game process still ran; start-own-menu.ps1 -RestoreOnly $session") }
        $stage.numbers = "$numbers; chain $(if ($chainOk) { 'PASSED' } else { 'FAILED' }): $chainText"
        if ($replayOk -and $framesOk -and $chainOk) { $stage.result = 'PASSED' }
    } finally {
        # es2-data keeps small files only: the built binaries go, their hashes stay in the details.
        foreach ($file in @(Get-ChildItem -LiteralPath $bin -File -ErrorAction SilentlyContinue)) {
            $file.IsReadOnly = $false
            Remove-Item -LiteralPath $file.FullName -Force
        }
        $env:ES2_FRAMES_REFERENCE = $savedFrames
        $env:ES2_ORACLE_DIR = $savedOracle
    }
}

function Invoke-Timed([scriptblock]$Body) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $before = $Stages.Count
    try { & $Body } catch {
        if ($Stages.Count -eq $before) { [void](New-Stage 'internal' 'FAILED') }
        $stage = $Stages[$Stages.Count - 1]
        $stage.result = 'FAILED'
        $stage.details.Add("error: $($_.Exception.Message)")
    } finally {
        $clock.Stop()
        $Stages[$Stages.Count - 1].seconds = $clock.Elapsed.TotalSeconds
    }
    $stage = $Stages[$Stages.Count - 1]
    Write-Host ("[{0}] {1} ({2}) {3}" -f $stage.name, $stage.result, (Format-Seconds $stage.seconds), $stage.numbers)
}

# ---- Run --------------------------------------------------------------------------------------
$savedDataRoot = $env:ES2_DATA_ROOT; $savedDataRepo = $env:ES2_DATA_REPO
$savedBazel = $env:ES2_BAZEL; $savedBase = $env:ES2_BAZEL_OUTPUT_BASE; $savedCache = $env:ES2_BAZEL_CONTENTS_CACHE
try {
    $env:ES2_DATA_ROOT = $DataRoot
    # The scripts run outside Bazel (replay_suite.py, frames_suite.py) find the goldens through it.
    $env:ES2_DATA_REPO = $Es2Data
    # Es2Bazel.psm1 (prepared proxy build) reads these.
    if ($PSBoundParameters.ContainsKey('Bazel')) { $env:ES2_BAZEL = $Bazel }
    if ($BazelOutputBase) { $env:ES2_BAZEL_OUTPUT_BASE = $BazelOutputBase }
    if ($BazelContentsCache) { $env:ES2_BAZEL_CONTENTS_CACHE = $BazelContentsCache }

    Write-Host "evidence: $EvidenceRoot"
    if ($Overlap) {
        # Live stage first; the quick tier starts from inside it (OverlapStart) once the proxy exists,
        # or here if the live stage never got that far. Neither waits for the other's result.
        $script:OverlapRun = $null
        $script:OverlapStart = {
            if (!$script:OverlapRun) { $script:OverlapRun = Start-BazelTest 'quick' @('//...') }
        }
        Invoke-Timed { Invoke-LiveStage }
        $script:OverlapStart = $null
        if ($OwnSim) { Invoke-Timed { Invoke-OwnSimStage } }
        if (!$script:OverlapRun) { $script:OverlapRun = Start-BazelTest 'quick' @('//...') }
        $overlapQuick = $script:OverlapRun
        $quickClock = $overlapQuick.Clock
        Complete-BazelStage 'quick' $overlapQuick
        $quickStage = $Stages[$Stages.Count - 1]
        $quickStage.seconds = $quickClock.Elapsed.TotalSeconds
        Write-Host ("[{0}] {1} ({2}) {3}" -f $quickStage.name, $quickStage.result, (Format-Seconds $quickStage.seconds), $quickStage.numbers)
    } elseif ($SkipQuick) { [void](New-Stage 'quick' 'SKIPPED') } else { Invoke-Timed { Invoke-BazelStage 'quick' @('//...') } }
    if ($Full) { Invoke-Timed { Invoke-BazelStage 'full' @('//tools/reference-game/invitro:differential_full', '//tools:scripts') } } else { [void](New-Stage 'full' 'SKIPPED') }
    if ($SkipSignatures) { [void](New-Stage 'signatures' 'SKIPPED') } else { Invoke-Timed { Invoke-SignaturesStage } }
    if ($SkipAudit) { [void](New-Stage 'audit' 'SKIPPED') } else { Invoke-Timed { Invoke-AuditStage } }
    $liveRan = $false
    if (!$Live) {
        [void](New-Stage 'live' 'SKIPPED')
    } elseif ($Overlap) {
        # Ran first, beside the quick tier (above); judged after all stages (below).
    } elseif (@($Stages | Where-Object { $_.result -eq 'FAILED' }).Count) {
        [void](New-Stage 'live' 'NOT RUN' 'an earlier stage failed')
    } else {
        Invoke-Timed { Invoke-LiveStage }
        $liveRan = $true
    }
    if ($OwnShell) { Complete-OwnShellStage }
    if ($OwnSim -and !$Overlap) {
        # After the live stage (and its own-shell run), whose finally restored the clones' runtime.
        if ($liveRan) { Invoke-Timed { Invoke-OwnSimStage } } else { [void](New-Stage 'own-sim' 'NOT RUN' 'the live stage did not run') }
    }
    if ($SkipEvidence) { [void](New-Stage 'evidence' 'SKIPPED') } else { Invoke-Timed { Invoke-EvidenceStage } }
} finally {
    $env:ES2_DATA_ROOT = $savedDataRoot; $env:ES2_DATA_REPO = $savedDataRepo
    $env:ES2_BAZEL = $savedBazel; $env:ES2_BAZEL_OUTPUT_BASE = $savedBase; $env:ES2_BAZEL_CONTENTS_CACHE = $savedCache
}

# ---- Overlap: stage order and the verdict on the live stage ---------------------------------------
if ($Overlap) {
    $order = @('quick', 'full', 'signatures', 'audit', 'live', 'own-shell', 'own-sim', 'evidence')
    $sorted = @($Stages | Sort-Object { $i = $order.IndexOf($_.name); if ($i -lt 0) { 99 } else { $i } })
    $Stages.Clear(); foreach ($stage in $sorted) { $Stages.Add($stage) }
    $others = @($Stages | Where-Object { $_.name -notin @('live', 'own-shell', 'own-sim', 'evidence') -and $_.result -eq 'FAILED' })
    # The own-shell run and the own-sim stage belong to the live stage and are judged like it.
    foreach ($liveStage in @($Stages | Where-Object { $_.name -in @('live', 'own-shell', 'own-sim') })) {
        if ($others.Count -and $liveStage.result -eq 'PASSED') {
            $liveStage.result = 'DISCARDED'
            $liveStage.numbers = "discarded, failed beside it: $(($others | ForEach-Object { $_.name }) -join ', '); was: $($liveStage.numbers)"
        }
    }
}

# ---- Summary ----------------------------------------------------------------------------------
$failed = @($Stages | Where-Object { $_.result -eq 'FAILED' })
$executed = @($Stages | Where-Object { $_.name -ne 'evidence' -and $_.result -in @('PASSED', 'FAILED') })
$overall = if ($failed.Count -or !$executed.Count) { 'FAILED' } else { 'PASSED' }
$total = ([DateTime]::UtcNow - $Started).TotalSeconds
$md = [Text.StringBuilder]::new()
[void]$md.AppendLine("# check $($Started.ToString('yyyyMMddTHHmmssZ')): $overall")
[void]$md.AppendLine('')
[void]$md.AppendLine("- repository: $Repo")
[void]$md.AppendLine("- commit: $commit" + $(if ($dirtyStamp.Count) { ' (uncommitted changes under the source-stamp paths)' } else { '' }))
[void]$md.AppendLine("- evidence: $EvidenceRoot")
if ($Live) {
    $rs = $script:RuntimeSummary
    $runtimeLine = if ($Runtime -eq 'own') {
        "own cw3220.dll" + $(if ($rs.sha256) { " $($rs.sha256)" } else { '' }) +
            "; deployed on $(@($rs.deployed).Count) of $($InstallName.Count) clone(s), restored on $(@($rs.restored).Count)" +
            $(if (@($rs.not_restored).Count) { ", NOT restored on $(@($rs.not_restored) -join ', ')" } else { '' })
    } else { 'original CW3220.DLL (nothing deployed)' }
    [void]$md.AppendLine("- runtime: $runtimeLine")
    if ($Bwcc32 -eq 'refuse') { [void]$md.AppendLine('- BWCC32: load answered with failure in DBSIM (-Bwcc32 refuse, the default)') }
    else { [void]$md.AppendLine('- BWCC32: loaded in DBSIM (-Bwcc32 load)') }
    if ($Suite.Count) { [void]$md.AppendLine("- suites: $($Suite -join ', ') (-Suite)") }
    if ($NewFail) { [void]$md.AppendLine("- new fail: ES2_NEW_FAIL=$NewFail, logs $NewFailLog (-NewFail)" + $(if ($newFailWarning) { "; WARNING: $newFailWarning" } else { '' })) }
}
[void]$md.AppendLine("- total: $(Format-Seconds $total)")
[void]$md.AppendLine('')
[void]$md.AppendLine('| stage | result | duration | key numbers | log |')
[void]$md.AppendLine('| --- | --- | --- | --- | --- |')
foreach ($stage in $Stages) {
    $duration = if ($stage.result -in @('PASSED', 'FAILED')) { Format-Seconds $stage.seconds } else { '-' }
    [void]$md.AppendLine(("| {0} | {1} | {2} | {3} | {4} |" -f $stage.name, $stage.result, $duration, ($stage.numbers -replace '\|', '/'), $stage.log))
}
foreach ($stage in @($Stages | Where-Object { $_.details.Count })) {
    [void]$md.AppendLine('')
    [void]$md.AppendLine("## $($stage.name)")
    [void]$md.AppendLine('')
    foreach ($line in $stage.details) { [void]$md.AppendLine("- $line") }
}
Write-Text (Join-Path $EvidenceRoot 'summary.md') ($md.ToString())
$json = [ordered]@{
    schema = 1; result = $overall; utc = $Started.ToString('o'); repository = $Repo; commit = $commit
    seconds = [Math]::Round($total, 1)
    runtime = if ($Live) { $script:RuntimeSummary } else { $null }
    stages =@($Stages | ForEach-Object { [ordered]@{ name = $_.name; result = $_.result; seconds = [Math]::Round($_.seconds, 1); numbers = $_.numbers; details = @($_.details) } })
}
if ($Live) { $json.suites = if ($Suite.Count) { @($Suite) } else { 'all' } }
if ($NewFail) { $json.new_fail = [ordered]@{ spec = $NewFail; log = $NewFailLog; warning = $newFailWarning } }
Write-Text (Join-Path $EvidenceRoot 'summary.json') ($json | ConvertTo-Json -Depth 5)
Write-Host "check: $overall ($EvidenceRoot\summary.md)"
if ($overall -eq 'PASSED') { exit 0 }
if (!$executed.Count) { exit 2 }
exit 1
