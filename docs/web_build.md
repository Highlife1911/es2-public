# The web build

The WebAssembly build of the reimplementation: its design and its acceptance tests. The Windows
build's process, dispatch and fault paths: [own_process.md](reference-game/own_process.md).

The web build runs the same C code as the Windows build. Three layers differ: the guest-call
mechanism (one portable dispatch for both builds, section 4), the host OS layer (files, clocks,
heap, process end, section 7) and the platform layer (window, input, sound, movies, CD music,
sections 9 to 12).

## 1. Targets and fixed parameters

| Parameter | Value |
|---|---|
| Browsers | Current Chrome and Firefox on desktop. Any other browser runs only if it has every feature the landing page checks, AV1 decoding included; there is no fallback format. |
| Hosting | A static host without header control (GitHub Pages class): at most 100 MB per file and about 1 GB per site. |
| Isolation | Cross-origin isolation (COOP `same-origin`, COEP `require-corp`), set by the page's own service worker. The first visit reloads once. |
| Input | Keyboard and mouse; no joystick. |
| Game data | Built locally from the user's extracted CD by `tools/web/build_site.py`, never committed. |
| Video | AV1 in WebM, visually lossless against the Windows build's decode; movie sound in Opus. |
| CD music | Opus, tracks 2–7 (the game plays 2–6), cut from the raw CD image. |
| Pictures | Not converted: no game code reads a BMP. |
| All other files | The original bytes, zstd-compressed per part and decoded in the worker. |
| Saves | Kept in IndexedDB. The `SAV` folder of an installed game can be uploaded and is taken over unchanged, even from another language. Saves export as a ZIP; the installation can be reset. |

## 2. Overview

```
main thread (page)                    game worker                           audio thread
------------------                    -----------                           ------------
landing page, canvas, <video>,        launcher loop (JS + launch_logic)     AudioWorklet:
input capture, fullscreen,     SAB    one wasm instance per process:  SAB   PCM rings -> output
IndexedDB, AudioContext,     <----->  es2shell, es2sim               ----->
CD music playback, dialogs            VFS: manifest, parts, overlay;
                                      zstd and SHA-256 in wasm
```

### Processes

| Windows | Web |
|---|---|
| `es2launch.exe` (ES.EXE's loop) | The worker's launcher loop (`web/launcher.mjs`). Every decision comes from `launcher/src/launch_logic.c` compiled into `launch.wasm` (exports: section 14). |
| `es2shell.exe` (VSHELL) | A fresh instance of `es2shell.wasm` per start, with the argv that es2launch builds. |
| `es2sim_window.exe` (DBSIM) | A fresh instance of `es2sim_window.wasm` per start (`//sim:es2sim_window_wasm`: the same sources with the web platform in place of SDL's). |
| exit code | The instance's exit status, or the fault mapping of section 6. |
| files between the processes | The same files in the VFS overlay. To the simulation: `script.dat`, `MISSION.STR`, `mission.var`, `player.mec`, `prefs.cfg`. Back: `results.dat`, `mission.var`. |

A fresh instance per process gives it fresh static data, as on Windows. The memory check gets the
totals `GlobalMemoryStatus` returns to the non-large-address-aware `es2launch.exe` on the reference
machine (`//launcher:memory_probe`; `REFERENCE_MEMORY` in `web/launcher.mjs`): physical total
2,147,483,647, page file total and available 4,294,967,295, and the 20,000,000 bytes of the
allocation loop.

The worker side is `web/game_worker.mjs` (`runGame`: the loop over `launch.wasm`, one Vfs for all
instances, the worker's own files below `C:\ES2HOST` that never reach the store: the shell's
`menu.ini` and the environment block es2sim copies); `web/worker.js` runs it in the browser,
`web/game.mjs` (`startGame`, `GameRequests`) is the page's side and `web/node/game_host.mjs` runs
the chain under Node in a `worker_threads` worker.

**Process start and end.** On the worker's `process` request the page queues `INPUT.START` and then
the window's activation (`FOCUS` 1 while the page has the focus); the worker drops the input ring up
to `INPUT.START`, so a process starts with an empty queue. The game ends with an error on `failed`,
on a throwing request handler and on the worker's `error` or `messageerror`; the page then waits for
the last overlay writes and releases the worker, the unload guard, fullscreen and both
AudioContexts.

## 3. Execution without an asynchronous main loop

All C code stays synchronous (no ASYNCIFY, no JSPI, no pthreads): the game loop and every nested
modal loop run unchanged in the worker, which blocks where Windows blocks. `sleep`, the waits inside
`peek` and the frame limiter call `Atomics.wait` on a control word with a timeout; new input makes
the main thread call `Atomics.notify`, which ends the wait early, as an arriving message does on
Windows. A message box, a movie, a CD command or a file to persist is sent with `postMessage`; where
the C caller needs the answer, the worker waits on the reply word in the SAB. The wasm memory is not
shared; the worker copies frames and samples into the SAB.

**The SharedArrayBuffer** (`web/shared.mjs`, `LAYOUT`): one buffer of 10,093,056 bytes, every region
on a 64-byte boundary. Ring counters are free-running uint32; a position is the counter modulo the
ring size.

| Region | Size | Content | Direction |
|---|---|---|---|
| control | 64 Int32 words | the words below | both ways |
| keys | 256 bytes | the key bitmap, one byte per VK: the asynchronous key state | main → worker |
| input ring | 4096 entries of 8 int32 | `type, a, b, c, d, time` and two reserved words (`es2_web_input`); a full ring drops the new entry | main → worker |
| frames | 2 × 1280×960×4 bytes (the DIB maximum) | RGBA; width, height and state per slot in the control words | worker → main |
| shell sound ring | 8192 frames of float stereo | the shell's mixed samples | worker → worklet |
| simulation audio ring | 65,536 bytes | the mixer's 22,050 Hz stereo U8 frames | worker → worklet |

Control words: `WAKE` 0 (incremented and notified after each input entry), `INPUT_WRITE` 1,
`INPUT_READ` 2, `FRAME_LATEST` 3 (newest slot, -1 before the first), `FRAME_SEQ` 4 (notified per
frame), `SLOT_STATE` 5–6 (0 free, 1 the worker writes, 2 the page reads), `SLOT_WIDTH` 7–8,
`SLOT_HEIGHT` 9–10, `REPLY_SEQ` 11 (last answered request, notified), `REPLY_VALUE` 12,
`SOUND_WRITE` 13 and `SOUND_READ` 14 (shell sound frames), `SIM_AUDIO_WRITE` 15 and `SIM_AUDIO_READ`
16 (bytes), `FOCUS` 17, `VISIBLE` 18, `SLEEP` 19 (never notified), `MCI_FIELDS` 20–24 (the five
fields of an MCI reply).
The worker publishes a frame's slot before it counts the frame, and the page reads `FRAME_SEQ`
before `FRAME_LATEST`: otherwise the page could take an older slot under a newer count and keep
the old picture until the next frame.

Input types: `KEY` 1, `MOVE` 2, `BUTTON` 3, `FOCUS` 4, `EXPOSE` 5, `CLOSE` 6, `QUIT` 7, `MCI` 8,
`SIZE` 9, `WHEEL` 10, `START` 11. Key flags: `UP` 1, `REPEAT` 2, `SHIFT` 4, `ALT` 8, `CTRL` 16,
`NUMLOCK` 32. MCI notifications: `SUCCESSFUL` 1, `SUPERSEDED` 2, `ABORTED` 4, `FAILURE` 8.

**The message protocol.** The page's first message is `{kind: 'start', sab, overlay, language,
layout, run}`. The worker's messages:

| Message | Meaning |
|---|---|
| `{kind: 'process', seq, program}` | a process starts (`'shell'` or `'sim'`); answered once the page has queued `INPUT.START` and the window's first input |
| `{kind: 'process_end', program, code}` | it ended; the music and a movie stop without a notification |
| `{kind: 'persist', changes}` | one Vfs operation's overlay changes (section 8.5) |
| `{kind: 'message_box', seq, text, title, flags}` | a modal dialog; the answer is the button |
| `{kind: 'movie_play', seq, ...}`, `{kind: 'movie_stop'}` | section 11; `movie_play` answers 1 or 0 |
| `{kind: 'mci', seq, ...}` | a CD music command (section 10) |
| `{kind: 'cursor', ...}`, `{kind: 'fullscreen', on}`, `{kind: 'display', fullscreen}` | cursor, fullscreen and display requests |
| `{kind: 'manual'}` | the online manual: the project's page in a new tab (section 13) |
| `{kind: 'log', text}` | a console line |
| `{kind: 'end', state, files}`, `{kind: 'failed', text}` | the worker is done (`files`: the self-test run's collected files), or could not start |

A message with `seq` is a request: `request()` in `shared.mjs` posts it with the number after
`REPLY_SEQ` and waits on `REPLY_SEQ`; the page answers with `reply` (value, then sequence, then
notify), so a new process never takes a stale answer for its own.

## 4. One dispatch for both builds

### 4.1 Starting point

`es2sim` never executes guest code: every call into CODE reaches a C replacement (a hook, a service,
a headless row or a refusal). The x86-specific ways to get there (the stage-1 trap, a guard only:
4.8; `hook/machine.S`; `call *` bridges in `tools/reference-game/proxy/*_hooks.h`; the proxy's naked
capture of the entry shims, 4.4; the real FS:0; x87 state with ST0 results) have the portable
replacements below.

### 4.2 The machine

```c
typedef struct es2_machine {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
} es2_machine;
```

`hook/include/es2/hook/machine.h` declares `es2_machine`, the guest stack (base, limit), the FS:0
head and two calls: `es2_machine_call(machine, va, abi, args, count)` and
`es2_machine_call_planted(machine, va, abi, args, count, plants, plant_count, entry)`, which first
writes up to `ES2_MACHINE_MAX_PLANTS` (16) residue bytes (`es2_machine_plant`: offset, byte) at the
callee's entry ESP minus the offset; `*entry` gets that entry ESP (the return-address slot), 0 when
the call is refused. ABIs: CDECL, STDCALL, BORLAND (EAX, EDX, ECX, then stack slots); results in EAX
or EDX:EAX. `//hook:machine_test` runs the same calls through both backends and compares registers,
stack bytes and results for every ABI and result kind.

| Backend | Linked into | What a call does |
|---|---|---|
| **x86** (`hook/machine_x86.S`) | the proxy `ddraw.dll`, the in-vitro harness | Loads the eight registers, switches ESP when asked, calls the VA, stores the registers back, saves and restores the x87 state. The original code runs. |
| **sim** (`sim/src/machine_sim.c`) | es2sim on Windows and in wasm | Pushes the arguments and a return sentinel onto the modelled guest stack, looks the VA up in the dispatch table, calls the entry's C function with the machine and pops per ABI. Counts the guest-entry depth (section 6). |

- `m->esp` is the stack to push on; 0 means the current one (x86: the host stack; sim: below the
  innermost machine call's ESP, at the outermost level the guest stack's top). It is unchanged
  afterwards. Callers set every register they pass. BORLAND takes `args[0..2]` into EAX, EDX, ECX
  and pushes the rest left to right; the callee's EAX, ECX, EDX, EBX, EBP, ESI and EDI come back.
- The sim backend refuses an unlisted VA, fewer slots than the entry reads, a stack cleanup that is
  not the call's ABI's, more than 16 arguments or plants, and a call without a guest stack: exit 70
  through the dispatch's refusal callback. A push below the guest stack's limit, a shim gap or a
  probe loop that passes it is a model error: exit 71.
- The dispatch has a fourth row kind, *entry*: `es2_sim_entry_fn(es2_machine *, user)` with its
  stack cleanup and the slots it reads. A row with an entry shim runs the shim's C entry first, on
  the guest stack (4.4).

### 4.3 The guest stack and residue

In the sim backend every guest-visible stack byte lies in the modelled guest stack: a 1 MiB region
at `0x1ff00000..0x1fffffff`, directly below the guest heap, reserved at start-up (on Windows the
system's choice is the fallback). The host C stack holds only C locals; a pointer to a host local is
a valid guest address in both builds (identity mapping). The residue model is that of own_process.md
§5: before a shim's C entry runs, the sim backend zeroes `ES2_MACHINE_SIM_SHIM_ZERO` (4096) bytes
below the caller's ESP; residue goes through `es2_machine_call_planted`; a read-back is relative to
the returned entry ESP (`weapons_round3_stack_call` reads the preparation's entry-90h, the word
`0x47e0b0`'s `enter` leaves there).

### 4.4 Entry shims as C entries

Each entry shim of `tools/reference-game/proxy/entry_shims.h` (`ES2_SHIM`) is a C entry `int
(*es2_machine_shim)(es2_machine *m, uint32_t bridge)` (`es2/hook/machine.h`) that reads its
registers from the machine and its stack slots from guest memory at `m->esp`. It returns 0 to
continue into the replacement with the modified machine, or 1 after it called the replacement itself
through `es2_machine_call` of `bridge`. In the proxy, a naked capture shim (in `*_x86.h` headers
only the proxy links) fills an `es2_machine` from the real registers and calls the same C entry. The
emitter entries `0x40ea58` and `0x40ec64` move ESP to a residue record: their C entries set `m->esp`
to the record in ECX for their inner calls and restore it. es2sim keeps the shims in
`es2_sim_entry.shim` (`es2_sim_register_adapters`).

### 4.5 FS:0

The service `0x49630c` (DBSIM's frame records) and the 35 adapter headers that restore the head use
`es2_machine_fs0`: a variable in the sim backend (starting as a sentinel record at the top of the
guest stack whose `next` is `0xffffffff`, Windows' chain end), the real `%fs:0` in the x86 backend;
the in-vitro sim suites bind it to the real `%fs:0` too (`es2_machine_sim_fs0_host`). Guest code
reads the head only to link it as a new record's `next` (own_process.md section 4.4, item 4). The
guest records stay data: es2sim has no `__try`, no longjmp and no unwinding, and every fault ends
the process (section 6).

### 4.6 Stack limit, shim floor and probes

The C entries of the gapped shims call `es2_machine_probe` and `es2_machine_floor` (`shim_gap`,
`tools/reference-game/proxy/entry_shim.h`). The x86 backend (`hook/machine_x86.c`): the floor is
`es2_machine_x86_floor` (a call with a nonzero `m->esp` lowers it to the end of its host frame) when
it lies below the gap's end and at or above `%fs:8`, else the gap's end; the probe touches the guard
pages. The sim backend (`sim/src/machine_sim.c`): the floor is the gap's end; the probe compares the
gap with the guest stack's limit and ends with 71 ("a shim's stack gap passes the guest stack's
limit"), touching nothing, because neither the modelled region nor wasm memory has guard pages.

### 4.7 The x87 state

The own executables do no x87 arithmetic: llvm-mingw's i686 default computes in SSE, a
`-mfpmath=sse` build of es2sim, es2sim_window, es2shell, es2menu and es2launch is byte-identical,
and es2sim holds no floating-point arithmetic at all. The sim backend therefore has no x87 state (`sim/src/main.c`
runs no `fninit`/`fldcw`) and no `ES2_RESULT_ST0` result kind, which exists in `hook/` only; the x86
backend keeps `fnsave`/`frstor` around calls into original code.

### 4.8 What stays on Windows only

- **CODE stays no-access.** The vectored trap is a guard only; no call is trapped. An execute fault
  in CODE means an unconverted call site and ends with 70, reason "unconverted call".
- **The host-fault handler and its NULL-page rule** (own_process.md "Host faults under guest frame
  records").

### 4.9 The static check

`tools/web/seam_scan.py` lists the x86 dependencies per site (its docstring names the kinds);
`--forbid` fails on any hit in the sources of the portable link, whose x86-only exceptions
`tools/web/seam_allowlist.txt` names with a reason per file. `//tools/web:seam_scan_test` checks the
scanner; `--forbid` passes over `deps(//sim:es2sim)` and `deps(//sim:es2sim_window_web)`.

es2sim's portable link takes `//hook:descriptor` and `//hook:machine`, not `//hook`. The Windows
process is `sim/src/os_process_win32.c` behind `es2/sim/os_process.h`; the web build uses
`sim/src/os_process_wasm.c` and its pre-js `os_process_wasm.js`. The Windows guards install nothing
in wasm: `dispatch_trap_wasm.c` (no vectored trap, DEP flags 0), `host_fault_wasm.c` (no vectored
handler) and `image_access_wasm.c` (`--image-access` ends with 64).

## 5. Guest memory in linear memory

Identity mapping holds: a guest VA is the linear-memory address (wasm32 pointers are 32 bits).
Emscripten places everything above the image: static data from `-sGLOBAL_BASE=8388608` (`0x800000`),
then the C stack, then the host heap. A start-up check reads `__global_base`,
`emscripten_stack_get_end()`, `emscripten_stack_get_base()` and `__heap_base` and ends with 71 if
any lies below `0x00800000` (`es2_sim_os_reserved` in `sim/src/os_process_wasm.c`);
`//sim:es2sim_wasm_layout_test` links with `-sGLOBAL_BASE=1048576` and expects that exit and the
`FAULT` line naming `__global_base`.

| Range | Content | Check |
|---|---|---|
| `0x00000000..0x0000ffff` | NULL zone, never written | Inside a guest entry, an access through the guest accessors ends with `0xc0000005`. A canary check of the zone at each frame and at exit ends with 71. |
| `0x00010000..0x003fffff` | unused | The guest accessor ends with 71. |
| `0x00400000..0x006d7fff` | the guest image, laid out as on Windows (own_process.md §2.2) | The protections of the address map, through the accessor: an access to CODE outside the seven table runs, and a write to the table runs or to `.edata`, `.reloc` or `.rsrc`, end with 71. DATA and `.idata` are read-write. |
| `0x006d8000` | barrier page | Ends with 71. |
| `0x00800000..` | Emscripten's static data, the 16 MiB C stack (`-sSTACK_SIZE=16MB`), the host heap (`sbrk`) | The host heap must stay below `0x1ff00000`, checked at each guest reservation and each frame; a breach ends with 71. |
| `0x1ff00000..0x1fffffff` | the guest stack (4.3), the FS:0 sentinel record in its top 8 bytes | As on Windows. |
| `0x20000000..0x5fffffff` | the guest heap: the CW3220 model's reservations at their preferred base, committed through `emscripten_resize_heap` | As on Windows. |

Memory settings: `-sINITIAL_MEMORY=64MB`, `-sALLOW_MEMORY_GROWTH`, `-sMAXIMUM_MEMORY=2GB`. The pages
between the host heap and `0x1ff00000` are never touched; keeping the heap base keeps every heap
address equal to the Windows build's. **StackBase:** the c0w32 entry keeps its words under the
thread's StackBase (own_process.md "The c0w32 entry"); `es2_sim_os_stack_base` returns the end of a
16-byte static block outside every range of the table.

**The checked accessor.** `sim/src/guest_memory.c` (`es2/sim/guest_memory.h`) implements
`es2_guest_memory` (`core/include/es2/core/guest.h`) over the range table, filled by `es2_sim_map`
from the address map; the machine backend's slots, residue, plants and stack writes use it too (the
proxy keeps the unchecked identity; an empty table checks nothing). A page map answers accesses of
up to 4096 bytes within pages that forbid nothing in place; every other access, longer span and wrap
past 0xffffffff is looked up. A hit goes to `main.c`'s callback: a NULL-zone access inside a guest
entry is `0xc0000005` with the entry as the site (section 6), every other hit ends with 71.
`es2_guest_probe` (the adapters' `actor_pass_readable`, `assets_readable`) classifies without the
callback: an address in a checked range is unreadable, as `VirtualQuery` answers.

On Windows the unused range is not in the table, the NULL zone is not mapped and the host heap is
the process heap; the canary and the host-heap ceiling exist only in the web build
(`es2_sim_guest_check_frame`, `es2_sim_guest_check_heap`). A raw host access that bypasses the
accessor is not caught in wasm: a stray NULL-zone read returns zeros, a stray write is found by the
canary.

**C rules for the portable link** (the scan enforces them where it can):

- `wchar_t` is 4 bytes in Emscripten (UTF-16 text uses `uint16_t`) and there is no `long double`;
  structs that mirror guest memory have no bitfields (MinGW lays them out MS-style, Emscripten
  Itanium-style) and a packed struct carries a `_Static_assert` on every offset.
- Calling-convention attributes are no-ops; the sim backend pops per ABI. wasm traps on division by
  zero and on `INT_MIN / -1` as x86 does, but not on `INT_MIN % -1`; the fault sites check it
  (section 6). Float→int conversions out of range differ; es2sim has no floating-point arithmetic,
  and the scan lists the conversion sites of the shell and the launcher, each shown to stay in
  range.

## 6. Faults and exit codes

The exit codes are those of own_process.md "Exit codes". The mechanism that reaches each one:

| Case | Windows | Web |
|---|---|---|
| Refused call (70), guest stack limit passed (71) | Dispatch refusal; model-error callback | The same codes. |
| Divide fault `0xc0000094`/`0xc0000095` at the sites of own_process.md "The sites" | `es2_sim_divide_fault` runs a real IDIV or DIV | The primitive in C, at the same sites: a zero divisor gives `0xc0000094`, a quotient beyond 32 bits `0xc0000095`. `fault_paths_test` compares it with the asm primitive on Windows. |
| Host divides inside guest entries: the type loads and the HUD speed | A host IDIV faults; `host_fault.c` | The divide traps (`divide by zero`, `divide result unrepresentable`), and `es2_wasm_trap_exit` names the innermost guest entry: the same FAULT lines, with "wasm trap" in place of the host address. `INT_MIN % -1` does not trap. |
| The two adapter IDIVs (`actor_virtuals_hooks.h`, `base_flatten_hooks.h`) | `es2_machine_idiv`: the x86 backend executes the IDIV; the sim backend checks the operands and ends through `main.c`'s callback as `host_fault.c` does | The sim backend's check, the same callback. |
| NULL-page access inside a guest entry → `0xc0000005` | The vectored handler | The checked accessor and spans (section 5). |
| Any other fault of own code → 71 | Vectored handler and unhandled-exception filter | A wasm `RuntimeError`, below. |
| `ExitProcess(code)` | `ExitProcess` | `proc_exit` without atexit handlers. |

**A wasm `RuntimeError`.** es2sim's pre-js `sim/src/os_process_wasm.js` puts its own `quit_` in
front of Emscripten's (from a `preRun` hook). For a `RuntimeError` it resets the stack pointer to
the C stack base and calls the export `es2_wasm_trap_exit(kind)` (the kind from V8's message), which
writes the FAULT line and the exit record as `process_fault` does and picks the code
(`es2_machine_sim_trap_exit_code`):

| Trap kind | Inside a guest entry | Outside |
|---|---|---|
| divide by zero | `0xc0000094` | 71 |
| integer overflow | `0xc0000095` | 71 |
| any other kind | 71 | 71 |

**A read of a media file without bytes.** The VFS's `media-bytes` error (section 7) ends in
`es2_wasm_media_exit`: a refusal at the current guest entry (reason "media bytes"), exit code 70.
`//sim:es2sim_wasm_exits_test` makes an import throw (`sim/tests/es2sim_wasm_throw_host.mjs`) and
checks both paths' exit codes, FAULT lines and exit records.

**The guest entry.** `es2_sim_guest_entry_va(outward)` (`es2/sim/machine_sim.h`) names the guest
entries the sim backend is running, innermost first; the NULL-page rule, `es2_machine_idiv`, the
host divides, `es2_wasm_trap_exit` and the present row's call site (`present.c`) take their entry
from it, so a fault in a nested entry names the innermost one (the HUD speed `0x41bb3c` inside the
display set `0x42a1f0` is `0x41bb3c`). The sim `es2_machine_idiv` runs `es2_divide`
(`core/include/es2/core/divide.h`) on the sign-extended dividend; `es2_machine_stack_overflow` (the
flood fill's path overflow, `base_flatten_hooks.h`) passes `0xc00000fd` to
`es2_machine_sim_on_fault`: exit 71.

## 7. The host OS layer

`platform/include/es2/platform/os.h` (`//platform:os`) is the only place where es2sim, es2shell and
the launcher meet the operating system for files, time and memory. It has two implementations:
`os_win32.c` and `os_wasm.c` (imports from the JS host).

| Service | Windows | Web |
|---|---|---|
| Files: open, create, read, write, seek, size, close, delete, attributes, mkdir, find first/next with wildcards, current directory | Win32 and CRT | The worker's VFS, through synchronous imports |
| Whole-file view (`es2_os_map_file`, `es2_os_unmap_file`): the VOL mappings of es2sim (`vfs_hooks.h`, `core/src/vfs_stream.c`) | `CreateFileMapping`/`MapViewOfFile` | A buffer of the file's size in the instance's memory. A host that serves the file sparsely fills it per part on demand (`map_view`, `view_ensure`, section 8.3) and `es2_os_view_sparse` is true; otherwise `os_wasm.c` reads it whole through the file imports |
| INI files (UTF-16LE): `GetPrivateProfileStringW`/`IntW` | Win32 | An own reader, `platform/src/ini.c`, with the API's rules for the keys the code reads; tested against the API on Windows |
| Environment block, copied onto the guest heap | `GetEnvironmentStringsA`; the replay driver pads it on every run to `REFERENCE_ENVIRONMENT` (102 strings, 5648 bytes) | Both builds take `--environment FILE`. The browser passes a synthetic block of the reference size: its size changes the heap layout, and `c1_08-scan`'s frames depend on that (`sim/tests/frames_known.py`) |
| Clocks and calendar | QPC, `GetTickCount`, `GetLocalTime`, `GetTimeZoneInformation` | `performance.now()` and `Date` |
| Guest heap reservations | `VirtualAlloc` at `0x20000000` | `emscripten_resize_heap` from `0x20000000` up |
| Diagnostics: tick log (`CREATE_NEW`), journal, message trace | Files | VFS files; real files under Node |
| Process end and fault report | `ExitProcess`, stderr | `proc_exit`; the worker's console and the selftest log |

**The imports.** `os_wasm.c`, module `es2os`: `last_error`, `set_last_error`; files `create_file`,
`create_file_utf16`, `read_file`, `write_file`, `set_file_pointer`, `file_size`, `file_type`,
`close_handle`, `file_attributes`, `delete_file`, `create_directory`, `file_mtime`,
`move_file_replace`, `find_first`, `find_next`, `find_close`; paths `current_directory`,
`full_path`; time `counter`, `counter_frequency`, `tick_count`, `local_time`, `system_time`,
`time_zone`, `time_zone_bias`, `sleep`; memory `heap_reserve`, `map_view`, `view_ensure`,
`unmap_view`; `exit`. Module `es2sim` (windowed es2sim): `window_now`, `window_sleep`,
`window_take_input`, `window_present`, `window_cursor`, `window_display`, `window_metrics`,
`window_keys`, `window_message_box`, `audio_open`, `audio_write`, `audio_queued`, `audio_reset`,
`audio_close`, `mci_send`, and `key_scan` and `key_translate` of `os_process_wasm.c`. Module
`es2shell` (`shell/src/web/web_imports.h`): `now`, `wait_input`, `take_input`, `present`, `cursor`,
`fullscreen`, `message_box`, `movie_play`, `movie_stop`, `manual`, `sound_space`, `sound_write`.

**Paths.** The web side follows Windows rules: case-insensitive names, `\` and `/` as separators,
drive letters, `.` and `..`. The working directory is `C:\ES2`.

**Tests under Node.** A Node backend implements the imports with `node:fs` on the real Windows
paths, so a wasm run reads the same installation clone and CD as the native run:
`platform/src/os_node.js` (the `es2os` imports, a `--pre-js` of every wasm binary),
`web/node/os_host.mjs` (the same rules over `node:fs` or a site's VFS; a read of a file without
bytes throws the VFS's `media-bytes` error) and `web/node/host.mjs` (runs a module over `node:fs`
or, with `--site DIR --codec vfs.js`, over the site). `//web/node:host_test` runs
`platform/tests/os_calls.c` natively and as wasm over `node:fs` and a VFS and compares transcripts
and clocks.

The replay driver takes `--sim-argv NODE host.mjs es2sim.js` in place of `--sim`;
`sim/replay_wasm_tests.bzl` generates `//sim:replay_wasm_<suite>_test`,
`//sim:frames_wasm_<suite>_test`, `replay_wasm_all` and `frames_wasm_all`,
`sim/replay_site_tests.bzl` `//sim:replay_site_<suite>_test` and `replay_site_all`
(`sim/tests/replay_site.py`: a site that `build_site.py` builds from the CD, or the one in
`ES2_REPLAY_SITE`, with `--poison`, `--language G` and no prefetch). Each suite's part log goes to
`<output dir>/<suite>/part-log.json`.

## 8. Files, the VFS and data delivery

### 8.1 Trees

**`D:\`, the CD, read-only.** It holds every CD file except installers and system components
(`DIRECTX`, `DRIVERS`, `INDEO`, `WIN32S`, `WING`, `VER31`, `VER95`; all `*.EXE`, `*.DLL`, `*.DRV`
and `*.VXD` files; `*.??_`; `AUTORUN*`, `SETUP.*`, `SIERRA.INF`), the help files (`*.HLP`, `*.CNT`;
the online manual is the project's page, section 13) and `DEMOS` (other games' demos, opened by no
own code). The AVIs (`AVI`, `AVF`, `AVG`) keep entries with their original size but no parts: code
reads only their existence and size, a read of their bytes ends with 70 and reason "media bytes",
and the movie host plays their WebM instead (section 11).

**`C:\ES2\`, the installation.** It is laid out as `tools/reference-game/setup/prepare-native.ps1`
builds it, without the executables and DLLs: `DATA`, `SAV`, `TAPES` and `VOL` copied from the CD.
The language-dependent files come from the landing page (section 13): the language folder,
`ERROR.STR`, `DATA\MISSION.STR`, `DATA\LANGUAGE.CFG` and `DATA\DRIVE.CFG` (`D:` CRLF `C:\ES2` CRLF,
as BATCH.EXE writes it). The manifest's `languages` gives, per language byte (`G`, `E`, `F`), what
that step writes: `directories`, `copies` (target and `D:\` source) and `writes` (`LANGUAGE.CFG` and
`DRIVE.CFG` with their text); `Vfs.installLanguage` in `web/vfs.mjs` applies it.

**The overlay** holds every file the game writes, plus a tombstone for every file it deletes. An
entry either holds bytes or refers to base parts (copy on write), so copying the language folder
stores no bytes twice.

### 8.2 Parts and the manifest

The site stores content-addressed parts, `data/<sha256>.zst`. Most files are one part each. A VOL is
split into one part per member payload plus one part for the rest (header, directory and gaps), so a
member open fetches exactly its member. Identical parts are stored once (the copies shared by CD and
installation, the identical voice folders `SIMVOICE`, `SIMVOICF` and `SIMVOICG`, duplicate VOL
members). Each part is one zstd frame at level 19 with its content size, no checksum and no
dictionary ID, so equal inputs give equal parts.

`manifest.json`:

- `files`: `path` (`D:\VOL\SHELL0.VOL`, `C:\ES2\DATA\PREFS.CFG`), `size`, `flags` (`vol`, `media`,
  `no-bytes`) and `parts`. A part has `sha256`, `length`, `compressed` and either `offset` or, for a
  VOL's rest part, `ranges`: the `[offset, length]` file ranges its bytes come from, in order.
- `directories`: every directory of both trees, empty ones included.
- `languages` (section 8.1), `format` and `zstd_level`.

The worker decodes each part with libzstd and checks its SHA-256 (`//formats:sha256`), both compiled
into `vfs.wasm` (`web/src/vfs_codec.c`; the decoder is the BCR module `zstd` built without thread
support). `es2v_part_decode(src, src_size, dst, dst_size, sha256_hex)` returns 0 (ok), -1 (not a
frame, or no content size), -2 (content size differs from `dst_size`), -3 (corrupt, or more than one
frame) or -4 (hash differs); on any failure it clears `dst`. `web/vfs.mjs` holds the manifest, the
overlay and the sparse files, with the part fetch passed in; `web/node/vfs_site.mjs` gives it
`node:fs` for a built site.

### 8.3 Sparse files

A file the worker has not fully fetched is sparse. Two calls fill a range ("ensure range"): every
read through the OS layer, and the member open in `core/src/vfs_stream.c` (the whole member: the
block's 9-byte prefix, the payload and the suffix byte after it; the host's `ensure` in
`es2_vfs_host`). On a sparse view, es2sim's VOL load (`tools/reference-game/proxy/vfs_hooks.h`)
ensures the header, the directory, the member table and every block prefix, and checks the VOL with
`es2_vol_open_tables`, which reads no payload byte. `es2_vol_open` also compares every member's last
payload byte with the suffix byte after it; on a sparse view that check moves to the member open
(`es2_vol_member_at`) and to the site build, where `build_site.py` refuses a VOL whose member lacks
it.

A missing part is fetched synchronously (`XMLHttpRequest` in the worker, `responseType =
"arraybuffer"`), decoded and checked; decoded parts stay cached across process instances. Code may
read mapped bytes outside an ensured range; the Node test of the sparse mode catches that by filling
unfetched bytes with a poison pattern (byte `i` of the file is `DE AD BE EF`[`i` mod 4]).

### 8.4 Fetch order and caching

1. **At page start:** the manifest (fetched with `no-cache` on every start), the page's scripts and
   the wasm modules, and the shell's files from `prefetch/shell.json` (the shell reads its VOLs
   whole).
2. **Before each es2sim start:** the worker reads the mission key from `script.dat`
   ([mission.md](formats/mission.md)), fetches the parts of `prefetch/<mission>.json`
   asynchronously, then starts the instance.
3. **While an instance runs:** parts still missing come synchronously.

The mission key is `world<W>-zone<Z>` from the first 20 bytes of `C:\ES2\DATA\script.dat`
(`missionKey` in `web/vfs.mjs`): `W` is `signed16(2 * word 0 + word 9)` and `Z` is word 1
([mission.md](formats/mission.md) "Prepared script exact framing"). A list is `{"parts": [sha256,
...]}`. The site builder writes the lists from part logs of the wasm build under Node
(`build_site.py --part-log FILE`, repeated; `host.mjs --part-log` or `game_host.mjs --part-logs DIR`
write the logs: `{"mission", "prefetched", "parts"}`, the number of prefetched parts being `null`
without a list). A mission's list is the union of its logs' parts in first-fetch order, the logs in
path order; a log without a mission key or with a part outside the manifest fails the build. The
lists only save time: correctness never depends on them.

Fetched parts are stored in Cache Storage (`es2-parts`; `PartCache` in `web/store.mjs`, key
`data/<sha256>.zst`) and read from there first on a later visit (`prefetchParts` fills steps 1 and 2
into the Vfs through `Vfs.addPart`; step 3 reaches the cache through the Vfs's `onPart` hook). A
part enters the cache only after its SHA-256 check; a cached part that fails it is deleted and
fetched again.

### 8.5 Persistence

Writes go through at file close, delete and rename. The worker posts one `{kind: 'persist',
changes}` message per Vfs operation from the Vfs's `onChanges` (`persistTo` in
`web/game_worker.mjs`, which drops the keys below `C:\ES2HOST`): `[key, storedEntry(entry)]` pairs,
each entry a tombstone, a directory, bytes or part references, or `null` when the key left the
overlay. The main thread writes them into IndexedDB (database `es2`, store `overlay`, key = the
normalized path): `applyAll` writes one message in one transaction, so a rename never leaves the
file under both names or under neither, and transactions commit in the order of the operations.

At page start the main thread loads the overlay (`load` gives it as `Vfs.loadOverlay` takes it) and
hands it to the worker before the first process starts. `navigator.storage.persist()` is requested
at Start (`requestPersistence`), and `beforeunload` warns from Start until the game's last overlay
writes have ended (`guardUnload`). `resetInstallation` clears the overlay and the part cache.
Firefox's private windows run no service worker, so the page cannot be isolated there; the landing
page says that the game needs a normal window.

### 8.6 Sizes

Raw sizes: the movies in `AVI` 152 MB (93 files), those in `AVF` and `AVG` 13 MB each; the VOLs 68
MB (the largest, `SIMVOL0.VOL`, 30.8 MB); each voice folder 8 MB and each language folder 9 MB.
Every part is far below 100 MB; the site builder reports the site's total size, which must stay
below 1 GB.

## 9. Presentation

- **The simulation.** At each present, `es2_sim_window_convert` turns the 8-bit DIB and its palette
  into RGBA in the worker, writing into the next free frame slot. The main thread draws the newest
  slot on `requestAnimationFrame` with `putImageData`, on a canvas the size of the guest's mode.
- **Scaling.** CSS scales the canvas by the largest integer factor that fits, with `image-rendering:
  pixelated`, centred on black.
- **The shell.** The same path, fed from the shell's presenter. Each start clears the canvas, which
  otherwise would show the previous game's last frame until the intro's video covers it.
- **Fullscreen.** The Fullscreen API on the game container, switched only by Alt+Enter, whose
  keydown is a user activation. The game's own requests (prefs byte 6 at Start, its display calls)
  are answered but leave the page as it is.

## 10. Sound and CD music

- **The simulation's mixer** (the SOS model in `sim/src/audio.c`) is shared by both builds; its sink
  callbacks write into the simulation audio ring. The sink opens only format `0x29` (22050 Hz stereo
  U8, five buffers of `0x800` bytes; `WAVERR_BADFORMAT` otherwise, `MMSYSERR_ALLOCATED` when open),
  refuses a write that does not fit the ring whole (`MMSYSERR_ERROR`), answers a closed sink with
  `MMSYSERR_INVALHANDLE`, drops unplayed data on reset and close, and `queued` reports the ring's
  fill in SDL-sink units.
- **The AudioWorklet** (`web/audio_worklet.mjs`, `//web/node:audio_worklet_test`) runs in an
  `AudioContext({sampleRate: 22050})`. Each render quantum it reads whole frames from both rings (at
  most a quantum and at most the fill, never waiting: the rest is silence), maps U8 to `(b - 128) /
  128` as SDL3 does, sums the streams and clamps to [-1, 1]. Its port answers `'stats'` with the
  frames taken and the underruns.
- **The shell's four sounds** from `SHLSOUND.VOL` play through the shell's sound device interface
  (`shell/include/es2/shell/gui/sound.h`); its web device mixes as SDL3 does (float sum, clamp) into
  the shell sound ring, on every stream call, on every read of the shell's clock and, in 20 ms
  slices, during the shell's waits (the movie loop, idle, the movie queue's two-second WAIT after
  the briefing).

**CD music.** The platform's MCI `send` callback emulates `cdaudio` for the commands of the contract
in [audio.md](reverse-engineering/audio.md) "Sound and CD music in the window": `OPEN "cdaudio"`,
`SET TIME FORMAT TMSF`, `STATUS LENGTH` per track, `STATUS POSITION`, `PLAY FROM/TO` with `NOTIFY`,
`STOP` and `CLOSE`.

- Track lengths come from the TOC (section 15). The main thread plays the decoded Opus tracks as
  `AudioBuffer`s on an AudioContext of its own at the device rate and reports the position from that
  context's clock.
- `MM_MCINOTIFY` enters the input ring with the codes of Windows' cdaudio driver: successful when TO
  is reached, superseded when a new `PLAY ... NOTIFY` replaces a pending one, aborted on `STOP`.
  Headless runs answer `MCI_OPEN` 306 and everything else 257.
- The device lives on the page, so the input ring keeps one producer: the worker's `mci_send` import
  (`web/es2sim_imports.mjs`) posts an `mci` request (command, flags, the five fields of the host
  block and, for `OPEN`, the strings its fields 2 to 4 point at) and waits on the reply word; the
  page's `answerMci` writes the MCIERROR and the changed fields into the control words.
- `web/cdaudio.mjs` (`//web/node:cdaudio_test`) answers over the TOC and a clock, as headless
  without a TOC: a second `OPEN` 291, a track or time outside the TOC 282, everything outside the
  contract (other time formats, `PLAY` with `WAIT`, `MCI_PAUSE`, other status items) 274. A `PLAY`
  without `NOTIFY` and `CLOSE` end a pending play with ABORTED. Whether Windows' driver reports a
  replaced `PLAY ... NOTIFY` as SUPERSEDED or ABORTED is not measured; the notify handler acts only
  on SUCCESSFUL.
- `web/cd_player.mjs` (`//web/node:cd_player_test`) starts each segment of a play as an
  `AudioBufferSourceNode` on the context clock (the data track is silence; a track decoded late
  starts at the clock's position) and keeps the two most recently used tracks decoded; `CdMusic`
  posts SUCCESSFUL from a timer at the clock time of TO.

## 11. Movies

The shell's MCI player (`shell/src/win/mci_player.c`) has a web host with the same contract.
`es2_mci_player_place` gives the rectangle; the worker asks the main thread to play the movie in a
`<video>` element over the canvas at that rectangle, scaled like the canvas (the page appends the
manifest's video file to an AV1 `SourceBuffer` and the sound file to an Opus one of one
`MediaSource`, so the browser keeps them in sync; section 15); the worker runs the player's wait
loop over the input ring, as the Windows loop does, which ends on the `ended` event (posted as an
MCI notification), a mouse button, Esc or Space; then the video element is removed and the canvas
shows the shell again.

`web/movie_player.mjs` (`//web/node:movie_player_test`) is the page's side. The shell's path gives
the manifest name: the lower-case stem for `AVI\`, `avf/<stem>` and `avg/<stem>` for the language
folders; the names under `also` lead to their entry. A name the manifest lacks answers `movie_play`
with 0, which fails the movie. The AV1 type names Main profile, 8 bits and the smallest level whose
picture limits hold the encoded size (the manifest's size times its supersampling factor). A failed
fetch, append, element error or refused `play()` posts FAILURE, so the wait loop then ends only on a
button or a key; nothing is posted after `movie_stop`. DBSIM's AVI code is unreachable
(own_process.md "The AVI code in DBSIM and es2sim"), so the simulation plays no movies.

## 12. Input, focus and the window

**Keyboard.**

- **Scancodes and VKs.** `KeyboardEvent.code` gives the set-1 scancode (E0 prefix for extended keys)
  through a fixed table. A layout table gives the VK per scancode, as `MapVirtualKeyW` does
  natively: it starts from `navigator.keyboard.getLayoutMap()` in Chrome and, in Firefox (no layout
  map), from the built-in table for the browser's language (German, French, otherwise US). Each
  event's `keyCode` then corrects the table; Firefox's codes for `;`, `=` and `-` (59, 61, 173) are
  first mapped to the Windows OEM codes.
- **Native behaviour kept.** NumLock and the keypad behave as natively. The fake left Ctrl that
  Windows sends with AltGr is synthesized only when the browser did not deliver it (no `ControlLeft`
  with the same timestamp, and left Ctrl not held); its keyup gives the matching Ctrl up first.
  Auto-repeat arrives as the browser sends it (`event.repeat` sets the repeat flag), `blur` clears
  the key bitmap. `KeyWiring` in `web/keys.mjs` does the wiring per event; the asynchronous key
  state comes from the SAB's key bitmap, the queue-synchronous state from the events the worker has
  taken.
- **Keeping keys from the browser.** While the game has focus, every key the game uses gets
  `preventDefault`. In fullscreen, Chrome also takes Keyboard Lock (`navigator.keyboard.lock()`), so
  Esc and the other locked keys reach the game. Keys the browser reserves stay the browser's
  (section 18).
- **The tables.** `web/keys.mjs` holds the scancode table (platform_sdl.c's), the keypad and fixed
  VKs of `es2_sim_window_vk`, the keyCode correction, the layout choice, `MapVirtualKeyA`'s
  VK-to-scancode and TranslateMessage's characters. `web/key_layouts.mjs` is generated by
  `tools/web/keyboard_layouts.py` from `tools/web/testdata/keyboard_layouts.json` (a dump per layout
  of `MAPVK_VSC_TO_VK_EX`, `MAPVK_VK_TO_VSC`, `ToAscii` alone and after each dead key, and the AltGr
  test); the tool changes no setting and models every layout from its DLL's `KbdLayerDescriptor`
  tables (`kbdgr.dll`, `kbdfr.dll`, `kbdus.dll`), equal to the API in every entry for de, the only
  layout of the session on the reference machine. `//tools/web:keyboard_layouts_test` and
  `//web/node:keys_test` compare the module with the dump and with platform_sdl.c.

**Mouse.** Positions are absolute canvas pixels, scaled to guest client coordinates and clamped to
the client area. Buttons arrive as button-down and button-up messages, the wheel as `WM_MOUSEWHEEL`
with 120 per notch (`wheelDelta` in `web/game.mjs`). There is no pointer lock: Esc would release it.
`SetCursorPos` sets a virtual position, which `GetCursorPos` returns until the next real movement.
`SetCursor` and `ShowCursor` set the CSS cursor (`none` while hidden).

**Focus and window.** `focus` and `blur` produce the activation and focus messages of the native
platform. `visibilitychange` to hidden produces a minimize (`WM_SIZE` with `SIZE_MINIMIZED`);
becoming visible again restores.

**Message boxes.** `MessageBoxA` of the shell, the launcher and window-mode es2sim opens a modal
HTML dialog and the worker waits for the button (window-mode es2sim through the `window_message_box`
import: a `message_box` request with text, caption and style; `ES2SIM_MESSAGE_BOX=none` answers
without asking, as natively). Headless runs keep their answer.

## 13. The landing page

The page's own texts are English, whatever the browser's language (`web/texts.mjs`: `text(key,
params)`, `gameLanguage` for the default language; `//web/node:texts_test` checks that every text
is printable ASCII and every check, failure and reason has one). `web/index.html` is the page,
`web/app.js` its module. When every check passes, the page shows no message; a settings panel follows
(a label column with a `?` tooltip per setting: language, volume, savegames), below it the Start
button (a triangle only), and a footer links to the source repository.

1. **Checks.** `web/sw.js` (registered by `web/isolation.mjs`) sets COOP `same-origin` and COEP
   `require-corp` on every same-origin response. A page that is not isolated waits for the worker's
   activation and reloads once; a session flag turns a second failure into a message, and without a
   service worker (a private window) or a secure context the page names the cause. `web/checks.mjs`
   checks SharedArrayBuffer with `crossOriginIsolated`, `AudioWorkletNode` and `audioWorklet`,
   `WebAssembly.validate` of an empty module, an open of the probe database `es2-probe`,
   `decodingInfo` for `media-source` AV1 at 1280×960 and 15 frames per second, and `isTypeSupported`
   for the movie player's AV1 and Opus types. A missing feature gets a plain message and the game
   does not start. `//web/node:page_test` covers these modules under fakes,
   `//web/node:site_serve_test` the files over `serve.py` in both modes.
2. **Language.** German, English or French, one flag button each (drawn as inline SVG in
   `index.html`'s style, the name as tooltip; the label's tooltip says that only a reset changes
   it). No flag is pressed at first, and Start stays disabled until one is clicked; once the
   installation has a language, its flag stays pressed and the flags are disabled, until a reset
   unlocks them and Start waits for a new click. The choice writes into the overlay what
   `prepare-native.ps1 -Language` writes (section 8.1). The flags take a click while the overlay has
   no `DATA\LANGUAGE.CFG` (first visit, after a reset); Start passes the choice to the worker, whose
   `prepareInstallation` writes the language before the first process.
3. **Savegames.** Import, Export and Reset; the label's tooltip says what the ZIP holds. The
   player imports one ZIP file: the export's, or a ZIP of an installed game's `SAV` folder. `web/unzip.mjs` reads stored and Deflate entries
   (`DecompressionStream('deflate-raw')`), checks each entry's CRC-32 and refuses ZIP64, encryption,
   other methods and more than 16 MiB per entry or 64 MiB in all; only the entries' names count, not
   folders. Accepted names (case-insensitive): `GAME_<n>.SAV`, `script<n>.dat`, `missn<n>.str`,
   `player<n>.mec` and `GAMEFILE.STR`. Each file is parsed by the checks below; if any file or the
   ZIP fails, the page names it and writes nothing. Accepted files go into `C:\ES2\SAV\` unchanged
   in one `applyAll` on the overlay store (a file the base or the overlay already names keeps that
   name, as an overwrite on Windows does); a save from another language is taken over unchanged, and
   the page notes that its mission texts stay in that language.
4. **Start.** The click is the user activation for both AudioContexts and video playback.
   `startGame` in `web/game.mjs` requests persistence, arms the unload guard, starts the worker with
   the loaded overlay and the language's letter, and replaces the landing page by the canvas.

Also on the page: **Export** (a ZIP of the overlay's `bytes` entries directly in `C:\ES2\SAV\`,
sorted by name, stored entries with CRC-32, `web/zip.mjs`, no ZIP64; a Blob download); **Reset**
(asks for confirmation in a bubble over the button row, closed by Cancel, Escape or a second click;
deletes the overlay and the part cache, returns to step 2); **Volume** (0 to 100 %, set before the
start, as its tooltip says; kept in `localStorage`, `web/volume.mjs`; applied as a gain after the
sound worklet and after the CD music and as the movies' `volume`; the test plays the first 4 s of
the CD's first audio track, a 440 Hz tone when the CD has none).

**One installation per site.** A game keeps its own view of the overlay and writes into it until its
end, so Start and the reset take the Web Lock `es2-installation` (`holdInstallation` in
`web/store.mjs`, `navigator.locks` with `ifAvailable`): the game holds it until its last overlay
write, the reset while it clears. While another tab holds it, Start answers `start.busy` and the
reset `reset.busy`, and nothing is written. Without a `LockManager` the lock is granted; the
self-test's games use a memory store and take no lock.

**Help.** The shell's ONLINE MANUAL runs VSHELL's help routine (prefs, the sound focus lost, the
language check) and opens the project's page, `https://github.com/Highlife1911/es2-public`, in a new tab
(`manual` import, `MANUAL_URL` in `web/game.mjs`) instead of `ShellExecuteExA` on `es2guide.hlp`,
which Windows 10 without WinHelp does not open; the music returns with the focus. Help inside the
simulation does nothing, as on Windows.

**The save checks.** `web/src/tools_checks.c` (`//web:tools_wasm`) classifies a file name without
its folder (`es2t_kind`) and checks its bytes (`es2t_check`: `ES2_OK`, `-1` for another name, or the
parser's `es2_status` with the file offset it names):

| Name | Check |
|---|---|
| `GAME_<n>.SAV` | `es2_sav_parse`; `<n>` is decimal or `R`/`T`, the names `GAMEFILE.STR` gives slots 10 and 11 |
| `missn<n>.str` | `es2_str_parse` of one group in tag mode, as the readers of `data\mission.str` ask |
| `player<n>.mec` | `es2_mec_parse` (exact framing; `es2_mec_validate` needs the DAT tables and is not run) |
| `GAMEFILE.STR` | `es2_str_parse` of two groups in tag mode, at most 12 strings each, names of at most 13 and texts of at most 80 bytes with their NUL (VSHELL's slot table, `es2/shell/slots.h`) |
| `script<n>.dat` | `es2_script_parse`: the 20 header bytes, the 13 lists with nonnegative counts and their strides, every route reference below the coordinate count; bytes after the last list are a stale tail and pass, as for `GAME_<n>.SAV` (VSHELL rewrites `data\script.dat` without truncation and copies it whole) |

`web/saves.mjs` takes a folder or single files, ignores other names, refuses two files of the same
name and writes through an injected `write(path, bytes)` only when every file passed.
`//web/node:saves_test` runs the parsers on synthetic files, `//web/node:saves_real_test` on every
installation's `SAV` folder beside the CD.

## 14. Build and toolchain

**Toolchain.**

- **emsdk** is pinned in `es2-dependencies/emsdk` (version and checksums:
  `tools/setup-web-dependencies.ps1`, [dependencies.md](dependencies.md)); its Node is `HOST_NODE`
  in `bazel/platforms/host_tools.bzl`. JS tests use `node:test`; there are no npm packages.
- **`bazel/toolchains/emscripten`** holds a repository rule over the pinned emsdk (it reads the
  install stamp and `.emscripten` and downloads nothing) and a `cc_toolchain` whose tools are three
  native wrappers (`emcc_wrapper.c`: `emcc`, `em++`, `emar`) that run the emsdk's Python with
  `EM_CONFIG` and `EM_FROZEN_CACHE=1` through `CreateProcessA` and take no parameter files. C
  compiles with `emcc`; links, C++ archives and linkstamps use `em++`. Bazel allows an executable
  only without extension, `.exe` or `.wasm`: a link whose output is `<name>.wasm` writes `<name>.js`
  and `<name>.wasm`, the wrapper packs both into that one file (`wasm_bundle.h`) and `wasm_binary`
  unpacks them (`wasm_unpack`).
- **`//bazel/platforms:wasm32_emscripten`** (`@platforms//cpu:wasm32`, `@platforms//os:emscripten`)
  is selected by `--config=wasm`; the wasm tests are not built under it, because their runner is a
  native program.

**Rules** in `bazel/defs/wasm.bzl`:

- `wasm_binary`: a transition on its `binary` attribute to the wasm platform, with `.js` and `.wasm`
  outputs named after the binary.
- `node_test`: runs a JS file under `HOST_NODE` through the generic runner
  (`bazel/defs/python_test.c`).
- `wasm_cc_test`: an existing portable `cc_test`'s sources and dependencies built for wasm and run
  under Node; a test that reads data files sets `noderawfs = True` (`-sFILESYSTEM=1 -sNODERAWFS`).
  `core_test` in `bazel/defs/core_module.bzl` declares `<name>_wasm` for every core test; `formats/`
  and `launcher/` declare theirs beside the tests.
- `//bazel/toolchains/emscripten:layout_test_wasm` checks the section 5 layout under Node (static
  data from `0x800000`, a 16 MiB stack, the heap after it, 8 MiB of stack in use, heap growth beyond
  the initial memory).

**Link options.** Every wasm binary links with `-O2 -sGLOBAL_BASE=8388608 -sSTACK_SIZE=16MB
-sINITIAL_MEMORY=64MB -sALLOW_MEMORY_GROWTH -sMAXIMUM_MEMORY=2GB -sFILESYSTEM=0 -sEXIT_RUNTIME
-sENVIRONMENT=worker,node`; the browser modules add `-sMODULARIZE -sEXPORT_ES6`. No ASYNCIFY and no
pthreads. Compiled as C17 (`-std=c17`) or C++17 with `-Wall -Wextra -Werror -O2`. Sources include
`<stddef.h>` themselves for `NULL`, `size_t`, `ptrdiff_t`, `offsetof` and `wchar_t`; Emscripten's
`<stdint.h>` does not supply them.

**Modules.**

| Target | Content | Exports |
|---|---|---|
| `//sim:es2sim_wasm` | headless es2sim: `main.c` and its libraries with `os_wasm.c` and `os_process_wasm.c`, run under Node by `web/node/host.mjs` | `main`, `es2_wasm_trap_exit`, `es2_wasm_media_exit`, `emscripten_stack_get_base` |
| `//sim:es2sim_window_wasm` | the windowed es2sim with the web platform | as `es2sim_wasm` |
| `//shell:es2shell_wasm` | the graphical shell with the web platform | |
| `//launcher:launch_wasm` | `launch_logic.c` | `es2l_parse`, `es2l_parse_strings`, `es2l_language`, `es2l_memory_check`, `es2l_next`, `es2l_command_line`, `es2l_replacement_command`, `es2l_after_replacement`, `es2l_sim_command`, `es2l_sim_prefix`, `malloc`, `free` |
| `//web:vfs_wasm` | zstd and SHA-256 | `es2v_part_decode`, `es2v_sha256`, `malloc`, `free` |
| `//web:tools_wasm` | the `formats/` checks of the landing page | `es2t_kind`, `es2t_check`, `malloc`, `free` |
| `//web:page_files`, `//web:selftest_files` | the page's files and modules; the self-test page's | |

## 15. Media conversion

**Movies.**

- **Decode.** `media/tools/avi_frames.exe` (Windows) decodes each AVI through Video for Windows
  `ICDecompress` with the system Indeo codec, bound as `media/src/iv32_bind.c` binds it: RGB24
  frames, the full-colour picture the Windows build shows, plus the PCM sound.
- **Movies without a Windows decoder.** `ES2DROP3.AVI` (Indeo 4, `IV41`; VSHELL's movie table does
  not name it) has no Video for Windows decompressor on the reference machine; FFmpeg's `indeo4`
  decoder makes its reference instead, converted to RGB with BT.601, TV range and bilinear chroma
  (on `DEATH.AVI` that conversion of FFmpeg's `indeo3` decode comes closest to the Windows decode:
  42.5 dB mean PSNR). An empty index chunk is a dropped frame and repeats the previous picture. The
  report names each movie's reference.
- **Encode.** `tools/web/encode_media.py` runs FFmpeg: AV1 (libsvtav1, preset 4, `tune=1` = PSNR,
  keyframe interval 150), Main profile, 8-bit `yuv420p`, at the source frame rate and frame count,
  BT.601 signalled (`smpte170m`, TV range); the same input gives the same bytes. Sound is Opus. The
  video alone is `media/<name>.webm` and the sound alone `media/<name>.audio.webm`, with the
  lower-case AVI name; the AVF and AVG sets are in `avf/` and `avg/`. Files with identical bytes are
  converted once and listed under `also` in `manifest.json`; a file whose video packets equal an
  earlier file's takes over that video file and has only its own sound file (the AVF and AVG
  versions of `INTR_PT1` and `INTR_PT2` differ from the AVI ones only in sound). `manifest.json`
  gives each movie's `video` and `audio` file.
- **Quality search.** Per movie, the encoder lowers CRF from 6 until the thresholds hold (CRF 1 to
  40; CRF 1 is the floor, SVT-AV1's CRF 0 is not lossless).
- **Supersampling.** 4:2:0 halves the chroma resolution, and for hard-edged palette art the round
  trip alone (RGB to `yuv420p` and back, without a codec) can fall below the thresholds. The factor
  is the smallest of 1, 2 and 4 whose round trip leaves 43 dB mean PSNR, 39 dB minimum PSNR and
  0.981 mean SSIM for the codec's noise, with the picture at most 2400 pixels wide; the movie is
  encoded at that factor times its size (pixel replication) and measured after an area downscale to
  the source size. When CRF 1 misses a threshold, the next factor is tried. `manifest.json` and the
  report give each movie's factor.
- **Acceptance per movie.** Frame count and frame rate equal; duration equal within one Opus frame.
  Decoded with libdav1d and compared in RGB with the reference: mean PSNR ≥ 42 dB, every frame ≥ 38
  dB, mean SSIM ≥ 0.98.

**CD music.** `tools/web/cd_tracks.py` reads the raw image
`es2-dependencies/archive/EarthSiege2_Freeware_GoldGames_1r11_withAudio.iso` (2352-byte
sectors, 279,988 sectors, no CUE):

1. The data track ends at the last sector with the CD-ROM sync pattern.
2. The audio after it is split into tracks 2–7 at the five longest runs of digital silence of at
   least one second (if there are fewer than five, the five quietest one-second windows serve
   instead). The game's `(counter mod 5) + 2` plays tracks 2–6; track 7 is never played.
3. Each track runs from its start to the next track's start, as an MCI track length includes the
   following pregap.

`cd/toc.json` holds the starts and lengths in frames of 1/75 s. The tracks become Opus at 160 kb/s
stereo (resampled to 48 kHz); the decoded length is trimmed or padded to the TOC length. The image
has five interior silences of about 6.05 s (453 to 458 frames); by ear they separate six pieces. The
TOC report lists the silence runs found.

## 16. Tests and acceptance

| Level | What | Where |
|---|---|---|
| Unit | the portable `cc_test`s of `core/` and `formats/`, the shell's tests, `launch_logic_test`, the INI reader, the sim backend, the VFS, the input tables, the MCI emulation, the ZIP writer, the upload checks; the page's game side over fakes (`//web/node:game_test`) and the page modules (`//web/node:page_test`) | native and under Node |
| Dispatch | `//hook:machine_test` over both backends; zero traps in every suite | Windows |
| Native replay | `//sim:replay_all` (42 goldens), `//sim:frames_all`, the in-vitro tests (`//tools/reference-game/invitro:differential_*_test`, `differential_probes_test`, the suite `:differential_full`), `check.ps1 -Live -OwnShell -OwnSim` on a clone | Windows |
| Wasm replay | `//sim:replay_wasm_all`, `//sim:frames_wasm_all`: every suite's normalized tick log equal to its golden, every frame equal. `replay_suite.py` gives both builds the same environment block (else heap addresses and `heap_os_calls` differ), so the raw tick log equals the native run's but for `pid`, `tid`, `qpc` and `dep_flags` (`sim/tests/tick_log_raw_diff.py --dirs NATIVE WASM`, test `//sim:tick_log_raw_diff_test`) | Node |
| Sparse data | `//sim:replay_site_all`: every suite over the built site, with unfetched bytes poisoned and no prefetch | Node |
| Window mode | `acceptance-60s` and `c1_01-success` windowed through the web platform with a scripted Node host: tick log equal to the golden, message trace equal to es2sim_window's (`//sim:replay_window_wasm_<suite>_test`, `replay_window_wasm_all`, manual; `sim/tests/replay_window_wasm.py`, `web/node/sim_window_host.mjs`); `//web/node:sim_window_test` covers the imports, the audio ring, the MCI reply and the key wiring | Node |
| Shell and chain | the shell's headless screen suites; `gui_route_real_test`'s route; `//shell:launch_chain_wasm_test` (launcher → shell → es2sim → shell with the stub `chain_stub_sim.py`: written files byte-identical with the native chain's); `//shell:game_chain_wasm_test` (the real chain natively and through `game_host.mjs`: equal steps, tick logs and written files, DATA and SAV after a reload, a second session over a `--selftest` site with `prefetch/shell.json` from the first session's part logs, and the self-test's frame-time run) | Node |
| Faults | `fault_paths_test`, the portable parts of `host_fault_test`, `divide_scenarios.py`: the same exit codes | both |
| Media | the thresholds of section 15; the CD TOC report | Windows |
| Browser selftest | `selftest.html` in a local site build with `--selftest` (never deployed): the landing page's checks; a row per suite replay of `selftest_suites.json` against golden tick-log hashes; a key check (scancode and VK per key against the native `MapVirtualKeyW` table); audio (the worklet on the 22050 Hz context playing a 0.3 s tone from the shell sound ring; the first track of `toc.json` decoded at its TOC length, its first 4 s played); video (the shortest movie of the media manifest through the movie player); frame time (below) | manual, Chrome and Firefox |
| Manual acceptance | visual and listening acceptance: the shell with movies, missions with sound and CD music, saves surviving a reload, upload, export, reset, fullscreen | manual |

There are no automated browser tests: no browser is approved for automation.

**Performance.** The windowed busy time per frame must stay below the limiter's 40 ms at p95 on the
reference machine. The frame-time row (`web/selftest_run.mjs`) runs the game in the worker on a
fresh in-memory installation (a new pilot, a mission start, 640 paced frames of es2sim_window with
the frame recorder `ES2SIM_PERF`, its file below `C:\ES2HOST`, Q to leave) and shows the median, p95
and maximum of the busy time (a frame's time less its wait); `game_host.mjs --frame-time TICKS` runs
the same under Node. The windowed replay of `acceptance-60s` natively has a median of 11.4 ms
without traps (own_process.md "es2sim's frame times").

**Suite replays.** `tools/web/selftest_suites.py --site SITE --node NODE --out FILE` writes
`selftest_suites.json` for a local site: per suite (by default `c1_01-quit-dialog`, `c1_01-mouse`,
`c4_10-probe`, `c1_01-tape-play` and `acceptance-60s`) it takes the inputs of `replay_site.py`'s run
from es2-data's oracle directory and runs the entry with `game_host.mjs --replay FILE --suite NAME`
over the site (es2sim alone, `runGame`'s `simOnly`, on a store in memory). It writes the file only
when every suite is IDENTICAL to its golden as `replay_suite.py` judges it, with each run's exit
code, the SHA-256 of its masked raw tick log and the SHA-256 of the site's `es2sim.wasm`. The page's
row per suite compares that hash with the `es2sim.wasm` it serves, runs the entry in the worker and
passes when the exit code and the tick log's hash equal the Node run's.

The hash (`web/tick_log_hash.mjs`, `//web/node:tick_log_hash_test`) is taken over the raw tick log
with `pid`, `tid`, `qpc` and `dep_flags` of every record set to 0. The runs are headless, because
windowed runs of one suite differ in the exit record's call counts of the sound mixer, which follows
wall time. The expected hash is the Node run's over the same site, not the native run's: over the
site's Vfs the exit record's call counts of the file functions (`file_open` 0x004967c6, `file_read`
0x00496808, `file_findnext` 0x00496862, `file_close` 0x00496874) and of `operator_new` 0x0047749c
and `operator_delete` 0x00477548 differ from a run over a native installation. The self-test site
holds game files and journals and belongs to a local build only.

## 17. Site layout and deployment

```
build/web/site/
  index.html          selftest.html (only with --selftest)
  app.js  worker.js  audio.js  sw.js  and the page's modules (*.mjs)
  es2sim.js  es2sim.wasm  es2shell.js  es2shell.wasm  launch.js  launch.wasm
  vfs.js  vfs.wasm  tools.js  tools.wasm
  manifest.json
  data/<sha256>.zst
  media/<name>.webm  media/<name>.audio.webm  media/manifest.json
  cd/track02.opus .. cd/track07.opus  cd/toc.json
  prefetch/shell.json  prefetch/<mission>.json
```

**Building.** `tools/web/make-site.ps1` builds the wasm modules (`//shell:es2shell_wasm`,
`//sim:es2sim_window_wasm`, `//launcher:launch_wasm`, `//web:vfs_wasm`, `//web:tools_wasm`) and
`//tools/web:zstd_pack` with Bazel, then runs `build_site.py` with every input of this repository's
local build. It always passes `--clean`, `--media`, `--cd-audio`, the page's files, the part logs
and `--report "<Out>-report.json"` (for the default output, `build/web/site-report.json`). Its
parameters, all optional:

| Parameter | Meaning |
|---|---|
| `-Selftest` | switch: adds `selftest.html`, `selftest.mjs`, `selftest_run.mjs`, `tick_log_hash.mjs` and `selftest_suites.json` and passes `--selftest`; the default `-Out` becomes `build\web\selftest-site` |
| `-Out` | the site folder; default `build\web\site`; an existing site there is replaced |
| `-Cd` | the extracted CD; default `$env:ES2_DATA_ROOT`, else `build\extracted\cd` |
| `-Media` | `encode_media.py`'s output; default `build\web\media` |
| `-CdAudio` | `cd_tracks.py`'s output; default `build\web\cd` |
| `-PartLogs` | the folder of part logs (`*.json`, in name order); default `build\web\part-logs` |
| `-Suites` | the `selftest_suites.json` of `-Selftest`; default `build\web\selftest_suites.json`, written by `tools/web/selftest_suites.py`; the script stops when it is missing |
| `-Bazel` | the Bazel executable; default `$env:ES2_BAZEL`, else `bazel` |
| `-Python` | the Python executable; default the pinned interpreter of `es2-dependencies`, else `python` |

The script stops when `-Cd`, `-Media` or `-CdAudio` is not a folder.

`tools/web/build_site.py --cd <extracted CD> --media <media output> --out build/web/site` builds the
site. It is deterministic: equal inputs give an equal tree hash. It compresses the parts with
`//tools/web:zstd_pack`, a host tool over the BCR module `zstd`. Its options:

- `--zstd-pack <exe>` (required);
- `--media <dir>`: `encode_media.py`'s output (`*.webm` with `avf\` and `avg\`, and `manifest.json`)
  into `media/`;
- `--cd-audio <dir>`: `cd_tracks.py`'s output (`track02.opus` to `track07.opus`, `toc.json`) into
  `cd/`;
- `--file <path>`, repeated: a page script or module for the site root, under its own name, or
  `NAME=PATH` under `NAME` (a build output under its site name: `es2sim.*` from
  `//sim:es2sim_window_wasm`, `es2shell.*` from `//shell:es2shell_wasm`, `launch.*` from
  `//launcher:launch_wasm`, `vfs.*` and `tools.*` from `//web:vfs_wasm` and `//web:tools_wasm`,
  `audio.js` from `web/audio_worklet.mjs`); the page's modules (`//web:page_files`) go beside them;
- `--selftest`: the self-test page. It needs four `--file` arguments, `selftest.html`,
  `selftest.mjs`, `selftest_run.mjs` and `tick_log_hash.mjs` (`//web:selftest_files`), and takes
  `selftest_suites.json` from `--file` as an optional fifth; the build stops when one of the four is
  missing, and refuses these files without `--selftest`;
- `--part-log <json>`, repeated: a part log of `host.mjs`, from which the build writes
  `prefetch/<mission>.json` (section 8.4);
- `--report <json>`: the build report, outside the site;
- `--clean`: replaces an existing site in `--out` (a directory with a `manifest.json`); any other
  non-empty `--out` is refused.

The tree hash is the SHA-256 of the sorted lines `<path>` TAB `<size>` TAB `<sha256>` LF of every
site file. The build prints it with the site's size, the part count and the largest file, and lists
what is absent of the layout above (the content not given and the prefetch lists). It fails when a
file exceeds 100,000,000 bytes or the site reaches 1,000,000,000 bytes.

**Serving locally.** `tools/web/serve.py` serves the site with the COOP/COEP headers
(`--no-isolation-headers`: as a static host does, to test the service worker); options `--dir`
(default `build/web/site`), `--port` (8000) and `--bind` (`127.0.0.1`). It sets the MIME types of
`.wasm`, `.js`, `.mjs`, `.json`, `.webm` (`.audio.webm` as audio), `.opus` and `.zst`, serves parts
without a `Content-Encoding` and sends `Cache-Control: no-cache` on every response.

**Deployment.** Nothing of the site is committed to `es2` or `es2-data`; it holds the game's data
from the user's CD. The host needs:

- HTTPS: the service worker and `SharedArrayBuffer` need a secure context (`localhost` excepted);
- `application/wasm` for `.wasm` (`WebAssembly.compileStreaming` refuses other types) and a
  JavaScript type for `.js` and `.mjs`;
- no `Content-Encoding` on the parts (`data/*.zst`): the worker decodes and checks them itself, and
  a host that declares them zstd-encoded hands it the decoded bytes;
- room for the site: about 126 MB in about 2,400 files, the largest about 6 MB.

The site works in any folder of the host: every path in it is relative, and the service worker's
scope is the folder.

Steps:

1. `.\tools\web\make-site.ps1` builds `build/web/site`. It ends with the tree hash and
   `absent: nothing`; without `-Selftest` it holds no `selftest.html`.
2. Locally as a static host serves it: `serve.py --dir build/web/site --no-isolation-headers`, then
   `http://127.0.0.1:8000/` in Chrome and in Firefox. The page reloads once, the checks pass, Start
   runs the game.
3. The folder's contents go to the host's folder (an upload, or a commit to the host's repository),
   replacing the hosted site. Files of the hosted site that the new one lacks (`data/` parts of
   changed files) may be deleted; the new manifest names none of them.
4. The site's address in Chrome and in Firefox: the same as step 2.

Updates: the page, its modules and the `.wasm`/`.js` pairs keep their names from one build to the
next. The service worker fetches every file but the content-addressed parts with `cache:
'no-cache'`, so the browser revalidates them whatever caching headers the host sends. Only the page
itself (a navigation, which the worker passes on unchanged) follows the host's caching time; on a
first visit no worker is installed and nothing is cached yet.

## 18. Known differences from the Windows build

- The online manual is the project's page, not `es2guide.hlp` (section 13).
- The memory check uses the values recorded on the reference machine (section 2).
- `SetCursorPos` moves only the virtual cursor; the real pointer stays where it is.
- Keys the browser reserves never reach the game: Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Tab, Alt+Tab, Alt+F4
  and the Windows key. In Firefox's fullscreen, Esc leaves fullscreen.
- Movies are AV1 within the thresholds of section 15, not bit-exact Indeo.
- The CD tracks' boundaries come from the image's silence runs (section 15).
- A Firefox private window cannot run the game (section 8.5).
- While a message box of the shell is open, the shell's sound stops after the ring's 370 ms: the
  worker waits for the answer, and only the worker feeds the ring.
- A browser without AV1 decoding or another checked feature cannot run the game (section 13).
