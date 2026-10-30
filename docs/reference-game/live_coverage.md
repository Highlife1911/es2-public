# Live original-branch coverage

Opt-in measurement of which original branch sites run in the game: INT3
traps on the Jcc sites of `invitro/branch_sites.h` (4,676 `BRANCH` lines),
plus entry probes on selected original function starts. Measurement only; no
original executable is modified on disk. [Edge mode](#edge-mode) observes
each outcome once and is the mode for unions; the [current union](#current-union)
and the [never-reached sites](#never-reached-sites) with their reasons
follow the tool description.

## Trap mechanism

Each selected entry in `invitro/branch_sites.h` counts the original Jcc's
fallthrough and taken outcomes. The registry supplies hook identities. In
original mode the normal body is reachable. In replaced mode only executions
that still reach original code count (including nested/bypassed callers);
zero observations do not prove that the corresponding replacement path is
unused. Compare-mode original executions count too. Branchless hooks have
zero sites, not missing coverage.

The unchanged DBSIM file hash and loaded PE identity gate installation. All
selected sites are decoded and checked for length, condition, target and
overlap before any patch. Relocated hook prologues are excluded. Only the
first instruction byte changes to INT3. A first vectored exception handler
claims only owned breakpoint addresses, computes the original predicate
from saved EFLAGS, and changes only saved EIP. It performs no game calls,
allocation, report I/O or floating-point operations. Other exceptions remain
unclaimed and available to crash capture and later handlers. Runtime
counters are atomic; thread IDs are measured, rather than assuming all
callers use one thread.

`count` traps every execution and reports exact counts for the measured
interval. `saturate` restores each byte once both outcomes have been seen;
its counts are lower bounds. A permanently one-way hot site still traps
every time, so there is no general two-traps-per-site bound. `edge`
([below](#edge-mode)) observes each missing outcome once. Installation and
uninstallation require a quiescent caller; saturation retains the
handler/table to service a breakpoint already fetched by another thread.
Approximate added time is exception count times trap cost, plus per-report
I/O and patch/restore costs. Site lookup scans the selected table, so the
cost depends on the site's position and selection size (measured on a
210-site table: 8.7 µs per trap for the first entry, 8.9 µs for the last).
There is no bound on exceptions from a function's static site count alone:
loops and permanently one-way sites may execute arbitrarily often.

Architectural transparency is testable: preserve general registers, ESP,
all saved flags including AF/PF, and the x87/SSE context; only EIP chooses the
same successor as Jcc. **Stack residue is not universally transparent.**
Windows exception dispatch writes below interrupted ESP before our handler
runs. The existing harness documents changed uninitialized matrix padding
there. An alternate handler stack cannot undo those prior writes, so INT3 is
not invisible to every original instruction. Each measured suite must match
its golden using the existing normalization/comparison; otherwise its
coverage is invalid and the runner fails. Clock scheduling, uninitialized
data outside compared state, and thread behavior remain live questions even
after a golden match. Every documented coverage measurement ran with every
hook original and matched its goldens; replaced hooks plus coverage have no
live measurement.

Reports are separate JSON, never tick fields, and contain no instruction
bytes. Atomic periodic snapshots retain progress for forcibly stopped suites;
the imported C runtime's `atexit` callback and a verified DBSIM `__exit` IAT
intercept cover orderly and immediate exits before DLL detach. Tick shutdown
is another fallback. A fixed comparison window ends coverage at its final
included tick. Full-mission reports must be finalized at exit. Missing,
partial, failed or cross-thread reports are not accepted silently. The
measurement wrapper delegates launch, stop identity checks and golden
comparison to the existing oracle runner.

## Configuration and measurement

```ini
[coverage]
enabled=1
mode=saturate
hooks=all
report=<es2-data>\logs\my-fresh-run\coverage.json
report_every_ticks=32
frame_limit=0
first_hit_ticks=0
```

Absent section or `enabled=0` makes no patch or report. Other values are
strict: mode is `saturate`, `count` or `edge`; hooks is `all` or unique,
case-sensitive registry names. `report` must name a fresh file in an
existing directory. `report_every_ticks=0` disables periodic reports.
Nonzero `frame_limit` freezes counts and restores instrumentation
immediately after recording frame N-1. The interval starts at coverage
installation (which can include startup before frame zero) and ends at that
snapshot. Zero ends at the first runtime exit callback/intercept; later
shutdown callbacks are outside the interval.

A suite with `hold` ([oracle_regression.md](oracle_regression.md)) ends
parked after its N-th tick record and is stopped from outside, so no exit
callback runs and its golden carries no `frame_limit`. The runners write
`frame_limit=N` (N = the suite's `ticks`) for it: the report is then final
with reason `frame_limit` and last frame N-1, which is the golden's last
frame. The union and the probe merge take N from the launch report's
`hold_frame` and refuse a report whose window differs, a `hold_frame` that
is not the golden's last frame + 1, and a `frame_limit` report of a suite
without hold (which must be an exit report). `-Perf` does not support hold
suites.

The wrapper supplies a separate INI via inherited environment variable
`ES2_LIVE_COVERAGE_CONFIG`, because the unchanged oracle runner regenerates
the clone's main INI. It restores the calling process's environment afterward.
Hook modes still come from the main INI and are recorded in each report.

Optional `first_hit_ticks=1` / wrapper `-FirstHitTicks` records
`first_fall_tick` and `first_taken_tick` for each site. These are the **last
recorded tick before the hit**, not a claim about the engine's current update
phase: `-1` means before the first recorded tick, and `null` means that outcome
was never seen. For example, 1370 places a hit after snapshot 1370 and before
the next snapshot. The handler reads only an atomic proxy-owned tick value;
it does not read game state for timestamps. The merged summary pairs each
first-hit tick with its first-observing suite. Tracking defaults off.
Snapshot timing is not a CPU timestamp.

```powershell
# No game: inspect the intended suites/configuration.
& tools\reference-game\setup\run-live-coverage.ps1 -DryRun

# All three suites, all hooks original, saturate.
& tools\reference-game\setup\run-live-coverage.ps1 -InstallName timing-r111 -CoverageMode saturate -CoverageHooks all

# Exact counts for a focused follow-up (a fresh output directory is automatic).
& tools\reference-game\setup\run-live-coverage.ps1 -InstallName timing-r111 -CoverageMode count -CoverageHooks ground_collide -Suite c1_01-collision
```

Optional `-Hooks 'rng_next=replaced'` changes hook modes. It does not change
the meaning of branch observations: those always describe original code.
The wrapper rejects `native-r111`, path-like clone names, linked clone
directories, duplicate/unknown hooks and pre-existing outputs. Live output
must remain under `DataRoot/logs`. Each suite delegates deployment, launch,
path/start-time verified stopping, normalization and golden comparison to
`run-oracle-regression.ps1 -Mode check` unchanged. `-MockDirectory` uses
synthetic reports/goldens only.

## Report and acceptance

`coverage.json` schema 1 records `module_sha256`, `mode`, process/thread IDs,
`error`, `complete`, `reason`, `has_tick`, `last_frame`, `frame_limit`, selected
`hooks` (mode 0 original / 1 replaced / 2 compare), and `sites`. Each site has
`hook`, original `va`, `condition`, unsigned 64-bit `fall` / `taken` counts,
and `restored`. No saved bytes, memory dump or assets are serialized.
Updates use a fresh PID-suffixed temporary file, flush, close, then same-volume
atomic replacement. A failed write retains the previous report and invalidates
finalization; snapshots are not final evidence. Error codes: 1 install,
2 byte verification, 3 page protection, 4 flush/protection restoration,
5 busy uninstall, 6 thread ownership, 7 report I/O, 8 VEH removal,
9 exit-slot restoration; edge mode adds 10 to 12 ([below](#edge-mode)).
Zero is required for acceptance.

For fixed windows a final report must say `reason=frame_limit`, N-1 and N;
for the full mission it must say `reason=exit` with the golden's last frame.
DLL detach alone is insufficient. `TerminateProcess`, crashes or timeouts
before the boundary yield invalid coverage. Registration uses the original
runtime's `_atexit` export; its `__exit` import is replaced only if it still
matches the runtime export. The IAT slot is restored with pointer verification
at finalization, alongside verified Jcc restoration. The callback remains
registered until process termination and becomes a no-op after completion.
This diagnostic IAT change and C runtime callback bookkeeping are additional
to the one-byte-per-Jcc code patches; original files remain unchanged.

`coverage-manifest.json` ties every report to its identified launch PID,
proxy hash, requested hook modes and exact selected site inventory. The helper
rechecks golden identity with the existing comparator before merging anything.
Missing/partial reports, different proxy hashes, foreign observed executors,
different installation/tick threads, unmatched windows and unrestored sites
all fail. Failures produce `coverage-summary.json` with `valid=false`.
Valid JSON and Markdown summaries contain per-hook sites / both ways /
fall only / taken only / never, and each site's first suite for each outcome.
Merged count-mode totals sum the measured windows; saturate totals remain
lower bounds. Zero-site hooks are listed explicitly.

For a pasteable `both ways / one way / never` column, run the following on a
valid summary. The helper prints a table and never edits `hooks.md`; it
rejects invalid summaries and inconsistent totals.

```powershell
& '<es2-dependencies>\python\python.exe' -B tools\reference-game\scripts\live_coverage_column.py <es2-data>\logs\<run>\coverage-summary.json
```

## Entry probes

Entry probes measure arrivals at selected original addresses, including
functions without a registered replacement. They do not measure replacement
callbacks or establish the semantic identity of a function.

```ini
[probes]
enabled=1
mode=count
saturate_after=64
entries=<HEX_VA>:rcv=esp4,<HEX_VA>:rcv=ecx,<HEX_VA>
report=<es2-data>\logs\fresh-run\probes.json
frame_limit=0
```

`entries` has at most 512 unique hexadecimal VAs (`EP_MAX_ENTRIES`, the
same limit in `run-live-coverage.ps1`), optionally prefixed `0x`, in at most
8,192 characters. The receiver suffix is optional and case-sensitive: `esp4`
reads the entry stack argument at `[esp+4]`; `eax/ecx/edx/ebx/esp/ebp/esi/edi`
selects that entry register. The selected object pointer is read once for its
vtable. Each entry has 32 histogram slots; new keys beyond capacity, null
pointers, unreadable objects and null vtables increment `overflow`. An
inaccessible receiver does not prevent the entry count. Checked reads
preserve LastError. No histogram or pointer read occurs without `:rcv=`.

Count mode is exact within the finalized window. Saturate mode counts the
first N arrivals (positive decimal `saturate_after`, default 1), restores the
byte and labels the count a lower bound. Already-fetched breakpoints after
restoration still emulate the instruction without exceeding N. Concurrent
counts/histogram slots use interlocked operations; foreign-thread execution
invalidates a production report and prevents unsafe teardown. This is not
permission to install or remove probes while other threads execute them.

**Entry kinds.** The probe inventory `proxy/probe_entry_inventory.h` holds
two generated tables of the hash-verified DBSIM image
([generated static inputs](#generated-static-inputs)). `probe_instructions`
has 24,202 instruction starts with their one-byte signatures (a linear sweep
plus a disassembly synchronized at known code starts, which adds the starts
the sweep loses behind zero padding and jump tables, such as the logic frame
`0x45f570`, and drops linear entries that lie inside an instruction or a
jump table); a start there that begins with a one-byte `PUSH r32` (not ESP)
is probed by emulating the push. `probe_entry_kinds` gives the other known
function starts whose first instruction is one of 26 further kinds (`enter`,
`jmp [abs32]` import thunks, loads, register moves, stores, direct
transfers, immediate pushes, compares, additions, `xor r, r`); the engine
`entry_probe_engine.h` emulates each from a copy taken at install time.
Rules, kinds and counts:
[dbsim_map.md](dbsim_map.md#probe-inventory) (the first
table) and [dbsim_map.md](dbsim_map.md#entry-kinds) (the
second). The only known start that cannot be probed is `0x467ad8`
(`int3; ret`: its original byte is the INT3 itself). Interior instructions
can be decoded starts too: the investigator must choose an actual function
entry. Semantic entry identity remains a hypothesis until supported by the
relevant reverse-engineering evidence. Other instructions are refused, never
approximated.

Runtime binary search checks the inventory, the unchanged module SHA-256,
loaded PE identity and the saved opcode; the generator feeds the
already-hashed byte buffer to the installed LLVM decoder over stdin, and no
original image or disassembly is saved. The relocation range of a registered
hook is excluded only when the hook is actually installed. The proxy installs
either all registered hooks (as soon as one is not `original`; an `original`
hook then still patches its prologue and runs the original through its
trampoline) or none. So with every hook `original`, entry probes may sit on
hook entries such as `0x40e964` and `0x426a20`. Every coverage instruction
range is excluded even if coverage is disabled. Duplicates, non-instruction
starts and signature mismatches are refused before patching. Runtime changes
only replace the first byte with INT3. The handler emulates the instruction
in the saved context and never temporarily removes INT3 to single-step.
Coverage and probes have ordered handlers, each claiming only its own
addresses; synthetic execution verifies both registration orders with exact
counts.

Reports are atomically published JSON, schema 1, with `module_sha256`, `mode`,
`saturate_after`, PID/thread ownership, `error`, `complete`, `reason`,
`has_tick`, `last_frame`, `frame_limit`, and `entries`. Each entry contains
`va`, `receiver`, `count`, `saturated`, `receivers: [{vtable, count}]` and
`overflow`. Receiver keys are addresses, not class names. No original bytes
or object contents are reported. The wrapper rejects incomplete/failed,
wrong-process, wrong-window, wrong-inventory or inconsistent histogram reports.
No `[probes]` section means no patch, handler, exit registration or tick field.
The raw all-hook tick/watch fixture is unchanged at **440,247 bytes**, including
with `[hooks] fast_path=0`.

Coverage's verified `__exit`/`_atexit` import machinery owns both facilities.
The same tick boundary freezes frame-limited runs; natural exit finalizes
whole-mission runs. The wrapper supplies both sections in its
`ES2_LIVE_COVERAGE_CONFIG` file. Reports never substitute for golden checking.

```powershell
Set-Location <es2>
$entries='<ENTRY_LIST_FROM_docs/reverse-engineering/weapons.md>'
# Replace the placeholder with the reviewed, supported, non-overlapping VAs.
# No process is started by DryRun.
& .\tools\reference-game\setup\run-live-coverage.ps1 -DryRun -ProbeEntries $entries -ProbeMode saturate -SaturateAfter 64
# All three suites, coverage and entry probes together.
& .\tools\reference-game\setup\run-live-coverage.ps1 -InstallName timing-r111 -CoverageHooks all -CoverageMode saturate -ProbeEntries $entries -ProbeMode saturate -SaturateAfter 64
# Exact counts, probes alone, one focused suite.
& .\tools\reference-game\setup\run-live-coverage.ps1 -InstallName timing-r111 -ProbesOnly -ProbeEntries $entries -ProbeMode count -Suite c1_01-success
```

The wrapper delegates each suite to `run-oracle-regression.ps1 -Mode check`
and uses its unchanged normalizer/goldens. A perturbed suite fails the run
and invalidates its probe data. `probes-summary.json` and `probes-summary.md`
retain counts and receiver histograms separately per suite; saturation
counts are never presented as exact totals. `-MockDirectory` accepts a
synthetic `probes-manifest.json` (plus `coverage-manifest.json` for combined
runs); `-DryRun` validates syntax and prints the exact live command. These
paths do not access installations or start a game.

Fixtures cover exact hits, N=3 restoration, all selectors, 32-slot overflow,
three concurrent executors, every supported context of each kind
(`entry_probes_kinds_test`), malformed/duplicate selection,
relocation/coverage exclusions, both handler orders, shared shutdown and
mocked golden rejection. A real-data Bazel test checks all signatures and
independently regenerates all instruction starts; it skips without
`ES2_DATA_ROOT`. A synthetic own image tests the synchronization rule
without the CD. Open live hypotheses: exception-stack residue, unexpected
threads, actual receiver lifetimes.

### Arming time

The proxy installs hooks, coverage sites and entry probes lazily, not in
`DllMain`. Early arming is the default: `configure-tick-log.ps1` writes
`[arming] early=1` into `es2-oracle.ini` (`-EarlyArming:$false` or
`ES2_EARLY_ARMING=0` omit it), and the proxy then arms everything (hooks,
coverage, probes, tick log) at the call of the mission load `0x45f42b`, so
the load's executions are counted
([mission_load.md](../reverse-engineering/mission_load.md#13-l0-arming-before-the-load-and-the-after-load-snapshot)).
Without it the proxy arms at the first DirectDraw or MCI call
(`initialize_forwarding` in `media_proxy.c`). Either way, code that DBSIM runs
before the arming point always reads 0: a zero count for start-up code is
not evidence that it never runs (class **unarmed** of the
[never-reached sites](#never-reached-sites)). The VOL registration at
`0x45f2a8` ([vol.md](../formats/vol.md#portable-policy-and-live-questions))
has its own ordered exception: `[vol_probes]` in the same
`ES2_LIVE_COVERAGE_CONFIG` file, armed in `DllMain`
(`run-live-coverage.ps1 -VolProbes`; design in
[vol.md](../formats/vol.md#registration-probes-from-dllmain)).

## Coverage with every regression

`run-oracle-parallel.ps1 -LiveCoverage original|run` writes the live
branch-site union of a regression next to its tick-log comparison, so the
union is never older than the last batch run.

**Default off, and why.** `[coverage]` adds no tick field: the proxy writes its
report to a separate file, and `live_coverage_tick` runs only after the tick
record has been flushed (`tick_log.h`), so tick records, normalization and
the comparison rules are unchanged. The simulation itself can still change in
principle: every trap goes through Windows exception dispatch, which writes
below the interrupted ESP ([trap mechanism](#trap-mechanism)). The runner's
default behaviour stays unchanged, so the switch is opt-in, and the
`original` variant keeps the verdict pass uninstrumented.

| `-LiveCoverage` | Passes | What the union counts |
| --- | --- | --- |
| `off` (default) | the regression, unchanged | nothing; no file, variable or summary key is added |
| `original` | 1. the regression with its `-Hooks`, without `[coverage]` (`ES2_LIVE_COVERAGE_CONFIG` cleared for its children); 2. only if every suite was identical: the same suites, clones and prepared proxy without `-Hooks` (every hook original, so the proxy installs no hook) and with `[coverage] hooks=all` | the original branch outcomes the suites exercise. This is the number for track H: golden identity of the replaced-mode regression says that for the inputs reaching these outcomes the replacements produced the same results. It does not prove that a replacement takes the same internal paths |
| `run` | the regression itself carries `[coverage] hooks=all` | only executions that still reach original code under the run's hook modes: hooks in `original` or `compare` mode, calls a replacement bypasses or skips to the original, sites in shared helpers that other original callers reach. A replaced hook's own path never traps, so "never" is no evidence that its replacement path is unused. A residual check (`residual_hooks`: hooks not in original mode whose sites still saw outcomes); without `-Hooks` it is the original-mode union in one pass |

Neither variant changes a suite result, the `results` of `summary.json` or the
exit code. With `original`, a difference in pass 2 (perturbation, timeout,
crash) invalidates only the union. With `run` the coverage shares the verdict
pass, so a perturbation would show as a DIFFERENT suite of the regression
itself: run that regression again without the switch before bisecting. `run`
together with `-Perf` is refused (the profile would include the traps). The
site inventory is the same in both variants: sites inside relocated hook
prologues are excluded by construction, whether or not hooks are installed.

**Files.** Beside `summary.json`, which gains a `live_coverage` object (`mode`,
`coverage_mode`, `valid`, `union`, `suites`, `error`) only when the switch is
used: `coverage-union.json`; `coverage-union.md` (totals, suites, per-hook
table with mode, every never-reached site by hook); `coverage-column.md` (the
pasteable [hooks.md](hooks.md) column, `live_coverage_column.py --totals`);
`coverage-union.log`. Per suite in `<pass>\<suite>\` (the regression directory
for `run`; `coverage-original\` for `original`, which also gets its own
`summary.json`): `coverage.ini`, the proxy's `coverage.json`, and
`coverage-summary.json`/`.md` for that suite alone.

**Validation** (`scripts/live_coverage_union.py`). Expected inventory: every
`BRANCH(...)` of `invitro/branch_sites.h` outside comments, and the hooks of
`proxy/hook-names.txt` with the modes that the pass's `-Hooks` string
configures (names case-insensitive as in `configure-tick-log.ps1`; omitted
hooks original). Each suite is checked by the unchanged
`live_coverage_report.merge` that `run-live-coverage.ps1` uses: golden identity
recomputed with the existing comparator, schema and module hash, `error` 0,
`complete`, thread ownership, report PID equal to the launch PID in
`<suite>-entry.json`, the `frame_limit` or `exit` window of the golden, one
proxy hash for all suites, the hook modes, and the complete, restored site
list. In addition every report must have the requested counting mode, and the
run entry must be a check run with the pass's hook selection. The union is
valid only when every suite is; the per-suite summaries are written either way.

**Options.** `-CoverageMode saturate` (default; counts are lower bounds),
`count` or `edge`. `-CoverageBaseline <coverage-union.json>` (edge only,
[below](#edge-mode)). `-CoverageReportEveryTicks` defaults to 0: only the
installation and the final report are written. Periodic snapshots are never
final evidence, and with thousands of sites each is a sizeable file plus a
`FlushFileBuffers`. `-CoverageTimeoutSeconds` (default 3,600) is passed as
`run-oracle-regression.ps1 -SuiteTimeoutSeconds` only to a pass that carries
`[coverage]` traps; the verdict pass keeps 300 s. The proxy refuses report
paths longer than `MAX_PATH`-32 characters; the runner refuses such an
`OutputDirectory` before it starts anything. To compare two batches:
`suite_coverage_delta.py <old coverage-union.json> <new coverage-union.json>
--output <delta.json>` (a valid union has the summary format it reads).

**Tests.** `//tools/reference-game/scripts:live_coverage_union_unit_test`
(synthetic trees: valid unions of both kinds with frame-limit and exit suites,
commented-out sites, mode parsing, one perturbed suite invalidating only the
union, six rejected reports, the counting-mode check, the column with totals,
the delta between two unions) and
`//tools/reference-game/proxy:oracle_speed_scripts_test` (the actual runner
with mock children that publish synthetic proxy reports: INI contents and
environment per pass, the uninstrumented verdict pass, the skipped pass after
a failed regression, a perturbed coverage pass that leaves the verdict
identical, a perturbed `run` pass, the `-Perf` and path-length refusals, the
dry run, and no coverage file without the switch).

## Edge mode

`[coverage] mode=edge` observes every missing outcome of a site **once**
instead of trapping its Jcc on every execution. The cost of `saturate` comes
from sites that keep going one way (they trap until the other outcome
appears, often never) and from sites that see their second outcome late;
over six suites of about 3,100 sites that was about 132 million one-way and
88 million pre-saturation traps at about 9 µs each, roughly 33 minutes of
trap time (the 36-suite edge pass took 11 min 41 s in all). Edge mode gives
each missing outcome the cheapest role the site allows
([live_coverage_engine.h](../../tools/reference-game/proxy/live_coverage_engine.h)):

| Role | Outcome | How | Traps |
|---|---|---|---|
| redirect | taken | the Jcc's displacement points at a byte that faults by itself (INT3 `CC`, HLT `F4`, IN/OUT `E4`–`E7`/`EC`–`EF`, INS/OUTS `6C`–`6F`); the handler records taken, restores the displacement and continues at the target. rel32: a slot of the proxy's own INT3 page; rel8: a byte of the original image within reach that starts no reached instruction and is never written | once |
| fall, taken | the edge | a one-shot INT3 on the edge's destination when that destination has no other predecessor; the handler records the edge, restores the byte and re-executes it | once |
| jcc | both | the INT3 of the other modes, kept only while a missing outcome has no cheaper role (a fall-through that other code also reaches, a rel8 site without a redirect byte and with a shared target) | until the outcome appears |

Roles only ever disarm. Outcomes a baseline already knows are never armed
(below). A role whose bytes fail the install checks (outside the image, in
a relocated hook prologue, inside another site, a destination that is a
fault byte, a redirect byte that does not fault, a duplicate) falls back
to the jcc role. The fixture executes each of the 14 fault bytes and checks
that it raises a breakpoint or privileged-instruction exception at its own
address. The remaining edge-mode time is mostly jcc roles at join
destinations in the terrain loops (`terrain_height`, `edge_test`,
`segment`): fall-throughs that another Jcc also reaches. A one-shot role for
them (markers on the other predecessors and out-of-line execution of the
destination instruction) is not built.

**Static input.** [branch_edges.py](../../tools/reference-game/scripts/branch_edges.py)
writes [live_coverage_edges.h](../../tools/reference-game/proxy/live_coverage_edges.h),
one `EDGE(va, fall, taken, trap)` line per BRANCH site sorted by address:
exclusivity of the two destinations and the rel8 redirect byte. A
destination is exclusive when its only predecessor is the site's Jcc,
counting conservatively the synchronized descent of
`probe_instruction_inventory.py` (fall-throughs, direct targets, return
points, jump tables), one linear sweep (targets and fall-throughs) and every
base-relocated dword. Redirect bytes avoid every site's bytes, reached
instruction starts and the first 16 bytes after each hook entry of
`hook_registry.h`; a maximum bipartite matching gives each byte one site.
The committed table (4,676 sites): 5,789 of 9,352 edge destinations
exclusive, 2,194 rel8 redirect bytes; rel32 sites always get a stub. The
proxy joins the table by address: a stale table (new BRANCH lines) only
leaves the new sites without edge help. Regenerate it with the BRANCH lines:

```powershell
& <es2-dependencies>\python\python.exe -B tools\reference-game\scripts\branch_edges.py `
    build\extracted\cd\VER95\DBSIM.EXE tools\reference-game\proxy\live_coverage_edges.h
```

**Baseline (known outcomes).** `run-oracle-parallel.ps1 -LiveCoverage
run|original -CoverageMode edge -CoverageBaseline <coverage-union.json>`
writes `coverage-known.txt` (`live_coverage_union.py --write-known`: one
line per site with a known outcome) and names it in every proxy INI
(`known=`). Those outcomes are never watched; the report echoes them per
site (`known_fall`, `known_taken`, `known_outcomes`), the union validates
the echo against the baseline and counts them as seen. Such a union is
"baseline plus this pass": `suite_coverage_delta.py <baseline> <union>`
lists exactly what the pass adds.

**Reports.** `mode: "edge"`, `edge_roles` (roles armed at install),
`known_outcomes`, per site `known_fall`/`known_taken`. Counts are lower
bounds, normally one per outcome and role. Error codes: 10 a destination
point still armed when its instruction must re-execute, 11 no memory for
the tables or the stub page, 12 two roles of one kind at one address.

**Validation against saturate.** The edge-mode reports of the six suites of
a saturate union over 3,096 sites, merged from their per-suite summaries,
gave the same verdict as saturate at every one of those sites (1,642 both
ways, 500 fall only, 404 taken only, 550 never).

**Tests.** `live_coverage_engine_test` (16 predicates × rel8/rel32 × six
role variants with full register/flag/FPU identity, the 14 fault bytes, a
shared point, duplicate trap bytes, a released point),
`live_coverage_cost_test` (`edge_one_way`: one exception for 100,000 calls),
`live_coverage_configuration_test` (`mode=edge`, the `known=` file and its
refusals, report fields, the table sorted with one line per site),
`live_coverage_reference_test` (the full table installs on a private copy of
the reference image and uninstall restores every byte),
`branch_edges_unit_test` (own synthetic PE; with `ES2_DATA_ROOT` the
committed table equals a fresh generation), `live_coverage_union_unit_test`
and `oracle_speed_scripts_test` (baseline, echo checks, runner INI, refusals).

## Generated static inputs

Three proxy headers hold static data that is derived, not edited. Each has a
generator and a `bazel test` that regenerates it and compares bytes, so a
stale file fails the build; none is hand-maintained.

| File | Generator | Inputs | Test | Regenerate when |
| --- | --- | --- | --- | --- |
| `proxy/probe_entry_inventory.h` (`probe_instructions`, 24,202 `{va, opcode}` rows; `probe_entry_kinds`) | `scripts/probe_instruction_inventory.py` | the reference `DBSIM.EXE` only (SHA-256 gated) | `scripts:probe_instruction_inventory_unit_test` | never for a hook merge; only if the rule of the script changes |
| `proxy/live_coverage_edges.h` | `scripts/branch_edges.py` | `DBSIM.EXE`, `invitro/branch_sites.h`, `hook_registry.h` | `scripts:branch_edges_unit_test` | after every merge that adds `BRANCH` lines or hooks (coordinator) |
| the two marked regions of `proxy/callee_counters.h` (`legacy_graph_counters` list and the `#define <group>_hook_counters legacy_graph_counters` lines) | `scripts/legacy_graph_counters.py` | `hook_registry.h`, `proxy/*_hooks.h`, the counter arrays in `callee_counters.h` | `scripts:legacy_graph_counters_unit_test` | only if a hook of one of the 29 legacy groups is added or a group gains its own counter function |

**The inventory.** The registry is not an input, so no merge changes it.
The currency check runs in the scripts tier together with the synthetic-PE
cases (linear desync behind a jump table and zero padding, overlap refusal,
hash gate), so it is cached on the CD content digests of `@es2_data` and
skips without `ES2_DATA_ROOT`.

```powershell
& <es2-dependencies>\python\python.exe -B tools\reference-game\scripts\probe_instruction_inventory.py `
    build\extracted\cd\VER95\DBSIM.EXE tools\reference-game\proxy\probe_entry_inventory.h
```

**The legacy counter dispatch.** A group is *legacy* when no
`proxy/*_hooks.h` defines `<group>_hook_counters`; a hook is listed when its
group is legacy and `callee_counters.h` has a `<hook>_counters[]` array,
except the six hooks whose arrays are read by the damage and ground
observers directly (`ground_update`, `ground_collide`, `ground_effects`,
`vehicle_hit`, `building_hit`, `radius_hit`). The result is 29 groups and 89
listed hooks, in registry order (every `GRAPH_COUNTERS` line tests one
distinct hook index, so the order does not change behaviour). The test
additionally pins the 29 group names (a new group that forgets its
`<group>_hook_counters` would otherwise turn into a silent legacy group)
and, with the CD, checks all 384 direct-call counters against the reference
listing: every `call_XXXXXX` key (an RVA in the older arrays, a VA in the
newer ones) is a reached instruction start of the synchronized descent and
matches its name. The counter arrays themselves stay hand-written: they name
the game calls our replacement code still makes, which the listing alone
does not tell. A new hook goes into a new group with its own
`<group>_hook_counters`, so merges do not touch the generated regions.

```powershell
& <es2-dependencies>\python\python.exe -B tools\reference-game\scripts\legacy_graph_counters.py tools\reference-game\proxy
```

## Current union

Edge mode over the 40 registered suites, every hook original, 507 hooks,
3,776 sites: a 36-suite pass
(`<es2-data>\logs\edge-coverage-36-20261012T173520Z\edge`; five
clones, hidden window, 11 min 41 s, every suite identical to its golden)
plus a delta pass of `c1_01-cheat-disable-late`, `c1_01-display-keys`,
`c3_10-detail-options` and `c1_01-quit-dialog` against it as baseline
(`<es2-data>\logs\u-12-live\edge-delta`, all four identical; its
`coverage-union.json` is the union of the 40 suites). Both unions are valid.

**2,397 of 3,776 sites both ways, 647 fall only, 430 taken only, 302
never; 5,871 of 7,552 outcomes seen; 431 of 507 hooks have sites** (76
hooks are branchless).

The tape delta (`c1_01-tape-play`, `c1_01-tape-demo` against the 40-suite
union, `<es2-data>\logs\tape-coverage-live\edge-delta`, both
identical, valid) extends it to the whole inventory of 789 hooks: **2,745
of 4,676 sites both ways, 868 fall only, 559 taken only, 504 never; 6,917
of 9,352 outcomes seen; 577 of 789 hooks have sites**. 27 of the 302 never
sites are seen in it (their rows below say so); 229 of the 900 sites the
40-suite union did not instrument stay never in the 42-suite union, which
measures them in the two tape suites only. Its `coverage-union.json` is
the baseline for the next delta.

```powershell
& .\tools\reference-game\setup\run-oracle-parallel.ps1 -InstallName @('timing-r111','timing-r111-p1','timing-r111-p2','timing-r111-p4','timing-r111-p5') `
    -PreparedProxy $prepared -LiveCoverage run -CoverageMode edge -HiddenWindow -OutputDirectory <dir>\edge
```

Hooks with the most never-reached sites: `control_record` 40 of 140,
`player_input` 12 of 50, `ram_step` 10 of 10, `smoke_update` 10 of 10,
`trigger_scan` 10 of 28, `cell` 9 of 14, `cockpit_select` 7 of 9,
`prefs_video` 7 of 7, `track_target` 7 of 10. The per-hook table and the
never-reached site list are `coverage-union.md` of the run.

## Never-reached sites

Every one of the **302 sites** that the 40-suite [union](#current-union)
lists as never reached in either direction is classified below by hook;
the rows of the 27 that the tape delta reaches name what it saw.
Sources: the union and the per-suite `coverage.json` files of the two runs
(first suites, per-suite edges), the suite sections of
[oracle_suites.md](oracle_suites.md) with their live results, the hooks'
contract sections (linked from the [registry](hooks.md#registry)), and an
`llvm-objdump -d -M intel` listing of the reference `VER95/DBSIM.EXE`
(SHA-256 `8561aa47…c4ad`, checked; image base `0x400000`) with raw dword
scans of the file. Static claims; none is live evidence until a suite
variant shows the site in a later union.

### Classes

| Class | Meaning |
| --- | --- |
| input | a deterministic input reaches the site: keys, mouse, a launch option, a scripted joystick, another mission or loadout, or a longer suite. The recipe column names how; "(hypothesis)" marks a path whose trigger was inferred, not traced to the end |
| defensive | the shipped game never takes it: data-unused values (trigger types 5/6, activation modes 4/5, mission mode 5, VOL member kinds 7/9), an impossible overlap, a radius the data never produces |
| dead | no path in the retail binary: proved from the listing (a dword or byte without a writer, a constant argument from every caller, a pool that is never allocated from) |
| unarmed | the code runs only at start-up, before the mission-load call `0x45f42b` where the proxy arms ([arming time](#arming-time)); no suite can show it until coverage is armed from `DllMain` as for the VOL probes |

| Class | Sites | Hooks with such sites |
| --- | ---: | ---: |
| input | 204 | 64 |
| defensive | 50 | 13 |
| dead | 32 | 13 |
| unarmed | 16 | 3 |
| total | 302 | 87 |

### Recipes

The recipe column of the per-hook tables names these inputs.

- **R1, cheat mode.** `-SPRUNKNOWN` (dword `0x49ef38`,
  [launcher.md](../reverse-engineering/launcher.md)): Ctrl+N/Ctrl+P view
  subject, Alt+Ctrl+,/. damage slot (word `0x4d2500`), Alt+Ctrl+D 300 on the
  subject, Alt+Ctrl+N 32,000 on the first hostile within 100,000, Alt+S
  freeze, Alt+Num+ single step. Suites: `c1_01-cheat-*`,
  `c1_06-cheat-disable`; a cheat suite needs its own golden (the flag
  changes the Alt/Ctrl key path of `0x45a274`).
- **R2, wingman orders.** C3_09 HDD map and Alt order keys (suites
  `c3_09-wingman-orders`, `c3_09-orders-matrix`, `c5_05-orders-flyer`).
  What is left needs fight timing (R2+R6) or an order target put out by
  the cheat (R1+R2, a C3_09 cheat suite).
- **R3, scripted joystick.** Journal `joycaps`/`joypos` (suites
  `c1_01-joystick`, `c4_08-joystick`), default mapping only.
- **R4, tape.** `-r` recording is a suite (`c1_01-tape-record`); playback
  (`-p`, `-D`) runs as the held suites `c1_01-tape-play` and
  `c1_01-tape-demo`, stopped below the fault at `0x453127`
  ([oracle_suites.md](oracle_suites.md#tape-playback-suites)); their delta
  is in the [current union](#current-union). What lies after the hold (the
  end of playback, Ctrl+E, bits 3/11 of `DEMO2`, the mouse events of
  `DEMO1`) is still blocked by the fault.
- **R5, another mission or loadout.** The survey of the 61 prepared
  missions names the candidates (type-1 orders: C2_04, C4_07, C4_10, C5_04,
  C5_06, C5_09, C5_10; type-4: C1_05, C2_05, C2_08, C4_02, C4_06, C5_04,
  C5_10, TRAIN2/3/6; actor-scope orders: TRAIN6, C2_05, C4_02, C4_06;
  mode 3: C1_08, C2_04, C3_03, C5_01, C5_03, C5_06, TRAIN2).
- **R6, longer or engage/disengage.** AI timing; hypotheses; a real
  play-through if a scripted attempt fails.
- **R7, options dialog by key.** F12 or Alt+P opens `0x45cff0`
  (dialog `0x4566e0`, run loop `0x456d68`). Its key handler is the dialog
  base `0x454e2c`: Tab (0Fh) and code 52h move the focus forward
  (`0x454d98`, wrapping at the button count `+2CDh`), Shift+Tab back
  (`0x454dbc`; 80Fh, unreachable: Shift is masked), Enter presses the
  focused button (`0x454de8`), Esc the cancel button `+2FBh`. A pressed
  button k stores k at `+356h` (`0x4573c4`) and the run loop dispatches
  table `0x456eab`: actions 0..8 step prefs bytes 0, 1, 2, 3, 7..11
  (`0x45999c` toggles bit 0, `0x4599d4(i, n, …)` counts up modulo n,
  `0x459a10` down; action 8 is `0x4599d4(0Bh, 3, 1)`, the detail byte
  `0x4d1f43` read by `base_parts`), action 9 opens the second dialog
  `0x457d38` (the indexed readers of the prefs block at
  `0x45873e`..`0x4592fc`; not analysed), action 10 (the cancel button)
  stores the prefs file (`0x4574e8` → `prefs_store`) and exits. The F11
  screen `0x457538` (vtable `0x49e4d0`, run `0x457b00`) has one live
  button: Enter on the initial focus 0 closes it (`0x457c4c`). The
  pause/quit dialog `0x4561dc` (`0x456000`): button 0 continues (`+2D1h` =
  2, result 0), button 1 quits (3, result 1), so Ctrl+Q, Tab, Enter ends
  the mission. All answers are pump-keyed. The prefs file is rewritten at
  every exit of `0x45cff0`: a script that steps a byte must step it back
  before its last exit, and the runs hash `data\prefs.cfg` around them.
  Suites: `c3_10-detail-options` (the detail byte 2 → 1 and back),
  `c1_01-quit-dialog` (Ctrl+Q, Tab, Enter: DBSIM exits with 0),
  `c1_01-display-keys` (the F11 screen).
- **R8, display modes by key.** D = `[0x4d24c0]` = `[0x4cf99c]`
  ([display_modes.md](../reverse-engineering/display_modes.md)). Esc
  toggles modes 0/1, F9 requests 5 from mode 0, F1..F6 select the HDD page
  and, per mode, a transition (`hud_key` 432cd3..432d31). F10 gives display
  mode 2 and V mode 4 with the panel off (seen live in
  `c1_01-display-keys`). Entering mode 4 sets the panel-off byte `H+20Fh`
  (`0x429f2f` → `0x431500`), after which `hud_key` returns 0 for every key
  (432be1), so Enter, Esc, N, Tab and Backspace reach `command_key`'s own
  cases.
- **R9, joystick mapping (setup).** The joystick paths of
  `control_record`, `player_input`, `camera_update`, `flight_model`,
  `leg_input`, `slider_event` and Ctrl+1 depend on the mapping bytes of the
  prefs block (bytes 0Ch..24h at `0x4d1f44`.., written with defaults by
  `0x459df0` when byte 0Ch is 0 and a joystick answers) and on bytes 0Eh
  (`0x4d1f46` = 1: lever) and 1Ah (`0x4d1f52` = 2: flight throttle). The
  joystick suites run the defaults. Setting other values needs either a
  prefs.cfg variant in the launch set (a runner/launch decision) or R7's
  action 9 (the second dialog, not analysed).
- **Keys and scenarios:** K-MOUSE (left click on the throttle slider),
  S-DEV (an AI with TARG against the player's ECM), S-FLY (Razor ground
  contact), setup (a clone's `data\keyjoy.cfg`).

### Dead paths and PROVED candidates

Dead (32 sites; verdict lines in
[function_verdicts.md](function_verdicts.md#dead-branch-paths-of-the-36-suite-union)):

- **Key word with a zero low byte** (`control_record` 45a86c behind
  45a87b fall): the key ring `0x6bb6b0` has the single writer `0x477d00`
  in `key_message`; its codes are the `0x4a10ec` table entries (89 nonzero,
  low bytes 01h..58h), +80h for key-up, +200h/400h/800h for Alt, Ctrl,
  Shift; the modifier keys themselves are unmapped (code 0, not queued).
  The 0C000h code blocks of `0x45c0a0` (45c103.., 45c139..) have no
  predecessor.
- **Shift+V** (`command_key` 4600a9 behind 46008f fall): word `0x49eab8`
  (47FFh in DATA) masks every code before `command_key` (`0x45a87d`,
  `0x45ad55`, `0x45b67d`); it has no writer and no base pointer within 64
  bytes. Caveat: a tape's record words were not checked (playback is
  blocked anyway).
- **Byte `0x4d2538`** (`hud_key` 4330de; 4330ca taken only, 432d56 fall
  only; also the `m = 1` test of `0x42a1f0`): about 25 `movsx` reads, no
  write of any width covers it (the neighbours `0x4d2536`, `0x4d2537`,
  `0x4d2539` have byte writes only), no base pointer within 64 bytes, BSS
  zero.
- **`pool_query` side filter** (404bf9 behind 404bed taken only): the only
  caller `0x404ca0` (`0x404d61`) forwards its fifth argument, and its three
  callers `0x416407`, `0x416563`, `0x41dccc` push −1; neither function has a
  dword reference in the file.
- **`[0x49ef34]` != 0** (no writer): `weapon_prepare` 40e7ef/40e88d,
  `flyer_hit_test` 421cb6 and `cockpit_sync` 41b192 (41b18a taken only).
- **Smoke:** no `pool_alloc` receives the SMOKE pool, and SMOKE_BALL objects
  are allocated only inside `smoke_update`: `smoke_update` 10,
  `smoke_ball_update` 2 and the two update loops of the logic frame,
  `logic_frame` 45f60c/45f64d (45f63a and 45f67b fall only).
- `cockpit_select` 7 (T = 0 from every live caller), `condition_ratio` 2
  (selector 0 from every caller) and `heading_sin` 1 (no reference to
  `0x47e027`).

**No PROVED line exists for these edges.** The in-vitro harness accepts a
PROVED edge only when its cases never observe the other edge (`coverage.h`:
"feasible edge missing or impossible edge observed" fails the run), and
every hook group drives each of its branch sites both ways with direct cases
on arbitrary arguments and globals. A probe line
`PROVED(condition_ratio,0x4152fe,1,…)` (the selector is 0 from every caller)
failed `differential_vehicle_test` with `fall=13107 taken=3277`. The edges
proved infeasible from the listing are therefore candidates for the in-vitro
cases of their groups, not lines that can be appended alone; a PROVED line
would require dropping the cases that drive the infeasible state, which
would weaken the differential of the replacement:

| Hook | Site | Only feasible edge | Proof |
| --- | --- | --- | --- |
| `weapon_prepare` | 40e7e7, 40e885 | taken (1) | `[0x49ef34]` has no writer |
| `flyer_hit_test` | 421cae | taken | as above |
| `cockpit_valid`, `cockpit_arc` | 43318a, 433261 | taken | as above |
| `command_key` | 460836 | taken | as above |
| `cockpit_sync` | 41b18a | taken | as above |
| `condition_ratio` | 4152fe | taken | selector 0 from every caller |
| `cockpit_select` | 4332eb, 43338f | fall (0) | T = 0 from every live caller; `P+210h` cleared at 4332ef |
| `pool_query` | 404bed | taken | side −1 from every caller |
| `control_record` | 45a87b | fall | no zero-low-byte key word |
| `hud_key` | 432d56 / 4330ca | fall / taken | byte `0x4d2538` has no writer |
| `logic_frame` | 45f63a, 45f67b | fall | both smoke pools stay empty |
| `mech_attacked`, `hunt` | 41fa08, 41e2e4 | fall | state 4 implies an order target (block copies into the actor not checked) |

### Comparing the next union

The baseline is the union after the tape delta:
`run-oracle-parallel.ps1 … -LiveCoverage run -CoverageMode edge
-CoverageBaseline <es2-data>\logs\tape-coverage-live\edge-delta\coverage-union.json`
for the new suites, then `suite_coverage_delta.py <baseline> <union>`. A
site classified **input** counts as confirmed when the delta shows its
intended outcome after the suite named in its row; a **dead**,
**defensive** or **unarmed** site that shows any outcome refutes its row
(correct the row and the function_verdicts line). The per-hook tables below
list every never site with its class, joined by hook and VA.

The two tape suites (`c1_01-tape-play`, `c1_01-tape-demo`, held below the
playback fault) were measured this way against the 40-suite union:

```powershell
Import-Module .\tools\reference-game\setup\Es2Bazel.psm1 -Force
$prepared = New-Es2PreparedProxy -Directory "$repo\build\claude\prepared-<work>"
$base = '<es2-data>\logs\u-12-live\edge-delta\coverage-union.json'
& .\tools\reference-game\setup\run-oracle-parallel.ps1 -InstallName @('timing-r111','timing-r111-p1') -PreparedProxy $prepared `
    -Suite 'c1_01-tape-play','c1_01-tape-demo' -LiveCoverage run -CoverageMode edge -CoverageBaseline $base -HiddenWindow -OutputDirectory <dir>\edge
& <es2-dependencies>\python\python.exe -B tools\reference-game\scripts\suite_coverage_delta.py $base <dir>\edge\coverage-union.json --output <dir>\edge\coverage-delta.json
```

That baseline covers 3,776 of the 4,676 sites, so the delta lists the gains
over it and reports the other sites as newly instrumented (what the tape
suites reach, not a delta). The result is in the tables below and in the
playback targets table of [oracle_suites.md](oracle_suites.md#tape-playback-suites).

### Per hook

All 87 hooks with never sites, most sites first.

#### `control_record`

40 never: input 39, dead 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 45a86c | dead | skip loop for a key word with a zero low byte (45a87b fall only). No source makes one: the ring `0x6bb6b0` has the single writer `0x477d00` in `key_message`, whose codes are table entries `0x4a10ec` (89 nonzero, low bytes 01h..58h) plus 80h for key-up and 200h/400h/800h for modifiers; VK_SHIFT/CONTROL/MENU are unmapped (code 0, not queued); the 0C000h blocks of `0x45c0a0` have no predecessor. Ctrl alone at 850 in `c1_01-keys-extra` leaves it never | — |
| 45a8a7, 45a930, 45a938, 45a951, 45a95a, 45a964, 45a99e, 45a9e2, 45aa3d, 45aa4a, 45aa57, 45aa64, 45aa71, 45aa7e, 45aa8b, 45aaa1, 45aaaf, 45aabd, 45aacb, 45aad9, 45aae7, 45aaf5, 45ab03, 45ab11 | input | tape playback (`0x4d24d6`, `-p`/`-D`): the discoveries of `c1_01-tape-play`/`c1_01-tape-demo` both fault at `0x453127` (the ninth pending entry of the timed-widget list `0x4530bc`, panel `+356h`) after 686/920 frames, so the suites are held at 650/880 ticks. Seen in the tape delta: 45a8a7, 45a930, 45aa3d, 45aa4a, 45aaa1, 45aaaf, 45aabd, 45ab11 both ways; 45a938, 45a95a, 45a99e taken; 45a951, 45aa57, 45aa64, 45aa71, 45aa7e, 45aa8b, 45aacb, 45aad9, 45aae7, 45aaf5, 45ab03 fall; 45a964 and 45a9e2 stay never | R4: beyond the hold, blocked by the playback fault (which tape presses fill the list; whether the original faults outside the oracle): the end of playback (45a964, the fall edges of 45a938/45a95a/45a99e, 45a951 taken), `DEMO2` bits 3/11 at record 663, the events of `DEMO1` (45a9e2) |
| 45abc3, 45ac04, 45ac45, 45ac49, 45ac5b, 45ac78, 45accb, 45af0a, 45af56, 45afa2, 45afee, 45b00f, 45b612, 45b61b | input | joystick paths of the mapping modes other than the default: `c1_01-joystick`/`c4_08-joystick` run the default mapping set only (Z and R sweeps had no effect), so the m0..m3 variants, second axis sources, hat modes 1/2 and the mode-2 camera pair stay never | R9: a non-default joystick mapping (prefs bytes 0Ch..24h) with the joystick suites' program |
| 45b641 | input | option dword `0x49ea94` (Backturn), set only by `keyjoy_load` `0x45b7a8` when `data\keyjoy.cfg` says "Reverse" | setup: a clone whose keyjoy.cfg reverses Backturn, any forward-throttle suite |

#### `player_input`

12 never: input 12.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 460e48, 460e55, 460e63 | input | button action 5 (lever toggle): the joystick suites press buttons 1..8, but in the default mapping no button carries action 5, and byte `0x4d1f46` must be 1 | R9 (mapping with action 5 on a button, prefs byte 0Eh = 1) + R3 |
| 460ecf, 460f08, 460f0b, 460f11, 460f8f, 460fa6, 460fa9, 460fac, 460faf | input | button actions 11, 12, 15: not in the default mapping of the joystick suites (action byte `0x4d1f38[(i+s+4)&FFh]`) | R9 (buttons mapped to 11/12/15) + R3; action 15 also needs frame counter >= 50 |

#### `ram_step`

10 never: input 10.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41e591, 41e5a6, 41e5af, 41e5c6, 41e5ed, 41e62f, 41e643, 41e68e, 41e6e1, 41e71b | input | MECH state "ramming" (`0x4997c2`), chosen by `order_resume` for group order type 1 (C2_04, C4_07, C4_10, C5_04, C5_06, C5_09, C5_10 in the mission survey); `c4_10-probe` (600 frames) never resumed such a group | R5: C4_10 longer (until the type-1 group resumes its order) or C2_04 (open which group and when) |

#### `smoke_update`

10 never: dead 10.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 4092f7, 409313, 409328, 409330, 40933b, 40937f, 409386, 4093a1, 4093d5, 40942d | dead | no `pool_alloc` receives the SMOKE pool `[0x4a9684]` (five code references, none an allocation), so `smoke_update` never runs in any game | — |

#### `trigger_scan`

10 never: defensive 10.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 426d6d, 426d7f, 426d93, 426da5, 426db7, 426de3, 426df5, 426e09, 426e17, 426e29 | defensive | trigger types 5 and 6: none in the 62 MSN members; types 2..4 are reached by `c4_05-convoy`/`c5_05-moon-missiles` | — |

#### `cell`

9 never: defensive 9.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 4272bf, 4272ce, 4272d3, 4272d9, 4272de, 4272e4, 4272f9, 427302, 42730d | defensive | multi-cell path: no caller passed a radius above 2^(shift-1) (hypothesis: no retail shape is wider than half a terrain cell); 40 suites agree | — |

#### `cockpit_select`

7 never: dead 7.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 433301, 43331b, 433329, 433337, 43334e, 433368, 433387 | dead | T != 0 path: caller `0x432ea1` (key `;`) pushes 0; `0x43d7d2` runs only with byte `0x49be74` set (0 in DATA, only cleared) | — |

#### `prefs_video`

7 never: unarmed 7.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 45e522, 45e541, 45e557, 45e570, 45e582, 45e584, 45e58a | unarmed | start-up only (WinMain `0x4653c9`, option `-v` `0x45e72e`), before the mission-load call `0x45f42b` where even early arming installs the coverage traps ([file_io.md](../reverse-engineering/file_io.md#startup-and-configuration)): never observable | — |

#### `track_target`

7 never: input 7.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 478798, 4787a3, 4787aa, 4787c4, 4787cf, 4787d6 | input | the non-cyclic clip class ANSequence (`0x478760`/`0x47877c`): every live clip in 40 suites was ANCyclicSequence | R6 (hypothesis): identify the ANSequence users of the factory `0x4913f0` first |
| 4796bf | input | track entry word +1Ch after 4796b8 (clip state; hypothesis) | R6 (hypothesis), as above |

#### `attack_plan`

6 never: input 6.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41d08f, 41d0a6 | input | replan in state 4/6 with `A+248h` != `A+1A4h` (41d086 taken only): in `c3_09-orders-matrix` no ordered wingman switched away from its order target during the fight; 41d0a6 taken needs state 6 without an earlier attack order | R1+R2 (C3_09 cheat suite, open): attack order, then Ctrl+N to another hostile and Alt+Ctrl+D so the wingman's current target changes |
| 41d0eb, 41d0f4, 41d10b, 41d131 | input | the order target out in state 4 (41d0d4 taken only): the orders-matrix fight never put an order target out while its wingman still held state 4 | R1+R2: attack order (Alt+A / map A), then destroy that target with the cheat (C3_09 with `-SPRUNKNOWN`, own golden) |

#### `mech_attacked`

6 never: input 6.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41f939 | input | step 8 with a found attacker (41f931 taken only in `c3_10-hunt-targ`: `target_search` returned 0 every time) | R1 on a hunting MECH (hypothesis on the script flag +2) |
| 41fa08 | input | step 9 in state 4: fall reachable (an ordered wingman attacked after 8,000 damage), taken infeasible: `A+248h` is written nonzero with every store of state 4 (codes 0, 2, 0Bh) and zero only at `0x41e0cd` in the unreachable second body of `drive_off` | R2+R6: a wingman under an attack order that takes 8,000 damage (fall only) |
| 41fa1a, 41fa2c, 41fa3a | input | step 9 with order state 4 and its order target's out bytes (41f9ff taken only) | R2+R6: attack order in C3_09, the enemy returns fire on the wingman |
| 41fab2 | input | no new target while the old one was set: `order_resume` | R6 (hypothesis) |

#### `spawn_point`

6 never: defensive 6.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 423617, 423629, 423644, 42365a, 42366b, 42366f | defensive | check != 0 only from `group_activate` modes 4/5, absent from the 62 MSN members | — |

#### `stream_open`

6 never: defensive 6.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 47317f, 473184, 47319a, 4731ac, 4731f4, 473203 | defensive | member kinds 7 and 9 (47317a taken only: kind 2): every outer member of the CD's VOL files is kind 02 (vol.md), so the decompressing streams (`+27h` 92h bytes, `+2Bh` 28h bytes) are never created | — |

#### `vehicle_update`

6 never: input 6.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41acbd | input | lock roll against a target with an active device (`T+A1h`): `c3_10-hunt-targ` switches the player's ECM on, but the roll needs an attacker with TARG against that target; which hostile carries TARG/ECM is not known | S-DEV (open: a mission whose AI carries TARG) |
| 41aef1, 41aef6, 41af22, 41af34, 41aff3 | input | weapon tally words 4 and 2 nonzero: AI vehicles carrying weapons of those classes | R5 (class-to-weapon mapping open) |

#### `stream_seek`

5 never: defensive 5.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 473416, 47341b, 473468, 4734bf | defensive | seek on a kind-7/9 member: as `stream_open` | — |
| 4733e1 | defensive | a failed `_lseek` on a loose stream (4733cf taken only: no loose file is ever seeked; the suites seek only VOL members) | — |

#### `subject_status`

5 never: input 5.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 412dfe, 412e13, 412e2f, 412e39, 412e45 | input | actor subjects (412dd9 fall only: every subject was a group); `subject_actor` 0 in all suites. The demo mission of `DEMO1` (`c1_01-tape-demo`) has one: 412dd9 taken, 412dfe fall, 412e13 taken, 412e2f fall in the tape delta; 412e39, 412e45 stay never | R5: a mission with an actor-subject group objective (survey of the order/objective subjects open; candidates the actor-scope missions of the mission survey) |

#### `tape_restore`

5 never: unarmed 5.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 45cd70, 45cd9b, 45cda4, 45cdc5, 45cde1 | unarmed | option parser `-p`/`-D` at start-up (and the playback suites fault at `0x453127`) | — |

#### `hud_key`

4 never: input 3, dead 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 432cf8 | input | F1..F6 in display mode 4 or above (432cf6 taken only): taken in mode 4 while the panel-off byte `H+20Fh` is clear, fall above 4. `c1_01-display-keys` reaches modes 0..3 by key (432cf0 both ways); F10 gives mode 2 and V gives mode 4 with the panel off, where `hud_key` returns 0 for every key (hypothesis: mode 4 with the panel on is not reachable by key) | R8 (open: a way into mode 4 with the panel on) |
| 432dea, 432dec | input | Esc pressed only in display modes 0/1 (432de4 taken only) | R8: Esc in a side view (2/3: 432dea taken) and in mode 4 or above (432dec; F10 gives mode 2, F9 requests 5) |
| 4330de | dead | byte `0x4d2538` = 1 needed (4330ca taken only, 432d56 fall only): no instruction writes the byte (about 25 `movsx` reads; the neighbours `0x4d2536`/`0x4d2537`/`0x4d2539` have byte writes only, no base pointer within 64 bytes, BSS zero) | — |

#### `camera_update`

4 never: input 4.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 401243, 4019e9, 4019f9, 401a22 | input | camera mode 3 (replay, `+36h` = 3): requested only by button action 15 (`0x460fe5`); the joystick suites press buttons 1..8 in the default mapping, which has no action 15 | R9 (a button with action 15) + R3, in view mode 2 or 3 |

#### `flyer_attack`

4 never: input 4.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41c9fb, 41ca04, 41ca12, 41ca1b | input | entered in `c5_05-orders-flyer`; these four need the attacked flyer's relative word `-14h` below 2000 (41c9f3 taken only): a flyer passing close above the attacker (hypothesis) | R6: a longer flyer engagement (c5_05-orders-flyer beyond 1400) |

#### `flyer_engage`

4 never: input 4.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 422c33, 422c41, 422c4a, 422c61 | input | the member walk sees only the engaging flyer itself (422c27 taken only): single-flyer groups | R5: a mission with a multi-flyer group that engages (open) |

#### `flyer_follow`

4 never: input 4.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 422622, 422639, 422642, 422677 | input | FLYER follow state (callers `0x422a6d`, `0x422cf4`), never entered | R5: a multi-flyer group with a follow order (open) |

#### `initial_damage`

4 never: input 4.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41792f, 41797e, 4179aa, 4179b9 | input | authored MECH condition words 0..39 (417920 fall only: every armed mission had 40..79 or none) | R5: a mission whose authored mechs carry condition < 40 (survey of the MSN actor records open) |

#### `mech_order`

4 never: input 4.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 420c15 | input | code 0 to a member in state 4: `c3_09-orders-matrix` sent Alt+A at 2987..3011, but no member was in state 4 then (420c0d taken only) | R2: Alt+A while a wingman holds an attack order (a map A order seconds before), then `'` to another target and Alt+A again |
| 420eb4, 420ec6 | input | code 2 with an attacker of the player inside 60,000: the Alt+H presses at 2765..2810 found none (420ea0 fall only) | R2: Alt+H after 2850 (h3/h4 target the player inside 60,000 from about 2850 in the C3_09 golden) |
| 4211dc | input | code 0Bh to a member in state 4 (4211ce taken only), as 420c15 | R2: map A on the target a wingman already attacks, then on another |

#### `order_scan`

4 never: input 4.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41d8c8, 41d8d1, 41d8e8, 41d908 | input | the out branch (41d8b1 fall only: the scanning actor is out; `target_select(A, 0, 0)` and the retreat table), not an order state | R1: disable (not destroy) an ordered/scanning MECH (C3_09 cheat suite, open) |

#### `player_route`

4 never: input 3, defensive 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41c286 | defensive | mission mode 5: no mission has it (survey of the 61 prepared missions) | — |
| 41c343, 41c35b, 41c38e | input | mode-3 escort chain inside the range: `c1_08-escort-route`/`c1_08-scan` run mode 3, but the player was never within 10,000 of the order subject with the heading inside +-2000h (41c301 taken only) | R5: C1_08 escort: drive behind the order subject within 10,000 for about 110 frames (route data of the mission survey) |

#### `ram_move`

4 never: input 4.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41e4f8, 41e504, 41e52b, 41e545 | input | movement member of the ramming state (as `ram_step`) | R5, as `ram_step` |

#### `tape_save`

4 never: unarmed 4.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 45ccc5, 45cd04, 45cd0d, 45cd4b | unarmed | option parser `-r` (`0x45ec25`..`0x45eca3`) at start-up: `c1_01-tape-record` runs it, but before arming | — |

#### `targ_aim`

4 never: input 4.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 40e568 | input | TARG damage word `W+7Fh` above 104 (40e54e taken only in `c3_10-hunt-targ`): the damaged-computer timer | R1: Alt+Ctrl+D on the own TARG part (launch `c3_10-targ-eo` with `-SPRUNKNOWN`) |
| 40e595, 40e5a2, 40e5e6 | input | the fallback path (lock word `W+7Dh` = -1 or `W+7Fh` >= 155; 40e4f1/40e4fd fall only): a target without a lockable part or a broken TARG | R1 as 40e568 (W+7Fh >= 155), or a TARG lock on a building (part lookup -1, hypothesis) |

#### `vehicle_part`

4 never: input 4.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 4180e5, 418128 | input | a vehicle whose ratio list words +14h/+16h are negative (the 100h default; hypothesis: types with more than two ratio states) | R1 on such a vehicle type (which types: open) |
| 418615, 418651 | input | destroyed vehicle with attachments `+238h`/`+23Ch` (4185d7 taken only) | R1: destroy such a vehicle (type open) |

#### `command_key`

3 never: input 2, dead 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 4600a9 | dead | Shift+V (82Fh, 46008f fall only): every path into `command_key` masks the code with word `0x49eab8` = 47FFh (`0x45a87d` record word, `0x45ad55` queue, `0x45b67d`), which has no writer and no pointer within 64 bytes; the window procedure passes 410h, button action 6 passes 14h (caveat: a tape's record words were not checked) | — |
| 4607f0, 4607fa | input | Ctrl+1 (402h) pressed only without a joystick (`c1_01-keys-extra`, 4607e7 taken only); needs summary byte +4 (device 0 with Z, or device 1 answering) and prefs byte 0Eh (`0x4d1f46`) = 1 | R3 + R9: Ctrl+1 twice in a joystick suite with prefs byte 0Eh = 1 |

#### `dts_load`

3 never: defensive 3.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 47a72b | defensive | the block at 47a724 runs only for shapes that take 47a6f2's taken edge, which no loaded shape does; when reached only the taken edge exists (PROVED: `[0x4a1694]` stays 0) | — |
| 47b3ac, 47b3b2 | defensive | `0x47b3a0` is called only from the refill paths of `0x47b4b4` (47b4c7/47b4d7 taken only: every read fits the buffer); PROVED one-way when reached | — |

#### `hunt`

3 never: input 3.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41e258, 41e271 | input | hunt step with a held target (41e24f taken only; PROVED pair) | R6 (hypothesis) |
| 41e2e4 | input | state 4 without an order target: taken infeasible (state 4 implies `A+248h` != 0); the fall edge needs a hunting wingman in state 4 (41e2db taken only in `c4_08-razor-plasma`) | R2: attack order to a wingman that hunts (fall only) |

#### `mouse_dispatch`

3 never: input 1, defensive 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 452dd4 | input | tape playback with byte `0x49dbdf`: the playback suites are held below the fault at `0x453127`; never in the tape delta, the events of `DEMO1` (records 1427, 1498) lie after the hold at 880 | R4 (as `control_record`) |
| 452e11, 452fcb | defensive | drag mode with captured index -1 (hypothesis) | — |

#### `order_resume`

3 never: input 3.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41eb7f, 41eb98 | input | resume with a held target (41eb77 taken only; the pair is PROVED one-way) | R6: engage and disengage (hypothesis) |
| 41ecca | input | the player-group branch with a destroyed order target (member byte `+99h`): the orders-matrix fight did not give it | R1+R2: destroy an order target with the cheat |

#### `sleep_step`

3 never: input 3.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41c427, 41c42b, 41c444 | input | MECH state "sleeping", group order type 4 (C1_05, C2_05, C2_08, C4_02, C4_06, C5_04, C5_10, TRAIN2/3/6 in the mission survey); of the suites only the demo mission of `DEMO1` (`c1_01-tape-demo`) has one: 41c427 taken in the tape delta, 41c42b, 41c444 stay never | R5: one of those missions until its type-4 group resumes |

#### `slider_event`

3 never: input 3.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 4483c6 | input | a slider event with button bit 0 of `0x49db6c` (4483bb taken only: every throttle-slider event came without the left button) | K-MOUSE: left click on the throttle slider (gadget rectangle open) |
| 4483cf, 4483d7 | input | then joystick summary byte +4 and prefs byte 0Eh = 1 | K-MOUSE + R3 + R9 |

#### `weapon_prepare`

3 never: input 1, dead 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 40e7ef, 40e88d | dead | inside `[0x49ef34]` != 0: the dword has no writer | — |
| 40e82a | input | weapon with W+5Fh != 0 (second mount word) | R5 loadout with such a weapon (open) |

#### `member_assess`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41b98a, 41babd | input | the rating body behind `actor_safe(M)` (41b81a) runs in `c1_01-cheat-disable-late` (a hostile engages the wingman when the own Herc is disabled at 1440); these two stay never (41b977 taken only); their conditions are not analysed | open |

#### `condition_ratio`

2 never: dead 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 415300, 415303 | dead | selector 1/2: the only caller chain pushes 0 | — |

#### `contact_scan`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 412a99, 412aca | input | the actor's trigger pointer `+1B2h` set (412a8d/412aba taken only), not byte `+A2h`: no detected actor in 40 suites carried a trigger | R5: a mission whose detectable actor has an attached trigger (survey open) |

#### `engage_done`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41c516, 41c51e | input | the engaged target is out (+A4h/+99h) | R1: disable the AI's target during an engagement |

#### `fire_alloc`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 46b460, 46b480 | input | the FIRE pool full (46b449 taken only): the steal-the-oldest walk over `[0x6b4f30]` by word `+59h` | R6: more simultaneous fires than the pool holds (many building kills in a short time; hypothesis) |

#### `flight_model`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 466b92, 466ba0 | input | joystick throttle lever with prefs byte 1Ah (`0x4d1f52`) = 2: `c4_08-joystick` runs the default mapping | R3 + R9 in C4_08 |

#### `flyer_throttle`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 4222d7, 4222e3 | input | its only caller is `flyer_follow` (`0x422691`), never entered | R5, as `flyer_follow` |

#### `follow_step`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41db0c, 41db25 | input | follow step with a held target (41db08 taken only; PROVED pair); entered in `c4_05-convoy` | R6 (hypothesis) |

#### `group_activate`

2 never: defensive 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 4237c1, 423889 | defensive | activation modes 4 and 5: absent from the corpus | — |

#### `group_scan`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41d62b, 41d644 | input | scan with a held target (41d627 taken only; PROVED pair) | R6 (hypothesis) |

#### `leg_input`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 416117, 416125 | input | lever word `0x49a06e` != 0: Ctrl+1/action 5/slider need the joystick byte and prefs byte 0Eh = 1 (see `command_key` 4607f0) | R3 + R9 + Ctrl+1 |

#### `logic_frame`

2 never: dead 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 45f60c, 45f64d | dead | the logic frame's SMOKE_BALL and SMOKE update loops (`0x45f602`, `0x45f643`): both pools stay empty (see `smoke_update`), so 45f63a/45f67b only fall | — |

#### `playback_key`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 45fe9d, 45fea7 | input | Alt+Enter during tape playback (45fe95 reached only without playback) | R4: a key in a tape suite held below the playback fault (`c1_01-tape-play`/`-demo` press no Alt+Enter and stay never in the tape delta) |

#### `projectile_steer`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 40a2b7 | input | projectile type 2 with the flag 0 | R5 loadout firing type-2 guided projectiles (open) |
| 40a320 | input | target part node with a stale cached matrix | R5 (hypothesis) |

#### `retreat_check`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41cc1e | input | behind the out test 41cbdd; with the out actor of `c1_01-cheat-leader` the call before returned 0 (41cc08 taken only) | R1 on an AI that plans against a building (open) |
| 41cddb | input | threshold compare against A+29Eh (damage-driven retreat) | R1 damage on the planning AI (hypothesis) |

#### `sfx_lip`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 46350e, 46352c | input | lip-script search beyond the first entry (hypothesis: a longer voice message) | R6 (open) |

#### `smoke_ball_update`

2 never: dead 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 409203, 40922c | dead | SMOKE_BALL objects are allocated only inside `smoke_update` (`0x40941b`) | — |

#### `stream_destroy`

2 never: defensive 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 472f9c, 472fdb | defensive | the kind-7/9 objects `+27h`/`+2Bh` are never allocated (472f93/472fd2 taken only); when reached only the fall edge exists (PROVED) | — |

#### `track_route`

2 never: defensive 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 4794df, 479627 | defensive | track callback +2Ch returning nonzero (stop request); none in 40 suites (hypothesis) | — |

#### `travel_step`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41d9eb, 41da04 | input | travelling MECH with a held target (41d9e7 taken only; PROVED pair) | R6 in C4_08 |

#### `base_parts`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 40399c | input | stage 0 of a building part with C+4 = Q+2 = FFFFh (403995 taken only) | R5/R1: destroy that structure type (open) |

#### `actor_safe`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 4138af | input | an enemy with the actor as order target whose route is complete (4138aa taken only) | R5/R6 (open) |

#### `attack_target`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41c152 | input | target class word 3 (`T+1A8h`): `c5_05-orders-flyer` reached class 2 (41c14f taken); no class-3 target known | R5 (open) |

#### `base_update`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 403cee | input | building with an animation counter (C+24h >= 0) | R5 (open) |

#### `beam_dtor`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 40baec | input | a beam destroyed with a linked object `+34h` (40bae3 taken only); PROVED fall when reached | R6 (which beams link an object: open) |

#### `cockpit_sync`

1 never: dead 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41b192 | dead | inside `[0x49ef34]` != 0 (41b18a taken only): the dword has no writer | — |

#### `condition_init`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 415176 | input | the overflow split with q - top above `L[12+i]` (415165 taken only) | R5 (which MEC data gives it: open; hypothesis: heavy initial damage) |

#### `contact_share`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 4127bf | input | as `contact_scan`: trigger pointer `+1B2h` of the spotted actor (4127b3 taken only) | R5, as `contact_scan` |

#### `convoy_leader`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 46a60b | input | the leader search beyond the first member (46a601 fall only: member 0 always qualified) | R6: C4_05 with the first convoy member destroyed (hypothesis) |

#### `convoy_steer`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 46a8d0 | input | convoy byte +227h set: `c4_05-convoy` drives along the convoy but never attacks it | R6: fire at a convoy vehicle in C4_05 |

#### `convoy_think`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 46a740 | input | convoy vehicle with a target (+1A4h): as `convoy_steer` | R6: fire at a convoy vehicle |

#### `explosion_create`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 407fdf | input | explosion kind with a linked object (407fca taken only); PROVED fall when reached | R5/R6 (which kinds: open) |

#### `flyer_hit_test`

1 never: dead 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 421cb6 | dead | inside `[0x49ef34]` != 0 (no writer) | — |

#### `group_death`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 423f9a | input | group kind 0Bh..0Eh | R5 (open) |

#### `heading_sin`

1 never: dead 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 47e047 | dead | no call, jump or dword reference to `0x47e027` | — |

#### `map_areas`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 44d4d3 | input | map mode with byte `M+15Bh` set and `M+104h` (44d4ca taken only) | R2 (which map control sets +15Bh: open) |

#### `mech_fly`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41a253 | input | body ground clamp with region word +0Ch zero (41a1fd taken only): `c4_08-slope-dive` crashed the Razor at about 1300 without the clamp path (the crash came through another contact) | S-FLY variant: a shallower descent onto flat ground (hypothesis) |

#### `node_matrices`

1 never: defensive 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 478ca1 | defensive | node flag bit 1 of a shape hierarchy; no live shape had it (hypothesis) | — |

#### `obstacle_test`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 404b6c | input | object with word +12h > 1 | R5 (open) |

#### `order_point`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 4238f1 | input | actor-scope order point | R5, as `order_target` |

#### `order_target`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 423939 | input | actor-scope group order (TRAIN6 group 1, C2_05, C4_02, C4_06 in the mission survey); the demo mission of `DEMO1` (`c1_01-tape-demo`) gives 423939 taken (and 423933 fall) in the tape delta | R5: one of those missions |

#### `part_position`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41b6f4 | input | the cached-matrix test of the other part path | R6 (hypothesis) |

#### `pool_query`

1 never: dead 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 404bf9 | dead | side filter: `pool_query` is called only from `0x404ca0` (`0x404d61`), which passes its fifth argument, and all three callers of `0x404ca0` (`0x416407`, `0x416563`, `0x41dccc`) push -1; no dword reference to either function, so 404bed is taken only | — |

#### `route_flag`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 413277 | input | route flag search past the tenth entry (413263/413268 fall only: always found in the first pass) | R5 (open) |

#### `sweep`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 426737 | input | the hit actor's trigger pointer `+1B2h` (42672b taken only); `c3_10-own-building-fire` reached 4267cc/4267ec, but its building has no trigger | R5: hit an actor with an attached trigger (as `contact_scan`) |

#### `target_valid`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 411f6f | input | mission mode 3 branch with the order actor (`c1_08-*` run mode 3 but never reached it) | R5: C1_08 escort, as `player_route` |

#### `track_advance`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 478785 | input | ANSequence previous (backward step of a one-shot clip) | R6, as `track_target` |

#### `trigger_fire`

1 never: defensive 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 423472 | defensive | a used slot with an action other than 6: corpus actions are -1 or 6 | — |

#### `weapon_choose`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41f466 | input | fire permission byte A+96h clear with a weapon in range (41f45c taken only) | R2 hold/release around an engagement (hypothesis) |
