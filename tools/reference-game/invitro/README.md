# Private-image differential tests

`//tools/reference-game/invitro:differential_harness` is an own-code 32-bit
executable that maps the hash-checked original `DBSIM.EXE` into its own
process, calls the original functions behind all registered hooks on
generated inputs and compares them with our `core/` code: result registers,
every declared state byte, guard bytes around it and, where relevant, the log
of calls into game code. It never starts the game, calls the original entry
point, loads an original DLL through the Windows loader or saves original
code or tables (`--rtl` maps `CW3220.DLL` privately, as it maps DBSIM; see
[CW3220 runtime passes](#cw3220-runtime-passes)).

Every original branch site of the hooked functions must be covered in both
directions or proved unreachable, and every hook has a caught mutation
([MUTATIONS.md](MUTATIONS.md)). The current counts are printed by every run
(`<n> cases`, `PROVED:` lines) and by `check_shards.py`.

## Run

```powershell
# quick tier (part of `bazel test //...`): one test per hook group plus the probes
bazel test //tools/reference-game/invitro:differential_test
# full tier (on request only): the generators' own counts, sharded per groups.bzl
bazel test //tools/reference-game/invitro:differential_full
# (.bazelrc sets ES2_DATA_ROOT, the timeout tiers, --lockfile_mode=error and --test_output=errors)
$env:ES2_DATA_ROOT = 'build\extracted\cd'
& '<es2-dependencies>\python\python.exe' -B tools/reference-game/invitro/check_signatures.py
```

**Targets.** The harness is built once as `:differential_harness`; the tests
are small launchers (`launch.c`) that run it with their arguments, so a
change in one core module reruns only the groups that depend on it while the
other groups' results stay cached:

| Target | Arguments | Content |
| --- | --- | --- |
| `:differential_<group>_test` | `--quick --group <group>` | quick tier, `size = "large"`, shards from `groups.bzl`; in `//...` |
| `:differential_<group>_full_test` | `--group <group>` | full tier, `size = "large"`, `manual`, shards from `groups.bzl` |
| `:differential_probes_test` | `--probes` | chain, contract, poison and weapon-mode probes |
| `:differential_test` | suite | all quick group tests + probes |
| `:differential_full` | suite | all full group tests + probes |
| `:groups_test` | `check_groups.py` | `groups.bzl` names exactly the registry's groups with admissible shard counts |

The groups are those of `hook_registry.h` (two label stems differ:
`vehicle_upd`, `input_disp`, because Windows refuses to start executables
named `*update*`/`*patch*`, error 740); `groups.bzl` lists them with their
full-tier shard counts (a new group adds a line, a group that grows past a
multiple of 12 hooks raises its count; `groups_test` reports both).

**Sim suites.** The suites that compare a module's original machine code
with its own code in `sim/src` through the full service dispatch are not in
`:differential_harness`: each has its own binary
`:differential_<stem>_harness` built from `sim_main.c` and a generated
`sim_suite.h` (`suites.bzl`), and its test `:differential_<stem>_test` runs
it without arguments. A suite's case header starts with `#pragma once` and
includes the case headers and `es2/sim` headers it builds on, so a binary
compiles only that closure. The hook-group harness links no sim code, and
`harness_base.h` holds what both share (the start-up reservation, the
hash-gated mapping, the case helpers, the fault handler). With reproducible
linking (`-Wl,--no-insert-timestamp` in the toolchain) a binary whose objects
did not change keeps its bytes, so its test results stay cached:

| Change in | Rebuilds | Reruns |
| --- | --- | --- |
| one suite's case header | that suite and the suites including it | the same |
| `sim/src` or `sim/dispatch` | the suite binaries | the suites (not the hook groups) |
| `core/`, the hook registry, a hook-group case header | `:differential_harness` | the hook groups, probes, rtl tests |

`suites.bzl` lists the suites one line each, sorted by stem (header, entry
expression, test shards, size, extra data); a new suite is a new case header
plus one line there. A suite with `shards > 1` splits its parts over the
Bazel test shards with `suite_part_in_shard()` (`suite_sharding.h`): chunked
suites (`gl_fill`, `gl_gouraud`, `mfd_rest`, `loop`, `mfd`) run chunks of 500
cases, each chunk from its own seed, so the cases do not depend on the shard
layout; `display` gives each of its six parts a shard and a seed of its own,
and its hard coverage checks hold for the parts the shard ran (an unsharded
run checks them all). Every sim test is tagged `resources:membw:1`, and
`.bazelrc` runs at most three at once (`--local_resources=membw=3`): the
suites are bound by memory bandwidth.

**Per-case memory traffic.** Per case and side a suite resets DATA
(`su_reset`, 0x235000 bytes), clears the 16 MiB heap region
(`ir_heap_reset`), the loop's 1.2 MB frame bits (`lc_reset`) and, where files
are modelled, the 4 MiB runtime region (`su_files_reset`), takes a snapshot
of DATA and the used heap (`su_take`) and compares the two snapshots
(`su_compare`). None of these touches the whole buffer:

- The heap region, the frame bits and the runtime region are allocated with
  `MEM_WRITE_WATCH` (`ww_alloc`, init_records_cases.h). A clear (`ww_clear`)
  zeroes only the pages `GetWriteWatch` lists, then resets the watch. If
  write tracking fails it zeroes the whole buffer.
- DATA cannot be write-watched: it lies in the bottom of the start thread's
  stack reservation (startup_reservation.h), and a reservation's flags are
  fixed when it is made. So `su_reset` and `su_take` compare DATA page by
  page with the pristine image and restore or copy only the pages that
  differ; for a zero pristine page the test reads DATA alone. The page test
  (`ww_page_differs`) reads the whole page without an early exit, so the
  compiler vectorises it (a `memcmp` or a loop with a branch inside is about
  four times slower).
- `su_take` keeps per side the set of snapshot pages that may differ from
  their base (the pristine DATA page, or a zero heap page). It copies only
  the DATA pages that differ now and the heap pages written since the clear.
  Pages that leave the set get their base back. `su_compare` compares only
  the pages in either side's set. `su_snap_same` marks the pages it writes.
- The bytes each case compares, and the byte-wise diff printed on a mismatch,
  are those of a full copy and compare.

`ES2_INVITRO_CHECK_RESET=1` checks every shortcut against the full
operation: a cleared buffer must be zero throughout, after `su_reset` DATA
must equal the pristine image, after `su_take` the snapshot must equal DATA
and the used heap, and the page compare must agree with a full `memcmp`. Any
disagreement prints `FAIL:` and exits 1. Check mode catches a clear that
skips a written page, a restore that copies half a page and a snapshot that
keeps a stale DATA page in the first case that hits them; without it such
faults can pass a suite. `su_reset` scans DATA again instead of trusting
`su_take`'s list, because harness code may write DATA between the two.

**Tiers.** The full tier runs every generator's own count (about one million
random cases after its boundary prefix). The quick tier (`--quick`) caps the
random budget per hook at 100,000 cases (`ES2_INVITRO_QUICK_CASES` in
harness_base.h, applied through `case_budget()` in every generator); every
coverage window (at most 65,536 cases) and every boundary prefix lies inside
the cap, so the quick tier requires the same branch-site coverage and proved
edges as the full tier and differs only in the random tail. Installed-adapter
runs (256 cases per function and mode) and the explicit
`ES2_INVITRO_*_CASES` overrides are not capped.

**Data root.** Without `ES2_DATA_ROOT` (or with an empty value) the test
prints SKIP and exits zero before reading original data. The root accepts
exactly one of `VER95/DBSIM.EXE` or `extracted/cd/VER95/DBSIM.EXE`;
ambiguous candidates and resolved paths escaping the root are rejected. File
size and SHA-256
(`8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad`) are
checked on the same read buffer before PE parsing, mapping or execution, and
all descriptor signatures and instruction lengths are checked as well.
`check_signatures.py` repeats the signature audit from the registry.

The built `differential_harness.exe` also accepts:

| Argument | Effect |
| --- | --- |
| a hook name | isolated run of that one function (its own reproducible random stream) |
| `--group <group>` | the group's functions in registry order, split by the Bazel shard variables |
| `--quick` | quick tier (see above); combines with `--group` |
| `--probes` | only the chain/contract/poison/weapon-mode probes |
| `--chain` | the 40250c leaf-chain composition test |
| `--contract-probes` | division boundaries and the stack-poison padding probes |
| `--trap-self-test` | deliberately calls a named import trap |

The `ES2_INVITRO_<GROUP>_CASES` variables (for the first generators
`ES2_INVITRO_ADVANCE_CASES`, `_ACTOR_CASES`, `_TERRAIN_CASES`,
`_COPY_CASES`, `_GROUND_CASES`, `_COLLIDE_CASES`, `_EFFECTS_CASES`,
`_DAMAGE_CASES`) override the direct case counts of a group for quick runs.

**Sharding.** The unit of parallelism is the hook group: each group test
installs all adapters, exercises only the group's functions and reports
coverage only for their sites (every owned site still needs both feasible
outcomes; impossible edges are explicitly proved). Inside a full-tier group
test Bazel shards split the members by `rank in group mod shards`
(`groups.bzl`); the probes run in their own target. A direct run without
`--group` selects by registry index from the Bazel shard variables, shard 0
adds the probes, and without the variables the run is unsharded. Every run
starts from seed `0x48c06e21`, so sharding changes the random subsequences
but not the case counts, boundary prefixes or coverage requirements.
`check_shards.py <unsharded-log> <group-or-shard-logs...>` checks that
completed full-tier logs (the group logs plus the probes log) add up to the
inventory of an unsharded run; it accepts a parenthesized note after the
case count (`line_plane` prints `1001024 cases (N hits);`).

## Mapping and imports

The harness is linked at `0x10000000` without ASLR and with a 16 MiB PE stack
reservation; on this host Windows places the initial thread's stack at
`0x00400000..0x013fffff`. The entry point (`startup_reservation.h`) claims the
original image range `0x00400000..0x006d7fff` either as a free range or, when
it is the uncommitted bottom of the calling thread's own stack reservation, in
place: the image is committed there after the hash gate and the page at
`0x006d8000` becomes a `PAGE_NOACCESS` barrier, so a growing stack would fault
instead of overwriting the mapping. The initial thread only starts the
16 MiB test worker and joins it. Anything else at `0x00400000` fails with its
Win32 error and region details; `STACK` and `STARTUP` lines report the layout.
CODE becomes RX, data stays private RW. Releasing the initial stack and
reserving the range afterwards would leave a window in which another thread's
stack can win the range under parallel shards; the in-place claim has none
(`check_startup.ps1 -Executable <exe> -OutputDirectory <scratch>` launches the
harness alone and next to six parallel instances).
`//tools/reference-game/invitro:startup_test` covers the free range, the
own-stack claim, barrier failure and foreign, mapped, committed, too-small
and unqueryable regions.

DBSIM's relocation directory (RVA `0x2ce000`, size `0x8ab4`) is not needed at
the preferred base; there is no fallback rebasing. All **134 IAT entries**
are replaced by own-code traps that identify the import and fail the run.
The only exceptions are `cw3220.DLL!_memset` and `_memcpy`, which point to own
implementations in `probe_call.S` with the runtime's register results
(EAX = destination, EDX = count, ECX = 0). Unexpected CPU exceptions print the
function, code and address and fail without WER dialogs. The RNG seed and the
quarter-angle table are read from the mapped image at run time and never
stored. x87 starts from CW `0x1332` as set by CW3220's startup (hardware reads
`0x1372`); each original integer call must preserve CW, SW and tags.

Functions whose original calls further game code get those callees replaced
by stubs in the private mapping, and core gets the same stubs. Most groups
use logging stubs with a per-case result stream and compare the call log
(arguments, entry registers, buffer contents instead of stack addresses).

## Cases

The first groups' generators (behind `legacy_cases.h`):

| Functions | Cases per function |
| --- | --- |
| RNG, distance, timing, `multiply_q8`, `movement_step`, `hud_speed`, `transform_point`, `vector_subtract`, `distance_display` | 1,000,121 (121 boundary cases plus 1,000,000 random) |
| `heading_cos`, `heading_sin` | 65,536, the exhaustive 16-bit domain |
| `ground_effects`, the damage functions | 1,001,024 (1,024 boundary prefix plus 1,000,000 seeded cases) |
| `track_clip` | 1,000,025 (25 signed boundary cases plus 1,000,000 seeded cases) |
| the other first-group hooks | 1,000,000 |
| installed adapters | 256 per function in replaced mode, and 256 in compare mode for the hooks that support it |

The boundary prefix populates timer payloads, both HUD branches (including
INT_MIN full-width divisors), point coordinates, matrix coefficients,
translations and aliasing vector state. HUD cases keep the full-width
denominator magnitude at least 65,536 so that IDIV quotients stay valid; zero
divisors and faulting quotients are excluded, not normalized. Transforms and
vector subtraction exercise overlapping buffers; installed compare mode skips
overlapping span layouts on purpose (counted as `skipped`) and nested original
calls count as `bypassed`. Installed comparisons must have zero mismatches.

Structured generators add: matrices built by the original Euler routine in
identity/planar/full classes, all signed mode-byte classes, wrapped angle
ties, alias layouts, INT32/INT16 extremes, legal scalar-division endpoints,
ratio axes and sign combinations, all eleven track counts. Direct checks
restore the same arena addresses before running core, so pointer-derived
EAX/EDX are compared without normalization. Larger generators:

- `track_advance` (`advance_cases.h`): managers with 0–10 tracks over one or
  two resources, clips of both classes with the image's own vtables plus an own
  vtable for core's fallback, random hierarchies; EDX:EAX and the 12,288-byte
  arena must match except byte 19 of written matrices. 478d38 jumps to
  `advance_stub`, which writes a prepared matrix into [M+0x16] and records its
  arguments (removed before the chain test and probes; the `track_advance`
  cases run the real 478d38).
- `track_target`, `track_rate`, `actor_animate`, `ground_animate`
  (`actor_cases.h`): random entry EAX/EDX/ECX, tracks with frame records and
  transition tables; 40250c and 402604 are stubbed.
- `terrain_height` (`terrain_cases.h`): random grids and points.
- `actor_snapshot`, `actor_rollback` (`copy_cases.h`): the arena plus a 4 KiB
  window of the global snapshot block at 0x4a8cd0.
- `ground_update` (`ground_cases.h`): its nine callees are logging stubs
  driven by a per-case script; the call log and the proxy's callee-counter
  deltas are compared.
- `ground_collide` (`collide_cases.h`): 404ae4, 46e498, 492f24, 434010 and
  462830 are logging stubs, the scene objects have stub vtables (radius,
  reach, touch, hit, part damage); the arithmetic helpers run as original
  code. The call log compares buffer contents instead of stack addresses.
- `ground_effects` (`effects_cases.h`) isolates all five direct callees and
  virtual +20 with logging stubs. Full argument slots, EDX:EAX results,
  callback order, the actor/attachment arena, read-only globals and observer
  counter deltas match. The distance stub uses the verified wrapped metric;
  explicit deltas include 14999/15000/15001 and INT32 extremes. Synthetic
  configuration covers null/negative part lookup, every animation/category,
  forward/reverse threshold ties, cached/uncached matrices, null slots and
  post-sound flag changes. Transform output addresses are normalized, never
  pointed-to contents. Guards fill the rest of the 12 KiB arena.

**Compare-mode cases.** `compare_probes.h` (included by `main.c` before the
group headers) gives a group's compare-capable members installed compare
cases with real callees: `compare_probe_run(index, 0, build, state, size)`
after the group's real pass runs 256 cases of the unhooked original against
the hook installed in compare mode (pair and state equal, one call, no skip,
no bypass, no mismatch) and 256 with a skewed replacement (EAX bit 0, or for
hooks with spans every other case a declared byte in the clone, flipped): one
mismatch per call, the caller keeps the original's pair and state. In the
installed compare pass of `main.c` the group calls it with the limit instead
of its stubbed direct cases (stub logs and stub streams would run once per
phase): the trampoline against the hooked call, EAX and state. Two options a
group sets around one call: `compare_probe_same`, a pair predicate for the
unhooked against the compared call (frame addresses move with the stack
depth at which the original runs), and `compare_probe_skew_edx`, which skews
EDX instead of EAX. Which hooks use them:
[compare_mode.md](../../../docs/reference-game/compare_mode.md#in-vitro).

**Probes.** 256 states x all four original/replaced combinations of
`ground_update` and `ground_effects`, including no-track actors; and a real
Euler/transform/distance + 434144 state-4 feedback run. The latter proves
incoming EDX survives the real feedback code; a fabricated-return C stub
alone would miss this requirement. Both compare selections are refused by
the installed-adapter checks. `--chain` compares 4,096
actor/manager/pose/matrix/flag arenas following 40250c's leaf order on a
zero-track hierarchy (both actor cache states, root and child poses).
`--contract-probes` checks the original scalar/ratio #DE boundaries and the
enabled/disabled track division, and runs all five matrix wrappers from an
own caller that fills the unused stack with 31/a7: only matrix byte 19
changes, which is the padding contract.

## Branch coverage and probes

`branch_sites.h` lists the original Jcc sites of the hooked functions, found
by entry-aligned control-flow walks. For the first 65,536 cases per function
(fewer for some generators, 8,192 for `track_advance`) the harness replaces
each site's first byte with INT3 in private memory; a vectored handler
evaluates the condition on the saved EFLAGS, counts the outcome and continues
at the exact successor. Every byte is restored before the installed-adapter
tests; an unobserved feasible outcome fails the run and names function, site
and missing outcome (`coverage_diagnostics_test`). The exception dispatch
writes below ESP, so residue checks of an uninitialized local deeper than a
covered branch are masked in those cases. The same table drives the opt-in
[live coverage](../../../docs/reference-game/live_coverage.md) in the game.

**Early exit.** `coverage.h` restores a site's original byte inside the trap
handler as soon as the site has counted both its fallthrough and its taken
edge; the next time that code runs it executes the original Jcc without a
trap. The window lengths are therefore upper bounds:

- **Done** means, per site, fallthrough count and taken count both nonzero.
  That is exactly the condition `coverage_report` checks, so further counts
  could never change the verdict. A site missing an outcome stays armed for
  the whole window, and a window that ends with an outcome missing fails with
  the missing edge as witness.
- **Proved sites** (`PROVED` lines) are never done: they stay armed for every
  window, because their check also rejects the impossible edge and must keep
  seeing every hit. A hook whose time is dominated by a proved site in a hot
  loop keeps the trap cost for that site.
- **Nested and real-callee sites.** All sites are armed in every window,
  whatever hook owns them, so a hook's run still counts the sites of the
  callees it executes. The rule is per site, not per hook: a callee site that
  still lacks an outcome stays armed through the caller's window, so a site
  whose missing outcome only another hook's run supplies is still covered. A
  new window (`coverage_enable(1)`) arms only sites that are not done (plus
  the proved ones); `coverage_enable(0)` restores only armed sites.
- **The window itself** (`coverage_active`) opens at case 0 and closes at the
  runner's fixed case, so the boundary prefix is inside it and every runner
  that reads `coverage_active` (flyer_control's residue check) sees the same
  cases with or without early exit. Disarming happens in the middle of a
  case, which only changes how much of the stack below ESP the exception
  dispatch overwrites; no check depends on that (masked cases are chosen by
  window, not by trap).
- **Case counts** do not depend on it; only the cost of the trap ends early.

`BRANCH ... fall=N taken=M` counts are the hits until both edges were seen
(proved sites and sites that stay one-way keep full counts). The summary line
adds `N sites disarmed at their second outcome` (counted over the whole
process, including callee sites of other groups). Informational statistics
that measure stack residue (`track_advance`'s "residue bytes differing",
`track_list`'s "byte 19 ... differed") depend on how many exception frames
clobber the stack. Removing a needed `PROVED` line makes the hook fail with
`missing=<edge>`; flipping a proof's edge fails the proved-edge check.

## Mutations

```powershell
$env:ES2_DATA_ROOT = 'build\extracted\cd'
& tools/reference-game/invitro/check_mutations.ps1 -OutputDirectory <scratch>
& tools/reference-game/setup/test-hook-registry.ps1 -OutputDirectory <scratch>
```

The mutation runner copies only our sources, build files and the module lock
into a fresh scratch workspace, changes one expression per case, builds the
harness with Bazel and requires a named differential failure. `-Only
<name>[,<name>]` restricts the run. Scratch BUILD files are renamed
`.inactive` on exit. Do not run `bazel test //...` while a mutation workspace
is active: recursive discovery can include its temporary BUILD files.
`-RepoContentsCache <path>` keeps the optional repository-contents cache in
an allowed cache directory when the system default is inaccessible. New
registry entries without a mutation are rejected. The generator and what the
witnesses prove: [MUTATIONS.md](MUTATIONS.md).

## Evidence

Each run prints its inventory (direct and installed cases per hook, branch
sites, proved edges); the full-tier logs of a merge batch are copied to
`<es2-data>\logs\<topic>-<date>` together with the mutation logs.
Oracle evidence for each step is in `<es2-data>\logs`
([hooks.md](../../../docs/reference-game/hooks.md#live-validation)).

## How to add a hook group

The harness, the proxy and their tests derive every per-group list from
`proxy/hook_registry.h`, whose last `ES2_HOOK` argument is the adapter group.
Adding a group `G` touches only:

| File | What to add |
| --- | --- |
| `proxy/<G>_hooks.h` (own) | descriptors (exactly one `es2_hook_descriptor <x>_hook_descriptors[]` per group, any name), extensions, own `callee_counter` arrays and `static callee_counter *G_hook_counters(unsigned hook,unsigned *count)` (the array and length of one of its hooks, or NULL and 0 for a hook without game calls; required, also for pure leaves) |
| `invitro/<G>_cases.h` (own) | `static int G_cases(unsigned index,unsigned limit)` for all hooks of the group, plus its helpers |
| `proxy/hook_registry.h`, `proxy/hook-names.txt` | one line per hook, appended |
| `invitro/branch_sites.h` | an own section at the end: `BRANCH(...)` lines and, for edges proved unreachable, `PROVED(hook,site,edge,"reason")` lines (`edge` = the only feasible outcome, 0 fall-through, 1 taken) |
| `invitro/check_mutations.ps1` | one witness per hook at the end |
| `invitro/groups.bzl` | the group's line with its full-tier shard count |
| `core/` (own module and test; in `core/BUILD.bazel` one `core_module(name = "<module>", deps = [":weapons"])` line from `bazel/defs/core_module.bzl` plus the entry at the end of the `:core` aggregate list) and `docs/` | the module, its test and its contract |

Nothing else: `invitro/main.c`, `invitro/BUILD.bazel` and the
`hook_adapters` list in `proxy/BUILD.bazel` (both glob `*_cases.h` /
`*_hooks.h`), `proxy/rng_hooks.h`, `invitro/coverage.h`, `check_shards.py`,
`callee_counters.h`, the leaf list of `proxy/tests/path_counters_fixture.c`
and `LIVE_SITE_COUNT` in `proxy/tests/live_coverage_integration_fixture.c`
pick the group up by themselves.

How it works:

- `proxy/hook_groups.py` (Bazel rule `hook_group_header` in
  `proxy/hook_groups.bzl`, run with the pinned Python) writes two generated
  headers at build time: `hook_group_adapters.h` includes, for each group in
  the order of its first registry line, the `*_hooks.h` that defines its
  descriptor array (`rng_hooks.h` includes it); `hook_group_cases.h` includes
  the `*_cases.h` that defines `G_cases` (`main.c` includes it). A header is
  included once, so every group sees the headers of all earlier groups. A
  missing or doubly defined descriptor array or `G_cases` fails the build.
- `main.c`'s `run()` dispatches through a table built from the registry with
  `group##_cases`. The seven first groups keep their many headers behind
  `legacy_cases.h` (frozen): `math_cases`, `damage_cases` and
  `weapons_cases` there are the main.c dispatch chain restricted to each
  group (over `math_round1_cases`, `damage_round1_cases` and
  `weapons_round1_cases`); rng, geometry, timing and movement map to NULL and
  run in `run()`'s own loop.
- `function_hooks_set_user(image)` (`rng_hooks.h`) sets the user pointer of
  every registered descriptor through the registry; const descriptor arrays
  (rng, geometry) are skipped by `_Generic`, so `line_plane`'s descriptor
  also carries the image (its adapter ignores it).
- `callee_counters.h`: `weapons_graph_counters` calls `G_hook_counters`
  through a per-hook table; the first 29 groups map to the frozen
  `legacy_graph_counters`. `path_counters_fixture.c` keeps its leaf list for
  those groups and treats a hook of a newer group without counters as a leaf.
- `coverage.h` reads the `PROVED` lines of `branch_sites.h` (each must name
  one listed site of the same hook) and prints
  `PROVED: <hook> <site> <reason>; edge=<edge>`; `check_shards.py` reads the
  same lines. The live integration fixture compares `LIVE_SITE_COUNT` with
  the number of `BRANCH` lines and rejects duplicate sites.

Still shared: own-assembly entry shims chain through the `default:` of the
last group's `*_capture_entry` (`weapon_slots_capture_entry` in
`weapon_slots_hooks.h`, reached through `sfx_capture_entry`,
`panel_weapons_capture_entry`, `bullet_ctor_capture_entry`,
`transform_helpers_capture_entry`, `commands_capture_entry` and
`vfs_capture_entry`), so two groups that both need a shim edit the same line.

## Legacy nested hooks and the writer's stack mask

**Legacy nested hooks (`legacy_nested.h`).** `base_update`, `lc_base_update`,
`mech_contact`, `status_eval`, `hunt`, `ram_step`, `weapon_step` and
`target_emit` nest one of the fourteen first-group hooks without an
extension (`timer_word_step`, `timer_dword_step`, `rng_bounded`,
`dist2d_point`, `time_scale`, `transform_point`, ...). Their framework path
returns EAX only; EDX and ECX are what the replacement left. The callers'
originals pass those registers on (`base_update` returns "the timer's EDX",
vtables.md, "`base_update`: contract")
to their returned EDX or to the entry EDX/ECX a stub records, and a
real-callee pass compares two runs with the same nested hooks, so they match
only while the replacement leaves the same value twice. In the default build
`es2_timer_word_step` leaves EDX = the word minus the scale, ECX = the object;
built with `-fprofile-instr-generate`, EDX is the low dword of its own
profile counter, +2 per call, so the second run differs. Not a host stack
address. `nested_install_hook`/`nested_descriptor` install a legacy nested
hook in replaced mode through a copy of its descriptor whose replacement
calls the registered one and then sets EDX = `0xdead1e9a`, ECX = `0xdead1e9c`
(EAX, writes, dispatch unchanged; compare and original modes and every
extension hook are installed as registered). Both runs see the same EDX/ECX,
and the passes keep comparing the returned EDX and the stub entry registers
exactly on every path. Used by the nested installs of `base`, `lc_base`,
`mech_notices`, `mech_travel`, `mission_objectives`, `ai_behaviour` and
`weapons` (`weapon_step_modes`, the second and third weapon graphs,
`target_emit_modes`). `flyer_states` keeps its own exclusion (EDX not
compared when `timer_word_step` is replaced).

**The writer's stack mask (`results_writer_cases.h`).** `rwr_mask` maps a
stack value to its offset from the FS:0 record when it lies in the writer's
frame window, else to 0; the record is in the original's frame in one run
and in the core's scratch frame in the other. A value that is no frame
address but happens to lie in the worker thread's stack range would be 0 in
one run and a frame offset in the other, depending on the randomized stack
address (a write's EAX whose high word is a stub's random EAX; the tally
write's EDX = s16(bank word) × 25000). Each record therefore also keeps the
raw arguments, entry registers and stub EAX, and `rwr_same` replaces the
`memcmp` of the logs: every word must be equal, except that a masked word may
differ when its raw value is the same in both runs (a frame address of the
original's frame cannot recur raw in the core's run). The PASS line counts
such words.

## CW3220 runtime passes

These passes run `rtl_*_cases.h` instead of the hook groups. Each has a quick
target `:differential_rtl<...>_test` and a manual full target
`:differential_rtl<...>_full_test`; `rtl_<function>`, `rtl_heap_<name>`,
`rtl_start_<name>` and `rtl_io_<name>` select one function or witness for the
mutation runner. Contracts and results: [rtl_imports.md](../../../docs/reference-game/rtl_imports.md).

### `--rtl`: the pure imports

After the DBSIM mapping the harness maps `CW3220.DLL` from `ES2_DATA_ROOT`
(`CW3220.DLL` or `extracted/cd/CW3220.DLL`, exactly one; size and SHA-256
checked on the read buffer) at an address the system chooses, applies the
HIGHLOW base relocations, points every import of the DLL at a named trap and
makes CODE read/execute. No entry point, TLS callback or import of the DLL
runs.

- Direct cases: per function (the 20 of `core/src/rtl.c`) 20,000 quick /
  200,000 full generated cases in a 64 KiB arena followed by an inaccessible
  page; the export runs on a private test stack with given EAX/ECX/EDX and
  known EBX/ESI/EDI/EBP, then the arena is restored and the own code runs on
  the same addresses. EAX/ECX/EDX, every arena byte and the 16 argument slots
  must be equal; the export must keep EBX/ESI/EDI/EBP, ESP and DF. Formats
  with a floating-point conversion run only the own code (it must decline
  without writing; the export's float path calls imports).
- Installed cases: the proxy's `rtl_imports.h` is installed on the mapped
  DBSIM with its 53 CW3220 slots pointed at the mapped exports (restored at
  the end), the test stack as the owner stack, the 20 imports in mode
  replaced and then compare; 2,000 / 20,000 cases per function and mode go
  through DBSIM's thunk and must equal the direct export call, including the
  4 KiB below the arguments for the leaf functions and in compare mode (the
  return slot for the others); counters `COUNTERS rtl_<function> mode=…
  calls hook mismatches skipped`, mismatches 0.
- Shards: the two targets have two Bazel shards; function f belongs to shard
  `f mod 2` (direct and installed cases both). Every function draws its direct
  cases from a stream of its own and every function and mode its installed
  cases (`rtl_reseed`), so a function's cases do not depend on which
  functions ran before it, and a sharded run executes exactly the cases of an
  unsharded one. `rtl_<function>` runs belong to shard zero.
- `ES2_RTL_TRACE=1` prints each formatter case (format and slots). The fault
  handler prints `during case N` when a case is running.

### `--rtl-heap`: the heap, shadow and replaced

The CW3220 mapping of `--rtl`, then the DLL's two KERNEL32 slots for
`VirtualAlloc`/`VirtualFree` (RVA `0x24310`, `0x24314`) point at an emulated
OS over one 320 MiB reservation of the harness (reservations handed out by a
per-sequence policy: directly following, with gaps, mixed with refused
reservations and failed commits; commits and decommits are real page
operations, a release decommits). The heap's variables are reset and its
init record `0x4170fc` is called before each sequence; operator new's
handler word `0x421fb4` is cleared so that `new` returns NULL like `malloc`
when memory is refused.

- Direct sequences (quick 160 / full 1,600 of 300 operations): malloc,
  calloc, free, realloc, `_expand`, operator new/delete with sizes 0 to
  4.6 MB, random user data in every live block; after each operation
  `core/src/rtl_heap.c` replays it with the recorded OS calls and must give
  the same result, OS calls, allocator variables and every allocator word
  (walk of all regions). `PATHS:` lists the allocator paths reached; each
  must occur. A fixed sequence fills the 64-entry region table.
- Shadow sequences (16 / 160 of 200 operations): the proxy's shim on the
  mapped DBSIM with calloc/realloc/free in mode shadow (CW3220 entry detours
  and OS recorder on the mapped DLL); each sequence runs without and with the
  shim with the same OS decisions and must give identical registers, 4 KiB
  below the arguments and heap, 0 shadow mismatches and the expected origin
  counts; one sequence plants a model error, which must be caught once and
  resynchronized. Entry bytes and slots are restored.
- Replaced sequences (16 / 160 of 400 operations, calloc/realloc/free only,
  `proxy/rtl_heap_own.h` in mode `replaced`, through DBSIM's thunks). Each
  sequence runs first on the original exports, then with the shim installed
  and the own heap pointed at the emulated OS and CW3220's head-block
  address, so that the same OS decisions give the same addresses. Required
  for each step: equal EAX and OS calls; ECX/EDX where the own heap
  reproduces them (free; calloc except an out-of-memory NULL's EDX); and the
  4 KiB below the return address unchanged. Required after the sequence:
  equal allocator variables, an equal hash of every committed byte of the
  window (allocator words and user data), and CW3220's own heap variables
  untouched.
- Mixed sequences (8 / 80 of 300 operations). The own heap uses its own head
  block. Own blocks (calloc through the thunk) and CW3220 blocks (its
  `_malloc` called directly, as CW3220-internal code does) are freed and
  reallocated through the thunks. Required: every CW3220 block goes to the
  original (`rtl_heap_foreign_frees`), a block never changes heap, every live
  block keeps its pattern, calloc blocks are zero-filled, and both heaps pass
  a walk.

### `--rtl-start`: the runtime lifecycle

The CW3220 mapping of `--rtl`, then every import slot of the DLL points to an
own stub. Nine stubs answer from the case's script and log their arguments
(GetEnvironmentStrings, GetCommandLineA, GetModuleHandleA,
GetModuleFileNameA, GetVersion, GetCurrentThreadId, EnumThreadWindows,
MessageBoxA, ExitProcess); any other import ends the case as a failure.
ExitProcess leaves the private stack through `ls_escape` (FS:0 restored, then
the x87 state).

- Model cases (quick 4,000 / full 40,000): a synthetic module table (up to 14
  init and 14 exit records with random done flags, priorities with ties,
  function-0 records), WinMain (stdcall or cdecl) or main, a command line,
  initial atexit table, stream hooks and SIGABRT entry, and scripts for 64
  record functions and WinMain (atexit registrations; return, exit, `_exit`,
  `_cexit`, `_c_exit`, abort, `__ErrorExit`). The export `__startup` runs on
  a 256 KiB private stack with `[esp] = 0`, `[esp+4] = table`; then
  `core/src/rtl_start.c` runs on the same table, its env calling the same
  synthetic functions with the ESP the model predicts. The event logs (every
  guest and Win32 call, arguments, ESP relative to the outermost runtime
  entry, x87 control word; WinMain's x87 status/tags and SEH record), the
  table bytes, the runtime variables and the model's return address
  (`return_rva`, the `site` of each Win32 call) must be equal. `PATHS:`
  counts each scenario and must reach all; `DEPTH:` prints WinMain's and
  ExitProcess's offsets for a DBSIM-shaped case.
- Switched cases (1,500 / 15,000): the proxy's lifecycle group
  (`proxy/rtl_life.h`: `__startup`, `_atexit`, `__exit`, `_abort`,
  `__ErrorExit` in mode `replaced`) against the original, generated as the
  model cases but restricted to what DBSIM can call: `_atexit`, `__exit`,
  `_abort` and `__ErrorExit` through the mapped DBSIM's import thunks, from
  WinMain, the record functions, the atexit functions and the SIGABRT handler
  (C `exit`, `_cexit` and `_c_exit` are not DBSIM imports). Two runs per case
  from the same stack pattern, registers, x87 state, table and CW3220
  variables: original (DBSIM's slots hold the mapped exports; the export
  `__startup` runs at the anchor) and switched (`rtl_install` installs the
  group on the mapped DBSIM and CW3220 with the live checks, and the run
  enters `rtl_life_startup_entry` at the anchor, as the arming does).
  Compared: the model cases' events, and per call a frame: the registers at
  the callee's entry (all seven at guest calls, at the start-up's three Win32
  calls, GetModuleFileNameA and ExitProcess; EBX, ESI, EDI and EBP at the
  later `__ErrorMessage` calls) and every stack word from ESP+4 (for
  ExitProcess from ESP) up to `[anchor+8]`. Then the table bytes and CW3220's
  variables; the atexit table is compared with the lifecycle's, and CW3220's
  own table must stay untouched. The word at the callee's ESP must be
  `rtl_life_return` in the switched run, and the model's return address must
  equal the original's. `SWITCHED PATHS:` counts 30 scenarios; each must be
  reached.

### `--rtl-io`: file and console imports

Quick 800 / full 8,000 sequences. The CW3220 mapping of `--rtl`, then the
KERNEL32 slots the I/O code uses point to an emulated OS (in-memory files
with attributes, a terminal, a pipe, a read-only, a failing, a "disk full", a
carriage-return-only and a sink file, four read-only copies of small files of
the CD's `DATA` directory; handles with access and position; a find iterator;
a scripted clock; VirtualAlloc/VirtualFree over a reserved window) that logs
every call, stack structures by content. Every sequence starts from CW3220's
own heap, handle-table and standard-stream initialisation; `_fmode` and the
time zone vary.

- Direct: 60 generated operations per sequence over the 21 exports (plus
  set-ups: every handle or FILE in use, CW3220's heap exhausted, a terminal
  stream, and directed scenarios every tenth sequence); each runs on the
  export, then on `core/src/rtl_io.c` from the same state: EAX, the OS call
  log, CW3220's DATA section, the heap window, the test arena and the OS
  state must be equal (for the console stubs: EAX, and nothing changed).
  Operations C forbids and the export turns into memory corruption are not
  generated (input right after output and output over unread input without a
  seek; `__fputc` other than at level 0, the macro's precondition).
- `PATHS:` counts 63 scenarios, all must be reached; branch coverage puts an
  INT3 on the 315 conditional branches of the CW3220 code behind the set
  (`rtl_io_branches.h`) during the direct cases; each must be taken both ways
  or be listed with its one edge and the reason in `rtl_io_branch_notes.h`.
- Installed: the proxy's shim on the mapped DBSIM, the 18 file imports in
  shadow, then all 21 in replaced; each sequence runs without and with the
  shim from the same start, through DBSIM's thunks: EAX after every step and
  the final state equal, 0 shadow mismatches and skips; a planted model error
  is caught once; the console stubs through the thunks in replaced.

### `--rtl-own`: the own cw3220.dll against the original

`differential_rtl_own_test` (quick, about 12 s) and
`differential_rtl_own_full_test` (manual, about 120 s) load the own DLL
(`//rtl:cw3220.dll`, path in `ES2_OWN_CW3220`) into the harness, where its
DllMain runs in own mode, and compare its exports with the mapped original's
(`rtl_own_cases.h`). The DLL's `es2_own_runtime` descriptor gives its state;
its `reinitialize` reruns the attach-time set-up after the harness pointed
the DLL's KERNEL32/USER32 slots at the pass's emulated OS. The test stack is
the owner thread's stack, so every call takes the DLL's private-stack adapter
as DBSIM's do.

- **Tables.** `__ctype` with the tolower table below it (0x201 bytes from
  `4217b4`), the DOS error map, the scanner classes and the static I/O state
  against CW3220's data as linked; the data exports' addresses.
- **Pure.** The `--rtl` generator over the 20 exports, 4,000 / 40,000 cases
  each on the owner stack and a tenth from a foreign stack (the
  caller's-stack path): EAX/ECX/EDX, arena, argument slots, and below the
  arguments the leaf functions' pushed registers and nothing else. Counted,
  not hidden: empty `sprintf`/`vsprintf` results (EDX is CW3220's formatter
  residue, the DLL leaves 0) and `%p`/`%n` formats (outside the formatter).
- **Heap.** The `--rtl-heap` replaced sequences with the DLL instead of the
  shim: calloc/realloc/free through DBSIM's thunks over the emulated OS with
  CW3220's head-block address (24 / 240 sequences of 400).
- **I/O.** Two worlds over the shared memory (CW3220's data, arena, OS state,
  heap window), swapped per call: the original's, started by CW3220's init
  records (`4170fc`, `409014`, `407248`, `41e268` over a generated
  environment and time zone), and the DLL's, started by its `reinitialize`.
  Each step is generated in the original's world (the `--rtl-io` generator)
  and run in both; after the start-up and every step: EAX, the logged OS
  calls, OS state, arena and window bytes and the I/O and heap variables,
  FILE pointers, tokens and stream-hook values translated between the
  runtimes. The console stubs are compared by EAX (the DLL writes nothing).
  The sequence ends with the stream hooks exit runs (`407ca4`, `407c7c`).
  200 / 2,000 sequences of 60 steps.
- **Lifecycle.** The switched `--rtl-start` comparison with the DLL's exports
  in DBSIM's slots and DBSIM's `__startup` thunk as the entry (1,500 / 15,000
  cases). `rtl_start_cases.h` takes the DLL's trampoline return address, its
  model's return RVA and a map of its addresses (frame handler, window
  callback, abort text, dummy hook) to CW3220's.
- **Exceptions.** `__ExceptionHandler` of both on every context entry of
  DBSIM's 267 frame tables (found from the callers of `49630c`), for
  exceptions other than a C++ throw outside an unwind; also a C++ throw in the
  search phase (entries without a catch) and an unwind with nothing
  constructed (entries whose clean-up calls nothing; CW3220's helpers that
  would call DBSIM code start with `int3`).
- **Startup handler.** The handler of `__startup`'s frame record against
  CW3220's `405160`, both runtimes' `UnhandledExceptionFilter` slots at a stub
  (CW3220's `[0x424308]`): 540 matrix cases (nine CPU codes, flags 0, 1, 2,
  4, 6, filter answers 0, 1, −1, the `_signal` words `[0x42341c]` and
  `[0x423418]` at 0 or `405154`) and 2,000 / 20,000 random ones (any code but
  `0EEFFACEh`). Disposition, one filter call with the given record and
  context, nothing written.
- **Foreign.** `_atexit` from a thread of its own against the original's
  (registers, count, table; 700 / 7,000), `@_CatchCleanup$qv` there without a
  context block and with an empty list.

## Harness notes per group

The case header of a group or suite describes its world, stubs and passes.
The notes below add what the header alone does not show: the target, the
pass structure, and where the contract lives. `ES2_INVITRO_<GROUP>_CASES`
caps a group's direct cases.

### weapon_syncs

`weapon_syncs_cases.h` (identifiers `wsy_`/`WSY_`): the six slot `+50h`
gauge syncs and the LAEW emission `laew_fire` over a 2 KiB arena with a
synthetic gauge widget (its table points at logging stubs for the slots
`+18h`, `+14h`, `+10h`; the received record bytes are logged instead of the
local record's stack address) and patched callees; installed 31/a7 cases;
`movement_step` real and nested for gun_sync and laew_sync; `weapon_prepare`
real with its own hook under laew_fire (the entry shim's residue bytes, as in
weapon_fire's pass). The six empty `weapon_none_*` bodies run slot_leaves'
`slv_leaf_cases`. Contract: weapon_classes.md, "the weapon gauge syncs, the
LAEW emission and the empty bodies".

### mission_end

`mission_end_cases.h` (identifiers `mse_`/`MSE_`): the mission end
`mission_end` 461ff0 over a 1 KiB object arena and the global windows of the
mapped image (flag block, run block 4d24b0..4d2540, exit code 4d27b8, word
4a9e52). `_fclose` 496838 and `results_writer` 42412c are patched to logging
stubs that change the stream words, the quit, demo and bank words, the object
byte and the object pointer after the call; the bare-RET exits 426514 and
425344 stay original. Installed 31/a7 cases; no real-callee pass (file close
and file writes). Contract: launcher.md, "the mission end `0x461ff0` as own
code".

### init_records

`--init-records` (target `:differential_init_records_test`, one pass, no
quick/full split) runs DBSIM's c0w32 entry `0x401000` and its 65 init and 26
exit records as machine code (CW3220's selector as the `--rtl-start` model;
the entry's jump into `__startup` returns through a stub; `_memset`,
`_memmove`, `_calloc`, `_free`, `CreateFileA`, `CloseHandle`,
`GetModuleHandleA` on logging stubs, the heap as the `--rtl-heap` model over
an emulated region), then `sim/src/init.c` with the sim services from the
same start, and compares after each of the 92 steps DATA
`0x497000..0x6cc000`, the two dwords below the worker's StackBase, the heap
head and region, the selected VA, and at the end the call logs and
`CreateFileA`'s arguments; the first differing step is rerun and its bytes
printed. It also prints per step the bytes the original changed and the
counter delta, and probes the real `CreateFileA` with record 64's arguments.
Results: [own_process.md, "the static initialisation as own code"](../../../docs/reference-game/own_process.md#the-static-initialisation-as-own-code).

### mission_root

`mission_root_cases.h` (identifiers `mrt_`/`MRT_`): the mission-load root
`mission_root` 461608 over a 28 KiB arena (six pools with linked objects, four
removal lists, groups, type records, the terrain-object table, terrain
objects, counters, key devices, a spare camera and the blocks new returns)
and the global windows of the mapped image (the view block 4d24bc..4d257b,
the camera array 4d2684..4d26a7 and 29 further ranges). Its 62 direct callees
are logging stubs (the thunks 49694c/496970 pop their two slots; the bare RET
463370 stays original) and the graphics slot [4a5824] points at a slot stub.
Direct cases compare the original with the core (its frame at the original's
EBP, frame bytes compared) and, with a zero stack pattern, with the adapter's
plain path; the frame probe runs the hook through its entry shim (real
49630c, EBX/ESI/EDI, EBP/ESP, residue, SEH record, 1,024 bytes below the
caller) and the depth probe measures the 112-page gap plus host frames; the
real-callee pass runs the real 49630c, pool_prev/pool_next, removal_append
and dist_gate in all 32 mode combinations. The stubs log addresses inside the
root's frame relative to its EBP, and the adapter's buffer frame lies 4 bytes
below the original's, so a data word near either frame would normalise
differently on the two sides (with ASLR, a rare failure such as
`f000fed0`/`f000fed4`). The generator therefore keeps every 32-bit data word
the root forwards out of the worker stack (bit 31 set, a signed 16-bit value,
below 1 MiB, or an arena or image address), and each original direct run
fails on a logged argument or register inside the worker stack's reservation
outside the frame window (the FS:0 head excepted). Contract: mission_load.md,
"The mission-load root".

### startup

`--startup` (target `:differential_startup_test`, one pass, about 30 s; reads
`VOL\` of the CD and writes synthetic archives into a temporary directory,
deleted after) runs DBSIM's command-line split `0x4652f8`, both option
parsers `0x45e6cc`/`0x45e758` (4,000 random command lines; the tape options
excluded), the string-group reader `0x4539a0`/`0x453918` (4,000 holders) and
the VOL registration `0x473260` (the CD's `VOL\`, synthetic archives) as
machine code and then `sim/src/startup.c`/`volumes.c` with the sim services
from the same state, and compares DATA `0x497000..0x6cc000` (with the
construction counter at a free BSS word `[0x6cbee4]` points at), the emulated
heap region, argc/argv, the string slots, CW3220's handle flags and FILE
array of the `--rtl-io` model, and the import-level call sequence
(`startup_cases.h`; the import stubs use the `--rtl`, `--rtl-heap` and
`--rtl-io` models the sim services use). Results:
[own_process.md, "start-up, parsers and file services"](../../../docs/reference-game/own_process.md#start-up-parsers-and-file-services).

### loop

`--loop` (target `:differential_loop_test`) runs the DIB section `0x464d44`
and the surface release `0x480f38` (64 cases: video modes 0 and 1, an earlier
DIB, random palette bytes; the GDI imports answer as the headless platform),
the timing init `0x467840`, limiter `0x4678c0` and clock `0x467828` under
virtual time (2,000 sequences; time_scale as the original on both sides), the
pump `0x464c78` over random journals (4,000; PeekMessageA/TranslateMessage a
model of the proxy's replay order), MainWndProc `0x466034` with random
handler lists (4,000) and the rows `0x465fec`/`0x466010`/`0x479cfc` (2,000
sequences) as machine code and then `sim/src/loop.c`/`frame_buffer.c` from
the same state, and compares DATA `0x497000..0x6cc000`, the emulated heap,
the call logs, the atexit table, the dispatched messages, peeks, cursor calls
and EAX/EDX (`loop_cases.h`; it reuses `startup_cases.h`'s reset and
snapshots). `0x480f38` is called through a thunk that keeps the harness's
registers: it returns with ESI = 0 (the original's `0x4894a5` removes one
slot too many), checked every case. Results:
[main_loop.md, "the main loop, pump, window procedure, limiter and clock as own code"](../../../docs/reverse-engineering/main_loop.md#the-main-loop-pump-window-procedure-limiter-and-clock-as-own-code).

### hdd_map

`hdd_map_cases.h` (identifiers `hmf_`/`HMF_`): the HDD map frame `map_frame`
44c988 over a 16 KiB arena (the map object with its mode buttons and the
dwords below them pointing at button objects, the wingman-button array, three
order records, four target actors, two graphics contexts, the label bytes;
for the real pass the gadget and map_areas' cursor block) and eight global
windows of the mapped image (49d7e6, 49d6ac..49d6b9, 6c5f70, 49b0ac..49b0bb,
the label tables 4d12a8..4d1337, 4d3ba2, 49b088, 4d1638). Its 13 direct
callees are logging stubs that also record the record behind [X+22Ch] of both
contexts (and the rectangle of 43122c); frame-valued words are compared
relative to the run's frame (the original's EBP, `hdd_map_plain_frame` of the
adapter's plain path, or the slot address - 8 when installed). Direct cases
run the adapter's plain path; the installed probes run the hook through its
entry shim (EBX/ESI/EDI, EBP/ESP, residue below each callee and the residue
word [EBP-8] behind the context pointer); the real-callee pass runs the real
44d370 and map_areas (its four callees stubbed) in all four mode
combinations. Contract: cockpit.md, "HDD map frame".

### hdd_map_keys

`hdd_map_keys_cases.h` (identifiers `hmk_`/`HMK_`): the HDD map key path
(`map_key` 44cc68, `map_press` 44d5c0, `wingman_pick` 44d82c, `wingman_index`
433134, `mode_next`/`mode_prev` 44ee48/44ee88, `target_cycle1`/`target_cycle0`
44ef08/44ef1c, `target_cycle` 44ef30) over an 8 KiB arena (the map object
with its mode buttons and map view, two gadgets and holders, the graphics
context and its offsets object, twelve actors with type records, the actor
list with a null guard) and six global windows of the mapped image
(49d6ac..49d6b3, 49b088, 4a9af8..4a9aff, 4d03c8..4d03d3, 49dbe0,
4d2556..4d255d). All 23 callees of the group are logging stubs except the
hook's own function; the stubs also record the points behind the pointers of
44d3cc, 48c5fc and 44d24c and write the outputs of 486ece, 48c57c, 48c5fc and
44d24c. 44f03c's stub accepts every call from the 40th on: with the start
entry behind the count word the original's cycle has no other end. The
real-callee passes run the group's own functions, 44d3cc/44d24c (under
map_press), 433134 (under 44d82c) and map_mode 44d9f4 (under the mode steps)
as original code. Contract: cockpit.md, "HDD map key path".

### input_layer

`--input-layer` (target `:differential_input_layer_test`, one pass) runs the
input set-up `0x45a5dc` (2,000 random states: prefs.cfg missing, short or
full with each joystick byte, the voice archive, keyjoy values, joystick
journals per device, the capture bytes, partly used hot-key, handler and
graphics-slot tables), the device poll `0x45baa8` with the readers
`0x45bbc4`, `0x45c524`, `0x477904`, the prefs setter `0x459958`, the flush
`0x477abc`, the bounds `0x45b758`, `0x49667c` and `0x481350` (2,000 sequences
of up to six polls), the mouse queue set-up `0x452ae4` and the handler
`0x4809f8` (2,000 message sequences) and the read and write streams (1,000
cases) as machine code and then `sim/src/input.c` from the same state, and
compares DATA `0x497000..0x6cc000`, the emulated heap, the call logs, the
journals, the prefs file and EAX/EDX (`input_layer_cases.h`; it reuses
`loop_cases.h`'s loop and `startup_cases.h`'s file model and snapshots). It
writes, reads and deletes a real file per side, which makes it the longest
suite. The hooks the set-up calls (`prefs_load`, `keyjoy_load`,
`prefs_store`, `mouse_queue`) and the sound groups run as the original on
both sides. Results:
[input.md, "the input layer as own code"](../../../docs/reverse-engineering/input.md#the-input-layer-as-own-code).

### effect_pools

`effect_pools_cases.h` (identifiers `epc_`/`EPC_`): the effect pools with
resources (`explosion_pools` 407b54, `debris_pools` 408860, `debris_set`
40874c, `debris_record` 4083f8, `flat_pools` 4097a8, `meteor_pools` 409a34,
`fire_pools` 46b1a8, `smoke_balls` 408fb8) over a 32 KiB arena (two stream
tables whose slot +18h is a read stub, two shape lists over 16 shape objects,
the debris set, record and stream, a bump area) and nine global windows of
the mapped image (the three directory pointers, the low-memory word 4d2680,
the fire base name 4a0a74 with its terminator, the guard byte 498098,
4a961e..4a96bb, 6b4f28..6b4f3b, the counter pointer 6cbee4). All 18 direct
callees are logging stubs; frame addresses are logged relative to the hook's
record. The real-callee pass runs the real 49630c and 49667c, a bump
allocator for new/new[] and pool_create under the six pool hooks, debris_set
and debris_record under debris_pools and debris_record under debris_set in
every mode combination (the saved head of a nested record is logged as a
mark). Contract: mission_load.md, "Effect pools with resources".

### fit_leaves

`fit_leaves_cases.h` (identifiers `fit_`/`FIT_`): the remaining callees of
`weapon_fit` (`weapon_shape_word` 40e61c, `mount_entry` 40fea0, `mount_table`
420634, `shield_split` 413ac8, `energy_rate` 417d08, `equip_slots` 40fb2c,
`node_state_fill` 417530) over a 10 KiB arena (actor, weapons and
definitions, item array, equipment record, shield record, a tree of up to 16
nodes with child lists and word arrays, the part tables of the real
`part_ratio`) and the two globals 4a98e4 and 4a9d70 of the mapped image. The
direct callees (memset 49681a, multiply_q10 47e0b0, part_ratio 40dbc0,
mount_entry 40fea0, the node class getter in an arena vtable) are logging
stubs; the original runs through a naked caller that also captures ECX, so
the case compares the original, the adapter and the core (EDX:EAX, the
core's ECX, the call logs, the arena and the globals). The real-callee pass
runs the real `mount_entry`, `multiply_q10` and `part_ratio` nested in
rotating modes under `mount_table`, `shield_split` and `energy_rate`.
Contract: mission_load.md, "The remaining weapon_fit callees".

### camera_view

`camera_view_cases.h` (identifiers `cmv_`/`CMV_`): the outside camera and the
view subject (`camera_attached` 401078, `camera_orbit_set` 40109c,
`camera_attach_set` 40111c, `camera_turn_step` 401c20, `camera_speed_step`
401c4c, `outside_camera` 401c74, `view_apply` 45de30, `view_subject_set`
45df34, `view_offsets` 45edb0, `camera_current` 45edf8) over an 8 KiB arena
(eight camera objects, the player and its group record, subjects with an
arena vtable, the display record, the runtime counter, offset records) and
the mapped globals 4D24BCh..4D253Fh, 4D2684h..4D27A1h, 49F1ECh, 6CBEE4h and
4CF99Ch. Every direct callee (multiply_q8, approach_word, the two steps,
view_subject_set, 45e208, 48bd84, view_offsets, the two setters and the
subject's slot +30h) is a logging stub; 49630c is a record stub that links a
record at EBP + [EAX+4] as in `beam_cases.h`, so the core runs with the
adapter's synthetic frame and restores FS:0 like the adapter. Values on the
stack (the frame records, the FS:0 head) are masked in the logs. The case
compares the original, the adapter and the core (EDX:EAX, the core's ECX
except camera_orbit_set's frame address, the call logs with arena and globals
hashes, the arena, the globals, the callee counter deltas). The real-callee
pass runs the hooked callees as original code nested in rotating modes:
multiply_q8 and approach_word under the steps, the steps and both under
outside_camera, the setters under view_offsets, view_offsets and the setters
under view_subject_set, and the four under view_apply. Contract: camera.md,
"The hooks (group camera_view)".

### dialogs

`--dialogs` (target `:differential_dialogs_test`, one pass) runs the
mission-end alert `0x456000` and the quit confirmation `0x457b00` (1,500
cases), the options dialog `0x456d68` (800 cases), random sequences of the
single dialog rows between an open and a close (2,000 cases) and
`WM_CLOSE`/`WM_QUERYENDSESSION` through the window procedure `0x466034`
(1,000 cases) as machine code and then `sim/src/dialogs.c` (and
`es2_sim_window_proc`) from the same state, and compares DATA
`0x497000..0x6cc000`, the emulated heap, the stub log (every stub with its
arguments), EAX (AL for the key handler and the prefs rows), the messages
dispatched, the peeks, `SetCursor`, the journal's state, `SetCursorPos`
against the own `headless` count, the virtual time and the own clock sites
against main_loop.md, "the modal dialogs" (`dialogs_cases.h`; it reuses
`loop_cases.h`'s loop, message model and virtual time, `startup_cases.h`'s
snapshots and runtime stubs and `input_layer_cases.h`'s pass-through). Both
sides build the same synthetic dialog, panel, gadgets, ports, cockpit and
surface on the emulated heap from one seeded generator; each modal case has
a random journal (the pump-keyed entries, `WM_QUIT`) and a random pass script
written by the control-record stub `0x45a810` (which runs the original pump
on the original side and the own pump row on the own side). The other code
the dialogs call is stubbed identically on both sides (jumped to from the
original entries; services at the same VAs on the own side); `0x486e7c`,
`0x48993a` and `0x4530bc` run as the original on both. Results:
[main_loop.md, "the modal dialogs in the own process"](../../../docs/reverse-engineering/main_loop.md#the-modal-dialogs-in-the-own-process).

### sim_leaves

`sim_leaves_cases.h` (identifiers `sml_`/`SML_`): the 17 last simulation
leaves (`part_shapes_bind` 4030d0, `parts_intact` 403668, `formation_slot`
405b9c, `explosion_entry` 407b20, `flat_draw` 40991c, `projectile_draw`
40a120, `beam_done` 40c2a0, `weapon_by_mount` 410670, `ptr_list_add` 411dd4,
`box_square_init` 413a90, the two sound latches 41a6d0/41a994, the type
indexes 41ff00/422e80, the notices 420a68/420a98 and `notice_time` 426aec)
over a 6 KiB arena (the object, a child and its two vtables, record and list
areas) and the mapped global windows (60 bytes in all: 4975d2, 4A5B04,
49A1C8, 49B088, 4A95AC, 4A9628, 4A9E5C, 49F8EC, 4D2CFC, 4D3B64). Every game
callee (terrain_pose, shape_actor_matrix, timer_word_step, sfx_playing,
port_a, the child's slot +1Ch and the event port's slot 0) is a logging stub
with a per-case result stream; values on the stack are masked and the notice
records' residue bytes are never compared. The case compares the original,
the adapter and the core (EDX:EAX, the core's ECX against the original's,
the call logs with arena and globals hashes, the arena, the globals, the
callee counter deltas). The six readers also run the compare probes. The
real-callee pass covers `beam_done` (timer_word_step as a nested hook in
rotating modes; EDX is not compared when it is replaced), the two latches
(real `sfx_playing`, sound device absent) and `radio_notice` (real
`port_a`); `flat_draw` and `projectile_draw` have none (their callees draw
through matrix code that faults on random bytes). Contract: vtables.md,
"simulation leaves".

### mfd_keys

`mfd_keys_cases.h` (identifiers `mkc_`/`MKC_`): the MFD key handlers
`mfd_key` 4469e8, `mfd_alt_key` 446c38, `mfd_key_select` 447158,
`menu_select` 43f9e0, `mfd_entry_next` 44703c and `mfd_entry_prev` 447070
over a 4 KiB arena (the MFDGauge at +100h, its menu at +400h, a second menu
at +480h) and the mapped enable table 49cbd8 (104 bytes, 13 per MFD mode).
The ten game callees (panel_visible 431410, button_press 438d9c, mfd_range
446ff0, the entry steps 44703c/447070, mfd_key_select 447158, menu_select
43f9e0, menu_code 43f9c0, mfd_order 447248, mfd_entry 4470a4) are logging
stubs with a per-case result stream. The case compares the original, the
adapter and the core (EDX:EAX, the call logs with arguments and registers,
the arena, the table, the callee counter deltas); the installed probes run
256 states per hook in fast and poison modes. The real-chain pass runs the
five non-leaf hooks over the real callees, with panel_visible, button_press
and mfd_order as stubs and the nine chain hooks (this group's six and
mfd_orders' entry, code and range) uninstalled or replaced at random,
compared with the 1,024 stack bytes below the slots; it is the pass that
needs the menu_select shim's ECX reload (446c38 calls 447248 with
menu_select's ECX = k). `menu_select` also runs the compare probes (span
menu+32h, 7 bytes). Contract: cockpit.md, "MFD keys".

### hdd_exec

`hdd_exec_cases.h` (identifiers `hxc_`/`HXC_`): the HDD button executor
`hdd_execute` 44a1a0 over an 8 KiB arena (the HDD record at +100h, two
HDDMap objects at +800h/+C00h, the wingman table at +1000h, four actor
records from +1100h, two graphics contexts at +1400h/+1700h) and four mapped
globals (the context pointer 6c5f70, 4d3b96, 4d03c8, 49d6b0). The 20 direct
callees and the six callees of the real order chain are logging stubs with a
per-case result stream; each record holds the arguments and EAX/EDX/ECX
relative to the frame, the written locals, the record behind [X+22Ch] and,
in the installed and real passes, the caller's EBX/ESI/EDI, EBP/ESP, the 256
bytes below the callee's ESP and a hash of the 2Ch bytes below f. Passes:
direct (original, plain adapter, core; coverage of all 15 jump-table entries
of 44a1f7 in the first 16,384 cases, callee counters), installed probes (256
states, fast/preserving, 31/a7 stack, through the entry shim; the frame
pointer stored in at least one run, changed argument slots), and real chain
(4,096 cases over the real 44db9c, 44db4c, order_cancel, wingman_select and
map_mode with the four hooks original/replaced at random). In the real pass
the callee-saved registers, stack position and residue are compared only for
44a1a0's own callees at its outgoing ESP, and the ECX of the callees below a
replaced hdd_orders hook is not compared (the hdd_orders replacements do not
reproduce ECX). Contract: cockpit.md, "HDD button executor".

## shape_draw, actor_draw and gl_gouraud: stack values

The suites on `shape_draw_cases.h`'s world tell stack addresses apart by value
(`sd_is_stack`): on the original's side the committed part of the worker stack,
on the own side the call area `lc_stack`. Such a value is logged or compared as
"stack or not" (the context's mode record `+0x22c` in the fill stubs, EAX
results) or replaced by one marker (the matrix and intensity pointers in DATA).
A generated data word that reaches a classification must therefore lie in
neither range, which move with ASLR. The one generated word that does, the
context's `+0x22c` while no polygon has replaced it, keeps bit 31 set
(`sd_world_base`). On the original's side every classification of that word
inside the worker stack's reservation or the call area fails the case.

The other normalisers that map stack values by value need no such rule:
`jb_norm` (cockpit) and `sr_norm` (shell_rest) turn everything outside the
world and the image into one tag, and `csy_norm` (cockpit_sync) subtracts the
callee's EBP only at positions that always hold frame addresses.

## Hook groups: stack values

In `differential_harness` the original, the core and the adapter run on the
same worker thread, so a classifier over the whole 16 MiB stack reservation
(`StackBase - 16 MiB .. StackBase`) treats a value the same way on every side.
The risk is a window that lies elsewhere on each side: relative to FS:0 (the
original's record against the core's scratch record), to a run's frame base,
or to one exact frame address. A generated data word that falls into one
side's window is classified there and passed raw on the other, so the case
fails rarely and depending on ASLR. The rule for such a classifier: every
generated word that can reach it is kept out of the reservation, and a guard
fails the case if one still does. Where the original computes values in the
reservation from generated data, filtering cannot work; there the comparison
accepts a classified mismatch only when the raw words are equal.

- `stack_clean` (in `harness_base.h`) flips bit 31 of a word inside the
  reservation; `stack_clean_bytes` does the same for every dword of a random
  byte area at every offset. Neither consumes the stream, so the case stream
  stays the same; only the rare word inside the reservation changes, equally
  for every side.
- `stack_uncommitted` is the guard: a classified value in the reservation's
  uncommitted part (`AllocationBase .. StackLimit`) is never a live stack
  address. Raw words land there about once in 256, so a generator site
  without `stack_clean` trips the guard within a quick run.

Per harness:

| Classifier | Window | Data words kept out by | Guard |
|---|---|---|---|
| `bty_rel` (building_types) | FS:0 record | `bty_value` | — |
| `dmd_rel` (damage_data) | FS:0 record | `dmd_next` skips reservation words | — |
| `epc_rel` (effect_pools) | FS:0 record | `epc_value` | — |
| `st2_rel` (stage2_types) | FS:0 record | `st2_value` | — |
| `mw_frame_relative` (mission_weapons) | weapon_fit frame, FS:0 + 28h | `mw_value`, `mw_random` | `mw_alias` |
| `tyl_rel` (type_loads) | frame base | not filterable: the original computes stack-range values from the records; `tyl_same` accepts a classified mismatch when the raw words are equal | `tyl_same` |
| `hxc_rel` (hdd_exec) | exact f-8, f-20h | `hxc_value`, the arena | `hxc_alias` |
| `hmf_rel` (hdd_map) | exact f-0Ch, f-20h | `hmf_value`, the arena | `hmf_alias` |
| gauge_sync ESI, weapon_frame record (weapon_slots) | contents read at a committed stack address | stub words, the arena, weapon_frame's record argument | `wsl_alias` |

No rule is needed for these:

- `rwr_mask` (results_writer) has an FS:0 window, but `rwr_same` accepts a
  masked mismatch when the raw words are equal, which a data word always is.
- Reservation-wide tags: `cq_mask` (col), `wph_mask_low`/`wph_mask_full`
  (weapon_helpers), `wsl_normal` (weapon_slots) and the generic
  `*_stack` predicates that map stack values to 0.
- World/image tags: `h12_p` (hud_radio) and `m2_p` (mfd_rest2) keep world and
  image values and turn everything else into `0x57ac0000`.
- Anchors that are equal on both runs: `lcb_mask`, `bcc_mask`, `mcs_mask`,
  `mf_mask` and `mkc_mask` take offsets from the invoke origin, which is the
  same ESP in both runs; `cfx_mask` (config) anchors on the first stack value,
  and its stub words carry bit 31; `pj_mask` (projectile) cleans stub results
  with `pj_clean`.
- `hx_in_stack` (hud_pixels) sees only pointers: the context words it classifies
  hold a heap record.

## Dispatch tables of the tests: size and duplicate VAs

This rule holds for the in-vitro cases here and for the tests under
`sim/tests`.

- A test's dispatch table has 4,096 entries (`es2_sim_entry table[4096]`,
  `es2_sim_dispatch_init(&d, table, 4096, ...)`), the same as
  `ES2_SIM_MAX_ENTRIES` in `sim/include/es2/sim/host.h`. Tests that check the
  table's own limits (`sim/tests/table_test.c`, `init_test.c`, `trap_test.c`)
  keep their small sizes on purpose.
- `es2_sim_dispatch_seal` sorts the entries by VA and refuses a VA that is
  listed twice, so a test must not add a hook, service or headless entry for a
  VA that the service rows (`es2_sim_service_row`) already hold.
- A test that stubs such a VA skips it before the seal and replaces it after
  the seal: look the entry up with `es2_sim_dispatch_find` and set its
  `service` and `service_user`; keep the row's `pop_bytes` and `stack_slots`,
  because they are the original function's calling convention. Whether a VA has
  a row is checked by a lookup over `es2_sim_service_row`, as
  `sd_has_row` in `shape_draw_cases.h` does. Examples: `shape_draw_cases.h`,
  `cockpit_cases.h`, `hud_mfd_leaves_cases.h`, `sim/tests/gfx_test.c`,
  `gouraud_test.c` and `hud_test.c`.
- A stub for a VA without a row is added before the seal as usual.
