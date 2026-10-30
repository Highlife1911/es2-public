# DBSIM map

The DBSIM map says which functions of the original `VER95/DBSIM.EXE` exist
(known function starts), which of them run in the oracle suites, how often,
and what each is made of statically. It is an analysis instrument: the
function index ([docs/functions/](../functions/README.md)),
[hook_candidates.md](hook_candidates.md), [hook_frontier.md](hook_frontier.md) and
[sim_gap.md](sim_gap.md) are generated from its `map.json`. Addresses refer
to the binary of [reference_version.md](../reference_version.md) at image base
`0x00400000`.

| Part | File |
|---|---|
| Probe inventory, known starts, probe kinds | `tools/reference-game/scripts/probe_instruction_inventory.py` (its docstring is the normative text of the rules below) |
| Generated tables | `tools/reference-game/proxy/probe_entry_inventory.h` (DBSIM), `vshell_probe_inventory.h` (VSHELL) |
| Probe engine | `proxy/entry_probe_engine.h`, `proxy/entry_probes.h` |
| Batch plan and `map.json` | `tools/reference-game/scripts/dbsim_map.py` (`plan`, `build`; test `dbsim_map_unit_test`) |
| Probe run | `tools/reference-game/setup/run-live-coverage.ps1 -ProbesOnly` |

Evidence: `<es2-data>\logs\dbsim-map-20261012` (every hook original,
the baseline) and `<es2-data>\logs\dbsim-map-20261020` (all hooks
replaced, the current map). Each holds `map.json`, the batch directories
with one `probes.json` per suite, and `plan/`.

## Method

1. **Known function starts.** The starts are instruction boundaries of the
   synchronized descent ([Probe inventory](#probe-inventory)): the entry point,
   the exports, `55 8b ec` pointer targets, linear `call rel32` targets,
   operand pointers, the targets of reached calls and reached starts that a
   relocated dword outside the jump tables points to: **3,010 starts**. An
   entry is an instruction boundary, not a function identity (interior
   labels, this-adjusting thunks and RTTI records in CODE are among them).
2. **Live counts.** Every probeable start gets an entry probe
   ([Entry kinds](#entry-kinds)) in batches of at most 512 (`EP_MAX_ENTRIES`,
   the limit of `run-live-coverage.ps1`; a list of 512 plain entries is about
   3,600 of the 8,192 characters that `[probes] entries` accepts). Probes
   only, saturate mode with N = 1000 (exact below 1000, "at least 1000"
   above), each batch over all 40 registered suites with the golden check.
   Arming is early, at the mission-load call `0x45f42b`
   (`configure-tick-log.ps1`, default; `ES2_EARLY_ARMING=0` switches it off):
   the load phase is counted, start-up and static initialisation read 0.
3. **Static summary per start.** A control-flow walk over the reached
   instructions: instruction count, direct callees and tail calls, indirect
   calls, jump tables, x87 instructions, distinct absolute globals. Jump
   tables are followed; indirect calls are not, so virtual methods and
   callbacks appear as separate roots. No row of the current map has an x87
   instruction (the reached code has none).
4. **First-tick report.** The probe DLL writes the counts a second time at
   the first frame boundary `0x45f4ad` (`probes-load.json`). The window runs
   from the arming to that boundary: the mission load **and** the first pass
   of the loop body. `map.json` keeps those counts per suite as `load` next
   to the final `counts`. A function is "load only" when both are equal in
   every suite; that is exact only while the load count is below N, a
   function saturated in the load window may still run in frames.

Limits: functions reached only through computed addresses are missing from
the starts; "never run" means never run in these suites after the arming;
saturated counts are lower bounds (for exact totals of a chosen subtree run
count mode on its entries only).

## Probe inventory

`probe_entry_inventory.h` lists instruction boundaries with their one-byte
opcode (`probe_instructions`, 24,202 rows) and the second table
`probe_entry_kinds` (326 rows, [Entry kinds](#entry-kinds)). The proxy refuses
every probe VA outside the inventory (`entry_probes_config_refused`).

**Why a linear sweep is not enough.** One linear `llvm-objdump -d` sweep over
the whole image loses the boundary behind Borland's zero padding: after the
`ret` at `0x45f56e` it decodes `00 55 8b` (`add [ebp-0x75], dl`) and swallows
the `push ebp` of the logic frame `0x45f570`; jump tables and the RTTI
records that Borland keeps in CODE throw it out of step the same way.

**Rule** (`probe_instruction_inventory.py`):

1. The linear sweep runs as before: 23,995 entries.
2. The pinned llvm-objdump decodes the CODE bytes again, wrapped in an ELF
   image whose function symbols are the known code starts, so its sweep
   restarts at every start. Roots: the PE entry point, the export table
   targets, every base-relocated dword whose target begins with `55 8b ec`
   (push ebp; mov ebp, esp: vtable slots, callbacks) and every direct
   `call rel32` target of the linear sweep. From each reached instruction
   the descent follows the fall-through, conditional and direct jump
   targets, direct call targets and, for `jmp *T(,reg,4)`, the table
   entries (consecutive base-relocated dwords at T pointing into CODE).
   `ret`, `int3`, `hlt` and the other indirect jumps end a path. A target
   that is not a decoded start yet becomes a symbol of the next pass, until
   none is missing. The run aborts if two reached instructions overlap, the
   decoder rejects a reached byte, a reached instruction is an unknown
   control transfer, a path leaves CODE, the final pass decodes a reached
   instruction differently or a jump table overlaps reached code.
3. Inventory = linear entries + reached one-byte `PUSH r32` (not ESP) −
   linear entries proven not to be boundaries (inside a reached
   instruction or inside a jump table). Linear entries in bytes the descent
   never reaches are kept, unconfirmed.

The roots are not taken on trust: a wrong root inside real code shows up as
an overlap and stops the generator. A wrong root in bytes that nothing else
reaches cannot be detected that way, which is why pointer targets count only
with the `55 8b ec` prologue (relocated pointers also lead to the Borland
RTTI type descriptors kept in CODE, e.g. `0x402c01`, `0x406b17`). None of
the 2,069 linear call targets lands inside a reached instruction. Reached
code contains no rejected byte, no x87 instruction and one privileged one
(`in al, dx` at `0x45c63c` in the VGA retrace wait `0x45c638`).

**Result** (hash-checked `VER95/DBSIM.EXE`, LLVM 23.1.2; about 7 s per
generator run):

| | |
|---|---:|
| Decoder passes | 2 |
| Roots: entry / exports / `55 8b ec` pointers / linear call targets | 1 / 3 / 1,036 / 2,069 |
| Reached instructions | 176,549 (529,757 of 613,376 CODE bytes) |
| Jump tables | 52 |
| Overlaps, rejected bytes, paths out of CODE | 0 |
| Linear entries | 23,995 |
| … confirmed by the descent | 21,785 |
| … removed, provably not a boundary | 11 |
| … kept unconfirmed (never reached) | 2,199 |
| Added (reached, missed by the sweep) | 218 |
| **Inventory** | **24,202** |

The 11 removed entries would have corrupted code or a jump table if probed;
all lie in or right behind a jump table where the sweep was out of step:

| Entry | Proof |
|---|---|
| `0x41340f` | inside `mov edx, [edi+0x45]` at `0x41340e` |
| `0x41ec16` | inside the rel32 of `call 0x413e50` at `0x41ec14` |
| `0x43a9da`, `0x460ddd`, `0x47df96` | inside the instructions at `0x43a9d9`, `0x460ddc`, `0x47df95` |
| `0x4053ad` | jump table of `0x40535f` |
| `0x445567`, `0x44556b`, `0x445573`, `0x445577` | jump table of `0x445552` |
| `0x45722c` | jump table of `0x457225` |

Of the 2,199 unconfirmed entries, 1,458 lie in functions nothing refers to
(padding, then `55 8b ec`; no call, jump or relocated pointer), 553 in gaps
that hold a relocated pointer target without `55 8b ec` (static
initializers of the 6-byte startup table such as `0x413ed4`/`0x414c68`,
the pure-virtual stub `0x496748`, this-adjusting thunks, RTTI type
descriptors: the `S` of the type name at `0x402c0d` is the linear entry
`0x402c0f`) and 188 elsewhere. Nothing proves them wrong, so they stay; only
a known function start should be probed.

**Tests.** `//tools/reference-game/proxy:entry_probes_scripts_test`
regenerates the header from the CD image and compares it byte for byte,
checks sample VAs and that the inventory equals linear + added − removed with
a proof for every removal, and runs an own synthetic PE (a jump table with
push-like bytes, an operand that the sweep misreads, zero padding before a
called function and before a case reached only through the table, a function
known only from a DATA pointer, a data string with a DATA pointer to it)
through the rule; a variant with a root inside a reached instruction must be
refused. `entry_probes_reference_test` checks all 24,202 signatures against
the bytes of the executable section.

## Entry kinds

The first table has only one-byte `PUSH r32` rows, the one instruction the
probe engine replays on its own. A function whose first instruction is
something else gets a row of `probe_entry_kinds` when that instruction is one
of the 26 `ENTRY_KINDS` of the script (`EP_*`; the engine checks the same
bytes again in `ep_kind_size` before patching).

**Rule** (`probe_instruction_inventory.py`, step 4 of its docstring):

1. The first table is the rule above alone: 24,202 rows, byte-identical, so
   every probe and map row stays comparable.
2. A second descent adds the *operand pointers* as roots: targets in CODE,
   outside reached code, of base-relocated dwords inside reached
   `push imm32`, `mov r32, imm32` or `mov r/m32, imm32` instructions (code
   addresses taken as data: the pool and stage callbacks, e.g. `0x405ea4`,
   `0x409a1c`, `0x46b188`). It must reproduce every instruction of the
   first descent and keep its jump tables clear of reached code: 20 roots,
   118 more reached instructions. Other relocated operands (jump tables,
   byte index tables, `jmp [table]`) are not taken.
3. Known function starts = the reached roots (entry point, exports,
   `55 8b ec` pointer targets, linear call targets, operand pointers), the
   targets of every reached `call rel32` and every reached instruction
   start that a relocated dword outside the jump tables points to: **3,010
   starts**.
4. A start that begins with a one-byte `PUSH r32` of the first table is
   probed as `EP_PUSH` (2,683). Every other start whose decoded first
   instruction is an `ENTRY_KINDS` member gets a row of the second table
   (326 rows); the proxy accepts its VA in `[probes] entries` like a push row.

**Engine.** `entry_probe_engine.h` patches only the first byte (INT3), counts,
and then emulates the site's instruction from a copy taken at install time
(the byte pattern is checked again before patching, and the whole instruction
must lie outside every hook prologue and coverage site): no single-step
window and no out-of-line execution.

| Kind | Instruction | Starts | Emulated effect |
|---|---|---:|---|
| `EP_PUSH` | `push r32` (not ESP) | 2,683 (first table) | the push |
| `EP_ENTER` | `enter imm16, 0` | 90 | push EBP; EBP = ESP; ESP −= imm16 |
| `EP_JMP_ABS` | `jmp [abs32]` (import thunk) | 132 | EIP = the slot's current value |
| `EP_MOV_EAX_ABS`, `EP_MOV_AL_ABS`, `EP_MOV_AX_ABS`, `EP_MOV_R32_ABS`, `EP_MOVSX_R32_M8`, `EP_MOVSX_R32_M16` | loads from `[abs32]` | 38, 1, 3, 1, 2, 1 | register (part) from memory, sign extension for movsx |
| `EP_MOV_R32_IMM`, `EP_MOV_R16_IMM`, `EP_MOV_R32_R32` | register moves | 4, 2, 2 | no flags |
| `EP_MOV_M8_IMM`, `EP_MOV_M16_IMM` | stores to `[abs32]` | 4, 1 | store |
| `EP_CALL_REL32`, `EP_JMP_REL32`, `EP_RET` | direct transfers | 9, 2, 5 | push return address / EIP / pop EIP |
| `EP_PUSH_IMM8`, `EP_PUSH_IMM32` | pushes of immediates | 7, 2 | sign-extended imm8 |
| `EP_CMP_M32_IMM8`, `EP_CMP_M16_IMM8`, `EP_CMP_M8_IMM8` | compares with `[abs32]` | 5, 4, 3 | CF PF AF ZF SF OF of the subtraction |
| `EP_ADD_ESP_IMM8`, `EP_ADD_M8_IMM8`, `EP_INC_M32`, `EP_DEC_M32` | additions | 2, 1, 1, 1 | result and flags (INC/DEC keep CF) |
| `EP_XOR_R32_SELF` | `xor r, r` | 3 | r = 0; ZF PF 1, CF OF SF AF 0 |

If a memory operand is not accessible when the probe fires (never the case
for the image's own data), the site is restored and the original
instruction resumes at the site and faults by itself. All arithmetic flags
are computed, including AF, which `xor` leaves architecturally undefined;
the processor clears it, and the kinds fixture checks that on the host.

**Unprobed.** One start, `0x467ad8` (`int3; ret`, a called breakpoint stub):
its original byte is the INT3 itself. Starts outside the synchronized reach
(code that nothing reachable refers to, e.g. callbacks registered from
unreached code such as `0x401e5c` from `0x401ef8`) are no known starts; a
linear-only `PUSH r32` entry of the first table can still be probed by VA.

**Tests.** `entry_probes_kinds_test` assembles each kind in own code between
a register/flags setup and a capture stub, runs it natively and with a probe
on it, and requires equal registers, EFLAGS, top of stack and data block and
exactly one count (every push register, every load/move register, 16 memory
values × 8 immediates × 5 flag patterns for the arithmetic kinds), plus the
refusals (wrong kind for the bytes, `enter` with a nesting level, ESP
destinations, a register operand instead of `[abs32]`, an instruction
overlapping an exclusion or another site or running past the image) and both
inaccessible-operand paths. `entry_probes_reference_test` checks all 326
second-table rows against the executable section;
`entry_probes_runtime_test` parses a second-table entry;
`probe_instruction_inventory_unit_test` has an own image with an `enter`
function, an import thunk, a getter, an operand-pointer callback whose `push`
the linear sweep misses (an `EP_PUSH` row) and a called `int3` stub, and
checks the real counts (3,010 starts / 20 operand roots / 326 kind rows, the
single unprobed `0x467ad8`).

## Map build

`dbsim_map.py` (standard library plus the pinned llvm-objdump; no original
bytes are written):

- **`plan <DBSIM.EXE> <out-dir> [--batch-size 512] [--exclude-registry
  hook_registry.h]`** writes `plan.json` and `batch-<n>.txt` (comma-separated
  VAs for `run-live-coverage.ps1 -ProbeEntries`). Batch `t` holds the 150
  starts that could run on another thread (import thunks, operand-pointer
  callbacks), so that such a hit invalidates only that batch; the others
  hold the remaining probeable starts in address order, 512 each (`00`..`05`,
  the last 299). With `--exclude-registry` (a run with hooks replaced) the
  starts that a patched hook prologue covers are left out: the hook's own
  start and any start in the `HOOK_SPAN` = 16 bytes after it (the proxy
  refuses a probe inside a patched prologue; every relocated prologue is
  shorter).
- **`build <DBSIM.EXE> <map.json> --registry hook_registry.h --batches <dir>…
  [--suites a,b,…] [--baseline <map.json>]`** reads `<dir>/<suite>/probes.json`
  of every valid batch (valid: `probes-summary.md` present and not "Probes
  invalid", every report complete, error 0, no other thread) and writes
  `map.json`: `rows` (live original functions, with counts per suite,
  `max`, `hot` = saturated somewhere, the static summary, `callers`,
  `callees_live`, `callees_replaced`, `callees_unprobed`, subtree totals and,
  where `probes-load.json` exists, `load`), `hooks` (the registered hooks' own
  counts when the run kept every hook original), `unprobed` (start → kind or
  reason) and `summary`. With `--baseline <map.json>` of a run with every hook
  original, `superseded` lists the baseline rows that this run probed and
  that no longer run, and `summary.load_only` counts the rows that ran in no
  frame of any suite.
- The static walk follows jump tables (switch functions have more
  instructions than a walk that does not: `0x42a9c0` 155 → 2,480).
  `hook_candidates.py` and `function_index.py` read the schema.

## Map with every hook original

`dbsim-map-20261012` (536 hooks registered, every hook
original), early arming, saturate N = 1000, 40 suites, seven batches on five
measurement clones. Every batch valid, 40 of 40 suites identical to their
goldens, no hit from another thread.

| | |
|---|---:|
| Known function starts | 3,010 |
| Probed | 3,009 (only `0x467ad8` not probeable) |
| Run in at least one suite | 2,065 |
| … at least 1000 times in one suite | 995 |
| Never run in any suite | 944 |
| Hooks among the probed starts / live | 536 / 517 |
| Rows (live originals, not hooks) | 1,548 (97,882 instructions) |

- 198 of the rows are starts of the 26 non-push kinds (7,228 instructions:
  71 import thunks, 57 `enter` prologues, 22 `mov eax,[abs]` getters, the
  rest one to six each).
- The window procedure `0x466034` and the message helpers `0x462398`,
  `0x4809f8` are not saturated (63 to 864 calls): the game window is hidden
  in the oracle runs, so far fewer window messages arrive.
- Measured main-loop calls: the logic frame `0x45f570` and the presentation
  `0x45fba4` run once per frame (a60 saturated, c3_10 937/936 frames);
  `0x4034f4` (the `VEHICLE` slot) is saturated in a60 and c3_10. `0x492e88` (the RNG
  initializer) never runs after the arming; `0x42789c` runs once, in the
  load step ([rng.md](../reverse-engineering/rng.md)).

## Map with all hooks replaced

`dbsim-map-20261020`: prepared proxy from the clean tree,
**all 782 registered hooks replaced**, early arming, saturate N = 1000, 40
suites with the golden check, six batches (`batch-t`, `batch-00`..`batch-04`)
on five clones. Probed: the 2,223 known starts that are neither a hook start
nor inside a hook's patched span; the other 787 are not probed. Every batch
valid, 40 of 40 suites identical to their goldens with the same exit code in
every batch, no hit from another thread. About 40 minutes per batch with five
in parallel.

| | Every hook original | All hooks replaced |
|---|---:|---:|
| Known function starts | 3,010 | 3,010 |
| Hooks registered | 536 | 782 |
| Probed | 3,009 | 2,223 |
| Rows (live originals) | 1,548 | 1,286 (82,559 instructions) |
| … at least 1000 times in one suite | 995 (hooks included) | 526 |
| Never run (probed) | 944 | 937 |
| Superseded (baseline rows probed again, now never run) | — | 41 |

- **Against the baseline.** Of the 1,548 baseline rows, 221 are not probed
  now (hook starts or inside a hook span); of the 1,327 probed again, 1,286
  run and 41 never run. **No function runs now that was not a baseline
  row**: the replaced hooks open no original path that the original game did
  not take.
- **Superseded (41).** The original callees that own code computes itself:
  the formation core's `0x411d64`, `0x4205cc`, `0x423044`; the weapons sweep
  leaves `0x427d98`/`0x427da0`; the sfx gate `0x462784` (inlined);
  `0x443908`, `0x466a88`, `0x466ae0`, `0x466b20`, `0x467b28`, `0x471a50`;
  many renderer and math helpers between `0x47a6b4` and `0x47d2c4`, some of
  them only pointer-referenced (`0x47aeb0`, `0x47aecc`, `0x47c7bc`,
  `0x47c7d8`, `0x47c82c`); `0x47dbca`, `0x47df04`, `0x47df9c` (an `enter`
  start), `0x481f3d`, `0x482f38`, `0x482f80`, `0x492ac8` … `0x492b52`; the
  jump thunks `0x4967cc` and `0x496826`; the `ret` starts `0x425344` and
  `0x426514`. The regression's hook counters still show calls for several of
  them (`hk_*_call_<va>`): the hook adapters count each logical call through
  an observer even when the core computes the result inline (comment in
  `formation_hooks.h`; `sweep_leaf` in `weapons.c` for `0x427d98`), so those
  counters are logical calls, not guest calls.
- **Lower counts (27 rows).** 27 rows that run in both maps have a lower
  unsaturated count in some suite: `0x407048` (22 against 227),
  `0x47a8a4`/`0x47a904` (21 against 282), `0x474324`, `0x495e64`; the dialog
  and message-pump functions (`0x454acc`, `0x454e2c`, `0x455080`,
  `0x45719c`, `0x457480`, `0x45cfc8`, `0x464260`, `0x4809f8`, …) by one or
  two calls; the sound driver functions `0x464038`, `0x464118`, `0x464618`,
  `0x473898`, `0x4739ac` by one. Not traced (open). The suites are identical
  to their goldens, so the hypotheses are callers that are own code now and
  reach the function less often or not at all (the first group), and message
  timing outside the compared state (the others).
- **Load split** (1,286 rows; instructions of the row functions; area from
  the function index):

  | Area | In load and frames | Frames only | Load only (exact) | Saturated in the load window |
  |---|---:|---:|---:|---:|
  | all | 548 / 38,066 | 302 / 17,381 | 372 / 22,218 | 64 / 4,894 |
  | HUD | 170 / 16,025 | 138 / 9,009 | 128 / 14,035 | 0 |
  | R3D | 196 / 16,063 | 32 / 1,768 | 92 / 3,302 | 43 / 4,336 |
  | SIM | 6 / 393 | 16 / 803 | 41 / 904 | 3 / 182 |
  | AUD | 19 / 1,089 | 38 / 1,009 | 22 / 946 | 0 |
  | DLG | 3 / 26 | 44 / 3,728 | 1 / 134 | 0 |
  | FILE | 24 / 654 | 1 / 28 | 13 / 305 | 3 / 105 |
  | INP | 21 / 831 | 4 / 61 | 29 / 1,207 | 0 |
  | MATH | 25 / 770 | 3 / 45 | 4 / 115 | 3 / 34 |
  | PRES | 15 / 1,152 | 2 / 55 | 8 / 387 | 0 |
  | RTL | 48 / 169 | 8 / 8 | 13 / 13 | 10 / 178 |
  | SHELL | 10 / 698 | 12 / 723 | 9 / 392 | 0 |
  | (none) | 11 / 196 | 4 / 144 | 12 / 478 | 2 / 59 |

  Rows / instructions. The window includes the first loop pass, so "frames
  only" means "not in the first pass either": message, dialog and event code.
  About a quarter of the remaining original code (372 rows, 22,218 of 82,559
  instructions) runs only in the load window, most of it HUD code (14,035
  instructions; presumably the cockpit construction, not checked per
  function).
- **Derived documents** are regenerated from this map by
  `regenerate_function_docs.ps1`: [hook_candidates.md](hook_candidates.md),
  [hook_frontier.md](hook_frontier.md), [sim_gap.md](sim_gap.md) (its Live
  column; SIM functions called live by own code: 0) and
  [docs/functions/](../functions/README.md) (superseded rows marked).
  Original callees that the script printed without a note or verdict:
  `0x462af0`, `0x420a8d`, `0x420acb`, `0x40acac`, `0x44d24c`, `0x44f03c`.

## Main loop frame structure

The main loop (`0x45f458`..`0x45f4b3`) calls, per frame (static reach:
functions reachable by direct calls; indirect calls are not followed):

| Call | Static reach | Role (hypothesis) |
|---|---:|---|
| `0x4678c0` | 3 | limiter (known) |
| `0x45f570` | 733 | game logic: input `0x460870`, actors, AI, weapons, mission |
| `0x4011a0([ebx+2e])` | 17 | per-frame transform/terrain step of one object |
| `0x45fca8` | 262 | world and HUD rendering |
| `0x45fba4` | 74 | presentation |
| `0x401d94(4/6)` | 1 | frame markers |

`0x45f570` also reaches most actor code through indirect calls, which is why
many live functions are only reachable as vtable/callback roots. The actor
vtables (class tree, slot matrix, AI state methods and their readiness) are
in [vtables.md](../reverse-engineering/vtables.md). The rendering and
presentation trees under `0x45fca8` and `0x45fba4` belong to the
presentation part of the work.

## Reproducing a map

Run the batches on measurement clones with a prepared proxy bound to the
source stamp (the tree must not change while the batches run). A map with
every hook original omits `-Hooks`; with all hooks replaced `-Hooks` is the
list `<name>=replaced` of `tools\reference-game\proxy\hook-names.txt`.

```powershell
Set-Location <es2>
$py='<es2-dependencies>\python\python.exe'
$exe='build\extracted\cd\VER95\DBSIM.EXE'
$root='<es2-data>\logs\dbsim-map-<date>'
& $py -B tools\reference-game\scripts\dbsim_map.py plan $exe "$root\plan" --exclude-registry tools\reference-game\proxy\hook_registry.h
Import-Module .\tools\reference-game\setup\Es2Bazel.psm1 -Force
$env:ES2_PREPARED_PROXY=New-Es2PreparedProxy "$root\prepared"
$env:ES2_PARALLEL_CLONES='timing-r111,timing-r111-p1,timing-r111-p2,timing-r111-p3,timing-r111-p4'
# per clone and batch <n> (one runner process per clone):
& .\tools\reference-game\setup\run-live-coverage.ps1 -InstallName <clone> -ProbesOnly -ProbeEntries (Get-Content "$root\plan\batch-<n>.txt" -Raw).Trim() `
    -ProbeMode saturate -SaturateAfter 1000 -Hooks <hooks> -OutputDirectory "$root\batch-<n>"
```

Operational traps:

- Start the runner processes as separate processes, staggered by 20 s;
  `Start-Job` runners started at the same moment hung in `git rev-parse HEAD`
  before any game started. `ES2_PARALLEL_CLONES` must list the clones, or the
  second running game is refused.
- Each suite's raw tick log is 0.1 to 1.6 GB; five parallel batches of 40
  suites fill the drive unless each tick log is deleted once its normalized
  file exists.
- A clone must be restored from its backup before reuse: a diagnosis run
  leaves another suite's `script.dat` behind, and the next batch fails with
  "Oracle check failed; coverage invalid".
- A batch whose `probes-summary.md` starts with "Probes invalid" (or a suite
  with `other_thread`) is rerun alone as `batch-<n>-retry`; if batch `t` fails
  on another thread, split it (thunks / callbacks) and rerun.

Post-processing:

```powershell
& $py -B tools\reference-game\scripts\dbsim_map.py build $exe "$root\map.json" `
    --registry tools\reference-game\proxy\hook_registry.h --baseline <map.json of an every-hook-original run> `
    --batches (Get-ChildItem "$root\batch-*" -Directory).FullName
.\tools\reference-game\scripts\regenerate_function_docs.ps1 -MapJson "$root\map.json"
```

`regenerate_function_docs.ps1` runs `sim_gap.py`, `function_index.py` and the
three `hook_candidates.py` modes ([docs/functions/README.md](../functions/README.md#regenerating)).

## VSHELL counterpart

The same probe engine runs in `VSHELL.EXE` (`proxy/vshell_probes.h`, sites
from `probe_instruction_inventory.py --module vshell`, switched on by
`[vshell_probes]` in the INI of `ES2_LIVE_COVERAGE_CONFIG`). Instrument,
switch and report format: [vshell_map.md](vshell_map.md).
