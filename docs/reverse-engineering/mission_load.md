# Mission load `0x461608`: from the launch files to the first frame

Reference for the mission load of the reference `VER95/DBSIM.EXE` (SHA-256
`8561aa47…c4ad`, [reference_version.md](../reference_version.md)), image base
`0x400000`. Facts are read from the pinned `llvm-objdump -d -M intel`
listing. Call trees come from a control-flow walk of each function and a
recursive walk of its direct `call rel32` targets, with fresh aligned
decoding where the linear listing is misaligned; indirect and virtual calls
are **not** followed unless named. **Confirmed** means read from the
instructions; role names are **hypotheses** unless a linked document
confirms them.

- Sections 1 to 12 describe the original. Their live statements come from
  the entry-probe counts of the [DBSIM map](../reference-game/dbsim_map.md)
  (`dbsim-map-20261010/map.json`, suites a60/c1s/c1c/c3_09/c4_08/c3_10),
  taken with the late arming of section 8.
- Sections 13 to 32 hold the pieces of the load as own code: per piece the
  hooks, the hook group and core file, the contract from the bytes, the
  in-vitro validation, the live counts with early arming (section 13, the
  default), the after-load snapshot reach and the live result. The live
  counts there come from the entry-count map `dbsim-map-20261012` (every
  hook original, early arming).
- Sections 33 and 34 describe the load in the own process (`es2sim`).

Record layouts used here: [mission.md](../formats/mission.md) (script
framing, MEC, `mission.var`, `MISSION.STR`, triggers, timers, orders,
groups, objectives), [mission_runtime.md](mission_runtime.md) (runtime
records), [vtables.md](vtables.md) §1.2 (who builds which class),
[dts.md](../formats/dts.md#resource-map) (shape resources),
[file_io.md](file_io.md#mission-load-0x461608) (file call sites),
[rng.md](rng.md) (streams before the first frame) and
[oracle_suites.md](../reference-game/oracle_suites.md#static-team-mapping)
(MEC append and group zero).

## Result in short

- **Once per process.** `0x461608` has one caller, `0x45f42b` in the game
  main `0x45f160`, between stage 1 and stage 3 and before the main loop
  ([rng.md](rng.md#stream-initialization-at-runtime)). It has no
  arguments and returns nothing (the `[ebp-0x20]` words and the `[0x6cbee4]`
  adjustments are the runtime's exception bookkeeping, as in
  [main_loop.md](main_loop.md#the-frame-itself)). 652 instructions; 649 are
  reachable (`0x461dca..0x461dd1`, a call `0x462830(0)`, is dead code behind
  the jump at `0x461dc8`); 82 direct call sites to 63 functions plus one
  indirect call (`[0x4a5824]` at `0x461ecd`). The direct-call closure has
  963 functions, 92 of them registered hooks.
- **Five phases** (section 1): (A) process and presentation set-up plus a
  peek at the launch files; (B) the 19 stage-2 callbacks: type data and most
  effect pools; (C) the two script passes, **world load** `0x424308` (tables,
  actor selection, pools, terrain) and **actor load** `0x4253d8`
  (construction of mechs, flyers, buildings, orders, groups, objectives);
  (D) fix-ups in `0x461608` itself: drain of the effect objects the load
  created, ground heights, player pick, cameras, cockpit, HUD; (E) the first
  MCI call, the first render, presentation and the start sequence.
- **Arming (sections 8 and 13).** With early arming (the default) the
  proxy installs every hook, probe, coverage site and RNG counter at the
  load's call site `0x45f42b`, before phase A, so the load runs hooked and
  its calls are counted. With `ES2_EARLY_ARMING=0` it arms at the first MCI
  call, `0x461dc3` (the CD-music start `0x46313c`), inside the load: phases
  A to D then run as original code, the 79 registered hooks reachable from
  them (`pool_alloc`, `terrain_height`, `script_start`, `object_init`,
  `random_damage`, `euler_matrix`, …) are not installed yet, and every
  load-only function reads 0.
- **RNG (section 6):** stream A is drawn in the terrain loader (pass 1) and
  in the initial damage of authored mechs (pass 2), including the debris,
  fire and sound draws that the damage virtual `+0x74` (`vehicle_part`) can
  reach; stream B is drawn by those sound paths and, after the late arming point, by the
  first render and the start sequence. VSHELL's own randomness enters only
  as `mission.var` words 4..6.
- **"Static-object cleanup" corrected (section 11):** `0x497f9a` is the
  exception-frame descriptor of the smoke module's `smoke_update`
  `0x4092dc`, followed by that module's static-destructor records; it is not
  used by the load. The load-time cleanup is the pair of drain loops
- **Compare:** in-place compare (the snapshot regions of the compare mode, compare_mode.md) cannot cover the
  load as a whole, because it allocates from the heap and reads files; the
  acceptance for the replaced load is an after-load snapshot compared
  between an original and a replaced run with pointers canonicalized
  (sections 9 and 13).
  original and a replaced run with pointers canonicalized (section 9).

## Legend

Class column: **hook** registered hook (section 8: original during the load
under late arming); **load** simulation code of the mission load (own code
in sections 14 to 32); **file** file I/O (the stream library); **resource**
shape, bitmap and palette loading; **H3** rendering and presentation;
**audio** the SOS/MCI boundary; **input** the input layer; **platform**
Win32 shell, message pump, timing; **RTL** runtime (`0x49630c` exception
frame, `new`/`new[]`, array construction), H6.

## 1. Call tree of `0x461608` in execution order

`G` = the globals object `0x4d24bc` (EDI throughout); `G+n` is the global
`0x4d24bc+n`. Sub-steps of the big children follow in sections 2 to 4.
Phases of the summary by site: A `0x461621..0x4617b1`, B `0x4617b8`,
C `0x4617be..0x46182b` (with the terrain binning loop and the mouse queue
between the passes), D `0x461830..0x461d90`, E `0x461d92..0x461f61`.

| Function | Site | Effect (what it reads and writes) | Class |
|---|---|---|---|
| `0x49630c` | `0x461621` | exception frame (descriptor `0x49f7c9`) | RTL |
| `0x479cfc` | `0x46162b` | registers `0x461304` (close tape and dmp streams, [file_io.md](file_io.md#mission-end-and-exit)) as an abort-notify callback: `new(8)`, linked into the list `0x6c56d8` | platform |
| `0x45a5dc` | `0x461631` | input set-up: device objects, flight-key capture on (`0x45b68c(1)`), prefs read `0x45972c` (`prefs.cfg`, voice VOL existence), device poll, the device block `0x4d2310..0x4d238c`, bytes `0x4d22f6..9`, then `keyjoy.cfg` (`0x45b7a8`) ([cfg.md](../formats/cfg.md)) | input, file |
| `0x4955c4` | `0x46163f` | only when byte `0x4d267d` (`-m`) is set: `0x49571c(0x6cbc50, 0x20)`; not examined | RTL region |
| `GetWindowRect`, `GetClientRect` | `0x461652`, `0x461682` | window border sizes into locals | platform |
| `0x461294` | `0x46169f` | `new(0x239)`, graphics-context constructor `0x480c34`, stored at `G+0x12` (`0x4d24ce`), selected (`0x480d44`) | H3 |
| `0x464c78` | `0x4616a4` (and `0x461728`, `0x461835`, `0x461a6a`, `0x461a74`, `0x461e03`, `0x461e0d`, `0x461f44`) | message pump | platform |
| `0x46548c` | `0x4616a9`, `0x461a6f` | clear and show the frame buffer ([flyer.md](flyer.md#function-table)) | H3 |
| `0x4614c0` | `0x4616b6` | launch peek: `script.dat` header word 0 (world) and `player.mec` words 0..4; returns the world word and "MEC record 0 has vehicle ID 8" | file |
| `_strcat`, `0x492bec`, `0x465f7c`, `0x481fe0` | `0x4616eb..0x461722` | loading-screen palette `ld_herc<w+1>.dpl` or `ld_flyr<w+1>.dpl` (template `ld_` + `hercX`/`flyrX`, character 7 replaced by `'1'+world`) loaded and selected | resource, H3 |
| `0x461450` | `0x461734` | loading-screen bitmap of the same name through `0x45cdf4` (`.hba` when byte `0x4d2537` = 3, else `.dba`), blitted and presented | H3 |
| `new(6)`, `0x480af0` | `0x46173c..0x461776` | graphics-library object `(obj, 0x26d3)` stored at `G+0x4e` (`0x4d250a`) | H3 |
| `0x462410` | `0x46179f` | sound init `(byte G+0xc, five unused words)`: window hook, voice channels, `sound.cfg`, driver, sample preload; sets the sound context `[0x49f8ec]` ([audio.md](audio.md)) | audio |
| `0x463370` | `0x4617a7` | empty (`ret`) | — |
| `0x48dd08`, `0x4913dc` | `0x4617ac`, `0x4617b1` | graphics-library state (`memset 0x6c60d4`, `0x5b30` bytes) and two registrations through `0x47a0b8` (hypothesis: persistent class tables) | H3, resource |
| `0x401d94(2)` | `0x4617b8` | the 19 stage-2 callbacks (section 4) | load, resource, file |
| `0x46134c` | `0x4617be` | scene light set-up: two light objects (`0x48bc40`), `euler_matrix`, `0x4800c0`, `0x48be50`, `0x407800` | H3 |
| `0x467840` | `0x4617c3` | limiter reference and clock words `0x4d3b64..0x4d3b78` ([timing.md](timing.md), [mission.md](../formats/mission.md#timers-and-update-order)) | platform |
| `0x424308` | `0x4617c8` | **world load**, script pass 1 (section 2) | load, file |
| — | `0x4617cd..0x4617de` | dword `G+0xbb` (`0x4d2577`) ← 25 if byte `G+0xb5` (`0x4d2571`, set by the world load from the MEC vehicle-8 test) else 13 | load |
| `0x4282d8` | `0x4617fa` (loop) | for every entry of the terrain object table `[0x4aaa78]` (count word `0x4aaa7c`, stored by the terrain loader `0x42789c` at `0x427902`): virtual `+0x10`, then the binning step `0x42837c` ([terrain.md](terrain.md)) | H3 (binning) |
| `0x452ae4` | `0x46181f` | mouse event queue (two lists of 100 × 14 bytes) and the `0x452cdc` callback ([input.md](input.md)) | input |
| — | `0x461824` | byte `0x4d1dd6` ← 0 | input |
| `0x4253d8` | `0x46182b` | **actor load**, script pass 2 (section 3) | load, file |
| `0x44f6f4` | `0x461830` | bitmap and graphics-context set-up (`BMP_8`, `VGA_8`, Gouraud fills; [player_movement.md](player_movement.md)) | H3 |
| `removal_append`, `0x407b3c`, `0x4077e8`, `0x409904`, `pool_prev` | `0x46183a..0x461872` | **drain 1:** while the explosion pool `[0x4a961e]` has an object: append it to `[0x4a9622]`, flush explosions, lights and flat objects (section 5) | load (hooks) |
| `removal_append`, `0x4083e0`, `pool_prev` | `0x461874..0x4618a2` | **drain 2:** the same for the debris pool `[0x4a9642]` / list `[0x4a9646]` | load (hooks) |
| — | `0x4618a4..0x4618b4` | word `0x4cf880` ← word `0x49abba` (0 in the image); player `G+0x2a` (`0x4d24e6`) ← 0 | load |
| `terrain_height`, `pool_prev` | `0x4618bc..0x461967` | **height fix and player pick** over the MECH pool `[0x4a9b7a]`: z ← ground height at (x, y) + signed type word `[[A+0x1f2]+0x16]`, + 5000 when type word `+0x50` ≠ 0; player ← the last MECH with byte `A+0xa3` set | load |
| — | `0x46196d..0x461982` | `0x4d2684` ← player; the terrain object `[0x4a0be0]`'s callback `+0x125` ← `0x428c60` | load |
| `new(0x4e)`, `0x48bd84` | `0x4619a4..0x461a5e` | one camera-like object (vtable `0x49f8e8`, fields `+0x32`, `+0x36`, `+0x44..+0x4a` cleared) per member of the player's group (count word `[[player+0x45]+0x10]`), array `0x4d2688`; `G+0x60` (`0x4d251c`) ← player | H3 |
| `0x492bec`, `0x45cdf4`, `0x465618` | `0x461a79..0x461abd` | only when dword `G+0x82` < 640: cursor sprite `lo_curs.dba` | H3 |
| — | `0x461abe..0x461b03` | a value from bytes `G+0x7b`, `G+0xb`, word `G+0xaa` into a local that nothing reads; the `cmp` at `0x461b03` has no consumer (dead computation) | — |
| `0x4544c0` | `0x461b0b` | HUD fonts and bitmaps (`hfn` names, `0x453630`, `0x453684`), surface properties into `0x4d1e68..0x4d1e84`, `[0x49dd88]` | H3 |
| — | `0x461b10`, `0x461b1a` | modal enter/leave callbacks `[0x49dd94]` ← `0x461250`, `[0x49dd98]` ← `0x461274` (push and pop the flight-key capture state, `0x45b68c`) | input |
| `new(0x4e)`, `0x48bda4` | `0x461b24..0x461c5f` | the main camera `(obj, [G+0x12], 9 if dword 0x4d3040 > 1200 else 8, 0x100, &(0, −6000, 0), &(0, 0, 0))`, vtable `0x49f8e8`, stored at `G+0x2e` (`0x4d24ea`) | H3 |
| `0x45edb0` | `0x461c68` | view object of the player: camera virtual `+0x30`, camera setters `0x40111c`/`0x40109c`; then word `cam+0x36` ← 2 when `cam+0x4a` = 0 | H3 |
| — | `0x461c7d` | word `G+0x6e` (`0x4d252a`) ← 1 (cleared again at `0x461dfd`; hypothesis: "loading" flag) | — |
| `new(0x37)`, `0x429660` | `0x461c83..0x461cb6` | cockpit object D (`cockpit.dpl`, layout records), stored at `G+4` (`0x4d24c0`) ([display_modes.md](display_modes.md#objects)) | H3 |
| `0x431530` | `0x461cc0` | the player's group into `0x49b0f8`, up to three other members into `0x4d03c8`, their pilot words `+0x29c` to `0x44a7e8` (hypothesis: HUD wingman panels) | H3 (HUD) |
| `0x437c8c` | `0x461cd1` (loop over `[0x4a9b7a]`) | per MECH: an object built from the mount table `0x420634` (array of `0x22`-byte entries, constructor `0x438798`), stored at `A+0x1fe` (hypothesis: cockpit damage display, [collision.md](../formats/collision.md)) | H3 (HUD) |
| `0x41b130` | `0x461d02` | cockpit↔player synchronisation ([cockpit.md](cockpit.md)) | H4 (cockpit sync) |
| `camera_update` | `0x461d0c` | first camera step on `[G+0x2e]` ([camera.md](camera.md)) | hook |
| `0x42a1f0` | `0x461d21`, `0x461d2f` | cockpit layout switch `(D, 1)` unless byte `G+0x7c` = 2, then `(D, 0)` ([display_modes.md](display_modes.md)) | H3 |
| `pool_prev`, `dist_gate` | `0x461d34..0x461d90` | search of the building pool `[0x4a95a0]` for the first object whose group category byte `[[o+0x45]+0x12]` is 0 and that lies within 40000 (`dist_gate`) of the player; the result and the word read from `0x4a9e54` are never used (dead computation, no stores) | — |
| `0x46313c` | `0x461dc3` | only when the sound context `[0x49f8ec]` ≠ 0 and word `0x4aa728` (script header word 4) = 0: CD track `[0x49f8fc]` ← `(dword 0x4d2573 mod 5) + 2` (`0x4d2573` is the launcher's `-R` counter, [launcher.md](launcher.md)), loop byte `[0x49f900]` ← 1, then MCI play. **First MCI call: the late arming point** (section 8) | audio |
| `0x42eb40`, `0x43048c`, `0x45fca8` | `0x461dd9..0x461df1` | when word `G+0x54` (`0x4d2510`) = 0: word `0x49aef6` ← 8, the empty `0x42eb40(cam)`, palette flush, **first render** (render root, [main_loop.md](main_loop.md)) | H3, H4 (render root) |
| `0x45d548` | `0x461df8` | otherwise: prepare the intro ([display_modes.md](display_modes.md)) | H3 |
| `0x43048c` | `0x461e08` | palette flush | H3 |
| `0x464a14` / `0x43122c` | `0x461e1c` / `0x461eb4` | word `G+0xaa` ≠ 0: present the frame; else draw text in the rectangle (0..320, 0..400) | H3 |
| `0x4328cc` | `0x461ebc` | arm the cockpit start sequence (sound `0x462830`) ([hud.md](hud.md)) | H3, audio |
| — , `0x433f7c`, `[0x4a5824]` | `0x461ec1..0x461ecd` | byte `0x49b105` ← 1; HUD step; graphics-library slot call | H3 |
| `0x45d490`, `0x45d50c` | `0x461ee8`, `0x461f13` | detail levels from the prefs bytes `0x4d1f41` (→ `G+0x16`, `G+0x18`) and `0x4d1f42` (→ `G+0xb1`, `G+0xb3`); word `G+0x3a` ← byte `0x4d1f40` | H3 |
| `0x45d85c` | `0x461f2c` | when word `G+0x54` ≠ 0: the drop sequence ([display_modes.md](display_modes.md#0x45d85c-mission-start-drop-sequence)) | H3 |
| — | `0x461f31..0x461f38` | byte `0x4d1dd6` ← 1 unless tape playback (word `G+0x1a`) | input |
| `0x45b758` | `0x461f3f` | display set-up (surface properties) | H3 |
| `0x477abc` | `0x461f5b` | flush the key ring of the device `[0x6c5eb8]` ([input.md](input.md)) | input |
| — | `0x461f61` | word `G+0x76` (`0x4d2532`, the main loop's leave request) ← 0 | platform |

**Function roles** (one row per function, for the function index):

| Function | Role | Class |
|---|---|---|
| `0x461608` | mission load: launch files to the first frame (this document) | load |
| `0x424308` | world load, script pass 1: launch files, mission tables, actor selection, actor pools, terrain (section 2) | load |
| `0x4253d8` | actor load, script pass 2: mechs, flyers, buildings, orders, groups, objectives (section 3) | load |
| `0x4614c0` | launch peek: script header word 0 and "MEC record 0 is vehicle 8" | file |
| `0x4178e8` | initial damage of an authored mech from its record word `+0x84` (stream A, MECH V74) | load |
| `0x4175dc` | weapon fitting of a mech: player weapon system `0x4104ec` or weapon factory `0x40fff8`, stored at `A+0x202` | load |
| `0x4104ec` | player weapon system constructor (fire groups) | load |
| `0x40fff8` | weapon factory: the weapon set of a non-player actor | load |
| `0x425348` | resolve a mission subject (scope 0 group, 1/2/3 MECH/FLYER/building pool through the remap tables) | load |
| `0x411b90` | copy an actor's ten variable IDs and ten action codes to `A+0x1ba`/`A+0x1ce` | load |
| `0x415068` | part condition table from the MEC condition words | load |
| `0x471ad8` | pool construction `(header, capacity, size)`; 12 call sites, all in the load | load |
| `0x4282d8` | bin one terrain object: virtual `+0x10`, then `0x42837c` | H3 |
| `0x461294` | create and select the graphics context `[0x4d24ce]` | H3 |
| `0x479cfc` | register an abort-notify callback (list `0x6c56d8`) | platform |
| `0x46313c` | start the CD music track through MCI (the late arming point of the proxy) | audio |
| `0x431530` | the player's group and up to three other members for the HUD (`0x49b0f8`, `0x4d03c8`) | H3 |
| `0x437c8c` | per-MECH display object at `A+0x1fe` | H3 |

**Sound init arguments.** `0x461608` pushes six words; only the first
(byte `G+0xc`, the sound switch) is written, the other five come from
locals `[ebp-0x50..-0x60]` that the function never stores. `0x462410`
reads only `[ebp+8]` (confirmed: no other `[ebp+…]` operand in its body),
so the stale words are harmless.

## 2. Pass 1: world load `0x424308`

Reads, in this order (all through the read stream `0x47ad40`/`0x47af98`
and its virtual read `+0x18`; fatal error `0x479c78` with `WORLD.CPP` on a
failed open):

1. `memset` of the selection tables (`0x42432a..0x4243d3`): mech flags
   `0x4aa72a` and remap `0x4aa78e` (100 bytes each), the local mech type
   array (100 words), MEC flags `0x4aa7f2` (100), flyer flags `0x4aa856`
   and remap `0x4aa888` (50 each), the local flyer type array (50 words),
   building flags `0x4aa8ba`, remap `0x4aa946` and the building byte table
   `0x4aa9d2` (140 each).
2. `data\mission.var`, 2000 bytes into `0x4a9e70`, then the word clears of
   [mission.md](../formats/mission.md#missionvar) (variables 10, 20 and
   21..42).
3. `data\player.mec`: `0x4aaa64` (file word 0), N at `0x4aaa5e`, `new[]` of
   N × 126 bytes at `0x4aaa60`, the records
   ([oracle_suites.md](../reference-game/oracle_suites.md#launch-boundary-and-storage));
   byte `0x4d2571` ← (record 0 vehicle ID = 8).
4. `data\mission.str`: body at `0x4aa650`, up to 50 string pointers at
   `0x4aa660`, final LF stripped.
5. `data\script.dat` (name from `0x492b90`): the 20-byte header to
   `0x4a9e4e`, word `0x4a9e52` ← 0, then **`0x42e010(0x4a9e4e)`, the world**
   (`.wld`, `.dpl`, `cyc`, `.rmp`, the flat-object pool `0x4097a8`;
   [mission.md](../formats/mission.md#world-resource-selection)), then the
   lists of [mission.md](../formats/mission.md#prepared-script-exact-framing):
   coordinates (`new[]` of 12-byte points at `[0x4a9e0c]`, count
   `0x4a9e06`, map rectangle `0x4aa640..0x4aa64c`), angles (`[0x4a9e10]`,
   × 182), routes (`[0x4a9e18]`), regions (`[0x4a9e20]`, `0x423358`),
   triggers (`[0x4a9e28]`, count `0x4a9e24`), timers (`[0x4a9e38]`, count
   `0x4a9e34`, constructor `0x423104` → `timer_arm`).
6. **Selection scan.** Mech records (134 bytes): only the type word `+0x28`
   is kept (local array); flyers (92): type `+0x2c`; buildings (52): type
   `+0`; orders (14): skipped. Groups (156): `0x423d38(0x4a9e00, count)`
   allocates the group array; record 0 is kept aside, every other record
   marks its non-negative actor references (`+50..+88`) selected in the flag
   table of its class (`+40` = 0/1/2); for class 2 it also stores a byte in
   `0x4aa9d2`, and that byte is always 1 (the reader overwrites the record's
   word `+110` with 1 at `0x424e3a` before using it).
   Objectives (54): skipped. String indices: `new[]` at `[0x4a9e48]`, count
   `0x4a9e44`.
7. **Compaction** (`0x424f5e..0x425086`): consecutive remap indices for the
   selected mechs (`0x4aa78e`); each non-negative group-0 reference in slot
   k gets the remap index (selected mech count + k), which is the compact
   index the k-th MEC actor receives next; then flyers (`0x4aa888`) and
   buildings (`0x4aa946`); then each MEC record i is appended as mech M+i
   (flags `0x4aa7f2` and `0x4aa72a`, type = MEC vehicle `+4`) and N is added
   to the mech count
   ([oracle_suites.md](../reference-game/oracle_suites.md#static-team-mapping)).
8. **Type loads per selected actor:** `0x41ff10(type)` for each mech,
   `0x422e90(type)` for each flyer, `0x405ebc(type, byte)` for each
   building (`bases_an.dts`, `bases.dgs`).
9. **Pools:** MECH pool `[0x4a9b7a]` = `0x471ad8(new(0x12), mechs, 0x36a)`
   with removal list `[0x4a9b7e]` (25 slots); FLYER pool `[0x4a9db9]` =
   `0x471ad8(…, flyers, 0x291)`, list `[0x4a9dbd]`; building pool
   `[0x4a95a0]` by `0x405df4(buildings)`; actor list `0x4a9af8` by
   `0x411df8(…, mechs+flyers+buildings)`; then `0x40fecc`, `0x40d218`,
   `0x40cbec` (per-type tables), `0x42049c` (mech name table and shapes,
   [dts.md](../formats/dts.md#resource-map)), `0x422fb4` (flyer tables).
10. **Terrain:** `0x42789c(word 0x4a9e50, word 0x4a9e4e)` (zone `.dat`,
    `mat0.dat`, the `.dba` grid; the object table `0x4aaa78`; RNG A).
11. Result pairs: word `0x4a9e6a` ← 0, `[0x4a9e6c]` ← `new[](200)`; word
    `0x4aa728` ← header word 4 (`0x4a9e56`); close.

## 3. Pass 2: actor load `0x4253d8`

1. Function-local statics on first use: default position `0x49a871..79` =
   (0, 0, −32768) and default angle words `0x49a87e..82` = (0x8000,
   0x8000, 0). Status word `0x4a9e4c` ← −1. Reopens `data\script.dat`.
2. Skips header, coordinates, angles, routes, regions, triggers (74 bytes
   each) and timers (24 each) by reading them into locals.
3. `0x40f998`: weapon shape cache (`mechwpns.dts`); freed again by
   `0x40fa88` after the mech loop.
4. **Mechs**, i = 0 .. M+N−1 (M = authored mech count): for i < M the
   134-byte record is read (always, selected or not); skipped unless flag
   `0x4aa72a[i]`. For i ≥ M the type comes from MEC record `+4`.
   `pool_alloc([0x4a9b7a])`, MECH constructor `0x415bb0(A, type)`.
   - MEC actors: when the MEC ordinal equals `0x4aaa64`, `0x4a9b84` ← A and
     byte `A+0xa3` ← 1 (the player flag the height-fix loop reads); position
     and angles from the defaults.
   - Authored actors: position from coordinate `+0x3e` with
     `terrain_height` (−32768 is bumped to −32767) plus the type offsets of
     section 1; angle words (0, 0, angle `+0x40`), or the defaults.
   - Then `A+0x26` position, `A+0xc` angles, word `A+0x32` ← 0, and
     **`0x4175dc(A, items, secondary)` at `0x4258a2`**: items/secondary are
     MEC `+8`/`+0x20` or record `+0x2a`/`+0x6a`. `0x4175dc` takes the mount
     table from `0x420634(A)`; for the player (byte `A+0xa3`) it builds
     `new(0x35)` → `0x4104ec` (player weapon system, `fire_group`), else
     `new(0x14)` → the weapon factory `0x40fff8` (`weapon_record`,
     `def_lookup`, `0x40ffb0`), stores the result at `A+0x202`, then
     `actor_animate`, `mount_offset`, the weapon-data helpers `0x40cec0`,
     `0x40d240`, `0x40d128`, `0x40cb84`, `0x40d284`, `0x40fb2c`,
     `shield_capacity`, `0x413ac8`, `0x417d08`.
   - MEC actors: `0x415068(A+0x206, tail+0x3c, +0x56, +0x6a)` turns the
     13/10/10 condition words into the part condition table (the first
     block as `(100 − w) × table value / 100`; the other two blocks were
     not traced here), word `A+0x29c` ← MEC word 0; MEC ordinal +1.
   - Authored actors: byte `A+0x97` ← 1 when record word 0 ≠ 0; trigger
     pointers `A+0x1b2`/`A+0x1b6` from record `+0x80`/`+0x82`;
     `0x411b90(A, +0x42, +0x56)` copies ten variable IDs to `A+0x1ba` and
     ten action codes to `A+0x1ce`; **`0x4178e8(A, word +0x84)`, the initial
     damage** (section 6); word `A+0x252` ← record word `+2` when > −1.
5. **Flyers** (92 bytes): `pool_alloc([0x4a9db9])`, constructor `0x4215f4`,
   `0x411b90`.
6. **Buildings** (52 bytes): factory `0x405314` (allocates from
   `[0x4a95a0]`, [vtables.md](vtables.md#12-who-builds-which-class)),
   virtual `+0x10`, terrain marks `0x470ed4`, `terrain_height`, `0x411b90`.
7. **Orders:** count `0x4a9e2c`, `new[]` at `[0x4a9e30]`, 22-byte runtime
   orders, subjects through `0x425348`
   ([mission.md](../formats/mission.md#order-and-actor-group-wire-mapping)).
8. **Groups** (156 bytes): group 0 takes the roster of the appended MEC
   actors; `heading_point`, constructor `0x423b34`, `0x405db0`.
9. **Objectives** (54 bytes): count `0x4a9e3c`, `new[]` at `[0x4a9e40]`
   (stride `0x4c`), subjects through `0x425348`, two 20-byte `memcpy`,
   string pointers defaulting to `0x49a870`.
10. `0x47129c([0x4a0be0])` (terrain cell marks, [terrain.md](terrain.md));
    every building's z ← `terrain_height` (no offset); trigger subjects of
    types 7..10 resolved through `0x425348`; close.

`0x425348(index, scope)`: scope 0 → group `[0x4a9e00] + 122 × index`;
1/2/3 → `0x471cb0` on the MECH, FLYER or building pool with the remap byte
`0x4aa78e`/`0x4aa888`/`0x4aa946[index]`; other scopes → 0.

## 4. Stage-2 callbacks (`0x401d94(2)` at `0x4617b8`)

Registered by `0x401d64(fn, 2)` in 19 of the 65 static init records, all of
priority `0x20`, so they run in table order (init table `0x4a79d8`,
[rng.md](rng.md#stream-initialization-at-runtime)). Pool arguments
are `0x471ad8(header, capacity, object size)`; "list" is the removal list
the stage-5 flush drains ([main_loop.md](main_loop.md#the-main-loop-around-it)).

| Function | Record | Role | Pools and globals | Resources |
|---|---:|---|---|---|
| `0x405fac` | 3 | building type data | `0x4a95a8..0x4a95c0`, `0x4a9572..0x4a9594` | `bforms.dat`, `lc_wpns.dat`, `bases.dat`, `basecol.dat`, `bhulks.dgs`, `basetex.dba`, `vehtex.dba`, debris `base_deb` (`0x40874c`) |
| `0x4076e4` | 4 | light sources | pool `[0x4a95fe]` (3 × `0xe`), list `[0x4a9602]`, table `[0x4a9608]` (`0x406ee4(10, 2000)`) | — |
| `0x407b54` | 5 | explosions | pool `[0x4a961e]` (40 × `0x5b`), list `[0x4a9622]`, `0x4a9628..0x4a963c` | `explos.dts`/`explos2.dts`, explosion `.dat` and `.dba` names from a table |
| `0x408860` | 6 | debris | pool `[0x4a9642]` (150 × `0x61`), list `[0x4a9646]`, `0x4a964c`, words `0x4a9664..8` = 6000, 16000, 420 | `def_deb` |
| `0x4090cc` | 7 | smoke | pool `[0x4a9684]` (20 × `0x2a`), list `[0x4a9688]`; `SMOKE_BALL` pool `[0x4a966e]` through `0x408fb8` | — |
| `0x409a34` | 9 | meteors | pool `[0x4a96aa]` (10 × `0x59`), list `[0x4a96ae]`, `[0x4a96b8]` | `meteor.dts`, `impact.dba` |
| `0x40a060` | 10 | projectiles | pool `[0x4a96c2]` (160 × `0x66`), list `[0x4a96c6]` | — |
| `0x40a818` | 11 | rockets | `[0x4a96d0]` | `rockets.dat`, `rockets.dts` |
| `0x40ac38` | 12 | empty (`ret`) | — | — |
| `0x40ade0` | 13 | bullets | `0x4a96f8`, `0x4a9706`, `0x4a970a`, byte `0x498514` | `bullets.dat`, `.dts`, `.dba` |
| `0x40b6e0` | 14 | beams | `0x4a9804`, `0x4a9808` | `beam.dat`, `beamtex.dba` |
| `0x40fc8c` | 16 | weapon types | `0x4a98ec`, `0x4a98f8`, `0x4a98fc` | `wpntex.dba`, `mechwpn2.dts`, `weapons.dat`, `proj.dat` |
| `0x415464` | 19 | constants | words `0x4a9b82` ← 1, `0x4a9ccc`, `0x4a9cce` | — |
| `0x41fdb0` | 21 | mech types | `0x4a9d06` ← 0, `0x4a9d6c`, `memset 0x4a9d08` | `mforms.dat`, a `.dba` |
| `0x422d90` | 23 | flyer types | `0x4a9dca` ← 0, `0x4a9dfc`, `memset 0x4a9dcc` | `fforms.dat` |
| `0x428230` | 27 | tables at `0x4cf878` (hypothesis: terrain binning) | `0x4cf878`, `0x4cf87c`, `0x4cf884`, `0x4cf888`, `memset 0x4aab00` | — |
| `0x43034c` | 29 | one pointer | `0x49b034` | — |
| `0x467b64` | 51 | colours | — | `colors.dat` |
| `0x46b1a8` | 55 | fires | pool `[0x6b4f30]` (10 × `0x5b`), list `[0x6b4f34]`, `0x6b4f2c`, `0x6b4f38` | fire `.dts`, `.dat`, `.dba` (`fire`/`fire2`) |

The flat-object pool `[0x4a968d]` (list `[0x4a9691]`) is created by
`0x4097a8` inside the world loader `0x42e010`; the MECH, FLYER and building
pools by the world load (section 2). No stage-2 callback reaches an RNG
draw (direct-call closure; up to 70 indirect sites per callback not
followed).

## 5. State after the load

| Area | Where | Built by |
|---|---|---|
| Mission header, mode, status | `0x4a9e4e..0x4a9e61` (ten words; `0x4a9e52` cleared), status `0x4a9e4c` = −1, `0x4aa728` | pass 1, pass 2 |
| Mission variables | `0x4a9e70`, 1000 words | pass 1 |
| MEC | `0x4aaa5e`, `[0x4aaa60]` (N × 126), `0x4aaa64`, `0x4d2571` | pass 1 |
| Strings | `0x4aa650`, `0x4aa660`, `[0x4a9e48]` | pass 1 |
| Coordinates, angles, routes, regions | `[0x4a9e0c]`, `[0x4a9e10]`, `[0x4a9e18]`, `[0x4a9e20]`; rectangle `0x4aa640` | pass 1 |
| Triggers, timers | `[0x4a9e28]`/`0x4a9e24`, `[0x4a9e38]`/`0x4a9e34` | pass 1 (subjects resolved in pass 2) |
| Selection and remap | `0x4aa72a..0x4aaa5d` | pass 1 |
| Actor pools | `[0x4a9b7a]` MECH, `[0x4a9db9]` FLYER, `[0x4a95a0]` buildings, their lists, actor list `0x4a9af8` | pass 1 (pools), pass 2 (objects) |
| Orders, groups, objectives | `[0x4a9e30]`/`0x4a9e2c`, `[0x4a9e00]`/`0x4a9e04`, `[0x4a9e40]`/`0x4a9e3c` | pass 1 (group array), pass 2 |
| Effect pools | section 4 (empty after the drains, except objects created after them) | stage 2, drains in `0x461608` |
| Player and cameras | `0x4d24e6`, `0x4d2684`, `0x4a9b84`, `0x4d251c`, camera `0x4d24ea`, group cameras `0x4d2688[]` | `0x461608` |
| Cockpit and HUD | `0x4d24c0`, `0x49b0f8`, `0x4d03c8`, `A+0x1fe` | `0x461608` (H3) |
| Terrain | `[0x4a0be0]` (callback `+0x125` ← `0x428c60`), `0x4aaa78`, cell marks | pass 1, pass 2, `0x461608` |
| Result pairs | `0x4a9e6a`, `[0x4a9e6c]` | pass 1 |
| RNG | streams A `0x4d2599`, B `0x4d260b` (section 6) | — |
| Heap | every `new`/`new[]` above, through the game heap (`0x47749c`, `0x4774f0`) | all |

**The drains.** The load can create effect objects before the first frame:
the initial damage calls the MECH virtual `+0x74` (`vehicle_part`
`0x417de4`, [weapons.md](weapons.md#vehicle-part-417de4)),
whose static reach contains the explosion and debris constructors and the
flat-object flush `0x409904`. Drain 1 appends each remaining explosion to
its removal list and runs the explosion, light and flat-object flushes once
per object (an explosion's destructor can queue its light and flat object,
which the next two flushes free); drain 2 does the same for debris. Both
use the oldest-first iterator `pool_prev(pool, 0)` and stop when the pool is
empty. Smoke, meteors, projectiles and fires are not drained. Whether a
suite's load really creates such objects is not established statically.

## 6. RNG during the load

Static draw sites reachable from `0x461608` (direct calls; the virtual
`+0x74` named because its target is known), in execution order:

| Step | Chain | Stream | Count and condition |
|---|---|---|---|
| pass 1, terrain `0x4252f7` | `0x42789c` → `0x46c754`: R `0x46cac0`; → `0x46c4c4`: R `0x46c69d` | A | per tile, branches of the terrain loader ([rng.md](rng.md#stream-initialization-at-runtime)) |
| pass 2, each **selected authored** mech, in script order | `0x4178e8(A, w)`, w = record word `+0x84` | A | w < 0 or w ≥ 80: none. 60..79, 40..59: none here. 20..39: none here (fixed part write). 0..19: R `0x417976` (bit 0 picks part 7 or 8, damage 32000 through `+0x74`), and R `0x4179b1` (part 13 or 14) when type word `+0x4a` > 2 |
| same | `0x417a04` (`random_damage`) with severity s = 0/50/80/120/150 from w | A | s = 0: none; else for each of 29 part words that is nonzero: R `0x417a2e`, and when `(raw & 0xff)` < s+25 also N `0x417a48` (bound s), then damage through `+0x74` |
| same, inside `+0x74` | `vehicle_part` → debris `0x4086bc`, `0x408940`, `0x4089e4`, `0x408b74`, `0x408bb4`; fire/weapon selection `0x40cf44` | A | only when a part is destroyed (static reach, condition not traced) |
| same | `vehicle_part` → explosion sound `0x407f1c`; sounds `0x462830`, `0x4628e0` | B | as above; sound draws only for table rows with more than one variant |
| after the late arming point, `0x461df1` | first render `0x45fca8` → `0x4327ac` → `0x43408c` | B | word `0x4d2510` = 0; clock-gated |
| `0x461ebc` | `0x4328cc` → `0x462830` | B | sound row with variants |
| `0x461f2c` | drop sequence `0x45d85c`: R `0x45db50` per pass | B | word `0x4d2510` ≠ 0 |

MEC actors get **no** initial damage (the MEC branch jumps from `0x4258ff`
past `0x4178e8`). Flyer and building construction, the stage-2 callbacks,
orders, groups and objectives reach no draw on direct paths. The number of
stream-A draws during the load therefore depends on the terrain, on the
damage words of the selected authored mechs and on the drawn values
themselves; a replacement must keep the order terrain → mechs in script
order → per mech `0x4178e8`'s draws, then each part's. Under late arming
all load-time draws happen before the proxy arms and are invisible to the
`hk_rng_*` counters (section 8); with early arming (the default) the
counters include them (section 13). Either way they are already in the
stream state of the first tick record.

**Data-borne randomness.** VSHELL's clock-seeded draws reach the mission
only as `mission.var` words 4..6
([rng.md](rng.md#streams-across-save-load-and-the-vshell-hand-over)); the CD
track depends on the launcher counter `-R`; neither touches the streams.

## 7. File read order

1. `0x45a5dc`: `prefs.cfg` (54 bytes; `tapes\prefs.cfg` in playback), the
   `simvoic<lang>.vol` existence test, `keyjoy.cfg`.
2. `0x4614c0`: `data\script.dat` (20 bytes), `data\player.mec` (10 bytes).
3. Loading screen: `ld_herc<n>.dpl`/`ld_flyr<n>.dpl`, then the `.dba`
   (or `.hba`) of the same name.
4. `0x462410`: `sound.cfg`, then the sample table and preloads
   ([audio.md](audio.md)).
5. Stage 2, in the order of section 4.
6. Pass 1: `data\mission.var`, `data\player.mec`, `data\mission.str`,
   `data\script.dat`; inside it the world (`.wld`, `.dpl`, `cyc`, `.rmp`,
   flat-object shapes), the type data of the selected actors
   (`bases_an.dts`, `bases.dgs`, the mech name table and shapes), then the
   terrain (`zone<n>.dat`, `mat0.dat`, the `.dba` grid).
7. Pass 2: `data\script.dat` again; `mechwpns.dts`.
8. `0x461608`: `lo_curs.dba` (below 640 pixels), the HUD fonts and bitmaps
   (`0x4544c0`), `cockpit.dpl` and the cockpit layout (`vue`,
   `impactcp.dpl`), and what the first render and the start sequence load.

Every name goes through the VOL-aware stream or `0x45cdf4`
([file_io.md](file_io.md#the-stream-library-and-path-helpers),
[names](file_io.md#resource-name-families)); the DBSIM-specific opens are
listed in [file_io.md](file_io.md#mission-load-0x461608).

## 8. Arming and live visibility

This section describes the late arming point, which the proxy uses with
`ES2_EARLY_ARMING=0`. Early arming (section 13, the default) arms at the
call of the load `0x45f42b`, before phase A; the load then runs hooked and
its calls are counted.
The proxy installs function hooks, entry probes, coverage and the RNG hooks
at its first DirectDraw or MCI call
([live_coverage.md](../reference-game/live_coverage.md#arming-time));
DirectDraw is never created in the oracle
([presentation.md](presentation.md#directdraw-branch)). The first MCI call
on the direct paths of the load is `0x46313c` → `0x464858` → `0x473c48` →
`mciSendCommandA` at `0x461dc3`. The map agrees for all six suites:

- never entered after arming (no map row although probeable and called
  unconditionally): `0x452ae4`, `0x44f6f4`, `0x4544c0`, `0x429660`,
  `0x431530`, `0x437c8c` (each with the single call site in `0x461608`, the
  last at `0x461cd1`); `0x42a1f0` reads 0 although `0x461d2f` calls it
  unconditionally; `0x464858` reads 0 (its load-time call is the one that
  arms);
- entered once after arming: `0x45b758` (single site `0x461f3f`),
  1/1/1/1/1/1;
- `0x45fca8` and `0x45f570` both read 937 in c3_10, consistent with the
  load-time render at `0x461df1` being counted (the frame that ends a
  mission returns before the render).

So arming lies after `0x461d2f` and before `0x461f3f`, and `0x461dc3` is the
only MCI call between them on a direct path (its condition: the six suites
run with a sound context, [audio.md](audio.md), and header word 4 is 0 in
every prepared example, [mission.md](../formats/mission.md#prepared-script-exact-framing)).
**Consequences:**

- Phases A to D always run unhooked, also in a fully `replaced` run: the
  pre-arming closure (716 functions) contains 79 registered hooks, among
  them `pool_alloc`, `pool_prev`, `removal_append`, `removal_flush`,
  `terrain_height`, `script_start`, `object_init`, `random_damage`,
  `actor_animate`, `mount_offset`, `shield_capacity`, `weapon_record`,
  `def_lookup`, `fire_group`, `camera_update`, `euler_matrix`, the track
  and matrix helpers and `rng_next`/`rng_bounded`. Their counters never
  include load-time calls.
- `0x41b130` (cockpit sync) is called at `0x461d02`, before the late
  arming point: under late arming its hook sees only the render-root calls.
  `0x45fca8` (render root) is called at `0x461df1`, after it: its counter
  includes the load-time call.
- The map cannot give live counts for any load-only function under late
  arming: all of them read 0 (or have no row).

## 9. What the after-load acceptance must snapshot

**In-place compare does not fit the load.** The compare mode's mechanism (compare_mode.md) runs the
original on a snapshot, restores and runs the replacement at the same
addresses ([compare_mode.md](../reference-game/compare_mode.md#snapshot-regions)).
The load allocates heap blocks (`new`, `new[]`, pools), opens and reads
files, and creates pools whose headers are heap objects; a second phase
would allocate again at different addresses and leak the first phase's
blocks, so pointers differ and the heap is not restorable. Compare is
refused for every piece that allocates; it is possible for the pure pieces
of section 10 (`0x425348`, `0x411b90`, with an extra region `0x415068`),
with the actor pools as snapshot regions.

**Proposed acceptance: an after-load snapshot, original against replaced.**
At the first entry of the logic frame `0x45f570` (after the load and
before any frame code), dump:

- the fixed regions: header and mission tables `0x4a9e00..0x4a9e98`,
  variables `0x4a9e70` (2000 bytes), strings `0x4aa640..0x4aa728`,
  selection and MEC `0x4aa72a..0x4aaa7c`, stage-2 type globals
  `0x4a9572..0x4a9dfc` and `0x6b4f2c..0x6b4f3c`, the globals object
  `0x4d24bc..0x4d27b8`, both RNG streams (`0x4d2599`, `0x4d260b`, `0x72`
  bytes each), the actor list `0x4a9af8`, `0x49a871..0x49a884`,
  `0x4cf878..0x4cf888`, `0x4d03c8`, `0x49b0f8`;
- the pool blocks of every pool in sections 2 and 4 (the compare mode's resolver: header
  `+0` free, `+2` used, `+4` size, `+6` block; (free + used) × (size + 8)
  bytes) with their removal lists;
- the heap tables reached from the fixed pointers (coordinates, angles,
  routes and their point arrays, regions, triggers, timers, orders, groups
  with their actor and order arrays, objectives, string indices, MEC
  records, result pairs), sized by their count words;

and canonicalize pointers: a dword that points into a dumped block becomes
(block id, offset), other heap pointers become "heap" plus the target's
dumped contents where the block is known. Two runs (all hooks original;
the load's hooks replaced) must give identical dumps. The tick log's first
record is written after the load and frame 0, so without the snapshot a
load difference shows only through the watches and the later frames; the
snapshot checks the built state directly, including the tables no watch
reaches. Cockpit, HUD, camera and graphics-library objects (H3) are
excluded except for the camera position and angles, which the simulation
reads.

Section 13 implements this with a walk of the whole CW3220 heap instead of
per-table dumps, pairing blocks by their position in the walk.

## 10. The load in hookable pieces

The load split into pieces that can be hooked one by one. Map counts are
a60/c1s/c1c/c3_09/c4_08/c3_10 under late arming: every piece reads 0 in
all six suites (load-only code). With early arming (L0) a once-per-load
piece is called exactly once per suite; per-actor pieces are called once
per selected actor of the suite's launch set (counts from that set's
`script.dat` with `audit_script.py` plus the selection rule of section 2
step 6, plus N MEC records). "guest" = stays original behind the bridge.
Every piece is own code now; the last column names the section.
| Piece | Functions | Size | Depends on | Live counts (map) / with L0 | Compare |
|---|---|---|---|---|---|
| **L0** (tooling) | arm function hooks, probes and the RNG hooks at DBSIM's entry, reusing the import shim's entry arming (`__startup` slot, [rtl_imports.md](../reference-game/rtl_imports.md)), plus the after-load snapshot of section 9 | M | the entry arming; a full regression, because the 79 hooks of section 8 then run replaced during the load for the first time | — | — |
| L1 `pool_create` | `0x471ad8(header, capacity, size)` | S | L0 | 0 × 6 / 12 per suite (its 12 call sites, all in the load: `0x405e28`, `0x40776d`, `0x407b9e`, `0x408890`, `0x408fe9`, `0x4090f9`, `0x409887`, `0x409a62`, `0x40a090`, `0x425196`, `0x425231`, `0x46b1d8`) | refused (heap) |
| L2 load leaves | `0x425348` subject resolve, `0x411b90` action copy, `0x415068` condition init, `0x423358` region, `0x423104` timer, `0x423d38` group array | S each | L0 | 0 × 6 / per object | `0x425348` (no stores) and `0x411b90` (actor fields; the actor pools as regions): yes; `0x415068` only with the part table behind `[A+0x206+4]` as an extra region; the others refused (heap) |
| L3 `initial_damage` | `0x4178e8` (`random_damage` and `vehicle_part` are hooks already; V74 resolved through the vtable) | S | L0 | 0 × 6 / once per selected authored mech | refused (RNG A, effects, possible trigger fire) |
| L4 `weapon_fit` | `0x4175dc`, `0x4104ec`, the weapon factory `0x40fff8` | M | L0; the weapon helpers `0x40cec0` … `0x40fb2c` guest | 0 × 6 / once per mech (`0x4104ec` once, for the player) | refused (heap) |
| L5 constructors | MECH `0x415bb0`, FLYER `0x4215f4`, building factory `0x405314`, group `0x423b34`, `0x405db0` | M–L | L0, L2; shape set-up `0x4033a4`, `0x40d2cc` guest | 0 × 6 / per object | refused |
| L6 `actor_load` | `0x4253d8` | L | L2–L5; stream reads guest (the stream library) | 0 × 6 / 1 per suite | refused; after-load snapshot |
| L7 `world_load` | `0x424308` (selection, compaction, pools; the type loads `0x41ff10`, `0x422e90`, `0x405ebc`, `0x42049c`, `0x422fb4` guest; world `0x42e010` and terrain `0x42789c` guest, resource, terrain) | L | L1, L2; stream reads guest | 0 × 6 / 1 per suite | refused; after-load snapshot |
| L8 stage-2 pools | `0x4076e4`, `0x4090cc`, `0x40a060` (pool-only callbacks; the resource-loading ones: sections 25 and 31) | S each | L1; the dispatcher `0x401d94` | 0 × 6 / 1 per suite each | refused (heap) |
| L9 `mission_load` | `0x461608`: the drains, the height fix and player pick, the camera fields the simulation reads; everything H3/audio/input/platform guest | M | L6, L7, L8 | 0 × 6 / 1 per suite | refused; after-load snapshot |
Own code: L0 section 13; L1 to L3 section 14 (compare mode of the L2
leaves section 18); L4 section 16 (its leaves section 32); L5 section 17
(shape set-up section 19, collision data section 20, damage data section
22, group joins section 23); L6 section 21; L7 section 29 (terrain zone
load section 24, type loads section 26, base flattening section 27,
building types section 28); L8 section 15 (with type data section 25,
effect pools section 31); L9 section 30.

Not H4: the loading screen, palettes, cockpit, HUD, cameras' graphics
objects, `0x44f6f4`, the first render's drawing (H3), sound and CD music
(audio), `0x45a5dc`/`0x452ae4`/the modal callbacks (input), the pump and
limiter init (platform).
limiter init (platform).

## 11. Corrections and additions for other documents

- `0x497f9a` is not a "static-object cleanup table" of the load. It is
  the exception-frame descriptor that `smoke_update` `0x4092dc` passes to
  `0x49630c` (`mov eax, 0x497f9a` at `0x4092e2`); the records after it pair
  the smoke pools `0x4a966e`/`0x4a9684` with destructor code of that module
  (exit-time static destruction, compare the exit records in
  [rtl_imports.md](../reference-game/rtl_imports.md)). The load-time
  cleanup is the drain pair of section 5. "Actor construction `0x4258a2`"
  is the call site of `0x4175dc` (weapon fitting) inside `0x4253d8`, not a
  function.
- [rng.md](rng.md#draws-during-the-mission-load) (virtual constructors of the
  world load are not followed): the MECH damage virtual `+0x74` reached from
  `0x4178e8` adds the `vehicle_part` draws of section 6, including stream B
  sound draws before the late arming point; `0x4178e8` runs only for
  selected authored mechs, not for MEC actors (not "once per loaded MECH").
- [rng.md](rng.md#arming-and-the-loads-draws) (late arming): the map brackets the late
  arming point between `0x461d2f` and `0x461f3f` (section 8), which
  supports `0x461dc3`.
- Cockpit sync ("runs … at mission start"): the mission-start call
  `0x461d02` precedes the late arming point, so under late arming it stays
  original even when `0x41b130` is hooked.
  `0x41fdb0`, `0x422d90`, `0x428230` as stage-2 callbacks: confirmed; the
  full set of 19 is section 4.
- The functions index (`docs/functions/`) shows "0" for `0x433f7c`, but
  `0x433f7c` starts with `mov eax, [0x49b088]`, not a one-byte push: it was
  never probeable, so "probed and never entered" is an inference error of
  the index for callees without a push prologue.

## 12. Open

- The explosions and debris the load creates before the drains, and
  whether a part destroyed by the initial damage fires the actor's trigger
  `A+0x1b6` during the load (static reach through `vehicle_part` →
  `0x423430`): the root's callee counters `478628`, `407b3c`, `4077e8`,
  `409904` and `4083e0` count them (section 30); live values pending.
- The roles of `0x48dd08`/`0x4913dc` (hypothesis: persistent class
  tables).

Answered: the stream-A draws during the load (`rng_next` 1,024 to 16,436
load-time calls per suite, section 13); who places MEC actors (section
17); the roles of `0x4955c4` and of the slot `[0x4a5824]` (section 34) and
the tables of `0x40fecc`, `0x40d218`, `0x40cbec` (section 29); early
arming leaves every golden identical (section 13).

## Evidence

Sections 1 to 12: `build\claude\d-23\` (scripts, outputs; no listings or code images):
`build\claude\d-23\` (scripts, outputs; no listings or code images):
`walk.py` (control-flow and direct-call walk from `0x461608`; outputs
`all-calls.tsv`, `all-closure.tsv`, `all-rng.tsv`), `stages.py` (init
records and stage registrations; `stage-registrations.txt`), `names.py`
(resource names per function; `names.txt`), `reach.py` (RNG reach per
subtree; `rng-reach.txt`), `pre.py` (hooks before and after the arming
site; `pre-post-arming.txt`), `summ.py` (call and store summaries, output
not kept). Inputs: the pinned `llvm-objdump` listing of the reference
DBSIM (kept outside the evidence directory), `docs/functions/`,
`dbsim-map-20261010/map.json`. Run with
`python -B <script> <listing> …` from the evidence directory.

## 13. L0: arming before the load and the after-load snapshot

Code: [early_arming.h](../../tools/reference-game/proxy/early_arming.h)
(included by `media_proxy.c`, entry stubs `ea_entry_load` and
`ea_entry_frame`),
[after_load_snapshot.py](../../tools/reference-game/scripts/after_load_snapshot.py),
`configure-tick-log.ps1` (`-EarlyArming`, `-AfterLoadSnapshot`). Facts come
from the pinned `llvm-objdump` listing of the reference DBSIM; the fixture's
`reference` mode re-checks them against `VER95\DBSIM.EXE`. **Confirmed
live** (`<es2-data>\logs\l0-live-20261011`, below). Early arming is
the default; the snapshot is opt-in.

### Why the proxy armed at the first MCI call

`initialize_forwarding` (`media_proxy.c`) arms everything at once: it loads
the system `ddraw.dll` and `winmm.dll`, reads `es2-oracle.ini`, hashes
`DBSIM.EXE` through CryptoAPI (a provider DLL is loaded), opens the tick log
and may start its writer thread, installs the function hooks
(`install_rng_hooks`), live coverage and entry probes (both register an exit
callback through CW3220's `_atexit` and patch `__exit`), the speed runtime's
exit hooks and the VOL probes' forwarding mark. None of this may run in `DllMain`
(loader lock), so it was hung on the first call that reaches the proxy, a
DirectDraw export or one of the two MCI import slots. The point is a lazy
trigger, not a dependence on game state: nothing in it reads what the load
builds (the hooks patch code prologues, the tick log patches `0x45f4ad` and
reads state only at ticks, coverage and probes patch code).

What an earlier point must keep:

- **Not before `__startup`.** The import shim's slot (`rtl_imports.md`, "When it
  installs") runs before the 65 init records and before CW3220's start-up;
  `_atexit` registration, loading `ddraw.dll`, `winmm.dll` and a CryptoAPI
  provider before the static initializers would change more than the load
  needs (the import shim hashes with an own SHA-256 precisely to load no DLL there),
  and the static initializers, WinMain's set-up, the VOL registration and
  stage 1 would run hooked as well.
- **Nothing written to the game's stack** (guest code reads stack residue): the
  arming code runs on a private stack.
- **The site bytes original before any verifier runs**, so coverage, probes
  and the hooks see the original image.

### The arming point: the call of the mission load

`0x45f42b: e8 d8 21 00 00` (`call 0x461608`) in the game main `0x45f160`,
after stage 1 and the optional arena set-up (`0x476df4`, `G+0xb0` ← 0),
before `0x461608`'s first instruction (also the target of the `je` at
`0x45f401`; the instruction boundary is kept). It is the only `call rel32` to `0x461608`
in the code section and no registered hook prologue covers it (fixture,
`reference` mode).

- **`DllMain`** (DBSIM.EXE only): reads `[arming] early` and
  `[snapshot] after_load` from `es2-oracle.ini` next to the proxy
  (`GetPrivateProfileIntW`, kernel32; no DLL is loaded). With neither key
  set nothing else happens. Otherwise it checks the PE header
  (`es2_hook_image_valid`) and that the site is `call rel32` to its target,
  allocates a 1 MiB private stack and rewrites only the call's rel32 to
  `ea_entry_load`. A refused check changes nothing.
- **At the call** (the import shim's technique, `rtl_startup_entry`): the entry stub
  stores ESP, switches to the private stack, saves flags, the general
  registers, the TEB stack bounds (`fs:[4]`, `fs:[8]`, set to the private
  stack) and the x87/SSE state, restores the original rel32, runs
  `initialize_forwarding` (through the same `InitOnce`; the first MCI call
  later only registers the codec), restores everything and jumps to
  `0x461608` with ESP and every register as the call left them. The only
  stack write is the call's own return address `0x45f430`, the original
  value. LastError is preserved. If something armed before (a DirectDraw or
  MCI call before the load), the step only logs `early_arming_already_armed`.

**What changes with `early=1`:**

| Area | Change |
|---|---|
| Game code and data | nothing by itself; the hooks, the tick site, coverage and probe sites are installed before phase A instead of at `0x461dc3` |
| Without `[hooks]` (every hook original) | no function hook is installed at all (the proxy installs hooks only when one is not original); only the tick log, coverage and probes (if configured) and the DLL loads move earlier. Heap reservations may land elsewhere (heap addresses already differ between runs, [rtl_imports.md](../reference-game/rtl_imports.md) 4.3) |
| With `[hooks]` | the load-reachable hooks of section 8 (79 in section 8's list, plus later hooks, for example `cockpit_sync` `0x41b130` at `0x461d02` and `stage_dispatch` `0x401d94` at `0x4617b8`, and those reached through virtual calls, such as `vehicle_part` from the initial damage) run in their configured mode during the load; replaced ones for the first time |
| `hk_*_calls` | include the load-time calls. Against a run without early arming, every counter of the first tick record is larger by exactly the calls made between `0x45f42b` and `0x461dc3`, and the offset stays constant afterwards; hooks not reachable from the load keep their values. `hk_rng_next`/`hk_rng_bounded` gain section 6's load-time draws (terrain, initial damage), which answers section 12's open count |
| Tick records | unchanged: the first record is still written at the first frame; `hk_*`/`rtl_*` fields are not compared (`oracle_regression.py` `TICK_FIELDS`); the `start` line is written earlier |
| Live coverage, entry probes | the load's executions are counted (the load-only functions of section 8 read their first nonzero counts); their traps then happen during the load too, with the exception-dispatch residue of [live_coverage.md](../reference-game/live_coverage.md) |
| VOL probes | the `forwarding` mark moves from inside the load to its start |
| H6 import shim | unaffected (own `__startup` slot, armed before the init records) |
| Before `0x45f42b` | unchanged: static initializers, WinMain's set-up, the VOL registration and stage 1 stay unhooked |

With neither key set the only change is the two INI reads in `DllMain`.

### The after-load snapshot

Taken at the first execution of `0x45f487: e8 e4 00 00 00` (`call 0x45f570`,
the logic frame; its only caller), with the same entry technique
(`ea_entry_frame`, the rel32 restored before the step). Before it the game
runs stage 3 `0x401d94(3)`, `G+0xa8` ← 1 and the main loop's head (stage 4,
`0x4678c0` while word `G+0x1a` is 0). It is written once to `[snapshot]
path` (never overwriting a file) in the binary `ES2SNAP1` layout described
in `early_arming.h`:

- **fixed regions** (reference addresses): `0x4a9572..0x4a9dfc` (stage-2
  type globals, actor pools and lists, `0x4a9b84`), `0x4a9e00..0x4aa728`
  (header, tables, variables, strings), `0x4aa728..0x4aaa7c` (status word,
  selection and remap, MEC, `0x4aaa78`), `0x4aab00` + `0x249f0` (the table
  `0x428230` clears), `0x4cf878..0x4cf88c`, `0x6b4f2c..0x6b4f3c`, the
  globals object `0x4d24bc..0x4d27b8` (player, cameras, both RNG streams),
  `0x49a871..0x49a884`, `0x4d03c8`, `0x49b0f8`, `0x49b034`, `0x4a0be0`,
  the arena header `0x6bb2f8..0x6bb310` (including CW3220's byte sum
  `0x6bb30c`); the arena `0x604f04` + `0xb0000` only when `[0x6bb304]` is
  nonzero (option `-l`); `[snapshot] extra = 0xVA:LEN,...` adds up to 16;
- **the whole CW3220 heap** instead of a list of tables: the region table
  of the loaded `cw3220.DLL` (count `+0x23938`, (base, size) pairs
  `+0x23738`; only when the DLL is the reference `cw3220.DLL`) walked block by
  block (marker 1 at the base, header `p−4` and footer `p+s−8` = `s|1` in
  use, end marker); every block is listed with address, size and in-use
  flag, blocks in use with their `s−8` data bytes. This covers every pool
  block, removal list and heap table of section 9 without per-table sizes;
  `[snapshot] max_bytes_mib` (default 256) caps the data;
- header flags: heap walked, no heap, walk error, truncated, complete,
  armed early.

**Comparison** (`after_load_snapshot.py compare A B`): blocks are paired by
their position in the walk (region table order, then address, which is the
allocation history); sizes and in-use flags must agree. A differing byte
counts as equal when a 4-byte window covering it holds, in both snapshots, a
pointer with the same canonical form: (block index, offset) for any heap
block (free blocks included, for stale pointers; the end of a block's data
included) or the image offset. Unaligned pointers (packed game objects,
`0x4a9b7a`) are found the same way. Free blocks' bytes are never compared.
Output: `IDENTICAL`/`DIFFERENT`, the first differences with the block's
first referrer in the fixed regions, `--json` for the full result,
`--ignore VA:LEN` for fixed ranges; exit 0/1/2. Cockpit, HUD and graphics
objects are in the heap too; they are original code in both runs, so they
compare equal unless the simulation changed them.

### Tests

`//tools/reference-game/proxy:early_arming_{off,arm,refuse,snapshot,reference}_test`
(synthetic image with both call sites and recorder targets, synthetic
CW3220 data and heap): `off` nothing allocated or changed; `arm` the step
runs once, before the target, on the private stack with the TEB bounds
swapped and the site already original, the target receives the caller's
registers, ESP, return address, the untouched 256 bytes below ESP, x87
control word and ST0, LastError and TEB bounds, a second call goes straight
through; `refuse` wrong target, wrong opcode, wrong image size or base,
busy; `snapshot` layout, heap walk (free blocks without data), the
no-heap/walk-error/truncated/early flags, INI path and `extra`, no
overwrite, and the script on the proxy's own files (relocated heap
identical, one changed byte different); `reference` the two sites, their
single callers, the targets' prologues and no hook prologue at the sites
(485 hooks). `//tools/reference-game/scripts:after_load_snapshot_unit_test`
(11 cases: relocation, unaligned pointers, content, pointer offset, image
pointer, layout, ignore, header and region differences, truncation,
malformed files, CLI exit codes). `oracle_speed_scripts_test` covers the
INI sections and manifest keys of `configure-tick-log.ps1 -EarlyArming
-AfterLoadSnapshot`.

### Switching it on

`configure-tick-log.ps1` writes `[arming] early=1` unless
`-EarlyArming:$false` or `ES2_EARLY_ARMING=0`; without it the INI has no
`[arming]` section and the proxy arms at the first MCI call (section 8).
`[snapshot] after_load=1` with `path=<tick log without
.jsonl>-after-load.snap` is written with `-AfterLoadSnapshot` or
`ES2_AFTER_LOAD_SNAPSHOT=1` (`ES2_AFTER_LOAD_SNAPSHOT_EXTRA` sets
`[snapshot] extra`); the clone's `oracle-media-manifest.json` then records
`early_arming`, `after_load_snapshot` and the header hash. The runners pass
their environment to their children, so for a regression:

```powershell
$env:ES2_AFTER_LOAD_SNAPSHOT = '1'     # <run>-after-load.snap next to each tick log
& .\tools\reference-game\setup\run-oracle-parallel.ps1 ...   # as usual
Remove-Item Env:ES2_AFTER_LOAD_SNAPSHOT
```

A snapshot holds the mission's heap (several MB): it is evidence for
`es2-data` only, and above the 5 MiB budget it needs an `.es2budget-allow`
entry or stays outside git.

### Live result

Evidence: `<es2-data>\logs\l0-live-20261011`.

- **Every hook original, early arming** (all 33 suites registered then):
  every suite identical to its golden; the proxy log (`video-compat.jsonl`)
  has `early_arming_armed` once per game process. The earlier point itself
  (DLL loads, tick log, the site patch) changes nothing.
- **Every hook and the 41 CW3220 imports replaced, early arming:** every
  tick comparison identical. The load-reachable hooks run replaced during
  the load. `load-time-calls.json` lists per suite `hk_*_calls` at the
  compared tick minus those of the run without early arming
  `check-r6-final-u07`: about 95 hooks have load-time calls in all 22
  comparable suites, for example `stream_read` 105,651–192,578,
  `dist3d_delta` 65,494–260,838, `normalize` 32,258–130,050, `stream_tell`
  42,487–78,646, `member_lookup` 1,524–2,042, `pool_next` 180–2,952,
  `rng_next` **1,024–16,436** (the load-time draws of section 6),
  `object_init` 28–76, `pool_alloc` 40–104, `terrain_height` 24–126; the
  damage path (`vehicle_part`, `region_hit`, `weapon_damage`, `rng_bounded`
  8–19) in 13 suites (the initial damage). The input-side counters
  (`key_message`, `control_record`, `key_axes`, `mouse_dispatch`,
  `sfx_play`) differ by small amounts as between any two identical runs.
- **Snapshot:** two original runs differ (heap blocks and about 230–290
  bytes in the fixed regions); the noise model of section 14 classifies
  these differences.

### Open

- Compare-mode hooks during the load: their snapshot regions are resolved
  from pool pointers that may still be 0 at that point; not run in
  fixtures or live.
## 14. Pieces L1 to L3 and the snapshot noise model

Own code: L1 (`pool_create`), L2 (`subject_resolve`, `action_copy`,
`condition_init`, `region_init`, `timer_init`, `group_array`, and the pool
walk `pool_nth` `0x471cb0` that `subject_resolve` and the group constructor
use) and L3 (`initial_damage`): nine hooks in the adapter group
`mission_load`, core `core/src/mission_load.c`. Facts are read from the
pinned `llvm-objdump -d -M intel` listing of the reference DBSIM.
**Confirmed live:** `check-h495` and the after-load snapshots below.
Evidence (scripts and logs, no listings): `build\claude\h4-95\`. The
after-load snapshot's noise model (`after_load_snapshot.py`) belongs here
too.

### The noise model

`tools/reference-game/scripts/after_load_snapshot.py` compares two
after-load snapshots (section 13):

1. **Pairing by reference.** The fixed regions lie at the same addresses in
   both runs. A 4-byte window (aligned or not) that points into a heap
   block in both snapshots, at the same offset of the block, pairs the two
   blocks; the walk continues breadth-first through every paired block in
   use. Only this graph is compared. A block reached from two places must
   pair with the same block both times (otherwise a `pairing_conflict`).
   Blocks no fixed region reaches (the environment, the CW3220 stream
   buffers and objects, freed blocks) are counted and not compared;
   `--unreached align` aligns them by the walk's size sequence and reports
   their differences for information only.
2. **Pointers.** Equal when both point into paired blocks at the same
   offset, or into the image at the same offset; with a version-2 snapshot
   also into the same module at the same offset, or both into other
   committed memory of the same type (`foreign`: system heaps,
   VirtualAlloc'd buffers, mapped views; counted as `foreign pointers`).
3. **Learned noise.** `noise A1 A2` (two original runs) writes every
   remaining difference as an entry (block path and offset, or the fixed
   VA); `compare --noise FILE` counts a difference covered by an entry as
   noise, not as a difference. A block path is the chain by which the walk
   first reached the block in snapshot A: `0x004d250a/+0x6` is the block the
   pointer at `+6` of the block of the fixed dword `0x4d250a` points to.
4. **Scope.** `--scope all` (default) walks from every fixed byte;
   `--scope pools` compares and walks only from the pool headers and
   removal lists of sections 2 and 4 and the actor list `0x4a9af8`
   (pointer, capacity and count words): the actor arena and the pools.
5. `--pairing index` pairs blocks by walk position (section 13's first
   comparison), for diagnosis.
6. **Classes** (on by default, `--no-classes` switches them off; counted on
   the `CLASSES` line and in `classes_matched`, left out of `noise`
   files), described in "Noise classes" below: `environment`,
   `matrix_padding` and `aim_residue` for every compare;
   `allocation_slack`, `block_fit`, `byte_count` and `freed_target` only
   when the two snapshots' `heap_runtime` differ (own against cw3220).

**Snapshot version 2** (`proxy/early_arming.h`): after the heap the proxy
writes the committed regions of the address space (base, size, allocation
base, protection, type; one VirtualQuery walk) and one record per image
allocation (base, size of image from its PE header, the module's file name
without directory); flag 64 marks a map cut short. Version 1 files are
still read (no foreign or module classes).

**Why walk-position pairing fails.** Two original runs compared by walk
position (section 13's live result; `*-full-a-b.json`, `*-full-b-c.json` in
`<es2-data>\logs\l0-live-20261011`) differ in 6,705 to 7,554 bytes,
all from one cause: CW3220's start-up copies the **process environment**
into heap blocks (the first block is the pointer array, 4 bytes per
variable; blocks 48 to 117 hold `NAME=VALUE` strings such as `ORACLE_...`,
`TEMP=C:\...`), and the runner's environment had one variable more in one
run. The insertion shifts every later walk index. An original against a
replaced run with equal environments differs only in unreached blocks
(stale file data in 1,032-byte blocks, hypothesis: CW3220 stream buffers;
three fields of six 44-byte blocks, hypothesis: stream objects), in
pointers outside the image and the CW3220 heap (`00BFxxxx`/`0247xxxx`,
among them ten dwords of a 32,416-byte block and `0x4d250a`, `G+0x4e`, the
graphics-library object) and in byte `+0x173` of three 432-byte blocks.
Pairing by reference, the version-2 map and the classes remove all of
these.

Tests: `//tools/reference-game/scripts:after_load_snapshot_unit_test`
(synthetic snapshots: relocation, an inserted environment block that
walk-position pairing turns into differences, content and offset
differences with their path, image pointers, pairing conflicts, block
sizes, scope pools, ignore ranges, header and region differences,
truncation, unreached alignment, foreign and module pointers with and
without the map, the noise file from two "original" snapshots, malformed
v1/v2 files, the CLI; classes `Classes`, `RuntimeClasses` and
`StaleClasses`); `//tools/reference-game/proxy:early_arming_snapshot_test`
(the v2 map of the fixture process: sorted committed regions, the
fixture's heap in a private region, kernel32 with its size of image; the
script on files the proxy code wrote). A synthetic heap of 6,000 blocks
and 8.8 MB, all reached, compares in 1.6 s.

### Noise classes

**`environment`.** The walk does not follow windows into `_environ` and its
strings, found structurally: the first of the walk's first 256 in-use
blocks that holds at least 8 pointers to the starts of in-use `NAME=VALUE`
string blocks, then 0. Counted as `environment_links`, the blocks as
`environment_blocks_a/b` (also in `summary`, with `environment_array`, near
walk index 0). Origin: in `c3_09-loadout-b` the 64-byte block #48 holds
`ES2_CLONE_LOCK_TOKEN=` (21 bytes, `+0x00..+0x14`) and the clone lock's
`[Guid]::NewGuid().ToString('N')` (`CloneGuard.psm1`), 32 hex digits at
`+0x15..+0x34`, new in every run. The path `0x4a98f4/+0x23d9e/+0xbc` starts
at the `mechwpn2` shape list (`0x40fd04`, loaded by `0x474cd8` into one
500,000-byte `calloc` arena that `0x474be4` shrinks), whose bytes at the
unaligned `+0x23d9e` equal an address inside `_environ` (the last hop
`+0xbc` fits entry 47). That link holds only when both heaps lie at the
same addresses (block #48 at `0x02690b94` in both runs); with the heap
`0x70000` higher the same bytes point nowhere. So it is a coincidental
window, and the block is process input, not simulation state. es2sim
copies its own process environment into its heap
([own_process.md](../reference-game/own_process.md) "The heap trace"),
which the class finds there too (101 blocks).

**`matrix_padding`.** Byte 19 of each node matrix of a paired animation
manager whose vtable, node count and paired matrix block agree (the
`+0x173` bytes; writer and value in "Node matrix byte 19" below). `0x4a9b84`
is the player actor inside the MECH pool block; the paths' first offsets
`0xa92`, `0xe04`, `0x185a` are nodes 3, 4 and 7 (stride `0x372`) at actor
`+0x34`, the animation manager M that `0x402301` allocates (`new(0x42)`) and
the constructor `0x478838` builds (vtable `0x4a14c4` at +0, node count word
+0x0c, `[M+0x16]` = the node matrices from `0x49667c(0, 0x20, n, 1,
0x495132)`, which allocates through `new[]` `0x4774f0`: 32n + 8 bytes from
`calloc`, the array at offset 0). 432 bytes = 32 × 13 + 8 + the 8-byte block
overhead, so n = 13 and `0x173` = 11 × 32 + 19. It shows because with early
arming the constructor's traversal runs hooked during the load
(`node_matrices` 25 load-time calls in a60; `pose_parent` 0: the
traversal's core computes it inline). **No compared field can depend on
it:** byte 19 is read only inside 20-byte block copies (the padding
contract's (b)–(d); the hit tests' `matrix_compose` copies of node matrices
keep it without reading it, [hit_tests.md](hit_tests.md)), and the tick
fields (`oracle_regression.py` `TICK_FIELDS`: clock, RNG streams, position,
angles, speed, throttle, limiter counters) read no matrix padding.

**`aim_residue`** (cockpit_sync's stack residue in the cockpit panel H =
`[0x49b088]`). H is reached as `[[0x4d24c0] + 0x1f]` (the getter
`0x42982e`, `mov eax, [eax+0x1f]`), the 656-byte block at path
`0x004d24c0/+0x1f`. Values in the original (O1 = O2; es2sim has zero in
all of them):

| suite | +0x26c (target) | +0x270 | +0x274 | +0x278 | word +0x27c | word +0x27e |
|---|---|---|---|---|---|---|
| acceptance-60s | 0 | 0x8d | 0x87 | 0x8d | 0x004d | 0x24bc |
| c3_10-loadout-hercs | 0 | 0x53 | 0x1f | 0x53 | 0x004d | 0x24bc |
| c3_09-loadout-b | 0 | 0x59 | 0x35 | 0x59 | 0x004d | 0x24bc |

- **Writer.** The only writer is `0x434a24` (`0x434a24..0x434a68`):
  +0x26c ← argument 0 (the target), +0x270..+0x27b ← 12 bytes at
  argument 1, word +0x27c ← argument 2, word +0x27e ← argument 3. Its
  only call site is `0x41b458` in cockpit_sync `0x41b130`; the arguments
  are cockpit_sync's frame locals ([hud.md](hud.md) step 14), stored on
  every call, also without a target.
- **Callers of cockpit_sync.** The mission load `0x461608` at `0x461d02`
  and the render root `0x45fca8` at `0x45fdbf`; the mission load also
  calls the render root at `0x461df1` (when word `[edi+0x54]` = 0). At the
  snapshot (logic frame `0x45f570`) the fields therefore hold what a
  target-less cockpit_sync call left from its stack. The two words read
  0x004d and 0x24bc, the halves of 0x004d24bc next to the view root
  0x4d24c0, which looks like a saved register value (not followed
  further: no reader sees it).
- **Readers.** The HUD draw (`0x43d7e1..0x43d824`) reads +0x270.. and
  +0x27c only when +0x26c ≠ 0 (otherwise `0x43d7f9` jumps to `0x43d85a`).
  The MFD status method `0x43a5a0` reads +0x27c/+0x27e only on its
  target path (`0x43ac60..0x43ac9b`; X+0x3e = `[[X+8]+8]` set at
  `0x43a684..0x43a68a`, tested at `0x43a690`). The constructor's `lea` at
  `0x431034` does not read them. Every frame, the render root's cockpit
  sync (`0x45fdbb`/`0x45fdc4`) rewrites the fields before the HUD draw
  (slot 8 at `0x45fdc5`). With target 0 nothing reads the bytes.
- **Why es2sim has zero.** es2sim runs cockpit_sync at its guest stack
  coordinate ([hud.md](hud.md), "stack residue"), and that stack holds
  zeros there. Reproducing the bytes would need the original's load and
  render frames below cockpit_sync: outside the reconstructed behaviour,
  like the matrix padding.
- **The class.** When the H blocks of both snapshots are found this way,
  are paired by the walk and both hold target 0 at +0x26c, a differing run
  in +0x270..+0x27f is counted, not reported. A target ≠ 0 on either side,
  or a difference at +0x26c or +0x280, stays a difference. Count: 5 runs
  per suite (+0x270, +0x274, +0x278, +0x27c, +0x27e).

**Heaps of different runtimes** (the own heap model `core/src/rtl_heap.c`,
which es2sim and the own `cw3220.dll` share, against CW3220's). Two
original runs agree on these bytes, so a noise file cannot learn them:

- **`allocation_slack`.** A request of n bytes gets a block of
  `round4(n) + 8` bytes; calloc and new zero or fill only n of them, so
  the last 1-3 data bytes keep whatever the memory held before (for
  example `+0x22` of a 44-byte block, 2 bytes; `+0x41` of a 76-byte block,
  3; `+0x3af` of a 952-byte block, 1). The class covers a run entirely
  within the last 3 data bytes of the shorter block. Below 8 requested
  bytes the slack can be up to 7; that is not modelled, and such runs are
  still reported. The same slack byte at `+0x22` of a 44-byte block also
  differs between two original runs on different clones.
- **`block_fit`.** Next-fit hands out the whole free block when the
  remainder would be below the 16-byte minimum block, so block sizes can
  differ by less than 16 (multiples of 4), as the free list's history
  decides (the two blocks of the `0x4a9706` tree, the bullets.dba bitmap:
  40 against 28, 48 against 56). The common bytes are still compared; the
  `CLASSES` line gives the sum of the paired size differences, B minus A
  (`block_size_delta`).
- **`byte_count`.** The dword `0x6bb30c` sums the header words of the live
  blocks that `0x47747c` allocated; nothing branches on it
  ([rtl_imports.md](../reference-game/rtl_imports.md) section 4.3), and it
  follows the block sizes.
- **`freed_target`.** A differing window whose two values both point into
  free blocks (`block_at`, in-use flag 0). Seen in the HDDMap's icon
  table: H+0x20 points to a 1940-byte block, its +0x101 to the 908-byte
  HDDMap M (vtable `0x49d9d8`, [cockpit.md](cockpit.md)), M+0x31 to the
  576-byte `new[](0x230)` table of 140 (0x8c) map-icon pointers; each icon
  is a 100-byte `new(0x5b)` block (method table `0x49d9b0` at +0x17, M at
  +0x20; hud_cockpit.c `map_page`, `0x44c28c`). The icon list `0x44df00`
  (`..0x44e0a6`) hands out icons by the word M+0x10a, then at
  `0x44e07b..0x44e09e` deletes the unused ones, `[M+0x31][k]` for k =
  [M+0x10a] .. 0x8b, with `0x477548`, **leaving their pointers in the
  table** (es2sim's `map_icons`, sim/src/hud_draw.c, does the same). The
  reads found are bounded by the count: `icon_next`, the map draw's loops
  (`k < [M+0x10a]`) and its last icon `[M+0x10a] - 1`; `map_lamp`
  `0x44ede0` is called at `0x44daac`/`0x44db11` with the button index
  [M+0x105], which lies below the count (hypothesis: it is a button
  selection, and the buttons are the first icons). The freed memory lies
  differently: CW3220's heap has holes from earlier frees (the start-up
  environment and stream buffers), the own runtime's heap none. The walk
  pairs a free block only through a window at the same offset, so entries
  whose offsets disagree show as raw bytes. Two heaps of one runtime place
  freed memory alike, so there the class is off; a pointer into a free
  block on one side only stays a difference.

**Chance pairings (not a class).** Inside byte data (terrain cells,
bitmaps, text) a 4-byte window can look like a heap address by chance;
when the heap layout differs between two runs, the same bytes point into
different blocks and show as **pairing conflicts** at paths that end in odd
offsets (seen in c1_01-collision, `snapshot-r7`, 6 conflicts not
reproduced in four further runs; and in c3_09-loadout-b, `snapshot-r7g`,
O1 vs O2 already 4 conflicts, O2 vs R 8, not in the isolation rerun). A
conflict at an unaligned offset is noise unless it repeats. Between an
es2sim and an original snapshot equal byte strings cannot pair: es2sim's
heap is `0x20000000..0x2077ffff`, the original's blocks lie between
`0x025xxxxx` and `0x09bxxxxx`; unequal bytes that both look like pointers
into their own heap at the same offset can still pair and then show as
conflicts.
### Contracts (from the bytes)

All nine are cdecl; EDX:EAX is reproduced exactly; ECX is dead after every
call site (forward walk of the listing from each return address, both arms
of every branch; `build\claude\h4-95\liveness.py`), so the framework's
entry ECX stands in for it. No function reads an uninitialized local or
stack residue that a replacement would change, and no entry or exit shim
is needed. Game calls go through the guest bridge with the original's
entry EAX/EDX/ECX (listed per call below), so nested hooks keep their
modes.

**`pool_create` 0x471ad8 (header H, capacity, size)**, with its only
callee, the pool initializer `0x471a50` (computed by the core). Only the
low words c (capacity) and s (size) count. `IMUL` of the sign-extended
words gives total = c x (s + 8) in EDX:EAX. When c ≠ 0, `new[](total)`
`0x4774f0` (EAX total, EDX the product's high dword, ECX total; `POP ECX`
reloads the pushed total). Then `memset(block, 0, total)` `0x49681a` (EAX
total, ECX total or the entry ECX when c = 0). `0x471a50` writes H+0 = c,
H+2 = 0, H+4 = s (words), H+6 = H+0Ah = the block, H+0Eh = 0, rereads H+4,
stride = (s + 8) as a word, and for k = 0 .. c−1 (signed words) node k =
block + k x stride: [node] = node + stride, [node+4] = node k−1 (0 for the
first), then [last node] = 0. Returns EAX = H, EDX = the last node (c > 0)
or memset's EDX with its low word replaced by s (c = 0); ECX = total (`POP
ECX` of the local). A negative capacity faults in the original (memset of a
negative size, or the link write at `0000FFF8h` when s = −8): the taken
edge of `0x471aa3` is PROVED unreachable. Callers: the twelve sites of
section 10 (L1); each writes EAX or ECX before reading it, and EDX is next
written by `new` (`0x47749c`), which reads no register.

**`pool_nth` 0x471cb0 (pool, index)**: k = the index word; EAX = 0; loop:
`pool_prev(pool, EAX)` `0x471c70` (first call: the entry EDX/ECX; later:
EDX = the previous EDX with its low word = the word before the decrement,
ECX = pool_prev's); 0 ends; otherwise the word before the decrement is
tested and k is decremented in its own argument slot; 0 ends. Returns the
object at position index of pool_prev's order (oldest first), or 0; EDX
as described, ECX pool_prev's. The slot write is released by every caller
(`ADD ESP,8`). Callers: `subject_resolve` (three sites) and the group
constructor `0x423b34` (three sites).

**`subject_resolve` 0x425348 (index, scope)**: scope 0: EDX = the
sign-extended index word, EAX = 122 x EDX + [`0x4a9e00`] (the group array),
ECX the entry's. Scope 1/2/3: k = the signed remap byte at
`0x4aa78e`/`0x4aa888`/`0x4aa946` + index (the full dword index; callers
pass sign-extended words), then `pool_nth(pool, k)` with the pool from
`0x4a9b7a`/`0x4a9db9`/`0x4a95a0` and the entry registers (EAX 0, EDX k,
ECX pool) / (EAX k, EDX pool, ECX entry) / (EAX pool, EDX entry, ECX k);
returns pool_nth's registers. Other scopes: EAX = 0, EDX/ECX the entry's.
Callers: orders `0x425f9b`, objectives `0x4262fa`, trigger subjects
`0x4264dd` (trigger types 7, 8, 9, 10 give scopes 1, 2, 3, 0 through the
table at `0x426492`).

**`action_copy` 0x411b90 (A, ids, actions)**: ten times: read the word at
ids, write it to A+1BAh+2i, read the word at actions, write it to
A+1CEh+2i (interleaved, so overlapping inputs see earlier writes).
Returns EAX = A+1CEh, EDX = ids+20, ECX = actions+20. Callers: selected
authored mechs `0x42599f`, flyers `0x425c20`, buildings `0x425e7d`. At
`0x4259a7` the caller loads CX only and passes ECX to `initial_damage`,
which reads only the low word.

**`condition_init` 0x415068 (C, first, second, third)**, C = A+206h of a MEC
actor: T = [C+0Ch], the word list L = [T+4] (read once), table 1 = [C+4],
table 2 = [C]. Rows i = 0..18 of table 1: for i < 13, (100 − w) x M / 100
with w the next word of `first` and M = word 0 of row i of [T] (rows of
nine words; C+0Ch and [T] are reread for every row), else 0. Words 0..11
of table 2: for i < 9, (100 − w) x L[i] / 100 with w from `second`, words
9..11 are 0. Then i = 0..9: top = word 0 of row 19+i, sum = L[12+i] + top
(word), q = (100 − w) x sum / 100 with w from `third`; q ≤ top (signed
words): table 1 word 19+i = q; otherwise table 1 word 19+i = FFFFh and
table 2 word 12+i = FFFFh when q − top > L[12+i] (reread after that
write), else q − top. Products are 32-bit, `CDQ`/`IDIV` by 100. Returns
EAX the last quotient (low word replaced by q − top on the second path),
EDX its remainder or (second path) the list pointer, ECX the list pointer
after 22 words (the second `POP ECX` reloads it). The three argument
pointers advance in their own slots (released by the caller). Caller:
`0x4258de` (MEC actors).

**`region_init` 0x423358 (R, record, points)**: [R+2] = 12 x (signed word
record+2) + points; word R = word record; kind 0: [R+6] = points + 12 x
(signed word record+4), EDX that product, ECX the first value with its low
word = 0; otherwise [R+6] = 10 x (signed word record+4) = EDX, ECX that
signed word. EAX = R. Caller: `0x4249b3` (pass 1, per region).

**`timer_init` 0x423104 (T, first, words, duration)**: [T] = first;
`timer_arm(T+2Ch, EDX)` `0x467ac4` with EDX = the entry EDX's high word
and the duration word (EAX first, ECX T+2Ch); then ten dwords from words to
T+4 (forward). Returns EAX = words+40, EDX = T+44, ECX = 10. Caller:
`0x424c68` (pass 1, per timer).

**`group_array` 0x423d38 (G, count)**: word G+4 = count; the runtime's
array construction `0x49667c(0, 7Ah, count, 1, 0x4240e4)` (EAX the
sign-extended count; the element constructor `0x4240e4` clears word +4
of each element); [G] = its EAX. Returns 49667c's EDX:EAX. Caller:
`0x424dec` (pass 1, only when the script has groups).

**`initial_damage` 0x4178e8 (A, word)**: w = the signed low word. w < 0 or
w ≥ 80: severity 0. 60..79: 50; 40..59: 80. 20..39: 120, and
`multiply_q8(100h, v)` `0x47e0a0` with v the signed word +0Ah of the list
[[A+212h]+4] (EAX = EDX = v, ECX the list; the list pointer is the local
[EBP−4]), its result into word +0Ah of [A+206h] (read after the call),
byte A+0B3h = 1. 0..19: 150; `rng_next(0x4d2599)` (stream A; EAX the
argument word, the entry EDX/ECX; `POP ECX` reloads the stream), part 7
for an odd draw else 8 (the draw's high word kept), the actor's virtual
+74h (A, part, 32000, 0) with EAX = [A] (the vtable), EDX the draw's EDX,
ECX the stream; then when the signed type word [[A+1F2h]+4Ah] > 2 (read
after the call) a second draw (EAX the virtual's EAX, EDX [A+1F2h], ECX the
virtual's ECX) and the virtual with part 13/14 (EAX the part, EDX [A], ECX
the stream; [A] is reread); bytes A+0A4h, A+0B3h, A+0B4h = 1. Always
`random_damage(A, severity, severity + 25)` `0x417a04` (EAX severity + 25;
EDX/ECX: the entry's, multiply_q8's EDX and [A+206h], or A+92h and the
virtual's ECX). Returns random_damage's EDX:EAX; ECX = the local [EBP−4]
(the list on the 20..39 path, an uninitialized local otherwise; the
caller `0x4259b0` does not read it). Caller: `0x4259b0`, selected authored
mechs only.

### In vitro (group `mission_load`)

`invitro/mission_load_cases.h`: per hook 1,001,024 direct cases in the full
tier (100,000 quick), against logging stubs for new[] `0x4774f0`,
pool_prev, pool_nth, timer_arm, `0x49667c`, multiply_q8, rng_next,
random_damage and an arena vtable for the virtual +74h (memset is the
harness's own): pair, ECX (except `initial_damage` outside 20..39),
ordered calls with slots and entry registers, the arena and the image
globals (group array, the three pool pointers, the remap tables, stream
A); stubs change what the originals read after a call, and objects overlap
(a pool header inside its block, region and record, timer and words, the
condition tables over the list and the words). 256 installed 31/a7 cases
per hook (fast and preserving dispatch). Real-callee passes (3,072 cases;
the hook original or replaced against a run without it, the nested hooks
in rotating modes): `pool_nth` over the real pool_prev/pool_next,
`subject_resolve` over the real pool_nth, pool_prev and pool_next on
three real pools, `timer_init` over the real timer_arm, `group_array` over
the real `0x49667c` and element constructor (new[] stubbed),
`initial_damage` over the real rng_next, rng_bounded, multiply_q8,
random_damage and part_group_max (stream A seeded). 29 branch sites, 28
both ways, the taken edge of `0x471aa3` PROVED. One caught mutation per
hook (`MUTATIONS.md`). Core unit test `//core:mission_load_test` (790
checks, hand-computed).

### Live result

**Regression** `check-h495` (every hook replaced, 507; CW3220 original):
36 of 36 identical, no nonzero skipped/bypassed/mismatch counter. The nine
`hk_<name>_calls` equal the live counts below exactly, for example:

| Suite | `pool_create` | `pool_nth` | `subject_resolve` | `action_copy` | `condition_init` | `region_init` | `timer_init` | `group_array` | `initial_damage` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| c1_01-success | 12 | 65 | 15 | 62 | 2 | 8 | 0 | 1 | 8 |
| c3_10-loadout-hercs | 12 | 41 | 9 | 37 | 4 | 2 | 3 | 1 | 12 |

**After-load snapshot** (`<es2-data>\logs\h4-95-snapshot-20261012`,
comparison JSONs and noise files; imports original, O1 and O2 every hook
original, R every hook replaced; every run identical to its golden). Two
original runs are identical with no noise entry (4,175 / 4,175 / 4,583 /
4,739 blocks in acceptance-60s, c1_01-collision, c3_10-loadout-hercs,
c3_09-loadout-b). O2 against R, before the classes: the three `+0x173`
bytes in acceptance-60s and c1_01-collision (original 2, 3 and 2, replaced
0; `matrix_padding`) and 4 spans of ASCII hex digits in one 64-byte block of
c3_09-loadout-b (`0x4a98f4/+0x23d9e/+0xbc`, `environment`); no hook omits a
write. The later snapshot runs, which cover every piece of the load, are in
"Snapshot results" below.
### Node matrix byte 19

**Writer.** Byte 19 of a child node's matrix is written by the copy path
of `matrix_compose` `0x47fa20` (`rep movsd` at `0x47fce8`, 20 bytes) from
`pose_parent` `0x478bc4`'s local matrix at EBP−0x20, whose byte 19
(EBP−0xD) no instruction writes (`0x47ebb8` writes bytes 0..18, the mode
at 18; `pose_parent` the translation 20..31). The slot is set by the node
traversal `node_matrices` `0x478c64` itself. With the traversal's ESP = S
before it pushes the arguments of a call:

- the child call `pose_parent(pose, parent, child)` runs with EBP = S−0x14,
  so EBP−0xD = S−0x21;
- the root call `0x478c1c(pose, matrix)` (parent word −1, flag 1) runs
  with EBP = S−0x10 and pushes EBX = `[ebp+0Ch]`, its matrix pointer
  `[M+0x16]` + 32 × node, at `0x478c54`: S−0x24..S−0x21.

So every child call after a root call copies the **most significant byte
of the root node's matrix address**; before the first root call it copies
what the caller's stack left at S−0x21 (the probe below is exact with
this model). Fields of M: vtable `0x4a14c4`, node
count word +0x0c, flags +0x0e, poses +0x12, node matrices +0x16
(constructor `0x478838`, which sets every flag to 1 and runs the
traversal at `0x478925`).

**Evidence.** In every after-load comparison JSON with a `+0x173`
difference (`es2-data\logs\h4-95-snapshot-20261012`, `snapshot-r7-*`,
`snapshot-h5-26-20261022`; the `.snap` files are deleted) the original's
byte equals the high byte of its block's address: 03 for the block at
`0x03c25744`, 02 for the blocks at `0x02xxxxxx`; the other side read 00
when it was a replaced run or es2sim with the zero padding. The
blocks are the 13-node matrix arrays (432-byte blocks) of the MECH pool's
actors 3, 4 and 7 (`0x4a9b84/+0xa92|+0xe04|+0x185a/+0x16`), node 11 =
`0x173` = 11 × 32 + 19. c3_09-loadout-b and c3_10-loadout-hercs have no
such difference (`matrix_padding` 0 in `snapshot-h5-26-20261022`). In
vitro, the residue probe of `node_matrices` (`track_callee_cases.h`,
`tc_residue_probe`, part of `differential_math_test`) runs the original
traversal on a 31/a7-poisoned stack against the core with padding = the
poison byte and compares byte 19 of every node exactly: 8192 cases, 5150
byte-19 changes from the poison and 1614 from
the root pointer (the harness arena at `0x11xxxxxx`, byte 11). With the
pointer byte removed from the core the probe fails (original 0x11, core
0x31).

**Own code.** `es2_node_matrices_registers` (`core/src/animation.c`)
reproduces it: `padding` until the first root node, then the root
matrix address's high byte. The proxy's `node_matrices` hook writes the
original's byte (the same CW3220 heap); es2sim writes `0x20` (its heap
lies at `0x20000000..0x2077ffff`). The es2sim replays of acceptance-60s,
c1_01-collision, c3_09-loadout-b and c3_10-loadout-hercs stay IDENTICAL;
es2sim's after-load snapshot changes only at these three bytes of a60
(00 to 20), c3_09's not at all. Residue table: `sim/src/residue.c`, row
`node_matrices`, rule `writer`.

**Classes.** O against R (both on CW3220's heap): equal bytes,
`matrix_padding` 0. O against es2sim: 3 bytes in a60 and c1_01-collision
(02/03 against 20), counted by `matrix_padding`. Two es2sim runs: equal.

**Live.** Two runs with every hook original (O1, O2) and one with
`node_matrices` replaced (R), each over acceptance-60s, c1_01-collision and
c3_09-loadout-b with the after-load snapshot (early arming, extra
`0x6cbd58:0x18c`): every run identical to its golden; O1 against O2 0 noise
entries; O2 against R IDENTICAL without classes in all three suites (no
`+0x173` difference), so the hook's byte 19 equals the original's.
Evidence: es2-data `logs\snapshot-node-matrices-20261024` (`summary.md`, the
comparison JSONs, SHA-256 of the deleted `.snap` files).

### Live counts

Each hook runs during the load only (`0` in every column of the DBSIM map,
which predates early arming), so its counter is constant from the first
tick record on. Counts from the bytes and the launch sets
(`build\claude\h4-95\load_counts.py` over `es2-data\oracle\launch`; the
default C1_01 script has the list counts of
[mission.md](../formats/mission.md#prepared-script-exact-framing), the
selection taken from `c1_01-unarmed`'s script of the same lists, and two
MEC records). The selected authored mechs equal the load-time
`random_damage` deltas of section 13 in every suite (C1_01 8, c1_02 12, c3_09 12, c3_10
12, c4_08 10, c5_05 11, c1_08 7), and the timers its `timer_arm` deltas
(3 in c3_10, 1 in c1_02):

| Hook | a60 / c1s / c1c | c3_09 | c4_08 | c3_10 | c1_02 | c5_05 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `pool_create` | 12 | 12 | 12 | 12 | 12 | 12 |
| `hk_pool_create_call_4774f0` | 12 | 11 | 12 | 11 | 12 | 12 |
| `pool_nth` | 65 | 48 | 40 | 41 | 65 | 48 |
| `subject_resolve` | 15 | 9 | 10 | 9 | 17 | 10 |
| `action_copy` | 62 | 44 | 38 | 37 | 64 | 44 |
| `condition_init` | 2 | 4 | 1 | 4 | 1 | 4 |
| `region_init` | 8 | 6 | 7 | 2 | 5 | 3 |
| `timer_init` | 0 | 0 | 0 | 3 | 1 | 0 |
| `group_array` | 1 | 1 | 1 | 1 | 1 | 1 |
| `initial_damage` | 8 | 12 | 10 | 12 | 12 | 11 |

(c3_09 and c3_10 have no selected flyer, so their FLYER pool has
capacity 0 and no `new[]`.)

### Snapshot results

Every run: imports original, early arming, O1 and O2 every hook original,
R every hook replaced, compared with `--scope all` and `--scope pools`,
the `.snap` files deleted and the comparison JSONs and noise files kept in
`<es2-data>\logs\<name>`. **O2 against R is identical in
acceptance-60s, c3_10-loadout-hercs and c3_09-loadout-b in every run**;
acceptance-60s counts its 3 matrix padding bytes.

| Snapshot | Hooks replaced in R | Notes |
|---|---|---|
| `snapshot-r7-20261012` | 510 (the classes, L8) | c1_01-collision: 6 pairing conflicts in R, not reproduced (chance pairings); O1 against O2 differed (101 against 102 environment blocks, see below) |
| `snapshot-r7b-20261013` | 603 (L1 to L5, L8) | O1 = O2 with 101 environment blocks in every run; c1_01-collision: see below |
| `snapshot-r7c-20261013` | 631 (object shapes, collision data) | O1 = O2, empty noise files; 4,169 / 4,567 / 4,722 paired blocks (all), 1,864 / 2,373 / 2,482 (pools) in a60, c3_10, c3_09 |
| `snapshot-r7d`, `-r7e`, `-r7f`, `-r7g` | the actor load, damage data, group joins, terrain zone load, stage-2 type data | sections 21 to 25; r7g: chance pairings in c3_09 (above), not in `c309-isolation` |
| `snapshot-r7h-20261013` | 685 (seven extra regions incl. `0x4987d8:8,0x498850:8`) | c3_09: a one-off of 259 differences, see below |
| `snapshot-r7i-20261013` | building types (seven extra regions incl. `0x6b5470:8`) | c3_09: two original runs differed in 110 places, all absorbed by the noise file learned from them |
| `snapshot-r7j-20261013` | the world load (eight extra regions incl. `0x4cfcc4:0x44`) | O1 = O2 |

- **Runner environment.** The parallel runner once left six variables in
  the calling PowerShell process (`ES2_CLONE_LOCK_TOKEN`,
  `ES2_LIVE_COVERAGE_CONFIG`, `ES2_ORACLE_SPEED_CONFIG`,
  `ES2_PARALLEL_CLONES`, `ES2_PREPARED_PROXY`, `ES2_TICK_BUFFERED`) as empty
  variables (`[Environment]::SetEnvironmentVariable(name, $null)`, which
  PowerShell 7 passes to .NET as an empty string), so the first invocation
  ran with fewer variables and its heap lay elsewhere. The runners restore
  the caller's complete environment
  ([oracle_regression.md](../reference-game/oracle_regression.md) "Runner
  environment"); the `CLASSES` line's environment block count shows it.
- **c1_01-collision's after-load heap varies between runs of the same
  mode** (`snapshot-r7b`: a heap region laid out differently, chance
  pairings through unaligned pointer-like bytes, one unaligned stored
  address under `0x6b4f2c/+0/+8/+0` at `+0x22`). On one clone a fresh
  original run O3 and two replaced runs R5, R6 are identical, while O2 and
  O3 differ by 2. Compare its snapshots only within one session of runs on
  one clone.
- **The c3_09 one-off** (`snapshot-r7h`): R had 259 differences: the
  pointer at `0x4a9cbc` (fixed region `0x4a9656/+0x666`) led to a
  2,064-byte array of (pointer, byte) records in the originals and to an
  11,808-byte zero block at the same address in R. The bisect
  (`c309-bisect`, one run each on three clones: O original, R all replaced,
  Rx all replaced except `type_loads`) did not reproduce it: O vs R and O
  vs Rx show the same **3** differences (the fixed byte `0x6bb30c`, one
  block size in the `0x4d24c0` tree, one slack byte at `+0x22` of a 44-byte
  block), which also appear between original runs on different clones. A
  repeat would need a new look at `0x4a9cbc`'s writer.
- **Own `cw3220.dll`** (`snapshot-r7c`, `Rown`, the a60 load with the own
  runtime deployed, against R): 240 differences
  (`acceptance-60s-o2-rown.json`, `-r-rown.json`; 50 listed), all in bytes
  the program never wrote: allocation slack (47 of the 50 listed), two
  block sizes of the `0x4a9706` tree and the byte counter `0x6bb30c` (low
  byte `0x0e` against `0x16`). The runtime classes count them; the live
  regressions with the own runtime are identical.

### es2sim's snapshot against the original

es2sim writes ES2SNAP1 version 2 (flags 32 "armed early" and 128 "own
heap", the memory map and the modules) at its `before_logic` step, the
original's step: the main loop's call of the logic frame `0x45f570` at
`0x45f487`. `sim/tests/replay_suite.py --suite NAME --after-load-snapshot
[--snapshot-extra 0xVA:LEN,...]` adds the proxy's `[snapshot]` section
(`after_load=1`, `path`, `extra`) to the run's INI and writes
`<output-dir>/NAME/es2sim-after-load.snap`; without the option the INI is
unchanged (`//sim:replay_suite_test`). The replays stay identical to their
goldens with the snapshot on.

**es2sim's own snapshots** (headless, two passes S1 and S2): flags `heap`,
`complete`, `armed_early` and `own_runtime_heap`; 14 fixed regions (the 13
of section 13 and the extra); one heap region at `0x20000000`
(`0x780000` bytes); 5,488 (a60), 6,035 (c3_09) and 5,885 (c3_10) blocks;
the modules es2sim.exe at `0x10000000` plus system DLLs. S1 against S2 is
identical in all three suites and both scopes:

| Suite | Paired blocks, all | Paired blocks, pools | Foreign pointers | Conflicts | Unreached differing runs |
|---|---:|---:|---:|---:|---:|
| acceptance-60s | 4,483 | 2,536 | 10 | 0 | 0 of 989 |
| c3_09-loadout-b | 4,859 | 2,957 | 13 | 0 | 0 of 1,160 |
| c3_10-loadout-hercs | 4,685 | 2,831 | 10 | 0 | 0 of 1,183 |

The only values that differ between the two files are the foreign
pointers: in c3_10 10 dwords at a stride of 540 bytes in a 32,416-byte
block, the sound cache's sample slots, whose sample blocks are `calloc`'d
in the host's CRT heap (the original's "ten dwords of a 32,416-byte
block"). In a self-compare every byte window that happens to fall into the
heap pairs a block with itself, and the heap base `0x20` is the ASCII
space, so these paired-block counts are no measure against the original.
Two es2sim replays of one suite also differ in the replay's temporary
install directory (`…\Temp\replay-<suite>-<random>\install`, reached
through `0x004a98f4/+0x6ceb`), per-run input of the replay runner. The
extra region in all three suites: handle `0x6cbd58` = `0x50500000`, Win32
word `0x6cbd5c` = 1, the 97 entry slots `0x6cbd60..0x6cbee3` 0; the byte
counter `0x6bb30c` reads `0x771c16` (a60), `0x7846cd` (c3_09) and
`0x76eff7` (c3_10).

**Expected differences, es2sim against the original (headless):**

| What | Where | Original | es2sim | In the compare |
|---|---|---|---|---|
| SOS9503.DLL module handle | fixed `0x6cbd58` (dword) | the DLL's base | the stand-in `0x50500000` (not committed) | `--ignore 0x6cbd58:4` |
| the 97 SOS entry slots | fixed `0x6cbd60..0x6cbee3` (`0x184` bytes) | the DLL's entry addresses | 0 | `--ignore 0x6cbd60:0x184` |
| Win32 word | fixed `0x6cbd5c` | 1 | 1 | compared, equal ([audio.md](audio.md)) |
| sample blocks (`GlobalAlloc(40h)` by the sample load `0x474360`) | the sound cache's sample slots | process-heap blocks | `calloc` blocks in the host's CRT heap | foreign pointers (about 10; 13 in c3_09) |
| allocation slack, whole-block fit, byte counter | paired blocks, `0x6bb30c` | | | classes `allocation_slack`, `block_fit`, `byte_count` |
| cockpit_sync's residue | H+0x270..+0x27f | residue | 0 | class `aim_residue` |
| dangling map-icon pointers | `0x004d24c0/+0x1f/+0x20/+0x101/+0x31` | into free blocks | into other free blocks | class `freed_target` |
| node matrix byte 19 | `+0x173` of three 432-byte blocks (a60, c1_01-collision) | 02/03 | 20 | class `matrix_padding` |
| environment, CW3220 stream buffers | the original's unreached blocks | | | not compared; `environment_blocks` |
| heap runtime | header flags | cw3220 | own | the note "heaps of different runtimes: A cw3220, B own" |

The handle and the slots lie outside the fixed regions, so only a snapshot
with `extra=0x6cbd58:0x18c` holds them. A sample-block pointer points
outside every heap block and module on both sides; the blocks' contents
(the sample structure, its self-pointers at `+0`, `+4` and `+0xfc`) are
never compared, because foreign memory is not walked. A sample-block
pointer that shows up as a difference means one side's block lies in
memory of another type. Anything else is a finding for the owner of the
block's writer.

**Procedure.** Original runs O1 and O2 (every hook and import original,
`ES2_AFTER_LOAD_SNAPSHOT=1`, `ES2_AFTER_LOAD_SNAPSHOT_EXTRA=0x6cbd58:0x18c`)
and es2sim's snapshots (headless, `ES2_DATA_ROOT` set):

```powershell
foreach ($s in 'acceptance-60s','c3_10-loadout-hercs','c3_09-loadout-b') {
  & <es2-dependencies>\python\python.exe -B sim\tests\replay_suite.py --sim <bazel-bin>\sim\es2sim.exe --suite $s --registry tools\reference-game\oracle-suites.json --oracle-script tools\reference-game\scripts\oracle_regression.py --after-load-snapshot --snapshot-extra 0x6cbd58:0x18c --output-dir "$L\S"
}
```

Then per suite:

```
python -B tools\reference-game\scripts\after_load_snapshot.py noise O1.snap O2.snap --out $L\NAME-noise.json
python -B tools\reference-game\scripts\after_load_snapshot.py compare O1.snap $L\S\NAME\es2sim-after-load.snap --noise $L\NAME-noise.json --ignore 0x6cbd58:4 --ignore 0x6cbd60:0x184 --json $L\NAME-o1-s.json
(the same with --scope pools --json $L\NAME-o1-s-pools.json)
```

The c3_09 snapshot is written at the start of its replay (8886 ticks), so
the compare need not wait for the replay's end.

**Result** (`<es2-data>\logs\snapshot-h5-26-20261022`, JSONs; the
rerun with the classes `aim_residue` and `freed_target` in
`build\claude\h5-26b\compare\`): O1 against O2 identical (0 noise entries);
O1 against es2sim **IDENTICAL** in all three suites and both scopes,
0 differences, with these class counts:

| suite | scope | aim residue | freed | allocation slack | block fit | byte count | paired blocks |
|---|---|---|---|---|---|---|---|
| acceptance-60s | all | 5 | 7 | 338 | 1 | 1 | 4169 |
| acceptance-60s | pools | 5 | 7 | 328 | 0 | 0 | 1864 |
| c3_10-loadout-hercs | all | 5 | 0 | 349 | 3 | 1 | 4567 |
| c3_10-loadout-hercs | pools | 5 | 0 | 335 | 2 | 0 | 2373 |
| c3_09-loadout-b | all | 5 | 84 | 384 | 4 | 1 | 4722 |
| c3_09-loadout-b | pools | 5 | 84 | 366 | 1 | 0 | 2482 |

acceptance-60s also counts 3 matrix padding bytes; 6 or 7 foreign
pointers; environment blocks 103 against 0. The `freed_target` counts are
the entries from offset 4 × count on of the icon table (count [M+0x10a] 69
in a60, 42 in c3_10, 50 in c3_09): in a60 entries 69..132 point into one
free block and 133..139 into another in the original, 69..139 into one in
es2sim; in c3_09 50..139 point into 10 free blocks in the original, into
one in es2sim; in c3_10 one free block on each side with equal offsets
(paired, so 0). Mutation witnesses of the two stale classes:
`build\claude\h5-26b\mutations.log`.

### Open

- Reached H3 objects (graphics context, cameras' graphics objects) are part
  of `--scope all`; if they show replaced-run differences that are not
  noise, they belong to the hooks that build them.
## 15. L8 stage-2 pool callbacks

The three pool-only callbacks of `stage_dispatch(2)` (section 4), read from
the pinned `llvm-objdump -d -M intel` listing of the reference DBSIM
(descriptor words read from the file). Own code: adapter group
`mission_pools`, core appended to `core/src/mission_load.c`. **Confirmed
live:** `check-r7-1`, `snapshot-r7-20261012` (section 14). Evidence (scripts and logs,
no listings): `build\claude\h4-98\`.

| Hook | VA | Record | Builds |
|---|---|---:|---|
| `light_pools` | `0x4076e4` | 4 | light table `[0x4a9608]`, light pool `[0x4a95fe]` (3 × `0xe`), list `[0x4a9602]` (25) |
| `smoke_pools` | `0x4090cc` | 7 | smoke pool `[0x4a9684]` (20 × `0x2a`), list `[0x4a9688]` (25), then `0x408fb8` |
| `projectile_pools` | `0x40a060` | 10 | projectile pool `[0x4a96c2]` (160 × `0x66`), list `[0x4a96c6]` (80) |

Section 4's table needs one precision: the `SMOKE_BALL` pool `[0x4a966e]`
(200 × `0x4c`) and its list `[0x4a9672]` are built by `0x408fb8`, which also
loads the smoke-ball shapes (`0x474cd8`) and bitmap (`0x45cdf4`) and
stores `[0x4a967c]`; it is own code as `smoke_balls` (section 31), like
the other resource-loading callbacks.

### Call path and live visibility

Each function is registered once, by `0x401d64(fn, 2)` at `0x4078df`,
`0x4094a3` and `0x40a163` (static init records 4, 7, 10); no other code
refers to them. `0x401d94` is called with stage 2 only at `0x4617b8`, inside
the mission load `0x461608`, which runs once per process (section 1). The
dispatcher calls every table entry of the stage through `call [ebx]` at
`0x401dac` and reads no register afterwards (`inc esi; add ebx,6; cmp si,
[0x497074]`), so the callbacks' EAX/EDX/ECX matter only as the next
callback's entry registers. With early arming (section 13, the default)
the hooks are installed at `0x45f42b`, before `0x4617b8`: each runs exactly
**once per suite**, so `hk_<name>_calls` is 1 from the first tick record
on; the DBSIM map (before early arming) reads 0 for all three.

### Contracts (from the bytes)

All three: no arguments, `push ebp; mov ebp,esp; add esp,-N; mov
eax,<descriptor>`, then the runtime frame setup `0x49630c`. The
descriptors' second dword gives the record offset: `0x497b16` → −`0x2c`
(`light_pools`), `0x497f5e` and `0x4982ec` → −`0x28`. `0x49630c` (reads
EAX only, keeps EDX and ECX) fills the record at EBP + offset (+0 the old
FS:0 head, which it also returns in EAX; +4 handler `0x4967f0`; +8 the
descriptor; +0Ch the caller's ESP; +10h..+15h zero; +1Ch the construction
count `[[0x6cbee4]]`) and links it into FS:0. The word at record+10h
(EBP−1Ch or EBP−18h) is the runtime's context word, which the bodies set
around each construction (values below). The epilogue loads the saved
head from record+0 into EAX, stores it to FS:0 and returns: **EAX = the
old FS:0 head**.

Two common steps, written with the frame slot S, the context word C and
the image global P:

- **pool(capacity, size)**: `new(0x12)` `0x47749c` (entry EAX/EDX/ECX:
  those of the previous step); [S] = its EAX; `POP ECX` gives ECX = 12h.
  When EAX ≠ 0: C = in, `pool_create([S], capacity, size)` `0x471ad8` with
  EAX = new's EAX, EDX = [S], ECX = 12h, then C = out. Always ECX = [S]
  (reread), [P] = ECX.
- **list(capacity)**: `new(8)`; [S] = EAX; ECX = 8. When EAX ≠ 0: C = in,
  EDX = [S], `MOV AX,capacity` (the high word of new's EAX kept), word
  [EDX+6] = capacity, EDX = capacity × 4 (sign-extended word),
  `new[](EDX)` `0x4774f0` (EAX, EDX, ECX = 8); `POP ECX`, then ECX = [S];
  dword [ECX] = new[]'s EAX; EAX = [S]; word [EAX+4] = 0; C = out. Always
  EDX = [S] (reread), [P] = EDX.

**`light_pools` 0x4076e4** (record EBP−2Ch, C = EBP−1Ch; the pushed EBX is
restored): `new(0x404)` → [EBP−4], ECX = 404h. When nonzero: C = 14h, the
light-table constructor `0x406e44([EBP−4])` (EAX new's EAX, EDX the table,
ECX 404h), ECX = `[0x6cbee4]`, dword [ECX] −= 5 (the construction count),
C = 8. Then EBX = [EBP−4] (reread), [`0x4a9608`] = EBX and the leaf
`0x406ee4(EBX, 10, 2000)` (computed by the core): [EBX+10h] = 10 SAR 5 =
0, [EBX+14h] = 2000 SAR 5 = 62, returning EAX = EBX, EDX = 0, ECX = 62.
EAX = [`0x4a9608`] (reread), dword [EAX+0Ch] = 10000. Then pool(3, 0Eh)
with S = EBP−8, in/out 2Ch/20h, P = `0x4a95fe`; list(25) with S = EBP−0Ch,
in/out 38h/20h, P = `0x4a9602`. Returns EAX = the old head, EDX = the list
(or 0), ECX = the list, or 8 when its `new` failed. A null light table
faults in the original: `0x406ee4` writes `[0x00000010]` (the taken edge
of the `je` at `0x407705` is PROVED unreachable).

**`smoke_pools` 0x4090cc** (record EBP−28h, C = EBP−18h): pool(20, 2Ah) with
S = EBP−4, in/out 14h/8, P = `0x4a9684`; list(25) with S = EBP−8, in/out
20h/8, P = `0x4a9688`; then `0x408fb8()` with EAX = EDX = ECX = the list, or
(0, 0, 8) when the list's `new` failed. Returns EAX = the old head, EDX and
ECX = 408fb8's.

**`projectile_pools` 0x40a060** (record EBP−28h, C = EBP−18h): pool(160, 66h)
with S = EBP−4, in/out 14h/8, P = `0x4a96c2`; list(80) (`new[]` of 320
bytes) with S = EBP−8, in/out 20h/8, P = `0x4a96c6`. Returns as
`light_pools`.

**Callees and modes.** Through the guest bridge with the original's entry
registers: `0x49630c` (with the synthetic EBP), `new` `0x47749c` and `new[]`
`0x4774f0` (runtime, H6; neither reads a register nor writes its argument
slot), `pool_create` (a hook of group `mission_load`, keeps its own mode),
`0x406e44` (light table: the array construction `0x49667c` of 20 × 23h
entries with constructor `0x407548`, two graphics-library lights
`0x48bc40`, the nested leaf `0x406ee4(table, 2000, 3000)`; it does not write
its argument slot) and `0x408fb8` (resources). So the `POP ECX` values are
the pushed ones.

**Registers, stack and shims.** EDX:EAX is reproduced exactly; ECX is
reproduced by the core, and the adapters leave the framework's entry ECX
because nothing reads it: the dispatcher reads no register after
`0x401dac`, and the next callbacks of the table (`0x407b54` after
`light_pools`, `0x409a34` after `smoke_pools`, `0x40a818` after
`projectile_pools`) load EAX before `0x49630c` and set ECX and EDX before
any read (`mov ecx,4` before `rep movsd`; `new`, which reads no register;
`0x492bec` → `0x492b90`, `or ecx,-1` before `repne scasb`). The bodies read
only locals they wrote (the slots, the record head written by `0x49630c`);
a linear walk of the three following callbacks finds each local read after
an own store or after a call that received the local's address, so no
stack residue of these frames is read. No entry or exit shim. The frame
lives in a zeroed scratch at the original's EBP offsets; the adapter
writes the returned head back to FS:0 as the epilogue does.

**Compare:** refused for all three (allocation through `new`, `new[]` and
`pool_create`, the light table's construction and count, `0x408fb8`'s
resource loading); the after-load snapshot is the acceptance.

### In vitro (group `mission_pools`)

`invitro/mission_pools_cases.h`: per hook 1,001,024 direct cases in the
full tier (100,000 quick) against logging stubs for `0x49630c` (a record
stub that writes the real FS:0 head and a zero context word and links the
record), `new` (arena blocks that overlap the table and each other, or 0;
never 0 for the light table), `0x406e44` (moves the counter pointer,
changes the count, writes into the table), `pool_create` (writes the
header), `new[]` (sometimes a block over the list) and `0x408fb8`.
Compared: pair, ECX (original through an own caller against the core),
FS:0, the ordered calls with argument slots, entry registers and the
record's context word, the arena, the image globals (`0x4a95fe..0x4a960b`,
`0x4a9684`, `0x4a9688`, `0x4a96c2`, `0x4a96c6`, `0x6cbee4`) and the callee
counter deltas; the adapter is checked separately (pair, calls, memory).
256 installed 31/a7 cases per hook (fast and preserving dispatch). One
real-callee pass per hook (2,048 cases): real `0x49630c` and `pool_create`
nested in original and replaced mode over a bump allocator for `new` and
`new[]` (the harness's memset), `0x406e44`/`0x408fb8` stubbed, the hook
under test original or replaced. 7 branch sites, 6 both ways, the taken
edge of `0x407705` PROVED. One caught mutation per hook (`MUTATIONS.md`).
Core unit test `//core:mission_load_test` (1,040 checks, 250 of them new,
worked out by hand).

### Live counts

Once per suite, in every suite (stage 2 runs once per process and every
allocation succeeds in the game):

| Counter | every suite |
|---|---:|
| `hk_light_pools_calls`, `hk_smoke_pools_calls`, `hk_projectile_pools_calls` | 1 each |
| `hk_light_pools_call_49630c`, `_call_406e44`, `_call_471ad8`, `_call_4774f0` | 1 each |
| `hk_light_pools_call_47749c` | 3 |
| `hk_smoke_pools_call_49630c`, `_call_471ad8`, `_call_4774f0`, `_call_408fb8` | 1 each |
| `hk_smoke_pools_call_47749c` | 2 |
| `hk_projectile_pools_call_49630c`, `_call_471ad8`, `_call_4774f0` | 1 each |
| `hk_projectile_pools_call_47749c` | 2 |

`pool_create` stays at 12 per suite and `hk_pool_create_call_4774f0` as in
section 14 (the three callbacks still reach it, now through the bridge);
every `*_skipped`/`*_bypassed` 0.

### After-load snapshot reach

The hooks write the blocks reached from `0x4a95fe`, `0x4a9602`,
`0x4a9608`, `0x4a9684`, `0x4a9688`, `0x4a96c2` and `0x4a96c6` (the three
pools with their blocks and node links, the three lists with their slot
arrays, the light table) and allocate the same sizes in the same order
through the same `new`/`new[]`, so a difference there or in the block
pairing would be theirs.

### Live result

`check-r7-1` (all 510 hooks replaced, `rng_next` in `compare`, the 49 shim
imports replaced, five clones): **40 of 40 suites identical**, every counter of the
table above exactly as predicted in all 40 suites (`hk_light_pools_calls`
= `hk_smoke_pools_calls` = `hk_projectile_pools_calls` = 1, `_call_47749c`
3/2/2, `light_pools_call_406e44` = `smoke_pools_call_408fb8` = 1,
`hk_pool_create_calls` 12), no nonzero bypassed counter. After-load
snapshot `snapshot-r7-20261012` (section 14 "Snapshot results"): identical
in all four suites, with `--scope pools` and `--scope all`; no difference
in the blocks above.
## 16. L4 weapon fit

L4 of section 10, read from the pinned `llvm-objdump -d -M intel` listing
of the reference DBSIM (the dispatch tables and exception descriptors read
from the file). Own code: adapter group `mission_weapons`, core appended to
`core/src/mission_load.c`. **Confirmed live:** `check-h496`; after-load
snapshot `snapshot-r7b-20261013` (section 14). Evidence (scripts and logs,
no listings): `build\claude\h4-96\`.

| Hook | VA | Role |
|---|---|---|
| `weapon_fit` | `0x4175dc` | the weapon fit of one constructed mech (pass 2, `0x4258a2`) |
| `player_weapons` | `0x4104ec` | the player weapon system: the factory, then the three fire groups |
| `weapon_factory` | `0x40fff8` | the weapon set of a vehicle: one weapon object per mount |

The weapon helpers stay game code behind the bridge with their own modes:
the hooks `actor_animate`, `mount_offset`, `fire_group`, `shield_capacity`,
`weapon_record`, `def_lookup` and `proj_record`, and the original (section 32 makes six of them own code)
`0x420634`, `0x40e61c`, `0x417530`, `0x40cec0`, `0x40d240`, `0x40d128`,
`0x40cb84`, `0x40d284`, `0x40fb2c`, `0x413ac8`, `0x417d08`, the seven weapon
constructors `0x40e074` … `0x40e380`, the allocator `0x47a2c8` and the
runtime (`0x49630c`, `new` `0x47749c`, `new[]` `0x4774f0`, `0x49667c`,
`0x474bbc`/`0x474be4`).

### Call path and live visibility

`0x4175dc` has one call site, `0x4258a2` in the actor load `0x4253d8`
(section 3 step 4), reached once for every mech the pass constructs: the
selected authored mechs and the appended MEC actors. `0x4104ec` is called
only from `0x417634` (the player branch of `0x4175dc`), `0x40fff8` from
`0x41050e` (inside `0x4104ec`) and `0x417684` (the other branch). With early
arming (section 13, the default) the hooks are installed at `0x45f42b`,
before the load, so their counters are constant from the first tick record
on; the DBSIM map (before early arming) reads 0 for all three.

### Contracts (from the bytes)

All three are cdecl; EDX:EAX is reproduced exactly; ECX is dead at every
call site (forward walk from each return address, both arms of every
branch): after `0x4258a2` both paths write ECX (`0x4258b9`, `0x42590e`,
`0x42592c`, `0x425961`/`0x42596c`) and EAX/EDX before reading them; after
`0x417634` and `0x417684` ECX is reloaded from `[0x6cbee4]` and EAX from the
frame, while EDX reaches `actor_animate` as its entry EDX (reproduced);
after `0x41050e` EAX and EDX are reloaded and ECX enters `0x49667c`, which
reserves `[EBP-4]` with `PUSH ECX` and overwrites it (`0x496694`) before
any read, and whose element constructor `0x411892` loads ECX before using
it. The framework returns the entry ECX. Game calls go through the guest
bridge with the original's entry EAX/EDX/ECX (listed below); every `POP
ECX` after a call reloads the pushed value (no callee writes its argument
slot: checked for `0x420634`, `new`, `new[]`, `0x474bbc`, `weapon_record`,
`proj_record`, `0x40f540`, `0x40e61c`, `0x417bec`, `0x413ac8`, `0x417d08`).
`[0x6cbee4]` is the runtime's construction-count pointer; "count ±n" means
`[[0x6cbee4]] ± n`, the pointer reread each time.

**`weapon_factory` 0x40fff8 (W, A, count, mounts, items, secondary,
word)**, seven argument slots. Frame `add esp,-7Ch`; descriptor `0x499055`
gives the runtime record at EBP−44h (context word EBP−34h); `0x49630c` with
EAX = the descriptor and the entry EDX/ECX. Then [W] = vtable `0x499338`,
[W+4] = 0, count +1 (ECX the pointer), word [W+8] = the count word (`MOV
BX`, so EBX keeps the caller's high word), `new[](sx(count) × 4)` (EAX that
size, EDX 0, ECX the counter pointer; `POP ECX`), [W+4] = the block, the
locals EBP−4Ch = the block (cursor) and EBP−48h = the count word (ECX =
the size with its low word replaced), `0x474bbc(200000)` (EAX = W+4, EDX =
the block), ECX = 200000. Loop at `0x4104a4`: DX = the count word, the
word decremented in place, the cursor advanced by 4 when DX ≠ 0; the weapon
slot EBP−50h = the old cursor or 0, and 0 ends the loop (so the count is
taken unsigned: 65,535 iterations for −1, which runs past the small block;
the game's counts are small). Per mount ESI (stride 1Ah, the first not
advanced): i = the signed byte [mount+17h]; BX = items word i; the
secondary word i into EBP−52h (EAX = i with that low word); a negative item
becomes EBX = 0; item 0 stores 0 in the slot. Otherwise
`weapon_record(EBX)` (EAX = i/secondary word, EDX = secondary; the pushed
dword carries EBX's high word; `POP ECX` = EBX), EDI = the record C, AX =
the selector word [C+3Eh]: 22h → the secondary word (5 becomes 0, written
back) to `def_lookup(0, word)` (EDX = that word), 21h → nothing, else
`proj_record(selector)` (`POP ECX` = the pushed EAX); the result into the
projectile local EBP−58h. Then EAX = sx(BX), ECX = the heap `[0x6c5830]`
into EBP−5Ch; IDs above 20h leave the slot unwritten; otherwise the byte
table at `0x4100fd` (33 entries) picks one of ten blocks through the dword
table at `0x41011e` (classes of [weapon_classes.md](weapon_classes.md)).
Each block: `0x47a2c8(size, heap)` into its own frame slot (EAX = the class
byte, EDX/ECX as below), and when non-null the context word `in`, the
constructor, the context word `out`; the object or 0 into the weapon slot.

| Class | Block | IDs | Size | Slot | in/out | Constructor (slots) | After it |
|---|---|---|---:|---|---|---|---|
| 5 | `0x410146` | energy | 83h | EBP−4 | 14h/8 | `0x40e074` (W', A, mount, C, proj, word) | count −6 |
| 8 | `0x410222` | ballistic, racks | 81h | EBP−0Ch | 44h/38h | `0x40e140` (same) | count −6 |
| 7 | `0x4101a0` | ELF, ELF2 | 86h | EBP−8 | 2Ch/20h | `0x40e074` (staged in EBP−60h..−6Ch) | vtable `0x4992c0`, count +1, −7 |
| 6 | `0x41027c` | LAEW | 85h | EBP−10h | 5Ch/50h | `0x40e140` (staged in EBP−70h..−7Ch) | vtable `0x499248`, count +1, −7 |
| 9 | `0x4102fe` | ECM | 81h | EBP−14h | 74h/68h | `0x40e274` (W', A, mount, C, word) | count −7 |
| 4 | `0x410354` | TARG | 8Ah | EBP−18h | 8Ch/80h | `0x40e308` (same) | count −7 |
| 3 | `0x4103aa` | SHIELD | 81h | EBP−1Ch | A4h/98h | `0x40e344` (same) | count −7 |
| 2 | `0x410400` | TURBO | 85h | EBP−20h | BCh/B0h | `0x40e2bc` (same) | count −7 |
| 1 | `0x410453` | ENERGY | 7Dh | EBP−24h | D4h/C8h | `0x40e380` (same) | count −7 |
| 0 | — | 0, MINE | — | — | — | none | — |

Registers at the allocator: EDX = ECX = the heap (classes 1–5, 8, 9; EDX
reread from EBP−5Ch) or ECX = the heap and EDX unchanged (6, 7). At the
constructor: (EAX, EDX, ECX) = (A, W', proj) for 5/8, (A, W', mount) for
6/7 (all reread from the staging locals), (W', word, A) for the
equipment. The last slot is the word argument `[EBP+20h]` with the high
word of EDX (5, 8, equipment) or of EBX (6, 7, `MOV BX`, which stays in EBX
for the later mounts). After the null test the slot is stored from EAX
(5, 8) or ECX (the others), reread from the frame slot. At the end
`0x474be4()` (EAX 0, EDX with low word 0), count +1 (EDX the pointer),
ECX = the saved FS:0 head from the record, which the epilogue stores to
FS:0; EAX = W. No callee reads the word argument or the high word of EBX:
the seven constructors read at most `[EBP+18h]` (the projectile record;
`0x40df30` stores it at W'+20h), and `weapon_record` reads only the low
word (`MOVSX`).

The projectile local EBP−58h is written only when the selector is not 21h;
a class 5–8 block passes it to its constructor. A weapon whose record has
selector 21h therefore passes the previous mount's value, and before any
definition the frame's residue. In the shipped `dat\WEAPONS.DAT` (SIMVOL0,
SHA-256 `f88ba0a5…af6c`, [dat.md](../formats/dat.md)) only ID 18 (ECM,
class 9) has selector 33 (21h), and class 9 does not pass the local, so
the residue is never consumed with the game's data (read with
`build\claude\h4-96\weapons_dat.py`). The core keeps the local in the
scratch frame; the adapter's zeroed scratch stands in for the residue.

**`player_weapons` 0x4104ec (P, A, count, mounts, items, secondary)**: `PUSH
ECX` reserves [EBP−4] with the entry ECX. `weapon_factory(P, A, count with
secondary's high word, mounts, items, secondary, 1)` (EAX = that count
dword, EDX = A, ECX = P). [P] = vtable `0x499238`; `0x49667c(P+1Fh, 6, 3, 1,
0x411892)` (EAX = P, EDX = P+1Fh, ECX the factory's): three 6-byte groups
whose element constructor clears the dword and counts +1. For k = 0..2:
word [P+1Fh+6k+4] = the count word (`MOV SI`), `new[](2 × sx(count))`
(EAX that size; `POP ECX`), [P+1Fh+6k] = the block. Byte [P+1Dh] = FFh.
For b = 0 .. sx(count)−1 (signed compare): the weapon [[P+4]+4b]; null
skips. Otherwise the record index word [[W+1Ch]+56h] (EDX keeps C's high
word): IDs 12h, 1Dh, 1Eh, 1Fh, 20h (ECM and the four equipment classes,
[weapon_classes.md](weapon_classes.md)) set word [EBP−2] = FFFFh; the others
set it to 0, and the first of them (signed byte [P+1Dh] < 0) is selected:
[P+1Dh] = b. Then for k = 0..2: CX = 1, decremented (32-bit `DEC ECX`)
unless k equals [EBP−2]; word [EBP−4] = b; word [[P+1Fh+6k] + 2b] = CX.
So the weapons are in fire group 0, the equipment in none. ECX before the
loop: P (weapons) or sx(b) (equipment). Then `fire_group(P, 0)` (EAX =
sx(count), EDX = P), byte [P+16h] = 1, count +1. EAX = P, EDX = the counter
pointer, ECX = [EBP−4] (the entry ECX, or its words replaced by the last
non-null weapon's index and flag).

**`weapon_fit` 0x4175dc (A, items, secondary)**: frame `add esp,-12Ch`;
descriptor `0x49a0c0` gives the record at EBP−28h (context word EBP−18h);
`0x49630c` (EAX = the descriptor). `0x420634(A)` gives the mount table M
(count word, mounts pointer at +2); EAX = A+92h, ECX = A (`POP ECX`). The
player byte [A+0A3h] selects: `new(35h)` (EAX = A+92h, ECX = A; `POP ECX`)
into EBP−4 and, when non-null, context 14h, `player_weapons(obj, A, count
word with new's high word, [M+2], items, secondary)` (EAX = that count
dword, EDX = obj, ECX = [M+2]; [M+2] read before [M]), count −6, context 8;
or `new(14h)` into EBP−8, context 2Ch, `weapon_factory(obj, A, …, 0)`, count
−2, context 20h. [A+202h] = the object reread from its slot. A null object
faults later (the count read at `0x4177d0` reads `[00000008h]`): the taken
edges of `0x41761a` and `0x417668` are PROVED unreachable.
`actor_animate(A, 0)`. For i = 0 while i < sx([[A+202h]+8]) ([A+202h]
reread every time): W = [[[A+202h]+4] + 4i], null skips. Otherwise C =
[W+1Ch] into EBP−2Ch, C+12h into EBP−30h, C into EBP−34h (EBP−2Ch reread);
`mount_offset(W)` (EAX = C, EDX = C+12h, ECX = C; `POP ECX`) into EBP−38h;
the index byte b = signed [[W+18h]+17h] gives words b+13h (EBP−3Ah) and
b+0Ch (EBP−3Ch), [W+18h] read for each; word [C+2Ah] into EBP−3Eh. When
[W+10h] ≠ 0: `0x40e61c(W)` (`POP ECX`), its AX into EBP−40h, the shape
virtual +20h of [[A+34h]+4] with that word (EAX = the vtable, EDX =
[A+34h], ECX = the object), then `0x417530([W+10h], word [result+4])`. Then
word [C+18h] = b+13h, word [[C+8]+2] = b+0Ch, byte [C+4] = byte +4 of row
b+13h of the part table [[A+1FAh]] (18-byte rows); `0x40cec0([A+1F6h],
EBP−30h, EBP−38h, 1)` (EAX = EBP−38h, EDX = EBP−30h), `0x40d240([A+1FAh],
EBP−3Ah, EBP−34h, 1)` (EAX = EBP−34h, EDX = EBP−3Ah), `0x40d128([A+1FAh],
EBP−3Ch, EBP−3Eh, 1)` (EAX = EBP−3Eh, EDX = EBP−3Ch); ECX = the first
argument each time. After the loop `0x40cb84([A+1F6h], EBP−12Ch, EBP−0F0h,
EBP−0B4h)` (EAX = EBP−0F0h, EDX = EBP−12Ch) fills the three buffers (30
words, 30 words, 29 dwords) and returns their entry count n, then
`0x40d284([A+1FAh], n, EBP−12Ch, EBP−0F0h, EBP−0B4h)` (EAX = [A+1FAh], EDX
= EBP−12Ch, ECX = EBP−0F0h) reads n entries of each (both callees checked:
entries are written before they are read). For k = 0..28 the condition
word [[A+20Eh]+2k] = 0 when word +6 of part row k is 0 (both pointers
reread per row). Then `0x40fb2c([A+202h], A+307h)` (ECX = 9 × 28 or
[A+20Eh] from the last row), `shield_capacity(A)`, `0x413ac8(A+222h)`
(`POP ECX`), `0x417d08(A)` (ECX = A+222h). EAX = the saved FS:0 head
(EBP−28h), EDX = `0x417d08`'s, ECX = A.

**Frames and shims.** The two framed functions run with the synthetic EBP
of a zeroed scratch (`ES2_WEAPON_FIT_FRAME`, `ES2_WEAPON_FACTORY_FRAME`);
their locals lie at the original offsets because guest callees receive
their addresses (`0x40cec0`, `0x40d240`, `0x40d128`, `0x40cb84`,
`0x40d284`), and the adapters write the saved head back to FS:0
(`weapon_fit`: EAX, `weapon_factory`: ECX). Apart from the factory's
projectile local no function reads an uninitialized local; `player_weapons`
keeps [EBP−4] (the entry ECX) in the core. No entry or exit shim. The
framework passes six argument slots: the factory's seventh slot and the
high word of its caller's EBX reach only slots that no callee reads, and
the adapter reconstructs both as the two original callers have them (the
word = 1 exactly when the player byte [A+0A3h] that chose the caller is
set; EBX = A in both callers). The actor load's later callees may read
stack residue below its frame that the replaced frames leave differently;
none is known (the live regression is the check).

**Compare:** refused for all three (allocation, constructors, the
construction count, the runtime record); the after-load snapshot is the
acceptance.

### In vitro (group `mission_weapons`)

`invitro/mission_weapons_cases.h`: per hook 1,001,024 direct cases in the
full tier (100,000 quick) against logging stubs for every direct callee
(35 stub ids; the shape virtual through an arena vtable), the original
called with seven slots, EBX and a stack filled with a case pattern (the
factory's residue), the core with the same EBX and the residue in its
scratch. Compared: pair, ECX, FS:0, the ordered calls with slots, entry
registers, the record's context word and the frame words that the part
helpers' pointers name (frame addresses logged relative to the record),
the arena and the globals `0x6cbee4`/`0x6c5830`. The stubs change what
the originals read after a call (the system pointer [A+202h] after
`actor_animate`, [W+10h] after `mount_offset`, the frame word EBP−34h after
`0x40cec0`, a record's selector after `weapon_record`, the counter and its
pointer after the constructors), and objects overlap (records' [C+8]
inside the record or the part table, the condition words over the part
table, the factory's slot array over the item words, a fire group array
over the system's selection byte and group 0). In even cases (the real
callers' inputs: EBX = A, the word from the player byte, residue 0) the
adapter is compared too, with the callee counter deltas. 256 installed
31/a7 cases per hook (fast and preserving dispatch; data whose residue is
never consumed). Real-callee passes (2,048 cases per hook): real
`0x49630c`, `0x49667c` and `0x411892`, the nested `player_weapons` and
`weapon_factory` in rotating original/replaced modes, a zeroing bump
allocator for `new`, `new[]` and `0x47a2c8`, constructors that set a
weapon's mount, record and +10h; where the factory runs original under a
replaced caller, the values that carry its EBX's high word are compared as
their low word. 37 branch sites, 35 both ways, the taken edges of
`0x41761a` and `0x417668` PROVED. One caught mutation per hook
(`MUTATIONS.md`). Core unit test `//core:mission_load_test` (new scenarios
worked out by hand: the player's fire groups; the factory with an energy
weapon, a negative item and an ELF that reuses the previous projectile
record, then an ECM with a failed allocation and an ID above 20h; the fit
of an AI mech and of the player).

### Live counts

`weapon_fit` runs once per mech constructed in pass 2 (the selected
authored mechs plus the MEC actors), so its count is the sum of section
14's `initial_damage` and `condition_init` columns (both confirmed live in
`check-h495` for c1_01-success and c3_10); `player_weapons` once (the
player's MEC actor; hypothesis: the only actor with byte A+0A3h set before
its fit, since pool_create clears the MECH pool and pass 2 sets the byte
only for the player);
`weapon_factory` once per fitted mech (directly for the others, through
`player_weapons` for the player):

| Counter | a60 / c1s / c1c | c3_09 | c4_08 | c3_10 | c1_02 | c5_05 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `hk_weapon_fit_calls` | 10 | 16 | 11 | 16 | 13 | 15 |
| `hk_player_weapons_calls` | 1 | 1 | 1 | 1 | 1 | 1 |
| `hk_weapon_factory_calls` | 10 | 16 | 11 | 16 | 13 | 15 |
| `hk_weapon_fit_call_4104ec` | 1 | 1 | 1 | 1 | 1 | 1 |
| `hk_weapon_fit_call_40fff8` | 9 | 15 | 10 | 15 | 12 | 14 |

Per call: `weapon_fit` calls `49630c`, `420634`, `47749c`, `402604`,
`40cb84`, `40d284`, `40fb2c`, `417bec`, `413ac8` and `417d08` once each;
`weapon_factory` calls `49630c`, `4774f0`, `474bbc` and `474be4` once each;
`player_weapons` calls `40fff8`, `49667c` and `410ae4` once and `4774f0`
three times. The per-weapon counters (`weapon_fit`: `40f540`, `40cec0`,
`40d240`, `40d128`, and for weapons with W+10h set `40e61c`, `slot_20`,
`417530`; `weapon_factory`: `40fe84`, `40ffc8`/`40ffb0`, `47a2c8` and the
constructors) depend on the mount tables and loadouts and were not
predicted. Every `*_skipped`/`*_bypassed` 0. The hook counters of
`actor_animate`, `mount_offset`, `fire_group`, `shield_capacity`,
`weapon_record`, `def_lookup` and `proj_record` are unchanged (the bridge
still reaches them).

### After-load snapshot reach

A difference caused by these hooks would lie in a block reached through an
actor's weapon system [A+202h] (the system, its slot array, the weapon
objects, the three fire-group arrays at system+1Fh), in the weapon records
(`[0x4a98ec]`: the words +18h and [C+8]+2 and the byte +4 that the fit
writes), in an actor's condition words [A+20Eh], or in the block pairing
(sizes or walk order): the hooks allocate the same sizes in the same order
through the same `new`, `new[]` and `0x47a2c8`.

### Live result

`check-h496` (all 513 hooks replaced, the 49 shim imports
replaced, five clones): **40 of 40 suites identical**, the DBSIM exit state
equal to the golden's in all 40, every skipped/bypassed/mismatch counter 0.
The counters equal the prediction in every suite: `hk_weapon_fit_calls` =
`hk_weapon_factory_calls` = `hk_initial_damage_calls` +
`hk_condition_init_calls` (10 in the C1_01 suites, 16 in C3_09/C3_10, 11 in
C4_08, 13 in c1_02-probe and c4_05-convoy, 15 in C5_05, 8 in the C1_08
suites, 9 in c1_01-unarmed-keys and c4_10-probe, 16 in c1_06-cheat-disable,
11 in c2_02-identify, 1 in train1-timers); `hk_player_weapons_calls` = 1 and
`hk_weapon_fit_call_4104ec` = 1 everywhere (the hypothesis that only the
player has `A+0A3h` set before its fit holds in all 40 suites);
`hk_weapon_fit_call_40fff8` = calls − 1.

### Open

- The per-weapon callee counts per suite (mount tables and loadouts).
## 17. L5 constructors

L5 of section 10, read from the pinned `llvm-objdump -d -M intel` listing
of the reference DBSIM, decoded afresh from the entry where the linear
sweep is misaligned (the factory's case at `0x4053bc`, `0x413a90`); the
exception descriptors, the factory's case byte table `0x405366` and jump
table `0x4053a4` were read from the file. Own code: adapter group
`mission_ctors`, core appended to `core/src/mission_load.c`; the closest
earlier hooks are their callees. **Confirmed live:** `check-r7-3b`;
after-load snapshot `snapshot-r7b-20261013` (section 14). Evidence
(scripts and logs, no listings): `build\claude\h4-97\`.

| Hook | VA | Call site in the actor load `0x4253d8` | Builds |
|---|---|---|---|
| `mech_ctor` | `0x415bb0` | `0x4256a4`, once per constructed mech (authored and MEC) | the MECH object in its pool node |
| `flyer_ctor` | `0x4215f4` | `0x425a71`, once per selected flyer | the FLYER object |
| `building_factory` | `0x405314` | `0x425c90`, once per selected building | the building-pool object of the record's class |
| `group_ctor` | `0x423b34` | `0x4261e5`, once per script group | the runtime group, its member array and the members' placement |
| `base_marks` | `0x405db0` | `0x426231`, once per base group | the terrain marks of the base layout |

Computed in the core (no longer called when the hooks are replaced): the
type-index leaves `0x41ff00` (MECH, byte table `0x4a9d08`) and `0x422e80`
(FLYER, `0x4a9dcc`), the actor-list append `0x411dd4` (its only callers
are these constructors) and the energy-record leaf `0x413a90` (only caller
`mech_ctor`). Everything else stays game code behind the guest bridge with
its own mode: the shape set-ups `0x4033a4`/`0x40332c`/`0x403368`
(`TS_OBJ`/`ANIM_OBJ`/`DET_ANIM_OBJ` and `object_init`), the part tables
`0x40d2cc`/`0x40d354`/`0x40d160`/`0x40cd88`, the name builder `0x492bec`,
the animation tracks `0x402374`, the group join methods
`0x417aa8`/`0x421ee8`/`0x405c3c`, the terrain marks `0x47136c`, the runtime
(`0x49630c`, new[] `0x4774f0`, `0x49667c`, memset `0x49681a`) and the hooks
`pool_alloc`, `object_init`, `script_start`, `euler_matrix`,
`matrix_transpose`, `rotate_point`, `track_rate`, `track_seek` and
`pool_nth`.

### Call path and live visibility

Each function has its one call site above and no other reference (no dword
of the file holds its address). With early arming (section 13, the
default) the hooks are installed at `0x45f42b`, before the load, so their
counters are constant from the first tick record on; the DBSIM map (before
early arming) reads 0 for all five.

### Who places MEC actors (section 12, answered)

**The group constructor of group 0, through the MECH join method.** The
actor load gives every MEC actor the load's defaults: position (0, 0,
−32768) from the statics `0x49a871..79` and angle words (0x8000, 0x8000,
0) from `0x49a87e..82` (section 3). It rewrites group 0's 20 roster slots
before construction (`0x426024..0x42604f`): slot k < N becomes the remap
index of MEC actor k (`0x4aa78e` + M + k), the others −1. `group_ctor`
resolves each non-negative slot with `pool_nth` in the class's pool and
calls the class's join method with the member, the group and the slot k.
The MECH join `0x417aa8` (FLYER `0x421ee8` and building `0x405c3c` are
built the same way) stores the group at A+45h and the slot word at A+49h;
when the first angle word A+0Ch is still 0x8000 the angles become (0, 0,
G+0Ah), the group's heading word; when the height A+2Eh is still −32768 the
position becomes the group's point G+18h or, without one,
`route_point(G+4, 0)` (the first point of the first order's route, which
`group_ctor` stores at G+6), its z the `terrain_height` there plus the type
word +16h (and 5000 for a type with word +50h), moved by the formation slot
virtual +78h (`formation_mech` `0x417898`) for slot k, and stored at A+26h.
The mission load's height fix (`0x4618bc`, section 1) later recomputes z;
so the actors are placed during the load, before the first frame. The
group's heading word and point are
built by the actor load: the angle of group record word +2Eh, else the
`heading_point` of the first two points of the first order's route, else
0; the coordinate of record word +2Ch, else none.

**The same placement serves the authored actors.** In all 17 launch sets
(`build\claude\h4-97\ctor_counts.py`) every selected authored mech and
flyer has coordinate index −1 and angle index −1 (record words +3Eh/+40h
for mechs, +28h/+2Ah for flyers), so every mech and flyer starts at the
defaults and is placed by its group; group 0's class word is 0 in every
set.

### Contracts (from the bytes)

All five are cdecl; EDX:EAX is reproduced exactly; ECX is dead after every
call site (forward walk of both arms of every branch from `0x4256a9`,
`0x425a76`, `0x425c95`, `0x4261ea`, `0x426236`; after `0x4261ea` only CX
is read, by `TEST CX,CX` after `MOV CX` at `0x426237`), so the framework's
entry ECX stands in for it. Game calls go through the guest bridge with the
original's entry EAX/EDX/ECX (the core follows them step by step; the in
vitro logs compare every one); no callee writes an argument slot that is
read back (`0x49667c` decrements its count slot, which the callers release
with `ADD ESP`). No function reads a local it did not write, and neither do
their guest callees, apart from the runtime records' heads and the stream
object at EBP−30h that `0x40d160`/`0x40cd88` construct through `0x472f14`
(linear scan, `build\claude\h4-97\residue.py`). "Count ±n" means
`[[0x6cbee4]] ± n`, the pointer reread each time.

**`mech_ctor` 0x415bb0 (A, type)**, frame `add esp,-58h`: descriptor
`0x49a080` gives the runtime record at EBP−24h (context word EBP−14h);
`0x49630c` (EAX the descriptor, the entry EDX/ECX). Only the type word
counts: index i = byte [`0x4a9d08` + sx(type)], type record T =
[`0x4a9d64`] + 21Eh × i (both reread for the second lookup at
`0x415cce`). In order:

1. `0x4033a4(A, T+0DEh)`: EAX = T+0DEh, EDX = i, ECX = the dword pushed
   with the type word (`MOV SI,[EBP+0Ch]; PUSH ESI; …; POP ECX`): its high
   word is the caller's ESI. `0x4033a4` passes that ECX to `0x402278`,
   which keeps it through `0x49630c` and clears it (`0x402293`) before any
   read, so it is dead.
2. [A] = `ACTOR` `0x49a54c`, EBP−28h = A+4Dh (dead), word +1A8h = FFFFh,
   bytes +98h, +0A7h = 1; the actor list `0x4a9af8` (`0x411dd4`: the array
   [L] at the signed count word L+6 = A, the count reread, incremented, and
   stored at A+4Bh; EAX = ECX = the first count's sign extension with the
   reread low word, EDX = A); dwords +1B6h, +1B2h = 0; words +1BEh, +1BCh,
   +1BAh = FFFFh; count +1; [A] = `MECH` `0x49a282`.
3. `0x40d2cc(A+206h, 1Dh, 16h)` (EAX = −1, EDX = the counter pointer, ECX
   = the list append's); `0x49667c(A+258h, 3, 5, 1, 0x41c03e)` (EAX =
   A+23Eh, EDX = A+258h).
4. Dead stores EBP−2Ch .. EBP−3Ch; words +29Ch = 0, +4 = 7; the second
   lookup into [A+1F2h] (ECX = 0); words +29Ah, +298h, +296h, +294h, +290h,
   +28Eh, +28Ch = 0, +292h = 10000, +1A8h = 0. `0x413a90(A+222h, word
   T+0C0h)` (computed): words +228h = +22Ah = w, +226h = 200h, +224h =
   +222h = sx(w) SAR 1; EAX = A+222h, EDX = A+222h's high word : w, ECX =
   sx(w) SAR 1. Words +2AAh, +28Eh, +252h = 0; when word T+54h is 0, byte
   +0B3h = 1 (through EBP−40h; ECX = A+92h).
5. The animation tracks of the signed type words +12h, +1Ch, +24h:
   negative stores 0 (and clears EAX, EAX, ECX respectively), otherwise
   `0x402374(A, word)` (the word in the low half of EDX, EDX, EAX; the
   other registers as left) into +22Ch, +230h, +234h. After the first,
   word +6 of the shape nodes of the type words +12h and +14h is cleared
   ([[[[A+34h]+4]+2Ah]+6] + 4 × word, the chain reread for each).
6. The flat objects: the count word +23Ch counts the non-negative bytes
   from T+72h (reread and incremented in place); when nonzero, new[](4 ×
   count) (EAX = the count, EDX = the terminating byte + 1, ECX = the size)
   into +238h, and per k (DI, signed word compare with the reread count):
   `pool_alloc([0x4a968d])` into EBP−4 (copy EBP−44h; `POP ECX` = the
   pool); a null object faults at `0x415efb` (PROVED edge of `0x415e8b`);
   context 14h, `object_init(obj, [[0x4a9698] + 4 × byte])` (EAX = the
   table, EDX = the shape, ECX = obj), vtable `0x49a262`, word +4 = 9,
   count +1 −4, context 8; [array + 4k] = obj, reread, [obj+2Eh] =
   −100000.
7. The AI state through `script_start(A+4Dh, state)`: for the player (byte
   A+0A3h) `0x499420` ("player fly", type word +50h ≠ 0) or `0x4993e2`
   ("player"), else `0x4993a4` ("deciding"); the registers of each branch
   are in the core.
8. Words +29Eh, +23Eh = 0; the name [[`0x4a9d60`] + 4 × sx(word
   [A+1F2h]+6Ch)] with the suffix [`0x4a0a04`] to `0x492bec`, its buffer
   to `0x40d160` → +1FAh; with [`0x4a0a08`] to `0x492bec` and `0x40cd88` →
   +1F6h; `0x40d354(A+206h, A, [A+1FAh], the +1F6h value)`.
9. Bytes +2B7h .. +2B4h = 0, +2B8h = 1; `memset(A+2B9h, 0, 4Eh)`; +2D9h =
   A+0Ch; when word A+32h is 0 (always in the game: `0x402278` clears it)
   `euler_matrix(A+0Ch, A+12h)` and word +32h = 1; the matrix A+12h (five
   dwords, `REP MOVSD`) and the translation A+26h (+8, +0, +4) copied to
   A+2DDh, `matrix_transpose(A+2DDh)`, the copied translation negated (+8,
   +0, +4) and staged at EBP−58h, `rotate_point(EBP−58h, A+2DDh,
   A+2F1h)` (EAX/ECX the staged +8/+4 dwords, EDX = EBP−58h).
10. +2BDh = 1000, word +2D7h = word +290h, word +31Ch = 0, count +1. EAX =
    A, EDX = the counter pointer, ECX = the saved FS:0 head, which the
    caller restores. The caller subtracts 7 from the count afterwards
    (`0x4256b1`).

**`flyer_ctor` 0x4215f4 (F, type)**, frame `add esp,-0Ch` (the vector), no
runtime record. Index i = byte [`0x4a9dcc` + sx(type)], record T =
[`0x4a9df4`] + 70h × i (both reread for the second lookup at `0x4216b7`).
`0x403368(F, [T+36h])` (EAX = the shape, EDX = 14 × i, ECX =
[`0x4a9df4`]); `ACTOR`, +1A8h = FFFFh, bytes +98h/+0A7h, the actor list,
+1B6h/+1B2h = 0, +1BEh/+1BCh/+1BAh = FFFFh, count +1, `FLYER`
`0x49a5e0`; `0x40d2cc(F+200h, 1, 1)` (EAX = −1, EDX = the counter pointer,
ECX = the list append's); word +4 = 6; the second lookup (EDX = F+267h)
into +1FAh; +1A8h = 2, +235h = +231h = 0, +233h = word T+4;
`0x40d354(F+200h, F, [T+2Eh], [T+32h])` (EAX = [T+2Eh], EDX = F+200h, ECX
= [F+1FAh]; [F+1FAh] read before each of the two fields);
`script_start(F+4Dh, 0x499cf8)` ("deciding"; ECX = F+4Dh);
`memset(F+243h, 0, 4Eh)`; +263h = F+0Ch; the matrix part as in
`mech_ctor` step 9 at F+267h with the vector at EBP−0Ch; +247h = 1000,
word +261h = 200h, count +1. EAX = F, EDX/ECX = `rotate_point`'s.

**`building_factory` 0x405314 (record, byte)**, frame `add esp,-0A4h`:
descriptor `0x497556` gives the record at EBP−34h (context word
EBP−24h). The second slot (the byte from `0x4aa9d2`) is never read. T =
[`0x4a959c`] + 3Ch × sx(type word); EBP−38h = [`0x4a9574`] (first shape
table), ESI = [`0x4a957c`] (second). Types 0..3Dh select a case through
the byte table `0x405366` (62 bytes) and the jump table `0x4053a4`; above
3Dh (unsigned, so also negative types) case 0.

| Case | Code | Classes after `ACTOR` | Slot | Shape (table, set-up) | Context in/out | Count after the BASE part |
|---|---|---|---|---|---|---|
| 5 | `0x4053bc` | `BASE`, `RADAR_BASE` `0x4979d4` | EBP−4 | first, `0x40332c` | 14h/8 | +1 −8; bytes +96h, +0A5h = 1 (also on the null path, which faults) |
| 3 | `0x4054ea` | `BASE`, `GUN_BASE` `0x4978ac` | EBP−8 | first, `0x40332c` | 2Ch/20h | +1 −8 |
| 2 | `0x40560d` | `BASE` | EBP−0Ch | second, `0x403368` | 44h/38h | −7; byte +0A5h = 1 (faults when null) |
| 1 | `0x40572d` | `BASE`, `GUN_BASE`, `VEHICLE` `0x497818` (word +4 = 0Ah, +1A8h = 3) | EBP−10h | second, `0x403368` | 5Ch/50h | +1 +1 −9 |
| 4 | `0x405877` | `BASE`, `LC_BASE` `0x497784`, `0x49667c(obj+209h, 1Fh, 3, 1, 0x4069c5)` (EAX = the counter pointer, EDX = obj, ECX = obj+209h) | EBP−14h | second, `0x403368` | 74h/68h | +1 −8 |
| 0 | `0x4059ca` | none: types 10, 53, 54, above 3Dh; EDI stays the caller's | — | — | — | — |

Each class case calls `pool_alloc([0x4a95a0])` (the pool in EAX for cases
5, 2, 4, in EDX for 3, in ECX for 1; EAX otherwise the case byte, EDX the
first table, ECX the entry's), reads the shape of type word +2 from its
table, calls the set-up (EAX = sx(word) for case 5, the shape otherwise;
EDX = the table for case 5, the object otherwise; ECX = the object for
case 5, the shape otherwise) and builds the `ACTOR`/`BASE` part (`ACTOR`,
+1A8h = FFFFh, +98h/+0A7h through two dead locals, the actor list,
+1B6h/+1B2h = 0, +1BEh/+1BCh/+1BAh = FFFFh, count +1, `BASE`, word +4 =
5, +1A8h = 1, count +1). A null object faults at its first access in the
original (PROVED edges `0x4053cf`, `0x4054fe`, `0x405624`, `0x405745`,
`0x405894`). The tail (object EDI): for i = 0, 1 while i < sx(word T+6),
`0x402374(obj, i)` into obj+1F9h + 4i (EAX/EDX first T+20h and obj+1F9h,
ECX = sx(T+6)), `track_rate(track, word T+20h+2i)` (the word in the low
half of `0x402374`'s ECX; EAX = the slot address) and for i = 1
`track_seek(track, 1, 6000)`; when word T+1Eh is nonzero, byte +0B7h = 1;
new[](2 × sx(T+12h)) into +201h, every word 1; `0x49667c(0, 0Bh, sx(T+12h),
1, 0x4069e5)` into +205h (the part records); per part i (configuration
[T+14h], stride 1Eh; records stride 0Bh) word +5 = 0 and word +0 from the
record's word +32h (w): negative 0; 0 the configuration word +0 (and frame
word 1 at [[obj+34h]+8] + 2 × the configuration word +2 when that is not
negative); positive (100 − w) × the configuration word / 100 (the
product's low dword, `CDQ`, `IDIV`). When w = 0: bytes +99h = 1, +96h = 0
and, for a non-negative type word +4, [[obj+34h]+4] = [[`0x4a9584`] + 4
× that word] (the model). Word +1F7h = word T+26h, +1F2h = T. EAX = the
object, EDX = the saved FS:0 head (the caller restores it), ECX = the
record or [`0x4a9584`].

**`group_ctor` 0x423b34 (G, R, trigger, orders, heading, point)**, frame
`add esp,-10h` (the roster cursor and the member counters and slot words,
which reach no callee). Word G = class word R+28h, byte G+12h = R+6Eh,
word G+0Ah = the heading word (low word of slot 5), word G+2 = R+2Ah, word
G+10h = 0, G+14h = trigger, G+18h = point, G+6Ch = 0, byte G+13h = 0,
`memset(G+70h, 0, 0Ah)` (EAX = point, EDX = 0, ECX = G+70h); ten words
R+72h.. to G+1Ch.. and R+86h.. to G+30h.., interleaved; the ten order
pointers to G+44h; when the first is not null, G+6 = its route (+8) and
word G+4 = 0. The member count G+10h counts the non-negative roster words
R+32h .. R+58h; `new[](4 × count)` into G+0Ch (EAX = 20, EDX = R+58h, ECX
= the size). The class decides (`SUB 1; JB`, `JE`, `DEC; JE`): 0 the MECH
pool [`0x4a9b7a`] and `0x417aa8`, 1 the FLYER pool [`0x4a9db9`] and
`0x421ee8`, 2 the building pool [`0x4a95a0`] and `0x405c3c`; any other
class builds no member. Per slot k = 0 .. 19 (DI) with a non-negative
word: `pool_nth(pool, word)` (EAX and EDX keep their high words and take
the slot word, ECX = the pool), [[G+0Ch] + 4 × count] = the object, count
+1, the join `(object, G, k)` (EAX = the object, EDX = the count before
the increment, ECX = [G+0Ch]); the building join also receives sx(word R)
(EDX = R, ECX = that). EAX/EDX/ECX are those of the last step.

**`base_marks` 0x405db0 (B)**: the layout record L = [`0x4a95ac`] + 1Ch ×
sx(word [[B+45h]+2]) (the building's group, the group's word +2 = the
record's word +2Ah); when [L+8] is −1, EAX = L, EDX = B, ECX = −1;
otherwise `0x47136c([0x4a0be0], B+26h, [L+8], [L+18h], [L+14h])` (EAX =
the terrain, EDX = B+26h, ECX = [L+8]; reads +8, +14h, +18h, then the
terrain) and its registers. B = 0 (a base group without members) faults
in the original; the game data has none.

**Frames, invisible registers, shims.** `mech_ctor` and
`building_factory` run with the synthetic EBP of a zeroed scratch
(`ES2_MECH_CTOR_FRAME`, `ES2_BUILDING_FACTORY_FRAME`; `0x49630c` through
`weapons_impact_guest_call`), and their adapters write the saved head back
to FS:0 (`mech_ctor`: ECX, `building_factory`: EDX); `flyer_ctor`'s
scratch holds the vector `rotate_point` reads. Two values come from the
callers' callee-saved registers, which an adapter cannot see: (a)
`mech_ctor`'s pushed type dword carries the caller's ESI high word (the
previous mech of the same pool block, or the actor load's entry ESI for
the first); `0x41ff00` reads only the low word and the dword reaches only
`0x4033a4`'s entry ECX, which is dead (above); the adapter passes ESI = A.
(b) `building_factory`'s case 0 builds no object and works on the
caller's EDI (in the actor load the selection-flag pointer `0x4aa8ba` +
i), returning it as the "object"; no launch set contains such a type (all
17 sets: cases 1 to 5 only), and the adapter passes EDI = 0, so a replaced
case 0 faults at its first object access instead of writing into the
selection tables. The cores take both values as arguments, so the in-vitro
direct cases reproduce the originals exactly. No entry or exit shim.

**Compare:** refused for all five (allocation, constructors, the
construction count, the runtime record, game calls that write the actors
and the terrain); the after-load snapshot is the acceptance.

### In vitro (group `mission_ctors`)

`invitro/mission_ctors_cases.h`: per hook 100,000 direct cases in the
quick tier (1,001,024 in the full tier, not run) against logging stubs for every game callee (25 stub ids;
memset is the harness's own; the four computed leaves run as original code
inside the original): the original called with six slots, EAX/EDX/ECX,
EBX/ESI/EDI and a stack filled with a case pattern, the core with the same
ESI (`mech_ctor`) and EDI (factory) and the ECX it reports. Compared: the
pair, ECX, FS:0, the ordered calls with argument slots (and the bridge's
slot count), entry registers (stack addresses masked) and the record's
context word, the vector `rotate_point` reads by content, the 16 KiB
arena and the image globals (the actor list header and the dword a count
store may reach, the counter pointer, the MECH and FLYER type tables and
their index-byte windows, the name, shape and suffix pointers, the
building tables and pools, the base layouts and terrain pointers,
`script_start`'s sequence word). The stubs change what the originals
reread (the type index byte and table after `0x4033a4`/`0x403368`, the
counter pointer, the type words after `0x402374`, the flat count, array
and shape table after `pool_alloc`, the type record pointer after
`script_start`, the matrix after `euler_matrix`, the translation after
`matrix_transpose`, the part count after new[], the group class after
new[], the member array after `pool_nth` and the joins), and the actor
list's store can hit its own count word. In even cases (the real callers'
inputs: ESI = A, no factory case 0) the adapter is compared too, with the
callee counter deltas. 256 installed 31/a7 cases per hook. Real-callee
passes (4,096 cases per hook except `base_marks`, which has no nested
hook): `mech_ctor` over the real `0x49630c`, `pool_alloc` (a real flat
pool), `object_init`, `script_start`, `euler_matrix`,
`matrix_transpose`, `rotate_point`; `flyer_ctor` over the real
`0x403368`, `object_init`, `script_start` and the matrix hooks;
`building_factory` over the real `0x49630c`, `pool_alloc` (a real
building pool), `0x49667c` with its element constructors and both shape
set-ups (`0x402278`, `object_init`); `group_ctor` over the real
`pool_nth`, `pool_prev` and `pool_next` on three real pools; the nested
hooks in all original/replaced combinations, the hook under test original
or replaced; `0x402374`, `track_rate` and `track_seek` stay stubs (they
need real track objects). 44 branch sites, 38 both ways, the taken edges
of `0x415e8b`, `0x4053cf`, `0x4054fe`, `0x405624`, `0x405745` and
`0x405894` PROVED (a null object faults). One caught mutation per hook
(`MUTATIONS.md`); seven of eight ad-hoc mutants of the core caught (the
eighth, a zero- instead of sign-extended member count, is equivalent: the
count never reaches 8000h). Core unit test `//core:mission_load_test`
(scenarios worked out by hand: an AI mech with one flat object and one
track, the flying player with three tracks; a flyer; an LC_BASE with two
tracks and two damaged parts, a BASE at full condition with a frame word
and a model, a case-0 type; groups of class 0, 2 and 3; both
`base_marks` paths).

### Live counts

From the launch sets (`build\claude\h4-97\ctor_counts.py`, the selection
rule of section 2; `mech_ctor` = the selected authored mechs + the MEC
records, which equals `weapon_fit` of section 16, confirmed in
`check-h496` for every suite). The suites without a launch set use the
installed C1_01 mission (the lists of `c1_01-unarmed`, two MEC records):

| Suites (launch set) | `mech_ctor` | `flyer_ctor` | `building_factory` | `group_ctor` | `base_marks` |
|---|---:|---:|---:|---:|---:|
| the 19 suites on the installed C1_01 (acceptance-60s, c1_01-success, -collision, -cockpit-keys, …) | 10 | 4 | 50 | 38 | 2 |
| c1_01-unarmed-keys | 9 | 4 | 50 | 38 | 2 |
| c1_02-probe | 13 | 2 | 50 | 36 | 2 |
| c1_06-cheat-disable | 16 | 4 | 54 | 40 | 2 |
| c1_08-escort-route, c1_08-scan | 8 | 4 | 4 | 12 | 0 |
| c2_02-identify | 11 | 4 | 15 | 11 | 1 |
| c3_09-loadout-b, c3_09-wingman-orders, c3_09-orders-matrix | 16 | 0 | 32 | 21 | 10 |
| c3_10-loadout-hercs, -own-building-fire, -detail-options, c3_10-hunt-targ | 16 | 0 | 25 | 21 | 1 |
| c4_05-convoy | 13 | 2 | 2 | 12 | 0 |
| c4_08-razor-plasma, c4_08-slope-dive, c4_08-joystick | 11 | 1 | 27 | 15 | 1 |
| c4_10-probe | 9 | 2 | 24 | 13 | 1 |
| c5_05-moon-missiles, c5_05-orders-flyer | 15 | 2 | 31 | 29 | 1 |
| train1-timers | 1 | 0 | 11 | 3 | 1 |

Measured: the DBSIM map re-run `dbsim-map-20261012` (every hook original, early arming, all 40 suites) has exactly these entry
counts for the five functions in every suite
(`build\claude\h4-97\map_check.py`). That confirms the counts of the
original, not the hooks; their counters are confirmed by the live result below.

Callee counters per call: `mech_ctor` calls `49630c`, `4033a4`, `40d2cc`,
`49667c`, `413e50`, `40d160`, `40cd88`, `40d354`, `49681a`, `47ebb8`,
`47df19` and `480304` once and `492bec` twice (`hk_mech_ctor_call_<va>` =
the call count, `_call_492bec` twice it); `flyer_ctor` calls each of its
eight callees once; `building_factory` calls `49630c`, `4774f0` once and
`49667c` once plus once per LC_BASE, `471b30` once, `40332c` for cases
5/3 and `403368` for cases 2/1/4; `group_ctor` calls `49681a` and `4774f0`
once and `471cb0` once per member, split over the joins by class:

| Counter | installed C1_01 | c1_01-unarmed | c1_02 | c1_06 | c1_08 | c2_02 | c3_09 | c3_10 | c4_05 | c4_08 | c4_10 | c5_05 | train1 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `hk_group_ctor_call_471cb0` | 64 | 63 | 65 | 74 | 16 | 30 | 48 | 41 | 17 | 39 | 35 | 48 | 12 |
| `_call_417aa8` / `_call_421ee8` / `_call_405c3c` | 10/4/50 | 9/4/50 | 13/2/50 | 16/4/54 | 8/4/4 | 11/4/15 | 16/0/32 | 16/0/25 | 13/2/2 | 11/1/27 | 9/2/24 | 15/2/31 | 1/0/11 |
| `hk_building_factory_call_40332c` / `_403368` | 15/35 | 15/35 | 15/35 | 15/39 | 4/0 | 5/10 | 15/17 | 18/7 | 2/0 | 12/15 | 12/12 | 21/10 | 4/7 |
| `hk_building_factory_call_49667c` | 50 | 50 | 50 | 54 | 4 | 15 | 32 | 25 | 2 | 33 | 30 | 34 | 11 |
| `hk_base_marks_call_47136c` | 2 | 2 | 2 | 2 | 0 | 1 | 2 | 1 | 0 | 1 | 1 | 1 | 1 |

The same map confirms the rows whose callees have no other caller in the
listing: `0x417aa8`, `0x421ee8`, `0x405c3c` (only `group_ctor`),
`0x40332c` (only `building_factory`), `0x403368` (`building_factory` and
`flyer_ctor`: the map count is the row plus `flyer_ctor`), `0x413a90`
(only `mech_ctor`) and `0x411dd4` (the three constructors; its counts also
equal the `_call_471cb0` row). `0x47136c`'s only caller
is `base_marks` (`0x405de9`, at most once per call), so its map counts are
the `_call_47136c` row. `0x471cb0` has three more call sites
(`0x42538c`..`0x4253c5`), so its map counts are not the row.

The other type-dependent counters (`mech_ctor`: `402374` 0..3 per mech,
`4774f0` per mech with flat objects, `471b30` = `402188` per flat object;
`building_factory`: `402374`/`4796d8` per track, `479344` per type with two
tracks) depend on `mforms.dat` and `bforms.dat` and were not predicted;
their callees have other callers, so the map totals do not split. Every
`*_skipped`/`*_bypassed` 0. The hook counters of `pool_alloc`,
`object_init`, `script_start`, `euler_matrix`, `matrix_transpose`,
`rotate_point`, `track_rate`, `track_seek`, `pool_nth`, `route_point`,
`terrain_height` and `formation_*` are unchanged (the bridge still reaches
them).

### After-load snapshot reach

A difference caused by these hooks would lie in an actor pool block (MECH
[`0x4a9b7a`], FLYER [`0x4a9db9`], buildings [`0x4a95a0`]), the actor list
`0x4a9af8`, the flat-object pool [`0x4a968d`], the group array
[`0x4a9e00`] and its member arrays, the blocks the constructors allocate
(flat arrays, part words and records, the tracks reached through
[A+34h]), the terrain cells, or in the block pairing (sizes or walk
order): the hooks allocate the same sizes in the same order through the
same callees.

### Live result

`check-r7-3b` (all 603 hooks replaced, the 49 shim imports
replaced, five clones): **40 of 40 suites identical**, exit states equal
to the goldens', every skipped/bypassed/mismatch counter 0.
`hk_mech_ctor_calls`/`flyer_ctor`/`building_factory`/`group_ctor`/`base_marks`
equal the launch-set prediction of the table above in every suite. The
after-load snapshot `snapshot-r7b-20261013` (section 14) covers this piece
and section 16.

### Open

- The type-dependent callee counts (tracks, flat objects, part records)
  per suite.
- `0x402374`, `track_rate` and `track_seek` are stubs in the real-callee
  passes; their nesting under these hooks is checked live only.
- The join methods `0x417aa8`, `0x421ee8`, `0x405c3c` and the terrain marks
  `0x47136c` are own code in sections 23 and 27.
## 18. Compare mode for six L2 leaves

`pool_nth`, `subject_resolve`, `action_copy`, `condition_init`,
`region_init` and `timer_init` accept `compare` (details and the in-vitro
probes in
[compare_mode.md](../reference-game/compare_mode.md#mission-load-weapon-slot-and-weapon-class-leaves)).
The declarations follow the contracts of section 14: `action_copy` a span
over A+1BAh..1E1h (not the actor pools as regions, as section 10
proposed: the span clones exactly the bytes it writes), `region_init`
R+0..9, `timer_init` T+0..30h (timer_arm's dword at T+2Dh included; the
nested `timer_arm` runs on the clone), `condition_init` the two tables
behind [C+4] (29 words) and [C] (22 words) as one-hop regions isolated in
place (so the overlapping layouts of the in-vitro cases stay comparable);
`pool_nth` and `subject_resolve` declare nothing (their only stores are
the argument slot and the iterators' frames). Registers that hold an
address in a span (A+1CEh, R, T+2Ch) are compared relative to it.
`pool_create`, `group_array` and `initial_damage` keep their refusal.

### Live result

`check-c06` (every hook replaced except the 18 hooks of
compare_mode.md in `compare`, own runtime): **40 of 40 identical**, every
`hk_<h>_mismatches` 0. Calls as predicted (pool_nth 66, subject_resolve 15,
action_copy 62, condition_init 2, region_init 8 in the C1_01 suites;
timer_init 3 in the C3_10 suites, 14 in train1-timers). Nonzero only the
expected compare mechanics: `bypassed` for `pool_nth`, `pool_prev`,
`pool_next`, `timer_arm` and `weapon_ammo` called inside the original phase
of a compared caller, `skipped` for `weapon_ammo`/`gun_ammo` (null p) and
`beam_ready` (up to 5,251 of 64,262 calls in c3_09-loadout-b, reason not
traced; the compared calls stay above 0 in every suite that reaches it).
## 19. Object shape set-up

Read from the pinned `llvm-objdump -d -M intel` listing of the reference
DBSIM; the exception descriptors `0x497145` and `0x497175` (record at
EBP−24h) were read from the file. Own code: the nine functions as adapter
group `object_shapes`, core module `core/src/object_shapes.c` (the name
avoids "setup", which Windows' installer heuristic refuses in test
executables); before, section 17's constructors reached them through the
guest bridge. **Confirmed live:** `check-r7-4`; after-load snapshot
`snapshot-r7c-20261013` (section 14). Evidence (scripts and logs, no
listings): `build\claude\h4-108\`.

| Hook | VA | Role | Callers |
|---|---|---|---|
| `ts_obj_ctor` | `0x402278` | `TS_OBJ` base constructor with the animated shape instance | `0x40332c`, `0x4033a4` |
| `det_anim_shape` | `0x40332c` | `DET_ANIM_OBJ` over `ts_obj_ctor` (the shape) | `building_factory` cases 5, 3 |
| `det_anim_static` | `0x403368` | `DET_ANIM_OBJ` over `object_init` | `building_factory` cases 2, 1, 4; `flyer_ctor` |
| `det_anim_record` | `0x4033a4` | `DET_ANIM_OBJ` over `ts_obj_ctor` (the shape of a record) | `mech_ctor` |
| `anim_instance_ctor` | `0x478838` | the 42h-byte animated shape instance | `ts_obj_ctor` |
| `object_track` | `0x402374` | an animation thread of an object | `building_factory`, `mech_ctor` (three sites), the weapon set-up `0x40df30` (two) |
| `thread_add` | `0x478a3c` | a thread into the instance's ten slots | `object_track` |
| `thread_ctor` | `0x478e48` | the animation-thread constructor | `object_track` |
| `shape_word_block` | `0x402fc0` | a copy of a shape's word array | `0x40df30`, `0x42067c`; `0x4031d4` (no caller) |

A byte scan finds no dword of the file that holds any of the nine entries;
the call sites in the table are all callers. Correction for the verdict row of
function_verdicts.md: `0x40332c` stores `ANIM_OBJ` and then `DET_ANIM_OBJ`, like
`0x403368` and `0x4033a4`, so all three are `DET_ANIM_OBJ` constructors
([vtables.md](vtables.md) §1.1 already says so).

The callees stay game code behind the bridge with their own modes: the
group's own hooks (each calls the others through the bridge), `object_init`,
`shape_instance`, `node_matrices`, `track_clip`, `track_rate`, `track_sort`
and, through `0x49667c`, `matrix_ctor`; the runtime (`0x49630c`, new
`0x47749c`, new[] `0x4774f0`, `0x49667c` with the element constructors
`0x4922e6` and `0x495132`), the arena allocation `0x47a288` and the CW3220
thunks memset `0x49681a` and memcpy `0x49680e`.

### Call path and live visibility

All nine run while objects are built: the actor load's constructors
(section 17) and the weapon factory's constructors (section 16, through
`0x40df30`); `0x42067c` and its use of `shape_word_block` belong to the
same load (its counts are constant over each suite's run in the map). With
early arming (section 13, the default) the hooks are installed before the
load, so their counters are constant from the first tick record on.

### Contracts (from the bytes)

All nine are cdecl with two argument slots; EDX:EAX is reproduced exactly.
Game calls go through the guest bridge with the original's entry
EAX/EDX/ECX (the core follows them step by step; the in-vitro logs compare
every one). "Count ±n" means `[[0x6cbee4]] ± n`, the pointer reread each
time. No function reads a local it did not write (the two runtime records
are written by `0x49630c`), and none reads a caller's EBX/ESI/EDI.

**`ts_obj_ctor` 0x402278 (O, shape)**, frame `add esp,-38h`: `0x49630c`
(EAX = the descriptor `0x497145`, the entry EDX/ECX); [O] = `DRAWABLE`
`0x4a14a4`, count +1 (ECX = 0, EDX the pointer, EAX = 0); the locals
EBP−2Ch/−2Ah/−28h (words) and EBP−38h/−34h/−30h (dwords) = 0, then the pose
block from them: [O+2Eh], [O+26h], [O+2Ah] = 0, dword [O+0Ch] = 0, word
[O+10h] = 0, word [O+32h] = 0 (EAX = O+0Ch, EDX = 0, ECX = 0); [O] =
`MAT_OBJ` `0x497289`, count +1 (EAX the pointer), [O] = `TS_OBJ`
`0x4a0b80`. A null shape gives EDX = 0. Otherwise new(42h) (EAX the
pointer, EDX = ECX = 0; `POP ECX` = 42h) into EBP−4; null: EDX = 0;
else context word 14h, `anim_instance_ctor(obj, shape)` (EAX = obj, EDX =
shape, ECX = obj), count −3 (EAX the pointer), context 8, EDX = obj.
[O+34h] = EDX; memset(O+38h, 0, 8) (EAX = O+38h, EDX/ECX as left); byte
[O+39h] = 1; count +1 (EDX the pointer). EAX = O, EDX = the counter
pointer, ECX = the saved FS:0 head from the record (EBP−24h), which the
epilogue stores to FS:0.

**`det_anim_shape` 0x40332c and `det_anim_static` 0x403368 (O, shape)**:
`ts_obj_ctor(O, shape)` respectively `object_init(O, shape)` (EAX = shape,
the entry EDX/ECX); [O] = `ANIM_OBJ` `0x4973ac`; ECX = 0; count +1 (EDX the
pointer); [O] = `DET_ANIM_OBJ` `0x4973cc`; [O+41h] = 0; count +1 (EAX the
pointer). EAX = O, EDX = the first counter pointer, ECX = 0.

**`det_anim_record` 0x4033a4 (O, R)**: `ts_obj_ctor(O, [[R]])` (EAX =
[[R]], the entry EDX/ECX); [O] = `ANIM_OBJ`; count +1 (EDX the pointer);
[O] = `DET_ANIM_OBJ`; [O+41h] = R; count +1 (ECX the pointer). EAX = O.

**`anim_instance_ctor` 0x478838 (I, shape)**: `shape_instance(I, shape)`
(EAX = shape, the entry EDX/ECX), which stores the shape at I+4; [I] =
`0x4a14c4`; memset(`0x6c56a8`, 0, 20h) with `shape_instance`'s registers;
DX = 4000h (memset's high word kept) into the words `0x6c56b8`, `0x6c56ae`,
`0x6c56a8` (a global rotation with the diagonal 4000h); the ten slot
dwords I+1Ah..I+40h = 0 (EAX = I+42h, EDX = 10, ECX = 0). The node table
N = [[I+4]+2Ah]; word [I+0Ch] = n = word [N+16h]; new[](sx n) (EAX = sx n,
EDX = N, ECX = 0; `POP ECX` = sx n) into [I+0Eh]; `0x49667c(0, 0Ch, sx
word [I+0Ch], 1, 0x4922e6)` (EAX = new[]'s, EDX = that count, ECX = sx n)
into [I+12h] (the 12-byte poses); `0x49667c(0, 20h, sx word [I+0Ch], 1,
0x495132)` (EAX/EDX = the first's, ECX = the count) into [I+16h] (the
matrices). For k = 0 while k < sx word [I+0Ch] (reread, signed): the three
dwords of entry sx word [[N'+18h] + 2k] of the pose table [N'+12h] (N' =
[[I+4]+2Ah] reread per node, the entry index × 3 dwords, 32-bit
arithmetic) copied to [I+12h] + 12k, then byte [[I+0Eh] + k] = 1.
`node_matrices(I)` (EAX = the final k, EDX = [I+0Eh] or, with no node, the
second `0x49667c`'s EDX, ECX = sx word [I+0Ch]; `POP ECX` = I); M =
[I+16h]; memset(M, 0, 20h) (EAX/EDX = `node_matrices`', ECX = I); words
[M+10h], [M+6], [M] = 4000h; count +1 (EDX the pointer). EAX = I, ECX =
memset's.

**`object_track` 0x402374 (O, word)**, frame `add esp,-24h`: `0x49630c`
(descriptor `0x497175`); new(52h) (EAX = `0x49630c`'s, the entry EDX/ECX;
`POP ECX` = 52h) into EBP−4; non-null: context 14h, `thread_ctor(T,
[[[O+34h]+4]+2Ah])` (EAX = that shape tree, EDX = T, ECX = [[O+34h]+4],
T reread from EBP−4), context 8. `track_clip(T, word, 0, 0)` with AX =
the word (`MOV AX`: the pushed dword carries EAX's high word, from
`thread_ctor`, or from new on the null path), `track_rate(T, 0)`,
`thread_add([O+34h], T)` (EDX = [O+34h], read after `track_rate`). EAX = T,
EDX = the saved FS:0 head (the caller restores it), ECX = `thread_add`'s.
A null thread reaches `track_clip`, which faults at its first store; the
game's allocations do not fail (map: `0x478e48` = `0x402374` in every
suite).

**`thread_add` 0x478a3c (I, T)**: the first k = 0..9 with [I+1Ah+4k] = 0
gets T, then `track_sort(I)` (EAX = the slot, EDX = k, ECX = I; `POP ECX`
= I), EAX = 1, EDX = `track_sort`'s; ten taken slots: EAX = 0, EDX = 10.
ECX = I. The entry registers are not read.

**`thread_ctor` 0x478e48 (T, M)**: [T] = M; `track_clip(T, 0, 0, 0)` (EAX =
T+2Eh, EDX = M, the entry ECX); memset(T+2Eh, 0, 20h) with `track_clip`'s
registers; CX = 4000h (memset's high word kept) into the words [T+3Eh],
[T+34h], [T+2Eh]. EAX = T, EDX = memset's, ECX = memset's with that low
word.

**`shape_word_block` 0x402fc0 (B, S)**: [B] = S. S not null and n = word
[S+24h] not 0 (`JBE` after `TEST`: zero only): `0x47a288(2n, [0x6c5830])`
(EAX = the heap, EDX = 2n, the entry ECX) into [B+4], then memcpy(block,
[[B]+1Ch], 2 × word [[B]+24h]) ([B] reread; EAX = [B], EDX = the size, ECX =
the source); n = 0: [B+4] = 0 (EAX = 0); S = 0: [B+4] untouched. Count +1
(EDX the pointer). EAX = B; ECX = memcpy's or the entry ECX. The same steps
as `shape_instance`'s (main_loop.md) at +0/+4 instead of +4/+8.

**ECX at the callers** (forward walk of both arms of every branch from each
return address): `ts_obj_ctor` — ECX cleared (`0x403343`) or loaded
(`0x4033d6`) first; `det_anim_shape`/`det_anim_static` — loaded first at
`0x405404`, `0x405527`, `0x40564a`, `0x40576b`, `0x4058c0`; after
`0x421624` it enters the list append `0x411dd4`, which loads ECX first
(`0x411dde`); `det_anim_record` — loaded at `0x415bfa`;
`anim_instance_ctor` — enters the memset thunk (`0x40234b`; the CW3220
memset's registers do not depend on the entry ECX,
[rtl_imports.md](../reference-game/rtl_imports.md)), then loaded
(`0x402361`); `thread_add` — becomes `object_track`'s ECX; `thread_ctor` —
enters `track_clip`, which overwrites CX (`0x4792c3`) and then ECX
(`0x4792d4`) before reading ECX; `shape_word_block` — loaded first at
`0x40328c`, `0x40dfa8`, `0x4206bb`. `object_track`: in `mech_ctor` loaded
at `0x415dac`, or it enters the next `object_track` (whose `0x49630c` and
new do not read it: new's `0x476edc` writes ECX or leaves it to `POP
ECX`), `script_start` (loads ECX at `0x413e65`) or the flat-object count;
in `0x40df30` it enters the next `object_track`, the memset thunk
(`0x40e006`) or `0x40f540` (loads ECX at `0x40f54f`); in
`building_factory` CX takes the rate word (`0x405a01`) and the dword is
`track_rate`'s second slot, so ECX's high word reaches the slot, the high
word of `track_rate`'s EDX and ECX and the dwords it pushes to
`track_target` (`0x479709`); `track_target` reads its slots as words
(`0x479682`, `0x47968a`, `0x479692`), and after `track_rate` the factory
loads ECX (`0x405a21`, `0x4059e1`) while EDX enters `track_seek` (writes
EDX and ECX first, `0x479350`/`0x479353`) or the next `object_track` (not
read). So ECX is dead after all nine hooks and the framework's entry ECX
stands in for it; the cores still compute it (compared in vitro).

**Frames and shims.** `ts_obj_ctor` and `object_track` run with the
synthetic EBP of a zeroed scratch (`ES2_OBJECT_SHAPES_FRAME`; `0x49630c`
through `weapons_impact_guest_call`), and their adapters write the saved
head back to FS:0 (`ts_obj_ctor`: ECX, `object_track`: EDX). No entry or
exit shim: no uninitialized local, no caller register, no stack residue
read by a caller is known (the live regression is the check).

**Compare:** refused for all nine (allocation, constructors, the
construction count, the runtime record, game calls); the after-load
snapshot is the acceptance.

### In vitro (group `object_shapes`)

`invitro/object_shapes_cases.h`: per hook 100,000 direct cases in the quick
tier (1,001,024 in the full tier, not run)
against logging stubs for every game callee (17 stub ids; memset and
memcpy are stubs that fill or copy and return random registers): the
original called with six slots, EAX/EDX/ECX, EBX/ESI/EDI and a stack filled
with a case pattern, the core with the same inputs. Compared: the pair,
ECX, FS:0, the ordered calls with slots (and the bridge's slot count),
entry registers (stack addresses masked) and the record's context word,
the 16 KiB arena and the image globals (the counter pointer, the heap
`0x6c5830`, the rotation `0x6c56a8`). The stubs change what the originals
reread (the counter pointer after the constructors, [O+34h] and the shape
chain after new and the thread calls, the node count word I+0Ch and the
shape's tables after new[] and `0x49667c`, [I+16h] after `node_matrices`,
the block's shape and word count after `0x47a288`); odd cases add null
allocations, null shapes, random shapes and full slot tables. In even cases
(the real callers' inputs) the adapter is compared too, with the callee
counter deltas. 256 installed 31/a7 cases per hook. Real-callee passes
(4,096 cases per hook except `shape_word_block`, which has no nested hook):
the constructors over the real `0x49630c`, `ts_obj_ctor`,
`anim_instance_ctor`, `object_init` and `0x49667c` with its element
constructors (`matrix_ctor` entered 8,148 to 12,052 times), new, new[],
`shape_instance` and `node_matrices` stubs; `anim_instance_ctor` over the
real `shape_instance` (`0x47a288` a bump allocator) and `0x49667c`;
`object_track`, `thread_ctor` and `thread_add` over the real thread
functions, `track_clip` (its node's slot +20h a stub), `clip_pose`,
`track_rate`, `track_sort` and `track_count` on a synthetic node table and
thread slots; the nested hooks in all original/replaced combinations, the
hook under test original or replaced. 9 branch sites, all both ways (the
null-allocation edges `0x40230c` and `0x402396` with the odd cases). One
caught mutation per hook (`MUTATIONS.md`). Core unit test
`//core:object_shapes_test` (scenarios worked out by hand: a TS_OBJ with,
without and with a failed shape instance; the three DET_ANIM_OBJ
variants; a two-node instance and an empty one; a track with and without
its thread; a slot insert and ten taken slots; a thread; a three-word
block, an empty one and no shape).

### Live counts

Measured on the original: the DBSIM map re-run `dbsim-map-20261012` (every hook original, early arming, all 40 suites) gives the
entry counts below (`build\claude\h4-108\expected_counts.py`). They agree
with section 17's live counters of `check-r7-3b` in all 40 suites:
`det_anim_record` = `hk_mech_ctor_calls`, `det_anim_shape` =
`hk_building_factory_call_40332c`, `det_anim_static` =
`hk_building_factory_call_403368` + `hk_flyer_ctor_calls`, `ts_obj_ctor`
= `mech_ctor` + `_call_40332c`. The replaced hooks count the same entries.

| Suites | `ts_obj_ctor` | `det_anim_shape` | `det_anim_static` | `det_anim_record` | `anim_instance_ctor` | `object_track` | `thread_add` | `thread_ctor` | `shape_word_block` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| the 19 suites on the installed C1_01 (acceptance-60s, c1_01-success, -collision, …) | 25 | 15 | 39 | 10 | 25 | 59 | 59 | 59 | 71 |
| c1_01-unarmed-keys | 24 | 15 | 39 | 9 | 24 | 56 | 56 | 56 | 68 |
| c1_02-probe | 28 | 15 | 37 | 13 | 28 | 68 | 68 | 68 | 78 |
| c1_06-cheat-disable | 31 | 15 | 43 | 16 | 31 | 77 | 77 | 77 | 115 |
| c1_08-escort-route, c1_08-scan | 12 | 4 | 4 | 8 | 12 | 31 | 31 | 31 | 66 |
| c2_02-identify | 16 | 5 | 14 | 11 | 16 | 43 | 43 | 43 | 88 |
| c3_09-loadout-b, -orders-matrix, -wingman-orders | 31 | 15 | 17 | 16 | 31 | 78 | 78 | 78 | 178 |
| c3_10-detail-options, -hunt-targ, -loadout-hercs, -own-building-fire | 34 | 18 | 7 | 16 | 34 | 84 | 84 | 84 | 159 |
| c4_05-convoy | 15 | 2 | 2 | 13 | 15 | 43 | 43 | 43 | 123 |
| c4_08-joystick, -razor-plasma, -slope-dive | 23 | 12 | 16 | 11 | 23 | 54 | 54 | 54 | 125 |
| c4_10-probe | 21 | 12 | 14 | 9 | 21 | 47 | 47 | 47 | 71 |
| c5_05-moon-missiles, c5_05-orders-flyer | 36 | 21 | 12 | 15 | 36 | 86 | 86 | 86 | 174 |
| train1-timers | 5 | 4 | 7 | 1 | 5 | 11 | 11 | 11 | 13 |

Callee counters per call (`hk_<hook>_call_<va>`): `ts_obj_ctor` calls
`49630c`, `47749c`, `478838` and `49681a` once each (`478838` = the calls:
every shape is set and no allocation fails, map `0x478838` =
`0x402278`); `det_anim_shape` and `det_anim_record` call `402278`,
`det_anim_static` `402188` once; `anim_instance_ctor` calls `490b64`,
`4774f0`, `478c64` once and `49667c`, `49681a` twice; `object_track` calls
`49630c`, `47749c`, `478e48`, `4792ac`, `4796d8` and `478a3c` once each;
`thread_ctor` calls `4792ac` and `49681a` once; `thread_add`'s `4789d0`
(one per call with a free slot) and `shape_word_block`'s `47a288`/`49680e`
(one per shape with a nonzero word count) were not predicted (their
callees have other callers, so the map totals do not split). Every
`*_skipped`/`*_bypassed` 0. The hook counters of `mech_ctor`, `flyer_ctor`,
`building_factory`, `weapon_factory`, `object_init`, `shape_instance`,
`node_matrices`, `track_clip`, `track_rate`, `track_sort`, `clip_pose`,
`track_count` and `matrix_ctor` are unchanged (the bridge still reaches
them), and so are section 17's `hk_building_factory_call_402374`/`_40332c`/
`_403368` and `hk_mech_ctor_call_4033a4`/`_402374`.

### After-load snapshot reach

A difference caused by these hooks would lie in an object's pose block
(+0Ch..+33h), its shape-instance pointer +34h and flag bytes +38h..+3Fh,
the `DET_ANIM_OBJ` word +41h, the shape instances (their slots +1Ah, the
node count, the flag, pose and matrix arrays the instance points to), the
threads (52h-byte blocks: shape tree, clip state, the state block
+2Eh..+4Dh), the global rotation `0x6c56a8`, the weapon and object word
blocks (+4 of the block) or in the block pairing (sizes or walk order):
the hooks allocate the same sizes in the same order through the same new,
new[], `0x49667c` and `0x47a288`.

### Live result

`check-r7-4` (all 631 hooks replaced, own runtime): **40 of
40 identical**; the nine counters equal the table above in every suite (25
/ 15 / 39 / 10 / 25 / 59 / 59 / 59 / 71 in the C1_01 suites, 31 / 15 / 17 /
16 / 31 / 78 / 78 / 78 / 178 in C3_09, 34 / 18 / 7 / 16 / 34 / 84 / 84 / 84
/ 159 in C3_10, 5 / 4 / 7 / 1 / 5 / 11 / 11 / 11 / 13 in train1-timers),
and `det_anim_record` = `mech_ctor` everywhere. After-load snapshot
`snapshot-r7c-20261013` (section 14): identical.

### Open

- `thread_add`'s `track_sort` calls and `shape_word_block`'s allocation
  calls per suite.
- `0x4922e6` (the pose array's element constructor: two calls of
  `0x4922d3` at +0 and +6) is reached only through `0x49667c`'s element
  pointer and has no verdict row yet.
- The third call of `shape_word_block`, `0x403274`, lies in `0x4031d4` (a
  function of its own after the `RET` of the H3 draw `0x40316c` at
  `0x4031d3`, with the runtime record `0x497348`), not in `0x40316c` as
  the verdict row of function_verdicts.md says. No call, jump or file dword
  reaches `0x4031d4` and the map has no probe row for it; it was not
  examined further.
## 20. Collision data

The collision data of `collide.cpp` (the assertion strings name the file),
read from the pinned `llvm-objdump -d -M intel` listing of the reference
DBSIM (the exception descriptor `0x4987f0`, the stream vtable `0x4a0e10`
and the assertion strings read from the file). The verdict rows of
function_verdicts.md were the starting hypotheses and hold. Own code: adapter
group `collision_data`, core module `core/src/collision_data.c` (not
`collision.c`, which holds the runtime ground collision with its own
observer structure; these are load-time readers on the weapons guest
bridge like the other mission-load modules). **Confirmed live:**
`check-r7-4`; after-load snapshot `snapshot-r7c-20261013` (section 14).
Evidence (scripts and logs, no listings): `build\claude\h4-112\`.

| Hook | VA | Role | Callers |
|---|---|---|---|
| `collision_load` | `0x40cd88` | the COL resource of a type into the next slot of `[0x4a9824]` | `mech_ctor` (`0x415fb5`), the FLYER type load `0x422ed0` (`0x422f69`) |
| `collision_read` | `0x40ccf8` | the group reader | `collision_load` (`0x40cdfc`), the building module's stage-2 callback `0x405fac` (`0x40659a`, `BASECOL.DAT`, 65 entries) |
| `collision_entry` | `0x40cc50` | one group: part word, volume count, volumes | `collision_read` (`0x40cd6a`) |
| `collision_volume` | `0x40cc14` | one volume: region word, spheres | `collision_entry` (`0x40ccaa`), the weapon-list entry `0x40f8bc` (`0x40f8d7`) |
| `collision_points` | `0x40c7c4` | the sphere array | `collision_volume` (`0x40cc41`) |
| `collision_bounds` | `0x40ccc8` | the bounding sphere of each volume of a group | `collision_read` (`0x40cd73`), `collision_scan` (`0x40ceec`) |
| `collision_radius` | `0x40c5d0` | centre and radius of one volume | `collision_bounds` (`0x40ccdd`) |
| `collision_scan` | `0x40cec0` | the part-list scan of `weapon_fit` | `weapon_fit` (`0x41778f`) |
| `collision_match` | `0x40ce78` | the volume match of one group | `collision_scan` (`0x40cede`) |

No dword of the file holds the address of any of the nine (a scan of the
whole file), so these call sites are all. Every callee stays game code
behind the guest bridge with its own mode: the stream reads (slot `+18h`
of the stream object, `stream_read` `0x4735b8` for the game's streams),
new `0x4774f0`, new[] `0x49667c` with the element constructors `0x40c8be`
(returns its argument) and `0x40cf04` (EAX = the argument, EDX = it + 8),
the runtime record `0x49630c`, the stream constructor `0x472f14` and
`stream_destroy` `0x472f64`, the assertion `0x479c78` (fatal
report), `dist3d_delta` `0x47de72`, the original volume match `0x40ce28`
(which copies a weapon's spheres into the matched volume through
`0x40c834`) and each of the nine for the others, so a nested hook keeps its
mode and its counter.

### Layout

A descriptor is 6 bytes (group count word +0, groups +2). A group is 10
bytes: part word +0 (read from the file), +2 not written by the reader,
volume count word +4, volumes +6. A volume is 16 bytes: sphere count word
+0, spheres +2, region word +6, centre +8..+0Dh, radius word +0Eh. A sphere
is 8 bytes: x, y, z, radius words. This is [collision.md](../formats/collision.md)'s
COL grammar with the runtime fields; the reader reads the words straight
into these records, so the file's group "part, count" pair lands at +0 and
+4.

### Contracts (from the bytes)

All nine are cdecl with a `push ebp` frame; EDX:EAX is reproduced exactly.
"Read(n, buf)" is the stream's virtual `+18h` (S, n, buf); S's vtable
`[S]` is reread for every read. Loops count in a 32-bit register and
compare its low word with the reread count word, signed (`CMP r16,[m]`);
a negative count runs no iteration.

**`collision_load` 0x40cd88 (name)**, frame `add esp,-50h`: descriptor
`0x4987f0` ([+4] = −50h) gives the runtime record at EBP−50h, context word
EBP−40h; `0x49630c` (EAX the descriptor, the entry EDX/ECX), context word
= 8. `0x472f14(EBP−30h, name)` (EAX = `0x49630c`'s, EDX = EBP−30h)
builds the stream object at EBP−30h (vtable `0x4a0e10`). A nonzero status
dword [EBP−2Ch] calls `0x479c78(0x6c56c8, 5, 0, "collide.cpp", 101h,
name)` (EAX = the status, EDX = 0 from `SETE DL; AND EDX,1`). Then CX =
the word `0x4987de` (ECX keeps its high word), the word incremented in
memory (`INC WORD`, a second read), the slot = `[0x4a9824]` + 6 ×
s16(old word) (the table pointer read after the increment);
`collision_read(slot, EBP−30h)` (EAX = EBP−30h, EDX as left, ECX the CX
load); `0x472f64(EBP−30h, 2)` (EAX = the slot, EDX = EBP−30h; the slot is
pushed above the two arguments and popped into EAX afterwards). EAX = the
slot, EDX = the saved FS:0 head [EBP−50h], which the epilogue stores to
FS:0; ECX = `0x472f64`'s. The table `[0x4a9824]` is allocated by
`0x40cbec` in the world load (pass 1, `0x4252d8`, section 2 step 9): new(6
× the word `0x4987dc`), which the type loads `0x41ff10`/`0x422e90` count
up through `0x40cbd4` (step 8). The counter `0x4987de` is not reset by
any of the nine.

**`collision_read` 0x40ccf8 (D, S).** A nonzero status [S+4] calls
`0x479c78(0x6c56c8, 5, 0, "collide.cpp", 0EEh, "collision data load")`
(EAX = the status, EDX = 0, ECX the entry's). Read(2, D) (EAX = D, EDX =
S, ECX = [S]). Count word c = [D]: 0 returns the read's registers.
Otherwise `new(10 × s16(c))` (EAX = s16(c), EDX = the size, ECX the
read's; `POP ECX` = the size), [D+2] = the block; for each group G (stride
10) while i < s16([D]): `collision_entry(G, S)` (EAX = S) and
`collision_bounds(G)` (`POP ECX` = G). EDX:EAX/ECX: the last call's (a
negative count: new's, ECX the size).

**`collision_entry` 0x40cc50 (G, S).** Read(2, G) (EAX = G, EDX = S, ECX =
[S]), Read(2, G+4) (EAX = G+4); no read result is tested. A nonzero count
word k = [G+4]: `0x49667c(0, 10h, s16(k), 1, 0x40cf04)` (EAX = s16(k),
EDX/ECX the read's), [G+6] = the block; for each volume V (stride 10h)
while i < s16([G+4]): `collision_volume(V, S)` (EAX = S). Then `MOV AX,1`:
EAX = 1 with the high word of the last call's EAX (the second read's, the
new[] block's, or the last `collision_volume`'s).

**`collision_volume` 0x40cc14 (V, S).** Read(2, V+6) (EAX = S, EDX = [S],
ECX the entry's); r = AL of the result: nonzero calls
`collision_points(V, S)` (EAX = S). EAX = r (0..FFh), EDX/ECX the last
call's.

**`collision_points` 0x40c7c4 (V, S).** Read(2, V) (EAX = S, EDX = [S],
ECX the entry's); r = AL. When r is nonzero and the word 8 × [V] (EDI =
V with its low word replaced by the count, `SHL EDI,3`, tested as DI) is
nonzero: `0x49667c(0, 8, s16([V]), 1, 0x40c8be)` (EAX = s16 of the
count, reread), [V+2] = the block, Read(s16(DI), block) (EAX = that size,
EDX = S, ECX = [S], read after the store). r = AL of that read. EAX = r.
So a count of 1000h asks for a read of FFFF8000h bytes and 2000h reads
nothing; [collision.md](../formats/collision.md)'s corpus stays below 13
spheres per volume.

**`collision_bounds` 0x40ccc8 (G).** For each volume V of [G+6] while i <
s16([G+4]): word [V+0Eh] = AX of `collision_radius(V, V+8)` (EAX = V+8,
EDX/ECX as left). No volume: the entry's EAX/EDX/ECX.

**`collision_radius` 0x40c5d0 (V, O).** Six frame words: maxima x, y, z at
EBP−10h/−0Eh/−0Ch from −10000 (D8F0h), minima at EBP−8/−6/−4 from 10000.
For each sphere (x, y, z, r) of [V+2] while i < s16([V]): per axis the
word sum a + r replaces the maximum unless it is smaller (signed 16-bit
`CMP`/`JL`), the word difference a − r replaces the minimum unless it is
larger (`JG`); 16-bit wrap-around included, so a sum above 32767 counts as
negative. Then O: dword [O] = minimum x | minimum y << 16, word [O+4] =
minimum z, words +4, +0, +2 += the maxima (in that order), then `SAR` of
the words +4, +0, +2 (arithmetic, 16 bits): the centre is (minimum +
maximum) SAR 1 with the sum taken modulo 2^16. Without a sphere the
centre is 0 and the deltas below −10000 ([collision.md](../formats/collision.md)
"zero-sphere shapes"). The deltas are max − centre per axis (16 bits);
`dist3d_delta(s16(dx), s16(dy), s16(dz))` with EAX/EDX/ECX = O's high word
with the low word cy/cz/cx (the loads of the deltas); its EDX:EAX and ECX
are returned. The entry EDX/ECX never reach memory (only their low words
pass through the frame words, the tail reloads both).

**`collision_scan` 0x40cec0 (T, A, B, n)**, `weapon_fit`'s call is (the
actor's table [A+1F6h], EBP−30h, EBP−38h, 1). For each group g of [T+2]
(stride 10) while i < s16([T]): `collision_match(g, A, B, n)` (EAX = the
pushed dword: n's low word with the high word of s16([T]), which is 0
inside the loop; EDX = B, ECX = A). The first nonzero AX calls
`collision_bounds(g)` and ends the scan (its EDX:EAX, `POP ECX` = g).
Without a match EAX = s16([T]) (the failed loop test), EDX/ECX the last
call's or the entry's.

**`collision_match` 0x40ce78 (G, A, B, n).** For each volume V of [G+6]
while i < s16([G+4]): `0x40ce28(V, A, B, n's low word)` (registers as in
the scan); a nonzero AX ends the loop. EDX = s16([G+4]) reread, EAX = 1
when i < EDX (a match) else 0 (`SETL AL; AND EAX,1`), ECX the last call's.
`0x40ce28` compares V's region word with the region word of each of the n
volumes [A+4j] and, on the first equal one, copies its spheres moved by
[B+4j] into V through `0x40c834` (new[] of the sphere array); `weapon_fit`
has set the weapon volume's region word (+18h of the weapon record) to
the mount's slot byte + 13h just before.

**Registers after the calls (ECX).** The framework returns the entry ECX;
the originals' ECX is dead at every call site: `collision_load` → `POP
ECX` (`0x415fb6`, `0x422f6e`); `collision_read` → `0x472f64`'s entry
(it sets up its record through `0x49630c`, which reads neither ECX nor
EDX, writes ECX at `0x472fac`/`0x472feb` before the decoder calls, and its
close `0x47307c` and base destructor `0x47a8a4` read none) or, in
`0x405fac`'s loop, the next `collision_read`, whose entry ECX reaches only
the fatal `0x479c78` on a stream error; `collision_entry` →
`collision_bounds`, which passes ECX to `collision_radius` (dead there) or
returns it to a `POP ECX`; `collision_volume` → the next
`collision_volume` or `collision_entry`'s return (as before), or
`0x40f8bc`, which writes ECX at `0x40f8ec`; on the way the stream read
saves its entry ECX with `PUSH ECX` and pops it at its return without
reading the slot; `collision_points` → `collision_volume`'s return;
`collision_bounds` → `POP ECX` (`0x40cd78`, `0x40cef1`);
`collision_radius` → the next `collision_radius` or `collision_bounds`'
return; `collision_scan` → `weapon_fit` writes ECX at `0x4177a1`;
`collision_match` → `collision_bounds`, or the next match (ECX written at
`0x40ce91`), or the scan's return.

**Frames, residue, shims.** `collision_load` runs with the synthetic EBP
of a zeroed scratch (`ES2_COLLISION_LOAD_FRAME`; `0x49630c` through
`weapons_impact_guest_call`), the stream object at the original offset;
the adapter writes the saved head (EDX) back to FS:0. No function reads a
local it did not write, and no callee reads one of theirs apart from the
stream object, which `0x472f14` builds. The scan and the match push the
word n as a dword whose high word is that of the loop's count (0);
`0x40ce28` reads the low word only (`MOVSX`). The framework passes up to
six argument slots; `collision_load`'s `0x472f64` call reads two (the
original's third dword, the pushed slot, is popped, not read). No entry or
exit shim.

**Compare:** `collision_radius` accepts it: it writes only the six centre
bytes (one span: argument 1, 6 bytes; in the game O = V+8, outside the
sphere count and pointer it reads) and calls the pure `dist3d_delta`
(itself compare-capable), so a replacement phase repeats only that pure
call. The other eight refuse it (stream reads, allocation, the slot
counter, the runtime record, the assertion, game calls that write the
groups and volumes).

### In vitro (group `collision_data`)

`invitro/collision_data_cases.h`, quick tier: per hook 100,000 direct
cases against logging stubs for every game callee (16 stub ids; the
stream read is the slot `+18h` of two arena vtables, writing a scripted
word or bytes into its buffer), the original called with six slots,
EAX/EDX/ECX, EBX/ESI/EDI and a stack filled with a case pattern. Compared:
the pair, ECX, FS:0, the ordered calls with argument slots (and the
bridge's slot count), entry registers (stack addresses masked) and the
record's context word, the 8 KiB arena, and the globals `0x4987de`,
`0x4a9824` and `0x6cbee4`. The stubs change what the originals reread
(every count word inside a loop, the stream's vtable after a read, the
slot counter and table after `0x472f14`/`0x479c78`); the centre may lie
over the spheres or the volume's pointer; counts include negative words
and, for `collision_points`, the words whose 8-fold wraps (1000h, 1FFFh,
2000h, 2001h). In even cases (the real callers' inputs: status 0, counts
0..4, O = V+8, n = 1) the adapter is compared too, with the callee counter
deltas. 256 installed 31/a7 cases per hook. Real-callee passes (4,096
cases per hook): the reader chain over a scripted stream with the real
`0x49667c` and its element constructors and a bump allocator for
`0x4774f0` (`collision_load` with the real `0x49630c` and stubs for
`0x472f14`, `0x472f64`, `0x479c78`; nested `collision_read`,
`collision_entry`, `collision_volume`, `collision_points`,
`collision_bounds`, `collision_radius` in all original/replaced
combinations), `collision_read` (the same five below it and
`dist3d_delta`), `collision_entry`, `collision_volume`, `collision_points`
(no nested hook; the real `0x49667c`), `collision_bounds` and
`collision_radius` over the real `dist3d_delta`, `collision_scan` and
`collision_match` over the real `0x40ce28`/`0x40c834` (weapon volumes,
offsets and region words that match), the hook under test original or
replaced. `collision_radius` also passes the 512 installed compare cases
of `compare_probes.h` (0 mismatches, 256 skewed of which 128 by a span
byte). 21 branch sites, all both ways, none PROVED. One caught mutation per
hook (`MUTATIONS.md`). Core unit test `//core:collision_data_test`
(scenarios worked out by hand: two spheres, none, 16-bit wraps of the sums
and of the centre; the reader chain's registers, the wrapped sphere
counts, a failed read, a negative group count, the slot counter's sign;
the scan and the match with and without a hit).

### Live counts

From the map `dbsim-map-20261012` (every hook original, early arming,
all 40 suites), whose entry counts are those of the originals; its probe
counts stop at 1,000. The rows agree with the launch sets:
`collision_load` = `mech_ctor` + 1 when the mission has a flyer type load
(`0x422ed0`), `collision_read` = `collision_load` + 65 (`BASECOL.DAT`),
`collision_volume` = `collision_points` = the volumes read + 33 (the
weapon list, `0x40f8bc`), `collision_bounds` = `collision_entry` +
`collision_scan` (every scan finds its group), `collision_radius` = the
volumes of all groups + the volumes of the scanned groups.

| Suites | load | read | entry | volume | points | bounds | radius | scan | match |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| the 19 suites on the installed C1_01 (acceptance-60s, c1_01-success, -collision, …) | 11 | 76 | 103 | 296 | 296 | 134 | 603 | 31 | 189 |
| c1_01-unarmed-keys | 10 | 75 | 97 | 276 | 276 | 125 | 544 | 28 | 171 |
| c1_02-probe | 14 | 79 | 125 | 342 | 342 | 163 | 688 | 38 | 233 |
| c1_06-cheat-disable | 17 | 82 | 133 | 375 | 375 | 198 | 949 | 65 | 390 |
| c1_08-escort-route, c1_08-scan | 9 | 74 | 85 | 244 | 244 | 121 | 557 | 36 | 216 |
| c2_02-identify | 12 | 77 | 115 | 291 | 291 | 153 | 616 | 38 | 237 |
| c3_09-loadout-b, -wingman-orders, -orders-matrix | 16 | 81 | 132 | 438 | 438 | 230 | ≥ 1,000 | 98 | 588 |
| c3_10-loadout-hercs, -own-building-fire, -detail-options, c3_10-hunt-targ | 16 | 81 | 132 | 432 | 432 | 211 | ≥ 1,000 | 79 | 474 |
| c4_05-convoy | 14 | 79 | 123 | 355 | 355 | 176 | 958 | 53 | 324 |
| c4_08-razor-plasma, -slope-dive, -joystick | 12 | 77 | 98 | 319 | 319 | 153 | 978 | 55 | 295 |
| c4_10-probe | 10 | 75 | 105 | 284 | 284 | 136 | 670 | 31 | 186 |
| c5_05-moon-missiles, c5_05-orders-flyer | 16 | 81 | 127 | 413 | 413 | 221 | ≥ 1,000 | 94 | 564 |
| train1-timers | 1 | 66 | 42 | 133 | 133 | 45 | 139 | 3 | 18 |

(load = `hk_collision_load_calls`, and so on.) The three `≥ 1,000` rows
are the map's cap: there `collision_radius` = 405, 399 and 380 (the
volumes read) + the volumes of the 98, 79 and 94 scanned groups, which
depend on the mounts, the loadouts and each type's COL groups; live:
1,711 in the C3_09 suites, 1,418 in C3_10 and 1,608 in C5_05. Callee counters per call: `collision_load` one
`call_49630c`, `_472f14`, `_40ccf8`, `_472f64` (`_479c78` 0);
`collision_read` one `slot_18`, and per nonzero count one `call_4774f0`,
`call_40cc50` and `call_40ccc8` per group (so the suite totals of
`hk_collision_read_call_40cc50` = `_40ccc8` = the entry counts above,
`_4774f0` = the reads with groups, `_479c78` 0); `collision_entry` two
`slot_18`, one `call_49667c` per group with volumes and one
`call_40cc14` per volume; `collision_volume` one `slot_18` and one
`call_40c7c4` per successful read (= the volume count);
`collision_points` one or two `slot_18` and one `call_49667c` per volume
with spheres; `collision_bounds` one `call_40c5d0` per volume (totals =
the radius column); `collision_radius` one `call_47de72`; `collision_scan`
one `call_40ce78` per group tried and one `call_40ccc8` (= the scan
count); `collision_match` one `call_40ce28` per volume tried (the map's
`0x40ce28` counts: 341 in the C1_01 suites, 1,000 = capped in C3_09 and
C5_05). Every `*_skipped`/`*_bypassed` 0. `hk_dist3d_delta_calls` and
`hk_stream_read_calls` are unchanged (the bridge still reaches them);
`hk_mech_ctor_call_40cd88` and `hk_weapon_fit_call_40cec0` likewise.

### After-load snapshot reach

A difference caused by these hooks would lie in the slot table
`[0x4a9824]` and its counter `0x4987de`, in a block the readers allocate
(the group arrays, the volume arrays, the sphere arrays), in the centre or
radius words of a volume (+8..+0Fh), in the building table of
`BASECOL.DAT` (`0x405fac`'s descriptors), in the weapon list's volumes, or
in the block pairing (sizes or walk order): the hooks allocate the same
sizes in the same order through the same `new` and `new[]`.
`-Hooks "collision_radius=compare"` is possible (calls as above, 0
mismatches expected).

### Live result

`check-r7-4` (all 631 hooks replaced, own runtime): **40 of
40 identical**; the counters equal the table above in every suite (11 / 76
/ 103 / 296 / 296 / 134 / 603 / 31 / 189 in the C1_01 suites, 1 / 66 / 42 /
133 / 133 / 45 / 139 / 3 / 18 in train1-timers), the capped
`collision_radius` counts as given below the table. After-load snapshot
`snapshot-r7c-20261013` (section 14): identical.

### Open

- `0x40ce28` (36 instructions) and `0x40c834` (52) stay original: the
  sphere copy of the scan, the next candidates of this file; the element
  constructors `0x40c8be`/`0x40cf04` are reached only through `0x49667c`.
## 21. L6 actor load

L6 of section 10, the actor load `0x4253d8` (script pass 2), read from the
pinned `llvm-objdump -d -M intel` listing of the reference DBSIM (the
trigger case table `0x426492` and its jump table `0x42649d` decoded from
the bytes, the exception descriptor `0x49a8b5` and the stream vtable
`0x4a1738` read from the file). Own code: hook `actor_load` in adapter
group `mission_actors`, core `es2_actor_load_registers` appended to
`core/src/mission_load.c`. An own group rather than section 17's
`mission_ctors`: the hook is the caller of those constructors and its
in-vitro harness (a synthetic `script.dat` stream behind stub reads, every
callee a logging stub) shares nothing with theirs. **Confirmed live:**
`check-h499`; after-load snapshot `snapshot-r7d-20261013` (section 14).
Evidence (scripts and logs, no listings): `build\claude\h4-99\`.

Every callee stays game code behind the guest bridge with its own mode:

| Callee | Role | Class / mode |
|---|---|---|
| `0x49630c` | runtime frame record (descriptor `0x49a8b5`: EBP−34h) | RTL, through `weapons_impact_guest_call` |
| `0x45ee00`, `0x492b90` | the script name (`data\script` + `[0x4a09f0]`) | file |
| `0x47ad40`, `0x47af98`, `[vtable+18h]` (= `0x47af98`), `0x47ad78` | the stream in the frame: open, reads, close | file (stream library) |
| `0x40f998`, `0x40fa88` | the weapon shape cache (`mechwpns`) | resource |
| `0x470ed4`, `0x47129c` | terrain marks of a placed building, the cell marks | terrain |
| `0x4774f0`, `0x49680e` | new[], memcpy | RTL |
| `pool_alloc`, `mech_ctor`, `terrain_height`, `weapon_fit`, `condition_init`, `action_copy`, `initial_damage`, `flyer_ctor`, `building_factory`, `shape_word` (the building virtual +10h), `subject_resolve`, `heading_point`, `group_ctor`, `base_marks`, `pool_prev` | | hooks, their own modes |

### Call path and live visibility

`0x4253d8` has one call site, `0x46182b` in the mission load `0x461608`,
once per process. With early arming (section 13, the default) the hook is
installed at `0x45f42b`, before the load, so `hk_actor_load_calls` is 1 from
the first tick record on. The DBSIM map re-run `dbsim-map-20261012` (every
hook original, early arming) has the entry count 1 for `0x4253d8`,
`0x40f998`, `0x40fa88` and `0x47129c` in all 40 suites, and for
`0x425348` (all of whose calls come from here) exactly the predicted count
below in all 40 suites (`build\claude\h4-99\map_check.py`).

### Contract (from the bytes)

No arguments; EBX, ESI, EDI saved and restored. Frame `add esp,-0B30h`:
the runtime record at EBP−34h (context word EBP−24h), the stream object
EBP−0Ch (vtable `0x4a1738`, handle word +8), the count words EBP−36h (the
current list), EBP−38h (the authored mechs M), EBP−42h (timers), EBP−44h
(a route's points), the MEC ordinal EBP−46h, the record buffers (skip
EBP−8C4h, timer EBP−8DCh, mech EBP−964h, flyer EBP−9CCh, building
EBP−0A0Ch, order EBP−0A28h, group EBP−0AC4h, objective EBP−0B24h), the
group's ten order pointers EBP−0AECh, the building point EBP−0B30h, and the
staged positions and angles. No local is read before it is written (the
buffers are filled by the reads). EBX is the loop counter, compared as BX
(signed words); ESI the object or runtime-record cursor; EDI the
selection-flag pointer, then the trigger scope. In order:

1. `0x49630c` (EAX = the descriptor, the entry EDX/ECX). The statics: when
   byte `0x49a87d` is 0, dwords `0x49a871` = `0x49a875` = 0 (EDX, ECX = 0),
   `0x49a879` = FFFF8000h, the byte +1; when byte `0x49a884` is 0, words
   `0x49a87e` = `0x49a880` = 8000h, `0x49a882` = 0, the byte +1. Context 8.
2. `0x492b90(0x45ee00(0x49aa3e "data\script"), [0x4a09f0])` (the outer
   call's second slot pushed first, `[0x4a09f0]` read before the inner
   call; `POP ECX` = `0x49aa3e`), the stream `0x47ad40(EBP−0Ch, name)`, word
   `0x4a9e4c` = FFFFh, the 20-byte header `0x47af98(stream, 14h, EBP−8C4h)`.
3. Pass 1's lists are skipped: the count of each through the stream's
   virtual read +18h (`(stream, 2, buffer)`), then coordinates (12 × n in
   one read when n ≠ 0), angles (2 × n), routes (per route a count word,
   then 2 × k, read even for k = 0), regions (6 × n), triggers (74 bytes per
   record when n ≠ 0 and n > 0), timers (24 per record). The register
   pattern of each virtual call differs (EAX/EDX/ECX hold the vtable, the
   buffer and the stream in three permutations); the core follows each.
4. Word EBP−46h = 0; `0x40f998()`; M through the virtual read into EBP−38h.
5. **Mechs**, i = 0 while i < sx(M) + sx(word `0x4aaa5e`) (both reread;
   EAX = sx(N), ECX = the sum, EDX = sx(i) at the exit), EDI =
   `0x4aa72a` + i. Each pass: EBP−4Ch = [`0x4aaa60`] + 126 × sx(ordinal);
   for i < M the 134-byte record is read (selected or not). Unselected:
   next. Selected: for i ≥ M the type word EBP−93Ch ← MEC word +4 (CX).
   `pool_alloc([0x4a9b7a])` (`POP ECX` = the pool) into EBP−50h and
   EBP−10h; non-null: context 20h, `mech_ctor(A, type)` with the type in
   the low word of pool_alloc's EAX, entry EAX = that dword, EDX = ECX = A;
   count −7; context 14h. A null object faults (PROVED edge of `0x425690`).
   - MEC actor (i ≥ M): AX = the ordinal; when it equals word `0x4aaa64`,
     `[0x4a9b84]` = A and byte A+0A3h = 1 (through EBP−54h). Position and
     angles from the statics.
   - Authored: coordinate word +3Eh < 0: the statics; else the point
     12 × sx + [`0x4a9e0c`] (staged in EBP−60h), its x/y/z into
     EBP−970h/−96Ch/−968h, `terrain_height([0x4a0be0], EBP−970h)` (EAX =
     the point, EDX = the terrain, ECX = y), FFFF8000h becomes FFFF8001h,
     plus sx(word [[A+1F2h]+16h]), plus 5000 when word [[A+1F2h]+50h] ≠ 0
     (both pointers reread). Angle word +40h < 0: the statics; else words
     (0, 0, [[`0x4a9e10`] + 2 × sx]).
   - Items/secondary: MEC +8/+20h or the record EBP−93Ah/−8FAh (words
     +2Ah/+6Ah). Position A+26h (z at +8 first), angles A+0Ch, word A+32h
     = 0, `weapon_fit(A, items, secondary)` (EAX = items, EDX = A+0Ch, ECX
     = secondary).
   - MEC: `condition_init(A+206h, MEC+3Ch, +56h, +6Ah)` (EAX = +3Ch, EDX =
     A+206h, ECX = +56h), word A+29Ch = MEC word 0, the ordinal +1 (only
     here: an unselected MEC actor does not advance it).
   - Authored: byte A+97h = 1 when record word 0 ≠ 0; A+1B2h/A+1B6h = the
     trigger records 58 × sx + [`0x4a9e28`] of words +80h/+82h, or 0 for
     negative words; `action_copy(A, EBP−922h, EBP−90Eh)`;
     `initial_damage(A, word +84h)` (ECX = action_copy's ECX with that low
     word); word A+252h = word +2 when it is above −1.
6. `0x40fa88()`.
7. **Flyers** (92 bytes, selection `0x4aa856`): `pool_alloc([0x4a9db9])`,
   context 38h, `flyer_ctor(F, type +2Ch)` (EDX = pool_alloc's EDX with the
   type word, EAX = ECX = F), count −7, context 2Ch; a null flyer faults
   (PROVED edge of `0x425a5d`). Coordinate +28h (no ground height) and
   angle +2Ah or the statics, triggers +56h/+58h, `action_copy(F, +2Eh,
   +42h)`.
8. **Buildings** (52 bytes, selection `0x4aa8ba`, the factory byte from
   `0x4aa9d2` + i): `building_factory(EBP−0A0Ch, byte)` (EAX = the read's
   EAX with that low byte); with a coordinate (word +2 ≥ 0) the object's
   virtual +10h (`shape_word`, EAX = the point pointer, EDX = y), then
   `0x470ed4([0x4a0be0], EBP−0A18h, sx(word))` and `terrain_height` there
   (FFFF8000h becomes FFFF8001h, no offsets), else the statics; angle +4,
   triggers +2Eh/+30h, `action_copy(B, +6, +1Ah)`.
9. **Orders:** the count straight into word `0x4a9e2c`; when nonzero
   `new[](22 × n)` → `[0x4a9e30]` (`POP ECX` = the size, which the first
   read receives); per record while i < word `0x4a9e2c` (reread): words
   +0, +2, the scope +8 to +0, +2, +0Ch; the trigger (+0Ch), route (+6, 6 ×
   sx + [`0x4a9e18`]) and coordinate (+4) pointers to +12h, +8, +4 unless
   the word is −1; `subject_resolve(sx +0Ah, sx +8)` to +0Eh.
10. **Groups:** the count read here is only tested for 0; the loop runs
    over pass 1's group array (`[0x4a9e00]`, count word `0x4a9e04`, copied
    to EBP−0C4h and decremented after the test; it also ends at a null
    array). Per 156-byte record: group 0's twenty roster words become the
    remap bytes `0x4aa78e` + M + k for k < N (word `0x4aaa5e` reread),
    else −1; the other groups' non-negative slots go through the remap
    table of their class word +28h (0 `0x4aa78e`, 1 `0x4aa888`, 2
    `0x4aa946`; other classes unchanged). The ten order pointers (22 × sx +
    [`0x4a9e30`], 0 for −1). The heading word: angle +2Eh ≥ 0 from the
    angle table, else `heading_point([pts+4], [pts])` of the first order's
    route (+8 of the first order, which must exist: a null first order
    faults) when the route is not null and has at least two points, else
    0. Trigger +70h and coordinate +2Ch pointers (−1: 0; only −1 is
    tested). `group_ctor(G, EBP−0AC4h, trigger, EBP−0AECh, heading,
    point)` (EAX = G, EDX = the trigger, ECX = the record; the heading slot
    keeps ECX's high word). For class 2 with word 0 = 1, `base_marks` of
    the first member (`[[G+0Ch]]` when word G+10h > 0, else 0, which faults
    in `base_marks`).
11. **Objectives:** the count straight into word `0x4a9e3c`; when nonzero
    `new[](76 × n)` → `[0x4a9e40]`; per 54-byte record while i < word
    `0x4a9e3c` (reread): words +0, +2, the scope +4;
    `subject_resolve(sx +6, sx +4)` to +6; coordinate (+8) and route (+0Ah)
    pointers for words above −1 to +0Ah, +0Eh; `memcpy(+24h, rec+0Eh, 20)`,
    `memcpy(+38h, rec+22h, 20)`; the three dwords `[0x4aa660` + 4 × (index
    + j)]` (index +0Ch above −1, else 0) to +12h, +16h, +1Ah, and
    `0x49a870` to +1Eh.
12. `0x47129c([0x4a0be0])`; every building of the pool (`pool_prev`,
    oldest first) at `terrain_height` of its x/y (no adjustment), the point
    copied through EBP−0B30h and written back.
13. The trigger subjects (`[0x4a9e28]`, 58-byte records, count word
    `0x4a9e24` reread): the type word through the byte table `0x426492`:
    types 0..6 clear the subject dword +36h, types 7/8/9/10 resolve it
    with `subject_resolve(+36h, 1/2/3/0)`, other types (above 10 unsigned)
    leave it.
14. `0x47ad78(stream, 2)` (no delete). EDX:EAX = its result; ECX = the
    saved FS:0 head `[EBP−34h]`, which the epilogue stores to FS:0.

**Registers, stack and shims.** The core follows EAX/EDX/ECX through every
instruction, so each guest call gets the original's entry registers and
argument slots (`POP ECX` reloads the pushed value; none of the popped
callees writes its slot). The result registers are dead at `0x46182b`:
`0x44f6f4` loads EAX before `0x49630c`, which keeps EDX and ECX, and writes
both before reading them; the framework returns the entry ECX and the
adapter writes the head to FS:0. The frame lies in a zeroed scratch at the
original's offsets (`ES2_ACTOR_LOAD_FRAME`, the record through the bridge's
EBP). The guest callees receive the bridge's EBX/ESI/EDI instead of the
original's loop counter, object and flag pointer; the only callee known to
read one is `building_factory`'s case 0 (types 10, 53, 54 or above 3Dh: the
caller's EDI as the object), which no launch set contains (section 17); a
replaced `building_factory` passes EDI = 0. The guest callees run deeper
below the replaced frame; none reads stack residue that this would change
(`residue.py`, a linear scan, over the direct non-hook callees and the
stream helpers; the hooks' own contracts). The replaced frame also leaves
other bytes below the caller's ESP: `0x44f6f4`, called next, builds a
bitmap object at its EBP−1Ch whose flags byte +9 (EBP−13h) it only masks
(`AND F0h`, `OR 10h`), so bits 5–7 are residue (in the original bits 13–15
of this function's last flyer pointer EBP−14h), but nothing reads that
byte (the body reads +4, +6, +0Ah and +0Eh, the destructors `0x4815b8` and
`0x4838fd` +0Ah, +0Eh, +14h and +16h). The skip reads use the
2,232 bytes below the stream object; the largest in the launch sets is
1,008 (84 coordinates of C1_01). No entry or exit shim.

**Corrections to section 3:** the group loop runs over pass 1's array (the
count read in pass 2 only gates it); the trigger types 0..6 clear the
subject (as [mission.md](../formats/mission.md#trigger-table-and-actions)
has it); flyers get no ground height; a building's virtual +10h, terrain
marks and ground height happen only with a coordinate; an unselected MEC
actor keeps the MEC ordinal.

**Compare:** refused (G V I A: the stream, the allocations, the
constructors and the building virtual); the after-load snapshot is the
acceptance.

### In vitro (group `mission_actors`)

`invitro/mission_actors_cases.h`: 100,000 direct cases (quick tier; the
full tier, 1,001,024, not run) against logging
stubs for every game callee (27 stub ids; the stream's vtable and a
building vtable in the arena reach two of them; memcpy is the harness's
own) over a synthetic `script.dat` per case that the read stubs serve:
counts zero, small, negative (skipped reads), the selection, MEC and remap
tables, coordinates and angles present or −1, terrain heights of −32768,
trigger types −3..12, group classes 0..3 and −1, the static guard bytes
set or not. The core runs with its scratch frame at the original's frame
address (its own code on a separate stack), so the frame offsets of every
buffer and every register value are compared raw; the contents a callee
reads from the frame are logged as hashes. Compared: the pair, ECX, FS:0,
the ordered calls with argument slots, entry registers and the record's
context word, the arena and the image globals. The stubs change what the
original reads afterwards (N and the MEC table, the player ordinal, the
counter pointer, the coordinate, angle and trigger tables, the type words,
the points `terrain_height` and `0x470ed4` received, the terrain and pool
pointers, the member array, and the order and objective counts at a chosen
iteration, with the script holding the switched number of records). In
even cases the adapter is compared too (masked stack values) with the
callee counter deltas. 256 installed 31/a7 cases. One real-callee pass
(4,096 cases): the real `0x49630c`, `pool_alloc` on real MECH and FLYER
pools, `action_copy`, `subject_resolve`, `pool_nth`, `pool_prev` (with
`pool_next`) on three real pools, `heading_point` (with `vector_angle`),
the six nested hooks in all original/replaced combinations, the hook under
test original or replaced. 84 branch sites, 82 both ways, the taken edges
of `0x425690` and `0x425a5d` PROVED (a null object faults). One caught
mutation (`MUTATIONS.md`); twelve ad-hoc mutants of the core caught. Core
unit test `//core:mission_load_test` (one scenario worked out by hand: a
script with one coordinate, a route, a trigger record, an authored mech
with a coordinate and the MEC player, an unselected flyer, a building at
the defaults, an order, group 0, an objective and two runtime triggers;
54 calls checked).

### Live counts

`hk_actor_load_calls` = 1 in every suite, and once each per suite
`hk_actor_load_call_49630c`, `_45ee00`, `_492b90`, `_47ad40`, `_40f998`,
`_40fa88`, `_47129c`, `_47ad78`. The others from the launch sets
(`build\claude\h4-99\actor_counts.py`, the selection rule of section 2,
the suite-to-set mapping of section 17; the 19 suites on the installed
C1_01 use the lists of `c1_01-unarmed` with two MEC records):

| Suites (launch set) | `47af98` | `slot_18` | `471b30` | `415bb0`, `4175dc` | `46e180` | `415068` | `411b90` | `4178e8` | `4215f4` | `405314` | `slot_10`, `470ed4` | `4774f0` | `425348` | `492934` | `423b34` | `405db0` | `49680e` | `471c70` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| installed C1_01 (19 suites) | 138 | 21 | 14 | 10 | 50 | 2 | 62 | 8 | 4 | 50 | 0 | 2 | 15 | 14 | 38 | 2 | 2 | 51 |
| c1_01-unarmed-keys | 138 | 21 | 13 | 9 | 50 | 1 | 62 | 8 | 4 | 50 | 0 | 2 | 15 | 14 | 38 | 2 | 2 | 51 |
| c1_02-probe | 137 | 17 | 15 | 13 | 50 | 1 | 64 | 12 | 2 | 50 | 0 | 2 | 17 | 11 | 36 | 2 | 4 | 51 |
| c1_06-cheat-disable | 162 | 26 | 20 | 16 | 54 | 1 | 73 | 15 | 4 | 54 | 0 | 2 | 24 | 15 | 40 | 2 | 6 | 55 |
| c1_08-escort-route, c1_08-scan | 55 | 17 | 12 | 8 | 4 | 1 | 15 | 7 | 4 | 4 | 0 | 2 | 13 | 11 | 12 | 0 | 4 | 5 |
| c2_02-identify | 69 | 17 | 15 | 11 | 15 | 1 | 29 | 10 | 4 | 15 | 0 | 2 | 12 | 10 | 11 | 1 | 4 | 16 |
| the three C3_09 suites | 90 | 16 | 16 | 16 | 44 | 4 | 44 | 12 | 0 | 32 | 12 | 2 | 9 | 7 | 21 | 10 | 4 | 33 |
| the four C3_10 suites | 86 | 16 | 16 | 16 | 38 | 4 | 37 | 12 | 0 | 25 | 13 | 2 | 9 | 3 | 21 | 1 | 4 | 26 |
| c4_05-convoy | 66 | 20 | 15 | 13 | 2 | 1 | 16 | 12 | 2 | 2 | 0 | 2 | 13 | 9 | 12 | 0 | 6 | 3 |
| the three C4_08 suites | 83 | 17 | 12 | 11 | 27 | 1 | 38 | 10 | 1 | 27 | 0 | 2 | 10 | 5 | 15 | 1 | 2 | 28 |
| c4_10-probe | 71 | 17 | 11 | 9 | 24 | 1 | 34 | 8 | 2 | 24 | 0 | 2 | 11 | 7 | 13 | 1 | 8 | 25 |
| the two C5_05 suites | 105 | 20 | 17 | 15 | 31 | 4 | 44 | 11 | 2 | 31 | 0 | 2 | 10 | 8 | 29 | 1 | 4 | 32 |
| train1-timers | 52 | 13 | 1 | 1 | 11 | 1 | 11 | 0 | 0 | 11 | 0 | 2 | 2 | 1 | 3 | 1 | 2 | 12 |

They agree with the confirmed counts of the callees (sections 14, 16, 17:
`mech_ctor` = `weapon_fit`, `flyer_ctor`, `building_factory`,
`group_ctor`, `base_marks`, `action_copy`, `initial_damage`,
`condition_init`, `subject_resolve`). Every `*_skipped`/`*_bypassed` 0. The
counters of the nested hooks are unchanged (the bridge reaches them).

### After-load snapshot reach

A difference caused by the hook would lie in the actor pools and the actor
list, the order block `[0x4a9e30]`, the objective block `[0x4a9e40]`, the
group array and its member arrays, the trigger records' subjects, the
statics `0x49a871..0x49a884`, the counts `0x4a9e2c`/`0x4a9e3c`,
`0x4a9e4c`, `0x4a9b84`, or in the block pairing (the hook allocates the
same sizes in the same order through the same callees).

### Live result

`check-h499` (all 643 hooks replaced, own runtime): **40 of
40 identical**, every skipped/bypassed/mismatch counter 0;
`hk_actor_load_calls` 1 and `_call_49630c` 1 in every suite, and the callee
counters equal the table above (138 / 21 / 14 / 10 / 10 / 15 / 38 / 2 for
`_call_47af98` / `_slot_18` / `_call_471b30` / `_call_415bb0` /
`_call_4175dc` / `_call_425348` / `_call_423b34` / `_call_405db0` in the
C1_01 suites; 90 / 16 / 16 / 16 / 16 / 9 / 21 / 10 with `_slot_10` =
`_call_470ed4` = 12 in C3_09; 52 / 13 / 1 / 1 / 1 / 2 / 3 / 1 in
train1-timers). After-load snapshot `snapshot-r7d-20261013`: O1 = O2 and O2
vs R **identical** (scope all and pools) in acceptance-60s,
c3_10-loadout-hercs and c3_09-loadout-b.

### Open

- The bridged original callees without a verdict: `0x45ee00`, `0x492b90`,
  `0x47ad40`, `0x47af98`, `0x47ad78` (file), `0x40f998`, `0x40fa88`
  (resources); the runtime `0x49630c`, `0x4774f0`, `0x49680e` stay with the
  runtime library. `0x470ed4` and `0x47129c` are own code in sections 23
  and 27.
## 22. Damage data and part tables

The eleven functions of `damage.cpp`, read from the pinned
`llvm-objdump -d -M intel` listing of the reference DBSIM; the exception
descriptors `0x498840`/`0x498878`, the stream table `0x4a0e10` and the
initial globals were read from the file. The DMG grammar is
[collision.md](../formats/collision.md); the names below follow it. Own
code: adapter group `damage_data`, core appended to `core/src/damage.c`
(the readers use the same `es2_weapon_graph` bridge as
`es2_vehicle_part_registers` there). **Confirmed live:** `check-h499`;
after-load snapshot `snapshot-r7d-20261013` (section 14). Evidence
(scripts and logs, no listings): `build\claude\h4-113\`.

| Hook | VA | Role | Callers |
|---|---|---|---|
| `dmg_load` | `0x40d160` | the DMG file of a type (by name) into the next slot of `[0x4a9828]` | `mech_ctor` `0x415f98`, the FLYER type load `0x422ed0` (`0x422f4f`) |
| `dmg_read` | `0x40d09c` | the DMG reader: capacities, then the regions | `dmg_load` |
| `dmg_region_read` | `0x40cff8` | one 18-byte region: the 8-byte prefix, the link list, `+0Ch` = −1, `+0Eh` = 0 | `dmg_read`, the weapon-list entry `0x40f8bc` |
| `dmg_region_copy` | `0x40d050` | a region copied, with its own copy of the link list | `dmg_region_set` |
| `dmg_capacity_set` | `0x40d128` | n capacity words set by an index list | `weapon_fit` `0x4177c1` |
| `dmg_region_set` | `0x40d240` | n regions replaced by an index list (`dmg_region_copy`) | `weapon_fit` `0x4177a8` |
| `dmg_part_store` | `0x40d284` | the part word and center pointer of n regions (`+0Ch`, `+0Eh`) | `weapon_fit` `0x41781d` |
| `col_part_collect` | `0x40cb84` | the COL records' volume ids, center pointers and owner words into three buffers | `weapon_fit` `0x4177f8` |
| `col_part_gather` | `0x40cafc` | the same for one COL record | `col_part_collect` |
| `damage_state_ctor` | `0x40d2cc` | the actor's damage state: three word arrays and the two counts | `mech_ctor` `0x415c6b`, `flyer_ctor` `0x42169d` |
| `damage_state_bind` | `0x40d354` | DMG, COL and owner into the damage state; the shared 20-word block `[0x498864]` once | `mech_ctor` `0x415fd3`, `flyer_ctor` `0x42171c` |

`col_part_gather` reads a COL record of the collision data (section 20's
readers build it) but only gathers: no call, no allocation, three output
arrays. It stays with these hooks as the callee of `col_part_collect`.
Every other callee stays game code behind the guest bridge with its own
mode: the runtime (`0x49630c`, `new` `0x47749c`, `new[]` `0x4774f0`,
`_memcpy` `0x49680e`, `_memset` `0x49681a`), the VOL-aware stream
constructor `0x472f14`, the error report `0x479c78`, the stream's read
method (slot +18h; `0x4735b8` for the table `0x4a0e10`) and the hook
`stream_destroy` `0x472f64`. The eleven call each other only where the
original does (`dmg_load` → `dmg_read` → `dmg_region_read`,
`dmg_region_set` → `dmg_region_copy`, `col_part_collect` →
`col_part_gather`), through the bridge, so a nested hook keeps its mode.

### Call path and live visibility

All eleven run during the mission load only: `dmg_load` and the damage
state from the constructors (L5, section 17) and the FLYER type load of
pass 1, the rest from `weapon_fit` (L4, section 16) and the weapon list of
stage 2 (`0x40fc8c` → `0x40f8bc`, mission_load.md section 4). With early
arming (section 13, the default) the hooks are installed before the load.
No function has another call site and no dword of the file holds one of
the eleven addresses (scan of the listing for the eleven values).

### Contracts (from the bytes)

All eleven are cdecl, EDX:EAX is reproduced exactly (the core follows the
registers step by step, so a game call sees the original's entry
EAX/EDX/ECX). "sx" is the sign extension of a word; "n" counts compare
as signed words (`CMP r16,[...]` with `JL`/`JGE`), the loop counters are
32-bit registers. The words and pointers the loops test are reread from
memory on every pass where the original rereads them (noted), so a write
through an output that aliases them is observed in order.

**ECX is dead at every call site** (forward walk to the first write, both
arms of every branch), so the framework's entry ECX stands in for it:
`0x415f9d`/`0x422f54` `POP ECX`; `0x415fdb` `XOR ECX,ECX`; `0x421729`
`LEA ECX`; `0x415c70` passes ECX into `0x49667c`, which only stores it
into its own local (`PUSH ECX` at `0x49667f`, overwritten at `0x496694`)
and reloads ECX before the element constructor, and `0x4216a2` passes it
into `0x422e80` (no ECX use) before `POP ECX` at `0x4216bc`; `0x40f8cf`
passes it through `0x40cc14` into the read method `0x4735b8`, which only
stores it into its own local (`PUSH ECX`, overwritten at `0x4735f3` before
any read; the other paths never read it); in `weapon_fit` every call is
followed by an ECX write (`0x4177ad` … `0x417829`, and `0x4176dd` at the
loop head after `0x4177c1`); inside the group: after `0x40d113` the next
`dmg_region_read` passes it on to the read method (above), and after the
loop `0x40d1da` passes it into `stream_destroy`, which has no register
input (asset_loading.md; its replacement ignores the entry
registers); `0x40d26e` (`MOV ECX,[EDX]` before the next read) and
`0x40cbb8` (`MOVSX ECX,BX`). The core still reports the original's ECX
(in vitro compares it).

No function reads a local it did not write. The arguments `col_part_gather`
and `dmg_part_store` advance are their own stack slots (`ADD [EBP+0Ch],2`
…); the callers release them with `ADD ESP` without reading them, so the
replaced functions leave them unchanged. No entry or exit shim.

**`dmg_load` 0x40d160 (name)**, frame `add esp,-50h`: descriptor
`0x498840` (+4 = −50h) gives the runtime record at EBP−50h (context word
EBP−40h); `0x49630c` with EAX = the descriptor and the entry EDX/ECX. The
context word becomes 8. The stream S = EBP−30h (30h bytes: vtable +0,
state +4, handle +8, … up to +2Eh, all written by its constructor or the
open) is constructed by `0x472f14(S, name)` (EDX = S). If S+4 is nonzero
(the open failed), `0x479c78(0x6c56c8, 5, 0, 0x498858 "damage.cpp", 85h,
name)` with EDX = 0 (`SETE DL` of a nonzero EAX, `AND EDX,1`) and EAX =
S+4. The slot counter word `[0x498854]` is read into CX (ECX keeps its
high word), incremented in memory (reread), and the slot is
`12 × sx(counter) + [0x4a9828]` (the array is read after the increment);
`dmg_read(slot, S)` with EAX = S; `stream_destroy(S, 2)` with EAX = the
slot, EDX = S. Returns EAX = the slot, EDX = the record's saved head (the
epilogue writes it to FS:0; the adapter does that), ECX =
`stream_destroy`'s. The slot array is allocated by `0x40d218` (12 bytes ×
`[0x498856]`, the count that the type loads reserve through `0x40d200`);
`dmg_load` does not check the bound.

**`dmg_read` 0x40d09c (T, S)**, T the 12-byte DMG descriptor (regions
+0, capacities +4, region count +8, capacity count +0Ah). In order, each
read through S's table slot +18h (S, size, buffer) with EDX = S, ECX = the
table (reread from S each time): read(2) into T+0Ah (EAX = T+0Ah);
`new[](2 × sx(T+0Ah))` (ECX after it = the size), the block into T+4;
read(2 × sx(T+0Ah), block) with the count reread (EAX = the size);
read(2) into T+8 (EAX = T+8); `new[](18 × sx(T+8))` with EAX = sx, EDX =
18 × sx; the block into T+0; then `dmg_region_read(block + 18k, S)` with
EAX = S for k = 0, 1, … while k < sx(T+8) (reread every pass). Returns the
last `dmg_region_read`'s EAX/EDX/ECX, or for no region the block, `new[]`'s
EDX and ECX = 18 × sx(count).

**`dmg_region_read` 0x40cff8 (R, S)**: read(S, 8, R) with EAX = the table,
the entry EDX/ECX; L = the word R+6 (read once into DI); if L ≠ 0:
`new[](4 × sx(L))` (ECX = the size afterwards), the block into R+8, then
read(S, 4 × sx(R+6), [R+8]) with both reread, EDX = [R+8], ECX = the size
and EAX = the table reread from S. Then the word R+0Ch = FFFFh and the
dword R+0Eh = 0 (EDX = 0). Returns the last read's EAX and ECX, EDX = 0.

**`dmg_region_copy` 0x40d050 (D, R)**: `_memcpy(D, R, 12h)` with the entry
EAX/EDX/ECX (the `CMP WORD [D+6],0` before it is dead: its flags are never
tested; the core keeps the read); `new[](4 × sx(D+6))` (the copied link
count; ECX = the size afterwards); the block into D+8; `_memcpy(block,
[R+8], 4 × sx(D+6))` with D+6 reread, EAX = the size, EDX = [R+8]. Returns
the second `_memcpy`'s registers.

**`dmg_capacity_set` 0x40d128 (T, I, V, n)**: for k < n: the word V[k]
into capacity `[T+4]` + 2 × sx(I[k]) (I[k] read first, then [T+4] reread,
then V[k]). Returns EAX = the number of passes (0 for n ≤ 0), EDX = V + 2k,
ECX = I + 2k.

**`dmg_region_set` 0x40d240 (T, I, P, n)**: for k < n: `dmg_region_copy([T]
+ 18 × sx(I[k]), P[k])` (P[k] read first, then I[k], then [T] reread) with
EAX = sx(I[k]), EDX = 18 × sx(I[k]), ECX = the region. Returns the last
call's registers, or the entry registers for n ≤ 0.

**`dmg_part_store` 0x40d284 (T, n, I, W, Q)**: for k < n: region = [T] +
18 × sx(I[k]); the word region+0Ch = W[k], the dword region+0Eh = Q[k]
(read order I[k], Q[k], W[k], [T]). Returns EAX = the last region (entry
EAX for n ≤ 0), EDX = the number of passes, ECX = the last Q[k] (entry ECX
for n ≤ 0).

**`col_part_gather` 0x40cafc (C, I, O, P)**, C a 10-byte COL record (owner
word +0, count +4, entries +6, 16 bytes each): for k < [C+4] (reread every
pass): I[k] = entry word +6, P[k] = entry + 8, O[k] = the word C+0 (reread;
in this order). Returns EAX = the high word of entries + 16k with the low
word = C+4 (reread), EDX = C, ECX = k.

**`col_part_collect` 0x40cb84 (L, I, O, P)**, L a COL list (count word
+0, records +2, 10 bytes each): b = 0 (EBX, only BX changes); for k <
[L+0] (reread every pass): `col_part_gather(record k, I + 2 sx(b), O +
2 sx(b), P + 4 sx(b))` with EAX/EDX/ECX = the third, second and first
pointer; b += its AX (16 bits). Returns EAX = b (high word 0), EDX/ECX =
the last call's (the entry EDX/ECX when L is empty).

**`damage_state_ctor` 0x40d2cc (D, r, c)**, D the damage state (actor
+206h for a mech with r = 1Dh regions and c = 16h capacities, flyer +200h
with 1, 1); only the low words of r and c count. `new[](2 sx(c))` → D+0;
`new[](2 sx(r))` → D+4; `new[](2 sx(r))` → D+8 (EAX, EDX, ECX as the
original's: the size, the previous block, the previous size after
`POP ECX`); then `_memset([D], 0, 2 sx(c))`, `_memset([D+4], 0, 2 sx(r))`,
`_memset([D+8], 1, 2 sx(r))` with each pointer reread; the words D+18h = r,
D+1Ah = c. The third array is filled with bytes 01h, not zeroed (the verdict
row of function_verdicts.md said three zeroed arrays). Returns EAX = D, EDX/ECX = the last
`_memset`'s.

**`damage_state_bind` 0x40d354 (D, A, M, C)**, frame `add esp,-24h`:
descriptor `0x498878` (+4 = −24h), record at EBP−24h, context word
EBP−14h. D+0Ch = M (DMG), D+10h = C (COL), D+14h = A (owner). If the
dword `[0x498864]` is 0: `new(6)` (EAX = A, EDX = M, ECX = C; ECX = 6
after it) into EBP−4; if not null: context 14h, byte +4 = 14h (CL),
`new[](28h)` (EDX = the object, ECX = 14h), the block into +0, byte +5 = 0,
the construction count `[[0x6cbee4]]` incremented and decremented (the
pointer reread for each), context 8; `[0x498864]` = the object (or 0 when
`new` failed, so the next bind tries again). Returns EAX = A (block
present), 0 (`new` failed) or the object; EDX = the record's saved head
(FS:0, by the adapter); ECX = C, 6 or `[0x6cbee4]`.

### In vitro (group `damage_data`)

`invitro/damage_data_cases.h`: per hook 100,000 quick direct cases
(1,001,024 in the full tier) against logging stubs for every direct
callee: `49630c` (a record stub: the real FS:0 head at record+0, context
word zeroed, linked into FS:0), `0x472f14` (writes the stream's table and
a state that is nonzero in one case of four), `0x479c78`, `dmg_read`,
`stream_destroy`, `new[]`, `dmg_region_read`, `new`, `_memset`, `_memcpy`
(both perform their fill/copy inside the arena), `dmg_region_copy`,
`col_part_gather` and two arena tables whose slot +18h is a read stub
(writes bounded data, sometimes changes the stream's table). Compared: the
full return pair, the core's ECX, FS:0, the ordered calls with their
argument slots (frame addresses of `dmg_load` relative to its record),
entry EAX/EDX/ECX and the record's context word, the arena and the globals
`0x498854`, `0x4a9828`, `0x498864`, `0x6cbee4`. Objects overlap (output
arrays over the index lists and over the COL record's owner and count
words, regions over the index lists, `new[]` blocks over the descriptor),
so the order of reads and writes is observable (stubs also rewrite count
words, the region count, the region array pointer and the list count
that the originals reread). The adapter is compared in every case (with
the callee counter deltas). 256 installed 31/a7 cases per hook (fast and
preserving dispatch). Real-callee passes (2,048 cases) for the eight hooks
with callees: the real `49630c`, `_memcpy` and `_memset`, a bump allocator
for `new`/`new[]`, `0x472f14`, `0x479c78` and `stream_destroy` as stubs,
the nested hooks of the group (`dmg_read` and `dmg_region_read` under
`dmg_load`, `dmg_region_read` under `dmg_read`, `dmg_region_copy` under
`dmg_region_set`, `col_part_gather` under `col_part_collect`) in rotating
original/replaced modes, the hook under test original or replaced against
a run without it. 13 branch sites, all both ways (`dmg_region_copy` and
`damage_state_ctor` have none), no PROVED edge. One caught mutation per
hook (`MUTATIONS.md`). Core unit test `//core:damage_data_test`
(hand-worked scenarios). Quick-tier log: `differential_quick.log` in the
evidence directory (all eleven PASS, the integration pass with every hook
installed replaced included).

### Live counts

The map `dbsim-map-20261012` (every hook original, early arming, all 40
suites) counts the entries of the eleven functions; with the hooks
replaced the callers reach them the same number of times, so these are the
expected `hk_<name>_calls` (`map_counts.py` in the evidence directory):

| Suites | `dmg_load` | `dmg_read` | `dmg_region_read` | `damage_state_ctor` | `damage_state_bind` | `dmg_region_copy` | `dmg_region_set` | `dmg_capacity_set` | `col_part_collect` | `dmg_part_store` | `col_part_gather` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| the 19 suites on the installed C1_01 (acceptance-60s, c1_01-*) | 11 | 11 | 324 | 14 | 14 | 31 | 31 | 31 | 10 | 10 | 66 |
| c1_01-unarmed-keys | 10 | 10 | 295 | 13 | 13 | 28 | 28 | 28 | 9 | 9 | 60 |
| c1_02-probe | 14 | 14 | 411 | 15 | 15 | 38 | 38 | 38 | 13 | 13 | 88 |
| c1_06-cheat-disable | 17 | 17 | 498 | 20 | 20 | 65 | 65 | 65 | 16 | 16 | 96 |
| c1_08-escort-route, c1_08-scan | 9 | 9 | 266 | 12 | 12 | 36 | 36 | 36 | 8 | 8 | 48 |
| c2_02-identify | 12 | 12 | 353 | 15 | 15 | 38 | 38 | 38 | 11 | 11 | 78 |
| c3_09-loadout-b, -orders-matrix, -wingman-orders | 16 | 16 | 497 | 16 | 16 | 98 | 98 | 98 | 16 | 16 | 96 |
| c3_10-loadout-hercs, -detail-options, -hunt-targ, -own-building-fire | 16 | 16 | 497 | 16 | 16 | 79 | 79 | 79 | 16 | 16 | 96 |
| c4_05-convoy | 14 | 14 | 411 | 15 | 15 | 53 | 53 | 53 | 13 | 13 | 86 |
| c4_08-joystick, -razor-plasma, -slope-dive | 12 | 12 | 353 | 12 | 12 | 55 | 55 | 55 | 11 | 11 | 61 |
| c4_10-probe | 10 | 10 | 295 | 11 | 11 | 31 | 31 | 31 | 9 | 9 | 68 |
| c5_05-moon-missiles, c5_05-orders-flyer | 16 | 16 | 469 | 17 | 17 | 94 | 94 | 94 | 15 | 15 | 90 |
| train1-timers | 1 | 1 | 62 | 1 | 1 | 3 | 3 | 3 | 1 | 1 | 6 |

The identities behind them hold in every suite of the map: `dmg_load` =
`mech_ctor` + the FLYER type load `0x422ed0` (1 where a flyer type is
loaded); `damage_state_ctor` = `damage_state_bind` = `mech_ctor` +
`flyer_ctor`; `col_part_collect` = `dmg_part_store` = `weapon_fit`;
`dmg_region_set` = `dmg_capacity_set` = `dmg_region_copy` (one weapon each,
n = 1 at both call sites); `dmg_region_read` = the regions of the loaded
DMG files + 33 weapon-list entries (`0x40f8bc`, 33 in every suite).

Callee counters per suite: `hk_dmg_load_call_49630c` = `_call_472f14` =
`_call_40d09c` = `_call_472f64` = `hk_dmg_load_calls`, `_call_479c78` = 0
(every DMG file opens); `hk_dmg_read_slot_18` = 3 × calls,
`_call_4774f0` = 2 × calls, `_call_40cff8` = `dmg_region_read` − 33;
`hk_dmg_region_set_call_40d050` = calls; `hk_dmg_region_copy_call_49680e`
= 2 × calls, `_call_4774f0` = calls; `hk_col_part_collect_call_40cafc` =
`col_part_gather`'s calls; `hk_damage_state_ctor_call_4774f0` =
`_call_49681a` = 3 × calls; `hk_damage_state_bind_call_49630c` = calls,
`_call_47749c` = `_call_4774f0` = 1 (the shared block: `[0x498864]` is 0
in the file, written only at `0x40d3d6`). `hk_dmg_region_read_slot_18` and
`_call_4774f0` (regions with links) depend on the DMG files and were not
predicted. Every `*_skipped`/`*_bypassed` 0. The callers' counters
(`hk_mech_ctor_call_40d160`, `_40d2cc`, `_40d354`, `hk_flyer_ctor_call_*`,
`hk_weapon_fit_call_40d240`, `_40d128`, `_40cb84`, `_40d284`) are
unchanged; `hk_stream_destroy_calls` is unchanged (the bridge still reaches
it).

### After-load snapshot reach

A difference caused by these hooks would lie in the DMG descriptors (the
slot array `[0x4a9828]`, their region and capacity blocks and link lists),
the damage states (actor +206h/+200h and their three arrays), the shared
block `[0x498864]`, the region words +0Ch/+0Eh that `dmg_part_store`
writes, or in the block pairing (the hooks allocate the same sizes in the
same order through the same `new`/`new[]`).

### Live result

`check-h499` (all 643 hooks replaced, own runtime): **40 of
40 identical**; the eleven counters equal the table above in every suite
(11 / 11 / 324 / 31 / 31 / 31 / 10 / 10 / 66 / 14 / 14 in the C1_01
suites, 16 / 16 / 497 / 98 / 98 / 98 / 16 / 16 / 96 / 16 / 16 in C3_09, 1 /
1 / 62 / 3 / 3 / 3 / 1 / 1 / 6 / 1 / 1 in train1-timers). After-load
snapshot `snapshot-r7d-20261013` (section 21): identical.

### Open

- `hk_dmg_region_read_slot_18` and `_call_4774f0` per suite (the link
  counts of the DMG files and of WEAPONS.DAT's entries).
- The error path of `dmg_load` (`0x479c78`, a missing DMG file) is covered
  in vitro only.
- The neighbours `0x40d200`/`0x40d218` (slot reservation and the slot
  array) and `0x40cbd4`/`0x40cbec` (the same for COL) stay original
  (sections 26 and 29 call them).
## 23. Group joins and the building footprint

The three join methods that `group_ctor` (section 17) called as original
code, the building footprint marks and the cell flag writer, read from the
pinned `llvm-objdump -d -M intel` listing of the reference DBSIM. The
verdicts of function_verdicts.md hold. Own code: adapter group `group_joins`,
core module `core/src/group_joins.c`. **Confirmed live:** `check-h4109`;
after-load snapshot `snapshot-r7e-20261013` (section 14). Evidence
(scripts and logs, no listings): `build\claude\h4-109\`.

| Hook | VA | Call sites | Does |
|---|---|---|---|
| `mech_join` | `0x417aa8` | `0x423c6c` (`group_ctor`, class 0) | stores the group and slot in the actor, places a MECH still at the load's defaults |
| `flyer_join` | `0x421ee8` | `0x423cc3` (class 1) | the same for a FLYER, without the height term |
| `building_join` | `0x405c3c` | `0x423d1e` (class 2) | the same for a building, with the base layout's angle and grid and the footprint |
| `footprint_marks` | `0x470ed4` | `0x405d79` (`building_join`), `0x425d08` (the actor load: buildings with a coordinate) | sets the cell flags of the terrain cells within a radius of a point |
| `cell_flag_set` | `0x470ddc` | `0x470ed4` (5 sites), `0x47108c`, `0x4711da` (the flattening, section 27), `0x46e61a` (the per-frame cell clear `0x46e5a4`, H3) | one cell flag byte |

Their callees are all hooks already: `route_point`, `terrain_height`,
`dist_gate`, the formation slot +78h (`formation_mech` `0x417898`,
`formation_flyer` `0x421e98`, `formation_base` `0x405c04`) and slot +10h
(`shape_word` `0x46b910`); `footprint_marks` and `cell_flag_set` call each
other's hook. They stay calls through the guest bridge with their own
modes. No bridged original callee without a verdict remains.
`cell_flag_set` is long enough for a hook (34 bytes; `push ebp`, `mov
ebp,esp`, `mov eax,[ebp+8]` relocated), so it is a hook and not computed
inside `footprint_marks`.

### Call path and live visibility

The joins and `footprint_marks` run during the load only: with early
arming (section 13, the default) their hooks are installed before the load
and their counters are constant from the first tick record on.
`cell_flag_set` also runs every frame under the render root (the cell clear
`0x46e5a4` after the cell's objects are drawn), so its counter grows
during the run.

### Contracts (from the bytes)

All five are cdecl and return EDX:EAX as below; "sx" is the sign
extension of a word, "lo(R, w)" a 16-bit load into R that keeps R's high
word.

**`mech_join` 0x417aa8 (A, G, slot)**, frame `add esp,-14h`: the 12-byte
vector v at EBP−14h (ESI) and the angle staging dword EBP−8 plus word
EBP−4 (no callee sees it).

1. [A+45h] = G (EAX = G); word [A+49h] = the slot word (EDX = lo(entry
   EDX, slot)).
2. When word [A+0Ch] = 8000h (the load's default angle): ECX = [A+45h],
   AX = word [ECX+0Ah] (the group's heading); dword [A+0Ch] = 0, word
   [A+10h] = the heading, word [A+32h] = 0 (the matrix is stale). EAX =
   A+0Ch, EDX = 0:heading.
3. When [A+2Eh] = FFFF8000h (the default height): EAX = [A+45h], EDX =
   [EAX+18h] (the group point). Not null: v = the point, read +8, +0, +4
   (EDX, ECX, EAX in that order) and written in the same order. Null:
   `route_point(G+4, EDX)` with EAX = G+4, EDX = 0:word [G+4], ECX as
   left (G after step 2, else the entry ECX); v = the returned point,
   read +8 (ECX), +0 (EDX), +4 (EAX).
4. `terrain_height([4A0BE0h], v)` (EAX = v.y, EDX = v.x or v.z, ECX = the
   terrain); v.z = its EAX; EAX = T = [A+1F2h] (the type record, reread),
   EDX = sx(word [T+16h]), v.z += EDX; when word [T+50h] is not 0, v.z +=
   1388h.
5. The formation slot: `[[A]+78h](A, v)` (EAX = T, EDX = sx(+16h), ECX =
   the vtable).
6. A+26h = v: EAX = A+26h; [A+2Eh] = v.z (EDX), [A+26h] = v.x (ECX),
   [A+2Ah] = v.y (EDX). EDX:EAX = v.y : A+26h, ECX = v.x.

Without step 3 EDX:EAX is that of step 2, or G and lo(entry EDX, slot).

**`flyer_join` 0x421ee8 (F, G, slot)**: steps 1 to 3, 5 and 6 of
`mech_join` (byte for byte the same code; at the slot call EAX = v.y, EDX
= v.x or v.z), then on every path: when [F+2Eh] is 0 it becomes 1388h.

**`building_join` 0x405c3c (B, G, slot, snap)**, frame `add esp,-18h`: v
at EBP−18h, the grid's x' at EBP−0Ch meanwhile.

1. As `mech_join`.
2. When word [B+0Ch] = 8000h: ECX = [B+45h], EAX = sx(word [ECX+0Ah]);
   when the slot word (DX) is not 0, EAX += sx(word [[L0+4] + 10 × sx(slot)
   − 2]) with L0 = [4A95ACh] + 1Ch × sx(word [G+2]) (the base layout of
   the group; ECX ends as that sign-extended word; ESI, which held the
   table, is restored at the return). The words as in `mech_join` with this angle.
3. When [B+2Eh] = FFFF8000h: the point as in `mech_join` but with other
   registers (point: ECX = z, EDX = x, EAX = y; `route_point`'s second
   slot is ECX = lo(ECX, word [G+4]), whose high word is that of step 2's
   ECX or the entry ECX; its point: EDX = z, ECX = x, EAX = y).
4. When the dword snap is not 0 (`group_ctor` passes sx(word R+0), the
   record's first word): L = [4A95ACh] + 1Ch × sx(word [G+2]) (G reread
   from B+45h, the table reread); s = [L+14h] << 5, m = (s << 8) − 1; x'
   = (v.x AND NOT m) + [L+0Ch] × s; y' = (v.y AND NOT m) + m + 1 −
   low([L+10h] × s) (one-operand IMUL: EDX:EAX = the signed product); v.x
   = x' (EDX), v.y = y' (EAX). Every product and sum is a 32-bit
   wrap-around value.
5. `[[B]+78h](B, v)` (EAX = the vtable, ECX = v); `[[B]+10h](B)` (EDX =
   the vtable reread; `pop ecx` = B); ECX = sx(AX), the shape word (the
   radius).
6. `footprint_marks([4A0BE0h], v, ECX)` (EAX = v, EDX = the terrain);
   `terrain_height([4A0BE0h], v)` (EAX = the terrain, ECX = v, EDX =
   `footprint_marks`' EDX).
7. z = its EAX: v.z = z, [B+2Eh] = z, [B+26h] = v.x (EDX), [B+2Ah] = v.y
   (ECX). EDX:EAX = v.x : B+26h, ECX = v.y.

**`footprint_marks` 0x470ed4 (T, P, r)**, frame `add esp,-1Ch`: the box
bounds at EBP−4 .. −10h (no callee sees them) and the corner point (x, y,
z) at EBP−1Ch.

1. Reads [P] (x), [T+108h] (s), [P+4] (y), [P+8] (z, to the point's z);
   x0 = (x − r) SAR s, x1 = (x + r) SAR s, y0 = (y − r) SAR s, y1 = (y +
   r) SAR s (CL = the low byte of s, so the low five bits count).
2. `cell_flag_set(T, x SAR s, y SAR s, 1)` (EAX = ECX = s, EDX = x SAR s).
3. For row j = y0 .. y1 and column i = x0 .. x1 (signed, inclusive; empty
   when the start is above the end; [T+108h] reread for every row and
   every column): the point = (i << s, j << s, z); `dist_gate(point, P)`
   (EAX = the point, EDX = P, ECX = [T+108h]); when its EAX (signed) is
   at most r: `cell_flag_set(T, i, j, 1)`; for i > 0 also (i − 1, j)
   (EDX = i − 1) and, for j > 0, (i − 1, j − 1) (EAX = i − 1, ECX = j −
   1); for j > 0 also (i, j − 1) (EDX = j − 1). EDX:EAX and ECX are those
   of the last call, or after the loops' shifts (EAX = j << s for an empty
   column range).

**`cell_flag_set` 0x470ddc (T, x, y, v)**: c = [T+100h], then the cells
[T+0F0h]; byte [cells + (y << c) + x] = v (SHL by CL). EAX = the cells,
EDX = the index, ECX = c with v in its low byte.

**Callers' registers.** ECX is dead after every call site: `group_ctor`
loads ECX before reading it in each loop and returns after the last join
with ECX dead at `0x4261ea` (section 17); after `0x405d79` comes `lea
ecx`; after `0x425d08` `terrain_height`, which writes ECX before reading it
(`0x46e18f`); after `cell_flag_set`, `0x46e5a4` pops ECX in its epilogue,
`0x47108c` clears it, `0x4711da` loads it, and the five sites in
`0x470ed4` write it before reading it or return it as `0x470ed4`'s dead
ECX. So the framework's entry ECX stands in for it. No function reads a
local it did not write; the formation slots, `dist_gate` and
`terrain_height` read only the 12 bytes of the vector they receive. No
entry or exit shim. The adapters keep the vector in a zeroed scratch.

**Compare:** refused for all five (compare_mode.md): the joins and
`footprint_marks` call game code through the bridge that writes the actor
and the terrain cells; `cell_flag_set` writes one byte at a runtime index
(effort M: a runtime-indexed region).

### In vitro (group `group_joins`)

`invitro/group_joins_cases.h`: per hook 100,000 direct cases in the quick
tier (1,001,024 in the full tier, not run)
against logging stubs for every game callee (`route_point`,
`terrain_height`, `dist_gate`, `cell_flag_set`, `footprint_marks` patched;
the slots +78h and +10h in two arena vtables): the original called with
four slots and EAX/EDX/ECX over a stack filled with a case pattern, the
core with the ECX it reports. Compared: the pair, ECX, the ordered calls
with argument slots (and the bridge's slot count), entry registers (stack
addresses masked) and the vector by content, the 16 KiB arena and the
image globals (terrain pointer, base layouts, formation tables and their
static buffers). The stubs change what the originals reread (the vector
after `terrain_height`, the slot +78h and `footprint_marks`; the vtable
pointer; the type record and its word +50h; the terrain pointer; the
group's layout word; [T+108h] after `dist_gate` and `cell_flag_set`). In
even cases (the real callers' inputs: slot words 0..19, snap 0 or 1) the
adapter is compared too, with the callee counter deltas. 256 installed
31/a7 cases per hook. Real-callee passes (4,096 cases per hook except
`cell_flag_set`, a leaf): `mech_join` over the real `route_point`,
`terrain_height` (an 8 × 8 grid) and `formation_mech` (its table, the
leader's matrix through the real `euler_matrix` and `rotate_2d` as image
code, the static buffers); `flyer_join` over `route_point` and
`formation_flyer`; `building_join` over `route_point`, `terrain_height`,
`formation_base`, `shape_word`, `footprint_marks`, `dist_gate` and
`cell_flag_set` (cell flags in the arena); `footprint_marks` over
`dist_gate` and `cell_flag_set`; the nested hooks in all
original/replaced combinations, the hook under test original or replaced.
21 branch sites, all both ways (`cell_flag_set` has none). One caught
mutation per hook (MUTATIONS.md) and three of four ad-hoc core mutants
(the fourth equivalent). Core unit test `//core:group_joins_test`
(scenarios worked out by hand: a MECH with a group point and with a route,
a placed MECH, a FLYER lifted to 5000, a building with a layout angle,
route and grid and one without, footprints of 12 cells, a single cell, an
empty box, and two cell flag writes).

### Live counts

From the launch sets (`build\claude\h4-109\join_counts.py`): a join is
called per member as section 17 counts them (`hk_group_ctor_call_417aa8`
etc. of `check-r7-3b`); it places (route_point when the group has no point,
record word +2Ch < 0; the slot +78h; MECH and building `terrain_height`;
building +10h and `footprint_marks`) only members still at the default
height: every MEC actor and the authored members with a negative
coordinate index (mech +3Eh, flyer +28h, building +2), once per actor.
`footprint_marks` = the building placements + the actor load's buildings
with a coordinate (`0x425d08`). Checks: these totals equal the entry
counts of `0x470ed4` in `dbsim-map-20261012` in all 40 suites, and the
building placements equal `hk_formation_base_calls` of `check-r7-3b` in
the suites where the slot +78h of a building is its only caller (C1_08,
C2_02, C3_09, C3_10, C4_05, C4_08, C4_10, C5_05, train1).

| Suites (launch set) | `mech_join` | `flyer_join` | `building_join` | `footprint_marks` |
|---|---:|---:|---:|---:|
| the 19 suites on the installed C1_01 | 10 | 4 | 50 | 50 |
| c1_01-unarmed-keys | 9 | 4 | 50 | 50 |
| c1_02-probe | 13 | 2 | 50 | 50 |
| c1_06-cheat-disable | 16 | 4 | 54 | 54 |
| c1_08-escort-route, c1_08-scan | 8 | 4 | 4 | 4 |
| c2_02-identify | 11 | 4 | 15 | 15 |
| c3_09-loadout-b, -wingman-orders, -orders-matrix | 16 | 0 | 32 | 32 |
| c3_10-loadout-hercs, -own-building-fire, -detail-options, -hunt-targ | 16 | 0 | 25 | 25 |
| c4_05-convoy | 13 | 2 | 2 | 2 |
| c4_08-razor-plasma, -slope-dive, -joystick | 11 | 1 | 27 | 27 |
| c4_10-probe | 9 | 2 | 24 | 24 |
| c5_05-moon-missiles, -orders-flyer | 15 | 2 | 31 | 31 |
| train1-timers | 1 | 0 | 11 | 11 |

`hk_cell_flag_set_calls` is nonzero in every suite and grows per frame
(the map's probe saturates at 1000 in all 40 suites); it is at least the
load's marks and is not predicted exactly.

Callee counters (`hk_<hook>_<suffix>`):

| Counter | inst. C1_01 | c1_01-unarmed | c1_02 | c1_06 | c1_08 | c2_02 | c3_09 | c3_10 | c4_05 | c4_08 | c4_10 | c5_05 | train1 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `mech_join_call_423b0c` | 10 | 9 | 13 | 16 | 8 | 11 | 16 | 12 | 10 | 4 | 9 | 15 | 1 |
| `mech_join_call_46e180`, `_slot_78` | 10 | 9 | 13 | 16 | 8 | 11 | 16 | 16 | 13 | 11 | 9 | 15 | 1 |
| `flyer_join_call_423b0c` | 4 | 4 | 0 | 4 | 4 | 4 | 0 | 0 | 2 | 1 | 2 | 2 | 0 |
| `flyer_join_slot_78` | 4 | 4 | 2 | 4 | 4 | 4 | 0 | 0 | 2 | 1 | 2 | 2 | 0 |
| `building_join_call_423b0c` | 4 | 4 | 4 | 8 | 0 | 0 | 0 | 0 | 0 | 3 | 0 | 0 | 0 |
| `building_join_slot_78`, `_slot_10`, `_call_470ed4`, `_call_46e180` | 50 | 50 | 50 | 54 | 4 | 15 | 20 | 12 | 2 | 27 | 24 | 31 | 11 |

`footprint_marks_call_470ddc` and `_call_49288c` depend on the radii and
positions (cells in the box) and were not predicted; both are nonzero
wherever `footprint_marks` runs. Every `*_skipped`/`*_bypassed` 0. The
hook counters of `group_ctor` (its `_call_417aa8`/`_421ee8`/`_405c3c`
equal the three joins' calls), `route_point`, `terrain_height`,
`dist_gate`, `shape_word` and `formation_*` are unchanged (the bridge still
reaches them).

### After-load snapshot reach

A difference caused by these hooks would lie in an actor pool block
(positions A+26h..31h, angles A+0Ch..11h, the matrix flag A+32h, the group
and slot A+45h..4Ah), in the terrain's cell flag array [[4A0BE0h]+0F0h],
or in the formation slots' static buffers `0x49a430`/`0x4975d6`.

### Live result

`check-h4109` (all 657 hooks replaced, own runtime): **40 of
40 identical**, every skipped/bypassed/mismatch counter 0; `mech_join` /
`flyer_join` / `building_join` / `footprint_marks` equal the table above in
every suite (10 / 4 / 50 / 50 in the C1_01 suites, 16 / 0 / 32 / 32 in
C3_09, 16 / 0 / 25 / 25 in C3_10, 1 / 0 / 11 / 11 in train1-timers);
`cell_flag_set` is nonzero everywhere (1,670 in train1-timers … 132,745 in
c3_09-loadout-b: the per-frame cell clear `0x46e5a4` calls it). After-load
snapshot `snapshot-r7e-20261013`: O1 = O2 and O2 vs R **identical** (scope
all and pools) in acceptance-60s, c3_10-loadout-hercs and
c3_09-loadout-b.

### Open

- The cell counts of `footprint_marks` (its callee counters) per suite.
- `cell_flag_set`'s per-frame callers stay original (the H3 cell clear
  `0x46e5a4`) or are own code in section 27 (`0x470fe8`, `0x471128`).
## 24. Terrain zone load

The contracts, the in-vitro evidence and the data facts are in
[terrain.md](terrain.md#the-terrain-zone-load); this
section holds the load-side view, the live counts and the snapshot reach.
**Confirmed live:** `check-h4110`; after-load snapshot
`snapshot-r7f-20261013` (section 14). Evidence:
`build\claude\h4-110\` (`load_counts.py`, `zone_counts.py`, `bforms.py`,
`cfg.py` and their outputs; no listings).

Six hooks in the group `terrain_load`: `zone_load` `0x42789c`,
`grid_ctor` `0x46befc`, `height_cache_ctor` `0x495268`,
`terrain_materials` `0x46c754`, `terrain_cells` `0x46c4c4`,
`cell_mark_set` `0x470d74`. Step 10 of pass 1 (section 2):
`0x424308` calls `zone_load(word 0x4a9e50, word 0x4a9e4e)` at `0x4252f7`
(the second argument is not read); the zone file (`dat\zone<N>.dat`: two
unused dwords, the cell shift, the height scale), the grid object, the
materials (`dat\mat0.dat`) and the cells from the first bitmap of
`dba\zone<N>.dba` with one stream-A draw per block, then the zone
transform `0x4aaa84` (`euler_matrix`) and the terrain pointer `[0x4a0be0]`.
Corrections to section 2: the object table `0x4aaa74`/`0x4aaa78`/word
`0x4aaa7c` is written by `0x42789c` only for a negative zone word (cleared);
the RNG draws of section 6's first row are those of `0x46c4c4` alone
(`0x46c754`'s draw site `0x46cac0` is in its text-grid path, which no
suite runs).

### Live counts

From the map `dbsim-map-20261012` (every hook original, early arming,
all 40 suites): `0x42789c`, `0x46befc`, `0x495268`, `0x46c754` and
`0x46c4c4` are entered exactly once in every suite; `0x470d74` reads 1,000
(the cap). Its count is the cells (one call per cell from `0x46c4c4`) plus
the base marks (one call per nonzero layout byte from `0x47136c`, which
`base_marks` calls for each base group whose form has a material index):
`build\claude\h4-110\load_counts.py` over the launch sets' `script.dat`
(the zone is header word 1; the installed C1_01 from the launcher
chain's `data-backup`), the zone DBA's dimensions and `dat\BFORMS.DAT` (17 forms;
forms 5, 7..16 have marks). Its `base_marks` and `0x47136c` counts equal
the map's in every launch set (2/2/2/2/0/1/10/1/0/1/1/1/1 and
2/2/2/2/0/1/2/1/0/1/1/1/1).

| Suites (launch set) | zone | `cell_mark_set` | `hk_terrain_cells_call_470d74` | `hk_terrain_cells_call_492ee0` |
|---|---:|---:|---:|---:|
| the 19 suites on the installed C1_01 (acceptance-60s, c1_01-success, -collision, -cockpit-keys, -free-camera, -mouse, -keys, -cheat-destroy, -cheat-disable-leg, -cheat-leader, -cheat-keys, -cheat-keys-plain, -options-dialog, -keys-extra, -tape-record, -joystick, -cheat-disable-late, -display-keys, -quit-dialog) | 123 | 65,688 | 65,536 | 16,384 |
| c1_01-unarmed-keys, c1_02-probe, c1_06-cheat-disable | 123 | 65,688 | 65,536 | 16,384 |
| c1_08-escort-route, c1_08-scan | 123 | 65,536 | 65,536 | 16,384 |
| c2_02-identify | 22 | 16,408 | 16,384 | 1,024 |
| c3_09-loadout-b, -wingman-orders, -orders-matrix | 39 | 16,425 | 16,384 | 1,024 |
| c3_10-loadout-hercs, -own-building-fire, -detail-options, -hunt-targ | 3333 | 16,416 | 16,384 | 1,024 |
| c4_05-convoy | 405 | 16,384 | 16,384 | 4,096 |
| c4_08-razor-plasma, -slope-dive, -joystick; c4_10-probe | 408 | 16,394 | 16,384 | 4,096 |
| c5_05-moon-missiles, c5_05-orders-flyer | 501 | 16,394 | 16,384 | 4,096 |
| train1-timers | 111 | 16,416 | 16,384 | 4,096 |

`hk_zone_load_calls`, `hk_grid_ctor_calls`, `hk_height_cache_ctor_calls`,
`hk_terrain_materials_calls` and `hk_terrain_cells_calls` are 1 in every
suite. Callee counters per call (all suites): `zone_load` one
`call_49630c`, `_496820`, `_472f14`, `_47749c`, `_46befc`, `_46c754`,
`_47ebb8`, `_472f64`, two `call_492bec`, four `slot_18`, `call_479c78` 0;
`grid_ctor` one `call_49667c`, `_495268`; `height_cache_ctor` one
`call_4774f0`, two `call_49667c`; `terrain_materials` one `call_49630c`,
`_492bec`, `_4774f0`, `_4967a2`, `_496868`, `_47a144`, `_46c4c4`,
`_496406`, two `call_472f14`, `_472f64` and `slot_18` (the material count
read and the bitmap's destructor), 13 `call_4735b8` (MAT0's 13 records),
the text-grid counters (`_496802`, `_4967b4`, `_49667c`, `_492ee0`,
`_496838`, `_4952e8`, `_4952fc`) 0; `terrain_cells` one `call_49667c`,
`_4774f0`, `_4952e8`, `_4952fc` and the two columns above. Every
`*_skipped`/`*_bypassed` 0. Unchanged, because the bridge still reaches
them: `hk_rng_next_*` (the terrain draws above stay inside the counter of
`check-r7-3b`), `hk_euler_matrix_calls`, `hk_stream_read_calls`,
`hk_dts_load_calls`, `hk_stream_destroy_calls`, `hk_base_marks_*`.

### After-load snapshot reach

The grid is reached: the fixed region `0x4a0be0` points at G's heap block,
and G's dwords reach the cells `[G+0ECh]`, the marks `[G+0F0h]`, the
material table `[G+121h]` (an unaligned window, which the pairing follows)
and the height cache's arrays `[G+0E0h]`, `[G+0E4h]`, `[G+0E8h]` (new[] goes
to the CW3220 heap unless `-l` sets the arena threshold). Not reached: the
zone transform `0x4aaa84..0x4aaaa3`, the terrain detail words `0x4aaaa8`,
`0x4aaaac`, the count word `0x4aaa7c` (the fixed region ends at
`0x4aaa7c`), and `0x49aac8` (the second grid pointer), `0x49aad0` (20000),
the first-use byte `0x49aaf0`. The live run adds them with
`[snapshot] extra = 0x4aaa7c:0x34,0x49aac8:0x29`.

### Live result

`check-h4110` (all 663 hooks replaced, own runtime): **40 of
40 identical**, every skipped/bypassed/mismatch counter 0; `zone_load`,
`grid_ctor`, `height_cache_ctor`, `terrain_materials`, `terrain_cells` 1
and `terrain_materials_call_4735b8` 13 in every suite; `cell_mark_set` /
`terrain_cells_call_492ee0` equal the table above (65,688 / 16,384 in the
C1_01 suites, 16,425 / 1,024 in C3_09, 16,416 / 1,024 in C3_10, 16,394 /
4,096 in C4_08 and C5_05, 16,416 / 4,096 in train1-timers);
`hk_rng_next_calls` and `hk_rng_bounded_calls` equal `check-h4109`'s in
every suite. After-load snapshot `snapshot-r7f-20261013` with `[snapshot]
extra = 0x4aaa7c:0x34,0x49aac8:0x29` (15 fixed regions, written by
`configure-tick-log.ps1 -AfterLoadSnapshotExtra`): O1 = O2
and O2 vs R **identical** (scope all and pools) in acceptance-60s,
c3_10-loadout-hercs and c3_09-loadout-b. The zone matrix's byte 19 at
`0x4aaa97` is written by neither run (matrix padding class).

### Open

- `0x4952e8` (the height cache's offset/scale setter, also called per
  frame by the H3 terrain draw `0x46cb9c`) is recorded as H3 in
  function_verdicts.md and kept a guest call, like the reset `0x4952fc`.
- The text grid of `0x46c754` writes marks into `[G+0F0h]`, which only the
  bitmap path allocates; with no suite running it, it is reproduced as it
  stands.
## 25. Stage-2 callbacks with type data

The six stage-2 callbacks of section 4 that load type data and the
weapon-list entry reader, read from the pinned `llvm-objdump -d -M intel`
listing of the reference DBSIM (the six exception descriptors, the file and
template strings, the name tables `0x498eb0`/`0x49a360` and the step table
`0x498640` read from the file). The verdict rows of function_verdicts.md were
the starting hypotheses; they hold, with the additions listed under
"Corrections". Own code: adapter group `stage2_types`, core module
`core/src/stage2_types.c` (stream readers on the weapons guest bridge like
section 20's `collision_data`). **Confirmed live:** `check-h4115`;
after-load snapshot `snapshot-r7g-20261013` (section 14). Evidence
(scripts and logs, no listings): `build\claude\h4-115\`.

| Hook | VA | Record | Builds |
|---|---|---:|---|
| `rocket_types` | `0x40a818` | 11 | `rockets.dat`: count word `0x4a96d4`, 14-byte records (new[]) at `0x4a96d0`; the word `0x4a96dc` = max(0, record word +0 + 1); shapes `rockets.dts` into the list `0x4a96d8` |
| `bullet_types` | `0x40ade0` | 13 | the byte `0x498514` set once; `bullets.dat`: count `0x4a96fc`, 14-byte records at `0x4a96f8`, the word `0x4a9704` as above; shapes `bullets.dts` into `0x4a9700`; bitmap `bullets.dba` at `0x4a9706`, its texture table at `0x4a970a`; +26h of every shape = `0x4a9706` |
| `beam_types` | `0x40b6e0` | 14 | 120 step words at `0x4a9712`; `beam.dat`: count word in the local f−52h, 6-byte records at `0x4a9804`; the texture table of `beamtex.dba` at `0x4a9808`, the bitmap released |
| `weapon_types` | `0x40fc8c` | 16 | `wpntex.dba` (bitmap at `0x4a9ae8` while its texture table is built into `0x4a9aec`, then released and the slot cleared); shapes `mechwpn2.dts` into `0x4a98f4`, count `0x4a98f8`, +26h of every shape = `0x4a9ae8`; `weapons.dat`: count `0x4a98f0`, 58h-byte entries (`0x49667c`) at `0x4a98ec`, each read by `weapon_entry`, +52h = name pointer `[0x498eb0 + 4k]`, word +56h = k; then `proj.dat` on the same stream: count `0x4a9900`, 36-byte records (new[]) at `0x4a98fc` |
| `weapon_entry` | `0x40f8bc` | — | one weapon-list entry E from the stream S: `dmg_region_read(E, S)`, `collision_volume(E+12h, S)`, S.read(30h, E+22h); word E+48h = 0 |
| `mech_forms` | `0x41fdb0` | 21 | `_memset(0x4a9d08, FFh, 55h)`, word `0x4a9d06` = 0; `mforms.dat`: count `0x4a9d68`, 28-byte forms (`0x49667c`, element constructor `0x4207bb`) at `0x4a9d6c`; seven bitmaps named by `[0x49a360 + 4k]` (`light`, `medium`, `heavy`, `enemy`, `apocatex`, `razortex`, `newhercs`): texture tables at `0x4a9d76 + 8k`, the bitmaps released, the slots `0x4a9d72 + 8k` cleared |
| `flyer_forms` | `0x422d90` | 23 | `_memset(0x4a9dcc, FFh, 22h)`, word `0x4a9dca` = 0; `fforms.dat`: count `0x4a9df8`, 18-byte forms (`0x49667c`, element constructor `0x423078`) at `0x4a9dfc` |

The file names come from the path builder `0x492bec(template, extension)`
with the extension pointers `[0x4a09f0]` (`dat`), `[0x4a09f8]` (`dts`)
and `[0x4a0a10]` (`dba`); the error reports name `rocket.cpp`,
`bullet.cpp`, `BEAM.CPP`, `GUNLIST.CPP`, `mechsys.cpp` and `flyersys.cpp`.

### Call path and live visibility

The six callbacks are registered once each by `0x401d64(fn, 2)` at
`0x40ab1a`, `0x40b5da`, `0x40c2c2`, `0x411112`, `0x420656` and `0x42306a`
(static init records 11, 13, 14, 16, 21, 23); `weapon_entry` has one call
site, `0x40fdc8` in `weapon_types`. A scan of the whole file finds no other
dword holding one of the seven addresses and no other `call`/`jmp rel32`
to them. `0x401d94` calls every stage-2 entry through `call [ebx]` at
`0x401dac` once per process (section 15), so with early arming (section
13, the default) each callback runs exactly **once per suite** and
`weapon_entry` once per weapon-list entry; the map
(`dbsim-map-20261012`, every hook original, early arming) reads 1 for
each callback and 33 for `0x40f8bc` in all 40 suites.

### Contracts (from the bytes)

All seven are cdecl with a `push ebp` frame; EDX:EAX is reproduced exactly.
The six callbacks take no argument, start with `add esp,-50h`
(beam_types `-54h`) and set up the runtime record through `0x49630c` with
EAX = the descriptor (`0x49834c`, `0x498525`, `0x498658`, `0x498f8d`,
`0x49a38c`, `0x49a678`; each has −50h at +4, so the record lies at
EBP−50h, context word EBP−40h) and the entry EDX/ECX; `0x49630c` reads EAX
only and keeps EDX and ECX, so after it EAX = the old FS:0 head. Each
epilogue loads the saved head from EBP−50h into EAX (rocket_types,
bullet_types, weapon_types) or EDX (beam_types, mech_forms, flyer_forms)
and stores it to FS:0; the adapter does the same from the returned
register.

**The open** (all six, after the path call): the name N = EAX of
`0x492bec`; context word = 8; `0x472f14(S, N)` with S = EBP−30h (the
stream object), EAX = S and EDX/ECX the path builder's; EAX = the status
dword [S+4]; nonzero: `0x479c78(0x6c56c8, 5, 0, file, line, N)` with EDX =
0 (`SETE DL` of a nonzero EAX, `AND EDX,1`) and the lines E6h, 32h, 30h,
10Bh, 34h, 26h. "Read(n, buf)" below is `stream_read 0x4735b8(S, n, buf)`
called directly, "read(n, buf)" the stream's virtual +18h through the
table [S] reread at that point; s16 is the sign extension of a word.

**`rocket_types` 0x40a818** (ESI = `0x4a96dc`): path (`rockets`, EDX =
[`0x4a09f0`]); open; Read(2, `0x4a96d4`) with ECX = S; EAX = s16(count),
EDX = 14 × EAX, `new[](EDX)`, `POP ECX` = that size; the block into
`0x4a96d0`; Read(14 × s16(count reread), block) with EAX = s16, EDX = the
size, ECX = S; `stream_destroy(S, 2)` with EAX = S. Word `0x4a96dc` = 0;
ECX = 0, EDX = [`0x4a96d0`]; while CX < the count word (16-bit signed
`CMP CX,[0x4a96d4]`, reread every pass): `MOV AX,[EDX]` (EAX keeps its
high word), `INC EAX` (32 bits), and when AX ≥ the word `0x4a96dc`
(signed) BX = AX, else BX = that word (reread); the word ← BX; ECX += 1,
EDX += 14. Then path (`rockets`, EAX = [`0x4a09f8`], EDX/ECX the loop's),
`0x474cd8(0x4a96d8, N, 0x140008, 0)` (EAX = N). Returns EAX = the head,
EDX/ECX `0x474cd8`'s.

**`bullet_types` 0x40ade0** (ESI = `0x4a9704`): when the byte `0x498514`
is 0 it is incremented (`INC BYTE`, read again); then as rocket_types with
`bullets`, `0x4a96fc`, `0x4a96f8`, except the maximum loop: EAX = 0, ECX
= the records, the test loads EDX = s16(count) (reread) and compares EAX
with it (32-bit `JL`); the body `MOV DX,[ECX]`, `INC EDX` and the same
maximum into `0x4a9704`, EAX += 1, ECX += 14. Then path (`bullets`, EAX =
[`0x4a09f8`], EDX = s16(count), ECX = the records + 14k),
`0x474cd8(0x4a9700, N, 0x140008, 0)`, EBX = its EAX; path (`bullets`, EAX
= [`0x4a0a10`]); `0x45cdf4(N)`, `POP ECX` = N; the bitmap into
`0x4a9706`; `0x46a03c(bitmap)` (ECX = N), `POP ECX` = the bitmap; its EAX
into `0x4a970a`; EAX = 0; while EAX < s16(BX): ECX = [[`0x4a9700`] + 4
EAX] (the list reread), EAX += 1, [ECX+26h] = `0x4a9706`. Returns EAX =
the head, EDX = s16(BX), ECX = the last shape, or the bitmap when there is
none.

**`beam_types` 0x40b6e0**: for i = 0 .. 119 (AX against 78h): ECX =
s16(i), EBX = ECX SAR 1, ECX &= 3, BX += the word [`0x498640` + 2 ECX]
(1, 0, 1, 2 in the file), word [`0x4a9712` + 2i] = BX; after it EAX =
78h, EDX = `0x4a9802`, ECX = 3. Path (`beam`, EAX = [`0x4a09f0`]); open;
read(2, EBP−52h) with EAX = S, EDX = [S], ECX = EBP−52h; ECX = s16 of
the local, `new[](6 × ECX)` (EAX = the size), `POP ECX` = the size; the
block into `0x4a9804`; Read(6 × s16(local reread), block) with EAX = s16,
EDX = the size, ECX = S; `stream_destroy(S, 2)`; path (`beamtex`, EDX =
[`0x4a0a10`]); `0x45cdf4(N)`, `POP ECX` = N; EBX = the bitmap;
`0x46a03c(bitmap)`, `POP ECX` = the bitmap; its EAX into `0x4a9808`; the
construction count [[`0x6cbee4`]] += 1 (EAX = the pointer);
`0x48349c(bitmap, 3)` (the `GLBitmapArray` destructor). Returns EAX
and ECX `0x48349c`'s, EDX = the head. The local EBP−52h is the only local
not written by the function itself: the read it is passed to fills it
(the first 2 bytes of `beam.dat`). Only a file shorter than 2 bytes would
leave the stack word in place; that case is not modelled (the core's
frame scratch is zeroed).

**`weapon_types` 0x40fc8c** (ESI = `0x4a9ae8` until the weapon list):
path (`wpntex`, EDX = [`0x4a0a10`]); `0x45cdf4(N)`, `POP ECX` = N; EBX =
the bitmap, [`0x4a9ae8`] = it; `0x46a03c(bitmap)`, `POP ECX` = the
bitmap; [`0x4a9aec`] = its EAX; [[`0x6cbee4`]] += 1; `0x48349c([0x4a9ae8]
reread, 3)` (EAX = the counter pointer); ECX = 0, [`0x4a9ae8`] = 0. Path
(`mechwpn2`, EAX = the template, EDX = [`0x4a09f8`], ECX = 0);
`0x474cd8(0x4a98f4, N, 0x140008, 0)`; word `0x4a98f8` = its AX; for k
while k < s16([`0x4a98f8`]) (reread; ECX = it): EDX = [[`0x4a98f4`] +
4k] (reread), [EDX+26h] = `0x4a9ae8`. Path (`weapons`, EAX =
[`0x4a09f0`], EDX the last shape or `0x474cd8`'s, ECX = s16(count));
open; Read(2, `0x4a98f0`); `0x49667c(0, 58h, s16(count), 1, 0x41178d)`
(EAX = s16); ESI = the block, into `0x4a98ec`; EDI = `0x498eb0`; for k
while k < s16([`0x4a98f0`]) (reread, ECX = it): `weapon_entry(ESI, S)`
with EAX = S, then EDX = [EDI], EDI += 4, [ESI+52h] = EDX, word [ESI+56h]
= k, ESI += 58h. `stream_close 0x47307c(S)` (EAX = S), `POP ECX` = S;
path (`proj`, EDX = [`0x4a09f0`]); `stream_open 0x4730e8(S, N)` (EAX =
S); read(2, `0x4a9900`) with EAX = `0x4730e8`'s, EDX = S, ECX = [S]; n =
s16 of the count (read once), `new[](36n)` (EAX = 36n), `POP ECX` = 36n;
the block into `0x4a98fc`; Read(s16(36n as a word), block) with EDX = S,
ECX = 36n; `stream_destroy(S, 2)` (ECX = S). Returns EAX = the head,
EDX/ECX `stream_destroy`'s.

**`weapon_entry` 0x40f8bc (E, S)**: `dmg_region_read(E, S)` with the
entry registers; `collision_volume(E+12h, S)` with EAX = E+12h;
read(30h, E+22h) with EAX = E+22h, EDX = 30h (`MOV DI,30h; MOVSX
EDX,DI`), ECX = [S]; EAX = AL of the read; word [E+48h] = 0 (inside the
bytes just read). Returns EAX = 0..FFh, EDX/ECX the read's. Layout of an
entry: DMG region +0 (18 bytes, section 22), COL volume +12h (16 bytes,
section 20), 30h file bytes +22h with the word +48h cleared, name pointer
+52h, index word +56h.

**`mech_forms` 0x41fdb0**: `_memset(0x4a9d08, FFh, 55h)` (EAX = the head,
the entry EDX/ECX); word `0x4a9d06` = 0; path (`mforms`, EDX =
[`0x4a09f0`]); open; Read(2, `0x4a9d68`); `0x49667c(0, 1Ch, s16(count),
1, 0x4207bb)`; the block into `0x4a9d6c`; EDX = s16(count reread), EAX =
(8 EDX − EDX) × 4, Read(s16(AX), block) with EAX = S, ECX = s16(AX). Then
seven times (DI against 7; EBX = `0x4a9d72` + 8k, ESI = `0x49a360` + 4k):
path ([ESI], EAX = [`0x4a0a10`], EDX = [ESI]; ECX = the read's in the
first pass, then 0); `0x45cdf4(N)`, `POP ECX`; [EBX] = the bitmap, ECX =
[EBX] (reread); `0x46a03c(ECX)`, `POP ECX`; [EBX+4] = its EAX;
[[`0x6cbee4`]] += 1; `0x48349c([EBX] reread, 3)`; ECX = 0, [EBX] = 0.
Then `stream_destroy(S, 2)` (EAX = S, ECX = 0). Returns EAX/ECX
`stream_destroy`'s, EDX = the head.

**`flyer_forms` 0x422d90**: `_memset(0x4a9dcc, FFh, 22h)`; word
`0x4a9dca` = 0; path (`fforms`); open; Read(2, `0x4a9df8`);
`0x49667c(0, 12h, s16(count), 1, 0x423078)`; the block into `0x4a9dfc`;
EAX = 18 × s16(count reread), Read(s16(AX), block) with EDX = s16(AX),
ECX = S; `stream_destroy(S, 2)`. Returns EAX/ECX `stream_destroy`'s, EDX
= the head.

**Element constructors** (reached only through `0x49667c`, original):
`0x41178d` computes three addresses into registers it restores and
returns its argument (an empty constructor); `0x4207bb` builds 7
four-byte elements in place (`0x49667c(form, 4, 7, 1, 0x492428)`),
`0x423078` 3 six-byte elements (`0x49667c(form, 6, 3, 1, 0x4922d3)`);
`0x492428` and `0x4922d3` return their argument.

**Registers after the calls (ECX).** The framework returns the entry
ECX. `0x401d94` reads no register after `call [ebx]` at `0x401dac`, and
the next stage-2 entry after each callback writes ECX before reading it:
after `rocket_types` the empty `0x40ac38` (record 12) and then
`bullet_types`, whose entry ECX reaches only `0x49630c` (keeps it) and
`0x492bec` (`OR ECX,-1` before its scan, section 15); after
`bullet_types` `beam_types` (`MOVSX ECX,AX` at `0x40b6f8`); after
`beam_types` `weapon_types` (`0x492bec`); after `weapon_types` `0x415464`
(record 19, no register read); after `mech_forms` `flyer_forms`
(`_memset`, the RTL import that reads no register); after `flyer_forms`
`0x428230` (record 27, `new[]` first, which reads no register). After
`weapon_entry` (`0x40fdcd`) the caller writes EDX, then ECX at
`0x40fde0`, and loads EAX at `0x40fdc3`/`0x40fdeb` before reading it. The
core still reports the original's ECX (in vitro compares it).

**Frames, residue, shims.** The callbacks run with the synthetic EBP f of
a zeroed scratch (`ES2_STAGE2_TYPES_FRAME` 54h in 58h bytes): record at
f−50h, stream object at f−30h (built by `0x472f14`, used by every stream
call), beam_types' count word at f−52h (filled by the read, above);
`0x49630c` through `weapons_impact_guest_call`. The `POP ECX` values are
the pushed ones: `0x45cdf4`, `0x46a03c` and `0x47307c` only read their
argument slot, `new[]` neither reads a register nor writes its slot
(section 15). `weapon_entry` has no locals. No entry or exit shim.

**Compare:** refused for all seven (file reads through the stream,
allocation through new[] and `0x49667c`, the resource loads and
releases, the runtime record and the construction count); the after-load
snapshot is the acceptance.

**Corrections** to section 4 and to the verdict rows of function_verdicts.md: `beam_types` also writes
the 120 step words `0x4a9712..0x4a9801` and stores the texture table (not
the bitmap) at `0x4a9808`; `weapon_types` also loads `wpntex.dba`
(`0x4a9ae8`/`0x4a9aec`) and fills the entries' +52h/+56h; `bullet_types`
and `rocket_types` also keep the shape lists `0x4a9700`/`0x4a96d8` and
the maximum words `0x4a9704`/`0x4a96dc`, `bullet_types` the bitmap
`0x4a9706`/`0x4a970a` and the flag `0x498514`; `mech_forms` loads seven
bitmaps (not one `.dba`) and keeps only their texture tables.

### In vitro (group `stage2_types`)

`invitro/stage2_types_cases.h`, quick tier: per hook 100,000 direct cases
(1,001,024 in the full tier) against logging stubs for every direct game
callee (20 stub ids: the record stub `0x49630c`, `0x492bec`, `0x45cdf4`,
`0x46a03c`, `0x48349c`, `0x474cd8` with two arena shape lists,
`0x472f14` with a nonzero status one time in four, `0x479c78`,
`stream_read`, `0x49667c`, `weapon_entry`, `stream_close`,
`stream_open`, `stream_destroy`, new[], `_memset`, `dmg_region_read`,
`collision_volume` and the read method of two arena tables), the original
called with six slots, EAX/EDX/ECX and a stack filled with a case
pattern. Compared: the pair, ECX, FS:0, the ordered calls with argument
slots (frame addresses relative to the hook's record) and the bridge's
slot count, entry registers and the record's context word, the 16 KiB
arena, and 663 bytes of image globals (every table and count above, the
flag, the step table, the name tables, the extension pointers and the
counter pointer `0x6cbee4`, randomized per case). The stubs change what
the originals reread (the count word last read after new[], `0x49667c`
or `weapon_entry`; the stream's table after a read, `stream_open` or the
entry readers; the bitmap slots and the bullet shape list after
`0x46a03c`; the counter pointer after `0x48349c`). The adapter is
compared in every case with the callee counter deltas. 256 installed
31/a7 cases per hook. Real-callee passes (2,048 cases per hook): the real
`0x49630c`, `0x49667c` with the element constructors and `_memset`, a
bump allocator for new[]; `weapon_entry` nested under `weapon_types`
(entered 5,286 times), and `dmg_region_read`, `collision_volume` and
`collision_points` (with the real `0x49667c`) nested under
`weapon_entry`, in rotating original/replaced modes; the hook under test
original or replaced against a run without it. 16 branch sites, all both
ways, none PROVED. One caught mutation per hook (`MUTATIONS.md`). Core
unit test `//core:stage2_types_test` (scenarios worked out by hand: the
call sequences with their entry registers, the maximum words with a
16-bit wrap, the step table, the shape loops with and without shapes,
the seven bitmaps, the failed open).

### Live counts

The same in every suite (stage 2 reads the same files; 33 weapon-list
entries in all 40 suites of the map):

| Counter | every suite |
|---|---:|
| `hk_weapon_types_calls`, `hk_rocket_types_calls`, `hk_bullet_types_calls`, `hk_beam_types_calls`, `hk_mech_forms_calls`, `hk_flyer_forms_calls` | 1 each |
| `hk_weapon_entry_calls`, `_call_40cff8`, `_call_40cc14`, `_slot_18` | 33 each |
| `hk_weapon_types_call_492bec` / `_call_4735b8` / `_call_40f8bc` | 4 / 2 / 33 |
| `hk_weapon_types_call_49630c`, `_45cdf4`, `_46a03c`, `_48349c`, `_474cd8`, `_472f14`, `_49667c`, `_47307c`, `_4730e8`, `_slot_18`, `_4774f0`, `_472f64` | 1 each |
| `hk_rocket_types_call_492bec` / `_4735b8`; `_49630c`, `_472f14`, `_4774f0`, `_472f64`, `_474cd8` | 2 / 2; 1 each |
| `hk_bullet_types_call_492bec` / `_4735b8`; `_49630c`, `_472f14`, `_4774f0`, `_472f64`, `_474cd8`, `_45cdf4`, `_46a03c` | 3 / 2; 1 each |
| `hk_beam_types_call_492bec`; `_49630c`, `_472f14`, `_slot_18`, `_4774f0`, `_4735b8`, `_472f64`, `_45cdf4`, `_46a03c`, `_48349c` | 2; 1 each |
| `hk_mech_forms_call_492bec` / `_45cdf4` / `_46a03c` / `_48349c` / `_4735b8`; `_49630c`, `_49681a`, `_472f14`, `_49667c`, `_472f64` | 8 / 7 / 7 / 7 / 2; 1 each |
| `hk_flyer_forms_call_4735b8`; `_49630c`, `_49681a`, `_492bec`, `_472f14`, `_49667c`, `_472f64` | 2; 1 each |
| every `*_call_479c78` | 0 |

`hk_dmg_region_read_calls`, `hk_collision_volume_calls`,
`hk_stream_read_calls`, `hk_stream_close_calls`, `hk_stream_open_calls`,
`hk_stream_destroy_calls` and the `hk_stage_dispatch_*` counters are
unchanged (the bridge still reaches them); every `*_skipped`/`*_bypassed`
0.

**Snapshot reach.** The fixed region `0x4a9572..0x4a9dfc` (end exclusive)
holds every table pointer and count above except two: the FLYER forms
pointer `0x4a9dfc..0x4a9dff` and the flag byte `0x498514`. The forms block
itself is then reached only when a FLYER actor holds a pointer into it
(the getter `0x423044` adds an offset to `[0x4a9dfc]`). Add
`[snapshot] extra = 0x4a9dfc:4,0x498514:1` to `es2-oracle.ini`
(`configure-tick-log.ps1 -AfterLoadSnapshotExtra` writes it; the proxy parses up to 16
`0xVA:LEN` entries). Everything else is reached: `0x4a96d0`, `0x4a96d8`,
`0x4a96f8`, `0x4a9700`, `0x4a9706`/`0x4a970a`, `0x4a9804`/`0x4a9808`,
`0x4a98ec` (the entries with their DMG link lists and COL sphere arrays),
`0x4a98f4`, `0x4a98fc`, `0x4a9aec`, `0x4a9d6c` and the texture tables
`0x4a9d76 + 8k`.

A difference caused by these hooks would lie in the type tables and counts
above, in a block reached from them (records, entries, forms, shape lists,
texture tables), or in the block pairing: the hooks allocate the same
sizes in the same order through the same new[] and `0x49667c`.

### Live result

`check-h4115` (all 670 hooks replaced, own runtime): **40 of
40 identical**, every skipped/bypassed/mismatch counter 0; in every suite
each callback 1, `weapon_entry` 33 with `_call_40cff8`/`_call_40cc14` 33,
`weapon_types_call_40f8bc` 33, `_call_4735b8` 2, `mech_forms_call_492bec`
8; `weapon_types_call_492bec` is **4** (the prediction of 3 was a
miscount). After-load snapshot `snapshot-r7g-20261013` with `[snapshot]
extra = 0x4aaa7c:0x34,0x49aac8:0x29,0x4a9dfc:4,0x498514:1` (17 fixed
regions): acceptance-60s and c3_10-loadout-hercs identical; in
c3_09-loadout-b the two original runs already showed pairing conflicts
(paths ending in unaligned offsets such as `+0x576f`: 4-byte windows of
byte data that happen to look like heap addresses, the chance pairings of
section 14); the isolation rerun (`c309-isolation`, three clones) gives O2
vs R **identical** with section 24's extra regions and with all four.

### Open

- The element constructors `0x41178d`, `0x4207bb`, `0x423078` stay
  original behind `0x49667c` (verdict rows in function_verdicts.md).
- The short-read case of beam_types' count word (stack residue) is not
  modelled; it would need a `beam.dat` shorter than 2 bytes.
## 26. MECH and FLYER type loads

The fifteen functions of the MECH side and the FLYER side, read from the
pinned `llvm-objdump -d -M intel` listing of the reference DBSIM (the
exception descriptors `0x49a3e0`, `0x49a3b0`, `0x498fb1`, the strings
`mechs`, `flyers`, `fm`, `GUNLIST.CPP` and the extension pointers
`[0x4a09f0]` … `[0x4a0a08]` read from the file). The verdict rows of
function_verdicts.md were the starting hypotheses; they hold, with the
additions below. Own code: adapter group `type_loads`, core module
`core/src/type_loads.c` (the type loads are the world load's callees,
section 29, not pieces of the actor load, and need their own scratch
frames and stub world in vitro). **Confirmed live:** `check-r7-5`;
after-load snapshot `snapshot-r7h-20261013` (section 14). Evidence
(scripts and logs, no listings): `build\claude\h4-114\`.

| Hook | VA | Role | Call site |
|---|---|---|---|
| `mech_types` | `0x42049c` | the MECH type table: names, type records, each selected type | world load `0x4252dd` |
| `mech_type_load` | `0x4201a8` | one MECH type: OVL record, LOD shapes, stats, gun list, "fm" record, debris | `mech_types` `0x420542` |
| `lod_remap` | `0x420090` | the part remap rows of the LOD shapes and their walk | `mech_type_load` `0x4202b6` |
| `node_remap` | `0x41ff88` | the recursive shape-node walk by class id | `lod_remap` `0x420189`, itself `0x42006e` |
| `remap_word` | `0x41ff68` | one part word through a remap row | `node_remap` `0x41fff3`, `0x42002b`, `0x420050` |
| `mech_type_select` | `0x41ff10` | the type index of a selected mech and its slot reservations | world load `0x4250b9` |
| `gun_list_load` | `0x40fee8` | the gun list of a MECH type (`GUNLIST.CPP`) | `mech_type_load` `0x420391` |
| `gun_classes` | `0x40fc50` | the gun entries' class words | `mech_type_load` `0x42039e` |
| `node_list` | `0x40304c` | the pointer list of a shape's gun nodes | `mech_type_load` `0x4203ce` |
| `gun_slot_base` | `0x40feb4` | running gun-list slot word `0x4a98e8` | `mech_type_select` `0x41ff3c` |
| `col_slot_base` | `0x40cbd4` | running collision slot word `0x4987dc` | `mech_type_select` `0x41ff54`, `flyer_type_select` `0x422ec4` |
| `dmg_slot_base` | `0x40d200` | running damage slot word `0x498856` | `mech_type_select` `0x41ff5c`, `flyer_type_select` `0x422ebc` |
| `flyer_types` | `0x422fb4` | the FLYER type table | world load `0x4252e2` |
| `flyer_type_load` | `0x422ed0` | one FLYER type: records, shape, DMG and COL data | `flyer_types` `0x423008` |
| `flyer_type_select` | `0x422e90` | the type index of a selected flyer and its slot reservations | world load `0x4250f7` |

No dword of the file holds any of the fifteen addresses (scan of the file),
so these call sites are all. The slot bases are 22 bytes each; their
prologue (`push ebp; mov ebp,esp; mov ax,[moffs32]`, 9 bytes, `66 A1` on
the relocation whitelist) is hookable, so they are hooks,
not computed in core. Computed in core (no longer called when the hooks are
replaced): the type-index leaves `0x41ff00` (MECH, `MOVSX EAX,WORD
[EBP+8]; MOVZX EAX,BYTE [EAX+0x4a9d08]`, EDX and ECX unchanged; also
computed by `mech_ctor`, section 17) and `0x422e80` (FLYER, table
`0x4a9dcc`), and the pure Q16 helpers `0x47e068` (`(a << 16) / b`: EDX:EAX
the sign-extended dividend, `IDIV`, EAX the quotient, EDX the remainder; a
zero divisor or a quotient outside 32 bits faults in the original and is
outside the contract) and `0x47e08d` (`IMUL`, `SHRD EAX,EDX,16`: EAX = bits
16..47 of the product, EDX its high dword), both `enter` bodies that leave
ECX alone. Every other callee stays game code behind the guest bridge with
its own mode:

| Callee | Role | Class / mode |
|---|---|---|
| `0x49630c` | runtime frame record (descriptors `0x49a3e0`, `0x49a3b0`: EBP−24h; `0x498fb1`: EBP−50h) | RTL, through `weapons_impact_guest_call` |
| `0x4774f0`, `0x49667c`, `0x49681a` | new[], the array constructor (with the element constructors `0x420817` → `0x42067c` → `shape_word_block`, `0x4207db`, `0x4117a5`, `0x423098`), memset | RTL |
| `0x492d00`, `0x492bec`, `0x492f54` | the name list of a type file, the path builder, the OVL record reader | platform |
| `0x472f14`, `stream_read`, `stream_destroy`, `0x479c78` | the gun list's stream, its reads, its close, the fatal report | platform / hooks |
| `0x474cd8`, `0x474c38` | the shape list (ANShape `0x1e0003`) and the FLYER shape | platform (resource) |
| `0x4378d8`, `0x4379cc` | the player damage display bitmaps, the damage display of a type | H3 |
| `0x40fea0` | the mount entry `[0x4a98e4]` + 6 × sx(type word) | own code (section 32) |
| `0x40874c` | the debris set of a type | own code (`debris_set`, section 31) |
| `multiply_q10`, `dmg_load`, `collision_load` | Q10 product; the DMG and COL data of a FLYER type | hooks |
| shape virtuals `+20h` (part → node), `+24h` (class word → node); node virtual `+0` (class id) | | game code |

### Call path and live visibility

All fifteen run during the world load (pass 1, section 2 step 8 and 9)
only: the selects once per selected actor, the tables once per process,
the type loads once per selected type, the rest below them. With early
arming (section 13, the default) the hooks are installed at `0x45f42b`,
before the load, so their counters are constant from the first tick
record on. The map (`dbsim-map-20261012`, every hook original, early
arming) counts all fifteen (table below).

### Layout

**MECH type record** (21Eh bytes, the array `[0x4a9d64]` of `count`
records behind a 4-byte count header, built by `0x49667c` with the element
constructor `0x420817`, which constructs the ten 18h-byte entries at +0ECh):
+0 index word, +2 the OVL record R (D8h bytes from `name.dat`, so +2 ..
+0D9h), +6Ch the type word (written over R+6Ah after the read), +0DAh the
damage display (`0x4379cc`), +0DEh the shape list S (+0 the shape pointer
array, +4 = `0x49a344`, +8 the node-list array of 6-byte entries, +0Ch the
LOD count word), +1DCh the "fm" record (36h bytes from `name.fm`, +0Eh its
Q16 ratio), +212h the debris set (`0x40874c`). In R: +2, +4, +2Ch, +6Ch the
words scaled by q, +4Eh the flag that selects the scaling (0) or the "fm"
record (not 0; the same word is reread as rec+50h), +94h (rec+96h) the
index of the 8-byte entry `0x4a9d72` + 8 × sx, +0C0h = `multiply_q10(13Bh,
R+4)`, +0C2h/+0C4h the divisor and dividend words of q, +0C6h = R+4 before
the scaling, +0CCh (rec+0CEh) the debris name. The part list B of
`lod_remap` is rec+30h, 50 bytes: **it covers the flag word rec+50h** (bytes
32 and 33 of B), so a list that reaches byte 32 makes the flag nonzero;
the map's `0x492f54` counts (an "fm" record for one type of the C4_08
suites only, below) agree with lists that end earlier (an inference, not
read from the type files).

**FLYER type record** (70h bytes, `[0x4a9df4]`, `0x49667c(0, 70h, count, 1,
0x423098)`; the element constructor writes nothing): +0 the `name.dat`
record (2Eh bytes), +2Eh the DMG slot (`dmg_load`), +32h the COL slot
(`collision_load`), +36h the shape (`0x474c38`; its +26h = `0x4a9d8a`),
+3Ah the "fm" record (36h bytes, +0Eh its Q16 ratio).

**Gun list** (the mount entry E = `0x40fea0(type word)`, 6 bytes: count
word +0, entries +2): 26-byte (1Ah) entries, class word +0, class byte +6.

### Contracts (from the bytes)

All fifteen are cdecl; EDX:EAX is reproduced exactly (the core follows
EAX/EDX/ECX instruction by instruction, so every game call sees the
original's entry registers). "sx" is the sign extension of a word; counts
compare as signed words unless noted; the words and pointers a loop tests
are reread from memory where the original rereads them.

**`gun_slot_base` 0x40feb4 / `col_slot_base` 0x40cbd4 / `dmg_slot_base`
0x40d200 (n):** AX = the slot word (`0x4a98e8` / `0x4987dc` /
`0x498856`), DX = n, the word += DX (a second read). EAX and EDX keep their
high words, ECX is untouched.

**`remap_word` 0x41ff68 (P, table):** w = the word at P; when w > −1
(signed): ECX = sx(w), EDX = table, ECX = the sign-extended byte
table[w], the word at P = CX. EAX = P; EDX = table or (w ≤ −1) the entry
EDX with its low word w; ECX = the byte or the entry ECX.

**`node_remap` 0x41ff88 (N, table, limit):** the class c = N's virtual
+0 (N) (EAX = [N], the entry EDX/ECX; `POP ECX` = N). c = 140014h
(TSGroup, [dts.md](../formats/dts.md)): `remap_word(N+4, table)` (EAX = c,
EDX = the virtual's EDX, ECX = N) and end with its registers. c > 140014h
(signed): 140015h (TSBSPPart) walks its word list, 1E0003h (ANShape) walks
the children, every other class ends (EAX = c − 1E0005h). c < 140014h, d =
c − 140005h (unsigned): d < 2 ends (EAX = d − 2), d = 2, 3 (TSPartList,
TSShape) and 7 (TSDetailPart) walk the children, d = 6 (TSCellAnimPart)
does `remap_word(N+4, table)` (EAX = N+4) and walks the children, every
other d ends (EAX = d − 7). The word list: for i = 0 while sx(i) < the
zero-extended count N+16h (both reread), the word w at [N+1Ch] + 2i:
when 0 < w < limit (signed words) and the byte table[w] ≥ 0:
`remap_word(&w, table)` (EAX = sx(i) with the low word w, EDX = sx(w),
ECX = the byte + 1); then the children. The children: for i while sx(i) <
the zero-extended count N+10h (reread), `node_remap([[N+12h] + 4i], table,
limit word over N's high word)` (EAX = that dword, EDX = the child, ECX =
sx(i)). After the walk EAX = N, EDX = the count, ECX = sx(i).

**`lod_remap` 0x420090 (S, B):** nothing (the entry registers) when the
count word S+0Ch ≤ 1. Otherwise memset(f−1A0h, FFh, 190h) (EAX = B, EDX =
the rows), then for i = 0 .. 49 and the bytes B[i] up to the first
negative one: node0 = the virtual +20h of shape 0 ([[S]], EAX = shape 0,
EDX = its vtable, ECX = [S]) with B[i]; w0 = node0's word +4 (node0 is not
tested: a null node0 faults); for each shape k = 1 while k < S+0Ch (BX,
reread): nodek = shape k's virtual +20h (EAX = the vtable, EDX = B[i]
reread, ECX = shape k); a non-null nodek sets the byte f−1D2h + 50k +
sx(nodek's word +4) = the low byte of w0 (reread from f−0Eh). Then for k =
1 while k < sx(S+0Ch) (reread): `node_remap(shape k, f−1D2h + 50k,
word +10h of [[shape 0]+2Ah] over [S]'s high word)` (EAX = shape k, EDX =
the row, ECX = f−1D2h). The rows are row k at f−1A0h + 50(k−1); a row byte
outside f−1A0h .. f−1 would be the original's saved registers or its
caller's frame: the core skips such a store (outside the contract). The
LOD counts are 6 or 7 (`node_list`'s map counts), so k ≤ 6; of the
transform indices −1..19 that [dts.md](../formats/dts.md) observes, only −1
in LOD shape 1 leaves the window: it would write the top byte of the saved
EBX, the caller's shape-list pointer, and the caller faults unless that
byte is unchanged, so the shipped loads (which do not fault) never store a
different byte there (an inference). EAX/ECX the last call's, EDX =
sx(the count).

**`node_list` 0x40304c (L, S, n, classes):** word L+4 = n; `new[](4 ×
sx(n))` (EAX = n's word over the entry EAX, EDX likewise, ECX = the size;
`POP ECX`), [L] = the block; then with the counter word = L+4 (reread
once) and the cursor = [L]: while the counter (before its decrement) is
not 0 and the cursor not null: the entry = 0 for a negative class word,
else S's virtual +24h (S, class word) (EAX = the counter word over the
previous EAX, EDX = the class word over the previous EDX, ECX = [S],
reread); classes += 2. EAX = the last counter word over the last EAX, EDX
the last call's (or the block); ECX = n << 16 | the counter word after its
last decrement (`POP ECX` twice: the dword [EBP−4]).

**`gun_classes` 0x40fc50 (L, out):** for each 26-byte entry of [L+2]
while i < the count word L (reread): out[i] = the entry's word +0 when its
signed byte +6 is below 4, else FFFFh. EAX = the entry cursor with the
low word = the count word (reread), EDX = i, ECX = out + 2i.

**`mech_type_select` 0x41ff10 (t):** the index byte table[sx(t)]
(`0x41ff00`; `POP ECX` = t); when it is FFh: table[sx(t)] = the low byte
of the count word `0x4a9d06` (EDX's low byte), the count +1, `gun_slot_base(1)`
(EAX = sx(t), EDX as above, ECX = t; `POP ECX` = 1), and when the count is
now 1 the base word `0x4a9d70` = its AX. Always `col_slot_base(1)` and
`dmg_slot_base(1)` (`POP ECX` = 1 each). **`flyer_type_select` 0x422e90
(t):** the same over `0x4a9dcc`/`0x4a9dca`, but only a new type reserves
slots, `dmg_slot_base(1)` first, then `col_slot_base(1)`; an old type
returns EAX = its index, EDX the entry EDX, ECX = t.

**`gun_list_load` 0x40fee8 (name, type):** record at f−50h, context word
f−40h = 8; `0x472f14(f−30h, name)` (EAX = `0x49630c`'s, EDX = f−30h);
EBX = the caller's EBX with its low word = the type word; E =
`0x40fea0(EBX)` (the pushed dword carries the caller's EBX high word;
`0x40fea0` reads the word only; `POP ECX` = that dword); a nonzero stream
status [f−2Ch] reports through `0x479c78(0x6c56c8, 5, 0, "GUNLIST.CPP",
146h, name)` (EAX = the status, EDX = 0); `stream_read(f−30h, 2, E)` (ECX =
f−30h); [E+2] = `0x49667c(0, 1Ah, sx(count), 1, 0x4117a5)` (EAX =
sx(count)); `stream_read(f−30h, 26 × sx(count), [E+2])` (EAX = sx, EDX =
the size); `stream_destroy(f−30h, 2)` (EAX = E, EDX = f−30h; E is pushed
above the two arguments and popped into EAX). EAX = E, EDX = the saved
FS:0 head (the adapter stores it to FS:0), ECX = `stream_destroy`'s.

**`mech_type_load` 0x4201a8 (t):** record at f−24h; the name =
[[0x4a9d60] + 4 sx(t)] into f−28h; the index byte (`0x41ff00`) into
f−2Ah; rec = [0x4a9d64] + 21Eh × sx(index); `0x492f54(rec+2, D8h, 1,
0x492bec(name, [0x4a09f0]))`; rec+0 = the index (DX), rec+6Ch = t (BX);
S = rec+0DEh; `0x474cd8(S, 0x492bec(name, [0x4a09f8]), 1E0003h, the word
f−2Ch = 0 over EAX)`, S+0Ch = its AX, S+4 = `0x49a344`; `new[](6 × sx(S+0Ch)
+ 4)` into f−4 (`POP ECX`); when not null context 14h, `0x49667c(block, 6,
sx(S+0Ch), 211h, 0x4207db)`, context 8; S+8 = its EAX or 0;
`lod_remap(S, rec+30h)`; R+0C6h = R+4 (AX); when R+4Eh is 0: q =
`0x47e068(400 × sx(R+0C4h), sx(R+0C2h))` into f−30h, sx(R+4) into f−34h,
R+0C0h = `multiply_q10(13Bh, sx(R+4))` (EAX = sx(R+4), EDX = the
remainder, ECX = sx(R+4)), then R+2, R+4, R+2Ch, R+6Ch = `0x47e08d(sx(word),
q)` in that order (each operand reread). Then E = `gun_list_load(0x492bec(name,
[0x4a0a00]), the index + word 0x4a9d70 over the path's high word)`, n =
`gun_classes(E, f−60h)` (its AX into f−36h); for each shape k while k <
S+0Ch (SI, reread): `node_list([S+8] + 6k, shape k, n's word over EDX,
f−60h)` (EAX = the entry, EDX = shape k, ECX = sx(k)) and shape k +26h =
`0x4a9d72` + 8 × sx(rec+96h); rec+0DAh = `0x4379cc(name, the index word
reread)`; when rec+50h is not 0: `0x492f54(rec+1DCh, 36h, 1,
0x492bec(name, "fm"))` and +0Eh of it = `0x47e068([+22h] − [+26h], [+2Ah] −
[+2Eh])`; f−28h = rec+0CEh, and when that byte is not 0 `0x40874c(rec+212h,
rec+0CEh, 0x4a9d72 + 8 × sx(rec+96h))`. EAX = the saved FS:0 head. The
class list f−60h holds 21 words; a gun list of more entries would run into
f−36h … f−28h, which the core keeps in the scratch frame and rereads like
the original (f−2Ah and f−28h are read after `gun_classes`).

**`mech_types` 0x42049c ():** record at f−24h; [0x4a9d60] =
`0x492d00("mechs", 55h, 8)` (EAX = the old FS:0 head); `new[](21Eh ×
sx(count) + 4)` into f−4 (EDX = sx(count), ECX the size; `POP ECX`); when
not null context 14h, `0x49667c(block, 21Eh, sx(count), 211h, 0x420817)`
(EAX = sx(count) reread, EDX = the block), context 8; [0x4a9d64] = its EAX
or 0; `0x4378d8(the count word over EDX)` (`POP ECX`); then for t = 0 ..
54h (BX): `mech_type_load(t)` when the index byte is not FFh (EAX = the
byte, ECX = t). EAX = the saved FS:0 head, EDX the last call's, ECX = 54h.

**`flyer_types` 0x422fb4 ():** nothing (the entry registers) when the
count word `0x4a9dca` is 0; else [0x4a9df0] = `0x492d00("flyers", 22h, 4)`,
[0x4a9df4] = `0x49667c(0, 70h, sx(count), 1, 0x423098)`, and for t = 0 ..
21h `flyer_type_load(t)` when the index byte is not FFh. EAX the last
index byte or call's, ECX = 21h.

**`flyer_type_load` 0x422ed0 (t):** the name [[0x4a9df0] + 4 sx(t)]; rec
= [0x4a9df4] + 70h × the index (`0x422e80`; `MOVSX EAX,AX`; `POP ECX` =
t); `0x492f54(rec, 2Eh, 1, name.dat)`; rec+36h = `0x474c38(name.dts)`
(`POP ECX`) and its +26h = `0x4a9d8a`; rec+2Eh = `dmg_load(name.dmg)`;
rec+32h = `collision_load(name.col)`; `0x492f54(rec+3Ah, 36h, 1,
name.fm)`; +0Eh of it = `0x47e068([+22h] − [+26h], [+2Ah] − [+2Eh])`.
EDX:EAX = the quotient and remainder, ECX = `0x492f54`'s.

**Registers after the calls (ECX).** The framework returns the entry ECX;
the originals' ECX is dead at every call site: the world load's sites
`0x4250be`, `0x4250fc` (`POP ECX`); after `0x4252dd` the call `0x422fb4`
returns it unchanged only when it has no type, and then `0x4252ee` loads
CX before pushing ECX as `0x42789c`'s first argument, whose high word that
function never reads (`MOV EBX,[EBP+8]`, then only BX until EBX is
written), and the slot is overwritten by the next call's return address;
otherwise `0x422fb4` passes it into `0x492d00`, which writes ECX before
reading it (`0x49630c` reads neither EDX nor ECX); `0x4252e2` likewise;
`0x420542`, `0x423008`, `0x41ff3c`/`0x41ff54`/`0x41ff5c`,
`0x422ebc`/`0x422ec4` (`POP ECX`); `0x420391` → `gun_classes` writes ECX
first; `0x42039e` → `0x4203ae` (`MOVSX ECX,SI`) or `0x4203fd`; `0x4203ce`
→ `0x4203dd`; `0x4202b6` → `0x47e068` (computed: ECX untouched) and
`0x4202fd`, or `0x492bec` → `0x492b90`, which writes ECX (`OR ECX,−1`)
before reading it; `0x420189` → the next pass's `0x42016a`, or `lod_remap`'s
return (as above); `0x42006e` → `0x42007a`; `remap_word`'s sites →
`0x420088` (the walk's return, as above) or `0x420033`/`0x420058`. In the
core every nested hook's ECX is the framework's too; the same liveness
holds there.

**Frames, residue, shims.** `mech_types`, `mech_type_load` and
`gun_list_load` run with the synthetic EBP of a zeroed scratch
(`ES2_*_FRAME`; `0x49630c` through `weapons_impact_guest_call`), their
locals at the original offsets (the stream object f−30h and the class list
f−60h are passed to callees); the adapters write the saved head back to
FS:0 (EAX for the first two, EDX for `gun_list_load`). `lod_remap`'s rows
and locals live in a zeroed scratch frame (`ES2_LOD_REMAP_FRAME`), reread
like the original's. No function reads a local it did not write, and no
callee reads one of theirs apart from the objects passed to it. The only
caller register a function reads is `gun_list_load`'s EBX high word (dead,
above); the adapter reconstructs EBX as its only caller has it (rec+0DEh of
the type record whose index is the type word minus `0x4a9d70`), and the
core takes it as an argument (in vitro the harness's EBX). No entry or
exit shim.

**Compare:** `gun_slot_base`, `col_slot_base` and `dmg_slot_base` accept
it (one image word each, a region; no call); `remap_word` accepts it (span:
the word P; EAX = P compared relative to it; the table byte is read at a
runtime index and only read). The other eleven refuse it (allocation,
files, the runtime record, the virtual calls and the nested hooks that
write the type tables, shapes and node words).

### In vitro (group `type_loads`)

`invitro/type_loads_cases.h`: per hook 100,000 direct cases (quick tier;
the full tier, 1,001,024, not run) against
logging stubs for every game callee (35 stub ids: the direct callees
patched, the shape virtuals +20h/+24h, the node virtual +0 and a stream's
read +18h through arena vtables; `0x49630c` a record stub that keeps the
real FS:0 head and takes EBP as the frame base). The original runs with six
argument slots, EAX/EDX/ECX, EBX/ESI/EDI and a stack filled with a case
pattern, the core with its scratch frames; stack values in the logs are
taken relative to the frame (the original's EBP against the core's scratch;
`lod_remap`'s base from its memset), `lod_remap`'s rows and
`mech_type_load`'s class list by content. Compared: the pair, ECX, FS:0,
the ordered calls with argument slots (and the bridge's slot count), entry
registers and the record's context word, the 64 KiB arena and the image
globals (the type tables and counts, the slot words and counters, the slot
arrays, the extension pointers). The stubs change what the originals
reread (the count words, the part bytes, the shape count and table word,
the scaling flag, R+4, a stream's table, the class list past its 21 words,
node counts), and objects overlap (outputs over the gun list's count, the
remap table over the word, the part list over the flag). `node_remap`'s
recursion runs the original in the original run and through the bridge in
the core run. In even cases the inputs are the real callers' and the
adapter is compared too (callee counter deltas, except `node_remap`'s,
whose recursion is not logged as the adapter counts it). 256 installed
31/a7 cases per hook (fast and preserving dispatch; `node_remap` enters the
installed hook once per node). Real-callee passes (4,096 cases per hook
with callees) over a deterministic world (shape lists with node trees and
lookup maps per LOD shape, the OVL and "fm" records, gun lists, the real
`0x49630c`, `0x49667c` with its element constructors, `0x40fea0`, memset,
`multiply_q10`, the slot bases, a bump allocator for new[], stubs for the
file layer, the H3 displays and the debris set; `dmg_load`/`collision_load`
and their readers over a stream whose reads give zero counts), the nested
hooks in rotating original/replaced modes, the hook under test original or
replaced: `mech_types` (8 nested), `mech_type_load` (7), `lod_remap` (2),
`node_remap` (1), the selects (3, 2), `flyer_types` (5), `flyer_type_load`
(4), `gun_list_load` and `node_list` (none). The four compare-capable hooks
pass 512 installed compare cases each (0 mismatches; the skewed replacement
caught every time). 41 branch sites, all both ways, none PROVED. One caught
mutation per hook (`MUTATIONS.md`). Core unit test
`//core:type_loads_test` (hand-worked scenarios for all fifteen).

### Live counts

The map `dbsim-map-20261012` (every hook original, early arming, all 40
suites) counts the entries of the fifteen; with the hooks replaced the
callers reach them the same number of times through the bridge, so these
are the expected `hk_<name>_calls` (`map_counts.py` in the evidence
directory; the map's probe counts stop at 1,000):

| Suites | `mech_types` | `mech_type_load`, `lod_remap`, `gun_list_load`, `gun_classes`, `gun_slot_base` | `node_remap` | `remap_word` | `mech_type_select` | `node_list` | `col_slot_base`, `dmg_slot_base` | `flyer_types` | `flyer_type_load` | `flyer_type_select` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| the 19 suites on the installed C1_01 | 1 | 4 | ≥ 1,000 | ≥ 1,000 | 10 | 28 | 11 | 1 | 1 | 4 |
| c1_01-unarmed-keys | 1 | 4 | ≥ 1,000 | ≥ 1,000 | 9 | 28 | 10 | 1 | 1 | 4 |
| c1_02-probe | 1 | 4 | ≥ 1,000 | ≥ 1,000 | 13 | 28 | 14 | 1 | 1 | 2 |
| c1_06-cheat-disable | 1 | 5 | ≥ 1,000 | ≥ 1,000 | 16 | 35 | 17 | 1 | 1 | 4 |
| c1_08-escort-route, c1_08-scan | 1 | 3 | ≥ 1,000 | ≥ 1,000 | 8 | 21 | 9 | 1 | 1 | 4 |
| c2_02-identify | 1 | 5 | ≥ 1,000 | ≥ 1,000 | 11 | 35 | 12 | 1 | 1 | 4 |
| the three C3_09 suites, the four C3_10 suites | 1 | 8 | ≥ 1,000 | ≥ 1,000 | 16 | 56 | 16 | 1 | 0 | 0 |
| c4_05-convoy | 1 | 7 | ≥ 1,000 | ≥ 1,000 | 13 | 49 | 14 | 1 | 1 | 2 |
| the three C4_08 suites | 1 | 7 | ≥ 1,000 | ≥ 1,000 | 11 | 48 | 12 | 1 | 1 | 1 |
| c4_10-probe | 1 | 4 | ≥ 1,000 | ≥ 1,000 | 9 | 26 | 10 | 1 | 1 | 2 |
| c5_05-moon-missiles, c5_05-orders-flyer | 1 | 8 | ≥ 1,000 | ≥ 1,000 | 15 | 56 | 16 | 1 | 1 | 2 |
| train1-timers | 1 | 1 | 273 | 343 | 1 | 7 | 1 | 1 | 0 | 0 |

The identities behind them hold in every suite of the map: the MECH types
(`mech_type_load` = `lod_remap` = `gun_list_load` = `gun_classes` =
`gun_slot_base` = `0x4379cc`) are the distinct types among the selected
mechs; `col_slot_base` = `dmg_slot_base` = `mech_type_select` + the FLYER
types (`flyer_type_load`, 0 or 1); `node_list` = the LOD shapes of the
MECH types (6 or 7 each); `flyer_types` = `mech_types` = 1. `node_remap` and
`remap_word` depend on the node trees of the LOD shapes 1 .. n−1 of each
type and the part lists; the map is capped there except in train1-timers
(one type: 273 and 343); the live counter gives them.

Callee counters per suite: `hk_mech_types_call_49630c` = `_492d00` =
`_4774f0` = `_49667c` = `_4378d8` = 1, `_4201a8` = the MECH types;
`hk_mech_type_load_call_49630c` = `_474cd8` = `_4774f0` = `_49667c` =
`_420090` = `_40fee8` = `_40fc50` = `_4379cc` = the MECH types, `_40304c` =
`node_list`, `_492bec` = 3 × types + "fm" types, `_492f54` = types + "fm"
types, `_47e0b0` = types − "fm" types, `_40874c` = the types with a debris
name; from the map's `0x492f54` and `0x40874c` counts (minus `colors.dat`'s
read and the two other debris callers) the "fm" types are 0 in every suite
except one in the three C4_08 suites, and every type has a debris name
except one in C4_08 and one in c4_10-probe (inferences, not separately
probed). `hk_lod_remap_call_49681a` = the types, `_41ff88` = `node_list`
− types; `hk_gun_list_load_call_49630c` = `_472f14` = `_40fea0` =
`_49667c` = `_472f64` = calls, `_4735b8` = 2 × calls, `_479c78` = 0;
`hk_node_list_call_4774f0` = calls; `hk_mech_type_select_call_40feb4` = the
MECH types, `_40cbd4` = `_40d200` = calls; `hk_flyer_type_select_call_40d200`
= `_40cbd4` = the FLYER types; `hk_flyer_types_call_492d00` = `_49667c` =
1 where a FLYER is selected, `_422ed0` = the FLYER types;
`hk_flyer_type_load_call_492bec` = 5, `_492f54` = 2, `_474c38` =
`_40d160` = `_40cd88` = 1 per call. Every `*_skipped`/`*_bypassed` 0. The
counters of the nested hooks are unchanged (the bridge reaches them):
`multiply_q10`, `stream_read`, `stream_destroy`, `dmg_load`,
`collision_load` (with section 20's and 22's readers below them).

### After-load snapshot reach

**In reach:** the type arrays `[0x4a9d60]`/`[0x4a9d64]` and
`[0x4a9df0]`/`[0x4a9df4]`, the index tables `0x4a9d08`/`0x4a9dcc`, the
counts `0x4a9d06`/`0x4a9dca`, the base word `0x4a9d70`, the gun slot word
`0x4a98e8` and the mount table `[0x4a98e4]` all lie in the fixed region
`0x4a9572 .. 0x4a9dfc`, and the walk follows them into the name lists, the
type records, the shape lists, the node lists, the gun lists and the LOD
shapes' node words. **Not in the fixed regions:** the collision and damage
slot words `0x4987dc`/`0x498856` (and the slot counters
`0x4987de`/`0x498854` of sections 20 and 22); their effect is in reach (the
slot arrays `[0x4a9824]`/`[0x4a9828]` that `0x40cbec` and `0x40d218` size
from them, and the slots the type loads fill), and the words themselves
need `[snapshot] extra = 0x4987d8:8,0x498850:8` in `es2-oracle.ini`. A
difference caused by these hooks would lie in the type records (stats, the
scaled words, R+0C0h, the "fm" ratios), the shape lists and their
node-list arrays, the shapes' +26h, the LOD node words (the remap), the
gun lists and their entries, or in the block pairing (the hooks allocate
the same sizes in the same order through the same callees). `remap_word`,
`gun_slot_base`, `col_slot_base` and `dmg_slot_base` can also run in
`compare` (calls as above, 0 mismatches expected).

### Live result

`check-r7-5` (all 685 hooks replaced, own runtime): **40 of
40 identical**; the counters equal the table above in every suite
(mech_types 1, the five type-load hooks 4 / 8 / 7 / 1 in the C1_01 / C3_09
/ C4_08 / train1-timers suites, `mech_type_select` 10 / 16 / 11 / 1,
`node_list` 28 / 56 / 48 / 7, `col_slot_base` = `dmg_slot_base` 11 / 16 /
12 / 1, `flyer_type_select` 4 / 0 / 1 / 0). The map-capped counts are
`node_remap` / `remap_word` 1,303 / 1,647 in the C1_01 suites, 2,961 /
3,806 in C3_09, 2,953 / 3,763 in C3_10, 2,909 / 3,744 in C5_05 and 273 /
343 in train1-timers (as predicted). `dmg_load`, `collision_load`,
`multiply_q10` and `stream_read` equal `check-h4115`'s. After-load
snapshot `snapshot-r7h-20261013` with the extra regions above (section 14).

### Open

- The bridged element constructors `0x420817`, `0x42067c`, `0x4207db`,
  `0x4117a5`, `0x423098` (verdicts in function_verdicts.md); `0x492d00`,
  `0x492f54`, `0x492bec`, `0x474cd8`, `0x474c38` (platform) and
  `0x4378d8`, `0x4379cc` (H3) keep their verdicts.
## 27. Base flattening and the base-mark raster

The contracts, the recursion and the in-vitro evidence are in
[terrain.md](terrain.md#base-flattening-and-the-base-mark-raster);
this section holds the load-side view, the live counts and the snapshot
reach. **Confirmed live:** `check-h4119`; after-load snapshot
`snapshot-r7i-20261013` (section 14). Evidence: `build\claude\h4-111\`
(logs, `xrefs.py`; no listings).

Five hooks in the group `base_flatten`: `base_flatten` `0x47129c`,
`flood_sum` `0x470fe8`, `flood_fill` `0x471128`, `mark_raster` `0x47136c`,
`cell_flag_get` `0x470e00`. Step 12 of the actor load (section 21) calls
`base_flatten([0x4a0be0])` at `0x4263d9` once per load, after every
building's footprint (`footprint_marks`, section 23) and base layout
(`base_marks` → `mark_raster`, section 17) has set its cell marks: each
8-connected region of marked cells gets the integer mean of its height
bytes (`flood_sum`, then `flood_fill` with the quotient, also into the
row and column beyond the region), then the H3 shading pass `0x46c2e0`
recomputes the shade data from the new heights. So building sites are
flat before the first frame; the heights are what `terrain_height` and the
simulation read from then on. `cell_flag_get` also runs per frame (the H3
cell-object draw `0x46e5a4`, as `cell_flag_set`).

### Live counts

From the map `dbsim-map-20261012` (every hook original, early arming,
all 40 suites), the zone table (section 24) and section 17's base
marks:

- `hk_base_flatten_calls` = 1 and `hk_base_flatten_call_46c2e0` = 1 in
  every suite (the map: `0x47129c` and `0x46c2e0` entered once each; equal
  to `hk_actor_load_call_47129c`).
- `hk_base_flatten_call_470e00` = (1 << n)²: the scan reads every cell's
  mark once.
- `hk_mark_raster_calls` = `hk_base_marks_call_47136c` (the map's
  `0x47136c` entries); `hk_mark_raster_call_470d74` = the base marks' share
  of `cell_mark_set` (section 24's count minus the cells).
- T = the regions = `hk_base_flatten_call_470fe8` = `_call_471128` =
  `hk_flood_sum_calls` = `hk_flood_fill_calls`; V = the visited cells =
  `hk_flood_sum_call_470ddc` = `hk_flood_fill_call_470ddc`;
  `hk_flood_fill_call_470e00` = `hk_flood_sum_call_470e00` (the fill walks
  each region exactly as the sum: the marks are 0 or 1 before, so bit 2 is
  clear in every region cell). The floods' recursion is internal and not
  counted, so the relation to the map is T + 8V = the entries of
  `0x470fe8` (and of `0x471128`); in the suites where that count is not
  capped it fixes T modulo 8 (T is at most the footprints plus the base
  marks of the suite). T and V themselves depend on the building positions
  and radii and were not predicted.

| Suites (launch set) | zone | `hk_base_flatten_call_470e00` | `hk_mark_raster_calls` | `hk_mark_raster_call_470d74` | map `0x470fe8` = T + 8V | T mod 8 |
|---|---:|---:|---:|---:|---:|---:|
| the 19 suites on the installed C1_01 | 123 | 65,536 | 2 | 152 | 1,000 (capped) | – |
| c1_01-unarmed-keys, c1_02-probe, c1_06-cheat-disable | 123 | 65,536 | 2 | 152 | 1,000 (capped) | – |
| c1_08-escort-route, c1_08-scan | 123 | 65,536 | 0 | 0 | 82 | 2 |
| c2_02-identify | 22 | 16,384 | 1 | 24 | 423 | 7 |
| c3_09-loadout-b, -wingman-orders, -orders-matrix | 39 | 16,384 | 2 | 41 | 741 | 5 |
| c3_10-loadout-hercs, -own-building-fire, -detail-options, -hunt-targ | 3333 | 16,384 | 1 | 32 | 563 | 3 |
| c4_05-convoy | 405 | 16,384 | 0 | 0 | 66 | 2 |
| c4_08-razor-plasma, -slope-dive, -joystick | 408 | 16,384 | 1 | 10 | 277 | 5 |
| c4_10-probe | 408 | 16,384 | 1 | 10 | 276 | 4 |
| c5_05-moon-missiles, c5_05-orders-flyer | 501 | 16,384 | 1 | 10 | 372 | 4 |
| train1-timers | 111 | 16,384 | 1 | 32 | 130 | 2 |

c1_08 and c4_05 have four resp. two footprints and no base marks, so T = 2
(V = 10 and 8) unless a footprint falls apart into separate regions. `hk_cell_flag_get_calls` is nonzero everywhere and grows
per frame (the H3 draw); at the first record it is at least the scan's
count plus the floods' reads. Every `*_skipped`/`*_bypassed` 0. Unchanged,
because the bridge still reaches them: `hk_cell_flag_set_*`,
`hk_cell_mark_set_*`, `hk_base_marks_*`, `hk_actor_load_call_47129c` (1).

### After-load snapshot reach

The flattened heights, the shade data, the material bytes (+0Fh) and the
marks (bits 0 to 2 after the flattening) lie in the cell and mark arrays
behind `[0x4a0be0]` (G+0ECh, G+0F0h), which the fixed region `0x4a0be0`
reaches (section 24). Not reached: the sum and count `0x6b5470`/`0x6b5474`
(the last region's; nothing reads them after the load, but they are the
flatten's only other writes). The live run adds them with `[snapshot]
extra = 0x6b5470:8` (`configure-tick-log.ps1 -AfterLoadSnapshotExtra
0x6b5470:8`, together with section 24's `0x4aaa7c:0x34,0x49aac8:0x29` if they
are wanted).

A difference caused by these hooks would lie in the cells (heights, the
material and flag byte +0Fh, the shade data of `0x46c2e0`), the marks, the
extra words `0x6b5470..0x6b5477`, or in the block pairing (no hook
allocates). `cell_flag_get` also accepts `compare` (calls nonzero,
mismatches 0 expected).

### Live result

`check-h4119` (all 704 hooks replaced, own runtime): **40 of
40 identical**; `base_flatten` 1, `_call_470e00` 65,536 in the zone-123
suites and 16,384 elsewhere, `mark_raster` = `base_marks_call_47136c`,
`mark_raster_call_470d74` 152 / 41 / 32 / 24 / 10 / 0 as predicted; the
regions T = `flood_sum` = `flood_fill` calls and the visited cells V =
`flood_sum_call_470ddc` give T + 8V = 82 (c1_08), 423 (c2_02), 741 (c3_09),
563 (c3_10), 66 (c4_05), 277 (c4_08), 276 (c4_10), 372 (c5_05), 130
(train1-timers), the map's counts; in the capped zone-123 suites T = 21 …
26 and V = 232 … 240 (the deepest flood path is at most V + 1 = 241 levels).
After-load snapshot `snapshot-r7i-20261013` (seven extra regions incl.
`0x6b5470:8`): O2 vs R **identical** (scope all and pools) in
acceptance-60s, c3_10-loadout-hercs and c3_09-loadout-b (c3_09's two
original runs differed in 110 places, all absorbed by the noise file
learned from them).

### Open

- The adapters' path buffers (2 × 64 KiB in the proxy's data) are sized
  for the largest zone; no other bound is known.
## 28. Building types and pools

The two load-time functions of the building module (`base.cpp`, the
assertion strings name the file), read from the pinned
`llvm-objdump -d -M intel` listing of the reference DBSIM (the exception
descriptors `0x49766f`, `0x497603` and the strings `0x497712..0x49776d`
read from the file). The verdict rows of function_verdicts.md were the
starting hypotheses and hold. The live counts were predicted from the data
files in `SIMVOL0.VOL` (read in the scratch directory; nothing extracted to
disk). Own code: adapter group `building_types`, core module
`core/src/building_types.c` (the two share nothing with the helpers of
`mission_load.c` except the pool/list pattern of section 15, which
`base_pools` repeats in eight lines). **Confirmed live:** `check-h4119`;
after-load snapshot `snapshot-r7i-20261013` (section 14). Evidence
(scripts and logs, no listings): `build\claude\h4-116\`.

| Hook | VA | Role | Caller |
|---|---|---|---|
| `base_types` | `0x405fac` | stage-2 record 3: the building types, their weapons and collision data, hulks, textures and debris | `0x401d94(2)` through `call [ebx]` at `0x401dac` (registered by `0x401d64(fn, 2)` at `0x40696e`) |
| `base_pools` | `0x405df4` | the building pool and its removal list | the world load `0x424308` at `0x4252a0` (pass 1, section 2 item 9) |

### Call path and live visibility

`0x405fac` is referenced once in the file (the push at `0x40696e`), so the
dispatcher of stage 2 is its only caller; `0x405df4` has one call site
(`0x4252a0`). Both run once per process inside the mission load, after the
early arming of section 13, so with arming on (the default) each hook is
entered exactly **once per suite**. The map `dbsim-map-20261012`
(every hook original, early arming) counts both at 1 in each of the 40
suites.

### Contracts (from the bytes)

Both are cdecl with a `push ebp` frame and set up the runtime record through
`0x49630c` (EAX = the descriptor; its second dword gives the record offset:
−`0E0h` for `0x49766f`, −`28h` for `0x497603`). `0x49630c` keeps EDX and
ECX and returns the old FS:0 head in EAX; the context word is record+10h
(EBP−`0D0h`, EBP−`18h`). The epilogue loads the saved head from the record
into EAX and stores it to FS:0: **EAX = the old FS:0 head**. Every callee is
a guest call with the original's entry registers; the order of every guest
read and write is the original's.

**`base_types` 0x405fac** (no argument; EDI = EBP−30h throughout; four
stream objects in the frame: S1 = EBP−60h, S2 = EBP−90h, S3 = EBP−30h,
S4 = EBP−0C0h). Each file is opened the same way: the path
`0x492bec(name, [0x4a09f0])` stored in a local, the context word set,
`0x472f14(S, path)` (bforms and lc_wpns: EAX = S, ECX = the path
reread from the local; bases: EAX = EDX = the path; basecol: EAX = EDX =
the path, ECX = S), and when the status dword [S+4] is nonzero the fatal report
`0x479c78(0x6c56c8, 5, 0, "base.cpp", line, path)` with the SETE
register = 0 (bforms: line 51Ah, ECX; lc_wpns: 545h, ECX; bases: 550h,
EAX; basecol: 586h, EDX). "read(S, n, p)" is the stream's method at slot
+18h of the table [S], reread before every call.

1. **`bforms`** (context 8): read(S1, 4, `0x4a95a8`) (a dword count N),
   `0x49667c(0, 1Ch, N, 1, 0x406a5c)` → `[0x4a95ac]` (the forms). For each
   form F (loop counter dword EBP−0E8h against the dword `[0x4a95a8]`,
   signed): read(F, 4) the element count K; `[F+4]` =
   `0x49667c(0, 0Ah, K, 1, 0x406a64)`; `stream_read(S1, 10·K, [F+4])`;
   read(F+8, 4); `stream_read(S1, 8, F+0Ch)`; read(F+14h, 4) the list
   count M. When M ≠ 0: `[F+18h]` = `new[](4·M)`, and for i < M (signed):
   `[[F+18h]+4i]` = `new[](M)` (each list gets M bytes, not its length),
   read(S1, 1, EBP−0E9h) the length L, read(S1, sx8(L), `[[F+18h]+4i]`).
   When M = 0: `[F+18h]` = 0. Then `stream_destroy(S1, 2)`.
2. **`lc_wpns`** (context 14h): read(S2, 2, `0x4a95b8`) the count word C,
   `[0x4a95bc]` = `0x49667c(0, 16h, sx(C), 1, 0x406a6c)`, the word
   EBP−0F2h = 22·C, `stream_read(S2, sx(that word), [0x4a95bc])`,
   `stream_destroy(S2, 2)`.
3. **`bases`** (context 20h; S3 stays open until the end):
   `stream_read(S3, 2, 0x4a9598)` the type count word T,
   `[0x4a959c]` = `0x49667c(0, 3Ch, sx(T), 1, 0x406a77)`, `[0x4a95b0]` =
   `new[](6·sx(T))` (ECX = 6T after the POP). For each type B (counter word
   EBP−0FAh, compared as a word, loaded into CX over the high word of ECX;
   the dead word EBP−0FCh = 34h each pass): read(S3, 2) of +0, +2, +4, +6,
   +8; `stream_read(S3, 6, B+0Ah)`; read(S3, 2) of +10h and +12h (the child
   count); `[B+14h]` = `0x49667c(0, 1Eh, sx([B+12h]), 1, 0x406a85)` and
   `stream_read(S3, 1Eh, [B+14h] + 30i)` for i < sx(word +12h, reread);
   `stream_read(S3, 6, B+18h)`; read(+1Eh, 2); `stream_read(S3, 4, B+20h)`;
   read of +24h, +26h; read of +28h..+32h, each address first stored in its
   local EBP−100h..EBP−114h and reloaded (EAX = ECX = the address). Then
   `[B+38h]` = `[0x4a95b0]` + 6·k (k = the counter) when the word +30h is
   nonzero, else 0 (EDX = 0).
4. **`basecol`** (context 2Ch): after the open EBX = `[0x4a95b0]` (read
   before the status test, so the report cannot move it), then
   `collision_read(EBX + 6i, S4)` for i < the word `[0x4a9598]` (compared
   as SI, reread every pass; EAX = S4), `stream_destroy(S4, 2)`.
5. **The count words:** `0x4a9580`, `0x4a9578`, `0x4a9572` ← 0; over the
   types (EAX against sx(`[0x4a9598]`), reread): v = word +2 + 1 (INC of the
   32-bit register, used as a word); when word +6 ≠ 0, `0x4a9572` =
   max(`0x4a9572`, v) (via the local word EBP−11Ah; `jle` keeps v on a
   tie), else `0x4a9578` = max(`0x4a9578`, v) (the stored word goes through
   BX); then `0x4a9580` = max(`0x4a9580`, word +4 + 1). All signed words.
6. **The hulks:** `474cd8(0x4a9584, 0x492bec("bhulks", "dgs"), 2BC0001h,
   0)` (the returned word stored in the dead local EBP−11Ch);
   `[0x4a957c]` = `new[](4·sx([0x4a9578]))`, `[0x4a9574]` =
   `new[](4·sx([0x4a9572]))`, both zeroed dword by dword (the counts
   reread each pass).
7. **The textures** (`[0x4a0a10]` as the directory): `basetex` → bitmap
   `0x45cdf4` at `0x4a9588`, its table `0x46a03c([0x4a9588])` at
   `0x4a958c`, `[[0x6cbee4]]` += 1, `0x48349c([0x4a9588], 3)`, `0x4a9588` ←
   0; `vehtex` the same at `0x4a9590`/`0x4a9594` (the registers differ:
   EDX carries the directory, EAX the counter pointer, EDX the release
   argument; ECX = 0 at the end). Then `[[0x4a9584] + 4i] + 26h` =
   `0x4a9588` for i < sx(`[0x4a9580]`) (reread).
8. **The debris set** `0x40874c(0x4a95c0, "base_deb", 0)`, then
   `stream_destroy(S3, 2)`. Returns EAX = the head, EDX:ECX = the
   destroy's.

**`base_pools` 0x405df4** (count): BX = the argument word (the caller's
EBX high word stays); `new(12h)` → EBP−4 (ECX = 12h); when nonzero,
context 14h, `pool_create([EBP−4], EBX, 26Dh)`, context 8; `[0x4a95a0]` =
ECX = `[EBP−4]`. `new(8)` → EBP−8 (ECX = 8); when nonzero, context 20h,
`MOV AX,19h` (EAX's high word kept), word `[list+6]` = 25,
`new[](100)`, ECX = `[EBP−8]`, `[list]` = the block, word `[list+4]` = 0,
context 8; `[0x4a95a4]` = EDX = `[EBP−8]`. This is section 15's pool/list pair
(section 15) with the count from the argument and size 26Dh. The pushed
capacity slot carries the caller's EBX high word, which `pool_create` never
reads (it uses SI); the adapter, which cannot see EBX, passes 0 there.

**Callees and modes.** Guest calls with the original's entry registers:
`0x49630c` (with the synthetic EBP), new `0x47749c` and new[] `0x4774f0`
(runtime; neither writes its argument slot, so the POP ECX values are the
pushed ones), `0x49667c` with the element constructors `0x406a5c`,
`0x406a64` (return the argument), `0x406a6c` (EDX = +8), `0x406a77`
(EDX = +0Ah, ECX = +18h), `0x406a85` (EDX = +0Ah, ECX = +10h): five empty
bodies, reached only through `0x49667c`; the path builder `0x492bec`, the
stream (`0x472f14`, the method +18h, the hooks `stream_read` and
`stream_destroy`), `0x479c78`, the resources `0x474cd8`, `0x45cdf4`,
`0x46a03c`, `0x48349c` (resource; `0x45cdf4` and `0x46a03c` only read their
argument slot), the debris set `0x40874c` (`debris_set`, section 31), and the
hooks `collision_read` (section 20) and `pool_create` (section 14), which keep
their own modes.

**Registers, stack and shims.** EDX:EAX is reproduced exactly; ECX is
reproduced by the core, and the adapters leave the framework's entry ECX
because nothing reads it: the dispatcher reads no register after
`0x401dac`, and the next stage-2 callback (`light_pools` `0x4076e4`, record
4) loads EAX before `0x49630c` and reaches `new` with EDX/ECX unread; after
`0x4252a0` the world load pops ECX and reloads CX and AX (the high word of
the returned EAX goes on into `0x411df8`'s argument slot, and EAX is
exact). Both bodies read only locals they wrote or that a callee given
their address filled (the stream objects through `0x472f14`, the length
byte through the read); no stack residue, no entry or exit shim. The frame
lives in a zeroed scratch at the original's EBP offsets (`base_types`
124h bytes with EBP at +120h, the lowest local EBP−11Ch; `base_pools` 2Ch
bytes with EBP at +28h); the adapters write the
returned head back to FS:0 as the epilogues do.

**Compare:** refused for both (file I/O, allocation, resource loading, the
runtime record and the construction count); the after-load snapshot is the
acceptance.

### In vitro (group `building_types`)

`invitro/building_types_cases.h`: per hook 100,000 direct cases (quick
tier; 1,001,024 in the full tier, not run)
against logging stubs for all 16 direct callees and the read method of two
arena tables (counts 0..5 or small negatives for the 2- and 4-byte reads,
count changes after `49667c`/`new[]`, a lowered type count after
`collision_read`, a moved slot block after `479c78`, changed bitmap slots
after `46a03c`, a moved counter pointer after `48349c`, table switches
after reads, open failures one time in four, `new` failing one time in
four). Compared: pair, ECX (original against the core), FS:0, the ordered
calls with argument slots, entry registers and the record's context word,
frame addresses relative to the record, the arena, the image globals
(`0x4a09f0`, `0x4a0a10`, `0x4a9572..0x4a95bf`, `0x6cbee4`) and the callee
counter deltas; the adapter separately (pair, calls, memory). 256 installed
31/a7 cases per hook (fast and preserving dispatch). Real-callee pass per
hook (2,048 cases): real `0x49630c` and `0x49667c` (with the element
constructors) over a bump allocator; `collision_read`, `collision_entry`
and `collision_bounds` nested under `base_types` (entered 3,482 / 6,242 /
6,164 times), `pool_create` under `base_pools` (2,048), each in
original/replaced modes, the hook under test original or replaced. 21
branch sites, all both ways. One caught mutation per hook (`MUTATIONS.md`).
Core unit test `//core:building_types_test` (worked out by hand: one form
with a list, two `lc_wpns` records, two building types, the four open
failures, both pool paths).

### Live counts

Once per suite in all 40 suites (the four files exist once, in
`SIMVOL0.VOL`, so every suite reads the same data: `BFORMS.DAT` 17 forms, 145 elements, 11 forms with lists, 112
lists; `LC_WPNS.DAT` 3 records; `BASES.DAT` 65 types, 110 children;
`BASECOL.DAT` 65 entries):

| Counter | every suite |
|---|---:|
| `hk_base_types_calls`, `_call_49630c`, `_call_474cd8`, `_call_40874c` | 1 each |
| `hk_base_types_call_492bec` | 7 |
| `hk_base_types_call_472f14`, `_call_472f64` | 4 each |
| `hk_base_types_call_45cdf4`, `_call_46a03c`, `_call_48349c` | 2 each |
| `hk_base_types_call_40ccf8` | 65 |
| `hk_base_types_call_49667c` | 85 (18 + 1 + 66) |
| `hk_base_types_call_4735b8` | 341 (34 + 1 + 306) |
| `hk_base_types_call_4774f0` | 126 (123 + 1 + 2) |
| `hk_base_types_slot_18` | 1,317 (276 + 1 + 1,040) |
| `hk_base_types_call_479c78` | 0 |
| `hk_base_pools_calls`, `_call_49630c`, `_call_471ad8`, `_call_4774f0` | 1 each |
| `hk_base_pools_call_47749c` | 2 |

Unchanged (the bridge still reaches them): `hk_collision_read_calls` (76
in the C1_01 suites, 66 in train1-timers), `hk_pool_create_calls` (12),
`hk_stream_read_calls`, `hk_stream_destroy_calls`. Every
`*_skipped`/`*_bypassed` 0.

### After-load snapshot reach

Every global the two write lies in the fixed region `0x4a9572..0x4a9dfc`
of section 13 (`0x4a9572..0x4a95bf`, and `0x4a95c0` onwards, where
`0x40874c` writes the debris set), and every block they allocate hangs off
it (`0x4a95a0`, `0x4a95a4`, `0x4a95ac` with the element and list blocks,
`0x4a95bc`, `0x4a959c` with the child blocks, `0x4a95b0` with the COL
descriptors, `0x4a957c`, `0x4a9574`, the texture tables `0x4a958c`,
`0x4a9594`). No `[snapshot] extra` region is needed. The stream objects
and the dead locals are stack, not snapshotted.

### Live result

`check-h4119` (all 704 hooks replaced, own runtime): **40 of
40 identical**; in every suite `base_types` and `base_pools` 1,
`base_types_call_492bec` 7, `_call_49667c` 85, `_call_4735b8` 341,
`_slot_18` 1,317, `_call_40ccf8` 65, `base_pools_call_47749c` 2;
`collision_read` (76 in the C1_01 suites, 66 in train1-timers) and
`pool_create` 12 unchanged. After-load snapshot `snapshot-r7i-20261013`
(seven extra regions incl. `0x6b5470:8`): O2 vs R **identical** (scope all
and pools) in acceptance-60s, c3_10-loadout-hercs and c3_09-loadout-b
(c3_09's two original runs differed in 110 places, all absorbed by the
noise file learned from them).

### Open

- The five element constructors `0x406a5c`, `0x406a64`, `0x406a6c`,
  `0x406a77`, `0x406a85` stay original (reached only through `0x49667c`);
  verdict rows in function_verdicts.md.
## 29. L7 world load

L7 of section 10, the world load `0x424308` (script pass 1), and the four
leaves only it calls, read from the pinned `llvm-objdump -d -M intel`
listing of the reference DBSIM (the exception descriptor `0x49a7fc`, the
file-name strings `0x49a9cb..0x49aa34` and the stream table `0x4a1738` read
from the file). The verdict rows of function_verdicts.md were the starting
hypotheses: `0x424308` H4 holds, `0x405ebc` (platform) and `0x42e010` (H3)
stay guests, with the evidence below. Own code: adapter group
`mission_world`, core appended to `core/src/mission_load.c`; an own group
rather than `mission_actors`, because the harness (three synthetic files
behind stub reads, 31 stub ids) shares nothing with the actor load's.
**Confirmed live:** `check-h4100`; after-load snapshot
`snapshot-r7j-20261013` (section 14). Evidence: the scripts and logs of
`build\claude\h4-100\` were lost at a clean-up; the report is in
`<es2-data>\logs\h4-100-20261013\report.md`.

| Hook | VA | Role | Call site |
|---|---|---|---|
| `world_load` | `0x424308` | script pass 1: launch files, mission tables, selection, compaction, pools, type loads, terrain | `0x4617c8` in the mission load |
| `actor_list_init` | `0x411df8` | the actor list `(list, count)`: `[list]` = `new[](4 × sx(count))`, word `list+4` = count | `0x4252c6` |
| `gun_slot_table` | `0x40fecc` | `[0x4a98e4]` = `new[](6 × sx(word 0x4a98e8))`, the gun-list table (also for 0) | `0x4252ce` |
| `dmg_slot_table` | `0x40d218` | `[0x4a9828]` = `new[](12 × sx(word 0x498856))`, only for a nonzero word | `0x4252d3` |
| `col_slot_table` | `0x40cbec` | `[0x4a9824]` = `new[](6 × sx(word 0x4987dc))`, only for a nonzero word | `0x4252d8` |

No dword of the file holds any of the five addresses (scan of the file), so
these call sites are all. Every other callee stays game code behind the guest
bridge with its own mode:

| Callee | Role | Class / mode |
|---|---|---|
| `0x49630c` | runtime frame record (descriptor `0x49a7fc`: EBP−54h, context word EBP−44h) | RTL, through `weapons_impact_guest_call` |
| `0x49681a` | `_memset` (ten table and frame clears) | RTL |
| `0x45ee00`, `0x492b90` | the file names (`data\mission.var`, `data\player.mec`, `data\mission.str`, `data\script` + `[0x4a09f0]`) | platform (file) |
| `0x47ad40`, `0x47af98`, slot `+18h` (= `0x47af98`), `0x47ad78`, `0x479c78` | the three streams in the frame, their reads and closes, the fatal report of a failed open | platform (file) |
| `0x453878`, `0x4539a0`, `0x496766` | the STR reader into `0x4aa650`, its string pointers at `0x4aa660`, `_strlen` | platform, RTL |
| `0x42e010` | the world loader (WLD, palettes, shade and remap tables; the flat-object pool `0x4097a8` inside it) | H3 (confirmed below) |
| `0x405ebc` | the shape list of a building type (`bases_an.dts` / `bases.dgs` through `0x474de4`) | platform (confirmed below) |
| `0x47749c`, `0x4774f0`, `0x49667c` (element constructors `0x4953c4`, `0x427207`) | new, new[], the array construction | RTL |
| `region_init`, `timer_init`, `group_array`, `mech_type_select`, `flyer_type_select`, `pool_create`, `base_pools`, `mech_types`, `flyer_types`, `zone_load` and the four leaves | | hooks, their own modes |

### Call path and live visibility

`0x424308` has one call site, `0x4617c8` in the mission load `0x461608`, once
per process; the four leaves each have one call site in it. With early arming
(section 13, the default) the hooks are installed at `0x45f42b`, before the
load, so their counters are constant from the first tick record on. The
map `dbsim-map-20261012` (every hook original, early arming) counts
`0x424308`, `0x411df8`, `0x40fecc`, `0x40d218`, `0x40cbec`, `0x42e010`,
`0x405df4`, `0x42049c`, `0x422fb4` and `0x42789c` at exactly 1 in all 40
suites, and `0x405ebc`, `0x41ff10`, `0x422e90` (whose only caller is the world
load) exactly at the counts predicted below in all 40 suites
(`world_counts.py`).

### Contract (from the bytes)

No arguments; EBX, ESI, EDI saved and restored. Frame `add esp,-0E54h`: the
record EBP−54h, three 12-byte stream objects (player.mec EBP−24h, which EDI
holds at first; mission.var EBP−18h; the script EBP−0Ch; table +0, status
+4), the pool and list headers EBP−28h .. EBP−34h, the count words of the
lists EBP−56h (mechs M), −58h (flyers), −5Ah (buildings), −62h (groups), −64h
(orders), −66h (objectives), the compaction counters EBP−5Ch/−5Eh/−60h, the
cursors EBP−6Ch .. EBP−14Ch, the local type arrays (mechs EBP−9E4h, 100
words; flyers EBP−0A48h, 50; buildings EBP−0B60h) and the record buffers
(point EBP−0B6Ch, trigger indices EBP−0B7Ch, timer EBP−0B94h with the ten
trigger pointers EBP−0BBCh, mech EBP−0C44h, flyer EBP−0CA0h, building
EBP−0CD4h, order EBP−0CE4h, group 0 EBP−0D80h, group EBP−0E1Ch, objective
EBP−0E54h). EBX is the loop counter (compared as BX, signed words), ESI and
EDI the record cursors. "read" is `0x47af98(stream, size, buffer)`, "vread"
the stream's slot +18h with the same arguments (the table reread from the
stream object before every call). In order:

1. `0x49630c` (EAX = the descriptor, the entry EDX/ECX). `_memset` of the
   mech flags `0x4aa72a`, remap `0x4aa78e`, the local mech types (200 bytes,
   EDX = the address), the MEC flags `0x4aa7f2` (100 each), the flyer flags
   `0x4aa856` and remap `0x4aa888` (50), the local flyer types (100 bytes,
   ECX = the address), the building flags `0x4aa8ba`, remap `0x4aa946` and
   factory bytes `0x4aa9d2` (140 each); every other register is the previous
   call's.
2. Context 8: `0x45ee00("data\mission.var")` (`POP ECX` = the string),
   `0x47ad40(EBP−18h, name)` (EAX = the stream); a nonzero status (read
   twice) reports `0x479c78(0x6c56c8, 5, 0, "WORLD.CPP", 0A5h, "mission.var")`
   (EDX = the SETE result); read 2000 bytes into `0x4a9e70`; words `0x4a9e98`
   (variable 20), `0x4a9e84` (10) and `0x4a9e9a..0x4a9ec5` (21..42) = 0 (EAX
   ends at 2Bh); `0x47ad78(stream, 2)`.
3. Context 14h: `data\player.mec` the same way (EAX = the name, the report
   with line 0B2h and "player.mec"); vread the player ordinal `0x4aaa64`
   (ECX = the table) and N `0x4aaa5e` (EAX = the table); `new[](126 ×
   sx(N))` (EDX = sx(N), ECX = the size; `POP ECX`) to `0x4aaa60`; per
   126-byte record (i < N reread): vread words +0, +2, +4, +6 (EAX = the
   address, EDX = the table), read 2 × sx(word +6) bytes to +8 and to +20h
   (the word reread), vread word +3Ah, read 26 bytes to +3Ch, 20 to +56h, 20
   to +6Ah. A file record is 76 + 4 × w bytes (w = word +6), the runtime
   record 126; close.
4. Byte `0x4d2571` = (word +4 of MEC record 0 = 8), read through the dword
   EBP−6Ch also when N is 0; `0x45ee00("data\mission.str")`,
   `0x453878(0x4aa650, name)`, `0x4539a0(0x4aa650, 0x4aa660, 32h, 0)` (the
   third argument is not read: the count is the word at the start of the STR
   body, AX); for i < sx(AX) (EBP−70h): `_strlen` of `[0x4aa660 + 4i]` (`POP
   ECX` = the string), and when the byte before the terminator (for an empty
   string the byte before the string) is 0Ah it becomes 0.
5. Context 20h: `0x492b90(0x45ee00("data\script"), [0x4a09f0])` (the
   extension pushed first, before the inner call) to EBP−74h, `0x47ad40(EBP−0Ch,
   path)` (EAX = the stream, ECX = the path); a failed open reports with
   "WORLD.CPP", line 0DCh and the path (ECX = the SETE result, EDX = the
   path); read the 20-byte header to `0x4a9e4e`; word `0x4a9e52` = 0;
   `0x42e010(0x4a9e4e)`.
6. The coordinates: vread the count `0x4a9e06`; when nonzero
   `0x49667c(0, 0Ch, sx(n), 1, 0x4953c4)` (EAX = sx(n)) to `0x4a9e0c`, the
   map rectangle min y `0x4aa644`, min x `0x4aa640` = 7FFFFFFFh, max y
   `0x4aa64c`, max x `0x4aa648` = 80000000h (in that order), then per
   12-byte point (count reread) a read to EBP−0B6Ch, the three dwords copied
   and the four signed comparisons (each dword reread). The rectangle is
   untouched without coordinates.
7. The angles: vread `0x4a9e08`; when nonzero `new[](2 × sx(n))` to
   `0x4a9e10`, per word read to EBP−0FEh: 182 × sx(word) (LEA/SHL/SUB/SHL/SUB/ADD)
   stored as a word.
8. The routes: vread `0x4a9e14`; when nonzero `new[](6 × sx(n))` to
   `0x4a9e18`; per 6-byte route: vread the point count word +0, `new[](4 ×
   sx(count))` to +2, per point (CX = the count word, reread, against the
   counter word EBP−106h) vread an index to EBP−108h and store 12 × sx(index)
   + `[0x4a9e0c]`.
9. The regions: vread `0x4a9e1c`; when nonzero `new[](10 × sx(n))` to
   `0x4a9e20`; per region read 6 bytes to EBP−114h and `region_init(region,
   EBP−114h, [0x4a9e0c], ECX)` with four slots: the fourth is ECX with CX =
   the coordinate count (unread by `region_init`).
10. The triggers: vread `0x4a9e24`; when nonzero `new[](58 × sx(n))` to
    `0x4a9e28`; per 58-byte runtime trigger T: vread words +0 and +2, read
    the 8 region indices (16 bytes) to EBP−0B7Ch, 20 bytes to +0Ch and to
    +20h, 10 bytes to EBP−120h (never used), vread word +34h and a word to
    EBP−122h; dword +36h = its sign extension, word +34h − 1, word +0Ah = 0,
    word +4 = the count of leading non-negative indices (at most 8); when
    nonzero `new[](4 × count)` to +6 and the pointers 10 × sx(index) +
    `[0x4a9e20]`. A file trigger is 74 bytes.
11. The timers: vread `0x4a9e34`; when nonzero `0x49667c(0, 31h, sx(n), 1,
    0x427207)` (49-byte timers) to `0x4a9e38`; per 24-byte record: the first
    trigger (word +0; a negative word gives 0, otherwise 58 × sx + `[0x4a9e28]`)
    to EBP−134h, the ten pointers of words +4 .. +16h to EBP−0BBCh, then
    `timer_init(49 × sx(i) + [0x4a9e38], first, EBP−0BBCh, sx(word +2))` (EAX =
    first, EDX = sx(i), ECX = the timer).
12. The authored lists: vread the mech count to EBP−56h and per 134-byte
    record the type word +28h into the local array; flyers (92 bytes, +2Ch),
    buildings (52, +0) likewise; the orders (14 bytes) are only read.
13. The groups: the compaction counters = 0; vread the count to EBP−62h; when
    nonzero `group_array(0x4a9e00, ECX)` (CX = the count) and per 156-byte
    record: record 0 to EBP−0D80h (kept), every other one to EBP−0E1Ch with
    word +6Eh = 1, and each of its 20 non-negative member words +32h marks
    `0x4aa72a`, `0x4aa856` or `0x4aa8ba` + sx(word) for the class word +28h 0,
    1 or 2 (reread for each test; for class 2 also the factory byte
    `0x4aa9d2` + sx(word) = the low byte of +6Eh, so 1).
14. The objectives (54 bytes) are only read; the string indices: vread the
    count `0x4a9e44`, when nonzero `new[](2 × sx(n))` to `0x4a9e48` and one
    vread of 2n bytes into it.
15. Compaction: consecutive remap indices (counter EBP−5Ch, its low byte) to
    `0x4aa78e + i` for the selected mechs i < M; group 0's non-negative
    slots k get `0x4aa78e + sx(slot)` = counter + k (the remap index the k-th
    MEC actor receives next); flyers (EBP−5Eh into `0x4aa888`) and buildings
    (EBP−60h into `0x4aa946`) likewise; then each MEC record i < N (reread)
    is appended as mech M + i (AX = M + BX): flags `0x4aa7f2` and `0x4aa72a`,
    type word = MEC word +4 into the local array, remap = counter (+1); M +=
    N.
16. The type selects: `mech_type_select(type)` per selected mech i < M (EDX
    = the type word over the previous EDX; `POP ECX`),
    `flyer_type_select(type)` per selected flyer, `0x405ebc(type, sx(byte
    0x4aa9d2 + i))` per selected building (EAX = the type word over the flag
    pointer, EDX = the byte's address, ECX = the byte).
17. The MECH pool: `new(12h)` (`POP ECX`) to EBP−28h; when not null, context
    38h, `pool_create(H, the mech counter in DX, 36Ah)` (EAX = ECX = H),
    context 2Ch; `[0x4a9b7a]` = H (also 0). Its removal list: `new(8)`; when
    not null, context 44h, word +6 = 19h (CX), `new[](100)`, `[list]` =
    the block, word +4 = 0, context 2Ch; `[0x4a9b7e]` = the list. The FLYER
    pool (291h, contexts 5Ch/50h) to `0x4a9db9`, its list (context 68h) to
    `0x4a9dbd` the same way.
18. `base_pools(the building counter)` (`POP ECX`); `actor_list_init(0x4a9af8,
    mechs + flyers + buildings)` (CX summed, the word EBP−142h, AX = it);
    `gun_slot_table`, `dmg_slot_table`, `col_slot_table`, `mech_types`,
    `flyer_types` (no arguments, the previous registers);
    `zone_load(word 0x4a9e50, word 0x4a9e4e)` (ECX/EDX = the words over the
    previous registers).
19. Word `0x4a9e6a` = 0, `[0x4a9e6c]` = `new[](200)`, word `0x4aa728` = header
    word 4 (`0x4a9e56`); `0x47ad78(script stream, 2)`. EDX:EAX = its result;
    ECX = the saved head `[EBP−54h]`, which the epilogue stores to FS:0.

**The leaves.** `actor_list_init 0x411df8 (list, count)`: `new[]` with EAX = 4
× sx(word count), the entry EDX/ECX; `POP ECX`; `[list]` = EAX, then word
`list+4` = the count word. `gun_slot_table 0x40fecc`: EAX = sx(word
`0x4a98e8`), EDX = 6 × EAX, `new[](EDX)` with the entry ECX, `POP ECX`,
`[0x4a98e4]` = EAX (no zero test). `dmg_slot_table 0x40d218` and
`col_slot_table 0x40cbec`: BX = the word `0x498856` / `0x4987dc`; for 0 nothing
(EAX/EDX/ECX the entry's), else EAX = sx(BX), EDX = 12 / 6 × EAX, `new[](EDX)`,
`POP ECX`, `[0x4a9828]` / `[0x4a9824]` = EAX. All four return new[]'s EDX:EAX
and ECX = the size.

**Registers, stack and shims.** The core follows EAX/EDX/ECX through every
instruction, so each guest call gets the original's entry registers and slots
(`POP ECX` reloads the pushed value; none of the popped callees writes its
slot). The results are dead at the callers: after `0x4617c8` the mission
load writes ECX (`MOV ECX,19h`) and AX and loads EAX before pushing it; after
the leaves ECX flows only into `new[]` (which reads no register before
writing it) or, through a zero damage or collision word, into `mech_types`,
whose `0x49630c` and `0x492d00` read neither EDX nor ECX first (section 26), so
the framework's entry ECX of a replaced leaf changes nothing. The frame lies in
a zeroed scratch at the original's offsets (`ES2_WORLD_LOAD_FRAME`, the record
through the bridge's EBP). No local is read before it is written except one
case: **a script without groups** (count 0 or negative) never reads record 0,
so the compaction reads group 0's slots `EBP−0D4Eh .. EBP−0D27h` from stack
residue; the replacement reads zeros (outside the contract: every launch set
and every shipped script has groups, at least group 0 with the MEC actors).
The guest callees run deeper below the replaced frame; the direct non-hook
callees and their callees (`0x453878`/`0x4538d0`, `0x4539a0`/`0x453918`,
`0x42e010` with its 16 direct callees, `0x405ebc`/`0x474de4`/`0x492bec`, the
stream helpers, `0x49667c`, new/new[]) read no local before writing it
(`residue.py`, a linear scan; the flagged reads are the record heads and the
buffers callees fill). The guest callees receive the bridge's EBX/ESI/EDI
instead of the original's loop counter and cursors; none reads one before
saving it (`0x405ebc` and `0x42e010` push them first; the hooks' own
contracts; `base_pools` pushes its count with its caller's EBX high word,
which `pool_create` does not read). No entry or exit shim.

**Verdicts of the guest callees.** `0x42e010` stays a guest (H3,
confirmed): it writes the copied header `0x4cfcc4..0x4cfcd7`, the WLD words and
table pointers `0x4cfcd8..0x4cfd07`, the horizon painter and its words
`0x49aee0..0x49aefb`, the draw state `0x4a5b00..0x4a5b0b` and, through its
callees, palettes, shade and remap tables and the terrain texture table; every
other reader of those addresses is drawing or presentation code (`0x428c60`,
`0x429834`, `0x42e700`, `0x42e760`, `0x42e878`, `0x42e8e8`, the damage-palette
toggles `0x42ea44`/`0x42ea70`/`0x42ea94`, `0x45d548`, `0x45d85c`, the DTS draw
`0x474fa8`, the projectile draw slot `0x40a120`; `xrefs.py`), and its one
simulation callee, the flat-object pool `0x4097a8`, is own code (`flat_pools`, section 31) at
its own entry. `0x405ebc` stays a guest (platform, confirmed): a lazy
cache of the type's shape list in the arrays `[0x4a9574]` (word +6 of the type
≠ 0: `bases_an.dts`, ANShape `1E0003h`) or `[0x4a957c]` (`bases.dgs`,
`2BC0001h`) that `base_types` allocated, loaded once through `0x474de4`, and
the shape's material pointer +26h (`0x4a9588`, `0x4a9590`, or `0x4a9d9a` for
type 34h); **it reads only its first argument**: the factory byte the world
load passes is never read. `0x4539a0` (the STR string-pointer splitter, also
called by the HUD and dialog text loaders) is platform; the timer element
constructor `0x427207` is an empty body (EAX = its argument) reached only
through `0x49667c` and stays original (verdict rows in function_verdicts.md).

**Corrections to section 2:** the player.mec records are variable-length in
the file (76 + 4 × the item count of word +6) into fixed 126-byte runtime
records; the vehicle-8 test reads record 0 also when N is 0; the STR count is
the body's word 0 (the 50 passed to `0x4539a0` is not read) and an empty
string has its preceding byte tested for LF; the coordinates come from the
array construction `0x49667c` (not new[]) and the rectangle is set only with
coordinates; the angle words are 182 × the file word; a trigger is 74 bytes in
the file and 58 at runtime (the region count is the leading non-negative
indices, at most 8); the timers are 49-byte objects from `0x49667c` with the
empty constructor `0x427207`; `0x405ebc` ignores the factory byte; the
"per-type tables" `0x40fecc`, `0x40d218`, `0x40cbec` are the gun-list,
damage-slot and collision-slot tables sized by the running slot words of the
type selects; without groups the group-0 slots are stack residue.

**Compare:** refused for all five (the world load: G V A I R, the streams and
allocations, the virtual read, the RNG draws of `terrain_cells` below
`zone_load`; the leaves: G A, new[]); the after-load snapshot is the
acceptance.

### In vitro (group `mission_world`)

`invitro/mission_world_cases.h`, quick tier (100,000 direct cases per hook;
the full tier, 1,001,024, not run). **World load:**
logging stubs for every game callee (31 stub ids; two arena stream tables
whose slot +18h are read stubs; memset fills), three synthetic files per case
(mission.var, player.mec with 0..3 variable-length records, a script with
every list: counts zero, small or −1..−3, region indices and trigger
words, group classes 0..3 and −1, members −3..139); the core runs with its
scratch frame at the original's frame address (its own code on a static
stack), so frame offsets and registers compare raw, and with a zero stack
pattern the whole frame EBP−0E54h .. EBP is compared too (every local the
original writes, dead stores included). Compared: the pair, ECX, FS:0, the
ordered calls with argument slots (and the bridge's slot count), entry
EAX/EDX/ECX, the record's context word and the frame bytes `region_init` and
`timer_init` read (hashes), the 64 KiB arena and the image globals (the
script tables, header, variables, rectangle, STR object and pointers,
selection, remap and MEC fields `0x4a9e00..0x4aaa65`, the pools and lists,
the type and slot tables and words, the actor list, `0x4d2571`, `0x6cbee4`).
Half of the other cases let the stubs change what the original reads
afterwards (the stream's table, counts the loops reread, the coordinate,
trigger and timer bases, selection flags and factory bytes, the header words
read at the end), the other half run over a nonzero stack pattern (and keep
their groups). In even cases the adapter is compared as well (stack values
masked) with the 31 callee counter deltas. 256 installed 31/a7 cases. One
real-callee pass (4,096 cases): the real `0x49630c` and `0x49667c`, a bump
allocator for new and new[], and fourteen nested hooks (`region_init`,
`timer_init`, `group_array`, `mech_type_select`, `flyer_type_select`, the
three slot bases, `pool_create`, `base_pools` and the four leaves) in random
original/replaced modes per case, the hook under test original or replaced.
**Leaves:** new[] stubbed, 100,000 direct cases each (the original, the core
with ECX, the adapter with its counter delta), 256 installed cases. 84 branch
sites (82 of the world load, one each of the damage and collision tables),
all both ways, none PROVED. One caught mutation per hook (`MUTATIONS.md`).
Core unit test `//core:mission_load_test`: a worked script with every list
and two MEC records (125 calls checked) and the four leaves.

### Live counts

`hk_world_load_calls`, `hk_actor_list_init_calls`, `hk_gun_slot_table_calls`,
`hk_dmg_slot_table_calls`, `hk_col_slot_table_calls` = 1 in every suite, and
each leaf's `_call_4774f0` = 1 (every suite selects a mech, so the damage and
collision slot words are nonzero). The world load's callee counters from the
launch sets (`build\claude\h4-100\world_counts.py`, the selection rule above;
the 19 suites without a launch set run the installed C1_01 of the launcher
chain's `data-backup`, two MEC records): in every suite `_call_49630c` 1,
`_call_49681a` 10, `_call_45ee00` 4, `_call_47ad40` 3, `_call_47ad78` 3,
`_call_479c78` 0, `_call_453878`, `_call_4539a0`, `_call_492b90`, `_call_42e010`
1, `_call_47749c` 4, `_call_471ad8` 2, `_call_423d38` 1, and `_call_405df4`,
`_call_411df8`, `_call_40fecc`, `_call_40d218`, `_call_40cbec`,
`_call_42049c`, `_call_422fb4`, `_call_42789c` 1 each; per launch set:

| Suites (launch set) | `47af98` | `slot_18` | `4774f0` | `496766` | `49667c` | `423358` | `423104` | `41ff10` | `422e90` | `405ebc` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| the 19 suites on the installed C1_01 | 254 | 108 | 24 | 11 | 1 | 8 | 0 | 10 | 4 | 50 |
| c1_01-unarmed-keys (c1_01-unarmed) | 249 | 103 | 24 | 11 | 1 | 8 | 0 | 9 | 4 | 50 |
| c1_02-probe | 214 | 63 | 16 | 12 | 2 | 5 | 1 | 13 | 2 | 50 |
| c1_06-cheat-disable (c1_06-maverick) | 246 | 98 | 27 | 17 | 2 | 6 | 1 | 16 | 4 | 54 |
| c1_08-escort-route, c1_08-scan (c1_08-escort) | 97 | 62 | 16 | 18 | 1 | 5 | 0 | 8 | 4 | 4 |
| c2_02-identify | 131 | 66 | 17 | 10 | 1 | 6 | 0 | 11 | 4 | 15 |
| the three C3_09 suites (c3_09-loadout-b) | 158 | 70 | 17 | 12 | 1 | 6 | 0 | 16 | 0 | 32 |
| c3_10-loadout-hercs, -own-building-fire, -detail-options; c3_10-hunt-targ (c3_10-targ-eo) | 142 | 67 | 14 | 10 | 2 | 2 | 3 | 16 | 0 | 25 |
| c4_05-convoy | 97 | 71 | 21 | 8 | 1 | 4 | 0 | 13 | 2 | 2 |
| the three C4_08 suites (c4_08-razor-plasma) | 138 | 64 | 20 | 12 | 1 | 7 | 0 | 11 | 1 | 27 |
| c4_10-probe | 108 | 62 | 17 | 10 | 1 | 2 | 0 | 9 | 2 | 24 |
| c5_05-moon-missiles, c5_05-orders-flyer | 174 | 84 | 20 | 8 | 1 | 3 | 0 | 15 | 2 | 31 |
| train1-timers (train1) | 116 | 89 | 11 | 8 | 2 | 2 | 14 | 1 | 0 | 11 |

They agree with the confirmed counts of the callees (`region_init`,
`timer_init`, `group_array` of section 14, `mech_type_select` and
`flyer_type_select` of section 26) and, for `0x405ebc`, `0x41ff10` and
`0x422e90`, with the map in all 40 suites. Every `*_skipped`/`*_bypassed`
0. The counters of the nested hooks are unchanged (the bridge still reaches
them): `region_init`, `timer_init`, `group_array`, `mech_type_select`,
`flyer_type_select`, `pool_create` (12), `base_pools`, `mech_types`,
`flyer_types`, `zone_load` and the type loads below them, `hk_rng_next_*`
(the terrain draws).

### After-load snapshot reach

Every global the world load itself writes lies in the fixed regions of
section 13: the tables, header, variables, rectangle, STR object, string
pointers and result pairs in `0x4a9e00..0x4aa728`, word 4, the selection,
remap and MEC fields in `0x4aa728..0x4aaa7c`, the pools and lists
`0x4a9b7a`/`0x4a9b7e`/`0x4a9db9`/`0x4a9dbd` in `0x4a9572..0x4a9dfc`, the
vehicle-8 byte `0x4d2571` in the globals object; the leaves' tables
`0x4a98e4`, `0x4a9828`, `0x4a9824` and the actor list `0x4a9af8` lie in
`0x4a9572..0x4a9dfc` too, and every block they allocate hangs off them. The
slot words the type selects advance (`0x4987dc`, `0x498856`) are in the
current extras (`0x4987d8:8,0x498850:8`). Not reached and written only by the
guest world loader `0x42e010` (identical input in both runs): the copied
header and WLD words `0x4cfcc4..0x4cfd07` and the painter globals; optional
extra `0x4cfcc4:0x44` would show that `0x42e010` received the same header. No
new extra region is needed.

A difference caused by these hooks would lie in the variables, the MEC
block, the STR strings, the header, the coordinate, angle, route, region,
trigger and timer blocks, the rectangle, the selection, remap and factory
tables, the MECH and FLYER pools and lists, the actor list, the three slot
tables, the result pairs, `0x4aa728`, `0x4d2571`, or in the block pairing
(the hooks allocate the same sizes in the same order through the same new,
new[] and `0x49667c`).

### Live result

`check-h4100` (all 709 hooks replaced, own runtime): **40 of
40 identical**, every skipped/bypassed/mismatch counter 0; each of the five
hooks 1 per suite; `world_load_call_49681a` 10, `_45ee00` 4, `_47749c` 4,
`_471ad8` 2 everywhere; the per-launch-set callee counts equal the table
above (254 / 108 / 24 / 11 / 1 / 8 / 0 / 10 / 4 / 50 in the C1_01 suites,
158 / 70 / 17 / 12 / 1 / 6 / 0 / 16 / 0 / 32 in C3_09, 116 / 89 / 11 / 8 /
2 / 2 / 14 / 1 / 0 / 11 in train1-timers); `region_init`, `timer_init`,
`mech_types`, `zone_load`, `base_pools`, `pool_create` equal
`check-h4119`'s. After-load snapshot `snapshot-r7j-20261013` (eight extra
regions incl. the guest `0x42e010`'s `0x4cfcc4:0x44`): O1 = O2 and O2 vs R
**identical** (scope all and pools) in acceptance-60s,
c3_10-loadout-hercs and c3_09-loadout-b.

### Open

- A script without groups reads stack residue (above); outside the
  contract.
## 30. The mission-load root

L9 of section 10: the root `0x461608` itself, read from the pinned
`llvm-objdump -d -M intel` listing of the reference DBSIM (the strings
`0x49f89a..0x49f8b1` read from the file). Section 1's call tree was the
starting hypothesis; the corrections are listed at the end. **Validated in
vitro; the live check is pending** (below). Evidence (scripts and logs, no
listings): `build\claude\h4-101\`.

Own code: hook `mission_root` in adapter group `mission_root`, core
`core/src/mission_root.c` (its own module: the root shares no helper with
the load pieces), with an **entry shim** (`mission_root_entry`, chained
from `weapon_syncs_capture_entry`; the end of the chain is
`mission_root_capture_entry`). Of its 63 direct callees 15 are hooks
(`stage_dispatch`, `world_load`, `actor_load`, `removal_append`,
`flush_explosions`, `flush_lights`, `flush_attached`, `flush_debris`,
`pool_prev`, `terrain_height`, `order_setup`, `cockpit_sync`,
`camera_update`, `dist_gate`, `render_root`) and keep their own modes
through the bridge; the other 48 stay guest calls with a verdict row in
function_verdicts.md's Verdicts table (H3, platform or, for the bare RET
`0x463370`, "H4 (stays original)"). None of the 48 is simulation code: they
draw, load resources, talk to the window, the sound driver or MCI, or build
cockpit and HUD objects. With these verdicts the root's frontier row
closes.

### Arming: how the root can be a hook

The proxy installs every hook in one step (`initialize_forwarding`), and with
L0's early arming (section 13, the default) that step runs in the stub at the
root's only call site `0x45f42b`, before the root's first instruction; the
stub then jumps to `0x461608`. So the root needs no new mechanism: its
descriptor is installed with the others, its entry shim is put in place by the
capture chain like every other shim, and the stub's jump enters the shim. The
arming moment of the other hooks does not change: it is still the step at
`0x45f42b`; the root's own first MCI call (`0x46313c` at `0x461dc3`, the old
arming point) finds the `InitOnce` done, as it did before with early arming.
Nothing in `early_arming.h` changes: the stub restores the call's rel32 and
jumps to the target address, which now holds the detour.

Without early arming (`ES2_EARLY_ARMING=0`) the proxy still arms at that first
MCI call inside the running original: the detour is written while `0x461608`
executes (its prologue bytes are long past), the one call is never
intercepted and `hk_mission_root_calls` stays 0, like every other load-only
hook (section 8). Nothing breaks; the replacement is simply not reached.

### Call path and live visibility

One call site, `0x45f42b` in the game main `0x45f160`, once per process. The
The map `dbsim-map-20261012` (early arming, every hook original) counts
`0x461608` at exactly 1 in all 40 suites. After it the game main pushes 3 and
calls `stage_dispatch` (`0x45f430`), so EAX, EDX and ECX at the root's return
reach the stage-3 callbacks: the replacement returns all three exactly
(through the shim).

### Contract (from the bytes)

No arguments. Frame `add esp,-150h`; EBX, ESI, EDI saved at EBP−154h, −158h,
−15Ch (outgoing ESP EBP−15Ch for every call; the only other pushes, the
`push esi; push edi` around the `movsd` at `0x4616be`, lie in the next call's
argument slots). EDI = the view block G = `0x4d24bc` throughout; ESI = EBP−8
until the display walk; EBX = the caller's EBX until the first drain. The
runtime record is at EBP−30h (descriptor `0x49f7c9`, context word EBP−20h:
14h, 8, 2Ch, 20h, 44h, 38h, 5Ch, 50h around the constructions). Locals (f =
EBP): f−4 the graphics-library object, f−8 the camera slot (`[ESI]`), f−0Ch the
main camera, f−10h the cockpit, f−34h .. f−40h the border sizes, f−42h/f−44h
the launch peek's words, f−48h the palette, f−4Ch the sound byte, f−50h ..
f−60h never written, f−68h/f−6Ch the terrain-object loop, f−70h/f−74h the MECH
walk, f−78h the terrain object, f−7Ah/f−80h the camera loop, f−84h .. f−94h and
f−0C4h .. f−0D4h dead stores (the new objects' `+26h`, `+2Ch`, `+38h`, `+3Eh`,
`+44h`), f−98h/f−9Ch the cursor, f−0A0h/f−0A4h a dead computation, f−0B0h ..
f−0C0h the main camera's arguments with the zero words f−0B8h .. f−0B4h,
f−0D8h the display walk, f−0DAh/f−0E0h the building search, f−0E8h .. f−0F4h
and f−150h .. f−144h the text rectangle, f−0F8h the key device, f−108h/f−118h
the window and client rectangles, f−128h the loading-screen name, f−134h ..
f−12Ch the position, f−140h .. f−138h the vector (0, −6000, 0). In order
(the sites are the original's CALLs):

1. `0x49630c` (EAX = the descriptor, the entry EDX/ECX); `0x479cfc(0x461304)`
   (`pop ecx`); the input set-up `0x45a5dc`; `0x4955c4` when byte `0x4d267d`
   (`-m`) is set.
2. `GetWindowRect(hwnd [0x4d3610], f−108h)` and `GetClientRect(hwnd,
   f−118h)` through the stdcall thunks `0x49694c`/`0x496970` (no stack
   clean-up at the caller); f−34h = right − left, f−38h = bottom − top of the
   window, f−3Ch/f−40h = those minus the client's right/bottom. Nothing reads
   the four dwords again.
3. The graphics context `0x461294`, the pump `0x464c78`, the frame clear
   `0x46548c`, the launch peek `0x4614c0(f−44h, f−42h)` (the world word; the
   second word, section 1's vehicle-8 flag).
4. The loading screen: the dword at `0x49f89a` ("ld_") to f−128h,
   `_strcat(f−128h, flag ? "flyrX" (0x49f89e) : "hercX" (0x49f8a4))`, byte
   f−121h = '1' + the world byte, `0x492bec(f−128h, [0x4a09fc])`, the palette
   `0x465f7c` (`pop ecx`) to f−48h, `0x481fe0([f−48h])`, the pump,
   `0x461450(f−128h)`.
5. `new(6)` (`pop ecx`) to f−4; when not null `0x480af0(obj, 26D3h)` and the
   construction counter `[[0x6cbee4]]` − 1; G+4Eh = f−4.
6. The sound init `0x462410` with six slots: slot 0 the sound byte G+0Ch
   (zero-extended, stored at f−4Ch), slots 1..5 the words f−50h, f−54h,
   f−58h, f−5Ch, f−60h (never written by the root: stack residue) under the
   high words EAX, ECX, EDX, EAX, ECX carry. `0x462410` reads only slot 0
   (section 1's reading of its body).
7. `0x463370` (bare RET), `0x48dd08`, `0x4913dc`, `stage_dispatch(2)`, the
   lights `0x46134c`, the limiter reference `0x467840`, `world_load`
   `0x424308`.
8. G+0BBh = 19h if byte G+0B5h else 0Dh (`mov ecx,19h; add ecx,-0Ch`).
9. The terrain objects: f−68h = word `0x4aaa7c`, f−6Ch = `[0x4aaa78]`; loop:
   CX = the word, the word − 1 (before the test), CX = 0 or a null entry ends
   it; otherwise `0x4282d8(entry)` (`pop ecx`; the next `mov cx` keeps that
   high word). A negative count runs until a null entry.
10. The mouse queue `0x452ae4`, byte `0x4d1dd6` ← 0, `actor_load` `0x4253d8`,
    `0x44f6f4`, the pump.
11. The drains (section 5): while `pool_prev([0x4a961e], 0)` (EBX = the
    object) is not 0: `removal_append([0x4a9622], obj)` (EAX = obj),
    `0x407b3c`, `0x4077e8`, `0x409904`; then the same with
    `[0x4a9642]`/`[0x4a9646]` and `0x4083e0`. The globals are reread every
    time.
12. Word `0x4cf880` ← word `0x49abba`, G+2Ah ← 0; the MECH walk over
    `[0x4a9b7a]` (f−70h, copied to f−74h before each call): the position
    `A+26h..A+31h` to f−134h, `terrain_height([0x4a0be0], f−134h)` to
    f−12Ch, + signed word `[[A+1F2h]+16h]`, + 5000 when word `[[A+1F2h]+50h]`
    ≠ 0; A+2Eh, A+26h, A+2Ah ← f−12Ch, f−134h, f−130h (in that order); G+2Ah
    ← A when byte `A+0A3h` (`[A+92h+11h]`) is set.
13. `0x4d2684` ← G+2Ah; `[[0x4a0be0]+125h]` ← `0x428c60` (through f−78h).
14. The group cameras: count = word `[[G+2Ah]+45h]+10h` (f−7Ah, signed
    against the dword counter f−80h); per member `new(4Eh)` to `[ESI]`
    (f−8); when not null (context 2Ch) `0x48bd84(obj)`, vtable ← `0x49f8e8`,
    the dead stores f−84h .. f−94h, dword `+32h` ← 0, dword `+4Ah` ← 0, words
    `+36h`, `+48h`, `+46h`, `+44h` ← 0, the counter + 1 − 3 (context 20h);
    `0x4d2688[k]` ← `[ESI]` (EBX walks the array).
15. G+60h ← G+2Ah; pump, frame clear, pump. Below 640 (signed dword G+82h):
    `0x492bec("lo_curs" 0x49f8aa, [0x4a0a10])`, `0x45cdf4`, `0x465618` (each
    result through f−98h/f−9Ch).
16. f−0A4h ← 0 and f−0A0h ← (byte G+7Bh = 1: byte G+0Bh = 0 ? 1 : 0; else
    word G+0AAh ≠ 0: 0; else byte G+0Bh = 0 ? 2 : 0), never read; the CMP
    at `0x461b03` has no consumer. The HUD set-up `0x4544c0`; `[0x49dd94]` ←
    `0x461250`, `[0x49dd98]` ← `0x461274`.
17. The main camera: f−0B0h = 9 if signed dword `0x4d3040` > 1200 else 8;
    `new(4Eh)` to f−0Ch; when not null (context 44h) the zero words f−0B8h,
    f−0B6h, f−0B4h, the vector f−140h = (0, −1770h, 0), f−0BCh = f−0B0h,
    f−0C0h = G+12h, `0x48bda4(cam, [f−0C0h], [f−0BCh], 100h, f−140h,
    f−0B8h)` (EAX/ECX/EDX = slots 0/1/2), vtable ← `0x49f8e8`, the dead
    stores f−0C4h .. f−0D4h, the same field clears as step 14 and the counter
    + 1 − 3 (context 38h); G+2Eh ← f−0Ch (also when null).
18. `0x45edb0([0x4d2684])`; then `[G+2Eh]` is reread: word `+36h` ← 2 when
    dword `+4Ah` is 0 (a null camera faults here in the original); word
    G+6Eh ← 1.
19. The cockpit: `new(37h)` to f−10h; when not null (context 5Ch)
    `0x429660(D, G+2Ah)` (context 50h); G+4 ← f−10h. `0x431530([0x49b088])`.
20. The display walk: `pool_prev([0x4a9b7a], [f−0D8h])` with ESI = the
    previous object; per MECH `A+1FEh` ← `0x437c8c(A)`.
21. `cockpit_sync(G+2Ah)`, `camera_update(G+2Eh)`, `0x42a1f0(G+4, 1)` unless
    byte G+7Ch is 2, then `0x42a1f0(G+4, 0)`; AX ← word `0x4a9e54` (stored at
    f−0DAh, never read).
22. The building search over `[0x4a95a0]` (f−0E0h, in ESI at the call): for a
    building whose group category byte `[[B+45h]+12h]` is 0, `dist_gate(B+26h,
    G+2Ah+26h)` with EBX = B+26h, ESI = player+26h; a distance below 40000
    (signed) ends the search. Nothing uses the result.
23. With a sound context `[0x49f8ec]` and word `0x4aa728` = 0: `[0x49f8fc]` =
    (dword G+0B7h `cdq; idiv 5` remainder) + 2 (EAX the quotient, ECX 5),
    byte `0x49f900` ← 1, `0x46313c`. (`0x461dca..0x461dd1` is dead.)
24. Word G+54h = 0: word `0x49aef6` ← 8, `0x42eb40(G+2Eh)`, `0x43048c`,
    `render_root`; else `0x45d548`. Word G+6Eh ← 0, pump, `0x43048c`, pump.
25. Word G+0AAh ≠ 0: `0x464a14`; else the rectangle: f−0ECh = 140h, f−0E8h =
    190h (EDX), f−0F4h = 0, f−0F0h = 0 (EAX), f−150h/f−148h and f−14Ch/f−144h
    = min/max of the two pairs (both comparisons of constants fall through;
    the other edges are PROVED unreachable), `0x43122c(1, f−150h, 0)`.
26. `0x4328cc`, byte `0x49b105` ← 1, `0x433f7c`, `call [0x4a5824]`.
27. G+18h and G+16h ← byte `0x4d1f41` (EDX zero-extended), `0x45d490(G+16h
    in AX)`; G+0B3h ← byte `0x4d1f42`, G+0B1h ← G+0B3h, `0x45d50c(G+0B1h in
    DX)`; G+3Ah ← byte `0x4d1f40`.
28. Word G+54h ≠ 0: `0x45d85c`. Byte `0x4d1dd6` ← 1 unless word G+1Ah (tape
    playback). `0x45b758`, pump, `0x477abc([0x6c5eb8])` (through f−0F8h), word
    G+76h ← 0.
29. ECX ← f−30h (the record's saved head), `mov fs:[0],ecx`; EDX:EAX are
    `0x477abc`'s.

Every one-slot call is followed by `pop ecx` (the core rereads the slot at the
original coordinate); the image constants (`0x461304`, `0x49f8e8`,
`0x428c60`, `0x461250`, `0x461274`, the strings, the descriptor) are relocated
by the core. Faults the original takes are not modelled: a null player at
step 14, a null main camera at step 18 (unless `0x45edb0` stores one).

**Registers at the callees.** The ECX after `removal_append` (passed into the
first flush) and after `dist_gate` (passed through the building walk's
`pool_prev` into `0x42eb40` or `0x45d548`) is dead: `0x407b3c`/`0x4083e0` pass
it to `removal_flush`, which overwrites it after `0x496406` or returns it
unread, and `0x42eb40`/`0x45d548` write ECX before reading it (`0x45d548` calls
`0x49630c` and `new` first; `pop ecx`). So the framework's ECX of those two
replaced hooks changes nothing (the real-callee pass masks exactly these two
entry ECX values).

### Stack: the entry shim

The guest callees run below the root's frame and some read stack they did not
write: the sound init receives five words of residue in its slots (unread by
`0x462410` itself), and the loaders, the HUD and cockpit construction, the
render root and the start sequences were not audited (the render root and the
logic frame needed shims for this reason, main_loop.md). The root
therefore runs at the original stack coordinate: the shim keeps EBP = T−4 (T =
entry ESP), stores the caller's EBX/ESI/EDI where the prologue pushes them,
reserves a guard-probed gap of 112 pages (448 KiB, the logic frame's size) and
leaves {EBP, EBP−15Ch, ECX} for the adapter; the core's frame is the original's
frame, and every game call goes through `ai_behaviour_stack_call` with the
EBX/ESI/EDI and return address the core names (the return address is left in
the slot below the arguments, as a CALL does). So each callee sees the
original's EBP, registers, argument slots and the bytes the earlier calls
left below the frame. Under the root the shims of `render_root` (116 pages),
`cockpit_sync` and `panel_frame` nest; the shared shim floor puts their host
frames below the root's. The in-vitro depth probe measures the shimmed root at
459,856 (fast) / 459,972 (preserving dispatch) bytes below the caller: the
gap plus 1.1 KiB of context and host frames. Peak under the load: the larger of
this and the render root's chain, well inside the 1 MiB main-thread stack.

### Compare

Refused (`compare_unsupported`): the root is the whole load (allocation, file
reads, both script passes, the stage-2 callbacks, the draws). The acceptance
is the after-load snapshot (below) and the regression.

### In vitro (group `mission_root`)

`invitro/mission_root_cases.h` (README "mission_root"): 62 stubbed
callees, the graphics slot stub, a 28 KiB arena with real pool links. Quick
tier: 100,000 direct cases, each against (a) the core with its frame at the
original's EBP (frame bytes, ECX and every register compared raw) and (b) one
case in four (zero stack pattern) the adapter's plain path with the callee
counter deltas; 1,024 frame cases through the entry shim (fast and preserving
dispatch, 31/a7 stack, real `0x49630c`, per call EBX/ESI/EDI, EBP/ESP, the 256
residue bytes, the SEH record; 14,214 changed argument slots in both runs; the
1,024 bytes below the caller); the depth probe; 1,024 real-callee cases (the
real `0x49630c`, `pool_prev`/`pool_next`, `removal_append`, `dist_gate`, all 32
mode combinations). 35 branch sites, 33 both ways, the two constant
comparisons `0x461e57`/`0x461e8d` PROVED. One caught mutation
(`MUTATIONS.md`). Logged addresses inside the root's frame are compared
relative to its EBP; the adapter's buffer frame is 4 bytes below the
original's, so the generated data words the root forwards (the objects' +2Eh,
view +12h and +0B7h, the rectangle words of `0x49694c`/`0x496970`) stay out
of the host worker stack, and every original direct run checks its logged
arguments and registers against that stack's reservation. Core unit test `//core:mission_root_test` (two worked
scenarios, every call in order).

### Live counts (expected)

`hk_mission_root_calls` = 1 in every suite; skipped/bypassed 0. Callee
counters (`hk_mission_root_call_<va>`, `_slot_4a5824`):

- exactly 1 in every suite: `49630c`, `479cfc`, `45a5dc`, `49694c`,
  `496970`, `461294`, `4614c0`, `4967ea`, `465f7c`, `481fe0`, `461450`,
  `462410`, `463370`, `48dd08`, `4913dc`, `401d94`, `46134c`, `467840`,
  `424308`, `452ae4`, `4253d8`, `44f6f4`, `4544c0`, `48bda4`, `45edb0`,
  `429660`, `431530`, `41b130`, `4011a0`, `4328cc`, `433f7c`, `slot_4a5824`,
  `45d490`, `45d50c`, `45b758`, `477abc`; `464c78` 8, `46548c` 2; `480af0` 1
  (new(6) not null); `4955c4` 0 (no suite sets `-m`);
- `46313c` 1, except `train1-timers` 0 (the map: 0 there, 1 in the other 39);
- `437c8c` = `46e180` = the MECH pool's objects, the map's counts of
  `0x437c8c` (root-only): 10 in the 19 C1_01 suites except
  `c1_01-unarmed-keys` 9; `c1_02-probe` 13; `c1_06-cheat-disable` 16; the
  C1_08 suites 8; `c2_02-identify` 11; the C3_09 and C3_10 suites 16;
  `c4_05-convoy` 13; the C4_08 suites 11; `c4_10-probe` 9; the C5_05 suites
  15; `train1-timers` 1;
- `471c70` ≥ 2 × (MECH + 1) + 2, `47749c` = 3 + the cameras of step 14 (=
  `48bd84`), `42a1f0` 1 or 2, the cursor calls (`492bec` 2, `45cdf4`,
  `465618`) only below 640 pixels, the first render (`42eb40`, `45fca8` 1,
  `43048c` 2) or the intro (`45d548` 1, `43048c` 1), `464a14` or `43122c`,
  `45d85c` with the intro: first live values;
- **new information:** `478628`, `407b3c`, `4077e8`, `409904` (= explosions)
  and `4083e0` (debris) count the objects the load created before the
  drains (section 12, open in sections 1 to 12), `4282d8` the terrain objects binned
  at load, `49288c` the buildings the dead search measured.

The counters of the 15 nested hooks keep their values (the bridge still
reaches them; `render_root`'s count still includes the load-time call).

### After-load snapshot reach

The root's simulation writes lie in the fixed regions of section 13: the view
block and the camera array `0x4d2684..0x4d26a7` in the globals object
`0x4d24bc..0x4d27b8`, `0x4cf880` in `0x4cf878..0x4cf88c`, the MECH positions,
`A+1FEh` and the cameras, cockpit and graphics objects in the heap (reached
from the pools and the view block). Not in a fixed region and written by the
root itself: the modal callbacks `0x49dd94..0x49dd9b`, the CD track and loop
byte `0x49f8fc..0x49f900`, word `0x49aef6`, byte `0x49b105` and the input byte
`0x4d1dd6`: extra `0x49dd94:8,0x49f8fc:5,0x49aef6:2,0x49b105:1,0x4d1dd6:1`
covers them (five regions; with the eight of section 29, 13 of 16).

### Live check (pending)

1. **Regression, all suites**, every registered hook replaced (the hooks.md
   selection line ends with `mission_root=replaced`), early arming as by
   default (`tools\check.ps1 -Live -OwnShell`): every suite identical;
   `hk_mission_root_calls` 1 and the counters above, every
   `*_skipped`/`*_bypassed` 0 (the root adds one dispatch level and the
   112-page gap to the load). The proxy log shows `early_arming_armed` once
   per game process.
2. **After-load snapshot** as in section 14 (imports original, the same
   environment, O1 and O2 every hook original, R every hook replaced) on
   `acceptance-60s`, `c3_10-loadout-hercs` and `c3_09-loadout-b`, with
   `ES2_AFTER_LOAD_SNAPSHOT=1` and `ES2_AFTER_LOAD_SNAPSHOT_EXTRA` = the
   current extras plus
   `0x49dd94:8,0x49f8fc:5,0x49aef6:2,0x49b105:1,0x4d1dd6:1`, compared with
   `after_load_snapshot.py compare O2.snap R.snap --noise noise.json` for
   `--scope all` and `--scope pools`: `IDENTICAL` expected (apart from the
   noise classes of section 14). This is the acceptance that the replaced
   root builds the same state: R runs the root through its shim, O1/O2 the
   original.
3. Optional, to show the switch: one suite with `-Hooks
   "mission_root=replaced"` and `ES2_EARLY_ARMING=0`: identical,
   `hk_mission_root_calls` 0.

### Open

- The live check above.

### Corrections to section 1

`0x461d0c` calls `camera_update` `0x4011a0` (named in section 1, address
added); the
window rectangles are not only "window border sizes into locals": four
differences are stored at f−34h .. f−40h and never read; `0x4614c0` writes
the world word to its first argument f−44h, and a nonzero word at f−42h (its
second argument) selects "flyrX"; the sound init's slots 1..5 carry the high words of EAX/ECX/EDX;
`0x463370` is called (a bare RET); `0x467840` reads the clock once.

## 31. Effect pools with resources

The rest of L8: the eight load-time functions that load shapes and bitmaps
next to their pools, read from the pinned `llvm-objdump -d -M intel`
listing of the reference DBSIM (the exception descriptors, the name
template `0x497ba0`, the base name `0x4a0a74` and the strings
`0x497d24..0x4a0b70` read from the file). The verdict rows of
function_verdicts.md and the row of hook_frontier.md were the starting
hypotheses and hold, except the read count of `0x4083f8` (below). Live
counts come from the map `dbsim-map-20261012` and the contracts. Own code:
adapter group `effect_pools`, core module `core/src/effect_pools.c` (the
pool/list steps of section 15 appear six times with two register
variants; they share nothing else with `mission_load.c` or
`building_types.c`). Every function of the frontier row is a hook; the
element constructor `0x46bbc1`, reached only through `0x49667c`, stays
original (verdict row in function_verdicts.md). **Validated in vitro; the live
check is pending** (below). Evidence (scripts and logs, no listings):
`build\claude\h4-117\`.

| Hook | VA | Role | Caller |
|---|---|---|---|
| `explosion_pools` | `0x407b54` | stage-2 record 5: the explosion pool, list, bitmaps, shapes and records | `0x401d94(2)` through `call [ebx]` at `0x401dac` (registered at `0x408296`) |
| `debris_pools` | `0x408860` | stage-2 record 6: the debris pool, list, the default debris set, three constants | the same (registered at `0x408eae`) |
| `meteor_pools` | `0x409a34` | stage-2 record 9: the meteor pool, list, shapes and bitmap | the same (registered at `0x409f8a`) |
| `fire_pools` | `0x46b1a8` | stage-2 record 55: the fire pool, list, shapes, per-shape bytes and bitmaps | the same (registered at `0x46b872`) |
| `smoke_balls` | `0x408fb8` | the smoke-ball pool, list, shapes and bitmap | `smoke_pools` `0x4090cc` at `0x409164` |
| `flat_pools` | `0x4097a8` | the flat shapes, their bitmap pair, the flat-object pool and list | the world loader `0x42e010` at `0x42e6dc` (a guest of `world_load`) |
| `debris_set` | `0x40874c` | one debris set: shapes, records, the shape tag | `base_types` at `0x4067e1`, `debris_pools` at `0x408907`, `mech_type_load` at `0x420483` |
| `debris_record` | `0x4083f8` | one debris record and its pieces from a stream | `debris_set` at `0x40880e` |

### Call path and live visibility

Each stage-2 callback is referenced once in the file (the push before
`0x401d64(fn, 2)` at the address above), so the dispatcher of stage 2 is
its only caller; `0x408fb8`, `0x4097a8` and `0x4083f8` have one call site
each, `0x40874c` three. All run inside the mission load after the early
arming of section 13, so with arming on (the default) every hook is entered
in every suite. The map (every hook original, early arming) counts
`0x407b54`, `0x408860`, `0x409a34`, `0x46b1a8`, `0x408fb8` and `0x4097a8`
at 1 in each of the 40 suites; `0x40874c` and `0x4083f8` per suite in the
table of the expected counts below.

### Contracts (from the bytes)

All eight are cdecl. The seven with a `push ebp` frame and a descriptor set
up the runtime record through `0x49630c` (EAX = the descriptor, EDX and ECX
as at entry, which `0x49630c` keeps); the descriptors' second dword gives the
record offset (`0x497be0` and `0x4a0aa9` −58h, `0x497da0` −50h, the others
−28h), the context word lies at record+10h. The core gets the synthetic EBP
f in a zeroed scratch (`..._FRAME` of `effect_pools.h`) and returns the
record's saved head, which the adapter writes to FS:0 as the epilogue does:
**in EAX** for `explosion_pools`, `meteor_pools`, `fire_pools`,
`smoke_balls` and `flat_pools`; **in ECX** for `debris_pools` (EAX = 0)
and `debris_set` (EAX:EDX = `stream_destroy`'s), whose epilogues load
it into ECX (`0x408931`, `0x40884f`).

Two common steps (the pool and list of section 15), with the slots f−4
and f−8 and the context words 14h/8 and 20h/8:

- **pool(capacity, size)**: `new(12h)`, ECX = 12h (POP), [f−4] = EAX; when
  nonzero: `pool_create([f−4], capacity, size)` with the header loaded into
  EDX (into ECX in `flat_pools`); then the header reloaded into ECX (into
  EAX in `flat_pools`) and stored to the global.
- **list(capacity)**: `new(8)`, ECX = 8, [f−8] = EAX; when nonzero: the
  capacity through AX (CX in `flat_pools`, the high word kept) into word
  +6, `new[](4 × capacity)` with the size in EDX (EAX in `flat_pools`),
  ECX = the size (POP), the slot reloaded into ECX (EDX in `flat_pools`)
  for dword [list] = the block, word +4 = 0; then EDX = [f−8] into the
  global.

Resources: `0x492bec(name, directory)` builds a path (directories
`[0x4a09f8]` shapes, `[0x4a09f0]` data, `[0x4a0a10]` bitmaps; extension
`dts` for `debris_set`'s shapes), `0x474cd8(list slot, path, 140008h, 0)`
loads shapes and returns their count in AX, `0x45cdf4(path)` a bitmap
(POP ECX after it). Every name and directory is passed as the original
pushes it; the cores never interpret a resource.

**`explosion_pools` 0x407b54** (record f−58h, stream f−38h, count word
f−5Ah, name buffer f−6Ch): the 16-byte template `0x497ba0` ("explo666")
copied into f−6Ch with REP MOVSD (four dword reads and writes in order,
ECX = 0 after); pool(40, 5Bh) → `0x4a961e`; list(30) → `0x4a9622`;
`0x49667c(0, 8, 15, 1, 0x46bbc1)` → `0x4a9638` (15 bitmap pairs). With the
low-memory word `[0x4d2680]` (option `-l`, launcher.md) nonzero: `_strcat`
(`0x4967ea`) of `0x497d24` ("s") onto the name, then 13 bitmaps, else 15;
bitmap k: `_itoa(k, f−67h, 10)` (`0x496820`), `0x492bec(f−6Ch,
[0x4a0a10])`, `0x45cdf4`, stored at `[0x4a9638] + 8k` (the pointer reread
per bitmap). Then the word is reread: `explos2` (`0x497d26`) when set, else
`explos` (`0x497d2e`); shapes into `0x4a9630` (count in SI); `new[](4 ×
count)` → `0x4a963c` (the map); the data file (`0x492bec(name,
[0x4a09f0])`, context 2Ch, `0x472f14(f−38h, path)`; a nonzero status
reports `0x479c78(6C56C8h, 5, 0, "EXPLO.CPP", 56h, path)`); the read method
(slot +18h) reads the map count word into f−5Ah, `stream_read(S, 4 × count,
[0x4a963c])`, `stream_read(S, 2, 0x4a962c)` (the record count), `new[](40 ×
count)` → `0x4a9628`, `stream_read(S, 40 × count, block)`,
`stream_destroy(S, 2)`. Then word `0x4a9634` = 0 and, per record, the
16-bit (word +0) + 1 if not below the stored word (signed), the 32-bit INC
of EAX kept (ECX = EAX or the stored word); last, for every shape i <
sx(SI): [shape i + 26h] = `[0x4a9638]` + 8 × sx(map word 2i+1). Returns
EAX = the head, EDX = sx(SI), ECX as the last loop left it.

**`debris_pools` 0x408860** (record f−28h): pool(150, 61h) → `0x4a9642`;
list(40) → `0x4a9646`; `debris_set(0x4a9650, "def_deb", 0)`; EAX = 0,
dword `0x4a964c` = 0, words `0x4a9664`/`0x4a9666`/`0x4a9668` = 6000,
16000, 420; ECX = the head. EDX = `debris_set`'s.

**`debris_set` 0x40874c** (set, name, tag; record f−50h, stream f−30h):
`0x492bec(name, "dts")`, `0x474cd8(set+6, path, 140008h, 0)` with EAX =
set+6, word set+0Ah = AX; `0x492bec(name, [0x4a09f0])`, context 8,
`0x472f14(f−30h, path)` (status: `0x479c78(…, "debris.cpp", A1h, path)`
with EDX = 0); the read method reads word set+4 (the record count);
`new[](10 × count)` → [set]; `debris_record(record k, S)` for k <
sx(word set+4) (reread per record, EAX = S); when the tag (the argument
slot, reread) is nonzero, [shape i + 26h] = tag for i < sx(word set+0Ah)
(EAX = the count word at the loop's end); `stream_destroy(S, 2)` with
EDX = S. Returns EDX:EAX of `stream_destroy`, ECX = the head.

**`debris_record` 0x4083f8** (record R, stream S; no runtime record): the
read method (table reloaded from [S] for every read) reads word R+0 and
word R+2 (the piece count n); `new[](14 × sx(n))` (SHL 3, SUB, ADD) → R+4;
word R+8 = 0; then while the 16-bit n (reread) is greater than the counter:
seven word reads into the piece (+0..+0Ch, EAX = the word's address), the
words +8 and +0Ah multiplied by 182 (LEA/SHL/SUB/SHL/SUB/ADD of the
sign-extended word, 16-bit store) unless FFFFh, word R+8 += word +2,
the maximum of the signed words +0 (starting at 0), the next piece 0Eh
bytes on. The two words live in the slot of the entry PUSH ECX (f−2 the
maximum, f−4 the counter), so the final POP ECX returns **ECX = maximum <<
16 | counter**; AX = the maximum over EAX's high word. the verdict row of function_verdicts.md ("nine
stream reads") is 2 + 7 per piece.

**`flat_pools` 0x4097a8** (flag; record f−28h; EDI = the pair `0x4a969e`):
the byte `0x498098` incremented when 0 (a once-guard nothing else reads);
the name `flat2` (`0x498109`) when the flag word is nonzero, else `flat`
(`0x49810f`); shapes into `0x4a9698`, count word `0x4a969c` = AX; the bitmap
of the same name (`0x492bec(name, [0x4a0a10])`, `0x45cdf4`) into
`[0x4a969e]`, its descriptor `0x46a03c(bitmap)` into `[0x4a96a2]`, the
construction count `[[0x6cbee4]]` + 1, `0x48349c([0x4a969e], 3)` (the
slot reread) and the slot cleared; [shape i + 26h] = `0x4a969e` while
the 16-bit i < word `0x4a969c` (reread); then pool(150, 41h) → `0x4a968d`
and list(25) → `0x4a9691` in the ECX/EAX/CX/EDX variant.

**`meteor_pools` 0x409a34** and **`smoke_balls` 0x408fb8** (record f−28h;
the same code): pool → `0x4a96aa` (10, 59h) / `0x4a966e` (200, 4Ch);
list(25) → +4; shapes `meteor` / `smoke` (`[0x4a09f8]`) into `0x4a96b4` /
`0x4a9678`, count in BX; the bitmap `impact` / `smoke` into `0x4a96b8` /
`0x4a967c`; then, when sx(BX) > 0, [shape i + 26h] = the bitmap slot's
address for the 16-bit i while BX > i.

**`fire_pools` 0x46b1a8** (record f−58h, stream f−38h, base name f−60h,
name buffer f−74h): pool(10, 5Bh) → `0x6b4f30`; list(10) → `0x6b4f34`;
the five bytes `0x4a0a74` ("fire" and its terminator) to f−60h; the inline
string copy (REPNE SCASB with ECX = −1 until the terminator, then REP MOVSD
and REP MOVSB of the length + 1 to f−74h; EAX = f−74h, EDX = length + 1,
ECX = 0 after); with `[0x4d2680]` nonzero `_strcat(name, "2")`; shapes
(`[0x4a09f8]`) into `0x6b4f28` (count in SI), context 2Ch; the data file
of the base name (`[0x4a09f0]`; status: `0x479c78(…, "fire.cpp", 3Bh,
f−60h)` with ECX = 0); the read method reads a dword into f−64h (never
read again) and sx(SI) bytes into `new[](sx(SI))` (the block is not
stored: leaked); `stream_destroy`; `0x49667c(0, 8, 2, 1, 0x46bbc1)` →
`0x6b4f2c`; two bitmaps k = 0, 1: the copy again, `_itoa(k, f−66h, 10)`,
`_strcat(name, number)`, with the word `_strcat(name, "s")`, the bitmap
(`[0x4a0a10]`) into `[0x6b4f2c] + 8k`; then [shape i + 26h] =
`[0x6b4f2c]` + 8 × (signed byte i) for i < sx(SI); dword `0x6b4f38` = 0.
Returns EAX = the head, EDX = sx(SI), ECX as the last loop left it.

**Callees and modes.** Through the guest bridge with the original's entry
registers: `0x49630c` (with the synthetic EBP), `new` `0x47749c`, `new[]`
`0x4774f0`, `0x49667c` (with the element constructor `0x46bbc1`, empty:
returns its argument; verdict row added), the CW3220 thunks `_strcat`
`0x4967ea` and `_itoa` `0x496820`, the path builder `0x492bec`, the stream
constructor `0x472f14`, the read method (slot +18h of the table at [S]),
the error report `0x479c78`, the resources `0x474cd8`, `0x45cdf4`,
`0x46a03c`, `0x48349c` (resource, guest) and the hooks `pool_create`
`0x471ad8`, `stream_read` `0x4735b8`, `stream_destroy` `0x472f64`; inside
the group `debris_pools` → `debris_set` → `debris_record` through the
bridge, so each keeps its mode.

**Registers, stack and shims.** EDX:EAX is reproduced exactly; ECX by the
cores, and the adapters leave the framework's entry ECX, because nothing
reads it: the dispatcher reads no register after `0x401dac`, and the
callbacks after records 5, 6 and 9 (`0x408860`, `0x4090cc`, `0x40a060`)
load EAX and hand EDX/ECX only through `0x49630c` into `new`, which reads
none (record 55 is the last stage-2 entry); `smoke_pools` returns at once
after `0x409164` (section 15: its ECX is dead); the world loader pops ECX at
`0x42e6e1`; `debris_set` overwrites ECX in its loops or passes it into the
next `debris_record` (whose PUSH ECX slot is overwritten before it is
read) or into `stream_destroy`, whose paths write ECX before reading it
and pass it before that only to `0x49630c`, `0x47307c` and cdecl RTL
calls; after `debris_set`, `debris_pools` reloads ECX (`0x408931`),
`base_types` calls `stream_destroy`, `mech_type_load` returns (its adapter
returns the framework's ECX as well). No local is read before an own store
or a callee that received its address wrote it (the record by `0x49630c`,
the streams by `0x472f14`, f−5Ah and f−64h by the read method, which with
the complete CD files always writes them); `fire_pools`' scan stops at
the terminator of its five copied bytes (the original would scan on into
uninitialized stack at f−5Bh if `0x4a0a74` had no zero in its five bytes;
the core would read the scratch's zeros there: no writer of `0x4a0a74`
exists in the file). No entry or exit shim; no game instruction embedded.

**Compare:** refused for all eight (allocation, file I/O, resource loading,
the runtime record and FS:0); the after-load snapshot is the acceptance.

### In vitro (group `effect_pools`)

`invitro/effect_pools_cases.h`: per hook 100,000 direct cases (quick tier;
1,001,024 in the full tier, not run) against logging stubs for all 18
direct callees and the read method of two arena tables (counts 0..5 or
small negatives including FFFFh for the 2- and 4-byte reads, bounded random
data otherwise, count changes after `49667c`/`new[]`, a lowered record count
after `debris_record`, a changed bitmap slot after `46a03c` and shape count
after `48349c` (`flat_pools` rereads both), table switches after reads, open
failures one time in four, `new` failing one time in four, the low-memory
word 0 or not, the fire base name with its terminator at a random one of
its five bytes, the flag word 0 or not, the tag 0 or not). Compared: pair,
ECX (original against the core), FS:0, the ordered calls with argument
slots, entry registers and the record's context word, frame addresses
relative to the record, the arena, the image globals (`0x4a09f0`,
`0x4a09f8`, `0x4a0a10`, `0x4d2680`, `0x4a0a74..0x4a0a78`, `0x498098`,
`0x4a961e..0x4a96bb`, `0x6b4f28..0x6b4f3b`, `0x6cbee4`) and the callee
counter deltas; the adapter separately (pair, calls, memory). 256 installed
31/a7 cases per hook (fast and preserving dispatch). Real-callee pass per
hook (2,048 cases): real `0x49630c` and `0x49667c` over a bump allocator;
`pool_create` nested under the six pool hooks (entered 2,048 times each),
`debris_set` and `debris_record` under `debris_pools` (2,048 / 3,624),
`debris_record` under `debris_set` (3,552), each in original/replaced
modes, the hook under test original or replaced. 40 branch sites, all
both ways. One caught mutation per hook (`MUTATIONS.md`). Core unit test
`//core:effect_pools_test` (worked out by hand: a record with two pieces,
the empty and negative cases, a set with a tag and a failed open, both
debris-pool paths, the explosion load in both word states and with a
failed open, the fire load, both flat flags, meteors and smoke balls with
and without shapes).

### Live counts (expected)

Once per suite in all 40 suites (`[0x4d2680]` = 0 expected: no suite sets
`-l` and the machines have more than 12 MB; with it, explosion's `496820`
and `45cdf4` read 13, `492bec` 15, `4967ea` 1, and fire's `4967ea` 5):

| Counter | every suite |
|---|---:|
| `hk_explosion_pools_calls`, `_call_49630c`, `_call_471ad8`, `_call_49667c`, `_call_474cd8`, `_call_472f14`, `_slot_18`, `_call_472f64` | 1 each |
| `hk_explosion_pools_call_47749c` | 2 |
| `hk_explosion_pools_call_4774f0`, `_call_4735b8` | 3 each |
| `hk_explosion_pools_call_496820`, `_call_45cdf4` | 15 each |
| `hk_explosion_pools_call_492bec` | 17 |
| `hk_debris_pools_calls`, `_call_49630c`, `_call_471ad8`, `_call_4774f0`, `_call_40874c` | 1 each |
| `hk_debris_pools_call_47749c` | 2 |
| `hk_meteor_pools_calls`, `hk_smoke_balls_calls` and each one's `_call_49630c`, `_call_471ad8`, `_call_4774f0`, `_call_474cd8`, `_call_45cdf4` | 1 each |
| the same two hooks' `_call_47749c`, `_call_492bec` | 2 each |
| `hk_flat_pools_calls`, `_call_49630c`, `_call_474cd8`, `_call_45cdf4`, `_call_46a03c`, `_call_48349c`, `_call_471ad8`, `_call_4774f0` | 1 each |
| `hk_flat_pools_call_492bec`, `_call_47749c` | 2 each |
| `hk_fire_pools_calls`, `_call_49630c`, `_call_471ad8`, `_call_474cd8`, `_call_472f14`, `_call_472f64`, `_call_49667c` | 1 each |
| `hk_fire_pools_call_47749c`, `_call_4774f0`, `_call_4967ea`, `_call_496820`, `_call_45cdf4`, `_slot_18` | 2 each |
| `hk_fire_pools_call_492bec` | 4 |
| every `_call_479c78` | 0 |

Per suite (the map's entry counts of `0x40874c` = D and `0x4083f8` =
R):

| Suites | D | R |
|---|---:|---:|
| the 19 C1_01 suites, `acceptance-60s`, `c1_02-probe` (21 in all) | 6 | 42 |
| `c1_06-cheat-disable` | 7 | 49 |
| `c2_02-identify` | 7 | 50 |
| `c1_08-escort-route`, `c1_08-scan`, `c4_10-probe` | 5 | 35 |
| the C3_09, C3_10 and C5_05 suites (9) | 10 | 70 |
| `c4_05-convoy` | 9 | 64 |
| the C4_08 suites (3) | 8 | 56 |
| `train1-timers` | 3 | 21 |

`hk_debris_set_calls` = D, its `_call_49630c`, `_call_474cd8`,
`_call_472f14`, `_slot_18`, `_call_4774f0`, `_call_472f64` = D each,
`_call_492bec` = 2D, `_call_4083f8` = R; `hk_debris_record_calls` =
`_call_4774f0` = R, `_slot_18` = 2R + 7 × the pieces (data-dependent, not
predicted). Unchanged (the bridge still reaches them): `hk_pool_create_calls`
12, `hk_smoke_pools_call_408fb8` 1, `hk_base_types_call_40874c` 1,
`hk_mech_type_load_call_40874c` (D − 2), `hk_stream_read_calls`,
`hk_stream_destroy_calls`. Every `*_skipped`/`*_bypassed` 0.

### After-load snapshot reach

The globals written lie in the fixed regions of section 13
(`0x4a961e..0x4a96bb` in `0x4a9572..0x4a9dfc`, `0x6b4f2c..0x6b4f3b`)
except the fire shape list pointer `0x6b4f28` and the guard byte
`0x498098`; every allocated block is in the walked heap (the pools with
their objects, the lists, the bitmap pairs, the explosion map and records,
the debris records and pieces, the fire bytes block, which nothing
references). Extra regions: `0x6b4f28:4,0x498098:1` (two more; with the 13
of section 30, 15 of 16). The shapes and bitmaps are the resource
loaders' and unchanged.

### Live check (pending)

1. **Regression, all suites**, every registered hook replaced (the hooks.md
   selection line ends with `explosion_pools`, `debris_pools`,
   `debris_set`, `debris_record`, `flat_pools`, `meteor_pools`,
   `fire_pools`, `smoke_balls`), early arming as by default: every suite
   identical; the counters above, constant from the first record. The
   eight alone: `-Hooks "explosion_pools=replaced,
   debris_pools=replaced,debris_set=replaced,debris_record=replaced,
   flat_pools=replaced,meteor_pools=replaced,fire_pools=replaced,
   smoke_balls=replaced"` (one string, no spaces).
2. **After-load snapshot** as in section 14 (imports original, the same
   environment, O1 and O2 every hook original, R every hook replaced) on
   `acceptance-60s`, `c3_10-loadout-hercs` and `c3_09-loadout-b`, with
   `ES2_AFTER_LOAD_SNAPSHOT=1` and `ES2_AFTER_LOAD_SNAPSHOT_EXTRA` = the
   current extras plus `0x6b4f28:4,0x498098:1`, compared with `--scope all`
   and `--scope pools`: `IDENTICAL` expected (apart from the noise classes
   of section 14). A difference caused by these hooks would lie under the
   globals above (the six pools with their blocks and node links, the
   lists, the bitmap pairs and their pointers in the shapes' +26h, the
   explosion map, records and count word, the debris sets at `0x4a9650`,
   `0x4a95c0` and in the MECH types with their records and pieces, the
   three debris words, the fire bytes block) or in the block pairing: the
   hooks allocate the same sizes in the same order through the same `new`,
   `new[]` and `0x49667c`.

### Open

- The live check above.
- `hk_debris_record_slot_18` is not predicted (the piece counts of the
  debris files were not read).
- The roles of the explosion map words +0, the record word +0 of the
  explosion records (`0x4a9634` is their maximum + 1) and of the debris
  piece words are not interpreted; the cores copy the original's
  arithmetic.
## 32. The remaining weapon_fit callees

The seven functions that section 16 left as guest callees of `weapon_fit`,
read from the pinned `llvm-objdump -d -M intel` listing of the reference
DBSIM (listing ranges `0x40e61c..0x40e629`, `0x40fea0..0x40feb3`,
`0x420634..0x420652`, `0x413ac8..0x413af5`, `0x417d08..0x417de0`,
`0x40fb2c..0x40fbd8`, `0x417530..0x4175db`); the byte table `0x40fb7a` and
the jump table `0x40fb89` were read from the file, and a scan of the whole
file for each address (as a dword and as the target of every
`call`/`jmp rel32`) found no reference except the call sites in the table
below (no vtable or data reference). The rows of hook_frontier.md and the
verdict rows of function_verdicts.md were the starting hypotheses; they hold,
with the roles made precise below. Own code: adapter group `fit_leaves`,
core module `core/src/fit_leaves.c`. Every callee of `weapon_fit` is a hook
except `0x40cec0`, `0x40d240`, `0x40d128`, `0x40cb84`, `0x40d284`,
`0x40f540` (sections 20 and 22) and the runtime calls. **Validated in
vitro; the live check is pending** (below). Evidence (scripts and logs, no
listings): `build\claude\h4-122\`.

| Hook | VA | Role | Callers |
|---|---|---|---|
| `weapon_shape_word` | `0x40e61c` | the first word of the weapon's mount definition `[W+18h]` | `weapon_fit` at `0x41771d` |
| `mount_entry` | `0x40fea0` | the 6-byte entry of the table `[0x4a98e4]` for a type word | `gun_list_load` (`0x40fee8`) at `0x40ff15`, `mount_table` at `0x42064b` |
| `mount_table` | `0x420634` | the mount table of an actor: `mount_entry` of the actor's type word plus the global word `0x4a9d70` | `weapon_fit` at `0x4175f9`; the H3 display-object code `0x437c8c` at `0x437ce7` |
| `shield_split` | `0x413ac8` | the front/rear share split of the shield record `A+222h` | `weapon_fit` at `0x417878` |
| `energy_rate` | `0x417d08` | the derived actor word `A+256h` (read by `vehicle_update`, vehicle.md) | `weapon_fit` at `0x41787f` |
| `equip_slots` | `0x40fb2c` | files the equipment items of a weapon system into the five slots of a record | `weapon_fit` at `0x417862` (record `A+307h`) |
| `node_state_fill` | `0x417530` | the recursive state fill over a shape-node tree | `weapon_fit` at `0x417744`; itself at `0x4175bb` |

### Contracts (from the bytes)

All seven are cdecl. None builds a runtime record or touches FS:0. Notation
as in sections 15 to 17: `lo(r, w)` replaces the low word of r, `sx` sign
extension, R the record, W a weapon object, D = `[W+18h]` the mount
definition, C = `[W+1Ch]` the weapon definition.

- **`weapon_shape_word` `0x40e61c` (W)**: `mov eax,[ebp+8]; mov eax,[eax+18h];
  mov ax,[eax]`. EAX = lo(D, word [D]); EDX and ECX unchanged. `weapon_fit`
  passes the word (its AX) to the shape virtual `+20h` of `[[A+34h]+4]`.
- **`mount_entry` `0x40fea0` (type)**: EAX = 6 × sx(low word of type) +
  dword `[0x4a98e4]` (`movsx eax,word [ebp+8]; add eax,eax; lea eax,[eax+2*eax];
  add eax,[4a98e4h]`); EDX, ECX unchanged. `[0x4a98e8]` is the running slot
  base of `0x40feb4`, the table pointer is allocated by `0x40fecc`
  (6 × the count word); neither is touched here.
- **`mount_table` `0x420634` (A)**: EAX = A, EDX = `[A+1F2h]` (the actor's
  type record), CX = word [EDX] + word `[0x4a9d70]` (16-bit adds into the entry
  ECX's low word), pushed; `mount_entry(pushed dword)` is called with those
  registers; `POP ECX` restores the pushed dword. Result: EAX = the entry,
  EDX = `[A+1F2h]` (as `mount_entry` leaves it), ECX = the pushed dword.
  `weapon_fit` reads the entry as the mount table M (count word at +0,
  mounts pointer at +2); the display-object code at `0x437ce7` scans M's
  26-byte mounts.
- **`shield_split` `0x413ac8` (S = A+222h)**: ESI = sx word `S+4` (the
  balance, `0..400h`, moved by the shield gauge keys, cockpit.md), EDI = sx
  word `S+6` (the total); `multiply_q10(ESI, EDI)` (`0x47e0b0`) is called with
  the entry registers; its AX is stored at `S+0` (the front share), then word
  `S+2` (the rear share) = word `S+6` minus word `S+0`, both read again after
  the store; EAX keeps `multiply_q10`'s high word, EDX and ECX are
  `multiply_q10`'s.
- **`energy_rate` `0x417d08` (A)**: EDI = 20 (EBX = A, ESI = 20); EAX = EDX =
  A+92h, ECX = the entry ECX. When byte `A+0ABh` (A+92h+19h) is nonzero EDI =
  `multiply_q10(200, 20)`; else when byte `A+0AAh` is nonzero EDI =
  `multiply_q10(600, 20)`. Then W = dword `[A+313h]` (unaligned; this is the
  ENERGY slot of the record `A+307h` that `equip_slots` fills): when nonzero,
  b = sx byte `[[W+18h]+17h]`, the pushed ECX = lo(D, b + 13h) (D's high
  word rides along), `c = sx(AX(part_ratio(A+206h, ECX)))` with EDX = b and
  EAX = A+206h; when c < 225 (E1h, signed) EDI += `multiply_q10(1024 - 204 ×
  trunc(c / 51), 20)` (the original multiplies the quotient by 51 and shifts
  by 2: `0x417da0..0x417dab`; EAX = the value, EDX = 20, ECX = the value at the
  call). The low word of EDI is stored at `A+256h`. The pair is the last
  call's: EAX = 0 without a weapon; for c ≥ 225 EAX = `part_ratio`'s, EDX =
  c; ECX likewise `part_ratio`'s (or `multiply_q10`'s). The condition term is
  the one that the speed command of `vehicle_control` `0x416a04` uses ([ai.md](ai.md#416a04-vehicle_control-speed-turn-and-the-walkstand-clips),
  speed command, item 4: k = 1024 − 204 (r / 51) for r < 225). The roles of
  the flags `A+0AAh`/`A+0ABh` are not interpreted; `vehicle_update` reads the
  word `A+256h` as the argument of `movement_step` (vehicle.md,
  "vehicle_update").
- **`equip_slots` `0x40fb2c` (S, R)**: `[ebp-8]` = dword `[S+4]` (the cursor
  over the item pointers), `[ebp-4]` = word `[S+8]` (the count), ECX =
  lo(entry ECX, count); `memset(R, 0, 20)` (`0x49681a`) is called with EAX =
  S+4, EDX = the cursor, ECX = that. Loop (`0x40fbb7`): AX = the counter
  word, the word decremented, and while the OLD counter is nonzero EDX = the
  cursor (then +4), else EDX = 0; a zero EDX ends the loop (so a null item
  array ends it at once). Body: EAX = `[EDX]` (the item W; null skips), EDX =
  `[W+1Ch]`, ECX = sx word `[C+56h]` − 12h; above 0Eh (unsigned) skips; the
  byte table `0x40fb7a` (15 entries: 5, ten zeros, 4, 3, 2, 1) gives the
  jump-table index (`0x40fb89`: 0 skip, 1 → `R+0Ch`, 2 → `R+10h`, 3 →
  `R+8`, 4 → `R+4`, 5 → `R+0`), so the weapon ID 18 (ECM) is stored at
  `R+0`, 29 (TARG) at `+4`, 30 (SHIELD) at `+8`, 32 (ENERGY) at `+0Ch`, 31
  (TURBO) at `+10h` (IDs and names from weapon_classes.md, "the five
  equipment classes"); every other ID, including the weapons proper, is not
  stored, and the last item of an ID wins. EDX = 0 and EAX = lo(EAX, the
  last old counter) at the end; the epilogue's two `POP ECX` leave ECX =
  the dword `[ebp-4]` whose high word was never written (stack residue; the
  core reports the low word only).
- **`node_state_fill` `0x417530` (N, v)**: cdecl, the dword v is kept whole
  (`push ebx` for every child), only its low word is stored. EAX = `[N]`,
  `call [eax]` (slot 0 of the node: the class id c, one stack argument N),
  `POP ECX`. Signed compare of c with 140014h:
  - c > 140014h: c = 140015h → the array fill below; every other value →
    return with EAX = c − 1E0005h, except c = 1E0003h, which leaves EAX = 0
    (c = 1E0005h gives 0 as well).
  - c = 140014h: word `[N+4]` ← v (low word); return.
  - below: c = 140005h/140006h → return (EAX = c − 140007h); c =
    140007h/140008h → children; c = 14000Bh → word `[N+4]` ← v, children;
    c = 14000Ch → children; every other value returns (EAX = c − 14000Ch).
  - **Array fill** (c = 140015h): while sx(i) < word `[N+16h]` (re-read every
    test): word `[[N+1Ch] + 2i]` ← v (low word); then the children.
  - **Children**: ESI = dword `[N+12h]` read once; for i = 0 while sx(i) <
    word `[N+10h]` (re-read every test): `node_state_fill([ESI], v)` with
    EAX = the child, EDX = sx(i), ECX = N at the call, then ESI += 4. The
    visiting order is pre-order, children left to right; the core recurses on
    the host stack in exactly that order (the self call does not pass the
    detour, so in replaced mode `hk_node_state_fill_calls` counts the
    top-level calls only; the map's original-mode entries, e.g. 467 in
    `acceptance-60s`, count every level).
  - Exit registers: after a children loop EAX = word `[N+10h]`, EDX = sx(i),
    ECX → see below; on a return path EAX is the subtraction residue above
    and EDX the class call's. The epilogue's `POP ECX`, `POP ECX` leave ECX
    = `[ebp-4]`, which only the array fill writes (N); in every other case
    it is stack residue (the core reports 0).
  - The ids are only those the compare chain tests; which shape-node classes
    they name (the slot-0 bodies) was not read, and the state word `N+4` and
    the role of the fill are not interpreted. `weapon_fit` calls it for
    a weapon with `[W+10h]` ≠ 0 with the node tree `[W+10h]` and the word
    `[result+4]` of the shape virtual `+20h`.

### Registers, stack and shims

EDX:EAX are exact for all seven. **ECX is dead at every call site**, so the
framework's entry ECX is returned: `40e61c` (site `0x41771d`: `pop ecx`),
`40fea0` (`0x40ff15`, `0x42064b`: `pop ecx`), `420634` (`0x4175f9`,
`0x437ce7`: `pop ecx`), `413ac8` (`0x417878`), `417d08` (`0x41787f`)
(`pop ecx` each), `40fb2c` (`0x417862`: `add esp,8`, then `shield_capacity`
`0x417bec`, which writes ECX, EDX and EAX before reading them),
`417530` (`0x417744`: `add esp,8`, then only AX is read from EAX and EDX,
ECX are overwritten at `0x417750`/`0x41775e`; the self call: `mov ecx,[ebp-8]`
and EDX, EAX reloaded by the loop test). The cores report the original's ECX
for the in-vitro comparison (`equip_slots`: the low word; `node_state_fill`:
only for class 140015h). No local or stack word is read before it is
written, so **no entry or exit shim**. All seven refuse compare mode
(guest calls, the recursion, the load-time state); the load is accepted by the
after-load snapshot.

The gun-list core (`0x40ff15`) and `weapon_fit` read `mount_entry`'s and
`mount_table`'s EDX after the call; both return the original's EDX (the entry
EDX and `[A+1F2h]`).

### In vitro (group `fit_leaves`)

`invitro/fit_leaves_cases.h` (identifiers `fit_`/`FIT_`): per hook 1,001,024
direct cases in the full tier (100,000 quick) over a 10 KiB arena (actor,
two weapons with definitions, 16 weapons and definitions for the item
array, set and record, shield record, a tree of up to 16 nodes with their
child lists and word arrays, and for `energy_rate` the real part tables),
against logging stubs for every direct callee (memset `49681a` clearing its
buffer, `multiply_q10`, `part_ratio` with a planned condition word,
`mount_entry`, the node class getter through an arena vtable). The stubs
log the call (ordered arguments, the slot count the caller pushed, entry
EAX/EDX/ECX, a hash of the arena) and in some cases change a word the
original reads again after the call (the weapon pointer `A+313h` after the
flag multiply, `S+6` of the shield record, a node's child count or array
count after a class call). Compared per case: EDX:EAX of the original (a
naked caller capturing ECX too), of the adapter and of the core, the core's
ECX against the original's (masked as above), the three call logs, the
arena and the two globals (`0x4a98e4`, `0x4a9d70`), and the adapter's
callee counter deltas. Aliases: the equipment record over the item array or
over the set record; node arrays starting at the node's own array count
word (with a value whose low word keeps the loop short). The tree classes
come from the boundary list (every chain value around 140005h..140016h and
1E0003h..1E0006h, negative and large ids) and random ids. 19 branch sites
(`energy_rate` 4, `equip_slots` 4, `node_state_fill` 11), all both ways; the
indirect `jmp` of `0x40fb73` is a table jump (each of its five targets is
checked by the case results). 256 installed 31/a7 cases per hook (fast and
preserving dispatch). Real-callee pass (3,072 cases): `mount_table` over
the real `mount_entry`, `shield_split` over the real `multiply_q10`
(entered 2,048), `energy_rate` over the real `multiply_q10` (2,061) and
`part_ratio` (1,553; built part tables, every maximum positive) in rotating
original/replaced/compare modes (replaced where a hook refuses compare), the
hook original or replaced. One caught mutation per hook (`MUTATIONS.md`).
Core unit test `//core:fit_leaves_test` (486 checks worked out by hand:
every register of each function, the flag, weapon and condition cases of
`energy_rate`, all five equipment slots and the null-array and zero-count
exits of `equip_slots`, the visiting order and registers of a five-node
tree, every flat class value of `node_state_fill`).

### Live counts (expected)

All seven run during the load only. With the hooks replaced:

| Counter | a60 / c1s / c1c | c3_09 | c4_08 | c3_10 | c1_02 | c5_05 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `hk_equip_slots_calls`, `hk_shield_split_calls`, `hk_energy_rate_calls` (= `hk_weapon_fit_calls`) | 10 | 16 | 11 | 16 | 13 | 15 |
| `hk_weapon_shape_word_calls`, `hk_node_state_fill_calls` (= `hk_weapon_fit_call_40e61c`, the weapons with `[W+10h]` ≠ 0) | 15 | 58 | 31 | 48 | 19 | 55 |
| `hk_mount_table_calls` (fits + the display-object code) | 59 | 133 | 82 | 114 | 77 | 132 |
| `hk_mount_entry_calls` (= `mount_table` + `gun_list_load`) | 63 | 141 | 89 | 122 | 81 | 140 |

(the map's entry counts of `0x40e61c`, `0x40fb2c`, `0x420634`,
`0x40fea0` in `acceptance-60s`, `c3_09-loadout-b`, `c4_08-razor-plasma`,
`c3_10-loadout-hercs`, `c1_02-probe`, `c5_05-moon-missiles`; the other C1_01
suites equal `acceptance-60s`). Callee counters: `hk_mount_table_call_40fea0`
= `hk_mount_table_calls`; `hk_shield_split_call_47e0b0` and
`hk_equip_slots_call_49681a` = their hook's calls; `hk_energy_rate_call_47e0b0`
and `_call_40dbc0` depend on the loadouts (`_call_40dbc0` = the fits with a
weapon in `A+313h`) and were not predicted. Unchanged: `hk_weapon_fit_call_*`
(the bridge still reaches the seven; `hk_weapon_fit_call_417530` equals
`hk_node_state_fill_calls`), `hk_gun_list_load_call_40fea0`. Every
`*_skipped`/`*_bypassed` 0.

### After-load snapshot reach

The seven write only heap blocks the snapshot already walks: the actor's
words `A+222h`/`A+224h` (shield shares), `A+256h`, the record `A+307h..A+31Ah`,
and the shape nodes' state word `+4` and word arrays `[N+1Ch]` of each weapon's
node tree (`[W+10h]`); no new fixed global is written, so the extras are the
current ones (those of sections 30 and 31).

### Live check (pending)

1. **Regression, all suites**, every registered hook replaced (the hooks.md
   selection line ends with `node_state_fill=replaced`), early arming as by
   default: every suite identical; the counters above, constant from the
   first record. The seven alone: `-Hooks
   "weapon_shape_word=replaced,mount_entry=replaced,mount_table=replaced,
   shield_split=replaced,energy_rate=replaced,equip_slots=replaced,
   node_state_fill=replaced"` (one string, no spaces).
2. **After-load snapshot** as in section 14 (imports original, the same
   environment, O1 and O2 every hook original, R every hook replaced) on
   `acceptance-60s`, `c1_01-collision`, `c3_10-loadout-hercs` and
   `c3_09-loadout-b`, with `ES2_AFTER_LOAD_SNAPSHOT=1` and the current
   `ES2_AFTER_LOAD_SNAPSHOT_EXTRA`, compared with `--scope pools` and
   `--scope all`: `IDENTICAL` expected (apart from the noise classes of
   section 14). A difference caused by these hooks would lie in a block
   reached through an actor's weapon system `[A+202h]` or its equipment
   record `A+307h`, in the words `A+222h`, `A+224h`, `A+256h`, or in a
   weapon's node tree `[W+10h]` (state words and arrays), or in the block
   pairing (the seven allocate nothing).
### Open

- The live check above.
- The roles of the shape-node classes (140005h..140016h, 1E0003h..1E0005h),
  of the node state word `N+4` and of the flags `A+0AAh`/`A+0ABh` are not
  interpreted; the cores copy the original's compare chain and arithmetic.
- `hk_energy_rate_call_47e0b0` and `_call_40dbc0` are not predicted.

## 33. The mission load in the own process

The root `0x461608` (section 30) runs in `es2sim.exe`; every guest function
it reaches that is neither a hook nor an earlier service is answered by own
code in `sim/src/load.c`, in the root's call order. Every fact is read from
the pinned `llvm-objdump -d -M intel` listing of the reference DBSIM (each
function's comment in `load.c` names its listing range); REA was used
read-only on a scratch copy for orientation only. Validated by unit tests on
a synthetic image and, for the last callees, in vitro (section 34); the
after-load snapshot of `es2sim` against the original's is **identical**
(section 14, "es2sim's snapshot against the original").

### What runs

302 service rows (`sim/dispatch/services/mission_load.h`), in the order the root
reaches them:

| Root step | Own code (VAs) | Notes |
|---|---|---|
| graphics context | `0x461294` (object vtable `0x4a2e10`, the 0x22c-byte palette copy, `[0x4d24ce]`, `[0x6c5f70]`, `[0x4a3614]`) | |
| frame clear, launch peek | `0x46548c`; `0x4614c0` with the stream close and reopen; the path builders `0x492b90`, `0x492bec` | `_open` gets the mode slot 0 (read only, no `O_CREAT`) |
| loading screen and palette | `0x461450`, `0x45cdf4`/`0x465f7c` (load by name through the VOL stream `0x472f14`), `0x4820d4`, `0x4821cc`, `0x481fe0` (range table `0x4a4fa5`, `0x465e28`, `0x465a24` into the LOGPALETTE `0x4d3728`), `0x4816a8`, `0x464a14`, `0x48349c`, `0x4815b8` | headless, see below |
| arena and devices | `0x47a288`, `0x47a2c8`, `0x474bbc`, `0x474be4` (the pop to index -1 keeps the original's `0x6c595c` quirk), `0x480af0`, `0x496406` | |
| sound | `0x462410`, `0x463624`, `0x463694`, `0x463370` | own up to the driver load `0x473708` (audio rows below) |
| renderer and classes | `0x48dd08`, `0x4913dc`, the TS/AN factories and reads (22 constructors, 12 reads, `0x48f9a0`, `0x48fea0`, `0x490e68`, `0x476710`, `0x490a34`, `0x4922d3`, `0x4922e6`, `0x49090f`), the grid `0x427568`/`0x42762c`, the shape loaders `0x474b30`, `0x474cd8`, `0x474de4`, `0x474c10`, `0x474c38` (its twin `0x474c74` has no static reference and no row), `0x474cb0`, `0x47a308`, `0x47a3b4`, `0x47a540`, the part lookups `0x47622c`, `0x476850`, `0x476890` | |
| stage-2 callbacks | texture pages `0x46a03c`/`0x469e14` (pages at `0x4d4f04`), lights `0x48bc40`, `0x406e44`, `0x4800c0`, `0x46134c`, `0x48bcec`, `0x48be50`, `0x407800`, `0x4953c4`, `0x415464`, `0x40ac38`, the binning tables `0x428230` with `0x428e10`, row buffers `0x43034c`, `0x492f54`, colours `0x467b64`, five inner-array constructors, the string holder `0x453878`/`0x453918`/`0x4539a0` | the effect pools `0x407b54`, `0x408860`, `0x409a34`, `0x46b1a8`, `0x408fb8`, `0x40874c`, `0x4083f8` (and `0x4097a8`, called from the world loader) are section 31's hooks, reached through the dispatch |
| world load callees | `0x42e010` with the sky, range table `0x430d08`, shade tables `0x42e760`, cycle registration `0x430638` (reads the virtual clock), remap arena `0x46a464`, world textures `0x46bd9c`, reciprocals `0x4680a0` (the flat objects `0x4097a8` through the dispatch); `0x405ebc`, `0x492d00`, `0x492c84`, `0x492c4c`, the element stores, terrain shading `0x46c2e0` (section 34), the height cache `0x4952e8`/`0x4952fc`, `0x4954e4` | the world loader's 12-byte-stride overflow is kept |
| actor load callees | mounts `0x42067c`, `0x40fab0`, `0x40dd4c`, weapon displays `0x4378d8`, damage display `0x4379cc`, weapon shapes `0x40f998`/`0x40fa88`, volumes `0x40c834`/`0x40ce28`, `0x4205cc`, `0x411d64` (`0x40e61c`, `0x40fea0`, `0x420634`, `0x413ac8`, `0x417d08`, `0x40fb2c`, `0x417530` are section 32's hooks), the memory stream (`0x47a888`, `0x47a8a4`, `0x47b36c`, `0x481aea`, `0x47b3a0`, `0x47b400`, `0x47b410`, `0x47b45c`, `0x47b4b4`, `0x47b454`) | |
| trivial bodies | 63 constant returns, empty bodies, argument returns, member addresses | each row names its listing bytes |

### Headless decisions

- **Display answers** (`es2_sim_load_platform`, measured on the host):
  `GetSystemPaletteUse` 0, `GetDeviceCaps(NUMCOLORS)` -1 (index 0x18 at
  `0x465ae9`, section 34; the field is `num_colors`),
  `GetSystemPaletteEntries` 0; the palette handle and paint DC are
  constants. With these answers the original computes the same LOGPALETTE;
  the colour table a presenter would upload is kept in
  `es2_sim_load_platform.color_table` with an upload count (for the
  presentation frame, presentation.md).
- **Blit and present** (`0x4816a8`, `0x464a14`): no pixels are written by
  `0x4816a8`. The row of `0x464a14` points at present.c's
  `es2_sim_service_present_capture` (section 34). The original's blit
  allocates and frees transient heap blocks; the own code does not, so the
  heap's free-block history can differ (no simulation field; the after-load
  compare of section 14 is identical with the noise model).
- **Terrain shading** `0x46c2e0` is own code (section 34); its tables lie in
  the heap and are snapshot-visible.
- Calls into hooks go through the dispatch (the hook's mode decides); calls
  into functions that are neither are refused by name, never stepped over.

### The root's other guest calls

Each has a service row or is a hook:

| VA | What | Own code |
|---|---|---|
| `0x473708` | SOS driver load (inside `0x463694`) | `sim/dispatch/services/audio.h` (audio rows below) |
| `0x44f6f4` | root step 10: the map bitmap and graphics-context set-up (711 instructions, 82 callees; it writes the map dimensions `0x4d1ce8`/`0x4d1cea` that the map hooks read) | `hud_load.h` |
| `0x48bd84`, `0x48bda4` | cameras (simulation-visible) | `hud_load.h` |
| `0x45cdf4`/`0x465618` | cursor load | `mission_load.h` / `mission_load_tail.h` (section 34) |
| `0x4544c0` | HUD set-up | `hud_load.h` |
| `0x45edb0` | view set-up | hook `view_offsets` (`core/src/camera_view.c`) |
| `0x429660` | cockpit construction (about 20k instructions; the MFD, HDD and HDDMap constructors read the clock; cockpit target H+0x210) | `hud_load.h` |
| `0x431530` | the order set-up | hook `order_setup` (`core/src/mfd_orders.c`) |
| `0x437c8c` | display objects (A+0x1fe) | `hud_load.h` |
| `0x42a1f0` | layout | `hud_load.h` |
| `0x46313c` | CD music | `audio.h` |
| `0x42eb40`, `0x43048c`, render_root's drawing | the first render | `mission_load.h`, `hud_load.h`; the terrain draw `0x46cb9c` and the frustum library `0x4949a4` in `terrain_draw.h`, `frustum.h` |
| `0x45d548`, `0x45d85c` | intro and drop | `flight_capture.h`, `cockpit_drop.h` |
| `0x45d490`, `0x45d50c` | window | `hud_map_draw.h` |
| `0x43122c`, `0x4328cc` | present, sound and clock | `hud_load.h`, `audio.h` |
| `0x433f7c`, slot `[0x4a5824]` | cockpit step, graphics slot | `mission_load_tail.h` (section 34) |

### Tests

`//sim:load_test` (synthetic image: graphics context, path builders, the
palette set with the headless answers, the arena allocator, texture pages,
the sound init with and without a context, the memory stream's read,
seek and tell, the mount offset walk over class tags, small bodies), with
a caught mutation (the end-of-data status 2 -> 1 of `0x47b3a0`);
`//sim/oracle:snapshot_test` (the writer below). The dispatch storage of
the shared tests (`//sim:input_test`, `loop_test`, `startup_test`,
`services_test` and the in-vitro cases `init_records_cases.h`,
`input_layer_cases.h`, `loop_cases.h`, `startup_cases.h`) holds 1024
entries.

### The after-load snapshot

`sim/oracle/snapshot.{h,c}` writes ES2SNAP1 version 2 (sections 9 and 13)
from callbacks: the proxy's fixed regions, the extras, the `-l` arena, the
CW3220-layout heap walk, the map and the modules. `es2sim` writes it before
the first logic frame when the run config has `[snapshot] after_load=1`
(keys `path`, `extra`, `max_bytes_mib` as the proxy's), with flags 32 and
128; `after_load_snapshot.py summary` reads the unit test's file. The
compare against the original is in section 14.

### Audio rows

The sound init's driver load `0x473708` and the table init `0x46254c` are
audio rows ([audio.md, audio in the own process](audio.md#audio-in-the-own-process)),
and so are the CD start `0x46313c` (step 23) and `0x4328cc` (step 26).
## 34. The root's last callees, the terrain shading pass

The listing is `llvm-objdump -d -M intel` of the reference `DBSIM.EXE`
(SHA-256 `8561aa47...c4ad`). Every row below was compared in vitro against
the original's machine code (`--load-rest`, see Tests). Rows in
`sim/dispatch/services/mission_load_tail.h`, code in `sim/src/load.c`.

### NUMCOLORS, not reserved entries

The palette set's system-entry count is `GetDeviceCaps(dc, 0x18)`: the
index pushed at `0x465ae9` is 0x18, which is NUMCOLORS (verified in the
listing). `load.h`'s field
is `num_colors`, and the count `r` is NUMCOLORS / 2 (`sar 1` with the
`jns`/`adc` fix-up, which rounds toward zero like C's `/`). On a
true-colour desktop GDI answers -1, so `r` = 0. Section 33 and
[flyer.md](flyer.md) use this.

### New rows (`sim/src/load.c`)

| VA | Row | Listing | What |
|---|---|---|---|
| `0x433f7c` | `cockpit_step` | `0x433f7c..0x43400f` | one step of the cockpit H = `[0x49b088]`: H+0x232 == `[H+0x226]` sets `[H+0x22a]` to H+0x236; otherwise `[H+0x226]` is set to H+0x236 and the empty `0x48992e` (word `0x4d2566` nonzero) or `0x486e70` (zero) is called with (`[P+4]`+4 or 0, P+8), P = `[H+0x236]`. Both callees only save and restore registers, so the row computes their effect, which is the registers the original leaves (EAX/EDX/ECX per path, in the code comment) |
| `0x465618` | `cursor_sprite_set` | `0x465618..0x465624` | `[0x4d3724]` gets the argument (the cursor sprite the present draws); EAX is the argument |
| `0x4955c4` | `mono_out` | `0x4955c4..0x4955d3`, `0x49571c..0x495748`, `0x4956e4..0x4956f4` | only with `-m`: `0x49571c(0x6cbc50, 0x20)`. With the DARKMONO.VXD handle `[0x6cbc50]` = -1, nothing happens: EAX 0 and ECX 0x6cbc50. With any other handle the original calls `DeviceIoControl(h, 5, &0x20, 1, 0, 0, 0, 0)`; the own process opens no VxD, so the row refuses that path |
| `0x46c2e0` | `terrain_shading` | `0x46c2e0..0x46c3ed`, `0x46c3f0..0x46c49d`, `0x46bfdc..0x46c238` | own code, see below |

The terrain shading pass `0x46c2e0(G)`:

- It first calls `0x48c444(0)` (the R3D object select, camera.md).
- For each row j < 1 << `[G+0x104]` and column i < 1 << `[G+0x100]`, it
  calls `0x46c3f0(G, s, i, j)`. Both bounds are signed and re-read on every
  iteration.
  - s is the row's cell plus 16 i.
  - The row's cell is NULL when its index is negative or at least
    1 << ((`[G+0x100]` + `[G+0x104]`) & 31). The original would then write
    near NULL, so the own code refuses, but only if the inner loop runs.
- A second loop over the empty `0x46c4bc` has no effect. The own code does
  not call it.
- Exit registers:
  - EAX: 1 << `[G+0x104]`.
  - ECX: the second loop's row counter, popped from its stack slot at
    `0x46c3eb`. That is 1 << `[G+0x104]` when positive, else 0.
  - EDX: 1 << `[G+0x100]` when that loop ran at least one row, else 0.
- An earlier headless stand-in set ECX to `[G+0x104]` and always set EDX.
  Both were wrong; the in-vitro test caught the ECX difference.

`0x46c3f0(G, s, i, j)`:

- The last row or column (j ≥ (1 << `[G+0x104]`) - 1 or
  i ≥ (1 << `[G+0x100]`) - 1, signed) gets flat normals: (0, 0, 0x800) in
  both triangles.
- Every other cell:
  - `0x46bfdc(G, i, j)`, an own function with no row;
  - the hook `normalize(s+1, 0x800)` and `normalize(s+7, 0x800)`.
- Then the shade bytes:
  - s+0xd gets AL of `0x48c16c(s+1, 0x6b4f5d)`;
  - s+0xe gets AL of `0x48c16c(s+7, 0x6b4f5d)`.
  - `0x48c16c` is the renderer's terrain lighting.
- `0x46c49f..0x46c4b5` is unreachable: it follows an unconditional
  `jmp`.

`0x46bfdc(G, x, y)`:

- It reads the corner heights (the first byte of each cell): h00 (cell
  (x, y), which it writes), h01, h11 and h10.
- d = (h00 + h11) - (h01 + h10) picks the triangle split. The split bits
  0..1 of s+0xf and the two 16-bit normals are:
  - d = 0: bits 1; both normals (h00-h10, h00-h01).
  - d > 0: bits 0; normal 1 (h00-h10, h00-h01), normal 2 (h01-h11,
    h10-h11).
  - d < 0: bits 2; normal 1 (h01-h11, h00-h01), normal 2 (h00-h10,
    h10-h11).
- The z words s+5 and s+0xb get (1 << `[G+0x108]`) idiv `[G+0x118]`.
- Then all six words are shifted left by one.
- Refusals:
  - A NULL corner refuses. It cannot happen inside the grid for shifts
    whose sum is below 32.
  - A faulting idiv refuses (reported at `0x46c208`).
- A zero normal faults inside the original `normalize`, at its idiv
  `0x46c29e`. The test inputs therefore keep the z word nonzero.

### Slot `[0x4a5824]` and the rest of the root

- Slot `[0x4a5824]` is zero in the image.
- `0x4893aa` fills it at run time by copying the graphics driver's table to
  `0x4a57ac` (`rep movsd`).
- For the BMP_8 driver (table `0x4a586c`), entry 30 is `0x4898df`. That
  function already has a row: `driver_saves_4898df`.
- So nothing is missing here.

Every direct callee of the root `0x461608..0x461f77` has a row or a hook
(section 33). The intro and drop sequence `0x45d548` and `0x45d85c` runs
only when G+0x54 is set.

### The present `0x464a14`

`0x464a14` is a thin wrapper:

- It pushes `0x4d2ff8` and `0x4d3018` and calls `0x465628`.
- Then `add esp, 8`, `ret`.

`0x465628` with DirectDraw off goes to the GDI branch at `0x46589a`:

- If `[0x4d3034]` = 0, nothing happens, and EAX, ECX and EDX keep their
  entry values.
- Otherwise:
  - `GetClientRect`, `InvalidateRect(TRUE)` and `BeginPaint`;
  - with a palette `[0x4d302c]`: `SelectPalette(FALSE)` and
    `RealizePalette`;
  - the scales by `idiv` with `[0x4d253e]`/`[0x4d2542]`;
  - `StretchBlt(SRCCOPY)` at `0x4659ad`;
  - the old palette back (`SelectPalette(TRUE)`, `RealizePalette`) and
    `EndPaint`.
  - EAX is EndPaint's result.

`present()` in `sim/src/present.c` reproduces this, including the GDI calls
and their arguments, and is compared in vitro
(`differential_present_test`). The row of `0x464a14`
(`sim/dispatch/services/mission_load.h`) points at
`es2_sim_service_present_capture`.

- The row returns EAX = 1 on the no-DC path, where the original keeps the
  entry EAX.
- No caller reads that EAX:
  - the root continues at `0x461e21`;
  - `0x45719c`'s caller at `0x4570ba` ignores it;
  - the `0x466xxx` callers end in `xor eax, eax`.

### The cockpit step `0x433f7c`

`0x433f7c` is also called by the display transition `0x42a9c0`; it has one
row (`cockpit_step`). display.c's `0x433ee8` is its twin with the record
H+0x23a instead of H+0x236.

### Tests

- `//sim:load_test`, new case `load_rest`:
  - the shading pass on a 2 x 2 grid with stand-ins;
  - a shift of 31, which leaves no rows;
  - a zero divisor, which is refused;
  - the cockpit step's three paths;
  - the cursor store;
  - the mono output with handle -1 and with another handle (refused).
- `//tools/reference-game/invitro:differential_load_rest_test`
  (`--load-rest`, `load_rest_cases.h`):
  - shading, 1500 cases identical (world, DATA, heap, EAX/EDX/ECX), with 0..3
    lights of the three kinds and the view pointer as the identity or a
    random matrix;
  - small, 1000 cases identical;
  - the own-side refusals.
  - Coverage of one run: 106 empty passes, 78 without cells, 8482 edge
    cells, 9766 interior cells (split level 4860, positive 2458, negative
    2448). The cockpit step took each of its four paths at least 230
    times.
  - normalize and view_cases.h's ten matrix and vector hooks run as the
    original's code on the own side.
- Mutation:
  - Change: word s+3 = h01 - h00 instead of h00 - h01 on the d < 0 path.
  - Result: caught, the world differs in case 3. Logged in
