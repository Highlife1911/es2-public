# Weapon IDs, classes and emitters

Binds the simulator weapon IDs to the runtime classes (vtables) and V28
emitters that the live entry probes count
([live_coverage.md](../reference-game/live_coverage.md)). Contracts of the
emitters and flight code: [weapons.md](weapons.md). Addresses refer to the
hash-checked DBSIM image (`VER95/DBSIM.EXE`, SHA-256 `8561aa47…c4ad`, image
base `0x400000`); the bodies below are read from its `llvm-objdump -d -M intel`
listing and its vtables.

Own code: `core/src/weapon_classes.c` (`core/include/es2/core/weapon_classes.h`,
`//core:weapon_classes`), reached through three hook groups whose adapters are
`tools/reference-game/proxy/<group>_hooks.h`: `weapon_methods` (29 hooks, the
slot bodies `+2Ch` … `+60h`), `weapon_ctors` (9 constructors) and
`weapon_syncs` (13 hooks: six gauge syncs, the LAEW emission, six empty
bodies). Nothing in `sim/` or `shell/` calls these functions; they run as
replacements inside the original process. In-vitro tests:
`//tools/reference-game/invitro:differential_weapon_methods_test`,
`differential_weapon_ctors_test` and `differential_weapon_syncs_test`
(`weapon_methods_cases.h`, `weapon_ctors_cases.h`, `weapon_syncs_cases.h`);
unit test `//core:weapon_classes_test`.

## Weapon factory `0x40fff8`

**Confirmed (static).** `0x40fff8` builds one weapon group (vtable
`0x499338`) and creates one weapon object per hardpoint. Callers: `0x41050e`
and `0x417684`, so the same factory serves every vehicle that carries
weapons, not only the player. Per hardpoint it reads the weapon ID as a
signed word (`0x410070`; negative IDs become 0 at `0x410084`, ID 0 creates
nothing), resolves the weapon record through `0x40fe84`, and dispatches on
the ID: `cmp eax,0x20` / `ja` at `0x4100e4`, byte index table at `0x4100fd`
(33 entries) and dword jump table at `0x41011e` (10 targets).

Weapon names come from a pointer table at `0x498eb0` (33 entries, short
internal identifiers, ID 0 empty).

| IDs | Names | Case block | Constructor → vtable | V28 body |
| --- | --- | --- | --- | --- |
| 1–5 | ATC20, ATC35, ATC50, ATC75, ATC100 | `0x410222` | `0x40e140` → `0x498d54` | `0x40e964` emission |
| 13–16 | 6, 8, 10, 24 | `0x410222` | `0x40e140` → `0x498d54` | `0x40e964` |
| 21 | MISSL | `0x410222` | `0x40e140` → `0x498d54` | `0x40e964` |
| 7–12 | EMP, LAS100, LAS200, LAS300, LAS400, LAS500 | `0x410146` | `0x40e074` → `0x498dcc` | `0x40ea58` type-dependent |
| 17, 19, 20 | PBEAM, EMP, PBEAM | `0x410146` | `0x40e074` → `0x498dcc` | `0x40ea58` |
| 23–25, 28 | EMP2, PBW2, PLAS, MAGN | `0x410146` | `0x40e074` → `0x498dcc` | `0x40ea58` |
| 6, 22 | ELF, ELF2 | `0x4101a0` | `0x40e074`, then `0x4992c0` stored at `0x4101f4` | `0x40ec64` alternate (immediate impact `0x40bf74`) |
| 26 | LAEW | `0x41027c` | `0x40e140`, then `0x499248` at `0x4102d0` | `0x40ea04` |
| 18 | ECM | `0x4102fe` | `0x40e274` → `0x498c64` | `0x40e95c` (no-op) |
| 29 | TARG | `0x410354` | `0x40e308` → `0x498b74` | `0x40e95c` |
| 30 | SHIELD | `0x4103aa` | `0x40e344` → `0x498afc` | `0x40e95c` |
| 31 | TURBO | `0x410400` | `0x40e2bc` → `0x498bec` (own V34 `0x40f0d0`) | `0x40e95c` |
| 32 | ENERGY | `0x410453` | `0x40e380` → `0x498a84` | `0x40e95c` |
| 0, 27 | (none), MINE | `0x4104a1` | no object | — |

The no-op V28 classes are equipment (ECM, targeting, shield, turbo, energy),
not guns. MINE (27) is not created by this factory.

**Hypotheses.** Names such as "6/8/10/24" are read as missile racks and
"ATC" as autocannons only from the identifiers. `0x40e964` chooses the
projectile constructor `0x40a9c4` (flight `0x40a538`) when the weapon type
word is zero and the bullet constructor `0x40b43c` (flight `0x40b124`)
otherwise ([weapons.md](weapons.md#emission-choice-40e964)); which IDs take
which branch depends on the weapon record and is not yet tabulated.

## Loadouts and measured V28 counts

**Confirmed (data).** `DATA/player.mec` of the prepared C1_01 (measurement
clone `timing-r111`, read with `player_mec_tool show`) holds two records:
record 0 items `3,3,7` (ATC50, ATC50, EMP), record 1 items `10,10,14`
(LAS300, LAS300, 8). This assumes MEC item IDs are the simulator weapon IDs
above (**hypothesis**, consistent with both being 33-entry tables).

Measured V28 calls (all hooks original): `c1_01-success` `0x498d54` 218 and
`0x498dcc` 44; `c1_01-collision` `0x4992c0` 92 only. No C1_01 MEC record
carries ELF/ELF2, so the ELF shots in `c1_01-collision` come from another
vehicle built by the same factory (AI), **hypothesis**. `acceptance-60s` has
no V28 call but 408 projectile flights of class `0x498448`: a projectile
source outside V28 exists (**hypothesis**: the outside-V28 emitter,
[weapons.md](weapons.md#outside-v28-emitter-4226a0)).

Class coverage over the published suites
([oracle_suites.md](../reference-game/oracle_suites.md)): `0x498d54`
(autocannon and missile IDs), `0x498dcc` (laser, EMP, particle beam, plasma,
MAGN) and `0x4992c0` (ELF) fire from the player and from AI vehicles;
`0x499248` (LAEW) was never seen. The equipment classes only need to be
mounted.

## Emitter behaviour

**Confirmed from instructions.** The BEAM table `0x498dcc` binds `emit`
(`0x40ea58`): type-record word 4 calls immediate impact `0x40bf74`; other
types choose `0x40b5a0` when the signed cost exceeds the current charge, and
`0x40b43c` otherwise. The scaled path can emit three shots when config+3c is
word 3, and config+3e word 19 enables its alternating charge latch. The ELF
table `0x4992c0` binds `alt_emit` (`0x40ec64`), which always debits charge and
calls immediate impact with scale word 1200. These record-level tests do not
identify which individual weapon IDs take each variant branch; no further
ID-to-class inference is made. Original immediate-impact callers also include
`0x4045c8` at call site `0x404a65`, with type word 3; it is an additional
source outside weapon V28. Exact field widths and call/return contracts:
[weapons.md](weapons.md#outside-v28-emitter-4226a0).

## Hangar mounting (VSHELL)

**Confirmed (static, VSHELL SHA-256 `1534aa8f…`).** The shell side of the same
IDs, verified with the loadout sweep
([vshell_preparation.md](vshell_preparation.md#hangar-rules-of-the-vshell-ui)):


- The hangar weapon list holds IDs 1..18, 22..25 and 29..32 (table at
  `0x476a4c`). IDs 19, 20, 21 (EMP, PBEAM and MISSL in the table above),
  26 (LAEW), 27 (MINE) and 28 (MAGN) cannot be mounted by the player; the
  factory still accepts them in a MEC record.
- Which slot takes which ID comes from the vehicle's ARM table (group key =
  item ID, record word = slot + 2), checked by `0x41501f`. The Razor missile
  rack 16 fits only the Razor.
- Mounting sets the item's secondary word to 1 for IDs 13..15 and to 5 for all
  others (`0x411bb6..0x411bde`); this is the per-slot MEC secondary word.

The published suites (`oracle_suites.md`) fit every ID except 26 at least once.

## The weapon-class methods

The slot bodies `+28h` … `+60h` of the weapon vtables, own code in
`core/src/weapon_classes.c` and hooked in group `weapon_methods`. Every
contract below is checked in vitro (section *Validation (in vitro)*).

### Classes and the slot matrix `+28h` … `+60h`

The constructors store eleven tables: two abstract bases (`WEAPON`
`0x498e44`, stored at `0x40df99`; `EQUIP` `0x498cdc`, stored at `0x40e252`)
and the nine classes the factory creates (table above). `0x496748` is the
runtime's pure-virtual trap. A dword scan of the image finds each body below
only in these tables, and the listing has no direct call of them except the
three noted (`0x40e644` → `0x40e62c`, `0x40f3b8` → `0x40f330`, `0x40f3d8` →
`0x40f00c`), so the receiver's table decides which body runs:

| Slot | WEAPON | GUN | LAEW | BEAM | ELF | EQUIP | ECM | TARG | SHIELD | TURBO | ENERGY |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| +28h | 40e788 | 40e964 | 40ea04 | 40ea58 | 40ec64 | 40e95c | 40e95c | 40e95c | 40e95c | 40e95c | 40e95c |
| +2Ch | 496748 | 40ed6c | 40ed6c | 40ecdc | 40eda0 | 40ecd4 | 40ecd4 | 40ecd4 | 40ecd4 | 40ecd4 | 40ecd4 |
| +30h | 40f8ad | 40f8ad | 40e71c | 40f8ad | 40e680 | 40e678 | 40e678 | 40e678 | 40e678 | 40e678 | 40e678 |
| +34h | 40ef94 | 40ef94 | 40ef94 | 40f00c | 40f3d8 | 40ef94 | 40ef94 | 40ef94 | 40ef94 | 40f0d0 | 40ef94 |
| +38h | 4111ce | 4111ce | 4111ce | 40f48c | 40f48c | 4111ce | 4111ce | 4111ce | 4111ce | 4111ce | 4111ce |
| +3Ch | 4111d3 | 4111d3 | 4111d3 | 40f4d8 | 40f4d8 | 4111d3 | 4111d3 | 4111d3 | 4111d3 | 4111d3 | 4111d3 |
| +40h | 4111d8 | 4111d8 | 4111d8 | 40f4f0 | 40f4f0 | 4111d8 | 4111d8 | 4111d8 | 4111d8 | 4111d8 | 4111d8 |
| +44h | 4111dd | 4111dd | 4111dd | 4116ad | 4116ad | 4111dd | 4111dd | 4111dd | 4111dd | 4111dd | 4111dd |
| +48h | 4111e2 | 4111e2 | 4111e2 | 40f504 | 40f504 | 4111e2 | 4111e2 | 4111e2 | 4111e2 | 4111e2 | 4111e2 |
| +4Ch | 40f89f | 4115f8 | 4115f8 | 4116cf | 4116cf | 40f89f | 40f89f | 40f89f | 40f89f | 40f89f | 40f89f |
| +50h | 496748 | 40f330 | 40f3b8 | 40f288 | 40f288 | 40f144 | 40f184 | 40f144 | 40f144 | 40f1f0 | 40f144 |
| +54h | 4111e9 | 40f520 | 40f520 | 4111e9 | 4111e9 | 4111e9 | 4111e9 | 4111e9 | 4111e9 | 4111e9 | 4111e9 |
| +58h | 40df00 | 40df1c | 40df1c | 40df08 | 40df08 | 40df00 | 40df00 | 40df00 | 40df00 | 40df00 | 40df00 |
| +5Ch | 40f8a4 | 40ed48 | 40ed48 | 40ed34 | 40ed34 | 40f8a4 | 40f8a4 | 40f8a4 | 40f8a4 | 40f8a4 | 40f8a4 |
| +60h | 40e62c | 40e644 | 40e644 | 40e62c | 40e62c | 40e62c | 40e62c | 40e62c | 40e62c | 40e62c | 40e62c |

Weapon dispatch sites (static, by owning function): `+28h` `group_fire`,
`fire_selected` (×2), `aim_fire`; `+2Ch` `chain_ready`, `fire_selected`
(×4), `weapon_choose`, `aim_fire`, `command_key` (×2), `player_input`
(×2); `+30h` `fire_selected` (×2); `+34h` `group_fire`, `weapon_choose`;
`+38h` `weapon_keys` (×2), `aim_fire`, `player_input`; `+3Ch` and `+40h`
`weapons_v3c` (`0x410d04`), `+3Ch` and `+48h` `group_toggle`; `+48h`
`group_fire` (×2); `+50h` `weapon_frame`; `+54h` `aim_select`,
`combat_rating`; `+58h` `weapon_find`; `+5Ch` `weapon_choose`; `+60h`
`weapon_tally`, `chain_ready`, `fire_selected`, `weapon_choose`,
`aim_fire`. No site with a weapon receiver was found for `+44h` and `+4Ch`
(their sites in `contact_scan`, `target_select` and `attack_plan` dispatch
actors). Most of these offsets are actor slots too, so the bridge table's
`slot_<off>` counters mix both kinds of receiver.

### Hook verdicts

- **Hooks (group `weapon_methods`, 29):** every body of the matrix that
  carries behaviour except the LAEW emission and the gauge syncs (below). A
  body shared by several tables is one hook; identical bytes at different
  addresses (`0x40ecd4`, `0x40e678`, `0x4111e2`, `0x40df00`; `0x40f8a4`,
  `0x4111e9`; `0x40df08`, `0x40df1c`) are separate hooks over one core
  function.
- **Gauge syncs of slot `+50h` (six bodies):**
  hooks of group `weapon_syncs`
  ([the weapon gauge syncs](#the-weapon-gauge-syncs-the-laew-emission-and-the-empty-bodies)).
  Their only dispatch site is `weapon_frame` (`0x410cb6`, once per weapon and
  rendered frame): they read the weapon's gauge widget (`[W+79h]` slots
  `+18h`/`+14h`; `[W+77h]` through `0x440e9c`/`0x4414cc` and slot `+10h`) and
  write it back; the ECM sync also posts the cockpit message
  `0x420a68(2Ah or 2Bh)`, the TURBO sync plays `sfx_play(2Ch)`. Some of
  them copy widget state into the weapon (ECM words `W+7Dh`/`+7Fh`, TURBO
  `W+7Dh`/`+81h`, BEAM/ELF `W+7Bh` when byte `W+34h` is clear, GUN the
  smoothed `W+7Dh` through `movement_step`): the player's toggles reach the
  simulation through the HUD widget. Rows in
  [function_verdicts.md](../reference-game/function_verdicts.md#weapon-class-methods).
- **Empty bodies** `0x40e95c`, `0x40f89f`, `0x4111ce`, `0x4111d3`,
  `0x4111d8`, `0x4111dd` (`push ebp; mov ebp,esp; pop ebp; ret`: the empty
  bodies of `+28h`, `+4Ch`, `+38h`, `+3Ch`, `+40h`, `+44h`) change no
  register and no memory. They are hooks of group `weapon_syncs`, accepted
  because the hook framework takes a body that ends in a final `ret`.
- **LAEW emission (slot `+28h`):** a hook of group `weapon_syncs`. It calls
  `weapon_prepare` at the same depth as `weapon_fire`, so preparation reads the same two uninitialized
  matrix-padding bytes of its caller's stack
  ([weapons.md](weapons.md#nested-frame-residues)); the hook needs
  `weapon_fire`'s residue entry shim and `0x40acac` as a guest call. LAEW
  (ID 26) fits no suite and cannot be mounted by the player, so the body is
  never entered live.

### Contracts

cdecl, `this` = W in the first slot. hi(x) is the high word of x, lo(x, w)
is x with its low word replaced by w, sx a sign extension, s16/s32 a signed
compare; "e" is the entry register. D = `[W+1Ch]` (the definition record:
words `+36h`, `+38h` charge thresholds, `+3Ah` capacity, `+56h` weapon
ID), T = `[W+20h]` (the type record), R the record of slot `+30h` (byte
`R+0Dh` the fire request). Bytes `W+43h` … `W+4Dh` are the flag block
(`W+43h` active, `W+46h` LAEW latch, `W+47h`/`W+48h` ELF phases, `W+49h`
ejected), word `W+31h` the cooldown, words `W+7Bh` (ammunition or level),
`W+7Dh` (charge), `W+7Fh` (transfer limit), dword `W+81h` (TURBO/LAEW
counter), word `W+84h`. The slot names are hypotheses from the bodies.

| Hook | VA | Slot | Arguments, writes | EAX | EDX | ECX |
| --- | --- | --- | --- | --- | --- | --- |
| `equip_ready` | 40ecd4 | +2Ch | – | 0 | e | e |
| `equip_trigger` | 40e678 | +30h | – | 0 | e | e |
| `weapon_priority` | 4111e2 | +48h | – | 0 | e | e |
| `weapon_gauge_match` | 40df00 | +58h | – | 0 | e | e |
| `weapon_spent` | 40f8a4 | +5Ch | – | lo(e, 1) | e | e |
| `weapon_stock` | 4111e9 | +54h | – | lo(e, 1) | e | e |
| `weapon_trigger` | 40f8ad | +30h | (W, R) | zx byte `R+0Dh` | e | e |
| `beam_gauge_match`, `gun_gauge_match` | 40df08, 40df1c | +58h | (W, x) | x = `[W+77h]` ? 1 : 0 | x | e |
| `beam_spent` | 40ed34 | +5Ch | (W) | sx byte `W+49h` | W+43h | = EAX |
| `gun_spent` | 40ed48 | +5Ch | (W) | 1 if word `W+7Bh` = 0 (EDX e) or byte `W+49h` ≠ 0, else 0 | W+43h | e |
| `gun_stock` | 40f520 | +54h | (W) | sx word `W+7Bh` ≥ sx word `D+3Ah` >> 3 (arithmetic, s32) | sx word `W+7Bh` | sx word `D+3Ah` >> 3 |
| `beam_priority` | 40f504 | +48h | (W) | lo(e.ECX, byte `W+43h` ≠ 0 ? 2710h : word `W+7Bh`) | W+43h | = EAX |
| `gun_clear` | 4115f8 | +4Ch | word `W+7Bh` ← 0 | W | e | e |
| `beam_fill` | 40f4f0 | +40h | byte `W+43h` ← 1, word `W+7Bh` ← 4B0h | W | W+43h | e |
| `beam_preset` | 40f4d8 | +3Ch | byte `W+49h` = 0: word `W+7Bh` ← 334h | W | W+43h | e |
| `beam_drain` | 4116ad | +44h | in order word `W+7Bh` ← 0, bytes `W+43h` ← 0, `W+34h` ← 1, `W+3Ch` ← 1 | W+3Bh | W+43h | e |
| `beam_clear` | 4116cf | +4Ch | words `W+7Dh`, `W+7Bh` ← 0, bytes `W+34h`, `W+3Ch` ← 1 | W+3Bh | 0 | e |
| `weapon_ammo` | 40e62c | +60h | (W, p): p ≠ 0: word `[p]` ← 0 | lo(p, 5) | e | e |

**`gun_ammo` `0x40e644`** (`+60h`, W, p): ECX ← T. Word `[T]` ≠ 0: the
body calls `0x40e62c(W, p)` (entry EAX = W, EDX = p, ECX = T) and returns
its registers: EAX = lo(p, 5), EDX = p, ECX = T, word `[p]` ← 0 when
p ≠ 0. Word `[T]` = 0: when p ≠ 0, ECX ← lo(T, word `W+7Bh`) and word
`[p]` ← CX; then `[W+20h]` is read again (T') and EAX = lo(T', word
`T'+2`), EDX = p. The core computes the direct call (a replaced
`gun_ammo` does not enter `weapon_ammo`; no counter). Branch sites
`0x40e654`, `0x40e658` (`weapon_ammo`: `0x40e634`).

**`gun_ready` `0x40ed6c`** (`+2Ch`, W): EDX = W+43h. Byte `W+49h` ≠ 0:
EAX 0, ECX e. Else ECX = (word `W+31h` = 0) (SETE, AND 1); 0: EAX 0; else
EAX = (word `W+7Bh` ≠ 0). Sites `0x40ed79`, `0x40ed89`, `0x40ed90`.

**`beam_ready` `0x40ecdc`** (`+2Ch`, W): ECX ← lo(e, word `D+36h`). If
s16 `D+36h` < s16 `D+38h`: DX = the larger (s16) of `D+36h` and `W+7Bh`;
EDX = lo(e.ECX, `D+36h`) when `D+36h` ≥ `W+7Bh`, else lo(**the caller's
EBX**, `W+7Bh`) (BX is loaded with `W+7Bh` and copied to EDX; EBX is saved
and restored around it). Else `[W+1Ch]` is read again (D') and EDX =
lo(D', word `D'+38h`). Then ECX = W+43h; byte `W+49h` ≠ 0: EAX 0. Else
ECX = (word `W+31h` = 0); 0: EAX 0; else EAX = (s16 DX ≤ s16 word
`W+7Dh`). Sites `0x40ecee`, `0x40ecf9`, `0x40ed0f`, `0x40ed1f`, `0x40ed25`.

**`elf_ready` `0x40eda0`** (`+2Ch`, W): EDX = W+43h; byte `W+49h` ≠ 0:
EAX 0, ECX e. Else ECX = lo(e, word `W+7Bh`), EDX = lo(D, word `D+36h`),
and ECX = EDX when s16 DX ≥ s16 CX. s16 CX ≤ s16 word `W+7Dh`: EAX =
lo(W, 1). Else EDX = W+33h; byte `W+33h` = 0: EAX 0; else ECX = `[W+1Ch]`
(read again), EDX = lo(W+33h, word `ECX+38h`), EAX = 0 when s16 DX > s16
word `W+7Dh`, else lo(W, 1). Sites `0x40edad`, `0x40edbd`, `0x40edc5`,
`0x40edcd`, `0x40edda`.

**`elf_trigger` `0x40e680`** (`+30h`, W, R): if word `D+56h` = 16h
(ELF2): ECX = W+43h, bytes `W+47h`, `W+48h` ← 1. Byte `W+48h` ≠ 0: EDX =
zx byte `R+0Dh`; a request returns it. Without one: if `[W+10h]` ≠ 0,
ECX = `[W+14h]` and word `[ECX]` ← 0; byte `W+48h` ← 0; EAX = EDX = 0. Byte
`W+48h` = 0: ECX = sx byte `W+47h`, EDX = 0; with a request and CX = 0:
ECX = W+43h, byte `W+47h` ← 1, word `W+84h` ← 0; without a request, CX ≠ 0
and `[W+10h]` ≠ 0: ECX = `[W+14h]`, word `[ECX]` ← 0, byte `W+47h` ← 0.
EAX = EDX at return (0, or the request byte). Sites `0x40e693`,
`0x40e6ad`, `0x40e6b7`, `0x40e6bd`, `0x40e6dd`, `0x40e6e2`, `0x40e6f8`,
`0x40e6fe` and `0x40e704` (**PROVED** fall-through: `[W+10h]` was read
nonzero at `0x40e6fa` and nothing is written before the second test).

**`laew_trigger` `0x40e71c`** (`+30h`, W, R): EBX = 0. Byte `W+46h` = 0:
with a request byte `W+46h` ← 1 (EDX = W+43h) and dword `W+81h` ← 4E20h;
EAX 0. Byte `W+46h` ≠ 0: with a request dword `W+81h` += 1000, and EAX 0
when it is then s32 ≤ 100000. Otherwise (no request, or the counter above
100000) EAX = 1, and with a request the button latch `0x45b734(1, 1)`
runs first (platform, guest call; entry EAX = `[W+81h]`, EDX = R, ECX e).
EDX/ECX: R or W+43h and e, or the latch call's. Sites `0x40e733`,
`0x40e739`, `0x40e751`, `0x40e768`, `0x40e772` (both edges: reached from
`0x40e751` without and from `0x40e768` with a request).

**`beam_step` `0x40f00c`** (`+34h`, W, a, f; BEAM's per-tick charge
transfer): EAX = 0, EDX = W+43h. Byte `W+49h` ≠ 0: return EAX = a, ECX e.
Else `weapon_step(W, a, f)` (`0x40ef94`, guest call; entry EAX 0, EDX
W+43h, ECX e). DX = (f's word ≠ 0 ? 0 : word `W+7Bh`) − word `W+7Dh` (EDX
= 0, or keeps the callee's high word); ID `D+56h` = 19h (PLAS): EDX
doubled (32 bit). s16 DX > 0: EAX = a when s16 a < s16 word `W+7Fh`, else
lo(D, `W+7Fh`); then EAX = EDX when s16 DX < s16 AX. s16 DX ≤ 0: byte
`W+43h` ← 0, EAX = lo(W+43h, FFFBh), EAX = EDX when s16 DX > −5. Word
`W+7Dh` += AX; EDX = `[W+1Ch]` (read again); PLAS: ECX = sx(AX) >> 1, word
`W+7Dh` −= CX. Return EAX = lo(a, a − AX), ECX the callee's unless PLAS.
Sites `0x40f024`, `0x40f034`, `0x40f04a`, `0x40f051`, `0x40f05a`,
`0x40f061`, `0x40f074`, `0x40f084`.

**`turbo_step` `0x40f0d0`** (`+34h`, W, a, f): ESI = 0. Dword `W+81h` ≠ 0:
dword `W+7Dh` −= 23h, and when it is then s32 ≤ 0, dwords `W+81h` and
`W+7Dh` ← 0. Byte `W+49h` ≠ 0: return EAX = a, EDX e, ECX e. Else
`weapon_step(W, a, lo(e.EDX, f))` (entry EAX W+43h, EDX lo(e.EDX, f), ECX
e); AX = 7D0h − word `W+7Dh` (EAX keeps the callee's high word); s16 AX >
0: ESI = 14h, then a when s16 a < 14h, then EAX when s16 AX < s16 SI;
dword `W+7Dh` += sx(SI). Return EAX = lo(a, a − SI), EDX/ECX the
callee's. Sites `0x40f0e5`, `0x40f0ef`, `0x40f103`, `0x40f11f`, `0x40f128`,
`0x40f12f`.

**`elf_step` `0x40f3d8`** (`+34h`, W, a, f): EAX = a, EDX = W+43h. Byte
`W+49h` ≠ 0: return, ECX e. Byte `W+47h` ≠ 0: `timer_word_step(W+83h)`
(`0x467a48`; entry EAX W+83h, EDX W+43h, ECX e; then POP ECX = W+83h); EDX
= (AX = 0); when the timer expired, P = `[W+10h]`: P = 0, word `P+24h` =
0, or the frame word `[[W+14h]]` = word `[[P+20h]]` − 1 (ECX = that word,
EDX = the count − 1, EAX = `[P+20h]`): bytes `W+48h` ← 1 and `W+47h` ← 0
(EDX = EAX = W+43h). Else `[[W+14h]]` ← (frame + 1) mod word `[[P+20h]]`
(EDX the remainder, ECX = W+10h, EAX = `[W+14h]`) and word `W+84h` ← 0.
Then `beam_step(W, a, lo(ECX, f))` (the direct call `0x40f47d`, entry
registers of the path), whose registers it returns. Sites `0x40f3ef`,
`0x40f3fb`, `0x40f416`, `0x40f41d`, `0x40f424`, `0x40f435` and `0x40f44c`
(**PROVED** fall-through: `[W+10h]` was read nonzero at `0x40f418` and
nothing is written in between). A zero count word with an expired timer
divides by zero in the original; that state is outside the domain.

**`beam_adjust` `0x40f48c`** (`+38h`, W, d): EDX = lo(e, 50h); word d = 0:
EDX += FFFFFF60h (32 bit: DX = −50h, the high word borrows). Word `W+7Bh`
+= DX, EDX = lo(EDX, `W+7Bh`); ECX = lo(e, 4B0h) when s16 DX ≥ 4B0h, 0
when s16 DX ≤ 0, else EDX; word `W+7Bh` ← CX; bytes `W+34h`, `W+3Ch` ← 1;
EAX = W+3Bh. Sites `0x40f49b`, `0x40f4b0`, `0x40f4bb`.

### Registers the hooks do not return

EDX:EAX are exact with one exception: on `beam_ready`'s path where s16
`D+36h` < s16 `D+38h` and s16 `D+36h` < s16 `W+7Bh`, EDX's high word is
the caller's EBX high word, which the adapter cannot see; it returns 0
there (the core takes that word as an input; the in-vitro originals run
with EBX's high word 0; the core test covers other values). The callers
use DX only. With the callers replaced (every dispatch site above is in a
hook) the original already runs with the guest bridge's EBX in that
position, so the replaced live runs have carried an arbitrary high word
there.

ECX: the framework returns the entry ECX; the originals' ECX is listed
above. A scan of the weapon dispatch sites (both edges, up to 60
instructions) finds ECX
written before any read after every `+2Ch`, `+30h`, `+3Ch`, `+40h`,
`+48h`, `+54h`, `+58h` and `+5Ch` site, or only `mov cx,…` before a use of
CX. The sites that carry ECX on: `group_fire` (after `+28h` the loop's
`mov cx,[ebp+0Ch]; push ecx` builds the next `+34h` slot, whose high word
only reaches the step's EAX high word, and `group_fire` stores AX),
`fire_selected` (`+28h`, then `+2Ch` at `0x410eea`), `chain_ready` and
`aim_fire` (into their `ret`; their contracts return "the last call's"
ECX, dead at their callers, [weapons.md](weapons.md#410970-chain_readyw-e-i-f)),
`weapon_tally` (`+60h` into its `ret`; `vehicle_update` does not read it).
Every such caller is a hook whose replacement calls the slot through the
guest bridge and reads EAX (and DX) only.

No hook needs an entry shim: none reads an uninitialized local or a
register other than EAX/EDX/ECX at entry (BEAM's EBX high word, above).

### Compare mode

Accepted by the 15 hooks without game calls and memory writes
(`equip_ready`, `gun_ready`, `elf_ready`, `equip_trigger`,
`weapon_trigger`, `weapon_priority`, `beam_priority`, `weapon_stock`,
`gun_stock`, `weapon_gauge_match`, `beam_gauge_match`, `gun_gauge_match`,
`weapon_spent`, `beam_spent`, `gun_spent`; no span). Also accepted:
`beam_ready` (a comparator on EAX and DX, so the EBX high word above does
not count) and the nine writers, with spans over W (`gun_clear` W+7Bh,
`beam_fill`/`beam_preset` W+43h..7Ch, `beam_adjust`/`beam_drain`
W+34h..7Ch, `beam_clear` W+34h..7Eh, `elf_trigger` W+47h..85h plus the
frame word `[[W+14h]]` as a one-hop region) or over the word behind p
(`weapon_ammo`, `gun_ammo`; a null p skips the comparison), registers
that hold W, W+3Bh, W+43h or p compared relative to the span
([compare_mode.md](../reference-game/compare_mode.md#mission-load-weapon-slot-and-weapon-class-leaves)).
Refused: the three steps (`beam_step`, `turbo_step`, `elf_step`) and
`laew_trigger` (game calls).

### Validation (in vitro)

`//tools/reference-game/invitro:differential_weapon_methods_test`
(`weapon_methods_cases.h`, three shards), quick tier: 100,000 direct cases
per hook (the full tier, 1,001,024, was not run). Per case the original body
(EBX = 0), the adapter (pair) and the core (pair and ECX) run over the arena
described in the case header with the direct callees `weapon_step`,
`timer_word_step`, `beam_step` and the latch `0x45b734` as logging stubs;
compared are the pair, ECX (core), the ordered calls with arguments and entry
EAX/EDX/ECX, the arena, the two latch bytes and the callee-counter deltas
(first 4,096 cases). Then 256 installed 31/a7 cases per hook (fast and
preserving dispatch, nonempty x87, EBX = 0); real-callee passes of 1,536
cases for `beam_step` and `turbo_step` (`weapon_step` and `timer_word_step`
nested), `elf_step` (`timer_word_step`, `beam_step`, `weapon_step` nested) and
`laew_trigger` (the real latch), the hook original or replaced and the nested
hooks in rotating modes; the compare probe (512 cases: exact 0 mismatches,
skewed 256) for the 15 compare hooks; the adapter integration (256 cases per
hook with every hook installed, replaced and compare). Branch sites: all 58
listed above both ways, `0x40e704` and `0x40f44c` `PROVED` (fall 1,159 and
1,384, taken 0). `//core:weapon_classes_test`: each body at its edges
(beam_ready's EBX high word nonzero, PLAS doubling, the ELF frame advance,
the LAEW counter boundary, beam_adjust's borrow). One caught mutation per
hook (29, [MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).

### Live counts

Evidence: the DBSIM map probes `dbsim-map-20261010` (a60/c1s/c1c/c3_09/c4_08/
c3_10; 1000 = saturated) and the regression `check-r7-3b` (all hooks
replaced, 40 of 40 suites identical, every skipped/bypassed/mismatch counter
0). Nonzero `hk_<name>_calls` for `weapon_ammo`, `gun_ammo`, `beam_ready`,
`beam_step`, `weapon_priority`, `beam_priority` (≥ 1000 ×6),
`weapon_trigger` (×5, c3_10 468), `beam_spent` (×5, c3_10 287), `gun_ready`
(not c3_10), `gun_spent` (c1s, c3_09, c4_08, c3_10), `elf_step` (not c4_08),
`equip_ready` (c3_09, c4_08, c3_10), `elf_ready` (c1c 263, c3_09 968,
c3_10), `turbo_step` (c3_09, c3_10 728), `elf_trigger` (c3_10 204; 459 in
c3_10-own-building-fire and c3_10-detail-options), `weapon_stock`
17/125/50/≥1000/210/224, `gun_stock` 3/183/65/≥1000/165/88,
`weapon_spent` (c3_09 only), `beam_preset` (c3_09 5), the three gauge
matches (c3_09 only: 30/75/45); `weapon_ammo` up to 309,652 calls
(c5_05-moon-missiles). No probe row: `equip_trigger`, `beam_adjust`,
`beam_fill`. Zero everywhere: `laew_trigger` (LAEW fits no suite),
`beam_drain`, `gun_clear`, `beam_clear` (no weapon dispatch site).
Relations: the slot `+34h` counters of `group_fire` and `weapon_choose`
equal `hk_beam_step_calls` + `hk_turbo_step_calls` + `hk_elf_step_calls` +
the `+34h` entries of `weapon_step`; `hk_elf_step_call_40f00c` ≤
`hk_beam_step_calls`; `hk_beam_step_call_40ef94` and
`hk_turbo_step_call_40ef94` equal their calls minus the ejected weapons'.
Open: the receiver histogram of the bodies shared by several instantiated
classes (an optional probes-only run).

## The weapon constructors

The nine constructors that `weapon_factory` (`0x40fff8`, mission_load.md
"L4 weapon fit") calls are own code in group `weapon_ctors` (core appended to
`core/src/weapon_classes.c`: the constructors build the layout that the
methods above read). Read from the same listing; validated in vitro and live.

| Hook | VA | Class (vtable) | Callers |
|---|---|---|---|
| `weapon_ctor` | `0x40df30` | `WEAPON` base (`0x498e44`) | `beam_ctor`, `gun_ctor`, `equip_ctor` |
| `beam_ctor` | `0x40e074` | BEAM (`0x498dcc`; ELF through `0x4992c0` in the factory) | `weapon_factory` (`0x410177`, `0x4101e9`) |
| `gun_ctor` | `0x40e140` | GUN (`0x498d54`; LAEW through `0x499248`) | `weapon_factory` (`0x410253`, `0x4102c5`) |
| `equip_ctor` | `0x40e234` | `EQUIP` base (`0x498cdc`) | the five below |
| `ecm_ctor` | `0x40e274` | ECM (`0x498c64`) | `weapon_factory` (`0x41032b`) |
| `turbo_ctor` | `0x40e2bc` | TURBO (`0x498bec`) | `weapon_factory` (`0x41042d`) |
| `targ_ctor` | `0x40e308` | TARG (`0x498b74`) | `weapon_factory` (`0x410381`) |
| `shield_ctor` | `0x40e344` | SHIELD (`0x498afc`) | `weapon_factory` (`0x4103d7`) |
| `energy_ctor` | `0x40e380` | ENERGY (`0x498a84`) | `weapon_factory` (`0x41047d`) |

All nine are cdecl, without a frame record, exception descriptor or local;
each starts `push ebp; mov ebp,esp; push ebx` and none shares a tail or is
entered elsewhere, so each is hookable (relocated 5 bytes for `0x40df30`
and `0x40e074`, 7 for the others). Notation as for the methods; "count
+1" is `[[0x6cbee4]] += 1` with the pointer read each time (the runtime's
construction count, mission_load.md). Arguments: W the object
(from the allocator `0x47a2c8`), A the vehicle, M the mount record (26
bytes: words `M+2`, `M+4` the two animation track words, signed byte `M+6`
the hardpoint, words `M+10h..15h` the offset that `mount_offset` adds), C
the definition record ([dat.md](../formats/dat.md)), P the projectile
record. EBX/ESI/EDI are saved and restored; no function reads a caller
register other than the slots, an uninitialized local or stack residue.

**`weapon_ctor` `0x40df30` (W, A, M, C, P)**, EBX = W, EDI = A, ESI = M:

1. EAX = sx byte `M+6`; s32 EAX > 3: EAX = 0. Else (`M+6` read again)
   `0x40fab0(C, sx M+6)` (weapon shape, platform guest; entry EAX = EDX =
   sx `M+6`, ECX = C); its EAX is the shape.
2. `[W]` ← `0x4a69f0`, `[W]` ← `0x4a6a14`, count +1 (EDX the pointer),
   `[W]` ← `0x4a6250`, count +1 (ECX the pointer).
3. `shape_word_block(W+10h, shape)` (hook; entry EAX = W+10h,
   EDX/ECX the two counter pointers).
4. `[W]` ← `0x49a494`, count +1 (EAX the pointer), `[W]` ← `0x498e44`;
   `[W+18h]` ← M, `[W+1Ch]` ← C (EDX = C), `[W+20h]` ← P (ECX = P).
5. s16 `M+2` ≥ 0: `object_track(A, lo(EAX, M+2))` (hook; the
   pushed dword keeps the counter pointer's high word; EDX = C, ECX = P),
   else EAX = 0; `[W+5Bh]` ← EAX.
6. s16 `M+4` ≥ 0 (read after the first call): `object_track(A, lo(EDX,
   M+4))` (EDX: C or the first call's; EAX/ECX of the path), else EAX = 0;
   `[W+5Fh]` ← EAX.
7. S = `[W+10h]` (read again: the block's shape). S ≠ 0: `mount_offset(W)`
   (hook; registers of the path), ECX = W (`POP ECX`), `0x40dd4c(S,
   offset)` (mesh offset, H3 guest; entry EAX = the offset, EDX =
   `mount_offset`'s, ECX = W).
8. memset (`0x49681a`, CW3220 thunk, guest) `(W+24h, 0, 6)` (EAX = W+24h),
   `(W+2Ah, 0, 6)` (EDX = W+2Ah), word `[W+31h]` ← 0, `(W+33h, 0, 8)`,
   `(W+3Bh, 0, 8)` (EAX = W+3Bh), `(W+43h, 0, 18h)` (EAX = W+43h); the
   other entry registers are the previous call's. Byte `W+30h` is never
   written.
9. Byte `[W+4Ch]` ← 1, word `[W+63h]` ← 400h, count +1. EAX = W, EDX =
   the counter pointer, ECX = the last memset's.

**`beam_ctor` `0x40e074` (W, A, M, C, P)**: `weapon_ctor(W, A, M, C, P)`
(entry EAX = A, EDX = C, ECX = M), `[W]` ← `0x498dcc`,
`multiply_q10(334h, 4B0h)` (hook; entry registers `weapon_ctor`'s; 820 ×
1200 >> 10 = 960, the core takes the call's EAX), words `[W+7Bh]` and
`[W+7Dh]` ← its low word, bytes `[W+34h]`, `[W+3Ch]` ← 1, dword `[W+77h]`
← 0, dword `[W+7Fh]` ← 14h, count +1. EAX = W, EDX = 0, ECX = the counter
pointer.

**`gun_ctor` `0x40e140` (W, A, M, C, P)**: `weapon_ctor(W, A, M, C, P)`
(entry EAX = A, EDX = C, ECX = M), `[W]` ← `0x498d54`; D = `[W+1Ch]` (read
again), x = word `D+3Ah` (the capacity); word `[W+7Bh]` ← x, dword
`[W+7Dh]` ← sx(x) << 8, dword `[W+77h]` ← 0, count +1. EAX = W, EDX =
sx(x) << 8, ECX = 0.

**`equip_ctor` `0x40e234` (W, A, M, C)**: `weapon_ctor(W, A, M, C, 0)`
(entry EAX = C, EDX = M, ECX = A), `[W]` ← `0x498cdc`, byte `[W+4Ch]` ← 0,
word `[W+77h]` ← FFFFh, count +1. EAX = W, EDX = the counter pointer, ECX =
`weapon_ctor`'s.

### The five equipment classes

(W, A, M, C): `equip_ctor(W, A, M, C, 0)`
(five slots, entry EAX = C, EDX = M, ECX = A), the vtable, then in this
order:

| Hook | Writes after the vtable | Code word `W+77h` | EDX | ECX |
|---|---|---|---|---|
| `ecm_ctor` | word `W+7Fh` ← 0 twice, dword `W+79h` ← 0 | 12h | 0 | the counter pointer |
| `turbo_ctor` | dword `W+79h` ← 0; after the code dword `W+81h` ← 0, dword `W+7Dh` ← 7D0h | 1Fh | 0 | the counter pointer |
| `targ_ctor` | dword `W+79h` ← 0 | 1Dh | the counter pointer | `equip_ctor`'s |
| `shield_ctor` | dword `W+79h` ← 0 | 1Eh | the counter pointer | `equip_ctor`'s |
| `energy_ctor` | dword `W+79h` ← 0 | 20h | the counter pointer | `equip_ctor`'s |

Each ends with count +1 and EAX = W. The code word is the weapon ID (18,
31, 29, 30, 32); it replaces `equip_ctor`'s FFFFh and is the `[W+77h]` that
the gauge matches of the methods compare.

**Registers at the callers.** EDX:EAX are exact. ECX is dead at every call
site: after each constructor call `weapon_factory` writes ECX, EDX and EAX
before reading them (`0x41017f`, `0x4101f1`, `0x41025b`, `0x4102cd` and
the `[0x6cbee4]` loads and slot reloads of the equipment blocks up to
`0x410494`); after `weapon_ctor`, `gun_ctor` clears ECX and `beam_ctor`
passes it to `multiply_q10`, which does not read ECX (`enter`, `imul
[ebp+0Ch]`, `shrd`); after `equip_ctor`, ECM and TURBO load ECX and
TARG/SHIELD/ENERGY return it to the factory, which writes it first. So the
framework's ECX (the entry ECX) is safe and the adapters return the pair
only. No entry or exit shim.

**Callees.** `shape_word_block`, `object_track`, `mount_offset` and
`multiply_q10` are hooks, and `weapon_ctor`/`equip_ctor` become hooks here;
the bridge reaches them, so they keep their own modes. `0x40fab0`
(platform), `0x40dd4c` (presentation) and the memset thunk `0x49681a` (own
runtime) stay guest calls. Every callee has a verdict
([function_verdicts.md](../reference-game/function_verdicts.md#weapon-constructors)).

**Compare:** refused by all nine (vtables and the construction count, game

**Compare:** refused by all nine (vtables and the construction count, game
calls that allocate and build animation threads, the vertex edit); the load
is accepted by the after-load snapshot.

### Validation of the constructors

`invitro/weapon_ctors_cases.h` (group `weapon_ctors`): per hook 100,000 direct
cases (quick tier; the full tier was not run) against logging
stubs for every direct callee (`0x40fab0`, `shape_word_block`,
`object_track`, `mount_offset`, `0x40dd4c`, memset, `weapon_ctor`,
`multiply_q10`, `equip_ctor`), the original called with six slots over a
stack filled with a case pattern. Compared: the pair, ECX (core), the
ordered calls with slots and entry EAX/EDX/ECX, the arena and the counter
pointer `0x6cbee4`. The stubs change what the originals read again (the
counter pointer, the block's shape `[W+10h]`, the track word `M+4` after
the first `object_track`, `[W+1Ch]` after `weapon_ctor`), and in odd cases
objects overlap (the mount record inside the weapon, the counter inside
the weapon). In even cases (the factory's inputs) the adapter is compared
too, with the callee counter deltas. 256 installed 31/a7 cases per hook
(fast and preserving dispatch). Real-callee passes (4,096 cases per hook;
the stubs log EAX and EDX only, since ECX after a nested replaced hook is
outside the contract): `weapon_ctor` over the
real `shape_word_block` (`0x47a288` a bump allocator, memcpy the
harness's), `object_track` (real `0x49630c`, `new` a bump allocator, its
thread callees stubs), `mount_offset` and `mount_base`; the class
constructors over the real `weapon_ctor` (with `multiply_q10` and
`equip_ctor` where they call them); each nested hook original or replaced
in rotating modes, the hook under test original or replaced (nested
entries in `weapon_ctor`'s pass: `shape_word_block` 4,096, `object_track`
4,140, `mount_offset` 2,276, `mount_base` 2,326). Four branch sites
(`0x40df46`, `0x40dfb3`, `0x40dfcf`, `0x40dfeb`), all both ways. One
caught mutation per hook (MUTATIONS.md; `weapon_ctor` at case 20, `gun_ctor`
at case 5, the others at case 0). Core unit test
`//core:weapon_classes_test` (constructors worked out by hand).

### Live counts of the constructors

Load-time only, from `weapon_factory`'s callee counters. Evidence: regression
`check-h4109` (all hooks replaced, own runtime: 40 of 40 identical, every
`*_skipped`/`*_bypassed` 0) and the DBSIM map `dbsim-map-20261012`, which has
the same nine counts in all 40 suites. The after-load snapshot is identical
(mission_load.md, "The after-load snapshot"). Relations:
`hk_weapon_ctor_calls` = beam + gun + `equip_ctor`, `hk_equip_ctor_calls` =
the sum of the five equipment hooks;
`hk_weapon_ctor_call_402fc0` = `hk_weapon_ctor_calls`,
`hk_weapon_ctor_call_49681a` = 5 × `hk_weapon_ctor_calls`,
`hk_beam_ctor_call_47e0b0` = `hk_beam_ctor_calls`, each class's
`call_40df30`/`call_40e234` = its calls; `hk_weapon_ctor_call_40fab0` =
the map's `0x40fab0` count (only caller `0x40df30`; column "fab0"), and
`call_40f540` = `call_40dd4c` = the same value if the weapon shape is
never null (hypothesis; a weapon on hardpoint 4 or higher has no shape
block and skips both). `call_402374` depends on the mount records. The
counters of `shape_word_block`, `object_track`, `mount_offset` and
`multiply_q10` are unchanged (the bridge reaches them).

| Suites | weapon | beam | gun | equip | ecm | turbo | targ | shield | energy | fab0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| acceptance-60s and the 18 other c1_01 suites (all but c1_01-unarmed-keys) | 31 | 9 | 22 | 0 | 0 | 0 | 0 | 0 | 0 | 15 |
| c3_09-loadout-b, -wingman-orders, -orders-matrix | 98 | 25 | 65 | 8 | 1 | 2 | 2 | 2 | 1 | 58 |
| c3_10-loadout-hercs, -own-building-fire, -detail-options | 79 | 17 | 59 | 3 | 1 | 1 | 0 | 0 | 1 | 48 |
| c3_10-hunt-targ | 79 | 16 | 60 | 3 | 1 | 0 | 1 | 0 | 1 | 48 |
| c4_08-razor-plasma, -slope-dive, -joystick | 55 | 18 | 36 | 1 | 0 | 0 | 0 | 1 | 0 | 31 |
| c5_05-moon-missiles, -orders-flyer | 94 | 31 | 59 | 4 | 1 | 0 | 1 | 1 | 1 | 55 |
| c1_02-probe | 38 | 10 | 26 | 2 | 1 | 1 | 0 | 0 | 0 | 19 |
| c1_08-escort-route, c1_08-scan | 36 | 11 | 23 | 2 | 1 | 1 | 0 | 0 | 0 | 24 |
| c1_01-unarmed-keys | 28 | 4 | 23 | 1 | 1 | 0 | 0 | 0 | 0 | 11 |
| c4_05-convoy | 53 | 14 | 37 | 2 | 1 | 1 | 0 | 0 | 0 | 34 |
| c4_10-probe | 31 | 17 | 12 | 2 | 1 | 1 | 0 | 0 | 0 | 21 |
| c1_06-cheat-disable | 65 | 22 | 41 | 2 | 1 | 1 | 0 | 0 | 0 | 51 |
| c2_02-identify | 38 | 15 | 21 | 2 | 1 | 1 | 0 | 0 | 0 | 25 |
| train1-timers | 3 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3 |

("weapon" = `hk_weapon_ctor_calls`, the others `hk_<class>_ctor_calls`,
"fab0" = `hk_weapon_ctor_call_40fab0`.)

## The weapon gauge syncs, the LAEW emission and the empty bodies

Read from the same listing; every behaviour below is checked in vitro (group
`weapon_syncs`, section *Validation of the syncs*). The roles of the widget
slots are read from the pointer flow only (the widget classes are HUD drawing
code and stay original), so they are hypotheses.

### Scope

The group holds the six gauge syncs of slot `+50h`, the LAEW emission
and the six empty bodies of the matrix (`0x40e95c`, `0x40f89f`, `0x4111ce`,
`0x4111d3`, `0x4111d8`, `0x4111dd`). `0x40e61c` (the getter before the range)
is not part of it; `0x40f77f..0x40f7e5` is type data, `0x40f7e6` is
`debris_dtor`.

The sync bodies are **the only bridge from the HUD gauge widgets to the
weapon's state** (read from the data flow: the player's toggles reach the
simulation through the ECM words, the TURBO latch, the BEAM/ELF level), so an
own process needs them as own code in front of the widget, whatever the
widget becomes. The widget calls stay original (guest calls through the
weapons bridge with the original's EAX/EDX/ECX), so the drawing is not
touched.

### The widget protocol

W is the weapon (cdecl `this`, first argument). The classes keep their gauge
widget in two places (constructor stores and sync bodies): the EQUIP-like
classes (`EQUIP`, `ECM`, `TARG`, `SHIELD`, `TURBO`, `ENERGY`) at the **dword
`W+79h`** (their word `W+77h` is the class code), BEAM, ELF, GUN and LAEW at
the **dword `W+77h`** (also the argument of the gauge-record getters). A
widget starts with its table pointer. Three slots matter:

| Slot | Called by | Arguments | Role (hypothesis from the data flow) |
| --- | --- | --- | --- |
| `+18h` | `0x40f144`, `0x40f184`, `0x40f1f0` | (widget) | returns a pointer to an 8-byte state record; the sync copies both dwords |
| `+14h` | the same three | (widget, 8-byte record) | takes the record back (byte 1 = `W+49h`, the ejected flag) |
| `+10h` | `0x40f288`, `0x40f330` | (widget, 17h-byte record) | takes the 23-byte gauge record that the sync fills in |

The 17h-byte record comes from `0x440e9c(widget)` (BEAM/ELF) or
`0x4414cc(widget)` (GUN/LAEW); the sync copies it (`rep movsd` x5, `movsw`,
`movsb`) into a 24-byte local, changes fields and hands the local to slot
`+10h`. **The 24th byte of the local is never written** (it is not part of
the record); the hooks leave their scratch byte as it is and the in-vitro
cases log 23 bytes only. The address of the local is the only stack address
the widget sees; the cases log the record's bytes instead of the address.

### Sync contracts

`weapon_frame`'s loop (dispatch site `0x410cb6`) calls slot `+50h`
once per weapon and rendered frame with (W, a flag word `[ebp-0Ah]`, a table
word, the result of `0x410970`); it reads no register after the call (the
loop body reloads EAX and EDX at `0x410c00` and keeps ESI/EDI/EBX), so ECX
is the framework's; the cores still
return the original's EAX/EDX/ECX exactly (a game call's registers pass
through). b1, b2, b3 are the low bytes of the arguments at `[ebp+0Ch]`,
`[ebp+10h]`, `[ebp+14h]`.

#### `equip_sync` `0x40f144`

EQUIP, TARG, SHIELD, ENERGY: record =
slot18(`[W+79h]`); the local = both record dwords with byte 1 replaced by
`W+49h`; slot14(`[W+79h]` read again, local). Result registers: the slot
14h call's EAX/EDX; ECX = the local's second dword.

#### `ecm_sync` `0x40f184`

As equip_sync, but first `shown` = sx16(`W+7Dh`)
against the zero-extended record byte 0 (the toggle): on a difference the
cockpit message `0x420a68(2Ah)` (toggle nonzero) or `(2Bh)` (zero); then
both words `W+7Dh` and `W+7Fh` take the toggle byte, byte 1 takes `W+49h`,
slot 14h.

#### `turbo_sync` `0x40f1f0`

Toggle = record byte 0. Toggle set: with the
latch dword `W+81h` set and the charge dword `W+7Dh` <= 0 (signed) both
are cleared; with the latch clear and the charge above 258h (600) the
latch becomes 1 and `sfx_play(2Ch)` (`0x462830`) runs; then a clear latch
clears the local's toggle byte. Toggle clear: latch = 0. The local's
second dword takes `W+7Dh`, byte 1 takes `W+49h`, slot 14h.

#### `beam_sync` `0x40f288`

BEAM and ELF: rec = `0x440e9c([W+77h])`; the
local takes 23 bytes of rec; byte `W+34h` nonzero: local dword 4 =
sx16(`W+7Bh`)·1024 / 1200 (signed, truncating), else word `W+7Bh` = the
local dword 4 · 1200 >> 10 (arithmetic, 32-bit wrap); local dword 0 =
sx16(`W+7Dh`)·1024 / 1200; local bytes 12h = `W+43h`, 11h = `W+49h`, 14h =
b1, 15h = b2, 16h = b3; slot `+10h` of the widget `[W+77h]` with (widget,
local). Result registers: that call's EAX/EDX/ECX.

#### `gun_sync` `0x40f330`

GUN: rec = `0x4414cc([W+77h])`; local = 23
bytes; `target` = sx16(`W+7Bh`) << 8 (the ammunition in 1/256 units); if
`target` is below the dword `W+7Dh` (signed) the shown value ramps down:
`movement_step(250)` (`0x467924`, the EAX word sign-extended) is
subtracted from `W+7Dh`; then a value below `target` is raised to it;
local dword 0 = `W+7Dh` >> 8 (arithmetic); the bytes as in beam_sync;
slot `+10h`.

#### `laew_sync` `0x40f3b8`

LAEW: `gun_sync` with the three argument words;
the pushed dwords keep the high words of the entry EDX and ECX, which
gun_sync's getter call sees (computed in core, no second dispatch).

#### `laew_fire` `0x40ea04`

LAEW slot `+28h`: `weapon_prepare` `0x40e788`
(W, owner, free word) with the entry registers; a zero free word (low word)
decrements the word `W+7Bh`; byte `W+46h` = 0; the emission
`0x40acac(shape id word | high word of W+43h, 0x4a9854, 0x4a9860, owner,
[W+81h])` with EDX = `[W+81h]`, ECX = `[W+20h]`, EAX = the first argument.
LAEW (ID 26) fits no suite and cannot be mounted by the player: the body is
never entered live. It has `weapon_fire`'s frame, so the preparation reads
the same two uninitialised matrix bytes of its caller's stack (weapons.md,
"nested frame residues"): the **entry shim** is `weapons_fire_entry`'s two
bytes (entry ESP - 65 and - 97 into CL and CH), replayed through
`weapons_prepare_stack_call`.

#### Empty bodies

- **weapon_none_28 `0x40e95c`**, **weapon_none_4c `0x40f89f`**,
  **weapon_none_38 `0x4111ce`**, **weapon_none_3c `0x4111d3`**,
  **weapon_none_40 `0x4111d8`**, **weapon_none_44 `0x4111dd`**:
  `push ebp; mov ebp,esp; pop ebp; ret`: no register, no memory
  (`es2_actor_empty_registers`).

### Registers and compare

EDX:EAX exact, ECX exact in core (checked against the original in every
direct case; the hook returns the framework's). The seven bodies with game
calls refuse compare mode (the replacement phase would run the widget slot
and the other callees again on real state); the six empty bodies accept it.
The widget slots have no callee counter (their address is read at run
time); the direct callees have (`hk_<hook>_call_<va>`, keys in
`proxy/weapon_syncs_hooks.h`).

### Validation of the syncs

Per hook: 1,001,024 direct cases (quick tier 100,000) over a 2 KiB arena: the
original body, the adapter and the core against logging stubs for the
widget's slots (the arena table points at them) and the patched callees;
compared are the ordered calls with arguments, entry registers (the local's
address normalised), the received record bytes, the full arena, the pair, the
core's ECX and the callee-counter deltas; 256 installed 31/a7 cases in fast
and preserving dispatch with a nonempty x87 (laew_fire over distinct residue
bytes and a stub that logs the two bytes it finds below its entry ESP); real
callees: `movement_step` nested in rotating original/replaced/compare modes
under gun_sync and laew_sync (1,536 cases each), `weapon_prepare` real with
its own hook in the four original/replaced combinations under laew_fire
(1,024 cases, the states and poison patterns of weapon_fire's own pass); the
empty bodies reuse slot_leaves' direct, installed and compare cases. Every
conditional branch of the seven bodies is taken both ways (`BRANCH` lines:
`ecm_sync` 2, `turbo_sync` 5, `beam_sync` 1, `gun_sync` 2, `laew_fire` 1;
`equip_sync` and `laew_sync` have none). Mutations: 13 caught, see
`MUTATIONS.md`.

In-vitro equality is not live validation: no widget, getter or cockpit
function was executed (they are stubs); the nested `movement_step` is the
only real callee under the syncs.

### Live counts of the syncs

Not yet validated live: the hooks.md registry has no counts for the group.
Expected in a `replaced` regression with every hook replaced (the selection
line of [hooks.md](../reference-game/hooks.md)), nonzero counters from the
DBSIM map `dbsim-map-20261012`:
`hk_beam_sync_calls` in 39 of 40 suites (all but `c1_01-unarmed-keys`),
`hk_gun_sync_calls` in 28 (the C1_01 family without `-unarmed-keys`,
`c3_09-*`, `c3_10-hunt-targ`, `c4_08-*`, `c5_05-*`), `hk_equip_sync_calls` in 7
(`c3_09-loadout-b`, `c3_09-orders-matrix`, `c3_09-wingman-orders`,
`c3_10-hunt-targ`, `c4_08-joystick`, `-razor-plasma`, `-slope-dive`),
`hk_ecm_sync_calls` in 12 (`c1_01-unarmed-keys`, `c1_02-probe`,
`c1_06-cheat-disable`, `c1_08-escort-route`, `c1_08-scan`, `c2_02-identify`,
`c3_10-detail-options`, `-hunt-targ`, `-loadout-hercs`, `-own-building-fire`,
`c4_05-convoy`, `c4_10-probe`), `hk_turbo_sync_calls` in 13 (`c1_02-probe`,
`c1_06-cheat-disable`, `c1_08-*`, `c2_02-identify`, `c3_09-loadout-b`,
`-orders-matrix`, `-wingman-orders`, `c3_10-detail-options`, `-loadout-hercs`,
`-own-building-fire`, `c4_05-convoy`, `c4_10-probe`); the callee counters
`hk_ecm_sync_call_420a68` and `hk_turbo_sync_call_462830` may stay 0 where no
toggle changes. `weapon_none_38` and `weapon_none_3c` are entered 1 and 6
times in the map (`c1_01-tape-record`; `c1_01-keys`, `c3_09-loadout-b`);
`laew_sync`, `laew_fire` and the other four empty bodies have no map row
(expected 0). Every suite must stay identical with every
`*_skipped`/`*_bypassed` 0. The widget bytes the syncs hand to the gauge are
not in the tick log; the player-visible effect (ECM and TURBO toggles, the
gun counter ramp) is checked by a play test, not by the oracle.
