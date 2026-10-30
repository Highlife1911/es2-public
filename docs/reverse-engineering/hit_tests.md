# Actor hit tests, COL traversal, debris burst and region damage

Scope: the actor hit-test virtual `+0x20` of the three actor families
(`BASE` `0x405038`, `MECH` `0x418ba8`, `FLYER` `0x421c8c`), the `MECH` part
hit `0x4188c8`, the `FLYER` part damage `0x421bb4` (slot `+0x74`), the COL
traversal `0x40c9d4`/`0x40c8fc`/`0x40c8c8`/`0x40c4c4`, the shared debris
spawn `0x4089bc`/`0x408530` with its subtree, the `MECH` "player fly"
movement `0x4198f4`, the region-damage helpers `0x40d3ec`, `0x40cf44`,
`0x40ca58`, `0x40ca8c` and the region damage `0x40da38` with the part break
`0x40d434`. Every function here is a hook; the groups are `hit_test`,
`mech_part_hit`, `flyer_part`, `col`, `debris_burst`, `mech_fly`,
`region_damage` and `region_break` ([hooks.md](../reference-game/hooks.md)).
The slot itself (`+0x20`) and the actor families are in
[vtables.md](vtables.md#21-slot-matrix) section 2.1.

**Evidence.** Read from the reference `VER95/DBSIM.EXE` (hash in
`data/reference_hashes.txt`; image base `0x400000`; DATA file offset = VA −
`0x400e00`) with `llvm-objdump -d -M intel` of the pinned LLVM-MinGW, whole
image plus entry-aligned listings of every function named here; the
runtime descriptors, the debris and packet statics were read from the file.
Live counts are the saturated entry probes of the DBSIM map, order
a60/c1s/c1c/c3_09/c4_08/c3_10, 1000 = at least 1000; count-mode values are
from [live_coverage.md](../reference-game/live_coverage.md). The register
flows, memory orders and results are tested in vitro (original against
core, with logging stubs, installed probes and real callees; tests named
in each validation subsection). Caller-side liveness and the residue audits
are static. Role names (in parentheses or marked *hypothesis*) are
hypotheses.

**Notation.** f = the function's EBP; `s16`/`u16`/`s8` sign/zero
extension; `lo(R, w)` a 16-bit load into R that keeps R's high word; "→ X"
a branch to X; "as left" = what the previous call or instruction left in
the register; `[0x6cbee4]` the runtime construction counter (`[[0x6cbee4]]`
decremented by 4 after every constructor call); "the record" the runtime
exception record that `0x49630c` links at f−0x24 (f−0x34 in `0x40d434`).
Register tables list EAX, EDX, ECX at the callee's entry.

## Overview

1. **One dispatch site.** The only call of the actor slot `+0x20` is
   `0x4266c9` in `sweep` (`0x426528`). The other 25 `call [reg+0x20]`
   sites of the image call the shape manager's node lookup
   (`[[obj+0x34]+4]`, word/byte argument), the shape-attachment path
   `0x420090` (`[[edi]]`) or a non-actor class (`0x42fa31`, whose function
   `0x42fa18` is referenced only from the table dword at `0x49b01a`). No
   direct call of `0x405038`, `0x418ba8` or `0x421c8c` exists. Only EAX of
   the hit test is live after the return.
2. **Three different bodies.** All three gate on a distance test of the
   segment start against the actor, transform the segment into actor space
   and run the COL traversal. `BASE` adds an object-query fallback
   (`object_query`) for destroyed buildings and buildings without COL;
   `MECH` runs the shield model (`shield_geometry`) first and only reaches
   COL with the damage the shield did not absorb, then hands the part hit
   to `0x4188c8`; `FLYER` has a view-subject exemption. Results: `BASE` and
   `FLYER` return the COL/query hit distance **+ 1**, `MECH` returns the
   COL hit word **without +1** or the shield penetration depth.
3. **RNG stream A per hit.** Every effect choice draws `rng_next`
   (`0x4d2599`): `BASE` 1 (explosion type, only when the pool allocation
   succeeded) + 1 (debris chance, 1025/4096); `MECH` 1 on a full shield
   absorption (explosion type, allocation succeeded); `0x4188c8` 1 for the
   explosion type (allocation succeeded) plus, for a weapon-mount part
   whose condition bracket changed, 1 for the weapon-destruction chance;
   `FLYER` 1 (explosion type, always drawn after a COL hit). The debris
   burst draws more (per piece: the weighted pick, `rng_bounded(30000)` in
   the constructor, the spin draws and `debris_heading`'s own). The order
   is the call order below.
4. **Calls through the bridge.** Every replacement calls every game
   function through the weapons guest bridge with the original's
   registers, so nested hooks keep their modes. Exceptions: `0x4188c8`
   computes `0x410670` and `0x426ac8` in core, and `debris_update`
   computes `0x4089bc` in core. `0x49630c` (RTL), memset `0x49681a`, the
   node lookups `0x47622c`/`0x476850`, `0x434010` and the empty ACTOR
   default slots run as original code.
5. **No entry shim** except `flyer_part`'s, which zeroes ECX. The bodies
   read no local before writing it and no entry register except to hand it
   on. The residues that stay are the composed-matrix byte 19, frame
   addresses stored in globals (`[0x4a980c]`, `[0x49a170]`), argument slots
   that keep the pushed value and the event-record bytes of the notice
   functions (section "Stack, frames and residue").
6. **Compare mode.** Refused by every hook here except `debris_table` and
   `region_entry` (game calls, frame addresses in registers and globals,
   undeclared stores).

## Dispatch site and packet

**The caller (`0x42669e`..`0x426807`).** For each actor of the actor list
that is not the owner `P+0xe`, not the excluded `P+0x14` and whose
definition dword `[[actor+0x45]+0x14]` is 0: `push edi (P); push ebx
(actor); mov eax,[ebx]; call [eax+0x20]`, then `add esp,8; mov
[f−0x30],eax; test eax,eax`. At `0x4266c9`: stack (actor, P), EAX = the
actor's vtable, EDX = `[actor+0x45]`, ECX = 0 (`setne cl; and ecx,1` fell
through). A zero result skips the actor (`0x4266d4`); otherwise the sweep
later stores the result as the new segment length (`0x426807`..`0x42680d`,
`[S+4]` ← EAX) and stops below 500.

**Result liveness.** After `0x4266cc`: EDX is rewritten at `0x4266e2`
(owner path), `0x426807` (no owner) or `0x426836` (next actor); when the
loop ends at `0x426841` EDX reaches `0x427da0` (`mov eax,[0x4aaad0]; ret`,
reads no register) and is rewritten at `0x426870` or `0x426a0c` before any
read. ECX is rewritten at `0x4266e8`/`0x42680a` or at the next actor's
`0x4266b6`; on the exits it can become the sweep's returned ECX, which all
nine sweep callers pop ([weapons.md](weapons.md)). **Only EAX of the hit
test is observable.** The hooks return EDX:EAX exactly anyway; a replaced
hook returns its entry ECX and flags.

**Packet P and segment S (as the hit tests read them).** P+0 = S; P+4 word
(the damage handed to slot `+0x74` by BASE, FLYER and `0x4188c8`), P+6
word (the damage the MECH shield model sees; BASE only tests it for zero),
P+8 word (`0x4188c8`: splash factor), P+0xa dword → an effect table of
words (BASE and FLYER use words 8..11, `0x418ba8` words 0..3, `0x4188c8`
words 4..11), P+0xe owner, P+0x12 word (weapon kind; MECH only). S+0 → the
forward pose (32 bytes, translation = segment start at +0x14), S+4 dword
length, S+8 word radius, S+0xa the inverse pose (written by the sweep). See
[weapons.md](weapons.md#swept-query-426528).

**Runtime record.** `0x49630c` reads only EAX (the descriptor) and EBP; it
returns EAX = the old FS:0 head and leaves EDX and ECX unchanged. It writes
record+0 (the head), +4 (handler `0x4967f0`), +8 (the descriptor), +0xc
(its caller's ESP), +0x10..+0x15 (zero) and +0x1c (`[[0x6cbee4]]`). The
descriptors `0x4974e6`, `0x49a138`, `0x49a4cc`, `0x49a108`, `0x497d70` hold
−0x24 at +4 (bytes `dc ff ff ff`): the record lies at f−0x24, the context
word at f−0x14; `0x4989e8` (`weapon_eject`) also holds −0x24. `0x4988e8` (`0x40d434`) holds −0x34. The replacements keep
their frame in adapter scratch (f = scratch + 208 in a 256-byte scratch
unless stated otherwise); `0x49630c` gets that synthetic EBP through
`weapons_impact_guest_call`, and the adapters restore FS:0 to their entry
head (= the record's saved head) as the epilogues do. Record+0xc is then
the bridge's ESP instead of the function's; only an exception unwind would
read it.

## Hit tests (group `hit_test`)

Hooks `base_hit_test` `0x405038`, `mech_hit_test` `0x418ba8`,
`flyer_hit_test` `0x421c8c`; core `core/src/hit_test.c`, adapters
`proxy/hit_test_hooks.h`, in vitro `invitro/hit_test_cases.h`.

### `0x405038` `base_hit_test` (`BASE`, `RADAR_BASE`, `GUN_BASE`, `VEHICLE`, `LC_BASE`)

212 instructions; 1000 ×6 (count mode 25,649 / 460,234 / 38,965;
receivers `0x497940`, `0x4979d4`, `0x4978ac`, `0x497818`). Prologue
`55 8b ec 83 c4 80 b8 e6 74 49 00` (relocate 6: 1,2,3). Frame 0x80, saves
EBX/ESI/EDI; EBX = B, ESI = S. Names: C = `[B+0x1f2]` (configuration), P0 =
`[B+0x205]` (first runtime part record, stride 0xb).

1. `0x49630c` with EAX = descriptor `0x4974e6`, EDX/ECX = entry values (not
   read). Record at f−0x24, context word f−0x14.
2. ESI = `[P]`; result f−0x28 ← 0; DI = word P+6 (EDI's high word stays
   the caller's; only DI is read); word f−0x2a ← word P+4; part f−0x30 ←
   −1.
3. **Path choice.** q = 1 when word `C+4` ≠ −1 (`0x40507c`), `s16(P0+5)` <
   2 (`0x405089`) and byte `[B+0x99]` ≠ 0 (`0x405095`; EAX = B+0x92), else
   0. q ≠ 0 (`0x4050a3`) or dword `[C+0x38]` = 0 (`0x4050a9`) → object
   query (4a); otherwise COL (4b).
4. a. **Object query** (`0x4050ab`): f−0x34 ← B (a one-element list);
   `object_query(f−0x3c, S, f−0x34, 1)` (EAX = q, EDX = f−0x34, ECX =
   f−0x3c). AX = 0 (`0x4050c4`) → 5. Hit: result ← `[f−0x3c]` + 1 (the
   shortened length; `object_query` also wrote `[S+4]`); if word f−0x2a = 0
   (`0x4050d6`) and DI = 0 (`0x4050df`): word f−0x2a ← word `[0x4a95f2]`
   (set by `0x40501c`, a default damage). Part stays −1. `object_query`
   writes `[out]` (f−0x3c) and `[out+4]` (f−0x38, the building's B+0xc) on
   every hit (AX = 1), so the read of f−0x3c never reads residue
   ([weapons.md](weapons.md#sweep-receiver-object-query-427da8-grid-sweep-4273c8-set_length-4927f0)).
   b. **COL** (`0x4050f5`): r = slot `+0x10`(B) (`shape_word` `0x46b910`
   for every actor class: word +8 of `[[B+0x34]+4]`, 0 without shape;
   entry EAX = 0, EDX = C, ECX = the vtable); reach = `s16(r) + s16(S+8) +
   [S+4]` (read in that order); d = `dist_gate([S]+0x14, B+0x26)` (EAX =
   `s16(S+8)+[S+4]`, EDX = B+0x26, ECX = `[S]+0x14`). reach < d signed
   (`0x40511c`) → return EAX = 0, EDX = the record's saved head, FS:0 ← it
   (ECX = `dist_gate`'s). Otherwise f−0x40 ← S+0xa; word `[B+0x32]` = 0
   (`0x40513d`): `euler_matrix(B+0xc, B+0x12)` (EAX = B+0x12, EDX = B+0xc
   via f−0x44, ECX = S+0xa), word `[B+0x32]` ← 1. `matrix_compose(B+0x12,
   S+0xa, f−0x68)` (EAX = B+0x12, EDX = f−0x68, ECX = S+0xa). h =
   `col_query([C'+0x38], B, S, f−0x68, [B+0x201])` with `[B+0x201]` read
   first, then C' = `[B+0x1f2]` again (EAX = `[C'+0x38]`, EDX = f−0x68,
   ECX = C'). h ≠ 0 (`0x40518d`): result ← `[h]` + 1 (EDX), part f−0x30 ←
   `s16(h+4)` (ECX). BASE reads `[h]` and `[h+4]` right after `col_query`.
5. Result 0 (`0x4051a0`) → return EAX = 0, ECX = the saved head, FS:0 ←
   ECX. EDX on this exit (`0x40519c` → `0x40529f`): the object query's EDX
   (AX = 0, or a length of −1), `lo(that EDX, [0x4a95f2])` when the default
   damage word was taken before a length of −1, `col_query`'s EDX on a COL
   miss, or 0 (`[h] + 1`) for a hit word of −1.
6. **Hit effects.** The point (0, result, 0) at f−0x80/−0x7c/−0x78 (written
   0, result, 0); `transform_point(f−0x80, [S], f−0x74)` (EAX = `[S]`, EDX =
   f−0x74, ECX = f−0x80). Byte `[B+0x99]` = 0 (`0x4051d3`, EAX = B+0x92):
   slot `+0x74`(B, part, damage, `[P+0xe]`) with the dwords EDX = `lo(P,
   f−0x30)` and EAX = `lo(B+0x92, f−0x2a)` pushed as the part and damage
   slots (high words P's and (B+0x92)'s), ECX = the vtable;
   `building_part` (`0x404d70`) for every building class (it writes 0 into
   the part slot's low word when the part is −1, as documented in
   [player_movement.md](player_movement.md#building-part-damage-404d70)).
7. `pool_alloc([0x4a961e])` (EAX = the pool; `pop ecx` = the pool); slot
   f−4. Non-null (`0x405200`): context word ← 0x14; pushes 1, B, f−0x74;
   k = `rng_next(0x4d2599)` & 3 (EAX = obj, EDX = f−0x74, ECX = the pool);
   type = word `[[P+0xa] + 0x10 + 2k]`; `explosion_create(obj, type,
   f−0x74, B, 1)` (EAX = obj, EDX = `[P+0xa]`, ECX = type, high word 0);
   counter −= 4 (EDX = its pointer); context ← 8.
8. Debris chance (always on a hit): `rng_next(0x4d2599)`; `s16(AX) & 0xfff`
   < 0x401 (`0x40526b`): w = 1 at f−0x46; R = `debris_table(&w)` (EAX = 1
   from `setl al; and eax,1`, EDX = f−0x46, ECX = `s16(draw) & 0xfff`);
   `debris_burst([R] + 10·s16(w), [R+6], f−0x74, 0)` reading `[R+6]`, w,
   `[R]` in that order (EAX = the first argument, EDX = 10·s16(w), ECX =
   s16(w)).
9. Return EAX = the result, ECX = the saved head, FS:0 ← ECX; EDX = the
   last callee's (`debris_burst`'s, `rng_next`'s or `pool_alloc`'s).

Branch sites (15): `0x40507c`, `0x405089`, `0x405095`, `0x4050a3`,
`0x4050a9`, `0x4050c4`, `0x4050d6`, `0x4050df`, `0x40511c`, `0x40513d`,
`0x40518d`, `0x4051a0`, `0x4051d3`, `0x405200`, `0x40526b`.

### `0x418ba8` `mech_hit_test`

216 instructions; 1000/1000/1000/1000/1000/652 (count mode 1,569 / 34,631
/ 2,316, receiver `0x49a282`). Prologue `55 8b ec 81 c4 78 ff ff ff`
(relocate 9: 1,2,6). Frame 0x88, saves EBX/ESI/EDI; EDI = P, EBX = A, ESI
= S. C = `[A+0x1f2]`.

1. `0x49630c` (EAX = `0x49a138`; EDX/ECX entry).
2. d = `dist_gate([S]+0x14, A+0x26)` (EAX = A+0x26, EDX = `[S]+0x14`, ECX =
   the entry ECX, which `dist_gate` does not read). reach = `s16(S+8) +
   s16(C+0x1a) + [S+4]` (read order `[A+0x1f2]`, S+8, C+0x1a, S+4). d >
   reach signed (`0x418bed`) → return EAX = 0, ECX = the saved head, FS:0
   ← ECX, EDX = `s16(C+0x1a)`.
3. Word f−0x26 ← word P+6 (damage). AI reaction, only when byte `[A+0xa3]`
   (the player flag, read as `A+0x92+0x11`) = 0 (`0x418c12`): k = word
   P+0x12; k = 0 (`0x418c1b`) or 1 (`0x418c21`): word `[A+0x26b]` = 0
   (`0x418c2b`) → byte `[A+0x96]` ← 1; else k (reread at `0x418c39`
   without a call in between; first read `0x418c14`) = 2 (`0x418c3e`):
   byte `[A+0x96]` ← 0, word `[A+0x26b]` ← 6000. (*Hypothesis:* an "under
   fire" flag with a 6000-tick hold for weapon kind 2.)
4. Word f−0x32 ← f−0x26 (the damage that the shield may reduce); f−0x38 ←
   S+0xa; f−0x3c ← A+0xc; f−0x44 ← A+0x222 (written, never read). Word
   `[A+0x32]` = 0 (`0x418c6f`): `euler_matrix(A+0xc, A+0x12)` (EAX =
   A+0x12, EDX = A+0xc, ECX = A+0xc), word `[A+0x32]` ← 1.
   `matrix_compose(A+0x12, S+0xa, f−0x64)` (EAX = A+0x12, EDX = f−0x64,
   ECX = S+0xa).
5. depth = `shield_geometry(A+0x222, A, f−0x64, [S+4], f−0x32)` (EAX =
   A+0x222, EDX = `[S+4]`, ECX = f−0x64; contract in
   [weapons.md](weapons.md#vehicle-shield-geometry-413cc4)); f−0x30 ←
   depth. 0 (`0x418ccd`) → return EAX = 0, EDX = the saved head, FS:0 ←
   EDX (ECX = `shield_geometry`'s).
6. Byte `[A+0x94]` ← 0. Dword `[A+0x288]` += `s16(f−0x26) − s16(f−0x32)`
   (the absorbed amount; EDX = it, ECX = `s16(f−0x32)`).
7. **Fully absorbed** (word f−0x32 = 0, `0x418cf2`): the point (0, depth, 0)
   at f−0x7c; `transform_point(f−0x7c, [S], f−0x70)`; `pool_alloc([0x4a961e])`;
   non-null (`0x418d2c`): context 0x14, k = `rng_next` & 3, type = word
   `[[P+0xa] + 2k]` (words 0..3), `explosion_create(obj, type, f−0x70, A,
   1)` (EAX = `[P+0xa]`, EDX = `lo(rng_next's EDX, type)`: the high word of
   `rng_next`'s EDX is pushed, ECX = obj), counter −= 4, context 8. → 10
   with result = depth.
8. **Penetrating**: player flag set (`0x418d82`), byte `[A+0xb0]` = 0
   (`0x418d8e`) and `s16([A+0x224]) + s16([A+0x222])` < 500 (`0x418db7`;
   `[A+0x224]` read first): `cockpit_notice(0x15)` (`0x420a68`, shield
   warning), byte `[A+0xb0]` ← 1.
9. h = `col_query([A+0x1f6], A, S, f−0x64, [A+0x20e])` (`[A+0x20e]` read
   first; EAX = `[A+0x1f6]`, EDX = `[A+0x20e]`, ECX = f−0x64); EDX ← 0
   (`0x418de4`); f−0x2c ← h; f−0x30 ← 0. h = 0 (`0x418df3`) → return EAX =
   0, EDX = the saved head. Hit: f−0x30 ← `[h]` (no +1); dword `[A+0x288]`
   += `s16(P+4)`; the point (0, `[h]`, 0) at f−0x88;
   `transform_point(f−0x88, [S], f−0x70)`; then `[h+4]` is read;
   `mech_part_hit(A, s16(h+4), P, f−0x70)` (EAX = f−0x70, EDX = h, ECX =
   `s16(h+4)`).
10. Return EAX = f−0x30, EDX = the saved head, FS:0 ← EDX; ECX = the last
   callee's.

Branch sites (14): `0x418bed`, `0x418c12`, `0x418c1b`, `0x418c21`,
`0x418c2b`, `0x418c3e`, `0x418c6f`, `0x418ccd`, `0x418cf2`, `0x418d2c`,
`0x418d82`, `0x418d8e`, `0x418db7`, `0x418df3`. Map: `shield_geometry`
22/753/151/1000/300/112 (calls past step 2), `col_query` (all three
callers) 6/125/21/1000/205/64.

### `0x421c8c` `flyer_hit_test`

173 instructions; 922/1000/1000/0/0/0. Prologue `55 8b ec 83 c4 8c b8 cc
a4 49 00` (relocate 6: 1,2,3). Frame 0x74; EBX = F, ESI = S.

1. `0x49630c` (EAX = `0x49a4cc`).
2. Dword `[0x49ef34]` ≠ 0 (`0x421cae`) and F = `[0x4d2684]` (`0x421cb6`;
   the view subject of [hud.md](hud.md)) → return EAX = 0, EDX = the saved
   head (ECX = the entry ECX).
3. r = slot `+0x10`(F) (`shape_word`; EAX = the old head returned by
   `0x49630c`, EDX = P, ECX = the vtable); reach and `dist_gate` exactly as
   BASE step 4b; miss (`0x421cf0`) → return 0.
4. Word f−0x26 ← word P+4; f−0x2c (result) ← 0; f−0x30 ← S+0xa; lazy
   matrix (`0x421d21`) as BASE; `matrix_compose(F+0x12, S+0xa, f−0x5c)`.
   h = `col_query([[F+0x1fa]+0x32], F, S, f−0x5c, [F+0x208])` (read order
   `[F+0x208]`, `[F+0x1fa]`, `+0x32`; EAX = the COL, EDX = f−0x5c, ECX =
   `[F+0x1fa]`). h = 0 (`0x421d73`) → return 0.
5. k = `rng_next` & 3 (EAX = h, EDX/ECX `col_query`'s); `[h]` and `[h+4]`
   are read after it; word f−0x36 ← word `[[P+0xa] + 0x10 + 2k]` (ECX =
   `[P+0xa]` at `0x421d8e`); word f−0x38 ← 1; result ← `[h]` + 1. ECX is
   reloaded with P (`mov ecx,[ebp+0Ch]`, `0x421da6`) before `mov
   cx,[edi+4]`: slot `+0x74`(F, `lo(P, [h+4])`, `lo([h]+1, f−0x26)`,
   `[P+0xe]`) at `0x421dba` with EAX = the vtable, EDX = `lo([h]+1,
   f−0x26)`, ECX = `lo(P, [h+4])`: `flyer_part` (`0x421bb4`, section
   "FLYER part damage"). Byte `[F+0x99]` ≠ 0 after it (`0x421dca`): f−0x36
   ← 10, f−0x38 ← 3.
6. The point (0, result, 0) at f−0x74; `transform_point(f−0x74, [S],
   f−0x68)`; `pool_alloc`; non-null (`0x421e0c`): `explosion_create(obj,
   lo(pool, f−0x36), f−0x68, 0, 1)` — **owner 0**, unlike BASE/MECH;
   counter −= 4. Then always: w = f−0x38 at f−0x3a, R =
   `debris_table(&w)` (EAX = f−0x3a, EDX = the counter pointer after an
   explosion or `pool_alloc`'s EDX after a failed allocation, ECX =
   `lo(explosion_create's ECX or the pool, w)`), `debris_burst([R] +
   10·s16(w), [R+6], f−0x68, 0)` (EDX = s16(w), ECX = 10·s16(w)).
7. Return EAX = the result, EDX = the saved head, FS:0 ← EDX.

Branch sites (7): `0x421cae`, `0x421cb6`, `0x421cf0`, `0x421d21`,
`0x421d73`, `0x421dca`, `0x421e0c`.

### Hit-test registers at the game calls

f = the original's EBP; "+0xc" etc. relative to the actor.

| Function | Call | EAX | EDX | ECX |
|---|---|---|---|---|
| all | `0x49630c` | descriptor | entry EDX | entry ECX |
| BASE | `object_query`(f−0x3c, S, f−0x34, 1) | q (0/1) | f−0x34 | f−0x3c |
| BASE / FLYER | slot `+0x10`(actor) | 0 / the old head | C / P | vtable |
| BASE, FLYER | `dist_gate`([S]+0x14, +0x26) | s16(S+8)+[S+4] | +0x26 | [S]+0x14 |
| MECH | `dist_gate`([S]+0x14, A+0x26) | A+0x26 | [S]+0x14 | entry ECX |
| BASE, FLYER | `euler_matrix`(+0xc, +0x12) | +0x12 | +0xc | S+0xa |
| MECH | `euler_matrix`(A+0xc, A+0x12) | A+0x12 | A+0xc | A+0xc |
| all | `matrix_compose`(+0x12, S+0xa, M) | +0x12 | M | S+0xa |
| BASE | `col_query`([C'+0x38], B, S, f−0x68, [B+0x201]) | [C'+0x38] | f−0x68 | C' (reread) |
| MECH | `shield_geometry`(A+0x222, A, f−0x64, [S+4], f−0x32) | A+0x222 | [S+4] | f−0x64 |
| MECH | `cockpit_notice`(0x15) | lo(A+0x222, [A+0x222]) | s16([A+0x222]) | the sum |
| MECH | `col_query`([A+0x1f6], A, S, f−0x64, [A+0x20e]) | [A+0x1f6] | [A+0x20e] | f−0x64 |
| FLYER | `col_query`([[F+0x1fa]+0x32], F, S, f−0x5c, [F+0x208]) | the COL | f−0x5c | [F+0x1fa] |
| BASE | `transform_point`(f−0x80, [S], f−0x74) | [S] | f−0x74 | f−0x80 |
| MECH | `transform_point`(f−0x7c or f−0x88, [S], f−0x70) | [S] | f−0x70 | f−0x7c / f−0x88 |
| FLYER | `transform_point`(f−0x74, [S], f−0x68) | [S] | f−0x68 | f−0x74 |
| BASE | slot `+0x74`(B, lo(P, part), lo(B+0x92, dmg), [P+0xe]) | lo(B+0x92, dmg) | lo(P, part) | vtable |
| FLYER | slot `+0x74`(F, lo(P, [h+4]), lo([h]+1, dmg), [P+0xe]) | vtable | lo([h]+1, dmg) | lo(P, [h+4]) |
| all | `pool_alloc`([0x4a961e]) | the pool | the last callee's | the last callee's |
| BASE | `rng_next` (type) | obj | f−0x74 | the pool |
| BASE | `rng_next` (debris) | explosion_create's / 0 | counter pointer / pool_alloc's | explosion_create's / the pool |
| MECH | `rng_next` | obj | f−0x70 | the pool |
| FLYER | `rng_next` | h | col_query's | col_query's |
| BASE | `explosion_create`(obj, type, f−0x74, B, 1) | obj | [P+0xa] | type (high word 0) |
| MECH | `explosion_create`(obj, lo(rng EDX, type), f−0x70, A, 1) | [P+0xa] | lo(rng EDX, type) | obj |
| FLYER | `explosion_create`(obj, lo(pool, type), f−0x68, 0, 1) | obj | f−0x68 | lo(pool, type) |
| BASE | `debris_table`(f−0x46) | 1 | f−0x46 | s16(draw) & 0xfff |
| FLYER | `debris_table`(f−0x3a) | f−0x3a | counter pointer / pool_alloc's | lo(explosion_create's ECX or the pool, w) |
| BASE | `debris_burst`([R]+10·s16(w), [R+6], f−0x74, 0) | the first argument | 10·s16(w) | s16(w) |
| FLYER | `debris_burst`([R]+10·s16(w), [R+6], f−0x68, 0) | the first argument | s16(w) | 10·s16(w) |
| MECH | `mech_part_hit`(A, s16([h+4]), P, f−0x70) | f−0x70 | h | s16([h+4]) |

The COL hit record h is the static `0x4a981e` (`col_query`).

### Hit-test results, residue and validation

**Results.** EDX:EAX exactly. EDX is the saved head (f−0x24) on every MECH
and FLYER exit except MECH's distance miss (EDX = `s16(C+0x1a)`); BASE as
in its steps 5 and 9. ECX and the flags are dead at the sweep (section
"Dispatch site and packet").

**Callees.** The hit tests call `col_query`, `shape_word` (slot `+0x10`,
target read from the actor's vtable), `debris_table` and `debris_burst`,
`cockpit_notice`, `mech_part_hit` and slot `+0x74` through the bridge, so
those hooks keep their modes and `hk_debris_table_calls` includes the hit
tests' picks. The list fallback of `debris_table` (the second list
`[0x4a964c]` when `s16([0x4a9654])` ≤ s16(w); w = 1 for BASE and live
FLYERs, 3 for dead ones) happens inside it. `0x49630c` runs as original
code.

**Stack and residue.** No local of the three bodies is read before it is
written on any path (the context word and the saved head are the record's;
every point, matrix and debris word is written before its address is
handed on). No entry register is read except to hand it on (EDX/ECX to
`0x49630c`, MECH's ECX to `dist_gate`, FLYER's ECX returned on the
view-subject exit). No entry shim. Residues that remain, none observable in
the simulation state:

- Byte 19 of the composed matrices f−0x68, f−0x64, f−0x5c (section "Stack,
  frames and residue").
- Record+0xc is the bridge's ESP.
- `cockpit_notice` (MECH's shield warning, and FLYER's notice through
  `flyer_part`) posts an event record with unwritten bytes (section
  "Stack, frames and residue").
- After the return, the stack below the sweep holds the replacement's
  frames instead of the original's. With `sweep` replaced (every live
  all-hooks run) the original hit tests already ran from the bridge; a
  later callee of the sweep that read residue there would be a live risk
  (none known).

**Compare mode** refused (game calls; frame addresses in EDX/ECX).

**In vitro** (`differential_hit_test_test`): original against core with
logging stubs and real callees; a mutation that keeps the FLYER part slot
as `lo([P+0xa], h+4)` is caught
([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)). Branch
sites: 36, all both ways (BASE 15, MECH 14, FLYER 7; none proved).

**Live expectations** (map entry probes; not measured with the hooks):
`base_hit_test` 1000 ×6, `mech_hit_test` 1000/1000/1000/1000/1000/652,
`flyer_hit_test` 922/1000/1000/0/0/0. Relations with every hook replaced:

- `hk_sweep_slot_20` = `hk_base_hit_test_calls` + `hk_mech_hit_test_calls`
  + `hk_flyer_hit_test_calls` (the sweep is the only dispatch site).
- `hk_col_query_calls` = `hk_base_hit_test_call_40c9d4` +
  `hk_mech_hit_test_call_40c9d4` + `hk_flyer_hit_test_call_40c9d4`.
- `hk_shield_geometry_calls` = `hk_mech_hit_test_call_413cc4` (its only
  caller); `hk_object_query_calls` = `hk_base_hit_test_call_427da8` + the
  calls of `pool_query` `0x404bc0`.
- `call_49630c` = calls for each hook. BASE: `call_427da8` + `slot_10` =
  calls, `call_49288c` = `slot_10`, `call_47fa20` = `call_40c9d4` ≤
  `call_49288c`, `call_47ebb8` ≤ `call_47fa20`; `call_48043c` =
  `call_471b30` = the hits, `call_492ee0` = `call_48043c` + `call_407f1c`,
  `call_4089bc` = `call_408530` ≤ `call_48043c`, `slot_74` ≤
  `call_48043c`.
- MECH: `call_49288c` = calls; `call_47fa20` = `call_413cc4`;
  `call_47ebb8` ≤ `call_47fa20`; `call_4188c8` = the COL hits ≤
  `call_40c9d4`; `call_48043c` = `call_4188c8` + `call_471b30` (full
  absorptions); `call_492ee0` = `call_407f1c` ≤ `call_471b30`;
  `call_420a68` ≤ `call_40c9d4`. `hk_mech_part_hit_calls` =
  `hk_mech_hit_test_call_4188c8` + the calls of `0x4198f4`.
- FLYER: `slot_10` = `call_49288c` ≤ calls; `call_47fa20` =
  `call_40c9d4`; `call_492ee0` = `slot_74` = `call_48043c` =
  `call_471b30` = `call_4089bc` = `call_408530` = the COL hits;
  `call_407f1c` ≤ `call_471b30`.

## MECH part hit `0x4188c8` `mech_part_hit`

`0x4188c8(A, part, P, point)`; hook `mech_part_hit` in its own adapter
group (core `core/src/mech_part_hit.c`, adapter
`proxy/mech_part_hit_hooks.h`, in vitro `invitro/mech_part_hit_cases.h`).
236 instructions; 5/31/3/455/6/15 (count mode 5 / 31 / 3; the c4_08 calls
come from `0x4198f4`, the others from the MECH hit test). Callers:
`0x418e41` (MECH hit test) and six sites in `0x4198f4` (`0x419d05`,
`0x419e17`, `0x419eb6`, `0x419f55`, `0x41a07e`, `0x41a243`; parts 8, 7, 4,
5, 0, 6). Prologue `55 8b ec 83 c4 b4 53 56 57` (relocate 6: 1,2,3). Frame
0x4c; ESI = part (dword), EBX = A.

1. `0x49630c` (EAX = `0x49a108`).
2. Word f−0x26 ← word P+4 (CX = word P+4 is loaded over the ECX that
   `0x49630c` returns, so ECX's high word is the runtime call's); EDI =
   `s16(P+8)`; f−0x2c ← `s16(P+4)`. splash = `multiply_q10(s16(P+8),
   s16(P+4))` → EDI, with EAX = `s16(P+4)` (`movsx eax, word [ebp−0x26]`
   at `0x4188f3` follows the `mov eax,[ebp+0x10]`) and EDX = `s16(P+4)`;
   `multiply_q10` returns the low dword of the product shifted by 10 and
   does not read ECX. P+4 and P+8 are read once, before any call but
   `0x49630c`.
3. before = `part_ratio(A+0x206, part)` (ECX = A+0x206) → word f−0x32;
   word f−0x28 ← `s16(before) >> 5`.
4. Slot `+0x74`(A, part, `lo(EAX, f−0x26 − DI)`, `[P+0xe]`) (EDX = the
   vtable, ECX = `[P+0xe]`; EAX's high word is the sign of `before >> 5`):
   `vehicle_part`. DI ≠ 0 (`0x418944`): slot `+0x70`(A, EDI, point, 500,
   `[P+0xe]`) (EAX = `[P+0xe]` reread, EDX = point, ECX = the vtable):
   `vehicle_hit`. Only DI is tested; the full EDI is passed.
5. after = `part_ratio(A+0x206, part)` (EAX = A+0x206) → EDI; f−0x38 ←
   (DI = 0x100) (EDX = 0/1).
6. `s16(after) >> 5` = word f−0x28 as 16-bit words (`0x41898a`): words
   f−0x2e, f−0x30 ← 1 → 7. Otherwise both ← 2 and:
   - player flag (`0x4189b3`), `s16(after)` < 100 (`0x4189b9`) and part
     (dword) 0 (`0x4189bd`) or 1 (`0x4189c2`): `0x434010(0)` (panel jolt:
     presentation state, clock, stream B; [display_modes.md](display_modes.md),
     original code as under `ground_collide`); then `s16(before)` > 100
     (`0x4189d1`): `cockpit_notice(0x12)`.
   - f−0x38 = 0 (`0x4189df`), word `[C+0x56]` ≠ 0 (`0x4189f0`, C =
     `[A+0x1f2]`) and part ≥ 19 signed (`0x4189f9`) — a weapon mount part:
     n = 10, or 3 when byte `[[A+0x45]+0x12]` = 0 (`0x418a0c`, the side
     byte; n latched in f−0x3a/f−0x3c, the side byte read just before the
     draw); `rng_next(0x4d2599)`; `s16(AX) & 0xfff` < 41·n strictly signed
     (`setl` at `0x418a3c`, branch `0x418a45`): W = `0x410670([A+0x202],
     lo(part, part − 19))`; `weapon_eject(W, A, 1)` (`0x40f57c`: a debris
     object from pool `[0x4a9642]` with `debris_heading`); word
     `[[A+0x20e] + 2·s16(part)]` ← 0 (the COL region flag of that part);
     when `s8([[A+0x45]+0x12])` = 1 (`0x418a8f`): v = `multiply_q10(500,
     s16(after))`, the item dword (value word f−0x46 << 16) | word
     `[[W+0x1c]+0x56]` at f−0x48 with value = `(s16(0x100 − v) · 100) >>
     8` (arithmetic) goes to `0x426ac8(&item)` (the results.dat item list
     of [mission.md](../formats/mission.md), salvage *hypothesis*);
     finally `region_hit(A+0x206, part, 10000)` (`0x40da38`).
7. (`0x418af1`) `pool_alloc([0x4a961e])`; non-null (`0x418b02`): context
   0x14; SI = word f−0x2e (1 or 2; ESI's high word stays the part's);
   k = `rng_next` & 3; type = word `[[P+0xa] + 2·(4·s16(SI) + k)]` (words
   4..7 unchanged bracket, 8..11 changed); `explosion_create(obj, type,
   point, A, 1)` (EAX = `[P+0xa]`, EDX = `lo(P, type)`, ECX = obj);
   counter −= 4; context 8.
8. f−0x38 = 0 (`0x418b58`) and word f−0x30 ≠ 1 (`0x418b5f`): EBX = point;
   w = f−0x30 (2) at f−0x4a; R = `debris_table(&w)`; `debris_burst([R] +
   10·s16(w), [R+6], point, 0)` (EAX = the first argument, EDX = s16(w),
   ECX = 10·s16(w)).
9. Return EAX = the saved head, FS:0 ← EAX; EDX/ECX = the last callee's:
   `debris_burst`'s after debris; else `explosion_create`'s EDX and ECX,
   or `pool_alloc`'s EDX with ECX = the pool.

**The two leaves computed in core.** The hooks `weapon_by_mount` and
`list_append` exist, but `mech_part_hit` computes these two calls itself,
so those hooks do not see them.

- `0x410670(system, word)` (`es2_weapon_by_kind`): ESI = 0, EDX = 0, EAX =
  `[system+4]`; while `s16(DX) < s16([system+8])` (word reread per test,
  signed): ECX = `[EAX]`; a non-null entry whose definition byte
  `[[ECX+0x18]+0x17]`, sign-extended, equals the argument word as 16-bit
  words ends the scan with ESI = ECX; else EDX += 1, EAX += 4. Returns EAX
  = ESI (0 when nothing matches), EDX = the stop index, ECX = the last
  entry read (the caller's ECX = n when the count is ≤ 0).
- `0x426ac8(&item)` (`es2_results_append`): EAX = `s16([0x4a9e6a])`, EDX =
  `[0x4a9e6c]`, ECX = the dword at item; `[EDX + 4·EAX]` ← ECX (no bound
  check, a negative index writes below the list); the word `[0x4a9e6a]` is
  incremented.
- `weapon_eject` dereferences W first (`[W+0x49]`, then `[W+0x18]`), so W
  = 0 on the weapon path faults in the original, independently of the side
  byte; the core keeps that precondition.

### Part-hit registers at the game calls

| Site | Callee | EAX | EDX | ECX | Slots |
|---|---|---|---|---|---|
| `0x4188dc` | `0x49630c` | `0x49a108` | entry EDX | entry ECX | — (EBP = f) |
| `0x4188ff` | `multiply_q10` | `s16(P+4)` | `s16(P+4)` | lo(`0x49630c`'s ECX, P+4) | `s16(P+8)`, `s16(P+4)` |
| `0x418911` | `part_ratio` | splash | `multiply_q10`'s EDX | A+0x206 | A+0x206, part |
| `0x41893b` | slot `+0x74` | lo(`sar(s16(before), 5)`, P+4 − DI) | `[A]` | `[P+0xe]` | A, part, that EAX, `[P+0xe]` |
| `0x41895a` | slot `+0x70` (DI ≠ 0) | `[P+0xe]` (reread) | point | `[A]` | A, splash, point, 500, `[P+0xe]` |
| `0x418968` | `part_ratio` | A+0x206 | as left | as left | A+0x206, part |
| `0x4189c6` | `0x434010` | A+0x92 | 0 or 1 (after = 0x100) | `part_ratio`'s | 0 |
| `0x4189d5` | `cockpit_notice` | `0x434010`'s | `0x434010`'s | 0 (`pop ecx`) | 0x12 |
| `0x418a21` | `rng_next` | lo(A+0x92 or a notice call's EAX, n) | C = `[A+0x1f2]` | `[A+0x45]` | `0x4d2599` |
| (core) | `0x410670` | lo(part, part − 19) | `[A+0x202]` | n | `[A+0x202]`, that EAX |
| `0x418a6b` | `weapon_eject` | W | the stop index | W | W, A, 1 |
| `0x418aa0` | `multiply_q10` | `s16(after)` | `s8` side byte (1) | `s16(after)` | 500, `s16(after)` |
| (core) | `0x426ac8` | the value | f−0x48 | lo(W, `[[W+0x1c]+0x56]`) | f−0x48 |
| `0x418ae9` | `region_hit` | the old index, or `[A+0x45]` (side ≠ 1) | the list, or `s8(side)` | A+0x206 | A+0x206, part, 10000 |
| `0x418af7` | `pool_alloc` | `[0x4a961e]` | as left | as left | `[0x4a961e]` |
| `0x418b1a` | `rng_next` | the object | point | the pool (`pop ecx`) | `0x4d2599` |
| `0x418b3e` | `explosion_create` | `[P+0xa]` | lo(P, type) | the object (f−4) | object, lo(P, type), point, A, 1 |
| `0x418b70` | `debris_table` | lo(counter pointer or 0, w) | f−0x4a | `explosion_create`'s ECX, or the pool | f−0x4a |
| `0x418b8d` | `debris_burst` | `[R] + 10·s16(w)` | `s16(w)` | `10·s16(w)` | that, `[R+6]`, point, 0 |

"As left" at `pool_alloc`: after the bracket test with no change, EDX =
the 0/1 flag and ECX = `part_ratio`'s; after the notice block the notice
call's EDX and 0 or 0x12; on the gates `0x4189df`/`0x4189f0`/`0x4189f9`
EDX = the flag or C; after a failed draw EAX = 41·n, EDX = 0, ECX = n;
after `region_hit` its EDX/ECX.

**Memory order.** `[P+0xe]` is read for slot `+0x74` and reread for slot
`+0x70`; `[A]` is read before each slot call. After the second
`part_ratio`: the player byte `[A+0xa3]`, then (after the notice calls)
`[A+0x1f2]` → `+0x56`, `[A+0x45]` → `+0x12`. `[A+0x202]` once. After
`weapon_eject`: `[A+0x20e]`, then `[A+0x45]` → `+0x12` again; `[W+0x1c]` →
`+0x56` after `multiply_q10`. The pool pointer after `region_hit`;
`[P+0xa]` after the explosion draw; `[R+6]`, then the word f−0x4a
(lowered by `debris_table` on the second list), then `[R]`.

### Part-hit results, residue and validation

**Result liveness.** Only EDX:EAX leave the hook (a replaced hook returns
its entry ECX). All seven call sites rewrite EAX and EDX before reading
them (`0x418e49`/`0x418e4c`; `0x419d0d`, `0x419e1f`, `0x419ebe`/`0x419f5d`,
`0x41a086`, `0x41a24b` in `0x4198f4`). ECX can reach `weapon_converge`'s
entry (path `0x41a243` → `0x41a2a0` → `0x41a322` → `0x41a34d`), which
writes ECX before reading it ([input.md](input.md#weapon_converge-41a74c));
it is dead everywhere.

**RNG stream A**, in call order: the weapon-destruction draw (only for a
changed bracket below 0x100 on a weapon-mount part ≥ 19 with gate word
C+0x56 ≠ 0), `weapon_eject`'s own draws (after a successful draw), the
explosion type (after a successful allocation), the debris burst's draws
(changed bracket below 0x100). The panel jolt `0x434010` draws stream B.

**Stack and residue.** No frame local is read before it is written (f−0x24
by `0x49630c`). The frame lies in the adapter's 256-byte scratch (f =
scratch + 208). The callees behind the bridge run from host frames; static
audit (first access per `[ebp−k]` in address order, checked by hand):
`0x434010` has no frame; `cockpit_notice` builds its event record with
unwritten bytes (section "Stack, frames and residue"); `weapon_eject`
reads f−0x34 after `aim_angles` (`0x492990`) wrote it through f−0x38;
`region_hit`, `region_absorb`, `region_spread` write every local before
reading it; `region_break`'s first reads are f−0x34 (its record), f−0x7c
(written by `aim_angles` through f−0x80) and f−0x90/f−0x94 (written by
`0x4801d4` through f−0x98). No entry shim.

**Compare mode** refused (game calls).

**In vitro** (`differential_mech_part_hit_test`): original against core
with logging stubs and real nested hooks; the stubs change every reread
value between the calls (and P+4/P+8, which must not be reread). Branch
sites (16, all both ways, none proved): `0x418944`, `0x41898a`,
`0x4189b3`, `0x4189b9`, `0x4189bd`, `0x4189c2`, `0x4189d1`, `0x4189df`,
`0x4189f0`, `0x4189f9`, `0x418a0c`, `0x418a45`, `0x418a8f`, `0x418b02`,
`0x418b58`, `0x418b5f`. Map callee rows: `0x410670` 1/1/0/4/0/0, `0x426ac8`
1/2/0/3/0/0 (also from `0x418e60`), `0x40f57c` 2/7/0/65/13/14 (also from
`0x40ee0c`, `0x40ee90`), `0x40da38` 16/43/8/549/15/46, `0x434010`
0/0/0/5/0/0.

**Live expectations** (map entry probes, not measured with the hook):
`hk_mech_part_hit_calls` ≈ 5/31/3/455/6/15. Relations with the hook
replaced: `…_call_49630c` = `…_call_471b30` = `…_slot_74` = calls;
`…_call_40dbc0` = 2 × calls; `…_slot_70` ≤ calls; `…_call_420a68` ≤
`…_call_434010` ≤ 0/0/0/5/0/0 (map entries of `0x434010`, which
`ground_collide` also calls); `…_call_40f57c` = `…_call_40da38` ≤ the
weapon-chance draws; `…_call_47e0b0` = calls + the results-list items (≤
`…_call_40f57c`); `…_call_492ee0` = the chance draws + `…_call_407f1c`;
`…_call_4089bc` = `…_call_408530`. The MECH hit test's calls of
`0x4188c8` plus the six `0x4198f4` sites = `hk_mech_part_hit_calls`.

## FLYER part damage `0x421bb4` `flyer_part`

`0x421bb4(F, part, damage, source)`, slot `+0x74` of the FLYER vtable; hook
`flyer_part` in its own adapter group (core `core/src/flyer_part.c`,
adapter `proxy/flyer_part_hooks.h`, in vitro `invitro/flyer_part_cases.h`).
73 instructions, no probe row; prologue `55 8b ec 53 56` (relocate 5). No
frame: `push ebp; mov ebp,esp; push ebx; push esi; push edi` (EBX = F, ESI
= part, EDI = source). No local, no runtime record, no RNG.

1. `[0x4a9660]` ← F+0x24f (the velocity that `debris_heading` adds to new
   debris), stored after the arguments are loaded.
2. `region_hit(F+0x200, part, damage)`. The damage is loaded as `mov
   dx,[ebp+0x10]` and `push edx` pushes the whole register, so `region_hit`
   gets lo(entry EDX, damage) as its third dword; it hands the address of
   that slot to `region_absorb`, which reads and writes only its low word.
   The high word is never read, but the hook passes it exactly.
3. The part is pushed as the full dword ESI; the death test is `movsx
   eax,si; sub eax,1; jae` after AX ≠ 0: only a part whose **low word** is
   0 dies (a dword such as `0x10000` too); otherwise the function returns
   with EAX = s16(part) − 1.
4. Death: with a source, `cockpit_notice(0x2e)` when the source is
   `[0x4d24e6]` and its target `+0x1a4` is F, and the source's slot
   `+0x60`(source, F, 0) (`mech_kill`, or the empty ACTOR default
   `actor_kill` `0x411b2c`); then bytes F+0x99 ← 1, F+0x96 ← 0, F+0x38 ←
   1, dword F+0x2e ← −100000, `death_actions(F)`, `trigger_fire([F+0x1b6])`
   if nonzero, `script_start(F+0x4d, 0x499e60)` (the FLYER "dead" record),
   and when word F+6 ≠ 0 `sfx_stop(0x31)` (`0x462ac4`) and word F+6 ← 0.
5. Always `[0x4a9660]` ← 0 at the end (`xor ecx,ecx; mov [0x4a9660],ecx`):
   `[0x4a9660]` stays set through every callee of the death path. **ECX =
   0 on every exit.**

### FLYER part registers and callers

| Site | Callee | EAX | EDX | ECX | Slots |
|---|---|---|---|---|---|
| `0x421bdb` | `region_hit` | F+0x24F | lo(entry EDX, damage) | F+0x200 | F+0x200, part, that EDX |
| `0x421c0f` | `cockpit_notice` | `[0x4d24e6]` (= source) | `region_hit`'s | `region_hit`'s | 0x2E |
| `0x421c1b` | source slot `+0x60` (`mech_kill` or `actor_kill`) | `cockpit_notice`'s, or `[0x4d24e6]` | `[source]` | 0x2E (`pop ecx`), or `region_hit`'s | source, F, 0 |
| `0x421c41` | `death_actions` | F+0x38 | F+0x92 | slot `+0x60`'s, or `region_hit`'s (no source) | F |
| `0x421c52` | `trigger_fire` | `[F+0x1b6]` | `death_actions`' | F | `[F+0x1b6]` |
| `0x421c61` | `script_start` | `trigger_fire`'s, or 0 | F+0x4D | the trigger pointer, or F | F+0x4D, `0x499e60` |
| `0x421c72` | `sfx_stop` | `script_start`'s | `script_start`'s | `script_start`'s | 0x31 |

**Memory order.** `[0x4a9660]` ← F+0x24F; `region_hit`; `[0x4d24e6]`, and
`[[0x4d24e6]+0x1a4]` only when the source equals it; `cockpit_notice`;
`[source]` (the vtable, read after the notice); slot `+0x60`; bytes
F+0x99 ← 1, F+0x96 ← 0, F+0x38 ← 1, dword F+0x2E ← −100000;
`death_actions`; `[F+0x1b6]`; `trigger_fire`; `script_start`; word F+6;
`sfx_stop`; word F+6 ← 0; `[0x4a9660]` ← 0.

**Results.** EDX:EAX: `region_hit`'s (AX = 0); EAX = s16(part) − 1 with
`region_hit`'s EDX (another part); `sfx_stop`'s (sound) or
`script_start`'s (F+6 = 0) on the death path. ECX = 0.

**Callers through slot `+0x74` and what they read** (all 20 decoded
`call [reg+0x74]` sites, [player_movement.md](player_movement.md)):

| Site | Function | Receiver a FLYER? | After the return |
|---|---|---|---|
| `0x421dba` | FLYER hit test `0x421c8c` | always | EAX, EDX, ECX rewritten (`0x421dc0`, `0x421ddb`, `0x421de3`) before any read |
| `0x411b82` | `actor_radial_hit` `0x411b3c` (ACTOR slot `+0x70` default, FLYER's `+0x70`) | yes | tail return of all three registers to the `+0x70` callers below |
| ↳ `0x426a8d` | `radius_hit` (every actor in range) | yes | CX reloaded (high word kept); when the loop ends right after, EAX's high word (AX = the hit flag), EDX and ECX's high word leave `radius_hit` |
| ↳ `0x41938a` | `ground_collide` (the collided actor EBX from the actor list) | yes | AX ← 1, the high word of EAX, EDX and ECX leave `ground_collide` |
| ↳ `0x419373`, `0x41895a` | `ground_collide`, `0x4188c8` | no (the MECH's own `vehicle_hit`) | — |
| `0x460138`, `0x460197` | cheat dispatcher `0x45feb8` (living side-1 actors within 100000 of the player; the view subject `[0x4d2684]`) | yes | EAX, EDX rewritten at `0x46085c`/`0x46085f`; ECX reaches `0x45feb8`'s return (five callers) |
| `0x4038e6`, `0x404ff6`, `0x4051e9` | `base_part_chain`, `building_hit`, BASE hit test | no (BASE pool) | — |
| `0x417999`, `0x4179d0` | `0x4178e8` (initial damage of the mission loader's MECH, `0x4259b0`) | no | — |
| `0x417a91`, `0x4184fa`, `0x4188ae`, `0x41893b` | `random_damage`, `vehicle_part`, `vehicle_hit`, `0x4188c8` | no (MECH) | — |
| `0x419500` … `0x41959b` | `ground_collide` (on A itself) | no | — |
| `0x41e537` | `ram_move` (on A itself) | no | — |

So all three result registers can leave through other functions'
returns; the hook makes them exact instead of arguing them dead: EDX:EAX
from the core, and ECX = 0 from an **entry shim** that zeroes ECX before
the framework's dispatch (which restores the entry ECX on return, replaced
mode; the original never reads its entry ECX, original mode).

### FLYER part residue and validation

**Stack and residue.** No frame local. The callees behind the bridge run
from host frames: `region_hit` and its subtree write every local before
reading it; `cockpit_notice` builds its event record with unwritten bytes
(section "Stack, frames and residue"); `sfx_stop` has no locals;
`actor_kill` is empty. The one entry shim exists only for ECX.

**Compare mode** refused (game calls).

**In vitro** (`differential_flyer_part_test`): original against core with
logging stubs, installed probes and real callees; the stubs change each
reread value between the calls and record the function's own stores at
every call. The installed probes compare ECX through the shim; without the
`xor` they fail at case 0 (ECX 0x31313131 instead of 0). Branch sites (7,
all both ways, none proved): `0x421be6`, `0x421bf2`, `0x421bfa`,
`0x421c03`, `0x421c0b`, `0x421c4f`, `0x421c6e`.

**Live expectations.** `0x421bb4` has no probe row in the DBSIM map. The
FLYER hit test (922/1000/1000/0/0/0) lists `explosion_create` among its
live callees, which it reaches only after a COL hit and the slot `+0x74`
call, so `hk_flyer_part_calls` > 0 is expected in at least one of
acceptance-60s, c1_01-success, c1_01-collision, and 0 in c3_09, c4_08,
c3_10 (no FLYER there). Relations with the hook replaced:
`…_call_40da38` = calls; `…_call_411bc8` = `…_call_413e50` = FLYER
deaths; `…_slot_60` ≤ `…_call_411bc8`; `…_call_420a68` ≤ `…_slot_60`;
`…_call_423430` and `…_call_462ac4` ≤ `…_call_411bc8`;
`hk_flyer_part_calls` ≥ the FLYER hit test's slot `+0x74` calls (the rest
come from `actor_radial_hit` and the cheat dispatcher).

## COL traversal

Hooks `col_query` `0x40c9d4`, `col_group` `0x40c8fc`, `col_shape`
`0x40c8c8`, `col_bound` `0x40c4c4` in the adapter group `col` (core
`core/src/col.c`, adapter `proxy/col_hooks.h`, in vitro
`invitro/col_cases.h`). Runtime COL layout from
[collision.md](../formats/collision.md): the descriptor {word group count,
dword groups}; group 10 bytes {word part, word, word shape count +4, dword
shapes +6}; shape 16 bytes {word sphere count +0, dword spheres +2, word
region +6, bounding sphere x/y/z/r words +8..+0xf}.

### `0x40c9d4` `col_query`

`0x40c9d4(COL, actor, S, M, flags)`: 45 instructions; 6/125/21/1000/205/64;
callers the three hit tests (`0x405183`, `0x418ddf`, `0x421d67`); prologue
`55 8b ec 53 56 57`, relocate 5; no frame. EDX ← 0; EDI = COL, EAX = S, EBX
= `[COL+2]` (read once, before the statics are reset); word `[0x4a981c]` ←
−1; dword `[0x4a9818]` ← 0; `[0x4a9810]` ← `[S+4]`; word `[0x4a9814]` ←
word S+8 (each read once). For SI = 0 while SI < word `[COL]` (signed word
compare, reread at every test, `0x40ca27`): `col_group(group_i, actor, M,
flags)` (EAX = flags, EDX = M, ECX = actor); groups stride 0xa. Then
`[0x4a981e]` ← `[0x4a9818]`, word `[0x4a9822]` ← word `[0x4a981c]` (via
DX). Result: EAX = ECX = `0x4a981e` when the hit dword is nonzero
(`0x40ca43`), else 0; EDX = `lo(E, [0x4a981c])` with E the last
`col_group`'s EDX (`[S+4]` without a group). Only EAX is read by the hit
tests (BASE returns this EDX on its COL-miss path; dead at the sweep).

### `0x40c8fc` `col_group`

`0x40c8fc(G, actor, M, flags)`: 82 instructions; 48/803/49/1000/393/332;
only caller `0x40ca18`; prologue `55 8b ec 83 c4 dc`, relocate 6: 1,2,3;
frame 0x24. EDX = M; AX = word G+0. Part < 0 (`0x40c911`): copy M's first
20 bytes to f−0x24 (`rep movsd`, ECX ends 0) and the translation in the
order +0x1c → f−8, +0x14 → f−0x10, +0x18 → f−0xc. Otherwise ESI = M, H =
`[actor+0x34]`, L = `[H+4]`, n = L.V20(L, `lo(G, part)`) — the shape
manager's node lookup (section "Node lookup"); n ≠ 0 (`0x40c94e`) and
`s16(n+4)` > −1 (`0x40c955`): N = `[H+0x16] + 32·s16(n+4)`, else N =
`0x6c56a8`; `matrix_compose(N, M, f−0x24)` (EAX = ECX = N, EDX = f−0x24).
Then **`[0x4a980c]` ← f−0x24 (a frame address in a global)**; for DI = 0
while DI < word `[G+4]` (reread at every test, `0x40c9c6`), shapes `[G+6]`
(read once, after the store to `[0x4a980c]`) stride 0x10: SI = word
shape+6 (region); word `flags[s16(region)]` ≠ 0 (`0x40c99b`): r =
`col_shape(shape)` (EAX = `s16(region)`, EDX = flags, ECX = G); r ≠ 0
(`0x40c9a9`): `[0x4a9818]` ← r, word `[0x4a981c]` ← SI. Return EAX = 0,
ECX = G, EDX = G without a shape, else from the last shape: flags (flag
word 0), `col_shape`'s EDX (flag set, r = 0) or r (hit).

Memory order: `[actor+0x34]`, `[H+4]`, `[L]` and the slot before the
lookup; word `[n+4]` (twice) and `[H+0x16]` after it.

**Node lookup.** The lookup's receiver `[[actor+0x34]+4]` is the root of
the shape's node tree. Among the runs of code pointers in DATA (vtable scan
of the image), the `+0x20` entries that find a node by id are `0x47622c`
(leaf class: returns `this` when word `[this+6]` equals the id word, else
0) and `0x476850` (group class: `this` on a match, else the first child
`[[this+0x12]+4i]` (i below word `[this+0x10]`) whose own `+0x20` finds
it). Both read only their two stack slots, never the entry ECX or EDX;
`0x47622c` writes only EAX/EDX, `0x476850` saves EBX/ESI/EDI. Their
returned EDX/ECX are dead in `col_group` (ECX rewritten at
`0x40c95e`/`0x40c965`, EDX at `0x40c96c`). The replacement calls them as
original code with the original's ECX (`col_group`'s entry ECX, the actor
when called from `col_query`). The other `+0x20` entries of the scan
(`0x4787e4`, `0x478760`: index steps of a sequence class; `0x405038`,
`0x421c8c`: actor hit tests) are not node lookups.

### `0x40c8c8` `col_shape`

`0x40c8c8(shape)`: 24 instructions; 51/1000/64/1000/548/373; only caller
`0x40c99e`; prologue `55 8b ec 53 56 8b 5d 08`, relocate 5: 1,2,1,1.
`col_bound(shape+8, lo(EAX, shape+0xe))` (EDX = shape+8, ECX = the entry
ECX; EAX = `lo(entry EAX, radius)`); AX ≠ 0 (`0x40c8e6`): ESI =
`sphere_hits(shape, 0)` (`0x40c524`; EAX/EDX/ECX = `col_bound`'s). Return
EAX = ESI (0 without the call), EDX/ECX the last callee's.

### `0x40c4c4` `col_bound`

`0x40c4c4(center, radius)`: 39 instructions; counts as `col_shape`; only
caller `0x40c8db`; prologue `55 8b ec 83 c4 f4`, relocate 6.
`matrix_point(center, [0x4a980c], f−0xc)` (`0x4801d4`; EAX =
`[0x4a980c]`, EDX = center, ECX = f−0xc); b = `s16(radius) + [0x4a9810]`
(`[0x4a9810]` read after `matrix_point`); b ≤ the local Y **unsigned**
(`0x40c4f2` JBE → `0x40c51a`) → EAX = 0 with `matrix_point`'s EDX/ECX.
Else d = `dist_octagonal(x, z)` (`0x47de4c`; local X, Z, Z read before X;
EAX = b, EDX/ECX `matrix_point`'s); d < `s16(radius) + s16([0x4a9814])`
signed (`0x40c512`; the word reread after `dist_octagonal`): AX = 1 (EAX's
high word = d's), else EAX = 0; EDX = `s16(radius) + s16([0x4a9814])`, ECX
= `s16([0x4a9814])`. `matrix_point` writes all three output dwords on each
of its three mode-byte paths (< 1, = 1, > 1), so `col_bound` never reads
residue.

### COL registers, statics and validation

| Site | Callee | EAX | EDX | ECX | Slots |
|---|---|---|---|---|---|
| `0x40ca18` | `col_group` | flags | M | actor | G, actor, M, flags |
| `0x40c946` | node lookup | L = `[H+4]` | `[L]` | entry ECX | L, `lo(G, part)` |
| `0x40c972` | `matrix_compose` | N | f−0x24 | N | N, M, f−0x24 |
| `0x40c99e` | `col_shape` | `s16(region)` | flags | G | shape |
| `0x40c8db` | `col_bound` | `lo(entry EAX, radius)` | shape+8 | entry ECX | shape+8, `lo(entry EAX, radius)` |
| `0x40c8eb` | `sphere_hits` | `col_bound`'s | `col_bound`'s | `col_bound`'s | shape, 0 |
| `0x40c4de` | `matrix_point` | `[0x4a980c]` | center | f−0xc | center, `[0x4a980c]`, f−0xc |
| `0x40c4fc` | `dist_octagonal` | b | `matrix_point`'s | `matrix_point`'s | X, Z |

**Liveness of the result registers.** A replaced hook returns its entry
ECX and flags; both are dead at every caller. ECX: after `col_query`, BASE
rewrites it at `0x405195` (hit) or `0x4051b1`/`0x4052a2`, MECH at
`0x418df5` or on the `0x418e49` exit, FLYER pops it at `0x421d8d` (the
`rng_next` call before reads no entry register: `0x492ee0` loads CX itself
and then `movsx ecx, cl`) or leaves on `0x421e70`; after `col_group`,
`col_query` rewrites it at `0x40ca13` or `0x40ca45`/`0x40ca4c`; after
`col_shape`, `col_group` pops it at `0x40c9a3`; after `col_bound`,
`sphere_hits` loads it at `0x40c5ae` before any use, or `col_group` pops
it. EDX of `col_query` is used only by BASE's COL-miss exit `0x40529f`
(dead at the sweep); MECH clears it at `0x418de4`, FLYER rewrites it at
`0x421d8a`/`0x421e73`. The replacement returns the exact EDX:EAX anyway.

**The statics.** A byte search of the image for the dwords
`0x4a980a`..`0x4a9825` finds 23 occurrences, all operands of instructions
in `0x40c428`..`0x40ca4f`. `0x40c524` is called only from `0x40c8eb`;
`0x40c4a4` (a reader of `[0x4a980c]`) has no caller. So the readers of
`[0x4a980c]`, `col_bound` (`0x40c4ca`) and `sphere_hits` (`0x40c576`), run
only inside `col_group`'s shape loop, after the store at `0x40c980` of the
same activation, and `0x4a9810`..`0x4a9822` are read only inside the
traversal or, through the returned pointer, by the hit tests. No oracle
watch set covers `0x4a980c`..`0x4a9823`. The replacement writes all seven
statics with the original's values and in its order, except:

- **`[0x4a980c]`**, the original's `col_group` frame address (entry ESP −
  0x28). The replacement stores the address of its own matrix, the
  adapter's zeroed 32-byte scratch on the same thread stack. While
  `col_group` runs, the pointer is meaningful: `col_bound` and
  `sphere_hits` read through it the same 32 bytes as in the original (the
  copied or the composed matrix). After the return it dangles in both
  programs and is never read before the next `col_group` overwrites it.
  Only compare mode or a `mem_watch` of the dword could see the different
  value; all four hooks refuse compare mode. Storing the exact value would
  need the matrix at the original's stack coordinate (an entry shim with a
  stack gap) and changes nothing observable.
- Byte 19 of that matrix: the adapter's scratch is zero where the original
  keeps stack residue on the lookup path; `matrix_point` reads the nine
  words, byte 18 and the translation, never byte 19.

**No entry shim.** `col_query` and `col_shape` have no frame;
`col_group`'s matrix and f−4 and `col_bound`'s vector are written before
they are read; entry registers are only handed on (`col_shape`'s entry EAX
high word and ECX to `col_bound`, which reads only BX of its second slot;
`col_group`'s entry ECX to the lookup). None of the callees behind the
bridge reads uninitialized stack of its own (`sphere_hits` writes f−8..f−3
before `matrix_point` reads them). Every game call goes through the bridge
(`col_bound` calls `matrix_point` through it), so nested hooks keep their
modes; the node lookups run as original code. All four refuse compare
mode.

**In vitro** (`differential_col_test`, quick tier; full tier
`differential_col_full_test`): per hook 100,000 direct cases against
logging stubs for every game callee (the group's own nested functions
included; arguments, entry registers, the matrix behind `[0x4a980c]` when
`col_shape` is called, the center `matrix_point` receives), stub answers
at the thresholds (AX 0 with a high word, the unsigned Y bound ±1 and
negative Y, the signed distance bound ±1, negative distances, node null /
index −1 / −2 / `0x8000`) and pokes of the words the original rereads
(group and shape counts, `[G+6]` after `matrix_compose`, region and flag
words, `[H+0x16]`, `0x4a9810`, `0x4a9814`) or must not reread (`[COL+2]`,
`[actor+0x34]`); EDX:EAX, the arena, the statics `0x4a980c`..`0x4a9823`
(`[0x4a980c]` as "a stack address") and the callee counter deltas
compared. 256 installed 31/a7 probes per hook and 3,072 real-callee cases
per hook (the real lookups `0x47622c`/`0x476850` over an own two-level
tree, real `0x4801d4`, nested `col_*`, `matrix_compose`, `sphere_hits`,
`sphere_test`, `dist_octagonal` in rotating modes; 695/533/485/1,026 cases
with a hit). Seven caught mutations
([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).

Branch sites (11, all both ways in vitro): `0x40ca27`, `0x40ca43`,
`0x40c911`, `0x40c94e`, `0x40c955`, `0x40c99b`, `0x40c9a9`, `0x40c9c6`,
`0x40c8e6`, `0x40c4f2`, `0x40c512`. All are `BRANCH` lines of
`branch_sites.h`; the JBE `0x40c4f2` (2 bytes) is counted by the condition
tables like the others (its fall-through edge is exactly the call of
`0x47de4c`; fall/taken 36,001/63,999 in the quick tier, 359,344/641,680 in
the full tier).

**Live expectations** (map entry probes; with the hooks replaced):
`col_query` ≈ 6/125/21/1000/205/64, `col_group` ≈ 48/803/49/1000/393/332,
`col_shape` = `col_bound` ≈ 51/1000/64/1000/548/373. Relations:
`col_query` calls = the hit tests' COL-path counts; `col_bound` =
`col_shape`; `hk_sphere_hits_calls` ≤ `col_bound`.

## Debris burst

Hooks in the adapter group `debris_burst`: `debris_table` `0x4089bc`,
`debris_burst` `0x408530`, `debris_pick` `0x4086bc`, `debris_piece`
`0x408588`, `debris_init` `0x408940`, `debris_spin` `0x408b74`,
`debris_spin_any` `0x408bb4` (core `core/src/debris_burst.c`, adapter
`proxy/debris_burst_hooks.h`, in vitro `invitro/debris_burst_cases.h`).

**Records.** The list records at `0x4a9650` (the default list) and
`[0x4a964c]` (the second list) are {dword tables, word count at +4, dword
shapes at +6}; the callers hand `debris_burst` the table `[R] + 10·s16(w)`
and the shapes `[R+6]`. A table T is a 10-byte record {word count, word
count2, dword pieces, word bound}. Piece record (14 bytes): +0 shape index
(word, sign-extended, ×4 into the shapes table), +2 weight, +4 the byte
stored at obj+0x41 (pushed sign-extended), +6 the word stored at obj+0x59,
+8 angle or −1, +0xa heading or −1, +0xc speed (sign-extended;
`debris_heading`'s divisor).

### `0x4089bc` `debris_table`

`0x4089bc(&w)`: 14 instructions; 34/84/8/620/108/85; twelve call sites in
ten functions; prologue `55 8b ec 53 8b 4d 08`, relocate 7: 1,2,1,3. ECX =
&w, EAX = `0x4a9650`, EDX = `s16([0x4a9654])`; reads `s16([0x4a9654])`,
then `s16(w)`, compares them as signed dwords; EDX > `s16(w)` (`jg` at
`0x4089d4`) → return; else EAX = `[0x4a964c]` (the second list, last
written by `vehicle_part`, `base_part_debris`, `debris_update` or
`0x408911`) and word w −= DX (16-bit SUB). EBX restored. Returns EAX =
`0x4a9650` or `[0x4a964c]`, EDX = `s16([0x4a9654])`, ECX = &w. Pure except
the index word; exported as `es2_debris_table_registers`. `debris_update`
(pools.c) computes it in core, so its spawns do not reach the hook; the
hit tests, `mech_part_hit`, `mech_fly`, `base_part_debris` and
`region_break` call it through the bridge.

### `0x408530` `debris_burst`

`0x408530(T, shapes, pos, flag)`: 35 instructions; 23/59/5/503/81/47;
prologue `55 8b ec 83 c4 e0 53`, relocate 6: 1,2,3. Callers: `0x40379c`
(`base_part_debris`), `0x405038`, `0x408bd8` (`debris_update`), `0x4188c8`,
`0x4198f4` (three sites), `0x421c8c`. memset `0x49681a`(f−0x20, 0, 0x20)
(EAX = f−0x20, entry EDX, entry ECX; RTL); EDX = lo(memset's EDX,
0x4000), the words f−0x10, f−0x1a, f−0x20 ← 0x4000 in that order (an
identity rotation, mode byte 0); the translation from pos in the order +8
→ f−4, +0 → f−0xc, +4 → f−8; `debris_pick(T, shapes, f−0x20, flag)` (EAX
= f−0x20, EDX = shapes, ECX = flag). Returns `debris_pick`'s EDX:EAX.

### `0x4086bc` `debris_pick`

`0x4086bc(T, shapes, M, flag)`: 62 instructions; 34/84/8/620/108/85.
Callers `debris_burst` and `region_break` (`0x40d785`, `0x40d97e`). `push ecx` keeps
the entry ECX at f−4; the count word T+0 is read once (EAX = lo(entry
EAX, count)) and kept at f−2.

- Count ≠ 0 (`0x4086cc`): count times, the counter DI compared with it as a
  signed word (`0x4086d8`, `0x40871c`; a negative count picks nothing): r
  = `rng_bounded(0x4d2599, word T+8)` with EAX = lo(previous EAX, T+8)
  (the high word is the entry EAX or the last `debris_piece`'s EAX;
  `rng_bounded` reads only the word, `movsx` at `0x492f31`), EDX and ECX as
  left. The walk starts at `[T+4]` (both words reread per pick): ECX =
  lo(rng ECX, weight), `cmp dx,cx; jg` (`0x408700`), DX −= CX, piece +=
  14, **no end check** (a weight table that does not cover the bound runs
  past the list; precondition). `debris_piece(shapes, piece, M, flag)`
  with EAX = shapes, EDX = M, ECX = lo(rng ECX, last weight).
- Count = 0: every piece of `[T+4]` in order (the pointer read once), word
  T+2 of them (reread per piece, `0x408744`); `debris_piece` gets EAX =
  flag, EDX = M, ECX = shapes.

Returns the last `debris_piece`'s EDX:EAX, or lo(entry EAX, count) with
the entry EDX when nothing is called. ECX on return is the popped f−4
dword: (count << 16) | entry CX on the count path, the entry ECX
otherwise.

### `0x408588` `debris_piece`

`0x408588(shapes, piece, M)`: 105 instructions; 95/238/22/1000/333/239;
prologue `55 8b ec 83 c4 a4`, relocate 6; record f−0x24 from descriptor
`0x497d70`. It reads three arguments; the flag that `debris_pick` hands on
is never read.

1. `0x49630c` (EAX = `0x497d70`, entry EDX, entry ECX).
   `pool_alloc([0x4a9642])` (its EAX, the pool, its ECX); then ECX = the
   pool (`pop ecx`). Object → f−4.
2. Non-null (`0x4085b5`): context word 0x14; reads piece+6, +4, +0 in that
   order; `debris_init(obj, shapes[s16(+0)], M+0x14, s16(+4), s16(+6))`
   (EAX = obj, EDX = shapes, ECX = the shape); then `[[0x6cbee4]]` −= 4
   (EDX = the counter pointer), context word 8. ECX = f−4. Null object
   (`0x4085fc`, taken exactly when `0x4085b5` was) → return EAX = 0 with
   `pool_alloc`'s EDX.
3. EAX = lo(EAX, piece+8). Angles when +8 ≠ −1 (`0x40860a`), or +8 = −1
   and +0xa ≠ −1 (`0x408611`): words f−0x34, f−0x32 ← 0, f−0x30 ← AX;
   `euler_matrix(f−0x34, f−0x48)` (EAX = f−0x34, EDX = f−0x48, ECX = the
   object); `rotation_compose(f−0x48, M, f−0x5c)` (`0x47f058`, the
   rotation-only compose; EAX = M, EDX = f−0x5c, ECX = f−0x48);
   `matrix_angles(f−0x5c, f−0x2c)` (EAX = f−0x2c, EDX = f−0x5c, ECX =
   `rotation_compose`'s ECX).
4. EAX = lo(EAX, piece+0xa); ≠ −1 (`0x40865e`): DX = word f−0x28 + AX −
   piece+8 (16-bit, over `matrix_angles`' EDX), `debris_spin(obj,
   s16(+0xc), EDX)` with EAX = EDX, ECX = s16(+0xc). −1:
   `debris_spin_any(obj, s16(+0xc))` with EAX = s16(+0xc), EDX and ECX as
   left (after the angles `matrix_angles`' EDX/ECX, without them the
   counter pointer and the object).
5. piece+8 is reread (`0x408691`); ≠ −1: obj+0xc ← dword f−0x2c (from
   `matrix_angles`), word obj+0x10 ← f−0x28, word obj+0x32 ← 0; EAX =
   obj+0xc, EDX = (word f−0x2a << 16) | word f−0x28.

Returns that store's or the spin's EDX:EAX; ECX = the saved head (dead).
Every local read is written first: the angles exist whenever piece+0xa or
piece+8 is not −1, and the final store from f−0x2c (`0x408693`) follows the
angles whenever the angle word was not −1 at `0x408602` (the piece table is
static data; the in-vitro stubs keep that invariant). The three calls
`euler_matrix`, `rotation_compose` and `matrix_angles` run once per piece
with an angle or a heading word.

### `0x408940` `debris_init`

`0x408940(obj, shape, pos, b, w)`: 40 instructions. Callers `0x4085d9`
(`debris_piece`) and `0x40f702` (`weapon_eject`); the third call site
`0x40d885` lies in dead code of `0x40d434` (section "Region damage 40da38
and part break 40d434"). `object_init(obj, shape)` (EAX = shape, entry EDX,
entry ECX); `[obj]` ← `0x497ed8`; word obj+4 ← 1; pos +8, +0, +4 →
obj+0x2e, +0x26, +0x2a; dword obj+0x50 ← 0; byte obj+0x41 ← b (EAX = b &
0xff); word obj+0x59 ← w (EDX = lo(pos+4, w)); `rng_bounded(30000)` (that
EAX and EDX, ECX = pos+0); dword obj+0x55 ← s16(draw) + 2000; dword
obj+0x5d ← `[0x4a964c]`; `[[0x6cbee4]]` += 1. Returns EAX = obj, EDX =
`[0x4a964c]`, ECX = the counter pointer.

### `0x408b74` `debris_spin` and `0x408bb4` `debris_spin_any`

`0x408b74(obj, speed, heading)` (25 instructions): reads `[0x4a9664]`, then
`[0x4a9666]`; `rng_bounded(lo(entry EAX, hi − lo))` (entry EDX/ECX);
`debris_heading(obj, speed, lo(rng EAX, draw + lo), lo(rng EDX,
heading))` (`0x4089e4`; EAX = speed, that EDX, rng ECX). Returns its
EDX:EAX. `debris_heading` is called from `0x408ba8` (here) and `0x40f741`
(`weapon_eject`); `0x40d901` lies in the dead code of `0x40d434`.

`0x408bb4(obj, speed)` (16 instructions): `rng_next` (entry registers);
`debris_spin(obj, speed, rng EAX)` (EAX = speed, rng EDX, ECX = `0x4d2599`
from `pop ecx`).

Both are called only inside the group.

### Debris RNG, liveness, residue and validation

**RNG stream A per piece**, in call order: the weighted pick (one
`rng_bounded`, count path only); `debris_init`'s `rng_bounded(30000)`
(only after a successful allocation); the spin: `debris_spin_any`'s
`rng_next` when the heading word is −1, `debris_spin`'s `rng_bounded`,
`debris_heading`'s `rng_next`.

**Result liveness at the callers** (static). Only EDX:EAX leave a hook,
so ECX must be dead after each. `debris_table`: all twelve sites `pop ecx`
at once and rewrite EDX before reading it. `debris_burst`: `0x40381c` and
`0x403852` rewrite ECX; `0x403884` and the `jle` at `0x403821` return from
`0x40379c` to `0x403ad8`, where every path writes ECX before reading it;
`0x40529f`, `0x418b95`, `0x421e70` return to callers with ECX dead
(sections "Hit tests (group `hit_test`)" and "MECH part hit `0x4188c8`
`mech_part_hit`"); `0x41a0cd`/`0x41a101` → `0x41a10e` and `0x41a28d` →
`0x41a299` load ECX; `0x408e2f` returns from `debris_update`, whose caller
pops ECX. `debris_pick` in `0x40d434`: `0x40d986` → `0x40d9cd` rewrites
ECX. `debris_piece` in `debris_pick`: the next call reloads ECX or the
epilogue pops it. `debris_init` (ECX = the counter pointer) at `0x40f702`
→ `0x40f72e` loads CX, so its high word reaches only `debris_heading`'s
fourth argument, of which only the low word is used (`0x47dff0` reads a
word).

**Stack and residue.** No local is read before it is written (see
`debris_piece`). Byte 19 of the rotations at f−0x48 and f−0x5c is copied
by `rotation_compose` (identity M, mode 0, as `debris_burst` builds it)
but never reaches the object. With a degenerate table (count < 0, or count
0 and T+2 ≤ 0) `debris_burst` returns EAX = lo(its frame address, count),
a stack address that differs under the hook; no caller reads EAX after
`debris_burst`. No entry shim: no callee in the subtree reads
uninitialized stack (`rotation_compose`, `matrix_angles`,
`debris_heading` and its leaves `0x47dff0`, `0x47e0c0`, `0x47e0b0` read
only their arguments and their own written locals).

**Implementation.** Every game call goes through the guest bridge with the
registers above; nested hooks keep their modes (`rng_bounded`,
`rng_next`, `pool_alloc`, `object_init`, `euler_matrix`,
`rotation_compose`, `matrix_angles`, `debris_heading` and the group's own
functions); memset `0x49681a` and `0x49630c` run as original code.
`debris_burst` and `debris_piece` keep their frames in a 256-byte adapter
scratch (f = scratch + 208); `debris_piece`'s adapter restores FS:0 to its
entry head. **Compare mode:** `debris_table` accepts it; the other six
refuse it.

**In vitro** (`differential_debris_burst_test`): original against core,
with logging stubs and with real callees. Branch sites (12, all both ways,
none proved): `0x4089d4`; `0x4086cc`, `0x4086d8`, `0x408700`, `0x40871c`,
`0x408744`; `0x4085b5`, `0x4085fc`, `0x40860a`, `0x408611`, `0x40865e`,
`0x408691`.

**Live expectations** (map entry probes; not measured with the hooks):
`debris_burst` ≈ 23/59/5/503/81/47, `debris_pick` ≈ 34/84/8/620/108/85,
`debris_piece` ≈ 95/238/22/≥1000/333/239; `debris_table`, `debris_init`,
`debris_spin` and `debris_spin_any` have no probe rows. With every hook
replaced:

- `hk_debris_burst_call_49681a` = `hk_debris_burst_call_4086bc` =
  `hk_debris_burst_calls`.
- `hk_debris_pick_calls` = `hk_debris_burst_calls` + the calls from
  `0x40d434` (`0x40d785`, `0x40d97e`).
- `hk_debris_table_calls` = `hk_debris_pick_calls` −
  `hk_debris_update_call_408530` (each `debris_burst`/`debris_pick` call
  follows one `debris_table` call; `debris_update` computes it in core).
- `hk_debris_piece_calls` = `hk_debris_pick_call_408588` =
  `hk_debris_piece_call_49630c` = `hk_debris_piece_call_471b30`;
  `hk_debris_pick_call_492f24` ≤ `hk_debris_pick_call_408588`.
- `hk_debris_piece_call_408940` = `…_call_408b74` + `…_call_408bb4` ≤
  `…_call_471b30`; `…_call_47ebb8` = `…_call_47f058` = `…_call_47f9a0` ≤
  `…_call_408940`.
- `hk_debris_init_calls` = `hk_debris_piece_call_408940` + the
  constructions in `weapon_eject`; `hk_debris_init_call_402188` =
  `hk_debris_init_call_492f24` = `hk_debris_init_calls`.
- `hk_debris_spin_any_calls` = `hk_debris_piece_call_408bb4`;
  `hk_debris_spin_calls` = `hk_debris_piece_call_408b74` +
  `hk_debris_spin_any_calls` = `hk_debris_spin_call_4089e4` ≤
  `hk_debris_heading_calls` (also `0x40f741`).
- `hk_base_part_debris_call_4089bc` and `…_call_408530` nest into
  `debris_table` and `debris_burst`.

## MECH player fly movement `0x4198f4` `mech_fly`

`0x4198f4(A)`; hook `mech_fly` in its own adapter group (core
`core/src/mech_fly.c`, adapter `proxy/mech_fly_hooks.h`, in vitro
`invitro/mech_fly_cases.h`). 697 instructions, 0/0/0/0/1000/0; the
`+0x24` member of the MECH state "player fly" (`0x499420`). Prologue `55
8b ec 81 c4 e0 fe ff ff` (relocate 9: 1,2,6). Frame 0x120; EBX = A, ESI =
R = A+0x2b9 (a motion record: dwords +0/+4/+8, velocity dwords
+0xc/+0x10/+0x14, rate words +0x18 pitch / +0x1a roll; *hypothesis*).

**Caller.** No direct call or jump to `0x4198f4` exists; a byte search of
the image finds its address once, at `0x49994c` (DATA), the `+0x24` dword
of the initial state table that the startup code copies into the MECH
state records (`0x41401e`..`0x414036`, [weapons.md](weapons.md)). It is
entered only by the trampoline `mech_move` `0x415afc` (`push ebx; call
[ebp-0xc]`), with EAX = the state record, EDX = its third dword, ECX = the
OR of the three dwords. After the return the trampoline pops ECX
(`0x415b2d`) and returns EDX:EAX to `actor_step`, which rewrites EAX and
EDX (A+0x92) before reading them: only EDX:EAX leave the hook, and they
are dead there; the hook returns them exactly anyway.

**Entry registers.** EAX and EDX are written before any read; the entry
ECX reaches `euler_matrix` (`0x4199f5`, lazy matrix) unless the fifth
probe block ran (it clears ECX at `0x4199c1`). `euler_matrix` loads CL
itself and returns the entry ECX's upper bytes; the pose copy overwrites
ECX at `0x419a7d`. The hook passes its entry ECX on.

1. **Function statics** (`0x419910`..`0x4199da`): five guard bytes
   `0x49a194`, `0x49a1a1`, `0x49a1ae`, `0x49a1bb`, `0x49a1c8` (0 in the
   image) gate one-time stores of five probe offsets (dword triples):
   `0x49a188` (1000, −700, −100), `0x49a195` (−1000, −700, −100),
   `0x49a1a2` (450, −500, 0), `0x49a1af` (−450, −500, 0), `0x49a1bc` (0,
   1000, 0); each guard is incremented. Tested and stored in address
   order.
2. Lazy matrix (`0x4199e8`); copy A+0x12..A+0x31 (all 32 bytes, bytes 18/19
   included, read once) to f−0xdc; five `transform_point`s of the probes
   through it into f−0x8c (part 8), f−0x98 (part 7), f−0xbc (part 5),
   f−0xb0 (part 4), f−0xa4 (nose).
3. Byte `[A+0xa4]` = 0 (`0x419b28`): the velocity dwords R+0xc/+0x10/+0x14
   (each read once) through `movement_step` (word argument) and added to
   the position in the order +8 (Z), +0 (X), +4 (Y).
4. **Static sweep packet** `0x49a170`: +0 ← f−0x114 (a segment in this
   frame: +0 → the pose copy f−0xdc, +4 ← `[R+4]`, +8 the radius word),
   word +4 ← 8000, +6 ← 8000, +8 ← 0, dword +0xe ← 0 (no owner), word +0x12
   ← 5, dword +0x14 ← A. Dword +0xa is never written: the image holds
   `0x49a158`, an effect table (words 11, 11, 11, 11, 0, 1, 4, 5, 0, 1, 4,
   5). f−8 ← `[A+0x20e]` (the region flag array, read once).
5. **The pose copy is the segment pose.** Each block replaces the copy's
   translation (f−0xc8/f−0xc4/f−0xc0) with the block's probe point from
   step 2 after its terrain test. The terrain probes of the parts 8, 7 and
   the nose and the slope point are recomputed through that copy, so they
   are offset by the previous block's segment start, not by A's position:
   part 8 sees A's position before the integration, part 7 the part-8
   point, the nose the last segment start (part 5, 4, 7 or 8), the slope
   the nose point.
6. **Parts 8 and 7** (region words `+0x10`, `+0xe`; radius 300): the probe
   recomputed into f−0x80, `terrain_height([0x4a0be0], f−0x80)` compared
   (signed dword) strictly with the recomputed Z (f−0x78); terrain above →
   contact without sweep, Z f−0x78 raised to the height; else
   `sweep(0x49a170)`, 0 → skip (a sweep hit does not raise f−0x78).
   Contact: the roll word R+0x1a changes by `multiply_q10(4000, depth)`
   with `[0x49a174]` ← `multiply_q10([R+4], 500)` after a terrain contact,
   or by 4000 with `[0x49a174]` ← 1000 after a sweep hit (subtracted for
   part 8, added for part 7); word A+0xe ← A+0xe + R+0x1a, word A+0x32 ← 0
   (matrix stale); `mech_part_hit(A, part, 0x49a170, f−0x80)`.
7. **Parts 4 and 5** (region words `+8`, `+0xa`; radius 150): sweep only.
   Part 4 adds 8000 (`0x1f40`) to R+0x1a, part 5 subtracts 8000
   (`0xe0c0`); both store 5000 in `[0x49a174]`, update A+0xe and clear
   A+0x32. Both pass f−0xb0 (the part-4 point) to `mech_part_hit(A, 4|5,
   0x49a170, f−0xb0)`, also part 5, whose segment started at f−0xbc
   (`0x419f3d`; reproduced).
8. **Nose** (part 0, region `+0`, radius 200): as parts 8/7 on the pitch
   word R+0x18 and A+0xc. R+0x18 is clamped to ≥ 0 on either contact
   (terrain or sweep, `0x419fea`), then += `multiply_q10(2000, depth)`
   with `[0x49a174]` ← `multiply_q10([R+4], 1000)`, or += 2000 with
   `[0x49a174]` ← 5000. f−0x3c ← whether the terrain was above. After
   `mech_part_hit(A, 0, 0x49a170, f−0x80)`, if the region flag word `+0` is
   now 0 (`0x41a08d`): debris w = 3 (`debris_table` + `debris_burst(…,
   f−0x80, 0)`) and byte `[A+0xa4]` ← 1. The two debris variants differ
   only in where the word and the record live (f−0x3c = 1: w at f−0x3e,
   the record at f−0x44; f−0x3c = 0: w at f−0x46, the record in EAX) and
   in the registers at the burst call (table below). Both read `[T+6]`,
   then the (possibly lowered) word w, then `[T]`.
9. **Slope** (regions +8, +0xa, +0 all nonzero; the slope reads the region
   words three times): the point (0, 15000, −1500) through the pose copy,
   terrain above it → R+0x18 clamped ≥ 0, += `multiply_q10(20, diff)`,
   A+0xc updated, A+0x32 ← 0. No part hit and no packet change.
10. **Ground clamp**: terrain under the position (copied to f−0x80; reads
   A+0x2e, A+0x26, `[0x4a0be0]`, A+0x2a in that order) above Z
   (`0x41a1fd`): Z ← terrain, the position written back (+8, +0, +4),
   `[0x49a174]` ← `multiply_q10([R+4], 5000)`, `mech_part_hit(A, 6,
   0x49a170, f−0x80)`; region word `+0xc` = 0 → debris (w = 3) and byte
   `[A+0xa4]` ← 1.
11. **Audio** (player flag `[A+0xa3]` only, `0x41a2aa`): dead byte
   `[A+0x99]` → `sfx_stop(0x2d)`; else d = `dist3d_delta([R], [R+4],
   [R+8])` (reads `[R+8]`, `[R+4]`, `[R]`), v = 16·d + `0x6d60` (32-bit),
   v ≥ `0xffff` (signed) → `0xffff`, then v ≤ 0 → 0; `sfx_rate(0x2d, v)`,
   `sfx_refresh(0x2d, A+0x26)`.
12. `weapon_converge(A, target ? dist_gate(A+0x26, target+0x26) : 0)`
   (target `[A+0x1a4]`, read twice). Return AX = 1 (EAX = lo(`weapon_converge`'s
   EAX, 1), EDX = its EDX).

RNG only inside the callees (`sweep`'s terrain effect, `mech_part_hit`,
`debris_burst`).

### Mech-fly registers at the game calls

| Site | Callee | EAX | EDX | ECX | Slots |
|---|---|---|---|---|---|
| `0x4199f5` | `euler_matrix` | A+0x12 | A+0xc | entry ECX (0 after the fifth probe block) | A+0xc, A+0x12 |
| `0x419aaa` | `transform_point` | f−0xdc | `[A+0x2a]` | f−0x8c | `0x49a188`, f−0xdc, f−0x8c |
| `0x419ac5` | `transform_point` | as left | f−0x98 | f−0xdc | `0x49a195`, f−0xdc, f−0x98 |
| `0x419ae0` | `transform_point` | f−0xbc | f−0xdc | as left | `0x49a1a2`, f−0xdc, f−0xbc |
| `0x419afb` | `transform_point` | f−0xdc | as left | f−0xb0 | `0x49a1af`, f−0xdc, f−0xb0 |
| `0x419b16` | `transform_point` | as left | f−0xa4 | f−0xdc | `0x49a1bc`, f−0xdc, f−0xa4 |
| `0x419b4f` | `movement_step` | `[R+0x14]` | `[R+0xc]` | `[R+0x10]` | `[R+0xc]` |
| `0x419b66` | `movement_step` | lo(first EAX, R+0x10) | as left | s16(first AX) | that EAX |
| `0x419b7d` | `movement_step` | as left | s16(second AX) | lo(second slot, R+0x14) | that ECX |
| `0x419c22`, `0x419d2b` | `transform_point` | `[f−8]` | f−0x80 | f−0xdc | probe 8 / 7, f−0xdc, f−0x80 |
| `0x419f7a` | `transform_point` | f−0xdc | `[f−8]` | f−0x80 | `0x49a1bc`, f−0xdc, f−0x80 |
| `0x41a165` | `transform_point` | f−0x80 | f−0xdc | f−0x120 | f−0x120, f−0xdc, f−0x80 |
| `0x419c35`, `0x419d3e`, `0x41a178` | `terrain_height` | f−0x80 | `[0x4a0be0]` | as left | map, f−0x80 |
| `0x419f8d` | `terrain_height` | as left | f−0x80 | `[0x4a0be0]` | map, f−0x80 |
| `0x41a1f0` | `terrain_height` | `[A+0x2a]` | f−0x80 | `[0x4a0be0]` | map, f−0x80 |
| `0x419c76` | `sweep` | Z f−0x84 | X f−0x8c | Y f−0x88 | `0x49a170` |
| `0x419d88` | `sweep` | `[R+4]` | X f−0x98 | Y f−0x94 | `0x49a170` |
| `0x419e68` | `sweep` | Y f−0xac | `[R+4]` | X f−0xb0 | `0x49a170` |
| `0x419f07`, `0x419fd7` | `sweep` | `[R+4]` | X f−0xbc / f−0xa4 | Y f−0xb8 / f−0xa0 | `0x49a170` |
| `0x419c9a`, `0x419dac`, `0x41a008` | `multiply_q10` | depth / as left / as left | depth | as left / depth / depth | 4000 / 4000 / 2000, depth |
| `0x419cb5`, `0x419dc7`, `0x41a023` | `multiply_q10` | `[R+4]` | as left / `[R+4]` / `[R+4]` | `[R+4]` / as left / as left | `[R+4]`, 500 / 500 / 1000 |
| `0x41a1a0` | `multiply_q10` | the difference | as left | as left | 20, difference |
| `0x41a229` | `multiply_q10` | `[R+4]` | `[f−0x7c]` | `[R+4]` | `[R+4]`, 5000 |
| `0x419d05`, `0x419e17` | `mech_part_hit` | A+0xc | lo(as left, new A+0xe) | f−0x80 | A, 8 / 7, `0x49a170`, f−0x80 |
| `0x419eb6` | `mech_part_hit` | A+0xc | f−0xb0 | lo(`0x49a170`, new A+0xe) | A, 4, `0x49a170`, f−0xb0 |
| `0x419f55` | `mech_part_hit` | f−0xb0 | lo(the sweep's EDX, new A+0xe) | A+0xc | A, 5, `0x49a170`, f−0xb0 |
| `0x41a07e` | `mech_part_hit` | f−0x80 | lo(as left, new A+0xc) | 0 | A, 0, `0x49a170`, f−0x80 |
| `0x41a243` | `mech_part_hit` | f−0x80 | as left | as left | A, 6, `0x49a170`, f−0x80 |
| `0x41a09f`, `0x41a25f` | `debris_table` | as left | `[f−8]` | f−0x3e / f−0x5a | &w |
| `0x41a0dc` | `debris_table` | as left | f−0x46 | as left | &w |
| `0x41a0c8`, `0x41a288` | `debris_burst` | 10·s16(w) | the table | s16(w) | `[T]`+10·s16(w), `[T+6]`, f−0x80, 0 |
| `0x41a0fc` | `debris_burst` | the table | 10·s16(w) | s16(w) | the same |
| `0x41a2ba` | `sfx_stop` | A+0x92 | as left | as left | `0x2d` |
| `0x41a2d9` | `dist3d_delta` | `[R+4]` | `[R]` | `[R]` | `[R]`, `[R+4]`, `[R+8]` |
| `0x41a30c` | `sfx_rate` | v | v | d | `0x2d`, v |
| `0x41a31a` | `sfx_refresh` | as left | as left | A+0x26 | `0x2d`, A+0x26 |
| `0x41a33f` | `dist_gate` | the target T | T+0x26 | A+0x26 | A+0x26, T+0x26 |
| `0x41a34d` | `weapon_converge` | d (0 without target) | as left | as left | A, d |

After every sweep and `debris_table` call ECX is the popped slot (the
packet address, &w); after `sfx_stop` it is `0x2d`; after the leg debris
EDX = ECX = A+0x92 (`0x41a290`). The second and third `movement_step`
slots carry the previous calls' high words (the callee reads only the low
word). The eight `transform_point` calls write the probe points and the
recomputed terrain probes through their out pointers; those are the only
early reads of the frame. `transform_point` reads its source dwords and
the pose copy f−0xdc; every `transform_point` output is read after the
call that wrote it.

**Memory order.** `[R+4]` is read for the setup and again in every block
that stores the length and before every `multiply_q10` that scales it.
`[A+0x20e]` is read once (f−8); every region test rereads `[f−8]` and the
word. A+0xe/A+0xc are read after the rate word is updated; R+0x18/R+0x1a
are read-modify-written. `[0x4a0be0]` is reread for every terrain call.
The in-vitro stubs change the region words, R+4, R+0x18/R+0x1a,
A+0xc/A+0xe, the position, A+0xa3/A+0x99/A+0x1a4, the map pointer and
(through the segment) f−0x78 between the calls, and `[A+0x20e]`, which
must not be reread.

**Writes.** The probe dwords and guard bytes `0x49a188`..`0x49a1c8` (once),
A+0x12..A+0x24 (through `euler_matrix`) and the word A+0x32 (1 after it, 0
after every rate change), the position A+0x26/A+0x2a/A+0x2e (integration
and ground clamp), R+0x18/R+0x1a, A+0xc/A+0xe, byte A+0xa4 (a lost nose or
leg region), the packet `0x49a170`..`0x49a187` except `+0xa`, and the
frame.

### Mech-fly frame, packet and residue

**`[0x49a170]` with host frames.** The original stores its segment's stack
address (entry ESP − 0x118) in the packet. Readers: only the sweeps of the
same call, through P+0 (the byte search finds the dword `0x0049a170` only
as twelve operands inside `0x4198f4`; the packet is handed only to the
sweep and to `mech_part_hit`, which reads P+4, P+8, P+0xa and P+0xe). The
replacement keeps the whole frame in the adapter's zeroed 320-byte scratch
(f = scratch + 0x130, every local at its original offset) and stores the
address of that segment: meaningful during the call, dangling afterwards
in both programs, never read before the next call overwrites it. No watch
set of the five suites covers `0x49a158`..`0x49a1cf`. The exact value
would need the frame at the original coordinate (an entry shim) and
changes nothing observable; compare mode is refused.

**Stack and residue (static, no entry shim).** `0x4198f4` reads no local
before it is written (a first-access walk over the body: the only early
reads are the probe points that `transform_point` wrote through its out
pointer). Its direct callees read no uninitialized local of their own:
`euler_matrix`, `transform_point`, `movement_step` (no frame),
`terrain_height`, `multiply_q10`, `dist3d_delta`, `dist_gate` (the same
walk), `sweep` ([weapons.md](weapons.md); its only caller residue,
P+0x14, is written here), `mech_part_hit`, `debris_table`/`debris_burst`,
`weapon_converge` ([input.md](input.md#weapon_converge-41a74c)) and the SOS
calls. With P+0xe = 0 the sweep takes neither the notification nor the
turn path (V50, V64, `0x423430`, `0x41f790`), so no weapon body or
emitter, the known residue readers of the sweep's callees, lies below.
Deeper presentation residue (the notice records under the part hits)
already runs below the host frames of `mech_part_hit` and the hit tests
when those are replaced (section "Stack, frames and residue"). Stack
budget: the member runs at the original coordinate above `mech_move`'s
80-page gap; the replacement's host frames (320-byte scratch, the core,
the bridge) exceed the original's 0x12c bytes by well under one page, and
no callee on the paths above has a gap shim.

**Implementation.** Every game callee goes through the bridge with the
original's registers, so the nested hooks (`euler_matrix`,
`transform_point`, `movement_step`, `terrain_height`, `sweep`,
`multiply_q10`, `mech_part_hit`, `debris_table`, `debris_burst`,
`sfx_stop`, `sfx_rate`, `sfx_refresh`, `dist3d_delta`, `dist_gate`,
`weapon_converge`) keep their modes. Compare mode refused (game calls).

**In vitro** (`differential_mech_fly_test`): original against core, with
logging stubs and with real nested hooks; the statics
`0x49a158`..`0x49a1cf` read from the file (all zero except the effect
table and the packet's `+0xa` dword `0x49a158`). Branch sites (41, all both
ways, none proved): `0x419910`, `0x41993d`, `0x41996a`, `0x419994`,
`0x4199bf` (guards), `0x4199e8`, `0x419b28`, `0x419c0c`, `0x419c6f`,
`0x419c7e`, `0x419c87`, `0x419cf4`, `0x419d15`, `0x419d81`, `0x419d90`,
`0x419d99`, `0x419e06`, `0x419e27`, `0x419e70`, `0x419ec6`, `0x419f0f`,
`0x419f64`, `0x419fd0`, `0x419fdf`, `0x419fea`, `0x419ff5`, `0x41a066`,
`0x41a08d`, `0x41a093`, `0x41a116`, `0x41a124`, `0x41a131`, `0x41a185`,
`0x41a18c`, `0x41a1fd`, `0x41a253`, `0x41a2aa`, `0x41a2b6`, `0x41a2f2`,
`0x41a2ff`, `0x41a329`.

**Live expectations** (map entry probes, not measured with the hook):
`hk_mech_fly_calls` 0/0/0/0/≥1000/0. Relations with the hook replaced:
`…_call_41a74c` = calls; `…_call_48043c` ≥ 5 × calls (plus one per
part-8, part-7, nose and slope block); `…_call_46e180` ≥ calls (the
clamp, plus one per part-8, part-7, nose and slope block); `…_call_47ebb8`
≤ calls; `…_call_467924` = 3 × the calls with A+0xa4 = 0; `…_call_426528`
≤ 5 × calls; `…_call_4188c8` ≤ 6 × calls and ≤ `hk_mech_part_hit_calls`
(map: 6 in c4_08); `…_call_4089bc` = `…_call_408530` (a lost nose or leg
region, at most a few per run); `…_call_47de72` = `…_call_463114` =
`…_call_46297c` (player alive), plus `…_call_462ac4` (player dead), = the
calls with A+0xa3 ≠ 0; `…_call_49288c` ≤ calls. `hk_mech_move_calls` ≥
`hk_mech_fly_calls`.

## Region-damage helpers

Hooks in the adapter group `region_damage` (core
`core/src/region_damage.c`, adapter `proxy/region_damage_hooks.h`, in
vitro `invitro/region_damage_cases.h`). They read and write the
region-damage state (D = `A+0x206` for the MECH and the vehicle, `F+0x200`
for the FLYER; layout in section "Region damage 40da38 and part break
40d434") and draw stream A.

| Hook | VA | Instr. | Callers | Callees | Live (map) |
|---|---|---|---|---|---|
| `region_entry` | `0x40ca58` | 24 | `0x40ca8c` | – | 59/150/5/634/77/178 |
| `region_find` | `0x40ca8c` | 32 | `0x40d434` (`0x40d46f`) | `0x40ca58` | 12/26/3/121/27/38 |
| `region_spread` | `0x40cf44` | 67 | `0x40d434` (`0x40d4c7`), `0x40da38` (`0x40dabc`) | `rng_bounded`, `0x40d3ec` | 20/49/9/220/38/66 |
| `region_absorb` | `0x40d3ec` | 29 | `0x40cf44` (`0x40cfd1`), `0x40da38` (`0x40da71`) | – | 32/88/15/738/46/97 |

No other reference exists (no absolute dword of the four VAs in the image,
no other `call`). All four are cdecl `push ebp; mov ebp,esp` frames
without a runtime record, `enter` or FS:0 access; the hooks relocate the
first five bytes (lengths 1, 2, 1, 1), which no branch targets.

### `0x40d3ec(v, L, d)` `region_absorb`

v → a region word, L a dword whose low word is the limit, d → a damage
word. CX ← word [v] (ECX keeps the entry high word). s16([v]) < 0
(`0x40d400`) → the test. Else ECX = s16([v]) + s16([d]) (32-bit), compared
signed with s16(L) (`0x40d40f`): not below → CX −= L's low word, word [d]
← CX (the overflow), then word [v] ← 0xFFFF (in that order); below → DX ←
word [d] (reread), word [v] += DX (read-modify-write of [v]). Finally EAX
= 1 if s16(word [v]) < 0 (reread), else 0 (`setl al; and eax,1`).

EDX: d, or lo(d, the reread damage word) on the below path. ECX:
lo(entry ECX, [v]) when v was negative; lo(sum, sum − L) after the
overflow; the sum otherwise. No other memory is touched.

### `0x40cf44(R, V, d, T)` `region_spread`

R → a per-part record (`[[D+0xc]] + 18·part`: word count R+6, dword R+8 →
4-byte entries {word weight, word region index}); V → the region words
(`[D]`); d the damage (third argument slot; low word used); T → the limit
words (`[[D+0xc]+4]`). A round (`0x40cf4e`): total word ← 0, ECX ← 0, EDX
← 0, EAX ← [R+8] (reread every round); for each entry e while s16(DX) <
s16(word [R+6]) (the count is reread every step): EBX ← V + 2·s16(word
e+2) (32-bit wrap); s16(word [EBX]) ≥ 0 (`0x40cf6a`) → total += word e+0
(16-bit). Total = 0 (`0x40cf82`) → the end. Else `rng_bounded(0x4d2599,
lo(EAX, total))` (EAX = the entry pointer after the walk, EDX = the step
count, ECX = 0 at the call); CX ← the draw; a second walk from [R+8]
(reread): EBX as before; a live region (`0x40cfac`) subtracts its weight
from CX and the walk stops at the first entry where s16(CX) < 0
(`0x40cfb4`). If no entry stops it (`0x40cfbe` falls through; only with
negative or wrapped weights, or a draw outside the total), EAX points one
entry past the end and EBX is the **last computed** region pointer (of
the last walked entry, or of the first walk when the count fell to ≤ 0 in
between). Then `0x40d3ec(EBX, lo(s16(word e+2), word [T + 2·s16(word
e+2)]), &d)` with EAX = that second dword, EDX = &d (the function's own
argument slot), ECX = T. ECX ← its EAX; CX ≠ 0 (`0x40cfde`) → the next
round, else the end. The end: EAX = 1 if the last total was 0, else 0;
EDX = the step count of the first walk (total 0) or `0x40d3ec`'s EDX;
ECX = the dword f−4 (`pop ecx` at `0x40cff2`): the last total word
above the low word of the entry ECX, which the prologue's `push ecx`
stored there.

In words: the damage goes to a weighted random live region; a region that
breaks passes its overflow on to the next draw; the result says whether no
weighted live region is left. One `rng_bounded` draw per round, before
that round's `0x40d3ec`. Precondition (the original loops forever
otherwise): the rounds end, i.e. a region that `0x40d3ec` reports broken
is not chosen again (true for non-negative weights whose live total stays
below 0x8000). This holds over all shipped part tables: every weight of the
22 DMG tables and the WEAPONS.DAT prefixes is 0..150, the largest weight sum
of a part 161 (`tools/reference-game/scripts/domain_region_spread.py`).

The damage lives only in the argument slot, which only `0x40d3ec` reads
and writes through the pointer. The replacement keeps it in 4 bytes of
adapter scratch (core: `g->scratch`), so the pointer handed to `0x40d3ec`
is another stack address, and the caller's argument slot keeps the pushed
value instead of the final damage word. Both callers pop the slot (`add
esp,10h`) and never read it.

### `0x40ca58(N, k, o)` `region_entry`

N → a 10-byte record {word id, word, word count N+4, dword N+6 → 16-byte
entries with a key word at +6}. EAX ← [N+6] once; for each entry e while
s16(DX) < s16(word [N+4]) (reread every step): word e+6 = the low word of
k (`0x40ca71`) → word [o] ← word [N+0] (the record's id, read then
written), return EAX = e, EDX = the index, ECX = lo(N, id). The end
(`0x40ca83`): EAX = 0, EDX = the step count, ECX = N.

### `0x40ca8c(S, k, o)` `region_find`

S → {word count S+0, dword S+2 → 10-byte records}. EBX ← [S+2] once; for
each record while s16(SI) < s16(word [S]) (reread after every call):
`0x40ca58(EBX, lo(S, k), o)` with EAX = o, EDX = lo(S, k), ECX = the
entry ECX (first call) or the previous call's ECX; a nonzero EAX
(`0x40cab2`) is returned with that call's EDX and ECX. The end
(`0x40cac2`): EAX = 0, EDX = S, ECX = the last call's (the entry ECX
without records).

### Region-helper registers and residue

- **EDX:EAX** are exact (the hook pair) for all four; `region_spread`
  returns `0x40d3ec`'s EDX, which `0x40da38` returns on its "not broken"
  exit to `vehicle_part`, `mech_part_hit` and `flyer_part`.
- **ECX** after a replaced hook is the entry ECX (the dispatch restores
  it). It is dead everywhere: `0x40cf44` overwrites `0x40d3ec`'s
  (`0x40cfd9`); `0x40ca8c` never reads it; `0x40d434` rewrites
  `0x40ca8c`'s at `0x40d48f` or leaves through `0x40d9e5` with it, and
  after `0x40cf44` rewrites it (`0x40d4ee`, `0x40d585`, `0x40d5c4`,
  `0x40d608`) or passes it only to `fire_alloc` `0x46b430` (whose
  `pool_alloc` call keeps it in a `push ecx` slot and whose own `pop ecx`
  drops it) or to `euler_matrix` `0x47ebb8` (sets CL first and never reads
  the upper bits); `0x40da38` rewrites it (`0x40daa5`, `0x40daf2`,
  `0x40db19`) or returns it (AX = 0 after `0x40d3ec` or `0x40cf44`, AX = 1
  after `0x40d434`). After `0x40da38`: `vehicle_part` rewrites ECX and EDX
  at `0x417f08`/`0x417f11`; `mech_part_hit` hands it to `pool_alloc`
  (`push ecx` as its local, `pop ecx` back, never read) and pops ECX after
  that call; `flyer_part` returns ECX = 0 for AX = 0 or part ≠ 0, and on
  the death path passes it only to `cockpit_notice` (writes ECX first), the
  source's slot `+0x60` (`mech_kill` writes ECX before reading it for the
  zero third argument; `actor_kill` is empty) and `death_actions` (`push
  ecx`/`pop ecx`; its callee `0x423f30` writes ECX first), after which
  `trigger_fire` gets ECX = F. Flags are dead after every call (`add
  esp`). **No entry shim.**
- **Locals and residue.** `0x40cf44` has one local dword f−4 (`push ecx`):
  the total word f−2, written first, and the entry ECX's low word, read
  only by the epilogue's `pop ecx`; the other three have none. No entry
  register is read except to pass it on. The callers read no local before
  writing it (`0x40da38`, `0x40d434`: section "Region-break residue"), so
  neither the missing frames nor the argument slot that keeps the pushed
  damage are read.

### Region-helper C entry points and validation

`core/include/es2/core/region_damage.h`: the two leaves
`es2_region_absorb_registers` and `es2_region_entry_registers` work on
guest memory only (no calls); `es2_region_find_registers` and
`es2_region_spread_registers` take an `es2_weapon_graph` and call
`0x40ca58`, `rng_bounded` and `0x40d3ec` through `g->call` with the
registers above (nested hooks keep their modes). All four return EDX:EAX
and the exact ECX through an out pointer. `region_spread` needs
`ES2_REGION_SPREAD_SCRATCH` (4) bytes of `g->scratch` for the damage
slot; on return its low word is the final damage.

**Compare mode.** `region_entry` accepts it: its only store is the record's
word into the third argument when the key is found (EAX the entry pointer
or 0, EDX the index). The other three refuse it (two call game code;
`region_absorb`'s stores are not declared).

**In vitro** (`differential_region_damage_test`): original against core,
installed probes, real callees; the ECX check found the `pop ecx` at
`0x40cff2`. Branch sites (13, all both ways): `region_entry` `0x40ca71`,
`0x40ca83`; `region_find` `0x40cab2`, `0x40cac2`; `region_spread`
`0x40cf6a`, `0x40cf7b`, `0x40cf82`, `0x40cfac`, `0x40cfb4`, `0x40cfbe`,
`0x40cfde`; `region_absorb` `0x40d400`, `0x40d40f`.

**Live.** With all hooks `replaced` all six suites are identical. Calls
a60/c1s/c1c/c3_09/c4_08/c3_10: `region_entry` 59 / 150 / 5 / 634 / 77 /
178, `region_find` 12 / 26 / 3 / 121 / 27 / 38, `region_spread` 20 / 49 /
9 / 220 / 38 / 66, `region_absorb` 32 / 88 / 15 / 738 / 46 / 97, equal to
the map probes; `hk_region_find_call_40ca58` = `hk_region_entry_calls`.
Relations with the hooks replaced: `hk_region_spread_call_492f24` =
`hk_region_spread_call_40d3ec`; `hk_region_absorb_calls` =
`hk_region_spread_call_40d3ec` + `hk_region_hit_calls`;
`hk_region_find_calls` = `hk_region_break_calls`.

## Region damage 40da38 and part break 40d434

Hooks `region_hit` `0x40da38` and `region_break` `0x40d434` in the adapter
group `region_break` (core in `core/src/region_damage.c`, adapter
`proxy/region_break_hooks.h`, in vitro `invitro/region_break_cases.h`, unit
test `core/tests/test_region_break.c`).

| Hook | VA | Instr. | Callers | Live (map) |
|---|---|---|---|---|
| `region_hit` | `0x40da38` | 89 | `vehicle_part` (`0x417f00`), `mech_part_hit` (`0x418ae9`), `flyer_part` (`0x421bdb`) | 16/43/8/549/15/46 |
| `region_break` | `0x40d434` | 329 (reachable) | `0x40da38` (`0x40dae0`, `0x40dafb`) | 12/26/3/121/27/38 |

No absolute dword of either entry exists in the file; these five calls are
all callers. `0x40da38`: `push ebp; mov ebp,esp; add esp,-10h` (relocated
6: 1, 2, 3), no runtime record, no `enter`, no FS:0 access. `0x40d434`:
`push ebp; mov ebp,esp; add esp,-0C4h` (relocated 9: 1, 2, 6), runtime
record of descriptor `0x4988e8` (`[+4]` = −34h, bytes `cc ff ff ff`):
record and saved head at f−34h, context word at f−24h; both exits restore
FS:0 from the saved head.

**Dead code in 0x40d434.** The block `0x40d792`..`0x40d945` after the
unconditional `jmp 0x40d986` (`0x40d78d`) has no branch into it and no
absolute reference: a heap object `0x47749c(2Ah)` with `0x490a34`,
`0x4774f0` and `0x49667c`, the second virtual `+20h` call (`0x40d7f2`),
`pool_alloc([0x4a9642])`, `0x408940` (`0x40d885`), `euler_matrix`,
`0x492990`, `0x4089e4` (`0x40d901`) and `0x47f9a0`. The instruction
count, the DBSIM map's callee list and the branch sites are those of the
reachable body; of the two virtual `+0x20` calls only the node lookup is
live, and `debris_init` and `debris_heading` get no calls from
`0x40d434`.

### Damage state layout

D is the damage state (`A+0x206` for the MECH and the vehicle, `F+0x200`
for the FLYER; built by `damage_state_bind` `0x40d354`):

- D+0 dword → the spread target words (region_spread's V); D+4 dword →
  one word per part (region_absorb's word, −1 once broken); D+8 dword →
  one word per part (nonzero = intact); D+0Ch dword C: `[C]` → the part
  records, `[C+4]` → the limit words T; D+10h dword → the region list
  (region_find's S); D+14h dword → the owner A; D+18h word → the part
  count.
- Part record R = `[C]` + 18·s16(part): +0 word the part's limit, +2 byte
  the debris word (−1 → 2), +3 byte a state index (negative: no fire and
  no state word), +4 byte the parent part (cascade), +5 byte flags (bit 0
  break with cascade, bit 1 fire kind 0 after a fire release, bit 2 the
  large explosion, bit 3 fire kind 2), +6/+8 the spread entries (section
  "Region-damage helpers").
- The region entry e that region_find returns (16 bytes): +6 the key
  word, +8 three words, the part's point (`matrix_point` reads them;
  `fire_create` copies the dword `[e+8]` and the word `[e+0Ch]`).
- The cascade stack Q = `[0x498864]` (6 bytes, allocated once by
  `0x40d354`): +0 dword → words, +5 byte the count; a push has no bound
  check. The word `[0x4a982c]`: cleared by `0x40da38` before a cascade,
  set by `0x40d434` for bit 2; while it is nonzero `0x40d434` creates no
  fire and no explosion (one large explosion per cascade).
- A: +0Ch angles, +12h..+31h the pose (mode byte +24h, translation
  +26h/+2Ah/+2Eh), word +32h the matrix-fresh flag, +34h H the shape
  instance: `[H+4]` L the node tree (virtual `+20h` = the node lookup of
  `col_group`), `[H+8]` → state words, `[H+16h]` → node poses (32 bytes).

### `0x40da38(D, part, damage)` `region_hit`

ESI = part (dword), EBX = D, EDI = R (`[D+0Ch]` and `[[D+0Ch]]` read
once); f−2 ← 0. Entry registers are not read (ECX, EAX, EDX are written
first).

1. `region_absorb([D+4] + 2·s16(part), lo(s16(part), word [R]), &damage)`
   with EAX = the limit dword, EDX = the region pointer, ECX = &damage.
   **&damage is the function's own third argument slot**; region_absorb
   leaves the overflow there. Read order: `[D+0Ch]`, `[[D+0Ch]]`, word
   `[R]`, `[D+4]`.
2. AX = 0 (`0x40da7c`): return EAX = lo(its EAX, 0) (= 0), EDX and ECX
   region_absorb's.
3. Otherwise the damage word is reread from the slot; then `[D]` and
   `[D+0Ch]` once, T = `[[D+0Ch]+4]`, R″ = `[[D+0Ch]]` (reread) +
   18·s16(part): `region_spread(R″, [D], lo([D+0Ch], damage′), T)` with
   EAX = 18·s16(part), EDX = R″, ECX = s16(part).
4. AX = 0 (`0x40dac7`): ECX ← s8(byte `[R+3]`) + 1; return EAX = lo(its
   EAX, 0) (= 0), EDX region_spread's.
5. Byte `[R+5]` bit 0 clear (`0x40dacd`): return EAX = lo(its EAX, 0)
   (= 0), EDX and ECX region_spread's.
6. Cascade: f−2 ← 1, word `[0x4a982c]` ← 0, `region_break(D, part)` (the
   whole part dword) with region_spread's EAX/EDX/ECX. Then, while byte
   `[Q+5]` ≠ 0 (`0x40db15`; Q = `[0x498864]` reread at every test, EDX =
   0 or 1 from `sete dl; and edx,1`): byte `[Q+5]` −= 1, c = that byte
   (reread), x = word `[[Q] + 2·s8(c)]`, `region_break(D, x)`
   (zero-extended) with EAX = `[Q]`, EDX = x, ECX = s8(c). Return EAX =
   lo(Q, 1), EDX = 1, ECX the last region_break's.

AX = 1 exactly when the part broke with bit 0 set (EAX's high word is
then Q's), else EAX = 0 with the real callees (their EAX is 0 or 1).

### `0x40d434(D, part)` `region_break`

EBX = D. Only the low word of part is read. Context word values: 14h/8
around the first fire, 2Ch/20h around the second, 44h/38h around the
explosion. Entry EAX is not read; EDX/ECX only reach `0x49630c`, which
reads EAX and EBP.

1. `0x49630c` (EAX = `0x4988e8`, the entry EDX/ECX). R = `[[D+0Ch]]` +
   18·s16(part) (ESI; both read before the next call).
2. e = `region_find([D+10h], s16(part), f−36h)` (EAX = s16(part), EDX =
   `[D+10h]`, ECX = f−36h); f−3Ch ← e + 8. Word `[[D+8] + 2·s16(part)]`
   = 0 (`0x40d489`): return EAX = 0, EDX = the saved head, FS:0 ← it (ECX
   region_find's).
3. Word `[[D+4] + 2·s16(part)]` ← −1, word `[[D+8] + 2·s16(part)]` ← 0
   (`[D+8]` reread); then `[D]`, `[D+0Ch]`, T = `[[D+0Ch]+4]`, R′ =
   `[[D+0Ch]]` (reread) + 18·s16(part): `region_spread(R′, [D], 7D00h, T)`
   (EAX = R′, EDX = 18·s16(part), ECX = `[D]`).
4. s8(byte `[R+3]`) ≥ 0 (`0x40d4d4`; R from step 1, not R′):
   - word `[0x4a982c]` = 0 (`0x40d4e2`) and byte `[R+5]` bit 1
     (`0x40d4ec`): `fire_release(A)` (`0x46b62c`; A = `[D+14h]`; EAX =
     s8(`[R+3]`) + 1, EDX region_spread's, ECX = A); O =
     `fire_alloc(5Bh)` (`0x46b430`; fire_release's EAX/EDX, ECX = A); O ≠
     0 (`0x40d505`): `fire_create(O, A, lo(5Bh, o), e+8, 0)` (`0x46b48c`;
     A reread; EAX = A, EDX = O, ECX = lo(5Bh, o) with o = word f−36h),
     then `[[0x6cbee4]]` −= 4.
   - Word `[0x4a982c]` = 0, bit 1 clear, bit 3 (`0x40d53d`, the byte
     reread): O = `fire_alloc(5Bh)` (EAX = s8(`[R+3]`) + 1, EDX/ECX
     region_spread's); O ≠ 0 (`0x40d54c`): `fire_create(O, A, lo(5Bh,
     o), e+8, 2)`, counter −= 4.
   - Then (also when `[0x4a982c]` ≠ 0): word `[[H+8] + 2·k]` ← 2 with k
     the low word of s8(byte `[R+3]`) zero-extended (the byte reread),
     A = `[D+14h]` (reread) and H = `[A+34h]`; EAX = H, EDX = k, ECX =
     `[H+8]`.
5. The pose at f−C4h (32 bytes):
   - o = word f−36h < 0 (`0x40d599`): A′ = `[D+14h]`; word `[A′+32h]` = 0
     (`0x40d5a6`): `euler_matrix(A′+0Ch, A′+12h)` (EAX = A′+12h, EDX =
     A′+0Ch, ECX as left: `[H+8]` after step 4, region_spread's ECX
     without it) and the word ← 1. The 20 bytes A′+12h..+25h go to
     f−C4h (`rep movsd`), then `[A′+2Eh]` → f−A8h, `[A′+26h]` → f−B0h,
     `[A′+2Ah]` → f−ACh.
   - o ≥ 0: A = `[D+14h]`, H = `[A+34h]`, L = `[H+4]`; n =
     L.V20(L, lo(A, o)) (EAX = H, EDX = L, ECX = `[L]`). n ≠ 0
     (`0x40d62f`) and word `[n+4]` > −1 (`0x40d636`; the word reread for
     the index): N = `[H+16h]` + 32·s16(`[n+4]`) (ECX = that offset, EAX =
     H); else N = `0x6c56a8` (a static pose; EAX = n, ECX the
     lookup's). A″ = `[D+14h]` (reread); word `[A″+32h]` = 0 (`0x40d65c`):
     `euler_matrix(A″+0Ch, A″+12h)` (ECX as left) and the word ← 1.
     `matrix_compose(N, A″+12h, f−C4h)` (EAX = EDX = N, ECX = f−C4h).
6. `matrix_point(e+8, f−C4h, f−98h)` (`0x4801d4`, the transform with the
   `enter` prologue; EAX = f−C4h, EDX = e+8, ECX = f−98h); word
   `[0x4a982c]` is read after it, and the output replaces the pose's
   translation (f−A8h ← `[f−90h]`, f−B0h ← `[f−98h]`, f−ACh ←
   `[f−94h]`).
7. Word `[0x4a982c]` = 0 (`0x40d6e0`): type f−66h ← 0Ah; byte `[R+5]`
   bit 2 (`0x40d6ec`, reread): word `[0x4a982c]` ← 1, type ← 11h. O =
   `pool_alloc([0x4a961e])` (EAX = `[f−98h]`, EDX = `[f−94h]`, ECX = the
   pool); O ≠ 0 (`0x40d70f`): `explosion_create(O, lo(O, type), f−B0h, A,
   1)` (A = `[D+14h]`; EAX = lo(O, type), EDX = O, ECX = f−B0h), counter
   −= 4.
8. Debris: s8(byte `[R+2]`) = −1 (`0x40d749`): w = 2 at f−68h, Rd =
   `debris_table(f−68h)` (EAX = 0, EDX = f−68h, ECX as left: the counter
   pointer after an explosion, the pool after a failed allocation,
   `[f−90h]` while `[0x4a982c]` ≠ 0); `debris_pick([Rd] + 10·s16(w),
   [Rd+6], f−C4h, 0)` (`0x40d785`), reading `[Rd+6]`, w, `[Rd]` in that
   order (EAX = the table, EDX = s16(w), ECX = 10·s16(w)), then `jmp
   0x40d986`. Otherwise w = s8(byte `[R+2]`) (reread) at f−8Ah,
   `debris_table(f−8Ah)` (EAX = w) and `debris_pick` (`0x40d97e`) with
   EDX = 10·s16(w), ECX = s16(w).
9. Cascade push: for i = 0 while i < s16(word `[D+18h]`) (reread at
   every test): R_i = `[[D+0Ch]]` + 18·s16(i) (both reread); word
   `[[D+8] + 2i]` ≠ 0 (`0x40d9a7`) and s8(byte `[R_i+4]`) = s16(part)
   (`0x40d9b3`): c = byte `[Q+5]` (Q = `[0x498864]`), the byte ← c + 1,
   word `[[Q] + 2·s8(c)]` ← i. Return EAX = 1, EDX = s16(word `[D+18h]`),
   ECX = the saved head, FS:0 ← it.

**Precondition.** region_find writes o only when it finds an entry; after
a miss with a nonzero part word f−3Ch = 8, and `matrix_point` reads the
point at address 8, so the original faults on every path that reads the
unwritten o. Q is not null, and the cascade terminates (the stack holds
the intact children of broken parts).

**RNG.** Neither function draws itself. region_spread draws stream A per
round; the fire, explosion and debris callees draw in their own order
(sound selection on stream B in `fire_create` and `explosion_create`,
stream A in `debris_pick`). The order of the calls is the order of the
steps above.

### Region-break residue

A first-access analysis (a must-write analysis over every EBP frame along
all paths; runtime records and locals written through an escaped pointer
checked by hand) walked the two bodies, their callees and the callers'
later callees with their direct-call subtrees (204 functions):

- **The two bodies.** `0x40da38` reads no local before writing it.
  `0x40d434` reads only o (precondition above), the transform output
  f−98h..f−8Dh (`matrix_point` writes all three dwords on each of its
  three mode paths) and the record head. Byte 19 of the pose (f−B1h) stays
  unwritten on the compose path when `matrix_compose` does not write it;
  its readers only copy it (`rotation_compose` under `debris_piece` when
  the Euler matrix has mode 0), and no arithmetic reads it (`matrix_point`
  reads the words, byte 18 and the translation; `matrix_angles` the
  coefficients).
- **Callees.** The node lookups and `matrix_point` have no locals;
  `0x49630c` writes the record. Of the hooks' originals only `fire_alloc`
  returns an uninitialized local (ECX, popped at `0x40d4ff`/`0x40d546`);
  the audits of the FIRE helpers and [audio.md](audio.md) (the SOS calls
  under `fire_create` and `explosion_create`) agree. No callee depends on
  the bytes that earlier callees leave below `0x40d434`'s ESP, so calling
  them from the host stack changes nothing they compute.
- **After the return.** The three callers are hooks: with every hook
  replaced (the live configuration) they call `0x40da38` through their
  bridges, so the subtree already runs at a host coordinate. Their later
  callees (vehicle_part: `cockpit_notice`, `part_ratio`, the weapon `+68h`
  bodies `0x40ee0c`/`0x40ee38`/`0x40ef6c` with `weapon_eject`, `0x417bec`,
  `removal_append`, the source's `+60h` (`mech_kill` `0x415710` with
  `radio_notice` `0x420a98`, or the empty `actor_kill`), `death_actions`,
  `trigger_fire`, `script_start`, `0x40d9f8`/`0x40cf10`, the recursive
  `+74h`, `0x409904` with `removal_flush` and the FlatObj destructor
  `0x41bd46`; mech_part_hit: `pool_alloc`, `rng_next`, `explosion_create`,
  `debris_table`, `debris_burst`; flyer_part: `cockpit_notice`, the
  source's `+60h`, `death_actions`, `trigger_fire`, `script_start`,
  `sfx_stop`) read stack they did not write only in the event records of
  `cockpit_notice`, `radio_notice` and `trigger_fire` `0x423430` (section
  "Stack, frames and residue"). In an all-original call `cockpit_notice`
  right after `0x40da38` (vehicle_part `0x417f5d`/`0x417f71`) reads
  `0x40da38`'s f−0Ah..f+5 there (its last locals, the saved EBP and half
  the return address). Every other finding of the walk is a runtime record
  head, a local written through an escaped pointer (`weapon_eject`'s
  angles by `aim_angles`, `0x46299c`'s v, the stream objects of the SOS
  and shape loaders) or a path-dependent local written on the reading path
  (`debris_piece`'s angles, vehicle_part's player and count locals).
- **Conclusion: no entry shim.** No simulation reader of the bytes these
  two functions leave below a caller's ESP is known, and none of their
  callees reads uninitialized stack, so the replacements keep their frames
  in adapter scratch and call from the host stack. A shim that keeps the
  original's outgoing coordinate (the `bullet_create` technique) would
  need a guard-probed gap for two nested frames inside the emitters'
  128-KiB gap, where impact → sweep → hit test → part hit → `0x40da38`
  runs; it would buy parity only for the presentation records.

### Region-break registers at the callers

Only EDX:EAX leave a replaced hook (the dispatch returns the entry ECX);
both cores return EDX:EAX exactly and the original's ECX through an out
pointer. `0x40da38`'s ECX is dead at its callers (vehicle_part rewrites
ECX at `0x417f11`; mech_part_hit keeps it only in `pool_alloc`'s `push
ecx` local; flyer_part per section "Region-helper registers and residue").
Its EAX can reach `0x417bec`'s entry when vehicle_part's weapon loop does
not run, and its EDX `pool_alloc`'s entry under mech_part_hit; both are
exact. `0x40d434`'s EAX and EDX are rewritten by `0x40da38` after every
call (`0x40db03`, `sete dl`); its ECX becomes `0x40da38`'s returned ECX
(dead) or is rewritten by `movsx ecx, dl`.

### Region-break implementation and validation

The cores `es2_region_hit_registers` and `es2_region_break_registers`
call every game function through the weapons guest bridge with the
registers above, so the nested hooks keep their modes: `region_hit` calls
`region_absorb`, `region_spread` and `region_break` through it (not the C
entry points of the helpers); `region_break` calls `region_find`,
`region_spread`, the FIRE helpers, `euler_matrix`, `matrix_compose`,
`matrix_point`, `pool_alloc`, `explosion_create`, `debris_table` and
`debris_pick` through it; `0x49630c` and the node lookup (target read from
the node tree's vtable) run as original code. `region_break`'s frame lives
in a zeroed 256-byte adapter scratch (f = scratch + F0h, every local at
its original offset); `0x49630c` gets that synthetic EBP through
`weapons_impact_guest_call`, and the adapter restores FS:0 from the saved
head at f−34h. `region_hit` keeps the damage dword in 4 bytes of scratch
and hands their address to `0x40d3ec`, so the callers' argument slot keeps
the pushed value (popped unread at all three sites). Both hooks refuse
compare mode (game calls). Callee counters: `region_hit` `call_40d3ec`,
`call_40cf44`, `call_40d434`; `region_break` `call_49630c`,
`call_40ca8c`, `call_40cf44`, `call_46b62c`, `call_46b430`,
`call_46b48c`, `call_47ebb8`, `slot_20`, `call_47fa20`, `call_4801d4`,
`call_471b30`, `call_407f1c`, `call_4089bc`, `call_4086bc`.

**In vitro** (`differential_region_break_test`, quick tier; full tier
`differential_region_break_full_test`, 2 shards): per hook 1,001,024
direct cases (100,000 on the quick tier), each run three ways (the
original through an own caller that returns EAX/EDX/ECX, the adapter, the
core with its ECX) against logging stubs for every game callee: ordered
calls with arguments and entry registers (frame addresses by offset from
the record stub's EBP, other stack addresses masked), the slots the
bridge pushed, the runtime record's context word at every call, by
content the point and pose handed to `0x4801d4`, the explosion position,
the debris word and pose (byte 19 left out), the damage word; stub
answers around the tested values (o around −1, misses only for broken
parts, null fires and explosions, node index words around −1, AX zero or
not, stack counts around 80h); pokes of everything the originals reread
(the actor pointer, `[D+0Ch]`, `[D+8]`, `[D+4]`, `[D]`, the record
bytes, the flag word, the shape instance, the counter and pool pointers,
the part count, the stack pointer and list) and of `[D+0Ch]` where the
cached record must be used. The arena spans three 64-KiB regions so the
high words of `lo(R, w)` registers tell their sources apart; globals
(counter and pool pointers, `0x4a982c`, `0x498864`, the debris records,
both RNG streams) and FS:0 are compared after every run. 256 installed
31/a7 poison probes per hook (fast and preserving dispatch) and 256
integration cases with every hook installed. 3,072 real-callee cases per
hook: `region_find`, `region_entry`, `region_spread`, `region_absorb`,
`rng_bounded`, `rng_next`, `euler_matrix`, `matrix_compose`,
`pool_alloc`, `debris_table` (and `region_break` under `region_hit`) in
rotating original/replaced/compare modes over consistent tables, the
real node lookups `0x476850`/`0x47622c` over an own tree and the real
`0x4801d4`; the FIRE helpers, `explosion_create` and `debris_pick` stay
stubs; pair, trace, arena, globals and FS:0 compared (full tier: 1,996
`region_hit` cases with a cascade, 832 `region_break` cases that push
children). Branch sites: 23, all both ways (`region_hit` 4, `region_break`
19; none proved). Six caught mutations
([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).

**Live.** With all hooks `replaced` and the RTL shim installed all six suites
are identical, skipped/bypassed 0. `hk_region_hit_calls` 16 / 43 / 8 / 549 / 15 / 46 (=
`call_40d3ec`) and `hk_region_break_calls` 12 / 26 / 3 / 121 / 27 / 38 (=
`call_49630c`) (a60/c1s/c1c/c3_09/c4_08/c3_10), equal to the map probes.
Relations with every hook replaced:

- `hk_region_hit_call_40d3ec` = `hk_region_hit_calls`;
  `hk_region_hit_call_40d434` = `hk_region_break_calls`;
  `hk_vehicle_part_call_40da38` + `hk_mech_part_hit_call_40da38` +
  `hk_flyer_part_call_40da38` = `hk_region_hit_calls`.
- `hk_region_break_call_49630c` = `…_call_40ca8c` = `hk_region_break_calls`
  = `hk_region_find_calls`; `…_call_40cf44` = `…_call_4801d4` =
  `…_call_4089bc` = `…_call_4086bc` (the intact parts); `…_slot_20` =
  `…_call_47fa20` ≤ `…_call_4801d4`; `…_call_47ebb8` ≤ `…_call_4801d4`;
  `…_call_407f1c` ≤ `…_call_471b30` ≤ `…_call_4801d4`;
  `…_call_46b62c` ≤ `…_call_46b430`, `…_call_46b48c` ≤ `…_call_46b430`.
- `hk_region_spread_calls` = `hk_region_hit_call_40cf44` +
  `hk_region_break_call_40cf44`; `hk_region_absorb_calls` =
  `hk_region_spread_call_40d3ec` + `hk_region_hit_calls`;
  `hk_fire_release_calls` = `hk_region_break_call_46b62c`;
  `hk_fire_alloc_calls` = `hk_region_break_call_46b430` + the
  `base_parts` calls.

## Callee status

How the replacements reach each callee. "Bridge" = through the weapons
guest bridge with the original's registers (a hook there keeps its mode);
"core" = computed in the replacement; "original" = original code behind
the bridge.

| Callee | Called from | Status |
|---|---|---|
| `0x49630c` | `0x405038`, `0x418ba8`, `0x421c8c`, `0x4188c8`, `0x408588`, `0x40d434` | RTL, original (links the record, returns the old FS:0 head; reads no EDX/ECX) |
| `dist_gate` `0x49288c` | hit tests, `mech_fly` | hook, bridge |
| slot `+0x10` `shape_word` `0x46b910` | BASE, FLYER | hook, bridge (virtual; target read from the receiver) |
| `object_query` `0x427da8` | BASE | hook, bridge |
| `euler_matrix`, `matrix_compose`, `transform_point` | all | hooks, bridge |
| `shield_geometry` `0x413cc4` | MECH | hook, bridge |
| `col_query` and below | hit tests | hooks, bridge |
| slot `+0x74` | hit tests, `mech_part_hit` | hooks `building_part`, `vehicle_part`, `flyer_part`, bridge |
| slot `+0x70` | `mech_part_hit` | hook `vehicle_hit`, bridge |
| `pool_alloc`, `rng_next`, `rng_bounded`, `explosion_create`, `object_init` | all | hooks, bridge |
| `debris_table` `0x4089bc` | hit tests, `mech_part_hit`, `mech_fly`, `base_part_debris`, `region_break` | hook, bridge; core in `debris_update` |
| `debris_burst` `0x408530`, `debris_pick` `0x4086bc` and the subtree | hit tests, `mech_part_hit`, `mech_fly`, `region_break` | hooks, bridge |
| `mech_part_hit` `0x4188c8` | MECH hit test, `mech_fly` | hook, bridge |
| `cockpit_notice` `0x420a68` | MECH hit test, `mech_part_hit`, `flyer_part` | hook, bridge |
| `0x434010` (panel jolt) | `mech_part_hit` | original (presentation, clock, stream B) |
| `multiply_q10`, `part_ratio` | `mech_part_hit`, `mech_fly` | hooks, bridge |
| `weapon_by_mount` `0x410670` | `mech_part_hit` | core (`es2_weapon_by_kind`); the hook does not see these calls |
| `weapon_eject` `0x40f57c` | `mech_part_hit` | hook, bridge |
| `list_append` `0x426ac8` | `mech_part_hit` | core (`es2_results_append`); the hook does not see these calls |
| `region_hit` `0x40da38` | `mech_part_hit`, `flyer_part`, `vehicle_part` | hook, bridge |
| `region_break`, `region_absorb`, `region_spread`, `region_find`, `region_entry` | region damage | hooks, bridge |
| `fire_release`, `fire_alloc`, `fire_create` | `region_break` | hooks, bridge |
| shape node lookup (`[[actor+0x34]+4]`.V20; `0x47622c`, `0x476850`) | `col_group`, `region_break` | original |
| `matrix_point` `0x4801d4` | `col_bound`, `region_break` | hook, bridge |
| `sphere_hits` `0x40c524`, `sphere_test`, `dist_octagonal` `0x47de4c` | COL | hooks, bridge |
| memset `0x49681a` | `debris_burst` | RTL, original |
| `rotation_compose` `0x47f058` | `debris_piece` | hook, bridge |
| `matrix_angles`, `debris_heading` | debris | hooks, bridge |
| `death_actions`, `trigger_fire`, `script_start`, slot `+0x60` (`mech_kill`, `actor_kill`) | `flyer_part` | hooks, bridge |
| `sfx_stop` `0x462ac4`, `sfx_rate` `0x463114`, `sfx_refresh` `0x46297c` | `flyer_part`, `mech_fly` | hooks, bridge |
| `movement_step`, `terrain_height`, `sweep`, `dist3d_delta`, `weapon_converge` | `mech_fly` | hooks, bridge |

## Simulation state touched

| Function | Writes (in addition to the hooked callees' own) |
|---|---|
| hit tests | lazy matrix `obj+0x12..0x25` and word `obj+0x32`; the segment length (BASE through `object_query`; the sweep sets `[S+4]` from the result); explosion objects (pool `[0x4a961e]`, counter); RNG A (section "Overview") |
| MECH hit test | bytes `A+0x94`, `A+0x96`, `A+0xb0`; word `A+0x26b`; dword `A+0x288`; shields `A+0x222`/`A+0x224` and the damage copy (through `shield_geometry`); cockpit notice 0x15 |
| `mech_part_hit` | part damage (`vehicle_part`), splash (`vehicle_hit`); region flag `[[A+0x20e]+2·part]`; weapon eject (debris pool `[0x4a9642]`, RNG A); results list `0x4a9e6a`/`0x4a9e6c`; region damage; panel jolt (presentation, stream B, clock); notice 0x12 |
| FLYER hit test, `flyer_part` | region damage, death, script state, bytes F+0x99/F+0x96/F+0x38, dword F+0x2e, word F+6; `[0x4a9660]` set and cleared |
| COL traversal | globals `0x4a980c`..`0x4a9822` (not read after the call) |
| debris burst | debris objects (pool `[0x4a9642]`, counter); RNG A |
| `mech_fly` | statics `0x49a188`..`0x49a1c8`, the packet `0x49a170`..`0x49a187`; A's pose, angles, position, byte A+0xa4; R+0x18/R+0x1a |
| region damage | region words, part words, the cascade stack `[0x498864]`, word `[0x4a982c]`, the shape's state words; fires, explosions and debris through the callees |

## Stack, frames and residue

- **No local read before written** in any function of this document
  except `0x40d434`'s o after a region_find miss (a precondition: the
  original faults there). Locals written through an escaped pointer:
  `object_query`'s out pair, `weapon_eject`'s f−0x34 (by `aim_angles`),
  the probe points of `mech_fly` (by `transform_point`), the transform
  output of `region_break` (by `matrix_point`).
- **Composed-matrix byte 19.** The hit tests' `matrix_compose` outputs
  (f−0x68, f−0x64, f−0x5c), `col_group`'s f−0x24 and `region_break`'s
  pose can keep byte 19 unwritten on non-copy paths
  ([weapons.md](weapons.md#padding-and-transient-packets)). `col_group`'s
  part < 0 path copies the hit test's byte 19 into its own matrix, and
  `shield_geometry` copies the 32 bytes; no arithmetic reads it
  (`matrix_point` dispatches on byte 18). The replacements' scratch is
  zero there. Invisible except to a `mem_watch` of the frame.
- **Frame addresses in globals.** `[0x4a980c]` ← `col_group`'s f−0x24 and
  `[0x49a170]` ← `mech_fly`'s f−0x114 survive the call. Readers: only
  inside the call (`0x4a980c` at `0x40c4a9`, `0x40c4ca`, `0x40c576`,
  `0x40c980`; `0x49a170` only in `0x4198f4` and, through the packet, the
  sweep). The replacements store the address of their own scratch; compare
  mode and a `mem_watch` on these dwords would see it. No oracle watch
  covers `0x4a980c`..`0x4a9823` or `0x49a158`..`0x49a1cf` (the direct
  watches are `0x4a9e20` and `0x4a9e70`).
- **Argument slots.** `region_spread` and `region_hit` keep the damage
  word in adapter scratch; the callers' argument slots keep the pushed
  value and are popped unread.
- **Entry registers.** EDX/ECX reach `0x49630c` (not read). MECH passes the
  entry ECX into `dist_gate` (not read). FLYER's early exit returns the
  entry ECX (dead). `col_group` passes its entry ECX into the node lookup
  (not read). `mech_fly`'s entry ECX reaches `euler_matrix`. `flyer_part`
  returns ECX = 0, made exact by its entry shim; it is the only entry shim
  of these groups.
- **Event-record residue.** `cockpit_notice` `0x420a68` has a 0x18-byte
  frame and writes only the word f−0x18 and the dword f−0x16 of its event
  record, so 18 bytes (f−0x12..f−1) stay unwritten. Slot 0 of the
  presentation port, `0x434e8c`, copies record+2..+0x15 into the cockpit
  message queue: 16 of the unwritten bytes (entry ESP−0x16..−7).
  `radio_notice` `0x420a98` and `trigger_fire` `0x423430` build their
  records the same way (`trigger_fire`'s shim copies the bytes). In the
  original these bytes are stack residue of the caller's depth; the
  reconstruction writes zeros there and the presentation port does not
  read them ([vtables.md](vtables.md), "Event notices and the port";
  [mission_runtime.md](mission_runtime.md)). Simulation state is not
  affected.
- **Stack-residue readers below.** None among the callees behind the
  bridge: the debris subtree, `weapon_eject`, the region subtree,
  `matrix_point`, the node lookups, `0x434010` and the SOS calls read no
  local before writing it.

**The default debris list count `[0x4a9654]`.** Were it ≤ 3, the hit
tests' and `mech_fly`'s debris pick would take the second list
`[0x4a964c]`. Its only writer is `debris_set` `0x40874c`, called at
`0x408907` with the list `0x4a9650` and the name `0x497ec3` "def_deb"; it
reads the count word `[list+4]` from the file through the stream's slot
`+0x18` (`0x4087e9`). A scan of the image finds the absolute `0x4a9654`
only at the read `0x4089c8`, and `0x4a9650` only at the push `0x408903`
and at `0x4089c4`; the other `debris_set` calls fill `0x4a95c0`
("base_deb", `0x4067e1`) and a vehicle's list `edi+0x212` (`0x420483`).
The shipped `DEF_DEB.DAT` has 6 groups
(`tools/reference-game/scripts/domain_default_debris.py`), so the count is
above 3 unless a modified `DEF_DEB.DAT` lowers it.

## Open questions

- Meaning of `[A+0x288]`, `[A+0x26b]`/`[A+0x96]`, `[A+0xa4]` and the
  `0x2b9` record (hypotheses above); which weapon kinds P+0x12 takes.
- The live counts of the hit-test, part-hit, COL, debris and `mech_fly`
  hooks have not been compared with the map probes; their relations above
  are expectations.
