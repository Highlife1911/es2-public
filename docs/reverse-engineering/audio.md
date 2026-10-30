# Audio: SOS driver, SFX manager, sound API and the own audio

The audio of the original and its reconstruction: DBSIM's sound API, SFX
manager, SOS glue and CD music; the `sfx` hooks that replace the sound API in
the original process; VSHELL's shell sounds and the own shell's player;
DBSIM's audio as own code in `es2sim`, with a silent device headless and a
software mixer and MCI host in the window.

Sources: the `llvm-objdump -d -M intel` listings, image base `0x400000`, of
the reference `VER95/DBSIM.EXE` (SHA-256 `8561aa47…c4ad`), `VER95/VSHELL.EXE`
(SHA-256 `1534aa8f…a16e`) and `VER95/SOS9503.DLL` (SHA-256 `8422475c…d97e`),
see [reference_version.md](../reference_version.md). Addresses of the DLL are
the DLL's own. **Confirmed** (in the VSHELL part: **C**) means read from the
instructions or the data at the cited addresses; role names and anything
inside `SOS9503.DLL` beyond the cited instructions are **hypotheses** (**H**).
Live counts are the entry probes of the [DBSIM map](../reference-game/dbsim_map.md)
`dbsim-map-20261008` (six suites, saturated at 1000, order
a60/c1s/c1c/c3_09/c4_08/c3_10; "0" = probed and never entered after the proxy
armed). The derived tables of the static reading (`sounds_str_table.txt`,
`eax_after_sound_calls.txt`, `audio_global_writes_xref.txt`) and the scripts
of the sfx hooks (`closure.py`, `clockreach.py`, `vtcheck.py`, `ecxlive.py`)
are named under the evidence directory `d-03-20261008` and the scratch
directory `build/claude/v-06/`; neither is present in `es2-data`.

## DBSIM's audio boundary

No audio function in this part has a jump table (map `jump_tables` = 0 and
none found while reading), so the map's instruction counts apply.

Two functions next to the audio code are **not audio**: `0x465628` is the
frame presentation (DirectDraw/GDI blit, `BeginPaint`/`StretchBlt`/`EndPaint`,
palette select, cursor clamp) and `0x465cc4` uploads a 256-entry palette
(`GetSystemPaletteEntries`, `SetDIBColorTable`, `AnimatePalette` or the
DirectDraw palette's slot `+0x18`). Both belong to the presentation; they read
display globals (`0x4d253e`, `0x4d2542`, `0x4d3714`, `0x4d302c`, …) and write
only window, GDI and DirectDraw state. Neither touches RNG, clock or actors.

### Boundary in short

- **Driver.** DBSIM imports no audio DLL. The loader `0x4958fd` loads
  `sos9503.dll` (Win32s: `sos32s03.dll`) with `LoadLibraryA` and fills 97
  function-pointer slots `0x6cbd60..0x6cbee0` with `GetProcAddress`; every
  SOS call goes through a one-line stub `0x495a43..0x496300` that calls one
  slot. 50 of the 97 entry points have a calling site in the glue (table
  below); `sosDIGIGetBytesProcessed`'s site lies in the uncalled `0x473a8c`,
  so 49 are reachable. Music is CD audio through `mciSendCommandA` (WINMM
  import), not SOS MIDI: both driver initialisations pass MIDI device
  `0xffff`, so the SOS MIDI path is dead.
- **Simulation state the audio code changes.** (1) RNG stream B
  (`0x4d260b`, compared as `rng_b_hex`): `0x462830` and `0x4628e0` draw
  `rng_bounded(0x4d260b, n)` for every table entry with more than one
  variant, but only while the SFX context `[0x49f8ec]` exists. (2) The
  virtual clock: `0x462b9c`, `0x463400`, `0x4634b0` and `0x4632c4` call
  `0x467828` (`GetTickCount`); with `virtual_time_intra_frame=1` each call
  moves the clock seen by later readers of the same frame by 1 ms. (3) The
  shared view-transform scratch: positional sounds (`0x46299c`) call
  `0x48c444(0)`, which rebuilds `0x6c5fb4..0x6c6028` and `[0x6c5ff8]`.
  Nothing else: no actor field, no simulation global, no stream A.
- **Results that flow back.** Only two driver-dependent values reach game
  code: the init success (the context pointer `[0x49f8ec]`; outside the
  audio code only the UI loop, an alert screen, the main-loop exit and
  mission start test it) and `sosDIGISampleDone` through `0x4641e8` → `0x462af0`
  ("sound still playing"). The latter is real playback time, not virtual
  time. Its users (`0x41a6d0`, `0x41a994`, the cockpit radio panel
  `0x44b514`/`0x44b620`) only update their own latches or restart the
  static noise sample `0x1c`. Every other return value of the sound API is
  ignored by its callers, or is driver-independent (play success, voice
  channel pointer).
- **Never affects the comparison** (given the same call sequence): the SFX
  manager `0x463694..0x4649b8` except the clock and file side effects
  listed below, the glue `0x473708..0x474b2e`, the SOS stubs, CD audio and
  the window hook `0x462398`.
- **In vitro.** The bookkeeping layer is pure apart from `SetLastError`,
  the allocator and `0x467828`; driver calls can be faked by filling the 97
  slots, which the private mapping leaves zero.

### Layers and call graph

```
simulation / cockpit / UI callers (≈60 sites)
  │  index = row of the sound table (SOUNDS.STR), position = actor+0x26
  ▼
sound API, globals 0x4d2a88/0x4d2b78/0x4d2c78, context [0x49f8ec]
  0x462830 play(index)            0x4628e0 play_at(index, pos)
  0x46297c update_at(index, pos)  0x462ac4 stop(index)
  0x462af0 is_playing(index)      0x463114 set_rate(index, rate)
  0x462940 / 0x462b9c voice channels, 0x463400 / 0x4634b0 / 0x4632c4 lip timeline
  helpers: 0x462784 gate, 0x46299c distance/pan, 0x462b30 channel LRU
  ▼
SFX manager (object at [0x49f8ec], 0x4c bytes), handles = generation<<16 | slot
  0x463a14 open   0x463c50 close   0x463d4c cache/load   0x464038 play
  0x464118 stop   0x4641e8 is_done 0x464618 volume  0x4646dc pan  0x46466c rate
  0x46474c loops  0x4647b8 priority  0x464390 evict  0x4648e0 stop all
  ▼
SOS glue (0x473708..0x474b2e), driver handle word 0x6b566e
  0x473898 start  0x4739ac stop  0x473a5c done?  0x473aec volume
  0x473b54 rate   0x473b80 pan   0x473828 load/unload (0x474360/0x47465c)
  0x473eac / 0x4741fc driver init/uninit, 0x4737d8 user service
  ▼
stubs 0x495a43..0x496300 → slots 0x6cbd60..0x6cbee0 → SOS9503.DLL
  (own timer thread: timeSetEvent callback "TimerProc", waveOut*, PostMessageA)

CD music: 0x464858/0x464874/0x464988/0x46499c → 0x473c48 / 0x473c00 / 0x473d44 / 0x473dcc
  → mciSendCommandA ("cdaudio", TMSF, play with notify to the game window)
window hook: 0x462398 (registered in the list 0x4d3b30 that MainWndProc 0x466034 calls first)
```

#### Driver loading and the SOS entry points

Confirmed. `0x4958fd` (from the SFX init `0x473708`) keeps a use count
`0x4a6a98`. `GetVersion`: Windows NT, or Windows 9x with major version ≥ 4,
loads `sos9503.dll` (string `0x4a7864`); otherwise `sos32s03.dll`
(`0x4a7870`); module handle `0x6cbd58`, flag `0x6cbd5c`. The name table at
`0x4a6a9c` has 36-byte records (pointer to the slot, then a 32-byte name) up
to a null slot pointer: 97 names, slots `0x6cbd60..0x6cbee0`. `0x495a1f`
decrements the count and calls `FreeLibrary`. Load failure returns
`0x390000`.

Entry points with a DBSIM calling site (stub → calling glue site):

| Group | Entry points (calling sites) |
|---|---|
| Detection / init | `sosDIGIDetectInit` (`0x473f80`), `…FindFirst` (`0x473f99`), `…DetectUnInit` (`0x473fa7`, `0x473fb6`), `sosDIGIInitSystem` (`0x473fd2`), `sosTIMERInitSystem` (`0x473fd9`, `0x474107`), `sosDIGIInitDriver` (`0x4740a0`), `sosDIGISetActiveWindow` (`0x4740d1`, `0x474225`) |
| Shutdown | `sosDIGIStopAllSamples` (`0x474216`), `sosDIGIUnInitDriver` (`0x474232`), `sosTIMERUnInitSystem` (`0x474246`, `0x4742f4`), `sosDIGIUnInitSystem` (`0x47424b`), `sosGetErrorString` (`0x474311`) |
| Samples | `sosDIGIStartSample` (`0x473985`), `sosDIGIStopSample` (`0x4739f8`), `sosDIGISampleDone` (`0x473a81`), `sosDIGISetSampleVolume` (`0x473b47`), `sosDIGISetPanLocation` (`0x473b9d`), `sosDIGISetSampleRate` (`0x473b77`), `sosDIGIGetBytesProcessed` (`0x473aa9`, in `0x473a8c`, which has no caller) |
| Memory | `sosPrepare32Memory` (6 sites in `0x4744b7..0x474902`), `sosUnPrepare32Memory` (6 sites) |
| Service | `sosDIGIUserService` (`0x4737e8`, `0x474aee`), `sosDIGIAcquireDriver` (`0x4737b6`), `sosDIGIReleaseDriver` (`0x4737cf`) |
| Streams | `sosDIGIStreamAcquire`/`PrepareBuffer`/`AddBuffer`/`Start`/`Stop`/`Release`/`UnPrepareBuffer` (`0x474791..0x474a6b`), reached only for sample flag `0x1000` (open mode 2, never used by the sound table) |
| MIDI | `sosMIDIDetect*`, `InitSystem`, `InitDriver`, `SetInsData`, `InitSong`, `StartSong`, `StopSong`, `UnInitSong`, `PauseSong`, `ResumeSong`, `SongDone`, `SetMasterVolume`, `StopAllSongs`, `ResetDriver`, `SetActiveWindow`, `UnInitDriver`, `UnInitSystem` — **dead in DBSIM**: `0x473708` calls `0x473eac(digi, 0xffff, hwnd, hwnd)` twice (configured device, then device 1), so the MIDI half (`0x473ef4..0x473f5c`, `0x4740df..0x4741f2`) never runs and the `MELODIC.BNK`/`DRUM.BNK` loads of [file_io.md](file_io.md) are not reached from DBSIM |

The other 47 names (timer events, pause/resume output, panning queries,
`GetDMAPosition`, `GetSamplePointer`, …) are loaded but never called
([dll_imports.md](../reference-game/dll_imports.md) counts 49/48, leaving
out `sosDIGIGetBytesProcessed` because `0x473a8c` has no caller; the names
agree).

The DLL side (read only around the cited instructions): `SOS9503.DLL`
imports `CreateThread`, `SetThreadPriority`, `SuspendThread`,
`ResumeThread`, `timeSetEvent`/`timeKillEvent`, `waveOut*`, `midiOut*`,
`IsWindow` and `PostMessageA`. Its exported `TimerProc` calls the
sample-structure pointer `+0x64` when it is non-zero (`0x4030f0..0x403101`);
`PostMessageA` sites post message `0xa000` with wParam
`0xa000`/`0xa002`/`0xa003`/`0xa010` to the window given at init
([main_loop.md "Where the `0xa000` messages come from"](main_loop.md#where-the-0xa000-messages-come-from-static-sos9503dll)).

**No DBSIM code runs on the driver's thread (confirmed for the DBSIM side).**
`0x473898` copies the slot payload `+0xc` into the sample structure `+0x64`;
payload `+0xc` is zeroed by `0x463a14` and its only setter `0x4645dc`
("setCallback") has no caller and no pointer reference. The DLL receives
only audio-owned DBSIM memory: the driver structure `0x6b5590`, the handle
words `0x6b566e`/`0x6b5670`, sample structures in `GlobalAlloc` blocks and
the handle outputs in the sample-slot table. **Hypothesis:** the DLL writes
nothing else in DBSIM's address space (its internals were not read in full).

#### Window messages

Confirmed, live. `0x462398` (symbol `sfxWndProc`) is registered in the hook
list `0x4d3b30` by `0x462410` and called by MainWndProc `0x466034` for every
message (≥ 1000 in all suites); it always returns 0. It handles only:

- `WM_TIMER` (`0x113`): with a context, `0x4639e8` → `0x4737d8` (driver user
  service, then `0x474aa0`). DBSIM's only `SetTimer` call is in `0x474aa0`
  under message `0x110`, which `0x462398` never forwards; **`0x4639e8` ran 0
  times in all six suites**. The `0xa000` driver messages are ignored
  (`0x474af5..0x474b09`).
- `MM_MCINOTIFY` (`0x3b9`) with `MCI_NOTIFY_SUCCESSFUL`, a context and the
  loop flag `0x49f900`: stop and replay CD track `[0x49f8fc]` (`0x464874`,
  `0x464858`). Touches only `0x4a0e2c`, `0x6b558c`.

`0x464260` (from the UI loop `0x454acc`, 0/1000/0/1000/1000/793) calls
`sosDIGIUserService` directly.

### Sound data

**Sound table** `st<lang>\sounds.str` (path `0x45ef1c("sounds", "str")`, read
by the STR reader `0x453878`/`0x453918` in `0x46254c`). Member of
`SIMVOL0.VOL` (`str\`) and `SIMLANG.VOL` (`stf\`, `stg\`), 1,172 bytes, all
three byte-identical (SHA-256 `a1eea7ea…43f2`). Layout (confirmed from
`0x453918` and the bytes): u32 length, u16 record count (57), then per
record a u16 name length and the NUL-terminated sample name, a u8 parameter
length (7) and the parameter bytes. `0x4d2a88[i]` points to the name,
`0x4d2b78[i]` to the parameters, `0x4d2c78[i]` is the sample handle, the
count is the word `0x4d2d68`. Parameter bytes (`p` = `[0x4d2b78 + 4·i]`):

| Byte | Use |
|---|---|
| `p+0` | play count, 0 = endless loop (`0x46474c`; driver loop count = count − 1, or −1) |
| `p+1` | volume 0..100; scaled by `0x47e08d(p+1, 65000)` |
| `p+2` | preload at init (`0x463d4c(…, 1)`) |
| `p+3` | detail divisor in `0x4627c8` (no caller) |
| `p+4`, `p+5` | near / far distance in units of 1024; `0xff` becomes 5 / 100 at init |
| `p+6` | number of variants: rows `i .. i+n−1` are alternatives |
| `p+7` | "was playing" flag for pause/resume (`0x46317c`/`0x463238`); before the first pause it is the next record's name length, so nonzero |
| `p+8` | volume percent, set to 100 at init (overlaps the next record's length bytes after parsing) |

In the reference data **only row 12 (`0xc`) has more than one variant (3)**;
all others have 1. Rows used below: `0x1c` static noise, `0x21`, `0x2d`
engine loop, `0x33` fire loop (row names in `sounds_str_table.txt`). Sample
files come from `HMI\` (or `HMX\` with the `-l` option, `0x46233c`), i.e.
`SIMSOUND.VOL`.

**SFX context** (`new 0x4c`, constructor `0x46379c` (hook `sound_config`),
sizes `0x463694`): `+0x10` file-record capacity 60, `+0x14` sample-slot
capacity 90, `+0x18` cache limit (2,000,000 bytes, 1,000,000 with `-l`),
`+0x1c` cached bytes, `+0x20` handle generation counter, `+0x24..+0x33`
settings from `data\sound.cfg` (`Driver`, `Buffers`, `Rate`, `Width`,
`Mono`), `+0x34` file records (540 bytes: handle, 512-byte name, open
argument, reference count, cached flag, size, driver sample structure),
`+0x38` sample slots (40 bytes: handle, file handle, flags, priority,
callback, volume, loop count, pan, rate, driver handle word), `+0x3c`/`+0x40`
digital/MIDI playing counts, `+0x44` window handle `[0x4d3610]`. Slot flags:
bit 0 MIDI, `0x80` loop count set, `0x100` playing, `0x400` rate set, `0x800`
pan set, `0x1000` stream.

**Voice channels**: 5 records of `0x42` bytes at `[0x4d2d6c]` (count word
`0x4d2d74`), 100-byte lip buffers at `[0x4d2d70]`, an event queue (pointers
`0x4d2ec8`, entries `0x4d2e76`, count `0x49f964`). Channel `+0` sample
handle, `+4` next event time, `+8` current lip value (−1 = end), `+0xa`
script pointer, `+0x2f` buffer, `+0x33` name pointer, `+0x37` start time,
`+0x3b` in use, `+0x3e` handle.

**Flags**: `0x49f8ec` context (0 = no sound), `0x49f8f4` gate for rows
0..9, `0x49f8f8` gate for rows ≥ 10 (both set only by `0x462d40`/`0x462d5c`
from the option toggles `0x459c80`/`0x459cb4`, which never ran after arming;
the file holds 0, **inferred** `0x49f8f8` = 1 at run time because
`0x464038` runs far more often than `0x462830`), `0x49f966` voice enable,
`0x49f8fc` CD track, `0x49f900` CD loop, `0x49f904` saved CD position,
`0x4a0e30` master volume (100; its setter `0x473ab4` has no caller),
`0x4a0e50` last driver status.

**Sound on/off.** `0x461608` calls `0x462410(byte [0x4d24c8], …)`; the byte
is 1 from the static initializer `0x45caf4` and toggled by `-s`
([launcher.md](launcher.md)). `0x462410` always registers the window hook
and allocates the voice channels; only with the byte set does it create the
context, and the context is cleared again when `0x463694` → `0x473708`
(driver load and init) fails. **The six oracle suites run with a context**
(`0x462784`, reached only behind the context test, runs in all of them;
`0x473898` calls the driver 121/714/319/1000/322/304 times).

### The sound API functions

Counts are map entries; "callers use EAX" was checked at every direct call
site (`eax_after_sound_calls.txt`).

| Function | Role | Reads | Writes / side effects | RNG / clock | Driver | Result used by callers | Live |
|---|---|---|---|---|---|---|---|
| `0x462830(i)` | play row `i` at its table volume | `[0x49f8ec]`, `p+6`, `p+1`, `p+8`, `0x4d2c78[i]`, gate | slot volume and play (below) | **stream B** if `p+6` > 1 | yes, via `0x464618`, `0x464038` | no (30 sites; the tail returns through `0x40f09c`, `0x41b0bc` (in `0x41aa5c`) and the UI callback `0x438e2c` are ignored by `0x41d3a4`/`0x41fb37`, `0x45f8ac`; `0x438e2c`'s table users not checked) | 11/61/18/153/32/104 |
| `0x4628e0(i, pos)` | play row `i` at a world position | same, plus position | `0x46299c`; plays only if audible | **stream B** if `p+6` > 1 | yes | no (9 sites) | 113/738/312/1000/344/192 |
| `0x46297c(i, pos)` | refresh volume/pan of a running loop | `[0x49f8ec]` | `0x46299c` only (no play) | none | yes (if playing) | no (`0x41a31a`, `0x46b82d`) | 545/1000/0/1000/1000/999 |
| `0x46254c` | init: parse table, open and preload samples | STR file | `0x4d2a88`, `0x4d2b78`, `0x4d2c78`, `0x4d2d68`, `p+4/+5/+8`, channel `+0x3c/+0x3e` | none | via cache load, volume | — | 0 (runs before arming) |
| `0x462b9c(wav, lip)` | start a voice on an LRU channel | channels, clock | channel record, lip buffer (VOL stream read `0x463374`), may close the previous voice, opens/caches the WAV only with a context and `[0x49f966]` | **`0x467828` once** | via cache load | channel pointer → `0x44aff0` → object `+0x12f`; 0 only if the lip file cannot be opened | 1/13/3/37/4/8 |
| `0x463a14(ctx, name, mode, a, b)` | open: allocate a handle, find/create the file record | context tables | slot, record, generation `+0x20`, refcount; `SetLastError(0xe)` when full | none | none | handle stored in `0x4d2c78`/channel | 1/7/3/23/2/8 |
| `0x4634b0(ch)` | advance a lip script and queue the next event | channel script bytes | channel `+4/+8/+0xa`, queue `0x4d2ec8`/`0x4d2e76`/`0x49f964` | **`0x467828` once** | none | via `0x46356c` → cockpit panel | 1/39/16/191/4/69 |
| `0x464038(ctx, h)` | play | slot, record | may load the sample (`0x463d4c`: VOL stream or `_lopen`, `GlobalAlloc`, `sosPrepare32Memory`, cache counter `+0x1c`, eviction `0x464390`); playing count `+0x3c`, flag `0x100` | none | `sosDIGIStartSample` | 1 = started; **driver-independent** after a successful init (below) | 121/714/319/1000/322/303 |
| `0x464618(ctx, h, v)` | set volume | slot | payload `+0x10`; if playing `sosDIGISetSampleVolume` | none | yes | no | 670/1000/332/1000/1000/1000 |
| `0x462af0(i)` | is row `i` still playing | `0x4d2c78[i]` | if the driver says done: stop (`0x464118`, playing count, flag) | none | **`sosDIGISampleDone`** | **yes**: `0x41a6d0`, `0x41a994`, `0x44b514`, `0x44b620` | 0/27/2/71/0/12 |
| `0x41a6d0(P, rate)`, `0x41a994(P, rate)` | torso twist/pitch "servo" latch | `P+0x298`/`P+0x29a`, latch | `0x49a1c9`/`0x49a1ca`, `0x49a1cc`/`0x49a1ce` (read nowhere else) | none | via `0x462af0(0x21)` | — | 1000/1000/1000/1000/0/937 each |

Notes on single functions:

- **`0x462830`** (confirmed; matches the contract in
  [player_movement.md](player_movement.md#sound-selection-462830-and-the-isolation-boundary)):
  without a context it returns at once. Otherwise, for `p+6` > 1 it adds
  `rng_bounded(0x4d260b, p+6)` to the index, then volume =
  gate ? `0x47e08d(p+1, 65000)` : 0, times `p+8` / 100, `0x464618`, and
  always `0x464038` — even at volume 0. The draw happens before the gate,
  so muting effects does not change stream B. It has 30 direct call sites,
  each with a literal row, and no data reference or tail jump.
- **`0x4628e0`**: same draw, then `0x46299c(i, pos)`; plays (`0x464038`) only
  when that returns non-zero.
- **`0x46299c`**: `0x48c444(0)` (rebuild the default-camera view transform
  into `0x6c5fb4..`, set `[0x6c5ff8] = 0x6c5ffc`, run the light callbacks
  of `0x6c60ac` when `[0x6cbc04]`), `0x48c57c(pos, &v)`, distance
  `dist3d_delta(v)`; volume 0 beyond `p+5`·1024, full table volume inside
  `p+4`·1024, linear `(far − d)/far` between; times `p+8`/100 →
  `0x464618`. If audible, pan from `angle_ratio(v.x, v.y)` (doubled, sign
  by half-circle) → `0x4646dc`. Returns audible (0/1). Gate closed → 0.
- **`0x464038`/`0x473898`**: the glue sets the driver sample's volume
  (`v·[0x4a0e30]·32767/10000` in both halves), pan, loop count and the
  zero callback, then calls `sosDIGIStartSample`; it returns success when
  the call returns 0 **or** the stored status `[0x4a0e50]` is 0 (its
  third argument points to slot `+0x24`, the word that stop, done, volume,
  pan and rate later pass as the sample handle; the DLL writes the sample
  handle word there, see "The device seam"). That status is written only
  by init/uninit (`0x473eac`, `0x4741fc`) and is 0 after a successful init,
  so the result is 1 whatever the driver returns; `0x464038` fails only for
  an invalid handle or a failed sample load (file missing, `GlobalAlloc` or
  `_hread` failure). It does not check whether the sample is already
  playing.
- **`0x41a6d0`/`0x41a994`** (hooks `twist_sound_latch`/`pitch_sound_latch`)
  do not play anything (as
  [input.md "Torso servos and weapon keys"](input.md#torso-servos-and-weapon-keys)
  states): they only ask whether row `0x21` is playing and set a latch when
  it is not. Row `0x21` is started elsewhere (`0x45d91b`). **Hypothesis:**
  a vestigial servo sound.
- **`0x463400(ch)`** (voice start after `0x462b9c`): reads the clock and
  runs `0x4634b0` when the lip script is not empty, else sets `+8` = 1; then
  returns `0x464038`'s result when a context, `[0x49f966]`, `+8` and a
  valid handle are present, else AL = 1.
- **`0x4632c4`** (every frame from the cockpit panel `0x44b620`): reads the
  clock and handles the due queue entry through `0x4634a0` → `0x4634b0`.

Dead code in the sound API (no direct caller and no pointer reference):
`0x4627c8`, `0x462708`, `0x462e24`, `0x462f74`, `0x463060`, `0x4630c0`,
`0x463168`, `0x463464`, `0x464280`, `0x46399c`, `0x463974`, `0x4645dc`,
`0x4647ec`, `0x464824`, `0x473a08`, `0x473a34`, `0x473a8c`, `0x473ab4`,
`0x473ac8`.

### Sound calls from simulation code

Literal row arguments never include 12; row 12 can only come from the
data-driven sites `0x4080dc` (`10 + [type+0x24]`, explosion),
`0x40aafe` (`10 + [type+0xc]`), `0x40b583` (`10 + [type+8]`) and
`0x4198a7` (computed). Measured with every hook original over the 40 suites
([hud.md "The coupling set measured"](hud.md#the-coupling-set-measured), item
6): `sfx_play_at`'s variant draw `0x462908` draws 741 times in 14 suites,
`sfx_play`'s `0x46285c` never.

Sound calls inside hooks:

| Hook | Site | Call |
|---|---|---|
| `ground_collide` `0x418f74` | `0x4195ab` | `0x462830(0x29)` |
| `ground_update` `0x41a360` | `0x41a4ab` | `0x462830(0x29)` |
| `ground_effects` `0x4195c8` | `0x4198a7` | `0x4628e0(computed, A+0x26)` |
| `projectile_flight` `0x40a538` | `0x40a7f6` | `0x462830(0x32)` |
| `impact` `0x40bf74` | `0x40c056` | `0x4628e0(0xb, …)` |
| `vehicle_control` `0x416a04` | `0x416dd5`, `0x416e4a` | `0x4628e0(0x1e / 0x29, A+0x26)` |
| `explosion_create` `0x407f1c` | `0x4080cf`, `0x4080dc` | own `rng_bounded(0x4d260b, 50)` (result discarded), then `0x4628e0(10 + t, …)` |
| `fire_update` `0x46b678` | `0x46b82d` | `0x46297c(0x33, …)` |
| `mech_fly` `0x4198f4` | | engine loop: `0x462ac4(0x2d)` stop, `0x463114(0x2d, rate)` and `0x46297c(0x2d, A+0x26)` |
| `vehicle_update` `0x41aa5c` | `0x41b0bc` | the HUD warning beeps `0x462830`; they alternate on bit 6 of `0x467828`. `0x41aa5c` returns `0x462830`'s EAX on one path, which its caller `0x45f8ac` ignores, so an EAX check of `0x41aa5c` must not require it |
| `twist_sound_latch` `0x41a6d0`, `pitch_sound_latch` `0x41a994` | | `0x462af0(0x21)` (below `0x41a550`/`0x41a808`) |
| `meteor_update` `0x409d2c`, `bullet_create` `0x40b43c`, `fire_create` `0x46b48c` | | `0x4628e0` / sound rows |

Original code (no hook) that calls the sound API: the cockpit/radio code
`0x436abc`, `0x44b514`, `0x44b620`, `0x44cee4..`, `0x44d78f..` and the
mission presentation `0x45d85c`/`0x45dc50`.

The cockpit radio panel (`0x44b514` → `0x44aff0` → `0x462b9c`; per frame
`0x44b620` from the vtable method `0x449bf8`) is the only user that
branches on audio results: `0x462af0(0x1c)` decides whether the noise row
`0x1c` (one variant, no draw) is restarted, and `0x463400`/`0x46356c` drive
its state machine, which itself draws stream B in `0x44b148`/`0x44b344`
and reads `0x467828` ([rng.md](rng.md)). With a successful init the
`0x463400` result is driver-independent (above), so the panel's stream-B
draws depend on the lip files and the virtual clock, not on the driver.

### What can change the oracle's comparison

The comparison covers the clock fields, both RNG states, the player
position/angles/speed/throttle and the mission watches
([tick_log.md](../reference-game/tick_log.md),
`oracle_regression.py:TICK_FIELDS`); hook counters are not compared.

**Cannot** (as long as the same API calls happen in the same order): the
SFX manager (`0x463a14`, `0x463c50`, `0x464118`, `0x464618`, `0x46466c`,
`0x4646dc`, `0x46474c`, `0x4647b8`, `0x4641e8`'s bookkeeping), the glue and
stubs, CD audio, the window hook, `0x46254c` (init), `0x46356c`, the
presentation functions `0x465628`/`0x465cc4`. Their state is audio-owned:
the context tables, `0x4d2a88..0x4d2ec8`, `0x49f8f4..0x49f966`,
`0x4a0e2c..0x4a0e5a`, `0x6b5588..0x6b5684`, `0x6cbd58..0x6cbee0` (literal
cross-reference of every absolute write in the audio ranges: only
`0x49f8ec` (tested by UI and mission start) and `0x49f8f8` (option screen
`0x42bb57`) are referenced outside; `audio_global_writes_xref.txt`).

**Can:**

1. **Stream B** — `0x462830`, `0x4628e0`: one bounded draw per call of a
   row with several variants, only with a context. Removing, adding or
   reordering such calls, or running without a context (no audio device,
   failed `LoadLibrary`, `-s`), changes `rng_b_hex` from the first row-12
   call on.
2. **Virtual clock** — `0x462b9c`, `0x463400`, `0x4634b0` (also via
   `0x4634a0`), `0x4632c4`: each `0x467828` call advances the intra-frame
   virtual clock by 1 ms ([timing.md](timing.md)); `0x4632c4` runs every
   frame. Fewer or more calls shift the value later readers of the frame
   see (e.g. the clock-gated stream-B draws of `0x43408c`). These clock
   calls do not depend on the context: the channel and timeline code runs
   with sound off as well.
3. **View-transform scratch** — `0x46299c` → `0x48c444(0)`. **Open:** this
   is a set-before-use scratch area with 27 call sites of `0x48c444` (also
   in `0x40bc56`, `0x42814a`, `0x46c2ec`, `0x478d82`, …); whether any
   reader depends on the value left by a positional sound was not checked.
   A replacement calls `0x48c444(0)` where the original does.
4. **Driver playback time** — `0x462af0`. Real time, not virtual time, so
   it can differ between runs; with the reference data it only changes the
   latches of `0x41a6d0`/`0x41a994` and whether row `0x1c` is restarted
   (both outside the compared fields), and the hook counters.
5. **Files and heap** — on-demand sample loads (`0x463d4c`: 14/29/17/69/21/23)
   and lip files (`0x463374`) open the VOL-aware stream, which allocates
   from the game allocator `0x47749c`; the cache eviction `0x464390` uses
   `0x4774f0` (0 in the suites). Sample memory itself is `GlobalAlloc`.
   **Hypothesis:** no compared state depends on these heap addresses.

Prediction, not run: one suite with `-s` against its golden diverges in
`rng_b_hex` at the first row-12 sound call or the first frame in which the
voice code's clock calls change, and is otherwise identical.

### Isolation rules for a replacement

What any replacement of the audio code must keep, and where the own code
keeps it:

1. The **stream-B rule**: draw `rng_bounded(0x4d260b, p+6)` exactly when the
   context exists and `p+6` > 1, before the gate, once per call. The `sfx`
   hooks reproduce it ("Sound/clock isolation: the sfx hooks").
2. A **deterministic context**: the oracle and the own build must agree on
   whether `[0x49f8ec]` exists. The oracle ran with a context; `es2sim`'s
   silent device always initialises ("Headless decisions").
3. **Clock**: the same `0x467828` calls in the same order. The timeline
   hooks read the clock at the original's places; exempting the audio call
   sites (return addresses in `0x462b9c`, `0x463400`, `0x4634b0`,
   `0x4632c4`) from the virtual-time model would change the goldens.
   Measured over the 40 suites
   ([hud.md "The coupling set measured"](hud.md#the-coupling-set-measured)):
   the lip queue `0x4632c7` reads once per tick together with the radio
   panel that calls it (73,975 reads after the load, 80 in the load
   windows), the voice start `0x462c08` 334 times, the voice update
   `0x463419` 332 and the lip step `0x4634ba` 2,029 times, the last three
   never in the load window.
4. **Playback time**: `sosDIGISampleDone` modelled from the sample length and
   the virtual clock (or its answers recorded and replayed) makes `0x462af0`
   deterministic; everything else the driver returns is ignored after init.
   `es2sim` answers it from the virtual clock; in the original process it
   stays the driver's real time.
5. The `0x48c444(0)` call of positional sounds is preserved (`sfx_place`
   calls it as the original does) or proven harmless.
6. VFS: `sounds.str`, `HMI\`/`HMX\` samples (`SIMSOUND.VOL`), voice WAVs and
   lip files (`simvoic?`), `data\sound.cfg`; platform: MCI CD audio and the
   `MM_MCINOTIFY` loop; the window hook list `0x4d3b30`.

### In-vitro access to the audio code

The private mapping traps all IAT entries and leaves the SOS slots zero.

- **Pure bookkeeping, testable as original code** with a synthetic context:
  `0x463a14` (avoid the table-full path or trap `SetLastError`),
  `0x463c50` and `0x4641e8`/`0x464118` for slots without flag `0x100`,
  `0x4647b8`, `0x4648a0`, `0x462784`, `0x462fe0`, `0x46356c`, `0x462b30`
  (closes the previous voice through `0x463c50`), `0x4634b0`/`0x4632c4`
  with a stub for `0x467828` (an IAT trap otherwise).
- **Driver-facing**: `0x464038`, `0x464118`, `0x4641e8`, `0x464618`,
  `0x46466c`, `0x4646dc` and everything in `0x473708..` reach the slots;
  fill `0x6cbd60..0x6cbee0` (and the driver handle `0x6b566e`) with own
  recording fakes in the private mapping. `0x464038` additionally needs a
  cached record (`+0x208` set) or a file stub.
- **`0x462830`/`0x4628e0`** can run in vitro with a synthetic table,
  context, the RNG hooks and fake slots; `0x46299c` also needs the view
  globals for `0x48c444`. The `sfx` group's cases and the audio cases do
  this ("In-vitro evidence", "Verification"); other groups keep the context
  at 0 and so never reach SOS
  ([invitro/README.md](../../tools/reference-game/invitro/README.md)).

### Open questions of the boundary

- Whether any `0x48c444` reader depends on the transform left by a
  positional sound (item 3 of "What can change the oracle's comparison").
- Which type tables lead to row 12 at the data-driven sites (live
  frequency: "Sound calls from simulation code").
- The users of the UI callback `0x438e2c` (15 table references) were not
  checked for EAX use (UI only).

## Sound/clock isolation: the sfx hooks

Nine hooks in the adapter group `sfx`
(`tools/reference-game/proxy/sfx_hooks.h`, core `core/src/sfx.c`,
`es2/core/sfx.h`): the six sound API calls that the simulation makes, and
three functions of the clock-reading voice/lip timeline. REA was not used.

| Hook | Function | Called from (direct sites) |
|---|---|---|
| `sfx_play` | `0x462830(i)` play row `i` | 30 sites, every one with a literal row (never 12) |
| `sfx_play_at` | `0x4628e0(i, position)` | 9 sites (`explosion_create`, `meteor_update` ×2, `projectile_create`, `bullet_create` `0x40b43c`, `impact`, `vehicle_control` ×2, `ground_effects`) |
| `sfx_place` | `0x46299c(i, position)` distance volume and pan | `0x4628e0`, `0x46297c` |
| `sfx_refresh` | `0x46297c(i, position)` | `mech_fly`, `fire_update` |
| `sfx_stop` | `0x462ac4(i)` | `mech_fly`, `flyer_part`, `0x45db2b`, the fire destructor `0x46b5a9` |
| `sfx_rate` | `0x463114(i, rate)` | `mech_fly`, `0x43291b` |
| `sfx_voice` | `0x463400(ch)` | the radio panel (`0x44b754`), `0x462940` |
| `sfx_lip` | `0x4634b0(ch)` | `0x463400`, `0x4634a0` |
| `sfx_lip_due` | `0x4634a0(ch)` | `0x4632c4` |

No function of the first six has a data reference or a tail jump to it (byte
scan of the image for the six addresses; no `jmp` to them).

### The split: own code and original code

**Own code** (everything the comparison can see):

- the context test `[0x49f8ec]` (no context: `0x462830`, `0x4628e0` and
  `0x46297c` return the entry EAX/EDX/ECX; `0x46299c` returns EAX = 0 with
  the entry EDX/ECX; `0x462ac4` and `0x463114` return EAX = 0, the
  context, with the entry EDX/ECX);
- the row selection: variant count = byte +6 of the row
  `[4*i+0x4d2b78]` (CMP SI,1 / JLE on the zero-extended byte, so counts 0
  and 1 draw nothing), one draw `rng_bounded(0x4d260b, count)` per call
  with EAX = the row pointer and the entry EDX/ECX, `MOVSX EAX,AX`, index
  += draw, **before** the gate, so muted effects still draw;
- the gate `0x462784` inline (it rereads the context; with one,
  EAX = EDX = (signed `i` < 10 ? `[0x49f8f4]` : `[0x49f8f8]`) != 0;
  without one only AL = 1, which opens);
- the volume `0x47e08d(byte +1, 65000)` inline (EAX = bits 16..47 of the
  signed product, EDX its high dword), `MOVSX` of its low word times byte
  +8, `CDQ`/`IDIV 100`;
- in `0x46299c`: the distance test against byte +4 (near) and byte +5
  (far), both ×1024 and signed: beyond far → 0; inside near → the full
  volume; otherwise `(far − d) · volume / far` (IMUL, CDQ of the low dword,
  IDIV far; 0/0 only for near = far = 0 at distance 0, as in the
  original), then byte +8 / 100; the pan from `angle_ratio`'s AX: below
  8000h `NEG` of the doubled low word, otherwise the doubled word,
  zero-extended; EAX = (level != 0);
- the reread order of the original (row pointer, handle and context are
  read again after every call, never cached) and the index wrap (the
  scaled address `4*i` wraps to 32 bits, the gate's test is signed on the
  full index; there is no range check).

**Original code through the guest bridge**, with the arguments and the
EAX/EDX/ECX the original holds at each call ("Registers at the game calls"):

- the **audible part**: the SFX manager `0x464618` (volume), `0x464038`
  (play), `0x4646dc` (pan), `0x464118` (stop), `0x46466c` (rate) and
  behind them the SOS glue `0x473898`/`0x473aec`/`0x473b80`/`0x473b54`/
  `0x4739ac`, the sample load `0x463d4c` and the SOS stubs into
  `SOS9503.DLL`. With every hook replaced the game plays every sound it
  played before, with the same driver commands in the same order (in
  vitro: the real-callee pass). There is no silent variant and none is
  needed for determinism: the simulation-visible part does not depend on
  the driver (`0x464038` reports success whatever the driver returns after
  a successful init);
- the view helpers `0x48c444(0)` (rebuild the default view transform into
  `0x6c5fb4..`, `[0x6c5ff8]` and the light callbacks; item 3 of "What can
  change the oracle's comparison") and `0x48c57c(position, v)`
  (`transform_point` through `0x48d9e4`; all three paths of `0x48043c` write
  the three dwords of `v`);
- the nested hooks `rng_bounded`, `dist3d_delta`, `angle_ratio` and the
  group's own `sfx_place` keep their modes.

### Registers at the game calls

`c` = context, `h` = handle `[4*i+0x4d2c78]` read just before, `p` = row
pointer, "entry" = the caller's register:

| Function | Call | EAX | EDX | ECX |
|---|---|---|---|---|
| `0x462830` | `0x492f24(0x4d260b, n)` | `p` | entry | entry |
| | `0x464618(c, h, vol)` | `c` | IDIV remainder | 100 |
| | `0x464038(c, h)` | `0x464618`'s | `h` | `c` |
| `0x4628e0` | `0x492f24(0x4d260b, n)` | `p` | entry | entry |
| | `0x46299c(i, pos)` | `p` or the draw | `pos` | entry or the draw's |
| | `0x464038(c, h)` (AL ≠ 0) | `c` | `0x46299c`'s | `h` |
| `0x46299c` | `0x48c444(0)` | entry | entry | entry |
| | `0x48c57c(pos, v)` | `0x48c444`'s | `v` (frame) | `pos` |
| | `0x47de72(x, y, z)` (gate open) | `x` | `x` | `i` |
| | `0x464618(c, h, vol)` | `h` | `c` | 100 |
| | `0x47d32c(x, y)` (level ≠ 0) | `x` | `0x464618`'s | `x` |
| | `0x4646dc(c, h, pan)` | `c` | `pan` | `h` |
| `0x46297c` | `0x46299c(i, pos)` | `pos` | `i` | entry |
| `0x462ac4` | `0x464118(c, h)` | `c` | `i` | `h` |
| `0x463114` | `0x46466c(c, h, rate)` | `c` | `h` | `i` |

The frame address of `v` (EBP−18h; the hook's scratch) reaches only
`0x48c57c`, which does not read its entry EDX, and `transform_point`'s
EAX (`v` again on two of its paths), which the gate overwrites before any
further call or the return.

### Result registers and the exit shims

EDX:EAX are returned exactly (the last callee's, or the entry or fixed
values above). The original's **ECX** at return is the last callee's ECX
(`0x464038` returns its `[EBP−4]`: the entry ECX on its early exits,
otherwise the file-record number), or the entry ECX without a context. The
framework restores the entry ECX in replaced mode, and that ECX is live at
some callers: after `0x4628e0` in `meteor_update` it reaches `radius_hit`
(whose loop counter `MOV CX,[EBP−4]` keeps the high word and can return
it), it is returned by `projectile_create` (`0x40aafe`) and `0x40b43c`
(`0x40b583`), and `fire_update` returns `0x46297c`'s (`ecxlive.py` lists the
candidates). Rather than prove every path dead, each hook has an **exit
shim** (the `control_record` pattern): it copies the arguments, calls the
framework's dispatch and, if the replacement ran, loads the ECX the core
computed; a fallback to the original (bypass, depth limit, other thread)
keeps the original's own ECX. EAX and EDX pass through. At the 30 sites of
`0x462830` and the four of `0x462ac4` ECX is dead (`POP ECX` right after the
call); the shim makes those exact as well. Flags are not part of the
contract (no caller reads them after the call). The shims are chained from
`flyer_part_capture_entry`; `sfx_capture_entry` is the end of the chain.

### Clock and RNG

**No clock read in the six sound API hooks.** The direct-call closure of the
six functions (down to the SOS stubs, the stream library and the RTL thunks;
`closure.py`) contains no call of the clock wrapper `0x467828` and no call
of the `GetTickCount` thunk `0x4968ec` (IAT `0x6cc3e8`; the closure's only
KERNEL32 thunks are `SetLastError` `0x4968e6` and the memory and file thunks
of the sample load). Its indirect calls are the SOS slot stubs (into the
DLL, which runs no DBSIM code, "Driver loading and the SOS entry points"; a
clock the DLL reads itself is not DBSIM's IAT slot and not the virtual
clock) and the stream and archive virtuals of the sample load (`0x463d4c`
→ `0x472f64`, `0x474360`, `0x47a8a4`, `0x47b5cc`, `0x47b9cc`, `0x47cdf8`):
none of the entries of the stream vtables `0x4a0e10`, `0x4a1710`,
`0x4a1738`, `0x4a1760`, nor any other code pointer in
`0x4a0c00..0x4a1800`, reaches `0x467828` by direct calls (`clockreach.py`,
`vtcheck.py`). So these sound calls never move the intra-frame clock, in
any mode. The clock-reading sound code is the voice/lip timeline
(`0x462b9c`, `0x463400`, `0x4634b0` directly and via `0x4634a0`, `0x4632c4`
every frame); it is reached only from the cockpit radio panel (`0x44aff0`,
`0x44b514`, `0x44b620`, all original) and from no hooked subtree. Three of
its functions are hooks of this group ("The voice/lip timeline"); their
clock reads stay calls of `0x467828` at the original's places.

**Stream B.** One draw per call of a row with more than one variant, and
only with a context, in the original's order. With the reference
`sounds.str` only row 12 has variants (three), and only the data-driven
sites of `0x4628e0` can select it ("Sound calls from simulation code"), so
live `hk_sfx_play_call_492f24` stays 0 and `hk_sfx_play_at_call_492f24`
counts the sound draws of stream B.

### Residue

The originals have no frame locals except `0x46299c`'s `v` (written by
`0x48c57c` before it is read). The direct callees read no uninitialized
stack: `0x464038`'s `PUSH ECX` slot `[EBP−4]` is written before every
read; `0x464618`, `0x4646dc`, `0x464118`, `0x46466c` and the glue they
reach have no locals; `0x48c444`'s locals are written by `0x48db70` and
the copy of `[0x6c5ff8]` before use. In replaced mode the bytes left below
a caller's ESP differ from the original's, as for every replaced hook; no
reader of them is known, and every replaced caller (`ground_update`,
`ground_collide`, `ground_effects`, `projectile_flight`, `impact`,
`vehicle_control`, `explosion_create`, `fire_update`, `meteor_update`,
`mech_fly`, `flyer_part`, …) runs these functions at a host-stack
coordinate.

### The voice/lip timeline: `sfx_voice`, `sfx_lip`, `sfx_lip_due`

The clock-reading sound code belongs to the cockpit radio: a voice channel
(5 records of 42h bytes at `[0x4d2d6c]`: +0 sample handle, +4 next event
time, word +8 lip value, −1 = end, +0Ah script pointer, +2Fh script buffer)
steps through its lip script against the clock, and the event queue (count
word `0x49f964`, record pointers `0x4d2ec8[k]`, 8-byte records
`0x4d2e76 + 8k`: time, channel) wakes it again. The clock reads are made by
own code as `g->call` of the wrapper `0x467828` at the original's places, so
the `GetTickCount` thunk and with it the virtual clock see exactly the same
calls in the same order.

- **`0x4634b0` lip(ch)** (`sfx_lip`): one clock read (entry registers),
  then a loop: the lip byte at `[ch+0Ah]` (pointer incremented, MOVSX);
  −1 ends the script (word `[ch+8]` = FFFFh; EAX = ch, EDX = −1, ECX = the
  clock); otherwise the next byte (MOVSX) is added to the event time
  `[ch+4]`, and the loop repeats while the clock is above it (JA,
  unsigned). Then word `[ch+8]` = the lip value and the event goes into
  the queue. Two quirks of the original are reproduced: the queue test
  compares the **last time step** (EAX), not the event time, with the
  first queued event's time (`CMP EAX,[EDX]` / JAE at `0x46350c`); when
  the step is below it, the first entry is dropped (count − 1, entries
  1.. move down); and the insert is always at position 0, because the
  search loop's increment (`0x463530`) is unreachable, so a nonempty
  queue loses its old first pointer. The record at index count gets
  (time, ch), `q[0]` points to it, count + 1. Result EAX = the record,
  EDX = ch, ECX = 0.
- **`0x4634a0` lip_due(ch)** (`sfx_lip_due`): `0x4634b0(ch)` with
  EAX = ch; ECX = ch (`POP ECX`). Its only caller is the per-frame queue
  pass `0x4632c4`.
- **`0x463400` voice(ch)** (`sfx_voice`): script pointer = buffer, EDX =
  MOVSX of the first byte; `CMP EDX,0FFh` / JE (`0x463411`, `0x463417`)
  can never be taken (a sign-extended byte is never 0FFh; PROVED), so the
  clock is always read (EAX = buffer, EDX = that byte) into `[ch+4]` and
  `0x4634b0(ch)` follows (EAX = the clock); ECX = ch. With a context, the
  voice byte `[0x49f966]`, a nonzero lip value and a handle other than −1
  the sample plays (`0x464038(c, [ch])`, EAX = the handle, EDX = the lip
  step's EDX, ECX = ch), otherwise `MOV AL,1` over the lip step's EAX.
  Callers: the radio panel (call at `0x44b754`, tests AL) and
  `0x462940` (returns it).
- **`0x4632c4`** (the per-frame queue pass, ≥ 1000 calls in every suite)
  **cannot be hooked**: it begins with `PUSH EBX/ESI/EDI` (3 bytes) and
  then calls the clock, and the framework relocates only whole
  whitelisted instructions of at least 5 bytes. It stays original in the
  original process; its clock read and queue shift run as before, and its
  call of `0x4634a0` enters `sfx_lip_due`.

Registers: the ECX at return is dead at every caller of the three (each
`POP ECX` after the call); the exit shims return it exactly anyway.

### In-vitro evidence

`differential_harness --group sfx` (`invitro/sfx_cases.h`): per hook
1,001,024 direct cases (100,000 on the quick tier) against logging stubs
for every game callee, with random rows, row pointers, handles, context
and gate words, indices with 40000000h multiples, planned draws (in and
out of `0..n−1`, negative), planned distances around near and far, and
stubs that change what the original rereads; compared: EDX:EAX, the ECX
at return, the ordered calls with arguments and entry registers, the
arena, the globals (context and gate words, both tables, stream B) and
the callee counters. 256 installed 31/a7 poison cases per hook through the
exit shim (EAX/EDX/ECX).

A real-callee pass per sound API hook: 768 sequences of eight sound calls on
one synthetic SFX world (context, 64 sample slots, four cached file records
with driver sample structures, driver handle word, master volume, status)
through the **real** SFX manager, SOS glue and SOS stubs into recording fake
driver slots (`StartSample`, `StopSample`, `SetSampleVolume`,
`SetPanLocation`, `SetSampleRate` and the five MIDI entry points behind slot
flag bit 0), with the real gate, product and RNG stream B; the hook under
test original, then replaced, with `rng_bounded`, `dist3d_delta`,
`angle_ratio` and the other five sfx hooks in rotating modes. After **every
call** the 114 bytes of stream B, the three result registers and the driver
command count must match; at the end the whole driver command log (slot,
arguments, results), the arena and the globals.

The timeline's real-callee pass: sequences of eight calls of voice, lip,
lip_due and the original `0x4632c4` on three channels with scripts and the
queue, through the **real** clock wrapper `0x467828` into a fake
`GetTickCount` in the import slot `0x6cc3e8` and the real SFX manager into
the fake driver; after every call the number of clock reads, the queue, the
channels and EAX/EDX/ECX must match between the hook original and replaced.
25 branch sites, 24 both ways, one PROVED (`0x463417`).

### Open questions of the sfx hooks

- The rest of the timeline is original code in the original process:
  `0x4632c4` (not hookable) and the voice start `0x462940`/`0x462b9c` (LRU
  channel, lip file through the stream library inside an exception frame,
  one clock read), together with the radio panel
  `0x44aff0`/`0x44b514`/`0x44b620`, whose own stream-B draws (`0x44b148`,
  `0x44b344`) depend on the timeline. `es2sim` runs them as own rows
  ("Audio in the own process").
- `0x462af0` (is playing) answers from the driver's playback time
  (`sosDIGISampleDone`); not hooked (item 4 of "Isolation rules for a
  replacement").
- Whether any reader depends on the view transform that `0x48c444(0)`
  leaves; the hooks call it exactly as the original does.

## The VSHELL shell audio contract

Read from `VER95/VSHELL.EXE` (image base `0x400000`); import names resolved
through the IAT thunks (`0x46bb02` `LoadLibraryA`, `0x46bb08`
`GetProcAddress`, `0x46bc64` `mciSendCommandA`, `0x46bc5e`
`mciSendStringA`); WAV header facts read from `VOL/SHLSOUND.VOL` (member
headers only). Static reading, not run in vitro or live. Source names come
from the assertion strings (`src\sound.cpp`, `src\SOSEZ.CPP`,
`.\inc\sfx_fix.h`): the shell links the same SFX/SOS library as DBSIM
("DBSIM's audio boundary"), with its own globals.

### Shell audio in short

- **No CD music and no MIDI in the shell (C).** The ten `mciSendCommandA`
  calls (`0x42e311..0x42e59b`, device `cdaudio`, id `[0x473274]`) sit in
  `0x42e2f9` (stop + close), `0x42e343` and `0x42e4d6` (open, time format
  TMSF, play one track with notify) and `0x42e448`/`0x42e48c` (status),
  reached only from the wrappers `0x42dc25`, `0x42dc40`, `0x42dc53`,
  `0x42ddf4`, `0x42de07`, which have **no caller and no pointer
  reference** anywhere in the image: dead code; `[0x473274]` stays −1.
  The SOS MIDI half is disabled exactly as in DBSIM (MIDI device `0xffff`
  at `0x42de9c`/`0x42deb6` → flag `[0x4732a2]` := 0 at `0x42e5ed`), so
  `MELODIC.BNK`/`DRUM.BNK` (`0x42e933`/`0x42e94a`) are never loaded;
  `\sos\song.hmp` (`0x46f365`) has no code reference. The
  `mciSendStringA` sites (`0x41e56d..0x41e79a`) are the AVI movies.
  `0x42d9c3` and `0x42dcf9` are `SFX::setLooping` and `SFX::stopAll`
  (their own assertion strings), not CD music, so the shell has no CD-audio
  behaviour to reconstruct; DBSIM's CD music is a separate, live path.
- **The shell music is a looping WAV (C):** `hmi\shell1.wav` or
  `hmi\shell2.wav` (choice by prefs byte 5, which every start-up flips and
  writes back to `data\prefs.cfg`), played through SOS with a software
  volume ramp for "Music" on/off. The Preferences Music toggle ramps this
  WAV music (`0x42f7f0`/`0x42f894`); it does not stop or start CD music.
- **Three effects (C):** `hmi\gm_69.wav` on a button press,
  `hmi\bptlt2.wav` when a service-bay bar button changes the screen,
  `hmi\lswitch2.wav` once per process when the main-menu background starts
  animating. Gated by prefs byte 1.
- **Start-up failure (C):** VSHELL ends at start-up only when the 0x4c-byte
  sound object cannot be allocated from the arena ("Could not create Sound
  Manager.", `0x401f9d..0x401fc1`, exit code 0). A missing or failing SOS
  driver does **not** end it: the init result is only debug-printed
  (`0x42f39e..0x42f3ab`), the driver failure is a severity-2 assertion
  ("Sound driver initialization failed.", `0x42e81b`, logged only), and
  every later play fails silently.

### Objects, globals and the SOS boundary

| Item | Address | Meaning (C unless marked) |
| --- | --- | --- |
| sound flag | byte `0x482356` | 1 by default (`0x40798a`), 0 with `-s`/`-S` (`0x40172a`), `-v` (`0x401757`), `-?` (`0x4017f1`) |
| SFX object | `[0x473338]` | `new 0x4c` (`0x4086b7`) + constructor `0x42c591`; 0 = no sound; window `+0x44` := `[0x481f38]` (`0x42f37a`) |
| init arguments | `[0x473340]` = 2,000,000, `[0x47333c]` = 10 | `0x42c490(sfx, 2000000, 10, 20)`: cache limit `+0x18`, 10 file records of `0x21c` bytes (`+0x10`, `+0x34`), 20 sample slots of 40 bytes (`+0x14`, `+0x38`); handles are `generation << 16 \| slot` with the generation `+0x20` starting at 1, so the four handles are `0x10000`, `0x20001`, `0x30002`, `0x40003`; failure is −1, never 0 |
| sample handles | `[0x48ceac]` gm_69, `[0x48ceb4]` bptlt2, `[0x48ceb8]` lswitch2, `[0x48ceb0]` shell music | opened at `0x42f3c8..0x42f44f` (`SFX::open` `0x42c7f0`, mode 1 = digital; it only records the name, the file is read at the first play) |
| music volume | `[0x473344]` | 0..100, initial 0 (data) |
| focus flag | `[0x473348]` | 1 while the shell holds the SOS driver (`0x42f5ee`), 0 after `0x42f7aa` |
| master volume | `[0x473278]` | 100 (data); its setter `0x42e1dc` has no caller |
| prefs block | `0x48259c` (+0 music, +1 effects, +5 music choice), committed copy `0x4825d2` | [cfg.md](../formats/cfg.md) |

**Driver loading (C).** `0x46ab1d` (from `0x42de5b`) is DBSIM's `0x4958fd`
again: `GetVersion`; Windows NT or major ≥ 4 loads `sos9503.dll`
(`0x480ed8`), else `sos32s03.dll` (`0x480ee4`); module `[0x5dde08]`; a
36-byte name table at `0x4800a4` (slot pointer + 32-byte name) with
**100** entries fills the slots `0x5dde10..0x5ddf9c` by `GetProcAddress`;
load failure returns `0x390000`; `0x46ac3f` frees the library at use
count 0. Each SOS call goes through a one-line stub in
`0x46ac6d..0x46b4cc`. No SOS DLL is imported. 50 stubs have a caller:

| Group | Entry points (calling site) |
| --- | --- |
| Init / shutdown | `sosDIGIDetectInit` (`0x42e690`), `…FindFirst` (`0x42e6ad`), `…DetectUnInit` (`0x42e6bb`, `0x42e6ca`), `sosDIGIInitSystem` (`0x42e6e6`), `sosTIMERInitSystem` (`0x42e6ed`, `0x42e89d`), `sosDIGIInitDriver` (`0x42e807`), `sosDIGISetActiveWindow` (`0x42e862`, `0x42e9bf`), `sosDIGIStopAllSamples` (`0x42e9b0`), `sosDIGIUnInitDriver` (`0x42e9cc`), `sosTIMERUnInitSystem` (`0x42e9e0`, `0x42ea97`), `sosDIGIUnInitSystem` (`0x42e9e5`), `sosGetErrorString` (`0x42eab1`) |
| Focus | `sosDIGIAcquireDriver` (`0x42deee`), `sosDIGIReleaseDriver` (`0x42df07`) |
| Service | `sosDIGIUserService` (`0x42df1d`; `0x42f2b1` only for streams) |
| Samples | `sosDIGIStartSample` (`0x42e0ba`), `sosDIGIStopSample` (`0x42e12c`), `sosDIGISetSampleVolume` (`0x42e269`); `sosDIGISampleDone` (`0x42e1af`), `sosDIGIGetBytesProcessed` (`0x42e1d4`), `sosDIGISetPanLocation` (`0x42e2ba`) and `sosDIGISetSampleRate` (`0x42e295`) sit in library functions without a path from the shell code (`0x42e18a` ← `0x42d272`, `0x42e1b7`, `0x42e29d`, `0x42e272` ← `0x42d8fd`: no caller or pointer found) |
| Memory | `sosPrepare32Memory` (6 sites `0x42ec4f..0x42f0c4`), `sosUnPrepare32Memory` (6 sites `0x42ecad..0x42f220`) |
| Streams | `sosDIGIStreamAcquire`/`Release`/`PrepareBuffer`/`UnPrepareBuffer`/`AddBuffer`/`Start`/`Stop` (`0x42ef2a..0x42f1e2`): open mode 2 only; the shell opens with mode 1 |
| MIDI (dead) | 19 `sosMIDI*` entries (`0x42dfef..0x42ea9c`): only behind the MIDI flag `[0x4732a2]` or slot flag bit 0 (open mode 0), neither of which the shell sets |

**Initialisation `0x42c490` → `0x42de44(window, cfg = sfx+0x24)` (C).**
Loads the DLL, copies the cfg words to `0x47328c..0x473294`, then calls
`0x42e5bc(device = cfg+6, 0xffff, window, window)` and, if that fails, the
same with device 1; a second failure frees the DLL. `cfg+6` is the
`Driver` word `+0x2a`, which the constructor always sets to 1, so both
attempts use device 1. The result goes only to a debug print (above).

**Timer (C).** `sfxWndProc` `0x404e21`, the 15 ms `SetTimer` procedure of
the shell's main loop ([vshell_process.md](vshell_process.md)), calls
`0x42d334` → `0x42df0d(0, …)` → `sosDIGIUserService(driver)` on `WM_TIMER`
while `[0x473338]` ≠ 0 (`0x404e2e..0x404e5c`); the stream service `0x42f264`
is skipped (first argument 0). It is the driver's mixing service (**H**); it
changes no shell state.

### sound.cfg

C. Constructor `0x42c591` (cross-check of
[cfg.md](../formats/cfg.md#ini-files-soundcfg-and-keyjoycfg)): path
`0x40d99d("data\sound.cfg")` = install root `0x482369` + `data\sound.cfg`;
the install root is empty after `drive.cfg`
([vshell_process.md](vshell_process.md), start-up step 2), so the name
passed to `GetPrivateProfileStringA` is relative, with the same open
question as DBSIM's ([file_io.md](file_io.md#open-questions)). Section
`Sound`, default `""`, buffer 0x28:

| Key | Rule | Field |
| --- | --- | --- |
| `Driver` (`0x42c5fc`) | read, result ignored; field := 1 | `+0x2a` |
| `Buffers` (`0x42c61d`) | `_atol`; 1..64 kept, otherwise 5 | `+0x30` |
| `Rate` (`0x42c658`) | `_atol` = 11 → `0x10`, otherwise `0x20` | `+0x24` |
| `Width` (`0x42c690`) | `_stricmp(value, "Mono")` = 0 → 4, otherwise 8 | `+0x28` |

Constants: `+0x26` = 1, `+0x2c` = `+0x2e` = 0, `+0x32` = `0x200`. These
only describe the SOS driver's output (rate code, sample width or
channels, buffers; **H** from their use as `sosDIGIInitDriver`
parameters at `0x42e7e1..0x42e807`). No value changes which sound plays,
when, or at what volume; samples keep their own format ("Files and playback
format"). **For an own shell: no observable effect beyond output quality.**

### Events and effects

Common gate of the three effect functions `0x42f501` (bptlt2),
`0x42f547` (gm_69), `0x42f58d` (lswitch2): `[0x473338]` ≠ 0 **and** prefs
byte 1 (`[0x48259d]`) ≠ 0 **and** focus `[0x473348]` ≠ 0; then
`setVolume(handle, 100)` (`0x42d78c`) and `play(handle)` (`0x42cf77`).
Nothing is stopped first; a press while gm_69 still sounds starts it
again through the driver (restart or second voice is decided inside the
DLL, open).

| Event | Trigger (C) | Effect |
| --- | --- | --- |
| Button press, text button (vtable `0x46e9b0`, handler `0x40a0bf`, constructor `0x40a004`) | event type `0x20` with byte `+0x24` = 2 (left press) or 4 (right press; the mouse producer `0x4090b3` posts 2/1 for the left and 4/3 for the right button), button enabled `+0x49` ≠ 0, sound flag `0x482356` ≠ 0, event dword `+0x25` = 0 (`0x40a0ca..0x40a116`) | gm_69 (`0x42f547`), then the base handler `0x409d8a` |
| Button press, image button (vtable `0x46ea5c`, handler `0x40a3a2`, constructor `0x40a2c4`) | type `0x20`, `+0x24` = 2, enabled, no movie open (`[0x470eb0]` = 0), no movie sequence (`[0x470fb0]` = 0), sound flag (`0x40a4e9..0x40a53d`); `+0x25` not tested | gm_69, then the pressed state and the handler |
| Button press, image-button subclass (vtable `0x46ead8`, handler `0x40a6e9`, constructor `0x40a6b0`; 11 instances built at `0x435ea3..0x436d10`) | type `0x20`, `+0x24` = 2, enabled, sound flag (`0x40a776..0x40a796`) | gm_69, pressed state, handler on the press |
| Service-bay bar button that changes the screen: MAIN MENU `0x43a66c` (call `0x43a71b`, only when the screen word `0x4758b8` ≠ 0), SAVE `0x43a768` (`0x43a815`, word ≠ 1), WEAPONS `0x43a862` (`0x43a916`, ≠ 2), REPAIR `0x43a962` (`0x43aa16`, ≠ 3), BUILD `0x43aa62` (`0x43ab16`, ≠ 4), ARMORY `0x43ab62` (`0x43ac0e`, ≠ 5), CREW `0x43ac5a` (`0x43ad0e`, ≠ 6), MISSION `0x43ad5a` (`0x43ae89`, ≠ 7) | in the handler's activation branch (`[0x46c280]` ≠ 0), after the old screen was left, the new one built and the word stored; the button of the current screen plays nothing | bptlt2 (`0x42f501`) |
| Main-menu background, first animation step | `0x431830` is the callback (`+0x3d`, called by `0x41fc28`) of the animated background `[0x48d1a0]` (class `0x40ce0c`, an event `0x200` every 500 ms, [vshell_presentation.md](vshell_presentation.md)); first call: `[0x473750]` = 0 → `0x42f58d`, `[0x473750]` := 1 (`0x431849..0x43185e`) whether or not the gate let it sound; at frame index 5 `[0x47374c]` := 1 and the menu buttons appear (`0x431718`); after that the callback does nothing | lswitch2 once per process, about 500 ms after the main menu first appears |
| Sound focus gained `0x42f5d3` (start-up `0x401885`; `WM_SETFOCUS` without a movie, `0x405485` → `0x42f905`; after movies `0x41ef9b`, `0x41f10f`) | `[0x473338]` ≠ 0 and `[0x473348]` = 0 | focus := 1; `sosDIGIAcquireDriver` (`0x42c73f`; failure: severity-2 "cant gain focus"); music loops endlessly (`setLooping(music, 0)`); gm_69 volume 0 and play (inaudible); play the music at the stored volume `[0x473344]`, **from its start** (failure: severity-2 "music cant play"). Not gated by prefs byte 0 or 1 |
| Sound focus lost `0x42f6a8` (`WM_KILLFOCUS` `0x40544b` → `0x42f8ff`; help `0x431ea2`; movie sequence `0x41ea37`; dropship `0x4447c4`; shutdown `0x42f7c2`) | `[0x473338]` ≠ 0 and `[0x473348]` ≠ 0 | `setLooping(music, 1)`, `SFX::stopAll` `0x42dcf9` (every playing slot stopped at once, no fade), `sosDIGIReleaseDriver` (`0x42c72c`), focus := 0; around it about 10,000 debug prints (`0x46a834`, loop `0x42f75d..0x42f778`) |
| Music fade-in `0x42f894` | `[0x473338]` ≠ 0 and prefs byte 0 ≠ 0 | while `[0x473344]` < 100: pump (`0x406328`; a nonzero return ends the loop); when `GetTickCount` advanced by **more than** 10 ms: volume + 1 and `setVolume(music, v)`. 0 → 100 is 100 steps: at least 1.1 s, about 1.6 s with a 15.6 ms tick |
| Music fade-out `0x42f7f0` | `[0x473338]` ≠ 0 and prefs byte 0 ≠ 0 | the same loop downwards while the volume is **above 1**: it ends at volume 1, not 0, and the sample keeps playing |
| Movies (player `0x41e98c`, with `[0x482359]` ≠ 0 (**H** movies enabled) and a queued movie in `0x485748`) | `0x41ea06..0x41ea37` | `[0x470fb0]` := 1, fade-out, focus lost; after the queue: focus gained + fade-in (`0x41ef9b`/`0x41efa0` on the mission-screen path, else `0x41f10f`/`0x41f114`) |
| Theatre 5 dropship (`0x44472f`, `0x4447ad..0x4447c4`) | queues `dropship.avi` (0x55) | fade-out, focus lost (the player restores both after the movie) |
| Preferences Music check box (`0x437351` → `0x436ed1(0)`) | byte 0 = 1: fade-out, then byte 0 := 0; byte 0 = 0: byte 0 := 1, then fade-in (`0x436eee..0x436f1a`) | the music ramps; the screen waits for the ramp |
| Preferences Sound Effects check box (`0x4373b2` → `0x436ed1(1)`) | byte 1 toggled (`0x436f24`) | effects on or off from the next event; nothing stops |
| Preferences Cancel `0x437220` | restore the block from the committed copy (`0x40ddb6`), then byte 0 = 1 → fade-in; byte 0 = 0 → byte 0 := 1, fade-out, byte 0 := 0 (`0x43725f..0x437285`) | the music follows the restored choice |
| Preferences Accept `0x4372e1` | commit + write `prefs.cfg` | no audio action |
| Movie error with `error.str` message 5 (`0x41ed8d..0x41edbb`) | prefs +6 := 0, write, box, then `0x42f7c2` | sound shut down, quit flag |
| Loop end (`0x40988c` → `0x42f7c2`) | focus lost + `0x42c6d2` (driver uninit, `FreeLibrary`), `[0x473338]` := 0 | the music stops without a fade |

The prefs setters `0x40ddfe`/`0x40de35` would call a per-byte callback
from the table `0x482608` (54 entries); the table is zeroed (`0x40dc44`)
and its only setter `0x40e0dd` has no caller, so their "notify" argument
has no effect (C).

### Start-up sequence and the music choice

C. `0x42f2f4`, called at `0x401f94` after the root window:

1. Sound flag 0 → return 1 (no object, no file access, prefs untouched).
2. `new 0x4c` + constructor (`sound.cfg`); allocation failure → exit code
   := 0 (`0x408d1a(0)`), return 0 → the caller's box and early return.
   This is the only start-up path to "no sound manager → exit code 0"
   ([vshell_process.md](vshell_process.md)); driver failures continue
   silently.
3. Window into `+0x44`, `0x42c490` (driver; result ignored).
4. Open gm_69, bptlt2, lswitch2; then **prefs byte 5 ≠ 0 →
   `hmi\shell1.wav`, = 0 → `hmi\shell2.wav`** (`0x42f40f..0x42f44f`). A
   zero handle would show "Cannot load sound." (`0x42f45c..0x42f48e`);
   handles are never 0, so the box cannot appear.
5. Byte 5 := byte 5 xor 1 (`0x40de35(5, 0)`), commit the whole block
   (`0x40dd6e(1)`) and write all 54 bytes to `data\prefs.cfg`
   (`0x40dd0a`): **consecutive VSHELL runs alternate the two pieces**,
   including the runs after a mission (`-X3`/`-X4`).
6. `setLooping(music, 0)`, `setVolume(music, [0x473344] = 0)`; return 1.

Then, after the 22 screen constructors, `0x401885` gains the focus (the
music starts at volume 0) and the entry dispatch fades in: `-X3`/`-X4`
after the results path (`0x4018bc`), `-X6` before the main menu
(`0x4018c6`); the other entries play the intro movies, whose player does
fade-out and fade-in itself. With prefs byte 0 = 0 nothing fades in and
the music plays at volume 0 for the whole run (inaudible); once it was
switched on and off again in Preferences it plays at volume 1.

### Files and playback format

C. The effects and the music come from `SHLSOUND.VOL` (a shell-only archive,
[vol.md](../formats/vol.md)) through the VOL-aware stream (`0x403065` in
the cache `0x42cbbf`, `0x4032b1` in the loader `0x42eaf7`); the `_lopen`
loader `0x42ede6` and the BNK loads are the MIDI-mode path (slot flag bit 0)
and are not used. A missing file is a severity-3 assertion ("SFX::cache:
Unable to open file %s", logged) and the play fails silently.

| Member | Format (header) | Data bytes | Played bytes | Duration |
| --- | --- | --- | --- | --- |
| `HMI\GM_69.WAV` | PCM, mono, 8-bit, 22,050 Hz | 1,416 | 1,372 | 64 ms |
| `HMI\BPTLT2.WAV` | PCM, mono, 8-bit, 22,050 Hz | 4,848 | 4,804 | 0.22 s |
| `HMI\LSWITCH2.WAV` | PCM, mono, 8-bit, 22,050 Hz | 84,876 | 84,832 | 3.85 s |
| `HMI\SHELL1.WAV` | PCM, mono, 8-bit, 11,025 Hz | 1,003,104 | 1,003,060 | 91.0 s |
| `HMI\SHELL2.WAV` | PCM, mono, 8-bit, 11,025 Hz | 1,030,021 | 1,029,977 | 93.4 s |

The rates and played bytes are confirmed from the data through the own
loader (`//shell:sounds_test` with `ES2_DATA_ROOT`).

The loader `0x42eaf7` assumes the canonical 44-byte header: on `RIFF` it
takes the rate from offset `0x18`, channels from `0x16`, bits from `0x22`
(8 bits → unsigned flag `0x8000`), the samples from offset `0x2c` and
**length = the dword at offset `0x28` minus `0x2c`** (`0x42ebd4..0x42ebd7`),
so the last 44 bytes of each sample are not played ("Played bytes"); all
five members have exactly a 44-byte header (member length − data length
= 44). A non-RIFF file would play as raw 8-bit mono 11,025 Hz
(`0x42ec13..0x42ec2b`).

**Volume (C).** `setVolume` stores the value in the slot (`+0x10`) and,
while the slot is playing (flag `0x100`), applies it at once
(`0x42e20e`): driver volume = `v · 100 · 32767 / 10000` (master 100,
unsigned division, truncated), the same 16-bit value for left and right
(`0x42e237..0x42e269`); a start applies the stored value the same way
(`0x42e03d..0x42e067`). The gain is linear in `v`/100 (100 → 32,767,
1 → 327). Loop count: 0 → driver −1 (endless), n → n − 1
(`0x42e076..0x42e096`).

### What an own shell must reproduce

Observable, to reproduce:

1. The four sounds with the triggers and gates of "Events and effects":
   gm_69 on every press of an enabled button of the three classes (text
   buttons also on a right press, not for events with `+0x25` ≠ 0); bptlt2
   only on a real bay-bar screen change, after the new screen is built;
   lswitch2 once per process on the first background step of the main
   menu; the music looping from its start whenever the shell gains the
   sound focus.
2. Prefs byte 1 gates the three effects; byte 0 only the ramps. The
   ramp: one volume step per tick of more than 10 ms between 0 or 1 and
   100, the screen blocked meanwhile (only the message pump runs); off
   ends at volume 1.
3. Music and effects stop at once on focus loss (window deactivation,
   help, movies, exit), and the music restarts from the beginning on
   focus gain; movies fade out before and fade in after.
4. Prefs byte 5 chooses shell1/shell2 and is flipped and written to
   `data\prefs.cfg` at every start with sound on (a file effect: the
   prefs bytes the own shell writes must match).
5. `-s` (and `-v`, `-?`): no sound, no button sounds, no byte-5 flip.
6. Sample length = data − 44 bytes; linear gain `v`/100; samples at
   their own rate (11,025 or 22,050 Hz, 8-bit unsigned mono).

No observable effect (need not be reproduced): the SOS DLL and its 100
names, the driver user service on the 15 ms timer (an SDL3 stream needs
none), `sound.cfg` (output format only), the dead CD-audio and MIDI code,
`MELODIC.BNK`/`DRUM.BNK`, the inaudible gm_69 play at focus gain (volume
0; **H** a preload), `setLooping(music, 1)` before `stopAll`, the debug
prints and assertion counters (`0x48dd08`), the unreachable "Cannot load
sound." box, the 2,000,000-byte cache and its eviction (everything
fits), `SFX::stopAll` scanning only the first 10 of the 20 slots (the
shell uses 4). A missing audio device leaves the shell silent and
running.

### Open questions of the shell audio

- Inside `SOS9503.DLL`: what a second `sosDIGIStartSample` on a playing
  sample does (restart or second voice), and the exact meaning of the
  `sound.cfg` codes; only matters for an exact gm_69 repeat.
- Whether `GetPrivateProfileStringA` finds `data\sound.cfg` (shared with
  DBSIM, file_io.md, Open questions); irrelevant for an own shell.
- Not observed live: an API trace of `sosDIGIStartSample` (sample pointer)
  while pressing a main-menu button, a bay-bar button, on the main menu's
  first appearance, on Alt+Tab and on the Preferences Music box would
  confirm "Events and effects" and "Start-up sequence and the music
  choice".

## The own shell's sounds

### Code and decisions

Code: `shell/src/sounds.c` (logic, no I/O), `shell/src/gui/sound.c` (player
over a device interface), `shell/src/gui/sound_sdl.c` (SDL3 device), the
listening tool `shell/tools/es2sound.c`; written from "The VSHELL shell
audio contract", no further static reading.

- **Verified:** `//shell:sounds_test` (unit checks on synthetic prefs and a
  fake device; with `ES2_DATA_ROOT` the five `SHLSOUND.VOL` members parse
  to the rates and "Played bytes" of "Files and playback format"). The
  prefs byte-5 flip is a unit test of "Start-up sequence and the music
  choice" (the written block = the loaded block with byte 5 xor 1,
  committed); **not in vitro**: the shell in-vitro harness has no op for
  `0x42f2f4` → `0x42c591` (it would need the SOS loader stubbed), so the
  byte effect rests on the static reading of `0x42f40f..0x42f44f`/`0x40de35`.
- **Decisions:** `sound.cfg` is ignored (output format only); SDL converts
  each sample's own format. A start while the sample still sounds restarts
  it (SOS's restart-or-second-voice is open). The inaudible gm_69 play at
  focus gain and the SOS user service are left out. An endless loop is fed
  by `es2_shell_audio_service` (one more pass queued while less than one
  pass is left), so the caller must call it every frame. A non-RIFF file
  plays whole (the length on that path was not read).

### The fades pump messages

Read from VSHELL's listing, `0x42f7f0..0x42f8fe`:

- Both fades take `GetTickCount` (`0x46baf0`) as the start, then loop:
  **pump `0x406328` first** (`0x42f8e9` up, `0x42f86f` down); a nonzero
  return (WM_QUIT) leaves the loop at once; then the volume test (up:
  `[0x473344]` < 100 at `0x42f8f2..0x42f8fa`; down: `[0x473344]` − 1 > 0 at
  `0x42f878..0x42f87e`); then, when the tick count advanced by more than
  10 ms (`sub`/`cmp esi=10`/`jbe`), a new start, volume ± 1 and
  `setVolume` (`0x42d78c`). The fade-out is bracketed by two debug prints
  (`0x46a834`), the fade-in has none. A fade that is already at its end
  pumps once.
- The pump only dispatches to the window procedure; the fade calls neither
  the input poll `0x41e0bf` nor the event dispatch `0x46848c`. So during a
  fade paints, focus changes and close are handled as always, clicks and
  keys are queued (button messages, key ring) and reach the screen after
  the fade, in the main loop (static reading; not observed live).
- Own (`shell/src/gui/app_sound.c` `es2_app_sound_ramp`, `app.c` `ramp`):
  the same order with the shell's pump (`es2_gui_platform.pump`, as the
  movie player); the session's end (`SDL_EVENT_QUIT`, the pump's 1) ends
  the fade and sets the shell's quit. Between passes `Sleep(1)` instead of
  VSHELL's spin (own; the steps keep the > 10 ms rule). Tests:
  `//shell:app_sound_test` (virtual clock: 101 pumps for 0 → 100 at 11 ms,
  301 at 5 ms, a step every second pass at 10 ms, 100 pumps for 100 → 1,
  one pump at the end, quit on the fifth pump after four steps, music off
  without a pump, a click and a key during the fade queued and not
  dispatched).

### Output quality of "Music off"

Compared live in es2shell and in the original VSHELL on `rebuild-r111`: the
Preferences' Music check box fades the music down in both and keeps it
playing quietly (volume 1 of 100; the same `setVolume` formula,
1 → 327 / 32,767). The original sounds noticeably noisier at that volume
(**hypothesis:** the SOS driver's mixing and output quantise the quiet
sample; not traced in the DLL). es2shell keeps its output without that
noise. This is a difference in output quality only (the driver's output
format has no other observable effect), not in behaviour: which sounds
play, when, and at which volume stay VSHELL's.

## Audio in the own process

The audio code DBSIM reaches runs in `es2sim.exe` as own code in
`sim/src/audio.c` (`es2/sim/audio.h`), and `SOS9503.DLL` is not loaded.
Every fact is read from the `llvm-objdump -d -M intel` listing of the
reference DBSIM (each function's comment in `audio.c` names its listing
range); `SOS9503.DLL` was read the same way for the contract of the entry
points DBSIM calls (described below in our words, no code taken). REA was
not used. **Validated in vitro against the original's machine code**
("Verification") and by the replays.

### Service rows

26 service rows (`sim/dispatch/services/audio.h`):

| Rows (VA) | What | Callers |
|---|---|---|
| `0x473708` | the driver load with `0x4958fd` (no DLL) and the init `0x473eac` | the sound sizes `0x463694` (own code) |
| `0x46254c` | the sound table: `st<letter>\sounds.str`, the opens, priorities, loop counts, volumes and preloads | the sound init `0x462410` (own code) |
| `0x464618`, `0x464038`, `0x464118`, `0x46466c`, `0x4646dc` | volume, play, stop, rate, pan with the original's EAX, EDX and ECX | the sfx hooks (`core/src/sfx.c`) |
| `0x462af0`, `0x462940`, `0x462b9c`, `0x46356c`, `0x4632c4` | is playing, the voice, the voice start, the lip value, the per-frame lip pass | the cockpit radio |
| `0x46313c`, `0x464874`, `0x462398`, `0x464260` | CD start, CD stop, the window handler, the user service | the mission load, `0x45f4cf`, the window procedure, the UI loop |
| `0x46317c`, `0x463238`, `0x462d78`, `0x462ddc`, `0x462e58`, `0x462efc`, `0x462fe0`, `0x463014`, `0x462734`, `0x4328cc` | pause, resume, the four prefs sound groups, the option screen's level get/set, the sound close, the cockpit sound step of the load | the pause, the prefs callbacks, the option screen, the abort notify `0x461304`, the mission load |

Own functions behind them (no rows: only audio code calls them): open
`0x463a14`, close `0x463c50`, cache `0x463d4c`, done `0x4641e8`, loops
`0x46474c`, priority `0x4647b8`, eviction `0x464390`, stop all
`0x4648e0`, the glue `0x473828`, `0x473898`, `0x4739ac`, `0x473a5c`,
`0x473aec`, `0x473b54`, `0x473b80`, the sample load `0x474360`/`0x474508`,
the VOL stream constructor `0x472e6c`, the error report `0x4649b8`, the
path helpers `0x45ef1c`/`0x46233c`, the LRU channel `0x462b30`, the lip
file `0x463374`, CD audio `0x473c00`, `0x473c48`, `0x473d44`, `0x473d84`,
`0x473dcc`, the user service `0x4737d8`/`0x474aa0` (WM_TIMER branch), the
driver shutdown `0x4737a4`/`0x4741fc`. `es2_sim_audio_internal` reaches
the main ones by VA for the in-vitro cases.

The rows refuse by name the paths DBSIM never takes: the MIDI driver halves
of `0x473eac`/`0x4741fc`, every MIDI and stream slot path of the glue, the
Win32s branch of the memory stubs, `0x474aa0`'s messages other than
WM_TIMER, a pause over a row without a valid slot (the original reads
through NULL), an LRU channel search that finds none, and the cache load's
`CloseHandle` of an uninitialised local ("Facts read on the way").

### The device seam

`es2_sim_audio_device` takes every SOS call DBSIM makes through its stubs
`0x495a43..0x496300` (20 entry points, `es2_sim_sos_call`, the stubs'
arguments with words zero-extended) and every `mciSendCommandA`. The own
code calls the device where the original's stub would call the DLL's slot.
Headless it is `es2_sim_audio_silent`; the window binds the mixing device
over an SDL3 sink and the MCI host ("Sound and CD music in the window").
The contract of the entry points, from the DLL's bytes:

- `sosDIGIDetectFindFirst(caps)` fills the capability record from the wave
  device's `WAVEOUTCAPS` (`0x404589`): +2/+4/+6 manufacturer, product and
  version words, +8 the name, +48h the formats, +4Ch ORed with the format
  classes (10h 11 kHz, 20h 22 kHz, 40h 44 kHz, 1 8 bits, 2 16 bits, 4 mono,
  8 stereo), +4Eh 0, +50h the support flags, +54h the device index. DBSIM
  reads only +4Ch (`0x474022`).
- `sosDIGIInitDriver(caps, &handle)` writes the driver handle word (the
  first free driver index, 0 for the first driver; `0x4016e5..0x4016ec`);
  `sosDIGIStartSample(driver, sample, &handle)` writes the sample handle
  word into slot +24h and answers 0 (17h when no sample slot is free).
- `sosPrepare32Memory`/`sosUnPrepare32Memory` are not called on Win32: the
  stubs `0x496271`/`0x4962a2` store the pointer and answer 0 themselves
  when the word `0x6cbd5c` is set.

### Headless decisions

- **The driver initialises, as on the recording machine.** The goldens
  have no sound-state field, but the oracle ran with a context
  (`0x462784` and the driver calls are live in every suite, "Sound data"),
  and stream B depends on it. The silent device reports a wave device with
  every PCM format (`dwFormats` FFFh, classes 7Fh); with the CD's
  `sound.cfg` (`Driver = MME`, 22 kHz, stereo) the requested format 29h is
  taken. The details of the recording machine's device (name, ids) are not
  observable and not modelled.
- **No DLL.** `0x4958fd` sets the use count and the Win32 word as the
  original (GetVersion answers an NT value), the module handle `0x6cbd58`
  holds the stand-in `0x50500000`, and the 97 entry slots
  `0x6cbd60..0x6cbee0` stay 0 (the original holds the DLL's addresses
  there: a known after-load snapshot difference, as are the module handle
  and the sample blocks' addresses).
- **Registers after a driver call** are the DLL's in the original and
  unknown; the own code keeps EDX and ECX as they were at the stub's call
  (the in-vitro thunks do the same). Open: whether any caller reads them.
- **Playing time.** `sosDIGISampleDone` answers from the virtual clock (the
  sample's bytes, rate, width, channels and repeat count against the time
  since its start), read without a clock call. In the recording it was real
  playback time (item 4 of "What can change the oracle's comparison"): it
  changes only latches and restarts outside the compared fields.
- **CD audio**: MCI_OPEN of "cdaudio" answers 306 (no device), so
  `0x473c48` returns 0 and `[0x4a0e2c]` stays -1; every other MCI command
  answers 257. No journal holds MM_MCINOTIFY or WM_TIMER (checked over the
  40 suite journals), so the window handler's two branches never run in a
  replay.
- **Sample memory** (`GlobalAlloc(40h)`, GMEM_FIXED) is host memory,
  outside the game heap as in the original; `SetLastError` is recorded in
  `es2_sim_audio_platform`, not passed to the system.

### Facts read on the way

Confirmed from the bytes.

- **The table init's open mode is stack residue.** `0x46254c` tests
  `_strstr(L, ".hmp")` and `_strstr(L, ".wav")` on its own 20-byte local L,
  which it never writes (`0x4625b6..0x4625e6`); the mode is otherwise the EDI
  it was entered with, which `0x462410` left as the channel array
  `[0x4d2d6c]` (`0x46246a`). So every row opens with that pointer as the
  mode: digital, no MIDI (mode 0) or stream (mode 2) flag. Modelled with L
  zeroed. **The zero model is neutral (C,
  [own_process.md](../reference-game/own_process.md), the residue table).**
  In the original, L's bytes 0..15 are bytes 12..27 of `0x46379c`'s profile
  buffer `[ebp-28h]`; the short sound.cfg values do not reach them. L's
  bytes 16..19 hold the EBP that `0x463694` saved. Both are callees of
  `0x462410` before the call. A found `".wav"` (mode 1) opens as the pointer
  does. Only `".hmp"` would change the open, and DBSIM's only `".hmp"`
  string is the constant it searches for.
- **The clock 0x467828 returns game units**, (GetTickCount - `[0x4d3b78]`)
  >> 4: the voice start times (+37h) and the lip event times are in these
  units.
- **The eviction's score** adds 1000 for a playing slot (byte S+5 bit 0 =
  flag 100h, `0x464465`) and 10000 for a playing slot with a loop count; its
  unload passes the MIDI flag of the scoring loop's last slot (`[EBP-0Ch]`,
  never written when no slot is used: residue, the original then faults) and
  the record count as the index (unused by the glue).
- **Stop all** (`0x4648e0`) walks the slots up to the record capacity X+10h
  (60), not the slot capacity (90): slots 60..89 are never stopped.
- **The cache load** calls `CloseHandle` on its uninitialised local
  `[EBP-54h]` when a sample file is empty (`0x463f2b`): refused by name.

### Verification

- Unit: `//sim:audio_test` (synthetic image: the driver load with the
  silent device and with format failures, the manager rows with their
  registers, done on the virtual clock, the lip pass, the voice start, the
  window handler, CD start without CD audio, the close).
- In vitro, original machine code against the own code
  (`//tools/reference-game/invitro:differential_audio_test`, `--audio`): the
  driver load and shutdown, 2,000 cases over random `sound.cfg` words,
  device formats and failure scripts (1,610 loads succeed, 174 with the
  fallback format 15h); the SFX manager, 4,000 sequences of 12 calls on
  random worlds (863 plays started, 2,921 stops, 2,032 opens, 1,616
  evictions); the timeline, 4,000 cases of lip passes, voice starts, voices
  and lip values with sfx_lip/sfx_lip_due/sfx_voice as the original's code
  on both sides (3,666 voice starts). Compared: EAX/EDX/ECX per call (EDX
  and ECX not where the original's come from the host: the voice start's
  head and string copy, an empty lip pass's clock), DATA with the DLL slots
  masked, the heap, the worlds, the device call logs, the GlobalFree calls,
  the last error. All identical. Not covered in vitro: the sample load from
  a VOL file and the table init (they need the VOL stream hooks over a file
  model); the replays run both.
- Caught mutation: the eviction's 10000 for a looping playing slot changed
  to 1000 is caught by the manager cases.
- `-s` (sound off) runs the same rows without a context.

### Open questions of the own audio

- The cockpit radio panel `0x44aff0`/`0x44b514`/`0x44b620` calls the voice
  rows; `0x41a6d0`/`0x41a994` call `0x462af0`. Their use of EDX/ECX after
  these rows is not checked.

## Audit of the audio rest

Every VA of the SFX open/close/load/done path `0x463a14..0x4641e8`, the lip
read `0x463374` and the SOS glue `0x4737a4..0x474508` with the import stubs
`0x4958fd..0x4962a2` has a verdict against the own code (`sim/src/audio.c`).
[sim_gap.md](../reference-game/sim_gap.md) lists the AUD area with no called
root and no behind root; the audit confirms that **no further service row is
needed**.

### Method

- Call sites: the listing of a verified copy of `VER95/DBSIM.EXE`, every
  `call` and `jmp` to each VA.
- Pointers: a byte search of the whole image for each VA as a little-endian
  dword (tables, vtables, `push imm32`, `mov reg, imm32`). "Dead" means no
  call or jump and no dword reference anywhere in the image.
- A caller is "own" when it is an audio or load service row
  (`sim/dispatch/services/audio.h`, `mission_load.h`) or own code without a row
  that is reached only from rows.

Verdicts: **row** (an existing row, reused), **own** (own code without a row,
named function in `audio.c`), **inlined** (part of an own function or row),
**dead**, **refused** (only the MIDI or stream path reaches it; the own code
refuses these paths by name).

### SFX path and lip read

| VA | Verdict | Evidence (call sites) |
|---|---|---|
| `0x463a14` open | own (`sfx_open`) | `0x46261a` (table init, row `0x46254c`), `0x462cc6` (voice start, row `0x462b9c`), `0x4630fb` (in the dead `0x4630c0`) |
| `0x463c50` close | own (`sfx_close`) | `0x462b86` (LRU `0x462b30`, own), `0x46437c` (in the dead `0x464280`) |
| `0x463d4c` cache | own (`sfx_cache`) | `0x462698` (table init), `0x462724` (in the dead `0x462708`), `0x462ce3` (voice start), `0x463d10` (close), `0x4640cb` (play) |
| `0x463f2f` | inlined | a label inside the cache (closing an empty file), not a function start |
| `0x464038` play | row | |
| `0x464118` stop | row | |
| `0x4641e8` done | own (`sfx_done`) | `0x462b15` (row `0x462af0`), `0x463485` (in the dead `0x463464`) |
| `0x463374` lip read | own (`lip_read`, inlined in `voice_start`) | `0x462d1d` (voice start row), `0x463360` (in the dead `0x463350`) |
| `0x46333c` | inlined in the row `voice_channel` `0x463624` | `0x46362c` |
| `0x4638f0` | inlined in `sfx_close` (the record's destructor) | |
| `0x463948`, `0x46395c` | inlined in the pause/resume rows | `0x46322c`, `0x463245` |
| `0x4639e8` | inlined in the window handler row `0x462398` | `0x4623ca` |
| `0x464858`, `0x464888`, `0x464988`, `0x46499c`, `0x473c38` | inlined (`mov eax, imm32` / test / call wrappers inside own rows; `0x464888` with `0x473c38` is the sound close) | |
| `0x464874` | row | |

Dead functions in this range (no call, no dword reference): `0x463350` (a
three-argument wrapper of the lip read that returns the channel), `0x463464`
(voice playing; the function index merges it with `0x463400`, which is why
it shows `0x4641e8` as a callee of `0x463400`), `0x462708` (row cache
wrapper), `0x4630c0` (reopen of rows 0..9), `0x464280` (reclaim of the
lowest-score slot), `0x463974` and its only callee `0x47380c` (stores
`0x78`, `0x20`, `1`), `0x46399c` (cache limit set, calls the eviction).

### SOS glue `0x4737a4..0x474508`

| VA | Verdict |
|---|---|
| `0x4737a4` driver shutdown, `0x4741fc` its digital half (`0x4741fc..0x474250`) | own (`driver_shutdown`); a failed uninit returns 1 and `0x4737a4` still frees the DLL (`0x495a1f`), as the own code does |
| `0x4737b0` acquire, `0x4737c4` release (`0x1000`) | own |
| `0x4737d8` user service | own (`user_service`) |
| `0x473828`, `0x473898`, `0x4739ac`, `0x473a5c`, `0x473aec`, `0x473b54`, `0x473b80` | own (`glue_*`: open, start, stop, done, volume, rate, pan) |
| `0x473ba8` | row (`empty_473ba8`) |
| `0x473c00`, `0x473c48`, `0x473d44`, `0x473d84`, `0x473dcc` | own (CD audio) |
| `0x473eac` driver init, digital half | own (`driver_init`) |
| `0x474324` status read | own (in `glue_start`) |
| `0x47432c` | own (in the WM_TIMER branch of `0x474aa0`) |
| `0x474350` | own (`driver()`, the word `0x6b566e`) |
| `0x474360` sample load, `0x474508` unload | own (`sample_load`, `sample_unload`) |
| `0x474304` (error-string wrapper, site `0x474311`), `0x474358` (MIDI handle word) | dead |
| labels `0x47383c`/`0x47465c`, `0x473864`/`0x474708`, `0x4738a9`, `0x4738f9`/`0x47473c`, `0x4739ba`, `0x4739e0`/`0x4749d0`, `0x473a69`, `0x473afb`; the MIDI init halves `0x473f08`, `0x4740fb`; the MIDI shutdown `0x47425e..0x4742fe` | refused (MIDI or stream branches) |
| `0x47453c`, `0x474628`, `0x47498c`; `0x474aa0` apart from WM_TIMER | refused (MIDI and stream) |

### Import stubs `0x4958fd..0x4962a2`

- `0x4958fd` (DLL load, site `0x47371f`) and `0x495a1f` (DLL free, sites
  `0x47378f`, `0x4737a9`): own (`dll_load`, `dll_free`).
- The device calls own code reaches (`es2_sim_sos_call`), with their digital
  call sites: `0x495a43` at `0x473fd2`; `0x495a59` at `0x47424b`; `0x495a65`
  at `0x4740a0`; `0x495a79` at `0x474232`; `0x495a8c` at `0x473fd9`
  (`0x474107` is MIDI); `0x495a9f` at `0x474246` (`0x4742f4` is MIDI);
  `0x495b2b` at `0x4737e8`, `0x474aee`; `0x495b7c` at `0x4740d1`,
  `0x474225`; `0x495ba3` at `0x473f80`; `0x495bb9` at `0x473fa7`,
  `0x473fb6`; `0x495bc5` at `0x473f99`; `0x495be7` at `0x4737b6`; `0x495bfa`
  at `0x4737cf`; `0x495d1f` at `0x473b47`; `0x495d9f` at `0x473b9d`;
  `0x495dd4` at `0x473b77`; `0x495e64` at `0x473a81`; `0x495e7c` at
  `0x473985`; `0x495e95` at `0x4739f8`; `0x495ead` at `0x474216`. All are
  device calls through the seam.
- `0x496271` (prepare, Win32 branch, site `0x4744b7`) and `0x4962a2`
  (unprepare, site `0x474517`): own (in `sample_load`, `sample_unload`); the
  DLL branches `0x49628e`, `0x4962b5` are refused by name.
- Reached only from MIDI or stream paths, refused: `0x495c12`, `0x495c43`,
  `0x495c5b`, `0x495c76`, `0x495c91`, `0x495cac`, `0x495cc4`, `0x495d52`,
  `0x495fa2`, `0x495fba`, `0x495fcd`, `0x495fe0`, `0x495ff3`, `0x495fff`,
  `0x49602b`, `0x49603f`, `0x4960d7`, `0x4960ea`, `0x49612c`, `0x496142`,
  `0x49614e`, `0x496174`, `0x496187`, `0x49619a`, `0x4961b0`, `0x4961d6`,
  `0x4961e2`. `0x495b92` is reached only from the dead `0x474304`.
- The remaining stubs of the range (e.g. `0x495aab`, `0x495ac4`, `0x495adc`,
  `0x495aef`) have no call site: dead (names the DLL load resolves but no
  code calls).

### Result of the audit

No row added. Every caller of the range is an audio or load row, own code
without a row, or one of the dead functions above.

## The mixing device

A second device behind the seam `es2_sim_audio_device`: SOS9503.DLL's
software mixer for the MME (waveOut) driver, rebuilt from static readings
of the CD's DLL (the addresses in this section are the DLL's). Code:
`sim/src/audio.c` (end of the file, `es2_sim_sos_mixer_*`, `es2_sim_sos_mix`,
`es2_sim_sos_pump`) in `:services`; the SDL3 sink `sim/src/audio_sdl.c` in
`//sim:audio_sdl`. Headless es2sim binds the silent device, and `:services`
does not link SDL. The window binds `es2_sim_sos_mixer_device` with
`es2_sim_audio_sdl_sink` ("Sound and CD music in the window").

### What DBSIM hands the driver

From the DBSIM listing and the own rows:

- The caps record `0x6b5590`:
  - `+4Eh` is the format: `29h`, or `15h` as the fallback.
  - `+6Ch` is the buffer count and `+6Eh` the block word (`0x200` from
    `sound.cfg`).
  - `+70h` = 1 (the service thread on).
- The sample structure from the sample load `0x474360` (zeroed first):
  - `+0` data, `+10h` length in bytes.
  - `+30h` = `(v << 16) | v`, `+34h` = loops − 1 or −1, `+38h` rate,
    `+3Ch` bits, `+40h` channels.
  - `+44h` = `8000h` for 8-bit data (unsigned), else 0.
  - `+48h` pan (`S+18h`).
  - `+5Ch`, `+60h`, `+64h` (= `S+0Ch`, always 0) and `+68h` are 0, so
    DBSIM installs no callbacks of its own; `sosDIGIStartSample` then
    installs its default end and remove callbacks
    ([main_loop.md "Where the `0xa000` messages come from"](main_loop.md#where-the-0xa000-messages-come-from-static-sos9503dll)).

### Format decode

`sosDIGIInitDriver` `0x401596`, decode `0x403547..0x403778`:

- Starting value: block = caps `+6Eh`.
- `+4Eh & 70h`:
  - `10h`: 11025 Hz.
  - `20h`: 22050 Hz, block × 2.
  - `40h`: 44100 Hz, block × 4.
- `& 3`:
  - 1: 8-bit; the output is unsigned (driver `+10h` = `8000h`).
  - 2: 16-bit, block × 2.
- `& 0Ch`:
  - 4: mono.
  - 8: stereo, block × 2.
- The block is a word. For `29h` with `0x200` that gives 22050 Hz, stereo,
  8-bit, `0x800` bytes per buffer.
- waveOutOpen failure: `MMRESULT | 320000h`. A format with no rate, width or
  channel bits answers `320020h` (waveOut's BADFORMAT); DBSIM never asks for
  one.

### The mix

`0x406b90..0x406ecd`; routine table `0x40730c`, output table `0x40924c`; the
exact addresses are in the code comments.

- **Setup.** The accumulators hold one int32 per output sample of the block
  and are zeroed; `+4Ch` (the listed count) = 0.
- **Each listed voice** adds 1 to `+4Ch`.
  - A voice with `4000h` (ended) gets one notice (`800h`; callback
    `0x403a71`). The notice sets `+70h` = the driver's written-buffer count
    `+992h`, and the voice is not mixed.
  - The list stops once `+4Ch` ≥ 32.
- **Routine flags:**

  | Flag | Set when |
  |---|---|
  | 1 | the data is unsigned (`+44h & 8000h`) |
  | 2 | the data is 16-bit |
  | 4 | the output is stereo |
  | 8 | the data is stereo |
  | 10h | the voice rate ≠ the output rate |
  | 20h | gain L < `7FF0h` or gain R < `7FF0h` |

- **Gains.**
  - Pan `8000h`: L = volume & `7FFFh`, R = (volume >> 16) & `7FFFh`.
  - Otherwise both factors start at `7FFFh`. If pan & `8000h`, factor L =
    `FFFFh` − pan; otherwise factor R = pan.
  - Each gain is the doubled high word of the signed 16 × 16 product of the
    volume's low word and the factor.
  - Pan `4000h` with volume `7FFFh` gives L `7FFEh`, R `3FFEh`.
- **Resampling.**
  - q = (rate << 32) / (output rate << 16): `0x406cf0` is `div ecx` with
    EDX = the voice's rate dword `+38h`, EAX = 0 and ECX = the driver's
    rate `+4` << 16, only when the two rates differ.
  - The DIV faults (`0xC0000095`) when the rate is at least output rate ×
    10000h (722,534,400 at 11,025 Hz, 1,445,068,800 at 22,050 Hz); ECX is
    never 0, as the driver opens only with 11,025, 22,050 or 44,100 Hz.
    No unmodified input gets there: the sample load writes `+38h` from the
    WAV header's rate dword (`[Q+18h]`) or 2B11h (11,025) for a raw
    sample, `SET_RATE` stores a word, and every RIFF WAVE header of the
    verified CD (1566, in the `.VOL` archives and as files) says 22,050
    or 11,025 Hz (a scan of `build/extracted/cd`). A modified WAV header
    reaches it; es2sim then ends the process with the original's code at
    `0x00406cf0` through `es2_sim_loop_divide_fault` (own_process.md "The
    original's fault paths"). In the original the mix may run on the DLL's
    service thread; the process ends with the same code either way.
  - The fraction (q << 16) is added to ECX per frame, with ECX =
    `80000000h` at the start of every run.
  - On a carry, the position advances by (q >> 16) + 1 frames; otherwise by
    q >> 16 frames (in bytes: × 2 for 16-bit data, × 2 for stereo data).
  - The phase is not kept across runs or buffers. A loop restart and every
    new buffer begin at `80000000h` again.
- **Fetch.**
  - 8-bit: the byte goes into AH and AL keeps its value; a stereo source
    puts its second byte into BH, with BL staying 0. 16-bit fetches whole
    words.
  - Unsigned data is XORed with `8000h`.
- **Scaling.**
  - The add is (int16)(high product word << 1).
  - AX keeps the low product word, but for 8-bit data its low byte is
    always 0, so the AL carry has no audible effect. It is still modelled.
  - A mono source to stereo output scales the same word with L and R.
  - Mono output gets L only, and a stereo source's right word is dropped.
  - Voices are added unscaled (routine `0x4077b1`).
- **End of the data** (`0x406d9f..0x406df5`):
  - Loops = 0: the voice gets `4000h` and `+0` = 0 (the default end
    callback `0x404140`), and it stops.
  - Otherwise loops is decremented unless it is −1, and the position goes
    back to the data within the same buffer.
  - After every run the voice's `+5Ch` grows by the full accumulator index
    (from the buffer start, not from the run start).
- **Output** (routine `0x4098fa` and its table).
  - The accumulators are clipped to [−8000h, 7FFFh] only when `+4Ch` > 1.
    The count includes ended voices still in the list.
  - 8-bit output: bits 8..15 of the accumulator, XORed with `8000h` first
    when unsigned (truncation, no rounding).
  - 16-bit output: the low word, little-endian.

### The buffers

Driver fields beyond the above: `+87Eh` outstanding buffers, `+882h` the
queue of returned headers (64 entries, write index `+982h`, read index
`+984h`), `+87Ch` and `+880h` two further refill gates, `+992h` written
buffers, `+996h` returned buffers. WINMM imports: `0x41f118` waveOutWrite,
`0x41f134` waveOutUnprepareHeader, `0x41f138` waveOutReset, `0x41f13c`
waveOutPause, `0x41f140` waveOutRestart, `0x41f144` waveOutPrepareHeader,
`0x41f148` waveOutOpen.

- **Priming** (`0x402ca4..0x402e40`), called by StartSample (`0x406188`)
  and AcquireDriver (`0x403eb4`, only when `+4Ch` ≠ 0):
  - the service thread is stopped (`0x4039f9`), then waveOutReset and
    waveOutPause (a failure answers `MMRESULT | 220000h`);
  - both queue indices are set to 0;
  - for every buffer of the ring (count `+62h`): mix through the format's
    mix routine, waveOutWrite (failure: the thread is resumed and the call
    answers `| 250000h`), `+992h`++, `+87Eh`++;
  - waveOutRestart, the thread resumed (`0x403a43`), answer 0.
- **StartSample's condition** (`0x405dc0`; `0x40614a..0x406188`): the driver
  word's bit 0 set, `+4Ch` read before the start (`0x405e46`, through
  SamplesPlaying `0x405c95`) 0, and `+87Eh` 0. Then `+87Ch` = 0 and
  `+4Ch`++ (`0x40618d..0x40619c`), so one voice leaves 2. A start while any
  buffer is still outstanding does not prime; it waits for the refill.
- **WOM_DONE** (message `3BDh`, `0x402fa1..0x403123`): `+87Eh`−−,
  `+996h`++, the header stored in the queue (`+982h`++, wrapping at 40h).
  The mode word `+A4Ch` (`[0x4180b8]` for driver 0) is copied from the
  caps' `+70h` at init; DBSIM writes 1. With its bit 0 the thread is
  resumed (`call [0x41f0d0]` on the handle `+986h`, `0x403055`); with
  bit 1 (value 2) instead the user service runs directly (`0x40306f`).
  Then every voice with `+70h` = `+996h` gets `+24h` = `2000h`, is unlinked
  and its remove callback is called (`call [voice+64h]`, `0x403101`); `+4Ch`
  is not decremented.
- **Refill** (user service `0x40316b..0x4033e2`; the thread `0x403809` has
  the same gate at `0x40385b..0x40388d`): not initialised answers
  `50000h | driver`; `[0x4180b8]` bit 0 (`0x403205`, so in DBSIM's thread
  mode the user service never refills) or `+98Eh` ≠ 0 answer 0; with
  `+4Ch` = 0 and `+880h` = 0 the call ends when `+87Ch` is 0 (otherwise
  `+87Ch`++ and it goes on). Then, while the queue is not empty: take a
  header, mix (`call [+56h]`), waveOutWrite (failure `| 280000h`),
  `+992h`++, `+87Eh`++. `+4Ch` is checked only on entry, so the loop can
  write a buffer that the mix found empty.
- `+87Ch` is written only by the increment-when-nonzero sites and by
  StartSample's zeroing, so in DBSIM it stays 0 and the gate is `+4Ch`
  alone. `+4Ch` is the listed count as the last mix left it.
- **Voice end** (`0x406d9f..0x406df5`): at the end of the data, loops
  −1 starts again and a nonzero count is decremented. At 0, `+24h` |=
  `4000h` and the end callback (`+60h`) runs if set (`[esi]` is cleared
  first; if the callback set it again, `4000h` is cleared). The notice
  (`0x406dfa..0x406e21`): unless `800h` is set, `call [+6Ch]` and set
  `800h`. That callback (`0x403a71`) stores `+70h` = driver 0's `+992h`,
  the buffer number whose return removes the voice.
- **Release** (`0x403bbc`): first spins until no buffer is outstanding
  (`+87Eh`, `0x403c73..0x403c9b`) while the service thread keeps mixing, so
  stopped voices are noticed and removed; then waveOutReset (every buffer
  comes back) and close.
- **Acquire** (`0x403d7e`): open, then prime when `+4Ch` ≠ 0.
- **The calls:**
  - StopSample (`0x4061ad`) and StopAll (`0x40623c`) only set `4000h`.
  - SampleDone (`0x405cd0`) answers whether `8000h` is clear. It turns
    nonzero only once the buffer that holds the voice's last frame has come
    back.
  - SetVolume (`0x40556b`) answers the old low word, or `18h` for an unused
    voice. SetPan (`0x405850`) and SetRate (`0x405996`) answer the old word,
    or handle | `180000h`.
  - A start with every voice in use answers `17h`. Uninitialised answers
    `50000h | driver`; a NULL sample answers `80000h`.

### The ring runs dry as waveOut's does

In the original the ring drains like this:

1. The mix that finds no voice listed leaves `+4Ch` 0.
2. The refill still writes that buffer (its gate is checked on entry
   only) and then stops.
3. waveOut plays every outstanding buffer to its end and returns it, so
   `+87Eh` reaches 0.
4. The next StartSample primes the whole ring again.

The own mixer follows the same gates: `sos_refill` checks `+4Ch` on entry
only; `sos_start` primes only with nothing outstanding; a buffer counts as
returned when `es2_sim_sos_pump` finds the sink's queued bytes below that
buffer's share (rounded up to whole blocks).

With waveOut every buffer returns. An SDL 3.4.18 stream does not return the
last one by itself:

- `SDL_GetAudioStreamQueued` counts input bytes not yet converted. A
  stream that resamples (22,050 Hz input, a device at another rate)
  keeps its resampler's padding frames back until more input follows or
  the stream is flushed. The padding is `RESAMPLER_MAX_PADDING_FRAMES` =
  zero crossings (6 with SSE) + 1 = 7 frames (`SDL_audioresample.c`; in
  `SDL_audiocvt.c` an unflushed stream subtracts the padding from its
  output). For 8-bit stereo that is 14 bytes. Measured on a synthetic
  stream in `//sim:audio_sdl_test`: 14 bytes unflushed, 0 after
  `SDL_FlushAudioStream`, 0 without resampling.
- Without a flush the newest buffer stays outstanding for as long as no
  input follows, and with `+4Ch` 0 nothing follows: no buffer returns,
  nothing refills, a later start does not prime (one outstanding), and the
  voices then started are never mixed, never end, are never removed and
  post no `0xa000` notice, until all 32 are in use
  ([main_loop.md "Trap: the SOS ring on the SDL sink"](main_loop.md#trap-the-sos-ring-on-the-sdl-sink)).

**The flush.** A sink may supply `flush` (`es2_sim_sos_sink`, last member;
NULL for waveOut-like sinks and the tests' fakes). `sos_write` calls it
after writing a buffer whose mix left `+4Ch` 0, which is the original's
last write before the ring drains. The SDL sink answers with
`SDL_FlushAudioStream`. `es2_sim_sos_mixer.flushes` counts the calls. The
return rule (rounded up) and all other timing are unchanged.

Windowed acceptance (SDL dummy drivers, `replay_suite.py --window
--message-trace`): `acceptance-60s`, `c1_01-success`,
`c1_01-options-dialog` and `c3_10-detail-options` are IDENTICAL. The
`es2sim: sos:` lines at the end:

| Suite | Voices listed | Written / done | Outstanding | Queued |
|---|---|---|---|---|
| c1_01-options-dialog | 0 | 131 / 131 | 0 | 0 |
| c1_01-success (uninitialised at the end) | 2 | 2792 / 2792 | 0 | 0 |
| acceptance-60s | 6 | 521 / 516 | 5 | 8192 |
| c3_10-detail-options | 1 | 600 / 595 | 5 | 9216 |

The last two rings are still playing a listed voice when the run ends;
nothing is stuck. Notices on the real devices:
[main_loop.md "Notices on the real devices"](main_loop.md#notices-on-the-real-devices).

### Forced deviations and guards

- **No waveOut threads.** Returned buffers are counted when the mixer is
  pumped, at the start of every device call: the sink's queued bytes, in
  whole blocks. In thread mode (caps `+70h` bit 0 or 1, `service` in
  `es2_sim_sos_mixer`) the refill runs then too, standing in for the
  service thread, and the user service answers 0 as the DLL's does
  (`0x403205`). DBSIM calls the user service from WM_TIMER, and every
  WM_TIMER call into the mixer pumps first, so the refill still happens at
  that call; the service thread's own timing is not reproduced.
- **Refill before removal.** On each returned buffer the own mixer refills
  first, then runs WOM_DONE's removals: the time-critical service thread
  (`0x402b19`) resumed at `0x403055` runs before the winmm callback goes on.
  In vitro (`//sim:sos_mixer_differential_test`) this order is bit-exact
  with the DLL. The other order (the callback finishes before the thread
  runs) differs:
  - one voice, two or more buffers: the same sound and notices; es2sim
    writes one silent buffer more per drain;
  - one buffer: the DLL never removes an ended voice (SampleDone stays 0,
    the ring keeps writing silence), so after 32 effects every start would
    answer `17h`;
  - several voices: a start right after a drain finds 1 buffer outstanding
    in the DLL and 2 in es2sim, so its voice begins one block (46 ms at
    22,050 Hz, block `200h`) later in es2sim; notices come in another
    order, handles and answers can differ.

  Which order the original gets is the scheduler's (a time-critical thread
  against winmm's callback thread, one CPU or several) and cannot be
  decided in vitro. Hypothesis: refill first. With the shipped
  `Buffers = 5` both orders sound alike but for that block of start
  latency after silence.
- **Release drains the ring.** As the original's release spins until no
  buffer is outstanding, the own release drains the ring before the reset
  (bounded; a voice still playing would make the original spin forever), so
  voices stopped by a pause are removed and do not stay listed
  (`//sim:sos_mixer_test` "the pause").
- **Flush after the last write** ("The ring runs dry as waveOut's does").
  Forced by the platform: an SDL 3.4.18 stream that resamples keeps its
  last 7 input frames until more input follows or it is flushed, and the
  SOS ring's last buffer before it drains has no input after it, so it
  would never count as played; waveOut plays and returns every buffer by
  itself and has no such call. What the mixer's callers see (buffers back,
  `+87Eh` reaching 0, the removals, the `0xa000` notices, the next start
  priming the whole ring) is the original's. The alternative, counting a
  buffer as played while its last 7 frames are still queued, would move
  the return earlier than the device takes it and would change the rule
  for every buffer. **Audible effect: none expected.** The flushed buffer
  was mixed with no voice listed, so every sample is the silence level
  (accumulators 0: unsigned 8-bit output `80h`, 16-bit 0); the flush plays its 7
  held frames (0.32 ms at 22,050 Hz) and the resampler pads with silence.
  SDL documents that input after a flush may start with a gap; it can fall
  only between that silent buffer and the next start's priming, so silence
  meets silence. Measured in vitro: in `//sim:sos_mixer_differential_test`
  es2sim flushed after 3549 buffers, all of them silent, and in refill
  first the DLL's last buffer before a dry ring was silent in all 292
  cases. Not checked by listening.
- **PostMessage notices: windowed only.** The default end and remove
  callbacks post `0xa000`-range messages; DBSIM's window procedure ignores
  them (`0x474af5..0x474b09`), but the pump's retrievals count. The
  windowed mixer posts them through `es2_sim_loop_post`; headless posts
  none (verdict and measurement: [main_loop.md "Why the headless replay does not post them"](main_loop.md#why-the-headless-replay-does-not-post-them)).
- **CD audio.** MCI and detection go to the silent device's answers
  headless, to winmm in the window.
- **SDL latency.** A buffer counts as played once SDL has taken it from the
  stream, not once it is heard. SDL's device buffer adds latency after that.
- **Not modelled:**
  - flag `40h` (`+44h & 4000h`), because the sample load never sets it;
  - guest callbacks, because DBSIM passes none.
- **Guards** (where the original would fault or loop):
  - an endless voice with empty data stops after the second empty run;
  - more than 64 buffers is clamped;
  - a block of 0 is not refilled;
  - a resample quotient beyond 32 bits ends the process as the original's
    DIV fault does; only without a process record (the library default,
    unit tests) is it saturated;
  - a handle ≥ 32 is treated as an unused voice.

### Tests of the mixer

- `//sim:sos_mixer_test`: synthetic voices over a recording sink, with
  hand-computed bytes:
  - decode, open failure;
  - unscaled, scaled, pan law, resampling, loop restart and `+5Ch`, phase
    reset per buffer;
  - the resample DIV: a rate of output rate × 10000h ends the run with
    `0xC0000095` at `0x00406cf0` through a recording process record and
    nothing more is mixed or written; one below does not fault; without a
    record the quotient saturates;
  - clipping with two voices;
  - the SampleDone and StopSample lifecycle;
  - 32 voices and `17h`, the setters, the inactive answers;
  - release and acquire, 16-bit, the pause;
  - the dry ring: a fake sink holds back part of the last buffer until it
    is flushed (`hold`); a once voice ends (`4000h`) and is removed
    (`2000h`); the buffer with no voice listed is written and flushed once,
    and both remaining buffers come back; SampleDone answers 1, the next
    start primes the whole ring, and a refill with a voice listed does not
    flush.

  Caught mutations: the clip condition `> 1` → `> 2` (two `FFh` voices gave
  `7E` instead of `FF`); the flush gate disabled (`if (0 && …)`):
  `sos_mixer_test` fails (no flush), and `audio_sdl_test` fails with
  "1 outstanding, 14 queued".
- `//sim:sos_mixer_differential_test` (Windows x86): the mixer against
  SOS9503.DLL's DIGI driver. The DLL (SHA-256 checked) is mapped privately
  and relocated, with stubbed imports, a fake waveOut that records every
  written buffer and returns it when the test plays it, and the service
  thread `0x403809` run as a fiber. Both sides get DBSIM's driver load and
  the same calls; after every call the written buffers (bytes), the
  `0xa000` notices, EAX, the sample handles and the outstanding count are
  compared.
  - Cases: one voice over the sound.cfg codes `15h/19h/25h/29h` × buffers
    1, 2, 3, 5, 8, 16, 64 × five voice kinds (once, looped, endless,
    resampled, scaled and panned), the 16-bit and 44 kHz codes; a second
    StartSample on a playing sample; the pause (StopAll, release,
    acquire); a start while the ring drains; random runs of starts,
    stops, setters, user service calls, plays, releases and acquires
    (`ES2_SOS_DIFFERENTIAL_SEEDS`, default 400; `ES2_SOS_DIFFERENTIAL_TRACE`
    = seed prints the calls). Each runs in both thread orders.
  - Refill first: equal call by call. 400 seeds: 1304 scenarios, 211,465
    calls, 57,438 buffers and 14,023 notices equal; the `+68h` callback
    `0x40421c` called 0 times. 3000 seeds: 6318 scenarios, 355,598 buffers
    (781 MB) and 95,734 notices equal.
  - Removal first: counted, not failed, except the strict one-voice cases
    ("Refill before removal" above). 400 seeds: 605 runs; 120 strict runs
    equal; of the others 162 the same, 184 notices in another order only,
    10 the same sound with other notices or answers, 109 other sounding
    buffers.
  - Caught mutation: the user service refilling in thread mode (the
    `0x403205` gate removed) fails 46 refill-first scenarios, e.g. "op 9
    (user service): buffers written: SOS9503 7, es2sim 11".
  - Logs: `run.log` (400 seeds), `run-3000-seeds.log`,
    `mutation-user-service-refills.log` in es2-data
    `logs\sos-mixer-invitro`.
  - Not covered: the DirectSound device (2); the DLL's thread and
    callback timing beyond the two orders.
- `//sim:audio_sdl_test` runs on SDL's dummy audio driver, so no sound card
  is needed: the sink's open, write, queued and clear answers; the 14-byte
  tail on a synthetic stream; the sink alone keeping 14 bytes queued on the
  dummy device, still after 250 ms, until `flush`, then 0; the device with
  format `29h` refilling as the dummy device plays; a once voice running dry
  with 0 outstanding, 0 queued, 0 listed and one flush, the notices `4000h`
  then `2000h`, and the next start priming 4 buffers; release, acquire and
  uninit.

### Open questions of the mixer

- Which thread order the original runs ("Refill before removal"). A
  listening check on a clone of the original: with `Buffers = 1` in
  sound.cfg, sounds stop after about 32 effects if the original removes
  first; with refill first they go on.
- The DirectSound device (2) is not compared.
- Whether the gap after a flush is audible, and the sound against the
  original (a listening check).

## Sound and CD music in the window

`es2sim_window` without `--headless` puts a real audio device behind the
seam `es2_sim_audio_device`:

- **Effects:** the SOS calls go to the mixing device (`es2_sim_sos_mixer`)
  over the SDL3 sink. The rows decide rate, volume, pan, loops and format;
  the mixer only renders them. With the CD's `DATA/SOUND.CFG` (Rate 22,
  Width Stereo, Buffers 5) the driver load `0x473708` asks for format
  `29h`, so the sink opens the original's output format: 8-bit unsigned,
  22,050 Hz, stereo, five buffers of `800h` bytes.
- **CD music:** the `mciSendCommandA` calls of the CD glue
  `0x473c00..0x473ea8` (reached from `0x46313c`, `0x464858`, `0x464874`,
  `0x464888` and the window handler `0x462398`) go to winmm through
  `es2_sim_mci_send` (`sim/src/audio.c`). A present audio CD plays; with no
  drive or no disc, winmm's error goes back to the guest, which takes the
  original's failure path.
- **Headless:** `//sim:es2sim` and `es2sim_window --headless` keep the
  silent device (`MCI_OPEN` 306, everything else 257). Neither the timer
  nor the `0xa000` messages are touched, so every replay stays identical.

The code is in `sim/src/audio_sdl.c` (`es2_sim_audio_window_*`, declared in
`es2/sim/audio_sdl.h`), `sim/src/audio.c` (`es2_sim_mci_host`,
`es2_sim_mci_send`, declared in `es2/sim/audio.h`) and `sim/src/main.c`.
`:audio_sdl` links `-lwinmm`. In `main.c` the audio set-up adds, under
`#ifdef ES2SIM_WINDOW` and only when not headless, `es2_sim_audio_window_init`
and `es2_sim_audio_window_device` before `es2_sim_audio_headless`;
`es2_sim_audio_window_attach(&window_audio, &loop)` after
`es2_sim_window_init` (it wraps `loop->window`); and the audio line plus
`es2_sim_audio_window_close` before `es2_sim_window_close`. A layer that
replaces `loop->window` must do so before the attach, or attach again.

### The MCI contract

Read from the DBSIM listing. The IAT slot of `mciSendCommandA` is
`0x6cc4ec` (stub `0x496a5a`). Confirmed in vitro: the rows ran on a
synthetic image against a recording `mciSendCommandA`
(`//sim:audio_window_test`).

| VA | Command | Flags | Block (guest dwords) |
|---|---|---|---|
| `0x473c00` | `MCI_STOP` 808h, then `MCI_CLOSE` 804h | 0 | none (params 0), only when `[0x4a0e2c]` ≥ 0; then `[0x4a0e2c]` = -1 |
| `0x473c48`(track) | `MCI_OPEN` 803h | 2000h (TYPE) | `ebp-14h`: +8 = `0x4a0e34` "cdaudio"; +4 (wDeviceID) read back into `[0x4a0e2c]` |
| | `MCI_SET` 80Dh | 400h (TIME_FORMAT) | `ebp-20h`: +4 = 10 (TMSF) |
| `0x473d84`(track) | `MCI_STATUS` 814h | 112h (ITEM, TRACK) | +8 = 1 (length), +0Ch = track; +4 (dwReturn) is the answer, stored in `[0x6b558c]`; 0 → stop, EAX 0 |
| | `MCI_PLAY` 806h | 0Dh (NOTIFY, FROM, TO) | `ebp-2Ch`: +0 = `[0x6b5588]` (the window), +4 = track, +8 = track \| length's low three bytes << 8 |
| `0x473d44` | `MCI_STATUS` | 102h (ITEM) | +8 = 2 (position) |
| `0x473dcc`(position) | as `0x473c48` without the length, "cdaudio" at `0x4a0e3c`, FROM = the position | | |

Any failure in `0x473c48` and `0x473dcc` runs the stop `0x473c00`. EAX is
1 only when the last command answered 0.

The host translation:

- Each guest field is read as a dword into a host block of the same field
  order. The field count is known per command: OPEN 5, STATUS 4, PLAY and
  SET 3, STOP and CLOSE 1.
- On `MCI_OPEN`, the TYPE, ELEMENT and ALIAS strings (not their `_ID`
  forms) are copied into host buffers of at most 128 characters. An
  unterminated string answers 263 and is not sent.
- With `MCI_NOTIFY` and a nonzero callback, the callback becomes the host's
  notify window. The guest's own value (`0x00010001`, its window stand-in)
  is recorded.
- After the call, every field the host did not replace and that winmm
  changed (wDeviceID, dwReturn) is written back. Pointers and the callback
  are not.
- Params 0 is sent as NULL. Any other command with a block answers 274 and
  is not sent; DBSIM sends none here.

The AVI player's MCI calls `0x471835..0x4719e7` ("avivideo" at `0x4a0cc4`)
are a different device and are not handled; es2sim plays no AVI.

### MM_MCINOTIFY

The layer owns a message-only window (`HWND_MESSAGE`, class
`es2sim.mci_notify`) on the simulation thread, and `MCI_PLAY`'s notify goes
to it. Its window procedure queues `MM_MCINOTIFY` (3B9h, wParam the code,
lParam the device). The queue holds 16; any further notify is counted as
dropped. The loop's peek hands the queued notifies to the guest as posted
messages for the main window, ahead of the window layer's input.
`es2_sim_window_proc` then runs the handler list `0x4d3b30` to `0x462398`:
with wParam 1 (SUCCESSFUL), a context and the loop byte `0x49f900` it stops
the CD and plays track `[0x49f8fc]` again (`0x464874`, then `0x464858`). Any
other code is ignored, as in the original.

### Deviations in the window

Platform-forced:

- In the original, winmm's waveOut thread takes the buffers back. Here the
  mixer is pumped on the simulation thread instead: at every peek, at the
  pause path's `Sleep(100)` and around the limiter's pace. Long loads with
  no peek can let the five buffers (about 116 ms) run dry, so a short
  dropout can be heard where the original kept playing.
- The notify goes to the layer's window, not the guest's. The guest still
  sees the message on its main window, but it arrives at the next peek and
  not from `DispatchMessageA` of the original pump. Its order relative to
  input of the same peek is "notify first".
- The collect's filtered `PeekMessageA` (the notify window, `MM_MCINOTIFY`
  only) runs only while `GetQueueStatus(QS_POSTMESSAGE)` reports a posted
  message. Called every frame without that check, it lets Windows flag the
  window as not responding about every 6 s ("Focus, pause and resume in the
  window"). The notifies the guest sees are the same.
- SDL3 resamples to the device format. The latency is SDL's stream buffer
  on top of the mixer's five buffers.
- An MCI device the guest leaves open at exit stays as the original leaves
  it, to winmm's own process-exit handling.

### Focus, pause and resume in the window

Facts from trace builds on a clone with SDL's disk audio driver; evidence
`es2-data/logs/o33-20261022/` (`audio-trace-repro5.log`,
`audio-envelope-repro5.txt`, `hang-*`, `replay-*`, `summary.md`).

- **Clipping at mission start.** Standing still after the load, three
  samples start, all 8-bit mono at 22,050 Hz: v0 (110,732 bytes, volume
  `71EAh`), v1 (2,940 bytes, `651Dh`) and v2 (68,300 bytes, full volume
  `7FFFh`), centred. The output clips from 4.6 s to 7.1 s while v0 and v2
  overlap; the mixer adds the voices unscaled and clips in the output
  routine, as in `SOS9503.DLL`'s listing ("The mix"). No buffer underruns.
  **Hypothesis:** the clipping is the original's; not confirmed while the
  mixer is not compared with the DLL in vitro.
- **The hang flag.** With the collect's per-frame filtered `PeekMessageA`,
  `IsHungAppWindow` turned true about every 5.5 to 6 s; Windows put its
  ghost window in front, which took the focus, so the game paused and
  resumed every ~6 s, and during a long focus loss the ghost kept the focus
  and the game never resumed (event log: AppHang 1002 for es2sim). A minimal
  SDL3 program (`hang-hungtest.c`) reproduces the flag with a per-frame
  filtered `PeekMessageA` (window or message range); unfiltered, or gated
  by `GetQueueStatus`, it does not. With the gate, a run on a clone showed
  no flag in 32 s of play and 40 s of focus loss.
- **A looping row restarts after a focus loss: original behaviour.** Every
  pause stops all samples (`0x4648e0`), every resume starts each row again
  whose slot loops endlessly (`S+14h` 0) and was playing (`S+4` bit 100h),
  from the start (`0x4631db..0x463217`, `0x463259..0x463283`).
- **A resume without a pause** is not the original's. A window started
  behind another window gets its first focus only when clicked, after the
  sound's set-up. That first `WM_SETFOCUS` would run the resume `0x463238`
  with no pause having written the rows' bytes `p[7]`, which are nonzero
  ("Sound data"): the resume starts every row (`replay-before-fix-trace.log`:
  all 53 rows, the endless rows 0..9 among them), the voices run out (45
  starts answered 17h) and the sound falls silent. The original's
  `ShowWindow` gives the focus before the set-up, and pause and resume then
  alternate. The window layer does the same
  ([own_process.md](../reference-game/own_process.md), the focus row): the
  activation at the first peek, then only alternating focus messages
  (`replay-after-fix-trace.log`: no row started).
- **Voices stopped by a pause are removed.** The pause releases the driver
  with flag 1000h (`0x463226`); the release drains the ring first ("Forced
  deviations and guards").
- A pause longer than the clock's run time turns the EventPort's elapsed
  time negative (both ports add their paused time); `0x436f70` divides only
  the product's low dword and does not fault, in the original as in es2sim
  ([hud.md](hud.md) "The original's slips").

### Tests of the window audio

- `//sim:audio_window_test` uses a synthetic image, a recording sink and a
  fake `mciSendCommandA`, so no audio device and no CD drive are opened. It
  has 4 case groups:
  1. `es2_sim_mci_send` alone: the strings, the write-back, the callback
     substitution, NULL, 274, 263 and 306.
  2. The driver load: format 29h, sink 22050/2/8, five buffers primed, the
     peek and the sleep pumping.
  3. The CD start with a disc: the four commands with their flags and
     FROM/TO. Then a posted `MM_MCINOTIFY` is retrieved by the peek, both
     directly and through `DispatchMessageA`; the handler replays on
     SUCCESSFUL and ignores ABORTED; the stop row `0x464874` follows.
  4. The failures of OPEN, STATUS, SET and PLAY.

  Caught mutation: with the notify substitution disabled, the test fails at
  the `MCI_PLAY` callback check.
- `es2sim_window_test` (with `ES2_DATA_ROOT`) stops its windowed run at the
  launch peek `0x461524`, before the sound init, so it opens no device.

### Listening check

On a test clone with the CD in the drive (never `native-r111`), from its
installation directory:

`bazel-bin\sim\es2sim_window.exe --config <the clone's es2sim ini> --arguments "<launcher line>"`

Compared with DBSIM on the same clone:

- **Effects:** volume, left/right pan and pitch (rate) as in the original;
  the grain of 8-bit 22 kHz stereo.
- **CD music:** the mission track starts. When it ends it starts again
  (`MM_MCINOTIFY`). Pausing (switching to another window) stops it and
  saves the position (`0x46317c`, `0x473d44`); resuming plays on from that
  position (`0x463238`, `0x473dcc`). The effects stop on pause too, and the
  looping ones come back on resume.
- **Without a disc:** silence and no error box, as in the original.

At exit, the run prints `es2sim: audio: … mixed buffers, … bytes to SDL, …
MCI commands (… sent), … notifies (… delivered), … pumps` and the
`es2sim: sos:` line.

### Open questions of the window audio

- The dropout risk during long loads. If heard, a pump from a host timer
  thread is the next step, with the mixer locked.
- The AVI MCI calls.
