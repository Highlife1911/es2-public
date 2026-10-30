# Vehicle simulation functions

Contracts of the DBSIM functions around the vehicle object: the condition
and damage leaves of its part tables, the per-vehicle update `0x41aa5c` of
the logic frame with its leaves, the actor-list pass `0x4123ac` with its
contact helpers, the shield, facing and key methods of the vehicle vtable,
and the part and weapon-group helpers. All are read from the bytes of the
reference `DBSIM.EXE` (SHA-256 `8561aa47…c4ad`, image base `0x00400000`,
[reference_version.md](../reference_version.md)) with `llvm-objdump -d -M
intel` listings and raw dword scans of the image for the entry addresses.
Roles and names are hypotheses; the contracts are confirmed in vitro and by
the live regression.

| Adapter group | Header (`tools/reference-game/proxy/`) | Core | Section |
|---|---|---|---|
| `vehicle` | `vehicle_hooks.h` (names `vehicle_leaf_*`) | `core/src/vehicle.c` | [Vehicle leaves](#vehicle-leaves) |
| `vehicle_update` | `vehicle_update_hooks.h` | `core/src/vehicle_update.c` | [Per-vehicle update](#per-vehicle-update) |
| `actor_pass` | `actor_pass_hooks.h` | `core/src/actor_pass.c` | [Actor-list pass](#actor-list-pass) |
| `vehicle_aux` | `vehicle_aux_hooks.h` | `core/src/vehicle.c` | [Shield, damage, facing and keys](#shield-damage-facing-and-keys) |
| `part_helpers` | `part_helpers_hooks.h` | `core/src/vehicle_update.c` (the two slot-0 bodies), `core/src/vehicle.c` (the rest) | [Vehicle part and weapon-group helpers](#vehicle-part-and-weapon-group-helpers) |

The hook table is in [hooks.md](../reference-game/hooks.md#registry). Every
game call of these hooks goes through the weapons guest bridge
(`weapons_graph`) with the registers the original holds at that call
(tracked instruction by instruction in the core), so hooked callees are
entered as hooks and keep their own modes.

**Live.** Every hook of this document is replaced in the full regression
`<es2-data>\logs\check-r15-1` (789 hooks replaced, 40 of 40 suites
identical, every skipped/bypassed/mismatch counter 0). In its 40 suites
`shield_max_set` and `shield_capacity` have equal call counts, and
`damage_score` is never called (its contract rests on the in-vitro
evidence).

## Notation

`R16`/`R32` are little-endian guest reads, `W8`/`W16` writes, `sx`
sign-extends a word (`sx8` a byte), `s16`/`s32` compare signed, `lo(r, w)`
replaces the low word of register r (a 16-bit load or arithmetic that
keeps the high word), `AX(f(...))` is the low word of a callee's result.
"Vn" is the virtual at offset n of the object's first dword. The vehicle
table `0x49a282` has V20 = 418ba8, V24 = 417b98, V2c = 4157c8, V3c =
415488, V40 = 415504, V48 = 412800, V54 = 41b534, V58 = 41b60c. Every
result is the full EDX:EAX the original leaves; no hook returns ECX (the
framework gives the entry ECX back; see [ECX after
return](#ecx-after-return) and the callers' sections). An "entry shim"
moves a caller register that the original reads into the entry EAX before
the framework's dispatch.

## Objects

### The part tables P (vehicle +206h)

The condition functions take the part-table object that the vehicle keeps
at +206h (the same object `part_ratio` 40dbc0 and `part_condition` 40db2c
read):

- `[P]` → part value words (signed; negative = "use the maximum"),
- `[P+4]` → part-group value words,
- `[P+8]` → 29 flag words (nonzero = the entry is counted or measured),
- `[P+0Ch]` → a pair of pointers: `[[P+0Ch]]` = 18-byte group records whose
  first word is the group's base maximum, `[[P+0Ch]+4]` = part maximum words.

A group record has the base maximum at +0, its part count word at +6 and
the pointer to its part list at +8 (4-byte entries whose word +2 is a part
index). The flag word table is also reachable as `R32(A+20Eh)`
(`facing_part` walks it, 29 words; nonzero = the part can be measured)
and holds the flag words that `damage_score` tests for groups 0..18.

### Weapon group, configuration record and target

G = `R32(A+202h)` is the vehicle's weapon group (vtables 499238/499338,
[weapons.md](weapons.md#group_fire-4107e4)): G+4 → weapon pointers, word
G+8 = their count, words G+0Ah..13h = five request flags, signed byte
G+1Dh = the selected weapon. C = `R32(A+1F2h)` is the vehicle's
configuration record. T = `R32(A+1A4h)` is the current target (re-read at
every use, as in the original). The weapon objects (vtables 498a84…4992c0)
have V54 = 4111e9 (`weapon_stock`, AX = 1) or 40f520 (`gun_stock`), and
V60 = 40e62c (`weapon_ammo`: writes 0 through a non-null pointer, AX = 5)
or 40e644 (`gun_ammo`: writes word W+7Bh through a non-null pointer and
returns word +2 of `[W+20h]`, or 40e62c's result); so V60 writes its word
whenever the pointer is non-null.

### Actor list and actor fields

The actor list is the dword array at `R32(4a9af8h)` with the signed count
word `4a9afeh`. Every loop of the actor-list functions walks it the same
way: a word index and a dword cursor in the frame start at 0 and at the
array; per step the old index is compared with the count (re-read every
step, signed `jge`), then the cursor is post-incremented and the entry
read; a null entry ends the loop as well. The step loads the index into
the low word of one register (AX, or DX in loop 3 of 4123ac and in 412704)
and the old cursor into a second one (EDX, or ECX).

An actor X has its vtable at +0, the position vector at +26h (three
dwords), the group G = `R32(X+45h)` (G+0Ch member array, G+10h member count
word, G+12h the signed side byte, dword G+14h nonzero = activation
pending), the slot word +4Bh, the bytes +95h ("alerted"), +96h, +99h and
+A4h (out of action), +9Eh (marked), +A2h (per-frame flag, also set by
`vehicle_update`), +A3h (player), the contact table +C2h (byte per slot:
`X+C2h+sx(R16(Y+4Bh))` ≠ 0 = X knows Y; `cockpit_valid` reads the same
table, [hud.md](hud.md)), the trigger dword +1B2h and the two word timers
+1E2h (word +1E3h, also `sight_check`'s re-check timer) and +1E5h (word
+1E6h).

The actor vtables (vehicles `0x49a282`, buildings `0x4978ac`, `0x497940`,
`0x4979d4`, `0x497818`): V3c = 415488 (`mech_angle`, word +298h), 403594
(`gun_angle`, word +20Fh) or 411a5c (`actor_angle`, EAX = 0); V44 = 411acc
(`actor_in_front`: `(uint16)(arg2 − R16(X+10h) + 3800h) < 7000h`, EAX =
0/1) or 405308 (`base_in_front`: AX = 1, EAX's high word is the entry
EAX's); V48 = 412800 (`mech_contact`, vehicles; timer 4a9b64, the order
call 420a98 `radio_notice`) or 4127f8 (`actor_contact`, empty). None of
them reads an entry register.

## Vehicle leaves

| Hook | VA | Arguments (cdecl) | Role | Callees |
|---|---|---|---|---|
| `condition_table` | 4151a4 | (parts P, out) | Q8 condition table of a vehicle's part groups | none |
| `condition_ratio` | 4152ec | (parts P, kind word, index word) | one Q8 condition value | none |
| `sight_check` | 412608 | (viewer A, target B) | terrain line of sight with a re-check timer | `rng_bounded`, V24 (twice), `terrain_line` |
| `group_state` | 412c8c | (group G) | condition level 0..4 of a group | V40 per member |
| `target_bearing` | 41e758 | (actor A, out, target T or 0) | bearing/aspect/distance record to a target | `heading_point`, V3c, `dist_gate` |
| `part_position` | 41b60c | (vehicle A, part word, out) | vehicle virtual +58h: world position of a part | V20, 4801d4, `euler_matrix`, `transform_point` |

All six refuse compare mode: the two condition functions write a table or
need the caller's EBX, the others call game code. Listings and the bounded
register walk: `<es2-data>\logs\h4-03-20261008`.

### condition_table 4151a4

Called only by 41b4ec(vehicle, out) (`condition_record`), which stores
word `[vehicle+1F2h]+6Ch` into `out+0` and passes (vehicle+206h, out).
41b4ec is called from 41edf5 (in `combat_rating` 41edd8), 423d97, 43a7d8,
44ae2b and 450f4a.

Writes 41 words of `out`; `out+0` is not touched:

1. **Groups 0..18** (`out+2+2i`, loop 4151bb..4151fa, counter compared as a
   word against 13h): with `g = [P+4]` read once, if `s16(R16(g+2i)) < 0`
   the value is 100h; otherwise `(sx(R16(g+2i)) << 8) / sx(R16(rec(i)))`
   (IDIV, low word stored), where `rec(i) = R32(R32(P+0Ch)) + 18i` is
   re-read every iteration.
2. **Groups 19..28 with parts 12..21** (`out+40h+2i`, i = 0..9, loop
   41521b..41528e): `t = sx(R16(rec(19+i)))`; `v = t` if the group value
   word at `g+26h+2i` is negative, else that value (the group pointer
   continues where loop 1 stopped); `p` = the part value at `[P]+18h+2i`, or
   the part maximum at `[[P+0Ch]+4]+18h+2i` if the value is negative;
   result `((v + p) << 8) / (sx(maximum) + t)`. The two part pointers are
   read once before the loop.
3. **Parts 0..11** (`out+28h+2i`, loop 4152a6..4152e1): with the part values
   `[P]` and maxima `[[P+0Ch]+4]` read once: maximum word 0 gives 0, a
   negative value gives 100h, else `(sx(value) << 8) / sx(maximum)`.

**Result.** EAX = P if loop 3 never divides, else the last loop-3
quotient; EDX = the remainder of the last division (loop 3, or the last
loop-2 division). Both are dead at every caller of 41b4ec except as the
pass-through result of 41b4ec itself.

**Preconditions.** A zero group record word in loop 1, or a zero divisor in
loop 2, faults (#DE) like the original. No dividend can overflow
(|v + p| < 2^16, shifted by 8). The shipped data never divides by zero
(`tools/reference-game/scripts/domain_vehicle_part.py`): the loop-1 group
thresholds of every vehicle are non-zero, empty slots divide by 1, and the
only items with a zero maximum (0, 26 LAEW, 27) are mounted by no shipped
loadout.

**Branch sites (8):** 4151bf, 4151fa, 41523f, 415253, 41528e, 4152aa,
4152b7, 4152e1; all reachable both ways.

### condition_ratio 4152ec

Called only by 41b514(vehicle, kind, index) = 4152ec(vehicle+206h, kind,
index), which is called from 432a03 and 4342f8, **both with kind 0** (push
0). The kind and index are word arguments: `movsx ESI, word kind`; the
index is used as `sx(DX)` with EDX = the full index slot.

| kind (s16) | value word | divisor word |
|---|---|---|
| 0 | `[P] + 2·index` (part value) | `[[P+0Ch]+4] + 2·index` (part maximum) |
| 1 | `[P+4] + 2·index` (group value) | `R32(R32(P+0Ch)) + 18·index` (group base) |
| 2 | `[P+4] + 2·index + 26h` (group 19 + index) | same record as kind 1 |
| other | **entry ECX** | **entry EBX** |

Then: value negative or divisor zero → AX = 100h (EAX keeps its high
word); else EAX = `(sx(value) << 8) / sx(divisor)` and EDX the remainder.
On the 100h path EDX is the index slot, and EAX's high word is the path's
EAX: `[P] + 2·index` (kind 0), `2·sx(index)` (kinds 1/2) or P (other).

**Entry registers.** For kinds outside 0..2 the function dereferences its
caller's ECX and EBX as word pointers. EAX and EDX are dead on entry (EDX
is loaded at 4152f1, EAX at 4152f4), so an entry shim moves EBX into EAX
before the framework sees the call; the core receives EBX as entry EAX and
ECX as entry ECX. Because a compare-mode original phase would see the
dispatcher's EBX instead, the hook refuses compare mode.

**Branch sites (5):** 4152fe (JB, kind 0), 415300 (JE, kind 1), 415303
(JE, kind 2), 41535b, 415361; all both ways in vitro (the "other" kinds
with a controlled EBX/ECX).

### sight_check 412608

cdecl (viewer A, target B). Callers: 4125a1 and 4129c0/412b0f/412b75 in
the actor-list pass 4123ac → 41251c/4128f8 (main logic step 16,
[main_loop.md](main_loop.md#direct-calls-in-execution-order)), and
41aded/41adff in the per-vehicle update 41aa5c. Every caller tests AX
only.

1. `idx = R16(B+4Bh)` (read first; the target's slot in the viewer's
   sight table).
2. If the **target's** word `B+1E3h` is nonzero, skip to 6. (The re-check
   timer is read from B but re-armed in A; the bytes say so.)
3. `W16(A+1E3h, AX(rng_bounded(stream A 4d2599h, 1000)) + 5000)`.
4. Copy A+26h..31h and B+26h..31h (three dwords each) into frame vectors;
   call V24(A), then V24(B) (each returns a record or 0); raise each
   vector's third dword by `R32(record+1Ch)` or 500 if the record is null.
5. `terrain_line(&frame8, &A vector, &B vector, 0)` (4280f4); byte
   `A+132h+sx(idx)` = 1 if its AX is 0 (no terrain contact), else 0.
6. EAX = the sign-extended byte `A+132h+sx(idx)`, EDX = A+132h.

**Entry registers of the calls.** `rng_bounded`: the function's entry
EAX/EDX/ECX. V24(A): EAX = `R32(B+2Eh)`, EDX = A's table, ECX =
`R32(B+2Ah)`. V24(B): EAX = V24(A)'s EAX, EDX = its EDX, ECX = B's table.
`terrain_line`: EAX = &frame8, EDX = &B vector, ECX = &A vector. No entry
register is read by 412608 itself; no local is read before it is written.

**Branch sites (4):** 412623, 412683, 412697, 4126c8; all both ways.

### group_state 412c8c

cdecl (group G); callers 412de3, 412f9a, 413939. G+0Ch → member pointer
array, G+10h = member count word (both re-read every iteration, because
V40 may change memory).

For each member M (`si < s16 count`): M counts as down if byte `M+A4h` or
`M+99h` is nonzero (word counter); then `sum += AX(V40(M))` (word; for a
vehicle V40 = 415504 = `part_condition(M+206h)`). V40's entry registers:
EAX = M's table, ECX = the down flag (0/1, full register), EDX = M+92h when
byte M+A4h is zero, else `R32(G+0Ch)`.

After the loop, with `n = sx(R16(G+10h))` (re-read): `avg = sx(sum) / n`,
`q = (sx(down) << 10) / n` (both IDIV; EDX = the second remainder). Level:
4 if `n` equals the down count (word compare); else 3 if `s16(q) >= 650`
or `s16(avg) > 192`; else 2 if `s16(q) >= 250` or `s16(avg) > 128`; else 1
if `s16(avg) > 50`; else 0. EAX = `lo(q, level)` for levels 1..4 and 0 for
level 0 (`XOR EAX,EAX`).

**Precondition.** A zero count faults (#DE) in the first IDIV.

**Branch sites (10):** 412cb9, 412cc5, 412cd3, 412ce9, 412d09, 412d15,
412d1c, 412d28, 412d2f, 412d3b; all both ways.

### target_bearing 41e758

cdecl (actor A, out, target T); T = 0 selects `R32(A+1A4h)`. Callers
41c5c3, 41c892, 41c9df, 41d327, 41d442, 41d50a (AI behaviour states; out
is a frame record that 41e828 reads next).

| out | Value |
|---|---|
| +0 | `h = AX(heading_point(T+26h, A+26h))` (492934) |
| +2 | `h - R16(A+10h)` (relative bearing) |
| +4 | magnitude of +2 (8000h gives 7FFFh) |
| +6 | `AX(V3c(T)) + h + 8000h - R16(T+10h)` (aspect; V3c of a vehicle = word +298h) |
| +8 | magnitude of +6 |
| +0Ah | dword `dist_gate(A+26h, T+26h)` (49288c) |
| +0Eh | word 0 |
| +10h, +14h | dwords 15000 and 30000 |

**Entry registers of the calls.** `heading_point`: EAX = A+26h, EDX =
T+26h, ECX = the entry ECX. V3c(T): EAX = T's table, EDX = heading_point's
EDX, ECX = `lo(heading_point's ECX, out+2 value)`. `dist_gate`: EAX =
`lo(V3c's EAX, out+6 value)`, EDX = `lo(V3c's EDX, h + 8000h - R16(T+10h))`,
ECX = T+26h.

**Result.** EAX = `lo(dist_gate's EAX, out+6 value)`; EDX = 7FFFh in the
low word of dist_gate's EDX when out+2 is 8000h, else `lo(dist_gate's EAX,
magnitude of out+2)` (the register copy happens before the low word is
replaced).

**Branch sites (5):** 41e76a, 41e7cf, 41e7da, 41e7f1, 41e7fc; all both ways.

### part_position 41b60c

Vehicle virtual +58h (vehicle table 49a282; nine `call [reg+58h]` sites:
404fba, 4068ac, 40a2fd, 40e530, 4106cd, 418872, 41b569, 41ea1b, 41ead6).
cdecl (vehicle A, part word, out); writes the 12-byte world position of a
part to `out`.

`rec = R32(R32(A+1FAh)) + 18·sx(part)` (18-byte part records; dword
rec+0Eh = the part's local offset vector, word rec+0Ch = its shape node).

1. Offset null: copy A+2Eh → out+8, A+26h → out+0, A+2Ah → out+4 (in this
   order); EAX = `R32(A+2Ah)`, EDX = `R32(A+2Eh)`.
2. Else, if `s16(rec+0Ch) > 0`: `node = V20(R32(R32(A+34h)+4), zero-extended
   node word)` on the shape manager `S = R32(A+34h)` (entry EAX = the
   object's table, EDX = S, ECX = the object). The node record is
   `0x6c56a8` (a default record in the image's BSS) if node is null or
   `s16(node+4) <= -1`, else `R32(S+16h) + 32·sx(node+4)`. A node word
   `<= 0` gives no record.
3. With a nonzero record: `4801d4(offset, record, &frame1c)` (entry EAX =
   record, EDX = offset, ECX = &frame1c); if word A+32h is 0, rebuild the
   matrix A+12h from the angles A+0Ch (`euler_matrix(A+0Ch, A+12h)`, entry
   EAX = A+12h, EDX/ECX = 4801d4's) and set A+32h = 1; then
   `transform_point(&frame1c, A+12h, out)` (EAX = A+12h, EDX = &frame1c,
   ECX = the previous callee's ECX). Result: transform_point's EDX:EAX.
4. With a null record (node word <= 0, or a computed record address 0): if
   A+32h is 0, `euler_matrix(A+0Ch, A+12h)` (EAX = A+12h, EDX = A+0Ch, ECX =
   the path's ECX: the entry ECX for a node word <= 0, else `sx(node+4) <<
   5`) and A+32h = 1; then `4801d4(offset, A+12h, out)` (EAX = A+12h, EDX =
   offset). Result: 4801d4's EDX:EAX.

4801d4 (`matrix_point`: an offset vector through a matrix plus its
translation) and V20 (the shape manager's node lookup, not the vehicle's
V20) are entered through the bridge.

**EDX.** On the callee paths the result EDX is 4801d4's or
transform_point's EDX, which the real functions can derive from a frame
address (their own, or the frame vector's), so it differs between the
original and the replacement by the stack depth. EDX is dead at all nine
V58 call sites: written first, passed as a word argument slot (41eb20 →
41f5a0), or carried through returns (40e533 → 41b728's callers; 4106d0 →
432a50 → … → 40f340, where 467924's 47e0a0 IMUL writes it; 41ea1e → 41f5a0
→ 4928d0 → 49545b, which returns its argument, then MOV EDX).

**Branch sites (7):** 41b636, 41b655, 41b680, 41b68a, 41b6a7, 41b6c2,
41b6f4; all both ways.

### ECX after return

The hooks return EDX:EAX only. A forward walk with byte-lane tracking over
the full listing (`ecxlive.py`/`retfollow.py`, evidence directory
`<es2-data>\logs\h4-03-20261008`), following direct calls into
their callees and returns into all direct callers, finds ECX written
before it is read after every call site:

- 4151a4 (via 41b4ec's return): 41edfa, 423d9c, 44ae30, 450f4f overwrite
  it; 43a7dd reaches 433134, which loads ECX first.
- 4152ec (via 41b514): 432a08 loops back into 41b514, whose `push ecx`
  passes the kind slot that 4152ec reads as a word; 4342fd reaches 438700,
  which passes ECX through, and the caller then overwrites it.
- 412608: 4125a6, 41adf2/41ae04 overwrite it; 4129c5 and 412b7a return
  through 4128f8 into 4124a8/4124d5 (`pop ecx`); the paths through the
  vehicle V48 (412800) and V3c (415488) pass ECX through to an overwrite.
- 412c8c: `pop ecx` right after all three calls.
- 41e758: overwritten at all six callers; 41d447's `add ecx,-2` only
  feeds the stored CX (low word from the preceding `mov cx,1`).
- 41b60c: overwritten after seven of the nine virtual calls; 41ea1e reaches
  `push ecx` after `mov cx, word arg`, a word argument slot of 41f5a0
  (read as a word at 41f623); 4106d0 returns through 4106ac → 432a50 →
  432bc8 into the input dispatcher (45feb8/460870), where every path writes
  ECX or calls the RTL thunk 496952 (treated as clobbering, not followed).
  The paths the walk does not follow are covered only by the live
  regression.

## Per-vehicle update

The logic-frame child `0x41aa5c` (row 17 of the direct-call table in
[main_loop.md](main_loop.md#direct-calls-in-execution-order): called at
45f8ac for every vehicle of the `MECH` pool whose group is not pending and
whose byte +99h is 0) and its six leaves. Instruction count from the bytes:
464 for `0x41aa5c` (agrees with the map), no jump table. Per-function
listings, the branch-site list and the callee/vtable scans:
`<es2-data>\logs\h4-14-20261008`.

| Hook | VA | Arguments (cdecl) | Role (names are hypotheses) | Callees |
|---|---|---|---|---|
| `combat_rating` | 41edd8 | (vehicle A) | damage-weighted strength word A+29Eh | `condition_record`, weapon V54, `multiply_q8` |
| `condition_record` | 41b4ec | (vehicle A, out) | config word +6Ch and the condition table | `condition_table` |
| `device_state` | 41aa10 | (vehicle A) | state of the device at A+307h | none |
| `device_word` | 41aa44 | (vehicle A) | word +7Fh of the device at A+307h | none |
| `ready_tone` | 41b0bc | (vehicle A) | warning tones of the ready flags | 467828 (clock), 462830 (sound) |
| `weapon_tally` | 40fbdc | (group G, out) | five words summed over the weapons' V60 | memset 49681a, weapon V60 |
| `vehicle_update` | 41aa5c | (vehicle A) | energy, countdowns, target lock, fire flags | see below |

Every caller of the six leaves is `0x41aa5c`, except for
`condition_record` (also 423d97, 43a7d8, 44ae2b, 450f4a); no dword of the
image holds any of the seven addresses (raw scan). The objects G, C and T
are those of [Weapon group, configuration record and
target](#weapon-group-configuration-record-and-target). The two device
readers are pure and allow compare mode; the other five call game code
and refuse it.

### condition_record 41b4ec

`W16(out, R16(R32(A+1F2h)+6Ch))`, then `condition_table(A+206h, out)` with
EAX = A+206h, EDX = out, ECX = `lo(R32(A+1F2h), R16(R32(A+1F2h)+6Ch))`.
Result: condition_table's EDX:EAX. No branch. ECX after the return is dead
at all five callers (the [walk for 4151a4](#ecx-after-return) passes
through these return addresses).

### device_state 41aa10 and device_word 41aa44

D = `R32(A+307h)`.

- 41aa10: D = 0 gives EAX = EDX = 0. Otherwise, with byte A+A3h ≠ 0, EDX =
  `lo(A+92h, R16(D+7Dh))`; else EDX = the sign-extended byte A+96h. EAX =
  EDX. Branch sites 41aa21, 41aa2f.
- 41aa44: EAX = `R16(D+7Fh)` (zero-extended) or 0, EDX = D. Branch site
  41aa54.

Both are pure readers. The only caller tests AX and pops ECX.

### weapon_tally 40fbdc

cdecl (G, out); `out` is a five-word array.

1. `list = R32(G+4)`, `n = R16(G+8)` (frame copies), then
   `memset(out, 0, 10)` (CW3220 import thunk 49681a, through the bridge)
   with EAX = G+4, EDX = list, ECX = `lo(entry ECX, n)`.
2. Loop: `old = n; n -= 1` (word); EDX = `lo(EDX, old)`; old = 0 ends the
   loop, else the slot address `p = list; list += 4`, and p = 0 ends it as
   well (EAX = 0 on both exits).
3. `W = R32(p)`; W = 0 skips (EDX = 0). Otherwise `k = AX(W.V60(W, &local))`
   (entry EAX = &local, a frame word; EDX = W's vtable; ECX = the current
   ECX); k = 5 skips; else ECX = `sx(k)` and
   `W16(out + 2·sx(k), R16(out + 2·sx(k)) + R16(&local))` (AX =
   `lo(V60's EAX, local)`).

Result EAX = 0, EDX = `lo(EDX, 0)` (or the nonzero old count when the slot
address wraps to 0). **Preconditions:** V60 writes the local word when it
returns AX ≠ 5 (true for both weapon classes; otherwise the original
reads stack residue); when called from 41aa5c the returned index must be
0..5, because `out` lies in 41aa5c's frame (other indices write into its
locals). EDX's high word can be a frame address after the real V60 (40e644
returns its pointer argument in EDX); EDX is dead at the only caller (41ac29
`xor edx,edx`). Branch sites 40fc0c, 40fc1f, 40fc38, 40fc47.

### combat_rating 41edd8

cdecl (A); frame: out = 84 bytes at EBP-74h; P = C+2.

1. `condition_record(A, out)` (EAX = P, EDX = out, ECX = entry ECX).
2. `sum = sx(R16(P+42h))` (32-bit).
3. Weapons, i = 0 while `i < sx(R16(G+8))` (G re-read at every test and
   in the body): `W = R32(R32(G+4) + 4·sx(i))`; a null W (41ee24) and a
   negative `c = R16(out+40h+2i)` (41ee2a) skip; `W.V54(W)` (EAX = EDX = W,
   ECX = its vtable) with AX = 0 skips (41ee39); else
   `sum += multiply_q8(sx(R16(R32(W+1Ch)+4Eh)), 100h - sx(c))` (EAX = the
   first argument, EDX = ECX = the second).
4. Groups i = 0..18: if `c = R16(out+2+2i)` ≥ 0 (41ee83),
   `sum += multiply_q8(sx(R16(R32(R32(A+212h)) + 18i)), 100h - sx(c))`
   (EAX = the base, EDX = ECX = 100h − c).
5. Parts i = 0..9: if `s16(R16(out+28h+2i)) > 0B4h` (41eedb),
   `sum -= sx(R16(P+7Ch+2i))` (EDX = that value).
6. `W16(A+29Eh, sum >> 4)` (SAR), `W8(A+94h, 1)`; EAX = `lo(A+92h,
   R16(A+29Eh))`; EDX as left by the last step that wrote it.

**Precondition:** the weapon count `sx(R16(G+8)) ≤ 10` (step 3 reads the
ten group words out+40h..53h; with more weapons the original reads its own
locals, as in `group_fire`); this holds over all shipped `gl\*.GL` lists
(at most 10 entries, `tools/reference-game/scripts/domain_gun_lists.py`).
Branch sites 41ee24, 41ee2a, 41ee39, 41ee78,
41ee83, 41eec9, 41eedb, 41eeed. A+29Eh is read by the AI states
(41cae2…41cdd2); 41aa5c calls this only while byte A+94h is 0.

### ready_tone 41b0bc

cdecl (A); globals `t0` = byte 49a1d0, `t1` = byte 49a1d1. Entry
registers of every call: EAX = A, EDX = ECX = A+92h unless noted.

- Byte A+9Bh ≠ 0 (41b0ce): `t1 = 1`; `467828()` (clock: `(timeGetTime() -
  base) >> 4`); if bit 40h of AL is clear (41b0de): `t0 = 0`. Otherwise,
  unless `t0` ≠ 0 (41b0e7): `462830(15h)` with 467828's registers, `t0 = 1`.
- Else if `t1` ≠ 0 (41b10a): `462830(16h)`, `t1 = 0`.
- Else if byte A+9Dh ≠ 0 (41b123): `462830(14h)`.

Result: the last callee's EDX:EAX, or EAX = A, EDX = A+92h. The clock
467828 stays original code behind the bridge; the sound call 462830 is the
`sfx_play` hook ([audio.md](audio.md#soundclock-isolation-the-sfx-hooks)),
also entered through the bridge. The two flag bytes are part of the
hook's state.

### vehicle_update 41aa5c

cdecl (A). `buf` = five words at EBP-20h; `Wp` = `R32(A+202h) + 0Ah`,
computed once at 41ac16.

1. `k = AX(movement_step(lo(entry EAX, R16(A+256h))))` (the pushed dword
   carries the entry EAX's high word); `A+292h += k` (word).
2. `v = G.V0(G, lo(EDX, R16(A+292h) - 500), A)` (the slot-0 bodies 41194a
   and 411a1d around `group_fire`; EAX = the vtable, ECX = G); then
   `e = energy_accumulate(A+222h, v)`; word A+292h = `AX(e) + 500`,
   clamped: ≥ 10000 → 10000 (41aac4), ≤ 0 → 0 (41aacf).
3. If byte A+A3h ≠ 0 (41aae8) and `sx(R16(A+222h)) + sx(R16(A+224h)) >
   5DCh` (41ab17): byte A+B0h = 0.
4. Countdowns: `timer_dword_step(A+272h)`, `(A+277h)`,
   `timer_word_step(A+26Ah)`, `timer_dword_step(A+27Ch)` (EAX = 0 →
   dword A+281h = 0, 41ab68), `timer_word_step(A+285h)` (AX = 0 → word
   A+286h = 0FA0h, dword A+288h = 0, 41ab8b).
5. `combat_rating(A)` if byte A+94h = 0 (41aba8).
6. If T ≠ 0 (41abb8) and `AX(device_word(A))` ≠ 0 (41abc4): byte T+A2h = 1.
   Byte A+A1h = (`AX(device_state(A))` ≠ 0) (41abe6).
7. `weapon_tally(R32(A+202h), buf)` (EAX = G, EDX = Wp, ECX = buf); the
   five words at Wp are cleared (the loop's compares at 41ac36/41ac3b feed
   no later branch; site 41ac39 only jumps over the second one). Byte A+9Bh
   = 0. T = 0 (41ac5d) → step 13.
8. Lock roll, `si` = 0: if word T+1A8h = 0 (41ac6d), byte T+A1h ≠ 0
   (41ac7d) and `AX(timer_word_step(A+267h))` = 0 (41ac9c): `d = 14h`, or 5
   if `R32(A+30Bh)` ≠ 0 (41acae) and `sx(R16(R32(A+30Bh)+7Fh)) < 33h`
   (41acbd); `x = sx(AX(rng_next(4d2599h))) & 0FFFh`; x < 41·d (41acf0; `lea ecx,[edx+4*edx]`, `lea ecx,[edx+8*ecx]`)
   → byte A+9Ch = 1, word A+268h = 5000, si = 1; else byte A+9Ch = 0, word
   A+268h = 5DCh. The first two failures clear byte A+9Ch; an unexpired
   timer leaves it.
9. Bearing: if word C+50h ≠ 0 (41ad47): `di = 0`, `ang = AX(heading_point(T+26h,
   A+26h)) - R16(A+10h)`. Else `di = min(dist_gate(A+26h, T+26h) >> 2
   (SAR), 7FFFh)` (signed, 41ad9e) and `ang = AX(heading_point(T+26h,
   A+26h)) - R16(A+10h) + R16(A+298h)`.
10. `s = sight_check(A, T)` if byte `R32(A+45h)+12h` = 0 (41ade3), else
    `sight_check(T, A)`; only AX is used (sign-extended).
11. If byte A+9Dh ≠ 0 (41ae1d), or `(uint16)(ang + 3000h) ≥ 6000h`
    (41ae36), or s = 0 (41ae3c): words A+259h, A+25Ch, A+25Fh = di and the
    frame flag `f = 1`. Otherwise, for the tally words:
    - buf[0] ≠ 0 (41ae7b): byte A+96h = 0 (41ae8d) or si ≠ 0 (41ae92) →
      word A+259h = di; else `timer_word_step(A+258h)` with AX = 0
      (41aebe) and byte A+9Ch = 0 (41aed0) → Wp[0] = 1.
    - buf[4] ≠ 0 (41aedf): the same with A+265h, timer A+264h, Wp[4]
      (41aef1, 41aef6, 41af22, 41af34).
    - buf[1] ≠ 0 (41af44): si ≠ 0 (41af49) → word A+25Ch = di; else timer
      A+25Bh with AX = 0 (41af75) and byte A+9Ch = 0 (41af87) → Wp[1] = 1.
    - buf[2] ≠ 0 (41af97): T's bytes +96h and +A1h both 0 (41afaf,
      41afc7) → word A+25Fh = di; else timer A+25Eh with AX = 0 (41aff3)
      → Wp[2] = 1.
12. If byte A+A3h ≠ 0 (41b014): `j = sx8(byte G+1Dh)` with G re-read; j <
    0 (41b021, `inc; jle`) gives k = 5, else `k = AX(W.V60(W, 0))` for
    `W = R32(R32(G+4) + 4j)` (EAX = `R32(G+4)`, EDX = W, ECX = its
    vtable). k = 5 (41b045): byte A+9Bh = 1 if any Wp word is nonzero
    (41b052, 41b067); else byte A+9Bh = 1 if `R16(Wp + 2·sx(k))` ≠ 0
    (41b076). Then `ready_tone(A)`.
13. If f ≠ 0 (41b09a): byte A+9Dh = 0.

Result: EDX:EAX as left by the path (dead: the caller pops ECX and
rewrites EAX/EDX before any read, 45f8b1..45f8bd). No local is read before
it is written (`si`/`di` are set on every path that reads them; the five
`buf` words are written by memset inside `weapon_tally`), and the entry
EAX that 41aa5c forwards is one of the framework's entry registers, so no
entry shim. No global is accessed directly; the callees use the RNG state
4d2599h, the time scale and the tone flags. 50 branch sites, all listed in
`tools/reference-game/invitro/branch_sites.h`.

**Bridge calls.** Hooked callees: `movement_step`, `energy_accumulate`,
`timer_dword_step`, `timer_word_step`, `rng_next`, `heading_point`,
`dist_gate`, `sight_check`, `multiply_q8`, `condition_table`, the six
leaves above, the group's V0 bodies (`group_fire_sel` 41194a,
`group_fire_any` 411a1d, which call `group_fire`), the weapon virtuals V54
(`weapon_stock`, `gun_stock`) and V60 (`weapon_ammo`, `gun_ammo`) and the
sound call 462830 (`sfx_play`). Original code behind the bridge: memset
49681a and the clock 467828.

**Further preconditions of 41aa5c.** With byte A+A3h set and `sx8(byte
G+1Dh)` ≥ 0 the selected weapon slot must be non-null (V60 is called
unconditionally); `weapon_tally` iterates the unsigned count word, so the
count must be small and non-negative there (10 or less by `group_fire`'s
domain).

**Audio order ([audio.md](audio.md#clock-and-rng)).** 462830 draws RNG
stream B for sound rows with variants, and sound calls advance the
intra-frame virtual clock read through 467828. The replacement keeps every
clock and sound call of 41b0bc, in the original order relative to the RNG
and timer users of 41aa5c, through the bridge; one path of 41aa5c returns
462830's EAX through 41b0bc, which the caller ignores.

## Actor-list pass

The logic-frame child `0x4123ac` (row 16 of the direct-call table in
[main_loop.md](main_loop.md#direct-calls-in-execution-order): called once
per logic frame at 45f881 while the freeze word is clear) and its three
helpers. Instruction counts from the bytes: 121, 76, 217 and 77 (the map's
76/217/77 for the helpers agree), no jump table, and no dword of the image
holds any of the four addresses (raw scan). The actor list, the actor
fields and the virtuals V3c/V44/V48 are those of [Actor list and actor
fields](#actor-list-and-actor-fields).

| Hook | VA | Arguments (cdecl) | Role (names are hypotheses) | Callees |
|---|---|---|---|---|
| `actor_pass` | 4123ac | () | per-frame pass over the actor list: contact expiry timers, contact scans of side-0 actors, clears the per-frame byte +A2h | `timer_word_step` ×2, `rng_bounded`, `contact_expire`, `contact_scan` |
| `contact_expire` | 41251c | (actor A) | forgets mutual contacts with hostile actors that are out of range or out of sight | `dist2d_point`, `sight_check` |
| `contact_scan` | 4128f8 | (actor A) | A (side 0) against every side-1 actor B: alert bytes, mutual contact, warning triggers, sight inside the facing arcs | `dist_gate`, `sight_check`, `heading_point`, `trigger_fire`, `contact_share`, V3c, V44, V48 |
| `contact_share` | 412704 | (actor P, actor Q) | P's allies within 100000 of Q learn Q; within 50000 Q is marked and the ally's trigger fires | `dist_gate`, `trigger_fire` |

Callers: 4123ac only from 45f881; 41251c only from 41241d; 4128f8 from
4124a3 and 4124d0; 412704 from 412b4d, 412b84 and 411af7 (411aec, the
building virtual +50h and the direct call 41f853 in the vehicle virtual
+50h 41f7b8).

`actor_pass`, `contact_expire` and `contact_share` refuse compare mode
(they write actors and call game code); `contact_scan` accepts it with
snapshot regions and guards (`actor_pass_hooks.h`).

The registers the original holds at every game call are tracked in the
core and passed through the guest bridge; they are listed only where they
are not obvious from the steps. After each call the cdecl `pop ecx`
reloads ECX with the (unchanged) first argument slot.

### actor_pass 4123ac

No arguments, no entry register read. Frame: cursor at EBP+0, index word
at EBP+4 (EBP = entry ESP − 18h).

1. Loop 1 (EAX = the array at the start): for each actor B whose group is
   not pending (4123d5): `timer_word_step(B+1E2h)` (result ignored), then
   `timer_word_step(B+1E5h)`; if its AX = 0 (4123fd, the timer expired):
   `W16(B+1E6h, AX(rng_bounded(4d2599h, 1000)) + 10000)` and
   `contact_expire(B)`.
2. Loop 2 (EDI = 0 = held actor): for each B whose group is not pending
   (412467), whose bytes B+A4h and B+99h are 0 (412475, 41247d, 41248b)
   and whose side byte G+12h is 0 (412494): if byte B+A3h ≠ 0 (41249c) B
   becomes the held actor (the last such one wins), else `contact_scan(B)`.
3. If an actor is held (4124cd): `contact_scan(held)`.
4. Loop 3: for each B, byte B+A2h = 0 (no group test).

Entry registers: timer 1 EAX = B+1E2h, EDX = G, ECX = 0; timer 2 EDX =
B+1E5h, ECX = B+1E2h; `rng_bounded` gets timer 2's EAX/EDX and ECX = 1;
`contact_expire` gets `rng_bounded`'s EDX/ECX and EAX = its result plus
10000 in the low word; in loop 2 `contact_scan` gets EAX = EDX = B+92h, ECX
= G; the held call gets the registers of loop 2's last step.

Result: EAX as left by the last step of loop 3 (B+92h, or the array if no
actor was visited); EDX = the dword at EBP+4 and ECX = the cursor, both
popped from the frame. **EDX's high word is uninitialized stack** (only
the low word of that dword is ever written). All three registers are dead
at the only caller (45f886 clears EAX, 45f8b6 reloads EDX, ECX is written
at 45f894 or in 471c18 before any read), so the replacement returns EDX's
high word as 0 and needs no entry shim; the in-vitro comparison masks that
word. The RNG draw goes through the bridge into `rng_bounded` (stream A,
state 4d2599h) in the original order relative to the timers and the
helpers.

### contact_expire 41251c

cdecl (A). Byte A+95h = 0 first. `s` = the sign-extended side byte of A's
group (read once). For each B whose group is not pending (41255d), whose
side differs from s (412566, word compare of the sign-extended bytes) and
for which byte `A+C2h+sx(R16(B+4Bh))` ≠ 0 (412586):

- `d = dist2d_point(A+26h, B+26h)`; if d ≤ 100000 (signed, 41259d) and
  `AX(sight_check(A, B))` ≠ 0 (4125ac), the contact stays;
- otherwise byte `A+C2h+sx(R16(B+4Bh))` = 0 and byte
  `B+C2h+sx(R16(A+4Bh))` = 0 (both slot words re-read after the calls).

Entry registers: `dist2d_point` EAX = A+C2h, EDX = B+26h, ECX = A+26h;
`sight_check` gets `dist2d_point`'s three result registers. Result:
EDX:EAX as left (EAX's low word = the last index loaded). Every local is
written before it is read. No entry shim.

### contact_share 412704

cdecl (P, Q). If the sign-extended side bytes of P's and Q's groups are
equal (412724): return with EAX = Q's group, EDX = Q's side. Otherwise the
slot word `R16(Q+4Bh)` is read **once** (41272a), and for each X whose
group is not pending (412755) and whose side equals P's (41275e):

1. `d = dist_gate(X+26h, Q+26h)` (EAX = Q+26h, EDX = X+26h, ECX = X's
   side); d ≥ 100000 (signed, 41277b) → next.
2. Byte `X+C2h+sx(slot)` = 1; d ≥ 50000 (41279e) → next.
3. Byte Q+9Eh = 1; if `R32(X+1B2h)` ≠ 0 (4127b3) and byte X+A2h = 0
   (4127bf): `trigger_fire(R32(X+1B2h))` (EAX = X+92h, EDX = the trigger,
   ECX = sx(slot)).

Result: EDX:EAX as left. ECX is not part of the contract (the framework
returns the entry ECX): after 412b4d/412b84 it enters V3c (none of the
three bodies reads it) and is then popped; 411aec passes it out of the
virtual +50h (41f7b8 and the building tables), whose callers write it
before reading it (410c11 or 41b1d7, 42680a; 4665b3 calls slot +50h of an
unrelated object).

### contact_scan 4128f8

cdecl (A). At entry EAX = `R32(R32(R32(A+45h)+0Ch)) + 92h` (the first
member of A's group + 92h; only its high word survives into the first
list step, and only into the result if the list ends at once). For each B:

1. Skip if byte B+A4h ≠ 0 or byte B+99h ≠ 0 (412930, 412938, 412946), if
   B's group is pending (41295c) or if B's side byte is not 1 (41296a).
2. `d = dist_gate(A+26h, B+26h)` (EAX = B+26h, EDX = A+26h, ECX = 1).
3. Alert: unless bytes A+95h and B+95h are both set (41298c, 412998): if
   byte A+96h or byte B+96h is set (4129a8, 4129b4), d < 200000 (4129bc)
   and `AX(sight_check(A, B))` ≠ 0 (4129cb):
   - if byte B+96h ≠ 0 or d < 140000 (4129d7, 4129df): `V48(A, B)` (EDX =
     A's vtable) and byte B+95h = 1;
   - if byte A+96h ≠ 0 or d < 140000 (412a05, 412a0d): byte A+95h = 1.
4. d ≥ 80000 (412a25) → next.
5. If A knows B and B knows A (412a49, 412a6d): d ≥ 50000 (412a75) →
   next; else byte B+9Eh = 1, `trigger_fire(R32(A+1B2h))` if that dword ≠
   0 (412a8d) and byte A+A2h = 0 (412a99); byte A+9Eh = 1, then the same
   trigger test for B (412aba, 412aca) → next.
6. Otherwise: `h = heading_point(B+26h, A+26h)` (full EAX);
   - if `AX(V44(A, lo(V3c(A), AX + h)))` ≠ 0 (412b0b; the pushed dword
     keeps V3c's high word) and `AX(sight_check(A, B))` ≠ 0 (412b1a):
     `V48(A, B)`, then byte `A+C2h+sx(R16(B+4Bh))` = 1 if byte A+A3h ≠ 0
     (412b30), else `contact_share(A, B)`;
   - then (always) if `AX(V44(B, lo(h, h + AX(V3c(B)) + 8000h)))` ≠ 0
     (412b71) and `AX(sight_check(A, B))` ≠ 0 (412b80):
     `contact_share(B, A)`.

Both `sight_check` calls of step 6 are (A, B), also before
`contact_share(B, A)`. The trigger dwords are read twice (test, then
push), the slot words after the calls. Result: EDX:EAX as left. Every
local is written before it is read. No entry shim.

**Bridge calls.** Hooked callees: `timer_word_step`, `rng_bounded`,
`dist2d_point`, `dist_gate`, `heading_point`, `sight_check`,
`trigger_fire`, the three helpers below `actor_pass`, and the virtuals V3c
(`mech_angle`, `gun_angle`, `actor_angle`), V44 (`actor_in_front`,
`base_in_front`) and V48 (`mech_contact`, which calls the order code
420a98 and `timer_word_step`; `actor_contact`). No sound call, no
allocator call, no direct global write; the RNG is drawn only through
`rng_bounded` (here and inside `sight_check`).

**Branch sites (61):** 15 in 4123ac, 7 in 41251c, 30 in 4128f8, 9 in
412704, listed in `tools/reference-game/invitro/branch_sites.h`.

## Shield, damage, facing and keys

Five more vehicle functions: the shield maximum, the damage score of the
part tables, and the vehicle virtuals +54h and +2Ch. All five refuse
compare mode.

| Hook | VA | Arguments (cdecl) | Role (names are hypotheses) | Callees |
|---|---|---|---|---|
| `shield_max_set` | 413ab8 | (shield record S, value word) | stores the shield maximum word S+6 | none |
| `shield_capacity` | 417bec | (vehicle A) | recomputes the shield maximum A+228h from the configured maximum and two part conditions | `multiply_q10` (up to 2), `part_ratio`, `shield_max_set` |
| `damage_score` | 41537c | (parts P) | weighted sum of the damage fractions of 19 part groups and 12 parts | none |
| `facing_part` | 41b534 | (vehicle A, pose T) | vehicle virtual +54h: the part whose direction from T deviates least from T's angles | V58 (`part_position`), `aim_angles` |
| `vehicle_keys` | 4157c8 | (object O, code) | vehicle virtual +2Ch, the key method below the main command handler | `weapon_keys`, `euler_matrix`, 4801d4, 438d9c |

**Callers.** 413ab8 only from 417cf6 (in 417bec). 417bec from 41786b (in
4175dc `weapon_fit`) and 41802e (in `vehicle_part` 417de4, which reaches
it through the guest bridge); both pop ECX right after the call. 41537c
only from 418f1e in 418e60 (`vehicle_value`; `pop ecx`, then EDX is
reloaded before any read). The raw scan finds 41b534 only at 49a2d6 = slot
+54h and 4157c8 only at 49a2ae = slot +2Ch of the vehicle vtable 49a282.
Slot +54h is called at 40aacb and 41eac5 (the walk for `part_facing`, the
building implementation of the same slot,
[weapons.md](weapons.md#406868-part_facingb-p)): both read AX only and ECX
is dead there. Slot +2Ch is called at 460841 and 460854 in the main
command handler 45feb8 with the code dword in ESI pushed as the second
argument; both keep the whole EAX (`mov ebx,eax`, then `lea eax,[edi+ebx]`
is 45feb8's result), while EDX and ECX are dead after them (the walk for
`weapon_keys`, [input.md](input.md#weapon_keys-410f78), which escapes only
through 4157c8's return). So no hook returns ECX (the framework gives the
entry ECX back); EDX:EAX is exact for all five.

**Not drawing, no sound, no RTL.** 4157c8 does not read an input device:
it gets the key code from its caller, and its only non-simulation callee is
438d9c (`button_press`, press button i of a widget group,
[cockpit.md](cockpit.md#the-shield-balance-chain)). 4801d4 is
`matrix_point`, as in `part_position`. Every hooked callee is entered
through the guest bridge with the registers the original holds at the
call, so nested hooks keep their own modes.

### shield_max_set 413ab8

cdecl (S, value). `W16(S+6, value)`; EAX = S, EDX = `lo(entry EDX,
value)` (`mov dx,[ebp+0Ch]`), ECX unchanged. The shield record is
vehicle+222h: front and rear shield words at +0/+2, a word +4 (200h at
creation), the maximum at +6 and +8 (the creation routine 413a90 sets +6
and +8 to the configured maximum and +0/+2 to half of it,
[dat.md](../formats/dat.md)). No branch.

The DBSIM map's entry probe shows 413ab8 with 0 hits although 417bec
calls it unconditionally (417cf6); with 7 instructions it is probably too
short for the probe. With the hooks replaced its call count equals
417bec's in every suite of `check-r15-1`.

### shield_capacity 417bec

cdecl (A). P = A+206h (the [part tables](#the-part-tables-p-vehicle-206h)).
The frame locals (EBP-4 = the running total, EBP-8..-14h argument copies)
are written before they are read; no entry register is read.

1. EBX = A, ECX = P, EDX = `R32(P)`, EAX = `R32(P+0Ch)`, ESI =
   `sx(R16(EDX+8))` (the value word of part 4), EDI = `R32(EAX+4)` (the
   part maxima).
2. If s32 ESI < 0 (JGE 417c0c not taken): AX = 100h (EAX keeps
   `R32(P+0Ch)`'s high word); else ECX = `sx(R16(EDI+8))` (part 4's
   maximum) and EDX:EAX = IDIV(ESI << 8, ECX). Then q = `sx(AX)`.
3. EAX = C = `R32(A+1F2h)`, EDI = base = `sx(R16(C+0C0h))` (the configured
   shield maximum, read once).
4. If s32 q < 80h (JGE 417c3a not taken): total = base. Else t = IDIV(q −
   80h, 25) (quotient), k = 400h − 102·t (computed as 3t, 25t, 51t, 102t
   with 32-bit wrap), `multiply_q10(base, k)` with EAX = ECX = k, EDX =
   base; total = its full EAX.
5. EAX = X = `R32(A+30Fh)`. If X ≠ 0 (JE 417c83 not taken):
   - EDX = D = `R32(X+18h)`, e = the sign-extended byte D+17h, slot =
     `lo(e, e + 13h)` (the dword keeps the byte's sign in its high word);
     `part_ratio(P, slot)` with EAX = P, EDX = D, ECX = the ECX of the
     path so far (P, part 4's maximum, or `multiply_q10`'s);
   - s = `sx(AX)`, EDX = s. If s32 s < 0E1h (JGE 417cae not taken): t2 =
     IDIV(s, 51), k2 = 400h − 204·t2 (51·t2, shifted by 2),
     `multiply_q10(k2, base)` with EAX = base, EDX = ECX = k2; total += its
     EAX.
6. ECX = `lo(ECX, total)`; `shield_max_set(A+222h, ECX)` with the EAX/EDX
   the path left (X = 0: EAX = 0; s ≥ E1h: `part_ratio`'s EAX and EDX = s;
   else the last `multiply_q10`'s registers).

**Result.** `shield_max_set`'s EDX:EAX: EAX = A+222h, EDX = `lo(EDX before
the call, total)`. Memory: A+228h = the low word of total, through the
callee.

**Preconditions.** Part 4's maximum word 0 with a value ≥ 0 faults (#DE)
like the original. The dividends cannot overflow (|ESI| < 2^15, shifted by
8; q and s are 16-bit).

**Branch sites (4):** 417c0c, 417c3a, 417c83, 417cae; all reachable both
ways.

### damage_score 41537c

cdecl (P), a pure reader. Weights: the word tables at 499fe0 (19 words)
and 49a006 (12 words) in the image's data section; the raw scan finds no
other reference to them, and the core reads them from the image.

1. Groups 0..18 (loop 41539d..4153ee, dword counter): with the flag words
   F = `R32(P+8)` and group values G = `R32(P+4)` read once, if `R16(F+2i)`
   ≠ 0 and 0 ≤ s16 `R16(G+2i)` < 96h: d = `sx(R16(R32(R32(P+0Ch)) + 18i))`
   (the group's base, re-read every iteration), EAX = (d − sx(G word)) ·
   sx(w1[i]) (IMUL, low dword; CDQ discards the product's high dword),
   EDX:EAX = IDIV(EAX, d), sum += EAX.
2. Parts 0..11 (loop 415405..41543f): with the part maxima M =
   `R32(R32(P+0Ch)+4)` and part values V = `R32(P)` read once, if
   `R16(M+2i)` ≠ 0 and 0 ≤ s16 `R16(V+2i)` < 96h: EAX = (sx(M word) −
   sx(V word)) · sx(w2[i]), EDX:EAX = IDIV(EAX, sx(M word)), sum += EAX.

**Result.** EAX = sum (32-bit); EDX = the remainder of the last division,
or the entry EDX if nothing was divided. ECX (P's value pointer + 24) is
dead at the caller (`pop ecx`).

**Preconditions.** A zero group base d of a counted group faults (#DE)
like the original (loop 2 tests its divisor). |d − value| ≤ 32917 and |w|
≤ 8000h, so the product fits in 31 bits and neither the CDQ nor the IDIV
can overflow.

**Branch sites (8):** 4153a1, 4153a7, 4153ae, 4153ee, 415409, 41540f,
415416, 41543f; all reachable both ways.

### facing_part 41b534

Vehicle virtual +54h (the vehicle's counterpart of `part_facing`,
[weapons.md](weapons.md#406868-part_facingb-p)). cdecl (A, T); T is a pose
(three angle words at T+0/+2/+4, position at T+1Ah) and is dereferenced
unconditionally (T = 0 faults; the callers pass 0 only for type-1 targets
at 41eac5). Frame 18h: L18 = EBP-18h (12-byte part position), Lc = EBP-0Ch
(three angle words), best index word EBP-6 (init FFFFh), best value dword
EBP-4 (init 1000000h).

1. ESI = `R32(A+20Eh)` (a word per part, read once), EAX = A, EBX = i = 0.
2. For i = 0..28 (EBX 32 bits, loop test `cmp bx,1Dh` / JL 41b5f9): if
   `R16(ESI)` ≠ 0 (JE 41b558 not taken):
   - V58(A, i, &L18) with EAX = &L18, EDX = A, ECX = `R32(A)` (re-read);
   - `aim_angles(&Lc, &L18, T+1Ah)` with EAX = T+1Ah, EDX = &L18, ECX =
     &Lc;
   - Lc+4 −= `R16(T+4)`, Lc −= `R16(T)`, Lc+2 −= `R16(T+2)` (16-bit, in
     that order; Lc+2 is never used). Registers: EDX = `lo(aim EDX, T+4
     word)`, ECX = `lo(aim ECX, T+0 word)`, EAX = `lo(T, T+2 word)`;
   - a = |s16 Lc| (8000h gives 7FFFh; JNE 41b5a8, JGE 41b5b3) into CX with
     the magnitude rule of `target_bearing` (`lo(ECX, 7FFFh)`, `lo(EAX,
     −w)` or EAX), EAX = `sx(CX)`; c = |s16 Lc+4| the same from EDX (JNE
     41b5ca, JGE 41b5d5), EDX = `sx(CX)`; EAX = a + c;
   - if s32 EAX < best (JGE 41b5e8 not taken): best = EAX, index = i.
   ESI += 2.

**Result.** EAX = `lo(EAX, index)`: the index zero-extended if any part was
measured (a + c ≤ FFFEh), else `lo(A, FFFFh)`; EDX = c of the last measured
part, else the entry EDX. Unlike 406868 there is no early exit for T = 0
and the part count is the constant 29.

**Uninitialized stack.** `aim_angles` reads L18, which only V58 writes;
`part_position` (41b60c, the vehicle's V58) writes all 12 bytes on every
path, so no residue is read.

**Branch sites (7):** 41b558, 41b5a8, 41b5b3, 41b5ca, 41b5d5, 41b5e8,
41b5f9; all reachable both ways (41b5e8: the first measured part always
beats 1000000h, a later tie or larger sum does not).

### vehicle_keys 4157c8

Vehicle virtual +2Ch, the key method (classified in
[launcher.md](launcher.md) and [input.md](input.md)): cdecl (O, code), O =
the player actor or the view subject, code = the scan-code-like key code
with 200h for Alt, 400h for Ctrl, 800h for Shift. The dispatch uses the
low word of the code only. Globals: V = `R32(4a9b84h)` (the vehicle whose
weapon system and request words are used, read twice), the cheat flag
dword 49ef38h, the step words 4a9ccch (move step) and 4a9cceh (turn step),
the cockpit H = `R32(49b088h)`, and the step tables 49a020h/49a032h (nine
words each, addressed as 49941ch/49942eh + 2·code).

1. EAX = V, EDX = W = `R32(V+202h)`; `weapon_keys(W, code)` with ECX = the
   entry ECX and **ESI = the caller's ESI** (410f78 returns its high word).
   If AX ≠ 0 (JE 4157ef not taken): EAX = `lo(weapon_keys' EAX, 1)`, EDX =
   its EDX; done.
2. SI = 1 (ESI keeps the caller's high word; this ESI is the result of
   every handled code below and is live in ESI at the later calls). Then a
   compare/subtract chain on k = the code's low word (no jump table):

| k | Key | Effect (flag = dword 49ef38h ≠ 0) | EDX at the end |
|---|---|---|---|
| 1Ah, 1Bh | `[`, `]` | `438d9c(R32(H+1E9h), k ≠ 1Bh, 1)` with EAX = H, EDX = `R32(H+1E9h)`, ECX = 0/1: the shield balance buttons of the gauge `[H+1E9h]` (no flag test) | 438d9c's |
| 4Ch | Num5 | V = `R32(4a9b84h)` (re-read): word V+290h = 0, byte V+93h = 1 (no flag test) | weapon_keys' |
| 248h, 24Bh, 24Dh, 250h | Alt+Up/Left/Right/Down | with flag: if word O+32h = 0, `euler_matrix(O+0Ch, O+12h)` (EAX = O+12h, EDX = O+0Ch, ECX = weapon_keys') and O+32h = 1; the vector (0, s, 0), (−s, 0, 0), (s, 0, 0) or (0, −s, 0) with s = `R16(4a9ccch)` in its own frame slot (EBP-24h, -0Ch, -18h, -30h); `4801d4(&vector, O+12h, &F)` (F = EBP-3Ch, EAX = &vector, EDX = `lo(EDX, ±s)`, ECX = &F); then O+2Eh = F+8, O+26h = F+0, O+2Ah = F+4 | `R32(F+4)` |
| 44Bh, 44Dh | Ctrl+Left/Right | with flag: O+10h = `R16(O+10h) ± R16(4a9cceh)`, word O+32h = 0 | `lo(weapon_keys', new heading)` |
| 602h..60Ah | Alt+Ctrl+1..9 | with flag: 4a9ccch = `R16(49941ch + 2k)`, then 4a9cceh = `R16(49942eh + 2k)` | `lo(weapon_keys', move step)` |
| other | | none | weapon_keys' |

Without the flag the flagged codes do nothing (EDX = weapon_keys').
**Result.** EAX = 0 for "other", else `lo(caller's ESI, 1)` (also when the
flag is clear: SI was set before the dispatch). ECX and EDX are dead at
both callers (see above); EDX is returned exactly anyway.

**Entry shim.** The result's high word is the caller's ESI, which no
replacement sees, and `weapon_keys` needs it too. 4157c8 overwrites EAX
(`mov eax,[4a9b84h]`) and EDX before reading them, so the shim
`vehicle_aux_keys_entry` copies ESI into EAX before the framework's
dispatch; the core reads the caller's ESI from its entry EAX and the
caller's ECX from its entry ECX. Every game call goes through
`weapon_system`'s ESI-loading bridge: `weapon_keys` with the caller's ESI,
the later calls with SI = 1. EBX (the code) and EDI (O) are the bridge's at
those calls; 47ebb8, 4801d4 and 438d9c save them before use (438d9c's
widget method and the clock call in 4530bc were not read further for
them). The shim is installed from the `default:` of
`weapon_system_capture_entry` and passes every other hook on to
`ai_behaviour_capture_entry`. Compare mode is refused (an original phase
would see the dispatcher's ESI).

**Branch sites (22):** 4157ef, 415806, 415808, 415814, 41581d, 415824,
41582d, 41583d, 415848, 415851, 41585f, 415889, 4158bc, 4158ca, 415933,
415941, 4159a7, 4159b5, 415a1b, 415a29, 415a8f, 415ab2; all reachable both
ways (the code, the flag and the cache word O+32h are inputs).

## Vehicle part and weapon-group helpers

Seven small functions called by the hooks above and by `vehicle_part`,
`random_damage` and `base_offsets`.

| Hook | VA | Arguments (cdecl) | Role (names are hypotheses) | Callees | Callers |
|---|---|---|---|---|---|
| `group_fire_any` | 411a1d | (group G, value word V, owner O) | slot 0 of the weapon group table 499338: one `group_fire` pass with no preferred weapon | `group_fire` | 41aa97 (`vehicle_update`, through `[G]`) |
| `group_fire_sel` | 41194a | (G, V, O) | slot 0 of table 499238: the same pass preferring the selected weapon s8 `[G+1Dh]` | `group_fire` | 41aa97 |
| `part_list_dead` | 40cf10 | (group record R, part values Q) | 1 if every part listed by R is destroyed | none | 40da26 (in 40d9f8) |
| `part_group_dead` | 40d9f8 | (parts P, index word i) | 1 if group i and all its parts are destroyed | 40cf10 | 4183e0, 4183fa (`vehicle_part`) |
| `part_group_max` | 40dc58 | (P, index word i) | maximum of group i: its base plus its parts' maxima | none | 417a6c (`random_damage`) |
| `mission_guard` | 4240f4 | () | predicate over the global words 4a9e52/4a9e5a/4a9e5c (4a9e5c is the difficulty word) | none | 417e05 (`vehicle_part`, player guard) |
| `actor_offsets` | 411a74 | (object X, record A, record B) | ACTOR/FLYER slot +30h: clears the two 6-byte offset records | memset 49681a (twice) | slot +30h at 4042d4, 40473c, 41eafd, 45edc5; 40352d (`base_offsets`) |

`part_list_dead`, `part_group_dead`, `part_group_max` and `mission_guard`
accept compare mode; the two slot-0 bodies refuse it (`group_fire`'s
weapon virtuals), and `actor_offsets` refuses it (its two records are
written through game code behind argument pointers).

**Entries.** The raw scan of the image finds 411a1d only at 499338 and
41194a only at 499238 (slot 0 of the two weapon group tables, file
offsets 98538h and 98438h), 411a74 only at 49a57c and 49a610 (slot +30h of
the ACTOR table 49a54c and the FLYER table 49a5e0), and no dword equal to
40d9f8, 40cf10, 40dc58 or 4240f4; the direct calls are the ones in the
table. 411a1d and 41194a are not 4-aligned but are real entries: the bytes
before 411a1d end with the `ret` of 4119c3 (411a1c), those before 41194a
are data (a name string and a small table ending in the zero bytes
411947..411949), the vtable slots point at them, and both begin with `push
ebp; mov ebp,esp; mov eax,[ebp+8]` (six relocated bytes, whitelisted).
40cf10 and 40dc58 relocate `push ebp; mov ebp,esp; push ebx|ecx; push
esi|ebx` (five bytes), 40d9f8 `push ebp; mov ebp,esp; push ebx; mov
edx,[ebp+0Ch]` (seven), 411a74 `push ebp; mov ebp,esp; push 6` (five).
4240f4 begins with `cmp word [4a9e52h],0` (`66 83 3D`, moffs32, imm8:
eight bytes); the prologue whitelist of `hook/hook.c` accepts the
operand-size prefix before `83 /0,/5,/7` (ADD/SUB/CMP r/m16,imm8), whose
length the prefix does not change, while `66 81` (imm16) stays refused
(fixture `hook/tests/hook_fixture.c`, `hook/README.md`). No jump targets
the relocated bytes of any of the seven.

### group_fire_any 411a1d and group_fire_sel 41194a

411a1d: EAX = G, EDX = O; push O, push −1, `mov cx,[ebp+0Ch]` and push
ECX, push G; call 4107e4; `add esp,10h`; return. The value dword pushed is
`lo(entry ECX, V)`: its high word is the caller's ECX. Registers at the
call: EAX = G, EDX = O, ECX = that dword.

41194a: EAX = G, EDX = O; push O; ECX = sx8(byte G+1Dh), push ECX; `mov
dx,[ebp+0Ch]` and push EDX (= `lo(O, V)`), push G; call 4107e4. Registers
at the call: EAX = G, EDX = `lo(O, V)`, ECX = sx8(byte G+1Dh).

Both return `group_fire`'s EDX:EAX unchanged and leave its ECX.
`group_fire` reads the value and index slots as words only (4107f6,
410861, 410873, 410883, 410904) and writes the value slot (41090f,
41095f), which the bodies never read again. Its EAX high word can be a
frame address of its own ([weapons.md](weapons.md#group_fire-4107e4));
41aa97's continuation uses AX only. The index byte G+1Dh must be at most 9
when non-negative (`group_fire`'s visited array, same precondition). In
the core `group_fire` runs through the guest bridge with these registers,
so its hook keeps its mode; `vehicle_update`'s bridged V0 call enters
these hooks the same way (the hook sits at the entry).

### part_list_dead 40cf10

R = record, Q = part values. `list = [R+8]`, k = 0 (EDX); while s16(DX) <
s16(word R+6) (re-read each time, JL 40cf39): if `s16([Q + 2·sx(word
list+2)]) ≥ 0` (JL 40cf2b not taken) return EAX = 0, EDX = k; else k += 1,
list += 4. After the loop: EAX = `lo(list, 1)` (the cursor's high word),
EDX = k. ECX = R throughout. Reads only; no writes.

### part_group_dead 40d9f8

`i = sx(word index)`. If `s16(word [P+4] + 2i) ≥ 0` (JGE 40da0d): EAX = 0,
EDX = the index argument dword, ECX = i. Otherwise call 40cf10(`[[P+0Ch]]
+ 18i`, `[P]`) with EAX = 18i, EDX = the record address, ECX = i, and
return its EDX:EAX (its ECX = the record). Reads in the order [P+4], the
group word, [P], [P+0Ch], [[P+0Ch]]; no writes.

### part_group_max 40dc58

`i = sx(word index)`, T = [P+0Ch], R = `[T] + 18i`, M = [T+4] (read once,
kept in the frame word EBP−4), list = [R+8]; sum = sx(word R); k = 0; while
s16(DX) < s16(word R+6) (re-read, JL 40dc9a): sum += sx(word `M +
2·sx(word list+2)`), k += 1, list += 4. EAX = sum, EDX = k, ECX = R. The
frame slot EBP−4 is written before it is read. Reads only.

### mission_guard 4240f4

No frame. If word 4a9e52 = 0 (JE 424105 not taken): EAX = (word 4a9e5a =
1); else EAX = (word 4a9e5c = 0); both through `sete al; and eax,1` and a
final `movsx eax,al`, so EAX is 0 or 1. EDX and ECX are not touched (the
hook returns the entry EDX; the framework gives the entry ECX back).

### actor_offsets 411a74

X is not read. memset(A, 0, 6) with EAX = A, EDX/ECX = the entry values;
then memset(B, 0, 6) with EDX = B, EAX/ECX = the first memset's. Returns
the second memset's EDX:EAX (EAX = B for the CW3220 memset). Both calls go
through the bridge to the thunk 49681a, so `hk_actor_offsets_call_49681a`
counts two per call and an RTL memset hook keeps its mode.

### Callers' registers (all seven)

The framework restores the entry ECX after a hook; the originals leave
`group_fire`'s ECX (411a1d, 41194a), R (40cf10, 40dc58), i or R (40d9f8),
the entry ECX (4240f4) or memset's (411a74). Every caller writes ECX
before reading it: 41aa97's continuation (weapons.md, `group_fire`);
4183e5 and 4183ff (`lea ecx` at 41840b, or `mov ecx` at 4184db after the
jump to 4184b9); 417a71 (`movsx edx,ax`, then `mov ecx,[ebp-0Ch]` at
417a7a); 417e0a: the AX test, then either the exit 4187c9 (`vehicle_part`
returns its pair; ECX is not part of the `vehicle_part` hook's contract)
or 417e13, which writes EAX/EDX and leaves on the same exit or writes ECX
at 417e3d; 403532 (`mov cx` at 40353b; `base_offsets` returns EAX =
411a74's EAX). The slot +30h sites that can hold an ACTOR or FLYER (4042d4,
40473c, 41eafd: `movsx` into EAX, EDX and ECX before any read; 45edc5: EAX
and EDX written at 45edcb/45edd0, and the callee 40111c writes ECX before
reading it, then 45eddf rewrites it) read no register the hook leaves
different. The other slot +30h call sites (410e5d/410e77 on weapons with
two arguments, 42ff46 and 466718 on objects of the graphics and platform
layer) do not dispatch to 411a74. EDX:EAX is exact for all seven.

**Not drawing, no sound, no RTL.** The memset thunk is RTL and stays
behind the bridge; 411a74 itself only clears two simulation records (the
offset vectors that `aim_select`, `turret` and `lc_base_update` add to a
target's position, [ai.md](ai.md)).

**Branch sites (5):** 40cf2b, 40cf39, 40da0d, 40dc9a, 424105; all reachable
both ways (the part values, the group value word, the counts and the guard
words are inputs). The two slot-0 bodies and 411a74 have no branch.

**Preconditions.** As in the original: the part and group indices must
address valid words (the tables of the vehicle's part object); the
selected byte G+1Dh at most 9 when non-negative.

## In-vitro evidence

`differential_harness <hook>` with the group's cases file
([invitro/README.md](../../tools/reference-game/invitro/README.md)): direct
cases run the original body and the core against logging stubs for every
game callee and virtual, 256 installed 31/a7 poison cases per hook, and
real-callee cases with the nested hooks in rotating modes. Every listed
branch site is taken both ways; the mutation witnesses are in
[MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md).

| Group | Cases file | Direct cases per hook | Real-callee cases | Branch sites | Mutation witnesses |
|---|---|---|---|---|---|
| `vehicle` | `vehicle_leaf_cases.h` | 1,001,024 (no stubs for the two pure condition functions) | 6,144 for `sight_check` (`rng_bounded`), `target_bearing` (`heading_point`, `dist_gate`) and `part_position` (`euler_matrix`, `transform_point`, 4801d4) | 39 | 6 |
| `vehicle_update` | `vehicle_update_cases.h` | 1,001,024 (no stubs for the two device readers) | 6,144 each for `vehicle_update` (14 nested hooks), `combat_rating`, `condition_record` (real `condition_table`) and `weapon_tally` (real memset emulation) | 70 | 7 |
| `actor_pass` | `actor_pass_cases.h` | 1,001,024, with stubs that change the list, groups, slot words, contact tables and flags between calls | 6,144 per hook (nine nested hooks below `actor_pass`) | 61 | 4 |
| `vehicle_aux` | `vehicle_aux_cases.h` | 1,001,024 (no stubs for the two leaves) | 3,072 for `shield_capacity`, `facing_part` and `vehicle_keys` | 41 | 6 |
| `part_helpers` | `part_helpers_cases.h` | 1,001,024 on the full tier, stubs for `group_fire`, 40cf10 and the memset thunk (ordered calls with their arguments and entry registers, the return pair, arena and the three guard words); 512 installed compare cases for each of the four hooks that accept compare | 2,304 each for `group_fire_any` and `group_fire_sel` (over the real group of `weapon_helpers_cases.h`, with `group_fire`, `weapon_ready`, `weapon_step` and `timer_word_step` nested), `part_group_dead` (the real 40cf10 as `part_list_dead`) and `actor_offsets` (the harness's memset; half of the cases under `base_offsets`, which reaches 411a74 through its bridge) | 5 | 7 |

Logs: `<es2-data>\logs\h4-03-20261008` (vehicle leaves) and
`<es2-data>\logs\h4-14-20261008` (per-vehicle update).
