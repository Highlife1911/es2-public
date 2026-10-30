# EarthSiege II DAT tables

Status: **static readers and complete CD inventory checked**;
**runtime consumers of the simulator `WEAPONS.DAT`, the 216-byte vehicle
files and `PROJ.DAT` traced** (method in
[Runtime consumers](#runtime-consumers)).
There is no common DAT header. Simulator, shell, graphics and map resources
use different layouts, including two incompatible `WEAPONS.DAT` files.

**Hypothesis** marks proposed meanings or schemas without a reader.
Unknown fields must remain uninterpreted in a reimplementation.

## Evidence, notation and scope

Inputs are the 13 `VOL/*.VOL` archives and loose `DATA/*.DAT` under
`build/extracted/cd`, plus `VER95/DBSIM.EXE` and `VSHELL.EXE`.
All 17 input-file hashes and all 140 DAT archive-member hashes match
[iso_file_hashes.sha256](../../data/iso_file_hashes.sha256). The inputs are
`VER95/DBSIM.EXE`, `VER95/VSHELL.EXE`, `VOL/SIMVOL0.VOL`, `VOL/SHELL0.VOL`,
`VOL/ZONES.VOL`, `DATA/MAT0.DAT` and `DATA/MFORMS.DAT`.

This document gives layouts and statistics, not copies of the tables.

Offsets below are relative to the **DAT payload**, unless explicitly called
record-relative, runtime-relative or archive offsets. Archive payloads start
nine bytes after the member block; the extra VOL suffix is excluded
([vol.md](vol.md)). Executable addresses are VAs at base `0x00400000`.

All numeric reads are little-endian. `W` denotes a two-byte bit pattern whose
signedness is not established; `D` similarly denotes four bytes. `i16/i32`
and `u8/u16/u32` specify a demonstrated interpretation. Bulk reads establish
byte spans, **not** the signedness or subdivision of every field in them.
Word views of otherwise opaque spans in the scratch audit are diagnostic,
not proof that every two bytes are a separate original field.

## Complete member inventory

39 DAT members in SHELL0, 62 in SIMVOL0, 39 in ZONES,
none in the other ten `VOL/` archives, and two loose DAT files: **142 inputs,
67,539 payload bytes**. No DAT path repeats between these archives. Both
loose files are byte-identical to their SIMVOL0 counterparts. Basename-only
lookup would incorrectly merge the two weapon tables. Override order is not
established here.

The following lists enumerate every member. Names in the first two tables
have `.DAT` appended; their directory is part of their identity.

| SIMVOL0 `dat\` family | Members | Count / bytes each |
| --- | --- | --- |
| Vehicle configuration | `OUTLAW`, `RAPTOR2`, `TOMAHAWK`, `SAMSON`, `COLOSSUS`, `APOCA`, `OGRE`, `MAVERICK`, `RAZOR`, `MONGOOSE`, `STINGRAY`, `MIRIMAC`, `RAMSES`, `ACHILLES`, `HYPERION`, `PITBULL`, `SPIDER`, `DIABLO`, `HEADHUNT`, `SCARAB`, `CERBERUS` | 21 / 216 |
| Separate flyer configuration | `SKIMMER` | 1 / 46 |
| Weapons / projectiles | `WEAPONS`, `PROJ`, `ROCKETS`, `BULLETS`, `BEAM`, `LC_WPNS` | 3,790; 974; 72; 170; 62; 68 |
| Vehicle debris | `OUTL_DEB`, `RAPT_DEB`, `TOMA_DEB`, `SAMS_DEB`, `COLO_DEB`, `APOC_DEB`, `OGRE_DEB`, `MAVE_DEB`, `MONG_DEB`, `STIN_DEB`, `MIRI_DEB`, `RAMS_DEB`, `ACHI_DEB`, `HYPE_DEB`, `PITB_DEB`, `DIAB_DEB`, `HEAD_DEB`, `SCAR_DEB`, `CERB_DEB` | 18 / 128; `PITB_DEB` / 146 |
| Other debris | `BASE_DEB`, `DEF_DEB` | 622; 264 |
| Base / formation data | `BFORMS`, `BASES`, `BASECOL`, `MFORMS`, `FFORMS` | 3,186; 6,422; 4,938; 142; 92 |
| Rendering / effects | `EXPLOS`, `EXPLOS2`, `COLORS`, `FIRE`, `SHADES`, `TEX`, `MAT0`, `STYLES` | 964; 956; 54; 8; 56; 64; 244; 258 |

For shell per-Herc families, suffixes are exactly
`APOC, COLO, MAVR, OGRE, OUTL, RAPT, RAZR, SAMS, TOMA`.

| SHELL0 `GAM\` family | Members | Count / bytes |
| --- | --- | --- |
| Arming graphics | `ARM_` + each of the nine suffixes | 9 / 926–3,186 |
| Repair graphics | `RPR_` + each of the nine suffixes | 9 / 702–2,146 |
| Initial loadouts | `INI_` + each of the nine suffixes | 9 / 32–64 |
| Hit regions | `ARM_HOTS`, `RPR_HOTS` | 998; 902 |
| Weapon graphics | `ARM_WEAP` | 364 |
| Vehicle / weapon data | `HERCS`, `HERC_INF`, `TRN_HERC`, `WEAPONS`, `DAMAGE` | 148; 146; 432; 707; 102 |
| Campaign / maps | `CAREER`, `THEATER`, `MAP_LOCS`, `MAP_INFO` | 148; 58; 1,108; 148 |

ZONES has 39 `DAT\ZONE<number>.DAT` members, all 16 bytes. Numbers:
`15, 22, 39, 111, 123, 222, 234, 333, 401, 402, 403, 404, 405, 406, 407,
408, 409, 410, 444, 501, 502, 503, 504, 505, 506, 507, 508, 509, 510,
555, 666, 777, 888, 1400, 1700, 1800, 3333, 3334, 7000`.
Loose `DATA` contributes only `MAT0.DAT` and `MFORMS.DAT`.

## Simulator weapons: `dat\WEAPONS.DAT`

A signed-count word followed by 33
variable-length records, with no strings or trailing bytes. Payload starts
at SIMVOL0 archive offset `0x6a7eda`.
The header bytes are `21 00`. Records occupy 60–232 bytes, not a constant
stride. Their in-memory stride is separately `0x58`.

Let `R` be a record start, `A` its first nested count and `B` its second.

| Relative offset | Width | Representation |
| --- | --- | --- |
| `R+0` | 6 | Three `W` values (runtime `+0,+2,+4`); observed 1,500–15,000 / −1 / 511 (all zero for IDs 0, 26, 27); meanings are a hypothesis |
| `R+6` | 2 | `i16 A`, observed 0–1 (runtime `+6`) |
| `R+8` | `4*A` | Array of four-byte entries, allocated and pointed to by runtime `+8`; all 30 entries are bytes `14 00 0c 00` |
| `R+8+4*A` | 2 | Selector word, observed 19 in all records (runtime `+0x18`) |
| `R+10+4*A` | 2 | `i16 B`, observed 0–21 (runtime `+0x12`, array pointer `+0x14`) |
| `R+12+4*A` | `8*B` | Array of eight-byte entries; sphere-region layout (see below) |
| `T = R+12+4*A+8*B` | 48 | Fixed parameter tail, detailed below |

Thus record size is `60 + 4*A + 8*B`. Across all 33 records, `sum(A)=30`
and `sum(B)=211`: `2 + 33*60 + 4*30 + 8*211 = 3790`.

The prefix reader `0x40cff8` is the same
routine that fills the 18-byte part-group records of the vehicle damage
tables (loop `0x40d109–0x40d11e`); it also sets runtime `+0xc` to −1 and
dword `+0xe` to zero, which are therefore not file data. In those vehicle
tables the 18-byte record is word 0 = base maximum, word 6 = entry count,
and each four-byte entry carries a part index in its second word
([vehicle.md](../reverse-engineering/vehicle.md), `part_ratio` in
[ai.md](../reverse-engineering/ai.md)). The B array uses the region reader
`0x40c7c4` (count word, then pointer), the layout that `sphere_hits`
`0x40c524` consumes as spheres of words x, y, z, radius
([weapons.md](../reverse-engineering/weapons.md#sphere_hits-40c524)).
**Open:** no instruction that reads runtime weapon-record offsets
`0x00–0x21` through the weapon object (`W+0x1c`) or through the two
`0x40fe84` lookups was found, so the part-record and sphere meanings above
are layout analogies, not demonstrated weapon behaviour.

Reader chain in DBSIM: `0x40fc8c`, filename reference `0x40fd38`, count read
`0x40fd84–0x40fd94`, loop `0x40fdc3–0x40fde9`, record reader `0x40f8bc`.
That reader calls `0x40cff8` for the eight-byte prefix and `4*A` bytes,
then `0x40cc14 → 0x40c7c4` for selector, count and `8*B` bytes, and reads
48 bytes into runtime record `+0x22`. At `0x40f8df`, bytes
`66 bf 30 00` supply that size; `0x40f8f9` explicitly clears runtime `+0x48`.
Pointers, the name pointer at `+0x52`, and record index at `+0x56` are not
serialized. Names are supplied from executable data at `0x498eb0`.

### Parameter tail and supported meanings

`T+x` maps to runtime weapon record `+0x22+x`. Every row's byte range is
accounted for. Names are restricted to what the use sites establish.

Below, `C` is the runtime weapon record (the weapon object keeps it at
`W+0x1c`, stored by the common constructor `0x40df30`), `W` the weapon
object and `D` its hardpoint record (`W+0x18`). Classes are those of
[weapon_classes.md](../reverse-engineering/weapon_classes.md): ballistic
`0x498d54`, energy `0x498dcc`, ELF `0x4992c0`. Distances are world units
(6 game millimetres, [player_state.md](../reverse-engineering/player_state.md)).
Timer values count down by the frame scale word `0x4d3b64`
(`0x467894`: elapsed milliseconds × 256 / 125, clamped to 64–450,
[timing.md](../reverse-engineering/timing.md)), so one unit is about
0.49 ms of game time.

| Tail offset | Width | Operation / hypothesis |
| --- | --- | --- |
| `+0x00,+0x02,+0x04,+0x06` | 4 × `i16` | Weapon shape index per mount variant. `0x40df30` passes `C` and the hardpoint byte `D+6` to `0x40fab0`, which reads `C+0x22+2*D+6` (only when `D+6` ≤ 3) and loads that entry of the `mechwpns` shape list (resource type `0x140008`, name tables `0x4a9904/0x4a99f4`, shapes `[0x4a98f4]`). `+0x04` is also read on its own by `0x40f57c` (`0x40f658`) as the shape of the debris object spawned when the weapon is shot off |
| `+0x08` | 2 | `W`, observed 0 / 500 / 15,000; **open:** no reader found |
| `+0x0a` | 4 | `i32` minimum range. `0x40e5f8(W, d)` returns 1 only if `C+0x2c` < `d` < `C+0x30` (signed); `d` is the `0x4928d0` distance from the shooter to the aim point (`0x41f5a0`) or a target distance (`0x410996`, `0x41f440`). Zero in all records |
| `+0x0e` | 4 | `i32` maximum range in world units, upper bound of the same test; also the beam length passed to immediate impact (`0x40eacf`, `0x40ecb4`), and `0x4110d7` skips weapons whose value is ≤ 0. Observed 0–75,000 (450 m) |
| `+0x12` | 2 | AI selection penalty. `0x41f358` scores each weapon as `Q10(100, damage)` − `Q10(1000, s16 C+0x34)` plus a random term (`0x41f4e4–0x41f53f`), keeping the highest score. Observed 0–600; **hypothesis:** an energy/heat cost weight |
| `+0x14` | 2 | Minimum charge to fire. Energy-class ready test `0x40ecdc`: required = `C+0x38` if `C+0x36` ≥ `C+0x38`, else min(`C+0x36`, `W+0x7b`); ready if that is ≤ the charge `W+0x7d`. The emitter then fires a reduced shot with the remaining charge (`0x40b5a0`) when cost > charge. ELF test `0x40eda0` requires max(`C+0x36`, `W+0x7b`) ≤ charge, or, with the latch byte `W+0x33` set, `C+0x38` ≤ charge. Equal to the cost for lasers, 300–400 for EMP/particle beams whose cost is 10,000 |
| `+0x16` | 2 | Cost per shot. Ballistic emitter `0x40e964` subtracts it from the round count `W+0x7b` (5 rounds per burst for ATC*, 1 for racks); energy and ELF emitters subtract it from the charge `W+0x7d` (`0x40ea58`, `0x40ec64`). Energy capacity is not from the DAT: the energy constructor `0x40e074` sets `W+0x7b`=`W+0x7d`=`Q10(820,1200)`=960 |
| `+0x18` | 2 | Initial rounds, ballistic class only (`0x40e140`: `W+0x7b` = value, dword `W+0x7d` = value << 8). Racks 6/8/10/24 hold exactly their names' counts. The ballistic `V54` `0x40f520` reports "enough ammunition" while rounds ≥ value / 8. Ignored by the energy constructor (PLAS/MAGN carry 20) |
| `+0x1a` | 2 | `W`, firing-layout selector; value 3 (only ID 19) adds two side shots at `0x40eb24` |
| `+0x1c` | 2 | `i16`, projectile selection; `0x4100a0–0x4100db`: 34 looks `PROJ` up by (class 0, the slot's secondary word, 5 → 0) through `0x40ffc8`; 33 selects none; other values are direct `PROJ` indices (`0x40ffb0`). Value 19 also arms the alternating latch in `0x40ea58` (`0x40ec03`) |
| `+0x1e` | 6 | Muzzle offset (x, y, z words, model coordinates in world units). Preparation `0x40e788` copies `C+0x40/+0x42/+0x44` as the shot origin before adding the mount offset and transforming; the three-shot layout uses (0, y, z) and (−x, y, z). Observed y 630–2,725 (forward) |
| `+0x24` | 2 | Mount offset magnitude along the first axis, read by `0x40f904(C, D+6)` (`0x40f919`) for the variants that use it: variant 2 gives (−v,0,0), 3 gives (v,0,0) |
| `+0x26` | 2 | Never used: the loader clears runtime `+0x48` and `0x40f904` writes 0 for the middle component. Zero in the corpus |
| `+0x28` | 2 | Mount offset magnitude along the third axis: variant 0 gives (0,0,v), 1 gives (0,0,−v); variant 4 gives (0,0,0) and variants > 4 keep (`+0x24`, 0, `+0x28`). `0x40f540` adds the hardpoint vector `D+0x10..0x15` |
| `+0x2a` | 2 | Cooldown in timer units: `0x40e788` stores `Q10(W+0x63, C+0x4c)` into the timer word `W+0x31` (`W+0x63` is 1,024 from the constructor); `timer_word_step` `0x467a48(W+0x30)` decrements it every maintenance call; `weapon_ready` requires 0. 1,200 ≈ 0.59 s at the nominal scale. The latch path stores value >> 2 instead (`0x40ec13`) |
| `+0x2c` | 2 | Weapon term of the vehicle rating at `0x41edd8`: for each weapon whose `V54` is true, `+= Q8(C+0x4e, 256 − condition)` (`0x41ee3b–0x41ee65`), see the vehicle field `0x42` below. Observed 0–1,000 |
| `+0x2e` | 2 | Cockpit weapon icon. `0x437d22–0x437d5b` adds it to the HUD slot's base index and selects that image of the `weapons.hba`/`weapons.dba` bitmap array (`[0x4d16d0]`, loaded at `0x43793f`). Observed 0–13 |

**Statistics:** initial rounds 0–2,000; cost per shot
0–10,000; cooldown 0–5,000; projectile selector 0–34. These are raw values;
the damage dealt comes from `PROJ.DAT`, not from this table.

## Simulator vehicle configurations

### The 21 files of 216 bytes

DBSIM `0x4201a8` resolves a vehicle name, then
`0x420204–0x420210` reads `0xd8` bytes into `config+2`. The size instruction
at `0x420207` is `68 d8 00 00 00`. The runtime object starts with a generated
two-byte index and has further pointers/state after the serialized block.
Do not treat a captured runtime configuration as an unchanged DAT record.

Every file in the vehicle inventory has exactly one
216-byte block; no count or signature precedes it. All ranges below cover
the block once. `W[n]` describes an observed word view when original
subdivision is still unknown. Names in this table are **hypotheses**.

**Notation.** `S` = `[vehicle+0x1f2]+2`: the runtime configuration
pointer plus two, so `S+x` is file offset `x` (code that does not add two
reads runtime `+(x+2)`; offsets below are always file offsets). Clip/animation
IDs are passed to `0x402374` (track for an animation of the vehicle shape)
or compared with the current clip of the track `[vehicle+0x22c]`. Angles
are 16-bit binary angles (65,536 per turn). "Per 125 ms" means the value
goes through `movement_step` `0x467924` (×scale/256, scale as in the weapon
section). Evidence for `vehicle_control` `0x416a04` is in
[ai.md](../reverse-engineering/ai.md#vehicle-control-416a04-and-its-helpers);
for `0x4195c8` and the collision in
[player_movement.md](../reverse-engineering/player_movement.md).

| File offset | Width | Interpretation / evidence |
| --- | --- | --- |
| `0x00` | `i16` | Turn-rate scale. `0x416a04` tail computes `u` from `S+0` and the speed, then `Q10(1600, u)`, multiplies by the turn command (Q8) and approaches that rate. Raw 600 (PITBULL 300); physical rate not established |
| `0x02` | `i16` | Reverse-speed parameter, −182 to −79; rescaled on load; reverse command and limit in `0x416a04` |
| `0x04` | `i16` | Forward-speed parameter, 140–325; rescaled on load; forward command and limit in `0x416a04`, upper clamp after a collision (`0x41a4ea`) |
| `0x06` | `i16` | Speed step: `approach_word(vehicle+0x28e, command, S+6)` (`0x416c71`), applied per control call without time scaling. 25–30 |
| `0x08` | `i16` | Turn-rate step: `approach_word(vehicle+0x28c, w, S+8)` (`0x417439`). 150 |
| `0x0a` | `i16` | Shape node index. `V24` `0x417b98` (`0x417ba8`) and the collision (`0x419251`, `0x4192ab`) look it up with the shape manager's `V20`; dword `+0x1c` of the node record is the height that `sight_check` adds to both ends of its line and that the collision uses for the hit point. 5, 10 or 12 |
| `0x0c` | `i16` | Clip ID targeted while accelerating (`0x416a04`) |
| `0x0e` | `i16` | Clip ID targeted when the speed exceeds `S+0x2c` |
| `0x10` | `i16` | Walk clip ID (positive speed or turn); also the clip of the initial track `vehicle+0x22c` (`0x415d95`, negative means no track) and the clip set after a collision (`0x41a4da`) |
| `0x12` | `i16` | Walk clip ID for negative speed or turn (`0x416a04`) |
| `0x14` | `i16` | Height of the vehicle origin above the terrain: added to `terrain_height` before the vertical position is written (`0x417b5d`, `0x41a411`, `0x41e4cd`, `0x4257aa`, `0x461900`); three of these sites add a further 5,000 for the flyer branch (`0x4e` ≠ 0; `0x417b64`, `0x4257ba`, `0x46190a`). 0 (400 COLOSSUS, 512 RAZOR) |
| `0x16` | `i16` | Shield sphere centre: the vector (0, 0, `S+0x16`) is transformed by the impact matrix in `shield_geometry` `0x413cc4` ([weapons.md](../reverse-engineering/weapons.md#vehicle-shield-geometry-413cc4)). 0–1,000 |
| `0x18` | `i16` | Shield/hit radius: radius of that sphere (`0x413d28`, `0x413e21`) and added to the coarse range test of the vehicle hit receiver (`0x418be2`). 1,000–2,500 |
| `0x1a` | `i16` | Torso-twist clip ID: track `vehicle+0x230` (`0x415dec`, none if negative); the twist controller calls `0x479344(track, S+0x1a, twist angle >> 2)` (`0x41a6a6`) |
| `0x1c` | `i16` | Twist rate scale: commanded rate = `Q8(command, S+0x1c)` (`0x41a569`) |
| `0x1e` | `i16` | Twist acceleration: the rate `vehicle+0x294` approaches the command by `movement_step(S+0x1e)` (`0x41a5e3`) |
| `0x20` | `i16` | Twist limit: the twist angle `vehicle+0x298` is clamped to ±`S+0x20` (`0x41a627`). 14,000 ≈ 77° (PITBULL 32,767) |
| `0x22` | `i16` | Pitch clip ID, track `vehicle+0x234` (`0x415e11`, `0x41a955`) |
| `0x24,0x26` | 2 × `i16` | Pitch rate scale and acceleration, same scheme as `0x1c/0x1e` on `vehicle+0x296` (`0x41a821`, `0x41a89b`) |
| `0x28,0x2a` | 2 × `i16` | Pitch angle upper and lower limit for `vehicle+0x29a` (`0x41a8df–0x41a8f7`). 3,500/6,000 and −2,000/−4,000 |
| `0x2c` | `i16` | Rescaled parameter; gait speed threshold in `0x416a04`: speed clamp while the walk clip runs and switch to clip `0x0e` above it |
| `0x2e` | 20 bytes | Signed-byte list of shape node indices ended by a negative byte (at most 50 scanned). `0x420090` maps each node of the first detail level to the matching node of every further level; skipped when the shape has one level. Every file ends its list inside the span |
| `0x42` | `i16` | Base of the vehicle rating `vehicle+0x29e` computed by `0x41edd8`: base + weapon terms (weapon tail `+0x2c`) + `Q8(group maximum, 256 − condition)` for groups 0–18 − `S+0x7c+2i` for parts 0–9 with condition > 180/256, then >> 4. Read by the AI comparisons `0x41cabc` (each side adds `RNG & 0x3e8`; a difference below 400 counts as even) and `0x41cb44`/`0x41cd78`. 1,000 everywhere; **hypothesis:** a vulnerability rating (it grows with damage) |
| `0x44` | `i16` | Fall clip ID (`0x416a04` "vehicle down" branch; special ID in `0x4195c8`) |
| `0x46` | `W` | 2 in every file; **open:** no reader found |
| `0x48` | `i16` | Compared with 2 in the destruction code (`0x4179a5`, `0x4180b5`); values above 2 add tests of further part words (from part 10). 2 (PITBULL 4, SPIDER 0); **hypothesis:** leg count |
| `0x4a` | `i16` | Nonzero at destruction (`0x4185d2`) sets byte `vehicle+0x38`, moves the vehicle to Z = −100,000 and calls `0x413e50(vehicle+0x4d, 0x49983e)`. Only SPIDER has 1 |
| `0x4c` | `i16` | Collision momentum factor: closing value = `Q10(S+0x4c, speed)` of each side in `ground_collide` (`0x4191e4`, `0x41920a`). 5,000–20,000 |
| `0x4e` | `i16` | Branch selector: zero enables ground-speed rescaling; nonzero loads an additional `.FM` resource (`0x42040f–0x420435`), raises the terrain offset by 5,000 and selects the other HUD formula. Only RAZOR has 1 |
| `0x50` | `i16` | Cockpit colour set: `0x429acd` copies the 24 dword entries starting at entry 32 + 24 × value of the `cockpit` resource into entries 42–65 of the table `[0x49b020]` (`0x4303c4`); **hypothesis:** palette colours. 0–8 on the nine player Hercs, 0 elsewhere |
| `0x52` | `i16` | Q10 value weight (1,024 = 1.0): `0x418e60` returns `Q10(0x41537c(parts), value)`, halved when part group 0 is intact; `0x423e88` sums that over the vehicles of other sides whose down flags (`vehicle+0x92+0x12` or `+7`) are set; `0x41b8bf` sums it over vehicles within 45,000 units per side. Zero sets byte `vehicle+0xb3` at creation (`0x415d73`), which makes `0x418e60` return 0 |
| `0x54` | `i16` | Weapons can be shot off: nonzero lets a hit on group ≥ 19 detach the weapon with probability `(RNG & 0xfff) < 41k` (`0x4189eb–0x418a6b`, k = 10, or 3 when the side byte is 0). 1 (PITBULL 0) |
| `0x56` | 12 bytes | NUL-terminated ASCII label slot on all 21 files. **Open:** no DBSIM reader found; runtime names come from the table `[0x4a9d60]` indexed by the vehicle ID |
| `0x62,0x64` | 2 × `i16` | Vehicle `V30` `0x4155c4` returns (0, `S+0x62`, `S+0x64`) as the camera offset of the attached camera mode 2 (`0x45edc5` → `0x40111c`). `S+0x64` is also subtracted from weapon mount heights before the aim angles are computed (`0x41a7b7`, `0x41f141`) |
| `0x66,0x68` | 2 × `i16` | Second `V30` vector (0, `S+0x66`, `S+0x68`): orbit-camera centre offset (mode 1, `0x40109c`) and the AI aim offset added to a target's position (`0x41eafd`). `0x66` is 0, `0x68` 0–2,600 |
| `0x6a` | `i16` | Overwritten by the loader with the vehicle ID (`0x42021f`); later reads (`0x415f81`, `0x41b500`, …) see the ID, never the file value (0 in every file) |
| `0x6c` | `i16` | Rescaled parameter, raw −182 to −24; negative gait threshold paired with `0x2c` in the deceleration branch of `0x416a04`, and the lower clamp of the reversed speed after a collision (`0x41a4ee`) |
| `0x6e` | `i16` | Collision radius, returned by vehicle `V5c` `0x415518` and `V7c` `0x41552c` and used by `ground_collide`. 750 |
| `0x70` | 5 bytes | Signed-byte category list of attached objects, ended by a negative byte. `0x415e2e` counts them into `vehicle+0x23c` and creates one object each from the class table `[0x4a9698]`; `0x4195c8` treats category 2 specially |
| `0x75` | 5 bytes | Signed-byte shape node index per attached object (`0x41962b`), e.g. 14, 15, 12 |
| `0x7a` | `i16` | Stand clip ID (`0x416a04`, `0x4195c8`) |
| `0x7c–0x8f` | `W[10]` | Rating deduction per intact part 0–9 in `0x41edd8` (`0x41eed6–0x41eeed`). 500 |
| `0x90–0x93` | `W[2]` | 500; **open:** not read by `0x41edd8`, no other reader found |
| `0x94` | `i16` | Resource/skin-table index at `0x4203d6–0x4203ee`, range 0–6 |
| `0x96,0x98,0x9a` | 3 × `i16` | Footstep "fire" heights `k` = 0, 1, 2 (runtime `C+0x98+2k`, `0x419877`, `0x41988e`): `k` is 0 (positive rate) or 1 while the clip is `0x0c`, `0x7a`, `0x10` or `0x12`, and 2 for clip `0x0e`; an attached node crossing the height plays sound 0x29 when near |
| `0x9c` | `i16` | 400/800; **open:** no reader found |
| `0x9e–0xa9` | 12 bytes | All zero in this corpus; **open:** no reader found |
| `0xaa,0xac,0xae` | 3 × `i16` | Footstep "arm" heights for the same three cases (runtime `C+0xac+2k`, `0x419831`, `0x419848`) |
| `0xb0–0xbd` | 14 bytes | All zero in this corpus; **open:** no reader found |
| `0xbe` | `i16` | Shield maximum. `0x413a90` at creation (`0x415d47`) sets the words `vehicle+0x228/+0x22a` to it, the front and rear shield words `vehicle+0x222/+0x224` to half of it and `+0x226` to 512; `0x417bec` recomputes the maximum from it and part conditions. 3,500 (SPIDER 0); same unit as the damage words absorbed in `0x413cc4` |
| `0xc0` | `i16` | HUD scale slot; overwritten for the ground branch, retained by the other branch (`0x41bb5a`) |
| `0xc2` | `i16` | Denominator of ground rescaling; **not established as friction** |
| `0xc4` | `i16` | Numerator factor of ground rescaling; **not established as acceleration** |
| `0xc6` | `i16` | Loader overwrites this zero-in-file slot with original forward-speed parameter; `0x41d28a` compares that copy with 185 to choose an AI state |
| `0xc8–0xcb` | 4 bytes | Zero in all files; **open:** no reader found |
| `0xcc` | 12 bytes | NUL-terminated ASCII debris-resource slot; empty is allowed; checked at `0x420456–0x420483` |

"No reader found" means the search of [Runtime consumers](#runtime-consumers) did not find one: a register walk
from every `[reg+0x1f2]` load (runtime offsets with and without the `+2`,
through register copies, `add`/`lea` and frame slots, up to 400
instructions or `ret`), plus the five references to the configuration
array `[0x4a9d64]`. Readers that reach the configuration through another
pointer would be missed.

Strings are bounded slots, not length-prefixed strings. Only bytes before the
first NUL have demonstrated text meaning; no wider code page can be inferred
from these ASCII-only labels. No parser should require unused slot bytes or
unknown zero spans to remain zero in other versions.

### File-to-runtime speed conversion

Let `f(x)` be signed little-endian file word `x`.
For `f(0x4e)==0`, `0x4202be–0x42036c` does the following:

```text
config+0xc8 = f(0x04)                         (original forward parameter)
Q = trunc_toward_zero((f(0xc4)*400*65536) / f(0xc2))
config+0xc2 = low16((f(0x04)*315) >> 10)       (HUD scale)
config+(2+x) = low16((f(x)*Q) >> 16)          for x = 0x02,0x04,0x2c,0x6c
```

The division is signed (`0x47e068`); the multiplication helpers extract
bits after signed multiplication (`0x47e08d`, `0x47e0b0`). The expressions
above describe the observed non-overflowing inputs; a general implementation
must preserve x86 bit truncation. All ground-branch denominators are nonzero.

`0x41621c–0x416259` chooses runtime `config+4` or `config+6` by the sign of
`actor+0x290`, then multiplies by that signed Q10 throttle. `0x41bb3c`,
especially `0x41bb7f–0x41bb94`, confirms the earlier HUD formula:

`display_KH = trunc_toward_zero(i16(actor+0x28e) * i16(config+0xc2) / i16(config+6))`.

Concrete data cross-check: `OUTLAW.DAT` starts at archive offset `0xdd61a7`.
Its `0x04` bytes `45 01` and `0xc2–0xc5` bytes `5c 12 0a 00` produce
`Q=55775`, runtime forward parameter 276 and HUD scale 99. Those last two
values independently agree with the captured configuration in
[player_state.md](../reverse-engineering/player_state.md). This is agreement
with existing evidence, not a new runtime capture or proof of vehicle identity.
Across ground-branch files, computed forward parameters span 99–601 and
HUD scales 43–99. File forward parameters are therefore not directly km/h.
The known six-millimetre world-coordinate scale does not establish the units
of every DAT offset, angular rate or acceleration candidate.

### `SKIMMER.DAT`: separate 46-byte configuration

DBSIM `0x422ed0`, read at
`0x422f0e–0x422f14`, requests exactly `0x2e` bytes at runtime object offset
zero, followed by separate model/collision/flight-model resources. This is
not the 216-byte layout. File spans: `0x00–0x11` = 18 parameter bytes,
`0x12–0x1d` = 12-byte NUL-terminated ASCII label, `0x1e–0x2d` = 16 parameter
bytes. Individual numeric widths, signedness and units within the opaque
spans remain open. The audit's word view does not resolve them.

## Other simulator weapon and debris structures

### Fixed-count tables

Each row below is `i16 N` at offset zero,
then exactly `N` records at `2+i*stride`. There is no string or trailer.
Unknown records remain raw spans; interpreting all their words as signed is
only a diagnostic view. In `PROJ`, record `+0/+2` are signed selector words
used by `0x40ffc8` and the dispatch at `0x40e9a4–0x40e9e2`.

| Member | N / stride | Reader evidence in DBSIM |
| --- | --- | --- |
| `PROJ.DAT` | 27 / 36 | `0x40fe18–0x40fe5b`; index stride at `0x40ffb0`; bytes `1b 00` at file 0 |
| `ROCKETS.DAT` | 5 / 14 | `0x40a818`, count/read at `0x40a882–0x40a8ca` |
| `BULLETS.DAT` | 12 / 14 | `0x40ade0`, `0x40ae56–0x40ae9e` |
| `BEAM.DAT` | 10 / 6 | `0x40b6e0`, `0x40b766–0x40b7a4` |
| `LC_WPNS.DAT` | 3 / 22 | `0x405fac`, `0x4061ec–0x406252` |

`ROCKETS` and `BULLETS` share a stride, not a proved common semantic schema.
Projectile speed, damage, lifetime, colours and energy-unit names remain
**hypotheses** until individual use sites are traced.

**Field readers, `PROJ.DAT`** (runtime array `[0x4a98fc]`, records
copied unchanged; flight and impact contracts in
[weapons.md](../reverse-engineering/weapons.md)):

| Record offset | Width | Static use |
| --- | --- | --- |
| `+0` | `i16` | Class. Lookup key of `0x40ffc8(class, index)`. Class 0 = rocket (`0x40a948` looks up `(0, n)`, flight `0x40a538`), class 2 = bullet (`0x40af6c` looks up `(2, n)`, flight `0x40b124`), class 4 = immediate impact (`0x40bf74` looks up `(4, type)`). The ballistic emitter chooses the rocket constructor when the word is 0 (`0x40e9ab`), the energy emitter immediate impact when it is 4 (`0x40ea90`). Class 3 occurs (records 7–9); its consumer was not traced |
| `+2` | `i16` | Index within the class: second lookup key, and the `ROCKETS`/`BULLETS` record index that the constructors store in the object byte `+0x41` (`0x40a988`, `0x40afae`) |
| `+4` | `i16` | Damage word, copied to impact packet `+6` (`0x40c00f`, flight `0x40b277`) after the emitter's `Q10` scale and `0x426b04`; the vehicle receiver passes it to `shield_geometry` as the damage the shields absorb first (`0x418c00`). The AI weapon score uses it when the target's facing shield is nonzero (`0x41f4bc`) |
| `+6` | `i16` | Damage word, copied to packet `+4` (`0x40bfeb`, `0x40b267`) with the same scaling; the vehicle receiver adds it to the dword `vehicle+0x288` (`0x418dfd`), which a vehicle timer clears. The AI score uses it against a zero shield (`0x41f4b6`), and value + 10,000 when `+4` exceeds the shield. Which word is armour damage is **open** |
| `+8` | `i16` | Packet `+8` (0/500/1,000); **open:** read only by receivers, not traced |
| `+0xa` | `i16` | Rocket speed cap: the averaged speed is capped at this value (`0x40a619`); 0 for classes 3/4 |
| `+0xc` | 4 × `i16` | Explosion types for vehicle hits, one chosen by `RNG & 3` (`0x418d44–0x418d5d` → `explosion_create`); the packet carries the address of `+0xc` |
| `+0x14` | 4 × `i16` | Explosion types for terrain hits, chosen by `RNG & 3` (`0x426528` step 8) |
| `+0x1c` | 4 × `i16` | Explosion types for building and other receivers (`0x405227`, `0x421d91`, `RNG & 3`) |

**Field readers, `ROCKETS.DAT` and `BULLETS.DAT`** (selected by the
object byte `+0x41` through `0x40a234`/`0x40adc0`):

| Offset | `ROCKETS` (flight `0x40a538`) | `BULLETS` (flight `0x40b124`, constructor `0x40b43c`) |
| --- | --- | --- |
| `+0` | Shape index into `[0x4a96d8]` (`0x40a966`) | Shape index into `[0x4a9700]` (`0x40af8a`) |
| `+2` | Lifetime in updates: expires when the word counter `O+0x54` exceeds it (no time scaling) | Lifetime: expires when `O+0x54` (+`movement_step(512)` per update) exceeds value << 9, i.e. about value × 62.5 ms |
| `+4` | Acceleration: `movement_step(+4)` is added to the speed `O+0x52` (per 125 ms) | Segment radius word of the impact query |
| `+6` | Segment radius word of the impact query | Animation period: copied to `O+0x59` at creation, nonzero enables the frame timer |
| `+8` | Animation period (gate and reset of `O+0x5d`) | **Open** (0/8/22) |
| `+0xa` | Index of the animation state word | Angular spread: `(RNG & 2v) − v` is added to two of the three launch angle words (`0x40b4b8–0x40b4ec`) |
| `+0xc` | **Open** (5 in all records) | **Open** (0/1) |

`BEAM.DAT`, `LC_WPNS.DAT`, `SKIMMER.DAT`, the debris files and the base
tables are not traced for runtime use; their open spans stay as described.

### Debris: all 21 `*_DEB.DAT` files

`i16 G`, then `G` variable groups. Each group
has `W id` at `+0`, `i16 K` at `+2`, then `K` records of 14 bytes at `+4`.
Each child is seven separately read words at `+0,+2,...,+12`.
DBSIM `0x40874c` reads the group count and loops through `0x4083f8`;
`0x408452–0x4084b4` reads the seven words. Child `+2` is accumulated;
`+8/+10` use signed `-1` sentinels, otherwise multiply by 182
(`0x4084b7–0x4084f6`). Angular conversion is a **hypothesis**; other units
and the proposed mesh/mass meanings are open.

The 18 small vehicle files have seven groups with one child each;
`PITB_DEB` has eight with one each. `BASE_DEB` has 8 groups/42 children,
`DEF_DEB` 6 groups/17 children. Total: 21 files, 3,336 bytes, all consumed.
**Exception to a tempting schema:** nine-word flat records fit the small
files only. The original reader and the two general debris files prove the
nested layout.

## Shell vehicle/loadout and weapon tables

### `HERC_INF.DAT`

`i16 N=9`, followed by nine 16-byte records, total 146.
VSHELL `0x411e40`, count at `0x411e98`, multiplies by 16 and reads into
`0x483c38` at `0x411eb0–0x411ebc`. File record `+x` corresponds to
`0x483c38 + 16*i + x`. There are no strings.

| Record offset | Width | Raw range / interpretation |
| --- | --- | --- |
| `+0` | `i16` | 0–8, Herc ID; compared in `0x411ef6–0x411f33` |
| `+2` | `i16` | 25–80, displayed at `0x4473ae`; **hypothesis:** weight in tonnes |
| `+4` | `i16` | 46–200, displayed at `0x44740d`; **hypothesis:** shell nominal speed in km/h |
| `+6` | `i16` | 3–10, displayed at `0x447458`; **hypothesis:** available hardpoints |
| `+8` | `i16` | 55–230, multiplied by 1,000 in `0x41169a–0x4116a1`; **hypothesis:** build/salvage cost |
| `+10` | `W` | 30–125; unknown |
| `+12` | `i16` | 1–3, build-progress divisor/countdown in `0x411688`, `0x4116aa–0x4116df`; mission-duration unit is a hypothesis |
| `+14` | `W` | 0–1, tested at `0x411ef6–0x411f00`; **hypothesis:** initial availability |

The UI reads and signedness are established; the unverified labels/units
remain hypotheses. The shell speed values must not
replace the simulator's separately computed HUD parameters.

### Loadout record, `INI_*`, `HERCS` and `TRN_HERC`

VSHELL record reader `0x41149d` reads four header
words, then a slot ID and three more words for each item. It calls
`0x41205a` for those three words. All index/count uses are signed.

| Record offset | Width | Meaning / status |
| --- | --- | --- |
| `+0` | `i16` | Herc ID |
| `+2` | `W` | Condition/progress value; 100 in the INI/TRN corpus; percent-health meaning is a hypothesis |
| `+4` | `W` | Build countdown candidate; zero in INI/TRN |
| `+6` | `i16 K` | Number of serialized slot entries |
| `+8+8*j` | `i16` | Slot ID, used to index a pointer array (`0x411522–0x41152a`) |
| `+10+8*j` | `i16` | Weapon/item ID |
| `+12+8*j` | `W` | Condition candidate, 100 in INI/TRN |
| `+14+8*j` | `W` | Ammunition-variant candidate, 5 in INI/TRN |

Record length is `8+8*K`. All nine INI files contain one record, with
`K=3–7`; combined size 432. The filename table at `0x47083c` is selected
by `0x41cbb1`; `0x41cc70` calls the record reader.

`HERCS.DAT`: `i16 count=5`, then five repetitions of `i16 bay_slot` and a
loadout record. VSHELL `0x410b4b–0x410c48` proves the additional slot word
and the count loop; the total is 148 bytes. It is an initial bay population,
not the nine-entry vehicle specification table.

`TRN_HERC.DAT`: **data fit, consumer open**. Nine consecutive
loadout records consume all 432 bytes, without an outer count or bay-slot
prefix. No literal filename reference was found in either executable;
whether this resource is used by these binaries remains open.

**Exception:** the seven-entry `INI_APOC` and `INI_SAMS` records,
and corresponding TRN records, use slot IDs `0–5,7`. A count is not a bound
on the largest serialized slot ID. Slots are unique within each parsed
record; item IDs are within the shell table's 0–32 domain.

### Shell `GAM\WEAPONS.DAT`

707 bytes at SHELL0 archive offset
`0x8676fd`.
`i16 N=33` precedes these variable records:

| Record-relative offset | Width | Interpretation |
| --- | --- | --- |
| `+0` | `i16` | Weapon ID, indexes runtime stride `0x1d` |
| `+2` | `i16 L` | String byte count, includes NUL; observed 4–7 |
| `+4` | `L` | ASCII name, exactly one final NUL in this corpus |
| `+4+L` | `u16` | Cost candidate; zero-extended and multiplied by 1,000 |
| `+6+L` | `u8` | Availability flag candidate, observed 0–1 |
| `+7+L` | `W` | Priority candidate, observed 0–99 |

Then `i16 M=39`, followed by `M` six-byte entries: item ID, condition word,
ammunition-variant word. These use the same `0x41205a` reader as loadouts.
Exact consumption: `2 + 469 + 2 + 39*6 = 707`.

VSHELL `0x41261a–0x412733` proves both count loops and the extra priority
word. `0x41237b` reads length/string/two-byte cost/one-byte flag; its
`0x4123b6–0x4123c0` explicitly performs the unsigned ×1,000 conversion.
Observed raw costs span 0–50. Cost, availability and priority names are
**hypotheses** beyond these demonstrated operations. Localized display
strings are subsequently supplied separately (`0x412676–0x412690`).

### `DAMAGE.DAT`: shell coefficients

102 bytes. VSHELL read sequence
`0x413c79–0x413cf6` is:

| File offset | Width | Representation / use |
| --- | --- | --- |
| `0x00` | `i16` | Common coefficient |
| `0x02` | 12 | Six signed coefficients |
| `0x0e` | 18 | Nine signed coefficients |
| `0x20` | `i16` | Second common coefficient |
| `0x22` | `i16 N` | 33 |
| `0x24` | `2*N` | 33 signed per-item coefficients |

`0x413d03–0x413dee` combines the first arrays with `HERC_INF` record `+8`,
including ×1,000. `0x413df4–0x413e1f` combines the last array with the
second common coefficient, using a fixed 33-iteration loop. The resulting
arrays feed `0x413f8c` and condition-dependent calculations with `/100`.
**Hypothesis:** repair/build costs by part and item. This does not establish
simulator armour hit points or damage resistance. Original malformed-count
behavior is not a safe parser specification.

## Shell graphics: `ARM_*`, `RPR_*`, hotspots

These are graphics/placement tables.
They are not a demonstrated armour-statistics family. Words named flags
below are raw fields; bit meanings remain **hypotheses**.

Three serialized image-record shapes occur:

| Record | Relative offsets and widths |
| --- | --- |
| A22 (22 bytes) | `+0 i16 id`, `+2 D x`, `+6 D y`, `+10 D x2`, `+14 D y2`, `+18 W frame`, `+20 W flags` |
| R14 (14 bytes) | `+0 i16 id`, `+2 D x`, `+6 D y`, `+10 W frame`, `+12 W flags` |
| I12 (12 bytes) | `+0 i16 id`, `+2 D x`, `+6 D y`, `+10 i16 frame` |

The x/y pairs feed UI placement; A22's second pair is an outline-position
candidate. Signed coordinate interpretation is a hypothesis: bulk reads and
increment/add operations alone do not prove it. Frames are indexed signed
by the I12 consumer at `0x43eea3–0x43ef1a`. Screen-pixel units are
supported by UI usage but have not been calibrated independently.

### Nine `ARM_<vehicle>` files

Two A22 records at `0x00` and `0x16`, then `i16 G` at `0x2c`.
At `0x2e` begin `G` groups: `i16 item_id`, `i16 K`, then `K*A22`.
`G=20–26`; item IDs index the weapon table. All nine files consume exactly
`46 + sum(4+22*K)` bytes, totaling 18,158.

Reader: VSHELL `0x41508f`, nine-name table `0x47006c`, initial records at
`0x41510f–0x41521e`, nested counts at `0x415221–0x41529e`, children at
`0x4152c1–0x415437`. The runtime image stride is 26, **not** the 22-byte
serialized stride. The loader increments all four child coordinates by one
(`0x415335`, `0x415353`, `0x41539f`, `0x4153bd`); these increments are not
file bytes. The group for item zero is present and must not be discarded.

### Nine `RPR_<vehicle>` files

`i16 B`, `B*R14`, one additional R14, `i16 G`, then
`G` groups of `i16 item_id`, `i16 K`, `K*R14`.
`B=4–12`, `G=19–28`; all nine consume exactly
`18 + 14*B + sum(4+14*K)` bytes, totaling 12,240.
The standalone R14 is an internal-view image candidate.

Reader: VSHELL `0x414340`, filename table `0x46ffd4`, initial count/records
`0x4143b8–0x4144a1`, additional record `0x4144a7–0x414512`, nested groups
`0x414515–0x414699`. Runtime stride is again 26. The common runtime
representation does not make ARM and RPR disk records interchangeable.

### `ARM_WEAP.DAT`

`i16 N=26`, `26*I12`, `i16 M=4`, `4*I12`; size 364.
Reader regions `0x43edf3–0x43ef57` and `0x43ef5d–0x43f088` prove both
lists. IDs in the second list form a separate UI index domain.
Another consumer (`0x4486de–0x448873`) reads the first list and second count,
then closes the stream, leaving the final 48 bytes unread. They are not
padding: the first consumer reads them.

### `ARM_HOTS.DAT`, `RPR_HOTS.DAT`

`i16 N=9`, then nine groups of `i16 vehicle_id`, `i16 K`,
`K` 16-byte rectangles. Each rectangle has four 32-bit coordinate fields
at `+0,+4,+8,+12`. No per-rectangle ID is serialized. Exact rectangle
convention (two corners versus origin/extent) remains a **hypothesis**.

VSHELL `0x43c866/0x43c8e0` selects the resource;
`0x43c991–0x43ca7f` checks nine groups and reads 16-byte children.
The stored vehicle ID is read into a temporary; allocation uses loop order
(`0x43c9fd–0x43ca3a`). Both files have IDs 0–8 in that order.
ARM has 60 rectangles (3–10 per group), RPR 54 (six each):
`2+9*4+16*60=998`, `2+9*4+16*54=902`.

## Auxiliary simulator structures

These structures have **static read spans**, with two limitations:
`FIRE` has an external count that was not independently reconstructed, and
`MAT0` has an unread suffix. Resource names are role hints, not proof of all
field meanings. Numeric units within opaque spans remain unknown. No text
strings were identified in these structures.

### Formations and bases

| Member | Complete framing | Reader evidence |
| --- | --- | --- |
| `MFORMS.DAT` (archive and loose) | `i16 N=5`; at `2+28*i`, 28 opaque bytes, viewed as 14 words | DBSIM `0x41fe30–0x41fe80` |
| `FFORMS.DAT` | `i16 N=5`; at `2+18*i`, 18 opaque bytes, viewed as nine words | DBSIM `0x422e0e–0x422e59` |
| `BFORMS.DAT` | `i32 N=17`; per group: `i32 K`, `10*K` bytes, three four-byte fields, `i32 M`, then `M` lists each with `i8 L` and `L` bytes | DBSIM `0x405fac`, `0x40602c–0x406168` |

BFORMS has 145 ten-byte elements and 112 byte lists. Length bytes are
sign-extended at `0x406127`; all observed lengths are nonnegative. The byte
lists are numeric data, not encoded text. These nested counts consume all
3,186 bytes. Coordinate/formation membership meanings are **hypotheses**.

`BASES.DAT`: `i16 N=65`. Per record, 20 header bytes, with `i16 K` at
header `+18`; `K*30` child bytes; then a 28-byte tail. The header is read as
five words, six bytes, and two words. Tail reads are six bytes, two bytes,
four bytes, then eight two-byte fields. All 110 children consume
`2+65*48+110*30=6422` bytes. Reader `0x405fac`, particularly
`0x4062ce–0x406513`, proves the variable child list. Runtime stride is 60;
neither 48 nor 60 is a fixed disk-record stride.

`BASECOL.DAT`: **no independent top-level count**. `0x40658e–0x4065ad`
uses the BASES count (65), calling `0x40ccf8` per entry. Each entry is:
`i16 G`, then `G` groups of `W id`, `i16 K`, then `K` shapes. A shape is
`W selector`, `i16 P`, `P*8` opaque bytes. Readers
`0x40ccf8 → 0x40cc50 → 0x40cc14 → 0x40c7c4` prove this nesting.
Totals: 36 groups, 80 shapes, 543 eight-byte elements; all 4,938 bytes
consumed. Geometry/part meanings are hypotheses, but the implicit dependency
on BASES holds.

### Effects, colours and shading

| Member | Framing / counts | Reader evidence in DBSIM |
| --- | --- | --- |
| `EXPLOS.DAT`, `EXPLOS2.DAT` | `i16 A`; `A*4` mapping bytes; `i16 B`; `B*40` parameter bytes. A=20/18, B=22 in both; 964/956 bytes | `0x407ca3–0x407db3`; branch selects one name; mapping word `+2` is used at `0x407e14` |
| `COLORS.DAT` | Fixed 54-byte block; audit view 27 words, **not a proved count-prefixed list** | `0x467b64–0x467b81` reads `0x36` bytes regardless of the first word |
| `FIRE.DAT` | Four-byte header, then four remaining payload bytes on this CD | `0x46b30c–0x46b338`; byte read count comes from the previously loaded shape array, not directly from the DAT header; equality of that external count to four remains unverified |
| `SHADES.DAT` | `u8 capacity=11`, `u8 G=5`; per group `u8 id`, `u8 K`, `2*K` bytes; 22 pairs total | `0x42e760`, `0x42e799–0x42e854`; size 56 |

### `MAT0.DAT`: material mapping and unread suffix

Both copies have `i32 N=13` at `0`, then 13 eight-byte
records at `4+8*i`. A pair of 32-bit values is a useful data view; precise
material semantics and signedness of each component remain open.
DBSIM `0x46c7bb–0x46c80c` and VSHELL `0x4296f3–0x429750` read that prefix.
VSHELL closes the stream immediately afterward. The DBSIM routine likewise
does not consume a further MAT0 table in this inspected path.

**Exception:** prefix size is 108, but both files are 244 bytes. The 136 bytes
at `0x6c–0xf3` are preserved as an opaque suffix in the audit. They are
nonempty original payload, not VOL padding. That suffix is divisible into
17 further eight-byte chunks, but calling those active records would be a
**hypothesis** inconsistent with the inspected count loop. No complete
semantic schema or alternate consumer of the suffix is claimed.

### `ZONE*.DAT`

All 39 members are four four-byte fields at `+0,+4,+8,+12`,
with no count, signature, string or trailer. DBSIM constructs the decimal
name from `zoneXXXX` at `0x42790c–0x427944`; four separate reads occur at
`0x427988–0x427a19`. The third value is used in signed comparisons/shifts
at `0x427a2a–0x427a3d`. Observed first/second values are 7–8, third 12–15,
fourth 90–256. **Hypotheses:** dimension exponents, spatial scale, and a
terrain/render coefficient; no physical units are assigned here.

## Campaign and data-only candidate structures

### `CAREER.DAT`

`i16 G=6`; each group is `i16 id`,
`i16 K`, then `K` signed two-byte string indices. VSHELL `0x412f2a`
reads the count and loops through `0x412e88`, which reads the two header
words and resolves each index using `0x4087f0`. Counts are 10–11 and sum
to 61; `2+6*4+61*2=148`. There are no strings in the DAT itself.
Career/theatre grouping semantics remain a **hypothesis**.

### Five layouts without an identified original reader

The following are **hypotheses as schemas**, with **data fits**.
Their proposed counts consume every byte, but field widths, signedness and
meaning require consumer confirmation. Do not use them as proved executable
contracts. The separate `TRN_HERC` consumer limitation is described above.

| Member | Candidate framing, offsets and verified fit |
| --- | --- |
| `THEATER.DAT` | `u16 N=4`; 14-byte records at `2+14*i`: `W +0`, `W +2`, `D +4`, `D +8`, `W +12`; 58 bytes |
| `MAP_INFO.DAT` | Five groups until EOF: `W id`, `u16 K`, then `K` records of `W id,W value`; group counts 5,7,7,6,7; 148 bytes |
| `MAP_LOCS.DAT` | Same group headers/counts; children 34 bytes: `W id` at `+0`, 32 opaque bytes at `+2` (eight-dword diagnostic view); 1,108 bytes |
| `TEX.DAT` | `u32 N=15` at 0, then 15 four-byte values at `4+4*i`; 64 bytes |
| `STYLES.DAT` | `u16 N=4` at 0, then four 64-byte spans at `2+64*i`; 258 bytes |

MAP group IDs are 0–4 and child IDs run from 1 through each group count in
both files. This cross-file agreement supports the candidate grouping;
it does not prove that the eight MAP_LOCS dwords are coordinates or establish
how the original uses either file. All five candidates are binary; no text
encoding is established or needed by the candidate parses.

## Verification results and exceptions

**Audit result:** every member in every listed family, and both
loose files, was parsed in scratch with explicit bounds checks and an exact
EOF assertion. There were no unexplained gaps or size failures in the final
cursor audit. This means **byte accounting**, not complete semantic decoding:
133 inputs have complete framing supported by inspected readers; six have
data-only framing (the five candidates plus TRN_HERC); FIRE's four remaining
bytes fit a reader whose external count is unverified; and two MAT0 copies
retain an explicit 136-byte opaque suffix after their read prefix.
Every opaque span is included in the accounting.

The substantive exceptions/cautions are:

- Simulator WEAPONS has variable disk records and a different runtime stride.
- Ground vehicle fields are transformed after loading; disk `0xc0` is not
  the final ground HUD scale. Runtime offsets include a two-byte prefix.
- Seven-entry loadouts can contain slot ID 7; count and highest ID differ.
- Debris files share a nested grammar; the apparent flat small-file format
  fails on BASE_DEB and DEF_DEB.
- ARM/RPR disk image records differ from each other and from runtime stride.
- One ARM_WEAP consumer intentionally leaves the second list unread.
- FIRE's second read depends on a shape count outside the DAT itself.
- Both MAT0 copies contain the same unread suffix; its role remains open.
- No consumer is known for the five candidate layouts or TRN_HERC.

`llvm-objdump.exe -d --x86-asm-syntax=intel` generated the two scratch
disassemblies. For an individual function, also pass `--start-address` and
`--stop-address`: whole-section x86 disassembly can lose alignment across
embedded data. The instructions, bytes and addresses cited here are the
evidence, not objdump's nearest exported-symbol labels.

## Independent check

An own parser confirms: DAT member
counts 62/39/39 and the two loose files identical to their SIMVOL0 copies;
simulator `WEAPONS.DAT` 33 records, `sum(A)=30`, `sum(B)=211`, 3,790 bytes
consumed; 21 vehicle files of 216 bytes; `OUTLAW.DAT` gives `Q=55775`, runtime
forward parameter 276 and HUD scale 99 (matching the live calibration
276 → 99 K/H in [player_state.md](../reverse-engineering/player_state.md));
`HERC_INF.DAT` 2+9×16 bytes; shell `WEAPONS.DAT` 33 + 39 entries, 707 bytes
consumed; all 21 debris files consumed exactly by the nested grammar; DBSIM
bytes `68 d8 00 00 00` at `0x420207` and `66 bf 30 00` at `0x40f8df`.

## Runtime consumers

Static only. Inputs: the hash-checked `VER95/DBSIM.EXE`,
LLVM objdump listings per function, and the CD members read through the
inventory offsets. The runtime uses of the fields above were found this way:

1. Locate the runtime record of each table: the weapon record `C` through
   the weapon object (`W+0x1c`, constructor `0x40df30`), the vehicle
   configuration through `vehicle+0x1f2` (loader `0x4201a8`, record array
   `[0x4a9d64]`, stride `0x21e`), the `PROJ` record through `0x40ffc8` and
   the projectile objects' `+0x4e`.
2. Register walk from every load of that pointer (copies, `add`/`lea`,
   frame slots; stops at `ret`), listing every displacement that maps into
   the serialized span, then reading each hit in its function. False hits
   (other classes with a field at the same offset, notably building
   records behind their own `+0x1f2` in `0x403000–0x406000`) were dropped
   by checking the object class at each site.
3. Units follow from the use: world distances through `0x4928d0`/`0x49288c`,
   timers through `0x467a48` and the scale word `0x4d3b64`, rates through
   `movement_step` `0x467924`, angles through the 16-bit heading words,
   and every value was compared with the decoded corpus (ranges quoted in
   the rows).

Fields the runtime **never uses as file data** (overwritten or ignored):
weapon tail `+0x26` (cleared at `0x40f8f9`, zeroed again in `0x40f904`),
weapon tail `+0x18` for energy and ELF classes, vehicle `0x6a` (vehicle ID
written at `0x42021f`), vehicle `0xc0` on the ground branch and `0xc6`
(written by the loader). Spans with **no reader found** by the walk:
weapon header `R+0..` and its sphere array (layout analogy only), weapon
tail `+0x08`; vehicle `0x46`, the label `0x56`, `0x90–0x93`, `0x9c`,
`0x9e–0xa9`, `0xb0–0xbd`, `0xc8–0xcb`; `PROJ +8`; `ROCKETS +0xc`;
`BULLETS +8`, `+0xc`. A reader reached through another pointer (for
example a copied record) would not be found by this method, so "no reader
found" is not proof that the bytes are unused.

## Remaining work

The layouts and the runtime mapping come from the CD bytes and DBSIM/VSHELL.
Names without corroborating use evidence are hypothetical; unverified names,
notably friction/acceleration at `0xc2/0xc4`, remain hypotheses. The nested
original debris reader supersedes a flat interpretation on the wider corpus.
The raw corpus verifies the vehicle block size and strings; DBSIM proves the
speed transformation.

Open: the spans listed as "no reader found" in
[Runtime consumers](#runtime-consumers), the tables not traced for runtime
use (`BEAM`, `LC_WPNS`, `SKIMMER`, debris, `BFORMS`/`BASES`/
`BASECOL`, effects, `ZONE*`), which `PROJ` damage word is armour damage, the
six unlocated consumers, FIRE's external count, MAT0's suffix, runtime
reachability and behavioral validation of the runtime-use readings.
