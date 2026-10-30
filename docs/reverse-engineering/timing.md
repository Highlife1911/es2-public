# DBSIM timing

Reference and active-loop boundary: [main_loop.md](main_loop.md).

## Timing code

| Address | Function | Behaviour | Own code |
|---|---|---|---|
| `0x467828` | clock wrapper | `(GetTickCount − [0x4d3b78]) >> 4` (logical shift); about 64 callers use it for blink phases, timed message slots, redraw intervals and clock-gated draws | `es2_sim_clock` (`sim/src/loop.c`) |
| `0x467840` | timing init | sets the scales `0x4d3b64`, `0x4d3b66` to 256, `[0x4d3b70]` to 1000, `[0x4d3b72]` and the references `0x4d3b68`, `0x4d3b6a` to 125 (1000 `sar` 3 with sign bias), `[0x4d3b6c]` to 125; one `GetTickCount` read (call at `0x467883`) goes to the base `0x4d3b78` and the previous clock `0x4d3b74` | `es2_sim_timing_init` |
| `0x4678c0` | frame limiter | threshold = `[0x4d3b74]` + 40 (`add ebx, 0x28` at `0x4678cd`); polls the `GetTickCount` thunk `0x004968ec` (call at `0x4678d0`) until the signed comparison reaches the threshold; elapsed = poll value − previous goes to `0x4d3b6c`; `time_scale` is called twice (reference `0x4d3b68` and `0x4d3b6a` against the elapsed word) into the scales `0x4d3b64` and `0x4d3b66`; the store at `0x46791a` advances the previous clock `0x4d3b74` by the elapsed value | `es2_sim_limiter` |
| `0x467894` | `time_scale` | about `elapsed * 256 / reference`, clamped to `[64,450]` | `es2_time_scale` (`core/timing`) |
| `0x467924` | `movement_step` | applies the signed Q8 scale at `0x4d3b64`, clamps to −32767..32767 ([player_movement.md](player_movement.md)) | `es2_movement_step` (`core/movement`) |
| `0x467a48` | `timer_word_step` | timer object: untouched tag at +0, word at +1; subtracts the scale, clamps a set sign bit to 0 | `es2_timer_word_step` |
| `0x467aa8` | `timer_dword_step` | same with a dword at +1 and the signed scale word | `es2_timer_dword_step` |
| `0x467ac4` | `timer_arm` | dword at +1 = sign-extended duration word `<< 11` modulo 2^32; returns the pointer | `es2_timer_arm` |

The 40 ms is a minimum wait, a ceiling of 25 paced iterations per second. It is
not a guarantee of 25 Hz: clock granularity, rendering time and pauses lengthen
the interval, so a fixed-step model cannot be assumed from the wait alone. The
limiter has a bypass flag; measurement logs must report it. A busy wait on a
constant `GetTickCount` would never finish; a replacement must model both the
wait and the elapsed-time scale.

### Globals

| Address | Representation | Interpretation |
|---|---|---|
| `0x004d3b64`, `0x004d3b66` | two 16-bit values | clamped timing scales |
| `0x004d3b68`, `0x004d3b6a` | two 16-bit values | timing references, initialised to 125 |
| `0x004d3b6c` | 32-bit scratch | absolute clock during the busy wait; elapsed time after the limiter completes |
| `0x004d3b74` | 32-bit clock | last completed limiter timestamp |
| `0x004d3b78` | 32-bit clock | base of the wrapper `0x467828` |
| `0x004d257b` | 32-bit counter | completed active loop iterations, incremented at the observation boundary |

### Core functions

`core/timing` ([hooks.md](../reference-game/hooks.md#distance-and-timing)
has the static semantics, extreme-case tests and validation commands; proxy
hooks are disabled by default). These functions write no globals:

- `time_scale` takes **reference first, elapsed second**, both signed low
  words; only the quotient's low word is clamped, its upper word survives in the
  returned EAX pattern.
- The timer functions subtract modulo their width, then test the sign bit.
  `timer_word_step` and `timer_dword_step` read the scale at loaded-image RVA
  `0xd3b64`.
- `movement_step` (the helper at `0x467924`) multiplies the signed value by
  the signed Q8 scale at `0x4d3b64`, then clamps.

Validation: the four timing hooks and `movement_step` pass their
original-code comparisons in the
[private-image harness](../../tools/reference-game/invitro/README.md)
(1,000,121 comparisons for `movement_step`); replacing `movement_step` in
308,875 calls of C1_01 gives an identical regression. `timer_arm` is not called
by the regression suites, so its live evidence is the in-vitro comparison only.
Startup CW3220 sets x87 control word `0x1332` (hardware readback `0x1372`);
that it persists into mission ticks is a hypothesis
([player_movement.md](player_movement.md)).

## Measured frame rate

Native run, installation `timing-r111` (a clone of the tested primary
installation), prepared Alpha-sector patrol started with the wrapper's
arguments `-eggplant -X2 -R0 -G`; this recreates the prepared mission and does
not load a SAV slot.

| Observation | Result |
|---|---|
| Undebugged baseline, 60 s | 1,280 counter increments, about **21.335 Hz**; the game clock changes by 46 ms (160 times) or 47 ms (1,120 times), mean **46.875 ms** |
| Hardware boundary trace | 250 consecutive counters (2615..2864), all player snapshots readable, no bypass or pause flag; median 46.9854 ms, range 45.4664–48.0467 ms (debugger-perturbed) |
| After debugger detach, 10 s | 213 increments, intervals still 46/47 ms |

The baseline persists only changed clock/counter pairs (1,787 records of
12,000 polls at about 5 ms) and detects no reset, wrap, multi-increment gap or
skipped poll. The sampler is asynchronous, not an atomic state logger.

The active loop therefore runs at about **21.3 iterations per second** on this
host, above the 40 ms minimum, explained by the 46/47 ms clock intervals. The
result characterises this native Windows setup and mission, not every machine.
The in-process [tick log](../reference-game/tick_log.md) observes the loop
boundary.

### Repeating the measurement

Use a separate clone with the prepared `DATA/script.dat`, `player.mec`,
`mission.var` and `mission.str`, its own DRIVE.CFG and an installation manifest
recording `measurement_clone_of`; `build/installs/timing-r111` is
the validated copy. The launcher refuses the primary installation and existing
output files. A completed mission can update the prepared files; restore them
from a snapshot or clone again.

```powershell
tools/reference-game/setup/start-timing-mission.ps1 -InstallName timing-r111 -OutputPath new-mission-launch.json
$launch = Get-Content new-mission-launch.json -Raw | ConvertFrom-Json
# Allow loading to finish; confirm advancing counter values before interpreting a steady-state rate.
python tools/reference-game/scripts/probe_timing.py --pid $launch.pid --seconds 60 --output new-baseline.jsonl
```

`probe_timing.py` has 16 unit tests (counter/clock wraps, polling gaps,
failures, identity gates). Close only the measurement mission: verify both
executable path and process start time against the launch report before
stopping a stored PID, because Windows reuses PIDs.

## Virtual time

DBSIM reads `GetTickCount` only through the thunk `0x004968ec` (IAT
`0x006cc3e8`). A byte scan finds no other reference to the IAT slot and exactly
three callers of the thunk: the wrapper `0x00467828`, timing init `0x00467840`
(call at `0x00467883`) and the limiter poll at `0x004678d0` (return address
`0x004678d5`). The listing desynchronises at `0x467825` and misses the
wrapper's call.

### Contract

A limiter poll returns `previous + step` (never moving backwards), so the
original wait ends after exactly one poll and the elapsed time is exactly the
step. All other callers see the current virtual clock. Tape playback (`-p`,
`-D`) skips the limiter, so a frame that made no poll gets the step a poll
would have given it at the frame boundary. Configuration is `[time]` in
`es2-oracle.ini`, read on the first call:

| Key | Meaning |
|---|---|
| `virtual_time_step_ms` | 0 = off (real `GetTickCount`); otherwise the fixed step per limiter call, at least 40 |
| `virtual_time_start_ms` | initial clock value (default 16,777,216) |
| `virtual_time_pace` | 1 = hold each step for at least the step in wall time; 0 = run as fast as possible |
| `virtual_time_stall_limit` | after this many non-limiter calls without a limiter call, each further call advances the clock by 1 ms (deterministic guard against a busy wait; default 100,000) |
| `virtual_time_intra_frame` | 1 = every non-limiter call after the first since the last limiter poll advances the clock by 1 ms, at most step − 1 times per limiter interval (counted as `vt_intra_advances`; afterwards the stall guard applies); 0 = the clock stands still inside a frame. Absent means 0 in the proxy; the oracle and recording scripts write 1 |

The tick log carries `vt_limiter_polls`, `vt_stall_advances` and
`vt_late_polls`.

### Own code

- `sim/oracle/virtual_time.{h,c}` (`//sim/oracle:virtual_time`) is the pure C17
  state machine: `es2_vt_enable`, `es2_vt_limiter_poll`, `es2_vt_read`,
  `es2_vt_frame_boundary`. It is shared by the proxy and the own process.
- `tools/reference-game/proxy/virtual_time.h` replaces the IAT slot in memory
  from `DllMain`, before DBSIM code runs, after checking the thunk and the
  limiter prologue; it keeps the INI configuration and the wall-clock pacing.
- The own process (`sim/src/loop.c`) names the three call sites explicitly
  instead of testing return addresses; `es2_sim_setup` (`sim/src/host.c`)
  reads `[time]` and defaults to step 47 (`ES2_SIM_VT_STEP`), intra-frame 1.
  Windowed, `sim/src/platform_sdl.c` lets the clock follow the wall clock and
  paces the poll. Details: [own_process.md](../reference-game/own_process.md#7-virtual-time).
- Fixture [virtual_time_fixture.c](../../tools/reference-game/proxy/tests/virtual_time_fixture.c),
  targets `//tools/reference-game/proxy:virtual_time_*_test` (valid, intra-frame
  clock, disabled, step below 40 refused, signature mismatch, pacing).

### Determinism evidence

Live, `timing-r111`, step 47 ms, idle player:

| Run | Pace | Ticks | Wall time | Result |
|---|---|---:|---:|---|
| first | 1 | 1,024 | 48.1 s | reference |
| repeat | 1 | 1,024 | 48.1 s | identical to the first at all 1,024 counters |
| fast | 0 | 1,216 | 1.4 s | identical to the first at all 1,024 common counters |

Compared: completed clock, the 24 timing bytes, both RNG states, player
position, angles, speed and throttle. Every clock delta is 47 ms, no stall
advances. Without virtual time two runs diverge at the first 46/47 ms
difference (counter 105, [tick_log.md](../reference-game/tick_log.md)). The
input-driven oracle suites replay identically too
([input_journal.md](../reference-game/input_journal.md),
[oracle_regression.md](../reference-game/oracle_regression.md)).

### Intra-frame clock and the cockpit instruments

With the clock standing still inside a frame the cockpit instruments are not
drawn: the weapon list (EMP charge bar, ATC50 rounds), the radar picture
(sweep, contacts, range and target readouts), the damage schematic and the two
top-right buttons stay empty, while compass, waypoint, speed and mission time
are drawn and weapons fire. Visual comparison of C1_01 started directly in
`timing-r111`:

| Configuration | Instruments |
|---|---|
| no `[time]` section (real `GetTickCount`) | drawn |
| virtual time, step 47, start 16,777,216, pace 1 | missing |
| same, start close to the real tick count | missing |
| same, pace 0 | missing |
| same, every non-limiter call after the first advances 1 ms | drawn |

Which instrument routine needs the moving clock is not identified; the
wrapper's callers use it for blink phases, timed message slots and redraw
intervals. In C1_01 with the advancing clock a frame makes 9 to 16 non-limiter
calls, so all 850 frame steps stay exactly 47 ms. The 480 ms effect loop at
`0x45db31` (one RNG draw and one rendered frame per pass) ends after a few
dozen passes, not after about 100,000 via the stall guard, closer to the
original's real-time behaviour.

The cap of step − 1 advances per limiter interval makes modal loops
deterministic: the modal end dialog of `c1_01-success` reads the clock once per
message-loop pass, and the pass count depends on real message traffic, so an
uncapped clock would make the clock values from the end of the mission
(frame 5,947 on) differ by about 2 ms between runs. Modal loops always reach
the cap, active frames (9 to 16 reads) never do, and every frame step stays
exactly the step. The goldens are recorded with the cap.

The capped clock also stays in modal loops under tape playback. DEMO1 presses
the GeneralAlert's focused gadget in about 31 consecutive passes; their
release times (clock + 10) do not expire at that pace, and the ninth pending
entry overruns the panel's release list. The original reaches the same state
live (fault at `0x453127` in `c1_01-tape-demo`: frame 920 with the frozen
clock, 919 with the 47 ms step), and any machine faster than 21 fps faults no
later ([oracle_suites.md, "Tape playback
suites"](../reference-game/oracle_suites.md#tape-playback-suites)). Only a
clock at the recording machine's frame times keeps the list short; that is not
part of the oracle and would change every modal replay's reads and release
times.
es2sim therefore keeps this clock and refuses the overrun ([main_loop.md, "The
modal dialogs in the own process"](main_loop.md#the-modal-dialogs-in-the-own-process)).

## Other time sources

Sources: the import tables and `llvm-objdump -d -M intel` listings (image base
`0x400000`) of `VER95/DBSIM.EXE`, `CW3220.DLL`, `BWCC32.DLL` and
`VER95/SOS9503.DLL` from `build/extracted/cd`. Live counts are
entry-probe counts of the DBSIM map (order a60/c1s/c1c/c3_09/c4_08/c3_10,
saturated at 1000; "0" = probed and never entered after the proxy armed).

### Modules DBSIM loads

| Module | How | Time-related imports |
|---|---|---|
| `DBSIM.EXE` | — | `GetTickCount`, `Sleep` (KERNEL32); `SetTimer`, `KillTimer` (USER32); `_time` (CW3220); `mciSendCommandA`, `mciGetDeviceIDA` (WINMM). **No** `timeGetTime`, `timeSetEvent`, `QueryPerformanceCounter`, `GetSystemTime*`, `GetLocalTime`, `GetMessageTime`, no `rdtsc` |
| `CW3220.DLL` | static import (53 names) | `GetTickCount`, `Sleep`, `GetLocalTime`, `GetTimeZoneInformation`, file-time conversions, waits |
| `BWCC32.DLL` | `LoadLibraryA("BWCC32.DLL")` at `0x465140` in the window-class setup `0x4650e8` | none (no timer, tick, time or sleep import) |
| `SOS9503.DLL` | `LoadLibraryA` in `0x4958fd` (Win32s: `sos32s03.dll`, not on the CD), [audio.md](audio.md) | `timeGetDevCaps`, `timeBeginPeriod`/`timeEndPeriod`, `timeSetEvent`/`timeKillEvent`, `CreateThread`, `PostMessageA` |
| `ddraw.dll` | static import; in the oracle our proxy, which forwards to the **system** DirectDraw of Windows 10 (the CD's `DDRAW.DLL` is never installed, [setup.md](../reference-game/setup.md#the-original-installer-and-the-portable-layout)) | internal only |
| `MSVFW32.DLL` | static import (`ICSendMessage`) | none |

### Inventory

| Source | Call sites (caller) | Value goes to | Live | Covered by the virtual clock |
|---|---|---|---|---|
| `GetTickCount` | `0x467828` wrapper, `0x467883` init, `0x4678d0` limiter | frame pacing, elapsed scale, the ~64 wrapper callers (cockpit clocks, blink phases, clock-gated stream-B draws) | 1000 × 6 | **yes** (all three sites) |
| `Sleep(100)` | `0x45f4b7`, the only call; main loop `0x45f160` while pause byte `0x4d2572` ≠ 0 | wall time only. Each pause pass also runs the limiter (`0x45f467`) and the pump `0x464c78` but no simulation stage, so under virtual time every pass is one limiter poll and advances the clock by one step | pause flag 0 in every compared tick | yes for the clock; the number of pause passes is set by when the pause ends |
| pause byte writers | `0x45f0d4` ← 1 (`WM_KILLFOCUS`, power suspend), `0x45f108` ← 0 (`WM_SETFOCUS`, resume), an init store at `0x45cbcb` ← 0 | — | `0x46317c`/`0x463238` (their sound pause/resume) 0 × 6 | the replay keeps focus and power messages from the game ([input_journal.md](../reference-game/input_journal.md)), so a replay never pauses |
| `SetTimer(hwnd, 1, 1, 0)` | `0x474aca`, the only call, case `0x110` of `0x474aa0` | — | 0 × 6 | not needed: unreachable |
| `KillTimer(hwnd, 1)` | `0x474ad7`, case `2` of `0x474aa0` | — | 0 × 6 | unreachable |
| `KillTimer([0x4d3610], [0x4d3608])` | `0x4650e1` in `0x4650d0` (from the shutdown and window paths `0x45f546`, `0x4652e7`, `0x466143`, `0x46685d`, `0x479ccd`) | — | n/p | presumably a no-op: the call is skipped when `[0x4d3608]` = 0, and nothing writes `0x4d3608` (the read at `0x4650d0` is the only byte reference to the address, and no instruction uses offset `0x114c` from the main-loop block `0x4d24bc`; a write through another pointer is not excluded) |
| `WM_TIMER` (`0x113`) | `sfxWndProc` `0x462398` → `0x4639e8` → `0x4737d8` → `sosDIGIUserService`, then `0x474aa0(hwnd, 0x113, …)` → `0x47432c` flag test → `sosDIGIUserService` again | audio driver service only | `0x4639e8`, `0x474aa0` 0 × 6 | nothing to cover: no timer exists on the main window |
| `_time` | `0x45cf91` in `0x45ceb8` (demo pick, `-D`) | `0x49f034` = `time % count` over `tapes\demolist.str` ([rng.md](rng.md)) | `0x45ceb8` 0 × 6 | **no**; only matters for a demo suite |
| MCI CD audio | `0x473c48` play (open `cdaudio`, time format TMSF, `MCI_STATUS` track length `0x473d84`, play with notify), `0x473dcc` play from a position, `0x473c00` stop/close, `0x473d44` `MCI_STATUS` **position** | position → `0x49f904`, read only by the resume paths `0x463238` and `0x462e58` as the start of the next play; track end → `MM_MCINOTIFY` in `0x462398` → stop and replay (`0x4a0e2c`, `0x6b558c`) | `0x473d84` 1 × 6 (`0x473c48` itself 0; presumably the track start begins before the probes arm); `0x473d44`, `0x473dcc`, `0x462d78`, `0x462e58` 0 × 6 | no; touches only CD state |
| MCI AVI | `0x4718f8` (open element, window, status hwnd, play with notify; loops on `MM_MCINOTIFY` via hook `0x4718d4` or a key) and the device probes `0x4717f8`/`0x471848` | presentation | `0x4718f8` has no static reference ([file_io.md](file_io.md)) | not needed |
| `MSG.time` | both pumps `0x464c78`, `0x4659e8` hand the `MSG` only to `TranslateMessage`/`DispatchMessageA`; the window procedures get (hwnd, msg, wParam, lParam) | — | — | not read |
| `ICSendMessage` | `0x47168a`…`0x4717eb` in `0x47136c` (codec queries) | presentation | — | no time involved |
| VGA retrace | `in al, dx` at `0x45c63c` in `0x45c638`, only while word `0x4d2566` = 0 ([display_modes.md](display_modes.md)) | wall time | the only privileged instruction in reached code ([dbsim_map.md](../reference-game/dbsim_map.md)) | not taken in the oracle |
| CW3220 `_time` internals | `_time` `0x41eb64` → `GetLocalTime` and the local-to-UTC conversion | the `_time` result above | — | see `_time` |
| CW3220 startup | two pointers in CW3220's DATA (`0x41e268` `_tzset` → `GetTimeZoneInformation`; `0x41e690` stores `GetTickCount` as the `clock()` base at `0x423080`), presumably its init table | CW3220-private time-zone and `clock()` state | — | not reachable from DBSIM's 53 imports except through `_time` |
| CW3220 file times | `_findnext` → `0x408784` → `FileTimeToLocalFileTime` | the `ffblk` date/time fields; the VOL enumeration `0x467cd8` reads only the name at `ffblk+0x10` ([file_io.md](file_io.md)) | startup | not needed |
| CW3220 `Sleep`, `GetTickCount` | `Sleep` only in `_locking` (`0x4092ef`) and `__sleep` (`0x41a37b`); `GetTickCount` only in `_clock` and its base | — | — | not reachable from DBSIM's imports (call-graph walk over direct calls; indirect calls not followed) |
| SOS timer | `sosTIMERInitSystem` `0x401d20`: `timeGetDevCaps`, `timeBeginPeriod`, `timeSetEvent(1000, 0, TimerProc 0x4026fd, 0, periodic)`; `0x402672..0x4026e4` re-arms with a new period; a mixer thread (`CreateThread` at `0x402afe`); seven `PostMessageA` sites (message `0xa000`) | audio mixing; `sosDIGISampleDone` answers; `0xa000` messages to the main window | DBSIM calls `sosTIMERInitSystem`/`UnInitSystem` only | **no** (below) |
| `sosDIGISampleDone` | `0x473a81` ← `0x4641e8` ← `0x462af0` (is playing) | the servo latches of `0x41a6d0`/`0x41a994` (`0x49a1c9..0x49a1ce`, read nowhere else), the restart of noise row `0x1c` by the radio panel `0x44b514`/`0x44b620`, and the stop of the finished slot (`0x464118`) | `0x462af0` 0/27/2/71/0/12 | **no**: real playback time ([audio.md](audio.md#what-can-change-the-oracles-comparison) item 4) |
| system DirectDraw | imports `timeGetTime`, `SetTimer`, threads and events internally | blocking presents and locks: wall time only; DBSIM reads no time value from it | — | not needed |

### What reaches the compared state

Only `GetTickCount` reaches the compared simulation state. Every other time
source ends in wall time, audio, CD music, presentation, a dead path or the
`-D` demo pick. The virtual clock (all three `GetTickCount` sites, intra-frame
advances capped at step − 1) therefore covers everything the oracle compares,
which matches the identical replays of all six suites.

**`WM_TIMER`, `SetTimer`, `KillTimer`** are dead in DBSIM. `0x474aa0` is not a
dialog procedure but the SOS glue's message handler. It has exactly one caller,
`0x4737fe` in `0x4737d8`, which passes the message only when its window argument
is non-zero; the callers are `0x4639e8` (only for `WM_TIMER`, forwarded from
`0x462398`) and `0x464260` (window 0). The `0x110` and `2` cases with
`SetTimer`/`KillTimer` therefore cannot run, and `WM_TIMER` could only come from
a timer someone else sets on the main window. Live: `0x4639e8` and `0x474aa0`
are 0 in all six suites.

### Time dependence outside the virtual clock

State outside the compared fields, or future suites:

1. **SOS playback time.** In the original `sosDIGISampleDone` answers from
   the driver's real-time mixer; es2sim answers from the sample length and the
   virtual clock ([audio.md](audio.md#headless-decisions)). The answer changes
   the SFX slot table (stop of finished samples), the servo latches and whether
   the radio panel restarts row `0x1c`, all outside the compared fields
   ([audio.md](audio.md#what-can-change-the-oracles-comparison)). At the current suite lengths the dependence is
   latent: two runs of c1s/c3_09/c3_10 on `timing-r111` with all hooks `original`
   give the same counts,
   `0x462af0` and `0x4641e8` 27/71/12, `0x464118` 5/21/3, `0x462830`
   61/154/104, `0x4639e8` and `0x474aa0` 0.
2. **Asynchronous messages.** The SOS timer thread posts `0xa000` messages to
   the main window and MCI posts `MM_MCINOTIFY` when a CD track ends. DBSIM
   ignores the former and only restarts the CD track on the latter, but the
   pumps retrieve both and so shift the `PM_REMOVE` count that pump-keyed
   journal entries (`"p"`, used by c1s/c3_09/c4_08/c3_10) wait for. This moves
   an answer inside a modal screen, where the frame counter stands, and is why
   `"p"` is not an exact position. The difference is accounted per frame by
   `message_trace.py compare` and bounded
   ([main_loop.md](main_loop.md#the-pump-accounting)): each notice before a
   frame's last answer costs one modal pass, the compared fields stay equal
   while the intra-frame clock reaches its cap before the answer, and no
   `MM_MCINOTIFY` reaches a replay (the CD start fails on the oracle machine
   and in es2sim). The notices follow wall time, so es2sim headless posts none
   and does not model them.
3. **`_time`.** Pin its result (DBSIM IAT `0x6cc344`) before a `-D` demo suite
   is added.
4. **CD position** (`0x473d44`). Used only after a focus pause or the screen
   `0x459c74` (`0x462d78`/`0x462e58`), never in a replay; it can stay real.

Can stay real-time: the pause `Sleep(100)` (each pass is one virtual step, and
replays never pause), the SOS mixer and its multimedia timer, `timeBeginPeriod`
(it can change the host's tick granularity and thus the 46/47 ms cadence, a
hypothesis that matters only without virtual time), CD and AVI playback (AVI is
unreachable anyway), DirectDraw blocking, CW3220's startup time-zone and
`clock()` reads, and the file times returned by `_findnext`.
