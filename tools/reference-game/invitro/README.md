# Private-image differential tests

`//tools/reference-game/invitro:differential_harness` is an own-code 32-bit executable that maps
the hash-checked original `DBSIM.EXE` into its own process, calls the original functions behind
all registered hooks on generated inputs and compares them with our `core/` code: result
registers, every declared state byte, guard bytes around it and, where relevant, the log of calls
into game code. It never starts the game, calls the original entry point, loads an original DLL
through the Windows loader or saves original code or tables (`--rtl` maps `CW3220.DLL` privately,
as it maps DBSIM; see [CW3220 runtime passes](#cw3220-runtime-passes)).

Every original branch site of the hooked functions must be covered in both directions or proved
unreachable, and every hook has a caught mutation ([MUTATIONS.md](MUTATIONS.md)). Every run prints
its inventory (`<n> cases`, `PROVED:` lines, branch sites); `check_shards.py` adds up sharded logs.

## Run

```powershell
bazel test //tools/reference-game/invitro:differential_test   # quick tier, part of `bazel test //...`
bazel test //tools/reference-game/invitro:differential_full   # full tier, on request only
$env:ES2_DATA_ROOT = 'build\extracted\cd'        # local.bazelrc sets it for Bazel runs
& python -B tools/reference-game/invitro/check_signatures.py
```

The harness is built once as `:differential_harness`. The tests are small launchers (`launch.c`)
that run it with their arguments, so a change in one core module reruns only the groups that
depend on it:

| Target | Arguments | Content |
| --- | --- | --- |
| `:differential_<group>_test` | `--quick --group <group>` | quick tier, `size = "large"`, shards from `groups.bzl`; in `//...` |
| `:differential_<group>_full_test` | `--group <group>` | full tier, `size = "large"`, `manual`, shards from `groups.bzl` |
| `:differential_probes_test` | `--probes` | chain, contract, poison and weapon-mode probes |
| `:differential_test`, `:differential_full` | suite | all group tests, probes, rtl tests and sim suites (quick / full) |
| `:groups_test` | `check_groups.py` | `groups.bzl` names exactly the registry's groups with admissible shard counts |

The groups are those of `proxy/hook_registry.h`. Two label stems differ (`vehicle_upd`,
`input_disp`): Windows refuses to start executables named `*update*`/`*patch*` (error 740).
`groups.bzl` holds each group's full-tier shard count (`1 <= shards <= members`,
`12 * shards >= members`); a new group adds a line, and `groups_test` reports a count that no
longer fits.

**Sim suites.** A suite compares a module's original machine code with our code in `sim/src`
through the full service dispatch. It has its own binary `:differential_<stem>_harness`
(`sim_main.c` plus a generated `sim_suite.h`, see `suites.bzl`) and its test
`:differential_<stem>_test`, which runs it without arguments. A suite's case header starts with
`#pragma once` and includes the headers it builds on, so a binary compiles only that closure;
`harness_base.h` holds what both harnesses share (start-up reservation, hash-gated mapping, case
helpers, fault handler). Linking is reproducible (`-Wl,--no-insert-timestamp`), so a binary whose
objects did not change keeps its bytes and its cached result: a change in `sim/src` or
`sim/dispatch` reruns the suites but not the hook groups, a change in `core/`, the hook registry
or a hook-group case header reruns the hook groups, probes and rtl tests but not the suites. A new
suite is a case header plus one line in `suites.bzl` (header, entry expression, test shards, size,
extra data; sorted by stem). With `shards > 1` the parts are split over the Bazel test shards by
`suite_part_in_shard()` (`suite_sharding.h`); chunked suites run chunks of 500 cases from their
own seeds, so cases do not depend on the shard layout. Every sim test is tagged
`resources:membw:1`, and `.bazelrc` runs at most three at once (`--local_resources=membw=3`): the
suites are bound by memory bandwidth.

**Per-case memory traffic.** Per case and side a suite resets DATA (`su_reset`, 0x235000 bytes),
the 16 MiB heap region (`ir_heap_reset`), the loop's 1.2 MB frame bits (`lc_reset`) and, where
files are modelled, the 4 MiB runtime region (`su_files_reset`), then snapshots DATA and the used
heap (`su_take`) and compares the snapshots (`su_compare`). No step touches a whole buffer. Heap,
frame bits and runtime region are `MEM_WRITE_WATCH` allocations (`ww_alloc`); `ww_clear` zeroes
only the pages `GetWriteWatch` lists. DATA lies in the start thread's stack reservation and cannot
be write-watched: `su_reset` and `su_take` compare it page by page against the pristine image
(`ww_page_differs`, no early exit so that it vectorises) and restore or copy only differing pages;
`su_reset` rescans DATA instead of trusting `su_take`'s list, because harness code may write DATA
in between. `su_take` keeps per side the set of snapshot pages that may differ from their base and
`su_compare` compares only those; `su_snap_same` marks the pages it writes.
`ES2_INVITRO_CHECK_RESET=1` checks every shortcut against the full operation (cleared buffers are
zero, DATA equals the pristine image after `su_reset`, the snapshot equals DATA and the used heap
after `su_take`, the page compare equals `memcmp`); a disagreement prints `FAIL:` and exits 1.
Without it, a clear that skips a written page, a half-page restore or a stale snapshot page can
pass a suite.

**Tiers.** The full tier runs every generator's own count (about one million random cases after
its boundary prefix). The quick tier (`--quick`) caps the random budget per hook at 100,000 cases
(`ES2_INVITRO_QUICK_CASES` in `harness_base.h`, applied by `case_budget()` in every generator).
Every coverage window (at most 65,536 cases) and every boundary prefix lies inside the cap, so the
quick tier requires the same branch-site coverage and proved edges as the full tier and differs
only in the random tail. Installed-adapter runs (256 cases per function and mode) and the explicit
`ES2_INVITRO_<GROUP>_CASES` overrides (`_ADVANCE_`, `_ACTOR_`, `_TERRAIN_`, `_COPY_`, `_GROUND_`,
`_COLLIDE_`, `_EFFECTS_`, `_DAMAGE_`) are not capped.

**Data root.** Without `ES2_DATA_ROOT` (or with an empty value) a test prints SKIP and exits zero
before reading original data. The root accepts exactly one of `VER95/DBSIM.EXE` or
`extracted/cd/VER95/DBSIM.EXE`; ambiguous candidates and paths escaping the root are rejected.
File size and SHA-256 (the pin in `data/reference_hashes.txt`) are
checked on the read buffer before PE parsing, mapping or execution, and all descriptor signatures
and instruction lengths are checked as well (`check_signatures.py` repeats the audit from the
registry).

The built `differential_harness.exe` also accepts:

| Argument | Effect |
| --- | --- |
| a hook name | isolated run of that one function (own reproducible random stream) |
| `--group <group>` | the group's functions in registry order, split by the Bazel shard variables |
| `--quick` | quick tier; combines with `--group` and the `--rtl*` flags |
| `--probes`, `--chain`, `--contract-probes` | the probes; the 40250c leaf-chain test; division boundaries and stack-poison padding probes |
| `--rtl`, `--rtl-heap`, `--rtl-start`, `--rtl-io`, `--rtl-own` | the CW3220 passes |
| `--trap-self-test` | deliberately calls a named import trap |

**Sharding.** The unit of parallelism is the hook group: a group test installs all adapters,
exercises only the group's functions and reports coverage only for their sites (every owned site
still needs both feasible outcomes; impossible edges are explicitly proved). In a full-tier group
test the Bazel shards split the members by `rank in group mod shards`; the probes run in their own
target. A direct run without `--group` selects by registry index from the Bazel shard variables,
shard 0 adds the probes, and without the variables the run is unsharded. Every run starts from seed
`0x48c06e21`, so sharding changes the random subsequences but not the case counts, boundary
prefixes or coverage requirements. `check_shards.py <unsharded-log> <group-or-shard-logs...>`
checks that completed full-tier logs (group logs plus the probes log) add up to the inventory of
an unsharded run; it accepts a parenthesized note after the case count (`line_plane` prints
`1001024 cases (N hits);`).

## Mapping and imports

The harness is linked at `0x10000000` without ASLR and with a 16 MiB PE stack reservation; Windows
places the initial thread's stack at `0x00400000..0x013fffff`. The entry point
(`startup_reservation.h`) claims the original image range `0x00400000..0x006d7fff` either as a
free range or, when it is the uncommitted bottom of the calling thread's own stack reservation, in
place: the image is committed there after the hash gate and the page at `0x006d8000` becomes a
`PAGE_NOACCESS` barrier, so a growing stack faults instead of overwriting the mapping. The initial
thread only starts the 16 MiB test worker and joins it. Anything else at `0x00400000` fails with
its Win32 error and region details. CODE becomes RX, data stays private RW. Releasing the initial
stack and reserving the range afterwards would leave a window in which another thread's stack can
win the range under parallel shards; the in-place claim has none.
`check_startup.ps1 -Executable <exe> -OutputDirectory <scratch> [-Launches <n>]` launches the
harness alone and next to six parallel instances; `:startup_test` covers the free range, the
own-stack claim, barrier failure and foreign, mapped, committed, too-small and unqueryable regions.

There is no fallback rebasing (DBSIM's relocation directory is not needed at the preferred base).
All **134 IAT entries** are replaced by own-code traps that identify the import and fail the run,
except `cw3220.DLL!_memset` and `_memcpy`: those point to own implementations in `probe_call.S`
with the runtime's register results (EAX = destination, EDX = count, ECX = 0). Unexpected CPU
exceptions print the function, code and address and fail without WER dialogs. The RNG seed and the
quarter-angle table are read from the mapped image at run time and never stored. x87 starts from
CW `0x1332` as set by CW3220's startup (hardware reads `0x1372`); each original integer call must
preserve CW, SW and tags.

Functions whose original calls further game code get those callees replaced by stubs in the
private mapping, and core gets the same stubs. Most groups use logging stubs with a per-case
result stream and compare the call log (arguments, entry registers, buffer contents instead of
stack addresses).

## Cases

Every group has a case header `<group>_cases.h` with `G_cases(index, limit)` ([How to add a hook
group](#how-to-add-a-hook-group)). A hook's cases are a boundary prefix followed by seeded random
cases; the quick tier cuts only the random tail. The first groups' generators (behind
`legacy_cases.h`):

| Functions | Cases per function |
| --- | --- |
| RNG, distance, timing, `multiply_q8`, `movement_step`, `hud_speed`, `transform_point`, `vector_subtract`, `distance_display` | 1,000,121 (121 boundary plus 1,000,000 random) |
| `heading_cos`, `heading_sin` | 65,536, the exhaustive 16-bit domain |
| `ground_effects`, the damage functions | 1,001,024 (1,024 boundary plus 1,000,000) |
| `track_clip` | 1,000,025 (25 signed boundary plus 1,000,000) |
| the other first-group hooks | 1,000,000 |
| installed adapters | 256 per function in replaced mode, 256 more in compare mode for hooks that support it |

HUD cases keep the full-width denominator magnitude at least 65,536 so that IDIV quotients stay
valid; zero divisors and faulting quotients are excluded, not normalized. Transforms and vector
subtraction exercise overlapping buffers; installed compare mode skips overlapping span layouts
on purpose (`skipped`) and nested original calls count as `bypassed`. Installed comparisons must
have zero mismatches. Direct checks restore the same arena addresses before running core, so
pointer-derived EAX/EDX are compared without normalization.

**Compare-mode cases.** `compare_probes.h` (included by `main.c` before the group headers) gives a
group's compare-capable members installed compare cases with real callees:
`compare_probe_run(index, 0, build, state, size)` runs 256 cases of the unhooked original against
the hook installed in compare mode (pair and state equal, one call, no skip, no bypass, no
mismatch) and 256 with a skewed replacement (EAX bit 0, or for hooks with spans every other case a
declared byte in the clone, flipped): one mismatch per call, and the caller keeps the original's
pair and state. A group may set `compare_probe_same` (a pair predicate for the unhooked against
the compared call, because frame addresses move with the stack depth) and `compare_probe_skew_edx`
(skew EDX instead of EAX). Which hooks use them:
[compare_mode.md](../../../docs/reference-game/compare_mode.md#in-vitro).

**Probes** (`--probes`). 256 states x all four original/replaced combinations of `ground_update`
and `ground_effects`, including no-track actors; and a real Euler/transform/distance + 434144
state-4 feedback run, which proves incoming EDX survives the real feedback code (a
fabricated-return C stub alone would miss it). `--chain` compares 4,096
actor/manager/pose/matrix/flag arenas following 40250c's leaf order on a zero-track hierarchy.
`--contract-probes` checks the original scalar/ratio #DE boundaries and the enabled/disabled track
division, and runs all five matrix wrappers from an own caller that fills the unused stack with
31/a7: only matrix byte 19 changes, which is the padding contract.

## Branch coverage and probes

`branch_sites.h` lists the original Jcc sites of the hooked functions, found by entry-aligned
control-flow walks. For the first 65,536 cases per function (fewer for some generators, 8,192 for
`track_advance`) the harness replaces each site's first byte with INT3 in private memory; a
vectored handler evaluates the condition on the saved EFLAGS, counts the outcome and continues at
the exact successor. Every byte is restored before the installed-adapter tests; an unobserved
feasible outcome fails the run and names function, site and missing outcome
(`coverage_diagnostics_test`). The exception dispatch writes below ESP, so residue checks of an
uninitialized local deeper than a covered branch are masked in those cases. The same table drives
the opt-in [live coverage](../../../docs/reference-game/live_coverage.md) in the game.

**Early exit.** `coverage.h` restores a site's original byte inside the trap handler as soon as
the site has counted both edges, so the window lengths are upper bounds:

- A site is done when both counts are nonzero, exactly the condition `coverage_report` checks. A
  site missing an outcome stays armed for the whole window; a window that ends with an outcome
  missing fails with the missing edge as witness.
- **Proved sites** (`PROVED` lines) are never done: their check also rejects the impossible edge
  and must keep seeing every hit.
- All sites are armed in every window, whatever hook owns them, so a hook's run also counts the
  sites of the callees it executes. A new window (`coverage_enable(1)`) arms only sites that are
  not done (plus the proved ones); `coverage_enable(0)` restores only armed sites.
- The window (`coverage_active`) opens at case 0 and closes at the runner's fixed case, so every
  runner that reads `coverage_active` (flyer_control's residue check) sees the same cases with or
  without early exit. Disarming in the middle of a case only changes how much of the stack below
  ESP the exception dispatch overwrites; case counts do not depend on it.

`BRANCH ... fall=N taken=M` counts are the hits until both edges were seen (proved and one-way
sites keep full counts); the summary line adds `N sites disarmed at their second outcome`.
Statistics that measure stack residue (`track_advance`'s "residue bytes differing", `track_list`'s
"byte 19 ... differed") depend on how many exception frames clobber the stack. Removing a needed
`PROVED` line makes the hook fail with `missing=<edge>`; flipping a proof's edge fails the
proved-edge check.

## Mutations

```powershell
$env:ES2_DATA_ROOT = 'build\extracted\cd'
& tools/reference-game/invitro/check_mutations.ps1 -OutputDirectory <scratch>
& tools/reference-game/setup/test-hook-registry.ps1 -OutputDirectory <scratch>
```

`check_mutations.ps1` copies only our sources, build files and the module lock into a fresh
scratch workspace, changes one expression per case, builds the harness with Bazel and requires a
named differential failure; a new registry entry without a mutation is rejected. Options, the
generator and what the witnesses prove: [MUTATIONS.md](MUTATIONS.md).

## Evidence

The full-tier logs and the mutation logs are kept in the log root of `es2-data`. Oracle runs are
described in [hooks.md](../../../docs/reference-game/hooks.md#live-validation).

## How to add a hook group

The harness, the proxy and their tests derive every per-group list from `proxy/hook_registry.h`,
whose last `ES2_HOOK` argument is the adapter group. Adding a group `G` touches only:

| File | What to add |
| --- | --- |
| `proxy/<G>_hooks.h` (own) | descriptors (exactly one `es2_hook_descriptor <x>_hook_descriptors[]` per group, any name), extensions, own `callee_counter` arrays and `static callee_counter *G_hook_counters(unsigned hook,unsigned *count)` (array and length of one hook, or NULL and 0 for a hook without game calls; required, also for pure leaves) |
| `invitro/<G>_cases.h` (own) | `static int G_cases(unsigned index,unsigned limit)` for all hooks of the group, plus its helpers |
| `proxy/hook_registry.h`, `proxy/hook-names.txt` | one line per hook, appended |
| `invitro/branch_sites.h` | an own section at the end: `BRANCH(...)` lines and, for edges proved unreachable, `PROVED(hook,site,edge,"reason")` lines (`edge` = the only feasible outcome, 0 fall-through, 1 taken) |
| `invitro/check_mutations.ps1` | one witness per hook at the end |
| `invitro/groups.bzl` | the group's line with its full-tier shard count |
| `core/` and `docs/` | the module, its test and its contract; in `core/BUILD.bazel` one `core_module(name = "<module>", deps = [":weapons"])` line (`bazel/defs/core_module.bzl`) plus the entry at the end of the `:core` aggregate list |

Nothing else: `invitro/main.c`, `invitro/BUILD.bazel` and the `hook_adapters` list in
`proxy/BUILD.bazel` (both glob `*_cases.h` / `*_hooks.h`), `proxy/rng_hooks.h`,
`invitro/coverage.h`, `check_shards.py`, `callee_counters.h`, the leaf list of
`proxy/tests/path_counters_fixture.c` and `LIVE_SITE_COUNT` in
`proxy/tests/live_coverage_integration_fixture.c` pick the group up by themselves. Entry shims are
C entries listed in `ES2_ENTRY_SHIMS` in `proxy/entry_shims.h`, so two groups that both add a shim
edit that list.

- `proxy/hook_groups.py` (Bazel rule `hook_group_header`, `proxy/hook_groups.bzl`) writes two
  headers at build time: `hook_group_adapters.h` includes, per group in the order of its first
  registry line, the `*_hooks.h` that defines its descriptor array (`rng_hooks.h` includes it);
  `hook_group_cases.h` includes the `*_cases.h` that defines `G_cases` (`main.c` includes it). A
  header is included once, so every group sees the headers of all earlier groups. A missing or
  doubly defined descriptor array or `G_cases` fails the build.
- `main.c`'s `run()` dispatches through a table built from the registry with `group##_cases`. The
  seven first groups keep their many headers behind `legacy_cases.h` (frozen); rng, geometry,
  timing and movement map to NULL and run in `run()`'s own loop.
- `function_hooks_set_user(image)` (`rng_hooks.h`) sets the user pointer of every registered
  descriptor; const descriptor arrays (rng, geometry) are skipped by `_Generic`, so `line_plane`'s
  descriptor also carries the image.
- `callee_counters.h`: `weapons_graph_counters` calls `G_hook_counters` through a per-hook table;
  the first 29 groups map to the frozen `legacy_graph_counters`. `path_counters_fixture.c` keeps its
  leaf list for those groups and treats a hook of a newer group without counters as a leaf.
- `coverage.h` reads the `PROVED` lines of `branch_sites.h` (each must name one listed site of the
  same hook) and prints `PROVED: <hook> <site> <reason>; edge=<edge>`; `check_shards.py` reads the
  same lines. The live integration fixture compares `LIVE_SITE_COUNT` with the number of `BRANCH`
  lines and rejects duplicate sites.

## Legacy nested hooks and the writer's stack mask

**Legacy nested hooks (`legacy_nested.h`).** `base_update`, `lc_base_update`,
`mech_contact`, `status_eval`, `hunt`, `ram_step`, `weapon_step` and `target_emit`
nest one of the fourteen first-group hooks without an extension (`timer_word_step`,
`rng_bounded`, `transform_point`, ...). Their framework path returns EAX only; EDX and
ECX are what the replacement left, while the callers' originals pass those registers on
(vtables.md, "`base_update`: contract"). A real-callee pass compares two runs, which
match only while the replacement leaves the same EDX/ECX twice; a build with
`-fprofile-instr-generate` leaves a profile counter in EDX, +2 per call.
`nested_install_hook`/`nested_descriptor` therefore install a legacy nested hook in
replaced mode through a descriptor copy whose replacement calls the registered one and
then sets EDX = `0xdead1e9a`, ECX = `0xdead1e9c` (compare and original modes and every
extension hook stay as registered). Used by the nested installs of `base`, `lc_base`,
`mech_notices`, `mech_travel`, `mission_objectives`, `ai_behaviour` and `weapons`;
`flyer_states` keeps its own exclusion (EDX not compared when `timer_word_step` is
replaced).

**The writer's stack mask (`results_writer_cases.h`).** `rwr_mask` maps a stack value to
its offset from the FS:0 record when it lies in the writer's frame window, else to 0; the
record is in the original's frame in one run and in the core's scratch frame in the other.
A value that is no frame address but lies in the worker thread's stack range would be 0 in
one run and a frame offset in the other, depending on the randomized stack address. Each
record therefore also keeps the raw arguments, entry registers and stub EAX, and
`rwr_same` replaces the `memcmp` of the logs: every word must be equal, except that a
masked word may differ when its raw value is the same in both runs.

## CW3220 runtime passes

These passes run `rtl_*_cases.h` instead of the hook groups. Each has a quick target
`:differential_rtl<...>_test` and a manual full target `:differential_rtl<...>_full_test`;
`rtl_<function>`, `rtl_heap_<name>`, `rtl_start_<name>` and `rtl_io_<name>` select one function or
witness for the mutation runner. Contracts and results:
[rtl_imports.md](../../../docs/reference-game/rtl_imports.md). Counts are quick / full. What each pass
compares in detail is in that document.

### `--rtl`: the pure imports

After the DBSIM mapping the harness maps `CW3220.DLL` from `ES2_DATA_ROOT` (`CW3220.DLL` or
`extracted/cd/CW3220.DLL`, exactly one; size and SHA-256 checked) at a system-chosen address, applies
the HIGHLOW relocations, points every import of the DLL at a named trap and makes CODE read/execute. No
entry point, TLS callback or import runs.

- **Direct:** the 20 functions of `core/src/rtl.c`, 20,000 / 200,000 cases each in a 64 KiB arena
  followed by an inaccessible page. The export runs on a private test stack with given EAX/ECX/EDX and
  known EBX/ESI/EDI/EBP, then the own code runs on the restored arena. EAX/ECX/EDX, every arena byte and
  the 16 argument slots must be equal; the export must keep EBX/ESI/EDI/EBP, ESP and DF.
- **Installed:** the proxy's `rtl_imports.h` on the mapped DBSIM (53 CW3220 slots at the mapped
  exports) in replaced and compare mode, 2,000 / 20,000 cases per function and mode through DBSIM's
  thunk, equal to the direct export call including the 4 KiB below the arguments; mismatches 0.
- Two shards; every function and mode has its own stream (`rtl_reseed`), so a sharded run executes the
  cases of an unsharded one. `ES2_RTL_TRACE=1` prints each formatter case.

### `--rtl-heap`: the heap, shadow and replaced

The `--rtl` mapping with the DLL's `VirtualAlloc`/`VirtualFree` slots (RVA `0x24310`, `0x24314`) at an
emulated OS over one 320 MiB reservation (policy per sequence: contiguous, gaps, refused reservations,
failed commits). Before each sequence the heap's variables are reset, its init record `0x4170fc` runs
and operator new's handler word `0x421fb4` is cleared, so `new` returns NULL like `malloc` when memory
is refused.

- **Direct** (160 / 1,600 sequences of 300 operations): `core/src/rtl_heap.c` replays each operation
  with the recorded OS calls; result, OS calls, allocator variables and every allocator word match.
- **Shadow** (16 / 160 of 200): the proxy's shim in mode shadow; identical registers, 4 KiB below the
  arguments and heap with and without the shim, 0 shadow mismatches; one planted model error is caught
  once and resynchronized.
- **Replaced** (16 / 160 of 400) and **mixed** (8 / 80 of 300): `proxy/rtl_heap_own.h` in mode
  `replaced` through DBSIM's thunks against the original exports under the same OS decisions: equal EAX,
  OS calls, allocator variables and window hash, ECX/EDX where the own heap reproduces them, the 4 KiB
  below the return address unchanged, CW3220's own heap variables untouched. In mixed sequences every
  CW3220 block goes to the original (`rtl_heap_foreign_frees`).

### `--rtl-start`: the runtime lifecycle

The `--rtl` mapping with every import slot of the DLL on an own stub; nine stubs answer from the case's
script and log their arguments, any other import fails the case. `ExitProcess` leaves the private stack
through `ls_escape` (FS:0 restored, then the x87 state).

- **Model cases** (4,000 / 40,000): `__startup` on a 256 KiB private stack against
  `core/src/rtl_start.c` on the same synthetic module table. The event logs (every guest and Win32 call
  with arguments, ESP relative to the outermost runtime entry, x87 control word), table bytes, runtime
  variables and the model's return address are equal; every `PATHS:` scenario is reached.
- **Switched cases** (1,500 / 15,000): the proxy's lifecycle group (`proxy/rtl_life.h`: `__startup`,
  `_atexit`, `__exit`, `_abort`, `__ErrorExit`, mode `replaced`) against the original, restricted to what
  DBSIM can call (C `exit`, `_cexit`, `_c_exit` are not DBSIM imports). Two runs per case from the same
  stack pattern, registers, x87 state, table and CW3220 variables; compared are the events, per call a
  frame (registers at the callee's entry, every stack word from ESP+4 up to `[anchor+8]`), the table
  bytes and CW3220's variables. 30 `SWITCHED PATHS:` scenarios, all reached.

### `--rtl-io`: file and console imports

800 / 8,000 sequences over an emulated OS behind the KERNEL32 slots the I/O code uses (in-memory files,
terminal, pipe, find iterator, scripted clock, VirtualAlloc window; every call is logged). Each sequence
starts from CW3220's own heap, handle-table and standard-stream initialisation.

- **Direct:** 60 generated operations per sequence over the 21 exports, each on the export and then on
  `core/src/rtl_io.c` from the same state; EAX, OS call log, CW3220's DATA section, heap window, test
  arena and OS state equal. Operations that C forbids and the export turns into memory corruption are not
  generated (input right after output and output over unread input without a seek; `__fputc` other than
  at level 0). 63 `PATHS:` scenarios, all reached; INT3 coverage of the 315 conditional branches behind
  the set (`rtl_io_branches.h`), each taken both ways or listed with its one edge in
  `rtl_io_branch_notes.h`.
- **Installed** (8 / 80): the proxy's shim on the mapped DBSIM, the 18 file imports in shadow, then all
  21 in replaced; EAX after every step and the final state equal with and without the shim.

### `--rtl-own`: the own cw3220.dll against the original

`differential_rtl_own_test` (quick, about 12 s) and `differential_rtl_own_full_test` (manual, about
120 s) load the own DLL (`//rtl:cw3220.dll`, path in `ES2_OWN_CW3220`) into the harness, where its
DllMain runs in own mode, and compare its exports with the mapped original's (`rtl_own_cases.h`). The
DLL's `es2_own_runtime` descriptor gives its state; its `reinitialize` reruns the attach-time set-up
after the harness pointed the DLL's KERNEL32/USER32 slots at the pass's emulated OS. The test stack is
the owner thread's stack, so every call takes the DLL's private-stack adapter as DBSIM's do.

- **Tables:** `__ctype`, the DOS error map, the scanner classes, the static I/O state and the data
  exports' addresses against CW3220's data.
- **Pure and heap:** the `--rtl` generator, 4,000 / 40,000 per export, a tenth from a foreign stack
  (the caller's-stack path); the `--rtl-heap` replaced sequences with the DLL (24 / 240 of 400). Counted,
  not hidden: empty `sprintf`/`vsprintf` results (EDX is CW3220's formatter residue, the DLL leaves 0)
  and `%p`/`%n` formats.
- **I/O:** two worlds over the shared memory, swapped per call: the original's, started by CW3220's init
  records (`4170fc`, `409014`, `407248`, `41e268`), and the DLL's, started by its `reinitialize`. Each
  step is generated in the original's world and run in both; EAX, the OS calls, OS state, arena and
  window bytes, I/O and heap variables, FILE pointers, tokens and stream-hook values (translated between
  the runtimes) are compared. 200 / 2,000 sequences of 60 steps.
- **Lifecycle:** the switched `--rtl-start` comparison with the DLL's exports in DBSIM's slots and
  DBSIM's `__startup` thunk as the entry (1,500 / 15,000); `rtl_start_cases.h` maps the DLL's addresses
  (trampoline return, frame handler, window callback, abort text, dummy hook) to CW3220's.
- **Exceptions and foreign threads:** `__ExceptionHandler` of both on every context entry of DBSIM's 267
  frame tables, plus a C++ throw in the search phase and an unwind with nothing constructed (CW3220's
  helpers that would call DBSIM code start with `int3`); the `__startup` frame handler against CW3220's
  `405160` (540 matrix and 2,000 / 20,000 random cases); `_atexit` from a thread of its own (700 / 7,000)
  and `@_CatchCleanup$qv` there.

## Harness notes per group

The case header of a group or suite describes its world, stubs and passes. These notes give the
identifier prefix, the trap that the header does not show and the contract document. Hook groups
run as `:differential_<group>_test`; the suites at the end have a test of their own
(`:differential_<stem>_test`, one pass, no quick/full split).
- **weapon_syncs** (`wsy_`): the received record bytes are logged instead of the local record's
  stack address; `weapon_prepare` runs real under `laew_fire`. weapon_classes.md, "the weapon
  gauge syncs, the LAEW emission and the empty bodies".
- **mission_end** (`mse_`): `_fclose` 496838 and `results_writer` 42412c are logging stubs; the
  bare-RET exits 426514 and 425344 stay original; no real-callee pass. launcher.md, "the mission
  end `0x461ff0` as own code".
- **mission_root** (`mrt_`): 62 logging direct callees (the thunks 49694c/496970 pop their two
  slots; the bare RET 463370 stays original). The adapter's buffer frame lies 4 bytes below the
  original's, so the generator keeps every data word the root forwards out of the worker stack
  ([stack values](#hook-groups-stack-values)). mission_load.md, "The mission-load root".
- **hdd_map** (`hmf_`), **hdd_map_keys** (`hmk_`): frame-valued words are compared relative to the
  run's frame. The 44f03c stub accepts every call from the 40th on: with the start entry behind
  the count word the original's cycle has no other end. cockpit.md, "HDD map frame" and "HDD map
  key path".
- **effect_pools** (`epc_`): the real-callee pass runs the real 49630c and 49667c, a bump
  allocator for new/new[] and `pool_create`. mission_load.md, "Effect pools with resources".
- **fit_leaves** (`fit_`): the original runs through a naked caller that also captures ECX, so the
  case compares the original, the adapter and the core. mission_load.md, "The remaining weapon_fit
  callees".
- **camera_view** (`cmv_`): 49630c is a record stub that links a record at EBP + `[EAX+4]` as in
  `beam_cases.h`; stack values are masked in the logs. camera.md, "The hooks (group camera_view)".
- **sim_leaves** (`sml_`): `flat_draw` and `projectile_draw` have no real-callee pass (their
  callees draw through matrix code that faults on random bytes); the notice records' residue bytes
  are never compared. vtables.md, "simulation leaves".
- **mfd_keys** (`mkc_`): the real-chain pass needs the `menu_select` shim's ECX reload (446c38
  calls 447248 with `menu_select`'s ECX = k). cockpit.md, "MFD keys".
- **hdd_exec** (`hxc_`): in the real pass callee-saved registers, stack position and residue are
  compared only for 44a1a0's own callees, and the ECX of the callees below a replaced
  `hdd_orders` hook is not compared. cockpit.md, "HDD button executor".
- **Sim suites** compare original machine code with `sim/src` after every step:
  `init_records` (DBSIM's c0w32 entry `0x401000` with its 65 init and 26 exit records; after each
  of the 92 steps DATA `0x497000..0x6cc000`, the two dwords below the worker's StackBase, the heap
  head and region and the selected VA are compared;
  [own_process.md](../../../docs/reference-game/own_process.md#the-static-initialisation-as-own-code)),
  `startup` (about 30 s; reads `VOL\` of the CD and writes synthetic archives into a temporary
  directory;
  [own_process.md](../../../docs/reference-game/own_process.md#start-up-parsers-and-file-services)),
  `loop` (`0x480f38` is called through a thunk that keeps the harness's registers and returns with
  ESI = 0, because the original's `0x4894a5` removes one slot too many; checked every case;
  [main_loop.md](../../../docs/reverse-engineering/main_loop.md#the-main-loop-pump-window-procedure-limiter-and-clock-as-own-code)),
  `input_layer` (the longest; it writes, reads and deletes a real file per side;
  [input.md](../../../docs/reverse-engineering/input.md#the-input-layer-as-own-code)) and `dialogs`
  (both sides build the same synthetic dialog from one seeded generator; each modal case has a
  random journal and a pass script written by the control-record stub `0x45a810`;
  [main_loop.md](../../../docs/reverse-engineering/main_loop.md#the-modal-dialogs-in-the-own-process)).

## Hook groups: stack values

A classifier that tells stack addresses apart by value gives different answers on the two sides
when its window lies elsewhere on each side (relative to FS:0, a run's frame base or one exact frame
address), because ASLR moves the stacks. A generated data word that falls into one side's window is
classified there and passed raw on the other, so the case fails rarely and depending on ASLR. The
suites on `shape_draw_cases.h`'s world use `sd_is_stack` (original: the committed worker stack; own:
the call area `lc_stack`); the one generated word that reaches it, the context's `+0x22c` before a
polygon replaces it, keeps bit 31 set (`sd_world_base`), and on the original's side a classification
of that word inside the reservation or the call area fails the case. `jb_norm`, `sr_norm` and
`csy_norm` need no such rule.

In `differential_harness` the original, the core and the adapter run on the same worker thread, so
a classifier over the whole 16 MiB reservation (`StackBase - 16 MiB .. StackBase`) treats a value
the same way on every side. The rule for a classifier with a window: keep every generated word that
can reach it out of the reservation, and guard against one that still gets in. Where the original
computes reservation values from generated data (`tyl_rel` in type_loads), filtering cannot work;
the comparison then accepts a classified mismatch only when the raw words are equal (`tyl_same`, as
`rwr_same` does).

- `stack_clean` (`harness_base.h`) flips bit 31 of a word inside the reservation;
  `stack_clean_bytes` does the same for every dword of a random byte area at every offset. Neither
  consumes the stream, so the case stream stays the same.
- `stack_uncommitted` is the guard: a classified value in the reservation's uncommitted part
  (`AllocationBase .. StackLimit`) is never a live stack address. Raw words land there about once in
  256, so a generator site without `stack_clean` trips the guard within a quick run.
- Classifiers with a window: `bty_rel`, `dmd_rel`, `epc_rel`, `st2_rel` (FS:0 record);
  `mw_frame_relative`, `mw_random` (weapon_fit frame, FS:0 + 28h); `hxc_rel` (exact f-8, f-20h);
  `hmf_rel` (exact f-0Ch, f-20h); gauge_sync ESI and weapon_frame's record (weapon_slots,
  `wsl_alias`). Reservation-wide tags,
  world/image tags and anchors equal on both runs need no rule.

## Dispatch tables of the tests: size and duplicate VAs

This rule holds for the in-vitro cases here and for the tests under `sim/tests`.

- A test's dispatch table has 4,096 entries (`es2_sim_entry table[4096]`,
  `es2_sim_dispatch_init(&d, table, 4096, ...)`), the same as `ES2_SIM_MAX_ENTRIES` in
  `sim/include/es2/sim/host.h`. Tests that check the table's own limits (`sim/tests/table_test.c`,
  `init_test.c`, `trap_test.c`) keep their small sizes on purpose.
- `es2_sim_dispatch_seal` sorts the entries by VA and refuses a VA that is listed twice, so a test
  must not add a hook, service or headless entry for a VA that the service rows
  (`es2_sim_service_row`) already hold.
- A test that stubs such a VA skips it before the seal and replaces it after the seal: look the
  entry up with `es2_sim_dispatch_find` and set its `service` and `service_user`; keep the row's
  `pop_bytes` and `stack_slots`, because they are the original function's calling convention.
  Whether a VA has a row is checked by a lookup over `es2_sim_service_row`, as `sd_has_row` in
  `shape_draw_cases.h` does. Examples: `shape_draw_cases.h`, `cockpit_cases.h`,
  `hud_mfd_leaves_cases.h`, `sim/tests/gfx_test.c`, `gouraud_test.c`, `hud_test.c`.
- A stub for a VA without a row is added before the seal as usual.
