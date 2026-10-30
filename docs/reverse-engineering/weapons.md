# Weapons and projectiles

Weapon emission, projectile and bullet flight, impact, the swept and radius
queries, the building and actor emitters, the weapon system behind the
weapon keys, and the constructor, effect and steering helpers below them.
The weapon class matrix (class tables, IDs, per-class methods) is in
[weapon_classes.md](weapon_classes.md).

## Evidence and conventions

- **Image.** German 1.11 `VER95/DBSIM.EXE` (hash in
  `data/reference_hashes.txt`), image base 400000. The CODE file offset is VA minus 400a00 (RVA minus a00).
  Contracts are decoded from the entry instruction with `llvm-objdump`.
- **Static audits.** Static audits check direct calls, relative transfers
  and initialized pointers and decode the emission, flight, preparation,
  vehicle, type-dependent emitter, impact and building bodies from their
  entries. A byte-pattern candidate alone is not a call. Nearest-prologue labels from a
  linear disassembly are search hints only: CODE contains inline RTTI.
- **Notation.** Arguments are four-byte slots; narrow loads keep or
  sign-extend bits as stated (s16/s8: sign extension; lo(R, w): a 16-bit
  load or 16-bit arithmetic into R that keeps R's high word; f: the
  original's EBP). Arithmetic wraps at the written width; signed
  tests use that width. Stack figures exclude incoming arguments and the
  caller's return address; maximum outgoing figures exclude the callee's
  return address and frame. Unless stated otherwise a body is cdecl,
  preserves EBP/EBX/ESI/EDI and returns with plain RET. No body below
  executes x87 instructions unless stated; a delegating body inherits the
  x87 effects of its last callee. The call domain is a valid accessible
  guest graph and non-faulting division.
- **Own code.** Each hooked body has a core function
  `es2_<hook>_registers` (or a close variant) in the core source named in
  its section, and a proxy adapter in
  `tools/reference-game/proxy/<group>_hooks.h`. Callees stay guest calls
  through the framework bridge, so every hook keeps its own independent
  mode. Every hook is off by default; compare is refused unless a section
  says otherwise. Mechanics: [hooks.md](../reference-game/hooks.md#how-a-hook-runs).
- **Validation.** Unless a section says otherwise, a hooked body passes the
  regime of [hooks.md](../reference-game/hooks.md#validation): 1,001,024
  seeded direct cases (100,000 on the quick tier) and 256 installed cases
  with 31h/a7h stack poison, fast and preserving dispatch and a nonempty x87
  stack, comparing the full EDX:EAX, ordered argument slots, callee
  results, graph state and both RNG streams; every conditional site is seen
  both ways or PROVED in `invitro/branch_sites.h`; at least one mutation is
  caught. Sections list only extra cases and mutants.
- **Call sites.** The doc keeps the call sites a contract depends on; it
  is not an exhaustive call-site, binding and operand inventory.
- **Live status.** All registered hooks replaced together give identical
  live regressions in every suite ([hooks.md](../reference-game/hooks.md),
  Current state). A body that no suite enters stays in vitro
  only; the hook map in [Firing, flight and impact graph](#firing-flight-and-impact-graph)
  and the per-section validation notes say which.
- **Names.** Semantic names (projectile, bullet, beam, free shot, debit,
  animation, excluded definition, notification, lifecycle callback) are
  hypotheses from the code. Arithmetic, widths and order do not depend on
  them. Static tables may not cover dynamically assigned receivers.

## Firing, flight and impact graph

**Static edges.** Each entry and edge was decoded from its entry
or a known instruction boundary.

```text
active loop 45f487 -> 45f570
  projectile pool [4a96c2], iterator 471c70 at 45f779
    object.V14 at 45f753
      table 498448 -> 40a538 (constructor assignment 40a99c)
      table 498628 -> 40b124 (constructor assignment 40afbe)
    nonzero AX -> pool removal 478628 at 45f766
  actor pool [4a95a0] -> V18 at 45f7a6
    vehicle table 49a282 -> 415b38 -> definition callback (+18/+1c/+20)
    selected static definition 49991c -> 41c194 (AI/control boundary)
  460870 -> 415608 at 460cf2 -> 410dbc at 415623
    selected/paired weapon: V2c, V30 guards -> V28 at 410e8e / 410ee1

weapon group 4107e4
  select by V48 -> V34 maintenance at 41090c
  readiness 40ede8 -> V28 at 41092e

weapon V28 bindings (constructor table, pointer slot):
  498d54 +28 = 40e964 at 498d7c; constructor 40e15d
  498dcc +28 = 40ea58 at 498df4; constructor 40e095
  4992c0 +28 = 40ec64 at 4992e8; constructor 4101f4
  each -> common preparation 40e788
  40e964 -> 40a9c4 or 40b43c (projectile allocation/initialization)
  40ea58 -> 40bf74 / 40b5a0 / 40b43c (type-dependent variants)
  40ec64 -> 40bf74

40a538 -> 40a234, timer_word_step, movement_step, guidance 40a254/40a488,
          Euler/point transforms -> 426528 at 40a721
40b124 -> 40adc0, timer_word_step, movement_step, guidance 40aff0,
          transforms -> 426528 at 40b3d5 / 40b406
          optional radius dispatch 426a20 at 40b3f8
40bf74 -> 426528 at 40c03d, sound and effect allocation

426528 -> terrain/segment query 428048; actor-list receiver.V20 at 4266c9
  vehicle V20 418ba8 -> 4188c8 at 418e41
    4188c8 -> V74 at 41893b; conditional V70 at 41895a
  building V20 405038 (separate part intersection/damage boundary)
  receiver.V50 at 426801 (vehicle 41f7b8; building 411aec)
426a20 -> receiver.V5c radius, dist_gate, receiver.V70 at 426a8d
```

The projectile constructors acquire from [4a96c2] and set position
+26/+2a/+2e, orientation +c/+e/+10, owner +4a, definition +4e and lifetime
word +54 (40a9da..40aa70; analogous 40b43c path). RTTI contains `PROJECTILE`
at 40c300 and `BULLET` at 40b62d. 426528 is a shared swept-query and impact
wrapper, not a geometry leaf (see [Swept query 426528](#swept-query-426528)).
426a20 is a second static route to the hit virtuals, independent of
`ground_collide`. The five fixed probes in 4198f4 also call 426528, but they
are not a weapon emitter; its callback pointer 49994c is copied at
41401e..414036 and its role is open. Not every 426528 caller is a weapon.

### Direct callers and dispatch slots

**Static inventory.** Direct CALL sites, each decoded at the
site; no direct tail jumps to these entries exist. Pointer occurrences are
all matching initialized DATA pointers, including unaligned slots.
Dynamic or computed references are not covered by a static scan.

| Body | Direct sites | Initialized dispatch |
| --- | --- | --- |
| 40a234 | 40a545,40aaec | none |
| 40adc0 | 40afd3,40b131,40b496 | none |
| 40f881 | 40efae | none |
| 426a20 | 409f40,40b3f8,41e517 | none |
| 40ef94 | 40f029,40f10c | eight V34 slots below |
| 40e964 | none | 498d54.V28, pointer slot 498d7c |
| 40e788 | 40e976,40ea16,40ea6e,40ec76 | 498e44.V28, pointer slot 498e6c |

The eight V34 slots (tables 498a84, 498afc, 498b74, 498c64, 498cdc, 498d54,
498e44, 499248) all equal 40ef94. The first five V28 slots point to the no-op
40e95c; 499248.V28 points to 40ea04, another caller of preparation.

Bindings from DATA and constructor assignment instructions:

| Role (semantic hypothesis) | Table | Slot and body | Assignment instruction |
| --- | --- | --- | --- |
| ordinary emitter | 498d54 | +28 = 40e964 | 40e15d, [EBX] |
| variant emitter | 498dcc | +28 = 40ea58 | 40e095, [EBX] |
| alternate emitter | 4992c0 | +28 = 40ec64 | 4101f4, [ECX] |
| projectile flight | 498448 | +14 = 40a538 | 40a99c, [EBX] |
| bullet flight | 498628 | +14 = 40b124 | 40afbe, [EBX] |
| vehicle | 49a282 | +20/+58/+70/+74 = 418ba8/41b60c/4187d0/417de4 | 415c60, [EBX] |

Building tables 497784, 497818, 4978ac, 497940, 4979d4 share V20=405038,
V58=406808, V70=404f20, V74=404d70. Class and weapon names are interpretations (IDs and names per class:
[weapon_classes.md](weapon_classes.md)).

Of the decoded indirect +28/+34 sites only the weapon-group sites 41092e,
410e8e, 410ee1 (+28) and 41090c (+34) are statically tied to this weapon
graph; the others use the same slot number in
unrelated or unbound classes. Projectile V14 pool dispatch is 45f753. Function-body
detours cover all tables without rewriting them.

Flight entries have no direct relative CALL/JMP; 49845c=40a538 and
49863c=40b124 are their only initialized pointer occurrences. Vehicle part
has only pointer 49a2f6=417de4.

**Constructor call sites** (each decoded at its instruction boundary):
40a9c4 at 404542, 40458e, 40499d, 40e9c5, 422994; 40b43c at 404557, 4045a3,
40b5b5, 40e9e2, 40ec54, 4229b1, 4229ff; 40b5a0 at 40eb19, 40eb8f, 40ebf8.
The emitters outside weapon V28 are 404100, 4045c8 and 4226a0; none needs
weapon V28 or common preparation. 404100 emits type 0 at 404542/58e or
type 2 at 404557/5a3 from mirrored local offsets; it is table 4978ac+18
(pointer 4978c4) and is also called at 46a71a. 4045c8 emits type 3 at
40499d; it is table 497784+18 (pointer 49779c). 4226a0 emits type 0 at
422994 or two type-2 shots at 4229b1/9ff; it is called at 422ceb.

### Hook map

Hook name, VA, core source, registry group (compare flags and live counts:
[hooks.md, Registry](../reference-game/hooks.md#registry)). Core sources
are under `core/src/`, proxy adapters under `tools/reference-game/proxy/`.

| Hook | VA | Core | Proxy group |
| --- | --- | --- | --- |
| `projectile_def`, `bullet_def` | 40a234, 40adc0 | weapons.c | weapons |
| `mask_merge` | 40f881 | weapons.c | weapons |
| `radius_hit` | 426a20 | weapons.c | weapons |
| `weapon_step` | 40ef94 | weapons.c | weapons |
| `weapon_fire` | 40e964 | weapons.c | weapons |
| `weapon_prepare` | 40e788 | weapons.c | weapons |
| `bullet_flight`, `projectile_flight` | 40b124, 40a538 | weapons.c | weapons |
| `vehicle_part` | 417de4 | damage.c | weapons |
| `target_emit` | 4226a0 | weapons.c | weapons |
| `emit`, `alt_emit` | 40ea58, 40ec64 | weapons.c | weapons |
| `impact` | 40bf74 | weapons.c | weapons |
| `turret` | 404100 | building_weapons.c | weapons |
| `side_scale`, `cell` | 426b04, 427238 | weapons.c | weapons |
| `sweep` | 426528 | weapons.c | weapons |
| `segment`, `sweep_terrain` | 46e980, 428048 | weapons.c | weapons |
| `terrain_line`, `diagonal`, `face_test`, `face_cross`, `edge_test`, `contact`, `normalize`, `slope_test` | 4280f4, 46fdb8, 46ff90, 470080, 470468, 470798, 46c23c, 46ff4c | weapons.c | weapons |
| `line_plane` | 47e610 | terrain.c | line_plane |
| `shield_geometry` | 413cc4 | node.c | node |
| `lc_base_update`, `projectile_create`, `weapon_record` | 4045c8, 40a9c4, 40fe84 | lc_base.c | lc_base |
| `target_select`, `target_valid`, `group_death`, `death_actions` | 411fa0, 411e80, 423f30, 411bc8 | building.c | building |
| `turret_target`, `part_facing` | 403eec, 406868 | building.c | building_aim |
| `weapon_select` | 410708 | panel_weapons.c | panel_weapons |
| `select_step`, `fire_group`, `weapons_v3c`, `group_toggle` | 41074c, 410ae4, 410d04, 4110ac | weapon_system.c | weapon_system |
| `weapon_frame`, `ready_step`, `selected_ready`, `chain_ready`, `latch_set`, `link_toggle`, `member_toggle` | 410b40, 410a3c, 410a04, 410970, 410f04, 410f14, 410cd0 | weapon_slots.c | weapon_slots |
| `gauge_sync`, `gauge_state`, `gauge_link`, `gauge_mode`, `gauge_button` | 4420c8, 442120, 442054, 441f90, 442154 | weapon_slots.c | weapon_slots |
| `group_fire`, `weapon_ready`, `part_condition`, `sphere_hits`, `sphere_test` | 4107e4, 40ede8, 40db2c, 40c524, 40c428 | weapon_helpers.c | weapon_helpers |
| `group_fire_sel`, `group_fire_any` | 41194a, 411a1d | vehicle_update.c | part_helpers |
| `mount_offset`, `mount_base` | 40f540, 40f904 | mount.c | mount |
| `weapon_damage`, `beam_damage`, `gun_damage`, `targ_damage`, `weapon_eject` | 40ee0c, 40ee38, 40ee90, 40ef6c, 40f57c | weapon_eject.c | weapon_eject |
| `bullet_create`, `bullet_init`, `def_lookup` | 40b43c, 40af6c, 40ffc8 | weapons.c | bullet_ctor |
| `proj_record`, `pitch_point`, `damage_words`, `bullet_charge`, `rocket_init` | 40ffb0, 49295c, 40501c, 40b5a0, 40a948 | weapons.c | weapon_leaves |
| `beam_create` | 40b804 | weapons.c | beam |
| `pool_alloc`, `object_init`, `light_create`, `explosion_create` | 471b30, 402188, 407604, 407f1c | effects.c | effect |
| `explosion_entry`, `beam_done` | 407b20, 40c2a0 | sim_leaves.c | sim_leaves |
| `vector_angle`, `aim_angles`, `projectile_steer`, `bullet_steer` | 49290c, 492990, 40a254, 40aff0 | projectile.c | projectile |
| `missile_steer`, `ally_nearest` | 40a488, 423974 | weapons.c | cold_paths |
| `targ_aim`, `targ_next` | 40e4dc, 40e4ac | input.c | cold_paths |
| `rotation_place`, `rotation_compose` | 47f4f4, 47f058 | matrix.c | rotation_helpers |
| `object_query`, `grid_sweep`, `set_length` | 427da8, 4273c8, 4927f0 | collision.c | object_query |

`turret_aim` 403d5c (track_pose), `base_update` 403ca8 and `base_parts`
403914 (base) are callees of the building emitters documented elsewhere.
The weapon-class virtuals (`weapon_none_3c`, `weapon_none_28`, `laew_fire`,
`weapon_priority`, `beam_preset`, `beam_priority`, `beam_ready`,
`equip_ready`, `gun_ready`, `elf_ready`, `weapon_ammo`, `gun_ammo`) are in
[weapon_classes.md](weapon_classes.md).

## Weapon emission

The emission entries are the selectors, the mask merge, maintenance V34,
the three V28 emitters and their common preparation. Prologue signatures
are the bytes the proxy checks before relocation (instruction lengths in
parentheses). The selectors, mask merge, radius dispatch, maintenance and
emission choice are in vitro (`weapons_cases.h`).

### Selectors 40a234 and 40adc0

**Leaf** `(object)`. At 40a23a / 40adc6 read signed byte O+41;
multiply by 14 modulo 2^32; add the dword at 4a96d0 / 4a96f8 respectively
(40a247 / 40add3). EAX=EDX=result (40a24d / 40add9). No branch, write,
callee or x87 change. Stack: saved EBP only, four bytes. Prologue for both:
`55 8b ec 8b 45 08`, six relocated bytes (1/2/3). Own code: `projectile_def`
and `bullet_def` (weapons.c); every signed-byte index is accepted.

### Mask merge 40f881

**Bounded leaf** `(destination,source)`. Read source byte, then
read/AND/write destination byte, increment both addresses, repeat eight
times in ascending address order (40f88f..40f89a). The only conditional
site is signed CX<8 at 40f89a; CX starts at zero. Every relative overlap,
including identical addresses and destination=source+1, is legal; earlier
writes may change later source reads. EAX=source+8, EDX=destination+8
modulo 2^32. Stack eight bytes (EBP,EBX), no callees or x87 change.
Prologue `55 8b ec 53 8b 45 08`, relocation seven bytes (1/2/1/3).
Own code: `mask_merge` (weapons.c); overlap and order cases are mandatory.

### Maintenance 40ef94

**Delegating body**, V34 `(weapon,result_word)`; eight table
slots refer to this one body. EAX=W+43. If byte W+49!=0 (40efa4), return
immediately. Else call 40f881(W+33,W+3b) (40efae), clear the eight bytes
W+3b..42 via memset 49681a (40efbd), then call timer_word_step 467a48(W+30)
(40efc9). Read byte W+44; zero skips animation (40efd6). Otherwise read
pointers P=[W+10], Q=[W+14], D=[P+20]; numerator=u16[Q]+1, divisor=u16[D].
Signed IDIV at 40efed gives quotient EAX and remainder EDX; both operands
are nonnegative (numerator <=65536), divisor must be 1..65535. Write the
low remainder to Q. If the remainder is zero (40eff7), clear W+44 and
EAX=W+43; otherwise keep the quotient. Non-animation EAX=W+43 and EDX=timer
result. Finally replace AX with the incoming result word (40f000); the
guarded exit preserves incoming EDX. Stack 16 saved bytes, maximum 28
including memset arguments. All graph and callback alias effects occur in
this order; compare cannot cover arbitrary callee effects and the
runtime-indexed Q. Own code: `weapon_step` (weapons.c).

### Emission choice 40e964

**Delegating body**, V28 of 498d54 `(weapon,owner,free_word)`.
Call 40e788(W,owner,free_word) unconditionally (40e976). If free_word's low
word is zero (40e981), reload C=[W+1c] and subtract u16[C+38] from word
W+7b modulo 65536. A signed result >0 keeps it (40e995); otherwise clear
W+7b and byte W+4c, EAX=W+43. The debit preserves the preparation EDX's
upper word and places the cost in DX. Then capture T=[W+20]; u16[T]==0
chooses constructor A (40e9ab). Call owner.V38(owner) (40e9b0); constructor
40a9c4 receives ((V38_EDX&ffff0000)|u16[T+2],4a9854,4a9860,owner,V38_EAX),
in that order, and its complete pair is returned. T is retained across V38.

For a nonzero type, constructor 40b43c receives
((current_EAX&ffff0000)|u16[[W+20]+2],4a9854,4a9860,owner). Afterwards
reload D=[W+18]; EDX=D. Signed byte D+6>=4 skips (40e9f4) and returns the
constructor's EAX; otherwise set byte W+44=1 and EAX=W+43. No
allocation-result test exists. Stack 16 saved bytes, maximum 36 with five
arguments. Own code: `weapon_fire` (weapons.c); its relocated interval is
[40e964,40e969). The fire entry shim also carries the preparation residues
(see [Padding and transient packets](#padding-and-transient-packets)).

### Common preparation 40e788

**Delegating body, not compare-isolatable** `(weapon,owner,unused)`.
112 local bytes + 16 saved, maximum 140 including outgoing arguments. Read
D=[W+18], word D+0, and capture M=[owner+34]. Look up [M+4].V20 with
receiver [M+4] and a slot combining D's upper word with D+0 (40e7b5). A null
result (40e7bf), or signed result+4<=-1 (40e7c6), chooses record 6c56a8;
otherwise record=[M+16]+32*s16(result+4). If dword [49ef34]!=0 and
owner==[4d24e6] (40e7e7/ef), copy the 32-byte record to a local (five
dwords, then Z/X/Y) and subtract 1500 from its Z dword modulo 2^32.

Read the W+5f dword. If it or the W+5b dword is zero (40e824/82a), prepare
three local zero words: if W+5f==0 (40e840), third=W+28; if W+5b==0
(40e84e), first=W+24. Call Euler 47ebb8(local_angles,local_matrix), then
47f4f4(local_matrix,record,local_composed); use the composed record.
Recheck [49ef34] and owner==[4d24e6] (40e885/88d); when both hold, use actor
[4d2684] instead. For the selected actor, if word +32==0 (40e89d), call
Euler (actor+c,actor+12) and set word +32=1. Call
47fa20(record,actor+12,4a9834).

Reload C=[W+1c] and copy the three words C+40/+42/+44 to a local point.
Call 40f540(W); add its words +4/+0/+2 to local Z/X/Y, wrapping at 16 bits.
Call 4801d4(local_point,4a9834,4a9854), then 47f9a0(4a9834,4a9860). Reload
C=[W+1c]; call Q10 47e0b0(s16[W+63],s16[C+4c]); store AX to W+31 and set
bytes W+33 and W+3b to one. Final EAX=W+3b, EDX=Q10 high product.

Direct writes are the W+31 word, the two latch bytes and the selected
actor's cache flag; callees write its matrix and the global 32-byte matrix
4a9834, 12-byte position 4a9854 and six-byte angles 4a9860. Shared global
aliases and callback graph changes must stay ordered. Local matrix byte 19
is stack residue (next subsections). Callers 40e976, 40ea16, 40ea6e,
40ec76 and 498e44.V28; 40ea04 (V28 of 499248) also calls it. V58 bodies of
the receivers are not leaves: vehicle calls V20 at 41b673 and transforms at
41b6b2/41b6cb/41b6e2; building calls Euler at 40682a and point transform
at 406858. Own code: `weapon_prepare` (weapons.c).

### Type-dependent V28 40ea58

**40ea58..40ec62**, V28 of 498dcc `(weapon,owner,free_word)`;
eight local bytes, saves EBP/EBX/ESI/EDI (24 bytes; 44 with outgoing
slots). Prologue `55 8b ec 83 c4 f8` (six bytes, 1/2/3). Let W be weapon,
O owner, C=[W+1c], T=[W+20]. Preparation 40e788(W,O,free_slot) always runs
first at 40ea6e; free_slot keeps entry EAX's high word and free_word's low
word. No body branch tests free_word. Read D=[W+18]; signed byte D+6<4
(40ea80) sets byte W+44=1. Read T anew: word T==4 (40ea90) selects
immediate impact.

For type 4, copy the global position to the matrix translation in Z/X/Y
order: 4a985c->4a9850, 4a9854->4a9848, 4a9858->4a984c. Read signed words
charge=W+7d and cost=C+38. The signed minimum is selected at 40eac2; when
cost<=charge select cost, including the tie. Its argument keeps the high
word of global X for cost or global Y for charge. Call
40bf74(type_slot,4a9834,[C+30],O,minimum_slot) at 40eae0, with type_slot
combining C's high word and u16[T+2]. Then subtract the retained minimum
from the current W+7d word. Return the complete impact EDX:EAX.

For other types, signed cost<=charge (40eaff) subtracts cost from W+7d,
then calls 40b43c(type_slot,4a9854,4a9860,O) at 40ec54 and returns its
pair. Here type_slot keeps C's high word in EDX and uses the reloaded T+2
low word. When cost>charge, call 40b5a0(type_slot,4a9854,4a9860,O,
charge_slot). charge_slot preserves the preceding EAX's high word;
type_slot preserves ECX's high word from signed D+6, replacing only CX with
T+2. Reload C after the call. If C+3c word==3 (40eb29), make two additional
scaled emissions: first (0,C+42,C+44), then (-s16(C+40),C+42,C+44), both
modulo words. For each, call 40f540(W), add its XYZ words, transform through
4801d4(local_short_point,4a9834,4a9854), reload W+7d and T, and call 40b5a0.
The second point reloads C after the preceding constructor. Local words are
assigned before every read; the unused two bytes of the eight-byte local
are never passed as point data.

After all scaled emissions reload C. If C+3e word==19 and byte W+4d==0
(40ec08/40ec11), set W+31 to low16(s16[C+4c] SAR 2) and W+4d=1, preserving
W+7d. Otherwise clear W+4d and W+7d. Both return EAX=W+43, EDX=C; a scaled
constructor's return pair does not survive this tail. No allocation-result
guard exists. Own code: `emit` (weapons.c).

### Alternate V28 40ec64

**40ec64..40ecd0**, V28 of 4992c0, same slots; saves
EBP/EBX/ESI (12 bytes; 32 with outgoing slots). Prologue `55 8b ec 53 56`
(five bytes, 1/2/1/1). Call preparation with the same narrow free slot.
Reload C, subtract u16[C+38] from W+7d without clamp or free-shot guard,
then copy the global position Z/X/Y to the matrix translation. Reload C and
T, call 40bf74(type_slot,4a9834,[C+30],O,1200) at 40ecc5 and return its
complete EDX:EAX. type_slot uses C's high word and T+2's low word. There are
no conditional branches or uninitialized direct local reads. Own code:
`alt_emit` (weapons.c).

### Padding and transient packets

**Preparation padding is observable.** Running the original 40e788 with
real Euler, both matrix operations, point transform, matrix angles and Q10
(only V20 and 40f540 synthetic), with identical weapon, owner, record and
globals and stack poison 31h versus a7h before the call, changes exactly
one of the 50 global output bytes: **4a9847**, matrix byte 19; EDX:EAX is
equal. Record and owner matrices have mode zero, local yaw 4000h. Euler
leaves its local padding byte unwritten; 47f4f4's second-mode-zero path
(47f6f9..47f70e) copies the first matrix's first 20 bytes; 47fa20's
second-mode-zero path (47fcbf..47fce8) propagates them to 4a9834. Freshly
allocated scratch therefore cannot reproduce every original output byte.
The `--weapon-prepare-probe` in vitro probe (default shard 0) shows this.

**Direct-reference inventory.** The hash-gated scan of every image section
finds no literal pointer to 4a9847 and no initialized-data alias into
4a9834..4a9853. The only code operands into the 32-byte matrix are the
compose destination, the short-point transform and matrix-angle sources,
the 40bf74 and 4801d4 matrix arguments and the translation stores in the
emitters.

**Consumers.** 4801d4 and 48043c read coefficient words 0..17, mode byte 18
and translation dwords 20/24/28; 480304 omits translation. 47f9a0 reads
coefficients; 47df19 transposes coefficients. None uses byte 19 in
arithmetic. Whole-rotation copies do read it: 47f4f4's identity paths at
47f6f9..47f712, matrix_compose's copy paths, 40bf74's explicit byte copy at
40c157/40c15a, and sweep's five-dword copy at 4265e2. "No reader" is false
even without a gameplay observation.

The 40bf74 matrix flows to 426528 through its segment pointer. Sweep writes
an inverse 32-byte matrix at segment+10 (4265d7..42663a), then passes the
segment to 428048 and the impact packet to receiver V20 (4266c9). Terrain
428048 consumes the original matrix at 428085, then passes only translated
points and radius to 46e980 (4280a4). Building V20 405038 and vehicle V20
418ba8 consume matrix translation for range (40510c/418bca), compose the
inverse with the actor matrix (405164/418ca2), and forward it to 40c9d4 /
413cc4; the simpler building path uses 427da8. That helper consumes the
original and inverse matrices through 48043c (427e55, 427fa7), then passes
points to 4273c8. Vehicle shield helper 413cc4 copies byte 19 at
413d95/413d9b, then passes its local matrix only to transpose 47df19 and
rotation 480304; its state-changing decisions use the transformed
coordinates, not byte 19. Building helper 40c9d4 calls 40c8fc at 40ca18,
which copies the first 20 bytes at 40c91f or composes at 40c972, then
publishes the local matrix through 4a980c at 40c980. The only three readers
of that pointer are 40c4a9 (translation +20 only), 40c4ca and 40c576
(4801d4 at 40c4de/40c584); their downstream 40c428 receives a point, not the
matrix. This closes the identified building and vehicle query paths; it is
not a universal non-observation proof for unknown receiver tables. The tick
`mem_watch_check` can also emit any configured byte range, including a
watch of `4a9834 32`.

**Chosen solution: reproduce residue, no normalization.** At preparation
entry ESP=S, its prologue makes EBP=S-4. The local Euler matrix starts at
EBP-30h; its byte 19 is **S-33**. The composed local matrix starts at
EBP-50h; its byte 19 is **S-65**. The body reserves both before any guest
call. Euler leaves its byte 19 untouched; rotation composition either copies
that byte or leaves its own destination byte untouched. The native entry
shim captures both bytes before the framework writes below ESP, packs them
in CL/CH, and tail-jumps to the framework bridge. Entry ECX is dead before
V20 (40e7a1/40e7af), so no observable input register is lost. Core
initializes the two local padding bytes from the explicit context; all math
still goes through guest entries. Matrix padding from a resource, owner or
global destination is preserved by those callees' own contracts. With 31 at
S-33 and a7 at S-65 at once, record mode 0 yields 31 and modes 1/2 yield a7;
both preserving and fast dispatch match the original, including 4a9847 and
full EDX:EAX. This proves entry-frame equivalence, not identical unused
stack bytes across different replaced callers.

`weapon_fire` preserves the two caller-frame sources. Its prologue saves
EBP/EBX/ESI/EDI and pushes three arguments before 40e976, so preparation
enters at fire-entry ESP-32 and the source bytes are fire-entry **ESP-65 /
ESP-97**. No intervening call or write touches them. The fire entry shim
captures them in dead ECX; its preparation guest bridge places them at the
callee's ESP-33 / ESP-65 before calling the ordinary entry, so the hook
modes stay independent. Without this bridge a replaced fire differed on an
identity-owner/local-matrix path.

**Flight exclusion words.** Both flight packets leave dword +14h
uninitialized. Sweep compares it with the candidate actor at 4266a6; it is
an excluded actor, not padding. The bullet packet starts at EBP-78h, giving
**S-104**; the projectile packet starts at EBP-6ch, giving **S-92**. Their
entry shims capture those complete dwords in ECX, which both selectors
overwrite before the bodies use it; core writes the captured word to
impact+20. The impact storage and the segment's 32-byte inverse output are
disjoint.

### Nested frame residues

**Stack offsets.** Preparation reads two matrix-padding bytes.
Its entry is emitter ESP-40 for 40ea58 and ESP-28 for 40ec64, so the source
bytes are ESP-73/-105 and ESP-61/-93 respectively. The immediate impact
exclusion is emitter ESP-184 / ESP-172, the same preparation-entry ESP-144
in both callers. This word can change during preparation: its last original
Q10 call saves preparation EBP in that location. Capturing initial emitter
poison and forwarding it to impact is therefore insufficient (a mixed-mode
run showed a callee-produced preparation frame pointer in the exclusion
slot); the guest adapter observes the word after preparation. The native
entry shim reserves a guard-probed gap below the original caller frame. Its
guest bridge runs preparation and immediate impact at the original outgoing
ESP coordinate while portable core runs below the gap. It seeds and
recaptures the exact word; no pointer normalization and no assumption that
an excluded receiver cannot live on the stack is used.

The emitter stack gap is **128 KiB**, probed one page at a time. It covers
one live extended comparison frame (four 16-KiB snapshot arrays plus
metadata) and more than 60 KiB of other guest frames. In the supported
preparation and impact graph, compare-enabled math, timing, geometry and RNG
replacements compute in core and do not enter further guest hooks;
graph-delegating replacements refuse compare. The framework's original
comparison phase bypasses nested hooks (`hook.c`, `original_phase_depth`),
so these combinations have at most one live comparison frame in this guest
interval. This is not a bound for arbitrary new callbacks or recursive
emitter graphs; stack high-water use must be checked before extending that
domain. The reference DBSIM PE32 optional header (magic 010b, offsets
+72/+76) reserves 00100000h stack bytes and commits 00002000h. The
reservation probes each page to permit guard-page growth; the remaining
headroom at real entry sites has not been measured in game. Each emitter
passes 2,048 adversarial installed states in which the delegated
preparation stub writes 73,728 bytes of a local stack buffer before
returning (fast and preserving dispatch, both stack poisons).

## Projectile flight and impact

Both flight bodies are cdecl `(object)`, save EBP/EBX/ESI/EDI, reserve 120
local bytes, and return with plain RET (40a814, 40b438). Their own maximum
stack use is 136 bytes plus outgoing arguments: 12 for 40a538, 20 for
40b124. Neither executes x87 instructions; the return x87 state is the last
delegated callee's. Prologue `55 8b ec 83 c4 88`, lengths 1/2/3. The scale
calls in both bodies target **467924** `movement_step` (a signed word times
the current elapsed scale), not `time_scale` 467894. Flight updates are
deferred, not nested in the emitters: the spawn wrappers 40a9c4..40ab16 and
40b43c..40b59d contain no direct flight-body or V14 call; their
initializers install tables 498448 (40a99c) and 498628 (40afbe), and the
object loop calls V14 later at 45f753.

### Flight 40b124

40b124..40b438, V14 of 498628. Own code: `bullet_flight`
(weapons.c).

1. Call 40adc0(O), retain the returned definition D. If u16[D+6]!=0
   (40b144), call timer_word_step(O+58). If AX==0 (40b15c), copy D+6 to
   O+59, increment u16[[[O+34]+8]] in 32 bits, divide by the unsigned word
   [[[[O+34]+4]+20]], write the remainder word to the captured state
   pointer. Reread the state pointer and redundantly clear a zero word
   (40b18a). Divisor zero faults (40b17d); it is not normalized.
2. Call 467924(512); add AX to O+54 modulo 65536. Compare signed O+54 with
   `s16(D+2)<<9` as signed32 (40b1b6). Strictly greater expires; equality
   continues. Expiration returns AX=1, the EAX upper word from that shifted
   limit, the EDX upper word from 467924 and DX=wrapped lifetime.
3. If dword O+5b!=0 (40b1c7), call guidance 40aff0(O), retain AX. Call
   467924 with `(current_EDX & ffff0000)|u16[O+52]`. Build the local
   signed-word vector (0,AX,0). If the O+32 word is zero (40b1fd), Euler
   47ebb8(O+c,O+12), then set O+32=1. Call 4801d4(vector,O+12,
   new_position12). Recheck O+32 (40b234), repeating Euler if the transform
   callback invalidated it.
4. Initialize the ten-byte header of a 42-byte segment: pointer O+12,
   sign-extended vector Y, word D+4. Build the impact arguments: segment
   pointer, words from [O+4e]+6/+4/+8, pointer [O+4e]+c, source [O+4a],
   word 5. If the O+56 word is nonzero (40b27b), scale the first two damage
   words with **two delegated** 47e0b0 calls, rereading O+56 for the
   second. The +5f/+61 decay block 40b2b8..40b303 has no reachable
   predecessor: both incoming paths jump to 40b307. Its Jcc at 40b2bd is
   dead code.
5. Signed byte O+41==9 selects the special path (40b33f). With a nonnull
   O+5b, guidance AX=-32768 maps to 32767 (40b351); other negative words are
   negated (40b35e). Magnitude **>16384** (40b372) calls
   49288c(O+26,[O+5b]+26); a signed result **<2000** (40b38c) marks
   proximity. Call 40501c(first_damage_slot,second_damage_slot), preserving
   their EDX/EAX upper words, then clear all three impact damage words. Call
   426b04(&first_scaled_damage,s8[[source+45]+12]). Unless proximity is set
   (40b3cf), call 426528(impact); nonzero full EAX (40b3dd) also triggers
   426a20(O+26,4000,s16(damage),source,0) (40b3df..40b3f8). The radius result
   is ignored for contact: the contact flag is one. Other types call 426528
   directly (40b406) and keep its full EAX as contact.
6. Nonzero contact expires (40b410); otherwise write the transformed
   position **Z,X,Y** to O+2e/+26/+2a (40b40e..40b42e). This path sets
   EAX=O+26 and EDX=new Y. The return replaces only AX by 0/1 (40b42e).
   Contact preserves the upper EAX and full EDX of the final delegated
   call. No direct position write occurs on contact or lifetime expiration.

The body has 17 reachable conditions and three return classes (strict
signed lifetime expiration, full-EAX contact, Z/X/Y commit with AX=0). D is
retained; O+4e, O+4a and the cache state
are reread at the listed boundaries, and callback graph mutations stay
ordered.

### Flight 40a538

40a538..40a814, V14 of 498448. Own code:
`projectile_flight` (weapons.c).

1. Call 40a234(O), retain D. Optional animation is gated by the D+8 word
   (40a558) and timer(O+5c) AX==0 (40a570). Reset O+5d from D+8. The index
   is u16[D+a]; increment the indexed u16 state in 32 bits, divide by the
   indexed u16 resource count, store the remainder, reread and redundantly
   clear zero (40a5c2). State and resource pointers are reached through
   O+34, manager+8 and [manager+4]+20. The divisor must be nonzero
   (fault at 40a5a5).
2. Increment the O+54 word. Signed word lifetime **>s16[D+2]** expires
   (40a5e3; 40a5d7..40a5eb). Otherwise keep the signed old speed O+52; call
   467924 with D+4 in AX and the prior EAX upper word. Add the returned AX
   to O+52 as a word, then average the signed old and new words with an
   arithmetic shift right by one (negative odd sums round down). Store the
   word to O+52; a signed result above s16[[O+4e]+a] is capped (40a619).
3. Owner=[O+4a]. If owner byte +a3 is nonzero and s8[O+41]==3
   (40a62b/634), call 40a488(O,4d22c6) and set word 4d2526=1. Otherwise call
   40a254(O). Call 467924 with the speed in AX, construct the local
   (0,AX,0), cache Euler and transform 4801d4 as in 40b124, including the
   second cache check (40a67f/6b6).
4. The segment contains O+12, signed vector Y and the D+6 word. The impact
   packet contains the segment, words [O+4e]+6/+4/+8, pointer [O+4e]+c,
   source [O+4a] and word [O+4e]+2. Call 426528 (40a721). Full nonzero EAX
   expires (40a729), else copy the transformed position Z,X,Y
   (40a733..744).
5. On expiration or contact (40a74c), repeat the owner+a3 / type==3 gates
   (40a75a/763); call 45b734(1,1) and write byte 4d22d3=0. If O equals
   [49c394] (40a77e) and signed lifetime<D+2 (40a788), set byte 49c398=1.
   No position commit follows a contact.
6. Always reread the owner. If owner+a3 is nonzero (40a79d), the final EAX
   before the AX replacement is owner+92 and EDX is the preceding path's
   result. Otherwise subtract camera [4d24ea]+4/+8/+c from O's XYZ modulo
   2^32 and call 47de72(dx,dy,dz). Signed distance>=40000 (40a7eb) clears the
   O+6 word. Below 40000, nonzero O+6 skips the sound (40a7f2); zero calls
   462830(50), then sets O+6=1. Final EDX is the distance or sound EDX and
   the EAX upper word is that callee's. The return changes only AX to the
   expiration/contact flag (40a80a).

### Validation of preparation, flight and vehicle part

`weapon_prepare`, `bullet_flight`, `projectile_flight` and `vehicle_part`
pass the standard regime. The flight sweep stub writes the inverse matrix
and changes the segment length; no-contact commits the precomputed
endpoint, never the shortened segment. Flight cases reach the original
divide faults at 40b17d/40a5a5. 1,728 mixed-mode states run the three
weapon bodies with independently chosen real callee modes. Mutants caught:
preparation cooldown (case 0), bullet guidance tie (13), projectile
averaging (20), vehicle warning tie (12). Cases in
`tools/reference-game/invitro/weapons_round2_cases.h`.

### Immediate impact 40bf74

**40bf74..40c29c** `(type_word,matrix,range,owner,scale_word)`;
208 local bytes, saves EBP/EBX/ESI/EDI (224 bytes; 244 with outgoing
slots). Prologue `55 8b ec 81 c4 30 ff ff ff` (nine bytes, 1/2/6). Direct
callers 404a65, 40eae0 and 40ecc5 (independent PE-relative transfer scan,
each decoded at its instruction boundary). Own code: `impact` (weapons.c).

Initialize Borland's exception frame through 49630c with EAX=4986cc. Its
descriptor supplies EBP-44 (read-only DATA); the helper stores the prior
fs:0 head there and installs the new frame. The normal exit reloads that
saved head into EAX and fs:0 (40c28d..40c290). **EAX is the incoming
exception-list head on every normal path**, not an effect pointer or
contact result. EDX is the last path value given below. Exception
unwinding is outside the tested normal return domain; the native adapter
still establishes a valid guest frame. The replacement keeps the
runtime-frame bookkeeping: allocation pointers at synthetic EBP-4/-8/-12,
and the state word at EBP-28 goes 20->8 for the single effect, 44->32 for
each intermediate effect and 68->56 for the final effect; core rereads the
previous exception head at EBP-44 at return.

Build a segment with the matrix pointer, full range and word radius 200.
Initialize the local long vector (0,0,0); call 40ffc8(4,u16(type)) for
definition D. Call 47e0b0(s16(scale),s16[D+6]), then again with D+4. Build
the packed impact packet: segment pointer, two low-word products, D+8 word,
D+c pointer, owner, word 5, and the uninitialized exclusion dword below.
Call 426528(packet) (40c03d). Store the full returned EAX as vector Y; zero
instead selects the incoming range (40c048), setting EDX=range only on this
fallback. Call 4628e0(11,matrix+20), then 48043c(vector,matrix,endpoint).

Signed type==1 or type==7 (40c076/40c07b) requests one pool object through
471b30([4a96c2]); null (40c093) exits with the allocation EDX. Nonnull
calls 40b804(object,type_slot,matrix+20,endpoint,owner), where type_slot
combines the owner's high word and the type's low word. Afterwards
decrement the dword pointed to by [6cbee4] by five. The exit keeps the
constructor EDX.

All other signed types copy the matrix's first 20 bytes, including byte 19,
to a local matrix, create the long vector (0,5000,0), copy the matrix
translation Z/X/Y to a local current point, and call
480304(vector,local_matrix,step). The saved contact-or-range is the
remaining length. Signed length>5000 (40c1a3) enters a loop: add step to
the current point in Z/X/Y order, allocate, and if nonnull (40c1e0) call
40b804(object,type_slot,matrix+20,current,owner) and decrement [*[6cbee4]]
by five. Whether allocation succeeds or not, subtract 5000 and commit the
current point to the matrix translation Z/X/Y. Repeat only while the signed
remainder>5000 (40c240): a 5000 tie performs no intermediate allocation,
10000 performs one. Finally allocate once more (40c24c); if nonnull
(40c259), construct the final segment to the endpoint and decrement the
same runtime dword. Final EDX is the final allocator or constructor EDX.
Only the matrix translation and the runtime allocation count are written
directly outside scratch; the query and constructors own their graph
effects.

The 42-byte segment and 24-byte packet occupy disjoint locals. Packet+20 is
never assigned before 426528: original EBP-132 = entry ESP-136. Sweep reads
it as an excluded receiver, so it is explicit context, not padding (callers
supply it, see [Nested frame residues](#nested-frame-residues)). All
vector, matrix and endpoint reads are initialized directly or by delegated
output contracts; the exception-head local is initialized by 49630c.

### Validation of the type-dependent emitters and impact

`emit`, `alt_emit` and `impact` pass 1,001,024 seeded and boundary cases
each for the portable arithmetic, plus 8,192 direct cases, 48 installed
31/a7 cases and 384 mixed-mode states with real preparation, impact, Q10,
transforms and Borland frame initialization. All 15 conditional sites
(seven type emitter, none in the alternate emitter, eight impact) are seen
both ways. Transient packets and raw excluded dwords are compared exactly;
normal-path tests inspect the impact state word and allocation slot
through fs:0. Cases in
`tools/reference-game/invitro/weapons_round3_cases.h`. No emitter-to-next-tick
lifecycle integration or real spawn-allocation graph is claimed by these
synthetic tests.

### Radius dispatch 426a20

**Delegating body** `(point,range,damage,source,excluded)`.
Read and capture the dword list base [4a9af8] at 426a26. The word index and
the hit flag start at zero. Each loop test postincrements the index modulo
65536 (426a9d), compares its old signed value against the freshly read
signed word [4a9afe] (426aa8); when below the count, fetch the next dword
list entry and advance the captured cursor by four bytes. Otherwise select
null. Null stops (426ab9), including a null before the declared count.

For a nonnull B: read pointer B+45 and its dword +14; nonzero skips
(426a55). B==excluded skips (426a59). **Source is argument 4, excluded is
argument 5; they are not interchangeable** (ESI is loaded from the fifth
argument at 426a31). Resolve B.V5c anew and call `(B)` at 426a5e.
Sign-extend the returned AX as radius. Call 49288c(B+26,point) at 426a6b;
subtract the radius modulo 2^32. A signed result >= the full signed range
skips (426a79, JGE); equality is excluded. Otherwise reload B's vtable and
call V70(B,damage_slot,point,range_slot,source) at 426a8d, then set the
local hit=1. The damage slot combines the source's upper word with the
incoming damage's low word. The range slot combines the distance callee's
ECX upper word with the incoming range's low word (426a7b..426a89). The
real distance wrapper ends with ECX pointing at its own expired local point
(4928c9); known V70 bodies consume only the signed low range word. A new
V70 class needs a read-width audit before assuming the same observation
domain; stubs make this stack-dependent upper word observable.

No direct graph writes: V5c, distance and V70 effects are delegated, and V70
may change the count, later entries or tables. EDX after distance is the
saved signed radius, not the distance EDX (426a73). On the
excluded-definition and excluded-receiver skips EDX is B+45's pointer.
V70's pair survives until a later iteration overwrites it. Every entry fetch
writes EAX=the cursor address, even for a null entry; the count exit leaves
EAX unchanged. Final AX=hit, preserving EAX's upper word (426abb); EDX is
the last path value, including the incoming EDX for an initially empty
list. Stack: 12 locals + 16 saved = 28 bytes, maximum 48 with five outgoing
slots. Prologue relocation interval [426a20,426a26). Own code:
`radius_hit` (weapons.c); compare is unsafe.

## Vehicle part 417de4

Vehicle V74 (table 49a282, pointer 49a2f6), cdecl
`(actor,part_word,damage_word,source)`, 292 local bytes and 16 saved bytes;
maximum 324 bytes including four outgoing slots, before callee return
addresses. No x87 in the body. The early player guard 417e0d returns
4240f4's pair; the disabled-part guard 417e22 returns EAX=[A+20e],
EDX=s16(part). Every normal path returns 409904's pair (4187c4). The
phase-by-phase arithmetic and field contract is in
[player_movement.md](player_movement.md#collision-virtual-targets-70-hit-74-part-damage).
Own code: `vehicle_part` (core/src/damage.c, proxy group weapons); every
callback is a guest call, so lifecycle writes need no compare isolation
when the parent runs once. Compare is refused. Callback addresses are
passed, not copied tables.

| Boundary | Argument and ABI evidence | Delegation |
| --- | --- | --- |
| 4240f4 | no pushes at 417e05; AX guard at 417e0a | guest call, zero slots |
| 40dbc0 | (A+206, signed-byte equipment selector+19 in low word), 417e64/417fc3, cleanup 8 | guest call, two slots |
| 40da38 | (A+206,part_slot,damage_slot), 417f00, cleanup 12; own frame at 40da38 and plain RET 40db28 | guest call, three slots; may change pointed-to damage state |
| equipment V68 | (equipment,A,previous_ratio,current_ratio), 417fdf, cleanup 16 | guest call, four slots; resolve vtable each time |
| 417bec | (A), 41802e, pop ECX | guest call, one slot |
| 420a68 | message slot at 417f5d/71,418011/26f/343/39f/5c6/74d/7a8 | guest call, one slot |
| 478628 | ([4a9691],attachment), 418175/634, cleanup 8 | guest call; reread A+238 before clearing array element |
| source V60 | (source,A,0 or saved signed A+a4 byte), 41824e/515 | guest call, three slots |
| 411bc8 / 423430 | (A) / ([A+1b6]), 418276/28c and 418526/53c | guest call, one slot; preserve caller scratch bits on unlink |
| 413e50 | (A+4d,4998ba/49983e/49987c), 41829b/5ff/674; plain RET 413ead | guest call; callback-state initialization is a normal two-slot boundary |
| 40d9f8 | (A+206,0/1), 4183e0/3fa, cleanup 8 | guest call, two slots |
| recursive V74 | (A,0,30000,0), 4184fa, cleanup 16 | guest call; independent hook mode and framework nesting limit apply |
| 409904 | zero slots, 4187c4; loads globals 4a968d/91, calls 478644 at 409911, RET 409919 | guest call; returns callee's complete pair |

The A+1a4 target reference decrement/clear and the A+9d latch are direct
word/dword/byte writes (4182cf..2fb, 41855a..58c); no hidden lifecycle
callee is there. A+2e=-100000 and A+38=1 are direct stores (4185ec/ef);
attachments use the pool boundary above, then the A+23c word is cleared.

**Domain.** Original uninitialized local equipment snapshots require that a
previously null slot does not become nonnull between the two passes. Core
ratios need nonzero signed denominators. The bounded portable domain is
0..59 equipment entries (the first later scratch overlap is index 59 at
EBP-38h), configuration attachment count 0..4, and valid indexed storage.
These are call-domain preconditions, not file-format validation. The
shipped vehicles stay inside them (`tools/reference-game/scripts/domain_vehicle_part.py`):
at most 10 equipment entries (OGRE), attachment counts 0, 2 and 4, and every
capacity the function can divide by is non-zero (RAZOR's zero capacities 0
and 1 sit behind its DAT word `+0x4e` = 1; capacities 10 and 11 are needed
only by PITBULL, count 4, where they are 2000). The in-vitro cases cover 0..4
equipment entries only. Of the simulator `WEAPONS.DAT` records only IDs 0, 26
(LAEW) and 27 install a part with maximum 0; no shipped loadout (MSN list 12,
the shell's INI_*/HERCS/TRN_HERC records and inventory, the tapes'
player.mec) mounts 26 or 27, and the shell marks both unavailable.

**Branches.** 80 reachable conditional sites, all both ways except six
proved one-way edges: 4182c7/418552 repeat an unchanged nonnull target
test; 4182e0/41856b test ECX immediately after XOR-zero; 418749/4187a4
repeat unchanged cleared warning flags from 4186ae/418692. The seeded cases
exercise ratios 80/128/140/192/256 ties, the full signed state-word range,
positive and negative denominator extrema, and 32 real recursive V74 calls
that each assert two final 409904 calls and compare the complete nested
trace, state, result pair and RNG state (validation summary in
[Validation of preparation, flight and vehicle part](#validation-of-preparation-flight-and-vehicle-part)).

## Building emitter 404100 (turret)

404100..4045c5, table 4978ac+18 (pointer 4978c4), direct
caller 46a71a (adjacent decoded PUSH/CALL, definition+2e!=0); no other
direct CALL or initialized DATA pointer. cdecl `(B)`, preserves
EBP/EBX/ESI/EDI, 128 local + 16 saved bytes, at most 20 outgoing argument
bytes (164 total, excluding callee frames). No x87 instruction. The
constructor calls at 404542/58e (40a9c4) and 404557/5a3 (40b43c) make it a
projectile emitter. Own code: `turret`
(core/src/building_weapons.c, proxy group weapons); replacement only,
since allocation, unknown virtual writes and runtime-indexed graph writes
preclude compare.

Fields are unaligned. Pointer and position reads are u32, angle, count and
timer writes u16 modulo 65536, flags u8. The sequential contract (callees
can change memory):

1. At 404116, B+99!=0 calls 403ca8(B) and returns its complete EDX:EAX.
   Otherwise call 403914(B), then timer 467a48(B+21d). If low AX==0
   (404144), call 411fa0(B,30h,0). Compare the returned pointer with the
   freshly read B+1a4 (40415b). On change, decrement the old nonnull
   target's +1a2 word (40415f), write the new B+1a4, increment the new
   nonnull target's +1a2 word (404176), and set B+9d=1. Set B+21e=10000
   even if unchanged.
2. If B+1a4!=0 (40419f), call distance 49288c(B+26,target+26), else
   distance=0. Capture the full signed distance. If >60000 (4041c3) and
   the now-read target is nonnull (4041cd), decrement its +1a2, clear B+1a4
   and set B+9d=1. The repeated null test at 4041d1 can only fall through;
   after XOR EDX, the test at 4041ea can only take its zero edge. No callee
   or intervening write can change these register predicates.
3. If B+1f7!=0 (404214), call timer(B+1f6). Low AX==0 (40422f) advances the
   animation index selected by u16[[B+1f2]+24]. Capture M=[B+34];
   numerator=u16[[M+8]+2*index]+1, divisor=u16[[[M+4]+20]+2*index]. The
   signed IDIV at 404262 requires a nonzero divisor (zero faults, never
   normalized); store its remainder to the freshly read [M+8]+2*index.
   Reread that word (40427f); if nonzero, write B+1f7=u16[[B+1f2]+26].
4. Reload target=B+1a4; null (40429a) finishes. Copy its three position
   dwords to a local point. Capture projectile mode
   `u16[[B+1f2]+2e]==2`. Call target.V30(target,unused-local,offset-local);
   add the three signed offset words to the point in Z/X/Y order, wrapping
   at 32 bits. If the captured signed distance<40000 (4042f5), call the
   freshly read target's V38; sign-extend AX as target speed. In
   non-projectile mode (404311) call 40ffb0(2) and sign-extend
   definition+0a as projectile speed; otherwise projectile speed stays zero.
   If speed>0 (404326) and target speed!=0 (40432c),
   lead = s16(s32(low32(s16(distance ASR 3)*(target_speed*8))) / speed),
   truncating toward zero. Call 4929fc(point,heading-slot,lead), where
   heading-slot keeps lead's high word and loads the heading from the
   then-current target+10 into its low word.
5. Call 403eec(B,point) and keep its returned angle pointer. Call
   timer(B+218); on low AX==0 (404386), set B+21b to the Boolean inverse of
   its old word, then B+219 to the word at image 4973e0+2*new-value (read
   from the original mapping, never embedded). Call timer(B+211). Fire only
   if low AX==0 (4043c8), B+21b!=0 (4043d6), u16(angle[0]+1000)<2000
   (4043f5), u16(angle[2]+1000)<2000 (404415), and the captured signed
   distance<40000 (404421). Equality at either upper threshold fails.
6. Capture M=[B+34], call [M+4].V20([M+4],2). Null (404442) or signed
   returned+4<=-1 (404449) selects image 6c56a8; otherwise select
   [M+16]+32*s16(returned+4), rereading M+16 after V20. Local muzzle
   offset=(300,400,0). Non-projectile mode (404478) enables both barrels.
   Projectile mode calls RNG 492ee0(stream A 4d2599): the first barrel is
   enabled iff the low five bits are zero (404494). If enabled, the second
   is disabled; otherwise a second draw enables the second iff its low five
   bits are zero. Second enabled (4044c4) or first enabled (4044ca)
   proceeds.
7. If B+32==0 (4044d8), call Euler 47ebb8(B+0c,B+12), then set B+32=1.
   Compose 47fa20(selected-record,B+12,local-matrix); extract angles with
   47f9a0(local-matrix,local-angles). If the first barrel is enabled
   (40451b), transform the point 4801d4(offset,matrix,local-position), then
   call 40a9c4(0,position,angles,B,0) in projectile mode (404533), otherwise
   40b43c(2,position,angles,B). If the second is enabled (404563), negate
   offset X as u16, transform again and select the same constructors
   (40457f). Neither constructor result is tested. The local matrix is
   reused even if a constructor invalidates B's cache. Set B+212=1500 and
   B+1f7=1.

Every normal path returns **EAX=0** at 4045bd, with **EDX kept from its
last ordered operation or callee**; the disabled first path returns both
403ca8 registers. The null-target exit has EDX=0, the angle gates keep EDX's
high word and put u16(angle+1000) in DX, RNG rejection has EDX=0, and a
successful shot keeps the last constructor's EDX. This is not a Boolean
pair interface. Own code tracks all three volatile registers at callee
boundaries; scratch pointer identities are local-storage identities.

**Stack.** All scalar locals and point/angle payloads are initialized
before use, assuming V30 supplies its three offset words. Local composed
matrix byte 19 (entry ESP-101) can stay uninitialized on non-copy compose
paths. The real 47f9a0/4801d4 consumers do not read it, but synthetic
probes observe it; the adapter captures it before allocating a frame. The
two unused V30 output bytes and alignment gaps are not consumed. Cases:
`tools/reference-game/invitro/building_emitter_cases.h`.

## Related building emitter 4045c8

Table 497784.V18 (pointer 49779c); no direct CALL. cdecl
`(B)`, preserved nonvolatiles, 192 local + 16 saved bytes and up to 20
outgoing bytes. Own code: `lc_base_update` (core/src/lc_base.c, proxy group
lc_base), with `projectile_create` 40a9c4 and `weapon_record` 40fe84; the
instruction-level contract and validation are in
[vtables.md](vtables.md), section "`0x4045c8(B)` lc_base_update".

The disabled B+99 gate (4045e7) calls 403ca8(B) and returns its pair.
Otherwise 403914(B), save heading u16[B+10], add 1555h to the heading,
clear cache B+32. If B+a5!=0 (40463a), skip to the heading restoration.
Otherwise scan the part index starting at 1: signed current word
[B+205]+11*i versus signed definition word [[B+1f2]+14]+30*i (404664),
increment while i<signed definition+12 (40467c). If i equals that count
(40468b), set B+a5=1. In either case process three banks i=0..2 (404ab1)
at B+209+31*i, rotating the heading by 5554h and clearing the cache after
each bank.

Each bank calls timer(bank+18); low AX==0 (4046c4) calls
411fa0(B,10h,3000h), writes the bank+1b target and bank+19=10000. A nonnull
bank target (4046ec) calls distance(B+26,target+26), copies its position,
then target.V30(target,unused-local,offset-local), adds the signed offset
words, and calls 492990(local-angles,target-point,B+26). Subtract B's three
angle words modulo 65536. Capture global 4a95bc, then iterate three weapons
j=0..2 (404a82), using global record stride 22 and bank subrecord stride 8.
Skip if word [B+205]+11*(2*i+1+(j!=0)) equals the corresponding definition
word (4047e0). Timer(subrecord+3) zero (4047fc) toggles subrecord+6 between
0/1, loads its signed-indexed base word from 4973f4, calls bounded RNG
492f24(stream A, slot with low word at 4973f8), adds AX and stores the
cooldown subrecord+4. These table words are read at run time, not copied.

Timer(subrecord) low AX must be zero (404855) and the phase word
subrecord+6 nonzero (404860). Gate with u16(local-yaw+record+2) <
u16(2*record+2) (40487f), the corresponding pitch/record+0 gate (40489d),
and signed distance strictly less than signed record+4 (4048a9). Copy the
record's position offset, call 47eb30(B heading,local rotation), then
47ffe8(offset,rotation,rotated), keep the original offset Z, and add the
rotated X/Y/Z to B's copied position.

For j==0 (40493c), update B+1a4 to the bank target (40494e), with old
decrement (404957), new increment (40496e) and B+9d=1 on change. Call
40a9c4(3,position,B+0c,B,0), then clear a nonnull B+1a4 (4049ac),
decrementing its count. The repeated test 4049b9 must fall through and the
post-XOR 4049d2 must take zero. For j!=0, call
492990(local-angles,target-point,position), 40fe84(8), copy the position
into the local matrix translation, call Euler 47ebb8(angles,matrix), then
**40bf74(3,matrix,definition+30,B,word-slot)** (direct impact caller
404a65), where the last slot's low word is definition+38 and its high word
comes from Euler EDX. Store record+14 to subrecord+1 after either emission.

The normal exit restores the original heading, clears B+32, returns EAX=0
and EDX with its previous high word and the restored heading in DX
(404ac5..404ad5). No direct x87 operation. All other state and scratch
register effects are delegated in order. Scalar locals are initialized
before they are read. Euler matrix byte 19 at entry ESP-177 and planar
rotation byte 19 at ESP-69 are uninitialized on paths where their callees
keep padding; the former can be passed on to immediate impact. V30 and
492990 output payloads rely on their callee contracts.

## Outside-V28 emitter 4226a0

**4226a0..422a39.** One cdecl actor slot, 60 local bytes plus
three saved registers, plain RET; called at 422ceb. Own code: `target_emit`
(weapons.c, proxy group weapons).

It snapshots the target [A+1a4] position, calls heading 492934 and distance
4928d0, then the actor and target V38. A signed distance is capped at
200000. State word A+1fe=1 and distance<65000 select definition
40ffb0(2)+10 as the lead denominator. Positive actor speed and nonzero
target speed enable a signed division:
low32(s16(capped_distance SAR 3) * (s16(target_speed)<<3)) / actor_speed,
then the low-word signed lead; the multiplication discards high bits before
the division. 4929fc changes the temporary target point and 492934
recomputes the wrapped yaw difference. All calls are guest boundaries.

State 0 clears A+5a, reverses yaw by 32768, switches to state 1 when the
distance is strictly greater than 90000, and delegates altitude target
Z+10000 to 422108. State 1 uses 422108(A,0) when A+ae is set, otherwise
pitch 49295c then 422098. It resets the state to zero unless the wrapped
yaw is in [-8192,8191], the height difference is >=3000 and the distance
is >=10000; at distance>=65000 it uses target Z+10000 instead. Repeated
state reads follow callbacks.

Emission also requires a nonzero state, distance<65000, wrapped yaw and
pitch errors in [-1000,999], signed rate A+1f4 in [-10,9], and an expired
timer at A+21e. It transforms the short muzzle offset (500,200,-100),
rebuilding the A+12 matrix when cache word A+32 is zero. A+5a=0 and
distance<30000 allocate one type-0 projectile with A+233's low-word slot
(422994), then set A+5a=1. Otherwise it allocates two type-2 bullets
(4229b1, 4229ff), negating muzzle X and checking the cache again between
allocations. It writes cooldown word A+21f=1500. Finally, signed state<2
writes A+21c=180 and calls 4222fc(A,yaw,control,0). The final callee's full
EDX:EAX survives; otherwise the last arithmetic or callee pair survives. No
x87 instruction.

**Residue.** The local control word at EBP-10h is set only in the state 0/1
paths. A negative state, or a callback changing a state >=2 to <2 before
the final dispatch, exposes entry ESP-20 residue. The replacement keeps
that input through an entry shim and an explicit context value; the 31/a7
probe confirms it on both dispatch paths.

**Validation.** 24 reachable conditional sites, all both ways. A
384-state mixed test uses real heading, timer, Euler and short-point
transforms with independent caller and leaf modes. The strict
distance>90000 mutation is caught at case 70.

## Swept query 426528

Hook `sweep`, core `es2_sweep_registers` (core/src/weapons.c). Range
426528..426a1c, cdecl, one slot: the impact packet P. The frame is 0xd0
bytes; prologue `55 8b ec 81 c4 30 ff ff ff` (1,2,6), the same bytes as
40bf74. The direct-call inventory (entry-aligned read and an independent
raw relative-call target scan) contains transform/distance/heading hooks,
the runtime initializer, terrain 428048, effects/allocation/RNG, the
notification and the receiver virtuals V20/V50/V64, and the two arithmetic
leaves 426b04 and 427238 below. 492860 is not on the sweep path. Actor-list
selection 423974 is a separate read-only graph query
([below](#missile-steering-40a488-and-ally-selection-423974)). 427d98
clears dword 4aaad0 and 427da0 reads it; neither is a clock operation
([hit accumulator](#427d98-and-427da0-the-hit-accumulator-of-sweep)).

**Exception frame.** The body starts with the runtime initializer 49630c,
EAX = descriptor 49a909. That routine places the frame's exception record at
EBP plus the descriptor's offset (here EBP-0x24): saved head, handler,
descriptor, ESP, a zeroed context word at record+0x10 (EBP-0x14) and a
counter copy, then links the record into FS:0. The epilogue reloads the saved
head into **EDX** and stores it to FS:0, so the returned EDX is the previous
chain head; EAX is 0 without contact, otherwise the final segment length + 1.

**Packet and segment as used here.** P+0 segment S; P+4, P+6 signed words
scaled in place by 426b04 (see below; +6 first) when the owner is nonzero;
P+8 and P+0x12 are only read by receivers; P+0xa points to an effect table
whose words +8..+0xf are indexed by `RNG & 3`; P+0xe owner actor or 0;
P+0x14 the excluded actor. The weapon callers never assign P+0x14 (residue,
captured by their entry shims, see
[Padding and transient packets](#padding-and-transient-packets)); 4198f4's
five calls use the static packet at 49a170. S+0 matrix M (nine words, two
padding bytes, translation dwords at +20); S+4 signed length; S+8 radius
word for the terrain query. 426528 writes S+0xa..S+0x29: M's first 20
bytes, then the translation copied in dword order +8, +0, +4, transposed by
47df19 in place, the translation negated in the same order and rotated by
480304 into S+0x1e. This is the world-to-segment transform the receivers
use.

**Order of work.**

1. Initializer; index word 0; cursor = [4a9af8] read once; contact 0.
2. With owner: 426b04(P+6, side), then 426b04(P+4, side); each reloads the
   owner and the signed side byte at definition +0x12.
3. 48043c({0, len, 0}, M, frame) — the endpoint is never read again, but the
   call (a hooked transform) must happen.
4. The inverse transform above. M is reread from S+0 after step 3.
5. Owner (ESI) is read from P+0xe only now. 427d98 clears [4aaad0];
   428048(&out, S) returns AX ≠ 0 on terrain contact with out = {distance,
   4aaad4}; a strictly shorter signed distance sets the length, the hit
   pointer to out+4 and contact. [4aaa6c] = 427da0(); 427d98 again.
6. Actor loop: the signed word count [4a9afe] is reread on every pass, the
   cursor is not; a null entry ends the loop. Skip the owner, P+0x14 and
   actors whose definition dword +0x14 is nonzero. V20(actor, P) returns a
   distance; zero skips. Without owner the hit is recorded directly.
   With owner: same side = the two definition side bytes compare equal as
   words. If owner+0x1a4 (target) is not this actor, hit = actor+0xc;
   otherwise owner byte +0x9e = 1, and when actor+0x1b2 is nonzero and actor
   byte +0xa2 is zero, 423430(actor+0x1b2); hit = 0. Actor byte +0x99 then
   suppresses the notification; for different sides +0xa5 or +0xa4 also do.
   Before notifying, same side with owner byte +0xa3 set and different
   definitions calls 423974(owner definition, owner); a found ally closer
   than 30000 by dist_gate(ally+0x26, owner+0x26) gets 41f790(ally). The
   notification is V50(actor, owner, ECX) where only CX is set from P+4; the
   upper half is whatever the preceding path left in ECX (0/1, the popped
   423430/41f790 argument, or a callee's returned ECX), so register flow
   must be exact. Recording a hit sets S+4 = distance and both contact
   flags; a signed distance below 500 ends the loop.
7. [4aaa68] = 427da0().
8. Terrain-only contact: endpoint {0, len, 0} into frame buffer EBP-0xac;
   471b30([4a961e]) allocates an effect; if it did, context word 0x14,
   492ee0(4d2599) & 3 picks the effect word, 407f1c(object, word, endpoint,
   0, 0), the dword at [6cbee4] drops by 4 and the context word becomes 8.
9. Contact, hit, owner and owner target all nonzero: endpoint into EBP-0xd0,
   reach = 4928d0(owner+0x26, endpoint), gate = 49288c(owner+0x26,
   target+0x26). If reach < gate (signed), turn = (heading to endpoint −
   owner heading) − (heading to target − owner heading) as a word; a signed
   turn below 0x2000 calls the owner's V64(owner, hit).

**Branches.** 37 conditional sites. Three taken edges are unreachable:
426706 and 426716 retest EBX/ESI that the loop exit and 4266dc already proved
nonzero, and 426783 retests actor byte +0x99, checked zero at 42674f with no
call or guest write in between. The harness requires their fallthrough and
rejects an observed taken edge. **Stack.** No local is read before it is
written; only the packet's +0x14 can carry caller residue. **ECX** at return
is dead at all nine call sites (each pops ECX), so the pair result suffices.

**Implementation.** `es2_sweep_registers` owns the control flow and
register flow; every game call, including hooked leaves and the three
virtual slots, goes through the guest call bridge. The two hit-accumulator
leaves 427d98/427da0 are done inside the core
([below](#427d98-and-427da0-the-hit-accumulator-of-sweep)). The adapter
gives it a 256-byte scratch frame with a synthetic EBP at +208 (as for
40bf74), so the runtime record, its context word and the object slot sit at
their original frame offsets, and restores FS:0 from the returned EDX.
Compare mode is refused (allocation, RNG and notifications).

### Leaf hooks side_scale 426b04 and cell 427238

**426b04 `side_scale`: side/difficulty damage-word scaling.** Range
426b04..426b46, 67 bytes. cdecl arguments are a writable word pointer and a
full dword side selector. The sole conditional branch at 426b12 chooses
table 49a73c when **all 32 side bits are zero**, otherwise 49a744. Read
signed word difficulty at 4a9e5c; read a signed word from the selected table
plus twice that signed index. There is no index clamp, and all address
arithmetic wraps at 32 bits. Read the destination as signed word, then call
47e0b0(table value, old destination value) at 426b36 through the
independent guest hook entry. Both arguments are fully sign-extended.
Incoming EAX and EDX at that call both equal the old signed destination;
ECX retains its incoming full value. Store returned AX to the destination
at 426b3e, preserving its neighbors. Return full EAX/EDX from the delegated
callee; ECX becomes the old signed destination due to the local-slot pop.
EBX/ESI/EDI/EBP are preserved.

No x87 operation; the delegated Q10 state propagates. The four-byte local
is completely initialized before use (the saved-ECX local is overwritten
with the old signed word); no uninitialized stack is read. Maximum own
outgoing stack is 28 bytes below entry ESP, plus the callee return address.
The only direct write is the two-byte destination; aliasing with
difficulty/table storage follows the stated reads-before-call and
store-after-call ordering. A callback could mutate either table and its
effects must survive. The fixed difficulty read and signed table index are
runtime guest reads, not copied constant tables. Entry-aligned direct
calls by raw relative-call targets: 40b3c5, 426573 and 42658d; no
non-CODE pointer candidates. The returned ECX (the popped old word) is dead
at all three. Compare is refused because the Q10 multiply is a delegated
call.

**427238 `cell`: signed grid-height query.** Range 427238..42735e, cdecl
`(resource, point, radius_slot)`. The point supplies only X/Y dwords.
Resource words +2a/+2c are signed grid dimensions, +32 is shift count, +434
is a pointer to a row-pointer array, and +34 contains the 256-entry dword
height map. A cell is an unsigned byte indexing that map. All guest
addresses wrap at 32 bits. Reads may be unaligned. The function has no
calls, guest writes, x87 instructions, image globals or constant-table
copies. EBP/EBX/ESI/EDI are preserved. Full EDX always equals `resource`;
EAX is the result below. Its 20 local bytes plus saved registers use 36
stack bytes; each local is initialized before it is read. No entry residue
or scratch pointer reaches the returned pair.

Read the signed resource shift and form a dword threshold by shifting 1
left by `(shift-1)&31`. Compare this threshold as signed32 against the
signed low radius word (42725b). If threshold is greater than or equal to
radius, use the single-cell path. Arithmetic-shift X and Y right by the
resource shift masked to five bits, then interpret only each result's low
word as signed. The four bounds checks (42732a/427330/427335/42733b) require
`0 <= x < signed width` and `0 <= y < signed height`. Failure returns zero
without reading the row-pointer array. Success reads row[y], its byte at x,
and returns the corresponding full dword height. Negative height values are
returned unchanged on this path.

Otherwise form inclusive signed-word bounds by arithmetic-shifting the
wrapped dword values X-radius, Y-radius, X+radius and Y+radius, then taking
the low word. The row pointer address starts at the array base plus four
times the signed lower Y bound. Traverse Y, then X, with inclusive signed
word comparisons, incrementing the full counters while testing their low
words. Skip each negative or dimension-exceeding coordinate before any
row/cell dereference. Begin the maximum at zero; update it only when the
selected dword height is strictly larger as signed32 (4272f9). This path
therefore never returns a negative maximum. It returns that maximum through
the reachable jump at 427312 to the return at 427358 (427314 is
unreachable), including zero for empty or fully clipped rectangles. The
loop/coordinate conditions are at 4272bf, 4272ce, 4272d3, 4272d9, 4272de,
4272e4, 4272f9, 427302 and 42730d. Together with the entry split and four
single-cell checks, this is **14 conditional sites**.

The original reads the row-array pointer on the rectangle path even for an
empty rectangle, but dereferences rows only for coordinates passing bounds.
**Trap:** original word-counter wrap can prevent termination: an entered
outer loop whose upper Y bound is 32767 never exits; likewise an entered
inner loop with upper X bound 32767. The supported oracle/API domain
requires valid accessible addresses and terminating original traversal.
This is an explicit precondition, not a clamp, iteration cap, silent
fallback or normalization. The implementation preserves wrapping counters
and read order in that domain. Read-only graph compare is safe for stable
inputs; no writable spans or callee counters are needed. The six-byte
prologue signature is `55 8b ec 83 c4 ec`, with instruction lengths 1/2/3.

Known direct callers inspected at instruction boundaries are 4273b8 in the
point-offset wrapper and 427504 in the sampled segment query 4273c8. The
latter is reached from object-query helper 427da8 through 427fda; that helper
is used by the swept-impact receiver query graph. Further callers are not
enumerated. The returned ECX is data-dependent; a conservative liveness
walk (any read of ECX/CX/CL/CH before a full write, following returns up to
six frames: 427360, 427c68, 404ae4, 418f74, 42354c, 46a614) found no use
before a full write. The only calls reached while it is live are 46ff90 and
4928d0, which write ECX before reading it, and 471b30, whose `push ecx`
only fills a local slot; the caller pops ECX right after. The pair result
therefore suffices. Compare allowed.

## Terrain segment walk 46e980 and its sweep wrapper 428048

Hooks `segment` (46e980..46fdb5) and `sweep_terrain` (428048..4280f3), both
in `core/src/weapons.c`. The octant jump table at 46ec4f was decoded from
the image and the code read from its eight targets.

### 46e980: grid walk along a segment

cdecl `(map, start, end, radius, out, mode)`; `map` is the terrain grid
described for 46e180 ([player_movement.md](player_movement.md#terrain-height-46e180)),
`start`/`end` are XYZ dword points. Five direct callers: 41643b, 416597 and
41dcac pass mode 1 and radius 0, the wrappers 428048 and 4280f4 mode 0.

1. d = end − start per axis (32-bit). While any of dX, dY, dZ lies outside
   ±32000, all three are halved arithmetically (46e9bc..46e9f6).
2. Mode ≠ 0: the low words of dX, dY, dZ go to the frame words at EBP−28h
   and 46c23c(&words, 800h) runs (direction for the line-of-sight callees).
3. Q16 slopes through 47e068 ((a << 16) / b, IDIV; no overflow because of
   step 1): Y/X and Z/X when dX ≠ 0, X/Y and Z/Y when dY ≠ 0, otherwise
   10000h each.
4. Octant from the signs of dX, dY and their magnitudes (46ea96..46eafb):
   dX ≥ 0: dY ≥ 0 → 0 if dY < dX, else 1; dY < 0 → 6 if −dY > dX, else 7.
   dX < 0: dY ≥ 0 → 2 if dY > −dX, else 3; dY < 0 → 4 if dY > dX, else 5.
   Octants 0, 3, 4, 7 step along X (major axis), 1, 2, 5, 6 along Y;
   X decreases in 2..5, Y decreases in 4..7.
5. prev = start (copied to EBP−90h), cell = 1 << (low five bits of
   [map+108h]); column/row = start X/Y shifted right arithmetically by the
   same count. Outside 0 ≤ column < 1 << [map+100h], 0 ≤ row <
   1 << [map+104h] (signed compares; a count of 31 makes the limit negative)
   the result is 0 with EDX = cell (column < 0) or 1 << [map+100h]. The X
   boundary is column << s, plus cell for octants 0, 1, 6, 7; the Y boundary
   row << s, plus cell for octants 0..3.
6. Loop. The cell (column, row) of prev is bounds-checked again (fail → 0,
   EDX = the previous cur.Z; the first pass cannot fail). One step:
   - If a crossing point was saved, cur = saved and the major counter and
     boundary step.
   - Else, if end lies on this side of the major boundary (≤ for increasing,
     ≥ for decreasing), cur = end and the "reached" flag is set. Else cur is
     the point where the segment meets the major boundary: on the first pass
     the minor and Z coordinates are prev + t·slope through 47e08d
     ((a·b) >> 16 from the 64-bit product), and the per-cell increments
     ±47e08d(cell, slope) are stored; later passes add those increments.
   - If cur passes the minor boundary (> for increasing, < for decreasing),
     cur is saved, cur becomes the minor-boundary crossing (prev + t·slope)
     and the minor counter and boundary step; otherwise the major ones do.
   Direction codes (EBP−18h): X+ 1, Y+ 2, X− 0, Y− 3. **Quirk:** octant 2
   computes its minor crossing, and octant 3 its first point and its minor
   crossing, with an inline `imul`, i.e. (low 32 bits of a·b) >> 16 instead
   of 47e08d. The results differ once a·b leaves 32 bits.
7. done = reached and no saved crossing. Mode ≠ 0: on the first pass
   46ff90(map, words, prev) ≠ 0 (AL) → out = prev; when done,
   46ff90(map, words, cur) decides between 0 and contact, and contact with
   out set calls 46fdb8(map, prev, cur, column, row, out); otherwise
   470080(map, column, row, words, direction) (full EAX): 0 continues,
   2 → 46fdb8 as above, any other value → out = cur. Mode 0: when done,
   46e180(map, cur) ≥ cur.Z (signed), or on the first pass
   46e180(map, prev) ≥ prev.Z, leads to 470798(map, prev, cur, radius,
   column, row); a nonzero pointer p gives out = *p; otherwise the result
   is 0. When not done, 470468(map, column, row, cur, direction) (AL) ≠ 0
   leads to the same 470798 test. A continuing pass copies cur to prev and
   the counters to column/row.
8. Return: EAX 0, or AL = 1 with the upper 24 bits of the last value in EAX
   (prev.Z, cur.Y, p.Y, p or a callee result, path by path). EDX is the
   value the path leaves (a callee's EDX, a coordinate or a boundary). The
   two `out = *p` copies leave different registers: when done EAX = p.Y and
   EDX = p.Z, otherwise EAX = p and EDX = p.Y.

**Branches.** 89 conditional sites. Three edges are unreachable: the taken
edge of 46ec02 (the done flag tested there was cleared at 46ebaf), the taken
edge of 46ec42 (octant ≤ 7) and the fallthrough of 46ebd0 (the fourth
`sub 2; jb` on the octant always jumps). 46fcf6 is a dead `jmp`.

**Stack.** Every local is written before it is read: the saved crossing
only after it was stored, the per-cell increments only after a first pass
that computed them (the major end test stays true once it held, so no
non-first pass follows a first pass that took the end branch). The callees
receive pointers to the direction words (6 bytes are read), prev and cur,
so the replacement keeps them in scratch at their original EBP offsets.
**Callee residue:** 46ff90 reads its own uninitialized local −18h as a
pointer when the cell's flags & 3 is 3 (the switch at 47002f has no case
for it). The terrain builders write flags & 3 as 1 (46c13b) or 2 (46c1bc)
exclusively, and every later writer (46c68a, 46c68f, 46c6c8, 46cab0,
46cb15, 471460) keeps bit 1 and stores only bits 2..7, so flags 3 does not
occur in game terrain. The hook relies on that.

**Registers after return.** ECX and EDX are dead at all five callers, by a
conservative walk over jumps, calls (callee summaries) and returns:
41643b/416597/41dcac write both before any read; 428048 returns to the sweep
(426662), which writes them; 4280f4 returns to 4126b9 and 41dd11 → 41dd2b →
41ddf8, which write them. EAX is used as AL only. The hook still reproduces
EDX exactly from the callee results; only real callees that leave a frame
address in EDX make it depend on the stack depth, which is harmless because
EDX is dead.

### 428048: terrain contact for the swept query

cdecl `(out, segment)`. Sets byte 49ab6f to 1 if it is 0. Transforms
{0, segment length, 0} by the matrix at [segment] (48043c, into a frame
point), then runs 46e980([4a0be0], matrix + 14h, that point, signed word
segment+8, frame out, mode 0). Without contact EAX = 0 and EDX = the walk's
EDX. With contact: out+0 = 49288c(matrix + 14h, contact point), out+4 =
4aaad4h, and the contact point goes to 4aaaee (X), 4aaaf2 (Y), 4aaaf6 (Z);
EAX = 1, EDX = contact X. Two branch sites (42805b, 4280ae).

### Validation (in vitro)

`segment_cases.h`: the standard regime with all seven callees as logging
stubs, plus 12,288 cases with the real callees on generated terrain (887
contacts) under rotating modes of the nested hooks `terrain_height` and
`dist3d_delta`, comparing EAX and all memory. All 89 sites are covered,
three of them one-way as proved above. `sweep_terrain`: the same kinds of
cases, the real ones with `transform_point`, `dist_gate` and `segment`
nested (39 contacts in 12,288 cases); both sites both ways. Mutation
witnesses: octant 2 using the 64-bit
product (case 282) and an unconditional init-byte write (case 0).

## Terrain walk callees

Eight hooks below `segment`, in
`core/src/weapons.c`. "Cell (x, y)" is the grid record at index
(y << columns) + x, valid for 0 <= index < 1 << ((columns + rows) & 31)
(signed); outside it is 0. Heights are byte × [map+118h] + [map+110h].
Every helper reads the grid fields again at each use.

**46ff4c `slope_test`** (map, words, normal): normal points at three words
of a cell (+1 or +7). Signed z below 60eh → AL = 1 with AH = z's high byte
and the upper EAX word unchanged from entry; above → 0; exactly 60eh →
47dce7 (dot product of the two word triples, 32-bit) < -7a1200h. EDX is
the entry EDX, or the 0/1 result after the dot product. Read-only.

**46c23c `normalize`** (words, length): while the 47de72 length of the
three words exceeds 7d00h (signed word compare), halve Z, X, Y
arithmetically; then q = (length << 16) / length2 (IDIV) and each word
becomes the low word of 47e08d(word, q). EAX = words, EDX = the high
product dword of the last multiply. **Precondition:** nonzero words (the
original divides by zero otherwise).

**46fdb8 `diagonal`** (map, prev, cur, column, row, out): in cell
coordinates (X mirrored as size − x when flags & 3 is 0), if prev and cur
are on the same side of the diagonal x = y, out = prev (EAX = prev.y,
EDX = out). Otherwise eight bisections of the midpoint (arithmetic >> 1 of
the 32-bit sum) stop early when its x equals y; the midpoint goes back to
world coordinates, out = {x, y, 0}, EAX = 0, EDX = y. **Precondition:** the
cell exists; otherwise the original reads its flags through a null pointer.
The walk only calls it with an in-grid cell.

**470468 `edge_test`** (map, column, row, point, direction): the two
corner cells of the crossed edge: 0 (x, y)/(x, y+1), 1 (x+1, y)/(x+1, y+1),
2 (x, y+1)/(x+1, y+1), 3 (x, y)/(x+1, y); another code or a missing cell →
0. point.z <= the lower height → AL = 1 over the map pointer (EDX = frame
address of that height, dead in the walk); above the higher → 0 (EDX =
point.z); otherwise the edge height ha + ((hb − ha)·t >> shift) with t the
point's offset along the edge (Y for codes 0/1, X for 2/3) decides: EAX =
EDX = height >= point.z.

**46ff90 `face_test`** (map, words, point): the triangle of the point's cell
(flags 1: +1; flags 0: +7 if size < fx + fy, else +1; flags 2: +7 if fy <=
fx, else +1) is checked by 46ff4c; outside the grid AL = 1 (EDX = 0).
**Residue:** flags 3 has no case, so the original passes its uninitialized
local −18h as the normal pointer. An entry shim copies that dword (entry ESP
− 1Ch) before the hook frame overwrites it. Game terrain never has flags 3
(see the segment section).

**470080 `face_cross`** (map, column, row, words, direction): the near
triangle (0: +1, 1: +7, 2: +1 if flags & 3 else +7, 3: +7 if flags & 3
else +1) through 46ff4c; AL ≠ 0 → 2. Then the neighbour in the direction
(x−1, x+1, y+1, y−1) and its far triangle (0: +7, 1: +1, 2: +7 if flags & 3
else +1, 3: +1 if flags & 3 else +7) → its AL. A missing cell → 1 (EDX 0);
another direction → EAX = direction − 3, EDX unchanged. The registers at
both 46ff4c calls are reproduced, because 46ff4c keeps the entry EAX high
word and returns the entry EDX.

**470798 `contact`** (map, prev, cur, radius, column, row): radius is not
read. Byte 4a0c4c becomes 1 if it is 0. Plane 1 is the normal at cell+1
with constant −height·n.z; the segment is prev/cur minus the cell origin
(Z unchanged). 47e610(plane, segment, 6b505c) writes the contact. Accepted
if flags & 3 is 1, if x < y for flags 2, if x + y <= size for flags 0
(flags 3: never); the contact is then shifted by the origin and EAX =
6b505c. Otherwise, unless flags & 3 is 1, plane 2 is the normal at cell+7:
for flags 0 with the next column's height (missing cell → 0) and the
segment and origin moved by one cell in X; **for flags 2 with plane 1's
constant**. A contact there is shifted the same way. EDX follows the path
(origin X, contact X, X + Y or 47e610's EDX).

**4280f4 `terrain_line`** (out, start, end, radius word): 46e980([4a0be0],
start, end, signed radius, frame out, mode 0); without contact EAX = 0 and
EDX = the walk's EDX; with contact out = 49288c(start, contact) and EAX =
(distance & ffff0000h) | 1, EDX = 49288c's EDX.

**Registers.** ECX is dead after all eight at their callers (46e980, the
sweep wrapper and 41dd0c/4126b4 for terrain_line; checked with the same
walk as for 46e980). EDX after `edge_test`, `contact`, `face_cross` and
`face_test` is dead in 46e980.

**Branches.** 99 sites. One-way: 470989 and 47021e (i+1 from a valid index
is never negative), 470152 and 47041a (i−1, i−2^columns never reach the
limit), 470745 (second "sub 2; jb" on a direction already bounded to 0..3),
46fde3/46fdfa (`diagonal`'s caller contract). Wrapped exponent sums
(columns 31 with a small row count, for instance) do reach the other
negative-neighbour edges, so the generator includes them.

### 47e610 `line_plane`: segment/plane intersection

Range 47e610..47e73a. With `line_plane` replaced, everything below
`segment` is own code.
Core `es2_line_plane_registers` in `core/src/terrain.c`, adapter
`proxy/line_plane_hooks.h`.

**ABI.** cdecl `(plane, segment, out)`, three slots, caller cleanup; the
entry EAX/ECX/EDX are not read. Frame `push ebp; mov ebp,esp; add esp,-3Ch;
push ebx/esi/edi` (signature `55 8b ec 83 c4 c4`, relocated 6 bytes,
lengths 1,2,3). No x87 instruction anywhere in the subtree; all arithmetic
is 32-bit integer.

**Inputs.** plane = four dwords n.x, n.y, n.z, c (the plane n·p + c = 0);
segment = six dwords, start S at +0..+8 and end E at +0Ch..+14h. Both are
read-only. out = three dwords, written only on a hit.

**Callees, all pure and computed in core (none is hooked).**
- 4953c4(p) returns p (it calls 49545b, which returns its argument); no
  memory effect. Called once on the direction local, for nothing.
- 47eaa1(dst, a, b) → 47df84(dst, a, b): the signed 64-bit product a·b
  (one-operand `imul`) stored as two dwords at dst; returns dst.
- 47eabe(dst, src, d) → 47de2d(dst, src, d): `idiv` of the 64-bit value at
  src by d; quotient to dst+0, remainder to dst+4; returns dst.
They write only the function's own frame locals. (The compiler leaves den
pushed for 47eaa1 as an unused fourth slot that becomes the third argument
of the following 47eabe call.)

**Body.**
1. D = E − S per axis (32-bit wrap), into locals −3Ch/−38h/−34h.
2. den = n.x·D.x + n.y·D.y + n.z·D.z, each product the low 32 bits of a
   two-operand `imul`, summed modulo 2^32.
3. den = 0 → EAX = 0, EDX = n.y·D.y (low 32), return.
4. num = −(n.x·S.x + n.y·S.y + n.z·S.z + c), same wrapping arithmetic (the
   first product is a one-operand `imul` whose EDX is overwritten at once).
5. Range test, signed: den > 0 requires 0 ≤ num ≤ den; den < 0 requires
   den ≤ num ≤ 0 (47e684 `jle`, 47e688 `jl`, 47e68c `jge`, 47e697 `jg`,
   47e69b `jle`). Otherwise EAX = 0, EDX = n.y·S.y (low 32).
6. Hit: for X, Y, Z in this order, q = trunc((D·num) / den) with the exact
   64-bit product and a truncating signed division, then out[k] = q +
   segment[k], where segment[k] is **read again from memory** after the
   previous out word was written (the writes are interleaved with these
   reads exactly as listed). The division cannot fault: den ≠ 0, and
   |num| ≤ |den| with equal signs keeps |q| ≤ |D| with the sign of D, so
   the quotient always fits (D = −2^31 gives at most −2^31).
7. Return on a hit: AL = 1 over the upper 24 bits of out (EAX = (out &
   FFFFFF00h) | 1); EDX = the frame address EBP−28h (`lea edx,[ebp-28h]`,
   the Z quotient local).

The parameter is t = num/den ∈ [0, 1] along S→E; the point is S + D·t,
each axis truncated toward zero separately.

**Registers after return.** ECX (n.z·D.z, n.z·S.z or out.z by path) is not
part of the hook's result: it is dead at both callers. 470798 at
4708b9/4709f3 tests AL; every continuation writes ECX or EDX before reading
it, or returns to 46e980, where ECX and EDX after `contact` are dead (see
the registers paragraph above). 47e73c (a polygon clipper against one
plane, called twelve times from 494d3c..495066; not reached in any suite of
the map) at 47e8e1/47e952 tests AL and writes ECX and EDX before any read on
both edges. EDX is reproduced exactly on the miss paths; on a hit it is a
frame address, which the hook gives from its own scratch frame (dead, as
for `edge_test`). The upper EAX bits on a hit are the out pointer's, exact.

**Stack.** No local is read before it is written (the products and
quotients are stored by the callees before they are read back), so there
is no residue dependence and no entry shim. The locals the original leaves
below the caller's stack pointer are not reproduced; neither caller reads
uninitialized stack that this frame could have written (470798 and 47e73c
read only locals they wrote).

**Aliasing.** out is written word by word between the rereads of
segment+0/+4/+8, so an out that overlaps the segment changes later words
exactly as in the original; the core keeps that order. The live caller
passes the global 6b505c as out and its own frame for plane and segment.

**Callers.** 470798 (`contact`) at 4708b1 and 4709eb (plane 1 and plane 2)
and 47e73c at 47e8d9 and 47e94a. The DBSIM map records only 470798 as a
live caller. 47eaa1/47eabe have
further callers (47e118, 47e450, 4300c2..4301e6 for the inner helpers);
they stay original there.

**Compare mode:** accepted with a 12-byte span on
out and a comparator. The hit's EDX is a frame address that differs between
the original and the replacement by construction (as for `edge_test` and
`contact`), and the hit's EAX carries out's upper 24 bits, so a miss
compares EAX = 0 and EDX, a hit compares EAX = (out & FFFFFF00h) | 1 with
out remapped to the clone and only checks that both EDX values lie in the
thread's stack ([compare_mode.md](../reference-game/compare_mode.md#spans-and-comparators)).

**Branches.** Six sites, all in 47e610 (47e65c, 47e684, 47e688, 47e68c,
47e697, 47e69b); every edge is reachable (den zero/nonzero, both signs,
num on either side of 0 and of den). The callees are branchless.

## Vehicle shield geometry 413cc4

Hook `shield_geometry`, range 413cc4..413e4c; core `es2_shield_geometry_registers` in `core/src/node.c`,
adapter `tools/reference-game/proxy/node_hooks.h`. The only caller is the
vehicle receiver V20 418ba8 (vtable 49a282, slot 20h) at 418cbe, after it has
composed the impact matrix with the vehicle matrix (418ca2).

**Arguments** (cdecl, five slots): S = A+222h (shield words: front S+0,
rear S+2), A = vehicle, M = that composed 32-byte matrix (coefficient words
0..17, mode byte 18, byte 19, translation dwords 20/24/28), R = range dword
(packet +4), D = address of the damage word (418ba8's local copy of packet
+6). Entry EAX/EDX/ECX are overwritten before any call.

1. def = [A+1f2h] + 2. The short vector (0, 0, word def+16h) goes through
   4801d4(vector, M, out12): out = M·v + translation.
2. If out.y ≥ R **unsigned** (JAE 413d0a): EAX = 0, EDX = 4801d4's EDX.
3. d = 47de4c(out.x, out.z); w = word def+18h. If (u16)(d + w) ≥ (u16)(2w)
   (SETB/JE 413d3f, a 16-bit wrapped "d < w"): EAX = 0, EDX = d with its low
   word replaced by d + w.
4. M is copied into a local L (all 32 bytes, byte 19 included), 47df19(L)
   transposes it, the three translation dwords are negated (z, x, then y)
   and 480304(−t, L, L+14h) writes the rotated point over L's translation.
5. The shield word is S+0 if that rotated Y (signed dword) > 0, else S+2
   (JLE 413dee).
6. absorbed = the damage word if s16(shield) > s16(damage) (JG 413e03), else
   the shield word. If absorbed ≠ 0 (JE 413e0e): damage −= absorbed, **then**
   shield −= absorbed (word arithmetic; an aliasing D sees the first write).
7. margin = s16(word def+18h − d), with the definition word **reread**. If
   margin ≥ s32(out.y) (JGE 413e30): EAX = 1; else EAX = out.y − margin + 1,
   EDX = margin. On the EAX = 1 path EDX keeps step 6's value: low word =
   absorbed, high word from D (shield > damage) or from the ECX that 480304
   left (otherwise).

EAX is never 0 on steps 7's paths (out.y > margin), so the caller's zero
test separates "no hit" from a penetration depth. Writes: only *D and the
selected shield word. Reads: A+1f2h, the two definition words (one twice),
M, the shield and damage words. No local is read before it is written
(4801d4 writes all three out dwords in each of its three mode paths).

**Callees.** 4801d4 (`matrix_point`), 47de4c (`dist_octagonal`), 47df19
(`matrix_transpose`) and 480304 (`rotate_point`) all go through the guest
bridge with the original entry registers (EAX/EDX/ECX at each call are
reproduced), so the hook refuses compare mode. Callee counters:
`call_4801d4`, `call_47de4c`, `call_47df19`, `call_480304`.

**Registers at the caller.** EDX is dead at 418cc3 (both successors write
it first). ECX is not reproduced: on the two zero returns it is 4801d4's or
47de4c's ECX, and V20 returns it unchanged (418e49..418e5c). A forward walk
from all 26 slot-20 return sites in DBSIM (the sweep's 4266cc and the
ground collision's 419270/4192ca among them) finds no read of the old ECX
(4266b9 follows `setne cl`; 41f02e stores a CX it has just loaded); the
callers of 413cc4's EDX/ECX therefore observe only EAX (heuristic liveness walk).

**Validation (in vitro).** `node_cases.h`: the standard regime with all
four callees as logging stubs, including damage pointers aliasing either
shield word or straddling both, plus 3,072 real-callee cases comparing EAX
and memory (EDX there may hold 480304's ECX or a 4801d4 frame address, both
dead). Six branch sites, all both ways.

## Weapon group and sphere helpers

Five hooks. Core `core/src/weapon_helpers.c`, adapter group
`weapon_helpers` (`tools/reference-game/proxy/weapon_helpers_hooks.h`).

| Hook | VA | Role |
|---|---|---|
| `group_fire` | 4107e4 | one pass over a weapon group: maintenance V34 for every weapon in priority order, V28 for the ready ones |
| `weapon_ready` | 40ede8 | ready test: cooldown word zero and trigger byte set |
| `part_condition` | 40db2c | condition ratio ×256 of a vehicle's part table |
| `sphere_hits` | 40c524 | collision spheres of one region against the current probe |
| `sphere_test` | 40c428 | one sphere against the probe; optionally shortens the probe |

**40bb4c** is not hooked. Its only caller is
40bc14 (the draw slot at 4987c4), which transforms the two end points of a
tracer into camera space (48c57c with the camera matrix 6c5fb4), lets
40bb4c clip them against the near plane Y = [6c602c], projects them
(48c5fc divides by depth or by [6c6030]) and emits a quad through 468414.
40bb4c (static contract: if both Y < [6c602c] return AL = 0; otherwise
move the end point below the plane onto it along the segment with the Q16
helpers 47e068/47e08d, AL = 1; ECX is an uninitialized local on three of
four paths) therefore only shapes rendered geometry and stays original.

### group_fire 4107e4

cdecl `(G, V, I, O)`: G = weapon group (vtables 499238/499338; reached as
their slot 0 bodies 41194a with I = s8 [G+1Dh] and 411a1d with I = −1),
V = value word, I = preferred weapon index (signed word), O = owning
vehicle. No direct writes outside its own frame; every effect comes from
the callees. Reads G+4 (weapon list) and the signed count word G+8 (both
reread at every use), O+A3h and the global words 4a9e52 and 4a9e58.

1. F = 1 if byte O+A3h ≠ 0, word [4a9e52] = 0 and word [4a9e58] = 1,
   else 0 (JE 410811, JE 410824, JE 41082e, JNE 41083c; registers: EDX's
   low byte = O+A3h, then EDX = 0/1 after the global tests, ECX = F).
2. memset(frame−28h, 0, 14h) clears ten "visited" words (CW3220 import
   49681a, reproduced; EAX = its destination). k = 0, EDX = s16(I),
   ECX = P = frame−28h + 2·s16(I).
3. Loop while s16(k) < s16(word G+8) (JL 41094e; DX = k at each test):
   - If s16(I) > −1 (JLE 410878; EAX = P) and visited[I] = 0 (JNE 410881):
     W = list[I], sel = I (ECX = s16(I), EAX = [G+4]).
   - Otherwise search j = 0, 1, … while s16(j) < s16(word G+8) (JL 4108ee;
     EDX = address of visited[j]): skip set words (JNE 4108a8) and null
     entries (JE 4108b4); take the first candidate (JE 4108b8); a later
     candidate C replaces W only if s16(C.V48(C)) > s16(W.V48(W, first))
     (JLE 4108d6), the second call carrying the first result in a second
     slot. Entry registers: V48(C) EAX = C, EDX = C's vtable, ECX = [G+4];
     V48(W) EAX = the first result, EDX = its EDX, ECX = W's vtable. After
     the pair ECX = W, EDX = the first result.
   - W = 0 ends the pass (JE 4108f2).
   - visited[sel] = 1; W.V34(W, slot V, slot any) with EAX = W's vtable
     and the low words of ECX/EDX replaced by the current V and the "any"
     flag; the result's AX becomes the new V. Then 40ede8(W); a nonzero AX
     (JE 410920) calls W.V28(W, O, slot F) with ECX = O and EDX's low word
     = F.
   - EAX = W+43h; a nonzero byte there sets any = 1 (JE 41093a); k += 1.
4. If F ≠ 0 (JE 410959), V is restored to the incoming word. EAX = the
   last EAX with AX = V; EDX = the last EDX.

The ten-word array bounds the domain: count ≤ 10 and I ≤ 9 (larger values
make the original overwrite its own cursor locals). Counts come from the
vehicle's hardpoint table (40fff8 stores that word at G+8); this holds over
all shipped files: the 21 `gl\*.GL` lists have at most 10 entries (OGRE),
so the index is at most 9 (`tools/reference-game/scripts/domain_gun_lists.py`).
The frame address leaks into
registers (EAX after the memset, EDX during the search, ECX = P), so the
core takes its frame from the caller; the residues are equal up to the
frame address.

**Callers' registers.** The only live call of the two slot 0 bodies is
41aa97 in 41aa5c (static search for slot 0 calls on [vehicle+202h]; the
live entry counts of 4107e4 and of `energy_accumulate` 413b38 agree,
[live_coverage.md]). There EAX goes to ESI and, as the word argument, to 413b38,
which reads only its low word; ESI is rewritten (41ac63, 41b016) before
any read and restored at 41b0b3; EDX and ECX are written by 413b38 before
they are read (413b68/413b6c, 413b7d). Only AX is live; 41194a/411a1d
return the pair unchanged.

[live_coverage.md]: ../reference-game/live_coverage.md

### weapon_ready 40ede8

cdecl `(W)`: if word W+31h ≠ 0 (JNE 40edf3): EAX = 0, EDX unchanged;
otherwise EDX = W+43h and EAX = (byte W+4Dh ≠ 0) (JNE 40edfc). ECX
unchanged. W+31h is the cooldown word that 40e788 sets; W+4Dh is a
"fire requested" byte (hypothesis). Only caller 4107e4 (410917), which
pops ECX and uses AX and EDX's high word.

### part_condition 40db2c

cdecl `(X)`, X = vehicle+206h (415504, vehicle V40 at 49a2c2) or object
+200h (421e84, V40 of class 49a5e0). Two signed-word loops, both counts
reread at each test:

- k < s16(word X+18h) (JL 40db7d): T = [[X+Ch]] + 18·k (reread), c = s16
  of the k-th word of [X+4]; a = s16(word T); b += a; s += (c < 0 ? a : c)
  (JGE 40db61).
- k < s16(word X+1Ah) (JL 40dbae): over the words of [X] and of
  [[X+Ch]+4] in parallel, the same rule (JGE 40db8f).

Then EAX = (s·256 mod 2^32) / b and EDX = the remainder (CDQ, IDIV),
ECX = b (its second POP takes the local b). Domain: b ≠ 0 and not
(s·256 = −2^31, b = −1). No writes. The V40 wrappers end with `pop ecx`,
so ECX is dead; they return the pair unchanged to the vtable callers.
Reading: current/maximum armour over all parts, a part whose current
value is negative counting as intact (hypothesis).

### sphere_hits 40c524

cdecl `(P, Q)`, P = a region (word count at P+0, at P+2 a dword pointer to
8-byte spheres of words x, y, z, r), Q = an offset vector of three words
or 0. Only caller 40c8c8 (`(P, 0)` at 40c8eb). For i while
s16(i) < s16(word P) (JL 40c5b4, ECX = P at each test):

1. Point = the sphere words x, y, z (DX, CX, AX take them in this order);
   if Q ≠ 0 (JE 40c55b) add Q's z, x, y words (EAX = Q with AX = Q's y).
2. 4801d4(point, [4a980c], out) with EAX = [4a980c], EDX = out,
   ECX = point; out is 12 bytes of the frame, the point 12 bytes above it.
3. 40c428(out, slot, 1) with EDX = out and the slot = 4801d4's EAX with
   AX = the sphere word r; a nonzero AX (JE 40c5a2) sets hit.

EAX = hit ? [4a9810] + 1 : 0 (JE 40c5bb); EDX = the last callee's EDX or
the entry EDX. ECX = P is not reproduced (dead: 40c8fc pops it at
40c9a3). EDX can be the frame address `out` (40c428's early exit keeps
its entry EDX); its high word travels up through 40c8fc and 40c9d4 (which
replaces DX at 40ca35) to the building V20's exit. The replacement's
frame lies elsewhere on the same stack, so that high word differs only
when the two frames straddle a 64 KiB boundary.

### sphere_test 40c428

cdecl `(R, slot, mode)`, R = a transformed centre (dwords x, y, z):

1. If (u32)[R+4] ≥ [4a9810] (JAE 40c43a): EAX = 0.
2. d = 47de4c([R], [R+8]) (`dist_octagonal`, entry EAX = [R+4]); then
   DX = word [4a9814] + the slot word (EDX's high word from 47de4c).
3. If s32(d) ≥ 7FFFh (JGE 40c45b): EAX = 0. Else
   ECX = ((u16)(d + DX) < (u16)(2·EDX)) (SETB, JE 40c472); 0 returns
   EAX = 0.
4. If the mode word ≠ 0 (JE 40c479): v = s16(DX) − d;
   [4a9810] = v < [R+4] (signed, JGE 40c485) ? [R+4] − v : 0, also left in
   EDX; EAX = v. Finally EAX's low word becomes 1.

Writes only [4a9810] (mode ≠ 0). Callers: 40c524 (mode 1) and 40c4a4,
which nothing calls or points to. ECX is dead at 40c524 (reloaded at
40c5ae).

### Hooks, registers and validation

All five prologues are whole instructions: 4107e4 `55 8b ec 83 c4 d8`,
40ede8 `55 8b ec 8b 45 08`, 40db2c `55 8b ec 83 c4 f8`, 40c524
`55 8b ec 83 c4 ec` (1/2/3), 40c428 `55 8b ec 53 56` (1/2/1/1). No x87,
no entry shim (no local is read before it is written; the frame residues
above are addresses, not contents). `weapon_ready` and `part_condition`
call no game code and write nothing, so they allow compare mode; the
other three call game code through the weapons guest bridge with the
original entry registers and refuse it. Virtual targets are read from the
receiver at each call.

## Effect creation

Four hooks: `explosion_create`
(`0x407f1c`), its base constructor `object_init` (`0x402188`), the light
constructor `light_create` (`0x407604`) and the pool allocation `pool_alloc`
(`0x471b30`); core in `core/src/effects.c`
(`es2/core/effect_create.h`), registry group `effect`, adapter
`proxy/effect_create_hooks.h`. Registers below are full 32-bit values;
"the counter" is the dword the runtime pointer `[0x6cbee4]` points to (the
Borland runtime's construction count, also touched by every other
constructor). Pool layout and iteration order: [main_loop.md, "The frame
itself"](main_loop.md#the-frame-itself).

### `pool_alloc` `0x471b30` (cdecl, one argument: pool header H)

Prologue `55 8b ec 51 53` (relocated 5 bytes: 1,2,1,1); the `push ecx`
slot is the local that holds the result, written before its only read. No
calls, no globals. Order of accesses, exactly as the original:

1. Word `H+0` (free count) = 0 (`0x471b3d`): return EAX = 0, EDX = entry
   EDX (untouched); ECX = entry ECX (the slot is popped unchanged).
2. Result = dword `H+a` + 8 (first read of the free head). Word `H+0` −= 1,
   word `H+2` (used count) += 1, both as 16-bit read-modify-writes.
3. N = dword `H+a` (re-read; the link header of the object). Next free
   F = `[N]`; `H+a` ← F; F ≠ 0 (`0x471b5f`): `[F+4]` ← 0.
4. Used head U = `[H+e]`; U ≠ 0 (`0x471b6b`): `[N]` ← U, then `[[H+e]+4]`
   ← N (`H+e` re-read); U = 0: `[N]` ← 0.
5. `[H+e]` ← N; `[N+4]` ← 0. Return EAX = result, EDX = N, ECX = result.

So a new object becomes the **head** of the used list; the iterator
`0x471c70` walks from the tail via the previous links, so it is updated
after every older object of its pool. A free count that disagrees
with the free list (nonzero count, null head) faults in the original;
pools keep both in step (`0x471bc8` is the only other writer besides the
constructor `0x471a50`), so that state is outside the contract.

Callers: 32 direct call sites in 23 functions (`callers-and-refs.txt`):
the constructors and spawning code of every pooled object class, and the
two wrappers `0x471b8c`/`0x471b9c`; no absolute reference (no vtable or
callback). **ECX liveness:** the forward walk (`ecx-liveness.py`) finds
ECX written before any read after all 32 sites, so the pair result
suffices although the original returns ECX = result on the allocation
path.

### `object_init` `0x402188` (cdecl O, S)

Prologue `55 8b ec 83 c4 c8` (6 bytes: 1,2,3). Runtime exception frame:
`0x49630c` with EAX = descriptor `0x497115` (frame offset −24h), so the
record lies at EBP−24h, its context word at EBP−14h; the slot EBP−4 holds
the allocation. Locals EBP−38h..−28h are a zero vector and zero angles that
are written before they are copied. No local is read before it is written.

1. `0x49630c` (entry EAX = `0x497115`, EDX/ECX = entry values).
2. `[O]` ← `0x4a14a4`; counter += 1; then, in this order, dword `O+2e`,
   dword `O+26`, dword `O+2a`, dword `O+c` ← 0, word `O+10` ← 0, word
   `O+32` ← 0; `[O]` ← `0x497289`; counter += 1; `[O]` ← `0x4a0b80`.
   Registers now EAX = `[0x6cbee4]`, EDX = 0, ECX = 0.
3. S = 0 (`0x40220c`): EDX = 0. Otherwise `0x47749c(12)` (heap, zero-filled;
   entry registers as just listed), ECX = 12 (`pop ecx`); the result A is
   stored at EBP−4. A = 0 (`0x40221b`): EDX = 0. A ≠ 0: context word ← 14h,
   `0x490b64(A, S)` (entry EAX = A, EDX = S, ECX = A; the shape instance
   constructor), counter −= 2 (EAX = `[0x6cbee4]`), context word ← 8,
   EDX = A.
4. Dword `O+34` ← EDX. `0x49681a(O+38, 0, 8)` (the CW3220 `memset` import
   thunk; entry EAX = O+38, EDX as in 3, ECX = 0, 12 or `0x490b64`'s ECX).
5. Counter += 1; FS:0 ← the record's saved head; return EAX = O, EDX =
   `memset`'s EDX. ECX = `[0x6cbee4]` in the original.

Callees: `0x49630c` (runtime frame setup, not hooked), `0x47749c` (heap
allocation from `0x6bb2f8`, a runtime helper: it keeps the original's heap
state, so it stays original), `0x490b64` (shape instance constructor, the hook `shape_instance`;
it allocates through `0x47a288`) and `memset`, all through the
bridge. Callers: 13 call sites; every caller
overwrites `[O]` with its own vtable right after the call, and ECX is
written before any read after all 13 (at `0x409b64` the next call
`rng_next` writes CX and then all of ECX by `movsx`; at `0x46b4a6`
`memset` and `0x478628` are reached, the latter writes ECX first).

### `light_create` `0x407604` (cdecl L, P, v)

Prologue `55 8b ec 53 56` (5 bytes: 1,2,1,1). No frame record, no locals.
`[L]` ← `0x4a14a4`; counter += 1; `[L]` ← `0x497b8a`;
`0x406f38([0x4a9608], P)` (entry EAX = `[0x6cbee4]`, EDX = P, ECX = the
table; it claims an entry of the light table and returns its index I in
EAX); word `L+c` ← I; `0x407048([0x4a9608], I, w)` with w = (I &
FFFF0000h) | low word of v (entry EAX = w, EDX = the table, re-read, ECX =
`0x406f38`'s ECX); counter += 1; return EAX = L, EDX = `0x407048`'s EDX
(ECX = `[0x6cbee4]`). `0x406f38` and `0x407048` are the light module of
light table (other callers, no hook) and stay original through the bridge. Only
caller: `0x40807e` in `0x407f1c`, where ECX is dead.

### `explosion_create` `0x407f1c` (cdecl O, t, P, d, s)

Prologue `55 8b ec 83 c4 d8` (6 bytes: 1,2,3). Exception record at EBP−28h
(descriptor `0x497c68`, offset −28h), context word at EBP−18h; slots EBP−4
(linked object) and EBP−8 (light), each written before read. O is an
object of the `EXPLOSION` pool `[0x4a961e]` that the caller has just
allocated; t a type (low word used), P a 12-byte position, d a dword, s a
byte flag. T(x) = `[0x4a9628]` + 40 × x is the type entry; the pure leaf
`0x407b20(O)` computes T(s8(byte `O+41`)) (EAX = EDX = T(…), ECX
untouched, the caller's `pop ecx` leaves ECX = O); it is itself the hook
`explosion_entry` (`core/src/sim_leaves.c`), and `explosion_create`
computes it in core at its three call sites.

1. `0x49630c` (EAX = `0x497c68`, EDX/ECX = entry values).
2. `0x402188(O, [[0x4a9630] + 4 × s16(word T(s16(t)))])` with entry
   EAX = that shape pointer, EDX = the s16 word, ECX = `[0x4a9630]`. (The
   shape lookup uses the 16-bit type; everything later uses the byte.)
3. `[O]` ← `0x497d4c`; dword `O+57` ← d; byte `O+41` ← low byte of t.
4. T = `0x407b20(O)`. Word `O+4` ← 8 if word `T+26` ≠ 0 (`0x407f76`), else
   2. Word `O+4b` ← word `T+2` (EAX keeps T's high word).
5. W = s16 word `[[0x4a963c] + 4 × s16(word T)]`; W > −1 (`0x407f9e`): word
   `[[O+34]+8] + 2 × (W & FFFFh)` ← 0 (resets that animation's frame).
6. `O+2e`, `O+26`, `O+2a` ← P+8, P+0, P+4 (in that order). EAX = O+26,
   ECX = `[P]`, EDX = `[P+4]`.
7. Word `T+4` ≠ 0 (`0x407fca`): K = `0x471b30([0x4a968d])` (entry EAX = the
   pool, EDX/ECX from 6); ECX = the pool (`pop ecx`); K → EBP−4. K ≠ 0
   (`0x407fdf`): context word ← 14h; `0x402188(K, [[0x4a9698]+4])` (entry
   EAX = `[0x4a9698]`, EDX = the shape, ECX = K); `[K]` ← `0x49a262`; word
   `K+4` ← 9; counter += 1, then counter −= 4; context word ← 8. Dword
   `O+4f` ← K; K+2e, K+26, K+2a ← P+8, P+0, P+4 (P re-read). With K = 0
   the original stores to addresses 2Eh..31h and faults, so a successful
   allocation from the `FlatObj` pool (a hypothesis) is part of
   the contract: the taken edge of `0x407fdf` is outside it. Word `T+4` = 0:
   dword `O+4f` ← 0 (EAX = 0).
8. Word `T+6` ≠ 0 (`0x408054`): L = `0x471b30([0x4a95fe])` (the
   `LightSource` pool, three entries of 14 bytes; entry EAX = the pool, EDX
   and ECX = `[P+4]` and `[P]` on every path that does not fault); ECX =
   the pool; L → EBP−8. L ≠ 0 (`0x408069`): context word ← 2Ch;
   `0x407604(L, P, w)` with w = (L & FFFF0000h) | word `T+8` (entry
   EAX = w, EDX = P, ECX = L); counter −= 2; context word ← 20h. Dword
   `O+53` ← L (0 when the light pool is empty). Word `T+6` = 0: dword
   `O+53` ← 0.
9. Byte s ≠ 0 (`0x4080aa`): T′ = `0x407b20(O)` (byte `O+41` re-read);
   s16 word `T′+24` > −1 (`0x4080b8`): T″ = `0x407b20(O)` again,
   `rng_bounded(0x4d260b, 50)` (entry EAX = EDX = T″, ECX = O; its result
   is discarded: the draw only advances stream B), then
   `0x4628e0(s16(word T″+24) + 10, P)` (entry EAX = P, EDX/ECX =
   `rng_bounded`'s).
10. EDX = `[0x6cbee4]`; counter += 1; FS:0 ← the saved head; return
    EAX = O, EDX = `[0x6cbee4]`. The original's ECX is the saved head.

Callees: `0x49630c` (runtime), `object_init`, `pool_alloc`, `light_create`,
`rng_bounded` (hooks, entered in their own modes) and the sound wrapper
`0x4628e0` (hook `sfx_play_at`: SOS audio behind the flag `0x49f8ec`), all through
the bridge with the entry registers listed. Writes: O's fields above, K's
vtable/word/position, the counter, the animation frame word, and what the
callees write. Callers (9): `0x403765`, `0x403c4a`, `0x405231` (buildings),
`0x408dc0` (debris update `0x408bd8`), `0x40d72d`, `0x418b3e` (`0x4188c8`),
`0x418d5d` (vehicle V20 `0x418ba8`), `0x421e25` (flyer) and `0x4268f3`
(sweep); each allocates O from `[0x4a961e]` right before and passes
s = 0 or 1.

**ECX at return.** Replaced hooks return the entry ECX (the framework
restores the saved registers except EAX/EDX), the original returns the
saved exception head. The liveness walk shows it dead everywhere: seven
callers write ECX first (`0x405231` reaches `rng_next`, which writes CX and
then all of ECX; `0x421e25` loads CX and stores only that word before a
`pop ecx`); `0x408dc0` returns to the logic frame's `pop ecx`; `0x4268f3`
returns from the sweep, whose nine callers write ECX first; `0x418d5d` and
`0x418b3e` → `0x418e41` return from the vehicle V20, whose 26 slot-20
return sites were walked for `shield_geometry`
(nothing reads ECX for the vehicle class); `0x4188c8`'s other callers write
ECX first except `0x41a243`, whose function `0x4198f4` is a movement
callback (pointer `0x49994c`) dispatched by `0x415afc`, which pops ECX
after the call. So no entry or exit shim is needed.

**Branch sites.** `explosion_create` 8 (`0x407f76`, `0x407f9e`,
`0x407fca`, `0x407fdf`, `0x408054`, `0x408069`, `0x4080aa`, `0x4080b8`),
the taken edge of `0x407fdf` outside the contract (above); `object_init` 2
(`0x40220c`, `0x40221b`); `pool_alloc` 3 (`0x471b3d`, `0x471b5f`,
`0x471b6b`); `light_create` none.

## Building logic: target selection and destruction actions

Four hooks. Core `core/src/building.c`, adapter group `building`
(`tools/reference-game/proxy/building_hooks.h`).

| Hook | VA | Role |
|---|---|---|
| `target_select` | 411fa0 | best hostile target of an actor over the whole actor list (turret 404100, the bank pass of 4045c8 and nine AI callers) |
| `target_valid` | 411e80 | may actor A attack candidate C? (the only other caller is 41e0e0) |
| `group_death` | 423f30 | when the last living member of a group is destroyed, apply the group's ten variable actions |
| `death_actions` | 411bc8 | a destroyed actor's own ten variable actions, after `group_death` for its group |

`0x4033e4` and `0x401fe4` are **not** hooked, because they only draw. 401fe4(O) rebuilds the object matrix with
`euler_matrix` 47ebb8 when the cache word O+32 is clear (and sets it), then
calls 48c444, the renderer's object-to-camera setup (it writes the current
transform pointer 6c5ff8 and combines the camera matrix at 6c5fb4 and the
camera position 6c601c; 23 callers including the rendering root 45fca8).
4033e4(O) calls 401fe4, chooses a level of detail from the 3-D distance
between the camera position 6c601c and the object (47de72, scaled by
[6c6028] and the factor 497368), writes the chosen model into the shape
instance [O+34]+4, calls 401fe4 again and then the shape's draw slot
[[O+34]]+1C. Its callers are draw slots: 4174c8 (which also draws the parts
through 4030d0), 4215cc and 4034f4; the last two bracket it with 4769b8,
which sets the renderer's colour override 4a1020. Nothing in the simulation
reads the LOD choice, so both stay original. `death_actions` 411bc8 is
the only caller of 423f30 (their live entry counts agree, so every
destroyed actor of the regression suites had a group). The code from
4123ac (the actor-list pass of the logic frame) is a separate function;
411fa0 returns at 4123a8.

Common layout (see also [mission_runtime.md](mission_runtime.md)): actor
+0 vtable, +10 heading word, +26 position (three dwords), +45 group, +4D
pointer to an optional record (its byte +9 is tested), state bytes +99,
+0A4, +0A5 (the "alive" test) and +9A, +0A3, +0B4, +0B7, +1A2 word (number
of actors currently targeting this one, kept by the turret), +1A4 current
target, +1A8 type word, +1BA ten variable indices and +1CE ten action
words. Group: +0C member array, +10 member count word, +12 category byte,
+1C ten variable indices and +30 ten action words, +44 ten order pointers,
+6C current order index. Mission variables: words at 4a9e70 + 2·index.
Actor list `[4a9af8]` with count word `[4a9afe]`; cockpit object
`[49b088]` (its +210 dword is compared with C); mission mode word 4a9e54.
Factor tables (Q10 words, read from the image, never embedded): 49933c[k]
= 1500/2000/2500, 499342[k] = 600/400/200, 499348[k] = 700/850/1000 for
k = 0..2, 49934e[t] = 1500/700/500/500 for t = 0..3.

### 411e80 `target_valid(A, C, flags)`

cdecl, three slots; only the low byte of `flags` is read. Returns EDX:EAX;
every refusal clears EAX with XOR.

1. EDX = s8 [[C+45]+12]; equal low words with s8 [[A+45]+12] (JE 411e9d)
   refuse, EDX stays that value.
2. Byte [C+99] (JNE 411eaf) or [C+0B4] (411ebb) nonzero refuses.
3. `411c58(A, C)` (`target_known`, entry EAX = C+92, EDX = step 1's value,
   ECX = [A+45]); AX = 0 refuses (JE 411ece). Every later read happens after
   this call.
4. Byte [C+0B7] nonzero refuses (411eda). With `flags & 10h` (JE 411ee4),
   equal type words [C+1A8] and [A+1A8] refuse (411ef4).
5. Type word [C+1A8] = 2 (JNE 411f02): if [C+0A5], [C+0A4] or [C+99] is set
   (411f0e, 411f1a, 411f26, 411f34, 411f42) refuse with EDX = 1.
6. With the leader L = [[[A+45]+0C]] (first member of A's group): if
   [L+0A3] (JE 411f5a) and word [4a9e54] = 3 (JNE 411f64), call `4238a0(G)`
   (G = [A+45]; entry EAX = L, EDX = L+92, ECX from 411c58); a result equal
   to C refuses (411f6f) with 4238a0's EDX. 4238a0 returns the current
   order's target; it is the hook `order_actor` (group `groups`), entered
   in its own mode.
7. If [A+9A] (JE 411f7f) and [[49b088]+210] = C (411f8d): refuse with EDX
   = [49b088].
8. Accept: EAX = high word of A+92 over AX = 1. EDX = [49b088] when [A+9A]
   was set, else 4238a0's EDX when step 6 called it, else L+92.

### 411fa0 `target_select(A, flags, arc)`

cdecl, three slots. EAX, EDX and ECX at entry are not read and no local is
read before it is written (no entry shim). State: cursor = [4a9af8] (read
once), i = 0, best = 0, best_score = 0 (dword), best_class = 1 (word),
catflag = ([[A+45]+12] = 0).

1. With G = [A+45] and order = [G + 4·[G+6C] + 44] (JE 411fef), an order
   type word (0Bh for no order) of 3 sets best_class = 0 (412014). EAX =
   that comparison (0/1), EDX = high word of [G+6C] over the type, ECX = G.
2. Loop (412379): AX = i (old value, high word kept), i += 1. While
   s16 i < s16 [4a9afe] (re-read each pass, JGE 412388): EDX = cursor,
   C = [cursor], cursor += 4; otherwise C = 0. C = 0 ends (JNE 412399) and
   returns EAX = best, EDX as it stands.
3. CX = low word of `flags` (high word of ECX kept); `411e80(A, C, ECX)`
   with the whole ECX pushed. AX = 0 skips C (412037).
4. `423918([A+45], C)` (entry EAX = C, EDX = [A+45]). AX nonzero (412053):
   G = [A+45] again; matched = (current order type, 0Bh if none (JE
   412060), is 0), with ECX = high word of [G+6C] over that type.
   Otherwise matched = 0. EAX = matched.
5. preferred = matched or `flags & 1` clear (41208c, 412092); dead = any of
   [C+0A5], [C+0A4], [C+99] (4120a9, 4120bb, 4120cd, 4120db): EDX = dead,
   ECX = !dead; class = 2·preferred + !dead (low word of EDI).
6. Unless matched and catflag = 0 (4120f9, 412100), a set [C+0A4] skips C
   (41210c, EDX = C+92). class < best_class (signed words) skips (41211a).
7. limit = matched ? 1000000 : 100000 (412128); d = `49288c(A+26, C+26)`
   (`dist_gate`, entry EAX = C+26, EDX = A+26, ECX = class); limit < d
   (signed) skips (41214c).
8. Angle: `flags & 20h` gives 0 and skips the arc test (412156). Otherwise
   h = `492934(C+26, A+26)` (`heading_point`, entry EAX = EDX = C+26, ECX =
   A+26); DI = h − [A+10] (word); angle = |DI| as a word with 8000h →
   7FFFh (41217d, 412188; EAX keeps h's high word). A nonzero `arc` word
   (412198) skips C when s16 angle > s16 arc (4121b8). The second absolute
   value repeats the first on a value in 0..7FFFh, so 41219e and 4121a9
   always jump (proved one-way); EDX = EAX afterwards.
9. f = 1000h − (s16 angle >> 3) (word in DI); score = `47e0b0(46h, f)`
   (`multiply_q10`, entry EAX = EDX = s16 f). If d < 60000 (signed,
   4121e8): q = max(d >> 12, 1) (arithmetic shift, 4121fa), and the signed
   quotient word f / q replaces score when larger (signed words, 412213);
   EDI = q, EDX = remainder.
10. k = AX of the virtual `[[A]+4C](A, C)` (entry EAX = [A], ECX = score;
    observed as `slot_4c`).
11. Up to four Q10 factors, each `score = 47e0b0(table, s16 score)`:
    49933c[k] when `flags & 2` is clear and [C+1A4] = A (41222d, 412235;
    entry EAX = EDX = score, ECX = table value); 499342[k] when [C+4D] is
    nonzero, byte [[C+4D]+9] is zero and [C+0A3] is zero (412262, 412274,
    412286; same registers); 499348[k] when [C+1A4] is neither 0 nor A
    (4122b7, 4122bb; same registers); always 49934e[t] with t = s16
    [C+1A8], except t = 0 (and EDX = 0) when [C+1A4] = A and t = 1 (4122f4,
    4122fa; entry EAX = table value, EDX = ECX = score). Every pointer is
    re-read after the previous call; k and t are sign-extended words.
12. n = word [C+1A2] − (C = [A+1A4]) in CX over the high word the last
    call left. Without `flags & 4` and with n ≠ 0 (412341, 412346), score =
    s16 score / (s16 n + 1) (IDIV). n = −1 would divide by zero (a fault in
    the original); it needs [C+1A2] = 0 while A targets C, which the
    bookkeeping excludes (whoever stores C as its target increments
    [C+1A2], turret 404176). It is outside the contract; the core keeps the
    score there instead of dividing.
13. Take C when s32(s16 score) > best_score (41235c) or, failing that,
    s16 class > s16 best_class (412366; EAX's low word becomes class):
    best_score = s16 score (also EDX), best_class = class (CX), best = C.

### 423f30 `group_death(G, C)`

cdecl, two slots, no calls. For i = 0 while i < s16 [G+10] (re-read, JL
423f7a): member M = [[G+0C] + 4i]; M ≠ C (JE 423f45) and alive (neither
[M+0A4] nor [M+99]; 423f51, 423f5d, 423f6b) returns EAX = 0, EDX = i. When
no other member is alive, for i = 0..9 (423fdd): index = s16 [G+1C+2i]; a
negative index skips (JL 423f88); action = s16 [G+30+2i]: 1 writes
variable[index] = 0 (423f8f), 2 increments it (423f92), 13..16 write
action − 12 (the `ADD −0Bh; SUB 4; JB` test at 423f9a), any other action
does nothing. Returns EAX = G+30, EDX = 10 (ECX is not part of the
contract).

### 411bc8 `death_actions(C)`

cdecl, one slot. If [C+45] is nonzero (JE 411bd6), `423f30([C+45], C)` with
entry EAX = [C+45] and the entry EDX/ECX. Then the same ten-entry action
loop over C's own indices +1BA and actions +1CE (411bf4, 411bfa, 411bfd,
411c05, 411c4f). Returns EAX = C+1CE, EDX = C+1E2. The pushed entry ECX is
a local whose high word is overwritten before it is read, so no residue
reaches anything. Its callers set [C+99] = 1 first (404ecf in the building
damage path), which is why the action tables read as "on destruction".

### Hooks, registers and validation

Prologues: 411fa0 `55 8b ec 83 c4 90` (1/2/3), 411e80 and 423f30
`55 8b ec 53 56 57`, 411bc8 `55 8b ec 51 53 56` (relocate 5: 1/2/1/1). No
x87 instruction and no entry shim. `group_death` calls no game code but
writes the mission variables; the other three call game code through the
weapons guest bridge with the original's entry registers. All four refuse
compare mode. 4238a0 is entered as the hook
`order_actor`; the actors' virtual +4C is a virtual call.

## Sweep receiver object query 427da8, grid sweep 4273c8, set_length 4927f0

Hooks `object_query` (427da8),
`grid_sweep` (4273c8) and `set_length` (4927f0), adapter group
`object_query` (`tools/reference-game/proxy/object_query_hooks.h`), core
appended to `core/src/collision.c`. 427da8 is "candidate B" of the DBSIM
map: the query below `object_line`'s 404bc0 and below the building receiver
V20 405038. With its live callees 4273c8 and 4927f0 the whole subtree is own
code; the matrix and distance leaves are hooks and are entered
through the guest bridge.

### 427da8 `object_query(out, segment, list, count)`

cdecl, four slots; `count` is read as a signed word at each loop test
(427dcf, 428031), the others as dwords. `segment` P is the swept segment
record of the [swept query](#swept-query-426528): P+0 forward pose
M (32 bytes, translation = segment start at M+14h), P+4 signed length L, P+8
radius word, P+0Ah the inverse pose. `list` points to `count` object
pointers. Callers: 404bc0 (list = its frame array of the pool objects that
passed the team filter, out = 404ca0's result) and building V20 405038
(list = the one-element array {building}, count 1, out = its frame).

Frame 9Ch; locals by EBP offset: -2 result word, -4 index word, -8 shape,
-0Ch reach, -10h/-14h object+0Ch, -18h cursor, -24h object position in
segment space, -30h object position, -50h the object's inverse pose (32
bytes), -5Ch scratch translation, -6Ch the 4273c8 out record, -84h/-78h the
segment start/end in object space, -90h the segment end in world space, -9Ch
the local end point (0, L, 0). Every local is written before it is read on
every path; the hit record at -68h is read only after 4273c8 returned
AX != 0, which writes it. No entry shim.

Per entry (index word i from 0 while s16 i < s16 count, JGE 427dd3 / JL
428035; cursor += 4):

1. B = [cursor]. H = [B+34h]; shape S = [H+4] if H != 0, else 0 (JE 427de2).
   S = 0 makes 427df7 read the word at address 8, so the null edge faults
   in the original and is outside the contract (B = 0 faults one
   instruction earlier).
2. r = s16 [S+8] + s16 [P+8]; reach = [P+4] + r (P+4 is re-read for every
   object, so a hit shortens the reach of the later ones).
3. Copy B's position B+26h..B+31h to -30h. d = 49288c(M+14h, -30h).
   If s32 d < s32 [4aaad0] then [4aaad0] = d (JGE 427e32). d > reach
   (signed, JG 427e3c) skips the object; otherwise [4aaad0] = 0 first. The
   global is thus the smallest distance of the objects out of reach since
   the last one in reach; the sweep 426528 reads it back after its receiver
   loop through 427da0 (`mov eax, [4aaad0]`) into [4aaa68].
4. 48043c(-30h, P+0Ah, -24h): the position in segment space (x, y). The
   object is skipped unless -r < x < r and -r < y < reach (signed: JLE
   427e62, JLE 427e6e, JGE 427e7b, JGE 427e84; x = r or x = -r skip).
5. Inline pose matrix: if word [B+32h] = 0 (JNE 427e98), 47ebb8(B+0Ch,
   B+12h) and word [B+32h] = 1. Copy the 20 matrix bytes B+12h..B+25h and
   the translation to -50h, transpose with 47df19(-50h), negate the
   translation in the order Z, X, Y, copy it to -5Ch and rotate it with
   480304(-5Ch, -50h, -3Ch): -50h is now the world-to-object pose.
6. Write (0, L, 0) at -9Ch (L = [P+4]). 48043c(M+14h, -50h, -84h) gives the
   segment start in object space, 48043c(-9Ch, M, -90h) the segment end in
   world space and 48043c(-90h, -50h, -78h) the end in object space.
7. 4273c8(S, -6Ch, -84h, radius slot) with the radius word [P+8] in AX (the
   high word is the last 48043c's EAX). AX = 0 (JE 427fe5) skips the object.
8. Hit: d2 = 49288c(-84h, -68h), the distance from the start to the hit
   point; [P+4] = d2 (the segment is shortened); result = 1; [out+4] =
   B+0Ch (B = 0 would store 0, JE 428005, unreachable after step 1); [out]
   = [P+4] re-read after that store; if s32 [P+4] (re-read again) < 500 (JL
   428023) the loop ends.

Return: EAX = (high word of the last EAX) | result word; EDX = the list
pointer when the loop never ran, `out` after the < 500 exit, else the last
EDX with its low word replaced by the index. The last EAX/EDX can be a
callee's residue or a frame address (in the high halves): the replacement
returns the same values except that frame addresses are its own. Both
callers test AX only. 404bc0 hands EAX/EDX/ECX on to 404ca0, whose callers
overwrite all three ([ai.md](ai.md)); 405038 overwrites EAX and ECX before
its exit, and its caller, the sweep loop at 4266c9, rewrites EDX (426836) or
leaves through 4269fa..426a0c, which reloads EDX. ECX is dead after both
callers and is not reproduced (at the first exit it is the entry ECX with
its low word 0).

Entry registers of the calls (the original's, reproduced): 49288c EAX = B's
Z, EDX = -30h, ECX = M+14h; 48043c (step 4) EAX = P+0Ah, EDX = -24h, ECX =
-30h; 47ebb8 EAX = ECX = B+0Ch, EDX = B+12h; 47df19 EAX = B's Z, EDX = B's
Y, ECX = -50h; 480304 EAX = -3Ch, EDX = -50h, ECX = -5Ch; the three 48043c of
step 6 (EAX, EDX, ECX) = (M+14h, -84h, -50h), (M, -90h, -9Ch), (-78h, -50h,
-90h); 4273c8 EAX = S, EDX = -84h, ECX = -6Ch; 49288c (step 8) EAX =
4273c8's EAX, EDX = -68h, ECX = -84h.

State: reads the list, B+0Ch..B+37h, [B+34h]+4, S+8, P+0..P+29h and
[4aaad0]; writes [4aaad0], word B+32h, P+4, out+0..out+7 and whatever the
callees write (47ebb8 rebuilds B+12h..B+24h). 13 conditional sites; 427de2
(null holder) and 428005 (null object) have one feasible edge.

### 4273c8 `grid_sweep(S, out, segment, radius)`: a segment against a height grid

cdecl, four slots; `radius` is read as a word. S is the height-grid shape:
word +2Ah columns, +2Ch rows, +2Eh/+30h column/row origin in cells, +32h
cell shift, dword +430h an upper height bound (by its use here), and the
cell table that the `cell` hook 427238 reads (+34h heights, +434h row
pointers). `segment` is two points (start at +0, end at +0Ch, object
space).

1. Word [S+2Ah] = 0 (JE 4273dc) returns EAX = 0, EDX = entry EDX.
2. If s32 start.z >= s32 [S+430h] (JL 4273ed not taken) and s32 [S+430h] <=
   s32 end.z (JLE 4273f2), the segment lies above the grid: EAX = 0, EDX =
   [S+430h].
3. Copy start to A (-30h) and end to E (-24h). With k = word [S+32h] & 31
   (CL): ox = (word [S+2Eh] << k) & FFFFh, oy = (word [S+30h] << k) & FFFFh
   (16-bit shifts); A.x, E.x += s16 ox; A.y, E.y += s16 oy. step = ((1 << k)
   & FFFFh) + radius word, as a word.
4. D (-3Ch) = E - A (written Z, X, Y). 4927f0(D, step slot) rescales D to
   the length `step` (the slot is EBX, whose high word is that of s16 oy).
5. len = 47de72(A.x - E.x, A.y - E.y, A.z - E.z) (the unscaled segment). n =
   word of (trunc(len / s16 step) + 1) (CDQ/IDIV; step 0 or INT_MIN / -1
   faults in the original and is outside the contract). The dead stores
   -4Ch = -48h = 0, -44h = s16 [S+2Ah] << k, -40h = s16 [S+2Ch] << k are
   frame-only and never read; they are not reproduced.
6. If s16 n <= 0 (JGE 4274fb): EAX = 0, EDX = the IDIV remainder.
7. For i = 0 .. n-1 (DI against the word n, JL 42755a): h = 427238(S, A,
   radius slot) (the slot's high word is the previous EAX). If h != 0 (JE
   42750e) and s32 h > s32 A.z (JLE 427513): out+4..out+0Fh = A, written Z,
   X, Y, then out+4 -= s16 ox and out+8 -= s16 oy; EAX = ((out+4) &
   FFFF0000h) | 1, EDX = s16 oy. out+0 is not written. Otherwise A += D (Z,
   X, Y).
8. After n samples: EAX = 0, EDX = D.y.

Entry registers of the calls: 4927f0 EAX = D, EDX = A.x, ECX = A.y; 47de72
EAX = A.z - E.z, EDX = A.y - E.y, ECX = A.x - E.x; the first 427238 EAX =
(high word of n | radius word), EDX = the remainder, ECX = s16 step; later
ones EAX = (high word of D.x | radius word), EDX = D.y, ECX = D.z. The only
caller 427da8 tests AX; its loop end hands the high word of EDX on (dead,
see above) and ECX (dead). ECX is not reproduced. 7 conditional sites, all
feasible both ways.

### 4927f0 `set_length(V, length)`

cdecl, two slots; V points to three dwords, `length` is read as a word.
Callers: 4273c8 and 40b804; both ignore EAX, EDX and ECX afterwards (4273c8
rewrites them at 427494..4274d1, 40b804 at 40b9da..40b9e2).

1. m = 492a7d(V), the octagonal length of (V.x, V.y, V.z) (492a7d -> 492aaf
   -> 47de72 `dist3d_delta`); EDX is then the sign of V.z (CDQ), and the
   `pop ecx` after the call leaves ECX = V.
2. m = 0 (JE 492804): memset(V, 0, 12) through the CW3220 import thunk
   49681a (entry EAX = 0, EDX = sign of V.z, ECX = V); EAX = V, EDX = the
   runtime's EDX.
3. Otherwise q = (s16 length << 16) / m (492b04, signed IDIV; INT_MIN / -1
   faults and is outside the contract) and f = s16 (low word of q). V.x,
   V.y, V.z (in that order, each read just before its write) become
   47e08d(v, f) = low dword of ((v * f) >> 16) (64-bit signed product).
   EAX = V, EDX = high dword of the last product, ECX = the old V.z.

The replacement computes 492a7d, 492b04 and 47e08d in core (pure integer
code; their other callers keep the originals) and calls only the memset
thunk through the bridge, so the runtime's register results stay exact. One
conditional site, both ways.

### Hooks, registers and validation

Prologues: 427da8 `55 8b ec 81 c4 64 ff ff ff 53 56 57` (relocate 9:
1/2/6), 4273c8 `55 8b ec 83 c4 b4 53 56 57` (relocate 6: 1/2/3), 4927f0
`55 8b ec 53 56 57 8b 5d 08` (relocate 5: 1/2/1/1). No x87 instruction, no
indirect call, no entry shim. All three accept compare mode: spans on the outputs (object_query also on the segment
record, whose P+4 it shortens, and an RVA region for [4aaad0]),
comparators for AX (object_query), the remapped high word of the hit EAX
(grid_sweep) and EAX = V (set_length); the objects' lazy matrix cache is
filled by the original phase, so the replacement phase writes no object
([compare_mode.md](../reference-game/compare_mode.md#spans-and-comparators)).
The only callee that stays original is the CW3220 memset; all others (49288c, 48043c,
47ebb8, 47df19, 480304, 47de72, 427238 and the two new hooks) are hooks,
each entered through the bridge with its own configured mode.

## Projectile and bullet steering

Four hooks. Core `core/src/projectile.c` (`es2/core/projectile.h`),
adapter group `projectile` (`tools/reference-game/proxy/projectile_hooks.h`).

| Hook | VA | Role |
|---|---|---|
| `vector_angle` | 49290c | angle of the vector (x, y); (0, 0) counts as (1, 0) |
| `aim_angles` | 492990 | pitch/roll/heading words of the direction start → end |
| `projectile_steer` | 40a254 | homing step of a projectile towards its target (called by `projectile_flight`) |
| `bullet_steer` | 40aff0 | homing step of a bullet towards its target (called by `bullet_flight`) |

All callees of the two steering bodies are hooks (`euler_matrix`,
`transform_point`, `approach_word`, `movement_step`, `aim_angles`), plus
the target's virtual +24 and +58 slots.

### 49290c `vector_angle(x, y)`

cdecl, two dword slots. If x = 0 and y = 0 (JNE 492919, JNE 49291d) x
becomes 1. Then 492ac8(x, y), a plain forwarder to `angle_ratio` 47d32c.
Result: EDX:EAX exactly as `angle_ratio` returns them (EAX = angle word,
upper half zero; EDX = correction byte or DIV remainder), and ECX =
47d32c's divisor register: with a = |x| and b = |y| (wrapped 32-bit
absolute values), ECX = b if b ≠ 0 and s32(b) > s32(a) (the XCHG at
47d372), otherwise a. No memory except the 513-byte correction table at
4a1c34; no write. Domain as `angle_ratio`: y ≠ 0 with an INT_MIN argument
faults (#DE) in the original. The two branch sites are already in the
in-vitro list under `heading_point` (which computes 49290c inline), so
they are not listed twice; the `vector_angle` cases drive both outcomes of
both.

Direct callers: 407113 (in 407098), 422143 (422108), 43c54b (HUD 43c40c),
49294b (`heading_point`), 492983 (49295c, which returns 49290c's EAX, EDX
**and ECX** unchanged), 4929ba and 4929e8 (492990).

### 492990 `aim_angles(out, end, start)`

cdecl, three pointers; `end` and `start` are XYZ dword points, `out`
receives three words. Order of memory accesses (it matters when `out`
overlaps a point):

1. dx = end.x − start.x, dy = end.y − start.y (reads end.x, start.x,
   end.y, start.y; stored at EBP−4 and EBP−8).
2. h = `vector_angle`(dx, dy); word out+4 = AX + C000h; word out+2 = 0.
3. dz = end.z − start.z (read **after** those two writes).
4. d = 492a9a(dx, dy) = 47de4c(dx, dy) (`dist2d_delta`: the larger
   magnitude plus half the smaller, signed compare, wrapped).
5. p = `vector_angle`(d, dz); word out+0 = AX.

Result: EAX = out, EDX = the second `vector_angle`'s EDX, ECX = dx (the
two POP ECX at 4929f8/4929f9 reload the locals EBP−8 and EBP−4). No
branch in the body, no global, no write besides the six bytes of out. Both
`vector_angle` calls must lie in the `angle_ratio` domain.

Callers: 13 direct sites, among them `turret_target`,
`part_facing`, both steering hooks, `weapon_eject` and `object_line`.

**ECX.** The hook framework returns only EDX:EAX; after a replaced call
ECX is the value it had at the hook's entry (the dispatch stub restores
the saved registers with POPAD). A forward liveness walk from 4929fb over
the whole listing (a walker that also
treats `push ebp; mov ebp, esp` and `enter` as function starts) finds ECX
read after some callers: as the upper half of a word argument at 40d8f5
(4089e4 reads it as a word only), through `mov eax, ecx` at 41f1d6 (EAX's
upper half then dead), at the `push ecx` reservation of 471b30 (restored
and popped by 40f6d7), and, through chains of returns into the torso servo
and the input dispatcher, at word arguments, local reservations, the
virtual +60 call at 41f702 and returns of functions reached only through
vtables. Rather than prove each of those dead, the adapter installs an
**entry shim** that sets ECX = end.x − start.x before the framework's
dispatch, so the replaced call returns the original's ECX exactly. The same
holds for `vector_angle`, whose ECX reaches more readers (virtual +3C
calls behind the `heading_point` callers): its entry shim computes the
divisor register described above. The originals do not read their entry
ECX, so the shims change nothing in original or compare mode. With them
both hooks return EAX, EDX and ECX bit-identically for every caller; only
the flags differ, which no caller reads after a call.

### 40a254 `projectile_steer(O)`

cdecl, one slot: O = projectile object (constructor 40a9c4). T = [O+56h]
(target), owner = [O+4Ah], type = s8 [O+41h]. Frame of 28h bytes; the
locals used are L8/L6/L4 (EBP−8/−6/−4: the object's angle words), L28
(EBP−28h, 12-byte target point), L1C, L10/LE/LC (the six bytes `aim_angles`
writes) and L18/L16/L14 (three words cleared). Registers below are the
ones each callee receives; the core reproduces them on every path.

1. T = 0 (JE 40a264): return the entry EDX:EAX.
2. EAX = owner+92h, EDX = owner. If byte owner+A3h ≠ 0 (JE 40a278) and
   [owner+1A4h] ≠ [O+56h] (JNE 40a283; ECX = [owner+1A4h]): return.
3. S = [O+56h]+92h. flag = 1 if byte S+4 ≠ 0 (JNE 40a298; EAX = 1, EDX
   unchanged), else EDX = S and flag = (byte S+0Fh ≠ 0) (JNE 40a2a0; MOV
   EAX,1 or XOR EAX,EAX). EDX = type. type = 2 with AX = 0 (JNE 40a2b2,
   JE 40a2b7) returns EAX = 0, EDX = 2.
4. L8, L6, L4 = words O+C, O+E, O+10 (through DX, CX, AX: their upper
   halves are kept, EAX's from O+C). L18 = L16 = L14 = 0. AX = word O+5Ah.
5. Target point: if s16(O+5Ah) ≥ 0 (JL 40a2f0): T.V58(T, EAX, &L28) with
   EAX = T's vtable, EDX = &L28, ECX = T (the part slot carries O+C's
   upper half). Otherwise R = T.V24(T) (EAX as in 4, EDX = T, ECX = vtable;
   POP ECX = T). R ≠ 0 (JE 40a313): C = [O+56h]+0Ch; if word C+26h = 0
   (JNE 40a320): L1C = C, `euler_matrix`(C, C+6) (EAX = C+6, EDX = C,
   ECX = T), word C+26h = 1; then `transform_point`(R+14h, C+6, &L28)
   (EAX = C+6, EDX = R+14h, ECX = &L28). R = 0: L28 = the three dwords at
   [O+56h]+26h, copied Z, X, Y (EDX = Z, ECX = X, EAX = Y).
6. `aim_angles`(&L10, &L28, O+26h) with EAX = &L10, EDX = O+26h, ECX =
   &L28. Then LC −= L4, L10 −= L8, LE −= L6 (DX, CX, AX loaded from the
   locals over the callee's registers).
7. type = 3 (JNE 40a39b): byte owner+B5h = 1 (EAX = owner+92h). Else
   type = 2 (JE 40a3b2) or byte owner+9Ch = 0 (JE 40a3c0) skip; otherwise
   the **near-zero push**: for the heading delta LC (AX = LC, EDX = EAX
   with DX = LC + C00h, ECX = (DX < 1800h unsigned) as 0/1, JE 40a3db):
   if the delta lies in [−C00h, BFFh], add C00h for a negative delta
   (JL 40a3e4) or EDX + FFFFE800h (DX = F400h, EDX's upper half minus one)
   for a non-negative one; then the same for the pitch delta L10 with the
   roles of ECX and EDX exchanged (JE 40a409, JL 40a412).
8. `approach_word`(&L18, AX = L10, 500h) with EAX = &L18;
   `approach_word`(&L14, DX = LC, 500h) with ECX = &L14; then L8 +=
   AX of `movement_step`(AX = L18) and L4 += AX of `movement_step`(DX =
   L14), each followed by POP ECX (ECX = the pushed value).
9. dword O+C = L8 | L6 << 16, word O+10 = L4, word O+32h = 0 (the cached
   matrix is invalid). Return EAX = O+C, EDX = L6 << 16 | L4.

No local is read before it is written (V58 writes its 12 output bytes on
every path in both known bodies, 41b60c and 406808, see the V58 table
above; the V24 bodies 417b98 and 403548 and the leaves 467adc/467924 do
not write their argument slots, so each POP ECX reloads the pushed value).
17 conditional branch sites.

### 40aff0 `bullet_steer(O)`

cdecl, one slot; T = [O+5Bh], always nonzero at the only caller (40b1c7
tests it). Same frame layout; no early exit, no V58 path, no type gate and
no near-zero push:

1. L8/L6/L4 from O+C/E/10 (the upper halves of the entry EDX/ECX and of
   O+C kept), L18 = L16 = L14 = 0.
2. R = T.V24(T) (EAX = O+C's upper half | word O+10, EDX = T, ECX = the
   vtable; POP ECX = T); R ≠ 0 (JE 40b036) runs the cache check (JNE
   40b043) and `transform_point` as in 40a254, R = 0 copies [O+5Bh]+26h.
3. `aim_angles`(&L10, &L28, O+26h); LC −= L4, L10 −= L8, ECX = &L18,
   LE −= L6.
4. `approach_word`(&L18, DX = L10, 280h) with ECX = &L18;
   `approach_word`(&L14, AX = LC, 280h) with EDX = &L14; L8 += AX of
   `movement_step`(CX = L18), L4 += AX of `movement_step`(AX = L14), each
   with POP ECX.
5. dword O+C = L8 | L6 << 16, word O+10 = L4, word O+32h = 0. Return EAX =
   O+C's upper half | LC (the remaining heading delta, which
   `bullet_flight` uses for its proximity test), EDX = L6 << 16 | L4.

Two conditional branch sites.

### Hooks, registers and validation

Prologues: 49290c `55 8b ec 53 56 8b 75 0c` (relocate 5: 1/2/1/1), 492990
`55 8b ec 83 c4 f8 53 56 57`, 40a254 and 40aff0 `55 8b ec 83 c4 d8 53 56
57` (relocate 6: 1/2/3). No x87 instruction. `vector_angle` and
`aim_angles` call no game code (the core computes 492ac8/47d32c and
492a9a/47de4c) and use the entry shims described above; `vector_angle`
allows compare mode (no write, read-only correction table), `aim_angles`
refuses it (its six output bytes may overlap the inputs). The two steering
hooks call every callee through the weapons guest bridge with the
original's registers (`projectile_flight` and `bullet_flight` reach them
through the same bridge), read the virtual
targets from the receiver at each call and refuse compare. Their ECX at
return is dead: both callers pop it (the walks from 40a484 and 40b121
report no read). The frame locals live in an adapter buffer; their
addresses appear in the callees' argument and register slots as the
original's frame addresses do.

## Weapon mount helper 40f540 and 40f904

Core `core/src/mount.c`, adapter group `mount`. W is a weapon,
C = `[W+18h]` its definition (the byte C+6 selects the mount side, the words
C+10h/12h/14h are the mount offset), M = `[W+1Ch]` the carrier whose words
M+46h/48h/4Ah hold the base mount point (roles from the callers; the
arithmetic does not depend on them).

### 40f904 `mount_base(M, side)`

cdecl, two slots, EBP frame without locals; only the low word of `side` is
read (MOVSX). It fills a static three-word vector V at `4a9af0` and returns
its address.

1. The byte `498f34` (the compiler's guard of the function-static V) is
   tested; when it is 0 it is incremented to 1 (JNE 40f911). V has no
   constructor, so the guard has no other effect; nothing else reads it.
2. EDX = dword M+46h, stored to `4a9af0` (V.x and V.y). Then DX = word
   M+4Ah (read **after** that store), stored to `4a9af4` (V.z); word
   `4a9af2` (V.y) ← 0.
3. s = s16 side. s > 4 unsigned (JA 40f93d, also every negative s): V stays
   (x, 0, z). Otherwise the jump table at 40f946:
   - 0: V.x ← 0;
   - 1: V.z ← −V.z, V.x ← 0;
   - 2: V.x ← −V.x, V.z ← 0;
   - 3: V.z ← 0;
   - 4: ECX ← 0, V.z ← 0, V.x ← 0.

Result: EAX = `4a9af0`, EDX = the dword M+46h with its low word replaced by
word M+4Ah (as read in step 2), ECX = 0 for side 4, else the entry ECX.
No entry register or local is read; no game call; no x87.

### 40f540 `mount_offset(W)`

cdecl, one slot, EBP frame, EBX saved.

1. C = `[W+18h]`; `40f904([W+1Ch], s8 C+6)` with EAX = C, EDX = s8 C+6
   (the pushed value), ECX = `[W+1Ch]`. V = its EAX.
2. C is read again (`[W+18h]`, after the call). Word V+4 += word C+14h,
   then word V+0 += word C+10h, then word V+2 += word C+12h (16-bit
   wrapping read-modify-writes in this order, each source word read just
   before its add, so a V that overlaps C sees the earlier adds).

Result: EAX = V, EDX = (C + 10h) with its low word replaced by word C+12h,
ECX = 40f904's ECX with its low word replaced by word C+10h. No entry
register or local is read.

### Mount callers and register liveness

40f540 has seven call sites in six functions: 40dfee (40df30), 40e8ea
(`weapon_prepare` 40e788), 40eb44 and 40ebad (`emit` 40ea58), 40f62b
(40f57c), 4176e4 (4175dc) and 41a797 (`weapon_converge` 41a74c); the three
hooked callers call it through the guest bridge. 40f904 is called only at
40f553 (40f540). Neither address appears as a dword elsewhere in the file.

ECX after 40f540 is dead: all seven sites `pop ecx` right after the call
(40e8ef reads only `[eax+4]` before its POP). So the replaced hooks'
ECX (the framework returns the entry ECX) is outside the contract; in
particular 40f904's ECX for side 4 reaches only 40f540's ECX high word.
EDX after 40f540 reaches 40dd4c's entry at 40dff6 and is otherwise
overwritten; it is returned exactly.

### Hooks

| Hook | Prologue | Relocate / lengths | Slots | Result | Callee counters |
|---|---|---|---:|---|---|
| `mount_offset` | `55 8b ec 53 8b 5d 08` | 7 / 1,2,1,3 | 1 | EDX:EAX | call_40f904 |
| `mount_base` | `55 8b ec 8b 45 08` | 6 / 1,2,3 | 2 | EDX:EAX | — |

Both refuse compare mode (`mount_base` writes the static V and the guard
byte, `mount_offset` calls game code). `mount_offset` calls 40f904 through
the weapons guest bridge with the registers above, so `mount_base` stays
nested in its own mode. No entry shim. Branch sites: 40f911 (JNE) and
40f93d (JA), both ways; the jump-table dispatch at 40f93f is not a Jcc
site, its five targets are reached by the selector values 0..4 in vitro.
40f540 has no branch.

## Weapon-system routines below the weapon keys

Core `core/src/weapon_system.c`, adapter group `weapon_system`. W is the weapon system of a vehicle
(`[P+202h]`, built by `0x4104ec`, vtable `0x499238`): `[W+4]` the weapon
list (one dword per slot, 0 = empty), word W+8 the slot count, byte W+16h
a "group changed" flag, byte W+18h, byte W+1Ch the current **fire group**
g (0..2; the B key cycles it), signed byte W+1Dh the **selected slot**
(−1 = none), and at W+1Fh three six-byte group records (g at W+1Fh+6g):
a dword pointer to a word table T_g with one membership word per slot,
then a word count. The constructor fills group 0 with every weapon except
the equipment types 12h and 1Dh..20h, groups 1 and 2 with zeros, sets the
selection to the first non-equipment weapon and calls `0x410ae4(W, 0)`.
A weapon E: vtable `[E]`, definition D = `[E+18h]` (signed byte D+16h,
byte D+7 = the number the Alt+1…Alt+0 keys use), carrier record `[E+1Ch]`
(dword +30h > 0 required by `0x4110ac`), bytes E+4Bh and E+4Ch (readiness
flags read by the step). The roles are hypotheses from the key table
([input.md](input.md)) and the constructor; the arithmetic below does not
depend on them.

Hooked here: `0x41074c` `select_step`, `0x410ae4` `fire_group`,
`0x410d04` `weapons_v3c`, `0x4110ac` `group_toggle`. No dword in the file
points at any of them (byte search for the little-endian VAs), so the
direct calls listed below are all callers.

**Neighbouring leaves.** `0x410708` (sets the selection, 26 instructions;
callers 4106e3, 4107d3, 410ad5, 410c6d) is the hook `weapon_select`
(contract in [hud.md](hud.md#0x410708-weapon_selectw-slot)); `0x410cd0`
(toggles one membership word; caller 4110df only) is `member_toggle` and
`0x441f90` (stores the HUD fire-group word H+0B1h) is `gauge_mode`
([Player weapon system](#player-weapon-system)). **Trap:** a replaced hook
returns the framework's entry ECX, and both leaves' ECX is read further
on: 410708's ECX reaches the weapon virtual +2Ch at 410982 (in
`0x410970`; the 498dcc body 40ecdc copies ECX's high word into EDX), and
410cd0's ECX is the entry ECX of the weapon virtual +48h. The routines
here call them through the guest bridge with the original's registers, so
each runs in its own mode; `member_toggle` returns the original's ECX
through its exit shim (Player weapon system, Hooks). `0x4610ad` (getter
of the HUD sub-object `[[0x49b088]+1E1h]`) stays original. The weapon
virtuals +3Ch and +48h (bodies: 4111d3, an empty function, and 4111e2,
`xor eax,eax`, for most classes; for the classes 498dcc/4992c0 40f4d8
sets word E+7Bh to 334h unless byte E+49h is set, and 40f504 returns
2710h when byte E+43h is set, else word E+7Bh) are the weapon-class hooks
`weapon_none_3c`, `weapon_priority`, `beam_preset` and `beam_priority`
([weapon_classes.md](weapon_classes.md)), entered as virtual calls.

**`0x40e4dc`** (aim through the weapon `[P+30Bh]`; caller 41b779 in
`0x41b728` only) is the hook `targ_aim` and `0x40e4ac` is `targ_next`
(`core/src/input.c`, group `cold_paths`). Seven game calls (the weapon
virtuals +84h, +58h and +24h, `0x40e4ac`, `0x467aa8`, `0x47ebb8`
`euler_matrix`, `0x48043c` `transform_point`) and two output pointers.

### 41074c `select_step(W, d)`

cdecl; d is read as a word (`add ax, [ebp+0Ch]`). Frame: `push ecx`
reserves the dword EBP-4, whose high word EBP-2 is the local `found`; the
low word keeps the entry CX. EDX = W throughout.

1. AL = byte W+1Dh (EAX's high 24 bits stay the **entry EAX**'s);
   ECX = s8 sel + 1; JLE 4107db when sel < 0: return at once with
   EAX = (entry EAX & FFFFFF00h) | sel, EDX = W, ECX = the entry ECX.
2. SI = word W+8 (**ESI's high word stays the caller's**), EAX = s8 sel,
   `found` ← 0.
3. Loop: AX += d (16 bit). If s16 AX < 0: EAX = ESI − 1 (the whole
   register: the caller's high word, minus a borrow when the count is 0).
   Else if s16 SI > s16 AX: keep; else EAX = 0. E = `[[W+4] + 4·s16 AX]`
   (the list pointer read each round). If E ≠ 0 and byte E+4Ch ≠ 0 and
   (byte E+4Bh = 0 or s8 `[E+18h]`+16h > 0): `found` = word T[s16 AX]
   with T = `[W+1Fh+6·s8 W+1Ch]` (group byte and table pointer read each
   round). If `found` = 0 and s16 AX ≠ s8 W+1Dh (reread): next round.
4. `0x410708(W, EAX)` (the whole EAX as the slot) with EAX = the index,
   EDX = W, ECX = (high word of s16 AX) | `found` when a member was found,
   else s16 AX.

Result: 410708's EAX (= W) and EDX (the index slot, its low word adjusted
by 410708 when the definition byte is negative); ECX = `found` : entry CX
(POP ECX of the reservation). The loop ends only when a member is found or
the index comes back to the selection: with d = ±1 (every caller) and
0 ≤ sel < count that takes at most count + 1 rounds; with sel ≥ count or
another step it may not end (outside the callers' domain).

Entry registers read: EAX (high 24 bits, step 1 only) and the caller's ESI
(high word, step 3). **Entry shim:** EDX is dead at entry (written before
any read), so `weapon_system_select_entry` copies ESI into EDX before the
framework's dispatch; the replacement reads the caller's ESI from its
entry EDX. No game instruction is embedded.

### 410ae4 `fire_group(W, g)`

cdecl; EBX = g (the whole slot), EAX = W, EDX = s16 g, ECX = s8 W+1Ch.

1. s16 g = s8 W+1Ch: return at once (EAX = W, EDX = s16 g).
2. EDX = g; byte W+1Ch ← DL. CL = byte W+1Dh (ECX = (s8 old group &
   FFFFFF00h) | sel), ESI = s8 sel + 1. JLE 410b2a when sel < 0 (EDX = g).
3. ECX = s8 sel, EDX = T = `[W+1Fh+6·s8 DL]`; if word T[sel] = 0 (the
   selected weapon is not in the new group): `0x41074c(W, 1)` with
   EAX = W, EDX = T, ECX = s8 sel and **ESI = s8 sel + 1** (high word 0;
   41074c reads it).
4. `0x4610ad` with g in its (unused) stack slot, then `0x441f90(H, g)`
   with H = 4610ad's EAX; the registers flow through both calls.

Result: 441f90's EDX:EAX (EAX = H, EDX's low word = g). No entry register
is read. ESI is set before the 41074c call, so the replacement calls
41074c through an own bridge that loads that ESI.

### 410d04 `weapons_v3c(W)`

cdecl, frame 8: EBP-8 = p = `[W+4]`, word EBP-4 = c = word W+8; the word
EBP-2 is never written. EAX = W+4, EDX = p, CX = c (ECX's high word is the
entry ECX's).

Loop (410d2c): CX = c, c −= 1 (16 bit); CX = 0 → EAX = 0, end. Else
EAX = p, p += 4; EAX = 0 → end (only when p reaches 0). E = `[EAX]` (read
at that moment), EDX = E; E ≠ 0: `E.V3C(E)` with EAX = E's vtable, EDX = E,
ECX = (previous ECX high word) | CX; POP ECX = E. The count is unsigned:
a negative word W+8 runs up to 65535 rounds.

Result: EAX = 0; EDX = `[W+4]` when no slot is visited, else the last
visited slot's E (0) or the last V3C's EDX. ECX = dword EBP-4 = the
stack word EBP-2 (uninitialized) : c (FFFFh after a full count), dead
(POP ECX at 410fdd).
The only entry register read is ECX's high word.

### 4110ac `group_toggle(W, n)`

cdecl; ECX = n (the whole slot, compared as a word), EDX = W, EAX = i = 0.

For s16 i < s16 word W+8 (reread each round): E = `[[W+4] + 4·s16 i]`;
skip when E = 0, when CX ≠ s8 `[E+18h]`+7 (16-bit compare) or when the
dword `[[E+1Ch]+30h]` ≤ 0 (signed). On the first match:

1. `0x410cd0(W, EAX)` (the whole EAX = i) with EAX = i, EDX = W, ECX = n.
2. `E.V48(E)` (vtable `[E]` read again) with EAX = the vtable, EDX/ECX
   as 410cd0 left them; POP ECX = E.
3. AX = 0: `E.V3C(E)` (vtable read again) with EAX = V48's EAX, EDX = the
   vtable, ECX = E; POP ECX = E.
4. AX ← 1: EAX = (V48's or V3C's EAX & FFFF0000h) | 1, EDX = that call's.

No match: EAX = 0, EDX = W. ECX at return: E after a match, else n.
No entry register is read. **Entry shim:** ECX escapes through the returns
of the MFD gadget methods 440f18 and 4414dc (slot 0 of the tables 49c91c
and 49c8fc, no direct callers), so `weapon_system_toggle_entry` computes
the original's return ECX (the same search, reads only) before the
framework's dispatch; the replaced call then returns EAX, EDX and ECX
exactly.

### Callers and register liveness

| Function | Call sites (function) |
|---|---|
| 41074c | 410a84 (`0x410a3c`), 410b22 (`0x410ae4`), 410b63 (`0x410b40`), 411012 and 411029 (`weapon_keys` 410f78) |
| 410ae4 | 41061f (constructor `0x4104ec`), 410bbe (`0x410b40`), 410ffb (`weapon_keys`) |
| 410d04 | 410fd8 (`weapon_keys`) |
| 4110ac | 41109a (`weapon_keys`), 440f59 (MFD `0x440f18`), 44151c (MFD `0x4414dc`) |

`0x410b40` runs every frame (from `0x41b130`) and calls
410ae4 with the HUD's fire-group word whenever byte W+16h is clear, and
41074c when word `4d2528` ≠ 0 and word `4d2526` = 0; `0x410a3c` (also
every frame, from 410b40) steps the selection when the selected weapon is
not ready.

ECX walks (over the listing with symbol suffixes stripped):

- After 41074c: dead at 410a89, at 410b68 (the `push ecx` reservation of
  410a3c, both words written at 410a63/410a67 before any read), through
  410ae4's return (below) and through `weapon_keys` (escapes only through
  4157c8's return; dead after its virtual call sites 460841/460854,
  [input.md](input.md)).
- After 410ae4: dead at 410624 and through `weapon_keys`; at 410bc3 the
  only reported readers are the HUD virtual +4 calls in `0x441fa4`
  (441fe1/441ff8), reached from `0x4420c8` only when byte H+0B3h differs
  from the copy that `0x442120` took earlier in 410b40. On the path that
  keeps ECX (copy byte 0, so 410f04/410f14 and their POP ECX are skipped)
  nothing in between writes H+0B3h (410ae4, 41074c, 410708 and 441f90 do
  not), so that call is not reached. Dead.
- After 410d04: POP ECX at 410fdd.
- After 4110ac: returned exactly through the shim (above).

EDX and EAX are returned exactly by all four. The replaced 41074c gets the
caller's ESI from its shim. When **`weapon_keys` is replaced** it calls
41074c through the plain guest bridge, whose ESI is the bridge's own, so
the index slot's high word (and 41074c's EDX) differs from the original's
on the path "step below 0" (Alt+W at slot 0), whether 41074c is original or
replaced. That EDX leaves 410f78 through 4157c8 and is dead after
460841/460854; the other callers of 410f78 (461005, 46104a, 461066 in
`0x460870`) were not walked (open).

### Stack residue and branch sites

410d04's EBP-2 is read only into the dead ECX. No other local is read
before it is written; 410708 and 410cd0 read no uninitialized stack.
Branch sites: 41074c 9 (41075d, 410773, 41077d, 41078c, 410795, 41079e,
4107a7, 4107c4, 4107cf), 410ae4 3 (410af8, 410b06, 410b1d), 410d04 3
(410d23, 410d38, 410d47), 4110ac 5 (4110c6, 4110d2, 4110db, 4110f1,
411105); all are reachable both ways (410d47's fall-through needs
`[W+4]` = 0, or a list running up to address 0).

## Building turret target 403eec and part facing 406868

Two hooks. Core in `core/src/building.c` (`es2/core/building.h`), adapter
group `building_aim` (`tools/reference-game/proxy/building_aim_hooks.h`).

| Hook | VA | Role |
|---|---|---|
| `turret_target` | 403eec | the turret's aim angles toward a world point: the point in the building frame, then in the turret part's frame, then `aim_angles`, then `turret_aim` |
| `part_facing` | 406868 | building virtual +54h: the index of the living part whose direction from a viewer deviates least from the viewer's angles (or the first living part without a viewer) |

Both are simulation (no drawing, no sound, no allocator or RTL call).
Callees: `euler_matrix` 47ebb8, `matrix_transpose` 47df19, `rotate_point`
480304, `transform_point` 48043c, `aim_angles` 492990 and `turret_aim`
403d5c are hooks; the shape manager's virtual +20h (403eec) and the
building's virtual +58h (406868) are virtual calls. All go through the
weapons guest bridge with the registers the original holds at the call.

### 403eec `turret_target(B, P)`

cdecl, two slots: B = building (the turret, caller `turret` 404100 at
404362), P = a 12-byte world point (turret's local at EBP-54h). Frame of
80h bytes; locals by EBP offset: T2 -80h and T1 -74h (12 bytes each), M2
-68h and M1 -48h (32-byte matrices: nine rotation words, mode byte +12h,
byte +13h, translation dwords +14h/+18h/+1Ch), Z -28h, V1 -1Ch, V2 -10h (12
bytes each); -4h holds a copy of B+0Ch that is never read back. S = the
static angle triple at 4a95ec (six bytes written by `aim_angles`), G = the
byte 4974d5 (function-static guard; no constructor runs).

1. If byte G = 0 (JNE 403f02): G = 1 (INC). Z = (0, 0, 0). EAX = EDX =
   ECX = 0.
2. If word B+32h = 0 (JNE 403f24): `euler_matrix`(B+0Ch, B+12h) with EAX
   = B+12h, EDX = B+0Ch, ECX = 0; then word B+32h = 1 (after the call).
3. M1 = the 32 bytes at B+12h (words +0..+10h, bytes +12h/+13h, dwords
   +14h..+1Ch, in that order).
4. Mgr = [B+34h], O = [Mgr+4]: O's virtual +20h(O, 2) with EAX = O, EDX =
   [O], ECX = B. If the result R ≠ 0 (JE 403fbe) and s16 [R+4] > -1 (JLE
   403fc5): record = [Mgr+16h] (read after the call) + 32 · s16 [R+4];
   else record = 6c56a8. M2 = the 32 bytes at record. Registers after the
   copy: EAX = [record+1Ch], EDX = [record+14h], ECX = [record+18h].
5. `matrix_transpose`(M1) (registers from step 4); ECX = M1 (POP).
   Translation of M1 negated (wrapping) in the order +1Ch, +14h, +18h; T1
   = M1's translation; `rotate_point`(T1, M1, M1+14h) with EAX = M1+14h,
   EDX = &T1, ECX = the negated +1Ch.
6. The same for M2 and T2: `matrix_transpose`(M2) with the registers of
   the previous call's result, ECX = M2, negation, `rotate_point`(T2, M2,
   M2+14h) with EAX = M2+14h, EDX = &T2, ECX = negated M2+1Ch.
7. `transform_point`(P, M1, V1) with EAX = P, EDX = &V1, ECX = the
   previous result's; `transform_point`(V1, M2, V2) with EAX = &V1, ECX =
   &V2, EDX = the previous result's.
8. `aim_angles`(S, V2, Z) with EDX = &Z, ECX = &V2, EAX = the previous
   result's.
9. `turret_aim`(B, S) with EAX = B, EDX/ECX = `aim_angles`' result.

Result: EAX = S (4a95ec), EDX = `turret_aim`'s EDX. Memory written: G,
the cache word B+32h, the matrix cache B+12h..31h through `euler_matrix`,
S through `aim_angles`, and whatever the callees write. No local is read
before it is written (M1/M2 are fully copied, T1/T2/Z are written, V1/V2
are written by `transform_point`), so no entry shim. 6c56a8 is the same
default record `turret` and `part_position` use.

**Registers after the call.** The hook returns its entry ECX; the
original returns `turret_aim`'s ECX. At the only call site (404362) the
next call 467a48 (`timer_word_step`) writes CX before reading it and the
caller pops ECX at 40437c, so ECX is dead. EDX is returned exactly.

Branch sites: 403f02, 403f24, 403fbe, 403fc5; all four reachable both
ways (G is 0 only on the first call of a session; R = 0 and s16 [R+4] ≤ -1
both select the default record).

### 406868 `part_facing(B, P)`

Building virtual +54h: the only DATA references to 406868 are slot +54h
of the five building-pool vtables 497784, 497818 (`VEHICLE`), 4978ac
(GUN_BASE), 497940 (BASE) and 4979d4 (their slot +58h is 406808, the
building part position; slot +18h 4045c8, 46a6d4, 404100, 403ca8,
403ca8). Callers through +54h
(40aacb in the projectile constructor 40a9c4 on the shooter's target,
P = projectile + 0Ch or 0; 41eac5 in 41ea7c for type-1 targets, P = 0)
read AX only. cdecl, two slots: B = the
building, P = 0 or a pose (three angle words at P+0/+2/+4, position at
P+1Ah, the layout of actor + 0Ch). Frame of 18h bytes: L18 -18h (12-byte
part position), Lc -0Ch (three angle words), best index word -6h (init
FFFFh), best value dword -4h (init 1000000h).

1. i = 0 (EBX, 32 bits), p = [B+201h] (read once; a word per part).
2. Loop head (40693e): EDX = B, ECX = D = [B+1F2h] (reread every pass);
   continue while s16 i < s16 [D+12h] (JL 40694b, word compare of BX).
3. Body, part i: if word [p] = 0 (JE 406891) skip. If P = 0 (JE 40689b)
   return EAX = i (all 32 bits), EDX = B: the first living part.
4. B's virtual +58h(B, i, &L18) with EAX = &L18, EDX = B, ECX = [B].
5. `aim_angles`(&Lc, &L18, P+1Ah) with EAX = P+1Ah, EDX = &L18, ECX =
   &Lc.
6. Lc+4 −= word P+4, Lc −= word P+0, Lc+2 −= word P+2 (16-bit, in that
   order; Lc+2 is never used). a = |s16 Lc|, c = |s16 Lc+4| with 8000h →
   7FFFh (JNE 4068eb/40690d, JGE 4068f6/406918); EAX = a + c (0..FFFEh).
7. If s32 EAX < best (JGE 406931): best = EAX, best index = i.
8. i += 1, p += 2.

Result (loop exit): AX = best index (FFFFh if no part qualified), the
high word of EAX = 0 if any part was measured (the last sum), otherwise
the caller's entry EAX high word (EAX is not written before the loop);
EDX = B. ECX = D at every exit (the hook returns its entry ECX instead;
dead at both call sites: 40aaf1 pops ECX after 40a234, which does not read
it, and 41ead4 loads ECX before any read). No global is read or written.

**Uninitialized stack.** `aim_angles` reads L18, which only the virtual
+58h writes. Both implementations known write all 12 bytes (406808 and
the vehicle's 41b60c end in 4801d4/48043c with L18 as output; see
player_movement.md rows 41b60c and 406808), so no stack residue is read;
a virtual that left L18 incomplete would be outside the contract.

Branch sites: 406891, 40689b, 4068eb, 4068f6, 40690d, 406918, 406931,
40694b; all reachable both ways (406931: the first measured part always
beats 1000000h, a later tie or larger sum does not).

### Hooks, registers and validation

Prologues: 403eec `55 8b ec 83 c4 80` and 406868 `55 8b ec 83 c4 e8`
(relocate 6: 1/2/3). No x87 instruction, no entry shim, no own-assembly
bridge. The adapters give the core a zeroed host frame of 128 (403eec) or
24 bytes (406868) at the same offsets as the original frame (frame base −
80h or − 18h), so stack pointers passed to callees keep their relative
layout. Both refuse compare mode (they call game code). Callee counters:
`turret_target` call_47ebb8, slot_20, call_47df19, call_480304,
call_48043c, call_492990, call_403d5c; `part_facing` slot_58,
call_492990.

## Beam constructor 40b804

One hook. Core in `core/src/weapons.c` (`es2/core/weapons.h`), adapter
group `beam` (`tools/reference-game/proxy/beam_hooks.h`).

| Hook | VA | Role |
|---|---|---|
| `beam_create` | 40b804 | constructor of the `BEAM` object (vtable 4987c4, base vtable 4987a0) that `impact` places in a pool node: a polyline from a start to an end point, for types 1 and 7 cut into 1024-unit steps with random jitter |

**`0x49667c` is RTL (stays original).** Its body is the Borland runtime's
array construction (`_vector_new_`-style): `(p, size, count, flags, ctor)`;
p = 0 allocates `size·count` (+4 with flag 10h, which stores the count
first) through the heap helper `0x4774f0` and returns 0 on failure; then
it calls `ctor` once per element with one of four calling conventions
chosen by `flags & 7` (anything else reaches the runtime error `0x49676c`)
and with flag 200h restores the runtime's exception context word through
FS:4. 133 call sites in DBSIM. It is the runtime allocator path
([cockpit.md](cockpit.md)) and not hooked: every caller reaches it through
the guest bridge. Here it is called twice, both times as
`(0, 12, count, 1, 0x4953c4)` (12-byte elements, the runtime's element
constructor `0x4953c4`, itself not hooked).

Callers of 40b804: exactly three call sites, all in `impact` 40bf74
(`0x40c0b4` for types 1/7, `0x40c200` in the 5000-unit loop and
`0x40c276` for the final segment of the other types); no data reference
to 40b804 exists. Every caller has just allocated O through `pool_alloc`
and tested it for null, so O ≠ 0. After each call the caller reloads ECX
from `[6cbee4]` and EAX before reading them; `impact` returns the
constructor's EDX as its own EDX (see "Immediate impact 40bf74"), so EDX
is part of the contract.

### 40b804 `beam_create(O, t, F, T, w)`

cdecl, five slots: O = the object, t = type (only the low word is used;
the caller's slot is `owner high word : type`), F and T = start and end
points (three dwords each), w = a dword stored at O+4Ah (`impact` passes
its owner). Frame of 28h bytes: i -2h (word), j -4h (word), cur -10h,
delta -1Ch, point -28h (three dwords each, x at the lowest address). No
local is read before it is written; no entry register reaches anything
but the first callee.

1. `object_init`(O, 0) (402188) with the entry EAX/EDX/ECX.
2. In this order: `[O]` ← 4987a0; word O+4 ← 3; byte O+41h ← low byte of
   t; dword O+4Eh ← 0; dword `[[6cbee4]]` += 1; `[O]` ← 4987c4; word O+52h
   ← t; dword O+4Ah ← w.
3. s16 t = 1 (JE 40b856) or s16 t = 7 (JE 40b85b): the stepped beam.
   1. d = `dist_gate`(F, T) (49288c) with EAX = T, EDX = F, ECX = 0.
   2. e = (s32 d >> 10) + 1 (arithmetic shift, 32 bits); n = low byte of
      e, byte O+54h ← n; c = low word of 2·s8 n + 2, word O+56h ← c.
   3. A = `0x49667c`(0, 12, s16 c, 1, 4953c4) with EAX = s16 c, EDX = e,
      ECX = `dist_gate`'s ECX. Dword O+58h ← A; byte O+55h ← 1.
   4. cur = F (x, y, z); delta = T (x, y, z); then delta.z −= F.z,
      delta.x −= F.x, delta.y −= F.y (F reread in that order).
   5. `set_length`(&delta, 400h) (4927f0) with EAX = F.y, EDX = &delta,
      ECX = F.x (the values just read).
   6. i = 0; loop while s16 i < s8 [O+54h] + 1 (JL 40b9e7; the byte is
      reread every pass):
      - P = T if s16 i = s8 [O+54h] (the same reread; JNE 40b900), else
        &cur; point = P (x, y, z). j = low word of 2·i.
      - If i ≠ 0 (JE 40b92c): point.x, point.y, point.z (in that order)
        += `rng_next`(4d2599) & 7Fh (492ee0; 0..127). Entry registers of
        the three calls: EAX = point.z, EDX = point.y, ECX = 2·((P &
        FFFF0000h) | i) (32 bits; the low word is j); then EAX = r1 & 7Fh,
        EDX = the first call's EDX, ECX = 4d2599; then EAX = (r2 &
        FFFF007Fh), EDX = r2 & 7Fh, ECX = 4d2599 (r_k = the k-th result's
        EAX).
      - With A1 = [O+58h]: element j+1 at A1 + 12j + 12 ← point (z, x,
        y); with A2 = [O+58h] (reread): element at A2 + 12j ← element
        j+1 at A1 (z, x, y; each read just before its write). The z of
        the element at [O+58h] (third read) + 12j += s16 word
        [[4a9804] + 6·s16 t] (the table pointer reread every pass).
      - cur.z += delta.z, cur.x += delta.x, cur.y += delta.y; i += 1.
   7. Word O+5Dh ← 38h; O+26h ← F (z, x, y).
4. Any other t: byte O+54h ← 1; word O+56h ← 2; A = `0x49667c`(0, 12, 2,
   1, 4953c4) with EAX = 2, EDX = w, ECX = s16 t − 7 (32 bits). Dword
   O+58h ← A; element 0 at A ← F (z, x, y); element 1 at [O+58h]
   (reread) + 12 ← T (z, x, y); byte O+55h ← 0; word O+5Dh ← 38h; O+26h
   ← F (z, x, y).
5. Dword `[[6cbee4]]` += 1 (pointer reread).

Result: EAX = O, EDX = F.y, ECX = F.x (the last reads of F). So a stepped
beam has 2n + 2 elements; elements 2i and 2i+1 (i = 0..n) both hold the
i-th sample point (T for i = n, otherwise F + i·delta with delta of
length 400h), jittered by 0..127 per axis except for i = 0, and the even
one is raised by the type's height word. Only the low byte of e counts:
from d = 127 · 1024 on, e can be 128..255 modulo 256, s8 n is negative,
the count is ≤ 0 and the loop does not run (a wrapped n ≥ 0 gives a
shorter beam that still ends at T). A null A (allocation failure) faults
in the original (no null test); that case is outside the contract.

What it reads: O+54h and O+58h (reread), F, T, `[6cbee4]` (twice),
`[4a9804]` and the table words of types 1/7, and whatever the callees
read. It writes O's fields listed above, the array A and the runtime count
`[[6cbee4]]` (+2 in total). O+5Dh is the word of the timer at O+5Ch (the
`timer_word_step` layout: word at +1), which BEAM's logic slot +14h
`0x40c2a0` steps (`timer_word_step`(O+5Ch); AL = 1 when the word reaches
zero; seven instructions, the hook `beam_done` in
`core/src/sim_leaves.c`). The timer's byte O+5Ch is not
written by the constructor.

**Registers after the call.** The hook returns EAX and EDX exactly; it
returns its entry ECX instead of F.x, which is dead at all three call
sites (each reloads ECX from `[6cbee4]` at once).

Branch sites: 40b856, 40b85b, 40b900, 40b92c, 40b9e7, all reachable both
ways (40b9e7 falls through at once only for a negative s8 n).

### Hooks, registers and validation

Prologue `55 8b ec 83 c4 d8 53 56 57` (relocate 6: 1/2/3; the same bytes
as `explosion_create`). No x87 instruction, no entry shim, no own-assembly
bridge. The adapter gives the core a zeroed host frame of 28h bytes at the
original's offsets (delta at frame base + 0Ch, cur at + 18h, point at +
0). Every callee goes through the weapons guest bridge with the registers
listed above: `object_init`, `dist_gate`, `set_length` and `rng_next` are
hooks; `0x49667c` stays original. Compare mode is
refused (it calls game code and allocates). Callee counters: call_402188,
call_49288c, call_49667c, call_4927f0, call_492ee0.

## Weapon eject 40f57c and the weapon damage slot +68h

Five hooks (classified in
[function_verdicts.md](../reference-game/function_verdicts.md)). Core `core/src/weapon_eject.c`
(`es2/core/weapon_eject.h`), adapter group `weapon_eject`
(`tools/reference-game/proxy/weapon_eject_hooks.h`), in vitro
`invitro/weapon_eject_cases.h`. W is the weapon (equipment object), A
its vehicle.

| Hook | VA | Instr. | Role |
|---|---|---:|---|
| `weapon_eject` | 40f57c | 176 | eject W of A once: mark it, build a debris object at the weapon's mount point |
| `weapon_damage` | 40ee0c | 16 | common slot +68h body: condition 100h on a weapon not yet ejected → `weapon_eject(W, A, 0)` |
| `beam_damage` | 40ee38 | 37 | slot +68h of 498dcc (lasers, EMP, PBEAM, plasma, MAGN) and 4992c0 (ELF) |
| `gun_damage` | 40ee90 | 83 | slot +68h of 498d54 (autocannons, missiles) and 499248 (LAEW) |
| `targ_damage` | 40ef6c | 19 | slot +68h of 498b74 (TARG) |

**Slot +68h (V26).** A byte search for the five VAs in the image finds
40ee38 at 498e34 and 499328, 40ee90 at 498dbc and 4992b0, 40ef6c at
498bdc, and 40ee0c itself at slot +68h of six more equipment vtables
(498a84 ENERGY, 498afc SHIELD, 498bec TURBO, 498c64 ECM, 498cdc, 498e44);
40f57c has no data reference. The only dispatch of the weapon slot +68h
is `0x417fdf` in `vehicle_part` (hook): stack (W, A, x, d) with x = the
weapon part's condition before the damage (dword `[edi]`) and d =
`s16(part_ratio)` after it, EAX = W's vtable, EDX = x, ECX = W.
**Other +68h callers:** `0x41903a` (in `0x418e60`) and `0x46a685`
(`convoy_collide`) call slot +68h of an *actor* (one stack argument, the
actor's vtable), not of a weapon; they never reach these bodies. The live
entry count of 40ee0c is the sum of its direct slot calls and the three
handlers' calls.

**Callers of 40f57c.** `0x40ee2b` (40ee0c), `0x40ef26` (40ee90) and
`0x418a6b` (`mech_part_hit`, a hook, through the guest bridge
with EAX = W, EDX = the weapon scan's stop index, ECX = W, flag 1).

### 40ee0c `weapon_damage(W, A, x, d)`

Frame only (`push ebp; mov ebp,esp`). Compares the dword d with 100h
(`cmpl $0x100,0x14(%ebp)`), then loads EAX = W. d ≠ 100h: returns EAX =
W, EDX = entry EDX. Else EDX = W+43h; byte W+49h ≠ 0 (already ejected):
returns (W, W+43h). Else `weapon_eject(W, A, 0)` with EAX = W, EDX =
W+43h, ECX = A, and returns its EDX:EAX. x is never read.

### 40ee38 `beam_damage(W, A, x, d)`

`weapon_damage(W, A, x, d)` with EAX = x, EDX = A, ECX = entry ECX; then
dword W+7Fh ← 14h. s32 d ≤ 80h: returns `weapon_damage`'s EDX:EAX. Else q
= (d − 80h) / 25 (IDIV, truncating; EDX = the remainder), v = 100·q
(`shl 2`, two `lea ×5`, 32-bit), `multiply_q10(14h, v)` with EAX = v, EDX
= the remainder, ECX = 19h; then dword W+7Fh ← dword W+7Fh − EAX (a
read-modify-write after the call). Returns `multiply_q10`'s EDX:EAX (ECX
as `multiply_q10` left it).

### 40ee90 `gun_damage(W, A, x, d)`

Prologue `push ecx` reserves f−4 (initialized with the entry ECX).
`weapon_damage(W, A, x, d)` with EAX = A, EDX = entry EDX, ECX = entry
ECX. Then:

1. Word W+7Bh = 0: return `weapon_damage`'s EDX:EAX.
2. EDX = `[W+20h]` (the type word at +0). Word 0:
   - s32 d ≤ 80h: return EAX as left, EDX = `[W+20h]`.
   - end = (d − 80h) / 25 → f−4; i = (x − 80h) / 25 (IDIV, x any dword,
     32-bit wrap), i < 0 → i = −1; hit = 0.
   - Loop (40ef0f): EAX = i, i += 1; EAX ≥ end (signed) or hit → leave.
     Else `rng_next(4d2599h)` (stream A) with EAX = the counter, EDX = the
     IDIV remainder or the previous draw's EDX, ECX = 19h or 4d2599h;
     AX &= 3FFh, ECX = 4d2599h (`pop ecx`); `s16(AX) < 12Ch` → hit = 1.
     At most end − max(i, −1) draws; a hit stops the draws.
   - hit: `weapon_eject(W, A, 0)` with EAX = the last counter value, EDX =
     A, ECX = 4d2599h; return its EDX:EAX. No hit: return EAX = the last
     counter value, EDX = the remainder or the last draw's EDX.
3. Else `[W+20h]` is reread; word ≠ 2 or s32 d ≤ 80h: return EAX as left,
   EDX = `[W+20h]`. Else q = (d − 80h) / 25; word W+63h ← 400h − 102·q
   (16-bit); returns EAX = 102·q (32-bit), EDX = q.

ECX at the return is the f−4 dword (`pop ecx`): end on the word-0 path
with d > 80h, otherwise the entry ECX.

### 40ef6c `targ_damage(W, A, x, d)`

`weapon_damage(W, A, x, d)` with EAX = x, EDX = A, ECX = entry ECX; then
word W+7Fh ← d. Returns `weapon_damage`'s EDX:EAX.

### 40f57c `weapon_eject(W, A, flag)`

Frame 84h bytes; EBX = W, EDI = A. Runtime record: `0x49630c` with EAX =
4989e8h (descriptor, −24h at +4: record at f−24h, context word f−14h),
entry EDX/ECX. In order:

1. EDX = EAX = W+43h. Byte W+49h ≠ 0: leave (EAX = EDX = W+43h).
2. Dword W+10h ← 0, byte W+49h ← 1. K = word `[[W+18h]]` (ECX = K,
   zero-extended), word f−26h ← K.
3. Lazy pose matrix: word A+32h = 0 → `euler_matrix(A+0Ch, A+12h)`
   (EAX = A+12h, EDX = A+0Ch, ECX = K; f−2Ch ← A+0Ch), then word A+32h ← 1.
4. f−30h ← A+12h. S = `[A+34h]`, R = `[S+4]`: the node lookup R.V20(R,
   lo(EAX, K)) (EAX = R, EDX = `[R]`, ECX as left; EAX before the load is
   W+43h or `euler_matrix`'s EAX). Result N ≠ 0 and s16 `[N+4]` > −1: M =
   `[S+16h]` + 32·s16(`[N+4]`) (S read once, before the call; `[S+16h]`
   after it); else M = 6c56a8h.
5. `matrix_compose(M, A+12h, f−78h)` (EAX = ECX = M, EDX = A+12h);
   `mount_offset(W)` (registers as left), ECX ← W (`pop ecx`);
   `0x4801d4(mount_offset's EAX, f−78h, f−58h)` (EDX = f−78h, ECX =
   f−58h; the pure transform `matrix_point`, a hook).
6. s8 byte `[[W+18h]+6]` ≥ 4: leave (EAX = `[W+18h]`, EDX =
   `0x4801d4`'s; on its translation-only path, matrix mode byte 18 < 1,
   that is the entry EDX f−78h, a frame address, which under the hook is
   the scratch address; dead at every caller, see below).
7. Word f−32h ← word `[[W+1Ch]+26h]` (EDX = lo(EDX, word)). Word A+32h
   is reread: 0 → `euler_matrix(A+0Ch, A+12h)` again (EAX = A+12h, EDX =
   A+0Ch, ECX = the s8 byte; f−3Ch ← A+0Ch), word A+32h ← 1.
8. A.V24(A) (EAX = `[A]`, EDX/ECX as left), ECX ← A;
   `transform_point(EAX + 14h, A+12h, f−84h)` (EDX = f−84h);
   `aim_angles(f−38h, f−58h, f−84h)` (EAX = f−58h, EDX = f−38h, ECX =
   f−84h), which writes the three words f−38h..f−33h.
9. The flag is compared as a word: 0 → (b, w) = (2, 14h), else (−1, −1).
   `pool_alloc([4a9642h])` (EAX = the pool, EDX/ECX = `aim_angles`'s),
   ECX ← the pool; f−40h and f−4 ← the object O. O = 0: leave (EAX = EDX
   = 0).
10. Word f−14h ← 14h; `debris_init(O, [[4a98f4h] + 4·s16(f−32h)], f−58h,
    b, w)` (EAX = `[4a98f4h]`, EDX = the shape, ECX = O); `[[6cbee4h]]`
    −= 4 (the runtime construction counter); word f−14h ← 8.
11. Dword O+50h ← `[4a9868h]` (read after `debris_init`).
    `debris_heading(O, 4b0h, lo(O, word [[W+18h]+18h]), lo(ECX, word
    f−34h))` (EAX = `[W+18h]`, EDX and ECX = the two pushed dwords; ECX's
    high word is `debris_init`'s).
12. `matrix_angles(f−78h, f−38h)` (EAX = f−78h, EDX = `debris_heading`'s,
    ECX = f−38h), overwriting the aim angles. Dword O+0Ch ← dword f−38h,
    word O+10h ← word f−34h, word O+32h ← 0.

Epilogue: ECX = dword f−24h (the saved FS:0 head), FS:0 ← ECX. Results:
step 1 (W+43h, W+43h), step 6 (`[W+18h]`, `0x4801d4`'s EDX), step 9 (0,
0), else EAX = O+0Ch, EDX = lo(dword f−38h, word f−34h). The flag's high
word is never read. W is dereferenced first (byte W+49h), so W = 0
faults in the original (also a precondition of `mech_part_hit`). An actor whose V24
returns 0 (ACTOR/FLYER body 411a9c) would make `transform_point` read
address 14h; the hook keeps that read.

**Compared state.** W+10h, W+49h; A's lazy matrix A+12h..A+32h (through
`euler_matrix`); the pool `[4a9642h]` and the new object (fields from
`debris_init`, +0Ch..+11h, +32h, +50h, `debris_heading`'s velocity); the
runtime counter `[[6cbee4h]]`; RNG stream A (`debris_init`'s
`rng_bounded`, `debris_heading`'s `rng_next`, 40ee90's chance draws).
For the slot handlers: dword W+7Fh (40ee38), word W+7Fh (40ef6c), word
W+63h (40ee90 type 2).

**Registers after the hooks.** A replaced hook returns its entry ECX.
40f57c returns ECX = the saved head; its callers: 40ee0c returns it to
the slot caller, 40ee90 pops ECX, `mech_part_hit` rewrites ECX. The slot
handlers' EAX, EDX and ECX are all dead at `0x417fdf`: `vehicle_part`
rewrites EAX (`lea eax,[ebx+92h]`), EDX (`0x41801b`) and ECX
(`0x418021`) before reading them, and the only call in between, the
message post `0x420a68(10h)`, writes ECX and EDX before reading them. The
hooks return EDX:EAX exactly all the same.

**Stack and residue.** No frame local of 40f57c is read before it is
written: f−34h is read (step 11) only after `aim_angles` wrote
f−38h..f−33h; f−26h, f−30h, f−32h, f−40h,
f−4 and f−2Ch/f−3Ch are written before their reads; f−78h
(`matrix_compose`), f−58h (`0x4801d4`), f−84h (`transform_point`) are
outputs before inputs. Byte 19 of the matrix f−78h may stay residue on
`matrix_compose`'s copy path (player_movement.md, padding contract) but
`0x4801d4` and `matrix_angles` never read it. 40ee90's f−4 reaches only
the dead ECX. The callees behind the bridge (`0x4801d4`, the node lookups 47622c/476850, the record setup 49630c)
read only their arguments and their own written locals (`aim_angles`
writes f−4/f−8 first). **No entry shim.**

**Implementation.** Every game call goes through the guest bridge with
the registers above. Hooked callees: `euler_matrix`, `matrix_compose`, `mount_offset`, `transform_point`,
`aim_angles`, `pool_alloc`, `debris_init`, `debris_heading`,
`matrix_angles`, `multiply_q10`, `rng_next`, the actor V24 hooks
(`mech_aim_node`, `base_aim_node`) and the group's own hooks
(`weapon_damage` from the handlers, `weapon_eject` from 40ee0c/40ee90);
`matrix_point` 4801d4 is a hook as well; the node lookup virtual is a
virtual call and `0x49630c` runs as original code.
`weapon_eject` keeps its frame in a 256-byte adapter scratch (f = scratch
+ 208); 49630c gets the synthetic EBP (as for `sweep`) and the adapter
restores FS:0 to the saved head (the core hands it out as the original's
ECX). All five refuse compare mode (game calls, allocation).

**Branch sites** (22): 40ee0c: 40ee19, 40ee22; 40ee38: 40ee63; 40ee90:
40eeb4, 40eec1, 40eec9, 40eeee, 40ef08, 40ef15, 40ef19, 40ef1d, 40ef37,
40ef3f; 40f57c: 40f5a1, 40f5c4, 40f5fb, 40f602, 40f64c, 40f665, 40f6c0,
40f6e3, 40f724. All are reachable both ways (40f724 is taken after the
failed allocation and falls through after a successful one; 40ef19 falls
through only when a hit precedes the end of the range).

**Validation.** The standard regime against logging stubs for every
direct game callee, plus 3,072 real-callee cases with the nested hooks in
rotating modes; no branch site proved; nine caught mutations
([in-vitro README](../../tools/reference-game/invitro/README.md),
[MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).

## Bullet constructor 40b43c, 40af6c and 40ffc8

Ranges `0x40b43c`..`0x40b59d`, `0x40af6c`..`0x40afef` and
`0x40ffc8`..`0x40fff7`; `0x40adc0` was read for the callee contract. The
core is appended to `core/src/weapons.c`
(`es2/core/weapons.h`, unit test `core/tests/test_bullet.c`). The adapter group
is `bullet_ctor` (`tools/reference-game/proxy/bullet_ctor_hooks.h`) and the
in-vitro cases are in `invitro/bullet_ctor_cases.h`.

Notation: O is the new bullet object, D its bullet definition (the result of
bullet_def) and R its lookup record (`[O+4Eh]`).

| Hook | VA | Instr. | Role |
|---|---|---:|---|
| `bullet_create` | 40b43c | 117 | `BULLETS` constructor: a pool object, the angular spread from RNG stream A, the position, the owner and the sound |
| `bullet_init` | 40af6c | 47 | the object part: `object_init`, the vtables 4987a0 and then 498628, the lookup record and word +59h |
| `def_lookup` | 40ffc8 | 20 | the first record of `[4a98fch]` (stride 24h) with the given category and type words |

**Callers.** These come from a raw call scan; none of the three has a data
reference.

- `0x40b43c` is called by these hooks through their bridges: `turret` at
  `0x404557` and `0x4045a3`, `weapon_fire` at `0x40e9e2`, `emit` at `0x40ec54`
  and `target_emit` at `0x4229b1` and `0x4229ff`. It is also called by the
  wrapper `0x40b5a0` (hook `bullet_charge`) at `0x40b5b5`; `emit` calls that wrapper three
  times.
- `0x40af6c` is called only at `0x40b474`.
- `0x40ffc8` is called at:
  - `0x40a9a8`, in the projectile constructor `0x40a948` (hook `rocket_init`, category 0);
  - `0x40afc7` (category 2);
  - `0x40bfb5` in `impact` (category 4; bridged);
  - `0x4100be` in the weapon factory (category 0);
  - `0x40ac8f` (category 3), inside `0x40ac3c`, which has no call and no
    pointer reference.

### 40ffc8 `def_lookup(c, t)`

EAX = `[4a98fch]` and EDX = 0. The loop runs while s16(DX) < s16 of the word
`[4a9900h]`. The count is reread for every test (JL at `0x40fff1`).

- If word `[EAX]` equals the low word of c (`0x40ffde`) and word `[EAX+2]`
  equals the low word of t (`0x40ffe4`), it returns EAX = the record and
  EDX = its index.
- Otherwise EDX += 1 and EAX += 24h.

If no record matches, it returns EAX = 0 and EDX = the final index (0 for a
count ≤ 0). The function writes nothing.

The original returns ECX = c (the full dword); the hook returns its entry ECX
instead. ECX is dead after every live call site:

- `0x40a9b3`, `0x40bfba` and `0x4100de` write ECX;
- after `0x40afcc`, bullet_def ignores its entry ECX and `0x40afdc` pops ECX.

Only `0x40ac8f` would pass ECX on, and it is unreferenced code.

Precondition: the records up to the hit, or up to the count, are readable.

### 40af6c `bullet_init(O, t)`

1. ECX = `[4a96f8h]`, the bullet definition table of 14-byte records.
   EAX = s16(t) and EDX = 7·EAX. EAX = s16 of the word `[ECX + 2·EDX]`; this is
   word +0 of definition s16(t), a shape index. EDX = `[4a9700h]` and
   EAX = `[EDX + 4·EAX]`, the shape.
2. `object_init(O, shape)` (402188), called with exactly those EAX/EDX/ECX.
3. In this order: `[O]` ← 4987a0, word O+4 ← 3, byte O+41h ← the low byte of t,
   dword O+4Eh ← 0, `[[6cbee4h]]` += 1, `[O]` ← 498628.
4. `def_lookup(2, t)` with EAX = the counter pointer, EDX = t and ECX = 0.
   Dword O+4Eh ← its EAX.
5. `bullet_def(O)` (40adc0) with def_lookup's EAX/EDX/ECX. Word O+59h ← the
   word `[its EAX + 6]`.
6. `[[6cbee4h]]` += 1 (the pointer is reread).

It returns EAX = O and EDX = the counter pointer. The original's ECX is O
(popped); the hook returns its entry ECX instead. That ECX is dead, because the
only caller loads ECX from `[6cbee4h]` at once.

From the prologue on, EBX = O and ESI = EDI = t, so no caller register reaches
a callee. There is no branch site.

### 40b43c `bullet_create(t, P, A, W)`

The function is cdecl with four slots:

- t: the type (only the low word is used);
- P: the position (three dwords);
- A: the angles (three words);
- W: the owner.

The frame is 2Ch bytes:

- the angles copy at f−2Ch, f−2Ah and f−28h (words);
- the runtime record at f−24h (descriptor 498549h, offset −24h), with its
  context word at f−14h;
- O at f−4;
- the saves at f−30h, f−34h and f−38h (EBX, ESI, EDI); the outgoing ESP is
  f−38h.

f−26h and f−0Eh..f−9 are never written. EBX, ESI and EDI keep the caller's
values until the function first uses them (see the steps below).

1. `0x49630c` is called with EAX = 498549h, the entry EDX/ECX and the caller's
   EBX/ESI/EDI. It links the record into FS:0 and returns the old head in EAX.
2. `pool_alloc([4a96c2h])` with EDX = the pool and EAX/ECX as 49630c left them.
   ECX = the pool (popped). EBX = O, stored to f−4.
3. If O ≠ 0 (`0x40b463`):
   - context word ← 14h;
   - `bullet_init(O, lo(O, t))` with EAX = lo(O, t), EDX = O (reread from f−4)
     and ECX = the pool;
   - `[[6cbee4h]]` −= 5 (ECX = the counter pointer);
   - context word ← 8.

   If O is null, the code reaches `bullet_def(0)`, which faults reading byte
   `[41h]`. This is a precondition, and the taken edge of `0x40b463` is PROVED
   impossible.
4. EBX = `[f−4]`. `bullet_def(O)` is called with EAX = O and the EDX/ECX of
   step 3 (bullet_init's EDX and the counter pointer). ESI = D. ECX = O
   (popped).
5. The angles copy, in this order:
   - word f−2Ch ← `[A]` (EDX = lo(EDX, ·));
   - word f−2Ah ← `[A+2]` (ECX = lo(O, ·));
   - word f−28h ← `[A+4]` (EAX = lo(A, ·)).
6. **Spread: two stream A draws** (see rng.md).
   - First draw: EDI = 2·lo(EDI, `[D+0Ah]`) (32 bits). `rng_next(4d2599h)`
     (492ee0) is called with the step-5 registers. Then DI = (DI AND AX) −
     `[D+0Ah]` (16 bits; the word is reread after the call). ECX = 4d2599h
     (popped). Word f−2Ch += DI.
   - Second draw: the same again, with `[D+0Ah]` reread twice, the first draw's
     EAX/EDX and ECX = 4d2599h. Word f−28h += DI.

   With w = `[D+0Ah]`, each draw adds (r AND 2w) − w to one angle word. f−2Ah
   does not change. EDI keeps the caller's high word (doubled, with the carry)
   and reaches every later callee.
7. In this order: O+2Eh ← `[P+8]`, O+26h ← `[P]`, O+2Ah ← `[P+4]`; dword
   O+0Ch ← dword f−2Ch, word O+10h ← f−28h, word O+32h ← 0; O+4Ah ← W. Then
   `0x49681a(O+42h, 0, 8)` (the memset thunk) is called with EAX = O+42h,
   EDX = lo(dword f−2Ch, f−28h) and ECX = `[P]`.
8. Words O+56h and O+54h ← 0 (EDX = 0). W's virtual +38h is called with the
   argument W and EAX = W, EDX = 0, ECX = W's vtable. ECX = W (popped). Then
   word O+52h ← AX + word `[R+0Ah]`, where R = `[O+4Eh]` is read after the
   call.
9. Dword O+5Bh ← 0 (EAX = 0) and word O+5Fh ← 400h. If word `[D+0Ch]` ≠ 0
   (`0x40b561`): ECX = s16 `[D+2]`, 400h is divided by it with IDIV (after
   CDQ) and word O+61h ← the quotient. A zero divisor faults (precondition).
10. `sfx_play_at(s16 [D+8] + 0Ah, P)` (4628e0) is called with EAX = that index,
    EDX = s16 `[D+8]` and ECX = P. It draws from stream B in its own order.
11. It returns EAX = O and EDX = the saved head `[f−24h]`, and sets FS:0 ← EDX.
    ECX is sfx_play_at's ECX.

**RNG.** Each bullet makes exactly two `rng_next` draws from stream A, before
any other callee that draws. sfx_play_at's stream B draw stays inside the
bridged call.

**Reads:**

- `[4a96c2h]`;
- `[6cbee4h]` (twice);
- A;
- D: +0Ah four times, then +0Ch, +2 and +8;
- `[O+4Eh]` and R+0Ah;
- P;
- W's vtable.

**Writes:** the frame locals above, O's fields, `[[6cbee4h]]` and FS:0
(through 49630c and the epilogue).

**Branch sites:**

- 40b43c: `0x40b463` is PROVED one-way; `0x40b561` goes both ways;
- 40ffc8: `0x40ffde`, `0x40ffe4` and `0x40fff1` go both ways.

### Registers, stack and residue

**After the calls.** Every caller of 40b43c writes the flags before it reads
them.

- EAX: `0x40b5a0` stores through it, and `weapon_fire` and `emit` may return
  it.
- EDX (the saved head) and ECX (sfx_play_at's) reach the returns of `turret`,
  `emit`, `target_emit` and 40b5a0. 40b5a0 replaces only CX.

When it is entered through its shim (below), the hook returns all three
registers exactly. bullet_init's EDX reaches bullet_def's entry and is exact.
The ECX of bullet_init and of def_lookup is dead (see above).

**Entry shim of bullet_create.** It uses the `projectile_create` technique
(lc_base_hooks.h):

- The shim keeps the original's EBP and stores the caller's EBX/ESI/EDI in the
  original's save slots.
- It reserves a guard-probed gap of 24 pages (96 KiB). The gap holds the
  callees' subtree, including the 64 KiB comparison storage of a nested hook in
  compare mode. aim_fire's 256 KiB gap still holds the emitters' 128 KiB, their
  host frames and this gap.
- It hands {EBP, f−38h} to the adapter.

The core writes every frame local at the original's offset, and 49630c links
the record at the original f−24h. Every game call runs at the original
outgoing ESP with the original's EBX/ESI/EDI/EBP and return address. So the
frame and the callees' area below it hold the bytes the original leaves there,
apart from the framework bytes of nested replaced hooks (as everywhere).

After the framework's dispatch, the shim loads the core's ECX. Without the shim
(direct and legacy calls), the adapter uses the plain bridge and gives 49630c a
synthetic EBP in its scratch (`weapons_impact_guest_call`).

**bullet_init and def_lookup have no shim.** bullet_init's entry is
bullet_create's outgoing ESP−8. Below it, a replaced bullet_init writes the
framework's host frames and its own, where the original writes its saved
registers and `object_init`'s subtree. A replaced def_lookup does the same
below its own entry. Later callees of bullet_create (bullet_def, rng_next, the
memset thunk, the owner's virtual and sfx_play_at's subtree) overwrite part of
that region.

The known readers of stale stack near this region are `flyer_steer`'s pose
bytes below `target_emit` (flyer.md, "State method stack residue";
`0x40b43c` reaches 284h below target_emit's entry). There, a replaced
sfx_play_at already writes host frames. Parity of those bytes is not
claimed (open).

**Uninitialized reads.** There are none in the three functions. Every frame
local is written before it is read, and the never-written f−26h and
f−0Eh..f−9 are not read.

### Hooks, registers and validation

The prologues:

| Function | Prologue bytes | Relocate (instruction lengths) |
|---|---|---|
| 40b43c | `55 8b ec 83 c4 d4 b8 49 85 49 00` | 6 (1/2/3) |
| 40af6c | `55 8b ec 53 56 57 8b 0d f8 96 4a 00` | 5 (1/2/1/1) |
| 40ffc8 | `55 8b ec 53 a1 fc 98 4a 00` | 9 (1/2/1/5) |

Every game callee goes through the weapons guest bridge with the registers
above (`pool_alloc`, `object_init`, `bullet_def`, `rng_next`, `sfx_play_at`
and the group's own hooks are hooks; 49630c and the memset thunk run as
original code). All three hooks refuse compare mode; def_lookup is a leaf and
refuses it for uniformity.

Callee counters:

- bullet_create: call_49630c, call_471b30, call_40af6c, call_40adc0,
  call_492ee0 (two per call), call_49681a, slot_38, call_4628e0;
- bullet_init: call_402188, call_40ffc8, call_40adc0.

**In vitro** (`differential_bullet_ctor_test`), standard regime against
logging stubs for every direct callee. The stubs change the spread word
around each draw, after the virtual the lookup pointer, the division flag,
the divisor and the sound word, and the counter pointer. def_lookup runs
over random tables with the counts −1, 0, 1 … 16, 8000h, FFFFh, and 7FFFh
with a planted hit. bullet_create's installed cases also compare ECX, the
EBX/ESI/EDI/EBP/ESP and stack arguments at every callee entry, and 80h
bytes below the argument slots. Real-callee cases: 3,072 for bullet_create
(pool_alloc, bullet_init, rng_next, object_init, def_lookup and bullet_def
nested in rotating modes, compare included) and 3,072 for bullet_init
(object_init, def_lookup and bullet_def nested). One branch site PROVED;
five mutations caught (MUTATIONS.md).

`def_lookup` is entered live from bullet_init, impact, 40a948 and the
weapon factory (at mission load).

## Player weapon system

Twelve functions run as own code behind the
hooks of the adapter group `weapon_slots` (`core/src/weapon_slots.c`,
`tools/reference-game/proxy/weapon_slots_hooks.h`): the per-frame step of
the player's weapon system 410b40 with its helpers, and the range gauge it
mirrors.

Objects. W is the weapon system
([above](#weapon-system-routines-below-the-weapon-keys)): dword W+4 the slot list, word W+8
the count, bytes W+14h (latch, set from the gauge lamp), W+16h (group
changed by a key), W+18h (selection made by a key or panel), words at W+0Ah
(indexed by the weapon virtual +60h result), group byte W+1Ch, selection
s8 W+1Dh, group records at W+1Fh+6g, dword W+31h (the second argument of
410b40). A weapon E: vtable `[E]`, definition D = `[E+18h]` with s8 D+16h
(the slot offset of its partner, "link"), carrier `[E+1Ch]` with word +56h,
bytes E+49h, E+4Bh (linked), E+4Ch. H is the range gauge
(`WeaponRangeGauge`, vtable 49C85Ch, constructor 441df8): four button
gadgets at H+18h..H+24h, the dword count H+68h (= 4), word H+B1h (the fire
group it shows), bytes H+B3h (lamp) and H+B4h (link). X = `[49B088h]` is
the HUD root: `[X+1E1h]` = H, `[[X+203h]+202h]` = the player's W.

### 410b40 `weapon_frame(W, x)`

cdecl, frame 18h, called once per rendered frame from 41b1cf (`0x41b130`).
EBX = W throughout; ESI and EDI are the caller's until 410b85 / the loop.

1. Word 4d2528 ≠ 0 and word 4d2526 = 0 (both read before anything else):
   `select_step(W, 1)` (41074c) with the entry EAX/EDX/ECX, then
   EAX = W+14h, byte W+18h ← 0.
2. EDX = x, dword W+31h ← x; `0x410a3c(W)`; POP ECX.
3. H = `[[49B088h]+1E1h]` (ESI); `0x442120(H)` with ECX = `[49B088h]`;
   POP ECX = H. The record R (local EBP−8) ← the dword at 442120's EAX,
   byte EBP−4 ← the byte after it (4d1a60 for the real 442120; never read).
4. EAX = EDX = W+14h. Byte W+16h ≠ 0: byte W+16h ← 0, word R ←
   s8 W+1Ch (ECX). Else `fire_group(W, (W+14h)&FFFF0000h | word R)`
   (410ae4).
5. Byte R+2 ≠ 0: `0x410f04(W)`, POP ECX = W; else EAX = W+14h, byte W+14h
   ← 0. Byte R+3 ≠ 0: `0x410f14(W)`, POP ECX = W. Byte R+3 ← 0.
6. `0x4420c8(H, &R)` with EDX = &R.
7. For i = 0 while s16 i < s16 word W+8 (reread; ESI = i): EAX = i,
   EDX = `[W+4]`, E = `[EDX+4i]` (EDI); E = 0: next. ECX = s8 W+1Dh,
   `selected` = (i = ECX). EAX = E+43h. Byte E+4Bh ≠ 0: EAX = s8 D+16h,
   L = `[[W+4] + 4·s16(EAX+i)]`, EDX = ECX = E+43h; if byte E+49h ≠ 0 or
   byte E+4Ch = 0: byte E+4Bh ^= 1, EDX = L+43h, byte L+4Bh ^= 1,
   `0x410708(W, EAX+i)` (EAX = that slot); else EDX = i + EAX,
   ECX = s8 W+1Dh, `selected` ← 1 when they are equal.
   Then `ready = 0x410970(W, E, i, 0)`; EDX = s8 W+1Ch, ECX = 3·EDX,
   EDX = T = `[W + 2·ECX + 1Fh]`, EAX = i : word T[i];
   CX ← `selected`; `E.V50(E, ECX, EAX, ready)` with EAX = `[E]`.

Result: the last call's or the loop's registers; all three are dead at the
only caller (41b1d7 loads ECX and EAX; EDX reaches `0x447df8`, which only
reads its slot, and 41b1ea overwrites it).

### 410a3c `ready_step(W)`

cdecl; `push ecx` reserves the dword EBP−4 (the entry ECX). AL = byte
W+1Dh; s8 + 1 ≤ 0: return EAX = entry EAX & FFFFFF00h | sel,
EDX = s8 sel + 1, ECX = entry ECX. Else EDI = s8 sel,
E = `[[W+4] + 4·sel]` (ESI), word EBP−2 ← count = word W+8, word EBP−4
← k = 0, and `0x410a04(W, E, s8 W+1Dh)` with EAX = count, EDX = the
reread s8 W+1Dh, ECX = the entry ECX. AX ≠ 0: return. Else loop:
`select_step(W, 1)`; EAX = EDX = s8 W+1Dh (reread), ECX = `[W+4]`,
E = `[ECX+4·EAX]`; `0x410a04(W, E, EAX)`; AX ≠ 0: EAX = W+14h, byte W+18h
← 0, return. Else EDX = s8 W+1Dh, ECX = EDI; equal: leave the loop. Else
AX ← k, k += 1; old k ≤ count (signed words): loop again. After the loop:
EDX = EAX = EDI, byte W+1Dh ← DL, `0x410708(W, EDI)` with ECX = EDI.
ECX at return: the dword EBP−4 (count : k), or the entry ECX on the
early return. 41074c gets d = 1 every time, so its "below 0" path (the
only reader of the caller's ESI) is not reached.

### 410a04 `selected_ready(W, E, i)`

cdecl, no local. Byte W+18h ≠ 0 or word 4d2526 ≠ 0: EAX = (W & FFFF0000h)
| 1, EDX = W+14h, ECX kept. Else `0x410970(W, E, ECX, 0)` with
CX ← word i (the **entry ECX's high word stays** and is pushed as the
index slot), EAX = W, EDX = E; its registers are returned.

### 410970 `chain_ready(W, E, i, f)`

cdecl; EBX = W, ESI = E, EDI = i (the whole slot).

1. `E.V2C(E)` with EAX = `[E]`, the entry EDX and ECX; POP ECX = E.
   AX = 0: EAX = 0.
2. Dword W+31h ≠ 0: `weapon_range(E, W+31h)` (40e5f8) with EDX = W+31h;
   AX = 0: EAX = 0.
3. `E.V60(E, 0)` with ECX = `[E]` (read again). AX ≠ 5 and ≠ 3:
   EAX = s16 AX; word `[W + 2·EAX + 0Ah]` = 0: EAX = 0.
4. Word f ≠ 0: AX ← 1. Else EAX = E+43h; byte E+4Bh = 0: AX ← 1. Else
   EDX = `[E+18h]`, ESI = `[W+4]`, EAX = s8 D+16h, EDX = EAX + i (32 bit),
   ECX = s16 DX, L = `[ESI + 4·ECX]`, and the result is
   `0x410970(W, L, EDX, 1)` (EBX = W, ESI = L, EDI = i), which returns at
   step 4 (f = 1) or earlier, so the recursion is one level deep.

EDX and ECX at return are the last call's (V2C, 40e5f8, V60 or the
recursion), ECX = E after step 1.

### 410f04 `latch_set(W)` and 410f14 `link_toggle(W)`

410f04: EAX = W+14h, byte W+14h ← 1; EDX and ECX kept.

410f14 (EBX saved): DL = byte W+1Dh (EDX keeps its high 24 bits),
ECX = s8 + 1 ≤ 0: return EAX = W, ECX = s8 sel + 1. Else E =
`[[W+4] + 4·sel]` (EDX), ECX = s8 D+16h; 0: return (EAX = W, ECX = 0).
ECX = s16(CX + sel), L = `[[W+4] + 4·ECX]` (EAX, list pointer read
again); 0: return. Word `[[E+1Ch]+56h]` ≠ word `[[L+1Ch]+56h]`: return
(ECX = 0). Else EDX = E+43h, EAX = L+43h, byte E+4Bh ^= 1, byte L+4Bh ^=
1, ECX = 1. A null selected weapon faults (outside the callers' domain).

### 410cd0 `member_toggle(W, i)`

The membership word of slot s16 i in the current group's table
T = `[W + 1Fh + 6·s8 W+1Ch]` gets bit 0 flipped. EAX = W, EDX = s16 i,
ECX = (s16 i & FFFF0000h) | the new word. Only caller 4110df
(`group_toggle`); its ECX is the entry ECX of the weapon virtual +48h.

### 4420c8 `gauge_sync(H, R)`, 442120 `gauge_state(H)`, 442054 `gauge_link(H, b)`, 441f90 `gauge_mode(H, m)`

4420c8 (EBX = H, ESI = R): AX ← word R (EAX keeps the entry high word);
≠ word H+B1h: word H+B1h ← AX, `0x441f90(H, EAX)` (the whole EAX pushed).
DL ← byte H+B3h; ≠ byte R+2: the lamp `0x441fa4(H)` (not hooked, a guest call;
toggles H+B3h, calls the lamp gadget's virtual +4 and sends the EventPort
messages 26h/27h through 435ac8), POP ECX = H. AL ← byte R+3; ≠ byte
H+B4h (read after the lamp): EDX = AL zero-extended,
`0x442054(H, EDX)`. Registers at return: the last values.

442120: word 4d1a5c ← word H+B1h (EDX keeps its high word), byte 4d1a5e ←
byte H+B3h (ECX keeps its high 24 bits), byte 4d1a5f ← byte H+B4h;
EAX = 4d1a5c.

442054: byte H+B4h ← byte b (EDX keeps its high 24 bits),
`0x410f14([[[49B088h]+203h]+202h])` with EAX = that W, ECX = `[X+203h]`;
POP ECX = W.

441f90: word H+B1h ← DX = word m; EAX = H, EDX keeps its high word, ECX
kept.

### 442154 `gauge_button(H, x)`

Slot 0 of the gauge's vtable 49C85Ch (no direct caller; the MFD gadget
dispatch calls it, so its EAX/EDX/ECX are returned exactly). EBX = x,
ECX = H, EAX = k = 0, EDX = H+18h; while k < dword H+68h (signed, reread):
x = `[EDX]` (32 bit) selects button k, else k += 1, EDX += 4. Button 0:
word H+B1h += 1, and ← 0 when it is then 3; `0x441f90(H, FFFF0000h | word
H+B1h)` (EAX after `sub eax,1` from 0). Button 1: `0x442054(H, 1)` with
EAX = 0. Button 2: the lamp `0x441fa4(H)` with EAX = 0, POP ECX = H.
Buttons 3 and above: EAX = k − 2, nothing called. No match: EAX = k.

### Callers, registers and residue

The callers named in the sections above are the ones whose registers matter.
No dword in the image points at any of them except 442154 (byte search for
the little-endian VAs). Every function returns EAX, EDX and ECX exactly
(ECX through exit shims, below). The only callee-saved register reads
found below these calls are select_step's ESI high word (step "below 0",
never with d = 1) and the weapon virtual +2Ch body 40ecdc (class 498dcc),
which loads BX (EBX = W) and CX (ECX = the entry ECX of 410970) partially
and returns their high words in EDX; that EDX is dead behind 410970's
callers (410a04 → 410a3c → select_step's entry EDX or 410b40, where 442120
keeps only its high word until 410b92 overwrites EDX). The core still
passes the original's EBX/ESI/EDI to every call.

Two register differences of already registered hooks reach these
functions; both are dead:

- select_step's return ECX is `found` : entry CX. Through 410a3c
  (`push ecx`) and 410a04 (`mov cx`) its high word becomes the high word
  of 410970's index slot, of the ECX/EDI at V2C/V60/weapon_range and of
  the recursion's index (EDX at its V2C). It is dead at 410b68
  and further on: the V2C bodies (40ecd4, 40ecdc, 40ed6c,
  40eda0) and the V60 bodies (40e62c, 40e644) compare only words, and the
  partial values end in EDX/ECX as above. With select_step replaced the
  framework returns its entry ECX, so these high words differ.
- fire_group's return ECX (dead at 410bc3, see
  [Callers and register liveness](#callers-and-register-liveness)) reaches 4420c8 and its
  lamp call as their entry ECX when the record bytes R+2/R+3 are 0; the
  lamp gadget's virtual +4 (455540: byte +1Fh ← 2; 455575: empty) and
  435ac8 (writes EDX/ECX first) do not read it, and 410b40's return ECX is
  dead.

The gauge record R is a stack local of 410b40; its address is pushed to
4420c8 and is in EDX there, ESI is R inside 4420c8, and EDX keeps R's high
24 bits after `mov dl` (to the lamp and to 410b40's return when the slot
loop does not run). The replacement places R in its own scratch; these
values are dead (the lamp's callees above, 410b40's caller). No function
reads an uninitialized local (410b40 writes EBP−8..−5 before reading them
and never reads EBP−4..−1; 410a3c's EBP−4 is the pushed ECX).

Write set: W+14h, W+16h, W+18h, W+1Dh, dword W+31h, link bytes E+4Bh,
membership words, the gauge's H+B1h and H+B4h, the copy 4d1a5c..4d1a5f;
through nested hooks also W+1Ch, word 4d2528 (weapon_select) and H+B3h
(the lamp, original).

### Hooks

Twelve cdecl hooks, `ES2_RESULT_PAIR` through the context replacement,
compare refused except `latch_set` and `gauge_mode` (spans W+14h and
H+0B1h; [compare_mode.md](../reference-game/compare_mode.md#mission-load-weapon-slot-and-weapon-class-leaves)). Prologues (relocated bytes): 410b40 `55 8b ec 83 c4 e8`
(6); 410a3c, 410970, 4420c8, 410cd0 `55 8b ec` + two pushes (5); 410a04,
410f04, 442120, 442054, 441f90 `55 8b ec 8b 45 08` (6); 410f14
`55 8b ec 53 8b 45 08` and 442154 `55 8b ec 53 8b 5d 0c` (7). No branch
targets the relocated bytes. Game calls go through the callee observer and
this group's bridge (EAX/EDX/ECX plus EBX/ESI/EDI from the core), so every
nested hook (select_step, fire_group, weapon_select, weapon_range and the
group's own hooks) is entered in its own mode. The weapon virtuals +2Ch/+50h/+60h
(readiness and display bodies of the weapon classes) and the lamp 441fa4
(not hooked) stay guest calls.

**Exit shims** for the ten hooks whose ECX differs from the entry ECX (all
but latch_set and gauge_mode): the shim records the caller's EBX/ESI/EDI
(the adapter reads them first and passes them on where the original does),
calls the framework's dispatch with the arguments copied and, when the
replacement ran, loads the ECX the core computed.

**Bridged calls of weapon_system.** `fire_group` calls 441f90 and
`group_toggle` 410cd0 through the guest bridge, so those calls land in
`gauge_mode` and `member_toggle` in their own modes (counters
`hk_fire_group_call_441f90` and `hk_group_toggle_call_410cd0`). The
weapon_system core does not call these cores natively: that would tie the
callees' modes to their callers' and bypass the exit shims.

Callee counters: weapon_frame call_41074c, call_410a3c, call_442120,
call_410ae4, call_410f04, call_410f14, call_4420c8, call_410708,
call_410970, slot_50; ready_step call_410a04, call_41074c, call_410708;
selected_ready call_410970; chain_ready slot_2c, call_40e5f8, slot_60,
call_410970; gauge_sync call_441f90, call_441fa4, call_442054; gauge_link
call_410f14; gauge_button call_441f90, call_442054, call_441fa4.

### Validation (in vitro)

`differential_weapon_slots_test` (two shards), standard regime:

- **Direct cases** run against logging stubs for the hook's callees (the
  game functions patched in the private image, the weapon virtuals through
  arena vtables; chain_ready's recursion runs real). Each stub logs its
  arguments, the bridge's slot count and EAX/EDX/ECX/EBX/ESI/EDI, and
  changes what the originals read again (W+31h, vtables, link bytes, the
  list pointer, the count, the selection, the group, H+B4h). Also compared:
  the log, the globals 49b088, 4d1a5c..63, 4d2520..2f, and the callee
  counter deltas for the first 4,096 cases. The record's address is logged
  as its contents at 4420c8 and the lamp; stack-range register values
  compare by their low byte. Installed cases run through the exit shims.
- **Real-callee cases:** 2,048 of 410b40 over the real 410a3c, 410a04,
  410970, 442120, 410f04, 410f14, 4420c8, 442054, 441f90, 41074c, 410ae4
  and 410708 with the thirteen hooks in random modes (all original and all
  replaced included); 1,024 of 442154 over 441f90, 442054 and 410f14; 1,024
  of the weapon_system `group_toggle` 4110ac over 410cd0. Stubs remain for
  the virtuals, weapon_range and the lamp. Because of the two dead
  differences above, these passes compare only the low words of ECX/EDI
  (and the V2C's EDX) at the calls below 410970, and not 410b40's return
  ECX and the lamp's ECX when fire_group is replaced; group_toggle's
  virtual calls are logged without EBX/ESI/EDI (its replaced bridge does
  not load them).
- **Branch sites:** 39 (410b40 11, 410a3c 5, 410a04 2, 410970 8, 410f14
  4, 4420c8 3, 442154 6), all both ways; one mutation per hook
  (MUTATIONS.md).

**Harness.** The original reads no uninitialised byte of the stack gauge
record: 410b40 writes EBP−8..−5 (dword 410b94) and EBP−4 (byte 410b9a)
before 410bc6/410bdb read EBP−6 and EBP−5, and 4420c8 reads only R, R+2
and R+3 (4420d3, 4420f6, 442102), so R+4 is never read; the other locals
(EBP−0Ah, −10h, −14h, −18h) are stored before each use. The log of
synthetic calls holds 512 records (a 410b40 case with all 24 slots live
logs about 200). Register values inside the thread's whole stack
reservation (taken once from the allocation of the current stack pointer)
count as stack values and compare by their low byte; a random register
that falls into it is replaced by 0, so the case sequence does not depend
on where the stack lies. Two diagnostic environment variables exist for
hunting rare states: `ES2_INVITRO_WEAPON_SLOTS_SEED` (xored into the fixed
seed) and `ES2_INVITRO_WEAPON_SLOTS_REAL` (multiplies the real-callee case
counts).

**Coverage.** `member_toggle` and `gauge_mode` are reached in the live
regression only by `c1_01-keys` (Alt+1/Alt+2 through `group_toggle`).

## Weapon and projectile helpers

Entry-aligned from each VA to its `ret`; the callers read at every call
site. A raw scan of the file finds no data reference (dword) to any of the seven entries, so
they are reached only by the direct calls listed below. The cores are
appended to `core/src/weapons.c` (`es2/core/weapons.h`, unit test
`core/tests/test_weapon_leaves.c`); the adapter group is `weapon_leaves`
(`tools/reference-game/proxy/weapon_leaves_hooks.h`), the in-vitro cases
are in `invitro/weapon_leaves_cases.h`.

| Function | VA | Instr. | Hook | Role |
|---|---|---:|---|---|
| hit-accumulator clear | 427d98 | 3 | none (in `sweep`'s core) | dword `[4aaad0h]` ← 0 |
| hit-accumulator read | 427da0 | 2 | none (in `sweep`'s core) | EAX ← `[4aaad0h]` |
| `PROJ` record by index | 40ffb0 | 8 | `proj_record` | `[4a98fch]` + 24h·s16(i) |
| pitch toward a point | 49295c | 24 | `pitch_point` | `vector_angle(dist2d_delta(P−Q), Pz−Qz)` |
| default damage words | 40501c | 8 | `damage_words` | words `[4a95f2h]`, `[4a95f4h]` |
| low-charge bullet | 40b5a0 | 24 | `bullet_charge` | `bullet_create` plus the charge word and the lock target |
| rocket constructor | 40a948 | 44 | `rocket_init` | the category-0 projectile object part |

### 427d98 and 427da0 (the hit accumulator of `sweep`)

`427d98` is `xor eax,eax; mov [4aaad0h],eax; ret`: EAX = 0, the dword
`[4aaad0h]` = 0, EDX and ECX unchanged. `427da0` is `mov eax,[4aaad0h]; ret`:
EAX = the dword, nothing else changes. Their only callers are in `sweep`
(426528): 427d98 at `0x426650` and `0x426694`, 427da0 at `0x42668a` (stored
to `[4aaa6ch]`) and `0x42684b` (stored to `[4aaa68h]`). The accumulator is
lowered by `0x427da8` (`0x427e2c`..`0x427e34` keep the smaller distance,
`0x427e47` clears it), which the slot +20h hit tests reach (`0x404c80`,
`0x4050b9`).

`sweep`'s core does both itself (the write and the read through
the guest memory, EAX as above) instead of calling them through the guest
bridge. The callee counters `hk_sweep_call_427d98` and
`hk_sweep_call_427da0` are kept: the core still reports each of the four
sites to the observer (two each per call); it does not call the
originals. `sweep`'s in-vitro cases patch both leaves with
counting stubs that do exactly what the leaves do (no log entry, EDX and
ECX kept), and the slot +20h stubs sometimes write the accumulator, so the
stored `[4aaa68h]`/`[4aaa6ch]` values depend on the reads.

### 40ffb0 `proj_record(i)`

`movsx eax,word [ebp+8]; shl eax,2; lea eax,[eax+8·eax]; add eax,[4a98fch]`:
EAX = `[4a98fch]` + 24h·s16(i) (modulo 2³²), the record of the `PROJ` lookup
table that `def_lookup` (40ffc8) walks. EDX and ECX are not touched; the
hook returns the entry EDX in EDX and the framework restores the entry ECX,
so all three registers are exact. Nothing is written.

Callers:

- `turret` at `0x404315` (i = 2; then `movsx edx,[eax+0Ah]; pop ecx`);
- the weapon factory at `0x4100d2` (i = the slot's selection word in EAX;
  then `pop ecx`; inside the hook `weapon_factory` 40fff8, reached at
  mission load);
- `target_emit` at `0x422735` (i = 2; then `mov ax,[eax+0Ah]; pop ecx`).

Prologue `55 8b ec 0f bf 45 08`, relocated 7 bytes (1/2/4).
Compare-capable: a pure reader of one global.

### 49295c `pitch_point(P, Q)`

EBX = P and ESI = Q (both saved). EAX = `[P+8]−[Q+8]` (dz, pushed first),
EDX = `[P+4]−[Q+4]` (dy), ECX = `[P]−[Q]` (dx); `dist2d_delta` (492a9a) is
called with the three slots (dx, dy, dz) and exactly those EAX/EDX/ECX.
`add esp,8` leaves the dz slot and `push eax` adds the distance, so
`vector_angle` (49290c) is called as (distance, dz) with dist2d_delta's
EAX/EDX/ECX. The function returns vector_angle's EAX and EDX (and its ECX,
see below). Nothing is written.

Both callers are in `target_emit` (bridged by its core): `0x422818` and
`0x4228b5`, each with P = the frame point EBP−30h and Q = actor+26h.

- After `0x422818` the result goes to 422098 as the pitch. 422098's entry
  ECX only fills a local reserved by `push ecx` that is written before it is
  read, and its EDX is loaded at `0x4220ab`.
- After `0x4228b5` the caller does `sub ax,[ebx+0Ch]`, then
  `setb cl; and ecx,1` and later `setb dl; and edx,1` before any other read
  of ECX or EDX.

So ECX and EDX after the call are dead at both sites; the hook still
returns vector_angle's EDX exactly and leaves the entry ECX. Prologue
`55 8b ec 53 56`, relocated 5 bytes (1/2/1/1). Both callees are hooks; the
core calls them through the guest bridge so they keep their modes.
Compare-capable: nothing is written and both callees are pure.

### 40501c `damage_words(x, y)`

`mov ax,[ebp+8]; mov [4a95f2h],ax; mov dx,[ebp+0Ch]; mov [4a95f4h],dx`:
the two words take the low words of x and y, in that order. EAX = lo(entry
EAX, x), EDX = lo(entry EDX, y), ECX unchanged; the hook computes both from
the entry registers, so the registers are exact. The only caller is
`bullet_flight` at `0x40b39d` (x, y = the words at EBP−74h and EBP−72h; it
clears ECX at `0x40b3a5` and reloads EAX and EDX at `0x40b3b3`). Prologue
`55 8b ec 66 8b 45 08`, relocated 7 bytes (1/2/4). Compare-capable with a
4-byte region over `[4a95f2h]`..`[4a95f5h]`.

### 40b5a0 `bullet_charge(t, P, A, W, c)`

EBX = t, ESI = W. `bullet_create(t, P, A, W)` (40b43c, a hook with an entry
shim) is called with EAX = A, EDX = P and ECX = the entry ECX. Then:

1. CX = the low word of c (ECX keeps bullet_create's high word);
2. the word `[O+56h]` = c, O = bullet_create's EAX;
3. if the low word of t is 9 (`cmp bx,9`, branch site `0x40b5c9`), EDX =
   `[W+1A4h]` (W's current target) and the dword `[O+5Bh]` = EDX: the lock
   of a homing bullet.

It returns EAX = O and EDX = `[W+1A4h]` for type 9, bullet_create's EDX
otherwise; the original's ECX is lo(bullet_create's ECX, c). There is no
null test: a null O faults at `0x40b5c5` (precondition, as in the
original).

The callers are `emit` at `0x40eb19`, `0x40eb8f` and `0x40ebf8` (t =
lo(ECX, `[[W'+20h]+2]`), P = 4a9854, A = 4a9860, W = the owner, c =
lo(EAX, the charge word `[W'+7Dh]`), W' = the weapon). After each call
`emit` reloads EAX and EDX (`0x40eb21`, `0x40eb2f`/`0x40ec00`,
`0x40eb97`/`0x40eb9a`); ECX is either popped from a call slot (`0x40eb49`,
`0x40ebb2`, after `mount_offset` 40f540, which reads neither its entry ECX
nor EDX) or reloaded by `emit`'s epilogue (`pop ecx` twice at `0x40ec5f`).
So ECX after 40b5a0 is dead and the hook leaves the entry ECX. Prologue
`55 8b ec 53 56`, relocated 5 bytes (1/2/1/1). The core calls
bullet_create through the guest bridge (its shim and its mode are kept).
Not compare-capable (allocation, sound and RNG in bullet_create).

Known difference: bullet_create's shim saves the caller's EBX/ESI/EDI in
its frame; under the replaced hook those are the bridge's registers, not
t/W/EDI, so these stale stack bytes differ (no reader is known).

### 40a948 `rocket_init(O, n)`

The object part of the category-0 (rocket) projectile. It has the same shape
as `bullet_init` (40af6c), with other tables and another vtable:

1. ECX = `[4a96d0h]` (the projectile definition table, 14-byte records),
   ESI = n, EBX = O, EAX = s16(n), EDX = 7·EAX, EDI = n. EAX = s16 of the
   word `[ECX + 2·EDX]` (word +0 of definition s16(n), a shape index),
   EDX = `[4a96d8h]`, EAX = `[EDX + 4·EAX]`, the shape.
2. `object_init(O, shape)` (402188, a hook) with exactly those EAX/EDX/ECX.
3. In this order: `[O]` ← 4987a0, word O+4 ← 3, byte O+41h ← the low byte
   of n, dword O+4Eh ← 0, `[[6cbee4h]]` += 1, `[O]` ← 498448, dword
   O+56h ← 0.
4. `def_lookup(0, n)` (40ffc8, a hook; n the full dword) with EAX = the
   counter pointer, EDX = 0, ECX = 0. Dword O+4Eh ← its EAX.
5. `[[6cbee4h]]` += 1 (the pointer is reread).

It returns EAX = O and EDX = def_lookup's EDX; the original's ECX is the
counter pointer. The only caller is `projectile_create` (40a9c4) at
`0x40a9ff` (n = lo(EAX, the type word)). It loads ECX from `[6cbee4h]` at
`0x40aa07`, EAX at `0x40aa16` and EDX at `0x40aa20` before any read, so all
three are dead there; the hook returns the pair exactly and leaves the
entry ECX. No branch site. Prologue `55 8b ec 53 56`, relocated 5 bytes
(1/2/1/1). Not compare-capable (a constructor: object_init, the counter).

### Hooks, registers and validation

None of the five hooks reads an uninitialized local or caller stack
residue, so none has an entry shim. Callee counters (hk_<hook>_):

- `pitch_point`: call_492a9a, call_49290c;
- `bullet_charge`: call_40b43c;
- `rocket_init`: call_402188, call_40ffc8;
- `proj_record` and `damage_words` have no game calls.

Compare mode: `proj_record`, `pitch_point` and `damage_words` accept it;
`bullet_charge` and `rocket_init` refuse it.

**In vitro** (`differential_weapon_leaves_test`; `sweep` in
`differential_weapons_test`), standard regime:

- **Direct cases** run against logging stubs for the game callees (entry
  EAX/EDX/ECX and argument slots logged, the bridge's slot count checked);
  ECX is compared against the core's value, plus the callee counter
  deltas. The stubs change what the originals read again: `[W+1A4h]` after
  bullet_create, the counter pointer after object_init and def_lookup.
- **Real-callee cases:** `pitch_point` over the real dist2d_delta and
  vector_angle; `bullet_charge` over the real bullet_create with
  pool_alloc, bullet_init, rng_next, object_init, def_lookup and bullet_def
  nested (the runtime record 49630c and the sound stay stubs);
  `rocket_init` over the real object_init and def_lookup. Nested hooks run
  in rotating modes, the hook under test original or replaced.
- **Compare cases** (`compare_probes.h`) for the three compare-capable hooks.
- **Branch sites:** one (`0x40b5c9`), both ways; mutations: one more for
  `sweep`'s leaves (MUTATIONS.md).

## Rotation helper 47f4f4

The matrix helper that `weapon_prepare` calls at `0x40e873` (local Euler
matrix, mount record, composed record) is hooked as `rotation_place`
(group `rotation_helpers`); contract, registers and validation in
[flyer.md](flyer.md#the-rotation-helpers-0x47f058-and-0x47f4f4)
together with its twin `0x47f058`. In short: b's translation dwords to out,
then the 3 x 3 product by the mode bytes; on the identity paths (either
mode < 1) the 20 rotation bytes of the other operand are copied with byte
19, which is how the local Euler matrix's unwritten byte 19 reaches
`0x4a9847` ([Padding and transient packets](#padding-and-transient-packets)). The hook reads and writes the same guest
bytes in the same order, so that byte still comes from the caller's frame;
no padding rule is added.

## Missile steering 40a488 and ally selection 423974

Group `cold_paths`, core `core/src/weapons.c`. Game calls through the
weapons guest bridge; ECX after each is the entry ECX, dead at the caller.

### `missile_steer` 40a488 `(O, R)`

The player-steered missile `O` of `projectile_flight` (type `s8[O+41h]`
== 3), bridged from `0x40a63c`. `R` = the control record `4d22c6`. With the
camera byte `R+0Dh` = 0 (site `0x40a49b`): guidance off — word `O+56h` ← 0,
byte `O+41h` ← 0, EDX:EAX = entry. Otherwise the pose words `O+0Ch` (x),
`O+0Eh` (y), `O+10h` (z) are bent: `multiply_q8(500h, s16[[R+22h]])` then
`movement_step` of it is subtracted from z, `multiply_q8(500h, s16[[R+26h]])`
then `movement_step` is added to x; the three words are stored with the
cache word `O+32h` ← 0, then `R+0Dh` ← 0 and both axis words `[R+22h]`,
`[R+26h]` ← 0. EAX = 0, EDX = `[R+22h]`; ECX is a frame dword, dead at the
caller. One conditional site, both ways.

### `ally_nearest` 423974 `(G, X)`

Read-only query bridged by `sweep` (`0x4267bd`). For i = 0 while
`(int16)i < (int16)[G+10h]` (re-read each pass), member A = `[[G+0Ch]+4i]`:
skip A == X, or A down (byte `A+0A4h` or `A+99h` set). Else
`dist_gate(A+26h, X+26h)` (EAX = X, EDX = X+26h, ECX = A+26h); the first A
with the smallest distance below 100,000 (0x186A0) wins. EAX = the best
member (0 if none), EDX = the last pass's register (the entry EDX without a
pass). Six conditional sites, both ways. Accepts compare mode (its only
callee `dist_gate` is pure); the comparator checks EAX and EDX.

**Coverage.** `missile_steer` is reached by no regression suite;
`ally_nearest` only by `c3_10-own-building-fire` and
`c5_05-moon-missiles`.

## Open questions

- **Semantic labels.** Names and roles in this file (projectile/bullet,
  free shot, debit, animation, excluded definitions, fire group, gauge
  lamp, homing lock and the like) need a loadout/visual correlation; the
  arithmetic and widths do not depend on them.
- **Dynamic receivers.** Static vtable tables may not cover dynamically
  assigned receivers (V28/V14/V20/V58/V74 receiver classes); the group
  objects passed to 410dbc have no vtable contract. Native
  distance returns a stack-derived ECX upper word; the known V70 bodies
  consume only the signed low range word, and an unexpected V70 class
  needs a read-width audit before the same observation domain is assumed.
- **Hit provenance.** Which regression suite exercises which path
  (firing, flight, swept versus radius dispatch, intersection/damage) is
  not bound by entry counts.
- **Stack headroom at the real entry sites.** The emitter and
  `bullet_create` gaps hold the subtrees tested in vitro; the remaining
  headroom at real entry sites ([Nested frame residues](#nested-frame-residues))
  is not measured in game.
- **Stale stack below `bullet_init`.** Parity of the bytes that
  `flyer_steer`'s pose reads below `target_emit` is not claimed
  ([Bullet constructor](#bullet-constructor-40b43c-40af6c-and-40ffc8)).
  Globally identical unused stack bytes are not claimed for any hook here.
- **`weapon_keys` and 41074c's ESI.** The callers 461005, 46104a and
  461066 of 410f78 (in `0x460870`) are not walked for the index slot's
  high word that a replaced `weapon_keys` changes.
