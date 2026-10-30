# Camera, view subject and render camera

The camera object, its per-frame update `0x4011a0`, the terrain face lookup
`0x46e498`, the outside camera and the view subject (the `camera_view`
group), and the render camera's setup, view transform and projection.
Derived from the r1.11 bytes of `DBSIM.EXE` (image base `0x400000`, SHA-256
`8561aa47…c4ad`, [reference_version.md](../reference_version.md)), from the
entry-aligned `llvm-objdump -d -M intel` listing, byte scans for `E8`/`E9`
transfers and absolute dwords, and the frame descriptor bytes at `0x49f1fd`.
Field names are interpretations; behaviour is confirmed in vitro (Evidence).

B = the view block `0x4d24bc`; cam = `[B+2Eh]` = the global pointer
`[0x4d24ea]`; T = the camera's target object `[C+32]`.

## Own code

| Function | VA | Core | Adapter | Compare |
| --- | --- | --- | --- | --- |
| `terrain_face` | `0x46e498` | [terrain.c](../../core/src/terrain.c) `es2_terrain_face_registers` | [weapons_hooks.h](../../tools/reference-game/proxy/weapons_hooks.h) | yes |
| `camera_update` | `0x4011a0` | [camera.c](../../core/src/camera.c) `es2_camera_update_registers` | [weapons_hooks.h](../../tools/reference-game/proxy/weapons_hooks.h) | no |
| the ten `camera_view` hooks | table below | [camera_view.c](../../core/src/camera_view.c) | [camera_view_hooks.h](../../tools/reference-game/proxy/camera_view_hooks.h) | no |
| view set-up, view transform, projection, lights | `0x48c2e4` … | [view.c](../../sim/src/view.c) | – | – |

Every hook is registered in the hook registry ([hooks.md](../reference-game/hooks.md#registry)).
`es2sim` gets them through the adapters compiled from the registry
(`sim/src/adapters.c`), so the camera and the view subject, which are
simulation input (see below), run in the own process's logic frame without
further code. The render camera's consumer side is own code in `view.c`
(rasterizer.md, "View transform, projection and lights").

## Camera object, target and globals

**Camera object C** (vtable `0x49f8e8`, `0x4e` bytes; the graphics library's
camera, vtable `0x4a5b29`, constructor `0x48bda4`, extended by the game's
fields):

| Offset | Field |
| --- | --- |
| `+04/+08/+0c` | position X/Y/Z dwords |
| `+10/+12/+14` | camera angles: pitch, roll, yaw words |
| `+16` | graphics context pointer (render camera) |
| `+1a` | focal shift: 9 when dword `0x4d3040` > 1200, else 8 |
| `+1e` | near plane (`0x100`) |
| `+22` | orbit distance word; as a dword the render divisor, used only when the focal shift is 0 (never with these constructors) |
| `+24` | speed or distance step word |
| `+26/+28/+2a` | orbit angles |
| `+2c/+2e/+30` | angle deltas (pitch, roll, yaw) |
| `+32` | target object pointer |
| `+36` | mode word: 0 free flight, 1 orbit, 2 attached, 3 replay |
| `+38` | orbit offset (three words) |
| `+3e` | attachment offset (three words) |
| `+44` | attachment angle offsets (three words) |
| `+4a` | lock dword: when nonzero the setters and the outside camera change nothing |

**Target object** T: vtable `+00`; angles `+0c` (yaw word `+10`); matrix `+12`
(32 bytes: nine Q14 words, mode byte, padding, translation `+26/+2a/+2e`);
"matrix valid" word `+32`. Whenever the camera needs T's matrix and the word
is 0, it builds it with `euler_matrix(T+0c, T+12)` and sets the word to 1.

**View block B** (`0x4d24bc`): player `+2a` (`0x4d24e6`), camera `+2e`
(`0x4d24ea`), view-mode word `+32` (`0x4d24ee`), outside word `+34`
(`0x4d24f0`), outside default `+5c` (`0x4d2518`), requested view `+5e`
(`0x4d251a`), subject `+60`, countdown `+64` (`0x4d2520`), flag `+78`. The
view subject is `0x4d2684`; the group cameras are `0x4d2688[]`; the static
camera is `0x4d2754`.

**Camera_update globals.** Ring index `[0x4a8048]` (0..49); position ring
`0x4a7bfc` (50 × 12 bytes); angle ring `0x4a7e54` (50 × 6 bytes); step ring
`0x4a7f80` (50 dwords); followed object `[0x4d24e6]`; terrain grid
`[0x4a0be0]`. The rings are constructed by the static initializer `0x401d30`
(referenced from the init table at `0x4a79da`; empty).

## The camera as simulation input

Five simulation hooks read the camera position `[0x4d24ea]+4..+0xc`:
`projectile_flight` (`0x40a7b3`: camera distance < 40,000 and the
projectile's latch word `+6` = 0 → `sfx_play(0x32)`, which draws stream B
before its gate, [audio.md](audio.md#what-can-change-the-oracles-comparison),
and sets the latch), `ground_effects` (`0x419726`: a distance < 15,000, or the
actor's byte `+0xa3`, opens a block of effects that was not followed
further), `flyer_move` (`0x421b4f`), `meteor_update` (`0x409ed9`, `0x409f1d`:
the camera position passed as the position of two positional sounds
`0x4628e0`; their stream-B draws do not depend on it) and `fire_update`
(`0x46b7d2`). The view subject `[0x4d2684]` is read by `weapon_prepare`,
`cockpit_sync`, `flyer_hit_test`, `cockpit_valid`, `cockpit_arc` and
`command_key`; the view word `0x4d24f0` by `player_input`, `leg_input`,
`control_record` and `command_key` (listing scan for the three addresses).
Every write to these comes from `camera_update` and the functions of this
document, which run in the logic frame. They must be reproduced exactly;
acceptance is the tick log, not the frame hash alone. Whether the camera
reads in `flyer_move` and `fire_update` gate compared values is not
followed (only `projectile_flight`, `ground_effects` and `meteor_update` were
followed to their gates); it does not matter, the code is exact either way.

The drawing split of `0x401c74`, `0x45de30`, `0x45df34` and `0x45edb0` in
[function_verdicts.md](../reference-game/function_verdicts.md) must not be read as
"drawing only": their writes reach compared values through the reads above.

## terrain_face `0x46e498`

cdecl (map M, point P) → EDX:EAX. Callers: the camera (`0x401368`),
`slope_speed` `0x41693c` (`0x416959`), `ground_collide` (`0x4193eb`) and
`0x46e62c` (`0x46e7b5`), always with the terrain grid pointer `[0x4a0be0]` (or
the grid passed through) as the first argument.

Grid fields as for `terrain_height` ([terrain.h](../../core/include/es2/core/terrain.h)):
cell array `[M+ec]` (16-byte cells, diagonal flags in the low two bits of byte
0x0f, two normal vectors of three signed words at +1 and +7), log2 columns
`[M+100]`, log2 rows `[M+104]`, log2 cell size `[M+108]` (shift counts use
their low five bits). Point: X at P+0, Y at P+4.

1. Column `x = X sar s`, row `y = Y sar s`. If `x < 0`, `x >= 1 << columns`,
   `y < 0` or `y >= 1 << rows` (signed): EAX = 0, EDX = x.
2. Offsets in the cell `fx = X - (x << s)`, `fy = Y - (y << s)`; cell index
   `i = (y << columns) + x`. If `i < 0` or `i >= 1 << ((columns + rows) & 31)`
   the original takes cell pointer 0 and reads the flags at address `0x0f`,
   which faults; for every grid whose exponent sum stays below 32 the index
   is always valid, so these two branch outcomes (`0x46e543` taken,
   `0x46e55a` not taken) are excluded by the caller contract and proved
   one-way in vitro. The core function reproduces the fault path literally.
3. flags = cell byte 0x0f & 3. Flags 2: EDX = fx, EAX = cell + 7 if
   `fx > fy`, else cell + 1. All other flags: EDX = `size - fx`
   (size = 1 << s), EAX = cell + 7 if `size - fx < fy`, else cell + 1.
   All comparisons signed.

Read-only; compare mode is allowed.

## camera_update `0x4011a0`

cdecl (camera C) → EDX:EAX (dead at every caller, reproduced anyway). The
function reads no entry register (EAX, EDX and ECX are overwritten before
use). Frame: 1C0h bytes; callees receive pointers into it, so the core keeps
its buffers at the original EBP offsets in 1C0h bytes of scratch.

**Callers.** The third call of the main loop (`0x45f491`: `mov edx,[ebx+2e]`,
`push edx`, call at `0x45f495`, EBX = `0x4d24bc`, so the argument is the
global pointer at `0x4d24ea`), and the mission start and replay paths
`0x45cfe3`, `0x45d1d2`, `0x45d250`, `0x45d28f`, `0x45d3d0`, `0x45d430`,
`0x45d949` and `0x461d0c` with the same pointer: nine direct call sites in the
listing. Every caller pops the argument into ECX and then calls another
function or overwrites EDX; EDX:EAX are dead at all of them. `0x45d246` and
`0x45d285` set the mode word to 1 before the call.

**Every call, all modes.** Copy the followed object's position (`+26`, 12
bytes, written `+8`, `+0`, `+4`) to the position ring and its angles (`+0c`, 6
bytes) to the angle ring at the current index; store the sign-extended AX
of `movement_step(400h)` (`0x467924`) in the step ring; then
`index = (index + 1) % 50` (IDIV, so the sign follows the dividend). The
mode word C+36 selects:

- **0, free flight.** Angles += deltas (16-bit). If the new pitch leaves
  [-3000h, 3000h] it is clamped and the pitch delta C+2c is cleared. Write
  the angles to C+10..15. Build `euler_matrix(C+10)` with translation =
  the current position and `transform_point((0, speed C+24, 0))`. Then
  `terrain_face` and `terrain_height` at the old position; slope =
  `dist_octagonal(normal x, normal y)` of the face (0 without face);
  floor = height + `multiply_q10(500, slope)` + 226h. The new Z is
  max(transformed Z, floor) (signed). Result: EAX = C+4, EDX = new Y.
- **1, orbit.** Distance C+22 += C+24, clamped to [2500, 30000] (signed
  16-bit). Orbit angles C+26..2b += deltas; an orbit pitch outside
  [-3000h, 3000h] is clamped (and written back) and C+2c cleared. Camera
  angles = orbit angles with the target yaw [T+10] added to the yaw.
  Position = `0x4801d4(C+38, T matrix)` (offset transformed by the target
  matrix) + `rotate_point((0, -distance, 0))` in the camera frame. If the
  terrain height + 226h at that point is above the camera Z, Z is raised to
  it; then, if the pitch delta C+2c is positive, it is undone (C+26 -=
  C+2c, C+2c = 0), the camera pitch is reset to its value at entry, and the
  position is computed once more (same raise rule). Results: EAX = C+4, EDX
  = Y on the full paths; EAX = floor, EDX = terrain_height's EDX when the
  camera stays above the terrain.
- **2, attached.** Calls the target's virtual +24 (cdecl, argument T;
  entry EAX = vtable, ECX = T, EDX = the new ring index). With a non-null
  result R: M = `matrix_compose(R, T matrix)`, position =
  `0x4801d4(C+3e, M)`, angles = `matrix_angles(M)` + C+44..49. With null:
  position = `0x4801d4(C+3e, T matrix)`, angles = T angles. Results: EAX =
  C+4 (or the angle dword with its low word replaced by the third angle),
  EDX = Y.
- **3, replay.** Walk backwards through the step ring from the newest
  entry, subtracting the steps from 8000 (1F40h) until the remainder is at
  most the current step (signed); f = (remainder << 16) / step (`0x47e068`).
  Position and angles are interpolated from the next newer entry k = i + 1
  (wrapping 50 → 0) towards the stopping entry i:
  `p = ring[k] + ((ring[i] - ring[k]) * f >> 16)` (`0x47e08d`), positions
  as 32-bit and angles as 16-bit values. The camera thus trails the
  followed object by 8000 units of accumulated frame steps. Result: EAX = angle
  dword with the low word replaced by the third angle, EDX = Y.
- **Other values:** only the ring update; EAX = mode − 3, EDX = new index.

Entry registers of every callee are reproduced (they are recorded and
compared in vitro). The two Q16 helpers are computed in core; all other
game code, including the hooked callees, runs through the guest bridge, so
the hook is compare-refused.

**Caller contract.** The ring index is 0..49 (the function writes to
`0x4a7bfc + 12 × index`). Mode 3's backward walk ends only when some step
reaches the remainder; with the steps `movement_step` returns (positive
frame steps) it ends within one lap.

**Proved one-way edges.** `0x4012e6` and `0x4014e6` (the lower clamp test
`jg`) run only for a pitch below -3000h, so they are never taken.
`0x4016dc` (second "matrix valid" test in mode 1) is taken except when T
overlaps the camera so that T+32 is C+2c, which the camera has just
cleared; the in-vitro cases include that overlap and reach both outcomes.

Live, mode 3 is not reached: it is requested only by button action 15
(`0x460fe5`), which no suite presses
([live_coverage.md](../reference-game/live_coverage.md#camera_update)).

## Outside camera `0x401c74(C, a, b, c)` (logic frame)

Callers: `player_input` (`0x460ab2`, every logic frame: the outside view's
axis words `[[R+0x22]]`, `[[R+0x26]]` and byte `R+0xd`, zeros in the
cockpit view; [input.md](input.md)), `death_sequence` (`0x45fb73`), and the F12
dialog's idle callback `0x45cfc8` (call at `0x45cfd4`, never live). Unless
dword `C+0x4a` ≠ 0, by the mode word `C+0x36`:

- **0 (free flight):** yaw delta `C+0x30` approaches `multiply_q8(0x800,
  −a)` by `0xc0` per call (`0x401c20` → `approach_word`); with c ≠ 0 the
  speed `C+0x24` approaches `multiply_q8(0x400, −b)` by `0x50` (`0x401c4c`)
  and the pitch delta `C+0x2c` approaches 0, else the speed approaches 0
  and the pitch delta approaches `multiply_q8(0x800, b)`.
- **1 (orbit):** the same with +a and, with c ≠ 0, +b for the step.
- **other modes:** nothing.

`camera_update` then integrates these deltas in the same logic frame (main
loop: logic frame, camera step `0x4011a0`, render root).

## View mode and view subject (logic frame)

`0x45de30()` (logic frame step 15, call at `0x45f872`; also `0x45fafe` in
`death_sequence`): while the display transition byte `[0x4cf99c]+0x1c` is
set and the requested view word `B+0x5e` (`0x4d251a`) differs from the
current `B+0x32` (`0x4d24ee`), it applies it: 1 → camera mode 0 (free)
unless `+0x4a`, `B+0x34` ← `B+0x5c`; 2 → `0x45df34(B+0x78 ? [B+0x60] :
player)`, camera mode 2 (attached), `B+0x34` ← 0; 3 → `0x45df34([B+0x60])`,
camera mode 1 (orbit), countdown `B+0x64` ← 2; 4 → `0x45df34(player)`,
camera mode 3 (replay), countdown 2, `B+0x34` ← 0. Without the transition
byte: in view 3 with the subject equal to the player, `B+0x34` ← `B+0x5c`
when they differ. Then, when the countdown `B+0x64` (`0x4d2520`) is nonzero,
`0x45e208()` once and the countdown − 1 (one step per logic frame).

`0x45df34(S)` (view subject setter; also from `death_sequence` and
`command_key`): constructs the static camera `0x4d2754` once (guard byte
`0x49f1ec`, `0x48bd84`, vtable `0x49f8e8`, fields cleared). It walks the
group cameras `0x4d2688[0 .. word [[P+0x45]+0x10])` (P = the player
`[0x4d24e6]`) and stops at the first whose target `+0x32` is S (cam ← it,
mode 1 unless `+0x4a`) or 0 (free: the current camera's fields `+4..+0x4d`
are copied into it, cam ← it, `0x45edb0(S)`). With neither, the current
camera is copied into `0x4d2754`, cam ← `0x4d2754`, `0x45edb0(S)`. Finally
the view subject `0x4d2684` ← S and the view word `0x4d24f0` ← (S = player ?
word `0x4d2518` : 1).

`0x45edb0(S)`: S's slot `+0x30` (two offset vectors, [vtables.md](vtables.md))
gives the attachment offset a and the orbit offset b; `0x40111c(cam, S, a)`:
unless `+0x4a`, `+0x3e..+0x43` ← a, target `+0x32` ← S, mode ← 2;
`0x40109c(cam, S, b)`: unless `+0x4a`, `+0x38..+0x3d` ← b, target ← S, orbit
angles `+0x26..+0x2b`, distance `+0x22`, deltas `+0x2c..+0x31` and step
`+0x24` ← 0, mode ← 1. So a subject change leaves the camera in orbit mode
with both offsets set.

`0x401078(cam, O)` = 1 when the camera is attached (mode 2) to O: the binning
(`0x408240`), the cell objects (`0x428c60`) and `0x42883c` skip O (the own
vehicle in the cockpit view). `0x45edf8` returns `[0x4d24ea]`. `0x401148`
(the camera mode setter of `command_key`) is not hooked: `command_key` calls
it only when camera word `+36h` = 1, which its mask excludes (verdict dead,
[function_verdicts.md](../reference-game/function_verdicts.md)).

The camera position is also read by the renderer: the binning's distance
gate, the sort distances, the LOD pick of `0x4033e4` (`0x6c601c`) and the DTS
detail selection `0x476248`.

## The hooks (group `camera_view`)

| Hook | VA | Arguments | Callers (scan) |
| --- | --- | --- | --- |
| `camera_attached` | `0x401078` | C, O | `0x40826a`, `0x428877`, `0x428cf1` (binning, cell objects: drawing) |
| `camera_orbit_set` | `0x40109c` | C, S, b | `0x45edea` |
| `camera_attach_set` | `0x40111c` | C, S, a | `0x45edd7` |
| `camera_turn_step` | `0x401c20` | p, w | `0x401cb1`, `0x401cdf`, `0x401cf6`, `0x401d21` |
| `camera_speed_step` | `0x401c4c` | p, w | `0x401cd2`, `0x401d14` |
| `outside_camera` | `0x401c74` | C, a, b, c | `0x45cfd4` (F12 idle), `0x45fb73` (`death_sequence`), `0x460ab2` (`player_input`) |
| `view_apply` | `0x45de30` | – | `0x45f872` (`logic_frame`), `0x45fafe` (`death_sequence`) |
| `view_subject_set` | `0x45df34` | S | `0x45de99`, `0x45deba`, `0x45dedb`, `0x45fae2`, `0x4602c4`, `0x4604b6` |
| `view_offsets` | `0x45edb0` | S | `0x45d237`, `0x45d276` (mission start `0x45cff0`), `0x45e0ae`, `0x45e1cb`, `0x461c68` (`mission_root`) |
| `camera_current` | `0x45edf8` | – | `0x42a86a`, `0x42a8bb`, `0x42a916`, `0x42a959`, `0x43ec27`, `0x4406d2`, `0x450328` (drawing) |

`camera_attached` and `camera_current` are pure readers whose callers draw;
they are hooked with the group because they belong to the camera object and
cost nothing to keep exact. All ten refuse compare mode
([compare_mode.md](../reference-game/compare_mode.md)).

### Register contracts

- **camera_attached** `0x401078`: EAX = EDX = 1 when word `C+0x36` = 2 and
  `C+0x32` = O, else 0; ECX unchanged.
- **camera_orbit_set** `0x40109c` and **camera_attach_set** `0x40111c`:
  locked (`C+0x4a` ≠ 0): no write, EAX = C, EDX/ECX unchanged. Otherwise
  in this order: the dword `[b]` to `C+0x38` (`C+0x3e`), then the word
  `[b+4]` to `C+0x3c` (`C+0x42`), so an offset record inside the camera
  reads its third word after the first store; target `C+0x32` ← S. The
  orbit setter then clears `+0x26` (dword), `+0x2a`, `+0x22`, `+0x2c`
  (dword), `+0x30`, `+0x24` and sets mode 1; EAX = C, EDX = 0 (the zeroed
  six-byte locals at f−8 and f−10h are copied through EDX), ECX = the frame
  address f−10h. The attachment setter sets mode 2; EAX = C, EDX = S, ECX =
  `[a]` with its low word replaced by `[a+4]`.
- **camera_turn_step** `0x401c20` / **camera_speed_step** `0x401c4c`:
  `multiply_q8(0x800 | 0x400, sx(word w))` with the entry registers, then
  `approach_word(p, product, 0xc0 | 0x50)` with EAX = p and multiply_q8's
  EDX/ECX; the result registers are approach_word's.
- **outside_camera** `0x401c74`: the argument dwords the steps get: mode 0
  `0xffff0000 | (−a & 0xffff)` (AX negated over EAX = sx(mode) − 1 = −1),
  mode 1 `a & 0xffff`; the step and pitch targets are EDI/ESI cleared, then
  their low word set (`−b` or `b` in mode 0 as above, `b` in mode 1). Entry
  registers: first call EAX = that dword, EDX = C+30h, ECX = entry ECX;
  second EAX = C+24h; third as the second call left them. Locked: entry
  registers. Modes other than 0 and 1: EAX = sx(mode) − 4, EDX/ECX
  unchanged.
- **view_apply** `0x45de30`: EAX = `[0x4cf99c]` with the low word replaced by
  `B+0x5e` (copied to EDX); the view word is stored to `B+0x32` before the
  case; ECX = sx(view) − 1 counted down by the case chain (0 for views 1, 3
  and 4 at the call, the subject for view 2); after each `0x45df34` call ECX
  = the pushed subject, EAX = `[B+0x2e]` reread. Without the transition
  byte, EDX = `[0x4d2684]` only in view 3. The countdown word is reread
  after `0x45e208` (`dec word`).
- **view_subject_set** `0x45df34`: frame descriptor `0x49f1fd` (offset −20h
  read from the file), so `0x49630c` links its record at f−20h; the try word
  f−10h ← 8 only on the construction path; locals: count word f−22h, index
  dword f−28h. Construction (guard byte `0x49f1ec` = 0): `0x48bd84(0x4d2754)`
  with EAX = 49630c's result (the old FS:0 head), then vtable `0x49f8e8`,
  dwords `0x4d2786`, `0x4d279e` and words `0x4d278a`, `0x4d279c`,
  `0x4d279a`, `0x4d2798` ← 0, `[[0x6cbee4]]` + 1 − 3, guard + 1. The group
  loop compares sx(count) > index (signed), so a negative count word skips
  both the loop and the static camera (only the subject and the view word
  change). The free-camera copy runs in the order 22, 24, 26, 2a, 2c, 30,
  32, 36, 38, 3c, 3e, 42, 44, 48, 4a, 16, 1a, 1e, 10, 14 (words or dwords as
  in the listing, through CX/ECX), then +0c, +04, +08 (through ESI); the
  static copy has the same offsets with EDX and ECX alternating and ends
  with +0c, +04, +08. Calls of `0x45edb0`: free camera EAX = the camera, EDX
  = S, ECX = `[src+8]`; static EAX = S, EDX = `[src+0xc]`, ECX = `[src+4]`,
  and `[0x4d24ea]` ← `0x4d2754` after the push. Result: AX = the view word
  over the last EAX (the found camera, view_offsets' EAX or sx(count)), EDX
  = the record's saved head `[f−20h]`, ECX = S; the epilogue restores FS:0
  from EDX (the adapter does the same).
- **view_offsets** `0x45edb0`: `[S]+0x30` (S, f−8, f−10h) with EAX = f−10h,
  EDX = f−8, ECX = the vtable; `0x40111c([0x4d24ea], S, f−8)` with EAX =
  f−8, EDX = the camera; `0x40109c([0x4d24ea], S, f−10h)` with EAX = the
  camera reread, ECX = f−10h; the result is the last call's. The three slot
  `+0x30` implementations (`0x40351c` through `0x411a74`, `0x411a74` by two
  `memset`s of 6 bytes, `0x4155c4` word by word) write all twelve bytes, so
  no local is read uninitialized (listing).
- **camera_current** `0x45edf8`: `mov eax,[0x4d24ea]; ret`.

### Exit ECX at the call sites

EDX:EAX are exact in every core. The framework returns the entry ECX, so
the original's exit ECX must be dead at the call sites (listing, each path
to the first write):

- `camera_orbit_set`, `camera_attach_set`: the only caller `0x45edb0`
  writes ECX (`lea ecx` at `0x45eddf`) after the first; after the second
  ECX leaves `0x45edb0`, whose five callers `pop ecx`.
- `view_subject_set`, `view_offsets`: every caller pops ECX.
- the steps: their exit ECX reaches only the next step (multiply_q8 reads
  no register; approach_word loads ECX from the stack, `0x467ae5`) and
  `outside_camera`'s exit.
- `outside_camera`: `0x45cfd9` reaches `camera_update`, which reads no entry
  register; `0x45fb78` reaches `timer_dword_step` (writes ECX only) and the
  `pop ecx` of `death_sequence`'s epilogue; `0x460ab7` (`player_input`):
  `movsx ecx` before `0x41bb9c`, `fire_line` whose first callee `0x410dbc`
  pops ECX, the player's slot `+0x3c` (leaves `actor_angle`, `gun_angle`,
  `mech_angle`, vtables.md; `pop ecx` after) and, at `0x460c24`, the high
  word of `leg_input`'s third slot, which `leg_input` uses only as SI.
- `view_apply`: `0x45f877` either writes ECX (`0x45f8dd`) or calls
  `0x4123ac`, whose first loop overwrites it (`setne cl`/`and ecx,1` for
  every actor of the never-empty list `0x4a9af8`; an empty list was not
  followed); `0x45fb03` (`death_sequence`) loads only CX before pushing it
  as `outside_camera`'s third argument, of which that reads the word.
- `camera_attached`, `camera_current`: ECX unchanged.

No entry or exit shim.

### Game functions called by the group

`0x48bd84` (the graphics camera constructor, own service `camera_ctor` in
[hud_load.c](../../sim/src/hud_load.c)) and `0x49630c` (the runtime frame
record, own service `frame_record`) are called by `view_subject_set` through
the bridge.

`0x45e208` (view_apply's countdown step, also called twice from `0x45cff0`)
draws the subject's label: font record `[0x4d1e2c]` saved at `0x4d27a4`, the
rectangle `0x4d27a8..0x4d27b4` cleared through `0x486704`, strings from the
table `0x4d16a0` (the entry chosen by the view word `0x4d24f0`) and a name
from `0x434298` or `0x49f240` formatted through `0x4967ea` and drawn through
`0x482634` (roles read from the calls). It writes only its own globals
`0x4d27a4..0x4d27b7` (no other reference, dword scan), the guard `0x49f211`,
the graphics context `[0x6c5f70]+0x208/+0x20c` (restored) and `+0x22c` (left
pointing at its stack local), and the frame buffer. It is game code in the
oracle and the own service `subject_label` in `es2sim`
([loop.c](../../sim/src/loop.c); [main_loop.md](main_loop.md#0x45e208-the-subject-label)).

## Render camera

The graphics library's camera (vtable `0x4a5b29`, constructor `0x48bda4`,
own service `camera_main_ctor`) extended by the game's fields as in the
object table above. Its consumers are own code in [view.c](../../sim/src/view.c);
the contract, the data words and the verification are in
[rasterizer.md](rasterizer.md#view-transform-projection-and-lights) (the
renderer line: [rasterizer.md](rasterizer.md#the-renderer-line-r3d)).

### Camera setup `0x48c2e4(cam)` (render root steps 5 and 10)

Also called by the HUD line (`0x43fe44`, `0x44060c`, `0x44d5c0`, `0x44e334`,
`0x44f0c4`) and the drop sequence `0x45d85c`. What it takes from the camera
record: the position `+4`, the angles `+0x10` (words), the context pointer
`+0x16` (viewport `+0x210..+0x21f`, centre offsets `+0x220`/`+0x224`), the
focal shift `+0x1a`, the near plane `+0x1e` and the divisor `+0x22`.

1. Once: `0x6c5f94` ← matrix constructor (guard byte `0x4a5b0c`).
2. `[0x6c6028]` ← cam `+0x1a`; `[0x6c602c]` ← cam `+0x1e`; when the focal
   shift is 0, `[0x6c6030]` ← cam `+0x22`.
3. `0x6c603c..0x6c6048` ← the context's clip rectangle `[cam+0x16]+0x210..
   +0x21c`; `0x6c6034`/`0x6c6038` ← its left/top minus the context origin
   `+0x220`/`+0x224` (`vector_subtract`, hook). The render root added the
   jolt point to that origin at step 4, so the jolt shifts the projection
   offset (`0x6c6034`/`0x6c6038` is the screen centre).
4. `0x6c5f94` ← cam's pose (`0x495178`: position `+4`, angles `+0x10`),
   inverted in place (`0x48db12`); `0x48c3b0(0x6c5f94)`: `[0x6c5ff4]` ← the
   camera matrix, the view transform `0x6c5fb4` = compose(`[0x6c5ff8]`,
   camera) (`0x48db57`), its copy `0x6c5fd4` inverted, the eye in object
   space `0x6c601c` (`0x48db70`, `0x48d9e4`).

### View transform `0x48c444(M)`

`[0x6c5ff8]` ← M or the identity `0x6c5ffc` for M = 0; `0x6c5fb4` ←
compose(M, `[0x6c5ff4]`); `0x6c5fd4` ← its inverse; `0x6c601c` ← the eye in
M's space; then, when byte `0x6cbc04` is set, each active light
`0x6c60ac[0 .. [0x6c60a8])` is moved into M's space (`0x48bcfc`). Called by
every object and part draw (`0x401fe4`, `0x47613c`, `0x478d60`, …), the
terrain (`0x46cb9c`, `0x428140`, `0x46d1a8`) and, outside the renderer, by
the positional sound `0x46299c` with M = 0
([audio.md](audio.md#what-can-change-the-oracles-comparison), item 3). The
latter reads the camera matrix `[0x6c5ff4]` that the last `0x48c2e4` left, so
the volume and pan of a positional sound follow the last camera setup
(stream B does not). `0x48c444(0)` must stay callable from the logic frame.

### Projection

`0x48c57c(p, v)`: the view-space point (`0x48d9e4` with `0x6c5fb4`).
`0x48c5fc(v, s)`: s = (v.x << f / v.y, v.z << f / v.y) with f = `[0x6c6028]`
(or `(·) << 8 / [0x6c6030]` when f = 0); `0x48c6d0(s, t)` adds the offset
`0x6c6034`/`0x6c6038`; `0x48c5cc(size, depth)` = size << f / depth (or `<< 8
/ [0x6c6030]`). The near plane `[0x6c602c]` is the clippers' (`0x468760`,
`0x46817c`, `0x48cf20`). `IDIV` by a zero depth would fault; the callers clip
at the near plane first (static, not checked per caller).

### Own code state

The view set-up `0x48c2e4`, the view and object selection `0x48c3b0` and
`0x48c444`, the projection leaves `0x48c57c`, `0x48c5fc`, `0x48c5cc` and
`0x48c6d0`, the saved view `0x48d7ec`, the inverse `0x48db12` and the light
objects are own code in `view.c`, with the globals `0x6c5f94..0x6c6048`,
`0x6c5ff4`/`0x6c5ff8`, `0x6cbc04` and the active light list
`0x6c60a8`/`0x6c60ac`. Confirmed in vitro (`differential_view_test`, 1500
view cases); not live.

## Evidence

- **terrain_face, camera_update.** In vitro (`differential_test
  terrain_face|camera_update`, [camera_cases.h](../../tools/reference-game/invitro/camera_cases.h)):
  1,001,024 direct cases each (`ES2_INVITRO_CAMERA_CASES` caps them); for
  `camera_update` eleven callees as logging stubs plus the virtual slot,
  recorded with entry registers and input payloads; 256 installed poison
  cases each (31h/A7h stack, fast and preserving dispatch); 3,072
  real-callee cases for `camera_update` on generated terrain with real
  target and mount matrices, ten nested hooks in rotating modes. 38 branch
  sites (9 + 29), four proved one-way. Original run logs:
  `<es2-data>\logs\wp-h4-20261008\wp1` (directory not present now).
- **camera_view.** In vitro (`differential_camera_view_test`, two shards,
  [camera_view_cases.h](../../tools/reference-game/invitro/camera_view_cases.h),
  `ES2_INVITRO_CAMERA_VIEW_CASES` caps the direct cases): the original, the
  adapter and the core equal in EDX:EAX, ECX (except camera_orbit_set's frame
  address), the logged callee calls with entry registers, arena and globals
  hashes, the arena and the mapped globals; 256 installed 31/A7 poison cases
  per hook (fast and preserving dispatch); 3,072 real-callee cases each for
  the six hooks with hooked callees, the nested hooks in rotating modes (all
  entered). 32 branch sites, every one both ways, none proved one-way. Unit
  test `camera_view_test` ([test_camera_view.c](../../core/tests/test_camera_view.c),
  hand-worked synthetic cases).
- **Mutations** ([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md),
  `check_mutations.ps1`): `terrain_face` flags-2 diagonal on the second face;
  `camera_update` orbit redone for a zero pitch delta; one per `camera_view`
  hook, caught at direct case 0 to 51.
- **Live.** The hooks are replaced in the oracle regression; their hook
  counters are in the function index ([docs/functions](../functions/README.md)).
  The view and projection code of `view.c` is verified in vitro only.
