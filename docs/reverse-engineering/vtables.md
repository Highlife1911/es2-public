# Actor vtables of DBSIM

The actor class tree of DBSIM (`MECH`, `FLYER` and the building-pool
classes `BASE`, `RADAR_BASE`, `GUN_BASE`, `VEHICLE`, `LC_BASE`): the tables,
their slots, the two update dispatchers, the AI state records behind the
`MECH`/`FLYER` update slots, and the contracts of the hooks whose
specification is this document (groups `actor_step`, `base`,
`actor_virtuals`, `lc_base`, `actor_leaves`, `slot_leaves`, `sim_leaves`).
These are the functions that the [DBSIM map](../reference-game/dbsim_map.md)
sees only as vtable or member-pointer roots. Other class trees with vtables
(weapons, projectiles, shapes, streams, cockpit gauges) are in
[weapons.md](weapons.md), [rasterizer.md](rasterizer.md) and
[cockpit.md](cockpit.md).

**Source and evidence.** Everything is read from the reference
`VER95/DBSIM.EXE` (hash in `data/reference_hashes.txt`; DATA at VA `0x497000` = file
offset `0x96200`, CODE at `0x401000` = file offset `0x600`) with
`llvm-objdump -d -M intel` of the pinned LLVM-MinGW, re-disassembled from
the function entry (`--start-address`) where the linear sweep is
misaligned (`0x411a3c`, `0x421584`, around `0x407b1f`). Vtable slots,
Borland RTTI names (dword at `vtable − 12` → type descriptor, name offset
word at descriptor `+6`, as in [cockpit.md](cockpit.md)) and the AI state
records are read from the image bytes; the state records are written at
startup by straight-line initializers whose `mov`/`lea` sequence was
emulated over the image bytes. Hook names and groups are those of
`tools/reference-game/proxy/hook_registry.h`. Role names are hypotheses
unless marked; the hook contracts are checked in vitro by the
differential suite of the group (see [hooks.md](../reference-game/hooks.md#registry)).

**Counts.** "Calls per suite" are the saturated entry probes of the DBSIM
map in the order
a60/c1s/c1c/c3_09/c4_08/c3_10 (acceptance-60s, c1_01-success,
c1_01-collision, c3_09-loadout-b, c4_08-razor-plasma, c3_10-loadout-hercs);
1000 = at least 1000, "–" = no probe row (liveness unknown, not zero).
"Suites of 40" are from the 40-suite map (every hook original).

**Notation.** O or A the object (first stack argument), B a building, C its
configuration `[obj+1F2h]`, P the runtime part records `[obj+205h]` (stride
0Bh), Q the configuration part records `[C+14h]` (stride 1Eh), M the shape
instance `[obj+34h]`, f the frame pointer (EBP), `s16`/`u16`/`s8` sign or
zero extension, `lo(R, w)` a 16-bit load or 16-bit arithmetic into R that
keeps R's high word. "BASE family" = `BASE`, `RADAR_BASE`, `GUN_BASE`,
`VEHICLE`, `LC_BASE`. All hooks here are cdecl; "args" is the descriptor's
argument-slot count.

## 1. The class tree

`DRAWABLE` → `MAT_OBJ` → `TS_OBJ` → `ANIM_OBJ` → `DET_ANIM_OBJ` → `ACTOR`
(`0x49a54c`, abstract: six pure-virtual slots) → { `MECH` `0x49a282`,
`FLYER` `0x49a5e0`, `BASE` `0x497940` → { `RADAR_BASE` `0x4979d4`,
`GUN_BASE` `0x4978ac` → `VEHICLE` `0x497818`, `LC_BASE` `0x497784` } }
(from RTTI names and constructor stores). Every actor table has
34 slots (`+0x00`..`+0x84`). "MECH" is the RTTI name of the class the docs
call *vehicle* (`0x49a282`); the building-pool class `VEHICLE`
(`0x497818`) is the convoy truck of [convoy.md](convoy.md).

### 1.1 Tables and RTTI

| Table | File offset | dword at −12 (bytes) | RTTI name | Constructor store |
|---|---|---|---|---|
| `0x49a54c` | `0x9974c` | `31 70 42 00` → `0x427031` | `ACTOR` | `0x4053f5`, `0x40552a`, `0x40564d`, `0x40576e`, `0x4058c3` (factory); `0x415bf4` (MECH); `0x421629` (FLYER) |
| `0x497940` | `0x96b40` | `b7 6b 40 00` → `0x406bb7` | `BASE` | `0x405486`, `0x4055bc`, `0x4056df`, `0x405800`, `0x405964` |
| `0x4979d4` | `0x96bd4` | `5b 6c 40 00` → `0x406c5b` | `RADAR_BASE` | `0x4054ab` |
| `0x4978ac` | `0x96aac` | `07 6c 40 00` → `0x406c07` | `GUN_BASE` | `0x4055e1`, `0x405825` |
| `0x497818` | `0x96a18` | `67 6b 40 00` → `0x406b67` | `VEHICLE` | `0x405836` |
| `0x497784` | `0x96984` | `17 6b 40 00` → `0x406b17` | `LC_BASE` | `0x405989` |
| `0x49a282` | `0x99482` | `d7 70 42 00` → `0x4270d7` | `MECH` | `0x415c60` |
| `0x49a5e0` | `0x997e0` | `6f 71 42 00` → `0x42716f` | `FLYER` | `0x421692` |

Every name offset word (descriptor `+6`) is `0x30`. The dwords at
`vtable − 8` and `vtable − 4` are 0; the dword at `vtable + 0x88` is the
next table's descriptor pointer, not a slot (e.g. `BASE + 0x88` =
`0x406c5b`, the `RADAR_BASE` descriptor), so the tables have 34 slots.
The `MECH` table starts at an odd address (`0x49a282`); the slots are
read as unaligned dwords.

Base classes below `ACTOR` (from the stores, names from RTTI):
`0x402278` stores `DRAWABLE` `0x4a14a4`, `MAT_OBJ` `0x497289` and `TS_OBJ`
`0x4a0b80` into `[obj]` in that order; `0x40332c`/`0x403368`/`0x4033a4`
(three variants, called by the factory cases and the `MECH` constructor)
call a `TS_OBJ` constructor and store `ANIM_OBJ` `0x4973ac`, then
`DET_ANIM_OBJ` `0x4973cc`. Slots `+0x00`..`+0x10` are the `TS_OBJ`/
`DET_ANIM_OBJ` part of the layout (see 2.2).

### 1.2 Who builds which class

The three constructors are hooks of group `mission_ctors`: the building
factory `building_factory` `0x405314`, `mech_ctor` `0x415bb0`, `flyer_ctor`
`0x4215f4`, and the group constructor `group_ctor` `0x423b34`; contract in
[mission_load.md](mission_load.md#17-l5-constructors).

**Building pool `[0x4a95a0]`** (created once per mission by `0x405df4`,
memset at creation by `0x471ad8` → `0x49681a`; allocated only in the
factory `0x405314`, [mission_runtime.md](mission_runtime.md)). The factory
switches on the record's type word through the byte table `0x405366`
(62 entries, types 0..3Dh) and the jump table `0x4053a4` (6 cases; bytes
read from the image):

| Case | Code | Classes stored | Type words |
|---|---|---|---|
| 5 | `0x4053bc` | `ACTOR` → `BASE` → `RADAR_BASE` | 5, 6, 29, 30 |
| 3 | `0x4054ea` | `ACTOR` → `BASE` → `GUN_BASE` | 8, 11, 32, 35 |
| 2 | `0x40560d` | `ACTOR` → `BASE` | 0–4, 7, 9, 12–28, 31, 33, 36–44 |
| 1 | `0x40572d` | `ACTOR` → `BASE` → `GUN_BASE` → `VEHICLE` | 45–52, 55–61 |
| 4 | `0x405877` | `ACTOR` → `BASE` → `LC_BASE` | 34 |
| 0 | `0x4059ca` | nothing (default) | 10, 53, 54, above 3Dh |

- The class chain is stored in construction order, each step with the
  runtime's construction count (`[[0x6cbee4]]`): `ACTOR` `0x49a54c`,
  `BASE` `0x497940` (word `+4` = 5, `+1A8h` = 1), then `RADAR_BASE`,
  `GUN_BASE`, `GUN_BASE` → `VEHICLE` (word `+4` = 0Ah, `+1A8h` = 3) or
  `LC_BASE` (with its three 1Fh-byte weapon records at `+209h`,
  `0x49667c(…, 0x4069c5)`).
- Case 0 builds no object and continues with the caller's EDI; no launch
  set uses such a type.
- The factory never writes the AI state pointer `+0x4d` (it only computes
  `obj+0x4d` into a dead local at `0x405401`), and pool memory is zeroed at
  creation, so building-pool objects keep `[obj+0x4d]` = 0; no state
  install site (section 3) is in building code.

**MECH pool `[0x4a9b7a]`**: `0x471b30([0x4a9b7a])` at `0x42567f`, then the
constructor `0x415bb0` (single caller `0x4256a4`, mission loader):
`ACTOR` → `MECH` `0x49a282` (word `+4` = 7). **FLYER pool `[0x4a9db9]`**:
`0x471b30` at `0x425a46`, constructor `0x4215f4` (caller `0x425a71`):
`ACTOR` → `FLYER` `0x49a5e0` (word `+4` = 6).

All three register the object in the actor list `0x4a9af8` (`0x411dd4`,
hook `ptr_list_add`: the store, then the reread count + 1 and its old value
at `+4Bh`) and leave the AI state pointer `+4Dh` to `script_start` (MECH:
"deciding" `0x4993a4`, the player "player" `0x4993e2` or "player fly"
`0x499420`; FLYER: "deciding" `0x499cf8`). The group constructor
`0x423b34` assigns every member its group (`+45h`) and slot (`+49h`)
through the class's join method (`0x417aa8` MECH, `0x421ee8` FLYER,
`0x405c3c` building), which also places members still at the load's
default position through the formation slot `+78h`.

**Live classes per suite.** The classification runs of
[oracle_suites.md](../reference-game/oracle_suites.md)
saw `MECH` in all
three kept sets, `VEHICLE` in c3_09 and c4_08 (none in c3_10, matching
`convoy_think` 0 there), `LC_BASE` and one `FLYER` only in c4_08 (matching
`0x4045c8` = 1000 only in c4_08), `RADAR_BASE` in c3_10 and c4_08. The
FLYER update slots count 0 outside the C1_01/acceptance suites
([flyer.md](flyer.md) explains why the c4_08 flyer never acts).

### 1.3 The two update dispatchers

Logic-frame row 10 updates only the building pool; `MECH` and `FLYER`
objects are updated in row 11 through `0x411cec` (rows in
[main_loop.md](main_loop.md)). `0x415b38`, the `MECH` update, is therefore
a row-11 update.

**Row 10, `0x45f791`..`0x45f7c2`.** For each object of `[0x4a95a0]` whose
group `[obj+0x45]` has `+0x14` = 0: `push ebx; mov edx,[ebx]; call
[edx+0x18]; pop ecx`; result ignored. Slot `+0x18` is `base_update`
`0x403ca8` (`BASE`, `RADAR_BASE`), `turret` `0x404100` (`GUN_BASE`),
`convoy_think` `0x46a6d4` (`VEHICLE`) or `lc_base_update` `0x4045c8`
(`LC_BASE`).

**Row 11, `actor_step` `0x411cec(A)`** (50 instructions, 1000 ×6; single
caller `0x423af5` in `order_update` `0x423a74`, for every member of every
group, index order). `MECH` and `FLYER` objects live in their own pools
(`[0x4a9b7a]`, `[0x4a9db9]`) and get slots `+0x1c`, `+0x14`, `+0x18` here:

1. `[A+0x4d]` = 0 → return (`0x411cf7`; all building-pool members).
2. dword `[A+0x52]` = 0 (state timer run out) → slot `+0x1c(A)`
   (`0x411d0b`).
3. byte `[[A+0x4d]+8]` = 0 → `timer_dword_step(A+0x51)` (`0x411d20`).
4. slot `+0x14(A)` (`0x411d29`).
5. byte `[A+0xaf]` ≠ 0 → clear it (skip once); else slot `+0x18(A)`
   (`0x411d46`) and, when its AX ≠ 0, dword `[A+0x52]` ← 0 (forces a
   decision next frame).
6. `0x413eb0(A+0x4d)` (15 instructions: byte `[A+0x83]` set → clear it;
   else byte `[A+0x82]` ← 1 and dword `[A+0x56]` += 1).

The register-level contract is in
[`actor_step` and the update-slot trampolines](#actor_step-and-the-update-slot-trampolines).
`0x411d0b`, `0x411d29` and `0x411d46` are the only actor dispatch sites
of slots `+0x1c`, `+0x14`, `+0x18` established from the bytes besides
`0x45f7a6`. The other sites with these displacements (28, 114 and 275 in
total) were sampled: the large groups are stream reads in the file loaders
(`0x405fac` `LC_WPNS.DAT`, `0x424308` script, `0x42e010` world; slot
`+0x18` of the stream class), terrain/world code (`0x42a9c0`) and shape or
cockpit objects (`[ebx+0x17]` receivers); none of them was bound to an
actor table.

## 2. Slots

### 2.1 Slot matrix

Addresses per table with the hook that replaces the function (**bold**).
*pv* = pure-virtual trap `0x496748` (pushes the string "Pure virtual
function called" at `0x4a79b8` and calls the runtime). `RADAR_BASE`,
`GUN_BASE`, `VEHICLE` and `LC_BASE` equal `BASE` except where the fourth
column shows a value; "=" means the same function as the `ACTOR` column.
The draw slot `+00` and the destructors `+08` are not hooks (2.2).

| Slot | Role (hypothesis unless noted) | ACTOR | BASE | RADAR / GUN / VEHICLE / LC | MECH | FLYER |
|---|---|---|---|---|---|---|
| +00 | draw (LOD by camera distance, shape draw) | `0x4033e4` | `0x4034f4` | | `0x4174c8` | `0x4215cc` |
| +04 | position pointer (`obj+0x26`) | `0x46b905` **object_position** | = | | = | = |
| +08 | virtual destructor | `0x427081` | `0x406dab` | `0x406df3` / `0x406d5c` / `0x406d05` / `0x406caf` | `0x427127` | `0x4271bf` |
| +0c | returns 0 | `0x4786cb` **object_zero** | = | | = | = |
| +10 | shape word `[[[obj+0x34]+4]+8]` | `0x46b910` **shape_word** | = | | = | = |
| +14 | movement step (state `+0x24`) | `0x411ab4` **actor_move** (AX = 1) | = | | `0x415afc` **mech_move** | `0x4217fc` **flyer_move_call** |
| +18 | update / behaviour (state `+0x18`) | *pv* | `0x403ca8` **base_update** | `0x403ca8` / `0x404100` **turret** / `0x46a6d4` **convoy_think** / `0x4045c8` **lc_base_update** | `0x415b38` **mech_act** | `0x42184c` **flyer_act** |
| +1c | decision (state `+0x30`) | *pv* | `0x406cfe` **base_decide** (0) | | `0x415b74` **mech_decide** | `0x421888` **flyer_decide** |
| +20 | projectile segment hit test (receiver V20, [weapons.md](weapons.md)) | *pv* | `0x405038` **base_hit_test** | | `0x418ba8` **mech_hit_test** | `0x421c8c` **flyer_hit_test** |
| +24 | sight/aim node matrix | `0x411a9c` **actor_aim_node** (0) | `0x403548` **base_aim_node** | | `0x417b98` **mech_aim_node** | `0x411a9c` |
| +28 | order handler | `0x411a64` **actor_order** (0) | = | | `0x420ad4` **mech_order** | = |
| +2c | key handler | `0x411a6c` **actor_keys** (0) | = | | `0x4157c8` **vehicle_keys** | `0x4215c0` **flyer_keys** (0) |
| +30 | two offset vectors | `0x411a74` **actor_offsets** | `0x40351c` **base_offsets** | | `0x4155c4` **mech_offsets** | `0x411a74` |
| +34 | armour value toward a side | `0x411a4c` **actor_armour** (0) | = | | `0x4154d0` **mech_armour** | = |
| +38 | target scale for aiming | `0x411a54` **actor_target_scale** (0) | = | | `0x415498` **mech_target_scale** | `0x421584` **flyer_target_scale** |
| +3c | weapon/torso angle | `0x411a5c` **actor_angle** (0) | = | = / `0x403594` **gun_angle** / `0x403594` / = | `0x415488` **mech_angle** | = |
| +40 | condition score | *pv* | `0x4052b4` **base_condition** | | `0x415504` **mech_condition** | `0x421e84` **flyer_condition** |
| +44 | angle in front sector | `0x411acc` **actor_in_front** | `0x405308` **base_in_front** (1) | | `0x411acc` | `0x411acc` |
| +48 | contact notice | `0x4127f8` **actor_contact** (empty) | = | | `0x412800` **mech_contact** | = |
| +4c | engagement roll | `0x411ac0` **actor_engage_roll** (1) | = | | `0x41cabc` **mech_engage_roll** | = |
| +50 | attacked-by notice (receiver V50) | `0x411aec` **actor_attacked** | = | | `0x41f7b8` **mech_attacked** | = |
| +54 | part facing a viewer | `0x411a44` **actor_part_facing** (−1) | `0x406868` **part_facing** | | `0x41b534` **facing_part** | `0x411a44` |
| +58 | part position | `0x411a3c` **actor_part_position** (empty) | `0x406808` **base_part_position** | | `0x41b60c` **part_position** | `0x411a3c` |
| +5c | radius | `0x411aa4` **actor_radius** (0) | `0x4035a4` **base_radius** | | `0x415518` **mech_radius** | `0x411aa4` |
| +60 | kill credit | `0x411b2c` **actor_kill** (empty) | = | | `0x415710` **mech_kill** | = |
| +64 | take position of linked object | `0x411b34` **actor_link_position** (empty) | = | | `0x41dd2c` **mech_link_position** | = |
| +68 | set byte `+0xb1` | `0x42200c` **actor_mark** | = | | = | = |
| +6c | mount word | `0x411b04` **actor_mount_word** (AX = 1) | = | | `0x4155ac` **mech_mount_word** | = |
| +70 | radial hit (receiver V70) | `0x411b3c` **actor_radial_hit** | `0x404f20` **building_hit** | | `0x4187d0` **vehicle_hit** | `0x411b3c` |
| +74 | part damage (V74) | *pv* | `0x404d70` **building_part** | | `0x417de4` **vehicle_part** | `0x421bb4` **flyer_part** |
| +78 | formation slot point | *pv* | `0x405c04` **formation_base** | | `0x417898` **formation_mech** | `0x421e98` **formation_flyer** |
| +7c | radius for sight/contact | `0x411aac` **actor_sight_radius** (0) | `0x4035b8` **base_sight_radius** | | `0x41552c` **mech_sight_radius** | `0x411aac` |
| +80 | next living part/weapon index | `0x411b1c` **actor_next_part** (−1) | `0x403624` **base_next_part** | | `0x415558` **mech_next_part** | `0x411b1c` |
| +84 | part/weapon word i | `0x411b10` **actor_part_word** (1) | `0x40360c` **base_part_word** | | `0x415540` **mech_part_word** | `0x411b10` |

`ACTOR` is abstract (pure-virtual slots `+0x18`, `+0x1c`, `+0x20`, `+0x40`,
`+0x74`, `+0x78`); its table is stored only transiently in the constructor
chains. `GUN_BASE` and `VEHICLE` differ from `BASE` only in `+0x08`,
`+0x18`, `+0x3c`; `RADAR_BASE` only in `+0x08`; `LC_BASE` only in `+0x08`,
`+0x18` ([convoy.md](convoy.md) noted the same for `VEHICLE`/`GUN_BASE`).

The eight tables hold 95 distinct slot functions; the state records
(section 3) add 28 methods. Every slot function is a hook except the four
draw methods, the eight destructors and the trap `0x496748`. Every slot
body is referenced only from the actor tables (`0x4786cb`, the `+0c`
method, from all 31 tables of the `DRAWABLE` tree); apart from
`0x411aec` (called directly at `0x41f853`, below) no direct call, jump or
branch enters any slot leaf except its own internal branches (byte and
dword scans of the image).

**Coverage of the "vtable-only" set** (DBSIM map). With
direct calls from the main-loop roots (every child of `0x45f570`,
`0x45fca8`, `0x45fba4`, `0x4011a0`) as "directly reachable", 721 of the
probed live entries are reachable only through indirect calls (the map's
673 used a different root set). The 80 live actor-table and state-method
roots and their direct-call subtrees cover 182 of them; the rest belongs to
the other class trees (weapons, projectiles, shapes/animation, cockpit) and
to callbacks.

Contracts of the slots in other documents: `+20` and `flyer_part` `+74`
in [hit_tests.md](hit_tests.md) (`0x4266c9` in `sweep` is the only
dispatch site of the actor slot `+0x20`, and only its EAX is live; the
MECH part hit `0x4188c8` is the hook `mech_part_hit`); `building_hit`,
`building_part`, `vehicle_hit`, `vehicle_part` in
[player_movement.md](player_movement.md) and [vehicle.md](vehicle.md);
`mech_order` in [mech_order.md](mech_order.md); the MECH notices
(`mech_contact`, `mech_attacked`, `mech_kill`, group `mech_notices`) and the
formation slots (group `formation`) in [ai.md](ai.md); `object_position` and
`shape_word` in [main_loop.md](main_loop.md#the-object-base-methods-ts_obj-slots-00-04-10).

### 2.2 Shared slots `+0x00`..`+0x10` (TS_OBJ part)

| Function | Slots | Instr. | Calls per suite | State | Notes | Own code |
|---|---|---:|---|---|---|---|
| `0x4033e4` | ACTOR +00, called by the other three | 104 | 1000 ×6 | draw | `0x401fe4` (lazy `euler_matrix` into `obj+0x12`, sets `+0x32`, then `0x48c444`), LOD pick by `dist3d_delta` to the point `0x6c601c` (the camera, hypothesis) and `multiply_q10` over `[obj+0x41]`, writes `[[obj+0x34]+4]` (shape detail pointer) and resets it after the shape's slot `+0x1c` (`0x4034d7`) | service row `shape_actor_levels` ([rasterizer.md](rasterizer.md#detail-levels-of-actors)) |
| `0x4034f4` | BASE family +00 | 17 | – | draw | `0x4769b8([0x4a95b4])` (sets renderer global `0x4a1020`), `0x4033e4`, `0x4769b8(0)` | service row `shape_actor_levels_offset` |
| `0x4174c8` | MECH +00 | 39 | 1000 ×6 | draw | if word `0x4a9b82`: `0x4030d0` (hook `part_shapes_bind`) per mount (config `+0xe6`, count `+0xea`), then `0x4033e4` | service row `shape_mech_draw` ([rasterizer.md](rasterizer.md#actor-draw-slots-and-explosion-binning)) |
| `0x4215cc` | FLYER +00 | 17 | 704/1000/764/0/0/0 | draw | `0x4769b8`, `0x4033e4` | service row `shape_flyer_draw` |
| `0x46b905` | +04 all | 6 | 1000 ×6 | no | returns `obj+0x26` | hook `object_position` |
| destructors | +08 | 28–32 | – | heap | decrement the runtime's construction counter `[0x6cbee4]`, call `0x402400`/`0x411e1c` | stay original (runtime) |
| `0x4786cb` | +0c all | 5 | – | no | `xor eax, eax` | hook `object_zero` |
| `0x46b910` | +10 all | 22 | 1000 ×6 | no | word `+8` of `[[obj+0x34]+4]`, 0 without shape | hook `shape_word` |

The draw slots are the own process's service rows in `sim/src/shape_draw.c`
(in vitro; not checked live). The MECH draw slot `0x4174c8`
reaches `0x4030d0` through the dispatch's hook entry `part_shapes_bind`
(core's `es2_part_shapes_bind_registers`), so it needs no service row and
does not refuse; the in-vitro differential test runs the real `0x4030d0` on
the original side
([rasterizer.md](rasterizer.md#actor-draw-slots-and-explosion-binning)).

Trap for the boundary between drawing and simulation: the draw path writes
object fields that simulation code also writes — the lazy matrix
(`+0x12`..`+0x32`, same value any simulation reader would compute) and,
for objects with an LOD list at `[obj+0x41]`, the shape detail pointer
`[[obj+0x34]+4]`, which the BASE part-destruction step also writes
(`0x403b08`, destroyed model). Building objects have `[obj+0x41]` = 0
(`0x403356` in `0x40332c`), so for them the draw path does not touch it.

### 2.3 Building-pool classes (`BASE`, `RADAR_BASE`, `GUN_BASE`, `VEHICLE`, `LC_BASE`)

"Actor sites" are dispatch sites whose receiver the surrounding bytes take
from an actor (pool iterator, actor list, group member, target or link
field) or that earlier documents bound. The receiver histograms of
[hook_frontier.md](../reference-game/hook_frontier.md#receiver-histograms-of-the-virtual-call-leaves)
bind every live slot leaf to the classes read statically (MECH-only,
FLYER-only and building-pool leaves as in 2.3, 2.4, 2.5), and each object
destructor runs on its own class only.

| Slot | Function | Hook | Instr. | Calls per suite | Actor sites (source) | State |
|---|---|---|---:|---|---|---|
| +14 | `0x411ab4` | `actor_move` | 3 | – | `0x411d29` (never reached: `[obj+0x4d]` = 0) | no |
| +18 | `0x403ca8` BASE update | `base_update` | 59 | 1000 ×6 (also direct from `turret`, `convoy_think`, `0x4045c8`) | `0x45f7a6` | **yes** |
| +18 | `0x404100` | `turret` | 387 | – | `0x45f7a6` | yes |
| +18 | `0x46a6d4` | `convoy_think` | 100 | 1000 ×5, 0 c3_10 | `0x45f7a6` | yes |
| +18 | `0x4045c8` LC_BASE update | `lc_base_update` | 374 | 0/0/0/0/1000/0 | `0x45f7a6` | yes |
| +1c | `0x406cfe` | `base_decide` | 3 | – | `0x411d0b` (never reached) | no |
| +20 | `0x405038` | `base_hit_test` | 212 | 1000 ×6 (25,649/460,234/38,965 in count mode, [live_coverage.md](../reference-game/live_coverage.md)) | `0x4266c9` (`sweep`, [weapons.md](weapons.md)) | **yes** |
| +24 | `0x403548` | `base_aim_node` | 26 | 1000 ×6 | `0x412671`, `0x41267b`, `0x41dc60`, `0x41dc6f`, `0x41ea4c` ([ai.md](ai.md)) | static buffer |
| +30 | `0x40351c` | `base_offsets` | 19 | – | `0x4042d4`, `0x40473c`, `0x41eafd` | no |
| +34 | `0x411a4c` | `actor_armour` | 3 | 170/0/0/1000/0/383 | `0x41f39f` | no |
| +38 | `0x411a54` | `actor_target_scale` | 3 | 0/0/6/674/26/0 | `0x404305`, `0x41f654`, `0x422705` | no |
| +3c | `0x403594` (GUN, VEHICLE: word `+0x20f`), `0x411a5c` (others: 0) | `gun_angle`, `actor_angle` | 6 / 3 | 623/510/658/1000/503/459; 1000/1000/1000/1000/91/1000 | `0x412af7`, `0x412b58`, `0x41c832`, `0x41da90`, `0x41dbb1`, `0x41e06c`, `0x41e797`, `0x460b53` | no |
| +40 | `0x4052b4` | `base_condition` | 36 | 0/0/0/1000/1000/1000 | `0x404e8e`, `0x412cdc`, `0x412e27`, `0x41cc4f` | no; **IDIV by the summed maxima, faults on 0** |
| +44 | `0x405308` | `base_in_front` | 3 | 1000/1000/1000/1000/594/1000 | `0x412b02`, `0x412b68` | no |
| +48 | `0x4127f8` | `actor_contact` | 2 | 1000/1000/1000/0/0/1000 | `0x4129e5`, `0x412b20` | no |
| +4c | `0x411ac0` | `actor_engage_roll` | 3 | 22/38/22/546/27/3 | `0x41221f`, `0x41d217` | no |
| +50 | `0x411aec` | `actor_attacked` | 10 | 4/76/34/754/80/0 | `0x426801` ([weapons.md](weapons.md)), `0x410cb6` | yes (via `contact_share`) |
| +54 | `0x406868` | `part_facing` | 83 | 170/0/0/1000/0/383 | `0x40aacb`, `0x41eac5` | no |
| +58 | `0x406808` | `base_part_position` | 39 | 170/0/0/1000/9/383 | nine sites listed in [vehicle.md](vehicle.md) (`0x404fba`, `0x4068ac`, …) | matrix cache |
| +5c | `0x4035a4` | `base_radius` | 7 | 1000 ×6 | `0x418f94`, `0x41900a`, `0x426a5e`, `0x4284a0`, `0x428544`, `0x4285c2`, `0x46a631` (`0x41f41b` is a weapon receiver, [ai.md](ai.md)) | no |
| +60 | `0x411b2c` | `actor_kill` | 2 | 1/1/1/0/0/0 | `0x404ec1`, `0x41824e`, `0x418515`, `0x421c1b` (the damage source; `0x41f42d`, `0x41f702`, `0x41b035` are weapon receivers) | no |
| +64 | `0x411b34` | `actor_link_position` | 2 | 0/10/4/5/0/0 | `0x4269f4` | no |
| +68 | `0x42200c` | `actor_mark` | 7 | 0/0/0/38/0/100 | `0x41903a`, `0x46a685`, `0x417fdf` | yes (byte `+0xb1`) |
| +6c | `0x411b04` | `actor_mount_word` | 3 | 9/6/6/25/3/0 | `0x40aa9b` | no |
| +70 | `0x404f20` | `building_hit` | 85 | – | `0x426a8d`, `0x419373`, `0x41938a`, `0x41895a` | yes |
| +74 | `0x404d70` | `building_part` | 139 | – | 20 sites ([player_movement.md](player_movement.md)), plus `0x4038e6` in `0x403890` | yes |
| +78 | `0x405c04` | `formation_base` | 25 | 1000/1000/1000/0/0/0 | `0x405d5d`, `0x41fbe7`, `0x423862`, `0x4225dd`, `0x46aa90`, `0x417b76`, `0x421f84` | no (out point, function-static buffer `0x4975d6`; through `0x405b9c`, hook `formation_slot`) |
| +7c | `0x4035b8` | `base_sight_radius` | 29 | 1000 ×6 | `0x416611`, `0x418fde`, `0x42361c`, `0x46a654` | no |
| +80 | `0x403624` | `base_next_part` | 28 | – | `0x40e4ca` (weapon code; receiver not bound) | no |
| +84 | `0x40360c` | `base_part_word` | 9 | – | `0x40e50d` (as +80) | no |

`+0x28`, `+0x2c` (`0x411a64` `actor_order`, `0x411a6c` `actor_keys`) return
0 and have no probe row.

### 2.4 `MECH` (`0x49a282`)

| Slot | Function | Hook | Instr. | Calls per suite | Actor sites | State |
|---|---|---|---:|---|---|---|
| +14 | `0x415afc` → state `+0x24` | `mech_move` | 26 | 1000 ×6 | `0x411d29` | via member |
| +18 | `0x415b38` → state `+0x18` | `mech_act` | 26 | 1000 ×6 | `0x411d46` | via member |
| +1c | `0x415b74` → state `+0x30` | `mech_decide` | 26 | 3/7/5/38/9/8 | `0x411d0b` | via member |
| +20 | `0x418ba8` | `mech_hit_test` | 216 | 1000/1000/1000/1000/1000/652 | `0x4266c9` | yes (`0x4188c8` 236 instr., hook `mech_part_hit`) |
| +24 | `0x417b98` | `mech_aim_node` | 35 | 1000 ×6 | as BASE +24 | no: node of config word `+0xc` through the shape's slot `+0x20` (`0x417bbe`), else `0x6c56a8` |
| +28 | `0x420ad4` | `mech_order` | 32 | 0/0/0/4/0/0 | `0x43164d`, `0x41f74c`, `0x4232f9` ([mech_order.md](mech_order.md)) | yes |
| +2c | `0x4157c8` | `vehicle_keys` | 225 | 1000/…/937 | `0x460841`, `0x460854` | yes |
| +30 | `0x4155c4` | `mech_offsets` | 22 | 315/315/315/1000/1000/1 | as BASE +30 | no |
| +34 | `0x4154d0` | `mech_armour` | 17 | 348/1000/1000/1000/1000/0 | `0x41f39f` ([ai.md](ai.md); see [the front flag](#mech_armour-0x4154d0-and-the-front-flag)) | no |
| +38 | `0x415498` | `mech_target_scale` | 19 | 1000/1000/1000/1000/701/4 | as BASE +38 | no (`multiply_q10`) |
| +3c | `0x415488` | `mech_angle` | 6 | 1000 ×6 | as BASE +3c, plus `0x4156ad`, `0x460528`, `0x460e07` | no |
| +40 | `0x415504` | `mech_condition` | 9 | 1000/1000/1000/1000/1000/4 | as BASE +40 | no (`part_condition`) |
| +44 | `0x411acc` | `actor_in_front` | 11 | 1000 ×6 | `0x412b02`, `0x412b68` | no |
| +48 | `0x412800` | `mech_contact` | 66 | 330/940/431/1000/112/662 | `0x4129e5`, `0x412b20` (`actor_pass` neighbourhood) | **yes** (globals `0x4a9b00[]`, `0x4a9b64` timer, `0x4a9b65`; message `0x420a98`) |
| +4c | `0x41cabc` | `mech_engage_roll` | 46 | 6/25/16/153/14/4 | `0x41221f` (`target_select`), `0x41d217` | **yes** (two `rng_next` draws, stream A) |
| +50 | `0x41f7b8` | `mech_attacked` | 215 | 4/77/34/803/74/25 | `0x426801` | **yes** (`0x41d5ec` `attack_replan` → `0x41cf18`, `0x41f790` `mech_alert`, `0x420a98`) |
| +54 | `0x41b534` | `facing_part` | 75 | 8/5/6/130/9/0 | `0x40aacb`, `0x41eac5` | no |
| +58 | `0x41b60c` | `part_position` | 105 | 510/565/572/1000/855/43 | as BASE +58 | no |
| +5c | `0x415518` | `mech_radius` | 7 | 1000 ×6 | as BASE +5c | no |
| +60 | `0x415710` | `mech_kill` | 60 | 0/3/0/22/4/5 | as BASE +60 | **yes** (kill words `+0x2a4[type]`, byte `+0xa6`, `0x4a9e84`; message `0x420a98`) |
| +64 | `0x41dd2c` | `mech_link_position` | 21 | 0/23/0/78/1/0 | `0x4269f4` | **yes** (`+0x31e` ← link `[+0x1a4]` position, byte `+0xad`) |
| +68 | `0x42200c` | `actor_mark` | 7 | as BASE | as BASE | yes |
| +6c | `0x4155ac` | `mech_mount_word` | 8 | 0/0/0/113/0/0 | `0x40aa9b` | no |
| +70 | `0x4187d0` | `vehicle_hit` | 93 | – | as BASE +70 | yes |
| +74 | `0x417de4` | `vehicle_part` | 690 | – | as BASE +74 | yes |
| +78 | `0x417898` | `formation_mech` | 31 | 1000/1000/1000/1000/0/1000 | as BASE +78 | no (out point; statics `0x49a430`) |
| +7c | `0x41552c` | `mech_sight_radius` | 7 | 1000 ×6 | as BASE +7c | no |
| +80 | `0x415558` | `mech_next_part` | 32 | – | `0x40e4ca` | no |
| +84 | `0x415540` | `mech_part_word` | 8 | – | `0x40e50d` | no |

`0x415558`/`0x415540` are dispatched only in `targ_next` `0x40e4ac` (at
`0x40e4ca`) and `targ_aim` `0x40e4dc` (at `0x40e50d`); `0x40e4dc` is called
only by `aim_point` (whose `call_40e4dc` counter is 0 in all six suites), and `0x40e4ac` also by the HUD key handler `hud_key`
at `0x432ee9`. They run only outside the six suites (`mech_part_word` and
`mech_next_part` in c3_10-hunt-targ, see
[Actor slot leaves](#actor-slot-leaves)).

### 2.5 `FLYER` (`0x49a5e0`)

Live only in acceptance/c1_01 (1000/1000/1000/0/0/0 for the update
trampolines). Specific slots: `+0x00` `0x4215cc` (draw, 2.2), `+0x14`
`0x4217fc` `flyer_move_call`, `+0x18` `0x42184c` `flyer_act`, `+0x1c`
`0x421888` `flyer_decide`, `+0x20` `0x421c8c` `flyer_hit_test` (173
instructions, 922/1000/1000/0/0/0), `+0x2c` `0x4215c0` `flyer_keys` (0),
`+0x38` `0x421584` `flyer_target_scale` (`multiply_q10(1000, word
+0x233)`), `+0x40` `0x421e84` `flyer_condition`
(`part_condition(obj+0x200)`), `+0x74` `0x421bb4` `flyer_part` (73
instructions, no probe row; damage with `death_actions`, `trigger_fire`,
`0x40da38`, sound `0x462ac4`), `+0x78` `0x421e98` `formation_flyer` (uses
`0x423044`). All other slots are the `ACTOR` defaults.

## 3. AI state records (the targets of slots `+0x14`/`+0x18`/`+0x1c`)

The `MECH`/`FLYER` slots `+0x14`/`+0x18`/`+0x1c` are member-pointer
trampolines into AI state records: the object's state pointer
`[A+0x4d]` points to a record whose member pointers are called by the
trampolines.

**Layout (from the initializers and the trampolines):** `+0x00`
name pointer, `+0x04` dword (0, 10, 0x1f4, 0x1388, 0x3a98 or 0xc350; a
duration, hypothesis), `+0x08` a sub-record built by `0x415028` (its byte
`+0` gates the state timer in `0x411cec` step 3), `+0x18` (behaviour, slot
`+0x18`), `+0x24` (movement, slot `+0x14`) and `+0x30` (decision, slot
`+0x1c`) three Borland member pointers (function, this-delta, third dword);
MECH records also have a word at `+0x3c`. The trampolines call
`function(obj + delta)` when any of the three dwords is nonzero and return
its EAX; otherwise EAX = 0. Every delta and third dword in the retail
tables is 0.

The records are filled at startup by `0x413ed4` (MECH, from the constant
groups `0x4998f8` + 0x24·k, file offset `0x98af8`) and `0x414c68` (FLYER,
from `0x499e9c` + 0x24·k, file offset `0x9909c`); names from the strings at
`0x499c10` and `0x499f98`. Neither has a direct caller; their only
references are the 6-byte records `00 20 d4 3e 41 00` and `00 20 68 4c 41 00`
at `0x4a7a3e`/`0x4a7a44` (file `0xa6c3e`), the format of the runtime's
startup-initializer list (hypothesis for the list, not for the
bytes). States are installed by `push`/`mov` of the record address in
MECH and FLYER code only (`0x415f2a`..`0x415f50`, `0x418292`, `0x4185f6`,
`0x41866b`, `0x41c156`, `0x41c17c`, `0x41cc2c`, `0x41d1e0`..`0x41d29d`,
`0x41e37e`, `0x41e39d`, `0x41eb54`/`0x41eb5b`, `0x41ec63`,
`0x41ed53`..`0x41ed7d`, `0x41f99c`, `0x4212cd`..`0x421533` inside
`mech_order`; FLYER records at `0x421724`, `0x421c58`, `0x422bf9`,
`0x422d5c`..`0x422d6a`).

Every state method is a hook; their contracts are in [ai.md](ai.md)
(MECH) and [flyer.md](flyer.md) (FLYER). The methods without a probe row
(`flyer_attack` `0x41c9cc`, `follow_step` `0x41daac`, `sleep_step`
`0x41c418`, `drive_off` `0x41def0`, `ram_step` `0x41e570`, `ram_move`
`0x41e488`, `flyer_patrol` `0x422b34`, `flyer_scout` `0x422bdc`) counted 0
calls in all six suites with all hooks replaced.

### 3.1 MECH states (`0x4993a4`, stride `0x3e`)

| Record | Name | `+0x18` behaviour (slot +18) | `+0x24` movement (slot +14) | `+0x30` decision (slot +1c) |
|---|---|---|---|---|
| `0x4993a4` | deciding | – | – | `order_resume` 0x41eb34 |
| `0x4993e2` | player | `player_route` 0x41c194 | `ground_update` 0x41a360 | – |
| `0x499420` | player fly | `player_route` | `mech_fly` 0x4198f4 | – |
| `0x49945e` | attacking | `attack_step` 0x41c594 | `ground_update` | `attack_plan` 0x41cf18 |
| `0x49949c` | flanking | `flank_attack` 0x41d4e4 | `ground_update` | `attack_plan` |
| `0x4994da` | facing off | `face_off` 0x41d41c | `ground_update` | `attack_plan` |
| `0x499518` | attacking base | `base_attack` 0x41c86c | `ground_update` | `attack_plan` |
| `0x499556` | attacking flyer | `flyer_attack` 0x41c9cc | `ground_update` | `attack_plan` |
| `0x499594` | patrolling | `order_scan` 0x41d7d0 | `ground_update` | `order_resume` |
| `0x4995d2` | travelling | `travel_step` 0x41d9cc | `ground_update` | `order_resume` |
| `0x499610` | following | `follow_step` 0x41daac | `ground_update` | `order_resume` |
| `0x49964e` | bulldog travel | `travel_step` 0x41d9cc | `ground_update` | `order_resume` |
| `0x49968c` | search/destroy | `group_scan` 0x41d60c | `ground_update` | `order_resume` |
| `0x4996ca` | sleeping | `sleep_step` 0x41c418 | `ground_update` | `order_resume` |
| `0x499708` | skirting | `clear_shot` 0x41dd64 | `ground_update` | `order_resume` |
| `0x499746` | guarding | `hunt` 0x41e224 | `ground_update` | `order_resume` |
| `0x499784` | driving off en(emy) | `drive_off` 0x41def0 | `ground_update` | `order_resume` |
| `0x4997c2` | ramming | `ram_step` 0x41e570 | `ram_move` 0x41e488 | `order_resume` |
| `0x499800` | fleeing | `retreat_fire` 0x41d2c4 | `ground_update` | `attack_plan` |
| `0x49983e` | in limbo | – | – | – |
| `0x49987c` | dead | `dead_step` 0x41e554 | `ground_update` | – |
| `0x4998ba` | disabled | `dead_step` 0x41e554 | `ground_update` | – |

`player_route` and `0x41c194` are the same function (the "player" records
get it from the constants `0x49991c`/`0x499940`, as mission.md says).

| Method | Hook | Instr. | Calls per suite | Direct callees |
|---|---|---:|---|---|
| `0x41c594` attacking | `attack_step` | 143 | 0/1000/170/521/0/0 | `engage_end` `0x41c478`, `engage_done`, `skirt_check` `0x41de9c`, `target_bearing`, `drive_apply`, `aim_select` `0x41ea7c`, `heading_point`, `point_offset` |
| `0x41d4e4` flanking | `flank_attack` | 72 | 0/0/926/0/1000/0 | `0x41c478`, `engage_done`, `flank_step`, `0x41de9c`, `target_bearing`, `drive_apply`, `0x41ea7c` |
| `0x41d41c` facing off | `face_off` | 74 | 370/947/403/1000/885/0 | as flanking without `flank_step` |
| `0x41c86c` attacking base | `base_attack` | 116 | 170/0/0/1000/0/383 | `0x41c478`, `flank_step`, `0x41de9c`, `target_bearing`, `drive_apply`, `0x41ea7c`, `order_target` |
| `0x41c9cc` attacking flyer | `flyer_attack` | 79 | – | `0x41c478`, `target_bearing`, `drive_apply`, `0x41ea7c` |
| `0x41d7d0` patrolling | `order_scan` | 140 | 1000/1000/1000/1000/0/0 | `target_select`, `script_start`, `group_move`, `attack_replan` `0x41d5ec`, `timer_word_step` |
| `0x41d9cc` travelling | `travel_step` | 78 | 0/0/0/0/1000/0 | `target_select`, `0x41ea7c`, `0x41fb60`, `timer_dword_step`, `heading_point`; slot `+0x3c` at `0x41da90` |
| `0x41daac` following | `follow_step` | 104 | – | `target_select`, `vehicle_control`, `0x41ea7c`, `0x41fac4`, `order_actor`, `timer_dword_step`, `dist_gate`, `heading_point`; slot `+0x3c` |
| `0x41d60c` search/destroy | `group_scan` | 133 | 106/844/212/1000/63/1000 | `target_select`, `script_start`, `group_move`, `0x41d5ec`, `order_target`, `timer_word_step` |
| `0x41c418` sleeping | `sleep_step` | 32 | – | `vehicle_control`, `0x41c3c8` |
| `0x41def0` driving off | `drive_off` | 24 | – | `0x41d41c`, `0x41de9c`; slot `+0x3c` at `0x41e06c` |
| `0x41e570` ramming | `ram_step` | 150 | – | `target_select`, `vehicle_control`, `0x41e488`, `timer_word_step`, `timer_dword_step`, `heading_point`, `rng_bounded` |
| `0x41e488` ramming movement | `ram_move` | 76 | – | `ground_animate`, `ground_collide`, `ground_effects`, `radius_hit`, `terrain_height`; slot `+0x74` at `0x41e537` |
| `0x41d2c4` fleeing | `retreat_fire` | 109 | 0/60/0/50/0/0 | `0x40f09c`, `target_bearing`, `drive_apply`, `0x41ea7c`, `timer_dword_step`, `rng_next` |
| `0x41e554` dead/disabled | `dead_step` | 12 | 592/1000/1000/1000/685/1000 | `vehicle_control` |
| `0x4198f4` player fly movement | `mech_fly` | 697 | 0/0/0/0/1000/0 | `0x408530`, `0x4089bc`, `0x4188c8`, `weapon_converge`, `sweep`, sound `0x46297c`/`0x462ac4`/`0x463114`, `movement_step`, `terrain_height`, `dist3d_delta`, `multiply_q10`, `euler_matrix`, `transform_point`, `dist_gate` |
| `0x41cf18` combat decision | `attack_plan` | 268 | 2/8/5/50/6/4 | `target_select`, `script_start`, `retreat_check`, `part_pick`, `0x41d5ec`, `order_resume`; slot `+0x4c` at `0x41d217` |

### 3.2 FLYER states (`0x499cf8`, stride `0x3c`)

| Record | Name | `+0x18` (slot +18) | `+0x24` (slot +14) | `+0x30` (slot +1c) |
|---|---|---|---|---|
| `0x499cf8` | deciding | – | – | `flyer_decision` 0x422d00 |
| `0x499d34` | attacking | `flyer_attacking` 0x422ca8 | `flyer_move` 0x4218c4 | `flyer_decision` |
| `0x499d70` | patrolling | `flyer_patrol` 0x422b34 | `flyer_move` | `flyer_decision` |
| `0x499dac` | search and destroy | `flyer_search` 0x422a80 | `flyer_move` | `flyer_decision` |
| `0x499de8` | sleeping | – | – | – |
| `0x499e24` | scouting | `flyer_scout` 0x422bdc | `flyer_move` | `flyer_decision` |
| `0x499e60` | dead | – | – | – |

`flyer_move` (`0x4218c4`) is the `+0x24` member of the four active flyer
states (`[0x499ecc]` → `[state+0x24]` at `0x414d36`/`0x414d3c`; the dwords
`0x499ecc`, `0x499ef0`, `0x499f14`, `0x499f5c` are the constant groups'
second member pointers), so it is reached through slot `+0x14`
(`0x4217fc`, called at `0x411d29`), not through `+0x1c`/`0x421888` at
`0x411d0b`. Counts agree: `0x4217fc` and `0x4218c4` 1000/1000/1000/0/0/0,
`0x421888` and its decision method `0x422d00` 14/14/14/0/0/0.

| Method | Hook | Instr. | Calls per suite | Direct callees |
|---|---|---:|---|---|
| `0x422ca8` attacking | `flyer_attacking` | 36 | 1000/1000/1000/0/0/0 | `flyer_follow` `0x422598` (no probe row), `target_emit` |
| `0x422b34` patrolling | `flyer_patrol` | 56 | – | `target_select`, `0x422a50`, `0x422bf0`, `timer_word_step` |
| `0x422a80` search and destroy | `flyer_search` | 63 | 638/638/638/0/0/0 | as patrolling plus `order_target` |
| `0x422bdc` scouting | `flyer_scout` | 9 | – | `0x422a50` |
| `0x422d00` decision | `flyer_decision` | 27 | 14/14/14/0/0/0 | `script_start` |

## `actor_step` and the update-slot trampolines

Eight hooks in group `actor_step` (`tools/reference-game/proxy/actor_step_hooks.h`,
core `core/src/actor_step.c`, `es2/core/actor_step.h`): `actor_step`
`0x411cec`, `state_frame` `0x413eb0`, and the update-slot trampolines
`mech_move` `0x415afc`, `mech_act` `0x415b38`, `mech_decide` `0x415b74`
(MECH `+0x14`/`+0x18`/`+0x1c`), `flyer_move_call` `0x4217fc`, `flyer_act`
`0x42184c`, `flyer_decide` `0x421888` (FLYER). Prologues: `0x411cec`
`55 8b ec 53 8b 5d 08`; `0x415afc`/`0x415b38`/`0x415b74`/`0x42184c`/
`0x421888` `55 8b ec 83 c4 f4 53 8b 5d 08`; `0x4217fc`
`55 8b ec 83 c4 e8 53 8b 5d 08`; `0x413eb0` `55 8b ec 8b 45 08`.

### Callers and references

A call scan of the whole listing finds one caller each: `0x411cec` from
`0x423af5` (`order_update`), `0x413eb0` from `0x411d58`. The six
trampoline addresses occur in the image only as table dwords: file
offsets `0x99496`, `0x9949a`, `0x9949e` (MECH `0x49a282` + `0x14`/`0x18`/
`0x1c`) and `0x997f4`, `0x997f8`, `0x997fc` (FLYER `0x49a5e0` + the same).

### `actor_step` `0x411cec(A)`

Frame: `push ebp; mov ebp,esp; push ebx` (EBP = S−4 for entry ESP S, EBX
saved at S−8); every call pushes one dword (S−0xC) and is followed by
`pop ecx`; EBX = A at the slot and timer calls; ESI/EDI are the caller's
throughout.

1. `[A+0x4d]` = 0 → return with the entry EAX/EDX/ECX (`0x411cf7`).
2. `cmp dword [A+0x52],0; sete al; and eax,1`: when the timer has run
   out, slot `+0x1c` (`0x411d0b`) with EAX = 1, EDX = `[A]` (read here),
   ECX = the entry ECX; ECX ← A.
3. EAX = A+0x4D, EDX = `[A+0x4d]`+8 (the state read again); byte `[EDX]`
   = 0 → `timer_dword_step(A+0x51)` (`0x411d20`) with EAX = A+0x51, EDX =
   state+8; ECX ← A+0x51.
4. Slot `+0x14` (`0x411d29`): EAX = A+0x4D or the timer's EAX, EDX = `[A]`
   (read again), ECX = entry ECX / A / A+0x51; ECX ← A.
5. EAX = EDX = A+0x92. Byte `[A+0xaf]` ≠ 0 → cleared (`0x411d3d`), no
   behaviour. Else slot `+0x18` (`0x411d46`) with EAX = EDX = A+0x92, ECX
   = `[A]` (read again); ECX ← A; when its AX ≠ 0 (`0x411d4d`) EAX ← 0
   and dword `[A+0x52]` ← 0 (on the AX = 0 path EAX keeps the
   behaviour's high word).
6. `0x413eb0(A+0x4d)` (`0x411d58`); ECX ← A+0x4D; `pop ebx; pop ebp`.

Result: EAX/EDX of `0x413eb0`, ECX = A+0x4D; on the early exit the entry
registers. `order_update` returns the last `0x411cec` EAX (its member loop
leaves it), and its only caller `0x45f570` overwrites EAX at once
(`0x45f7fb` `mov ax,[ebp-0x5c]`, then `0x45f809`/`0x45f812`), so the
result is dead there; the hook returns it exactly anyway. `0x411cec` reads
only AX after slot `+0x18`; after `+0x1c` and `+0x14` the pair and ECX are
dead (`0x411d0e`..`0x411d12`, `0x411d2c`..`0x411d33`).

### `state_frame` `0x413eb0(P)`, P = A+0x4D

EDX = ECX = P+0x35 (A+0x82). Byte `[P+0x36]` (A+0x83) ≠ 0 (`0x413ebf`) →
cleared, EAX = P+0x35. Else byte `[P+0x35]` ← 1 and `inc dword [P+9]`
(A+0x56), EAX = P. Frame: only `push ebp`.

### The trampolines `mech_move`, `mech_act`, `mech_decide`, `flyer_move_call`, `flyer_act`, `flyer_decide`

`0x415afc`/`0x415b38`/`0x415b74`/`0x42184c`/`0x421888` (member offset m
= `0x24`, `0x18`, `0x30`, `0x18`, `0x30`): `push ebp; mov ebp,esp; add
esp,-0xc; push ebx`; state = `[A+0x4d]`; the dwords at state+m, +m+4,
+m+8 go to EBP−0xC (function), EBP−8 (delta), EBP−4 (third); ECX = their
OR. OR = 0 (`0x415b24`, `0x415b60`, `0x415b9c`, `0x421874`, `0x4218b0`)
→ EAX = 0, EDX = third (0), ECX = 0. Else EBX = A + delta, `push ebx;
call [ebp-0xc]` with EAX = state, EDX = third, ECX = OR; ECX ← A + delta;
result EAX/EDX of the member.

`0x4217fc` (FLYER movement, m = `0x24`) differs: it first copies the 12
bytes A+0x26..A+0x31 (the position) to EBP−0xC..−4
(`0x421806`..`0x421817`) and never reads them again; the member pointer
lives at EBP−0x18/−0x14/−0x10 (`add esp,-0x18`). Registers at the call:
EAX = the OR, EDX = the state, ECX = the third dword; empty (`0x421838`):
EAX = 0, EDX = the state, ECX = the third dword (0). The result registers
are dead at `0x411d29` as for the other trampolines.

The empty-member path belongs to "deciding" (`+0x18`, `+0x24` empty), "in
limbo", "player"/"player fly" (`+0x30`), "dead"/"disabled" (`+0x30`) and
the FLYER "deciding"/"sleeping"/"dead" records.

Return addresses left below the argument: `0x411d0e`, `0x411d25`,
`0x411d2c`, `0x411d49`, `0x411d5d` (actor_step), `0x415b2d`, `0x415b69`,
`0x415ba5`, `0x421841`, `0x42187d`, `0x4218b9` (trampolines).

### Stack coordinate (why the hooks have entry shims)

These functions sit directly above the AI state methods, and code below
them reads uninitialized stack: the weapon bodies under `aim_fire`
(preparation `0x40e788`, the emitters), the projectile and impact entries
whose shims copy residue in `weapons_hooks.h`, `0x46ff90`, the FLYER emit
`0x4226a0` ([weapons.md](weapons.md);
[ai.md](ai.md#stack-residue-under-aim_fire)). What such a read sees
depends on the coordinate, the saved registers and the bytes the frames above leave. Instead of auditing
every state method's subtree, the replacements are made transparent for the
stack:

- Each of the seven hooks with game calls has an entry shim: it builds the original's frame in place (`push ebp`, the
  trampolines' locals left for the core to write, `push ebx`), reserves a
  gap below it without writing there, puts the context {EBP, ESI, EDI,
  ECX} below the gap and calls the framework's dispatch there. When the
  gap reaches below the thread's committed stack (TIB StackLimit, `FS:8`)
  the pages are probed one by one with `or dword [esp],0`, which keeps
  their bytes; otherwise ESP moves down at once.
- The adapter takes the context (only if the argument above its EBP is
  the call's) and makes every game call through `ai_behaviour_stack_call`
  (`ai_behaviour_hooks.h`): ESP = the original's outgoing coordinate
  (EBP−4 for `0x411cec`, EBP−0x10 for the trampolines, EBP−0x1C for
  `0x4217fc`), EBX/ESI/EDI/EBP as the original holds them at that call
  (the core names EBX and the return address per call; ESI/EDI are the
  caller's), and the original's return address written into the slot
  below the argument after the return.
- The core writes the trampolines' locals into the original frame and,
  for `0x413eb0` (computed in core, a leaf), the three dwords its call
  leaves: the argument at EBP−8, the return address `0x411d5d` at EBP−0xC
  and the saved EBP at EBP−0x10.
- ECX at return goes back through the context, so the shims return the
  original's ECX (dead at `0x411cec`'s pops and at `order_update`'s, but
  exact).

So a state method, `timer_dword_step` and everything below them run at
the original coordinate with the original's saved registers above them
and the original's residue below them, whatever mode these hooks are in.
Remaining differences (accepted, as for the `aim_fire` residue): a
*replaced* callee's own framework frames (its PUSHFD keeps the bridge's
flags, the extended dispatch keeps a call number), which exist only when
that callee is replaced anyway; and a replaced `state_frame` under an
original `actor_step` leaves framework bytes below its entry like every
replaced leaf.

**Gap budget.** A state method's deepest known subtree is `aim_fire`'s:
its shim reserves 256 KiB below its own entry and runs its host frames
below that (an emitter's 128-KiB gap fits inside). The trampoline gap is
80 pages (320 KiB): a stub that fills 288 KiB below a member entry passes
in vitro, 66 pages crash. `actor_step`'s gap is 84 pages (336 KiB),
enough for a trampoline frame, its gap and its host frames (in vitro equal
gaps crash). Under `order_update` the AI step needs about 340 KiB of the
1-MiB reserve (`SizeOfStackReserve` `0x100000`); live headroom is not
measured.

### Contract summary

| Hook | Args | Result | Entry registers read | ECX at return | Shim |
|---|---|---|---|---|---|
| `actor_step` | A | EDX:EAX of `0x413eb0`, or the entry EAX/EDX | ECX (slot `+0x1c` on the run-out path); all three on the early exit | A+0x4D or the entry ECX (through the shim) | gap 84 pages, EAX/EDX/ECX kept |
| `state_frame` | P | EAX = P or P+0x35, EDX = P+0x35 | none | P+0x35 in the original, the entry ECX replaced (dead: `0x411d5d` pops it) | none |
| trampolines | A | the member's EDX:EAX, or EAX = 0, EDX = the third dword (`0x4217fc`: the state) | none (all three written first) | A + delta, 0 or the third dword (through the shim) | gap 80 pages, EAX scratch |

Writes: `[A+0x52]` (dword, decision request), `[A+0xaf]`, `[A+0x82]`,
`[A+0x83]`, `[A+0x56]`, and the original frames on the stack. All other
effects are in the slots, the state methods and `timer_dword_step`, which
stay behind the guest bridge (nested hooks keep their modes). A replaced
`actor_step` does not enter the `state_frame` hook (it computes `0x413eb0`
in core). All eight refuse compare mode. The state methods were never
checked for entry-register reads, so the guest calls reproduce the entry
registers exactly.

### Actor step validation (in vitro)

`tools/reference-game/invitro/actor_step_cases.h`, group `actor_step`
(`differential_harness <hook>`):

- 1,001,024 direct cases per hook (quick tier 100,000): the original body
  against the core with logging stubs for the slots (two arena vtables),
  the member pointers (arena state records; empty in a quarter of the
  cases, deltas 0, ±4, 8, ±16 or random, third dword 0 or random) and
  `timer_dword_step` (patched); `0x413eb0` runs as original code under
  `actor_step`. The stubs change what the original reads again (state
  pointer, vtable, timer, skip and once bytes) and return AX = 0 or not.
  Compared: the pair, the ordered calls with argument and entry
  EAX/EDX/ECX, the arena, the callee counters (first 4,096 cases).
- 256 installed 31/a7 cases per hook (fast and preserving dispatch,
  nonempty x87) with distinct EBX/ESI/EDI: also ECX (through the shims),
  EBX/ESI/EDI/EBP/ESP at every stub entry relative to the caller, a hash
  of the 512 bytes below every stub's ESP at entry, and the 512 bytes
  below the argument after the return.
- 3,072 real cases per hook: `0x411cec` entered with the real MECH or
  FLYER table (so the real trampolines run), the hook under test original
  in one run and replaced in the other, the other seven hooks and
  `timer_dword_step` in the same rotating mode in both runs (installed
  afresh); members are tracked stubs (a quarter of the cases fill 288 KiB
  below their entry) or the real `timer_dword_step` on a timer record.
  Pair, ECX, trace and arena always; the stack (registers, window hashes,
  the 512 bytes below the argument) in the two rotations where
  `timer_dword_step` and `state_frame` are original.
- 12 branch sites, all both ways (`0x411cf7`, `0x411d06`, `0x411d1a`,
  `0x411d39`, `0x411d4d`, `0x413ebf` and the six empty-member tests).
- Adapters without the stack context fail the installed probes at case 0
  (EBX/ESI/EDI/EBP/ESP at the first stub); a 66-page trampoline gap and an
  80-page `actor_step` gap crash in the real pass.
- Nine mutation witnesses, at least one per hook
  ([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).

### Actor step counters

`actor_step`, `mech_move`, `mech_act` ≥ 1000 in all six suites;
`mech_decide` ≈ 3/7/5/38/9/8; `flyer_move_call`, `flyer_act`
1000/1000/1000/0/0/0; `flyer_decide` 14/14/14/0/0/0; `state_frame` 0
while `actor_step` is replaced. Relations: `hk_actor_step_slot_14` =
`hk_mech_move_calls` + `hk_flyer_move_call_calls`;
`hk_actor_step_slot_18` = `hk_mech_act_calls` + `hk_flyer_act_calls`;
`hk_actor_step_slot_1c` = `hk_mech_decide_calls` +
`hk_flyer_decide_calls`; `hk_actor_step_call_467aa8` ≤
`hk_actor_step_slot_14` ≤ `hk_actor_step_calls` =
`hk_order_update_call_411cec`; `hk_flyer_move_call_member_24` =
`hk_flyer_move_calls` (the four active FLYER states hold `flyer_move` at
`+0x24`, the others nothing).

## `base_update`: contract

Six functions of the building update, five of them hooks in group `base`
(`tools/reference-game/proxy/base_hooks.h`, core `core/src/base_update.c`,
`es2/core/base_update.h`). Read from the listing entry-aligned from
`0x403668` to `0x403d5a`; every statement below is tested in vitro
(`differential_base_test`). Pointers and counts are reread where the
original rereads them; the core follows that order.

### Hooks and prologues

| Hook | Address | Prologue (verified bytes) | Relocate / lengths | ABI | Frame |
|---|---|---|---|---|---|
| `base_update` | `0x403ca8` | `55 8b ec 53 56 57 8b 5d 08` | 5 / 1,2,1,1 | cdecl (B) | none |
| `base_parts` | `0x403914` | `55 8b ec 83 c4 9c b8 85 74 49 00` | 6 / 1,2,3 | cdecl (B) | 64h; runtime record f−2Ch (descriptor `0x497485`, `[+4]` = −2Ch) |
| `base_part_chain` | `0x403890` | `55 8b ec 83 c4 f8 53 56 57` | 6 / 1,2,3 | cdecl (B, k) | 8 |
| `base_part_blast` | `0x4036c0` | `55 8b ec 83 c4 c8 53 56 57` | 6 / 1,2,3 | cdecl (B, P_i, kind, offset) | 38h; runtime record f−24h (descriptor `0x497435`, `[+4]` = −24h) |
| `base_part_debris` | `0x40379c` | `55 8b ec 83 c4 ec 53 56 57` | 6 / 1,2,3 | cdecl (B, Q_i) | 14h |

`0x403668` ("every fire part destroyed", 31 instructions, pure, only
caller `0x403914`) is computed inside the `base_parts` core
(`es2_base_parts_done`); when `base_parts` is original it reaches the hook
`parts_intact` of group `sim_leaves` ([Simulation leaves](#simulation-leaves)).

Direct callers (listing): `0x403ca8` ← `turret` `0x404119`, `0x4045c8` at
`0x4045ea`, `convoy_think` `0x46a701`/`0x46a752`, and slot `+0x18` from row
10 (`0x45f7a6`); `0x403914` ← `0x403cb2`, `turret` `0x404125`, `0x4045c8`
at `0x4045f6`; `0x403890` ← `0x403b32`, `0x403c73` and itself
(`0x4038ee`); `0x4036c0` ← `0x403ab0`, `0x403ace`; `0x40379c` ←
`0x403ad8`.

Stay original behind the bridge: FIRE creation `0x46b430`/`0x46b48c`
(sound `0x462830` inside), debris `0x408530`/`0x4089bc`, the point
transform `0x4801d4` (hook `matrix_point`, [dts.md](../formats/dts.md)),
RTL `0x49630c`; hooks used as callees: `timer_word_step`, `actor_animate`,
`rng_bounded`, `euler_matrix`, `transform_point`, `pool_alloc`,
`explosion_create`, and slot `+0x74` (`building_part`) from `0x4038e6`.

**ECX is outside every contract.** All callers of `base_update` and
`base_parts` `pop ecx` right after the call; after `base_part_chain` ECX
is rewritten (`0x403c8a`, `0x403904`) before any read; after
`base_part_debris` at `0x403c8a`, `0x403b21` or `0x403afe`. The one flow:
`base_part_blast`'s ECX (the saved FS:0 head) becomes
`base_part_debris`'s entry ECX and reaches only `euler_matrix`'s entry ECX
(`0x4037bc`), which `euler_matrix` overwrites with `mov cl,3` before any
read; its result ECX is rewritten at `0x4037d0`. EDX:EAX is exact for all
five. `base_update`'s EDX:EAX flows out as the return of `turret`,
`convoy_think` and `0x4045c8` (each jumps to its epilogue right after the
call: `0x40411f`, `0x4045f0`, `0x46a707`); row 10 ignores it, but the
replaced callers compare EDX:EAX exactly.

### `0x403ca8(B)` base_update

1. `base_parts(B)` with the entry EAX/EDX/ECX (unchanged up to
   `0x403cb2`). Then ECX = B, EAX = B+92h.
2. Byte `[B+99h]` ≠ 0 (`0x403cc2`): return EAX = 0, EDX = `base_parts`'s
   EDX (which is C, see below).
3. EDX = C. `s16(C+24h)` ≤ −1 (`0x403cd3`): go to 5.
4. `timer_word_step(B+1F6h)` (EAX = B+92h, EDX = C, ECX = B+1F6h); ECX =
   B+1F6h; EAX = (AX = 0). AX ≠ 0 (`0x403cee`): go to 5 with EAX = 0 and
   the timer's EDX. Otherwise word `[B+1F7h]` ← `[C'+26h]` (C' reread);
   then, with C'' reread, M = `[B+34h]`, s = `u16(C''+24h)` (in CX), F =
   `[M+8]`, L = `[M+4]`, counts = `[L+20h]`: `F[s]` ← `(u16 F[s] + 1) mod
   u16 counts[s]` (signed IDIV of non-negative values at `0x403d29`; a zero
   count raises #DE: precondition), stored through `[M+8]` reread; EAX =
   s, EDX = `[M+8]` reread after the store.
5. ECX = `[B+1F2h]` reread. Word `[ECX+6]` ≠ 0 (`0x403d46`):
   `actor_animate(B, 64h)` with the current EAX/EDX/ECX (ECX's high word
   reaches the third slot of `0x40250c`).
6. Return EAX = 0, EDX = the last EDX (C, the timer's EDX, `[M+8]` or
   `actor_animate`'s EDX).

### `0x403914(B)` base_parts

Entry: `0x49630c` (EAX = `0x497485`, entry EDX/ECX) links the record at
f−2Ch into FS:0 and returns the old head. P and Q are read once; the
detail byte `[0x4d1f43]` is read once (zero-extended, in EAX). Loop over i
while `s16(C+12h)` > i (C and the count reread at `0x403c84`: EDX = C, ECX
= the count; P += 0Bh, Q += 1Eh per part):

- Stage word `[P+5]` = 0 (`0x403952`): next part.
- `timer_word_step(P+2)` (EAX = the previous EAX, EDX = C, ECX = P+2);
  ECX = P+2, EAX = (AX = 0); AX ≠ 0 (`0x40396e`): next part. Otherwise
  `[P+5]` −= 1 and the stage is reread:
- **Stage 0** (`0x40397d`; EAX = C): if `[C+4]` = FFFFh, `[Q+2]` = FFFFh
  and `[C+12h]` = 1 (`0x40398e`, `0x403995`, `0x40399c`): byte `[B+38h]` ←
  1, dword `[B+2Eh]` ← −100000 (EAX = B+38h; the sink, only for a
  single-part object without models). Else EDX = C reread; if `s16(C+8)` >
  −1 and `s16(Q+6)` > −1 (`0x4039bb`, `0x4039c2`): `0x403668` (below), ECX
  = B; not all destroyed (`0x4039cd`): **no FIRE** (not the part's own);
  all destroyed: `0x46b430(5Bh)` (EAX = 1, EDX = the scan index, ECX = B),
  ECX = 5Bh, slot f−4; a non-null object (`0x4039e0`) gets
  `0x46b48c(obj, B, −1, C+0Ah, u16(C+8))` (EAX = C+0Ah, EDX = obj, ECX =
  `lo(5Bh, C+8)`), then the construction count `[[0x6cbee4]]` −= 4 (ECX =
  its pointer). Else (no object fire entry), if `s16(Q+6)` > −1
  (`0x403a29`): the part's own FIRE, `0x46b430(5Bh)` (EAX = EDX = C, ECX =
  P+2), slot f−8; non-null (`0x403a3c`): `0x46b48c(obj, B, −1, Q+0Ah,
  EDX)` with EDX = `lo(46b430's EDX, Q+6)` (EAX = obj, ECX = Q+0Ah), count
  −= 4 (EDX = its pointer). Context word f−1Ch: 14h/8 and 2Ch/20h around
  the two constructions.
- **Stage 1** (`0x403a79`): done = `0x403668`; ECX = B; EAX = C. If
  `s16(C+8)` > −1, `s16(Q+6)` > −1 and done (`0x403a94`, `0x403a9b`,
  `0x403aa1`): `base_part_blast(B, P, s16(C+10h), C+0Ah)` (EAX = C, EDX =
  C+0Ah, ECX = the kind). Else EAX = `lo(C, Q+4)`; `s16(Q+4)` > −1
  (`0x403ac2`): `base_part_blast(B, P, s16(Q+4), Q+0Ah)` (EDX = Q+0Ah,
  ECX = the kind). Then `base_part_debris(B, Q)` with the registers as they
  are. If done (`0x403ae4`) and `s16(C+4)` > −1 (`0x403af1`; EAX = C):
  `[M+4]` ← `[[0x4a9584] + 4·s16(C+4)]`, the destroyed model (EAX = it,
  EDX = M, ECX = the index). Else EAX = `lo(EAX, Q+2)`; `s16(Q+2)` > −1
  (`0x403b18`): word `F[u16(Q+2)]` ← 1 (ECX = M, EAX = F = `[M+8]`) and
  `base_part_chain(B, i)` (EDX = i).
- **Stage ≥ 2 or < 0** (`0x403b3f`..`0x403b65`): a puff only when the detail byte is
  2 (`0x403b43`), or 1 (`0x403b49`) and the low byte of the stage word
  is odd (`0x403b53`). Three `rng_bounded(0x4d2599, ECX)` on stream A with
  ECX = `2·lo(ECX, Q+16h+2k)` (ECX: P+2, then each draw's result ECX; the
  callee reads the low word only, so the bound is `s16(2·word mod
  10000h)` and a zero bound, word 0 or 8000h, faults), offsets
  `s16(AX) − s16(Q+16h+2k)` (EDX = the subtrahend), plus
  `s16(Q+10h/12h/14h)` (EAX = Q+10h, then the adds in the order Z, X, Y
  via EDX, ECX, EAX), stored at f−64h. `[B+32h]` = 0 (`0x403bde`):
  `euler_matrix(B+0Ch, B+12h)` (EAX = ECX = B+0Ch, EDX = B+12h), `[B+32h]`
  ← 1. `transform_point(f−64h, B+12h, f−58h)` (EAX = B+12h, EDX = f−58h,
  ECX = f−64h). `pool_alloc([0x4a961e])` (EAX = the pool), ECX = the pool,
  slot f−0Ch; non-null (`0x403c2a`): `explosion_create(obj, lo(obj,
  [0x497414 + 2·s16(Q+4)]), f−58h, B, 1)` (EDX = obj, ECX = `s16(Q+4)`),
  count −= 4 (ECX = its pointer), context words 44h/38h. With the puff
  only: word `[P+3]` ← 300 (the timer re-arm); without it the timer stays
  expired and the next frame decrements again, so with the detail byte 0
  the stages ≥ 2 run down one per frame. Then, puff or not, `[P+5]` = 4
  (`0x403c6c`, reread): `base_part_chain(B, i)` (EAX = i).

Return: EAX = the record's saved head `[f−2Ch]`, EDX = C as reread at the
last loop test (on every path, so `base_update`'s EDX on the dead path is C
as well), ECX = the count; FS:0 ← the head. `turret` passes this EAX as
`timer_word_step`'s entry EAX, which that function overwrites.

**The detail byte.** RNG stream A consumption depends on `[0x4d1f43]`, so
the replacement reads the byte and never assumes a value. It is byte `+0xb`
(prefs byte 11) of the 54-byte option block `0x4d1f38`; there is no
absolute write in the listing (indexed stores in the options code
`0x45994f`, `0x459971`, `0x459b38`); readers `0x40393b`, `0x408dd7` (the
debris effects gate at `0x408dde`, [main_loop.md](main_loop.md)),
`0x45738f` (options display), `0x4627e9` (sound). It is 2 in the suites
except `c3_10-detail-options`, where the options dialog's action 8 steps it
modulo 3 (`0x4599d4(0Bh, 3, 1)`) to 1 and back
([live_coverage.md](../reference-game/live_coverage.md#base_parts)).

`0x403668(B)` (pure): j from 0 while j < `s16([B+1F2h]+12h)` (reread per
step); stop at the first part with `s16(Q_j+6)` > −1 and `s16(P_j+0)` <
`s16(Q_j+0)` (`0x403688`, `0x403690`, `0x4036a5`). EAX = (j = the reread
count), EDX = j, ECX = the count.

### `0x403890(B, k)` base_part_chain (recursive)

R = `[B+205h]` + 11·k (EAX before the loop). For j while j <
`s16(C+12h)` (C reread per step; EDX = C, ECX = the count; P_j/Q_j advance
by 0Bh/1Eh from `[B+205h]`/`[C+14h]`, read once): if `s16(Q_j+1Ch)` = k
(`0x4038ca`; EAX = that) and `s16(P_j+0)` < `s16(Q_j+0)` (`0x4038d8`; EAX =
`lo(k, Q_j+0)`, ECX = `lo(ECX, P_j+0)`): slot `+0x74`(B, j, EAX, `[R+7]`)
(EAX = the vtable, EDX = R, ECX = `[R+7]`; `building_part` `0x404d70` for
every building class), then `base_part_chain(B, j)` with the slot's result
registers. Return EAX = the last EAX, EDX = C; ECX = the popped walk
pointer (dead).

### `0x4036c0(B, P_i, kind, offset)` base_part_blast

`0x49630c` (EAX = `0x497435`; record f−24h, context word f−14h). Type =
`s16([0x4973fc + 2·kind])` at f−28h. Word `[0x49740c + 2·kind]` ≠ 0
(`0x4036ed`): the point f−38h ← B's position (read in the order +2Eh,
+26h, +2Ah). Else, `[B+32h]` = 0 (`0x40370d`): `euler_matrix(B+0Ch,
B+12h)` (EAX = B+12h, EDX = B+0Ch, ECX = the entry ECX), `[B+32h]` ← 1;
`0x4801d4(offset, B+12h, f−38h)` (EAX = B+12h, EDX = offset, ECX = f−38h).
`pool_alloc([0x4a961e])`, ECX = the pool, slot f−4; non-null
(`0x40374d`): `explosion_create(obj, lo(pool, type), f−38h, B, 1)` (EAX =
obj, EDX = f−38h), count −= 4 (EDX = its pointer), context words 14h/8.
Word `[P_i+3]` ← `[0x497404 + 2·kind]` (re-arm). Return EAX = `lo(EAX,
that word)` (EAX = `explosion_create`'s EAX or 0), EDX = P_i; ECX = the
saved head, FS:0 ← it. Image tables for kinds 0..3: types 16/21/15/15,
"at the position" 1/0/0/0, re-arm 2000/0/0/0; the kind is any signed word
and indexes the image as in the original.

### `0x40379c(B, Q_i)` base_part_debris

`[B+32h]` = 0 (`0x4037b3`): `euler_matrix(B+0Ch, B+12h)` (EAX = B+12h,
entry EDX/ECX), `[B+32h]` ← 1. `0x4801d4(Q_i+0Ah, B+12h, f−14h)`. Dword
`[0x4a964c]` ← `0x4a95c0`. Debris rounds with the words w0 = `[Q_i+8]` at
f−2 (EAX = `lo(EAX, it)`), w1 = 0Ah at f−4, w2 = 0Ch at f−6; rounds 2 and 3
only when `s16(Q_i+8)` > 5 (`0x403821`, reread after round 1). A round: R
= `0x4089bc(&w)` (EDX = &w in round 1, EAX = &w in rounds 2 and 3),
ECX = &w, then `0x408530([R] + 10·s16(w), [R+6], f−14h, 0)` with w reread
after `0x4089bc` (which lowers it by the count `[0x4a9654]` when it picks
the second table `[0x4a964c]`), reading `[R+6]`, w, `[R]` in that order,
EAX = the first argument, EDX = 10·s16(w), ECX = s16(w). Return
`0x408530`'s EDX:EAX.

### Stack audit and residue

No frame local of the five functions is read before it is written (the
points are written in full by the copy, `0x4801d4` or `transform_point`;
the type, slots and words before their uses). The original callees that
stay behind the bridge read no uninitialized stack of their own:
`0x4801d4` has no locals (its nonrotating path returns the entry EDX/ECX),
`0x4089bc` none, `0x408530` clears its 20h-byte local with `memset`,
`0x46b48c` has none, `0x49630c` only writes its record. `0x46b430`
returns an uninitialized local in ECX when its first allocation succeeds
(`pop ecx; pop ecx` at `0x46b487`); `base_parts` discards it (`pop ecx` at
`0x4039da`/`0x403a36`). So no entry shim and no fixed outgoing stack
coordinate is needed. The callees run from host frames, so the stack bytes
below row 10 differ after the step; a later original reader of stale
stack would show in the live regression.

### Building update liveness

`0x403ca8`, `0x403914`: 1000 ×6 (saturated). `0x403668`: 0/0/0/13/10/0.
`0x4036c0`, `0x40379c`: 0/0/0/12/9/0. `0x403890`: 0/0/0/12/6/0 (with the
recursion). `0x46b430`/`0x46b48c` (also called from `0x40d434`):
1/7/0/27/6/5. `0x4089bc` and `0x408530` have six other callers each. The
suites without building destruction exercise only the timer/animation
part; the destruction stages rest on the in-vitro tiers (stage values,
detail byte 0/1/2, empty pools, zero counts).

## Actor virtuals

Eleven hooks, one per function, in group `actor_virtuals`
(`tools/reference-game/proxy/actor_virtuals_hooks.h`; core
`core/src/actor_virtuals.c`, `es2/core/actor_virtuals.h`). Every statement
below is tested in vitro (`differential_actor_virtuals_test`).

### Actor virtual hooks

| Hook | Address | Slot | Prologue (verified bytes) | Relocate / lengths | Args |
|---|---|---|---|---|---|
| `base_aim_node` | `0x403548` | BASE +24 | `55 8b ec 53 56 8b 5d 08 be cc 95 4a 00` | 5 / 1,2,1,1 | A |
| `base_offsets` | `0x40351c` | BASE +30 | `55 8b ec 53 56 8b 75 10 8b 5d 08` | 5 / 1,2,1,1 | A, first, second |
| `base_condition` | `0x4052b4` | BASE +40 | `55 8b ec 51 53 56 57 8b 5d 08 33 c9` | 5 / 1,2,1,1 | A |
| `actor_attacked` | `0x411aec` | ACTOR +50 | `55 8b ec 8b 45 08 8b 55 0c 52 50 e8` | 6 / 1,2,3 | A, attacker |
| `base_part_position` | `0x406808` | BASE +58 | `55 8b ec 51 53 56 57 8b 75 08 8b 7d 10` | 5 / 1,2,1,1 | A, part word, out |
| `mech_aim_node` | `0x417b98` | MECH +24 | `55 8b ec 51 53 56 57 8b 75 08 8b 86 f2 01 00 00` | 5 / 1,2,1,1 | A |
| `mech_offsets` | `0x4155c4` | MECH +30 | `55 8b ec 53 8b 45 08 8b 4d 10` | 7 / 1,2,1,3 | A, first, second |
| `mech_armour` | `0x4154d0` | MECH +34 | `55 8b ec 8b 55 08 8d 82 22 02 00 00` | 6 / 1,2,3 | A, angle word |
| `mech_target_scale` | `0x415498` | MECH +38 | `55 8b ec 53 56 8b 5d 08 8b 83 f2 01 00 00` | 5 / 1,2,1,1 | A |
| `mech_engage_roll` | `0x41cabc` | MECH +4c | `55 8b ec 53 56 8b 45 0c 8b 5d 08` | 5 / 1,2,1,1 | A, target T |
| `mech_link_position` | `0x41dd2c` | MECH +64 | `55 8b ec 53 8b 45 08 8b 90 a4 01 00 00` | 7 / 1,2,1,3 | A |

All cdecl. A raw dword scan of the image finds each address only in its
class tables (`0x403548`, `0x40351c`, `0x4052b4`, `0x406808` in the five
building tables; `0x411aec` in those, at ACTOR +50 `0x49a59c` and FLYER
+50 `0x49a630`; the MECH functions once each). The only direct call is
`0x411aec` from `0x41f853` inside MECH +50 `0x41f7b8` (`mech_attacked`,
which reaches this hook through the bridge). `0x40351c` has no row in the
DBSIM map (not probed; sites `0x4042d4` in `turret`, `0x40473c`,
`0x41eafd`; `0x411a74`, which it calls, counted 140/356/230/0/0/0, also
through FLYER +30).

### Bodies

- **`0x403548` base_aim_node.** The guard byte `[0x497424]` is incremented
  only while it is 0 (`0x40355c`; nothing else reads it). `memset(0x4a95cc,
  0, 20h)` (import thunk `0x49681a`) with the entry EAX/EDX/ECX. Then AX ←
  4000h (memset's high word stays), words `+10h`, `+6`, `+0` of the record
  ← 4000h (in that order), EAX = `0x4a95cc`, EDX = C, ECX = `s16(C+2Ch)`,
  stored as dword `+1Ch`. A static 32-byte node record rewritten on every
  call (the returned pointer aliases between calls, as in the original).
- **`0x40351c` base_offsets.** `0x411a74(A, first, second)` (ACTOR +30:
  `memset(first, 0, 6)`, `memset(second, 0, 6)`) with EAX = first and the
  entry EDX/ECX; then EDX = C (read after the call), CX ← `C+2Ch` over
  411a74's ECX, word `second+4` ← it. EAX = 411a74's EAX.
- **`0x4052b4` base_condition.** P = `[A+205h]` (stride 0Bh) and Q =
  `[C+14h]` (stride 1Eh), read once; for i while i < `s16(C+12h)` (C and
  the count reread per step, signed 32-bit compare at `0x4052f6`): current
  += `s16(P_i+0)`, maxima (the frame dword EBP−4: the pushed ECX, cleared
  first) += `s16(Q_i+0)`. `EAX = current << 8; CDQ; IDIV maxima`
  (`0x4052fe`): EAX = quotient, EDX = remainder, ECX = maxima (`pop
  ecx`). **The IDIV faults** on maxima 0 (no parts, a non-positive count,
  or cancelling maxima: #DE divide by zero) and on `80000000h / −1` (a
  current sum of ±800000h, e.g. 256 parts at 8000h, over maxima −1: #DE
  overflow). The core does not divide then; the adapter executes an `IDIV`
  with the same operands, which raises the same exception.
- **`0x411aec` actor_attacked.** `contact_share(A, attacker)` (`0x412704`,
  hook) with EAX = A, EDX = attacker, ECX = the entry ECX; its registers.
  Callers pass a third (at `0x410cb6` a fourth) slot that is not read.
- **`0x406808` base_part_position.** The frame dword EBP−4 holds the entry
  ECX (`push ecx`). Word `[A+32h]` = 0 (`0x40681d`): EBP−4 ← A+0Ch,
  `euler_matrix(A+0Ch, A+12h)` (EAX = A+12h, EDX = A+0Ch, ECX = entry
  ECX), word `[A+32h]` ← 1 after the call. Then ECX = `30·s16(part)`, EDX
  = `[C+14h]` (C read after `euler_matrix`) + ECX + 10h, EAX = A+12h;
  `0x4801d4(EDX, A+12h, out)`; its EDX:EAX. ECX at return = EBP−4 (entry
  ECX or A+0Ch). `0x4801d4` (a pure 106-instruction transform with an
  `enter` prologue, [dts.md](../formats/dts.md)) is called through the
  bridge: its paths return EAX = out or a data value and EDX from data or
  the entry EDX, never from its own frame, so EDX:EAX is exact.
- **`0x417b98` mech_aim_node.** The frame dword EBP−4 holds the entry ECX;
  DX ← `C+0Ch` (entry EDX high word kept), the word is stored to EBP−2 (the
  high half of that dword) and reloaded into CX (entry ECX high word
  kept). S = `[A+34h]` read once, L = `[S+4]`; the shape's slot `+20h(L,
  ECX)` with EAX = L, EDX = `[L]`, ECX as pushed (the word argument
  carries the entry ECX's high word). Result N: N ≠ 0 and `s16(N+4)` > −1
  (`0x417bc8`, `0x417bcf`): EDX = `s16(N+4) << 5`, EAX = `[S+16h]` (read
  after the call) + EDX; else EAX = `0x6c56a8` (the default node record)
  and EDX the slot's EDX. ECX at return = `word << 16 | entry CX`.
- **`0x4155c4` mech_offsets.** E = C+2. Word `[second]` ← 0, word
  `[first]` ← 0, then first+2 ← `C+64h`, first+4 ← `C+66h`, second+2 ←
  `C+68h` (via DX: EDX = `lo(first, …)`), second+4 ← `C+6Ah` (via AX: EAX
  = `lo(E, …)`), each read right before its store (the vectors may alias
  the configuration or each other). ECX = second.
- **`0x4154d0` mech_armour.** DX ← the angle word (EDX keeps A's high
  word); `DX + 4000h < 8000h` (unsigned, `SETB`) selects the word
  `[A+222h]`, else `[A+224h]` (`0x4154f3`). EAX = EDX = `lo(A, word)`, ECX
  = the 0/1 test.
- **`0x415498` mech_target_scale.** EAX = C; word `C+50h` ≠ 0
  (`0x4154ab`): EAX = `[A+2BDh]`, EDX/ECX the entry values. Else
  `multiply_q10(2000, s16(A+28Eh))` (EAX = C, entry EDX/ECX); its
  registers.
- **`0x41cabc` mech_engage_roll.** Word `T+1A8h` ≠ 0 (`0x41cacf`): EAX =
  0 with the entry EDX/ECX, no draw. Else `rng_next(0x4d2599)` (stream A;
  EAX = T, entry EDX/ECX); a = `(draw & 3E8h) + [A+29Eh]` in AX (the word
  read after the draw, the draw's high word kept); second `rng_next`
  (EAX = a, EDX = the first draw's EDX, ECX = `0x4d2599` from the `pop
  ecx`); d = `(draw2 & 3E8h) + [T+29Eh]` in DX (read after the draw, the
  draw's high word kept); AX = a − d. AX = 8000h (`0x41cb0d`) → CX =
  7FFFh; AX ≥ 0 (`0x41cb18`) → ECX = EAX; else ECX = EAX with CX negated.
  `s16(CX)` < 400 (`0x41cb28`) → AX = 1; else `s16(d)` ≤ `s16(a)`
  (`0x41cb33`) → EAX = 0; else AX = 2. EDX = d on every drawing path.
- **`0x41dd2c` mech_link_position.** L = `[A+1A4h]`; 0 (`0x41dd3b`): EAX =
  A, EDX = 0, ECX entry. Else dwords A+326h ← L+2Eh, A+31Eh ← L+26h,
  A+322h ← L+2Ah (each read right before its store), byte `[A+ADh]` ← 1;
  EAX = A+92h, EDX = A+31Eh, ECX = the dword L+2Ah.

No local is read before it is written (EBP−4 of `0x4052b4` is cleared
before the loop; those of `0x406808` and `0x417b98` only feed ECX at
return), and no original callee reads uninitialized stack of its own
(memset, `0x411a74`, `0x4801d4`, `multiply_q10` and `rng_next` have no
locals; `contact_share` and `euler_matrix` are hooks with checked
contracts). So no entry shim.

### `mech_armour` 0x4154d0 and the front flag

The only actor site `0x41f39f` (in `0x41f358`) computes `AX = angle +
4000h; CMP AX,8000h; SETB DL; AND EDX,1` itself and pushes EDX, a 0/1
front flag ([ai.md](ai.md)), where `0x4154d0` expects an angle. Both 0 and
1 (with any high word) lie in `[−4000h, 4000h)`, so the original **always
returns the front word `[A+222h]` from that site**; `[A+224h]` is never
selected there. In vitro (`mech_armour` front-flag probe): 4,096 calls of
the original body with the flag 0 or 1 under random high words and
distinct front and rear words all return `+222h`; angles outside the
sector return `+224h`. The hook keeps this behaviour (it is the
original's; that the caller meant to pass the angle is a hypothesis).

### Result registers at the call sites

The framework returns EDX:EAX from the core and restores the entry ECX
and flags. EDX:EAX is exact everywhere (callee registers come back through
the bridge). ECX and the flags were checked at every actor site of
section 2:

- +24 (`0x412671`, `0x41267b`, `0x41dc60`, `0x41dc6f`, `0x41ea4c`): `pop
  ecx` after each call; flags written (`cmp`, `add`) before any read.
- +30 (`0x4042d4`, `0x40473c`, `0x41eafd`): `add esp`, then ECX loaded by
  `movsx` before use.
- +34 (`0x41f39f`): `mov [ebp-4],ax; xor eax,eax`; CX reloaded at
  `0x41f3b4`, all of ECX at `0x41f3ce` (`sete cl; and ecx,1`) before any
  full read.
- +38 (`0x404305`, `0x41f654`, `0x422705`): `pop ecx` (and `mov ecx,eax`
  at `0x41f65d`).
- +40 (`0x404e8e`, `0x412cdc`, `0x412e27`, `0x41cc4f`): `pop ecx`.
- +4c (`0x41221f`, `0x41d217`): ECX written before it is read on every
  path (`0x41223e`, `0x4122c4`, `0x412301`; `0x41d247`; the path to
  `0x41d2a4` enters `0x41ce08`, which does not read its entry ECX).
- +50 (`0x426801`, `0x410cb6`, direct `0x41f853`): as recorded for
  `contact_share` in [vehicle.md](vehicle.md#contact_share-412704) (ECX
  written at `0x42680a` and `0x410c11`; out of `0x41f7b8` to its callers,
  which write it first).
- +58 (the nine sites `0x404fba`, `0x4068ac`, `0x40a2fd`, `0x40e530`,
  `0x4106cd`, `0x418872`, `0x41b569`, `0x41ea1b`, `0x41ead6`): `add esp`
  first; ECX as walked for `part_position` in
  [vehicle.md](vehicle.md#ecx-after-return) (the same sites).
- +64 (`0x4269f4` in `sweep` `0x426528`): `add esp`; ECX is not written
  again before `sweep` returns, and the callers of `sweep` write ECX first
  ([weapons.md](weapons.md), "ECX at return" of `explosion_create`).

### Callees and modes

Through the weapons guest bridge with the original's entry registers:
`contact_share` (and below it `dist_gate`, `trigger_fire`),
`euler_matrix`, `multiply_q10` and `rng_next` are hooks in their own modes
(the stream A draws stay in `rng_next`'s mode); memset (CW3220 import
thunk `0x49681a`), `0x411a74`, `0x4801d4` and the shape's slot +20h run as
original code. No sound, clock or allocator call. The short getters of
section 2 (≤ 12 instructions) are the separate group `actor_leaves`
([Actor slot leaves](#actor-slot-leaves)).

## `lc_base_update`: contract

Three hooks in group `lc_base` (`tools/reference-game/proxy/lc_base_hooks.h`,
core `core/src/lc_base.c`, `es2/core/lc_base.h`). Read from the listing
entry-aligned from `0x4045c8` to `0x404ae1`, `0x40a9c4` to `0x40ab17` and
`0x40fe84` to `0x40fe9e`; the image tables were read from the file. Every
statement below is tested in vitro (`differential_lc_base_test`); the
outline in [weapons.md](weapons.md#related-building-emitter-4045c8) agrees
with it. Names: B the `LC_BASE` object, K the three banks B+209h+1Fh·i, S
= K+8j their three weapon records, W = `[0x4a95bc]`+16h·j the three mount
records. Roles are hypotheses (a building with three turrets of one cannon
and two lasers each).

### Hooks, prologues and callers

| Hook | Address | Prologue (verified bytes) | Relocate / lengths | ABI | Frame |
|---|---|---|---|---|---|
| `lc_base_update` | `0x4045c8` | `55 8b ec 81 c4 40 ff ff ff` | 9 / 1,2,6 | cdecl (B) | C0h; EBX/ESI/EDI saved at f−C4h/−C8h/−CCh |
| `projectile_create` | `0x40a9c4` | `55 8b ec 83 c4 d8 b8 70 83 49 00` | 6 / 1,2,3 | cdecl (type, position, angles, owner, word) | 28h; runtime record f−24h (descriptor `0x498370`); saves at f−2Ch/−30h/−34h |
| `weapon_record` | `0x40fe84` | `55 8b ec 0f bf 45 08` | 7 / 1,2,4 | cdecl (index) | none |

Callers (listing; no other direct CALL): `0x4045c8` only through the
`LC_BASE` slot +18h (`0x49779c`), dispatched by row 10 at `0x45f7a6`,
which pops ECX and ignores EDX:EAX. `0x40a9c4` ← `turret` `0x404542` and
`0x40458e`, `0x4045c8` at `0x40499d`, `weapon_fire` `0x40e9c5`,
`target_emit` `0x422994` (all four callers are hooks that reach it
through the guest bridge). `0x40fe84` ← `0x4045c8` at `0x404a0c` and the
weapon factory `0x40fff8` at `0x410098` (original code).

### `0x40fe84(i)` weapon_record

EAX = `[0x4a98ec]` + 58h·`s16(i)` (the weapon definition record, stride
88), EDX = `s16(i)`; ECX untouched; no memory write. A pure leaf.

### `0x40a9c4(type, position, angles, owner, word)` projectile_create

ESI = owner from the prologue on. `0x49630c` (EAX = `0x498370`, the entry
EDX/ECX; EBX and EDI the caller's) links the record at f−24h into FS:0.
`pool_alloc([0x4a96c2])` (EDX = the pool, EAX/ECX as `0x49630c` left
them); ECX = the pushed pool; the object O → f−4. **A null O faults** at
`0x40aa29` (the copy below writes through it; the test at `0x40a9ee` only
skips the constructor): precondition. Non-null: context word f−14h ← 14h,
`0x40a948(O, lo(O, type))` (EAX = that second slot, EDX = O, ECX = the
pool; EBX = O from here on), then `[[0x6cbee4]]` −= 5 (ECX = the counter
pointer), context word ← 8, EAX = O. Then, in this order: `[O+2Eh]` ←
`[position+8]`, `[O+26h]` ← `[position]`, `[O+2Ah]` ← `[position+4]`
(EAX = O+26h); f−28h ← angles; `[O+0Ch]` ← dword `[angles]`, word
`[O+10h]` ← `[angles+4]`, word `[O+32h]` ← 0 (EAX = O+0Ch, EDX =
angles); `[O+4Ah]` ← owner; word `[O+52h]` ← word + 1F4h (EAX =
`lo(O+0Ch, word+1F4h)`); `0x49681a(O+42h, 0, 8)` (the runtime memset; EAX
= O+42h); word `[O+54h]` ← 0. Homing (`0x40aa83`, `0x40aa8a`,
`0x40aaa6`): byte `[owner+0A3h]` = 0 and the word type = 3 give the flag
1 without a call; otherwise the flag is `s16` of the owner's virtual +6Ch
(owner, `lo(EDX, type)`) (EAX = owner+92h, EDX = `lo(memset's EDX,
type)`, ECX = the owner's vtable). Flag 0: dword `[O+56h]` ← 0 (EDX =
0). Flag ≠ 0: EDI = `[owner+1A4h]` (the owner's target) → `[O+56h]`; a
non-null target (`0x40aaba`) gets its virtual +54h (target, O+0Ch) (EAX =
O+0Ch, EDX = the target's vtable; `0x40aabe` tests the non-null O again
and is one-way) and word `[O+5Ah]` ← AX. Byte `[owner+0A3h]` ≠ 0
(`0x40aadf`): `[0x49c394]` ← O (EAX = owner+92h).
`projectile_def(O, position)` (EDX = position), ECX = O (popped), ECX =
`s16([def+0Ch])` + 0Ah, EAX = 0Ah; `0x4628e0(ECX, position)` (the sound;
the position slot is the one left from the previous call). Return EAX and
ECX = `0x4628e0`'s, EDX = the saved head `[f−24h]`; FS:0 ← EDX.

### `0x4045c8(B)` lc_base_update

Entry: EBX = B, f−4 ← B+92h; EAX = EDX = B+92h.

1. Byte `[B+99h]` ≠ 0 (`0x4045e7`, the dead building): `base_update(B)`
   with the entry ECX; ECX = B (popped); return its EDX:EAX.
2. `base_parts(B)`; ECX = B. The heading h0 = word `[B+10h]` → f−6;
   word `[B+10h]` ← h0+1555h (f−8; EAX = `lo(base_parts' EAX,
   h0+1555h)`); word `[B+32h]` ← 0 (the matrix cache); f−0Ch ← B+0Ch,
   f−10h ← B+92h; EAX = ECX = B+92h, EDX = B+0Ch.
3. Byte `[B+0A5h]` ≠ 0 (`0x40463a`): go to 7.
4. Scan (`0x404664`, `0x40467c`, `0x40468b`): i = 1 (f−14h); while
   `s16(C+12h)` > i (C and the count reread per test; ECX = the count):
   stop at the first part with `s16(P_i+0)` < `s16(Q_i+0)` (ECX =
   `lo(count, P_i+0)`), else i += 1 (EDX = P_i, EAX = Q_i, from
   `[B+205h]`+0Bh and `[C+14h]`+1Eh read once). If `s16(C+12h)` (reread,
   EDX) = i: byte `[B+0A5h]` ← 1 (f−18h ← B+92h; EAX = EDX = B+92h).
   Either way the banks run in this frame.
5. Banks i = 0..2 (f−14h; K in f−1Ch, reread at every use;
   `0x404ab1`): `timer_word_step(K+18h)` (EDX = K+18h; EAX = 0 for i = 0,
   then B+0Ch; ECX = B+209h for i = 0, then the last ECX); ECX = (AX =
   0). Run out (`0x4046c4`): `target_select(B, 10h, 3000h)` (EAX/EDX the
   timer's, ECX = 1), dword `[K+1Bh]` ← EAX, word `[K+19h]` ← 10000 (EAX
   = EDX = ECX = K). No target (`0x4046ec`): go to 6. Otherwise T =
   `[K+1Bh]` → f−24h; `dist_gate(B+26h, T+26h)` (EAX = B+26h, EDX = T, ECX
   = T+26h) → f−20h (the distance). With T reread: the aim point f−7Ch ←
   T's position (+26h, +2Ah, +2Eh, in that order); T's virtual +30h (T,
   f−2Ch, f−34h) with EAX = K, EDX = T, ECX = the vtable (every receiver
   class writes both 6-byte records); f−74h += `s16(f−30h)`, f−7Ch +=
   `s16(f−34h)`, f−78h += `s16(f−32h)` (that order); `aim_angles(f−3Ch,
   f−7Ch, B+26h)` (EAX = B+26h, EDX = f−7Ch, ECX = f−3Ch); f−40h ← B+0Ch;
   words f−38h −= `[B+10h]`, f−3Ch −= `[B+0Ch]`, f−3Ah −= `[B+0Eh]`.
   EDI = `[0x4a95bc]` (reread per bank), ESI = K; weapons j = 0..2
   (f−44h, `0x404a82`; EDI += 16h and ESI += 8 after each, also when
   skipped):
   - Part n = 2i+1+(j≠0) → f−48h (EAX = `lo(11n, P_n+0)`, ECX =
     `[C+14h]`, EDX = 15n). Word `[P_n+0]` = word `[Q_n+0]` (`0x4047e0`,
     the part is destroyed): next j.
   - `timer_word_step(S+3)`; ECX = S+3; EDX = (AX = 0). Run out
     (`0x4047fc`): the phase word `[S+6]` ← (`[S+6]` = 0) (EAX = that 0/1,
     EDX = its `s16`); f−4Ah ← word `[0x4973f4 + 2·phase]` (ECX =
     `lo(S+3, it)`), the phase reread (EAX);
     `rng_bounded(0x4d2599, lo(phase, [0x4973f8 + 2·phase]))` on stream
     A; f−4Ah += AX; word `[S+4]` ← f−4Ah (EAX = `lo(rng's EAX, it)`). The
     tables hold 5000, 5000 and 5000, 5000; the core reads them, the
     in-vitro cases also randomize them.
   - `timer_word_step(S)` (EAX/EDX/ECX: the RNG's, or the first timer's
     EAX, EDX = 0, ECX = S+3); ECX = S; EDX = (AX = 0). AX ≠ 0
     (`0x404855`) or phase 0 (`0x404860`): next j.
   - Cone and range: `u16(f−38h + [W+2])` < `u16(2·[W+2])` (16-bit
     wrapped, `0x40487f`; EAX = `lo(timer's EAX, [W+2])` doubled in 32
     bits, then the flag), the same for f−3Ch and `[W+0]` (`0x40489d`; EDX
     = `lo(1, ·)`), and dword `[W+4]` > the distance (signed,
     `0x4048a9`). Else next j.
   - The offset f−88h ← `[W+8]`, f−84h ← `[W+0Ch]`, f−80h ← `[W+10h]`;
     the muzzle f−94h ← `[B+26h]`, f−90h ← `[B+2Ah]`, f−8Ch ← `[B+2Eh]`
     (reads and writes interleaved in that order);
     `sincos_pair(lo([B+2Eh], [B+10h]), f−54h)` (the full dword is
     pushed; EDX = f−54h, ECX = `[B+2Ah]`); `rotate_2d(f−88h, f−54h,
     f−A0h)`; f−98h ← f−80h; f−8Ch += f−80h, f−94h += f−A0h, f−90h +=
     f−9Ch (that order).
   - j = 0, the cannon (`0x40493c`): if T (reread through K) ≠
     `[B+1A4h]` (`0x40494e`): the old link's word `[+1A2h]` −= 1 when
     non-null (`0x404957`), `[B+1A4h]` ← T, T's word `[+1A2h]` += 1 when
     non-null (`0x40496e`), byte `[B+9Dh]` ← 1 (f−58h ← B+92h).
     `projectile_create(3, f−94h, B+0Ch, B, 0)` (EAX = T on the equal
     path, else B+92h; EDX = B+0Ch, ECX = f−94h). Then, if `[B+1A4h]` ≠ 0
     (`0x4049ac`): its word `[+1A2h]` −= 1, `[B+1A4h]` ← 0, byte `[B+9Dh]`
     ← 1 (f−5Ch ← B+92h; EAX = ECX = B+92h, EDX = 0). The repeated test at
     `0x4049b9` and the test of the XOR-zero EDX at `0x4049d2` are
     one-way (PROVED). The shot therefore sees B's target in `[B+1A4h]`
     (homing through `projectile_create`'s virtual +54h), and the link
     is undone right after.
   - j ≠ 0, the lasers: `aim_angles(f−64h, f−7Ch, f−94h)`;
     `weapon_record(8)` with aim_angles' registers → f−68h (ECX = 8); the
     matrix translation f−A4h ← f−8Ch, f−ACh ← f−94h, f−A8h ← f−90h;
     `euler_matrix(f−64h, f−C0h)` (EAX = f−90h's value, EDX = f−C0h, ECX
     = f−64h); `impact(3, f−C0h, [R+30h], B, lo(euler's EDX, [R+38h]))`
     with R = f−68h (EAX = `[R+30h]`, EDX = f−C0h, ECX = R).
   - Both: word `[S+1]` ← `[W+14h]` (the fire timer's re-arm; EAX =
     `lo(EAX, it)`).
6. After the weapons or without a target (`0x404a88`): word `[B+10h]` ←
   `[B+10h]` + 5554h (f−6Ah; EDX = `lo(EDX, it)`, EAX = B+0Ch), word
   `[B+32h]` ← 0; f−14h += 1, f−1Ch += 1Fh.
7. `0x404ab7`: word `[B+10h]` ← h0 (f−6Ch ← h0, f−70h ← B+0Ch), word
   `[B+32h]` ← 0. Return EAX = 0, EDX = `lo(EDX, h0)` (EDX = B+0Ch on the
   path from step 3), ECX = B+0Ch.

The three banks aim from h0+1555h, h0+1555h+5554h and h0+1555h+AAA8h
(mod 10000h; 120° apart). RNG: one draw on stream A per weapon whose
cooldown timer runs out, never otherwise.

### Registers, stack and residue

**Result registers.** Row 10 pops ECX and ignores EDX:EAX after
`lc_base_update`; the hook still returns the exact pair (`base_update`'s
on the dead path). `projectile_create`: EDX:EAX exact; ECX after the
replaced hook is its entry ECX, the original's is `0x4628e0`'s. That ECX
is dead in 4045c8 (the next reader is `timer_word_step`'s entry, which
ignores it, and the POP after it), in `turret` (returns to row 10, which
pops it) and in `target_emit` (its caller `0x422ceb` pops it);
`weapon_fire` returns it to the weapon V28 call sites, where the replaced
`weapon_fire` already returns its own entry ECX (no new exposure).
`weapon_record` leaves ECX alone in both.

**Uninitialized reads.** No frame local of the three functions is read
before it is written on any path, given the callee contracts (virtual
+30h writes both records in `0x40351c`, `0x411a74` and `0x4155c4`;
`sincos_pair` four words; `rotate_2d` two dwords; `aim_angles` three
words; `euler_matrix` the nine coefficients). Two values leave as stack
residue: the matrix byte 13h (f−ADh) inside the matrix that `impact`
copies, and `impact`'s own uninitialized packet dword at its entry
ESP−88h ([weapons.md](weapons.md#nested-frame-residues): the receiver
that `sweep` excludes, captured by `impact`'s entry shim). For the call at
`0x404a65` that dword is f−16Ch, below 4045c8's frame, where the earlier
callees of the same frame (`projectile_create`'s subtree,
`target_select`, `aim_angles`, `euler_matrix`) leave their stack bytes.
A replacement that ran these calls from host frames would hand `impact`
host bytes, possibly a pointer to B or T kept in a host local, as the
receiver to exclude. Likewise `turret`'s entry shim reads its entry
ESP−65h, which is f−61h of a preceding row-10 updater at the same stack
level (4045c8 writes it on the laser path).

**Entry shims.** `lc_base_update` and `projectile_create` therefore run
like `retreat_fire` and the MECH combat steps: the shim keeps the
original's EBP, stores the caller's EBX/ESI/EDI in the original save
slots, reserves a guard-probed gap (40 pages for `lc_base_update`, which
holds `projectile_create`'s 16 pages, `impact`'s and `target_select`'s
subtrees and their host frames; 16 pages for `projectile_create`), and
every game call runs at the original outgoing stack coordinate with the
original's EBX/ESI/EDI/EBP and return addresses. The cores write every
frame local the originals write, at the originals' offsets and in their
order, so the frames hold the original bytes after return. With the
shim, `projectile_create`'s record lies at the original f−24h and
`0x49630c` runs with the original EBP; without it (direct calls) a
synthetic frame in the adapter's scratch is used, as in
`base_part_blast`.

### Building emitter liveness

`0x4045c8`: 0/0/0/0/1000/0 (saturated). `0x40a9c4` (all five call
sites): 9/6/6/138/9/0. `0x40fe84`: 0/0/0/0/18/0 (4045c8's lasers and the
weapon factory `0x40fff8`, which also runs at mission load).

## Actor slot leaves

39 hooks, one per function body: 33 in group `actor_leaves`
(`tools/reference-game/proxy/actor_leaves_hooks.h`) and six in group
`slot_leaves` (`tools/reference-game/proxy/slot_leaves_hooks.h`, in vitro
`invitro/slot_leaves_cases.h`); core `core/src/actor_leaves.c`,
`es2/core/actor_leaves.h` for both. With them every slot function of
section 2.1 is a hook except the draw methods, the destructors and the
pure-virtual trap. A scan of the image (every dword of the data sections
that points into CODE, minus the registered VAs) finds no other original
slot function in the eight tables. The pointer-referenced originals with
map counts outside them lie in other tables (the weapon-class slots
`0x40e0e0` … `0x4111dd`, the draw and HUD tables; the table at
`0x49a4b0` is none of the eight, its function `0x40316c` is the
configuration-part draw `shape_config_part` of
[rasterizer.md](rasterizer.md#actor-draw-slots-and-explosion-binning)).

### Actor slot leaf hooks

Prologue: `push ebp; mov ebp,esp` plus the next instruction(s) of the
listed lengths; the signature runs through the `ret` for the short
bodies. "Suites of 40" counts the suites with calls (six-suite counts in
2.3 and 2.4).

| Hook | Address | Slots (tables) | Relocate / lengths | Args | Result (EDX:EAX; ECX at return) | Suites of 40 |
|---|---|---|---|---|---|---:|
| `gun_angle` | `0x403594` | +3c GUN_BASE, VEHICLE | 6 / 1,2,3 | 1 | lo(O, [O+20Fh]); EDX, ECX entry | 28 |
| `base_radius` | `0x4035a4` | +5c BASE family | 6 / 1,2,3 | 1 | lo(C, [C+2Ah]); EDX, ECX entry | 40 |
| `base_sight_radius` | `0x4035b8` | +7c BASE family | 7 / 1,2,1,3 | 1 | see below | 40 |
| `base_part_word` | `0x40360c` | +84 BASE family | 6 / 1,2,3 | 2 | EAX = ECX = s16 [[O+201h]+2i]; EDX = i | 0 |
| `base_next_part` | `0x403624` | +80 BASE family | 7 / 1,2,1,3 | 3 | see below | 0 |
| `base_in_front` | `0x405308` | +44 BASE family | 7 / 1,2,4 | 1 | lo(entry EAX, 1); EDX, ECX entry | 31 |
| `base_decide` | `0x406cfe` | +1c BASE family | 5 / 1,2,2 | 1 | 0; EDX, ECX entry | 0 |
| `actor_armour` | `0x411a4c` | +34 ACTOR, BASE family, FLYER | 5 / 1,2,2 | 1 | 0 | 11 |
| `actor_target_scale` | `0x411a54` | +38 ACTOR, BASE family | 5 / 1,2,2 | 1 | 0 | 13 |
| `actor_angle` | `0x411a5c` | +3c ACTOR, BASE, RADAR, LC, FLYER | 5 / 1,2,2 | 1 | 0 | 30 |
| `actor_order` | `0x411a64` | +28 ACTOR, BASE family, FLYER | 5 / 1,2,2 | 1 | 0 | 0 |
| `actor_keys` | `0x411a6c` | +2c ACTOR, BASE family | 5 / 1,2,2 | 1 | 0 | 0 |
| `actor_aim_node` | `0x411a9c` | +24 ACTOR, FLYER | 5 / 1,2,2 | 1 | 0 | 28 |
| `actor_radius` | `0x411aa4` | +5c ACTOR, FLYER | 5 / 1,2,2 | 1 | 0 | 28 |
| `actor_sight_radius` | `0x411aac` | +7c ACTOR, FLYER | 5 / 1,2,2 | 1 | 0 | 28 |
| `actor_move` | `0x411ab4` | +14 ACTOR, BASE family | 7 / 1,2,4 | 1 | lo(entry EAX, 1) | 0 |
| `actor_engage_roll` | `0x411ac0` | +4c ACTOR, BASE family, FLYER | 7 / 1,2,4 | 1 | lo(entry EAX, 1) | 28 |
| `actor_in_front` | `0x411acc` | +44 ACTOR, MECH, FLYER | 6 / 1,2,3 | 2 | see below | 31 |
| `actor_mount_word` | `0x411b04` | +6c ACTOR, BASE family, FLYER | 7 / 1,2,4 | 1 | lo(entry EAX, 1) | 9 |
| `actor_part_word` | `0x411b10` | +84 ACTOR, FLYER | 8 / 1,2,5 | 1 | 1 | 0 |
| `actor_next_part` | `0x411b1c` | +80 ACTOR, FLYER | 6 / 1,2,3 | 3 | [out] = EAX = EDX = −1; ECX entry | 0 |
| `mech_angle` | `0x415488` | +3c MECH | 6 / 1,2,3 | 1 | lo(O, [O+298h]) | 40 |
| `mech_condition` | `0x415504` | +40 MECH | 6 / 1,2,3 | 1 | part_condition(O+206h); ECX = O+206h | 35 |
| `mech_radius` | `0x415518` | +5c MECH | 6 / 1,2,3 | 1 | lo(C, [C+70h]) | 40 |
| `mech_sight_radius` | `0x41552c` | +7c MECH | 6 / 1,2,3 | 1 | lo(C, [C+70h]) (the same body as `0x415518`) | 39 |
| `mech_part_word` | `0x415540` | +84 MECH | 6 / 1,2,3 | 2 | s16 [[O+20Eh]+2i]; EDX = i; ECX entry | c3_10-hunt-targ only (104) |
| `mech_next_part` | `0x415558` | +80 MECH | 7 / 1,2,1,3 | 3 | see below | c3_10-hunt-targ only (7) |
| `mech_mount_word` | `0x4155ac` | +6c MECH | 6 / 1,2,3 | 2 | lo(P, [P+2k+0Ah]), P = [O+202h], k = s16(w); EDX = k | 7 |
| `flyer_target_scale` | `0x421584` | +38 FLYER | 5 / 1,2,1,1 | 1 | multiply_q10(1000, s16 [O+233h]) with its ECX | 16 |
| `flyer_keys` | `0x4215c0` | +2c FLYER | 7 / 1,2,4 | 2 | 0 (the load of the second argument into AX is overwritten) | c1_01-cheat-keys only (759) |
| `flyer_condition` | `0x421e84` | +40 FLYER | 6 / 1,2,3 | 1 | part_condition(O+200h); ECX = O+200h | 0 |
| `actor_mark` | `0x42200c` | +68 all eight | 6 / 1,2,3 | 1 | byte [O+B1h] ← 1; EAX = O+92h | 6 |
| `object_zero` | `0x4786cb` | +0c all eight (and the 23 other `DRAWABLE` tables) | 5 / 1,2,2 | 1 | 0 | 0 |
| `actor_contact` | `0x4127f8` | +48 ACTOR, BASE family, FLYER (contact notice) | 5 / 1,2,1,1 (the whole body) | 1 | entry EAX, EDX; ECX entry | 28 |
| `actor_part_position` | `0x411a3c` | +58 ACTOR, FLYER | 5 / 1,2,1,1 (the whole body) | 1 | entry EAX, EDX; ECX entry | 0 |
| `actor_part_facing` | `0x411a44` | +54 ACTOR, FLYER | 7 / 1,2,3,1 (the RET stays) | 1 | EAX = FFFFFFFFh (`or eax,-1`); EDX, ECX entry | 2 |
| `actor_kill` | `0x411b2c` | +60 ACTOR, BASE family, FLYER (kill credit) | 5 / 1,2,1,1 (the whole body) | 1 | entry EAX, EDX; ECX entry | 5 |
| `actor_link_position` | `0x411b34` | +64 ACTOR, BASE family, FLYER | 5 / 1,2,1,1 (the whole body) | 1 | entry EAX, EDX; ECX entry | 7 |
| `actor_radial_hit` | `0x411b3c` | +70 ACTOR, FLYER | 5 / 1,2,1,1 | 5 | the tail callee's EDX:EAX (below); ECX 0 | 0 |

Unless stated, EDX and ECX are the entry values. The last six (group
`slot_leaves`) are reached by no direct `call`/`jmp` (rel32 byte scan of
CODE); each address occurs only as table slots (dword scan; for example
`0x4127f8` at the file offsets `0x969cc`, `0x96a60`, `0x96af4`, `0x96b88`,
`0x96c1c` in the five BASE-family tables and `0x99794`, `0x99828` in ACTOR
and FLYER, i.e. slot +48 of each). Their bodies: `55 8b ec 5d c3` (five
bytes) for `0x4127f8`, `0x411b2c`, `0x411b34`, `0x411a3c`;
`55 8b ec 83 c8 ff 5d c3` (eight bytes) for `0x411a44`.

**Hook framework entries used.** `push r32`, `mov r32,r/m32` (`8b`),
`xor r32,r/m32` (`33`), `mov r16,imm16` (`66 b8+r`), `mov r16,r/m16`
(`66 8b`) and `mov r32,imm32` (`b8+r`); for the five short bodies
`hook/hook.c` also accepts `83 /1` (the 32-bit form only; `81 /1` and
`66 83 /1` stay refused) and a near RET (`c3`) as the **last** byte of the
relocated range (a RET inside the range and `c2 imm16` stay refused). The
trampoline then returns by itself and its closing `jmp` is dead; the
detour overwrites the whole body, which nothing else branches into.
`0x411a44` is relocated up to its `pop ebp` (its RET stays). The fixture
`hook/tests/hook_fixture.c` (case `instructions`) tests the decoder both
ways and a whole-body install with original call, replaced call,
trampoline and uninstall; `hook/README.md` lists the entries. The empty
bodies change no register (`es2_actor_empty_registers`); `0x411a44` returns
EAX = −1 with EDX/ECX entry (`es2_actor_no_part_registers`).

**`base_sight_radius` `0x4035b8`.** EDX = C; `[C+6]` = 0 → EAX 0, EDX C,
ECX entry (`0x4035ca`). Otherwise ECX = C (reread at `0x4035cc`); when
`[C+4]` = FFFFh (`0x4035d7`), or s16 `[[O+205h]+5]` ≥ 2 (`0x4035e4`), or
the byte `[O+99h]` is 0 (`0x4035f0`), EDX = 0 and EAX = lo(O, `[C+2Ah]`);
else EDX = 1 and EAX = 0 (`0x4035fe`). EBX is saved and restored.

**`base_next_part` `0x403624`(O, i, out).** s = max(i, 0) (signed,
`0x403630`), ECX = s. k runs s+1, s+2, …; every step rereads
n = s16 `[C+12h]` (through `[O+1F2h]`) and wraps k to 0 when k = n
(`0x403643`); the walk stops when k = s (`0x403649`) or the word
`[[O+201h]+2k]` (also reread every step) is nonzero (`0x403656`). A walk
back to s gives −1 (`0x40365a`). `[out]` = EAX = k or −1, EDX = out. The
walk is unbounded: s ≥ n (or n ≤ 0) scans on until a nonzero word, as
the core does.

**`mech_next_part` `0x415558`(O, i, out).** W = `[O+20Eh]` (read first),
s = max(i, 0). k runs s+1, … and wraps at 7 (`0x415574`); stop at k = s
(`0x41557a`) or when the word `[W + 2·s16 T[k]]` is nonzero (`0x415589`),
T the seven words at `0x49a060` (0, 4, 5, 7, 8, 9, 10: the part indices of
the seven weapon slots, read from the image). Back at s: EAX = −1 and
EDX = −1 (`0x41558d`, `0x415595`); else EAX = k, EDX = s16 T[k].
`[out]` = EDX, ECX = out. For s ≥ 7 the walk would read T beyond its
seven words (`0x49a070` holds 0BCE0h) and leave W by 34 KiB; no caller
passes such an index (the slot word of `targ_next` `0x40e4ac` is −1..6,
set at `0x40e49b`/`0x40e4a3` or by this method), and the in-vitro cases do
not either.

**`actor_in_front` `0x411acc`(O, a).** DX = a − `[O+10h]` + 3800h (16-bit;
EDX keeps the entry high word); EAX = 1 when DX < 7000h unsigned (SETB),
else 0 (`and eax,1` clears the object pointer's upper bits).

**The callers.** `mech_condition`/`flyer_condition` call part_condition
`0x40db2c` (hook `part_condition`, group `weapon_helpers`) with EAX = the
pointer and the entry EDX/ECX; its EDX:EAX is the result and `pop ecx`
takes the argument back (part_condition never writes `[ebp+8]`; listing
`0x40db2c`..`0x40dbbf`). Its IDIV faults on a zero sum of maxima, in the
callee, in every mode. `flyer_target_scale` pushes s16 `[O+233h]` and
1000 and calls multiply_q10 `0x47e0b0` with the entry EAX/EDX/ECX; result
and ECX are the callee's.

### `actor_radial_hit` 0x411b3c(O, damage, point, reach, source)

38 instructions, no conditional branch, no local; EBX = O, ESI = reach,
EDI = s16(reach). The argument roles were read at the callers of slot +70h
(scan for `call [reg+70h]`: four sites): `radius_hit` `0x426a8d` (pushes the
object, the damage word `[ebp+10h]`, the point `[ebp+8]`, the reach word
`[ebp+0Ch]` and the source `[ebp+14h]`; it has already skipped the object
unless `dist − radius < reach`) and `ground_collide` `0x41938a` (the collided
actor, a local damage, a local vector, reach 4B0h, the source); the sites
`0x419373` and `0x41895a` have MECH receivers (`vehicle_hit`), not this body.
The receiver of this body is always a FLYER (ACTOR is abstract).

1. `[O]` is the vtable V; slot +5Ch(O) is called with EAX = V and the entry
   EDX/ECX; `radius` = s16(AX) (`0x411b4f`).
2. `dist_gate(point, O+26h)` with EAX = point, EDX = radius (sign-extended),
   ECX = O+26h; the radius dword stays on the stack under the two arguments
   and is popped into EDX at `0x411b68`.
3. `t` = EAX − radius; `x` = s16(reach) − t; `x` ← the low dword of
   `x · s16(damage)` (one-operand IMUL, then CDQ drops the high dword);
   `q`, `rem` = IDIV of `x` by s16(reach) (truncating, the remainder takes
   the dividend's sign).
4. `[O]` is read again (V′); slot +74h(O, 0, q, source) is the tail call with
   EAX = V′, EDX = `rem`, ECX = source; its EDX:EAX and ECX are the result of
   the function (`add esp,10h` and the pops only restore EBX/ESI/EDI/EBP).

Preconditions of the original that the core keeps: s16(reach) ≠ 0 and the
quotient fits (IDIV faults otherwise; the in-vitro cases avoid zero and the
INT_MIN / −1 case). `radius_hit` guarantees `reach − (dist − radius) > 0`.

**ECX.** The tail callee of every FLYER is `flyer_part` (slot +74h), which
returns ECX = 0 on every path ([hit_tests.md](hit_tests.md)); FLYER's slot
+5Ch is `actor_radius` (`xor eax,eax`). The replacement needs the entry ECX
(it is the radius slot's entry register, logged by the in-vitro stubs) and
the caller must get 0: an **entry shim** (`slot_leaves_radial_hit_entry`) calls the framework's bridge with the five
argument slots copied and clears ECX after the return. `flyer_part`'s own
shim zeroes ECX at entry because that body never reads it; this body passes
it on, which is why the wrapper form is used. Compare: refused (the
replacement calls three slots/functions that change game state); the five
empty bodies accept it.

### Callers' registers

The hooks reproduce EDX:EAX; ECX and the flags at return are the
framework's. Every `call dword ptr [r+off]` of the slots concerned (all
classes, not only actor receivers) was walked along all branches until ECX
or the flags are written or read:

- ECX differs from the entry only in `base_sight_radius`,
  `base_part_word`, `base_next_part`, `mech_next_part`, `mech_condition`,
  `flyer_condition` (and `flyer_target_scale`, the callee's). At every
  `+7c`, `+84`, `+80`, `+40`, `+38` site ECX is written first, except: the
  `+84` site `0x40e50d` (`0x40e4dc`, weapon targeting) and `targ_next`'s
  `+80` site `0x40e4ca` reach `mov cx,[ebx+86h]; push ecx` at `0x40e525`,
  whose high word goes to slot `+58` as the part index, which every `+58`
  body reads as a word (`movsx` at `0x406838`, `0x41b623`; `0x411a3c`
  ignores it); after `targ_next`'s other caller, `hud_key` at `0x432ee9`,
  ECX reaches `hud_key`'s own `ret` unread (`0x43311e`..`0x433130`;
  `hud_key` is a hook); `0x41105f`/`0x41108c` (`+38`) are
  weapon receivers and return; the `+80`/`+7c` sites at `0x465524`,
  `0x465870`, `0x465f72`, `0x4668f4` are sound-window objects (`[4d3714h]`),
  not actors.
- The flags: no site reads them before writing them; 15 sites (nine
  `+1c` sites, two `+0c`, one `+3c`, the sound-window `+2c`/`+7c` sites and
  the `+68` site `0x46a685` in `convoy_think`'s neighbourhood) pass them on
  to their own `ret`; no compiled caller uses flags across a call.
- No local is read before it is written; no shim except the radial hit's
  (none of the other bodies has a frame beyond saved registers).

### Actor slot leaf validation (in vitro)

`//core:actor_leaves_test`: every constant, getter, both sight-radius
paths, the walks' wrap, return-to-start and negative starts, the callers'
arguments and registers through a logging bridge, the empty bodies, and
the radial hit through a logging bridge (four plans with positive and
negative reach, damage and remainder, the product's wrap, the order and
registers of the three calls, the vtable reread).

`differential_actor_leaves_test` (quick tier, three shards): per hook
100,000 direct cases (original body, adapter and core on the same arena:
EDX:EAX, ECX of the core against the original's, the 8 KiB arena after
each run, the callee-counter delta), 256 installed 31/a7 poison cases
(fast and preserving dispatch, x87 non-empty), 512 installed compare cases
(exact 0 mismatches, skewed 256; the four span hooks 128 of them by the
declared byte), and for the three callers 1,536 real-callee cases with
part_condition or multiply_q10 nested in original, replaced and compare
mode and the hook original or replaced. Branch sites: `base_sight_radius`
5, `base_next_part` 5, `mech_next_part` 6, all 16 both ways in the coverage
window, none proved. Paths seen (100,000 cases): `base_sight_radius` 0 with
EDX C / radius / 0 with EDX 1 = 19,531/68,056/12,413; `base_next_part` −1 /
found = 24,155/75,845; `mech_next_part` 23,975/76,025.

`differential_slot_leaves_test` (quick tier, one shard): per empty hook
100,000 direct cases (original body, adapter and core: EAX, EDX, the
original's ECX), 256 installed 31/a7 poison cases (fast and preserving
dispatch, x87 non-empty) and 512 installed compare cases (exact 0
mismatches, skewed 256), whose original side is the **relocated
trampoline** (the five-byte bodies through its own RET); for
`actor_radial_hit` 100,000 direct cases against logging stubs for the
radius slot (two arena vtables), `dist_gate` (patched over the original)
and the part slot (two vtables), with the object's vtable pointer swapped
between the calls in a quarter of the cases (the reread), ordered calls
with arguments and entry EAX/EDX/ECX, arena and callee counters; 256
installed poison cases through the shim; and 1,536 real-callee cases with
`dist_gate` and `actor_radius` as nested hooks in rotating
original/replaced/compare modes, the part slot a stub, the hook original
or replaced. No conditional branch exists in any of the six (no `BRANCH`
lines).

Mutations: one per hook in `check_mutations.ps1`
([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).

### Actor slot leaf liveness

Every hook is reached only through its slots, so a replaced run counts its
calls in `hk_<name>_calls`. Group `actor_leaves` is live: the
regression with all hooks replaced and the 49
shim imports replaced finds **40 of 40 suites identical**,
exit states equal to the goldens', every skipped/bypassed/mismatch counter
0. The unsaturated map counts are met exactly (`actor_target_scale`
0/0/6/674/26/0, `actor_engage_roll` 22/38/22/546/27/3, `actor_mount_word`
9/6/6/25/3/0, `mech_mount_word` 113 and `actor_mark` 38 in
c3_09-loadout-b, `actor_mark` 100 in c3_10-loadout-hercs, `mech_part_word`
104 in c3_10-hunt-targ, `flyer_keys` 759 in c1_01-cheat-keys, `gun_angle`
623/510/658 in a60/c1s/c1c); a count at the compared tick may be slightly
below the map's, which counts to the end of the run (`gun_angle` 454
against 459 and `actor_armour` 379 against 383 in c3_10-loadout-hercs). The
saturated ones run up to 3,781,835 calls (`base_radius` in
c5_05-moon-missiles). `base_decide` and the other zero-count leaves stay 0
in all 40 suites. The callee counters `hk_mech_condition_call_40db2c`,
`hk_flyer_condition_call_40db2c` and `hk_flyer_target_scale_call_47e0b0`
equal their hooks' calls.

Group `slot_leaves` is in vitro; not checked live. Expected
counts from the 40-suite map and the receiver histograms of
[hook_frontier.md](../reference-game/hook_frontier.md#receiver-histograms-of-the-virtual-call-leaves)
(order a60/c1s/c1c/c3_09/c4_08/c3_10):

| Hook | Map probe | Receiver histogram | Suites of 40 |
|---|---|---|---:|
| `actor_contact` | 1000/1000/1000/0/0/1000 | 5,775/10,120/6,968/0/0/6,539 (BASE, GUN_BASE, RADAR_BASE, VEHICLE) | 28 |
| `actor_part_facing` | 1/1/0/0/0/0 | 1/1/0/0/0/0 (FLYER) | 2 |
| `actor_kill` | 1/1/1/0/0/0 | 1/1/1/0/0/0 (GUN_BASE) | 5 |
| `actor_link_position` | 0/10/4/5/0/0 | 0/10/4/5/0/0 (FLYER, GUN_BASE) | 7 |
| `actor_part_position` | no row | 0 in every suite | 0 |
| `actor_radial_hit` | no row | 0 in every suite | 0 |

`hk_actor_radial_hit_calls` 0 is expected everywhere (no FLYER is in the
radius of a blast or collided in the 40 suites; its flow rests on the
listing, the callers' pushes and the in-vitro cases); should it be nonzero,
`…_slot_5c` = `…_call_49288c` = `…_slot_74` = the calls.

## Simulation leaves

17 hooks, one per function body, in group `sim_leaves`
(`tools/reference-game/proxy/sim_leaves_hooks.h`; core
`core/src/sim_leaves.c`, `es2/core/sim_leaves.h`; in-vitro cases
`tools/reference-game/invitro/sim_leaves_cases.h`). Listing started at each
function (`--start-address`: the full listing is misaligned around
`0x407b1f` by padding). Counts are from the 40-suite map.

### Simulation leaf hooks

The result is exact EDX:EAX (`ES2_RESULT_PAIR`). The three slot bodies
(`flat_draw`, `projectile_draw`, `beam_done`) are reached through vtables,
so the receiver still decides which body runs.

| Hook | Address | Args | Contract | Suites with calls |
|---|---|---:|---|---:|
| `part_shapes_bind` | `0x4030d0` | 3 | (A, B, pool): A and B are counted lists (array dword `+0`, word count `+4`). While A's count (tested, then decremented) and A's array cursor are non-zero, each A element takes one B step: B's count word is tested, then decremented with 16-bit wrap; once it was zero the step yields nothing and later steps read on past B's array. A step that yields nothing skips the slot and leaves the pool; otherwise a zero A element skips and the pool advances by 18h. An A element points at a cell `s` holding a record pointer; the chosen record is the B element, or the pool's current 18h-byte record when the B element is 0; the words `+4` and `+6` of `[s]` are copied to it one at a time (`[s]` re-read for each), then `[s]` = chosen and the pool advances by 18h. EAX 0, DX the last count word, ECX the pool cursor. Called by the MECH draw slot `0x4174c8` | 36 |
| `parts_intact` | `0x403668` | 1 | 1 when no part record whose cap word `+6` is non-negative has its current word (`0Bh`-byte records from `[O+205h]`) below the record's word `+0` (`1Eh`-byte records from `[[O+1F2h]+14h]`); the count `[[O+1F2h]+12h]` is read again at every test | 8 |
| `formation_slot` | `0x405b9c` | 3 | (_, row, col): the first call clears the three-dword cell `0x4975d6` (the byte `0x4975e2` counts that); EAX = `0x4975d6` after copying the two dwords of the table entry `[[0x4a95ac]+1Ch×row+4] + 0Ah×col - 0Ah` | 39 |
| `explosion_entry` | `0x407b20` | 1 | EAX = EDX = `[0x4a9628]` + 40 × s8 byte `[O+41h]` | 27 |
| `flat_draw` | `0x40991c` | 1 | object slot: `terrain_pose` `0x4029d8(O)`, `shape_actor_matrix` `0x401fe4(O)`, then slot `+1Ch` of the child `[O+34h]` | 38 |
| `projectile_draw` | `0x40a120` | 1 | object slot: global `0x4a5b04` ← 0, `0x401fe4(O)`, the child's slot `+1Ch`, `0x4a5b04` ← `[0x4a5b08]` (which is also EAX) | 23 |
| `beam_done` | `0x40c2a0` | 1 | slot: `timer_word_step(O+5Ch)`; EAX = 1 when the returned word is zero, else 0 | 16 |
| `weapon_by_mount` | `0x410670` | 2 | (list, w): first non-null entry of the pointer array `[L+4]` (word count `[L+8]`, signed word compare) whose definition `[entry+18h]` has the s8 byte `+17h` equal to w (word compare); else 0 | 5 |
| `ptr_list_add` | `0x411dd4` | 2 | (list, p): the pointer goes to `[L][s16 [L+6]]`, the count word `[L+6]` is incremented and the old count is stored at `[p+4Bh]` | 40 |
| `box_square_init` | `0x413a90` | 2 | (rect, w): words `+6` and `+8` ← w, `+4` ← 200h, words `+2` and `+0` ← s16(`[R+6]`) >> 1 | 40 |
| `twist_sound_latch` | `0x41a6d0` | 2 | (vehicle, w): the torso-twist servo sound latch on the cells `0x49a1c9`/`0x49a1ca` and the vehicle word `+298h` (below) | 37 |
| `pitch_sound_latch` | `0x41a994` | 2 | the same code on the cells `0x49a1cc`/`0x49a1ce` and the vehicle word `+29Ah` | 37 |
| `mech_type_index` | `0x41ff00` | 1 | EAX = byte `0x4a9d08 + s16(w)` | 40 |
| `flyer_type_index` | `0x422e80` | 1 | EAX = byte `0x4a9dcc + s16(w)` | 32 |
| `cockpit_notice` | `0x420a68` | 1 | (id): the 24-byte event record `{word id, dword 0, 18 bytes}` is built in the frame and posted through the main object's port: `[0x49b088]` object, port `[obj+20Bh]`, its vtable slot 0 with (port, record) | 14 |
| `radio_notice` | `0x420a98` | 3 | (actor, id, flag): returns (A, A+92h) at once when the byte `[A+99h]` is set and the flag word is zero; else the record `{id, A}` is built and `port_a` `0x433158` gives the port; slot 0 gets (port, record) | 16 |
| `notice_time` | `0x426aec` | 0 | EAX = 7530h, doubled unless the word `0x4a9e5e` is 1 | 16 |

`c5_05-moon-missiles` is the one suite whose map counts are nonzero for all
17 functions. `0x41a6d0` and `0x41a994` have identical per-suite counts.
The suites with zero map counts are listed in
[function_verdicts.md](../reference-game/function_verdicts.md).

### Sound latches

EAX = w (the whole dword); a negative AX is negated (the high word stays).
With the flag byte clear and AX above C0h (signed word compare) and the
latch word different from the servo word `[V+off]`: `sfx_playing`
`0x462af0(21h)` is called with EAX = AX, EDX = the latch word in DX (high
word of the entry EDX), ECX unchanged; a zero return sets the flag. With
the flag set and (AX = 0 or the latch word equal to the servo word) the
flag is cleared. In every case the latch word ← the servo word. The two
bodies differ only in their cells. `sfx_playing` is a service row
(`sim/dispatch/services/actor_draw.h`) and a guest call here (observer key `0x462af0`); the sound
driver is behind `0x462af0`, not in the latch bodies.

### Event notices and the port

`cockpit_notice` and `radio_notice` build the same 24-byte frame record
(`word +0`, `dword +2`, and 18 bytes the original never writes: stack
residue). The reconstruction writes zeros there: the records go to the
presentation port, which does not read them (the same convention as
core/src/mission.c); the in-vitro stubs mask values that point into the
stack, so the residue is not compared. The slot-0 calls use the call sites
`0x420a8d` (cockpit) and `0x420acb` (radio) as observer keys, because a
vtable slot 0 is not a counter key. The port's slot 0 itself stays original.

### Registers (ECX liveness)

EDX:EAX is exact for all 17. ECX is the framework's (the entry value): the
cores report the original's ECX through an out-parameter, and the in-vitro
harness compares it core against original, but the adapters do not forward
it. This is argued, not measured: the BCC32 code generator never keeps a
value in ECX across a call (as found for the actor slot leaves), so a
caller that reads ECX after one of these calls would read a value the
callee is free to clobber; the three slot bodies (`0x40991c`, `0x40a120`,
`0x40c2a0`) are entered through a vtable call and follow the same rule.
`flat_draw`, `projectile_draw` and `beam_done` leave EDX as the last
callee's residue (for `beam_done` the scaled word `timer_word_step` leaves
in DX); a replaced `timer_word_step` returns EAX only, so `beam_done`'s EDX
differs then, which is why the real-callee pass of the in-vitro suite does
not compare EDX when the nested `timer_word_step` is replaced.

### Compare

Six pure readers accept compare: `parts_intact`, `explosion_entry`,
`weapon_by_mount`, `mech_type_index`, `flyer_type_index`, `notice_time`.
The other eleven write state without a declared span or make guest calls and
refuse it (`compare_unsupported`).

### Simulation leaf validation (in vitro)

`//core:sim_leaves_test` (the core against a recording bridge) and
`differential_sim_leaves_test` (quick tier, 2 shards): per hook 100,000
direct cases against the original body (EDX:EAX of the original, the adapter
and the core, the core's ECX against the original's, every logged callee
call with its entry registers and arena hash, the arena and the global
windows); 256 installed 31/a7 poison cases (fast and preserving dispatch,
non-empty x87) and 256 integration cases; 512 compare cases for the six
readers (exact replacement 0 mismatches, skewed 256). The latches and
`radio_notice` also run 3,072 real-callee cases each (the real
`sfx_playing` with the sound device absent, the real `port_a`) and
`beam_done` 3,072 with `timer_word_step` as a nested hook in rotating
original/replaced/compare modes. `flat_draw` and `projectile_draw` have no
real-callee pass: their callees `0x401fe4` and `0x4029d8` draw through
matrix and terrain code that faults on random object bytes and are the
subject of the shape-draw and node groups. The group's 28 `BRANCH` lines
(part_shapes_bind 6, weapon_by_mount 3, the latches 8 each, radio_notice 2,
notice_time 1) are covered both ways; the sites of `parts_intact` (under
`base_parts`) and `formation_slot` `0x405ba6` (under `formation_base`) are
counted by those groups' lines (an address is one site). Mutations: one per
hook in `check_mutations.ps1`.

### Computed, not hooked

`0x427d98` and `0x427da0` are called only from inside `sweep` and stay
computed there. `0x425344` and `0x426514` are one-byte `ret` bodies (below
the detour size) and stay original.

### Simulation leaf liveness

In vitro; not checked live. `hk_<name>_calls` is the number of
entries of the hook in the compared run; every nonzero map count above is
expected nonzero for the replaced hook. Counters of the callees:
`beam_done` `call_467a48`, `flat_draw` `call_4029d8`, `call_401fe4` and
`slot_1c`, `projectile_draw` `call_401fe4` and `slot_1c`, the latches
`call_462af0`, `cockpit_notice` `call_420a8d`, `radio_notice`
`call_433158` and `call_420acb`.

## Open

- Receiver classes of slot sites without a live histogram are inferred
  from the bytes around them, not observed.
- `0x415558`/`0x415540`/`0x40e4ac` and the eight actor-class destructors
  were never entered in the histogram runs
  ([hook_frontier.md](../reference-game/hook_frontier.md#receiver-histograms-of-the-virtual-call-leaves)).
- Live stack headroom under `order_update` (about 340 KiB of the 1-MiB
  reserve in vitro) is not measured.
- Groups `slot_leaves` and `sim_leaves`: in vitro; not checked
  live. `actor_radial_hit` and `actor_part_position` have no live entry in
  the 40 suites.

## TS and AN part tables (`0x49a494`, `0x4a6250`, `0x4a62b8`, `0x4a6284`, `0x4a6319`, `0x4a67b0`, `0x4a67e4`, `0x4a6810`, `0x4a684c`, `0x49aaa0`)

The persistent part classes whose vtables `sim/src` installs (`load.c`
`part_list_ctor`, `shape_ctor`, `class_construct`, `es2_sim_service_grid_new`,
`es2_sim_service_mount_ctor`; `view.c` `group_ctor`), the roles of their
slots, every own call through them, and the outcome of the 33 targets that
[sim_gap.md](../reference-game/sim_gap.md) lists for these tables under
"Virtual ... R3D" (with `0x490fa0`, slot 5 of `0x49aaa0`). Read from the
reference `VER95/DBSIM.EXE` with
`llvm-objdump -d -M intel`, re-disassembled from the entry where the linear
sweep is misaligned (`0x492357`, `0x49264f`, `0x49267c`, `0x4926a9`,
`0x4926de`); class names from the Borland RTTI (dword at vtable − 12).

### Classes and slots (read from the image bytes)

| Table | Class | Factory or constructor in sim/src |
|---|---|---|
| `0x49a494` | `CONFIG_PART` | `0x42067c` (`mount_ctor`) |
| `0x4a6250` | `TSPartBase` | `0x48f8dc`; base of every part below |
| `0x4a62b8` | `TSBitmapPart` | `0x48fb74` |
| `0x4a6284` | `TSCellAnimPart` | `0x48fff4` |
| `0x4a6319` | `TSShape` | `0x490a34` (`shape_ctor`), `0x490d68` |
| `0x49aaa0` | `GridShape` | `0x427568` (`grid_new`) |
| `0x4a67b0` | `ANShape` | `0x492164` |
| `0x4a67e4` | `ANAnimList` | `0x491a50` |
| `0x4a6810` | `ANCyclicSequence` | `0x4919a0` |
| `0x4a684c` | `ANSequence` | `0x4913f0` |

Slot roles, from the bodies of the slots of all ten tables:

| Slot (offset) | Role | Evidence (listing) |
|---|---|---|
| 0 (`+0x00`) | class tag | `0x48f97c`: `mov eax,0x140005; ret` |
| 1 (`+0x04`) | stored size | `0x48f988`: EAX `0xc`; the derived ones add their fields to the base's (`0x48fd84`: base + 4; `0x490de8`: a nonzero base + 4 + 32·word +0x16 + 2·word +0x24; `0x4921f4`: `0x490de8` + `0x47a55c` of [P+0x2a]) |
| 2 (`+0x08`) | memory size | `0x48f994`: EAX `0x10`; the lists add each child's slot +8 (`0x49009c`, `0x490e14`, `0x491b50`: `call [edx+8]`) |
| 3 (`+0x0c`) | read (P, S, arg) | `0x48f9a0`: words +4, +6, +8 by `0x492430`, 6 bytes at +0xa by S slot +0x18 |
| 4 (`+0x10`) | store (P, S) | `0x48f9e8`: words +4, +6, +8 by `0x492447` (S slot +0x14, 2 bytes), 6 bytes at +0xa by S slot +0x14 |
| 5 (`+0x14`) | debug dump (P, out, indent) | `0x48fa30`: `0x47a594`, indent spaces by `0x4967f6` into the stream record at `0x496886`, fields by `0x4967d2` with the formats from `0x4a6154` |
| 6 (`+0x18`) | destructor (P, flags) | `0x4909d2`: P nonzero: counter − 1, base destructor, flags bit 0: delete `0x477548` |
| 7 (`+0x1c`) | draw | not in this set (`0x48be48`, `0x4763f4`, `0x4768f0`, `0x490ab8`, `0x478de4`) |
| 8, 9 (`+0x20`, `+0x24`) | id lookups | `0x47622c`/`0x476240`, `0x476850`/`0x476890` (rows `part_lookup`); `0x4a67e4` has 8 slots: its +0x20 is the RTTI descriptor `0x49259f` of the next table |

The store callees are stream-write thunks through S slot +0x14: `0x49085d`
(1 byte), `0x492447` and `0x4911a3` (2 bytes), `0x491156` and `0x492479`
(2n bytes); `0x47a1ac` stores a child object, `0x47a55c` sizes one (its
slots 0 and 1).

### Every own call through these tables

A reading of every indirect call in `sim/src` and `core/src` (the helpers
`vcall`, `VCALL`, `HVCALL`, `vcallr`, `fvcall`, `vslot`, `slot` and every
`call(…, r32(r32(obj) + off), …)`): the calls whose receiver can be one of
these classes use slots 0, 3, 7, 8 and 9 only.

| Slot | Site | Receiver |
|---|---|---|
| 0 | `load.c` `mount_offset` (`0x40dd4c`) | parts of the mount tree |
| 3 | `load.c` `es2_sim_service_class_new`, `es2_sim_service_grid_new` | the new object |
| 7 | `shape_draw.c` `slot` | parts being drawn |
| 8, 9 | `load.c` `part_find`, `part_slot_find` | children of a part list |

The other slot calls have other receivers: streams (`sread`, `header_peek`,
`object_skip`, `audio.c`, `fill.c`, `gfx.c`, `services_files.c`,
`core/src/asset_load.c`), GL lights (`load.c` `scene_light`, `view.c` spot
and point lights), GL bitmaps and holders (`load.c`
`bitmap_array_destroy`, `fill.c` `gl_holder_destroy`), game objects
(`scene.c` `bin_object`; `shape_draw.c` `ranges_level`, the objects of
`0x42e8e8`), DirectDraw objects (`present.c`) and HUD objects (`cockpit.c`,
`dialogs.c`, `hud_*.c`, `mfd.c`). No own code stores, sizes, dumps or
destroys a TS or AN part: they stay in the load arena.

### Outcome of the targets

All 33 are **refused on purpose**: no row, the dispatch refuses a call to
them, and no own path makes one (slots 1, 2, 4, 5 and 6 above). None has an
entry of its own in the live map (40 suites). Their callees in
the last column stay without rows for the same reason.

| VA | Table (slot) | Role | Callees (listing) |
|---|---|---|---|
| `0x48f9e8` | `0x49a494`, `0x4a6250` (4) | TSPartBase store | `0x492447`, S +0x14 |
| `0x48fa30` | `0x49a494`, `0x4a6250` (5) | TSPartBase dump | `0x47a594`, `0x4967d2`, `0x4967f6` |
| `0x492357` | `0x4a6250` (6) | TSPartBase destructor | `0x48be20`, `0x477548` |
| `0x48fd84` | `0x4a62b8` (1) | TSBitmapPart stored size | `0x48f988` |
| `0x48fd24` | `0x4a62b8` (4) | TSBitmapPart store | `0x48f9e8`, `0x49085d`, `0x4911a3` |
| `0x48fc24` | `0x4a62b8` (5) | TSBitmapPart dump | `0x48fa30`, `0x4967d2`, `0x4967f6` |
| `0x4909ff` | `0x4a62b8` (6) | TSBitmapPart destructor | `0x48be20`, `0x477548` |
| `0x490084` | `0x4a6284` (1) | TSCellAnimPart stored size | `0x48fe18` |
| `0x49009c` | `0x4a6284` (2) | TSCellAnimPart memory size | children's +8 |
| `0x490108` | `0x4a6284` (4) | TSCellAnimPart store | `0x48ff0c`, `0x492447` |
| `0x490134` | `0x4a6284` (5) | TSCellAnimPart dump | `0x48ff60`, `0x4967d2`, `0x4967f6` |
| `0x4909d2` | `0x4a6284` (6) | TSCellAnimPart destructor | `0x476754`, `0x477548` |
| `0x490de8` | `0x4a6319` (1) | TSShape stored size | `0x48fe18` |
| `0x490e14` | `0x4a6319` (2) | TSShape memory size | children's +8 |
| `0x490f40` | `0x4a6319` (4) | TSShape store | `0x48ff0c`, `0x491156`, `0x4911a3`, S +0x14 |
| `0x490fa0` | `0x4a6319`, `0x49aaa0` (5) | TSShape dump | `0x47a594`, `0x48ff60`, `0x4967d2`, `0x4967f6` |
| `0x490a64` | `0x4a6319` (6) | TSShape destructor | `0x476754`, `0x47758c`, `0x477548` |
| `0x4921f4` | `0x4a67b0` (1) | ANShape stored size | `0x490de8`, `0x47a55c` |
| `0x492218` | `0x4a67b0` (2) | ANShape memory size | `0x490e14`, +8 of [P+0x2a] |
| `0x492270` | `0x4a67b0` (4) | ANShape store | `0x490f40`, `0x47a1ac` |
| `0x49229c` | `0x4a67b0` (5) | ANShape dump | `0x490fa0`, +0x14 of [P+0x2a] |
| `0x49264f` | `0x4a67b0` (6) | ANShape destructor | `0x490a64`, `0x477548` |
| `0x491af0` | `0x4a67e4` (1) | ANAnimList stored size | `0x47a55c` |
| `0x491b50` | `0x4a67e4` (2) | ANAnimList memory size | children's +8 |
| `0x491d18` | `0x4a67e4` (4) | ANAnimList store | `0x47a1ac`, `0x492447`, `0x492479`, S +0x14 |
| `0x491df0` | `0x4a67e4` (5) | ANAnimList dump | `0x47a594`, `0x4967d2`, `0x4967f6`, +0x14 |
| `0x49267c` | `0x4a67e4` (6) | ANAnimList destructor | `0x48be20`, `0x477548` |
| `0x491490` | `0x4a6810`, `0x4a684c` (1) | ANSequence stored size | none (words +0xa, +0x10) |
| `0x4914c0` | `0x4a6810`, `0x4a684c` (2) | ANSequence memory size | none (words +0xa, +0x10) |
| `0x4915dc` | `0x4a6810`, `0x4a684c` (4) | ANSequence store | `0x492447`, `0x492479`, S +0x14 |
| `0x491674` | `0x4a6810`, `0x4a684c` (5) | ANSequence dump | `0x47a594`, `0x4967d2`, `0x4967f6` |
| `0x4926a9` | `0x4a6810` (6) | ANCyclicSequence destructor | `0x48be20`, `0x477548` |
| `0x4926de` | `0x4a684c` (6) | ANSequence destructor | `0x48be20`, `0x477548` |

`0x4911a3`, `0x491156` and `0x492479` are also behind targets of the TS
group and list tables `0x4a0f8b`, `0x4a10bc`, `0x4a1088` (sim_gap.md "Behind
the virtual targets").

### Open

- The roles of slots 1, 2 and 5 are read from the bodies; their callers in
  the original (the persistent store, the debug dump) are not traced.
- A row becomes due only when own code stores, sizes, dumps or destroys a
  part (a save path or an unload with destructors); none exists.

## Shape-class virtual tables

The persistent R3D shape classes (TS shapes, groups, part lists and
polygons) and their base tables. Slot numbers as in
[own_process.md](../reference-game/own_process.md#sim-r3d-and-hud-gap-roots-as-rows-and-verdicts):
0 tag, 1/2 stored sizes, 3 read, 4 write, 5 dump, 6 destructor (+0x18),
7 part draw (+0x1c), 8/9 part lookups (+0x20/+0x24). Read from the
reference DBSIM.EXE's tables; a slot not listed is inherited or outside
this group.

| Table | 1 | 2 | 4 | 5 | 6 |
|---|---|---|---|---|---|
| `0x4a0f8b` | `0x48ee90` | `0x48eeb4` | `0x48ef84` | `0x48efc8` | `0x475aec` |
| `0x4a0fbf`, `0x49afb0` | `0x48e930` | `0x48e990` | `0x48eb2c` | `0x48ebec` | `0x475900`; `0x49afb0`: `0x42f6d2` |
| `0x4a10bc` | `0x48fe18` | `0x48fe60` | `0x48ff0c` | `0x48ff60` | `0x476754` |
| `0x4a1088` | `0x490220` | `0x490240` | `0x4902d4` | `0x490304` | `0x476968` |
| `0x4a1054` | `0x490408` | `0x490434` | `0x490530` | `0x490588` | `0x476ae0` |
| `0x4a5eb4` | | | `0x48e070` | `0x48e0b8` | `0x48f218` |
| `0x4a5ee0`, `0x4a5f0c`, `0x4a5f64`, `0x4a5f90`, `0x4a5fe8` | `0x48e238` | | `0x48e284` | `0x48e2b0` | `0x48f2a1`, `0x48f336`, `0x48f794`, `0x48f7e0`, `0x48f868` |
| `0x4a5f38`, `0x4a5fbc` | `0x48e568` | | `0x48e5b4` | `0x48e5e0` | `0x48f748`, `0x48f824` |
| `0x4a6014` | | | | | `0x48f8ac` |
| `0x4a5e88` | slot 0 `0x48dce0` | | | | `0x48f19b` |
| `0x4a6a14` | slot 0 `0x48dce0` | | | | |

The polygon tables' slot 8 (`0x48f1c8`, `0x48f24d`, ...) has no row and is
not in the gap list.

### Refusals (static reading)

No slot 1, 2, 4, 5 or 6 of these tables is reachable from own code; the 43
targets keep no row and stay refused (exit 70) if ever called.

- **Own dynamic call sites.** Every `es2_sim_call` through a vtable in
  sim/src was read. Slot 0 is called on SimObjects, draw records, widgets,
  ports, the joystick and parts (load.c `mount_offset`); +0xc on objects
  being loaded (load.c `class_new`); +0x1c, +0x20, +0x24 on parts
  (shape_draw.c `slot`, load.c `part_find`, `part_slot_find`); +4 on lights
  (view.c); +8 on bitmap-array elements (fill.c); +0x10 on SimObjects
  (scene.c, shape_draw.c `ranges_level` with kind 5). The +0x18 calls with
  flag 3 destroy DBA bitmaps (load.c `bitmap_array_destroy`), the cockpit
  and impactcp palettes (hud_cockpit.c `destroy_shape`), intro palettes
  (cockpit.c `drop_release`), the dialog bitmap (dialogs.c `owned_free`),
  the sysbuttn elements (widget_leaves.c `three_shape_destroy`) and the GL
  holder (fill.c `gl_holder_destroy`); the other +0x18 uses are stream
  reads (view.c `stream_slot`, rows `0x48f3ea`, `0x490874`, ...).
- **Sizes and writes (slots 1, 2, 4).** Reached only through the store
  closure `0x47a1ac` / `0x47a55c` and from derived slots of the same
  classes (`0x48e295` → `0x48e070`, `0x48e570` → `0x48e238`, `0x48e5c5` →
  `0x48e284`, `0x48ee98` → `0x48e930`, `0x48ef95` → `0x48eb2c`, and the part
  lists' derived slots → `0x48fe18`, `0x48ff0c`). The closure's callers are
  size and write slots only; it has no row.
- **Dumps (slot 5).** Reached only through the dump base `0x47a594` (callers
  `0x48e0cf`, `0x48fa47`, `0x490fb7`, `0x49168e`, `0x491e07`) and derived
  dump slots (`0x48e2c7` → `0x48e0b8`, `0x48e5f7` → `0x48e2b0`, `0x48efe0`
  → `0x48ebec`, the part lists' → `0x48ff60`).
- **Destructors (slot 6).** These objects come from the arena `0x47a2c8`
  (`[0x6c5830]`, load.c `class_construct`) and are never destroyed one by
  one. `0x476754` is also called directly from the derived destructors
  `0x47698d`, `0x476b0f`, `0x4909e7` and `0x490a9d`. The horizon object
  (`0x49afb0`, stored only at `0x49aee0`) has no slot called on it.
- **Base tables.** `0x4a5e88` and `0x4a6a14` are overwritten in their
  constructor chains, so no finished object carries `0x48dce0`
  (`push ebp; mov ebp, esp; or eax, -1; pop ebp; ret`) or `0x48f19b`.
  `0x48dce0` is also slot 0 of `0x4a0c74` (grid, hook `0x46befc`),
  `0x4a14c4` (constructor `0x478838`), `0x4a62ed` (`0x490b64`) and `0x4a6a40`
  (height cache `0x495268`); own code calls slot 0 on none of these, and no
  final table of `class_construct` has it as its tag.

### Stream-write thunks

Rows in `sim/dispatch/services/stream_writes.h`, own code in
`sim/src/view.c` next to the stream-read thunks. Each calls the stream's
write slot `[[S] + 0x14](S, n, p)` (cdecl, the thunk pops) and returns
with the slot's EAX, EDX and ECX.

| Row | Arguments | n | p | Slot entry EAX / EDX / ECX |
|---|---|---|---|---|
| `0x48f3ce` (listing `0x48f3ce..0x48f3e9`) | `(S, k, p)` | `k << 2` | `p` | `[S]` / `p` / `n` |
| `0x491156` (`0x491156..0x491170`) | `(S, k, p)` | `k + k` | `p` | `[S]` / `p` / `n` |
| `0x492479` (`0x492479..0x492493`) | `(S, k, p)` | `k + k` | `p` | `[S]` / `p` / `n` |
| `0x4911a3` (`0x4911a3..0x4911bd`) | `(S, w)` | 2 | the caller's argument slot of `w` | `[S]` / `p` / entry ECX |

`0x4911a3` has the bytes of `0x492447` (row `stream_put_word`). Callers:
`0x48f3ce` from `0x4829b3` (the font write `0x4828f4`, fill.c
`gl_font_write`) and `0x48ebb4`; `0x491156` from `0x482a08` (the font
write), `0x48eb89` and `0x490f7f`; `0x4911a3` from the persistent classes'
write slots (among them `0x48fd43`); `0x492479` from `0x4831ec`, `0x4902f7`,
`0x49057c`, `0x491650`, `0x491668` and `0x491dbd`. The font write reaches
them only through the store closure `0x47a1ac`, so in the own process they
stay unentered.

Validation: `differential_stream_writes_test` (`stream_writes_cases.h`),
2,000 random cases of 1..8 calls with random entry registers, k from 0..16
and random, p in a random buffer; a host stub in the write slot logs its
arguments, the bytes at p and the entry EAX, EDX (as "is p") and ECX.
Compared: the three exit registers, DATA, the heap and the stub log; the
mutation 0x492479 `k + k` → `k << 2` is caught. Not checked live.
