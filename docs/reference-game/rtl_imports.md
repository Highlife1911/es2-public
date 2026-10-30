# CW3220 imports of DBSIM

Every import that the reference `VER95/DBSIM.EXE` (SHA-256 `8561aa47…c4ad`,
image base `0x400000`, see [reference_version.md](../reference_version.md))
takes from `CW3220.DLL` (CD root, SHA-256 `6e90e1ca…f505`): every call site,
the function that owns it, whether that function is simulation code, what
the Borland runtime linked into DBSIM itself does, and the own code that
replaces each import. The imports are classed pure / state / I/O. The own
runtime consists of the IAT-slot shim in the proxy, the core models
(`core/src/rtl.c`, `rtl_heap.c`, `rtl_io.c`, `rtl_start.c`) and the own
`cw3220.dll` (`rtl/`). Linked from [hooks.md](hooks.md#registry).

Sources of the static facts: `llvm-objdump -d -M intel` of DBSIM and CW3220
(the pinned LLVM-MinGW), the DBSIM map
(`<es2-data>\logs\dbsim-map-20261008`: `map.json` live counts,
`cfg.json` entries), the hook registry and the core modules. **Confirmed
(static)** means read from the instructions or bytes; **confirmed live**
means implied by exact live counts or a live run; **confirmed in vitro**
means the in-vitro harness compared the own code with the original DLL's
functions. Roles are hypotheses unless a cited document confirms them.
Evidence of the inventory (listings, per-site and per-owner tables):
es2-data `logs\d-14-20261010` (list under [Evidence](#evidence)).

## Summary

- **Inventory: 53 CW3220 imports, 555 direct call sites** (all `call`/`jmp`
  rel32 to the 53 jump thunks `0x496754..0x49688c`; no `call [IAT]`) in
  256 owner functions, plus 19 immediate references for the three imports
  that are not called (`__ExceptionHandler`, `_errno`, `__streams`).
  `tools/reference-game/scripts/rtl_imports.py` regenerates the inventory
  and `--check`s the Sites column of [section 2](#2-the-53-imports).
- **Classification: 20 pure, 12 state, 21 I/O.** Pure: memory and string
  functions, `_itoa`, `_atol`, `_sprintf`, `_vsprintf`. State: heap
  (`_calloc`, `_realloc`, `_free`), exit list (`_atexit`), process
  lifecycle and exception handling (`__startup`, `__exit`, `__ErrorExit`,
  `_abort`, `__ExceptionHandler`, `@_CatchCleanup$qv`) and the two data
  imports `_errno`, `__streams`. I/O: file, console and clock.
- **Simulation code calls 12 imports at 85 sites** (49 owners): 20 sites
  in 14 registered hooks, 6 in other frame-time code, 59 in mission load
  and static initialization. At frame time only `_memset`, `_memcpy` and
  `_sprintf` (a debug message) are called; mission load adds `_memmove`
  (RNG seed copy), `_strcat`, `_strlen`, `_strchr`, `_stricmp`, `_itoa` and,
  on a development-only text-grid branch, `_fopen`/`_fscanf`/`_fclose`. All
  twelve are pure apart from those three file calls. The other 470 sites
  are presentation (97), platform (219: stream library, paths, config,
  tapes, audio, input, window), the runtime itself (18) and debug, error or
  unreferenced code (136).
- **The heap is reached only through the game's own `operator new`/`delete`**
  (`0x47749c`, `0x4774f0`, `0x477548`, `0x47758c`), never directly from
  simulation code. A low-memory arena in DBSIM's BSS exists but is used
  only with `-l` or below 12 MB of physical memory; in the oracle runs
  every `new` is a CW3220 `_calloc(1, n)` (confirmed live, section 4.3).
  Simulation allocates during frames (`object_init` alone 146 to 1000
  times per suite).
- **DBSIM never throws a C++ exception.** It imports no throw entry; the
  only catch block is the runtime's own `terminate()`. The ~260 frame
  records of `0x49630c`, the FS:0 chain and the construction counter
  `[[0x6cbee4]]` are bookkeeping that only an unwinding exception would
  read; they live in DBSIM's code and on the main thread's stack, not in
  CW3220, and stay unchanged by the own runtime.
- **The own runtime.** All 53 imports have own code:
  - the IAT-slot shim ([The IAT-slot shim](#the-iat-slot-shim)) with the
    modes `original`, `compare`, `replaced` and, for the heap and I/O,
    `shadow`;
  - the pure imports ([The pure imports as own code](#the-pure-imports-as-own-code));
  - the heap ([shadow](#the-heap-shadow-stage), [replaced](#the-heap-replaced));
  - file and console I/O ([File and console imports](#file-and-console-imports));
  - the lifecycle ([The runtime lifecycle](#the-runtime-lifecycle),
    [The lifecycle group as own code](#the-lifecycle-group-as-own-code));
  - the own `cw3220.dll`, the default runtime of the oracle runs
    ([The own cw3220.dll](#the-own-cw3220dll),
    [The own runtime as the default](#the-own-runtime-as-the-default)).

## The runtime version

- **Version (confirmed).** The game's `CW3220.DLL` (CD root, 176,128 bytes)
  carries the version resource "Borland C++ 5.0", file version 2.0, and the
  string "Borland C++ - Copyright 1995 Borland Intl."; linker version 2.25
  like the executables. `BWCC32.DLL` is version 2.02 and is not statically
  imported by any of the three executables.
- **Older than the release build (confirmed: sizes, exports).** The
  `CW3220.DLL` of the Borland C++ 5.0 release and the copy in the game CD's
  `DEMOS\A10` (229,376 bytes each, different hashes) export 1,299 names; the
  game's DLL exports 875. 424 of the 425 missing names are in the release
  DLL, 407 of them the C++ standard library, the rest debugger and heap
  helpers (`__free_heaps`, `__lockDebuggerData`, …); only the game's DLL
  exports `__heapadd`. **Hypothesis:** the game was built with a pre-release
  of Borland C++ 5.0, so a fact from the release's runtime source holds only
  once checked against the game's DLL.
- **Imports (confirmed).** DBSIM 53, VSHELL 47, ES 28 functions from
  `CW3220.DLL`, 65 distinct. None of the math library, no `rand`, no
  `qsort`, no `malloc`. No `%f`/`%e`/`%g` conversion appears in a DBSIM
  string: DBSIM formats no floating-point value.
- `_findfirst`/`_findnext` (function `0x467cd8`) enumerate the VOL archives
  in file-system order ([vol.md](../formats/vol.md)); `_time` has one call,
  the demo selection ([rng.md](../reverse-engineering/rng.md#_time-is-a-separate-choice-source)).

## 1. Method and limits

1. **Imports and thunks (confirmed static).** The import directory names
   `cw3220.DLL` with 53 symbols, IAT `0x6cc2d4..0x6cc3a4`. Each slot has
   exactly one `FF 25` jump thunk; the 134 thunks of all DLLs fill
   `0x496754..0x496a72`.
2. **Sites.** A byte scan of CODE for `E8`/`E9` rel32 to a thunk (555
   sites; `FF 15` never occurs) and for dwords equal to a thunk or slot
   address (19 immediates: `_errno` 4, `__streams` 14,
   `__ExceptionHandler` 1). The scan agrees with the llvm-objdump listing.
3. **Owners.** The nearest function entry at or below the site. Entries:
   the DBSIM map's `cfg.json` (2,939), every direct `call` target, every
   `push ebp; mov ebp, esp`, code addresses used as `push`/`mov`
   immediates, and the runtime pieces of section 4.1. The listing
   misaligns 177 prologues after `00` padding (`00 55 8b` decodes as
   `add [ebp-0x75], dl`); the script's byte scan finds them. Owners of 6 of
   the 555 sites differ between the listing-based walk and the script's
   byte scan (false entries inside data); the tables use the walk.
4. **Simulation or not.** For each owner a walk over its direct callers up
   to: the logic frame `0x45f570`, `camera_update` `0x4011a0`, the
   rendering `0x45fca8` and presentation `0x45fba4` frames, the mission
   load `0x461608` and end `0x461ff0`, the startup chain (`0x401000`,
   WinMain `0x46538c`, `0x45f160`, the option parser `0x45e758`), the
   window procedure `0x466034`, registered hooks, stage callbacks
   (`0x401d64`: stage 2 = mission load, 5 = frame, 6 = dmp), the 65 init
   and 26 exit records, and pointer roots resolved through the Borland RTTI
   descriptor at `vtable−12` (196 vtables, e.g. `MECH`, `EnergyWeaponGauge`,
   `VolRStream`). Actor, weapon, effect and animation classes count as
   simulation (slot `+0x00` draw and `+0x08` destructor excepted); gauges,
   panels, HUD, MFD, GL, TS renderer, controls and alerts as presentation;
   streams, joystick and message ports as platform/presentation. 173 of
   the 256 owners got their verdict from a document or a reading of the
   code (the stream, path and tape tables of
   [file_io.md](../reverse-engineering/file_io.md), the HUD, cockpit and
   display documents, the allocator and runtime of section 4); the walk
   decided the other 83 (list with reasons in `owner_categories.md` of the
   evidence).
5. **Categories.** **S** simulation: *hook* (owner is a registered hook),
   *frame* (reached from the logic frame, actor slots or hooks), *load*
   (mission load, sim object constructors, static init); **A** the game's
   allocator layer; **R** runtime code linked into DBSIM; **P**
   presentation; **X** platform (stream library and paths of
   [file_io.md](../reverse-engineering/file_io.md), configuration, tapes,
   audio, input, window); **E** error and assert reporting, object dumps,
   code without a static reference.
6. **Map counts** are the map's entry probes per suite
   (acceptance-60s / c1_01-success / c1_01-collision / c3_09-loadout-b /
   c4_08-razor-plasma / c3_10-loadout-hercs, saturated at 1000) of the
   owner, not of the call site. The proxy arms at the first DirectDraw or
   MCI call inside the mission load ([rng.md](../reverse-engineering/rng.md)
   Q1), so world load, actor construction and static init read 0 although
   they run. Hooks replaced when the map was made have no row ("hook").
   Exact counts after arming: section 4.3.

## 2. The 53 imports

S = simulation (hook / frame / load), A = allocator layer, R = runtime in
DBSIM, P = presentation, X = platform, E = error/debug/unreferenced (see
section 1). "Sites" counts direct call sites; `imm N` marks an import that
is only referenced as an immediate (its users are named in the notes; the
category columns and the total count call sites only). The site set equals
the `call`/`jmp` instructions to the thunks in the llvm-objdump listing.
`rtl_imports.py --check` compares the Sites column with the binary (it
reads the first table of this document with the columns Import and
Sites). The own code of every import: section 5.

| Import | Class | Sites | S hook | S frame | S load | A | R | P | X | E | Notes (simulation use, contract) |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `_memset` | pure | 110 | 14 | 4 | 36 | 4 | | 29 | 19 | 4 | hot at frame time (6 hooks with ≥1000 entries in most suites); fill bytes 0, 1 and 0xff; returns EAX = dest, ECX = 0, EDX = n |
| `_memcpy` | pure | 29 | 6 | 1 | 5 | 2 | | 1 | 14 | | snapshot/rollback (native in core), `0x490b64` (object_init's sub-object); EAX = dest, ECX = 0, EDX = n, forward copy |
| `_memmove` | pure | 3 | | | 1 | | | | 2 | | RNG stream init `0x492e88` copies the 0x70-byte seed table (`rng.c` uses memmove) |
| `_memcmp` | pure | 3 | | | | | | 1 | 2 | | joystick, dmp compare (dead), HUD |
| `_strcat` | pure | 55 | | | 5 | | | 15 | 30 | 5 | resource names (stage-2 callbacks), mission load; paths |
| `_strlen` | pure | 16 | | | 2 | | | 9 | 2 | 3 | world load, string table parser |
| `_strncpy` | pure | 16 | | | | | | 7 | 6 | 3 | — |
| `_strchr` | pure | 5 | | | 1 | | | | 4 | | terrain loader `0x46c754`: extension of the grid name |
| `_stricmp` | pure | 10 | | | 1 | | | | 9 | | terrain loader: `.dba` test decides binary or text grid; compares `toupper` values (CW3220 ctype table) |
| `_strncat` | pure | 4 | | | | | | 4 | | | — |
| `_strupr` | pure | 2 | | | | | | 2 | | | — |
| `_strncmp` | pure | 2 | | | | | | | 2 | | — |
| `_strstr` | pure | 2 | | | | | | | 2 | | — |
| `_strlwr` | pure | 1 | | | | | | | 1 | | — |
| `_strnicmp` | pure | 1 | | | | | | | | 1 | — |
| `_strrchr` | pure | 1 | | | | | | | 1 | | — |
| `_itoa` | pure | 27 | | | 4 | | | 16 | 5 | 2 | resource names (radix 10) at mission load |
| `_atol` | pure | 9 | | | | | | | 9 | | options, `sound.cfg` |
| `_sprintf` | pure | 16 | | 1 | | | | 11 | 1 | 3 | pool check `0x471c18`: debug text into a local buffer; every format in DBSIM is `%d`/`%Nd`/`%ld`/`%s`/`%08x` (no floating point) |
| `_vsprintf` | pure | 2 | | | | | | | 1 | 1 | SFX cache sample names (`0x4649b8` → static buffer `0x4d2ef0`), debug monitor |
| `_calloc` | state | 1 | | | | 1 | | | | | only from `0x47747c` (`calloc(1, n)` of `new`/`new[]`) |
| `_free` | state | 6 | | | | 2 | | 2 | 2 | | `delete`/`delete[]`, stream buffers, GL bitmaps |
| `_realloc` | state | 2 | | | | | | | 2 | | resource-to-memory loader, stream buffer |
| `_atexit` | state | 3 | | | | | | | 3 | | input and graphics device constructors; the proxy registers one more |
| `__startup` | state | 1 | | | | | 1 | | | | c0w32 entry `0x401000` jumps to it with the module table `0x49702c` |
| `__exit` | state | 1 | | | | | | | | 1 | fatal error path `0x479c78` (quick exit) |
| `__ErrorExit` | state | 3 | | | | | 3 | | | | vector new/delete "Illegal mode", pure-virtual trap |
| `_abort` | state | 5 | | | | | 3 | | | 2 | runtime terminate/unexpected, assert log failure |
| `@_CatchCleanup$qv` | state | 1 | | | | | 1 | | | | only in the runtime's `terminate()` `0x496377` |
| `__ExceptionHandler` | state | imm 1 | | | | | | | | | thunk address `0x4967f0` stored as handler in every frame record by `0x49630c` (R) |
| `_errno` | state | imm 4 | | | | | | | | | stream library (X): read through the thunk bytes (section 4.5) |
| `__streams` | state | imm 14 | | | | | | | | | object dumps (E): `FILE` array taken at the thunk (section 4.5) |
| `_fclose` | I/O | 20 | | | 1 | | | | 19 | | text grid (development branch); own code, modes shadow/replaced |
| `_fopen` | I/O | 16 | | | 1 | | | | 15 | | text grid (development branch); own code, modes shadow/replaced |
| `_fscanf` | I/O | 4 | | | 2 | | | | 2 | | text grid `%d %d %d %d`, `%d`; own code, modes shadow/replaced |
| `_lseek` | I/O | 19 | | | | | | | 19 | | stream library; own code, modes shadow/replaced |
| `_fread` | I/O | 14 | | | | | | | 14 | | tapes, prefs, dmp; own code, modes shadow/replaced |
| `_fwrite` | I/O | 11 | | | | | | | 11 | | tapes, dmp; own code, modes shadow/replaced |
| `_open` | I/O | 5 | | | | | | | 4 | 1 | stream library, `assert.log`; own code, modes shadow/replaced |
| `_close` | I/O | 5 | | | | | | | 4 | 1 | stream library; own code, modes shadow/replaced |
| `_write` | I/O | 5 | | | | | | | 2 | 3 | stream library; own code, modes shadow/replaced |
| `_read` | I/O | 3 | | | | | | | 3 | | stream library; own code, modes shadow/replaced |
| `_findfirst` | I/O | 2 | | | | | | | 2 | | VOL registration; own code, modes shadow/replaced |
| `_findnext` | I/O | 2 | | | | | | | 2 | | VOL registration; own code, modes shadow/replaced |
| `_fseek` | I/O | 1 | | | | | | | 1 | | tape header; own code, modes shadow/replaced |
| `_ftell` | I/O | 1 | | | | | | | 1 | | tape header; own code, modes shadow/replaced |
| `_rewind` | I/O | 1 | | | | | | | 1 | | tape header; own code, modes shadow/replaced |
| `_filelength` | I/O | 1 | | | | | | | 1 | | stream library; own code, modes shadow/replaced |
| `_flushall` | I/O | 1 | | | | | 1 | | | | runtime `terminate()`; own code, modes shadow/replaced |
| `_printf` | I/O | 51 | | | | | | | | 51 | object dumps (never live); own code, mode replaced (stub) |
| `__fputc` | I/O | 54 | | | | | | | | 54 | object dumps (never live); own code, mode replaced (stub) |
| `_fputs` | I/O | 1 | | | | | | | | 1 | allocation failure in the assert notifier; own code, mode replaced (stub) |
| `_time` | I/O | 1 | | | | | | | 1 | | `-D` demo pick `0x45ceb8`: `time() % count` over `tapes\demolist.str`; own code, modes shadow/replaced |
| **total** | | 555 | 20 | 6 | 59 | 9 | 9 | 97 | 219 | 136 | |

`_time` is the only wall-clock read of CW3220 in DBSIM and runs only with
`-D`
([file_io.md](../reverse-engineering/file_io.md#dbsim-recording-and-playback));
no simulation path reads it.

## 3. Calls from simulation code

### 3.1 Inside registered hooks (14 hooks, 20 sites)

"Own code" says how the core module handles the call: *bridge* = the core
calls the CW3220 thunk through the guest bridge, *native* = the core writes
the same bytes and register results itself. The bridged calls go through
the shim; the own runtime serves them
([The hooks that call `_memset` through the guest bridge](#the-hooks-that-call-_memset-through-the-guest-bridge)).

| Hook | VA | Calls (site, arguments) | Map counts | Own code |
| --- | --- | --- | --- | --- |
| `object_init` | `0x402188` | `_memset` `0x402258` (O+0x38, 0, 8) | 146/796/183/1000/1000/432 | bridge (`effects.c`) |
| `animation_step` | `0x40250c` | `_memset` `0x402523` (f−0x2c, 0, 32), `0x4025e6` (second argument, 0, 32) | hook | native (`animation.c`: host-local start matrix, manager reset) |
| `actor_snapshot` | `0x402628` | `_memcpy` `0x4027b7`, `0x4027d4`, `0x4027ec` (into `0x4a9024`/`0x4a9042`/`0x4a91aa`) | hook | native (`runtime_copy`: dwords then bytes) |
| `actor_rollback` | `0x4027fc` | `_memcpy` `0x402992`, `0x4029af`, `0x4029c7` (back) | hook | native |
| `base_aim_node` | `0x403548` | `_memset` `0x403569` (static node `0x4a95cc`, 0, 32) | 1000 ×6 | bridge (`actor_virtuals.c`) |
| `debris_burst` | `0x408530` | `_memset` `0x408542` (f−0x20, 0, 32) | 23/59/5/503/81/47 | bridge |
| `projectile_create` | `0x40a9c4` | `_memset` `0x40aa6b` (p, 0, 8) | 9/6/6/138/9/0 | bridge (`lc_base.c`) |
| `weapon_step` | `0x40ef94` | `_memset` `0x40efbd` (W+0x3b, 0, 8) | hook | native (`weapons.c`, ECX = 0) |
| `weapon_tally` | `0x40fbdc` | `_memset` `0x40fbfe` (out, 0, 10) | 1000 ×6 | bridge (`vehicle_update.c`) |
| `group_fire` | `0x4107e4` | `_memset` `0x410853` (frame f−0x28, 0, 20) | 1000 ×6 | native (`weapon_helpers.c`) |
| `script_start` | `0x413e50` | `_memset` `0x413e8c` (S+0x35, 0, 16), `0x413ea2` (S+0xd, 0, 40) | no probe | bridge (`ai_order.c`) |
| `player_input` | `0x460870` | `_memset` `0x460d05` (frame f−0x8c, 0, 21) | 1000/1000/1000/1000/1000/937 | bridge (`input_dispatch.c`) |
| `node_matrices` | `0x478c64` | `_memset` `0x478d27` (flag array, 0, count) | 1000 ×6 | native (`animation.c`, returns EDX:EAX = count:flags) |
| `set_length` | `0x4927f0` | `_memset` `0x492851` (V, 0, 12) | 904/1000/1000/1000/304/42 | bridge (`collision.c`; the hook returns memset's EDX) |

`control_record` (`0x45a810`, a platform hook) calls `_memset`, `_fopen`,
`_fread`, `_fwrite` and `_fclose` through the bridge (tape playback and
recording, [file_io.md](../reverse-engineering/file_io.md#frame)); it
is counted under X.

### 3.2 Other frame-time simulation code (5 owners, 6 sites)

| Owner | Role | Calls | Map counts |
| --- | --- | --- | --- |
| `0x40b43c` | `BULLETS` constructor (turret, weapon slots `+0x28`) | `_memset` `0x40b52a` (B+0x42, 0, 8) | 4/280/12/1000/70/4 |
| `0x411a74` | slot `+0x30` of `ACTOR`/`FLYER` (and of the building classes through `0x40351c`) | `_memset` `0x411a7f`, `0x411a8f` (two 6-byte records, 0) | 140/356/230/0/0/0 |
| `0x46b48c` | `FIRE` constructor | `_memset` `0x46b4df` (F+0x41, 0, 8) | 1/7/0/27/6/5 |
| `0x490b64` | sub-object constructor under `object_init` | `_memcpy` `0x490bbd` (2·n bytes of shape data) | 147/800/93/1000/649/377 |
| `0x471c18` | pool link check of the pool iterator | `_sprintf` `0x471c56` ("array ptr = %ld, …" into a local buffer; only for a link value below `0x100000`, never read, see [main_loop.md](../reverse-engineering/main_loop.md#pool-iterator-471c70--471c18)) | 1000 ×6 (entry) |

### 3.3 Mission load and static initialization (30 owners, 59 sites)

All read 0 in the map: they run before the proxy arms (or, like the
text-grid branch, not at all). The IAT-slot shim, armed at DBSIM's entry,
sees them (category `sim`).

| Owner | Role | Calls |
| --- | --- | --- |
| `0x402278` | `DRAWABLE`/`MAT_OBJ`/`TS_OBJ` constructor ([vtables.md](../reverse-engineering/vtables.md)) | `_memset` (8) |
| `0x478838`, `0x478e48` | animation manager set-up under the `TS_OBJ` constructors | `_memset` ×3 (32 each) |
| `0x402fc0` | shape data copy (weapon constructor `0x40df30` and two others) | `_memcpy` |
| `0x40d050`, `0x40fb2c` | weapon and actor data from the script load (`0x4175dc`) | `_memcpy` ×2; `_memset` (20) |
| `0x40d2cc` | helper of the `MECH`/`FLYER` constructors | `_memset` ×3 (fill 0, 0, 1) |
| `0x40dd4c`, `0x40df30` | common weapon constructor ([dat.md](../formats/dat.md)) | `_memset` ×6 |
| `0x415bb0`, `0x4215f4` | `MECH` and `FLYER` constructors | `_memset` (0x4e each) |
| `0x420090` | detail-level node map of DTS shapes ([hit_tests.md](../reverse-engineering/hit_tests.md)) | `_memset` (0xff, 0x190) |
| `0x41fdb0`, `0x422d90`, `0x428230` | stage-2 callbacks (pool/table set-up) | `_memset` (0xff ×0x55 at `0x4a9d08`; 0xff ×0x22 at `0x4a9dcc`; 0 ×0x249f0 at `0x4aab00`) |
| `0x422fb4` | world-load table reset | `_memset` (0xff ×0x22 at `0x4a9dcc`) |
| `0x423b34` | order install ([mission.md](../formats/mission.md)) | `_memset` (10) |
| `0x424308` | world load | `_memset` ×10 (mission tables `0x4aa72a..0x4aa9d2`), `_strlen` |
| `0x4253d8` | script/actor load | `_memcpy` ×2 (20) |
| `0x42789c` | terrain load (zone name) | `_itoa` |
| `0x46c754` | terrain grid loader | `_strchr`, `_stricmp` (`.dba`?); text-grid branch: `_fopen`, `_fscanf` ×2, `_fclose` (development format, file_io.md, Mission load) |
| `0x407b54`, `0x46b1a8` | stage-2 set-up of the `LightSource` and `FIRE` pools (resource names, `-l` suffix) | `_strcat` ×4, `_itoa` ×3 |
| `0x461608` | mission load | `_strcat` |
| `0x46a03c` | object list creation for the pool set-ups (`0x405fac`, `0x40ade0`, …) | `_memset` |
| `0x471ad8` | pool construction `(pool, capacity, size)` | `_memset` |
| `0x490e68` | shape initialization (`GridShape`/`ANShape` slots) | `_memset` |
| `0x492c4c` | string table parser | `_strlen` |
| `0x45caf4` | globals constructor, init record 46 of 0–64 (the 47th; [rng.md](../reverse-engineering/rng.md)) | `_memset` (0xc3) |
| `0x492e88` | RNG stream initializer (from `0x45caf4`) | `_memmove` (state, seed table `0x4a6940`, 0x70) |

### 3.4 Through the allocator and the runtime

Simulation code reaches `_calloc`/`_free` only through `new`/`delete`
(section 4.3): by the caller walk alone (no manual review), about 70 of the
~100 callers of `new` (`0x47749c`), most of the 85 of `new[]` (`0x4774f0`)
and ~40 of the 186 of `delete` (`0x477548`) are simulation code. Live at
frame time are e.g. `object_init` (`new(0xc)`, 146–1000 entries) and
`beam_create` (through `0x49667c`); the light module's
`0x407388`/`0x407444` (`new(0x34)`, `new(0x40)` on one branch, ≥1000
entries) also allocates during frames. The runtime pieces of section 4.1
that simulation code calls are `0x49630c` (frame records), `0x49667c`
(array construction; 19 live callers, among them `beam_create`) and
`0x496406` (destruction count before a virtual delete, e.g.
`meteor_update`).

## 4. The runtime inside DBSIM.EXE

### 4.1 Borland runtime pieces linked into DBSIM (confirmed static)

| VA | Role | References | Map counts | CW3220 imports |
| --- | --- | --- | --- | --- |
| `0x401000` | c0w32 entry: zero-fills BSS `0x4a7bfc..0x6cbee7`, saves the two dwords below the main thread's stack base (`fs:[4]`) in `0x497061`/`0x497065`, sets `[0x6cbee4]` ← StackBase−4, lowers ESP below them if needed, `0x496425(0)`, `GetModuleHandleA(0)` → `0x49706a`, then jumps to `__startup(0x49702c)` | entry point | before arming | `__startup` |
| `0x40106e` | exported `__GetExceptDLLinfo` → `0x4964e2` | export table | — | — |
| `0x49630c` | frame record set-up (4.2) | ~260 functions (map: 252, 52 live) | 1000 ×6 | handler field = thunk `0x4967f0` |
| `0x496406` | `(obj, offset)` → `[[(vptr − [vptr−4]) − 0xc] + 0x20]` with vptr = `[obj+offset]`: a dword of the RTTI descriptor of the most-derived class, added to the construction counter before a virtual delete (`pools.c`; hypothesis: its destructible-object count) | 33 (map 32; before virtual deletes) | 315/1000/288/1000/1000/874 | — |
| `0x49667c` | array construction: allocates through `new[]` when no buffer is given, runs the element constructor per element, saves/restores the construction counter (flag `0x200`) | 85 (map 69) | 1000 ×6 | `__ErrorExit` ("Illegal mode in _vector_new_") |
| `0x49656c` | array destruction loop; calls `delete[]` with flag 8 | 6 | no row | `__ErrorExit` ("Illegal mode in _vector_delete_") |
| `0x496630` | array delete wrapper (adjusts the counter at `fs:[4]−4`) | `0x420560` (no static reference) | — | — |
| `0x496748` | pure-virtual trap: `__ErrorExit("Pure virtual function called")` | abstract vtable slots | — | `__ErrorExit` |
| `0x496425` | exception init: StackBase−8 ← context block `0x4a7884`, StackBase−4 ← 0 | `0x401000` | — | — |
| `0x4964e2` | `__GetExceptDLLinfo` body: `0x4a78a0` ← context+0x24; returns magic `0x82727349` and `0x4a7884` | `0x40106e`, `0x496425` | — | — |
| `0x496377` | `terminate()`: `_flushall`, calls the handler at context+8 inside a catch-all record (context word 8), `@_CatchCleanup$qv`, `_abort` | via `0x4963c7` | — | `_flushall`, `@_CatchCleanup$qv`, `_abort` |
| `0x49634c`, `0x4963c7` | default terminate (→ `_abort`) and unexpected (→ `terminate`) handlers at context+8/+0xc | context block | — | `_abort` |
| `0x496352`, `0x4963cd`, `0x4963f2` | set_terminate, set_unexpected, `unexpected()` | none | — | `_abort` |
| `0x496510`, `0x49653c` | default `matherr`/`matherrl` (type 4 underflow → result 0, handled; type 5 → handled) in the module table `0x49702c+0x1c/+0x20` | module table | — | — |

The module table `0x49702c` passed to `__startup` holds the init records
`0x4a79d8..0x4a7b5d` (65), the exit records `0x4a7b5e..0x4a7bf9` (26),
flags 1, WinMain `0x46538c` and the two `matherr` functions. What each of
the 65 init records writes, the exit records' pairing and the readers of
the written globals:
[own_process.md](own_process.md), static initialisation.

### 4.2 Bookkeeping compiled into game functions (confirmed static)

- **Frame records.** A function with destructible locals calls `0x49630c`
  with EAX = its descriptor table in DATA (`0x49f558` for the logic frame).
  The record lies at EBP + `[EAX+4]` (typically f−0x24 to f−0x34): `+0`
  previous FS:0, `+4` `0x4967f0` (DBSIM's thunk of `__ExceptionHandler`),
  `+8` the descriptor table, `+0xc` the caller's ESP after the call,
  `+0x10..+0x15` zero (the word at `+0x10` is the context word the function
  updates, e.g. `mov word [ebp−0x14], 8`), `+0x16..+0x1b` untouched (stack
  residue), `+0x1c` the construction counter at entry; then FS:0 ←
  record. 332 epilogue stores (`mov fs:[0], reg`) unlink the records.
- **Construction counter.** The dword at the main thread's StackBase−4,
  addressed through `[0x6cbee4]` (~970 instructions: `inc`/`dec`/`add`/`sub`
  after construction and before destruction of objects with destructors).
  `0x496406` supplies the amount before a virtual delete; `0x49667c` and
  `0x496630` adjust it at `fs:[4]−4` directly. The hooks reproduce these
  adjustments (`base_update.c`, `effects.c`, `hit_test.c`, `lc_base.c`, …).
- **Exception context.** StackBase−8 points to the block `0x4a7884`
  (terminate handler at `+8`, unexpected handler at `+0xc`).
- **No throw.** DBSIM imports no throw entry of CW3220; `@_CatchCleanup$qv`
  is called once, by the runtime's `terminate()`. Game code therefore never
  raises a C++ exception and has no catch block. The records, the counter
  and the context only matter when an OS exception unwinds the stack, which
  does not happen in a normal run (the proxy's probes use a vectored
  handler, which runs before the FS:0 chain). What an unwind at the process
  end runs: [The unwind clean-up at the process end](#the-unwind-clean-up-at-the-process-end).
- **Compiler intrinsics.** Inline copies and string scans (139 `rep movsd`,
  49 `rep movsb`, 41 `repne scasb`, 14 `rep stos`) are part of the game
  functions and need no runtime.

### 4.3 The game's allocator over CW3220 (confirmed static, confirmed live)

| VA | Role |
| --- | --- |
| `0x47749c(n)` | `operator new`: if n < `[0x6bb304]` and the arena's largest free block (`0x476edc`) is big enough, arena block `0x476f0c(0x6bb2f8, n, 1)` + `_memset(p, 0, n)`; otherwise `0x47747c(n)` |
| `0x4774f0(n)` | `operator new[]`: the same with n + 8 |
| `0x47747c(n)` | `p = _calloc(1, n)`; when p ≠ 0, `[0x6bb30c] += [p−4]` (CW3220's block header) |
| `0x477548(p)`, `0x47758c(p)` | `delete`, `delete[]`: an arena block (`0x476e2c`: inside the arena and tag `KLBA` at p−8) goes to `0x47700c`; otherwise, for p ≠ 0, `[0x6bb30c] −= [p−4]` and `_free(p)` |
| `0x476df4(heap, size, base)` | arena set-up, called once at `0x45f41c` with (`0x6bb2f8`, `0xb0000`, `0x604f04`) — a 704 KiB block inside DBSIM's own BSS — only when the word `0x4d2680` is set: option `-l` (`0x45ed18`) or `GlobalMemoryStatus` total physical memory below `0xbb8000` (`0x45f3e4`; below `0x73a000` the start fails) |

- Without the arena `[0x6bb304]` stays 0 (zero-filled BSS), so **every
  `new` and `new[]` is a CW3220 `_calloc(1, n)`** and every `delete` a
  `_free`.
- Memory from `new` is zero-filled; game constructors rely on it.
- `0x6bb30c` sums CW3220 header words (block size | 1) of live blocks; the
  only other users are the resource loaders `0x474bbc`/`0x474be4`, which
  save it before a load and reset it to the saved value plus the final
  buffer size after `_realloc`. Nothing branches on it.
- Heap pointers are stored in game objects and pools; no compared tick
  field holds one, and heap addresses differ between runs by a constant
  base offset with identical goldens ([oracle_regression.md](oracle_regression.md)).
  A different relative layout does not change the oracle's outputs either
  (confirmed live with the own allocator: [The heap, replaced](#the-heap-replaced)).

**Exact counts after arming (confirmed live).** Count-mode probes only,
every hook original, six suites on `timing-r111`, all tick logs identical to
their goldens. Evidence: es2-data `logs\d-14-alloc-probes-20261010\`.
Counts a60/c1s/c1c/c3_09/c4_08/c3_10:

| Entry | Counts |
|---|---|
| `0x47747c` | 202 / 1,282 / 247 / 8,558 / 1,309 / 597 |
| `0x47749c` | 156 / 897 / 100 / 4,724 / 667 / 395 |
| `0x4774f0` | 46 / 385 / 147 / 3,834 / 642 / 202 |
| `0x477548` | 157 / 864 / 104 / 4,725 / 618 / 403 |
| `0x47758c` | 153 / 834 / 192 / 6,697 / 987 / 450 |
| `0x476f0c`, `0x47700c` (arena) | 0 in all six |
| `0x474bbc`, `0x474be4`, `0x47c2c4`, `0x47c6d0`, `0x48a1d4` | 0 in all six |
| `0x49630c` | 151,792 / 1,024,125 / 215,528 / 2,134,161 / 499,885 / 68,863 |
| `0x49667c` | 30,871 / 132,966 / 45,079 / 201,220 / 43,212 / 23,605 |
| `0x496406` | 315 / 1,682 / 288 / 11,801 / 1,650 / 874 |
| `0x49656c` | 0 in all six |
| `0x490b64` | 147 / 800 / 93 / 4,706 / 649 / 377 |
| `0x471c18` | 1,006,982 / 4,939,354 / 1,657,032 / 7,814,341 / 809,093 / 598,264 |
| `0x40b43c` | 4 / 280 / 12 / 1,056 / 70 / 4 |
| `0x411a74` | 140 / 356 / 230 / 0 / 0 / 0 |
| `0x46b48c` | 1 / 7 / 0 / 27 / 6 / 5 |

The low-memory arena is never used. With the five other callers of
`0x47747c` (`0x474bbc`, `0x474be4`, `0x47c2c4`, `0x47c6d0`, `0x48a1d4`) at 0,
`0x47747c` = `0x47749c` + `0x4774f0` exactly in all six suites (202 = 156 +
46, …, 8,558 = 4,724 + 3,834): both take the `_calloc` path. The frame-record
helper `0x49630c` runs up to 2.1 million times per suite and the pool tail
`0x471c18` up to 7.8 million, so a shim on those paths must be cheap.

### 4.4 CW3220 contracts an own implementation must reproduce (confirmed static, CW3220 code)

- **Memory functions** (DLL VAs `0x4029e8`, `0x402a0c`, `0x402a58`):
  `memcpy` copies n/4 dwords forward, then n mod 4 bytes forward (an
  overlapping copy behaves like a forward copy); `memmove` copies backward
  (trailing bytes first, then dwords) when dest > src and nothing when
  dest = src; `memset` stores the fill byte replicated in a dword, dwords
  then bytes. All three return EAX = dest, ECX = 0, EDX = n, keep
  EBX/ESI/EDI/EBP and leave the direction flag clear. Hooks depend on this
  (`set_length` returns memset's EDX; `node_matrices` returns
  count:flags). Confirmed in vitro with the remaining register results:
  [Contracts](#contracts-confirmed-in-vitro-against-the-dll).
- **Case functions**: `stricmp`, `strnicmp`, `strupr`, `strlwr` go byte by
  byte through `toupper`/`tolower`, which test the CW3220 ctype table
  (flag 8 → −0x20, flag 4 → +0x20); `stricmp` compares the `toupper`
  values and returns the difference of the first differing pair as signed
  bytes. No `setlocale` is imported, so the table is constant.
- **Heap** (`_malloc` `0x41729c`, `_calloc` `0x416ffc`): `malloc(0)`
  returns NULL; a request n becomes a block of 16 bytes when n < 8, else
  `((n + 3) & ~3) + 8`; the dword at p−4 and the dword at p+size−8 hold
  size | 1 while the block is in use; free blocks form a circular
  doubly-linked list (links at `[p]` and `[p+4]`, the search follows
  `[p+4]`) searched next-fit from a rover; a remainder of at least 16
  bytes is split off behind the allocated block and becomes the rover,
  otherwise the whole block is taken and the rover moves on; the heap
  grows by regions rounded to 64 KiB (`0x417028`). `calloc(c, n)` =
  `malloc(c·n)` + `memset 0`. `free` (`0x417160`) and `realloc`
  (`0x417b8c`) coalesce neighbours; their full rules and the region growth:
  [The CW3220 heap](#the-cw3220-heap-confirmed-in-vitro-against-the-dll).
- **Exit list** (`_atexit` `0x419070`): 32 entries; a full table returns 1.
  `exit` (`0x4191fc`): atexit functions last-in-first-out, then the module
  exit records, then the stream clean-up hooks, then `ExitProcess`.
  `__exit` (`0x419214`) skips the atexit functions and the exit records.
- **Startup** (`__startup` `0x41a6e0`): `__fpreset` (`fninit` and the
  default control word), environment and command line, then the init
  records of every module, then `WinMain` and `exit` with its result.
  Init records run lowest priority first, exit records highest first;
  among equal priorities init records run in table order and exit records
  in **reverse** table order (the selector `0x41a634` takes `<` for init
  and `≥` for exit). All 65 init and 26 exit records of DBSIM have
  priority `0x20`. Step by step: [Start-up](#start-up-confirmed-in-vitro-unless-marked-static).
- **FPU.** DBSIM contains no live x87 code (the map; the ~20 x87-looking
  lines of the listing are data in CODE), so the control word set by
  `__fpreset` is not simulation state.

### 4.5 Two link artefacts: `_errno` and `__streams`

The game code was linked without import declarations for these two data
symbols, so its references resolve to the jump thunks themselves:

- `cmp dword [0x4967ae], 0` (`0x473586`, `0x47abbe`, `0x47af66`,
  `0x47b29a`) reads the thunk bytes `ff 25 10 c3`, which are never zero:
  the stream status setter `0x473580` always stores 1, never 3.
- The 14 object-dump functions load `0x496886` (the `__streams` thunk) as
  the `FILE` array, so their `stdout` is `0x49689e` inside CODE; their
  first `inc dword [edi+0x20]` would write to read-only code. They are
  never live in the suites (map).

An own runtime therefore needs no `errno` or `FILE` data for DBSIM; the
two IAT slots only have to resolve.

## 5. Classes and their own code

| Group | Imports | Simulation use | Own code must reproduce | Own code |
| --- | --- | --- | --- | --- |
| Memory | `_memset`, `_memcpy`, `_memmove`, `_memcmp` | frame (memset, memcpy), load | bytes; EAX/ECX/EDX; direction flag | `core/src/rtl.c`; shim modes compare/replaced ([The pure imports as own code](#the-pure-imports-as-own-code)) |
| Strings | `_strlen`, `_strcat`, `_strncat`, `_strncpy`, `_strchr`, `_strrchr`, `_strstr`, `_strncmp`, `_stricmp`, `_strnicmp`, `_strupr`, `_strlwr` | load (strcat, strlen, strchr, stricmp) | bytes; EAX (difference values); ctype table | `core/src/rtl.c` |
| Conversion, formatting | `_itoa`, `_atol`, `_sprintf`, `_vsprintf` | load (itoa), frame (sprintf, debug text) | output bytes and EAX for integer and string formats only | `core/src/rtl.c` |
| Heap | `_calloc`, `_realloc`, `_free` | through `new`/`delete` at frame and load time | zero fill; header word at p−4 (read by the game); 4-byte alignment; the address sequence | `core/src/rtl_heap.c`; modes shadow ([shadow stage](#the-heap-shadow-stage)) and replaced ([replaced](#the-heap-replaced)) |
| Exit list | `_atexit` | device constructors (platform) | 32 entries, LIFO, return value | `core/src/rtl_start.c` ([The lifecycle group as own code](#the-lifecycle-group-as-own-code)) |
| Lifecycle, exceptions | `__startup`, `__exit`, `__ErrorExit`, `_abort`, `__ExceptionHandler`, `@_CatchCleanup$qv` | handler address in every simulation frame record (never invoked) | record order at startup and exit, WinMain arguments, stack depth at WinMain | `core/src/rtl_start.c`, `proxy/rtl_life.h`, the own DLL |
| Data artefacts | `_errno`, `__streams` | none | nothing (slots must resolve) | exports of the own DLL |
| Files | `_open`, `_close`, `_read`, `_write`, `_lseek`, `_filelength`, `_fopen`, `_fclose`, `_fread`, `_fwrite`, `_fseek`, `_ftell`, `_rewind`, `_fscanf`, `_findfirst`, `_findnext`, `_flushall` | text grid (development only) | CW3220's handle and `FILE` tables, the bytes read and written | `core/src/rtl_io.c`; modes shadow/replaced ([File and console imports](#file-and-console-imports)) |
| Console | `_printf`, `__fputc`, `_fputs` | none | nothing visible (dumps, assert) | stubs, mode replaced only |
| Clock | `_time` | none (`-D` only) | wall clock | `core/src/rtl_io.c`; modes shadow/replaced |

Imports are reached through IAT slots, not prologue detours, so the own
runtime swaps slots (with signature and hash checks) instead of patching
prologues. Pure functions carry no game state, so their shims can arm
before DBSIM's static initializers and see the whole mission load.

## 6. What the own runtime must keep

1. **The runtime pieces in DBSIM.** `0x49630c`, `0x49667c`, `0x496406`,
   the c0w32 entry and the inline counter/FS:0 code are DBSIM's own code
   and stay as long as original DBSIM functions run. Their only CW3220
   dependencies are error paths (`_abort`, `__ErrorExit`, `_flushall`,
   `@_CatchCleanup$qv`) and the handler address `0x4967f0` stored in each
   record. That value is DBSIM's thunk address, so the stack residue of a
   record does not change when the slot behind it points to an own handler.
2. **The FS:0 chain and the main thread's stack layout.** Records link to
   the previous FS:0 head; the outermost head and the stack depth at
   WinMain come from CW3220's `__startup`. Game code only stores FS:0
   values, it never compares them, but stack addresses end up in records
   and in stack residue that later callees read as uninitialized locals.
   An own startup therefore enters WinMain at the
   same depth below the stack base. StackBase−8/−4 (context, counter) are
   set by DBSIM's own entry code and need no CW3220.
3. **Init and exit order.** 65 init records in table order before WinMain
   (record 46 of 0–64, `0x45caf4`, initializes both RNG streams,
   [rng.md](../reverse-engineering/rng.md)); 26 exit records in reverse
   table order after the atexit functions.
4. **The heap contract** of section 4.4 as far as the game sees it: zero
   fill, the header dword at p−4 (`0x47747c`, `0x477548`, `0x47758c` read
   it), 4-byte alignment, `malloc(0)` = NULL. The own allocator reproduces
   the exact address sequence too, although the layout does not change the
   oracle's outputs (section 4.3).
5. **Register results of the pure functions** (section 4.4), because
   callers and hooks read EDX/ECX after the call.
6. **The proxy's exit hook.** Live coverage patches the `__exit` slot and
   registers a callback through `_atexit`; the own runtime offers both.
7. **`-l` behaviour** if it is ever needed: the arena lives in DBSIM's BSS
   and is DBSIM code; it needs no CW3220 allocator.

## 7. Open points of the inventory

- **Hypothesis:** the exception descriptor tables at `[record+8]` matter
  only for unwinding; in a normal run nothing reads them, because nothing
  raises. What the unwind at the process end reads from them:
  [The unwind clean-up at the process end](#the-unwind-clean-up-at-the-process-end).
- **Hypothesis:** `0x435458`, `0x4342c0`, `0x4326a8`, `0x4352ec` (cockpit
  region) and the other owners without a static reference are dead; they
  are counted under E.
- Owner categories are static reach plus manual verdicts. A function that
  simulation code also reaches through an unresolved indirect call may sit
  in a non-simulation column; the import classes do not depend on that
  split, only the S counts do (and the shim's `sim` category, which uses
  the 85-site table).

## The IAT-slot shim

Code: `tools/reference-game/proxy/rtl_imports.h` (included by
`media_proxy.c`; tick fields through `tick_log.h`). The shim redirects
DBSIM's CW3220 import slots to thunks that count each call and, per import,
forward it to the CW3220 export (mode `original`) or to an adapter of the
own code (`compare`, `replaced`, `shadow`; the modes of each group are in
the sections below). With every import `original` no import's behaviour
changes. Facts come from the llvm-objdump listing of the reference DBSIM and
from the inventory's `owner_categories.json`.

### What it does

- **Slots.** The 53 CW3220 slots `0x6cc2d4 + 4·i` and their thunks
  `0x496754 + 6·i` (`jmp dword [slot]`) in import order. 47 slots are
  redirected; six never are: `__startup` (used for arming, below),
  `__exit` and `_atexit` (live coverage and the speed runtime verify and
  patch them, `live_coverage.h`, `oracle_perf_runtime.h`; their own code is
  [The lifecycle group as own code](#the-lifecycle-group-as-own-code)),
  `__ExceptionHandler` (called by the OS unwinder through `0x4967f0`) and
  the data imports `_errno` and `__streams` (section 4.5).
- **Thunks.** One per import, in assembly. The thunk borrows EAX by
  exchanging it with the return-address slot (`xchg eax, [esp]`), counts,
  exchanges back and tail-jumps (`jmp [target]`) to the export or adapter.
  On entry to the export every general register, ESP, the stack contents
  (nothing is written below ESP; `[esp]` is restored), DF and the x87/SSE
  state are exactly what the caller left; the export's results (EAX, EDX,
  ECX, memory) reach the caller unchanged because the thunk is no longer on
  the stack. Only the arithmetic flags are clobbered (not an input of any
  import). The thunks call nothing, so they are reentrant (an export that
  calls back through another thunk is counted twice, correctly). Calls from
  other threads jump to the stored export (`rtl_original`), never to an
  adapter.
- **Counters** (uint64, per import): `calls`, `mismatches` and the caller
  categories, from the return address: `hook` = inside the proxy module (a
  replaced hook's core calling the import through a guest bridge, e.g.
  `base_aim_node`'s memset), `sim` = one of the 85 simulation call sites of
  section 3 (20 hook, 6 frame, 59 load; a 76 KiB bitmap over the first
  `0x98000` bytes of the image, one `bt`), `dbsim` = any other DBSIM
  address, `other` = anything else. `calls` is the sum of the four. Calls
  count only on the thread whose stack reservation was recorded at install
  (DBSIM's main thread; the test is ESP inside it, no register needed);
  calls from other threads are forwarded and counted in one shared,
  lock-prefixed `foreign_calls`. A simulation call from a site that the
  inventory classified otherwise counts as `dbsim`.
- **Cost.** About 15 instructions per call, two of them `xchg` with memory.

### When it installs (timing)

The proxy arms its function hooks, the tick log and live coverage at its
first DirectDraw or MCI call inside the mission load (`initialize_forwarding`,
[rng.md](../reverse-engineering/rng.md) Q1). DBSIM's static initializers
(the 65 init records, record 46 `0x45caf4` with the RNG stream init and its
two `_memmove` calls) run inside `__startup`, and section 3.3's mission-load
sites partly before the first DirectDraw call, so a shim armed there would
miss most of the load-time traffic. Arming in `DllMain` would see everything
(DBSIM imports `DDRAW.dll` statically, so the proxy's `DllMain` runs before
DBSIM's entry point and after the loader has bound DBSIM's IAT), but reading
the INI and hashing the executable there means file I/O under the loader
lock.

The shim therefore arms **at DBSIM's entry point**:

1. `DllMain` (memory only): if `ES2_RTL_CONFIG` is set and the process is
   `DBSIM.EXE`, it checks the PE header (`es2_hook_image_valid`), the
   `__startup` thunk bytes at `0x4967d8` and that slot `0x6cc32c` holds
   `GetProcAddress(cw3220, "__startup")`, allocates a 256 KiB private stack
   and points that one slot at `rtl_startup_entry`. Without the variable
   nothing at all is done (no allocation, no slot touched).
2. DBSIM's entry `0x401000` ends with `push 0; jmp 0x4967d8` (checked
   against the reference file by the fixture). `rtl_startup_entry` saves ESP
   in a static, switches to the private stack, saves flags, the general
   registers, the TEB stack bounds (`fs:[4]`, `fs:[8]`, set to the private
   stack while it runs) and the x87/SSE state (`fxsave`), restores the
   `__startup` slot, runs the arming step, restores everything and jumps to
   the real `__startup` with the entry ESP. The game's stack is not written
   (no residue for later uninitialized reads); LastError is preserved.
3. The arming step (outside the loader lock, before any init record):
   reads `[rtl]`, hashes `DBSIM.EXE` (size 724,512 and the full SHA-256 of
   hooks.md, with the proxy's own SHA-256 (`proxy/sha256.h`, shared with the
   presenter capture) so that no CryptoAPI provider DLL is loaded before the
   game's static initializers), verifies the image and installs.

Interplay with the other proxy patches: the shim touches only CW3220 slots
and none of the slots or bytes the later installers verify (`__exit`,
`_atexit`, the hook prologues, the tick site, the GetTickCount and message
slots). The hooks' guest bridges call thunks such as `0x49681a` (memset)
and therefore go through the shim (category `hook`). The function hooks can
arm at the same entry too ([mission_load.md](../reverse-engineering/mission_load.md),
`proxy/early_arming.h`).

### Verification and refusal

Before any slot changes: PE header and image size; the import descriptor
named `cw3220.DLL` with `FirstThunk` = `0x2cc2d4` and exactly the 53 names
in the expected order; the 53 thunk byte strings; every one of the 85
simulation sites is `E8 rel32` to the thunk of its import; each of the 53
slots equals `GetProcAddress` of its name in the loaded `cw3220.DLL`. Any
mismatch refuses the whole shim and changes nothing. The slots are then
patched one by one with a compare-exchange, after everything a thunk reads
has been published (a call arriving during installation reaches either the
export or a ready thunk); a failed patch rolls back every slot. Refusal
reasons (tick field `rtl_refused`, `log_event` `rtl_imports_refused`):
`config_enabled`, `config_imports`, `config_unknown_import`,
`config_duplicate_<key>`, `not_shimmable_<key>`, `mode_reserved_<key>`,
`mode_malformed_<key>`, `hash`, `image`, `image_size`, `pe_header`,
`import_directory`, `import_descriptor`, `iat_rva`, `import_lookup`,
`import_name_<key>`, `import_count`, `thunk_<key>`, `sim_site_<rva of the
call>`, `slot_<key>`, `options`, `proxy_range`, `patch_<key>`,
`rollback_failed`, `private_stack`, `startup_restore`, `config_path`, and
`not_armed` (still pending at a tick: the entry never went through the
slot). The heap shadow and the I/O shadow add their own reasons (below).
`rtl_uninstall` (fixtures) restores every slot that still holds its thunk
with a compare-exchange; a slot someone else changed is left alone.

### Configuration

The INI named by the environment variable `ES2_RTL_CONFIG` (absolute path;
it may also name the hooks' `es2-oracle.ini`), section `[rtl]`:

```ini
[rtl]
enabled=1          ; absent or 0: nothing is installed
imports=all        ; or a comma list of keys: memset,memcpy,strcat
memset=original    ; per import, key = name without leading underscores
```

Keys are the import names without leading underscores (`memset`,
`ErrorExit`, `fputc`; `CatchCleanup` for `@_CatchCleanup$qv`), matched
case-insensitively; values as for `[hooks]`: `original`, `compare`,
`replaced`, `shadow`, case-insensitive, missing = `original`. A mode that
the import's group does not offer is refused (`mode_reserved_<key>`), as
are malformed values and unknown, duplicate or non-shimmable names in
`imports`. A refusal installs nothing. An environment variable (instead of
a key the proxy reads at its first DirectDraw call) is needed because the
decision is taken in `DllMain`, where the proxy reads no files;
`run-oracle-parallel.ps1` and its children pass the environment on to DBSIM
unchanged, as for `ES2_LIVE_COVERAGE_CONFIG`. The per-import mode selects
the thunk's target (`rtl_targets`).

### Tick fields and the exit record

While the shim is installed, every tick (schema 2) appends
`rtl_<key>_calls` for each redirected import (47 by default, IAT order)
and `rtl_foreign_calls`. The `exit_counters` record
([tick_log.md](tick_log.md)) appends the full set per import:
`rtl_<key>_calls`, `_mismatches`, `_hook`, `_sim`, `_dbsim`, `_other`, then
`rtl_foreign_calls`; with the shim the record is written even without
`[hooks]`. A refused request adds `rtl_refused` instead. Without
`ES2_RTL_CONFIG` the log carries no `rtl_*` field. Maximum sizes with every
import `original` (fixture `bound`, every hook replaced and every counter
at `UINT64_MAX`): tick 99,173 bytes, exit record 108,059 bytes, both below
`TICK_LOG_LINE_MAX` (262,144). `oracle_regression.py normalize` copies the
`rtl_*` exit fields into `exit_counters` next to `hk_*`; the comparison
tools read fixed field lists and ignore them (`counter_normalization_test`).
The fields the other modes add are listed in their sections.

### Tests

`//tools/reference-game/proxy:rtl_imports_<mode>_test`
([fixture](../../tools/reference-game/proxy/tests/rtl_imports_fixture.c)):
- `sha256`: three FIPS 180-4 vectors, 308 lengths against CryptoAPI, and 14
  files around the block and read-buffer boundaries through
  `rtl_file_matches`; the header itself is tested in
  `//tools/reference-game/proxy:sha256_test`.
- `forward`: a synthetic DBSIM-shaped image with descriptor, names, thunks,
  IAT and the 85 sites, own stub exports behind a resolver: 47 slots
  redirected; an assembly probe export sees every register, ESP, the return
  address, the argument, the x87 control word and ST0 exactly as without
  the shim, and nothing below ESP changes; categories hook/sim/dbsim/other
  including a DBSIM address beyond the bitmap; every import forwards once
  per call; a reentrant export; uninstall byte-exact over the whole image,
  a foreign slot left alone.
- `refuse` (13 refusals, image byte-identical after each), `rollback` (a
  failed patch and a failed protection restore roll back byte-exactly;
  calls made while installing reach the export or the thunk), `foreign`
  (8,000 calls from four threads: forwarded, only `foreign_calls`),
  `config` (the `[rtl]` rules above).
- `entry`: arming through the `__startup` slot. The arming step runs on the
  private stack with the TEB bounds swapped; `__startup` sees the entry
  registers, ESP, return address, argument, TEB bounds, LastError and x87
  state unchanged and an unchanged stack below ESP; the static-init memset
  at `0x45cb25` counts as `sim`; disabled at entry: slot restored, nothing
  else; DllMain refusals.
- `reference` (with `ES2_DATA_ROOT`): the own SHA-256 of DBSIM.EXE, the
  descriptor, names, thunks and 85 sites against the file, the entry's
  `jmp` to the `__startup` thunk, all 53 names exported by the reference
  `CW3220.DLL`.

`//tools/reference-game/proxy:rtl_imports_tick_<mode>_test`
([fixture](../../tools/reference-game/proxy/tests/rtl_imports_tick_fixture.c)):
`fields`, `off`, `refused`, `bound`.

### Live

Every import `original`, all hooks `replaced`, `[rtl] enabled=1
imports=all`: all six suites identical, no `rtl_refused`, 47 imports counted
(283 `rtl_*` exit fields in the four outcome suites; acceptance-60s and
c1_01-collision are stopped by the runner and have no exit record).
Evidence: es2-data `logs\check-set3-20261011T005925Z`.
- `rtl_memset_hook` equals the sum of the hooks' bridged memset counters
  `hk_*_call_49681a` exactly (c1s/c3_09/c4_08/c3_10: 89,692 / 346,433 /
  25,558 / 55,383); `rtl_memcpy_hook` = `hk_shape_instance_call_496826`
  (318 / 1,838 / 254 / 136); `rtl_foreign_calls` 0.
- Simulation callers (c3_09 `sim`): `memset` 2,286, `memcpy` 283, `strlen`
  33, `itoa` 18, `strcat` 3, `memmove` 2, `strchr` 1, `stricmp` 1.
  `sprintf` (2,544,293 calls), `fopen`, `fclose`, `fscanf` come only from
  platform and HUD code (`sim` 0).
- `rtl_memset_hook` varies by a few calls between identical runs (89,692 /
  89,694 / 89,698 in c1s over three runs): input-side hooks follow the
  message pump. The simulation counters do not vary.

## The pure imports as own code

The 20 imports of class *pure* are own code in
[`core/src/rtl.c`](../../core/src/rtl.c) (header `es2/core/rtl.h`, unit
test `//core:rtl_test`). The shim accepts the modes `replaced` and
`compare` for exactly these 20, and the in-vitro harness compares the own
code with the original functions of a privately mapped `CW3220.DLL`.
Contracts were read from `llvm-objdump -d -M intel` of the reference
`CW3220.DLL` (CD root, SHA-256 `6e90e1ca…f505`, link base `0x400000`; VAs
below are at that base).

### Contracts (confirmed in vitro against the DLL)

`E` is ESP at entry (`[E]` = return address, the arguments from `E+4`).
Every function keeps EBX/ESI/EDI/EBP, leaves DF clear and touches no x87
state. "Residue" is what the CW3220 frame leaves below the return address
(the registers pushed after `push ebp`, in push order). Strings are scanned
with `repne scasb`, so every function reads exactly the bytes up to and
including the NUL (no word-at-a-time over-read).

| Import (VA) | Memory | EAX / ECX / EDX | Residue |
| --- | --- | --- | --- |
| `_memset` (`402a58`) | fill byte = low byte of `c`; n/4 dwords then n mod 4 bytes | dest / 0 / n | EBP, EDI |
| `_memcpy` (`4029e8`) | n/4 dwords **forward, each read whole before it is written**, then n mod 4 bytes; an overlap by 1 to 3 bytes smears dwords (`"ABCDEFGHIJ"`, dest = src+1, n = 9 gives `"AABCDDFGHH"`) | dest / 0 / n | EBP, ESI, EDI |
| `_memmove` (`402a0c`) | dest < src: as memcpy; dest > src: the n mod 4 trailing bytes backward, then the dwords backward; dest = src: nothing | dest / **n if dest = src**, else 0 / n | EBP, ESI, EDI |
| `_memcmp` (`402ed4`) | bytes up to the first difference | unsigned byte difference (0 when equal) / n−i−1 for a difference at i, −1 when all n are equal, 0 for n = 0 / b's byte (b itself for n = 0) | EBP, EBX |
| `_strlen` (`402dfc`) | — | length / length+1 / **unchanged** | EBP, EDI |
| `_strcat` (`402d3c`) | scans dest, then src, then copies strlen(src)+1 bytes like memcpy | dest / 0 / strlen(src)+1 | EBP, ESI, EDI |
| `_strncat` (`402e18`) | copies m = min(strlen(src), n) bytes like memcpy, then a NUL | dest / 0 / m | EBP, ESI, EDI |
| `_strncpy` (`4030b8`) | l = strlen(src); l > n: memcpy n bytes; else memcpy l bytes and memset(dest+l, 0, n−l) (no NUL when l = n) | dest / 0 / n if l > n, else n−l | (calls strlen, memcpy, memset) |
| `_strchr` (`402d78`) | scans s, then its first strlen+1 bytes for the low byte of `c` (so a NUL is found) | pointer or 0 / remaining scan count / s | EBP, EDI |
| `_strrchr` (`4031c0`) | strlen, then backward from the NUL to s comparing the low byte of `c` | pointer or 0 / s / c (the whole argument) | (calls strlen) |
| `_strstr` (`403280`) | strlen of both; naive search restarting at start+1; empty s2 gives s1, empty s1 gives 0 | pointer or 0 / s2 (empty cases) or s1 with the low byte of the last s1 byte read / unchanged (empty cases) or the scan pointer or match index | (calls strlen) |
| `_strncmp` (`402e64`) | scans b for a NUL within n bytes (k bytes), then compares k bytes (`repe cmpsb`) | unsigned difference of the last pair compared / remaining count / b's last byte; n = 0: 0 / 0 / unchanged | EBP, ESI, EDI |
| `_stricmp` (`403074`) | byte pairs through `toupper` until a difference or a NUL in a | `(int8)TU(a) − (int8)TU(b)` / the caller's ECX with its top byte replaced by TU(b) (the local is the pushed ECX slot) / `(int8)TU(b)` | (calls toupper) |
| `_strnicmp` (`403108`) | pairs through `toupper` while equal, a's byte nonzero and the count not exhausted; one pair is read even for n = 0 | 0 when the count ran out (ECX/EDX = TU(a)/TU(b) of the last pair), else `TU(a) − TU(b)` as ints / the sign-extended b byte / TU(b) | (calls toupper) |
| `_strupr`, `_strlwr` (`412e34`, `412e10`) | every byte through toupper/tolower, written back including the NUL | s / 0 / 0 | (calls toupper/tolower) |
| `_itoa` (`414d58`) | radix 2..36 (signed compare), otherwise only `buf[0] = 0`; **signed only in radix 10** (`itoa(-5, b, 16)` = `"fffffffb"`), INT_MIN gives `"-2147483648"`; digits above 9 from `'a'` | buf / E−68 and E−68 (the helper's digit buffer) for a valid radix, else buf / value | (calls the helper) |
| `_atol` (`4148c8`) | skips bytes whose ctype entry at the **sign-extended** index has the space flag: 0x09–0x0d, 0x20 and every even byte 0x80–0xfe (they index the identity table below ctype; 0xff hits ctype[−1] = 0); one sign; decimal digits with 32-bit wrap | result / magnitude / address after the byte that ended the digits | EBP, EBX |
| `_sprintf`, `_vsprintf` (`40718c`, `4071b0`) | `dest[0] = 0`, then the formatter (`4075d8`) writes through an 80-byte buffer; each flush copies the chunk to the cursor, advances it and stores a NUL; **the cursor is the caller's first argument slot**, so `[E+4]` = dest + count afterwards | count / E−216 (the formatter's buffer record) / 0 | (formatter frame) |

`toupper`/`tolower` (`412eb4`, `412e8c`) take the sign-extended byte: the
byte 0xff arrives as −1 (EOF) and returns −1; otherwise only `a`–`z` /
`A`–`Z` change (ctype flags 8 and 4; no flag on 0x80–0xff, and DBSIM
imports no `setlocale`). So stricmp and strnicmp differ for high bytes:
stricmp subtracts sign-extended bytes, strnicmp the int results (0x80–0xfe
positive, 0xff −1).

**The formatter subset.** The own formatter follows the state machine of
`4075d8` with its class table (`0x4202b7`): flags `-` `+` space `#` `0`
(only before width and precision; `+` wins over space; `0` is ignored after
`-`), width and precision as digits or `*` (a negative `*` width sets `-`;
a negative `*` precision means none), size letters `l`, `h`/`H`, `L`, `N`,
`F`, conversions `d i o u x X c s` and `%%`. Quirks reproduced: `%c` reads
one byte of its slot and writes it even when it is 0; `%s` with NULL prints
`(null)`; `%h` reads the low word, sign- or zero-extended; a zero value with
precision 0 prints nothing; with `#` and a nonzero hex value the zero count
(from the precision or the `0` flag) loses two for the `0x` prefix (so
`%#.3x` of 1 is `0x1`); `#o` forces one zero; an invalid specification (an
unknown letter, a control or high byte, `%` after flags, a misplaced digit,
`.` or `*`) copies the rest of the format verbatim from its `%` and **ends**
the formatting. Floating-point conversions, `%p` and `%n` are outside the
subset: the own code then does nothing and the call goes to the original
(`ES2_RTL_UNSUPPORTED`); a dry pass over the format decides this before
anything is written. An empty result is declined too (its EDX is formatter
residue), after the `dest[0] = 0` that the original also writes first.
DBSIM's own formats are inside the subset (`%d`, `%Nd`, `%ld`, `%s`, `%c`,
`%08x` and literal text; section 2 and the strings of the image). The own
DLL has its own answer for the declined formats
([The own cw3220.dll](#the-own-cw3220dll)).

### The shim's modes (proxy/rtl_imports.h)

- **Configuration.** `<key>=compare|replaced` is accepted for the 20
  imports above.
- **replaced** (`rtl_replaced_<i>`): saves EAX..ESP in a static record,
  switches to the private stack (the 256 KiB block of the entry arming,
  allocated at install when missing), runs the own function on the game's
  memory, writes the leaf residue (table above) below the return address,
  switches back and returns EAX/ECX/EDX. EBX/ESI/EDI/EBP, DF and the FPU are
  untouched; nothing else is written below ESP (the C part never runs on the
  game's stack). A declined sprintf/vsprintf jumps to the export with the
  entry registers (`_skipped` + 1).
- **compare** (`rtl_compare_<i>`): the own function runs first, on the real
  pre-state, through an **overlay** (reads fall through to memory, writes go
  to a private hash table of up to 262,144 bytes); guard bytes are taken (up
  to 8 below the lowest and above the highest written address, in the same
  page, never in the stack below the arguments); the return address is
  swapped for `rtl_compare_post` and the export runs on the real state with
  the entry registers and stack. In `rtl_compare_post` the slot is restored
  (as the export's `ret` leaves it) and compared: EAX/ECX/EDX, every overlay
  byte against memory, the guard bytes and, for the leaf functions, the
  residue. The caller always gets the export's results. A mismatch counts
  `_mismatches`; the first one of the run is kept as
  `key:what@address:args=a0,a1,a2:own=eax,ecx,edx:orig=eax,ecx,edx` (`what`
  = `registers`, `memory`, `guard` or `residue`). Declined calls (formats
  outside the subset, empty results) and an overlay overflow count
  `_skipped`. Compare is well defined for all 20: every own function reads
  exactly the bytes the original reads, so running it first on the real
  pre-state cannot fault where the original would not, and the overlay
  isolates its writes, including the overlapping cases and sprintf's
  argument slot.
- **Nesting.** A call that arrives while an adapter runs goes to the export
  (`_skipped` + 1); the exports never call back into DBSIM's thunks, so this
  happens only in fixtures.
- **Not reproduced in replaced mode:** the stack residue of the non-leaf
  functions (strncpy, strrchr, strstr, stricmp, strnicmp, strupr, strlwr,
  itoa, sprintf, vsprintf): below their return address the stack keeps what
  was there. Compare mode runs the export, so its residue is the original's.
  The replaced live run (below) shows no reader of that residue in the six
  suites.
- **Tick fields.** Unchanged per tick; the exit record adds
  `rtl_<key>_skipped` after the six counters of every import whose mode is
  not `original`, and `rtl_first_mismatch` after `rtl_foreign_calls` when
  there was a mismatch. With every import `original` the output carries
  neither.

### The hooks that call `_memset` through the guest bridge

Eleven hooks (`base_aim_node`, `weapon_tally`, `object_init`,
`player_input`, `set_length`, `script_start`, `debris_burst`,
`projectile_create`, `bullet_create`, `fire_create`, `control_record`) call
the thunk `0x49681a` through the bridge and count it in
`hk_<hook>_call_49681a`. They keep doing so: calling the own memset
directly instead would change those counters (part of every regression's
exit record and of `hook_registry_test`'s callee lists), would not leave
memset's stack residue at the bridge's call depth, and would gain nothing:
with `memset=replaced` the bridged calls already run the own code (category
`hook` in `rtl_memset_hook`), with `memset=compare` they are compared. The
three hooks that model memset natively (`weapon_step`, `node_matrices`,
`group_fire`) do so in their core modules.

### Tests

- `//core:rtl_test`: 59 checks of the contracts above on a synthetic arena
  (overlap smear, memmove's ECX, stricmp/strnicmp high bytes, itoa radices,
  atol's high-byte whitespace, a 21-specification format, vsprintf with `*`,
  a 180-byte output over two flushes, declined formats, an invalid
  specification).
- `//tools/reference-game/invitro:differential_rtl_test` (quick) and
  `:differential_rtl_full_test` (manual), pass `--rtl`:
  - The harness maps `CW3220.DLL` from `ES2_DATA_ROOT` (CD root or
    `extracted/cd`; size 176,128 and SHA-256 checked on the read buffer) at
    an address the system chooses, applies its 2,675 HIGHLOW relocations,
    points its 95 imports at named traps and makes CODE read/execute; no
    entry point runs.
  - Per function 20,000 (full: 200,000) generated direct cases with random
    and edge arguments and random entry EAX/ECX/EDX: lengths 0–3, 4, 7–9,
    15–17, 31–33, 79–81, 255, 256 and up to 4,000; overlaps by −8..+8 and
    equal pointers; strings ending at the last byte before an inaccessible
    page; all 256 leading bytes for atol; INT_MIN, −1, 0 and the radices 0,
    1, 2..36, 37, −1, `0x80000002` for itoa; `c` with high bits for
    strchr/strrchr; mixed-case, 0x80–0xff and 0xff strings for the case
    functions; random formats of the grammar above, including `%%`,
    invalid and unsupported specifications, NULL strings and `%n`.
  - The export runs on a private test stack, then the arena is restored and
    the own code runs on the same addresses: EAX, ECX, EDX, every byte of
    the 64 KiB arena and the 16 argument slots must be equal. Formats with
    a floating-point conversion run only the own code, which must decline
    without writing (the export's float path calls CW3220 imports).
  - Then the shim is installed on the mapped DBSIM (its CW3220 slots
    pointed at the mapped exports, the test stack as the owner stack), all
    20 in mode replaced and then compare; 2,000 (full: 20,000) cases per
    function and mode through DBSIM's thunk (`0x496754 + 6·i`) must equal
    the direct export call in registers, arena, argument slots and the
    stack below the arguments (all 4 KiB for the leaves and in compare
    mode, the return slot for the others), with 0 compare mismatches.
  - Full tier: sprintf/vsprintf 11,077 / 11,090 of 200,000 direct cases
    declined (empty results and formats with a float, `%p` or `%n`),
    installed skipped 643 / 657 (replaced) and 652 / 652 (compare) of
    20,000, every other function 0.
- `//tools/reference-game/proxy:rtl_imports_replaced_test` and
  `:rtl_imports_compare_test`: the adapters in the shim's synthetic image.
  All 20 through their thunks equal a direct own call; registers, ESP, DF
  and the 256 bytes below the arguments are as specified; declined, nested
  and foreign calls reach the export. Compare against own stand-in exports
  (memset and strlen with the CW3220 contract): 0 mismatches; a wrong last
  byte, a byte too many and a wrong EDX are each counted with their detail;
  a declined format is skipped; the exit-record fields.
- Mutations: 11 entries `rtl_…` in `check_mutations.ps1`, at least one per
  family (memory, strings, case, conversion, formatting), all caught
  ([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).

### Live

All hooks replaced.
- **Compare** (the 20 imports in `compare`): all six suites identical,
  every `rtl_*_mismatches` 0, no `rtl_first_mismatch`, `rtl_memset_skipped`
  3 per suite (memsets over 262,144 bytes, the overlay limit), nothing else
  skipped. Evidence: es2-data `logs\check-set4a-20261011T020506Z`.
- **Replaced** (the 20 in `replaced`): identical, no skips, no refusal.
  `rtl_memset_calls` c1s/c3_09/c4_08/c3_10 about 450,300 / 1,175,080 /
  110,460 / 144,670 per run. Evidence: es2-data
  `logs\check-set4b-20261011T021359Z`.

### Limits

- The dry pass decides support on the format as it is before the call; a
  destination that overlaps its own format is not modelled (DBSIM never
  does that).

## The heap, shadow stage

An own model of the CW3220 heap in
[`core/src/rtl_heap.c`](../../core/src/rtl_heap.c) (header
`es2/core/rtl_heap.h`, unit test `//core:rtl_heap_test`), the import mode
`shadow` in the shim, and the in-vitro differential against the privately
mapped `CW3220.DLL`. The contract below was read from `llvm-objdump -d -M
intel` of the reference `CW3220.DLL` (VAs at its link base `0x400000`). The
model as the allocator: [The heap, replaced](#the-heap-replaced).

### The CW3220 heap (confirmed in vitro against the DLL)

| VA | Variable |
| --- | --- |
| `0x423944` | list head block: size word (`-4`) and footer (`-8`) 0, links at `+0`/`+4`; never allocated (size 0) |
| `0x42394c` | pointer to the head block (written once by the init record) |
| `0x423950` | rover: where the next search starts |
| `0x423938`, `0x423738` | region count (at most 64) and table of (base, size) pairs |
| `0x421f80`, `0x421f84` | committed end and reserved end of the last reservation |
| `0x422098` | reservation granularity, chosen at the first reservation: 1 MiB when `0x423a60` is 1 or 2, else 4 MiB |
| `0x423a60` | platform word of the DLL's start-up code `0x418fc1`: 2 when `GetProcAddress(GetModuleHandleA("kernel32.dll"), "Borland32")` succeeds, else 1 when `GetVersion` has bit 31 set, else 0; on Windows NT/10 it is 0, so reservations are 4 MiB (bytes and import names checked; not run) |

- **Blocks.** `p` is the pointer `malloc` returns, `s` the block size (a
  multiple of 4, at least 16). Header word at `p-4`, footer word at `p+s-8`:
  `s|1` while in use, `s` while free. A free block keeps its links at `p`
  (previous) and `p+4` (next) of a circular list through the head block. A
  region starts with the word 1 at its base (first block at `base+8`) and
  ends with the word 1 at `base+size-4`. A request of `n` bytes needs
  `s = 16` for `n < 8`, else `((n+3) & ~3) + 8` (32-bit wrap); `n = 0` or
  `s < 16` as a signed number gives NULL.
- **Init record `0x4170fc`.** Head block words 0, 0, self, self; rover =
  head; region count 0.
- **`_malloc` `0x41729c`.** Next fit: from the rover along the next links;
  the first free block with size `>= s` is taken. A remainder of at least
  16 bytes stays in the list in the block's place (header and footer `rest`
  without the flag) and becomes the rover; a smaller remainder is not split
  (the whole block is used, header/footer `size|1`) and the rover moves to
  the next block **only when the block was the rover**. After a full circle
  the heap grows; after the second growth without a fit, or when a growth
  fails, the result is NULL.
- **Growth `0x417028`.** `want` = `s` rounded up to 64 KiB. First the
  uncommitted rest of the last reservation (`min(want, rest)`) is committed
  and added. If that is not enough, a new reservation of `want` rounded up
  to the granularity is made; when it starts exactly at the old reserved
  end, only `want - rest` is committed there, otherwise all of `want`. The
  Win32 calls, through CW3220's own import slots (`0x424310`, `0x424314`):
  `VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS)` per reservation;
  `VirtualAlloc(page, 0x1000, MEM_COMMIT, PAGE_READWRITE)` per 4 KiB page;
  after a failed commit `VirtualFree(page, 0x1000, MEM_DECOMMIT)` for each
  page committed in that call and, for a new reservation,
  `VirtualFree(base, 0, MEM_RELEASE)`.
- **`__heapadd` `0x4171f8`.** A committed range (at least 16 bytes, rounded
  down to 4) extends the region whose end equals its start (the new block
  begins on that region's end marker) or opens a new table entry (base
  marker, first block at `base+8`); with 64 entries it is refused, and the
  growth step ignores the refusal (the committed memory is lost to the heap,
  the search fails again). The range becomes one block in use with an end
  marker and is freed.
- **`_free` `0x417160`.** NULL or a block whose header has no flag: nothing.
  A free block directly below (footer at `p-8` without the flag) absorbs the
  block and **keeps its list place**; a free block directly above is
  unlinked (the rover moves to its next block if it was the rover) and
  absorbed. Header and footer of the result get the plain size. A block not
  joined below is inserted directly after the head block.
- **`_calloc` `0x416ffc`.** `malloc(count * n)` (32-bit product), then
  `memset(p, 0, count * n)`.
- **`__expand` `0x417a70`.** Same block size: `p`. Smaller: a remainder of
  at least 16 bytes is split off (both blocks marked in use) and freed, so it
  joins a free block above; otherwise unchanged. Larger: only into a free
  block directly above whose size covers the difference; a remainder of at
  least 16 bytes stays free in that block's list place (the rover, if it was
  that block, moves to its next block, not to the remainder); else NULL.
- **`_realloc` `0x417b8c`.** `n = 0`: `free(p)`, NULL. `p = NULL`:
  `malloc(n)`. Then `_expand`; then, when the block directly below is free
  and both together are large enough: that block is unlinked (rover moved on
  if it was the rover), `memmove(below, p, s_old)` (the old block's whole
  size, its footer and the next block's header included), and a remainder
  of at least 16 bytes is split off and freed; result `below`. Otherwise
  `malloc(n)`, `memmove(new, p, s_old - 8)`, `free(p)`.
- **Who else allocates.** CW3220 calls `_malloc`/`_free`/`_realloc`/`_calloc`
  directly, bypassing DBSIM's import table: stream buffers (`setvbuf` code
  `0x405384`, freed by `_fclose` `0x40555c`), `_setargv` `0x419364` and the
  environment code after it, `_strdup`, operator new/delete
  (`@$bnew$qui` `0x417d94`, `@$bdele$qpv` `0x417d00`), locale, directory and
  path helpers, the iostream and string classes. Only DBSIM imports
  CW3220 (BWCC32 and SOS9503 do not), so every heap operation of the
  process is either a DBSIM import call or CW3220-internal.

### The model (core/src/rtl_heap.c)

The allocator's variables are in `es2_heap`; its words are reached through
`es2_heap_env` (`read32`/`write32` on 32-bit addresses), the Win32 calls
through `virtual_alloc`/`virtual_free` with the original's arguments in the
original's order, user data only through the optional `fill`/`move` (NULL in
the shadow: the model keeps no data; the allocator never reads a word it
did not write, which the in-vitro run checks). Functions: `es2_heap_init`,
`es2_heap_malloc`, `_free`, `_calloc`, `_realloc`, `_expand`,
`es2_heap_walk` (every allocator word of a heap: head block, per region the
markers, per block header, footer and the links of free blocks; validates
the structure) and `es2_heap_attach` (takes over a running heap from its
variables and memory). `es2_heap_shadow_env` supplies a word store (hash
table, caller storage), a journal of the words written per operation and a
replay of the recorded Win32 calls that checks every argument.

### Shadow mode (proxy/rtl_heap_shadow.h)

- **Configuration.** `calloc=shadow`, `realloc=shadow`, `free=shadow` in
  `[rtl]`, all three and all three selected (otherwise `shadow_set`).
  `malloc` is not an import of DBSIM; a `malloc` key is not read. The
  shadowed imports stay in mode original for counting and reporting.
  `heap_trace=<path>` writes the heap trace
  ([own_process.md](own_process.md), the heap trace): the heap at the
  attach, then one line per operation. `CREATE_NEW` is used, so an existing
  file refuses (`heap_trace_file`).
- **Install** (at DBSIM's entry, the shim's arming): the CW3220 file on disk
  must be the reference (size 176,128, SHA-256 `6e90e1ca…f505`, refusal
  `cw_hash`); the loaded image's size `0x2e000`; the five exports `_malloc`,
  `_free`, `_realloc`, `_calloc`, `__expand` at their RVAs with their
  prologue bytes; ten relocated operands of the heap code naming the
  variables above; the KERNEL32 import entries 84/85 named `VirtualAlloc`
  and `VirtualFree`; an initialized heap (refusals `cw_image`,
  `cw_export_<f>`, `cw_prologue_<f>`, `cw_operand`, `cw_imports`,
  `heap_uninitialized`). The model then **learns the heap as it is**
  (attach: the variables and a walk of every region; `heap_attach` if the
  walk fails; `rtl_heap_attach_blocks` reports the number of blocks, 0 when
  nothing was allocated before DBSIM's entry). Then CW3220's two OS slots
  get the recorder, each of the five entries a `jmp` to a detour (5 or 6
  bytes; the displaced `push ebp; mov ebp, esp; add esp, -k` or `push ebx;
  push esi` run in a trampoline that jumps back behind them), and only then
  DBSIM's three slots the shadow adapters. A failure rolls everything back
  (`cw_patch`, `rollback_failed`). 10 MiB are allocated for the model (word
  store of 2^20 entries, journal, a log of 65,536 OS calls). Without
  `shadow` nothing of this happens.
- **Per operation.** A heap operation is observed once, at its outermost
  entry: through a shadow adapter (DBSIM's import call, origin `import`) or
  through a detour (origin `internal` when the return address is inside
  CW3220, `external` otherwise); nested entries (calloc's malloc, realloc's
  free and `_expand`, the growth's `free`) and calls from other threads go
  straight to the trampoline (other threads: `rtl_heap_foreign_ops`). The
  entry registers are saved, the return slot is swapped for
  `rtl_heap_post` and the export runs on the game's stack with the entry
  registers. The recorder logs each `VirtualAlloc`/`VirtualFree` of the
  operation (pure asm; it swaps and restores only the return slot of that
  call) and passes it on to the real function. After the export returns, on
  the private stack, the return slot is restored and the model runs the same
  request with the recorded calls: **every reservation base is taken from
  the recorded `VirtualAlloc(NULL, …, MEM_RESERVE, …)` result**; every other
  argument (reservation size, commit and decommit pages, release) is
  predicted and compared. Then: the result pointer; every allocator word the
  model wrote in this operation against memory (except words that now lie in
  the returned block's user data, which calloc's zero fill or realloc's copy
  overwrote in the original), read only inside a region of the original's
  table or the head block; the allocator variables (rover, region count and
  table, committed and reserved end, granularity); no read of a word the
  model never wrote. The original's EAX/ECX/EDX reach the caller; LastError
  is preserved. The exports' ECX/EDX and their stack residue are not
  modelled in the shadow (it forwards the original's); the replaced
  allocator reproduces the registers the callers could read.
- **Mismatch.** Counted per import (`rtl_<key>_shadow_mismatches`) or for
  the detours (`rtl_heap_detour_mismatches`); the first is kept as
  `rtl_heap_first_mismatch` =
  `function:origin:what@where:args=a0,a1:caller=…:model=…:orig=…:result=…`
  (`what` = `result`, `os`, `os_log`, `store`, `unwritten`, `address`,
  `word`, `rover`, `regions`, `committed`, `reserved`, `granularity`,
  `region`). The model then re-learns the heap from memory
  (`rtl_heap_resyncs`); if that fails, later operations are counted in
  `rtl_heap_skipped`. A `free` of a pointer the model never saw (not from
  this heap) would be reported as `unwritten`.
- **Fields.** Every tick: `rtl_heap_shadow_mismatches` (the total) after
  `rtl_foreign_calls`. Exit record: `rtl_<key>_shadow_mismatches` after each
  shadowed import's six counters, the total after `rtl_foreign_calls`, and
  at the end `rtl_heap_internal_ops`, `rtl_heap_external_ops`,
  `rtl_heap_detour_mismatches`, `rtl_heap_foreign_ops`, `rtl_heap_os_calls`,
  `rtl_heap_os_unobserved` (OS calls outside an observed operation),
  `rtl_heap_skipped`, `rtl_heap_resyncs`, `rtl_heap_attach_blocks`,
  `rtl_heap_regions` and `rtl_heap_first_mismatch` when there was one.
  Bound (fixture): tick 103,034 bytes, exit record 113,807 bytes.
- **Cost.** About the original's own work again per operation (the model's
  search walks the same list), a hash lookup per word and a few dozen reads
  of the variables; nothing on the paths of the other imports.
- **Not observed:** the exports `__heapadd` and `__heapmin` called from
  outside CW3220 (no caller in DBSIM; `__heapmin` has no caller in CW3220
  either).

### Tests

- `//core:rtl_heap_test`: 130 checks on a scripted fake OS (sizes, refusals,
  next fit, both joins, list places, `_expand` in all directions, realloc
  into the block below and by copy, the rest of a reservation, a following
  and a separate reservation, refused reservation, failed commit with
  decommit and release, the 64-region limit, replay and attach).
- `//tools/reference-game/invitro:differential_rtl_heap_test` (quick) and
  `:differential_rtl_heap_full_test` (manual), pass `--rtl-heap`, see the
  [in-vitro README](../../tools/reference-game/invitro/README.md#--rtl-heap-the-heap-shadow-and-replaced).
  - Full tier: 1,600 direct sequences, 480,000 operations (malloc 96,135,
    calloc 38,466, free 149,039, realloc 91,346, `_expand` 28,559, new
    38,344, delete 38,111), 993,172 OS calls, platforms 0/1/2: result, OS
    calls, variables and every allocator word equal after every operation.
  - Paths: free alone 32,786, joins below 24,418, above 61,073, both
    40,915; split 167,547, whole block 438; realloc in place 53,280, into
    the block below 3,133, moved 9,521, NULL 83; `_expand` shrink 6,257,
    same 7,780, grow 6,570, fails 7,952; growth from the reservation's rest
    4,527, reservation following 310, elsewhere 2,371, refused 86, commit
    failed 497; NULL results 21,652; plus the 64-region limit.
  - Installed: 160 sequences of 200 operations without and with the shadow:
    EAX/ECX/EDX, the 4 KiB below the arguments and the heap identical, 0
    mismatches, origins counted exactly; a planted model error is caught
    once and resynchronized. Quick tier 160 / 16 sequences.
- Mutations: 9 entries `rtl_heap_…` in `check_mutations.ps1`, all caught
  ([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).
- Fixtures: `rtl_imports_config_test` (the shadow rules),
  `rtl_imports_refuse_test` (three shadow refusals, image unchanged),
  `rtl_imports_tick_bound_test` (fields and bound).

### Live

The 20 pure imports `replaced`, `calloc`/`realloc`/`free` `shadow`, all
hooks `replaced`: all six suites identical, no `rtl_refused`. In the four
exit records (c1s/c3_09/c4_08/c3_10) every shadow and detour mismatch
counter 0, `rtl_heap_resyncs` 0, `rtl_heap_skipped` 0, no
`rtl_heap_first_mismatch`; `rtl_heap_internal_ops` 9,
`rtl_heap_external_ops`/`foreign_ops`/`os_unobserved` 0, `rtl_heap_regions`
2; `rtl_calloc_calls` 7,792 / 15,634 / 8,192 / 7,530. The model predicts
every allocation of the six suites exactly. Evidence: es2-data
`logs\check-set5-20261011T033307Z`.

## The runtime lifecycle

The contract of DBSIM's start-up and shutdown as CW3220 performs them, and
the own model in [`core/src/rtl_start.c`](../../core/src/rtl_start.c)
(header `es2/core/rtl_start.h`, unit test `//core:rtl_start_test`),
compared in vitro with the privately mapped `CW3220.DLL` (`--rtl-start`).
Read from `llvm-objdump -d -M intel` of the reference `CW3220.DLL` (VAs at
its link base `0x400000`) and DBSIM.EXE. **Confirmed in vitro** here means
the original code ran on synthetic module tables and its every guest call,
Win32 call and variable matched the model; **static** means read from the
listing only. The model runs DBSIM's start-up and exits in the game:
[The lifecycle group as own code](#the-lifecycle-group-as-own-code).

### Start-up (confirmed in vitro unless marked static)

**DBSIM's entry (`0x401000`, static).** DBSIM's own code: zero-fills its BSS,
sets up the construction counter and the exception context (section 4.1),
stores `GetModuleHandleA(NULL)` at `0x49706a`, then `push 0; jmp 0x4967d8`
(the `__startup` thunk) with the module table `0x49702c` left on the stack.
So at `__startup`'s entry ESP = **E** with `[E]` = 0 (a fake return
address) and `[E+4]` = `0x49702c`; E is the process entry ESP minus 8 (minus
8 below StackBase−8 if the entry ESP was higher).

**The module table** (DBSIM's values):

| Offset | DBSIM | Meaning |
| --- | --- | --- |
| `+0x00`, `+0x04` | `0x4a79d8`, `0x4a7b5e` | init records (65 × 6 bytes) |
| `+0x08`, `+0x0c` | `0x4a7b5e`, `0x4a7bfa` | exit records (26 × 6 bytes) |
| `+0x10` | 1 | flags; bit 0: call WinMain (GUI), else main |
| `+0x18` | `0x46538c` | WinMain (stdcall, `ret 0x10`) |
| `+0x1c`, `+0x20` | `0x496510`, `0x49653c` | matherr, matherrl (stored at `0x421df8`/`0x421dfc`) |
| `+0x28` | `0x4a7974` | address of the module's `_fmode` word (it holds `0x4000`, text mode), stored at `0x41fdb0`; CW3220's open/fopen take their default mode from it ([File and console imports](#file-and-console-imports)) |

A record is byte 0 *done flag*, byte 1 *priority*, dword 2 *function*. All
65 init and 26 exit records of DBSIM have priority `0x20` and flag 0.

**`__startup(table)` (`0x41a6e0`)** in this order:

1. `[0x423f30]` ← flags & 1 (no reader found).
2. `__fpreset` (`0x413aac`): `fninit`, then `_control87(0x1332, 0x1fff)` with
   the default word of `0x421dc8`. The x87 control word is **`0x1332`**
   (64-bit precision, round to nearest, infinity control set, invalid,
   zero-divide and overflow unmasked, denormal, underflow and precision
   masked), status word 0, all tags empty. MXCSR is not touched.
3. matherr pair and `_fmode` pointer stored (table above).
4. The module list `0x423f24` = {count 1, table}.
5. **SEH record** in `__startup`'s frame at **E−0xc**: `[E−0xc]` = the
   previous FS:0, `[E−8]` = `0x405160` (CW3220's top-level handler); FS:0 ←
   E−0xc; its address in `0x423f0c` (`0x405274` → `0x4051fc` → `0x4052ac`).
6. `GetEnvironmentStrings()` → `0x423f10`, `GetCommandLineA()` →
   `0x423f14`. (`0x4192a0` is a bare `ret`; the list of runtime DLL modules,
   `0x41a62c`, is `xor eax, eax; ret`, so its init loop is dead.)
7. **Init records**: the selector `0x41a634(list, 0)` returns the next
   function, marks its record done (byte 0 ← 1) *before* the call, and the
   function is called with no arguments at ESP **E−0x20**, until the
   selector returns 0. Selector: per table it scans from start to end and
   stops early at a record whose function is 0; a record with a nonzero
   done flag is skipped; the threshold starts at `0xffff` (init) or 0 (exit)
   and a pending record becomes the choice when its priority is below the
   threshold (init) or at least the threshold (exit), its priority becoming
   the threshold. Init records therefore run lowest priority first and in
   table order among equal priorities; exit records highest first and in
   **reverse** table order among equals. DBSIM: init records in table order
   (record 46 of 0–64, `0x45caf4`, initializes the RNG streams, rng.md;
   record 64, `0x4956a4`, opens `\\.\DARKMONO.VXD` with `CreateFileA`, the
   handle closed by exit record 25).
8. Flags bit 0 is read again. **GUI**: lpCmdLine is the command line after
   the program name: skip blanks and tabs; a name starting with `"` runs to
   the next `"`, tab or end, else to the next blank, tab or end; a `"` there
   is skipped; then blanks and tabs. No backslash rules, a tab ends even a
   quoted name. `hInstance = GetModuleHandleA(NULL)` (called **after** the
   init records); **`WinMain(hInstance, 0, lpCmdLine, 10)`** (nCmdShow is
   always SW_SHOWDEFAULT, not the STARTUPINFO value) at ESP **E−0x30**, return
   address `0x41a812` in CW3220. **Console**: `main([0x423f18], [0x423f1c],
   [0x423f20])` at E−0x2c (for DBSIM dead; argc/argv are only built by
   `__setargv`, which nothing in DBSIM pulls in).
9. `exit(result)` (the internal call at `0x41a816`). `__startup` cleans
   WinMain's 16 argument bytes with `add esp, 0x10` although the stdcall
   WinMain has already removed them, so `exit` is entered at
   **X = E − 0x24 + bytes removed by WinMain**: E−0x14 for DBSIM.

The stack at WinMain's entry, relative to E (the in-vitro `DEPTH` line
measures −0x30 and, for the exit below, −0x44):

| Address | Content |
| --- | --- |
| E+4, E | module table, 0 |
| E−4 | saved EBP |
| E−8, E−0xc | SEH record: handler `0x405160`, previous FS:0 (FS:0 points here) |
| E−0x10 | an uninitialized local (only the dead DLL loop writes it) |
| E−0x14, E−0x18, E−0x1c | saved EBX, ESI (= table), EDI |
| E−0x20 … E−0x2c | 10, lpCmdLine, 0, hInstance |
| E−0x30 | return address `0x41a812` |

Below E−0x30 lies the residue of the init records and the selector (they
ran at E−0x20 and deeper).

### Exit paths (confirmed in vitro)

All four exports end in `0x4191a4(quick, keep_running, status)`, entered at
**Y**:

| Export (VA) | Y | atexit functions | exit records | exitbuf | exitfopen, exitopen | then |
| --- | --- | --- | --- | --- | --- | --- |
| `_exit` = C `exit` (`0x4191fc`) | X−0x14 | yes | yes | yes | yes | `ExitProcess(status)` |
| `__exit` = C `_exit` (`0x419214`) | X−0x14 | no | no | no | no | `ExitProcess(status)` |
| `__cexit` (`0x41922c`) | X−0x10 | yes | yes | yes | no | returns |
| `__c_exit` (`0x41923c`) | X−0x10 | no | no | no | no | returns |

- **atexit** (`_atexit` `0x419070`): table `0x423a64[32]`, count
  `0x4222b8`; returns 1 exactly when 32 are registered, else stores and
  returns 0. At exit the functions run last-registered first, each at
  Y−0xc; the count is re-read after every call, so a function registered
  during exit runs next.
- **Exit records** (`0x41a848`): the module list in selector mode 1, each
  function at Y−0x1c; then CW3220's second list `0x423f04` (BSS, never
  written: empty) and the DLL list (`0x41a630`: empty).
- **Stream hooks**, each called at Y−0xc: exitbuf `0x4222bc` (a bare `ret`
  at `0x4191a0` until the stream buffer set-up `0x4053c0` installs
  `0x407ca4`, fflush of the three standard streams), exitfopen `0x4222c0`
  (installed by the fopen path `0x405956`: `0x407c7c`, fclose of every open
  FILE, which flushes their buffers), exitopen `0x4222c4` (never written:
  the bare `ret`). So `exit` flushes and closes every FILE that DBSIM left
  open; `__exit` does not.
- `ExitProcess(status)` at Y−0x1c. The status is the pushed argument; no
  clean-up re-reads it. Afterwards the loader detaches CW3220, whose
  `__startupd` (reason 0) runs CW3220's own exit records (static, not
  traced beyond the record `0x405260` below).

**DBSIM's exit code** (the launcher reads it, [launcher.md](../reverse-engineering/launcher.md)):
WinMain returns the dword `0x4d27b8` → `exit` → atexit functions, last
registered first (the three device callbacks of section 2 and, in oracle
runs, the proxy's live-coverage callback; their relative order depends on
when the devices are constructed, not traced) → exit records 25…0 → stream
hooks → `ExitProcess(code)`. None of the 26 exit records writes `0x4d27b8`
or calls an exit function (table below), so the code is WinMain's result
unless a clean-up faults. The fatal path `0x479c78` uses `__exit(1)`: no
atexit functions, no exit records, no stream flush.

### abort, __ErrorExit, __ErrorMessage (confirmed in vitro)

- **`_abort`** (`0x419060`, entered at X): `raise(22)` (`0x41a2f0`).
  SIGABRT is index 9 of the signal table (`0x4223a8`: 2, 4, 8, 11, 15, 16,
  17, 20, 21, 22), its handler `0x4223a4`. Default (0; DBSIM never changes
  it, it imports no `signal`): raise itself shows the message and calls
  `__exit(3)` (message at X−0x20, Y = X−0x34). Ignore (1): raise returns,
  abort shows the message and calls `__exit(3)` (message at X−0xc, Y =
  X−0x20). Other: the entry is reset to 0, the handler is called with (22)
  at X−0x1c, or (22, byte `0x4223d9`) at X−0x20 when that byte is nonzero,
  then as for ignore. The message is `"Abnormal program termination"`
  (`0x422298`). Exit code **3**, which is also one of DBSIM's normal codes.
- **`__ErrorExit(text)`** (`0x418fa4`): message at X−0xc, `__exit(1)`
  (Y = X−0x20).
- **`__ErrorMessage(text)`** (`0x418ee4`, entered at M): the GUI branch
  (byte `0x41f069` is 1 in CW3220's data and never written, so the console
  branch with `WriteFile` to stderr is dead): `GetModuleFileNameA(NULL,
  buffer, 0x50)` into a stack buffer at M−0x58; the title is the name after
  its last `\`, else after its last `:`, else the whole name; style
  `0x10010` | modality, where modality (`0x418e9c`) is `0x2000` (task-modal)
  unless `GetVersion()` has bit 31 set and
  `EnumThreadWindows(GetCurrentThreadId(), 0x418e88, &flag)` finds no window
  (the callback sets the flag and stops), then `0x1000` (system-modal);
  `MessageBoxA(NULL, text, title, style)` at M−0x74.
- DBSIM reaches these only on error paths (terminate, assert log, vector
  new/delete "Illegal mode", pure-virtual call, `0x479c78`), none of them in
  the suites.

### Exception handlers (static)

The static reading; the paths the own runtime takes and their host and
in-vitro tests are in
[Error and exception paths of the own runtime](#error-and-exception-paths-of-the-own-runtime).

- `__startup`'s record (handler `0x405160`): for the C++ exception code
  `0x0EEFFACE` it unwinds and terminates; the stack-overflow message branch
  needs `0x41f818` ≠ 0 (constant 0: dead); then the user handlers at
  `0x42341c` and `0x423418` (CW3220's init record `0x405248` sets both to
  `0x405154`, which returns 1; `signal` would install `0x41a0f8` in the
  second); then `UnhandledExceptionFilter`. CW3220's exit record `0x405260`
  unlinks the record at DLL detach.
- `__ExceptionHandler` (`0x4042f0`, stored by `0x49630c` in every DBSIM frame
  record through the thunk `0x4967f0`): on an unwind (flags & 6) it runs the
  frame's clean-up (`0x40415d`) and, for `0x0EEFFACE`, the destructors of the
  thrown object; in the search phase it walks the frame's descriptor table
  from the context word `[record+0x10]` and acts only for `0x0EEFFACE`
  (catch blocks). DBSIM has no catch block other than its terminate's
  catch-all and never throws, so the handler only matters when an OS
  exception unwinds the stack (a crash).
- `@_CatchCleanup$qv` (`0x403e91`): takes the current exception off the
  context block (StackBase−8) and runs its clean-up; called only by DBSIM's
  `terminate()` (`0x496377`) after its catch-all.

### The 26 exit records of DBSIM (static)

They run at exit in reverse table order (all priority `0x20`), after the
atexit functions. "−n" is the construction counter `[[0x6cbee4]]` decremented
n times; *delete* is `0x477548` and *delete[]* `0x47758c` (both CW3220
`_free` in normal-memory mode, section 4.3, after `[0x6bb30c]` −= the block
header).

| # | Function | What it does |
| ---: | --- | --- |
| 0 | `0x4069b0` | −1; delete `[0x4a95a0]` |
| 1–3 | `0x40791c`, `0x4082d8`, `0x408ef0` | −1; delete `[0x4a95fe]`, `[0x4a961e]`, `[0x4a9642]` if nonzero |
| 4 | `0x4094f0` | −1 delete `[0x4a9684]`, −1 delete `[0x4a966e]` (each if nonzero) |
| 5, 6 | `0x409984`, `0x409fcc` | −1; delete `[0x4a968d]`, `[0x4a96aa]` if nonzero |
| 7 | `0x40a1a0` | −1; delete `[0x4a96c2]` |
| 8, 9 | `0x41bcc8`, `0x421ff4` | −1; delete `[0x4a9b7a]`, `[0x4a9db9]` if nonzero |
| 10 | `0x426edc` | `0x453848(0x4aa650, 2)`: −1, delete[] of the object's buffer if nonzero |
| 11 | `0x42dc40` | −6 |
| 12 | `0x4372c4` | `0x453848` on `0x4d0454` and `0x4d0444`; `0x49656c(0x4d0414, 16, 3, 1, 0x453848)`: the same for 3 elements of 16 bytes, last first |
| 13 | `0x4378c8` | `0x453848(0x4d1298, 2)` |
| 14 | `0x4404a4` | −3 |
| 15 | `0x4476f0` | −3 if byte `[0x49cc26]` |
| 16, 17 | `0x4504a4`, `0x4541a8` | `0x4815b8` on `0x4d1cf6`, `0x4d1e38`: −1, vtable `0x4a306c`, delete[] `[+0x16]` if word `[+0x14]`, delete[] `[+0xe]` if `[+0xa]`, base destructor `0x4838fd` (−1, vtable `0x4a3368`) |
| 18 | `0x45cc0c` | −1; `0x4815b8(0x4d257f, 2)` |
| 19 | `0x45e460` | if byte `[0x49f1ec]`: −2, `0x48bcc4(0x4d2754, 0)` (−1, vtable store); if byte `[0x49f032]`: `0x453848(0x4d26e0, 2)` |
| 20 | `0x4635d0` | `0x453848(0x4d2c68, 2)` |
| 21 | `0x46a5ac` | `0x4815b8(0x6b4f04, 2)` |
| 22 | `0x46b8b4` | −1; delete `[0x6b4f30]` if nonzero |
| 23 | `0x476cb0` | `0x49656c(0x6b7b70, 0x110, 52, 1, 0x476d26)`: per element, last first, −1 and 10 sub-elements of 22 bytes at `+0x34` through `0x48bcc4` |
| 24 | `0x479d7c` | `0x479c20(0x6c56c8, 2)`: −1; the log function `0x479f24` with type 1, which returns at once |
| 25 | `0x4956d4` | `0x495530(0x6cbc50, 2)`: −1; `CloseHandle` of the `DARKMONO.VXD` handle unless it is −1 |

They write the construction counter, vtable pointers inside the destroyed
statics, CW3220's heap (frees) and `0x6bb30c`; no file is written, no game
state that a tick log or `results.dat` contains is read or written, and none
calls exit, abort or `__ErrorExit` (`0x49656c`'s error branch needs a mode
other than 1, 2, 3 or 5). The frees happen after the proxy's exit record
(its atexit callback runs before the exit records), so the heap shadow does
not report them in `exit_counters`.

### The model (core/src/rtl_start.c)

`es2_rtl_runtime` holds CW3220's lifecycle variables (gui flag, matherr
pair, `_fmode` pointer, module list, SEH record address, environment and
command line, argc/argv/envp, the atexit table, the three stream hooks, the
SIGABRT entry and its extra byte). `es2_rtl_start_env` gives byte/dword
access to the module's memory (table, records, command line), `call` for
every guest function (kind, target, ESP at the callee's entry, arguments;
returns EAX and the bytes the callee removed), and the platform calls
(`fpu_reset`, `register_frame`, the nine Win32 functions above). Functions:
`es2_rtl_startup`, `es2_rtl_select_record`, `es2_rtl_command_tail`,
`es2_rtl_atexit`, `es2_rtl_exit`, `es2_rtl_quick_exit`, `es2_rtl_cexit`,
`es2_rtl_c_exit`, `es2_rtl_abort`, `es2_rtl_error_exit`,
`es2_rtl_error_message`. Every ESP above is computed relative to the ESP the
caller passes for the export's entry. The model returns after
`exit_process` (`ES2_RTL_EXITED`, `exited` set) so that tests can continue;
a real env never returns from it.

### Tests

- `//core:rtl_start_test`: 136 checks (selector order with ties, done flags,
  function-0 stop and empty list; nine command lines; GUI and console
  start-up with every ESP; atexit full and LIFO with a registration during
  exit; `_exit`, `_cexit`, `_c_exit`; exit from an init record; abort with
  default, ignore and a handler with and without the extra byte;
  `__ErrorExit`; titles, modality and an 80-character name).
- `//tools/reference-game/invitro:differential_rtl_start_test` (quick) and
  `:differential_rtl_start_full_test` (manual), see the
  [in-vitro README](../../tools/reference-game/invitro/README.md#--rtl-start-the-runtime-lifecycle):
  the original `__startup` on synthetic module tables against the model,
  every guest and Win32 call with its arguments and ESP, the x87 state and
  SEH record WinMain sees, the done flags and all runtime variables. Quick
  4,000 cases (123,980 events), full 40,000 (1,234,484 events, every path at
  least 300 times).
- Mutations: 13 entries `rtl_start_…` in `check_mutations.ps1`, all caught
  ([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).

**Branches of the CW3220 code in scope.** Every conditional branch of
`__startup`, the selector, `0x4191a4`, `0x41a848`, `_atexit`, `_abort`,
`_raise` (for signal 22), `__ErrorExit`, `__ErrorMessage` and `0x418e9c` is
taken both ways in the quick tier (the `PATHS:` line counts each scenario:
ties, priorities, done flags, function-0 stops, empty tables, stdcall and
cdecl WinMain, main, nine command-line shapes, the seven endings, abort
default/ignore/handler/extra, a full atexit table, LIFO, registrations and
exits from record functions, called and dummy stream hooks, the three
modality cases, the three title cases), except these, **PROVED** constant:

| Site | Why it never changes direction |
| --- | --- |
| `0x41a756`, `0x41a775`, `0x41a79a` | the DLL list `0x41a62c` is `xor eax, eax; ret` |
| `0x40527b` | the record pointer was stored just before (`0x41a729`) |
| `0x41a87b` (second list) | `0x423f04` is BSS with no writer (the harness checks it after every case) |
| `0x41a886` | `0x41a630` is `xor eax, eax; ret` |
| `0x41a302`, `0x41a321`, `0x41a326`, `0x41a32b` | abort always raises 22, which is in the table (index 9, not 18 or 20) |
| `0x418ef6` | byte `0x41f069` = 1, no writer (checked at harness start) |
| `0x405197` (handler, stack overflow) | `0x41f818` = 0, no writer (static; the handler's other branches: [Error and exception paths of the own runtime](#error-and-exception-paths-of-the-own-runtime)) |

### Open

- Static only: what CW3220's own exit records do at DLL detach beyond the
  record `0x405260`.
- The mapped DLL ran without its own init records (no entry point): the
  start-up path reads none of their state (the stub for every other import
  ends the run, and none fired), but CW3220's state after its own start-up
  (environment copy, heap) is not part of the comparison.

## File and console imports

The 21 file, console and clock imports of section 2 are own code in
[`core/src/rtl_io.c`](../../core/src/rtl_io.c) (header `es2/core/rtl_io.h`,
unit test `//core:rtl_io_test`). The shim accepts the modes `shadow` (the 18
file and clock imports) and `replaced` (all 21) for them
([`proxy/rtl_io_shadow.h`](../../tools/reference-game/proxy/rtl_io_shadow.h),
included by `rtl_imports.h`); the in-vitro harness compares the own code
with the original functions of the privately mapped `CW3220.DLL` over an
emulated OS (`--rtl-io`). Contracts were read from `llvm-objdump -d -M
intel` of the reference `CW3220.DLL` (link base `0x400000`; VAs below at
that base) and DBSIM.EXE.

### The five questions

**1. What DBSIM calls** (call sites from the listing; live counts
c1s/c3_09/c4_08/c3_10 with the shim armed at DBSIM's entry, so start-up
included, from es2-data `logs\check-set5-20261011T033307Z`). Every caller
reads EAX only; the arguments:

| Import | Sites (owner) | Arguments | Live calls |
| --- | --- | --- | --- |
| `_open` | `0x47311c` VOL-aware open, `0x47ae7f` read stream, `0x47aad8` read/write stream, `0x47b1b4` write stream, `0x479eb9` assert log | `(path, 0x8000)`, `(path, 0x8000)`, `(path, 0x8102, 0x180)`, `(path, 0x8301, 0x180)`, `(log path, 0x901, 0x180)`; the result is stored as a word and tested against −1 | 336/434/320/351 (mostly failed loose opens of members, [file_io.md](../reverse-engineering/file_io.md)) |
| `_close` | `0x473095`, `0x47aa95`, `0x47ae41`, `0x47b171`, `0x479f0f` | the sign-extended word handle; −1 sets stream status 3 | 19 ×4 |
| `_read` | `0x4735eb`, `0x47ac40`, `0x47afbc` | `(h, buf, n)`; −1 and short reads set the stream status | 11,342/11,680/10,112/10,554 (the archives' buffered refills, launch files) |
| `_write` | `0x47abff`, `0x47b2db`; `0x479ed8/ef7/f06` assert log | `(h, buf, n)`; −1 sets the status | 76/146/38/140 (`results.dat`, `mission.var`, prefs) |
| `_lseek` | 19 in the stream classes | `(h, pos, 0/1/2)` | 10,771/11,299/9,760/10,194 |
| `_filelength` | `0x473348` (size of a loose VOL-aware stream) | `(h)` | 0 |
| `_fopen` | 16 (file_io.md, DBSIM call sites): `drive.cfg` `rt`, `prefs.cfg` `rb`, `language.cfg` `rt`, `simvoic?.vol` `rb`, tapes `rb`/`ab`/`wb`, dmp, text grid `r` | `(path, mode)`; the FILE pointer is only passed on or tested for NULL | 3 ×4 |
| `_fclose` | 20 | `(fp)`; result unused | 2 ×4 (`drive.cfg` stays open) |
| `_fread` | 14 | `(buf, size, n, fp)`; tapes, prefs (7 bytes) | 1 ×4 |
| `_fwrite`, `_fseek`, `_ftell`, `_rewind` | 11, 1, 1, 1 | tapes and dmp only | 0 |
| `_fscanf` | `0x45f1d7`, `0x45f21e` (`drive.cfg`, `%s` twice), `0x46c916`, `0x46ca13` (text grid `%d %d %d %d`, `%d`) | | 2 ×4 |
| `_findfirst`, `_findnext` | `0x467d4c`, `0x467e5d`; `0x467e0f`, `0x467f20` (VOL registration) | `(pattern, ffblk = [ebp−0x118], 0)`, `(ffblk)`: both passes use the same block, so the second findfirst reuses CW3220's node (FindClose of the first search) | 2 and 26 ×4 |
| `_flushall` | `0x49638a` in `terminate()` | result unused | 0 |
| `_time` | `0x45cf91` (`-D` demo pick) | `(&local)`; the stored value is used | 0 |
| `_printf`, `__fputc`, `_fputs` | 51, 54, 1 (object dumps, assert notifier) | see question 5 | 0 |

With the VOL hooks of [file_io.md](../reverse-engineering/file_io.md)
replaced, `read` and `lseek` are about 17 % lower (9,458/9,882/9,289/9,653
and 8,886/9,497/8,936/9,291), because those hooks serve VOL members from a
mapping (Live, below).

ECX/EDX after these calls: a data-flow scan of the listing (every direct call
site, following both branch edges until the register is written; through up
to four levels of callers where the stream functions return straight after
the import) finds no read of ECX or EDX: only partial writes (`sete cl`,
`mov cx, word`, `mov dl, 20h`) followed by masking or by callees that read the
argument as a word (`0x45fe6c`, `0x4960ea`, `0x44a7e8`). Virtual calls of the
stream classes were not enumerated. The original's ECX/EDX are kernel32
residue; the own code returns ECX = EDX = 0.

**2. Handle and FILE consistency.** DBSIM never reads a FILE field or the
handle tables: its FILE pointers are only passed to the f-functions, its
handles only to the handle functions. The one place with inline FILE access
is the object-dump code's `putc` macro (`inc [fp+8]`, then `__fputc`), which
runs on `__streams`' thunk address (section 4.5) and is dead. CW3220 itself,
however, shares the tables between its functions: fopen opens through the
internal `_open`, exit closes every FILE (`0x407c7c`), flushall walks the FILE
array, findfirst keeps its search in a node on CW3220's heap. The own code
therefore keeps **no state of its own**: it reads and writes CW3220's
variables in place (handle flags `0x41fce4[50]`, OS handles `0x4234e8[50]`,
`__nfile`, the FILE array `0x41f830[50]`, errno `0x422248`, `_doserrno`
`0x4235d0`, the find list `0x4235cc`, the exit hooks `0x4222bc`/`0x4222c0`,
fgetc's byte `0x4235b0`) and allocates buffers and find nodes with CW3220's
own `_malloc`/`_free`. Every byte the original writes there the model writes
(the in-vitro run compares CW3220's whole DATA section and heap). So the
imports need **not** switch as one set: any mix of `original`, `shadow` and
`replaced` among them (and with CW3220's internal callers) is consistent. The
shim refuses only: `compare` for any of them (I/O cannot run twice), `shadow`
for the three console stubs, a non-reference `CW3220.DLL` (file hash at the
arming step, `cw_hash`), and an image whose export RVAs, 20 variable operands
or KERNEL32 import names differ (`cw_image`, `cw_io_export_<f>`,
`cw_io_operand`, `cw_io_imports`). The own DLL holds these tables itself
([The own cw3220.dll](#the-own-cw3220dll)).

**3. Compare semantics (shadow).** The original runs on the game's stack with
the entry registers; a recorder in CW3220's own KERNEL32 import slots
(CreateFileA, CloseHandle, ReadFile, WriteFile, SetFilePointer, GetFileSize,
GetFileType, GetFileAttributesA, GetLastError, FindFirstFileA, FindNextFileA,
FindClose, FileTimeToLocalFileTime, FileTimeToDosDateTime, GetLocalTime) logs
each call with its arguments, result, the bytes ReadFile delivered (≤ 64 KiB),
WriteFile's input, the security attributes, the find data, the FILETIMEs, the
two DOS words and the SYSTEMTIME. Before the call the tracked state is copied:
the tables above, the 64 argument bytes, the find nodes, the FILE argument's
buffer, the find block. Afterwards the model runs on that pre-state (its own
writes in an overlay, everything else from live memory) and takes every OS
result from the log (`es2_io_replay`). Compared: EAX; every OS call in order
(function and arguments; pointers to the original's own stack frame by
content); every byte the model wrote against memory after the original
(errno, the tables, the FILE fields and buffers, the bytes delivered into the
caller's buffer where the model reads them back, the fscanf targets, the find
block); every tracked byte the model did not write must be unchanged (no
unexplained write). A malloc's result is the pointer the original stored
(its CW3220 header must be the size's block word); a freed buffer is not
compared (CW3220's free writes links into it). Not compared: ECX/EDX, the
stack below the caller's ESP, the bytes of a binary read (they come from the
OS; the stream read compare of [file_io.md](../reverse-engineering/file_io.md)
checks them end to end), the caller's argument slots (CW3220 reuses them as
locals: `_open` ORs the default mode into its flags slot). Mismatches count
per import (`rtl_<key>_shadow_mismatches`), calls the model could not judge
in `rtl_<key>_shadow_skipped` (log or snapshot full, a declined format); the
first mismatch is kept.

**4. `_time`.** Its only DBSIM use is the `-D` demo pick (`time % count`),
outside every suite; no time source other than GetTickCount reaches
simulation state ([timing.md](../reverse-engineering/timing.md)). CW3220's
`_time` is `GetLocalTime` plus a local-to-UTC conversion (`0x41e50c`):
seconds since 1970 from the broken-down local time (years 70..138,
normalising seconds, minutes, hours, days and months), plus `_timezone`
(`0x423070`, 18000 = EST in the DLL's data), minus 3600 when `_daylight`
(`0x423074`) is set and the US rule of `0x41e3f8` says summer time (first
Sunday of April from 1987, last Sunday of April before, last Sunday of
October; 2:00/1:00); ≤ 0 gives −1. The own code calls the same
`GetLocalTime` and reads the same two variables, so it returns what the
original returns at the same instant; the shadow compares the value computed
from the original's own GetLocalTime record (exact), the replaced mode
cannot be compared live (wall clock).

**5. Console stubs.** `_printf` (51 sites), `__fputc` (54) and `_fputs` (1)
are reached only from the object dumps and the assert notifier's allocation
failure; none ran in any suite. The dumps' `putc` and the notifier's `fputs`
pass `__streams`-relative FILE pointers inside DBSIM's CODE (section 4.5), on
which the original would write to read-only code. Their results flow back only
as the return value of dump functions (`ret` right after the call, 14 sites);
no caller tests them. `printf` returns the length the pure-import formatter
produces (formats outside its subset, and an empty result, go to the
original), `__fputc` the character as an unsigned byte, `fputs` its last
character (0 for NULL or ""). `_printf` and `__fputc` write nothing and touch
no FILE. `_fputs` reproduces the original's one write: it reads the word at
FILE+0x12 and writes it back, so it faults on a read-only FILE where the
original faults ([Error and exception paths of the own runtime](#error-and-exception-paths-of-the-own-runtime)).
The return values equal the original's whenever its stream accepts the
output; when it does not (no console: stdout's write fails once its
512-byte buffer is full) the original returns −1 (`EOF`), the stubs still
report success.

### Contract highlights (confirmed in vitro against the DLL)

- **Errors** (`0x409158`): a code ≥ 0 (above `0x12a` taken as 1) is stored in
  `_doserrno`, errno = the signed byte of the DOS error map `0x420358`; a
  negative code −c (only −6 occurs) gives `_doserrno` −1, errno c. A Win32
  failure takes `GetLastError() & 0xffff` (`0x4091bc`).
- **`_open`** (`0x409d84`): no text/binary flag: `*_fmode & 0xc000` (DBSIM's
  `_fmode` word `0x4a7974` holds `0x4000`); still neither: text. Creation
  bits `0x700` → disposition (`0x100` OPEN_ALWAYS, `0x200`/`0x600`
  TRUNCATE_EXISTING, `0x300` CREATE_ALWAYS, `0x500`/`0x700` CREATE_NEW, else
  OPEN_EXISTING); with O_CREAT the attributes are READONLY unless
  `(mode & umask) & 0x80`, else GetFileAttributesA's (−1 → 0). Access `flags
  & 3` (3: error 1), share from `flags & 0x70` (0x10 none, 0x20 read, 0x30
  write, else both), SA {12, NULL, inherit = !(flags & 0x80)}. Failure:
  GetLastError, `0x6e` → `0x50` with O_CREAT else 2. A char device adds
  `0x2000`; the stored flags lose `0x700`. The first free entry (< `__nfile`)
  takes flags and handle; none: error 4, CloseHandle, −1.
- **`_read`** (`0x408108`): bad handle error −6; n = 0 or −1: 0; binary: one
  ReadFile (TRUE exactly = success). Text: stops at the EOF flag `0x200`; per
  round ReadFile(rest), Ctrl-Z ends the data and sets `0x200`, a CR as the
  last byte is overwritten by one more byte read, then **every** CR is removed
  (also one not before LF); a round that kept nothing reads again, a short
  round on a device returns.
- **`_write`** (`0x4082f0`): append flag `0x800` seeks to the end first;
  binary: one WriteFile; text: chunks of at most 128 expanded bytes (LF → CR
  LF, a pair never split), a short write returns the source bytes done.
- **`_lseek`** clears the EOF flag even when SetFilePointer then fails;
  whence > 2: error 1. **`_close`** needs CloseHandle to return exactly 1; the
  entry then holds 0 / −1. **`_filelength`**: GetFileSize, −1 → errno.
- **FILE** (`0x18` bytes: curp, buffer, level, bsize, istemp, flags, token =
  low word of the FILE's address, fd, hold byte). `fopen` takes the first
  entry with fd < 0 (it also tests the byte one entry past the table, inside
  `_openfd[4]`), parses the mode (`r`/`w`/`a`, `+` second or third, `t`/`b`,
  else `*_fmode`; sets the exit hook `0x4222c0`), opens, marks a device
  `0x200`, then `setvbuf(fp, NULL, terminal ? line : full, 512)` (exit hook
  `0x4222bc`, malloc 512, flag 4); `fclose` checks the token, flushes
  pending output (a failed flush returns −1 and leaves the stream open),
  frees, closes, clears flags/bsize/level and fd. `fread` (no token check)
  copies from the buffer, reads whole buffers directly, the rest through
  `fgetc` (refill; a refill at the end sets EOF `0x20`, an error `0x10`);
  `fwrite` (size 0 returns count) copies into the buffer (an empty output
  buffer has level −1−bsize), writes a request larger than the buffer
  directly (a failed write still counts as complete), and goes character by
  character through `fputc` on a line-buffered stream. `fseek` flushes,
  corrects SEEK_CUR by the buffered input (newlines counted twice in text
  mode), clears `0x20|0x80|0x100`; `ftell` adds pending output or subtracts
  buffered input (text: per newline one more), and on an append stream asks
  for the end first; `rewind` = `fseek(0, 0)` and clears `0x10`;
  `flushall` flushes and counts every stream with `flags & 3` and level ≠ 0.
- **`fscanf`** (`0x40668c`, numbers `0x406c24`): get = fgetc, unget =
  ungetc; a byte `0xff` is end of input; white space in the format skips
  input white space; `%[*][width][hlLNF]` with `d D u U` (base 10), `i I`
  (0: `0x`/`0` prefixes), `o O`, `x X`, `p` (16, at most 8 digits), `s`,
  `c`, `[...]` (`^`, ranges, `]` first), `n`; an uppercase letter stores a
  long; overflow saturates to `0x7fffffff`/`0x80000000` without the
  unget; an empty scan set match is not a failure; a letter of class 2 ends
  the scan; at end of input the result is the count, or −1 if 0. The model
  declines (the original runs) float conversions and a `%` followed by NUL,
  white space or `%` after flags (the original reads its number base from an
  uninitialised local there).
- **`findfirst`/`findnext`** (`0x4086ec`, `0x408740`, `0x408784`): a node
  (`0x14` bytes on CW3220's heap: block, handle, mask, path pointer, next)
  per find block address, found again by address (its old search closed) or
  allocated (NULL: error 8); FindFirstFileA on the stored path pointer, then
  FindNextFileA; entries with hidden/system/directory bits need them in the
  mask (a volume label only with mask 8); the block gets attributes,
  FileTimeToLocalFileTime → FileTimeToDosDateTime into time/date, size, name.
  No FindClose at the end (the handle stays open).

### Shim modes (proxy/rtl_io_shadow.h)

- **Configuration.** `<key>=shadow` for `open`, `close`, `read`, `write`,
  `lseek`, `filelength`, `fopen`, `fclose`, `fread`, `fwrite`, `fseek`,
  `ftell`, `rewind`, `fscanf`, `flushall`, `findfirst`, `findnext`, `time`;
  `<key>=replaced` for those and `printf`, `fputc`, `fputs`. The heap rule
  `shadow_set` counts only calloc/realloc/free.
- **Install** (the shim's arming): the CW3220 hash (as for the heap shadow),
  the export RVAs, 20 relocated operands naming the variables, the 15
  KERNEL32 names in CW3220's import table; a private stack of 256 KiB of its
  own (the heap shadow's detour switches to the shim's private stack while
  the own code waits for CW3220's `_malloc`); with a shadow, 2.9 MiB for
  overlay, snapshot, log and pool, and the recorder in the 15 slots
  (restored by `rtl_uninstall`).
- **replaced**: the adapter saves the entry registers, runs the own function
  on its stack and returns EAX (ECX = EDX = 0). Win32 calls and `_malloc`/
  `_free` run on the game's stack 32 bytes below the caller's ESP through
  `rtl_io_guest_call`, with the addresses CW3220's import slots held at
  install (with the heap shadow active such an allocation is an `external`
  operation there). A declined call (above) runs the export with the entry
  registers (`rtl_<key>_skipped`). Not reproduced: ECX/EDX and the stack
  below the caller's ESP (CW3220's frames, the 128-byte text-write buffer).
- **shadow**: as in question 3; the recorder logs only on the owner thread
  inside an observed call, swaps and restores the return slot of each Win32
  call and runs its copy step on the private stack.
- **Fields.** Exit record: per shadowed import `rtl_<key>_shadow_mismatches`,
  `rtl_<key>_shadow_skipped`; per replaced import `rtl_<key>_skipped`; at the
  end `rtl_io_os_calls` (recorded Win32 calls) and `rtl_io_first_mismatch`
  (`key:what@address:model=..:orig=..:args=..:eax=model,orig`; `what` =
  `os`, `os_missing`, `os_extra`, `result`, `memory`, `state`, `malloc`).
  Every tick: `rtl_io_shadow_mismatches` (the total) when a shadow is
  active. Bound (fixture, every counter at its maximum): see
  `rtl_imports_tick_bound_test`.

### Tests

- `//core:rtl_io_test`: 76 checks on a synthetic arena and a scripted OS
  (errors and the `0x6e` mapping, text reads with CR, Ctrl-Z and a trailing
  CR, text and append writes, fopen/fscanf (`%s %d %i %i %i %[a-c]%n`)/fread/
  ftell/rewind/fseek/fclose, buffered fwrite and flushall, findfirst reuse,
  time with summer time, the stubs, the replay with a planted OS argument and
  an unexplained state change).
- `//tools/reference-game/invitro:differential_rtl_io_test` (quick) and
  `:differential_rtl_io_full_test` (manual); see the
  [in-vitro README](../../tools/reference-game/invitro/README.md#--rtl-io-file-and-console-imports).
  - Full tier: 8,000 sequences, 581,423 operations on the exports and on
    the model from the same state: EAX, every OS call, CW3220's DATA
    section, heap, the test arena and the emulated OS identical (34,193
    declined: float/uninitialised-base fscanf formats, unbuffered 0-byte
    freads, printf formats outside the pure formatter's subset; 1,468 stub
    calls whose export failed). All 63 scenario counters reached (`PATHS:`).
  - Branch coverage of the original (an INT3 on each of the 315 conditional
    branches of the 50 CW3220 functions behind the set): 265 both ways, 50
    with one edge, each with its reason in `invitro/rtl_io_branch_notes.h`
    (paths no import reaches: fflush(NULL), unbuffered readable streams,
    tmpfile streams, setvbuf arguments other than fopen's, the day-of-year
    form of the summer-time test, ...).
  - Installed: 161 sequences of 120 calls through DBSIM's thunks, 9,720 in
    shadow (0 mismatches, 0 skipped) and 9,600 in replaced, EAX after every
    step and the final state identical to the run without the shim; 3,000
    stub calls in replaced; a planted model error (a wrong errno address in
    the layout) is caught once.
  - The emulated OS is a model of Win32 too (positions past the end,
    failing reads and writes, a device, a pipe, a sink); behaviour that
    differs on the real OS (sharing violations, real devices) is covered
    only by the shadow.
- Fixtures: `rtl_imports_config_test` (21 replaced, 18 shadow, the rest
  refused), `rtl_imports_refuse_test` (`cw_image` without a CW3220 image,
  `mode_reserved_printf` for a stub shadow, shadow plus replaced),
  `rtl_imports_tick_bound_test` (the I/O fields at their maximum).
- Mutations: 21 entries `rtl_io_…` in `check_mutations.ps1`, one per import,
  all caught ([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).

### Live

All hooks `replaced`, the pure imports `replaced` and the heap imports
`shadow`: (a) the 18 imports in `shadow` (es2-data
`logs\check-h6-4-shadow`) and (b) the 21 in `replaced`
(`logs\check-h6-4-replaced`) give 15 of 15 suites identical, every
`rtl_<key>_shadow_mismatches`/`_shadow_skipped` and `rtl_<key>_skipped` 0,
`rtl_io_shadow_mismatches` 0, no `rtl_refused`. Calls (c1s/c3_09/c4_08/c3_10):
`open` 336/434/320/351, `write` 76/146/38/140, `close` 19, `fopen` 3,
`fclose` 2, `fread` 1, `fscanf` 2, `findfirst` 2, `findnext` 26, `time` and
`filelength` 0; `read` and `lseek` as in question 1 with the VOL hooks. In
(b) `rtl_heap_external_ops` is 6 per suite (the own `fopen`/`findfirst`
allocating through CW3220's `_malloc`) with every heap mismatch counter 0.
The closing check `check-final` (438 hooks) runs with (b) and is identical
in all 15 suites.

### Open

- **Tapes.** No suite uses one, so the f-family has had no live traffic
  beyond the start-up's configuration reads. A playback run needs
  `-GameArguments '-p<tape>'` (or `game_arguments` per suite): it reads the
  tape header with `fopen`/`fread` (seven embedded files, restored with
  `wb` writes) and one record per frame with `fread`, and `fclose`s it at
  the end; a recording (`-r<name>`) writes the header with
  `fseek`/`ftell`/`rewind`/`fwrite` and appends every frame with `fopen`
  `ab`/`fwrite`/`fclose`; `-D` calls `_time`.

## The heap, replaced

The three heap imports in mode `replaced`:
[`proxy/rtl_heap_own.h`](../../tools/reference-game/proxy/rtl_heap_own.h)
runs an own instance of the heap model (`core/src/rtl_heap.c`) as the
allocator of DBSIM's blocks, over own reservations. The register and stack
contract below was read from `llvm-objdump -d -M intel` of DBSIM and of the
reference `CW3220.DLL` (CW3220 VAs at its link base `0x400000`).

### What it does

- **Configuration.** `calloc=replaced`, `realloc=replaced`,
  `free=replaced` in `[rtl]`: all three, all three selected, otherwise
  `heap_set`. `compare` is refused (`mode_reserved_<key>`), because a heap
  operation cannot run twice. Mixing with `shadow` is refused
  (`shadow_set`). Install refuses the same.
- **The own heap.**
  - The head block is 16 bytes of the proxy.
  - The regions are own reservations made with the original's Win32 calls,
    in the original's order: `VirtualAlloc(NULL, size rounded to the
    granularity, MEM_RESERVE, PAGE_NOACCESS)`, commit and decommit per
    4 KiB page, and release.
  - The platform word is detected as CW3220's start-up code `0x418fc1`
    does: 2 when `kernel32` exports `Borland32`, else 1 when `GetVersion`
    has bit 31 set, else 0. On Windows 10 it is 0, which gives 4 MiB
    reservations.
  - User data lives in real memory: calloc's zero fill and realloc's
    `memmove` with the original's byte counts.
  - The block layout is CW3220's, so DBSIM's wrapper `0x47747c` and
    `delete` (`0x477548`, `0x47758c`) read the same header word at `p-4`
    for the counter `0x6bb30c`.
- **CW3220's own allocations stay on CW3220's heap** in the proxy's
  replaced mode. These are the stream buffers, the start-up copies,
  operator new inside CW3220 and the file code, which calls CW3220's
  `_malloc`/`_free` directly. Nothing in CW3220 is patched. (The own
  `cw3220.dll` has one heap for both: [The own cw3220.dll](#the-own-cw3220dll).)
- **Foreign pointers.** A `free` or `realloc` of a pointer outside the own
  regions goes to the original export and is counted in
  `rtl_heap_foreign_frees` (every tick). None is expected:
  - the shim arms before DBSIM's first instruction, so every block DBSIM
    frees comes from its own `calloc` import;
  - no CW3220 function that DBSIM calls frees a block DBSIM passes in
    (`fclose` frees CW3220's own stream buffer).
- **Calls.** Calls run through the replaced adapters on the private
  stack. LastError is preserved, and nothing below the caller's ESP is
  written. A nested call while an adapter runs goes to the export and is
  counted `rtl_<key>_skipped`. It is impossible in practice: the own code
  calls only `VirtualAlloc`/`VirtualFree`.
- **Threads.** Calls from another thread take the thunk's foreign path to
  the original. A `free` of an own block from another thread would reach
  CW3220 and be fatal; `rtl_foreign_calls` is 0 in every suite.

### Registers and stack residue (static; the modelled values confirmed in vitro)

DBSIM calls the three imports at nine sites:

- `calloc` at `0x477485`, inside the wrapper `0x47747c`;
- `free` at `0x477580` and `0x4775c4` (`delete`, `delete[]`), `0x47c33d`,
  `0x47c6ad`, `0x48a1f2` and `0x48a335`;
- `realloc` at `0x474bf2` and `0x47c55f`.

A forward walk of the listing from each return address finds no read of
the bits CW3220 leaves in EAX (after `free`), ECX or EDX. The walk follows
both arms of every branch and the direct callers of the containing
function, up to six levels. At a call it walks the callee's entry for the
still-undefined bits, then takes Borland's ABI (EAX/ECX/EDX are scratch
after a call).

- At `0x47c33d`, `0x47c6ad`, `0x48a1f2` and both `realloc` sites, every
  register is written before it is read.
- The reads it flags behind the wrappers use only bits written since:
  - `setcc al; and eax, 1`;
  - `mov dx, …; push edx` (a 16-bit argument);
  - `mov ax, …; inc eax; cmp ax, …`;
  - `mov ecx, eax` with only CL/CX used afterwards.
- The other flagged reads come from function bounds that the linear
  listing misplaces (data in CODE before `0x481aea`).
- The wrapper `0x47747c` reads only EAX and the header at `p-4`.

So no DBSIM code depends on ECX/EDX, or on the stack below ESP, after these
imports. The own heap still returns what CW3220 leaves where that is cheap:

| Import | EAX/ECX/EDX after the own code |
| --- | --- |
| calloc | EAX the block. ECX 0 and EDX count·n after the zero fill (memset's registers, `0x417019`). For NULL: ECX count·n (the argument popped at `0x41700e`); EDX unchanged, which is exact for a refused size. Out of memory, the original's EDX comes from the search or the growth code and is not reproduced. |
| free | p = 0: EAX 0, ECX and EDX unchanged. A block without the in-use flag: EAX p, EDX its header, ECX unchanged. Absorbed by the free block below: EAX the start, ECX start + size, EDX the size (`0x4171c6..0x4171c8`). Otherwise EAX p, ECX the head block's former next block, EDX the head block (`0x4171d6..0x4171ed`). |
| realloc | EAX the block. ECX and EDX unchanged: the original's come from the nested free, malloc, memmove or `_expand`. |

The original's frames leave saved registers, return addresses into CW3220
and malloc's locals below the caller's ESP; the own code writes nothing
there.

### Tests

- **Fixtures.**
  - `rtl_imports_heap_test` (synthetic image, real `VirtualAlloc`) checks:
    - the own reservation and the block layout, with LastError kept;
    - both free paths and their registers;
    - the refused sizes, and realloc moved, in place, to 0 and from NULL,
      with the data kept;
    - nothing below ESP;
    - foreign pointers going to the export (counted, not skipped), and a
      busy call going to the export (skipped);
    - the fields.
  - `rtl_imports_refuse_test`: the four replaced refusals.
  - `rtl_imports_config_test`: the replaced rules.
  - `rtl_imports_tick_bound_test`: bound with both heap modes' fields, a
    tick of 122,445 bytes and an exit record of 135,572 bytes.
- **In vitro** (`--rtl-heap`, [README](../../tools/reference-game/invitro/README.md#--rtl-heap-the-heap-shadow-and-replaced)):
  - **Replaced sequences.** 16 in the quick tier, 160 in the full tier, of
    400 operations each. The same calloc/realloc/free sequence through
    DBSIM's thunks runs on the original exports and on the own heap, with
    the same emulated OS decisions and CW3220's head-block address.
    Required: equal results, OS calls, allocator variables, every committed
    byte of the window and the modelled registers, nothing below the return
    address, and CW3220's own heap untouched. Quick tier: 6,400 operations,
    883 NULL results, 6,924 OS calls, ECX/EDX compared in 4,489.
  - **Mixed sequences.** 8 in the quick tier, 80 in the full tier, of 300
    operations each. Own blocks and blocks from CW3220's `_malloc` sit side
    by side. Every free or realloc of a CW3220 block through a thunk must
    reach the original (354 in the quick tier). Both heaps stay valid,
    every live block keeps its data, and calloc blocks are zero-filled.
- **Mutations.** Four `rtl_heap_own_*` entries, all caught
  (`tools/reference-game/invitro/check_mutations.ps1`).
- **`tools/check_counters.py`** checks the `rtl_*` counters of every live
  suite: nonzero `*_skipped`/`*_bypassed`/`*_mismatches` counters and the
  counters of `RTL_ZERO` are reported; `rtl_refused` and a negative
  `rtl_heap_own_blocks` are problems, which make the live stage fail.

### Fields

- Every tick: `rtl_heap_foreign_frees`, after `rtl_foreign_calls`.
- Exit record: `rtl_<key>_skipped` per heap import, and at the end
  - `rtl_heap_own_regions`;
  - `rtl_heap_own_blocks`, a walk of the own heap at exit (-1 when its
    words do not form a valid heap);
  - `rtl_heap_own_os_calls`;
  - `rtl_heap_own_os_failed`.

### Live: the layout does not matter in the 36 suites

- **Run.** `tools\check.ps1 -Live -Overlap -RtlConfig` on the five clones,
  every hook replaced and every shimmable import except the lifecycle
  group: the 20 pure imports, the 21 file and console imports and the
  three heap imports (all `replaced`, i.e. 44 of DBSIM's 53 imports as own
  code). Evidence: es2-data `logs\check-h67`.
- **Result.** 36 of 36 suites identical to their goldens. No nonzero
  `hk_*` or `rtl_*` skipped/bypassed/mismatch counter, and no
  `rtl_refused`.
- **What it shows.** DBSIM's blocks lie in own reservations, apart from
  CW3220's internal blocks, so the relative layout differs from the
  original's, and nothing the oracle compares changes.
- **Exit records** (12 suites with one):
  - `rtl_calloc_calls` are the heap shadow run's counts (c1_01-success
    7,792, c3_09-loadout-b 15,634, c4_08-razor-plasma 8,192,
    c3_10-loadout-hercs 7,530);
  - `rtl_heap_foreign_frees`, `rtl_foreign_calls`, every heap
    `_skipped` and `rtl_heap_own_os_failed` are 0;
  - `rtl_heap_own_regions` is 2 (3 in c5_05-moon-missiles), so the own
    heap reserved two separate 4 MiB-rounded regions;
  - `rtl_heap_own_blocks` is 4,917 … 6,092 (the walk at exit is valid
    in every suite);
  - `rtl_heap_own_os_calls` is 1,810 … 2,099 (the reservations and one
    commit per 4 KiB page).

### Limits

- Not reproduced: realloc's ECX/EDX, calloc's out-of-memory EDX, and the
  stack residue (no DBSIM reader, see above).

## The lifecycle group as own code

DBSIM's start-up and exits run the own model
[`core/src/rtl_start.c`](../../core/src/rtl_start.c), extended with the
registers and frame words of the original's code, on the real stack. The
shim installs it as the **lifecycle group**
([`proxy/rtl_life.h`](../../tools/reference-game/proxy/rtl_life.h)); the
own `cw3220.dll` runs the same model
([The own cw3220.dll](#the-own-cw3220dll)). Read from `llvm-objdump -d -M
intel` of the reference `CW3220.DLL` (VAs at its link base `0x400000`) and
of `VER95\DBSIM.EXE`. **Confirmed in vitro** means the switched comparison
of the
[in-vitro README](../../tools/reference-game/invitro/README.md#--rtl-start-the-runtime-lifecycle)
found it equal to the original; **static** means read from the listing.

### What it does

- **Configuration.** `[rtl]` keys `startup`, `atexit`, `exit`, `abort` and
  `ErrorExit`, value `replaced`:
  - all five or none, and all five selected, otherwise `lifecycle_set`.
    With `imports=all` the three keys that are otherwise never shimmed
    (`startup`, `atexit`, `exit`) are selected as soon as the group is
    requested; an explicit `imports` list must name all five;
  - `compare` and `shadow` are refused (`mode_reserved_<key>`): a lifecycle
    function cannot run twice;
  - `startup`, `atexit` or `exit` outside the group stay
    `not_shimmable_<key>`; `abort` and `ErrorExit` without the group stay
    count-only ([The IAT-slot shim](#the-iat-slot-shim)).
- **Install**, in the shim's arming step at DBSIM's entry, before the first
  init record. Each refusal leaves both images unchanged:
  - `cw_hash` (the SHA-256 of the CW3220 file, as for the heap and I/O
    modes) and `cw_image`;
  - `cw_life_export_<key>`: the five exports at `0x41a6e0`, `0x419070`,
    `0x419214`, `0x419060`, `0x418fa4`;
  - `cw_life_operand`: 23 relocated operands of the code the model mirrors
    name the expected variables, the handler `0x405160`, raise's tables,
    the abort message and the EnumThreadWindows callback (list in
    `rtl_life.h`);
  - `cw_life_imports`: CW3220's nine Win32 thunks jump through the expected
    import slots, their lookup entries name the expected functions, and the
    slots are bound;
  - `cw_life_atexit`: CW3220's atexit count above 32;
  - `life_stack`: the 64 KiB lifecycle stack.

  Then DBSIM's `_atexit`, `__exit`, `_abort` and `__ErrorExit` slots point
  to the shim's thunks, counted `rtl_<key>_calls` like every shimmed import
  (other threads take the thunk's foreign path to CW3220). `__startup`'s
  slot is never patched for the group: the arming owns it and restores it
  as for every arming, and then jumps to `rtl_life_startup_entry` instead
  of CW3220's `__startup`.
- **Start-up.** `es2_rtl_startup` with E = the entry ESP and DBSIM's entry
  registers. Its C part runs on the lifecycle stack, not on the pure
  imports' private stack (their adapters run while the lifecycle waits for
  a guest function). Every guest call (init record, WinMain, atexit
  function, stream hook, SIGABRT handler) and every Win32 call goes through
  one trampoline:
  - the registers the model gives, ESP exactly where the original enters
    the callee, the arguments and frame words written by the model
    beforehand;
  - the callee returns into the trampoline (`rtl_life_return`), which hands
    every register back to the model;
  - the Win32 functions are the values of CW3220's own import slots at
    install, i.e. the functions the original calls;
  - `ExitProcess` is entered by a jump with CW3220's return address
    `0x41a8cc` in place.

  The C part calls no Win32 function on the lifecycle stack (the stack is
  committed at install), so the TEB stack bounds are not swapped.
- **The exits.**
  - `__exit`, `_abort`, `__ErrorExit`: adapters that keep the caller's
    registers and ESP, continue on the lifecycle stack below the frames
    still active (a call from inside WinMain, a record or an atexit
    function), run `es2_rtl_quick_exit`, `es2_rtl_abort` or
    `es2_rtl_error_exit` and never return. Nothing is written to the game's
    stack except the model's frame words.
  - `_atexit`: a leaf through the replaced adapter of the pure imports on
    the private stack, `es2_rtl_atexit` on the own table. EAX/ECX/EDX and
    the pushed EBP below the return address as CW3220's `0x419070` leaves
    them (EAX 0, ECX the function, EDX the old count; a full table: EAX 1,
    ECX/EDX unchanged).
  - The internal `exit` that `__startup` calls after WinMain is
    `es2_rtl_exit`. CW3220's `__startup` and `exit` never run.
- **State shared with CW3220.**
  - Written when `__startup` would write it, because CW3220's other code
    reads it: the GUI flag `0x423f30`, the matherr pair
    `0x421df8`/`0x421dfc`, the `_fmode` pointer `0x41fdb0` (open and fopen
    take their default mode through it), the module list
    `0x423f24`/`0x423f28`, the frame record's address `0x423f0c` (CW3220's
    exit record `0x405260` unlinks it at detach), the environment
    `0x423f10` and the command line `0x423f14`.
  - Read from CW3220 when exit or abort runs: the stream hooks
    `0x4222bc`/`0x4222c0`/`0x4222c4` (installed lazily by CW3220's stream
    code) and the SIGABRT entry `0x4223a4` with its byte `0x4223d9`
    (raise's reset is written back).
  - Adopted at install: CW3220's atexit table (anything registered before
    the switch) and main's argc/argv/envp (`0x423f18`…`0x423f20`).
- **CW3220's internal callers (static).** Its assert, exception and vector
  helpers, heap and argument errors and raise's default action call
  CW3220's own `__exit`, `_abort` and `__ErrorExit`, which still run. All of
  them end in CW3220's quick `__exit`, which runs no atexit function and no
  exit record, so they do not depend on which table holds the
  registrations. CW3220's normal `exit` is called only from `__startup`
  (`0x41a816`, `0x41a839`); nothing in CW3220 calls `_atexit`.
- **The proxy's exit hooks.** Live coverage (`live_install_exit`, also used
  by the entry probes) and the speed runtime (`speed_install_exit`, in
  buffered, perf and DirectDraw-observer runs) install at the first
  DirectDraw or MCI call, after the start-up. Their `live_import`
  (`live_coverage.h`) accepts the shim's thunk in DBSIM's
  `_atexit`/`__exit` slots in place of the export, so:
  - they register with the own table through the thunk, in the proxy's
    order: DBSIM's registrations, `live_coverage_on_exit`, `speed_on_exit`;
    the own exit runs them last first;
  - `__exit`: DBSIM's slot holds live coverage's `live_coverage_exit`,
    which chains to the speed runtime's `speed_exit`, then to the shim's
    thunk and the own quick exit;
  - live coverage restores its slot to the shim's thunk, and the shim
    restores the export.

  `rtl_life_order_test` and `rtl_life_quick_test` show both orders through
  the real arming path.
- **Threads.** Only the owner thread reaches the group. An `_atexit` from
  another thread would register with CW3220's table, which the own exit
  does not run; `rtl_life_cw_atexit` reports CW3220's count (expected 0).

### Registers and frame words (confirmed in vitro)

E = ESP at `__startup`'s entry (`[E]` = 0, `[E+4]` = the table); X = ESP at
an exit function's entry (`[X]` its return address, `[X+4]` its argument);
Y = ESP at `0x4191a4`'s entry; M = ESP at `__ErrorMessage`'s entry.
"Return" is the word the original leaves at the callee's ESP; the own run
has `rtl_life_return` there. WinMain's return address is `0x41a812`, the
instruction after `call [esi+18h]` at `0x41a80f` (not the `push eax` at
`0x41a815` behind it). Registers not named keep the value of the rows above
(in the start-up ESI = the table and EBP = E−4, EDI = 0 from the init
records on); in the exits they are the caller's of the export, and
EBX/ESI/EDI/EBP are preserved by every callee.

| Callee | ESP | Return | Registers at the callee's entry |
| --- | --- | --- | --- |
| GetEnvironmentStrings, GetCommandLineA | E−0x20 | `41a739`, `41a743` | EAX = ECX = E−0xc, EDX 0 (left by the frame registration `405274`), ESI the table, EBP E−4 |
| init record | E−0x20 | `41a7a0` | EAX = EBX = the function, ECX = EDX = the record (the selector's), EDI 0 |
| GetModuleHandleA | E−0x30 | `41a80e` | EAX the first byte of lpCmdLine, EBX lpCmdLine, ECX 0, EDX `0xff20` or `0xff22` (DL the name's end: blank or quote) |
| WinMain | E−0x30 | `41a812` | EAX hInstance, ECX/EDX as GetModuleHandleA left them, EBX lpCmdLine |
| main (console, dead for DBSIM) | E−0x2c | `41a835` | EAX argc, ECX argv, EDX envp |
| `exit` | X = E−0x24 + the bytes WinMain removed | `41a81b` (`41a83e`) | WinMain's (main's) registers, EAX its result |
| atexit function | Y−0xc | `4191c5` | EAX the count after the decrement, EDX the function, EBX the quick flag 0, EBP Y−4 |
| exit record | Y−0x1c | `41a84f` | EAX = EBX = the function, ECX = EDX = the record |
| exitbuf | Y−0xc | `4191d9` | EAX 0, ECX 1, EDX 0 (the exit-mode selector found nothing), EBX/ESI/EDI restored |
| exitfopen, exitopen | Y−0xc | `4191e9`, `4191ef` | as the previous hook left them |
| ExitProcess (jump) | Y−0x1c | `41a8cc` | EAX = ECX = the status, EBP Y−0x14 |
| SIGABRT handler (raise at X−8, return `419067`) | X−0x1c; X−0x20 with the extra byte | `41a361`; `41a359` | EAX the handler, EBX 9 (the signal's index), ECX = ESI = 22, EDX the extra byte |
| GetModuleFileNameA | M−0x70 | `418f05` | EAX the buffer M−0x58, ESI the text, EBP M−4; ECX/EDX the caller's (on abort's default and ignore paths EDX `0x4223cc`, the signal-table entry raise's lookup `41a0d8` matched) |
| GetVersion | M−0x6c | `418ea7` | EBX the title in the buffer; EAX/ECX/EDX not modelled (after CW3220's strrchr) |
| GetCurrentThreadId, EnumThreadWindows | M−0x74, M−0x78 | `418eca`, `418ed0` | as GetVersion; the window flag at M−0x68 |
| MessageBoxA | M−0x74 | `418f47` | as GetVersion |

Frame words:

- `__startup` writes E−4 (EBP), the SEH record E−8/E−0xc (handler
  `0x405160`, the previous FS:0) and E−0x14…E−0x1c (EBX, ESI, EDI); E−0x10
  is never written.
- `exit` and `__exit` push EBP, the status, 0 and the quick flag below X
  and call `0x4191a4` (return `41920c` or `419224` at Y = X−0x14), which
  pushes EBP and EBX. The exit records run under `0x41a848`'s frame (return
  `4191d3` at Y−0xc, then EBX, ESI, EDI); ExitProcess under `0x41a8c0`'s
  (Y−0xc the status, Y−0x10 `4191f8`, Y−0x14 EBP, Y−0x18 the status).
- `__ErrorExit` calls `__ErrorMessage` at M = X−0xc (return `418fb0`), then
  `__exit(1)` at X−0xc (return `418fb8`).
- abort with the default handler: the message at M = X−0x20 (return
  `419056`, called from `41a334`'s frame) and `__exit(3)` at X−0x20 (return
  `41905e`); after ignore or a handler: the message at M = X−0xc (from
  `41906d`'s frame) and `__exit(3)` at X−0xc.
- After a stdcall WinMain, `exit`'s frame overwrites E−0x10 and below, as
  in the original.

**Not reproduced:**

- the return address at each callee's ESP (`rtl_life_return`, except for
  ExitProcess); the word stays below the caller's ESP afterwards;
- the residue below a callee's ESP that the original's internal calls leave
  (the selector between the records, `__fpreset`/`_control87`, the frame
  registration, strrchr, raise's lookup): the own code runs them in C on the
  lifecycle stack and writes nothing there. A guest function that reads
  uninitialized stack could see a difference; the Win32 functions run at
  the original's ESP, so their own residue is of the same kind in both;
- EAX/ECX/EDX at GetVersion, GetCurrentThreadId, EnumThreadWindows and
  MessageBoxA.

### Tests

- `//core:rtl_start_test`: 163 checks (the 136 of the model, plus the
  registers, frame words and return addresses of the start-up and of the
  exits through an env with `write32` and `site`).
- Fixtures (synthetic DBSIM and CW3220 images, no game code):
  - `rtl_life_order_test`: the arming runs the own start-up; WinMain
    installs live coverage's and the speed runtime's hooks over the shim;
    the own table holds DBSIM's registration, live coverage's and the speed
    runtime's in that order; `exit(5)` runs them last first, then the exit
    record, the stream hooks and `ExitProcess(5)`;
  - `rtl_life_quick_test`: `__exit(7)` from WinMain through live coverage's
    slot, the speed runtime's chain and the shim's thunk to the own quick
    exit: no atexit function, `ExitProcess(7)`;
  - `rtl_life_fields_test`: the fields after `order`;
  - `rtl_life_refuse_test`: six CW3220 refusals with both images
    byte-identical; an intact install adopts CW3220's atexit table and
    uninstalls byte-exactly;
  - `rtl_imports_config_test`: the group's rules; `rtl_imports_refuse_test`:
    five more refusals (28 in all); `rtl_imports_tick_bound_test`: bound
    with the group's fields, a tick of 122,608 bytes and an exit record of
    136,680 bytes.
- **In vitro** (`--rtl-start`): the switched comparison, 1,500 cases in the
  quick tier (40,715 events, 25,974 frames, 274,374 stack words) and 15,000
  in the full tier (402,312 events, 259,615 frames, 2,742,444 stack words);
  the model comparison of [The runtime lifecycle](#the-runtime-lifecycle)
  (quick 4,000 cases, 123,980 events; full 40,000, 1,234,484) also checks
  the model's return addresses.
- **Mutations:** 36 `rtl_start_*` entries (13 on the model's paths, 15 more
  on `rtl_start.c`, 8 on `rtl_life.h`), all caught
  (`tools/reference-game/invitro/check_mutations.ps1`).

### Fields

- Every tick, with the group installed: `rtl_life_init_records`,
  `rtl_life_winmain`.
- Exit record: those two, then `rtl_life_atexit_registered`,
  `rtl_life_atexit_refused`, `rtl_life_atexit_pending` (still in the own
  table), `rtl_life_atexit_run`, `rtl_life_exit_records`,
  `rtl_life_stream_hooks`, `rtl_life_signals` (SIGABRT handler calls),
  `rtl_life_exit_status` (-1 until WinMain returns or an exit runs, then
  WinMain's result or the exit's status) and `rtl_life_cw_atexit` (CW3220's
  own count).
- `rtl_atexit_calls`, `rtl_exit_calls`, `rtl_abort_calls` and
  `rtl_ErrorExit_calls` as for every shimmed import.
- `rtl_life_atexit_refused` and `rtl_life_cw_atexit` are in `RTL_ZERO` of
  `tools/check_counters.py`.
- Exit records written at other times: one written by `speed_on_exit`
  (registered last, it runs first) has `rtl_life_atexit_run` 1,
  `rtl_life_atexit_pending` = registered − 1, `rtl_life_exit_records` 0,
  `rtl_life_stream_hooks` 0, `rtl_life_exit_status` = WinMain's result; one
  written at a `runner_flush` has `rtl_life_atexit_run` 0 and
  `rtl_life_exit_status` -1.

### Live

- **Run.** `tools\check.ps1 -Live -Overlap -RtlConfig` on the five clones,
  every hook replaced; the RTL config of the heap-replaced run plus the
  five lifecycle lines, so 49 of DBSIM's 53 imports are own code. Still
  CW3220: `@_CatchCleanup$qv`, `__ExceptionHandler`, `_errno` and
  `__streams`. Evidence: es2-data `logs\check-h68`.
- **Suites:** 36 of 36 identical to their goldens, including the outputs
  (`results.dat`, `mission.var` hashes) of the outcome suites. No nonzero
  `hk_*` or `rtl_*` skipped/bypassed/mismatch counter, including
  `rtl_life_atexit_refused` and `rtl_life_cw_atexit`. No `rtl_refused`.
- **Exit records** (12 suites, written at DLL detach), all equal:
  - `rtl_life_init_records` 65, `rtl_life_winmain` 1;
  - `rtl_life_atexit_registered` 3 = `rtl_atexit_calls` (DBSIM's own
    registrations; no proxy hook registered), `_atexit_run` 3,
    `_atexit_pending` 0;
  - `rtl_life_exit_records` 26, `rtl_life_stream_hooks` 3,
    `rtl_life_signals` 0, `rtl_life_exit_status` 3, `rtl_life_cw_atexit` 0;
  - `rtl_exit_calls`, `rtl_abort_calls` and `rtl_ErrorExit_calls` 0.

  Every process ended normally (the runner's `own_process_exit` is
  `exited`).
- **Exit code.** Every golden carries the original's exit state (12 suites
  exit with 3, 24 are stopped by the runner;
  [oracle_regression.md](oracle_regression.md#exit-code-of-the-own-dbsim)),
  and every check run compares it; the runs on the own runtime give exit
  codes `same` ([The own runtime as the default](#the-own-runtime-as-the-default)).

### Limits

- Not reproduced: the return-address word, the residue below ESP, and
  EAX/ECX/EDX at the later `__ErrorMessage` calls.
- An `_atexit` from another thread would go to CW3220's table.
- With the original CW3220.DLL, `@_CatchCleanup$qv` and
  `__ExceptionHandler` stay CW3220's; the start-up's SEH record keeps
  CW3220's handler `0x405160`. The own DLL has its own
  ([The own cw3220.dll](#the-own-cw3220dll)).

## The own cw3220.dll

An own `cw3220.dll` ([`rtl/`](../../rtl/src/cw3220.c), `//rtl:cw3220.dll`)
replaces CW3220.DLL in a measurement clone, so that DBSIM's process loads no
CW3220 code at all. Read from `llvm-objdump -d -M intel` of the reference
`CW3220.DLL` (VAs at its link base `0x400000`) and of `VER95\DBSIM.EXE`,
`ES.EXE`, `VSHELL.EXE`. **Confirmed in vitro** means the comparison of the
own DLL with the mapped original
([in-vitro README](../../tools/reference-game/invitro/README.md#--rtl-own-the-own-cw3220dll-against-the-original));
**static** means read from the listings.

### What the own DLL takes over from CW3220 (static)

| Dependency | Where | Own DLL |
| --- | --- | --- |
| `@_CatchCleanup$qv`, `__ExceptionHandler` | DBSIM's terminate and the handler of its 267 frame records (`49630c`) | own code in the DLL |
| `_errno`, `__streams` | link artefacts (section 4.5) | the DLL's own variables, exported as data |
| the 49 imports the shim replaces | the shim's own code on CW3220's tables, allocator, variables and import slots | own code in the DLL, on its own state |
| the shim's heap shadow (`rtl_heap_shadow.h`) | CW3220's entries and VirtualAlloc/VirtualFree slots | refused with the own DLL (`own_runtime`) |
| the shim's I/O modes (`rtl_io_shadow.h`) | CW3220's tables (`es2_io_layout_cw3220`), its malloc/free, its KERNEL32 slots | refused (`own_runtime`); the DLL holds the tables |
| the lifecycle group (`rtl_life.h`) | CW3220's variables, Win32 slots, handler `405160`, its code base | refused (`own_runtime`); the DLL runs the same model |
| early arming's snapshot (`early_arming.h`) | walks CW3220's heap when the loaded file is the reference | walks the own DLL's heap ([The own runtime as the default](#the-own-runtime-as-the-default)) |
| live coverage's `live_import` | `GetProcAddress(GetModuleHandle("cw3220.DLL"))` | unchanged: resolves to the own DLL's exports |
| CW3220's 22 init records (`41f02c`: table `423084`) | DLL attach | reproduced where the 53 functions read the state: heap `4170fc`, platform `418fbc`, handles `409014`, streams `407248`, `_tzset` `41e268`, the user handlers of `405248` (constants of the frame handler); the rest is not read by them: console handles `401f48`, the C++ static objects (iostreams, `409f98`, `40a2e4`, `417dc8`, `41871c` with `418444`'s buffer), the float-conversion hooks of the formatters (`4151b8`, `415698`), the OS version bytes `419000`, the environment copy `41a050` (only getenv; the DLL's `_tzset` reads the block itself), the clock base `41e690`, `416f78`, `4182a0`, `41a618`, `41a8d0` (empty), `41dd5c`. es2sim (not the own cw3220.dll) makes the blocks of `41a050`, `409f98` (with `40a67c`), `418444` and `417dc8` on the game's heap (own_process.md, the heap trace), for the layout only; their contents stay unread |
| CW3220's 10 exit records | DLL detach | the frame record's unlink (`405260`) reproduced; the others free the standard streams' buffers (`4072fc`), close handles (`402068`, `41dd98`), destroy the C++ objects and the locale: no file content depends on them |
| the exception context at StackBase−8 | CW3220's DLL entry `401067`–`401088` | not needed: DBSIM's entry installs its own (`496425`) before `__startup` and never reads the saved word `497061` |
| the stream hooks `exitbuf`/`exitfopen` | `407ca4`, `407c7c`, installed by setvbuf/fopen | own code (`es2_io_exit_buffers`, `es2_io_exit_files`), run by the lifecycle |
| the tables `__ctype`/tolower, DOS error map, scanner classes, `_sys_nerr` | `4217b4`, `420358`, `420244`, `41fe8c` | the DLL's own copies, built from the core functions |

**Which processes load it.** DBSIM imports 53 names, ES 28, VSHELL 47 (65
in all). The oracle starts DBSIM.EXE directly (`start-timing-mission.ps1`);
normal play starts ES.EXE, which starts VSHELL.EXE, which starts DBSIM.EXE.
No other module of the game imports cw3220 (BWCC32.DLL and SOS9503.DLL
import only system DLLs; SOSLIBS3.DLL is a 16-bit NE DLL). DBSIM loads
BWCC32 itself with `LoadLibraryA` (`0x4650e8`); the switch that refuses that
load is in [dll_imports.md](dll_imports.md#the-bwcc32-switch).

**DBSIM's frame tables (static, and in vitro).** The 267 callers of `49630c`
store context indices whose entries are destructor entries (kind 5) and one
catch (kind 3, `496385`); there is no `__except` entry (kinds 1 and 2,
whose filters the original runs for any exception code). Outside an
unwind the original handler therefore acts only for the C++ exception
`0EEFFACEh`.

### The environment copy

The init record `41a050` copies the process environment onto the heap as
it finds it: a pointer array of `(count + 1) * 4` bytes, then one
`_malloc(strlen + 1)` per string in the block's order. es2sim does the same
from its own process environment before DBSIM's entry
(`es2_sim_heap_copy_environment`, `sim/src/heap.c`; own_process.md "The heap
trace"), so the copy has the original's layout for any given environment.
What the copy holds is process input, not the game's behaviour:

- No game program sets a variable. DBSIM.EXE, VSHELL.EXE and ES.EXE import
  neither `_putenv` nor `SetEnvironmentVariableA` (static: their import
  names); ES.EXE starts the programs with `_spawnv`, which takes no
  environment argument, so DBSIM gets its launcher's environment.
- The original's 102 strings in 5648 bytes of string blocks
  (`frames_known.REFERENCE_ENVIRONMENT`, from `a1-heap-trace-20261023`) are
  the oracle machine's environment at that run, one variable fewer than the
  proxy listed after the attach.
- The own launch chain passes es2sim its environment unchanged with one
  exception: `es2launch` leaves `__COMPAT_LAYER` out of a replacement's
  environment (`launcher/src/es2launch.c`; the start scripts' `256COLOR`
  would quantise es2sim's true-colour output, and es2sim must not run under
  a layer), while DBSIM in the same chain keeps it. For es2sim the launcher
  hands the removed value on as `ES2SIM_COMPAT_LAYER=<value>` (set around
  the `CreateProcessA` only). es2sim builds the guest's environment block
  from its own (`es2_sim_guest_environment`, `sim/src/heap.c`): the
  `ES2SIM_COMPAT_LAYER` entry is dropped (DBSIM has no such variable), any
  `__COMPAT_LAYER` entry of es2sim's own is replaced, and
  `__COMPAT_LAYER=<value>` is inserted at its sorted place, in the order
  Windows keeps a block (names compared upper-cased byte by byte, a prefix
  first, a leading `=` part of the name; read on a host block of 95 entries,
  where `__COMPAT_LAYER` follows `windir`, because `_` sorts after the
  letters). The host's own `GetEnvironmentStringsA` is not changed, so
  es2sim's window and SDL run without the layer. For `256COLOR` the copy
  has the same string as DBSIM's: one 24-byte string in a 32-byte block, a
  pointer-array word more, and the blocks after it lie 36 bytes higher than
  without it. Without `ES2SIM_COMPAT_LAYER` (a launcher that had no layer, or
  es2sim started by hand) the block is copied as it is. The value is the
  launcher's own, which Windows may have rewritten on the way to it (an
  unmanifested launcher under test sees `ElevateCreateProcess`); DBSIM
  inherits the same value. `heap_test` shows the block with the layer at the
  sorted position (after `windir`, between `_Z` and `__COMPAT_LAYERX`, the
  empty hand-over and the case without one) and the copy's block sizes;
  `launch_chain_test` case J shows the value reaching es2sim and nobody else.
  The heap trace's `env_lengths` list the guest's strings. Not yet compared
  with a live DBSIM run that has the layer in its environment.

es2sim therefore does not pad its environment: a fixed size would replace
the player's environment, not reproduce DBSIM. A read past a heap block's
end (`c1_08-scan`, `c1_08-escort-route`, own_process.md "The free camera's
shape edge") follows the block layout in the original as in es2sim, so its
bytes depend on the environment in both. The frame tests compare with a
capture made under the oracle's environment, so `replay_suite.py --frames`
gives es2sim an environment of that size (synthetic `Z000`… variables);
the replay verdicts do not depend on it.

### Design

- **Core models, unchanged in behaviour.** The DLL runs
  `core/src/rtl.c`, `rtl_heap.c`, `rtl_io.c` and `rtl_start.c`, the models
  compared with the original in the sections above. Core also holds the
  static I/O state, the handle and stream start-up, `_tzset` and the two
  exit hooks (`es2_io_static_state`, `es2_io_startup`, `es2_io_tzset`,
  `es2_io_exit_buffers`, `es2_io_exit_files`), the class, error-map and
  scanner tables as functions, and the leaf functions' register residue
  (`es2_rtl_leaf_residue`).
- **Deployment by renaming.** `configure-runtime.ps1` renames CW3220.DLL to
  `CW3220-ORIGINAL.DLL` and puts the own DLL in its place. A side-by-side
  redirection does not work: Windows caches a failed manifest lookup per
  executable path and write time, so an external manifest takes effect
  only after DBSIM.EXE's metadata changes.
- **Two modes.** Own mode in DBSIM.EXE and every other process; **delegate
  mode** in ES.EXE and VSHELL.EXE
  ([The delegate mode is unused](#the-delegate-mode-is-unused)). In own
  mode a name outside DBSIM's 53 loads the original on first use (counted
  `rtl_own_delegated`; DBSIM never calls one).
- **The shim with the own DLL.** The forwarding modes and the own code of
  the pure imports (`replaced`/`compare`) and of the heap (`replaced`) work
  with either runtime. The modes that work on CW3220's internals are
  refused as `own_runtime` (the DLL does their work). `rtl_runtime_check`
  replaces the arming's reference-file check.

### The DLL (rtl/)

- **Build.** `cc_binary` with `linkshared`: exports from
  [`rtl/cw3220.def`](../../rtl/cw3220.def) (the 65 names, `_errno`,
  `__streams` and `__ctype` as DATA, and `es2_own_runtime`), image base
  `0x0f000000` without ASLR and without a time stamp. Imports: KERNEL32,
  USER32 and the Windows UCRT API sets only.
- **Calls.** Each code export jumps through a route table. Pure, heap, file
  and `_atexit` calls from the owner thread (the thread that ran DllMain:
  DBSIM's main thread) with the stack free run their C part on a 256 KiB
  private stack (the pure imports' adapter): below the caller's ESP only
  the leaf functions' pushed registers are written, and the file functions'
  Win32 calls run 32 bytes below it (as in the shim's replaced I/O mode).
  Calls from another thread or while the private stack is busy run on the
  caller's stack (`rtl_own_foreign_calls`, `rtl_own_nested_calls`).
- **Registers.** Pure: as the shim's replaced mode
  ([The pure imports as own code](#the-pure-imports-as-own-code)); an empty
  `sprintf`/`vsprintf` result: EAX 0, ECX the formatter's buffer record,
  EDX 0 where the original leaves formatter residue. calloc/free: as
  [The heap, replaced](#the-heap-replaced); realloc EAX. File functions:
  EAX (ECX/EDX 0; the original leaves call residue, which cdecl callers do
  not read). `_atexit`: as the lifecycle group.
- **State.** The I/O tables (`es2_io_layout` on the DLL's variables), the
  heap (one heap for DBSIM's blocks and the runtime's own buffers, as in
  the original), the lifecycle state and the three stream-hook words live
  in the DLL. DllMain sets them up in CW3220's order: static state, heap,
  handles and streams (stdin/stdout get their 512-byte buffers and the
  exitbuf hook), `_tzset` over `GetEnvironmentStrings`.
- **Start-up and exits.** `__startup`, `__exit`, `_abort` and `__ErrorExit`
  are the lifecycle group's model on the real stack, with the DLL's own
  trampoline, 64 KiB lifecycle stack and code base (the return-address
  words are the DLL's base plus CW3220's RVAs, never executed).
  `__startup`'s frame record gets the DLL's handler `own_startup_handler`:
  `UnhandledExceptionFilter`, as CW3220's `405160` with its default user
  handlers. DLL detach unlinks the record.
- **Exceptions.** `__ExceptionHandler` continues the search;
  `@_CatchCleanup$qv` does nothing while the context block at StackBase−8
  names no exception (DBSIM's terminate after a catch-all), otherwise it
  aborts (counted declined). Details and tests:
  [Error and exception paths of the own runtime](#error-and-exception-paths-of-the-own-runtime).

**Not reproduced:** floating-point, `%p` and `%n` formatting (DBSIM's
formats have none; the DLL writes `dest[0] = 0` and returns 0); EDX after
an empty `sprintf` result; ECX/EDX after the file functions; `tzname`;
the catch handling of a thrown C++ exception and the destructor calls of
an unwind; CW3220's other init records (table above); as in the lifecycle
group, the return-address word at a callee's ESP and the residue below it.

### Tests

- `//core:rtl_io_test` 119 checks (with the start-up with and without an
  inherited handle block, `_tzset`'s TZ grammar and
  GetTimeZoneInformation, the exit hooks).
- `//rtl:cw3220_dll_test` 1,405 checks: the export table (65 names and the
  descriptor, data names in a writable section), imports, base, the state
  DllMain set up against this process's handles, streams, heap and time
  zone, and calls of every family on and off the owner thread.
  `//rtl:cw3220_delegate_test`: a stand-in importing four names runs as
  ES.EXE, VSHELL.EXE and OTHER.EXE next to the DLL and a fake original; ES
  and VSHELL reach the original for every name and `__streams`, OTHER the
  own code and the original only for a name outside DBSIM's 53.
- **In vitro** (`--rtl-own`), quick / full tier:
  - tables: `__ctype` with the tolower table, error map, scanner classes,
    161 words of the static I/O state equal to CW3220's as linked;
  - pure: 79,901 / 798,979 cases on the owner stack and 7,989 / 79,906
    from a foreign stack; 188 / 1,843 empty results equal but EDX; 110 /
    1,115 `%p`/`%n` calls left out;
  - heap: 24 / 240 sequences, 9,600 / 96,000 operations, ECX/EDX compared
    in 6,694 / 66,007;
  - I/O: 200 / 2,000 sequences with their start-ups (TZ absent, valid and
    invalid) and exit hooks, 12,751 / 127,337 file steps, 576 / 5,662
    console stub calls; 776 / 7,804 calls of the I/O decline classes left
    out;
  - lifecycle: 1,500 / 15,000 cases, 39,253 / 405,458 events, 25,228 /
    261,127 frames, 260,691 / 2,776,851 stack words, all 30 paths;
  - exceptions: 267 frame tables, 814 context-index stores, 4,884 handler
    calls (more in
    [Error and exception paths of the own runtime](#error-and-exception-paths-of-the-own-runtime)).
- Eight manual mutations (`atexit`'s EDX, the residue registers, free's
  EDX, a `__ctype` byte, FileTimeToDosDateTime's arguments, stdout's
  buffering at start-up, `_daylight` from GetTimeZoneInformation, the exit
  hook's stream test) were each caught by the quick tier (not in
  MUTATIONS.md).
- `//tools/reference-game/proxy:rtl_imports_runtime_test`: the own DLL
  detected, the internal modes refused as `own_runtime` (`cw_hash` for
  another module), the exit fields; `rtl_imports_tick_bound_test` with the
  runtime fields: exit record 141,231 bytes.
- `//tools/reference-game/setup:configure_runtime_test` 60 checks: preview,
  apply, a new build over a deployed one, remove; changed files (either
  DLL) refused by both scripts and both directions; sources that are not
  the own DLL, `native-r111`, a name outside the installs and a clone
  without a source refused.

### Fields

The exit record ends with `rtl_runtime` (`own` or `cw3220`) once the arming
checked the runtime; for the own DLL also `rtl_runtime_original_loaded`
(`CW3220-ORIGINAL.DLL` in the process), `rtl_own_calls`,
`rtl_own_foreign_calls`, `rtl_own_nested_calls`, `rtl_own_declined`,
`rtl_own_delegated` and `rtl_own_exit_status` (-1 until WinMain returns or
an exit runs). `rtl_own_calls` is the sum of the record's `rtl_<key>_calls`
over the 20 pure, the three heap and the 21 file and console imports, plus
the `_atexit` calls (`_atexit` is not shimmed without the lifecycle group).

### Deployment

```powershell
# preview, then deploy; the original stays as CW3220-ORIGINAL.DLL
pwsh -File tools\reference-game\setup\configure-runtime.ps1 -InstallName <clone>
pwsh -File tools\reference-game\setup\configure-runtime.ps1 -InstallName <clone> -Apply
# back to the original
pwsh -File tools\reference-game\setup\configure-runtime.ps1 -InstallName <clone> -Remove
```

`-Apply` builds `//rtl:cw3220.dll` (or takes `-Source`), checks that it
exports `es2_own_runtime`, stages it, renames the original and checks its
hash, moves the DLL in and writes `oracle-runtime-manifest.json`.
`start-native.ps1` (and with it `start-timing-mission.ps1` and
`configure-media.ps1`) then checks CW3220.DLL against that manifest and the
original as `CW3220-ORIGINAL.DLL`; their records gain `runtime` and
`runtime_sha256` on such a clone only. `verify-native-rebuild.ps1` reports a
deployed clone as changed (remove first). The preview of a clone at rest
says `state: original`; after `-Remove`, `start-native.ps1 -InstallName
<clone> -Preview` shows no `runtime`.

### Live

- **Run.** The own `cw3220.dll` deployed with `configure-runtime.ps1
  -Apply` on the five check clones (the preview showed `state` original and
  the original hash `6e90e1ca…` on each), then `tools\check.ps1 -Live` with
  `[rtl] enabled=1 imports=all` (count only) and every hook replaced, then
  `-Remove` on all five (each has the original `CW3220.DLL` again, no
  `CW3220-ORIGINAL.DLL` left). Evidence: es2-data `logs\check-h66`.
- **Result: 36 of 36 identical** to their goldens, with no nonzero
  skipped/bypassed/mismatch counter and no `rtl_refused`. In every exit
  record:
  - `rtl_runtime` is `own` and `rtl_runtime_original_loaded` is 0: no
    CW3220 module in DBSIM's process;
  - `rtl_own_delegated`, `rtl_own_nested_calls`, `rtl_own_foreign_calls`
    and `rtl_own_declined` are 0;
  - `rtl_own_exit_status` is 3;
  - `rtl_own_calls` is 149,471 in c1_01-cheat-destroy and 179,773 in
    c1_01-cheat-disable-leg.

**DBSIM therefore runs every one of its 53 CW3220 imports as own code, with
the original DLL not loaded.**

### Open

- The DLL against the pure imports' model live (the 20 pure imports
  `=compare` with the own DLL deployed) has not run.
- Not reproduced: see the list above.

## The own runtime as the default

The regressions deploy the own `cw3220.dll` themselves; the after-load
snapshot walks the own DLL's heap; the exception exports and `_atexit` are
compared in vitro under a throw, an unwind and a foreign thread. Read from
`llvm-objdump -d -M intel` of the reference `CW3220.DLL` and
`VER95\DBSIM.EXE`.

### Regressions on the own runtime (check.ps1)

- `tools\check.ps1 -Live` takes `-Runtime own|original`, default **own**
  ([oracle_regression.md](oracle_regression.md#acceptance-script)).
  Before any stage, `configure-runtime.ps1`'s preview runs for every clone
  of the run; it refuses anything but a separate measurement clone with the
  expected runtime files (native-r111, a clone without
  `measurement_clone_of`, changed DLLs), and check.ps1 then refuses the call
  (exit 2). `rtl/` is part of the clean-tree check of `-Live`.
- After the prepared proxy, the live stage builds `//rtl:cw3220.dll` into
  `<evidence>\runtime` (or takes `-RuntimeSource`), deploys it on each clone
  (`configure-runtime.ps1 -Apply -Source`, checked: state `own` and the
  build's hash) and starts the runner. A `finally` looks at every clone it
  touched again and runs `-Remove` where the own runtime is deployed, also
  after a refused deployment, a runner that failed or threw, or any error of
  the stage; a restore that fails fails the stage and names the clone. The
  built DLL is deleted afterwards (its hash stays in the summary). The
  measurement clones' resting state stays the original `CW3220.DLL`; a clone
  found with the own runtime (left over by an interrupted run) is redeployed
  with the run's build and restored.
- `-Runtime original` deploys nothing (the shim's modes that work on
  CW3220's internals, the heap shadow, the I/O modes and the lifecycle
  group, need it) and refuses a clone that still has the own runtime.
- With `-Runtime own` the shim's internal modes are refused at install
  (`rtl_refused` `own_runtime`), which `check_counters.py` counts as a
  problem; the RTL config for such a run is the count-only `[rtl]
  enabled=1 imports=all`.
- The summary names the runtime: a header line (`- runtime: own cw3220.dll
  <sha256>; deployed on N of N clone(s), restored on N`), the live stage's
  details (one line per clone with the state found), `; runtime own` in its
  key numbers and `runtime` in `summary.json` (source, hash, found, deployed,
  restored, not_restored, bwcc32).

### The after-load snapshot with the own runtime

[`early_arming.h`](../../tools/reference-game/proxy/early_arming.h) takes the
heap's region table from whichever runtime DBSIM's cw3220 imports bind to: the
own DLL when `cw3220.DLL` exports `es2_own_runtime` (version 1: `heap` →
`region_count`, `region_base[]`, `region_size[]`), else the reference
CW3220.DLL (the file check of [The heap, shadow stage](#the-heap-shadow-stage),
its table at `0x423938`/`0x423738`). Both heaps keep CW3220's block layout,
so the walk and the file format are the same; flag 128 (`own_runtime_heap`)
marks the own runtime's.
[`after_load_snapshot.py`](../../tools/reference-game/scripts/after_load_snapshot.py)
names the flag, reports `heap_runtime` (`own`, `cw3220`, `none`) in
`summary`, and a comparison of heaps of different runtimes carries a note
(their unreached blocks differ: the own runtime keeps no environment copy, so
the `environment` class finds no array). es2sim's heap has CW3220's attach
blocks, its environment copy among them (own_process.md, the heap trace), so
the class finds es2sim's array; the own cw3220.dll still has none. A
flag-128 snapshot compares with a CW3220 one by the same graph pairing.

### Exceptions, `_atexit` and `@_CatchCleanup$qv` (in vitro)

Three parts of the `--rtl-own` pass
([in-vitro README](../../tools/reference-game/invitro/README.md#--rtl-own-the-own-cw3220dll-against-the-original)),
quick tier counts:

- **A C++ throw, search phase** (code `0EEFFACEh`, flags 0 and
  NONCONTINUABLE): on the 813 of DBSIM's 814 context entries whose chain has
  no catch, both `__ExceptionHandler`s continue the search and write nothing
  (1,626 calls): for kinds 0, 4 and 5 the original's search (`4043b7`) only
  steps to the previous entry. The one entry that reaches DBSIM's only catch
  (kind 3, the catch-all of terminate `0x496377`) is **not reproduced**: the
  original matches it (`4042a1`), runs the global unwind and jumps into the
  catch block; the DLL continues the search, so a throw would end at
  `__startup`'s record (`UnhandledExceptionFilter`).
- **An unwind** (flags 2, 4 and 6; a throw code only where the table's first
  word is 0), with the construction counter at StackBase−4 equal to the
  frame's entry value (nothing constructed since). The original's clean-up
  `40415d` walks the chain from the context word down to 0, writes each
  previous index into the word at record+0x10, calls the block of a kind-0
  entry through `4052a3` (EBP = the frame) and, for a kind-5 entry, the
  object walk `40487b` (destructors through `404624`/`403a7c`, `operator
  delete` `417d00`/`417d10`) unless its object table is empty, has flag 100h
  (returns the descriptor's +24h, which the next entry's count adds) or
  counts nothing to destroy (flags 20h and 3 always walk). On the 499
  entries whose chain calls nothing that way, both return
  `ExceptionContinueSearch`; the original only steps the context word to 0
  (the frame is being discarded), the DLL writes nothing (1,497 calls). The
  CW3220 helpers that would call DBSIM code (`4052a3`, `403a7c`, `403ac9`,
  `403e1c`, `405036`, `417d00`, `417d10`) start with `int3` during these
  runs, so the classification is checked: none was reached. The other 315
  entries (313 destructor entries, and two context stores that the static
  classification took for kind-0 blocks, see
  [The unwind clean-up at the process end](#the-unwind-clean-up-at-the-process-end))
  would run DBSIM clean-up code in the original; the DLL runs none: **not
  reproduced**. DBSIM throws nothing (no throw import). A crash goes
  through `__startup`'s record to `UnhandledExceptionFilter`; the unwind
  that may follow at the process end, and why skipping its clean-up is not
  observable, is in that section.
- **Foreign thread.** 700 `_atexit` registrations from a thread of its own
  (counts 0 … 32, every seventh on a full table): EAX, ECX and EDX (preset
  before the call; a full table leaves them), the count and the table equal
  to CW3220's `419070` from the same state, every call counted foreign
  (caller's stack, `rtl_own_foreign_calls`), none on the private stack.
  `@_CatchCleanup$qv` on that thread with StackBase−8 = 0 and with a context
  block whose exception list is empty: returns, writes nothing, declines
  nothing (the original reads through the empty list at `403ea6`, static).
  With an exception on the list the DLL declines and aborts (DBSIM's
  terminate aborts right after it anyway); host test in
  [Error and exception paths of the own runtime](#error-and-exception-paths-of-the-own-runtime).

### Tests

- `//tools:check_script_test`: the own runtime as the default, `-Runtime
  original`, refusals, the restore after failures, `-Bwcc32`, and the real
  `configure-runtime.ps1` on a synthetic clone with the own DLL (deployed
  while the runner runs, the original back afterwards, a clone that is not
  a separate measurement clone refused).
- `//tools/reference-game/proxy:early_arming_own_test` (mode `own`): the
  own DLL loaded by its file name, found through `es2_own_runtime`, its heap
  walked with flag 128 (blocks as `es2_heap_walk` counts them, contents of
  the blocks in use, the freed one without), and the script: the own heap
  against the same objects on a synthetic CW3220 heap identical, a changed
  byte different. `early_arming_snapshot_test` (the heap table through
  `ea_heap_from_cw`).
- `//tools/reference-game/scripts:after_load_snapshot_unit_test`: flag 128,
  `heap_runtime`, the note.
- `//tools/reference-game/invitro:differential_rtl_own_test`: the three parts
  above.
- `//tools/reference-game/proxy:bwcc32_switch_{switch,refuse,reference}_test`
  (dll_imports.md).

### Live

Every hook replaced, five clones:

- **Default runtime (own)**, count-only config `[rtl] enabled=1
  imports=all`: **40 of 40 identical**, exit codes `same`, no nonzero
  skipped/bypassed/mismatch counter; summary `runtime: own cw3220.dll
  3F76CCDF…; deployed on 5 of 5 clone(s), restored on 5`; every exit record
  has `rtl_runtime` own, `rtl_runtime_original_loaded` 0,
  `rtl_own_delegated` and `rtl_own_declined` 0; afterwards every clone's
  preview says `state` original with no `CW3220-ORIGINAL.DLL` left.
  Evidence: es2-data `logs\check-h610b`.
- **With `-Bwcc32 refuse`**: **40 of 40 identical**, all counters as above.
  BWCC32.DLL is not needed by DBSIM in any suite. Evidence: es2-data
  `logs\check-h610-bwcc`.
- **After-load snapshot, own against original** (a60 with all hooks
  replaced on `timing-r111-p1`, the own runtime deployed and removed in a
  `finally`): `summary` shows `own_runtime_heap`; `compare` against the
  same load on CW3220 gives the note "heaps of different runtimes" and
  **240 differences**: allocation slack (the last one to three payload
  bytes of blocks larger than their request: stale bytes the program never
  wrote), two block sizes in the `0x4a9706` tree (40/28 and 48/56) and the
  fixed byte `0x6bb30c`. The live regressions with the own runtime are
  identical; the runtime classes of the snapshot comparison count the
  differences ([mission_load.md](../reverse-engineering/mission_load.md#snapshot-results)).
  Evidence: es2-data `logs\snapshot-r7c-20261013`.

### Limits

- The throw's catch (terminate's catch-all) stays not reproduced; DBSIM
  throws nothing. The clean-up of an unwind (destructors) is not
  reproduced on purpose:
  [The unwind clean-up at the process end](#the-unwind-clean-up-at-the-process-end).
- `@_CatchCleanup$qv` with a thrown exception on the list (the DLL aborts;
  the original takes it off and runs its clean-up before DBSIM's abort).

## The delegate mode is unused

The delegate mode of the own `cw3220.dll` serves ES.EXE and VSHELL.EXE only,
and with the own launcher and the own shell neither program runs, so it does
not occur in a default session. No own code for the imports of ES.EXE and
VSHELL.EXE is built. The mode is chosen in `DllMain` by
`own_process_delegates()` in `rtl/src/cw3220.c` (module name `ES.EXE` or
`VSHELL.EXE`): their own Borland start-up, iostreams and the names outside
DBSIM's 53 then stay the original's, so the DLL loads `CW3220-ORIGINAL.DLL`
and binds the executable's cw3220 imports, data names included, to it.
DBSIM takes the own branch. The live counters agree: `rtl_own_delegated` is
0 in every exit record of the runs in
[The own cw3220.dll](#the-own-cw3220dll) and
[The own runtime as the default](#the-own-runtime-as-the-default).

The mode stays in the DLL, `//rtl:cw3220_delegate_test` exercises it with a
stand-in named `ES.EXE`, `VSHELL.EXE` and `OTHER.EXE`, and
`configure-runtime.ps1` records `delegate_mode` in its manifest. It is used
only if the original VSHELL is started on a clone that carries the own
runtime (`start-own-menu.ps1 -Shell vshell`), where it runs the original
runtime unchanged; not checked with the real ES.EXE and VSHELL.EXE (the
stand-in only). The comments in `rtl/include/es2/rtl/own_runtime.h` and
`rtl/src/cw3220.c` that announce own code for these imports are out of
date; they stay unchanged so that the DLL, and with it the hash recorded
for a deployed own runtime, does not change. Only an own runtime for VSHELL
would need that own code, which the own shell makes unnecessary
(`native-r111` plays VSHELL).

## Error and exception paths of the own runtime

The error and exception paths in CW3220.DLL (SHA-256 `6E90E1CA…F505`) that
DBSIM can reach, compared with the own `cw3220.dll`. The evidence is the
in-vitro cases of the sections above, all of which compare against the
privately mapped original, `//rtl:cw3220_fault_test` on the build host, or
the `llvm-objdump` listings of CW3220.DLL and DBSIM.EXE. DBSIM imports
neither `RaiseException`, `SetUnhandledExceptionFilter` nor `SetErrorMode`,
so the only exceptions it can see are CPU faults.

### Error paths per import

| Import(s) | Error path in the original | Reached by DBSIM | Own DLL | Verdict, evidence |
|---|---|---|---|---|
| 20 pure (`_mem*`, `_str*`, `_itoa`, `_atol`) | No error returns. `_itoa` with a radix outside 2..36 writes only `buf[0] = 0` (`414d58`). Bad pointers fault. | Every radix is 10, and no caller passes a bad pointer. | The model of [The pure imports as own code](#the-pure-imports-as-own-code), which includes the radix rule. A bad pointer faults inside the DLL. | Reproduced. Evidence: the pure imports' in-vitro cases (boundary cases, radices); fault dispatch from the private stack per `//rtl:cw3220_fault_test`. |
| `_sprintf`, `_vsprintf` | Formats outside the integer/string subset (floating point, `%p`, `%n`). | No: every DBSIM format is `%d`/`%Nd`/`%ld`/`%s`/`%08x` (section 3). | Declines: `dest[0] = 0`, EAX 0. | Unreachable, so no code. |
| `_calloc`, `_realloc`, `_free` | A refused reservation or commit makes the allocator return NULL. | Only when memory is exhausted. `new` (`47749c`) then returns NULL. | The heap model. GetLastError is kept. | Reproduced. Evidence: the heap's in-vitro cases (reservation refused, commit failed, NULL results). |
| `_open`, `_close`, `_read`, `_write`, `_lseek`, `_filelength` | Win32 failure → `0x409158` sets `_doserrno` and sets errno through the map `0x420358`, then returns −1. Also invalid access mode 3, bad handle, bad whence, negative seek, short write. | Yes (stream library, `assert.log`, files that are missing or busy). | The I/O model, with the same errno and `_doserrno` words. | Reproduced. Evidence: `--rtl-io` (emulated OS: faulty, full, device and pipe files, open missing/busy/exists/access 3/no handle, close bad, read error, write error/short, lseek bad whence/negative) and the same paths through the DLL in `--rtl-own`. |
| `_fopen`, `_fclose`, `_fread`, `_fwrite`, `_fseek`, `_ftell`, `_rewind`, `_fscanf`, `_flushall` | Bad mode; failed open; no free FILE; a buffer allocation that fails (`vm_limit`); the error and EOF flags of a stream; failed seek; `fclose` of a token or NULL. | Yes (configuration, tapes, text grid). | The I/O model. | Reproduced. Evidence: as above, and the branch coverage of `rtl_io_branch_notes.h`. |
| `_fclose` of a `tmpfile()` stream | Deletes the temporary file. | No: DBSIM does not import `tmpfile`. | Declines and returns −1. | Unreachable. |
| `_fread`, 0 bytes, unbuffered | Special case of the read loop. | No: DBSIM's streams are buffered; only stderr and closed entries are unbuffered. | Declines and returns 0. | Unreachable. |
| `_fscanf`, unsupported conversion | — | No: DBSIM uses `%d %d %d %d`, `%d` and `%s`, all inside `scan_supported`. | Declines and returns 0. | Unreachable. |
| `_time` with month or day 0 | — | No: GetLocalTime never returns them. | Declines and returns −1. | Unreachable. |
| `_findfirst`, `_findnext` | No match → −1, errno 2. | Yes (VOL registration). | The I/O model. | Reproduced. Evidence: `--rtl-io` ("findfirst none"); `//rtl:cw3220_dll_test` (findnext → −1, errno 2). |
| `_printf`, `__fputc` | Writes to the stream; returns EOF when the write fails. | Only from the object dumps, which never run live. DBSIM's `putc` macro increments `[fp+8]` itself before calling `__fputc`, and its FILE pointers lie in DBSIM's read-only CODE (4.5). DBSIM's own `inc` therefore faults before `__fputc` is reached. | Stubs ([File and console imports](#file-and-console-imports), question 5). | Unreachable, so no code. |
| `_fputs` | `405b7c` returns 0 for NULL or `""`. Otherwise it writes the FILE: `405aa8` → `_fputc` `40622c`. | Only from the assert notifier `479cfc` (`479d18`), when `new(8)` returns NULL (callers `46162b`, `4779e5`, `48063f`, `480b23`, `480f70`, `480fbc`). FILE = `__streams`' thunk + 0x30 = `0x4968b6`, inside CODE (characteristics `0x60000020`, not writable). The FILE's flags byte has bit 8 set, so `_fputc` is called per character. Level `0x006cc3c8` ≥ −1 and flags & 0x90 ≠ 0 lead to `or word [fp+0x12], 0x10` at `406287`: an access violation. | Before the model runs, the word at `fp+0x12` is written back unchanged, only for a non-empty string: from the owner thread by the entry `own_entry_fputs_probe` (`or word [fp+0x12], 0` on the caller's stack, before the switch to the private stack), from another thread by the stub. On DBSIM's FILE the access violation happens at the same address, with ESP on the thread's stack as in the original. A writable FILE keeps its bytes. | Reproduced: in both runtimes the fault goes through `__startup`'s record to `UnhandledExceptionFilter` (a stub that returned the last character would let DBSIM go on to `_abort`, exit 3). Confirmed on the host; the live runs below ran a DLL without the probe. Evidence: `//rtl:cw3220_fault_test`. |
| `__startup`'s frame record (`405160`) | `0EEFFACEh` → terminate (flushall, then the handler at context+8, `_CatchCleanup`, `_abort`). `C00000FD` → `__ErrorExit("Stack Overflow!")`, but only if `[0x41f818]` ≠ 0. After that come the `_signal` handlers `[0x42341c]`/`[0x423418]` (default `405154`, which returns 1). Last, `UnhandledExceptionFilter` (`[0x424308]`), whose EAX becomes the disposition. | CPU faults: yes. `0EEFFACEh`: no (nothing throws). Stack overflow branch: no, because `__stkchk` `0x41f818` starts at 0, nothing in CW3220 writes it and DBSIM does not import it. Signal handlers: their defaults only (`_signal` has no internal callers and DBSIM does not import it). | Returns `UnhandledExceptionFilter(&pointers)`. | Reproduced for every code DBSIM can raise. Evidence: static, plus `//rtl:cw3220_fault_test` and the in-vitro matrix below. |
| `__ExceptionHandler` (`4042f0`) | Acts only for `0EEFFACEh` (catch, global unwind) and during an unwind (clean-up `40415d`). | Throw: no. Unwind: at the end of an unhandled fault, when the thread start's record unwinds the chain (measured with the error box off, below); live the original ends before it. | `ExceptionContinueSearch`. | Equal in the search phase. The unwind clean-up is not reproduced; not observable ([The unwind clean-up at the process end](#the-unwind-clean-up-at-the-process-end)). Evidence: `--rtl-own`; `//rtl:cw3220_fault_test`. |
| `@_CatchCleanup$qv` (`403e91`) | Reads `[[StackBase−8]]` and then `[ebx]` (`403ea6`); with an empty list this is a fault. | No: only terminate's catch block `4963ac` calls it, and only a throw gets there. terminate itself is referenced only through the unexpected handler `4963c7` at context+0xc (`49643f`, `496446`). | Returns on an empty list. Aborts if an exception is on the list. | Unreachable, so the difference stays. Evidence: `--rtl-own` (empty list); `//rtl:cw3220_dll_test`. |
| `_abort`, `__ErrorExit`, `__exit`, `_atexit` | Abort with and without a handler; ErrorExit's message box and exit; a full atexit table returns 1. | `__ErrorExit`: vector new/delete "Illegal mode" and the pure-virtual trap. `_abort`: terminate and the assert notifier. `_atexit`: 3 registrations. | The lifecycle model. | Reproduced. Evidence: `--rtl-start` (30+ lifecycle paths, message boxes, table full); `--rtl-own` (atexit from a foreign thread). |
| `_errno`, `__streams`, `__ctype` (data) | — | DBSIM reads the thunk bytes, not the variables (4.5). | The DLL's own variables. | No error path. |

The `_fputs` write-back is in `rtl/src/cw3220.c` (addresses `405b7c`,
`405aa8`, `40622c`, `406287` in the comments): `own_entry_fputs_probe`, the
route of `_fputs`, on the owner thread's private-stack path, and `own_run`,
`OWN_IO` on the caller's-stack path. It is on the DLL's side only. The core
model `es2_io_call` does not have it, so the shim's replaced `_fputs`
(original CW3220.DLL) is the plain stub. The in-vitro console-stub
comparison is unaffected because the write-back leaves a writable FILE's
bytes as they were.

### The exception paths of the own runtime

The exception, error and exit paths of the own `cw3220.dll` for DBSIM's 53
imports, each with its test. `//rtl:cw3220_fault_test`
(`rtl/tests/cw3220_fault_fixture.c`) runs on the build host with real
dispatch: frame records on FS:0, CPU faults raised by the processor, the
DLL's import slots pointed at stubs where a branch needs it. Its `child`
mode runs the same executable as a child process per scenario and checks
the exit code and every line the child wrote. The comparison with the
original is `//tools/reference-game/invitro:differential_rtl_own_test`
(`--rtl-own`, the "Startup handler" pass and those named in
[The own cw3220.dll](#the-own-cw3220dll) and
[The own runtime as the default](#the-own-runtime-as-the-default)).

| Path | Original | Own DLL | Test, both ways where it branches |
|---|---|---|---|
| `__startup`'s record handler (`405160`) | Disposition = `UnhandledExceptionFilter`'s EAX (`[0x424308]` via `41ed9b`), after the `_signal` words `[0x42341c]`, `[0x423418]` (0 or `405154`, which returns 1); no flag test, so also during an unwind. `0EEFFACEh` and the `C00000FD` branch (`[0x41f818]` = 0) are unreachable. | `own_startup_handler`: the filter's answer. | In vitro: 540 matrix cases (nine codes, flags 0/1/2/4/6, answers 0/1/−1, `_signal` words 0 or `405154`) plus random ones: equal EAX, one filter call with the given record and context, nothing written. Host: an access violation and a divide fault (`C0000094`) in DBSIM's chain shape (two `__ExceptionHandler` records above it) through the real filter (answer 1: the outer record gets the exception after the filter; the test filter's EXECUTE_HANDLER becomes disposition 1) and through the DLL's slot at a stub (answer 1: same; answer 0: the instruction runs again, the outer record is not asked); an `RtlUnwind` through it: one filter call with `EXCEPTION_UNWINDING` and STATUS_UNWIND. |
| `__ExceptionHandler` (`4042f0`) | Search phase: acts only for `0EEFFACEh`. Unwind: clean-up `40415d`. | `ExceptionContinueSearch` always, nothing written. | Host: 42 direct calls (codes incl. `0EEFFACEh`, flags 0, 1, 2, 3, 4, 6, 10h); as a record's handler under real dispatch (the search goes on to the outer record, so the OS accepts the DLL's handlers) and under `RtlUnwind`. In vitro: over DBSIM's frame tables. |
| Detach (`405260` → `4052c1`) | `__startup`'s record off FS:0: the head replaced by its successor, deeper in the chain its predecessor patched, walk to −1. | `own_detach`: the same; stops also at 0 and does nothing without a record. | Host: `FreeLibrary` with the record at the head, deeper, absent from the chain and never set. |
| `_abort` | `raise(SIGABRT)` default: "Abnormal program termination", title the module name in a 50h-byte buffer, style `12010h` (NT), `_exit(3)`; no atexit function. | The lifecycle model. | Host child: exit 3, the box (MessageBoxA slot at a stub), no atexit function. In vitro: `--rtl-start` (handler, ignore, extra argument). |
| `__ErrorExit` | The box with the text, `_exit(1)`. | The lifecycle model. | Host child ("Illegal mode"): exit 1, the box, no atexit function. |
| `__exit` | Exit records and hooks, no atexit functions, `ExitProcess(status)`. | The lifecycle model. | Host child: exit 7, no box, no atexit function. |
| `@_CatchCleanup$qv` (`403e91`) | `[[StackBase−8]]`: the exception list; empty: reads through it (fault). | Context 0 or empty list: returns. An exception on the list: counted declined, `_abort`. | Host child with an exception on the list: exit 3, the abort box, declined 1. Empty list and no context: `//rtl:cw3220_dll_test`, `--rtl-own`. |
| `_fputs` on a read-only FILE | `or word [fp+0x12], 0x10` (`406287`) faults on the caller's stack. | The same write, value unchanged, on the caller's stack (`own_entry_fputs_probe`; other threads: the stub). | Host: on a FILE in a `PAGE_EXECUTE_READ` page the fault is at fp+0x12 with ESP on the calling thread's stack (owner thread and another thread), the outer record makes the page writable and continues, EAX is the last character and the FILE is unchanged; a writable FILE: no fault; `""` and NULL on a `PAGE_NOACCESS` FILE: no access; child: exit `C0000005` after the unwind. Caught mutation: without the probe's route, 5 checks fail (no fault, address, stack, the child's exit code and output). |
| An unhandled fault (process end) | — (Windows) | — | Host child, error box off, the top filter answering 1: the filter is asked by `__startup`'s record, by the thread start's record, and once more with STATUS_UNWIND during the thread start's unwind; exit code = exception code (`C0000094` divide, `C0000005` fputs); no atexit function. |
| `_calloc`, `_realloc` refused | NULL; `calloc` NULL: ECX = count·size, EDX unchanged; a block: ECX 0, EDX the total. GetLastError kept. | The heap model. | Host: a reservation of `7FF00000h` (1·n and 10h·n/10h), count 0, a block; `realloc` to the huge size: NULL, the block and its header intact, the region count unchanged, then a growth that keeps the bytes. In vitro: `--rtl-heap`. |
| New handler | CW3220's `operator new`/`set_new_handler` (`417d94`, `417d80`, word `0x421fb4`) are not among DBSIM's imports; DBSIM's own `new` (`47749c`) calls `_calloc(1, n)` and returns its NULL. | No new handler. | Nothing to test in the DLL; `new` failure live: [The CODE pages' protection and the `new` failure switch](#the-code-pages-protection-and-the-new-failure-switch). |

The control scenario `offstack` (no runtime call) faults with ESP in a
block outside the thread's stack: the filter is asked twice, the thread
start's unwind is refused (exit `C0000028` or `C0000029`, by where the block
lies) and no unwind call reaches `__startup`'s record. That is what a fault
on the DLL's private stack gets; `_fputs`'s probe keeps DBSIM's one
reachable fault inside the DLL off the private stack.

### The unwind clean-up at the process end

After an unhandled fault the thread start's record unwinds the chain
when the process gets that far (measured above with the error box off).
In DBSIM that unwind would call `__ExceptionHandler` for each frame record
above `__startup`'s and run that frame's clean-up; the DLL runs none.
Live, the original does not get that far ("Live" below), and none of the
oracle's outputs could see the difference either.
The static evidence below comes from the llvm-objdump listings of the
verified `CW3220.DLL` and `VER95/DBSIM.EXE`.

**What the original runs.**
- **The frame records.** DBSIM's `49630c` (EAX = the table) builds the
  record at EBP + `[table+4]`:
  - handler `4967f0` → `__ExceptionHandler`;
  - +8 the table;
  - +10h the word context index;
  - +1Ch the construction counter `[[0x6cbee4]]` at entry (StackBase−4).
    Constructors increment it and destructors decrement it.
- **`__ExceptionHandler`.** With flag 2 or 4 (`40432f`) it calls
  `40415d(record, 0)`, then returns 1 for every code except `0EEFFACEh`.
- **The clean-up `40415d`.** It walks the table chain from the context
  index down to 0 and stores each previous index into the word:
  - kind 0: the block through `4052a3`;
  - kind 4: `403e1c`;
  - kind 5: the object walk `40487b`, with destructors through
    `404624`/`403a7c` and `delete` through `403ac9` → `417d00`/`417d10`.
- **The entries.** DBSIM has 267 frame set-ups with 789 context stores
  in 262 functions. Their kind-5 entries reach 156 destructors.
- **No kind-0 block.** The two kind-0 "blocks" of the static
  classification in
  [The own runtime as the default](#the-own-runtime-as-the-default) are
  context stores at `442011`/`442019`. They lie in `441fa4`, which
  registers no record, so they are ordinary locals.
- **What the destructors reach.** Over direct calls the 156 destructors
  reach only these imports:
  - `_free` (152);
  - `_close` (FileRWStream `47a9d0`, FileRStream `47ad78`, FileWStream
    `47b09c`, VolRStream `472f64`). CW3220's `_close` (`409a74`) is
    CloseHandle plus resetting the handle table, with no buffer;
  - CloseHandle (MonoDevice `495530`);
  - the FIRE destructor `46b574`: `_lclose`, GlobalUnlock, GlobalFree,
    SetLastError;
  - `__ErrorExit`, only in the "Illegal mode" default of `49656c`, which
    is unreachable because mode 3 is constant.
- **Their indirect calls.** The StreamIO base `47a8a4` calls slot 10h of
  `4a16e8`, which is `47a904` and returns 1.
- **The one write.** BuffWStream's destructor `47c314` → `47c388` flushes
  its buffer through slot 10h (`47c444` → `47c5ec`) into the stream it
  wraps. A BuffWStream lives on a stack only in `47a1ac`, the object
  writer, at context 8 (`47a1f3`).

**Live at the known fault sites.** The direct-call ancestors of the 14
sites in own_process.md (the original's fault paths) and of `453127` and
`46162b` include no frame of a write stream: none of `47a1ac`, the
result writer `42412c`, `459838` or `459b94`. The main loop `45f160`
(table `49f273`) is an ancestor of every sim site. It holds context 8
(`45f2bb`) through the loop. Its clean-up is the local Strings
(tpid `4635e0`, destructor `453848` → `47758c`), which only frees. The
other ancestor frames hold read streams (VolRStream) and memory.

**Why it is not observable.**
- **Files.** The clean-ups free memory and close handles, which the OS
  also does at the end. No known fault site has a write-stream frame
  live.
- **Exit code.** The exit code is the exception code either way:
  `ExitProcess` from the thread start's handler, or the error report.
- **Exit record.** Buffered oracle runs write `exit_counters` only from
  the runtime exit hooks or the runner's flush. Detach skips it when
  `tick_io_ready` is set (`media_proxy.c`), and `oracle_regression.py`
  never compares the record. The live tape faults at `453127` (original
  runtime, oracle_suites.md, tape playback) ended with no exit record. Hooked
  destructors that a clean-up would reach (`stream_close`,
  `stream_destroy`, `shape_base_dtor`, …) can therefore change counters
  only in an immediate log's detach record, and only in the original.
- **es2sim.** `process_fault` ends without clean-ups, like the DLL.

**What would make it observable.**
- A fault inside `47a1ac`'s object writer, where the original would flush
  the partial buffer.
- A clean-up that faults during the unwind, which would change the exit
  code.

Neither is reached by a known site (static, direct calls; virtual calls
are not followed).

**Live.** The trace
[`proxy/fault_trace.h`](../../tools/reference-game/proxy/fault_trace.h)
records every call of `__ExceptionHandler` through DBSIM's import with its
flags and the context word before and after. With the divide fault at
`0x46c208` (`zone-zero`, original runtime, all hooks original, error box on,
Windows' default error-reporting settings): three search calls with flags 0,
each answering 1 with the context word unchanged; `UnhandledExceptionFilter`
from CW3220 +`0x51f5`, answer 1; then nothing: no call with
EXCEPTION_UNWINDING, no detach record; exit code `0xC0000094`. The process
ends before an unwind reaches DBSIM's records, so `40415d` does not run
(es2-data `logs\fault-trace-live`, `report.md`). The DLL's missing
clean-up therefore matches the original as well.

### Open

- Not testable on the host: `0EEFFACEh` through `__startup`'s record and
  `__ExceptionHandler`'s catch (nothing in DBSIM throws; the DLL reproduces
  neither), the `C00000FD` branch (`[0x41f818]` is constant 0), the
  Win32s/9x box modality (`--rtl-start` only).

## The CODE pages' protection and the `new` failure switch

Two instruments for the `_fputs` fault, **off by default**: a report of the
protection of DBSIM's CODE pages, and a switch that answers one `new`
request with NULL. The code is
[`proxy/new_fail_switch.h`](../../tools/reference-game/proxy/new_fail_switch.h);
its tests are `proxy/tests/new_fail_switch_fixture.c`.

### What is known statically

Each fact has the address, and how it was checked: the `llvm-objdump` listing
of `DBSIM.EXE` (SHA-256 `8561aa47…c4ad`) and the test
`new_fail_switch_reference_test`, which reads the executable from
`ES2_DATA_ROOT` and compares every byte below.

- **The request.** `new` is `0x47749c` (cdecl; `push ebp; mov ebp,esp; push
  ebx; push esi; mov ebx,[ebp+8]; cmp ebx,[…]`). The assert notifier, here
  called the *registration*, is `0x479cfc`: `push ebp; mov ebp,esp; push
  ebx; push 8; call new` (the call is at `0x479d02`), `pop ecx; mov ebx,eax;
  test eax,eax; jne 0x479d25`, then `push 0x4968b6; push 0x4a1508; call
  0x496844` (the `_fputs` thunk); `add esp,8; call 0x49688c` (the `_abort`
  thunk). Above `0x479d25` the node is linked into the list at `[0x6c56d8]`.
  There is no other call of `new` in the function. The NULL path is
  therefore `_fputs(string, 0x4968b6)` and `_abort`.
- **The six callers** of the registration are `0x46162b` (the mission load
  `0x461608`), `0x4779e5`, `0x48063f`, `0x480b23`, `0x480f70` and
  `0x480fbc`. Each is `push <callback>; call 0x479cfc; pop ecx`. The listing
  has exactly these six `call 0x479cfc`; the reference test scans every `E8`
  of the file-backed bytes for them and checks that `call new` occurs once in
  the registration.
- **CODE is read-only.** The section header of `CODE`: RVA `0x1000`, virtual
  size `0x96000` (150 pages), raw size `0x95c00`, characteristics
  `0x60000020` (code, execute, read; not write). The FILE `0x4968b6` (RVA
  `0x968b6`) lies in the file-backed bytes of that section. The faulting
  store is `or word [fp+0x12], 0x10` at `0x406287` of CW3220, so the access
  violation's target is `0x4968c8`.
- **What the proxy does to CODE pages.** Every writer of DBSIM's code in
  `tools/reference-game/proxy/*.h`, `media_proxy.c` and `hook/hook.c` takes
  the same shape: `VirtualProtect` to `PAGE_EXECUTE_READWRITE` with the old
  protection returned, the bytes, `FlushInstructionCache`, and
  `VirtualProtect` back to the old protection (the hook framework's
  `patch_protect`, `lc_write`/`lc_protect` of the coverage engine,
  `tick_log.h`, the switches and probes; the failure paths restore as well,
  or leave an error code and uninstall). The framework also refuses a patch
  that would cross a protection region. So the CODE pages stay
  `PAGE_EXECUTE_READ` after arming, and the original's fault stays a fault
  (measured: Live, below).

### The page report

`nf_scan_code` walks every section that is code and executable
(`IMAGE_SCN_CNT_CODE` and `IMAGE_SCN_MEM_EXECUTE`) of the loaded image, asks
`VirtualQuery` for each page and counts the pages per protection. It tells
patched from unpatched pages by comparing the loaded bytes with the
executable file on disk, mapped into memory layout by `nf_map_file`
(bounds-checked; only when the image has base `0x400000`, size `0x2d8000` and
is the running executable; the relocation-free comparison is valid because
DBSIM is not relocated). A page counts as patched when any byte of it that
the file backs differs. A writable page is listed (up to 16) with its RVA, and
the totals separate writable-patched from writable-unpatched pages. The FILE's
page and the page of the call at `0x479d02` are reported separately, each with
its protection and patched state. The record's fields: `by_protect`
(pages per protection), `writable_patched`, `writable_unpatched`,
`writable_pages` (the RVAs), `file_page.protect` and the call page's;
`"pristine":false` means the file could
not be mapped (the protection numbers are still valid, patched/unpatched are
then unknown).

### The switch

- **Variables.** `ES2_NEW_FAIL=<caller>:<n>` (case-insensitive). `<caller>`
  is the hexadecimal address of one of the six call sites (optional `0x`) or
  `any`; `<n>` is decimal 0..1000000. The n-th request from the counted
  site(s) is answered with NULL, once. `n` = 0 only counts and records.
  `ES2_NEW_FAIL_LOG=<path>` names the NDJSON file (appended, one object per
  line, CRLF); without it nothing is written. A malformed value, an unknown
  site or an executable other than `DBSIM.EXE` leaves everything as it was.
- **What it changes.** Only the rel32 of the single `call 0x47749c` at
  `0x479d02`, in memory, from `DllMain` (kernel32 only), the same way as the
  BWCC32 switch. The call enters `nf_entry`, which reads the registration's
  return address from the stack (`[esp+0x10]`: return, size, saved EBX, saved
  EBP, return into the caller), asks `nf_decide(size, return)` and either
  returns EAX 0 with the stack and all registers as `new` would leave them
  (cdecl, the caller pops the argument), or jumps to the real `new` with the
  stack untouched. A request counts only if its size is 8 and its caller
  return address is `site + 5` of one of the six sites. `new` itself and its
  other callers are not touched, and no hook of the registry covers the call
  (the reference test checks all 789 entries).
- **Checks before the write.** The image (`es2_hook_image_valid`: base
  `0x400000`, size `0x2d8000`), `new`'s first 10 bytes, the registration's
  first 6 bytes, its `call new`, `pop ecx; mov ebx,eax; test eax,eax; jne
  +0x17`, `push 0x4968b6; push`, and for each of the six sites `push` five
  bytes before, `call 0x479cfc` and `pop ecx` after. Any difference: nothing
  is written, and `nf_state` holds the reason.
- **Records.** `new_fail_installed` (caller, n, image); `new_fail_request`
  for each counted request (first 256: site, ordinal over all counted
  requests, ordinal at that site, inject); `new_fail_pages` (the page report)
  with `"when":"armed"` after the proxy's own arming (the end of
  `initialize_forwarding`: at the first MCI call, or at the mission load call
  with `[arming] early=1`) and `"when":"inject"` just before the NULL is
  returned; `new_fail_exception` for the first 8 severe exceptions after the
  injection (code, address, module, module base, offset, access type, target
  address, protection of the target page, ESP and whether it lies on the
  thread's stack, and the SEH chain:
  [The exit path under the `new` failure](#the-exit-path-under-the-new-failure-and-checkps1--newfail));
  `new_fail_summary` at process detach (not reached when the fault ends the
  process). The exception handler is added as the last vectored handler,
  only at the injection, and only records.
- **Switch off.** The environment variable is read in `DllMain`; if unset,
  nothing else happens: `nf_report_armed` and `nf_shutdown` return at once,
  no counter, signature or log of the registry changes.
- **A control run** with `ES2_NEW_FAIL=any:0` (counts only, nothing
  injected) gives the number of requests per site in a normal run.

### Tests

Six targets, `//tools/reference-game/proxy:new_fail_switch_<mode>_test`.
They run on a synthetic image shaped like DBSIM (the same addresses; `new`,
the registration with recorders for `_fputs` and `_abort`, six callers, a
150-page CODE section with `PAGE_EXECUTE_READ`); no original code runs.

| Mode | Checks |
|---|---|
| `parse` | `46162b:1`, `0X480FBC:25`, `ANY:3`, `any:0`, the limit 1000000; refused: 1000001, no n, empty site, non-hex, zero, a sign, 9 digits; the environment (unset, malformed, set). |
| `switch` | Only the rel32 changes and its page stays `PAGE_EXECUTE_READ`; count mode passes 12 requests over six sites with EBX, ESI, EDI, EBP and ESP intact; `any:5` answers NULL to the fifth request (site `0x480f70`), runs `_fputs(string, FILE)` and `_abort`, and later requests pass; `480b23:2` counts only its own site; other sizes and unknown callers are not counted; a second install is refused; the log lines. |
| `refuse` | A changed byte in `new`, the registration (each part), the call, the `jne`, the FILE operand, and each of the six sites (opcode, target, `pop`, `push`); a wrong base, a wrong image size, a site that is not a caller, n above the limit: the image is byte-identical afterwards. |
| `pages` | A switch patch and a framework hook (`hook/hook.c`) leave the two patched pages at `PAGE_EXECUTE_READ`: 2 patched, 148 unpatched, 0 writable; the report line. **Positive controls:** a patch API that skips the restoration, and a writable page that nobody patched, are both reported as writable. `nf_map_file` rejects a wrong image size, raw data beyond the file, a section beyond the image and a short file. |
| `exception` | After the injection, a real write to the read-only FILE page faults; the record has code `0xc0000005`, access 1, target FILE+0x12, protection `0x20`, ESP on the thread's stack, and the exception goes on (a test handler resumes it). Before the injection no handler is registered; a non-severe code is not recorded. |
| `reference` | The reference file in `ES2_DATA_ROOT` (SKIP without it): signatures, the six sites and `call new` unique in the registration, CODE `0x60000020` containing the FILE, no registry hook prologue at the call. |

Caught mutations: (1) the ordinal test shifted by one, (2) `nf_entry`
reading the return address at `[esp+0x14]`, (3) the writable mask without
`PAGE_EXECUTE_READWRITE`, (4) the signature check skipped. Each failed at
least one target (1, 2: `switch` and `exception`; 3: `pages`; 4:
`refuse`).

### Live

Clone `timing-r111-p2`, suite `acceptance-60s` via
`run-oracle-parallel.ps1`, every hook original, `ES2_NEW_FAIL=46162b:1`;
the own `cw3220.dll` (deployed with `configure-runtime.ps1 -Apply`) and the
original (`-Remove`). No control run with `any:0`. Evidence: es2-data
`logs\t-71b-live-20261023` (`summary.md`, `new_fail-own.ndjson`,
`new_fail-original.ndjson`).

- **Both runs:** result DIFFERENT, exit code 0xC0000005: the expected
  crash, not `_abort`'s 3.
- **Pages, "armed" and "inject", both runtimes:** all 150 CODE pages have
  protect 0x20, and 2 of them are patched; no page is writable; the FILE
  page (rva 0x96000) and the call page (rva 0x79000) are 0x20. The proxy's
  patching leaves CODE read-only.
- **Original CW3220:** request at site `0x46162b`, ordinal 1, inject 1.
  Then `new_fail_exception` c0000005, a write to `0x4968c8` (protect 0x20)
  at `CW3220.DLL`+0x6287, with ESP on the thread's stack.
- **Own `cw3220.dll`:** the same request and the same write fault on
  `0x4968c8`, at `cw3220.DLL`+0x24ea: the instruction there is `movw %cx,
  0x12(%eax)` in `_own_run` (the `OWN_IO` `fputs` stub reads the FILE word
  +0x12 and writes it back); ESP `0x024bffc8`, not on the thread's stack
  (that DLL build had no probe, see
  [Error and exception paths of the own runtime](#error-and-exception-paths-of-the-own-runtime)).
  The process ends with the same code.

### Limits

- The patched/unpatched split compares with the file on disk and assumes the
  executable is not relocated (DBSIM is mapped at `0x400000`); under
  another base the switch is not installed anyway.
- The vectored handler only records. A second injection run (for example
  `n` = 2 after a first NULL has been survived) is pointless: the original
  aborts at the first one.
- The switch only covers the registration's own request. Every other caller
  of `new` and the allocators below it stay as they are.

## The exit path under the `new` failure, and `check.ps1 -NewFail`

After the `_fputs` fault, both runtimes exit through `__startup`'s exception
record and `UnhandledExceptionFilter` (Live, below). Two instruments, both
**off by default**: a probe on that path, and the `check.ps1` parameter that
sets the switch's variables. Code:
[`proxy/new_fail_switch.h`](../../tools/reference-game/proxy/new_fail_switch.h)
(tests `proxy/tests/new_fail_switch_fixture.c`, modes `seh`, `suite`,
`import`, `probe`, `own`) and [`tools/check.ps1`](../../tools/check.ps1)
(tests in `tools/test_check.py`).

### (b) The exit-path probe

**What is recorded, and what is not changed.** Only when `ES2_NEW_FAIL` is
set, and only at the moment of the injection (just before the NULL is
returned), the switch looks at `CW3220.DLL` in the process (the original and
the own runtime are both deployed under that name). It walks the module's
import table with bounds checks, looks for kernel32's
`UnhandledExceptionFilter` by name, and compares the slot's content with the
address the real kernel32 exports. Only if they are equal does it replace
the slot with a thunk that records and then forwards the call (stdcall, one
argument, same argument, same result). The slot is a data word in the IAT
(RVA `0x24308` in the original; found by name in the own DLL, where the RVA
differs: `0x19060` in the build of the live run); its page is made writable
for the single store and put back. With `ES2_NEW_FAIL` unset nothing is read
or written, and the unit tests assert that no `new_fail_exit_probe` record
exists before an injection. A module, import or slot content that does not
match leaves everything untouched, and the record says why (`state`).

**Records** (NDJSON, in the file of `ES2_NEW_FAIL_LOG`; the contract is the
header comment of `new_fail_switch.h`):

| event | when | content |
| --- | --- | --- |
| `new_fail_exit_probe` | once, at the injection | `state`: `armed`, `no_module`, `no_import`, `slot_mismatch`, `write_refused` or `bad_image`; `module`, `module_base`, `size_of_image`, `timestamp`, `slot_rva`, `slot_was`, `kernel32_export` |
| `new_fail_exit_filter` | each call through the patched slot, the first 4 | `return`, `caller_module`, `caller_offset` (who calls the filter: `__startup`'s record in the original, `own_startup_handler` in the own runtime); `pointers`, `code`, `address`, `fault_module`, `fault_offset` of the exception record the caller passes; `context_esp` and `context_esp_on_thread_stack`; `sp`, `sp_on_thread_stack`, `stack_limit`, `stack_base`, `sp_region`, `sp_region_size` of the thunk; the SEH chain (`seh_count`, `seh_end`, `seh`) |
| `new_fail_exit_filter_result` | when the real filter returns | `call`, `result` |
| `new_fail_exception` | the first 8 severe exceptions | also `seh_count`, `seh_end` (`terminated`, `bad`, `backwards`, `truncated`) and `seh`: up to 8 records of the chain at that moment, each `{frame, handler, module, offset, on_thread_stack}` |
| `new_fail_summary` | process detach | adds `exit_probe`, `exit_filter_calls`, `exit_filter_returns` (not reached when the fault ends the process) |

**How to read a result.**
- `new_fail_exit_probe` `armed`, then at least one `new_fail_exit_filter`:
  the fault reached `UnhandledExceptionFilter` through the import. Compare
  `caller_module` (`CW3220.DLL` in both) and `caller_offset` (for the
  original: the handler of `__startup`'s record, CW3220 `0x405160`, calls
  through this import; for the own runtime the caller is
  `own_startup_handler`). Compare `code` and `fault_module`/`fault_offset` of
  the exception record with the `new_fail_exception` record before it, and
  the stack the call runs on (`sp_on_thread_stack`, `sp_region`).
- `armed` and **no** `new_fail_exit_filter`: the fault did not reach the
  filter through the slot of this DLL. The probe cannot say where it went;
  the `seh`/`seh_end` fields of `new_fail_exception` show the chain valid at
  the fault, and whether the walk left the thread's stack (`bad`,
  `backwards`), which is what to expect if a private stack (`VirtualAlloc`,
  outside the TEB bounds) hides the frame records.
- `no_import` / `slot_mismatch`: the import was not found by name or holds
  something else (a hook of another tool on the filter, for example).

**Evidence of the building blocks** (unit and reference tests, no game):
- `new_fail_switch_reference_test` (reads `ES2_DATA_ROOT`): in the original
  `CW3220.DLL` the walk finds `KERNEL32.DLL` / `UnhandledExceptionFilter`
  with the slot at RVA `0x24308`, `SizeOfImage` `0x2e000`, image base
  `0x400000` (so the slot address `[0x424308]` is confirmed from the file,
  not only from the listing).
- `new_fail_switch_own_test` runs the same walk on `//rtl:cw3220.dll`: the
  import is found by name in the own DLL.
- `import` (synthetic PE images: case-insensitive DLL name, exact function
  name, ordinal entries skipped, truncated tables, RVAs outside the image),
  `probe` (arming in a process, stdcall balance of the thunk, forwarding,
  records, the first 4 calls recorded, signature mismatch, missing import,
  unreadable slot page, the image left unchanged on every refusal),
  `seh` (the chain walk: terminated, bad, backwards, truncated) and `suite`
  (the `{suite}` expansion below).
- Caught mutation: the signature check (slot must equal kernel32's export)
  removed makes `new_fail_switch_probe_test` fail.

### (a) `check.ps1 -NewFail <spec>`

`-NewFail 46162b:1` (or `any:0`, `0x4779e5:3`; a hexadecimal call site or
`any`, then `:` and a decimal 0..1000000; case-insensitive) sets for the
oracle runs the script starts:

- `ES2_NEW_FAIL=<spec>`;
- `ES2_NEW_FAIL_LOG=<EvidenceRoot>\new-fail-{suite}.ndjson`.

The proxy expands `{suite}` itself: the stem of `[trace] tick_log=` in the
INI beside the proxy (`...\<suite>\run\<suite>.jsonl`, written by
`configure-tick-log.ps1` per clone), reduced to `[A-Za-z0-9_-]`, 64
characters at most, `unknown` without. One variable thus gives each suite of
a parallel run its own file; the runner and `media_proxy.c` are not
involved. Both variables are restored afterwards, whatever happened. A
value already in the environment is replaced for the run. Without the
parameter the script touches neither variable (an inherited `ES2_NEW_FAIL`
reaches the runner and is noted in the live details). The summary names the
spec and the log pattern (the `- new fail:` line, the live details, the
`new_fail` field of `summary.json`) and lists the files that arrived (`new
fail logs:`).

Refused (exit 2, before anything is created): a bad spec; no `-Live`;
`-OwnShell` or `-OwnSim` (their chains, with the own-sim replays and frame
comparisons, would run under the injection and their verdicts would mean
nothing). With the injection the suites are expected to end in DBSIM's
fault, so the live stage reports DIFFERENT and the script exits 1: the
result is the NDJSON evidence, not a regression verdict.

**Warning for a site in a replaced function.** The switch counts a request
only when its return address is `site + 5`, i.e. when the original's
machine code at the site makes it. When the function containing the site
is replaced by a hook, the own code runs instead and nothing is injected
(measured: Live, below). `check.ps1` maps the site to its containing
function (`46162b` → `0x461608`, `4779e5` → `0x4779d5`, `48063f` →
`0x4805e0`, `480b23` → `0x480af0`, `480f70` → `0x480f58`, `480fbc` →
`0x480fa4`, the function index in `docs/functions`), looks for a hook at
that entry in `proxy/hook_registry.h` and, when its mode in this run is
`replaced` (the default, or `-HookModes`), warns: on the console
(`check.ps1: warning: ...`), in the live details (`WARNING: ...`), on the
`- new fail:` line and in `new_fail.warning` of `summary.json` (empty
without a warning). Of the six functions only `0x461608` (`mission_root`)
is hooked. A site that is none of the six also warns (the switch leaves
everything as it was). `any:<n>` gets no warning. The run goes ahead
either way. For `46162b` the remedy is a `-HookModes` file with
`mission_root=original`.

**Suite selection.** `-Suite <name[,name...]>` runs only the named suites
of the registry ([oracle_regression.md](oracle_regression.md#acceptance-script));
with the injection a single short suite (`acceptance-60s`) is enough. A
run per runtime and spec:

```powershell
& tools\check.ps1 -Live -SkipQuick -SkipSignatures -SkipAudit -InstallName <clone> `
    -Runtime <own|original> -NewFail <spec> -EvidenceRoot <fresh directory>
```

`<clone>` is a measurement clone, never `native-r111`; `check.ps1 -Live`
refuses uncommitted changes under the stamp paths. The file to read is
`new-fail-acceptance-60s.ndjson`.

Tests: `test_new_fail_switch`, `test_new_fail_replaced_site` and
`test_new_fail_refusals` in `tools/test_check.py` (the mock runner prints
the variables it sees; the fake Python of the evidence stage prints them
after the live stage, which proves the restore). Caught mutation: the
restore of `ES2_NEW_FAIL` in the `finally` removed makes
`//tools:check_script_test` fail (the evidence stage saw the injected value
instead of the inherited one).

### Live

Clone `timing-r111-p1`, six runs, evidence es2-data
`logs\t-71c-live-20261023` (`summary.md` and `new-fail-*.ndjson` each). The
own `cw3220.dll` of these runs wrote `_fputs`'s word back on its private
stack (no probe yet).

| Run | Hooks | Live stage | Injected |
|---|---|---|---|
| own / original `any:0` | 789 replaced | identical 40 of 40, exit 0 | no (controls) |
| own / original `46162b:1` | 789 replaced | identical 40 of 40, exit 0 | **no** |
| own / original `46162b:1` | 789 original (`-HookModes`) | DIFFERENT 40, exit code DIFFERENT 31 | yes |

**With all hooks replaced, nothing is injected.** The default hook set
replaces the callers of `new`. The own code calls `new` from `0x480f70`,
so the site `0x46162b` is never reached. `-NewFail <site>` therefore needs
the hook that contains the site set to `original`.

With the hooks original, both runtimes take the same path in
`new-fail-acceptance-60s.ndjson`:

| Event | Own `cw3220.dll` | Original `CW3220.DLL` |
|---|---|---|
| `new_fail_request` | inject 1 | inject 1 |
| `new_fail_exit_probe` | `armed`, slot_rva `0x19060` | `armed`, slot_rva `0x24308` |
| `new_fail_exception` | `0xc0000005` at +`0x24ea`, target `0x4968c8`; esp `0x8dffc8`, **not on the thread stack** | `0xc0000005` at +`0x6287`, target `0x4968c8`; esp `0x19fb1c`, on the thread stack |
| `new_fail_exit_filter` | from CW3220 +`0x2c2b`, code and address equal the exception's | from CW3220 +`0x51f5`, code and address equal the exception's |
| `new_fail_exit_filter_result` | 1 | 1 |

- Both runtimes have the same SEH chain: DBSIM `0x4967f0` twice, the
  CW3220 handler, ntdll twice.
- Every suite that reports an exit code ends with `0xC0000005` (31 of
  40).
- Both runtimes exit through the top-level filter. The one measured
  difference is the stack the fault runs on (that build's private stack);
  it is no difference in the exit path.

**With `_fputs`'s probe** (the own DLL writes the word back on the caller's
stack; `46162b:1`, 789 hooks original, five clones, evidence es2-data
`logs\t-71c-live-20261024`, `own-…` and `original-…`):

| Event | Own `cw3220.dll` | Original `CW3220.DLL` |
|---|---|---|
| `new_fail_exception` | `0xc0000005` at +`0x289f`, target `0x4968c8`; esp `0x19fb64`, on the thread stack | `0xc0000005` at +`0x6287`, target `0x4968c8`; esp `0x19fb1c`, on the thread stack |
| `new_fail_exit_filter` | from +`0x2c8b`, SEH chain of 6, `terminated` | from +`0x51f5`, SEH chain of 6, `terminated` |

- Both live stages: DIFFERENT 40 of 40 against the goldens (the injected
  fault), exit code `0xC0000005` in the same 31 suites; the other 9 write no
  run summary in either.
- The 9 output files the runs wrote are byte-identical between the two
  runtimes.
- The fault now runs on the thread's stack in both; the ESPs differ by
  0x48 (the two `_fputs` implementations' frames; not analysed further).

### Limits

- What kernel32's filter does after it returns 1 is seen only through the
  exit code.
- `-NewFail <site>` injects only where the original code runs. With the
  default hook set the site `0x46162b` is never reached; the run needs
  `-HookModes` with the containing hook original. The warning looks only at
  the hook of the containing function, not at replaced callers further up
  whose own code might not call the original function at all.
- The thunk replaces the slot of `CW3220.DLL` only. A caller that reaches
  `UnhandledExceptionFilter` through another import is not seen.

## Evidence

The inventory's evidence is es2-data `logs\d-14-20261010\`: `dbsim.lst`,
`cw3220.lst` (llvm-objdump listings), `dbsim_p.txt`, `cw3220_p.txt`
(headers and import/export tables), `cw_sites.json` (sites per import),
`owner_categories.json`/`.md` (owner, category, reason, map counts, sites),
`per_import_summary.md` (section 2 counts), `rtl_callers.json` (callers of
the runtime pieces and the allocator), `vtables.json` (196 RTTI-named
vtables), `stages.json` (31 stage registrations),
`rtl_imports_table.txt`/`rtl_imports.json` (script output), `tools\`
(scratch walk and classification scripts).

```powershell
& <es2-dependencies>\python\python.exe -I tools\reference-game\scripts\rtl_imports.py `
  build\extracted\cd\VER95\DBSIM.EXE --check docs\reference-game\rtl_imports.md
```

