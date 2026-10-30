# The own `dbsim.exe`: the `es2sim` host

`es2sim.exe` runs our own code for DBSIM without any original instruction
executing: the guest address space, the heap, the dispatch from original
addresses to own code, the stack residue that callees read, virtual time, the
input journal, the log writers and the directory `sim/` with its targets.
Sections 1 to 13 are the host's design; the sections after them are the
contracts of the pieces built on it. The component README is
[sim/README.md](../../sim/README.md).

**Sources.** The reference `VER95/DBSIM.EXE` (724,512 bytes, SHA-256
`8561aa47…c4ad`, [reference_version.md](../reference_version.md)) and its
`llvm-objdump -d -M intel` listing; the registry
`tools/reference-game/proxy/hook_registry.h`; the DBSIM map
`<es2-data>\logs\dbsim-map-20261012\map.json` (40 suites, armed at the
mission-load call, every hook original; [dbsim_map.md](dbsim_map.md));
[dbsim_gap.md](dbsim_gap.md), [rtl_imports.md](rtl_imports.md) (the heap, the runtime lifecycle),
[hook_frontier.md](hook_frontier.md#bridged-call-inventory),
[tick_log.md](tick_log.md), [input_journal.md](input_journal.md),
[oracle_regression.md](oracle_regression.md), the subsystem documents in
`docs/reverse-engineering/` and the in-vitro
[README](../../tools/reference-game/invitro/README.md).

**Labels.** **Confirmed** = read from the bytes of the reference file or from
a cited document that confirmed it; **static** = read from the listing, not
run; **confirmed in vitro** = the original's machine code ran in the in-vitro
harness and the own code gave the same result (not checked live unless said);
**in-process** = checked by a `//sim` test on the real image (that confirms
our host, not the original); **live** = measured on the original under the
proxy; **decision** = a design choice of this document with its reason;
**hypothesis** = not yet confirmed; **open** = section 13 or the topic's open
points.

## 1. What the host provides, and what it reuses

| Seam | Existing code reused | In the host |
|---|---|---|
| Guest image | `invitro/startup_reservation.h` (claim `0x400000` before the CRT), harness link options (base `0x10000000`, no dynamic base, 16 MiB stack) | built from the own tables ([es2sim without DBSIM.EXE](#es2sim-without-dbsimexe)); CODE never executable |
| Guest memory view | `es2_guest_memory` ([guest.h](../../core/include/es2/core/guest.h)), taken by 99 of the 112 `core/src/*.c` files; the adapters' identity view `math_native_memory` | identity view for the adapters; the same struct for new `sim/` code (section 2.4) |
| Own code per original function | the adapters (`proxy/*_hooks.h`) through `//tools/reference-game/proxy:hook_adapters` and the generated include list (`hook_groups.py`), as the harness links them | dispatch entries of kind *hook* call the adapter's replacement (section 4) |
| Calling conventions | `//hook`: `es2_hook_extension` (ABI cdecl/stdcall/Borland, result EAX/pair/ST0, `replacement_context` with entry EAX/EDX/ECX) | the dispatcher honours the same contract |
| Heap | `core/src/rtl_heap.c` (the CW3220 heap model), `proxy/rtl_heap_own.h` (the allocator over own reservations) | `//sim:heap` over the same model (section 3) |
| Runtime start and exit | `core/src/rtl_start.c`, the own `rtl/cw3220.c` imports | the CW3220 import services (section 6) |
| Virtual time | `proxy/virtual_time.h` (contract in [timing.md](../reverse-engineering/timing.md#virtual-time)) | `//sim/oracle:virtual_time` (section 7) |
| Input journal | `proxy/input_journal.h` (format and pump keys in [input_journal.md](input_journal.md)) | `//sim/oracle:journal` (section 8) |
| Tick log, watches, snapshot | `proxy/tick_log.h`, `mem_watch.h`, `early_arming.h` (ES2SNAP1 v2) | `//sim/oracle:tick_record`, `:mem_watch`, `:snapshot` (section 9) |
| Acceptance | `scripts/oracle_regression.py`, `after_load_snapshot.py`, the goldens and launch sets in `es2-data\oracle` | the replay test (section 10, [The headless acceptance](#the-headless-acceptance)) |
| Files | `formats/` (VOL, DPL, DBM, DTS, DAT, MSN, VFS), `core/src/vfs_stream.c`, `core/src/rtl_io.c` | the file services (section 6) |

## 2. The process and the guest address space

### 2.1 Process model (decision)

- **i686 Windows executable**, built by the default platform
  `//bazel/platforms:windows_x86` (`.bazelrc`), linked like the harness
  (`--image-base,0x10000000`, `--disable-dynamicbase`, `--stack,0x1000000`):
  Windows then places the initial 16 MiB stack at `0x00400000`, and
  `startup_reservation.h` claims the image range in place with a
  `PAGE_NOACCESS` barrier page at `0x006d8000` (the harness's
  `check_startup.ps1`: 100/100 launches, 50 next to six parallel instances).
  The initial thread starts the simulation thread and joins it.
- **One simulation thread.** `//hook` requires that every hooked call and
  install happen on one thread; the adapters keep process-lifetime counters
  without locks. Headless the logs are written synchronously.
- **x87 control word `0x1332`** at the start of the simulation thread, as
  CW3220's `__fpreset` `0x413aac` sets it
  ([rtl_imports.md "The runtime lifecycle"](rtl_imports.md#start-up-confirmed-in-vitro-unless-marked-static);
  the harness's `IN_VITRO_FPU_INIT`). The core was validated in vitro under it.
- **DEP on.** The LLVM-MinGW link marks the image NX-compatible, so DEP is on
  permanently (flags 3); otherwise the host calls `SetProcessDEPPolicy` with
  ATL thunk emulation off (Windows would otherwise emulate some byte patterns
  of non-executable pages). A jump into CODE faults (section 4.4).

**Why identity and not an offset mapping (decision).** The adapters, which
carry the argument marshalling, the scratch frames and the residue choices of
every validated hook, assume guest = host: they pass `math_native_memory`
(whose `read32` dereferences the address) to the core, take host pointers as
`(BYTE *)u + rva` (`u` = the image base), and read guest objects directly
(`*((BYTE *)(uintptr_t)a[0]+0x41)` in `weapons_hooks.h`). An offset mapping
would mean rewriting every adapter; a 64-bit host is therefore not part of
this design.

### 2.2 Address map (confirmed for the image, decision for the rest)

No column below comes from a file: the host builds the image from the own
tables ([Headers, imports and the icon as own tables](#headers-imports-and-the-icon-as-own-tables)
for the layout and imports,
[The initialised DATA and the CODE bytes as own runs](#the-initialised-data-and-the-code-bytes-as-own-runs)
for the bytes, [es2sim without DBSIM.EXE](#es2sim-without-dbsimexe) for the
mapping).

| Guest range | Content | Source | Protection |
|---|---|---|---|
| `0x00000000..0x000fffff` | never used | Windows keeps the first 64 KiB free; `pool_next` `0x471c18` takes a debug path for list nodes below `0x100000` ([pool_iter.c](../../core/src/pool_iter.c)) | no guest object here |
| `0x00400000..0x00400400` | PE headers | none: zero (the host takes the layout from `es2_sim_image_info_own`) | read-only |
| `0x00401000..0x00497000` | CODE (`vsize 0x96000`, raw `0x95c00`) | the seven CODE_TABLE runs (six index tables, the dword `0x4967ae`); every other byte zero | **never executable; read-only on the seven table pages, no access elsewhere** |
| `0x00497000..0x004a7c00` | DATA, initialised (raw `0x10c00` = 68,608 bytes) | the own runs (`sim/data/dbsim_data.c`), equal to the file's bytes zeros included | read-write |
| `0x004a7c00..0x006cc000` | DATA, zero-filled rest (BSS; DATA `vsize 0x235000`) | zero | read-write |
| `0x006cc000..0x006cd000` | `.idata`; IAT at `0x6cc2d4..0x6cc504`, 134 slots | zero, slots set to refusal traps from the own import table | read-write |
| `0x006cd000..0x006d8000` | `.edata`, `.reloc`, `.rsrc` | zero | read-only |
| `0x006d8000` | barrier page | host | no access |
| heap regions | DBSIM's `new`/`calloc` blocks | own heap (section 3) | read-write |
| stack, host modules, host heap | the host's own | Windows | — |
| platform regions | memory the original obtains from the OS (the DIB section's bits, sound buffers) | platform services (section 6) | read-write |

The highest relocated reference into DATA is `0x6cbee8` (a dword; the
catalogue's `0x497000..0x6cbee8`), inside the section end `0x6cc000`. 1,336
distinct relocated references point into the zero-filled part and 2,852 into
the initialised part (4,188 distinct targets). Confirmed from the reference
file's section table and relocations.

### 2.3 The image: what is read and what is built

- **Compiled in, never read from a file** ([CLAUDE.md](../../CLAUDE.md) §3,
  "Data from the executables"). The host writes the own runs
  (`es2_sim_dbsim_data_write`) into the zeroed reservation: all of the
  initialised DATA, where the vtables, the constant tables (the quarter-angle
  and sine tables the math adapters pass as `u + 0xa05bc`, `u + 0xa1e3c`,
  `u + 0xa1c34`), the RNG seed, the keyboard map `0x4a10ec`, the 65 init
  records `0x4a79d8..0x4a7b5d` and the 26 exit records from `0x4a7b5e` live,
  plus the seven CODE runs. The layout, the imports and the icon come from
  the own image tables. The file path (hash gate, PE parse) survives only in
  the testonly `//sim:image_file`, for the tests that compare the own tables
  with the file.
- **CODE is data too (confirmed in code).** Core functions read byte tables
  inside CODE through guest memory:
  [mission_load.c](../../core/src/mission_load.c) at `0x4100fd + i`,
  `0x405366 + t` and `0x426492 + i`, and the HUD readers of `0x436be2`,
  `0x44bb9f`, `0x44e1f4`; the input and file services read the dword
  `0x4967ae`. The seven CODE offsets the adapters form from the image base
  (all in `math_hooks.h`) are call targets of guest bridges, not reads. The
  measured reads are the table sites and the RTTI counts of `0x496406`
  ([What es2sim really reads from the mapped image](#what-es2sim-really-reads-from-the-mapped-image)).
  CODE holds only the seven table runs, readable on their pages; the RTTI
  counts are read from the own runs by the models of `0x496406`, and every
  other CODE page is no-access, so a stray read faults with a named line.
- **Built by own code, never read.** The zero-filled part is the program's
  state: the static initialisation (the 65 records in table order), the
  start-up, the mission load and the frames fill it, as the original does.
  The stage registry `0x4a804c` (count `0x497074`) is written by the init
  records.
- **Rebuilt from documented formats: nothing of the image.** The run-time data
  (VOL members, DTS, DBM/DPL, MSN, STR, the `.cfg` files) is read from the CD
  and the installation through `formats/` and the VFS, at the moments the
  original reads it.
- **Not applied: relocations.** The guest keeps the original VAs, so the
  `.reloc` directory (RVA `0x2ce000`, size `0x8ab4`, 16,850 HIGHLOW sites:
  13,396 in CODE, 3,454 in DATA) is only the inventory of section 4.1.

### 2.4 The guest memory view for new `sim/` code (decision)

New code in `sim/` reads and writes guest state only through an
`es2_guest_memory` (the host's identity view in the executable, a synthetic
arena in unit tests), never by casting a guest address. That keeps the `sim/`
unit tests independent of the `0x400000` claim (they run with a synthetic
image at any address) and keeps the code to the rules of
[core/README.md](../../core/README.md). The adapters keep their identity view;
they are validated in vitro as they are.

## 3. The heap

**Contract (confirmed, [rtl_imports.md](rtl_imports.md) §4.3).** DBSIM
allocates only through `operator new` `0x47749c`, `new[]` `0x4774f0` (n + 8),
both `0x47747c` → CW3220 `_calloc(1, n)` when the arena is off, and frees
through `delete` `0x477548`/`delete[]` `0x47758c` → `_free`; `_realloc` at
`0x474bf2` and `0x47c55f`. The game relies on: zero-filled blocks
(constructors), the CW3220 header word `s|1` at `p-4` (summed into the counter
`0x6bb30c`; nothing branches on it), 4-byte alignment, `malloc(0)` = NULL.
The arena (`0x476df4`, 704 KiB inside BSS at `0x604f04`) is set up only with
`-l` or below 12 MB of physical memory; no suite passes `-l` (all 40 entries
of `oracle-suites.json`: none, `-SPRUNKNOWN` or `-rc1tape`), so it is off.

**The address sequence does not matter (confirmed live).** With DBSIM's
blocks in own reservations, apart from CW3220's internal blocks, so that the
relative layout differed from the original's, 36 of 36 suites were identical
to their goldens ([rtl_imports.md "The heap, replaced"](rtl_imports.md#the-heap-replaced)).
Pointer-bearing watches are compared decoded and no tick field holds a pointer
(oracle_regression.md "What is compared"). The one known address test,
`pool_next`'s `node < 0x100000`, only selects a debug report.

**In the host.** `es2_sim_heap` (`//sim:heap`) runs the CW3220 heap model over
own `VirtualAlloc` reservations with the original's granularity choice, data
through `fill`/`move`, the same block layout, so the services for
`0x47749c`/`0x4774f0`/`0x47747c`/`0x477548`/`0x47758c` and the `realloc` sites
keep reading `p-4` as the game does: zero fill, header `s|1` at `p-4`, NULL
for 0, the allocator's registers (`//sim:heap_test`). Reservations are asked
for at `0x20000000` first (logged), the system's choice as fallback: runs are
then repeatable byte for byte, and every address is above `0x100000`.
CW3220's attach blocks (the environment copy, the stdin and stdout buffers,
the C++ static objects) and the file services' stream buffers and find nodes
are on this heap, before DBSIM's entry, as in the original
([The heap trace](#the-heap-trace)); its reservations do not adjoin.

**After-load snapshot.** The host writes ES2SNAP1 v2 with flag 128 (own heap)
from an `es2_heap_walk`; `after_load_snapshot.py` pairs blocks by reference,
so a different layout is no difference (mission_load.md §14).

## 4. Dispatch

### 4.1 The domain (confirmed counts, reference file and DBSIM map)

| Set | Count | Basis |
|---|---:|---|
| Pointer-referenced entries: CODE targets of relocated dwords that start with `55 8b ec` | 1,036 (856 referenced from outside CODE) | the relocations; [dbsim_map.md](dbsim_map.md) lists the same 1,036 roots |
| … live original (a map row, i.e. entered with every hook original, in at least one of 40 suites, load and frames) | 372 (147 hot) | the DBSIM map |
| Original callees the adapters call through a bridge | 177 (170 real guest calls: H3 79, platform 47, RTL 31, H4 12, dead 1; 7 computed in core) | hook_frontier.md "Original callees" |
| Virtual-call slot offsets the adapters use | 36 | hook_frontier.md "Slot calls" |
| Import slots | 134: CW3220 53, KERNEL32 23, USER32 39, GDI32 13, DDRAW 1, WINMM 4, MSVFW32 1 | `.idata` of the reference file |

By the address ranges of dbsim_gap.md §1 (module map only, an estimate) the
372 live entries are HUD 151, R3D 92, SIM 58, DLG 24, FILE 14, AUD 13, INP 10,
RTL 2, SHELL 1, MATH 1 and 6 outside the map's ranges.

A virtual call is not a separate case: the core reads the slot dword from the
vtable in DATA (guest memory) and calls the VA it finds, so the dispatch keys
on VAs only.

### 4.2 Entry kinds (decision)

| Kind | Implementation | Examples |
|---|---|---|
| *hook* | the registry's adapter: `replacement_context(args, u, regs)` when the extension has one, else `replacement64`/`replacement80`/the legacy `replacement`; ABI and result kind from the extension | every registry hook |
| *service* | host code with the same calling contract, listed in `sim/dispatch/services.h` | the runtime pieces linked into DBSIM (`0x49630c` frame records, `0x49667c`, `0x4953c4`), the import thunks (`0x49681a` `_memset`, `0x4968ec` `GetTickCount`, …), the allocator wrappers of section 3, the clock `0x467828` and the limiter `0x4678c0`, and every original function rebuilt in `sim/` |
| *headless* | a service that keeps every non-pixel write and draws nothing | a drawing function whose contract shows no write outside the frame buffer and no clock, stream-B or sound call ([hud.md](../reverse-engineering/hud.md#the-coupling-set-measured)) |
| *refused* | stops the run: log line with the VA, the function index name and the caller's return address, exit code 70 (`ES2SIM_REFUSED`) | every VA not listed above, every IAT slot without a service |

Nothing ever falls back to the original.

### 4.3 Table generation

- The hook rows are generated at compile time from the registry X-macro
  (`sim/src/adapters.c`, a static assert ties the row count to
  `ES2_HOOK_COUNT`), `{0x400000 + rva, "name", &descriptor, extension}`.
- `sim/dispatch/services.h`: an X-macro `ES2_SIM_SERVICE(name, va, abi,
  result, args, function)`; the rows are kept in topic fragments
  `sim/dispatch/services/*.h`, which the generated `services_rows.h`
  includes.
- When the table is sealed it is sorted and checked: a VA twice, a service VA
  that is also a hook, more than six arguments, an ABI/result combination the
  hook framework would refuse.
- A refusal names the VA, the hook/service/import name, the caller's return
  address and frame and, for CODE, the `docs/functions/` page
  (`//sim:trap_refusal_test`). There is no generated name list for unhooked
  functions; the refusal points at the index page.
- `//sim:table_test` on the reference image: every VA lies in CODE and has
  entry evidence (a target of the 16,850 relocated dwords, or of a rel32 call
  or jump in CODE), with one exception, **`heading_sin` `0x47e027`**, which
  nothing references statically (no relocated pointer, no rel32 call or jump;
  it starts with `enter 0, 0` right after `heading_cos`'s `ret` at
  `0x47e026`): hypothesis, reached only through a way the scan does not see,
  or dead. The thunk services are `jmp dword [slot]` on the import they name.

### 4.4 How a call reaches the dispatcher

1. **Own C code** calls `uint64_t es2_sim_call(es2_sim *, uint32_t va, const
   uint32_t *args, unsigned count, uint32_t regs[3])`.
2. **Adapters and core through their bridges.** The adapters reach other game
   code with machine calls to the VA: `es2_hook_machine_call`
   (`hook/machine.S`), group bridges with `call *…` in 31 adapter headers
   (`weapons_guest_call`, `ai_behaviour_stack_call`, …) and five function
   pointer casts in `math_hooks.h`. **Stage 1, the trap:** the call faults
   (CODE is not executable); a vectored exception handler sees an execute
   fault with EIP in CODE, takes the VA, the return address and the
   arguments at ESP+4, and EAX/EDX/ECX from the context, and redirects EIP to
   a host trampoline that runs the dispatcher on the normal stack and returns
   to the caller like the original function (popping stdcall/Borland stack
   arguments, loading ST0 for an x87 result). Import slots trap the same way,
   keyed by slot VA (`//sim:trap_calls_test`: all kinds and ABIs, nesting,
   ST0 results). No adapter changes; a call to an unlisted VA is refused by
   the same path. **Stage 2, the bridge seam:** the hot VAs are called
   directly at compile time (`ES2_SIM_HOST`), without the trap
   ([Stage 2: the bridge seam for the hot VAs](#stage-2-the-bridge-seam-for-the-hot-vas)).
3. **Entry shims where the proxy has them.** 109 hooks are not entered at the
   replacement under the proxy but at a shim of the capture chain
   (`*_capture_entry`), and the shim carries part of the contract: it reads
   the caller's registers and stack (the emitter adapters of `0x40ea58` and
   `0x40ec64` read a residue record through ECX and move ESP to it). With the
   direct path, `c3_09-loadout-b` dies at tick 1390 with a corrupt ESP.
   `es2_sim_register_adapters` therefore runs the chain once per row on a
   stand-in hook and keeps the shim it installs (`es2_sim_entry.entry_shim`);
   the trap continues at the shim, whose bridge is the trampoline; a C call
   of such an entry is made as a machine call (except an x87 result). The
   residue the shims read is section 5's zero model
   ([The entry shims](#the-entry-shims)).
4. **FS:0.** 35 adapter headers handle FS:0: they restore it to the head that
   cores name after the original's `0x49630c` frame records. The `0x49630c`
   service links and unlinks records on the real FS:0 as the original does;
   first-chance traps are handled by the vectored handler before any SEH
   walk, so the guest records (handler field `0x4967f0`, in CODE) are never
   called for a trapped call (tested, section 13 question 8).

**Rejected alternative: a stub image** (executable CODE with a jump written at
each entry). It needs the original bytes executable (a call to an unlisted
entry would run original code silently), and four entries of the known
inventory lie less than five bytes before the next (`0x425344`, `0x463370`,
`0x4650cc`, `0x467ad8`; 2,575 entries from the map, the pointer set and the
registry).

**Cost.** One trapped call costs about **11.58 µs** under WOW64
(`//sim:trap_benchmark_test`: 11,581 ns per call against 2.4 ns for a direct
call and 28 ns for `es2_sim_call`; `rng_next` through the trap on the real
image 11,575 ns against 4.9 ns for the core function). At about 3,200 traps
per frame windowed and 4,000 to 5,000 headless that is 37 to 58 ms per frame,
more than the limiter's 40 ms on its own; hence stage 2. The per-frame counts
come from [es2sim's frame times](#es2sims-frame-times).

### 4.5 Refusal and the log

The host keeps per VA a call count and writes the counts in the tick log's
exit record; a refusal writes
`{"event":"refused","va":…,"name":…,"return":…,"frame":…}` to the tick log,
then the exit record (reason `refused`), and ends the process with 70. A
refused trapped call writes the exit record before the trap handler ends the
process.

## 5. Stack residue as modelled values

Inside DBSIM some original callees read stack bytes they did not write. Under
the proxy the entry shims give them the original's bytes by running the
replacement at the original coordinate; in the host no original frame exists,
so each reader gets a **modelled value**. Known readers ("direct path" = what
the adapter's direct path supplies, which is what the host gets):

| Reader | Residue | On the direct path | Evidence that the golden accepts it | Source |
|---|---|---|---|---|
| `group_order` `0x4231a4` | priority word `[f-1Ch]` for codes 3, 6, 9..0Eh and above 0Fh | zeroed scratch frame: 0, order given | the golden's word was ≥ −1 (order given) in `c3_09-wingman-orders` (Alt+O at frames 151, 1481); accepted, not traced | [input.md](../reverse-engineering/input.md#stack-residue-under-the-command-handler), `mfd_orders.h` |
| the `+28h` weapon bodies under `aim_fire` `0x41f5a0` (preparation `0x40e788`, emitter, alternate, impact exclusion) | padding bytes of the matrices and points, the impact-exclusion dword | `es2_weapon_graph.padding`/`composed_padding`/`excluded` as the adapter sets them | **open**: the live difference of `c3_09-loadout-b` (frame 1671) was fixed by the shim, the consuming byte not identified; in the host the shims run over a zeroed area (4096 bytes below the caller's ESP), and `c3_09-loadout-b` replays identical for 8886 ticks | [ai.md](../reverse-engineering/ai.md#stack-residue-under-aim_fire), `weapons.h` |
| `track.h` matrix byte 19 | output byte 19 on some paths | `padding` argument | weapons.md: copied, not used in arithmetic by the known consumers | `track.h` |
| configuration loader | scratch content where the original reads residue | zeroed scratch; `ES2_CONFIG_RESIDUE` observer key marks the case | open per call site; no reader VA and offset named | `config.h` |
| mission triggers and events | 16 record bytes | caller-supplied guest address | open; no reader VA and offset named | `mission.h` |
| `vehicle_value` `0x418e60` under the results writer | the ECX it leaves when no item was appended | the scratch dword `f-4` | open (no reader named) | `results_writer.h` |
| weapon factory under the load (`0x47a2c8`'s class constructor) | the projectile record local `[f-58h]` when the selector is `21h` | the adapter's scratch content | open | `mission_load.h` |
| `cockpit_sync`, `bridged_leaves` (bytes 6..17h of M) | frame residue | caller's frame | open; no reader VA and offset named | the two headers |
| register residue (EDX:EAX, ECX of `ai.h`, `flyer.h`, `input.h`, `weapon_helpers.h`, `rtl.h`, `rtl_io.h`) | registers the original leaves | reproduced by the `_registers` variants | dead at the callers where the headers say so | the headers |
| sound table init `0x46254c` | its 20-byte local L (ESP+0, never written), searched for `".hmp"` (mode 0) and `".wav"` (mode 1); else the mode is the entry EDI | L zeroed: neither found | **neutral** (static): in the original L holds bytes 12..27 of `0x46379c`'s profile buffer and the EBP `0x463694` saved; mode 1 and the EDI pointer open alike, only `".hmp"` would differ, and DBSIM's only `".hmp"` is the searched constant | [audio.md](../reverse-engineering/audio.md), `audio.c` |
| palette apply `0x465e28` | its 1 KiB colour table: entries without a flag bit, byte 3 of every entry | zeroed | **neutral** (static; live: every installed colour has flag bit 0, [presentation.md](../reverse-engineering/presentation.md)): [dpl.md](../formats/dpl.md), the shield and intro entries, copies; byte 3 reaches only `SetDIBColorTable`'s `rgbReserved`; no `GetDIBColorTable` import; both frame hashes take bytes 0..2 | [presentation.md](../reverse-engineering/presentation.md), `load.c` |
| shade ranges `0x42e8e8`, flag path | p1's stack address read as a pair (an original bug) | the original's frame layout: equal at an equal call-area address | **unreachable** (static): both callers pass a clear low flag byte (`0x46bd6e`: 0; `0x4287b3`: byte `0x4cf984` = B+9 `0x4d24c5`, cleared by `0x45caf4`'s memset and written by no code) | [rasterizer.md](../reverse-engineering/rasterizer.md), `shape_draw.c` |
| `0x451de0`, `0x45c964` | ECX = the second local, unwritten without a drawn entry or bitmap | 0 | dead: no caller reads ECX ([The popped locals](#the-popped-locals)) | `mfd.c`, `hud_draw.c` |
| projection `0x48cce4` | EDX without a focal shift (its unwritten local) | not modelled | dead (static): the only caller `0x46bcb0` writes EDX before any read on both paths | `shape_draw.c` |
| the buffered stream rows' callees | the EDX/ECX the call sites leave, the callee's popped residue | zero entry registers | dead: no caller reads them | [Files, streams, math and runtime leftovers](#files-streams-math-and-runtime-leftovers) |
| the 109 entry shims | the caller's stack below its frame | 4096 zeroed bytes (`ES2_SIM_SHIM_ZERO`) | every suite identical; no reader named ([The entry shims](#the-entry-shims)) | `dispatch_trap.S` |

**Seam.** `sim/src/residue.c` holds this table as data: reader VA, offset or
register, width, rule (`zero` = the direct path's scratch, `writer` = a value
computed from guest state the original's earlier writer leaves, `constant`),
and the evidence line. Where the rule is not `zero`, the host sets the value
in the adapter's scratch or frame before the call. A rule other than `zero`
needs the writer's contract from the bytes. The host stack's own bytes are
never a residue source: they depend on our compiler.

**The zero model checked (static).** For the readers whose original writers
could be found in the listing (the rows marked static), the residue was traced
to its writer and its consumers: `0x46254c` and `0x465e28` read residue that
changes nothing, `0x42e8e8`'s stack-address bug is never reached, and the
register rows are dead at every caller. No rule other than `zero` is needed.
The `aim_fire` padding, the `group_order` word, the input layer's locals, the
terrain shade words and the shims are not disproved, but no reader of their
bytes has been named. Method: find the reader's local relative to its
caller's ESP; find the earlier sibling calls whose frames overlap it; decide
whether any value they can leave changes the reader's result; scan the
listing for every writer of the bytes that select the path. The listing scans
are hypotheses with their addresses in the rows (`sim/src/residue.c`).

## 6. Platform services: the imports

Every import call of the original becomes a call of a host service, with one
headless implementation (no window, no device) and a windowed one on SDL3
([The windowed platform layer](#the-windowed-platform-layer)):

| Imports | Headless | Own code |
|---|---|---|
| CW3220 (53): heap, string and memory, file and console, start-up and exit | `core/src/rtl*.c`, the heap of section 3, the file model over the working directory | `services_rtl.c`, `services_files.c`, `startup.c`, `loop.c` |
| KERNEL32 `GetTickCount` (thunk `0x4968ec`, IAT `0x6cc3e8`) | virtual time (section 7) | `services_rtl.c` |
| KERNEL32 global memory, `Sleep`, `OpenFile`, `_lopen` family, `GetPrivateProfileStringA`, `LoadLibraryA`/`GetProcAddress`, `GlobalMemoryStatus` | files of the working directory; `Sleep` returns at once headless; `GlobalMemoryStatus` answers 64 MiB of physical memory (no arena) | `startup.c`, `input.c`, `audio.c` |
| USER32 window, class, pump (`PeekMessageA` `0x6cc40c`, `TranslateMessage`, `DispatchMessageA`), `GetKeyState`, `GetCursorPos`, paint, cursor | the journal (section 8); no window; the window procedure's handler list runs as own code | `loop.c`, `input.c`, `services_cursor.c` |
| WINMM `joyGetDevCapsA`, `joyGetPosEx` | the journal or `JOYERR_UNPLUGGED` (as the proxy's replay) | `input.c` |
| WINMM `mciSendCommandA` (CD music) | records the call, plays nothing; `MCI_OPEN` fails | `audio.c` |
| GDI32 DIB section, palettes, `StretchBlt`; DDRAW `DirectDrawCreate` (only on the untaken DirectDraw branch) | a host region for the DIB bits (the original's `G+0x4e` pointer at `0x4d250a` points outside the heap), frame hash only | `frame_buffer.c`, `present.c` |
| `SOS9503.DLL` (97 entry points, `LoadLibraryA`), `BWCC32.DLL` (loaded, never used) | a silent device that keeps every call and its order | `audio.c` ([dll_imports.md](dll_imports.md)) |
| MSVFW32 `ICSendMessage` | no running caller ([The AVI code in DBSIM and es2sim](#the-avi-code-in-dbsim-and-es2sim)) | refused |

The import slots stay refusal traps; own code that reproduces an original
function calls the service directly (C), never the slot.

## 7. Virtual time

**Contract (confirmed, timing.md).** DBSIM reads `GetTickCount` only through
`0x4968ec`, from three sites: the clock wrapper `0x467828`
(`(value − [0x4d3b78]) >> 4`), timing init `0x467840` (call at `0x467883`:
base `0x4d3b78`, previous clock `0x4d3b74`) and the limiter poll at
`0x4678d0` (return address `0x4678d5`). The proxy answers a limiter poll with
`previous + step`, every other call with the current clock, and with
`virtual_time_intra_frame=1` advances it by 1 ms per further call in a frame,
at most step − 1 times, then the stall guard (default 100,000 calls). Tape
playback skips the limiter; the frame boundary then gives the missing step
(`vt_frame_boundary`). The oracle compares `completed_clock_ms`,
`clock_raw_hex` and the counters `vt_limiter_polls` and `vt_stall_advances`.

The registers each site leaves are in [The host skeleton](#the-host-skeleton).

**In the host.** The state machine (`es2_vt_limiter_poll`, `es2_vt_read`,
`es2_vt_frame_boundary`, the counters) is the pure library `//sim/oracle:virtual_time` (C17, no Win32,
explicit state struct); the proxy's IAT wrapper `proxy/virtual_time.h` calls
it and keeps the INI configuration and the wall-clock pacing (a callback at
the same point of the poll); the `vt_*` names are macros onto the library
state, so `tick_log.h` and the `virtual_time_*_test` fixtures read them
unchanged. The host's three call sites name the caller explicitly
(`ES2_VT_LIMITER`, `ES2_VT_INIT`, `ES2_VT_CLOCK`) instead of the
return-address test. Parameters come from the suite's launch report (step
47 ms for recorded suites per `record-suite.ps1`, start 16,777,216, intra 1,
pace 0 headless). The frame counter `[0x4d257b]` is the key the boundary and
the journal share.

## 8. The journal provider

**Contract (confirmed, input_journal.md).** Per frame (`[0x4d257b]`) the
journal injects its messages before real ones; a message with pump key `p` is
injected only after `p` earlier `PM_REMOVE` peeks in that frame; `GetKeyState`,
joystick and cursor answers come from the journal, else from a virtual key
state, `JOYERR_UNPLUGGED`/`JOYERR_PARMS` and (320,240); injected mouse
messages set the virtual cursor; focus messages are never seen in replay.
`in_injected` and `in_fallbacks` are compared; `in_dropped`/`in_focus_dropped`
are not.

**In the host.** The parser (`ij_parse_line`, `ij_load`'s table), `ij_take`,
the per-frame pump count, the virtual key state and cursor and the counters
are `sim/oracle/journal.c` (`//sim/oracle:journal`, pure C), used by the
proxy's IAT wrappers and by the host's headless platform. `proxy/input_journal.h`
keeps the IAT wrappers, the record writer, the configuration and the focus
subclass; its `ij_*` names are macros onto the library state (`ij_state`).
Headless there is no other message traffic, so the peek count in a modal
frame is exactly the own modal loop's: the pump-keyed answers (keys 300, 310,
… from `modal_pump_keys.py`) arrive at a fixed pass. Simulation state cannot
change while the frame stands (input_journal.md), so only the order and the
clock calls per pass matter ([The modal dialogs](#the-modal-dialogs)).

## 9. The log writers and the return files

- **Tick record** (tick_log.md schema 2): written at the frame counter
  increment of the own main loop (the original's `0x45f4ad`), after the frame
  boundary of virtual time, every field read from guest memory at its VA
  (`0x4d24bc` context, `0x4d3b64` 24 timing bytes, RNG states
  `0x4d2599`/`0x4d260b` of `0x72` bytes, the player actor `[0x4d24e6]` at
  `+0x26`, `+0x0c`, `+0x28e`, `+0x290`), the `vt_*` and, with a journal, the
  `in_*` counters (`in_mode` replay). `sequence`, `qpc` and `ebx` are written
  for the format, not compared. No `hk_*` fields; the host's own counters go
  into the exit record (`es2_sim_tick`, `host.c`). The start record names the
  mode `own_process_headless`.
- **Watches** (`mem_watch.h`): the same INI syntax (`watchN = NAME ADDRESS
  LENGTH`, pointer chains `@off`, counted lengths `wADDR*STRIDE`) and the same
  events (whole ranges up to 256 bytes, `diff` runs, `unreadable`), read
  through the host's guest view ([Oracle outputs in the host](#oracle-outputs-in-the-host)).
- **Library.** The line formatting of both is `//sim/oracle:tick_record`
  (the start line, the head of a tick line, the RNG, actor, virtual-time and
  `in_*` fields, with the proxy's format strings) and `:mem_watch`, shared
  with the proxy; the proxy's `tick_log_*`, `mem_watch_*` and `normalization`
  fixtures guard its output byte for byte. The site patch, the guest reads,
  the I/O and the proxy-only fields (journal, hooks, runtime) stay in
  `proxy/tick_log.h`.
- **After-load snapshot**: ES2SNAP1 v2 as the proxy writes it,
  `//sim/oracle:snapshot` (fixed regions by reference VA, the own heap's
  walk, flag 128; the memory map lists the host's regions).
- **Return files** `results.dat`, `mission.var` and the prefs write are
  written by own code into the run's directory, as the mission end `0x461ff0`
  does ([file_io.md](../reverse-engineering/file_io.md)); the **exit code** is
  the word `0x4d27b8` (launcher.md).
- **Configuration:** the host reads the proxy's INI sections (`[time]`,
  `[trace]`, `[input]`, `[snapshot]`, `[run]`), so the oracle runners'
  generated configuration drives both processes.

## 10. Headless acceptance

The host runs one suite from its launch set and journal and stops itself at
the suite's `frame_limit` (or at the mission's own end), so the length is
exact (the proxy's runs are stopped by a 500 ms poll). The replay runs
`oracle_regression.py normalize-compare <tick-log> <out> --outputs <dir>
--frame-limit <n> --golden <golden>\normalized.json`, i.e. the comparison
`check.ps1 -Live` uses:

- ticks keyed by frame: the 14 fields of `TICK_FIELDS` (completed clock,
  limiter flag, pause flag, timing bytes, both RNG states, position, angles,
  speed, throttle, `vt_limiter_polls`, `vt_stall_advances`, `in_injected`,
  `in_fallbacks`) and the set of frames;
- watches as a sequence of changes, pointer watches decoded;
- return files by SHA-256;
- the exit code against the golden manifest's `own_process_exit`.

Not compared: `hk_*`/`rtl_*` exit counters, `player_actor`, `qpc`. For the
start-up and the load the acceptance is the after-load snapshot against the
original's snapshot of the same step (`after_load_snapshot.py compare` with
the learned noise of mission_load.md §14;
[The after-load snapshot against the original](#the-after-load-snapshot-against-the-original)).
The harness, the replay table and the frame compare are in
[The headless acceptance](#the-headless-acceptance) and
[The frame compare as tests](#the-frame-compare-as-tests).

## 11. `sim/`: layout and targets

```text
sim/
  BUILD.bazel
  README.md                      component README
  include/es2/sim/
    host.h        es2_sim: guest view, heap, dispatch, platform, oracle state
    image.h       the guest image from the own tables
    heap.h        es2_heap over own reservations; the new/delete/calloc services
    dispatch.h    es2_sim_call, entry kinds, refusal
    residue.h     the residue table of section 5
  src/
    main.c        es2sim.exe: start-up thread, claim, worker, exit code
    image.c heap.c dispatch.c dispatch_trap.c dispatch_trap.S residue.c
    services_rtl.c   runtime pieces, import thunks, allocator wrappers
    adapters.c    the proxy adapters compiled in (generated include list)
    init.c startup.c loop.c load.c input.c audio.c dialogs.c
    hud_*.c gfx.c poly.c view.c shape_draw.c terrain_draw.c scene.c
    present.c hud_widgets.c cockpit.c display.c gadgets.c mfd.c
    platform_sdl.c
  data/           the generated own image data (dbsim_data.c, dbsim_image_tables.c)
  dispatch/
    services.h    ES2_SIM_SERVICE rows: includes the generated services_rows.h
    services/     the row fragments
  oracle/         pure C17, shared with the proxy
    BUILD.bazel virtual_time.{h,c} journal.{h,c} tick_record.{h,c}
    mem_watch.{h,c} snapshot.{h,c}
  tests/          unit tests on synthetic images; the replay and frame tests
```

| Target | Kind | Content |
|---|---|---|
| `//sim:image`, `:heap`, `:dispatch`, `:residue` | `cc_library` | the seams |
| `//sim:adapters` | `cc_library` | `//tools/reference-game/proxy:hook_adapters` compiled for the host (i686 only) |
| `//sim:services` | `cc_library` | the service rows' own code |
| `//sim:host` | `cc_library` | everything above |
| `//sim:es2sim` | `cc_binary` | `es2sim.exe`; link options as the harness |
| `//sim/oracle:virtual_time`, `:journal`, `:tick_record`, `:mem_watch`, `:snapshot` | `cc_library` | portable, also linked by the proxy |
| `//sim:<seam>_test` | `cc_test`, small | synthetic images and tables |
| `//sim:es2sim_real_test` | `cc_test` | starts on the real data (`@es2_data`), SKIP without `ES2_DATA_ROOT` |
| `//sim/tests:replay_<suite>_test` | test, one per suite | headless replay + `oracle_regression.py`; goldens from `es2-data`, SKIP without them |

BUILD notes: executable and test names avoid `update`/`patch`/`setup`/`install`
(Windows' installer heuristic, error 740); every process start in a test
carries a no-window flag (`//tools:process_starts_test`).

## 12. Own code per seam

| Seam | Own code | Contract |
|---|---|---|
| claim, image, protections, DEP, FPU | `startup_reservation.h`, `sim/src/image.c`, `main.c` | sections 2.1 to 2.3 |
| heap | `sim/src/heap.c` | section 3 |
| dispatch table, kinds, trap, bridge seam, refusal | `adapters.c`, `dispatch.c`, `dispatch_trap.{c,S}` | section 4 |
| residue table | `residue.c` | section 5 |
| the c0w32 entry, the init and exit records | `init.c` | [The static initialisation as own code](#the-static-initialisation-as-own-code) |
| argv, both parsers, `drive.cfg`, `error.str`, the VOL enumeration, the file services | `startup.c`, `volumes.c`, `services_files.c` | [Start-up, parsers and file services](#start-up-parsers-and-file-services) |
| the game main and loop, the pump, MainWndProc's handler list, the limiter and clock, the mission end, the exit code, the DIB region | `loop.c`, `frame_buffer.c` | [The main loop, the journal and the exit](#the-main-loop-the-journal-and-the-exit) |
| the mission load | `load.c` | [The mission load](#the-mission-load) |
| input, audio | `input.c`, `audio.c` | [The input layer](#the-input-layer), [Audio](#audio) |
| HUD, dialogs | `hud_*.c`, `mfd.c`, `dialogs.c` | [The HUD line](#the-hud-line), [The modal dialogs](#the-modal-dialogs) |
| drawing | `view.c`, `shape_draw.c`, `terrain_draw.c`, `poly.c`, `gfx.c`, `scene.c`, `present.c` | rasterizer.md, presentation.md; accepted by the frame hashes |
| windowed platform | `platform_sdl.c` | [The windowed platform layer](#the-windowed-platform-layer) |

## 13. Open questions and their answers

| # | Question | Answer |
|---|---|---|
| 1 | Cost of the stage-1 trap per guest call, and whether stage 2 is needed | 11.58 µs per trapped call (section 4.4); stage 2 is needed and built ([Stage 2: the bridge seam for the hot VAs](#stage-2-the-bridge-seam-for-the-hot-vas)) |
| 2 | Which pointer-referenced entries a headless replay reaches with every hook replaced (the map was taken with every hook original) | with every hook replaced 41 rows of the map no longer run, among them the pointer-referenced `0x47aeb0`, `0x47aecc`, `0x47c7bc`, `0x47c7d8`, `0x47c82c` ([dbsim_map.md](dbsim_map.md#map-with-all-hooks-replaced)); the rest is the refusal log of the replays |
| 3 | Residue readers beyond section 5, and the rule for the `+28h` padding bytes | open; the padding's consumer is not identified (section 5); weapons.md for the writers |
| 4 | Which drawing entries may be *headless* (no compared effect) | the coupling set ([hud.md](../reverse-engineering/hud.md#the-coupling-set-measured)); per subsystem in hud.md and rasterizer.md "May be headless" |
| 5 | Launch inputs and the installation files a headless run needs | the files DBSIM opens: [Start-up, parsers and file services](#start-up-parsers-and-file-services) |
| 6 | The exit records' effects and the exit code path | the exit records change nothing observable ([The 26 exit records](#the-26-exit-records)); the exit code path is built ([The main loop, the journal and the exit](#the-main-loop-the-journal-and-the-exit); launcher.md "Exit code") |
| 7 | Memory the original gets from the OS besides the heap (DIB bits, sound buffers) and which guest objects point into it | the DIB bits are a host region the descriptor `[0x4d3020]+0xa` and the row table point into (launcher.md for the frame-buffer objects); the sound buffers are host memory from `GlobalAlloc` ([Audio](#audio)) |
| 8 | Whether the `0x49630c` records on the real FS:0 stay harmless under the trap handler | yes for trapped calls: with records linked on the real FS:0, one with a host handler and one whose handler lies in the non-executable page (as the guest's `0x4967f0`), trapped and nested trapped calls never reach either handler and FS:0 is unchanged afterwards (`//sim:trap_seh_test`). A genuine fault under a guest record is not tested; the original's divide faults end the process before any walk ([The original's fault paths](#the-originals-fault-paths)) |
| 9 | Whether the proxy's switch to the shared `sim/oracle` libraries leaves every golden identical | open until a replaced-mode regression of all suites with the switched proxy is recorded |

## The host skeleton

`es2sim.exe --headless` maps the image, sets up the heap, the dispatch and the
oracle libraries, and runs DBSIM's start-up as own code
([sim/README.md](../../sim/README.md)). `--tick-log` writes the start record
(mode `own_process_headless`) and an exit record with the per-VA call counts;
a synthetic image is refused with 65 (`//sim:es2sim_real_test`).

### Runtime service contracts (static, listing; in-process)

- **`0x47747c(n)`** (`0x47747c..0x47749b`): `_calloc(1, n)` with EAX = n
  pushed; when p ≠ 0, EDX = `[p−4]` is added to `[0x6bb30c]`. Leaves EAX = p,
  ECX = calloc's (0, or n for NULL), EDX = `[p−4]` or calloc's.
- **`operator new` `0x47749c`, `new[]` `0x4774f0`** (n + 8 at `0x4774f8`): with
  `[0x6bb304]` = 0 the `jae` at `0x4774aa`/`0x477501` is always taken and
  `0x47747c(n)` runs; its `pop ecx` (`0x4774e5`/`0x47753c`) leaves ECX = n
  (n + 8). n below a nonzero `[0x6bb304]` (the `-l` arena) is refused.
- **`delete` `0x477548`, `delete[]` `0x47758c`**: `0x476e2c(0x6bb2f8, p)`
  returns EAX = 0, ECX = EDX = p for every p while the arena header
  `[0x6bb2f8]`/`[0x6bb2fc]` is zero (signed compares at `0x476e3a` and
  `0x476e41`); p = 0 returns with EAX = ECX = EDX = 0; else `[0x6bb30c] −=
  [p−4]`, `_free(p)` with EAX = `[p−4]`, ECX = EDX = p, and `pop ecx` leaves
  ECX = p. A nonzero arena header is refused. `delete(0)` is a no-op.
- **`0x49630c`** (`0x49630c..0x496349`): record r = EBP + `[EAX+4]`; stores
  `[r+8]` ← EAX, `[r+0xc]` ← the caller's ESP after the call, `[r+4]` ←
  `0x4967f0`, dwords `[r+0x10]` and `[r+0x12]` ← 0, `[r+0x1c]` ←
  `[[0x6cbee4]]`, `[r]` ← FS:0, FS:0 ← r; EAX = the previous FS:0, ECX/EDX
  kept. The service links on the real FS:0 and refuses while `[0x6cbee4]` is
  0 (the c0w32 entry `0x401000` sets it).
- **`GetTickCount`** at the three sites (`0x467828..0x4678f5`): the limiter
  (return address `0x4678d5`) is a poll with `[0x4d3b74]`; ECX/EDX are left
  as at the call: the limiter overwrites EDX (`0x4678db`) and the low word of
  ECX (`0x4678e5`, the callee `0x467894` reads only that word); `0x467828`
  and `0x467883` return to their callers with them (hypothesis: dead there,
  not traced).
- The pure CW3220 imports run through their thunks over core `rtl.c`;
  `sprintf`/`vsprintf` formats the model does not cover are refused, never
  approximated (`//sim:services_test`).
- In-process on the real image: `new(0x808)` gives the header `0x811` and
  `[0x6bb30c]` = `0x811`, the figures derived statically for init record 53;
  `delete` returns it to 0.

## Static initialisation, the 65 init records and the 26 exit records

What DBSIM's static initialisation writes before WinMain, record by record,
what the exit records do, and which of the written globals the mission load
and the frame read. Read from the listing (static); every store of the init
and exit records is **confirmed in vitro**
([The static initialisation as own code](#the-static-initialisation-as-own-code));
the readers (below) are static.

### The tables

The module table `0x49702c` gives init `0x4a79d8..0x4a7b5e` and exit
`0x4a7b5e..0x4a7bfa`: **65 init and 26 exit records** of 6 bytes (byte 0 done
flag, 0 in the file; byte 1 priority; dword 2 function), every priority
`0x20`, all functions distinct. CW3220's selector runs the init records in
table order and the exit records in reverse table order
([rtl_imports.md "The runtime lifecycle"](rtl_imports.md#start-up-confirmed-in-vitro-unless-marked-static)).
Record numbers below are 0-based table indices, as in mission_load.md §4
(record 46 = `0x45caf4`, which dbsim_gap.md counts 1-based as
"record 47").

**The category split** by address (31 simulation, 13 HUD, 9 renderer, 3
input, 3 shell, 2 presentation, 2 dialogs, 1 audio, 1 runtime) is what the
module map of address ranges ([dbsim_gap.md](dbsim_gap.md#1-method)) gives:
24 records lie in its SIM range and 7 in ranges the map leaves unnamed
(`0x467824`, `0x467b8c`, `0x46a3c0`, `0x46a57c`, `0x46b860`, `0x47149c`,
`0x478dec`), counted as simulation. It is a split by **address**, not by
role: record 64 (`0x4956a4`, "runtime") opens the `DARKMONO` VxD, record 25
("simulation") registers a renderer class table, record 46 ("shell") is the
globals object with both RNG streams.

**What runs.** Of the 65 functions, **20 write nothing** (`ret`, or only array
constructions whose element constructors are empty), **7 write only zeros
into memory that is already zero** (BSS, or DATA whose file bytes are zero),
and **38 change the image**. The executed path reaches 33 distinct functions
besides the 65 (plus the thunks of `_memset`, `_memmove` and `CreateFileA`);
the 133 functions of dbsim_gap.md are the static closure, which includes
branches never taken here (the allocation and error paths of `0x49667c`, the
other cases of the log function `0x479f24`, the arena path of `0x47749c`).
Three callees are hooks: `matrix_ctor` `0x495132`, `rotation_ctor`
`0x495194` and `rotation_base_ctor` `0x49514f` (constructors in records 2 and
63; they write nothing). Under the proxy they never fire here: hooks arm at
the mission-load call at the earliest (`early_arming.h`), so the init records
always run original code.

**Bookkeeping.** Most constructors increment the construction counter
`[[0x6cbee4]]` (a stack word below StackBase,
[rtl_imports.md §4.1](rtl_imports.md#41-borland-runtime-pieces-linked-into-dbsim-confirmed-static)),
and records with destructible statics set up and remove a frame record
(`0x49630c`; FS:0 is restored on return). The counter does not reach the
image, but `0x49630c` copies it into frame records, so the own code moves it
as the original does (the per-record deltas are below). The array
construction `0x49667c(buf, size, count, mode 1, ctor)` with a buffer calls
the element constructor `count` times and allocates nothing.

### The 65 init records

Abbreviations: **stage s `f`** = `0x401d64(f, s)`, which appends {dword f,
word s} at `0x4a804c + 6·[0x497074]` and increments the count word `0x497074`
([main_loop.md](../reverse-engineering/main_loop.md#the-main-loop-around-it));
**zero ptr** = a pool pointer dword in BSS set to 0 (no visible change);
**GLBitmap ctor** `0x48156c(o)` = vtable stores `0x4a69f0`, `0x4a3368`,
`0x4a3040`, `0x4a306c` at `o` in that order, then dword `o+0xa`, word
`o+0x14`, dword `o+0x16`, dword `o+0xe` ← 0; **region ctor** (inline in records
28 and 37) = vtable stores `0x4a69f0`, `0x4a3368`, `0x4a3390`, `0x4a3264` at
`o`, dword `o+4` ← 0 (`GLRectangleRegion`,
[display_modes.md](../reverse-engineering/display_modes.md)); **zero16**
`0x453824(o)` = dwords `o+4`, `o`, `o+8` ← 0, words `o+0xe`, `o+0xc` ← 0;
**empty** = element constructors that write nothing. **Vis** = bytes that
differ from the file image afterwards, without the done flag. **Ctr** = the
construction counter's change.

| # | Function | Writes, in order | Vis | Ctr |
|---:|---|---|---:|---:|
| 0 | `0x401d30` | arrays `0x4a7bfc` (50 × 12) and `0x4a7e54` (50 × 6), empty (camera tables, [camera.md](../reverse-engineering/camera.md)) | 0 | |
| 1 | `0x401ef8` | stage 6 `0x401e5c` (the record-mode flush) | 4 | |
| 2 | `0x402bb8` | arrays `0x4a8cf0` (10 × `0x52`), `0x4a9042` (30 × 12), `0x4a91aa` (30 × `0x20`, `matrix_ctor`); no byte written | 0 | |
| 3 | `0x40695c` | stage 2 `0x405fac`, stage 5 `0x405ea4`; zero ptr `0x4a95a0` (buildings) | 8 | +1 |
| 4 | `0x4078c8` | stage 2 `0x4076e4`, stage 5 `0x4077e8`; zero ptr `0x4a95fe` | 8 | +1 |
| 5 | `0x408284` | stage 2 `0x407b54`, stage 5 `0x407b3c`; zero ptr `0x4a961e` | 8 | +1 |
| 6 | `0x408e9c` | stage 2 `0x408860`, stage 5 `0x4083e0`; zero ptr `0x4a9642` | 8 | +1 |
| 7 | `0x40948c` | stage 2 `0x4090cc`, stage 5 `0x408f9c`; zero ptrs `0x4a966e`, `0x4a9684` | 8 | +2 |
| 8 | `0x409940` | stage 5 `0x409904`; zero ptr `0x4a968d` | 4 | +1 |
| 9 | `0x409f78` | stage 2 `0x409a34`, stage 5 `0x409a1c`; zero ptr `0x4a96aa` | 8 | +1 |
| 10 | `0x40a14c` | stage 2 `0x40a060`, stage 5 `0x40a108`; zero ptr `0x4a96c2` | 8 | +1 |
| 11–14 | `0x40ab18`, `0x40accc`, `0x40b5d8`, `0x40c2c0` | stage 2 `0x40a818`, `0x40ac38`, `0x40ade0`, `0x40b6e0` | 4 each | |
| 15 | `0x40f780` | nothing (`ret`) | 0 | |
| 16 | `0x411110` | stage 2 `0x40fc8c` | 4 | |
| 17 | `0x413ed4` | the 22 MECH state records `0x4993a4` (stride `0x3e`), below | 308 | |
| 18 | `0x414c68` | the 7 FLYER state records `0x499cf8` (stride `0x3c`), below | 70 | |
| 19 | `0x41bc5c` | stage 2 `0x415464`, stage 5 `0x41544c`; zero ptr `0x4a9b7a` (MECH pool); array `0x4a9b88` (40 × 8), empty | 8 | +1 |
| 20 | `0x41fdac` | nothing | 0 | |
| 21 | `0x420654` | stage 2 `0x41fdb0`; array `0x4a9d72` (8 × 8), empty | 4 | |
| 22 | `0x421fb0` | stage 5 `0x4215a8`; zero ptr `0x4a9db9` (FLYER pool) | 4 | +1 |
| 23 | `0x423068` | stage 2 `0x422d90` | 4 | |
| 24 | `0x426eac` | zero16 `0x4aa650` (the `mission.str` object) | 0 | +1 |
| 25 | `0x4277f0` | `0x47a0b8(0x49aa7c, 1)`: class-registry pair 0, `[0x6c5838]` ← 1, `[0x6c5834]` ← `0x49aa7c` (the grid table), `[0x4a1690]` ← 1 | 5 | |
| 26 | `0x428194` | nothing | 0 | |
| 27 | `0x428de8` | stage 2 `0x428230`; array `0x4cf940` (4 × 8), empty | 4 | |
| 28 | `0x42db40` | array `0x4cf9a8` (4 × `0x20`), empty; region ctor at `0x4cfa7a`, then at `0x4cfc9c`; a rectangle as min/max of the constant points (`0x140`, `0xd2`) and (0, `0xf0`): `[0x49ac54]` ← 0, `[0x49ac5c]` ← `0x140`, `[0x49ac58]` ← `0xd2`, `[0x49ac60]` ← `0xf0` | 10 | +6 |
| 29 | `0x430e90` | stage 2 `0x43034c` | 4 | |
| 30 | `0x434a6c` | dwords `0x4d03ac` ← 0, `0x4d03b4` ← 4, `0x4d03bc` ← 0, `0x49b06e` ← 0, `0x49b072` ← 1000, `0x49b076` ← 0, `0x49b07a` ← −1000, `0x49b07e` ← 0, `0x49b082` ← 0 | 7 | |
| 31 | `0x437270` | zero16 on the 3 elements of `0x4d0414` (stride 16), then on `0x4d0444` and `0x4d0454` | 0 | +5 |
| 32 | `0x437898` | zero16 `0x4d1298` | 0 | +1 |
| 33 | `0x43874c` | array `0x4d16d4` (28 × 8), empty | 0 | |
| 34–36 | `0x438b1c`, `0x43b35c`, `0x43e23c` | nothing | 0 | |
| 37 | `0x440444` | region ctor at `0x4d1a2c` | 3 | +3 |
| 38 | `0x440818` | words `0x49c454` ← `0xc000`, `0x49c456` ← 0, `0x49c458` ← 0 | 1 | |
| 39 | `0x44c02c` | dwords `0x49d240`, `0x49d244` ← 0 (file bytes already 0) | 0 | |
| 40 | `0x450458` | words `0x49d6c0` ← `0xc000`, `0x49d6c2` ← 0, `0x49d6c4` ← 0; GLBitmap ctor `0x4d1cf6` | 4 | +3 |
| 41 | `0x4531b4` | nothing | 0 | |
| 42 | `0x454178` | GLBitmap ctor `0x4d1e38` | 3 | +3 |
| 43 | `0x4554c4` | dword `0x4d1e78` ← 0 | 0 | |
| 44 | `0x45b8a4` | 28 signed bytes `0x49eb45..0x49eb60`, the key-direction pairs of [input.md](../reverse-engineering/input.md) (keys 0–7: (−1,−1) (0,−1) (1,−1) (−1,0) (1,0) (−1,1) (0,1) (1,1); keys 8–13: (0,−1) (−1,0) (1,0) (0,1) (0,−1) (0,1)) | 18 | |
| 45 | `0x45bda0` | nothing | 0 | |
| 46 | `0x45caf4` | the globals object `0x4d24bc` and both RNG streams, below | 256 | +4 |
| 47 | `0x45fba0` | nothing | 0 | |
| 48 | `0x4635a0` | zero16 `0x4d2c68` | 0 | +1 |
| 49, 50 | `0x464ad4`, `0x467824` | nothing | 0 | |
| 51 | `0x467b8c` | stage 2 `0x467b64` | 4 | |
| 52 | `0x469870` | array `0x4d4d74` (10 × `0x28`), empty | 0 | |
| 53 | `0x46a3c0` | **the only heap allocation**: `operator new(0x808)` (`0x47749c` → `0x47747c` → `_calloc(1, 0x808)`, then `[0x6bb30c]` += the block header, `0x811`); on success `0x469ddc(p, 0)`: dwords `p+0x800` ← 0, `p+0x804` ← 0 (the argument), then 256 pairs `p+8k` ← 0, `p+8k+4` ← `0x100`; `[0x4a08b4]` ← p (also when p = 0) | 2 + pointer; block | |
| 54 | `0x46a57c` | GLBitmap ctor `0x6b4f04` | 3 | +3 |
| 55 | `0x46b860` | stage 2 `0x46b1a8`, stage 5 `0x46b188`; zero ptr `0x6b4f30` (fire pool) | 9 | +1 |
| 56 | `0x47149c` | six arrays `0x6b4f7c`, `0x6b4f9c`, `0x6b4fbc` (4, 4, 3 × 8) and `0x6b4fd4`, `0x6b5004`, `0x6b5034` (4, 4, 3 × 12), empty | 0 | |
| 57 | `0x476c70` | array `0x6b7b70` (52 × `0x110`, ctor `0x476ccc`); each element constructs 10 light objects of `0x16` bytes at `+0x34` (`0x48bc40`: dword ← vtable `0x4a5b39`, two empty members): 520 dwords `0x6b7ba4 + 0x110·i + 0x16·j` ← `0x4a5b39`, i outer, j inner | 1,560 | +572 |
| 58 | `0x4775d0` | `0x476dd8(0x6bb2f8)`: the arena header dwords `+0`, `+8`, `+0xc`, `+4` ← 0 (BSS) | 0 | |
| 59 | `0x4785e8` | arrays `0x6bbd90`, `0x6c0890` (480 × `0x28`), empty | 0 | |
| 60 | `0x478dec` | nothing | 0 | |
| 61 | `0x479d4c` | `0x479bf8(0x6c56c8)`, the log object: calls `0x479f24` with type 0, whose jump-table entry is the epilogue `0x47a0ae`; no byte written | 0 | +1 |
| 62 | `0x480ad8` | an empty constructor on `0x6c5f50` | 0 | |
| 63 | `0x48dd88` | `matrix_ctor` on `0x6c5fb4`, `0x6c5fd4`, `0x6c5ffc`; empty constructors on `0x6c601c`, `0x6c6034`, `0x6c603c`; dword `0x6c604c` ← 0; `0x6c6054` ← 1, `0x6c605c` ← 0, `0x6c6060` ← 0; arrays `0x6c6ce2` (1000 × 12), `0x6c9bc2` (1000 × 8), `0x6cbb02` (32 × 8), empty | 1 | |
| 64 | `0x4956a4` | `0x495500(0x6cbc50)`: `CreateFileA(0x4a6a60, access 0, share 0, NULL, disposition 0, FILE_FLAG_DELETE_ON_CLOSE, NULL)` on the name `\\.\DARKMONO.VXD`; `[0x6cbc50]` ← the handle | 4 | +1 |

The counter rises by 616 over the init records and is back to 0 after the exit
records.

**After record 64** (confirmed in vitro) **2,454 DATA bytes** differ from the
file (480 in initialised DATA, 1,974 in the zero-filled part), of them the 65
done flags and 6 bytes of the c0w32 entry (`0x49706a` = `0x400000`,
`0x4a78a0` = `0x24` when the thread's StackBase−8 held 0, `0x6cbee4`); the
heap pointer `[0x4a08b4]`'s bytes depend on the address. Without flags and
entry that is the 2,379 bytes of the Vis column (411 in initialised DATA),
plus the 2 bytes of `[0x6bb30c]` = `0x811`, plus the 2,056-byte heap block of
record 53. The stage registry holds 31 entries, `[0x497074]` = 31.

**The stage registry in registration order**, which is the order in which
`0x401d94` calls them: stage 6 `0x401e5c` (record 1); record 3: 2 `0x405fac`,
5 `0x405ea4`; 4: 2 `0x4076e4`, 5 `0x4077e8`; 5: 2 `0x407b54`, 5 `0x407b3c`; 6:
2 `0x408860`, 5 `0x4083e0`; 7: 2 `0x4090cc`, 5 `0x408f9c`; 8: 5 `0x409904`; 9:
2 `0x409a34`, 5 `0x409a1c`; 10: 2 `0x40a060`, 5 `0x40a108`; 11–14: 2
`0x40a818`, `0x40ac38`, `0x40ade0`, `0x40b6e0`; 16: 2 `0x40fc8c`; 19: 2
`0x415464`, 5 `0x41544c`; 21: 2 `0x41fdb0`; 22: 5 `0x4215a8`; 23: 2
`0x422d90`; 27: 2 `0x428230`; 29: 2 `0x43034c`; 51: 2 `0x467b64`; 55: 2
`0x46b1a8`, 5 `0x46b188`. That is 19 stage-2, 11 stage-5 and 1 stage-6
entries, the 31 `call 0x401d64` sites of main_loop.md: every registration
happens during static initialisation, and the registry is fixed after it.

**State records (records 17, 18).** Entry k at `base + stride·k`: dword `+0` ←
a name pointer (`0x499c10` + offset for MECH, `0x499f98` + offset for FLYER;
strings in DATA); dword `+4` ← a constant; `+8..+0x17` ← `0x415028(+8, &w)`,
which writes the 16 bits of the word w as 16 bytes 0 or 1, bit 0 first; dwords
`+0x18..+0x38` ← the 9 dwords of the constant group `0x4998f8 + 0x24·k`
(MECH) or `0x499e9c + 0x24·k` (FLYER), copied from DATA (source and
destination do not overlap); MECH only: word `+0x3c`. The interleaving of these
stores varies between entries; no byte is written twice, so the order has no
effect. The file already holds most of these bytes (only 308 and 70 differ),
but the own code writes every field: a snapshot cannot tell a written byte
from an untouched equal one. Roles of the fields:
[vtables.md](../reverse-engineering/vtables.md#31-mech-states-0x4993a4-stride-0x3e).

| MECH k | name off | `+4` | w | `+0x3c` | MECH k | name off | `+4` | w | `+0x3c` |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0 | 0 | 0 | 3 | 11 | `0x78` | 10 | 9 | 3 |
| 1 | 9 | 10 | 1 | 3 | 12 | `0x87` | 10 | 1 | 3 |
| 2 | `0x10` | 10 | 1 | 3 | 13 | `0x96` | 10 | 9 | 3 |
| 3 | `0x1b` | 50000 | 2 | 0 | 14 | `0x9f` | 10 | 3 | 0 |
| 4 | `0x25` | 50000 | 2 | 0 | 15 | `0xa8` | 10 | 5 | 3 |
| 5 | `0x2e` | 50000 | 2 | 0 | 16 | `0xb1` | 50000 | 6 | 0 |
| 6 | `0x39` | 50000 | 2 | 0 | 17 | `0xc0` | 10 | 9 | 0 |
| 7 | `0x48` | 50000 | 2 | 0 | 18 | `0xc8` | 15000 | `0x12` | 5 |
| 8 | `0x58` | 10 | 1 | 3 | 19 | `0xd0` | 10 | `0x21` | 6 |
| 9 | `0x63` | 10 | 1 | 3 | 20 | `0xd9` | 0 | `0x21` | 6 |
| 10 | `0x6e` | 10 | 1 | 3 | 21 | `0xde` | 0 | `0x21` | 7 |

| FLYER k | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---:|---:|---:|---:|---:|---:|---:|
| name off | 0 | 9 | `0x13` | `0x1e` | `0x31` | `0x3a` | `0x43` |
| `+4` | 0 | 50000 | 5000 | 5000 | 500 | 5000 | 500 |
| w | 0 | 1 | 0 | 0 | 1 | 0 | 1 |

**The globals object (record 46, `0x45caf4`).** In this order: GLBitmap ctor
on the member `0x4d257f` (+`0xc3`); `_memset(0x4d24bc, 0, 0xc3)`; the fields
below in table order (offsets from `0x4d24bc`; roles from the documents
cited); the construction counter; then `0x492e88` on stream A `0x4d2599` and
on stream B `0x4d260b`: byte `+0x70` ← 24, byte `+0x71` ← 55,
`_memmove(stream, 0x4a6940, 0x70)`, and the empty `0x492f48`
([rng.md](../reverse-engineering/rng.md#initialization-state-writes-and-seed-sources)).
The frame counter `0x4d257b` (+`0xbf`) lies in the cleared range. Byte
`0x4d2538` is written here (`mov [ebx+0x7c], al` at `0x45cb64` with EBX =
`0x4d24bc`, AL = 0), so it starts at 0.

| Offset (address) | Width, value | Known role |
|---|---|---|
| `+0x4a` (`0x4d2506`) | dword 1 | open |
| `+0x0c` (`0x4d24c8`) | byte 1 | sound on; `-s` toggles it; the load passes it to `0x462410` ([launcher.md](../reverse-engineering/launcher.md)) |
| `+0x0e` (`0x4d24ca`) | dword 0 | open |
| `+0x56`, `+0x5a`, `+0x58` (`0x4d2512`, `0x4d2516`, `0x4d2514`) | words −1 | open |
| `+0x5c` (`0x4d2518`) | word 1 | view-preference toggle |
| `+0x32`, `+0x5e` (`0x4d24ee`, `0x4d251a`) | words 2 | view-mode word, camera mode word ([camera.md](../reverse-engineering/camera.md)) |
| `+0x7a`, `+0x7c`, `+0x7b` (`0x4d2536`, `0x4d2538`, `0x4d2537`) | bytes `0x72`, 0, 0 | `0x4d2536`: the language character (`-E`/`-F`/`-G` overwrite it, launcher.md); `0x4d2538`: [live_coverage.md](live_coverage.md) "Byte `0x4d2538`" |
| `+0xaa` (`0x4d2566`) | word 1 | video word (display transitions, display_modes.md) |
| `+0x82`, `+0x86`, `+0x8a`, `+0x8e`, `+0x92`, `+0x96` (`0x4d253e` … `0x4d2552`) | dwords 320, 200, 640, 400, 320, 400 | screen and DIB sizes (display_modes.md, launcher.md) |
| `+0xb0` (`0x4d256c`) | byte 0 | display-mode byte (display_modes.md) |
| `+0x7d` (`0x4d2539`) | byte 1 | open |
| `+0xa2` (`0x4d255e`) | dword 1 | display flag: screen-sized topmost window (launcher.md) |
| `+0xb5`, `+0xb6` (`0x4d2571`, `0x4d2572`) | bytes 0, 0 | invert pitch (input.md); pause flag (timing.md) |
| `+0xbb` (`0x4d2577`) | dword 13 | input mapping set (input.md) |

### What the result depends on

Only on the file and the table order: every value written is a constant in
the code, a copy of initialised DATA (records 17, 18, 46), or computed from
what an earlier store wrote (the registry count `0x497074`, the
class-registry count `0x4a1690`). Three things come from the platform:

1. **The heap address** of record 53's block, stored at `[0x4a08b4]`. The
   after-load snapshot pairs blocks by reference (section 3), so any address
   satisfies it. `[0x6bb30c]` = `0x811` needs CW3220's header word at `p−4`,
   which the own heap keeps. `0x47749c` takes the `0x47747c` path because
   `[0x6bb304]` is still 0 (BSS) at record 53. The block is never freed.
2. **The VxD handle** `[0x6cbc50]` (record 64). The real `CreateFileA` with
   record 64's arguments returns `INVALID_HANDLE_VALUE` with error 87 on
   Windows 10 19045 in a 32-bit process under WOW64 (the in-vitro harness's
   `PLATFORM` probe); hypothesis: −1 on every NT system (no VxD; disposition
   0 is not a valid creation disposition there), other systems not measured.
   Its readers are `0x4955c4` (only with `-m`, which no suite passes) and exit
   record 25 (no `CloseHandle` for −1).
3. **The platform calls** of the whole initialisation, in order:
   `GetModuleHandleA` (the entry), `_memset` and two `_memmove` in record 46,
   `_calloc(1, 0x808)` in record 53, `CreateFileA` in record 64 (confirmed in
   vitro: the same import-level calls on both sides).

No init record reads the command line, a file, the clock or an RNG stream
(record 46 only seeds them), and none reads what a later record writes. The
order still matters for the stage registry (entry order = dispatch order) and
the class registry (the grid table is pair 0). No record reads its own stores
except the counts `0x497074` and `0x4a1690`.

### The 26 exit records

The table [rtl_imports.md "The 26 exit records of DBSIM"](rtl_imports.md#the-26-exit-records-of-dbsim-static)
is the contract; its function list matches the bytes (the same 26 functions
in the same order), and every store is confirmed in vitro. They run after the
atexit functions, in reverse table order (record 25 first). Pairing with the
init records: 0–9 and 22 delete the pools whose pointers records 3–10, 19, 22
and 55 zeroed (the load filled them; records 0 and 7 call `delete` also for
0); 10 the `mission.str` object (record 24); 12, 13, 20 the zero16 objects of
records 31, 32, 48; 16, 17, 18, 21 the GLBitmaps of records 40, 42, 46, 54
(afterwards `0x4d1cf6`, `0x4d1e38`, `0x4d257f`, `0x6b4f04` hold vtable
`0x4a3368`); 23 the light array of record 57; 24 the log object (record 61);
25 the VxD handle (record 64); 11 and 14 only lower the construction counter
(the regions of records 28 and 37); 15 and 19 act on function-local statics
constructed on first use (guard bytes `0x49cc26`, `0x49f1ec`, `0x49f032`),
not by an init record. After the exit records 2,480 DATA bytes differ from
the file (the 26 exit flags added).

**Nothing observable depends on them.** They write the construction counter,
vtable words inside the destroyed statics, `[0x6bb30c]` and CW3220's free
lists; no file, no tick-log or `results.dat` value; the exit code is fixed
before they run
([launcher.md](../reverse-engineering/launcher.md#exit-code)).
Their one platform call, `CloseHandle`, does not happen for −1; `_free` is
never called when the pool pointers are 0.

### What the mission load and the frame read (static)

From the listing: every instruction with an absolute operand in a written
range, its function, and the DBSIM map's counts ("×n" = live in n of the 40
suites). Records that write nothing, or only zeros into zero memory, feed
nothing beyond "the field starts at zero".

| Record | Global | Read by | When |
|---|---|---|---|
| 22 records | stage registry `0x4a804c`, count `0x497074` | `stage_dispatch` `0x401d94` (core `render_root.c` walks the table in guest memory) | stages 0 and 1 in the game main, 2 inside the load, 3 after it; 4, 5, 6 every frame |
| 17, 18 | MECH and FLYER state records | installed by address in the load (`script_start`, mission_load.md) and in AI code (vtables.md §3); read through the actor's state pointer by `mech_move`/`mech_act`/`mech_decide` `0x415afc`/`0x415b38`/`0x415b74`, `flyer_move_call` `0x4217fc` and the state timer `0x411cec` (byte `+8`); core (`ai_behaviour.c`, `damage.c`) uses the record addresses as constants | load, every frame |
| 46 | RNG streams A `0x4d2599`, B `0x4d260b` | `rng_next`/`rng_bounded` | load, every frame |
| 46 | globals `0x4d24bc` (table above) | the parsers (`-s`, `-E`/`-F`/`-G`, …), window and DIB set-up (sizes, display flag), camera and view keys, input mapping, pause | start-up, load, frame |
| 44 | key-direction table `0x49eb45` | `key_axes` `0x45a4cc` (core `key_axes.c` reads it) | frames with keys |
| 25 | class registry `0x6c5834`, count `0x4a1690` | `dts_load` `0x47a144` (core `asset_load.c`) | load |
| 53 | `[0x4a08b4]` (the `0x808`-byte table) | `0x469c64` polygon clip (×40, 1000 per suite), `0x46a03c` texture table (loaders) | load, frame drawing |
| 28 | rectangle `0x49ac54..0x49ac60` | `0x429660` cockpit object, once per run (×40): shifts the four values left | load |
| 28, 37 | regions `0x4cfa7a`, `0x4cfc9c`, `0x4d1a2c` | HUD strip save and restore (display_modes.md) | frame drawing |
| 40, 42, 46, 54 | GLBitmaps `0x4d1cf6`, `0x4d1e38`, `0x4d257f`, `0x6b4f04` | through their vtables | frame drawing |
| 57 | light array `0x6b7b70` | DTS draw state `0x4760e4`, `0x47613c` (×39–40) | frame drawing |
| 38, 40 | words `0x49c454`, `0x49d6c0` | addresses passed by the MFD page constructor `0x4404bc` and the HDD map constructor `0x44c28c` (once per run, ×40); `0x49c454` read by the MFD page draw `0x44060c` (×1) | load, frame drawing |
| 39 | `0x49d240`, `0x49d244` | cockpit panel state machine `0x44b3dc` (adds and subtracts, ×15) | frame |
| 30 | `0x49b06e..0x49b082`, `0x4d03ac..0x4d03bc` | `0x49b07e` is the default rectangle of the text routine `0x43122c` (×40); no other absolute reference (readers through base pointers not traced, open) | frame drawing |
| 43 | `0x4d1e78` | its address stored by the alert dialog set-up `0x4541b8` (×19) | dialogs |
| 63 | `0x6c6054`, `0x6c604c` | the rasterizer state functions `0x474ec0..0x48d624` (×7–39) | frame drawing |
| 64 | `0x6cbc50` | `0x4955c4` (`-m` only), exit record 25 | never in the suites |

So the **simulation** (what the tick log and the watches compare) reads from
static initialisation the stage registry, the AI state records, both RNG
streams, the key-direction table, the class registry and some fields of the
globals object; everything else written here feeds drawing (the *headless*
question, section 13 question 4) or start-up code that overwrites it.

**The class registry.** Only the grid table `0x49aa7c` is registered by an
init record (25), so pair 0 is always the grid table. The GL table is
registered by `0x480ef4`, called from `0x464d5c` (start-up) and `0x480be0`; TS
by `0x48dd08` and AN by `0x4913dc`, called by the mission load at
`0x4617ac`/`0x4617b1` (call sites from a rel32 scan of CODE;
asset_loading.md "Class registry" lists the routines).

**Open.** The roles of `0x4d2506`, `0x4d24ca`, `0x4d2512..0x4d2516`,
`0x4d2539` and of record 30's values (readers through base pointers not
traced); the VxD result on other Windows versions.

## The static initialisation as own code

DBSIM's start-up before WinMain as own code: the c0w32 entry `0x401000`, the
65 init records, the 26 exit records. **Confirmed in vitro**
(`//tools/reference-game/invitro:differential_init_records_test`); not
checked live.

| Piece | Where | Contract |
|---|---|---|
| c0w32 entry `0x401000..0x40106e` with `0x496425(0)` and `0x4964e2` | `es2_sim_c0w32_entry` | `[0x49705d]` ← `[0x497059]` << 2; BSS `0x4a7bfc..0x6cbee7` zeroed; `[0x497061]`, `[0x497065]` ← the dwords at StackBase−8, −4; `[0x6cbee4]` ← StackBase−4; the byte `0x497058` is 0 in the file, so `0x4964e2` runs: `[0x4a78a0]` ← `[StackBase−8]` + `0x24`; StackBase−8 ← `0x4a7884`; `[0x6cbee4]` ← StackBase−4 again; StackBase−4 ← 0 (the construction counter); `[0x4a7888]` ← 0; `[0x49706a]` ← `GetModuleHandleA(NULL)`. The branch for a nonzero `0x497058` (GetProcAddress of `__GetExceptDLLinfo`) is refused, not modelled |
| 65 init and 26 exit records | `sim/src/init.c`, one own function per record VA | the record tables above, every store in the original's order, the construction counter `[[0x6cbee4]]` moved as the original moves it |
| the selector | `es2_sim_run_next_record` | CW3220's `0x41a634` as the runtime model `es2_rtl_select_record` (confirmed against CW3220) over the module table `0x49702c` in guest memory: the record's done flag ← 1 before its function runs; a VA without own function is refused through the dispatch |
| platform services | `sim/src/services_platform.c` | `CreateFileA` (thunk `0x4968fe`, stdcall 7): −1 headless without calling the OS (the original's result on NT, not a deviation), the call recorded as a `{"event":"platform","call":"CreateFileA",…}` line; `CloseHandle` (`0x4968b0`): refused (the headless platform opens no handle); `GetModuleHandleA` (`0x496898`) with NULL: `0x400000`, a name refused |

Every call the records make to other code goes through `es2_sim_call`:
`operator new` `0x47749c` (record 53), `delete`/`delete[]` (exit records),
`_memset`/`_memmove` thunks (record 46), the three KERNEL32 thunks. The
constructors that store nothing (the array element constructors of records 0,
2, 19, 21, 27, 28, 33, 52, 56, 59, 63 and `matrix_ctor` in records 2 and 63)
are not called: in vitro their records change no byte beyond the done flag and
do not move the counter.

**StackBase in `es2sim.exe`** is the simulation thread's `fs:[4]`: the entry
keeps the exception context at StackBase−8 and the counter at StackBase−4
there, as the original does on its main thread. The two dwords the entry saves
are the thread's.

**Order in the own process:** `es2_sim_c0w32_entry`, then `__startup`'s part
before the records (`__fpreset`, the SEH record, environment and command line,
CW3220 variables), `es2_sim_run_init_records`, `GetModuleHandleA(NULL)` and
WinMain (WinMain's hInstance comes from CW3220's own call after the records),
at exit the atexit functions then `es2_sim_run_exit_records`, then
`ExitProcess` ([rtl_imports.md](rtl_imports.md) "Exit paths"). `[0x6cbee4]` is
set from the entry on, so the `0x49630c` service does not refuse.

**The in-vitro acceptance** (`--init-records`, `init_records_cases.h`) runs
two passes from the same start (DATA `0x497000..0x6cc000` as the file maps it,
the two dwords below the worker thread's StackBase, an empty heap model over
one emulated OS region at the same address, FS:0):

- **original:** `0x401000` as machine code (its `jmp` into `__startup`
  returns to the harness through a stub), then the selector model picks and
  the harness machine-calls the 65 init and the 26 exit records. The import
  slots on the path point at logging stubs (`_memset`, `_memmove`, `_calloc`
  and `_free` over the heap model, `CreateFileA` −1, `CloseHandle` refused,
  `GetModuleHandleA` `0x400000`, `__startup`); every other slot stays a trap.
- **own:** `sim/src/init.c` over the identity view with the real sim services
  through `es2_sim_call`, the same heap model at the same region.

After each of the 92 steps a hash of DATA, StackBase−8/−4 (exception context,
construction counter), the heap's head words and the first 64 KiB of the
region is compared, plus the selected VA; at the end the import-level call
logs and the `CreateFileA` arguments: **92 of 92 steps identical**. The
original pass prints per step the bytes it changed and the counter delta (the
per-record Vis and Ctr columns above). Not compared: CW3220's own variables
(the selector's are the runtime model's on both sides), the frame records
`0x49630c` writes on the stack (the own code calls no `0x49630c`; FS:0 is the
same after every step), and the harness's worker stack below the two words.

## Start-up, parsers and file services

argv, both parsers, `drive.cfg`, `error.str`, the VOL enumeration, the file
services and `-l` as own code. The contract and its corrections are in
[launcher.md](../reverse-engineering/launcher.md#dbsims-start-up-as-own-code).

| Piece | Where | Contract |
|---|---|---|
| file services | `sim/src/services_files.c`, `include/es2/sim/files.h`; 17 rows (`_fopen`, `_fclose`, `_fread`, `_fwrite`, `_fseek`, `_ftell`, `_rewind`, `_fscanf`, `_open`, `_close`, `_read`, `_write`, `_lseek`, `_filelength`, `_findfirst`, `_findnext`, `_flushall`) | the runtime file model (`core/src/rtl_io.c`, confirmed in vitro against CW3220) over CW3220's variables in a runtime region of their own (handle table, FILE array, errno, find list, the two constant tables), CW3220's own blocks (stream buffers, find nodes) from the game's heap, as in the original ([The heap trace](#the-heap-trace)); headless Win32: the file API on the working directory, `GetStdHandle` NULL and an empty `GetStartupInfoA` (a GUI process without a console), no clock calls. EAX as the model; ECX/EDX left as at the call (rtl_io.h: no DBSIM caller reads them). A call outside the model (an `fscanf` float, …) is refused, never approximated |
| `0x45ee00` | the same file, one row | install root + path in `0x4d285c`; EAX the buffer, ECX 0, EDX the appended length + 1 (strcat's registers) |
| start-up | `sim/src/startup.c`, `sim/src/volumes.c`, `include/es2/sim/startup.h` | CW3220's call of WinMain, WinMain, both parsers, the game main up to the load, the VOL registration with its stream objects, the fatal report, WinMain's tail (the queue with the posted `WM_QUIT`, then the own pump until `WM_QUIT`) |
| the read stream and the buffered stream | own code inside `volumes.c`, not service rows | the config adapters (`prefs_load`, `prefs_store`) reach `0x47ad40`, `0x47af98`, … through their bridges (the rows are the input layer's) |
| `es2sim --headless` | `sim/src/main.c` | after the init records: the file services, then the start-up with DBSIM's command line from `--arguments`, else `[game] command_line`, else `-eggplant -X2 -R0 -G` plus `[game] arguments` (the oracle runs' line, `OracleSpeed.psm1`); outcomes: WinMain returned (exit code `[0x4d27b8]`, reason `winmain_returned`), fatal report (1, no exit records, `fatal_error`), a refusal (70) |

**Frame area (decision).** The original's stack locals (WinMain's 27 argv
slots, the game main's `0x1c4`-byte frame with the two `drive.cfg` buffers,
the error.str holder and the string arrays at their offsets, the
registration's find block, the archive open's locals, the fatal report's two
buffers, the VOL-aware stream S of `0x453878`) live in a host-provided area at
the same relative offsets, zero-filled (section 5: no residue is read before
it is written, except the `-eggplant` message box's string pointers when
error.str has fewer strings). Call arguments are copied there too, so the
runtime models that read slots by guest address work in `es2sim.exe`
(identity) and in the tests' arena (an identity-mapped range).

**The files DBSIM opens** (section 13 question 5; by the own start-up and the
contracts it calls, file_io.md): `DATA\DRIVE.CFG` (fatal when missing; its
second line is the install directory, and from 80 characters on it overwrites
the CD root as in the original), `DATA\PREFS.CFG` (WinMain's `prefs_video`;
when missing, init record 46's display flag asks for DirectDraw, refused
headless), `VOL\*.VOL` and `<CD root>VOL\*.VOL` (enumerated; a name already
registered from the install is skipped), `ERROR.STR` (loose or in a VOL; when
missing the start-up goes on), `assert.log` (written by the fatal report); with
`-r`/`-p`/`-D`/`-d` the tape and dump files and the seven tape-header files
(`TAPES\`, `DATA\RESTORE.DAT`, `DATA\OBJECT.STR`); later, in the load and the
run: the four launch files, `DATA\KEYJOY.CFG`, `DATA\SOUND.CFG`,
`DATA\LANGUAGE.CFG` (only on F1 help), the voice WAVs below the CD root, and
the return files. `DATA\MAPLABEL.STR`, `MAT0.DAT`, `MFORMS.DAT` and `SAV\` are
VSHELL's (file_io.md, VSHELL call sites) and `DATA\EXIT.CFG` is named by none
of the three programs (file_io.md, Open questions): DBSIM opens none of them.

**Acceptance.** Confirmed in vitro
(`//tools/reference-game/invitro:differential_startup_test`,
`startup_cases.h`): the original's machine code against the own code from the
same state (DATA `0x497000..0x6cc000` with the construction counter, the
emulated heap region, the import-level call sequence) on random command lines
(every letter of both parsers except the tape options, numbers, signs,
separators, up to 26 arguments, `-v` through the original `prefs_video` on
both sides) through the split, the early and the main parser (argc and argv
too); string holders (two groups, with and without the second output,
truncated buffers) through `0x4539a0`/`0x453918`; the VOL registration
`0x473260` on the CD's `VOL\` (masks 1, 0x100, 0x101, a second root that
repeats every name, no second root: 9, 5, 13, 9 archives) and on synthetic
archives (ties and the carry, a foreign mask, two wrong signatures, tables
over several 0x400-byte windows, no directories, a second root with a new and
a repeated name, both pass orders), CW3220's handle flags and FILE array too.
Unit `//sim:startup_test` (the deferred stop, the load call with the DIB
section built, the `-eggplant` refusal and its message box, tape record and
playback with the prefs copy, the memory paths, the fatal report and
`assert.log`, the DirectDraw and argument-count refusals, the `drive.cfg`
quirks, the file services); `//sim:es2sim_real_test`: the exit codes 1, 0 and
1 of the first three rows of launcher.md's table.

**Open.** The DirectDraw switch is refused, not modelled. The window failure
path (`LoadStringA` 1000/1001 and the message box) is not reachable headless.

## The main loop, the journal and the exit

The game main and loop, the pump over `//sim/oracle:journal`, MainWndProc's
handler list, the limiter and clock calling section 7, the mission end, the
exit code and the DIB region. The contract of the own code is in
[main_loop.md](../reverse-engineering/main_loop.md#the-main-loop-pump-window-procedure-limiter-and-clock-as-own-code);
confirmed in vitro (`//tools/reference-game/invitro:differential_loop_test`).

| Piece | Where | Contract |
|---|---|---|
| the loop and its pieces | `sim/src/loop.c`, `include/es2/sim/loop.h` | one context `es2_sim_loop` (guest view, dispatch, virtual time, journal, headless platform record, a call area for own calls' slots, the tick callback, the posted-message queue, CW3220's atexit table) |
| the DIB section's objects | `sim/src/frame_buffer.c` | `0x464d44` and its graphics-library callees; the bits a host region (`es2sim`: 1280 × 960 bytes) that the descriptor `[0x4d3020]+0xa` and the row table point into |
| service rows | `dispatch/services.h` | `abort_notify_add` `0x479cfc`, `rt_atexit` (thunk `0x49675a`), `clock` `0x467828`, `timing_init` `0x467840`, `limiter` `0x4678c0`, `pump` `0x464c78`, `handler_add` `0x465fec`, `handler_remove` `0x466010`, `surface_notify` `0x480f30` (empty), over the bound loop (`es2_sim_loop_bind`) |
| the start-up's own steps | `es2_sim_startup.dib_section`, `.after_load`, `.quit_pump` (`startup.h`) | the DIB section as own code; after the load the game main's rest (the loop, the tail, the holder); WinMain's tail pumps through the own pump until `WM_QUIT`; outcome `ES2_SIM_STARTUP_STOPPED` |
| tick records | `es2_sim_tick` (`host.c`), `es2_tick_record_input` (`tick_record.c`) | section 9 |
| `WM_CLOSE`/`WM_QUERYENDSESSION` | `loop.c` | the demo word stops the loop; else the key dispatcher `0x45feb8(0x410)` through the dispatch and its answer at `[0x4d370c]` |
| `es2sim --headless` | `sim/src/main.c` | `[input] mode=replay` + `journal` loads the journal; `[run] frame_limit=n` stops after the tick of frame n − 1 (exit record reason `frame_limit`, exit code 0); WinMain's return runs `es2_sim_exit` (the atexit functions last first, the 26 exit records, the stream exit hooks, `_flushall`) and exits with `[0x4d27b8]` (reason `winmain_returned`); the exit record's `frames` is the number of tick records |

**The exit code path** (section 13 question 6): the mission end's code through
the hook `mission_end`, WinMain's return, then `exit` as rtl_imports.md
describes it.

## The input layer

The input services, headless and over SDL3. The contract of the own code is in
[input.md](../reverse-engineering/input.md#the-input-layer-as-own-code);
confirmed in vitro (`//tools/reference-game/invitro:differential_input_layer_test`),
unit `//sim:input_test`.

| Piece | Where | Contract |
|---|---|---|
| the input layer | `sim/src/input.c`, `include/es2/sim/input.h` | the set-up `0x45a5dc` with its devices, hot keys, flight-key capture, prefs read and joystick; the device poll `0x45baa8` and its readers; the mouse handler, event and queue; one context `es2_sim_input` over the bound main loop (guest view, dispatch, journal, call area) |
| the provider seam | `es2_sim_input_platform` | `GetKeyState`, `joyGetDevCapsA`, `joyGetPosEx`, `GetCursorPos` and the client size, keyed by the frame counter; `es2_sim_input_headless` answers from the journal with the proxy's replay semantics (section 8) and the game size; the windowed host fills the same structure from SDL3 |
| service rows | `dispatch/services.h` | the input functions guest code reaches (the set-up from `mission_root`; the poll, key reader, joystick object and summary from `control_record`, `flight_model`, `player_input`, `command_key`, `slider_event`; the mouse handler from the window procedure; the queue set-up, bounds and flush from the root), the prefs setter and five callbacks, the empty hot-key callback, two sound flags, two empty constructors, the array constructor `0x49667c`, the surface attribute `0x481350`, seven imports, and the read and write streams for the prefs adapters' bridges (`0x47ad40`, `0x47ad78`, `0x47af98`, `0x47b064`, `0x47b09c`, `0x47b2b8`; imports `0x496910`, `0x49694c`, `0x49695e`, `0x49696a`, `0x496970`, `0x496a66`, `0x496a6c`) |

`GetPrivateProfileStringA` reads the named file from disk. The joystick suites
reach `prefs_store` through the write-stream rows and write `DATA\PREFS.CFG`
in the replay's installation, as the original does. **Platform deviation:**
headless `GetClientRect` answers the game size (input.md "Headless platform");
the recording machine's client area is open.

## The mission load

The mission-load root and its pieces run in `es2sim` without the original's
code for the functions they reach; the contract per function is in
[mission_load.md section 33](../reverse-engineering/mission_load.md#33-the-mission-load-in-the-own-process).

| Piece | Where | Contract |
|---|---|---|
| the load services | `sim/src/load.c`, `include/es2/sim/load.h` | rows for the graphics context, palette and loading screen, arena, sound init up to the driver, renderer reset and classes, shape loaders, stage-2 callbacks, the world and actor load callees, the memory stream, trivial bodies (among them `0x47a288`, `0x47a2c8`, `0x47a308`, `0x47a3b4`, `0x47a540`, `0x47a888`, `0x47a8a4`, `0x47a8f4`, `0x47b36c`, `0x47b3a0`, `0x47b400`, `0x47b410`, `0x47b454`, `0x47b45c`, `0x47b4b4`, `0x4800c0`) |
| the display seam | `es2_sim_load_platform` | the palette answers, palette handle and paint DC, the colour table and present count; `es2_sim_load_headless` fills the headless answers |
| the snapshot writer | `sim/oracle/snapshot.{h,c}` (`//sim/oracle:snapshot`) | ES2SNAP1 version 2 as the proxy writes it |
| `es2sim --headless` | `sim/src/main.c`, `loop.h` (`before_logic`) | `[snapshot] after_load=1` writes the snapshot before the first logic frame |

`matrix_apply` `0x4800c0` re-reads the input per output word (an in-place call
differs) and returns out + 6 from its copy path (confirmed in vitro,
`differential_view_test`). Headless display answers, the blit and present
without pixels and the terrain shading without its renderer-only tables are
platform deviations (mission_load.md section 33, "Headless decisions").

## Audio

The audio code the mission load and the frame reach runs as own code; the SOS
driver DLL is replaced by a device seam. Contract:
[audio.md](../reverse-engineering/audio.md#audio-in-the-own-process);
confirmed in vitro (`//tools/reference-game/invitro:differential_audio_test`),
unit `//sim:audio_test`.

| Piece | Where | Contract |
|---|---|---|
| the audio services | `sim/src/audio.c`, `include/es2/sim/audio.h` | rows for the driver load, the sound table, the manager rows the sfx hooks bridge to, the voice and lip rows, CD audio, the window handler, pause and resume, the prefs groups, the close, the cockpit sound step |
| the device seam | `es2_sim_audio_device` | the 20 SOS entry points DBSIM calls and `mciSendCommandA`; `es2_sim_audio_silent` headless; the `audio_sdl` sink windowed ([audio.md](../reverse-engineering/audio.md)) |
| the platform | `es2_sim_audio_platform` | GlobalAlloc/GlobalFree (headless host memory), GetVersion, the recorded SetLastError |

**Platform deviations:** SOS9503.DLL is not loaded (its 97 entry slots stay 0,
the module handle is a stand-in); headless there is no CD audio (`MCI_OPEN`
fails) and the samples' playing time is measured on the virtual clock
(audio.md "Headless decisions").

## The HUD line

The HUD and cockpit construction calls of the mission load headless (state
yes, drawing no) and the HUD control flow with its coupling calls (clock
`0x467828`, stream B `0x492ee0`/`0x492f24`, the sound API) in the original
order. Contract:
[hud.md "The HUD line in the own process"](../reverse-engineering/hud.md#the-hud-line-in-the-own-process).

| Piece | Where | Contract |
|---|---|---|
| the HUD services | `sim/src/hud_load.c`, `hud_cockpit.c`, `hud_gauges.c`, `hud_display.c`, `hud_draw.c`, `hud_state.c`, `include/es2/sim/hud.h` | rows for the map set-up, cameras and fonts, the cockpit panel and every element it builds, the display modes, the elements' full draws, the per-frame state the hooks call |
| the HDD map tail, the radar draw | `sim/src/hud_draw.c` | rows `0x45030c`, `0x44e954`, `0x44f1bc`, `0x44c250`, `0x434280`, `0x43eef4`, `0x434228`; `0x44e334` runs to its end |
| the load root's last steps | `sim/src/hud_load.c` | rows `0x45d490`, `0x45d50c` |
| the per-frame row-1 walk | `sim/src/hud_frame.c` | rows `0x432924`, `0x43408c`, `0x434194`, `0x4349ac` and the leaves `0x438700`, `0x41b514`, `0x42ea70`, `0x42ea94`, `0x42d82c`, `0x42d854` (hud.md "the map draw's tail, the load root's last steps, the radar draw and the per-frame row-1 walk") |
| the coupling trace | `es2_sim_hud` (`hud.h`) | per coupling call: kind, original site, target, argument, EAX; kept by `//sim:hud_test` (the site table), counted by `es2sim` |

The cursor placement of the dialogs makes no `SetCursorPos` headless (counted
in `es2_sim_hud.headless`).

## The view transform, the projection and the lights

The "writes no pixels" module of the R3D line (the per-frame view set-up, the
view and object selection, the projection leaves, the light objects, the
active light list, the polygon and terrain lighting, the light table) as
service rows, exact headless. Contract:
[rasterizer.md](../reverse-engineering/rasterizer.md#view-transform-projection-and-lights);
confirmed in vitro (`//tools/reference-game/invitro:differential_view_test`,
`view_cases.h`, `--view`: the original's machine code against the own rows on
random worlds; EAX, named EDX/ECX, the world, DATA and the heap compared).

| Piece | Where | Contract |
|---|---|---|
| the view services | `sim/src/view.c`, `include/es2/sim/view.h` | `0x48c2e4`, `0x48c3b0`, `0x48c444`, `0x48c57c`, `0x48c5cc`, `0x48c5fc`, `0x48c6d0`, `0x48d7ec`, `0x48db12`, the lighting sums, the active list, the light objects, the light table and 27 leaves; the hooks (`matrix_ctor`, `vector_subtract`, `euler_matrix`, `matrix_compose`, `matrix_transpose`, `rotate_point`, `transform_point`, `matrix_point`, `dist3d_forward`, `vector_angle`) and the load's rows (`new`, `delete`, `0x48bc40`, `0x48be50`, `0x48bcec`, `0x4800c0`) are called through the dispatch |

No deviation: the module writes no pixels, every write is kept. The original's
faults at a zero divisor (the projection with depth 0, the lighting and
light-update divisions) end the process as the original does
([The original's fault paths](#the-originals-fault-paths)).

## The modal dialogs

The modal dialogs (the mission-end alert, the quit confirmation, the options
dialog) headless, answered by the journal's pump-keyed entries, with the clock
calls in the original order. Contract:
[main_loop.md](../reverse-engineering/main_loop.md#the-modal-dialogs-in-the-own-process);
confirmed in vitro (`differential_dialogs_test`: alert, options, rows and
wndproc cases), unit `//sim:dialogs_test` (the clock order on a synthetic
image).

| Piece | Where | Contract |
|---|---|---|
| the dialog services | `sim/src/dialogs.c`, `include/es2/sim/dialogs.h` | rows for the modal loops, open/close, step, keys, focus, presses, release list, selects, the options' step/label/page/store, the prefs toggles and cycles, the ports' pause and resume, the key flag, the device, the panel rows, the accessors |
| the pass counter | `es2_sim_dialogs` (`dialogs.h`) | `passes` per modal pass; `pass_limit` refuses a dialog that does not end (0: none; `es2sim` uses none) |

The second options page is refused by name; the empty routines of the cursor
restore are not called (main_loop.md "Headless decisions"). **Open:** the
dialog constructors, the destructor, `0x45cff0` and the key dispatcher
`0x45feb8` as own code; the second options page.

## The DTS shape draw slots

The shape instance slot, the shape draws, the part transforms, the part kinds
(list, group, cells, detail, BSP), the solid, lit and Gouraud polygons with
back-face test, projection, near-plane clip and fill dispatch, and the actor
draws with their detail levels, as service rows, exact headless. Contract and
open points:
[rasterizer.md](../reverse-engineering/rasterizer.md#dts-shape-draw);
confirmed in vitro
(`//tools/reference-game/invitro:differential_shape_draw_test`,
`shape_draw_cases.h`, `--shape-draw`: the original's machine code against the
own rows on synthetic shapes; registers, DATA, the world and the stubs' call
log compared), unit `//sim:shape_draw_test`.

| Piece | Where | Contract |
|---|---|---|
| the shape draw services | `sim/src/shape_draw.c`, `include/es2/sim/shape_draw.h` | the hooks (`matrix_compose`, `euler_matrix`, `angle_ratio`, `heading_cos`, `multiply_q10`, `dist3d_delta`, `matrix_point`), the view rows, the fill layer, the haze table, the shade lookup and every part's virtual slot are called through the dispatch |

## Files, streams, math and runtime leftovers

The leaf methods of the three stream classes, the arena set-up and block test,
the pure math leaves between `0x47db4d` and `0x480590`, `0x492b04`, and the
area-less leaf `0x46b850`, as service rows, exact headless. Every fact below
was checked against the listing and is **confirmed in vitro**
(`differential_runtime_leaves_test`, `tools/reference-game/invitro/runtime_leaves_cases.h`:
original machine code against the rows, random entry EAX/EDX/ECX; registers,
DATA `0x497000..0x6cc000`, the heap and the stub log compared).

| Piece | Where | Contract |
|---|---|---|
| the math and arena rows | `sim/src/services_rtl.c`, rows in `sim/dispatch/services/runtime_leaves.h` | `0x476df4` arena set-up, `0x476e2c` block test, `0x46b850` field, `0x47db4d`/`0x47dba4`/`0x47dbca` dot and cross products, `0x47ddcd` dot sign, `0x47de2d`/`0x47eabe` 64-bit divide, `0x47df04` word swap, `0x47df84`/`0x47eaa1` 64-bit multiply, `0x47df9c`/`0x47ea92` integer root, `0x47ea64` vector length (`vector_length_int`; `0x492a7d` is `vector_length`), `0x47e0c0` Q14 multiply, `0x480050` affine map, `0x480590`/`0x4805b4` the slot table at `0x6c5eb4`, `0x47eadb` double constructor, `0x492b04` signed divide |
| the stream rows | `sim/src/services_files.c` | base class `0x47a874`, `0x47a8d4`, `0x47a8fc`, `0x47a904`, `0x47a90c`, `0x47a914`; read-from-handle class `0x47adb4`, `0x47ae28`, `0x47aeb0`, `0x47aecc`, `0x47af58`, `0x47af60`, `0x47af84`; buffered class `0x47c684`, `0x47c754`, `0x47c78c`, `0x47c7bc`, `0x47c7d8`, `0x47c818`, `0x47c82c`, `0x47c8b0`, `0x47c8c4`, `0x47c95c`, `0x47c984`; the detach thunks `0x47b5cc` and `0x47b9cc` |
| the grid cell counter `0x470db8` | `sim/src/terrain_draw.c` (rasterizer.md, the terrain draw) | byte add, EDX the index, ECX's low byte the count; the suite tests this row |

Every row leaves EAX, EDX and ECX as the original does. The cases where the
listing is not obvious:

- **Stream classes.** Base `0x4a16e8`: `+4` status. Read-from-handle
  `0x4a1738`: `+4` status, `+8` handle word (`0xffff` none), `+0xa` open
  byte. Buffered `0x4a1ad8` (unaligned dwords): `+4` status, `+8` inner
  stream, `+0xc` size, `+0x10` position of the buffer's first byte, `+0x14`
  buffer, `+0x18` owns-buffer byte, `+0x19` valid bytes, `+0x1d` cursor.
  Status 6 is "detached" (`0x47c818` is `status != 6`; `0x47c7bc` returns 0
  then). Listing `0x47a874..0x47a914`, `0x47adb4..0x47af84`,
  `0x47c684..0x47c9c8`; the vtable words from the relocated data at
  `0x4a16e8`, `0x4a1738`, `0x4a1ad8`.
- **The refill `0x47c8c4`** seeks the inner stream (slot +8) to the position,
  reads `size` bytes through slot +0x18 and on a false read asks the inner
  status (slot +4 of the object, `2` is the end of the data): at the end the
  valid count becomes `tell - position` (clamped to what is not 0 and not
  beyond the buffer; `tell == position`, tell ahead and short), otherwise the
  buffer is marked failed (`0x47c8b0`). The inner stream's methods are called
  through the loop, the copies through the memcpy import `0x49680e`.
- **The buffered read `0x47c82c`** loops: copy what is buffered, refill at the
  buffer end, go on; ECX leaves as the running destination because the
  prologue's `push ecx` is the local `[ebp-4]` and the epilogue's `pop ecx`
  restores it from there. EDX is the last callee's. There is no round cap; it
  loops as the original does.
- **The detach thunks `0x47b5cc` / `0x47b9cc`** call slot 4 (+0x10) of the
  object's own table with the object as `this` and return its answer.
- **`0x470db8`:** the index is `(p - [obj+0xec]) >> 4`, a logical shift
  (listing `0x470dc3`); the suite draws `p >= [obj+0xec]` only (a larger `p`
  below the base would index far outside the counter array and fault).
- **`0x47ddcd`:** the sign is `SF != OF` of the last `adc`, so the 64-bit sum
  wraps as the original's does; computed without a 128-bit type (the target
  has none). **`0x47df9c`:** executed step by step, including the `jcxz` on CX
  and the shift counts `CL & 31`; for `n == 0` ECX is the entry value.
- **`0x480590` / `0x4805b4`** use the 32-bit slot table at `0x6c5eb4` (index
  `0..0x2ff` in the suite). **`0x476e2c`** compares signed (`[hdr] + [hdr+4]`
  can pass `2^31`).
- **Operand aliasing:** cross product (`out == a` or `out == b`), affine map
  (`out` equal to `m` or `w`) and the 64-bit divide (`out == num`) read and
  write in the listing's order.
- **Divide faults.** `0x47de2d`/`0x47eabe` (zero divisor or a quotient beyond
  32 bits) and `0x492b04` (zero divisor or `INT_MIN / -1`) end the process
  with the original's exception code
  ([The original's fault paths](#the-originals-fault-paths)); the suite draws
  only operands that do not fault.
- **The buffered stream rows' callees** (memcpy, lseek, close, free, operator
  delete and the inner stream's methods) are called through the loop with zero
  entry registers; the original leaves EDX/ECX as the call site had them and
  some pop sequences return the callee's residue. No caller reads them
  (checked for the callers in own code); the suite does not compare the stub
  calls' entry registers for that reason.

**Verdicts of the area's VAs.** "Not reached" means no `sim_gap.md` entry, a
static upper bound ("virtual" entries are noisy). `0x47dba4` has no static
reference at all (no relocated pointer, no rel32 call or jump): dead code like
`0x47e027`, kept as a row for the suite and the second known exception in
`//sim:table_test`.

| Verdict | VAs |
|---|---|
| **row** | `0x46b850`, `0x470db8`, `0x476df4`, `0x476e2c`, `0x47a874`, `0x47a8d4`, `0x47a8fc`, `0x47a904`, `0x47a90c`, `0x47a914`, `0x47adb4`, `0x47ae28`, `0x47aeb0`, `0x47aecc`, `0x47af58`, `0x47af60`, `0x47af84`, `0x47b5cc`, `0x47b9cc`, `0x47c684`, `0x47c754`, `0x47c78c`, `0x47c7bc`, `0x47c7d8`, `0x47c818`, `0x47c82c`, `0x47c8b0`, `0x47c8c4`, `0x47c95c`, `0x47c984`, `0x47db4d`, `0x47dba4`, `0x47dbca`, `0x47ddcd`, `0x47de2d`, `0x47df04`, `0x47df84`, `0x47df9c`, `0x47e0c0`, `0x47ea64`, `0x47ea92`, `0x47eaa1`, `0x47eabe`, `0x47eadb`, `0x480050`, `0x480590`, `0x4805b4`, `0x492b04` |
| **inlined in a row** | `0x47de3c` (the `idiv` in the middle of `0x47de2d`), `0x47e078` (the `idiv` in the middle of `poly_div16` `0x47e068`) |
| **core hook** | `0x47a144` `dts_load`, `0x47cdf8` `member_lookup`, `0x47de4c`, `0x47de72`, `0x47df19`, `0x47df48`, `0x47dff0`, `0x47e027`, `0x47e0a0`, `0x47e0b0`, `0x47e610`, `0x47eb30`, `0x47ebb8`, `0x47f058`, `0x47f4f4`, `0x47f9a0`, `0x47fa20`, `0x47ff8c`, `0x47ffe8`, `0x4801d4`, `0x480304`, `0x48043c` |
| **row of another area** | the mission load's (above), the input layer's (above), the drawing rows `0x47dce7`, `0x47dd1a`, `0x47e068`, `0x47e07d`, `0x47e08d`, `0x47e450`, `0x47e73c`, `0x47eaf8`; the thunks `0x4968ec`, `0x496898`, `0x4968b0`, `0x4968fe` |
| **own code without a row** (part of an own routine; never called through the dispatch) | `0x4538d0` (load.c, startup.c), `0x4539f4` (hud_gauges.c), `0x46233c` (audio.c), `0x472e6c` (audio.c), `0x4680a0`, `0x46a464`, `0x46bd9c`, `0x47a3fc`, `0x47a474`, `0x47a4a0`, `0x492b68` (load.c; `0x492b68` also startup.c), `0x471dac` (hud_cockpit.c), `0x47a0b8` (frame_buffer.c, gfx.c, init.c, load.c), `0x47ae68`, `0x47b158`, `0x47b198`, `0x47b294`, `0x47b35d` (input.c, volumes.c), `0x4805e0`, `0x480694` (input.c), `0x47c644`, `0x47c6d0`, `0x47c718`, `0x47cab0`, `0x47d046`, `0x47d05f`, `0x47d2c4` (volumes.c) |
| **dead** | `0x492b10`, `0x492b31` (the function index marks them dead) |
| **original code still called by rows** | `0x47a1ac` and `0x47a55c`: the fill layer's bitmap array rows (`fill.c`) call them through the loop (`sim_gap.md`: "virtual behind `0x482ed0`"); not implemented, nor the writer classes behind them (`0x47a638`, `0x47a798`, `0x47c284`, `0x47c2c4`, `0x47c314`, `0x47c360`, `0x47c388`, `0x47c570`, all "virtual behind `0x482ed0`") |
| **referenced only / virtual** | `0x47a34a`, `0x47a594`, `0x47b0d8`, `0x47b1e4`, `0x47b200`, `0x47b28c`, `0x47b2f8`, `0x47b3d4`, `0x47c9c8`, `0x47cad5`, `0x47cc41`, `0x47cc7d`, `0x47ccdd`, `0x47cd8c`: slots of the write-stream, DTS-writer and archive-member classes and the pieces of the archive reader that nothing in own code instantiates |
| **not reached** (callers are original code own code does not run) | `0x466a88`, `0x466ae0`, `0x466b20`, `0x467b28`, `0x471a50`, `0x478e8c`, `0x47a4e4`, `0x47a6b4`, `0x47a83c`, `0x47d198` (inside the hooked `dts_load`/`member_lookup`), `0x47a998`, `0x47a9d0`, `0x47aa7c`, `0x47aabc`, `0x47ab08`, `0x47ab24`, `0x47abb8`, `0x47abdc`, `0x47ac1c`, `0x47b50c`, `0x47b53c`, `0x47b574`, `0x47b594`, `0x47b634`, `0x47b704`, `0x47b824`, `0x47b840`, `0x47b870`, `0x47b8a8`, `0x47b918`, `0x47b974`, `0x47ba1c`, `0x47bc5c`, `0x47bc9c`, `0x47bcdc`, `0x47be24`, `0x47be74`, `0x47bef8`, `0x47bf58`, `0x47bfb8`, `0x47c02c`, `0x47c160`, `0x47c484`, `0x47c520`, `0x47d076`, `0x47d08d`, `0x47d0a4`, `0x47d0bd`, `0x47deba`, `0x47ded8`, `0x47e118`, `0x480654`, `0x492ac8`, `0x492add`, `0x492aed`, `0x492b52` (wrappers behind the dead `0x492b10`/`0x492b31` and `vector_angle` `0x49290c`) |

**Import thunks** (`0x496892..0x496a72`) without a row here:

- `0x4968f8`: `init.c` calls it only when the byte `0x497058` is set, which it
  is not in the reference file; refused, not guessed.
- Behind `0x464d44` (`0x496928`, `0x496964`, `0x4969ee`, `0x496a0c`,
  `0x496a18`, `0x496a1e`, `0x496a3c`, `0x496a4e`), behind `0x4667c8`
  (`0x496952`, `0x496994`, `0x4969a6`, `0x4969c4`, `0x4969d0`, `0x4969dc`,
  `0x4969e2`, `0x4969fa`, `0x496a54`) and behind `0x4669c4` (`0x4969ac`): the
  presentation and shell rows serve them where own code reaches them.
- `0x4968a4`: only referenced (`sim_gap.md`).
- Unreached by own code: `0x496892`, `0x49689e`, `0x4968aa`, `0x4968b6`,
  `0x4968bc`, `0x4968c2`, `0x4968c8`, `0x4968ce`, `0x4968d4`, `0x4968da`,
  `0x4968e0`, `0x4968e6`, `0x4968f2`, `0x496904`, `0x49690a`, `0x496916`,
  `0x49691c`, `0x496922`, `0x49692e`, `0x496934`, `0x49693a`, `0x496940`,
  `0x496946`, `0x496958`, `0x496976`, `0x49697c`, `0x496982`, `0x496988`,
  `0x49698e`, `0x49699a`, `0x4969a0`, `0x4969b2`, `0x4969b8`, `0x4969be`,
  `0x4969ca`, `0x4969d6`, `0x4969e8`, `0x4969f4`, `0x496a00`, `0x496a06`,
  `0x496a12`, `0x496a24`, `0x496a2a`, `0x496a30`, `0x496a36`, `0x496a42`,
  `0x496a48`, `0x496a5a`, `0x496a60`, `0x496a72`.

**Open.** The writer paths behind `0x482ed0` (`0x47a1ac`, `0x47a55c` and their
classes), the archive reader pieces and the write-stream slots.

## Oracle outputs in the host

The `[trace]` memory watches are one implementation for the proxy and
`es2sim`.

| Piece | Where | Contract |
|---|---|---|
| the watch library | `sim/oracle/mem_watch.{h,c}` (`//sim/oracle:mem_watch`) | parser, pointer chains, counted lengths, the whole, diff and unreadable events; the caller supplies the checked copy, the view of the live bytes, the output and an optional phase timer |
| the proxy | `tools/reference-game/proxy/mem_watch.h` | keeps its names (`mem_watch_add`, `mem_watch_check`, `mem_watches`, `mem_watch_count`, the `MEM_WATCH_*` limits) as wrappers and macros over the library's table; its checked copy (ReadProcessMemory, LastError restored, the VirtualQuery fallback), the identity view and the perf phases `PERF_WATCH`/`PERF_FORMAT` stay in the header; `:instrumentation` depends on the library. `mem_watch_differential_test`, `_random_test` and `_benchmark_test` compare it byte for byte against the frozen baseline `tests/mem_watch_baseline.h` (events, state, LastError, ReadProcessMemory copy counts) |
| the host | `sim/src/host.c`, `include/es2/sim/host.h` | `es2_sim_setup` reads `[trace] watch_count`/`watchN` with the proxy's rules (at most 48, every entry present, under 255 characters, printable ASCII, parsed); the start record's `watches` is the count; `es2_sim_tick` writes the watch events after its tick record (sequence = that record's, frame = the dword `0x4d24bc+0xbf` before its increment); the copy is the host's VirtualQuery `readable` check plus a copy through `es2_guest_memory`, the view `es2_guest_memory.host` (`//sim:host_watch_real_test`) |
| the replay driver | `sim/tests/replay_suite.py` | writes the suites' watches into the INI; opt-in `--after-load-snapshot [--snapshot-extra LIST]` adds the proxy's `[snapshot]` section (mission_load.md section 14, "es2sim's snapshot against the original") |

**Host behaviour (decision):** a malformed or refused watch fails the set-up
with exit 67 and `[trace] watch refused`, before anything else is set up and
before the tick log is created. The proxy refuses its whole tick log in that
case (`tick_log_watch_refused`). The after-load snapshot's expected headless
differences (the SOS module handle, the 97 entry slots, the sample blocks) are
in mission_load.md section 14.

## The windowed platform layer

`es2sim` runs in a window: the 8-bit frame through the present path, input
from SDL3 through the input layer's seam; focus, close, size and the cursor
follow the window procedure's contract (launcher.md "DBSIM's process shell, start-up and exit", flyer.md
"MainWndProc 0x466034"). Deviations are listed with the reason the platform
forces each; the open points are settled in
[The window's open points](#the-windows-open-points).

**What is built.**

- `sim/src/platform_sdl.c`, `es2/sim/platform_sdl.h` (`//sim:platform_sdl`,
  SDL3 and winmm) produce only what Windows would hand the own code
  (messages, key states, the client rectangle, the cursor position, the
  joystick answers) and show what the present drew. The pump `0x464c78`, the
  window procedure `0x466034` and the input functions stay the loop's and the
  input layer's own code.
- **The loop seam** `es2_sim_loop_window` (`loop.h`), NULL in every headless
  run, with the hooks `peek` (after the journal and the posted messages,
  before `WM_QUIT`), `set_cursor` (the pump's `SetCursor`), `sleep` (the pause
  path's `Sleep(100)`), `clock` and `pace` (around each limiter poll) and
  `def_window_proc` (the window procedure's default case). With
  `window == NULL` the loop is the headless one; the pump's loop form carries
  the peek's -1 (a refused sent message).
- **`//sim:es2sim_window`**: `src/main.c` built with `ES2SIM_WINDOW`;
  `--headless` is optional there. Without it, `main.c` sets the window layer
  over the loop, the input and the present, reads `SM_CXSCREEN`,
  `SM_CYSCREEN`, `SM_CXFRAME`, `SM_CYFRAME` and `SM_CYCAPTION` for the
  start-up's window rectangle `0x4d36cc` (headless keeps the stand-ins), and
  opens the window right after the DIB section. A windowed run ignores the
  configuration's journal replay. `//sim:es2sim` does not define
  `ES2SIM_WINDOW` and does not link SDL3.

| Behaviour (original) | Windowed layer | Verified by |
|---|---|---|
| The window is created at the rectangle `0x4d36cc` with client size W × H = `[0x4d253e]` × `[0x4d2542]`, after the DIB section, then shown and updated (presentation.md "GDI branch", launcher.md "DBSIM's process shell, start-up and exit"); the class carries the icon `LoadIconA(hInstance, "es2icon")` (`0x465111`, name at `0x4a084d`) | `SDL_CreateWindow("EarthSiege II", W, H, hidden)` at the client origin (rect x + `SM_CXFRAME`, y + `SM_CYFRAME` + `SM_CYCAPTION`), then shown; the creation paint is queued (`UpdateWindow`). SDL registers its own class, so the layer loads the icon from the compiled-in resources ([Headers, imports and the icon as own tables](#headers-imports-and-the-icon-as-own-tables); Windows' choice of image per size, `CreateIconFromResourceEx`) and sets it with `WM_SETICON`, large and small, before the window is shown; no icon leaves the window without one, as a failed `LoadIconA` leaves the class | `es2sim_window_test` (dummy driver: "window 640x480"); `platform_sdl_test`; live on a clone the title bar shows the icon (es2-data `logs/o33-20261022/icon-*`) |
| `WM_PAINT` → the present `0x464a14` (GDI branch); `BeginPaint` validates | a pending paint is retrieved after the queued input and never while `WM_QUIT` is posted; cleared on retrieval and by every present | `platform_sdl_test` (paint after `WM_QUIT`, restore repaint) |
| `WM_SETFOCUS` → resume `0x45f108`, `WM_KILLFOCUS` → pause `0x45f0d4` (byte `0x4d2572`), both sent; `ShowWindow` activates the window, so the first `WM_SETFOCUS` comes before the sound's set-up and the two then alternate | SDL focus events are dispatched inside the peek through `es2_sim_window_proc`, as `SendMessage` would. **Deviation:** Windows may focus this window late or never, so the layer sends the activation at the first peek when SDL has not reported the focus, and passes on only alternating focus messages (`guest_focus`); a late first `WM_SETFOCUS` would make the resume `0x463238` start every sound row (audio.md "Focus, pause and resume in the window") | unit test (pause byte, sound pause/resume, a late focus without a second `WM_SETFOCUS`, the activation without a focus) |
| `WM_CLOSE` → the quit dialog `0x45feb8(0x410)`, answer word `0x4d370c` | the close button sends `WM_CLOSE`; Alt+F4: `WM_SYSKEYDOWN` reaches `DefWindowProcA`, which sends `WM_SYSCOMMAND(SC_CLOSE)`, whose default sends `WM_CLOSE`; SDL's own Alt+F4 handling is off | unit test (the six messages, the answer word) |
| `WM_SIZE` on minimize and restore; while iconic `GetClientRect` is 0 × 0 | minimized: `WM_SIZE(SIZE_MINIMIZED, 0)` and client 0 × 0; restored: `WM_SIZE(SIZE_RESTORED, H<<16 \| W)`, then a repaint | unit test |
| Key messages: `WM_KEYDOWN`/`UP`, or `WM_SYSKEY*` while Alt is down and Ctrl is not, and for F10; lParam = repeat 1, set-1 scan code, extended bit 24, context bit 29, previous state bit 30, transition bit 31 | SDL scancode (HID) → own set-1 table → `MapVirtualKeyW(MAPVK_VSC_TO_VK_EX)`; fixed VKs for the modifiers (generic), the keypad (`VK_NUMPAD*` with Num Lock, else the cursor block), Pause, Num Lock, PrtSc and keypad Enter (`VK_RETURN`, extended); repeats carry bit 30. AltGr's fake left Ctrl: [The window's open points](#the-windows-open-points) | unit test (table, lParams, Esc repeat, Shift and Up) |
| `GetKeyState`: the thread's synchronous state, down `0x8000`, toggle bit 0 | the table is updated when a key message is *retrieved*; generic and left/right VKs both kept; initial toggles from `SDL_GetModState` (Caps, Num, Scroll); `0xff80 \| toggle` when down; resynchronised on activation ([The window's open points](#the-windows-open-points)) | unit test (before and after retrieval, `VK_LSHIFT`/`VK_RSHIFT`) |
| Mouse messages in client coordinates with `MK_` flags; no `CS_DBLCLKS` | left, right, middle, move, wheel (`WM_MOUSEWHEEL` in screen coordinates, delta·120); events outside the client area or while iconic are ignored (no capture, hint `MOUSE_AUTO_CAPTURE` 0) | unit test |
| `GetCursorPos`, `joyGetDevCapsA`, `joyGetPosEx` | the original's own calls (user32 and winmm); the process is DPI-unaware like the original | by reading (`in_cursor`, `winmm_caps`, `winmm_pos`); the answers depend on the machine |
| `SetCursor([0x4d3720])` in the pump; the start-up's wait cursor | the wait handle (`start.platform.cursor_wait`) → SDL's system wait cursor; any other → the arrow | unit test (the pump's `last_cursor`) |
| The limiter `0x4678c0` polls `GetTickCount` (47 ms granularity on the test machines) until previous + 40 | virtual time keeps its 47 ms step; before each poll the clock moves up to the wall clock since a base; after it the pace sleeps until the wall clock reaches the virtual clock (rebase when > 1 s ahead); a fast frame waits for the step, a slow frame's elapsed time is the real one | unit test (fake clock: 47 ms wait, 100 ms catch-up, the scratch and previous words) |
| `DefWindowProcA` with F10 or a lone Alt release → `SC_KEYMENU` | `WM_SYSCOMMAND(SC_KEYMENU)` is sent through the window procedure and only counted (`menu_keys`), headless too ([The headless acceptance](#the-headless-acceptance)) | unit test |

**Deviations.**

- **No 8-bit display mode.** A 32-bit SDL window; the present's source
  window is converted through the last uploaded colour table
  (`es2_sim_present_table`, RGBQUAD) to XRGB8888 and drawn at the present's
  destination rectangle with nearest scaling. `StretchBlt`'s mode and the
  256COLOR palette emulation (media_compatibility.md) are not reproduced; a
  1:1 present is the same copy. `AnimatePalette` has no separate effect (the
  next present uses the table). A present whose source lies outside the DIB
  is only counted.
- **Not resizable, no maximize box.** The original has
  `WS_THICKFRAME | WS_MINIMIZEBOX | WS_SYSMENU`, and `WM_GETMINMAXINFO` limits
  the track size (the static reading gives H − 1; the P-01 captures show
  client 640 × 480 = the game size). The layer keeps the game size and sends
  no `WM_GETMINMAXINFO`, `WM_ACTIVATE*`, `WM_SETCURSOR` or `WM_NCHITTEST` (the
  window procedure refuses `WM_GETMINMAXINFO`, a case no replay reaches).
- **No system-menu modal loop**: `SC_KEYMENU` is counted.
- **Message timing.** The creation paint and the first `WM_SETFOCUS` arrive
  at the first peek after the window opens, not inside
  `ShowWindow`/`UpdateWindow`; all sent messages are dispatched from the peek.
- **Virtual time is paced by the wall clock** (the 47 ms step kept; the
  original's `GetTickCount` is the real clock).
- **Keys.** Num Lock with Shift on the keypad (the temporary cursor key) is
  not modelled. Alt's key-up after another key is `WM_KEYUP`, `WM_SYSKEYUP`
  only when Alt was the last key down (hypothesis from the documented Win32
  sequence, not measured).
- **The client rectangle while iconic**: the present's client rectangle stays
  the game size (GDI would draw nothing then).

**Acceptance.** `//sim:platform_sdl_test` (dummy video driver, software
renderer, synthetic guest image, recording services): the key table and
lParams, the conversion, focus, close, size and paint, keys, Alt+F4, Alt alone
and F10, mouse, `GetKeyState` after retrieval, `WM_PAINT` after `WM_QUIT`, the
limiter's pacing, the present's texture; the mutation of the previous-state
bit in `es2_sim_window_key_lparam` (bit 30 → 28) is caught.
`//sim:es2sim_window_test`: usage 64, a synthetic EXE refused with 65 headless
and windowed (the claim `0x400000..0x6d7fff` holds with SDL3.dll loaded), and
the launcher's line in a synthetic installation directory (window 640 × 480).

## The window's open points

The window layer's verdicts on the keyboard menu, `GetWindowRect`, the
stretch mode, AltGr, the key state on activation, the cursor warp `0x4808dc`
and the statistics on a refusal. Code: `sim/src/platform_sdl.c`,
`sim/src/main.c`, `sim/src/hud_cockpit.c`, `sim/src/services_cursor.c` with
`sim/include/es2/sim/cursor.h`, rows in `sim/dispatch/services/cursor_warp.h`.
Import slots read from the hash-verified DBSIM copy with an own parser; code
facts from the listing.

| Import | Slot | Thunk |
|---|---|---|
| `GetWindowRect` | `0x6cc42c` | `0x49694c` |
| `ClientToScreen` | `0x6cc460` | `0x49699a` |
| `SetCursorPos` | `0x6cc490` | `0x4969e2` |
| `StretchBlt` | `0x6cc4c4` | `0x496a2a` |

DBSIM imports no `SetStretchBltMode`. It has no menu resource (resource types
3 and 14 only), but its window style carries `WS_SYSMENU`. `table_test`
checks the two cursor thunks against the import table.

- **Keyboard menu (F10, a lone Alt): no menu loop (live for injected keys).**
  With the recording's hidden window the golden's tick log runs on through F10
  ([The headless acceptance](#the-headless-acceptance)); a live run
  (main_loop.md) injected F10 `WM_SYSKEYDOWN`/`WM_SYSKEYUP` at pump 0 on a
  visible window and the frames continued with one present each.
  `DefWindowProcA`'s `WM_SYSCOMMAND(SC_KEYMENU)` is sent, counted
  (`menu_keys`, printed in the statistics) and returns at once.
  **Hypothesis, open:** injected messages skip USER32's real-input menu state,
  so a physical F10 or Alt could still enter the system menu's modal loop
  (loops inside USER32 are not visible in the trace). Check: on a clone's
  visible window and es2sim's, press F10, a lone left Alt and AltGr (German
  layout) on the real keyboard and watch whether the frame counter
  `[0x4d257b]` stops until Esc. If the original stops, es2sim needs a
  `WM_ENTERMENULOOP`/`WM_EXITMENULOOP` pair around a blocking wait, built from
  the measured sequence.
- **`GetWindowRect` after a move: no change (listing).** Callers: the load
  root `0x461652` (uses only `right-left` and `bottom-top`); the centring
  helper `0x464b2c` (calls at `0x464b50`, `0x464b7f`) and the window
  procedure's private `0x812` after `WM_DISPLAYCHANGE` (`0x46642b`,
  `0x466444`), which es2sim never sends; the DirectDraw toggle `0x4667c8`
  (`0x46682c`), which reads it only in full screen, where the platform's
  `ClientToScreen` leaves the point unchanged. No reachable reader sees the origin, so
  the input layer's row (the creation rectangle `0x4d36cc`) stays. The layer
  tracks the origin (`SDL_EVENT_WINDOW_MOVED`, wheel coordinates).
- **`StretchBlt` mode: the default, 1:1 here.** DBSIM never sets a stretch
  mode, so the paint DC's default (`BLACKONWHITE`) would apply to a scaled
  present. es2sim's window keeps the game size, so every present is 1:1; a
  scaled present is counted (`presents_scaled`) and drawn nearest. A present
  whose destination width or height is 0 or less draws nothing, as GDI does
  (`presents_empty`). No present computes such an extent; the mirroring of a
  negative extent is not modelled.
- **AltGr's fake left Ctrl (hypothesis from documented Windows behaviour, not
  measured).** When the layout has AltGr, a right-Alt key-down (repeats
  included) is preceded by a left-Ctrl `WM_KEYDOWN` (`VK_CONTROL`, scan
  `0x1d`, not extended), and the right Alt's key-up by the Ctrl's key-up; the
  fake Ctrl is added only when no Ctrl is down. "Has AltGr" is a heuristic
  standing in for `KLLF_ALTGR` (`es2_sim_window_layout_altgr`, read at open:
  `ToUnicodeEx` gives a character for some key with Ctrl and right Alt down).
  `WM_SYS*` applies while Alt is down and Ctrl is not, so with Ctrl down Alt's
  key-down is `WM_KEYDOWN` and its key-up `WM_KEYUP`; the fake Ctrl's key-up,
  sent while Alt is still down, is `WM_SYSKEYUP`. AltGr and a left Ctrl+Alt
  therefore never open the keyboard menu.
- **The key state on activation (hypothesis, documented behaviour, not
  measured).** On `SDL_EVENT_WINDOW_FOCUS_GAINED`, before `WM_SETFOCUS`, the
  layer reads the thread's synchronised state (`GetKeyboardState`; tests use
  the `key_sync` hook) and copies it into GetKeyState's table (down and toggle
  bits) and the event state (down bits): `es2_sim_window_resync`, counted as
  `resyncs`. A key held while the focus was away is down after activation.
- **The cursor warp `0x4808dc`** (listing `0x4808dc..0x480947`), own code with
  both paths:
  1. `GetClientRect(hwnd, 0x6c5f5a)`, result not read;
  2. `x = [p]·[0x6c5f62] / ([0x4d3040] sar 1)`,
     `y = [p+4]·[0x6c5f66] / ([0x4d3044] sar 1)`, each product imul's low
     dword, then `cdq` and `idiv`;
  3. `ClientToScreen(hwnd, &pt)`;
  4. `SetCursorPos(x, y)` with the converted point;
  5. return with `SetCursorPos`'s EAX; the first argument is not read.

  The two calls are rows (`client_to_screen`, `set_cursor_pos`; no caller
  reads EAX). Headless they answer as the oracle's hidden window does (point
  unchanged, TRUE, no move; the proxy's `window_shim.h`). Windowed,
  `platform_sdl.c` binds them in `es2_sim_window_init` and unbinds them in
  `es2_sim_window_close`: `ClientToScreen` adds the client origin,
  `SetCursorPos` moves the real cursor (`cursor_warps`). A zero divisor or
  `0x80000000 / -1` ends the process at its `idiv` (`0x480909`, `0x480922`)
  with the original's code ([The original's fault paths](#the-originals-fault-paths)).
  `dialogs.c`'s copy of the warp (`cursor_place`, used by `focus_set`
  `0x454c98`) makes the same two calls (the in-vitro dialogs suite compares
  `SetCursorPos` with the rows' calls).
- **The statistics on a refusal exit.** A refused trapped call ends in
  `dispatch_trap.c`'s `ExitProcess(70)` after the host's callback. In the
  window build, `main.c` wraps `sim.dispatch.refuse` right after
  `es2_sim_audio_window_attach`; when the refusal has a return address (the
  process ends) the wrapper prints the window and audio lines once, after the
  host's log; the fault filter prints them too. A line carries resyncs, AltGr
  Ctrl keys, keyboard-menu commands, cursor warps, and scaled and empty
  presents.

**Deviations.** AltGr detection is a heuristic (Windows does not expose
`KLLF_ALTGR`). The resync reads `GetKeyboardState` when SDL reports the focus,
which can be after Windows delivered the activation.

**Acceptance.** `platform_sdl_test`: the activation's resync (fake keyboard
state); the AltGr sequence with its repeat (exact lParams `0x001d0001`,
`0x01380001`, `0x401d0001`, `0x41380001`, `0xe01d0001`, `0xc1380001`) with no
keyboard menu; a held left Ctrl not doubled; right Alt without AltGr as
`WM_SYSKEYDOWN` `0x21380001`; the cursor rows windowed (origin offset, fake
`SetCursorPos`), headless after close, refused on a NULL point; the empty and
the scaled present. The mutation "fake Ctrl sent as extended" is caught.
`table_test` checks the two thunks.

**Open.** The real-key keyboard menu, AltGr and the resync are not measured
(owner item, above).

## The headless acceptance

The acceptance conditions of section 10: `es2sim.exe --headless` replays every
registered suite with the tick log, the watches and the return files
identical to the goldens of check-r11-4-original; a difference is bisected;
the frame capture is compared with DBSIM's ([The frame compare as
tests](#the-frame-compare-as-tests)). **All 40 registered suites replay
IDENTICAL headless** (evidence es2-data `logs\h5-10-20261021`). The pieces it
needs: the entry shims, the shim floor, the rows below and the headless
keyboard menu activation.

### The entry shims

109 of the 782 hooks are entered under the proxy at a shim of their capture
chain (`*_capture_entry`), not at the replacement, and the shim carries part of
the contract: it reads the caller's registers and stack (the emitter adapters
of `0x40ea58` and `0x40ec64` take a residue record through ECX and move ESP
to it; on the direct path `c3_09-loadout-b` dies at tick 1390 with a corrupt
ESP).

- `es2_sim_register_adapters` (`src/adapters.c`) runs each row's capture chain
  once on a stand-in site and keeps the shim the chain installs there
  (`es2_sim_entry.entry_shim`). `table_test` prints "entry shims: 109 of 782
  hooks" and checks 0 < shims < hooks.
- The trap continues a machine call of such a row at
  `_es2_sim_trap_shim_entry` (`src/dispatch_trap.S`), which zeroes the
  `ES2_SIM_SHIM_ZERO` (4096) bytes below the caller's ESP, top-down, then
  jumps to the shim. The shim's bridge is the trampoline back into the
  machine.
- A C call of such an entry from own code is made as a machine call
  (`es2_sim_trap_call`), except a row with an x87 result, which keeps the
  direct path.

**The zero model.** The shims read residue below the caller's frame; under the
proxy that is the original's stack content, in the host the zeroed area. The
goldens accept it (section 5's `aim_fire` row, `c3_09-loadout-b`, identical
for 8886 ticks; the whole replay table). It is a model, not a measurement of
the residue.

### The shim floor (`ES2_SHIM_FLOOR`, `weapons_hooks.h`)

`ES2_SHIM_FLOOR` lowers a gapped shim's ESP to the floor the last bridge
recorded and compares it with the thread's stack limit `FS:[8]`. It keeps EAX
across that test in the static `es2_shim_floor_eax` and writes no stack: a
`pushl %eax` there writes ESP − 4, which, when ESP is already a suspended
bridge's floor, is that bridge's saved host frame. With the push,
`c1_01-options-dialog` (F12 at tick 300) and `c3_10-detail-options` (F12 at
204) fault in the options dialog; the fault needs `player_input` `0x460870`
(shim 23) and `render_root` `0x45fca8` (shim 92), and the store is the push
of `cockpit_sync`'s (`0x41b130`, shim 89) shim under the dialog pass.
`differential_render_root_test` runs an edge sweep of 64 floors from the gap
edge with a painted stack (EAX inverted, so the paint cannot hide a store);
it passes (deepest 459752) and fails with the push ("edge 15"). Both dialog
suites replay identically ([main_loop.md "Shim stack budget"](../reverse-engineering/main_loop.md#shim-stack-budget), cockpit.md).

### Rows reached by the replays (`sim/dispatch/services/hud_mfd_leaves.h`)

Each row is decompiled, checked against its listing range and compared in
vitro by `differential_hud_mfd_leaves_test` (`hud_mfd_leaves_cases.h`: 3000 cases, 68
counters; it also covers `weapon_gauge_event`, below).

| Row | Address (listing) | Args | Own code | Reached by |
|---|---|---|---|---|
| `dump_flush` | `0x401e5c` (`401e5c..401ef7`) | 0 | `loop.c` | every suite (under `render_root`'s bridge) |
| `callsign_digit` | `0x434260` (`434260..43427c`) | 1 | `hud_radio.c` | the default launcher line (frame 1101) |
| `display_restore` | `0x4669b4` (`4669b4..4669c2`) | 0 | `loop.c` | `c1_01-keys-extra` |
| `part_state` | `0x438624` (`438624..4386fd`) | 3 | `mfd.c` | `c1_01-display-keys` |
| `part_bar` | `0x437e94` (`437e94..43822d`) | 4 | `mfd.c` | `c1_01-display-keys` |
| `status_tail` | `0x43b288` (`43b288..43b358`) | 4 | `mfd.c` | `c1_01-display-keys`, `c5_05-moon-missiles` |
| `projectile_gauge_event` | `0x4414dc` (`4414dc..441548`) | 3 | `hud_gauges.c` | `c5_05-moon-missiles` |
| `pod_gauge_event` | `0x441a24` (`441a24..441a5b`) | 2 | `hud_gauges.c` | `c1_01-unarmed-keys` |
| `shields_gauge_event` | `0x443834` (`443834..44387f`) | 2 | `hud_gauges.c` | `c1_01-keys` |
| `range_set` | `0x447014` (`447014..447039`) | 2 | `mfd.c` | `c5_05-moon-missiles` |
| `target_locked` | `0x440370` (`440370..4403e0`) | 1 | `mfd.c` | `c3_10-hunt-targ` |
| `map_target` | `0x44f03c` (`44f03c..44f0c1`) | 3 | `mfd.c` | `c3_09-orders-matrix`, `c3_09-wingman-orders`, `c5_05-orders-flyer` |
| `map_inside` | `0x44f0c4` (`44f0c4..44f157`) | 2 | `mfd.c` | with `map_target` |
| `hdd_parts` | `0x451de0` (`451de0..451e39`) | 3 | `mfd.c` | `c5_05-moon-missiles` (tick 5730, from the HDD draw `0x450c7c`) |
| `blit_at_z` | `0x45c964` (`45c964..45ca0e`) | 4 | `hud_draw.c` | behind `hdd_parts` |
| `part_bitmap` | `0x438230` (`438230..438623`) | 6 | `mfd.c` | `c5_05-moon-missiles` (tick 5730, the HDD draw's part bitmap) |

**`status_tail` takes four arguments.** The tail `0x43b288` is jumped to from
the status method `+0x40` at three sites (`0x43ac3d`, `0x43af9e`, `0x43b127`)
and reads the flag and the counter k from that method's leftover argument
slots at the caller's stack top − 8 and − 4. `mfd_rest2_cases.h` builds the
host frame with four slots (`M2_HOST4(36)`).

#### The popped locals

`0x451de0` and `0x45c964` end with `pop ecx; pop ecx`, so ECX is their second
local: the point's y (`hdd_parts`: the last drawn entry's; `blit_at_z`: pt.y
less `[desc+0x9e]`), not the blit's ECX. Where that local is never written (no
drawn entry, or `blit_at_z` without a bitmap) ECX is the original's stack
residue; the rows answer 0 (the zero model) and the suite does not compare
ECX on those two paths. No caller reads it: the HDD draw is own code, and
`0x42f0b0` does not use ECX after the call (listing).

**The part bitmap.** `0x438230` is `0x437e94`'s sibling for the HDD parts that
are not bars (contract in `mfd.c`). Kept as in the listing: the bitmap's
height is shifted by `[0x4d253a]` like its width (`4382a5`), not by
`[0x4d253c]`; mode 0 shifts the offsets by sx − 1 and sy − 1, not by the shift
counts; modes 1 to 3 step one pixel at a time, without the sx by sy block. The
scaled width and height are only stored (the dead locals EBP−0x54 and
EBP−0x50).

**Mutations** caught: 8 of 8 on the first `mfd.c` rows (the K loop's ECX,
dead after the loop, is equivalent); 8 of 8 on the later rows (entry stride
0x22 → 0x20; z → 0 in `blit_at_z`'s plain blit; the y sum using pt.x; the
blit's ECX instead of the popped local; `part_bitmap`'s row skip without the
− 1; its column offset shifted by sy; mode 2 writing b instead of pixel + b;
mode 3's lower bound exclusive); mode 1's ECX a instead of b on the b == a
exit is equivalent.

**`0x4968f8` (RTL)** is entered by the original in all 40 suites, but
`init.c` calls it only on the DLL exception-info branch, which the reference
file never takes (byte `0x497058` is 0); that call is refused on purpose.

#### `weapon_gauge_event` `0x440f18`

The energy weapon gauge's slot 0 (`EnergyWeaponGauge`, table `0x49c91c`;
listing `0x440f18..0x440f9d`), own code in `hud_gauges.c`:

- **The toggle argument.** The method keeps G in ECX, loads the word at G+0xd9
  into CX and pushes ECX, so `group_toggle` gets `(G & 0xffff0000) | word`.
  `group_toggle` compares only the word, but returns ECX = n when no entry
  matches, and that ECX escapes through this method's return (weapons.md
  "4110ac").
- **The result registers** at `ret`: EAX the callee's after either call
  (index 0), the clamped value after the slider (index 1), index − 1 after a
  later match, and the index reached without a match; EDX the list cursor,
  except after a call; ECX G, except after a call.

`hud_mfd_leaves_cases.h` calls it as the op "weapon gauge" with eight counters (toggle,
toggle with W's high word unequal to G's, panel, slider ≤ 0, inside and
≥ 0x400, a later index, no match); half of the widgets carry another high word
(`HX_WX`, never dereferenced). Seven mutations are caught (the sign-extended
word; W's high word instead of G's; the slider's EAX unset; a later index's
EAX = index; the callee's registers dropped; ECX = W; an unsigned clamp). The
original enters the method live (2/2/2/10/6/6 in the live column of
`docs/functions`); all 40 suites replay IDENTICAL with it (`fg.log` in the
evidence).

### The keyboard menu activation (`loop.c`)

`c5_05-moon-missiles` presses F10 three times (journal frames 8113 to 8134:
`WM_SYSKEYDOWN`/`WM_SYSKEYUP` 0x79). F10's key-up reaches `DefWindowProcA`,
which starts the keyboard menu (`WM_SYSCOMMAND(SC_KEYMENU)`; the window has
menu 1). The recording's window was hidden (oracle_regression.md:
`ShowWindow` answers `FALSE`) and its tick log goes on to frame 8261 with the
frame counter running, so no modal menu loop held the game's loop. Headless
`DefWindowProcA` counts these key-ups (`menu_keys`) and has no effect; the
suite replays IDENTICAL for 8262 ticks. A visible window: [The window's open
points](#the-windows-open-points).

### Harness (`sim/tests/replay_suite.py`)

- **`results.dat` baseline.** The recording clones (timing-r111, -p1, -p2,
  -p4, -p5) carry the same `DATA\results.dat`; `SuiteLaunch.psm1` deletes it
  only under KeepReturns, so a golden lists `results.dat` only when the run
  changed it (the quit of `c1_01-quit-dialog` writes exactly the clones'
  bytes). The own process starts without a result, so the comparison uses
  the clones' hash (`CLONE_RESULTS_SHA256`) as the baseline, except for a
  launch set's outcome run (keep_all).
- **`--frames`** adds es2sim's frame capture as
  `<output-dir>/<suite>/frames.ndjson` (not part of the replay verdict); es2sim
  then runs with an environment of the frame references' size ([The heap
  trace](#the-heap-trace)).

### Replay table

es2-data `logs\h5-10-20261021`; every verdict IDENTICAL (tick log, watches
and, where the golden has them, return files and exit code).

| Suite | Ticks | Watch changes | Return files | Exit code |
|---|---:|---:|---:|---|
| `acceptance-60s` | 1500 | 16 | 0 | 0 (no golden state) |
| `c1_01-cheat-destroy` | 430 | 13 | 1 | 3 (compared) |
| `c1_01-cheat-disable-late` | 1785 | 6253 | 1 | 3 (compared) |
| `c1_01-cheat-disable-leg` | 315 | 53 | 1 | 3 (compared) |
| `c1_01-cheat-keys` | 1200 | 3253 | 0 | 0 (no golden state) |
| `c1_01-cheat-keys-plain` | 1200 | 3258 | 0 | 0 (no golden state) |
| `c1_01-cheat-leader` | 1700 | 5404 | 0 | 0 (no golden state) |
| `c1_01-cockpit-keys` | 1000 | 2796 | 0 | 0 (no golden state) |
| `c1_01-collision` | 2200 | 17 | 0 | 0 (no golden state) |
| `c1_01-display-keys` | 800 | 12 | 0 | 0 (no golden state) |
| `c1_01-free-camera` | 1000 | 14 | 0 | 0 (no golden state) |
| `c1_01-joystick` | 1400 | 14 | 0 | 0 (no golden state) |
| `c1_01-keys` | 700 | 12 | 0 | 0 (no golden state) |
| `c1_01-keys-extra` | 950 | 12 | 0 | 0 (no golden state) |
| `c1_01-mouse` | 600 | 13 | 0 | 0 (no golden state) |
| `c1_01-options-dialog` | 700 | 12 | 0 | 0 (no golden state) |
| `c1_01-quit-dialog` | 201 | 12 | 0 | 0 (compared) |
| `c1_01-success` | 6198 | 16305 | 2 | 3 (compared) |
| `c1_01-tape-record` | 600 | 13 | 0 | 0 (no golden state) |
| `c1_01-unarmed-keys` | 600 | 13 | 0 | 0 (no golden state) |
| `c1_02-probe` | 1500 | 10344 | 2 | 3 (compared) |
| `c1_06-cheat-disable` | 700 | 1499 | 2 | 3 (compared) |
| `c1_08-escort-route` | 1500 | 5611 | 0 | 0 (no golden state) |
| `c1_08-scan` | 2600 | 11553 | 2 | 3 (compared) |
| `c2_02-identify` | 1800 | 4845 | 0 | 0 (no golden state) |
| `c3_09-loadout-b` | 8886 | 45249 | 2 | 3 (compared) |
| `c3_09-orders-matrix` | 4200 | 19156 | 0 | 0 (no golden state) |
| `c3_09-wingman-orders` | 3300 | 16150 | 0 | 0 (no golden state) |
| `c3_10-detail-options` | 700 | 3679 | 0 | 0 (no golden state) |
| `c3_10-hunt-targ` | 2400 | 16505 | 0 | 0 (no golden state) |
| `c3_10-loadout-hercs` | 936 | 4928 | 2 | 3 (compared) |
| `c3_10-own-building-fire` | 700 | 3666 | 0 | 0 (no golden state) |
| `c4_05-convoy` | 1700 | 8508 | 0 | 0 (no golden state) |
| `c4_08-joystick` | 1040 | 1368 | 2 | 3 (compared) |
| `c4_08-razor-plasma` | 2094 | 5157 | 2 | 3 (compared) |
| `c4_08-slope-dive` | 1319 | 2431 | 2 | 3 (compared) |
| `c4_10-probe` | 600 | 1217 | 0 | 0 (no golden state) |
| `c5_05-moon-missiles` | 8262 | 52524 | 2 | 3 (compared) |
| `c5_05-orders-flyer` | 1400 | 10604 | 0 | 0 (no golden state) |
| `train1-timers` | 1600 | 78 | 0 | 0 (no golden state) |

**Deviations.** The shims' residue is the zero model. The keyboard menu
activation has no effect headless, as with the recording's hidden window.

## The frame compare as tests

`//sim:frames_<suite>_test` (one per registered suite, `sim/frames_tests.bzl`
over `replay_tests.bzl`'s `SUITES`; manual, local, exclusive like the replay
tests; `//sim:frames_all` runs all 40) replays the suite headless with the
frame capture and compares it with DBSIM's capture of the same suite. A
difference fails unless the known-differences list `sim/tests/frames_known.py`
names it with its cause. **State: 40/40 pass**; the only row is the
"original varies" row below.

### Region hashes on both sides

Both captures carry region hashes from one shared table,
`tools/reference-game/proxy/frame_regions.h` (header-only, C17, table id
`t66-1`):

- the **display mode** m = dword `[[0x4d24c0] + 0x14]` at the present
  (display_modes.md "Objects"; -1 while D is not built);
- per mode a list of rectangles in 640 × 480 coordinates, scaled to the hashed
  window; a pixel belongs to the first rectangle that holds it, in the priority
  MFD, HUD, panel, view; a pixel no rectangle holds is "rest";
- per region the SHA-256 of its index bytes in row-major order, first 16 hex
  digits; null when the region has no pixels.

| Mode | Rectangles (left, top, right, bottom; exclusive) |
|---|---|
| 0 cockpit | MFD (318,296,562,434); HUD (196,0,444,92), (84,50,188,152), (290,180,350,240), (190,262,450,286); panel (0,286,640,480); view (0,0,640,390) |
| 1 scrolled panel | MFD (92,82,556,286), (133,292,518,402), (136,14,506,82); panel (0,0,640,480) |
| 2, 3 side views | view (0,0,640,480) |
| 4 outside view | view (0,0,640,390); panel (0,390,640,480) |
| other | everything rest |

The rectangles were measured on es2sim's frames of `c1_01-display-keys`
(frames 105, 325, 400, 485, 605), with the view rectangle
`0x4cfa40..0x4cfa4c` = (0, 0, 639, 389) at the present in every mode of that
suite. Mode 3 did not occur in the suites probed; it shares mode 2's table as
a hypothesis. The table is a localisation aid (a few pixels generous around
each instrument), not a contract of the drawing. A new table gets a new id;
captures with different ids compare whole frames only. Records of the slide
site `0x42b87f` are hashed with the rectangles of the mode D holds at that
present (mode 0 in both captures while the picture slides); whether that fits
the sliding picture is not checked.

Writers:

- **es2sim** (`sim/src/present.c`, `--frames`): every hashed record ends with
  `"display":m,"regions":{"view":…,"hud":…,"panel":…,"mfd":…,"rest":…}`; the
  start line carries `"regions":"t66-1"`. Read through the guest mapping with
  the same row check as the whole-frame hash.
- **the proxy** (`proxy/presenter.h`): the same fields with `[presentation]
  capture_regions=1`; without the key the bytes are those of the plain capture
  (`presenter_capture_test`). `capture-dbsim-frames.ps1 -Regions` writes the
  key.
- **dbsim_frames.py**: `build` keeps the table id (`capture.start.regions`,
  `frames.region_table`, `frames.records_with_regions`) and writes six more
  columns (display, five region hashes) under a second header line
  `#regions<TAB><id>`; tables without regions read and verify as before. `regions A B
  [--exclude-frames …]` reports every kind of difference with its first frame,
  its count and the first 200 frames.

### The kinds

Sequence records (site `0x45fbe4` and the slide returns,
`presenter_capture.SEQUENCE_SITES`) are matched by (frame, site, ordinal), so
a missing present does not shift the later ones:

| Kind | Meaning |
|---|---|
| `sequence` | a key on one side only |
| `geometry` | mode, page origin or source differ |
| `frame` | the whole-frame hash differs |
| `palette` | the colour table differs |
| `display` | the display mode differs (regions not compared then) |
| `view`, `hud`, `panel`, `mfd`, `rest` | that region's hash differs (both sides with the same table) |
| `other_sites` | the per-frame set of (index, palette) of every other site differs |

Only frames up to the last frame both captures hold are compared.

### The known-differences list

`sim/tests/frames_known.py`: `REFERENCE` (the es2-data `logs` directory of the
reference manifests, `dbsim-frames-20261021-regions`: 40 suites, table
`t66-1`, clones `timing-r111-p1`/`-p2`, hooks original),
`REFERENCE_ENVIRONMENT` ([The heap trace](#the-heap-trace)), and one row per
suite and kind: `first`, `cause`, a note, and for "original varies" the frames
to `exclude` from the other-sites compare. `frames_suite.py` judges as
`shell/tests/screen_known.c`: a kind without a row fails; a row whose
difference starts earlier fails (new difference); later fails "update the
row"; gone fails "drop the row"; "original varies" rows never fail when gone or
moved. The replay must be IDENTICAL first. Without `ES2_DATA_ROOT`, the
goldens or the reference the test SKIPs. `--capture FILE` judges an existing
`frames.ndjson`, `--suggest` prints the rows that would pass.

The one row: `c3_10-detail-options`, frames 204 and 600 out of the other-sites
compare, "original varies": frame 600 differs between three original runs
(presentation.md "The original's own frame spread", es2-data `logs\dbsim-frames-20261021`, `-a2`,
`-replaced`), and the dialog presents of frames 204 and 600 follow the SOS
timer's `0xa000` retrievals ([main_loop.md "The message trace of the replay pump"](../reverse-engineering/main_loop.md#the-message-trace-of-the-replay-pump), `-d46a/-d46b/-d46c`).

### Causes found by the compare

The original's pixels were dumped live (`capture-dbsim-frames.ps1 -Dump
-DumpFrames …`, ignored scratch) and compared with es2sim's dumps of the same
frames (es2-data `logs/t66-pixels-20261022`: counts and coordinates only).

- **The HDD map image** (eight slide suites, from the slide return `0x42b87f`,
  the scrolled panel's map page). Root step 10 calls `0x44f6f4`; its steps 1
  and 4 fill the map image (step 1: one byte per grid cell,
  `min(height, 0x7f) / 8 + 0x10`; step 4: the draw loop
  `0x44fd4e..0x44ff7d`, two `0x486ee8` triangles per cell). `hud_load.c`
  draws both (`map_cells()`); an index outside the grid is refused. Witness:
  the fill base `0x10u` → `0x11u` fails `frames_c1_01-display-keys_test`.
- **The missile camera's terrain** (MFD page F6, `0x43fe44`; c3_10-hunt-targ
  1091..1116 and 1420..1442, c5_05-moon-missiles from 5366). The draw order of
  `0x43fe44`: a fill with `[0x4d3b9c]`; `0x42841c`; `[0x4aaaa8]=0` and
  `[0x4aaaac]=1`, then the terrain draw `0x46cb9c([0x4a0be0], C, 0)` and
  `0x428140`; the lines `0x483a04` and the ellipse `0x48817c`. The camera point
  is the target point (t+0x26) offset by 500·sin/cos of the low heading word
  (`0x47dff0` uses only the low word). The flat fill `0x46bc44` (`gouraud.c`)
  points the polygon's index list `0x6c68f2` at the default list `0x6c6068`,
  which the renderer reset `0x48dd08` fills once per mission load (listing
  `0x48dd30..0x48dd41`: `xor eax,eax; mov edx,0x6c6068; mov [edx],ax; inc eax;
  add edx,2; cmp eax,0x20; jl`): word i holds i. `load.c`'s renderer reset
  writes 0, 1, .., 31 (a zero fill makes every projected vertex the first
  one's). The main view's textured terrain does not use the list; shape groups
  without their own index list (`shape_draw.c`, two sites) read it too (not
  measured on their own). In vitro: `load_rest_cases.h` "renderer reset", 200
  cases of `0x48dd08` (random words in `0x6c6068`, class count `[0x4a6488]`
  zero or not, registry index `[0x4a1690]` 0..3; DATA, world and EAX
  compared); `load_test` small_bodies checks the 32 words; the zero fill fails
  both.
- **The free camera's shape edge**: [The free camera's shape
  edge](#the-free-cameras-shape-edge).

**Pixel tooling.** `es2sim --dump-frames LIST --dump-dir DIR` writes ES2F
dumps in the proxy's format (presentation.md "Capture record"; `present.c` dump_write):
file `frame-<frame>-<site>-<ordinal>.es2f`; LIST is frame numbers and from-to
ranges, at most 64 items and 4096 frames; a frame is dumped only inside the
DIB, after the first colour-table upload and with a frame number; the stats
line counts `dumps` and `dump_failures`; both options together, a bad list or
directory exits with `ES2_SIM_EXIT_SETUP` (`present_test` 5b).
`replay_suite.py --dump-frames LIST --dump-dir DIR` passes them on into
`DIR/<suite>`. `tools/reference-game/scripts/frame_dumps.py compare A B
[--display M]` compares two dumps or directories paired by name: header fields
and colour-table entries that differ, differing pixels (and how many still
show the same colour), the first differing pixel, the bounding box, the
4-neighbour areas, with `--display M` the count per region; exit 0 identical,
1 different, 2 unusable (`frame_dumps_unit_test`). The original's code is not
made executable inside es2sim (a security weakening, refused); it runs only in
the in-vitro harness.

## The free camera's shape edge

c1_01-free-camera frames 652, 653, 709 and 715 differed by a black (16) shape
edge three to eight pixels off in the view (display 4, inside the yaw periods,
where `0x475538` draws the most shape polygons). The cause is the drawing
order of three polygon groups inside one BSP part, set by a read past the end
of a heap block whose bytes follow the heap layout. Since the heap's attach
blocks match the original's ([The heap trace](#the-heap-trace)) the suite
passes with all 1001 sequence records equal; the cause `HEAP_LAYOUT` stays in
`frames_known.py` without a row. Addresses below are es2sim's from one run,
not the original's.

- The groups are children of BSP part 0x2032ecd4, in the chain `0x478da4`
  (instance 0x2037b608) → `0x4769c8` → `0x476c18` → `0x4759d4`; the part's
  five nodes all use transform word 2. With the old heap es2sim drew the group
  with colour 17 first, then the strip group (filled by `0x484222`), then the
  group with colour 182; the original drew them in the reverse order.
- The node order: `d = 0x4911c0(eye 0x6bb2e8, (tw<<5) + [0x6b7b68], record) -
  dist` against `[0x49f248]` (`0x476b28`). bsp_node, part_bsp and
  plane_distance agree with the listing ranges `0x476b28..0x476c6d` and
  `0x4911c0..0x49129f`.
- The transform table `[inst+0x16]` is the animation manager's node matrices
  (constructor `0x478838`, new[] `0x4774f0` with n + 8 bytes; mission_load.md
  section 14, "Noise classes"). It holds n = 2 matrices in a heap block with
  header 0x51 and user size 0x4c: entry 0 the identity, entry 1 the torso yaw;
  "entry 2" at table+0x40 lies past the block's end: the calloc tail (zeros),
  the footer 0x51, the next block's header 0x5d, then that block's first words.
  That block is the manager's other allocation M+0x1a: a pointer (its low word
  at table+0x50), then the 16.16 counters that `0x45f570` advances each frame.
  The original reads past the block too (same new[] size and node count).
- Nodes 0 and 1 flip once the eye's row 2 in the transform-2 frame exceeds
  about 2600, which needs the word at table+0x50 below about -881 (0xfc8f);
  with the old heap es2sim's word was -242 (pointer 0x2032ff0e). Forcing the
  word to -1000, -3000 or -8000 made all eight dumped frames equal.
- **Live, the original's bytes** (es2-data
  `logs\dbsim-frames-20261022-t66f_probe`, `call-probe.ndjson`; clone
  `timing-r111-p1`, hooks original, the suite identical to the golden): 13,718
  calls, 115 in range, 20 matched, 4 dumps, 0 read faults. All four frames dump
  the table at 0x02bdd95c: +0x40/+0x44 0, 0 (calloc tail); +0x48 0x51 (footer);
  +0x4c 0x5d (next header); +0x50 0x02b8f6a2 (the next block's pointer; word
  -2398); +0x54/+0x58 16.16 counters (0x00020000, 0x00030000 in frame 652;
  changing per frame); +0x5c 0x0000ffff. So the original has the same
  neighbouring block in the same place; only the pointer's low word differs, a
  heap address, which the heap trace's attach blocks reproduce.

**The call probe** (`tools/reference-game/proxy/call_probe.h`,
`capture-dbsim-frames.ps1 -CallProbeVa …`, off unless the capture INI has
`[call_probe] enabled=1`): it accepts only the reference DBSIM.EXE (SHA-256,
image at 0x400000, a VA that starts an instruction in the hash-bound entry
inventory, with its kind); it puts a `jmp` over the five entry bytes of
`0x4911c0` (`enter 0,0; push esi`, relocated into its own tail); on each entry
it saves the flags in the slot the function's own `enter` overwrites next,
runs on a private stack and only reads memory (ReadProcessMemory: an
unreadable address is counted); in the listed frames (counter `0x4d257b`) when
the condition holds it writes a "dump" line once per frame and pointer; after
the last frame or `max_dumps` it writes an "end" line and restores the bytes.
`0x4911c0`'s only caller, `0x476b8f`, pushes `(movsx di << 5) + [0x6b7b68]` as
the second argument, so "tw is 2" is `[esp+8]-[0x6b7b68]==0x40`. Fixture
`tests/call_probe_fixture.c` (modes off, fire, limit, refuse, reference).

## SIM, R3D and HUD gap roots as rows and verdicts

Rows for the SIM, R3D and HUD targets of `sim_gap.md` own code can reach
(fragment `sim/dispatch/services/gap_roots_sim.h`, code in `sim/src/gfx.c`) and a
verdict for every other root and virtual target. Every target here has Live 0
in the 40-suite map (`dbsim-map-20261020`). Listing: `llvm-objdump -d` of the
verified `extracted\cd\VER95\DBSIM.EXE`.

**Rows.**

- **`0x492447` / `0x49085d`, stream put word / byte.** Same 23-byte body except
  the count (`6a 02` / `6a 01`); the listing is misaligned at `0x49085d` (it
  decodes from `0x49085f`), so the bytes were read raw. With stream `st` in
  slot 0 the routine calls `[[st]+0x14](st, n, &slot1)` and returns the
  callee's registers. Callers: `rd09_bitmap_write` (gfx.c) and
  `gl_palette_write` / `gl_font_write` (fill.c, `VA_STREAM_WORD`), all inside
  persistent write slots.
- **`0x446e3c`, next MFD page** (input_dispatch.c case 14 bridge).
  `n = [g+0xc1] + 1`, 0 when `n == 6`; calls `0x446e60(g, n)`; EAX = n, EDX/ECX
  the callee's.
- **`0x444b84` / `0x444b98`, shield arrow button** (table `0x49c9dd`, built by
  `0x444b14` inside shields_gauge `0x443524` and added to the root widget list).
  Slot 0 draws: calls `0x431410(b)` only. Slot 3 is the frame: byte
  `s = [b+0x1f]`; `s == 1` calls `0x43122c(0, b, 0)` and stores the re-read byte
  minus one; `s == 2` stores `s - 1`; otherwise nothing. The non-call paths
  return AL = `s - 2` over the entry EAX. Rowed because the root-list walks can
  reach the slots.

Confirmed in vitro: `differential_gap_roots_sim_test` (`gap_roots_sim_cases.h`), 2,000 random
cases (registers, DATA, heap, stub log); every path hit (page wrap, page from
-1, frame copy / count / none, copy rewriting the byte, visible, hidden); the
mutation `n == 6` → `n == 5` is caught.

**Root verdicts.**

| Root | Verdict | Evidence |
|---|---|---|
| `0x423044` | computed inline, counter bridge; row `flyer_slot` ([Called gap roots](#called-gap-roots)) | core/src/formation.c `fm_note` |
| `0x427d98`, `0x427da0` | computed inside the hooked caller; rows `sweep_clear`, `sweep_read` ([Called gap roots](#called-gap-roots)) | core/src/weapons.c sweep (SWEEP_CLEAR / SWEEP_READ), callee_counters.h |
| `0x401148` | dead: Shift+V masked; row `camera_mode` ([Called gap roots](#called-gap-roots)) | word `0x49eab8` = 0x47ff is only read (`0x45a87d`, `0x45ad55`, `0x45b67d`), no writer (function_verdicts.md, live_coverage.md) |
| `0x42900c`, `0x42937c` (+ `0x428f38`, `0x42933c`) | dead | sort_compare `0x429460` calls them only for record kinds != 0; `0x428d14` writes word +0x16 = 0 for every record (scene.c); `0x4cf884`/`0x4cf888` referenced only at `0x428268`, `0x428278`, `0x428c87`, `0x428c94`, `0x428dd1`; `0x429620` called only from `0x428dd9`, `0x429460` only from `0x4294e1` |
| `0x475900` | dead | reached only through slot 6 (destructor) of `0x49afb0` (hzline_dtor row) and of `0x4a0fbf`; no own code destroys a TS shape (below); horizon.md: no slot called on `[0x49aee0]` |

**Virtual target verdicts.** Persistent-class tables use slots 0 tag, 1/2
stored sizes, 3 read, 4 write, 5 dump, 6 destructor.

- **S/W, store closure.** Slots 1, 2 and 4 are called only from the store
  closure `0x47a1ac` / `0x47a55c`, whose own callers are size/write slots
  (`0x482e24`, `0x482ef7`, `0x4833fe`, `0x483479`, `0x48e96f`, `0x48ebcc`,
  `0x48fe39`, `0x48ff47`, `0x491b30`, `0x491d43`, `0x492209`, `0x492290`). Own
  code never saves shapes: SIM `0x4275fc`, `0x427714`; R3D `0x4833f4`,
  `0x483414`, `0x483458`, `0x483048`, `0x48e070`, `0x48e238`, `0x48e284`,
  `0x48e568`, `0x48e5b4`, `0x48e930`, `0x48e990`, `0x48eb2c`, `0x48ee90`,
  `0x48eeb4`, `0x48ef84`, `0x48f9e8`, `0x48fd24`, `0x48fd84`, `0x48fe18`,
  `0x48fe60`, `0x48ff0c`, `0x490084`, `0x49009c`, `0x490108`, `0x490220`,
  `0x490240`, `0x4902d4`, `0x490408`, `0x490434`, `0x490530`, `0x490de8`,
  `0x490e14`, `0x490f40`, `0x491490`, `0x4914c0`, `0x4915dc`, `0x491af0`,
  `0x491b50`, `0x491d18`, `0x4921f4`, `0x492218`, `0x492270`.
- **D, dump closure.** Slot 5 chains only through the dump base `0x47a594`
  (callers `0x48e0cf`, `0x48fa47`, `0x490fb7`, `0x49168e`, `0x491e07`, all dump
  slots); nothing outside reaches it: `0x48e0b8`, `0x48e2b0`, `0x48e5e0`,
  `0x48ebec`, `0x48efc8`, `0x48fa30`, `0x48fc24`, `0x48ff60`, `0x490134`,
  `0x490304`, `0x490588`, `0x490fa0`, `0x491674`, `0x491df0`, `0x49229c`.
- **R, read slot of `0x4a3264`** (`0x48302c`): own code constructs these objects
  instead of loading them.
- **X, destructor never called.** The own-code `+0x18` destructor calls (flag 3)
  are hud_cockpit.c destroy_shape (cockpit / impactcp resources), cockpit.c
  intro palettes, dialogs.c, widget_leaves.c, load.c (bitmap array elements)
  and fill.c; load.c's and audio.c's other `+0x18` uses are stream reads. None
  destroys a TS shape or part: SIM `0x420730`, `0x42786c`; R3D `0x475aec`,
  `0x476754`, `0x476968`, `0x476ae0`, `0x48326d`, `0x483790`, `0x483924`,
  `0x48f218`, `0x48f2a1`, `0x48f336`, `0x48f748`, `0x48f794`, `0x48f7e0`,
  `0x48f824`, `0x48f868`, `0x48f8ac`, `0x4909d2`, `0x4909ff`, `0x490a64`,
  `0x492357`, `0x49264f`, `0x49267c`, `0x4926a9`, `0x4926de`. Light and record
  destructors (slot 0) `0x40798c` (`0x497adc`) and `0x48d94d` (`0x4a5b29`),
  also `0x462107`, `0x4621a2`, `0x462254`: own code never destroys lights and
  uses only slot 1 (+4, intensity).
- **B, base table overwritten in the constructor chain**, so no finished object
  carries it: `0x4a5e88` (`0x48dce0` returns -1; `0x48f19b`), `0x4a6a14`,
  `0x49d5a4` (toggle intermediate; `0x438f14`).
- **HUD bars `0x49bd30` / `0x49bd48` / `0x49bd60`.** Own code calls only slot 0
  (hud_draw.c). Slot 1 (`0x4396a8`, `0x439460`, `0x4391e0`) is a full redraw
  through `0x439758`; slot 2 (`0x43a211`, `0x43a190`) resets B+0x20 by the sign
  of +0x14. Not called.
- **`0x437583`** (EventPort `0x49ba7c` slot 7) returns P+0x4a7; no own-code
  `+0x1c` call on ports.
- **`0x4506b5`** (`0x49d984` slot 6): the map icon's stack object is torn down
  inline in hud_draw.c; the slot is never called.

**The gap list.** The roots `0x492447`, `0x49085d`, `0x446e3c` and the virtual
targets `0x444b84`, `0x444b98` leave `sim_gap.md` at its next regeneration;
the other roots and targets stay listed (counter bridges, dead code, Live 0)
until the generator gets a verdict list.

**Open.** A verdict mechanism in `sim_gap.py` (the generator removes a root
only through a row, a hook or removal of the call). A live check of key case
14 (next MFD page) would give its row Live > 0.

## SHELL, RTL, FILE and INP gap roots as rows and verdicts

Rows for the SHELL, RTL, FILE and INP part of `sim_gap.md` (fragment
`sim/dispatch/services/gap_roots_shell.h`; code in `sim/src/services_files.c` and
`sim/src/loop.c`), the error report `0x479c78`, and a verdict for every other
root and virtual target. Listing and strings: a copy of the verified
`extracted\cd\VER95\DBSIM.EXE`.

**Rows.**

- **`0x45effc`, help language** (`0x45effc..0x45f06c`).
  `f = _fopen(path_root("data\language.cfg"), "rt")`. With a file it reads one
  byte into the low byte of a local dword (the entry ECX pushed, low byte
  zeroed) and closes the file. `F`, `G`, `S` give FRENCH (`0x49f415`), GERMAN
  (`0x49f41c`), SPANISH (`0x49f423`); anything else, and no file, ENGLISH
  (`0x49f42b`). EDX is the local dword (`pop edx`); ECX is `f` after a close,
  else `_fopen`'s ECX.
- **`0x45f070`, help file name** (`0x45f070..0x45f0cc`). Builds `0x4d2958` = the
  language + `\` (`0x49f433`) + `es2guide.hlp` (`0x49f435`), then returns the CD
  path `0x45ee60(0x4d2958)`; EAX and EDX from `0x45ee60`, ECX `0x4d2958`
  (`pop ecx`). Callers: the two F1 paths `0x4345cb` and `0x460821`, which lead
  into the help call.
- **`0x45ceb8`, the -D demo pick** (`0x45ceb8..0x45cfc5`; caller `0x45e944`).
  The frame record `0x49630c` is the exception frame, not modelled. Once (byte
  `0x49f032`) it builds the holder `0x4d26e0` (`0x453824`) and decrements the
  object counter `[[0x6cbee4]]` (a static object). Until a list was read (byte
  `0x49f033`) it opens a read stream on `tapes\demolist.str` (`0x47ad40`, a
  0xc-byte local). With stream status 0 it reads the holder as `0x4538d0`
  does (size through slot +0x18, `new[]`, the bytes through slot +0x18,
  `[holder+4] = [holder]`); the first group goes to `0x4d26f4` (`0x4539a0`), AX
  into the word `0x4d26f0`, and a positive count sets the byte; the stream is
  destroyed (`0x47ad78(S, 2)`). Without a list EAX is 0 and EDX the saved
  `fs:[0]` (not reproduced). Otherwise the index is 0 for a count of 1, else
  `_time(&t) % count` (`idiv`: the remainder has the dividend's sign); the word
  `0x49f034` = index; EAX = ECX = `[0x4d26f4 + 4*index]`, EDX = the index
  sign-extended. A marked list with a count of 0 or less is refused (the
  original would divide by it; its own code never leaves that state).
- **`0x479c54`, notify list** (`0x479c54..0x479c74`). Walks the nodes from
  `[0x6c56d8]` (next at +0, function at +4) and calls `fn(severity)`; the record
  argument is not read. EAX is the last function's, or the entry EAX when the
  list is empty.
- **`0x479dd0`, report text** (`0x479dd0..0x479e14`). `n = sprintf(buf,
  "Assert::%s: Line %d in \"%s\"", names[sev], line, file)`, `names` the table
  at `0x4a15f4`; with a text it continues `sprintf(buf + n, ": %s", text)`. EAX
  is the last sprintf's.
- **`0x479e48`, log path** (`0x479e48..0x479e8a`). `0x6c57dc` = the root
  `0x6c56dc` (copied inline) + `assert.log`; EAX `0x6c57dc`.
- **`0x479e8c`, log** (`0x479e8c..0x479f20`). `h = _open(0x479e48(), 0x901,
  0x180)`; a negative `h` calls `_abort` (EAX `_abort`'s). Otherwise, when the
  dword `0x4a1610` is set, it writes the 0x50-byte separator `0x4a154b` once and
  clears the dword; then the text (length counted inline), CR LF (from
  `0x4a1614`, copied into a local), then `_close(h)`, whose EAX it returns.
- **`0x479f24`, report body** (`0x479f24..0x47a0b4`; jump table `0x479f45`,
  `ja` unsigned; the second argument is not read). Severities 0, 1 and above 6:
  nothing, EAX = the severity. 2: logs the text (no null check in the
  original). 3: the display guard `0x4669b4`, the count `0x6c582c` + 1, the
  text into a 0x200-byte local, a box captioned `Error` (`0x4a165c`), then the
  log. 4: the guard, the text, the log, then the box (`0x4a1656`); EAX the
  box's. 5: the guard; box text "Internal Error: ..." (`0x4a159c`) + `\n` + the
  report text in a 0x3e8-byte local; the box (`0x4a1664`); then the log of the
  report text. 6: logs "Assert::break: Ctrl-C / Ctrl-Break" (`0x4a166a`).
- **`0x479c78`, the error report** (`0x479c78..0x479cfa`; row `fatal_report`,
  CDECL, EAX, 6; `es2_sim_service_fatal_report_479c78` in `loop.c`). Slots:
  record, severity, an unread argument, file, line, text. Severities 4, 5 and
  6: notify(record, severity), the report body, the report body (1, 1, 0, 0, 0)
  (nothing), `0x4650cc`, `0x4650d0` (KillTimer), `PostQuitMessage(0)`, then
  `__exit(1)`; EAX `__exit`'s when it returns. Other severities: only the
  report body. The in-vitro and scene tests replace the row after the seal
  (their replaced lists); `gap_roots_shell_cases.h` uses the fragment row.

The report rows return EDX and ECX as zero; the callers `0x401e5c` and the
scene asserts do not read them. The box, `PostQuitMessage`, `__exit` and
`_abort`: [The error paths' MessageBoxA, PostQuitMessage, __exit and _abort
windowed](#the-error-paths-messageboxa-postquitmessage-__exit-and-_abort-windowed).

Confirmed in vitro: `differential_gap_roots_shell_test` (`gap_roots_shell_cases.h`), 2,000 random
cases of 1..6 calls: EAX of every call; EDX and ECX of `0x45effc` and of a demo
pick that answers a name; ECX of `0x45f070`; DATA `0x497000..0x6cc000` with the
heap; the stub log (with the own platform's box caption, text hash, and quit,
in order). All 33 coverage counters non-zero (every language byte and no file,
holder built or not, stream status, list empty, filled or missing, count one
or `_time` (also a negative time), notify list empty or not, text or none, open
failing, separator set or clear, every severity class, the fatal end or report
only). The mutation "warning count + 2 in severity 3" fails at case 16 on byte
`0x6c582c`.

**Verdicts of the other roots.**

- **`0x464d44`** (the DIB section's window path): own code without a row;
  `main.c` always passes `dib_section`, so `frame_buffer.c`
  `es2_sim_dib_section` creates the section; `startup_test` stubs the VA. The
  imports behind it are not reached.
- **`0x467ad8`**: `int3; ret`, the `-B` debug break. The playback_key hook
  bridges it; row `debug_break`, which ends the process with
  EXCEPTION_BREAKPOINT ([Called gap roots](#called-gap-roots)).
- **`0x49688c` `_abort`, `0x49679c` `__exit`**: process ends, reached only
  through the report; they end through the process platform.
- **`0x4967fc` `_time`**: row `file_time` over the files' OS clock
  ([_time in es2sim](#_time-in-es2sim)).
- **`0x4968f8`**: a `jmp [0x6cc3f0]` import thunk; `init.c` calls it only when
  the byte `0x497058` is set, which is 0 in the image; refused on purpose.
- **`0x47a1ac`, `0x47a55c` and the writer classes behind `0x482ed0`**
  (`0x47a638`, `0x47a798`, `0x47c284..0x47c570`): dead code of the closed store
  closure; no pointer references; their only callers are the slot functions
  `0x483458` and `0x4833f4` of the S/W closure, which nothing reaches; Live 0
  in all 40 suites.

**Virtual target verdicts.**

- **INP (6, 5 live):** own code in `sim/src/input.c` without rows: `0x45c2e0`
  (joystick destructor, table `0x49ec74` slot 0) with `0x477630` and
  `0x477714`; `0x45c330` (the poll, slot 1) with `0x47785c` and `0x4778a8`. The
  joystick object is a guest object whose slots own code runs inline.
  `0x4778a8` (mode 2) is the one with Live 0.
- **FILE (22):** unreached classes, Live 0 except `0x47a4a0`: the VOL stream
  slots `0x473510`, `0x473578`, `0x4735a4` (`0x4a0e10` slots 3..5); the
  write-stream slots `0x47b0d8`, `0x47b1e4`, `0x47b200`, `0x47b28c`, `0x47b2f8`
  (`0x4a1760`) with `0x47b294`; the memory stream slot `0x47b3d4`
  (`0x4a1788`); `0x47a594` (`0x49d984` slot 5, and 13 other tables) with
  `0x4967d2` and `0x4967f6`; the writer pieces above. `0x47a4a0` (40/40 live,
  behind `0x47a594`) is inlined by `load.c`'s class lookup.

**The gap list.** The roots `0x45ceb8` and `0x45f070` and with them `0x45effc`,
`0x479c54`, `0x479dd0`, `0x479e48`, `0x479e8c`, `0x479f24` and `0x479c78`
leave `sim_gap.md` at its next regeneration; the other roots and targets stay
listed with their verdicts.

**Open.** The rows are not on the recorded replays' paths (F1 help, `-D`,
asserts).

## The error paths' MessageBoxA, PostQuitMessage, __exit and _abort windowed

The guest's `MessageBoxA` shows a real modal box in the windowed build and
returns its answer; `__exit` and `_abort` end the process with the original's
status in both builds. Code: `sim/include/es2/sim/process_platform.h` (the
record), `sim/src/loop.c` (`es2_sim_process_message_box`,
`es2_sim_process_abort`, the report box, `report_process_end`),
`sim/src/startup.c` (`message_box`, `assert_log`, `fatal`), `sim/src/main.c`
(the windowed and headless records).

**The calls (listing).** MessageBoxA's thunk is `0x496928` (`jmp [0x6cc414]`);
each site pushes four arguments:

| Site | Caller | Owner | Style | Caption | Answer |
|---|---|---|---|---|---|
| `0x47a08e` | `0x479f24`, severity 5 (every one of DBSIM's 43 callers of `0x479c78` passes 5) | 0 | 0 | `0x4a1664` | not read |
| `0x479fb3` | severity 4 | 0 | 0 | `0x4a1656` | EAX of `0x479f24` |
| `0x479ff4` | severity 3 | 0 | 0 | `0x4a165c` | not read |
| `0x45f377` | the eggplant box (game main, no `-eggplant`) | 0 | 0x30 (MB_ICONEXCLAMATION) | error.str group 2 entry 0 | not read |

The eggplant box's text is error.str group 1 entry 4; `0x4669b4` runs before
it. The fatal report `0x479c78` ends, for severities 4 to 6, with
`PostQuitMessage(0)` (thunk `0x4969fa`), then `__exit(1)` (`0x49679c`, push 1
at `0x479cd9`). `0x479e8c` calls `_abort` (`0x49688c`) when `_open` of the log
fails.

CW3220 (rtl_imports.md "abort, __ErrorExit, __ErrorMessage"; own model
`core/src/rtl_start.c`): `__exit(status)` is `ExitProcess(status)` (no atexit
functions, no exit records, no stream flush). `_abort` calls `raise(SIGABRT)`
with the default handler, then `__ErrorMessage("Abnormal program
termination")`, then `__exit(3)`. `__ErrorMessage` calls `MessageBoxA(NULL,
text, title, 0x10010 | modality)` with the title the module file name after
its last `\` or `:`; the modality is MB_TASKMODAL (0x2000), MB_SYSTEMMODAL only
when GetVersion's bit 31 is set and the thread has no window. The main window
exists before any code that can reach `_abort`, so every reachable call uses
style 0x12010 (static reading; no live run has reached `_abort`).

**The process platform** (`es2_sim_process_platform`; `es2_sim_loop.process`
and `es2_sim_startup.process` point to one record): `message_box(user, owner,
text, caption, style)` returns the answer; `process_exit(user, status)`;
`module_name`; `process_fault(user, code, site)` ([The original's fault
paths](#the-originals-fault-paths)).

- **Windowed:** `message_box` is Win32's `MessageBoxA(owner, text, caption,
  style)` with the guest's ANSI bytes (and prints `es2sim: MessageBoxA(owner,
  "text", "caption", style) = answer`). `process_exit` prints the window and
  audio statistics and `es2sim: __exit; ExitProcess(n) without exit records`
  (or `_abort`), closes the host's log with reason `fatal_error` (or `abort`)
  and the status, and calls `ExitProcess(status)`. `module_name` is the mapped
  EXE's file name (standing in for GetModuleFileNameA(NULL)). The status is 1
  for the fatal report and 3 after `_abort`'s box.
- **Headless:** the record has no `message_box`: MessageBoxA is counted
  (`platform.message_box`, the caption and an FNV-1a hash of the text in
  `es2_sim_fatal_box_last`) and answers IDOK. `__exit` ends with 1 (reason
  `fatal_error`) and `_abort` with 3 (reason `abort`).
- **Tests:** a recorder in the record returns from `process_exit`; the main
  loop then ends with outcome `ES2_SIM_LOOP_EXITED` and `exit_code`, and
  `es2_sim_loop_call` and the call helpers keep EXITED. The start-up ends with
  outcome EXIT and the status. A NULL record (the library default and most
  unit tests) refuses at `0x49679c` / `0x49688c`.

Win32 rather than SDL: `SDL_ShowSimpleMessageBox` takes UTF-8 and has no MB_*
style bits, so it cannot pass the original's bytes, style or answer. The owner
is 0, as in the original; the box does not disable the game window.
`ES2SIM_MESSAGE_BOX=none` is a test switch for runs without a desktop: the box
is printed, not shown, and answers IDOK.

**WM_QUIT.** `es2_sim_post_quit` sets `quit_posted`. The pump's peek returns
the journal, the posted messages, the window's queue (SDL input; WM_PAINT is
held back while WM_QUIT is pending), then WM_QUIT: Windows' retrieval order
(posted, input, quit, paint). In `0x479c78` `__exit(1)` follows
PostQuitMessage directly, so no pump runs in between; `loop_test` checks that
WM_QUIT is pending at the exit call and that a later pump returns it after the
posted and the window's message.

**Deviations forced by the platform.** Messages for the game window are
dispatched inside MessageBoxA's modal loop in the original; the own window
procedure runs on the simulation thread, which is blocked in the box, so SDL
queues them until the box returns (on the fatal path never, `__exit` follows).
The SDL mixer is pumped on the simulation thread, so sound stops while a box
is open (SOS kept mixing). `ExitProcess` skips the frame capture's
`close_frames` atexit (`--frames`), as `__exit` runs no atexit either.

**Tests.** `//sim:loop_test` `fatal_box` (0x479c78 headless and with a
recorder: text, caption, owner 0, style 0, WM_QUIT pending, status 1; severity
4 via `0x479f24`: EAX the recorder's answer (2); `_open` failing: the box
"Abnormal program termination", caption `module_name`, style 0x12010, status
3; a record with only a box). `//sim:startup_test` case 12 (the eggplant box:
style 0x30, owner 0, error.str's text and caption; no drive.cfg: style 0,
caption `0x4a1664`, `__exit(1)` after PostQuitMessage; assert.log as a
directory: the abort box, caption "DBSIM.EXE", style 0x12010, exit code 3).
`//sim:es2sim_window_test` with ES2_DATA_ROOT, both builds as processes (no
drive.cfg headless: exit 1, no box line; windowed: exit 1, the box line, the
ExitProcess line, the log's exit event `fatal_error`; no `-eggplant` windowed:
style 0x30). The mutation "report box style 0 → 0x30" is caught.

**Live (owner).** The fatal box (no `data\drive.cfg`), the eggplant box and the
script.dat box ([The mission start's clean-up 0x461304 and the call helpers'
EXITED](#the-mission-starts-clean-up-0x461304-and-the-call-helpers-exited))
were seen as expected, with `assert.log` holding the line. The main loop's
report (the flush `0x401e5c`, vcr.cpp line 0x2f, when `<name>.dmp` of a
`-r<name> -d<name>` run cannot be opened) needs a fault set up by hand and was
not run.

**Open.** The abort box's style 0x12010 and title come from a static reading of
CW3220. The two deviations (messages and sound while a box is open) stay until
the window procedure and the mixer run off the simulation thread.

## The error report 0x479c78 at the sites that refused

Every own site where the original calls the error report `0x479c78` calls it
through the dispatch with the arguments the original pushes; if the report
returns, the code goes on with the instructions after the call, as the
original's bytes do. Code: `sim/src/load.c` (`fatal_report` after the `CALL`
macro), `sim/src/hud_internal.h` (`hud_fatal_report`), `sim/src/hud_cockpit.c`,
`sim/src/hud_gauges.c`.

**The sites (listing; copy SHA-256 `8561aa47...c4ad`,
[reference_version.md](../reference_version.md)).** The listing has 43 calls of
`0x479c78`. All pass the record `0x6c56c8` and severity 5. The third argument is
the `sete`/`and 1` of the failed test, 0 at every site here (at `0x44a992` it is
`[R+0x18]`, 0 on that branch). Arguments after record and severity: (third,
file string, line, text).

| Call VA | File | Function (row) | Arguments | After the call (if the report returns) |
|---|---|---|---|---|
| `0x461524` | load.c | launch peek `0x4614c0` | 0, `0x49f86d`, 0xde, the path of `0x492b90` (`0x6cbc0c`) | R.read(0x14, f-44h) `0x47af98` |
| `0x461587` | load.c | launch peek `0x4614c0` | 0, `0x49f886`, 0xe4, `0x49f88f` | the five R.read(2) through the vtable slot +0x18 |
| `0x474d3e` | load.c | shape list `0x474cd8` | 0, `0x4a0f38`, 0x7e, the path (slot 1) | the skip test, then object_skip or dts_load |
| `0x474e3c` | load.c | shape at `0x474de4` | 0, `0x4a0f42`, 0x9e, the path (slot 0) | object_skip `0x47a3b4` |
| `0x492fab` | load.c | table reader `0x492f54` | 0, `0x4a69d4`, 0x15, the path | stream_read `0x4735b8` |
| `0x492feb` | load.c | table reader `0x492f54` | 0, `0x4a69dc`, 0x17, the path | stream_destroy `0x472f64` |
| `0x437a62` | load.c | damage display `0x4379cc` | 0, `0x49bc54`, 0x48, the name (slot 0) | S.read(4) |
| `0x40fa18` | load.c | weapon shapes `0x40f998` | 0, `0x4991e9`, 0x90, the path | the object size, then new[] `0x4774f0` |
| `0x429d14` | hud_cockpit.c | view shape `0x429c2c` | 0, `0x49ae7a`, 0x12e, a second `0x492bec(D+0x2d, f-54h)` | dts_load `0x47a144` |
| `0x429d97` | hud_cockpit.c | view shape `0x429c2c` | 0, `0x49ae87`, 0x135, a second `0x492bec(D+0x2d, f-54h)` | dts_load `0x47a144` |
| `0x4317c6` | hud_cockpit.c | panel layout `0x431778` (through the row `0x431bf8`) | 0, `0x49b754`, 0x1d6, the path | new(0x6a4) `0x47749c` |
| `0x44a905` | hud_gauges.c | radio record `0x44a7e8` | 0, `0x49d52d`, 0x3ee, `0x49d536` | the 0x1b offset reads |
| `0x44a992` | hud_gauges.c | radio record `0x44a7e8` | `[R+0x18]` (0), `0x49d55a`, 0x406, `0x49d563` | the labels (new(0x21) first) |

At `0x429d14` and `0x429d97` the original builds the path a second time for the
text argument and the own code makes the same `0x492bec` call; its result is
the same buffer `0x6cbc2a`, so the synthetic test cannot tell the second call
from the first, and no mutation covers it.

**Other sites.**

- Called through the dispatch in `core/` (the type loaders), with the listing's
  arguments and continuing after it: building_types.c `0x406024`, `0x4061e4`,
  `0x4062c6`, `0x406586`; effect_pools.c `0x407d37`, `0x4087d4`, `0x46b304`;
  stage2_types.c `0x40a87a`, `0x40ae4e`, `0x40b75e`, `0x40fd7c`, `0x41fe28`,
  `0x422e06`; collision_data.c `0x40cd2a`, `0x40cdd1`; damage.c `0x40d1a9`;
  type_loads.c `0x40ff3f`; results_writer.c `0x42419c`, `0x4242c8`;
  mission_load.c `0x424420`, `0x4244b8`, `0x4246a0`; terrain_load.c
  `0x427980`; control_record.c `0x45b31e`.
- In `sim/src`: the dump flush `0x401e5c` in loop.c (`0x401eb1`, through
  `es2_sim_loop_call`), and the scene's `0x4281b0` (`0x42820c`) and `0x4282f8`
  (`0x42832d`).
- **startup.c `0x45f160`** (`0x45f1c0`, `0x45f32b`, `0x45f3dc`): the start-up's
  own model of the report; it ends with outcome EXIT and code 1. Its refusal
  "a severity other than 5" cannot be reached (every caller passes 5).

**The call helpers keep EXITED.** Every call helper (`call_regs` in load.c,
`hud_call_regs` in hud_internal.h, the ones in scene.c, audio.c, frustum.c,
hud_frame.c, input.c, shape_draw.c, terrain_draw.c (two) and view.c) returns
with the outcome kept when a refused call ended the process inside it
(`if (l->outcome == ES2_SIM_LOOP_EXITED) return 1;` before the REFUSED line),
as `es2_sim_loop_call` does; otherwise a windowed report reached through them
would end with 70 instead of 1. The remaining REFUSED writes in `loop.c`
(`es2_sim_loop_refuse` and the exit records) are real refusals.

**Tests.** `//sim:fatal_sites_test` (synthetic): one case per site above (a
missing file, an open status, AL 0 from the read, an object of another class,
a NULL portrait leads to one report call with the arguments above; the next
call is the original's continuation); the report's own row on the table reader
and the panel layout (headless with a NULL record: refused at `0x49679c`, the
box counted; with a recording record: EXITED with code 1, one box and one
`__exit(1)`). Five mutations are caught (line 0xde → 0xdf at `0x461524`; the
EXITED line removed from `call_regs`; from `hud_call_regs`; a return right
after the report at `0x492fab`; the third argument 1 at `0x44a992`). In vitro:
gap_roots_shell, hud_cockpit, cockpit, cockpit_rest, gauges, hud_radio, cockpit_pixels, scene,
load_rest, type_loads, mission_root, mission_actors, mfd_orders.

## The mission start's clean-up 0x461304 and the call helpers' EXITED

The mission start `0x461608` registers `0x461304` with the notify list
`0x479cfc` (`push 0x461304; call 0x479cfc` at `0x461626`), so the error report
runs it before its box. `0x461304` and its only new callee `0x46a4ac` are rows
(`sim/src/loop.c`: `cleanup_call` (one dispatch call with the entry
registers, the slot read back after the call), `remap_release`,
`abort_cleanup`, `es2_sim_service_abort_cleanup_461304`,
`es2_sim_service_remap_release_46a4ac`; fragment
`sim/dispatch/services/mission_abort_cleanup.h`: `abort_cleanup` `0x461304` and
`remap_release` `0x46a4ac`, CDECL, EAX, no slots; no shims). The callees stay
rows: `sound_close` `0x462734`, `file_fclose` `0x496838`, `operator_delete`
`0x477548`.

**`0x461304..0x46134a`** (listing; copy SHA-256 `8561aa47...c4ad`):

1. `call 0x462734` (the sound close).
2. `mov eax,[0x4d24e2]`, `test`, `je 0x46133c`. If set: `push eax`,
   `call 0x496838`, `pop ecx`, `xor edx,edx`, `[0x4d24e2] <- edx`.
3. `mov eax,[0x4d24de]`, `test`, `je 0x46133c`. If set: the same with
   `[0x4d24de]`.
4. `0x46133c`: `call 0x46a4ac`, `xor ecx,ecx`, `[0x4d27b8] <- ecx`, `ret`.

Both tests jump to the same place, so without a tape stream the dump stream is
not looked at. `0x496838` is the thunk `jmp [0x6cc36c]`, the import `_fclose`
of cw3220.dll; `[0x4d24e2]` is the FILE* of the playback tape and `[0x4d24de]`
the dump stream (`-d`): `0x461304` closes the two streams.

**`0x46a4ac..0x46a4d3`** (only caller `0x461304`): `cmp word [0x4a0918],0`,
`je` to the `ret` (without the flag a bare return, every register as at the
entry). With the flag: `mov eax,[0x6b4f20]`, `push eax`, `call 0x477548`
(operator delete), `pop ecx`, `xor edx,edx`, `[0x6b4f12] <- edx`,
`word [0x4a0918] <- 0`, `ret`. The cleared dword is `0x6b4f12`, **not** the
block pointer `0x6b4f20`, which stays.

**Registers.** `0x46a4ac` with the flag: EAX delete's, EDX 0, ECX the argument
slot as it is after the call (the `pop ecx`). `0x461304`: ECX 0; EAX and EDX
what `0x46a4ac` leaves, that is the entry values from `0x462734` or the last
fclose when the flag is clear; after an fclose EDX is 0 and ECX the popped slot
until `0x46a4ac`.

**Tests.** `//sim:abort_cleanup_test` (synthetic image, callees replaced by
recording stubs after the seal): `0x461304` in six states (tape, dump, flag):
call order, arguments and entry registers, the cleared globals and the final
registers; `0x46a4ac` alone with and without the flag; the report with a notify
node `0x461304` (with a NULL record refused at `0x49679c`; with a recorder
EXITED with code 1, the clean-up run before the box); propagation: the real
`sound_close` runs and its `0x466010` call ends the process through the report
with the recorder, outcome EXITED with code 1. Confirmed in vitro:
`differential_mission_abort_cleanup_test` (`mission_abort_cleanup_cases.h`): the original's `0x461304` and
`0x46a4ac` against the rows, with `0x462734` and `0x477548` code-patched and
the `_fclose` import slot patched on the original side, the three rows replaced
on the own side; stubs log their entry registers and argument, leave seeded
registers and in half the calls rewrite their argument slot (so the `pop ecx`
shows); random streams, flag, block, cleared dword and exit code; EAX, EDX, ECX
of every call, DATA, heap and the stub log compared; 3000 cases identical;
every coverage counter non-zero. Mutations caught: `remap_release` without EDX
0; `0x6b4f20` cleared instead of `0x6b4f12`; audio.c's helper without the
EXITED line. `es2sim_real_test` case 3: the launcher's line without script.dat
calls `0x461304`, writes assert.log with `Line 222 in "INIT.CPP":
data\script.dat` and ends with the fatal report's code 1.
`es2sim_window_test` windowed (dummy driver): the "Error" box with line 222,
`__exit`, exit code 1 (`fatal_error`).

**Live (owner).** In a directory with `data\prefs.cfg` (seven zero bytes) and
`data\drive.cfg` but no `data\script.dat`, `es2sim_window --arguments "dummy
-eggplant -X2 -R0 -G"` showed the "Error" box with `Line 222 in "INIT.CPP":
data\script.dat`; after OK exit code 1 and assert.log in the directory.

## The after-load snapshot against the original

Details, tables and the live procedure: mission_load.md section 14, "es2sim's
snapshot against the original".

- es2sim writes its after-load snapshot (flags `own_runtime_heap` and
  `armed_early`) at the original's step; with the snapshot on, the replays
  stay identical to their goldens (acceptance-60s, c3_10-loadout-hercs,
  c3_09-loadout-b). Two headless passes compare identical in those three
  suites (`--scope all` and `pools`); only the foreign sample-block pointers
  vary between them.
- The building of the guest image from own tables changed one snapshot byte
  pair, compared on the same three suites (`after_load_snapshot.py compare`,
  a build that still mapped the file against the current one): `--scope
  pools` IDENTICAL; `--scope all` one difference each, bytes +6..+7 of the
  16-byte heap block of the graphics-library object `new(6)` at G+0x4e
  (`0x4d250a`), `0x1026` against `0x1028`. That is allocation slack beyond
  the 6-byte object: the upper half of a stale pointer into es2sim.exe's own
  read-write data, which moved from `0x10242000` to `0x1025b000` because
  `.rdata` grew by the compiled-in runs. Host state, not guest state.
- No original `.snap` files are kept in `es2-data`, so the compare against the
  original needs a live run: two original runs with
  `ES2_AFTER_LOAD_SNAPSHOT_EXTRA=0x6cbd58:0x18c`, a noise file, then
  `compare O1 es2sim --ignore 0x6cbd58:4 --ignore 0x6cbd60:0x184`.
- The snapshot differences between the own heap model (`core/src/rtl_heap.c`,
  shared by es2sim and the own cw3220.dll) and CW3220's allocation history
  ([mission_load.md "The noise model"](../reverse-engineering/mission_load.md#the-noise-model)) have three causes: allocation slack, the
  whole-block fit below the 16-byte minimum, and the byte counter
  `0x6bb30c`. `after_load_snapshot.py` counts them as classes when the two
  heaps' runtimes differ.
- es2sim's heap (`0x20000000`) and the original's (`0x025xxxxx..0x09bxxxxx`)
  do not overlap, so equal bytes cannot pair by chance (the cause of the
  c3_09 conflicts between original runs).

**Open.** The live compare above (expected `IDENTICAL`, with the classes
counted). Whether the 190 unlisted differences of `snapshot-r7c`'s
own-runtime load all fall into the three classes; the rerun's `CLASSES` line
and its `block_size_delta` settle it.

## es2sim's frame times

The instrument that measures whether the windowed es2sim holds the original's
40 ms limiter (`0x4678c0`, previous + 40) with margin. Stage 1 sends every
machine call into CODE through the trap at 11.58 µs per trap (section 4.4);
at about 3,200 traps per windowed frame the trap cost alone was about 37 ms,
so stage 1 could not hold 40 ms and the bridge seam was built ([Stage 2: the
bridge seam for the hot VAs](#stage-2-the-bridge-seam-for-the-hot-vas)).

### The instrument

- **Switch.** The environment variable `ES2SIM_PERF`: off when unset, empty
  or `0`; `1` writes `perf.ndjson` next to the `--tick-log` (or
  `es2sim-perf.ndjson` in the current directory without a tick log); any
  other value is the file path. Both builds have it. Switched on, es2sim
  prints `es2sim: frame times (<mode>) to <path>`.
- **What it reads.** Only the wall clock (QueryPerformanceCounter) and the
  dispatch's counters; it writes only its own file. It touches no guest
  memory, no virtual time and no tick-log line; replays with the switch on
  stay IDENTICAL.
- **Code.** `sim/include/es2/sim/perf.h` (documents the format) and
  `sim/src/perf.c`. `sim/src/main.c` calls it from the loop's `before_logic`
  callback (start of the work), `on_tick` (the frame, before the tick-log
  line) and `finish_run` (the end line, before `es2_sim_finish`). Windowed,
  the window layer's `delay_ms` is wrapped while the recorder is on, so the
  sleeps count as wait. `es2_sim_entry.traps` (dispatch.h, counted in
  `es2_sim_trap_prepare`) is the per-entry trap count; `calls` counts trapped
  plus own C calls.
- **Format.** NDJSON with three kinds of line:
  - a header: mode, clock rate, the set-up time to the first logic frame,
    and the counters at that point;
  - one line per frame: `us` the wall time since the previous frame's tick,
    `work` from the logic call to the tick, `wait` the window layer's sleep,
    and this frame's `traps` and `calls`;
  - an end line: totals since the first frame, plus the 24 entries with the
    most traps over the whole run, each as `[va, traps, calls]`.

  Busy time is `us − wait`; headless, `wait` is 0.
- **Summary.** `sim/tests/perf_suite.py summary <files or directories>`
  prints per file: frame and busy time (median, p95, max); traps per frame
  (median, mean, max) and calls per frame; the share of frames whose busy
  time is over 40 ms (`--budget-ms`) and over the 47 ms virtual-time step;
  the trap share of the busy time (traps × 11.58 µs); the set-up time.
  `--json OUT` also writes the figures as JSON.
- **Run.** `perf_suite.py run`: headless it runs `replay_suite.py` with
  `ES2SIM_PERF=1` and keeps the replay's verdict (anything other than
  IDENTICAL or SKIP fails). With `--window` it starts `es2sim_window` in the
  suite's prepared working directory with `[run] frame_limit`
  (`--window-frames`, by default the suite's frame limit or ticks), sets
  `SDL_VIDEODRIVER=dummy` and `SDL_AUDIODRIVER=dummy` (`--real-audio` keeps
  the real audio device) and writes `perf-window.ndjson`,
  `tick-log-window.jsonl` and `window-stdout.txt`/`window-stderr.txt`. Every
  child starts with CREATE_NO_WINDOW.
- **Tests.** `//sim:perf_test`: the switch's paths, a fake-clock run (header,
  frames, wait, nested and unmatched waits, the hot list ranked by traps),
  the clock conversion, an end before any frame, the off state, a failed
  write. `//sim:perf_suite_test`: the summary on synthetic files and `run`
  against a stub replay driver; a DIFFERENT verdict fails the run. Three
  mutations of `perf.c` (no wait reset per frame, trap delta against the
  run's base, hot list by calls) each fail `perf_test`.

**What the dummy driver leaves out.** The window figures come from SDL's
dummy video driver: no real present (GPU upload, compositor, vsync, scaling of
the 640×480 surface), no real input (windowed, the `[input]` journal is
ignored, so the mission runs without input: about 3,200 traps per frame
against 4,000 to 5,000 in the same suite's replay before the seam), no real
audio device (unless `--real-audio`) and no CD/MCI music. The paced frame
itself is measured: the clock follows the wall clock and sleeps through
`delay_ms`, the recorder's `wait`.

### How to run it (quiet machine)

From PowerShell in the repository, with nothing else running; `<base>` is the
Bazel output base.

```powershell
$env:ES2_DATA_ROOT = 'build\extracted\cd'
bazel --output_base=<base> build //sim:es2sim //sim:es2sim_window
$bin = "$(bazel --output_base=<base> info bazel-bin)\sim"
$out = '<es2-data>\logs\t69-quiet-<date>'
<es2-dependencies>\python\python.exe -B sim\tests\perf_suite.py run `
  --sim "$bin\es2sim.exe" --window "$bin\es2sim_window.exe" `
  --suite acceptance-60s --suite c1_01-cockpit-keys --suite c1_01-free-camera `
  --output-dir $out --json "$out\summary.json"
<es2-dependencies>\python\python.exe -B sim\tests\perf_suite.py summary $out
```

For a single binary by hand: `$env:ES2SIM_PERF = 'D:\…\perf.ndjson'`, then the
usual es2sim command line. The visible window: start `es2sim_window.exe`
without `SDL_VIDEODRIVER` (a visual run for the owner).

### Figures (quiet machine)

acceptance-60s, `perf_suite.py run` headless with the replay verdict and
windowed on SDL's dummy driver, each build from a clean checkout. Evidence:
es2-data `logs\t69-quiet-20261022\<commit>` (`summary.json` and the per-frame
records).

| Build | Mode | Frame ms median / p95 / max | Busy ms median / p95 / max | Traps per frame median / mean / max | Over 40 ms | Trap share of busy |
|---|---|---|---|---|---:|---:|
| stage 1 (trap only) | headless | 53.4 / 64.5 / 83.2 | 53.4 / 64.5 / 83.2 | 4481 / 4300.9 / 6282 | 93.3 % | 96 % |
| stage 1 | window | 47.0 / 48.9 / 57.5 | 43.1 / 45.8 / 51.8 | 3192 / 3235.3 / 3905 | 100 % | 86 % |
| ten bridged VAs | headless | 22.5 / 27.6 / 33.5 | 22.5 / 27.6 / 33.5 | 1512 / 1417.1 / 2586 | 0 % | 76 % |
| ten bridged VAs | window | 47.0 / 48.6 / 55.1 | 19.7 / 21.1 / 27.9 | 995 / 1016.6 / 1538 | 0 % | 59 % |
| 64 bridged VAs (current) | headless | 11.8 / 13.4 / 21.3 | 11.8 / 13.4 / 21.3 | 451 / 433.9 / 1037 | 0 % | 43 % |
| 64 bridged VAs | window | 47.0 / 48.5 / 53.8 | 13.0 / 14.2 / 22.8 | 329 / 341.9 / 832 | 0 % | 30 % |

All replays IDENTICAL (1500 ticks); calls per frame equal in every build
(10041.3 headless, 8038.8 windowed; 8034.4 with stage 1, where the windowed
count varies slightly because the window runs on the wall clock without
input). The windowed frame time is the limiter's 47 ms step. Its busy part
fell from 43.1 ms (over the budget in every frame) to 13.0 ms; the worst frame
(22.8 ms busy) keeps a margin of about 17 ms. The remaining 30 % trap share
windowed (about 330 traps per frame) is material for a further stage, not
needed for the 40 ms limit on this machine.

**Open.** The visible window (a real present with vsync) is the owner's
visual run; the dummy driver does not measure it.

## Stage 2: the bridge seam for the hot VAs

Calls to the listed VAs from the seamed bridges reach the dispatch without the
vectored trap. Behaviour is unchanged: `calls` stays the same, `traps` drops,
the replays stay IDENTICAL. Every faulting caller was own host code; no
original instruction calls these VAs inside es2sim.

### The seam

- **`es2_sim_bridge_target(va)`** (`sim/src/dispatch_trap.c`, declared in
  `es2/sim/trap.h`) looks `va` up in a sorted set (binary search). If `va` is
  not in the set, or the caller is not the trap thread, it returns `va`
  unchanged and that call faults as before. If it is, it sets the handler's
  pending state exactly as `trap_handler` would (`trap_pending_key`,
  `trap_pending_eip`, `es2_sim_trap_pending_shim`, plus `trap_bridged`) and
  returns `es2_sim_trap_entry`, or `es2_sim_trap_shim_entry` for an entry with
  a shim. The caller's CALL enters that trampoline with the same registers,
  stack and EBP as the faulting CALL would have, because nothing between the
  lookup and the CALL reaches the dispatch.
- **`es2_sim_trap_prepare`** counts `++e->bridges` and the global
  `es2_sim_bridge_calls()` instead of `++e->traps` when `trap_bridged` is
  set; `calls` counts both ways. `es2_sim_entry.bridges` is diagnostic only
  (the perf recorder has no bridges field).
- **`es2_sim_bridge_set(vas, n)`** checks the set: ascending, at most
  `ES2_SIM_BRIDGE_MAX` (64) entries, each a non-refused dispatch entry whose
  trap key is the VA itself (so import-slot traps are refused). If any check
  fails the set is empty, and es2sim stops at set-up with "bridge" (exit 67).
  `es2_sim_trap_remove` clears it.
- **Compile time.** `tools/reference-game/proxy/sim_bridge.h` defines
  `ES2_SIM_BRIDGE(va)` for C bridges and `ES2_SIM_BRIDGE_ARG8` for naked asm
  bridges (pushfl, replace the cdecl target at 8(%ebp), popfl, so the flags
  at the CALL are kept; ESP kept; EAX, ECX, EDX clobbered). Both are active
  only under `ES2_SIM_HOST`, a `local_define` of `//sim:adapters`; elsewhere
  they expand to the identity, and the proxy `ddraw.dll` and
  `differential_harness.exe` are byte-identical with and without the seam.
- **Seamed call sites.**
  - asm, `ES2_SIM_BRIDGE_ARG8` right after the prologue:
    `weapons_machine_call`, `weapons_exception_call`,
    `ai_behaviour_stack_call`, `weapon_slots_machine_call`,
    `logic_frame_machine_call` (before the host ESP and the target are saved
    to `_logic_frame_host_esp` and `_logic_frame_target`),
    `flyer_control_stack_call`, `flyer_states_stack_call`,
    `weapon_system_esi_call`, `route_esi_call`;
  - C: `math_guest_call`, `math_effects_call`, and in math_hooks.h the macro
    `MATH_GAME_TARGET(image,rva)` (`ES2_SIM_BRIDGE` on image+rva) used by
    `math_advance_original` (`0x478d38`), `math_animation_step_game`
    (`0x40250c`), `math_track_rate_game` (`0x4796d8`),
    `math_actor_animate_game` (`0x402604`), the `MATH_GAME1` functions
    (`0x402628`, `0x418f40`, `0x418f74`, `0x4027fc`, `0x492ee0`,
    `0x462830`), `math_effects_game` (`0x4195c8`, through
    `es2_hook_machine_call`), `math_height_game` (`0x46e180`) and
    `math_clip_game` (`0x4792ac`, through `es2_hook_machine_call`);
  - `_es2_sim_trap_call` (sim's own machine call, so `es2_sim_call` of a shim
    entry does not fault).
- **Shim entries.** The shim trampoline zeroes 4 KiB below ESP and does not
  keep the flags on the trap path either, so it behaves the same through the
  seam.

### The bridged set

`host_bridged_vas` (`sim/src/host.c`), 64 VAs, sorted. The services among
them are `0x476850`, `0x48e3e0`, `0x49630c` (reads `call.ebp`; through the
trampoline that is the CALL's EBP, as with the trap) and `0x49681a`; the
entries with shims are `0x411cec`, `0x4801d4` and `0x49290c`.

| Area | VAs |
|---|---|
| 0x40xxxx | 0x40250c animation_step, 0x402604 actor_animate, 0x4029d8 terrain_pose, 0x4035a4 base_radius, 0x4035b8 base_sight_radius, 0x403548 base_aim_node, 0x403914 base_parts, 0x403ca8 base_update, 0x404ae4 obstacle_test, 0x405038 base_hit_test, 0x40e62c weapon_ammo, 0x40ede8 weapon_ready, 0x40ef94 weapon_step, 0x40f881 mask_merge |
| 0x41xxxx | 0x411aac actor_sight_radius, 0x411cec actor_step, 0x412608 sight_check, 0x4128f8 contact_scan, 0x41552c mech_sight_radius, 0x417b98 mech_aim_node |
| 0x42xxxx | 0x4233a4 region_test, 0x4234b8 trigger_regions, 0x4236c4 group_activate, 0x423a74 order_update, 0x427238 cell, 0x427c68 collision_walk, 0x427da8 object_query, 0x4280f4 terrain_line |
| 0x46xxxx | 0x467924 movement_step, 0x467a48 timer_word_step, 0x467aa8 timer_dword_step, 0x467adc approach_word, 0x46a5bc convoy_leader, 0x46b910 shape_word, 0x46e180 terrain_height, 0x46e980 segment, 0x46ff4c slope_test |
| 0x47xxxx | 0x470468 edge_test, 0x470d74 cell_mark_set, 0x470e00 cell_flag_get, 0x471c18 pool_next, 0x471c70 pool_prev, 0x476850 part_find, 0x478644 removal_flush, 0x478d38 track_advance, 0x47d32c angle_ratio, 0x47de72 dist3d_delta, 0x47e0a0 multiply_q8, 0x47e0b0 multiply_q10, 0x47ebb8 euler_matrix, 0x47ffe8 rotate_2d |
| 0x48xxxx | 0x4801d4 matrix_point, 0x480304 rotate_point, 0x48043c transform_point, 0x48e3e0 class_new |
| 0x49xxxx | 0x49288c dist_gate, 0x4928d0 dist2d_point, 0x49290c vector_angle, 0x492934 heading_point, 0x4929fc point_offset, 0x492ee0 rng_next, 0x492f24 rng_bounded, 0x49630c (RTL frame record), 0x49681a rt_memset (the CODE jump thunk of cw3220.dll!_memset) |

The set was chosen by traps in acceptance-60s with the recorder's hot list and
a temporary histogram of the faulting return addresses (not committed). The
ten hottest before any seam (whole run, set-up included):

| VA | Name | Traps | Calls |
|---|---|---:|---:|
| 0x471c18 | `pool_next` | 1,009,877 | 1,009,877 |
| 0x471c70 | `pool_prev` | 845,810 | 1,008,554 |
| 0x4035b8 | `base_sight_radius` | 645,850 | 645,850 |
| 0x49288c | `dist_gate` | 528,816 | 596,014 |
| 0x4928d0 | `dist2d_point` | 428,543 | 428,543 |
| 0x4801d4 | `matrix_point` | 304,510 | 304,510 |
| 0x47de72 | `dist3d_delta` | 283,094 | 294,663 |
| 0x467a48 | `timer_word_step` | 205,298 | 205,298 |
| 0x4035a4 | `base_radius` | 193,934 | 268,984 |
| 0x49630c | (RTL frame record) | 145,829 | 145,829 |

They carried about 71 % of the run's 6.45 million traps (the two pool
iterators 29 %). With all 64 bridged, every bridged entry has `traps` 0 in
acceptance-60s (7,843,632 calls, 6,230,899 bridges over the run; the rest
reached the dispatch without a machine call). The remaining 684,844 traps are
spread over about 640 VAs, at most about 10k each (the hottest, `0x423b0c`,
9,761); 406,921 of them at `weapons_machine_call` and 160,958 at
`ai_behaviour_stack_call`, both seamed, for VAs outside the 64. One
attribution trap: 9,691 traps of `0x49290c` showed a host `[ESP]`
(`0x1007c9de`, inside `es2_contact_expire_registers`) that is not a return
address; all 12,806 calls of `0x49290c` now count as bridges.

### Registers at the seam

**The asm seams are neutral.** In every asm bridge, EAX, ECX and EDX are
loaded again from the register block or the arguments after the seam, before
the CALL (by reading each bridge; `trap_calls_test` covers the shape).

**C sites.** At a C site, EAX, ECX and EDX at the CALL are compiler-defined
with the trap or with the seam, and the lookup call changes them even for VAs
that are not bridged (`math_ground_animate_game` in es2sim's listing: before
the seam EAX = the argument, ECX = the target, EDX = a counter pointer; after,
EAX = the lookup's result and ECX, EDX the lookup's). Some leaves pass entry
registers through (`base_radius` returns the entry EDX and ECX), so no
consumer may treat a C bridge's entry registers as defined. Verdicts:

| Target | Verdict |
|---|---|
| `0x478d38`, `0x40250c`, `0x402604`, `0x46e180` | the adapters ignore the entry registers (`(void)regs`) |
| `0x4796d8` | the adapter uses only the stack arguments |
| `0x418f40` (ground_animate, not bridged) | reads entry EAX's high word and passes it into the rate value for `0x4796d8`; that word reaches only the EDX result of `0x4796d8`/`0x47967c`, which `es2_ground_animate_registers` discards; never guest memory |
| `0x4195c8` (effects), `0x4792ac` (clip) | explicit registers through `es2_hook_machine_call` |
| `0x404ae4`, `0x411aac`, `0x41552c`, `0x492934`, `0x492ee0` (from `math_guest_call`) | `0x411aac`/`0x41552c` method slots, results used in their low 16 bits only; obstacle_test overwrites ECX before use and ignores EAX, EDX; heading_point does `(void)r`; `0x492ee0` a legacy rng hook that reads only the stack |
| `0x47ebb8`, `0x480304` (read entry EDX) | reached only through `weapons_machine_call`, `ai_behaviour_stack_call` and `math_effects_call` with registers, which load them explicitly |

**Stack below ESP.** Under the trap the kernel's exception frame is written
below the caller's ESP; under the seam nothing is. Shim entries zero 4 KiB
below ESP on both paths. The bridges that plant residue below ESP
(`flyer_control_stack_call`, `flyer_states_stack_call`,
`weapons_prepare_stack_call`, `weapons_round3_stack_call`) do so after the
seam point, and none of the VAs they call in acceptance-60s is in the set.
Adding such a VA needs a check whether the callee reads below its ESP.

### Tests

- `//sim:trap_calls_test` `bridge()`: one trapped and one bridged call with
  the same inputs; EAX/EDX/ECX, popped bytes, EBX, ST0, the flags (except for
  shim entries) and the counters compared, for a stdcall service, a Borland
  hook, ST0, context, headless, legacy, nested with a trapped or a bridged
  inner call, and an entry with a shim; `es2_sim_call` of a shim entry through
  the seam; set checks (unsorted, unknown, import slot, cleared by
  `es2_sim_trap_remove`). Probe `sim_probe_call_flags` and test shim
  `sim_test_shim` in `sim/tests/trap_probe.S`.
- `regs_both_ways` (PASS line "register-block bridge seam (stage 3)"): a naked
  `REGS_BRIDGE` built with and without `ES2_SIM_BRIDGE_ARG8` after the
  prologue; it copies the target to a global, saves the host ESP, pushes one
  slot, loads EAX, EDX, ECX, EBX, ESI, EDI and a synthetic EBP from a block,
  then calls a recording service (`0xc0` bridged, `0xd0` not). Results, the
  callee's entry registers and EBP, EBX/ESI/EDI after the call and the
  counters agree; an unlisted VA through the seamed bridge still traps with
  the same result.
- Mutations caught: `trap_bridged = 1` removed (trap_calls_test,
  "es2_sim_bridge_calls() == bridged1 + 1u + ..."); the seam dropped from
  `REGS_BRIDGE` (line 305, "es2_sim_bridge_calls() == bridged + 1u"); two
  `host_bridged_vas` entries swapped (`replay_acceptance-60s_test`,
  "SETUP-FAILED host exit 67: es2sim: set-up failed: bridge").
- Pass: trap_calls, trap_refusal, trap_seh, table, host_real,
  hook_registry; in vitro differential_pool_iter, actor_leaves,
  transform_helpers, weapons, ai_behaviour, math, weapon_slots, logic_frame,
  flyer_control, flyer_states, weapon_system, route (unchanged harness).
  Replays IDENTICAL: acceptance-60s, c1_01-cockpit-keys, c3_09-loadout-b,
  c1_01-free-camera, c3_09-wingman-orders. Figures: [es2sim's frame
  times](#es2sims-frame-times).

### Open

- **The set is full.** Going further needs a larger `ES2_SIM_BRIDGE_MAX` (the
  top 128 VAs would have covered 87.8 % of the traps that the ten left); that
  changes a rule of the set.
- **Unseamed sites.** `weapons_prepare_stack_call`,
  `weapons_round3_stack_call`, and the bridges at commands_hooks.h:42 and
  input_dispatch_hooks.h:31 had no traps in acceptance-60s; seam them when a
  suite shows traps there.
- **C sites** give the callee compiler-defined entry registers that differ
  from build to build; adapters must keep ignoring them.

## Headers, imports and the icon as own tables

The three things es2sim would take from the headers and directories of
`DBSIM.EXE` (dbsim_gap.md "What `es2sim` takes from `DBSIM.EXE`") are own compiled-in tables:
`sim/data/dbsim_image_tables.c`, generated by
`tools/reference-game/scripts/dbsim_image_tables.py` from the reference
`VER95/DBSIM.EXE` (724,512 bytes, SHA-256 `8561aa47…c4ad`, checked before
anything is read). The generator has its own PE reader; its output is
committed and is the source of record. No build or test runs it, except the
fixture test and the real-data check that regenerates and compares.

| Table | Content | From |
|---|---|---|
| `es2_sim_dbsim_layout` | an `es2_sim_image_info`: image base `0x400000`, size `0x2d8000`, headers `0x400`, entry RVA `0x1000`; the 6 sections (`CODE`, `DATA`, `.idata`, `.edata`, `.reloc`, `.rsrc`) with RVA, virtual size, raw offset, raw size and flags; the import (`0x2cc000`, `0xc9a`), relocation (`0x2ce000`, `0x8ab4`) and resource (`0x2d7000`, `0x400`) directories; the executable range `0x401000..0x497000` | optional header and section table |
| `es2_sim_dbsim_imports[]` | 134 slots in the file's order: DLL name and the VA of that string, symbol, hint, VA of the hint/name entry, IAT slot VA; all by name. cw3220.DLL 53, USER32 39, KERNEL32 23, GDI32 13, WINMM 4, DDRAW 1, MSVFW32 1 | import descriptors, lookup tables, hint/name entries |
| `es2_sim_dbsim_resources[]` | ICON #1, language 0, 744 bytes at VA `0x6d70b0`; GROUP_ICON `"ES2ICON"`, language 0, 20 bytes at VA `0x6d7398` (one entry: 32 × 32, 16 colours, 1 plane, 4 bits, 744 bytes, ID 1). The bytes as C arrays, each 16-byte line with its VA. These are all of DBSIM's resources | resource directory |

**API** (`sim/src/image.c`, `es2/sim/image.h`):

- `es2_sim_image_info_own(info)`: the layout;
- `es2_sim_imports_own(imports, capacity, count, error)`: `"dll!symbol"` (or
  `"dll!#ordinal"`) and the slot VA;
- `es2_sim_resource_own(type, name, id, bytes, size, error)`: type matched by
  number, name without regard to case, or the ID when there is no name; of
  the languages the neutral one wins, else the first;
- `es2_sim_window_load_icon_own(name, cx, cy, error)` (`platform_sdl.c`):
  `LookupIconIdFromDirectoryEx` and `CreateIconFromResourceEx` on the own
  bytes. The window's class icon comes from it; `icon_name` is the string at
  `0x4a084d` (LoadIconA's argument at `0x465111`).

The file-based counterparts (`es2_sim_image_gate`, `_parse`, `_place`,
`_imports`, `_resource`, the file-based icon loader) live in the testonly
`//sim:image_file` ([es2sim without DBSIM.EXE](#es2sim-without-dbsimexe)).

**Tests.**

- `//sim:image_tables_real_test` (`ES2_DATA_ROOT`, SKIP without), against the
  reference file: the layout equals `es2_sim_image_parse`'s field by field
  (section names over all 9 bytes); the 134 imports equal
  `es2_sim_image_imports`' in order, name and slot; for every own import the
  DLL name at its VA, the hint and symbol at the hint/name VA, and the IAT
  slot holding that entry's RVA; every own resource equals
  `es2_sim_image_resource`'s (VA, size, bytes); the name at `0x4a084d` is
  `es2icon`; the icons from the placed image and from the own bytes are
  equal in colour and mask bits (`GetBitmapBits`) at 32, 16 and the system's
  `SM_CXICON` and `SM_CXSMICON` sizes.
- `//sim:image_test` `own_tables` (no game data): base and size, the imports
  through the API equal the table, slots ascending inside `.idata`, the
  capacity refusal, the resource lookup (case, the group's icon ID and size,
  a missing name or ID).
- `//sim:platform_sdl_test`: the own icon at 32 and 16; a name not among the
  own resources refused.
- `//tools/reference-game/scripts:dbsim_image_tables_unit_test` (7 cases): a
  synthetic PE32 with 4 sections and two import descriptors (by name with
  hint, by ordinal, a descriptor with no lookup table) and a resource tree
  with ICON #7, a named GROUP_ICON and a skipped STRING resource; the exact
  output lines, the language column, four refusals, the CLI's reference gate,
  the committed file's shape; the real case regenerates from the CD and
  compares byte for byte.
- Mutations caught: `es2_sim_imports_own`'s slot VA + 4 (`image_test`,
  `image_tables_real_test`); the generator's hint forced to 0 (fixture test).

**Open.** The visible icon of a real (non-dummy) window is the owner's eyes
(play test); the bit equality above is the in-process evidence.

## The AVI code in DBSIM and es2sim

**Verdict: unreachable.** No reference in DBSIM reaches the AVI block, so no
mission path reaches it; es2sim keeps no counterpart and needs no Indeo path.
`ICSendMessage`, `mciGetDeviceIDA` and `MoveWindow` stay refusal traps.

**Method.** The reference `VER95/DBSIM.EXE` (SHA-256 `8561aa47…c4ad`, on a
copy); its `llvm-objdump -d -M intel` listing with the import thunks named from
the own import table, and `llvm-objdump -p` for the exports. Two byte scans,
both supersets of the real references: per entry, every little-endian dword
anywhere in the file equal to the entry VA (marked when it is a `.reloc`
HIGHLOW site) and every `E8`/`E9` rel32 in CODE landing on it; over the range,
every dword (any byte offset) and every `E8`/`E9`/`0F 8x` rel32 (any byte
offset) from outside `0x471625..0x471a4f` pointing into it. Logs read:
es2-data `logs\video-compat.jsonl`, `dbsim-map-20261012` and
`dbsim-map-20261020` (`map.json`), `fulltest-r12-3-20261022.log`.

### The block: no class, eleven functions

The code is not a class: no constructor, no vtable, no object; eleven plain
functions in `0x471628..0x471a4d` (listing):

| VA (range) | What it does | Imports (site) | Direct callers | Pointer references |
|---|---|---|---|---|
| `0x471628..0x471692` | `ICSendMessage(arg1, 0x403e, block, 0x34)`; the block's 13 dwords from the 13 stack arguments after the handle: arguments 2–4 to `+0`/`+4`/`+8`, 5–8 to `+0x24..+0x30`, 9–14 to `+0xc..+0x20`. stdcall, `ret 0x38` | `ICSendMessage` `0x47168a` | none | none |
| `0x471698..0x471702` | the same, message `0x403c` | `0x4716fa` | none | none |
| `0x471708..0x471772` | the same, message `0x403d` | `0x47176a` | none | none |
| `0x471778..0x4717bf` | `ICSendMessage(arg1, 0x4032, block, 0x1c)`, 7 dwords from arguments 2–8, `ret 0x20` | `0x4717b7` | none | none |
| `0x4717c4..0x4717f3` | `ICSendMessage(arg1, 0x4048, block, 0xc)`, 3 dwords, `ret 0x10` | `0x4717eb` | none | none |
| `0x4717f8..0x471845` | `MCI_OPEN` (`0x803`), flags `0x2000` (TYPE), device type `"avivideo"` (`0x4a0cc4`); returns 1 when the open succeeds; no frame | `mciSendCommandA` `0x471835` | none | none |
| `0x471848..0x471864` | `mciGetDeviceIDA("avivideo")` (`0x4a0ccd`), then `MCI_CLOSE` (`0x804`) of that ID | `mciGetDeviceIDA` `0x47184e`, `mciSendCommandA` `0x47185e` | none | none |
| `0x471868..0x4718b1` | `GetClientRect(arg1)`, then `MoveWindow([0x6b5480], x, y, w, h, 1)`; x/y/w/h from the four dwords at `arg2` when nonzero, else 0, 0 and the client rect's right and bottom | `GetClientRect` `0x47187b`, `MoveWindow` `0x4718a7` | `0x4719b4` (the player) | none |
| `0x4718b4..0x4718d2` | `MCI_CLOSE` of device `arg1` when nonzero | `0x4718cb` | `0x471a35` (the player) | none |
| `0x4718d4..0x4718f4` | window-message handler (stdcall, 4 arguments, returns 0): message `0x3b9` (`MM_MCINOTIFY`) with wParam 1 (`MCI_NOTIFY_SUCCESSFUL`) sets `[0x4a0cb4]` = 1 | none | none | pushed only by the player: to `handler_add` `0x465fec` at `0x47192c`, to `handler_remove` `0x466010` at `0x471a3b` |
| `0x4718f8..0x471a4d` | the player (below) | `mciSendCommandA` `0x471947`, `0x471981`, `0x4719a1`, `0x4719e7` | none | none |

**The `ICSendMessage` wrappers (hypothesis):** the out-of-line copies of the
VfW header's inline helpers `ICDecompressEx` (`0x403e`),
`ICDecompressExBegin` (`0x403c`), `ICDecompressExQuery` (`0x403d`),
`ICDrawSuggestFormat` (`0x4032`) and `ICSetStatusProc` (`0x4048`): the
message numbers are `ICM_USER` + 62/60/61/50/72, and the block sizes and field
order match `ICDECOMPRESSEX`, `ICDRAWSUGGEST` and `ICSETSTATUSPROC`. The
object file is linked whole.

**The player `0x4718f8`** (cdecl; arguments: an element name and a pointer to
x/y/w/h, or 0; listing):

1. An open block at `ebp-0x1c`: callback 0, ID 0, type 0, element = argument
   1, alias 0, style `0x40000000` (`WS_CHILD`), parent `[0x4d3610]` (the main
   window, the handle `ShowWindow` gets at `0x4652ca`).
2. `handler_add(0x4718d4)`.
3. `MCI_OPEN` with flags `0x30200` (ELEMENT, `MCI_ANIM_OPEN_WS`,
   `MCI_ANIM_OPEN_PARENT`); on failure to step 8 with result 0.
4. The device ID to `[0x4a0cb8]`; `MCI_WINDOW` (`0x841`) with flag `0x40000`
   (`MCI_ANIM_WINDOW_STATE`) and `nCmdShow` 5.
5. `MCI_STATUS` (`0x814`) with ITEM `0x4001` (`MCI_ANIM_STATUS_HWND`); the
   video window to `[0x6b5480]`, placed by `0x471868`.
6. `[0x4a0cb4]` = 0; `MCI_PLAY` (`0x806`) with NOTIFY to the main window's
   low word.
7. `pump` `0x464c78` and `key_flush` `0x477abc` on the keyboard
   `[0x6c5eb8]`; loop until `[0x4a0cb4]` ≠ 0, each pass reading a key with
   `0x477ad4`: low byte 1 or `0x39` (Esc and Space as scan codes, hypothesis)
   sets `[0x4a0cb4]` = 1 and the result to 1; then `pump` again.
8. `0x4718b4([0x4a0cb8])` (only after a successful open), then
   `handler_remove(0x4718d4)`. Returns 1 if a key skipped the video, else 0.

**Next to the block.** The vtable `0x4a0ca0` (slots `0x4715ef`, `0x471619`,
`0x47161e`) belongs to the local `DummyDrawable` object that `0x46bd54`
installs (rasterizer.md, the Gouraud path; type name at `0x4715b9`);
`0x47161e` is its "return 0" method (`return_zero_47161e`, mission_load.h), not a
codec call. The first wrapper starts at `0x471628`, after three pad bytes at
`0x471625`. The function index's rows `47161e` (~220) and `4718b4` (~136)
each span several functions (a linear span to the next known entry).
`0x471a50` on is the mission load's pool code (`pool_create`'s helper).

### Callers: none

- `0x471628`, `0x471698`, `0x471708`, `0x471778`, `0x4717c4`, `0x4717f8`,
  `0x471848` and `0x4718f8` have no rel32 call or jump, and no dword anywhere
  in the file equals one of them (no vtable slot, handler table entry, push
  or mov immediate, relocated pointer). None is exported: DBSIM exports only
  `__GetExceptDLLinfo`, `@sfxWndProc$qqspvuiuil` and
  `@MainWndProc$qqspvuiuil`.
- No dword and no rel32 from outside `0x471625..0x471a4f` points into it.
- Referenced only from inside the block: `0x471868`, `0x4718b4`, `0x4718d4`;
  `0x4a0cb4` (`0x4718e9`, `0x4719d3`, `0x471a1d`, `0x471a28`); `0x4a0cb8` (4
  sites in `0x47197d..0x471a30`); `0x6b5480` (`0x4718a2`, `0x4719b0`); the two
  `"avivideo"` strings (`0x47180c`, `0x47184a`). Read only by this dead code,
  they are part of the initialised DATA runs anyway.
- Import thunks: `ICSendMessage` (`0x496a72`), 5 callers, the wrappers;
  `mciGetDeviceIDA` (`0x496a60`), 1 caller (`0x47184e`); `MoveWindow`
  (`0x496922`), 1 caller (`0x4718a7`); `mciSendCommandA` (`0x496a5a`), 17
  callers, 7 in the block and 10 in the CD glue `0x473c13..0x473e8b`. No thunk
  or IAT-slot address appears except as the thunks' own `jmp` operands.
- DBSIM is relocatable (16,850 HIGHLOW fixups), so any code pointer to the
  block would be a dword equal to its address; only a computed address could
  escape the scans, and none is known in DBSIM. The file holds no `.avi` or
  `avi\` string and no `mciSendStringA` or `AVIFile*` import; its only video
  strings are the two `"avivideo"`. The movies (86 names `avi\pt1.avi` …
  `avi\dropship.avi`) are `VSHELL.EXE`'s, played by its player `0x41e98c`
  through `mciSendStringA` (file_io.md); in the own chain `es2shell`'s player
  does this with the Indeo binding (media_compatibility.md). Mission script
  verbs run through relocated handler-table slots in DATA, none pointing into
  the block.

**The suites.** Under the proxy, the hidden window's shim logs
`window_hidden_<API>` the first time each of its seven APIs is called
(`window_shim.h`, `ws_count`): `video-compat.jsonl` (40,985 events) has 3,336
`window_hidden` and 3,336 `window_hidden_ShowWindow` events and **0**
`window_hidden_MoveWindow`; DBSIM's only `MoveWindow` site is `0x4718a7`,
reached only after a successful `MCI_OPEN`. The interactive and record runs
patch no slot and log nothing here. The DBSIM maps (40 suites, 3,010 starts)
have no row in `0x471625..0x471a4f` (consistent, not a count of zero). In
es2sim no constant of the range appears in `sim/src`, `core/src` or the
adapters, so an entry would end with the refusal `unmapped`; the 40 replays
are IDENTICAL and `//sim:frames_all` passes 40/40
(`fulltest-r12-3-20261022.log`, line 1337). es2sim's MCI host (audio.md) takes
only the CD glue's messages and answers `MCI_WINDOW` with
`MCIERR_UNSUPPORTED`; nothing sends it.

## What es2sim really reads from the mapped image

`es2sim --image-access <path>` (off by default; `replay_suite.py
--image-access` passes `<output-dir>/<suite>/image-access.json`, and so does
`bazel test //sim:replay_<suite>_test --test_arg=--image-access`) records which
bytes of the guest image es2sim reads at run time besides the code it
replaces. Without the switch `es2_sim_image_access_arm` does nothing. Code:
`sim/src/image_access.c`, `sim/include/es2/sim/image_access.h`; armed at the
end of `es2_sim_map` (`host.c`), after the image's own protections; each run
keeps the page's protection from arming time and restores it.

### Method

- **CODE.** Read faults on no-access CODE pages are recorded and served: the
  faulting VA, the operand size (own x86 operand decoder), the instruction as
  module + offset, the nearest own-code return address on the stack; the page
  turns read-only for one instruction (trap flag) and back on the single step.
  Execute faults are not touched; they reach the stage-1 trap
  (`ExceptionInformation[0] == 8`).
- **File-backed pages** (headers, the table pages, the initialised DATA
  `0x497000..0x4a7c00` (17 pages; the last shares BSS from `0x4a7c00`),
  `.idata`, `.edata`, `.reloc`, `.rsrc`) carry `PAGE_GUARD` on top of their
  protection. A guard fault counts a read, write or execute on the page and,
  for reads and writes, marks the operand's bytes, then re-arms after the
  instruction (single step).
- **Sampling.** A page is re-armed at most 32 times per epoch; an epoch ends
  at every host tick and after 50 ms of wall time (the load has no ticks).
  **Page granularity is complete; byte granularity is a sampled lower
  bound.** With 4 ms epochs acceptance-60s did not finish in seven minutes;
  with 50 ms it takes 40 s instead of 25 s.
- **Not counted:** the host's own reads (the tick record, the watches whose
  `readable()` checks see the original protections, the after-load snapshot),
  wrapped by `main.c` in `es2_sim_image_access_pause`/`resume`, and the
  set-up before arming.
- **The map** (`es2sim-image-access/1`, no image bytes): method, parameters
  and counts; the no-access CODE runs; the CODE reads; per area the watched,
  read, written and executed pages and the sampled bytes inside and beyond
  the file; per page the fault counts and the read/written VA ranges.
  `finish_run` writes it; a host refusal (`ExitProcess`) writes none.

### Result with the file's CODE bytes mapped (40 suites, headless)

All 40 IDENTICAL (2,630 s; acceptance-60s 18.5 s off, 39.9 s on). 57,271 CODE
read faults, 35.0 million guard faults, nothing undecoded, no overflow.
Evidence: es2-data `logs\dx-01-20261022` (`summary.json`, `summary.txt`, the
40 maps).

- **CODE reads outside the table pages: 15 records at 14 VAs**, all 4-byte
  reads by `memory.c` `id_read32` from own models of `0x496406`, which ends
  `mov eax,[eax-0xc]; mov eax,[eax+0x20]`: the dword +0x20 of a type
  descriptor in CODE (the count it returns). Callers by the nearest own
  symbol (a stack scan, so not necessarily the direct caller):

| VA | Caller | Reads | Suites |
|---|---|---:|---:|
| 0x408310 | rtti_count service | 1,314 | 25 |
| 0x40ab5d | rtti_count service | 382 | 13 |
| 0x40b61d | rtti_count service | 3,184 | 23 |
| 0x40c3f8 | rtti_count service | 8,275 | 16 |
| 0x40f7b6 | rtti_count service | 5,533 | 25 |
| 0x41bd16 | rtti_count service | 48 | 14 |
| 0x424004 | rtti_count service | 17 | 12 |
| 0x427000 (the dword at 0x426ffd) | rtti_count service | 5,481 | 27 |
| 0x45c573 | joystick_destroy | 38 | 38 |
| 0x46bb91 | rtti_count service | 61 | 9 |
| 0x47cf71 | r_destroy | 308 | 40 |
| 0x4823a6 | rtti_count service | 80 | 40 |
| 0x4836e0 | bitmap_array_destroy; rtti_count service | 17,756; 71 | 40 |
| 0x490cfc | rtti_count service | 14,723 | 30 |

  For a read across a page edge the record's VA is the faulting byte: the
  table page `0x426000` shows bytes `0x426ffd..0x426fff` read, so that dword
  starts at `0x426ffd`.
- **The table pages** `0x410000`, `0x426000`, `0x44b000`, `0x44e000` were read
  in 39 to 40 suites, at the CODE read sites of dbsim_gap.md (`0x4100fe..`, `0x426492..`,
  `0x44bb9f..`, `0x44e1f5..`). The pages of `0x405366`, `0x436be2` and
  `0x4967ae` were only executed; their static sites stand.
- **Initialised DATA.** All 17 pages read and written; every page read in
  every suite except `0x4a4000` (37) and `0x4a7000` (14); every page written
  in all 40. Sampled bytes (union): 9,067 of 68,608 read, 1,608 written, 8,447
  only read; the 1,024 BSS bytes `0x4a7c00..0x4a8000` all written.
- **Not read after arming:** headers, `.idata`, `.edata`, `.reloc`, `.rsrc`.

### With the image built from own runs

40 suites with `--image-access`, headless, all IDENTICAL: `code_reads` empty
(`code_read_faults` 0, `undecoded` 0). The non-site CODE pages form seven
no-access runs: `0x401000..0x405000`, `0x406000..0x410000`,
`0x411000..0x426000`, `0x427000..0x436000`, `0x437000..0x44b000`,
`0x44c000..0x44e000`, `0x44f000..0x496000`.

### The windowed start-up with `--image-access`

`es2sim_window`, `replay_suite.py --suite c1_01-success --window
--window-devices --image-access`: IDENTICAL (6198 ticks, 16305 watch changes,
2 return files `results.dat` and `mission.var`, exit code 3 compared; 198 s; a
640 × 480 window with 6222 presents; image from the own tables). Image access
(reason `winmain_returned`): `code_read_faults`, `code_read_sites`,
`code_read_overflow` and `undecoded` all 0; the no-access CODE ranges are the
seven gaps around the table pages `0x405000`, `0x410000`, `0x426000`,
`0x436000`, `0x44b000`, `0x44e000`, `0x496000`; read table pages `0x410000`
(`0x4100fe..`), `0x426000` (`0x426492..`), `0x44b000` (`0x44bb9f..`),
`0x44e000` (`0x44e1f6..`); `0x405000`, `0x436000`, `0x496000` not read. This
is the journal replay: input from the journal (the window's 23 events
dropped), virtual time unpaced, the run ends where the journal ends (WinMain
returned, 26 exit records); the CODE result does not depend on how the run
ends. Also seen: 204 notices posted and 2 lost (16 at most, in frame 5339),
the posted-queue overflow ([main_loop.md "The posted queue's depth"](../reverse-engineering/main_loop.md#the-posted-queues-depth)). Evidence: es2-data
`logs\dx-01b-20261023`.

**Open.** Kernel-mode reads: a system call that reads a guarded page would
fail instead of faulting; none showed up, but paths no suite reaches are not
covered. A run that ends in a host refusal writes no map. The caller column
is a stack scan.

## The initialised DATA and the CODE bytes as own runs

Everything es2sim reads from the guest image is own compiled-in runs at the
original VAs.

- `sim/data/dbsim_data.c` (833,369 bytes, 18,270 lines, one file below the
  4 MB split limit), generated by `tools/reference-game/scripts/dbsim_data.py`
  from the hash-checked reference `DBSIM.EXE` (SHA-256 `8561aa47...c4ad`) and
  its `llvm-objdump -d -z -M intel` listing; it imports
  `dbsim_image_tables.py` for the PE reader and the hash gate. Regenerate:
  `python -B tools/reference-game/scripts/dbsim_data.py --exe <DBSIM.EXE>
  --map <es2-data>/logs/dx-01-20261022/summary.json`; `--check`
  compares instead of writing.
- `sim/src/image_data.c`, `sim/include/es2/sim/image_data.h`
  (`//sim:image_data`): the run type (VA, size, kind, data);
  `es2_sim_data_check` (data present, kind known, size above 0 and a multiple
  of 4 for the dword kinds, a string ends in its NUL, inside the guest range,
  ascending without overlap); `es2_sim_data_write` (checks first, then writes
  in table order, dwords little-endian, the rest byte for byte; on a refusal
  nothing is written); `es2_sim_dbsim_data_write`; the RTTI lookup
  (`es2_sim_data_rtti_count`, `es2_sim_dbsim_rtti_count`).
- `sim/tests/image_data_map.h` (test input): the image-access map's read and
  written page ranges clipped to the initialised DATA, and its 14 CODE read
  VAs.

### Classification of the initialised DATA

`0x497000..0x4a7c00` (68,608 bytes):

| Class | Count | Bytes |
|---|---|---|
| Pointer slots (HIGHLOW `.reloc` sites) | 3,454 in 1,411 runs | 13,816 |
| of these, to code (functions 1,720, RTTI descriptors 687, labels 200) | 2,607 | |
| of these, to DATA | 702 | |
| of these, to BSS | 129 | |
| of these, unexplained, emitted as plain dwords (`ES2_SIM_DATA_DWORDS`) | 16 in 16 runs | |
| Strings (C literals; the NUL implicit) | 693 | 10,927 |
| Byte runs (zero gaps under 16 bytes stay inside a run) | 1,001 | 25,685 (16,761 non-zero) |
| Zero bytes not emitted | | 18,180 |

Non-printable bytes and `"`, `\`, `?` in string literals are octal escapes;
TAB, LF and CR are accepted inside strings (hence 693 strings against
dbsim_gap.md's static 653). 3,308 runs in all; 68 carry a doc name (the 75
VAs of the doc tables: math tables, keyboard map, init and exit records), and
a run is split at such a VA. Every relocation site in the initialised DATA is
a slot of a pointer or dword run and every slot is a site (3,454 = 3,454).

Pointer targets against the listing: an instruction start that is a call
target or begins `55 8b ec` is a function; any other instruction start a
label; a validated type descriptor RTTI.

**The 16 unexplained targets** lie one byte after a `00` or `90` padding byte,
where the linear listing decodes across that byte. By their bytes they are
code (hypothesis, not checked in vitro): this-adjusting thunks
(`83 44 24 04 imm8; e9 rel32`), functions starting with `enter`, and two
function pointers of the init records.

| Slot | Target |
|---|---|
| 0x49c788 | 0x4433c7 |
| 0x49ca0d | 0x444d36 |
| 0x49cf10 | 0x447aef |
| 0x49d11c | 0x448b3a |
| 0x49d1d0 | 0x448c2a |
| 0x49d63c | 0x44c1f5 |
| 0x49d92c | 0x450583 |
| 0x49dfa0 | 0x4557aa |
| 0x4a586c | 0x489f84 |
| 0x4a5874 | 0x48a6c4 |
| 0x4a587c | 0x48a340 |
| 0x4a5890 | 0x489830 |
| 0x4a58a0 | 0x48978c |
| 0x4a58c0 | 0x48a440 |
| 0x4a7a40 | 0x413ed4 (init record) |
| 0x4a7a46 | 0x414c68 (init record) |

The slots keep the original values; nothing relocates (the image stays at
`0x400000`).

### The CODE runs (203)

- **Six index-byte tables**, each verified in the listing by the pattern
  `cmp r32, size-1; ja; mov r8, [r32 + table]; jmp [4*r + table + size]`,
  emitted whether or not a suite read them:

  | Table | Size (bytes) |
  |---|---|
  | 0x405366 | 62 |
  | 0x4100fd | 33 |
  | 0x426492 | 11 |
  | 0x436be2 | 57 |
  | 0x44bb9f | 15 |
  | 0x44e1f4 | 29 |
- **The dword `0x4967ae`** (4 bytes), named by four relocated
  `cmp dword ptr [0x4967ae], 0` operands.
- **196 RTTI counts.** `0x496406` reads `[[v - [v-4] - 0xc] + 0x20]`, the dword
  +0x20 of the Borland type descriptor whose pointer sits at `[vtable-0xc]`. A
  candidate is a relocated CODE pointer in the initialised DATA at `v-0xc`
  with `[v-8] = [v-4] = 0`; it counts only if the target validates as a class
  descriptor (mask word at +4 is 3; the name at +6 printable and
  NUL-terminated; the vptr offset at +8 not `0xffffffff`). 223 candidates, 27
  rejected (triples of function pointers), 196 distinct descriptors; each run
  is the dword at descriptor + 0x20 with the class name as a comment. All 14
  measured VAs are among them (the run for the measured `0x427000` is at
  `0x426ffd`). CODE holds 251 class descriptors; the other 55 have vptr offset
  `0xffffffff` (no vtable), so `[obj-0xc]` cannot reach them.

**The spans table**: the whole initialised DATA, then each CODE run: 204
spans, 69,603 bytes. Written into zeroed memory, the runs reproduce the
initialised DATA exactly, zeros included.

### Tests

- `//tools/reference-game/scripts:dbsim_data_unit_test` (9 cases): a
  synthetic PE32 and listing (slot classes and an unexplained target; strings
  with escapes, byte runs and zero gaps, the named split; the table pattern,
  the code dword and the descriptor count); refusals (a bad table pattern,
  overlapping slots, relocation type 1, a code dword no operand names); the
  committed files are current; the real case disassembles the CD's
  `DBSIM.EXE`, regenerates, compares and expects 196 descriptors.
- `//sim:image_data_test`: write order, little-endian dwords, bytes outside
  the runs untouched; each refusal with nothing written, also a late one; the
  shape of the DBSIM table.
- `//sim:image_data_real_test` (SKIP without `ES2_DATA_ROOT`): every run
  equals the placed file; the runs written into a zeroed image equal the file
  over every span and leave everything else zero (BSS stays zero); relocation
  sites = pointer and dword slots (3,454); the seven CODE runs and the 14
  measured RTTI VAs are present; all 799 map ranges (10,522 bytes) and the 14
  CODE reads lie inside a span. PASS line: 3308 runs (bytes 1001, strings 693,
  pointers 1395, dwords 16, code tables 7, rtti counts 196), 204 spans with
  69603 bytes. Mutation caught: one byte of the run `0x4a6940` changed ("run
  0x004a6940 (112 bytes) differs from the file").

## es2sim without DBSIM.EXE

es2sim reads no `DBSIM.EXE`. The guest image is built from the own tables
([Headers, imports and the icon as own
tables](#headers-imports-and-the-icon-as-own-tables)) and the own runs ([The
initialised DATA and the CODE bytes as own
runs](#the-initialised-data-and-the-code-bytes-as-own-runs)).

**`es2_sim_map(sim, reserved)`** (`sim/src/host.c`), no path:

1. reads the layout from `es2_sim_image_info_own` and checks the base
   (`0x400000`), the size (`0x2d8000`) and the page alignment of CODE;
2. commits the reservation read-write (zero);
3. writes the runs with `es2_sim_dbsim_data_write(guest, 0x400000, 0x2d8000,
   &error)`;
4. reads the 134 imports from `es2_sim_imports_own` and fills each IAT slot
   with its refusal trap (a separate page range of `0xcc` bytes, read-only,
   never executable; each slot bounds-checked);
5. sets the original's protections: headers read-only; sections with the
   write flag read-write, the other data sections read-only; CODE page by page
   with `es2_sim_code_page_protection`: read-only on a page that holds a
   CODE_TABLE run, no-access on every other;
6. arms the image-access switch when asked.

The trap dispatch is unchanged: CODE is never executable. The seven
CODE_TABLE runs (the six index tables and `0x4967ae`) lie on seven distinct
pages, exactly the seven table sites (`host_real_test`); the other 143 CODE
pages are no-access. Bytes no run covers are zero: the PE headers, `.idata`
apart from the 134 IAT slots, `.edata`, `.reloc`, `.rsrc`, and every CODE byte
outside the seven runs.

**A stray read.** For a read or write access violation inside CODE, the
unhandled-exception filter of `main.c` prints, before the usual FAULT line
(exit 71): `FAULT: read|write of CODE 0x… at …: no own data there`. No test
triggers this line; `host_real_test` only shows that a read of `0x47cf71`
through `ReadProcessMemory` fails.

**The RTTI counts (option B).** The own models of `0x496406` (`rtti_count` in
`load.c`, `joystick_destroy` in `input.c`, `r_destroy` in `volumes.c`) read
the count through `es2_sim_data_rtti_count(runs, count, va, &value)` /
`es2_sim_dbsim_rtti_count(va, &value)` (binary search; a value only for an
RTTI_COUNT run of size 4 starting exactly at `va`). A descriptor outside the
runs falls back to the guest read (only with the tests' synthetic images).
Option A, readable pages holding only the counts, was rejected: the 196
dwords are spread over many CODE pages, and a stray read there would silently
return zero instead of faulting.

**The file path is testonly.** The hash gate, the PE parse, the placement,
the file's imports and resources and the file-based icon loader are in
`sim/src/image_file.c`, `sim/include/es2/sim/image_file.h`, the testonly
`//sim:image_file`. `//sim:image` holds only the own part and does not depend
on `sha256`. The tests that compare the own tables with the user's file use
it (SKIP without `ES2_DATA_ROOT`): `image_test`, `table_test`, `init_test`,
`platform_sdl_test`, `image_tables_real_test`, `image_data_real_test`. The
run-time binaries carry neither the `VER95` path nor the hash.

**Command line.** es2sim takes no path of a DBSIM.EXE or a data root: `--exe`
and `--data-root` are unknown options like any other (usage error 64). es2sim
does not read `ES2_DATA_ROOT`; the VOLs and game files come from the working
directory. The callers name neither:
`replay_suite.py` (finds the CD by its `VOL` and `DATA` directories),
`perf_suite.py`, `replay_stub_sim.py`, `replay_suite_test.py` (asserts that
`--exe` is absent), `launcher/tests/test_launch_chain.c` (cases J, K, L, M),
`configure-sim.ps1` and its tests, `test_start_own_menu.py`. Because the
command ends with a quote and GetPrivateProfileString drops a quote pair
around a whole value, `configure-sim.ps1` writes `command = "<command line>"`
(case K reads such a value with the real es2launch); it has no DBSIM.EXE hash
check and no manifest `dbsim_sha256`.

**Tests.**

- `//sim:host_real_test` (no data): every CODE page's protection (7
  read-only, the rest no-access, equal to `es2_sim_code_page_protection`);
  `0x400000` read-only and `0x492ee0` no-access; the run bytes at `0x4967ae`
  and `0x4a79d8`; the RTTI lookup (a hit at `0x4836e0`, a miss at
  `0x4836e4`). `//sim:host_watch_real_test` (no data).
- `es2sim_real_test` and `es2sim_window_test` start in a synthetic
  installation without a DBSIM.EXE and expect the line "built the guest image
  from the own tables".
- `//sim:no_dbsim_real_test` (`sim/tests/test_no_dbsim_real.py`): neither
  `es2sim.exe` nor `es2sim_window.exe` contains "VER95" or the gate's
  SHA-256; a temporary hard-link mirror of `ES2_DATA_ROOT` without
  `VER95\DBSIM.EXE` (864 links, deleted afterwards); `replay_suite.py` runs
  c1_01-quit-dialog and c4_10-probe from it against the real goldens, both
  IDENTICAL (10 checks). Mutation caught: a re-introduced `fopen` of
  `<ES2_DATA_ROOT>\VER95\DBSIM.EXE` with the path built without the literal
  "VER95" makes both replays SETUP-FAILED (exit 65), 3 of 10 checks fail.
- Acceptance: the 40 replays IDENTICAL; the 40 suites with `--image-access`
  without CODE reads ([What es2sim really reads from the mapped
  image](#what-es2sim-really-reads-from-the-mapped-image)); the after-load
  snapshot's one host byte pair ([The after-load snapshot against the
  original](#the-after-load-snapshot-against-the-original)); `//sim:frames_all`
  40/40.

## The chain without the executables

- `start-own-menu.ps1 -CdRoot <dir>` sets the `cd_root` that the own
  executables read (menu.ini); default `<ExtractedRoot>\cd`, `-ExtractedRoot`
  defaulting to `build\extracted`. The verification against the
  original keeps `-DataRoot` and `ES2_DATA_ROOT`. `test_start_own_menu.py`
  checks the default and a copy.
- `check.ps1 -OwnCdRoot <dir>` passes it to the own-shell and own-sim chains.

`check.ps1 -Live -SkipQuick -OwnShell -OwnSim -OwnCdRoot <copy>` on a scratch
copy of the extracted CD (861 files) without the `VER95` and `VER31` copies of
`DBSIM.EXE` and `VSHELL.EXE` passed: signatures 789/789; audit 0 problems;
live 40/40 identical (own runtime); own-shell text and gui PASSED; own-sim 40
replays IDENTICAL, frames 40/40, chain PASSED (es2sim identical to the
c1_01-success golden); evidence PASSED. The three chains' `menu.ini` files name
the copy. Evidence: es2-data `logs\check-r13-1`. The window icons of es2shell
and es2sim_window (both from compiled-in stores) are left to the owner's play
test.

## A time per DBSIM capture record

`[presentation] capture_qpc=1` gives the DBSIM presentation capture
(`proxy/presenter.h`, `capture.ndjson`, presentation.md "Capture record") a time stamp
per record, the counterpart of VSHELL's `qpc=1` (vshell_ui.md 42.5,
`scripts/vshell_present_times.py`): the wall time of the original's frames for
a pacing comparison with `es2sim_window`.

### The key and the schema

- Read with the other capture keys in `pr_open_capture`; any nonzero value;
  needs `capture=1`.
- **Off** (absent or `0`): every byte is the schema-1 report (start
  `"schema":1`).
- **On:** the start record has `"schema":2` and ends with
  `"qpc_frequency":<n>` (QueryPerformanceCounter counts per second), after
  `dump_range` and `regions` if present. Every record ends with `"qpc":<n>`,
  after the region fields if `capture_regions=1`. Removing `,"qpc":<n>` and the
  rate gives the schema-1 line byte for byte.
- The stats lines' `qpc_frequency`, `capture_qpc` and `present_qpc` are the
  capture's own cumulative cost counters, a different thing.
- Schema 2 is a new value of an existing field; a reader that insists on
  `schema == 1` refuses it, the readers here do not.

### The stamp

- The counter on **entry** to the hooked present (the `StretchBlt` import
  slot, return address `0x4659b2`), the value the hook reads anyway for its
  cost figures; no extra clock read. So the stamp precedes the own
  presenter's draw (`presenter=gdi`) and the record's formatting and write.
- The DirectDraw branch (`ddraw_windowed.h`, `[presentation]
  directdraw=windowed`) stamps on entry too: `ddw_present`, called by `Unlock`
  of a non-flipping primary (return `0x465876`) and by `Flip` (return
  `0x465895`), reads the counter first (before the palette conversion, the
  `GetDC`/`StretchDIBits` draw and the hash), and only when the record will be
  written (capture on, a present site, the game thread) and `capture_qpc=1`;
  without the key it reads no clock. The clears (return `0x46552a`,
  `0x46554a`) are drawn but neither recorded nor stamped.
- `pr_write_record` stamps a record that reaches it without an entry value at
  the write; no present path does so.
- Not included: the time before the present call in the tick (the sim step)
  and the time the original spends in `StretchBlt`. A gap between two stamps
  is "entry to entry".

### The readers

- `dbsim_frames.py` `read_capture`: schema 1 or 2; for 2 it checks
  `qpc_frequency` (integer ≥ 1) and every record's `qpc` (integer ≥ 0), else
  raises (`build` reports `unusable-capture`); other schemas refused. The frame
  table is identical for both versions (the stamp is no column), so `compare`
  works across schemas and `verify` checks the same bytes. The manifest's
  `capture.start` gets `schema` and `qpc_frequency` for schema 2 only.
- `presenter_capture.py` `load`: schema 1 or 2; `compare` and `summary` ignore
  the stamp.
- `scripts/dbsim_present_times.py <capture.ndjson | manifest directory>
  [--frames FIRST LAST] [--budget-ms 40] [--top 5] [--json]` (via
  `dbsim_frames.read_capture`: a torn last line skipped and counted, an
  unclosed capture named). Schema 1: says it has no times, names the key and
  the switch, exit 0. Schema 2: count, min, median, mean, p95 (nearest rank)
  and max in ms of the **present gap** (all records and the main-loop presents,
  site `0x45fbe4` ordinal 0) and of the **tick**, a step of the frame counter
  `[0x4d257b]` (first record of frame N to N + 1, and main-loop present to
  main-loop present; only steps of exactly one frame, others are
  "irregular_steps"); ticks over `--budget-ms` (default 40, the limiter at
  `0x4678c0`), ticks per second, presents per frame, the slowest ticks. Exit 2
  for an unusable capture (no start line, schema 3, schema 2 without the rate
  or a stamp, stamps going backwards).

### The runner

`capture-dbsim-frames.ps1 -Qpc` adds `capture_qpc=1` to the run's capture INI
(after `capture_regions=1` if both). It needs a `-Label`. The raw capture is
copied to `<evidence>\<suite>\capture.ndjson` (frame numbers, hashes and
counters only); the manifest's `run.qpc` records `capture_qpc: true`, the
copy's name and the reader; the session record has `qpc: true`. The table and
hashes equal the baseline's; the run must end `identical`. `-DryRun` shows the
INI and `qpc`.

### Tests

| Target | Checks |
| --- | --- |
| `//tools/reference-game/proxy:presenter_qpc_test` | schema-2 start line with the rate; records with the stamp; stamp before a 12 ms fake draw's entry and 10 ms or more before the caller's second read |
| `presenter_qpc_all_test` | with `capture_regions=1` and a range, the stamp after the region fields and the rate after the table id; the write-time fallback |
| `presenter_qpc_off_test` | `capture_qpc=0` is schema 1 without a stamp |
| `presenter_qpc_ddraw_test` | DirectDraw branch: `Unlock` and `Flip` stamped before the 12 ms draw; clears not recorded; the `Flip` record without the stamp equals `presenter_ddraw_test`'s line |
| `presenter_ddraw_test`, `presenter_ddraw_off_test`, `presenter_capture_test`, `presenter_capture_gdi_test`, `presenter_regions_test`, `presenter_range_test`, `presenter_own_site_test` | unchanged schema-1 output |
| `//tools/reference-game/scripts:dbsim_frames_unit_test` | both schemas, same table, manifest, compare across schemas, bad schema 2, a stray field in schema 1 |
| `dbsim_present_times_unit_test` (17) | schema 1 without times, gaps, ticks, main-loop ticks, irregular steps, range, options, frequency, refusals, files, torn line, directory, JSON, exit codes |
| `presenter_capture_unit_test`; `//tools/reference-game/setup:capture_dbsim_frames_test` | the readers; `-Qpc` plan and INI, label required, the raw capture kept byte for byte, manifest and session entries, same table as the baseline, nothing kept without the switch; `-WindowedDirectDraw` plan, INI line, `-Z1` handed to the child and recorded, label required, with `-Qpc` and `-Regions`, nothing without the switch |

Mutations caught: `pr_write_record` stamping at the write (`presenter_qpc_test`
record 0); `ddw_present` passing no entry value (`presenter_qpc_ddraw_test`);
`tick_steps` counting steps of more than one frame (4 reader tests).

### Live

Clone `timing-r111-p1`, `capture-dbsim-frames.ps1 -Suite <suite>
-InstallName timing-r111-p1 -Label qpc -Qpc`, one suite after the other, then
`dbsim_present_times.py` per capture. Evidence: es2-data
`logs\dbsim-frames-20261023-qpc` (manifests, captures, `present-times.txt` and
`.json` per suite). Both suites `ok`, `result identical`:

| Suite | Records | Last frame | Main-loop tick median / mean / p95 | Over 40 ms |
|---|---:|---:|---|---|
| acceptance-60s | 1705 | 1702 | 3.55 / 3.60 / 3.83 ms | 0 of 1702 |
| c3_10-detail-options | 1850 | 952 | 3.97 / 7.52 / 4.36 ms | 2 of 952: frames 204 (1706.7 ms) and 600 (1671.9 ms) |

`capture.start` has schema 2 and `qpc_frequency` 10,000,000;
acceptance-60s's `frames.tsv.gz` equals the plain capture's over the 1706
common lines; the reader reports "not closed (the process was stopped)" (the
runner ends the game after the replay). The clone runs **unpaced** in the
replay, at 270–280 ticks per second, not near the 40 ms limiter (why the
limiter at `0x4678c0` does not hold the tick there is not examined). In c3_10
the two modal dialog frames carry nearly all the wall time, with up to 461
presents in one frame. The pacing comparison: [main_loop.md "The notices against the paced original"](../reverse-engineering/main_loop.md#the-notices-against-the-paced-original).

**Live, DirectDraw branch** (`capture-dbsim-frames.ps1 -Suite acceptance-60s
-InstallName timing-r111 -Label ddqpc -Qpc -WindowedDirectDraw`, es2-data
`logs\dbsim-frames-20261024-ddqpc`; [below](#the-directdraw-branch-in-a-capture-run)):
status `ok`, `run.result` `identical`, `run.directdraw` `windowed`,
`run.game_arguments` `["-Z1"]`, session `windowed_directdraw: true`; the
capture's start record has schema 2 and `qpc_frequency` 10,000,000. All 1,692
records have `"source":"surface"`, mode 1 and `"presented":"windowed"`, at the
same call sites as the GDI branch (`0x45fbe4` 1,690, `0x4614a8`, `0x461e21`),
with a non-decreasing stamp each. Against the GDI-branch baseline
(`dbsim-frames-20261014\acceptance-60s`, 1,649 records) `dbsim_frames.py
compare` stops at record 0 on mode and source alone; site, ordinal, index and
palette hash agree in all 1,649 common records (the DirectDraw run has 43
records more, to frame 1,689). `dbsim_frames.py verify`: consistent.
`dbsim_present_times.py --json`: 1,690 frames in 6,673 ms, median present gap
3.73 ms (p95 4.71 ms), main-loop gaps at most 9.1 ms, the first frame 186 ms.
The time the original spends inside the blit shows only as part of the gap to
the next stamp.

### The DirectDraw branch in a capture run

`capture-dbsim-frames.ps1 -WindowedDirectDraw` makes DBSIM take the DirectDraw
branch and the proxy answer it with its own objects
(presentation.md "DirectDraw branch in a window"):

- the capture INI gets `directdraw=windowed` (last line of the
  `[presentation]` section, after `capture_regions=1` and `capture_qpc=1`);
  `presenter` stays unset, as in the fixture's `presenter_qpc_ddraw_test`;
- the runner is handed the game argument `-Z1` (`run-oracle-parallel.ps1
  -GameArguments`), which sets the DirectDraw flag `[0x4d255e]` at `0x45e706`,
  so DBSIM takes the branch from its start. The clone's `prefs.cfg` and
  `es2-oracle.ini` are not touched; there is nothing to restore;
- a `-Label` is required (the baseline directory holds the GDI-branch run).
  The records have `"source":"surface"`; with `-Qpc` the stamp is the counter
  on entry to `ddw_present`. `-DryRun` shows the INI, `windowed_directdraw`
  and `game_arguments`;
- the manifest's `run` has `directdraw: "windowed"` and, from the runner's
  entry, `game_arguments: ["-Z1"]`; the session record has
  `windowed_directdraw: true`;
- the run is compared with the golden of the GDI branch as usual. Whether the
  DirectDraw branch ends `identical` is part of the live result; the manifest
  records `run.result` either way;
- the DirectDraw observer must be off (no `-Perf`, `-DeferFlip`,
  `-PresentationEvery`; `capture-dbsim-frames.ps1` passes none), else the
  return address of `Unlock`/`Flip` lies in the observer and no present is
  recorded.

## The original's fault paths

Where the original faults, es2sim ends the process with the original's
exception code. Code: `sim/src/cpu_fault.S`, `sim/src/cpu_fault.c`,
`sim/include/es2/sim/cpu_fault.h` (the divide primitive); `sim/src/loop.c`
(`es2_sim_loop_fault`, `es2_sim_loop_divide_fault`,
`es2_sim_loop_divide_refuse`); `sim/include/es2/sim/process_platform.h` (the
record's fifth field `process_fault(user, code, site)`); `sim/src/main.c`
(`process_exit`, `process_fault`, the headless record); every own row with an
original IDIV or DIV that can fault; `sim/tests/replay_suite.py` (the
guest-fault verdict).

### How the original ends on an unhandled fault (static; step 4 live)

1. The fault is raised inside a CODE function.
2. The SEH chain on FS:0 holds DBSIM's frame records, whose handler is
   `0x4967f0`, CW3220's `__ExceptionHandler`, and at the bottom CW3220's
   `__startup` record (rtl_imports.md "Exception handlers (static)"). In the
   search phase `__ExceptionHandler` switches on the kind of the frame's
   current table entry (table `0x4043e0`): kind 1 calls an `__except` filter
   (`0x4044ae`), kind 2 takes a constant (`0x404507`), kind 3 is a catch and
   acts only for `0x0EEFFACE`. The entries DBSIM's 267 frame set-ups reach
   through their context stores are of kind 5 (objects), kind 0 and one
   kind 3 (table `0x4a7948`, a catch-all); none is of kind 1 or 2 (static,
   a scan of the set-ups' tables in the listing). So every DBSIM record
   answers ExceptionContinueSearch (1) to a divide fault or an access
   violation, and the search passes on. The `__startup` record calls the two
   user handlers, which return 1, then `UnhandledExceptionFilter`.
3. The `__startup` record returns the filter's answer as its own disposition
   (listing of the verified `CW3220.DLL`, `0x4051e6..0x4051fb`:
   EXCEPTION_POINTERS built at `[ebp-8]`; `call 0x41ed9b` is `jmp [0x424308]`,
   the IAT slot of `UnhandledExceptionFilter` (KERNEL32 IAT at RVA `0x241c0`,
   entry 83); EAX returned unchanged). The filter's usual answer,
   EXCEPTION_EXECUTE_HANDLER (1), reads as ExceptionContinueSearch, so the
   search goes on to the OS's thread-start record.
4. The OS ends the process with the exception code as its exit code; no
   atexit functions, exit records or stream flush run.

Step 4 is Windows' usual end of a thread start's unhandled exception. Live,
the original reaches it with the divide faults and the NULL access of the
data inputs below ("Inputs that reach a site"), with the injected `new`
failure `46162b:1` (exit code `0xC0000005`; rtl_imports.md "The exit path
under the `new` failure, and `check.ps1 -NewFail`") and with the tape
playback fault at `0x453127`, which ended with no exit record and no
outputs (oracle_suites.md "Tape playback suites").
Measured with the own `cw3220.dll` (`//rtl:cw3220_fault_test`, child
processes; rtl_imports.md "The exception paths of the own runtime"): with the
error box off and a top-level filter answering EXCEPTION_EXECUTE_HANDLER, the
thread start's record asks the filter a second time; its global unwind then
calls `__startup`'s record with STATUS_UNWIND and EXCEPTION_UNWINDING (the
record asks the filter a third time; `405160` tests no flag); the exit code is
the exception code; no atexit function runs. So the unwind passes the records
above `__startup`'s, DBSIM's frame records included, and would call
`__ExceptionHandler` in its unwind phase, running each frame's clean-up
`0x40415d`. Which dialog or report the OS shows depends on the Windows
version and its error-reporting settings; it is not part of DBSIM.

What the original does live (original `CW3220.DLL`, all hooks original, error
box on, Windows' default error-reporting settings): with the `new` failure
`46162b:1` (rtl_imports.md "The exit path under the `new` failure")
`UnhandledExceptionFilter` is called from CW3220 +`0x51f5` with `0xC0000005`
and returns 1; the chain at that call is ntdll's, DBSIM's `0x4967f0` twice,
CW3220's `__startup` record (+`0x5160`) and ntdll's thread start; no
`new_fail_summary` record follows, so no DLL_PROCESS_DETACH runs: the process
is terminated, not ended through ExitProcess. With the divide fault of
`zone-zero` the trace `proxy/fault_trace.h` (`ES2_FAULT_TRACE`, which records
each call of `__ExceptionHandler` through DBSIM's import slot RVA `0x2cc33c`)
shows the search phase: three calls with flags 0 at `0x46c208` (frames of the
tables `0x49a8b5`, `0x49f7c9`, `0x49f273`), each answering 1 with the context
word unchanged; then `UnhandledExceptionFilter` from CW3220 +`0x51f5`,
answer 1; then nothing: no call with EXCEPTION_UNWINDING, no detach record,
exit code `0xC0000094`. The process ends before an unwind reaches DBSIM's
records, so `0x40415d` does not run (es2-data `logs\fault-trace-live`,
`report.md`; rtl_imports.md "The unwind clean-up at the process end").

### What es2sim does

- **The primitive.** `es2_sim_divide_fault(dividend, divisor, is_signed)` runs
  the real IDIV or DIV of EDX:EAX = dividend on the host CPU; a vectored
  handler of its own, first in the list for that call only, steps over the
  instruction and keeps the code, so the code is the OS's own choice.
  `//sim:fault_paths_test` on the build host: a zero divisor gives
  `0xc0000094` (STATUS_INTEGER_DIVIDE_BY_ZERO), signed and unsigned; a
  quotient past 32 bits gives `0xc0000095` (STATUS_INTEGER_OVERFLOW) for
  `INT_MIN / -1`, `2^40 / 2` and a DIV whose high dword is at or above the
  divisor; a division that does not fault gives 0.
- **The process end.** `es2_sim_loop_divide_fault(l, site, ...)` asks the
  primitive and on a fault calls `es2_sim_loop_fault(l, code, site)`, which
  calls the record's `process_fault`; `es2_sim_loop_divide_refuse` likewise.
  It refuses when there is no record, when the record has no
  `process_fault`, or when the operands do not fault (a model error, named by
  the refusal).
- **The record.** es2sim always sets one: `window_process` windowed,
  `headless_process` headless (`//sim:es2sim`, `es2sim_window --headless`;
  no `message_box`, so MessageBoxA keeps the headless answer). `process_fault`
  prints `FAULT: guest exception 0x<code> at 0x<site> (the original's
  unhandled fault); ExitProcess(0x<code>) without exit records` to stderr,
  prints the window statistics when windowed, closes the host's log with
  reason `guest_fault` and the code, and calls `ExitProcess(code)`.
- **Tests.** A recorder in the record returns; the loop then ends with
  outcome `ES2_SIM_LOOP_EXITED` and the code as `exit_code`; the row's caller
  returns 1 without a refusal of its own, and the call helpers keep EXITED. A
  NULL record (the library default and most unit tests) keeps the refusals.

### Exit codes

| Code | Meaning |
|---|---|
| 0 .. 6 | the guest's, the dword `0x4d27b8` (WinMain returned) |
| 1 | the fatal report's `__exit(1)` |
| 3 | `_abort` after its box; also the guest's "mission over" |
| 64, 65, 66, 67 | es2sim: usage, image refused, range not claimable, set-up failed |
| 70 | es2sim: a refused call (a guest call without own code, or a model error) |
| 71 | es2sim: an exception in es2sim's own code that nothing handled, also under the guest's frame records (below) |
| `0xc0000094`, `0xc0000095` | the original's unhandled divide fault |
| `0xc0000005` | the original's access violation: a read or write of the NULL page inside a guest entry, or an own row's NULL access (below) |

The launcher passes es2sim's code to `es2l_next` as if it were DBSIM's;
`es2l_next` switches on the unsigned value, so a code at or above `0xc0000000`
is no state 1..6 and the chain ends, as ES2.EXE's does after a crashed DBSIM
(launcher.md "Exit codes").

### Host faults under guest frame records

Guest code links DBSIM's frame records on the real FS:0 (`0x49630c`); their
handler `0x4967f0` lies in the no-access CODE range. A hardware fault of
es2sim's own code under such a record (an own write through the identity view
to an address outside guest memory, say) never reached the unhandled-exception
filter: the OS's walk finds the record, refuses its handler
(`STATUS_INVALID_EXCEPTION_HANDLER`, `0xc00001a5`) and ends the process
without a FAULT line or exit record. `sim/src/host_fault.c`
(`es2/sim/host_fault.h`) adds a vectored handler at the end of the list, which
runs before that walk. It passes on what the first handlers take (the dispatch
trap's execute faults on CODE, the divide primitive, the image-access guard),
every exception raised outside es2sim's module and every code without error
severity; for the rest `main.c` prints the FAULT line of the unhandled filter
and ends with 71. The identity view itself stays unchecked: a range check
would need every valid guest range (image, heap, host stack, scratch) on each
access; out-of-range writes of the model end here instead of silently.

**The NULL page.** A read or write access violation below `0x10000`
(`es2_sim_host_fault_null_page`, `ES2_SIM_NULL_PAGE_END`) while a guest entry
runs (`es2_sim_trap_entry_va(0)` nonzero) is the original's own fault: the
identity view hands own code the guest's address, and Windows maps nothing
there in DBSIM's process either. `main.c` prints `FAULT: read|write of
0x<address> (the NULL page) in the entry 0x<entry>, host <address>` and ends
through `process_fault` with `0xc0000005` and the entry as the site (reason
`guest_fault`, as the divide faults). Execute faults, addresses at or above
`0x10000` and faults outside a guest entry keep 71: there the guest's
address is not known to be what the original would touch.

`//sim:host_fault_test`: child processes link a record with a no-access
handler on FS:0 and write to `0x1b`; with the handler the child ends with 71
through the callback, without it with an exception code at or above
`0xc0000000` and the filter never runs. A child whose callback ends NULL-page
faults with their code ends with `0xc0000005` for the write to `0x1b` and with
71 for a write to a no-access page above the NULL page. Plus the ownership
rules (codes, addresses inside and outside the module, before install and
after remove) and the NULL-page test on records (read and write at the page's
ends, `0x10000`, an execute fault, one parameter, another code).

### NULL accesses

An own row that finds a guest pointer NULL where the original reads, writes
or calls through it below `0x10000` ends like the original:
`es2_sim_loop_null_refuse(l, va, name, reason)` (`loop.c`) calls
`es2_sim_loop_fault(l, 0xc0000005, va)` and, without a record that has
`process_fault`, refuses as before (exit 70). The site is the row's refusal
VA: the faulting instruction or the call that reaches it. The start-up's
streams use `su_null_refuse` (`startup.c`), the service `0x49630c` calls
`es2_sim_loop_fault` over the bound loop. Each file keeps a `null_refuse`
wrapper beside its `refuse` (HUD: `hud_null_refuse`).

Classified from the `llvm-objdump -d -M intel` listing of `VER95\DBSIM.EXE`
at each row's VA: 106 rows refuse a NULL or out-of-range guest pointer; 93 fault in the original, one is a divide
fault, 12 stay refusals. An address out of range but mapped is no fault.

Most rows are an allocation that returned NULL (`operator new` `0x47747c`,
`new[]`, `_calloc`) followed by a write or read through it. The heap is
CW3220's on VirtualAlloc reservations (section 3), so such a row is reached
only on exhausted address space, in the original as in es2sim.

| File | Rows (refusal VA) | Pointer, and the original's access |
|---|---|---|
| `audio.c` | `0x4643a9`, `0x462b71`, `0x4631ed` | `new[]` NULL; no channel below the start time `0xffffffff` and a row without a slot: a write or read through NULL |
| `cockpit.c` | `0x45d56d`, `0x45d893`, `0x45d898` | `new` NULL; the intro record and its bitmap list NULL (the palette stores NULL, later `[[0x4d2744]+8]`) |
| `dialogs.c` | `0x47749c`, `0x456c7e`, `0x45d160`, a label row | `new` NULL, written or read through |
| `display.c` | `0x42a5e7`, `0x42aa49` | the view's graphics context NULL: `0x42a5e7`, `0x42aa64` read `[0x208]` |
| `fill.c`, `gouraud.c`, `services_rtl.c` | the frame record `0x49630c` | `[0x6cbee4]` 0: `0x496336` `mov eax, [eax]` reads `[0]` |
| `frame_buffer.c` | `0x489390`, `0x4810ca`, `0x481129`, `0x4894d1`, `0x464e5c`, `0x464f4e` | a pixel format without a driver table (the `-1` sentinel, then `call [eax+4*ebx]` at `0x48942f` reads `[0x1f]`); `new` NULL; no clip record; a NULL surface reaches `0x481028` `cmp dword ptr [ebx], 0` |
| `gfx.c` | `0x482634`, `0x480c7e`, `0x480c91` | no GC object (`0x48265f` reads `[eax+0x231]`); no current surface; no clip record |
| `hud_cockpit.c` | `0x42de7c`, `0x42df17`, `0x431a2f`, `0x430ef4`, `0x43445e`, `0x4387c3`, `0x438896`, `0x441f2e`, `0x444ebe`, `0x440575`, `0x43fb80`, `0x445699`, `0x445a85`, `0x447d24`, `0x43ca6b`, `0x43cb12`, `0x43cc13`, `0x43d095`, `0x44c3d6`, `0x4492eb` | `new` NULL, then a write, read or call through it (the panel layout: the stream read, then `mov ecx, [eax]` at `0x431a3b`) |
| `hud_draw.c`, `hud_frame.c` | `0x4389a5`, `0x44f3bb`, `0x43d83b` | no GC (reads `[0x22c]`); no icon bitmap; a target without a shape record (reads address 8) |
| `hud_gauges.c` | `0x4387c3`, `0x442b0b`, `0x437e82`, `0x4416ff`, `0x441b23` | `new` NULL, written through |
| `hud_load.c` | `0x480ec4`, `0x480c91`, `0x44fb32`, `0x44f718`, `0x44f80c`, `0x48284e`, `0x4833cf` | no surface or clip record; a NULL surface (`0x481028`); no terrain grid; a cell outside the grid (the cell lookup returns 0, the byte read `[0]` faults); an allocation NULL |
| `hud_widgets.c`, `widget_leaves.c` | `0x4342a5`, `0x435d43`, `0x434538`, `0x4520e5` | no roster record; `strcpy` from a NULL speaker name; no panel GC; no display record |
| `input.c` | `0x45bee0`, `0x45be50`, `0x477787`, `0x45bc3e`, `0x45bcb4`, `0x45bbeb` | a device id without an object (the rows' check `addr < 0x10000`) |
| `load.c` | `0x4612e4`, `0x480ec4`, `0x480c93`, `0x48212d`, `0x461495`, `0x462514`, `0x4275d4`, an allocation row, `0x46a023`, `0x4613d2`, `0x46bdbc`, `0x46bfdc`, `0x46c3f0` | allocations NULL; no surface or clip record; a bitmap array that did not load; a corner or row outside the grid (the lookups `0x44f7f7`, `0x46c019`, `0x46c321`, `0x46d54c` return 0; the shading `0x46c3f0` writes `[ebx+1]` on both branches) |
| `panel.c` | `0x43ef82` | no sweep bitmap |
| `present.c` | the DirectDraw rows | a method called through a NULL object |
| `scene.c`, `shape_draw.c`, `terrain_draw.c` | `0x42e70c`, `0x40341d`, the corner row | a NULL horizon object written; no instance (reads address 8); a corner cell outside the grid (its height read at 0) |
| `view.c` | `0x40717a`, `0x407210` | a NULL light, called through |
| `volumes.c` | `0x47cad5`, `0x47cc41`, `0x47cc7d` | the stream and archive allocations NULL: `0x47ccb6` writes `[ebx+0x20]`, `0x47d083` reads `[0]` |

The divide row: `mouse_scale` `0x480898` with a client height of 0 ends at
the IDIV `0x4808cf` with `0xc0000094` ("The sites").

The rows that stay refusals, with the reason:

| Row (file) | Why it is no NULL-page fault |
|---|---|
| the ninth release entry (`dialogs.c`, button release list) | the original runs past the 8-entry list and faults at `0x453127` on the gadget pointers behind it, not on the NULL page; the guard stays a known difference |
| `0x486e10` ramps copy (`gfx.c`) | the copy runs past DATA through mapped memory until the first unmapped page |
| `0x465d0f` palette upload (`present.c`) | writes outside a stack buffer, mapped |
| shade ranges, a count outside `0..0x7fff` (`shape_draw.c`) | a negative count copies until an unmapped page; `0x8000` or more never ends |
| `0x406f69` light entry, the table full (`view.c`) | writes through the last entry pointer, mapped |
| `0x436bdb` widget switch (`hud_widgets.c`) | the original bounds the index (`cmp 0x38; ja`); the own table only |
| `free`, `realloc`, `delete` outside the heap (`services_rtl.c`) | CW3220 on a foreign block: heap corruption, not a NULL-page access |
| `0x479d0e` abort-notify node (`loop.c`) | no fault: printf `0x496844`, then `_abort` `0x49688c` |
| `0x463765`, `0x46375c` sound context (`load.c`) | no fault: `SetLastError(0xe)` and the frees |

### The sites

The VA in the FAULT line is the row's refusal VA: the IDIV or DIV itself, or
the call that reaches it. Where one check covers two divides the site is the
row's first one (the zero-divisor half of the ladder). Every DBSIM site is a signed IDIV in the listing; the SOS mixer's is the DLL's unsigned DIV. The DIV `0x436f9c` of the
EventPort frame `0x436f70` is no site: `xor edx, edx` before it divides only the
product's low dword by 60, so it never faults (hud.md "The original's slips").

| Row (file) | Site | Faults on |
|---|---|---|
| `0x47de2d` / `0x47eabe` 64-bit divide (`services_rtl.c`) | `0x47de3c` | zero divisor, quotient past 32 bits |
| `0x492b04` (`services_rtl.c`) | `0x492b0b` | zero divisor, `INT_MIN / -1` |
| `0x47e068` 16.16 divide (`poly.c`, `gfx.c`) | `0x47e078` | zero divisor, quotient past 32 bits |
| `poly.c` 64-bit divides; `fill.c` edge cut | the row's IDIV | zero divisor, quotient past 32 bits |
| `0x48aba2` texture edge (`gfx.c`) | `0x48aba2` | `-2^31 / -1` |
| frustum plane (`frustum.c`) | `0x47de3c` | zero normal length, quotient past 32 bits |
| `view.c` and `shape_draw.c` (`0x48c5fc`, `0x48cce4` and the other rows with a divide) | the row's IDIV | zero divisor, `INT_MIN / -1` |
| `0x42ed4e` horizon (`scene.c`) | `0x42ed4e` | zero divisor, overflow |
| guest entries es2sim runs on the host: the shield balance `0x413bc8`, the cockpit sync `0x41b130`, the type loads `0x4201a8`, `0x422ed0` (`core/src/type_loads.c`), the HUD speed through `0x42a1f0` (`es2_hud_speed`) | the entry (`host_fault.c`) | zero divisor; overflow where the host divide faults (the type loads and the HUD speed wrap) |
| `0x46c208` terrain slopes (`load.c`) | `0x46c208` | zero divisor, overflow |
| `0x46c23c` normalize, called by the terrain shading `0x46c2e0` (`load.c`; the registry's normalize divides on the host) | `0x46c29e` | zero normal (length 0) |
| `0x45c3bd`, `0x45c3e9` joystick poll (`input.c`) | the call to `0x47e068` | zero divisor |
| `0x480898` mouse scale (`input.c`) | `0x4808cf` | a client height of 0 |
| `0x430984` fade set-up; `0x46590d`, `0x465920` present x and y (`present.c`) | the IDIV | zero divisor; for present, also overflow |
| SOS mixer resample (`audio.c`) | `0x00406cf0`, SOS9503.DLL's DIV at its link address | a voice rate of at least output rate × 10000h (DIV overflow; only a modified WAV header, audio.md "Resampling") |
| HUD and cockpit rows: `hud_cockpit.c`, `hud_gauges.c`, `hud_frame.c`, `hud_load.c`, `hud_draw.c`, `gadgets.c`, `panel.c`, `dialogs.c` | the row's IDIV: `0x43936f`/`0x4390ee`, `0x4526e3`/`0x4525f6`, `0x452660`/`0x45257b`, `0x43b7f3`, `0x44eef3`, `0x44c461`/`0x44c47f`, `0x44f8f3`/`0x44f905`, `0x44f54b`, the ladder, the cursor warp `0x480909`/`0x480922` | zero divisor; overflow where the row checked it |

**`0x47c82c`, the buffered read**, loops as the original does
(`while (avail < n)`, no cap). A stream that never fills its buffer hangs; the
replay driver's timeout or the window's close ends the run.

### Inputs that reach a site

Each site's divisor traced back in the `llvm-objdump -d -M intel` listing of
VER95\DBSIM.EXE (each input's result confirmed headless in es2sim and live in
the original, below; every other verdict is a hypothesis from the listing). A divide fault inside a guest entry that es2sim runs on the host
(the core's type loads, the HUD speed) ends with the original's code through
`host_fault.c`, like the NULL page. The data inputs are files the mission load reads; DBSIM's opener
`0x4730e8` opens a loose file in the working directory (`dat\zone<ID>.dat`,
`dat\mat0.dat`, `gau\<herc>.GAU`) before the VOL (vol.md "Lookup and loose
files"), and es2sim's `vfs_stream.c` does the same. `sim/tests/divide_scenarios.py` writes each input
(`write <scenario> <dir>`, `remove`) or runs a replay with it
(`replay <scenario> <replay_suite.py arguments>`).

| Site | Divisor from | Verdict | Input (es2sim: FAULT line, exit `0xc0000094`, no exit records, 0 ticks) |
|---|---|---|---|
| `0x46c208` terrain slopes | the zone DAT's fourth dword, stored unchecked at terrain `+0x118` (`0x427988..0x427a24`); all 39 VOL zone DATs hold a nonzero one | reachable at mission load | `zone-zero`: c1_01's `dat\zone123.dat` (VOL values 8, 8, 14, 256) with the fourth dword 0 |
| `0x46c29e` normalize | the `0x47de72` length of a cell normal; 0 only for the zero vector | reachable at mission load | `zone-flat`: the fourth dword 16385, so `(1 << 14) / 16385` = 0 and a flat cell's normal is zero |
| `0x44f8f3`, `0x44f905` map set-up | rows and columns `((max + 60000) >> s) - ((min - 60000) >> s) + 1` in 32 bits over the script.dat point rectangle `0x4aa640..0x4aa64c` (`0x424709..0x4247de`), `s` the zone's third dword | reachable at mission load for `s` 13 to 16 | `map-rows`, `map-columns`: two more script.dat points at -2147358112 and 2^31 - 1 in y or x |
| `0x45c3bd`, `0x45c3e9` joystick | the axis bits word J+0x16, written only as 0 (`0x477689`) and 7 (`0x47761e`): divisors -24/288 and 103/41; `(1 << b) - 25` is never 0 | unreachable | |
| `0x492b0b` | its one caller `0x49280f` skips a zero length (`test esi, esi; je`, `0x492802`); the length is not negative | unreachable | |
| `0x430984` fade | all three callers push 0x78 (`0x45d82b`, `0x45dcf9`, `0x45dd3a`) | unreachable | |
| `0x46590d`, `0x465920` present | the screen size, written only as 320x240 or 640x480 (`0x45e5ac..0x45e683`); `(w << 16) / 640` cannot overflow | unreachable | |
| `0x42ed4e` horizon | a zero `sp` becomes 1 (`0x42ecbf..0x42ecc6`); overflow needs `cp << f` = `INT_MIN` with `sp` = -1, a focal shift (camera `+0x1a`, `0x48c312`) of 31 | unreachable for zero; overflow not seen | |
| `0x48aba2` texture edge | `dy` nonzero by the walk loop; `-2^31 / -1` needs a clipped screen `dx` of 2^31 | unreachable (hypothesis) | |
| `0x47de2d` callers in the near-plane clipper `0x42ff88` (`0x4300d6..0x4301e6`) | `z_b - z_a`, nonzero where the class byte `+0x826` = 2 agrees with the z test | unreachable (hypothesis) | |
| `0x46c208` terrain slopes, overflow | `(1 << S) / F`, S the zone DAT's third dword (shift count mod 32), F the fourth: `INT_MIN / -1` for S = 31, F = -1 | reachable at mission load | `zone-overflow`: the zone DAT (31, -1) and a loose `dat\mat0.dat` with every material's level `+4` 22, which keeps the base-mark raster in the grid (below); exit `0xc0000095` at `0x0046c208` |
| `0x44c461`, `0x44c47f` map page | half the map page's width and height from the panel layout `gau\<herc>.GAU` (`0x431778`, 0x6a4 bytes), HDD record `L+0x4bc` (`0x432634` → `0x448cf0` → map constructor `0x44c28c`): `((L+0x4dc - L+0x4cc) << sx - 8 << sx) sar 1` and `((L+0x4d8 - L+0x4d0) << sy - 4 << sy) sar 1`; the dividend is half the script.dat range, so no overflow | reachable at mission load | `map-page-columns` (`L+0x4dc` = `L+0x4cc` + 8), `map-page-rows` (`L+0x4d8` = `L+0x4d0` + 4); all nine VOL layouts as loose files |
| `0x48c63b` projection `0x48c5fc` (`view.c`) | the view-space depth `v.y` when the focal shift `[0x6c6028]` is nonzero (else `[0x6c6030]`, `0x48c625`); the 3D callers clip at the near plane (camera.md), the map page's projection at mission load does not, and with the map scale 0 its depth is 0 | reachable at mission load | `map-scale`: every script.dat point's x set to the first one's, so the x quotient of `0x44c461` and the map scale are 0 |
| `0x44f54b` map icon ring | the map view's scale | unreachable on its own: a zero scale faults first at `0x48c63b`, at load | |
| `0x44eef3` map zoom | `[0x4d1ce0] - 60000`; `0x4d1ce0` is written only at `0x44c48f`: the smaller quotient of `0x44c461`/`0x44c47f` shifted left by `[0x49d6bc]` = 8 (never written), a multiple of 256, and 60000 mod 256 = 96 | unreachable | |
| `0x43b7f3` heading tape range | the tape's width `D+0x28` = `D+0xc - D+4` (`0x43b604`), the rectangle `L+0x450..0x45c` of the HUD record `L+0x440` (`0x4325c0` → `0x43c800`, tape `0x43b5a4` at `0x43ca2c`); the range set `0x43b7dc` runs once, at `0x43ca6b`, with -3640..3640, so no overflow | reachable at mission load | `heading-tape`: `L+0x458` = `L+0x450` |
| `0x43b67c` tape step | the same `D+0x28` | unreachable on its own: a zero width faults first at `0x43b7f3`; overflow needs a 16.16 product of `INT_MIN` | |
| `0x45257b`, `0x452660` slider value | the travel over the range; the weapon gauge slider (`0x442978`) travels 16 << sx; the throttle (`0x447bac`, record `L+0x3e8`, rectangle `L+0x3f8..0x404`, vertical in all nine layouts) travels the rectangle's height less the knob's (6 in the DBA); the value is read by slot +0x10, which only the panel's mouse handler `0x452898` calls (`0x45292f`, the widget under the cursor with the button held) | `0x45257b` unreachable; `0x452660` reachable through the panel layout and a left click on the throttle | `throttle`: `L+0x404` = `L+0x3fc` + 6, and a left click inside the rectangle: panel units shifted by sx = sy = 1, so (282, 355) for the Outlaw's x 134..148, y 175..181 (`c1_01-mouse` with the click script `c1_01-throttle.jsonl` in es2-data `logs\divide-sites-live`); FAULT at `0x00452660` on the release, tick 104; the same click without the scenario does not fault. Without a click `c1_01-keys` and `c1_01-mouse` replay IDENTICAL with it |
| altitude ladder `0x43decf` (es2sim's FAULT VA `0x43dd98`) | ceiling + 5000 - floor: after `terrain_cells` (`0x46c4c4`) the floor `G+0x110` is 0 and the ceiling `G+0x114` the highest heightmap pixel times the zone DAT's fourth dword (`0x46c70b..0x46c720`) | reachable at mission load for a flyer (the ladder draws only for one) | `ladder` on `c4_08-slope-dive`: script.dat's zone set to 508 (the only VOL heightmap whose highest pixel, 100, divides 5000) and its DAT's fourth dword -50; control: the zone switch with the VOL DAT runs without a fault (2518 frames) |
| ladder `0x43e095`, `0x43e09b` | 50 and 0x24 << sy | unreachable | |
| `0x4390ee`, `0x43936f` bar constructors | the word argument: the wrappers `0x4395c4`, `0x439834` and the direct callers pass 0x9c4, 0x400 or -0x400 | unreachable | |
| `0x4525f6`, `0x4526e3` slider scale | `S+0x26 - S+0x24`: the base calls `0x4524d0` pass 0..0x100, the throttle sets 0xfc00..0x400 | unreachable | |
| `0x43b4cc` rotation gauge, `0x43c40c` nav gauge | the word `D+0x2c` = hi - lo: 0x9000 from `0x43b3a0`, or the range set `0x43b3e8` from the constant pairs 0xc71d..0x38e3 and 0xf1c8..0x0e38 | unreachable | |
| `0x43b978` locking gunsight | the first IDIV (`0x43bb42`) divides by `dist2d` (`0x47de4c`) of the player's and the target's position, the other two by screen deltas set to 1 when 0 | unreachable from a file: the distance is 0 only when the two positions coincide in play; overflow needs a delta of -1 with a product of `INT_MIN` (not seen) | |
| `0x43ef5e`, `0x43f048`, `0x43f0fc`/`0x43f108`, `0x43f1aa`/`0x43f1b3` radar page | `R+0x28` = 25 (`0x43e7a4`), then the range over it; the range `[[R+8]+0xc]` taken to be the selected one of 50000, 100000, 200000 (`0x4459d5..0x4459e9`, `0x447014`) | unreachable (hypothesis) | |
| `0x43f4ab`, `0x43f4db`/`0x43f4e4`, `0x43f5a2`/`0x43f5ab` HUD radar | 23 << sy, then the range over it | unreachable | |
| `0x44f371` map icon range | `I+0x53` (`0x44edcd`, the `0x47de72` length estimate), 0 replaced by 1 | unreachable for zero; overflow not seen | |
| `0x480909`, `0x480922` cursor warp | the DIB width and height >> 1 | unreachable | |
| `0x47eabe` in the normalizers `0x47e118`, `0x47e300`, `0x47e450`, `0x47e610` | the vector's length under `x · 0x800` | zero only for a zero vector from the caller's geometry; overflow impossible (no component exceeds the length) | |
| `0x47e068` 16.16 divide, 37 callers in 18 functions | constants or bounds: 50 (`0x4173e3`), 1 (`0x4680c3`), at least 0x280 under 1 (`0x46859e`, `0x468bf3`, `0x469563`, `0x4781d2`), the joystick (above); clip fractions whose dividend does not exceed the divisor: `0x401a08` (the time left over the ring entry it no longer exceeds), `0x404df9` (10 over a word above a positive bound), `0x40bb85`, `0x40bbd2` (line clip), `0x468117` (the overshoot past half the screen width over the width); `0x46832d`, `0x46895b` edge steps, `0x468827` `1 << f` over a depth, `0x469900`, `0x469b9b` screen deltas; `0x4692c4`, `0x4696d8`, `0x469754` lie in `0x4690f0`, which nothing calls; `0x46e980` (four) shifts its operands right to ±0x7d00 and tests for zero; `0x477d48` (six) tests for zero; four in `0x48aaac` divide by texture edges; the type loads `0x4202f2`, `0x42044b`, `0x422fa1` divide by vehicle and flight-model words (below) | unreachable except the type loads; `0x46e980` cannot overflow; for `0x468827` (f at most 15), `0x469900`/`0x469b9b` (screen coordinates below 2^30), `0x477d48` (texture coordinates below 2^15) and `0x48aaac` the bound is a hypothesis | |
| `poly.c` `0x471d26` (`0x471d40`, `0x471d55`) | a weight sum tested for zero; the quotient is a positively weighted average of 32-bit `u << 16` values | unreachable | |
| `poly.c` `0x471d61` (`0x471d75`, `0x471d89`, `0x471da1`) | the row width `esi`, at least `1 << s` = 16 (`0x468e6b`) | unreachable | |
| `poly.c` edge tables and fills | `div16(1, w)` with w at least 0x280 | unreachable | |
| `fill.c` clip cuts `0x484338` (polygon), `0x483c6c` (line) | the coordinate difference across a crossed clip edge, nonzero; the quotient is at most the other delta | unreachable while coordinate differences stay below 2^31 (hypothesis) | |
| `frustum.c` plane normals (`0x47e450` → `0x47de3c`) | the length of the first plane normal of the frustum `0x4949e4`, whose depth `d` is the terrain's far distance `G+0x10c` (call `0x46cee0`): the detail table word at `0x4a0bb4` (6, 10 or 14 by the detail byte `0x4d1f3f`) `sar` (S - 14) when the zone DAT's third dword S exceeds 14 (`0x46cbd4..0x46cbf2`) | reachable at mission load for S at least 18 (17 at detail 0) | `frustum-far`: c1_01's zone DAT with S = 18 and a loose `dat\mat0.dat` with the levels 3, which keeps the base-mark raster in the grid; FAULT at `0x0047de3c` |
| `shape_draw.c` perspective projections `0x491337`/`0x491356`, `0x48cc26`/`0x48cc4a`, `0x48ce12`/`0x48ce33` | the depth, clipped above the near plane (camera `+0x1e` = 0x100) | unreachable | |
| `shape_draw.c` `[0x6c6030]` rows `0x48c9cc`/`0x48c9ed`, `0x48cb08`/`0x48cb2e`, `0x48cd38`/`0x48cd56`; `view.c` `0x48c5e2`, `0x48c616`, `0x48c625` | `[0x6c6030]`, used only with the focal shift `[0x6c6028]` 0; the shift is 8 or 9 (camera.md) | unreachable | |
| `shape_draw.c` detail part `0x476a25`, actor levels `0x403463` | a distance forced to at least 1 | unreachable | |
| `shape_draw.c` detail test `0x4762f7` | the cosine of `atan(w / 256)` with w at most 640, positive | unreachable | |
| `shape_draw.c` beam `0x40be1e`, `0x40be2a` | the beam length, tested for zero and positive | unreachable | |
| `shape_draw.c` cell animation `0x4768f0` (IDIV `0x47691c`) | the TSCellAnimPart's u16 child count `+0x10`; every shipped one (the 55 DTS files of SIMVOL0.VOL) is at least 2 | reachable through a modified DTS when the shape draws | `cell-anim`: SIMVOL0.VOL with every child count 0; FAULT at `0x0047691c` |
| `view.c` spot direction `0x407197` | `max(d, 1)` | unreachable | |
| `0x413bc8` shield balance (IDIV `0x413c2c`, `0x413c39`) | P+0x22a, the vehicle configuration's shield maximum `+0xbe` copied at creation (`0x413a90`); every shipped player herc holds 3500; the IDIV `0x413cac` divides by 1000 | reachable at mission load | `shield-zero`: `dat\<herc>.DAT` `+0xbe` = 0; FAULT in the entry `0x00413bc8` |
| `0x41b130` cockpit sync (IDIV `0x41b26b`; `0x41b14b` divides by 10000) | the vehicle configuration's forward speed `+0x04` (C+6); shipped 140 to 325 | reachable at mission start | `forward-speed-zero`: `+0x04` = 0; FAULT in the entry `0x0041b130` |
| `0x47e068` at `0x4202f2` (mech type load) | `+0xc2`, the ground rescale denominator, when `+0x4e` is 0 (RAZOR has `+0xc2` = 0 with `+0x4e` = 1 and skips it) | reachable at mission load; a small `+0xc2` overflows | `speed-scale-zero`: `+0xc2` = 0; FAULT in the entry `0x004201a8` |
| `0x47e068` at `0x42044b` (mech), `0x422fa1` (flyer) | FM+0x2a - FM+0x2e of `fm\<name>.FM` (RAZOR 1500 - 250, SKIMMER 1000 - 500), dividing (FM+0x22 - FM+0x26) << 16 | reachable at mission load, zero and overflow | `flight-model-zero`: FM+0x2e = FM+0x2a; FAULT in the entry `0x00422ed0`. `flight-model-overflow`: FM+0x2e = FM+0x2a - 1; the original faults with `0xc0000095` at `0x47e078`; FAULT `0xc0000095` in the entry `0x00422ed0` |
| `0x47df48` at `0x41bb71` HUD speed `0x41bb3c` | x1 - x0 = C+0x206 = FM+0x2a, for a vehicle with `+0x4e` set (C+0x50); the other branch divides by `+0x04` (C+6), which faults first at `0x41b26b` | reachable when the panel draws | `hud-speed-zero` on `c4_08-razor-plasma`: FM+0x2a = 0, FM+0x2e lowered by the same amount (the type loads' divisor unchanged); FAULT in the entry `0x0042a1f0` (the panel; es2sim's divide is in the core's `es2_hud_speed`), the original's IDIV `0x47df7a` |
| animation tracks `0x4790b4`, `0x479194` | the track duration T+1e; all 2,446 frame durations (50 to 100) and 989 transition durations (100) in the 152 ANAnimLists of SIMVOL0.VOL are nonzero | unreachable with the shipped files; a DTS with a zero duration could reach it (`0x479720` stores it unchecked, the zero branch at `0x479903` does not clear T+20); no scenario | |
| near-plane crossings `0x48d522`, `0x48d2b1`/`0x48d2d2` (`shape_draw.c` `crossing()`) | the depth difference across the near plane, nonzero | unreachable for zero; `INT_MIN / -1` needs d = -1 with a low dword of `INT_MIN`, not traced | |
| `shape_draw.c` detail test `0x47633a`; sprite part `0x47664f`; `view.c` spot light `0x40712f`; projection `0x48c5f6` callers | `ty` (overflow for -1 only); the bitmap width word `+4`; s + E+0xf (the table default T+0x10 or the setter `0x406f00`'s argument >> 5); the depth at 45 call sites | not decided (Open) | |

A zone DAT with the third dword 31 and the fourth -1 (meant for the
`0x46c208` overflow; scenario `zone-null-cell`) never reaches the slopes: the
actor load (`0x461608` → `0x4253d8` → base marks `0x405db0`) runs the
base-mark raster `0x47136c` first, the third dword (`G+0x108`) makes its shift
18, the box's cell index leaves the grid, the cell pointer read there is 0 and
the raster reads `[0 + 0xf]`; in the listing that is `mov dl, byte ptr
[eax+0xf]` at `0x471455`, an access violation in the original (live, below).
es2sim ends with `0xc0000005` at `0x0047136c`, 0 ticks, reason
`guest_fault` (es2-data `logs\null-page-fault-20261025`). The raster takes
its shift counts mod 32: with k = S - 13 and the building material's level t
(`+4` of its `mat0.dat` record) it shifts right by 21 - t and left by
8 - t - k. For S = 31 and t = 22 both cell indices are 0 for any position,
inside the grid; with the VOL levels (4 to 6) they leave it. `zone-overflow`
adds such a `mat0.dat`; es2sim then reaches the slopes and ends with
`0xc0000095` (integer overflow) at `0x0046c208`, 0 ticks. Controls: a loose `zone22.dat` (a zone c1_01 does not load) and a
loose `zone123.dat` with the VOL values replay IDENTICAL. Evidence: es2-data
`logs\divide-trigger-20261025` (`divsites.txt`, the replays `scenario-*` and
`replay-*`, `report.md`).

**The original, live** (`zone-zero` on `timing-r111`, `check.ps1 -Live -Suite
c1_01-keys`, all hooks replaced, hidden window; es2-data
`logs\divide-trigger-live`): DBSIM ends with `0xc0000094` (-1073741676) at
mission load, 0 tick records, no exit record, no return file (`outputs` of
the run empty). The Windows application log names `DBSIM.EXE` of the clone,
exception code `0xc0000094`, fault offset `0x6c208`, i.e. `0x46c208`
(`wer-event-1000.txt`), the same site and code as es2sim's FAULT line. The
faulting module is `DBSIM.EXE` itself, so the divide is the original's
instruction (the terrain slopes have no hook).

The other inputs the same way, but with the original runtime and all hooks
original (`-Runtime original -HookModes`; es2-data `logs\fault-inputs-live`,
`report.md`): each run ends at mission load with 0 tick records, and the WER
event names `DBSIM.EXE` of the clone with the predicted code and site:

| Input | Exit code and WER code | Fault offset |
|---|---|---|
| `zone-flat` | `0xc0000094` | `0x6c29e` |
| `zone-null-cell` | `0xc0000005` | `0x71455` |
| `zone-overflow` | `0xc0000095` | `0x6c208` |
| `map-columns`, `map-rows` | `0xc0000094` | `0x4f905`, `0x4f8f3` |
| `map-page-columns`, `map-page-rows` | `0xc0000094` | `0x4c461`, `0x4c47f` |
| `heading-tape` | `0xc0000094` | `0x3b7f3` |
| `map-scale` | `0xc0000094` | `0x8c63b` |
| `frustum-far` | `0xc0000094` | `0x7de3c` |
| `shield-zero` | `0xc0000094` | `0x13c2c` |
| `forward-speed-zero` | `0xc0000094` | `0x1b26b` |
| `speed-scale-zero`, `flight-model-zero` | `0xc0000094` | `0x7e078` |
| `flight-model-overflow` | `0xc0000095` | `0x7e078` |
| `cell-anim` | `0xc0000094` | `0x7691c` |
| `hud-speed-zero` (`c4_08-razor-plasma`) | `0xc0000094` | `0x7df7a` |
| `ladder` (`c4_08-slope-dive`) | `0xc0000094` | `0x3decf` |
| `throttle` (`c1_01-mouse` with the click) | `0xc0000094` | `0x52660` |

The rows from `frustum-far` on: es2-data `logs\divide-sites-live` (the
runner scripts, `check-<input>.txt`, `wer-<input>.txt`, the check
directories). `c4_08-slope-dive` deploys its launch set's script.dat over the
scenario's, so `ladder` ran with a temporary launch set (`c4_08-razor-plasma`
with the scenario's script.dat, `ladder-launch-manifest.json`);
`throttle` ran `c1_01-mouse` with the click script in place of its own. The
clone's script.dat and SIMVOL0.VOL hashes are the same after every run.

With the own runtime and the hooks replaced (`check.ps1`'s default)
`zone-null-cell` also ends with `0xc0000005`, but in the proxy (WER module
`DDRAW.dll`): the NULL read happens in the replaced code.

### Deviations

- **No SEH walk.** The guest's records point into no-access CODE, so es2sim
  ends the process itself at the fault; by the static reading no DBSIM record
  handles a divide fault or an access violation, and live the original ends
  before an unwind runs the frames' clean-up `0x40415d` (above, "How the
  original ends"). So no clean-up runs in either.
- **No OS report.** The crash dialog and error report are not reproduced;
  es2sim prints the FAULT line. The exit code is the original's.
- **The site** is es2sim's refusal VA, not always the faulting instruction.
  For a divide on the host inside a guest entry it is the entry.

### Acceptance

- `//sim:fault_paths_test`: the primitive's codes; the loop helpers with no
  loop, no record, a record without `process_fault`, the recorder, and
  operands that do not fault; the rows `0x47de2d`, `0x47eabe`, `0x492b04`
  through the dispatch over the bound loop.
- `//sim:hud_test` `event_pause`: negative products and products past
  60 << 32 in `0x436f70` neither fault nor refuse, headless, windowed and with
  a recorder (`differential_hud_widgets_test`: the original's machine code
  alike).
- `//sim:es2sim_real_test`, `//sim:es2sim_window_run_test`: the fatal report's
  `__exit` ends headless with 1 and reason `fatal_error`, no refused event.
- `//sim:replay_suite_test`: a guest fault gives the verdict FAULT with the
  host's line; against a golden that exited with the same code it is
  IDENTICAL.
- `//sim:load_test` `load_rest`: a zero fourth dword refuses at `0x46c208`;
  flat corners with a zero slope quotient refuse at `0x46c29e` before
  normalize runs.
- `//sim:divide_scenarios_test`: the scenario files on synthetic data, and the
  map set-up's 32-bit count wrapping to 0 for shifts 13 to 16.
- `divide_scenarios.py replay <scenario> --suite c1_01-keys`: FAULT at
  `0x46c208`, `0x46c29e`, `0x44f905`, `0x44f8f3`, `0x44c461`, `0x44c47f`,
  `0x43b7f3`, `0x48c63b` with `0xc0000094`; `zone-overflow`: `0xc0000095` at
  `0x46c208`; `zone-null-cell`: `0xc0000005` at `0x0047136c` (the NULL page);
  `ladder` on `c4_08-slope-dive`: `0xc0000094` at `0x0043dd98`.
- `//sim:host_fault_test`: the NULL page ends with `0xc0000005`, above it 71;
  a divide-by-zero or overflow record is a divide fault.
- `divide_scenarios.py replay <scenario> --suite c1_01-keys`: `frustum-far`
  FAULT at `0x0047de3c`, `cell-anim` at `0x0047691c`, `shield-zero` in the
  entry `0x00413bc8`, `forward-speed-zero` in `0x0041b130`,
  `speed-scale-zero` in `0x004201a8`, `flight-model-zero` in `0x00422ed0`,
  all `0xc0000094` at 0 ticks; `hud-speed-zero` on `c4_08-razor-plasma`:
  `0xc0000094` in the entry `0x0042a1f0`; an identity patch of `zone123.dat`
  replays IDENTICAL (es2-data `logs\divide-sites`). `throttle` on
  `c1_01-mouse` with the click script: FAULT at `0x00452660`, tick 104; the
  click without the scenario does not fault (es2-data
  `logs\divide-sites-live\throttle-es2sim`).
- `//sim:fault_paths_test` `null_paths`: `es2_sim_loop_null_refuse` without
  a record and with one lacking `process_fault` refuses; with the recorder it
  ends EXITED with `0xc0000005` at the row; a `free` outside the heap stays a
  refusal. `//sim:input_test` `mouse`: the joystick row `0x477787` refuses
  headless and ends with `0xc0000005` with the recorder; WM_SIZE with a client
  height of 0 ends with `0xc0000094` at `0x4808cf`.
- `//sim:sos_mixer_test`: a voice rate of output rate × 10000h ends the run
  with `0xc0000095` at `0x00406cf0` through a recorder and nothing more is
  mixed; one below it does not fault; without a record the quotient
  saturates.
- `//tools/reference-game/proxy:fault_trace_{arming,calls,raise,probe}_test`:
  the trace on a synthetic DBSIM-shaped image (the slot, the refusals, search
  and unwinding calls through the thunk, a raised fault and RtlUnwind through
  a frame record on fs:0, the exit probe).
- No golden reaches a fault site, so the replays are unchanged.
- Live, original: every input of "Inputs that reach a site" ends with
  es2sim's code at the same site (es2-data `logs\divide-trigger-live`,
  `logs\fault-inputs-live`, `logs\divide-sites-live`); the unwind trace
  (`logs\fault-trace-live`).

### Open

- **The sites still undecided** in "Inputs that reach a site": the near-plane
  crossings' `INT_MIN / -1`, the detail test `0x47633a`, the sprite part
  `0x47664f` (a zero-width bitmap; its loader not traced), the spot light
  `0x40712f`, the 45 callers of the projection `0x48c5f6`, the full-width
  callers of `angle_ratio` (`0x47d32c`), the frame-count IDIV of
  `explosion_update`, and the bounds marked as hypotheses.
- **The NULL-access rows** ("NULL accesses") are judged from the listing,
  not by a live run; `su_null_refuse` has no unit test of its own.
- **`main.c`'s start-up EXIT branch** (`outcome == ES2_SIM_STARTUP_EXIT`) is
  unreachable (es2sim always sets a record and its `process_exit` does not
  return); kept as the fallback for a record that returns.

## The heap trace

The heap at DBSIM's entry already holds CW3220's attach blocks, and es2sim
makes the same blocks in the same order, so every later block lies at the
original's offset in its reservation and reads past a block's end find the
original's bytes ([The free camera's shape
edge](#the-free-cameras-shape-edge)). Code: `sim/src/heap.c`,
`sim/include/es2/sim/heap.h` (`ES2_SIM_HEAP_RESERVATION_GAP`; CW3220's own
`_malloc` and `_free` on the game's heap, `es2_sim_heap_runtime_malloc`,
`es2_sim_heap_runtime_free`; the attach blocks,
`es2_sim_heap_copy_environment`, `es2_sim_heap_static_objects`; an observer of
every operation); `sim/src/services_files.c`, `sim/include/es2/sim/files.h`
(the file services on the game's heap; there is no second heap);
`sim/src/host.c` (`es2_sim_runtime_attach`, `[trace] heap_trace`);
`sim/src/main.c` (the attach, before the c0w32 entry);
`tools/reference-game/proxy/rtl_heap_shadow.h` (`[rtl] heap_trace`);
`tools/reference-game/scripts/heap_trace_compare.py`.

### The trace

The proxy and es2sim write the same NDJSON lines:

- **attach**: DBSIM's frame counter (`g_sim_ctx + 0xbf`), the allocator's
  variables (sentinel, rover, committed and reserved end, granularity,
  platform), the block count, the regions, the length of each environment
  string and of the command line (lengths only, never contents);
- **block**: one line per block as DBSIM's entry finds it (address, size,
  in-use bit);
- **op**: index and frame; the function and its origin (`import`: DBSIM
  through its imports, new and delete reaching the heap through `0x47747c`'s
  calloc and `_free`; `internal`: CW3220's own call); the caller's return
  address, both arguments, the result and the rover; the reservations and
  releases it made;
- **end**: the operation count and the lines lost, with the exit record.

The proxy writes with `[rtl] enabled=1`, `calloc`, `realloc` and
`free=shadow`, and `heap_trace=<path>`; es2sim with `[trace]
heap_trace=<path>`. Each operation's line is written once it is complete. A
suite ended at its frame limit by TerminateProcess gets no exit record.

`heap_trace_compare.py ORIGINAL ES2SIM` pairs the operations in order and
reports the first operation where function, origin, request size or the
import's caller differ (a caller of 0 is not compared: es2sim's own code calls
the heap too, for example `arena_pop`'s realloc in `load.c`), the double frees
on both sides, and the low 16 bits of each result with their difference, as
runs (the 4 MiB reservations are 64K-aligned, so the low word is the block's
offset in its reservation). `--find SIDE:ADDRESS` names the operation that
made a block. Unit test:
`//tools/reference-game/scripts:heap_trace_compare_unit_test` (synthetic
traces).

### What the original does (c1_01-free-camera, live, clone timing-r111-p1)

1155 frames, frame records identical to the reference. Evidence: es2-data
`logs\a1-heap-trace-20261023` (`original\heap-trace.ndjson`).

- **At DBSIM's entry**: 116 blocks in one region, 115 in use and the top free
  block of 56264 bytes; rover `0x02632438`, sentinel `0x008c3944`. In address
  order they are CW3220's init records (table `0x423084`, by priority):
  1. `0x41a050`, the environment copy: the pointer array (420 bytes: 102
     strings and the NULL word), then one `_malloc(strlen + 1)` per string in
     the block's order;
  2. the stdin and stdout buffers, two `_malloc(512)`;
  3. the C++ static objects: `0x409f98`, three filebufs, new(0x3c) and
     new[](0x204) each; `0x418444`, new[](0x100); `0x417dc8`, the xalloc
     message: its TStringRef new(0x12), the characters `_malloc(64)`, and
     xmsg's string new(4).
- **The environment.** The proxy reads it when the trace opens, after
  CW3220's attach, and lists 103 strings (4698 bytes); fitted to the block
  sizes, its string 48 (28 bytes) is not in CW3220's copy, the other 102 match
  in order (Windows keeps the block sorted, which fits a variable set after
  the attach).
- **7640 operations**: imports calloc 6518, free 1071, realloc 45; CW3220's
  own malloc 4, free 2.
- **The second reservation** is at `0x04440000`, not at `0x02630000 + 4 MiB`.
  `__heapadd` therefore opens a second region, and the growing block starts at
  its base + 8 (an adjoining reservation would extend the first region and
  join the free block below its end).
- **Op 4948** frees a block that is already free, in both runs: the game's
  double free.

### What es2sim does

- **The attach** (`es2_sim_runtime_attach`, before the c0w32 entry) makes the
  same blocks in the same order from es2sim's own environment
  (`GetEnvironmentStringsA`, none skipped): the copy; the file services' two
  buffers (`es2_sim_files_init`); the ten static-object blocks. Only the sizes
  are modelled: CW3220's vtables and fields in those blocks are read only by
  its iostream and string code, which DBSIM does not import.
- **The file services' blocks** (setvbuf `0x405384`, fclose `0x40555c`, the
  find nodes) come from the game's heap through CW3220's `_malloc` and
  `_free`; not zero-filled, without the replaced heap's modelled registers
  (rtl_imports.md "Registers and stack residue").
- **Reservations at the preferred base** never adjoin: the next starts
  `ES2_SIM_HEAP_RESERVATION_GAP` (`0x10000`) above the end of the last one, a
  multiple of 64K, so the low word stays the offset.

### Result

es2sim on c1_01-free-camera against the original's trace:

| Run | Blocks at entry | Operations | Low words |
|---|---|---|---|
| without the attach | 0 | the first call differs at op 1 (CW3220's 512-byte buffer) | −9264 from op 0 |
| attach, reservations adjoining | 111 | 7640 equal | −428 to op 3791, then other differences in the second 4 MiB |
| attach and gap | 111 (97 strings, 4306 bytes) | 7640 equal | runs of −428 (the first region) and +0 (the second) |
| attach, gap, environment padded | 116 (102 strings, 4669 bytes) | 7640 equal | +0 over ops 0..7636 (6567 results) |

The double free at op 4948 is found on both sides in every attach run.

- **The rest is the environment.** With the build host's environment the copy
  is 428 bytes shorter and every block of the first region lies 428 bytes
  lower; with an environment whose copy ends where the original's does, every
  result's low word is the original's. The environment is process input (the
  after-load snapshot's `environment` class); the reconstruction does not pad
  it.
- **The frame tests' environment.** The frame tests pad it, since their
  references were captured with the original's environment: `replay_suite.py
  --frames` keeps es2sim's `SYSTEMROOT`, `PATH`, `TEMP`, `TMP`, `TZ`,
  `ES2SIM_*` and `SDL_*`, and adds variables `Z000`, `Z001`, … until the copy
  has `frames_known.REFERENCE_ENVIRONMENT`: 102 strings in 5648 bytes of
  string blocks, the original's. With fewer bytes (a minimal or Bazel
  environment) `c1_08-scan` differs from frame 1185 and `c1_08-escort-route`
  differs too (es2-data `logs\fulltest-review-20261023.log`,
  `//sim:frames_all` 38/40); with the padded environment both pass, as with
  the build host's shell environment (`check-review-1`, own-sim frames 40/40).
- **The free camera.** `//sim:frames_c1_01-free-camera_test` passes with all
  1001 sequence records equal.

**Acceptance.** `//sim:heap_test`: the gap (a second region at base + 4 MiB +
gap, its block at base + 8) and the attach blocks (the array, the copied
strings, the NULL word, the ten static blocks and their sizes, an empty
environment). `//sim:services_test`, `//sim:frames_c1_01-free-camera_test`,
`heap_trace_compare_unit_test`,
`//tools/reference-game/setup:suite_scripts_test`, `//sim:all`, the proxy's
tests, the in-vitro startup, rtl_heap and input_layer cases,
`//tools/reference-game/scripts:all`: pass.

**Open.**

- **The 103rd string**: which variable joins the environment after CW3220's
  attach in the proxy's run, and whether a plain DBSIM start has it (either
  way it changes only the low words).
- **The own cw3220.dll** still makes no environment copy and no static
  objects.
- **Other suites.** Only c1_01-free-camera was traced live. The replays
  c1_01-success, c3_09-loadout-b and c5_05-moon-missiles pass on this state;
  the frame compare passes 40/40.

## Called gap roots

The 14 roots of [sim_gap.md](sim_gap.md) "Called by own code (the roots)" and
the 21 functions behind them ("Behind the called roots, per area"), each with
its outcome in es2sim. "Listing" = llvm-objdump -d -M intel of the reference
DBSIM.EXE (SHA-256 `8561aa47...c4ad`). Rows: `sim/dispatch/services/adapter_leaves.h`
with `sim/src/adapter_leaves.c`. A refusal means the VA keeps no row and the
dispatch refuses a call to it; the reason says why no own path calls it, or
why the call stops the run.

### The live root `0x4968f8`

`0x4968f8` is `jmp [0x6cc3f0]`, the GetProcAddress thunk. Its callers in the
listing are `0x495972` and `0x4959e2`, the two DLL branches of the SOS driver
loader `0x4958fd` (97 names each), and `0x49646b` in `0x496425`. The 40/40 of
the live map are the original's entries: the loader runs at every start.
es2sim never calls the VA:

- `0x4958fd` is own code (`sim/src/audio.c` `dll_load`): no DLL, the stand-in
  handle `0x50500000` in `0x6cbd58`, the 97 slots of `0x4a6a9c` untouched, no
  GetProcAddress.
- `0x496425` is own code in `sim/src/init.c` (`es2_sim_c0w32_entry`). It
  calls `0x4968f8` only when the byte `0x497058` is set. The byte is 0 in the
  image, and the listing has one reference to it, the read at `0x49644f`
  (`cmp byte [0x497058],0`), and no writer.

So the run does not stop: the only own call is behind a byte that the
reference file never sets. **Refused on purpose**; a row would serve a path
that no run takes.

### Rows

| VA | Row | Caller in es2sim | Contract (listing) |
|---|---|---|---|
| `0x401148` | `camera_mode` | `commands_hooks.h` playback keys `0x45fe6c` → core commands.c camera(C, 3) at `0x4600b7`; masked (Shift+V word `0x49eab8` = `0x47ff`, no writer) | `0x401148..0x40119e`: word C+0x36 ← mode for movsx(mode) 0..3 unless dword C+0x4a ≠ 0 (read again per branch); EAX C; EDX the entry's when C+0x4a is set, else -1 (mode 0), 0 (1..3), mode - 3 (any other); ECX the entry's |
| `0x4967fc` | `file_time` (`sim/dispatch/services/rtl_time.h`) | services_files.c demo_pick (`0x45ceb8`, `-D` with more than one demo) | CW3220's `_time` over the files' OS clock: [_time in es2sim](#_time-in-es2sim) |
| `0x423044` | `flyer_slot` | core formation.c `fm_flyer_slot` computes it inline and only counts it | `0x423044..0x423067`: EAX = `[0x4a9dfc]` + 18·movsx(type) + 6·movsx(slot) - 6; EDX movsx(slot); ECX 6·movsx(slot) |
| `0x427d98` | `sweep_clear` | core weapons.c `sweep_leaf` computes it inline and only counts it | `0x427d98..0x427d9f`: dword `0x4aaad0` ← 0, EAX 0 |
| `0x427da0` | `sweep_read` | as `0x427d98` | `0x427da0..0x427da5`: EAX dword `0x4aaad0` |
| `0x467ad8` | `debug_break` | `commands_hooks.h` playback keys `0x45fe6c`: Ctrl+B (code `0x430`) with the option word `0x4d252c` (`-B`, startup.c) calls it through the dispatch | `0x467ad8..0x467ad9`: `int3; ret`. No DBSIM handler takes EXCEPTION_BREAKPOINT (`__ExceptionHandler` acts only for `0x0EEFFACE`), so without a debugger the process ends as on a divide fault ([The original's fault paths](#the-originals-fault-paths), step 4 a hypothesis). The row calls `es2_sim_loop_fault(l, 0x80000003, 0x467ad8)`: with the record's `process_fault` es2sim ends there with exit code `0x80000003`; without it the row refuses |

The four leaves are reached only through hook adapters that compute them
inline (`0x423044`, `0x427d98`, `0x427da0`) or through a masked branch
(`0x401148`); the rows serve a direct call with the confirmed contract.

Confirmed in vitro: `differential_adapter_leaves_test` (`adapter_leaves_cases.h`),
2,000 random cases of 1..8 calls against the original's machine code (EAX, EDX
and ECX of every call, DATA `0x497000..0x6cc000`, the heap); every coverage
counter non-zero (camera locked, modes 0, 1, 2, 3, above 3 and negative, mode
with upper half set, negative slot). The mutation "EDX 0 for mode 0" is caught
at case 3. `//sim:adapter_leaves_test`: the breakpoint row ends the run EXITED
with `0x80000003` at `0x467ad8` under a recorder and refuses without a record
or without `process_fault`; the sweep rows serve over the bound loop and
refuse without one. The breakpoint is not compared in vitro (it would end the
test process); no live run of the original has reached it.

### Refusals

| VA | Reason | Evidence |
|---|---|---|
| `0x4968f8` | no own path calls it | above |
| `0x42900c`, `0x42937c` | dead | scene.c `sort_compare` (`0x429460`) calls them only for record kinds ≠ 0; the record builder `0x428d14` writes word +0x16 = 0 for every record ([SIM, R3D and HUD gap roots as rows and verdicts](#sim-r3d-and-hud-gap-roots-as-rows-and-verdicts)) |
| `0x428f38`, `0x42933c` | dead | reached only from `0x42900c` and `0x42937c` |
| `0x475900` | dead | reached only through slot 6 (destructor) of `0x49afb0` (row `scene_hzline_dtor` `0x42f6d2`; no slot is called on `[0x49aee0]`, horizon.md) and of `0x4a0fbf` (no own code destroys a TS shape); same section |
| `0x464d44` | caller replaced | startup.c calls it only when `s->dib_section` is NULL; `main.c` always sets `own_dib` (`es2_sim_dib_section`, frame_buffer.c); `startup_test` stubs the VA |
| `0x496928`, `0x496964`, `0x4969ee`, `0x496a0c`, `0x496a18`, `0x496a1e`, `0x496a3c`, `0x496a4e` | behind `0x464d44` only | the live counts (`0x496964`, `0x4969ee`, `0x496a0c`, `0x496a4e` 40/40) are the original's DIB section; own code reaches none of them |
| `0x49679c` `__exit`, `0x49688c` `_abort` | served by the process record | loop.c `report_process_end` and startup.c's fatal path call the record's `process_exit` (`_abort`: `es2_sim_process_abort`, box and status 3) and end the run; es2sim always sets a record (main.c `window_process`, `headless_process`), so the dispatch sees these VAs only from a loop without a record, where the refusal is the tested answer (`loop_test`, `fatal_sites_test`, `abort_cleanup_test`, `startup_test`) |
| `0x4667c8` | row `fullscreen_switch` | modelled DirectDraw, desktop full screen in es2sim_window, no window action headless ([presentation.md, The DirectDraw full-screen switch](../reverse-engineering/presentation.md#the-directdraw-full-screen-switch)); live 0 in all suites |
| `0x46658c`, `0x4665c4`, `0x45f13c` | inlined in `fullscreen_switch` | `0x4667c8` is their only caller; the DirectDraw creation, the surfaces and the cockpit redraw run in present.c ([presentation.md](../reverse-engineering/presentation.md#the-directdraw-full-screen-switch)) |
| `0x496952`, `0x496994`, `0x4969a6`, `0x4969c4`, `0x4969d0`, `0x4969dc`, `0x4969fa`, `0x496a54` | served inside `fullscreen_switch` | GetSystemMetrics, SetWindowPos, ShowCursor and the other window calls of the switch; the live counts of `0x496952`, `0x496994` and `0x4969fa` (14/40) come from the original's other callers, which own code serves elsewhere |

## Remaining virtual targets

This section covers the SIM, R3D and FILE entries of the `sim_gap.md`
section "Virtual: targets of the function-pointer tables that sim/src names"
that have neither a row nor a hook. None of them gets a row; each is refused
below. The HUD entries of the same section are in [hud.md, "The HUD's
remaining virtual targets"](../reverse-engineering/hud.md#the-huds-remaining-virtual-targets).

### Reachability

In es2sim, a table slot is reached only when own code (sim/src, rows
included) calls that slot on an object that carries the table. The reading
covers every slot call in sim/src:

- the helpers `vcall` (load.c, view.c), `vcallr` (fill.c), `VCALL`
  (scene.c), `fvcall` (services_files.c), `HVCALL`/`hud_vcall`
  (hud_internal.h), `WSLOT0` (hud_display.c) and `port_call` (display.c);
- the raw `r32(r32(x) + off)` calls (audio.c, gfx.c, hud_draw.c, mfd.c,
  present.c, shape_draw.c).

The own deletes with flag 3 through `+0x18`:

| Site | Object |
|---|---|
| cockpit.c `drop_release` | the intro palettes (GLPalette) |
| dialogs.c `owned_free` | the dialog's bitmap `o+0x30` and the inner object of the GLCursor holder |
| fill.c `gl_holder_destroy` (`0x4835c8`) | the holder's inner object |
| hud_cockpit.c `destroy_shape` | the cockpit and impactcp shapes |
| widget_leaves.c `three_shape_destroy` | the button's three shapes |
| load.c `bitmap_array_destroy` | GLBitmapArray elements |

The holder's inner object comes from `dts_load`. A GLRectangleRegion tag
(`0x0028000c`) would first reach its factory `0x482f94`, which has no row.
The other `+0x18` calls are stream reads (load.c, audio.c, services_files.c)
or widget slots called without a flag (hud_widgets.c).

The own slot-0 calls are:

- the gadgets and dialog draws in dialogs.c;
- the widget parts B+0x17 in display.c and hud_radio.c;
- G+0x18 and G+0x1c in hud_display.c;
- the blips and bars in panel.c;
- the draw lists in scene.c;
- the tag reads in load.c.

None of these is on a light, a camera or a view object.

The Borland RTTI descriptors in CODE hold the address of every class
destructor. That descriptor is the second reference of each destructor
below, e.g. `0x40795c` for `0x40798c`. Only the RTL unwinder reads the
descriptors. es2sim's vectored handler takes a trap before any SEH walk, so
the guest records (handler `0x4967f0`) never run (§4.4, item 4).

### Refusals

| VA | Table (slot) | Reason | Evidence |
|---|---|---|---|
| `0x40798c` | `0x497adc` (0) | light destructor never called | The only writer of the table is `0x406e8a` (load.c `light_table`), and the light at T+0x328 is embedded, never deleted. Own code calls lights only at slot 1 (`0x48bcec`, view.c) |
| `0x420730` | `0x49a494` (6) | destructor never called | Mounts are built by load.c `mount_ctor` (`0x42067c`), and no `+0x18` site deletes them. The other reference, the push at `0x420879`, is in the element destructor `0x420867`. That destructor is passed to `_vector_new_` `0x496630` at `0x42057f` (`0x420560`), and the RTL calls it only while unwinding. RTTI `0x4206f1` |
| `0x403020` | behind `0x420730` | dead with `0x420730` | Its other callers (`0x4111a3`..`0x4119f2`, `0x420748`) are not own paths; `sim_gap.md` lists only the root `0x420730` |
| `0x4275fc`, `0x427714` | `0x49aaa0` (1, 4) | store closure | The GridShape's stored sizes and write. Only `0x47a55c` and `0x47a1ac` call them (below). Own code calls the grid only at slot 3 (`grid_new`, load.c) |
| `0x42786c` | `0x49aaa0` (6) | destructor never called | No own `+0x18` site deletes the grid `[VA_GRID]`. RTTI `0x427840` |
| `0x462107`, `0x4621a2` | `0x49f8c0`, `0x49f8d4` (0) | destructors never called | The spot light and the light base (load.c, view.c). Own code calls them only at slot 1. RTTI `0x4620db`, `0x462172` |
| `0x462254` | `0x49f8e8` (0) | destructor never called | View objects over a camera (dialogs.c `L`, hud_cockpit.c `k`) are torn down inline. RTTI `0x462230` |
| `0x4715ef` | `0x4a0ca0` (2) | stack object, torn down inline | The only writer, `0x46bd54` (`0x46bc44`), builds `{0x4a0ca0, word 7}` at ebp-0xc for `0x42e8e8` and `0x48d5c0`, then drops it with the counter; gouraud.c does the same. `0x42e8e8` (row `shade_ranges`) calls no slot of the object. RTTI `0x4715b1` |
| `0x48302c` | `0x4a3264` (3) | read slot of an object that is never loaded | GLRectangleRegion (tag `0x0028000c`, GL registry `0x4a2eb8`) is read only after its factory `0x482f94` (entry `0x4a2f04`), which has no row. Own code builds regions by constructor (hud_cockpit.c view regions and `region_ctor`, hud_gauges.c, dialogs.c `dlg_ctor`). A static byte scan finds the tag 13 times in SIMVOL0.VOL. Every hit lies inside other data with an implausible length; none is at an object header |
| `0x483048` | `0x4a3264` (4) | store closure | The write slot; only `0x47a1ac` calls it |
| `0x48326d` | `0x4a3264` (6) | destructor never called | No own `+0x18` site deletes a GLRectangleRegion. The regions are embedded in widgets and dialogs or kept in the view-region arrays. RTTI `0x48322d` |
| `0x4833f4`, `0x483414`, `0x483458` | `0x4a3340` (1, 2, 4) | store closure | The GLCursor holder's sizes and write. Own code calls the holder only at slot 3 (row `holder_read`, `0x48342c`) and slot 6 (`gl_holder_destroy`) |
| `0x483790` | `0x4a3040` (6) | base table overwritten | Every writer of `0x4a3040` writes `0x4a306c` next: `0x481586`, gfx.c `rd09_bitmap_construct`, hud_load.c `hud_bitmap_init`. The exception is the map icon's stack object (`0x44f321`, hud_draw.c), which gets `0x49d984`. RTTI `0x483748` |
| `0x483924` | `0x4a3390` (6) | base table overwritten | Each of the 21 writers of `0x4a3390` writes the derived table next: `0x4a3264` in 20 of them, `0x4a33b8` in `0x4837c0`. Own code does the same. RTTI `0x483875` |
| `0x48d94d` | `0x4a5b29` (0) | destructor never called | The camera class (rows `camera_ctor` `0x48bd84`, `camera_main_ctor` `0x48bda4`). No own slot-0 call is on a camera. RTTI `0x48d921` |

**FILE, the save closure.** `0x47a1ac` saves an object and `0x47a55c`
returns its stored size. In the original, only the S/W slots call them:

- `0x482e24`, `0x482ef7`, `0x4833fe`, `0x483479`;
- `0x48e96f`, `0x48ebcc`, `0x48fe39`, `0x48ff47`;
- `0x491b30`, `0x491d43`, `0x492209`, `0x492290`.

See [SIM, R3D and HUD gap roots](#sim-r3d-and-hud-gap-roots-as-rows-and-verdicts), S/W.

In es2sim, the rows `gl_bitmaps_size_a` (`0x482e08`) and `gl_bitmaps_write`
(`0x482ed0`) in fill.c call them. Those rows are slots 1 and 4 of the
GLBitmapArray table `0x4a32ec`. No own code calls `+4` or `+0x10` on a
GLBitmapArray:

- own `+4` calls go to panel and HUD gadgets, dialogs, streams and scene
  objects;
- own `+0x10` calls go to dialogs, gauges, scene objects and the stream
  detach.

Such a call would therefore end at the refusal of `0x47a55c` or `0x47a1ac`.
These functions sit behind the two:

| VA | Called from | Evidence |
|---|---|---|
| `0x47a638`, `0x47a798` | `0x47a216`, `0x47a252`; `0x47a233`, `0x47a269` | all inside `0x47a1ac` |
| `0x47c284` | `0x47a202` | inside `0x47a1ac`; builds the closure's writer (table `0x4a1ab0`) |
| `0x47c2c4` | `0x47c265`, `0x47c2a1` | inside `0x47c248` (not referenced at all) and `0x47c284` |
| `0x47c314` | `0x47a23f` | inside `0x47a1ac`; slot 0 of `0x4a1ab0` |
| `0x47c360` | `0x47c2ae` | inside `0x47c284` |
| `0x47c388` | `0x47c32d`, `0x47c368` | inside `0x47c314` and `0x47c360` |
| `0x47c570` | `0x47c37d`, `0x47c46f`, `0x47c4d7` | inside `0x47c360` and the `0x4a1ab0` slots `0x47c444`, `0x47c484`. Only the closure writes `0x4a1ab0` (at `0x47c256`, `0x47c292`, `0x47c326`) |

The evidence is the `llvm-objdump` listing of the reference DBSIM.EXE: the
absolute and rel32 references, and every slot call in sim/src. None of these
functions has a live hit in the map. This is a static reading; it has not
been checked in vitro or live.

### Shared callees

Targets of other tables also call `0x490fa0` (`0x49aaa0` slot 5, the dump
base) and the base destructor `0x48bcc4` (row in `view_transform.h`). Those tables
cover them, so this section does not.

### Open

- The GLRectangleRegion tag scan reads SIMVOL0.VOL as raw bytes. A parse of
  every loaded member would turn the scan into a proof.

## _time in es2sim

**Contract.** `0x4967fc` is `jmp [0x6cc344]`, CW3220's `_time`. Its one
caller is the `-D` demo pick `0x45ceb8`: at `0x45cf8d..0x45cfa4` it passes
`&t` (`lea edx,[ebp-0x30]`), drops the argument, reads the stored `t` (not
EAX), sign-extends the demo count word `0x4d26f0` and takes `t % count`
(`cdq; idiv`) as the index into the word `0x49f034`. The pick does not seed
the RNG. `_time` itself is GetLocalTime, the seconds since 1970 plus
`_timezone`, minus 3600 when `_daylight` is set and the US rule says summer,
and -1 for a value ≤ 0. In core this is `rtl_io` `ES2_IO_TIME` over the OS
callback `ES2_IO_OS_GET_LOCAL_TIME`, compared in vitro by the `rtl_io` cases
(`rtl_io_cases.h`).

**Row.** `file_time` (`sim/dispatch/services/rtl_time.h`,
`es2_sim_service_file_time` in `services_files.c`) calls `ES2_IO_TIME` on the
files model. It refuses when the file services are not set up, or when the
clock answers a SYSTEMTIME with month or day 0.

**Clock.** Time enters only through the files' OS callback:

| Run | Callback | Value |
|---|---|---|
| headless, journal replay, or `[run] time` set | `es2_sim_files_os_headless` | `[run] time=<0..2147483647>`, else `ES2_SIM_FILES_TIME_DEFAULT` 852076800 (1997-01-01 00:00 UTC; `% 3 = 0`, DEMO1 of the CD's three) |
| windowed, not a replay, no `[run] time` | `es2_sim_files_os_wall_clock` | GetLocalTime; `_tzset` over the process environment (TZ or GetTimeZoneInformation) |

`es2_sim_files_init` runs `_tzset` without TZ, so the headless zone is UTC
(zone 0, no summer) and the headless value is exactly the configured seconds
(`es2_sim_files_set_time`, civil date from days). main.c prints the choice as
`es2sim: _time clock: …`. The journal does not carry the clock (the proxy
does not journal CW3220's GetLocalTime), so a replay of a `-D` capture picks
its demo through `[run] time`.

**In vitro.** `differential_gap_roots_shell_test` serves `0x4967fc` after the seal with
the own row (`gap_roots_shell_cases.h`); 2000 cases identical, coverage "demo time"
321 and "demo negative time" 117.

**Tests.** `//sim:rtl_time_test`: refusal unbound and without a clock
context; the default (stored and returned, divisible by 3); a NULL argument;
16 fixed and 2000 random seconds exact against FileTimeToSystemTime; 0 gives
-1; the wall clock against a GetLocalTime bracket, its zone equal to
GetTimeZoneInformation, other OS calls delegated, and the row within an hour
and two seconds of the host `time()`. The mutation `+ 2u` for the day in
`es2_sim_files_set_time` fails it.

**Headless `-D` runs** (CD install, `-eggplant -X2 -R0 -G -D`, to the end):

| `[run] time` | `0x49f034` | Result |
|---|---|---|
| default 852076800 | 0 (DEMO1) | 920 ticks, exit 70: the ninth pending press of the GeneralAlert's release list is refused at `0x4530bc` (the original faults at `0x453127`; [hud.md, "The ENERGY pod gauge creator 40e40c"](../reverse-engineering/hud.md#the-energy-pod-gauge-creator-40e40c)) |
| 852076801 | 1 (DEMO2) | 947 ticks, exit 6 (demo end, WinMain returned) |
| 852076802 | 2 (DEMO3) | 3349 ticks, exit 6 (`0x45a810` read all 3660 tape entries) |

### Open

- The wall clock is not compared live: no windowed `-D` run of es2sim or the
  original has been recorded against it.
