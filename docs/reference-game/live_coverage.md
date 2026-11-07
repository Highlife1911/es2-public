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
report=<run-dir>\coverage.json
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
& .\tools\reference-game\setup\run-live-coverage.ps1 -DryRun

# All three suites, all hooks original, saturate.
& .\tools\reference-game\setup\run-live-coverage.ps1 -InstallName timing-r111 -CoverageMode saturate -CoverageHooks all

# Exact counts for a focused follow-up (a fresh output directory is automatic).
& .\tools\reference-game\setup\run-live-coverage.ps1 -InstallName timing-r111 -CoverageMode count -CoverageHooks ground_collide -Suite c1_01-collision
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
report=<run-dir>\probes.json
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
table with mode, every never-reached site by hook); `coverage-union.log`. Per suite in `<pass>\<suite>\` (the regression directory
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
`OutputDirectory` before it starts anything.

**Tests.** `//tools/reference-game/scripts:live_coverage_union_unit_test`
(synthetic trees: valid unions of both kinds with frame-limit and exit suites,
commented-out sites, mode parsing, one perturbed suite invalidating only the
union, six rejected reports, the counting-mode check) and
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
python -B tools\reference-game\scripts\branch_edges.py `
    build\extracted\cd\VER95\DBSIM.EXE tools\reference-game\proxy\live_coverage_edges.h
```

**Baseline (known outcomes).** `run-oracle-parallel.ps1 -LiveCoverage
run|original -CoverageMode edge -CoverageBaseline <coverage-union.json>`
writes `coverage-known.txt` (`live_coverage_union.py --write-known`: one
line per site with a known outcome) and names it in every proxy INI
(`known=`). Those outcomes are never watched; the report echoes them per
site (`known_fall`, `known_taken`, `known_outcomes`), the union validates
the echo against the baseline and counts them as seen. Such a union is
"baseline plus this pass".

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
| `proxy/probe_entry_inventory.h` (`probe_instructions`, 24,202 `{va, opcode}` rows; `probe_entry_kinds`) | `scripts/probe_instruction_inventory.py` | the reference `DBSIM.EXE` only (hash gated) | `scripts:probe_instruction_inventory_unit_test` | never for a hook merge; only if the rule of the script changes |
| `proxy/live_coverage_edges.h` | `scripts/branch_edges.py` | `DBSIM.EXE`, `invitro/branch_sites.h`, `hook_registry.h` | `scripts:branch_edges_unit_test` | after every merge that adds `BRANCH` lines or hooks (coordinator) |
| the two marked regions of `proxy/callee_counters.h` (`legacy_graph_counters` list and the `#define <group>_hook_counters legacy_graph_counters` lines) | `scripts/legacy_graph_counters.py` | `hook_registry.h`, `proxy/*_hooks.h`, the counter arrays in `callee_counters.h` | `scripts:legacy_graph_counters_unit_test` | only if a hook of one of the 29 legacy groups is added or a group gains its own counter function |

**The inventory.** The registry is not an input, so no merge changes it.
The currency check runs in the scripts tier together with the synthetic-PE
cases (linear desync behind a jump table and zero padding, overlap refusal,
hash gate), so it is cached on the CD content digests of `@es2_data` and
skips without `ES2_DATA_ROOT`.

```powershell
python -B tools\reference-game\scripts\probe_instruction_inventory.py `
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
python -B tools\reference-game\scripts\legacy_graph_counters.py tools\reference-game\proxy
```

## Current union

Edge mode over the 40 suites without the two tape suites, every hook
original, 507 hooks, 3,776 sites: a 36-suite pass on five clones with hidden
window (every suite identical to its golden, 11 min 41 s) plus a pass of
`c1_01-cheat-disable-late`, `c1_01-display-keys`, `c3_10-detail-options` and
`c1_01-quit-dialog` against it as baseline (all four identical); the
`coverage-union.json` of the second pass is the union of the 40 suites.

**2,397 of 3,776 sites both ways, 647 fall only, 430 taken only, 302
never; 5,871 of 7,552 outcomes seen; 431 of 507 hooks have sites** (76
hooks are branchless).

The tape suites (`c1_01-tape-play`, `c1_01-tape-demo`, measured against the
40-suite union as baseline, both identical) extend it to the whole inventory of
the registered hooks: **2,745
of 4,676 sites both ways, 868 fall only, 559 taken only, 504 never; 6,917
of 9,352 outcomes seen; 577 of 789 hooks have sites**. 27 of the 302 never
sites are seen in them ([per hook](#per-hook)); 229 of the 900 sites the
40-suite union did not instrument stay never in the 42-suite union, which
measures them in the two tape suites only. Its `coverage-union.json` is
the baseline for a further pass.

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
lists as never reached in either direction is classified by hook
([per hook](#per-hook)); the rows of the 27 that the tape suites reach name
what they saw.
Sources: the union and the per-suite `coverage.json` files of the two runs
(first suites, per-suite edges), the suite sections of
[oracle_suites.md](oracle_suites.md) with their live results, the hooks'
contract sections (linked from the [registry](hooks.md#registry)), and an
`llvm-objdump -d -M intel` listing of the reference `VER95/DBSIM.EXE`
(checked; image base `0x400000`) with raw dword
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

The recipe column of the per-hook classification names these inputs.

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
  `c1_01-tape-demo` (the latter stopped below the fault at `0x453127`,
  [oracle_suites.md](oracle_suites.md#tape-playback-suites)); they are part
  of the [current union](#current-union). What lies after the hold (the
  end of playback, Ctrl+E, bits 3/11 of `DEMO2`, the mouse events of
  `DEMO1`) is blocked by the fault.
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

The baseline is the union that includes the tape suites: a pass for new
suites runs `run-oracle-parallel.ps1 … -LiveCoverage run -CoverageMode edge
-CoverageBaseline <coverage-union.json>`. A site classified **input** counts
as hit when the union shows its intended outcome after the suite named
in its row; a **dead**, **defensive** or **unarmed** site that shows any
outcome refutes its row (correct the row and the function_verdicts line). The
per-hook classification lists every never site with its class, joined by hook
and VA.

The two held tape suites (`c1_01-tape-play`, `c1_01-tape-demo`) were
measured this way against the 40-suite union:

```powershell
Import-Module .\tools\reference-game\setup\Es2Bazel.psm1 -Force
$prepared = New-Es2PreparedProxy -Directory "$repo\build\claude\prepared-<work>"
$base = '<coverage-union.json of the 40-suite union>'
& .\tools\reference-game\setup\run-oracle-parallel.ps1 -InstallName @('timing-r111','timing-r111-p1') -PreparedProxy $prepared `
    -Suite 'c1_01-tape-play','c1_01-tape-demo' -LiveCoverage run -CoverageMode edge -CoverageBaseline $base -HiddenWindow -OutputDirectory <dir>\edge
```

That baseline covers 3,776 of the 4,676 sites; the other sites are newly
instrumented (what the tape suites reach). The result is in the per-hook
classification and in the playback targets table of
[oracle_suites.md](oracle_suites.md#tape-playback-suites).

### Per hook

The site list of the union is the "Sites never reached" section of
`coverage-union.md`, written by `scripts/live_coverage_union.py` into the
run directory. The classification of all 302 sites by hook (87 hooks: sites,
class, reason, recipe, and what the tape suites saw) is kept with the
oracle evidence in `es2-data`. Hooks that other documents cite:

#### `camera_update`

4 never: input 4.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 401243, 4019e9, 4019f9, 401a22 | input | camera mode 3 (replay, `+36h` = 3): requested only by button action 15 (`0x460fe5`); the joystick suites press buttons 1..8 in the default mapping, which has no action 15 | R9 (a button with action 15) + R3, in view mode 2 or 3 |

#### `member_assess`

2 never: input 2.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 41b98a, 41babd | input | the rating body behind `actor_safe(M)` (41b81a) runs in `c1_01-cheat-disable-late` (a hostile engages the wingman when the own Herc is disabled at 1440); these two stay never (41b977 taken only); their conditions are not analysed | open |

#### `base_parts`

1 never: input 1.

| Sites | Class | Reason | Recipe |
| --- | --- | --- | --- |
| 40399c | input | stage 0 of a building part with C+4 = Q+2 = FFFFh (403995 taken only) | R5/R1: destroy that structure type (open) |
