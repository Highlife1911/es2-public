# Compare mode

Which registered hooks accept `<hook>=compare`, the contract each accepting
hook declares (spans, regions, snapshot regions, comparator), why the
others refuse it, and what a compare-capable variant of a refused hook
would need. The framework is [hook/README.md](../../hook/README.md); the
registry and live call counts are in [hooks.md](hooks.md#registry). The
[per-hook table](#per-hook-table) is checked against the adapters by
`compare_audit.py` ([Extraction script](#extraction-script)); the
refusal codes, needs and effort columns are hand-written classifications,
and every flag flip needs its own in-vitro proof.

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
other groups that accept compare without a declaration are listed only in
the [per-hook table](#per-hook-table). The tables below give the contract
of every hook that needed a span, a region, a comparator or a callee
argument.

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
assets hooks, whose comparators describe the loaded object) carry their declaration in the
last column of the [per-hook table](#per-hook-table) and in their adapter
comments (`tools/reference-game/proxy/*_hooks.h`).

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

A hook can have several; codes in the Refusal column.

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

From cheap to expensive; the Needs column refers to these.

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
   bytes are initialized; PE header of the reference `VER95/DBSIM.EXE`,
   SHA-256 `8561aa47…c4ad`); a whole-DATA region is possible but not used
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
run with `logic_frame=original` would compare it). The live evidence
for these two is open.

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

| Hooks in compare | Evidence | Notes |
| --- | --- | --- |
| the thirteen flag-level leaves | `oracle-parallel-c01-compare-20261011T000134Z` | `mech_armour` never compared (all calls at depth ≥ 8) |
| `sincos_pair`, `rotate_2d`, `line_plane`, `object_query`, `convoy_steer`, `slope_speed`, `key_axes`, the formation slots | `check-set4a-20261011T020506Z` | skips as the depth table predicts (`object_query` on the obstacle-steering path) |
| `contact_scan`, `object_query`, `grid_sweep`, `set_length`, the eight vfs hooks | `<es2-data>\logs\check-set6-compare`, `...\check-set6-compare-rerun` | `contact_scan` skips only the calls whose original phase entered a guard; `grid_sweep`/`set_length` mostly bypassed under `object_query`'s original phase |
| the six group leaves | `check-r6-set2-compare` | `region_entry` never compared (depth limit) |

Open: no live compare regression of the mission-load, weapon-slot and
weapon-class leaves (`pool_nth` … `gun_ammo`) is recorded here. Compare
runs of this size need `-SuiteTimeoutSeconds 1200`: with `contact_scan` in
compare `c1_01-success` exceeds the 300 s suite limit.

## Per-hook table

Columns: VA (image base `400000h`); group (registry); Compare as the
adapters declare it (checked by `compare_audit.py`); refusal codes
([Refusal classes](#refusal-classes)) and effort ([Effort](#effort));
the declaration of an accepting hook, or what a compare-capable variant of
a refused hook needs ([options](#what-a-compare-capable-variant-needs)).
Live call counts per hook are in the registry table of
[hooks.md](hooks.md#registry). The reasons come from the adapter header and
extension comments (`tools/reference-game/proxy/*_hooks.h`), the
callee-counter lists in hooks.md and the core headers; they are
classifications, not new reverse engineering.

| Hook | VA | Group | Compare | Refusal | Declaration, or what a compare-capable variant needs | Effort |
| --- | --- | --- | --- | --- | --- | --- |
| `rng_next` | 492ee0 | rng | yes | — | — | — |
| `rng_bounded` | 492f24 | rng | yes | — | — | — |
| `dist2d_point` | 4928d0 | geometry | yes | — | — | — |
| `dist2d_vector` | 492a45 | geometry | yes | — | — | — |
| `dist2d_delta` | 492a9a | geometry | yes | — | — | — |
| `dist_octagonal` | 47de4c | geometry | yes | — | — | — |
| `dist_gate` | 49288c | geometry | yes | — | — | — |
| `time_scale` | 467894 | timing | yes | — | — | — |
| `timer_word_step` | 467a48 | timing | yes | — | — | — |
| `timer_dword_step` | 467aa8 | timing | yes | — | — | — |
| `timer_arm` | 467ac4 | timing | yes | — | — | — |
| `heading_cos` | 47dff0 | movement | yes | — | — | — |
| `heading_sin` | 47e027 | movement | yes | — | — | — |
| `multiply_q8` | 47e0a0 | movement | yes | — | — | — |
| `movement_step` | 467924 | movement | yes | — | — | — |
| `hud_speed` | 41bb3c | movement | yes | — | — | — |
| `transform_point` | 48043c | movement | yes | — | — | — |
| `vector_subtract` | 492a30 | movement | yes | — | — | — |
| `distance_display` | 47d694 | movement | yes | — | — | — |
| `euler_matrix` | 47ebb8 | math | yes | — | — | — |
| `matrix_compose` | 47fa20 | math | yes | — | — | — |
| `rotate_point` | 480304 | math | yes | — | — | — |
| `matrix_transpose` | 47df19 | math | yes | — | — | — |
| `pose_matrix` | 478c1c | math | yes | — | — | — |
| `pose_interpolate` | 49270c | math | yes | — | — | — |
| `scalar_interpolate` | 47df48 | math | yes | — | — | — |
| `track_count` | 4789b0 | math | yes | — | — | — |
| `inverse_sine` | 47da4c | math | yes | — | — | — |
| `angle_ratio` | 47d32c | math | yes | — | — | — |
| `matrix_angles` | 47f9a0 | math | yes | — | — | — |
| `multiply_q10` | 47e0b0 | math | yes | — | — | — |
| `approach_word` | 467adc | math | yes | — | — | — |
| `energy_accumulate` | 413b38 | math | yes | — | — | — |
| `clip_pose` | 478ef4 | math | yes | — | — | — |
| `pose_parent` | 478bc4 | math | yes | — | — | — |
| `track_forward` | 478f6c | math | yes | — | — | — |
| `track_inverse` | 478ff4 | math | yes | — | — | — |
| `track_scaled` | 4790b4 | math | yes | — | — | — |
| `track_scaled_inverse` | 479194 | math | yes | — | — | — |
| `manager_reset` | 478b7c | math | yes | — | — | — |
| `animation_step` | 40250c | math | no | G X | snapshot of the actor's track/node graph (variable size) or whole-DATA snapshot; replay log for the track wrappers | L |
| `track_advance` | 478d38 | math | no | X V | graph clone of the manager's tracks, node matrices and flags (variable size); clip virtuals of unknown classes | L |
| `track_target` | 47967c | math | yes | — | — | — |
| `track_rate` | 4796d8 | math | yes | — | — | — |
| `actor_animate` | 402604 | math | no | G X | snapshot of the actor's track/node graph (variable size) or whole-DATA snapshot; replay log for the track wrappers | L |
| `ground_animate` | 418f40 | math | no | G X | snapshot of the actor's track/node graph (variable size) or whole-DATA snapshot; replay log for the track wrappers | L |
| `terrain_height` | 46e180 | math | yes | — | — | — |
| `actor_snapshot` | 402628 | math | no | X | RVA region for the global copy block (≤ 4 KiB, compared in vitro) plus an indirect region for the actor extent, if bounded under 16 KiB | M |
| `actor_rollback` | 4027fc | math | no | X | RVA region for the global copy block (≤ 4 KiB, compared in vitro) plus an indirect region for the actor extent, if bounded under 16 KiB | M |
| `ground_update` | 41a360 | math | no | G X | replay log for the ground callees (track wrappers, terrain, radius hits) or whole-DATA snapshot | L |
| `ground_collide` | 418f74 | math | no | G X | replay log for the ground callees (track wrappers, terrain, radius hits) or whole-DATA snapshot | L |
| `ground_effects` | 4195c8 | math | no | V I X | attachment/latch graph clone and a sound/feedback diversion; not worth it (oracle runs cover it) | XL |
| `track_clip` | 4792ac | math | no | V | restrict to the two known clip classes (others: skip), spans on the track; clip virtual +20h replayed | M |
| `dist3d_delta` | 47de72 | damage | yes | — | — | — |
| `dist3d_forward` | 492aaf | damage | yes | — | — | — |
| `heading_point` | 492934 | damage | yes | — | — | — |
| `shield_damage` | 413c68 | damage | yes | — | — | — |
| `vehicle_hit` | 4187d0 | damage | no | G V R | replay log for the V58/V74 calls and RNG regions, or whole-DATA snapshot | L |
| `building_hit` | 404f20 | damage | no | G V R | replay log for the V58/V74 calls and RNG regions, or whole-DATA snapshot | L |
| `building_part` | 404d70 | damage | no | X V | indexed 11-byte state as a runtime-indexed region (framework extension) and replay of V40/V60/lifecycle calls | L |
| `projectile_def` | 40a234 | weapons | yes | — | — | — |
| `bullet_def` | 40adc0 | weapons | yes | — | — | — |
| `mask_merge` | 40f881 | weapons | yes | — | — | — |
| `radius_hit` | 426a20 | weapons | no | G V | replay log of the V70 hit virtuals (one per receiver) | L |
| `weapon_step` | 40ef94 | weapons | no | G X A | whole-DATA snapshot (cooldowns, global matrix/point/angles, owner cache, projectile creation) | L |
| `weapon_fire` | 40e964 | weapons | no | G X A | whole-DATA snapshot (cooldowns, global matrix/point/angles, owner cache, projectile creation) | L |
| `weapon_prepare` | 40e788 | weapons | no | G X A | whole-DATA snapshot (cooldowns, global matrix/point/angles, owner cache, projectile creation) | L |
| `bullet_flight` | 40b124 | weapons | no | G A I | whole-DATA snapshot plus a sound-latch/camera-global diversion | XL |
| `projectile_flight` | 40a538 | weapons | no | G A I | whole-DATA snapshot plus a sound-latch/camera-global diversion | XL |
| `vehicle_part` | 417de4 | weapons | no | V X | replay of the damage-graph virtuals and notifications (recursive V74) | L |
| `target_emit` | 4226a0 | weapons | no | G | replay log of weapon_prepare/impact/mount calls (callees are hooks with their own effects) | L |
| `emit` | 40ea58 | weapons | no | G | replay log of weapon_prepare/impact/mount calls (callees are hooks with their own effects) | L |
| `alt_emit` | 40ec64 | weapons | no | G | replay log of weapon_prepare/impact/mount calls (callees are hooks with their own effects) | L |
| `impact` | 40bf74 | weapons | no | G A I | whole-DATA snapshot plus pool and sound diversion (pool_alloc, beam_create, 4628e0, frame record 49630c/FS:0) | XL |
| `turret` | 404100 | weapons | no | G V A | whole-DATA snapshot (target selection, base update, projectile creation, RNG) | L |
| `sweep` | 426528 | weapons | no | G V A | whole-DATA snapshot (receiver virtuals, triggers, explosions, pool, FS:0 record) | XL |
| `side_scale` | 426b04 | weapons | no | U | flag only: only callee multiply_q10 is pure; no writes | S |
| `cell` | 427238 | weapons | yes | — | — | — |
| `segment` | 46e980 | weapons | no | G | callee-first: make normalize…contact compare-capable, then regions for 6b505c/4a0c4c | M |
| `sweep_terrain` | 428048 | weapons | no | G | callee-first: make normalize…contact compare-capable, then regions for 6b505c/4a0c4c | M |
| `slope_test` | 46ff4c | weapons | yes | — | — | — |
| `normalize` | 46c23c | weapons | no | O | span on the output/in-place vector, comparator remapping a returned vector address (aliasing is skipped by the framework) | S |
| `diagonal` | 46fdb8 | weapons | no | O | span on the output/in-place vector, comparator remapping a returned vector address (aliasing is skipped by the framework) | S |
| `edge_test` | 470468 | weapons | no | F | comparator ignoring the dead frame-address EDX | S |
| `face_test` | 46ff90 | weapons | no | F S | flag + comparator; the entry shim hands the same ESP-1Ch residue to both phases | S |
| `face_cross` | 470080 | weapons | no | O | span on the output/in-place vector, comparator remapping a returned vector address (aliasing is skipped by the framework) | S |
| `contact` | 470798 | weapons | no | O | RVA regions for 6b505c and the init byte 4a0c4c; callee line_plane first | M |
| `terrain_line` | 4280f4 | weapons | no | O G | span on the output; callee segment first | M |
| `terrain_face` | 46e498 | weapons | yes | — | — | — |
| `camera_update` | 4011a0 | weapons | no | X I | RVA regions for the camera block (fixed globals), comparator ignoring the dead EDX:EAX; presentation-adjacent | M |
| `object_line` | 404ca0 | ai | no | G F | hook or prove 404bc0 pure; comparator on AX only | M |
| `obstacle_steer` | 416274 | ai | no | G V F | replay of the virtual +7Ch and object_line/segment once those compare | L |
| `track_sort` | 4789d0 | math | yes | — | — | — |
| `track_step` | 479720 | math | no | F V | comparator that remaps the track-address high words; skip unknown clip classes | M |
| `track_list` | 478aac | math | no | X | graph clone of the variable track list / node matrices (runtime-indexed) | L |
| `node_matrices` | 478c64 | math | no | X | graph clone of the variable track list / node matrices (runtime-indexed) | L |
| `terrain_pose` | 4029d8 | node | no | V | replay of the shape virtual +10h (node lookup) or prove it pure | M |
| `shield_geometry` | 413cc4 | node | no | G F | prove the point transform 4801d4 pure (or hook it); comparator ignoring the dead EDX | M |
| `debris_heading` | 4089e4 | node | no | R | RNG A/B regions (2 × 72h bytes at 4d2599/4d260b) | S |
| `trigger_scan` | 426b70 | mission | no | G I | replay log for trigger_fire (message posts through 433158) | L |
| `trigger_regions` | 4234b8 | mission | no | G I | replay log for trigger_fire (message posts through 433158) | L |
| `region_test` | 4233a4 | mission | yes | — | — | — |
| `trigger_group` | 423508 | mission | no | G I | replay log for trigger_fire (message posts through 433158) | L |
| `trigger_fire` | 423430 | mission | no | G I X | message-queue diversion (433158 per message) and the trigger record as span | L |
| `explosion_update` | 40813c | logic | no | R G | timer word as span; light_level first | L |
| `light_level` | 4076a0 | logic | no | X | runtime-indexed light entry: indirect region with a computed index (framework extension) | M |
| `fire_update` | 46b678 | logic | no | G V R | replay of 46297c and the shape slot +20h; timer as span | L |
| `group_fire` | 4107e4 | weapon_helpers | no | V F | replay of the weapon virtuals +48h/+34h/+28h; comparator on AX only | L |
| `weapon_ready` | 40ede8 | weapon_helpers | yes | — | — | — |
| `part_condition` | 40db2c | weapon_helpers | yes | — | — | — |
| `sphere_hits` | 40c524 | weapon_helpers | no | G F | prove 4801d4 pure, sphere_test first; comparator ignoring the frame-address EDX | M |
| `sphere_test` | 40c428 | weapon_helpers | no | O | RVA region for [4a9810] | S |
| `track_route` | 4793d4 | track_pose | yes | — | — | — |
| `track_poses` | 479ab0 | track_pose | no | X V | graph clone of the bone/pose lists; skip unknown clip classes | L |
| `pose_list` | 478b00 | track_pose | no | X V | graph clone of the bone/pose lists; skip unknown clip classes | L |
| `track_seek` | 479344 | track_pose | no | G | callee-first: track_clip | M |
| `turret_aim` | 403d5c | track_pose | no | G R | actor_animate/track_seek first (both refused); approach word as span | L |
| `part_ratio` | 40dbc0 | vehicle_control | yes | — | — | — |
| `slope_speed` | 41693c | vehicle_control | yes | — | — | — |
| `random_damage` | 417a04 | vehicle_control | no | R V | RNG regions plus replay of slot +74h and 40dc58 | L |
| `vehicle_control` | 416a04 | vehicle_control | no | G I | whole-DATA snapshot plus sound diversion (4628e0); many refused callees | XL |
| `objective_eval` | 413280 | mission_objectives | no | G | hook or replay 41324c/413920/413950/412d90/412c58; actor_safe first | L |
| `actor_safe` | 4137b4 | mission_objectives | no | G | callee-first: the leaves target_known, order_target, route_point | M |
| `target_known` | 411c58 | mission_objectives | yes | — | — | — |
| `order_target` | 423918 | mission_objectives | yes | — | — | — |
| `order_done` | 4239fc | mission_objectives | no | G | callee-first: subject_done, rivals_done (412dc4/412be4), route_point | M |
| `subject_done` | 412e58 | mission_objectives | no | G | hook or prove 412dc4 / 412be4 pure | M |
| `rivals_done` | 412e74 | mission_objectives | no | G | hook or prove 412dc4 / 412be4 pure | M |
| `route_point` | 423b0c | mission_objectives | yes | — | — | — |
| `outcome_poll` | 4131ac | mission_objectives | no | G I | status_check first (event post); timer word as span | L |
| `status_eval` | 413180 | mission_objectives | no | G I | status_check first (event post); timer word as span | L |
| `bounds_check` | 41379c | mission_objectives | yes | — | — | — |
| `bounds_outside` | 41373c | mission_objectives | yes | — | — | — |
| `explosion_create` | 407f1c | effect | no | A I R | pool/constructor diversion and sound diversion; whole-DATA snapshot | XL |
| `object_init` | 402188 | effect | no | A | constructor and runtime-count diversion (47749c, 490b64, frame record 49630c) | L |
| `light_create` | 407604 | effect | no | A | constructor diversion (406f38, 407048) | L |
| `pool_alloc` | 471b30 | effect | no | A X | indirect regions for the pool head, free list link and runtime count (bounded); FS:0 untouched | M |
| `condition_table` | 4151a4 | vehicle | no | O | span on the 82-byte table output | S |
| `condition_ratio` | 4152ec | vehicle | no | S | the original phase must see the caller's EBX: kinds outside 0..2 dereference the entry EBX/ECX, the entry shim hands EBX to the replacement only, and the framework enters the trampoline with EBX = its call target; needs an entry-EBX contract for the original phase, or a proof that kinds 3+ never occur (both callers push 0) | M |
| `sight_check` | 412608 | vehicle | no | R G V | RNG regions, terrain_line first, replay of the virtual +24h | L |
| `group_state` | 412c8c | vehicle | no | V | replay of the virtual +40h (or prove it a reader) | M |
| `target_bearing` | 41e758 | vehicle | no | V | replay of the virtual +3Ch; callees otherwise pure | M |
| `part_position` | 41b60c | vehicle | no | V G | replay of slot +20h, prove 4801d4 pure | M |
| `fire_selected` | 410dbc | input | no | V G | replay of the weapon/vehicle virtuals (+2Ch/+30h/+28h/+60h/+3Ch/+24h) | L |
| `fire_line` | 415608 | input | no | V G | replay of the weapon/vehicle virtuals (+2Ch/+30h/+28h/+60h/+3Ch/+24h) | L |
| `aim_point` | 41b728 | input | no | V G | replay of the weapon/vehicle virtuals (+2Ch/+30h/+28h/+60h/+3Ch/+24h) | L |
| `leg_input` | 4160dc | input | no | G | callee-first: vehicle_control (XL) | L |
| `line_plane` | 47e610 | line_plane | yes | — | — | — |
| `target_select` | 411fa0 | building | no | G V | replay of slot +4Ch; target_valid/order_target first | L |
| `target_valid` | 411e80 | building | no | G | callee-first: target_known, order_actor | M |
| `death_actions` | 411bc8 | building | no | G X | group_death first; variable actions write mission variables | L |
| `group_death` | 423f30 | building | no | X | the ten variable actions write runtime-indexed variables: indexed regions | M |
| `combat_rating` | 41edd8 | vehicle_update | no | V G | replay of slot +54h; condition_record first | L |
| `condition_record` | 41b4ec | vehicle_update | no | O | span on the 84-byte record; condition_table first | M |
| `device_state` | 41aa10 | vehicle_update | yes | — | — | — |
| `device_word` | 41aa44 | vehicle_update | yes | — | — | — |
| `ready_tone` | 41b0bc | vehicle_update | no | I | replay of the clock 467828 and the sound 462830 | L |
| `weapon_tally` | 40fbdc | vehicle_update | no | V | replay of slot +60h; memset acts on a span | M |
| `vehicle_update` | 41aa5c | vehicle_update | no | G V R I | whole-DATA snapshot (the per-vehicle update reaches most of the simulation) | XL |
| `poly_clip` | 472bcc | world | no | O | indirect regions for the outputs behind argument pointers (draw pass, not simulation) | M |
| `poly_spans` | 472b14 | world | no | G I | not meaningful: the scan converter 4930c1 draws | n/a |
| `view_rect` | 46d324 | world | no | O | indirect regions for the outputs behind argument pointers (draw pass, not simulation) | M |
| `cell_uv` | 46bdf8 | world | no | O | indirect regions for the outputs behind argument pointers (draw pass, not simulation) | M |
| `flight_model` | 466b58 | flyer | no | G I S | replay of the platform call 477904; stack-residue shim | L |
| `flyer_move` | 4218c4 | ai_motion | no | G I | sound diversion (462830); callees otherwise pure | M |
| `formation_steer` | 41fbb8 | ai_motion | no | G V | vehicle_control first; replay of slot +78h | L |
| `cockpit_valid` | 433174 | cockpit_target | no | U | flag only: only callee dist_gate is pure (HUD) | S |
| `cockpit_arc` | 433250 | cockpit_target | no | V | replay of the virtual +3Ch (HUD) | M |
| `cockpit_next` | 43349c | cockpit_target | no | G I | replay of the HUD record update 43d9b4 (HUD) | L |
| `cockpit_nearest` | 4333c8 | cockpit_target | no | G I | replay of the HUD record update 43d9b4 (HUD) | L |
| `cockpit_select` | 4332dc | cockpit_target | no | G I | replay of the HUD record update 43d9b4 (HUD) | L |
| `player_route` | 41c194 | route | no | G I | replay of the message post 420a68; route leaves first | L |
| `route_closing` | 423170 | route | no | O | span on the route holder | S |
| `route_advance` | 42313c | route | no | O | span on the route holder | S |
| `timer_scan` | 426b48 | route | no | G | callee-first: mission_timer | L |
| `mission_timer` | 4230a4 | route | no | R G | timer words as spans; trigger_fire first | L |
| `status_check` | 4135e8 | route | no | G I | event-post diversion (EventPort slot 0) and objective_eval first | L |
| `object_query` | 427da8 | object_query | yes | — | — | — |
| `grid_sweep` | 4273c8 | object_query | yes | — | — | — |
| `set_length` | 4927f0 | object_query | yes | — | — | — |
| `flyer_input` | 41bb9c | flyer_control | no | G S | flight_model first; stack-residue shims | L |
| `flyer_command` | 4221a8 | flyer_control | no | G S | flight_model first; stack-residue shims | L |
| `flyer_bank` | 42215c | flyer_control | no | G S | flight_model first; stack-residue shims | L |
| `flyer_steer` | 4222fc | flyer_control | no | G S | flight_model first; stack-residue shims | L |
| `vector_angle` | 49290c | projectile | yes | — | — | — |
| `aim_angles` | 492990 | projectile | no | O S | span on the output, comparator remapping EAX = out; the entry shim supplies the same ECX to both phases | S |
| `projectile_steer` | 40a254 | projectile | no | R V | approach/movement words as spans, replay of the virtuals +58h/+24h | L |
| `bullet_steer` | 40aff0 | projectile | no | R V | approach/movement words as spans, replay of the virtuals +58h/+24h | L |
| `torso_twist` | 41a550 | torso | no | G I | sound-latch diversion (41a6d0/41a994), track_seek first | L |
| `torso_pitch` | 41a808 | torso | no | G I | sound-latch diversion (41a6d0/41a994), track_seek first | L |
| `weapon_converge` | 41a74c | torso | no | G | callee-first: mount_offset, aim_angles; frame in scratch | M |
| `weapon_keys` | 410f78 | torso | no | G I | replay of the weapon-system routines and HUD calls | L |
| `smoke_ball_update` | 4091cc | pools | no | R | timer dword of the ball as span | S |
| `smoke_update` | 4092dc | pools | no | A G | whole-DATA snapshot (pool links, explosions, debris) | L |
| `smoke_ball_init` | 409178 | pools | no | A | callee-first: object_init (constructor diversion) | L |
| `debris_update` | 408bd8 | pools | no | A G | whole-DATA snapshot (pool links, explosions, debris) | L |
| `ground_height` | 427d80 | pools | yes | — | — | — |
| `meteor_update` | 409d2c | pools | no | A I R | pool/constructor diversion and sound diversion; whole-DATA snapshot | XL |
| `group_activate` | 4236c4 | groups | no | A G R | whole-DATA snapshot (spawn, pool, RNG, FS:0 record) | L |
| `spawn_point` | 42354c | groups | no | G V | obstacle_test/face_test first, replay of slot +7Ch | L |
| `order_update` | 423a74 | groups | no | G V | whole-DATA snapshot: reaches actor_step and the whole AI | XL |
| `group_assess` | 423f08 | groups | no | G | hook or replay 41b804 | M |
| `order_actor` | 4238a0 | groups | no | U | flag only: pure leaf reader | S |
| `order_point` | 4238d4 | groups | no | U | flag only: pure leaf reader | S |
| `actor_destination` | 41dbcc | groups | no | U | flag only once order_point compares | S |
| `actor_pass` | 4123ac | actor_pass | no | G V R | whole-DATA snapshot (actor contacts, sight checks, trigger_fire) | L |
| `contact_expire` | 41251c | actor_pass | no | G V R | whole-DATA snapshot (actor contacts, sight checks, trigger_fire) | L |
| `contact_scan` | 4128f8 | actor_pass | yes | — | — (snapshot regions, guards `mech_contact`, `trigger_fire`) | — |
| `contact_share` | 412704 | actor_pass | no | G V R | whole-DATA snapshot (actor contacts, sight checks, trigger_fire) | L |
| `mech_order` | 420ad4 | mech_order | no | G I | replay of the radio message 420a98 / HUD cancel 435ac8 and the AI order helpers | L |
| `wingman_order` | 431610 | mech_order | no | G I | replay of the radio message 420a98 / HUD cancel 435ac8 and the AI order helpers | L |
| `sincos_pair` | 47eb30 | mount | yes | — | — | — |
| `rotate_2d` | 47ffe8 | mount | yes | — | — | — |
| `mount_offset` | 40f540 | mount | no | G | callee-first: mount_base | S |
| `mount_base` | 40f904 | mount | no | O | RVA regions for the static vector 4a9af0 and the guard byte 498f34 | S |
| `target_class` | 411cb4 | ai_order | yes | — | — (pure leaf; default comparison) | — |
| `script_start` | 413e50 | ai_order | no | O | span on the script record (memset runs on the cloned argument); comparator remapping the memset result | S |
| `attack_target` | 41c0f4 | ai_order | no | G | callee-first: script_start, attack_replan (L) | L |
| `order_resume` | 41eb34 | ai_order | no | G | callee-first: script_start, attack_replan (L) | L |
| `torso_center` | 41e8d4 | input_dispatch | no | G I | callee-first: torso_twist/torso_pitch (sound latches) | L |
| `torso_aim` | 41ef14 | input_dispatch | no | G I S | torso servos first; stack-coordinate shim | L |
| `player_input` | 460870 | input_dispatch | no | G I | not meaningful: command handler, HUD, camera and platform calls | n/a |
| `select_step` | 41074c | weapon_system | no | G | hook or replay 410708 | M |
| `fire_group` | 410ae4 | weapon_system | no | G V I | replay of the HUD calls 4610ad/441f90 and the weapon virtuals | L |
| `weapons_v3c` | 410d04 | weapon_system | no | V | replay of the weapon virtual +3Ch | M |
| `group_toggle` | 4110ac | weapon_system | no | G V I | replay of the HUD calls 4610ad/441f90 and the weapon virtuals | L |
| `tex_edge` | 48aaac | math_lib | no | O | span on the edge record; overlap with the point arrays is skipped (draw pass) | S |
| `convoy_steer` | 46a89c | convoy | yes | — | — | — |
| `convoy_seek` | 46a958 | convoy | no | G | callee-first: convoy_steer, point_offset | M |
| `convoy_lead` | 46a9e8 | convoy | no | G V | replay of 46a5bc/47df04 and slot +78h; convoy callees first | L |
| `convoy_follow` | 46aa60 | convoy | no | G V | replay of 46a5bc/47df04 and slot +78h; convoy callees first | L |
| `convoy_drive` | 46a810 | convoy | no | G V | replay of 46a5bc/47df04 and slot +78h; convoy callees first | L |
| `convoy_think` | 46a6d4 | convoy | no | G V A | whole-DATA snapshot (base update, turret, collision) | XL |
| `turret_target` | 403eec | building_aim | no | G V | turret_aim first; replay of slot +20h | L |
| `part_facing` | 406868 | building_aim | no | V | replay of the building slot +58h; aim_angles first | M |
| `point_offset` | 4929fc | ai_helpers | yes | — | — | — |
| `threat_sum` | 41cb44 | ai_helpers | no | G | replay or hook the list step 471c70 (pure walk) | M |
| `engage_done` | 41c4a8 | ai_helpers | no | G | callee-first: target_class, order_target | S |
| `flank_step` | 41c72c | ai_helpers | no | R V | timer word as span, replay of the virtual +3Ch | M |
| `retreat_check` | 41cb94 | ai_helpers | no | G V | replay of slot +40h; order_resume/script_start first | L |
| `part_pick` | 41ce08 | ai_helpers | no | R O | RNG regions and a span on A+2A2h | S |
| `drive_apply` | 41e828 | ai_helpers | no | G | callee-first: vehicle_control (XL) | L |
| `obstacle_test` | 404ae4 | obstacle | no | G | replay of the list step 471c70 and the collision walk 427c68; frame image in scratch | L |
| `convoy_collide` | 46a614 | obstacle | no | G V | replay of the virtuals +5Ch/+68h/+7Ch; obstacle_test first | L |
| `shield_max_set` | 413ab8 | vehicle_aux | no | O | span on the shield record S; comparator remapping EAX = S | S |
| `shield_capacity` | 417bec | vehicle_aux | no | O | callee-first: shield_max_set; span on the shield record | S |
| `damage_score` | 41537c | vehicle_aux | no | U | flag only: pure leaf reader | S |
| `facing_part` | 41b534 | vehicle_aux | no | V | replay of slot +58h; aim_angles first | M |
| `vehicle_keys` | 4157c8 | vehicle_aux | no | G I | replay of the widget call 438d9c and weapon_keys | L |
| `beam_create` | 40b804 | beam | no | A R | object_init first; RNG regions; RTL 49667c on the clone | L |
| `weapon_range` | 40e5f8 | ai_behaviour | yes | — | — | — |
| `weapon_choose` | 41f358 | ai_behaviour | no | G V | replay log of the AI callees and virtuals, or whole-DATA snapshot | L |
| `aim_fire` | 41f5a0 | ai_behaviour | no | G V R S | whole-DATA snapshot (the compare buffers are off the stack; a compare level costs about 1.4 KiB of its 64-page gap) | XL |
| `aim_target` | 41e984 | ai_behaviour | no | G V | replay log of the AI callees and virtuals, or whole-DATA snapshot | L |
| `target_search` | 41e0e0 | ai_behaviour | no | G V | replay log of the AI callees and virtuals, or whole-DATA snapshot | L |
| `hunt` | 41e224 | ai_behaviour | no | G V | replay log of the AI callees and virtuals, or whole-DATA snapshot | L |
| `line_blocked` | 41dc24 | ai_behaviour | no | G V | replay log of the AI callees and virtuals, or whole-DATA snapshot | L |
| `clear_shot` | 41dd64 | ai_behaviour | no | G V | replay log of the AI callees and virtuals, or whole-DATA snapshot | L |
| `group_move` | 41d598 | ai_behaviour | no | G V | replay log of the AI callees and virtuals, or whole-DATA snapshot | L |
| `death_sequence` | 45fa84 | death | no | I R | not meaningful: display, view and camera presentation calls | n/a |
| `shield_balance` | 413bc8 | shield | no | I | replay of the gauge setter 443880 (one call); record in scratch | M |
| `result_conditions` | 423d68 | results | no | I | file-write diversion: capture the stream writes (virtual +14h) in both phases and compare the bytes | L |
| `map_pick` | 44d888 | map_pick | no | O | span on map+374h..37Fh (overlap with actor records is skipped) | S |
| `key_message` | 477bec | keyboard | no | I | not meaningful: GetKeyState (input journal), focus switch, hot-key callbacks | n/a |
| `panel_visible` | 431410 | hud_helpers | no | U | flag only: pure reader (presentation) | S |
| `aim_select` | 41ea7c | ai_behaviour2 | no | G V S | replay log of the AI callees, or whole-DATA snapshot; stack-coordinate shims | L |
| `attack_plan` | 41cf18 | ai_behaviour2 | no | G V S | replay log of the AI callees, or whole-DATA snapshot; stack-coordinate shims | L |
| `attack_replan` | 41d5ec | ai_behaviour2 | no | G V S | replay log of the AI callees, or whole-DATA snapshot; stack-coordinate shims | L |
| `group_scan` | 41d60c | ai_behaviour2 | no | G V S | replay log of the AI callees, or whole-DATA snapshot; stack-coordinate shims | L |
| `order_scan` | 41d7d0 | ai_behaviour2 | no | G V S | replay log of the AI callees, or whole-DATA snapshot; stack-coordinate shims | L |
| `retreat_fire` | 41d2c4 | ai_behaviour2 | no | G V S | replay log of the AI callees, or whole-DATA snapshot; stack-coordinate shims | L |
| `control_record` | 45a810 | control_record | no | I | not meaningful: message pump, device poll, tape file I/O | n/a |
| `actor_step` | 411cec | actor_step | no | V S | whole-DATA snapshot: the state members are the whole AI | XL |
| `state_frame` | 413eb0 | actor_step | no | U | flag only: pure leaf (0 calls while actor_step is replaced) | S |
| `mech_move` | 415afc | actor_step | no | V S | whole-DATA snapshot: the state members are the whole AI | XL |
| `mech_act` | 415b38 | actor_step | no | V S | whole-DATA snapshot: the state members are the whole AI | XL |
| `mech_decide` | 415b74 | actor_step | no | V S | whole-DATA snapshot: the state members are the whole AI | XL |
| `flyer_move_call` | 4217fc | actor_step | no | V S | whole-DATA snapshot: the state members are the whole AI | XL |
| `flyer_act` | 42184c | actor_step | no | V S | whole-DATA snapshot: the state members are the whole AI | XL |
| `flyer_decide` | 421888 | actor_step | no | V S | whole-DATA snapshot: the state members are the whole AI | XL |
| `formation_base` | 405c04 | formation | yes | — | — | — |
| `formation_mech` | 417898 | formation | yes | — | — | — |
| `formation_flyer` | 421e98 | formation | yes | — | — | — |
| `base_update` | 403ca8 | base | no | G A R | whole-DATA snapshot (parts, fires, debris, explosions, pool, FS:0 record) | XL |
| `base_parts` | 403914 | base | no | G A R | whole-DATA snapshot (parts, fires, debris, explosions, pool, FS:0 record) | XL |
| `base_part_chain` | 403890 | base | no | V | replay of slot +74h (recursive through the hook) | L |
| `base_part_blast` | 4036c0 | base | no | A G | whole-DATA snapshot (explosions, debris, pool) | L |
| `base_part_debris` | 40379c | base | no | A G | whole-DATA snapshot (explosions, debris, pool) | L |
| `engage_end` | 41c478 | mech_combat | yes | — | — (span A+23Eh..A+24Bh, the state word it clears; default comparison) | — |
| `skirt_check` | 41de9c | mech_combat | no | G | callee-first: script_start | S |
| `attack_step` | 41c594 | mech_combat | no | G V S | replay log of the combat callees (aim_select chain), or whole-DATA snapshot; stack-coordinate shims | L |
| `base_attack` | 41c86c | mech_combat | no | G V S | replay log of the combat callees (aim_select chain), or whole-DATA snapshot; stack-coordinate shims | L |
| `flyer_attack` | 41c9cc | mech_combat | no | G V S | replay log of the combat callees (aim_select chain), or whole-DATA snapshot; stack-coordinate shims | L |
| `face_off` | 41d41c | mech_combat | no | G V S | replay log of the combat callees (aim_select chain), or whole-DATA snapshot; stack-coordinate shims | L |
| `flank_attack` | 41d4e4 | mech_combat | no | G V S | replay log of the combat callees (aim_select chain), or whole-DATA snapshot; stack-coordinate shims | L |
| `drive_off` | 41def0 | mech_combat | no | G V S | replay log of the combat callees (aim_select chain), or whole-DATA snapshot; stack-coordinate shims | L |
| `flyer_attacking` | 422ca8 | flyer_states | no | G V | replay log of the FLYER callees (target_emit, flyer_steer), or whole-DATA snapshot | L |
| `flyer_follow` | 422598 | flyer_states | no | G V | replay log of the FLYER callees (target_emit, flyer_steer), or whole-DATA snapshot | L |
| `flyer_formation` | 422a50 | flyer_states | no | G V | replay log of the FLYER callees (target_emit, flyer_steer), or whole-DATA snapshot | L |
| `flyer_search` | 422a80 | flyer_states | no | G V | replay log of the FLYER callees (target_emit, flyer_steer), or whole-DATA snapshot | L |
| `flyer_patrol` | 422b34 | flyer_states | no | G V | replay log of the FLYER callees (target_emit, flyer_steer), or whole-DATA snapshot | L |
| `flyer_scout` | 422bdc | flyer_states | no | G V | replay log of the FLYER callees (target_emit, flyer_steer), or whole-DATA snapshot | L |
| `flyer_decision` | 422d00 | flyer_states | no | G V | replay log of the FLYER callees (target_emit, flyer_steer), or whole-DATA snapshot | L |
| `flyer_engage` | 422bf0 | flyer_states | no | G V | replay log of the FLYER callees (target_emit, flyer_steer), or whole-DATA snapshot | L |
| `mech_contact` | 412800 | mech_notices | no | G I | replay of the radio message 420a98 and the AI callees | L |
| `mech_kill` | 415710 | mech_notices | no | G I | replay of the radio message 420a98 and the AI callees | L |
| `mech_attacked` | 41f7b8 | mech_notices | no | G I | replay of the radio message 420a98 and the AI callees | L |
| `mech_alert` | 41f790 | mech_notices | no | I | replay of the radio message 420a98 (only callee) | M |
| `base_aim_node` | 403548 | actor_virtuals | no | O | RVA region for the static node record 4a95cc and its first-call byte | S |
| `base_offsets` | 40351c | actor_virtuals | no | G O | hook or replay 411a74; spans on the two outputs | M |
| `base_condition` | 4052b4 | actor_virtuals | yes | — | — | — |
| `actor_attacked` | 411aec | actor_virtuals | no | G | callee-first: contact_share (L) | L |
| `base_part_position` | 406808 | actor_virtuals | no | G | prove 4801d4 pure; output span | M |
| `mech_aim_node` | 417b98 | actor_virtuals | no | V | replay of the shape slot +20h | M |
| `mech_offsets` | 4155c4 | actor_virtuals | no | O | spans on the two 6-byte outputs; comparator remapping the address high words in EAX/EDX | S |
| `mech_armour` | 4154d0 | actor_virtuals | yes | — | — | — |
| `mech_target_scale` | 415498 | actor_virtuals | yes | — | — | — |
| `mech_engage_roll` | 41cabc | actor_virtuals | no | R | RNG A region (two rng_next draws) | S |
| `mech_link_position` | 41dd2c | actor_virtuals | no | O | one span A+ADh..A+329h (link copy and flag byte); comparator remapping the actor addresses in EAX/EDX | S |
| `col_query` | 40c9d4 | col | no | G V | replay of the shape lookup slot +20h and 4801d4; the 4a980c frame pointer must stay valid in both phases | L |
| `col_group` | 40c8fc | col | no | G V | replay of the shape lookup slot +20h and 4801d4; the 4a980c frame pointer must stay valid in both phases | L |
| `col_shape` | 40c8c8 | col | no | G V | replay of the shape lookup slot +20h and 4801d4; the 4a980c frame pointer must stay valid in both phases | L |
| `col_bound` | 40c4c4 | col | no | G V | replay of the shape lookup slot +20h and 4801d4; the 4a980c frame pointer must stay valid in both phases | L |
| `debris_table` | 4089bc | debris_burst | yes | — | — (2-byte span on the word it reduces; default comparison) | — |
| `debris_burst` | 408530 | debris_burst | no | A R | pool/constructor diversion and RNG regions; whole-DATA snapshot | L |
| `debris_pick` | 4086bc | debris_burst | no | A R | pool/constructor diversion and RNG regions; whole-DATA snapshot | L |
| `debris_piece` | 408588 | debris_burst | no | A R | pool/constructor diversion and RNG regions; whole-DATA snapshot | L |
| `debris_init` | 408940 | debris_burst | no | A R | pool/constructor diversion and RNG regions; whole-DATA snapshot | L |
| `debris_spin` | 408b74 | debris_burst | no | A R | pool/constructor diversion and RNG regions; whole-DATA snapshot | L |
| `debris_spin_any` | 408bb4 | debris_burst | no | A R | pool/constructor diversion and RNG regions; whole-DATA snapshot | L |
| `travel_step` | 41d9cc | mech_travel | no | G V | replay log of the travel callees (vehicle_control, aim_select, sound 40f09c), or whole-DATA snapshot | L |
| `follow_step` | 41daac | mech_travel | no | G V | replay log of the travel callees (vehicle_control, aim_select, sound 40f09c), or whole-DATA snapshot | L |
| `sleep_step` | 41c418 | mech_travel | no | G V | replay log of the travel callees (vehicle_control, aim_select, sound 40f09c), or whole-DATA snapshot | L |
| `ram_step` | 41e570 | mech_travel | no | G V | replay log of the travel callees (vehicle_control, aim_select, sound 40f09c), or whole-DATA snapshot | L |
| `ram_move` | 41e488 | mech_travel | no | G V | replay log of the travel callees (vehicle_control, aim_select, sound 40f09c), or whole-DATA snapshot | L |
| `dead_step` | 41e554 | mech_travel | no | G V | replay log of the travel callees (vehicle_control, aim_select, sound 40f09c), or whole-DATA snapshot | L |
| `leader_route` | 41fb60 | mech_travel | no | G V | replay log of the travel callees (vehicle_control, aim_select, sound 40f09c), or whole-DATA snapshot | L |
| `point_move` | 41fac4 | mech_travel | no | G V | replay log of the travel callees (vehicle_control, aim_select, sound 40f09c), or whole-DATA snapshot | L |
| `fire_permit` | 41c3c8 | mech_travel | yes | — | — | — |
| `lc_base_update` | 4045c8 | lc_base | no | G A S | whole-DATA snapshot (projectiles, pool, sound, FS:0 record); stack-coordinate shims | XL |
| `projectile_create` | 40a9c4 | lc_base | no | G A S | whole-DATA snapshot (projectiles, pool, sound, FS:0 record); stack-coordinate shims | XL |
| `weapon_record` | 40fe84 | lc_base | yes | — | — (pure leaf; default comparison) | — |
| `key_axes` | 45a4cc | key_axes | yes | — | — | — |
| `mech_part_hit` | 4188c8 | mech_part_hit | no | G A I | whole-DATA snapshot (damage, debris, explosions, messages, sound) | XL |
| `mouse_dispatch` | 452d40 | mouse_events | no | I | not meaningful: GUI gadget dispatch | n/a |
| `mouse_queue` | 453078 | mouse_events | no | O I | RVA region for the event list (append) | S |
| `base_hit_test` | 405038 | hit_test | no | G A I | whole-DATA snapshot (damage, debris, explosions, messages, sound) | XL |
| `mech_hit_test` | 418ba8 | hit_test | no | G A I | whole-DATA snapshot (damage, debris, explosions, messages, sound) | XL |
| `flyer_hit_test` | 421c8c | hit_test | no | G A I | whole-DATA snapshot (damage, debris, explosions, messages, sound) | XL |
| `flyer_part` | 421bb4 | flyer_part | no | G A I | whole-DATA snapshot (damage, debris, explosions, messages, sound) | XL |
| `weapon_eject` | 40f57c | weapon_eject | no | G A R | whole-DATA snapshot (debris pool and object, RNG stream A, FS:0 record) | XL |
| `weapon_damage` | 40ee0c | weapon_eject | no | G A R | whole-DATA snapshot (reaches weapon_eject: debris, RNG stream A) | XL |
| `beam_damage` | 40ee38 | weapon_eject | no | G A R | whole-DATA snapshot (reaches weapon_eject: debris, RNG stream A) | XL |
| `gun_damage` | 40ee90 | weapon_eject | no | G A R | whole-DATA snapshot (reaches weapon_eject: debris, RNG stream A) | XL |
| `targ_damage` | 40ef6c | weapon_eject | no | G A R | whole-DATA snapshot (reaches weapon_eject: debris, RNG stream A) | XL |
| `mech_fly` | 4198f4 | mech_fly | no | G A I | whole-DATA snapshot (position, part hits, debris, cockpit sound) | XL |
| `sfx_play` | 462830 | sfx | no | I G R | not meaningful: plays through the SFX manager (a second phase would sound twice); row draw from RNG stream B | n/a |
| `sfx_play_at` | 4628e0 | sfx | no | I G R | not meaningful: plays through the SFX manager (a second phase would sound twice); row draw from RNG stream B | n/a |
| `sfx_place` | 46299c | sfx | no | I G | not meaningful: positions a playing sound through the SFX manager | n/a |
| `sfx_refresh` | 46297c | sfx | no | I G | not meaningful: refreshes a playing sound through the SFX manager | n/a |
| `sfx_stop` | 462ac4 | sfx | no | I G | not meaningful: stops a sound through the SFX manager | n/a |
| `sfx_rate` | 463114 | sfx | no | I G | not meaningful: sets a playback rate through the SFX manager | n/a |
| `sfx_voice` | 463400 | sfx | no | I G | not meaningful: cockpit radio voice timeline (clock 467828, SFX manager) | n/a |
| `sfx_lip` | 4634b0 | sfx | no | I G | not meaningful: cockpit radio voice timeline (clock 467828, SFX manager) | n/a |
| `sfx_lip_due` | 4634a0 | sfx | no | I G | not meaningful: cockpit radio voice timeline (clock 467828) | n/a |
| `removal_flush` | 478644 | removal | no | G A | whole-DATA snapshot plus destructor/heap diversion (slot +8, 496406, pool_free, FS:0 record) | XL |
| `pool_free` | 471bc8 | removal | no | A X | indirect regions for the pool links, free list and runtime count (as pool_alloc) | M |
| `removal_append` | 478628 | removal | no | A X | region for the removal list entry and count (append) | M |
| `pool_prev` | 471c70 | pool_iter | no | G I | callee-first: pool_next; the debug report (sprintf thunk) only below 1 MiB | M |
| `pool_next` | 471c18 | pool_iter | no | I | replay or exclude the debug report (sprintf thunk 496796, nodes below 1 MiB); otherwise a reader | M |
| `pool_query` | 404bc0 | pool_query | no | G F | callee-first: pool_prev, object_query; comparator ignoring the frame-address EDX | M |
| `map_click` | 44d6e0 | map_click | no | G I O | replay of the sound 462830 and the label update 44dd14; span on the HDDMap object | L |
| `meteor_create` | 409b44 | meteor | no | A G R | pool/constructor diversion, RNG regions, FS:0 record; whole-DATA snapshot | XL |
| `shape_instance` | 490b64 | meteor | no | A | heap/arena allocation diversion (47749c, 47a288) | L |
| `region_absorb` | 40d3ec | region_damage | no | O | spans on the damage word and the region record it writes | S |
| `region_spread` | 40cf44 | region_damage | no | G R O | RNG B region; region_absorb first | M |
| `region_entry` | 40ca58 | region_damage | yes | — | — (2-byte span on the output word; default comparison) | — |
| `region_find` | 40ca8c | region_damage | no | G | callee-first: region_entry | S |
| `weapon_select` | 410708 | panel_weapons | no | O S | span W+1Dh (the prefix holds W+4), RVA region 4d2528h (2 bytes) and a comparator remapping EAX = W; but the live ECX reaches the caller from the exit shim, i.e. from the replacement phase, and no comparator sees ECX: needs ECX in `es2_hook_comparison` or an original-phase ECX for the shim (framework) | M |
| `weapon_find` | 4106ac | panel_weapons | no | V G | replay of the weapon virtual +58h; weapon_select first | L |
| `panel_weapon` | 432a50 | panel_weapons | no | G | callee-first: weapon_find | L |
| `bullet_create` | 40b43c | bullet_ctor | no | G A R S | pool/constructor diversion (pool_alloc, object_init, bullet_init), RNG A region, FS:0 record, entry shim | XL |
| `bullet_init` | 40af6c | bullet_ctor | no | G A | callee-first: object_init, def_lookup; writes the new bullet object | L |
| `def_lookup` | 40ffc8 | bullet_ctor | yes | — | — (pure table walk; default comparison) | — |
| `fire_alloc` | 46b430 | fire | no | A G | pool allocation diversion (pool_alloc; pool_prev on a full pool) | L |
| `fire_create` | 46b48c | fire | no | A G I | constructor diversion (object_init, memset thunk), sound 462830, removal_append | XL |
| `fire_release` | 46b62c | fire | no | G A | callee-first: pool_prev; region for the removal list (removal_append) | M |
| `results_writer` | 42412c | results_writer | no | I G A | not meaningful: writes `results.dat`/`mission.var` through the stream class (a second phase would write them twice); FS:0 record | n/a |
| `score_sum` | 423e88 | results_writer | no | G | callee-first: vehicle_value, pool_prev and the objective hooks | M |
| `vehicle_value` | 418e60 | results_writer | no | G O | callee-first: list_append, part_ratio, damage_score; two scratch frame dwords | M |
| `list_append` | 426ac8 | results_writer | no | O X | the dword at [4a9e6ch] + 4 × the word [4a9e6ah] (runtime-indexed: a resolver snapshot region) and the count word 4a9e6ah as an RVA region | M |
| `flight_key` | 45a324 | flight_keys | no | O I | regions for the key-state arrays 4d2394/4d23a3/4d23b2, the Alt word and the demo quit word; input side | S |
| `rotate_words` | 47ff8c | transform_helpers | no | O S | span on the 4-byte output (may overlap the inputs); the exit shim loads the ECX in both modes | S |
| `matrix_point` | 4801d4 | transform_helpers | no | O S | 12-byte output span (may overlap the inputs); the exit shim loads the ECX in both modes | S |
| `region_hit` | 40da38 | region_break | no | G A | callee-first: region_absorb, region_spread, region_break; damage word in scratch | L |
| `region_break` | 40d434 | region_break | no | G A R I | explosion/debris/fire constructors, node virtual +20h, FS:0 record, sound; whole-DATA snapshot | XL |
| `hud_key` | 432bc8 | hud_keys | no | G V I A | eleven panel gadget virtuals, display requests, F11 dialog allocation, FS:0 record | XL |
| `button_press` | 438d9c | hud_keys | no | G V | widget virtual and the clock 467828 | L |
| `button_release` | 4530bc | hud_keys | no | G | the clock 467828; release schedule region | M |
| `command_key` | 45feb8 | commands | no | G V I A S | view requests, dialogs, WinHelp and the joystick summary as guest calls, vehicle virtuals +74h/+3Ch/+2Ch, FS:0 record; whole-DATA snapshot | XL |
| `playback_key` | 45fe6c | commands | no | G S | the playback calls 4667c8/467ad8; entry shim (caller's EBX high word into AX) | M |
| `stream_open` | 4730e8 | vfs | yes | — | — | — |
| `stream_close` | 47307c | vfs | yes | — | — | — |
| `stream_read` | 4735b8 | vfs | yes | — | — | — |
| `stream_seek` | 4733a4 | vfs | yes | — | — | — |
| `stream_tell` | 473368 | vfs | yes | — | — | — |
| `stream_size` | 473334 | vfs | yes | — | — | — |
| `vol_lookup` | 468064 | vfs | yes | — | — | — |
| `member_lookup` | 47cdf8 | vfs | yes | — | — | — |
| `explosion_dtor` | 407e48 | destructors | no | G V A | callee-first: removal_append; heap free 477548, shape virtual +18h, FS:0 record | L |
| `fire_dtor` | 46b574 | destructors | no | G V A I | sound sfx_stop(33h); heap free 477548, shape virtual +18h, FS:0 record | XL |
| `beam_dtor` | 40ba94 | destructors | no | G V A | arena free 47758c, heap free 477548, shape virtual +18h, FS:0 record | L |
| `debris_dtor` | 40f7e6 | destructors | no | G V A | heap free 477548, shape virtual +18h, FS:0 record | L |
| `flat_dtor` | 41bd46 | destructors | no | G V A | heap free 477548, shape virtual +18h, FS:0 record | L |
| `meteor_dtor` | 424034 | destructors | no | G V A | heap free 477548, shape virtual +18h, FS:0 record | L |
| `bullet_dtor` | 40b64d | destructors | no | G V A | heap free 477548, shape virtual +18h, FS:0 record | L |
| `rocket_dtor` | 40ab8d | destructors | no | G V A | heap free 477548, shape virtual +18h, FS:0 record | L |
| `shape_dtor` | 490bdc | destructors | no | G A | callee-first: shape_base_dtor; arena free 47758c, heap free 477548 | L |
| `shape_base_dtor` | 48be20 | destructors | no | A O | heap free 477548 on flag bit 0 (allocation diversion); with flag 0 it still decrements the counter behind the pointer [6cbee4h] (a two-hop store no region declares) and writes the vtable word [A] | M |
| `weapon_frame` | 410b40 | weapon_slots | no | G V I O F S | callee-first: the whole weapon_slots group, select_step, fire_group, weapon_select; replay of the weapon virtual +50h and the lamp 441fa4; the globals 4d1a5c/4d2528; a comparator for EDX (the gauge record's frame address); exit shim | L |
| `ready_step` | 410a3c | weapon_slots | no | G S | callee-first: selected_ready, select_step, weapon_select; exit shim | L |
| `selected_ready` | 410a04 | weapon_slots | no | G S | callee-first: chain_ready; exit shim | L |
| `chain_ready` | 410970 | weapon_slots | no | G V S | replay of the weapon virtuals +2Ch/+60h, weapon_range (compare-capable) and the recursion; exit shim | L |
| `latch_set` | 410f04 | weapon_slots | yes | — | — (span W+14h (argument 0); EAX (W+14h) compared relative to it) | — |
| `link_toggle` | 410f14 | weapon_slots | no | X S | the link bytes of two listed weapons (runtime-indexed); the exit shim hands the caller the replacement phase's ECX, which no comparator sees (as weapon_select) | M |
| `member_toggle` | 410cd0 | weapon_slots | no | X S | the membership word at a runtime index of the current group's table; exit shim | M |
| `gauge_sync` | 4420c8 | weapon_slots | no | G I S | callee-first: gauge_mode, gauge_link; the lamp 441fa4 (EventPort message) as a replay; exit shim | L |
| `gauge_state` | 442120 | weapon_slots | no | O S | fixed region 4d1a5c..4d1a5f would do; kept refused, the exit shim hands the caller the replacement phase's ECX (EDX/ECX high parts), which no comparator sees | M |
| `gauge_link` | 442054 | weapon_slots | no | G S | callee-first: link_toggle; exit shim | M |
| `gauge_button` | 442154 | weapon_slots | no | G I S | callee-first: gauge_mode, gauge_link; the lamp 441fa4 as a replay; exit shim | L |
| `gauge_mode` | 441f90 | weapon_slots | yes | — | — (span H+0B1h..0B2h (argument 0); EAX (H) compared relative to it) | — |
| `collision_walk` | 427c68 | obstacle_walk | yes | — | — (comparator on EAX and DX; the lazy matrix cache of the entries is filled by the original phase, as for object_query) | — |
| `shape_cell` | 427360 | obstacle_walk | yes | — | — (writes only its frame; cell is read-only) | — |
| `convoy_leader` | 46a5bc | obstacle_walk | yes | — | — (pure reader) | — |
| `map_areas` | 44d450 | hdd_orders | no | G I | callee-first: map_mode, button_press; replay of the label 44dc6c and the mouse position 486e7c; span on the HDDMap object | L |
| `map_mode` | 44d9f4 | hdd_orders | no | G I | replay of the cursor 433e3c and the labels 44dc6c/44dd14; span on the HDDMap object, the gadget bytes | L |
| `wingman_select` | 44da98 | hdd_orders | no | G V X | callee-first: map_mode; replay of the lamps 44ede0 (button virtual +4), 44ee00/44ee24; the record byte at a runtime index | L |
| `order_cancel` | 44dc10 | hdd_orders | no | G X | callee-first: map_mode, wingman_select; the record byte at a runtime index | M |
| `slider_event` | 4483a0 | throttle | no | G | callee-first: slider_base; replay of the joystick summary 477904 (it clears its own block) | M |
| `slider_base` | 43904c | throttle | no | V | replay log of the three gadget virtuals (handler, class table, sub-object) | L |
| `shield_step` | 413af8 | throttle | yes | — | — (span S[0..5]; comparator on EAX relative to S and the low word of EDX, whose high word is entry residue) | — |
| `panel_frame` | 4327ac | panel_frame | no | G V I S | replay of the eleven panel draw virtuals, the port virtual +14h and the H3 callees (scroll zones, startup sequence with EventPort messages, jolt, bump, cursor, button releases, nav marker); callee-first: cockpit_valid; span on the two cockpit bytes H+23Eh and H+210h; entry shim at the original stack coordinate | XL |
| `subject_status` | 412dc4 | objective_preds | no | G V | callee-first: group_state (replay of the actor virtual +40h); 412bd0/412bbc are compare-capable | M |
| `subject_group` | 412bd0 | objective_preds | yes | — | — (pure reader) | — |
| `subject_actor` | 412bbc | objective_preds | yes | — | — (pure reader, twin of 412bd0) | — |
| `group_down` | 412be4 | objective_preds | yes | — | — (pure reader) | — |
| `route_flag` | 41324c | objective_preds | yes | — | — (pure reader) | — |
| `state_reached` | 413920 | objective_preds | no | G V | callee-first: group_state (replay of the actor virtual +40h) | M |
| `group_safe` | 413950 | objective_preds | no | G V | callee-first: state_reached and actor_safe | M |
| `group_marked` | 412d90 | objective_preds | yes | — | — (pure reader) | — |
| `group_flagged` | 412c58 | objective_preds | yes | — | — (pure reader) | — |
| `bounds_margin` | 413780 | objective_preds | yes | — | — (its only callee bounds_outside is a pure reader, as for bounds_check) | — |
| `logic_frame` | 45f570 | logic_frame | no | G V I A O S | the whole simulation step: replay or callee-first comparison of the 19 nested hooks and the virtuals +14h/+18h (pools, groups, input, timers, triggers, vehicles, outcome), the view request 45de30, the alert dialog (47749c, 455950, +8, 4543b0) and the runtime record 49630c/FS:0; whole-DATA snapshot with the sound, pool and dialog effects diverted; entry shim at the original stack coordinate | XL |
| `proj_record` | 40ffb0 | weapon_leaves | yes | — | — (pure reader of `[4a98fch]`; EDX from the entry registers) | — |
| `pitch_point` | 49295c | weapon_leaves | yes | — | — (writes nothing; its callees dist2d_delta and vector_angle are pure and run through the bridge in their modes) | — |
| `damage_words` | 40501c | weapon_leaves | yes | — | — (4-byte RVA region `4a95f2h`..`4a95f5h`; the high words of EAX/EDX from the entry registers) | — |
| `bullet_charge` | 40b5a0 | weapon_leaves | no | G A R I | callee-first: bullet_create (pool, RNG A, sound, FS:0 record, entry shim); writes the new bullet's charge word and lock | XL |
| `rocket_init` | 40a948 | weapon_leaves | no | G A | callee-first: object_init, def_lookup; writes the new rocket object and the runtime counter | L |
| `flyer_route` | 4224c4 | flyer_ai | no | G S | flight_model first (below flyer_steer), replay of route_advance (word R); stack-residue entry shim | L |
| `flyer_altitude` | 422108 | flyer_ai | yes | — | — (no write; callees multiply_q8, vector_angle, 422098 pure) | — |
| `flyer_pitch` | 422098 | flyer_ai | yes | — | — (no write; callee heading_cos pure) | — |
| `flyer_throttle` | 422260 | flyer_ai | yes | — | — (span A[21Ch..234h], the written word and the word A+233h it reads) | — |
| `group_fire_any` | 411a1d | part_helpers | no | G V | callee-first: group_fire (replay of the weapon virtuals +48h/+34h/+28h) | L |
| `group_fire_sel` | 41194a | part_helpers | no | G V | callee-first: group_fire (replay of the weapon virtuals +48h/+34h/+28h) | L |
| `part_list_dead` | 40cf10 | part_helpers | yes | — | — (pure reader) | — |
| `part_group_dead` | 40d9f8 | part_helpers | yes | — | — (its only callee 40cf10 is a pure reader) | — |
| `part_group_max` | 40dc58 | part_helpers | yes | — | — (pure reader) | — |
| `mission_guard` | 4240f4 | part_helpers | yes | — | — (pure reader of three image words; EDX is the entry EDX) | — |
| `actor_offsets` | 411a74 | part_helpers | no | G O | spans on the two 6-byte records and a comparator that maps memset's EAX (the second record) to the clone, or the native memset | M |
| `command_queue` | 45a498 | command_queue | no | O I | the store goes to 4d20c4 + 2 * the unbounded dword index 49eaa0, so no region can declare it (a window region would hide stores outside it); input side | M |
| `cockpit_sync` | 41b130 | cockpit_sync | no | G I O S | replay or callee-first comparison of shield_balance, dist_gate, weapon_frame (the player weapon and fire tree), the three helpers, timer_word_step and aim_point; replay of the EnergyPoolGauge 4450f8, the weapon word 40e484, the HUD record set 43d9b4 and the HUD fields 434a24; spans on the vehicle fields, the cockpit target, the reference counts of two targets and the globals 4A9CE8h/4D2504h; whole-DATA snapshot with the fire effects diverted; entry shim at the original stack coordinate | XL |
| `throttle_record` | 447df8 | cockpit_sync | yes | — | — (no memory access; EAX = slot + 0B1h, EDX the entry EDX) | — |
| `throttle_gauge` | 447da8 | cockpit_sync | no | V | On a set-point change the slider slot +14h reaches slider_event, slider_base, the ThrottleGauge handler and the lever word 49a06e; a guard on slider_event would skip those calls, but a snapshot set needs at least one region (hook.c); otherwise spans G+0B1h..0B8h and the redraw byte [[G+18h]+1Fh] (one-hop region) | M |
| `hud_record` | 43dbb0 | cockpit_sync | yes | — | — (no memory access, twin of 447df8) | — |
| `rotation_compose` | 47f058 | rotation_helpers | yes | — | — (spans a 20, b 20, out 20 bytes; the comparator remaps EAX = out to the clone and compares EDX; ECX comes from the exit shim, not compared; overlapping arguments are skipped) | — |
| `rotation_place` | 47f4f4 | rotation_helpers | yes | — | — (spans a 20, b 32, out 32 bytes; comparator as rotation_compose) | — |
| `render_root` | 45fca8 | render_root | no | G V I S | replay or callee-first comparison of stage_dispatch (the removal flushes), panel_frame, vector_copy and cockpit_sync; replay of the H3 draws (detail settings, jolt point, camera setup, terrain, binning, view test, view transform, sky/horizon, eye cell objects, HUD draw, port frames, widget draw); spans on the view-block words and the canvas origin; entry shim at the original stack coordinate | XL |
| `stage_dispatch` | 401d94 | render_root | no | G A | replay of the stage callbacks (removal_flush per pool, the stage-6 record flush with file I/O) | L |
| `vector_copy` | 40707c | render_root | yes | — | — (span on the 12-byte destination, comparator for EAX = D; disjoint vectors, as at the only caller) | — |
| `targ_aim` | 40e4dc | cold_paths | no | G V O S | replay of the target virtuals +24h/+58h/+84h, targ_next, timer_dword_step, euler_matrix, transform_point; spans on the weapon record (W+7Dh/+7Fh/+81h/+82h/+86h) and the out point; EDX is a frame residue | L |
| `targ_next` | 40e4ac | cold_paths | no | V O | replay of the target's virtual +80h; the word W+7Dh it writes as a span | M |
| `missile_steer` | 40a488 | cold_paths | no | G O | callee-first (multiply_q8, movement_step are pure); spans on the missile pose O+0Ch..+10h/+32h and the two axis words of the record | S |
| `ally_nearest` | 423974 | cold_paths | yes | — | — (read-only query; its only callee dist_gate is pure; the comparator checks EAX and EDX) | — |
| `turbo_engage` | 40f09c | cold_paths | no | I | the sound trigger sfx_play (its SFX manager stays original); the latch dword T+81h as a span | M |
| `member_assess` | 41b804 | cold_paths | no | G V R | callee-first (actor_safe, pool_prev readers; dist_gate pure; random_damage writes the parts through RNG A and the virtual +74h) | L |
| `dts_load` | 47a144 | assets | yes | — | — (pure description of the bitmap and array loads, compared with the original's objects from EAX; other classes and unserved streams SKIP) | — |
| `bitmap_new` | 481cd4 | assets | yes | — | — (description of the object, compared with the original's from EAX; EDX = FS:0 at entry) | — |
| `bitmap_read` | 481d8c | assets | yes | — | — (fields into the clone, pixel and colour bytes against the file; allocation pointers excluded) | — |
| `bitmap_array_new` | 482d88 | assets | yes | — | — (the array and its children described and compared from EAX) | — |
| `bitmap_array_read` | 482e7c | assets | yes | — | — (count into the clone, the children from the original's pointer array) | — |
| `stream_destroy` | 472f64 | assets | yes | — | — (the counter as a region; decoder objects, the delete flag and an owned handle SKIP) | — |
| `prefs_video` | 45e510 | config | no | I G | not meaningful: reads data\prefs.cfg through _fopen/_fread/_fclose and sets the display-mode globals; a second phase would read the file again | n/a |
| `prefs_load` | 459770 | config | no | I G V A | not meaningful: reads data\prefs.cfg through the read stream, calls the prefs callbacks (sound start/stop, voice, lever) a second time; FS:0 record | n/a |
| `prefs_store` | 459b94 | config | no | I G A | not meaningful: rewrites data\prefs.cfg through the read and write streams (a second phase would write it twice); FS:0 record | n/a |
| `keyjoy_load` | 45b7a8 | config | no | I G | not meaningful: GetPrivateProfileStringA on data\keyjoy.cfg and _stricmp; the flags it sets are never cleared | n/a |
| `sound_config` | 46379c | config | no | I G A | not meaningful: GetPrivateProfileStringA on data\sound.cfg, _stricmp/_atol; a constructor (construction counter) | n/a |
| `tape_save` | 45cca4 | config | no | I G | not meaningful: copies a launch file into the tape with _fopen/_fseek/_ftell/_rewind/_fread/_fwrite (a second phase would append the record twice) | n/a |
| `tape_restore` | 45cd5c | config | no | I G | not meaningful: reads a tape header record and rewrites the member file (a second phase would read the next record) | n/a |
| `pool_create` | 471ad8 | mission_load | no | G A | allocation: new[] `4774f0` and memset `49681a` (a second phase would allocate the block again) | L |
| `pool_nth` | 471cb0 | mission_load | yes | — | — (reader; its callee pool_prev (and pool_next) stores nothing on the heap-node path, flag only) | — |
| `subject_resolve` | 425348 | mission_load | yes | — | — (reader; callee pool_nth (compare-capable), flag only) | — |
| `action_copy` | 411b90 | mission_load | yes | — | — (span A+1BAh..1E1h (argument 0); EAX (A+1CEh) compared relative to it, EDX (ids+20) equal) | — |
| `condition_init` | 415068 | mission_load | yes | — | — (regions behind [C+4] (29 words) and [C] (22 words), one hop, in place; default comparison) | — |
| `region_init` | 423358 | mission_load | yes | — | — (span R+0..9 (argument 0); EAX (R) compared relative to it) | — |
| `timer_init` | 423104 | mission_load | yes | — | — (span T+0..30h (argument 0, timer_arm's dword T+2Dh included); EDX (T+2Ch) compared relative to it) | — |
| `group_array` | 423d38 | mission_load | no | G A | the runtime's array construction `49667c` allocates and constructs | L |
| `initial_damage` | 4178e8 | mission_load | no | G V R | RNG stream A, random_damage and the actor's damage virtual +74h (vehicle_part: effects, possible trigger) | XL |
| `light_pools` | 4076e4 | mission_pools | no | G A | allocation: new `47749c`, pool_create and new[] `4774f0`; the light table constructor `406e44` and the construction count (a second phase would allocate and construct again) | L |
| `smoke_pools` | 4090cc | mission_pools | no | G A | allocation: new `47749c`, pool_create and new[] `4774f0`; the smoke-ball set-up `408fb8` loads resources (a second phase would allocate and load again) | L |
| `projectile_pools` | 40a060 | mission_pools | no | G A | allocation: new `47749c`, pool_create and new[] `4774f0` (a second phase would allocate again) | L |
| `weapon_fit` | 4175dc | mission_weapons | no | G V A | allocation of the weapon system (new `47749c`, the nested player_weapons/weapon_factory), the runtime record `49630c`/FS:0, the shape virtual +20h and the part-table helpers write the actor and the shared weapon records | XL |
| `player_weapons` | 4104ec | mission_weapons | no | G A | the weapon factory builds every weapon; 49667c and new[] `4774f0` allocate the fire groups; fire_group; construction count | L |
| `weapon_factory` | 40fff8 | mission_weapons | no | G A S | allocation (new[] `4774f0`, the arena calls `474bbc`/`474be4`, the allocator `47a2c8`), the weapon constructors, the runtime record `49630c`/FS:0; the seventh argument slot and the projectile-record residue are outside the framework's six slots | L |
| `hdd_key` | 449ff4 | hdd_keys | no | G V | callee-first: button_press; replay of the page switch `44a60c`, the status view `450b88` and the active page's key slot +10h (the map page `44cc68`: map modes, picks, sends) | M |
| `flush_buildings` | 405ea4 | stage_flush | no | G A | the callee removal_flush (whole-DATA snapshot plus destructor/heap diversion: slot +8, 496406, pool_free, FS:0 record) | XL |
| `flush_lights` | 4077e8 | stage_flush | no | G A | the callee removal_flush (whole-DATA snapshot plus destructor/heap diversion: slot +8, 496406, pool_free, FS:0 record) | XL |
| `flush_explosions` | 407b3c | stage_flush | no | G A | the callee removal_flush (whole-DATA snapshot plus destructor/heap diversion: slot +8, 496406, pool_free, FS:0 record) | XL |
| `flush_debris` | 4083e0 | stage_flush | no | G A | the callee removal_flush (whole-DATA snapshot plus destructor/heap diversion: slot +8, 496406, pool_free, FS:0 record) | XL |
| `flush_smoke_balls` | 408f84 | stage_flush | no | G A | the callee removal_flush (whole-DATA snapshot plus destructor/heap diversion: slot +8, 496406, pool_free, FS:0 record) | XL |
| `flush_smoke` | 408f9c | stage_flush | no | G A | the callee removal_flush (whole-DATA snapshot plus destructor/heap diversion: slot +8, 496406, pool_free, FS:0 record); then 408f84 | XL |
| `flush_attached` | 409904 | stage_flush | no | G A | the callee removal_flush (whole-DATA snapshot plus destructor/heap diversion: slot +8, 496406, pool_free, FS:0 record) | XL |
| `flush_meteors` | 409a1c | stage_flush | no | G A | the callee removal_flush (whole-DATA snapshot plus destructor/heap diversion: slot +8, 496406, pool_free, FS:0 record) | XL |
| `flush_projectiles` | 40a108 | stage_flush | no | G A | the callee removal_flush (whole-DATA snapshot plus destructor/heap diversion: slot +8, 496406, pool_free, FS:0 record) | XL |
| `flush_mechs` | 41544c | stage_flush | no | G A | the callee removal_flush (whole-DATA snapshot plus destructor/heap diversion: slot +8, 496406, pool_free, FS:0 record) | XL |
| `flush_flyers` | 4215a8 | stage_flush | no | G A | the callee removal_flush (whole-DATA snapshot plus destructor/heap diversion: slot +8, 496406, pool_free, FS:0 record) | XL |
| `flush_fires` | 46b188 | stage_flush | no | G A | the callee removal_flush (whole-DATA snapshot plus destructor/heap diversion: slot +8, 496406, pool_free, FS:0 record) | XL |
| `object_position` | 46b905 | manager_slots | yes | — | — (leaf without writes, flag only) | — |
| `shape_word` | 46b910 | manager_slots | yes | — | — (leaf without writes, flag only) | — |
| `sequence_next` | 478760 | manager_slots | yes | — | — (leaf without writes, flag only) | — |
| `sequence_prev` | 47877c | manager_slots | yes | — | — (leaf without writes, flag only) | — |
| `sequence_reach_up` | 47878c | manager_slots | yes | — | — (leaf without writes, flag only) | — |
| `sequence_reach_down` | 4787b8 | manager_slots | yes | — | — (leaf without writes, flag only) | — |
| `cyclic_next` | 4787e4 | manager_slots | yes | — | — (leaf without writes, flag only) | — |
| `cyclic_prev` | 478804 | manager_slots | yes | — | — (leaf without writes, flag only) | — |
| `cyclic_reach_up` | 478820 | manager_slots | yes | — | — (leaf without writes, flag only) | — |
| `cyclic_reach_down` | 47882c | manager_slots | yes | — | — (leaf without writes, flag only) | — |
| `gun_angle` | 403594 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `base_radius` | 4035a4 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `base_sight_radius` | 4035b8 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `base_part_word` | 40360c | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `base_next_part` | 403624 | actor_leaves | yes | — | — (span: the out dword (argument 2); EDX (out) compared relative to it) | — |
| `base_in_front` | 405308 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `base_decide` | 406cfe | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_armour` | 411a4c | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_target_scale` | 411a54 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_angle` | 411a5c | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_order` | 411a64 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_keys` | 411a6c | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_aim_node` | 411a9c | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_radius` | 411aa4 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_sight_radius` | 411aac | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_move` | 411ab4 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_engage_roll` | 411ac0 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_in_front` | 411acc | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_mount_word` | 411b04 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_part_word` | 411b10 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `actor_next_part` | 411b1c | actor_leaves | yes | — | — (span: the out dword (argument 2)) | — |
| `mech_angle` | 415488 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `mech_condition` | 415504 | actor_leaves | yes | — | — (callee part_condition (pure, compare-capable)) | — |
| `mech_radius` | 415518 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `mech_sight_radius` | 41552c | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `mech_part_word` | 415540 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `mech_next_part` | 415558 | actor_leaves | yes | — | — (span: the out dword (argument 2)) | — |
| `mech_mount_word` | 4155ac | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `flyer_target_scale` | 421584 | actor_leaves | yes | — | — (callee multiply_q10 (pure, compare-capable)) | — |
| `flyer_keys` | 4215c0 | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `flyer_condition` | 421e84 | actor_leaves | yes | — | — (callee part_condition (pure, compare-capable)) | — |
| `actor_mark` | 42200c | actor_leaves | yes | — | — (span: the byte O+B1h (argument 0); EAX (O+92h) compared relative to it) | — |
| `object_zero` | 4786cb | actor_leaves | yes | — | — (leaf without writes, flag only) | — |
| `equip_ready` | 40ecd4 | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `beam_ready` | 40ecdc | weapon_methods | yes | — | — (no writes; comparator on EAX and DX (the EDX high word is the caller's EBX's on one path)) | — |
| `gun_ready` | 40ed6c | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `elf_ready` | 40eda0 | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `weapon_trigger` | 40f8ad | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `elf_trigger` | 40e680 | weapon_methods | yes | — | — (span W+47h..85h (argument 0) and the frame word [[W+14h]] as a one-hop region; default comparison (EAX = EDX, values)) | — |
| `laew_trigger` | 40e71c | weapon_methods | no | G O | the button latch 45b734 (platform, writes 4D22F6h/4D22FEh); writes W+46h, dword W+81h | M |
| `equip_trigger` | 40e678 | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `beam_step` | 40f00c | weapon_methods | no | G O | weapon_step 40ef94 (refuses compare); writes W+43h, word W+7Dh | M |
| `elf_step` | 40f3d8 | weapon_methods | no | G R X | timer_word_step 467a48, beam_step 40f00c; the frame word [[W+14h]] (runtime-indexed), W+47h/W+48h/W+84h | L |
| `turbo_step` | 40f0d0 | weapon_methods | no | G O | weapon_step 40ef94; dwords W+7Dh/W+81h | M |
| `beam_adjust` | 40f48c | weapon_methods | yes | — | — (span W+34h..7Ch; EAX (W+3Bh) compared relative to it) | — |
| `beam_preset` | 40f4d8 | weapon_methods | yes | — | — (span W+43h..7Ch; EAX (W) and EDX (W+43h) compared relative to it) | — |
| `beam_fill` | 40f4f0 | weapon_methods | yes | — | — (span W+43h..7Ch; EAX (W) and EDX (W+43h) compared relative to it) | — |
| `beam_drain` | 4116ad | weapon_methods | yes | — | — (span W+34h..7Ch; EAX (W+3Bh) and EDX (W+43h) compared relative to it) | — |
| `weapon_priority` | 4111e2 | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `beam_priority` | 40f504 | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `gun_clear` | 4115f8 | weapon_methods | yes | — | — (span W+7Bh..7Ch; EAX (W) compared relative to it) | — |
| `beam_clear` | 4116cf | weapon_methods | yes | — | — (span W+34h..7Eh; EAX (W+3Bh) compared relative to it) | — |
| `weapon_stock` | 4111e9 | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `gun_stock` | 40f520 | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `weapon_gauge_match` | 40df00 | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `beam_gauge_match` | 40df08 | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `gun_gauge_match` | 40df1c | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `weapon_spent` | 40f8a4 | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `beam_spent` | 40ed34 | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `gun_spent` | 40ed48 | weapon_methods | yes | — | — (leaf without writes, flag only) | — |
| `weapon_ammo` | 40e62c | weapon_methods | yes | — | — (span [p] (argument 1, 2 bytes; a null p skips the comparison); AX equal, EAX's high word p's in each phase) | — |
| `gun_ammo` | 40e644 | weapon_methods | yes | — | — (span [p] as weapon_ammo; EDX (p) compared relative to it, EAX lo(p, 5) or lo(T, word T+2)) | — |
| `mech_ctor` | 415bb0 | mission_ctors | no | G A S | the shape set-up, part tables, tracks, flat objects (pool_alloc, object_init), script_start and the part-table loaders write the actor, the actor list and the construction count; the runtime record `49630c`/FS:0; the pushed type dword carries the caller's ESI high word | XL |
| `flyer_ctor` | 4215f4 | mission_ctors | no | G A | the shape set-up (object_init), part tables, script_start and the matrix hooks write the flyer, the actor list and the construction count | L |
| `building_factory` | 405314 | mission_ctors | no | G A S | pool_alloc and the class constructors (shape set-ups, 49667c, new[]), tracks, the runtime record `49630c`/FS:0; case 0 works on the caller's EDI | XL |
| `group_ctor` | 423b34 | mission_ctors | no | G A | new[] of the member array, pool_nth, and the join methods (route_point, terrain_height, the formation slot +78h) place the members | L |
| `base_marks` | 405db0 | mission_ctors | no | G | the terrain footprint `47136c` writes the terrain cells | L |
| `weapon_target_word` | 40e484 | bridged_leaves | yes | — | — (span: the word W+7Dh (argument 0); EAX (W) compared relative to it) | — |
| `list_next` | 411e40 | bridged_leaves | yes | — | — (leaf without writes, flag only) | — |
| `list_prev` | 411e60 | bridged_leaves | yes | — | — (leaf without writes, flag only) | — |
| `r_key_toggle` | 41b468 | bridged_leaves | no | G V I S | replay of the sound row 462830, the two message cancels 435ac8 and the EventPort's slot 0 (message queue, H3); byte P+96h as a span; entry shim at the original stack coordinate (message residue) | L |
| `nav_marker_set` | 434974 | bridged_leaves | yes | — | — (region: H+25Eh..+26Bh of the cockpit [49B088h], one hop) | — |
| `throttle_handler` | 447e08 | bridged_leaves | yes | — | — (span G+0..C1h (argument 0); EDX (G+18h+4k) compared relative to it; a gadget table beyond 2Ah entries reads past the span) | — |
| `view_pref_toggle` | 45fe38 | bridged_leaves | yes | — | — (region: the image words 4D2518h..4D2521h) | — |
| `matrix_ctor` | 495132 | bridged_leaves | yes | — | — (empty constructor; callees rotation_ctor (compare-capable) and 4953c4 (pure)) | — |
| `rotation_ctor` | 495194 | bridged_leaves | yes | — | — (empty constructor; callee rotation_base_ctor (compare-capable)) | — |
| `rotation_base_ctor` | 49514f | bridged_leaves | yes | — | — (leaf without writes, flag only) | — |
| `ts_obj_ctor` | 402278 | object_shapes | no | G A | new 47749c and the shape instance anim_instance_ctor write the object, the arrays and the construction count; the runtime record `49630c`/FS:0; memset | L |
| `det_anim_shape` | 40332c | object_shapes | no | G A | ts_obj_ctor writes the object and the construction count; the two vtables and O+41h | L |
| `det_anim_static` | 403368 | object_shapes | no | G A | object_init writes the object and the construction count; the two vtables and O+41h | L |
| `det_anim_record` | 4033a4 | object_shapes | no | G A | ts_obj_ctor writes the object and the construction count; the two vtables and O+41h | L |
| `anim_instance_ctor` | 478838 | object_shapes | no | G A | shape_instance, new[], 49667c with the element constructors and node_matrices build the instance and its arrays; the global rotation `6C56A8h`; memset | L |
| `object_track` | 402374 | object_shapes | no | G A | new 47749c, thread_ctor, track_clip, track_rate and thread_add build the thread and sort the instance's slots; the runtime record `49630c`/FS:0 | L |
| `thread_add` | 478a3c | object_shapes | no | G O | writes the first free of the ten slots I+1Ah and calls track_sort, which reorders them through the threads' node words | M |
| `thread_ctor` | 478e48 | object_shapes | no | G | track_clip writes the thread through its shape node (slot +20h) and clip_pose; memset of the state block | M |
| `shape_word_block` | 402fc0 | object_shapes | no | G A | the arena allocation `47a288` and the CW3220 memcpy fill the block; the construction count | M |
| `collision_load` | 40cd88 | collision_data | no | G A I | the stream object built and destroyed (472f14, stream_destroy), the runtime record `49630c`/FS:0, the slot counter word 4987DEh, the nested reader writes the slot and its groups; the assertion 479c78 | L |
| `collision_read` | 40ccf8 | collision_data | no | G V A I | stream reads (virtual +18h), new 4774f0, the nested readers write the groups and volumes; the assertion 479c78 | L |
| `collision_entry` | 40cc50 | collision_data | no | G V A I | stream reads, new[] 49667c, the nested volume readers | L |
| `collision_volume` | 40cc14 | collision_data | no | G V I | a stream read into V+6, the nested collision_points | L |
| `collision_points` | 40c7c4 | collision_data | no | G V A I | stream reads, new[] 49667c (the sphere array) | L |
| `collision_bounds` | 40ccc8 | collision_data | no | G X | collision_radius (compare-capable) writes the centre and the radius word of every volume of [G+6] (runtime-indexed array) | M |
| `collision_radius` | 40c5d0 | collision_data | yes | — | — (span: argument 1, 6 bytes, the centre; callee dist3d_delta (compare-capable, pure)) | — |
| `collision_scan` | 40cec0 | collision_data | no | G A X | the original 40ce28/40c834 under collision_match allocate a sphere array and copy the weapon's spheres into the matched volume; collision_bounds writes its volumes | L |
| `collision_match` | 40ce78 | collision_data | no | G A X | the original 40ce28 → 40c834 (new[] 49667c, the copy into a runtime-indexed volume) | L |
| `actor_load` | 4253d8 | mission_actors | no | G V I A | the script stream (47ad40, 47af98, the virtual read +18h, 47ad78) reads a file; pool_alloc and the constructors, weapon_fit, new[] of the orders and objectives build the actors and tables; the building virtual +10h; the runtime record `49630c`/FS:0 | XL |
| `dmg_load` | 40d160 | damage_data | no | G A I | the stream constructor `472f14`, the error report `479c78`, the nested dmg_read and stream_destroy read the DMG file and allocate; the runtime record `49630c`/FS:0; the slot counter `498854` | XL |
| `dmg_read` | 40d09c | damage_data | no | G V A I | the stream's read method (slot +18h), new[] `4774f0` and the nested dmg_region_read fill the descriptor and its blocks | L |
| `dmg_region_read` | 40cff8 | damage_data | no | G V A I | the stream's read method (slot +18h) and new[] `4774f0` (the link list) | L |
| `dmg_region_copy` | 40d050 | damage_data | no | G A | `_memcpy` `49680e` and new[] `4774f0` (a second phase would allocate again) | L |
| `dmg_capacity_set` | 40d128 | damage_data | no | X | writes through a runtime-indexed pointer (the capacity array [T+4] at an index read from memory), n entries | M |
| `dmg_region_set` | 40d240 | damage_data | no | G A X | the nested dmg_region_copy allocates; regions at runtime indices | L |
| `dmg_part_store` | 40d284 | damage_data | no | X | writes n regions at runtime indices of the region array [T] | M |
| `col_part_collect` | 40cb84 | damage_data | no | G X | the nested col_part_gather writes three count-dependent arrays | M |
| `col_part_gather` | 40cafc | damage_data | no | X | writes three arrays whose length is the record's count word (read from memory, reread every pass) | M |
| `damage_state_ctor` | 40d2cc | damage_data | no | G A | new[] `4774f0` three times and `_memset` `49681a` | L |
| `damage_state_bind` | 40d354 | damage_data | no | G A | the runtime record `49630c`/FS:0, new `47749c` and new[] `4774f0` of the shared block `[498864]`, the construction count | L |
| `mech_join` | 417aa8 | group_joins | no | G V | route_point, terrain_height and the formation slot +78h (formation_mech) through the bridge; writes the actor's group, slot, angles and position | L |
| `flyer_join` | 421ee8 | group_joins | no | G V | route_point and the formation slot +78h (formation_flyer) through the bridge; writes the flyer's group, slot, angles and position | L |
| `building_join` | 405c3c | group_joins | no | G V | route_point, the slots +78h (formation_base) and +10h (shape_word), footprint_marks (terrain cell flags) and terrain_height through the bridge; writes the building's group, slot, angles and position | L |
| `footprint_marks` | 470ed4 | group_joins | no | G X | dist_gate and cell_flag_set through the bridge; the cell flags it sets are runtime-indexed bytes of `[T+0F0h]` | M |
| `cell_flag_set` | 470ddc | group_joins | no | X | one byte at the runtime index `[T+0F0h] + (y << [T+100h]) + x` (a runtime-indexed region; no game calls) | M |
| `weapon_ctor` | 40df30 | weapon_ctors | no | G A | the WEAPON base constructor: vtable chain and the construction count, shape_word_block (arena block), object_track (new, animation threads), mount_offset, the H3 vertex edit 40dd4c, memset | L |
| `beam_ctor` | 40e074 | weapon_ctors | no | G A | weapon_ctor through the bridge, multiply_q10; vtable and the construction count | M |
| `gun_ctor` | 40e140 | weapon_ctors | no | G A | weapon_ctor through the bridge; vtable and the construction count | M |
| `equip_ctor` | 40e234 | weapon_ctors | no | G A | weapon_ctor through the bridge; vtable and the construction count | M |
| `ecm_ctor` | 40e274 | weapon_ctors | no | G A | equip_ctor through the bridge; vtable and the construction count | M |
| `turbo_ctor` | 40e2bc | weapon_ctors | no | G A | equip_ctor through the bridge; vtable and the construction count | M |
| `targ_ctor` | 40e308 | weapon_ctors | no | G A | equip_ctor through the bridge; vtable and the construction count | M |
| `shield_ctor` | 40e344 | weapon_ctors | no | G A | equip_ctor through the bridge; vtable and the construction count | M |
| `energy_ctor` | 40e380 | weapon_ctors | no | G A | equip_ctor through the bridge; vtable and the construction count | M |
| `zone_load` | 42789c | terrain_load | no | G A I R | the runtime record `49630c`/FS:0, the zone stream built, read and destroyed (472f14, slot +18h, stream_destroy), new 47749c, the nested grid constructor and materials (cell arrays, heights, the stream-A draws of terrain_cells), euler_matrix into the zone transform 4AAA84h, the globals 49AAC8h, 4A0BE0h, 4AAA74h..4AAAACh, 49AAD0h; the assertion 479c78 | L |
| `grid_ctor` | 46befc | terrain_load | no | G A | the in-place array construction 49667c, the nested height_cache_ctor (new[], 49667c), the construction count [6CBEE4h] | M |
| `height_cache_ctor` | 495268 | terrain_load | no | G A | new[] 4774f0 and two array constructions 49667c, the construction count [6CBEE4h] | M |
| `terrain_materials` | 46c754 | terrain_load | no | G V A I R | the runtime record `49630c`/FS:0, two streams (472f14, slot +18h, stream_read, stream_destroy), dts_load and the bitmap's deleting destructor (slot +18h), new[], _fopen/_fscanf/_fclose (text grid), 49667c, terrain_cells, the stream-A draws, the H3 height cache 4952e8/4952fc | L |
| `terrain_cells` | 46c4c4 | terrain_load | no | G A R X | 49667c and new[] (cells and marks), cell_mark_set per cell, one stream-A draw per block (1,024 to 16,384 per zone), the whole cell array (runtime-sized), the H3 height cache 4952e8/4952fc | L |
| `cell_mark_set` | 470d74 | terrain_load | no | X | one byte at [G+0F0h] + ((cell - [G+0ECh]) >> 4), a runtime-indexed address (needs a computed region; the reads are two fixed dwords of G) | M |
| `weapon_types` | 40fc8c | stage2_types | no | G V A I | the weapon list and the projectile table from `weapons`/`proj` (472f14, stream_read, the read method +18h, stream_close/stream_open, stream_destroy, 479c78), new[] `4774f0` and `49667c`, the texture and shapes (45cdf4, 46a03c, 48349c, 474cd8), the nested weapon_entry; the runtime record `49630c`/FS:0 and the construction count | XL |
| `weapon_entry` | 40f8bc | stage2_types | no | G V A I | the nested dmg_region_read and collision_volume and the stream's read method (slot +18h) read the entry from the file and allocate its link list and sphere array | L |
| `rocket_types` | 40a818 | stage2_types | no | G A I | `rockets` through the stream (472f14, stream_read, stream_destroy, 479c78), new[] `4774f0`, the shapes `474cd8`, the runtime record `49630c`/FS:0 | L |
| `bullet_types` | 40ade0 | stage2_types | no | G A I | `bullets` through the stream (472f14, stream_read, stream_destroy, 479c78), new[] `4774f0`, the shapes and bitmap (474cd8, 45cdf4, 46a03c), the runtime record `49630c`/FS:0 | L |
| `beam_types` | 40b6e0 | stage2_types | no | G V A I | `beam` through the stream (472f14, the read method +18h, stream_read, stream_destroy, 479c78), new[] `4774f0`, the bitmap (45cdf4, 46a03c, 48349c), the runtime record `49630c`/FS:0 and the construction count | L |
| `mech_forms` | 41fdb0 | stage2_types | no | G A I | `mforms` through the stream (472f14, stream_read, stream_destroy, 479c78), `49667c`, `_memset` `49681a`, seven bitmaps (45cdf4, 46a03c, 48349c), the runtime record `49630c`/FS:0 and the construction count | L |
| `flyer_forms` | 422d90 | stage2_types | no | G A I | `fforms` through the stream (472f14, stream_read, stream_destroy, 479c78), `49667c`, `_memset` `49681a`, the runtime record `49630c`/FS:0 | L |
| `mech_types` | 42049c | type_loads | no | G A I | the name list `492d00` (file), new[] `4774f0` and `49667c` with the element constructors build the type array, the H3 display set-up `4378d8`, the runtime record `49630c`/FS:0, mech_type_load per selected type | XL |
| `mech_type_load` | 4201a8 | type_loads | no | G V A I | the OVL and "fm" records `492f54` and the shape list `474cd8` (files), new[] and `49667c`, the nested lod_remap, gun_list_load, gun_classes, node_list and multiply_q10, the H3 damage display `4379cc`, the debris set `40874c`, the runtime record `49630c`/FS:0 | XL |
| `lod_remap` | 420090 | type_loads | no | G V | memset of the frame rows, the shape virtual +20h, the nested node_remap writes the part words of the LOD shapes | L |
| `node_remap` | 41ff88 | type_loads | no | G V X | the node class virtual +0, remap_word and the recursion write the words of a node graph | L |
| `remap_word` | 41ff68 | type_loads | yes | — | — (span: the word P (argument 0, 2 bytes); EAX (P) compared relative to it; the table byte at a runtime index is only read) | — |
| `mech_type_select` | 41ff10 | type_loads | no | G O | the index byte table `4A9D08h` at a runtime index, the count `4A9D06h` and the base word `4A9D70h`; the slot bases (compare-capable) | M |
| `gun_list_load` | 40fee8 | type_loads | no | G A I S | the stream constructor `472f14`, stream_read and stream_destroy (file), `49667c`, the report `479c78`, the runtime record `49630c`/FS:0; the pushed type word carries the caller's EBX high word | XL |
| `gun_classes` | 40fc50 | type_loads | no | X | writes a word list whose length is the count word (read from memory, reread every pass) | M |
| `node_list` | 40304c | type_loads | no | G V A | new[] `4774f0` and the shape virtual +24h fill a count-dependent pointer list | L |
| `gun_slot_base` | 40feb4 | type_loads | yes | — | — (region: the image word `4A98E8h`) | — |
| `col_slot_base` | 40cbd4 | type_loads | yes | — | — (region: the image word `4987DCh`) | — |
| `dmg_slot_base` | 40d200 | type_loads | yes | — | — (region: the image word `498856h`) | — |
| `flyer_types` | 422fb4 | type_loads | no | G A I | the name list `492d00`, `49667c` builds the type array, flyer_type_load per selected type | XL |
| `flyer_type_load` | 422ed0 | type_loads | no | G A I | the records `492f54` and the shape `474c38` (files), dmg_load and collision_load | XL |
| `flyer_type_select` | 422e90 | type_loads | no | G O | the index byte table `4A9DCCh` at a runtime index and the count `4A9DCAh`; the slot bases (compare-capable) | M |
| `mfd_button` | 446844 | mfd_orders | no | G V I S | callee-first: mfd_range, mfd_view_next, mfd_target, mfd_order; replay of the page switch 446e60 and the radio buttons' slot +4 (MFD presentation); entry shim at the original stack coordinate (the group order's residue word below) | L |
| `mfd_click` | 4470c0 | mfd_orders | no | G S | callee-first: button_press, mfd_order, mfd_entry; entry shim at the original stack coordinate | L |
| `mfd_order` | 447248 | mfd_orders | no | G S | callee-first: menu_code, order_call, menu_toggle; entry shim at the original stack coordinate | L |
| `order_call` | 43166c | mfd_orders | no | G S | the group order (members' order slots, AI state); entry shim at the original stack coordinate | L |
| `group_order` | 4231a4 | mfd_orders | no | G V S | the members' order slot +28h (mech_order and the AI state of every member), dist_gate and memset; the priority word is stack residue of the frame (entry shim) | XL |
| `menu_code` | 43f9c0 | mfd_orders | yes | — | — (pure reader, no span; comparator: EDX:EAX exact; gap shim) | — |
| `menu_toggle` | 43f9f8 | mfd_orders | yes | — | — (span: argument 0, menu+2Ch, 0Ch bytes (the flag bytes, the entry, byte +36h); EAX (menu) compared relative to it, EDX exact; gap shim) | — |
| `mfd_entry` | 4470a4 | mfd_orders | yes | — | — (span: argument 0, G+B1h, 4 bytes; EAX (G) relative to it, EDX exact; gap shim) | — |
| `mfd_target` | 446fb0 | mfd_orders | no | G R | callee-first: cockpit_next (the cockpit target, its sound row); entry shim | M |
| `mfd_view_next` | 446fc4 | mfd_orders | yes | — | — (span: argument 0, G+318h, 8 bytes; EAX (G) relative to it, EDX exact; gap shim) | — |
| `mfd_range` | 446ff0 | mfd_orders | no | G S | replay or core of the range set 447014 (two MFD fields and a table read); entry shim at the original stack coordinate | S |
| `order_setup` | 431530 | mfd_orders | no | G V I | pool_prev (hook), the HDD panels 44a7e8 (presentation, allocations) and the MFD list 4472bc; writes the order group 49b0f8 and the wingman table 4d03c8 | L |
| `base_flatten` | 47129c | base_flatten | no | G X | cell_flag_get, flood_sum, flood_fill and the H3 shading pass 46c2e0 through the bridge; the floods write the marks and heights of every marked region (runtime-sized), the sum and count 6B5470h/6B5474h | L |
| `flood_sum` | 470fe8 | base_flatten | no | G X | cell_flag_get and cell_flag_set through the bridge per cell; marks of a whole region (runtime-indexed), the sum and count | M |
| `flood_fill` | 471128 | base_flatten | no | G X | cell_flag_get and cell_flag_set through the bridge per cell; heights and marks of a whole region and its write-through neighbours (runtime-indexed) | M |
| `mark_raster` | 47136c | base_flatten | no | G X | cell_mark_set through the bridge; the cell flag bytes +0Fh of the box (runtime-indexed) | M |
| `cell_flag_get` | 470e00 | base_flatten | yes | — | — (a pure read of one mark byte, no game calls; no spans or regions, the pair is compared) | — |
| `base_types` | 405fac | building_types | no | G V A I | the runtime record `49630c`/FS:0, four streams (472f14, the read method +18h, stream_read, stream_destroy, 479c78), 49667c and new[] `4774f0`, collision_read per building type, the hulk shapes and textures (474cd8, 45cdf4, 46a03c, 48349c), the debris set 40874c, the construction count | L |
| `base_pools` | 405df4 | building_types | no | G A | the runtime record `49630c`/FS:0, new `47749c`, pool_create and new[] `4774f0` (the building pool and its removal list) | L |
| `world_load` | 424308 | mission_world | no | G V A I R | the runtime record `49630c`/FS:0, three streams (47ad40, 47af98 and the read at slot +18h, 47ad78, 479c78), the STR reader 453878/4539a0 and strlen, memset, new/new[] and 49667c, the guests 42e010 and 405ebc (resources), the hooks region_init, timer_init, group_array, the type selects and type loads, pool_create, base_pools, the four leaves and zone_load (stream-A draws in terrain_cells) | XL |
| `actor_list_init` | 411df8 | mission_world | no | G A | new[] `4774f0` (the actor list's entry array) | L |
| `gun_slot_table` | 40fecc | mission_world | no | G A | new[] `4774f0` (the gun-list table) | L |
| `dmg_slot_table` | 40d218 | mission_world | no | G A | new[] `4774f0` (the damage slot table; none for a zero slot word) | L |
| `col_slot_table` | 40cbec | mission_world | no | G A | new[] `4774f0` (the collision slot table; none for a zero slot word) | L |
| `actor_contact` | 4127f8 | slot_leaves | yes | — | — (empty or constant body, flag only) | — |
| `actor_part_position` | 411a3c | slot_leaves | yes | — | — (empty or constant body, flag only) | — |
| `actor_part_facing` | 411a44 | slot_leaves | yes | — | — (empty or constant body, flag only) | — |
| `actor_kill` | 411b2c | slot_leaves | yes | — | — (empty or constant body, flag only) | — |
| `actor_link_position` | 411b34 | slot_leaves | yes | — | — (empty or constant body, flag only) | — |
| `actor_radial_hit` | 411b3c | slot_leaves | no | G V | slot +5Ch radius leaf and dist_gate (pure), tail slot +74h flyer_part (refuses compare: whole-DATA snapshot) | XL |
| `equip_sync` | 40f144 | weapon_syncs | no | G V O | widget slots +18h and +14h (arbitrary original gauge code; the second one stores the record): replay log of the two calls, or whole-DATA snapshot | L |
| `ecm_sync` | 40f184 | weapon_syncs | no | G V I O | widget slots +18h and +14h, the cockpit message 420a68 (HUD) and the words W+7Dh/+7Fh: replay log of the three calls | L |
| `turbo_sync` | 40f1f0 | weapon_syncs | no | G V I O | widget slots +18h and +14h, sfx_play 462830 (sound) and the latch dword W+81h: replay log of the calls | L |
| `beam_sync` | 40f288 | weapon_syncs | no | G V O | gauge record getter 440e9c and widget slot +10h (the record is written into the gauge): replay log of the two calls | L |
| `gun_sync` | 40f330 | weapon_syncs | no | G V O | getter 4414cc, movement_step 467924 (a hook, pure) and widget slot +10h; word W+7Dh ramps: replay log of the getter and the slot | L |
| `laew_sync` | 40f3b8 | weapon_syncs | no | G V O | as gun_sync (computes its call of 40f330 in core) | L |
| `laew_fire` | 40ea04 | weapon_syncs | no | G S O | weapon_prepare 40e788 (hook, residue bytes of the entry shim) and the emission 40acac (object creation) | XL |
| `weapon_none_28` | 40e95c | weapon_syncs | yes | — | — (empty body, flag only) | — |
| `weapon_none_38` | 4111ce | weapon_syncs | yes | — | — (empty body, flag only) | — |
| `weapon_none_3c` | 4111d3 | weapon_syncs | yes | — | — (empty body, flag only) | — |
| `weapon_none_40` | 4111d8 | weapon_syncs | yes | — | — (empty body, flag only) | — |
| `weapon_none_44` | 4111dd | weapon_syncs | yes | — | — (empty body, flag only) | — |
| `weapon_none_4c` | 40f89f | weapon_syncs | yes | — | — (empty body, flag only) | — |
| `mission_end` | 461ff0 | mission_end | no | I G | not meaningful: closes the tape and dump streams (runtime `_fclose`), calls the results writer (writes `results.dat`/`mission.var`) and sets the process exit code `4d27b8`; a second phase would close and write twice | n/a |
| `mission_root` | 461608 | mission_root | no | G V A I R | the whole mission load: the runtime record `49630c`/FS:0, new and the objects it builds, the file and resource loads of every guest callee (input set-up, loading screen, sound init, the HUD and cockpit construction), both script passes, the stage-2 dispatch, the drains, the render root and the start sequence (stream-A and stream-B draws below them); a second phase would load the mission twice | n/a |
| `map_frame` | 44c988 | hdd_map | no | G V I S | replay of the clock, the tick 44d370, the click callees map_click/44d82c, the button blink 44f644 and the H3 panel, label, button and map draws; callee-first: map_areas, map_click; span on M (the timer +149h, the record bytes, +81h, +0Ch) and the globals 49d6ac..49d6b9, 49d7e6; entry shim at the original stack coordinate (the frame address left in [[6c5f70h]+22Ch]) | M |
| `map_key` | 44cc68 | hdd_map_keys | no | G V I | callee-first: map_press, mode_next/mode_prev, target_cycle1/0, button_press, sfx_play; replay of the label 44dd14; span on M (+114h/+118h, +15Ch, the order record, +104h) | M |
| `map_press` | 44d5c0 | hdd_map_keys | no | G O | replay of the event record 44d3cc, the cursor 486ece, the projection 48c2e4/48c444/48c57c/48c5fc and the map placement 44d24c (the view at [M+14h] is written); span on M+15Ah | L |
| `wingman_pick` | 44d82c | hdd_map_keys | no | G V | callee-first: map_pick, wingman_index; replay of the selection 44a748 (lamp virtuals, DialogPort message); span on M+11Ch, 49d6ae | M |
| `wingman_index` | 433134 | hdd_map_keys | no | U | pure reader of the wingman table 4d03c8; flag only, with an in-vitro compare case | S |
| `mode_next` | 44ee48 | hdd_map_keys | no | G | callee-first: map_mode; span on the map gadget's two redraw bytes | M |
| `mode_prev` | 44ee88 | hdd_map_keys | no | G | callee-first: map_mode; span on the map gadget's two redraw bytes | M |
| `target_cycle1` | 44ef08 | hdd_map_keys | no | G | callee-first: target_cycle | M |
| `target_cycle0` | 44ef1c | hdd_map_keys | no | G | callee-first: target_cycle | M |
| `target_cycle` | 44ef30 | hdd_map_keys | no | G X | replay of the visibility test 44f03c (projection and map-area checks, 411c58); reads the actor list 4a9af8 at a runtime index | L |
| `explosion_pools` | 407b54 | effect_pools | no | G V A I | the runtime record `49630c`/FS:0, new `47749c`, pool_create, new[] `4774f0`, 49667c, the RTL `_strcat`/`_itoa`, 15 bitmaps (492bec, 45cdf4), the shapes 474cd8, one stream (472f14, the read method +18h, stream_read, stream_destroy, 479c78) | L |
| `debris_pools` | 408860 | effect_pools | no | G A | the runtime record `49630c`/FS:0 (restored from ECX), new `47749c`, pool_create, new[] `4774f0`, the debris set 40874c | L |
| `debris_set` | 40874c | effect_pools | no | G V A I | the runtime record `49630c`/FS:0 (restored from ECX), the shapes 474cd8, one stream (472f14, the read method +18h, stream_destroy, 479c78), new[] `4774f0`, debris_record per record | L |
| `debris_record` | 4083f8 | effect_pools | no | V A I | the stream read method +18h (2 + 7 per piece), new[] `4774f0` (the pieces) | L |
| `flat_pools` | 4097a8 | effect_pools | no | G A | the runtime record `49630c`/FS:0, the shapes and bitmap pair (474cd8, 45cdf4, 46a03c, 48349c), the construction count, new `47749c`, pool_create, new[] `4774f0` | L |
| `meteor_pools` | 409a34 | effect_pools | no | G A | the runtime record `49630c`/FS:0, new `47749c`, pool_create, new[] `4774f0`, the shapes and bitmap (474cd8, 45cdf4) | L |
| `fire_pools` | 46b1a8 | effect_pools | no | G V A I | the runtime record `49630c`/FS:0, new `47749c`, pool_create, new[] `4774f0`, 49667c, the RTL `_strcat`/`_itoa`, the shapes and two bitmaps, one stream (472f14, the read method +18h, stream_destroy, 479c78) | L |
| `smoke_balls` | 408fb8 | effect_pools | no | G A | the runtime record `49630c`/FS:0, new `47749c`, pool_create, new[] `4774f0`, the shapes and bitmap (474cd8, 45cdf4) | L |
| `weapon_shape_word` | 40e61c | fit_leaves | no | U | a pure reader; refused with the group (callee of weapon_fit, load only) | S |
| `mount_entry` | 40fea0 | fit_leaves | no | U | a pure lookup on the table pointer 4a98e4; refused with the group | S |
| `mount_table` | 420634 | fit_leaves | no | G | callee-first: mount_entry | M |
| `shield_split` | 413ac8 | fit_leaves | no | G O | callee-first: multiply_q10; two words behind the argument pointer (span of argument 0) | M |
| `energy_rate` | 417d08 | fit_leaves | no | G O | callee-first: multiply_q10 and part_ratio; the word A+256h (span of argument 0) | M |
| `equip_slots` | 40fb2c | fit_leaves | no | G O X | memset 49681a on the record (a clone span would do); reads the item array and definitions at runtime-indexed pointers | L |
| `node_state_fill` | 417530 | fit_leaves | no | G V O X | slot 0 of every node, the recursion, writes to node and array words behind runtime pointers | L |
| `camera_attached` | 401078 | camera_view | no | U | a pure reader of the camera; refused with the group | S |
| `camera_orbit_set` | 40109c | camera_view | no | O F | writes behind the camera argument; ECX is the original's frame address (dead at the callers) | S |
| `camera_attach_set` | 40111c | camera_view | no | O | writes behind the camera argument | S |
| `camera_turn_step` | 401c20 | camera_view | no | G R | callee-first: multiply_q8, approach_word (the approach word behind the pointer) | M |
| `camera_speed_step` | 401c4c | camera_view | no | G R | callee-first: multiply_q8, approach_word (the approach word behind the pointer) | M |
| `outside_camera` | 401c74 | camera_view | no | G R | callee-first: camera_turn_step, camera_speed_step | M |
| `view_apply` | 45de30 | camera_view | no | G O I | view_subject_set and the label step 45e208 (presentation) as guest calls; the view block and the camera word in fixed globals | L |
| `view_subject_set` | 45df34 | camera_view | no | G V A O | the runtime record 49630c/FS:0, the camera constructor 48bd84, view_offsets; the group and static cameras behind runtime pointers | L |
| `view_offsets` | 45edb0 | camera_view | no | G V O | the subject's virtual +30h and the two setters on the camera [4D24EAh] | L |
| `camera_current` | 45edf8 | camera_view | no | U | a pure getter of [4D24EAh]; refused with the group | S |
| `part_shapes_bind` | 4030d0 | sim_leaves | no | G O | writes behind two runtime list pointers and the pool; no declared span | L |
| `parts_intact` | 403668 | sim_leaves | yes | — | — (a pure reader of the object's part records) | — |
| `formation_slot` | 405b9c | sim_leaves | no | O | the cell 4975d6 and its first-call byte are fixed globals; no declared span | M |
| `explosion_entry` | 407b20 | sim_leaves | yes | — | — (a pure reader) | — |
| `flat_draw` | 40991c | sim_leaves | no | G V | terrain_pose, the object matrix and the child's slot as guest calls; refused with the group | L |
| `projectile_draw` | 40a120 | sim_leaves | no | G V O | the object matrix and the child's slot as guest calls; the global 4a5b04 | L |
| `beam_done` | 40c2a0 | sim_leaves | no | G | timer_word_step as a guest call (writes the timer word behind the object) | M |
| `weapon_by_mount` | 410670 | sim_leaves | yes | — | — (a pure reader) | — |
| `ptr_list_add` | 411dd4 | sim_leaves | no | O | writes behind runtime list and object pointers; no declared span | M |
| `box_square_init` | 413a90 | sim_leaves | no | O | writes five words behind the argument pointer | S |
| `twist_sound_latch` | 41a6d0 | sim_leaves | no | G O | sfx_playing as a guest call; the latch cells and flag are fixed globals | M |
| `pitch_sound_latch` | 41a994 | sim_leaves | no | G O | sfx_playing as a guest call; the latch cells and flag are fixed globals | M |
| `mech_type_index` | 41ff00 | sim_leaves | yes | — | — (a pure reader) | — |
| `flyer_type_index` | 422e80 | sim_leaves | yes | — | — (a pure reader) | — |
| `cockpit_notice` | 420a68 | sim_leaves | no | G V | the port's slot 0 as a guest call with a frame record | M |
| `radio_notice` | 420a98 | sim_leaves | no | G V | port_a and the port's slot 0 as guest calls with a frame record | M |
| `notice_time` | 426aec | sim_leaves | yes | — | — (a pure reader) | — |
| `mfd_key` | 4469e8 | mfd_keys | no | G O | panel_visible, button_press, mfd_range, the entry steps and mfd_key_select as guest calls; writes the gauge's mode bytes | M |
| `mfd_alt_key` | 446c38 | mfd_keys | no | G O | menu_select, menu_code, mfd_order and mfd_key_select as guest calls | M |
| `mfd_key_select` | 447158 | mfd_keys | no | G O | mfd_entry as a guest call; writes byte G+E6h | M |
| `menu_select` | 43f9e0 | mfd_keys | yes | — | — (span menu+32h, 7 bytes) | — |
| `mfd_entry_next` | 44703c | mfd_keys | no | G O | mfd_entry as a guest call | M |
| `mfd_entry_prev` | 447070 | mfd_keys | no | G O | mfd_entry as a guest call | M |
| `hdd_execute` | 44a1a0 | hdd_exec | no | G I O S | the page, wingman, order, clip, label and sound callees as guest calls (order_cancel and wingman_order hooked); the frame pointer EBP-8 left in [[6C5F70h]+22Ch] (entry shim) | L |

## Extraction script

`tools/reference-game/scripts/compare_audit.py` (standard library,
read-only) reads the registry and every `proxy/*_hooks.h` and prints the
flag per hook with its source line:

```powershell
python tools/reference-game/scripts/compare_audit.py            # Markdown
python tools/reference-game/scripts/compare_audit.py --json
python tools/reference-game/scripts/compare_audit.py --check docs/reference-game/compare_mode.md --hooks-doc docs/reference-game/hooks.md
```

`--check` compares the Compare column of the first table in this document
whose header starts with `Hook` and has a `Compare` column (the
[per-hook table](#per-hook-table)) and, with `--hooks-doc`, the registry
table of hooks.md with the adapters, and lists missing, extra or
disagreeing hooks (exit status 1). `tools/check.ps1` runs it as the
`audit` stage (`-SkipAudit`); it is not part of `bazel test`, because a new
hook would otherwise fail the build until this table is updated. Run it
when a hook is added or its flag changes.

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
selections). The classification columns (refusal codes, needs, effort) are
hand-written; the script does not derive them.
