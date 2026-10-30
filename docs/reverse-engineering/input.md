# Player input and command dispatch

The input path of DBSIM, from the window procedure to the per-frame control
record and the input dispatcher with its children, and the own code that
implements it: hooks over the original (core, adapter, in-vitro cases) and the
own process's input layer (`sim/src/input.c`, `dialogs.c`, `joystick_sdl.c`).

- **Source.** The `llvm-objdump` listing of the reference `VER95/DBSIM.EXE`
  (SHA-256 `8561aa47…c4ad`, image base `0x400000`). **Confirmed** means read
  from the instructions; **in vitro** means the original's machine code and
  the own code gave the same results in the differential named in the
  section. Role names and field meanings are hypotheses unless a cited
  document confirms them.
- **Map probes.** Counts per suite are the entry probes of the
  [DBSIM map](../reference-game/dbsim_map.md) (the six-suite `map.json` of `dbsim-map-20261008`/`-20261010`, which es2-data no longer holds; the current map has 40 suites; in the order a60/c1s/c1c/c3_09/c4_08/c3_10, saturated at 1000; "0" means probed and
  never entered, "n/p" not probeable).
- **The chain.** The window procedure `0x466034` calls the keyboard handler
  `0x477bec`, which offers each key to the hot-key callbacks (`0x45a324`
  flight keys, `0x45a498` command-key queue) and queues the rest in the key
  ring. Each logic frame `0x45f570` builds the control record `0x4d22c6` in
  `0x45a810` (pump, device poll, keyboard axes `0x45a4cc`, queued commands to
  `0x45feb8`, the mouse event queue `0x452d40`) and passes it to the input
  dispatcher `0x460870`.

## The input dispatcher `0x460870`

Hook `player_input` (row 12 of the logic frame's call table in
[main_loop.md](main_loop.md)); its register flow, jump table and own code are
in [Torso helpers and the input dispatcher](#torso-helpers-and-the-input-dispatcher).

### Size and jump table

`0x460870` spans `0x460870..0x4610ac` (2,109 bytes including a 21-entry jump
table at `0x460d83`) and has **579** reachable instructions. A CFG walk that
does not follow indirect jumps stops at the `jmpl *0x460d83(,%eax,4)` at
`0x460d7c` and sees 398 instructions and 17 direct callees. A walk that reads
the table bound from the preceding `cmp $N` finds over the whole image:

- 178 functions with jump tables, 45 of them gain instructions or callees,
  +8,348 instructions and +142 call edges in total; 133 indirect jumps stay
  unresolved (import thunks such as `0x49681a`, tables without a `cmp` bound).
- 25 of the 178 are live, among them `0x460870` (398 → 579, seven more
  callees), `0x45feb8` (448 → 599, six more), `0x426b70` (spatial triggers:
  30 → 260 instructions, new callees `0x4234b8` and `0x423508`), `0x413280`
  (118 → 247), `0x455950`, `0x42a3f4`.
- The static reach of `0x460870` with 121 registered hooks as leaves is 628
  functions / 332 live originals / 28 hooks (without the jump tables: 577 /
  307 / 28).

Reach and instruction numbers that come from a walk without jump tables are
therefore lower bounds for every function that has a jump table in its reach.

### Contract

`uint16 0x460870(record R)`, cdecl, one stack argument; the only caller is
the logic frame (`0x45f838` in `0x45f570`, R = `0x4d22c6`, the 48-byte record
that `0x45a810` built at the start of the frame). ECX and EDX are not read at
entry. Prologue `55 8b ec 81 c4 74 ff ff ff 53 56 57` (frame of 0x8c bytes);
no exception frame, no x87. Every local of its own is written before it is
read on every path (the word at EBP-4 is written and never read); the 12-byte
aim buffer at EBP-0x74 is filled by `0x41b728` (two of its three paths
confirmed; the `0x40e4dc` path writes are not read) before `0x41ef14` reads
it. EBX = player P = `[0x4d24e6]` until the button loop.

**Return.** AX = 1 when `0x45feb8(word R+0)` returned AX = 2, else 0
(SETE/AND at `0x460899`, stored at EBP-2, reloaded with `movw` at `0x4610a2`;
the high word of EAX is whatever the button loop left). The caller tests AX
only (`0x45f83e`) and returns 1 from the frame at once, so the rest of the
frame (timers, triggers, view, actor list, vehicles, outcome) is skipped.
The commands that can return 2 are Q (outcome dialog), Ctrl+Q and P (below).

**Record fields read** (offsets from R; meanings of the axes are hypotheses
from the consumers):

| Field | Read | Use in `0x460870` |
|---|---|---|
| `+0x00` word | always | command code for `0x45feb8` (keyboard code, see below) |
| `+0x02`, `+0x06` dword | `R+0xb` set | cursor X, Y (mouse drag; also passed to the HUD as `R+2`) |
| `+0x0b` byte | always | mouse button held (drag) |
| `+0x0d` byte | outside view | third camera argument of `0x401c74` |
| `+0x0e` word | cockpit view | turn input → `0x4160dc` (legs) / `0x41bb9c` |
| `+0x10` word | cockpit view | throttle input → `0x4160dc` / `0x41bb9c` |
| `+0x12` word | cockpit view | torso twist rate → `0x41a550` |
| `+0x14` word | cockpit view; outside view only if `0x45c524(3)` ≠ 0 and byte `[0x477904()+4]` ≠ 0 | torso pitch rate → `0x41a808` |
| `+0x16..+0x1d` bytes | always | eight button states (button loop) |
| `+0x22`, `+0x26` dword | outside view | pointers to two words, first and second camera argument of `0x401c74` |

`0x432b14` and `0x415608` read further fields (`+0x02`, `+0x06`, `+0x0b`,
`+0x1e..+0x21`; the weapon dispatch `0x410dbc` reads its own). The input
set-up stores pointers to `R+0xe`, `+0x10`, `+0x12`, `+0x14` and
`R+0x16..+0x1d` in `0x4d2310..0x4d233c` (`0x45a6a7..0x45a70d`); the live path
of `0x45a810` copies eight device bytes through them (`0x45b1dc..0x45b1fa`),
tape playback sets `+0x16..+0x19` from bits 0..3 of a tape byte. That the
button bytes are 0/1 is a **hypothesis** (the latch below flips them with
XOR 1).

### Execution order

1. `0x45feb8(word R+0)` (pushed as the full EAX after `movw (%esi),%ax`);
   remember (AX == 2).
2. `0x432b14([0x49b088], R)`: cockpit/HUD input (`[0x49b088]` is the cockpit
   object, [mission_runtime.md](mission_runtime.md)).
3. Axis selection by the view word `0x4d24f0` (0 = cockpit, 1 = outside;
   setter `0x45df34`, Ctrl+T, [launcher.md](launcher.md)):
   - cockpit: turn = `R+0xe`, throttle = `R+0x10`, twist = `R+0x12`,
     pitch = `R+0x14`; camera arguments 0.
   - outside: camera arguments `[[R+0x22]]`, `[[R+0x26]]`, byte `R+0xd`;
     turn = throttle = twist = 0; pitch as in the table above.
4. **Mouse drag.** Function statics with compiler init guards (`0x49f6e8`,
   `0x49f6e9`): drag values `0x49f6e0` (twist), `0x49f6e4` (pitch), active
   byte `0x49f6ea`, anchor `0x4d2a76`/`0x4d2a7a`. Button `R+0xb` up: active
   = 0, both values 0. Button down and not active: if the cursor lies in the
   rectangle `[[0x49b088]+0x1f5]+4..+0x13` (x0, y0, x1, y1, inclusive), set
   active and store the anchor. Button down and active: twist value =
   sgn(x − x0)·((x − x0)² >> 6), pitch value = sgn(y0 − y)·((y0 − y)² >> 5)
   (32-bit products, arithmetic shifts, x0/y0 the anchor). If either value
   is nonzero, their low words are added to twist and pitch.
5. `0x401c74([0x4d24ea], a, b, c)` with the three camera arguments (always,
   with zeros in the cockpit view).
6. Only if byte `P+0x39` ≠ 0 (meaning open):
   - **Flyer chassis** (word `[[P+0x1f2]+0x50]` ≠ 0, the Razor in
     c4_08): if the freeze word `0x4d24f2` = 0,
     `0x41bb9c(P, turn, throttle, pitch, twist)`. This is the test at
     `0x460ad4` (launcher.md "Freeze and single step"): freeze stops only the flyer
     input, the ground path below runs while frozen.
   - **Ground chassis:** T = target `[P+0x1a4]`; d = T ? `dist_gate(P+0x26,
     T+0x26)` : 0.
     - **Align mode** (dword `0x4d2a70` ≠ 0): H = word `P+0x10` (heading),
       e1 = H − `0x4d2a74`, e2 = H − twist(P) − `0x4d2a74` with twist(P) the
       virtual `+0x3c` (`0x415488`: word `P+0x298`); u =
       `multiply_q10(100, e1)`, v = `multiply_q10(0x46, e2)`; U = u²>>8,
       V = v²>>8 (s16 operands); if U < 30 and V < 10, `0x4d2a70` ← 0 (done
       after this frame). U takes the sign of u and is negated again when
       the virtual `+0x38` (speed, `0x415498`) is negative; V takes the sign
       of v. Then `0x4160dc(P, U, throttle)`, `0x41a550(P, V, −1, 0)`,
       `0x41a808(P, pitch, d, −1, 0)`.
     - **Normal:** `0x4160dc(P, turn, throttle)`. If twist or pitch is
       nonzero: word `0x4d2504` ← 0. Otherwise, if byte `[[P+0x202]+0x14]`
       ≠ 0 and T ≠ 0 and byte `T+0x99` = 0: `0x41b728(P, buf, 0, 0)`,
       `0x41ef14(P, buf)` (buf = 12 bytes at EBP-0x74), `0x4d2504` ← 0,
       tracked. Then: `0x4d2504` ≠ 0 → `0x41e8d4(P, d)`; else, unless
       tracked, `0x41a550(P, twist, −1, 0)`, `0x41a808(P, pitch, d, −1, 0)`.
7. If `0x4d24f0` = 0 (cockpit): `0x415608(P, R)` (weapon fire, below).
8. **Button loop**, i = 0…7 over c = byte `R+0x16+i`: skip if c = 0, c ≥ 21,
   or c was already seen this call (21-byte table at EBP-0x8c, cleared by
   `memset` `0x49681a`). Mark c; `0x45b734(i+1, 1)`; action A = byte
   `0x4d1f38[(i + byte 0x4d2577 + 4) & 0xff]`; A > 20 → skip; else the
   case table below. The seen table is indexed by the **button value**, not
   by the button or the action: with 0/1 values only the lowest pressed
   button runs per frame (**hypothesis** on the values).
9. Return AX.

### Flyer input

The flyer branch of step 6 (`0x41bb9c`, hook `flyer_input`) and the flight
model `0x466b58` it calls are described in [flyer.md](flyer.md): for a flyer
the turn and throttle axes (`R+0xe`, `R+0x10`) act as roll and pitch, the
twist axis (`R+0x12`) as yaw and the lever axis (`R+0x14`) as throttle;
`0x41bb9c` writes word `P+0x290` and byte `P+0x93` only when the lever axis
is nonzero. Of the six map suites only c4_08 has a player flyer; the C1_01
suites have AI flyers; c3_09/c3_10 contain no flyer.

### Button latch

`0x45b734(n, v)` sets byte `0x4d22f5+n` = 1 and byte `0x4d22fd+n` = v (arrays
`0x4d22f6..0x4d22fd`, `0x4d22fe..0x4d2305`). The next `0x45a810` forces a
button that is still pressed with the latched value to its complement and
clears the latch when the button changes (`0x45b1fc..0x45b23b`): a held
button acts once. Projectile flight calls it too (`0x45b734(1,1)`,
[weapons.md](weapons.md)).

### Button actions

Jump table `0x460d83`; table `0x4d1f38` is a 54-byte controls block loaded
at `0x4597bc`; `0x4d2577` is initialised to 13 at `0x45cbd2`, so the actions
are bytes `0x4d1f49..0x4d1f50`; `0x45a810` uses the same bytes with the fixed
index `0x11+i`. H = `[0x49b088]`, W = `[P+0x202]` (weapon system):

| A | Handler | Effect |
|---:|---|---|
| 0 | — | none |
| 1 | — | none here: `0x45a810` copies a button with action 1 into `R+0xd` and clears it (`0x45b245..0x45b25e`) |
| 2 | `0x460ddc` | `0x43349c(H)`: next target (HUD) |
| 3 | `0x460deb` | align mode on: `0x4d2a70` ← 1, `0x4d2504` ← 0, `0x4d2a74` ← word `P+0x10` − twist(P) (W+0x14 is not cleared, unlike key `0x2b`) |
| 4 | `0x460e24` | torso centering on: `0x4d2504` ← 1, `0x4d2a70` ← 0 |
| 5 | `0x460e39` | if byte `[0x477904()+4]` ≠ 0 and byte `0x4d1f46` = 1: word `0x49a06e` ← 1 if ≤ 0, else −1 (throttle lever direction, as Ctrl+1) |
| 6 | `0x460e81` | `0x45feb8(0x14)` (key T) |
| 7 | `0x461070` | stop: word `P+0x290` ← 0, byte `P+0x93` ← 1 |
| 8 | `0x460e8e` | `0x4333c8(H)`: nearest target (HUD) |
| 9, 10 | `0x460e9d`, `0x460eb2` | key method `+0x2c(P, 0x1a / 0x1b)` |
| 11 | `0x460ec8` | `0x432bc8(H, 1)` if `[0x4cf99c]` ≠ 0, else `0x432bc8(H, 0x41)` |
| 12 | `0x460ef7` | view switch of key V without V's camera guard: word `0x4d2534` ← 0; view mode `0x4d24ee` 1, 3, 4 → `0x42a3f4([0x4d24c0], 3)`, `0x4d251a` ← 2; mode 2 → `0x42a3f4(…, 2)`, `0x4d251a` ← 3 |
| 13, 18 | `0x460f56`, `0x461022` | virtual `+0xc` of `[H+0x1e1]` with 0x26 / 0x29 |
| 14 | `0x460f71` | `0x446e3c([H+0x1ed])`: cycles a HUD panel mode `+0xc1` through 0…5 |
| 15 | `0x460f88` | only if frame counter `0x4d257b` ≥ 50: `0x4d2534` ← 0; mode 1, 4 → as 12; mode 2 → `0x42a3f4(…, 2)`, `0x4d251a` ← 4; mode 3 → `0x4d251a` ← 4 |
| 16, 19, 20 | `0x460ff3`, `0x46103b`, `0x461054` | `0x410f78(W, 0x202 / 0x11 / 0x211)` (keys Alt+1, W, Alt+W) |
| 17 | `0x461012` | `0x432bc8(H, 1)` (HUD key Esc) |

### Torso, throttle and target state

From the callees' bytes (all confirmed reads/writes, roles hypotheses; the
exact contracts are in the hook sections below):

- `0x4160dc(P, turn, throttle)` — legs. Speed set point word `P+0x290`.
  With a throttle lever (word `0x49a06e` ≠ 0) and cockpit view it is set
  directly to |throttle − 256|·2 (0 below 100, negated when `0x49a06e` < 0),
  byte `P+0x93` ← 1 and the throttle input becomes 0. Then (throttle
  negated when `0x49a06e` < 0) Δ = `multiply_q8(0x91, −throttle)`; Δ ≠ 0
  sets `P+0x93` ← 1 and either zeroes the set point when the sum changes
  its sign or stores the sum clamped to −0x400…0x400 (lower bound 0 when
  `0x49a06e` > 0, upper bound 0 when it is < 0). The turn input is negated
  except when |throttle| > 60 with throttle > 0, or |throttle| ≤ 60 with a
  negative set point. Then `vehicle_control(P, turn, multiply_q10(s, P+0x290),
  0)` with s = −word `[[P+0x1f2]+4]` for a negative set point, else word
  `[[P+0x1f2]+6]`.
- `0x41a550(P, rate, target, snap)` — torso twist `P+0x298`. cfg =
  `[P+0x1f2]+2`; the scaled rate `multiply_q8(rate, cfg+0x1c)` replaces the
  current rate `P+0x294` when its magnitude is not larger, otherwise
  `approach_word(&P+0x294, scaled, movement_step(cfg+0x1e))`; the twist
  grows by `movement_step` of the mean of old and new rate and is clamped
  to ±`cfg+0x20`; with snap ≠ 0 a crossing of `target` stops there (rate
  0); `track_seek([P+0x230], cfg+0x1a, twist >> 2)`; for P with byte
  `P+0xa3` set, `0x41a6d0` only checks whether sound row 0x21 is still
  playing (`0x462af0`, driver sample-done query, [audio.md](audio.md)) and
  writes only its latch `0x49a1c9`/`0x49a1ca`.
- `0x41a808(P, rate, d, target, snap)` — torso pitch `P+0x29a`, same scheme
  (scale `cfg+0x24`, step `cfg+0x26`, rate `P+0x296`, clamp
  `cfg+0x2a…cfg+0x28`, track `[P+0x234]` with `cfg+0x22`), then always
  `0x41a74c(P, d)` (per-weapon convergence for distance d over the weapon
  list `[W+4]`), sound-row check `0x41a994` for `P+0xa3` (latch only, see
  [audio.md](audio.md)).
- `0x41e8d4(P, d)` — torso centering: rates −clamp(`multiply_q10(250,
  angle)`, ±256) for twist and pitch with target 0 and snap 1.
- `0x41b728(P, out, p3, p4)` — aim point of the target: via the targeting
  computer `[P+0x30b]` (`0x40e4dc`, entered only in `c3_10-hunt-targ`) when it is
  set and the target is closer than 30,000; otherwise the target's virtual
  `+0x24` node transformed by its pose (`euler_matrix`, `transform_point`),
  or its position `+0x26`. `0x41ef14(P, out)` turns the torso toward it
  (352 instructions; also used by AI behaviour `0x41f5a0`).
- `0x415608(P, R)` — fire: `0x410dbc(W, R, P)`; when it returns nonzero, a
  line of n points (n = clamp(d >> 12, 1, 40), or 20 without target, step
  0x1000 along the torso heading `P+0x10` − twist) is written to
  `0x4a9b88`, count `0x4a9cc8` (else count 0). The AI obstacle steering
  reads these points for wingmen of the player ([ai.md](ai.md), avoidance
  points), so the line is simulation state. It runs in the cockpit view
  only: in c3_10 `0x415608` ran 729 times in 937 frames; the 208 missing
  frames match the 209 calls of the death sequence `0x45fa84`
  (**hypothesis:** outside view during the death camera).
- **`P+0xa3` is the player flag (hypothesis):** it gates the servo sounds
  here, the record-driven guided projectile in [weapons.md](weapons.md)
  (`owner+a3`), and the avoidance points in ai.md.
- **Target selection goes through the cockpit object.** `0x4333c8` (nearest)
  and `0x43349c`/`0x4332dc` (cycle) store the target in `[0x49b088]+0x210`;
  `0x41b130` copies it into `P+0x1a4` when it differs (`0x41b328..0x41b365`).
  These HUD functions therefore change simulation state.
- Torso centering `0x4d2504` is also set by `0x41b130` (`0x41b3f5`).

### Command handler `0x45feb8` (key → handler)

The key table of the command handler; its register flow, frame and own code
are in [Command handler as own code](#command-handler-as-own-code).

Callers: `0x460870` (record code, every logic frame), `0x45a810`
(`0x45ad62`: every queued code of `0x4d20c4`, count `0x49eaa0`, result
ignored; the queue is filled by the key callback `0x45a498` registered at
`0x45b6d4` for the codes listed at `0x49eb35`: Enter, `-`, `=`, `0x1a`,
`0x1b`, Num+, Num−), the window procedure (`0x466522`, `0x466535`: code
0x410), and button action 6. Codes: scan-code numbering plus Alt `0x200`,
Ctrl `0x400`, Shift `0x800` ([launcher.md](launcher.md)); both input paths
mask the code with word `0x49eab8` = `0x47ff` (no writer found), which
removes Shift, so the Shift cases below are unreachable from the keyboard.
Key names are the US Windows virtual keys; OEM keys differ on the German
layout.

Contract: cdecl (uint16 code), runtime exception frame (`0x49630c`, EAX =
`0x49f648`). Returns 0 when word `0x4d2564` = 0 (set to 1 at `0x45f44f`
just before the main loop). Otherwise in order: the HUD key handler
`0x432bc8([0x49b088], code)` (AL ≠ 0 → return it), `0x45fe6c(code)`
(Alt+Enter `0x21c` → window/fullscreen switch `0x4667c8` unless tape
playback with `0x4d252c` set; Ctrl+B `0x430` → `int3` at `0x467ad8` when the
option word `0x4d252c` is set; returns 1 for these two codes), then the
case table; the result is EBX + EDI with EBX = 1 for a handled case and
EDI = 0 unless noted.

| Code | Key | Handler | Effect |
|---|---|---|---|
| `0x01` | Esc | `0x46069f` | camera `+0x4a` = 0: `0x42a3f4([0x4d24c0], 3)`, `0x4d251a` ← 2. The HUD handler returns 1 for Esc while its byte `+0x20f` is 0, so this case only runs while that byte is set |
| `0x0e` | Backspace | `0x46053f` | centering on (`0x4d2504` ← 1, `0x4d2a70` ← 0), byte `W+0x14` ← 0; if the HUD sub-object `[[0x49b088]+0x1e1]` (getter `0x4610ad`) has byte `+0xb3` set, `0x441fa4` toggles it off |
| `0x0f`, `0x1c` | Tab, Enter | `0x4604e1` | if `[[0x4d24c0]+0x14]` = 4: `0x45fe38` toggles word `0x4d2518` (the player's view preference) and sets the view countdown `0x4d2520` ← 2 when the camera subject `[cam+0x32]` is the player |
| `0x10` | Q | `0x4605ea` | outcome dialog: status `0x413180(P, 1)`, dialog `0x455950`, EDI = (button == word `0x49f5c0[status]`), word `0x4a9e68` ← 0 → returns 2 on a match ([mission.md](../formats/mission.md#q-einsatzalarm-and-incomplete-waypoints)) |
| `0x14` | T | `0x46058e` | `0x441fa4` toggles byte `+0xb3` of `[[0x49b088]+0x1e1]` (refused while word `[[o+0x20]+0x1b]` = 2); if it toggled and the byte is now 0 (`0x442120`): centering on, `W+0x14` ← 0 |
| `0x19` | P | `0x460759` | modal dialog `0x4561dc(obj, 0, 1, 0)`; EDI = dialog result (a result of 1 makes `0x460870` return 1; which buttons give 1 is open) |
| `0x28` | `'` (OEM 7) | `0x460812` | `0x4333c8(H)` nearest target |
| `0x2b` | `\` (OEM 5) | `0x4604fb` | align mode on (as action 3) and `W+0x14` ← 0 |
| `0x2f` | V | `0x4603d9` | camera `+0x4a` = 0: view switch (as action 12) |
| `0x31` | N | `0x460446` | camera `+0x4a` = 0, `[[0x4d24c0]+0x14]` = 4, view mode 3: next member of the player's group as view subject ([launcher.md](launcher.md)) |
| `0x35`, `0x835` | `/` (OEM 2), Shift+ | `0x460821` | `0x4669c4(0x45f070())`: WinHelpA with a built file name (UI) |
| `0x58`, `0x219` | F12, Alt+P | `0x4607d7` | `0x45cff0()` (338 instructions, dialog `0x4566e0`; never entered in the suites, role open) |
| `0x21f`, `0x24e` | Alt+S, Alt+Num+ | | freeze toggle, single step ([launcher.md](launcher.md)) |
| `0x402` | Ctrl+1 | `0x4607de` | throttle lever direction toggle (as action 5) |
| `0x410` | Ctrl+Q | `0x4606cd` | modal dialog `0x4561dc(obj, 1, 1, 0)`; EDI = result; nonzero → word `0x4d24fe` ← 1 (the main loop ends) |
| `0x414`, `0x419`, `0x421`, `0x431`, `0x620`, `0x631`, `0x633`, `0x634` | Ctrl+T/P/F/N, Alt+Ctrl+D/N/,/. | | cheat cases ([launcher.md](launcher.md)) |
| `0x82f` | Shift+V | `0x46009f` | camera `+0x36` = 1: `0x401148(camera, 3)` (unreachable, see mask) |
| other | | `0x46082f` | key method `+0x2c` of the player (dword `0x49ef34` ≠ 0) or of the view subject `[0x4d2684]`; EBX = its result |

Code 0 (no key) also takes the default: every logic frame calls the
subject's key method with 0 (`0x4157c8` 1000 in all six suites; the HUD
handler returns 0 for code 0, `0x45fe6c` too). The player in c4_08 is a
`MECH` with a flyer chassis (vtable `0x49a282`); `0x4215c0` (flyer vtable
`0x49a5e0`) is never entered.

Two points ([launcher.md](launcher.md#main-command-handler-0x45feb8-confirmed)): Alt+Ctrl+N damages
only the **first** qualifying actor of list `0x4a9af8` per key press (the
handler jumps to the exit right after the `+0x74` call, `0x46013e`); and
`0x460870` passes only the record code, the other codes reach `0x45feb8` from
`0x45a810`'s queue loop, the window procedure and button action 6.

### Callees of the input dispatcher

Direct callees of `0x460870` (24 including the seven behind the jump table,
plus seven virtual call sites). Counts a60/c1s/c1c/c3_09/c4_08/c3_10 count
all callers. "Sim" = writes state that the simulation reads. "Own code" names
the hook that replaces the callee; the others are called through the guest
bridge.

| Callee | Instr. | Calls per suite | Role | Sim | Own code |
|---|---:|---|---|---|---|
| `0x45feb8` | 599 | 1000×5/937 | command handler (above) | mixed | hook `command_key` |
| `0x432b14` | 64 | 1000×5/937 | HUD input: cursor to `0x433b20`, record bytes `+0x1e..+0x21` → display modes `0x42a3f4` 1/0/5/4, mouse-button latch `0x49b550` | no (hypothesis) | guest call (HUD) |
| `0x45c524` | 16 | 1000×6 | device slot `[0x4d2468+4n]` − 0x1f | no | guest call (platform) |
| `0x477904` | 43 | 1000×6 | fills the device state `0x6bb6a8` (8 bytes) | no | guest call (platform) |
| `0x401c74` | 71 | 1000×6 | outside camera control: `0x401c20`/`0x401c4c` ramp camera `+0x30`, `+0x24`, `+0x2c` (camera mode `+0x36` 0 or 1, `+0x4a` = 0) | camera only | hook `outside_camera` |
| `0x41bb9c` | 63 | 0/0/0/0/1000/0 | flyer control → flight model `0x466b58`; `P+0x290`, `P+0x93` | yes | hook `flyer_input` |
| `dist_gate` `0x49288c` | | hook | target distance | | hook `dist_gate` |
| `multiply_q10` `0x47e0b0` | | hook | | | hook `multiply_q10` |
| `0x4160dc` | 125 | 1000/1000/1000/1000/0/937 | legs: throttle and turn → `vehicle_control` | yes | hook `leg_input` |
| `0x41a550` | 122 | 1000×6 | torso twist servo | yes | hook `torso_twist` |
| `0x41a808` | 126 | 1000×6 | torso pitch servo + `0x41a74c` convergence | yes | hook `torso_pitch` |
| `0x41b728` | 80 | 0/1000/0/1000/493/0 | target aim point | writes `out`, `*p3`; `0x40e4dc` steps a weapon timer | hook `aim_point` |
| `0x41ef14` | 352 | 518/1000/1000/1000/1000/383 | torso toward a point | yes | hook `torso_aim` |
| `0x41e8d4` | 63 | 1000×6 | torso centering | yes | hook `torso_center` |
| `0x415608` | 89 | 1000×5/729 | fire + fire line `0x4a9b88` | yes | hook `fire_line` (with `fire_selected` `0x410dbc`) |
| `0x49681a` | 1 | n/p | `memset` import thunk (cw3220) | | RTL |
| `0x45b734` | 10 | 0/77/0/308/42/28 | button latch | input layer | guest call (platform state) |
| `0x43349c` | 253 | 0/4/0/17/5/0 | next target (HUD) | yes (target) | hook `cockpit_next` |
| `0x4333c8` | 72 | 0 | nearest target (HUD) | yes (target) | hook `cockpit_nearest` |
| `0x429820` | 2 | n/p | getter `[0x4cf99c]` | no | guest call (leaf) |
| `0x432bc8` | 417 | 1000×5/937 | HUD key handler (codes 0, 1, 2–0xb, 0xf, 0x13, 0x1c, 0x27, 0x2b, 0x3b–0x44, 0x57, 0x220 and a default through `0x446c38`): the compare chain at `0x432c32`/`0x432c3b` gives code `0x13` (R → `0x41b468`) and `0x1c` (Enter → target cycling); also `0x40e4ac`; full key table in [hud.md](hud.md) | mixed | hook `hud_key` |
| `0x42a3f4` | 58 | 0/0/0/19/1/1 | display-state selector (acts only when `+0x18` = −1; table of 7) | no | guest call (presentation) |
| `0x446e3c` | 17 | 0 | HUD panel mode cycle | no | guest call (presentation) |
| `0x410f78` | 109 | 1000×5/937 | weapon-system keys: `0xc`/`0xd` and Num−/Num+ → selected weapon's virtual `+0x38`; W `0x11` / Alt+W `0x211` → `0x41074c(W, ±1)`, `W+0x18` ← 0; Z `0x2c` → `0x410d04`; B `0x30` → fire mode `W+0x1c` = (+1) mod 3, `0x410ae4`, `W+0x16` ← 1; Alt+1…Alt+0 → `0x4110ac(W, n)` | yes | hook `weapon_keys` |
| virtual P`+0x2c` | | | key method (`0x4157c8`, 225; cheats in launcher.md) | yes | hook `vehicle_keys` (vehicle vtable) |
| virtual P`+0x38`, `+0x3c` | | | speed (`0x415498`), twist (`0x415488`, word `P+0x298`) | no | hooks `mech_target_scale`, `mech_angle` |
| virtual `[H+0x1e1]+0xc` | | | cockpit sub-object message | no (hypothesis) | guest call (presentation) |

Below the direct callees the reach contains further hooks (28 in all):
`vehicle_control`, `track_seek`, `approach_word`, `movement_step`,
`multiply_q8`, `euler_matrix`, `transform_point`, `terrain_height` and others.
Never own code in this subtree: `0x49681a` (RTL), `0x462af0` below the servo
sounds (audio), the dialogs `0x455950`/`0x4561dc`/`0x45cff0` and WinHelp
`0x4669c4`.

### Open

- Meaning of byte `P+0x39` (gates all movement input), of the action-1
  camera modifier `R+0xd` and of the device byte `[0x477904()+4]`.
- The values the live path writes into `R+0x16..+0x1d` (the 0/1 assumption
  decides whether two pressed buttons can act in one frame).
- Which button of the P dialog returns 1; whether `0x4d2564` and `0x4d2577`
  are written anywhere else through a base register.
- `0x45cff0` (F12/Alt+P) and the HUD sub-object `[[0x49b088]+0x1e1]`.
- A count-mode run would separate the callers of the shared callees
  (`0x41a550`, `0x41ef14`, `0x41b728`, `0x45b734`).

## Fire, aim point and legs

Four children of `0x460870` run as hooks in the adapter group `input` (core
`core/src/input.c`, adapter `tools/reference-game/proxy/input_hooks.h`, in
vitro `tools/reference-game/invitro/input_cases.h`; hook table in
[hooks.md](../reference-game/hooks.md#registry)). Read from the listing and
checked against the original in vitro; roles are hypotheses as in the
dispatcher section.

| Hook | VA | Callers | Map probes (a60/c1s/c1c/c3_09/c4_08/c3_10) |
|---|---|---|---|
| `fire_selected` | 410dbc | 415608 only | 1000×5/729 |
| `fire_line` | 415608 | 460870 only (460cf2) | 1000×5/729 |
| `aim_point` | 41b728 | 460870 (460c82), 41b130 (41b43b) | 0/1000/0/1000/493/0 |
| `leg_input` | 4160dc | 460870 only (460bef, 460c2f) | 1000/1000/1000/1000/0/937 |

No other reference to the four entries exists in the image (a byte search
for their little-endian VAs finds nothing, so no vtable or pushed address).
None of the four has a jump table; the map's instruction counts are complete
for them. `0x415608` runs in the cockpit view only; `0x41b728` from the
dispatcher only on the tracking path (`W+0x14` set, no twist/pitch input, a
live target). `0x41b130`, the second caller of `0x41b728`, is reached from
the rendering tree `0x45fca8`.

### fire_selected 410dbc

cdecl `(W, R, P)`: W = weapon system `[P+202h]`, R = input record, P =
vehicle. Frame of 8 bytes; the result dword EBP-4 and the flag word EBP-6
are written before they are read; the word EBP-8 is only popped.

1. b = byte W+1Dh (selection). If s8(b) + 1 ≤ 0 (no selection), return 0
   (EAX = 0, EDX = s8(b) + 1).
2. S = list[s8(b)] with list = [W+4]. F = 1 if word `4a9e52` = 0 and word
   `4a9e58` = 1, else 0 (the two words `group_fire` tests too, here
   without its O+A3h test). If byte S+4Bh ≠ 0 the partner is
   L = list[s16(s8 [[S+18h]+16h] + s8 b)] (16-bit sum), else L = 0. All of
   this is read before the first call.
3. Guards, each returning 0 when the result's AX is 0: S.V2C(S), L.V2C(L)
   if L, S.V30(S, R), L.V30(L, R) if L.
4. S.V28(S, P, x) with x = F in the low word over the high word of the
   preceding call's EAX; result := 1. S.V2C(S) = 0 → byte W+18h ← 0.
   S.V60(S, 0) with AX = 3 → words `4d2528` and `4d2526` ← 1 (CX ← 1).
5. If L: L.V28(L, P, x) (x = F over the V60 result's high word), then
   S.V2C(S) = 0 → byte W+18h ← 0.
6. Return EAX = result; ECX equals the result too (the epilogue pops the
   result local into ECX); EDX is the last callee's.

The vtable is read from the receiver at each call. Entry registers of the
virtual calls (all reproduced): V2C(S) EAX = S's vtable; V2C(L) EDX = L's
vtable, ECX = S; V30(S) ECX = R; V30(L) EDX = R, ECX = L's vtable; V28(S)
EDX = P, ECX = S's vtable; V60 EDX = S's vtable; V28(L) EAX = L's vtable,
EDX = P. V28 are the emitters of [weapons.md](weapons.md) (`weapon_fire`,
`emit`, `alt_emit` or a no-op), V2C/V30 readiness tests, V60 a mode query
(role open).

### fire_line 415608

cdecl `(P, R)`, frame 20h; all locals are written before they are read.

1. EAX = 410dbc(W = [P+202h], R, P) (EAX = R, EDX = W at the call). If the
   whole EAX is 0: count `4a9cc8` ← 0, return EAX = 0 with 410dbc's EDX.
2. T = [P+1A4h] (read after the call). T ≠ 0: n = `dist_gate`(P+26h,
   T+26h) >> 12 (SAR), stored, then clamped to 40 (JLE) and to 1 (JGE),
   each test rereading the stored dword. T = 0: n = 20.
3. The running point starts at P's x, y (dwords P+26h, P+2Ah, copied
   **before** the next call). a = word P+10h (heading, read after the call)
   minus the low word of P.V3C(P) (twist; EAX = P's vtable, EDX = x,
   ECX = y at the call).
4. 47eb30(a, m) builds four words cos a, sin a, −sin a, cos a: the table
   index is (a + 8) >> 4 (SAR, then ADC of the bit shifted out), folded at
   400h into the quarter sine table at `4a25c4` (negated above the fold);
   sin a = cos(a − 4000h). 47ffe8((0, 1000h), m, step) gives
   step = ((0·m0 + 1000h·m2 + 2000h) >> 14, (0·m1 + 1000h·m3 + 2000h) >> 14)
   on 64-bit sums. Both helpers are pure, read only their arguments and the
   table, leave no live register and are computed in core.
5. For i = 0 … n−1: x += step.x, y += step.y, point i ← (x, y) at
   `4a9b88 + 8i`. Return EAX = n, EDX = the last x (ECX = the last y). The
   40th point ends exactly at the count dword `4a9cc8`.

### aim_point 41b728

cdecl `(P, out, p3, p4)`. Its `push ecx` reserves the local EBP-4, which
holds the entry ECX unless the Euler path stores B there; it is popped
into ECX at the return.

1. If dword P+30Bh ≠ 0: d = `dist_gate`(P+26h, [P+1A4h]+26h); if d < 30,000
   (signed): 40e4dc([P+30Bh], [P+1A4h], out, [P+20Eh], p3, p4) with EAX =
   [P+20Eh], EDX = [P+1A4h], ECX = [P+30Bh] (all reread after `dist_gate`);
   return its EDX:EAX; *p3 is not touched.
2. Otherwise T = [P+1A4h]; node = T.V24(T) (EAX = T, EDX = T's vtable).
   - node ≠ 0: B = [P+1A4h] + 0Ch (reread). If word B+26h (T+32h) = 0:
     `euler_matrix`(B, B+6) (angles T+0Ch, matrix T+12h; EAX = ECX = B),
     then word T+32h ← 1. `transform_point`(node+14h, T+12h, out).
   - node = 0: out ← the 12 bytes at [P+1A4h]+26h, copied in the order
     +8, +0, +4 (EAX = y, EDX = z).
3. p3 ≠ 0: word *p3 ← 0, EDX = p3. Return EDX:EAX.

### leg_input 4160dc

cdecl `(P, turn, throttle)`; turn (EDI) and throttle (ESI) keep their high
words, every NEG and load touches only the low word. Frame 0Ch, written
before it is read. Globals: view word `4d24f0`, throttle lever word
`49a06e` (reread at each test).

1. Lever: if view = 0 and lever ≠ 0: v = |s16(throttle) − 256|·2; v < 100
   → 0; lever < 0 → −v. Word P+290h (speed set point) ← v, byte P+93h ← 1,
   throttle ← 0 (the whole ESI).
2. lever < 0 → the throttle word is negated. m = |throttle| as a word
   (8000h gives 7FFFh). The turn word is negated unless (m > 60 and
   throttle > 0) or (m ≤ 60 and set point < 0), all signed 16-bit.
3. q = `multiply_q8`(91h, −s16(throttle)). If q's AX ≠ 0: byte P+93h ← 1;
   s = AX + set point (16-bit); a nonzero old set point whose sign differs
   from s's sign zeroes the set point; otherwise s is clamped to [lo, hi]
   with hi = 400h (0 when lever < 0) and lo = −400h (0 when lever > 0) and
   stored.
4. k = −word [[P+1F2h]+4] for a negative set point, else word
   [[P+1F2h]+6]; then `vehicle_control`(P, turn, `multiply_q10`(k, set
   point), 0), whose EDX:EAX is returned.

Every register at the three calls is reproduced, including the cases where
the high words come from the lever path (EAX = P+92h, EDX = the sign of
s16(throttle) − 256, ECX = that difference) or from the entry registers.

### Registers at the callers and stack residue

The hooks return EDX:EAX exactly; ECX cannot be returned. Forward liveness
walks with byte lanes over the full listing:

- ECX after 415608 and after 410dbc is dead (410dbc's ECX reaches only
  `dist_gate`'s entry, which writes ECX before reading it).
- ECX after 41b728 reaches only the upper word pushed at 41b44a, an
  argument that 434a24 reads as a word; ECX after 4160dc reaches only word
  argument slots (41a550's rate, 467924) and the `push ecx` reservation of
  41e8d4. EAX and EDX of 41b728 and 415608 are dead at their callers too
  (41ef14 does not read them; at 460cf7 the memset call follows).
- A nested `transform_point` in compare mode is an EAX-only hook, so
  aim_point's EDX on the node path is then the dispatcher's stack address
  (dead, see above); the in-vitro real-callee pass allows exactly that.

The weapon virtuals called from 410dbc read uninitialized stack below their
entry (emitter residue, [weapons.md](weapons.md)); with 410dbc or 415608
replaced they are entered from the guest bridge at another depth, so raw
residue parity is not claimed, the same as for `group_fire`.

### Unreachable edges

None: all 42 conditional branches (14 + 5 + 5 + 18) are covered both ways
in vitro.

### Open

- `40e4dc` (aim through the weapon `[P+30Bh]`) is entered only in
  `c3_10-hunt-targ`, none of the six map suites; its contract is in
  [The targeting computer 40e4dc and 40e4ac](#the-targeting-computer-40e4dc-and-40e4ac).
- The roles of the weapon V60 and of the words `4d2526`/`4d2528` that its
  value 3 sets.

## Torso servos and weapon keys

Four functions run as hooks in the adapter group `torso` (core
`core/src/torso.c`, adapter `tools/reference-game/proxy/torso_hooks.h`, in
vitro `tools/reference-game/invitro/torso_cases.h`; hook table in
[hooks.md](../reference-game/hooks.md#registry)). Read from the listing and
checked against the original in vitro; roles are hypotheses.

| Hook | VA | Callers (direct) | Map probes (a60/c1s/c1c/c3_09/c4_08/c3_10) |
|---|---|---|---|
| `torso_twist` | 41a550 | 41e8d4 (41e962), 41ef14 (41f306), 460870 (460c01, 460cc8) | 1000 in all six |
| `torso_pitch` | 41a808 | 41e8d4 (41e974), 41ef14 (41f342), 460870 (460c17, 460cde) | 1000 in all six |
| `weapon_converge` | 41a74c | 41a808 (41a968), 4198f4 (41a34d) | 1000 in all six |
| `weapon_keys` | 410f78 | 4157c8 (4157e4), 460870 (461005, 46104a, 461066) | 1000×5/937 |

No other reference to the four entries exists in the image (byte search for
their little-endian VAs). None has a jump table. The map lists only 4157c8
as a caller of 410f78 because its walk does not follow 460870's jump table;
4198f4 (reached only through a vtable, entered only in c4_08) is the second
caller of 41a74c.

**Callees through the guest bridge.** The servo sound latches `0x41a6d0` and
`0x41a994` (hooks `twist_sound_latch`, `pitch_sound_latch`) only ask the SOS
layer whether sound row 0x21 is playing (`0x462af0`, `sosDIGISampleDone`)
and write the latch bytes `0x49a1c9`/`0x49a1cc` and words
`0x49a1ca`/`0x49a1ce`, which nothing else reads ([audio.md](audio.md)). The
weapon mount helper `0x40f540` (hook `mount_offset`; six callers; it calls
`0x40f904`, hook `mount_base`, and adds the definition offsets in place) and
the weapon-system routines below 410f78 (`0x410d04` `weapons_v3c`,
`0x410ae4` `fire_group`, `0x41074c` `select_step`, `0x4110ac`
`group_toggle`, the weapon virtual `+0x38`) are hooks of other groups
([weapons.md](weapons.md#weapon-system-routines-below-the-weapon-keys)). All
four torso hooks refuse compare mode ([compare_mode.md](../reference-game/compare_mode.md)).

### torso_twist 41a550

cdecl `(P, rate, target, snap)`; only the low words of `rate`, `target` and
`snap` are read. Frame 0Ch; the locals EBP-4 (scale), EBP-6 (rate before
the update, or the new rate on the direct path), EBP-8 (twist before) and
EBP-0Ah (mean rate) are written before they are read; EBP-0Ch is unused.
EBX = P, EDI = C = `[P+1F2h] + 2` (read once at entry), ESI the scaled rate.

1. q = `multiply_q8`(s16 rate, s16 C+1Ch) (47e0a0; entry EAX = EDX =
   s16 C+1Ch, ECX = the caller's). ESI = q (the whole EAX). After the call:
   CX = word P+294h (current rate; ECX keeps 47e0a0's high word), stored at
   EBP-6; AX = word P+298h (twist), stored at EBP-8.
2. |SI| and |P+294h| as words (8000h gives 7FFFh in DX/CX over the high
   words of EDX/ECX; otherwise ESI/EAX copied whole before NEG DX/CX).
   If |SI| ≤ |rate| (signed words): EBP-6 ← SI, word P+294h ← SI.
   Otherwise: s = `movement_step`(EAX with AX = word C+1Eh) (467924; the
   slot is the whole EAX, 467924 reads its low word), POP ECX reloads that
   slot, then `approach_word`(P+294h, ESI, s) (467adc; EAX = s,
   EDX = P+294h).
3. EAX = (s16 EBP-6 + s16 P+294h) SAR 1 on 32 bits (the low word is stored
   at EBP-0Ah), EDX = s16 P+294h, SI = word P+298h, CX = the mean;
   `movement_step`(ECX) (467924, slot = whole ECX), SI += AX, POP ECX.
4. Clamp: DX = −word C+20h, AX = word C+20h (over 467924's high words).
   If s16 AX ≤ s16 SI → EAX; else if s16 DX < s16 SI → ESI; else EDX. Word
   P+298h ← the low word of the chosen register (now in EAX).
5. Snap (word `snap` ≠ 0): a = word P+298h − target, b = EBP-8 − target
   (16 bit, in AX and DX). The servo stops at the target (word P+298h ←
   target, word P+294h ← 0, CX = target) when (a < 0 and b > 0) or
   (a ≥ 0 and b < 0) or (a = 0 and b > 0), all signed words.
6. EAX = `[P+230h]`; if nonzero: `track_seek`(EAX, ECX with CX = word
   C+1Ah, u16 P+298h SAR 2) (479344; EDX = that fraction).
7. EAX = P+92h. If byte P+0A3h ≠ 0: `0x41a6d0`(P, EDX with DX = rate)
   (sound latch; EAX = P+92h). 41a6d0 reads its rate slot as a whole dword
   and may return it in EAX (`NEG AX` keeps the slot's high word), so the
   slot carries EDX's high word exactly.

Result: EDX:EAX = P+92h : EDX at that point, or 41a6d0's pair.

### torso_pitch 41a808

cdecl `(P, rate, d, target, snap)`; the same scheme on the pitch: scale
C+24h, step C+26h, rate word P+296h, pitch word P+29Ah, track `[P+234h]`
with clip word C+22h. Differences:

- Clamp (step 4): AX = word C+28h (upper bound), DX = word C+2Ah (lower
  bound, **not** negated; loaded between the CMP and the JLE); the same
  register choice. The two bounds are independent words, so with a lower
  bound above the upper bound the upper bound wins whenever it is ≤ SI.
- After the track call: `weapon_converge`(P, d) (41a74c; EAX = d, the
  whole dword, EDX/ECX as left), then EAX = P+92h and, for byte P+0A3h ≠ 0,
  `0x41a994`(P, EDX with DX = rate) (sound latch).

Result: EDX:EAX = P+92h : EDX, or 41a994's pair.

### weapon_converge 41a74c

cdecl `(P, d)`; d is a dword (target distance or 0). Frame 24h (the
adapter's scratch). EDI = P, EBP-4 = C = `[P+1F2h] + 2` (EAX = C before the
loop), EBX = i = 0.

For i while s16 i < s16 word `[W+8]` (W = `[P+202h]`, reread at every
test; EDX = W after it): EAX = s16 i, ESI = S = dword `[[W+4] + 4·i]` (W
and the list reread). S = 0 → next. Otherwise:

- d ≠ 0: B = (0, d, 0) at EBP-18h, -14h, -10h. v = `0x40f540`(S)
  (EAX = d, EDX = 0, ECX = 0) returns a pointer to three words; POP ECX.
  A = (s16 v+0, s16 v+2, s16 v+4 − s16 C+64h) at EBP-24h, -20h, -1Ch,
  written in the order z (−1Ch), x (−24h), y (−20h), then the subtraction
  at −1Ch (C reloaded from EBP-4). `aim_angles`(EBP-0Ch, B, A) (492990;
  EAX = &B, EDX = out, ECX = &A): three angle words out.
- d = 0: ECX = 0, the three out words EBP-8, -0Ah, -0Ch ← 0.
- EAX = dword EBP-0Ch → dword S+24h; AX = word EBP-8 → word S+28h.

Result: EAX = C when the loop does not run, else the last iteration's
(s16 i for a null slot, otherwise the dword out+0 with the word out+4 in
AX); EDX = W from the last loop test. Nothing reads a local before it is
written (aim_angles writes all six out bytes). Role: every weapon of the
vehicle gets the angles from its mount point to the point d ahead on the
torso axis (convergence; hypothesis).

### weapon_keys 410f78

cdecl `(W, code)`; W = weapon system `[P+202h]`. EBX = W, EAX = the whole
code dword, SI ← 1 (**ESI's high word is the caller's**), EDX = u16 code.
Dispatch by a compare chain (no table); EDX keeps the chain's remainder:

| Code | Key | Effect |
|---|---|---|
| `0x0c`, `0x0d` | `-`, `=` | if s8 W+1Dh ≥ 0: S = `[[W+4] + 4·s8 W+1Dh]`, S.V38(S, code = 0x0d) (EAX = S's vtable, EDX = [W+4], ECX = S); else EDX = the selection byte over 0 |
| `0x4a`, `0x4e` | Num−, Num+ | the same with the flag (code = 0x4e) |
| `0x11` | W | `0x41074c`(W, 1), byte W+18h ← 0 (EAX = W+14h) |
| `0x211` | Alt+W | `0x41074c`(W, −1), byte W+18h ← 0 |
| `0x2c` | Z | `0x410d04`(W); POP ECX = W |
| `0x30` | B | byte W+1Ch += 1, reread; s8 > 2 → 0; `0x410ae4`(W, s8 W+1Ch) (EAX = that value, EDX = 0, ECX = the incremented byte sign-extended); byte W+16h ← 1 |
| `0x202`…`0x20b` | Alt+1…Alt+0 | `0x4110ac`(W, EAX with AX = code − 202h) (EDX = code − 20Ch) |
| other | | none |

Result: EAX = (caller's ESI & FFFF0000h) | 1 for a handled code (also when
`-`/`=`/Num± find no selection), 0 otherwise; EDX = the last callee's or
the chain's remainder. The callees' entry registers: EAX = code (with the
low word code − 202h for Alt+n), EDX = the chain's remainder (0 for an
exact match), ECX = the caller's (for B the sign-extended byte).

**Entry shim.** The result's high word is the caller's ESI, which no
replacement sees. 410f78 overwrites EAX and EDX before reading them, so the
shim `torso_keys_entry` copies ESI into EAX and jumps into the dispatch;
the replacement reads the caller's ESI from its entry EAX, and the entry
ECX stays the caller's (it reaches the callees). Chained from the
`default:` of `projectile_capture_entry` (`torso_capture_entry`).

### weapon_keys calls with the original's ESI

What the callees of 410f78 see in ESI (checked in vitro, `torso_cases.h`).

**ESI at the calls.** 410f78 saves ESI (410f7c), sets SI ← 1 at 410f83
before the compare chain and writes ESI again only at 4110a4 (XOR, the
"other" codes, which call nothing). So every call of 410f78 (410d04,
410ae4, 41074c ×2, 4110ac, the selected weapon's +38h) runs with
ESI = (caller's ESI & FFFF0000h) | 1. EBX = W at all of them.

**Who reads it.** 41074c reads ESI's high word: SI ← word W+8 at 41075f,
then `lea eax,[esi-1]` at 410775 when the 16-bit step goes below 0
([weapons.md](weapons.md#weapon-system-routines-below-the-weapon-keys)); the
whole EAX goes as the index slot to 410708, which returns it in EDX (low
word adjusted only for a negative definition byte), and 41074c and 410f78
leave EDX alone after that call. The other callees do not read ESI or EBX
before writing them: 410ae4 writes EBX (410ae9) and ESI (410b02) first;
410d04 touches neither, and its weapon virtual +3Ch is 4111d3 (empty) or
40f4d8 (reads only its slot); 4110ac writes EBX/ESI (4110bb, 4110be)
before 410cd0 and its virtuals; the weapon virtual +38h is 4111ce (empty)
or 40f48c (reads its two slots, EDX's high word and ECX's). The tables
were read at +38h/+3Ch of the weapon vtables 498a84, 498afc, 498b74,
498c64, 498cdc, 498d54, 498dcc, 498e44, 499248 and 4992c0 (weapons.md).

**The adapter.** Through the plain guest bridge, whose ESI is the bridge's
own, EDX after Alt+W at slot 0 (and on any step that wraps below 0) would
carry the wrong high word, with 41074c original or replaced (`select_step`
takes ESI from its entry shim, whatever the caller left there).
`torso_weapon_keys` therefore builds that ESI from the caller's ESI (its
shim's EAX slot) and calls through `weapon_system_guest_call`, the
ESI-loading bridge of `fire_group`; EBX stays the bridge's, which no callee
reads. EDX after 410f78 is dead behind 4157c8, so this makes the replaced
call exact rather than only equivalent.

### Registers at the callers

The hooks return EDX:EAX exactly; ECX after a replaced hook is its entry
ECX. ECX walks over the full listing:

- After 41a550: the first read is the `push ecx` at 41a8d5 (the mean-rate
  slot of 467924 in 41a808, high word only; 467924 reads the low word).
- After 41a808 and 41a74c: reads only at `push ecx` slots read as words
  (41a61d, 41a8d5), the `push ecx` reservation of 41e8d4 (41e8d7, written
  before it is read, [ai.md](ai.md)), the indirect call `+0x60` at 41f702
  and the returns of the vtable functions 41d9cc, 41d2c4, 41c9cc (AI
  behaviours) and 4198f4, which have no direct callers. Not followed
  further.
- After 410f78: escapes only through the return of the key method 4157c8
  (vtable); after its two virtual call sites in 45feb8 (460841, 460854)
  ECX is dead.

### Stack residue

No local of the four is read before it is written. The callees (41a6d0,
41a994, 40f540 → 40f904, and the weapon routines) are entered from the
guest bridge at another depth when a hook is replaced; the servo latches
and 40f540 read no uninitialized stack (40f904 and the weapon routines
below 410f78 were not checked for it), so residue parity below them is not
claimed, as for `fire_selected`.

### Unreachable edges

None: every conditional branch of the four (14 + 14 + 3 + 12 = 43 sites) is
covered both ways in vitro.

## Torso helpers and the input dispatcher

Three functions run as hooks in the adapter group `input_dispatch` (core
`core/src/input_dispatch.c`, adapter
`tools/reference-game/proxy/input_dispatch_hooks.h`, in vitro
`tools/reference-game/invitro/input_dispatch_cases.h`; hook table in
[hooks.md](../reference-game/hooks.md#registry)). Read from the listing and
checked against the original in vitro; roles are hypotheses.

| Hook | VA | Callers (direct) | Map probes (a60/c1s/c1c/c3_09/c4_08/c3_10) |
|---|---|---|---|
| `torso_center` | 41e8d4 | 41d598 (41d5e1), 41dd64 (41de8a), 41e224 (41e401, 41e477), 41e984 (41e9db), 460870 (460cb3) | 1000 in all six |
| `torso_aim` | 41ef14 | 41f5a0 (41f6f3, 41f780), 460870 (460c8f) | 518/1000/1000/1000/1000/383 |
| `player_input` | 460870 | 45f570 (45f838) only | 1000×5/937 |

No other reference to the three entries exists in the image (byte search
for their little-endian VAs). 41dd64 and 41e224 are reached through a
vtable (their VAs occur once each in the data). Only 460870 has a jump
table.

**Callees through the guest bridge.** Not hooks: the HUD functions
`0x432b14`, `0x446e3c`, `0x42a3f4` and the getter `0x429820`, the platform
helpers `0x45c524`, `0x477904`, `0x45b734` and the `memset` thunk
`0x49681a`, the cockpit sub-object virtual `[[0x49b088]+0x1e1]+0x0c` and,
under 41ef14, the model virtual `[[P+0x34]+4]+0x20`. Every other callee is
a hook called through the bridge as well, so it keeps its own mode:
`command_key`, `hud_key`, `cockpit_next`, `cockpit_nearest`,
`outside_camera`, `vehicle_keys` and `mech_angle`/`mech_target_scale` (the
vehicle virtuals `+0x2c`, `+0x3c`, `+0x38`), `dist_gate`, `multiply_q10`,
`multiply_q8`, `leg_input`, `flyer_input`, `torso_twist`, `torso_pitch`,
`aim_point`, `fire_line`, `weapon_keys`, `euler_matrix`,
`matrix_transpose`, `rotate_point`, `transform_point`, `aim_angles`,
`dist3d_delta`, and the two torso helpers under 460870.

### torso_center 41e8d4

cdecl `(P, d)`. Prologue `55 8b ec 51 53 56 57`: the `push ecx` reserves
EBP-4, which receives the pitch word before it is read and is popped into
ECX at the return. EBX = P.

1. u = `multiply_q10`(0FAh, s16 P+298h) (entry EAX/EDX/ECX = the caller's).
   Clamp of the whole EAX to −100h…100h (signed dwords: 100h, FFFFFF00h
   or u itself), then NEG AX (the high word stays): the twist rate.
2. EDX = s16 P+29Ah, stored at EBP-4 and reloaded into ECX;
   v = `multiply_q10`(0FAh, that dword) (EAX = the twist rate, EDX = ECX =
   s16 pitch). The same clamp and NEG AX give the pitch rate.
3. `torso_twist`(P, twist rate, 0, 1) (EAX = the pitch rate, EDX/ECX as
   multiply_q10 left them), `torso_pitch`(P, pitch rate, d, 0, 1) (EDX = d).

Result: EDX:EAX = 41a808's pair; ECX = s16 P+29Ah (not returnable by a
hook, see the ECX walks below).

### torso_aim 41ef14

cdecl `(P, point)`, frame 0ACh, no exception frame, no x87. ESI = P,
EBX = EBP-74h (matrix A), EDI = EBP-94h (matrix B). Statics: the three
angle words `0x4a9cfc`, `0x4a9cfe`, `0x4a9d00` (written by aim_angles) and
their init guard byte `0x49a340` (set to 1 when 0; nothing else depends on
it).

1. The start point EBP-54h..-4Dh ← (0, 0, 0). B = P+0Ch. If word P+32h = 0:
   `euler_matrix`(B, B+6) (EAX = B+6, EDX = ECX = B), then word P+32h ← 1.
2. A ← the 20h-byte block P+12h..P+31h (nine matrix words, two padding
   bytes, the position P+26h/2Ah/2Eh); the copy leaves ECX = [P+26h],
   EDX = [P+2Ah], EAX = [P+2Eh].
3. M = `[P+34h]`, O = `[M+4]`: n = O.V20(O, slot) with the slot = EAX with
   AX = word `[P+1F2h]+0Ch` (high word that of P+2Eh; EAX = O, EDX = O's
   vtable, ECX = M). If n ≠ 0 and s16 word n+4 > −1, B ← the block at
   `[M+16h]` + 20h·s16(n+4), else the block at `0x6c56a8`; the copy leaves
   EDX, ECX, EAX = the dwords at +14h, +18h, +1Ch.
4. For A, then B: `matrix_transpose`(M), POP ECX (= M), negate the three
   translation dwords (+1Ch, +14h, then +18h through EAX), copy them to
   t = EBP-0A0h (A) or EBP-0ACh (B), `rotate_point`(t, M, M+14h)
   (EAX = +1Ch, EDX = M+14h, ECX = t): the inverse transform of each.
5. `transform_point`(point, A, EBP-48h), `transform_point`(EBP-48h, B,
   EBP-3Ch): the point in the torso (node) frame (x, y, z at EBP-3Ch,
   -38h, -34h). z −= s16 word `[P+1F2h]+66h` (ECX).
6. `aim_angles`(0x4a9cfc, EBP-3Ch, EBP-54h) (EAX = EBP-54h, EDX = EBP-3Ch).
7. EBX = `multiply_q10`(0FAh, s16 4A9D00h), EDI = `multiply_q10`(0FAh, s16
   4A9CFCh) (whole EAX each). |4A9CFCh| as a word into DX (8000h gives
   7FFFh over EDX's high word, otherwise EAX copied whole and its low word
   negated), stored at EBP-0Eh; |4A9D00h| the same into CX, EAX = ECX.
8. Far dead zones (y = dword EBP-38h, reread, signed):
   - y > 50000 and |4A9D00h| < 1000: below 50 EBX ← 0, otherwise EBX ←
     `multiply_q10`(2BCh, s16 BX) (EAX = EDX = s16 BX).
   - y > 50000 and |4A9CFCh| < 1000: below 55h EDI ← 0, otherwise EDI ←
     `multiply_q10`(2BCh, s16 DI).
9. EDX = clamp(s16 BX) to ±100h with NEG DX → EBX; ECX = clamp(s16 DI) →
   EDI. The squared rates keep the sign: EBX = `multiply_q8`(s16 |BX|,
   s16 BX), EDI = `multiply_q8`(s16 |DI|, s16 DI) (|w| as in step 7, into
   AX and CX; EAX = the second argument, EDX = the first).
10. `torso_twist`(P, EBX, ECX with CX = word P+298h − word 4A9D00h, 1);
    then the target AX = word P+29Ah + word 4A9CFCh (over twist's EAX), both
    statics reread after the call. d = `dist3d_delta`(x, y, z) (reread
    from the frame; EAX = y, EDX = ECX = x). `torso_pitch`(P, EDI, d,
    target, 1).

Result: EAX = 0x4a9cfc, EDX = 41a808's EDX (ECX 41a808's).

### player_input 460870

The contract of [the input dispatcher](#the-input-dispatcher-0x460870); here
the register flow the core reproduces and the jump table.

- **Registers at the calls.** EBX = P (`[0x4d24e6]` read once at entry)
  and ESI = R until the button loop, then ESI = i and EDI = R+16h+i; EBX =
  i before 45b734 and then (i + byte `0x4d2577` + 4) in BL. The caller's
  EDI reaches the first two calls (45feb8, 432b14; `xor edi,edi` at 4608d1
  follows them); then EDI holds the twist input word (high word 0) until
  the align path, where it becomes the whole second `multiply_q10` result.
  EAX/EDX/ECX are followed exactly, including the slot high words (the
  command slot is EAX with AX = word R+0; the 401c74 and leg_input slots
  carry the high words of the registers they were built in).
- **POP ECX** after the one-argument calls (45feb8, 45c524, the virtuals
  +3Ch/+38h, 43349c, 4333c8, 446e3c) reloads the argument slot; the bridge
  does not copy slots back, so a callee that rewrote its argument would
  give another ECX (not checked for these callees; the stubs in vitro do
  not rewrite slots).
- **Drag products.** The twist product is the one-operand IMUL (EDX = the
  high half of the 64-bit square unless the value is negated), the pitch
  product the two-operand IMUL (low 32 bits); both SAR as in the execution
  order above. The align squares likewise: u² by one-operand IMUL (EDX =
  high half), v² by two-operand IMUL.
- **Button loop.** The byte at R+16h+i is read twice (test, then AL); the
  21-byte seen table at EBP-8Ch is cleared through 49681a(EBP-8Ch, 0, 15h)
  before the loop. Action index = the byte (i + `[0x4d2577]` + 4), table
  byte `0x4d1f38`[index], `ja` above 20.
- **Jump table** `0x460d83` (21 dwords, read from the image): 0 and 1
  → 461097 (next button), then 460ddc, 460deb, 460e24, 460e39, 460e81,
  461070, 460e8e, 460e9d, 460eb2, 460ec8, 460ef7, 460f56, 460f71, 460f88,
  460ff3, 461012, 461022, 46103b, 461054. After the table, 460dd7 is an
  unreachable `jmp 461097`. Case registers (all reproduced): action 3's
  twist virtual gets EAX = vtable, ECX = the player; action 12 decrements
  the mode (DEC/DEC/DEC/SUB 2, `jb`) so modes 1, 3 and 4 reach 460f18 with
  EAX = 0 or mode − 5; action 15 decrements ECX to 0 on a match (mode 2
  calls 42a3f4 with 2 and falls through to state 4); action 7 leaves
  EAX = P+92h and EDX = the player.
- **Result.** AX = word EBP-2 over the EAX that the last writer left (often
  a callee's result in the loop); EDX the last writer's. The caller tests
  AX only; on a nonzero AX `mov ax,1` keeps the high word and the logic
  frame returns it.

**Callee-saved registers and the original frame.** 410f78 (`weapon_keys`)
returns the caller's ESI high word in EAX, and the key method 4157c8 behind
the virtual +2Ch calls 410f78 without setting ESI; both are reached from
the button loop with ESI = i. The `player_input` adapter therefore loads
EBX, ESI and EDI at every game call from a three-dword block that the core
writes before the call (`es2_input_saved`). Installed, the replacement runs
at the original's stack coordinate through an entry shim, so the callees
see the original frame, the caller's EDI and the original's stack residue
([Stack residue under the command handler](#stack-residue-under-the-command-handler)).
The two torso helpers use the plain bridge (their callees were not seen to
read the caller's callee-saved registers).

### Registers at the callers and stack residue

The hooks return EDX:EAX exactly; ECX after a replaced hook is its entry
ECX.

- After 41e8d4 (original: s16 P+29Ah): 41d5e1 → return of 41d598, whose
  three callers pop ECX; 41de8a, 41e477 → returns of the vtable behaviours
  41dd64, 41e224; 41e401 → 41c3c8, which uses only CX (MOV CX,20h /
  MOVSX ECX,SI) and pops its own reservation; 41e9db → return of 41e984 →
  41eaaa → return of 41ea7c; 460cb3 → 415608 or the memset call (ECX dead
  there). Not followed further.
- After 41ef14 (original: 41a808's ECX): 41f6f3 → the indirect call +60h
  at 41f702; 41f780 → return of 41f5a0, whose callers 41ea6c and 41eb23
  return at once; 460c8f → 460ca4 → the same as after 460cb3.
- After 460870: the caller pops ECX at once (45f83d).

No local of the three is read before it is written, with one exception
outside the six map suites: 41ef14 reads the 12-byte point that 41b728 wrote
into 460870's frame (EBP-74h); on 41b728's `40e4dc` path (entered only in
`c3_10-hunt-targ`) the writes of 40e4dc were not read, so an unwritten byte there
would be stack residue. Below the original callees residue parity holds
only where the replacement runs at the original coordinate; the one live
reader found is the group order `0x4231a4` below `0x45feb8`
([The residue reader](#the-residue-reader-the-group-order-0x4231a4)).

### Unreachable edges

41ef14 tests the clamped rates for 8000h (41f286, 41f2bf) after clamping
them to ±100h, so both `jne` always jump (proved in `branch_sites.h`).
Every other conditional branch of the three (4 + 20 + 50 = 74 sites) and
every jump-table entry is covered both ways in vitro.

### Open

- The `40e4dc` aim path (see the residue note) and the HUD sub-object.
- Whether an original callee rewrites its own argument slot before the
  POP ECX of 460870 (only ECX would differ).

## Keyboard message handler 477bec

The platform keyboard layer that feeds `0x460870`: it turns
`WM_KEYDOWN`/`WM_KEYUP` into the key codes that the logic frame reads from
the key ring. Everything in it except three kinds of calls is pure over its
arguments and image memory, so it is replaced as one hook, `key_message`
(group `keyboard`, core `core/src/keyboard.c`, in vitro `keyboard_cases.h`,
and the core unit test).

### Registration and caller

The keyboard device constructor `0x4779a8` (called once at `0x45c029`)
registers the handler through `0x477a4c` → `0x465fec` in the window
procedure's handler list `0x4d3b30` (ten slots); its destructor
`0x477a10` removes it through `0x477a68` → `0x466010`. The only call is
`call [ebx]` at `0x466056` in the window procedure `0x466034`, for every
message, with the four window-procedure arguments (stdcall, `ret 10h`).
A nonzero result makes the window procedure return 0 at once (no
`DefWindowProcA`); 0 goes on with the next handler and then the message
switch. After the call the window procedure reads only EAX: the next
handler call reloads EDX and ECX (`0x466049..0x466055`), after the last
slot EDX ← message (`0x46606c`), and the four key messages reach the
default case `0x46656c`, which loads ECX before it is used. EDX, ECX and
the flags after the handler are dead.

### Contract

`uint32 __stdcall 0x477bec(window, message, wParam, lParam)`; `window`
and `lParam` are never read. Confirmed.

1. **Messages.** ECX ← message − 100h − 2, `jb` (100h/101h); ECX −= 4,
   `jb` (104h/105h) (`0x477bf9..0x477c0c`). Any other message returns
   EAX = 0 with EDX = wParam and reads, writes and calls nothing. Key-up
   is 101h or 105h.
2. **Code.** wParam below DFh (unsigned) selects the dword `0x4a10ec +
   4·wParam`, otherwise the code is 0. 89 of the 223 shipped entries are
   nonzero, numbered like PC scan-code set 1 ([launcher.md](launcher.md));
   `VK_SHIFT`, `VK_CONTROL` and `VK_MENU` themselves are unmapped.
3. **Unmapped key** (code 0, `0x477d29`): one `GetKeyState(VK_MENU)`;
   EAX = 1 when bit 15 of AX is set, else 0 (an unmapped key with Alt
   down skips `DefWindowProcA`). EDX = what the call left.
4. **Modifiers.** `GetKeyState` for `VK_SHIFT` (10h), `VK_MENU` (12h),
   `VK_CONTROL` (11h), always all three and in this order; bit 15 of AX
   (`test ax,ax` / `jge`) adds 800h, 200h, 400h.
5. **Focus keys.** A code of exactly 20Fh (Alt+Tab), 401h (Ctrl+Esc) or
   201h (Alt+Esc), down or up, calls `0x4669b4` without arguments (it
   calls the display-mode switch `0x4667c8` while the exclusive flag
   `0x4d255e` is set) and returns EAX = 0; nothing is queued and no hot
   key is offered. Shift with these keys gives another code.
6. **Key-up** adds 80h.
7. **Hot keys** (`0x477ca7..0x477ce5`). Ten entries of 12 bytes at
   `0x6bb8b8`: active dword, pointer to a zero-terminated byte list,
   callback (cdecl, one dword). For each active entry in order the list is
   scanned for the code's low byte (80h included, modifiers not); on a
   match the callback gets the whole code. A result of exactly 1 consumes
   the key: EAX = 1, nothing queued. Any other result skips the rest of
   that list and goes on with the next entry. The entry, its list pointer
   and its callback are read from memory at each step, so a callback that
   changes the table affects the rest of the scan.
8. **Queue** (`0x477ce7..0x477d12`). n ← (dword `0x6bb8b0` + 1) & FFh; if
   n ≠ dword `0x6bb8b4`: the write index is read again and used
   **unmasked** as the slot, word `0x6bb6b0 + 2·index` ← low word of the
   code, `0x6bb8b0` ← n, and dword `[[0x6c5eb8]]` |= 2 (keys pending in
   the device). A full ring drops the key silently. Readers: `0x477ad4`
   (take one; clears bit 1 when the ring becomes empty; called from
   `0x45c0cf..0x45c123` and `0x471a01`) and `0x477abc` (flush; `0x461f5b`,
   `0x4719f8`).
9. **Result.** After the queue step (also when the ring was full) EAX = 1
   if the code's low byte is 44h (F10 down; F10 up gives C4h), else 0.
   EDX = the old write index when the code was queued, otherwise what the
   last call left, with DL = the last list byte read if a list was
   scanned after that call.
10. **ECX** is the original's popped frame slot: the entry ECX until
    `0x477cb3`, from there the entry counter of the hot-key loop (the
    index of the consuming entry, or 10). Dead at the caller.

No frame local is read before it is written. None of the callees reads an
entry register or stack residue: `GetKeyState` is Win32; `0x4669b4`
reads only `0x4d255e`, and `0x4667c8` saves EBX/ESI/EDI/EBP before
writing them and writes its locals before reading them; `0x45a324` writes
its local at `0x45a363` before `0x45a3b5` reads it; `0x45a498` and
`0x477a7c` have no locals.

### Registered hot keys

The table is written only by `0x477b18` and `0x477b7c` (identical
"register in the first free entry" functions); `0x477b50` and `0x477bb4`
(unregister) have no direct caller and their addresses occur nowhere in
the file. Registrations:

| Registrar | List | Callback | Role |
|---|---|---|---|
| `0x4779a8` (device constructor, `0x4779d5..0x4779fe`) | address of a zero byte in its own frame (`ebp−1`) | `0x477a7c` (returns 0) | never made: the constructor tests bit 0 of the device's flag dword right after the base object `0x480590` stored 0 there (`0x477a5e`), so this branch never runs ([Facts read on the way](#facts-read-on-the-way)) |
| `0x45a64b` | `0x49eabc` (78 codes) | `0x477a7c` | none |
| `0x45b6c7` (flight-key capture on, [display_modes.md](display_modes.md)) | `0x49eb0b` (30 codes: the flight keys and their key-up forms) | `0x45a324` | sets or clears the three 15-byte key-state arrays `0x4d2394`/`0x4d23a3`/`0x4d23b2` and the Alt word `0x4d23d6`; returns 1 (consumed) or 2 |
| `0x45b6da` | `0x49eb35` (Enter, `-`, `=`, `[`, `]`, Num+, Num−) | `0x45a498` | appends the code to the command-key queue `0x4d20c4` (index `0x49eaa0`, unbounded) while `0x49eaa4` is set; returns 1 or 2 |

### Input journal

The only Win32 state the handler reads is `GetKeyState`, through the
import thunk `0x49695e` (`jmp [0x6cc438]`). The proxy's input journal
([input_journal.md](../reference-game/input_journal.md),
`proxy/input_journal.h`) replaces that IAT slot. The replacement calls
the same thunk through the guest bridge, so the journal sees the same
calls in the same frame, order and arguments (three per mapped key
message, one per unmapped one), records the same entries, and in replay
consumes the same entries (`ij_take` matches frame and virtual key) or
counts the same fallbacks (`in_fallbacks` is compared between replays).
The message, wParam and the window come from the stack; the core makes
no Win32 call. In the owner's recordings every recorded key message has
exactly three `key` entries: all recorded keys are mapped and none is a
modifier key.

### Replacement

`es2_key_message_registers` (core) does steps 1–9 with every global
reread where the original reads it, and makes every call through the
weapons guest bridge with the original's EAX/EDX/ECX: `GetKeyState`
through `0x49695e` (stdcall; the bridge restores ESP), `0x4669b4`, and
the callbacks at the address read from the entry, with EAX = the list
byte after the match, DL = the matched byte and ECX = the code once a
previous callback returned (`pop ecx` at `0x477ccb`). EAX and EDX are
exact; ECX is the entry ECX (dead). The 21 conditional branches are all
taken both ways in vitro. The hook refuses compare mode.

### Open

- The focus path (`0x4669b4`) never runs in the six suites (no Alt or Ctrl
  key in any script or recording); it is checked in vitro only.

## Control record 45a810

`0x45a810` (hook `control_record`, group `control_record`, core
`core/src/control_record.c`) builds the 48-byte control record R =
`0x4d22c6` that the input dispatcher `0x460870` reads. Function
`0x45a810..0x45b65a`, 140 conditional sites; every statement marked
confirmed was read from the listing and is checked by the in-vitro
comparison with the original body (`differential_control_record_test`).
Field and global names are hypotheses from the consumers unless a cited
section confirms them.

**Callers.** The logic frame `0x45f570` (row 2, `0x45f583`, every active
iteration) and the modal screen loops `0x454d0e`, `0x45601d` (the loop of
the modal-dialog stall in [input_journal.md](../reference-game/input_journal.md#modal-dialogs-in-replay)),
`0x456e7d`, `0x457b1d`, `0x45880f`. No stack argument; cdecl.

### Split: platform, game callees, own code

The body interleaves device reads with the record construction, but every
device access is a call, so the split is clean at the call boundary:

| Part | Calls (entry EAX/EDX/ECX exact, below) | Mode under the hook |
|---|---|---|
| Message pump | `0x464c78` (PeekMessage/Translate/Dispatch → WndProc `0x466034`, keyboard handler `0x477bec`) | original, bridge |
| Device poll | `0x45baa8` (fills the device block D = `0x4d23f6` from the keyboard ring, joystick and mouse objects), key reader `0x45bbc4(p, 1)`, joystick object `0x45c524(3)`, joystick summary `0x477904` | original, bridge |
| Runtime | memset `0x49681a`, fopen `0x496802`, fread `0x49684a`, fwrite `0x496832`, fclose `0x496838` (CW3220 thunks); file name `0x492b90`, error report `0x479c78`, cursor copy `0x486ece` | original, bridge |
| Game code | keyboard axes `0x45a4cc(0x4d23c1, flag)` (`key_axes`), command handler `0x45feb8(key)` for queued keys (`command_key`), mouse event dispatch `0x452d40` (`mouse_dispatch`), playback key check `0x45fe6c(key)` (`playback_key`), mouse event append `0x453078(p, v)` (`mouse_queue`) | hooks, called through the bridge (they nest) |
| Record construction | everything else in `0x45a810`: key filtering, tape decoding and encoding, joystick mapping, axis and button selection, latches, camera pointers | **own code** |

Nothing had to stay original for lack of separation. The pump and the
device reads keep their call order and count, so the input journal of
the proxy (it hooks `PeekMessageA`, `GetKeyState`, `joyGetPosEx`,
`joyGetDevCapsA`, `GetCursorPos` below these callees) sees the same API
sequence. In the own process the pump, the device poll and its readers are
own code ([The input layer as own code](#the-input-layer-as-own-code)).

### Globals and frame

| Address | Use |
|---|---|
| `0x4d22c6` R (48 bytes) | the record; zeroed by memset each call |
| `0x4d2390` | pointer to the device block D (`0x45baa8`'s result, stored and reread before every use) |
| D+0 word, +2/+6 dwords, +0Ah..+0Ch | key word; cursor x/y; three mouse buttons |
| D+0Dh/+11h/+15h/+19h dwords | joystick x, y, throttle, rudder (hypothesis) |
| D+1Dh..+24h, +25h..+28h | eight joystick buttons, four hat bytes |
| `0x4d2310..0x4d238c` | pointer tables set by the input initialisation `0x45a6a7..`: axis targets R+0Eh/+10h/+12h/+14h, button targets R+16h..+1Dh, axis defaults `0x4d23c1`/`c5`/`c9`/`cd`, joystick axes D+0Dh.., button defaults `0x4d23d1..d4`, joystick buttons D+1Dh.. |
| `0x4d23c1` block | the keyboard axes that `0x45a4cc` writes: +0 turn, +4 throttle, +8 twist, +0Ch pitch (the defaults of the four axis targets) |
| `0x4d1f38`, `0x4d2577` | control mapping bytes and the mapping set index s: bytes s+0..s+3 select the joystick axes, throttle, rudder and hat modes; bytes `0x4d1f49..0x4d1f50` mark the button that becomes R+0Dh |
| `0x4d22f6..fd`, `0x4d22fe..0x4d2305` | eight latch flags and latch values |
| `0x4d24d6`, `0x4d24d8`, `0x4d24da`, `0x4d24e2` | tape playback word, recording word, recording file name argument, playback FILE* ([launcher.md](launcher.md): `-p`/`-D`) |
| `0x4d24f0`, `0x4d2526` | view word (0 = cockpit), projectile-view word (set by `0x40a644`) |
| `0x4d20c4`, `0x49eaa0`, `0x49eab8` | queued key words, their count (zeroed at the end), key mask |
| `0x4d2530`, `0x4d2532`, `0x4d1dd6` | quit-on-key word, quit request (also set by a nonzero pump result), live-mouse gate (set by the start-up code `0x461f38` unless tape playback runs and here when the tape ends; [Mouse event queue 452d40 / 453078](#mouse-event-queue-452d40--453078)) |
| `0x49ea90/94/98/9c`, `0x4d2571` | invert pitch, turn sign with forward throttle, projectile-view throttle sign, rudder sign, invert joystick y |
| `0x49ebbc`, `0x49ebbd` | "joystick block pending" byte for the tape, latch-0 state |
| `0x4d2306`, `0x4d230a`, `0x4d3b64` | 8-byte cursor position (`0x486ece` copies the software cursor `0x4a3620`, set by `0x486e7c`; also the last replayed event's position), the time scale word |

Frame (`add esp,-0C8h`, offsets from the ESP after the prologue): +0 the
second button set pointer (playback: +4; else D+1Dh), +4..+0Bh decoded
tape buttons, +0Ah the key word passed to `0x45bbc4`, +0Ch the event
count, +14h..+30h eight axis-source pointers, +34h..+39h the mapping
bytes and the throttle/rudder flags, +44h `0x452d40`'s list, +58h the
16-byte tape entry, +68h a 14-byte event, +78h..+0B7h sixteen button
pointers, +0B8h the recorded entry. Every local is written before it is
read on every path; the byte store at `0x45a836` (+8 ← 1) is dead. The
core keeps the addressed buffers in the adapter's scratch at the same
offsets and the rest in C variables.

### Order of work

Confirmed.

1. Pump; nonzero EAX → `0x4d2532` ← 1. memset(R, 0, 30h). D ←
   `0x45baa8()`, stored to `0x4d2390`. Key word k ← D+0: while k ≠ 0 and
   its low byte is 0, `0x45bbc4(&k, 1)`, stop when it returns AL = 0.
   k &= `0x49eab8`; D+0 ← k.
2. **Playback** (`0x4d24d6` ≠ 0): once (byte `0x49ebbc`) fread 8 bytes into
   the joystick summary block and `0x486ece(0x4d2306)`; fread the 16-byte
   entry, the event count n, the queued-key count (each count 4 bytes);
   quit-on-key; `0x45fe6c(k)`. A short read (the AND of the three fread
   results is 0) or k = 412h ends playback: fclose, `0x4d24d6` ← 0,
   `0x4d1dd6` ← 1, D+0 ← 0. Otherwise n events (fread 14 bytes, then
   `0x453078(event, dword event+0Ah)`, `0x4d2306/0a` ← its first 8 bytes;
   the loop compares signed and rereads n), the queued keys (fread count
   words) and the entry: R+0 key, R+0Eh/+10h/+12h/+14h axes, R+2Eh the time
   scale word (copied to `0x4d3b64` by the caller, main_loop.md), bits 0–7
   of entry byte +0Ch → R+16h..+19h, R+1Eh..+21h, bits of +0Dh → the frame
   buttons +4..+0Bh, bit 0 of +0Eh → R+0Dh.
3. **Joystick modes**: `0x45c524(3)` (the joystick object or 0); if nonzero,
   `0x477904()` J; J+5 sets the rudder flag; if word J+0 ≠ 0: mode m0 =
   map[s] (0 zeroes D+0Dh/+11h; 1 makes them the turn/throttle sources; 2
   the twist/pitch sources and negates D+11h if `0x4d2571`), if J+4: m1 =
   map[(u8)(s+1)] for D+15h (negated first if `0x4d2571`; 0 zeroes it, 1/2
   take the throttle/pitch slot or, if taken, the second slot), if J+5:
   m2 = map[(u8)(s+2)] for D+19h (turn/twist slots likewise).
4. Not in playback: `0x45a4cc(0x4d23c1, f)` with f = 1 if (m0 = 1 and
   cockpit view) or the projectile word is set; the button set becomes
   D+1Dh. Then every queued key word (masked in place) goes to `0x45feb8`;
   the count is reread after each call. `0x452d40()` gives the event list.
5. Keyboard-axis block: latch-0 state `0x49ebbd` (1 while the projectile
   word is set, 2 when latch 0's flag and value are set and the state was
   1, 0 when the flag is clear; a nonzero state clears turn and throttle).
   Outside or projectile view: joystick x/y replace and clear
   turn/throttle, twist and pitch are cleared, and without the throttle
   flag the throttle/rudder device values are cleared; cockpit with
   m0 = 1: turn → twist, −throttle → pitch. Then the throttle flag with
   m1 ≠ 0 clears pitch; the pitch, rudder inversions.
6. Not in playback: each axis target gets the word of the second source
   if that source is set and its **dword** is nonzero, else of the first
   under the same test, else of the default pointer; the hat (m3 =
   map[(u8)(s+3)] = 2: ±C0h to pitch or twist from the first set hat byte)
   and clearing of the hat bytes (m3 ∈ {0, 2}); the eight buttons
   (sources chosen before any copy); the latches (toggle while the button
   holds the latched value, else clear the flag); R+0Dh from the first
   button whose map byte at `0x4d1f49+j` is 1 (that button is cleared), else
   from `[0x4d2360]`; cursor, mouse and hat bytes and the key word into R,
   read and written alternately (order matters if D overlaps R).
7. **Recording** (`0x4d24d8`): name `0x492b90([0x4d24da], 0x49ebe0)`,
   fopen(name, `0x49ebe4`) (on failure `0x479c78(0x6c56c8, 5, 0, 0x49ebe7,
   0x342, name)`, and the writes go to a null FILE*), the pending joystick
   block, then the 16-byte entry in the playback layout (key, four axes,
   time scale `0x4d3b64`, bit word: R+16h..+21h, the second button set,
   R+0Dh at bit 16), the event count, the queued-key count, the events,
   the keys; fclose.
8. Camera pointers R+22h/+26h: R+0Eh/+10h in the outside or projectile
   view or with m0 ≠ 2; else R+12h/+14h. Latch 0's flag and value both set
   (cockpit, no projectile view) clear the chosen pair. If `0x49ea94` and
   the signed throttle word R+10h > 0, turn is negated. `0x49eaa0` ← 0.

### Registers

**Entry.** EAX/EDX/ECX are not read; they reach the pump unchanged.
EBX/ESI/EDI/EBP are saved. The callees reload R and `0x4d2390` themselves,
with one exception: the playback key check `0x45fe6c` returns 45a810's EBX
in EAX (EAX = `0x004d0001` there); that EAX is dead
([Command handler as own code](#command-handler-as-own-code)).

**At the calls.** The core reproduces the original's EAX/EDX/ECX at every
call, including the high words that byte and word loads leave (for
example the queued-key argument is the previous callee's EDX high word
with the key in DX, the key reader gets EAX = D, ECX = the frame address
of k, the joystick query gets EAX = EDX = ECX = 0, the keyboard axes get
EAX = EDX = f, the recorded entry is written with EAX = the time scale,
ECX = 10000h or 0). Popped argument slots are the arguments: no callee of
the function writes its own slot (`0x45fe6c`, `0x45c524`, `0x45feb8` read
`[ebp+8]` only; fclose's ECX is overwritten before use).

**At RET.** EAX = R. EDX = R+10h, or R+12h in the twist/pitch branch.
ECX = R+14h in that branch, 0 after the latch clear of the other cockpit
branch, else the value of the last write before (in the live path
((D+2) & FFFFFF00h) | byte D+28h; in recording mode the FILE*; in playback
the value left by step 5). The logic frame ignores EDX and ECX
(`0x45f5e3` and the frozen path `0x45f834` → `0x460870`, which reads
neither) and `0x454d0e` writes both, but the four modal loops push EAX
and call the screen's input virtual +10h with ECX and EDX still as
returned, which was not proved dead. The adapter returns EDX:EAX as a
pair; an entry shim (`control_record_entry`, chained after the AI
behaviour shims) calls the framework's dispatch and then loads the core's
ECX if the replacement ran.

### Stack residue

The guest calls run on the host stack below the adapter, not at the
original's coordinate. Known readers of uninitialized stack among the
callees: `0x45baa8` copies a 1Ch-byte joystick buffer and a 10-byte mouse
buffer from its own frame into D even when `0x45bc10` or `0x45bc74` did
not fill them. The mouse path is safe: `[0x4d24a0]` holds 0 or the
registered mouse id 2 (`0x45bda4`, `0x45be50`, `0x45c200` store nothing
else), and id 2 always fills the buffer (live or cached `0x4d241f`). The
joystick path reads residue when a joystick object (id 3) is registered
but disabled or its poll virtual (`0x45c330`) fails. Then the device
block would carry stack bytes in both runs, but different ones. A
registered joystick object is exactly what makes `0x45c524(3)` nonzero, so
the counter `call_477904` of this hook is the check: 0 in a suite means no
joystick object and no residue path. Not analysed: uninitialized reads
inside the WndProc tree under the pump (the keyboard handler `0x477bec` is
[above](#keyboard-message-handler-477bec)), and readers in later steps of
the logic frame of the frame bytes `0x45a810` used to leave (its region
below `0x45f570`'s ESP now holds host frames; the next steps are replaced
pool updates or originals whose locals were not checked).

### Live facts and unreachable edges

`0x477904` always stores word J+0 = 1 or 2, so the J+0 = 0 edge
(`0x45ab97`) is unreachable live; in vitro the stub returns both. All 140
sites are reached both ways in vitro; none is listed as proved one-way.
Map entry probes: `0x45a810` ≥ 1000 in all six suites; `0x464c78`,
`0x45baa8`, `0x45c524`, `0x452d40`, `0x45a4cc` once per call (no tape
playback in the suites); the tape and recording callees never.

### Open

Field meanings beyond the dispatcher's record table (the camera pointers,
R+0Dh as the "camera button", the hat steps) stay hypotheses. The record
layout of the original tape file is confirmed for reading and writing by
this function; the files themselves were not examined.

## Keyboard axes 45a4cc

`0x45a4cc` (hook `key_axes`, group `key_axes`, core `core/src/key_axes.c`)
turns the flight-key state arrays into the keyboard-axis block that
[`control_record`](#control-record-45a810) uses as the defaults of its four
axis targets and of two button targets. Function `0x45a4cc..0x45a5d8`, 88
instructions, 12 conditional sites; every statement marked confirmed was
read from the listing and is checked by the in-vitro comparison with the
original body (`differential_key_axes_test`). Key names are hypotheses
from the scan codes.

### Caller and arguments

The only call is `0x45ad3c` in `0x45a810`, step 4 of its order of work:
`push edx` (the flag f, zero-extended byte), `push 0x4d23c1`, `call`,
`add esp,8`; it runs once per control record when no tape is played back
(the playback word `0x4d24d6` is reread at `0x45acfe` after step 2, so a
tape that ends in step 2 still reaches the call). cdecl, two stack
arguments: the block B and the flag. **The flag has no effect**
(confirmed): it is loaded into ESI (`0x45a4d2`) and only tested by
`test si,si` at `0x45a510`, whose flags `cmp eax,8` at `0x45a51a`
overwrites on every path; ESI is overwritten at `0x45a55e` and restored
by the epilogue. The Alt word `0x4d23d6` (written by `0x45a324`) is
compared at `0x45a501` with the same outcome: both edges of `0x45a509`
and `0x45a50e` lead to `0x45a513`. The shift count is the constant 7
(`mov bx,7` at `0x45a4fd`, CL ← BL before each shift).

### Key-state arrays and tables

| Address | Meaning |
|---|---|
| `0x4d2394` D[0..14] | key down (`0x45a324` sets it on key-down) |
| `0x4d23a3` U[0..14] | released before a frame saw it (`0x45a324`: key-up of an unseen key sets it, key-down clears it) |
| `0x4d23b2` S[0..14] | seen by a frame (set here; `0x45a324` clears D, U and S on key-up of a seen key) |
| `0x49eb3d` E[0..7] | exclusive flag per direction key; shipped 1,0,1,0,0,1,0,1 (the four diagonals) |
| `0x49eb45` T[0..13] | signed byte pairs (x, y), all zero in the file; `0x45b8a4` writes (−1,−1) (0,−1) (1,−1) (−1,0) (1,0) (−1,1) (0,1) (1,1) for keys 0..7 and (0,−1) (−1,0) (1,0) (0,1) (0,−1) (0,1) for keys 8..13 |

The 15 keys are the order of the down codes at `0x49eb0b` (scan-code set
1): Num7, Num8, Num9, Num4, Num6, Num1, Num2, Num3 (0..7, the turn and
throttle pad), M, J, K, I, Num−, Num+ (8..13, twist and pitch) and Space
(14).

### Contract

Confirmed.

1. B+0Ch, +8, +4, +0 ← 0 (dwords, in this order), then the bytes B+11h,
   B+10h ← 0.
2. For i = 0..13 (`jb` at `0x45a580`, unsigned): skip i if D[i] = 0.
   Otherwise read the Alt word (no effect), S[i] ← 1, then
   - i < 8 (`jae` at `0x45a51d`): if E[i] ≠ 0, B+4 ← 0 and B+0 ← 0; then
     B+0 += sx(T[2i]) << 7 and B+4 += sx(T[2i+1]) << 7 (each a 32-bit
     read-modify-write; the table byte is read before the block dword).
     If E[i], **reread** at `0x45a54c` after both adds, is nonzero, i
     jumps to 8 (EAX ← 7, then the increment): an exclusive key replaces
     what earlier keys added and ends the pad scan.
   - i ≥ 8: B+8 += sx(T[2i]) << 7, B+0Ch += sx(T[2i+1]) << 7, and the
     loop ends (`jmp 0x45a586`): only the first held key of 8..13 counts,
     and S is not set for the later ones.
3. B+10h ← 1 if byte `0x4d23a2` (D[14], Space) ≠ 0; B+11h ← 1 if byte
   `0x4d23a3` ≠ 0. That byte is U[0], one past the end of D (hypothesis:
   the source indexes a sixteenth key of D). Then B+13h, B+12h ← 0.
4. For j = 0..14: if D[j] ≠ 0 and U[j] ≠ 0 (D read first), S[j] ← 0 and
   then D[j] ← 0. U stays set. A tap shorter than one control record is
   thus seen exactly once.
5. Return. EAX = 0Fh, EDX = `0x4d23a3`, ECX = `0x4d23b2` (the pointers of
   the last loop after 15 steps), flags of `cmp eax,0Fh` (ZF set, CF
   clear); EBX, ESI, EDI, EBP restored. Every register at RET is a
   constant.

The block is 20 bytes (B+0..+13h): +0 turn, +4 throttle, +8 twist, +0Ch
pitch (the axis defaults `0x4d23c1`/`c5`/`c9`/`cd` of control_record's
pointer table `0x4d2340`) and the bytes +10h..+13h (`0x4d23d1..0x4d23d4`,
the button defaults of `0x4d2360`); +12h/+13h are always cleared. Bytes
past B+13h are not touched. Every read is of image data or the block;
there is no call, no Win32 state (the input journal is not involved) and
no frame local. All reads and writes are in the order above; a block that
overlaps the key arrays or the tables (only possible in vitro) sees the
same effects as in the original.

### Registers at the caller

After the call `0x45a810` does `add esp,8` (flags dead) and
`mov ecx,[edi]` at `0x45ad44` (ECX dead). EAX and EDX stay live: the
queued-key loop (`0x45ad55..`) replaces only AX and DX before pushing EDX
as the argument of `0x45feb8`, so the high word of `0x4d23a3` (004Dh) is
the high half of that argument slot; without queued keys `0x452d40` is
called with EAX = 0Fh and EDX = `0x4d23a3` at entry. The replacement
returns EDX:EAX exactly (both constants, relocated with the image base);
ECX and the flags are not reproduced. The replaced `control_record` calls
`0x45a4cc` through the guest bridge and takes the returned EDX:EAX as the
registers of its next call, so the nesting is exact in both of its modes.

### Stack

The original's frame is the two argument slots, the return address and
four saved registers: with S the caller's ESP before the two pushes, the
return address lies at S−0Ch and EBP, EBX, ESI, EDI at S−10h..S−1Ch. It
writes no argument slot. With the hook the dispatch frames lie below the
return address instead, so the residue below `0x45a810`'s ESP differs
after the call. Its next readers there are `0x45feb8` (queued keys only)
and `0x452d40`: in `0x45feb8`'s own body every local (`ebp−4..−18h`,
`−3Ch..−5Ch`) is written before it is read (listing scan in linear order;
the exception record at `ebp−38h` is filled by `0x49630c`), and
`0x452d40`'s locals at `esp+0`/`+4` are written at `0x452d56`/`0x452d82`
before their reads. Not analysed: the deeper callees of `0x45feb8`. With
`control_record` replaced, these calls come from host frames, so the
coordinate of the residue is the bridge's in both cases. No entry shim.

### Unreachable edges

None: all 12 sites are reached both ways in vitro. With the real caller
the block never overlaps the tables, so `0x45a553` always repeats the
outcome of `0x45a526`; the separate reread is still reproduced.

### Live facts

Map entry probe: `0x45a4cc` saturated (≥ 1000) in all six suites, one
call per control record (no tape playback in the suites), so
`hk_key_axes_calls` = `hk_control_record_call_45a4cc` =
`hk_control_record_calls`.

### Replacement

`es2_key_axes_registers` (core) does steps 1–5 through the guest memory
in the original's order, including the second read of the exclusive flag
and the table-before-block order of each add, and returns EDX:EAX =
`0x4d23a3`:0Fh relocated with the image base. The adapter
(`proxy/key_axes_hooks.h`) passes the block and the flag from the stack
and the image base from the descriptor's user pointer; no game call, no
callee counters, no entry shim. Checked in vitro against the original
body (direct cases with blocks that overlap the key arrays and the
tables, installed poison probes, and the real caller `0x45a810` with
`key_axes` and `control_record` each original or replaced). The hook
accepts compare mode: a 20-byte span on the block and one RVA region over
the three key arrays `0x4d2394..0x4d23c0`, restored in place for the
replacement phase; the default comparison
([compare_mode.md](../reference-game/compare_mode.md#spans-and-comparators)).

## Mouse event queue 452d40 / 453078

`0x452d40` and `0x453078` are the **mouse event queue of the modal GUI
screens**. Both are replaced by hooks in the group `mouse_events`
(`core/src/mouse_events.c`): `mouse_dispatch` (452d40) and `mouse_queue`
(453078). Names of fields and globals are hypotheses from the code.

### Classification

Confirmed from the listing.

The producer side: the window procedure's mouse message handler
`0x4809f8` (WM_MOUSEMOVE/BUTTON messages) calls `0x480948`, which scales the
cursor into the 10-byte block `0x6c5f50` (x dword, y dword, button word:
bit 0 left, bit 1 right) and calls every registered callback of the
10-entry table `0x6c5ed8` (flag, message mask, function) whose mask has the
message's bit. The queue initialisation `0x452ae4` (called once by
`0x46181f`) registers `0x452cdc` there with mask 1Eh (button messages, not
moves), allocates two lists of 100 events of 14 bytes (`0x49667c`, the
runtime's array new), sets the list headers `0x4d1df4`/`0x4d1dfc`
(count dword, entries pointer) as write list `[0x4d1e04]` and read list
`[0x4d1e08]`, clears the button states and sets the double-click time
`0x4d1dec` = 30. `0x452cdc` appends the message as an event (10 bytes plus
the time from `0x467828`) through `0x453078` when the byte `0x4d1dd6` is
set (the start-up code `0x461f38` sets it unless tape playback runs, so
recorded mouse input is not mixed with live input; 45a810 sets it when
the tape ends) and the button word changed, or, with unchanged buttons,
when the time advanced (the cursor follow `0x486e7c` first if `0x49dbdf`).
The tape playback of `0x45a810` appends the recorded events through the
same `0x453078`; its recording mode writes the list that `0x452d40`
returns.

The consumer side: `0x45a810` calls `0x452d40` once per call (logic frame
and modal screen loops). It swaps the two lists, empties the new write
list and dispatches every event of the read list to the active screen
`[0x49dbd8]` (set by `0x452bbc` from `0x431c9a`, `0x45478f`, `0x45498c`):
the widget library at `0x4523b0..0x452ab8` hit-tests the screen rectangle
(`0x4523b0`), finds the gadget under the cursor (`0x452a28`: gadget
pointers at screen +256h, count word +254h; gadget rectangle +0..+0Ch, round
flag +10h, centre +11h/+13h, radius +15h, vtable +17h, state word +1Bh: 0
up, 1 pressed, 2 disabled; drag word +1Dh), presses (`0x4527c8`), moves
with a pressed button (`0x45297c`), releases (`0x452898`) and redraws
(`0x452ab8`: screen +227h ← 1, gadget virtual +4). Gadget virtuals: +8
activate (the default `0x45557a` is empty), +10h the code passed to +8
(the default `0x45554c` returns the event's button word `[0x49db6c]`),
+18h drag (default empty). Screen code reads the outputs: the cursor
relative to the screen `0x4d1de3` (8 bytes), the event button word
`0x49db6c`, the double-click byte `0x49dbe0`.

This is GUI input processing between the platform layer (the window
procedure) and the screen code (behind the bridge). It feeds the control
record: the tape records and replays these lists. No compared simulation
field is written directly; the effect on the game is through the gadget
activations (for example the buttons of the end dialog). The pure part is
the whole of both functions; every callee is a guest call.

### Globals

| Address | Use |
|---|---|
| `0x4d1e04`, `0x4d1e08` | write list W, read list R (pointers to the headers `0x4d1df4`, `0x4d1dfc`: count dword, entries pointer) |
| event (14 bytes) | +0 x dword, +4 y dword, +8 button word, +0Ah time dword |
| `0x49dbd8` | the active screen S (0 = none; 452d40 dereferences it whenever an event needs it) |
| S+0, S+4 | screen origin used for the drag point; S+22Bh/+22Fh the origin subtracted from the cursor |
| `0x4d1de3` | cursor relative to the screen (x, y) |
| `0x4d1dcc`, `0x4d1dd0` | the last event position |
| `0x49db6c` | the current event's button word (0 between events) |
| `0x49dbdc` (word), `0x49dbde` (byte) | captured gadget index (−1 none), drag mode |
| `0x49dbdf` | cursor-follow flag (the playback path calls `0x486e7c(E)`) |
| `0x4d1dd4`, `0x4d1dd5` | left/right button state (1 = down) |
| `0x49dbe4`, `0x49dbe8`, `0x4d1dec` | time of the last left/right release, double-click time |
| `0x49dbe0` | double click (cleared per event, set when a release follows the previous one within `0x4d1dec`, unsigned) |
| `0x4d24d6` | tape playback word (read) |
| `0x4d1dd6` | live-mouse gate (above) |
| `0x4d2306` | cursor position in the tape header and of the last replayed event (`0x486ece` copies the software cursor `0x4a3620`) |

### `0x453078` append (E, v), cdecl

`W = [0x4d1e04]`. If the dword `[W]` is exactly 63h (99) the event is
dropped: EAX = 0, EDX and ECX as at entry. Otherwise, with n = `[W]`
(read twice, then `inc [W]`), the slot is A = `[W+4]` + 14·n (the entries
pointer read after the increment; 32-bit wrap). Copies in this order: word
E+8 → A+8, dword E+0 → A+0, dword E+4 → A+4, then v → A+0Ah (so an E that
overlaps A is read after the earlier stores). Returns EAX = (A & FFFFFF00h)
| 1, EDX = v, ECX = dword E+0. Counts above 99 are not refused (only
equality is tested). Callers: `0x452cdc` (twice) and `0x45a810` (playback);
ECX after the call is dead at both (452cdc returns to `0x480948`, which
pops ECX; 45a810 reloads ECX at `0x45a9cd`), so the hook returns EDX:EAX
exactly and needs no shim; the core's ECX is still checked in vitro.

### `0x452d40` dispatch (no arguments), cdecl

Frame `add esp,-18h`: +0 the old W, +4 the event index i, +8/+0Ch the drag
point (its address is passed to the gadget's virtual +18h), +10h the
release flag, +14h the event time; every slot is written before it is
read. EBX/ESI/EDI/EBP saved; ESI = `0x4d1de3`, EBP = `0x49dbd8` (so S is
reread at every use). Entry EAX/EDX/ECX are not read.

1. Swap `[0x4d1e04]` ↔ `[0x4d1e08]` (EAX = old W, EDX = old R, ECX = old W),
   EAX = new W, `[new W]` ← 0 (EDX = 0), ECX = new R, the event pointer
   E ← `[R+4]` (read once; +14 per event), EAX = 0, i = 0.
2. Loop while the count `[[0x4d1e08]]` (pointer and count reread before
   every event) > i, signed:
   - `0x49db6c` ← word E+8 (AX).
   - **Position.** Unless E+0 = `[0x4d1dcc]` and E+4 = `[0x4d1dd0]`: store
     both as the last position; if the playback word and `0x49dbdf` are set,
     `0x486e7c(E)` (EAX = E, EDX = E+0, ECX = E+4; POP ECX); P ← E+0/E+4,
     then P −= S+22Bh/+22Fh (dword reads interleaved with the stores). In
     drag mode (`0x49dbde`): if the captured index is −1 nothing more;
     else the drag point ← E − (S+0, S+4), G = S+256h[idx] (sign-extended
     index), `G->vt[+18h](G, &point)` (EAX = G, EDX = idx, ECX = vtable),
     then `0x452ab8(S, G')` with the index, S and G' reread (EAX = S,
     EDX = S, ECX = G'). Otherwise `0x4523b0(S, E)` (EAX = S as read before
     the subtraction, EDX = S, ECX = dword S+22Bh); AL ≠ 0 →
     `0x45297c(S, E)` (EAX/EDX = 4523b0's, ECX = S).
   - **Buttons.** `0x49dbe0` ← 0, EAX = 0, flag = 0, T = E+0Ah. Left bit
     with state `0x4d1dd4` = 0 → state 1, AL = 1. Right bit with state
     `0x4d1dd5` = 0 → state 1 (AL unchanged). AL ≠ 0 → P ← E − S+22Bh as
     above, `0x4527c8(S, E)` (EAX = dword S+22Fh, EDX = S, ECX = dword
     S+22Bh). Then AX ← word E+8 (reread): left bit clear and state = 1
     (exactly) → flag, state 0, `0x49dbe0` ← 1 if T − `[0x49dbe4]` <
     `[0x4d1dec]` (unsigned), AX |= 1, `0x49dbe4` ← T, `0x49db6c` ← AX.
     AX ← word E+8 again; the same for the right bit with `0x4d1dd5`,
     `0x49dbe8` and AX |= 2.
   - **Release** (flag): P ← E − S+22Bh. Drag mode: index −1 → nothing;
     else word G+1Bh ← 0, `0x452ab8(S, G)` (EAX = S, EDX = S, ECX = G),
     `0x49dbde` ← 0, G reread into EBX, `G->vt[+10h](G)` (EAX = vtable,
     EDX = idx, ECX = S; POP ECX), `G->vt[+8](G, (int16)AX)` (EAX = +10h's
     EAX, EDX = the sign-extended AX, ECX = the vtable reread), index ←
     −1. Otherwise `0x452898(S, E)` (EAX = S, EDX = dword S+22Bh, ECX =
     dword S+22Fh).
   - `0x49db6c` ← 0, i += 1, E += 14.
3. Return EAX = EDX = `[0x4d1e08]` (the read list), ECX = its count (both
   from the last loop test).

**Registers at RET.** The only caller 45a810 stores EAX (frame +44h) and
continues with EDX and ECX; in playback ECX can survive to 45a810's RET
([Registers](#registers)), which the modal loops pass to the screen's
input virtual. So `mouse_dispatch` returns EDX:EAX and loads ECX through
an entry shim (`mouse_events_entry`), the same scheme as `control_record`.

### Stack residue

452d40 reads no uninitialized local, and neither do its direct callees
`0x4523b0`, `0x452a28`, `0x45297c`, `0x4527c8`, `0x452898`, `0x452ab8`,
`0x486e7c` (each writes its locals before reading them). The gadget
virtuals are screen code; under the replacement they run on the host
stack below the adapter, as they do under the replaced `control_record`.
The drag point is passed by address in the adapter's scratch at the
original frame offset +8. 453078 has no frame and no callee.

### Live facts

DBSIM map entry probes (a60/c1s/c1c/c3_09/c4_08/c3_10): 452d40 ≥ 1000 in
all six suites; 453078 and 452cdc 0/0/0/48/2/6 (the callback runs only on
button messages and every call appends: a button change each time); 4527c8
and 452898 0/0/0/24/1/3 (their only caller is 452d40), 45297c 0/0/0/5/1/1
(only caller 452d40), 452ab8 0/0/0/48/2/6, `0x4523b0` 0/0/0/≥1000/3/101
(also called per gadget by 452a28). So the suites c3_09, c4_08 and c3_10
click on a modal screen (24, 1 and 3 press/release pairs) and replay the
events through both hooks; `control_record`'s `call_453078` counts only
the tape path.

## Flight-key callback 45a324

`0x45a324` (hook `flight_key`, group `flight_keys`, core in
`core/src/control_record.c`) is the hot-key callback that keeps the three
15-byte key-state arrays read by [`key_axes`](#keyboard-axes-45a4cc).
Function `0x45a324..0x45a439`, 84 instructions, 15 conditional sites; every
statement marked confirmed was read from the listing and is checked by the
in-vitro comparison with the original body (`differential_flight_keys_test`).
Key names are hypotheses from the scan codes.

### Registration and caller

The only reference to `0x45a324` in the file is the push at `0x45b6c1` in
the flight-key capture switch `0x45b68c` ([display_modes.md](display_modes.md)):
when capture turns on (and byte `0x49eb61` is 0) `0x477b18` puts it with
the list `0x49eb0b` into the first free entry of the hot-key table
`0x6bb8b8` ([Registered hot keys](#registered-hot-keys)). Nothing removes it
(the unregister functions have no caller), so after capture off the entry
stays active and the callback passes every key on (step 2 below); a second
capture-on adds a second entry. The only call is `call [edi+8]` at
`0x477cc8` in the keyboard handler `0x477bec` for every key message whose
code's low byte (80h included, modifiers not) is in the list: `push esi`
(the whole code: scan code, 80h for key-up, 200h Alt, 400h Ctrl, 800h
Shift), call, `pop ecx`, `dec eax`, `jne`. cdecl, one stack argument; plain
`ret`. The caller reads only EAX (1 consumes the key; anything else goes on
with the next entry and then the key ring). ECX is replaced by `pop ecx`,
the flags by `dec eax`; EDX survives into the next list scan (which replaces
only DL) and, when the key is not queued, into `0x477bec`'s own EDX at RET,
which is dead at the window procedure. With `key_message` replaced, its
core calls the callback through the guest bridge with the same arguments
and sets ECX itself after the call.

The list `0x49eb0b` (DATA, 30 bytes, no writer found) holds the 15 down
codes Num7, Num8, Num9, Num4, Num6, Num1, Num2, Num3, M, J, K, I, Num−,
Num+, Space (`47 48 49 4B 4D 4F 50 51 32 24 25 17 4A 4E 39`) and then the
same codes with 80h (`C7 … B9`), the key-up forms that make the handler
call the callback on release.

### Contract

`uint32 __cdecl 0x45a324(code)`. EDX ← code, ESI ← code. Confirmed.

1. **Demo stop.** If word `0x4d2530` (the demo / quit-on-key word set by
   `-D`) ≠ 0: word `0x4d2532` ← 1 (the quit request that ends the main
   loop) and EAX = 2. Nothing else is read or written.
2. **Capture off.** If byte `0x49eaa5` (the capture flag that `0x45b68c`
   toggles) = 0: EAX = 2, nothing written.
3. **Down flag.** d = 1 if bit 7 of the code is clear (key-down), else 0
   (`test dl,80h`, `sete cl`, `and ecx,1`); the byte is stored in the
   frame slot `[ebp−1]` (the top byte of the pushed entry ECX). Then DX ←
   DX & 7Fh: EDX = (code & FFFF0000h) | (code & 7Fh) from here on, and the
   key k = code & 7Fh.
4. **Num− / Num+.** If byte `0x4d2571` = 0 (a preference byte, written at
   `0x4245e0`; hypothesis "invert joystick y") and k is 4Eh or 4Ah:
   EAX = 2, nothing written.
5. **Search.** i = the first index 0..29 with byte `0x49eb0b`+i = k (byte
   zero-extended against `movzx edi,dx`), or 30 when none matches. If i >
   15 (`jbe` at `0x45a3a9`, unsigned), i −= 15. If i ≥ 15: EAX = 2, nothing
   written. With the shipped list the key-up forms (≥ 80h) never equal a
   7-bit k, so i is a down index 0..14 or 30 (→ 15, not found). The
   folding is off by one for list entries 15..29 (entry 15 gives 15 and is
   ignored, entry 15+j gives j); only a modified list reaches it.
6. **Key state** (D = `0x4d2394`, U = `0x4d23a3`, S = `0x4d23b2`, see
   [Key-state arrays and tables](#key-state-arrays-and-tables)): key-down:
   D[i] ← 1, then U[i] ← 0. Key-up: read S[i]; if ≠ 0 (a frame has seen the
   key): U[i] ← 0, D[i] ← 0, S[i] ← 0 in this order; else U[i] ← 1 (D stays
   set so that `key_axes` sees the tap once and drops it then).
7. **Alt word.** Word `0x4d23d6` ← code & 200h (the only other reference
   is `key_axes`'s dead compare at `0x45a501`).
8. **Result.** EAX = 2 if k is 1Ch (Enter; not in the shipped list, so
   never) or 39h (Space: the state is kept and the key also goes on to the
   ring); else EAX = 2 if dword `0x49ef38` (the `-SPRUNKNOWN` flag,
   [launcher.md](launcher.md)) ≠ 0 and code & 600h (Ctrl or Alt) ≠ 0; else
   EAX = 1 (consumed).

Registers at RET: EAX = 1 or 2; EDX = code after steps 1–2, else
(code & FFFF0000h) | (code & 7Fh); ECX = the entry ECX after steps 1–2,
else the entry ECX with its top byte replaced by d (`pop ecx` of the
slot written at `0x45a363`); EBX, ESI, EDI, EBP restored; flags of the
last compare (dead). Reads and writes happen exactly in the order of the
steps; the key arrays, the list and the globals are distinct image data,
so no step can alias another. No call, no Win32 state (the input journal
is not involved).

### Stack and entry state

Frame: with S the ESP at entry (return address at S), EBP at S−4, the
pushed entry ECX at S−8 (its top byte S−5 is rewritten with d), EBX, ESI,
EDI at S−0Ch..S−14h. The argument slot is not written. The only local,
`[ebp−1]`, is written at `0x45a363` on every path that reaches its read at
`0x45a3b5`; no entry register other than the pushed ECX is read, and ECX
leaves only through the dead `pop ecx`. With the hook the dispatch frames
lie below the return address instead, so the residue below the caller's
ESP differs. The original caller's next readers there are the next
callback (`0x45a498` and `0x477a7c` have no locals, `0x45a324` writes its
local first) and `GetKeyState` (Win32) of the next message; with
`key_message` replaced the callback is called from host frames, so the
residue's coordinate is the bridge's in both modes. No entry or exit shim.

### Unreachable edges

None: all 15 sites are reached both ways in vitro. The Enter edge
(`0x45a402` taken) and the folded indices 16..29 need a list that holds
1Ch or 7-bit codes in its second half; the direct cases modify the list in
a quarter of the cases.

### Live facts

Map entry probe (a60/c1s/c1c/c3_09/c4_08/c3_10): 16 / 224 / 8 / ≥ 1000 /
569 / 278. `hk_flight_key_calls` = `hk_key_message_call_45a324` (every
call comes from the replaced `key_message`); like `key_message_calls` they
follow the Windows message pump and can differ by one pump iteration
between identical runs. No callee counters. With all hooks replaced the six
suites are identical.

### Replacement

`es2_flight_key_registers` (core, in `control_record.c`) does steps 1–8
through the guest memory in the original's order and returns EDX:EAX
exactly. ECX is not reproduced (dead at both callers). The adapter
(`proxy/flight_keys_hooks.h`) passes the code from the stack and the image
base from the descriptor's user pointer; no game call, no callee
counters, no shim; compare mode is refused like the other input hooks.
Checked in vitro against the original body (direct cases including
modified lists, installed poison probes, and the real caller `0x477bec`
with `key_message` and `flight_key` each original or replaced).

## Command-key queue callback 45a498

`0x45a498` (hook `command_queue`, group `command_queue`, core
`es2_command_queue_registers` in `core/src/control_record.c`) is the
hot-key callback that fills the queue the command handler `0x45feb8` (hook
`command_key`) reads. Function `0x45a498..0x45a4c8`, 14 instructions, one
conditional site `0x45a4a2`. Every statement marked confirmed was read from
the listing and is checked by the in-vitro comparison with the original
body (`differential_command_queue_test`). Names are hypotheses.

### Registration and caller

The only reference to `0x45a498` in the file is the push at `0x45b6d4` in the
flight-key capture switch `0x45b68c` ([display_modes.md](display_modes.md)),
which, when capture turns on (and byte `0x49eb61` is 0), registers it with the
list `0x49eb35` through `0x477b18` right after `flight_key`
([Registered hot keys](#registered-hot-keys)). The only call is
`call [edi+8]` at `0x477cc8` in the keyboard handler `0x477bec` (cdecl:
`push esi` = the whole key code, call, `pop ecx`, `dec eax`, `jne`; result 1
consumes the key). Like `flight_key`, the entry stays in the table after
capture off. The queue itself (`0x4d20c4`, words) and its count
(`0x49eaa0`, dword) are read by the [control record](#control-record-45a810)
(every queued word goes to `0x45feb8`, the count is zeroed at the end), by
the recorder and by the playback loader (`0x45a91e`/`0x45a9f8`).

### Contract

`uint32 __cdecl 0x45a498(code)`; only the low word of the stack slot is
used. Confirmed.

1. **Gate.** `cmp byte [0x49eaa4],0 / je 0x45a4c2`: byte `0x49eaa4` = 0 gives
   EAX = 2 and nothing else happens (EDX, ECX and memory unchanged). The byte
   is the "queueing enabled" flag: `0x454697` clears it and `0x454a8c` sets
   it, around the counter word `0x49dd8c` of the modal screens.
2. **Append.** `mov dx,[ebp+8]` replaces only the low word of EDX (the high
   word of the entry EDX stays); `mov eax,[0x49eaa0]`; `mov word
   [2*eax+0x4d20c4],dx`; `mov eax,1`; `inc dword [0x49eaa0]`. The index is the
   whole dword, **not bounded** against the array: the effective address is
   `0x4d20c4 + 2*index` computed in 32 bits (it wraps, so index and index +
   80000000h write the same word). The queue is cleared every frame (the
   control record zeroes the count at `0x45b649`), and nothing else bounds the
   count: the array has 257 words before the control record `0x4d22c6`; the
   playback loader can set the count from a tape (`0x45a91e`), which is how a
   large index could arise (rng.md, item 5). The increment **rereads** the
   count, so a store that hits the count (index −19B12h or −19B11h, modulo
   2^31: the word at `0x49eaa0` or `0x49eaa2`) is incremented as modified.
3. **Registers at RET.** EAX = 1 on the append path, 2 on the gate path. EDX
   = (entry EDX & FFFF0000h) | (code & FFFFh) on the append path, the entry
   EDX on the gate path. ECX, EBX, ESI, EDI are untouched; EBP is restored by
   `pop ebp`. Flags are those of `inc` (dead).

The caller reads only EAX. After the call EDX is dead in `0x477bec`: the next
use is `mov dl,[eax]` in the list scan (which replaces only DL) or the
ring-index load `mov edx,[0x6bb8b0]` at `0x477cfa`; at the early exit it is
dead at the window procedure. The replaced `key_message` core nevertheless
carries the callback's EDX in its own EDX state, so the hook reproduces it.

### Precondition

The core writes the word and the count at exactly the addresses the original
does, with no bound check of its own. The precondition is the original's: the
address `0x4d20c4 + 2*index` (32-bit wrap, image-relative in the core) must be
writable guest memory. Outside the image the original faults, and the hook
faults at the same word. In vitro the index is limited to addresses inside
the two comparison windows (below).

### Stack and entry state

Frame: no locals; the only stack write is `push ebp` at S−4 (S the ESP at
entry, return address at S). The argument slot is not written and no entry
register other than EDX (high word) is read. No entry or exit shim. With the
hook the dispatch frames lie below the return address, so the residue below
the caller's ESP differs, as for `flight_key`; with `key_message` replaced
the callback is called through the bridge, so the residue's coordinate is
the bridge's in both modes.

### Unreachable edges

None: the single conditional site is reached both ways in vitro.

### Live facts

Map entry probe (a60/c1s/c1c/c3_09/c4_08/c3_10): 0 / 30 / 0 / 21 / 5 / 0.
`hk_command_queue_calls` = `hk_key_message_call_45a498` (every call comes
from the replaced `key_message`; they follow the Windows message pump and
can differ by one pump iteration between identical runs). No callee
counters. With all hooks replaced the suites are identical.

### Replacement

`es2_command_queue_registers` does steps 1–3 through the guest memory in the
original's order (flag read, index read, word store, count reread and store)
and returns EDX:EAX exactly. The adapter (`proxy/command_queue_hooks.h`)
passes the code from the stack, the entry EDX from the context registers and
the image base from the descriptor's user pointer; no game call, no callee
counters, no shim. Compare mode is refused (`compare_unsupported`): the
written address depends on the unbounded count, so no region can declare it.
The prologue holds `cmp byte [abs],imm8` (`80 3D`); the hook framework's
prologue whitelist accepts `80 /7` (fixture cases, hook README). Checked in
vitro against the original body: direct cases (index classes 0..120h, up to
3BFh, bit 31 set, and stores that alias the count, the flag and their
neighbours, over the windows `0x49ea80..0x49eabf` and `0x4d2000..0x4d27ff`),
installed 31/a7 poison probes, and the real caller `0x477bec` with
`key_message` and `command_queue` each original or replaced.

## Command handler as own code

The command handler `0x45feb8` and its playback key check `0x45fe6c` run
as own code behind the hooks `command_key` and `playback_key` in the
adapter group `commands` (`core/src/commands.c`,
`tools/reference-game/proxy/commands_hooks.h`, in vitro
`tools/reference-game/invitro/commands_cases.h`). The key table is in
[Command handler `0x45feb8` (key → handler)](#command-handler-0x45feb8-key--handler);
this section adds the register flow, the frame and the split.

| Hook | VA | Instructions | Map probes (a60/c1s/c1c/c3_09/c4_08/c3_10) |
|---|---|---|---|
| `command_key` | `0x45feb8` | 599 reachable (448 before the jump table), 88 Jcc sites, table `0x45ff50` (29 entries) | 1000/1000/1000/1000/1000/937 |
| `playback_key` | `0x45fe6c` | 24, 5 Jcc sites | 1000/1000/1000/1000/1000/937 |

### Callers and what they read after the call

`0x45feb8`, cdecl, one stack slot (the code dword), five call sites:

| Site | Caller | Reads after the call |
|---|---|---|
| `0x46088f` | `player_input` (record code) | `cmp ax,2; pop ecx; sete dl; and edx,1`: AX; the whole EAX reaches `0x432b14`'s entry; ECX popped, EDX rewritten |
| `0x460e83` | `player_input`, button action 6 (code `0x14`) | `pop ecx; jmp 461097`: EAX/EDX stay in the button loop's register flow (the loop's last writer feeds `player_input`'s result high word) |
| `0x45ad62` | `control_record`, queued keys | `pop ecx`; EAX/EDX reach the next call's entry (`0x45feb8` writes EAX first; EDX goes on to `0x49630c`/`0x432bc8`) and `0x452d40` (reads no entry register) |
| `0x466522` | window procedure, `WM_CLOSE` (code `0x410`) | `pop ecx; mov [4d370ch],ax` |
| `0x466535` | window procedure, `WM_QUERYENDSESSION` | `pop ecx; mov ebx,eax; mov [4d370ch],bx; movsx eax,bx; sub eax,2`; EDX reaches the window procedure's return (Windows) |

`0x45fe6c`, cdecl, one slot, two call sites: `0x45ff08` in `0x45feb8`
(`pop ecx; mov ebx,eax; test ax,ax`: the whole EAX becomes EBX, which is
the command handler's result when AX ≠ 0 and the high word of its EBX for
the case table otherwise; EDX flows into the case handlers) and `0x45a949`
in `control_record`'s tape playback (`pop ecx; test esi,esi`: EAX reaches
only the `fclose` thunk's entry on the short-read path, EDX is rewritten on
every path). So both hooks return EDX:EAX exactly; **ECX is popped at all
seven sites** (no exit shim); the flags are written before they are read at
all seven.

### Contract of `0x45fe6c`

Confirmed. `push ebp; mov ebp,esp; push ebx; mov bx,1` keeps the
**caller's EBX high word**; `mov ax,[ebp+8]` overwrites AX before anything
reads it (the entry EAX's high word stays).

1. AX = code − 21Ch. Zero (Alt+Enter): unless both the tape playback word
   `0x4d24d6` and the option word `0x4d252c` are nonzero (read in that
   order, the second only if the first is set), call the display switch
   `0x4667c8` (EAX = the entry EAX high word over 0, EDX/ECX as at entry).
2. Else AX −= 214h. Zero (Ctrl+B, `0x430`): if `0x4d252c` ≠ 0, call
   `0x467ad8` (`int3; ret`, a debug break; same registers).
3. Result: EAX = (caller's EBX & FFFF0000h) | 1 for the two codes, else 0
   (`xor ebx,ebx`); EDX = the entry EDX or the callee's; ECX likewise
   (dead). At `0x45ff08` the caller's EBX is 0 (`xor ebx,ebx; mov bl,al`
   with AL = 0), so EAX is 1 or 0; at `0x45a949` it is R = `0x4d22c6`, so
   EAX is `0x004d0001` there (dead, see above).

### Contract of `0x45feb8`

Confirmed.

**Frame.** `add esp,-5Ch`; `mov eax,49F648h; call 49630c` links a runtime
SEH record at EBP−38h (descriptor `[49F648h+4]` = −38h; saved head at
EBP−38h, try-state word at EBP−28h). Every exit restores `FS:0` from
EBP−38h and returns that head in EDX. ESI = the code dword (`[ebp+8]`)
from `0x45fec6`. The caller's EBX and EDI are live at `0x49630c` and
`0x432bc8` and reach them as the original's (`0x432bc8` saves the caller's
EBX before writing it). Locals, all written before they are read on every
path: EBP−4/−8 (alert dialog object and copy), −0Ch/−10h (Ctrl+Q),
−14h/−18h (P), −5Ch (alert copy), −3Ch/−40h/−44h (Alt+Ctrl+N index word,
list cursor, actor), −48h/−4Ch/−50h (Ctrl+N/P cursor and copies),
−54h/−58h (N: the player's group, cursor copy).

**Order of work.**

1. Word `0x4d2564` (set at `0x45f44f` before the main loop) = 0 → EAX = 0.
2. `0x432bc8(H = [0x49b088], code)` (`hud_key`; EAX = H, EDX/ECX the entry
   registers). EBX = AL; AL ≠ 0 → EAX = AL.
3. `0x45fe6c(code)` with EBX = 0; ECX ← the popped slot (the code); EBX ←
   its EAX; AX ≠ 0 → EAX = that EAX.
4. `mov bx,1` (EBX = `0x45fe6c`'s EAX high word over 1, i.e. 1), EAX = code
   & FFFFh, the compare chain `0x45ff20..0x460094` (signed compares of the
   zero-extended word; the `sub`/`je` steps leave the remainder in EAX,
   which reaches the callees) and, for 0…1Ch, the jump table `0x45ff50`:
   0, 2–0Dh, 11h–13h, 15h–18h, 1Ah, 1Bh → the default; 1 → `0x46069f`;
   0Eh → `0x46053f`; 0Fh, 1Ch → `0x4604e1`; 10h → `0x4605ea`; 14h →
   `0x46058e`; 19h → `0x460759`. 1Dh–27h take the `ja` to the default.
5. Exit `0x46085c`: EAX = EDI + EBX (EBX = 1 or the key method's EAX, EDI =
   0 or the dialog result), EDX = the saved head.

Registers at the case handlers: EBX = 1, ESI = the code dword, EDI = 0,
ECX = the code dword, EDX = `0x45fe6c`'s EDX. That EDX is `0x432bc8`'s
saved `FS:0` head, i.e. the address of this function's own SEH record; it
is the entry EDX of most handler callees and the high word of
Alt+Ctrl+D's slot argument (below).

| Code | Handler | Effect (callees are guest calls unless named as hooks) |
|---|---|---|
| default | `0x46082f` | key method `+2Ch(V, code)` of V = the player `[0x4d24e6]` if dword `0x49ef34` ≠ 0, else the view subject `[0x4d2684]` (EAX = V, EDX = vtable, ECX = code, ESI = code); EBX ← its EAX. For the vehicle vtable this is `vehicle_keys` `0x4157c8`, which returns its caller's ESI high word over 1 (through `weapon_keys`) |
| 1 Esc | `0x46069f` | camera `[0x4d24ea]+4Ah` = 0: `0x42a3f4(D = [0x4d24c0], 3)`, word `0x4d251a` ← 2 |
| 0Eh Backspace | `0x46053f` | word `0x4d2504` ← 1, dword `0x4d2a70` ← 0, byte `[[0x4d24e6]+202h]+14h` ← 0; `0x429828(D)` (result unused), `0x4610ad()` (gadget `[[0x49b088]+1E1h]`); byte `+B3h` ≠ 0 → `0x441fa4(gadget)` |
| 0Fh, 1Ch | `0x4604e1` | dword `D+14h` = 4 → `view_pref_toggle` `0x45fe38()` (EAX = the code's low word) |
| 10h Q | `0x4605ea` | status s = `status_eval` `0x413180(P, 1)` (EAX = 10h); ESI = s, EDI = sx(SI); `0x47749c(57Eh)`; non-null → try-state 14h, `0x455950(obj, EDI, 1, 0)`, counter `[[0x6cbee4]]` −= 0Ah, try-state 8; modal virtual `+8(obj)`; EDI = (its EAX == the sign-extended word `0x49f5c0[sx(SI)]`); obj ≠ 0 → counter += 9, try-state 2Ch, `0x4543b0(obj, 3)`, try-state 20h; word `0x4a9e68` ← 0 |
| 14h T | `0x46058e` | `0x429828(D)`, `0x4610ad()` → G, ESI = G; `0x441fa4(G)`; AL ≠ 0 → `gauge_state` `0x442120(G)`; byte `[result+2]` = 0 → the Backspace writes (centering on) |
| 19h P | `0x460759` | `0x47749c(57Eh)`; non-null → try-state 74h, `0x4561dc(obj, 0, 1, 0)`, counter −= 0Bh, try-state 68h; modal `+8(obj)` (ESI = obj), EDI = its EAX; obj ≠ 0 → counter += 9, try-state 8Ch, `0x4543b0(obj, 3)`, try-state 80h |
| 28h `'` | `0x460812` | `cockpit_nearest` `0x4333c8([0x49b088])` |
| 2Bh `\` | `0x4604fb` | ESI = P; dword `0x4d2a70` ← 1, word `0x4d2504` ← 0, byte `W+14h` ← 0; twist virtual `+3Ch(P)`; word `0x4d2a74` ← word `P+10h` − AX |
| 2Fh V | `0x4603d9` | camera `+4Ah` = 0: word `0x4d2534` ← 0; view mode word `0x4d24ee` 1, 3, 4 → `0x42a3f4(D, 3)`, `0x4d251a` ← 2 (EDX = 0, −2, −1); 2 → `0x42a3f4(D, 2)`, `0x4d251a` ← 3 |
| 31h N | `0x460446` | camera `+4Ah` = 0, `D+14h` = 4, mode 3: from the view subject, `list_next` `0x411e40(0x4a9af8, cur)` until an entry with `+2Eh` ≥ 0 and the player's group `+45h`; ≠ subject → `view_subject_set` `0x45df34(entry)`, camera `+36h` ← 1 if `+4Ah` (reread after the call) = 0, `0x4d251c` ← entry, `0x4d2520` ← 2 |
| 35h, 835h `/` | `0x460821` | `0x4669c4(0x45f070())` (WinHelp) |
| 58h, 219h F12, Alt+P | `0x4607d7` | `0x45cff0()` |
| 21Fh Alt+S | `0x46039e` | cheat flag `0x49ef38`, tape playback `0x4d24d6` or recording `0x4d24d8` set → toggle the freeze word `0x4d24f2` |
| 24Eh Alt+Num+ | `0x4601f0` | flag → `0x4d24fc` ← 1, `0x4d24f2` ← 0 |
| 402h Ctrl+1 | `0x4607de` | `0x477904()`; byte `+4` ≠ 0 and byte `0x4d1f46` = 1 → word `0x49a06e` ← 1 if ≤ 0, else −1 |
| 410h Ctrl+Q | `0x4606cd` | as P with `0x4561dc(obj, 1, 1, 0)` and try-states 44h/38h/5Ch/50h; DI ≠ 0 → word `0x4d24fe` ← 1 |
| 414h Ctrl+T | `0x460377` | flag → toggle word `0x4d24f0` |
| 419h, 431h Ctrl+P/N | `0x460214` | flag, camera `+4Ah` = 0: from the view subject, `list_prev` `0x411e60`/`list_next` `0x411e40(0x4a9af8, cur)` (the test is `cmp si,431h`) until `+2Eh` ≥ −99000; ≠ subject → `0x4d251c` ← it; mode 2 → `0x42a3f4(D, 2)`, `0x4d251a` ← 3; else `0x45df34(it)`, `0x4d2520` ← 2; then `0x4d24f0` ← 1 |
| 421h Ctrl+F | `0x4602e1` | flag, camera `+4Ah` = 0: `0x4d2534` ← 1; mode 1 → `0x42a3f4(D, 3)`, `0x4d251a` ← 2; 2 → `0x42a3f4(D, 2)`, ← 3; 3 → camera `+36h` ← 0 if `+4Ah` (reread) = 0, `0x4d24ee` ← 1 |
| 620h Alt+Ctrl+D | `0x46016e` | flag → view subject S: `+74h(S, EDX with DX = word 0x4d2500, 12Ch, P)` (EAX = vtable, ECX = S) |
| 631h Alt+Ctrl+N | `0x4600bf` | flag → for i < word `0x4a9afe` (signed) over `[0x4a9af8]` (a null entry ends the loop): skip a group with `+14h` ≠ 0, an actor with byte `+99h`, a group side byte `+12h` ≠ 1; `dist_gate(P+26h, A+26h)` < 100000 → `+74h(A, 0, 7D00h, P)` and stop |
| 633h, 634h Alt+Ctrl+, . | `0x4601a2`, `0x4601c9` | flag → word `0x4d2500` −1 if > 0 / +1 if < 1Dh |
| 82Fh Shift+V | `0x46009f` | camera word `+36h` = 1 → `0x401148(camera, 3)` |

The entry registers of every callee (EAX/EDX/ECX, including the
remainders of the compare chain and the reloads of the listing) are
reproduced; `pop ecx` after each one-slot call reloads the slot.

### The split

The whole body runs as own code: the enable test, the compare chain and
the jump table, every global write (freeze, step, view words, damage slot,
centering and align words, the lever word, the quit and alert words), the
frame and try-state bookkeeping, the counter adjustments around the
dialogs, the list loops of N, Ctrl+N/P and Alt+Ctrl+N, and the status
match of Q. The simulation effects are the cheat and alert cases
(Alt+Ctrl+N/D damage through `+74h`, the freeze and step words, the align
and centering words, the lever, Q's outcome result), plus the hooked
callees. **Guest calls (presentation, platform, RTL):** the view requests
`0x42a3f4`, `0x401148`; the dialogs `0x47749c` (heap), `0x455950`,
`0x4561dc`, the dialog virtual `+8`, `0x4543b0`, `0x45cff0`; WinHelp
`0x45f070`/`0x4669c4`; the lamp `0x429828`, `0x4610ad`, `0x441fa4`; the
joystick summary `0x477904`; the runtime `0x49630c`; and under `0x45fe6c`
the display switch `0x4667c8` and the debug trap `0x467ad8`. **Hooks
reached through the bridge** (own modes): `hud_key`, `playback_key`,
`dist_gate`, `status_eval`, `cockpit_nearest`, `view_subject_set`,
`view_pref_toggle`, `gauge_state`, `list_next`, `list_prev`, and through
the vtables `vehicle_keys` (`+2Ch`) and `vehicle_part` (`+74h` of
vehicles).

### Replacement

- **Bridge with callee-saved registers.** The key method `+2Ch` returns its
  caller's ESI high word and `0x45fe6c` its caller's EBX high word, so
  `command_key`'s bridge (`commands_machine_call`) loads the EBX, ESI and
  EDI the original holds at each call (the core writes them into
  `es2_commands_saved`, as `player_input` does), except for `0x49630c`,
  which runs with the original EBP through the stack bridge. The adapter
  restores `FS:0` to the record's saved head on exit, as the epilogue does.
- **`command_key` entry shim.** Installed, the replacement runs at the
  original stack coordinate: frame and record at the original EBP−5Ch/−38h,
  the calls at EBP−68h with the original's EBX/ESI/EDI and return
  addresses, the caller's EBX and EDI at `0x49630c` and `0x432bc8`; the
  record address that reaches the callees in EDX is the original's
  ([Stack residue under the command handler](#stack-residue-under-the-command-handler)).
  Direct calls without the shim keep the plain bridge and a synthetic frame
  in the adapter's scratch.
- **`playback_key` entry shim** (`commands_playback_entry`, chained from
  `transform_helpers_capture_entry`; `commands_capture_entry` continues the
  shim chain): AX ← the caller's EBX high word (EBX and EAX's high word
  unchanged), then the framework's dispatch. The entry AX is overwritten at
  `0x45fe74` before any read and the flags at `0x45fe78`, so the shim
  changes nothing for a pass-through original. The adapter passes the
  entry EAX high word to the callees and the EBX high word into the result.
  The two callees read no callee-saved register (`0x4667c8` saves
  EBX/ESI/EDI/EBP before writing them; `0x467ad8` is `int3`) and go through
  the plain bridge.
- **Framework.** `0x45fe6c`'s prologue holds `66 BB 01 00` (MOV BX,1); the
  hook's 8 relocated bytes include it, so the prologue whitelist of
  `hook/hook.c` accepts `66 B8+r imm16` (four bytes) (fixture cases in
  `hook/tests/hook_fixture.c`, hook README).
- Both hooks refuse compare mode (game calls, dialogs, allocation).

### Stack and residue

No local of either function is read before it is written (checked in vitro:
the original runs over a per-case stack fill, the adapter over a zeroed
scratch). Without the shim the synthetic frame lives in the adapter's
scratch, so the record address that `hud_key` returns in EDX (and that
reaches the case handlers' callees as their entry EDX) is a scratch address
instead of EBP−38h. Of the callees that receive it, `0x401148`,
`0x429828`, `0x477904` and `0x47749c` (with its first callee `0x476edc`)
were read and write EDX before reading it; the dialogs, WinHelp,
`0x4333c8` and `0x45cff0` were not read for this. The one place it is data
is the high word of Alt+Ctrl+D's slot argument, which `vehicle_part`
`0x417de4` reads as a word only (`0x417e19`, `0x417ef4`).

Residue below the callees is read: the group order `0x4231a4` under
`hud_key`'s MFD forward reads its priority word uninitialized for Alt+O,
which is why the installed hook runs at the original stack coordinate
(above).

### Unreachable edges

Seven of the 88 `command_key` sites are one-way (`PROVED` in
`branch_sites.h`): the three dialog allocations `0x460610`, `0x4606dd`,
`0x460769` (a null object is dereferenced at `0x460647`/`0x46070f`/
`0x46079b` in the original), the later non-null tests of the same objects
`0x46066c`, `0x46071e`, `0x4607aa`, and `0x460361` (Ctrl+F mode 3 rereads
camera `+4Ah`, tested zero at `0x4602f3`; the only store in between is word
`0x4d2534`). The other 81 sites, all five of `playback_key` and all 29
jump-table entries are taken both ways in vitro.

### Live facts

Counter relations of the replaced hooks: `hk_command_key_calls` =
`hk_player_input_call_45feb8` + `hk_control_record_call_45feb8`, equal to
`_call_49630c` and `_call_432bc8` (`0x4d2564` is set throughout the
missions); `_slot_2c` ≈ `hk_vehicle_keys_calls` (`player_input` calls no
`+2Ch` in the suites); `hk_playback_key_calls` =
`hk_command_key_call_45fe6c` (the calls `hud_key` does not handle);
`_call_4667c8` = `_call_467ad8` = 0. The c1_01 success script presses Q
eight times (`_call_413180`, `_call_47749c`, `_call_455950`, `_slot_8`,
`_call_4543b0` 8 each in c1s) and T once (`_call_429828`, `_call_4610ad`,
`_call_441fa4` ≥ 1); the recordings press T or Backspace (map probes
`0x441fa4` 0/1/0/6/2/0). With all hooks replaced, and with the compare-mode
hooks in `compare`, the six suites are identical.

### Evidence

In vitro (`differential_commands_test`): per hook the direct cases against
logging stubs for every callee (arguments, entry EAX/EDX/ECX, EBX/ESI/EDI
except where `ck_mask` in `commands_cases.h` names a register the callee
does not read, the slot count the bridge pushed), the arena, the image
globals, the callee-counter deltas and `FS:0`; the jump table through
counting trampolines; 256 installed 31/a7 poison cases with random
EBX/ESI/EDI (through the shim); 6,144 real-callee cases of the command
handler over the real `hud_key`, `playback_key`, `dist_gate`,
`vehicle_keys`, `status_eval` and `cockpit_nearest` (the seven hooks
original/replaced in all 128 combinations; their own callees stubbed except
the getter `0x43dbb0`, which `cockpit_nearest`'s core computes inline; the
record address normalized, since it is a stack address in one run and
scratch in the other; the entry ECX of `0x47749c` after a replaced
`status_eval` is ignored, because the framework returns that hook's entry
ECX and `0x47749c` writes ECX before reading it or hands it to the
runtime). The direct cases cycle every jump-table code and every handler
through the first 16,384 cases, so every site shows both outcomes within
about 3,000 of the 32,768 coverage cases. The frame probe of the shim is in
[Validation](#validation). Core test `//core:commands_test`.

### Open

- The other `+74h` implementations (buildings, flyers) and the callees
  named above as not read were not checked for the record address in EDX.
- `0x467ad8`'s `int3` with option word `0x4d252c` set would raise a
  breakpoint exception with the record at the head of the SEH chain; the
  option's writer was not traced and the suites never set it.

## Stack residue under the command handler

The hooks `player_input` (`0x460870`), `command_key` (`0x45feb8`) and
`hud_key` (`0x432bc8`, [hud.md](hud.md)) run at the original stack
coordinate, because an original callee below them reads stack residue.
Read from the listing and checked in vitro; no hook is added by it.

### The live difference

With the three hooks running from host stack, the suite
`c3_09-wingman-orders` differed from the original (`bisect_hooks.py`:
`player_input` alone already fails). First differences: RNG stream B from
frame 151 (the original draws once there and again in frames 179–190, the
replaced run does not), the `team2_pos` watch at frame 1484, then the
player's position, speed and throttle from frame 1541 (a collision with the
displaced wingman). Frame 151 is the `O` of the script's first Alt+O (code
`0x218`), 1481 its second; every other order of the script (Alt+C/E/F/H/G/A,
the HDD map orders) gives the same team targets and draws in both runs. With
the hooks on host stack the Alt+O frame shows `hud_key`'s call of the MFD
forward `0x446c38` and no order (`hk_mech_order_calls` 0).

### The residue reader: the group order `0x4231a4`

Alt+O reaches `0x446c38` (item 3, `0x446d1d`) → `0x447248`, which stores
`0x43f9c0(menu)` (3, or 6 with bit 1 of the item's flag byte) as the order
code in `0x4d03d4` → `0x43166c` → `0x4231a4(group [0x49b0f8], 0x4d03d4)`
(its only call site; `0x43166c`'s only caller is `0x447248`). For each
eligible member `0x4231a4` sets a priority word EBP−1Ch through the byte
table `0x423239` and the jump table `0x423249`: codes 0, 2, 5 → byte
`[+4Dh]+9` = 0 (`0x42325d`), 1, 8, 0Fh → that byte (`0x42326c`), 4 → byte
`+96h` = 0 (`0x423272`), 7 → byte `+96h` (`0x423288`); **codes 3, 6 and
9–0Eh jump straight to `0x423298`, which reads EBP−1Ch without writing
it**. A member is skipped while that word is below the running best
(`cmp ax,[ebp-1Ah]; jl`; the best starts at −1 and every pass resets it at
`0x4231cd`), so for Alt+O the order is given (in distance order, to every
eligible member) when the residue word is ≥ −1 and refused when it is
below. The word lies at 460870's EBP−1CCh (45feb8's EBP = EBP−0A4h, 432bc8
−124h, 446c38 −170h, 447248 −184h, 43166c −1A0h, 4231a4 −1B0h); nothing on
that chain writes it first, so it holds what the logic frame's earlier rows
left there (≥ −1 in the golden).

### Why the replaced hooks differ

A hook that calls `0x45feb8` (or `hud_key`'s callees) through the plain
guest bridge runs them below the framework's and the adapter's frames, so
`0x4231a4` runs at another address and reads host bytes. Also not exact on
the plain path: the caller's EDI at `0x45feb8`/`0x432b14`, the caller's
EBX/EDI at `0x49630c`/`0x432bc8`, and `hud_key`'s EBX/ESI/EDI at every call.
In vitro, with the plain bridge, the original's Alt+O follows the stack fill
(3131h: given, A7A7h: refused) while the replacement follows host bytes.

### The fix: the three hooks at the original stack coordinate

Each hook gets an entry shim, installed in the capture chain without
changing its order (`player_input` in `ai_behaviour_capture_entry` beside
`torso_aim`'s, `command_key` and `hud_key` in `commands_capture_entry`
beside `playback_key`'s), as for `aim_fire` and `torso_aim`. The shim
pushes EBP (the original's), stores the caller's EBX/ESI/EDI in the
prologue's save slots, reserves a guard-probed gap and publishes {EBP,
outgoing ESP}; the core's frame is then the original one and every game
call runs through `ai_behaviour_stack_call` at the original's outgoing ESP
with the EBX/ESI/EDI, EBP and return address the core names.

| Hook | Frame | Saves (EBX/ESI/EDI) | Outgoing ESP | Gap | Core entry |
|---|---|---|---|---|---|
| `player_input` 460870 | 8Ch | EBP−90h/−94h/−98h | EBP−98h | 64 pages | `es2_player_input_frame_registers` (caller's EDI) |
| `command_key` 45feb8 | 5Ch | EBP−60h/−64h/−68h | EBP−68h | 48 pages | `es2_command_key_frame_registers` (caller's EBX, EDI) |
| `hud_key` 432bc8 | 30h | EBP−34h/−38h/−3Ch | EBP−3Ch | 32 pages | `es2_hud_key_frame_registers` (caller's ESI) |

- **Registers and return addresses** (from the listing, per call site):
  460870 as in [player_input](#player_input-460870), with the caller's EDI
  at its first two calls (`xor edi,edi` at 4608d1 follows them); 45feb8 as
  in [Contract of `0x45feb8`](#contract-of-0x45feb8), with the caller's EBX
  and EDI at `0x49630c` and `0x432bc8`; 432bc8: EBX = H (the F11 dialog
  object at its `+8` and `0x4543b0`), EDI = the code, ESI the caller's up
  to the first `0x429820`, then the display mode M, which the Esc and F1–F6
  dispatch leave as M−1 (M ≤ 1) or M−4, Tab and the default chain as M−1.
  No call of 460870 or 45feb8 has another call's slots above it; 432bc8
  pushes the display request before calling `0x429820` (432d22, 432d5c,
  432d75, 432d93, 432dac, 432dca, 432e1b), so that getter runs 4 bytes
  lower over the request slot.
- **`hud_key` call registers (also on the plain path):** `0x429820` is a
  guest call (with the original frame its return address is residue the
  original leaves); the entry EAX/EDX/ECX at the calls are the listing's:
  EAX at `0x446c38` is the compare chain's remainder (code−0Ch, −1Ch, −43h
  or −220h by range; Alt+O: FFFFFFF8h, not the code), ECX the MFD; F1–F6
  pass the request in ECX and the MFD in EDX; F7/F8 EDX = `[H+1F9h]`, ECX =
  byte `0x4d2538`−1; F9/F10 keep EDX/ECX; Tab in mode 1 has EAX = the
  gadget and EDX its vtable, as the `[H+1D8h]` offer and the second
  `[H+1F9h]` offer (ECX 0); the other offers have ECX = the gadget and EAX
  its vtable; Enter ECX = the MFD (EDX too in MFD mode 4); F11 enters
  `0x47749c` with EAX = 0, reloads ECX by `pop ecx` (3B8h, later the dialog
  object), ECX = `[6CBEE4h]` at the dialog virtual and EDX = `[6CBEE4h]` at
  `0x4543b0`. The mode-5..7 Esc/F1–F6 request words are read from the
  original's frame.
- **The runtime frame:** `0x49630c` runs through the stack bridge with the
  original EBP, so 45feb8's record lies at EBP−38h and 432bc8's at EBP−28h
  of the game's stack, chained into `FS:0` as in the original; the adapters
  restore `FS:0` from the core's head on exit.
- **Gaps:** 432bc8's callees (the MFD and gadget methods, the button and
  target hooks, a compare-mode nested hook's 64 KiB) fit 32 pages; 48 for
  45feb8 hold hud_key's gap and host frames, the key method and the
  dialogs; 64 for 460870 hold command_key's 48 pages and host frames and
  torso_aim's 48 (torso_aim's 192 KiB below the logic frame lie inside
  player_input's gap; the deepest path of the logic frame stays the AI
  step's, about 340 KiB of the 1-MiB reserve). Live headroom (the
  Q/P/Ctrl+Q dialogs run inside command_key's 192 KiB) is not measured.
- **Without a shim** (direct calls) the adapters keep the plain bridge and
  their own frames.

**Remaining differences, accepted** (as for `aim_fire` and `torso_aim`): a
replaced callee's PUSHFD saves the bridge's flags, and the host frames below
the gaps are not the original's. The bytes `0x4231a4` reads are the
original's only as far as the logic frame's earlier rows (control_record,
the pools, the projectile, building and group updates, several of them
replaced without a shim) leave the original's bytes at that address; in the
bisect runs with `player_input` original those rows gave the original's
decision.

### Validation

In vitro:

- `player_input` (`input_dispatch_cases.h`): frame probe, 2,048 cases
  (fast/preserving dispatch, 31/a7 stack, the script's key codes in three of
  four): all callees logging stubs that record EBX/ESI/EDI and EBP/ESP
  relative to the caller's slots, the command handler's stub also the 1,024
  bytes below its entry; records, pair, arena, globals and the 1,024 bytes
  below the slots equal; a plain-bridge adapter fails at the first call (EDI
  and coordinates). Order-chain probe, 2,048 cases over the real 45feb8,
  49630c, 432bc8, 429820, 45fe6c, 446c38, 43f9e0/43f9c0/43f9f8, 447248,
  447158, 43166c and 4231a4 (the members' `+28h`, dist_gate, memset, the
  gadget offers and the key method stubbed), with player_input, command_key
  and hud_key in the seven replaced combinations: records, arena, order
  record, globals, FS:0 and window equal; the original gives Alt+O over the
  3131h fill and refuses it over A7A7h.
- `command_key` (`commands_cases.h`): frame probe, 394 cases (every code and
  handler), EAX..EDI unmasked and EBP/ESP per stub, FS:0, window.
- `hud_key` (`hud_keys_cases.h`): frame probe, 2,048 cases (the direct
  cases' codes and the script's keys, display modes 0..7; the getter is a
  logging stub there so its 4-byte-lower call is seen), EBX/ESI/EDI and
  EBP/ESP per stub, FS:0, window.
- The direct, installed (through the shims) and real-callee passes of the
  three groups; the core tests `//core:input_frame_test`,
  `//core:commands_test` and `//core:hud_keys_test` check the frames.
  Mutation witnesses: `check_mutations.ps1`.

Live: `c3_09-wingman-orders` is identical with all hooks replaced; the
first Alt+O gives the order at frame 152 (`hk_mech_order_calls` > 0 there).

### Other input-side hooks on the script's paths

Checked for the same class (a replaced hook whose original callees read
stack residue from host stack): the only call chain of `0x4231a4` is the
MFD one above, at the original coordinate end to end. For `key_message`,
`flight_key`, `control_record`, `key_axes`, `vehicle_keys`, `weapon_keys`,
`wingman_order`, `mech_order`, `map_click` and `map_pick` the live run with
all hooks replaced shows no further difference on this script: the HDD map
orders (1/2/3, D/A/F/T/G/O/C/E, Enter, Tab, X in frames 650–960, 1100–1119
and 1400–1463), the other group orders and the throttle keys give the
golden's team targets and positions. Their original callees were not
audited statically for residue reads.

## The targeting computer 40e4dc and 40e4ac

Group `cold_paths` (`proxy/cold_paths_hooks.h`), core `core/src/input.c`.
Contracts from the listing, checked by the in-vitro differential. W = the
TARG equipment object `[P+30Bh]`, T = the target. Every game call goes
through the weapons guest bridge with the original's EAX/EDX/ECX, so nested
hooks keep their modes; ECX after each is the entry ECX (restored by the
framework), dead at the callers.

### `targ_next` 40e4ac `(W, T)`

`T` null (site `0x40e4b9`): return with EDX:EAX = entry. Otherwise the
target's virtual `+80h` with (T, sx(word `W+7Dh`), `W+86h`) (EAX = `W+86h`,
EDX = sx(word `W+7Dh`), ECX = `[T]`); its AX is stored into word `W+7Dh`.
EDX:EAX = the virtual's. One conditional site, both ways.

### `targ_aim` 40e4dc `(W, T, out, unused, p5, p6)`

The fourth stack slot is never read. Two cases:

- **Lock path** (word `W+7Dh` > −1 and word `W+7Fh` < 155, sites
  `0x40e4f1`/`0x40e4fd`): the part liveness virtual `+84h`(W+86h) (EAX =
  `W+86h`, ECX = `[T]`); a zero result calls `targ_next(W, T)`
  (`0x40e51c`). Then the part position virtual `+58h`(T, low word `W+86h`,
  out) (EAX = `[T]`, ECX = word `W+86h`). With `p6`: `*p6` ← word `W+86h`.
  With `W+7Fh` > 104 the damaged-computer timer `timer_dword_step(W+81h)`
  (`0x467aa8`); when it expired (AX = 0) `W+82h` ← 5000 and word `W+7Dh` ←
  −1. With `p5`: `*p5` ← 1. EDX:EAX = the timer's or the virtual's.
- **Fallback** (otherwise): the target's aim node — virtual `+24h`(T) (ECX
  = T). A nonzero node: the lazy pose at `B = T+0Ch` (if word `B+26h` = 0,
  `euler_matrix(B, B+6)` and set it), then `transform_point(node+14h, B+6,
  out)`. A null node: copy the three position dwords `[T+26h..+31h]` to
  `out`. With `p5`: `*p5` ← 0. EDX:EAX = the last callee's; EDX is a frame
  residue of euler/transform on the node path, dead at aim_point's callers.

Ten conditional sites, all covered both ways in vitro.

### Reachability

`targ_aim` is bridged only by `aim_point` (`0x41b728` → `0x41b779`), which
needs a player weapon object `[P+30Bh]` and a target closer than 30,000: a
mission where the player mounts the TARG weapon (ID 29, see
[weapon_classes.md](weapon_classes.md)) and locks a target. `targ_next` is
also reached by the HUD Tab key handler `0x432ee9` ([hud.md](hud.md), key
0x0f). The suite
[`c3_10-hunt-targ`](../reference-game/oracle_suites.md#c3_10-hunt-targ)
mounts TARG and enters both live; with all hooks replaced it is identical
to the original. The six other map suites never enter them.

## The input layer as own code

The input functions the own `dbsim.exe` reaches without the original: the
input set-up `0x45a5dc` that the mission-load root calls first, its device
objects and prefs read, the device poll `0x45baa8` with its readers, the
joystick, the mouse handler and queue, and the platform imports below them
behind one provider seam (`sim/src/input.c`, `sim/include/es2/sim/input.h`);
the host side is [own_process.md "The input layer"](../reference-game/own_process.md#the-input-layer).
**Confirmed** = read from the listing; **in vitro** = the original's machine
code and the own code gave the same bytes
(`//tools/reference-game/invitro:differential_input_layer_test`,
`input_layer_cases.h`). Not validated live.

### What is own code

| Original | Own code (`input.c`) | Checked |
|---|---|---|
| input set-up `0x45a5dc..0x45a80d`: `new(0xe)` keyboard K, `0x45c01c(K, 0x10000)`, counter − 2, `0x45be50(K+4)`, hot key `0x477b7c(K, 0x477a7c, 0x49eabc)`, flight-key capture `0x45b68c(1)`, `new(0x3f)` mouse M, `0x45c1d4(M)`, counter − 2, `0x45be50(M+0x3a)`, prefs read `0x45972c`, device poll, `[0x4d2390]` = D and the record's pointer tables `0x4d2310..0x4d238c`, bytes `0x4d22f9..0x4d22f6` ← 0, `keyjoy_load` | `es2_sim_input_setup`; row `input_setup` | in vitro (2,000 set-ups) |
| device registry `0x45bda4` (register), `0x45be50` (activate), `0x45bee0` (deactivate), `0x45bf04`, `0x45bf40`, `0x45bf68`, `0x45c524`; graphics-library slot `0x480590` | static helpers; row `joystick_object` (`0x45c524`) | in vitro |
| keyboard device `0x45c01c` / `0x4779a8` / `0x477a4c`, reader `0x45bbc4` → `0x45c0a0` → `0x477ad4`, flush `0x477abc` | `keyboard_ctor`, `es2_sim_key_reader`, rows `key_reader`, `key_flush` | in vitro |
| mouse device `0x45c1d4` / `0x4805e0` / `0x480694`, reader `0x45bc74` (→ `0x45c248` → `0x480834`), queue set-up `0x452ae4` (with `0x480848`), queue callback `0x452cdc`, window handler `0x4809f8`, event `0x480948`, scale `0x480898`, bounds `0x45b758` (with `0x480738`, `0x480764`) | `mouse_ctor`, `es2_sim_mouse_*`; rows `mouse_handler`, `mouse_event`, `mouse_button`, `mouse_queue_setup`, `mouse_bounds` | in vitro (2,000 message sequences; bounds in the poll cases) |
| joystick device: prefs part `0x459df0`, constructor `0x45c298` / `0x4775dc` / `0x477674`, destructor `0x45c2e0` / `0x477630` / `0x477714` (through `0x496406` and the vtable `0x49ec74`), poll `0x45c544` → `0x477720`, reader `0x45bc10`, poll virtual `0x45c330` with `0x47785c` / `0x4778a8`, summary `0x477904`, map `0x45a274` | static helpers; rows `joystick_summary`, `joystick_map` | in vitro (212 set-ups with an active device, 1,317 polls with it) |
| prefs read `0x45972c` / `0x45a1b8`, setter `0x459958`, callback slot `0x459c74`, voice-archive test `0x459d88` (with `0x45ef84`), callbacks `0x459cb4`, `0x459c88`, `0x459ce0`, `0x459d3c`, `0x459d68` | `prefs_read`, `es2_sim_prefs_set`; rows `prefs_set`, `prefs_sound_a`, `prefs_sound_b`, `prefs_flag_49f966`, `prefs_lever`, `prefs_flag_4aaaa8` | in vitro |
| the sound flags `0x462d40`, `0x462d5c` (audio's; reached by two prefs callbacks), the empty constructors `0x4531b5`, `0x49545b`, the runtime's array constructor `0x49667c`, the surface attribute `0x481350` (graphics library) | rows `sound_flag_a`/`_b`, `empty_ctor_event`, `empty_ctor`, `vector_new`, `surface_attribute` | in vitro (the poll cases) |
| the read and write streams `0x47ad40`/`0x47af98`/`0x47ad78` and `0x47b064`/`0x47b2b8`/`0x47b09c` as rows for the prefs adapters' bridges (volumes.c has the read stream as static code) | rows `stream_open`, `stream_read`, `stream_destroy`, `stream_write_open`, `stream_write`, `stream_write_destroy` | in vitro (1,000 cases, both streams) |
| imports: `GetKeyState` (thunk `0x49695e`), `joyGetDevCapsA` (`0x496a66`), `joyGetPosEx` (`0x496a6c`), `GetCursorPos` (`0x49696a`), `GetClientRect` (`0x496970`), `GetWindowRect` (`0x49694c`), `GetPrivateProfileStringA` (`0x496910`) | rows over `es2_sim_input_platform` | the journal semantics against a model of the proxy's replay in vitro; the rest unit test |

Calls into other code go through the dispatch: the hooks `prefs_load`,
`prefs_store`, `keyjoy_load`, `mouse_queue` (and, under the window
procedure, `key_message`, `flight_key`, `command_queue`), the services of
the heap, the strings, the files, `path_root`, `handler_add` and the clock,
the sound groups `0x462e58`, `0x462d78`, `0x462efc`, `0x462ddc` (the prefs
callbacks for bytes 0 and 1 outside the prefs load; `audio.c`), the cursor
follow `0x486e7c` and the focus switch `0x4669b4` under the keyboard
handler (row `display_restore`).

### Facts read on the way

Confirmed; in vitro where marked.

- **The keyboard and mouse devices never configure themselves.** Both
  constructors test bit 0 of the device's flag dword right after the base
  object `0x480590` stored 0 there (`0x477a5e`, `0x4806a6`), so the
  keyboard's atexit `0x47799c`, abort-notify `0x4779a0` and own-frame hot key
  (`0x4779d5..0x4779fe`) and the mouse's configuration
  `0x48080c`/`0x480738`/`0x480764` and abort-notify `0x480804` never run
  (the keyboard constructor's hot-key registration in
  [Registered hot keys](#registered-hot-keys) is therefore never made, and
  the devices have no atexit functions). The own code refuses those
  branches; none of 6,000 in-vitro set-ups reached one.
- **The device registry** (`0x4d2468`: object per id; bytes `0x4d248c` +
  id: active; current ids `0x4d2498` (0), `0x4d249c` (keyboard), `0x4d24a0`
  (mouse), `0x4d24a4` (joystick)). Registering ids 2 and 3 deactivates the
  previous current record and always stores the new id; activating them
  does both only when a current id exists (`0x45be6f`, `0x45bea2`), else the
  id word stays as it was. A slot of 0 makes the original touch address
  `0x3a` or `0x1f` (a fault); the own code refuses. A destroyed joystick
  leaves its record in slot 3 (the destructor clears only the id word and
  the graphics slot).
- **The graphics-library slots** `0x6c5eb4` + 4k (k = 1 keyboard, 2 mouse, 3
  joystick) decide whether a device registers its window handler or opens
  the joystick: only when the slot was free. With the slots empty at the
  load (BSS) the handler list is keyboard, mouse (the condition of
  [main_loop.md](main_loop.md#the-main-loop-pump-window-procedure-pause-limiter-and-clock):
  both hold when nothing else filled the slots; in vitro both ways).
- **The joystick device** is made by the prefs read for prefs byte `0xc` 0
  or 1 (`0x459df0`): `joyGetDevCapsA(i, 0x6bb378 + 0x194 i, 0x328)` for
  devices 0 and 1 (the size 808 overlaps the next buffer) into the
  results `0x6bb6a0`/`0x6bb6a4`, `joyGetPosEx(i)` after a successful caps
  call, the device's bit 0 when device 0 answered. Without it the device is
  destroyed (byte 1 then clears prefs bytes `0xd..0x18` through the
  callbacks); with it, byte 0 writes the two button maps of `0x45a274` into
  bytes `0xd..0x24` and stores them (`prefs_store(0x19, 0x49e9a8)`), and
  both activate the record. In vitro 212 of 775 set-ups with byte 0 or 1 had
  the device active.
- **The joystick poll** (`0x45c330`, response mode `[J+0x28]` = 1 from the
  constructor): the axes scaled to ±0x80 by `0x47785c`, then in mode 1 a
  dead zone of 25 and a quadratic map with `0x47e068(1, ((1 << 7) − 25)² >>
  8)`; modes 0 and 2 a linear dead zone and scale; the change byte
  `0x4d2466` is the low byte of `_memcmp`'s result. In vitro (1,317 polls with
  the device).
- **The mouse handler** answers 1 (the window procedure stops) for
  `WM_MOUSEMOVE` and the left button, 0 for the right button; `WM_SIZE`
  rescales. The event's button word sets bit 0 for the left and bit 1 for
  the right button; the move event's bit 1 is never set (`(wParam & 1) + 2 ×
  (wParam & 2)`). The scale `(DIB size << 15) / client size` is computed at
  the first event from `GetClientRect` (in vitro, 12,820 messages, 3,521
  button events queued).
- **The prefs setter's callbacks** get EAX = EDX = the index sign-extended
  and ECX = the callback (`0x45998a..0x459995`); `0x459d68` returns that EAX
  for a nonzero byte, so the setter's EAX is the index (in vitro).
- **`0x45a5dc` returns** EAX = the FS:0 head its frame record saved; EDX
  and ECX are `keyjoy_load`'s. The root does not read them.
- **keyjoy.cfg's path** is the install root `[0x4d27bc]` + `data\keyjoy.cfg`
  (`keyjoy_load` calls `0x45ee00` first), so file_io.md's relative-name
  question arises only with a relative root in `drive.cfg`.
- **DBSIM has no menu resource** (the resource directory holds types 3 and
  14 only), so the class's menu 1 is never loaded and the client area is
  the window's minus frame and caption.

### Stack residue

Three readers of uninitialised locals, modelled 0 (rows in `residue.c`,
[own_process.md section 5](../reference-game/own_process.md#5-stack-residue-as-modelled-values)):
`0x45b758` copies three words of its local (+0x10..+0x15) to M+0x1e..+0x23
(in vitro the original wrote stack bytes there: masked); `0x45baa8` copies
its 0x1d-byte joystick local into D+0xd.. even when the reader `0x45bc10`
did not write it (an inactive record or a failed poll); `0x45c330` scales
its local when the response mode is above 2 or the device's bit 0 is clear.
The last two need a registered joystick; of the oracle suites only the two
joystick suites have one.

### Headless platform

- `GetKeyState`, `joyGetDevCapsA`, `joyGetPosEx`, `GetCursorPos`: the
  journal as the proxy's replay answers (`input_journal.h`): the entry of
  the frame and key, else the virtual key state, `JOYERR_PARMS`,
  `JOYERR_UNPLUGGED`, the virtual cursor; fallbacks counted.
- `GetClientRect`: the game size `[0x4d253e]` × `[0x4d2542]`, what the
  window was made for ([launcher.md](launcher.md), DBSIM's process shell).
  **Deviation, open:** on the recording machine the real client area may
  differ by the frame metrics (Windows 10's padded border is not in
  `SM_CXFRAME`); the mouse scale and so the mouse suites' cursor positions
  depend on it. `GetWindowRect`: the creation rectangle `0x4d36cc`.
- `GetPrivateProfileStringA`: formats/cfg.md's INI subset over the file the
  game names (the host reads it); the default when the file or key is
  missing. The shipped `KEYJOY.CFG` says `Default` for all four keys.

The windowed host answers the joystick from winmm or SDL3
([The SDL3 joystick](#the-sdl3-joystick)).

### Acceptance

- **In vitro** `differential_input_layer_test`: set-up 2,000, poll 2,000
  (each up to six polls with the readers, the prefs setter, the flush, the
  bounds, the array constructor and the surface attribute in between),
  mouse 2,000, streams 1,000 cases identical: DATA `0x497000..0x6cc000`, the
  emulated heap, the import-level call logs, the journals, the prefs file
  after the case (only after a full 54-byte read: `prefs_store` writes
  stack residue for a shorter file, config.h), EAX and EDX where no import
  is the last writer. Two mutations caught.
- **Unit** `//sim:input_test` (synthetic image).

### Interfaces

- The root's input calls are rows (`0x45a5dc`, `0x452ae4`, `0x45b758`,
  `0x477abc`), as are `GetWindowRect`/`GetClientRect`.
- The profile row serves `sound.cfg` (`0x46379c`) too; the sound flags
  `0x462d40`/`0x462d5c` are rows here.
- `es2_sim_input_platform` is the seam for keys, joystick, cursor and the
  client size; window messages come from the pump's journal source in
  `loop.c`, which the windowed host feeds from SDL3 events.

### Open

- The recording machine's client area (above), the joystick suites'
  residue reads (no live check).

## The input rest

Verdicts for the input VAs of [sim_gap.md](../reference-game/sim_gap.md)
that [The input layer as own code](#the-input-layer-as-own-code) does not
list (prefs path and read, voice VOL test, joystick defaults, `0x45a274`,
the device objects `0x45bc74..0x45c544` and `0x4775dc..0x477b7c`), from the
listing and a scan of the image for dword references to each VA (code,
data, the exception tables). **Confirmed** = read from the listing; **in
vitro** = the original's machine code and the own code gave the same bytes
(`//tools/reference-game/invitro:differential_joystick_prefs_test`,
`joystick_prefs_cases.h`). Not validated live.

### Two rows (`sim/dispatch/services/joystick_prefs.h`)

| Row | VA | Own code (`input.c`) | Why a row | Checked |
|---|---|---|---|---|
| `joystick_map` | `0x45a274(set)` cdecl | `joystick_map` (returns registers) | the joystick page `0x45866c` calls it at `0x458ad7` | in vitro |
| `prefs_write` | `0x459b80(index)` cdecl | `es2_sim_service_prefs_write` | the frame end in `loop.c` calls it at `0x45f504` (sim_gap's only INP "called" root) | in vitro |

- `0x45a274` (`0x45a274..0x45a2d0`): calls the joystick summary
  `0x477904`; if the summary's word is 0 it clears the 12-byte map
  `0x4d20b5` with `_memset(0x4d20b5, 0, 12)` (`0x45a288..0x45a295`, the
  summary's EAX/EDX/ECX live at the call), else it copies the 12 bytes of
  set 0 or 1 from its two tables; any other set leaves the map. Returns EAX =
  `0x4d20b5`, EDX/ECX the summary's (or `_memset`'s). The clearing branch is
  **unreachable**: `0x477904` always stores 1 or 2 into the word
  (`0x477917..0x477925`), confirmed; the own code keeps it anyway.
- `0x459b80` (`0x459b80..0x459b92`): `prefs_store(1, &index)` (the hook
  `0x459b94`) with the address of its own argument slot; at that call EAX
  = that address and EDX/ECX are as at entry; it returns prefs_store's
  registers. Confirmed; in vitro.

In vitro: 3,000 cases of 1..8 calls each on a random joystick state
(`0x6bb6a0`, `0x6bb6a4`, the button counts `0x6bb3b4`/`0x6bb548`, the
capability byte `0x6bb3d8`, the summary `0x6bb6a8`, the map) with random
entry registers; the summary runs as the original's machine code on the
original side (the `joystick_summary` row on the own side); prefs_store and
`_memset` are recording stubs on both sides. EAX, EDX and ECX of every
call, DATA `0x497000..0x6cc000`, the heap and the stub log are identical;
sets 0, 1 and others, summary words 1 and 2 and the store are all taken.
The `_memset` entry registers are not compared: rtl.c's results do not
depend on them. One caught mutation (prefs_store's count 1 → 2).

### Verdicts for the rest of the row

| VA | Verdict | Evidence (confirmed) |
|---|---|---|
| `0x45972c`, `0x45a1b8` (prefs path, read) | own code without row (`prefs_read`) | called only from the set-up `0x45a5dc` (own) |
| `0x459c74`, `0x459d88` (callback slot, voice VOL test) | own code without row | called only from `0x45972c`/`0x45a1b8` |
| `0x459df0` (joystick prefs part) | own code without row (`joystick_prefs`) | called only from the prefs read |
| `0x45bc10`, `0x45bc74` (with `0x45bcb4` inside) | own code without row (readers) | called only from the poll `0x45baa8` (row) |
| `0x45bd84`/`0x45bf40`, `0x45bda4`, `0x45be50`, `0x45bee0`, `0x45bef8`, `0x45bf04` | own code without row (registry) | callers all own (set-up, constructors, poll) |
| `0x45c01c`, `0x45c0a0`, `0x45c1d4`, `0x45c248`, `0x45c258`, `0x45c298`, `0x45c544` | own code without row (device constructors, readers, poll) | callers all own |
| `0x45c2e0` (joystick virtual slot 0, vtable `0x49ec74`) | own code without row (destructor) | called only at `0x459e97`, `0x459fe0` (`[reg+0x24]` in `0x459df0`); also named in the exception table at `0x45c57b` |
| `0x45c330` (slot +4; `0x45c3bd`, `0x45c3e9` inside) | own code without row (poll virtual) | called only at `0x45bc4c` in `0x45bc10`; a scan of every `call [reg+…]` after a `+0x24` load found no other joystick vtable call (`0x459e94`, `0x459fdd`, `0x45bc49` only) |
| `0x4775dc`, `0x477630`, `0x477674`, `0x477714`, `0x477720` (`0x477787` inside), `0x47785c`, `0x4778a8` | own code without row (joystick object) | callers all own |
| `0x4779a8` (`0x4779d5` inside), `0x477a4c`, `0x477ad4`, `0x477b18`, `0x477b7c` | own code without row (keyboard object, hot key) | callers all own |
| `0x45baa8`, `0x45bbc4`, `0x45bd8c`, `0x45bf68`, `0x45c524`, `0x477904`, `0x4779a0`, `0x477a7c`, `0x477a84`, `0x477abc`, `0x459958`, `0x459c88..0x459d68` | rows already | — |
| `0x45bd24`, `0x45bd50`, `0x45bd7c` (→ `0x45bf14`), `0x45bd94` | dead | no call, jump or dword reference in the image |
| `0x4718b4` and its call of `0x477ad4` | dead | its only reference is its own recursion |
| `0x45c058` (keyboard destructor; ref `0x45b9cc`) → `0x477a10` (ref `0x45ba2c`) → `0x477a68`; `0x45c200` (mouse destructor; ref `0x45b970`) | dead in the own process (exception unwinding only) | referenced only from the Borland exception descriptors in code, never called; the own process does not unwind through them |
| `0x45bda0` (`ret`), `0x4775d0` (pool init `0x476dd8(0x6bb2f8)`) | own code without row (`init.c`) | entries of the init table `0x4a7ae8`/`0x4a7b36` |

sim_gap.md's "referenced only" VAs of this row (`0x459d88`, `0x45be50`,
`0x45bee0`, `0x45bbeb`, `0x45bc3e`, `0x45bcb4`, `0x45c3bd`, `0x45c3e9`,
`0x477787`, `0x4779d5`) are the refusal VAs `input.c` names for its own
code, not calls of the original. The virtual roots `0x45c2e0`, `0x45c330`
stay in sim_gap's static reading; their verdict is above.

Open: the joystick page's map with a real joystick is not checked live
([The joystick page and the SDL3 joystick](#the-joystick-page-and-the-sdl3-joystick)).

## The joystick page and the SDL3 joystick

The joystick options page (gadget 9 of the options dialog, `0x457036`),
its helpers `0x457cf8..0x459a10`, and the window's joystick from SDL3 as an
opt-in platform deviation. **Confirmed** = read from the listing; **in
vitro** = the original's machine code and the own code gave the same bytes
(`//tools/reference-game/invitro:differential_joystick_page_test`,
`joystick_page_cases.h`). Not validated live.

### Rows (`sim/dispatch/services/joystick_page.h`, own code in `dialogs.c`)

All rows are cdecl and return EAX. The page object P is 0x480 bytes. Its
vtable `0x49e804` is {draw `0x458c84`, `0x454b74`, run `0x45866c`, select
`0x458ed8`, keys `0x458fb8`, text `0x459148`}. The joystick-page block of
`dialogs.c` lists its fields.

| Row | VA (listing) | What | Checked |
|---|---|---|---|
| `joy_table` | `0x457cf8(i, j)` (`..0x457d37`) | entry j of row i of the 13-byte tables `0x49e659` (byte `0x4d2571` set) or `0x49e5f1` | in vitro |
| `joy_page_ctor` | `0x457d38(P, flag, …)` (`..0x4585f3`) | the page: dialog base, texts, bitmaps, labels, buttons, entry counts | in vitro |
| `joy_page_dtor` | `0x4585f4(P, flags)` (`..0x45866b`) | frees the labels and the bitmap, then the dialog base; deletes with bit 1 | in vitro |
| `joy_page_run` | `0x45866c(P)` (`..0x458c83`) | open, the step loop, the pending gadget's action (cycle, step, defaults with `0x45a274`, store), close | in vitro |
| `joy_page_draw` | `0x458c84(P)` | backdrop, title, gadgets, labels | in vitro |
| `joy_page_label` | `0x458d3c(P, k)` | value label k (k <= 12) | in vitro |
| `joy_page_select` | `0x458ed8(P, g, …)` | highlight and column on gadget focus | in vitro |
| `joy_page_keys` | `0x458fb8(P, key)` | keys 1, 0xf, 0x1c, 0x52, 0x80f, others | in vitro |
| `joy_page_text` | `0x459148(P, k)` | EAX = `[P+0x336+4k]` | in vitro |
| `joy_page_store` | `0x45915c(P)` | the 13 pref indices once, then prefs store `0x459b94(13, 0x49e7ba)` | in vitro |
| `joy_page_row` | `0x459314(P, i)` | the shown row | in vitro |
| `joy_page_next`, `joy_page_back` | `0x45933c`, `0x4593a8(P, i)` | steps entry i forward or back with wrap, sets the pref with `0x459958`, draws its label | in vitro |
| `joy_page_column` | `0x459414(P, i)` | the column's labels | in vitro |
| `prefs_commit` | `0x459894(P)` | committed copies and the change callbacks `0x4d1fdc` | in vitro |

`0x459244` and `0x459af4` are dead and get no row: the image has no call,
jump or dword reference to either.

Facts (confirmed):

- `0x457cf8` returns the entry byte in AL. The upper bytes of EAX depend
  on the path:
  - With `[0x4d2571]` set, EAX held the row address (`0x457d16`).
  - Otherwise EAX held the row offset 13·i (`0x457d2a`); the address is
    formed in ECX.

  Next and back push that full EAX as the value to `0x459958` (`0x459386`,
  `0x4593f2`). Trap: taking the row address on both paths is caught by the
  in-vitro stub log (`%(12,c,1)` against `%(12,49e60c,1)`).
- Next wraps when the counter exceeds the count byte (`jbe`, `0x45935c`).
  Back wraps when the counter becomes 0xff (`0x4593ba`).
- The prefs callbacks `0x4d1fdc` are 0 in the pristine image. The commit
  calls one only when it is set and the value changed.

### The wiring

`options_run`'s gadget 9 (`0x457036..0x457093`) is `joystick_page`. It
does, in order:

1. gadget 9's draw (`+0`);
2. a 0x480-byte frame;
3. `0x457d38(P, 1, 0)`, then `0x45866c(P)`;
4. the dialog's redraw (`[d]` slot 0);
5. focus back to `[d+0x2fb]`;
6. `0x4585f4(P, 2)`.

`sim/tests/dialogs_test.c` checks this order.

### In vitro

The suite runs 3,000 cases. Each case:

1. runs the constructor on a random world;
2. sets a random page state;
3. makes 1..8 calls of the thirteen rows;
4. runs the destructor.

The random world covers:
- the prefs, their committed copies and the callbacks;
- the joystick summary and object;
- the layout and base bytes;
- the entry tables, with a 1/4 chance of a full row;
- the once guards `0x49e5f0`/`0x49e7d4`.

Recording stubs stand in for these on both sides:
- the dialog engine (`0x45464c`, `0x4548c8`, `0x454acc`, focus, press, key flag);
- the cycle `0x4599d4`/`0x459a10`;
- the joystick rows;
- `0x45a274`, `0x459958`, `0x459b94`, `0x4530bc` and `0x45a810`.

The step stub sets random pending gadgets and ends the run.

EAX, DATA `0x497000..0x6cc000`, the heap and the stub log are identical,
and all 55 named paths are taken. One mutation was caught: next's wrap
`>` → `>=`.

### The SDL3 joystick

By default the window layer answers
`joyGetDevCapsA`/`joyGetPosEx` with winmm, as the original asks. winmm
does not see some controllers (an Xbox 360 controller answers 165 to
`joyGetDevCapsA` for ids 0 and 1, while SDL3 lists it).

`sim/src/joystick_sdl.c` (`es2/sim/joystick_sdl.h`) answers the same two
calls from SDL3, in winmm's layouts:
- JOYCAPSA is 0x194 bytes; only the A part of the game's 0x328-byte buffer
  is written.
- JOYINFOEX is 0x34 bytes; dwSize must be 0x34, and only the fields
  dwFlags asks for are written.

`ES2SIM_JOYSTICK` picks the source. `sdl` turns this path on. Unset,
empty or `winmm` keeps winmm. Any other value is an error.
`es2sim_window` makes the choice in `main.c` after the video set-up (set
`ES2SIM_JOYSTICK=sdl` before starting it).

| winmm | SDL3 gamepad | Other SDL3 joystick |
|---|---|---|
| devices 0, 1 | the first two SDL joysticks at the caps call | same |
| X, Y | left stick x, y | axis 0, 1 |
| Z (`JOYCAPS_HASZ`) | right stick y | axis 2 (if any) |
| R (`HASR`) | right stick x | axis 3 (if any) |
| U, V | left, right trigger (0..32767 → 0..65535) | axis 4, 5 |
| buttons bits 0..9 | south, east, west, north, left/right shoulder, back, start, left/right stick | buttons 0..31 |
| POV (`HASPOV`, `POVCTS`) | d-pad: 0, 4500 … 31500, 0xffff centred | hat 0, same |
| axis range | -32768..32767 → 0..65535 (centre 32768) | same |
| no device bound | caps and pos 165 (JOYERR_PARMS) | same |
| bound device went away | pos 167 (JOYERR_UNPLUGGED) | same |

The game reads only part of these answers (`input.c` `joystick_read`):
- X and Y;
- Z, or device 1's X without HASZ;
- R, or device 1's Y without HASR;
- the four exact POV values 0/9000/18000/27000; diagonals are ignored, as
  with an 8-way hat under winmm;
- the low byte of the buttons.

So buttons 8 and 9, U and V never reach the game.

Not reproduced:
- winmm's `JOY_RETURNCENTERED`/`JOY_USEDEADZONE` calibration. The game
  asks dwFlags 0xccf; SDL's readings are passed on unchanged.
- wPeriodMin/Max, which are stand-ins (10/1000) and are never read.

winmm without a device (joystick_probe in winmm mode, one driver set):
`joyGetPosEx(0)` answers 0 with axes 32767 and POV 0xffff;
`joyGetPosEx(1)` answers 165. Another driver set may answer differently
(the game only tests for 0).

Trying a real controller without the game:

    bazel run //sim:joystick_probe -- 30 sdl

Use `-- 30 winmm` for the default instead. The probe prints the caps of
devices 0 and 1. Then, for 30 s, it prints the pos answers and the axes,
POV bit and button byte as the game scales them (`0x47785c`).

Tests: `//sim:joystick_sdl_test` uses a fake backend, with no device and
no SDL subsystem. It covers both layouts, the error numbers, both
mappings, the POV angles and the mode selection.

### Open

- Live: the page with a real joystick and the SDL path inside the game
  (an owner test).

## The joystick virtuals as rows

The joystick vtable `0x49ec74` has two slots: the destructor `0x45c2e0`
(slot 0) and the poll `0x45c330` (slot 1). Both are service rows in
`sim/dispatch/services/input_devices.h`; their code is `joystick_dtor` and
`joystick_poll` in `sim/src/input.c`. The functions behind them run inside
those rows: `0x477630` and `0x477714` in the destructor, `0x47785c` and
`0x4778a8` (`axes_scale`), `0x47e068` and `0x47e08d` in the poll. Their
only callers are the two slots, and the only data reference is
`0x45c5db -> 0x477630`.

### Own callers

- `joystick_destroy` (`0x459e72..0x459e99`, the prefs path without device
  0) calls slot 0 with flags 3 through the dispatch.
- `joystick_reader` (`0x45bc10`, ids 3 and 4 of the device poll) calls
  slot 1 through the dispatch.

Both read the slot from the object's vtable. Neither has a short-circuit,
so the rows serve every joystick object whose vtable is `0x49ec74`. The
live map has the destructor in 38/40 suites (the prefs path at mission
load) and the poll in 2/40 (`c1_01-joystick`, `c4_08-joystick`).

### Contract `0x45c2e0(J, flags)`, cdecl

Listings `0x45c2e0..0x45c32d`, `0x477630..0x477671`, `0x477714..0x47771e`.

- The counter behind `0x6cbee4` drops by 1. For J = 0 that is all: EAX is
  the counter pointer, EDX and ECX are as at the call.
- Otherwise:
  1. The vtable `0x49ec74` is written to `J+0x24`.
  2. The id's active byte `0x4d248c + [J+0x20]` is set to 0. The current
     joystick id `0x4d24a4` becomes 0 when it equals the id.
  3. `0x477630(J, 0)` drops the counter again. EDX = k = `[J+0x18]`.
  4. When `[0x6c5eb4 + 4k]` is set, `0x477714` clears bit 0 of `[J]` and
     the slot becomes 0. EAX = k, EDX = 0, ECX = J. Otherwise EAX is the
     counter pointer and ECX is as at the call.
  5. With flag bit 0, operator delete `0x477548(J)` runs. EAX and EDX are
     delete's, ECX = J.

The EDX = 0 store at `0x45c30b` is always overwritten by step 3.

### Contract `0x45c330(J)`, cdecl

Listing `0x45c330..0x45c522`. The steps are given in the comment on
`joystick_poll`.

- EAX is `0x4d242c`.
- EDX and ECX come from `_memcpy` when the result changed (`_memcmp`'s AL
  is non-zero). Otherwise they come from `_memcmp`.
- The divisor `(1 << b) - 25` is never 0, so the divide at `0x45c3bd`
  cannot fault.
- A zero square `((1 << b) - 25)^2 >> (b + 1)`, for example with b = 5,
  faults in `0x47e068`. The own code refuses it as a divide fault at
  `0x45c3e9`.
- For a mode above 2, or when bit 0 of `[J]` is clear, the 0x30-byte local
  at `[ebp-0x30]` is never written. That is residue (see "Stack residue"
  above). The own model is 0.

### In vitro

`//tools/reference-game/invitro:differential_input_devices_test`
(`input_devices_cases.h`) runs 3,000 cases with 1..8 calls each: about
6,600 destructor calls and 6,700 poll calls. Each call starts with random
EAX, EDX and ECX.

- **Objects.** Three random objects, each with:
  - an id 0..7 (often the current one) and a slot 0..7;
  - the 0x6c5eb4 table set or zero;
  - flag bit 0 either way;
  - raw or edge axis values;
  - axis bits (without the faulting squares);
  - mode 0, 1, 2 or other.
- **Arguments.** The destructor gets J = 0 or an object, with random
  flags.
- **Stubs.** On both sides, `_memcmp`, `_memcpy` and operator delete are
  recording stubs.
- **Residue.** The original side's thunk zeroes 1 KiB below its stack
  pointer, which gives the residue the own value 0.
- **Compared.** EAX, EDX and ECX, DATA, the heap and the stub log are
  identical.
- **Coverage.** Every path counter is non-zero: J = 0, the current or
  another id, the slot set or clear, delete or not, each mode, flag bit 0
  clear, the curve's dead zone, positive and negative values, the linear
  dead zone and both signs, and the result changed or unchanged.

A mutation that dropped ECX = J after the delete failed the suite (case 0,
ECX differs).

### Acceptance

These headless replays stay identical with the rows in place of the old
inline calls: `//sim:replay_acceptance-60s_test`,
`//sim:replay_c1_01-success_test`, `//sim:replay_c1_01-joystick_test` and
`//sim:replay_c4_08-joystick_test`. The in-vitro suites
`differential_input_layer_test` and `//sim:input_test` also stay green.
