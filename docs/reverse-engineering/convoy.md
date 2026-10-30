# VEHICLE update chain (`0x46a6d4`, vtable `0x497818` slot `+0x18`)

The object update of the building-pool class `VEHICLE` of `DBSIM.EXE`
(reference binaries: [reference_version.md](../reference_version.md); image
base `0x400000`) and the functions it calls: the leader search, the steering
and route-following chain, and the collision test.

Evidence: an entry-aligned `llvm-objdump -d --x86-asm-syntax=intel` listing
of the reference `DBSIM.EXE`; vtable slots and Borland RTTI names read from
the image bytes (dword at `vtable − 12` → type descriptor, name offset at
`+6`, as in [hud.md](hud.md)). Probe counts come from the DBSIM map
(counts per suite in [docs/functions](../functions/dbsim-460000.md)). Roles named from the use
are hypotheses where marked.

## The class VEHICLE

| Table | RTTI name | Slot +18h | Note |
|---|---|---|---|
| `0x497818` | `VEHICLE` (descriptor `0x406b67`) | `0x46a6d4` | the chain of this document |
| `0x4978ac` | `GUN_BASE` | `0x404100` (`turret`) | VEHICLE's direct base |
| `0x497940` | `BASE` | `0x403ca8` | GUN_BASE's base |
| `0x4979d4` | `RADAR_BASE` | `0x403ca8` | |
| `0x497784` | `LC_BASE` | `0x405038` | |

The 34-slot tables of `0x497818` and `0x4978ac` differ only in slots +08h
(`0x406d05`/`0x406d5c`) and +18h; slot +3Ch is `0x403594` in both, slot
+78h is `0x405c04` (shared by all five). The object factory `0x405314`
(switch on a record type word; caller `0x425c90`) builds the VEHICLE at
`0x40572d`…`0x405848`: object from the pool `[0x4a95a0]` (`0x471b30`),
`0x403368(object, record)`, registration in the actor list `0x4a9af8`
(`0x411dd4`), then the table chain `0x49a54c` → `0x497940` (type word +4 = 5,
+1A8h = 1) → `0x4978ac` → `0x497818` with type word +4 = 0Ah and word +1A8h
= 3. Only `0x405836` stores `0x497818`; only the dword `0x497830` (table slot
+18h) points at `0x46a6d4`.

A VEHICLE is a building-pool object with a turret (it inherits the GUN_BASE
turret update) that can also drive: a group of them follows the group's
route (hypothesis from the bytes: a convoy of ground vehicles; the hook group
is named `convoy`). Its update runs in logic-frame stage 10
([main_loop.md](main_loop.md)): `0x45f7a6` calls slot +18h for every object
of the pool `[0x4a95a0]` whose group has no pending activation
(`[[obj+45h]+14h]` = 0), with EAX = 0, EDX = the table, ECX = `[obj+45h]`;
the result and ECX are dead there (`pop ecx` at `0x45f7a9`, then EDI/ECX are
reloaded).

### Object fields

B is the object. Roles are read from the use.

| Offset | Content |
|---|---|
| +0 | table |
| +0Ch/+0Eh/+10h | angle words (yaw at +10h) |
| +12h | matrix (coefficients, mode byte +24h; translation = the position) |
| +26h/+2Ah/+2Eh | position dwords |
| +32h | matrix-valid word |
| +45h | group pointer |
| +99h, +0A4h | state bytes (the "alive" bytes of [mission_runtime.md](mission_runtime.md)) |
| +1A4h | turret link dword |
| +1A8h | type word (3 = VEHICLE) |
| +1F2h | configuration pointer (word +2Eh: turret enabled) |
| +20Dh/+20Fh | turret angle words (`turret_aim`, [player_movement.md](player_movement.md)) |
| +220h | speed word |
| +222h | timer record (flag byte, countdown word +223h; stepped by `timer_word_step`) |
| +227h | reverse flag byte |

Group G = `[B+45h]` (stride 7Ah, mission_runtime.md): +4 route holder (word
index, route pointer at +6), +0Ch member array, +10h member count word.

## Call graph

Each function below has exactly one call site (direct calls and stored
addresses scanned over the whole image), except `0x46a5bc` with two (`0x46a81d`
and `0x46aa6d`):

```text
0x45f7a6 (stage 10) -> [0x497830] 0x46a6d4 convoy_think
  0x46a6d4 -> 0x403ca8 base update | 0x404100 turret, 0x403d5c turret_aim
           -> 0x46a810 convoy_drive (0x46a79b), 0x4029d8 terrain_pose, 0x46a614 collision test
  0x46a810 -> 0x46a5bc leader search; 0x46a9e8 convoy_lead (0x46a828) | 0x46aa60 convoy_follow (0x46a831);
              0x47ebb8 euler_matrix, 0x48043c transform_point
  0x46a9e8 -> 0x423b0c route_point (2x), 0x492934 heading_point, 0x46a958 convoy_seek (0x46aa2d),
              0x42313c route_advance | 0x46a89c convoy_steer (0x46aa50)
  0x46aa60 -> 0x46a5bc, virtual +78h, 0x47ebb8, 0x47df04, 0x47ffe8 rotate_2d, 0x4929fc, 0x46a958 (0x46aba8)
            | 0x4928d0 dist2d_point, 0x492934, 0x46a89c (0x46ac04)
  0x46a958 -> 0x4928d0, 0x4929fc, 0x492934, 0x46a89c (0x46a9cb)
  0x46a89c -> 0x467a48 timer_word_step, 0x467adc approach_word, 0x47e0a0 multiply_q8
```

### Callees outside the chain

| VA | Function | Own code |
|---|---|---|
| `0x403ca8` | BASE update (59 instructions, large subtree) | hook `base_update` (group `base`) |
| `0x404100` | `turret` | hook (group `weapons`) |
| `0x403d5c` | `turret_aim` | hook (group `track_pose`) |
| `0x4029d8` | `terrain_pose` | hook (group `node`) |
| `0x4929fc` | `point_offset`: moves a 2D point by a distance word along an angle word through the sine table (pure) | hook (group `ai_helpers`) |
| `0x47df04` | swaps words +2 and +4 of a 2 × 2 matrix, returns its pointer (pure) | computed in `core/src/convoy.c` (`cv_swap`); the call is only observed |
| virtual +78h (`0x405c04` for VEHICLE) | when word `[B+49h]` is nonzero it calls `0x405b9c` and `0x411d64` on the point; hypothesis: the formation offset of B's slot | hook `formation_base` ([ai.md](ai.md#405c04-formation_basea-p-base-classes)) |

None of the chain functions or these callees draws, plays a sound or calls
the runtime. The callees are reached through the weapons guest bridge with
the original's entry registers, so a callee keeps its own hook mode.

## 46a5bc `convoy_leader(B)`

cdecl, one slot; ESI and EDI saved and restored (written, never read
before). No entry register is read: EAX ← 0 (the index i), EDX is loaded
before its first use, ECX's high word is reached only by `MOV CX, word` and
then overwritten. Callers: `0x46a81d` in `convoy_drive` and `0x46aa6d` in
`convoy_follow`, both `push ebx; call; pop ecx` with EBX = B. After the call
`0x46a810` compares EAX with B and enters `0x46a9e8` or `0x46aa60` with
`0x46a5bc`'s EDX (`0x46aa60` does not read it; `0x46a9e8` hands it on to
`route_point` `0x423b0c` as that call's entry EDX), so EDX is kept exact;
`0x46aa60` moves EAX to ESI and overwrites EDX and ECX before the next call.
ECX at return is popped at both sites: dead.

1. Pass i (EAX): EDX = G = `[B+45h]`, CX = word `[G+10h]` (both reread in
   every pass). When (int16)CX > (int16)AX (JLE at `0x46a5d0` not taken):
   ECX = (int16)AX, EDX = `[[G+0Ch] + 4·ECX]` (the list dword reread); else
   EDX = 0.
2. ECX = ESI = EDX + 92h; byte `[EDX+0A4h]` ≠ 0 (`0x46a5eb`) → ECX = 1; else
   EDI = ECX and byte `[EDX+99h]` ≠ 0 (`0x46a5f3`) → ECX = 1; else ECX = 0.
3. CX = 0 (`0x46a601`): EAX = EDX, return.
4. Else i += 1; while i < 4 (`0x46a60b`, signed dword) next pass; then
   EAX = 0.

Result: EAX = the first of members 0..3 whose bytes +0A4h and +99h are both
zero, else 0; EDX = the last member read (= EAX on a hit); ECX = 0 on a hit,
1 otherwise (dead). An index at or above the signed count gives EDX = 0 and
the original faults at `[0A4h]`, so the JLE at `0x46a5d0` is proved one-way
(`PROVED`, edge 0): a valid group's counted list reaches an alive member, or
holds at least four members.

Own code: hook `convoy_leader` (group `obstacle_walk`, with `collision_walk`
and `shape_cell`, [mission_runtime.md](mission_runtime.md#collision-walk-427c68-and-shape-cell-427360)),
`es2_convoy_leader_registers` in `core/src/convoy.c`, adapter
`tools/reference-game/proxy/obstacle_walk_hooks.h`. Pure: no call, no write,
the same reads in the same order, including the faulting read of `[0A4h]`
for an exhausted count. EDX:EAX exact; no entry shim (no entry register or
uninitialized local is read). Accepts compare mode with the default
comparison (EDX:EAX; nothing written).

## 46a89c `convoy_steer(B, steer, speed)`

cdecl, three slots read (callers push a fourth 0). Entry EDX/ECX reach the
first call unchanged.

1. `timer_word_step(B+222h)` (EAX = B+222h). When its AX ≠ 0 (the reverse
   timer runs), the speed slot's low word becomes FF38h (−200) when byte
   B+227h is set, else 00C8h (200); its high word stays.
2. s = steer clamped on its low word: `(int16)steer ≥ 100h` → 100h over the
   timer result's high word; `≤ −100h` → FF00h over the same; otherwise the
   whole steer dword.
3. `approach_word(B+220h, speed, 1Eh)` with EAX = s, EDX = B+220h, ECX =
   B+222h (the popped slot).
4. The words +0Ch, +0Eh, +10h are read after that call. `multiply_q8(0C8h,
   (int16)s)` with EAX = (B+0Ch) high word over word +10h, EDX = approach's
   EDX high word over word +0Ch, ECX = approach's ECX high word over word
   +0Eh.
5. Dword B+0Ch ← the two saved words, word B+10h ← saved yaw + product
   (16 bit), word B+32h ← 0.

Result EAX = B+0Ch, EDX = word +0Eh << 16 | new yaw. ECX = the frame dword
at EBP−4, whose high word (EBP−2) is never written: uninitialized, but dead
at every caller (`0x46a958` and `0x46aa60` return, `0x46a9e8` returns or
reloads ECX, `0x46a810` pops ECX after both calls). The replacement returns
its entry ECX.

Own code: `es2_convoy_steer_registers`. Its callees are pure apart from the
words they are handed, so compare mode is accepted: one span B+0Ch..B+227h
(angles, B+32h, approach word B+220h, timer word B+223h, reverse byte
B+227h) and a comparator remapping EAX = B+0Ch
([compare_mode.md](../reference-game/compare_mode.md#spans-and-comparators)).

## 46a958 `convoy_seek(B, P, heading, speed)`

cdecl, four slots (the fourth read as a dword test, then as a word).

1. d = `dist2d_point(B+26h, P)` (EAX = B+26h, entry EDX/ECX).
2. The point's x and y dwords are read after that call into the frame
   point L (EBP−0Ch; EBP−4 is never written and never read).
   `0x4929fc(&L, (P.x high word : heading word), 2328h − d)` with EAX =
   2328h − d, EDX = that second slot, ECX = &L.
3. h = `heading_point(&L, B+26h)` (EAX = B+26h, EDX = &L).
4. steer = (int16)(h − word B+10h) >> 4 (the `SAR EDX,64h` at `0x46a9b0`
   shifts by 100 mod 32 = 4); the speed word is the fourth slot's low word
   when the slot is nonzero, else 0AAh, over h's high word.
5. `convoy_steer(B, steer, speed, 0)` with EAX = the speed dword, EDX =
   steer, ECX = heading_point's ECX.

Result EAX = 1 when d < 10000 (signed), else 0; EDX = convoy_steer's EDX.
Own code: `es2_convoy_seek_registers`.

## 46a9e8 `convoy_lead(B)`

The group leader follows the route of its group's holder H = `[B+45h]+4`
(reread before every use).

1. p = `route_point(H, ((entry EAX & FFFF0000h) | [H]) + 1)`: the 32-bit
   increment of the word loaded into AX over the entry EAX (index FFFFh
   carries into the high word), also the entry EAX of the call; entry
   EDX/ECX.
2. p = 0: `convoy_steer(B, 0, 0, 0)` (EAX = 0, route_point's EDX/ECX);
   return its EDX:EAX.
3. q = `route_point(H, (p high word : [H]))`, then h = `heading_point(p, q)`
   (EAX = q), then `convoy_seek(B, p, h, 0)` (EAX = h); when its AX = 0
   return its EDX:EAX, else `route_advance(H)` (EAX = seek's result, EDX =
   H) and return that EDX:EAX.

Own code: `es2_convoy_lead_registers`.

## 46aa60 `convoy_follow(B)`

A member that is not the leader keeps formation behind it.

1. L = `leader(B)` (entry registers). The frame point V (EBP−1Ch) = L's
   position; virtual +78h `(B, &V)` (EAX = V.z, EDX = &V, ECX = the table)
   moves V to B's formation slot.
2. EDI = the entry EDI high word over L's yaw (read after the virtual);
   diff = yaw(L) − yaw(B) over the same high word. The magnitude is 7FFFh
   for 8000h (high word from the virtual's EDX), else |diff|.
3. Magnitude < 4000h (near path): B's position is read, then when word
   L+32h = 0 `euler_matrix(L+0Ch, L+12h)` (EAX = EDX = L+0Ch, ECX = L+12h)
   and L+32h ← 1; M (EBP−8) = L's first four matrix words; D (EBP−34h) =
   B's position − V; `0x47df04(&M)` (EAX = V.x, EDX = V.y, ECX = &M); R =
   `rotate_2d(&D, M', &R)` (EAX = &D, EDX = &R, ECX = &M); s = (int16)
   [L+220h] − (R.y >> 5) clamped on its low word to [−96h, 100h] (the bound
   over rotate_2d's EDX high word, else the whole dword), sign-extended;
   `0x4929fc(&V, EDI, 4E20h)` (EAX = s, EDX = the clamped dword, ECX = &V);
   `convoy_seek(B, &V, EDI, s)` (EAX = &V); return its EDX:EAX.
4. Otherwise (far path): d = `dist2d_point(B+26h, &V)` (EAX = diff, EDX =
   &V, ECX = the virtual's ECX); h = `heading_point(&V, B+26h)` (EAX = &V);
   steer = (int16)(h − yaw(B)) >> 4; speed = d >> 5 clamped on its low word
   to [−96h, 100h] (bound over steer's high word), sign-extended;
   `convoy_steer(B, steer, speed, 0)` (EAX = steer, EDX = speed); return its
   EDX:EAX.

**Entry EDI.** `0x46aa60` never sets EDI before it reads DI's high word; the
game's only chain passes B+26h (`0x46a6d4` sets EDI at `0x46a768`,
`0x46a810` keeps it). The high word reaches only (a) the far path's entry
EAX of `dist2d_point` (`0x4928d0` overwrites EAX first), (b) the high word
of `0x4929fc`'s second slot (it reaches `0x492b10`/`0x492b31`, which load it
with a word move, and `0x492add`/`0x492b52`/`0x47dff0` read words only), (c)
the high word of `convoy_seek`'s third slot (word load at `0x46a98e`). The
adapter therefore passes B+26h; no entry shim.

Own code: `es2_convoy_follow_registers` (frame: D f−34h, R f−28h, V f−1Ch, M
f−8, f = scratch + 40h).

## 46a810 `convoy_drive(B)`

1. L = `leader(B)` (entry registers); then ECX = B (`pop ecx`).
   `convoy_lead(B)` when L = B, else `convoy_follow(B)` (EAX = L, EDX =
   leader's EDX, ECX = B).
2. Velocity v (EBP−0Ch) = (0, (int16)[B+220h], 0), read after that call.
   When word B+32h = 0: `euler_matrix(B+0Ch, B+12h)` (EAX = B+12h, EDX = ECX
   = 0) and B+32h ← 1. `transform_point(&v, B+12h, &out)` (EAX = B+12h, EDX
   = &out, ECX = &v).
3. Position ← out (z, x, y in that order). Result EAX = B+26h, EDX = out.y.

Own code: `es2_convoy_drive_registers` (frame: velocity f−0Ch, result f−18h).

## 46a6d4 `convoy_think(B)`

1. G = `[B+45h]`. When `(int16)[G+10h] > 0`: EAX = `[G+0Ch]`, M = `[EAX]`,
   else EAX = G, M = 0; the type word `[M+1A8h]` is loaded into AX (an empty
   group reads `[1A8h]` and faults: precondition).
2. Type ≠ 3: `0x403ca8(B)` (EAX = that, EDX = M, entry ECX); return its
   EDX:EAX.
3. ECX = `[B+1F2h]`. Word +2Eh ≠ 0: `turret(B)`, then the pair P (EBP−8)
   = (−word B+20Dh over turret's EAX high word, word B+20Fh over its EDX
   high word; EBP−6 and EBP−2 are never written, turret_aim reads only +0
   and +4) and, when dword B+1A4h = 0, `turret_aim(B, &P)` (ECX = &P).
   Word +2Eh = 0: `0x403ca8(B)`.
4. Byte B+99h ≠ 0: return EAX = B+92h, EDX as left by step 3.
5. Saved: the position dwords and the three angle words. `convoy_drive(B)`
   (EAX = (B+0Ch) high word over yaw, EDX = position x high word over word
   +0Ch, ECX = position y high word over word +0Eh), `terrain_pose(B)`
   (drive's EAX/EDX, ECX = B), the dword B+2Eh is read and written back
   unchanged, `0x46a614(B)` (EAX = that dword, terrain_pose's EDX, ECX = B).
6. Its AX = 0: return its EDX:EAX. Otherwise (collision): byte B+227h ←
   ((int16)[B+220h] > 0), word B+223h ← 3000 (the reverse timer), the saved
   position and angles are restored, B+32h ← 0, B+220h ← 0; result EAX =
   B+0Ch, EDX = the saved y.

Own code: `es2_convoy_think_registers` (frame: the angle pair P at f−8, words
at f−8 and f−4).

## Collision test 0x46a614

`0x46a614` has the single call site `0x46a7af` in `convoy_think` (step 5);
the address is stored nowhere. It calls the static-obstacle test `0x404ae4`,
whose contract and 140-entry list overflow are in
[mission_runtime.md](mission_runtime.md#static-obstacle-list-404ae4); both
are hooks of the group `obstacle`.

### Virtual slots it calls

Read from the image: slot +5Ch is `0x4035a4` (`base_radius`) in all five
building tables (`VEHICLE`, `GUN_BASE`, `BASE`, `RADAR_BASE`, `LC_BASE`; word
`[[o+1F2h]+2Ah]`, the configured radius), slot +7Ch is `0x4035b8`
(`base_sight_radius`; 0 when word `[C+6]` = 0 or when the first test of
`0x404ae4` step 4 passes, else the same radius word `[C+2Ah]`, C =
`[o+1F2h]`), slot +68h is `0x42200c` (`actor_mark`; byte `[o+0B1h]` ← 1, a
"touched" flag). The vehicle table `0x49a282` (the MECHs of the actor list)
has +5Ch = +7Ch = `0x415518`/`0x41552c` (`mech_radius`/`mech_sight_radius`;
word `[[o+1F2h]+70h]`) and the same +68h; the actor list holds both kinds.
None draws, plays a sound or calls the runtime; all are hooks of the group
`actor_leaves` ([vtables.md](vtables.md#actor-slot-leaf-hooks)).

### 46a614 `convoy_collide(B)`

cdecl, one slot (the VEHICLE). Frame: the actor-list index word EBP−4
(EBP−2 is never written, never read as a dword), the cursor EBP−8, B's
radius word EBP−0Ah, the current actor EBP−10h.

1. EAX = `[4a9af8]` (the actor list's entry array), cursor ← EAX, index ←
   0. r = virtual `+5C`(B) with EAX = that array, EDX = B's table, ECX = the
   entry ECX; then ECX = B (`pop ecx`); the radius word ← r's AX.
2. Next actor: EDX = (EDX high word) | the index word before the increment;
   the index word += 1; when the old index ≥ the count word `[4a9afe]`
   (signed 16-bit) the actor is 0 (ECX unchanged), else ECX = the cursor,
   cursor += 4, actor = `[ECX]`. Actor 0 ends the walk (step 4).
3. For actor A: skip A = B. ECX = `[A+45h]` (its group), EAX = 1 when
   `[ECX+14h]` ≠ 0 (activation pending; SETNE/AND, so EAX is exactly 0 or
   1), skip on 1. s = virtual `+7C`(A) with EAX = 0, EDX = A's table, ECX =
   the group; then ECX = A. Skip when the word s is 0. d =
   `dist_gate(B+26h, A+26h)` (`0x49288c`; EAX = A+26h, EDX = B+26h, ECX =
   A). Then ECX = bound = (int16)radius + (int16)s, EDX = (int16)s. d ≥
   bound (signed) skips A. Otherwise virtual `+68`(A) (EAX = A's table, EDX
   = (int16)s, ECX = bound) and return EAX = its high word | 1, EDX = its
   EDX.
4. After the walk: `404ae4(B+26h, (EAX high word) | radius, B)` with EAX =
   that dword, EDX and ECX as step 2 left them. When its AX ≠ 0, AX ← 1
   (EAX keeps `0x404ae4`'s high word). Return EDX:EAX (EDX = `0x404ae4`'s).

So B collides with the first actor-list entry (MECHs and other actors,
walked forwards) whose activation is not pending and whose collision radius
s is nonzero within the combined radius, which is flagged through +68h;
otherwise with the static objects of the building pool (excluding B itself).
ECX after `0x46a614` is dead: `convoy_think` returns or overwrites it, and
`0x45f7a6` pops ECX after slot +18h.

Registers `0x46a614` reads at entry: only ECX (passed unchanged to virtual
+5Ch, whose `0x4035a4` never reads it). EBX, ESI and EDI are saved and
restored and written before use.

Own code: `es2_convoy_collide_registers` in `core/src/obstacle.c`, adapter
`tools/reference-game/proxy/obstacle_hooks.h`.

## Hooks

All cdecl, EDX:EAX exact. The game calls the functions of the group
`convoy` through the guest bridge, so they refuse compare mode; the
exceptions are `convoy_steer` (span and comparator above) and
`convoy_leader` (default comparison). The prologue column gives the bytes
the hook checks and the relocated length (frame lengths in parentheses).

| Hook | VA | Group | Prologue / relocate | Slots | Compare |
|---|---|---|---|---|---|
| `convoy_steer` | `0x46a89c` | convoy | `55 8b ec 83 c4 f8 53 56 57`, 6 (1,2,3) | 3 | yes |
| `convoy_seek` | `0x46a958` | convoy | `55 8b ec 83 c4 f4 53 56 57`, 6 | 4 | no |
| `convoy_lead` | `0x46a9e8` | convoy | `55 8b ec 53 56 57 8b 5d 08`, 5 (1,2,1,1) | 1 | no |
| `convoy_follow` | `0x46aa60` | convoy | `55 8b ec 83 c4 cc 53 56 57`, 6 | 1 | no |
| `convoy_drive` | `0x46a810` | convoy | `55 8b ec 83 c4 e8 53 56 57`, 6 | 1 | no |
| `convoy_think` | `0x46a6d4` | convoy | `55 8b ec 83 c4 e4 53 56 57`, 6 | 1 | no |
| `convoy_leader` | `0x46a5bc` | obstacle_walk | `55 8b ec 56 57 33 c0`, 5 (1,2,1,1) | 1 | yes |
| `obstacle_test` | `0x404ae4` | obstacle | `55 8b ec 81 c4 c4 fd ff ff 33 c0`, 9 (1,2,6) | 3 | no |
| `convoy_collide` | `0x46a614` | obstacle | `55 8b ec 83 c4 f0 a1 f8 9a 4a 00`, 6 (1,2,3) | 1 | no |

Core: `core/src/convoy.c` (`convoy_*`), `core/src/obstacle.c`
(`convoy_collide`, `obstacle_test`). Adapters:
`tools/reference-game/proxy/convoy_hooks.h`, `obstacle_walk_hooks.h`,
`obstacle_hooks.h`. In vitro: `tools/reference-game/invitro/convoy_cases.h`,
`obstacle_walk_cases.h`, `obstacle_cases.h`; mutations in
`check_mutations.ps1`; core tests `//core:convoy_test` and
`//core:obstacle_test`. Registry rows and counts:
[hooks.md](../reference-game/hooks.md#registry).

`convoy_follow`'s adapter passes B+26h as the entry EDI. No hook embeds a
game instruction.

### Branch sites

The six functions of the group `convoy` have 23 conditional jumps
(`tools/reference-game/invitro/branch_sites.h`), 22 covered both ways in the
in-vitro direct cases. `0x46a6e8` (JLE, empty group) is proved one-way: its
taken edge loads `[1A8h]` through a null member and faults in the original
(`PROVED(convoy_think,0x46a6e8,0,…)`). `0x46a5bc` has five (all both ways
except `0x46a5d0`, proved). `0x46a614` has seven (`0x46a63d`, `0x46a64f`,
`0x46a65d`, `0x46a680`, `0x46a69e`, `0x46a6af`, `0x46a6c6`), `0x404ae4` ten
(`0x404b0b`, `0x404b18`, `0x404b25`, `0x404b32`, `0x404b3e`, `0x404b4c`,
`0x404b59`, `0x404b65`, `0x404b6c`, `0x404b95`); all reachable both ways with
suitable actor lists, none proved one-way.

## Open

- The group and formation semantics (route following by the leader,
  formation slot through virtual +78h, reverse after a collision) are read
  from the bytes. Which mission objects are VEHICLEs is not checked; the
  probes in [docs/functions](../functions/dbsim-460000.md) show `0x46a6d4`
  entered in the first five suites of the map but `0x46a810` only in the
  first three (a60, c1s, c1c): in c3_09 and c4_08 there is no type-3 leader
  alive, or all are dead (unchecked).
