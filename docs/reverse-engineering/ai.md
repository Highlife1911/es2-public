# Vehicle AI: steering, vehicle control, behaviours and MECH states

The AI code of DBSIM between the object queries and the MECH state
methods: the object line and pool queries, obstacle steering, vehicle
control, formation steering and slots, the behaviours around the torso
servos, attack planning, the MECH combat, notice and travel states and the
TURBO engage. Addresses are VAs at image base `0x400000`
([reference_version.md](../reference_version.md)); contracts are read from
the `llvm-objdump -d -M intel` listing of `VER95/DBSIM.EXE`. Roles are
hypotheses from the code paths; the register and memory effects are what
the replacements reproduce and what the in-vitro groups check.

| Adapter group | Hooks | Core | Adapter |
|---|---|---|---|
| `ai` | `object_line` 404ca0, `obstacle_steer` 416274 | `core/src/ai.c` | `proxy/ai_hooks.h` |
| `pool_query` | `pool_query` 404bc0 | `core/src/obstacle.c` (`es2_pool_query_registers`) | `proxy/pool_query_hooks.h` |
| `vehicle_control` | `part_ratio` 40dbc0, `slope_speed` 41693c, `random_damage` 417a04, `vehicle_control` 416a04 | `core/src/vehicle_control.c` | `proxy/vehicle_control_hooks.h` |
| `ai_motion` | `formation_steer` 41fbb8 | `core/src/ai_motion.c` | `proxy/ai_motion_hooks.h` |
| `formation` | `formation_base` 405c04, `formation_mech` 417898, `formation_flyer` 421e98 | `core/src/formation.c` | `proxy/formation_hooks.h` |
| `ai_behaviour` | `weapon_range`, `weapon_choose`, `aim_fire`, `aim_target`, `target_search`, `hunt`, `line_blocked`, `clear_shot`, `group_move` | `core/src/ai_behaviour.c` | `proxy/ai_behaviour_hooks.h` |
| `ai_behaviour2` | `aim_select`, `attack_plan`, `attack_replan`, `group_scan`, `order_scan`, `retreat_fire` | `core/src/ai_behaviour2.c` | `proxy/ai_behaviour2_hooks.h` |
| `mech_combat` | `engage_end`, `skirt_check`, `attack_step`, `base_attack`, `flyer_attack`, `face_off`, `flank_attack`, `drive_off` | `core/src/mech_combat.c` | `proxy/mech_combat_hooks.h` |
| `mech_notices` | `mech_contact`, `mech_kill`, `mech_alert`, `mech_attacked` | `core/src/mech_notices.c` | `proxy/mech_notices_hooks.h` |
| `mech_travel` | `fire_permit`, `dead_step`, `sleep_step`, `point_move`, `leader_route`, `ram_move`, `ram_step`, `travel_step`, `follow_step` | `core/src/mech_travel.c` | `proxy/mech_travel_hooks.h` |
| `cold_paths` | `turbo_engage` 40f09c | `core/src/mech_travel.c` | `proxy/cold_paths_hooks.h` |

Adapters are under `tools/reference-game/proxy/`, the in-vitro cases under
`tools/reference-game/invitro/` (`<group>_cases.h`), the mutation witnesses
in [MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md). Every
game call of a replacement goes through the guest bridge with the
EAX/EDX/ECX the original holds at that call, so nested hooks keep their
own modes.

**Notation** (where a section defines a letter differently, the section
wins). A = the actor (vehicle) the code runs for; G = `[A+45h]` its group;
L = `[[G+0Ch]]` the first member of the group (the leader, may be A); T =
`[A+1A4h]` the current target; S = A+4Dh the script/state record, B = A+5Ah
the behaviour block inside it ([mech_order.md](mech_order.md)); F = A+92h,
the base through which the originals address the flag bytes (F+4 = A+96h
fire permission, F+5 = A+97h, F+7 = A+99h, F+0Bh = A+9Dh, F+11h = A+0A3h,
F+12h = A+0A4h, F+13h = A+0A5h, F+16h/F+17h = A+0A8h/A+0A9h, F+1Ah =
A+0ACh, F+1Bh = A+0ADh, F+1Fh = A+0B1h, F+20h = A+0B2h, F+24h = A+0B6h).
lo(r, w) is a 16-bit load or 16-bit arithmetic that keeps r's high word; sx
is sign extension of a word, sx8 of a byte. "Regs" lists EAX/EDX/ECX at a
call. A step "PROVED" one-way has one feasible edge by the listing.

## 404ca0 `object_line`: objects along a segment

cdecl `(out, start, end, radius, filter)`; `start`/`end` are XYZ dword
points, `radius` and `filter` are used as words. Two callers: 416274 (twice,
radius 0, filter -1) and 41dc24 (radius 0, filter -1). No conditional
branch (82 instructions).

1. 492990(&angles, end, start) writes three angle words at EBP-8 (pitch,
   roll 0, heading) for the direction start -> end.
2. The forward pose at EBP-54h gets the translation start (written Z, X, Y
   at EBP-38h, -40h, -3Ch) and 47ebb8(&angles, pose) fills its matrix.
3. The segment record at EBP-34h, the same layout the swept query uses
   ([weapons.md](weapons.md#swept-query-426528)): +0 pointer to
   the forward pose, +4 length = 49288c(end, start), +8 radius word, +0Ah
   the inverse pose: the 20 matrix bytes copied (REP MOVSD), translation =
   start, then 47df19 transposes the matrix, the translation is negated
   (Z, X, then Y), copied to EBP-60h and rotated by 480304(EBP-60h, +0Ah,
   +1Eh) into +1Eh.
4. 404bc0(out, record, filter) (`pool_query`, below) and its EDX:EAX is the
   result: AX = 1 on a hit, else 0.

**Registers.** Entry EDX/ECX reach 492990 unchanged; every other callee gets
the registers the instructions before it leave (reproduced). The filter
slot is pushed as `mov ax, word [ebp+18h]; push eax`, so its high word is
480304's EAX; 404bc0 reads only DI. Both callers test AX only and overwrite
EAX, EDX and ECX before any other read: 416274 at 416412..41642d and
41656e..416589; 41dc24 at 41dcd4..41dcfe, and its own return path to
41ddf8 pops ECX and rewrites EDX. The hook returns 404bc0's pair unchanged.

**Stack residue.** 47ebb8 writes the nine matrix words and the mode byte 18,
never byte 19, so byte 19 of the forward pose (EBP-41h) is uninitialized and
REP MOVSD copies it to record+1Dh. Nothing reads it: 427da8 hands both
matrices only to 48043c, which reads the words, byte 18 and the translation
(the swept query has the same residue). The replacement keeps its scratch
zero there, so no entry shim is needed; the in-vitro stubs mask that byte.

## Pool object query 404bc0

Hook `pool_query` (404bc0), the last step of `object_line`. It is the
sibling of `obstacle_test` 404ae4
([mission_runtime.md](mission_runtime.md)); its two callees are hooks: the
pool step 471c70 (`pool_prev`,
[main_loop.md](main_loop.md#pool_prev-0x471c70-cdecl-p-o)) and the object
query 427da8 (`object_query`,
[weapons.md](weapons.md#427da8-object_queryout-segment-list-count)).

### 404bc0 `pool_query(out, record, filter)`

cdecl, three slots, 74 instructions. `out` and `record` are only passed on
to 427da8 (`record` is the segment record `object_line` builds at its
EBP-34h); of `filter` only DI is read (`mov edi,[ebp+10h]`, then `test
di,di` and `cmp di,dx`; EDI is saved and restored). Entry EAX/EDX/ECX are
never read (`xor eax,eax`, `xor edx,edx`, `lea ecx,[ebp-240h]` first).
Frame `add esp,-240h` after the saved EBP: the list L = EBP-240h (140
dwords, up to EBP-11h), the cursor dword EBP-10h, the result dword EBP-0Ch,
the word EBP-8 (never written), the count word EBP-6, the iterator dword
EBP-4; then EBX/ESI/EDI.

1. iterator ← 0, count ← 0, result ← 0, cursor ← L; EAX = EDX = 0, ECX = L.
2. Loop (404c49): o = 471c70(`[4a95a0]`, iterator), the pool dword reread
   for every call (ECX = the pool, EAX/EDX as the previous step left
   them); iterator ← o, EBX = o; o = 0 ends the loop (JNE 404c63).
3. Team filter, only for DI ≥ 0 (signed word; JL 404bed skips it): EAX =
   `[o+45h]`, EDX = MOVSX of byte `[EAX+12h]`; DI ≠ DX (JNE 404bf9) skips
   o. A team byte 80h..FFh becomes FF80h..FFFFh and never matches a
   non-negative DI.
4. ECX = `[o+1F2h]`. Word `[ECX+6]` = 0 (JE 404c06) appends o directly.
   Otherwise the first test of `obstacle_test`: EDX = `[o+1F2h]` reread;
   word `[EDX+4]` = FFFFh (JE 404c13) → EAX = 0; else ECX = `[o+205h]` and
   (int16)`[ECX+5]` ≥ 2 (JGE 404c20) → EAX = 0; else EAX = o+92h, then 1
   when byte `[o+99h]` ≠ 0 (JNE 404c2c), else 0. AX = 0 (JE 404c3a) skips
   o. Unlike 404ae4 there is no exclude pointer and no second test.
5. Append: EDX = the cursor slot's value, `[EDX]` ← o, the count word += 1
   (16-bit), the cursor slot += 4 (each read from its frame slot).
6. After the loop (EAX = 0, EDX = the last 471c70's): count word = 0 (JE
   404c6a) skips the query. Otherwise 427da8(out, record, L, count slot)
   with EAX = record (the dword slot), EDX = out, ECX = L; the count slot
   holds DX = count with the high word of the last 471c70's EDX (427da8
   reads it as a signed word only). 427da8 writes out = {distance, object
   or object+0Ch} and shortens the record length on a hit. AX ≠ 0 (JE
   404c8b falls through) sets the result dword to 1.
7. Return: `mov ax,[ebp-0Ch]`, so EAX = (high word of the EAX left by the
   last callee) | result word: 0 when nothing was listed (471c70 returned
   EAX = 0), else 427da8's high word with 1 or 0. EDX = the last 471c70's
   (empty list) or 427da8's. ECX = the last callee's.

**Registers into 471c70** (reproduced; `pool_prev` reads none of them, it
returns the entry EDX for O ≠ 0): the first call EAX = EDX = 0; after a
team skip EAX = `[o+45h]`, EDX = the sign-extended team byte; after a
first-test skip EAX = 0, EDX = `[o+1F2h]`; after an append EDX = the old
cursor (a frame address) and EAX = 1 (passed the first test), o (flag word
0, DI < 0) or `[o+45h]` (flag word 0, DI ≥ 0). A frame address therefore
reaches only 471c70's EDX and the high word of the count slot; in the
replacement it is the address in the adapter's frame image.

**Callers.** One: 404ca0 at 404d61 (no data reference to 404bc0 in the
file). It pushes the filter as `mov ax,[ebp+18h]; push eax` and returns
404bc0's EDX:EAX unchanged after `add esp,0Ch` and its epilogue. Both
callers of `object_line` pass filter −1, so live DI = FFFFh and the team
filter (step 3) never runs; it is covered in vitro. They test AX only and
overwrite EAX, EDX and ECX first; the out record is read only after AX ≠
0, which implies 427da8's write. EDX can be a frame address of 427da8 (its
last EDX, weapons.md); with the hook it is the bridge run's own. Flags are
dead (404ca0's epilogue, then TEST AX).

**Stack.** Every local is written before it is read, except the word
EBP-8, which only 427da8 can see, and only as list entry 142 after an
overflow (below). 404bc0 reads nothing below its own ESP. What the
original 404bc0 and its callees leave below `object_line`'s frame is not
read by 416274 or 41dc24 or their later callees (the `line_blocked` row of
[Stack residue under aim_fire](#stack-residue-under-aim_fire); 416274's
**Stack** paragraph). Under the replaced `object_line`, 404bc0 runs below
the dispatcher's frames, not at its original coordinate. No entry shim.

**Overflow** (as 404ae4, mission_runtime.md): the 141st append writes o_140
into the cursor slot EBP-10h itself, the cursor becomes o_140 + 4, and
every later append writes into o_140's fields and beyond. 427da8 then sees
entry 140 = the final cursor, 141 = the result dword (0), 142 = (count <<
16) | the uninitialized word EBP-8, 143 = the iterator (0); entry 141
makes the original fault at `[34h]` unless an earlier hit ends 427da8's
loop (distance below 500). The pool holds at most as many objects as the
mission loader's 140-entry base-object tables (mission_runtime.md, proof
for 404ae4), so it is unreachable in valid states. The replacement keeps
the 240h-byte frame image in the adapter's zeroed buffer and appends
through the cursor slot, so it reproduces the overflow; EBP-8 reads 0 there.

**Branch sites** (10, all both ways in vitro): 404bed, 404bf9, 404c06,
404c13, 404c20, 404c2c, 404c3a, 404c63, 404c6a, 404c8b.

**State.** Reads `[4a95a0]`, the objects' +45h (and the team byte +12h of
that block), +1F2h (words +4, +6 of the configuration), +205h (word +5),
byte +99h; writes only its frame and, through the cursor after an
overflow, o_140's memory. Callees: 471c70 and 427da8 through the guest
bridge with the original's registers; the hook refuses compare mode.

### Validation of pool_query

`pool_query_cases.h`, `differential_harness pool_query`:

- 1,001,024 direct cases against logging stubs for 471c70 (a planned walk
  over a shuffled used list, sometimes shortened or repeating an object;
  it may move the pool pointer and change a later object's dead byte,
  configuration or team byte) and 427da8 (out, record, list by content,
  count slot, entry EAX/EDX/ECX; AX = 0 half of the time, a hit writes
  out). Filter words negative, FFFFh, team values (0..3, 7Fh, 80h, 82h,
  FFh) or random, with random high words. Stack addresses are masked. One
  case in 128 and cases 16..47 list 139 to 310 objects (the overflow;
  entry 142's low word, the original's EBP-8, masked). Full pair, ordered
  calls, listed entries, arena, `[4a95a0]` and `[4aaad0]`; callee counter
  deltas checked.
- 256 installed cases (31h/A7h stack patterns, fast and preserving
  dispatch, live x87 stack).
- 6,144 real-callee cases: the real pool walk and the real 427da8 with
  4273c8, 4927f0 and the math leaves on generated height grids, a segment
  record built by the original 492990/47ebb8/49288c/47df19/480304, objects
  near the segment and up to eight pool members with mixed filter fields;
  `pool_prev`, `pool_next`, `object_query`, `grid_sweep`, `set_length`,
  `cell`, `dist3d_delta`, `transform_point`, `dist_gate`,
  `matrix_transpose`, `rotate_point`, `euler_matrix`, `dist2d_delta` and
  `object_line` in rotating modes, `pool_query` original or replaced. Half
  of the cases enter at 404bc0, half through `object_line` 404ca0 (the
  live path, filter −1 or a team value). EAX, memory and both globals
  compared; EDX unless both runs return a stack address.
- Coverage: all 10 sites both ways.
- Mutation witnesses: the team byte zero-extended, all of EAX tested after
  427da8, the list bounded at 140.

**Live.** With all hooks replaced all six suites are identical;
`hk_pool_query_calls` equals `hk_object_line_call_404bc0`, `call_427da8`
equals the calls and `call_471c70` is at least the calls (one walk step
per pool member plus one).

## 416274 `obstacle_steer`: feelers, vehicles, avoidance points

An obstacle avoidance step (older notes call it the "AI sight check"; it
calls the terrain walk `segment` 46e980 at 41643b and 416597): two feeler
segments, nearby vehicles and a table of avoidance points decide how far
to turn away and whether to slow down.

cdecl `(actor, turn, speed, flags)`; `turn` and `speed` point to words
(416a04 passes the addresses of its own argument slots), only bit 0 of
`flags` is read. Only caller: 416a04 (vehicle control) at 416acf, for
actors other than [4a9b84] whose byte +0A4h is 0.

**Early exits.** Word actor+28Eh = 0 and speed word = 0: return. Speed word
negative (signed) and dword actor+26Eh = 0: return.

**Constants.** The word at EBP-2 is written 0 at 4162b0 and never again:
the feeler length word is (B1E0h + 9C40h) & FFFFh = 20000 (4162c1), no half
turn is added to the heading differences (41667a, 416788), and the final
comparison at 416918 always takes the jump. ESI ("left", the first feeler)
and EDI ("right") start at 22000 (55F0h) and keep the nearest obstacle per
side, as signed dwords.

**Feelers (flags bit 0 clear).**

1. h = 46e180([4a0be0], actor+26h), the terrain height at the actor.
2. Left feeler: local points (-1500, 0, 0) at EBP-0D0h and (-10000, 20000,
   0) at EBP-0C4h. Each is transformed by 48043c with the actor matrix
   actor+12h (translation = actor position actor+26h) into EBP-0ACh /
   EBP-0B8h; before each transform an inlined "matrix of pose" calls
   47ebb8(actor+0Ch, actor+12h) and sets word actor+32h = 1 when that word
   is 0. Both world points get Z = h.
3. 404ca0(EBP-10h, first, second, 0, -1): AX != 0 and ESI > [EBP-10h]
   (signed) gives ESI = [EBP-10h].
4. 46e980([4a0be0], first, second, 0, EBP-0DCh, 1): AL != 0 gives
   d = 4928d0(first, EBP-0DCh) and ESI = min(ESI, d).
5. Right feeler: (1500, 0, 0) and (10000, 20000, 0) (at EBP-0E8h) the same
   way into EDI.

**Vehicles (flags bit 0 clear).** For index 0.. while index < word [4a9afe]
(signed words) the pointer list [4a9af8] is read; a null entry ends the
loop. The actor itself and vehicles with dword [[v+45h]+14h] != 0 are
skipped, then the virtual v->vtable[+7Ch](v) must return AX != 0.
v' = 47e0b0(2000, 49288c(actor+26h, v+26h)); when ESI or EDI exceeds v',
a = AX of 492934(v+26h, actor+26h) minus word actor+10h; |a| (8000h counts
as 7FFFh) must be below 2000h (45 degrees); a < 0 updates EDI, else ESI
(minimum).

**Avoidance points.** If byte [[[actor+45h]+0Ch]]+0A3h != 0, for i below
dword [4a9cc8] (signed) the point at 4a9b88 + 8i (X, Y): v' = 47e0b0(1000,
4928d0(actor+26h, point)), the same side test with a cone of 3000h (67.5
degrees); an update also sets that side's flag (EBP-74h right, EBP-70h
left).

**Steering.**

1. If byte actor+0AEh != 0 and nothing was found (ESI = EDI = 22000): word
   actor+254h > 0 shortens ESI by a quarter (to 16500), else EDI.
2. Both >= 22000: return.
3. If byte actor+0AEh != 0 and both are below 22000, ESI += signed word
   actor+254h (32-bit wrap).
4. n = min(ESI, EDI); the avoidance-point flag of that side (a tie counts
   as the ESI side) selects limit 3000 and gain 1000, else limit 0 and gain
   700.
5. q = (int32)((22000 - n) * gain, low dword) / 22000 (IMUL, then CDQ
   discards the high half before IDIV, truncation toward zero). turn word
   += q when EDI was smaller, else += -q (16-bit).
6. n >= limit: return. Otherwise the speed word becomes word
   [[actor+1F2h]+4]. (The half-speed alternative at 41691a needs the EBP-2
   word to be 1 and never runs.)

**State.** Reads the actor fields named above, the vehicle list and its
objects, the point table, [4a0be0], [4a9af8], word [4a9afe], [4a9cc8].
Writes word actor+32h (and through 47ebb8 the actor matrix), the turn and
speed words, and frame locals; no global. Callees: 46e180, 47ebb8, 48043c,
404ca0, 46e980, 4928d0, the virtual +7Ch, 49288c, 47e0b0, 492934.

**Registers after return** (EAX / EDX). First exit: the speed pointer /
entry EDX. Second exit: entry EAX (word actor+28Eh nonzero) or the speed
pointer / the speed pointer. "Nothing near": p = [[[actor+45h]+0Ch]]+92h
(points skipped) or the final point index / actor+92h. Steering exit: the
turn pointer / the step added to the turn word (quotient, low word negated
for the ESI side). Speed exit: the speed pointer / 1. All are dead at 416a04:
after 416ad4 it writes AX from actor+28Eh, then EDX (416ade) and ECX
(416aeb) and overwrites EAX on both following paths (416af4, 416b0d) before
any read. The hook still returns the exact pair.

**Stack.** Every local the function or a callee reads is written first in
all paths: [EBP-10h] is read only when 404ca0 returned AX != 0, which in
427da8 implies the out write; EBP-0DCh only when 46e980 returned AL != 0,
which implies its out write in mode 1 (weapons.md). No entry shim.

**Static facts.** 416274 has no x87 instruction; the two 10-byte
`mov dword [ebp-disp32], imm32` at 416304 and 416320 are integer moves. The
one indirect call is the vehicle virtual +7Ch. Of the eight absolute
addresses it names, three are call targets (4928d0, 49288c, 492934); the
globals are 4a0be0, 4a9af8, 4a9afe, 4a9b88, 4a9cc8.

**Branches.** 404ca0 has none. 416274 has 59 conditional sites; four have
one feasible edge only: 4162bf (never taken, EBP-2 = 0), 41667a and 416788
(always taken, same word), 416918 (always taken: SETL repeats the
comparison that 4168fb just failed, so EDX = 1 against ECX = 0). The four
inline matrix tests (416346, 41639b, 4164a2, 4164f7) all reach both edges
in vitro: the stubs for 48043c and 404ca0 may clear the actor word +32h,
since the replacement rereads it at each test as the original does.

### Validation of object_line and obstacle_steer

`ai_cases.h`, `differential_test object_line` and `differential_test
obstacle_steer`:

- 1,001,024 direct cases per hook. Every callee (492990, 47ebb8, 49288c,
  47df19, 480304, 404bc0 for 404ca0; 46e180, 47ebb8, 48043c, 404ca0,
  46e980, 4928d0, 49288c, 47e0b0, 492934 and the generated vtable's +7Ch
  for 416274) is a logging stub recording ordered arguments, entry
  EAX/EDX/ECX and frame payloads by content. Values inside the worker
  thread's stack reservation (frame addresses) are masked, as are outputs
  the caller has not written yet and matrix byte 19. Stub results cover
  both thresholds (22000, 3000), ties, negative and full-width distances,
  the cone edges 1FFFh/2000h/2FFFh/3000h and 8000h, AX/AL = 0 with upper
  bits set; vehicle lists with the actor itself, null entries, counts -1 to
  13, 7FFFh and 8000h; point counts -1 to 11, 80000000h; a quarter of the
  cases find nothing. Callee counter deltas are checked.
- 256 installed cases per hook (31h/A7h stack patterns, fast and preserving
  dispatch, live x87 stack).
- 6,144 real-callee cases per hook: nested hooks in rotating modes, a
  patterned stack, EAX (object_line) or EDX:EAX (obstacle_steer), the whole
  arena, the globals and the inputs of the remaining stubs compared. Terrain
  of up to 8 x 8 cells with flags 0..2; an actor, six objects and an object
  pool [4a95a0] linking a subset (sometimes the actor) for 404bc0 and
  427da8; vehicles and avoidance points around the actor. Only 4273c8 (the
  shape query below 427da8) and the virtual +7Ch are stubs.
- Coverage: all 59 sites of 416274 (55 both ways, the four above one way).
- Mutation witnesses: an unnegated inverse translation Y (object_line), the
  excluded 2000h cone edge and the 64-bit steering product instead of the
  CDQ-truncated one (obstacle_steer).

## Vehicle control 416a04 and its helpers

Four hooks of the group `vehicle_control`: `part_ratio` (40dbc0),
`slope_speed` (41693c), `random_damage` (417a04) and `vehicle_control`
(416a04), the parent of `obstacle_steer`. Notation: A = vehicle, E = A+92h
(its flag bytes), S = [A+1F2h]+2 (its configuration, read once at entry of
416a04), T = [A+22Ch] (its animation track). "walk", "stand" and "fall"
are hypotheses for the clip words.

### 40dbc0 `part_ratio`: condition of a part group, Q8

cdecl `(obj, group)`; only the low word of `group` is used (signed). Leaf,
no writes, three conditional sites (40dc0b, 40dc2b, 40dc46). Seven callers
(416a04 passes A+206h).

- tab = [obj+0Ch]; entry = [tab] + 18 * group (18-byte group records);
  M = [tab+4] (word table of part maxima); X = [obj] (current part values);
  G = [obj+4] (group values).
- max = signed word entry+0; cur = signed word G[group]; a negative cur
  takes max.
- For i = 0 while i < signed word entry+6 (16-bit signed compare): p =
  signed word at [entry+8] + 4i + 2; x = X[p], a negative x takes M[p];
  cur += x, max += M[p] (signed 32-bit sums).
- EAX = (cur << 8) / max (IDIV after CDQ of the shifted 32-bit value), EDX
  the remainder. max = 0 or a quotient overflow faults (#DE) in the
  original; that is a precondition of the replacement.

ECX at return (the last part maximum over a sign-extended index high word,
or the sign-extended base word) is outside the result contract.

### 41693c `slope_speed`: speed along the terrain slope

cdecl `(A, speed)`; `speed` points to a word (416a04 passes its own argument
slot). Only caller 416a04 (416a7f).

1. f = 46e498([4a0be0], A+26h) (`terrain_face`; ECX = the map, EDX = A+26h,
   EAX = the speed word in AX over the entry EAX).
2. Inline "matrix of pose": word A+32h = 0 runs 47ebb8(A+0Ch, A+12h)
   (EAX = A+12h, EDX = A+0Ch) and sets the word to 1.
3. The local vector (0, speed) at EBP-10h is rotated by 47ff8c with the
   first four matrix words of A+12h into EBP-0Ch: x' = (x m0 + y m2 + 2000h)
   SAR 14, y' = (x m1 + y m3 + 2000h) SAR 14 (32-bit wrap). 47ff8c is a
   pure helper (also a hook of its own, `rotate_words`); this core computes
   it with its result registers (EAX = EBP-0Ch, EDX = low dword of y m3,
   ECX = m3 sign-extended).
4. f = 0: return. Otherwise d = 47dbca(f, EBP-0Ch), the 2D dot product of
   the face's first two words with (x', y'), also computed in core (ECX =
   f.x x', EDX = y' sign-extended, EAX = the sum).
5. Speed word 0: return (EAX = d). Otherwise q = d / 2400 (IDIV); q is
   negated (16-bit) when the speed is negative; s = q + speed (16-bit).
   Speed and s both positive, or both negative: speed = s; otherwise 0.

**Result.** EDX:EAX per path: f = 0 gives EBP-0Ch (a frame address) and the
low dword of y m3; speed 0 gives d and y' sign-extended; else the 16-bit sum
over the quotient's high word, and the IDIV remainder. All three registers
are dead in 416a04 except EAX's high word, which becomes the high word of
416274's flags slot (only bit 0 of that slot is read, at 4162d0 and
4165c4). The hook returns the exact pair, with its own scratch as the frame
address. Eight conditional sites, all with two feasible edges.

Compare mode is accepted: spans A+12h..A+33h (the matrix `euler_matrix`
builds and the flag A+32h; the prefix holds the angles and the position it
hands on) and the speed word; `terrain_face` and `euler_matrix` act on the
clone. The comparator checks the bytes and EDX (a value on every path) and
accepts different EAX values only when both lie in the thread's stack (the
face-0 path's frame address)
([compare_mode.md](../reference-game/compare_mode.md#spans-and-comparators)).

### 417a04 `random_damage`: random damage to the parts

cdecl `(V, amount, chance)`, both words. Callers 416a04 (amount 150, chance
120), 4179f2 and 41baab.

- amount = 0: return with the entry EAX/EDX.
- P = [V+20Eh] (29 part words, the pointer read once). For i = 0..28 with
  word P[i] != 0: r = 492ee0(4d2599h) with AND AX, FFh (`rng_next` on
  stream A; EAX keeps its high word and AL, AH = 0); r >= chance (signed
  words): next part. Else v = 492f24(4d2599h, amount) + (amount SAR 1)
  (16-bit; `rng_bounded`, its slot carries EAX's high word over amount, ECX
  = 4d2599h after the POP); t = 40dc58(V+206h, i) (the part's maximum,
  hook `part_group_max`); w = 47e0a0(v, t sign-extended) (`multiply_q8`);
  then the vehicle virtual V->vtable[+74h](V, i, w, 0) with EAX = w, EDX =
  the vtable, ECX = 47e0a0's ECX.
- EDX:EAX at return are the last instruction's registers (a callee's pair,
  the AH-cleared RNG value, or the entry pair). In 416a04 they flow into the
  entry registers of the following 4628e0 call; the hook returns the exact
  pair. Four conditional sites.

### 416a04 `vehicle_control`: speed, turn and the walk/stand clips

cdecl `(A, turn, speed, flags)`; `turn` and `speed` are words in the
function's own argument slots (their addresses go to 41693c and 416274),
`flags` is passed on to 416274 (only bit 0 is read there). One exit
(4174c1); 90 conditional sites. 20 call sites in 4160dc (player input),
41c418, 41daac, 41dd64, 41e224, 41e554, 41e570, 41e828, 41fac4, 41fb60 and
41fbb8 (AI behaviours).

**Speed command.**

1. Byte E+12h set (vehicle down): turn = speed = 0. Otherwise the dword
   timer A+26Dh steps (467aa8, `timer_dword_step`):
   - not expired: speed = word S+2 when byte E+1Ch (A+0AEh) is set, else
     word S+4;
   - expired: byte E+1Ch = 0, `slope_speed(A, &speed)`, then speed >= S+4
     gives S+4, else speed <= S+2 gives S+2 (S+4 is tested first).
2. A != [4a9b84] (not the player's vehicle) and byte E+12h = 0:
   `obstacle_steer(A, &turn, &speed, flags)`; the flags slot carries the
   current EAX's high word (dead, see above).
3. old = word A+28Eh (the current speed word). Byte E+17h or E+19h set:
   speed = 47e0b0(400, speed) (`multiply_q10`, 400/1024); else byte E+16h
   or E+18h set: speed = 47e0b0(750, speed).
4. D = [A+317h] nonzero with dword [D+81h] nonzero: r = sign-extended AX of
   `part_ratio(A+206h, byte [[D+18h]+17h] sign-extended + 19)` (the slot's
   high word is the high word of [D+18h]). r < 225: k = 1024 - 204 (r / 51)
   and, if word A+28Eh != 0, acc = 47e0b0(k, 1000); acc stays 0 otherwise.
   Then speed += AX of 47e0b0(acc, S+4) when A+28Eh > 0, else of
   47e0b0(acc, S+2) (signed words).
5. turn is clamped to -256..256 (signed); `approach_word(A+28Eh, speed,
   S+6)` (467adc) moves the speed word towards the command.

**Clips.** cur = word T+4 (current clip), next = T+8, W = T+16h (target
clip); |x| maps 8000h to 7FFFh. M = |A+28Eh| after step 5, Mold = |old|,
stand = cur == word S+7Ah, walk = cur == S+10h or cur == S+12h; W50 is a
frame flag, initially 0.

- M != 0, stand and not walk: turn = 0 and A+28Eh = 0 (M becomes 0).
- cur != next (a clip change is in progress): A+28Eh = old, go to the tail.
- Byte E+12h set and not stand (vehicle down): cur == S+44h ("fall"): rate
  word A+2A0h = 120; if byte E+22h = 0 and words T+6 == T+0Ah (the clip
  ended): byte E+22h = 1, `random_damage(A, 150, 120)`, sound 4628e0(29h,
  A+26h). Other clips: if W != S+44h: (cur == S+12h:
  `track_clip`(T, S+10h, 0, 0)), `track_target`(T, S+44h, -1, 0) and sound
  4628e0(1Eh, A+26h) when byte E+22h = 0; rate = 100.
- M > Mold (accelerating): walk: the speed word is clamped to S+2Ch
  (positive speed) or -S+2Ch (otherwise, a 32-bit compare) and the rate is
  max(speed, 60) or min(speed, -60); W50 = 1; if W != S+0Ch:
  `track_clip`(T, S+12h for a negative speed else S+10h, 0, 0) and
  `track_target`(T, S+0Ch, -1, 0). Not walk, cur == S+0Ch: rate = speed;
  W == S+10h or S+12h resets the track targets (words T+0Ch, +18h, +16h =
  FFFFh, T+50h = 1, T+1Ah = 0); speed > S+2Ch and S+0Eh != W:
  `track_target`(T, S+0Eh, -1, 0). Not walk, another clip: reset the
  targets, rate = speed.
- M < Mold (decelerating): cur == S+0Eh: M < S+2Ch sets the speed word to
  S+2Ch (old > 0) or S+6Ch and the rate to it, and targets S+0Ch unless W
  is S+0Ch; M >= S+2Ch resets the targets and rate = speed. cur == S+0Ch:
  M != 0: rate = speed, and W == S+0Eh resets the targets; M = 0: rate =
  0, and when not walk: `track_target`(T, S+10h if old > 0 else S+12h, -1,
  0) unless W already is that clip, rate = 100 or -100, W50 = 1.
- M == Mold: M != 0: rate = speed. M = 0: with |turn| > 50 and walk (not
  stand): unless W == S+7Ah, `track_clip`(T, S+12h for a negative turn else
  S+10h, 0, 0) and `track_target`(T, S+7Ah, -1, 0); rate = -100 or 100 by
  the turn's sign. With |turn| > 50 and stand: rate = AX of 47e0b0(350,
  turn). Otherwise, stand and not walk: unless W == S+10h
  `track_target`(T, S+10h, -1, 0); rate = 100 if the rate was positive,
  else -100. The second `test di, di` at 41729b repeats the test 417296
  just passed, so 41729e never jumps (4172df is unreachable).

**Tail (4172f8).** m = |A+28Eh|. 0 < m < 45: AX = 45 and, if the rate is
nonzero with |rate| < 60, rate = 60 or -60 by its sign. Turn scale u: W50
set or AX < 45: u = 0. Otherwise a = min(AX, S+4) - 45, R = S+4 - 45,
h = R SAR 1; h < a: u = S+0 - S+0 (a - h) / (2 (R - h)) (16-bit); else
u = S+0 (a + h') / (h' + h) with h' = 47e08d(h, 47e068(50, 50)) = h (two
Q16 helpers without hooks, computed in core with their exact registers).
Then u = 47e0b0(1600, u); w = 47e0a0(u, turn); `approach_word`(A+28Ch, w,
S+8) moves the turn rate; heading word A+10h += word A+28Ch; the dword
A+0Ch is rewritten unchanged; word A+32h = 0 (matrix invalid). A zero
divisor (S+4 = 45 or 46 with AX = 45) faults (#DE) in the original; that is
a precondition.

**Registers.** Every game call gets the EAX/EDX/ECX the original holds
there; the core follows each register through all paths, including the
16-bit loads that keep a high word, the `ADD EDX, FFFFFF38h`/`ADD ECX, C8h`
carries into the high word, SETcc/AND results and the POP ECX after
467aa8. The result is EAX = A+0Ch, EDX = word A+0Eh << 16 | the new heading
word, on every path. No local is read before it is written (the frame dword
EBP-58h is written twice and never read), so no entry shim.

**ECX liveness.** The hooks cannot return ECX. A bounded interprocedural
forward walk with byte-lane tracking
over a full listing from all 20 return sites of 416a04 finds ECX overwritten, or reaching only
argument slots that the callees read as words (`push ecx` after a 16-bit
load: 47967c, 41a550, 467924, 479344), `push ecx` local reservations
written before they are read (41c3c8, 41e8d4), and the virtual +54h at
41eac5 whose implementations in the known vtables (406868, 411a44, 4111e9,
40f520) do not read ECX. Open: the upper word passed through `pop ecx` in
41a550 is followed only to further word slots, and 40dbc0's ECX at its ten
call sites reaches 471b30's `push ecx`/`pop ecx` pair (returned unchanged
to that function's callers) and word argument slots, not followed further.
417a04's ECX is dead after 416e41 (4628e0 writes it first).

**Callees.** Through the guest bridge: 467aa8, 41693c, 416274, 47e0b0,
40dbc0, 467adc, 4792ac (`track_clip`), 47967c (`track_target`), 417a04,
47e0a0 and the sound 4628e0 (`sfx_play_at`) in 416a04; 46e498 and 47ebb8 in
41693c; 492ee0, 492f24, 40dc58, 47e0a0 and the virtual +74h in 417a04.
Computed in core: 47ff8c, 47dbca, 47e068, 47e08d.

### Validation of the vehicle control group

`vehicle_control_cases.h`: direct cases run the original body and the
replacement against logging stubs. The case generator never builds the
zero tail divisor (S+4 = 45 or 46 with AX = 45 becomes 47), which faults in
the original.

## Formation steering 41fbb8 `formation_steer`

Group `ai_motion` (with the AI flyer step `flyer_move` 4218c4,
[flyer.md](flyer.md)).

**Callers.** Two direct call sites, both `push A; call; pop
ecx` (ECX and the result pair are dead after them): 41d5d1 in `group_move`
41d598, taken when word A+23Eh is 0 and A is not the first member of its
group (the leader runs `leader_route` 41fb60 instead; a nonzero A+23Eh runs
`point_move` 41fac4 towards the point A+240h), and 41e467 in `hunt` 41e224.
So 41fbb8 is the **formation-keeping step of a group member**: it steers
towards its slot next to the group leader and matches the leader's heading
and speed.

**Contract.** `EDX:EAX 41fbb8(A)`, cdecl, one slot. Prologue
`55 8b ec 83 c4 cc 53 56 57 8b 5d 08` (locals 34h; relocate 6 = 1/2/3). No
x87 and no indirect jump except the virtual call. EAX, EDX and ECX are
written before they are read; no local is read before it is written. The
result is the EDX:EAX of the final `vehicle_control` call (every path ends
with exactly one). Frame locals: T = EBP−1Ch (3 dwords), R = EBP−28h (2
dwords), Q = EBP−34h (3 dwords), M = EBP−8 (4 words).

1. L = [[[A+45h]+0Ch]] (first member of A's group, the leader; may be A).
   T ← L's position (`L+26h`, `+2Ah`, `+2Eh`). The virtual `+78h` of A
   (`[[A]+78h](A, T)`, EAX = `L+2Eh`, EDX = T, ECX = A's table) moves T to
   A's formation slot ([below](#formation-slot-virtual-78h)).
2. d = `dist2d_point(A+26h, T)` (EAX = T, EDX = A+26h); b =
   `heading_point(T, A+26h)` (EAX = A+26h, EDX = T); AX −= word A+10h, so b
   is the bearing to the slot relative to A's heading.
3. **At the slot** (s32 d < 2000, JGE 41fc19): turn = sar(−s16(word A+10h −
   word L+10h), 6) (32-bit), `vehicle_control(A, turn, 0, 0)` with EAX =
   (b's high word : the heading difference), EDX = turn.
4. **Catch up or wait** (s32 d > 25000, JG 41fc44; or 2000 ≤ d ≤ 25000 and
   the leader's speed word `L+28Eh` + 19h, as an unsigned word, < 32h, i.e.
   the leader within −25…24, JE 41fc5e): speed = DX ← 100h (EDX's high word
   kept: `heading_point`'s EDX on the far path, 0 on the near one); if s32
   d < 25000 (JGE 41fc6a) speed = sar(d, 7) instead; turn = sar(s16(b), 6);
   `vehicle_control(A, turn, speed, 0)`.
5. **Keep station** (2000 ≤ d ≤ 25000, leader moving): s = word A+10h − word
   L+10h (in SI, ESI's high word from d), a = |s| as a word (8000h gives
   7FFFh with EAX's high word from b, JNE 41fc96; JGE 41fca1).
   - s16 a ≥ 2000h (JGE 41fcb0): EAX = (a's high word : FF00h), plus 200h as
     a 32-bit add unless s16 s > 0 (JG 41fd91; the add carries into the high
     word), `vehicle_control(A, EAX, FFFFFF00h, 0)`: turn in place towards
     the leader's heading, backing slowly.
   - otherwise Q ← A's position; if word `L+32h` = 0 (JNE 41fcd8):
     `euler_matrix(L+0Ch, L+12h)` (EAX = ECX = L+0Ch, EDX = L+12h), word
     L+32h ← 1. M ← words `L+12h..L+19h`; Q −= T (z, x, y); M's words 1 and
     2 are swapped (47df04); R = 47ffe8(Q, M) (x' = (x·M0 + y·M2 + 2000h) >>
     14, y' = (x·M1 + y·M3 + 2000h) >> 14, 64-bit products); so R is A's
     offset from the slot in the leader's frame (x', y' = x·W0 + y·W1,
     x·W2 + y·W3 with W = the words at L+12h). Then turn = −(sar(s16(s), 6)
     + sar(−R.x, 5)) and speed = word L+28Eh + sar(−R.y, 5), both as words
     with the high words of their 32-bit terms (EAX from sar(s16 s, 6), EDX
     from sar(−R.x, 5)), ECX = sar(−R.y, 5); `vehicle_control(A, turn,
     speed, 0)`.

47df04 (swap two words in place; returns EAX = its argument, ECX's low word
the old word 2) and 47ffe8 (returns EAX = R) are pure helpers whose result
registers are overwritten before the next use; the replacement computes
them. Writes of its own: T, Q, R, M (frame) and word L+32h; everything else
is written by the callees.

**Callees and modes.** `dist2d_point` 4928d0, `heading_point` 492934,
`euler_matrix` 47ebb8 and `vehicle_control` 416a04 are hooks, entered
through the weapons guest bridge with the original's registers; the virtual
`+78h` goes through the bridge too (observed as `slot_78`). No sound.
Compare is refused (game calls;
[compare_mode.md](../reference-game/compare_mode.md)). No entry shim.

**Branch sites (9).** 41fc19, 41fc44, 41fc5e, 41fc6a, 41fc96, 41fca1,
41fcb0, 41fcd8, 41fd91; all reachable both ways (41fc6a: taken on the far
path, and on the near path for d = 25000 exactly). In-vitro cases:
`ai_motion_cases.h`.

## Formation slot virtual +78h

The actor virtual `+78h` ([vtables.md](vtables.md#21-slot-matrix), row
`+78`) moves a point to the actor's formation slot: it adds the slot's
offset, rotated into the group leader's heading, to the point in place.
Three implementations, each with its own slot table and helper, share the
placement step `0x411d64`:

| Class | Function | Hook | Slot helper | Slot table |
|---|---|---|---|---|
| BASE, RADAR_BASE, GUN_BASE, VEHICLE, LC_BASE | `0x405c04` | `formation_base` | `0x405b9c` (static buffer `0x4975d6`) | `[0x4a95ac]`, 1Ch-byte records, each with a pointer at +4 to 10-byte entries |
| MECH | `0x417898` | `formation_mech` | `0x4205cc` (static buffer `0x49a430`) | `[0x4a9d6c]`, 1Ch-byte records of seven 4-byte entries |
| FLYER | `0x421e98` | `formation_flyer` | `0x423044` (pointer into the table) | `[0x4a9dfc]`, 12h-byte records of three 6-byte entries |

The tables are allocated at startup next to their record counts
(`0x4a95a8`, `0x4a9d68`, `0x4a9df8`; loaders around `0x40605c`, `0x41fe60`,
`0x422e3e`). The helpers and `0x411d64` have no direct callers outside
these three functions (listing scan), so they are computed inside the
hooks (405b9c is also a hook of its own, `formation_slot` in the group
`sim_leaves`). A is the actor, P the point (both cdecl slots), G =
`[A+45h]` the group, L = `[[G+0Ch]]` its first member (the leader, may be
A).

### 405c04 `formation_base(A, P)` (BASE classes)

Prologue `55 8b ec 53 56 8b 5d 08 66 8b 73 49` (relocate 5 = 1/2/1/1;
saves EBX, ESI, no locals).

1. SI ← word `A+49h` (the slot number; 0 = no slot). If 0 (JE 405c13):
   return with EAX, EDX and ECX as at entry.
2. EAX ← P, EDX ← G, CX ← word `G+2` (the formation type; ECX's high word
   from the caller); push P, ESI, ECX, A and call `0x405b9c` (cleans 0Ch;
   P stays on the stack as the third argument of the next call).
3. `0x411d64(A, Q, P)` with Q = 405b9c's EAX; returns its EAX/EDX/ECX.

**405b9c(A, type, slot)** (`55 8b ec 80 3d e2 75 49 00 00`, no frame
locals, A unread): if byte `0x4975e2` = 0 (JNE 405ba6): the dwords
`0x4975d6`, `0x4975da`, `0x4975de` ← 0 and the byte is incremented. Then
with t = s16(type), s = s16(slot) (32-bit wraparound arithmetic) the row
R = `[[0x4a95ac] + 28·t + 4]` and the entry E = R + 10·s − 10; dword
`0x4975d6` ← `[E]`, dword `0x4975da` ← `[E+4]` (read, store, read, store);
the dword `0x4975de` (z) is never written after the first call. Returns
EAX = `0x4975d6`, EDX = `[E]`, ECX = 10·s.

**Trap: the returned pointer aliases between calls.** Every call
overwrites x and y of the one static buffer and returns the same address;
405c04 uses it at once (411d64 reads z first, then x and y through
`rotate_2d`).

### 417898 `formation_mech(A, P)` and 421e98 `formation_flyer(A, P)`

Both: prologue `55 8b ec 83 c4 f4 53 8b 5d 08 66 8b 43 49` (relocate 6 =
1/2/3; locals Q = EBP−0Ch, 3 dwords; saves EBX).

1. AX ← word `A+49h` (EAX's high word stays). If 0 (JE 4178a9 / 421ea9):
   return; EAX = entry EAX with the low word 0, EDX and ECX as at entry.
2. Push EAX (slot), EDX ← G, CX ← word `G+2` (type), push ECX, call the
   helper (MECH `0x4205cc`, FLYER `0x423044`) with H = its EAX.
3. Q.z ← s16 word `H+4` (EDX), Q.x ← s16 word `H` (ECX), EDX ← Q,
   Q.y ← s16 word `H+2` (EAX), EAX ← P (this read order: +4, +0, +2).
4. `0x411d64(A, Q, P)`; returns its EAX/EDX/ECX.

**4205cc(type, slot)** (`55 8b ec 80 3d 36 a4 49 00 00`): if byte
`0x49a436` = 0 (JNE 4205d6): the words `0x49a430`, `0x49a432`, `0x49a434`
← 0, the byte is incremented. E = 28·t + `[0x4a9d6c]` + 4·s − 4; word
`0x49a430` ← word `[E]` (via DX), word `0x49a432` ← word `[E+2]` (via
AX); the word `0x49a434` (z) is never written after the first call.
Returns EAX = `0x49a430`, EDX = (s16(t) high word : word `[E]`), ECX =
4·s.

**423044(type, slot)** (`55 8b ec 0f bf 45 08`, no memory write):
returns EAX = 18·t + `[0x4a9dfc]` + 6·s − 6 (a pointer into the table:
x, y, z words), EDX = s, ECX = 6·s.

### 411d64 placement `(A, Q, P)`

Frame locals: R = EBP−10h (2 dwords), z = EBP−8, EBP−4 (a copy of the
angle pointer on the cache path only); saves EBX, ESI, EDI.

1. z ← `[Q+8]` (read first), then G ← `[A+45h]` (reread here), ECX ←
   `[G+0Ch]`, EBX ← `[ECX]` + 0Ch (the leader's angles; L+12h is its
   cached heading matrix, word L+32h the cache flag).
2. If word L+32h = 0 (JNE 411d8c): `euler_matrix(L+0Ch, L+12h)` (47ebb8;
   EAX = L+12h, EDX = L+0Ch, ECX = `[G+0Ch]`), then word L+32h ← 1.
3. `rotate_2d(Q, L+12h, R)` (47ffe8; EAX = L+12h, EDX = Q, ECX = R):
   R.x, R.y = the Q14-rounded 2 × 2 rotation of Q.x, Q.y by the words
   L+12h..L+19h (only those two dwords written).
4. `[P+8]` += z (EAX = z), `[P]` += R.x (EDX), `[P+4]` += R.y (ECX), in
   this order (read-modify-write each). Returns EAX = z, EDX = R.x, ECX =
   R.y.

**Results and liveness.** The hooks return the exact EDX:EAX above
(entry registers on the slot-0 paths, from the register context). ECX
and the flags are dead at all seven `call *78h(...)` sites (`0x405d5d`,
`0x417b76`, `0x41fbe7`, `0x421f84`, `0x4225dd`, `0x423862`, `0x46aa90`,
the only indirect calls through +78h in the listing): each is followed by
`add esp,8` and every register is written before it is read, including
in the next callees there (`0x46b910`, `0x4928d0`, `0x49288c`, which
write EAX/ECX before reading them); at `0x46aa90` EAX is rebuilt from EDI
and EDX's low word is set before any compare, its full value before any
further read. EAX and EDX are therefore dead too, but compared in vitro.

**Writes.** The two static buffers and their first-call bytes (BASE,
MECH), the cache flag word L+32h (and the leader's matrix through
`euler_matrix`), the three dwords of P; frame locals. No sound, no clock,
no RNG, no runtime call; reads no uninitialized local and no entry
register except to pass it through (no entry shim). The static buffers
are not referenced by any other instruction (byte search of the file for
the addresses `0x4975d6..e2` and `0x49a430..36`: all hits are inside
405b9c and 4205cc); the table pointers are read only by the loaders, the
helpers and, for `0x4a95ac`, the placement 405c3c.

**Callers and receivers.** `formation_steer` 41fbb8 (41fbe7, the AI
formation path), the convoy follower 46aa60 (46aa90), group activation
4236c4 (423862, placement modes 4/5), the group placement 405c3c (405d5d,
which also writes A+45h and A+49h), MECH 417a04 (417b76), FLYER 421ee8
(421f84) and 4224c4 (4225dd). DBSIM map entry probes (a60/c1s/c1c/c3_09/
c4_08/c3_10): 405c04 and 405b9c 1000/1000/1000/0/0/0; 417898 and 4205cc
1000/1000/1000/1000/0/1000; 411d64 1000/1000/1000/1000/0/1000; 421e98 and
423044 not probed (their caller 4224c4 638/638/638/0/0/0, the flyer
suites).

### Implementation

`euler_matrix` and `rotate_2d` are entered through the weapons guest bridge
with the registers listed above (both are hooks and keep their own modes);
the helpers and 411d64 are computed and reported to the callee observer as
`call_405b9c`, `call_4205cc`, `call_423044` and `call_411d64` without a
game call. Q (MECH, FLYER) and R live in the adapter's scratch, not on the
game stack; the static buffers are the game's (guest memory at their image
addresses). Every guest read and write is in the original's order.
Compare mode is accepted: a 12-byte span on P, an RVA region for the
class's static buffer (BASE 4975d6..4975e2, MECH 49a430..49a436) and the
default comparison; the leader's lazy matrix cache is filled by the
original phase, so the replacement phase finds it set
([compare_mode.md](../reference-game/compare_mode.md#spans-and-comparators)).
In-vitro cases: `formation_cases.h`.

## AI behaviours around the torso servos

Nine hooks of the group `ai_behaviour` (core `core/src/ai_behaviour.c`,
`es2/core/ai_behaviour.h`; adapter `proxy/ai_behaviour_hooks.h`), around
`torso_aim` 41ef14 and `torso_center` 41e8d4 ([input.md](input.md)).
Notation as above; W = a weapon object (vtable `[W]`, definition
`[W+20h]`, carrier record `[W+1Ch]`); P the weapon system `[A+202h]`
(`[P+4]` slot list, word P+8 slot count). Every callee gets exactly the
registers listed, so nested hooks see the original's registers.

| Hook | VA | Role (hypothesis) | Callers | Map probes (a60/c1s/c1c/c3_09/c4_08/c3_10) |
|---|---|---|---|---|
| `weapon_range` | 40e5f8 | is distance d inside the weapon's range window? | 410970, 41f358, 41f5a0 | 904/1000/1000/1000/1000/1000 |
| `weapon_choose` | 41f358 | best weapon for target T at distance d | 41f5a0 | 518/1000/1000/1000/1000/383 |
| `aim_fire` | 41f5a0 | aim the torso at a point (with lead and spread) and fire the weapon | 41e984, 41ea7c (41eb23) | 518/1000/1000/1000/1000/383 |
| `aim_target` | 41e984 | aim point on T (part or centre) inside the turret cone, then `aim_fire` | 41ea7c | 370/1000/1000/1000/1000/0 |
| `target_search` | 41e0e0 | best target from the MECH list | 41e224, 41f7b8 | 0/0/0/0/12/0 |
| `hunt` | 41e224 | script step: pick a target and start the attack (or retreat) script, else move | script tables (499b14) | 0/0/0/0/1000/1000 |
| `line_blocked` | 41dc24 | is the line of fire to T blocked (object 1, terrain 1/2)? | 41dd64 | 0/9/0/232/1/0 |
| `clear_shot` | 41dd64 | script step: circle the remembered target point until the line of fire is clear | script tables (499af0) | 0/521/0/1000/53/0 |
| `group_move` | 41d598 | group movement step: own point, group leader or formation slot, then the torso | 41d5fc (thunk), 41d60c, 41d7d0 | 1000/1000/1000/1000/63/1000 |

41dd64 and 41e224 occur once each in DATA, as the member-function word of
script tables (`0x499af0`, `0x499b14`, 12-byte Borland member pointers);
the script runners 415b38/415afc/415b74 call them as `fn(A + adjust)` and
pop ECX (mech_order.md), so their EDX:EAX goes back to the runner and ECX
is dead there.

### 40e5f8 `weapon_range(W, d)`

cdecl, two slots, leaf (14 instructions). EDX = d, EAX = W, ECX = C =
`[W+1Ch]`; EAX = 1 when s32 d > `[C+2Ch]` (JLE 40e607) and s32 d <
`[C+30h]` (JL 40e60c), else 0. Result EDX:EAX = d : 0/1; ECX = C. Both
branch sites have two feasible edges.

**ECX at return.** After 41f440 (in 41f358) ECX reaches the weapon
virtual +2Ch at 41f49f when the weapon's state word is 3 or 5 (the path
41f476/41f47c skips the `movsx ecx` at 41f47e); 410970 is not followed. Rather than prove every weapon's +2Ch independent of ECX, the
hook has an **entry shim** that loads ECX = `[[ESP+4]+1Ch]` before the
framework's dispatch, so the replaced call returns the original's ECX
exactly. The original reads no entry register, so the shim changes nothing
in original or compare mode.

### 41f358 `weapon_choose(A, angle, d, T)`

cdecl, four slots (angle is used as a word). Frame locals: best score
word EBP−2, the T+34h result word EBP−4, the chosen weapon EBP−8, the
slot count EBP−0Ah, "nothing ready" word EBP−0Ch (1), the random bound
EBP−0Eh, the index EBP−10h.

1. best = AX of `scalar_interpolate`(sx(A+2AAh), 0, 400h, 96h, −64h)
   with the entry EAX/EDX/ECX.
2. f = 1 if lo(angle) + 4000h (16-bit) < 8000h (unsigned), else 0 (T in
   the front half). t34 = AX of T's virtual +34h (T, f); regs EAX =
   lo(previous EAX, angle + 4000h), EDX = f, ECX = T's vtable.
3. count = word P+8 (ECX keeps +34h's high word); EAX = `[A+45h]`; ECX =
   1 when `[A+45h]` = `[[4a9b84]+45h]` (A in the player's group; written
   and never used, but it is the ECX of a later call); bound = 23h, or
   1Eh when word T+1A8h ≠ 0 (EAX's high word stays `[A+45h]`'s); EDX =
   lo(`[4a9b84]`, 0).
4. For i = 0 while s16 i < s16 count, W = `[[P+4] + 4i]` (P read again
   each time); W = 0: next.
   - W.+5Ch(W) (regs `[W]`, `[P+4]`, sx(i)); AX ≠ 0: next (ECX = W after
     POP ECX).
   - s = W.+60h(W, 0) (EAX = +5Ch's EAX, EDX = `[W]`, ECX = W); nothing
     ready = 0.
   - k = AX of `weapon_range`(W, d) (EAX = d, EDX/ECX = +60h's).
   - SI = 0 and k ≠ 0 and byte A+96h = 0 and word A+26Bh = 0: byte A+96h
     = 1 (EDX = A+92h from 41f452 on).
   - SI ∉ {3, 5}: ECX = sx(s), EDX = P (read again); word
     `[P + 2·sx(s) + 0Ah]` = 0: next. k = 0: next.
   - W.+2Ch(W) (EAX = `[W]`, EDX/ECX as left); AX = 0: next.
   - e = sx(word D+6) with D = `[W+20h]` when t34 = 0; otherwise e =
     sx(D+4), EDX = sx(t34), and e > EDX gives e = sx(D+6) + 10000.
   - X = `multiply_q10`(100, e) (EAX = e); Y = `multiply_q10`(1000,
     sx(word `[W+1Ch]`+34h)) (EAX = ECX = that word, EDX = `[W+1Ch]`); Z =
     X − Y.
   - r1 = sx(AX of `rng_bounded`(4d2599h, lo(Y's ECX, bound))) (EAX = Y,
     EDX = Z); r2 = sx(AX of `rng_bounded`(4d2599h, lo(r1's EAX, bound)));
     score = Z + r1·r2 (32-bit); EDX = sx(best).
   - s32 score > EDX: best = low word of score, chosen = W.
   - i += 1 (word), ECX = lo(ECX, i).
5. Nothing ready (no weapon passed +5Ch) and byte A+0A5h = 0: byte A+0A5h
   = 1; if `[A+1B6h]` ≠ 0, `trigger_fire`([A+1B6h]) with EAX = that value
   (EDX/ECX as the loop left them; ECX = the value after POP ECX); then
   word `[4a9e63]` = max(word, 1000) (signed).
6. Result EAX = chosen (0 if none), EDX as left by the last instruction.

Callees: `scalar_interpolate` 47df48, `weapon_range` 40e5f8,
`multiply_q10` 47e0b0, `rng_bounded` 492f24 (stream A at 4d2599h),
`trigger_fire` 423430 (hooks), and the virtuals T.+34h, W.+5Ch, W.+60h,
W.+2Ch (original). 21 branch sites, all with two feasible edges.

### 41f5a0 `aim_fire(A, P, w, T)`

cdecl, four slots (w used as a word). Frame: L = EBP−10h (a copy of the
point P, 3 dwords), k = word EBP−2.

1. d = `dist2d_point`(A+26h, P) (EAX = A+26h, EDX/ECX = entry). L ← P.
2. C = `[A+2ACh]` (the weapon kept from the last shot). C ≠ 0 and
   C.+2Ch(C) AX ≠ 0 (regs C, `[C]`, `[P+4]`) and AX of
   `weapon_range`(`[A+2ACh]`, d) ≠ 0 (EAX/EDX = +2Ch's, ECX = C): W =
   `[A+2ACh]`.
3. Otherwise byte A+0B5h ≠ 0: W = 0, the byte is cleared (EAX = EDX =
   A+92h). Else W = `weapon_choose`(A, lo(ECX, w), d, T) with EAX = A+92h,
   EDX = T; dword A+2ACh = 0, EAX = 0.
4. W = 0: return `torso_aim`(A, L) (ECX = L) and its EDX:EAX.
5. k = word `[W+20h]`+0Ah (ECX keeps its high word); v = T.+38h(T) (EAX =
   T, EDX = `[T]`); ECX = v. s16 k > 0 and v's low word ≠ 0: lead q =
   low word of ((s32)(sx(sar(d, 2)) · 4·sx(v), low 32 bits) / s16 k), as
   IMUL, CDQ, IDIV; `point_offset`(L, lo(sx(q), word T+10h), sx(q)) with
   EDX = L, ECX = T.
6. Byte `[[A+45h]+12h]` ≠ 0 and the spread word z =
   `[49a30c + 2·sx(word [4a9e5c])]` ≠ 0: three `rng_bounded`(4d2599h,
   lo(d, z)) draws are added (sign-extended) to L.x, L.y, L.z in turn.
   Regs: first EAX = sx(word 4a9e5c), ECX = `[A+45h]`; second EAX = the
   first's sign-extended AX; third EDX = the second's sign-extended AX.
7. V = `torso_aim`(A, L) (EAX = L); m = W.+60h(W, 0) (EAX = `[W]`).
   - AX of m = 5 (EAX = 0): fire only when word V+0 + 3E8h and word V+4 +
     3E8h are both below 7D0h (unsigned, 16-bit); otherwise return with
     EAX = 0, EDX = lo(m's EDX, V+0 + 3E8h) (first test) or EAX = word V+4
     + 3E8h, EDX = 0 (second test).
   - otherwise EAX = 1 and fire.
8. Fire: W.+28h(W, A, 0) (ECX = `[W]`; EAX = 1 or the second test's
   word, EDX = m's EDX or 1). EAX = lo(`[W+1Ch]`, word `[[W+1Ch]+56h]`);
   EDX = 1 when that word is 6 or 16h, and then dword A+2ACh = W (keep
   the weapon), else EDX = 0. Result EDX:EAX as left.

Callees: `dist2d_point` 4928d0, `weapon_range`, `weapon_choose`,
`point_offset` 4929fc, `rng_bounded`, `torso_aim` 41ef14 (hooks); the
virtuals C/W.+2Ch, T.+38h, W.+60h, W.+28h (original). 15 branch sites,
all with two feasible edges. No division fault: k > 0 is tested first.

### 41e984 `aim_target(A, w, T)`

cdecl, three slots (w used as a word). Frame: the aim point Q = EBP−1Ch
(3 dwords).

1. c = word `[[A+1F2h]+22h]` (the turret half-cone). b = AX of
   `heading_point`(T+26h, A+26h) (EAX = EDX = T+26h, ECX = A+26h) minus
   word A+10h. Inside: lo(b + c) < lo(2c) (unsigned, 16-bit; DI keeps the
   entry EDI's high word, which never leaves the function).
2. Outside: `torso_center`(A, 0) with EAX = lo(…, b), EDX = lo(heading's
   EDX, b + c), ECX = 0; return its EDX:EAX.
3. d = `dist_gate`(A+26h, T+26h) (EAX = A+26h, EDX = lo(…, b + c), ECX =
   1). p = word A+2A2h (the part `part_pick` chose; EDX = lo(dist_gate's
   EDX, p)). s16 p ≥ 0 and d − 5000 < 15000 (unsigned): T.+58h(T, EDX, Q)
   writes Q (EAX = d − 5000, EDX = `[T]`, ECX = Q; the vehicle's slot +58h
   is `part_position`).
4. Otherwise the centre: word T+32h = 0 runs `euler_matrix`(T+0Ch, T+12h)
   (EAX = T+12h, EDX = T+0Ch) and sets the word to 1; R = T.+24h(T) (EAX =
   `[T]`; ECX = T after POP ECX); `transform_point`(R+14h, T+12h, Q) (EAX =
   R+14h, EDX = Q, ECX = T).
5. `aim_fire`(A, Q, lo(ECX, w), T) with EAX = Q; its EDX:EAX. The slot's
   high word is the ECX that +58h or `transform_point` returned (read as a
   word only downstream).

Callees: `heading_point` 492934, `torso_center` 41e8d4, `dist_gate`
49288c, `euler_matrix` 47ebb8, `transform_point` 48043c, `aim_fire`
(hooks); T.+58h, T.+24h (original). 4 branch sites, two edges each.

### 41e0e0 `target_search(A, Pt)`

cdecl, two slots; Pt points to a 2D point (A's destination). Range R =
90000 when dword `[O+0Eh]` ≠ 0 for the current order O = `[G+44h +
4·[G+6Ch]]` of A's group G = `[A+45h]`, else 980000. Word A+5Bh = 10000.
Walk the MECH list (`471c70`([4a9b7a], cursor), the head re-read each
step; first call EDX = A+5Ah, ECX = R); for each entry E:

- `target_valid`(A, E, 0) AX = 0: next (EAX = E).
- byte E+0A4h ≠ 0: next. cls = 0 when byte E+0A5h or E+99h is set, else 1
  (EDX = 1/0 then ECX = cls). The re-test of byte E+0A4h at 41e16d
  repeats 41e151 with no write in between, so 41e171 never jumps
  (PROVED).
- cls < best class (s16): next. d = `dist2d_point`(E+26h, Pt) (EAX = EDX
  = Pt, ECX = E+26h); x = R − d ≤ 0 (signed): next. Word E+1A2h (its
  attacker count) ≠ 0: x = x / (sx(E+1A2h) + 1) (IDIV; EAX = ECX = x, EDX
  = the remainder). A count of FFFFh divides by 0 and faults in the
  original (precondition).
- x > best score (s32), or cls > best class: best = E with x and cls.

Result EAX = best E or 0, EDX = the last list step's EDX. 13 branch
sites, one PROVED one-way (41e171), the rest both ways.

### 41e224 `hunt(A)`

cdecl, one slot (script step). D = `actor_destination`(A) with the entry
registers. Then:

1. T ≠ 0: T's word +1A2h −= 1, `[A+1A4h]` = 0, byte A+9Dh = 1 (the test
   at 41e251 repeats 41e248 and never jumps; ECX = 0 at 41e26f, so 41e271
   always jumps: both PROVED).
2. F = 0 when L = `[4a9b84]` (A in the player's group) and word A+23Eh =
   0, else 1. F = 0 and byte A+0B6h = 0: no search.
3. Search: `timer_word_step`(B) (EAX = `[A+45h]`, or A+92h when F = 0;
   EDX = B; ECX = L); running (AX ≠ 0): none. Expired: N = `[A+248h]`
   when word A+23Eh = 4 and that dword ≠ 0, else N =
   `target_search`(A, D) (EAX/EDX = the timer's, ECX = 1).
4. N ≠ 0: when N ≠ T, T's count −1 (if T ≠ 0), `[A+1A4h]` = N, N's count
   +1 (N ≠ 0, so 41e327 never jumps: PROVED), byte A+9Dh = 1. A out
   (byte A+0A5h, A+0A4h or A+99h set): `script_start`(A+4Dh, 499800h)
   (EAX = A+92h or 1, EDX = 1, ECX = A+4Dh), word A+2A2h = FFFFh, return.
   Otherwise `script_start`(A+4Dh, 499784h) (EAX = A+4Dh, EDX = 0, ECX as
   left: 1, the old target or `target_search`'s ECX), `part_pick`(A), and
   byte A+96h = 1 unless byte L+0A3h ≠ 0 and byte A+0B2h = 0; EDX =
   `[A+45h]`.
5. N = 0 and F = 1: dist = `dist2d_point`(A+26h, D) (EDX = A+26h, EAX/ECX
   as the path left them), `torso_center`(A, 0), 41c3c8(A); dist > 30000:
   41fac4(A, D) (go to the point); else a = AX of `heading_point`(D,
   A+26h) − word A+10h + 8000h, speed = lo(heading's EDX, 100h), plus
   FFFFFF00h (32-bit, so the low word becomes 0 and the high word stays)
   when dist ≥ 10000, `vehicle_control`(A, sar(sx(a), 6), speed, 0).
6. N = 0 and F = 0: `formation_steer`(A), 41c3c8(A), `torso_center`(A, 0).
7. EAX = 0; EDX = the last callee's EDX (`[A+45h]` after `part_pick`).

Callees: `actor_destination` 41dbcc, `timer_word_step` 467a48,
`target_search`, `script_start` 413e50, `part_pick` 41ce08,
`dist2d_point`, `torso_center`, `heading_point`, `vehicle_control` 416a04,
`formation_steer` 41fbb8, `fire_permit` 41c3c8 and `point_move` 41fac4
(hooks). 24 branch sites, three PROVED one-way (41e258, 41e271,
41e327).

### 41dc24 `line_blocked(A)`

cdecl, one slot. Frame (EBP−2Ch): out O (12 bytes, for `segment`), end E =
EBP−20h, start S = EBP−14h, hit H = EBP−8 (distance, object).

1. S ← A+26h, E ← T+26h. R1 = A.+24h(A) (EAX = `[T+2Eh]`, EDX = `[A]`,
   ECX = `[T+2Ah]`); R2 = T'.+24h(T') with T' = `[A+1A4h]` read again
   (EAX = T', EDX = `[T']`, ECX = A). S.z += `[R1+1Ch]` (500 when R1 = 0),
   E.z += `[R2+1Ch]` (500 when R2 = 0): the eye and target heights.
2. g = AL of `segment`([4a0be0], S, E, 0, O, 1) (EAX = S, EDX = the map,
   ECX = E).
3. `object_line`(H, S, E, 0, −1) (EAX = E, EDX = S, ECX = H); AX ≠ 0 and
   `[H+4]` ≠ (T ≠ 0 ? T+0Ch : 0), with T read again: return AX = 1 (EAX's
   high word from T + 0Ch, EDX = object_line's).
4. `terrain_line`(H, S, E, 0) (EAX = H, EDX = E, ECX = S): AX ≠ 0 returns
   lo(EAX, 1) when g ≠ 0, else lo(EAX, 1) + 1 (INC EAX); AX = 0 returns
   EAX = 0. EDX = terrain_line's.

`[H+4]` is read only after AX ≠ 0, which implies `object_line`'s out
write (above). The test at 41dce0 (T = 0) is reachable only if a callee
clears `[A+1A4h]` (the second virtual call already dereferences T'); the
replacement re-reads it as the original does. 7 branch sites, all both
ways in vitro.

### 41dd64 `clear_shot(A)`

cdecl, one slot (script step). Frame: Q = EBP−0Ch, a copy of the point
A+31Eh (41dd2c stores T's position there and sets byte A+0ADh).

1. h = `heading_point`(A+26h, Q) (EAX = `[A+326h]`, EDX = Q, ECX = A+26h).
2. Word B+4 = 0 (first run): byte B+6 = (AX of `heading_point`(Q, A+26h)
   (EAX = A+26h, EDX = Q) − word A+10h < 0, signed), word B+4 = 1, dword
   B+9 = 5000, byte B+7 = 1.
3. `timer_dword_step`(B+8) (EAX = B+8); EDX = 1 when it returns EAX = 0
   (32-bit test), else 0; ECX = B+8. Expired: dword B+9 = 5000, byte B+7
   = AL of `line_blocked`(A) (EAX = 0, EDX = 1, ECX = B+8). Clear (0):
   `script_start`(A+4Dh, `[B]`) (EAX = `[B]`, EDX = A+4Dh, ECX = A; the
   behaviour resumes the script saved at B) and byte A+0ADh = 0. Blocked:
   dword B+0Dh = `dist2d_point`(A+26h, Q) (EDX = Q, ECX = A+26h).
4. Byte B+7 = 1 (read again): `point_offset`(Q, lo(h, h + δ), `[B+0Dh]`)
   with δ = lo(EDX, C000h), plus 8000h as a 32-bit add when byte B+6 = 0
   (so DX = 4000h); EAX = `[B+0Dh]`, EDX = δ, ECX = Q: the point a quarter
   turn round Q.
5. t = AX of `heading_point`(Q, A+26h) − word A+10h; `vehicle_control`(A,
   sar(sx(t), 6), 100h, 0) (EAX = that, EDX/ECX = heading_point's);
   `torso_center`(A, 0). EAX = 0, EDX = torso_center's.

Callees: `heading_point`, `timer_dword_step` 467aa8, `line_blocked`,
`script_start`, `dist2d_point`, `point_offset`, `vehicle_control`,
`torso_center` (all hooks). 5 branch sites, two edges each.

### 41d598 `group_move(A)`

cdecl, one slot. EDX = `[[A+45h]+0Ch]`, EAX = L. Word A+23Eh ≠ 0:
`point_move`(A, A+240h) (EDX = A+240h, ECX = the entry ECX); else A = L:
41fb60(A) (`leader_route`); else `formation_steer`(A); ECX = A
after the POP on the last two. Then `fire_permit`(A) and `torso_center`(A, 0)
(ECX = A); the result is torso_center's EDX:EAX. 2 branch sites.

### Shared properties

- **Entry registers.** Only `weapon_choose` (scalar_interpolate's
  registers), `hunt` (actor_destination's) and `group_move` (ECX into its
  first callee) pass entry registers on; the adapters forward EAX/EDX/ECX
  as received. No hook reads a callee-saved register of its caller (41e984
  writes DI over the entry EDI; the high word never leaves). No frame
  local is read before it is written. Entry shims: `weapon_range`'s ECX
  loader, and the stack shims of `aim_fire`
  ([Stack residue under aim_fire](#stack-residue-under-aim_fire)) and
  `weapon_choose`
  ([Residue shims](#residue-shims-for-torso_aim-and-weapon_choose)) for
  their callees' stack residue.
- **ECX after return.** The hooks return EDX:EAX; ECX after a replaced
  hook is its entry ECX (`weapon_range`: the shim's value). Callers:
  41d598's three pop ECX; 41dd64/41e224 return to the script runners, which
  pop ECX; 41dc24's caller pops ECX; after 41e0e0 (41e2f0) ECX is
  overwritten (41e312, 41e383) or reaches only the entries of
  `script_start` (41e3a6), `dist2d_point` (41e3f4; 4928d0 calls 49545b,
  which returns its slot, then writes ECX) and `formation_steer` (41e467),
  none of which reads its entry ECX; after 41f927 (41f7b8) it is
  overwritten (41f964, 41f97b) or reaches `script_start` (41f9a5) or
  `attack_target` 41c0f4 (41f9bb), which load their registers from the
  slots first (mech_order.md). After 41f358, 41f5a0 keeps CX = word
  `[W+20h]`+0Ah, so 41f358's high word reaches the target virtual +38h at
  41f654 as ECX; none of its implementations reads it (vehicle 415498 and
  flyer 421584 pass it only into `multiply_q10` 47e0b0, which reads its
  slots; the building tables 497784/497818/4978ac/497940/4979d4 have
  411a54 = `xor eax, eax`), and the POP ECX at 41f65c ends it. After
  41e984 and 41f5a0 the callers return at once (41ea71 → 41ea74, 41eb23),
  as after `torso_aim` (input.md).
- **Registers between callees.** The replacement hands every callee the
  registers the bridge returned from the previous one, so a nested hook's
  own ECX difference (its entry ECX when replaced) travels exactly as in
  the original: for example the ECX of `euler_matrix` or `dist_gate` into
  T.+24h at 41ea4c (the vehicle's 417b98 keeps its high word in a slot of
  its model virtual +20h) or the high word of `aim_fire`'s word slot.
  These hooks add no ECX difference of their own beyond the returns above.
- **Divisions.** `target_search` divides by sx(count) + 1; a count of FFFFh
  faults in the original. The counts are attacker counts that `hunt`,
  41f7b8, 41c0f4 and 41c418 keep consistent (each +1 when an actor
  targets E, −1 when it drops E), so a target's count is at least 1 while
  someone targets it; the in-vitro real pass builds consistent counts.
  `aim_fire` divides by k > 0 only.
- **Out parameters.** `aim_target` hands Q to `aim_fire` after T.+58h
  (`part_position`, which writes all 12 bytes on every path, vehicle.md)
  or `transform_point` wrote it; `line_blocked` reads `[H+4]` only after
  `object_line` reported a hit.
- **Draw, sound, RTL.** None: all nine are simulation; every game call is
  a hook or an actor/weapon virtual behind the guest bridge.

### Validation of the torso-servo behaviours

`tools/reference-game/invitro/ai_behaviour_cases.h`, group
`ai_behaviour` (`differential_harness <hook>`):

- 1,001,024 direct cases per hook (quick tier 100,000) against logging
  stubs for every direct game callee of the hook under test: ordered
  slots, entry EAX/EDX/ECX (stack addresses masked), frame points by
  content (S/E of the terrain and object walks, Q, L and the points of
  `heading_point`, `dist2d_point`, `dist_gate`, `point_offset`,
  `torso_aim`, `aim_fire` and slot +78h). The actor and weapon virtuals
  (+24h, +28h, +2Ch, +34h, +38h, +58h, +5Ch, +60h, +78h) go through the
  arena vtable. Stubs return planned values where a comparison follows
  (cone edges ±c, ±c−1 around the heading; distances 10000, 30000 and the
  search ranges 90000/980000 ± 1; the part window 5000/20000 ± 1; AL/AX
  zero or not; weapon states 0…15 with 3 and 5; aim words on the ±1000
  edges; scores near the running best) and change what the originals
  reread (the target pointer A+1A4h, the heading, the kept weapon A+2ACh,
  the order state and target, the part word, the flag bytes, the
  behaviour state B+7, the type word of the weapon carrier, the trigger
  word 4a9e63, E's flags and count, the list head). Compared: the pair,
  the arena (six actors with list node headers, two groups, orders,
  configurations, the weapon system with six weapons, carriers and
  definitions, points, the model record, script tables), the globals
  4a9b84, 4a9b7a, 4a0be0, 4a9e5c, 4a9e63, RNG stream A, the time-scale and
  stagger words, and the callee counters in the first 4,096 cases.
- 256 installed 31/a7 poison cases per hook (fast and preserving
  dispatch, live x87 stack); `weapon_range` also compares ECX, which its
  entry shim makes exact.
- 3,072 real-callee cases for seven hooks, nested hooks in rotating
  modes and the hook itself original or replaced: `weapon_choose`
  (scalar_interpolate, weapon_range, multiply_q10, rng_bounded),
  `aim_fire` (dist2d_point, weapon_range, weapon_choose, point_offset,
  rng_bounded, multiply_q10), `aim_target` (heading_point, dist_gate,
  euler_matrix, transform_point, aim_fire, weapon_choose, weapon_range),
  `target_search` (dist2d_point over the real list step 471c70), `hunt`
  (timer_word_step, target_search, script_start, dist2d_point,
  heading_point; 41c3c8 real), `clear_shot` (heading_point,
  timer_dword_step, line_blocked, script_start, dist2d_point,
  point_offset) and `group_move` (formation_steer with dist2d_point,
  heading_point, euler_matrix; 41c3c8 real). Stubs that stay: torso_center,
  torso_aim, trigger_fire, vehicle_control, actor_destination,
  target_valid, part_pick, 41fac4, 41fb60, the terrain and object walks
  under line_blocked, and the virtuals. `line_blocked` (its callees are
  the terrain and object walks, validated in their own groups) and the
  leaf `weapon_range` have no real pass.
- Coverage: 93 branch sites, 89 both ways in the instrumented prefix,
  four PROVED one-way (41e171, 41e258, 41e271, 41e327). 41e30c/41e310
  (the new target equals the old one) are reached both ways only because
  the stubs may set A+1A4h again; in the game nothing between 41e248 and
  41e304 writes it, so the old target is always 0 there.

Mutation witnesses ([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)):
ten, at least one per hook.

## Stack residue under aim_fire

The weapon bodies that `aim_fire`'s W.+28h enters read stack bytes that no
instruction of theirs wrote, at positions fixed relative to `torso_aim`'s
entry. `aim_fire` therefore has an entry shim that runs its callees at the
original stack coordinate with the original's registers.

**Symptom.** With all hooks replaced, c3_09-loadout-b differed from frame
1671 (both RNG streams, then AI fire decisions, later the player position)
while the other suites were identical. It was identical with `aim_fire`
original, and with `aim_fire` replaced but `torso_aim` (group
`input_dispatch`) original; it differed with both replaced, whatever
`weapon_range`, `weapon_choose` or `point_offset` did. The shim below
removed the difference
([own_process.md](../reference-game/own_process.md#5-stack-residue-as-modelled-values)).
Open: the consumer that turns the residue into the RNG difference;
weapons.md finds byte 19 copied but not used in arithmetic by the known
consumers. The shim does not depend on which byte it is.

**Where the weapon bodies read residue.** 41f5a0 enters with ESP = S, EBP
= S−4, outgoing ESP O = S−20h after `add esp,-10h` and three pushes.
`torso_aim` (2 slots, 41f6f3) and W.+60h (2 slots, 41f702) are entered at
T = O−0Ch = S−2Ch, W.+28h (3 slots, 41f74c) at E = T−4. The bodies read
uninitialized stack only through preparation 40e788 and, for the
emitters, the immediate impact
([weapons.md](weapons.md#nested-frame-residues)). Relative to torso_aim's
EBP (T−4) the original torso_aim (frame ACh bytes, EBX/ESI/EDI saved at
EBP−0B0h..−0B8h) has written every one of these bytes:

| +28h body | read (its entry ESP E) | = T− | original torso_aim's byte there |
|---|---|---|---|
| fire 40e964, 40ea04 | E−41h, E−61h (padding at preparation entry −21h/−41h) | 45h, 65h | EBP−41h: byte 7 of the transformed point EBP−48h (48043c at 41f122); EBP−61h: byte 19 of the matrix EBP−74h, copied from A+25h (byte 19 of A's pose) at 41efce/41efd4 |
| emitter 40ea58 | E−49h, E−69h | 4Dh, 6Dh | EBP−49h: byte 11 of the zero point EBP−54h (41ef4a); EBP−69h: byte 0Bh of the matrix EBP−74h (coefficient word 5 after 47df19) |
| alternate 40ec64 | E−3Dh, E−5Dh | 41h, 61h | EBP−3Dh: byte 11 of the point EBP−48h; EBP−5Dh: byte 17h of the matrix EBP−74h (translation dword after 480304) |
| impact exclusion (40ea58 / 40ec64) | E−0B8h / E−0ACh | 0BCh, 0B0h | saved EDI (aim_fire's EDI) / the dword EBP−0ACh; an original preparation overwrites the word with its own EBP (its last 47e0b0 call), a replaced one keeps it |

So with the original torso_aim the bytes do not depend on where aim_fire
sits. A **replaced torso_aim without its own shim** writes none of the
padding positions. Its framework entry leaves the hook pointer at T−4, a
return address at T−8, PUSHFD at T−0Ch, PUSHAD at T−10h..−2Ch, a 108-byte
reservation T−2Dh..−98h that the fast dispatch never writes (the extended
one FNSAVEs the x87 state there), then `es2_hook_dispatch_fast`'s frame.
All six padding positions fall into the reservation and keep residue
older than torso_aim; T−0B0h is the caller's ESI at the torso_aim call
and T−0BCh a dispatch local or alignment padding. In vitro
(`ab_real_fire_probe`: the real 41f5a0, 40e964 and 40e788, servos stubbed)
a host-stack `aim_fire` matched with the original torso_aim and failed
with the replaced one at 4a9847 (preparation's global matrix byte 19),
fast dispatch: a7 under the original aim_fire, 00 under the host-stack
one. A replaced torso_aim at the game's coordinate leaves the older fill
(fast) or x87 bytes (preserving) there, not the original torso_aim's
bytes; that is fixed by torso_aim's own shim
([below](#residue-shims-for-torso_aim-and-weapon_choose)).

**The shim.** `ai_behaviour_aim_fire_entry`, like `weapon_range`'s: PUSH EBP gives the
original's EBP (S−4); the caller's EBX/ESI/EDI go to the original's save
slots EBP−14h/−18h/−1Ch; EAX (dead at entry, 41f5b2 writes it) counts 64
guard-probed pages (256 KiB, `orl $0` keeps the bytes); a context {EBP, O
= EBP−1Ch} below the gap is published in `ai_behaviour_aim_fire_pending`;
the four slots are copied for the framework's dispatch; EDX/ECX pass
through. The adapter takes the context (only if the slots above its EBP
are this call's), puts the core's frame at the original's (L at EBP−10h, k
at EBP−2) and routes every game call through `ai_behaviour_stack_call`:
an `es2_machine_call` at ESP O with the slots, EAX/EDX/ECX and the
EBX/ESI/EDI the core names for this call plus EBP = S−4 (the x86 backend
switches ESP, nests and returns to the host stack), then store the
registers and write the original's return address (41f5bb … 41f785) into
the slot below the arguments. The core (`es2_aim_fire_registers` with an
`es2_ai_call_frame *`) states per call: EBX = A; ESI = P until the weapon
is known, then W (0 on the no-weapon path); EDI = P at 4928d0, then d,
then d's high word with the spread word (41f6a5, also when that word is
0), V at +60h and +28h. Without the shim (direct and legacy calls) the
adapter uses the plain bridge and its own 16-byte frame.

**Gap.** 64 pages, because a replaced emitter entered at E reserves its
own 128 KiB below E and runs its host frames below that (in vitro a +28h
stub that fills 192 KiB below E passes, the same probe crashes with a
128-KiB gap). Nested shims move their host frames down to the shared
stack floor, so the peak depth is the deepest single gap plus the stacked
host frames, not the sum of the gaps
([main_loop.md](main_loop.md#shim-stack-budget); the AI-tree nestings
have large positive margins there).

**Accepted differences.** A replaced callee entered through
`ai_behaviour_stack_call` saves the bridge's flags with its framework's
PUSHFD (the original's are those of the instructions before its CALL), and
the host frames below the gap are not the original's; no reader of either
position is known. After aim_fire returns, its frame (S−20h..S−1) and the
slots of its last call are the original's.

**The other hooks of the group.** A residue reader at the same outgoing
ESP after framed callees:

| Hook | Finding |
|---|---|
| weapon_choose | `trigger_fire` 423430 (41f57c) reads uninitialized record bytes; they reach only the cockpit message queue (434e8c, not simulation; [cockpit.md](cockpit.md)). Shimmed for exactness ([below](#weapon_choose--trigger_fire-record-bytes)). |
| aim_target | Calls aim_fire after framed callees. Under aim_fire the padding positions are older residue only when torso_aim runs without its shim, and then they can come from aim_target's earlier callees (outgoing ESP −81h..−0ADh). Plain bridge; live-identical original and replaced. |
| line_blocked | object_line 404ca0 copies an uninitialized byte 19 into its record, which nothing reads (above); segment 46e980's face_test reads its local −18h only for terrain flags 3, which game terrain never has (weapons.md). No effect. |
| target_search, hunt, clear_shot, group_move | No callee in their trees with a documented uninitialized read (target_valid, the list step, script_start, part_pick, vehicle_control, torso_center and the servos, the movement helpers). No fire or emission path; plain bridge. |
| weapon_range | Leaf. |

**In vitro** (`ai_behaviour_cases.h`, `differential_harness aim_fire`):
the 256 installed and 3,072 real-callee cases also compare EBX, ESI, EDI,
EBP and ESP at every stub entry and the 64 bytes below the caller's
argument slots after return (a host-stack adapter fails at case 0); 2,048
residue cases with a framed torso_aim stub (caller registers and
addresses at the positions above, a model of the replaced entry) and a
+28h stub that captures the 512 bytes below its entry, half of them also
filling 192 KiB below it; 3,072 real-fire cases (real 40e964 and 40e788,
torso_aim framed stub / original / replaced, `weapon_prepare` and
`weapon_fire` in all four modes, fast and preserving dispatch). Mutation
witnesses: [MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md).

## Residue shims for torso_aim and weapon_choose

Entry shims that make a replaced `torso_aim` and a replaced
`weapon_choose` leave the original's stack bytes for the readers below
their callers. Written from the listing and checked in vitro.

### Who calls torso_aim, and who reads what it leaves

**Callers.** The listing has three direct CALLs of 41ef14: 41f6f3 and
41f780 in `aim_fire`, 460c8f in `player_input`. A byte search of the
image finds no absolute 0041EF14h (no vtable slot, no pushed address).
Through hooks: a replaced `aim_fire` calls it through
`ai_behaviour_stack_call` at the original coordinate, a
replaced `player_input` through its host-stack bridge; no other core
module calls it.

**What the original leaves.** For the entry ESP T: EBP = T−4, locals
EBP−0ACh..−1, the caller's EBX/ESI/EDI at EBP−0B0h/−0B4h/−0B8h,
outgoing ESP O = EBP−0B8h = T−0BCh, below that the slots, return
addresses and frames of its callees. Every local that is read later is
written (EBP−10h/−0Fh and, off the stale-pose and far-point paths,
EBP−8, −14h and −18h keep what was there).

**Readers below a caller's ESP** (positions relative to T):

| Caller | Path after torso_aim | Bytes read |
|---|---|---|
| aim_fire 41f6f3 | W.+60h (40e62c, a leaf: one dword below its entry), then W.+28h at T−4 | preparation and emitters: T−45h/−65h, T−4Dh/−6Dh, T−41h/−61h, exclusion T−0B0h/−0BCh ([the table](#stack-residue-under-aim_fire)), all inside torso_aim's frame |
| player_input 460c8f | 4D2504h ← 0, AX = 1, no centring; with 4D24F0h = 0 (cockpit view) `fire_line` 415608 at 460cf2 from the same outgoing ESP (T+0Ch), `fire_selected` 410dbc, the weapon's +2Ch/+30h, then +28h at T−68h | fire 40e964/40ea04: T−0A9h, T−0C9h; emitter 40ea58: T−0B1h, T−0D1h; alternate 40ec64: T−0A5h, T−0C5h; impact exclusion T−120h/−114h |
| aim_fire 41f780 | aim_fire returns (no weapon); 41ea6c and 41eb23 return at once | whatever runs next at that depth under the AI step (not enumerated; the shim makes the bytes the original's for every later reader) |

On the player path T−0A9h and T−0A5h are byte 3 of the locals EBP−0A8h
and −0A4h (the second inverted translation), T−0B1h is byte 3 of the
saved EBX (the vehicle pointer), T−0C5h/−0C9h/−0D1h lie in the slots of
torso_aim's last call (41a808: the 47de72 result, the second 47e0a0
result, the return address 0041F347h) and T−114h/−120h in 41a808's
frame. Nothing between torso_aim's return and the +28h call writes
there: 415608 reaches down to T−30h, 410dbc to T−58h, and the weapon
+2Ch/+30h bodies of the nine weapon vtables (found by their +60h slot
40e62c: 498a84h, 498afch, 498b74h, 498bech, 498c64h, 498cdch, 498dcch,
498e44h, 4992c0h; bodies 40ecd4, 40ecdc, 40eda0, 496748, 40e678, 40e680,
40f8ad) are leaves with at most three pushes, to T−70h. So
`player_input` is a second caller whose later callees read torso_aim's
bytes, but only while it, `fire_line` and `fire_selected` run as
original code (replaced, they call from host stack, and no residue parity
is claimed for them). With a replaced torso_aim and no shim all these
bytes are the framework's (PUSHAD, the untouched reservation,
dispatch_fast's frame).

### torso_aim's entry shim

`input_dispatch_torso_aim_entry` (input_dispatch_hooks.h): PUSH EBP gives the
original's EBP; the caller's EBX/ESI/EDI go to EBP−0B0h/−0B4h/−0B8h;
EBX counts 48 guard-probed pages (192 KiB) and is reloaded from its
slot; {EBP, EBP−0B8h} is published; EAX/EDX/ECX pass through (41ef14
writes all three before it reads them). The adapter puts the core's
frame at the original's (scratch EBP−0B0h, synthetic EBP = EBP) and runs
every game call through `ai_behaviour_stack_call` (forward-declared: the
input_dispatch group is registered before ai_behaviour) with the
EBX/ESI/EDI and return address the core names
(`es2_torso_aim_frame_registers`, `es2_input_call`): ESI = P throughout;
EBX/EDI the two matrices up to `aim_angles`, then the rates as they pass
through `multiply_q10`, the clamps and `multiply_q8`; EBX = [EBP−34h] at
47de72 and 41a808; return addresses 41ef70 … 41f347.

**Trap: pushed slots at 47de72.** At 47de72 (41f337) the original has
already pushed 41a808's last two slots (41f30e PUSH 1, 41f31e PUSH EAX
with the target word), so 47de72 is entered 8 bytes below the outgoing
ESP and its slots lie below those two. The core reports them
(`pushed` = 2, `temp` = {1, target}); the bridge stores them and lowers
ESP. ECX at RET is 41a808's in the original; the framework returns the
entry ECX, so the shim loads the core's ECX when the replacement ran
(as `control_record_entry`). At 41f6f3 it reaches the weapon +60h,
whose body 40e62c ignores it, and after 460c8f it is dead (input.md);
this is exactness, not a known reader.

Gap: 48 pages. torso_aim's callees are the servos and math leaves (no
emitter), but a callee hook in compare mode reserves 64 KiB of
comparison storage below its entry, and the in-vitro real-callee pass
nests two (`torso_pitch` → `multiply_q8`); with 16 pages that pass
crashed. Nested under other shims (`aim_fire`, `player_input`, the logic
frame) its host frames move to the shared stack floor
([main_loop.md](main_loop.md#shim-stack-budget)).

### weapon_choose → trigger_fire record bytes

41f57c calls trigger_fire only when no listed weapon passed +5Ch
(EBP−0Ch still 1), the latch A+0A5h is clear and A+1B6h ≠ 0. Before it
only 47df48 (five slots), T.+34h (two) and W.+5Ch (one per non-null
entry) ran, all at the outgoing ESP O = EBP−24h. trigger_fire is entered
at O−8, so its record bytes (entry ESP−16h..−7, mission_runtime.md) are
O−1Eh..O−0Fh: 47df48's slots O−14h/−10h (A+2AAh sign-extended, 0), its
return slot O−18h, its saved EBP O−1Ch and whatever the +34h/+5Ch bodies
(per class) leave from O−10h down. They depend on weapon_choose's EBP,
its EBX/ESI/EDI and the callees' code, so a closed model is not
possible; the shim technique of `aim_fire` is used: `ai_behaviour_weapon_choose_entry`
(frame 18h: EBX/ESI/EDI at EBP−1Ch/−20h/−24h; 32 pages, counted in EBX
and reloaded, because the entry EAX/EDX/ECX reach 47df48 unchanged),
`es2_weapon_choose_frame_registers` with `es2_ai_push_frame` (EDI = A;
EBX = T, then the list entry W, also 0; ESI = the sign-extended word
A+2AAh, then W.+60h's whole EAX, the rank, the first draw, the product
of both draws; return addresses 41f382 … 41f581), and the two pushed
temporaries: X (41f4e3) during the second `multiply_q10`, Z (41f502)
during both `rng_bounded` calls, which are entered 4 bytes lower. The
core also writes the frame locals (best −2, t34 −4, chosen −8, count
−0Ah, none −0Ch, bound −0Eh, i −10h, rank −14h, cost −18h) as the
original does, so after return weapon_choose's whole frame is the
original's. Gap: 32 pages (41f358's only caller is 41f629, under
aim_fire's shim). The bytes
still reach only the cockpit message queue (presentation, not compared
by the oracle); a replaced `trigger_fire` captures them in its own entry
shim.

### Validation of the residue shims

- `torso_aim` (input_dispatch_cases.h): residue probe, 1,024 cases
  (fast/preserving dispatch, 31/a7 stack, distinct caller EBX/ESI/EDI):
  the original 41ef14 against the installed replacement, with logging
  stubs that record EBX/ESI/EDI and EBP/ESP (relative to the caller's
  slots) at every entry; pair, ECX at return, arena, globals and the
  1,024 bytes below the caller's slots must be equal. It found the 47de72
  slots above (stub ESP −0D8h against −0D0h).
- `player_input`: fire-path probe, 4,096 cases, half of them forced onto
  the tracking path (cockpit view, zero axes, ground chassis, a target,
  W+14h set): the original 460870 with stubbed callees, the real 41ef14
  (original in the reference run, replaced in the compared run) and a
  415608 stub that copies the 1,024 bytes below its entry; 1,135 (quick tier) or 1,207 (full tier) cases
  reach torso_aim and then fire_line; window, records, pair, arena and
  globals equal.
- `aim_fire`: the real-fire probe has a fourth variant, the original
  torso_aim in the reference run against the replaced one in the
  compared run, under the original or the replaced aim_fire, through the
  real 40e964 and 40e788 (weapon_prepare/weapon_fire in all four modes):
  4a9834.. including 4a9847 equal, which differ without a torso_aim-side
  shim.
- `weapon_choose` (ai_behaviour_cases.h): the installed and real-callee
  passes now compare EBX/ESI/EDI/EBP/ESP at every stub entry and the 64
  bytes below the arguments after return; residue probe, 2,048 cases
  (half with every weapon busy, the latch clear and a trigger set),
  trigger_fire as a stub that copies the 512 bytes below its entry: 1,225 (quick)
  or 1,209 (full) cases reach it; window (record bytes included), records and frame
  equal.
- Mutation witnesses: [MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md).

**Remaining differences, accepted** (as under `aim_fire`): a replaced
callee's PUSHFD saves the bridge's flags, and the host frames below the
gaps are not the original's; no reader of either is known.

## Aim selection, attack planning, group scans and retreat

Six hooks of the group `ai_behaviour2` (core `core/src/ai_behaviour2.c`,
`es2/core/ai_behaviour2.h`; adapter `proxy/ai_behaviour2_hooks.h`), next
to `group_move` and `aim_target`. The data references come from a raw
scan of the file for the function addresses.

**Script tables.** Four of the six are script members. The static
initializers (from 413ed4) copy 24h-byte template blocks into the script
tables: block +0 becomes the table's step member +18h (called by 415b38,
actor vtable +18h, from the logic frame at 45f7a6 and from the group
update 411cec, which ends the script when the step returns AX ≠ 0), block
+0Ch the member +24h (41a360 in all of these; 415afc) and block +18h the
member +30h (415b74, vtable +1Ch, called by 411cec when the script timer
A+52h is zero). The blocks found by the raw scan, with the table each
initializer fills:

| Table | Step (+18h) | +30h | Started by |
|---|---|---|---|
| 49945e | 41c594 | 41cf18 | `attack_plan` (+4Ch result 1) |
| 49949c | 41d4e4 | 41cf18 | `attack_plan` (+4Ch 2, configuration word +0C8h > B9h) |
| 4994da | 41d41c | 41cf18 | `attack_plan` (+4Ch 0, or 2 otherwise) |
| 499518 | 41c86c | 41cf18 | `attack_plan` (target type 1 or 3), `attack_target` |
| 499556 | 41c9cc | 41cf18 | `attack_plan` (target type 2), `attack_target` |
| 499594 | 41d7d0 | 41eb34 | `order_resume` (order type 3; player-led states 1, 2) |
| 49968c | 41d60c | 41eb34 | `order_resume` (order type 0) |
| 499800 | 41d2c4 | 41cf18 | `hunt`, `retreat_check` and the two scans below (an actor that is out) |

So 41cf18 is the "timer expired" member of the five attack scripts and of
the retreat script 499800, whose steps all fire through 41ea7c.

| Hook | VA | Role (hypothesis) | Callers | Map probes (a60/c1s/c1c/c3_09/c4_08/c3_10) |
|---|---|---|---|---|
| `aim_select` | 41ea7c | aim point on T by its type word, then `aim_target` or `aim_fire` | the attack and retreat steps (41c6c8, 41c8e9, 41ca44, 41d3c1, 41d480, 41d534) and 41da9a, 41dbbb (41e076 lies in `drive_off`'s unreachable tail) | 540/1000/1000/1000/1000/383 |
| `attack_plan` | 41cf18 | member +30h of the attack scripts: fire permission, target by order state, idle members, retreat check, next attack script | 41d5ec; member +30h of six tables | 2/8/5/50/6/4 |
| `attack_replan` | 41d5ec | `attack_plan(A)` (8 instructions) | 41c174 (`attack_target`), 41d18f (`attack_plan`), 41d7c2, 41d9bd, 41faa8 | 2/7/4/21/2/4 |
| `group_scan` | 41d60c | step of 49968c: move with the group, scan for a target the order allows | step member (block 499aa8) | 106/844/212/1000/63/1000 |
| `order_scan` | 41d7d0 | step of 499594: the same for order state 2 or a type-3 order | step member (block 499a18) | 1000/1000/1000/1000/0/0 |
| `retreat_fire` | 41d2c4 | step of 499800: back away from the remembered target and keep firing at it | step member (block 499b80) | 0/60/0/50/0/0 |

### 41ea7c `aim_select(A, w, T)`

cdecl, three slots (w used as a word); T = 0 selects `[A+1A4h]`. Frame
1Ch: the aim point Q = EBP−1Ch (3 dwords), two 6-byte records EBP−8 and
EBP−10h. EBX = T and ESI = A at every call; EDI is the caller's EDI
except on the part path (EDI = T from 41eabe).

1. Word T+1A8h = 0: `aim_target`(A, lo(slot 3, w), T) (slot 3 is 0 when
   the target came from A+1A4h) with EAX = that slot, EDX/ECX = the entry
   registers; return its EDX:EAX.
2. Word T+1A8h (read again) = 1: k = AX of T.+54h(T, 0) (EAX = `[T]`,
   EDX/ECX entry); T.+58h(T, sx(k), Q) (EAX = sx(k), EDX = Q, ECX =
   `[T]`): Q is the part's position (`part_position` writes 12 bytes).
3. Otherwise: Q ← T+26h (read in the order z, x, y), T.+30h(T, EBP−8,
   EBP−10h) (EAX = `[T]`, EDX = EBP−10h, ECX = EBP−8; every implementation
   writes both records: vehicle 4155c4, building 40351c → 411a74, flyer
   411a74), then Q.z += sx(word EBP−0Ch), Q.x += sx(word EBP−10h), Q.y +=
   sx(word EBP−0Eh).
4. `aim_fire`(A, Q, lo(EDX, w), T) with EAX/EDX as the path left them (the
   +58h call's, or the three words just added), ECX = Q; its EDX:EAX.

Callees: `aim_target`, `aim_fire` (hooks), the target virtuals +30h, +54h
(`facing_part` 41b534 on a vehicle), +58h (`part_position`). 3 branch
sites, both ways. ECX after return is dead at every caller: 41c6cd,
41d485 and 41d539 (and 41e07b in `drive_off`'s unreachable tail) hand it to `engage_done`, whose callees load
ECX first (mission_runtime.md); 41c8ee overwrites it; 41ca49 passes it
through 41c478 to 415b38's POP ECX; 41d3c6, 41da9f and 41dbc0 return or
pop.

### 41cf18 `attack_plan(A)`

cdecl, one slot. The only entry register read is ECX's high 24 bits,
which reach `target_select` at 41d011 when no instruction before has
written ECX (MOV CL at 41cfe2). Frame 20h (EBP−4..−20h): copies of F,
the table, a flag, T and a configuration word; no callee gets their
address.

1. Fire permission: L = `[[[A+45h]+0Ch]]` (the group leader). Byte L+0A3h
   set and byte A+0B2h clear: byte A+96h = 0. Otherwise, word A+26Bh = 0:
   byte A+96h = 1 (EDX = ECX = F).
2. Byte A+0ACh set: clear it and go to 5.
3. Order state s = word A+23Eh ∈ {0, 1, 2}: t = the group's current order
   type (`[G+44h+4·[G+6Ch]]`, word +0; EDX = lo([G+6Ch], t), 0Bh for an
   empty slot), f = (t ∈ {5, 6}); c = sx(byte `[[A+4Dh]+9]`) (the script
   flag; EDX = c, CL = that byte). EAX = 1 when s = 1 or (s = 0 and f),
   else 0 (s read again twice). EAX ≠ 0 and c ≠ 0: `order_resume`(A) and
   return 0. Otherwise N = `target_select`(A, 1, 0) (EAX, EDX, ECX as
   above), the target switch (below; ECX = the old target, EAX = the new),
   and `[A+1A4h]` ≠ 0 goes to 5, else `order_resume`(A).
4. Other states: the switch to `[A+248h]` (the order target; ECX = the
   old and then the new target, EDX = F). T = `[A+1A4h]` ≠ 0 with byte
   T+99h clear goes to 5. Otherwise N = `target_select`(A, 0, 0) (EAX =
   `[A+248h]` or T+92h, EDX = T, ECX as left), the switch (ECX = old, EAX
   = new, then EDX = ECX = F), and `[A+1A4h]` ≠ 0 goes to 5, else
   `order_resume`(A) (EAX = N, EDX/ECX as left).
5. A is the leader (`[[A+45h]+0Ch]` = A; EAX = 0/1 from SETE): for i = 0
   while s16 i < s16 word `[[A+45h]+10h]` (re-read each time), E =
   `[[[A+45h]+0Ch] + 4·sx(i)]`, E = 0 ends the walk: E ≠ A with bytes
   E+0A4h and E+99h clear and byte `[[E+4Dh]+9]` clear: `attack_replan`(E)
   (EAX = E, EDX = `[E+4Dh]`+8, ECX = the list; ECX = E after the POP).
6. Word A+2A2h = FFFFh; `retreat_check`(A) (EAX = 0, EDX = `[A+45h]`, ECX
   = the list or the last E called); AX ≠ 0: return 0 with its EDX.
7. v = sx(word T+1A8h) with T = `[A+1A4h]` (EDX = T): v = 1 or 3 → table
   499518h; v = 2 → 499556h; otherwise `retreat_check`(A) again (EAX = v,
   EDX = T, ECX = A), then k = sx(AX of A.+4Ch(A, `[A+1A4h]`)) (EAX = the
   check's, EDX = T read again, ECX = `[A]`): k = 0 → 4994DAh (EAX =
   FFFFFFFFh), k = 1 → 49945Eh (EAX = 0), k = 2 → 4994DAh, or 49949Ch
   when bytes A+0A8h and A+0A9h are clear and the signed word
   `[[A+1F2h]+0C8h]` > B9h (EAX = T read again; EDX = F with ECX = 1 when
   a byte is set, else EDX = the configuration, ECX = the word); then
   `part_pick`(A).
8. `script_start`(A+4Dh, table) with EAX = table, EDX = `part_pick`'s (or
   T on the v = 1, 2, 3 paths), ECX = A. Result EAX = 0, EDX the last
   callee's.

The target switch (inlined seven times in these functions, also in
`hunt`): when N ≠ `[A+1A4h]`, the old target's attacker word +1A2h −1
(when there is one), `[A+1A4h]` = N, N's word +1 (N ≠ 0), byte A+9Dh = 1;
the copies load the old and the new target into different scratch
registers, which the cores reproduce.

Two edges are proved unreachable: 41d207 (the second `retreat_check`
returns nonzero) — the first call returned AX = 0 on the same state; on
that path `retreat_check` writes only word A+2AAh, which it does not read
(mission_runtime.md: virtual +40h, the part ratios, `threat_sum`), and
nothing between 41d1c0 and 41d1fd stores; and 41d228's fall-through (k
outside 0..2) — the only +4Ch implementations are 41cabc (vehicle table
49a282: AX 0, 1 or 2) and 411ac0 (every other actor table: AX = 1).
Both unreachable paths would push the uninitialized frame dword EBP−0Ch
as the script table; the core passes 0 there. Precondition (the original
faults): with byte A+0ACh set, A has a target (7 reads T+1A8h without a
test). 48 branch sites, 46 both ways.

### 41d5ec `attack_replan(A)`

`attack_plan`(A) with EAX = A, EDX/ECX entry; returns its EDX:EAX and
ECX = A (POP). Every caller pops ECX right after the call (41c179,
41d194, 41d7c7, 41d9c2, 41faad), so the framework's entry ECX is
equivalent. No branch.

### 41d60c `group_scan(A)` and 41d7d0 `order_scan(A)`

cdecl, one slot, no frame. Both: `group_move`(A) with the entry registers
(ECX = A after the POP). 41d7d0 reads s = word A+23Eh into SI **before**
that call and tests SI afterwards.

1. Drop the target: `[A+1A4h]` ≠ 0 → its word +1A2h −1, `[A+1A4h]` = 0,
   byte A+9Dh = 1, EAX = F. 41d60c loads `[A+1A4h]` into EDX first (EDX =
   0 afterwards either way, ECX = the old target); 41d7d0 tests it in
   memory (EDX = the old target, ECX = 0). The re-test after the first
   (41d62b, 41d7fa) and the test of the XOR-zeroed register (41d644,
   41d813) are one-way (PROVED).
2. Unless byte A+0B6h is set (41d7d0: or s ≠ 0), only the leader goes on
   (EAX = 0/1, EDX = `[A+45h]`, ECX = the list; else return 0 with that
   EDX).
3. `timer_word_step`(B) (the scan timer; ECX = B after the POP); running
   (AX ≠ 0): return EAX = EDX = 0. Expired: word B+1 = 10000.
4. Out (byte A+0A5h, or A+0A4h or A+99h; EAX = F or 1, EDX = 1): N =
   `target_select`(A, 0, 0) (ECX = B), the switch (ECX old, EAX new), and
   with a target word A+2A2h = FFFFh and `script_start`(A+4Dh, 499800h)
   (the retreat). Return 0 with the last EDX.
5. Not out (EAX = EDX = 0). 41d60c: N = `target_select`(A, 1, 0); N ≠ 0
   and AX of `order_target`([A+45h], N) ≠ 0 (EAX = N, EDX = the group):
   the switch (ECX old, EAX new, EDX = F). 41d7d0: only when s = 2, or s
   = 0 and the group's current order type is 3 (EAX = lo(order, 3), EDX =
   ECX = the order index there); N = `target_select`(A, 1, 0), the switch
   (EDX old, EAX new) and a target needed.
6. Byte A+0ACh = 1 (EAX = F) and `attack_replan`(A): the attack plan
   starts at once. Return EAX = 0, EDX the last callee's (or the value of
   the path that returned).

41d60c: 20 branch sites, 17 both ways, three PROVED (41d62b, 41d644, and
41d79e: the new target is nonzero by the test at 41d763). 41d7d0: 24
sites, 22 both ways, two PROVED. Both return AX = 0, so these scripts end
only through their timers.

### 41d2c4 `retreat_fire(A)`

cdecl, one slot. Frame 18h: the steering record S = EBP−18h that
`target_bearing` fills completely (+2 relative bearing, +4 its magnitude,
+6 aspect, +0Eh drive word, +10h/+14h bounds; vehicle.md) and
`drive_apply` reads. EBX = A, ESI = B and EDI = the caller's EDI at every
call.

1. `[A+1A4h]` ≠ 0: remember it in dword B+7 (A+61h), then drop it (the
   re-tests 41d2e8/41d2ec and the XOR-zero test 41d305 are PROVED one-way;
   ECX = the old target, EDX = 0, EAX = F).
2. V = dword B+7; `target_bearing`(A, S, V) (EAX = V, EDX = S, ECX entry
   or the old target).
3. Signed word S+4 < 3000h (V roughly ahead): drive word S+0Eh = FFFFh
   (reverse), word S+2 += 8000h. Otherwise `timer_dword_step`(B) (with the
   registers `target_bearing` returned); ECX = 1 when it returns EAX = 0,
   else 0; expired: dword B+1 = 4000 and word B+5 = AX of
   `rng_next`(4d2599h) & 1 (EAX = 0, ECX = 1; ECX = 4d2599h after the
   POP). Then S+0Eh = 1 and word S+2 += lo(EAX, A000h), plus C000h as a
   32-bit add when word B+5 = 0 (a turn of A000h or 6000h).
4. Dwords S+10h = 0, S+14h = 10000000 (EDX = 0). `[A+317h]` ≠ 0:
   `turbo_engage`(`[A+317h]`) (a one-shot sound through 462830; EAX = the
   pointer, ECX = it after the POP).
5. `drive_apply`(A, S) (EDX = S); `aim_select`(A, lo(EAX, word S+6), V)
   with V = dword B+7 read again (ECX = V).
6. V read again: byte V+0A5h set → EAX = lo(V, 1), EDX = 1; else EAX =
   EDX = (byte V+0A4h or V+99h set); EAX = 0 when EDX = 0. AX ≠ 0 ends
   the script (411cec).

Callees: `target_bearing` 41e758, `timer_dword_step`, `rng_next` 492ee0
(stream A), `turbo_engage` 40f09c, `drive_apply` 41e828, `aim_select`.
13 branch sites, 10 both ways, three PROVED. Precondition (the original
faults): V is an actor (A's target, or the one remembered when the
script started).

### Stack residue and the entry shims of aim_select and retreat_fire

The weapon bodies under `aim_fire`'s W.+28h
read stack bytes that no instruction of theirs wrote, at positions fixed
relative to `torso_aim`'s entry ([Stack residue under
aim_fire](#stack-residue-under-aim_fire)).
`aim_select` is the only caller of `aim_fire` besides `aim_target` (every
`aim_fire` call in the six suites runs under 41ea7c), so a replaced
41ea7c that called through the host-stack bridge would move `aim_fire`,
`torso_aim` and the +28h bodies to another coordinate and fill the region
below them with host frames instead of the residue of the +54h/+58h/+30h
calls (original game code) and of 41ea7c's caller. **`aim_select` and
`retreat_fire` (which calls it) therefore have entry shims** like
`aim_fire`'s (ai_behaviour2_hooks.h): PUSH EBP keeps the original's EBP; the
caller's EBX/ESI/EDI go to the original's save slots (41ea7c: EBP−20h,
−24h, −28h; 41d2c4: EBP−1Ch, −20h, −24h); EAX (dead at both entries:
41ea85 and 41d2d3 write it) counts the guard-probed pages; the context
{EBP, outgoing ESP} is published for the adapter; EDX and ECX pass
through. The adapter puts the core's frame at the original's (Q at
EBP−1Ch, S at EBP−18h), reads the caller's EDI back from its save slot
(both originals keep it in EDI up to their calls) and runs every game
call through `ai_behaviour_stack_call` (ai_behaviour_hooks.h) at the
original outgoing ESP with the EBX/ESI/EDI the core names, EBP, and the
original's return address left in the slot below the arguments (41eaaf,
41eac8, 41ead9, 41eb00, 41eb28; 41d32c, 41d34b, 41d36a, 41d3a9, 41d3b4,
41d3c6). Gaps: 41ea7c 68 pages (272 KiB: `aim_fire`'s 256 KiB plus its
host frames, and `aim_target`'s host frames on the type-0 path), 41d2c4
72 pages (288 KiB: 41ea7c's plus its host frames and those of its other
callees). Nested shims move their host frames to the shared stack floor
([main_loop.md](main_loop.md#shim-stack-budget)). Without the shim (direct and legacy
calls) the adapters use the plain bridge and their own frame.

The other four have no fire path: a direct-call walk from 41cf18, 41d5ec,
41d60c and 41d7d0 (at most 192 functions) reaches none of 41ea7c,
41e984, 41f5a0, 41ef14 or the weapon bodies 40e788/40e964/40ea04/40ea58/
40ec64; their indirect calls are the actor virtuals +40h/+4Ch, the
formation, obstacle and animation virtuals and the runtime. They keep the
plain bridge, like `hunt`, `clear_shot` and `group_move` (live-identical). Their own frames are copies (41cf18) or absent.

### Shared properties of aim selection and planning

- **Entry registers.** Read before written: ECX's high 24 bits in 41cf18
  (above); the entry EDX/ECX of 41ea7c reach `aim_target` and +54h, those
  of 41d60c/41d7d0 `group_move`, ECX of 41d2c4 `target_bearing`, EDX/ECX
  of 41d5ec 41cf18. The adapters forward them; the shims pass EDX/ECX. No
  frame local is read before it is written (except on the two PROVED
  unreachable paths of 41cf18), so no shim is needed for an uninitialized
  read.
- **Callee-saved registers.** 41d7d0 keeps the caller's ESI high word in
  ESI (MOV SI), and 41d60c/41d7d0 overwrite ESI/EDI with F on the out
  tests; they reach callees only as values the callees save (residue;
  plain bridge as for `hunt`). For the two shim hooks the core names
  EBX/ESI/EDI per call.
- **ECX after return.** The framework returns the entry ECX: 41ea7c above;
  41cf18 returns to 41d5ec or 415b74, which pop ECX; 41d5ec's callers pop;
  the step members return to 415b38's POP ECX.
- **EDX after return** is reproduced exactly; the steps' EDX goes through
  415b38 to 45f7a6 (then the list step 471c70) or 411cec (413eb0
  overwrites it).
- **Divisions:** none. **Draw:** none. **Sound:** `turbo_engage` 40f09c →
  `sfx_play` 462830, both hooks behind the bridge.

### Validation of aim selection, attack planning and scans

`tools/reference-game/invitro/ai_behaviour2_cases.h`, group
`ai_behaviour2` (`differential_harness <hook>`):

- 1,001,024 direct cases per hook (quick tier 100,000) against logging
  stubs for every direct game callee of the hook under test (ordered
  slots, entry EAX/EDX/ECX with stack addresses masked, the steering
  record and the aim point by content). The arena: six actors with flag
  bytes, targets, order states and remembered targets, two groups with
  member lists (counts 0..8 and negative, null entries), eight orders,
  four script tables (flag +9) and configurations (word +0C8h around
  B9h), the vtable with +30h, +3Ch, +4Ch, +54h, +58h. Stubs return planned
  values where a comparison follows (timers zero or not, the magnitude on
  the 3000h edge, +4Ch 0/1/2, `order_target` AX with a random high word)
  and change what the originals reread (A's target behind the scans' and
  `attack_plan`'s back, the order state word during `group_move`, the
  remembered target during `aim_select`). `retreat_check` answers the same
  for a whole case (the PROVED repeat). Compared: the pair, the arena, RNG
  stream A and the stagger word, and the callee counters in the first
  4,096 cases.
- 256 installed 31/a7 poison cases per hook (fast and preserving
  dispatch, live x87 stack; the caller's EBX/ESI/EDI differ from each
  other); for `aim_select` and `retreat_fire` the stubs also record EBX,
  ESI, EDI and EBP/ESP relative to the caller's argument slots, and the 64
  bytes below the slots (return address, saved EBP, the frame, the save
  slots and the last call's slots) are compared after return.
- 3,072 real-callee cases per hook, nested hooks in rotating modes and the
  hook itself original or replaced, consistent groups (each actor in its
  own group's list, leader first): `attack_plan` (`script_start`,
  `attack_replan`, so the member walk re-enters the hook under test),
  `attack_replan` (`attack_plan`, `script_start`), `group_scan`
  (`timer_word_step`, `script_start`, `order_target`), `order_scan`
  (`timer_word_step`, `script_start`), `retreat_fire` (`target_bearing`,
  `timer_dword_step`, `rng_next`, `drive_apply`, `aim_select` with its
  shim, `heading_point`, `dist_gate`; per-call registers and frame
  tracked) and `aim_select` on ai_behaviour_cases.h's arena and stubs with
  the real `aim_target` and `aim_fire` nested (both shims stacked;
  per-call registers and frame tracked). Stubs that stay: `target_select`,
  `order_resume`, `retreat_check`, `part_pick`, `group_move`, `turbo_engage`,
  `vehicle_control`, and under `aim_select` the callees of `aim_target`
  and `aim_fire`.
- Coverage: 108 branch sites, 98 both ways in the instrumented
  prefix, ten PROVED one-way (41d207, 41d228, 41d62b, 41d644, 41d79e,
  41d7fa, 41d813, 41d2e8, 41d2ec, 41d305).

Mutation witnesses
([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)):
eight, at least one per hook, two of them on the shim adapters.

## MECH combat states

Eight hooks of the group `mech_combat` (core `core/src/mech_combat.c`,
`es2/core/mech_combat.h`; adapter `proxy/mech_combat_hooks.h`): the step
members (+18h) of the five attack states that `attack_plan` starts and of
"driving off", which `hunt` starts, with the two helpers only they call.
The data references come from a raw scan of the file for the function
addresses, the probe counts from the DBSIM map's entry probes. S is the
18h-byte steering record that `target_bearing` fills completely (+0
heading to T, +2 relative bearing, +4 its magnitude, +6 aspect, +8 its
magnitude, +0Ah distance dword, +0Eh drive word, +10h/+14h distance
bounds; [vehicle.md](vehicle.md#target_bearing-41e758)) and `drive_apply`
turns into the `vehicle_control` call
([mission_runtime.md](mission_runtime.md#41e828-drive_applya-s)).

**How they are reached.** The six steps occur once each in DATA, as the
first dword of a 24h-byte constant block that the startup initializer
413ed4 copies to the state record's member +18h (vtables.md section 3):
41c594 at file offset 98b64h (state "attacking" 49945Eh), 41d4e4 98b88h
("flanking" 49949Ch), 41d41c 98BACh ("facing off" 4994DAh), 41c86c
98BD0h ("attacking base" 499518h), 41c9cc 98BF4h ("attacking flyer"
499556h) and 41def0 98D38h ("driving off" 499784h). The member
trampoline 415b38 (actor slot +18h) calls them as `fn(A + delta)` with
EAX = the state record, EDX = its third dword, ECX = the OR of the three
member dwords (the function address in the retail tables) and EBX = A,
then pops ECX and returns their EDX:EAX; 411cec ends the state when AX ≠
0. 41d41c is also called directly by 41def0 (41df10). 41c478 and 41de9c
have only direct callers, all in this group (41c478: 41c71a, 41c9bb,
41caab, 41d4d2, 41d586; 41de9c: 41c5a4, 41c879, 41d429, 41d4f1, 41defd).

| Hook | VA | Role (hypothesis) | Map probes (a60/c1s/c1c/c3_09/c4_08/c3_10) |
|---|---|---|---|
| `engage_end` | 41c478 | in order state 4 with the target being the order target: back to state 0; AX = 1 (the state ends) | 0/0/0/14/1/0 |
| `skirt_check` | 41de9c | after a blocked line of fire (byte A+0ADh, set by slot +64h 41dd2c): keep the state, start "skirting" (`clear_shot`), AX = 1 | 540/1000/1000/1000/1000/383 |
| `attack_step` | 41c594 | "attacking": close in on T beside its heading, or back off when T faces away, and fire | 0/1000/170/521/0/0 |
| `base_attack` | 41c86c | "attacking base": the flank manoeuvre at a fixed distance window, and fire | 170/0/0/1000/0/383 |
| `flyer_attack` | 41c9cc | "attacking flyer": hold a distance window under the flyer and fire; AX = 1 beyond 249F0h | no probe row |
| `face_off` | 41d41c | "facing off": keep 3E80h..88B8h from T facing it, and fire | 370/947/403/1000/885/0 |
| `flank_attack` | 41d4e4 | "flanking": the flank manoeuvre, and fire | 0/0/926/0/1000/0 |
| `drive_off` | 41def0 | "driving off": `skirt_check`, else `face_off` | no probe row |

Derived from the counts: `skirt_check` is called once per step of
41c594, 41c86c, 41d41c (every entry, also those from 41def0), 41d4e4 and
41def0, so a60 (0 + 170 + 370 + 0 = 540) and c3_10 (383) leave no room
for a 41def0 call; with `clear_shot` at 0 in a60 and c3_10 no skirt was
started there, and `target_bearing` (540, 383) then leaves none for 41c9cc
either. Elsewhere the saturated counters bound nothing.

### 41c478 `engage_end(A)`

cdecl, one slot, leaf (10 instructions), EAX = A from the slot. Word
A+23Eh = 4: EDX = `[A+1A4h]`, and EDX = `[A+248h]` clears the word. EAX =
1 always; EDX = the target (state 4) or the entry EDX; ECX = the entry
ECX. 2 branch sites, both ways.

### 41de9c `skirt_check(A)`

cdecl, one slot; EBX = A, ESI/EDI the caller's until the first call.
Byte A+0ADh (`[F+1Bh]`) = 0: EAX = 0 (XOR), EDX/ECX entry. Otherwise:

1. EDI = `[A+4Dh]` (the current state record, read **before** the call);
   `script_start`(A+4Dh, 499708h) with EAX = A+4Dh and the entry EDX/ECX
   (EBX = A, ESI the caller's, EDI the old record).
2. `[A+5Ah]` = that record (B+0, which `clear_shot` restarts when the line
   is clear; `script_start` cleared B+0..B+27h); `dist_gate`(A+26h,
   A+31Eh) with EAX = A+31Eh and `script_start`'s EDX/ECX (EBX = A+26h,
   ESI = B, EDI the old record).
3. `[A+67h]` (B+0Dh) = its EAX; result EAX = lo(that EAX, 1), EDX =
   `dist_gate`'s.

1 branch site, both ways.

### 41c594 `attack_step(A)`

cdecl, one slot; frame 30h: the mirror point P = EBP−30h (in EDI from the
prologue on), the offset point L = EBP−24h, S = EBP−18h. EBX = A; ESI =
the caller's ESI at the first call, T from 41c5b6 on.

1. `skirt_check`(A) with the entry EAX/EDX/ECX (ESI the caller's, EDI =
   P); ECX = A (POP). AX ≠ 0: return EAX = 0 with its EDX.
2. T = `[A+1A4h]` (read once, after `skirt_check`). `target_bearing`(A, S,
   0) with EAX = S, EDX = `skirt_check`'s, ECX = A.
3. Word S+8 ≥ 1F40h (signed; T's aspect points away): drive word S+0Eh =
   0 when word S+4 < 2000h, else FFFFh (EDX = 0 or FFFFFFFFh); go to 6
   with EAX = `target_bearing`'s.
4. Otherwise L = T+26h (x, y, z read in that order); EDX = FFFFFFFFh, +2
   when 8000h − sx(word S+4) ≥ FA0h (32-bit, signed): S+0Eh = DX (1 =
   forward, FFFFh = reverse). EDX = (word S+2 < 0), ECX = (word S+4 >
   4000h) (both 0/1, all 32 bits); EAX = lo(8000h − sx(S+4), 4000h), plus
   FFFF8000h (32-bit) when EDX ≠ ECX, then lo(EAX, AX + word T+10h).
   `point_offset`(L, EAX, 4650h) with EAX = L.
5. Word S+0Eh ≠ FFFFh: P = L (z, x, y). Otherwise P = A+26h (z, x, y),
   L −= P and then P −= L (each z, x, y; `[A+2Ah]` read once): the point
   mirrored through A. `heading_point`(P, A+26h) with EAX = A+26h, EDX =
   L.x, ECX = L.y (as last loaded); word S+2 = lo(EAX, AX − word A+10h).
6. `drive_apply`(A, S) with ECX = S (EAX/EDX as the path left them).
7. `aim_select`(A, lo(EAX, word S+6), 0) with `drive_apply`'s EDX/ECX.
8. `engage_done`(A) with `aim_select`'s registers; ECX = A (POP).
9. EAX ≠ 0 (all 32 bits) or A out (below): `engage_end`(A) and its
   EDX:EAX; otherwise EAX = EDX = 0.

EBX/ESI/EDI at the calls: A, T (the caller's at the first), P; at
`engage_end` ESI/EDI as the out test left them. 12 branch sites, all both
ways.

**The out test** (41c6db, 41c97c, 41ca6c, 41d493, 41d547; inlined five
times): EAX = F; byte A+0A5h set: EDX = 1. Otherwise EDI = EDX = F; byte
A+0A4h set: EAX = 1; else ESI = F and EAX = (byte A+99h ≠ 0); EDX = (AX ≠
0). EDX ≠ 0 calls `engage_end` with these EAX/EDX and the ECX of the
moment.

### 41c86c `base_attack(A)`

cdecl, one slot; frame 18h: S = EBP−18h. EBX = A; ESI and EDI stay the
caller's up to the target test.

1. `skirt_check`(A) (entry registers); ECX = A; AX ≠ 0: EAX = 0, its EDX.
2. `target_bearing`(A, S, 0) with EAX = `skirt_check`'s, EDX = S, ECX = A.
3. ECX = T (read after the call); word `[[T+1F2h]+2Eh]` = 0: EDX =
   lo(EDX, 7D00h), words S+8 and S+6 = 7D00h (in that order).
4. `flank_step`(A, S) with EAX = S, ECX = T. Dword S+10h = 61A8h, ECX =
   S, dword S+14h = FFFFAFC8h. `drive_apply`(A, S) with `flank_step`'s
   EAX/EDX.
5. `aim_select`(A, lo(EAX, word S+6), 0) with `drive_apply`'s EDX/ECX.
6. Word A+23Eh = 4 and `[A+248h]` ≠ 0: go to 8. Otherwise
   `order_target`(`[A+45h]`, T) with EAX = `aim_select`'s, EDX = T, ECX =
   the group; AX ≠ 0: go to 8.
7. T out (T read again): EAX = T, EDX = T+92h; byte T+0A5h: EDX = 1;
   otherwise ESI = T+92h, byte T+0A4h: EAX = 1; else EDI = T+92h, EAX =
   (byte T+99h ≠ 0); EDX = (AX ≠ 0). ECX = EAX = sx(DX). Go to 9.
8. T destroyed: DL = byte `[T+99h]` (T read again; EDX keeps its upper 24
   bits), ECX = EAX = the byte sign-extended.
9. EAX ≠ 0 or A out: `engage_end`(A); otherwise EAX = EDX = 0. On the out
   path ECX = 0.

15 branch sites, all both ways.

### 41c9cc `flyer_attack(A)`

cdecl, one slot; frame 18h: S. EBX = A, ESI/EDI the caller's (no
`skirt_check`).

1. `target_bearing`(A, S, 0) with EAX = S and the entry EDX/ECX.
2. Drive word S+0Eh = FFFFh; when word S+4 < 7D0h and (word S+8 > 7D0h or
   dword S+0Ah > 88B8h, signed): 0; when also word S+8 > 7D0h and S+0Ah >
   88B8h (both read again): 1.
3. EDX = 0, ECX = S, dwords S+10h = 0, S+14h = F4240h; `drive_apply`(A,
   S) with `target_bearing`'s EAX.
4. `aim_select`(A, lo(EAX, word S+6), 0) with `drive_apply`'s EDX/ECX.
5. Dword S+0Ah > 249F0h (read again): return EAX = lo(EAX, 1), EDX =
   `aim_select`'s.
6. EAX = T+92h (T read again); byte T+99h set: `engage_end`(A) with that
   EAX and `aim_select`'s EDX/ECX. Otherwise the out test and
   `engage_end`, or EAX = EDX = 0.

12 branch sites, all both ways.

### 41d41c `face_off(A)` and 41d4e4 `flank_attack(A)`

cdecl, one slot; frame 18h: S. EBX = A, ESI/EDI the caller's up to the
out test.

1. `skirt_check`(A) (entry registers); ECX = A; AX ≠ 0: EAX = 0, its EDX.
2. `target_bearing`(A, S, 0) with EAX = `skirt_check`'s, EDX = S, ECX = A.
3. 41d41c: CX = 1 over `target_bearing`'s ECX, minus 2 (32-bit) when word
   S+4 ≥ 2000h; S+0Eh = CX; EAX = S; dwords S+10h = 3E80h, S+14h = 88B8h;
   `drive_apply`(A, S) (EDX = `target_bearing`'s).
   41d4e4: `flank_step`(A, S) with `target_bearing`'s EAX/EDX and ECX =
   S; `drive_apply`(A, S) with EAX = S and `flank_step`'s EDX/ECX.
4. `aim_select`(A, lo(EDX, word S+6), 0) with `drive_apply`'s EAX and
   ECX (the word goes through DX here, through AX in the other three).
5. `engage_done`(A) with `aim_select`'s registers; ECX = A; EAX ≠ 0 (32
   bits) or the out test: `engage_end`(A); otherwise EAX = EDX = 0.

41d41c: 8 branch sites, 41d4e4: 7, all both ways.

### 41def0 `drive_off(A)`

cdecl, one slot; frame 24h (unused by the live code). `skirt_check`(A)
with the entry registers (EBX = A, ESI/EDI the caller's); ECX = A; AX ≠
0: EAX = 0 with its EDX. Otherwise `face_off`(A) with `skirt_check`'s
EAX/EDX and ECX = A, and its EDX:EAX. **The rest of the body is
unreachable**: 41df16 jumps to the epilogue, and 41df1b..41e0d6 (a
second engagement variant with `actor_destination`, `dist2d_point`,
`heading_point`, `point_offset`, `vehicle_control`, the virtual +3Ch,
`aim_select` at 41e076 and `engage_done` at 41e07f) has no jump, call or
data reference from outside it (a branch scan of the full listing and a
raw scan of the file for every dword in that range). So the slot +3Ch
call at 41e06c (vtables.md's 41def0 row), the `aim_select` call 41e076
and the `engage_done` call 41e07f (mission_runtime.md) are dead code. 1 branch site, both
ways.

### Stack residue and the entry shims of the combat steps

All six steps reach the fire path: five call `aim_select` (41ea7c), whose
`aim_fire` reaches the weapon bodies that read stack bytes no instruction
of theirs wrote ([Stack residue under aim_fire](#stack-residue-under-aim_fire)), and 41def0 calls 41d41c. A replaced step
that called through the host-stack bridge would move `aim_select`,
`aim_fire`, `torso_aim` and the bodies to another coordinate and fill the
region below with host frames. Each step therefore has an entry shim like
`retreat_fire`'s (one macro in mech_combat_hooks.h): PUSH EBP keeps the
original's EBP; the caller's EBX/ESI/EDI go to the original's save slots
(41c594: EBP−34h/−38h/−3Ch; the four 18h-frame steps: EBP−1Ch/−20h/−24h;
41def0: EBP−28h/−2Ch/−30h); the guard-probed pages are counted in EBX
(the `aim_select`/`retreat_fire` shims count in EAX, which is dead at their entries; here the
entry EAX reaches `skirt_check`, which ignores it, but the shim keeps it
exact) and EBX is reloaded from its slot before the dispatch; the context
{EBP, outgoing ESP = the EDI slot} is published for the adapter. The
adapter puts the core's frame at the original's (P/L/S from EBP−30h, S at
EBP−18h), reads the caller's ESI and EDI back from their save slots and
runs every game call through `ai_behaviour_stack_call` at the original
outgoing ESP with the EBX/ESI/EDI the core names, the original's EBP and
the original's return address left below the arguments (41c5a9, 41c5c8,
41c63e, 41c697, 41c6bd, 41c6cd, 41c6d6, 41c71f; 41c87e, 41c897, 41c8c3,
41c8de, 41c8ee, 41c914, 41c9c0; 41c9e4, 41ca39, 41ca49, 41cab0; 41d42e,
41d447, 41d475, 41d485, 41d48e, 41d4d7; 41d4f6, 41d50f, 41d51c, 41d529,
41d539, 41d542, 41d58b; 41df02, 41df15).

Gaps: 72 pages (288 KiB) for the five `aim_select` callers, as
`retreat_fire` (`aim_select`'s 68 plus its host frames), 74 pages (296
KiB) for 41def0 (41d41c's 72 plus its host frames, which stay well inside
two pages). The member trampolines 415afc/415b38/415b74 have 80-page
shims (actor_step_hooks.h); nested shims move their host frames to the
shared stack floor ([main_loop.md](main_loop.md#shim-stack-budget)), so a
415b38 → 41def0 → 41d41c → 41ea7c → 41f5a0 chain with every shim active
keeps the outer host frames intact.
Without the shim (direct and legacy calls) the adapters use the plain
bridge and their own frame. `engage_end` (a leaf) and `skirt_check`
(`script_start`, `dist_gate`: no fire path) use the plain bridge.

**Accepted difference** (as under `aim_fire`): a replaced callee entered
through `ai_behaviour_stack_call` saves the bridge's flags with its
framework's PUSHFD, while the original's would be those of the
instructions before its CALL. In vitro this shows in the real-callee pass
as the lowest dword of the 64-byte window below an 18h-frame step's
argument slots (the PUSHFD slot of a replaced one-slot callee); the
comparison skips exactly that dword there. Nothing reads it.

### Shared properties of the combat states

- **Entry registers.** The entry EAX/EDX/ECX of 41c594, 41c86c, 41d41c,
  41d4e4 and 41def0 reach `skirt_check` (which reads only EDX/ECX: on its
  zero path as the result EDX, on the other as `script_start`'s
  registers), those of 41c9cc `target_bearing` (EDX/ECX); 41c478
  returns the entry EDX in states other than 4. The adapters forward
  them; the shims keep all three.
- **Callee-saved registers.** The steps keep the caller's ESI/EDI in
  those registers up to their own use (41c594 loads EDI before its first
  call); the shims save them where the original's prologue does and the
  cores name them per call.
- **ECX after return** is the framework's entry ECX: 415b38 and 41def0
  pop ECX right after the call, so it is dead. EDX is reproduced exactly.
- **Frames.** No frame local is read before it is written:
  `target_bearing` writes all of S, the rest is stored first.
- **Preconditions** (the original faults): A has a target where the step
  reaches `target_bearing`, `engage_done` or a target field; T's
  configuration pointer is valid (41c86c).
- **Divisions, draw, sound:** none.

### Validation of the MECH combat states

`tools/reference-game/invitro/mech_combat_cases.h`, group `mech_combat`
(`differential_harness <hook>`; identifiers `mcs_`):

- 1,001,024 direct cases per hook (quick tier 100,000) against logging
  stubs for every direct game callee of the hook under test (ordered
  slots, entry EAX/EDX/ECX with stack addresses masked; S, L, P and the
  heading points by content). The arena: six actors (each with a target,
  a group, a state record and a configuration; order state 4 with and
  without an order target equal to the target; the out bytes of A and T;
  byte A+0ADh), two groups, four configurations (word +2Eh 0 or not), the
  vtable with +3Ch, +30h, +54h, +58h. `target_bearing` writes S on the
  edges of every comparison (magnitudes 1FFFh..2001h, 4000h, 7060h/7061h,
  7CFh..7D1h, 1F3Fh..1F41h; distances 88B7h..88B9h, 249EFh..249F1h and
  the sign bit); `engage_done` returns zero, a nonzero low word or only a
  high word; the stubs change what the originals reread (A's target after
  `skirt_check`, `target_bearing` and `aim_select`; the order state after
  `aim_select`; S+6 and S+0Ah after `drive_apply`; the state record and
  B+0 inside `script_start`). Compared: the pair, the arena, the stagger
  word 4a9b70h, and the callee counters in the first 4,096 cases.
- 256 installed 31/a7 poison cases per hook (fast and preserving
  dispatch, live x87 stack, distinct caller EBX/ESI/EDI); for the six
  shim hooks the stubs also record EBX, ESI, EDI, EBP, ESP and every
  stack-address argument or register relative to the caller's argument
  slots, and the 64 bytes below the slots are compared after return.
- 3,072 real-callee cases per hook but `engage_end`, nested hooks in
  rotating modes and the hook itself original or replaced: `skirt_check`
  (`script_start`, `dist_gate`); the steps with `skirt_check`,
  `script_start`, `dist_gate`, `target_bearing`, `heading_point`,
  `drive_apply`, `aim_select` (its shim stacked on theirs), `engage_end`,
  and as they call them `point_offset`, `flank_step`, `timer_word_step`,
  `engage_done`; `drive_off` also `face_off` (three shims stacked). Stubs
  that stay: `vehicle_control`, `aim_target`, `aim_fire`, `target_class`,
  `order_target` and the arena virtuals; in a quarter of the cases the
  `aim_target`/`aim_fire` stubs fill 65 pages below their entry (a model
  of a replaced `aim_fire`'s 64-page gap and host frames), which must not
  reach any suspended host frame of the stacked shims.
- Coverage: 58 branch sites, all both ways in the instrumented
  prefix; none proved one-way. 41def0's unreachable tail has no sites.

Mutation witnesses
([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)):
eleven, at least one per hook, three of them on the shim adapters.

## MECH notices

Four hooks of the group `mech_notices` (core `core/src/mech_notices.c`,
`es2/core/mech_notices.h`; adapter `proxy/mech_notices_hooks.h`). P =
`[4a9b84h]`, the player's actor, read again where the original reads it
again. The radio post 420a98 is the hook `radio_notice`.

| Hook | VA | Slot (MECH table 49a282h) | Role (hypothesis) | Callers | Map probes (a60/c1s/c1c/c3_09/c4_08/c3_10) |
|---|---|---|---|---|---|
| `mech_contact` | 412800 | +48h (A, B) | contact notice: the per-index "reported" byte 4a9b00h[B+4Bh] and, at most once per message-timer period, radio code 1 | `contact_scan` 4128f8 (4129e5, 412b20) | 330/940/431/1000/112/662 |
| `mech_kill` | 415710 | +60h (K, V, w) | kill credit of K for V: radio 2, kill word K+2A4h[type of V], V's wingman radio 4/25h, the player's kill count 4a9e84h | 404ec1 (`building_part`), 41824e and 418515 (`vehicle_part`), 421c1b (FLYER +74h 421bb4) | 0/3/0/22/4/5 |
| `mech_attacked` | 41f7b8 | +50h (A, S, w) | attacked-by notice: the alert, contact share, damage sum, leader's radio 3, retarget and the attack script | the sweep 426528 at 426801 | 4/77/34/803/74/25 |
| `mech_alert` | 41f790 | – (A) | the "under attack" radio code 8, at most once per 40,000-tick timer A+278h | 41f7e8 (in 41f7b8), 4267f2 (the sweep) | 0/0/0/43/0/25 |

**Call sites.** A raw scan of the image finds 412800, 415710
and 41f7b8 once each, in the MECH table (file offsets 994cah, 994e2h,
994d2h = slots +48h, +60h, +50h); 41f790 has no pointer and two direct
callers. The other indirect +48h/+50h/+60h sites take their receiver
from weapon arrays or other objects (4108c6/4108ce, 410cb6 and 40fc15:
`[[ebx+4]+4·i]` of a weapon system; 4109a8, 410eb3, 4110ea: weapon code;
4665b3, 4403bc, 4654aa, 46564f: other classes;
[vtables.md](vtables.md#24-mech-0x49a282)); 410cb6 even pushes a word
where 41f7b8 would dereference S.

### 412800 `mech_contact(A, B)`

cdecl, two slots; EBX = A, ESI = B. Frame 10h: copies of B's index word
and of the row bases; no callee gets their address.

1. Byte 499358h = 0: increment it (a function-static first-call byte).
2. EAX = A+92h. Byte A+0A3h set: EDX = sx(word B+4Bh), byte
   4a9b00h+EDX = 1, return (ECX = the entry ECX).
3. `timer_word_step`(4a9b64h) with EAX = A+92h, EDX/ECX entry; ECX =
   4a9b64h after the POP.
4. Word B+1A8h = 1: return the timer's EDX:EAX.
5. ECX = P, EAX = `[P+45h]`; ≠ `[A+45h]` (A not in the player's group):
   return.
6. AX = word B+4Bh (EAX keeps the group's high word), EDX = sx; byte
   4a9b00h+EDX set: return. EDX = B+92h; byte B+95h set: return.
7. ECX = lo(ECX, word B+4Bh), then sx, EDX = A+0C2h; byte A+0C2h+ECX
   set: return. The same with EDX = `[4a9b84h]`+0C2h (P read again):
   return when set.
8. EAX = sx(AX of step 6); byte 4a9b00h+EAX = 1. Word 4a9b65h (the
   timer's word, after the call of step 3) ≠ 0: return.
9. 420a98(A, 1, 0) with EAX = the index, EDX = P+0C2h, ECX = the index;
   then word 4a9b65h = 10000. Return 420a98's EDX:EAX.

Every read is in the original's order; the index word is read three
times. A negative index reads and writes before the arrays (no bounds
check); the core does the same.

### 415710 `mech_kill(K, V, w)`

cdecl, three slots (w read as a word); ESI = K, EBX = V. No frame.

1. w ≠ 0 goes to 4. EDX = P, ECX = `[P+45h]`, EAX = `[K+45h]`. Equal
   (K in the player's group): ECX = `[V+45h]`, EAX = sx8(byte
   `[EAX+12h]`), ECX = sx8(byte `[ECX+12h]`) (the side bytes); the words
   differ and P ≠ K: 420a98(K, 2, 0) with these registers.
2. EDX = `[V+45h]`, EAX = sx8 of V's side, EDX = `[K+45h]`, ECX = sx8 of
   K's side (all read again). Equal: go to 4.
3. EAX = sx(word V+1A8h); word K+2A4h+2·EAX += 1; EAX = K+92h; byte
   K+0A6h = 1.
4. EAX = P (read again), EDX = `[P+45h]`; ≠ `[V+45h]` or P = V: return.
5. EAX = V, CX = 25h (ECX keeps its high word: the entry ECX on the
   w ≠ 0 path, else step 1's or 420a98's), EDX = V+92h; byte V+99h clear:
   ECX −= 21h (CX = 4). EDX = ECX; 420a98(V, ECX, 1): the whole dword is
   the slot (420a98 reads its word). K = P (read again): word 4a9e84h +=
   1. Return 420a98's EDX:EAX.

### 41f790 `mech_alert(A)`

cdecl, one slot. EAX = A. Dword A+278h = 0: A+278h = 40000, then
420a98(A, 8, 0) with EAX = A, EDX/ECX entry; return its EDX:EAX. Else
return EAX = A, EDX entry.

### 41f7b8 `mech_attacked(A, S, w)`

cdecl, three slots (w read as a word); EBX = A, ESI = S (later the new
target), EDI = f, later the leader L. Frame 0Ch: copies only.

1. EAX = P, EDX = `[P+45h]`; f = ECX = (EDX = `[A+45h]`) (SETE, AND 1:
   the whole ECX is 0 or 1). S = `[4a9b84h]` (read again) and f:
   `mech_alert`(A) with EAX = P, EDX = `[P+45h]`, ECX = 1; ECX = A after
   the POP.
2. EAX = S+92h. Byte S+0A5h set: return (ECX = 1). EDX = S+92h; byte
   S+0A4h set: EDX = 1, return. ECX = EAX = S+92h; byte S+99h set: EDX
   = 1, return. Otherwise EDX = ECX = 0.
3. EAX = `[S+45h]`, ECX = `[A+45h]`, EDX = sx8 of S's side, EAX = sx8 of
   A's side; equal words: return.
4. 411aec(A, S, 0) (the ACTOR +50h default: `contact_share`(A, S)) with
   these registers. Dword A+27Dh = 30000.
5. f: EDX = sx(w); dword A+281h += EDX; ECX = A+281h; signed < 8000:
   return (EDX = sx(w)).
6. EAX = A+92h; byte A+0A3h set or dword A+273h ≠ 0: return. S =
   `[A+1A4h]`: EAX = `[A+4Dh]`+8; byte EAX+1 set: return.
7. EDX = `[A+45h]`, ECX = `[EDX+0Ch]`, L = `[ECX]` (the leader); dword
   A+273h = 10000. EAX = `[A+4Dh]`+8; byte +1 clear: EAX = L+92h; byte
   L+0A3h set: word S+1A8h = 0 → 420a98(A, 3, 0) with EAX = L+92h, EDX
   = the group, ECX = its list; then S = `[L+1A4h]` → byte A+9Ah = 0
   (EAX = A+92h).
8. EAX = `[A+4Dh]`+8 (read again). Byte +2 set:
   `actor_destination`(A) (EAX = that pointer, EDX/ECX as left; ECX = A
   after the POP), N = `target_search`(A, its EAX) (ECX = A). N = 0 or
   `[N+1A4h]` ≠ A, and word A+23Eh = 4: N = `[A+248h]`. N ≠ 0: S = N.
   The target switch to S (ECX = the old target for the decrement, then
   the new one, EDX = A+92h; skipped when S is already the target). Word
   S+1A8h = 0: `script_start`(A+4Dh, 499784h) (EAX = A+4Dh) and
   `part_pick`(A) with `script_start`'s registers; else
   `attack_target`(A, S) (EAX = S). Return the last callee's EDX:EAX.
9. Otherwise EAX = `[A+4Dh]`+8 (read again); byte +3 set: return. old =
   `[A+1A4h]`; N = `target_select`(A, 24h, 0) (EAX = that pointer,
   EDX/ECX as left by step 7). N = 0 or `[N+1A4h]` ≠ A, and word A+23Eh =
   4: with O = `[A+248h]` ≠ 0 and O not out (bytes O+0A4h, O+99h; EDX = 1
   when out, else 0, ECX = O+92h on the second test) N = `[A+248h]` (read
   a third time); otherwise word A+23Eh = 0. The switch to N (EDX = N;
   ECX = the old target, EAX = the new one read back; afterwards ECX =
   EAX = A+92h). `[A+1A4h]` ≠ 0: byte A+0ACh = 1, `attack_replan`(A)
   (EAX = A+92h); else old ≠ 0: `order_resume`(A). Return the last
   EDX:EAX.

One edge is proved unreachable: the taken edge of 41f979 (the new target
is zero). EAX = ESI there, and ESI is S, which step 2 dereferences (S = 0
faults in the original), or a nonzero N (41f94b/41f94f). 59 branch sites
in all (9 in 412800, 9 in 415710, 40 in 41f7b8, 1 in 41f790), 58 both
ways.

### Result registers at the callers

The replacements return the original's EDX:EAX exactly; ECX after return
is the framework's (the entry ECX) and is dead at every caller:

- +48h at 4129e5: ECX is written at 4129eb, EAX at 4129f4; EDX can leave
  `contact_scan` unchanged when its loop ends (412a05 → 412a1f → 412b8c →
  412ba8), so it is reproduced. At 412b20: EAX written at 412b26; on the
  412b4b path `contact_share` 412704 is entered with the +48h ECX and
  EDX: it writes EDX at 41271d and returns ECX unchanged on its 412724
  exit, into V3c at 412b58, whose implementations (415488, 411a5c,
  403594) do not read ECX; 412b5e pops ECX.
- +60h at 404ec1, 41824e, 418515, 421c1b: EAX/EDX are written before
  they are read or are entry registers of `death_actions` 411bc8, which
  writes EAX (411bd1) and EDX (411be8) first; ECX reaches 411bc8, which
  saves it with PUSH ECX and restores it (its high word possibly
  rewritten at 411c31), and every caller pops ECX after 411bc8.
- +50h at 426801: ECX written at 42680a; EDX:EAX reproduced.
- 41f790 at 41f7e8 and 4267f2: both pop ECX; after 4267f2 the EDX reaches
  the +50h call at 426801, whose implementations (41f7b8, 411aec →
  412704) write EDX before reading it.

### Callees, the fire path and stack residue

Every callee goes through the guest bridge with the registers above, so
nested hooks keep their modes: `timer_word_step`, `actor_destination`,
`target_search`, `script_start`, `part_pick`, `attack_target`,
`target_select`, `attack_replan`, `order_resume`, `mech_alert` (under
`mech_attacked`), `radio_notice` 420a98 (the cockpit message post) and
`actor_attacked` 411aec (the ACTOR +50h default). The
`pop ecx` after a one-slot call reloads the pushed value: none of 467a48,
41f790, 41dbcc, 41ce08, 41d5ec, 41eb34 writes its argument slot
(checked in the listing).

A direct-call walk from 412800, 415710 and 41f7b8 (49 functions,
jump-table cases included) reaches none of 41ea7c, 41e984, 41f5a0,
41ef14 or the weapon bodies 40e788/40e964/40ea04/40ea58/40ec64; its
indirect calls are the actor virtuals +40h/+4Ch (in `target_select`,
`attack_plan`, 41cb94), the message port's virtual 0 (420a98,
`trigger_fire`) and the runtime. So no residue is read on a fire path
and the plain bridge is used, as for `attack_plan`/`attack_replan`. No local of the four is read
before it is written and no entry register is read beyond EDX/ECX
forwarded to the first callee (412800, 41f790), so there is no entry
shim.

Remaining residue: 420a98 builds its message record (code word, actor)
at its EBP−18h and passes it to the port, whose receive slot copies
record bytes +2..+15h into the cockpit message queue
([mission_runtime.md](mission_runtime.md), the pattern of
`trigger_fire`); bytes +6..+15h are uninitialized stack below the
caller's outgoing ESP. With a replaced notice they hold host-frame bytes
instead of DBSIM residue. They reach only the cockpit queue
(presentation state, not compared by the oracle) — accepted as for
`weapon_choose` → `trigger_fire` and `mech_order` → 420a98
([mech_order.md](mech_order.md)). The same holds for `trigger_fire` reached from 411aec through
`contact_share`.

### Shared properties of the notices

- **Global writes:** 499358h (first-call byte), 4a9b00h[index] (seen
  bytes), 4a9b65h (message timer word, also through `timer_word_step`),
  4a9e84h (the player's kill count). Object writes: A+27Dh, +281h,
  +273h, +278h, +9Ah, +9Dh, +0ACh, +1A4h, +23Eh, the targets' +1A2h, K's
  kill words +2A4h[] and +0A6h.
- **RNG, clock, divisions, draw, sound:** none directly; whatever the
  callees do runs in them.
- **Preconditions** (the original faults otherwise): the group pointer
  +45h, the leader list and the state pointer +4Dh are valid where read;
  S ≠ 0 in 41f7b8; P is an actor.

### Validation of the MECH notices

`tools/reference-game/invitro/mech_notices_cases.h`, group
`mech_notices` (`differential_harness <hook>`):

- 1,001,024 direct cases per hook (quick tier 100,000; the first 16,384
  instrumented) against logging stubs for all twelve callee addresses
  (41f790 only under `mech_attacked`): ordered argument slots and entry
  EAX/EDX/ECX (stack values masked). The arena has eight actors (groups,
  state records, flag bytes, index words −2Ch..67h, type words also
  negative, targets, order states, the timers +273h/+278h and the damage
  sum +281h around 8000), four groups with leader lists and side bytes,
  four state records. The stubs return 0 or an actor where the result is
  dereferenced (and let it attack A), set the timer word expired or not,
  and in one call of four poke fields the originals read again (flags,
  groups, sides, index and type words, targets, order state, timers, the
  player pointer, the seen bytes). Compared: the pair, the arena, the
  global windows 499358h, 4a9a80h..4a9b87h, 4a9e84h, 49b088h, 4d3b64h and
  the callee counters (first 4,096 cases).
- 256 installed 31/a7 poison cases per hook (fast and preserving
  dispatch, nonempty x87).
- 3,072 real-callee cases per hook: the real 420a98 with a cockpit whose
  message port's virtual 0 is a stub that records the message (code
  word, actor) by content; `timer_word_step` (`mech_contact`),
  `mech_alert` and `script_start` (`mech_attacked`) nested in rotating
  modes, the hook original or replaced; under `mech_attacked` the large
  subtrees (411aec, 41dbcc, 41e0e0, 41ce08, 41c0f4, 411fa0, 41d5ec,
  41eb34) stay stubs.
- Coverage: 59 branch sites, 58 both ways in the instrumented prefix,
  41f979 PROVED one-way.

Mutation witnesses: six, at least one per hook
([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).

## MECH travel, follow, sleep, ram and dead states

Nine hooks of the group `mech_travel` (core `core/src/mech_travel.c`,
`es2/core/mech_travel.h`; adapter `proxy/mech_travel_hooks.h`). The
state names are those of
[vtables.md](vtables.md#31-mech-states-0x4993a4-stride-0x3e); the data
references come from a raw scan of the file for the function addresses
and the call graph of the DBSIM map.

**Where they are reached.** The six state methods occur in the
image only as dwords of the MECH constant groups that `0x413ed4` copies into
the state records (group k at `0x4998f8 + 24h·k`; its first dword the
`+18h` behaviour, the dword at +0Ch the `+24h` movement):

| Hook | VA | State (group k, record) | Member | Called by |
|---|---|---|---|---|
| `travel_step` | 41d9cc | travelling (9, `0x4995d2`), bulldog travel (11, `0x49964e`) | +18h | `mech_act` 415b38 |
| `follow_step` | 41daac | following (10, `0x499610`) | +18h | `mech_act` |
| `sleep_step` | 41c418 | sleeping (13, `0x4996ca`) | +18h | `mech_act` |
| `ram_step` | 41e570 | ramming (17, `0x4997c2`) | +18h | `mech_act` |
| `ram_move` | 41e488 | ramming | +24h | `mech_move` 415afc; `ram_step` (41e636, 41e681) |
| `dead_step` | 41e554 | dead (20, `0x49987c`), disabled (21, `0x4998ba`) | +18h | `mech_act` |
| `leader_route` | 41fb60 | – | – | `group_move` 41d598 (41d5c8), `travel_step` (41d9d9) |
| `point_move` | 41fac4 | – | – | `group_move` (41d5b9), `follow_step` (41daf8), `hunt` (41e41a), `leader_route` (41fb85) |
| `fire_permit` | 41c3c8 | – | – | `sleep_step` (41c46d), `group_move` (41d5d8), `hunt` (41e40a, 41e46e) |

A call scan of the whole listing finds no other direct caller; the raw
scan finds no other dword equal to any of the nine addresses. The
trampolines enter a member with EAX = the state record, EDX = the third
member dword, ECX = the OR of the three dwords and EBX = A + delta
(vtables.md; every delta and third dword in the retail tables is
0); the hooks accept exactly these.

Map entry probes (a60/c1s/c1c/c3_09/c4_08/c3_10, 1000 = at least 1000):
41d9cc 0/0/0/0/1000/0, 41e554 592/1000/1000/1000/685/1000, 41fb60
106/844/212/633/1000/841, 41fac4 106/844/212/1000/1000/841, 41c3c8
1000 ×6; 41daac, 41c418, 41e570 and 41e488 have no probe row (liveness
unknown, not zero), so the in-vitro cases carry their evidence.

### 41c3c8 `fire_permit(A)`

cdecl, one slot; `push ecx` makes a local EBP−4, saves EBX/ESI. L =
`[[G+0Ch]]` (the group leader, G read from A). Byte L+0A3h set: byte A+96h
= (byte A+0B2h ≠ 0); clear (`add ecx,-1Bh` turns CX = 20h into 5): byte
A+96h = (byte A+97h ≠ 0). Returns EAX = EDX = F; ECX = F (the local, POP
ECX). The entry ECX's high word passes through ESI only (restored). No
entry register is read. 2 branch sites (41c3e7, 41c401), both ways.

### 41e554 `dead_step(A)`

`vehicle_control`(A, 0, 0, 0) with EAX = A, EDX/ECX = entry; returns EAX
= 0, EDX = vehicle_control's (ECX vehicle_control's; no POP). No branch.

### 41c418 `sleep_step(A)`

1. The inlined target drop (identical in 41d9cc and 41daac): EDX = T; T ≠
   0: ECX = T read again, T's attacker word +1A2h −1, EDX = 0, `[A+1A4h]`
   = 0, EAX = F, byte A+9Dh = 1. The copy re-tests EDX at once (41c42b
   never jumps) and then tests the XOR-zeroed EDX before an increment of
   the new target's word (41c444 always jumps): both PROVED.
2. `vehicle_control`(A, 0, 0, 0) with EAX = F or the entry EAX, EDX = 0,
   ECX = T or the entry ECX.
3. `fire_permit`(A) with vehicle_control's registers (no POP before it);
   ECX = A after the POP. Result EAX = 0, EDX = fire_permit's.

3 branch sites, 41c427 both ways, two PROVED.

### 41fac4 `point_move(A, P)`

cdecl, two slots (A, a point P); `push ecx` makes a local EBP−4 (a copy of
P, never read back); EBX = A, ESI = P then the distance, EDI = P then the
heading, then the turn.

1. h = `heading_point`(P, A+26h) (EAX = A+26h, EDX/ECX entry) with the
   low word − word A+10h (16-bit, the high word stays heading_point's).
2. d = `dist2d_point`(A+26h, P) (EAX = h, EDX = P, ECX = A+26h).
3. turn = sar(sx(h), 6) (32-bit). speed = lo(turn, word A+252h), or
   lo(turn, AAh) when that word is 0.
4. Word A+23Eh ≠ 0 and s32 d > 30000: when `[A+317h]` ≠ 0, `turbo_engage`
   of it ([below](#turbo-engage-40f09c); EAX = the pointer, EDX/ECX
   dist2d_point's; ECX = the pointer after the POP); speed = lo(that EAX,
   100h), or 100h when the pointer is 0.
5. `vehicle_control`(A, turn, speed, 0) with EAX = speed. Result EAX = 1
   when s32 d < 10000 (`setl`, `and eax,1`), else 0; EDX =
   vehicle_control's; ECX = P (the local, POP ECX).

4 branch sites (41fb0b, 41fb22, 41fb2a, 41fb34), both ways.

### 41fb60 `leader_route(A)`

H = G+4 is the group's route holder (index word H, route pointer H+2;
mission_runtime.md, `route_point`). EBX = A, ESI = H.

1. P = `route_point`(H, lo(entry EAX, word H) + 1) — `mov ax,[esi]; inc
   eax`, a 32-bit increment, so the entry EAX's high word (the trampoline's
   state pointer under 41d9cc) reaches the index slot and EAX, plus one on a
   carry out of the word; EDX/ECX entry.
2. P = 0: `vehicle_control`(A, 0, 0, 0) (EAX = 0), return its EDX:EAX.
3. `point_move`(A, P) (EAX = P); AX = 0: return its EDX:EAX. Else
   `route_advance`(`[A+45h]`+4, G read again; EDX/ECX point_move's), ECX =
   that after the POP, return its EDX:EAX.

2 branch sites (41fb81, 41fb90), both ways.

### 41e488 `ram_move(A)`

Frame 34h, of which only Q = EBP−0Ch (3 dwords) is written; EBX = A, ESI =
A+26h from the second call on.

1. `ground_animate`(A) with the entry registers (it pushes lo(its entry
   EAX, word A+2A0h) as a slot of `track_rate`, so the entry EAX's high
   word is passed on); ECX = A.
2. Q ← A+26h (x, y, z in that read order, the terrain map pointer
   `[4a0be0]` read between y and z); h = `terrain_height`(map, Q) (EAX = z,
   EDX = Q, ECX = map). Q.z = h + sx(word `[[A+1F2h]+16h]`); A+2Eh ← Q.z,
   A+26h ← Q.x, A+2Ah ← Q.y (this order).
3. `ground_effects`(A) (EAX = A+26h, EDX = Q.y, ECX = Q.x), ECX = A;
   `ground_collide`(A), ECX = A.
4. AX = 0 and byte A+0B1h clear (EAX = F): return EAX = lo(F, 1), EDX =
   ground_collide's.
5. Otherwise `radius_hit`(A+26h, 3000, 2000, 0, A) (ECX = A+26h, EAX
   ground_collide's or F); then for i = 0 … 1Ch with the parts pointer
   `[A+20Eh]` read once and each word `[parts + 2i]` read in its round: a
   nonzero word gives A.+74h(A, i, 7D00h, A) (EAX = `[A]` read each time,
   EDX/ECX the previous call's). Result EAX = lo(last EAX, 1) (AX = 1),
   EDX the last call's.

4 branch sites (41e4f8, 41e504, 41e52b, 41e545), both ways.

### 41e570 `ram_step(A)`

Frame 0Ch (the three steering steps store T+26h there and read it back
into EDX); EBX = A, ESI = B, EDI = T+26h. B+5 is the charge word (1 = charging), B+7 the charge/rest timer
record (word B+8), B+1 the scan timer.

1. `timer_dword_step`(B) with the entry registers; ECX = B. Expired (EAX =
   0): N = `target_select`(A, 6, 0) (EAX = 1, EDX the timer's, ECX = B);
   N ≠ T: T's word −1 (T ≠ 0; ECX = T read again), `[A+1A4h]` = N, N's word
   +1 (N ≠ 0; ECX = N read back), EDX = F, byte A+9Dh = 1; then dword B+1
   = 10000.
2. `[A+1A4h]` = 0: `vehicle_control`(A, 0, 0, 1); return EAX = 0, EDX its.
3. A steering step: `heading_point`(T+26h, A+26h) with 100h and 1 already
   pushed above (EAX = A+26h, EDX = T+26h, T read again), then
   `vehicle_control`(A, sar(sx(lo(h, h − word A+10h)), 8), 100h, 1) (EAX =
   that heading, ECX = the turn).
4. Word B+5 = 0 (resting): `timer_word_step`(B+7) (EAX/EDX
   vehicle_control's, ECX = B+7); expired (AX = 0): word B+5 = 1 and word
   B+8 = lo(`rng_bounded`(4d2599h, 5DCh), AX + BB8h) (EAX = 1, EDX the
   timer's): the next charge after 3000 + rng(1500). Return EAX = 0, EDX
   the last call's.
5. Charging: `ram_move`(A) with vehicle_control's registers; T read again =
   0 ends (ECX = A after the POP); a second steering step; `ram_move`
   again; T test; a third steering step; then `timer_word_step`(B+7) (EAX =
   B+7): EDX = (AX = 0) as `sete dl; and edx,1`; running: return EAX = 0,
   EDX = 0; expired: `rng_bounded`(4d2599h, FA0h) (EAX the timer's, EDX = 1,
   ECX = B+7), word B+5 = 0, word B+8 = AX + 1F40h (rest 8000 +
   rng(4000)); return EAX = 0, EDX rng's.

So a charging MECH steers three times and moves two extra ground steps per
frame (its own `+24h` movement is `ram_move` as well). 10 branch sites, all
both ways.

### 41d9cc `travel_step(A)`

Frame: EBX/ESI/EDI saved at EBP−4/−8/−0Ch, no locals; EBX = A, ESI = B
(V from 6 on), EDI = the caller's EDI (A+26h from 6 on).

1. `leader_route`(A) with the entry EAX/EDX/ECX; ECX = A.
2. The target drop (as 41c418; 41d9eb and 41da04 PROVED).
3. `timer_dword_step`(B) (EAX = F or leader_route's, EDX = 0, ECX = T or
   A); ECX = B; EAX = 1 when it returned 0, else 0.
4. Expired: dword B+5 (the remembered target, A+5Fh) = `target_select`(A,
   0, 0) (EAX = 1, EDX the timer's, ECX = B), dword B+1 = 10000; byte
   A+96h = 1 when B+5 (read again) ≠ 0, else = (byte A+97h ≠ 0); EAX = F.
5. V = dword B+5 (read again) = 0: return EAX = 0, EDX as left.
6. `heading_point`(A+26h, V+26h) with V pushed first as a third slot (EAX =
   V+26h); a = lo(h, h − word V+10h); V.+3Ch(V, a) (EAX = a, EDX = `[V]`,
   ECX heading_point's; MECH 415488, BASE family 403594/411a5c);
   `aim_select`(A, lo(a, a + AX of +3Ch), V) with +3Ch's EAX/EDX, ECX = that
   word (the two POPs reload a). Return EAX = 0, EDX aim_select's.

7 branch sites, five both ways, two PROVED. Return addresses: 41d9de,
41da23, 41da3b, 41da85, 41da93, 41da9f.

### 41daac `follow_step(A)`

Frame: `push ecx` makes the local L = EBP−4 (the entry ECX until step 1);
EBX/ESI/EDI saved at EBP−8/−0Ch/−10h.

1. P = `order_actor`(G) (EAX = G, EDX/ECX entry; mission_runtime.md: the
   actor of the group's current order, or its leader); ECX = G; EDI = P,
   L = P.
2. d = `dist_gate`(A+26h, P+26h) (EAX = P+26h read back from L, EDX =
   A+26h, ECX = G). s32 d < 25000: `vehicle_control`(A, 0, 0, 0) (EAX = d);
   else EDI = P+26h and `point_move`(A, P+26h) (EAX = d); EDX/ECX
   dist_gate's.
3. From the target drop on exactly as 41d9cc 2–6 (41db0c and 41db25
   PROVED; EDI is P or P+26h up to the aim).
4. Return EAX = 0, EDX as left; ECX = P (L, POP ECX).

Precondition (the original faults): the group has an order actor (P is
dereferenced through `dist_gate` and `point_move`). 8 branch sites, six
both ways, two PROVED. Return addresses: 41dac2, 41dad8, 41daee, 41dafd,
41db44, 41db5c, 41dba6, 41dbb4, 41dbc0.

### Stack coordinate and the entry shims of travel_step and follow_step

`travel_step` and `follow_step` call `aim_select`, whose `aim_fire` reaches
the weapon bodies that read uninitialized stack
([Stack residue under aim_fire](#stack-residue-under-aim_fire)). Like
`retreat_fire` they have entry shims (`mech_travel_hooks.h`): PUSH EBP keeps the original's EBP; the caller's
EBX/ESI/EDI go to the original's save slots (41d9cc: EBP−4/−8/−0Ch;
41daac: EBP−8/−0Ch/−10h, and the entry ECX to the local EBP−4 as PUSH ECX
does); 72 guard-probed pages (288 KiB: `aim_select`'s 68 plus its host
frames, the budget of `retreat_fire`); the context {EBP, outgoing ESP
(EBP−0Ch, EBP−10h)} is published for the adapter. `travel_step`'s entry
EAX is live (step 1 above), so its shim keeps EAX in memory while EAX
counts the pages; `follow_step`'s EAX is dead at entry (41dab9 writes it).
EDX and ECX pass through. The adapters run every game call through
`ai_behaviour_stack_call` at the original outgoing ESP with the
EBX/ESI/EDI the core names, EBP and the original's return address left in
the slot below the arguments, and read the caller's EDI back from its save
slot; `follow_step`'s L is the original's EBP−4. Without the shim (direct
and legacy calls) the plain bridge and the adapter's own frame are used.
The nested gaps are 80 (trampoline) > 72 > 68 (`aim_select`) > 64
(`aim_fire`) pages; in vitro a 68-page travel gap crashes and 70 pages
pass. Nested shims move their host frames to the shared stack floor
([main_loop.md](main_loop.md#shim-stack-budget)).

The other seven reach no fire function: a direct-call walk over the map's
call graph (with the actor virtuals +3Ch, +5Ch, +70h and +74h of the actor
tables added) reaches from 41c3c8, 41e554, 41c418, 41fac4, 41fb60, 41e488
and 41e570 none of 41ea7c, 41e984, 41f5a0, 41ef14 or the weapon bodies;
their only readers of uninitialized stack are the terrain face test 46ff90
under `vehicle_control` (flags 3 only, which game terrain never has;
weapons.md) and, under `ram_move`'s `radius_hit` and slot +74h,
`trigger_fire`'s record bytes (cockpit message queue only). `vehicle_control`, `radius_hit` and `vehicle_part` already run
their subtrees from host frames under the replaced `hunt`, `group_move`,
`clear_shot` and the weapon hooks (live-identical). So these seven keep the
plain bridge, the treatment `hunt` and `group_move` got.

### Shared properties of the travel states

- **Entry registers.** Read before written and passed on: all three of
  41d9cc (into `leader_route`), EAX's high word of 41fb60 (the index slot)
  and of 41e488 (into `ground_animate`), EDX/ECX of 41daac
  (`order_actor`), 41e554 and 41c418 (`vehicle_control`), 41fac4
  (`heading_point`), 41fb60 (`route_point`) and 41e570
  (`timer_dword_step`, which ignores them and hands ECX back to a POP).
  The adapters forward them; the shims pass them. No frame local is read
  before it is written.
- **ECX after return** is outside the contract: every caller pops it
  (`mech_act`/`mech_move` after a member, 41d5cd, 41d9de, 41e642, 41e68d and
  the four POPs after 41c3c8) or, after 41fac4, overwrites it before a read
  or hands it only to the entries of 41c3c8, `timer_dword_step` and
  `route_advance` (none reads it) or back out through `hunt` to the
  trampoline's POP.
- **EDX after return** is reproduced exactly (the members' EDX goes through
  the trampoline to `actor_step`, where 413eb0 overwrites it; `hunt` returns
  `point_move`'s EDX).
- **Callee-saved registers.** Only the two shim hooks name EBX/ESI/EDI per
  call; the other seven run their callees from host frames like `hunt`.
- **Divisions:** none. **Draw:** none. **Sound:** `turbo_engage` (→
  `sfx_play`) under `point_move`, hooks behind the bridge; the SOS calls under
  `vehicle_control`, `ground_effects` and `ground_collide` are theirs.
- **Preconditions** (the original faults): `follow_step`'s group has an
  order actor; `travel_step`/`follow_step`'s remembered target B+5 and
  `ram_step`'s target are actors; `ram_move`'s configuration and parts
  pointers are valid.

### Validation of the MECH travel states

`tools/reference-game/invitro/mech_travel_cases.h`, group `mech_travel`
(`differential_harness <hook>`):

- 1,001,024 direct cases per hook (quick tier 100,000) against logging
  stubs for every direct game callee of the hook under test (22 callee
  addresses; ordered slots, entry EAX/EDX/ECX with stack addresses masked,
  `ram_move`'s point Q by content) and the virtuals +3Ch and +74h through
  two arena vtables. The arena holds six actors (flag bytes, targets and
  attacker words, remembered targets, positions, headings, speed and order
  words, sound records, configurations, part lists), two groups (member
  lists, route holders with index words up to FFFFh and null routes, order
  slots), eight orders, two routes over eight points and the terrain map
  header; the globals 4a0be0, RNG stream A and the frame time are compared
  and restored. Stubs return planned values where a comparison follows
  (timers zero or not, `dist_gate` on 25000 ± 1, `dist2d_point` on 10000
  and 30000 ± 1, `ground_collide`/`point_move`/`timer_word_step` AX zero or
  not, `route_point` 0 or a point) and change what the originals read again
  (A's target behind `leader_route`, `vehicle_control`, `point_move`,
  `ram_move` and `target_select`; the remembered target behind the timer;
  the group behind `point_move`; the headings behind `heading_point`; the
  position, configuration, collision byte, parts pointer and part words
  behind the ground and radius calls and slot +74h; A's vtable behind
  +74h). Callee counters in the first 4,096 cases.
- 256 installed 31/a7 poison cases per hook (fast and preserving dispatch,
  live x87); for the two shim hooks the stubs also record EBX/ESI/EDI and
  EBP/ESP relative to the caller's slots, and the 64 bytes below the slots
  are compared after return.
- 3,072 real-callee cases for seven hooks (not for the leaf `fire_permit`
  and for `dead_step`, whose only callee `vehicle_control` stays a stub: its
  subtree reaches the SOS calls), nested hooks in rotating modes and the
  hook itself original or replaced: `travel_step` (`leader_route`,
  `route_point`, `route_advance`, `point_move`, `heading_point`,
  `dist2d_point`, `timer_dword_step`, `aim_select` with its shim),
  `follow_step` (`order_actor`, `dist_gate`, `point_move`, `heading_point`,
  `dist2d_point`, `timer_dword_step`, `aim_select`), `sleep_step`
  (`fire_permit`), `ram_step` (`ram_move`, `terrain_height`,
  `timer_dword_step`, `timer_word_step`, `rng_bounded`, `heading_point`),
  `ram_move` (`terrain_height` on a map that answers 0 off the grid),
  `leader_route` (`route_point`, `route_advance`, `point_move`,
  `heading_point`, `dist2d_point`) and `point_move` (`heading_point`,
  `dist2d_point`). Stubs that stay: `target_select`, `vehicle_control`,
  `turbo_engage`, `aim_target`, `aim_fire`, the ground calls, `radius_hit` and the
  virtuals. For the two shim hooks every third rotation installs the
  nested hooks without a shim original (and `aim_select` original or
  replaced) and compares the registers at every stub entry and the frame
  below the arguments (elsewhere a replaced callee's framework leaves its
  entry flags there, the accepted difference of vtables.md); a quarter of
  their cases fill 264 KiB below the `aim_fire`/`aim_target` stubs, and one
  in eight enters through the MECH trampoline 415b38 (`mech_act` original
  or replaced) from a state record, so the 80-, 72- and 68-page shims nest.
- Coverage: 40 branch sites, 34 both ways in the instrumented prefix,
  six PROVED one-way (41c42b, 41c444, 41d9eb, 41da04, 41db0c, 41db25).
- Sensitivity: adapters that ignore the shim's context fail the installed
  probes at case 0 (EBX/ESI/EDI/EBP/ESP at the first stub).

Mutation witnesses
([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)):
eleven, at least one per hook, two of them on the shim adapters.

### Counter relations

Expected from the call graph (map entry probes above give the per-suite
counts): `hk_leader_route_calls` = `hk_group_move_call_41fb60` +
`hk_travel_step_call_41fb60`; `hk_point_move_calls` =
`hk_group_move_call_41fac4` + `hk_hunt_call_41fac4` +
`hk_follow_step_call_41fac4` + `hk_leader_route_call_41fac4`;
`hk_fire_permit_calls` = `hk_group_move_call_41c3c8` +
`hk_hunt_call_41c3c8` + `hk_sleep_step_call_41c3c8`; `hk_ram_move_calls` ≥
`hk_ram_step_call_41e488` (the rest through `mech_move`). Per hook:
`travel_step`: `call_41fb60` = `call_467aa8` = calls ≥ `call_411fa0`,
`call_492934` = `slot_3c` = `call_41ea7c`; `follow_step`: `call_4238a0` =
`call_49288c` = `call_467aa8` = calls = `call_416a04` + `call_41fac4`;
`sleep_step`: `call_416a04` = `call_41c3c8` = calls; `dead_step`:
`call_416a04` = calls; `leader_route`: `call_423b0c` = calls =
`call_416a04` + `call_41fac4`, `call_41fac4` ≥ `call_42313c`;
`point_move`: `call_492934` = `call_4928d0` = `call_416a04` = calls ≥
`call_40f09c`; `ram_move`: `call_418f40` = `call_46e180` = `call_4195c8` =
`call_418f74` = calls ≥ `call_426a20`, `slot_74` ≤ 29 × `call_426a20`;
`ram_step`: `call_467aa8` = calls ≥ `call_411fa0`.

## TURBO engage 40f09c

Hook `turbo_engage` (40f09c), group `cold_paths`, core
`core/src/mech_travel.c` (`es2_turbo_engage_registers`). T = the TURBO
equipment object `[A+317h]`; callers `point_move` (41fb37) and
`retreat_fire` (41d3a4).

- Not yet latched (dword T+81h = 0, site 40f0a9) and the charge dword
  T+7Dh above 600 (signed, 40f0b2): T+81h ← 1, and with the dword T+79h
  set (40f0c2) `sfx_play`(2Ch) (462830, a hook; entry EAX = T) through the
  bridge.
- Result: EAX = T or `sfx_play`'s, EDX the entry EDX or `sfx_play`'s; ECX
  after the sound is the popped slot, otherwise the entry ECX (restored;
  dead at both callers).
- Three conditional sites, all both ways in vitro.

**Live.** Entered in `c3_09-wingman-orders` and `c3_09-orders-matrix`
through the `point_move` site 41fb34 (the Colossus wingman carries TURBO
31), 0 elsewhere; with all hooks replaced every suite is identical
([oracle_suites.md](../reference-game/oracle_suites.md#c3_09-wingman-orders)).
