# DBSIM's other DLLs: BWCC32, SOS, WINMM, MSVFW32, DDRAW

Every import of the reference `VER95/DBSIM.EXE` (image base `0x400000`) from the DLLs that are neither CW3220
([rtl_imports.md](rtl_imports.md)) nor the Windows core (KERNEL32, USER32,
GDI32), the dynamically loaded `BWCC32.DLL` and SOS driver, their call sites
and owners, what reaches simulation state, and what an own replacement must
provide. Related: [audio.md](../reverse-engineering/audio.md) "Layers and
call graph" (the SOS layers), [input_journal.md](input_journal.md) (the
joystick seam), [media_compatibility.md](media_compatibility.md) (Indeo),
[display_modes.md](../reverse-engineering/display_modes.md) (DirectDraw).

**Method.** A byte scan of the PE (import directory, the 134 `FF 25` jump
thunks of `0x496754..0x496a72`, every `E8`/`E9` rel32 and `FF 15` site in
CODE) and the pinned `llvm-objdump -d -M intel` listing. Owners are the
nearest `push ebp; mov ebp, esp` at or before a site, because the function
index ([functions/](../functions/README.md)) lacks most of the small virtual
methods here. Strings are read from the file bytes. Labels: static
unless marked hypothesis.

## Which DLLs the import directory names

Besides `cw3220.DLL`, KERNEL32, USER32 and GDI32 the directory imports
`DDRAW.dll` (1 name), `WINMM.dll` (4) and `MSVFW32.DLL` (1). BWCC32 and SOS
are not imported; DBSIM loads both with `LoadLibraryA`.

| DLL | Name | IAT slot | Thunk | Sites | Owner (entry) | Role |
|---|---|---|---|---:|---|---|
| DDRAW | `DirectDrawCreate` | `0x6cc4e4` | `0x496a54` | 1 | `0x46658c` | display set-up and mode switch (display_modes.md) |
| WINMM | `mciSendCommandA` | `0x6cc4ec` | `0x496a5a` | 17 | `0x4717f8`, `0x471848`, `0x4718b4`, `0x4718f8` (AVI); `0x473bf8`, `0x473c48`, `0x473d84`, `0x473dcc` (CD) | in-mission video and CD music |
| WINMM | `mciGetDeviceIDA` | `0x6cc4f0` | `0x496a60` | 1 | `0x471848` | the AVI device of an opened video |
| WINMM | `joyGetDevCapsA` | `0x6cc4f4` | `0x496a66` | 1 | `0x477674` | joystick capabilities at init |
| WINMM | `joyGetPosEx` | `0x6cc4f8` | `0x496a6c` | 2 | `0x477674`, `0x477720` | joystick position and buttons |
| MSVFW32 | `ICSendMessage` | `0x6cc500` | `0x496a72` | 5 | `0x471628`, `0x471698`, `0x471708`, `0x471778`, `0x4717c4` | video decompression |

Dynamic loads (all `LoadLibraryA` sites of DBSIM): `0x465140` (BWCC32),
`0x49593e` and `0x4959b1` (the two SOS names). `GetProcAddress` sites:
`0x495972`, `0x4959e2` (SOS) and `0x49646b` in `0x496425`, the runtime's
debugger hook (`GetModuleHandleA(0)`, then a named export of the module
itself; not a DLL import). `FreeLibrary`: `0x495a3a` (SOS) only.

## BWCC32: loaded and never used

`0x4650e8` (the window-class registration called by WinMain `0x46538c`)
calls `LoadLibraryA("BWCC32.DLL")` (string `0x4a0859`) at `0x465140`, right
before `RegisterClassA` of class `"ES2"` (`0x4a0855`). The handle in EAX is
overwritten unread; there is no `GetProcAddress` on it and no `FreeLibrary`.
DBSIM imports no dialog function (no `DialogBox*`, `CreateDialog*`; USER32
gives it `MessageBoxA` only), so no Borland custom control class is ever
instantiated. The only effect is BWCC32's own initialisation in the process.
**Hypothesis:** a leftover of the Borland application framework. The
regression below shows the mission path does not depend on it.

`BWCC32.DLL` imports KERNEL32 (46 names), GDI32 (31), USER32 (69) and ntdll
(1), none from CW3220, so an own runtime DLL does not have to serve it.

**Own code.** `es2sim` stands in for the load: `win_main` in
`sim/src/startup.c` counts it and drops the handle.

### The BWCC32 switch

[`proxy/bwcc32_switch.h`](../../tools/reference-game/proxy/bwcc32_switch.h) is
called from the proxy's DllMain (DBSIM.EXE only) when the environment has
`ES2_BWCC32=refuse`. `tools\check.ps1 -Live -Bwcc32 refuse|load` sets or clears
the variable for the runner ([oracle_regression.md](oracle_regression.md#acceptance-script));
`refuse` is the default, `load` keeps the original behaviour.

- **Patch.** In memory, the rel32 of the single call `call 0x496904` at
  `0x465140` (the LoadLibraryA thunk, `jmp [0x6cc3f8]`) is pointed at an own
  stdcall function that loads nothing: EAX 0 and LastError 126
  (`ERROR_MOD_NOT_FOUND`), as a failed load of a missing DLL. It pops the
  argument and keeps EBX/ESI/EDI/EBP. ECX and EDX are not read after the call
  (`lea edx, [ebp-0x28]` follows, then `RegisterClassA`). The other
  `LoadLibraryA` calls (SOS, `0x49593e`, `0x4959b1`: the thunk's other two
  callers) are untouched; nothing else of the image changes.
- **Checks before the patch** (else nothing is changed): the image (base,
  size), `push 0x4a0859` at `0x46513b` with the string `BWCC32.DLL`, the call
  to the thunk, the thunk's `jmp [0x6cc3f8]`, the `lea edx, [ebp-0x28]` at
  `0x465145`.
- **Tests.** `//tools/reference-game/proxy:bwcc32_switch_{switch,refuse,reference}_test`:
  a synthetic DBSIM-shaped image (off unless requested; installed: only the
  rel32 changes, the call returns 0 with LastError 126, registers and ESP
  kept, the slot not reached; a changed push, call, thunk, name or following
  instruction or a wrong image refuses without a change), and the reference
  DBSIM (`ES2_DATA_ROOT`): the three instructions, the slot is KERNEL32's
  `LoadLibraryA` in the import directory, `0x465140` is one of the thunk's
  three callers and the only push of the name, no hook prologue covers it.
  `test_check.py` `test_bwcc32_switch` covers the script option.

**Regression (`refuse` against `load`).**

- **Suites.** `check.ps1 -Live -SkipQuick -Bwcc32 refuse` on five clones with
  the own `cw3220.dll` and all hooks replaced: every suite identical,
  the exit code the same in all, every skipped, bypassed and mismatch
  counter 0; all signatures match, audit without problems.
- **Effect.** The proxy records nothing when it installs the switch; the
  module list was checked separately. `acceptance-60s` on `timing-r111-p1`
  with `ES2_AFTER_LOAD_SNAPSHOT=1` and every hook original, with and without
  `ES2_BWCC32=refuse`, both identical to the golden: the version-2 module list
  of the refused run has no `bwcc32.dll`, the control run has it.
- **After-load pools.** `after_load_snapshot.py compare --scope pools`,
  `ES2_EARLY_ARMING=1`, six runs (unset, `refuse`, `load`, two each), all
  identical to the golden. The noise model of each same-mode pair has 0
  entries. Unset against `refuse` differs in one byte, at +0x22 of the 44-byte
  block `0x004a9b7a/+0x6/+0x1d9a/+0x4/+0x0/+0x77/+0x14/+0x14/+0xd9/+0x38`
  (`7c` against `9c`): the u16 there is the low word of the block's own
  address + 0x40, and the heap of a run with `ES2_BWCC32` set sits 0x20 higher
  because one more environment string comes first (104 `_environ` blocks
  against 103). `load` against `refuse`: identical. So loading or refusing
  BWCC32 leaves the after-load pools byte-identical; the walk pairs 1864
  blocks across modes and 2357 within one mode.

Result: DBSIM's mission path and after-load memory do not depend on
`BWCC32.DLL`.

## SOS: 97 entry points through a name table

The loader `0x4958fd` (use count `0x4a6a98`; Windows NT or 9x major >= 4 →
`sos9503.dll`, else `sos32s03.dll`) fills the slots `0x6cbd60..0x6cbee0` from
the 36-byte records at `0x4a6a9c`; `0x495a1f` unloads. Each slot is read by
exactly one `stdcall` stub in `0x495a43..0x4962b0` (`mov eax, [slot]; call
eax`, word arguments zero-extended). Every stub caller lies in the SOS glue
`0x473708..0x474b2e`, and the glue is reached only from

- the SFX manager `0x463a14..0x4648e0`, whose callers include the hooks
  `sfx_play`, `sfx_play_at`, `sfx_place`, `sfx_stop`, `sfx_rate`, `sfx_voice`;
- the driver service calls `0x4639e8` (from the sound window hook `0x462398`)
  and `0x464260` (from the UI loop `0x454acc`);
- the driver init/uninit `0x473708`/`0x4737a4` (from `0x463694`/`0x4638f0`).

This agrees with audio.md "Driver loading and the SOS entry points". The MIDI
half is dead in DBSIM: the init passes `0xffff`.

`SOS9503.DLL` imports KERNEL32 (43 names), USER32 (2) and WINMM (19), none
from CW3220. Its timer system runs its own thread (`timeSetEvent`,
`waveOut*`, posted window messages `0xa000`; audio.md).

### SOS entry points by use

Exactly one stub per name; "called" means the stub has a direct caller in the
glue. Called (49 stubs): the DIGI system and driver (`sosDIGIInitSystem`,
`…UnInitSystem`, `…InitDriver`, `…UnInitDriver`, `…SetActiveWindow`,
`…AcquireDriver`, `…ReleaseDriver`, `…UserService`, `sosGetErrorString`),
detection (`sosDIGIDetectInit`, `…DetectUnInit`, `…DetectFindFirst`), the
timer system (`sosTIMERInitSystem`, `…UnInitSystem`), samples
(`sosDIGIStartSample`, `…StopSample`, `…StopAllSamples`, `…SampleDone`,
`…SetSampleVolume`, `…SetPanLocation`, `…SetSampleRate`,
`…GetBytesProcessed`), memory (`sosPrepare32Memory`, `sosUnPrepare32Memory`),
streams (`sosDIGIStreamAcquire`, `…Release`, `…PrepareBuffer`,
`…UnPrepareBuffer`, `…AddBuffer`, `…Start`, `…Stop`) and the MIDI names of
audio.md's dead half. The other 48 (timer events, pause/resume output, buffer
skip, panning queries, DMA position, sample pointer, …) are loaded but never
called. audio.md counts 50/47 because it lists `sosDIGIGetBytesProcessed`
under its site `0x473aa9` although that function (`0x473a8c`) has no caller;
both counts agree on the names. The stream functions are reached only for
sample flag `0x1000`, which the sound table never sets.

**Own code.** `sim/src/audio.c` implements the SFX manager, the SOS entry
points and the CD-audio code as own code behind a device seam; under `es2sim`
a stand-in module handle replaces the load of `0x4958fd` (audio.md "Audio in
the own process"). Under the proxy the original SOS driver is loaded.
Replacing the glue `0x473708..0x474b2e` instead of the entry points would
leave the stubs unused.

## WINMM: CD audio, the in-mission video code and the joystick

`mciSendCommandA` has 17 sites:

- **CD audio** (device `"cdaudio"`, `0x4a0e34`): `0x473bf8` stop/close,
  `0x473c48` open, time format and play, `0x473d84` status position,
  `0x473dcc` play from a position. They are reached from the sound API's CD
  wrappers `0x464858`/`0x464874`/`0x464988`/`0x46499c`. The command blocks are
  in audio.md "The MCI contract".
- **AVI video** (device `"avivideo"`, `0x4a0cc4`/`0x4a0ccd`): `0x4717f8` open,
  `0x471848` close (with `mciGetDeviceIDA` at `0x47184e`), `0x4718b4` and
  `0x4718f8`. These are plain functions without any caller.

`joyGetDevCapsA` (`0x4776ba`) and `joyGetPosEx` (`0x4776e4`, `0x477754`) are
in the joystick init `0x477674` (caller `0x47760a`) and poll `0x477720`
(caller `0x45c54b`).

**Own code.** `sim/src/input.c` answers both joystick imports from the input
journal (headless) or the window host (SDL3); `sim/src/audio.c` records the CD
calls (import table in [own_process.md](own_process.md)).

## MSVFW32 and the AVI code; DDRAW

`ICSendMessage` has 5 sites, all in the AVI block (`0x471628`, `0x471698`,
`0x471708`, `0x471778`, `0x4717c4`): the video decompressor (Indeo; the
proxy's `system_indeo` path, media_compatibility.md). The block
`0x471628..0x471a4d` has no reference anywhere in DBSIM, so no mission path
opens a video; its first `ICSendMessage` owner is `0x471628`. Function table
and evidence: [own_process.md](own_process.md) "The AVI code in DBSIM and
es2sim". `es2sim` keeps no counterpart and needs no Indeo path.

`DirectDrawCreate` has one site, `0x466595` in the display set-up `0x46658c`
(display_modes.md); the proxy is that `ddraw.dll`.

## What reaches simulation state

Only the joystick values and, indirectly, the sound API:

- The joystick position and buttons go through the control record into the
  player's input; in the oracle the input journal answers both calls
  ([input_journal.md](input_journal.md)).
- The sound API's simulation-visible effects (stream-B draws, the voice/lip
  timeline) are DBSIM code above the SFX manager, already behind the `sfx`
  hooks (audio.md "Sound/clock isolation: the sfx hooks"). The SOS DLL writes
  only audio memory (audio.md, "No DBSIM code runs on the driver's thread").
- CD audio and the AVI video are presentation. One side effect matters to the
  oracle: the first `mciSendCommandA` of a mission is the CD-music start
  `0x46313c`, called at `0x461dc3` in the mission load, the proxy's arming
  point before L0 ([mission_load.md](../reverse-engineering/mission_load.md)).
  It runs only with a sound context and script header word 4 = 0, which is why
  TRAIN1 never arms without early arming
  ([oracle_suites.md](oracle_suites.md#train1-timers)).

## Requirements on replacements

- **Own `cw3220.dll`:** nothing from these DLLs (neither BWCC32 nor SOS9503
  imports CW3220). DBSIM ignores the result of the BWCC32 load, so a missing
  BWCC32 cannot change its control flow.
- **Own audio driver:** the 49 called SOS entry points with their
  word-argument `stdcall` stubs, the timer system the driver starts, and the
  slot table filled by `0x4958fd`. CD audio stays on MCI.
- **SDL3 input:** the two joystick imports are answered behind the same seam
  as the journal.
- **AVI class and Indeo:** presentation; the proxy's media layer owns this
  path. No simulation code needs it.
