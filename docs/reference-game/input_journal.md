# Input journal and input scripts

The input journal records or replays everything DBSIM reads from the outside, so that a scripted input sequence produces identical per-iteration logs in two runs (with [virtual time](../reverse-engineering/timing.md), the oracle acceptance). Own code: the IAT wrappers, record writer, configuration and focus subclass are [input_journal.h](../../tools/reference-game/proxy/input_journal.h) in the proxy; the parser, the per-frame consumption and the counters are the pure-C library `//sim/oracle:journal` ([journal.h](../../sim/oracle/journal.h)), shared with the own process, whose headless platform ([input.c](../../sim/src/input.c)) answers the same imports from a journal (contract: [own_process.md](own_process.md) section 8). Fixture: [input_journal_fixture.c](../../tools/reference-game/proxy/tests/input_journal_fixture.c) (`input_journal_*` targets; modes include `focus` and `pump`).

## How DBSIM reads input

Established with a byte scan for IAT callers and disassembly of the reference DBSIM.

| Import | IAT | Callers / role |
|---|---|---|
| `PeekMessageA` | `0x006cc40c` | pump `0x00464c78` (called every active iteration via `0x0045f570` -> `0x0045a810` -> call at `0x0045a824`, and in the pause path `0x0045f4bc`); pump `0x004659e8` |
| `TranslateMessage`, `DispatchMessageA` | `0x006cc474`, `0x006cc44c` | same pumps -> WndProc `0x00466034` (registered at `0x004650fb`) |
| - | - | WndProc first offers each message to up to 10 handlers at `0x004d3b30`; the keyboard handler `0x00477bec` maps keys via table `0x004a10ec`, adds modifiers and queues events in a 256-entry ring at `0x006bb6b0` (write index `0x006bb8b0`, read index `0x006bb8b4`) |
| `GetKeyState` | `0x006cc438` | only in the keyboard handler: `VK_SHIFT`, `VK_MENU`, `VK_CONTROL` (`0x00477c47`-`0x00477d2b`) |
| `joyGetDevCapsA`, `joyGetPosEx` | `0x006cc4f4`, `0x006cc4f8` | joystick poll for devices 0 and 1 (`0x004776ba`, `0x004776e4`, `0x00477754`) |
| `GetCursorPos` | `0x006cc440` | `0x0046577b` |
| `SetTimer`/`KillTimer` | `0x006cc488`/`0x006cc424` | `SetTimer(hwnd, 1, 1, NULL)` in a dialog procedure `0x00474aa0` on `WM_INITDIALOG`; not in the mission loop |

No DirectInput, `GetAsyncKeyState` or raw input is imported. VSHELL (the shell, not DBSIM) reads input the same way (window messages through `PeekMessageA`, `GetKeyState` for the modifiers); its key table, command codes, mouse scaling and journal forms are in [vshell_widgets.md section 7](../reverse-engineering/vshell_widgets.md#7-input).

## Mechanism

The proxy replaces the IAT slots of `PeekMessageA`, `GetKeyState`, `joyGetPosEx`, `joyGetDevCapsA`, `GetCursorPos` and `CreateWindowExA` in memory from `DllMain`, after checking the image and each `jmp dword [iat]` thunk. `CreateWindowExA` is wrapped only to learn the main window for injected messages. The time key is the active-loop frame counter `[0x004d257b]` (the tick log's `frame_counter_before_increment`, [tick_log.md](tick_log.md)).

| Mode (`[input] mode`) | Behavior |
|---|---|
| `off` | all calls pass through |
| `record` | real results pass through and are appended to the journal (`CREATE_NEW`): keyboard (`WM_KEYDOWN/UP`, `WM_SYSKEYDOWN/UP`) and mouse (`0x0200`-`0x020e`) messages, every `GetKeyState`, joystick and cursor result; written at each frame change |
| `replay` | journal messages are injected at their frame before real messages; real keyboard/mouse messages are discarded; `WM_CHAR` from `TranslateMessage` and all other messages pass; `GetKeyState`, joystick and cursor results come from the journal, falling back to a virtual key state, `JOYERR_UNPLUGGED`/`JOYERR_PARMS` and (320,240) |

The tick log reports `in_mode`, `in_recorded`, `in_injected`, `in_dropped`, `in_fallbacks`, `in_focus_dropped`.

### Focus

DBSIM pauses on `WM_KILLFOCUS` (WndProc jump table entry 8, `0x00466369` -> `0x0045f0d4` sets the pause flag `[0x4d2572]`, the tick log's `loop_pause_flag`) and resumes on `WM_SETFOCUS` (`0x00466373` -> `0x0045f108`); `WM_POWERBROADCAST` suspend/resume (`0x004664d1`) does the same. These messages are sent, not posted, so the `PeekMessageA` hook cannot drop them: a C1_01 pilot run paused at frame 2225 when the desktop focus changed, while the same script had passed that frame before. In replay mode the proxy therefore subclasses the main window after `CreateWindowExA` and answers these messages with `DefWindowProcA` without passing them to the game; a replayed session behaves as if it kept the focus. Record mode does not subclass: focus changes during a recording are real behavior and are not journaled.

## Journal and script format

JSON Lines; whitespace after `:` is allowed; lines without a known `api` (for example comments as `{"event":...}`) are ignored. Entries of one API are consumed in file order, so a script must be sorted by `f` and, within a frame, by `p`.

```text
recorded:  {"f":120,"api":"msg","a":256,"b":38,"c":4718593,"d":0}      message, wParam, lParam
           {"f":120,"api":"key","a":16,"b":0,...}                       GetKeyState(vk) result
           {"f":5,"api":"joypos","a":0,"b":167,...,"hex":"34000000..."} id, result, JOYINFOEX bytes
           {"f":0,"api":"joycaps","a":0,"b":165,"c":404,...}            id, result, size (+ bytes)
           {"f":6,"api":"cursor","a":100,"b":200,"c":1,...}             x, y, result
scripted:  {"f":100,"api":"keydown","vk":38}                            WM_KEYDOWN with scan code from MapVirtualKey
           {"f":120,"api":"keyup","vk":38}
           {"f":100,"p":300,"api":"keydown","vk":13}                    pump-keyed (below)
           {"f":100,"p":320,"api":"mousedown","x":500,"y":377}          WM_LBUTTONDOWN (also mouseup, mousemove), client coordinates
```

### Pump key `p`

In a modal game screen the frame counter stands still but the screen's loop keeps calling `PeekMessageA`. A message entry with `"p":N` is injected at its frame only after N earlier `PM_REMOVE` peeks in that frame (counted in replay mode; the first peek of a frame counts 0; entries without `p` behave as before). The count depends on the session's real message traffic, so `p` orders and delays answers inside a modal screen but is not an exact position; simulation state cannot change while the frame stands. Record mode never writes `p`: in ordinary frames a recorded count could push a key into a later frame and break replay determinism. Injected mouse messages also set the virtual cursor returned by `GetCursorPos` (default 320,240). In the own process the headless platform has no other message traffic, so the peek count in a modal frame is exactly the own modal loop's.

### Recordings and verification

Owner recordings use [record-suite.ps1](../../tools/reference-game/setup/record-suite.ps1) and the [suite workflow](oracle_suites.md). Record at 47 ms/pace 1, replay twice at pace 0. `recording_compare.py` uses the established oracle normalization; record/replay additionally excludes only `in_injected` and `in_fallbacks`; replay/replay keeps those counters. Both comparisons require matching complete frame sets (or the declared bounded window), decoded watches, start configuration and return-file hashes. Missing fields, gaps and empty or truncated captures fail.

Keyboard and mouse may be used; keep focus throughout recording. Successful joystick samples fail the verifier. The owner recording `c3_09-loadout-b` (8,886 frames, 583 mouse moves, 24 clicks, mouse and keyboard, ending destroyed by enemies) replays identically in two replays, so mouse messages are accepted.

**Modal screens.** The recorder omits the pump key. A replay that injected the 108 messages of an end dialog before the dialog existed hung. Verification therefore replays a derived journal ([modal_pump_keys.py](../../tools/reference-game/scripts/modal_pump_keys.py)): messages of frames that took at least 500 ms in the recording, and of the frame after the last tick, get pump keys 300, 310, ... in recorded order. The recording journal is unchanged; the derivation report (`modal-frames.json`) records both hashes; the suite draft's `script` is the derived journal and `recording` names the recording. The two-replay check still decides; a failure is reported and no passing golden is manufactured.

**Screens opened by the player.** A key that opens a modal screen is read by the ordinary pump, so keying it delays the screen. In the `c5_05-moon-missiles` recording the derived journal keyed F11 (objectives screen) at frame 8122, the replay delivered it one frame late and the click that had closed the screen did not, so the replay stalled (`es2-data\logs\c5_05-moon-missiles-authoring\verify-1-stalled`). The same recording has Q and Enter in a frame of 210 ms, below the 500 ms threshold. `record-suite.ps1 -VerifyRecording -ModalOpenerKeys 0x51,0x7A` (`modal_pump_keys.py --opener-vk`) leaves every message up to the first opener key-down of a frame unkeyed, keys the rest, and treats every frame with an opener key-down as modal. This matches the scripted suites (`Q` unkeyed, answers keyed). Q opens EINSATZALARM ([below](#keyboard-mapping)); F11 as opener rests on this recording only (the objectives screen was opened with it; the two frames took 1,369 and 554 ms). Without the option the derivation is unchanged.

## Keyboard mapping

Probe run `probe-keys-run2` (evidence `<es2-data>\logs\input-20261005`): 23 candidate keys, each held 20 frames.

| Key | Effect |
|---|---|
| up / numpad 8 | throttle towards +1024 while held; stays after release |
| down / numpad 2 | throttle down / reverse while held |
| left / numpad 4, right / numpad 6 | turn while held |
| `1`, `5`, `9`, `0`, numpad `+`/`-`, `=`/`-` | no effect on throttle, speed or heading |
| `Q` | ends the mission early: opens the modal in-game dialog **EINSATZALARM** ("Ihr Einsatz ist nicht vollständig ... nicht alle Wegmarkierungen ...") with the buttons WEITER and a second (abort) button |
| `S`, `W`, `A`, `Z` | each tested alone from standstill: no stall, no visible effect in the logged fields |

## Modal dialogs in replay

A script that opens a modal game screen stops logging: the frame counter does not advance, the main thread sits in the screen's own loop and real mouse/keyboard input is discarded in replay mode, so nobody can answer. Established with the probe run above, which stopped logging at counter 895 (window reported as not responding on close; evidence `<es2-data>\logs\hang-s-20261005`):

1. The stop is deterministic: the same script stops at the same point with and without virtual time.
2. [hang_snapshot.exe](../../tools/reference-game/helpers/hang_snapshot.c) shows no extra window and the main thread inside the modal loop `0x00456000`: it polls input (`0x0045a810`, which pumps messages), lets a screen object handle input, updates its gadgets and repaints (`0x00454acc` -> `0x00464a14` -> `0x00465628`: `GetClientRect`, `InvalidateRect`, `BeginPaint`, palette), until `[object+0x2d1]` is set.
3. Prefixes of the script up to and including `S` do not stop. The real stop was at frame 900, where the script pressed `Q` (`Q` alone reproduces it: last logged counter 99 for a `Q` at frame 100; a screenshot shows the EINSATZALARM dialog). Logged records had been lost only because the log was then written every 32 records.

This is not a defect of the game or of the hooks. Consequences:

- The tick log writes every record immediately by default ([tick_log.md](tick_log.md)).
- `run-tick-log-mission.ps1` reports `stalled=true` when no tick arrives for `-StallSeconds` (default 5) while the process lives.
- Scripts answer modal screens with pump-keyed entries. Alert answers measured in C1_01 (`es2-data\logs\c1_01-20261006`): Enter, Escape, left+Enter, right+Enter and down+Enter close the alert with the first button (WEITER, mission continues); **Tab, Enter** selects the second button, which for statuses 4/5/9/10 (`0x49f5c0`) ends the mission: DBSIM writes `results.dat` and `mission.var` and exits. Space and `A` have no effect; a left click at (500,377)/(500,381) had none either (the alert's mouse handling is not understood).

## Acceptance result

Script [acceptance-60s.jsonl](../../tools/reference-game/input-scripts/acceptance-60s.jsonl): frames 100-1377 (60.0 s at 47 ms): full throttle, turn left, turn right, throttle to zero, turn while slow, reverse, turn while reversing, reduce reverse throttle. Arrow keys only. Runs with [run-tick-log-mission.ps1](../../tools/reference-game/setup/run-tick-log-mission.ps1), `timing-r111`, prepared Alpha-sector mission, virtual time 47 ms. Evidence: `<es2-data>\logs\acceptance-20261005`.

| Run | Pace | Ticks | Wall time | Compared with `accept-1` |
|---|---|---:|---:|---|
| `accept-1` | real time | 1,408 | 66.1 s | - |
| `accept-2` | real time | 1,408 | 66.1 s | identical at all 1,408 counters |
| `accept-fast` | unpaced | 1,536 | 1.9 s | identical at all 1,408 common counters |

Compared per iteration: completed clock, 24 timing bytes, both RNG states (stream A changed in 1,018 iterations, B in 28), player position, angles, speed and throttle. The player moved from `[2227902,2324298,13030]` through speed 276 and back to reverse; heading went through 63524 -> 5181 -> 56677 -> 1591. Each run: 16 injected key messages, 1 real input message discarded, 50 fallbacks (GetKeyState without journal entries and joystick polls; identical in all runs).

Scope of this result: one mission, keyboard only, hidden window, measurement clone. Later recorded and scripted suites ([oracle_suites.md](oracle_suites.md)) cover mouse and modal-dialog answers; replays against real joystick devices and `WM_TIMER` are not covered.
