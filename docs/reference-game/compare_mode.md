# Compare mode

Which registered hooks accept `<hook>=compare`, the contract each accepting
hook declares (spans, regions, snapshot regions, comparator), why the
others refuse it, and what a compare-capable variant of a refused hook
would need. The framework is [hook/README.md](../../hook/README.md); the
registry with each hook's Compare flag and live call counts is in
[hooks.md](hooks.md#registry), checked against the adapters by
`compare_audit.py` ([Extraction script](#extraction-script)). Every flag
flip needs its own in-vitro proof.

## Mechanism

In `compare` mode the framework snapshots the declared state (argument
spans and extension regions, at most four (`ES2_HOOK_MAX_SPANS`), 16,384
bytes together (`ES2_HOOK_MAX_STATE_BYTES`), plus optional snapshot
regions), runs the original once on real state, then runs the replacement
on private clones (spans) or on the restored pre-state in place (regions)
and compares EDX:EAX (and x87 state where declared) plus every declared
byte. The caller always gets the original's result and post-state. While
the original runs, nested hooks are bypassed and call their originals;
while the replacement runs, nested hooks keep their own modes.

- **Refusal.** The proxy refuses compare at install time
  (`extension_valid` in `hook/hook.c`) for a hook whose
  `es2_hook_extension` sets `compare_unsupported`, the eighth initializer.
  A hook with a NULL extension (legacy path) or with the flag FALSE or
  omitted installs in compare mode. A refusal rolls back every hook of the
  selection, so a `-Hooks` or `-HookModes` selection must keep refused
  hooks out of compare. The flag is the adapter author's declaration "this function
  changes state outside its spans and regions": the adapters set it for
  every hook whose replacement makes a game call with effects through the
  guest bridge (the replacement phase would execute the callee a second
  time on real state), for allocation and lifecycle code, and for variable
  or runtime-indexed state.
- **Entry registers.** The original phase gets the entry EAX/EDX/ECX and
  the stack arguments, but the trampoline is entered from
  `es2_hook_machine_call` with the framework's EBX, ESI and EDI. The
  dispatch returns the entry ECX in every mode; ECX is not compared.
- **Span clones.** A span is cloned with its prefix (argument + 0 up to
  the span's end), so the replacement reads fields before the span's start
  from the clone. Every argument pointer the comparators do not span is
  checked only by its start address: a call whose other argument starts
  inside a span is skipped, not compared, and a partial overlap would show
  as a mismatch (the caller keeps the original's result). A null span or
  region pointer is not resolved: the call is skipped.
- **One-hop regions.** An extension region can be an RVA range or a range
  behind a pointer argument plus offset (one hop, in place). Multi-hop
  regions must be read-only, so stores two pointers away cannot be
  declared.
- **Comparators** are adapter functions in the groups' `*_hooks.h`
  (`es2_hook_comparison` exposes both EDX values). They remap a returned
  address of a cloned argument to the clone ("EAX relative to the span"),
  restrict the comparison to AX or DX where the callers read only the low
  word, or accept two stack addresses for a dead frame-address register
  ([Noise classes](#noise-classes-and-known-differences)). Without a custom
  comparator both registers are compared directly.
- **Depth limit.** A compare hook entered at hook nesting depth ≥
  `ES2_HOOK_MAX_COMPARE_DEPTH` (8) runs the original only and counts
  `skipped`. With the buffers off the stack this is a policy, not a stack
  budget; raising it changes which live calls are compared and is a
  decision of its own ([Nesting depth](#nesting-depth-and-live-skips)).
- **Buffers.** `dispatch_extended_compare` keeps its span/region buffers
  in a static set per compare level (8 × 64 KiB of BSS); a compare level
  costs about 1.4 KiB of host stack (fixture `hook_snapshot_test`, part 9).
  The legacy dispatcher (NULL-extension hooks, all leaves) keeps its
  16 KiB stack buffer, because it is reserved on replaced calls too
  ([hook/README.md](../../hook/README.md#stack-use)).
- **Counters.** Per hook `calls`, `mismatches`, `skipped` and `bypassed`
  (`hk_<h>_calls` and so on in the oracle output). `bypassed` counts calls
  inside the original phase of a compared caller. Compared calls are calls
  minus skipped minus bypassed. Mismatches are logged as JSONL records
  ([hook/README.md](../../hook/README.md#configuration-counters-and-logs)).

## Compare contract

Compare is sound for a hook when both phases see the same inputs
(arguments, entry EAX/EDX/ECX, the memory it reads), the function writes
nothing outside its declared spans and regions, and every callee its
replacement reaches through the guest bridge stores nothing outside the
declared bytes (the nested hook then runs again in the replacement phase
in its own mode; in the original phase it is bypassed). Results are
compared where the callers read them. Readers need nothing: reads of real
state are the same in both phases because the original runs on real state
first. A replacement's guest call whose pointer arguments are cloned spans
(the CW3220 memset `49681a` in `set_length` and `script_start`) acts on the
clone. Every contract below was checked against the `llvm-objdump -d -M intel`
listing of the reference `VER95/DBSIM.EXE` (functions, callees and
call sites) and the adapters.

- **Lazy matrix caches.** `object_query` and the formation slots write a
  lazy matrix cache (word B+32h ← 1,
  B+12h..B+24h through `euler_matrix`) in objects reached through
  pointers (list entries; the group leader, three pointers from A), which
  no span or region can declare. They are still sound: the fill is
  idempotent and the original phase runs first, so the replacement phase
  finds the flag set and reads the same matrix. Only a replacement that
  diverged and visited an object the original skipped could fill that
  object's cache early (the same values, earlier than the game would); the
  comparison of the result usually reports such a divergence.
- **Exit shims hide ECX.** A hook whose exit shim hands the caller the ECX
  computed by the replacement phase cannot compare: no comparator sees
  ECX ([Why refused hooks refuse](#why-refused-hooks-refuse)).
- **Faults.** Inputs that fault (`base_condition` with summed maxima 0, or
  80000000h / −1) leave through the detour in the original phase, as they
  do through the replacement in replaced mode; that is outside the
  framework contract in both modes.
- **Aliasing at the call sites.** The live callers pass disjoint objects:
  `rotate_2d`'s 25 sites (and the wrapper 488ddd's four callers) use
  separate frame locals or an object and a local; the seven formation
  sites pass a frame-local P; `line_plane`'s caller `contact` passes the
  global 6b505c as out and its own frame for plane and segment (another
  call site of `line_plane` is 47e73c);
  `object_query`'s callers pass a frame list and record; `key_axes` gets
  the block 4d23c1 next to its region; the load's script records are
  disjoint from the actor, region and timer objects; the weapon is
  disjoint from its record or output word.

## Snapshot regions

For hot simulation hooks whose state is too large for four spans (an
object pool, the actor arena), a snapshot set (fixed image range, or a
resolver callback for runtime base and size such as a pool block) is bound
to an extension with `es2_hook_bind_snapshot` (`hook/hook.c`, interface
`hook/hook.h`, details and fixture list in
[hook/README.md](../../hook/README.md#snapshot-regions)). It is a binding,
not an extension member, because the adapter extensions are positional
initializers and `-Wmissing-field-initializers` is an error.

**In place, not a copy at other addresses.** A replacement reads guest
memory natively (`math_native_memory` dereferences the guest address) and
calls game code through the guest bridge; that original code follows the
pointers stored in the objects (actor list, groups, vtables, the actors in
each other's contact tables). A copy at other addresses would need every
read of the replacement remapped and every embedded pointer in the copy
rewritten for the original callees; the second is not feasible. The
replacement therefore runs at the real addresses on the restored
pre-state, and neither the cores nor the adapters change.

**Per compared call:** copy S0, run the original, keep S1 for the 256-byte
chunks the original changed and restore S0 there, run the replacement,
compare every chunk with S1/S0 and restore S1. Snapshot differences are
mismatches that a comparator cannot hide; the log reports span
`span_count + region_count + i` with the offset in region i.

- **Guards** (hooked functions with effects outside the regions) skip the
  replacement phase when the original phase entered one; a guard must be
  installed (not `original`). A snapshot set needs at least one region.
- **Buffers:** `levels` sets of 2 × the declared region sizes per
  compare-mode install (`VirtualAlloc`, outside the stack); a deeper
  re-entry of the same hook is skipped (exhausted), not a crash. Optional
  sampling compares one call in N.
- **Cost:** memory 2 × (sum of the rounded maximum region sizes) per level
  and hook. Time: one copy and two vectorized compares of the actual
  region bytes per compared call, about 0.3 µs per KiB (fixture part 10:
  40 µs for 128 KiB against 0.34 µs for a replaced call; the CRT `memcmp`
  compares bytewise and took three times as long, so the framework uses
  its own chunk comparison). A whole-DATA region (2.2 MiB) would cost about
  0.7 ms per call and would also revert stores of other threads; no hook
  uses it.
- **Contract:** single simulation thread (no other thread writes a region
  during a compared call); stores of the reach outside the regions are
  guarded or overwrite scratch with values independent of its pre-state,
  so a correct replacement phase leaves them as the original did.

### contact_scan 4128f8

The only hook with snapshot regions. Its reach (direct-call walk of the
listing from 4128f8 and the targets of the virtuals +24h, +3Ch, +44h, +48h
of all classes, stopped at the guards) writes:

| Store | Where | Treatment |
| --- | --- | --- |
| actor bytes +95h, +9Eh, contact tables +C2h, +132h, timer word +1E3h (`contact_scan`, `contact_share`, `sight_check`) | actor objects, all in the blocks of the pools `[4a95a0]` (buildings and vehicles), `[4a9b7a]` (MECH), `[4a9db9]` (FLYER) | snapshot regions: resolver per pool (header +0 free, +2 used, +4 object size, +6 block; (free + used) × (size + 8) bytes, 471a50/471ad8; absent when the header or block pointer is 0; at most 256 KiB) |
| RNG stream A 4d2599h, 72h bytes (`rng_bounded` in `sight_check`) | DATA | fixed snapshot region RVA d2599h |
| terrain contact 6b505c..6b5067h, byte 4a0c4c (470798 under `terrain_line`) | DATA | not declared: written from the call's own inputs (the add-shift follows 47e610's store in the same call; the byte only goes 0 → 1) |
| BASE V24 buffer 4a95cc (20h bytes) and its first-call byte 497424 (403548) | DATA | not declared: overwritten from the object |
| `mech_contact` 412800 (MECH V48): radio message 420a98, message timer 4a9b64, table 4a9b00 | DATA, cockpit queue (heap), clock | guard (RVA 12800h) |
| `trigger_fire` 423430 (from `contact_scan` and `contact_share`): latch, variables 4a9e70, message port, clock | trigger records, DATA, heap, clock | guard (RVA 23430h) |

The shape slot +20h reached from the MECH V24 417b98 is a node lookup
([vtables.md](../reverse-engineering/vtables.md)) and assumed not to
store; V3c/V44 are readers; `dist_gate`, `heading_point` and the terrain
walk below `terrain_line` store only into their frames and the scratch
above. Soundness: the pools and the stream are restored, so both phases
see the same inputs; a correct replacement repeats the original's call
sequence, so the undeclared scratch ends with the original's values; a
call that reaches a message, clock or trigger path is not compared.
Assumptions: no other thread writes the pools or stream A; every actor in
the list lies in one of the three pool blocks (the list registration
411dd4 has seven direct callers, 40543e … 40591c in the factory 405314,
415c24 in the MECH constructor and 421656 in the FLYER constructor, each
on an object just allocated from its pool:
[vtables.md](../reverse-engineering/vtables.md), section 1.2). `bypassed`
stays 0 (the caller `actor_pass` is replaced and never in an original
phase). Memory: 1.5 MiB per level (three pools of at most 256 KiB, the
stream). The live pools are small (the building pool holds at most 140
objects of 26Dh + 8 bytes, 86 KiB; MECH objects 36Ah + 8, FLYER 291h + 8),
so a compared call costs tens of microseconds (estimate from the fixture
throughput). The adapter is `tools/reference-game/proxy/actor_pass_hooks.h`.

## Contracts of the accepting hooks

The hooks of the four all-compare groups (`rng`, `geometry`, `timing`,
`movement`) and the bounded leaves of `math`, `damage`, `weapons` and the
other groups that accept compare without a declaration are classified in
[Per-hook declarations](#per-hook-declarations). The tables below give the
contract of every hook that needed a span, a region, a comparator or a
callee argument.

### Flag-level leaves

| Hook | Writes | Callees of the replacement | Declaration |
| --- | --- | --- | --- |
| `route_point` 423b0c | none | none | flag |
| `order_target` 423918 | none | none | flag |
| `bounds_outside` 41373c | none | none | flag |
| `bounds_check` 41379c | none | `bounds_outside` (pair contract) | flag |
| `target_known` 411c58 | its local EBP−4 only (written before read) | `dist_gate` (pair contract with the Z-sign EDX) | flag |
| `region_test` 4233a4 | none | `dist2d_point` (legacy hook, EAX contract) | flag; comparator on EAX only |
| `ground_height` 427d80 | none | `terrain_height` (pair contract) | flag |
| `weapon_range` 40e5f8 | none | none; the entry shim loads ECX = [W+1Ch] for both phases (the original ignores its entry ECX) | flag |
| `mech_target_scale` 415498 | none | `multiply_q10` (pair contract); on the other path EDX is the entry EDX, the same in both phases | flag |
| `mech_armour` 4154d0 | none | none | flag |
| `base_condition` 4052b4 | its local EBP−4 only | none | flag |
| `point_offset` 4929fc | P[0..7] (adds the two offsets in place; the original reaches `heading_cos` through 492b10/492b31, bypassed in the original phase; the core computes the cosines) | none | span {P, 0, 8}; comparator: bytes, EAX = P remapped to the clone, EDX equal |
| `fire_permit` 41c3c8 | byte A+96h | none | span {A, 96h, 1Dh} (the clone's prefix holds A+45h, A+97h and A+0B2h, which it reads; the leader byte is read through real pointers); comparator: bytes, EAX = EDX = A+92h remapped |

None of the thirteen reads the caller's EBX, ESI, EDI or EBP (the
prologues only save them). `point_offset` and `fire_permit` are writers,
not pure readers. `region_test`'s EDX on the distance path is
`dist2d_point`'s EDX, which the legacy `dist2d_point` hook does not
reproduce (in vitro its compare mismatched exactly when `dist2d_point` was
replaced), hence the EAX-only comparator.

### Spans and comparators

| Hook | Writes (from the listing) | Callees of the replacement | Declaration |
| --- | --- | --- | --- |
| `sincos_pair` 47eb30 | m+0..m+7 (order +0, +6, +2, +4) | none (the cosine lookups are computed in core) | span {m, 0, 8}; default comparison (EAX = −s, EDX the CDQ of the second index) |
| `rotate_2d` 47ffe8 | out+0..out+7 | none | span {out, 0, 8}; comparator: bytes, EAX = out remapped, EDX |
| `line_plane` 47e610 | out+0..out+0Bh, only on a hit | none (the 64-bit helpers are computed in core) | span {out, 0, 12}; comparator: miss EAX = 0 both and EDX equal; hit EAX = (out & FFFFFF00h) \| 1 in each phase and both EDX inside the current thread's stack (TIB StackLimit..StackBase; EBP−28h of the original, the adapter's frame) |
| `object_query` 427da8 | out+0..+7, P+4 (segment length), `[4aaad0]`; for each object whose word B+32h is 0, B+32h ← 1 and B+12h..B+24h through `euler_matrix` | `dist_gate`, `transform_point`, `euler_matrix`, `matrix_transpose`, `rotate_point`, `grid_sweep` on the replacement's scratch frame | spans {out, 0, 8}, {P, 0, 2Ah} (the prefix holds the pose pointer, the radius and the inverse pose it reads); region RVA aaad0 (4 bytes, in place); comparator: bytes and AX (both callers test AX only; the high halves can be callee or frame residue) |
| `grid_sweep` 4273c8 | out+4..out+0Fh on a hit | `set_length`, `dist3d_delta`, `cell` on its own scratch | span {out, 4, 12}; comparator: bytes, EDX, AX, and the high word of EAX equal or the high words of out+4 and its clone (EAX = ((out+4) & FFFF0000h) \| 1 on a hit) |
| `set_length` 4927f0 | V+0..V+0Bh (or the memset of V when its length is 0) | the CW3220 memset 49681a on the clone (its EDX is the count, 12) | span {V, 0, 12}; comparator: bytes, EAX = V remapped, EDX |
| `convoy_steer` 46a89c | B+0Ch..B+11h (dword +0Ch rewritten, word +10h), word B+32h, and through its callees the approach word B+220h and the timer word B+223h (`timer_word_step` 467a48 steps [arg+1]); the speed slot it changes is a register (EDI) | `timer_word_step`, `approach_word`, `multiply_q8` | span {B, 0Ch, 21Ch} (B+0Ch..B+227h; the reverse byte B+227h is read); comparator: bytes, EAX = B+0Ch remapped, EDX |
| `slope_speed` 41693c | A+12h..A+24h (through `euler_matrix`) and word A+32h when that word is 0; the speed word | `terrain_face` (pure), `euler_matrix` on the clone | spans {A, 12h, 22h} (A+12h..A+33h; the prefix holds the angles A+0Ch and the position A+26h it hands on) and {speed, 0, 2}; comparator: bytes, EDX, and EAX equal or both EAX inside the current thread's stack (the face-0 path returns the frame address EBP−0Ch; the other paths return 0 or a small value; EDX is a value on every path) |
| `key_axes` 45a4cc | block B+0..B+13h, seen array 4d23b2.., down array 4d2394.. | none | span {B, 0, 14h}; one region RVA d2394, 2Dh bytes (4d2394..4d23c0: down, released, seen; the released array is only read), in place; default comparison (EAX = 0Fh, EDX = 4d23a3 relocated) |
| `formation_base` 405c04 | static buffer 4975d6..4975e2 (with its first-call byte), P+0..P+0Bh, the leader's cache (word L+32h, L+12h..L+24h through `euler_matrix`, L = [[[A+45h]+0Ch]]) | `euler_matrix` (only when L+32h = 0), `rotate_2d` into its scratch | span {P, 0, 12}; region RVA 975d6, 13 bytes; default comparison |
| `formation_mech` 417898 | static buffer 49a430..49a436, P, the leader's cache | as `formation_base` | span {P, 0, 12}; region RVA 9a430, 7 bytes; default comparison |
| `formation_flyer` 421e98 | P, the leader's cache (423044 writes nothing) | as `formation_base` | span {P, 0, 12}; default comparison |

### Group leaves with spans

None of these six calls game code; three are writers. The results are
read from real memory (no span pointer is returned), so no comparator
remaps an address. An argument value that happens to point into a span's
extent makes the framework skip the comparison (`region_find` hands
`region_entry` EDX with only its low word set to the key), which costs a
compared call, not correctness.

| Hook | Stores (from the listing) | Result | Declaration |
| --- | --- | --- | --- |
| `target_class` 411cb4 | none | EAX = (A & FFFF0000h) \| 2 or \| 1, else 0; EDX = [A+4Dh] + 8 or the entry EDX | flag; default comparison |
| `engage_end` 41c478 | word A+23Eh ← 0 when it is 4 and [A+1A4h] = [A+248h] | EAX = 1, EDX = [A+1A4h] or the entry EDX | span {A, 23Eh, 0Eh} (the clone's prefix holds A+1A4h; the span reaches A+248h..A+24Bh, which it reads); default comparison |
| `debris_table` 4089bc | word [P] −= [4a9654h] when the count is not above it | EAX = 4a9650h or [4a964ch], EDX = the sign-extended count | span {P, 0, 2}; default comparison |
| `weapon_record` 40fe84 | none | EAX = i × 58h + [4a98ech], EDX = i sign-extended | flag; default comparison |
| `region_entry` 40ca58 | word [out] ← [R] when the key is found | EAX = the matching entry ([R+6] + 10h × k) or 0, EDX = k | span {out, 0, 2}; default comparison |
| `def_lookup` 40ffc8 | none | EAX = the record of [4a98fch] (24h bytes each, [4a9900h] of them) or 0, EDX = the index | flag; default comparison |

### Mission-load, weapon-slot and weapon-class leaves

| Hook | Stores (from the listing) | Result | Declaration |
| --- | --- | --- | --- |
| `pool_nth` 471cb0 | its own argument slot (the index word, decremented in place) | pool_prev's EAX; EDX with the counted word | flag; default comparison |
| `subject_resolve` 425348 | none | scope 0: 122 × index + [4a9e00h]; scopes 1..3: pool_nth's pair | flag; default comparison |
| `action_copy` 411b90 | A+1BAh..1CDh and A+1CEh..1E1h, interleaved | EAX = A+1CEh, EDX = ids+20 | span {A, 1BAh, 28h}; comparator: bytes, EDX equal, EAX relative to the span |
| `condition_init` 415068 | table 1 = [C+4] words 0..28, table 2 = [C] words 0..21 (both pointers read once at entry); its argument slots and locals | EAX the last quotient, EDX the remainder or the list pointer [T+4] (real memory) | two one-hop regions (argument 0, pointer offsets 4 and 0, 3Ah and 2Ch bytes, in place); default comparison |
| `region_init` 423358 | R+0..9 | EAX = R, EDX a product | span {R, 0, 10}; comparator: bytes, EDX equal, EAX relative |
| `timer_init` 423104 | [T], T+4..2Bh, and through timer_arm the dword T+2Dh | EAX = words+40, EDX = T+2Ch | span {T, 0, 31h} (timer_arm runs on the clone); comparator: bytes, EAX equal, EDX relative |
| `latch_set` 410f04 | byte W+14h | EAX = W+14h | span {W, 14h, 1}; comparator: bytes, EDX equal, EAX relative |
| `gauge_mode` 441f90 | word H+0B1h | EAX = H, EDX = hi(e) \| m | span {H, 0B1h, 2}; same comparator |
| `beam_ready` 40ecdc | none | EAX 0/1; EDX's high word on one path the caller's EBX's | flag; comparator: EAX and DX |
| `elf_trigger` 40e680 | bytes W+47h, W+48h, word W+84h, the frame word [[W+14h]] (only when [W+10h] ≠ 0) | EAX = EDX (0 or the request byte) | span {W, 47h, 3Fh}, one-hop region {argument 0, pointer offset 14h, 2 bytes}; default comparison |
| `beam_adjust` 40f48c | word W+7Bh (twice), bytes W+34h, W+3Ch | EAX = W+3Bh, EDX a value | span {W, 34h, 49h}; comparator: bytes, EDX equal, EAX relative |
| `beam_preset` 40f4d8 | word W+7Bh when byte W+49h = 0 | EAX = W, EDX = W+43h | span {W, 43h, 3Ah}; comparator: bytes, EAX and EDX relative |
| `beam_fill` 40f4f0 | byte W+43h, word W+7Bh | as `beam_preset` | as `beam_preset` |
| `beam_drain` 4116ad | word W+7Bh, bytes W+43h, W+34h, W+3Ch | EAX = W+3Bh, EDX = W+43h | span {W, 34h, 49h}; comparator: bytes, EAX and EDX relative |
| `gun_clear` 4115f8 | word W+7Bh | EAX = W | span {W, 7Bh, 2}; comparator: bytes, EDX equal, EAX relative |
| `beam_clear` 4116cf | words W+7Dh, W+7Bh, bytes W+34h, W+3Ch | EAX = W+3Bh, EDX = 0 | span {W, 34h, 4Bh}; comparator: bytes, EDX equal, EAX relative |
| `weapon_ammo` 40e62c | word [p] when p ≠ 0 | EAX = lo(p, 5) | span {p, 0, 2}; comparator: bytes, EDX equal, AX equal and EAX's high word equal or p's in each phase |
| `gun_ammo` 40e644 | word [p] when p ≠ 0 (0, or W+7Bh's word) | EAX = lo(p, 5) or lo(T, word T+2), EDX = p | span {p, 0, 2}; comparator: as `weapon_ammo`, EDX relative |

The span prefix holds W+10h, W+14h and [W+1Ch], which the replacement
reads from the clone where the span starts later. Skipped, not compared:
`weapon_ammo`/`gun_ammo` with p = 0, `elf_trigger` with [W+14h] = 0,
`condition_init` when its two tables overlap each other.

**pool_nth and subject_resolve.** `pool_prev`/`pool_next` store nothing
on the path the live pools take (heap nodes above 1 MiB); the debug report
of `pool_next` (sprintf into the original's 32-byte stack buffer, see
[main_loop.md](../reverse-engineering/main_loop.md)) needs a node below
1 MiB, which no live pool has. The replacement phase therefore re-runs
only reads. The two iterators themselves refuse compare; flipping them is
the same argument plus the report branch.

**Weapon-slot reach.** `fire_group` calls `gauge_mode` 441f90 and
`group_toggle` calls `member_toggle` 410cd0 through the guest bridge, so
those calls land in the callee hooks in their own modes. `member_toggle`
and `gauge_mode` are reached in the live regression only by `c1_01-keys`
(Alt+1/Alt+2 through `group_toggle`;
[weapons.md](../reverse-engineering/weapons.md#player-weapon-system)).

### Leaves classified with their adapters

The other accepting hooks (for example `throttle_record`, `hud_record`,
the leaves of the groups `manager_slots`, `actor_leaves`,
`weapon_methods`, `bridged_leaves`, `sim_leaves` and `mfd_keys`, and the
assets hooks, whose comparators describe the loaded object) carry their
declaration in their adapter comments (`tools/reference-game/proxy/*_hooks.h`);
those beyond "flag only" are in
[Per-hook declarations](#per-hook-declarations).

## Why refused hooks refuse

**`condition_ratio` 4152ec.** For kinds outside 0..2 the original
dereferences its caller's ECX and EBX as word pointers
([vehicle.md](../reverse-engineering/vehicle.md#condition_ratio-4152ec)).
The entry shim copies EBX into EAX for the replacement, but the original
phase is entered with EBX = the trampoline address, so a compared call
with such a kind would hand the caller a wrong original result. Both live
callers (432a03, 4342f8 through 41b514) push kind 0, so the path is not
live; making the hook compare-capable needs an entry-EBX contract for the
original phase (framework change) or accepting that proof. Open.

**Exit shims with the replacement's ECX.** `weapon_select` (its live ECX
comes from the exit shim, i.e. from the replacement phase), `link_toggle`,
`gauge_sync`, `gauge_state`, `gauge_link`, `gauge_button` and
`member_toggle` hand the caller the ECX of the replacement phase, which no
comparator sees; they need ECX in `es2_hook_comparison` or an
original-phase ECX for the shim (framework). `gauge_sync`, `gauge_link`
and `gauge_button` also call the lamp and the link toggle.

**`throttle_gauge`** calls the slider class-table slot +14h, which reaches
`slider_event`, `slider_base`, the ThrottleGauge handler and the lever word
49a06e on a set-point change; a snapshot guard on `slider_event` would
skip exactly those calls, but a snapshot set needs at least one region.

**Game calls with effects.** Every hook of `hdd_orders` (`map_areas`,
`map_mode`, `wingman_select`, `order_cancel`), `fire` (`fire_alloc`,
`fire_create`, `fire_release`), `results_writer` (except `list_append`),
`panel_weapons` (`weapon_find`, `panel_weapon`) and `destructors` (except
`shape_base_dtor`) calls game code (labels, lamps, cursor, pool, removal
list, heap free, shape virtual +18h, the FS:0 record). `list_append` has a
runtime-indexed store; `shape_base_dtor` frees heap on flag bit 0 and with
flag 0 stores through the pointer [6cbee4h]. Heap and load-time pieces
(`pool_create`, `group_array`, `initial_damage`, the `mission_pools`,
`mission_weapons` and `mission_ctors` hooks), the stage-5 flushes,
`hdd_key`, `r_key_toggle`, `laew_trigger` and the three weapon steps
refuse for allocation and game calls with effects.

**Files.** The config readers (`keyjoy_load`, `sound_config`,
`prefs_load`, `prefs_video`, `prefs_store`, `tape_save`, `tape_restore`)
read or write files and call the prefs callbacks, the profile API or the
stream classes; a second phase would repeat them, and their in-vitro byte
comparison stays the check.

**Mission load.** In-place compare cannot cover the load as a whole (the
argument in [mission_load.md](../reverse-engineering/mission_load.md));
only its six leaves above accept it.

### Refusal classes

A hook can have several; the per-hook codes are in the evidence table of
[Per-hook declarations](#per-hook-declarations).

| Code | Class |
| --- | --- |
| G | game calls: the replacement calls game code (unhooked functions, or hooks in their own modes) through the guest bridge, so the replacement phase would run them again on real state |
| V | virtual or member calls into arbitrary object graphs (vtable slots, AI state members) |
| I | I/O or presentation: sound, message posts, HUD/display calls, window messages, files, input tape |
| A | allocation and lifecycle: pool links, constructors, explosion/debris creation, the runtime frame record `49630c`/FS:0 |
| O | outputs behind argument pointers or fixed globals that are not declared as spans/regions (some may alias inputs) |
| R | nested state writers: RNG streams A/B (`4d2599`/`4d260b`, 72h bytes each), timer and approach words |
| S | stack contract: an entry shim keeps the original stack coordinate or a residue byte, or (`condition_ratio`) the original reads a caller register the framework does not hand to the original phase |
| U | uniformity only: pure leaf or reader whose only callees are pure; nothing to isolate |
| X | variable or runtime-indexed state beyond four fixed spans (track/node graphs, light entries, tables, lists) |
| F | a result register holds a host frame/scratch address or path residue that is dead at the callers; the default comparison would report it |

### Effort

| Effort | Meaning |
| --- | --- |
| S | flip the flag and declare spans/regions (or a comparator for a dead register); an in-vitro compare case and a mutation; no framework change |
| M | as S, but a callee must become compare-capable first, one callee must be replayed or proven pure, or a small framework feature (runtime-indexed region, pointer remap, entry register for the original phase) is needed |
| L | needs a game-call replay log or a graph/whole-DATA snapshot (options 4 and 5 below) |
| XL | needs the whole-DATA snapshot **and** a diversion of sound/pool/I/O effects; reaches most of the simulation |
| n/a | compare is not meaningful (presentation, platform, input or sound I/O); the replaced-vs-original oracle run stays the check |

## What a compare-capable variant needs

From cheap to expensive.

1. **Flag flip with spans/regions (S).** Set `compare_unsupported` FALSE
   and declare every byte the function writes: argument spans or
   fixed-RVA regions (`mount_base`'s vector `4a9af0` and guard byte
   `498f34`, `sphere_test`'s `[4a9810]`, the RNG streams). Where EAX/EDX
   return the address (or an address high word) of a cloned argument, the
   comparator remaps it (as `math_track_state_equal` and the timing hooks
   do). The claim "nothing else is written" is proven in vitro: one
   compare-mode case set and one mutation per flipped hook.
2. **Comparator for dead residue registers (S).** `edge_test`,
   `sphere_hits` and `shield_geometry` return a host frame address or
   path residue in EDX that no caller reads; a comparator that checks EAX
   (or AX) and the declared bytes and ignores the documented dead register
   does ([Noise classes](#noise-classes-and-known-differences)).
3. **Callee first (M).** A hook whose callees are all compare-capable and
   whose own writes are declared becomes cheap once its callees are: the
   replacement phase then re-runs only side-effect-free callees
   (`actor_safe` after the objective leaves, `mount_offset` after
   `mount_base`, `skirt_check` and `engage_done` after
   `script_start`/`target_class`/`order_target`, `segment` after its six
   helpers).
4. **Game-call replay log (L, framework extension, not implemented).**
   For functions whose game calls have effects the function itself does
   not read back (sound `4628e0`/`462830`/`462ac4`, message posts
   `420a68`/`420a98`/`433158`, the HUD update `43d9b4`, the gauge setter
   `443880`): record each callee entry (target, stack arguments,
   EAX/EDX/ECX) and its EDX:EAX/ECX in the original phase, then answer the
   replacement's guest calls from the log without executing them and count
   a mismatch when target, arguments or order differ. In the original
   phase the original calls unhooked callees directly, not through the
   bridge, so recording needs temporary pass-through probes on those
   callee entries (the entry-probe engine of the DBSIM map is the model).
   It does not help when the function reads back what a callee wrote (most
   AI and damage code), because the replacement would see the original's
   post-state.
5. **Snapshot regions (L/XL).** Implemented as
   [snapshot regions](#snapshot-regions) (a pool block, a fixed range)
   with guards for the effects outside them. DBSIM's `DATA` section is at
   RVA `97000h` with virtual size `235000h` (2.2 MiB, of which `10C00h`
   bytes are initialized; PE header of the reference `VER95/DBSIM.EXE`); a whole-DATA region is possible but not used
   (cost and other threads' stores, see above). Not covered: heap objects
   outside a declared region, and effects outside the process image
   (sound, DirectDraw, files), which would run twice unless diverted (the
   XL rows).
6. **Not meaningful (n/a).** `poly_spans` (draws), `player_input`,
   `death_sequence`, `key_message`, `control_record`, `mouse_dispatch`,
   `results_writer`: presentation, platform, input and file I/O; the nine
   `sfx` hooks (sound output through the SFX manager, which a second phase
   would play twice). The replaced-mode oracle regression and the in-vitro
   harness stay their checks.

## Candidates

Compare localises a live mismatch to the first differing call with its
arguments, instead of an end-of-run tick difference that needs
`bisect_hooks.py`, and checks leaves called from many contexts against
live inputs that in-vitro sampling may miss. The hot, simulation-critical
and cheap hooks accept compare (the contract tables above), except
`condition_ratio` (entry-EBX contract, open). Hot but left out: `cell_uv`,
`poly_clip`, `view_rect`, `tex_edge` and `panel_visible` are draw-pass or
HUD code (cheap, S/M, but not simulation); `order_update`,
`vehicle_update`, `group_activate` and the AI state methods are hot and
simulation-critical but need a replay log or a whole-DATA snapshot (L/XL).

## Nesting depth and live skips

Typical depths at entry, estimated from the direct-call graph of the
listing (hooks on the chain to the tick root; an AI state method sits
below `order_update`, `actor_step` and a MECH/FLYER trampoline, i.e. +3):

| Hook | Typical depth at entry | Expected live |
| --- | --- | --- |
| `key_axes` | 1 (`control_record`) | always compared |
| `sincos_pair` | 0 to 2 (HUD/view code, `lc_base_update`, meteor, convoy) | always compared |
| `convoy_steer` | 3 to 4 (`convoy_think` → `convoy_drive` → `convoy_follow`/`convoy_lead` → `convoy_seek`) | always compared |
| `rotate_2d` | 0 to 7 (formation slots under the AI ≤ 7, `ground_update`, convoy, HUD) | nearly always compared |
| formation slots | 1 (`group_activate`), 3 (convoy), 5 to 6 (`formation_steer` under `hunt`, `group_move`) | always compared |
| `slope_speed` | 3 (player: `player_input` → `leg_input` → `vehicle_control`), 5 to 7 (AI states), 8 (`group_scan`/`order_scan` → `group_move` → `leader_route` → `point_move` → `vehicle_control`) | compared except the AI group-move path |
| `object_query` | 4 (bullet → `sweep` → `base_hit_test`, most live calls), 6 (player obstacle path), 7 to 11 (AI obstacle path) | mostly compared; the AI obstacle path is skipped |
| `grid_sweep`, `set_length` | `object_query` + 1 / + 2; `set_length` also under `beam_create` (3 to 4) | compared on the bullet path, skipped under the AI; bypassed below a compared `object_query` |
| `line_plane` | 5 (bullet → `sweep` → `sweep_terrain` → `segment` → `contact`), 6 (player, `lc_base_update`), 7 to 11 (AI obstacle steering) | mixed: the AI obstacle calls are skipped |

Hooks reached only at depth ≥ 8 are never compared live at the current
limit: `mech_armour` (every call deep inside the replaced AI and actor
trees) and `region_entry` (below the logic frame, which adds a level; a
run with `logic_frame=original` would compare it). Live compare runs
of these two are open.

## Noise classes and known differences

Differences a comparison must not report, and the comparator or rule that
handles each:

- **Frame address in a dead register** (class F): `line_plane`'s hit EDX
  (EBP−28h of the original, the adapter's frame), `slope_speed`'s face-0
  EAX (EBP−0Ch). Compared as "both inside the current thread's stack"
  (TIB StackLimit..StackBase).
- **Address of a cloned argument** in EAX/EDX: remapped to the clone
  (`rotate_2d`, `set_length`, `point_offset`, `fire_permit`,
  `convoy_steer`, the weapon-slot and weapon-class leaves "relative to the
  span").
- **High-word residue**: `object_query` (callee or frame residue, AX
  only), `grid_sweep` (high word of out+4), `beam_ready` (EDX's high word
  is the caller's EBX's on one path; EAX and DX), `weapon_ammo`/`gun_ammo`
  (EAX's high word p's in each phase).
- **A legacy callee's register**: `region_test` compares EAX only, because
  the legacy `dist2d_point` hook does not reproduce the original's EDX.
- **ECX** is not compared in any mode (the dispatch returns the entry
  ECX); hooks whose exit shim loads the replacement's ECX refuse compare.
- **Snapshot bytes** are never hidden by a comparator.
- **In vitro only:** the unhooked original against the compared call runs
  at another stack depth, so frame addresses move; `compare_probes.h`
  takes a pair predicate for this (`compare_probe_same`).

## Validation

### In vitro

`tools/reference-game/invitro/compare_probes.h` runs, for every hook that
accepts compare with a declaration (the contract tables above), from its
group's `*_cases.h` (`mld_compare_build`, `wsl_compare_build`,
`wm_compare_build` and the others), 256 exact cases with real callees and
the group's real-pass or direct-case inputs (the unhooked original against
the call through the hook installed in compare mode: same pair and state,
one call, no skip, no bypass, no mismatch) and 256 skewed cases with a
replacement whose EAX bit 0 (or EDX, `compare_probe_skew_edx`, for
`slope_speed`) is flipped, half of them by a flipped span byte for span
hooks: one mismatch per call, the caller still gets the original's pair and
state. In the harness's installed compare pass (every hook installed, the
compare-capable ones in compare) the same generator compares the
trampoline with the hooked call (EAX and state); the pass requires zero
mismatches. The pair predicate `compare_probe_same` (frame addresses
move with the stack depth at which the original runs) serves
`line_plane`'s hit EDX, `slope_speed`'s face-0 EAX, `object_query`'s
residue and `beam_ready`'s EDX high word; `compare_probe_skew_edx` skews
EDX instead of EAX for `slope_speed`, whose comparator ignores EAX on the
face-0 path. Usage: [invitro/README.md](../../tools/reference-game/invitro/README.md).

The real-caller passes also rotate nested hooks through compare:
`sincos_pair`/`rotate_2d` (under 488dc7/488ddd), `line_plane` (under
`contact`), `key_axes` (under `control_record`), `engage_end` (under the
six MECH combat steps), `weapon_record` (under `lc_base_update`),
`def_lookup` (under `bullet_create` and `bullet_init`), `region_entry`
(under `region_find`), `pool_nth` (under `subject_resolve`), and the
groups `object_query`, `pool_query`, `beam`, `convoy`, `formation`,
`meteor`, `obstacle` reach `grid_sweep`, `set_length`, `convoy_steer`,
`rotate_2d` and `sincos_pair` in compare. Exceptions: `ground_height`
under `debris_update` and `slope_speed` under `vehicle_control` stay
replaced, because their terrain callee there is a stub drawing from a
stream (a second phase would see another answer). `slope_speed`'s own compare
cases use the real `terrain_face` on `camera_cases.h`'s grids (faces and
face 0) and the real `euler_matrix`.

`contact_scan` (`actor_pass_cases.h`): the arena is the block of a
synthetic building pool (10 objects of 1F8h + 8 bytes), the MECH and FLYER
pools are absent, RNG stream A is real; `sight_check`, `contact_share`,
`dist_gate`, `heading_point`, `rng_bounded` run as real code,
`terrain_line` and the virtuals are pure stubs (answers from the case seed
and the inputs, so both phases see the same answers), `trigger_fire` is
the real hook on a latched trigger record (entered, changes nothing) and
`mech_contact` is installed as the second guard. Each call is compared or
guarded, never refused; the skewed replacement flips A+95h. Fixture:
`//hook:hook_snapshot_test`. In-vitro equality is not live evidence.

### Live

Compare regressions (selected hooks in `compare`, every other hook
`replaced`); every compared call equal, `hk_<h>_mismatches` 0:

| Hooks in compare | Notes |
| --- | --- |
| the thirteen flag-level leaves | `mech_armour` never compared (all calls at depth ≥ 8) |
| `sincos_pair`, `rotate_2d`, `line_plane`, `object_query`, `convoy_steer`, `slope_speed`, `key_axes`, the formation slots | skips as the depth table predicts (`object_query` on the obstacle-steering path) |
| `contact_scan`, `object_query`, `grid_sweep`, `set_length`, the eight vfs hooks | `contact_scan` skips only the calls whose original phase entered a guard; `grid_sweep`/`set_length` mostly bypassed under `object_query`'s original phase |
| the six group leaves | `region_entry` never compared (depth limit) |

Open: the mission-load, weapon-slot and
weapon-class leaves (`pool_nth` … `gun_ammo`) have no live compare regression. Compare
runs of this size need `-SuiteTimeoutSeconds 1200`: with `contact_scan` in
compare `c1_01-success` exceeds the 300 s suite limit.

## Per-hook declarations

The Compare flag of every hook is the Compare column of the
[registry](hooks.md#registry) and of the generated function pages
(`docs/functions/*`); `compare_audit.py` checks it against the adapters
([Extraction script](#extraction-script)). Per class:

| Class | Contract |
| --- | --- |
| accepting, declared in the descriptor | spans and comparators of the hook descriptor in `proxy/*_hooks.h`; the 14 legacy hooks of `rng`, `geometry`, `timing` and `movement` have a NULL extension. Contracts of the early groups: [hooks.md](hooks.md#function-notes); of the vfs hooks: `vfs_hooks.h` |
| accepting, flag only | pure readers, leaves without writes, empty or constant bodies, hooks whose only callees are pure and compare-capable: default comparison of EDX:EAX |
| accepting, declaration documented here | the contract tables above and the exceptions below |
| refused | `compare_unsupported` TRUE; the reasons are the [refusal classes](#refusal-classes), the named cases are in [Why refused hooks refuse](#why-refused-hooks-refuse), the options in [What a compare-capable variant needs](#what-a-compare-capable-variant-needs) |

Accepting hooks whose declaration is not in the contract tables above:

| Hook | VA | Declaration |
| --- | --- | --- |
| `collision_walk` | 427c68 | comparator on EAX and DX; the lazy matrix cache of the entries is filled by the original phase, as for `object_query` |
| `shield_step` | 413af8 | span S[0..5]; EAX relative to S, EDX low word only (the high word is entry residue) |
| `proj_record` | 40ffb0 | pure reader of `[4a98fch]`; EDX from the entry registers |
| `damage_words` | 40501c | region RVA `4a95f2h`..`4a95f5h`; the high words of EAX/EDX from the entry registers |
| `flyer_throttle` | 422260 | span A[21Ch..234h] (the written word and the word A+233h it reads) |
| `mission_guard` | 4240f4 | pure reader of three image words; EDX is the entry EDX |
| `throttle_record`, `hud_record` | 447df8, 43dbb0 | no memory access; EAX = slot + 0B1h, EDX the entry EDX |
| `rotation_compose` | 47f058 | spans a 20, b 20, out 20 bytes; EAX = out remapped, EDX compared; ECX from the exit shim, not compared; overlapping arguments skip |
| `rotation_place` | 47f4f4 | spans a 20, b 32, out 32 bytes; comparator as `rotation_compose` |
| `vector_copy` | 40707c | span on the 12-byte destination, EAX = D remapped; disjoint vectors, as at the only caller |
| `ally_nearest` | 423974 | read-only; callee `dist_gate` pure; EAX and EDX compared |
| `dts_load` | 47a144 | the bitmap and array loads described and compared with the original's objects from EAX; other classes and unserved streams skip |
| `bitmap_new`, `bitmap_array_new` | 481cd4, 482d88 | the object (and children) described and compared from EAX; `bitmap_new`'s EDX = FS:0 at entry |
| `bitmap_read`, `bitmap_array_read` | 481d8c, 482e7c | fields or count into the clone; pixel and colour bytes against the file, children from the original's pointer array; allocation pointers excluded |
| `stream_destroy` | 472f64 | the counter as a region; decoder objects, the delete flag and an owned handle skip |
| `base_next_part`, `actor_next_part`, `mech_next_part` | 403624, 411b1c, 415558 | span on the out dword (argument 2); `base_next_part`'s EDX (out) relative to it |
| `actor_mark` | 42200c | span on byte O+B1h (argument 0); EAX (O+92h) relative |
| `weapon_target_word` | 40e484 | span on word W+7Dh (argument 0); EAX (W) relative |
| `nav_marker_set` | 434974 | one-hop region H+25Eh..+26Bh of the cockpit `[49B088h]` |
| `throttle_handler` | 447e08 | span G+0..C1h (argument 0); EDX (G+18h+4k) relative; a gadget table beyond 2Ah entries reads past the span |
| `view_pref_toggle` | 45fe38 | region: image words `4D2518h`..`4D2521h` |
| `collision_radius` | 40c5d0 | span argument 1, 6 bytes (the centre); callee `dist3d_delta` pure |
| `remap_word` | 41ff68 | span on word P (argument 0); EAX (P) relative; the runtime-indexed table byte is only read |
| `gun_slot_base`, `col_slot_base`, `dmg_slot_base` | 40feb4, 40cbd4, 40d200 | region: the image word `4A98E8h`, `4987DCh`, `498856h` |
| `menu_code` | 43f9c0 | pure reader; EDX:EAX exact; gap shim |
| `menu_toggle` | 43f9f8 | span menu+2Ch, 0Ch bytes (flag bytes, entry, byte +36h); EAX (menu) relative, EDX exact; gap shim |
| `menu_select` | 43f9e0 | span menu+32h, 7 bytes |
| `mfd_entry` | 4470a4 | span G+B1h, 4 bytes; EAX (G) relative, EDX exact; gap shim |
| `mfd_view_next` | 446fc4 | span G+318h, 8 bytes; EAX (G) relative, EDX exact; gap shim |

The per-hook classification of the refused hooks (refusal codes, what a
compare-capable variant needs, effort) is kept with the oracle evidence in
`es2-data`.

## Extraction script

`tools/reference-game/scripts/compare_audit.py` (standard library,
read-only) reads the registry and every `proxy/*_hooks.h` and prints the
flag per hook with its source line:

```powershell
python tools/reference-game/scripts/compare_audit.py            # Markdown
python tools/reference-game/scripts/compare_audit.py --json
python tools/reference-game/scripts/compare_audit.py --hooks-doc docs/reference-game/hooks.md
```

`--hooks-doc` compares the Compare column of the registry table of
hooks.md with the adapters; `--check <doc>` does the same for the first
table of any document whose header starts with `Hook` and has a `Compare`
column. Both list missing, extra or disagreeing hooks (exit status 1).
`tools/check.ps1` runs the script as the `audit` stage (`-SkipAudit`); it
is not part of `bazel test`, because a new hook would otherwise fail the
build until the documents are updated. Run it when a hook is added or its
flag changes.

The adapters define their extensions directly
(`static const es2_hook_extension x_extension={…,FLAG};`, also split over
two lines) or through function-like macros: a fixed flag
(`ACTOR_STEP_EXT(name)`), a flag parameter (`WEAPONS_EXT(name,regions,n,unsafe)`,
`VEHICLE_UPDATE_EXT(name,refuse)`, `WEAPON_HELPERS_EXT(name,unsafe)`) and
nested wrappers (`BUILDING_BOTH(name)` → `BUILDING_EXT(name)`). The script
strips comments, joins continuation lines, processes `#define`/`#undef` of
function-like macros in file order, substitutes parameters with `##`
pasting and expands nested invocations; it then reads the eighth
initializer of every `es2_hook_extension` definition (missing = 0 = FALSE).
It is not a C preprocessor: object-like macros, `#if` and `#include` are
ignored, and a flag that is not `TRUE`/`FALSE`/`1`/`0` after expansion, a
registry extension without a definition, or a name defined twice is
reported (`unknown`, exit status 1 or 2) rather than guessed. The unit
test `//tools/reference-game/scripts:compare_audit_unit_test` covers these
forms on synthetic snippets (direct, split, short initializer, fixed and
parameter flags, nested and redefined macros, commented-out definitions,
unknown flags, missing and duplicate definitions, the two `--check` table
selections). The refusal classes and effort are hand-written; the script
does not derive them.
