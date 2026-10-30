# The own `dbsim.exe`: the `es2sim` host

`es2sim.exe` runs our own code for DBSIM without any original instruction
executing: the guest address space, the heap, the dispatch from original
addresses to own code, the stack residue that callees read, virtual time, the
input journal, the log writers and the directory `sim/` with its targets.
Sections 1 to 13 are the host's design; the sections after them are the
contracts of the pieces built on it. The component README is
[sim/README.md](../../sim/README.md).

**Sources.** The reference `VER95/DBSIM.EXE` (724,512 bytes,
[reference_version.md](../reference_version.md)) and its
`llvm-objdump -d -M intel` listing; the registry
`tools/reference-game/proxy/hook_registry.h`; the DBSIM maps
([dbsim_map.md](dbsim_map.md)): the baseline map
(every hook original, 40 suites, armed at the mission-load call) and
the current map (every hook replaced); [dbsim_gap.md](dbsim_gap.md),
[sim_gap.md](sim_gap.md), [rtl_imports.md](rtl_imports.md) (the heap, the
runtime lifecycle), [hook_frontier.md](hook_frontier.md#bridged-call-inventory),
[tick_log.md](tick_log.md), [input_journal.md](input_journal.md),
[oracle_regression.md](oracle_regression.md), the subsystem documents in
`docs/reverse-engineering/` and the in-vitro
[README](../../tools/reference-game/invitro/README.md).

**Current check.** Live 42/42 suites
identical to their goldens, own-sim replay 42 IDENTICAL, frame compare PASS 40
and SKIP 2.

**Labels.** **static** = read from the listing, not
run; **in vitro** = the original's machine code ran in the in-vitro
harness and the own code gave the same result (not checked live unless said);
**in-process** = checked by a `//sim` test on the real image (that confirms
our host, not the original); **live** = measured on the original under the
proxy; **decision** = a design choice of this document with its reason;
**hypothesis** = unchecked; **open** = section 13 or the topic's open
points.

## 1. What the host provides, and what it reuses

| Seam | Existing code reused | In the host |
|---|---|---|
| Guest image | `invitro/startup_reservation.h` (claim `0x400000` before the CRT), the harness's link options | built from the own tables ([es2sim without DBSIM.EXE](#es2sim-without-dbsimexe)); CODE never executable |
| Guest memory view | `es2_guest_memory` ([guest.h](../../core/include/es2/core/guest.h)); the adapters' `math_native_*` accessors | the checked accessor in es2sim, the identity in the proxy (section 2.4) |
| Own code per original function | the adapters (`proxy/*_hooks.h`) through `//tools/reference-game/proxy:hook_adapters` | dispatch entries of kind *hook* (section 4) |
| Calling conventions | `//hook`: `es2_hook_extension` (ABI, result kind, `replacement_context` with entry EAX/EDX/ECX) | the same contract |
| Heap | `core/src/rtl_heap.c`, `proxy/rtl_heap_own.h` | `//sim:heap` (section 3) |
| Runtime start and exit | `core/src/rtl_start.c`, `rtl/cw3220.c` | the CW3220 import services (section 6) |
| Virtual time, journal, tick log, watches, snapshot | `proxy/virtual_time.h`, `input_journal.h`, `tick_log.h`, `mem_watch.h`, `early_arming.h` | the shared `//sim/oracle` libraries (sections 7 to 9) |
| Acceptance | `oracle_regression.py`, `after_load_snapshot.py`, the goldens in `es2-data\oracle` | section 10, [The headless acceptance](#the-headless-acceptance) |
| Files | `formats/`, `core/src/vfs_stream.c`, `core/src/rtl_io.c` | the file services (section 6) |

## 2. The process and the guest address space

### 2.1 Process model (decision)

- **i686 Windows executable** (`//bazel/platforms:windows_x86`), linked like
  the harness (`--image-base,0x10000000`, `--disable-dynamicbase`,
  `--stack,0x1000000`): Windows places the initial 16 MiB stack at
  `0x00400000`, and `startup_reservation.h` claims the image range in place
  with a `PAGE_NOACCESS` barrier page at `0x006d8000`. The initial thread
  starts the simulation thread and joins it.
- **One simulation thread.** `//hook` requires every hooked call and install
  on one thread; the adapters keep counters without locks.
- **x87 control word `0x1332`** at the start of the simulation thread, as
  CW3220's `__fpreset` `0x413aac` sets it
  ([rtl_imports.md "The runtime lifecycle"](rtl_imports.md#start-up)).
- **DEP on** permanently (the image is NX-compatible; otherwise
  `SetProcessDEPPolicy` with ATL thunk emulation off). A jump into CODE faults
  (section 4.4).

**Why identity and not an offset mapping (decision).** The adapters assume
guest = host: they pass `math_native_memory` (whose `read32` dereferences the
address) to the core, take host pointers as `(BYTE *)u + rva` and read guest
objects directly. An offset mapping would mean rewriting every adapter; a
64-bit host is therefore not part of this design.

### 2.2 Address map (the image; a decision for the rest)

No column below comes from a file: the host builds the image from the own
tables ([es2sim without DBSIM.EXE](#es2sim-without-dbsimexe)).

| Guest range | Content | Protection |
|---|---|---|
| `0x00000000..0x000fffff` | never used (`pool_next` `0x471c18` takes a debug path for nodes below `0x100000`) | no guest object |
| `0x00400000..0x00400400` | PE headers, zero | read-only |
| `0x00401000..0x00497000` | CODE: the seven CODE_TABLE runs, every other byte zero | **never executable; read-only on the seven table pages, no access elsewhere** |
| `0x00497000..0x004a7c00` | initialised DATA (68,608 bytes): the own runs, equal to the file's bytes | read-write |
| `0x004a7c00..0x006cc000` | BSS, zero | read-write |
| `0x006cc000..0x006cd000` | `.idata`; IAT `0x6cc2d4..0x6cc504`, 134 slots set to refusal traps | read-write |
| `0x006cd000..0x006d8000` | `.edata`, `.reloc`, `.rsrc`, zero | read-only |
| `0x006d8000` | barrier page | no access |
| `0x1ff00000..0x20000000` | the guest stack of `es2_machine_call` (section 4.4), the FS:0 sentinel record `{0xffffffff, 0}` in its top 8 bytes; reserved directly below the heap's preferred base | read-write |
| heap regions | DBSIM's blocks (section 3) | read-write |
| platform regions | memory the original gets from the OS (DIB bits, sound buffers; section 6) | read-write |

The highest relocated reference into DATA is `0x6cbee8`, inside the section
end `0x6cc000` (from the section table and relocations).

### 2.3 The image: what is read and what is built

- **Compiled in, never read from a file** ([CLAUDE.md](../../CLAUDE.md) §3).
  The host writes the own runs into the zeroed reservation: all of the
  initialised DATA (vtables, the constant tables the math adapters pass as
  `u + 0xa05bc`, `u + 0xa1e3c`, `u + 0xa1c34`, the RNG seed, the keyboard map
  `0x4a10ec`, the 65 init records `0x4a79d8..0x4a7b5d`, the 26 exit records
  from `0x4a7b5e`) plus the seven CODE runs. The file path survives only in
  the testonly `//sim:image_file`.
- **CODE is data too.** Core functions read byte tables inside CODE:
  [mission_load.c](../../core/src/mission_load.c) at `0x4100fd + i`,
  `0x405366 + t`, `0x426492 + i`; the HUD readers of `0x436be2`, `0x44bb9f`,
  `0x44e1f4`; the input and file services the dword `0x4967ae`. The RTTI
  counts of `0x496406` come from the own runs; every other CODE page is
  no-access, so a stray read faults with a named line
  ([What es2sim really reads from the mapped image](#what-es2sim-really-reads-from-the-mapped-image)).
- **Built by own code, never read.** The BSS is the program's state, filled
  by the static initialisation, the start-up, the mission load and the
  frames, as in the original. The stage registry `0x4a804c` (count
  `0x497074`) is written by the init records.
- **Run-time data** (VOL members, DTS, DBM/DPL, MSN, STR, `.cfg`) is read from
  the CD and the installation through `formats/` and the VFS, when the
  original reads it.
- **Not applied: relocations.** The guest keeps the original VAs; the
  `.reloc` directory (16,850 HIGHLOW sites: 13,396 in CODE, 3,454 in DATA) is
  only an inventory.

### 2.4 The guest memory view for new `sim/` code (decision)

New code in `sim/` reads and writes guest state only through an
`es2_guest_memory` (the host's identity view in the executable, a synthetic
arena in unit tests), never by casting a guest address. That keeps the `sim/`
unit tests independent of the `0x400000` claim (they run with a synthetic
image at any address) and keeps the code to the rules of
[core/README.md](../../core/README.md). In es2sim the executable's view and the
adapters' accessors are the checked accessor `sim/src/guest_memory.c` over the
address map (web_build.md section 5); in vitro the adapters run on the proxy's
unchecked identity.

## 3. The heap

**Contract ([rtl_imports.md](rtl_imports.md) §4.3).** DBSIM
allocates only through `operator new` `0x47749c`, `new[]` `0x4774f0` (n + 8),
both `0x47747c` → CW3220 `_calloc(1, n)` when the arena is off, and frees
through `delete` `0x477548`/`delete[]` `0x47758c` → `_free`; `_realloc` at
`0x474bf2` and `0x47c55f`. The game relies on zero-filled blocks, the header
word `s|1` at `p-4` (summed into the counter `0x6bb30c`; nothing branches on
it), 4-byte alignment and `malloc(0)` = NULL. The arena (`0x476df4`, 704 KiB
in BSS at `0x604f04`) is set up only with `-l` or below 12 MB of physical
memory; no suite passes `-l`, so it is off.

**The address sequence does not matter (live).** With DBSIM's blocks
in own reservations, so that the relative layout differed from the
original's, 36 of 36 suites were identical to their goldens
([rtl_imports.md "The heap, replaced"](rtl_imports.md#the-heap-replaced)).
Pointer watches are compared decoded and no tick field holds a pointer. The
one known address test, `pool_next`'s `node < 0x100000`, only selects a debug
report.

**In the host.** `es2_sim_heap` (`//sim:heap`) runs the CW3220 heap model over
own `VirtualAlloc` reservations with the original's block layout: zero fill,
header `s|1` at `p-4`, NULL for 0, the allocator's registers
(`//sim:heap_test`). Reservations are asked for at `0x20000000` first (the
system's choice as fallback), so runs repeat byte for byte. CW3220's attach
blocks and the file services' buffers are on this heap before DBSIM's entry,
as in the original ([The heap trace](#the-heap-trace)).

**After-load snapshot.** ES2SNAP1 v2 with flag 128 (own heap) from an
`es2_heap_walk`; `after_load_snapshot.py` pairs blocks by reference, so a
different layout is no difference (mission_load.md §14).

## 4. Dispatch

### 4.1 The domain (counts, reference file and DBSIM map)

| Set | Count | Basis |
|---|---:|---|
| Pointer-referenced entries (CODE targets of relocated dwords starting `55 8b ec`) | 1,036 | the relocations; [dbsim_map.md](dbsim_map.md) |
| … live with every hook original (a map row in at least one of 40 suites) | 372 | baseline map |
| Original callees the adapters call through a bridge | 177 | hook_frontier.md "Original callees" |
| Virtual-call slot offsets the adapters use | 36 | hook_frontier.md "Slot calls" |
| Import slots | 134 | `.idata` |

A virtual call is not a separate case: the core reads the slot dword from the
vtable in DATA and calls the VA it finds, so the dispatch keys on VAs only.

### 4.2 Entry kinds (decision)

| Kind | Implementation |
|---|---|
| *hook* | the registry's adapter: `replacement_context(args, u, regs)` when the extension has one, else `replacement64`/`replacement80`/`replacement`; ABI and result kind from the extension |
| *service* | host code with the same calling contract, a row in `sim/dispatch/services.h`: the runtime pieces linked into DBSIM (`0x49630c`, `0x49667c`, `0x4953c4`), the import thunks, the allocator wrappers, the clock `0x467828`, the limiter `0x4678c0`, and every original function rebuilt in `sim/` |
| *entry* | a C function over the machine (`es2_sim_entry_fn(es2_machine *, user)`): all eight registers, the return address at `m->esp`, the argument slots above it; for contracts beyond EAX, EDX, ECX |
| *headless* | a service that keeps every non-pixel write and draws nothing ([hud.md](../reverse-engineering/hud.md#the-coupling-set-measured)) |
| *refused* | stops the run: log line with the VA, the function index name and the caller's return address, exit code 70 |

Nothing ever falls back to the original.

### 4.3 Table generation

- The hook rows are generated from the registry X-macro (`sim/src/adapters.c`;
  a static assert ties the row count to `ES2_HOOK_COUNT`).
- `sim/dispatch/services.h`: the X-macro `ES2_SIM_SERVICE(name, va, abi,
  result, args, function)`; rows in topic fragments `sim/dispatch/services/*.h`,
  included by the generated `services_rows.h`.
- When the table is sealed it is sorted and checked: a VA twice, a service VA
  that is also a hook, more than six arguments, an ABI/result combination the
  hook framework would refuse.
- A refusal names the VA, the name, the caller's return address and frame
  and, for CODE, the `docs/functions/` page (`//sim:trap_refusal_test`).
- `//sim:table_test` on the reference image: every VA lies in CODE and has
  entry evidence (a relocated dword or a rel32 call or jump), with one
  exception, **`heading_sin` `0x47e027`**, which nothing references
  statically (it starts with `enter 0, 0` right after `heading_cos`'s `ret`
  at `0x47e026`): hypothesis, reached by a way the scan does not see, or dead.

### 4.4 How a call reaches the dispatcher

1. **Own C code** calls `es2_sim_call(dispatch, va, slots, count, regs,
   &result)` (`regs` = EAX, EDX, ECX), or `es2_machine_call(machine, va, abi,
   args, count)` with all eight registers (item 5). `es2_sim_call` wraps the
   sim backend: ESP at `slots - 4`, return address 0, EBX, EBP, ESI, EDI 0;
   the slot below the arguments is neither read nor written.
2. **Adapters and core through their bridges.** The plain calls and the
   stack and register bridges are machine calls of item 5 through
   `tools/reference-game/proxy/machine_call.h`; in es2sim the sim backend runs
   the VA's row ([Stage 2: the hot VAs](#stage-2-the-hot-vas)). **The trap is
   a guard:** CODE is not executable, so a call site that still jumps to a
   CODE VA or an import-slot trap faults; a vectored handler refuses it with
   the reason `unconverted call`, the return address and ESP, then exit 70.
   No original instruction runs (`//sim:trap_refusal_test`). Every replay of
   `//sim:replay_all` counts 0 trapped calls.
3. **Entry shims.** 110 hooks are entered under the proxy at an entry shim
   that reads the caller's registers and stack (the emitter adapters of
   `0x40ea58` and `0x40ec64` read a residue record through ECX and move ESP to
   it). With the direct path, `c3_09-loadout-b` dies at tick 1390 with a
   corrupt ESP ([The entry shims](#the-entry-shims)).
4. **FS:0.** 35 adapter headers (and `vfs_hooks.h`) restore FS:0 to the head
   that cores name after the original's `0x49630c` frame records, through
   `es2_machine_fs0` / `es2_machine_set_fs0`: under the proxy and in vitro the
   real `%fs:0`, in es2sim a variable that starts at the sentinel record
   {next `0xffffffff`, handler 0} at the guest stack's top. es2sim never
   writes the real FS:0. **No guest code sees the sentinel (static):** CODE
   has 333 stores to `%fs:0x0` and one read, `0x49633b`, whose value becomes
   the new record's `next`; a chain walk happens only in CW3220's
   `__ExceptionHandler` (thunk `0x4967f0`), which DBSIM never throws to
   ([rtl_imports.md](rtl_imports.md), "No throw"). **Trap:** the `%fs:0x4`
   reads (StackBase, `0x401026` and 14 sites in `0x496358..0x496731`) take
   Borland's per-thread data at `[StackBase-8]`, so es2sim passes the c0w32
   entry a StackBase away from the guest stack (`es2_sim_os_stack_base`); one
   at the guest stack's top would put that word on the sentinel
   ([web_build.md](../web_build.md) section 5).
5. **The machine interface** (`es2/hook/machine.h`;
   [web_build.md](../web_build.md) section 4.2). `es2_machine_call` has two
   backends. The x86 one (`hook/machine_x86.S`) loads the registers and calls
   the VA. The sim one (`sim/src/machine_sim.c`) never jumps: it pushes the
   slots and the return sentinel `0xfffe0000` onto the guest stack, runs the
   VA's row (a row with an entry shim runs the shim first) and pops per ABI.
   Refused (exit 70): an unlisted VA, fewer slots than the row reads, a stack
   cleanup that is not the ABI's, more than 16 arguments or plants, no guest
   stack. A push below the stack's limit is a model error (exit 71). The sim
   backend has no x87 state: a row of the ST0 result kind is refused when the
   table is sealed (`//sim:table_test`). `//hook:machine_test` compares both
   backends per ABI and result kind.

**Rejected alternative: a stub image** (executable CODE with a jump at each
entry): a call to an unlisted entry would run original code silently, and
four entries lie less than five bytes before the next (`0x425344`,
`0x463370`, `0x4650cc`, `0x467ad8`).

**Cost.** A call carried by a fault and a trampoline costs about **11.58 µs**
under WOW64 (against 2.4 ns for a direct call and 28 ns for `es2_sim_call`).
At about 3,200 such calls per frame windowed and 4,000 to 5,000 headless that
is 37 to 58 ms per frame, more than the limiter's 40 ms; hence stage 2, and
the trap carries no calls (`//sim:host_real_test`; per-frame counts in
[es2sim's frame times](#es2sims-frame-times)).

### 4.5 Refusal and the log

The host keeps per VA a call count and writes the counts in the tick log's
exit record; a refusal writes
`{"event":"refused","va":…,"name":…,"return":…,"frame":…}` to the tick log,
then the exit record (reason `refused`), and ends the process with 70. A
unconverted call (the trap's refusal) writes the exit record before the trap
handler ends the process.

## 5. Stack residue as modelled values

Some original callees read stack bytes they did not write. Under the proxy
the entry shims give them the original's bytes; in the host no original frame
exists, so each reader gets a **modelled value**. `sim/src/residue.c` holds
the table as data: reader VA, offset or register, width, rule (`zero`,
`writer`, `constant`) and the evidence line.
A rule other than `zero` needs the writer's contract from the bytes. The host
stack's own bytes are never a residue source: they depend on our compiler.

**The zero model checked (static).** Where the original writers could be
found in the listing, the residue was traced to writer and consumers: the
sound table init `0x46254c` and the palette apply `0x465e28` read residue
that changes nothing, the shade ranges bug `0x42e8e8` is never reached, the
register rows and the popped locals of `0x451de0`/`0x45c964` are dead. No
rule other than `zero` is needed. Not disproved, but with no reader named:
the `aim_fire` padding ([ai.md](../reverse-engineering/ai.md#stack-residue-under-aim_fire)),
the `group_order` word
([input.md](../reverse-engineering/input.md#stack-residue-under-the-command-handler)),
the configuration, trigger and loader locals, and the 4096 zeroed bytes below
the entry shims ([The entry shims](#the-entry-shims)).

### The emitter's excluded word

The impact exclusion dword of the `aim_fire` row has a named writer (static,
listing): the preparation `0x40e788`'s last call `0x47e0b0` begins with
`enter 0,0`, which stores `0x40e788`'s EBP (E-4, E its entry ESP) at E-90h;
the emitters `0x40ea58` and `0x40ec64` call the preparation and the impact
`0x40bf74` from the same ESP, so the impact's read at entry-88h is that word
([ai.md](../reverse-engineering/ai.md#stack-residue-under-aim_fire)).

The adapter (`weapons_round3_stack_call`, `proxy/weapons_hooks.h`) plants the
padding bytes at E-33 and E-65 and the current excluded word at E-90h through
`es2_machine_call_planted`, reads the word back after the call and plants it
at the impact's entry-88h. In vitro: `differential_weapons_test`, the
emitters' mixed-mode and poison cases.

## 6. Platform services: the imports

Every import call becomes a call of a host service, with one headless
implementation and a windowed one on SDL3 ([The windowed platform
layer](#the-windowed-platform-layer)):

| Imports | Headless | Own code |
|---|---|---|
| CW3220 (53): heap, strings, files, console, start-up and exit | `core/src/rtl*.c`, the heap of section 3, the file model over the working directory | `services_rtl.c`, `services_files.c`, `startup.c`, `loop.c` |
| KERNEL32 `GetTickCount` (thunk `0x4968ec`, IAT `0x6cc3e8`) | virtual time (section 7) | `services_rtl.c` |
| KERNEL32 global memory, `Sleep`, `OpenFile`, `_lopen` family, `GetPrivateProfileStringA`, `LoadLibraryA`/`GetProcAddress`, `GlobalMemoryStatus` | working directory files; `Sleep` returns at once; `GlobalMemoryStatus` answers 64 MiB (no arena) | `startup.c`, `input.c`, `audio.c` |
| USER32 window, class, pump (`PeekMessageA` `0x6cc40c`, `TranslateMessage`, `DispatchMessageA`), `GetKeyState`, `GetCursorPos`, paint, cursor | the journal (section 8); the window procedure's handler list runs as own code | `loop.c`, `input.c`, `services_cursor.c` |
| WINMM `joyGetDevCapsA`, `joyGetPosEx` | the journal or `JOYERR_UNPLUGGED` | `input.c` |
| WINMM `mciSendCommandA` (CD music) | records the call; `MCI_OPEN` fails | `audio.c` |
| GDI32 DIB section, palettes, `StretchBlt`; DDRAW `DirectDrawCreate` | a host region for the DIB bits (`G+0x4e` at `0x4d250a` points outside the heap), frame hash only | `frame_buffer.c`, `present.c` |
| `SOS9503.DLL` (97 entry points), `BWCC32.DLL` (loaded, never used) | a silent device that keeps every call and its order | `audio.c` ([dll_imports.md](dll_imports.md)) |
| MSVFW32 `ICSendMessage` | no running caller ([The AVI code in DBSIM and es2sim](#the-avi-code-in-dbsim-and-es2sim)) | refused |

The import slots stay refusal traps; own code calls the service directly,
never the slot.

## 7. Virtual time

**Contract (timing.md).** DBSIM reads `GetTickCount` only through
`0x4968ec`, from three sites: the clock wrapper `0x467828`
(`(value − [0x4d3b78]) >> 4`), timing init `0x467840` (call at `0x467883`:
base `0x4d3b78`, previous clock `0x4d3b74`) and the limiter poll at
`0x4678d0` (return address `0x4678d5`). A limiter poll gets `previous +
step`, every other call the current clock; with `virtual_time_intra_frame=1`
the clock advances 1 ms per further call in a frame, at most step − 1 times,
then the stall guard (default 100,000 calls). Tape playback skips the
limiter; the frame boundary gives the missing step. The registers each site
leaves: [The host skeleton](#the-host-skeleton).

**In the host.** The state machine is the pure library
`//sim/oracle:virtual_time` (`es2_vt_limiter_poll`, `es2_vt_read`,
`es2_vt_frame_boundary`); the proxy's `proxy/virtual_time.h` calls it and
keeps the INI configuration and the wall-clock pacing. The host's three call
sites name the caller (`ES2_VT_LIMITER`, `ES2_VT_INIT`, `ES2_VT_CLOCK`)
instead of the return-address test. Parameters come from the suite's launch
report (step 47 ms for recorded suites, start 16,777,216, intra 1, pace 0
headless). The frame counter `[0x4d257b]` is the key the boundary and the
journal share.

## 8. The journal provider

**Contract (input_journal.md).** Per frame (`[0x4d257b]`) the
journal injects its messages before real ones; a message with pump key `p` is
injected only after `p` earlier `PM_REMOVE` peeks in that frame;
`GetKeyState`, joystick and cursor answers come from the journal, else from a
virtual key state, `JOYERR_UNPLUGGED`/`JOYERR_PARMS` and (320,240); focus
messages are never seen in replay.

**In the host.** `sim/oracle/journal.c` (`//sim/oracle:journal`, pure C)
holds the parser, `ij_take`, the per-frame pump count, the virtual key state
and cursor and the counters; the proxy's `input_journal.h` keeps the IAT
wrappers, the record writer and the focus subclass. Headless there is no
other message traffic, so the peek count in a modal frame is exactly the own
modal loop's: the pump-keyed answers arrive at a fixed pass ([The modal
dialogs](#the-modal-dialogs)).

## 9. The log writers and the return files

- **Tick record** (tick_log.md schema 2): written at the frame counter
  increment of the own main loop (the original's `0x45f4ad`), after the
  frame boundary of virtual time, every field read from guest memory at its VA
  (`0x4d24bc` context, `0x4d3b64` 24 timing bytes, RNG states
  `0x4d2599`/`0x4d260b`, the player actor `[0x4d24e6]` at `+0x26`, `+0x0c`,
  `+0x28e`, `+0x290`), the `vt_*` and `in_*` counters. The start record names
  the mode `own_process_headless`; the host's own counters go into the exit
  record.
- **Watches** (`mem_watch.h` syntax and events), read through the host's
  guest view ([Oracle outputs in the host](#oracle-outputs-in-the-host)).
- **Library.** The line formatting is `//sim/oracle:tick_record` and
  `:mem_watch`, shared with the proxy, whose fixtures guard its output byte
  for byte.
- **After-load snapshot**: ES2SNAP1 v2, `//sim/oracle:snapshot`.
- **Return files** `results.dat`, `mission.var` and the prefs write are
  written by own code into the run's directory, as the mission end `0x461ff0`
  does ([file_io.md](../reverse-engineering/file_io.md)); the **exit code** is
  the word `0x4d27b8` (launcher.md).
- **Configuration:** the host reads the proxy's INI sections (`[time]`,
  `[trace]`, `[input]`, `[snapshot]`, `[run]`).

## 10. Headless acceptance

The host runs one suite from its launch set and journal and stops itself at
the suite's `frame_limit` (or at the mission's own end). The replay runs
`oracle_regression.py normalize-compare <tick-log> <out> --outputs <dir>
--frame-limit <n> --golden <golden>\normalized.json`, the comparison
`check.ps1 -Live` uses ([oracle_regression.md](oracle_regression.md)): ticks
keyed by frame (the 14 `TICK_FIELDS`), watches as a sequence of changes,
return files by SHA-256, the exit code against the golden's
`own_process_exit`. For the start-up and the load the acceptance is the
after-load snapshot ([The after-load snapshot against the
original](#the-after-load-snapshot-against-the-original)). Harness and frame
compare: [The headless acceptance](#the-headless-acceptance), [The frame
compare as tests](#the-frame-compare-as-tests).

## 11. `sim/`: layout and targets

The directory layout is in [sim/README.md](../../sim/README.md).

| Target | Kind | Content |
|---|---|---|
| `//sim:image`, `:heap`, `:dispatch`, `:residue` | `cc_library` | the seams |
| `//sim:adapters` | `cc_library` | `//tools/reference-game/proxy:hook_adapters` for the host (i686 only) |
| `//sim:services`, `//sim:host` | `cc_library` | the service rows' own code; everything above |
| `//sim:es2sim` | `cc_binary` | `es2sim.exe`; link options as the harness |
| `//sim/oracle:virtual_time`, `:journal`, `:tick_record`, `:mem_watch`, `:snapshot` | `cc_library` | portable, also linked by the proxy |
| `//sim:<seam>_test` | `cc_test` | synthetic images and tables |
| `//sim:es2sim_real_test` | `cc_test` | the real data, SKIP without `ES2_DATA_ROOT` |
| `//sim/tests:replay_<suite>_test` | test per suite | headless replay + `oracle_regression.py`, SKIP without the goldens |

**Trap:** executable and test names avoid `update`/`patch`/`setup`/`install`
(Windows' installer heuristic, error 740); every process start in a test
carries a no-window flag (`//tools:process_starts_test`).

## 12. Own code per seam

| Seam | Own code | Contract |
|---|---|---|
| claim, image, protections, DEP, FPU | `startup_reservation.h`, `sim/src/image.c`, `main.c` | sections 2.1 to 2.3 |
| heap | `sim/src/heap.c` | section 3 |
| dispatch table, kinds, trap, refusal | `adapters.c`, `dispatch.c`, `dispatch_trap.{c,S}` | section 4 |
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
| 1 | Cost of the trap per guest call, and whether stage 2 is needed | 11.58 µs per call (section 4.4); stage 2 is built ([Stage 2: the hot VAs](#stage-2-the-hot-vas)) and the trap carries no call |
| 2 | Which pointer-referenced entries a replay reaches with every hook replaced | 41 rows of the every-hook-original map do not run, among them `0x47aeb0`, `0x47aecc`, `0x47c7bc`, `0x47c7d8`, `0x47c82c` ([dbsim_map.md](dbsim_map.md#map-with-all-hooks-replaced)); the rest is the refusal log |
| 3 | Residue readers beyond section 5, and the rule for the `+28h` padding bytes | open (section 5) |
| 4 | Which drawing entries may be *headless* | the coupling set ([hud.md](../reverse-engineering/hud.md#the-coupling-set-measured)); rasterizer.md "May be headless" |
| 5 | Launch inputs and installation files a headless run needs | [Start-up, parsers and file services](#start-up-parsers-and-file-services) |
| 6 | The exit records' effects and the exit code path | nothing observable ([The 26 exit records](#the-26-exit-records)); the exit code path: [The main loop, the journal and the exit](#the-main-loop-the-journal-and-the-exit) |
| 7 | Memory the original gets from the OS besides the heap | the DIB bits are a host region that `[0x4d3020]+0xa` and the row table point into; the sound buffers are host memory from `GlobalAlloc` ([Audio](#audio)) |
| 8 | Whether the `0x49630c` records on the real FS:0 stay harmless | es2sim's FS:0 head is the sim backend's variable (section 4.4 item 4); the original's faults end the process before any walk ([The original's fault paths](#the-originals-fault-paths)) |
| 9 | Whether the proxy's switch to the shared `sim/oracle` libraries leaves every golden identical | the live suites are identical with the switched proxy |

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
  kept. The service links on `es2_machine_fs0` (in es2sim the sim backend's
  variable, section 4.4 item 4) and refuses while `[0x6cbee4]` is
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
and exit records is **checked in vitro**
([The static initialisation as own code](#the-static-initialisation-as-own-code));
the readers (below) are static.

### The tables

The module table `0x49702c` gives init `0x4a79d8..0x4a7b5e` and exit
`0x4a7b5e..0x4a7bfa`: **65 init and 26 exit records** of 6 bytes (byte 0 done
flag, 0 in the file; byte 1 priority; dword 2 function), every priority
`0x20`, all functions distinct. CW3220's selector runs the init records in
table order and the exit records in reverse table order
([rtl_imports.md "The runtime lifecycle"](rtl_imports.md#start-up)).
Record numbers are 0-based table indices, as in mission_load.md §4 (record 46
= `0x45caf4`, which dbsim_gap.md counts 1-based as "record 47"). The split of
the records by address range is by **address**, not by role (record 64 opens
the `DARKMONO` VxD, record 25 registers a renderer class table, record 46 is
the globals object with both RNG streams).

**What runs.** Of the 65 functions, **20 write nothing**, **7 write only zeros
into memory that is already zero**, and **38 change the image**. The executed
path reaches 33 further functions (plus the thunks of `_memset`, `_memmove`
and `CreateFileA`); dbsim_gap.md's 133 functions are the static closure,
which includes branches never taken here. The three hooks among the callees
(`matrix_ctor` `0x495132`, `rotation_ctor` `0x495194`, `rotation_base_ctor`
`0x49514f`) write nothing and never fire here under the proxy: hooks arm at
the mission-load call at the earliest (`early_arming.h`).

**Bookkeeping.** Most constructors increment the construction counter
`[[0x6cbee4]]` (a stack word below StackBase,
[rtl_imports.md §4.1](rtl_imports.md#41-borland-runtime-pieces-linked-into-dbsim)),
and records with destructible statics link and remove a frame record
(`0x49630c`). The counter does not reach the image, but `0x49630c` copies it
into frame records, so the own code moves it as the original does. The array
construction `0x49667c(buf, size, count, mode 1, ctor)` with a buffer calls
the element constructor `count` times and allocates nothing.

### The 65 init records

The own code is `sim/src/init.c`, one function per record VA (per record:
the stores in order, the visible bytes and the counter delta). The facts the rest of
the host relies on:

- The construction counter rises by 616 over the init records and is back to
  0 after the exit records.
- **After record 64** (in vitro) 2,454 DATA bytes differ from the
  file (480 in initialised DATA, 1,974 in the zero-filled part), among them
  the 65 done flags and 6 bytes of the c0w32 entry (`0x49706a` = `0x400000`,
  `0x4a78a0` = `0x24` when the thread's StackBase−8 held 0, `0x6cbee4`); the
  heap pointer `[0x4a08b4]` depends on the address. Record 53 adds the
  2,056-byte heap block and `[0x6bb30c]` = `0x811`.
- **The stage registry** `0x4a804c` (`0x401d64(f, s)` appends {dword f, word
  s} and increments `0x497074`) holds 31 entries: 19 stage-2, 11 stage-5 and
  1 stage-6, the 31 `call 0x401d64` sites of
  [main_loop.md](../reverse-engineering/main_loop.md#the-main-loop-around-it).
  It is fixed after static initialisation; registration order is the order
  in which `0x401d94` calls them.
- **State records (records 17, 18).** Entry k at `base + stride·k`: name
  pointer, a constant, `0x415028(+8, &w)` (the 16 bits of w as bytes 0 or 1,
  bit 0 first), the 9 dwords of `0x4998f8 + 0x24·k` (MECH) or `0x499e9c +
  0x24·k` (FLYER), MECH only word `+0x3c`. The file already holds most of
  these bytes, but the own code writes every field: a snapshot cannot tell a
  written byte from an untouched equal one. Field roles:
  [vtables.md](../reverse-engineering/vtables.md#31-mech-states-0x4993a4-stride-0x3e).
- **The globals object (record 46, `0x45caf4`).** In this order: GLBitmap
  ctor on `0x4d257f`; `_memset(0x4d24bc, 0, 0xc3)`; the field stores; the
  construction counter; then `0x492e88` on stream A `0x4d2599` and stream B
  `0x4d260b` (byte `+0x70` ← 24, `+0x71` ← 55, `_memmove(stream, 0x4a6940,
  0x70)`; [rng.md](../reverse-engineering/rng.md#initialization-state-writes-and-seed-sources)).
  The frame counter `0x4d257b` lies in the cleared range; byte `0x4d2538` is
  written 0 at `0x45cb64`.

### What the result depends on

Only on the file and the table order: every value written is a constant, a
copy of initialised DATA (records 17, 18, 46), or computed from an earlier
store (the counts `0x497074` and `0x4a1690`). Three things come from the
platform:

1. **The heap address** of record 53's block, stored at `[0x4a08b4]`; the
   after-load snapshot pairs blocks by reference, so any address satisfies
   it. `[0x6bb30c]` = `0x811` needs CW3220's header word at `p−4`. The block
   is never freed.
2. **The VxD handle** `[0x6cbc50]` (record 64). The real `CreateFileA` with
   record 64's arguments returns `INVALID_HANDLE_VALUE` (error 87) on Windows
   10 19045 under WOW64 (the in-vitro `PLATFORM` probe); hypothesis: −1 on
   every NT system. Its readers are `0x4955c4` (only with `-m`, which no
   suite passes) and exit record 25 (no `CloseHandle` for −1).
3. **The platform calls**, in order: `GetModuleHandleA` (the entry), `_memset`
   and two `_memmove` in record 46, `_calloc(1, 0x808)` in record 53,
   `CreateFileA` in record 64 (in vitro).

No init record reads the command line, a file, the clock or an RNG stream,
and none reads what a later record writes. The order matters for the stage
registry (entry order = dispatch order) and the class registry (the grid
table is pair 0).

### The 26 exit records

The contract is [rtl_imports.md "The 26 exit records of
DBSIM"](rtl_imports.md#the-26-exit-records-of-dbsim-static); its function list
matches the bytes, and every store is checked in vitro. They run after the
atexit functions, in reverse table order (record 25 first); they delete the
pools, objects and GLBitmaps the init records set up.

**Nothing observable depends on them.** They write the construction counter,
vtable words inside the destroyed statics, `[0x6bb30c]` and CW3220's free
lists; no file, no tick-log or `results.dat` value; the exit code is fixed
before they run ([launcher.md](../reverse-engineering/launcher.md#exit-code)).
Their one platform call, `CloseHandle`, does not happen for −1; `_free` is
never called when the pool pointers are 0.

### What the mission load and the frame read (static)

From the listing: every instruction with an absolute operand in a range the
init records write. 

The **simulation** (what the tick log and the watches compare) reads from
static initialisation only: the stage registry (`stage_dispatch` `0x401d94`,
core `render_root.c`), the MECH and FLYER state records (AI code, core
`ai_behaviour.c`, `damage.c`), both RNG streams, the key-direction table
`0x49eb45` (`key_axes` `0x45a4cc`), the class registry `0x6c5834` with count
`0x4a1690` (`dts_load` `0x47a144`) and some fields of the globals object.
Everything else written there feeds drawing (section 13 question 4) or
start-up code that overwrites it. The VxD handle `0x6cbc50` (record 64) is
read only by `0x4955c4` (`-m`) and exit record 25.

**The class registry.** Only the grid table `0x49aa7c` is registered by an
init record (25), so pair 0 is always the grid table. The GL table is
registered by `0x480ef4` (called from `0x464d5c` and `0x480be0`), TS by
`0x48dd08` and AN by `0x4913dc`, called by the mission load at
`0x4617ac`/`0x4617b1` (asset_loading.md "Class registry").

**Open.** The roles of `0x4d2506`, `0x4d24ca`, `0x4d2512..0x4d2516`,
`0x4d2539` and of record 30's values (readers through base pointers not
traced); the VxD result on other Windows versions.

## The static initialisation as own code

DBSIM's start-up before WinMain as own code: the c0w32 entry `0x401000`, the
65 init records, the 26 exit records. **In vitro**
(`differential_init_records_test`); not checked live.

| Piece | Where | Contract |
|---|---|---|
| c0w32 entry `0x401000..0x40106e` with `0x496425(0)` and `0x4964e2` | `es2_sim_c0w32_entry` | `[0x49705d]` ← `[0x497059]` << 2; BSS `0x4a7bfc..0x6cbee7` zeroed; `[0x497061]`, `[0x497065]` ← the dwords at StackBase−8, −4; `0x4964e2` (byte `0x497058` is 0): `[0x4a78a0]` ← `[StackBase−8]` + `0x24`; StackBase−8 ← `0x4a7884`; `[0x6cbee4]` ← StackBase−4; StackBase−4 ← 0 (the construction counter); `[0x4a7888]` ← 0; `[0x49706a]` ← `GetModuleHandleA(NULL)`. The branch for a nonzero `0x497058` is refused |
| 65 init and 26 exit records | `sim/src/init.c` | every store in the original's order, the counter `[[0x6cbee4]]` moved as the original moves it |
| the selector | `es2_sim_run_next_record` | CW3220's `0x41a634` as `es2_rtl_select_record` over the module table `0x49702c`: done flag ← 1 before the function runs; a VA without own function is refused |
| platform services | `sim/src/services_platform.c` | `CreateFileA` (thunk `0x4968fe`): −1 headless without calling the OS, recorded as a `platform` event line; `CloseHandle` (`0x4968b0`): refused; `GetModuleHandleA` (`0x496898`) with NULL: `0x400000` |

Every call the records make to other code goes through `es2_sim_call`. The
constructors that store nothing (the array element constructors of records
0, 2, 19, 21, 27, 28, 33, 52, 56, 59, 63 and `matrix_ctor` in records 2 and
63) are not called: in vitro their records change no byte beyond the done
flag.

**StackBase in `es2sim.exe`** is the simulation thread's `fs:[4]`; in the web
build the end of a zeroed static block, so `[0x4a78a0]` is `0x24` there.

**Order in the own process:** `es2_sim_c0w32_entry`, `__startup`'s part
before the records, `es2_sim_run_init_records`, `GetModuleHandleA(NULL)` and
WinMain; at exit the atexit functions, `es2_sim_run_exit_records`, then
`ExitProcess` ([rtl_imports.md](rtl_imports.md) "Exit paths").

**The in-vitro acceptance** (`init_records_cases.h`) runs the original's
`0x401000` and the 91 records machine-called against the own `init.c` from
the same start and compares after each of the 92 steps a hash of DATA, the
two words below StackBase, the heap and the selected VA, and at the end the
import-level call logs: **92 of 92 steps identical**. Not compared: CW3220's
own variables and the frame records `0x49630c` writes on the stack.

## Start-up, parsers and file services

argv, both parsers, `drive.cfg`, `error.str`, the VOL enumeration, the file
services and `-l` as own code. Contract:
[launcher.md](../reverse-engineering/launcher.md#dbsims-start-up-as-own-code).

| Piece | Where | Contract |
|---|---|---|
| file services | `sim/src/services_files.c`, `include/es2/sim/files.h`; 17 rows (`_fopen` … `_flushall`) | the runtime file model (`core/src/rtl_io.c`, in vitro against CW3220) over CW3220's variables in a runtime region of their own; CW3220's own blocks on the game's heap ([The heap trace](#the-heap-trace)); headless: the file API on the working directory, `GetStdHandle` NULL, an empty `GetStartupInfoA`. EAX as the model; ECX/EDX as at the call (no DBSIM caller reads them). A call outside the model (an `fscanf` float, …) is refused |
| `0x45ee00` | the same file | install root + path in `0x4d285c`; EAX the buffer, ECX 0, EDX the appended length + 1 |
| start-up | `sim/src/startup.c`, `volumes.c`, `include/es2/sim/startup.h` | CW3220's call of WinMain, WinMain, both parsers, the game main up to the load, the VOL registration, the fatal report, WinMain's tail (the own pump until `WM_QUIT`) |
| `es2sim --headless` | `sim/src/main.c` | DBSIM's command line from `--arguments`, else `[game] command_line`, else `-eggplant -X2 -R0 -G` plus `[game] arguments`; outcomes: WinMain returned (exit code `[0x4d27b8]`, `winmain_returned`), fatal report (1, no exit records, `fatal_error`), a refusal (70) |

**Frame area (decision).** The original's stack locals of these functions
(WinMain's 27 argv slots, the game main's `0x1c4`-byte frame, the fatal
report's buffers, the VOL-aware stream S of `0x453878`, …) live in a
host-provided area at the same relative offsets, zero-filled; no residue is
read before it is written, except the `-eggplant` message box's string
pointers when error.str has fewer strings. Call arguments are copied there
too, so the runtime models that read slots by guest address work in
`es2sim.exe` and in the tests' arena.

**The files DBSIM opens** (section 13 question 5): listed in
[file_io.md](../reverse-engineering/file_io.md). Traps: `DATA\DRIVE.CFG` is
fatal when missing and its second line from 80 characters on overwrites the
CD root; a missing `DATA\PREFS.CFG` makes init record 46's display flag ask
for DirectDraw, refused headless; a VOL name already registered from the
install is skipped on the CD.

**Acceptance.** In vitro `differential_startup_test` (`startup_cases.h`:
random command lines through the split and both parsers; string holders;
the VOL registration `0x473260`; CW3220's handle flags and FILE array).
Unit `//sim:startup_test`; `//sim:es2sim_real_test` (exit codes 1, 0 and 1
of launcher.md's first three rows).

**Open.** The DirectDraw switch is refused, not modelled. The window failure
path (`LoadStringA` 1000/1001 and the message box) is not reachable headless.

## The main loop, the journal and the exit

The game main and loop, the pump over `//sim/oracle:journal`, MainWndProc's
handler list, the limiter and clock calling section 7, the mission end, the
exit code and the DIB region. The contract of the own code is in
[main_loop.md](../reverse-engineering/main_loop.md#the-main-loop-pump-window-procedure-limiter-and-clock-as-own-code);
in vitro (`//tools/reference-game/invitro:differential_loop_test`).

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
in vitro (`//tools/reference-game/invitro:differential_input_layer_test`),
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
differs) and returns out + 6 from its copy path (in vitro,
`differential_view_test`). Headless display answers, the blit and present
without pixels and the terrain shading without its renderer-only tables are
platform deviations (mission_load.md section 33, "Headless decisions").

## Audio

The audio code the mission load and the frame reach runs as own code; the SOS
driver DLL is replaced by a device seam. Contract:
[audio.md](../reverse-engineering/audio.md#audio-in-the-own-process);
in vitro (`//tools/reference-game/invitro:differential_audio_test`),
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
in vitro (`//tools/reference-game/invitro:differential_view_test`,
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
in vitro (`differential_dialogs_test`: alert, options, rows and
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
in vitro
(`//tools/reference-game/invitro:differential_shape_draw_test`,
`shape_draw_cases.h`, `--shape-draw`: the original's machine code against the
own rows on synthetic shapes; registers, DATA, the world and the stubs' call
log compared), unit `//sim:shape_draw_test`.

| Piece | Where | Contract |
|---|---|---|
| the shape draw services | `sim/src/shape_draw.c`, `include/es2/sim/shape_draw.h` | the hooks (`matrix_compose`, `euler_matrix`, `angle_ratio`, `heading_cos`, `multiply_q10`, `dist3d_delta`, `matrix_point`), the view rows, the fill layer, the haze table, the shade lookup and every part's virtual slot are called through the dispatch |

## Files, streams, math and runtime leftovers

The leaf methods of the three stream classes, the arena set-up and block
test, the pure math leaves between `0x47db4d` and `0x480590`, `0x492b04`, and
`0x46b850`, as service rows (`sim/src/services_rtl.c`, rows in
`sim/dispatch/services/runtime_leaves.h`; the stream rows in
`services_files.c`; the grid cell counter `0x470db8` in `terrain_draw.c`),
exact headless. **In vitro** (`differential_runtime_leaves_test`,
`runtime_leaves_cases.h`: random entry EAX/EDX/ECX; registers, DATA, the heap
and the stub log compared).

Every row leaves EAX, EDX and ECX as the original does. The traps:

- **Stream classes.** Base `0x4a16e8`: `+4` status. Read-from-handle
  `0x4a1738`: `+4` status, `+8` handle word (`0xffff` none), `+0xa` open
  byte. Buffered `0x4a1ad8` (unaligned dwords): `+4` status, `+8` inner
  stream, `+0xc` size, `+0x10` position of the buffer's first byte, `+0x14`
  buffer, `+0x18` owns-buffer byte, `+0x19` valid bytes, `+0x1d` cursor.
  Status 6 is "detached" (`0x47c818` is `status != 6`; `0x47c7bc` returns 0
  then).
- **The refill `0x47c8c4`** seeks the inner stream (slot +8), reads `size`
  bytes through slot +0x18 and on a false read asks the inner status (slot
  +4; `2` is the end): at the end the valid count becomes `tell - position`
  (clamped), otherwise the buffer is marked failed (`0x47c8b0`).
- **The buffered read `0x47c82c`** loops with no round cap; ECX leaves as the
  running destination (the prologue's `push ecx` is the local `[ebp-4]`).
- **The detach thunks `0x47b5cc` / `0x47b9cc`** call slot +0x10 of the
  object's own table.
- **`0x470db8`:** the index is `(p - [obj+0xec]) >> 4`, a logical shift.
- **`0x47ddcd`:** the sign is `SF != OF` of the last `adc`. **`0x47df9c`:**
  includes the `jcxz` on CX and shift counts `CL & 31`; for `n == 0` ECX is
  the entry value.
- **`0x476e2c`** compares signed.
- **Operand aliasing:** cross product, affine map and the 64-bit divide read
  and write in the listing's order when an output aliases an input.
- **Divide faults.** `0x47de2d`/`0x47eabe` and `0x492b04` end the process
  with the original's exception code
  ([The original's fault paths](#the-originals-fault-paths)).

**Verdicts.** `0x47dba4` has no static reference: dead code like
`0x47e027`, kept as a row and the second known exception in
`//sim:table_test`. `0x47a1ac` and `0x47a55c` are reached only through the
bitmap array rows of `fill.c` ([Remaining virtual
targets](#remaining-virtual-targets)).

**Open.** The writer paths behind `0x482ed0`, the archive reader pieces and
the write-stream slots.

## Oracle outputs in the host

The `[trace]` memory watches are one library, `sim/oracle/mem_watch.{h,c}`,
for the proxy and `es2sim`. The proxy (`proxy/mem_watch.h`) keeps its names
as wrappers over the library and its own checked copy (ReadProcessMemory,
LastError restored); `mem_watch_differential_test`, `_random_test` and
`_benchmark_test` compare it with the frozen baseline
`tests/mem_watch_baseline.h`. The host (`sim/src/host.c`) reads `[trace]
watch_count`/`watchN` with the proxy's rules (at most 48 entries, each under
255 printable characters) and writes the watch events after each tick record
(frame = the dword `0x4d24bc+0xbf` before its increment); the copy is a
VirtualQuery `readable` check plus a copy through `es2_guest_memory`
(`//sim:host_watch_real_test`). `replay_suite.py` writes the suites' watches
into the INI; `--after-load-snapshot` adds the `[snapshot]` section
(mission_load.md section 14).

**Host behaviour (decision):** a malformed or refused watch fails the set-up
with exit 67 and `[trace] watch refused`, before the tick log is created
(the proxy refuses its whole tick log, `tick_log_watch_refused`).

## The windowed platform layer

`es2sim` runs in a window: the 8-bit frame through the present path, input
from SDL3 through the input layer's seam; focus, close, size and the cursor
follow the window procedure's contract (launcher.md "DBSIM's process shell,
start-up and exit", flyer.md "MainWndProc 0x466034").

- `sim/src/platform_sdl.c` (`//sim:platform_sdl`, SDL3 and winmm) produces
  only what Windows would hand the own code (messages, key states, the client
  rectangle, the cursor position, the joystick answers) and shows what the
  present drew. The pump `0x464c78`, the window procedure `0x466034` and the
  input functions stay own code.
- **The loop seam** `es2_sim_loop_window` (`loop.h`), NULL headless, with the
  hooks `peek`, `set_cursor`, `sleep` (the pause path's `Sleep(100)`),
  `clock`, `pace` and `def_window_proc`.
- **`//sim:es2sim_window`** (`src/main.c` with `ES2SIM_WINDOW`): without
  `--headless` it sets the window layer, computes the window rectangle
  `0x4d36cc` from the system metrics and opens the window right after the
  DIB section. A windowed run ignores the configuration's journal replay.
  `//sim:es2sim` does not link SDL3.

**Behaviour per message.** Traps:

- The class icon `es2icon` (`0x465111`) is set with `WM_SETICON` before the
  window is shown.
- Focus messages pass on only alternating (`guest_focus`): a late first
  `WM_SETFOCUS` would make the resume `0x463238` start every sound row.
- `WM_PAINT` is retrieved after the queued input and never while `WM_QUIT`
  is posted.
- Close and Alt+F4 reach the quit dialog `0x45feb8(0x410)` through
  `DefWindowProcA`; SDL's own Alt+F4 handling is off.
- Keys: SDL scancode → own set-1 table → `MapVirtualKeyW(MAPVK_VSC_TO_VK_EX)`
  with fixed VKs for modifiers and keypad; `WM_SYSKEY*` while Alt is down and
  Ctrl is not, and for F10. `GetKeyState` changes when a key message is
  retrieved.
- The limiter keeps the 47 ms virtual step; the pace sleeps until the wall
  clock reaches the virtual clock (rebase when more than 1 s ahead).

**Deviations.** A 32-bit SDL window: the present's source is converted
through the last uploaded colour table to XRGB8888 and drawn nearest
(`StretchBlt`'s mode and the 256COLOR palette emulation are not reproduced;
a 1:1 present is the same copy). Not resizable; no `WM_GETMINMAXINFO`,
`WM_ACTIVATE*`, `WM_SETCURSOR` or `WM_NCHITTEST`. No system-menu modal loop
(`SC_KEYMENU` is counted). All sent messages are dispatched from the peek.
Virtual time is paced by the wall clock. Num Lock with Shift on the keypad is
not modelled. The client rectangle while iconic stays the game size.

**Acceptance.** `//sim:platform_sdl_test` (dummy video driver, synthetic
guest image; the mutation of the previous-state bit is caught) and
`//sim:es2sim_window_test` (usage 64, a synthetic EXE refused with 65, the
claim `0x400000..0x6d7fff` holds with SDL3.dll loaded).

## The window's open points

Code: `sim/src/platform_sdl.c`, `main.c`, `hud_cockpit.c`,
`services_cursor.c`, rows in `sim/dispatch/services/cursor_warp.h`. Imports:
`GetWindowRect` (slot `0x6cc42c`, thunk `0x49694c`), `ClientToScreen`
(`0x6cc460`, `0x49699a`), `SetCursorPos` (`0x6cc490`, `0x4969e2`),
`StretchBlt` (`0x6cc4c4`, `0x496a2a`).

- **Keyboard menu (F10, a lone Alt): no menu loop for injected keys**
  (main_loop.md); `WM_SYSCOMMAND(SC_KEYMENU)` is counted and returns.
  **Hypothesis, open:** a physical F10 or Alt could enter USER32's menu loop;
  then es2sim needs a `WM_ENTERMENULOOP`/`WM_EXITMENULOOP` pair around a
  blocking wait.
- **`GetWindowRect` after a move** changes nothing: no reachable reader sees
  the origin (listing), so the creation rectangle `0x4d36cc` stays.
- **`StretchBlt`**: every present is 1:1; a scaled present is counted and
  drawn nearest; an extent of 0 or less draws nothing.
- **AltGr's fake left Ctrl (hypothesis):** with an AltGr layout a right-Alt
  key-down is preceded by a left-Ctrl `WM_KEYDOWN` (scan `0x1d`) and its
  key-up followed by the Ctrl's, only when no Ctrl is down.
- **Key state on activation (hypothesis):** on focus gain, before
  `WM_SETFOCUS`, `es2_sim_window_resync` copies `GetKeyboardState`.
- **The cursor warp `0x4808dc`** (`0x4808dc..0x480947`): `GetClientRect`
  (result not read); `x = [p]·[0x6c5f62] / ([0x4d3040] sar 1)`, `y =
  [p+4]·[0x6c5f66] / ([0x4d3044] sar 1)` (imul low dword, `cdq`, `idiv`);
  `ClientToScreen`; `SetCursorPos`; EAX its answer. Rows `client_to_screen`
  and `set_cursor_pos`: headless as the oracle's hidden window (point
  unchanged, TRUE), windowed with the client origin and a real move. A zero
  divisor or `0x80000000 / -1` ends the process at `0x480909` or `0x480922`
  ([The original's fault paths](#the-originals-fault-paths)).

**Acceptance.** `platform_sdl_test` (the resync, the AltGr sequence and its
lParams, the cursor rows, the empty and scaled present).

**Open.** The real-key keyboard menu, AltGr and the resync are not measured.

## The headless acceptance

The acceptance conditions of section 10: `es2sim.exe --headless` replays every
registered suite with the tick log, the watches and the return files
identical to the goldens in es2-data `oracle`; a difference is bisected; the
frame capture is compared with DBSIM's ([The frame compare as
tests](#the-frame-compare-as-tests)). **All 42 registered suites replay
IDENTICAL headless**. The pieces it
needs: the entry shims, the shim floor, the rows below and the headless
keyboard menu activation.

### The entry shims

110 of the 783 hooks of `//sim:table_test` are entered under the proxy at an
entry shim, which carries part of the contract (section 4.4 item 3).

- Each shim is one C entry `int (*)(es2_machine *m, uint32_t bridge)`
  (`es2/hook/machine.h`, `es2_machine_shim`; `ES2_SHIM` objects listed in
  `tools/reference-game/proxy/entry_shims.h`). It reads the entry registers
  from `m` and its stack slots at `m->esp`, writes the original's frame and
  save slots there, probes its gap (`es2_machine_probe`), continues at the
  floor (`es2_machine_floor`) and calls the replacement through `bridge`, or
  returns 0 to continue into it with the modified `m`. The emitter entries set
  `m->esp` to the residue record in ECX for their inner calls and restore it
  afterwards.
- Under the proxy a capture shim fills an `es2_machine` from the real
  registers and calls the same C entry (`entry_shims_x86.h`, proxy only).
- `es2_sim_register_adapters` (`src/adapters.c`) sets `es2_sim_entry.shim`;
  `table_test` prints "entry shims: 110 of 783 hooks" and checks the count
  against `es2_sim_adapter_shim_count`.
- The sim backend runs the C entry on the guest stack. The shim's call of
  `bridge` runs the replacement; every other call of the VA runs the shim
  again. A gap that passes the guest stack's limit is a model error (exit
  71).

**The residue model.** Before the shim runs, the sim backend zeroes the
`ES2_MACHINE_SIM_SHIM_ZERO` (4096) bytes of the guest stack below the
caller's ESP. With the bytes the guest stack holds there instead,
`c1_01-success` differs at tick 5208 and `c3_09-orders-matrix` at 1481. The
goldens accept the zeroed bytes; it is a model, not a measurement of the
residue.

### The shim floor (`es2_machine_floor`)

A gapped shim continues at `es2_machine_floor(top - gap)`: in the proxy the
lowest host ESP of a suspended machine call that switched ESP
(`es2_machine_x86_floor`) when it lies below the gap's end and at or above
`FS:[8]`, else the gap's end; in es2sim the gap's end. **Trap:** nothing is
stored between the gap's end and the floor. A store at the gap's end − 4,
when that is already a suspended bridge's floor, hits that bridge's saved
host frame: `c1_01-options-dialog` and `c3_10-detail-options` then fault in
the options dialog (`player_input` `0x460870` shim 23, `render_root`
`0x45fca8` shim 92, the store `cockpit_sync`'s `0x41b130` shim 89).
`differential_render_root_test` runs an edge sweep of 64 floors with a
painted stack ([main_loop.md "Shim stack
budget"](../reverse-engineering/main_loop.md#shim-stack-budget), cockpit.md).

### Rows reached by the replays (`sim/dispatch/services/hud_mfd_leaves.h`)

Sixteen rows (`dump_flush` `0x401e5c` to `part_bitmap` `0x438230`), each
checked against its listing range and compared in vitro by
`differential_hud_mfd_leaves_test` (`hud_mfd_leaves_cases.h`, 3000 cases).
Traps:

- **`status_tail` `0x43b288` takes four arguments**: it is jumped to from the
  status method `+0x40` (`0x43ac3d`, `0x43af9e`, `0x43b127`) and reads the
  flag and counter from that method's leftover slots at stack top − 8 and − 4.
- **`part_bitmap` `0x438230`** shifts the height by `[0x4d253a]` like the
  width (`4382a5`); mode 0 shifts by sx − 1 and sy − 1; modes 1 to 3 step one
  pixel at a time.
- **`0x4968f8` (RTL)** is entered by the original in every suite, but
  `init.c` calls it only on a branch the reference file never takes
  ([Called gap roots](#called-gap-roots)).

#### The popped locals

`0x451de0` and `0x45c964` end with `pop ecx; pop ecx`, so ECX is their
second local (the point's y), not the blit's ECX. Where that local is never
written ECX is stack residue; the rows answer 0 (the zero model). No caller
reads it (listing).

#### `weapon_gauge_event` `0x440f18`

Slot 0 of `EnergyWeaponGauge` (table `0x49c91c`; `0x440f18..0x440f9d`), own
code in `hud_gauges.c`. **The toggle argument:** CX holds the word at G+0xd9
with G's high word, so `group_toggle` gets `(G & 0xffff0000) | word`; when no
entry matches, its ECX = n escapes through this method's return (weapons.md
"4110ac").

### The keyboard menu activation (`loop.c`)

`c5_05-moon-missiles` presses F10 three times (journal frames 8113 to 8134).
F10's key-up reaches `DefWindowProcA`, which starts the keyboard menu
(`WM_SYSCOMMAND(SC_KEYMENU)`). The recording's window was hidden
(`ShowWindow` answers `FALSE`) and its tick log goes on with the frame counter
running, so no modal menu loop held the game's loop. Headless
`DefWindowProcA` counts these key-ups (`menu_keys`) and has no effect. A
visible window: [The window's open points](#the-windows-open-points).

### Harness (`sim/tests/replay_suite.py`)

- **`results.dat` baseline.** The recording clones carry the same
  `DATA\results.dat`, so a golden lists `results.dat` only when the run
  changed it. The own process starts without a result, so the comparison
  uses the clones' hash (`CLONE_RESULTS_SHA256`) as the baseline, except for
  a launch set's outcome run (keep_all).
- **The environment.** Every run gives es2sim an environment of the frame
  references' size as `--environment <output-dir>/<suite>/environment.block`
  ([The heap trace](#the-heap-trace), "The replay's environment").
- **`--frames`** adds es2sim's frame capture as
  `<output-dir>/<suite>/frames.ndjson` (not part of the replay verdict).

### Replay table

Every registered suite replays IDENTICAL (tick log, watches and, where the
golden has them, return files and exit code): 42 of 42.
The longest replays are `c3_09-loadout-b` (8886 ticks) and
`c5_05-moon-missiles` (8262 ticks).

**Deviations.** The shims' residue is the zero model. The keyboard menu
activation has no effect headless, as with the recording's hidden window.

## The frame compare as tests

`//sim:frames_<suite>_test` (one per registered suite, `sim/frames_tests.bzl`
over `replay_tests.bzl`'s `SUITES`; manual, local, exclusive like the replay
tests; `//sim:frames_all` runs them all) replays the suite headless with the
frame capture and compares it with DBSIM's capture of the same suite. A
difference fails unless the known-differences list `sim/tests/frames_known.py`
names it with its cause. **State:** PASS 40, SKIP 2 (`c1_01-tape-demo` and
`c1_01-tape-play` have no reference manifest); the only row is the "original varies" row
below.

### Region hashes on both sides

Both captures carry region hashes from one shared table,
`tools/reference-game/proxy/frame_regions.h` (header-only, C17, table id
`t66-1`): the **display mode** m = dword `[[0x4d24c0] + 0x14]` at the present
(-1 while D is not built); per mode a list of rectangles in 640 × 480
coordinates, scaled to the hashed window, in the priority MFD, HUD, panel,
view (a pixel no rectangle holds is "rest"); per region the first 16 hex
digits of the SHA-256 of its index bytes in row-major order, null for an
empty region. The table is a
localisation aid, not a contract of the drawing; mode 3 shares mode 2's table
as a hypothesis. A new table gets a new id; captures with different ids
compare whole frames only.

Writers: es2sim (`sim/src/present.c`, `--frames`; the start line carries
`"regions":"t66-1"`), the proxy (`proxy/presenter.h`, `[presentation]
capture_regions=1`, written by `capture-dbsim-frames.ps1 -Regions`; without
the key the bytes are the plain capture's), and `dbsim_frames.py` (`build`
keeps the table id and writes six more columns under `#regions<TAB><id>`;
`regions A B [--exclude-frames …]` reports each kind of difference with its
first frame and count).

### The kinds

Sequence records (site `0x45fbe4` and the slide returns,
`presenter_capture.SEQUENCE_SITES`) are matched by (frame, site, ordinal), so
a missing present does not shift later ones. Kinds: `sequence` (a key on one
side only), `geometry`, `frame` (whole-frame hash), `palette`, `display`
(regions not compared then), `view`/`hud`/`panel`/`mfd`/`rest`, and
`other_sites` (the per-frame set of (index, palette) of every other site).
Only frames up to the last frame both captures hold are compared.

### The known-differences list

`sim/tests/frames_known.py`: `REFERENCE` (the directory of the
reference manifests in the es2-data `logs`: table `t66-1`, clones
`timing-r111-p1`/`-p2`, hooks original), `REFERENCE_ENVIRONMENT` ([The heap
trace](#the-heap-trace)), and one row per suite and kind: `first`, `cause`, a
note, and for "original varies" the frames to `exclude` from the other-sites
compare. `frames_suite.py` judges as `shell/tests/screen_known.c`: a kind
without a row fails; a difference that starts earlier than its row fails; a
later one fails "update the row"; a gone one fails "drop the row"; "original
varies" rows never fail when gone or moved. The replay must be IDENTICAL
first. Without `ES2_DATA_ROOT`, the goldens or the reference the test SKIPs.
`--capture FILE` judges an existing `frames.ndjson`, `--suggest` prints rows
that would pass.

The one row: `c3_10-detail-options`, frames 204 and 600 out of the
other-sites compare, "original varies": frame 600 differs between three
original runs (presentation.md "The original's own frame spread"), and the dialog presents of
frames 204 and 600 follow the SOS timer's `0xa000` retrievals
([main_loop.md "The message trace of the replay pump"](../reverse-engineering/main_loop.md#the-message-trace-of-the-replay-pump)).

### Causes found by the compare

Found by comparing live pixel dumps of the original with es2sim's:

- **The HDD map image** (slide return `0x42b87f`): `0x44f6f4` step 1 writes
  one byte per grid cell, `min(height, 0x7f) / 8 + 0x10`, and step 4 draws
  two `0x486ee8` triangles per cell (`0x44fd4e..0x44ff7d`); `hud_load.c`
  `map_cells()`. Witness: fill base `0x10u` → `0x11u` fails
  `frames_c1_01-display-keys_test`.
- **The missile camera's terrain** (`0x43fe44`): the flat fill `0x46bc44`
  points the index list `0x6c68f2` at the default list `0x6c6068`, which the
  renderer reset `0x48dd08` fills once per load with word i = i for i in
  0..31 (`0x48dd30..0x48dd41`). In vitro: `load_rest_cases.h` "renderer
  reset"; `load_test` small_bodies.
- **The free camera's shape edge**: [below](#the-free-cameras-shape-edge).

**Pixel tooling.** `es2sim --dump-frames LIST --dump-dir DIR` writes ES2F
dumps in the proxy's format (at most 64 items and 4096 frames; a bad list or
directory exits `ES2_SIM_EXIT_SETUP`); `replay_suite.py` passes the options
on into `DIR/<suite>`; `frame_dumps.py compare A B [--display M]` exits 0
identical, 1 different, 2 unusable.

## The free camera's shape edge

The drawing order of three polygon groups inside one BSP part depends on a
read past the end of a heap block, so `c1_01-free-camera` frames 652, 653,
709 and 715 depend on es2sim's heap layout. With the heap's attach blocks
([The heap trace](#the-heap-trace)) the suite passes with all 1001 sequence
records equal; the cause `HEAP_LAYOUT` stays in `frames_known.py` without a
row.

- **The read.** The node order of `0x476b28` compares `d = 0x4911c0(eye
  0x6bb2e8, (tw<<5) + [0x6b7b68], record) - dist` against `[0x49f248]`. The
  transform table `[inst+0x16]` (constructor `0x478838`, new[] `0x4774f0`)
  holds 2 matrices, but the part's nodes use transform word 2, which lies
  past the block's end. The original reads past the block too.
- **The trap.** Nodes 0 and 1 flip once the eye's row 2 exceeds about 2600,
  which needs the word at table+0x50 (a heap pointer's low word) below about
  -881.
- **Live:** the original
  has the same neighbouring block; only the pointer's low word differs.

**The call probe** (`tools/reference-game/proxy/call_probe.h`, off unless
`[call_probe] enabled=1`): only the reference DBSIM.EXE and a VA in the
hash-bound entry inventory; a `jmp` over the five entry bytes, a private
stack, reads only, one "dump" line per listed frame and pointer, then the
bytes restored. Fixture `tests/call_probe_fixture.c`.

## SIM, R3D and HUD gap roots as rows and verdicts

Rows for the SIM, R3D and HUD targets of `sim_gap.md` that own code can reach
(fragment `sim/dispatch/services/gap_roots_sim.h`, code in `sim/src/gfx.c`);
every other root and virtual target carries a verdict. Every target here has
Live 0 in the current map.

**Rows.**

- **`0x492447` / `0x49085d`, stream put word / byte** (same 23-byte body
  except `6a 02` / `6a 01`): `[[st]+0x14](st, n, &slot1)`, the callee's
  registers returned. **Trap:** the listing is misaligned at `0x49085d` (it
  decodes from `0x49085f`); read the bytes raw.
- **`0x446e3c`, next MFD page** (input_dispatch.c case 14 bridge): `n =
  [g+0xc1] + 1`, 0 when `n == 6`; calls `0x446e60(g, n)`; EAX = n.
- **`0x444b84` / `0x444b98`, shield arrow button** (table `0x49c9dd`). Slot 0
  calls `0x431410(b)`. Slot 3: `s = [b+0x1f]`; `s == 1` calls `0x43122c(0, b,
  0)` and stores the re-read byte minus one; `s == 2` stores `s - 1`; the
  non-call paths return AL = `s - 2` over the entry EAX.

In vitro: `differential_gap_roots_sim_test`
(`gap_roots_sim_cases.h`, 2,000 random cases, every path hit).

**Verdicts** are in `tools/reference-game/scripts/sim_gap_verdicts.json`,
listed in [sim_gap.md](sim_gap.md) "Verdicts". Persistent-class
tables use slots 0 tag, 1/2 stored sizes, 3 read, 4 write, 5 dump, 6
destructor. Rows for `0x423044`, `0x427d98`, `0x427da0` and `0x401148`:
[Called gap roots](#called-gap-roots).

**Open.** A live check of key case 14 (next MFD page) would give its row
Live > 0.

## SHELL, RTL, FILE and INP gap roots as rows and verdicts

Rows for the SHELL, RTL, FILE and INP part of `sim_gap.md` (fragment
`sim/dispatch/services/gap_roots_shell.h`; code in `sim/src/services_files.c`
and `sim/src/loop.c`): the help language `0x45effc` and help file name
`0x45f070` (F1), the `-D` demo pick `0x45ceb8`, and the error report
`0x479c78` with its pieces (notify list `0x479c54`, report text `0x479dd0`,
log path `0x479e48`, log `0x479e8c`, report body `0x479f24`). Traps:

- **`0x45effc`** reads one byte into the low byte of a local dword that holds
  the entry ECX; EDX leaves as that dword (`pop edx`).
- **`0x45ceb8`** picks `_time(&t) % count` (`idiv`: the remainder has the
  dividend's sign); without a list EAX is 0 and EDX the saved `fs:[0]` (not
  reproduced); a marked list with a count of 0 or less is refused.
- **`0x479f24`** is a jump table (`0x479f45`, `ja` unsigned): severities 0, 1
  and above 6 do nothing; 2 logs; 3 shows an `Error` box and counts
  `0x6c582c`; 4 logs, then the box; 5 the "Internal Error" box, then the log;
  6 logs the Ctrl-Break line.
- **`0x479c78`** (row `fatal_report`, CDECL, 6 slots: record, severity, an
  unread argument, file, line, text): severities 4 to 6 run notify, the
  report body, `0x4650cc`, `0x4650d0` (KillTimer), `PostQuitMessage(0)`,
  `__exit(1)`; others only the body. EDX and ECX return zero; no caller reads
  them.

In vitro: `differential_gap_roots_shell_test`
(`gap_roots_shell_cases.h`, 2,000 random cases, all 33 coverage counters
non-zero).

**Verdicts** of the other roots ([sim_gap.md](sim_gap.md) "Verdicts"):
`0x464d44` caller-replaced; `0x467ad8`, `0x49688c` `_abort`, `0x49679c`
`__exit` and `0x4968f8`: [Called gap roots](#called-gap-roots); `0x4967fc`
`_time`: row `file_time` ([_time in es2sim](#_time-in-es2sim)); the store
closure: [Remaining virtual targets](#remaining-virtual-targets). The
joystick object's slots run inline in `sim/src/input.c`; the VOL stream,
write-stream and memory stream slots belong to classes no own code
instantiates.

**Open.** The rows are not on the recorded replays' paths (F1 help, `-D`,
asserts).

## The error paths' MessageBoxA, PostQuitMessage, __exit and _abort windowed

The guest's `MessageBoxA` shows a real modal box in the windowed build and
returns its answer; `__exit` and `_abort` end the process with the original's
status in both builds. Code: `sim/include/es2/sim/process_platform.h`,
`sim/src/loop.c`, `sim/src/startup.c`, `sim/src/main.c`.

**The calls (listing).** MessageBoxA's thunk is `0x496928`. The sites:
`0x47a08e` (report body, severity 5, which every one of DBSIM's 43 callers of
`0x479c78` passes), `0x479fb3` (severity 4, its answer is `0x479f24`'s EAX),
`0x479ff4` (severity 3), `0x45f377` (the eggplant box, style 0x30). The
fatal report ends with `PostQuitMessage(0)`, then `__exit(1)` (`0x49679c`,
push 1 at `0x479cd9`); `0x479e8c` calls `_abort` (`0x49688c`) when `_open` of
the log fails.

CW3220 (rtl_imports.md "abort, __ErrorExit, __ErrorMessage"; own model
`core/src/rtl_start.c`): `__exit(status)` is `ExitProcess(status)` (no atexit
functions, no exit records, no flush). `_abort` raises SIGABRT with the
default handler, shows "Abnormal program termination" with the module file
name as title and style 0x12010 (MB_TASKMODAL; the main window exists before
any code that can reach `_abort`), then `__exit(3)` (static; no live run has
reached `_abort`).

**The process platform** (`es2_sim_process_platform`): `message_box`,
`process_exit`, `module_name`, `process_fault` ([The original's fault
paths](#the-originals-fault-paths)).

- **Windowed:** Win32 `MessageBoxA` with the guest's ANSI bytes (SDL's box
  takes UTF-8 and has no MB_* bits); `process_exit` prints the window and
  audio statistics, closes the host's log with reason `fatal_error` or
  `abort` and calls `ExitProcess`. `ES2SIM_MESSAGE_BOX=none` prints the box
  instead and answers IDOK.
- **Headless:** MessageBoxA is counted (caption and an FNV-1a hash of the
  text in `es2_sim_fatal_box_last`) and answers IDOK. `__exit` ends with 1
  (`fatal_error`), `_abort` with 3 (`abort`).
- **Tests:** a recorder returns from `process_exit`; the loop ends with
  `ES2_SIM_LOOP_EXITED`. A NULL record refuses at `0x49679c` / `0x49688c`.

**WM_QUIT.** The pump's peek returns the journal, the posted messages, the
window's queue (WM_PAINT held back while WM_QUIT is pending), then WM_QUIT:
Windows' retrieval order. `loop_test` checks that WM_QUIT is pending at the
`__exit` call.

**Deviations forced by the platform.** The own window procedure and the SDL
mixer run on the simulation thread, which is blocked in the box: window
messages queue and sound stops until it returns. `ExitProcess` skips the
frame capture's `close_frames` atexit, as `__exit` runs no atexit either.

**Tests.** `//sim:loop_test` `fatal_box`, `//sim:startup_test` case 12,
`//sim:es2sim_window_test` with ES2_DATA_ROOT.

**Live.** The fatal box (no `data\drive.cfg`), the eggplant box and the
script.dat box ([The mission start's clean-up 0x461304 and the call helpers'
EXITED](#the-mission-starts-clean-up-0x461304-and-the-call-helpers-exited))
appeared as expected, with `assert.log` holding the line. The main loop's
report (the flush `0x401e5c` when `<name>.dmp` cannot be opened) has not been
run.

**Open.** The abort box's style and title come from a static reading of
CW3220.

## The error report 0x479c78 at the sites that refused

Every own site where the original calls the error report `0x479c78` calls it
through the dispatch with the arguments the original pushes; if the report
returns, the code goes on with the instructions after the call, as the
original's bytes do. Code: `sim/src/load.c` (`fatal_report` after the `CALL`
macro), `sim/src/hud_internal.h` (`hud_fatal_report`), `sim/src/hud_cockpit.c`,
`sim/src/hud_gauges.c`.

**The sites (listing).** The listing has 43 calls of `0x479c78`. All pass the
record `0x6c56c8` and severity 5; the third argument is the `sete`/`and 1` of
the failed test, 0 at every site (at `0x44a992` it is `[R+0x18]`, 0 on that
branch). The
sites are in `sim/src` (load.c, hud_cockpit.c, hud_gauges.c, the dump flush
`0x401e5c` in loop.c, the scene's `0x4281b0` and `0x4282f8`), in `core/` (the
type loaders, called through the dispatch) and in startup.c `0x45f160` (the
start-up's own model of the report; it ends with outcome EXIT and code 1).

**The call helpers keep EXITED.** Every call helper (`call_regs` in load.c,
`hud_call_regs` in hud_internal.h, the ones in scene.c, audio.c, frustum.c,
hud_frame.c, input.c, shape_draw.c, terrain_draw.c and view.c) returns with
the outcome kept when a refused call ended the process inside it (`if
(l->outcome == ES2_SIM_LOOP_EXITED) return 1;` before the REFUSED line), as
`es2_sim_loop_call` does. **Trap:** otherwise a windowed report reached
through them would end with 70 instead of 1.

**Tests.** `//sim:fatal_sites_test` (synthetic): one case per site; the
report's own row headless with a NULL record (refused at `0x49679c`) and with
a recording record (EXITED with code 1, one box, one `__exit(1)`). Five
mutations are caught (among them the EXITED line removed from `call_regs` and
`hud_call_regs`). In vitro: gap_roots_shell, hud_cockpit, cockpit,
cockpit_rest, gauges, hud_radio, cockpit_pixels, scene, load_rest,
type_loads, mission_root, mission_actors, mfd_orders.

## The mission start's clean-up 0x461304 and the call helpers' EXITED

The mission start `0x461608` registers `0x461304` with the notify list
`0x479cfc` (at `0x461626`), so the error report runs it before its box.
`0x461304` and its only new callee `0x46a4ac` are rows (`sim/src/loop.c`:
`abort_cleanup`, `remap_release`; fragment
`sim/dispatch/services/mission_abort_cleanup.h`, CDECL, EAX, no slots, no
shims). The callees stay rows: `sound_close` `0x462734`, `file_fclose`
`0x496838`, `operator_delete` `0x477548`.

**`0x461304..0x46134a`** (listing):

1. `call 0x462734` (the sound close).
2. `mov eax,[0x4d24e2]`, `test`, `je 0x46133c`. If set: `push eax`,
   `call 0x496838` (`_fclose`), `pop ecx`, `xor edx,edx`, `[0x4d24e2] <- edx`.
3. `mov eax,[0x4d24de]`, `test`, `je 0x46133c`. If set: the same with
   `[0x4d24de]`.
4. `0x46133c`: `call 0x46a4ac`, `xor ecx,ecx`, `[0x4d27b8] <- ecx`, `ret`.

`[0x4d24e2]` is the playback tape's FILE*, `[0x4d24de]` the dump stream
(`-d`). **Trap:** both tests jump to the same place, so without a tape stream
the dump stream is not looked at.

**`0x46a4ac..0x46a4d3`**: `cmp word [0x4a0918],0`, `je` to the `ret` (a
bare return). With the flag: `push [0x6b4f20]`, `call 0x477548`, `pop ecx`,
`xor edx,edx`, `[0x6b4f12] <- edx`, `word [0x4a0918] <- 0`, `ret`. **Trap:**
the cleared dword is `0x6b4f12`, not the block pointer `0x6b4f20`, which
stays.

**Registers.** `0x46a4ac` with the flag: EAX delete's, EDX 0, ECX the
argument slot after the call. `0x461304`: ECX 0; EAX and EDX what `0x46a4ac`
leaves.

**Tests.** `//sim:abort_cleanup_test` (six states of tape, dump and flag;
`0x46a4ac` alone; the report with a notify node `0x461304`; the real
`sound_close` ending the process through the report). In vitro:
`differential_mission_abort_cleanup_test`
(`mission_abort_cleanup_cases.h`), 3000 cases identical, with stubs that
rewrite their argument slot in half the calls so the `pop ecx` shows.
Mutations caught: `remap_release` without EDX 0; `0x6b4f20` cleared instead
of `0x6b4f12`; audio.c's helper without the EXITED line.
`es2sim_real_test` case 3: without script.dat the launcher's line calls
`0x461304`, writes assert.log with `Line 222 in "INIT.CPP": data\script.dat`
and ends with code 1. `es2sim_window_test` windowed: the "Error" box, exit
code 1. Live: the same box and exit code with `es2sim_window`.

## The after-load snapshot against the original

Details, tables and the live procedure: mission_load.md section 14,
"es2sim's snapshot against the original".

- es2sim writes its after-load snapshot (flags `own_runtime_heap` and
  `armed_early`) at the original's step; with the snapshot on, the replays
  stay identical to their goldens. Two headless passes compare identical
  (`--scope all` and `pools`); only the foreign sample-block pointers vary.
- **Host state in a guest block.** Bytes +6..+7 of the 16-byte heap block of
  the graphics-library object `new(6)` at G+0x4e (`0x4d250a`) are allocation
  slack: the upper half of a stale pointer into es2sim.exe's own read-write
  data, so they follow es2sim's layout.
- No original `.snap` files are kept in `es2-data`, so the compare against
  the original needs a live run: two original runs with
  `ES2_AFTER_LOAD_SNAPSHOT_EXTRA=0x6cbd58:0x18c`, a noise file, then
  `compare O1 es2sim --ignore 0x6cbd58:4 --ignore 0x6cbd60:0x184`.
- The differences between the own heap model (`core/src/rtl_heap.c`) and
  CW3220's allocation history ([mission_load.md "The noise
  model"](../reverse-engineering/mission_load.md#the-noise-model)) have three
  causes: allocation slack, the whole-block fit below the 16-byte minimum,
  and the byte counter `0x6bb30c`. `after_load_snapshot.py` counts them as
  classes when the two heaps' runtimes differ.
- es2sim's heap (`0x20000000`) and the original's (`0x025xxxxx..0x09bxxxxx`)
  do not overlap, so equal bytes cannot pair by chance.

**Open.** The live compare above has not been run; whether the 190 unlisted
differences of `snapshot-r7c`'s own-runtime load all fall into the three
classes.

## es2sim's frame times

The instrument that measures whether the windowed es2sim holds the original's
40 ms limiter (`0x4678c0`, previous + 40) with margin.

### The instrument

- **Switch.** `ES2SIM_PERF`: off when unset, empty or `0`; `1` writes
  `perf.ndjson` next to the `--tick-log` (or `es2sim-perf.ndjson` in the
  current directory); any other value is the file path. Both builds have it.
- **What it reads.** Only the wall clock (QueryPerformanceCounter) and the
  dispatch's counters; it writes only its own file. Replays with the switch
  on stay IDENTICAL.
- **Code.** `sim/include/es2/sim/perf.h` (documents the format) and
  `sim/src/perf.c`, called from the loop's `before_logic`, `on_tick` and
  `finish_run`. Windowed, the window layer's `delay_ms` is wrapped so the
  sleeps count as wait. `es2_sim_entry.traps` counts unconverted calls,
  `calls` the dispatched calls.
- **Format.** NDJSON: a header (mode, clock rate, set-up time, counters); one
  line per frame (`us` since the previous tick, `work`, `wait`, `traps`,
  `calls`); an end line with totals and the 24 entries with the most traps.
  Busy time is `us − wait`; headless, `wait` is 0.
- **Summary.** `sim/tests/perf_suite.py summary <files or directories>`:
  frame and busy time (median, p95, max), traps and calls per frame, the
  share of frames over 40 ms (`--budget-ms`) and over 47 ms, the trap share
  of the busy time (traps × 11.58 µs), the set-up time; `--json OUT`.
- **Run.** `perf_suite.py run`: headless it runs `replay_suite.py` with
  `ES2SIM_PERF=1` and keeps the replay's verdict. With `--window` it starts
  `es2sim_window` in the suite's prepared directory with `[run] frame_limit`,
  `SDL_VIDEODRIVER=dummy` and `SDL_AUDIODRIVER=dummy` (`--real-audio` keeps
  the real device). Every child starts with CREATE_NO_WINDOW.
- **Tests.** `//sim:perf_test` and `//sim:perf_suite_test`; three mutations
  of `perf.c` each fail `perf_test`.

**What the dummy driver leaves out.** No real present (GPU upload,
compositor, vsync, scaling), no real audio device (unless `--real-audio`) and
no CD music. The run is a windowed replay (`window_replay` in `main.c`):
virtual time unpaced, so the frame time is the busy time.

### How to run it (quiet machine)

From PowerShell in the repository, with nothing else running; `<base>` is the
Bazel output base.

```powershell
$env:ES2_DATA_ROOT = 'build\extracted\cd'
$env:ES2_DATA_REPO = '<es2-data checkout>'
bazel --output_base=<base> build //sim:es2sim //sim:es2sim_window
$bin = "$(bazel --output_base=<base> info bazel-bin)\sim"
$out = "$env:ES2_DATA_REPO\logs\perf-<date>"
python -B sim\tests\perf_suite.py run `
  --sim "$bin\es2sim.exe" --window "$bin\es2sim_window.exe" `
  --suite acceptance-60s --suite c1_01-cockpit-keys --suite c1_01-free-camera `
  --output-dir $out --json "$out\summary.json"
python -B sim\tests\perf_suite.py summary $out
```

For a single binary: `$env:ES2SIM_PERF = '<dir>\perf.ndjson'`, then the usual
es2sim command line.

### Figures (quiet machine)

acceptance-60s with the portable dispatch (no call traps, 10041 calls per
frame), all replays IDENTICAL (1500 ticks):

| Mode | Frame ms median / p95 / max | Over 40 ms |
|---|---|---:|
| headless | 8.2 / 9.7 / 14.6 | 0 % |
| windowed replay (dummy driver) | 11.4 / 12.7 / 20.4 | 0 % |

Medians of the other two suites: c1_01-cockpit-keys 10.3 ms headless and
13.6 ms windowed, c1_01-free-camera 7.9 ms and 11.2 ms.

**Open.** The visible window (a real present with vsync) is not measured by
the dummy driver.

## Stage 2: the hot VAs

The adapters' calls into CODE are machine calls (`es2_machine_call`,
`es2/hook/machine.h`): in es2sim the sim backend (`sim/src/machine_sim.c`)
looks the VA up in the dispatch and runs the entry's C code on the guest
stack, without the vectored trap; an entry with a shim runs the shim's C
entry first ([The entry shims](#the-entry-shims)). The trap remains as a
guard (section 4.4). The proxy `ddraw.dll` and `differential_harness.exe`
use the x86 backend (`hook/machine_x86.c`).

### Registers of the machine calls

**The stack helpers** are machine calls: EAX, EDX and ECX as the adapter
passes them, EBX, ESI, EDI and EBP as the helper names them for the call (its
frame, the core's call frame or ESI alone) and 0 otherwise. **The plain
calls** set EAX, EDX and ECX as the adapter passes them and EBX, EBP, ESI and
EDI to 0 (`machine_call.h`); the math calls without register inputs pass 0.
Some leaves pass entry registers through (`base_radius` returns the entry
EDX and ECX), so no consumer may treat such a call's undefined entry
registers as meaningful. Every math target ignores
or explicitly loads its entry registers; `0x418f40`'s entry EAX high word
reaches only a discarded EDX result.

**Stack below ESP.** The helpers that plant residue below ESP
(`flyer_control_stack_call`, `flyer_states_stack_call`,
`weapons_prepare_stack_call`, `weapons_round3_stack_call`) use
`es2_machine_call_planted`; the sim backend writes the plants below the
pushed return slot, where an entry shim's C entry reads them like the
original.

### Open

- **C sites** give the callee compiler-defined entry registers that differ
  from build to build; adapters must keep ignoring them.

## Headers, imports and the icon as own tables

The three things es2sim would take from the headers and directories of
`DBSIM.EXE` (dbsim_gap.md "What `es2sim` takes from `DBSIM.EXE`") are own
compiled-in tables: `sim/data/dbsim_image_tables.c`, generated by
`tools/reference-game/scripts/dbsim_image_tables.py` from the reference
`VER95/DBSIM.EXE` (724,512 bytes, checked against its hash before
anything is read). The generator has its own PE reader; its committed output
is the source of record. Only the fixture test and the real-data check run
it.

| Table | Content |
|---|---|
| `es2_sim_dbsim_layout` | image base `0x400000`, size `0x2d8000`, headers `0x400`, entry RVA `0x1000`; the 6 sections (`CODE`, `DATA`, `.idata`, `.edata`, `.reloc`, `.rsrc`); the import (`0x2cc000`, `0xc9a`), relocation (`0x2ce000`, `0x8ab4`) and resource (`0x2d7000`, `0x400`) directories; executable range `0x401000..0x497000` |
| `es2_sim_dbsim_imports[]` | 134 slots in file order, all by name: DLL, symbol, hint, hint/name VA, IAT slot VA. cw3220.DLL 53, USER32 39, KERNEL32 23, GDI32 13, WINMM 4, DDRAW 1, MSVFW32 1 |
| `es2_sim_dbsim_resources[]` | ICON #1 (744 bytes at `0x6d70b0`) and GROUP_ICON `"ES2ICON"` (20 bytes at `0x6d7398`; one 32 × 32 16-colour entry, ID 1), both language 0: all of DBSIM's resources |

**API** (`sim/src/image.c`, `es2/sim/image.h`): `es2_sim_image_info_own`,
`es2_sim_imports_own` (`"dll!symbol"` or `"dll!#ordinal"` and the slot VA),
`es2_sim_resource_own` (type by number, name case-insensitive, else the ID;
the neutral language wins, else the first), and
`es2_sim_window_load_icon_own` (`platform_sdl.c`;
`LookupIconIdFromDirectoryEx` and `CreateIconFromResourceEx` on the own
bytes). The window's class icon comes from it; `icon_name` is the string at
`0x4a084d` (LoadIconA's argument at `0x465111`). The file-based counterparts
live in the testonly `//sim:image_file` ([es2sim without
DBSIM.EXE](#es2sim-without-dbsimexe)).

**Tests.** `//sim:image_tables_real_test` (`ES2_DATA_ROOT`) compares layout,
imports, IAT slots, resources and the icon bits (32, 16, `SM_CXICON`,
`SM_CXSMICON`) with the reference file; `//sim:image_test` `own_tables`;
`//sim:platform_sdl_test`;
`//tools/reference-game/scripts:dbsim_image_tables_unit_test` (synthetic
PE32; the real case regenerates and compares byte for byte).

**Open.** The visible icon of a real window is checked only in the play test.

## The AVI code in DBSIM and es2sim

**Verdict: unreachable.** No reference in DBSIM reaches the AVI block
`0x471628..0x471a4d`, so no mission path reaches it; es2sim keeps no
counterpart and needs no Indeo path. `ICSendMessage`, `mciGetDeviceIDA` and
`MoveWindow` stay refusal traps.

**Method.** The `llvm-objdump -d -M intel` listing of the reference
`VER95/DBSIM.EXE` and `-p` for the exports; two byte scans, both supersets of
the real references: every dword equal to an entry VA and every `E8`/`E9`
rel32 landing on it; every dword and every `E8`/`E9`/`0F 8x` rel32 from
outside `0x471625..0x471a4f` pointing into the range.

### The block: no class, eleven functions

Eleven plain functions in `0x471628..0x471a4d`, no constructor, no vtable:

- five `ICSendMessage` wrappers (`0x471628`, `0x471698`, `0x471708`,
  `0x471778`, `0x4717c4`; messages `0x403e`, `0x403c`, `0x403d`, `0x4032`,
  `0x4048`). Hypothesis: the out-of-line copies of the VfW inline helpers
  `ICDecompressEx`, `ICDecompressExBegin`, `ICDecompressExQuery`,
  `ICDrawSuggestFormat`, `ICSetStatusProc`;
- `MCI_OPEN` of type `"avivideo"` (`0x4717f8`) and its close
  (`0x471848`);
- the video window placement `0x471868` (`MoveWindow([0x6b5480], …)`), the
  close `0x4718b4`, the `MM_MCINOTIFY` handler `0x4718d4` (sets
  `[0x4a0cb4]` = 1);
- the player `0x4718f8` (cdecl: element name, x/y/w/h pointer or 0): opens a
  `WS_CHILD` device on the main window `[0x4d3610]`, plays with NOTIFY,
  pumps until `[0x4a0cb4]` ≠ 0 or a key with low byte 1 or `0x39` (Esc,
  Space; hypothesis), closes; returns 1 if a key skipped the video.

**Next to the block.** The vtable `0x4a0ca0` belongs to the local
`DummyDrawable` that `0x46bd54` installs (rasterizer.md, the Gouraud path);
`0x47161e` is its "return 0" method. The function index's rows `47161e` and
`4718b4` each span several functions. `0x471a50` on is the mission load's
pool code.

### Callers: none

- No rel32 call or jump and no dword anywhere in the file equals one of the
  eight outer entries; none is exported (DBSIM exports only
  `__GetExceptDLLinfo`, `@sfxWndProc$qqspvuiuil`, `@MainWndProc$qqspvuiuil`).
  No dword and no rel32 from outside the range points into it.
- `0x471868`, `0x4718b4`, `0x4718d4`, `0x4a0cb4`, `0x4a0cb8`, `0x6b5480` and
  the two `"avivideo"` strings are referenced only from inside the block.
- The thunks' only callers outside the block are 10 `mciSendCommandA` calls
  in the CD glue `0x473c13..0x473e8b`.
- DBSIM is relocatable (16,850 HIGHLOW fixups), so a code pointer to the
  block would be a dword equal to its address; only a computed address could
  escape the scans, and none is known. DBSIM holds no `.avi` string and no
  `mciSendStringA` or `AVIFile*` import. The movies are `VSHELL.EXE`'s
  (player `0x41e98c`, file_io.md; own chain: media_compatibility.md).
- **The suites.** `video-compat.jsonl` has 3,336 `window_hidden_ShowWindow`
  and **0** `window_hidden_MoveWindow` events (DBSIM's only `MoveWindow` site
  is `0x4718a7`). The DBSIM maps have no row in the range. No constant of the
  range appears in `sim/src`, `core/src` or the adapters. es2sim's MCI host
  (audio.md) takes only the CD glue's messages and answers `MCI_WINDOW` with
  `MCIERR_UNSUPPORTED`.

## What es2sim really reads from the mapped image

`es2sim --image-access <path>` (off by default; `replay_suite.py
--image-access` writes `<output-dir>/<suite>/image-access.json`) records
which bytes of the guest image es2sim reads at run time besides the code it
replaces. Code: `sim/src/image_access.c`,
`sim/include/es2/sim/image_access.h`; armed at the end of `es2_sim_map`
(`host.c`).

### Method

- **CODE.** Read faults on no-access CODE pages are recorded (VA, operand
  size from an own decoder, instruction, nearest own-code return address) and
  served by making the page read-only for one single-stepped instruction.
  Execute faults go to the trap (`ExceptionInformation[0] == 8`).
- **File-backed pages** (headers, table pages, initialised DATA
  `0x497000..0x4a7c00`, `.idata`, `.edata`, `.reloc`, `.rsrc`) carry
  `PAGE_GUARD`; a guard fault counts the access, marks the operand's bytes
  and re-arms after the instruction.
- **Sampling.** At most 32 re-arms per page per epoch (each host tick and
  every 50 ms). **Page granularity is complete; byte granularity is a sampled
  lower bound.**
- **Not counted:** the host's own reads (tick record, watches, after-load
  snapshot; wrapped in `es2_sim_image_access_pause`/`resume`) and the set-up
  before arming.
- **The map** (`es2sim-image-access/1`, no image bytes) is written by
  `finish_run`; a host refusal (`ExitProcess`) writes none.

### Result with the file's CODE bytes mapped (40 suites, headless)

All suites IDENTICAL.

- **CODE reads outside the table pages: 14 VAs**, all 4-byte reads by
  `memory.c` `id_read32` from own models of `0x496406`
  (`mov eax,[eax-0xc]; mov eax,[eax+0x20]`: the dword +0x20 of a type
  descriptor): `0x408310`, `0x40ab5d`, `0x40b61d`, `0x40c3f8`, `0x40f7b6`,
  `0x41bd16`, `0x424004`, `0x426ffd`, `0x45c573`, `0x46bb91`, `0x47cf71`,
  `0x4823a6`, `0x4836e0`, `0x490cfc`. **Trap:** for a read across a page edge
  the record's VA is the faulting byte, so the dword recorded at `0x427000`
  starts at `0x426ffd`.
- **The table pages** `0x410000`, `0x426000`, `0x44b000`, `0x44e000` were
  read at the CODE read sites of dbsim_gap.md (`0x4100fe..`, `0x426492..`,
  `0x44bb9f..`, `0x44e1f5..`). The pages of `0x405366`, `0x436be2` and
  `0x4967ae` were only executed; their static sites stand.
- **Initialised DATA.** All 17 pages read and written; sampled 9,067 of
  68,608 bytes read, 1,608 written; the 1,024 BSS bytes
  `0x4a7c00..0x4a8000` all written.
- **Not read after arming:** headers, `.idata`, `.edata`, `.reloc`, `.rsrc`.

### With the image built from own runs

Every suite with `--image-access`, headless, IDENTICAL with `code_reads`
empty. The non-site CODE pages form seven no-access runs:
`0x401000..0x405000`, `0x406000..0x410000`, `0x411000..0x426000`,
`0x427000..0x436000`, `0x437000..0x44b000`, `0x44c000..0x44e000`,
`0x44f000..0x496000`.

### The windowed start-up with `--image-access`

`replay_suite.py --suite c1_01-success --window --window-devices
--image-access`: IDENTICAL; no CODE read faults; the same seven no-access
ranges; read table pages `0x410000`, `0x426000`, `0x44b000`, `0x44e000`.
This is the journal replay (input from the journal, virtual time unpaced);
the CODE result does not depend on how the run ends. The run also shows the
posted-queue overflow ([main_loop.md "The posted queue's
depth"](../reverse-engineering/main_loop.md#the-posted-queues-depth)).

**Open.** A system call that reads a guarded page would fail instead of
faulting; none showed up, but paths no suite reaches are not covered. The
caller column is a stack scan.

## The initialised DATA and the CODE bytes as own runs

Everything es2sim reads from the guest image is own compiled-in runs at the
original VAs.

- `sim/data/dbsim_data.c`, generated by
  `tools/reference-game/scripts/dbsim_data.py` from the hash-checked
  reference `DBSIM.EXE` and its `llvm-objdump -d -z -M intel` listing (PE
  reader and hash gate from `dbsim_image_tables.py`). Regenerate:
  `python -B tools/reference-game/scripts/dbsim_data.py --exe <DBSIM.EXE>
  --map <summary.json>`; `--check`
  compares.
- `sim/src/image_data.c`, `sim/include/es2/sim/image_data.h`: the run type
  (VA, size, kind, data); `es2_sim_data_check` (kind known, dword kinds a
  multiple of 4, strings NUL-terminated, inside the guest, ascending without
  overlap); `es2_sim_data_write` (checks first; nothing written on a
  refusal); the RTTI lookup (`es2_sim_data_rtti_count`,
  `es2_sim_dbsim_rtti_count`).
- `sim/tests/image_data_map.h`: the image-access map's ranges and its 14
  CODE read VAs.

### Classification of the initialised DATA

`0x497000..0x4a7c00` (68,608 bytes):

| Class | Count | Bytes |
|---|---|---|
| Pointer slots (HIGHLOW `.reloc` sites) | 3,454 in 1,411 runs | 13,816 |
| of these, to code / DATA / BSS | 2,607 / 702 / 129 | |
| of these, unexplained, emitted as plain dwords (`ES2_SIM_DATA_DWORDS`) | 16 | |
| Strings (C literals) | 693 | 10,927 |
| Byte runs (zero gaps under 16 bytes stay inside) | 1,001 | 25,685 |
| Zero bytes not emitted | | 18,180 |

TAB, LF and CR are accepted inside strings (hence 693 strings against
dbsim_gap.md's static 653). A run is split at each VA of a doc table (math
tables, keyboard map, init and exit records). Every relocation site in the
initialised DATA is a slot and every slot a site.

Pointer targets: an instruction start that is a call target or begins
`55 8b ec` is a function, any other a label; a validated type descriptor is
RTTI. **The 16 unexplained targets** lie one byte after a `00` or `90`
padding byte, where the linear listing decodes across it. By their bytes
they are code (hypothesis, not checked in vitro): this-adjusting thunks,
functions starting with `enter`, and two function pointers of the init
records (`0x4a7a40` → `0x413ed4`, `0x4a7a46` → `0x414c68`). The slots keep the
original values; the image stays at `0x400000`.

### The CODE runs (203)

- **Six index-byte tables**, each verified by the pattern `cmp r32, size-1;
  ja; mov r8, [r32 + table]; jmp [4*r + table + size]`: `0x405366` (62
  bytes), `0x4100fd` (33), `0x426492` (11), `0x436be2` (57), `0x44bb9f` (15),
  `0x44e1f4` (29).
- **The dword `0x4967ae`**, named by four relocated
  `cmp dword ptr [0x4967ae], 0` operands.
- **196 RTTI counts.** `0x496406` reads `[[v - [v-4] - 0xc] + 0x20]`. A
  candidate is a relocated CODE pointer in the initialised DATA at `v-0xc`
  with `[v-8] = [v-4] = 0`; it counts only if the target validates as a class
  descriptor (mask word +4 is 3, printable NUL-terminated name at +6, vptr
  offset at +8 not `0xffffffff`). 223 candidates, 27 rejected, 196
  descriptors; all 14 measured VAs are among them. CODE's other 55 class
  descriptors have no vtable, so `[obj-0xc]` cannot reach them.

**The spans table**: the whole initialised DATA, then each CODE run: 204
spans, 69,603 bytes. Written into zeroed memory, the runs reproduce the
initialised DATA exactly.

### Tests

`//tools/reference-game/scripts:dbsim_data_unit_test` (synthetic PE32 and
listing, refusals, committed files current, the real case expects 196
descriptors); `//sim:image_data_test`; `//sim:image_data_real_test`
(`ES2_DATA_ROOT`: every run equals the placed file, the zeroed image equals
the file over every span, relocation sites = slots, every map range and
CODE read inside a span). Mutation caught: one byte of the run `0x4a6940`.

## es2sim without DBSIM.EXE

es2sim reads no `DBSIM.EXE`. The guest image is built from the own tables
([Headers, imports and the icon as own
tables](#headers-imports-and-the-icon-as-own-tables)) and the own runs ([The
initialised DATA and the CODE bytes as own
runs](#the-initialised-data-and-the-code-bytes-as-own-runs)).

**`es2_sim_map(sim, reserved)`** (`sim/src/host.c`):

1. checks the own layout: base `0x400000`, size `0x2d8000`, CODE page-aligned;
2. commits the reservation read-write (zero);
3. writes the runs (`es2_sim_dbsim_data_write`);
4. fills each of the 134 IAT slots with its refusal trap (a separate
   read-only page range of `0xcc` bytes, never executable);
5. sets the original's protections: headers read-only; writable sections
   read-write, the other data sections read-only; a CODE page read-only when
   it holds a CODE_TABLE run, else no-access
   (`es2_sim_code_page_protection`);
6. arms the image-access switch when asked.

CODE is never executable. The seven CODE_TABLE runs lie on seven distinct
pages; the other 143 CODE pages are no-access. Bytes no run covers are zero.

**A stray read.** For an access violation inside CODE, `main.c`'s
unhandled-exception filter prints `FAULT: read|write of CODE 0x… at …: no own
data there` before the usual FAULT line (exit 71).

**The RTTI counts.** The own models of `0x496406` (`rtti_count` in `load.c`,
`joystick_destroy` in `input.c`, `r_destroy` in `volumes.c`) read the count
through `es2_sim_dbsim_rtti_count(va, &value)` (binary search; a value only
for an RTTI_COUNT run of size 4 starting at `va`); a descriptor outside the
runs falls back to the guest read (synthetic test images only). Readable
pages holding only the counts were rejected: the 196 dwords are spread over
many CODE pages, and a stray read there would silently return zero instead
of faulting.

**The file path is testonly.** The hash gate, PE parse, placement, the
file's imports and resources and the file-based icon loader are in
`sim/src/image_file.c` (`//sim:image_file`, testonly). `//sim:image` does not
depend on `sha256`. The run-time binaries carry neither the `VER95` path nor
the hash.

**Command line.** es2sim takes no path of a DBSIM.EXE or a data root (`--exe`
and `--data-root` are usage error 64) and does not read `ES2_DATA_ROOT`; the
VOLs and game files come from the working directory. `configure-sim.ps1`
writes `command = "<command line>"`, because GetPrivateProfileString drops a
quote pair around a whole value.

**Tests.** `//sim:host_real_test` (every CODE page's protection, run bytes,
the RTTI lookup); `es2sim_real_test` and `es2sim_window_test` in a synthetic
installation without DBSIM.EXE; `//sim:no_dbsim_real_test` (neither binary
contains "VER95" or the gate's hash; two replays IDENTICAL from a mirror of
`ES2_DATA_ROOT` without `VER95\DBSIM.EXE`; a re-introduced `fopen` of it is
caught).

## The chain without the executables

- `start-own-menu.ps1 -CdRoot <dir>` sets the `cd_root` that the own
  executables read (menu.ini); default `<ExtractedRoot>\cd`. The verification
  against the original keeps `-DataRoot` and `ES2_DATA_ROOT`.
- `check.ps1 -OwnCdRoot <dir>` passes it to the own-shell and own-sim chains.

`check.ps1 -Live -SkipQuick -OwnShell -OwnSim -OwnCdRoot <copy>` on a copy of
the extracted CD without the `VER95` and `VER31` copies of `DBSIM.EXE` and
`VSHELL.EXE` passed: own-shell text and gui, own-sim replays IDENTICAL,
frames, chain (es2sim identical to the c1_01-success golden).

## A time per DBSIM capture record

`[presentation] capture_qpc=1` gives the DBSIM presentation capture
(`proxy/presenter.h`, presentation.md "Capture record") a QueryPerformanceCounter
stamp per record, the counterpart of VSHELL's `qpc=1` (vshell_ui.md 42.5): the
wall time of the original's frames for a pacing comparison with
`es2sim_window`.

### The key and the schema

- Read in `pr_open_capture`; any nonzero value; needs `capture=1`.
- **Off:** every byte is the schema-1 report.
- **On:** the start record has `"schema":2` and ends with
  `"qpc_frequency":<n>`; every record ends with `"qpc":<n>` (after the region
  fields). Removing them gives the schema-1 line byte for byte.
- The stats lines' `qpc_frequency`, `capture_qpc` and `present_qpc` are the
  capture's own cost counters, a different thing.

### The stamp

- The counter on **entry** to the hooked present (the `StretchBlt` import
  slot, return address `0x4659b2`), the value the hook reads anyway; so the
  stamp precedes the own presenter's draw and the record's write.
- The DirectDraw branch (`ddraw_windowed.h`) stamps on entry to
  `ddw_present` (called by `Unlock` of a non-flipping primary, return
  `0x465876`, and by `Flip`, return `0x465895`), only when the record will
  be written and `capture_qpc=1`. The clears (returns `0x46552a`,
  `0x46554a`) are neither recorded nor stamped.
- `pr_write_record` stamps at the write a record without an entry value; no
  present path does so.
- A gap between two stamps is "entry to entry"; the time inside `StretchBlt`
  is part of the next gap.

### The readers

- `dbsim_frames.py` `read_capture`: schema 1 or 2 (for 2 it checks the rate
  and every stamp); the frame table is identical for both, so `compare` works
  across schemas.
- `presenter_capture.py` `load`: schema 1 or 2; the stamp is ignored.
- `scripts/dbsim_present_times.py <capture.ndjson | manifest directory>
  [--frames FIRST LAST] [--budget-ms 40] [--top 5] [--json]`: for schema 2,
  the present gap and the tick (a step of the frame counter `[0x4d257b]`;
  main-loop present site `0x45fbe4` ordinal 0) in ms, ticks over the budget
  (the limiter at `0x4678c0`), the slowest ticks. Exit 2 for an unusable
  capture.

### The runner

`capture-dbsim-frames.ps1 -Qpc -Label <label>` adds `capture_qpc=1` to the
run's capture INI and copies the raw capture to
`<run-dir>\<suite>\capture.ndjson`; the manifest's `run.qpc` and the session
record note it. The run must end `identical`.

### Tests

Proxy: `presenter_qpc_test`, `presenter_qpc_all_test`,
`presenter_qpc_off_test`, `presenter_qpc_ddraw_test`, with the schema-1
tests unchanged. Scripts: `dbsim_frames_unit_test`,
`dbsim_present_times_unit_test`, `presenter_capture_unit_test`;
`//tools/reference-game/setup:capture_dbsim_frames_test`. Mutations caught:
stamping at the write; `ddw_present` without an entry value; ticks counting
multi-frame steps.

### Live

Both runs (GDI and the DirectDraw branch) end
`identical`; the start record has schema 2 and `qpc_frequency` 10,000,000.

- acceptance-60s: main-loop tick median 3.55 ms, none over 40 ms.
- c3_10-detail-options: median 3.97 ms; two modal dialog frames (204, 600)
  carry nearly all the wall time.
- The clone runs **unpaced** in the replay (270–280 ticks per second), not
  near the 40 ms limiter; why the limiter at `0x4678c0` does not hold the
  tick there is not examined. Pacing comparison: [main_loop.md "The notices
  against the paced original"](../reverse-engineering/main_loop.md#the-notices-against-the-paced-original).
- DirectDraw branch: every record `"source":"surface"`, mode 1, at the GDI
  branch's call sites, stamps non-decreasing; against the GDI baseline
  `dbsim_frames.py compare` differs only in mode and source; median present
  gap 3.73 ms.

### The DirectDraw branch in a capture run

`capture-dbsim-frames.ps1 -WindowedDirectDraw -Label <label>` makes DBSIM
take the DirectDraw branch and the proxy answer it with its own objects
(presentation.md "DirectDraw branch in a window"):

- the capture INI gets `directdraw=windowed`; `presenter` stays unset;
- the game argument `-Z1` (`run-oracle-parallel.ps1 -GameArguments`) sets the
  DirectDraw flag `[0x4d255e]` at `0x45e706`; the clone's `prefs.cfg` and
  `es2-oracle.ini` are not touched;
- the manifest's `run` has `directdraw: "windowed"` and `game_arguments:
  ["-Z1"]`; the run is compared with the GDI branch's golden;
- **Trap:** the DirectDraw observer must be off (no `-Perf`, `-DeferFlip`,
  `-PresentationEvery`), else the return address of `Unlock`/`Flip` lies in
  the observer and no present is recorded.

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
2. The SEH chain on FS:0 holds DBSIM's frame records (handler `0x4967f0`,
   CW3220's `__ExceptionHandler`) and at the bottom CW3220's `__startup`
   record (rtl_imports.md "Exception handlers (static)"). The table entries
   DBSIM's 267 frame set-ups reach are of kind 5 (objects), kind 0 and one
   kind 3 (table `0x4a7948`, a catch-all that acts only for `0x0EEFFACE`);
   none is of kind 1 (`__except` filter) or 2 (static scan of the listing).
   So every DBSIM record answers ExceptionContinueSearch to a divide fault or
   an access violation. The `__startup` record calls the two user handlers,
   then `UnhandledExceptionFilter`.
3. The `__startup` record returns the filter's answer as its own disposition
   (verified `CW3220.DLL` `0x4051e6..0x4051fb`); the usual answer
   EXCEPTION_EXECUTE_HANDLER (1) reads as ExceptionContinueSearch, so the
   search goes on to the OS's thread-start record.
4. The OS ends the process with the exception code as exit code; no atexit
   functions, exit records or stream flush run.

**Live.** The original reaches step 4 with the divide faults and the NULL
access of the data inputs below, with the injected `new` failure `46162b:1`
(`0xC0000005`; rtl_imports.md "The exit path under the `new` failure, and
`check.ps1 -NewFail`") and with the tape playback fault at `0x453127`
(oracle_suites.md "Tape playback suites"). With the divide fault of
`zone-zero` the trace `proxy/fault_trace.h` (`ES2_FAULT_TRACE`, DBSIM's
import slot RVA `0x2cc33c`) shows three search calls at `0x46c208`, each
answering 1, then `UnhandledExceptionFilter` from CW3220 +`0x51f5`, then
nothing: no unwind, no detach record, exit code `0xC0000094`. **The process
ends before an unwind reaches DBSIM's records, so the frames' clean-up
`0x40415d` does not run** (rtl_imports.md
"The unwind clean-up at the process end"). The own `cw3220.dll`'s behaviour
under a top-level filter: `//rtl:cw3220_fault_test` (rtl_imports.md "The
exception paths of the own runtime"). The OS dialog or report is not part of
DBSIM.

### What es2sim does

- **The primitive.** `es2_sim_divide_fault(dividend, divisor, is_signed)`
  runs the real IDIV or DIV on the host CPU under its own first vectored
  handler, which steps over the instruction and keeps the OS's code:
  `0xc0000094` for a zero divisor, `0xc0000095` for a quotient past 32 bits,
  0 otherwise.
- **The primitive in C.** `es2_divide` (`core/include/es2/core/divide.h`)
  gives the code with the quotient and remainder the instruction leaves;
  `//sim:fault_paths_test` compares it with the CPU over edge values and
  20000 random pairs. The loop asks `cpu_fault.S` on Windows and
  `es2_divide` in the portable link. The core's host divides inside guest
  entries (shield balance, cockpit sync, type loads, HUD speed) check with it
  and on a fault call `es2_divide_raise`, a host divide that faults with the
  same code (x86 #DE into `host_fault.c`, in wasm the matching trap).
- **The process end.** `es2_sim_loop_divide_fault(l, site, ...)` asks the
  primitive and on a fault calls `es2_sim_loop_fault(l, code, site)`, which
  calls the record's `process_fault`. It refuses without a record, without
  `process_fault`, or when the operands do not fault (a model error).
- **The record.** es2sim always sets one (`window_process`,
  `headless_process`). `process_fault` prints `FAULT: guest exception
  0x<code> at 0x<site> (the original's unhandled fault);
  ExitProcess(0x<code>) without exit records`, closes the host's log with
  reason `guest_fault` and calls `ExitProcess(code)`.
- **Tests.** A recorder in the record returns; the loop ends EXITED with the
  code, and the call helpers keep EXITED. A NULL record keeps the refusals.

### Exit codes

| Code | Meaning |
|---|---|
| 0 .. 6 | the guest's, the dword `0x4d27b8` (WinMain returned) |
| 1 | the fatal report's `__exit(1)` |
| 3 | `_abort` after its box; also the guest's "mission over" |
| 64, 65, 66, 67 | es2sim: usage, image refused, range not claimable, set-up failed |
| 70 | es2sim: a refused call (a guest call without own code, or a case the model refuses, such as an exhausted own call area) |
| 71 | es2sim: an exception in es2sim's own code that nothing handled, also under the guest's frame records (below); a push or a shim gap below the guest stack's limit ([web_build.md](../web_build.md) 4.6) |
| `0xc0000094`, `0xc0000095` | the original's unhandled divide fault |
| `0xc0000005` | the original's access violation: a read or write of the NULL page inside a guest entry, or an own row's NULL access (below) |

The launcher passes es2sim's code to `es2l_next` as if it were DBSIM's;
`es2l_next` switches on the unsigned value, so a code at or above `0xc0000000`
is no state 1..6 and the chain ends, as ES2.EXE's does after a crashed DBSIM
(launcher.md "Exit codes").

### Host faults under guest frame records

es2sim links DBSIM's frame records (`0x49630c`) on the sim backend's FS:0
variable, never on the real FS:0 (section 4.4 item 4). The guard below stays
for any record with a handler in CODE on the real chain. **Trap:** a hardware
fault of es2sim's own code under such a record would never reach the
unhandled-exception filter: the OS refuses the no-access handler `0x4967f0`
(`STATUS_INVALID_EXCEPTION_HANDLER`, `0xc00001a5`) and ends the process
without a FAULT line. `sim/src/host_fault.c` (`es2/sim/host_fault.h`) adds a
vectored handler at the end of the list. It passes on what the first handlers
take (the trap's execute faults, the divide primitive, the image-access
guard), exceptions outside es2sim's module and codes without error severity;
for the rest `main.c` prints the FAULT line and ends with 71. The checked
accessor (web_build.md section 5) catches NULL-zone, no-access, read-only and
barrier accesses before the hardware does.

**The NULL page.** A read or write access violation below `0x10000`
(`ES2_SIM_NULL_PAGE_END`) while a guest entry runs
(`es2_sim_guest_entry_va(0)` nonzero) is the original's own fault: `main.c`
prints `FAULT: read|write of 0x<address> (the NULL page) in the entry
0x<entry>, host <address>` (or `..., guest accessor`) and ends through
`process_fault` with `0xc0000005` and the entry as the site. Execute faults,
addresses at or above `0x10000` and faults outside a guest entry keep 71.

`//sim:host_fault_test` (child processes with a no-access handler on FS:0;
the NULL page and the page above it; the ownership rules).

### NULL accesses

An own row that finds a guest pointer NULL where the original reads, writes
or calls through it below `0x10000` ends like the original:
`es2_sim_loop_null_refuse(l, va, name, reason)` (`loop.c`) calls
`es2_sim_loop_fault(l, 0xc0000005, va)` and, without a record that has
`process_fault`, refuses (exit 70). The site is the row's refusal VA. The
start-up's streams use `su_null_refuse` (`startup.c`); each file keeps a
`null_refuse` wrapper beside its `refuse` (HUD: `hud_null_refuse`).

Classified from the listing at each row's VA: 106 rows refuse a NULL or
out-of-range guest pointer; 93 fault in the original, one is a divide fault
(`mouse_scale` `0x480898`, client height 0, IDIV `0x4808cf`), 12 stay
refusals. An address out of range but mapped is no fault. Most rows are an
allocation that returned NULL followed by an access through it, reached only
on exhausted address space, in the original as in es2sim. Among the refusals: the
ninth release entry of the 8-entry button release list (`dialogs.c`; the
original faults at `0x453127` on the gadget pointers behind it, a known
difference), copies running through mapped memory (`0x486e10`, `0x465d0f`,
shade ranges, `0x406f69`), CW3220's heap on a foreign block (`free`,
`realloc`, `delete` in `services_rtl.c`), and rows that do not fault
(`0x479d0e`, `0x463765`, `0x46375c`).

### The sites

The VA in the FAULT line is the row's refusal VA: the IDIV or DIV itself, or
the call that reaches it; where one check covers two divides, the first.
Every DBSIM site is a signed IDIV; the SOS mixer's is the DLL's unsigned DIV
(`0x00406cf0`, at SOS9503.DLL's link address; audio.md "Resampling"). The DIV
`0x436f9c` of the EventPort frame `0x436f70` is no site: `xor edx, edx`
before it divides only the product's low dword by 60 (hud.md "The original's
slips").

Sites by group:
the 64-bit divides `0x47de2d`/`0x47eabe` (`0x47de3c`), `0x492b04`
(`0x492b0b`), the 16.16 divide `0x47e068` (`0x47e078`), the `poly.c`,
`fill.c`, `view.c` and `shape_draw.c` divides, the texture edge `0x48aba2`,
the frustum plane (`0x47de3c`), the horizon `0x42ed4e`, the terrain slopes
`0x46c208` and normalize `0x46c29e`, the joystick poll, the mouse scale
(`0x4808cf`), the fade and present divides, the HUD and cockpit rows, and the
guest entries es2sim runs on the host (shield balance `0x413bc8`, cockpit sync
`0x41b130`, type loads `0x4201a8`, `0x422ed0`, HUD speed through `0x42a1f0`),
whose site is the entry (`host_fault.c`).

**`0x47c82c`, the buffered read**, loops as the original does
(`while (avail < n)`, no cap): a stream that never fills its buffer hangs
until the replay driver's timeout or the window's close.

### Inputs that reach a site

Each site's divisor is traced back in the listing. Verdicts marked "reachable" are
seen headless in es2sim and live in the original; the others are
hypotheses from the listing. The data inputs are files the mission load reads;
DBSIM's opener `0x4730e8` opens a loose file in the working directory before
the VOL (vol.md "Lookup and loose files"), as es2sim's `vfs_stream.c` does.
`sim/tests/divide_scenarios.py` writes each input (`write <scenario> <dir>`,
`remove`) or replays with it (`replay <scenario> <replay_suite.py
arguments>`).

**Reachable sites and their inputs** (original and es2sim end at the same
site with the same code; offsets are from `0x400000`):

| Input | Site | Code |
|---|---|---|
| `zone-zero`: c1_01's `dat\zone123.dat` with the fourth dword 0 | `0x46c208` terrain slopes | `0xc0000094` |
| `zone-flat`: fourth dword 16385, a flat cell's normal is zero | `0x46c29e` normalize | `0xc0000094` |
| `zone-overflow`: zone DAT (31, -1) and a loose `dat\mat0.dat` with every level 22 | `0x46c208` | `0xc0000095` |
| `zone-null-cell`: zone DAT (31, -1) with the VOL `mat0.dat` | `0x471455` (es2sim `0x47136c`), the base-mark raster reads `[0 + 0xf]` | `0xc0000005` |
| `map-rows`, `map-columns`: script.dat points at the 32-bit edges | `0x44f8f3`, `0x44f905` map set-up | `0xc0000094` |
| `map-page-columns`, `map-page-rows`: panel layout `gau\<herc>.GAU` | `0x44c461`, `0x44c47f` | `0xc0000094` |
| `heading-tape`: `L+0x458` = `L+0x450` | `0x43b7f3` | `0xc0000094` |
| `map-scale`: every script.dat x equal | `0x48c63b` projection | `0xc0000094` |
| `frustum-far`: zone third dword 18 and `mat0.dat` levels 3 | `0x47de3c` | `0xc0000094` |
| `shield-zero`: `dat\<herc>.DAT` `+0xbe` = 0 | `0x413c2c` (es2sim: entry `0x413bc8`) | `0xc0000094` |
| `forward-speed-zero`: `+0x04` = 0 | `0x41b26b` (entry `0x41b130`) | `0xc0000094` |
| `speed-scale-zero`: `+0xc2` = 0 | `0x47e078` (entry `0x4201a8`) | `0xc0000094` |
| `flight-model-zero`, `flight-model-overflow`: FM+0x2e = FM+0x2a (or − 1) | `0x47e078` (entry `0x422ed0`) | `0xc0000094`, `0xc0000095` |
| `cell-anim`: SIMVOL0.VOL with every TSCellAnimPart child count 0 | `0x47691c` | `0xc0000094` |
| `hud-speed-zero` on `c4_08-razor-plasma`: FM+0x2a = 0 | `0x47df7a` (entry `0x41bb3c`) | `0xc0000094` |
| `ladder` on `c4_08-slope-dive`: zone 508, DAT fourth dword -50 | `0x43decf` (es2sim `0x43dd98`) | `0xc0000094` |
| `throttle` on `c1_01-mouse` with a click on the throttle | `0x452660` (tick 104) | `0xc0000094` |

**Trap (`zone-null-cell`).** A zone DAT meant for the `0x46c208` overflow
never reaches the slopes: the base-mark raster `0x47136c` runs first, the
third dword makes its shift 18, the cell index leaves the grid and the cell
pointer read there is 0. The raster takes its shift counts mod 32, so a
`mat0.dat` with level 22 keeps both cell indices at 0; `zone-overflow` adds
it.

Live (WER events name `DBSIM.EXE` of the clone with the predicted
code and fault offset): `zone-zero` with all hooks replaced; the other inputs
with the original runtime and all hooks original; es2sim reproduces the
faults. The clone's script.dat
and SIMVOL0.VOL hashes are unchanged after every run. With the own runtime
and the hooks replaced, `zone-null-cell` faults in the proxy (`DDRAW.dll`)
instead: the NULL read happens in the replaced code.

### Deviations

- **No SEH walk.** The guest's records point into no-access CODE, so es2sim
  ends the process itself at the fault; no DBSIM record handles a divide
  fault or an access violation, and the original ends before an unwind runs
  `0x40415d`. No clean-up runs in either.
- **No OS report.** es2sim prints the FAULT line; the exit code is the
  original's.
- **The site** is es2sim's refusal VA, not always the faulting instruction;
  for a divide on the host inside a guest entry it is the entry.

### Acceptance

`//sim:fault_paths_test` (the primitive, the loop helpers, the divide rows,
`null_paths`), `//sim:hud_test` `event_pause`, `//sim:es2sim_real_test`,
`//sim:es2sim_window_run_test`, `//sim:replay_suite_test` (a guest fault is
the verdict FAULT, IDENTICAL against a golden with the same code),
`//sim:load_test` `load_rest`, `//sim:divide_scenarios_test`,
`//sim:host_fault_test`, `//sim:input_test` `mouse`, `//sim:sos_mixer_test`,
`//tools/reference-game/proxy:fault_trace_{arming,calls,raise,probe}_test`,
and the `divide_scenarios.py replay` runs of every input above. No golden
reaches a fault site, so the replays are unchanged.

### Open

- **The bounds marked as hypotheses** in the per-site table (near-plane
  crossings, `angle_ratio`'s full-width callers, the spot light,
  `idiv_rest`) and the two data-reachable sites without a scenario: the
  sprite part `0x47664f` (a zero-width DBA bitmap) and `explosion_update`
  `0x40813c` (a zero `seqLenList` entry).
- **The NULL-access rows** are judged from the listing, not by a live run;
  `su_null_refuse` has no unit test of its own.
- **`main.c`'s start-up EXIT branch** is unreachable (es2sim always sets a
  record and its `process_exit` does not return); kept as the fallback for a
  record that returns.

## The heap trace

The heap at DBSIM's entry already holds CW3220's attach blocks, and es2sim
makes the same blocks in the same order, so every later block lies at the
original's offset in its reservation and reads past a block's end find the
original's bytes ([The free camera's shape
edge](#the-free-cameras-shape-edge)). Code: `sim/src/heap.c`,
`sim/include/es2/sim/heap.h` (`ES2_SIM_HEAP_RESERVATION_GAP`,
`es2_sim_heap_runtime_malloc`/`_free`, `es2_sim_heap_copy_environment`,
`es2_sim_heap_static_objects`, an observer of every operation);
`sim/src/services_files.c` (the file services on the game's heap; there is no
second heap); `sim/src/host.c` (`es2_sim_runtime_attach`, `[trace]
heap_trace`); `tools/reference-game/proxy/rtl_heap_shadow.h` (`[rtl]
heap_trace`); `tools/reference-game/scripts/heap_trace_compare.py`.

### The trace

The proxy and es2sim write the same NDJSON lines:

- **attach**: DBSIM's frame counter (`g_sim_ctx + 0xbf`), the allocator's
  variables (sentinel, rover, committed and reserved end, granularity,
  platform), the block count, the regions, the lengths of each environment
  string and of the command line (never contents);
- **block**: one line per block as DBSIM's entry finds it (address, size,
  in-use bit);
- **op**: index and frame; the function and its origin (`import`: DBSIM
  through its imports, new and delete via `0x47747c`'s calloc and `_free`;
  `internal`: CW3220's own call); the caller's return address, both
  arguments, the result and the rover; reservations and releases;
- **end**: the operation count and the lines lost.

The proxy writes with `[rtl] enabled=1`, `calloc`, `realloc` and
`free=shadow`, and `heap_trace=<path>`; es2sim with `[trace]
heap_trace=<path>`.

`heap_trace_compare.py ORIGINAL ES2SIM` pairs the operations in order and
reports the first operation where function, origin, request size or the
import's caller differ (a caller of 0 is not compared: es2sim's own code calls
the heap too), the double frees on both sides, and the low 16 bits of each
result as runs (the 4 MiB reservations are 64K-aligned, so the low word is
the block's offset in its reservation). `--find SIDE:ADDRESS` names the
operation that made a block. Unit test:
`//tools/reference-game/scripts:heap_trace_compare_unit_test`.

### What the original does (c1_01-free-camera, live, clone timing-r111-p1)


- **At DBSIM's entry**: 116 blocks in one region, the top one free. In address
  order they are CW3220's init records (table `0x423084`, by priority):
  `0x41a050`, the environment copy (the pointer array, then one
  `_malloc(strlen + 1)` per string); the stdin and stdout buffers, two
  `_malloc(512)`; the C++ static objects of `0x409f98` (three filebufs,
  new(0x3c) and new[](0x204) each), `0x418444` (new[](0x100)) and `0x417dc8`
  (the xalloc message: new(0x12), `_malloc(64)`, new(4)).
- **The second reservation** is not adjoining (`0x04440000`, not
  `0x02630000 + 4 MiB`), so `__heapadd` opens a second region and the growing
  block starts at its base + 8.
- **Op 4948** frees a block that is already free: the game's double free.

### What es2sim does

- **The attach** (`es2_sim_runtime_attach`, before the c0w32 entry) makes the
  same blocks in the same order from es2sim's environment: the copy; the file
  services' two buffers (`es2_sim_files_init`); the ten static-object blocks.
  Only the sizes are modelled: CW3220's vtables and fields in those blocks are
  read only by its iostream and string code, which DBSIM does not import.
- **The file services' blocks** (setvbuf `0x405384`, fclose `0x40555c`, the
  find nodes) come from the game's heap through CW3220's `_malloc` and
  `_free`; not zero-filled, without the replaced heap's modelled registers
  (rtl_imports.md "Registers and stack residue").
- **Reservations at the preferred base** never adjoin: the next starts
  `ES2_SIM_HEAP_RESERVATION_GAP` (`0x10000`) above the end of the last one, a
  multiple of 64K, so the low word stays the offset.

### Result

With the attach, the gap and the original's environment size, es2sim on
c1_01-free-camera matches the original's trace: 7640 operations equal and
every result's low word equal. The double free at op 4948 is found
on both sides.

- **The rest is the environment.** With another environment the copy has
  another length and every block of the first region shifts by the
  difference. The environment is process input (the after-load snapshot's
  `environment` class); the reconstruction does not pad it.
- **The replay's environment.** The replay driver pads it on every run because
  the frame references were captured with the original's environment:
  `replay_suite.py` keeps es2sim's `SYSTEMROOT`, `PATH`, `TEMP`, `TMP`, `TZ`,
  `ES2SIM_*` and `SDL_*`, and adds variables `Z000`, `Z001`, … until the copy
  has `frames_known.REFERENCE_ENVIRONMENT`: 102 strings in 5648 bytes of
  string blocks, the original's. The block goes to es2sim as `--environment
  FILE`, in the bytes `GetEnvironmentStringsA` returns (names sorted
  upper-cased, ANSI code page, each string NUL-terminated, then one NUL);
  es2sim copies that block instead of its process environment.
  `--padded-environment` (with `--frames`) passes the same variables as the
  process environment instead; both give the same frames, heap trace and tick
  log ([the host OS layer](#the-host-os-layer)). **Trap:** with fewer bytes (a
  minimal or Bazel environment) `c1_08-scan` and `c1_08-escort-route` differ
  in the frame compare.
- **The free camera.** `//sim:frames_c1_01-free-camera_test` passes with all
  1001 sequence records equal.

**Acceptance.** `//sim:heap_test` (the gap; the attach blocks and their
sizes; an empty environment), `//sim:services_test`,
`//sim:frames_c1_01-free-camera_test`, `heap_trace_compare_unit_test`.

**Open.**

- **The 103rd string**: the proxy's run lists one environment string more
  than CW3220's copy; which variable joins after the attach, and whether a
  plain DBSIM start has it (it changes only the low words).
- **The own cw3220.dll** makes no environment copy and no static objects.
- **Other suites.** Only c1_01-free-camera was traced live.

## Called gap roots

The roots of [sim_gap.md](sim_gap.md) "Called by own code (the roots)" and
the functions behind them, each with its outcome in es2sim. "Listing" =
llvm-objdump -d -M intel of the reference DBSIM.EXE. Rows:
`sim/dispatch/services/adapter_leaves.h` with `sim/src/adapter_leaves.c`. A
refusal means the VA keeps no row and the dispatch refuses a call to it.

### The live root `0x4968f8`

`0x4968f8` is `jmp [0x6cc3f0]`, the GetProcAddress thunk. Its callers are
`0x495972` and `0x4959e2`, the two DLL branches of the SOS driver loader
`0x4958fd` (97 names each), and `0x49646b` in `0x496425`. Its hits in every
suite of the live map are the original's: the loader runs at every start.
es2sim never calls the VA:

- `0x4958fd` is own code (`sim/src/audio.c` `dll_load`): no DLL, the stand-in
  handle `0x50500000` in `0x6cbd58`, the 97 slots of `0x4a6a9c` untouched.
- `0x496425` is own code (`sim/src/init.c` `es2_sim_c0w32_entry`). It calls
  `0x4968f8` only when the byte `0x497058` is set; the byte is 0 in the
  image, and the listing has one reference to it, the read at `0x49644f`,
  and no writer.

**Refused on purpose**; a row would serve a path that no run takes.

### Rows

Six rows.

- `0x401148` `camera_mode` (`0x401148..0x40119e`): reached only through a
  masked branch of the playback keys `0x45fe6c` (Shift+V word `0x49eab8` =
  `0x47ff`, no writer). Word C+0x36 ← mode for movsx(mode) 0..3 unless dword
  C+0x4a ≠ 0; EDX -1 (mode 0), 0 (1..3), mode − 3 otherwise.
- `0x4967fc` `file_time`: CW3220's `_time`, [_time in es2sim](#_time-in-es2sim).
- `0x423044` `flyer_slot`, `0x427d98` `sweep_clear`, `0x427da0`
  `sweep_read`: computed inline by core (`fm_flyer_slot`, `sweep_leaf`);
  the rows serve a direct call. `0x423044`: EAX = `[0x4a9dfc]` +
  18·movsx(type) + 6·movsx(slot) − 6, EDX movsx(slot), ECX 6·movsx(slot).
- `0x467ad8` `debug_break` (`int3; ret`): Ctrl+B with `-B`. No DBSIM handler
  takes EXCEPTION_BREAKPOINT, so without a debugger the process ends as on a
  divide fault ([The original's fault paths](#the-originals-fault-paths)).
  The row calls `es2_sim_loop_fault(l, 0x80000003, 0x467ad8)`; without
  `process_fault` it refuses. Not compared in vitro (it would end the test
  process); no live run of the original has reached it.

In vitro: `differential_adapter_leaves_test`
(`adapter_leaves_cases.h`, 2,000 random cases, every coverage counter
non-zero). Unit `//sim:adapter_leaves_test`.

### Refusals

- `0x4968f8`: no own path calls it (above).
- `0x42900c`, `0x42937c` (and `0x428f38`, `0x42933c` behind them),
  `0x475900`: dead
  ([SIM, R3D and HUD gap roots as rows and verdicts](#sim-r3d-and-hud-gap-roots-as-rows-and-verdicts)).
- `0x464d44` and the eight import thunks behind it only: caller replaced;
  `main.c` always sets `own_dib` (`es2_sim_dib_section`).
- `0x49679c` `__exit`, `0x49688c` `_abort`: served by the process record
  (`process_exit`); the dispatch sees them only from a loop without a record,
  where the refusal is the tested answer.
- `0x4667c8`: row `fullscreen_switch`
  ([presentation.md](../reverse-engineering/presentation.md#the-directdraw-full-screen-switch));
  its only callees `0x46658c`, `0x4665c4`, `0x45f13c` are inlined, and its
  window calls are served inside it.

## Remaining virtual targets

The SIM, R3D and FILE entries of `sim_gap.md` "Virtual: targets of the
function-pointer tables that sim/src names" that have neither a row nor a
hook. None gets a row; each is refused below. The HUD entries are in
[hud.md, "The HUD's remaining virtual
targets"](../reverse-engineering/hud.md#the-huds-remaining-virtual-targets).

### Reachability

In es2sim a table slot is reached only when own code calls that slot on an
object that carries the table. The reading covers every slot call in
sim/src: the helpers `vcall` (load.c, view.c), `vcallr` (fill.c), `VCALL`
(scene.c), `fvcall` (services_files.c), `HVCALL`/`hud_vcall`
(hud_internal.h), `WSLOT0` (hud_display.c), `port_call` (display.c), and the
raw `r32(r32(x) + off)` calls (audio.c, gfx.c, hud_draw.c, mfd.c, present.c,
shape_draw.c).

The own deletes with flag 3 through `+0x18`: cockpit.c `drop_release` (the
intro palettes), dialogs.c `owned_free` (the dialog's bitmap and the GLCursor
holder's inner object), fill.c `gl_holder_destroy` (`0x4835c8`),
hud_cockpit.c `destroy_shape`, widget_leaves.c `three_shape_destroy`, load.c
`bitmap_array_destroy`. The other `+0x18` calls are stream reads or widget
slots called without a flag. The own slot-0 calls (gadgets and dialog draws,
widget parts, G+0x18 and G+0x1c, blips and bars, draw lists, tag reads) are
on no light, camera or view object.

The Borland RTTI descriptors in CODE hold the address of every class
destructor (the second reference of each destructor below). Only the RTL
unwinder reads them, and the guest records (handler `0x4967f0`) never run in
es2sim (§4.4, item 4).

### Refusals

| VA | Table (slot) | Reason | Evidence |
|---|---|---|---|
| `0x40798c` | `0x497adc` (0) | light destructor never called | the table's only writer is `0x406e8a` (load.c `light_table`); the light at T+0x328 is embedded; own code calls lights only at slot 1 (`0x48bcec`) |
| `0x420730` | `0x49a494` (6) | destructor never called | mounts are built by `mount_ctor` (`0x42067c`) and never deleted; the element destructor `0x420867` reaches it only through `_vector_new_`'s unwind (`0x42057f`) |
| `0x403020` | behind `0x420730` | dead with `0x420730` | its other callers are not own paths |
| `0x4275fc`, `0x427714` | `0x49aaa0` (1, 4) | store closure | the GridShape's sizes and write; own code calls the grid only at slot 3 (`grid_new`) |
| `0x42786c` | `0x49aaa0` (6) | destructor never called | no own `+0x18` site deletes the grid |
| `0x462107`, `0x4621a2` | `0x49f8c0`, `0x49f8d4` (0) | destructors never called | the spot light and the light base, called only at slot 1 |
| `0x462254` | `0x49f8e8` (0) | destructor never called | view objects over a camera are torn down inline |
| `0x4715ef` | `0x4a0ca0` (2) | stack object, torn down inline | `0x46bd54` builds `{0x4a0ca0, word 7}` at ebp-0xc for `0x42e8e8` and `0x48d5c0`; `shade_ranges` calls no slot of it |
| `0x48302c` | `0x4a3264` (3) | read slot of an object never loaded | GLRectangleRegion (tag `0x0028000c`) is read only after its factory `0x482f94`, which has no row; own code builds regions by constructor. A byte scan finds the tag 13 times in SIMVOL0.VOL, none at an object header |
| `0x483048` | `0x4a3264` (4) | store closure | only `0x47a1ac` calls it |
| `0x48326d` | `0x4a3264` (6) | destructor never called | regions are embedded in widgets and dialogs or kept in the view-region arrays |
| `0x4833f4`, `0x483414`, `0x483458` | `0x4a3340` (1, 2, 4) | store closure | own code calls the GLCursor holder only at slots 3 (`holder_read`) and 6 |
| `0x483790` | `0x4a3040` (6) | base table overwritten | every writer of `0x4a3040` writes `0x4a306c` next, except the map icon's stack object (`0x44f321`), which gets `0x49d984` |
| `0x483924` | `0x4a3390` (6) | base table overwritten | each of the 21 writers writes the derived table next (`0x4a3264` or `0x4a33b8`) |
| `0x48d94d` | `0x4a5b29` (0) | destructor never called | the camera class; no own slot-0 call is on a camera |

**FILE, the save closure.** `0x47a1ac` saves an object and `0x47a55c`
returns its stored size; in the original only the S/W slots call them
(`0x482e24`, `0x482ef7`, `0x4833fe`, `0x483479`, `0x48e96f`, `0x48ebcc`,
`0x48fe39`, `0x48ff47`, `0x491b30`, `0x491d43`, `0x492209`, `0x492290`). In
es2sim the rows `gl_bitmaps_size_a` (`0x482e08`) and `gl_bitmaps_write`
(`0x482ed0`), slots 1 and 4 of the GLBitmapArray table `0x4a32ec`, call them,
but no own code calls `+4` or `+0x10` on a GLBitmapArray, so such a call
would end at the refusal of `0x47a55c` or `0x47a1ac`. Behind the two, all
reached only from inside the closure: `0x47a638`, `0x47a798`, `0x47c284`
(builds the closure's writer, table `0x4a1ab0`), `0x47c2c4`, `0x47c314`,
`0x47c360`, `0x47c388`, `0x47c570`. A static reading; none has a live hit in
the map; not checked in vitro or live.

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
`&t`, reads the stored `t` (not EAX), sign-extends the demo count word
`0x4d26f0` and takes `t % count` (`cdq; idiv`) as the index into the word
`0x49f034`. The pick does not seed the RNG. `_time` is GetLocalTime, the
seconds since 1970 plus `_timezone`, minus 3600 when `_daylight` is set and
the US rule says summer, and -1 for a value ≤ 0; in core `rtl_io`
`ES2_IO_TIME` (in vitro: `rtl_io_cases.h`).

**Row.** `file_time` (`sim/dispatch/services/rtl_time.h`, in
`services_files.c`) calls `ES2_IO_TIME` on the files model; it refuses when
the file services are not set up or the clock answers month or day 0.

**Clock.** Headless, in a journal replay or with `[run] time` set:
`[run] time=<0..2147483647>`, else 852076800 (1997-01-01 00:00 UTC, DEMO1 of
the CD's three), zone UTC. Windowed without a replay or `[run] time`: the
wall clock with `_tzset` over the process environment. The journal does not
carry the clock, so a replay of a `-D` capture picks its demo through
`[run] time`.

**Tests.** `differential_gap_roots_shell_test` serves `0x4967fc` with the
own row; `//sim:rtl_time_test` (default, NULL argument, fixed and random
seconds against FileTimeToSystemTime, 0 gives -1, the wall clock against a
GetLocalTime bracket). Headless `-D` runs per demo: DEMO1 ends with a refusal at
`0x4530bc` where the original faults at `0x453127`
([hud.md](../reverse-engineering/hud.md#the-energy-pod-gauge-creator-40e40c)).

### Open

- The wall clock is not compared live: no windowed `-D` run of es2sim or the
  original has been recorded against it.

## The host OS layer

`//platform:os` (`platform/include/es2/platform/os.h`) is where es2sim,
es2shell, es2menu and es2launch reach files, streams, clocks, the calendar,
memory reservations and protection, the environment, INI files and the
process end ([web_build.md section 7](../web_build.md#7-the-host-os-layer)).
`os_win32.c` calls the API with the same arguments; `os_wasm.c` declares the
web build's imports. Handles are the API's values on Windows.

- **INI files.** `platform/src/ini.c` follows `GetPrivateProfileStringW`/`IntW`
  and `GetPrivateProfileStringA` for the keys the code reads: lines end at LF
  or CR; names and values trimmed; a section ends at the first `]`; only the
  first section of a name is searched and the first key wins; one pair of
  surrounding quotes is removed; a UTF-16LE BOM selects UTF-16, otherwise
  cp1252; the integer reader takes the first 256 units and C prefixes in
  lower case only. `//platform:ini_test` compares it with the API. On Windows
  the code still calls the API.
- **The environment.** `--environment FILE` (es2sim, es2sim_window,
  es2shell) loads an environment block; `es2_os_environment` and
  `es2_os_getenv` then use it and not the process environment
  ([the heap trace](#the-heap-trace), "The replay's environment").
- **The C runtime.** Streams are the CRT's `FILE`, opaque to the callers;
  console text stays standard C.

**What stays Win32** (every Win32 call left in `sim/src`, `shell/src` and
`launcher/src`), by reason:

- `sim/src/os_process_win32.c`: the image range claim before the CRT
  (`VirtualQuery`, `claim_image_range`, shared with the in-vitro harness);
  the last-chance filter and the stack base (`SetErrorMode`,
  `SetUnhandledExceptionFilter`, `NtCurrentTeb`); the simulation thread with
  its 16 MiB stack (`CreateThread` and the waits); the keyboard layout and the
  guest's message box (`MapVirtualKeyA`, `ToAscii`, `MessageBoxA`).
- `dispatch_trap.c`, `cpu_fault.c`, `host_fault.c`, `image_access.c`: x86
  exception mechanics (vectored handlers, DEP policy, module handles).
- `audio_sdl.c`, `platform_sdl.c`, `joystick_sdl.c`: MCI CD audio and its
  notify window, the scan-code map, the joystick API.
- `shell/src`: the online manual and the MCI movie player, VSHELL's message
  boxes, `_stricmp`.
- `launcher/src/es2launch.c`: ES.EXE's memory check (`GlobalAlloc`,
  `GlobalMemoryStatus`, `GlobalFree`), the child processes and their
  `__COMPAT_LAYER` environment, ES.EXE's message box.
