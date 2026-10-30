# Flyers: flight model, flyer control and the FLYER AI

Scope: the flight model `0x466b58` shared by the player's Razor and the AI
flyers, the AI flyer move step `0x4218c4`, the flyer control functions
`0x41bb9c`, `0x4221a8`, `0x42215c`, `0x4222fc`, the FLYER AI state methods
and their helpers, the rotation helpers `0x47f058`/`0x47f4f4`, and DBSIM's
Win32 shell region `0x464a14`–`0x466586` (WinMain, frame buffer, palette,
message pumps, MainWndProc), which sits between the sound code and the
flight model. Every flyer function here is a hook; the groups are `flyer`,
`ai_motion`, `flyer_control`, `flyer_states`, `flyer_ai` and
`rotation_helpers` ([hooks.md](../reference-game/hooks.md)). The shell
region has no hook.

**Evidence.** Read from the reference `VER95/DBSIM.EXE` (hash in
`data/reference_hashes.txt`, image base `0x400000`) with the pinned llvm-objdump
(`-M intel`; entry-aligned re-disassembly behind the jump tables of
`0x466034` and `0x422d00`, and with `--start-address` at `0x47f058`/`0x47f4f4`,
where the linear listing loses the instruction boundary in the padding);
import thunks resolved through the import table (`jmp [IAT]` stubs
`0x4967f0..0x496a5a`); callers from an E8/E9, Jcc rel32 and absolute-dword
scan of the whole file. Live statements are the entry-probe counts of the
[DBSIM map](../reference-game/dbsim_map.md) (the six-suite map, in the order a60/c1s/c1c/c3_09/c4_08/c3_10, saturated at 1000; "0" = probed, never
entered after arming; "n/p" = not probeable) or the hook counters of a
regression where named. Mission facts come from the prepared `script.dat`
files of the suites, flight-model values from `RAZOR.FM`/`SKIMMER.FM` in
`VOL/SIMVOL0.VOL`. The stack
depths of the state methods and helpers come from a control-flow walker
over the same listing (push/pop/frame arithmetic, direct callees
recursively) and are tested in vitro by the planted residue bytes.

Names of axes, fields and roles (pitch, roll, yaw, speed, ceiling,
"attacking" …) are **hypotheses** derived from the arithmetic, the data
values and the state records unless marked otherwise.

**Notation.** A the FLYER object, P the player's `MECH`, S the flight
state, O the actor pose (actor + 0xc), M the flight-model block, C the
control record, F the failure flags. `s16`/`sx` sign extension; lo(r, w) a
16-bit write that keeps r's high word; q16(x, y) = `0x47e08d` (bits 16..47
of the signed 64-bit product, EDX = bits 32..63, ECX unchanged); q14(x, w) =
`0x47e0c0` (bits 14..45 of x·s16(w), EDX = bits 32..63, ECX = s16(w)).
"Regs" are EAX/EDX/ECX at a call; every bridged callee gets exactly these.

## The flight model `0x466b58`

`0x466b58` is the flight model shared by the player's Razor (a `MECH` with
a flyer chassis) and the AI flyers (`FLYER` pool). One call advances
throttle, forward speed, drag, angular rates and orientation of one flyer
by one frame; it does not move the position (the callers' updates do).

**Hook.** `flight_model` (RVA `0x66b58`), adapter group `flyer`
(`tools/reference-game/proxy/flyer_hooks.h`), core `core/src/flyer.c`
(`es2_flight_model_registers`, header `es2/core/flyer.h`). Prologue
`55 8b ec 81 c4 40 fe ff ff`, relocated 9 bytes as 1/2/6. Four cdecl slots
are passed on (the fifth, the constant 1, is never read). Compare mode is
refused (game calls).

**Size.** `0x466b58..0x467821` (3,274 bytes), **867** instructions, no
indirect jump (the map's count is exact), no x87 instruction (`0x47f058` and the leaves have none either). The map's
`x87 = 1` is a parser artefact: its line parser takes the wrapped last raw
byte `ff` of the 10-byte `movl $0xffffff00, -0xc0(%ebp)` at `0x4670bc` as a
mnemonic. 49 conditional branches, 47 call sites.

### Flight model callers

Only two direct call sites, no absolute reference anywhere in the file
(byte search for the address):

| Caller | Instr. | Calls per suite | Path |
|---|---:|---|---|
| `0x41bb9c` (call `0x41bc31`) | 63 | 0/0/0/0/1000/0 | player flyer input `flyer_input`, called by the input dispatcher `0x460870` ([input.md](input.md), step 6) only for a flyer chassis and not frozen |
| `0x4221a8` (call `0x422251`) | 70 | 1000/1000/1000/0/0/0 | AI flyer control `flyer_command`; callers `0x42215c` and `0x4222fc` (steering, called by the AI-state procedures `0x4224c4`, `0x422598` and the `target_emit` hook `0x4226a0`) |

`0x466b58` itself: 1000/1000/1000/0/1000/0.

### Flight model contract

`void 0x466b58(C, S, M, F)`, cdecl. Both callers push a fifth argument
(the constant 1) that is never read: the body reads only `[ebp+8..+0x14]`.
Prologue `55 8b ec 81 c4 40 fe ff ff 53 56 57` (locals 0x1c0 bytes); EBX =
S, ESI = M until `0x467507`. EAX, ECX, EDX are not read at entry. Both
callers ignore the returned EAX and EDX (`0x41bc36`, `0x422256`); the exact
result registers are under [Flight model registers](#flight-model-registers).

**C, control record** (five dwords, read only):

| Field | Player (`0x41bb9c`) | AI (`0x4221a8`) | Use |
|---|---|---|---|
| `C+0` | turn axis `R+0xe` | roll command (shaped, below) | roll input |
| `C+4` | throttle axis `R+0x10` | pitch command | pitch input |
| `C+8` | twist axis `R+0x12` | yaw command or 0 | yaw input |
| `C+0xc` | pitch axis `R+0x14` (the throttle lever, [input.md](input.md)) | 0 | throttle rate, or lever position |
| `C+0x10` | `terrain_height([0x4a0be0], P+0x26)` | `terrain_height([0x4a0be0], A+0x26)` | ground height below the flyer |

So for a flyer the stick X/Y axes become roll and pitch, the twist axis
yaw, the lever axis throttle. The AI record is filled by `0x4222fc`
(`C+4` = its third argument, `C+8` = `47e08d(−yaw error, 0xfa0)` only if
|yaw error| < 0x7d0 and roll word `A+0xe` < 0x320, `C+0xc` = 0, `C+0`
= its own steering sum when it calls `0x4221a8` directly) and `0x42215c`
(`C+0` = `47e08d(goal − A+0xe, 0x9c4) + 47e08d(A+0x25d, −0x1388)`, roll
toward a goal bank damped by the roll rate `S+0x1a`); `0x4221a8` then
replaces `C+0` and `C+4` **in the caller's record** by sgn(v)·(v² >> 8)
clamped to ±256.

**S, flight state** (78 = 0x4e bytes, zeroed at construction; player
`P+0x2b9`, built by the `MECH` constructor `0x415bb0` at `0x416000..0x4160b0`;
AI `A+0x243`, built by the `FLYER` constructor `0x4215f4` at
`0x421735..0x4217de`):

| Field | Init | Meaning (names hypothesis) |
|---|---|---|
| `+0x00`, `+0x04`, `+0x08` dword | 0, **1000**, 0 | body-frame velocity; `+4` is the forward speed |
| `+0x0c..+0x14` dword ×3 | 0 | world-frame velocity (pose rotation · body velocity); read by the position updates |
| `+0x18`, `+0x1a`, `+0x1c` word | 0 | angular rates pitch, roll, yaw (order of the Euler angles) |
| `+0x1e` word | player: word `P+0x290`; AI: 0x200 | throttle, −1024…1024 |
| `+0x20` dword | actor + 0xc | pointer to the actor pose O |
| `+0x24..+0x43` | inverse of the pose matrix | 32-byte matrix (rotation transposed, translation −Rᵀt) |
| `+0x44` dword | 0 | last bank-turn rate (unscaled) |
| `+0x48..+0x4d` | 0 | not used by `0x466b58` |

**O, actor pose** (actor + 0xc; the same layout and cache flag as in
[player_movement.md](player_movement.md)): angle words `O+0` (pitch), `O+2`
(roll), `O+4` (heading; `P+0x10` in input.md), 32-byte matrix at `O+6`
(translation = actor position at `O+0x1a`, i.e. actor+0x26/+0x2a/+0x2e; Z
is up), matrix-valid word `O+0x26`.

**M, flight-model block** (54 = 0x36 bytes, the raw `.FM` file):
player `[P+0x1f2]+0x1dc`, loaded by the vehicle-config loader at
`0x420416..0x420453` when config word `+0x50` (DAT file offset `0x4e`,
[dat.md](../formats/dat.md)) is nonzero, file `RAZOR.FM`; AI
`[A+0x1fa]+0x3a`, loaded by the SKIMMER loader at `0x422f72..0x422fa9`,
file `SKIMMER.FM`. Both loaders then overwrite `M+0xe` with
`47e068(M+0x22 − M+0x26, M+0x2a − M+0x2e)` (Q16 quotient (a<<16)/b).

| Field | RAZOR / SKIMMER | Use in `0x466b58` (role hypothesis) |
|---|---|---|
| `+0x00` word | 400 / 600 | pitch-rate limit and pitch command gain |
| `+0x02` word | 1500 / 1400 | roll-rate limit and roll command gain |
| `+0x04` word | 400 / 600 | yaw-rate limit and yaw command gain |
| `+0x06` word | 200 / 250 | pitch-acceleration limit |
| `+0x08` word | 400 / 300 | roll- and yaw-acceleration limit |
| `+0x0a` word | 100 / 100 | speed step per frame |
| `+0x0c` word | 0 / 0 | not read |
| `+0x0e` dword | 0 in file | ceiling slope (Q16, computed at load) |
| `+0x12` dword | 500 / 500 | rate damping (Q10) |
| `+0x16` dword | 16 / 16 | pitch auto-level shift |
| `+0x1a` dword | 5 / 6 | roll auto-level shift |
| `+0x1e` dword | 4 / 4 | bank-to-turn shift |
| `+0x22` dword | 60000 / 120000 | ceiling at maximum speed (read only by the loader) |
| `+0x26` dword | 6000 / 6000 | ceiling at minimum speed |
| `+0x2a` dword | 1500 / 1000 | maximum speed |
| `+0x2e` dword | 250 / 500 | minimum speed |
| `+0x32` dword | 300 / 400 | lateral drag (Q10) |

**F, failure flags** (four dwords, read only): the player passes
`F+0` = (word 7 of the part-flag array `[P+0x20e]` = 0), `F+4` = (word 8 = 0),
`F+8` = (word 4 = 0), `F+0xc` = (word 5 = 0); a zero part word means a lost
part ([player_movement.md](player_movement.md), `vehicle_hit`/`vehicle_part`).
The AI passes four zeros. Which Razor parts 4, 5, 7, 8 are is open.

### Flight model algorithm

In execution order. Helpers: `mq8(a,b)` = `multiply_q8` (a·b >> 8),
`mq10` = `multiply_q10` (a·b >> 10), `step(v)` = `movement_step`
(frame-scaled s16 via the time scale `0x4d3b64`, clamped ±32767), `cos` =
`heading_cos` (Q14), `q14(x,w)` = `47e0c0` (s32 x · s16 w >> 14),
`q16(a,b)` = `47e08d` (a·b >> 16); all low 32 bits. "clamp(v, L)" clamps a
signed word to [−L, L]. Word arithmetic wraps in 16 bits where the original
uses word operations.

1. **Throttle.** `0x477904()` (fills the device state `0x6bb6a8`). If its
   byte `+4` ≠ 0 and byte `0x4d1f52` (offset 0x1a of the 54-byte controls
   block `0x4d1f38`) = 2: `S+0x1e` ← clamp(low16(word `C+0xc` << 3), 0x400)
   (lever position). Else d = low16(mq8(100, `C+0xc`)); if d ≠ 0:
   `S+0x1e` ← clamp(`S+0x1e` + step(d), 0x400).
2. **Forward speed.** target = `M+0x2e` + mq10(`M+0x2a` − `M+0x2e`,
   (`S+0x1e` + 0x400) >> 1), minus mq10(250, pitch) when pitch `O+0` < 0,
   else minus mq10(62, pitch) (diving raises, climbing lowers the target).
   `0x467b28(&S+4, target, step(M+0xa))` moves `S+4` toward the target by
   at most that step (return value ignored).
3. **Drag.** If `O+0x26` = 0: `euler_matrix(O, O+6)`, `O+0x26` ← 1.
   W = `rotate_point`((S+0, 0, S+8), R); W.x, W.y ← mq10(`M+0x32`, ·), W.z
   kept; B = `rotate_point`(W, inverse `S+0x24`); `0x466ae0`(B) replaces
   each component by step(low word); `S+0/+4/+8` −= B. Then
   `S+0xc` ← `rotate_point`(S, R) (world velocity, old orientation).
4. **Pitch command** (old rates saved). p = `C+4`; if `F+8` or `F+0xc`:
   p = (−cos(roll)) >> 6. If p ≠ 0: Pc = mq8(`M+0`, p); else
   Pc = (−pitch) >> `M+0x16` (both branches of `0x466e8f` compute the same)
   and dP = −mq10(`M+0x12`, `S+0x18`). Yaw command Yc = mq8(`M+4`, −`C+8`).
5. **Ceiling.** ceiling = `M+0x26` + q16(`S+4` − `M+0x2e`, `M+0xe`)
   (linear from `M+0x26` at minimum to `M+0x22` at maximum speed);
   alt = Z(`O+0x22`) − `C+0x10`. If alt > ceiling: e = −mq10(10,
   alt − ceiling); Yc = q14(cos(roll + 0xc000), e) (s16); Pc =
   min(Pc, q14(cos(roll), e)). Else, if p = 0, dP is recomputed (same value).
6. **Roll command.** r = `C+0`; `F+0xc` → r = 0x100; else `F+8` → r =
   −0x100; else `F+4` and roll < 0x1000 → r += q14(20, 0x1000 − roll); else
   `F+0` and roll > −0x1000 → r −= q14(20, roll + 0x1000). Bank-turn rate
   T = (−roll) >> `M+0x1e` if |roll| < 0x4000, else (roll + 0x8000) >> `M+0x1e`
   (|0x8000| taken as 0x7fff). If r ≠ 0: Rc = mq8(`M+2`, r), and dR =
   −mq10(`M+0x12`, `S+0x1a`) only when r and the roll rate have opposite
   signs. If r = 0: Rc = (−roll) >> `M+0x1a`, dR = −mq10(`M+0x12`, `S+0x1a`);
   if |`S+0x1a` + dR| > |roll|: Rc = 0, dR = −`S+0x1a` − |roll| (word).
   (A dword `(C+0 < 0) == (roll < 0)` is stored at EBP−0xc4 and never read.)
7. **Rates.** dP += clamp(Pc, `M+6`); dR += clamp(Rc, `M+8`); dY =
   clamp(Yc, `M+8`) − mq10(`M+0x12`, `S+0x1c`). `0x466b20` steps the three
   words; `S+0x18/+0x1a/+0x1c` += them, then clamp to `M+0`, `M+2`, `M+4`.
8. **Orientation.** `0x466a88(new, old)` = per-word (new + old) >> 1 into the
   static `0x4d3b58` (sets guard byte `0x4a087c` once); `0x466b20` steps
   them (angle increments); D = `euler_matrix`(increments);
   N = `47f058`(D, R) (rotation-only compose, [Rotation helpers](#the-rotation-helpers-0x47f058-and-0x47f4f4))
   with O's position as translation; `O+0..+4` ← `matrix_angles`(N);
   `O+6..+0x19` ← N's 20 rotation bytes, translation rewritten unchanged,
   `O+0x26` ← 1. Then heading `O+4` += step(T), `O+0x26` ← 0, `S+0x44` ← T;
   matrix rebuilt by `euler_matrix(O, O+6)`; `S+0x24` ← inverse
   (`matrix_transpose`, negated translation, `rotate_point`).
9. **Re-express the velocity.** v0 = low16(`S+4`); `S+0..+8` ←
   `rotate_point`(`S+0xc`, `S+0x24`) (the world velocity kept across the
   turn). If s16(v0) > `S+4`: `S+4` += mq10(900, v0 − `S+4`) and
   `S+0xc` ← `rotate_point`(S, R) (90 % of the forward speed lost by the
   turn comes back).

**`0x467b28(p, t, step)`**: if t < *p: *p −= step, then if t < *p return
*p − t (ECX = *p), else *p = t and return 0; if t = *p return 0; if t > *p:
*p += step, then if t > *p return t − *p (EDX = the same), else *p = t and
return 0. Signed dword compares.

### Flight model side effects

Writes: `S+0..+0x1f`, `S+0x24..+0x47`; the actor pose `O+0..+0x27`
(angles, matrix, cache word; the translation, i.e. the position, is only
rewritten with its own value); the static
`0x4d3b58..0x4d3b5d` and guard `0x4a087c` (`0x466a88`; no other reader in
the file); the device state `0x6bb6a8..0x6bb6af` (`0x477904`, platform).
Reads `0x4d1f52`, `0x4d3b64` (through `movement_step`) and the
`heading_cos` table. The position is integrated by the callers' actor
updates: the AI flyer move step `0x4218c4` adds `S+0xc..+0x14` directly to
actor+0x26 (`0x421907..0x421917`); the player's flyer movement `0x4198f4`
(`mech_fly`, 697 instructions, 0/0/0/0/1000/0) reads `S+0xc` and
frame-scales each low word with `movement_step` (`0x419b2a..`;
[hit_tests.md](hit_tests.md#mech-player-fly-movement-0x4198f4-mech_fly)).

### Flight model quirks

- **Stack residue in the pose.** `47f058` never writes byte 19 of its
  result in its computing branches, and `euler_matrix` never writes byte 19
  either, so the pose byte actor+0x25 receives stack residue
  ([Flight model stack residue](#flight-model-stack-residue-and-the-entry-shim)).
- **Lever mode reaches the AI.** Step 1 tests only globals. With byte
  `[0x477904()+4]` ≠ 0 and `0x4d1f52` = 2, every AI flyer's throttle is
  forced to 0 each frame (its `C+0xc` is 0), i.e. half of the speed range
  instead of three quarters (0x200). `0x4d1f38` is the 54-byte controls
  block of the prefs, loaded at `0x4597bc` ([input.md](input.md)); byte
  0x1a = 2 selects the flight throttle. The oracle suites, the joystick
  suites included, run the default mapping, so the lever branch (sites
  `0x466b92`, `0x466ba0`) is never entered live
  ([live_coverage.md](../reference-game/live_coverage.md), non-default
  joystick mapping).
- Word wrap: `C+0xc << 3` wraps for |lever| ≥ 0x1000 before the clamp;
  drag and angle increments use only the low words of dword values
  (`0x466ae0`); v0 in step 9 is the low word of the speed.
- **One-way branch sites.** With real callees and S and O disjoint (both
  callers), `0x46750f` and `0x4677b6` always jump (the cache word is 1),
  and `0x467640` never jumps. The first two are reached both ways when a
  callee between the setting of the word and the test clears it (the Euler
  of D before `0x46750f`; `rotate_point`/`multiply_q10` before `0x4677b6`),
  as the in-vitro stubs do. `0x467640` has no call in between: `0x467629`
  clears `O+0x26` through `[S+0x20]`, the only later write is S+0x44, and
  `0x467638` rereads `[S+0x20]`. The jump needs a pose overlapping its own
  pointer slot, so it is unreachable for any layout the callers can build
  (PROVED in `branch_sites.h`). Of the 49 sites, 48 are reached both ways.

### Callees of the flight model

| Callee | Instr. | Calls per suite | Role | Mode in the replacement |
|---|---:|---|---|---|
| `0x477904` | 43 | 1000×6 | device state `0x6bb6a8`; byte +4 = lever present (hypothesis, open) | platform; original through the guest bridge (it reads no register; its `memset` takes stack arguments) |
| `0x467b28` | 32 | 1000/1000/1000/0/1000/0 | approach a dword toward a target by a step, returns the remaining gap | only caller `0x466b58`; leaf, computed in core |
| `0x466ae0` | 26 | same | step() of the low words of a dword vector, in place | only caller; leaf over `movement_step`, computed in core |
| `0x466b20` | 23 | same | step() of a word vector, in place | only caller (two sites); leaf over `movement_step`, computed in core |
| `0x466a88` | 27 | same | mean of two word vectors into the static `0x4d3b58`, returns it | only caller; computed in core with its writes to the static `0x4d3b58..0x4d3b5d` and the guard `0x4a087c` |
| `0x47f058` | 363 | n/p (`enter` prologue) | rotation-only matrix compose: the 3×3 part of `matrix_compose` with the same mode cases (identity → 20-byte copy of the other operand, planar 2×2, general); no translation | computed in core (`es2_compose_rotation` in `flyer.c`); the hook `rotation_compose` serves its other caller, debris `0x40863e` |
| `0x47e0c0`, `0x47e08d` | 7, 6 | n/p | Q14 word product, Q16 product | pure, computed in core |
| `movement_step`, `multiply_q8`, `multiply_q10`, `heading_cos`, `euler_matrix`, `rotate_point`, `matrix_transpose`, `matrix_angles` | | hooks | | hooks through the guest bridge, each in its own mode |
| `memset` `0x49681a` (in `0x477904`) | | n/p | | runtime import |

The five live callees of the map are the four private leaf helpers and the
platform function `0x477904`. Static reach with the hooks as leaves: 17
functions, 8 hooks, 9 originals (the table). Nothing in the subtree is
rendering or audio. All bridged calls use the guest bridge of the weapons
group with the EAX/EDX/ECX the original holds at each call; callee counters
`hk_flight_model_call_<va>` for the nine bridged targets.

### Flight model registers

466b58 reads none of EAX/EDX/ECX at entry; they reach only `0x477904`,
which reads no register. The register state at every later call is
reproduced from the code (`lo()` keeps the high word where the original
loads a word, `pop ecx` after one-slot calls restores the pushed slot, the
SAR counts are masked to five bits). Result EDX:EAX: the last
`rotate_point`'s EAX, EDX = s16 of the speed word before step 9 when the
speed recovery does not run (the `movswl` at `0x467762`), else the last
`rotate_point`'s EDX; reproduced exactly. ECX is not returned. Walk:
player path `0x41bc36` → return into `0x460afb` → `0x460ce6`, where ECX
reaches only `fire_line` `0x415608` (whose first call `0x410dbc` overwrites
ECX before reading it) or the runtime `memset`; AI path `0x4221a8` →
`0x42215c`/`0x4222fc` → returns into the AI-state procedures (`0x42257e`:
return to `0x42184c`, which pops ECX at `0x42187d`; `0x422668`: return
there or `0x422260`, which passes it only to `heading_cos` and pops it) and
into the `target_emit` core, which returns. So ECX is dead after 466b58 on
every path.

### Flight model stack residue and the entry shim

Byte 19 of the composed pose matrix N (frame `EBP−0x1a0`, 20 bytes) is
never written by the computing branches of `0x47f058`, and byte 19 of the
increment matrix D (`EBP−0x1b4`) is never written by `euler_matrix`. So
the 20-byte copy at `0x4675b9` puts into the pose byte actor+0x25 (O+0x19,
pose matrix byte 19, the reserved byte): the byte at `EBP−0x18d` = entry
ESP−0x191 (N computed), the byte at `EBP−0x1a1` = entry ESP−0x1a5 (R an
identity, all pose angles zero: `0x47f058` copies D), or the old pose byte
(D an identity, all increments zero: `0x47f058` copies R). The same byte
reaches S+0x37 by the inverse copy at `0x467688`. No instruction of 466b58
writes either frame byte and its callees' frames lie below `EBP−0x1cc`, so
both are fixed at entry. No reader of the pose byte is known
([terrain.md](terrain.md) item on the renderer's pose matrix: the
simulation cannot tell who filled it).

The shim `flyer_entry` loads both bytes into CL and CH and continues into
the framework's dispatch; the adapter puts them into its scratch frame at
the original offsets (`g.padding`, `g.composed_padding`). The 477904 call
therefore sees the residue in ECX instead of the caller's ECX; it does not
read ECX. `matrix_angles` reads N including byte 19 (in vitro its stub
records it).

### Flight model validation and live counts

**In vitro** (`tools/reference-game/invitro/flyer_cases.h`): 1,001,024
direct cases with logging stubs for the nine bridged callees (stubs move
the cache word, switch the pose pointer between two poses, change words of
C, S, M, F and the pose between calls), the original called from an own
caller that plants two distinct residue bytes; 256 installed 31/a7 poison
cases through the shim; 6,144 real-callee cases with the eight nested hooks
in rotating modes and the real `0x477904` on randomized device globals
(about 30 % of them in lever mode). 49 branch sites: 48 both ways, `0x467640`
PROVED. Mutation witnesses in
[MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md).

**Live expectation.** From the map's entry probes (1000/1000/1000/0/1000/0),
`hk_flight_model_calls` is nonzero in acceptance-60s, c1_01-success,
c1_01-collision (AI flyers of C1_01) and c4_08-razor-plasma (player
Razor) and zero in c3_09-loadout-b and c3_10-loadout-hercs. Per call: one
`call_477904`, `call_47f9a0` and `call_47df19`; 5 or 6 `call_480304` (the
drag pair, the world velocity, the inverse translation, step 9, the
recovery); 11 or 12 `call_467924` (throttle rate, speed step, 3 + 3 + 3 in
the leaves, the bank turn); 2 or 3 `call_47ebb8` with real callees (D, the
rebuild at `0x467653`, and the pose when its cache word is 0 at entry).

### Flight model reach per suite

The flight model runs in a60/c1s/c1c through the AI flyers of C1_01 (four
records, two of them active from the start) and in c4_08 through the
player's Razor. C3_09 and C3_10 have neither: their scripts contain no
flyer record and the player drives a ground Herc.

1. **Player path.** `0x460870` calls `0x41bb9c` only when config word
   `[P+0x1f2]+0x50` ≠ 0. That is DAT offset `0x4e`; among the 21 vehicle
   DATs of `SIMVOL0.VOL` only `RAZOR.DAT` has it nonzero (= 1). The c3_09
   player drives an Ogre, the c3_10 player a Tomahawk
   ([oracle_suites.md](../reference-game/oracle_suites.md)), c4_08 a Razor.
2. **AI path.** A `FLYER` object is constructed (`0x425a3f..0x425a71`) only
   for a record of the script's flyer list whose selection byte
   `0x4aa856[i]` was set; `0x424e00..0x424eb1` sets it for every reference
   of an actor group with class word +40 = 1, skipping group 0 (the
   player's group). Its update, flyer virtual `+0x18` = `0x42184c` (calls
   the current AI-state procedure), runs from main-loop row 11
   (`0x45f7f5` → `order_update` `0x423a74`, only for groups whose `+0x14`
   is 0, i.e. no pending activation → `0x411cec` → slot `+0x18` at
   `0x411d46`; [vtables.md](vtables.md) section 1.3,
   [main_loop.md](main_loop.md) row 11).

Script data:

| Suite | Flyer records | Class-1 groups | Reaches `0x466b58` via |
|---|---:|---|---|
| a60, c1s, c1c (script C1_01, as in oracle_suites.md) | 4 | 29 → [0], 31 → [2] active; 30 → [1], 32 → [3] wait for triggers 6, 7 | AI (`0x42184c` 1000×3) |
| c3_09-loadout-b | **0** | none | — |
| c3_10-loadout-hercs | **0** | none | — |
| c4_08-razor-plasma | 1 | 14 → [0], activation trigger 5 | player only; `0x42184c`, `0x4217fc`, `0x4218c4`, `0x4221a8` all 0 in c4_08 |

So c3_09/c3_10 contain no flyer at all and a ground player: no caller of
`0x466b58` can run. In c4_08 the one flyer is constructed but stays
inactive; that trigger 5 never fires in the 40,000-tick suite is a
**hypothesis** consistent with the zero counts (the trigger was not
decoded). The flight model is covered live by a60/c1s/c1c (AI) and c4_08
(player) only.

## The AI flyer move step `0x4218c4`

`0x4218c4` is the **per-frame move step of an AI flyer**: it integrates the
position with the world velocity of the flight state, steers away from
terrain with four probe points, keeps the flyer at least 500 units above
the ground and plays a fly-by sound once per approach to the camera. It is
neither rendering nor audio code (one sound call, kept original). Hook
`flyer_move`.

### How the move step is reached

No direct call. `0x4218c4` is the second member pointer of the constant
groups 1, 2, 3, 5 at `0x499e9c` + `0x24`·k (dwords `0x499ecc`,
`0x499ef0`, `0x499f14`, `0x499f5c`); the startup initializer `0x414c68`
copies it to `+0x24` of the FLYER state records attacking, patrolling,
search and destroy and scouting. The FLYER virtual `+0x14` = `0x4217fc`
(vtable `0x49a5e0`, slot dword at `0x49a5f4`) calls `[[X+0x4d]+0x24]`
with X + `[[X+0x4d]+0x28]` when the triple is not all zero (EAX = the OR
of the three dwords, EDX = the state record, ECX = the third dword, 0 in
the retail tables) and returns its EAX. `0x4217fc` also copies the actor
position (`X+0x26`, 12 bytes) into its own frame before the call and
never reads it. `0x4217fc` is called through `+0x14` by `0x411cec` at
`0x411d29`, which `order_update` `0x423a74` calls at `0x423af5` for every
group member (logic frame row 11). `0x411d29` is followed by `pop ecx`;
`0x411d2d` overwrites EDX and `0x411d33` EAX: the result pair and ECX are
dead. Slot `+0x1c` = `0x421888` (called at `0x411d0b`) is the decision
slot and does not reach the move step.

The member-pointer layout, checked from the image
([vtables.md](vtables.md) section 3.2): FLYER vtable `0x49a5e0` has
`+0x14` (`0x49a5f4`) = `0x4217fc`, `+0x18` = `0x42184c`, `+0x1c`
(`0x49a5fc`) = `0x421888`. `0x4217fc` calls `[[X+0x4d]+0x24]`
(`0x42181d`..`0x42183e`), `0x421888` calls `[[X+0x4d]+0x30]`
(`0x421895`..`0x4218b6`). The constant area `0x499e9c` holds groups of
**three** member pointers (`0x24` bytes per group, 12 per member
pointer); `0x414c68` copies group k to `+0x18`, `+0x24`, `+0x30` of the
FLYER state record k. `0x4218c4` goes to `+0x24` of the records attacking
`0x499d34` (`0x414d36`/`0x414d3c`), patrolling `0x499d70`
(`0x414dac`/`0x414db2`), search and destroy `0x499dac`
(`0x414e23`/`0x414e29`) and scouting `0x499e24` (`0x414f4a`/`0x414f51`,
store to `0x499e48`); the `+0x30` member of those groups is `0x422d00`.
Counts agree: `0x4217fc` and `0x4218c4` 1000/1000/1000/0/0/0; `0x421888`
and `0x422d00` 14/14/14/0/0/0.

Only the C1_01 suites have active AI flyers
([Flight model reach per suite](#flight-model-reach-per-suite); the c4_08
flyer stays inactive).

### Move step contract

`EDX:EAX 0x4218c4(X)`, cdecl, one slot X = the `FLYER` object. Prologue
`55 8b ec 83 c4 bc 53 56 57 8b 5d 08` (locals 0x44 bytes; relocate 6 =
1/2/3). No x87 instruction, no indirect jump. EAX is written before it is
read; entry EDX and ECX reach only the `euler_matrix` call (step 1). No
local is read before it is written. Names: O = X+0xc (actor pose, angles
`O+0..+4`, matrix `O+6` = X+0x12, translation `O+0x1a` = position P =
X+0x26, cache word `O+0x26` = X+0x32), S = X+0x243 (flight state,
[Flight model contract](#flight-model-contract)), grid = `[0x4a0be0]`,
frame locals V = EBP−0x2c (probe point, 3 dwords) and Q = EBP−0x38
(transformed point, 3 dwords).

0. EAX ← `[X+0x1fa]` (stored to EBP−4, never read).
1. If word `O+0x26` = 0 (JNE `0x4218e1`): `euler_matrix(O, O+6)` with EAX =
   O+6 and the entry EDX/ECX, then word `O+0x26` ← 1.
2. P += world velocity: dword `P+8` += `S+0x14`, then `P+0` += `S+0xc`,
   then `P+4` += `S+0x10` (ECX = `S+0xc`, EDX = `S+0x10` afterwards).
3. Four terrain probes, each `transform_point(V, O+6, Q)` then
   h = `terrain_height(grid, Q)`; a probe acts only if s32 h > s32 `Q+8`
   (JLE), with d = h − `Q+8` and a `multiply_q10(k, d)` whose AX is used:

   | # | V written before the call | Registers at `transform_point` | Action when h > Q.z |
   |---|---|---|---|
   | 1 (`0x42193b`) | (1000, −420, −300) | EAX = V, EDX = Q, ECX = `S+0xc` | word `S+0x1a` −= AX(k = 0xfa0); word `O+2` ← `S+0x1a` + `O+2` |
   | 2 (`0x42199b`) | V.x ← −V.x (NEG in memory) | EAX = O+6, EDX = Q, ECX = V | word `S+0x1a` += AX(k = 0xfa0); word `O+2` as in 1 |
   | 3 (`0x421a0a`) | (0, 1000, 0) | EAX = O+6, EDX = Q, ECX = V | if s16 `S+0x18` < 0 (JGE `0x421a31`) it is set to 0; word `S+0x18` += AX(k = 0x7d0); word `O+0` ← `S+0x18` + `O+0` |
   | 4 (`0x421a87`) | (0, 15000, −1500) | EAX = O+6, EDX = Q, ECX = V | as 3 with k = 0x14 (JGE `0x421aae`) |

   `terrain_height` is entered with EAX = Q, EDX = grid (probe 1: EAX =
   grid, ECX = Q) and the other register as `transform_point` left it;
   `multiply_q10` with EAX = d and EDX/ECX as `terrain_height` left them.
   An acting probe ends with EAX = O, EDX = (its EDX high word, the new
   angle word) and clears word `O+0x26` (the matrix is rebuilt later; the
   remaining probes still use the old matrix).
   So: terrain under the right probe rolls one way, under the left probe
   the other way, terrain ahead pitches up (a negative pitch rate is
   cancelled first); the rates also act on the angles at once.
4. Ground clearance: Q ← P (copied z, x, y), h = `terrain_height(grid, Q)`
   with EAX = `P+4`, EDX = Q, ECX = grid. If s32 (h + 500) > s32 `Q+8`
   (JLE `0x421b1c`): `Q+8` ← h + 500 and P ← Q (z, x, y; EAX = P, ECX =
   x, EDX = y).
5. Fly-by sound, only if s32 `S+4` (forward speed) > 0 (JLE `0x421b39`):
   d = P − `[0x4d24ea]+4..+0xf` (camera position; reads P x, y, z, then
   camera z, x, y), r = `dist3d_delta(dx, dy, dz)` with EAX = camera y,
   EDX = ECX = dx. If s32 r ≥ 30000 (JGE `0x421b87`): word `X+6` ← 0.
   Otherwise, if word `X+6` = 0 (JNE `0x421b8e`): `0x462830(0x31)` (sound
   row 0x31, entered with EAX = r and EDX/ECX as `dist3d_delta` left
   them; ECX = 0x31 after the `pop`), then word `X+6` ← 1.
6. Return AX = 1 with EAX's high word from the last writer (`terrain_height`
   of step 4 or P when step 4 moved the flyer, `dist3d_delta`'s r, or the
   sound call's EAX) and EDX as the last writer left it.

Writes: P (`X+0x26..+0x31`), the angle words `X+0xc`, `X+0xe`, the cache
word `X+0x32`, the rate words `S+0x18`, `S+0x1a`, the latch word `X+6`; the
callees write `O+6..+0x25` (`euler_matrix`) and Q (`transform_point`).
Reads additionally `S+4`, `S+0xc..+0x14`, `[X+0x1fa]`, `[0x4a0be0]`,
`[0x4d24ea]` and the camera position.

### Callees of the move step

| Callee | Calls per entry | Mode in the replacement |
|---|---|---|
| `euler_matrix` `0x47ebb8` | 0 or 1 (cache word 0) | hook, through the guest bridge |
| `transform_point` `0x48043c` | 4 | hook, through the guest bridge |
| `terrain_height` `0x46e180` | 5 | hook, through the guest bridge |
| `multiply_q10` `0x47e0b0` | 0..4 (acting probes) | hook, through the guest bridge |
| `dist3d_delta` `0x47de72` | 0 or 1 (speed > 0) | hook, through the guest bridge |
| `0x462830` sound | 0 or 1 | **original** through the guest bridge, same order (stream B draw and clock, [audio.md](audio.md)) |

No entry shim: nothing uninitialized is read, and the entry EDX/ECX that
step 1 forwards are part of the framework's entry registers. Compare mode
is refused (game calls, sound).

## Flyer control `0x41bb9c`, `0x4221a8`, `0x42215c`, `0x4222fc`

The four functions that build the control record of the flight model: the
player's flyer input and the AI flyer's command, bank and steering. Hook
group `flyer_control` (`proxy/flyer_control_hooks.h`), core in
`core/src/flyer.c`. Callers come from an E8/E9 and absolute-dword scan of
the whole file.

**Callers (byte scan).** `0x41bb9c` ← `0x460af6` (input
dispatcher, [input.md](input.md) step 6). `0x4221a8` ← `0x42219a`
(`0x42215c`), `0x4224b2` (`0x4222fc`). `0x42215c` ← `0x422414`, `0x422447`,
`0x422498` (all `0x4222fc`). `0x4222fc` ← `0x42257e` (AI state `0x4224c4`),
`0x422668` (AI state `0x422598`), `0x422a2b` (`target_emit` `0x4226a0`). No
absolute reference (no vtable slot). None of the four has an indirect jump
or an x87 instruction. Map entry probes (a60/c1s/c1c/c3_09/c4_08/c3_10):
`0x41bb9c` 0/0/0/0/1000/0; `0x4221a8`, `0x42215c`, `0x4222fc` each
1000/1000/1000/0/0/0.

**Hooks.**

| Hook | VA | Prologue / relocated | Slots | Frame |
|---|---|---|---|---|
| `flyer_input` | `0x41bb9c` | `55 8b ec 83 c4 dc` / 6 (1,2,3) | 5: P, turn, throttle, pitch, twist | 24h |
| `flyer_command` | `0x4221a8` | `55 8b ec 83 c4 ec` / 6 (1,2,3) | 2: A, record | 14h |
| `flyer_bank` | `0x42215c` | `55 8b ec 53 56` / 5 (1,2,1,1) | 3: A, goal word, record | none |
| `flyer_steer` | `0x4222fc` | `55 8b ec 83 c4 d8` / 6 (1,2,3) | 4: A, yaw error, pitch, target | 28h |

All four are cdecl and return what their last call left in EDX:EAX
(except `flyer_input`'s EAX, below). Calls: `terrain_height` `0x46e180`,
`heading_cos` `0x47dff0`, `flight_model` `0x466b58` and the group's own
`0x42215c`/`0x4221a8` go through the guest bridge (hooks keep their own
modes); `47e08d` (Q16) and `47e0c0` (Q14) are pure and computed in core
(the helpers of the flight model core).

### Player flyer input `0x41bb9c`

`0x41bb9c(P, turn, throttle, pitch, twist)`. EAX = `[P+0x20e]` (part
word array); frame: F at EBP−24h = (word +0Eh = 0), (word +10h = 0),
(word +8 = 0), (word +0Ah = 0) as 0/1 dwords (SETE, AND 1); C at
EBP−14h = turn, throttle, twist, pitch, terrain height.
`terrain_height([0x4a0be0], P+0x26)` with EAX = `[0x4a0be0]`, EDX = twist,
ECX = P+26h; its EAX → EBP−4. Then `flight_model(C, P+0x2b9,
[P+0x1f2]+0x1dc, F, 1)` with EAX = C, EDX = F, ECX = M. Afterwards, only
if pitch ≠ 0 (JE `0x41bc3b`): word `P+0x290` ← word `S+0x1e` (reread after
the call), byte `P+0x93` ← 1, EAX = P+92h. Entry EAX/EDX/ECX are dead
(overwritten before any read).

### AI flyer command `0x4221a8`

`0x4221a8(A, R)`. ECX = R; EBP−4 = S = A+243h. x = `R+0`, y = `R+4`:
EAX = sar32(low32(x·x), 8) (one-operand IMUL), EDX = sar32(low32(y·y), 8);
negated when x < 0 (JGE `0x4221d4`) and y < 0 (JGE `0x4221da`); each
clamped by signed dword compares to [−100h, 100h] (`0x4221e3`/`0x4221f1`,
`0x422204`/`0x422213`) and stored back to `R+0`, `R+4` (the caller's
record; squares of 2¹⁵·√2 and more wrap before the shift). F at EBP−14h =
four zero dwords. `flight_model(R, S, [A+0x1fa]+0x3a, F, 1)` with EAX = M,
EDX = S, ECX = R. Entry registers dead.

### AI flyer bank `0x42215c`

`0x42215c(A, goal, R)`. `R+0` ← q16(s16(goal) − s16(`A+0xe`), 9C4h)
(EDX = s16(`A+0xe`) before the product), then `R+0` += q16(s16(`A+0x25d`),
−1388h) (read after the first store); `0x4221a8(A, R)` with EAX/EDX = the
second product's EAX/EDX and ECX = s16(`A+0x25d`) (47e08d keeps ECX).
Only the low word of goal is read. Entry registers dead.

### AI flyer steering `0x4222fc`

`0x4222fc(A, yaw, pitch, T)`. ESI = q16(−s16(yaw), 9C4h). Words:
EBP−2 = G = word `[A+0x1fa]+0xe` (EAX = `[A+0x1fa]`, MOV DX),
EBP−4 = roll = word `A+0xe` (MOV CX), EBP−6 = word `A+0x287` (MOV AX).
If T ≠ 0 (JE `0x422345`): CX = word `A+0x10` − word `T+0x10` → EBP−0Ah;
EBP−10h = s16(`T+0x1f8`); `heading_cos(slot)` with slot = ECX, whose high
word is still **the entry ECX's high word** (EAX = T, EDX =
s16(`T+0x1f8`)); POP ECX; `47e0c0(s16(T+0x1f8), cos)` → EBP−8 = low
word; word EBP−6 −= q16(s16(EBP−8), C8h). Then SI += q16(s16(EBP−6),
7D00h) and SI += q16(s16(`A+0x25d`), −1388h) (word adds). Record R at
EBP−28h: +4 = pitch, +8 = 0, +0Ch = 0, +10h =
`terrain_height([0x4a0be0], A+0x26)` (EAX = A+26h, EDX = `[0x4a0be0]`,
ECX = 0). With roll = s16(EBP−4):

1. roll ≥ s16(G) − 5DCh (JL `0x422403`) and SI > 0 (JLE `0x422408`):
   `0x42215c(A, slot, R)`, slot = high word of s16(roll), low word G;
   EAX = s16(G) − 5DCh, EDX = R.
2. else roll ≤ 5DCh − s16(G) (JG `0x422433`) and SI < 0 (JGE `0x422438`):
   `0x42215c(A, slot, R)`, slot = high word of s16(roll), low word −G;
   EDX = 5DCh − s16(G), ECX = R.
3. else DX = |yaw word| (8000h → 7FFFh, JNE `0x422456`; JGE `0x422461`;
   EDX keeps the high word of the step-2 EDX, or of the yaw slot). If DX
   < 7D0h (JGE `0x422471`): if word `A+0xe` (reread) < 320h (JGE
   `0x422479`) then R+8 ← q16(−s16(yaw), FA0h); `0x42215c(A, 0, R)` with
   ECX = R and EAX/EDX as left.
4. else R+0 ← s16(SI), R+8 ← 0 (EDX = 0); `0x4221a8(A, R)` with EAX =
   s16(SI), ECX = R.

`R+0` is uninitialized when `0x42215c` is called; that function writes it
before `0x4221a8` reads it. Entry EAX and EDX are dead (the first q16
overwrites both); entry ECX is not (its high word reaches heading_cos's
slot when T ≠ 0; heading_cos reads only the low word).

### Flyer control result registers and ECX

EDX:EAX is reproduced exactly (the bridge returns the callee's pair). ECX
after each function is the flight model's ECX, which is dead on every path
([Flight model registers](#flight-model-registers): `0x460afb` →
`0x460ce6`; the AI-state procedures and `target_emit` above `0x4222fc`).
At `0x422684` the AI state pushes the low word of its EBP−4 with
`0x4222fc`'s EDX high word into `0x422260`, which reads only the word
(`movswl 0x10(%ebp)`).

### Flyer control stack residue chain

The flight model copies one of two stack bytes from its entry ESP−191h /
ESP−1A5h into the pose byte actor+25h
([Flight model stack residue](#flight-model-stack-residue-and-the-entry-shim)).
In an all-original call the flight model's entry ESP lies at a fixed depth
below each caller's entry ESP: 4Ch (`0x41bb9c`), 38h (`0x4221a8`), 50h
(`0x42215c`, via `0x4221a8` at 18h), 98h (`0x4222fc` via `0x42215c` at
48h) or 7Ch (`0x4222fc` via `0x4221a8` at 44h). No intermediate callee
reaches that deep (`terrain_height` has no calls and a 5Ch-byte frame;
`heading_cos`, `47e08d` and `47e0c0` use `enter 0,0`), so the bytes are
those present at the outer function's entry:

| Hook | Entry bytes read by the shim | Registers | Planted for the callee at its entry |
|---|---|---|---|
| `flyer_input` | ESP−1DDh, ESP−1F1h | CL, CH | flight_model: ESP−191h, ESP−1A5h |
| `flyer_command` | ESP−1C9h, ESP−1DDh | CL, CH | flight_model: ESP−191h, ESP−1A5h |
| `flyer_bank` | ESP−1E1h, ESP−1F5h | CL, CH | `0x4221a8`: ESP−1C9h, ESP−1DDh |
| `flyer_steer` | ESP−229h, ESP−23Dh; ESP−20Dh, ESP−221h | AL, AH; DL, DH | `0x42215c`: ESP−1E1h, ESP−1F5h; `0x4221a8`: ESP−1C9h, ESP−1DDh |

The adapter's stack call pushes the slots and writes the two bytes below
ESP at the callee's original offsets before the CALL, as `weapon_fire`
does for its preparation. `flyer_steer` uses EAX/EDX because its entry
ECX is live. The replacement always
delivers the entry bytes (the all-original value). In vitro the original's
stubs see exactly the planted entry bytes at these offsets (outside the
branch-trap phase), and with the real flight model and the nested hooks
(`flight_model`, `0x4221a8`, `0x42215c`, `heading_cos`) not installed,
original or replaced, a planted byte reached the pose byte actor+25h in
about 99 % of the cases, with equal results. That the framework's dispatch
of a nested hook never reaches those depths in the game is not claimed
([hooks.md](../reference-game/hooks.md): raw-residue parity across
different call trees).

### Flyer control branch sites

16 sites: `0x41bc3b`; `0x4221d4`, `0x4221da`, `0x4221e3`, `0x4221f1`,
`0x422204`, `0x422213`; `0x422345`, `0x422403`, `0x422408`, `0x422433`,
`0x422438`, `0x422456`, `0x422461`, `0x422471`, `0x422479`. `0x42215c`
has none. All are reachable both ways from the arguments and the callee
results (no proof needed).

## The Win32 shell region `0x464a14`–`0x466034`

The address range between the SFX manager and the flight model is DBSIM's
Win32 shell, not flyer physics and not audio. Read with entry-aligned
re-disassembly behind the jump table of `0x466034` and the import thunks
resolved.

### Shell region overview

1. **No function of the region is flyer physics, and none is audio.**
   `0x464a14`–`0x466586` is DBSIM's Win32 shell: WinMain `0x46538c`, window
   class and window creation, the DIB/DirectDraw frame buffer, frame
   presentation, palette handling, two message pumps, the handler list of
   the window procedure and the window procedure itself. The image's own
   symbols name `0x466034` (objdump labels `@MainWndProc$qqspvuiuil+…`
   resolve to it, e.g. `0x49692e` = `+0x308fa`) and `0x462398`
   (`@sfxWndProc$qqspvuiuil`). The functions after it up to the flight
   model's helpers (`0x46658c`..`0x466a85`: `DirectDrawCreate`, cooperative
   level and surfaces, release, the windowed/DirectDraw toggle `0x4667c8`,
   WinHelp, the RTTI block "AnimDisplay" and its destructor `0x466a59`)
   are platform code as well.
2. **Boundary to the audio code.** The region starts right after the SFX
   manager (`0x46499c`/`0x4649b8` are its last functions; `0x4649d8`, which
   sets the frame-buffer origin `0x4d3018`, already belongs to the display
   code). `0x465628` (presentation) and `0x465cc4` (palette upload) are
   not audio ([presentation.md](presentation.md)), and neither is the rest
   of the region. The only contacts with audio are indirect, through
   MainWndProc: its handler list holds `sfxWndProc` `0x462398` (registered
   at `0x46242d`), and its focus and power cases call the pause/resume
   functions `0x45f0d4`/`0x45f108`, which pause/resume the sounds
   (`0x46317c`/`0x463238`, the "was playing" flag `p+7` of
   [audio.md](audio.md)). Static reach from `0x465e28`, `0x465a24`,
   `0x46548c`, `0x465628`, `0x464a14`, `0x465cc4` (28 functions) and from
   `0x466034` without the handler targets (42 functions) contains no
   reference to RNG stream A `0x4d2599`, stream B `0x4d260b`, the clock
   `0x467828`, the sound API or the frame counter `0x4d257b`.
3. **What is simulation-relevant is routing, not computation.** MainWndProc
   computes nothing that the oracle compares; it forwards window messages
   to (a) the handler list: keyboard `0x477bec` (hook `key_message`, key
   ring `0x6bb6b0`), mouse `0x4809f8` (event queue `0x480948`),
   `sfxWndProc`, and the never-live video MCI handler `0x4718d4`; (b)
   pause/resume on `WM_KILLFOCUS`/`WM_SETFOCUS`/power suspend and resume
   (pause flag `0x4d2572`, the tick log's compared pause flag; cockpit
   clocks `0x435b58`/`0x435b80`); (c) `WM_CLOSE`/`WM_QUERYENDSESSION` →
   the command handler `0x45feb8(0x410)` (the Ctrl+Q dialog of
   [input.md](input.md)) or, in demo playback (word `0x4d2530`), the stop
   word `0x4d2532`.
4. **No simulation part.** `0x465e28`, `0x465a24` (with `0x465c40`) and
   `0x46548c` belong to the presentation contract next to
   `0x465628`/`0x465cc4` ([presentation.md](presentation.md));
   MainWndProc belongs to the platform shell (window and message layer).
   It is not replaced (gap: referenced only); a replacement must keep
   every edge of point 3 as a call of the original target.

### Function table

Instruction counts from the listing (the map's where it has a row). None
of these is a simulation function.

| VA | Instr. | Calls per suite | Role (from the bytes and imports) | Class |
|---|---:|---|---|---|
| `0x464a14` | 5 | n/p (≥ `0x465628`'s count: its only other caller is dead) | present the whole frame: `0x465628(&[0x4d3018], &[0x4d2ff8])`; 29 callers: display-mode code `0x42b135`…`0x42d040`, UI `0x454906`, `0x454b60`, `0x457205`, presentation `0x45fbdf`, `0x4614a3`, `0x461e1c`, MainWndProc `0x4662a7` (`WM_PAINT`) and `0x466449` (message `0x812`) | presentation |
| `0x464a28` | ≈59 | no caller | present a window-relative rectangle through `0x465628` | dead |
| `0x464ad8` | ≈30 | no caller | two `LoadStringA`, `MessageBoxA` | dead |
| `0x464b2c` | 94 | 0 | centre the window on its parent or the screen (`GetSystemMetrics`, `GetWindowRect`, `GetParent`, `SetWindowPos`); only from the `0x812` case | window shell |
| `0x464c10` | 38 | 0 | centred rectangle → `SetRect` (window creation) | window shell |
| `0x464c68` | 6 | 1×6 | store the cursor handle `[0x4d3720]` (callers `0x45f449`, WinMain) | window shell |
| `0x464c78` | 29 | 1000×6 | message pump: `PeekMessageA(PM_REMOVE)` → `TranslateMessage`/`DispatchMessageA` until empty; AX = 1 on `WM_QUIT` (`0x12`), else `SetCursor([0x4d3720])` when set and AX = 0; 15 callers | platform (kept original) |
| `0x464cc4` | 41 | n/p (startup) | initialise the display record `0x4d3020` (BITMAPINFOHEADER size `0x28`, width/height from `0x4d2546`/`0x4d254a`, 1 plane, 8 bit) | window shell |
| `0x464d44` | 293 | 0 (startup) | create the memory DC and DIB section, read and swap the system palette, build the off-screen surface object `[0x4d3020]` | window shell |
| `0x4650cc`, `0x4650d0` | 1, 8 | n/p | empty; `KillTimer([0x4d3610], [0x4d3608])` | window shell |
| `0x4650e8` | 35 | 0 | `RegisterClassA` with `lpfnWndProc` = `0x466034` (stored at `0x4650fb`) | window shell |
| `0x465158` | 135 | 0 | create the main window `[0x4d3610]` (`CreateWindowExA`; screen-sized and topmost when `[0x4d255e]` ≠ 0), then `0x464d44`, `ShowWindow`, `UpdateWindow` | window shell |
| `0x4652f8` | 65 | 0 | split the command line into an argv array | launcher ([launcher.md](launcher.md)) |
| `0x46538c` | 86 | 0 (before arming) | WinMain (DATA pointer `0x497044`, `ret 0x10`): argv, option parsing `0x45e510`/`0x45e6cc`, class and window, DirectDraw switch `0x4667c8` + `0x46548c` when `[0x4d255e]`, cursor, game main `0x45f160`, `PostQuitMessage`, pump until `WM_QUIT`, `0x466730`, returns `[0x4d27b8]` | launcher, window shell |
| `0x46548c` | 117 | 0/1/0/1/1/1 | clear the frame buffer to 0 and show it (below) | presentation |
| `0x465618` | 6 | 0 | store the cursor sprite `[0x4d3724]` (caller `0x461ab8`) | window shell |
| `0x465628` | 303 | 1000/1000/1000/1000/1000/940 | frame presentation; also copies the cursor sprite `[0x4d3724]` at `GetCursorPos` into the locked surface | presentation |
| `0x4659e8` | 19 | 0 | second pump without the `WM_QUIT` test (`0x465a14` wraps it; no caller) | platform |
| `0x465a24` | 167 | 0/0/0/52/0/0 | build the GDI logical palette (below) | presentation |
| `0x465c40` | 48 | 0/0/0/52/0/0 | flush the system palette to black (below) | presentation |
| `0x465cc4` | 115 | 177/1000/738/865/601/436 | palette upload | presentation |
| `0x465e28` | 113 | 0/0/0/52/0/0 | install a game palette (below) | presentation |
| `0x465f7c` | 33 | 0 | load a palette object through the DTS stream (`0x472f14`, `0x47a144`; callers `0x43039c`, `0x461715`; [dts.md](../formats/dts.md)) | file I/O |
| `0x465fd4` | 9 | no caller | `SetWindowTextA` | dead |
| `0x465fec` | 16 | 0 (startup) | put a handler into the first free slot of the 10-slot list `0x4d3b30` (callers `0x46242d` sfx, `0x471931` video, `0x477a58` keyboard, `0x4806a1` mouse) | platform |
| `0x466010` | 17 | 0/1/0/1/1/1 | remove a handler (callers `0x46275a`, `0x471a40`, `0x477a70`, `0x480712`) | platform |
| `0x466034` | 389 (map 338) | 1000×6 | MainWndProc (outline below) | window shell |

The map's 338 for `0x466034` misses the cases reached only through the jump
table (the map counts linearly reached instructions): the entry-aligned
listing has 33 instructions before the table and 358 after it, two of them
unreachable (`jmp`s at `0x4661fc`, `0x466282`).

### Palette and frame-clear contracts `0x465e28`, `0x465a24`, `0x465c40`, `0x46548c`

- **`0x465e28(P)`** (cdecl, one slot; `ret`): returns at once for P = 0.
  Stores P in `[0x4d3b2c]`. P+8 points to 256 four-byte entries; for each,
  byte 3 bit 0 set → the three colour bytes ×4 (6 → 8 bit) in reversed
  order into a 1 KiB frame array, else, with byte 3 bit 1 set, the bytes
  unscaled; with neither bit the frame entry is not written (stack
  residue reaches the palette; a presentation quirk). Then
  `0x465c40()`, `[0x4d302c]` ← `0x465a24(frame, 256, P)`, `GetDC(0)`,
  select/realize `[0x4d302c]`, `SetDIBColorTable([0x4d3034], 0, 256,
  frame)`, `ReleaseDC`. With `[0x4d255e]` ≠ 0 (DirectDraw mode) it copies
  the entries with flag 1 into `0x4d372c`, calls
  `IDirectDraw::CreatePalette` (`[0x4d3710]` slot `+0x14`, flags `0x4c`,
  out `0x4d371c`) and on success `IDirectDrawSurface::SetPalette` on the
  primary `[0x4d3714]` (slot `+0x7c`). No previous GDI or DirectDraw
  palette handle is released here. Callers: `0x481fe0` (select a palette
  object; its callers are the palette flush `0x43048c` of
  [cockpit.md](cockpit.md) and `0x461608`) and MainWndProc's
  `WM_QUERYNEWPALETTE` case `0x466228` (GDI mode only). The map counts
  are 52 for both `0x481fe0` and `0x465e28` in c3_09, so none of the
  c3_09 calls came through the window procedure.
- **`0x465a24(entries, count, P)`** (cdecl; P is not read): writes the
  LOGPALETTE header `0x4d3728` (version `0x300`, 256 entries) and
  returns `CreatePalette(0x4d3728)` in EAX. With
  `GetSystemPaletteUse` = `SYSPAL_NOSTATIC` (2): `count` entries copied
  with flag 1, the rest flag 0, entry 255 = white, entry 0 = black.
  Otherwise the system palette is read, the reserved entries
  (`GetDeviceCaps(NUMCOLORS)` / 2 at each end; index 0x18 at `0x465ae9`)
  keep flag 0, every game colour between them that equals a static colour
  gets its third byte (`+2`) moved by +3 (−3 from `0xfc` up), and every
  one gets flag 5.
- **`0x465c40()`**: `CreatePalette` from the 1 KiB template `0x4a0404`
  with all 256 entries black and flag 5, select and realize it on the
  screen DC, select the previous palette back, `DeleteObject`.
- **`0x46548c()`** (no arguments): DirectDraw mode: restore a lost
  primary (`IsLost` = `DDERR_SURFACELOST` `0x887601c2` → `Restore`), lock
  the primary (width `[0x4d253e]` ≥ 640) or the back buffer `[0x4d3718]`
  with `DDLOCK_WAIT`, zero `[0x4d2542]` rows of `[0x4d253e]` bytes at the
  locked pitch, unlock, and flip when the width is below 640. GDI mode
  with the DIB DC `[0x4d3034]`: `BeginPaint`, zero the DIB bits
  `[0x4d3028]` (size `[[0x4d3020]+6]`), `StretchBlt` of its 10 × 10 corner
  over the client rectangle (`SRCCOPY`), and, when `[0x4d302c]` is set,
  `SelectPalette(hdc, ESI, TRUE)` and `RealizePalette`, where ESI is
  **the caller's ESI** (the GDI branch never writes ESI; a presentation
  quirk), `EndPaint`. Callers `0x45f4da`, `0x4616a9`, `0x461a6f`, WinMain
  `0x465441`, `0x46673c`. The oracle runs the GDI branch (`[0x4d255e]` =
  0, [presentation.md](presentation.md)); ES.EXE never passes `-Z`
  ([launcher.md](launcher.md)), and the windowed/DirectDraw toggle
  `0x4667c8` can switch at run time.

None of the four touches an actor, a pool, RNG, clock or a compared global.

### MainWndProc `0x466034`

**Entry and result.** `LRESULT __stdcall (hwnd, msg, wParam, lParam)`,
`ret 0x10`; prologue `55 8b ec 53 56 57 8b 7d 0c` (EDI = msg, EBX/ESI/EDI
callee-saved). No entry register is read. Result in EAX; EDX and ECX are
whatever the last callee left (USER32 is the caller, it does not read
them). No local frame. Callers: USER32 only (`DispatchMessageA` from the
pumps `0x464c78`/`0x4659e8` and sent messages such as the focus pair); the
only reference is the WNDCLASS store at `0x4650fb`.

**Steps.**

1. Handler list: for i = 0..9, if dword `0x4d3b30[i]` ≠ 0, call it
   (stdcall, the same four arguments); a nonzero EAX ends the procedure
   with EAX = 0 (`0x46605c`). A zero slot is skipped, not the end of the
   list.
2. Dispatch on msg (jump table `0x466089`, 23 entries for 0..`0x16`, plus
   compare chains above `0x24`):

| msg | Case | Action | Result |
|---|---|---|---|
| `0x02` `WM_DESTROY` | `0x46613e` | `0x4650cc`, `0x4650d0`, `PostQuitMessage(0)` | 0 |
| `0x07` `WM_SETFOCUS` | `0x466373` | `0x45f108` (resume: pause flag `0x4d2572` ← 0, sounds, cockpit clocks) | 0 |
| `0x08` `WM_KILLFOCUS` | `0x466369` | `0x45f0d4` (pause: `0x4d2572` ← 1, sounds, cockpit clocks) | 0 |
| `0x0f` `WM_PAINT` | `0x466287` | DirectDraw mode: `DefWindowProcA`; else `0x464a14` | Def / 0 |
| `0x10` `WM_CLOSE` | `0x466508` | word `[0x4d2530]` ≠ 0: word `0x4d2532` ← 1; else word `0x4d370c` ← AX of `0x45feb8(0x410)` | 0 |
| `0x11` `WM_QUERYENDSESSION` | `0x466530` | word `0x4d370c` ← AX of `0x45feb8(0x410)` | 1 if that AX = 2, else 0 |
| `0x16` `WM_ENDSESSION` | `0x46655b` | wParam = 1: `DestroyWindow(hwnd)` | 0 |
| `0x24` `WM_GETMINMAXINFO` | `0x4662b1` | fills the MINMAXINFO at lParam (track sizes from `GetSystemMetrics` and the game size; `0x514` × `0x320`, `−200`) | `DefWindowProcA` |
| `0x7e` `WM_DISPLAYCHANGE` | `0x46647b` | `[0x4d36f4]`, `[0x4d36f8]` ← new screen size; unless `[0x4d36fc]`, `PostMessageA(hwnd, 0x812, 0, 0)`; `[0x4d36fc]` ← 0 | `DefWindowProcA` |
| `0x106` `WM_SYSCHAR` | `0x466137` | swallowed | 0 |
| `0x218` `WM_POWERBROADCAST` | `0x4664d1` | wParam 4 → `0x45f0d4`; 7 → `0x45f108` | 0, else `DefWindowProcA` |
| `0x219` `WM_DEVICECHANGE` | `0x466453` | (dead arithmetic on wParam) | `DefWindowProcA` |
| `0x30f` `WM_QUERYNEWPALETTE` | `0x466201` | GDI mode: `0x465e28([0x4d3b2c])`, select/realize `[0x4d302c]`, `InvalidateRect` if colours changed | realized count; DirectDraw mode `DefWindowProcA` |
| `0x311` `WM_PALETTECHANGED` | `0x466154` | GDI mode, wParam ≠ hwnd: clear the flags of entries 10..245 of `0x4d372c`, `[0x4d302c]` ← `CreatePalette(0x4d3728)`, realize, `InvalidateRect` if changed | realized count / 0 |
| `0x812` (private) | `0x46637d` | re-read the screen size; equal to the game size `[0x4d253e]`×`[0x4d2542]`: window fitted with `AdjustWindowRectEx`/`SetWindowPos`; else `SetWindowPos(50, 50, 0x294, 0x212)` and `0x464b2c`; then `0x464a14` | 0 |
| others | `0x46656c` | | `DefWindowProcA` |

**Writes** (all platform globals): `0x4d2532` (demo stop word; read at
`0x456022`), `0x4d370c` (written only, no reader in the file),
`0x4d302c`, the palette flags at `0x4d372c`, `0x4d36f4`/`0x4d36f8`/
`0x4d36fc`, the MINMAXINFO at lParam. Through callees: the pause flag
`0x4d2572` and clock state (`0x45f0d4`/`0x45f108`), the input queues (the
handlers), and whatever `0x45feb8(0x410)` does (dialog; `0x4d24fe` ends
the main loop).

**Callees and their modes for a replacement.** Keep original through the
bridge: the handler-list targets (`0x477bec`, `0x4809f8`, `0x462398`),
`0x45f0d4`, `0x45f108`, `0x45feb8`. Own shell code or USER32/GDI
pass-through: `0x464a14`, `0x465e28`, `0x464b2c`, `0x4650cc`, `0x4650d0`
and the imports. **Branch sites:** 32 conditional jumps (`0x466047`,
`0x46605a`, `0x46606a`, `0x466071`, `0x466073`, `0x46607c`, `0x4660eb`,
`0x4660ed`, `0x4660f6`, `0x466102`, `0x46610a`, `0x46611b`, `0x466124`,
`0x46612c`, `0x46615b`, `0x46617a`, `0x466197`, `0x4661e6`, `0x466208`,
`0x46626c`, `0x46628e`, `0x4662f0`, `0x4663a0`, `0x4663ae`, `0x466459`,
`0x46649f`, `0x4664d7`, `0x4664dc`, `0x466510`, `0x46654a`, `0x46654c`,
`0x46655f`), the jump table `0x466082` and the indirect handler call
`0x466056`. **Live:** `0x466034` 1000×6; which messages arrive after
arming is not measured (the case targets were not probed). An oracle
comparison cannot see this layer except through the pause flag, the input
queues and the loop-exit words; the input journal already subclasses the
window in replay mode for the focus pair
([input_journal.md](../reference-game/input_journal.md)).

## The FLYER AI state methods

The behaviour and decision methods of the FLYER state records. Roles are
hypotheses; the register and memory effects are what the replacement
reproduces and the in-vitro test compares. Hook group `flyer_states`
(core `core/src/flyer_states.c`, `es2/core/flyer_states.h`; adapter
`tools/reference-game/proxy/flyer_states_hooks.h`). Table bytes read from
the image; stack depths from the control-flow walker (scratch, not kept).

| Hook | VA | Role (hypothesis) | Prologue / relocated |
|---|---|---|---|
| `flyer_attacking` | 422ca8 | behaviour of "attacking": leader fires (`target_emit`), wingmen follow | `55 8b ec 56 57` / 5 (1,2,1,1) |
| `flyer_follow` | 422598 | wingman: formation point beside the leader, steering, throttle | `55 8b ec 83 c4 ec` / 6 (1,2,3) |
| `flyer_formation` | 422a50 | leader route step (`0x4224c4`) or wingman follow | `55 8b ec 53 8b 5d 08` / 7 (1,2,1,3) |
| `flyer_search` | 422a80 | behaviour of "search and destroy" | `55 8b ec 53 56` / 5 (1,2,1,1) |
| `flyer_patrol` | 422b34 | behaviour of "patrolling" | `55 8b ec 53 56` / 5 (1,2,1,1) |
| `flyer_scout` | 422bdc | behaviour of "scouting" | `55 8b ec 8b 45 08` / 6 (1,2,3) |
| `flyer_decision` | 422d00 | decision of the active records and "deciding": next state from the group's order | `55 8b ec 53 8b 55 08` / 7 (1,2,1,3) |
| `flyer_engage` | 422bf0 | switch to "attacking" and hand the target to the wingmen | `55 8b ec 51 53` / 5 (1,2,1,1) |

### State method reach and live counts

The FLYER state records (`0x499cf8`, stride `0x3c`, [vtables.md](vtables.md)
section 3.2) hold the behaviour member pointer at `+0x18` (called by the
trampoline `0x42184c`, slot `+0x18`) and the decision member pointer at
`+0x30` (`0x421888`, slot `+0x1c`). The constant groups at `0x499e9c` +
`0x24`·k hold them as dwords; a byte scan of the whole file finds
`0x422ca8` only at `0x499ec0`, `0x422b34` at `0x499ee4`, `0x422a80` at
`0x499f08`, `0x422bdc` at `0x499f50`, `0x422d00` at `0x499eb4`,
`0x499ed8`, `0x499efc`, `0x499f20`, `0x499f68`, and no absolute reference
to the helpers. Direct callers (E8 scan): `0x422598` ← `0x422a6d`
(`0x422a50`), `0x422cf4` (`0x422ca8`); `0x422a50` ← `0x422a8c`,
`0x422b41`, `0x422be3`; `0x422bf0` ← `0x422b28`, `0x422bcd`, `0x422c81`
(itself); `0x4224c4` ← `0x422a64` only.

Entry registers from the trampolines (read from `0x42184c`/`0x421888`):
EAX = the state record, EDX = the member pointer's third dword (0 in the
retail tables), ECX = the OR of its three dwords (the method's own
address in the retail tables). After the call both trampolines `pop ecx`
and return the method's EDX:EAX; `0x411cec` reads only AX after slot
`+0x18` (AX ≠ 0 clears the state timer `[A+0x52]`) and nothing after
`+0x1c`. So ECX after return is dead for all eight functions (every
caller pops it) and EDX is dead at `0x411cec`; the replacement still
returns the exact EDX:EAX. `0x4217fc` (slot `+0x14`) reaches only
`flyer_move`, not these methods.

Map entry probes (a60/c1s/c1c/c3_09/c4_08/c3_10): `0x422ca8`
1000/1000/1000/0/0/0 (saturated); `0x422a80`, `0x422a50` and `0x4224c4`
638/638/638/0/0/0; `0x422d00` 14/14/14/0/0/0; `0x422bf0` 2/2/2/0/0/0;
`0x422598`, `0x422b34`, `0x422bdc` have no probe row. Since `0x422a50`'s
count equals `0x422a80`'s and `0x4224c4`'s, patrolling and scouting did
not run in the probe runs and every `0x422a50` call took the leader path.

### State method contracts

Notation as in [ai.md](ai.md): A the flyer, G = `[A+0x45]` its group, L =
`[[G+0x0c]]` the group's first member (the leader), T = `[A+0x1a4]` the
target, B = A+0x5a.

**`0x422ca8` attacking(A).** Reads no entry register. EAX = A, EDX = T,
ECX = T+0x92. Byte T+0xa4 ≠ 0 (JNE `0x422cc2`) or byte T+0x99 ≠ 0 (JNE
`0x422cca`): EDX = 1, return AX = 1 over A's high word (the target is
dead or out; the trampoline's result clears the state timer, so a
decision follows next frame). Otherwise EDX = 0 (JE `0x422cd8`), ECX = G,
EDX = `[G+0x0c]`; A = L (JNE `0x422ce8`): `target_emit`(A), else
`0x422598`(A), regs (A, `[G+0x0c]`, G); return EAX = 0, EDX = the
callee's. T = 0 faults (an attacking flyer always has a target).

**`0x422598` wingman follow(A).** Frame: P = EBP−0x14 (3 dwords), EBP−8
(written 0 and read back), EBP−4 = d. Reads no entry register.

1. L from G (`[G+0x0c]` read once; L stays in ESI). P ← L+0x26 (x, y, z;
   ECX = y, EAX = z afterwards). `point_offset`(P, lo(x, word L+0x10),
   0x2000), regs (z, lo(x, L+0x10), P).
2. A's slot `+0x78` (the FLYER formation point `0x421e98`, original)
   (A, P), regs (P, `[A]`, point_offset's ECX).
3. d = `dist_gate`(P, A+0x26), regs (P, the slot's EDX/ECX); d → EBP−4.
4. h = AX of `heading_point`(P, A+0x26) − word A+0x10 (16-bit), regs (d,
   P, dist_gate's ECX); EDI = h over heading_point's high word.
5. c1 = (u16)(word A+0x10 − word L+0x10 + 0x4000) < 0x8000 (both words
   reread), EDX = c1 (SETB/AND). If c1 (JE `0x422622`): ECX = lo(EDI,
   h + 0x4000), EAX = c2 = (u16)(h + 0x4000) < 0x8000; if not c2 (JNE
   `0x422639`) and s32 d < 10000 (JGE `0x422642`, d reread): EDX = 0x8000
   − h (16-bit; the high word is 0 because EDX was 1), EDI = EDX.
6. ECX = 0, EBP−8 ← 0; pitch = `0x422108`(A, `[EBP−8]` + 0x7530), regs
   (0x7530, EDX of step 5, 0).
7. `flyer_steer`(A, EDI, pitch, L), regs = 0x422108's.
8. s32 d > 15000 (JLE `0x422677`, d reread): word A+0x21c ← 0x100, return
   flyer_steer's EDX:EAX. Otherwise `0x422260`(A, L, lo(flyer_steer's
   EDX, word d), EDI, 0x2000), regs (flyer_steer's EAX, that EDX, its
   ECX); return 0x422260's EDX:EAX.

**`0x422a50` formation step(A).** EAX = G, EDX = `[G+0x0c]`, EAX = L;
A = L (JNE `0x422a61`): `0x4224c4`(A) (the leader's route step), else
`0x422598`(A); regs (L, `[G+0x0c]`, **the entry ECX**). Then EAX =
A+0x92, byte A+0x96 ← 0; EDX = the callee's. The entry ECX is live:
`0x4224c4` loads only CX (`mov cx, [edx]` at `0x4224e0`) and pushes ECX
+ 1 as the second slot of `0x423b0c`, so the high word of the ECX the
trampoline passed to the state method reaches that slot. The replacement
forwards it unchanged.

**`0x422a80` search and destroy(A), `0x422b34` patrolling(A).** Both:
`0x422a50`(A) with the **entry EAX/EDX/ECX**, then ECX = A (pop). EAX =
G, EDX = `[G+0x0c]` (reread), ECX = (A = `[EDX]`) (SETE/AND); not the
leader (JE `0x422aa3`/`0x422b58`): return EAX = 0. `timer_word_step`(B),
regs (G, `[G+0x0c]`, 1); ECX = B, EAX = (AX = 0); still running (JE
`0x422abc`/`0x422b6d`): return. Word B+1 ← 10000; N =
`target_select`(A, kind, 0), regs (1, the timer's EDX, B); kind 0x11
(search) or 0x10 (patrol).

- Search: N = 0 (JE `0x422ad5`): return. `order_target`(`[A+0x45]`
  reread, N), regs (N, that G, target_select's ECX); AX = 0 (JE
  `0x422ae7`): return. If N ≠ T (JE `0x422af3`; T read here): T ≠ 0 (JE
  `0x422af7`) → word `[[A+0x1a4]+0x1a2]` −= 1 (ECX = the reread
  pointer); `[A+0x1a4]` ← N; N ≠ 0 (JE `0x422b0e`, never taken: N was
  tested at `0x422ad3` and stays in callee-saved ESI across
  `order_target`; PROVED) → word `[[A+0x1a4]+0x1a2]` += 1 (EAX = the
  reread pointer); EDX = A+0x92, byte A+0x9d ← 1. Then `0x422bf0`(A).
- Patrol: if N ≠ `[A+0x1a4]` (JE `0x422b8a`): `[A+0x1a4]` ≠ 0 (JE
  `0x422b93`) → EDX = it, its word +0x1a2 −= 1; `[A+0x1a4]` ← N; N ≠ 0
  (JE `0x422baa`, both ways) → EAX = `[A+0x1a4]`, its word +0x1a2 += 1;
  byte A+0x9d ← 1. Then `[A+0x1a4]` = 0 (JE `0x422bca`): return; else
  `0x422bf0`(A).
- Return EAX = 0, EDX = the last writer (a callee's or a pointer above).

**`0x422bdc` scouting(A).** `0x422a50`(A) with EAX = A and the entry
EDX/ECX; return EAX = 0, EDX = the callee's. No branch.

**`0x422d00` decision(A).** EDX = A, EAX = G, index = `[G+0x6c]`; the
current order O = `[G + 4·index + 0x44]` (32-bit address arithmetic; the
index is read again for the load). O ≠ 0 (JE `0x422d12`): AX = word O+0
over O's high word; else AX = 0x0b over G's. EAX = sx(AX); unsigned > 5
(JA `0x422d2a`, so negative words and 0x0b too) → default; else the
jump table `0x422d33` (six dwords from the image): 0 → ECX = `0x499dac`
(search and destroy); 1, 2 → default; 3 → `0x499d70` (patrolling); 4 →
`0x499de8` (sleeping) and byte A+0xa5 ← 1 (EAX = A+0x92); 5 →
`0x499e24` (scouting) and byte A+0xa5 ← 1. Then `script_start`(A+0x4d,
ECX), regs (ECX, A+0x4d, ECX); return AX = 1 over script_start's EAX,
EDX = its EDX.

**Defect (quirk).** On the default path (order types 1, 2, others, no
current order) ECX is never loaded, so the record is **the entry ECX**.
Through the retail trampoline `0x421888` that is the member-pointer OR,
i.e. `0x422d00` itself: the flyer's state pointer becomes code,
`script_start` takes the dword at `0x422d04` as the state timer, and the
next frame's trampolines would call code bytes as member pointers (a
crash). The suites do not crash, so their flyers only get order types 0,
3, 4, 5 (hypothesis consistent with the counts). The replacement
forwards the entry ECX unchanged. With `flyer_decision` installed the
dword at `0x422d04` holds the hook's patched entry bytes, so this already
broken path would also read other timer bytes (the real-callee test
therefore passes a state record as entry ECX). The replaced trampolines
(`flyer_move_call` `0x4217fc`, `flyer_act` `0x42184c`, `flyer_decide`
`0x421888`) pass ECX = the OR (`es2_state_member_registers` in
`core/src/actor_step.c`), so this path stays exact.

**`0x422bf0` engage(A).** `script_start`(A+0x4d, `0x499d34`
"attacking"), regs (A+0x4d, entry EDX, entry ECX). EDX = `[A+0x45]`, ECX
= `[EDX+0x0c]`, EAX = (A = `[ECX]`); not the leader (JE `0x422c1b`):
return EAX = 0, EDX = G. Loop i = 0, 1, … (ESI; the bound test compares
SI): EDX = `[A+0x45]` (reread every time); s16 i ≥ s16 word G+0x10 (JGE
`0x422c8f`) → EAX = 0, stop; M = `[[G+0x0c] + 4·sx(i)]` (EDX = the member
array); M = 0 (JNE `0x422ca0`) → stop. M = A (JE `0x422c27`) or byte
M+0x99 ≠ 0 (JNE `0x422c33`, EDX = M+0x92): next. EDX = `[A+0x1a4]`; if ≠
`[M+0x1a4]` (JE `0x422c41`): `[M+0x1a4]` ≠ 0 (JE `0x422c4a`) → word
`[[M+0x1a4]+0x1a2]` −= 1 (ECX); `[M+0x1a4]` ← EDX; EDX ≠ 0 (JE
`0x422c61`) → word `[[M+0x1a4]+0x1a2]` += 1; EDX = ECX = M+0x92 (through
EBP−4), byte M+0x9d ← 1. Then `0x422bf0`(M) (recursive, regs (M, EDX,
ECX)), ECX = M. Return EAX = 0, EDX = G or the member array. A wingman
that leads another group engages that group as well; a cycle of leaders
would recurse without end in the original (not built by the mission
loader, hypothesis).

### State method stack residue

Two callees below these methods read uninitialized stack at fixed depths
below their own entry ESP: `flyer_steer` (`0x4222fc`) at −0x229, −0x23d,
−0x20d, −0x221 (the bytes its flight-model chain copies into the pose
byte actor+0x25; [Flyer control stack residue chain](#flyer-control-stack-residue-chain))
and `target_emit` (`0x4226a0`) the dword at −0x14 (its control word,
[weapons.md](weapons.md)). Depth of each reading call below its caller's
entry ESP (prologues and pushes): `0x422598` → `0x4222fc` 0x38;
`0x4224c4` → `0x4222fc` 0x30; `0x4226a0` → `0x4222fc` 0x60; `0x422a50` →
`0x4224c4`/`0x422598` 0x10; `0x422ca8` → `0x4226a0`/`0x422598` 0x14;
`0x422a80`/`0x422b34`/`0x422bdc` → `0x422a50` 0x14/0x18/0x0c. The callees
before each reading call reach at most 0x98 below the entry of
`0x422598` (slot `+0x78` = `0x421e98`, depth 0x68, entered at 0x30) and
0x78 below `0x4224c4` (`0x422108` 0x50, `heading_point` 0x50,
`dist2d_point` 0x3c, `0x423b0c` and `0x42313c` 8); `0x422a50`,
`0x422ca8`, `0x422a80`, `0x422b34` and `0x422bdc` call nothing before the
reading call. So in an all-original call the bytes are those present at
the outer method's entry. Each shim copies them (4 bytes for `0x422598`;
8 for `0x422a50`, `0x422a80`, `0x422b34`, `0x422bdc`; 12 for `0x422ca8`;
offsets in `flyer_states_capture`) into a static record without writing
the stack (EAX parked in a static and restored; flags untouched); the
adapter writes them below ESP at the callee's offsets before that call
(`flyer_states_stack_call`, the flyer_control technique). The record is
used only if the dword above its ESP equals the call's first argument,
and it is consumed either way. At install, the shim's displacement bytes
are checked against the offset table. Inside `target_emit` the projectile
and bullet creation (`0x40a9c4` depth 0x280, `0x40b43c` 0x284) can
overwrite the flyer bytes before its own `flyer_steer` call when it
fires; then they are overwritten exactly as in the original. `0x422d00`
and `0x422bf0` have no residue reader below them (script_start, the
recursion) and no shim. As for flyer control: below a replaced caller
the bytes at a method's entry are whatever that caller's bridge left
(`order_update` already calls `0x411cec` on the host stack); the
replacement transfers them exactly and does not claim raw-residue parity
across different call trees.

### State method callees and their modes

Through the weapons guest bridge with the registers above: `point_offset`,
`dist_gate`, `heading_point`, `flyer_steer`, `target_emit`,
`timer_word_step`, `target_select`, `order_target`, `script_start`, the
FLYER AI helpers `0x4224c4`, `0x422108`, `0x422260`
([below](#the-flyer-ai-helpers-0x4224c4-0x422108-0x422098-0x422260)) and
this group's own hooks (each keeps its mode); original code behind the
bridge: the FLYER slot `+0x78` `0x421e98`. Two properties of existing
nested hooks pass through unchanged: a replaced nested hook returns its
entry ECX (for example `point_offset`'s, which `0x422598` forwards to the
slot `+0x78`, `dist_gate` and `heading_point` where the original forwards
the original's ECX; the chain ends at the `mov ecx, edi` / `xor ecx, ecx`
before `0x422108`; `0x421e98` passes only the high word of its entry ECX
into a word slot of `0x423044`, and what `dist_gate`/`heading_point` do
with an entry ECX is their own contract), and a replaced
`timer_word_step` returns only AX, no exact EDX (its EDX reaches
`target_select`'s entry and the dead return EDX of
`0x422a80`/`0x422b34`). Compare mode is refused (game calls). Callee
counters `hk_<hook>_call_<va>` (and `slot_78`) for every direct callee.

### State method branch sites

32 sites: `0x422cc2`, `0x422cca`, `0x422cd8`, `0x422ce8`; `0x422622`,
`0x422639`, `0x422642`, `0x422677`; `0x422a61`; `0x422aa3`, `0x422abc`,
`0x422ad5`, `0x422ae7`, `0x422af3`, `0x422af7`, `0x422b0e` (PROVED never
taken); `0x422b58`, `0x422b6d`, `0x422b8a`, `0x422b93`, `0x422baa`,
`0x422bca`; `0x422c1b`, `0x422c27`, `0x422c33`, `0x422c41`, `0x422c4a`,
`0x422c61`, `0x422c8f`, `0x422ca0`; `0x422d12`, `0x422d2a`. The jump
table's targets are reached by the order types of the cases (0..5, 6,
0x0b, negative); `0x422bdc` has no branch.

### State method validation

`tools/reference-game/invitro/flyer_states_cases.h`, group `flyer_states`
(two shards): per hook 1,001,024 direct cases (quick tier 100,000; the
first 16,384 instrumented) against logging stubs for the hook's direct
callees (ordered slots, entry EAX/EDX/ECX with stack addresses masked,
the point P by content, and the residue bytes each residue-reading stub
finds at its offsets), the original called from an own caller that fills
the stack with 31/a7 and plants distinct bytes at the shim's offsets;
outside the trap window the original's stubs must see exactly the
planted bytes (this checks the depth table), and the replacement gets
them through its residue record. Stubs return values on the branch edges
(distances 9999/10000/10001/14999/15000/15001, headings on the quarter
turns, AX zero or not, targets 0/T/other) and change what the originals
reread (A+0x10, L+0x10, the first member, A+0x45, A+0x1a4, flags and
counts of the members). Then 256 installed 31/a7 cases per hook through
the entry shims (fast and preserving dispatch, live x87 stack) and 3,072
real-callee cases: the group's own hooks and `point_offset`, `dist_gate`,
`heading_point`, `dist2d_point`, `timer_word_step`, `order_target`,
`script_start` not installed, original or replaced (rotating), the hook
itself original or replaced, with the real `0x4224c4`, `0x422108`,
`0x422260`; `target_emit`, `flyer_steer`, `target_select`, `0x423b0c`,
`0x42313c` and the slot `+0x78` stay stubs; pair, stub log (with the
residue `flyer_steer`/`target_emit` find; ECX at stub entries and, when
`timer_word_step` is replaced, EDX are not compared, see above), arena
and the stagger word `0x4a9b70` against an all-original run. 32 branch
sites, 31 both ways in the instrumented prefix, one PROVED. Mutation
witnesses: [MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md).

## The FLYER AI helpers `0x4224c4`, `0x422108`, `0x422098`, `0x422260`

The leader's route step and the pitch, altitude and throttle helpers below
the FLYER AI state methods and `target_emit`. Callers from an E8/E9, Jcc
rel32 and absolute-dword scan of the whole file. Roles are hypotheses; the
register and memory effects are what the replacement reproduces and the
in-vitro test compares. Hook group `flyer_ai` (adapter
`tools/reference-game/proxy/flyer_ai_hooks.h`), core in
`core/src/flyer_states.c` (`es2/core/flyer_states.h`).

| Hook | VA | Instr. | Role (hypothesis) | Prologue / relocated | Slots |
|---|---|---:|---|---|---|
| `flyer_route` | 4224c4 | 81 | leader route step: next waypoint, steering toward it, height 30000, throttle word `0xb4` | `55 8b ec 83 c4 f4` / 6 (1,2,3) | 1: A |
| `flyer_altitude` | 422108 | 29 | goal pitch toward a height (or from word A+0x23c), then `0x422098` | `55 8b ec 53 56` / 5 (1,2,1,1) | 2: A, height |
| `flyer_pitch` | 422098 | 43 | pitch command from a goal pitch, the roll and the pitch rate | `55 8b ec 51 53` / 5 (1,2,1,1) | 2: A, goal |
| `flyer_throttle` | 422260 | 52 | wingman throttle word A+0x21c from the along-track distance and the speed difference | `55 8b ec 83 c4 f4` / 6 (1,2,3) | 5: A, L, d, h, spacing |

Notation as for the state methods: A the flyer, G = `[A+0x45]`, R = G+4
(the group's route record: word index at R+0, list pointer at R+2; the
list has a word count at +0 and the array of point pointers at +2), L the
leader passed by `flyer_follow`; pitch, roll and heading are the angle
words A+0xc, A+0xe, A+0x10, the pitch rate is word A+0x25b (S+0x18 of the
flight state S = A+0x243).

### Helper callers and reach

Byte scan: `0x4224c4` ← `0x422a64` (`flyer_formation`, leader path) only;
`0x422108` ← `0x42254b` (`0x4224c4`), `0x42265d` (`flyer_follow`),
`0x4227ce`, `0x422803`, `0x42286d` (`target_emit`); `0x422098` ←
`0x42214d` (`0x422108`), `0x422822` (`target_emit`); `0x422260` ←
`0x422691` (`flyer_follow`) only. No absolute reference (no vtable slot),
no Jcc into them. Map entry probes (a60/c1s/c1c/c3_09/c4_08/c3_10):
`0x4224c4` 638/638/638/0/0/0, `0x422108` and `0x422098`
1000/1000/1000/0/0/0, `0x422260` no probe row. The bridge counters of the
regression with all hooks replaced (last tick record) give the
live calls: `flyer_formation` `call_4224c4` 1805/638/638/0/0/0,
`target_emit` `call_422108` 4949/10791/7161/0/0/0 and `call_422098`
482/965/631/0/0/0, `flyer_follow` 0 in every suite. So `0x422260` is never
entered live (in vitro only), and every `0x422108` call comes from
`target_emit` or `0x4224c4`.

### Helper contracts

**`0x4224c4` route(A).** Entry EAX and EDX are dead (written at
`0x4224d4`/`0x4224dd` before any read); the entry ECX's high word is live.

1. DI ← 1 (only DI is tested; EDI is callee-saved). R = `[A+0x45]` + 4.
   `route_point`(R, s) with s = lo(entry ECX, word R) + 1 (a 32-bit `inc`:
   index 0xffff carries into the high word), regs (R, R, s); P = its EAX.
2. P = 0 (JNE `0x4224f5` not taken): DI ← 0; R reread from `[A+0x45]`;
   `route_point`(R, s') with s' = lo(`route_point`'s ECX, word R reread),
   regs (R, R, s'); P = its EAX (the current waypoint; a zero P faults in
   the later `heading_point`, as in the original).
3. DI ≠ 0 (JE `0x42251c`, taken exactly when step 2 ran): d =
   `dist2d_point`(P, A+0x26), regs (A+0x26, `route_point`'s EDX/ECX). If
   s32 d < 15000 (JGE `0x422530`): `route_advance`(R) with R reread, regs
   (d, R, `dist2d_point`'s ECX); ECX ← R (POP).
4. pitch = `0x422108`(A, 30000) with the current regs (the second
   `route_point`'s, `dist2d_point`'s, or `route_advance`'s with ECX = R).
5. h = `heading_point`(P, A+0x26), regs (A+0x26, P, `0x422108`'s ECX);
   AX −= word A+0x10 (read after the call); ECX = s16(AX); yaw = q16(ECX,
   0x6d60) (computed: EAX/EDX as `0x47e08d` leaves them, ECX kept).
6. `flyer_steer`(A, yaw, pitch, 0), regs (yaw, q16's EDX, ECX); then word
   A+0x21c ← 0xb4. Return `flyer_steer`'s EDX:EAX; ECX = its ECX (dead:
   the only caller `0x422a50` pops ECX after the call).

Memory: reads `[A+0x45]` once or twice (again for step 2 or for
`route_advance`, never both), word R once or twice and word A+0x10;
writes word A+0x21c (after `flyer_steer`). The frame locals
EBP−4/−8/−0xc are written before they are read. Branch sites `0x4224f5`,
`0x42251c`, `0x422530`.

**`0x422108` altitude(A, height).** Entry EAX dead; entry EDX/ECX reach
the first callee. EAX ← A+0x92; if byte A+0xae (EAX+0x1c) ≠ 0 (JE
`0x42211a` not taken): `multiply_q8`(s16(word A+0x23c), 0xfa0), regs
(A+0x92, entry EDX, entry ECX), then AX += word A+0xc (read after the
call). Else EAX = height − dword A+0x2e (z of the position A+0x26) and
`vector_angle`(10000, EAX), regs (EAX, entry EDX, entry ECX). Then
`0x422098`(A, EAX) with the callee's EDX/ECX; return its EDX:EAX and ECX.
No memory write. The height slot is read only on the second path
(`target_emit` passes 0 at `0x422800`, on the first).

**`0x422098` pitch(A, goal).** `push ecx` reserves EBP−4 with the entry
ECX; both of its words are overwritten before the final `pop ecx`, so the
entry ECX reaches only `heading_cos`'s entry ECX (`0x47e08d` keeps ECX).
Entry EAX/EDX are dead (MOVSX at `0x4220a7`/`0x4220ab`).

1. e = q16(s16(goal) − s16(word A+0xc), 0x5dc) (only the goal's low word
   is read).
2. roll = word A+0xe, read once (`0x4220bc`, kept at EBP−2). `heading_cos`
   with the slot lo(q16's EDX, roll), regs (lo(e, roll), lo(q16's EDX,
   roll), entry ECX); ECX ← the slot (POP); c = its AX (EBP−4).
3. v = q14(s16(c), e) (ECX = s16(word e)); EAX = s16(AX of v).
4. EAX ← EAX − q16(s16(word A+0x25b), 0x1f40) (32-bit; the word A+0x25b
   is read after `heading_cos`); EDX = the second q16's EDX.

Return EAX and EDX as in 4, ECX = (roll << 16) | c (the EBP−4 dword). No
memory write, no branch.

**`0x422260` throttle(A, L, d, h, spacing).** Entry EAX dead (MOVSX at
`0x42227f`); the entry ECX reaches only `heading_cos`'s entry ECX (POP
afterwards); the entry EDX's high word rides only in the `heading_cos`
slot and, after the POP, in ECX's high word, which `0x47e0c0` replaces (it
reads the low word of its slot). The slots d, h and spacing are read as
words; EDI's high word never leaves the function.

1. DI = word h + word A+0x10 − word L+0x10 (16-bit, EBP−2); EBP−8 =
   s16(d).
2. `heading_cos`(lo(entry EDX, DI)), regs (s16(d), that slot, entry ECX);
   c = its AX (EBP−0xa).
3. v = q14(s16(d), c) (computed; ECX = s16(c)); EAX = s16(AX of v); AX −=
   word spacing; ECX = s16(AX).
4. DX = word L+0x233 − word A+0x233 (16-bit; EDX's high word is q14's),
   AX = word A+0x21c, EDX <<= 3 (32-bit), ECX = sar(ECX, 1), CX += DX,
   EDX = ECX, AX += DX.
5. s16(AX) ≥ 0x100 (JL `0x4222d7` not taken): DX = 0x100; else s16(AX) ≤
   0x8c (JG `0x4222e3` not taken): DX = 0x8c; else EDX = EAX. Word A+0x21c
   ← DX.

Return EAX (its high word is the sign extension of q14's AX), EDX as in 5
and ECX as in 4. The only write is word A+0x21c. `flyer_follow` pushes
lo(`flyer_steer`'s EDX, word d) as the d slot and `0x422260` reads only
its low word ([Flyer control result registers](#flyer-control-result-registers-and-ecx)).

### Helper result registers and ECX at the callers

EDX:EAX is reproduced exactly. ECX after each of the four is dead at
every caller, so the framework's return of the entry ECX (no exit shim)
changes nothing observable:

- `0x4224c4`: `0x422a50` pops ECX at `0x422a69`.
- `0x422108` and `0x422098` in `target_emit`: every path after the calls
  overwrites ECX (`0x422842`, `0x4228a4` or `0x422a25`) before a read, and
  EDX is either overwritten (`0x42283a`) or reaches `flyer_steer`'s entry
  EDX, which is dead there (its first q16 overwrites it); the word A+0x1fe
  stays 0 or 1 on these paths, so `0x422a10` always reaches `flyer_steer`.
- `0x422108` in `0x4224c4`: ECX reaches `heading_point` → `vector_angle`
  → `0x492ac8` → `angle_ratio`, which overwrites it at `0x47d33d` before
  any read; back in `0x4224c4` it is replaced at `0x422570`.
- `0x422108` in `flyer_follow`: ECX (0 there) becomes `flyer_steer`'s entry
  ECX with T = L ≠ 0; its high word reaches only `heading_cos`'s slot
  (which reads the low word) and is then replaced by `0x47e0c0`.
- `0x422098` in `0x422108`: passed on unchanged (see above).
- `0x422260` in `flyer_follow`: returned with the pair; `0x422a50` and
  `0x422ca8` pop ECX after `flyer_follow`.

### Route step stack residue and its entry shim

Only `flyer_steer` below these functions reads uninitialized stack (the
four bytes at its entry ESP−0x229, −0x23d, −0x20d, −0x221 that its flight
model copies into the pose byte A+0x25;
[Flyer control stack residue chain](#flyer-control-stack-residue-chain)).
`0x4224c4` calls it at depth 0x30 below its own entry (saved EBP, frame
0xc, three pushes, the T and pitch slots, the two arguments, the return
address), after callees that reach at most 0x84 below that entry
(`heading_point`, entered at 0x34, and `0x422108`, entered at 0x2c, each
reach 0x50 below their entry through `vector_angle` → `0x492ac8` →
`angle_ratio`; `dist2d_point`, entered at 0x28, 0x3c;
`route_point`/`route_advance` 8), far above the shallowest residue offset
0x23d. So in an all-original call the bytes `flyer_steer` reads are those
at `0x4224c4`'s entry ESP−0x259, −0x26d, −0x23d, −0x251 (the
`flyer_states` formation shim plants exactly these). `flyer_route` has an
entry shim (`flyer_ai_route_entry`, the `flyer_control` technique): it
loads them into AL, AH, DL, DH (entry EAX and EDX are dead) and jumps
into the framework's dispatch; the adapter writes them below ESP at
`flyer_steer`'s offsets before that call (`flyer_states_stack_call`). `0x422108`, `0x422098` and `0x422260` read
no uninitialized local and none of their callees does (`multiply_q8`,
`vector_angle`, `heading_cos`, `0x47e08d`, `0x47e0c0` read only their
slots): no shim. As for flyer control and the state methods, below a
replaced caller the bytes at `0x4224c4`'s entry are whatever that
caller's bridge left; the replacement transfers them exactly and does not
claim raw-residue parity across different call trees.

### Helper callees and their modes

Through the weapons guest bridge with the registers above (nested hooks
keep their modes): `route_point`, `dist2d_point`, `route_advance`,
`heading_point`, `flyer_steer`, `multiply_q8`, `vector_angle`,
`heading_cos` and this group's own `0x422108`/`0x422098`. The pure
`0x47e08d` (q16) and `0x47e0c0` (q14) are computed in core with their
exact register effects, as the flyer control core does
(`fm_q16`/`fm_q14`); they have no counter. The bridging hooks
`flyer_formation` (`call_4224c4`), `flyer_follow` (`call_422108`,
`call_422260`) and `target_emit` (`call_422108`, `call_422098`) call these
addresses through their guest bridges, so they enter them in their own
modes (`flyer_formation` plants the route residue for `0x4224c4`).
Compare: `flyer_route` refuses it (game calls, the flight model below
`flyer_steer`, the residue shim). `flyer_altitude` and `flyer_pitch`
write nothing and call only pure callees, and `flyer_throttle` writes
only the word A+0x21c (span on A at 0x21c, 0x19 bytes, so that the clone
also holds the word A+0x233 it reads): these three accept compare. Their
pairs do not depend on the modes of the nested hooks (only AX of
`multiply_q8`, `vector_angle` and `heading_cos` is used).

### Helper branch sites

6 sites: `0x4224f5`, `0x42251c`, `0x422530` (`flyer_route`); `0x42211a`
(`flyer_altitude`); `0x4222d7`, `0x4222e3` (`flyer_throttle`);
`0x422098` has none. All are reachable both ways from the arguments,
memory and callee results (no proof needed).

### Helper validation and live counts

`tools/reference-game/invitro/flyer_ai_cases.h`, group `flyer_ai` (one
shard): per hook 1,001,024 direct cases (quick tier 100,000; the first
16,384 instrumented) of the original, called from an own caller over a
patterned stack, against the core, with logging stubs for the hook's
direct callees (all argument slots and the entry EAX/EDX/ECX in call
order, the four residue bytes the `flyer_steer` stub finds, the pair, the
arena, the callee counters). For `flyer_route` the caller plants distinct
bytes at the shim's offsets and mirrors them in the entry AL/AH/DL/DH;
outside the trap window the original's `flyer_steer` stub must see exactly
them (this confirms the depth 0x30). The stubs return values on the edges
(route points 0 or not, distances 14999/15000/15001, quarter-turn
cosines, route indices 0xffff and 0x7fff, clamp sums 0x8c/0x100 and their
neighbours) and change what the originals read again (`[A+0x45]`, the
route index, A+0xc, A+0x10, A+0x21c, A+0x233, L+0x233, A+0x25b). Then 256
installed 31/a7 cases per hook (fast and preserving dispatch, live x87
stack; `flyer_route` through its entry shim); a real-callee pass under the
bridging hook `flyer_formation` (3,072 cases, 2,048 for `flyer_route`):
the leader path through `0x4224c4` over a valid route, the wingman path
through `flyer_follow` (which reaches `0x422108` and, within 15000,
`0x422260`), the group's other hooks and `flyer_formation`,
`flyer_follow`, `route_point`, `dist2d_point`, `route_advance`,
`heading_point`, `vector_angle`, `multiply_q8`, `heading_cos`,
`point_offset`, `dist_gate`, `angle_ratio` not installed, original,
replaced or compare (rotating), the hook itself original, replaced or (for
the three that accept it) compare; `flyer_steer` and the virtual `+0x78`
stay stubs. Pair, arena and stub log (ECX at the stubs masked) against the
run without the hook; `flyer_steer`'s residue must be the bytes planted at
`flyer_formation`'s shim offsets whenever its caller runs replaced or no
hook of the tree does (512 cases carried by the replaced `flyer_route`).
Finally 512 installed compare cases each for `flyer_altitude`,
`flyer_pitch`, `flyer_throttle` (`compare_probes.h`, real callees; the
skewed replacement is caught every time, for `flyer_throttle` half of
them by the span byte). 6 branch sites, all both ways; no PROVED edge.
Mutation witnesses: [MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md).

**Live** (regression with all hooks and the
41 CW3220 imports replaced, heap shadow, hidden window): all
sixteen suites identical, every skipped/bypassed/mismatch counter 0.
`flyer_route` 1,805/638/638/0/0/0 (= `hk_flyer_formation_call_4224c4` in
every suite), `flyer_altitude` 7,858/11,431/5,920/0/0/0, `flyer_pitch`
8,458/12,396/6,406/0/0/0 (= `hk_flyer_steer_calls` in every suite),
`flyer_throttle` 0 everywhere (in vitro only); c5_05-moon-missiles
8,475/8,752/8,797. The entry shim caused no fault.

## The rotation helpers `0x47f058` and `0x47f4f4`

Hooks `rotation_compose` (`0x47f058`) and `rotation_place` (`0x47f4f4`),
adapter group `rotation_helpers` (`proxy/rotation_helpers_hooks.h`), cores
`es2_matrix_rotation_compose_registers` and
`es2_matrix_rotation_place_registers` in `core/src/matrix.c`. Listing:
llvm-objdump `-d -M intel` with `--start-address` at each entry (the
linear listing loses the instruction boundary in the padding before
`0x47f058`).

### Rotation helper classification

Two pure leaves: no call, no global, no x87 instruction (363 and 369
instructions). `0x47f4f4` is `0x47f058` with six instructions in front
(`0x47f504..0x47f513`) and every jump target moved by 0x4ae; the rest of
the body is the same instruction for instruction. Both have an `enter 0,0`
prologue followed by `push esi; push edi; push ebx` and the three cdecl
slots in EBX (a), ESI (b), EDI (out); no local. The matrices have the
32-byte layout of `matrix_compose` (nine Q14 words, the signed mode byte at
+18, byte 19 reserved, three translation dwords at +20).

### Rotation helper contract

`rotation_place(a, b, out)` first copies b+0x14, b+0x18, b+0x1c to
out+0x14, out+0x18, out+0x1c (each dword read, then written), then
continues as `rotation_compose`, so its mode bytes are read after those
stores.

`rotation_compose(a, b, out)`, signed mode bytes ma = a+18, mb = b+18
(read before any store):

| Case | Branches | Writes | EDX | ECX |
|---|---|---|---|---|
| ma < 1 | JL `0x47f06c` | 20 bytes of b (REP MOVSD, ascending, byte 19 included) | entry EDX | 0 |
| ma ≥ 1, mb < 1 | JL `0x47f076` | 20 bytes of a | entry EDX | 0 |
| ma = 1, mb = 1 | `0x47f07c` falls, JE `0x47f082` | words 0, 2, 4, 6 (two terms), 0 at 8..14, 0x4000 at 16, byte 18 = 1 | a6·b6 | sx(b6) |
| ma > 1, mb = 1 | `0x47f082` falls (JMP `0x47f402`) | words 0, 2, 4, 6, 12, 14 (two terms), then a's words 8, 10, 16, byte 18 = 2 | a14·b6 | sx(b6) |
| ma = 1, mb > 1 | JG `0x47f07c`, JE `0x47f091` | words 0, 2, 8, 4, 6, 10 (two terms), then b's words 12, 14, 16, byte 18 = 2 | a6·b10 | sx(b10) |
| ma > 1, mb > 1 | `0x47f091` falls | all nine words (three terms), byte 18 = 2 | a16·b16 | sx(b16) |

Each word is out(c, r) = (Σk a(c, k)·b(k, r) + 0x2000) SAR 14, the
products of sign-extended words summed in 32 bits with wrap (three
0x8000·0x8000 products give 0xC0000000), with a(c, k) at the offsets
{{0,2,8},{4,6,10},{12,14,16}}. The words are stored in the order of the
table, each dot reading its inputs after the previous store (out may
overlap a or b). EDX and ECX are the registers of the last product (`IMUL
EDX,ECX` leaves the low product dword, `MOVSX ECX` b's word; the a·b of the
table are the words read in that last dot). EAX = out on every path. Byte
19 of out is written only by the copies. The copy path also loads ES from
DS (`mov cx,ds; mov es,ecx`), a no-op in the flat model; REP MOVSD relies
on DF = 0 (the C runtime convention). Flags at return are dead at the three
call sites (each writes them with ADD ESP first).

### Rotation helper callers and the ECX question

Three call sites, no dword reference: `0x40863e` (`debris_piece`: a = the
Euler matrix f−0x48, b = the piece matrix, out = f−0x5c), `0x46754e` (the
flight model `0x466b58`: a = the increment matrix D, b = the pose rotation,
out = N) and `0x40e873` (`weapon_prepare`, `0x47f4f4`: a = the local Euler
matrix, b = the mount record or its local copy, out = the composed record).
No caller passes overlapping matrices. ECX after `0x47f058` reaches
`matrix_angles` `0x47f9a0` at `0x40864e`, which does not write ECX before
its own call to `0x47da4c`; after `0x47f4f4` it reaches `euler_matrix` or
`matrix_compose` (`0x40e8aa`/`0x40e8c4`). Instead of following every
callee, both hooks have an exit shim (the `transform_helpers` pattern,
three slots) that loads the ECX the core computed when the replacement
ran. At `0x46754e` ECX is written at `0x46756b` before any read. The
bridging hooks: `flight_model`'s core computes `0x47f058` itself
(`es2_compose_rotation` in `flyer.c`), `debris_piece`'s and
`weapon_prepare`'s cores call `0x47f058`/`0x47f4f4` through their guest
bridges, i.e. into these hooks in their own modes.

### Rotation helper stack

The original leaves its return address, EBP, ESI, EDI and EBX in the five
dwords below the caller's ESP; the exit shim leaves the return address,
EBP and the three copied slots there and the dispatcher's frames below.
The real-caller passes (below) run `debris_piece` and `weapon_prepare` from
31/a7 stacks with the hook original, replaced and compare and see no later
read of those slots (stub log and the real `matrix_angles` under
`debris_piece`; the real Euler, `matrix_compose`, `matrix_point`,
`matrix_angles` and `multiply_q10` under `weapon_prepare`).

### Rotation helper hooks

| Hook | Prologue (signature) | Relocate / lengths | Slots | Result | Compare |
|---|---|---|---:|---|---|
| `rotation_compose` | `c8 00 00 00 56 57 53 8b 5d 08 8b 75 0c 8b 7d 10 80 7b 12 01` | 5 / 4,1 | 3 | EDX:EAX, ECX by exit shim | yes |
| `rotation_place` | `c8 00 00 00 56 57 53 8b 5d 08 8b 75 0c 8b 7d 10 8b 46 14` | 5 / 4,1 | 3 | EDX:EAX, ECX by exit shim | yes |

Both accept compare mode: spans a (20
bytes), b (20; 32 for `rotation_place`) and out (20; 32), and a
comparator that maps EAX = out to the clone and compares EDX and every
span byte. Overlapping arguments make the framework skip the call (no
caller passes them); in compare mode ECX comes from the replacement
(computed from the cloned pre-state) and is not compared. No game call,
no callee counter.

### Rotation helper validation and live counts

`invitro/rotation_helpers_cases.h`: 1,001,024 direct cases per hook
(quick tier 100,000) against the original body with chosen entry
EAX/EDX/ECX: edge and random words, every mode class for both operands
(the first 81 cases all pairs of 0, 1, 2, 3, 7Fh, 80h, FFh, FEh, 81h),
random byte 19 and translations, and out, a and b overlapping each other
at any offset (out over a's or b's mode byte, `rotation_place`'s
translation copy over either mode byte, a = b); EAX, EDX, the exit ECX and
the arena compared, each of the six mode paths reached. 256 installed
31/a7 cases per hook through the exit shim (fast and preserving dispatch,
live x87 stack). Real-caller passes: `rotation_compose` 3,072 cases under
the original `debris_piece` body and under the replaced `debris_piece`
(the `debris_burst` stubs for every callee except `0x47f058` and the real
`matrix_angles`, the stub Euler filling a with random bytes; pair, ordered
stub calls with entry registers, arena, globals and FS:0);
`rotation_place` 6,144 cases under the original `weapon_prepare` body and
under the replaced `weapon_prepare` (synthetic lookup V20 and mount offset
`0x40f540`, every other callee real; the record from the table, the static
record `0x6c56a8`, the player's local copy with `[0x49ef34]` set, both
`W+0x5b`/`W+0x5f` nonzero skipping the call; all six mode paths; pair,
ECX with the original body, `0x4a9834..0x4a9865` and the arena). 512
installed compare cases per hook (`compare_probes.h`; the skewed
replacement caught every time, half of them by a span byte). 10 branch
sites, all both ways. Mutation witnesses in
[MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md).

**Live** (no map probe row, `enter` entries; regression with all hooks and
the 41 CW3220 imports replaced, heap shadow, hidden window): all sixteen suites identical, every
skipped/bypassed/mismatch counter 0. `rotation_compose` 7/14/2/44/7/16
(a60/c1s/c1c/c3_09/c4_08/c3_10; = `hk_debris_piece_call_47f058` in every
suite, the replaced flight model computes its `0x47f058` in core),
`rotation_place` 0/262/92/1,395/82/56 (= `hk_weapon_prepare_call_47f4f4`
in every suite).

## Open questions

- Which Razor parts 4, 5, 7, 8 of the part-flag array the flight model's
  failure flags F test.
- What byte `[0x477904()+4]` of the device state means (the lever-present
  reading is a hypothesis; [input.md](input.md)).
- Whether trigger 5 of c4_08 (activation of the one flyer) ever fires; the
  zero counts suggest it does not (hypothesis, trigger not decoded).
- Which messages MainWndProc receives after arming in each suite (a probe
  at the case targets would answer it; relevant only for the window
  shell).
- Why `0x481fe0` (and with it `0x465e28`) runs only in c3_09 (52 calls):
  which palette change that suite triggers was not followed.
