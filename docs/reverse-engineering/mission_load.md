# Mission load `0x461608`: from the launch files to the first frame

Reference for the mission load of the reference `VER95/DBSIM.EXE`
([reference_version.md](../reference_version.md)), image base
`0x400000`. Facts are read from the pinned `llvm-objdump -d -M intel`
listing. Call trees come from a control-flow walk of each function and a
recursive walk of its direct `call rel32` targets, with fresh aligned
decoding where the linear listing is misaligned; indirect and virtual calls
are **not** followed unless named. Role names are **hypotheses**
unless a linked document gives them.

- Sections 1 to 12 describe the original. Their live statements come from
  the entry-probe counts of the [DBSIM map](../reference-game/dbsim_map.md)
  (suites a60/c1s/c1c/c3_09/c4_08/c3_10),
  taken with the late arming of section 8.
- Sections 13 to 32 hold the pieces of the load as own code: per piece the
  hooks, hook group and core file, the contract from the bytes, the
  in-vitro validation, the live counts with early arming (section 13, the
  default), the after-load snapshot reach and the live result. Live counts
  come from the entry-count map of all 40 suites (every hook original,
  early arming) or, for sections 30 to 32, from the all-replaced regression.
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
  them are not installed yet, and every load-only function reads 0.
- **RNG (section 6):** stream A is drawn in the terrain loader (pass 1) and
  in the initial damage of authored mechs (pass 2), including the debris,
  fire and sound draws that the damage virtual `+0x74` (`vehicle_part`) can
  reach; stream B is drawn by those sound paths and by the first render and
  the start sequence. VSHELL's own randomness enters only as `mission.var`
  words 4..6.
- **Load-time cleanup** is the pair of drain loops of section 5;
  `0x497f9a` is not part of the load (section 11).
- **Acceptance:** in-place compare cannot cover the load as a whole,
  because it allocates from the heap and reads files; the acceptance for
  the replaced load is an after-load snapshot compared between an original
  and a replaced run with pointers canonicalized (sections 9 and 13).

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
| `GetWindowRect`, `GetClientRect` | `0x461652`, `0x461682` | window border sizes: four differences stored at locals f−34h..f−40h and never read | platform |
| `0x461294` | `0x46169f` | `new(0x239)`, graphics-context constructor `0x480c34`, stored at `G+0x12` (`0x4d24ce`), selected (`0x480d44`) | H3 |
| `0x464c78` | `0x4616a4` (and `0x461728`, `0x461835`, `0x461a6a`, `0x461a74`, `0x461e03`, `0x461e0d`, `0x461f44`) | message pump | platform |
| `0x46548c` | `0x4616a9`, `0x461a6f` | clear and show the frame buffer ([flyer.md](flyer.md#function-table)) | H3 |
| `0x4614c0` | `0x4616b6` | launch peek: `script.dat` header word 0 (world) and `player.mec` words 0..4; writes the world word to its first argument (local f−44h) and "MEC record 0 has vehicle ID 8" to its second (f−42h); nonzero f−42h selects `flyrX` | file |
| `_strcat`, `0x492bec`, `0x465f7c`, `0x481fe0` | `0x4616eb..0x461722` | loading-screen palette `ld_herc<w+1>.dpl` or `ld_flyr<w+1>.dpl` (template `ld_` + `hercX`/`flyrX`, character 7 replaced by `'1'+world`) loaded and selected | resource, H3 |
| `0x461450` | `0x461734` | loading-screen bitmap of the same name through `0x45cdf4` (`.hba` when byte `0x4d2537` = 3, else `.dba`), blitted and presented | H3 |
| `new(6)`, `0x480af0` | `0x46173c..0x461776` | graphics-library object `(obj, 0x26d3)` stored at `G+0x4e` (`0x4d250a`) | H3 |
| `0x462410` | `0x46179f` | sound init `(byte G+0xc, five stale words)`: window hook, voice channels, `sound.cfg`, driver, sample preload; sets the sound context `[0x49f8ec]` ([audio.md](audio.md)) | audio |
| `0x463370` | `0x4617a7` | empty (`ret`) | — |
| `0x48dd08`, `0x4913dc` | `0x4617ac`, `0x4617b1` | graphics-library state (`memset 0x6c60d4`, `0x5b30` bytes) and two registrations through `0x47a0b8` (hypothesis: persistent class tables) | H3, resource |
| `0x401d94(2)` | `0x4617b8` | the 19 stage-2 callbacks (section 4) | load, resource, file |
| `0x46134c` | `0x4617be` | scene light set-up: two light objects (`0x48bc40`), `euler_matrix`, `0x4800c0`, `0x48be50`, `0x407800` | H3 |
| `0x467840` | `0x4617c3` | reads the clock once: limiter reference and clock words `0x4d3b64..0x4d3b78` ([timing.md](timing.md), [mission.md](../formats/mission.md#timers-and-update-order)) | platform |
| `0x424308` | `0x4617c8` | **world load**, script pass 1 (section 2) | load, file |
| — | `0x4617cd..0x4617de` | dword `G+0xbb` (`0x4d2577`) ← 25 if byte `G+0xb5` (`0x4d2571`, set by the world load from the MEC vehicle-8 test) else 13 | load |
| `0x4282d8` | `0x4617fa` (loop) | for every entry of the terrain object table `[0x4aaa78]` (count word `0x4aaa7c`; the terrain loader `0x42789c` fills the table only for a negative zone word and clears it otherwise, section 24; 0 calls in every suite, section 30): virtual `+0x10`, then the binning step `0x42837c` ([terrain.md](terrain.md)) | H3 (binning) |
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
| `camera_update` `0x4011a0` | `0x461d0c` | first camera step on `[G+0x2e]` ([camera.md](camera.md)) | hook |
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
(byte `G+0xc`, the sound switch) is meaningful; slots 1..5 carry the
stale high words of EAX/ECX/EDX. `0x462410`
reads only `[ebp+8]` (no other `[ebp+…]` operand in its body),
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
   N × 126 bytes at `0x4aaa60`, the records (variable length in the file:
   76 + 4 × the item count of word `+6`; fixed 126 bytes at runtime)
   ([oracle_suites.md](../reference-game/oracle_suites.md#launch-boundary-and-storage));
   byte `0x4d2571` ← (record 0 vehicle ID = 8), read also when N is 0.
4. `data\mission.str`: body at `0x4aa650`, string pointers at `0x4aa660`;
   the count is the body's word 0 (the 50 passed to `0x4539a0` is not
   read); the final LF is stripped (an empty string has its preceding byte
   tested for LF).
5. `data\script.dat` (name from `0x492b90`): the 20-byte header to
   `0x4a9e4e`, word `0x4a9e52` ← 0, then **`0x42e010(0x4a9e4e)`, the world**
   (`.wld`, `.dpl`, `cyc`, `.rmp`, the flat-object pool `0x4097a8`;
   [mission.md](../formats/mission.md#world-resource-selection)), then the
   lists of [mission.md](../formats/mission.md#prepared-script-exact-framing):
   coordinates (12-byte points from the array construction `0x49667c` at `[0x4a9e0c]`, count
   `0x4a9e06`, map rectangle `0x4aa640..0x4aa64c`, set only when there are coordinates), angles (`[0x4a9e10]`,
   × 182), routes (`[0x4a9e18]`), regions (`[0x4a9e20]`, `0x423358`),
   triggers (`[0x4a9e28]`, count `0x4a9e24`; 74 bytes in the file, 58 at
   runtime, region count = the leading non-negative indices, at most 8),
   timers (`[0x4a9e38]`, count `0x4a9e34`: 49-byte objects from `0x49667c`
   with the empty constructor `0x427207`, each set by `timer_init`
   `0x423104` → `timer_arm`). Without groups the group-0 slots are stack
   residue.
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
   `0x422e90(type)` for each flyer, `0x405ebc(type, byte)` (the byte is ignored) for each
   building (`bases_an.dts`, `bases.dgs`).
9. **Pools:** MECH pool `[0x4a9b7a]` = `0x471ad8(new(0x12), mechs, 0x36a)`
   with removal list `[0x4a9b7e]` (25 slots); FLYER pool `[0x4a9db9]` =
   `0x471ad8(…, flyers, 0x291)`, list `[0x4a9dbd]`; building pool
   `[0x4a95a0]` by `0x405df4(buildings)`; actor list `0x4a9af8` by
   `0x411df8(…, mechs+flyers+buildings)`; then `0x40fecc`, `0x40d218`,
   `0x40cbec` (the gun-list, damage-slot and collision-slot tables, sized by the running slot words of the type selects), `0x42049c` (mech name table and shapes,
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
   - An unselected MEC actor does not advance the MEC ordinal.
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
   `0x411b90`; no ground height.
6. **Buildings** (52 bytes): factory `0x405314` (allocates from
   `[0x4a95a0]`, [vtables.md](vtables.md#12-who-builds-which-class)),
   then, only when the building has a coordinate, virtual `+0x10`, terrain
   marks `0x470ed4` and `terrain_height`; `0x411b90`.
7. **Orders:** count `0x4a9e2c`, `new[]` at `[0x4a9e30]`, 22-byte runtime
   orders, subjects through `0x425348`
   ([mission.md](../formats/mission.md#order-and-actor-group-wire-mapping)).
8. **Groups** (156 bytes): the loop runs over pass 1's group array (the
   count read in pass 2 only gates it); group 0 takes the roster of the appended MEC
   actors; `heading_point`, constructor `0x423b34`, `0x405db0`.
9. **Objectives** (54 bytes): count `0x4a9e3c`, `new[]` at `[0x4a9e40]`
   (stride `0x4c`), subjects through `0x425348`, two 20-byte `memcpy`,
   string pointers defaulting to `0x49a870`.
10. `0x47129c([0x4a0be0])` (terrain cell marks, [terrain.md](terrain.md));
    every building's z ← `terrain_height` (no offset); trigger subjects of
    types 7..10 resolved through `0x425348`, types 0..6 cleared; close.

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
| pass 1, terrain `0x4252f7` | `0x42789c` → `0x46c4c4`: R `0x46c69d` | A | one per block of the zone bitmap (section 24; `0x46c754`'s site `0x46cac0` is in its text-grid path, which no suite runs) ([rng.md](rng.md#stream-initialization-at-runtime)) |
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
`ES2_EARLY_ARMING=0`; early arming (section 13, the default) arms at the
call of the load `0x45f42b`, before phase A.

Late arming installs function hooks, entry probes, coverage and the RNG
hooks at the proxy's first DirectDraw or MCI call
([live_coverage.md](../reference-game/live_coverage.md#arming-time));
DirectDraw is never created in the oracle
([presentation.md](presentation.md#directdraw-branch)). The first MCI call
on the direct paths of the load is `0x46313c` → `0x464858` → `0x473c48` →
`mciSendCommandA` at `0x461dc3`. The map brackets it in all six suites:

- never entered after arming although called unconditionally: `0x452ae4`,
  `0x44f6f4`, `0x4544c0`, `0x429660`, `0x431530`, `0x437c8c` (single call
  sites in `0x461608`, the last at `0x461cd1`), `0x42a1f0` (`0x461d2f`);
  `0x464858` reads 0 (its load-time call is the one that arms);
- entered once after arming: `0x45b758` (single site `0x461f3f`);
- `0x45fca8` and `0x45f570` both read 937 in c3_10: the load-time render at
  `0x461df1` is counted (the frame that ends a mission returns before the
  render).

So arming lies between `0x461d2f` and `0x461f3f`, and `0x461dc3` is the
only MCI call between them on a direct path (the six suites run with a
sound context, [audio.md](audio.md), and header word 4 is 0 in every
prepared example,
[mission.md](../formats/mission.md#prepared-script-exact-framing)).
**Consequences under late arming:**

- Phases A to D run unhooked, also in a fully `replaced` run: the
  pre-arming closure (716 functions) contains 79 registered hooks, among
  them `pool_alloc`, `pool_prev`, `removal_append`, `removal_flush`,
  `terrain_height`, `script_start`, `object_init`, `random_damage`,
  `actor_animate`, `mount_offset`, `shield_capacity`, `weapon_record`,
  `def_lookup`, `fire_group`, `camera_update`, `euler_matrix`, the track
  and matrix helpers and `rng_next`/`rng_bounded`. Their counters never
  include load-time calls.
- `0x41b130` (cockpit sync) is called at `0x461d02`, before the arming
  point, so its hook sees only the render-root calls; `0x45fca8` (render
  root) is called at `0x461df1`, after it, so its counter includes the
  load-time call.
- Every load-only function reads 0 (or has no row) in the map.

## 9. What the after-load acceptance must snapshot

**In-place compare does not fit the load.** The compare mode runs the
original on a snapshot, restores and runs the replacement at the same
addresses ([compare_mode.md](../reference-game/compare_mode.md#snapshot-regions)).
The load allocates heap blocks (`new`, `new[]`, pools whose headers are
heap objects) and reads files; a second phase would allocate again at
different addresses and leak the first phase's blocks. Compare is refused
for every piece that allocates; it is possible for the pure pieces of
section 10 (`0x425348`, `0x411b90`, and `0x415068` with an extra region),
with the actor pools as snapshot regions.

**Acceptance: an after-load snapshot, original against replaced.** At the
first entry of the logic frame `0x45f570` (after the load, before any frame
code) the snapshot holds:

- the fixed regions: header and mission tables `0x4a9e00..0x4a9e98`,
  variables `0x4a9e70` (2000 bytes), strings `0x4aa640..0x4aa728`,
  selection and MEC `0x4aa72a..0x4aaa7c`, stage-2 type globals
  `0x4a9572..0x4a9dfc` and `0x6b4f2c..0x6b4f3c`, the globals object
  `0x4d24bc..0x4d27b8`, both RNG streams (`0x4d2599`, `0x4d260b`, `0x72`
  bytes each), the actor list `0x4a9af8`, `0x49a871..0x49a884`,
  `0x4cf878..0x4cf888`, `0x4d03c8`, `0x49b0f8`;
- the pool blocks of every pool of sections 2 and 4 (header `+0` free,
  `+2` used, `+4` size, `+6` block; (free + used) × (size + 8) bytes) with
  their removal lists;
- the heap tables reached from the fixed pointers (coordinates, angles,
  routes and their point arrays, regions, triggers, timers, orders, groups
  with their actor and order arrays, objectives, string indices, MEC
  records, result pairs).

Pointers are canonicalized (a dword pointing into a dumped block becomes
(block id, offset)); two runs (all hooks original; the load's hooks
replaced) must give identical dumps. The tick log's first record is written
after the load and frame 0, so without the snapshot a load difference shows
only through the watches and later frames. Cockpit, HUD, camera and
graphics-library objects (H3) do not count, except the camera position and
angles, which the simulation reads. Section 13 implements this with a walk
of the whole CW3220 heap instead of per-table dumps.

## 10. The load in hookable pieces

Map counts are a60/c1s/c1c/c3_09/c4_08/c3_10 under late arming: every
piece reads 0 in all six suites (load-only code). With early arming (L0) a
once-per-load piece is called exactly once per suite; per-actor pieces once
per selected actor of the suite's launch set (counts from that set's
`script.dat` with `audit_script.py` plus the selection rule of section 2
step 6, plus N MEC records). "guest" = stays original behind the bridge.
Every piece is own code; the last paragraph names the sections.

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

## 11. Notes for other documents

- `0x497f9a` is the exception-frame descriptor that `smoke_update`
  `0x4092dc` passes to `0x49630c` (`mov eax, 0x497f9a` at `0x4092e2`); the
  records after it pair the smoke pools `0x4a966e`/`0x4a9684` with
  destructor code of that module (exit-time static destruction, compare
  the exit records in [rtl_imports.md](../reference-game/rtl_imports.md)).
  It is not a cleanup table of the load.
- `0x4258a2` is the call site of `0x4175dc` (weapon fitting) inside
  `0x4253d8`, not a function.
- For [rng.md](rng.md#draws-during-the-mission-load): the MECH damage
  virtual `+0x74` reached from `0x4178e8` adds the `vehicle_part` draws of
  section 6, including stream-B sound draws; `0x4178e8` runs only for
  selected authored mechs, not for MEC actors. For
  [rng.md](rng.md#arming-and-the-loads-draws): the map brackets the late
  arming point between `0x461d2f` and `0x461f3f` (section 8).
- Cockpit sync's mission-start call `0x461d02` precedes the late arming
  point.
- `0x433f7c` starts with `mov eax, [0x49b088]`, not a one-byte push: it is
  not probeable, so its "0" in the functions index (`docs/functions/`)
  means "no probe", not "never entered".

## 12. Open

- Whether a part destroyed by the initial damage fires the actor's
  trigger `A+0x1b6` during the load (static reach through `vehicle_part` →
  `0x423430`). The drain loops find both pools empty in every suite: the
  root's callee counters `407b3c`, `4077e8`, `409904`, `4083e0` and
  `478628` read 0 in every suite (section 30).
- The roles of `0x48dd08`/`0x4913dc` (hypothesis: persistent class
  tables).

Answered elsewhere: the stream-A draws during the load (section 13); who
places MEC actors (section 17); the roles of `0x4955c4` and of the slot
`[0x4a5824]` (section 34); the tables of `0x40fecc`, `0x40d218`, `0x40cbec`
(section 29); early arming leaves every golden identical (section 13).

## Evidence

Sections 1 to 12 rest on a control-flow and direct-call walk from
`0x461608`, the init records and stage registrations, the resource names per
function, the RNG reach per subtree and the hooks before and after the
arming site, computed over the pinned `llvm-objdump` listing, `docs/functions/`
and the DBSIM map.

## 13. L0: arming before the load and the after-load snapshot

Code: [early_arming.h](../../tools/reference-game/proxy/early_arming.h)
(included by `media_proxy.c`, entry stubs `ea_entry_load` and
`ea_entry_frame`),
[after_load_snapshot.py](../../tools/reference-game/scripts/after_load_snapshot.py),
`configure-tick-log.ps1` (`-EarlyArming`, `-AfterLoadSnapshot`). The
fixture's `reference` mode re-checks the site facts against
`VER95\DBSIM.EXE`. **Live.**
Early arming is the default; the snapshot is opt-in.

### Why the proxy armed at the first MCI call

`initialize_forwarding` (`media_proxy.c`) arms everything at once: it loads
the system `ddraw.dll` and `winmm.dll`, reads `es2-oracle.ini`, hashes
`DBSIM.EXE` through CryptoAPI (a provider DLL is loaded), opens the tick log
and may start its writer thread, installs the function hooks
(`install_rng_hooks`), live coverage and entry probes (both register an exit
callback through CW3220's `_atexit` and patch `__exit`), the speed runtime's
exit hooks and the VOL probes' forwarding mark. None of this may run in
`DllMain` (loader lock), so it hangs on the first call that reaches the
proxy (a DirectDraw export or one of the two MCI import slots). Nothing in
it reads what the load builds, so an earlier point is possible if it keeps:

- **Not before `__startup`.** The import shim's slot runs before the 65
  init records and CW3220's start-up; loading `ddraw.dll`, `winmm.dll` and
  a CryptoAPI provider and registering `_atexit` there would change more
  than the load needs (the import shim hashes with an own SHA-256 to load
  no DLL), and the static initializers, WinMain's set-up, the VOL
  registration and stage 1 would run hooked.
- **Nothing written to the game's stack** (guest code reads stack
  residue): the arming code runs on a private stack.
- **The site bytes original before any verifier runs**, so coverage,
  probes and hooks see the original image.

### The arming point: the call of the mission load

`0x45f42b: e8 d8 21 00 00` (`call 0x461608`) in the game main `0x45f160`,
after stage 1 and the optional arena set-up (`0x476df4`, `G+0xb0` ← 0),
before `0x461608`'s first instruction (also the target of the `je` at
`0x45f401`; the instruction boundary is kept). It is the only `call rel32`
to `0x461608` in the code section and no registered hook prologue covers it
(fixture, `reference` mode).

- **`DllMain`** (DBSIM.EXE only): reads `[arming] early` and
  `[snapshot] after_load` from `es2-oracle.ini` next to the proxy
  (`GetPrivateProfileIntW`, kernel32; no DLL is loaded). With neither key
  set nothing else happens. Otherwise it checks the PE header
  (`es2_hook_image_valid`) and that the site is `call rel32` to its target,
  allocates a 1 MiB private stack and rewrites only the call's rel32 to
  `ea_entry_load`. A refused check changes nothing.
- **At the call** (the import shim's technique, `rtl_startup_entry`): the
  entry stub stores ESP, switches to the private stack, saves flags, the
  general registers, the TEB stack bounds (`fs:[4]`, `fs:[8]`, set to the
  private stack) and the x87/SSE state, restores the original rel32, runs
  `initialize_forwarding` (through the same `InitOnce`; the first MCI call
  later only registers the codec), restores everything and jumps to
  `0x461608` with ESP and every register as the call left them. The only
  stack write is the call's own return address `0x45f430`. LastError is
  preserved. If something armed before (a DirectDraw or MCI call before the
  load), the step only logs `early_arming_already_armed`.

**What changes with `early=1`:**

| Area | Change |
|---|---|
| Game code and data | nothing by itself; the hooks, the tick site, coverage and probe sites are installed before phase A instead of at `0x461dc3` |
| Without `[hooks]` (every hook original) | no function hook is installed at all; only the tick log, coverage and probes (if configured) and the DLL loads move earlier. Heap reservations may land elsewhere (heap addresses already differ between runs, [rtl_imports.md](../reference-game/rtl_imports.md) 4.3) |
| With `[hooks]` | the load-reachable hooks (section 8's 79, plus for example `cockpit_sync` `0x41b130` at `0x461d02`, `stage_dispatch` `0x401d94` at `0x4617b8` and those reached through virtual calls, such as `vehicle_part` from the initial damage) run in their configured mode during the load |
| `hk_*_calls` | include the load-time calls: against a run without early arming every counter of the first tick record is larger by exactly the calls made between `0x45f42b` and `0x461dc3`, and the offset stays constant; `hk_rng_next`/`hk_rng_bounded` gain section 6's load-time draws |
| Tick records | unchanged: the first record is still written at the first frame; `hk_*`/`rtl_*` fields are not compared (`oracle_regression.py` `TICK_FIELDS`); the `start` line is written earlier |
| Live coverage, entry probes | the load's executions are counted; their traps then happen during the load too, with the exception-dispatch residue of [live_coverage.md](../reference-game/live_coverage.md) |
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
- **the whole CW3220 heap**: the region table of the loaded `cw3220.DLL`
  (count `+0x23938`, (base, size) pairs `+0x23738`; only when the DLL is
  the reference `cw3220.DLL`) walked block by block (marker 1 at the base,
  header `p−4` and footer `p+s−8` = `s|1` in use, end marker); every block
  is listed with address, size and in-use flag, blocks in use with their
  `s−8` data bytes. This covers every pool block, removal list and heap
  table of section 9; `[snapshot] max_bytes_mib` (default 256) caps the
  data;
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
overwrite, and the script on the proxy's own files; `reference` the two
sites, their single callers, the targets' prologues and no hook prologue at
the sites. `//tools/reference-game/scripts:after_load_snapshot_unit_test`
(relocation, unaligned pointers, content, pointer offset, image pointer,
layout, ignore, header and region differences, truncation, malformed files,
CLI exit codes). `oracle_speed_scripts_test` covers the INI sections and
manifest keys of `configure-tick-log.ps1 -EarlyArming -AfterLoadSnapshot`.

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

- **Every hook original, early arming:** every suite identical to its
  golden; the proxy log (`video-compat.jsonl`) has `early_arming_armed`
  once per game process.
- **Every hook and the CW3220 imports replaced, early arming:** every tick
  comparison identical; the load-reachable hooks run replaced during the
  load. The load-time calls are the `hk_*_calls` at the
  compared tick minus those of a late-arming run:
  about 95 hooks have load-time calls in all 22 comparable suites, among
  them `rng_next` **1,024–16,436** (the load-time draws of section 6),
  `pool_alloc` 40–104, `object_init` 28–76, `terrain_height` 24–126; the
  damage path (`vehicle_part`, `region_hit`, `weapon_damage`,
  `rng_bounded` 8–19) in 13 suites (the initial damage).
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
walk `pool_nth` `0x471cb0`) and L3 (`initial_damage`): nine hooks, adapter
group `mission_load`, core `core/src/mission_load.c`. **Live**
with the after-load snapshots below. The after-load snapshot's noise model is
defined here.

### The noise model

`tools/reference-game/scripts/after_load_snapshot.py` compares two
after-load snapshots (section 13):

1. **Pairing by reference.** Fixed regions lie at the same addresses in
   both runs. A 4-byte window (aligned or not) that points into a heap block
   in both snapshots, at the same block offset, pairs the two blocks; the
   walk continues breadth-first through every paired in-use block. Only this
   graph is compared. A block reached twice must pair with the same block
   (else `pairing_conflict`). Unreached blocks (environment, CW3220 stream
   buffers and objects, freed blocks) are counted, not compared;
   `--unreached align` aligns them by size sequence, for information only.
2. **Pointers.** Equal when both point into paired blocks or into the image
   at the same offset; with version-2 snapshots also into the same module at
   the same offset, or both into other committed memory of the same type
   (`foreign`: system heaps, VirtualAlloc'd buffers, mapped views; counted
   as `foreign pointers`).
3. **Learned noise.** `noise A1 A2` (two original runs) writes every
   remaining difference as an entry (block path and offset, or fixed VA);
   `compare --noise FILE` counts a covered difference as noise. A block path
   is the chain by which the walk first reached the block in A:
   `0x004d250a/+0x6` is the block the pointer at `+6` of the block of fixed
   dword `0x4d250a` points to.
4. **Scope.** `--scope all` (default) walks from every fixed byte;
   `--scope pools` only from the pool headers and removal lists of sections
   2 and 4 and the actor list `0x4a9af8` (pointer, capacity, count words).
5. `--pairing index` pairs by walk position, for diagnosis only: CW3220's
   start-up copies the process environment into heap blocks, so one extra
   variable shifts every later index (6,705 to 7,554 differing bytes between
   original runs).
6. **Classes** (default on, `--no-classes` off; counted on the `CLASSES`
   line and in `classes_matched`, never written to `noise` files):
   `environment`, `matrix_padding`, `aim_residue` always;
   `allocation_slack`, `block_fit`, `byte_count`, `freed_target` only when
   the snapshots' `heap_runtime` differ (own against cw3220).

**Snapshot version 2** (`proxy/early_arming.h`): after the heap, the
committed regions (base, size, allocation base, protection, type; one
VirtualQuery walk) and one record per image (base, size of image from its PE
header, file name without directory); flag 64 marks a map cut short.
Version 1 is still read (no foreign or module classes).

Tests: `//tools/reference-game/scripts:after_load_snapshot_unit_test`,
`//tools/reference-game/proxy:early_arming_snapshot_test`.

### Noise classes

Each class covers bytes that are process input or never-read residue.

**`environment`.** The walk does not follow windows into `_environ` and its
strings. `_environ` = the first of the walk's first 256 in-use blocks that
holds at least 8 pointers to starts of in-use `NAME=VALUE` string blocks,
else 0. Counted as `environment_links`, blocks as `environment_blocks_a/b`
(also in `summary`, with `environment_array`). Why: the strings vary per run
(`ES2_CLONE_LOCK_TOKEN` holds a fresh GUID), and links to them are
coincidental windows (c3_09-loadout-b: unaligned bytes of the `mechwpn2`
shape arena, path `0x4a98f4/+0x23d9e/+0xbc`, hit `_environ` only when both
heaps lie at the same addresses). es2sim's own environment copy
([own_process.md](../reference-game/own_process.md) "The heap trace") is
found too.

**`matrix_padding`.** Byte 19 of each node matrix of a paired animation
manager whose vtable, node count and paired matrix block agree (see "Node
matrix byte 19"). Seen as `+0x173` of three 432-byte blocks: path
`0x4a9b84` (player actor in the MECH pool), offsets `0xa92`, `0xe04`,
`0x185a` = actors 3, 4, 7 (stride `0x372`) at `+0x34`, the manager M
(`new(0x42)` at `0x402301`); `[M+0x16]` = 32n + 8 bytes from
`0x49667c(0, 0x20, n, 1, 0x495132)` via `new[]` `0x4774f0`, so 432 = 32 ×
13 + 8 + 8 overhead and `0x173` = node 11, byte 19. Visible because early
arming runs the traversal hooked during the load (`node_matrices` 25
load-time calls in a60; `pose_parent` 0, computed inline). **No compared
field depends on it:** byte 19 is read only inside 20-byte block copies
(padding contract (b)–(d); `matrix_compose` copies in the hit tests keep it
unread, [hit_tests.md](hit_tests.md)), and the tick fields
(`oracle_regression.py` `TICK_FIELDS`) read no padding.

**`aim_residue`.** cockpit panel H = `[0x49b088]`, reached as
`[[0x4d24c0] + 0x1f]` (getter `0x42982e`), the 656-byte block at
`0x004d24c0/+0x1f`. Its only writer `0x434a24` (`..0x434a68`, sole call
site `0x41b458` in cockpit_sync `0x41b130`) stores +0x26c ← target,
+0x270..+0x27b ← 12 bytes at argument 1, words +0x27c/+0x27e ← arguments
2/3: cockpit_sync's frame locals ([hud.md](hud.md) step 14), stored on
every call, also without a target. At the snapshot (logic frame `0x45f570`)
they hold a target-less call's stack residue (original nonzero, O1 = O2;
words read 0x004d/0x24bc, likely a saved 0x004d24bc, not followed); es2sim
runs cockpit_sync at its guest stack coordinate ([hud.md](hud.md), "stack
residue"), which holds zeros. Readers (HUD draw `0x43d7e1`, MFD status
`0x43a5a0`) read them only when +0x26c ≠ 0, and the render root rewrites
them every frame before the HUD draw. **Rule:** both H blocks found and
paired, both with target 0 at +0x26c: a differing run in +0x270..+0x27f is
counted (5 runs per suite). A target ≠ 0 on either side, or a difference at
+0x26c or +0x280, stays a difference.

**Heaps of different runtimes** (own heap model `core/src/rtl_heap.c`,
shared by es2sim and the own `cw3220.dll`, against CW3220's). Two original
runs agree on these bytes, so no noise file can learn them:

- **`allocation_slack`.** n requested bytes get a `round4(n) + 8` block;
  calloc/new fill only n, so the last 1-3 data bytes keep old memory. Covers
  a run entirely within the last 3 data bytes of the shorter block. Below 8
  requested bytes the slack can reach 7: not modelled, still reported.
- **`block_fit`.** Next-fit hands out the whole free block when the
  remainder would be below the 16-byte minimum, so sizes can differ by less
  than 16 (multiples of 4). Common bytes are still compared; `CLASSES` gives
  the summed size difference B − A (`block_size_delta`).
- **`byte_count`.** Dword `0x6bb30c` sums the header words of live blocks
  from `0x47747c`; nothing branches on it
  ([rtl_imports.md](../reference-game/rtl_imports.md) section 4.3).
- **`freed_target`.** A differing window whose two values both point into
  free blocks (`block_at`, in-use flag 0). Seen in the HDDMap icon table
  `0x004d24c0/+0x1f/+0x20/+0x101/+0x31` ([cockpit.md](cockpit.md)): the icon
  list `0x44df00` deletes unused icons k = [M+0x10a] .. 0x8b with `0x477548`
  but leaves their pointers (es2sim's `map_icons`, sim/src/hud_draw.c,
  too); every read found is bounded by the count [M+0x10a]. CW3220's heap
  has holes from earlier frees, the own heap none, and a free block pairs
  only through a window at the same offset. Off between heaps of one
  runtime; a pointer into a free block on one side only stays a difference.

**Chance pairings (not a class).** In byte data (terrain, bitmaps, text) a
window can look like a heap address; with different heap layouts it shows
as a **pairing conflict** at a path ending in an odd offset (seen in
c1_01-collision, c3_09-loadout-b). An unaligned
conflict is noise unless it repeats. es2sim's heap
(`0x20000000..0x2077ffff`) and the original's (`0x025xxxxx..0x09bxxxxx`)
cannot pair equal bytes; unequal bytes that each look like an own-heap
pointer at the same offset still can.

### Contracts (from the bytes)

All nine are cdecl; EDX:EAX is reproduced exactly; ECX is dead after every
call site (forward walk from each return address, both arms of every
branch), so the framework's entry ECX
stands in. No function reads an uninitialized local or stack residue a
replacement would change; no entry or exit shim. Game calls go through the
guest bridge with the original's entry EAX/EDX/ECX (listed per call), so
nested hooks keep their modes.

**`pool_create` 0x471ad8 (header H, capacity, size)**, with its only callee,
the pool initializer `0x471a50` (computed by the core). Only the low words c
and s count. `IMUL` of the sign-extended words: total = c x (s + 8) in
EDX:EAX. When c ≠ 0, `new[](total)` `0x4774f0` (EAX total, EDX the
product's high dword, ECX total; `POP ECX` reloads the pushed total). Then
`memset(block, 0, total)` `0x49681a` (EAX total, ECX total or the entry ECX
when c = 0). `0x471a50` writes H+0 = c, H+2 = 0, H+4 = s (words), H+6 =
H+0Ah = the block, H+0Eh = 0, rereads H+4, stride = (s + 8) as a word, and
for k = 0 .. c−1 (signed words) node k = block + k x stride: [node] = node +
stride, [node+4] = node k−1 (0 for the first), then [last node] = 0. Returns
EAX = H, EDX = the last node (c > 0) or memset's EDX with its low word = s
(c = 0); ECX = total (`POP ECX` of the local). A negative capacity faults in
the original (memset of a negative size, or the link write at `0000FFF8h`
when s = −8): the taken edge of `0x471aa3` is PROVED unreachable. Callers:
the twelve sites of section 10; each writes EAX or ECX before reading it,
and EDX is next written by `new` (`0x47749c`), which reads no register.

**`pool_nth` 0x471cb0 (pool, index)**: k = the index word; EAX = 0; loop:
`pool_prev(pool, EAX)` `0x471c70` (first call: entry EDX/ECX; later: EDX =
the previous EDX with low word = the word before the decrement, ECX =
pool_prev's); 0 ends; else the word before the decrement is tested and k is
decremented in its own argument slot; 0 ends. Returns the object at position
index of pool_prev's order (oldest first), or 0; EDX as described, ECX
pool_prev's. The slot write is released by every caller (`ADD ESP,8`).
Callers: `subject_resolve` (three sites), group constructor `0x423b34`
(three sites).

**`subject_resolve` 0x425348 (index, scope)**: scope 0: EDX = sign-extended
index word, EAX = 122 x EDX + [`0x4a9e00`] (group array), ECX the entry's.
Scope 1/2/3: k = signed remap byte at `0x4aa78e`/`0x4aa888`/`0x4aa946` +
index (full dword; callers pass sign-extended words), then `pool_nth(pool,
k)` with pool from `0x4a9b7a`/`0x4a9db9`/`0x4a95a0` and entry registers
(EAX 0, EDX k, ECX pool) / (EAX k, EDX pool, ECX entry) / (EAX pool, EDX
entry, ECX k); returns pool_nth's registers. Other scopes: EAX = 0, EDX/ECX
the entry's. Callers: orders `0x425f9b`, objectives `0x4262fa`, trigger
subjects `0x4264dd` (trigger types 7, 8, 9, 10 → scopes 1, 2, 3, 0 via the
table at `0x426492`).

**`action_copy` 0x411b90 (A, ids, actions)**: ten times: word at ids →
A+1BAh+2i, word at actions → A+1CEh+2i (interleaved, so overlapping inputs
see earlier writes). Returns EAX = A+1CEh, EDX = ids+20, ECX = actions+20.
Callers: selected authored mechs `0x42599f`, flyers `0x425c20`, buildings
`0x425e7d`. At `0x4259a7` the caller loads CX only and passes ECX to
`initial_damage`, which reads only the low word.

**`condition_init` 0x415068 (C, first, second, third)**, C = A+206h of a MEC
actor: T = [C+0Ch], word list L = [T+4] (read once), table 1 = [C+4], table
2 = [C]. Rows i = 0..18 of table 1: for i < 13, (100 − w) x M / 100 with w
the next word of `first` and M = word 0 of row i of [T] (rows of nine words;
C+0Ch and [T] reread per row), else 0. Words 0..11 of table 2: for i < 9,
(100 − w) x L[i] / 100 with w from `second`; words 9..11 = 0. Then i =
0..9: top = word 0 of row 19+i, sum = L[12+i] + top (word), q = (100 − w) x
sum / 100 with w from `third`; q ≤ top (signed words): table 1 word 19+i =
q; else table 1 word 19+i = FFFFh and table 2 word 12+i = FFFFh when q − top
> L[12+i] (reread after that write), else q − top. Products 32-bit,
`CDQ`/`IDIV` by 100. Returns EAX the last quotient (low word = q − top on
the second path), EDX its remainder or (second path) the list pointer, ECX
the list pointer after 22 words (second `POP ECX`). The three argument
pointers advance in their own slots (released by the caller). Caller:
`0x4258de` (MEC actors).

**`region_init` 0x423358 (R, record, points)**: [R+2] = 12 x (signed word
record+2) + points; word R = word record; kind 0: [R+6] = points + 12 x
(signed word record+4), EDX that product, ECX the first value with low word
0; otherwise [R+6] = 10 x (signed word record+4) = EDX, ECX that signed
word. EAX = R. Caller: `0x4249b3` (pass 1, per region).

**`timer_init` 0x423104 (T, first, words, duration)**: [T] = first;
`timer_arm(T+2Ch, EDX)` `0x467ac4` with EDX = entry EDX's high word and the
duration word (EAX first, ECX T+2Ch); then ten dwords from words to T+4
(forward). Returns EAX = words+40, EDX = T+44, ECX = 10. Caller: `0x424c68`
(pass 1, per timer).

**`group_array` 0x423d38 (G, count)**: word G+4 = count;
`0x49667c(0, 7Ah, count, 1, 0x4240e4)` (EAX the sign-extended count;
element constructor `0x4240e4` clears word +4); [G] = its EAX. Returns
49667c's EDX:EAX. Caller: `0x424dec` (pass 1, only with groups).

**`initial_damage` 0x4178e8 (A, word)**: w = signed low word. w < 0 or w ≥
80: severity 0. 60..79: 50; 40..59: 80. 20..39: 120, and
`multiply_q8(100h, v)` `0x47e0a0` with v the signed word +0Ah of the list
[[A+212h]+4] (EAX = EDX = v, ECX the list; list pointer in local [EBP−4]),
result into word +0Ah of [A+206h] (read after the call), byte A+0B3h = 1.
0..19: 150; `rng_next(0x4d2599)` (stream A; EAX the argument word, entry
EDX/ECX; `POP ECX` reloads the stream), part 7 for an odd draw else 8
(draw's high word kept), virtual +74h (A, part, 32000, 0) with EAX = [A]
(vtable), EDX the draw's EDX, ECX the stream; then when signed type word
[[A+1F2h]+4Ah] > 2 (read after the call) a second draw (EAX the virtual's
EAX, EDX [A+1F2h], ECX the virtual's ECX) and the virtual with part 13/14
(EAX the part, EDX [A], ECX the stream; [A] reread); bytes A+0A4h, A+0B3h,
A+0B4h = 1. Always `random_damage(A, severity, severity + 25)` `0x417a04`
(EAX severity + 25; EDX/ECX: the entry's, multiply_q8's EDX and [A+206h],
or A+92h and the virtual's ECX). Returns random_damage's EDX:EAX; ECX =
local [EBP−4] (the list on 20..39, else uninitialized; caller `0x4259b0`
does not read it). Caller: `0x4259b0`, selected authored mechs only.

### In vitro (group `mission_load`)

`invitro/mission_load_cases.h`: 1,001,024 direct cases per hook (100,000
quick) against logging stubs with overlapping objects, 256 installed 31/a7
cases per hook, real-callee passes (3,072 cases); 29 branch sites, 28 both
ways, `0x471aa3` taken edge PROVED; one caught mutation per hook
(`MUTATIONS.md`); `//core:mission_load_test` 790 hand-computed checks.

### Live result

Every hook replaced, CW3220 original: all suites
identical, no nonzero skipped/bypassed/mismatch counter; the nine
`hk_<name>_calls` equal the Live counts table. After-load snapshot:
O1 = O2 with no noise
entry; O2 against R differs only by `matrix_padding` (acceptance-60s,
c1_01-collision) and `environment` (c3_09-loadout-b); no hook omits a write.

### Node matrix byte 19

**Writer.** Byte 19 of a child node's matrix is written by the copy path of
`matrix_compose` `0x47fa20` (`rep movsd` at `0x47fce8`, 20 bytes) from
`pose_parent` `0x478bc4`'s local matrix at EBP−0x20, whose byte 19
(EBP−0xD) no instruction writes (`0x47ebb8` writes bytes 0..18, mode at 18;
`pose_parent` the translation 20..31). The slot is set by the traversal
`node_matrices` `0x478c64`. With its ESP = S before pushing a call's
arguments: the child call `pose_parent(pose, parent, child)` runs with EBP
= S−0x14, so EBP−0xD = S−0x21; the root call `0x478c1c(pose, matrix)`
(parent word −1, flag 1) runs with EBP = S−0x10 and pushes EBX =
`[ebp+0Ch]`, its matrix pointer `[M+0x16]` + 32 × node, at `0x478c54`:
S−0x24..S−0x21. So every child call after a root call copies the **most
significant byte of the root node's matrix address**; before the first
root call, whatever the caller left at S−0x21. M: vtable `0x4a14c4`, node
count word +0x0c, flags +0x0e, poses +0x12, node matrices +0x16
(constructor `0x478838` sets every flag to 1, runs the traversal at
`0x478925`).

**Evidence.** In every after-load JSON with a `+0x173` difference the
original's byte equals its block's address high byte (02/03); the residue
probe `tc_residue_probe` (`track_callee_cases.h`, in
`differential_math_test`, 8192 cases) matches the model exactly and fails
without the pointer byte.

**Own code.** `es2_node_matrices_registers` (`core/src/animation.c`):
`padding` until the first root node, then the root matrix address's high
byte. The proxy hook writes the original's byte (same CW3220 heap); es2sim
writes `0x20` (its heap base; replays stay IDENTICAL), counted by
`matrix_padding` against the original. Residue table: `sim/src/residue.c`,
row `node_matrices`, rule `writer`. **Live:** O2 vs R (`node_matrices`
replaced) IDENTICAL without classes in acceptance-60s, c1_01-collision,
c3_09-loadout-b.

### Live counts

Each hook runs during the load only (0 in the DBSIM map, which predates
early arming), so its counter is constant from the first tick record on.
Counts from the bytes and the launch sets (`es2-data/oracle/launch`;
default C1_01 script with the list counts of
[mission.md](../formats/mission.md#prepared-script-exact-framing), the
selection from `c1_01-unarmed`'s script, two MEC records). `initial_damage`
equals section 13's load-time `random_damage` delta (c1_08 7 too),
`timer_init` its `timer_arm` delta:

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

(c3_09 and c3_10 have no selected flyer: FLYER pool capacity 0, no `new[]`.)

### Snapshot results

Every run: imports original, early arming, O1/O2 every hook original, R
every hook replaced, both scopes. **O2 against R is identical in
acceptance-60s, c3_10-loadout-hercs and c3_09-loadout-b in every run**
(a60 counts its 3 matrix padding bytes), also with the hooks of sections 15
to 32 added.

- **Runner environment.** A differing environment moves the heap; the
  runners restore the caller's complete environment
  ([oracle_regression.md](../reference-game/oracle_regression.md) "Runner
  environment"); the `CLASSES` environment block count shows it.
- **c1_01-collision's after-load heap varies between runs of the same mode:**
  compare only within one session on one clone.
- **c3_09 one-off:** R once had 259 differences behind the
  pointer `0x4a9cbc` (fixed region `0x4a9656/+0x666`), not reproduced by a
  bisect; a repeat needs a look at `0x4a9cbc`'s writer.
- **Own `cw3220.dll`** (`Rown` vs R, a60): 240 differences,
  all in never-written bytes, counted by the runtime classes; the live
  regressions with the own runtime are identical.

### es2sim's snapshot against the original

es2sim writes ES2SNAP1 version 2 (flags 32 "armed early", 128 "own heap",
memory map, modules) at its `before_logic` step, the original's step (the
main loop's call of logic frame `0x45f570` at `0x45f487`).
`sim/tests/replay_suite.py --suite NAME --after-load-snapshot
[--snapshot-extra 0xVA:LEN,...]` adds the proxy's `[snapshot]` section
(`after_load=1`, `path`, `extra`) to the INI and writes
`<output-dir>/NAME/es2sim-after-load.snap`; without it the INI is unchanged
(`//sim:replay_suite_test`). Replays stay identical with the snapshot on.
Two headless es2sim passes compare identical (a60, c3_09, c3_10, both
scopes) except foreign sample-block pointers and the replay's temporary
install directory (`0x004a98f4/+0x6ceb`); self-compare block counts are no
measure against the original (heap base `0x20` is ASCII space).

**Expected differences, es2sim against the original (headless):**

| What | Where | Original / es2sim | In the compare |
|---|---|---|---|
| SOS9503.DLL handle | fixed `0x6cbd58` | DLL base / `0x50500000` (not committed) | `--ignore 0x6cbd58:4` |
| 97 SOS entry slots | fixed `0x6cbd60..0x6cbee3` | entry addresses / 0 | `--ignore 0x6cbd60:0x184` |
| Win32 word | fixed `0x6cbd5c` | 1 / 1 | equal ([audio.md](audio.md)) |
| sample blocks (`GlobalAlloc(40h)`, `0x474360`) | sound cache sample slots | process heap / host CRT `calloc` | foreign pointers (about 10; 13 in c3_09) |
| heap bytes, residue, environment | see "Noise classes" | | the seven classes; header flags note "heaps of different runtimes: A cw3220, B own" |

Handle and slots lie outside the fixed regions: only a snapshot with
`extra=0x6cbd58:0x18c` holds them. Foreign memory is not walked; a
sample-block pointer showing as a difference means one side's block lies in
memory of another type. Anything else is a finding for the owner of the
block's writer.

**Procedure.** O1, O2 every hook and import original with
`ES2_AFTER_LOAD_SNAPSHOT=1`, `ES2_AFTER_LOAD_SNAPSHOT_EXTRA=0x6cbd58:0x18c`;
es2sim via `replay_suite.py --after-load-snapshot --snapshot-extra
0x6cbd58:0x18c`; `after_load_snapshot.py noise O1 O2`, then `compare O1
es2sim --noise ... --ignore 0x6cbd58:4 --ignore 0x6cbd60:0x184`, both
scopes.

**Result.** O1 = O2; O1 against es2sim
**IDENTICAL** in all three suites and both scopes, every difference
class-counted (`aim_residue` 5 per suite; `freed_target` 7/0/84 in
a60/c3_10/c3_09; 3 matrix padding bytes in a60; environment blocks 103
against 0).

### Open

- Reached H3 objects (graphics context, cameras' graphics objects) are part
  of `--scope all`; non-noise replaced-run differences there belong to the
  hooks that build them.

## 15. L8 stage-2 pool callbacks

The three pool-only callbacks of `stage_dispatch(2)` (section 4). Own code:
adapter group `mission_pools`, core in `core/src/mission_load.c`.
**Live** with the after-load snapshot (section 14).

| Hook | VA | Record | Builds |
|---|---|---:|---|
| `light_pools` | `0x4076e4` | 4 | light table `[0x4a9608]`, light pool `[0x4a95fe]` (3 × `0xe`), list `[0x4a9602]` (25) |
| `smoke_pools` | `0x4090cc` | 7 | smoke pool `[0x4a9684]` (20 × `0x2a`), list `[0x4a9688]` (25), then `0x408fb8` |
| `projectile_pools` | `0x40a060` | 10 | projectile pool `[0x4a96c2]` (160 × `0x66`), list `[0x4a96c6]` (80) |

The `SMOKE_BALL` pool `[0x4a966e]` (200 × `0x4c`) and list `[0x4a9672]` are
built by `0x408fb8`, which also loads the smoke-ball shapes (`0x474cd8`) and
bitmap (`0x45cdf4`) and stores `[0x4a967c]`: own code `smoke_balls`
(section 31).

**Call path.** Each is registered once, by `0x401d64(fn, 2)` at
`0x4078df`, `0x4094a3`, `0x40a163` (static init records 4, 7, 10); nothing
else refers to them. `0x401d94` runs stage 2 only at `0x4617b8` in the
once-per-process load (section 1). The dispatcher calls each entry via
`call [ebx]` at `0x401dac` and reads no register afterwards (`inc esi; add
ebx,6; cmp si,[0x497074]`), so the callbacks' EAX/EDX/ECX matter only as
the next callback's entry registers. Early arming installs the hooks at
`0x45f42b`, before `0x4617b8`.

### Contracts (from the bytes)

All three: no arguments, `push ebp; mov ebp,esp; add esp,-N; mov
eax,<descriptor>`, then frame setup `0x49630c`. The descriptor's second
dword gives the record offset: `0x497b16` → −`0x2c` (`light_pools`),
`0x497f5e`, `0x4982ec` → −`0x28`. `0x49630c` (reads EAX only, keeps EDX,
ECX) fills the record at EBP + offset (+0 old FS:0 head, also returned in
EAX; +4 handler `0x4967f0`; +8 descriptor; +0Ch caller's ESP; +10h..+15h
zero; +1Ch construction count `[[0x6cbee4]]`) and links it into FS:0. The
word at record+10h (EBP−1Ch or EBP−18h) is the context word C, set around
each construction. The epilogue stores record+0 to FS:0 and returns it:
**EAX = the old FS:0 head**.

Common steps, with frame slot S and image global P:

- **pool(capacity, size)**: `new(0x12)` `0x47749c` (entry registers of the
  previous step); [S] = EAX; `POP ECX` → ECX = 12h. When EAX ≠ 0: C = in,
  `pool_create([S], capacity, size)` `0x471ad8` with EAX = new's EAX, EDX =
  [S], ECX = 12h, C = out. Always ECX = [S] (reread), [P] = ECX.
- **list(capacity)**: `new(8)`; [S] = EAX; ECX = 8. When EAX ≠ 0: C = in,
  EDX = [S], `MOV AX,capacity` (high word of new's EAX kept), word [EDX+6] =
  capacity, EDX = capacity × 4 (sign-extended word), `new[](EDX)`
  `0x4774f0` (EAX, EDX, ECX = 8); `POP ECX`, ECX = [S]; dword [ECX] =
  new[]'s EAX; EAX = [S]; word [EAX+4] = 0; C = out. Always EDX = [S]
  (reread), [P] = EDX.

**`light_pools` 0x4076e4** (record EBP−2Ch, C = EBP−1Ch; pushed EBX
restored): `new(0x404)` → [EBP−4], ECX = 404h. When nonzero: C = 14h,
light-table constructor `0x406e44([EBP−4])` (EAX new's EAX, EDX the table,
ECX 404h), ECX = `[0x6cbee4]`, dword [ECX] −= 5, C = 8. Then EBX = [EBP−4]
(reread), [`0x4a9608`] = EBX, leaf `0x406ee4(EBX, 10, 2000)` (computed by
the core): [EBX+10h] = 10 SAR 5 = 0, [EBX+14h] = 2000 SAR 5 = 62, returning
EAX = EBX, EDX = 0, ECX = 62. EAX = [`0x4a9608`] (reread), dword [EAX+0Ch]
= 10000. Then pool(3, 0Eh), S = EBP−8, in/out 2Ch/20h, P = `0x4a95fe`;
list(25), S = EBP−0Ch, in/out 38h/20h, P = `0x4a9602`. Returns EAX = old
head, EDX = the list (or 0), ECX = the list, or 8 when its `new` failed. A
null light table faults in the original (`0x406ee4` writes `[0x00000010]`;
the taken edge of the `je` at `0x407705` is PROVED unreachable).

**`smoke_pools` 0x4090cc** (record EBP−28h, C = EBP−18h): pool(20, 2Ah),
S = EBP−4, in/out 14h/8, P = `0x4a9684`; list(25), S = EBP−8, in/out 20h/8,
P = `0x4a9688`; then `0x408fb8()` with EAX = EDX = ECX = the list, or (0,
0, 8) when the list's `new` failed. Returns EAX = old head, EDX/ECX =
408fb8's.

**`projectile_pools` 0x40a060** (record EBP−28h, C = EBP−18h):
pool(160, 66h), S = EBP−4, in/out 14h/8, P = `0x4a96c2`; list(80) (`new[]`
of 320 bytes), S = EBP−8, in/out 20h/8, P = `0x4a96c6`. Returns as
`light_pools`.

**Callees and modes.** Via the guest bridge with the original's entry
registers: `0x49630c` (synthetic EBP), `new` `0x47749c` and `new[]`
`0x4774f0` (H6; read no register, do not write their argument slot),
`pool_create` (group `mission_load`, own mode), `0x406e44` (array
construction `0x49667c` of 20 × 23h entries with constructor `0x407548`,
two graphics-library lights `0x48bc40`, nested leaf `0x406ee4(table, 2000,
3000)`; does not write its argument slot) and `0x408fb8` (resources). So
the `POP ECX` values are the pushed ones.

**Registers, stack and shims.** EDX:EAX exact; ECX is computed by the core
but the adapters leave the framework's entry ECX, since nothing reads it:
the dispatcher reads no register after `0x401dac`, and the next callbacks
(`0x407b54`, `0x409a34`, `0x40a818`) load EAX before `0x49630c` and set ECX
and EDX before any read (`mov ecx,4` before `rep movsd`; `new`; `0x492bec`
→ `0x492b90`, `or ecx,-1` before `repne scasb`). The bodies read only
locals they wrote; a linear walk of the three following callbacks finds each
local read after an own store or after a call given its address, so no
stack residue is read. No shim. The frame lives in a zeroed scratch at the
original's EBP offsets; the adapter writes the returned head back to FS:0.

**Compare:** refused for all three (`new`/`new[]`/`pool_create`
allocation, the light table's construction and count, `0x408fb8`'s
resources); the after-load snapshot is the acceptance.

### In vitro (group `mission_pools`)

`invitro/mission_pools_cases.h`: 1,001,024 direct cases per hook (100,000
quick) against logging stubs (record stub, overlapping `new` arena, `new[]`
over the list), FS:0 and the context word compared; 256 installed 31/a7
cases per hook; one real-callee pass per hook (2,048 cases); 7 branch
sites, 6 both ways, `0x407705` taken edge PROVED; one caught mutation per
hook (`MUTATIONS.md`); `//core:mission_load_test` 1,040 checks (250 for
these hooks).

### Live

Each hook runs **once per suite** (stage 2 once per process, every
allocation succeeds); the DBSIM map (before early arming) reads 0.
Counters in every suite:

| Hook | `_calls` | `_call_47749c` | other `_call_*`, 1 each |
|---|---:|---:|---|
| `hk_light_pools` | 1 | 3 | `49630c`, `406e44`, `471ad8`, `4774f0` |
| `hk_smoke_pools` | 1 | 2 | `49630c`, `471ad8`, `4774f0`, `408fb8` |
| `hk_projectile_pools` | 1 | 2 | `49630c`, `471ad8`, `4774f0` |

`pool_create` stays 12 per suite and `hk_pool_create_call_4774f0` as in
section 14; every `*_skipped`/`*_bypassed` 0.

**Result.** Every hook replaced, `rng_next`
in `compare`, the 49 shim imports replaced: all 40 suites
identical, counters as in the table. After-load snapshot: identical in
all four suites, both scopes, including the blocks these hooks write (from
`0x4a95fe`, `0x4a9602`, `0x4a9608`, `0x4a9684`, `0x4a9688`, `0x4a96c2`,
`0x4a96c6`: pools, node links, list slot arrays, the light table).

## 16. L4 weapon fit

L4 of section 10, from the pinned listing (dispatch tables and exception
descriptors read from the file). Adapter group `mission_weapons`, core in
`core/src/mission_load.c`. Live and by after-load snapshot (section 14).

| Hook | VA | Role |
|---|---|---|
| `weapon_fit` | `0x4175dc` | weapon fit of one constructed mech (pass 2, `0x4258a2`) |
| `player_weapons` | `0x4104ec` | player weapon system: the factory, then three fire groups |
| `weapon_factory` | `0x40fff8` | weapon set of a vehicle: one weapon object per mount |

Behind the bridge with their own modes: the hooks `actor_animate`,
`mount_offset`, `fire_group`, `shield_capacity`, `weapon_record`,
`def_lookup`, `proj_record`; the original `0x420634`, `0x40e61c`,
`0x417530`, `0x40cec0`, `0x40d240`, `0x40d128`, `0x40cb84`, `0x40d284`,
`0x40fb2c`, `0x413ac8`, `0x417d08` (section 32 makes six of them own
code), the seven weapon constructors `0x40e074` … `0x40e380`, the allocator
`0x47a2c8` and the runtime (`0x49630c`, `new` `0x47749c`, `new[]`
`0x4774f0`, `0x49667c`, `0x474bbc`/`0x474be4`).

### Call path

`0x4175dc`: one call site, `0x4258a2` in the actor load (section 3 step 4),
once per mech the pass constructs (selected authored mechs and MEC actors).
`0x4104ec`: only from `0x417634` (player branch of `0x4175dc`). `0x40fff8`:
from `0x41050e` (in `0x4104ec`) and `0x417684`. Early arming (section 13)
installs them at `0x45f42b`, so counters are constant from the first tick
record; the late-arming DBSIM map reads 0 for all three.

### Contracts (from the bytes)

All cdecl; EDX:EAX exact. ECX is dead at every call site (forward walk,
both arms): after `0x4258a2` both paths write ECX (`0x4258b9`, `0x42590e`,
`0x42592c`, `0x425961`/`0x42596c`) and EAX/EDX before reading; after
`0x417634`/`0x417684` ECX is reloaded from `[0x6cbee4]`, EAX from the frame,
and EDX reaches `actor_animate` as its entry EDX (reproduced); after
`0x41050e` EAX/EDX are reloaded and ECX enters `0x49667c`, which reserves
`[EBP-4]` by `PUSH ECX` and overwrites it (`0x496694`) before any read; its
element constructor `0x411892` loads ECX before use. The framework returns
the entry ECX. Game calls use the original's entry EAX/EDX/ECX. Every `POP
ECX` after a call reloads the pushed value (no callee writes its argument
slot; checked for `0x420634`, `new`, `new[]`, `0x474bbc`, `weapon_record`,
`proj_record`, `0x40f540`, `0x40e61c`, `0x417bec`, `0x413ac8`, `0x417d08`).
`[0x6cbee4]` is the runtime's construction-count pointer; "count ±n" means
`[[0x6cbee4]] ± n`, the pointer reread each time.

**`weapon_factory` 0x40fff8 (W, A, count, mounts, items, secondary,
word)**, seven slots. `add esp,-7Ch`; descriptor `0x499055`, runtime record
EBP−44h (context word EBP−34h); `0x49630c` (EAX = descriptor, entry
EDX/ECX). [W] = vtable `0x499338`, [W+4] = 0, count +1 (ECX the pointer),
word [W+8] = count word (`MOV BX`: EBX keeps the caller's high word),
`new[](sx(count) × 4)` (EAX size, EDX 0, ECX counter pointer; `POP ECX`),
[W+4] = block; EBP−4Ch = block (cursor), EBP−48h = count word (ECX = size
with low word replaced); `0x474bbc(200000)` (EAX = W+4, EDX = block), ECX
= 200000. Loop `0x4104a4`: DX = count word, decremented in place; cursor
+4 when DX ≠ 0; weapon slot EBP−50h = old cursor or 0, 0 ends the loop
(unsigned: 65,535 iterations for −1, past the small block; game counts are
small). Per mount ESI (stride 1Ah, first not advanced): i = signed byte
[mount+17h]; BX = items word i; secondary word i into EBP−52h (EAX = i with
that low word); negative item → EBX = 0; item 0 stores 0 in the slot.
Otherwise `weapon_record(EBX)` (EAX = i/secondary word, EDX = secondary;
pushed dword carries EBX's high word; `POP ECX` = EBX), EDI = record C, AX
= selector [C+3Eh]: 22h → secondary word (5 → 0, written back) to
`def_lookup(0, word)` (EDX = word); 21h → nothing; else
`proj_record(selector)` (`POP ECX` = pushed EAX); result into the
projectile local EBP−58h. Then EAX = sx(BX), ECX = heap `[0x6c5830]` into
EBP−5Ch; IDs above 20h leave the slot unwritten; else byte table
`0x4100fd` (33 entries) picks one of ten blocks via dword table `0x41011e`
([weapon_classes.md](weapon_classes.md)). Each block: `0x47a2c8(size,
heap)` into its frame slot (EAX = class byte), if non-null context `in`,
constructor, context `out`; object or 0 into the weapon slot.

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

Allocator registers: EDX = ECX = heap (classes 1–5, 8, 9; EDX reread from
EBP−5Ch), or ECX = heap, EDX unchanged (6, 7). Constructor (EAX, EDX, ECX)
= (A, W', proj) for 5/8, (A, W', mount) for 6/7 (reread from staging),
(W', word, A) for equipment. Last slot = word arg `[EBP+20h]` with the high
word of EDX (5, 8, equipment) or EBX (6, 7, `MOV BX`, kept in EBX for later
mounts). After the null test the slot is stored from EAX (5, 8) or ECX
(others), reread from the frame slot. End: `0x474be4()` (EAX 0, EDX low
word 0), count +1 (EDX the pointer), ECX = saved FS:0 head from the record
(epilogue stores it to FS:0); EAX = W. No callee reads the word argument
or EBX's high word: the constructors read at most `[EBP+18h]` (projectile
record; `0x40df30` stores it at W'+20h); `weapon_record` reads only the
low word (`MOVSX`).

Trap: EBP−58h is written only when the selector is not 21h, and classes
5–8 pass it on, so a 21h weapon passes the previous mount's value (or
frame residue). In the shipped `dat\WEAPONS.DAT` (SIMVOL0,
[dat.md](../formats/dat.md)) only ID 18 (ECM, class 9)
has selector 21h and class 9 does not pass it, so the residue is never
consumed. The core keeps the local
in the scratch frame; the adapter's zeroed scratch stands in for the
residue.

**`player_weapons` 0x4104ec (P, A, count, mounts, items, secondary)**:
`PUSH ECX` reserves [EBP−4] = entry ECX. `weapon_factory(P, A, count with
secondary's high word, mounts, items, secondary, 1)` (EAX = that count
dword, EDX = A, ECX = P). [P] = vtable `0x499238`; `0x49667c(P+1Fh, 6, 3,
1, 0x411892)` (EAX = P, EDX = P+1Fh, ECX the factory's): three 6-byte
groups; the element constructor clears the dword, count +1. For k = 0..2:
word [P+1Fh+6k+4] = count word (`MOV SI`), `new[](2 × sx(count))` (EAX
size; `POP ECX`) into [P+1Fh+6k]. Byte [P+1Dh] = FFh. For b = 0 ..
sx(count)−1 (signed): weapon [[P+4]+4b], null skips; record index word
[[W+1Ch]+56h] (EDX keeps C's high word): IDs 12h, 1Dh, 1Eh, 1Fh, 20h (ECM
and the four equipment classes) set word [EBP−2] = FFFFh; others 0, and the
first of them (signed [P+1Dh] < 0) sets [P+1Dh] = b. For k = 0..2: CX = 1,
`DEC ECX` (32-bit) unless k = [EBP−2]; word [EBP−4] = b; word
[[P+1Fh+6k] + 2b] = CX. So weapons are in fire group 0, equipment in none.
ECX before that loop: P (weapons) or sx(b) (equipment). Then
`fire_group(P, 0)` (EAX = sx(count), EDX = P), byte [P+16h] = 1, count +1.
EAX = P, EDX = counter pointer, ECX = [EBP−4] (entry ECX, or its words
replaced by the last non-null weapon's index and flag).

**`weapon_fit` 0x4175dc (A, items, secondary)**: `add esp,-12Ch`;
descriptor `0x49a0c0`, record EBP−28h (context word EBP−18h); `0x49630c`
(EAX = descriptor). `0x420634(A)` → mount table M (count word, mounts
pointer at +2); EAX = A+92h, ECX = A (`POP ECX`). Player byte [A+0A3h]:
set → `new(35h)` (EAX = A+92h, ECX = A; `POP ECX`) into EBP−4, if non-null
context 14h, `player_weapons(obj, A, count word with new's high word,
[M+2], items, secondary)` (EAX = that dword, EDX = obj, ECX = [M+2]; [M+2]
read before [M]), count −6, context 8; clear → `new(14h)` into EBP−8,
context 2Ch, `weapon_factory(obj, A, …, 0)`, count −2, context 20h.
[A+202h] = object reread from its slot. A null object faults later
(`0x4177d0` reads `[00000008h]`): the taken edges of `0x41761a` and
`0x417668` are PROVED unreachable. `actor_animate(A, 0)`. For i while i <
sx([[A+202h]+8]) ([A+202h] reread each time): W = [[[A+202h]+4] + 4i],
null skips; C = [W+1Ch] into EBP−2Ch, C+12h into EBP−30h, C into EBP−34h
(EBP−2Ch reread); `mount_offset(W)` (EAX = C, EDX = C+12h, ECX = C; `POP
ECX`) into EBP−38h; b = signed [[W+18h]+17h] gives words b+13h (EBP−3Ah)
and b+0Ch (EBP−3Ch), [W+18h] read for each; word [C+2Ah] into EBP−3Eh. If
[W+10h] ≠ 0: `0x40e61c(W)` (`POP ECX`), AX into EBP−40h, shape virtual
+20h of [[A+34h]+4] with that word (EAX = vtable, EDX = [A+34h], ECX =
object), then `0x417530([W+10h], word [result+4])`. Then word [C+18h] =
b+13h, word [[C+8]+2] = b+0Ch, byte [C+4] = byte +4 of row b+13h of the
part table [[A+1FAh]] (18-byte rows); `0x40cec0([A+1F6h], EBP−30h,
EBP−38h, 1)` (EAX = EBP−38h, EDX = EBP−30h), `0x40d240([A+1FAh], EBP−3Ah,
EBP−34h, 1)` (EAX = EBP−34h, EDX = EBP−3Ah), `0x40d128([A+1FAh], EBP−3Ch,
EBP−3Eh, 1)` (EAX = EBP−3Eh, EDX = EBP−3Ch); ECX = first argument each
time. After the loop `0x40cb84([A+1F6h], EBP−12Ch, EBP−0F0h, EBP−0B4h)`
(EAX = EBP−0F0h, EDX = EBP−12Ch) fills three buffers (30 words, 30 words,
29 dwords) and returns their entry count n; `0x40d284([A+1FAh], n,
EBP−12Ch, EBP−0F0h, EBP−0B4h)` (EAX = [A+1FAh], EDX = EBP−12Ch, ECX =
EBP−0F0h) reads n entries of each (entries written before read). For k =
0..28: word [[A+20Eh]+2k] = 0 when word +6 of part row k is 0 (both
pointers reread per row). Then `0x40fb2c([A+202h], A+307h)` (ECX = 9 × 28
or [A+20Eh] from the last row), `shield_capacity(A)`, `0x413ac8(A+222h)`
(`POP ECX`), `0x417d08(A)` (ECX = A+222h). EAX = saved FS:0 head
(EBP−28h), EDX = `0x417d08`'s, ECX = A.

**Frames and shims.** The two framed functions run with the synthetic EBP
of a zeroed scratch (`ES2_WEAPON_FIT_FRAME`, `ES2_WEAPON_FACTORY_FRAME`);
locals sit at the original offsets because guest callees receive their
addresses (`0x40cec0`, `0x40d240`, `0x40d128`, `0x40cb84`, `0x40d284`);
the adapters write the saved head back to FS:0 (`weapon_fit`: EAX,
`weapon_factory`: ECX). Apart from the factory's projectile local no
function reads an uninitialized local; `player_weapons` keeps [EBP−4] in
the core. No entry or exit shim. The framework passes six slots: the
factory's seventh slot and its caller's EBX high word reach only unread
slots; the adapter rebuilds both as the two callers have them (word = 1
exactly when [A+0A3h] is set; EBX = A in both). The actor load's later
callees might read stack residue below its frame that the replaced frames
leave differently; none is known (the live regression is the check).

**Compare:** refused for all three (allocation, constructors, construction
count, runtime record); the after-load snapshot is the acceptance.

### In vitro (group `mission_weapons`)

`invitro/mission_weapons_cases.h`, per hook: 1,001,024 direct cases full
tier (100,000 quick) against logging stubs (35 stub ids; shape virtual via
an arena vtable); original with seven slots, EBX and a case-pattern stack
(the factory's residue), core with the same. Compared: pair, ECX, FS:0,
ordered calls with slots, entry registers, context word, the frame words
the part helpers' pointers name, arena, `0x6cbee4`/`0x6c5830`. Stubs
change what the originals reread (after `actor_animate`, `mount_offset`,
`0x40cec0`, `weapon_record`, the constructors) and objects overlap. Even
cases (EBX = A, word from the player byte, residue 0) also compare the
adapter with callee counter deltas. 256 installed 31/a7 cases. Real-callee
passes (2,048): real `0x49630c`, `0x49667c`, `0x411892`, nested
`player_weapons`/`weapon_factory` in rotating modes, a zeroing bump
allocator for `new`/`new[]`/`0x47a2c8`; values carrying the factory's EBX
high word compare as their low word when it runs original under a
replaced caller. 37 branch sites, 35 both ways, taken edges of
`0x41761a`/`0x417668` PROVED. One caught mutation per hook
(`MUTATIONS.md`). `//core:mission_load_test`: player fire groups; factory
with an energy weapon, a negative item, an ELF reusing the previous
projectile record, an ECM with a failed allocation, an ID above 20h; fit
of an AI mech and of the player.

### Live

Every hook replaced, 49 shim imports
replaced: **all suites identical**, DBSIM exit state
equal to the golden in all of them, every skipped/bypassed/mismatch counter 0.
In every suite `hk_weapon_fit_calls` = `hk_weapon_factory_calls` =
`hk_initial_damage_calls` + `hk_condition_init_calls` (= `mech_ctor` of
section 17), `hk_player_weapons_calls` = `hk_weapon_fit_call_4104ec` = 1,
`hk_weapon_fit_call_40fff8` = calls − 1. So only the
player has A+0A3h set before its fit (pool_create clears the MECH pool,
pass 2 sets the byte only for the player).

Per call: `weapon_fit` calls `49630c`, `420634`, `47749c`, `402604`,
`40cb84`, `40d284`, `40fb2c`, `417bec`, `413ac8`, `417d08` once;
`weapon_factory` `49630c`, `4774f0`, `474bbc`, `474be4` once;
`player_weapons` `40fff8`, `49667c`, `410ae4` once and `4774f0` three
times. Per-weapon counters (`weapon_fit`: `40f540`, `40cec0`, `40d240`,
`40d128`, and with W+10h set `40e61c`, `slot_20`, `417530`;
`weapon_factory`: `40fe84`, `40ffc8`/`40ffb0`, `47a2c8`, constructors)
depend on mount tables and loadouts. The counters of `actor_animate`,
`mount_offset`, `fire_group`, `shield_capacity`, `weapon_record`,
`def_lookup`, `proj_record` are unchanged.

Snapshot reach: an actor's weapon system [A+202h] (system, slot array,
weapon objects, the three fire-group arrays at system+1Fh), the weapon
records (`[0x4a98ec]`: word +18h, [C+8]+2, byte +4), condition words
[A+20Eh], and block pairing (same sizes, same order, same `new`, `new[]`,
`0x47a2c8`).

### Open

- The per-weapon callee counts per suite (mount tables and loadouts).
## 17. L5 constructors

L5 of section 10, from the pinned listing, decoded afresh where the linear
sweep is misaligned (`0x4053bc`, `0x413a90`); exception descriptors, case
byte table `0x405366` and jump table `0x4053a4` read from the file. Adapter
group `mission_ctors`, core in `core/src/mission_load.c`. Live and by
after-load snapshot (section 14).

| Hook | VA | Call site in `0x4253d8` | Builds |
|---|---|---|---|
| `mech_ctor` | `0x415bb0` | `0x4256a4`, per constructed mech (authored and MEC) | the MECH object in its pool node |
| `flyer_ctor` | `0x4215f4` | `0x425a71`, per selected flyer | the FLYER object |
| `building_factory` | `0x405314` | `0x425c90`, per selected building | the building-pool object of the record's class |
| `group_ctor` | `0x423b34` | `0x4261e5`, per script group | the runtime group, its member array, the members' placement |
| `base_marks` | `0x405db0` | `0x426231`, per base group | the terrain marks of the base layout |

Computed in the core (not called when the hooks are replaced): type-index
leaves `0x41ff00` (MECH, byte table `0x4a9d08`) and `0x422e80` (FLYER,
`0x4a9dcc`), actor-list append `0x411dd4` (only these constructors call
it), energy-record leaf `0x413a90` (only `mech_ctor`). Game code behind the
bridge: shape set-ups `0x4033a4`/`0x40332c`/`0x403368`
(`TS_OBJ`/`ANIM_OBJ`/`DET_ANIM_OBJ`, `object_init`), part tables
`0x40d2cc`/`0x40d354`/`0x40d160`/`0x40cd88`, name builder `0x492bec`,
animation tracks `0x402374`, join methods `0x417aa8`/`0x421ee8`/`0x405c3c`,
terrain marks `0x47136c`, runtime (`0x49630c`, new[] `0x4774f0`,
`0x49667c`, memset `0x49681a`), and the hooks `pool_alloc`,
`object_init`, `script_start`, `euler_matrix`, `matrix_transpose`,
`rotate_point`, `track_rate`, `track_seek`, `pool_nth`.

### Call path

Each function has its one call site above and no other reference (no file
dword holds its address). Early arming installs them at `0x45f42b`; the
late-arming DBSIM map reads 0 for all five.

### Who places MEC actors (section 12, answered)

**The group constructor of group 0, through the MECH join method.** The
actor load gives every MEC actor the defaults position (0, 0, −32768)
(statics `0x49a871..79`) and angle words (0x8000, 0x8000, 0)
(`0x49a87e..82`) (section 3). It rewrites group 0's 20 roster slots before
construction (`0x426024..0x42604f`): slot k < N = remap index of MEC actor
k (`0x4aa78e` + M + k), others −1. `group_ctor` resolves each non-negative
slot with `pool_nth` in the class's pool and calls the join method
(member, group, k). The MECH join `0x417aa8` (FLYER `0x421ee8` and building
`0x405c3c` alike) stores the group at A+45h and the slot word at A+49h; if
angle word A+0Ch is still 0x8000 the angles become (0, 0, G+0Ah), the
group's heading word; if height A+2Eh is still −32768 the position becomes
the group's point G+18h or, without one, `route_point(G+4, 0)` (first
point of the first order's route, stored by `group_ctor` at G+6), z =
`terrain_height` there + type word +16h (+5000 for a type with word +50h),
moved by the formation slot virtual +78h (`formation_mech` `0x417898`) for
slot k, stored at A+26h. The height fix `0x4618bc` (section 1) later
recomputes z; so actors are placed during the load, before the first
frame. The actor load builds the group's heading word (angle of record
word +2Eh, else `heading_point` of the first two route points of the first
order, else 0) and point (coordinate of record word +2Ch, else none).

**The same placement serves the authored actors.** In all 17 launch sets every selected authored mech and
flyer has coordinate and angle index −1 (record words +3Eh/+40h for mechs,
+28h/+2Ah for flyers), so all start at the defaults and are placed by
their group; group 0's class word is 0 in every set.

### Contracts (from the bytes)

All cdecl; EDX:EAX exact; ECX dead after every call site (both arms from
`0x4256a9`, `0x425a76`, `0x425c95`, `0x4261ea`, `0x426236`; after
`0x4261ea` only CX is read, `TEST CX,CX` after `MOV CX` at `0x426237`), so
the entry ECX stands in. Game calls use the original's entry EAX/EDX/ECX
(the core follows them step by step). No callee writes an argument slot
that is read back (`0x49667c` decrements its count slot; callers release
it with `ADD ESP`). No function or guest callee reads an unwritten local,
apart from the runtime records' heads and the stream object at EBP−30h
that `0x40d160`/`0x40cd88` construct through `0x472f14`. "Count ±n" as in section 16.

**`mech_ctor` 0x415bb0 (A, type)**, `add esp,-58h`: descriptor
`0x49a080`, record EBP−24h (context word EBP−14h); `0x49630c` (EAX
descriptor, entry EDX/ECX). Only the type word counts: i = byte
[`0x4a9d08` + sx(type)], T = [`0x4a9d64`] + 21Eh × i (both reread for the
second lookup at `0x415cce`). In order:

1. `0x4033a4(A, T+0DEh)`: EAX = T+0DEh, EDX = i, ECX = the dword pushed
   with the type word (`MOV SI,[EBP+0Ch]; PUSH ESI; …; POP ECX`; high word
   = caller's ESI). `0x4033a4` passes ECX to `0x402278`, which keeps it
   through `0x49630c` and clears it (`0x402293`) before any read: dead.
2. [A] = `ACTOR` `0x49a54c`, EBP−28h = A+4Dh (dead), word +1A8h = FFFFh,
   bytes +98h, +0A7h = 1; actor list `0x4a9af8` (`0x411dd4`: [L] at signed
   count word L+6 = A, count reread, incremented, stored at A+4Bh; EAX =
   ECX = first count's sign extension with the reread low word, EDX = A);
   dwords +1B6h, +1B2h = 0; words +1BEh, +1BCh, +1BAh = FFFFh; count +1;
   [A] = `MECH` `0x49a282`.
3. `0x40d2cc(A+206h, 1Dh, 16h)` (EAX = −1, EDX = counter pointer, ECX =
   the append's); `0x49667c(A+258h, 3, 5, 1, 0x41c03e)` (EAX = A+23Eh, EDX
   = A+258h).
4. Dead stores EBP−2Ch..−3Ch; words +29Ch = 0, +4 = 7; second lookup into
   [A+1F2h] (ECX = 0); words +29Ah, +298h, +296h, +294h, +290h, +28Eh,
   +28Ch = 0, +292h = 10000, +1A8h = 0. `0x413a90(A+222h, word T+0C0h)`
   (computed): words +228h = +22Ah = w, +226h = 200h, +224h = +222h =
   sx(w) SAR 1; EAX = A+222h, EDX = A+222h's high word : w, ECX = sx(w)
   SAR 1. Words +2AAh, +28Eh, +252h = 0; if word T+54h = 0, byte +0B3h = 1
   (via EBP−40h; ECX = A+92h).
5. Tracks of signed type words +12h, +1Ch, +24h: negative stores 0 (and
   clears EAX, EAX, ECX respectively), else `0x402374(A, word)` (word in
   the low half of EDX, EDX, EAX; others as left) into +22Ch, +230h,
   +234h. After the first, word +6 of the shape nodes of type words +12h
   and +14h is cleared ([[[[A+34h]+4]+2Ah]+6] + 4 × word, chain reread
   each).
6. Flat objects: count word +23Ch counts the non-negative bytes from T+72h
   (reread, incremented in place); if nonzero, new[](4 × count) (EAX =
   count, EDX = terminating byte + 1, ECX = size) into +238h; per k (DI,
   signed word compare with the reread count): `pool_alloc([0x4a968d])`
   into EBP−4 (copy EBP−44h; `POP ECX` = pool); null faults at `0x415efb`
   (PROVED edge of `0x415e8b`); context 14h, `object_init(obj,
   [[0x4a9698] + 4 × byte])` (EAX = table, EDX = shape, ECX = obj), vtable
   `0x49a262`, word +4 = 9, count +1 −4, context 8; [array + 4k] = obj,
   reread, [obj+2Eh] = −100000.
7. AI state via `script_start(A+4Dh, state)`: player (byte A+0A3h)
   `0x499420` ("player fly", type word +50h ≠ 0) or `0x4993e2` ("player"),
   else `0x4993a4` ("deciding"); per-branch registers are in the core.
8. Words +29Eh, +23Eh = 0; name [[`0x4a9d60`] + 4 × sx(word
   [A+1F2h]+6Ch)] with suffix [`0x4a0a04`] to `0x492bec`, buffer to
   `0x40d160` → +1FAh; with [`0x4a0a08`] to `0x492bec` and `0x40cd88` →
   +1F6h; `0x40d354(A+206h, A, [A+1FAh], the +1F6h value)`.
9. Bytes +2B7h..+2B4h = 0, +2B8h = 1; `memset(A+2B9h, 0, 4Eh)`; +2D9h =
   A+0Ch; if word A+32h = 0 (always: `0x402278` clears it)
   `euler_matrix(A+0Ch, A+12h)`, word +32h = 1; matrix A+12h (five dwords,
   `REP MOVSD`) and translation A+26h (+8, +0, +4) copied to A+2DDh,
   `matrix_transpose(A+2DDh)`, copied translation negated (+8, +0, +4) and
   staged at EBP−58h, `rotate_point(EBP−58h, A+2DDh, A+2F1h)` (EAX/ECX the
   staged +8/+4 dwords, EDX = EBP−58h).
10. +2BDh = 1000, word +2D7h = word +290h, word +31Ch = 0, count +1. EAX =
    A, EDX = counter pointer, ECX = saved FS:0 head (the caller restores
    it). The caller subtracts 7 from the count (`0x4256b1`).

**`flyer_ctor` 0x4215f4 (F, type)**, `add esp,-0Ch` (the vector), no
runtime record. i = byte [`0x4a9dcc` + sx(type)], T = [`0x4a9df4`] + 70h ×
i (both reread at `0x4216b7`). `0x403368(F, [T+36h])` (EAX = shape, EDX =
14 × i, ECX = [`0x4a9df4`]); `ACTOR`, +1A8h = FFFFh, bytes +98h/+0A7h,
actor list, +1B6h/+1B2h = 0, +1BEh/+1BCh/+1BAh = FFFFh, count +1, `FLYER`
`0x49a5e0`; `0x40d2cc(F+200h, 1, 1)` (EAX = −1, EDX = counter pointer, ECX
= the append's); word +4 = 6; second lookup (EDX = F+267h) into +1FAh;
+1A8h = 2, +235h = +231h = 0, +233h = word T+4; `0x40d354(F+200h, F,
[T+2Eh], [T+32h])` (EAX = [T+2Eh], EDX = F+200h, ECX = [F+1FAh]; [F+1FAh]
read before each field); `script_start(F+4Dh, 0x499cf8)` ("deciding";
ECX = F+4Dh); `memset(F+243h, 0, 4Eh)`; +263h = F+0Ch; matrix part as
`mech_ctor` step 9 at F+267h, vector at EBP−0Ch; +247h = 1000, word +261h
= 200h, count +1. EAX = F, EDX/ECX = `rotate_point`'s.

**`building_factory` 0x405314 (record, byte)**, `add esp,-0A4h`:
descriptor `0x497556`, record EBP−34h (context word EBP−24h). The second
slot (byte from `0x4aa9d2`) is never read. T = [`0x4a959c`] + 3Ch ×
sx(type word); EBP−38h = [`0x4a9574`] (first shape table), ESI =
[`0x4a957c`] (second). Types 0..3Dh select a case via byte table
`0x405366` (62 bytes) and jump table `0x4053a4`; above 3Dh (unsigned, so
also negative) case 0.

| Case | Code | Classes after `ACTOR` | Slot | Shape (table, set-up) | Context in/out | Count after the BASE part |
|---|---|---|---|---|---|---|
| 5 | `0x4053bc` | `BASE`, `RADAR_BASE` `0x4979d4` | EBP−4 | first, `0x40332c` | 14h/8 | +1 −8; bytes +96h, +0A5h = 1 (also on the null path, which faults) |
| 3 | `0x4054ea` | `BASE`, `GUN_BASE` `0x4978ac` | EBP−8 | first, `0x40332c` | 2Ch/20h | +1 −8 |
| 2 | `0x40560d` | `BASE` | EBP−0Ch | second, `0x403368` | 44h/38h | −7; byte +0A5h = 1 (faults when null) |
| 1 | `0x40572d` | `BASE`, `GUN_BASE`, `VEHICLE` `0x497818` (word +4 = 0Ah, +1A8h = 3) | EBP−10h | second, `0x403368` | 5Ch/50h | +1 +1 −9 |
| 4 | `0x405877` | `BASE`, `LC_BASE` `0x497784`, `0x49667c(obj+209h, 1Fh, 3, 1, 0x4069c5)` (EAX = counter pointer, EDX = obj, ECX = obj+209h) | EBP−14h | second, `0x403368` | 74h/68h | +1 −8 |
| 0 | `0x4059ca` | none: types 10, 53, 54, above 3Dh; EDI stays the caller's | — | — | — | — |

Each class case: `pool_alloc([0x4a95a0])` (pool in EAX for cases 5, 2, 4,
EDX for 3, ECX for 1; EAX otherwise the case byte, EDX the first table,
ECX the entry's); shape of type word +2 from its table; set-up (EAX =
sx(word) for case 5, else the shape; EDX = table for 5, else object; ECX =
object for 5, else shape); the `ACTOR`/`BASE` part (`ACTOR`, +1A8h =
FFFFh, +98h/+0A7h via two dead locals, actor list, +1B6h/+1B2h = 0,
+1BEh/+1BCh/+1BAh = FFFFh, count +1, `BASE`, word +4 = 5, +1A8h = 1, count
+1). A null object faults at its first access (PROVED edges `0x4053cf`,
`0x4054fe`, `0x405624`, `0x405745`, `0x405894`). Tail (object EDI): for i
= 0, 1 while i < sx(word T+6), `0x402374(obj, i)` into obj+1F9h + 4i
(EAX/EDX first T+20h and obj+1F9h, ECX = sx(T+6)), `track_rate(track,
word T+20h+2i)` (word in the low half of `0x402374`'s ECX; EAX = slot
address), for i = 1 `track_seek(track, 1, 6000)`; if word T+1Eh ≠ 0, byte
+0B7h = 1; new[](2 × sx(T+12h)) into +201h, every word 1; `0x49667c(0,
0Bh, sx(T+12h), 1, 0x4069e5)` into +205h (part records); per part i
(configuration [T+14h], stride 1Eh; records stride 0Bh) word +5 = 0 and
word +0 from the record's word +32h (w): negative → 0; 0 → configuration
word +0 (and frame word 1 at [[obj+34h]+8] + 2 × configuration word +2 if
that is non-negative); positive → (100 − w) × configuration word / 100
(low dword of the product, `CDQ`, `IDIV`). If w = 0: bytes +99h = 1, +96h
= 0 and, for non-negative type word +4, [[obj+34h]+4] = [[`0x4a9584`] + 4 ×
that word] (the model). Word +1F7h = word T+26h, +1F2h = T. EAX = object,
EDX = saved FS:0 head (caller restores), ECX = the record or
[`0x4a9584`].

**`group_ctor` 0x423b34 (G, R, trigger, orders, heading, point)**, `add
esp,-10h` (roster cursor, member counters and slot words; reach no
callee). Word G = class word R+28h, byte G+12h = R+6Eh, word G+0Ah =
heading (low word of slot 5), word G+2 = R+2Ah, word G+10h = 0, G+14h =
trigger, G+18h = point, G+6Ch = 0, byte G+13h = 0, `memset(G+70h, 0, 0Ah)`
(EAX = point, EDX = 0, ECX = G+70h); ten words R+72h.. to G+1Ch.. and
R+86h.. to G+30h.., interleaved; the ten order pointers to G+44h; if the
first is not null, G+6 = its route (+8), word G+4 = 0. Member count G+10h
counts the non-negative roster words R+32h..R+58h; `new[](4 × count)` into
G+0Ch (EAX = 20, EDX = R+58h, ECX = size). Class (`SUB 1; JB`, `JE`, `DEC;
JE`): 0 MECH pool [`0x4a9b7a`] and `0x417aa8`, 1 FLYER pool [`0x4a9db9`]
and `0x421ee8`, 2 building pool [`0x4a95a0`] and `0x405c3c`; other classes
build no member. Per slot k = 0..19 (DI) with a non-negative word:
`pool_nth(pool, word)` (EAX and EDX keep their high words and take the
slot word, ECX = pool), [[G+0Ch] + 4 × count] = object, count +1, join
`(object, G, k)` (EAX = object, EDX = count before the increment, ECX =
[G+0Ch]); the building join also gets sx(word R) (EDX = R, ECX = that).
EAX/EDX/ECX are those of the last step.

**`base_marks` 0x405db0 (B)**: layout record L = [`0x4a95ac`] + 1Ch ×
sx(word [[B+45h]+2]) (the building's group's word +2 = record word +2Ah);
if [L+8] = −1, EAX = L, EDX = B, ECX = −1; else
`0x47136c([0x4a0be0], B+26h, [L+8], [L+18h], [L+14h])` (EAX = terrain, EDX
= B+26h, ECX = [L+8]; reads +8, +14h, +18h, then the terrain) and its
registers. B = 0 (a base group without members) faults; the game data has
none.

**Frames, invisible registers, shims.** `mech_ctor` and
`building_factory` run on the synthetic EBP of a zeroed scratch
(`ES2_MECH_CTOR_FRAME`, `ES2_BUILDING_FACTORY_FRAME`; `0x49630c` through
`weapons_impact_guest_call`); their adapters write the saved head back to
FS:0 (`mech_ctor`: ECX, `building_factory`: EDX); `flyer_ctor`'s scratch
holds the vector `rotate_point` reads. Two values come from callers'
callee-saved registers that an adapter cannot see: (a) `mech_ctor`'s
pushed type dword carries the caller's ESI high word (previous mech of
the same pool block, or the actor load's entry ESI for the first);
`0x41ff00` reads only the low word and the dword reaches only
`0x4033a4`'s dead entry ECX; the adapter passes ESI = A. (b)
`building_factory` case 0 builds no object and returns the caller's EDI
(in the actor load the selection-flag pointer `0x4aa8ba` + i) as the
"object"; no launch set has such a type (all 17: cases 1 to 5 only); the
adapter passes EDI = 0, so a replaced case 0 faults at its first object
access instead of writing into the selection tables. The cores take both
values as arguments, so in-vitro direct cases reproduce the originals
exactly. No entry or exit shim.

**Compare:** refused for all five (allocation, constructors, construction
count, runtime record, game calls that write actors and terrain); the
after-load snapshot is the acceptance.

### In vitro (group `mission_ctors`)

`invitro/mission_ctors_cases.h`, per hook: 100,000 direct cases quick tier
(1,001,024 in the full tier) against logging stubs (25 stub ids; memset
is the harness's; the four computed leaves run as original code inside
the original); original with six slots, EAX/EDX/ECX, EBX/ESI/EDI and a
case-pattern stack, core with the same ESI (`mech_ctor`) and EDI
(factory). Compared: pair, ECX, FS:0, ordered calls with slots and slot
count, entry registers (stack addresses masked), context word, the
`rotate_point` vector by content, the 16 KiB arena and the image globals
the functions touch. Stubs change what the originals reread; the actor
list store can hit its own count word. Even cases (ESI = A, no case 0)
also compare the adapter with callee counter deltas. 256 installed 31/a7
cases. Real-callee passes (4,096; not `base_marks`): `mech_ctor` over real
`0x49630c`, `pool_alloc`, `object_init`, `script_start` and the matrix
hooks; `flyer_ctor` over real `0x403368`, `object_init`, `script_start`,
matrix hooks; `building_factory` over real `0x49630c`, `pool_alloc`,
`0x49667c` with element constructors, both shape set-ups; `group_ctor`
over real `pool_nth`/`pool_prev`/`pool_next` on three pools; nested hooks
in all mode combinations. 44 branch sites, 38 both ways, taken edges of
`0x415e8b`, `0x4053cf`, `0x4054fe`, `0x405624`, `0x405745`, `0x405894`
PROVED. One caught mutation per hook (`MUTATIONS.md`); seven of eight
ad-hoc core mutants caught (the eighth, zero- instead of sign-extended
member count, is equivalent: the count never reaches 8000h).
`//core:mission_load_test`: an AI mech with one flat object and one track,
the flying player with three tracks; a flyer; an LC_BASE with two tracks
and two damaged parts, a full-condition BASE with frame word and model, a
case-0 type; groups of class 0, 2, 3; both `base_marks` paths.

### Live

Counts from the launch sets (selection rule of section 2; suites without a launch set use the
installed C1_01, two MEC records). `mech_ctor` = selected authored mechs +
MEC records = `weapon_fit` of section 16.

| Suites | `mech_ctor` | `flyer_ctor` | `building_factory` | `group_ctor` | `base_marks` |
|---|---:|---:|---:|---:|---:|
| 19 on installed C1_01 | 10 | 4 | 50 | 38 | 2 |
| c1_01-unarmed-keys | 9 | 4 | 50 | 38 | 2 |
| c1_02-probe | 13 | 2 | 50 | 36 | 2 |
| c1_06-cheat-disable | 16 | 4 | 54 | 40 | 2 |
| c1_08 (2) | 8 | 4 | 4 | 12 | 0 |
| c2_02-identify | 11 | 4 | 15 | 11 | 1 |
| c3_09 (3) | 16 | 0 | 32 | 21 | 10 |
| c3_10 (4) | 16 | 0 | 25 | 21 | 1 |
| c4_05-convoy | 13 | 2 | 2 | 12 | 0 |
| c4_08 (3) | 11 | 1 | 27 | 15 | 1 |
| c4_10-probe | 9 | 2 | 24 | 13 | 1 |
| c5_05 (2) | 15 | 2 | 31 | 29 | 1 |
| train1-timers | 1 | 0 | 11 | 3 | 1 |

- Original: the DBSIM map (every hook original,
  early arming, 40 suites) has exactly these entry counts. It also confirms the callee rows
  whose callees have no other caller: `0x417aa8`, `0x421ee8`, `0x405c3c`
  (only `group_ctor`), `0x40332c` (only `building_factory`), `0x403368`
  (map = row + `flyer_ctor`), `0x413a90` (only `mech_ctor`), `0x411dd4`
  (the three constructors; also equals the `_call_471cb0` row), `0x47136c`
  (only `base_marks` at `0x405de9`, at most once per call). `0x471cb0` has
  three more call sites (`0x42538c`..`0x4253c5`), so its map count is not
  the row.
- Hooks: every hook replaced, 49 shim
  imports replaced: **all suites identical**, exit
  states equal to the goldens, every skipped/bypassed/mismatch counter 0;
  the five call counters equal the table in every suite.

Callee counters per call: `mech_ctor` calls `49630c`, `4033a4`, `40d2cc`,
`49667c`, `413e50`, `40d160`, `40cd88`, `40d354`, `49681a`, `47ebb8`,
`47df19`, `480304` once and `492bec` twice; `flyer_ctor` each of its
eight callees once; `building_factory` `49630c`, `4774f0`, `471b30` once,
`49667c` once plus once per LC_BASE, `40332c` for cases 5/3, `403368` for
2/1/4; `group_ctor` `49681a` and `4774f0` once and `471cb0` once per
member, split over the joins by class. Type-dependent counters
(`mech_ctor`: `402374` 0..3 per mech, `4774f0` per mech with flat
objects, `471b30` = `402188` per flat object; `building_factory`:
`402374`/`4796d8` per track, `479344` per type with two tracks) depend on
`mforms.dat`/`bforms.dat`; their callees have other callers, so map
totals do not split. The counters of `pool_alloc`, `object_init`,
`script_start`, `euler_matrix`, `matrix_transpose`, `rotate_point`,
`track_rate`, `track_seek`, `pool_nth`, `route_point`, `terrain_height`
and `formation_*` are unchanged.

Snapshot reach: actor pool blocks (MECH [`0x4a9b7a`], FLYER [`0x4a9db9`],
buildings [`0x4a95a0`]), actor list `0x4a9af8`, flat-object pool
[`0x4a968d`], group array [`0x4a9e00`] and member arrays, allocated blocks
(flat arrays, part words and records, tracks via [A+34h]), terrain cells,
block pairing (same sizes, order, callees). The after-load snapshot
(section 14) covers this piece and section 16.

### Open

- The type-dependent callee counts (tracks, flat objects, part records)
  per suite.
- `0x402374`, `track_rate`, `track_seek` are stubs in the real-callee
  passes; their nesting under these hooks is checked live only.
- The join methods `0x417aa8`, `0x421ee8`, `0x405c3c` and the terrain
  marks `0x47136c` are own code in sections 23 and 27.
## 18. Compare mode for six L2 leaves

`pool_nth`, `subject_resolve`, `action_copy`, `condition_init`,
`region_init` and `timer_init` accept `compare`
([compare_mode.md](../reference-game/compare_mode.md#mission-load-weapon-slot-and-weapon-class-leaves)
has details and in-vitro probes). Declarations follow section 14's
contracts: `action_copy` a span over A+1BAh..1E1h (exactly the bytes it
writes, not the actor pools as regions), `region_init` R+0..9,
`timer_init` T+0..30h (incl. timer_arm's dword at T+2Dh; the nested
`timer_arm` runs on the clone), `condition_init` the two tables behind
[C+4] (29 words) and [C] (22 words) as one-hop regions isolated in place
(keeps overlapping in-vitro layouts comparable); `pool_nth` and
`subject_resolve` declare nothing (they store only to the argument slot
and the iterators' frames). Registers holding an address in a span
(A+1CEh, R, T+2Ch) are compared relative to it. `pool_create`,
`group_array` and `initial_damage` keep their refusal.

### Live result

Every hook replaced except the hooks of
compare_mode.md in `compare`, own runtime: **all suites identical**, every
`hk_<h>_mismatches` 0. Calls: pool_nth 66, subject_resolve 15,
action_copy 62, condition_init 2, region_init 8 in the C1_01 suites;
timer_init 3 in C3_10, 14 in train1-timers. Other nonzero counters are
compare mechanics: `bypassed` for `pool_nth`, `pool_prev`, `pool_next`,
`timer_arm`, `weapon_ammo` called inside the original phase of a compared
caller; `skipped` for `weapon_ammo`/`gun_ammo` (null p) and `beam_ready`
(up to 5,251 of 64,262 calls in c3_09-loadout-b, reason not traced;
compared calls stay above 0 in every suite that reaches it).

## 19. Object shape set-up

From the pinned listing; the exception descriptors `0x497145` and
`0x497175` (record at EBP−24h) read from the file. Own code: adapter group
`object_shapes`, core `core/src/object_shapes.c` (not "setup": Windows'
installer heuristic refuses test executables with that name).

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

No file dword holds any of the nine entries, so these are all callers.
`0x40332c`, `0x403368` and `0x4033a4` all store `ANIM_OBJ`, then
`DET_ANIM_OBJ`: all three are `DET_ANIM_OBJ` constructors
([vtables.md](vtables.md) §1.1). Callees stay game code behind the bridge
in their own modes: the group's hooks for each other, `object_init`,
`shape_instance`, `node_matrices`, `track_clip`, `track_rate`,
`track_sort`, `matrix_ctor` (through `0x49667c`); the runtime (`0x49630c`,
new `0x47749c`, new[] `0x4774f0`, `0x49667c` with element constructors
`0x4922e6`, `0x495132`), the arena allocation `0x47a288`, the CW3220 thunks
memset `0x49681a` and memcpy `0x49680e`.

All nine run while objects are built: the actor load's constructors
(section 17), the weapon factory's (section 16, through `0x40df30`) and
`0x42067c` (constant counts over each suite in the map). With early arming
(section 13) the counters are constant from the first tick record.

### Contracts (from the bytes)

All cdecl, two argument slots; EDX:EAX reproduced exactly. Game calls go
through the bridge with the original's entry EAX/EDX/ECX (the core follows
them; in vitro compares each). "Count ±n" = `[[0x6cbee4]] ± n`, pointer
reread each time. No local is read before written (the runtime records are
written by `0x49630c`); no caller EBX/ESI/EDI is read.

**`ts_obj_ctor` 0x402278 (O, shape)**, frame `add esp,-38h`: `0x49630c`
(EAX = descriptor `0x497145`, entry EDX/ECX); [O] = `DRAWABLE` `0x4a14a4`,
count +1 (ECX = 0, EDX the pointer, EAX = 0); locals EBP−2Ch/−2Ah/−28h
(words) and EBP−38h/−34h/−30h (dwords) = 0, then the pose block from them:
[O+2Eh], [O+26h], [O+2Ah] = 0, dword [O+0Ch] = 0, words [O+10h], [O+32h] =
0 (EAX = O+0Ch, EDX = ECX = 0); [O] = `MAT_OBJ` `0x497289`, count +1 (EAX
the pointer), [O] = `TS_OBJ` `0x4a0b80`. Null shape: EDX = 0. Else
new(42h) (EAX the pointer, EDX = ECX = 0; `POP ECX` = 42h) into EBP−4;
null: EDX = 0; else context 14h, `anim_instance_ctor(obj, shape)` (EAX =
ECX = obj, EDX = shape), count −3 (EAX the pointer), context 8, EDX = obj.
[O+34h] = EDX; memset(O+38h, 0, 8) (EAX = O+38h, EDX/ECX as left); byte
[O+39h] = 1; count +1 (EDX the pointer). EAX = O, EDX = counter pointer,
ECX = saved FS:0 head from the record (EBP−24h), stored to FS:0 by the
epilogue.

**`det_anim_shape` 0x40332c / `det_anim_static` 0x403368 (O, shape)**:
`ts_obj_ctor(O, shape)` resp. `object_init(O, shape)` (EAX = shape, entry
EDX/ECX); [O] = `ANIM_OBJ` `0x4973ac`; ECX = 0; count +1 (EDX the
pointer); [O] = `DET_ANIM_OBJ` `0x4973cc`; [O+41h] = 0; count +1 (EAX the
pointer). EAX = O, EDX = first counter pointer, ECX = 0.

**`det_anim_record` 0x4033a4 (O, R)**: `ts_obj_ctor(O, [[R]])` (EAX =
[[R]], entry EDX/ECX); [O] = `ANIM_OBJ`; count +1 (EDX); [O] =
`DET_ANIM_OBJ`; [O+41h] = R; count +1 (ECX the pointer). EAX = O.

**`anim_instance_ctor` 0x478838 (I, shape)**: `shape_instance(I, shape)`
(EAX = shape, entry EDX/ECX; stores the shape at I+4); [I] = `0x4a14c4`;
memset(`0x6c56a8`, 0, 20h) with `shape_instance`'s registers; DX = 4000h
(memset's high word kept) into words `0x6c56b8`, `0x6c56ae`, `0x6c56a8` (a
global rotation, diagonal 4000h); slot dwords I+1Ah..I+40h = 0 (EAX =
I+42h, EDX = 10, ECX = 0). Node table N = [[I+4]+2Ah]; word [I+0Ch] = n =
word [N+16h]; new[](sx n) (EAX = sx n, EDX = N, ECX = 0; `POP ECX` = sx n)
into [I+0Eh]; `0x49667c(0, 0Ch, sx word [I+0Ch], 1, 0x4922e6)` (EAX =
new[]'s, EDX = that count, ECX = sx n) into [I+12h] (12-byte poses);
`0x49667c(0, 20h, sx word [I+0Ch], 1, 0x495132)` (EAX/EDX = the first's,
ECX = the count) into [I+16h] (matrices). For k = 0 while k < sx word
[I+0Ch] (reread, signed): the three dwords of entry sx word [[N'+18h] + 2k]
of pose table [N'+12h] (N' = [[I+4]+2Ah] reread per node; index × 3
dwords, 32-bit) to [I+12h] + 12k, then byte [[I+0Eh] + k] = 1.
`node_matrices(I)` (EAX = final k, EDX = [I+0Eh] or, with no node, the
second `0x49667c`'s EDX, ECX = sx word [I+0Ch]; `POP ECX` = I); M =
[I+16h]; memset(M, 0, 20h) (EAX/EDX = `node_matrices`', ECX = I); words
[M+10h], [M+6], [M] = 4000h; count +1 (EDX). EAX = I, ECX = memset's.

**`object_track` 0x402374 (O, word)**, frame `add esp,-24h`: `0x49630c`
(descriptor `0x497175`); new(52h) (EAX = `0x49630c`'s, entry EDX/ECX;
`POP ECX` = 52h) into EBP−4; non-null: context 14h, `thread_ctor(T,
[[[O+34h]+4]+2Ah])` (EAX = that shape tree, EDX = T, ECX = [[O+34h]+4], T
reread from EBP−4), context 8. `track_clip(T, word, 0, 0)` with AX = the
word (`MOV AX`: the pushed dword's high word is EAX's, from `thread_ctor`
or, null path, new), `track_rate(T, 0)`, `thread_add([O+34h], T)` (EDX =
[O+34h] read after `track_rate`). EAX = T, EDX = saved FS:0 head (caller
restores it), ECX = `thread_add`'s. A null thread faults at
`track_clip`'s first store; the game's allocations do not fail (map:
`0x478e48` = `0x402374` in every suite).

**`thread_add` 0x478a3c (I, T)**: the first k = 0..9 with [I+1Ah+4k] = 0
gets T, then `track_sort(I)` (EAX = the slot, EDX = k, ECX = I; `POP ECX` =
I), EAX = 1, EDX = `track_sort`'s; ten taken: EAX = 0, EDX = 10. ECX = I.
Entry registers not read.

**`thread_ctor` 0x478e48 (T, M)**: [T] = M; `track_clip(T, 0, 0, 0)` (EAX =
T+2Eh, EDX = M, entry ECX); memset(T+2Eh, 0, 20h) with `track_clip`'s
registers; CX = 4000h (memset's high word kept) into words [T+3Eh],
[T+34h], [T+2Eh]. EAX = T, EDX = memset's, ECX = memset's with that low
word.

**`shape_word_block` 0x402fc0 (B, S)**: [B] = S. S ≠ 0 and n = word
[S+24h] ≠ 0 (`JBE` after `TEST`: zero only): `0x47a288(2n, [0x6c5830])`
(EAX = heap, EDX = 2n, entry ECX) into [B+4], then memcpy(block,
[[B]+1Ch], 2 × word [[B]+24h]) ([B] reread; EAX = [B], EDX = size, ECX =
source); n = 0: [B+4] = 0 (EAX = 0); S = 0: [B+4] untouched. Count +1
(EDX). EAX = B; ECX = memcpy's or entry ECX. Same steps as
`shape_instance` (main_loop.md) at +0/+4 instead of +4/+8.

**ECX is dead after all nine** at every caller (forward walk of both
arms from each return address; in
`building_factory` ECX's high word reaches `track_rate`'s second slot and
`track_target`, which reads only words). The framework's entry ECX stands
in; the cores still compute ECX (compared in vitro).

**Frames and shims.** `ts_obj_ctor` and `object_track` run with the
synthetic EBP of a zeroed scratch (`ES2_OBJECT_SHAPES_FRAME`; `0x49630c`
through `weapons_impact_guest_call`); their adapters write the saved head
to FS:0 (`ts_obj_ctor`: ECX, `object_track`: EDX). No entry or exit shim
(no uninitialized local, caller register or stack residue read by a caller
is known). **Compare:** refused for all nine (allocation, constructors,
the construction count, runtime record, game calls).

### In vitro (group `object_shapes`)

`invitro/object_shapes_cases.h` passes: per hook 100,000 direct cases
(quick tier; the full tier has 1,001,024) against logging stubs (17 stub
ids), 256 installed 31/a7 cases, 4,096 real-callee cases (except
`shape_word_block`); 9 branch sites, all both ways; one caught mutation
per hook (`MUTATIONS.md`); core unit test `//core:object_shapes_test`.

### Live

Entry counts on the original (DBSIM map, all 40 suites):
`det_anim_record` = `hk_mech_ctor_calls`, `det_anim_shape` =
`hk_building_factory_call_40332c`, `det_anim_static` =
`hk_building_factory_call_403368` + `hk_flyer_ctor_calls`, `ts_obj_ctor` =
`anim_instance_ctor` = `mech_ctor` + `_call_40332c`, `object_track` =
`thread_add` = `thread_ctor` (e.g. C1_01 suites: 25 / 15 / 39 / 10 / 25 /
59 / 59 / 59 / 71). Every `*_skipped`/`*_bypassed` 0; the counters of the
bridged hooks are unchanged.

Snapshot reach: object bytes +0Ch..+41h, the shape instances and their
arrays, the 52h-byte threads, `0x6c56a8`, the word blocks, the block
pairing (same sizes, same order, same allocators).

**Result:** every hook replaced, own
runtime: all suites identical; counters equal the entry counts in every
suite. After-load snapshot (section 14): identical.

### Open

- `thread_add`'s `track_sort` calls and `shape_word_block`'s allocation
  calls per suite are not predicted (their callees have other callers).
- `0x4922e6` (the pose element constructor: two calls of `0x4922d3` at +0
  and +6) is reached only through `0x49667c` and has no verdict row.
- The third `shape_word_block` call, `0x403274`, lies in `0x4031d4` (own
  function after the `RET` of the H3 draw `0x40316c` at `0x4031d3`,
  runtime record `0x497348`); no call, jump or file dword reaches it and
  the map has no probe row for it; not examined.

## 20. Collision data

The collision data of `collide.cpp` (named by the assertion strings), from
the pinned listing (exception descriptor `0x4987f0`, stream vtable
`0x4a0e10` and assertion strings read from the file). Own code: adapter
group `collision_data`, core `core/src/collision_data.c` (not
`collision.c`, the runtime ground collision; these are load-time readers on
the weapons guest bridge).

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

No file dword holds any of the nine addresses, so these call sites are
all. Callees stay game code behind the bridge in their own modes: the
stream reads (slot `+18h`; `stream_read` `0x4735b8` for the game's
streams), new `0x4774f0`, new[] `0x49667c` with element constructors
`0x40c8be` (returns its argument) and `0x40cf04` (EAX = argument, EDX = it
+ 8), `0x49630c`, stream constructor `0x472f14`, `stream_destroy`
`0x472f64`, assertion `0x479c78` (fatal report), `dist3d_delta`
`0x47de72`, the original volume match `0x40ce28` (copies a weapon's
spheres into the matched volume through `0x40c834`) and the nine for each
other.

### Layout

Descriptor 6 bytes: group count word +0, groups +2. Group 10 bytes: part
word +0 (from the file), +2 not written by the reader, volume count word
+4, volumes +6. Volume 16 bytes: sphere count word +0, spheres +2, region
word +6, centre +8..+0Dh, radius word +0Eh. Sphere 8 bytes: x, y, z,
radius words. This is [collision.md](../formats/collision.md)'s COL grammar
with the runtime fields; the reader reads words straight into these
records, so the file's "part, count" pair lands at +0 and +4.

### Contracts (from the bytes)

All cdecl with a `push ebp` frame; EDX:EAX reproduced exactly. "Read(n,
buf)" = the stream's virtual `+18h` (S, n, buf), `[S]` reread per read.
Loops count in a 32-bit register and compare its low word with the reread
count word, signed (`CMP r16,[m]`); a negative count runs no iteration.

**`collision_load` 0x40cd88 (name)**, frame `add esp,-50h`: descriptor
`0x4987f0` ([+4] = −50h): record at EBP−50h, context word EBP−40h;
`0x49630c` (EAX the descriptor, entry EDX/ECX), context 8.
`0x472f14(EBP−30h, name)` (EAX = `0x49630c`'s, EDX = EBP−30h) builds the
stream at EBP−30h (vtable `0x4a0e10`). Nonzero status [EBP−2Ch]:
`0x479c78(0x6c56c8, 5, 0, "collide.cpp", 101h, name)` (EAX = status, EDX
= 0 from `SETE DL; AND EDX,1`). CX = word `0x4987de` (high word kept), the
word `INC WORD` in memory (a second read), slot = `[0x4a9824]` + 6 ×
s16(old) (table read after the increment); `collision_read(slot,
EBP−30h)` (EAX = EBP−30h, EDX as left, ECX the CX load);
`0x472f64(EBP−30h, 2)` (EAX = slot, EDX = EBP−30h; the slot is pushed
above both arguments and popped into EAX after). EAX = slot, EDX = saved
FS:0 head [EBP−50h] (epilogue stores it), ECX = `0x472f64`'s. The table is
allocated by `0x40cbec` in the world load (pass 1, `0x4252d8`, section 2
step 9): new(6 × word `0x4987dc`), counted up by the type loads
`0x41ff10`/`0x422e90` through `0x40cbd4` (step 8). None of the nine resets
`0x4987de`.

**`collision_read` 0x40ccf8 (D, S).** Nonzero status [S+4]:
`0x479c78(0x6c56c8, 5, 0, "collide.cpp", 0EEh, "collision data load")`
(EAX = status, EDX = 0, entry ECX). Read(2, D) (EAX = D, EDX = S, ECX =
[S]). Count c = word [D]: 0 returns the read's registers. Else
`new(10 × s16(c))` (EAX = s16(c), EDX = size, ECX the read's; `POP ECX` =
size), [D+2] = block; per group G (stride 10) while i < s16([D]):
`collision_entry(G, S)` (EAX = S), `collision_bounds(G)` (`POP ECX` = G).
EDX:EAX/ECX: the last call's (negative count: new's, ECX the size).

**`collision_entry` 0x40cc50 (G, S).** Read(2, G) (EAX = G, EDX = S, ECX =
[S]), Read(2, G+4) (EAX = G+4); no result tested. Nonzero k = word [G+4]:
`0x49667c(0, 10h, s16(k), 1, 0x40cf04)` (EAX = s16(k), EDX/ECX the
read's), [G+6] = block; per volume V (stride 10h) while i < s16([G+4]):
`collision_volume(V, S)` (EAX = S). Then `MOV AX,1`: EAX = 1 with the high
word of the last call's EAX (second read, new[] block or last
`collision_volume`).

**`collision_volume` 0x40cc14 (V, S).** Read(2, V+6) (EAX = S, EDX = [S],
entry ECX); r = AL: nonzero calls `collision_points(V, S)` (EAX = S). EAX
= r (0..FFh), EDX/ECX the last call's.

**`collision_points` 0x40c7c4 (V, S).** Read(2, V) (EAX = S, EDX = [S],
entry ECX); r = AL. If r ≠ 0 and the word 8 × [V] (EDI = V with low word
the count, `SHL EDI,3`, tested as DI) ≠ 0: `0x49667c(0, 8, s16([V]), 1,
0x40c8be)` (EAX = s16 of the reread count), [V+2] = block, Read(s16(DI),
block) (EAX = size, EDX = S, ECX = [S] read after the store); r = its AL.
EAX = r. So count 1000h reads FFFF8000h bytes and 2000h reads nothing;
[collision.md](../formats/collision.md)'s corpus stays below 13 spheres
per volume.

**`collision_bounds` 0x40ccc8 (G).** Per volume V of [G+6] while i <
s16([G+4]): word [V+0Eh] = AX of `collision_radius(V, V+8)` (EAX = V+8,
EDX/ECX as left). No volume: entry EAX/EDX/ECX.

**`collision_radius` 0x40c5d0 (V, O).** Frame words: maxima x, y, z at
EBP−10h/−0Eh/−0Ch from −10000 (D8F0h), minima at EBP−8/−6/−4 from 10000.
Per sphere (x, y, z, r) of [V+2] while i < s16([V]): per axis the word sum
a + r replaces the maximum unless smaller (signed 16-bit `CMP`/`JL`), a − r
the minimum unless larger (`JG`); 16-bit wrap included (a sum above 32767
is negative). Then dword [O] = min x | min y << 16, word [O+4] = min z,
words +4, +0, +2 += the maxima (in that order), then `SAR` of words +4,
+0, +2 (16-bit arithmetic): centre = (min + max) SAR 1, sum modulo 2^16.
No sphere: centre 0, deltas below −10000
([collision.md](../formats/collision.md) "zero-sphere shapes"). Deltas =
max − centre per axis (16 bits); `dist3d_delta(s16(dx), s16(dy),
s16(dz))` with EAX/EDX/ECX = O's high word with low word cy/cz/cx; its
EDX:EAX and ECX are returned. Entry EDX/ECX never reach memory.

**`collision_scan` 0x40cec0 (T, A, B, n)**; `weapon_fit` calls (the
actor's table [A+1F6h], EBP−30h, EBP−38h, 1). Per group g of [T+2] (stride
10) while i < s16([T]): `collision_match(g, A, B, n)` (EAX = the pushed
dword: n's low word with the high word of s16([T]), 0 inside the loop; EDX
= B, ECX = A). The first nonzero AX calls `collision_bounds(g)` and ends
the scan (its EDX:EAX, `POP ECX` = g). No match: EAX = s16([T]), EDX/ECX
the last call's or the entry's.

**`collision_match` 0x40ce78 (G, A, B, n).** Per volume V of [G+6] while i
< s16([G+4]): `0x40ce28(V, A, B, n's low word)` (registers as in the
scan); nonzero AX ends the loop. EDX = s16([G+4]) reread, EAX = 1 if i <
EDX else 0 (`SETL AL; AND EAX,1`), ECX the last call's. `0x40ce28`
compares V's region word with that of each of the n volumes [A+4j] and on
the first equal one copies its spheres moved by [B+4j] into V through
`0x40c834` (new[] of the sphere array); `weapon_fit` has just set the
weapon volume's region word (+18h of the weapon record) to the mount's
slot byte + 13h.

**ECX is dead at every call site**, so the framework returns the entry
ECX (`collision_read`'s entry ECX reaches only the fatal `0x479c78`).

**Frames, residue, shims.** `collision_load` runs with the synthetic EBP of
a zeroed scratch (`ES2_COLLISION_LOAD_FRAME`; `0x49630c` through
`weapons_impact_guest_call`), the stream at the original offset; the
adapter writes the saved head (EDX) to FS:0. No function reads a local it
did not write; no callee reads theirs except the stream object `0x472f14`
builds. Scan and match push n as a dword whose high word is the loop
count's (0); `0x40ce28` reads the low word only (`MOVSX`). The framework
passes up to six slots; `0x472f64` reads two (the pushed slot is popped,
not read). No entry or exit shim.

**Compare:** `collision_radius` accepts it: it writes only the six centre
bytes (one span: argument 1, 6 bytes; in the game O = V+8, outside the
count and pointer it reads) and calls the pure, compare-capable
`dist3d_delta`. `-Hooks "collision_radius=compare"` is possible. The other
eight refuse (stream reads, allocation, slot counter, runtime record,
assertion, game calls writing groups and volumes).

### In vitro (group `collision_data`)

`invitro/collision_data_cases.h` passes: per hook 100,000 direct cases
(quick tier) against logging stubs (16 stub ids; counts include negative
words and 1000h, 1FFFh, 2000h, 2001h), 256 installed 31/a7 cases, 4,096
real-callee cases; `collision_radius` passes the 512 installed compare
cases of `compare_probes.h` (0 mismatches); 21 branch sites, all both
ways; one caught mutation per hook (`MUTATIONS.md`); core unit test
`//core:collision_data_test`.

### Live

Entry counts on the original (DBSIM map, all 40 suites,
probe counts capped at 1,000) follow the
launch sets: `collision_load` = `mech_ctor` + 1 with a flyer type load
(`0x422ed0`); `collision_read` = `collision_load` + 65 (`BASECOL.DAT`);
`collision_volume` = `collision_points` = volumes read + 33 (weapon list
`0x40f8bc`); `collision_bounds` = `collision_entry` + `collision_scan`
(every scan finds its group); `collision_radius` = volumes of all groups +
volumes of the scanned groups (C1_01 suites: 11 / 76 / 103 / 296 / 296 /
134 / 603 / 31 / 189). Where the map caps `collision_radius`, live counts
are 1,711 (C3_09), 1,418 (C3_10), 1,608 (C5_05). `_call_479c78` and every
`*_skipped`/`*_bypassed` 0; `hk_dist3d_delta_calls`,
`hk_stream_read_calls`, `hk_mech_ctor_call_40cd88` and
`hk_weapon_fit_call_40cec0` unchanged.

Snapshot reach: `[0x4a9824]`, `0x4987de`, the group, volume and sphere
arrays, volume words +8..+0Fh, the `BASECOL.DAT` table, the weapon list's
volumes, the block pairing (same sizes and order through `new`/`new[]`).

**Result:** every hook replaced, own
runtime: all suites identical; counters equal the entry counts in every
suite. After-load snapshot (section 14): identical.

### Open

- `0x40ce28` (36 instructions) and `0x40c834` (52), the scan's sphere copy,
  stay original; the element constructors `0x40c8be`/`0x40cf04` are reached
  only through `0x49667c`.

## 21. L6 actor load

L6 of section 10, the actor load `0x4253d8` (script pass 2), from the
pinned listing (trigger case table `0x426492` and jump table `0x42649d`
decoded from the bytes, exception descriptor `0x49a8b5` and stream vtable
`0x4a1738` read from the file). Own code: hook `actor_load` in adapter
group `mission_actors`, core `es2_actor_load_registers` in
`core/src/mission_load.c`; its own group, not section 17's
`mission_ctors`, because it calls those constructors and its harness (a
synthetic `script.dat` behind stub reads) shares nothing with theirs.

Every callee stays game code behind the bridge in its own mode:

| Callee | Role | Class / mode |
|---|---|---|
| `0x49630c` | runtime frame record (descriptor `0x49a8b5`: EBP−34h) | RTL, through `weapons_impact_guest_call` |
| `0x45ee00`, `0x492b90` | the script name (`data\script` + `[0x4a09f0]`) | file |
| `0x47ad40`, `0x47af98`, `[vtable+18h]` (= `0x47af98`), `0x47ad78` | the stream in the frame: open, reads, close | file (stream library) |
| `0x40f998`, `0x40fa88` | the weapon shape cache (`mechwpns`) | resource |
| `0x470ed4`, `0x47129c` | terrain marks of a placed building, the cell marks | terrain |
| `0x4774f0`, `0x49680e` | new[], memcpy | RTL |
| `pool_alloc`, `mech_ctor`, `terrain_height`, `weapon_fit`, `condition_init`, `action_copy`, `initial_damage`, `flyer_ctor`, `building_factory`, `shape_word` (the building virtual +10h), `subject_resolve`, `heading_point`, `group_ctor`, `base_marks`, `pool_prev` | | hooks, their own modes |

`0x4253d8` has one call site, `0x46182b` in `0x461608`, once per process;
with early arming (section 13) `hk_actor_load_calls` is 1 from the first
tick record. In the DBSIM map: entry count 1 for `0x4253d8`,
`0x40f998`, `0x40fa88`, `0x47129c`, and the predicted count for
`0x425348` (all calls from here), in all 40 suites.

### Contract (from the bytes)

No arguments; EBX, ESI, EDI saved and restored. Frame `add esp,-0B30h`:
runtime record EBP−34h (context word EBP−24h), stream object EBP−0Ch
(vtable `0x4a1738`, handle word +8), count words EBP−36h (current list),
EBP−38h (authored mechs M), EBP−42h (timers), EBP−44h (a route's points),
MEC ordinal EBP−46h, record buffers (skip EBP−8C4h, timer EBP−8DCh, mech
EBP−964h, flyer EBP−9CCh, building EBP−0A0Ch, order EBP−0A28h, group
EBP−0AC4h, objective EBP−0B24h), the group's ten order pointers EBP−0AECh,
the building point EBP−0B30h, staged positions and angles. No local is
read before written. EBX is the loop counter, compared as BX (signed); ESI
the object or runtime-record cursor; EDI the selection-flag pointer, then
the trigger scope. In order:

1. `0x49630c` (EAX = descriptor, entry EDX/ECX). Statics: byte `0x49a87d`
   = 0: dwords `0x49a871` = `0x49a875` = 0 (EDX, ECX = 0), `0x49a879` =
   FFFF8000h, the byte +1; byte `0x49a884` = 0: words `0x49a87e` =
   `0x49a880` = 8000h, `0x49a882` = 0, the byte +1. Context 8.
2. `0x492b90(0x45ee00(0x49aa3e "data\script"), [0x4a09f0])` (outer second
   slot pushed first, `[0x4a09f0]` read before the inner call; `POP ECX` =
   `0x49aa3e`), stream `0x47ad40(EBP−0Ch, name)`, word `0x4a9e4c` = FFFFh,
   the 20-byte header `0x47af98(stream, 14h, EBP−8C4h)`.
3. Pass 1's lists skipped: each count through the virtual read +18h
   (`(stream, 2, buffer)`), then coordinates (12 × n in one read when n ≠
   0), angles (2 × n), routes (per route a count word, then 2 × k, read
   even for k = 0), regions (6 × n), triggers (74 bytes per record when n
   ≠ 0 and n > 0), timers (24 per record). EAX/EDX/ECX hold vtable, buffer
   and stream in three permutations across these calls; the core follows
   each.
4. Word EBP−46h = 0; `0x40f998()`; M through the virtual read into
   EBP−38h.
5. **Mechs**, i = 0 while i < sx(M) + sx(word `0x4aaa5e`) (both reread;
   EAX = sx(N), ECX = the sum, EDX = sx(i) at the exit), EDI = `0x4aa72a`
   + i. Each pass: EBP−4Ch = [`0x4aaa60`] + 126 × sx(ordinal); for i < M
   the 134-byte record is read (selected or not). Unselected: next.
   Selected: for i ≥ M type word EBP−93Ch ← MEC word +4 (CX).
   `pool_alloc([0x4a9b7a])` (`POP ECX` = pool) into EBP−50h and EBP−10h;
   non-null: context 20h, `mech_ctor(A, type)` with the type in the low
   word of pool_alloc's EAX, entry EAX = that dword, EDX = ECX = A; count
   −7; context 14h. A null object faults (PROVED edge of `0x425690`).
   - MEC actor (i ≥ M): AX = ordinal; equal to word `0x4aaa64`:
     `[0x4a9b84]` = A, byte A+0A3h = 1 (through EBP−54h). Position and
     angles from the statics.
   - Authored: coordinate word +3Eh < 0: statics; else point 12 × sx +
     [`0x4a9e0c`] (staged in EBP−60h), x/y/z into EBP−970h/−96Ch/−968h,
     `terrain_height([0x4a0be0], EBP−970h)` (EAX = point, EDX = terrain,
     ECX = y), FFFF8000h → FFFF8001h, plus sx(word [[A+1F2h]+16h]), plus
     5000 when word [[A+1F2h]+50h] ≠ 0 (pointers reread). Angle word +40h
     < 0: statics; else words (0, 0, [[`0x4a9e10`] + 2 × sx]).
   - Items/secondary: MEC +8/+20h or record EBP−93Ah/−8FAh (words
     +2Ah/+6Ah). Position A+26h (z at +8 first), angles A+0Ch, word A+32h
     = 0, `weapon_fit(A, items, secondary)` (EAX = items, EDX = A+0Ch, ECX
     = secondary).
   - MEC: `condition_init(A+206h, MEC+3Ch, +56h, +6Ah)` (EAX = +3Ch, EDX =
     A+206h, ECX = +56h), word A+29Ch = MEC word 0, ordinal +1 (only here:
     an unselected MEC actor does not advance it).
   - Authored: byte A+97h = 1 when record word 0 ≠ 0; A+1B2h/A+1B6h = the
     trigger records 58 × sx + [`0x4a9e28`] of words +80h/+82h, 0 for
     negative words; `action_copy(A, EBP−922h, EBP−90Eh)`;
     `initial_damage(A, word +84h)` (ECX = action_copy's ECX with that low
     word); word A+252h = word +2 when above −1.
6. `0x40fa88()`.
7. **Flyers** (92 bytes, selection `0x4aa856`): `pool_alloc([0x4a9db9])`,
   context 38h, `flyer_ctor(F, type +2Ch)` (EDX = pool_alloc's EDX with
   the type word, EAX = ECX = F), count −7, context 2Ch; a null flyer
   faults (PROVED edge of `0x425a5d`). Coordinate +28h (no ground height)
   and angle +2Ah or the statics, triggers +56h/+58h, `action_copy(F,
   +2Eh, +42h)`.
8. **Buildings** (52 bytes, selection `0x4aa8ba`, factory byte from
   `0x4aa9d2` + i): `building_factory(EBP−0A0Ch, byte)` (EAX = the read's
   EAX with that low byte); only with a coordinate (word +2 ≥ 0): the
   object's virtual +10h (`shape_word`, EAX = point pointer, EDX = y),
   `0x470ed4([0x4a0be0], EBP−0A18h, sx(word))` and `terrain_height` there
   (FFFF8000h → FFFF8001h, no offsets); else the statics. Angle +4,
   triggers +2Eh/+30h, `action_copy(B, +6, +1Ah)`.
9. **Orders:** count straight into word `0x4a9e2c`; nonzero: `new[](22 ×
   n)` → `[0x4a9e30]` (`POP ECX` = size, which the first read receives);
   per record while i < word `0x4a9e2c` (reread): words +0, +2, scope +8
   to +0, +2, +0Ch; trigger (+0Ch), route (+6, 6 × sx + [`0x4a9e18`]) and
   coordinate (+4) pointers to +12h, +8, +4 unless the word is −1;
   `subject_resolve(sx +0Ah, sx +8)` to +0Eh.
10. **Groups:** the count read here only gates the loop (tested for 0);
    the loop runs over pass 1's array (`[0x4a9e00]`, count word
    `0x4a9e04` copied to EBP−0C4h and decremented after the test; a null
    array also ends it). Per 156-byte record: group 0's twenty roster
    words become remap bytes `0x4aa78e` + M + k for k < N (word `0x4aaa5e`
    reread), else −1; other groups' non-negative slots go through the
    remap table of class word +28h (0 `0x4aa78e`, 1 `0x4aa888`, 2
    `0x4aa946`; other classes unchanged). Ten order pointers (22 × sx +
    [`0x4a9e30`], 0 for −1). Heading: angle +2Eh ≥ 0 from the angle table,
    else `heading_point([pts+4], [pts])` of the first order's route (+8 of
    the first order, which must exist: null faults) when non-null with at
    least two points, else 0. Trigger +70h and coordinate +2Ch pointers
    (only −1 tested, gives 0). `group_ctor(G, EBP−0AC4h, trigger,
    EBP−0AECh, heading, point)` (EAX = G, EDX = trigger, ECX = record; the
    heading slot keeps ECX's high word). Class 2 with word 0 = 1:
    `base_marks` of the first member (`[[G+0Ch]]` when word G+10h > 0,
    else 0, which faults in `base_marks`).
11. **Objectives:** count straight into word `0x4a9e3c`; nonzero:
    `new[](76 × n)` → `[0x4a9e40]`; per 54-byte record while i < word
    `0x4a9e3c` (reread): words +0, +2, scope +4; `subject_resolve(sx +6,
    sx +4)` to +6; coordinate (+8) and route (+0Ah) pointers for words
    above −1 to +0Ah, +0Eh; `memcpy(+24h, rec+0Eh, 20)`, `memcpy(+38h,
    rec+22h, 20)`; the three dwords `[0x4aa660` + 4 × (index + j)]` (index
    +0Ch above −1, else 0) to +12h, +16h, +1Ah; `0x49a870` to +1Eh.
12. `0x47129c([0x4a0be0])`; every building of the pool (`pool_prev`,
    oldest first) at `terrain_height` of its x/y (no adjustment), the
    point copied through EBP−0B30h and written back.
13. Trigger subjects (`[0x4a9e28]`, 58-byte records, count word
    `0x4a9e24` reread): the type word through byte table `0x426492`: types
    0..6 clear subject dword +36h (as
    [mission.md](../formats/mission.md#trigger-table-and-actions) has it),
    7/8/9/10 resolve it with `subject_resolve(+36h, 1/2/3/0)`, others
    (above 10 unsigned) leave it.
14. `0x47ad78(stream, 2)` (no delete). EDX:EAX = its result; ECX = saved
    FS:0 head `[EBP−34h]`, stored to FS:0 by the epilogue.

**Registers, stack and shims.** The core follows EAX/EDX/ECX through every
instruction, so each guest call gets the original's entry registers and
slots (`POP ECX` reloads the pushed value; no popped callee writes its
slot). The results are dead at `0x46182b`: `0x44f6f4` loads EAX before
`0x49630c`, which keeps EDX/ECX and writes both before reading them; the
framework returns the entry ECX, the adapter writes the head to FS:0. The
frame lies in a zeroed scratch at the original's offsets
(`ES2_ACTOR_LOAD_FRAME`, the record through the bridge's EBP). Guest
callees get the bridge's EBX/ESI/EDI instead of the original's; the only
one known to read one is `building_factory`'s case 0 (types 10, 53, 54 or
above 3Dh: caller's EDI as the object), in no launch set (section 17); a
replaced `building_factory` passes EDI = 0. Callees run deeper below the
replaced frame; none reads stack residue this changes (`residue.py`, a
linear scan of the direct non-hook callees and stream helpers; the hooks'
own contracts). Trap: `0x44f6f4`, called next, builds a bitmap object at
its EBP−1Ch whose flags byte +9 (EBP−13h) it only masks (`AND F0h`, `OR
10h`), so bits 5–7 are residue (originally bits 13–15 of this function's
last flyer pointer EBP−14h); nothing reads that byte (the body reads +4,
+6, +0Ah, +0Eh; destructors `0x4815b8`, `0x4838fd` +0Ah, +0Eh, +14h,
+16h). The skip reads use the 2,232 bytes below the stream object; the
largest in the launch sets is 1,008 (84 coordinates of C1_01). No entry or
exit shim.

**Compare:** refused (G V I A: stream, allocations, constructors, building
virtual).

### In vitro (group `mission_actors`)

`invitro/mission_actors_cases.h` passes: 100,000 direct cases (quick tier;
the full tier has 1,001,024) against logging stubs (27 stub ids) over a
synthetic `script.dat` per case, with the core's scratch frame at the
original's frame address so offsets and registers compare raw; 256
installed 31/a7 cases; 4,096 real-callee cases. 84 branch sites, 82 both
ways; the taken edges of `0x425690` and `0x425a5d` PROVED (null object
faults). One caught mutation (`MUTATIONS.md`), twelve ad-hoc core mutants
caught. Core unit test `//core:mission_load_test` (one hand-worked script,
54 calls checked).

### Live

`hk_actor_load_calls` = 1 in every suite, and once each per suite
`_call_49630c`, `_45ee00`, `_492b90`, `_47ad40`, `_40f998`, `_40fa88`,
`_47129c`, `_47ad78`. The other callee counters are predicted from the
launch sets (selection rule of
section 2, suite-to-set mapping of section 17; the 19 installed C1_01
suites use `c1_01-unarmed`'s lists with two MEC records). They agree
with the counts of sections 14, 16, 17 (`mech_ctor` =
`weapon_fit`, `flyer_ctor`, `building_factory`, `group_ctor`,
`base_marks`, `action_copy`, `initial_damage`, `condition_init`,
`subject_resolve`). Nested hook counters unchanged.

Snapshot reach: actor pools and list, `[0x4a9e30]`, `[0x4a9e40]`, group
and member arrays, trigger subjects, `0x49a871..0x49a884`, `0x4a9e2c`,
`0x4a9e3c`, `0x4a9e4c`, `0x4a9b84`, the block pairing.

**Result:** every hook replaced, own
runtime: all suites identical, every skipped/bypassed/mismatch counter 0;
counters equal the predicted counts in every suite. After-load snapshot: O1 = O2 and O2 vs R
identical (scope all and pools) in acceptance-60s, c3_10-loadout-hercs and
c3_09-loadout-b.

### Open

- Bridged original callees without a verdict: `0x45ee00`, `0x492b90`,
  `0x47ad40`, `0x47af98`, `0x47ad78` (file), `0x40f998`, `0x40fa88`
  (resources); `0x49630c`, `0x4774f0`, `0x49680e` stay with the runtime
  library. `0x470ed4` and `0x47129c` are own code in sections 23 and 27.

## 22. Damage data and part tables

The eleven functions of `damage.cpp` (descriptors `0x498840`/`0x498878`,
stream table `0x4a0e10` and initial globals read from the file). DMG
grammar and names: [collision.md](../formats/collision.md). Own code:
adapter group `damage_data`, appended to `core/src/damage.c` (same
`es2_weapon_graph` bridge as `es2_vehicle_part_registers`).

| Hook | VA | Role | Callers |
|---|---|---|---|
| `dmg_load` | `0x40d160` | DMG file of a type (by name) into the next slot of `[0x4a9828]` | `mech_ctor` `0x415f98`, FLYER type load `0x422ed0` (`0x422f4f`) |
| `dmg_read` | `0x40d09c` | capacities, then regions | `dmg_load` |
| `dmg_region_read` | `0x40cff8` | one 18-byte region: 8-byte prefix, link list, `+0Ch` = −1, `+0Eh` = 0 | `dmg_read`, weapon-list entry `0x40f8bc` |
| `dmg_region_copy` | `0x40d050` | region copy with its own link list copy | `dmg_region_set` |
| `dmg_capacity_set` | `0x40d128` | n capacity words by index list | `weapon_fit` `0x4177c1` |
| `dmg_region_set` | `0x40d240` | n regions replaced by index list | `weapon_fit` `0x4177a8` |
| `dmg_part_store` | `0x40d284` | part word and center pointer of n regions (`+0Ch`, `+0Eh`) | `weapon_fit` `0x41781d` |
| `col_part_collect` | `0x40cb84` | COL records' volume ids, center pointers, owner words into three buffers | `weapon_fit` `0x4177f8` |
| `col_part_gather` | `0x40cafc` | the same for one COL record (no call, no allocation) | `col_part_collect` |
| `damage_state_ctor` | `0x40d2cc` | actor damage state: three word arrays, two counts | `mech_ctor` `0x415c6b`, `flyer_ctor` `0x42169d` |
| `damage_state_bind` | `0x40d354` | DMG, COL, owner into the state; shared 20-word block `[0x498864]` once | `mech_ctor` `0x415fd3`, `flyer_ctor` `0x42171c` |

Other callees stay game code behind the guest bridge: runtime `0x49630c`,
`new` `0x47749c`, `new[]` `0x4774f0`, `_memcpy` `0x49680e`, `_memset`
`0x49681a`; VOL-aware stream constructor `0x472f14`; error report
`0x479c78`; the stream read method (slot +18h; `0x4735b8` for table
`0x4a0e10`); hook `stream_destroy` `0x472f64`. Internal calls
(`dmg_load` → `dmg_read` → `dmg_region_read`, `dmg_region_set` →
`dmg_region_copy`, `col_part_collect` → `col_part_gather`) go through the
bridge, so a nested hook keeps its mode.

### Call path

All eleven run during the load only: `dmg_load` and the damage state from
the constructors (L5, section 17) and the pass-1 FLYER type load; the rest
from `weapon_fit` (L4, section 16) and the stage-2 weapon list
(`0x40fc8c` → `0x40f8bc`, section 4). No other call site; no dword of the
file holds one of the eleven addresses.

### Contracts (from the bytes)

All cdecl; EDX:EAX reproduced exactly (the core follows the registers so a
game call sees the original's entry EAX/EDX/ECX). "sx" = sign extension of
a word; counts compare as signed words (`CMP r16,[...]`, `JL`/`JGE`), loop
counters are 32-bit. Words and pointers are reread where the original
rereads them (noted), so writes through aliasing outputs are observed in
order.

**ECX is dead at every call site** (forward walk, both arms), so the
framework's entry ECX stands in; the core still reports the original's.

No function reads a local it did not write. `col_part_gather` and
`dmg_part_store` advance their own argument slots (`ADD [EBP+0Ch],2` …);
callers release them with `ADD ESP` unread, so the replacements leave them
unchanged. No entry or exit shim.

**`dmg_load` 0x40d160 (name)**, `add esp,-50h`: descriptor `0x498840` (+4
= −50h), record EBP−50h, context word EBP−40h; `0x49630c` with EAX = the
descriptor and entry EDX/ECX; context = 8. Stream S = EBP−30h (30h bytes:
vtable +0, state +4, handle +8 … +2Eh, written by constructor or open)
via `0x472f14(S, name)` (EDX = S). S+4 ≠ 0 (open failed): `0x479c78
(0x6c56c8, 5, 0, 0x498858 "damage.cpp", 85h, name)` with EDX = 0 (`SETE
DL` of nonzero EAX, `AND EDX,1`), EAX = S+4. Slot counter word
`[0x498854]` read into CX (high word kept), incremented in memory
(reread); slot = `12 × sx(counter) + [0x4a9828]` (array read after the
increment); `dmg_read(slot, S)` with EAX = S; `stream_destroy(S, 2)` with
EAX = slot, EDX = S. Returns EAX = slot, EDX = the record's saved head
(epilogue writes it to FS:0; the adapter does), ECX = `stream_destroy`'s.
The slot array comes from `0x40d218` (12 × `[0x498856]`, reserved by the
type loads through `0x40d200`); `dmg_load` does not check the bound.

**`dmg_read` 0x40d09c (T, S)**, T the 12-byte descriptor (regions +0,
capacities +4, region count +8, capacity count +0Ah). Each read via S's
slot +18h (S, size, buffer), EDX = S, ECX = the table reread from S: read(2)
→ T+0Ah (EAX = T+0Ah); `new[](2 × sx(T+0Ah))` (ECX after = size) → T+4;
read(2 × sx(T+0Ah) reread, block) (EAX = size); read(2) → T+8 (EAX = T+8);
`new[](18 × sx(T+8))` with EAX = sx, EDX = 18 × sx → T+0;
`dmg_region_read(block + 18k, S)` (EAX = S) while k < sx(T+8) (reread).
Returns the last `dmg_region_read`'s registers; with no region: EAX = the
block, `new[]`'s EDX, ECX = 18 × sx(count).

**`dmg_region_read` 0x40cff8 (R, S)**: read(S, 8, R) (EAX = table, entry
EDX/ECX); L = word R+6 (once, DI); L ≠ 0: `new[](4 × sx(L))` (ECX = size
after) → R+8, read(S, 4 × sx(R+6), [R+8]) both reread, EDX = [R+8], ECX =
size, EAX = table reread. Then word R+0Ch = FFFFh, dword R+0Eh = 0 (EDX =
0). Returns the last read's EAX and ECX, EDX = 0.

**`dmg_region_copy` 0x40d050 (D, R)**: `_memcpy(D, R, 12h)` with entry
registers (the preceding `CMP WORD [D+6],0` is dead, flags untested; the
core keeps the read); `new[](4 × sx(D+6))` (ECX = size after) → D+8;
`_memcpy(block, [R+8], 4 × sx(D+6) reread)`, EAX = size, EDX = [R+8].
Returns the second `_memcpy`'s registers.

**`dmg_capacity_set` 0x40d128 (T, I, V, n)**: for k < n: word V[k] into
`[T+4]` + 2 × sx(I[k]) (read I[k], [T+4] reread, V[k]). Returns EAX =
passes (0 for n ≤ 0), EDX = V + 2k, ECX = I + 2k.

**`dmg_region_set` 0x40d240 (T, I, P, n)**: for k < n:
`dmg_region_copy([T] + 18 × sx(I[k]), P[k])` (read P[k], I[k], [T]
reread), EAX = sx(I[k]), EDX = 18 × sx(I[k]), ECX = the region. Returns the
last call's registers, or the entry ones for n ≤ 0.

**`dmg_part_store` 0x40d284 (T, n, I, W, Q)**: for k < n: region = [T] +
18 × sx(I[k]); word +0Ch = W[k], dword +0Eh = Q[k] (read order I[k], Q[k],
W[k], [T]). Returns EAX = last region (entry EAX for n ≤ 0), EDX =
passes, ECX = last Q[k] (entry ECX for n ≤ 0).

**`col_part_gather` 0x40cafc (C, I, O, P)**, C a 10-byte COL record (owner
word +0, count +4, entries +6, 16 bytes each): for k < [C+4] (reread):
I[k] = entry word +6, P[k] = entry + 8, O[k] = word C+0 (reread), in this
order. Returns EAX = high word of entries + 16k with low word = C+4
(reread), EDX = C, ECX = k.

**`col_part_collect` 0x40cb84 (L, I, O, P)**, L a COL list (count word
+0, 10-byte records +2): b = 0 (EBX, only BX changes); for k < [L+0]
(reread): `col_part_gather(record k, I + 2 sx(b), O + 2 sx(b), P + 4
sx(b))` with EAX/EDX/ECX = third, second, first pointer; b += AX (16
bits). Returns EAX = b (high word 0), EDX/ECX the last call's (entry ones
for an empty L).

**`damage_state_ctor` 0x40d2cc (D, r, c)**, D = actor +206h for a mech (r
= 1Dh regions, c = 16h capacities), +200h for a flyer (1, 1); low words
of r, c only. `new[](2 sx(c))` → D+0, `new[](2 sx(r))` → D+4, `new[](2
sx(r))` → D+8 (EAX, EDX, ECX: size, previous block, previous size after
`POP ECX`); `_memset([D], 0, 2 sx(c))`, `_memset([D+4], 0, 2 sx(r))`,
`_memset([D+8], 1, 2 sx(r))`, each pointer reread (the third array holds
bytes 01h, not zero); words D+18h = r, D+1Ah = c. Returns EAX = D,
EDX/ECX the last `_memset`'s.

**`damage_state_bind` 0x40d354 (D, A, M, C)**, `add esp,-24h`: descriptor
`0x498878` (+4 = −24h), record EBP−24h, context word EBP−14h. D+0Ch = M
(DMG), D+10h = C (COL), D+14h = A (owner). If dword `[0x498864]` is 0:
`new(6)` (EAX = A, EDX = M, ECX = C; ECX = 6 after) → EBP−4; if not null:
context 14h, byte +4 = 14h (CL), `new[](28h)` (EDX = object, ECX = 14h) →
+0, byte +5 = 0, construction count `[[0x6cbee4]]` incremented and
decremented (pointer reread each time), context 8; `[0x498864]` = the
object, or 0 when `new` failed (the next bind retries). Returns EAX = A
(block present), 0 (`new` failed) or the object; EDX = saved head (FS:0,
by the adapter); ECX = C, 6 or `[0x6cbee4]`.

### In vitro (group `damage_data`)

`invitro/damage_data_cases.h`: 100,000 quick direct cases per hook
(1,001,024 full) against logging stubs (`0x472f14` fails one in four);
overlapping objects and stubs that rewrite reread words make the
read/write order observable; globals `0x498854`, `0x4a9828`, `0x498864`,
`0x6cbee4` compared; adapter in every case. 256 installed 31/a7 cases;
real-callee passes (2,048) for the eight hooks with callees, nested hooks
in rotating modes. 13 branch sites both ways; one caught mutation per hook
(`MUTATIONS.md`); `//core:damage_data_test`.

### Live

Predicted `hk_<name>_calls` per suite: the entry counts of
the DBSIM map (C1_01 suites:
`dmg_load` 11, `dmg_region_read` 324, `damage_state_*` 14, region/capacity
set and copy 31, `col_part_collect`/`dmg_part_store` 10,
`col_part_gather` 66). Identities in every suite: `dmg_load` = `mech_ctor`
+ the FLYER type load `0x422ed0` (1 where a flyer type loads);
`damage_state_ctor` = `damage_state_bind` = `mech_ctor` + `flyer_ctor`;
`col_part_collect` = `dmg_part_store` = `weapon_fit`; `dmg_region_set` =
`dmg_capacity_set` = `dmg_region_copy` (n = 1 at both sites);
`dmg_region_read` = regions of the loaded DMG files + 33 weapon-list
entries. `_call_479c78` 0 (every DMG file opens);
`hk_damage_state_bind_call_47749c` = `_call_4774f0` = 1 (`[0x498864]` is
0 in the file, written only at `0x40d3d6`). Callers' counters and
`hk_stream_destroy_calls` unchanged.

A difference would lie in the DMG descriptors (`[0x4a9828]`, their blocks
and link lists), the damage states (actor +206h/+200h and arrays), the
block `[0x498864]`, the region words +0Ch/+0Eh, or the block pairing (same
sizes, same order, same `new`/`new[]`).

Result: the same run as section 21: identical, the eleven
counters equal the Live counts table, after-load snapshot identical.

### Open

- `hk_dmg_region_read_slot_18` and `_call_4774f0` per suite (link counts of
  the DMG files and WEAPONS.DAT entries) are not predicted.
- The `dmg_load` error path (`0x479c78`, missing DMG file) is covered in
  vitro only.
- `0x40d200`/`0x40d218` (slot reservation, slot array) and
  `0x40cbd4`/`0x40cbec` (the same for COL) stay original (called in
  sections 26 and 29).
## 23. Group joins and the building footprint

The three join methods `group_ctor` (section 17) calls, the building
footprint marks and the cell flag writer. Own code: adapter group
`group_joins`, core `core/src/group_joins.c`.

| Hook | VA | Call sites | Does |
|---|---|---|---|
| `mech_join` | `0x417aa8` | `0x423c6c` (`group_ctor`, class 0) | group and slot into the actor; places a MECH still at the load's defaults |
| `flyer_join` | `0x421ee8` | `0x423cc3` (class 1) | the same for a FLYER, no height term |
| `building_join` | `0x405c3c` | `0x423d1e` (class 2) | the same for a building, with base-layout angle, grid and footprint |
| `footprint_marks` | `0x470ed4` | `0x405d79` (`building_join`), `0x425d08` (actor load: buildings with a coordinate) | cell flags within a radius of a point |
| `cell_flag_set` | `0x470ddc` | `0x470ed4` (5 sites), `0x47108c`, `0x4711da` (flattening, section 27), `0x46e61a` (per-frame cell clear `0x46e5a4`, H3) | one cell flag byte |

All callees are hooks, called through the bridge: `route_point`,
`terrain_height`, `dist_gate`, formation slot +78h (`formation_mech`
`0x417898`, `formation_flyer` `0x421e98`, `formation_base` `0x405c04`),
slot +10h (`shape_word` `0x46b910`), and each other. `cell_flag_set` is
34 bytes, long enough for a hook (`push ebp`, `mov ebp,esp`, `mov
eax,[ebp+8]` relocated).

Joins and `footprint_marks` run during the load only (counters constant
from the first tick record); `cell_flag_set` also runs every frame under
the render root (`0x46e5a4` after the cell's objects are drawn).

### Contracts (from the bytes)

All cdecl. "sx" = sign extension of a word; "lo(R, w)" = a 16-bit load into
R keeping R's high word.

**`mech_join` 0x417aa8 (A, G, slot)**, `add esp,-14h`: vector v at EBP−14h
(ESI); angle staging dword EBP−8, word EBP−4 (no callee sees it).

1. [A+45h] = G (EAX = G); word [A+49h] = slot (EDX = lo(entry EDX, slot)).
2. Word [A+0Ch] = 8000h (default angle): ECX = [A+45h], AX = word
   [ECX+0Ah] (group heading); dword [A+0Ch] = 0, word [A+10h] = heading,
   word [A+32h] = 0 (matrix stale). EAX = A+0Ch, EDX = 0:heading.
3. [A+2Eh] = FFFF8000h (default height): EAX = [A+45h], EDX = [EAX+18h]
   (group point). Not null: v = the point, read +8, +0, +4 (EDX, ECX, EAX)
   and written in that order. Null: `route_point(G+4, EDX)` with EAX =
   G+4, EDX = 0:word [G+4], ECX as left (G after step 2, else entry ECX);
   v = the returned point, read +8 (ECX), +0 (EDX), +4 (EAX).
4. `terrain_height([4A0BE0h], v)` (EAX = v.y, EDX = v.x or v.z, ECX = the
   terrain); v.z = its EAX; EAX = T = [A+1F2h] (type record, reread),
   EDX = sx(word [T+16h]), v.z += EDX; word [T+50h] ≠ 0: v.z += 1388h.
5. `[[A]+78h](A, v)` (EAX = T, EDX = sx(+16h), ECX = vtable).
6. A+26h = v: EAX = A+26h; [A+2Eh] = v.z (EDX), [A+26h] = v.x (ECX),
   [A+2Ah] = v.y (EDX). EDX:EAX = v.y : A+26h, ECX = v.x.

Without step 3, EDX:EAX is step 2's, or G and lo(entry EDX, slot).

**`flyer_join` 0x421ee8 (F, G, slot)**: steps 1–3, 5, 6 of `mech_join`
(identical code; at the slot call EAX = v.y, EDX = v.x or v.z), then on
every path [F+2Eh] = 0 becomes 1388h.

**`building_join` 0x405c3c (B, G, slot, snap)**, `add esp,-18h`: v at
EBP−18h, grid x' at EBP−0Ch meanwhile.

1. As `mech_join`.
2. Word [B+0Ch] = 8000h: ECX = [B+45h], EAX = sx(word [ECX+0Ah]); slot
   (DX) ≠ 0: EAX += sx(word [[L0+4] + 10 × sx(slot) − 2]), L0 =
   [4A95ACh] + 1Ch × sx(word [G+2]) (the group's base layout; ECX ends as
   that word sign-extended; ESI, holding the table, restored at return).
   Words as in `mech_join` with this angle.
3. [B+2Eh] = FFFF8000h: point as in `mech_join` with other registers
   (point: ECX = z, EDX = x, EAX = y; `route_point`'s second slot ECX =
   lo(ECX, word [G+4]), high word from step 2's ECX or the entry ECX; its
   point: EDX = z, ECX = x, EAX = y).
4. Dword snap ≠ 0 (`group_ctor` passes sx(word R+0)): L = [4A95ACh] + 1Ch
   × sx(word [G+2]) (G reread from B+45h, table reread); s = [L+14h] << 5,
   m = (s << 8) − 1; x' = (v.x AND NOT m) + [L+0Ch] × s; y' = (v.y AND NOT
   m) + m + 1 − low([L+10h] × s) (one-operand IMUL, signed EDX:EAX); v.x =
   x' (EDX), v.y = y' (EAX). All 32-bit wrap-around.
5. `[[B]+78h](B, v)` (EAX = vtable, ECX = v); `[[B]+10h](B)` (EDX = vtable
   reread; `pop ecx` = B); ECX = sx(AX), the shape word (radius).
6. `footprint_marks([4A0BE0h], v, ECX)` (EAX = v, EDX = terrain);
   `terrain_height([4A0BE0h], v)` (EAX = terrain, ECX = v, EDX =
   `footprint_marks`' EDX).
7. v.z = [B+2Eh] = its EAX, [B+26h] = v.x (EDX), [B+2Ah] = v.y (ECX).
   EDX:EAX = v.x : B+26h, ECX = v.y.

**`footprint_marks` 0x470ed4 (T, P, r)**, `add esp,-1Ch`: box bounds at
EBP−4..−10h (no callee sees them), corner point (x, y, z) at EBP−1Ch.

1. Reads [P] (x), [T+108h] (s), [P+4] (y), [P+8] (z, the point's z); x0 =
   (x − r) SAR s, x1 = (x + r) SAR s, y0, y1 likewise (CL: low five bits
   count).
2. `cell_flag_set(T, x SAR s, y SAR s, 1)` (EAX = ECX = s, EDX = x SAR s).
3. Rows j = y0..y1, columns i = x0..x1 (signed, inclusive, empty when start
   > end; [T+108h] reread per row and column): point = (i << s, j << s,
   z); `dist_gate(point, P)` (EAX = point, EDX = P, ECX = [T+108h]); its
   EAX (signed) ≤ r: `cell_flag_set(T, i, j, 1)`; i > 0: also (i − 1, j)
   (EDX = i − 1) and, j > 0, (i − 1, j − 1) (EAX = i − 1, ECX = j − 1); j >
   0: also (i, j − 1) (EDX = j − 1). Registers are the last call's, or the
   loops' shifts (EAX = j << s for an empty column range).

**`cell_flag_set` 0x470ddc (T, x, y, v)**: c = [T+100h], cells [T+0F0h];
byte [cells + (y << c) + x] = v (SHL by CL). EAX = cells, EDX = index, ECX
= c with v in its low byte.

**Callers' registers.** ECX is dead after every site, so the entry ECX
stands in. No function reads an unwritten local; formation slots,
`dist_gate` and `terrain_height` read only the 12 vector bytes. No shim;
the adapters keep the vector in a zeroed scratch.

**Compare:** refused for all five (compare_mode.md): bridge calls write
actor and terrain cells; `cell_flag_set` writes one byte at a runtime
index (effort M).

### In vitro (group `group_joins`)

`invitro/group_joins_cases.h`: 100,000 quick direct cases per hook
against logging stubs that change what the originals
reread; adapter in even cases (slot 0..19, snap 0/1). 256 installed 31/a7
cases; real-callee passes (4,096 per hook except the leaf
`cell_flag_set`) over the real callees (8 × 8 terrain grid), nested hooks
in all mode combinations. 21 branch sites both ways; one caught mutation
per hook (MUTATIONS.md), three of four ad-hoc core mutants (the fourth
equivalent); `//core:group_joins_test`.

### Live

A join runs per member as section 17 counts (`hk_group_ctor_call_417aa8`
etc.) and places (route_point when the group has no point,
record word +2Ch < 0; slot +78h; MECH and building `terrain_height`;
building +10h and `footprint_marks`) only members still at the default
height: every MEC actor and authored members with a negative coordinate
index (mech +3Eh, flyer +28h, building +2), once per actor.
`footprint_marks` = building placements + actor-load buildings with a
coordinate (`0x425d08`); it equals the entries of `0x470ed4` in
the DBSIM map in all 40 suites, and building placements equal
`hk_formation_base_calls` where a building's slot +78h is
its only caller (C1_08, C2_02, C3_09, C3_10, C4_05, C4_08, C4_10, C5_05,
train1). Per suite: C1_01: 10 / 4 /
50 / 50; train1-timers: 1 / 0 / 11 / 11. `hk_cell_flag_set_calls` grows
per frame (map probe saturates at 1000), not predicted exactly.
`footprint_marks_call_470ddc`/`_call_49288c` depend on radii and
positions. Callee hooks' counters unchanged.

A difference would lie in an actor pool block (A+26h..31h, A+0Ch..11h,
A+32h, A+45h..4Ah), the cell flags [[4A0BE0h]+0F0h], or the formation
static buffers `0x49a430`/`0x4975d6`.

Result: every hook replaced, own runtime:
all suites identical, all skipped/bypassed/mismatch 0, counters equal the
table; `cell_flag_set` 1,670 (train1-timers) to 132,745 (c3_09-loadout-b).
After-load snapshot: O1 = O2 and O2 vs R identical
(all and pools) in acceptance-60s, c3_10-loadout-hercs, c3_09-loadout-b.

### Open

- Per-suite cell counts of `footprint_marks` (its callee counters).
- `cell_flag_set`'s per-frame callers stay original (H3 cell clear
  `0x46e5a4`) or are own code in section 27 (`0x470fe8`, `0x471128`).
## 24. Terrain zone load

Contracts, in-vitro evidence and data facts:
[terrain.md](terrain.md#the-terrain-zone-load).

Six hooks in group `terrain_load`: `zone_load` `0x42789c`, `grid_ctor`
`0x46befc`, `height_cache_ctor` `0x495268`, `terrain_materials`
`0x46c754`, `terrain_cells` `0x46c4c4`, `cell_mark_set` `0x470d74`. Step
10 of pass 1 (section 2): `0x424308` calls `zone_load(word 0x4a9e50, word
0x4a9e4e)` at `0x4252f7` (second argument unread): zone file
(`dat\zone<N>.dat`: two unused dwords, cell shift, height scale), grid
object, materials (`dat\mat0.dat`), cells from the first bitmap of
`dba\zone<N>.dba` with one stream-A draw per block, zone transform
`0x4aaa84` (`euler_matrix`), terrain pointer `[0x4a0be0]`. `0x42789c`
writes the object table `0x4aaa74`/`0x4aaa78`/word `0x4aaa7c` only for a
negative zone word (cleared). Section 6's first-row RNG draws are
`0x46c4c4`'s alone (`0x46c754`'s draw site `0x46cac0` is in the text-grid
path, which no suite runs).

### Live

DBSIM map (all 40 suites): `0x42789c`, `0x46befc`, `0x495268`,
`0x46c754`, `0x46c4c4` once each; `0x470d74` reads 1,000 (cap). Its count
= cells (one call each from `0x46c4c4`) + base marks (one per nonzero
layout byte from `0x47136c`, which `base_marks` calls per base group whose
form has a material index), computed from the launch
sets' `script.dat` (zone = header word 1; installed C1_01 from the
launcher chain's `data-backup`), zone DBA dimensions and `dat\BFORMS.DAT`
(17 forms; 5, 7..16 have marks); its `base_marks`/`0x47136c` counts equal
the map's. Per suite (C1_01:
`cell_mark_set` 65,688, `terrain_cells_call_492ee0` 16,384; C3_09:
16,425 / 1,024). Callee hooks' counters unchanged (`hk_rng_next_*` stay
within the earlier counts, `euler_matrix`, `stream_read`, `dts_load`,
`stream_destroy`, `base_marks_*`).

**Snapshot reach.** Reached: `0x4a0be0` → grid G, and through it the cells
`[G+0ECh]`, marks `[G+0F0h]`, material table `[G+121h]` (unaligned
window, followed by the pairing), height cache arrays `[G+0E0h]`,
`[G+0E4h]`, `[G+0E8h]` (new[] goes to the CW3220 heap unless `-l` sets the
arena threshold). Not reached by the fixed regions (they end at
`0x4aaa7c`): zone transform `0x4aaa84..0x4aaaa3`, detail words
`0x4aaaa8`/`0x4aaaac`, count word `0x4aaa7c`, second grid pointer
`0x49aac8`, `0x49aad0` (20000), first-use byte `0x49aaf0`; the live run
adds them with `[snapshot] extra = 0x4aaa7c:0x34,0x49aac8:0x29`.

Result: every hook replaced, own
runtime: all suites identical, all skipped/bypassed/mismatch 0, counters
equal the table (`terrain_materials_call_4735b8` 13); `hk_rng_next_calls`
and `hk_rng_bounded_calls` equal those of section 23's run. After-load snapshot
with that extra (15 fixed regions, written by
`configure-tick-log.ps1 -AfterLoadSnapshotExtra`): O1 = O2 and O2 vs R
identical (all and pools) in acceptance-60s, c3_10-loadout-hercs,
c3_09-loadout-b. Byte 19 of the zone matrix (`0x4aaa97`) is written by
neither run (matrix padding).

### Open

- `0x4952e8` (height cache offset/scale setter, also per frame from the H3
  terrain draw `0x46cb9c`) is H3 in function_verdicts.md and stays a guest
  call, like the reset `0x4952fc`.
- The text grid of `0x46c754` writes marks into `[G+0F0h]`, which only the
  bitmap path allocates; no suite runs it; reproduced as it stands.
## 25. Stage-2 callbacks with type data

The six type-data callbacks of section 4 and the weapon-list entry reader
(the six descriptors, file and template strings, name tables
`0x498eb0`/`0x49a360` and step table `0x498640` read from the file). Own
code: adapter group `stage2_types`, core `core/src/stage2_types.c` (stream
readers on the weapons guest bridge, like section 20's `collision_data`).

| Hook | VA | Record | Builds |
|---|---|---:|---|
| `rocket_types` | `0x40a818` | 11 | `rockets.dat` records (`0x4a96d0`, count `0x4a96d4`), word `0x4a96dc` = max(0, max record word +0 + 1), `rockets.dts` shapes (`0x4a96d8`) |
| `bullet_types` | `0x40ade0` | 13 | flag `0x498514`; `bullets.dat` records, max word `0x4a9704`, `bullets.dts` shapes, `bullets.dba` bitmap and texture table |
| `beam_types` | `0x40b6e0` | 14 | 120 step words `0x4a9712..0x4a9801`; `beam.dat` 6-byte records; `beamtex.dba` texture table (not the bitmap) |
| `weapon_types` | `0x40fc8c` | 16 | `wpntex.dba` texture table; `mechwpn2.dts` shapes; `weapons.dat` 58h-byte entries with name +52h, index +56h; `proj.dat` 36-byte records |
| `weapon_entry` | `0x40f8bc` | — | one weapon-list entry |
| `mech_forms` | `0x41fdb0` | 21 | `mforms.dat` 28-byte forms; texture tables of seven bitmaps named by `[0x49a360 + 4k]`: `light`, `medium`, `heavy`, `enemy`, `apocatex`, `razortex`, `newhercs` |
| `flyer_forms` | `0x422d90` | 23 | `fforms.dat` 18-byte forms |

These extend section 4 and the verdict rows of function_verdicts.md.
File names come from `0x492bec(template, extension)` with `[0x4a09f0]`
(`dat`), `[0x4a09f8]` (`dts`), `[0x4a0a10]` (`dba`); error reports name
`rocket.cpp`, `bullet.cpp`, `BEAM.CPP`, `GUNLIST.CPP`, `mechsys.cpp`,
`flyersys.cpp`.

### Call path

Each callback is registered once by `0x401d64(fn, 2)` at `0x40ab1a`,
`0x40b5da`, `0x40c2c2`, `0x411112`, `0x420656`, `0x42306a` (static init
records 11, 13, 14, 16, 21, 23); `weapon_entry`'s only site is `0x40fdc8`
in `weapon_types`. No other dword or `call`/`jmp rel32` refers to the
seven. `0x401d94` calls each stage-2 entry once per process via `call
[ebx]` at `0x401dac` (section 15): each callback runs once per suite,
`weapon_entry` once per entry (map: 1 each and 33 for `0x40f8bc`).

### Contracts (from the bytes)

All seven cdecl with a `push ebp` frame; EDX:EAX reproduced exactly. The
callbacks take no argument, start with `add esp,-50h` (beam_types `-54h`)
and call `0x49630c` with EAX = the descriptor (`0x49834c`, `0x498525`,
`0x498658`, `0x498f8d`, `0x49a38c`, `0x49a678`; each −50h at +4: record
EBP−50h, context word EBP−40h) and entry EDX/ECX; `0x49630c` reads only
EAX and keeps EDX/ECX, leaving EAX = old FS:0 head. The epilogue loads
the saved head from EBP−50h into EAX (rocket, bullet, weapon_types) or EDX
(beam_types, mech_forms, flyer_forms) and stores it to FS:0; the adapter
does the same from the returned register.

**The open** (all six, after the path call): N = `0x492bec`'s EAX;
context = 8; `0x472f14(S, N)`, S = EBP−30h, EAX = S, EDX/ECX the path
builder's; EAX = status [S+4]; nonzero: `0x479c78(0x6c56c8, 5, 0, file,
line, N)` with EDX = 0 (`SETE DL`, `AND EDX,1`), lines E6h, 32h, 30h, 10Bh,
34h, 26h. "Read(n, buf)" = `stream_read 0x4735b8(S, n, buf)` direct;
"read(n, buf)" = virtual +18h through [S] reread; s16 = word sign
extension.

**`rocket_types` 0x40a818** (ESI = `0x4a96dc`): path (`rockets`, EDX =
[`0x4a09f0`]); open; Read(2, `0x4a96d4`), ECX = S; EAX = s16(count), EDX =
14 × EAX, `new[](EDX)`, `POP ECX` = size → `0x4a96d0`; Read(14 ×
s16(count reread), block), EAX = s16, EDX = size, ECX = S;
`stream_destroy(S, 2)`, EAX = S. Word `0x4a96dc` = 0; ECX = 0, EDX =
[`0x4a96d0`]; while CX < count word (signed 16-bit `CMP CX,[0x4a96d4]`,
reread): `MOV AX,[EDX]` (EAX high word kept), `INC EAX` (32 bits); BX = AX
if AX ≥ word `0x4a96dc` (signed) else that word (reread); word ← BX; ECX
+= 1, EDX += 14. Path (`rockets`, EAX = [`0x4a09f8`], EDX/ECX the loop's),
`0x474cd8(0x4a96d8, N, 0x140008, 0)` (EAX = N). Returns EAX = head,
EDX/ECX `0x474cd8`'s.

**`bullet_types` 0x40ade0** (ESI = `0x4a9704`): byte `0x498514` = 0 → `INC
BYTE` (read again); then as rocket_types with `bullets`, `0x4a96fc`,
`0x4a96f8`, except the max loop: EAX = 0, ECX = records, test loads EDX =
s16(count) (reread), 32-bit `JL`; body `MOV DX,[ECX]`, `INC EDX`, same max
into `0x4a9704`, EAX += 1, ECX += 14. Path (`bullets`, EAX = [`0x4a09f8`],
EDX = s16(count), ECX = records + 14k), `0x474cd8(0x4a9700, N, 0x140008,
0)`, EBX = its EAX; path (`bullets`, EAX = [`0x4a0a10`]); `0x45cdf4(N)`,
`POP ECX` = N; bitmap → `0x4a9706`; `0x46a03c(bitmap)` (ECX = N), `POP
ECX` = bitmap; its EAX → `0x4a970a`; EAX = 0; while EAX < s16(BX): ECX =
[[`0x4a9700`] + 4 EAX] (list reread), EAX += 1, [ECX+26h] = `0x4a9706`.
Returns EAX = head, EDX = s16(BX), ECX = last shape (or the bitmap if
none).

**`beam_types` 0x40b6e0**: for i = 0..119 (AX vs 78h): ECX = s16(i), EBX =
ECX SAR 1, ECX &= 3, BX += word [`0x498640` + 2 ECX] (1, 0, 1, 2 in the
file), word [`0x4a9712` + 2i] = BX; after: EAX = 78h, EDX = `0x4a9802`, ECX
= 3. Path (`beam`, EAX = [`0x4a09f0`]); open; read(2, EBP−52h), EAX = S,
EDX = [S], ECX = EBP−52h; ECX = s16(local), `new[](6 × ECX)` (EAX =
size), `POP ECX` = size → `0x4a9804`; Read(6 × s16(local reread), block),
EAX = s16, EDX = size, ECX = S; `stream_destroy(S, 2)`; path (`beamtex`,
EDX = [`0x4a0a10`]); `0x45cdf4(N)`, `POP ECX` = N; EBX = bitmap;
`0x46a03c(bitmap)`, `POP ECX` = bitmap; its EAX → `0x4a9808`;
[[`0x6cbee4`]] += 1 (EAX = pointer); `0x48349c(bitmap, 3)`
(`GLBitmapArray` destructor). Returns EAX/ECX `0x48349c`'s, EDX = head.
EBP−52h is the only local the function does not write itself (the read
fills it with `beam.dat`'s first 2 bytes); a shorter file would leave the
stack word, not modelled (the core's frame scratch is zeroed).

**`weapon_types` 0x40fc8c** (ESI = `0x4a9ae8` until the weapon list):
path (`wpntex`, EDX = [`0x4a0a10`]); `0x45cdf4(N)`, `POP ECX` = N; EBX =
[`0x4a9ae8`] = bitmap; `0x46a03c(bitmap)`, `POP ECX` = bitmap;
[`0x4a9aec`] = its EAX; [[`0x6cbee4`]] += 1; `0x48349c([0x4a9ae8] reread,
3)` (EAX = counter pointer); ECX = 0, [`0x4a9ae8`] = 0. Path (`mechwpn2`,
EAX = template, EDX = [`0x4a09f8`], ECX = 0); `0x474cd8(0x4a98f4, N,
0x140008, 0)`; word `0x4a98f8` = its AX; for k < s16([`0x4a98f8`])
(reread, ECX = it): EDX = [[`0x4a98f4`] + 4k] (reread), [EDX+26h] =
`0x4a9ae8`. Path (`weapons`, EAX = [`0x4a09f0`], EDX = last shape or
`0x474cd8`'s, ECX = s16(count)); open; Read(2, `0x4a98f0`);
`0x49667c(0, 58h, s16(count), 1, 0x41178d)` (EAX = s16); ESI = block →
`0x4a98ec`; EDI = `0x498eb0`; for k < s16([`0x4a98f0`]) (reread, ECX =
it): `weapon_entry(ESI, S)` with EAX = S, then EDX = [EDI], EDI += 4,
[ESI+52h] = EDX, word [ESI+56h] = k, ESI += 58h. `stream_close
0x47307c(S)` (EAX = S), `POP ECX` = S; path (`proj`, EDX = [`0x4a09f0`]);
`stream_open 0x4730e8(S, N)` (EAX = S); read(2, `0x4a9900`) with EAX =
`0x4730e8`'s, EDX = S, ECX = [S]; n = s16(count) (read once),
`new[](36n)` (EAX = 36n), `POP ECX` = 36n → `0x4a98fc`; Read(s16(36n as
a word), block), EDX = S, ECX = 36n; `stream_destroy(S, 2)` (ECX = S).
Returns EAX = head, EDX/ECX `stream_destroy`'s.

**`weapon_entry` 0x40f8bc (E, S)**: `dmg_region_read(E, S)` with entry
registers; `collision_volume(E+12h, S)`, EAX = E+12h; read(30h, E+22h),
EAX = E+22h, EDX = 30h (`MOV DI,30h; MOVSX EDX,DI`), ECX = [S]; EAX = AL
of the read; word [E+48h] = 0 (inside the bytes just read). Returns EAX =
0..FFh, EDX/ECX the read's. Entry layout: DMG region +0 (18 bytes, section
22), COL volume +12h (16 bytes, section 20), 30h file bytes +22h with word
+48h cleared, name pointer +52h, index word +56h.

**`mech_forms` 0x41fdb0**: `_memset(0x4a9d08, FFh, 55h)` (EAX = head,
entry EDX/ECX); word `0x4a9d06` = 0; path (`mforms`, EDX = [`0x4a09f0`]);
open; Read(2, `0x4a9d68`); `0x49667c(0, 1Ch, s16(count), 1, 0x4207bb)` →
`0x4a9d6c`; EDX = s16(count reread), EAX = (8 EDX − EDX) × 4,
Read(s16(AX), block), EAX = S, ECX = s16(AX). Seven times (DI vs 7; EBX =
`0x4a9d72` + 8k, ESI = `0x49a360` + 4k): path ([ESI], EAX = [`0x4a0a10`],
EDX = [ESI]; ECX = the read's in pass 1, then 0); `0x45cdf4(N)`, `POP
ECX`; [EBX] = bitmap, ECX = [EBX] (reread); `0x46a03c(ECX)`, `POP ECX`;
[EBX+4] = its EAX; [[`0x6cbee4`]] += 1; `0x48349c([EBX] reread, 3)`; ECX
= 0, [EBX] = 0. Then `stream_destroy(S, 2)` (EAX = S, ECX = 0). Returns
EAX/ECX `stream_destroy`'s, EDX = head.

**`flyer_forms` 0x422d90**: `_memset(0x4a9dcc, FFh, 22h)`; word
`0x4a9dca` = 0; path (`fforms`); open; Read(2, `0x4a9df8`); `0x49667c(0,
12h, s16(count), 1, 0x423078)` → `0x4a9dfc`; EAX = 18 × s16(count
reread), Read(s16(AX), block), EDX = s16(AX), ECX = S; `stream_destroy(S,
2)`. Returns EAX/ECX `stream_destroy`'s, EDX = head.

**Element constructors** (only via `0x49667c`, original): `0x41178d`
computes three addresses into registers it restores and returns its
argument (empty); `0x4207bb` builds 7 four-byte elements
(`0x49667c(form, 4, 7, 1, 0x492428)`), `0x423078` 3 six-byte elements
(`0x49667c(form, 6, 3, 1, 0x4922d3)`); `0x492428`, `0x4922d3` return their
argument.

**ECX after the calls.** The framework returns the entry ECX; the core
still reports the original's. `0x401d94` reads no register after `call
[ebx]` (`0x401dac`) and each next stage-2 entry writes ECX before reading
it; after `weapon_entry` (`0x40fdcd`) the caller writes EDX, ECX
(`0x40fde0`) and EAX before reading them.

**Frames, residue, shims.** Synthetic EBP f in a zeroed scratch
(`ES2_STAGE2_TYPES_FRAME` 54h in 58h bytes): record f−50h, stream f−30h
(built by `0x472f14`), beam_types' count word f−52h; `0x49630c` through
`weapons_impact_guest_call`. `POP ECX` values are the pushed ones:
`0x45cdf4`, `0x46a03c`, `0x47307c` only read their argument slot, `new[]`
neither reads a register nor writes its slot (section 15). `weapon_entry`
has no locals. No shim.

**Compare:** refused for all seven (stream reads, new[] and `0x49667c`
allocation, resource loads/releases, runtime record, construction count);
the after-load snapshot is the acceptance.

### In vitro (group `stage2_types`)

`invitro/stage2_types_cases.h`: 100,000 quick direct cases per hook
(1,001,024 full) against 20 logging stubs that change what the originals
reread (`0x472f14` fails one in four); 663 bytes of image globals
randomized and compared; adapter in every case. 256 installed 31/a7
cases; real-callee passes (2,048 per hook) with `weapon_entry` nested
under `weapon_types` and its readers under it, rotating modes. 16 branch
sites both ways; one caught mutation per hook (`MUTATIONS.md`);
`//core:stage2_types_test`.

### Live

Every suite: each callback 1, `weapon_entry` and its `_call_40cff8`,
`_call_40cc14`, `_slot_18` 33, `weapon_types_call_492bec` 4,
`_call_4735b8` 2, `_call_40f8bc` 33, `mech_forms_call_492bec` 8, every
`*_call_479c78` 0. Callee hooks'
counters (`dmg_region_read`, `collision_volume`, `stream_read`,
`stream_close`, `stream_open`, `stream_destroy`, `hk_stage_dispatch_*`)
unchanged.

**Snapshot reach.** The fixed region `0x4a9572..0x4a9dfc` (end exclusive)
holds every pointer and count above except the FLYER forms pointer
`0x4a9dfc..0x4a9dff` (its block is otherwise reached only via a FLYER
actor pointer from the getter `0x423044`, `[0x4a9dfc]` + offset) and the
flag `0x498514`: add `[snapshot] extra = 0x4a9dfc:4,0x498514:1` to
`es2-oracle.ini` (`configure-tick-log.ps1 -AfterLoadSnapshotExtra`; the
proxy parses up to 16 `0xVA:LEN` entries). All other blocks are reached
from the region, the weapon entries with their DMG link lists and COL
sphere arrays included. A difference would lie in these tables, their
blocks, or the block pairing (same sizes, order, new[] and `0x49667c`).

Result: every hook replaced, own
runtime: all suites identical, all skipped/bypassed/mismatch 0, counters
as above. After-load snapshot with `[snapshot]
extra = 0x4aaa7c:0x34,0x49aac8:0x29,0x4a9dfc:4,0x498514:1` (17 fixed
regions): acceptance-60s and c3_10-loadout-hercs identical; in
c3_09-loadout-b the two original runs already pair by chance (unaligned
paths such as `+0x576f`: 4-byte windows of byte data that look like heap
addresses, section 14); an isolation rerun (three
clones) gives O2 vs R identical with section 24's extra regions and with
all four.

### Open

- Element constructors `0x41178d`, `0x4207bb`, `0x423078` stay original
  behind `0x49667c` (function_verdicts.md).
- beam_types' short-read case (a `beam.dat` under 2 bytes, stack residue)
  is not modelled.

## 26. MECH and FLYER type loads

Fifteen functions of the MECH and FLYER side, read from the listing
(exception descriptors `0x49a3e0`, `0x49a3b0`, `0x498fb1`; strings `mechs`,
`flyers`, `fm`, `GUNLIST.CPP`; extension pointers `[0x4a09f0]` …
`[0x4a0a08]`). The verdict rows of function_verdicts.md hold. Adapter group
`type_loads`, core `core/src/type_loads.c` (callees of the world load,
section 29, not of the actor load). **Live**, with the
after-load snapshot (section 14).

| Hook | VA | Role | Call site |
|---|---|---|---|
| `mech_types` | `0x42049c` | MECH type table: names, records, each selected type | world load `0x4252dd` |
| `mech_type_load` | `0x4201a8` | one MECH type: OVL record, LOD shapes, stats, gun list, "fm" record, debris | `mech_types` `0x420542` |
| `lod_remap` | `0x420090` | part remap rows of the LOD shapes and their walk | `mech_type_load` `0x4202b6` |
| `node_remap` | `0x41ff88` | recursive shape-node walk by class id | `lod_remap` `0x420189`, itself `0x42006e` |
| `remap_word` | `0x41ff68` | one part word through a remap row | `node_remap` `0x41fff3`, `0x42002b`, `0x420050` |
| `mech_type_select` | `0x41ff10` | type index of a selected mech, slot reservations | world load `0x4250b9` |
| `gun_list_load` | `0x40fee8` | gun list of a MECH type (`GUNLIST.CPP`) | `mech_type_load` `0x420391` |
| `gun_classes` | `0x40fc50` | class words of the gun entries | `mech_type_load` `0x42039e` |
| `node_list` | `0x40304c` | pointer list of a shape's gun nodes | `mech_type_load` `0x4203ce` |
| `gun_slot_base` | `0x40feb4` | gun-list slot word `0x4a98e8` | `mech_type_select` `0x41ff3c` |
| `col_slot_base` | `0x40cbd4` | collision slot word `0x4987dc` | `mech_type_select` `0x41ff54`, `flyer_type_select` `0x422ec4` |
| `dmg_slot_base` | `0x40d200` | damage slot word `0x498856` | `mech_type_select` `0x41ff5c`, `flyer_type_select` `0x422ebc` |
| `flyer_types` | `0x422fb4` | FLYER type table | world load `0x4252e2` |
| `flyer_type_load` | `0x422ed0` | one FLYER type: records, shape, DMG and COL data | `flyer_types` `0x423008` |
| `flyer_type_select` | `0x422e90` | type index of a selected flyer, slot reservations | world load `0x4250f7` |

No dword of the file holds any of the fifteen addresses, so these call
sites are all. The slot bases are 22 bytes; their 9-byte prologue (`push
ebp; mov ebp,esp; mov ax,[moffs32]`, `66 A1` on the relocation whitelist)
is hookable, so they are hooks. Computed in core, not called by the
replaced hooks: the type-index leaves `0x41ff00` (MECH, `MOVSX EAX,WORD
[EBP+8]; MOVZX EAX,BYTE [EAX+0x4a9d08]`, EDX and ECX unchanged; also used
by `mech_ctor`, section 17) and `0x422e80` (FLYER, table `0x4a9dcc`); the Q16
helpers `0x47e068` (`(a << 16) / b`: EDX:EAX the sign-extended dividend,
`IDIV`, EAX quotient, EDX remainder; a zero divisor or a quotient beyond 32
bits faults and is outside the contract) and `0x47e08d` (`IMUL`, `SHRD
EAX,EDX,16`: EAX = bits 16..47 of the product, EDX its high dword), both
`enter` bodies that keep ECX. Other callees stay game code behind the guest
bridge:

| Callee | Role | Class / mode |
|---|---|---|
| `0x49630c` | runtime frame record (descriptors `0x49a3e0`, `0x49a3b0`: EBP−24h; `0x498fb1`: EBP−50h) | RTL, through `weapons_impact_guest_call` |
| `0x4774f0`, `0x49667c`, `0x49681a` | new[], array constructor (element constructors `0x420817` → `0x42067c` → `shape_word_block`, `0x4207db`, `0x4117a5`, `0x423098`), memset | RTL |
| `0x492d00`, `0x492bec`, `0x492f54` | name list of a type file, path builder, OVL record reader | platform |
| `0x472f14`, `stream_read`, `stream_destroy`, `0x479c78` | gun-list stream, reads, close, fatal report | platform / hooks |
| `0x474cd8`, `0x474c38` | shape list (ANShape `0x1e0003`), FLYER shape | platform (resource) |
| `0x4378d8`, `0x4379cc` | player damage display bitmaps, damage display of a type | H3 |
| `0x40fea0` | mount entry `[0x4a98e4]` + 6 × sx(type word) | own code (section 32) |
| `0x40874c` | debris set of a type | own code (`debris_set`, section 31) |
| `multiply_q10`, `dmg_load`, `collision_load` | Q10 product; DMG and COL data of a FLYER type | hooks |
| shape virtuals `+20h` (part → node), `+24h` (class word → node); node virtual `+0` (class id) | | game code |

All fifteen run only in the world load (pass 1, section 2 steps 8 and 9):
the selects once per selected actor, the tables once per process, the type
loads once per selected type. With early arming (section 13) their counters
are constant from the first tick record.

### Layout

**MECH type record** (21Eh bytes; array `[0x4a9d64]` of `count` records
behind a 4-byte count header, built by `0x49667c` with element constructor
`0x420817`, which constructs the ten 18h-byte entries at +0ECh): +0 index
word; +2 the OVL record R (D8h bytes from `name.dat`, +2 .. +0D9h); +6Ch the
type word (written over R+6Ah after the read); +0DAh the damage display
(`0x4379cc`); +0DEh the shape list S (+0 shape pointer array, +4 =
`0x49a344`, +8 node-list array of 6-byte entries, +0Ch LOD count word);
+1DCh the "fm" record (36h bytes from `name.fm`, +0Eh its Q16 ratio);
+212h the debris set (`0x40874c`). In R: +2, +4, +2Ch, +6Ch the words scaled
by q; +4Eh the flag selecting the scaling (0) or the "fm" record (not 0;
reread as rec+50h); +94h (rec+96h) the index of the 8-byte entry `0x4a9d72`
+ 8 × sx; +0C0h = `multiply_q10(13Bh, R+4)`; +0C2h/+0C4h divisor and
dividend words of q; +0C6h = R+4 before scaling; +0CCh (rec+0CEh) the debris
name. `lod_remap`'s part list B is rec+30h, 50 bytes: **it covers the flag
word rec+50h** (bytes 32 and 33 of B), so a list reaching byte 32 makes the
flag nonzero; the map's `0x492f54` counts ("fm" for one type of the C4_08
suites only) agree with lists that end earlier (inference, not read from
the type files).

**FLYER type record** (70h bytes, `[0x4a9df4]`, `0x49667c(0, 70h, count, 1,
0x423098)`; the element constructor writes nothing): +0 the `name.dat`
record (2Eh bytes), +2Eh DMG slot (`dmg_load`), +32h COL slot
(`collision_load`), +36h the shape (`0x474c38`; its +26h = `0x4a9d8a`),
+3Ah the "fm" record (36h bytes, +0Eh its Q16 ratio).

**Gun list** (mount entry E = `0x40fea0(type word)`, 6 bytes: count word
+0, entries +2): 1Ah-byte entries, class word +0, class byte +6.

### Contracts (from the bytes)

All cdecl; EDX:EAX exact (the core follows EAX/EDX/ECX instruction by
instruction, so every game call sees the original's entry registers). sx =
sign extension of a word; counts compare as signed words unless noted;
words and pointers a loop tests are reread where the original rereads them.
f = the frame base (EBP).

**`gun_slot_base` / `col_slot_base` / `dmg_slot_base` (n):** AX = the slot
word (`0x4a98e8` / `0x4987dc` / `0x498856`), DX = n, word += DX (second
read). EAX, EDX keep their high words; ECX untouched.

**`remap_word` (P, table):** w = word at P; if w > −1 (signed): ECX = sx(w),
EDX = table, ECX = sign-extended byte table[w], word at P = CX. EAX = P; EDX
= table, or (w ≤ −1) entry EDX with low word w; ECX = the byte or entry ECX.

**`node_remap` (N, table, limit):** c = N's virtual +0 (EAX = [N], entry
EDX/ECX; `POP ECX` = N). c = 140014h (TSGroup, [dts.md](../formats/dts.md)):
`remap_word(N+4, table)` (EAX = c, EDX = the virtual's, ECX = N), end with
its registers. c > 140014h (signed): 140015h (TSBSPPart) walks its word list,
1E0003h (ANShape) walks the children, others end (EAX = c − 1E0005h). c <
140014h, d = c − 140005h (unsigned): d < 2 ends (EAX = d − 2); d = 2, 3
(TSPartList, TSShape), 7 (TSDetailPart) walk the children; d = 6
(TSCellAnimPart) does `remap_word(N+4, table)` (EAX = N+4) and walks the
children; others end (EAX = d − 7). Word list: for i = 0 while sx(i) < the
zero-extended count N+16h (both reread), w = word at [N+1Ch] + 2i: if 0 < w
< limit (signed words) and byte table[w] ≥ 0, `remap_word(&w, table)` (EAX
= sx(i) with low word w, EDX = sx(w), ECX = byte + 1); then the children.
Children: for i while sx(i) < zero-extended count N+10h (reread),
`node_remap([[N+12h] + 4i], table, limit word over N's high word)` (EAX =
that dword, EDX = the child, ECX = sx(i)). After the walk EAX = N, EDX =
the count, ECX = sx(i).

**`lod_remap` (S, B):** entry registers unchanged when the count word S+0Ch
≤ 1. Else memset(f−1A0h, FFh, 190h) (EAX = B, EDX = the rows); for i = 0 ..
49 over bytes B[i] up to the first negative: node0 = virtual +20h of shape 0
([[S]]; EAX = shape 0, EDX = its vtable, ECX = [S]) with B[i]; w0 = node0's
word +4 (node0 untested: null faults); for shape k = 1 while k < S+0Ch (BX,
reread): nodek = shape k's virtual +20h (EAX = vtable, EDX = B[i] reread,
ECX = shape k); a non-null nodek sets byte f−1D2h + 50k + sx(nodek word +4)
= low byte of w0 (reread from f−0Eh). Then for k = 1 while k < sx(S+0Ch)
(reread): `node_remap(shape k, f−1D2h + 50k, word +10h of [[shape 0]+2Ah]
over [S]'s high word)` (EAX = shape k, EDX = the row, ECX = f−1D2h). Row k
is at f−1A0h + 50(k−1). A row byte outside f−1A0h .. f−1 would hit the
original's saved registers or caller frame: the core skips such a store
(outside the contract). LOD counts are 6 or 7 (`node_list`'s map counts),
so k ≤ 6; of the transform indices −1..19 that dts.md observes, only −1 in
LOD shape 1 leaves the window: it would write the top byte of the saved EBX
(the caller's shape-list pointer), and the caller faults unless that byte is
unchanged, so the shipped loads never store a different byte there
(inference). Exit: EAX/ECX the last call's, EDX = sx(count).

**`node_list` (L, S, n, classes):** word L+4 = n; `new[](4 × sx(n))` (EAX =
n's word over entry EAX, EDX likewise, ECX = size; `POP ECX`), [L] = the
block; counter word = L+4 (reread once), cursor = [L]: while the counter
(before decrement) ≠ 0 and the cursor non-null: entry = 0 for a negative
class word, else S's virtual +24h (S, class word) (EAX = counter word over
previous EAX, EDX = class word over previous EDX, ECX = [S] reread);
classes += 2. Exit: EAX = last counter word over last EAX, EDX the last
call's (or the block), ECX = n << 16 | counter word after its last
decrement (`POP ECX` twice: dword [EBP−4]).

**`gun_classes` (L, out):** for each 1Ah-byte entry of [L+2] while i < count
word L (reread): out[i] = entry word +0 if its signed byte +6 < 4, else
FFFFh. EAX = entry cursor with low word = count word (reread), EDX = i, ECX
= out + 2i.

**`mech_type_select` (t):** index byte table[sx(t)] (`0x41ff00`; `POP ECX` =
t); if FFh: table[sx(t)] = low byte of count word `0x4a9d06` (EDX's low
byte), count +1, `gun_slot_base(1)` (EAX = sx(t), EDX as above, ECX = t;
`POP ECX` = 1), and if the count is now 1 the base word `0x4a9d70` = its AX.
Always `col_slot_base(1)`, `dmg_slot_base(1)` (`POP ECX` = 1 each).
**`flyer_type_select` (t):** the same over `0x4a9dcc`/`0x4a9dca`, but only
a new type reserves slots, `dmg_slot_base(1)` first, then
`col_slot_base(1)`; an old type returns EAX = its index, EDX the entry EDX,
ECX = t.

**`gun_list_load` (name, type):** record at f−50h, context word f−40h = 8;
`0x472f14(f−30h, name)` (EAX = `0x49630c`'s, EDX = f−30h); EBX = caller's
EBX with low word = type word; E = `0x40fea0(EBX)` (the pushed dword
carries the caller's EBX high word; `0x40fea0` reads the word only; `POP
ECX` = that dword); a nonzero stream status [f−2Ch] reports through
`0x479c78(0x6c56c8, 5, 0, "GUNLIST.CPP", 146h, name)` (EAX = status, EDX =
0); `stream_read(f−30h, 2, E)` (ECX = f−30h); [E+2] = `0x49667c(0, 1Ah,
sx(count), 1, 0x4117a5)` (EAX = sx(count)); `stream_read(f−30h, 26 ×
sx(count), [E+2])` (EAX = sx, EDX = size); `stream_destroy(f−30h, 2)` (EAX =
E, EDX = f−30h; E is pushed above the two arguments and popped into EAX).
Exit: EAX = E, EDX = saved FS:0 head (the adapter stores it to FS:0), ECX =
`stream_destroy`'s.

**`mech_type_load` (t):** record at f−24h; name = [[0x4a9d60] + 4 sx(t)]
into f−28h; index byte (`0x41ff00`) into f−2Ah; rec = [0x4a9d64] + 21Eh ×
sx(index); `0x492f54(rec+2, D8h, 1, 0x492bec(name, [0x4a09f0]))`; rec+0 =
index (DX), rec+6Ch = t (BX); S = rec+0DEh; `0x474cd8(S, 0x492bec(name,
[0x4a09f8]), 1E0003h, word f−2Ch = 0 over EAX)`, S+0Ch = its AX, S+4 =
`0x49a344`; `new[](6 × sx(S+0Ch) + 4)` into f−4 (`POP ECX`); if non-null,
context 14h, `0x49667c(block, 6, sx(S+0Ch), 211h, 0x4207db)`, context 8; S+8
= its EAX or 0; `lod_remap(S, rec+30h)`; R+0C6h = R+4 (AX); if R+4Eh = 0: q
= `0x47e068(400 × sx(R+0C4h), sx(R+0C2h))` into f−30h, sx(R+4) into f−34h,
R+0C0h = `multiply_q10(13Bh, sx(R+4))` (EAX = sx(R+4), EDX = remainder, ECX
= sx(R+4)), then R+2, R+4, R+2Ch, R+6Ch = `0x47e08d(sx(word), q)` in that
order (operands reread). Then E = `gun_list_load(0x492bec(name,
[0x4a0a00]), index + word 0x4a9d70 over the path's high word)`, n =
`gun_classes(E, f−60h)` (AX into f−36h); for shape k while k < S+0Ch (SI,
reread): `node_list([S+8] + 6k, shape k, n's word over EDX, f−60h)` (EAX =
entry, EDX = shape k, ECX = sx(k)) and shape k +26h = `0x4a9d72` + 8 ×
sx(rec+96h); rec+0DAh = `0x4379cc(name, index word reread)`; if rec+50h ≠ 0:
`0x492f54(rec+1DCh, 36h, 1, 0x492bec(name, "fm"))` and its +0Eh =
`0x47e068([+22h] − [+26h], [+2Ah] − [+2Eh])`; f−28h = rec+0CEh, and if that
byte ≠ 0 `0x40874c(rec+212h, rec+0CEh, 0x4a9d72 + 8 × sx(rec+96h))`. EAX =
saved FS:0 head. The class list f−60h holds 21 words; a longer gun list runs
into f−36h … f−28h, which the core keeps in the scratch frame and rereads
like the original (f−2Ah and f−28h are read after `gun_classes`).

**`mech_types` ():** record at f−24h; [0x4a9d60] = `0x492d00("mechs", 55h,
8)` (EAX = old FS:0 head); `new[](21Eh × sx(count) + 4)` into f−4 (EDX =
sx(count), ECX = size; `POP ECX`); if non-null, context 14h,
`0x49667c(block, 21Eh, sx(count), 211h, 0x420817)` (EAX = sx(count) reread,
EDX = block), context 8; [0x4a9d64] = its EAX or 0; `0x4378d8(count word
over EDX)` (`POP ECX`); for t = 0 .. 54h (BX): `mech_type_load(t)` if the
index byte ≠ FFh (EAX = byte, ECX = t). Exit: EAX = saved FS:0 head, EDX the
last call's, ECX = 54h.

**`flyer_types` ():** entry registers unchanged when count word `0x4a9dca`
= 0; else [0x4a9df0] = `0x492d00("flyers", 22h, 4)`, [0x4a9df4] =
`0x49667c(0, 70h, sx(count), 1, 0x423098)`, and for t = 0 .. 21h
`flyer_type_load(t)` if the index byte ≠ FFh. EAX the last index byte or
call's, ECX = 21h.

**`flyer_type_load` (t):** name [[0x4a9df0] + 4 sx(t)]; rec = [0x4a9df4] +
70h × index (`0x422e80`; `MOVSX EAX,AX`; `POP ECX` = t); `0x492f54(rec, 2Eh,
1, name.dat)`; rec+36h = `0x474c38(name.dts)` (`POP ECX`), its +26h =
`0x4a9d8a`; rec+2Eh = `dmg_load(name.dmg)`; rec+32h =
`collision_load(name.col)`; `0x492f54(rec+3Ah, 36h, 1, name.fm)`; its +0Eh
= `0x47e068([+22h] − [+26h], [+2Ah] − [+2Eh])`. EDX:EAX = quotient and
remainder, ECX = `0x492f54`'s.

**ECX after the calls.** The adapters return the framework's entry ECX:
the original's ECX is dead at every call site of the fifteen (in the core
too).

**Frames, residue, shims.** `mech_types`, `mech_type_load`, `gun_list_load`
run with the synthetic EBP of a zeroed scratch (`ES2_*_FRAME`; `0x49630c`
through `weapons_impact_guest_call`), locals at the original offsets (stream
object f−30h and class list f−60h are passed to callees); the adapters
write the saved head to FS:0 (EAX for the first two, EDX for
`gun_list_load`). `lod_remap`'s rows and locals live in a zeroed scratch
frame (`ES2_LOD_REMAP_FRAME`), reread like the original's. No function
reads a local it did not write; no callee reads one of theirs apart from
the objects passed to it. The only caller register read is
`gun_list_load`'s EBX high word (dead, above): the adapter reconstructs EBX
as its only caller has it (rec+0DEh of the type record whose index is the
type word minus `0x4a9d70`); the core takes it as an argument. No entry or
exit shim.

**Compare:** accepted by the three slot bases (one image word each, a
region; no call) and `remap_word` (span: the word P; EAX = P compared
relative to it; the table byte only read, at a runtime index). Refused by
the other eleven (allocation, files, runtime record, virtual calls, nested
hooks writing type tables, shapes and node words).

### In vitro (group `type_loads`)

`invitro/type_loads_cases.h`: 100,000 direct cases per hook (quick tier) against logging stubs for every game callee (35 stub
ids), with rereads and overlapping objects provoked; 256 installed 31/a7
cases per hook; real-callee passes (4,096 cases per hook) over a
deterministic world with nested hooks in rotating modes; 512 installed
compare cases for each compare-capable hook (0 mismatches). 41 branch
sites, all both ways, none PROVED. One caught mutation per hook
(`MUTATIONS.md`). Core unit test `//core:type_loads_test` (hand-worked
scenarios for all fifteen).

### Live

Predicted counts from the DBSIM map (every hook original, early
arming, 40 suites; map probes
stop at 1,000), per suite group and with the callee counter relations.
Identities in
every suite: `mech_type_load` = `lod_remap` = `gun_list_load` =
`gun_classes` = `gun_slot_base` = `0x4379cc` = the distinct MECH types among
the selected mechs; `col_slot_base` = `dmg_slot_base` = `mech_type_select` +
the FLYER types (`flyer_type_load`, 0 or 1); `node_list` = the LOD shapes of
the MECH types (6 or 7 each); `flyer_types` = `mech_types` = 1.
`node_remap` and `remap_word` depend on the node trees of LOD shapes 1 ..
n−1 and the part lists; the map caps them except in train1-timers (273,
343).

**Snapshot reach.** The type arrays `[0x4a9d60]`/`[0x4a9d64]`,
`[0x4a9df0]`/`[0x4a9df4]`, index tables `0x4a9d08`/`0x4a9dcc`, counts
`0x4a9d06`/`0x4a9dca`, base word `0x4a9d70`, gun slot word `0x4a98e8` and
mount table `[0x4a98e4]` lie in the fixed region `0x4a9572 .. 0x4a9dfc`; the
walk follows them into name lists, type records, shape lists, node lists,
gun lists and LOD node words. Outside the fixed regions: the slot words
`0x4987dc`/`0x498856` (and the slot counters `0x4987de`/`0x498854`,
sections 20 and 22); their effect is in reach (slot arrays
`[0x4a9824]`/`[0x4a9828]` sized from them by `0x40cbec`/`0x40d218`, and the
filled slots); the words need `[snapshot] extra = 0x4987d8:8,0x498850:8` in
`es2-oracle.ini`. A difference would show in the type records (stats,
scaled words, R+0C0h, "fm" ratios), shape lists and node-list arrays, the
shapes' +26h, LOD node words, gun lists, or block pairing (same sizes, same
order, same callees).

**Result.** Every hook replaced, own
runtime: **all suites identical**; counters equal the predicted counts;
uncapped `node_remap` / `remap_word` from 273 / 343 (train1-timers) to
2,961 / 3,806 (C3_09); `dmg_load`,
`collision_load`, `multiply_q10`, `stream_read` equal those of section 25's
run. Snapshot with the extra regions above (section 14).

### Open

- The bridged element constructors `0x420817`, `0x42067c`, `0x4207db`,
  `0x4117a5`, `0x423098`, the platform functions `0x492d00`, `0x492f54`,
  `0x492bec`, `0x474cd8`, `0x474c38` and the H3 functions `0x4378d8`,
  `0x4379cc` keep their verdicts in function_verdicts.md.

## 27. Base flattening and the base-mark raster

Contracts, recursion and in-vitro evidence:
[terrain.md](terrain.md#base-flattening-and-the-base-mark-raster); here the
load-side view. **Live**, with the after-load snapshot
(section 14).

Five hooks, group `base_flatten`: `base_flatten` `0x47129c`, `flood_sum`
`0x470fe8`, `flood_fill` `0x471128`, `mark_raster` `0x47136c`,
`cell_flag_get` `0x470e00`. Step 12 of the actor load (section 21) calls
`base_flatten([0x4a0be0])` at `0x4263d9` once per load, after every
building's footprint (`footprint_marks`, section 23) and base layout
(`base_marks` → `mark_raster`, section 17) has set its cell marks: each
8-connected region of marked cells gets the integer mean of its height
bytes (`flood_sum`, then `flood_fill` with the quotient, also into the row
and column beyond the region); then the H3 shading pass `0x46c2e0`
recomputes the shade data. Building sites are flat before the first frame,
and `terrain_height` and the simulation read these heights. `cell_flag_get`
also runs per frame (H3 cell-object draw `0x46e5a4`, as `cell_flag_set`).

### Live

Predicted counts per suite (DBSIM map, zone table of section
24, section 17's base marks). Relations:

- `hk_base_flatten_calls` = `hk_base_flatten_call_46c2e0` =
  `hk_actor_load_call_47129c` = 1 in every suite.
- `hk_base_flatten_call_470e00` = (1 << n)² (every cell's mark read once):
  65,536 in the zone-123 suites, 16,384 elsewhere.
- `hk_mark_raster_calls` = `hk_base_marks_call_47136c`;
  `hk_mark_raster_call_470d74` = the base marks' share of `cell_mark_set`
  (section 24's count minus the cells).
- T (regions) = `hk_base_flatten_call_470fe8` = `_call_471128` =
  `hk_flood_sum_calls` = `hk_flood_fill_calls`; V (visited cells) =
  `hk_flood_sum_call_470ddc` = `hk_flood_fill_call_470ddc`;
  `hk_flood_fill_call_470e00` = `hk_flood_sum_call_470e00` (the marks are 0
  or 1 before, so bit 2 is clear in every region cell and the fill walks
  each region as the sum does). The recursion is not counted: T + 8V = the
  map's entries of `0x470fe8` (and `0x471128`), which fixes T mod 8 where
  uncapped (T ≤ footprints + base marks). T and V depend on building
  positions and radii and are not predicted.
- `hk_cell_flag_get_calls` grows per frame; at the first record it is at
  least the scan plus the floods' reads. Every `*_skipped`/`*_bypassed` 0.
  Unchanged (bridge): `hk_cell_flag_set_*`, `hk_cell_mark_set_*`,
  `hk_base_marks_*`, `hk_actor_load_call_47129c`.

**Snapshot reach.** Heights, shade data, material bytes (+0Fh) and marks
(bits 0 to 2 after flattening) lie in the cell and mark arrays behind
`[0x4a0be0]` (G+0ECh, G+0F0h), reached by the fixed region `0x4a0be0`
(section 24). Not reached: the sum and count `0x6b5470`/`0x6b5474` (the
last region's; unread after the load, the flatten's only other writes),
added by `[snapshot] extra = 0x6b5470:8` (`configure-tick-log.ps1
-AfterLoadSnapshotExtra 0x6b5470:8`, with section 24's
`0x4aaa7c:0x34,0x49aac8:0x29` if wanted). A difference would show in the
cells (heights, byte +0Fh, shade data), the marks, `0x6b5470..0x6b5477`, or
block pairing (no hook allocates). `cell_flag_get` also accepts `compare`.

**Result.** Every hook replaced, own
runtime: **all suites identical**; counters equal the predicted counts, T +
8V equal the map's counts; in the capped zone-123 suites T = 21 … 26, V =
232 … 240 (deepest flood path at most V + 1 = 241 levels). Snapshot
(seven extra regions incl. `0x6b5470:8`): O2 vs R
**identical** (scope all and pools) in acceptance-60s, c3_10-loadout-hercs
and c3_09-loadout-b (c3_09's two original runs differed in 110 places, all
absorbed by the noise file learned from them).

### Open

- The adapters' path buffers (2 × 64 KiB in the proxy's data) are sized for
  the largest zone; no other bound is known.

## 28. Building types and pools

The two load-time functions of the building module (`base.cpp`, named by
the assertion strings), read from the listing (exception descriptors
`0x49766f`, `0x497603`, strings `0x497712..0x49776d`). The verdict rows of
function_verdicts.md hold. Counts predicted from the data files in
`SIMVOL0.VOL`. Adapter group `building_types`, core
`core/src/building_types.c` (`base_pools` repeats section 15's pool/list
pattern in eight lines). **Live**, with the after-load snapshot
(section 14).

| Hook | VA | Role | Caller |
|---|---|---|---|
| `base_types` | `0x405fac` | stage-2 record 3: building types, weapons, collision data, hulks, textures, debris | `0x401d94(2)` via `call [ebx]` at `0x401dac` (registered by `0x401d64(fn, 2)` at `0x40696e`) |
| `base_pools` | `0x405df4` | building pool and its removal list | world load `0x424308` at `0x4252a0` (pass 1, section 2 item 9) |

`0x405fac` is referenced once (the push at `0x40696e`); `0x405df4` has one
call site. Both run once per process in the mission load, after early
arming, so each is entered **once per suite** (DBSIM map: 1
in each of the 40 suites).

### Contracts (from the bytes)

Both cdecl, `push ebp` frame, runtime record via `0x49630c` (EAX = the
descriptor; its second dword is the record offset: −`0E0h` for `0x49766f`,
−`28h` for `0x497603`). `0x49630c` keeps EDX and ECX and returns the old
FS:0 head in EAX; the context word is record+10h (EBP−`0D0h`, EBP−`18h`).
The epilogue loads the saved head into EAX and stores it to FS:0: **EAX =
the old FS:0 head**. Every callee is a guest call with the original's entry
registers; every guest read and write is in the original's order.

**`base_types` 0x405fac** (no argument; EDI = EBP−30h throughout; stream
objects S1 = EBP−60h, S2 = EBP−90h, S3 = EBP−30h, S4 = EBP−0C0h). Each file
opens alike: path `0x492bec(name, [0x4a09f0])` stored in a local, context
word set, `0x472f14(S, path)` (bforms, lc_wpns: EAX = S, ECX = path reread
from the local; bases: EAX = EDX = path; basecol: EAX = EDX = path, ECX =
S); a nonzero status [S+4] calls `0x479c78(0x6c56c8, 5, 0, "base.cpp",
line, path)` with the SETE register = 0 (bforms: line 51Ah, ECX; lc_wpns:
545h, ECX; bases: 550h, EAX; basecol: 586h, EDX). "read(S, n, p)" = the
stream method at slot +18h of [S], reread before every call.

1. **`bforms`** (context 8): read(S1, 4, `0x4a95a8`) dword count N;
   `0x49667c(0, 1Ch, N, 1, 0x406a5c)` → `[0x4a95ac]` (forms). Per form F
   (counter dword EBP−0E8h against dword `[0x4a95a8]`, signed): read(F, 4)
   element count K; `[F+4]` = `0x49667c(0, 0Ah, K, 1, 0x406a64)`;
   `stream_read(S1, 10·K, [F+4])`; read(F+8, 4); `stream_read(S1, 8,
   F+0Ch)`; read(F+14h, 4) list count M. M ≠ 0: `[F+18h]` = `new[](4·M)`,
   and for i < M (signed): `[[F+18h]+4i]` = `new[](M)` (each list gets M
   bytes, not its length), read(S1, 1, EBP−0E9h) length L, read(S1,
   sx8(L), `[[F+18h]+4i]`). M = 0: `[F+18h]` = 0. Then
   `stream_destroy(S1, 2)`.
2. **`lc_wpns`** (context 14h): read(S2, 2, `0x4a95b8`) count word C;
   `[0x4a95bc]` = `0x49667c(0, 16h, sx(C), 1, 0x406a6c)`; word EBP−0F2h =
   22·C; `stream_read(S2, sx(that word), [0x4a95bc])`;
   `stream_destroy(S2, 2)`.
3. **`bases`** (context 20h; S3 open until the end): `stream_read(S3, 2,
   0x4a9598)` type count word T; `[0x4a959c]` = `0x49667c(0, 3Ch, sx(T), 1,
   0x406a77)`; `[0x4a95b0]` = `new[](6·sx(T))` (ECX = 6T after the POP).
   Per type B (counter word EBP−0FAh, compared as a word, loaded into CX
   over ECX's high word; dead word EBP−0FCh = 34h each pass): read(S3, 2)
   of +0, +2, +4, +6, +8; `stream_read(S3, 6, B+0Ah)`; read(S3, 2) of +10h
   and +12h (child count); `[B+14h]` = `0x49667c(0, 1Eh, sx([B+12h]), 1,
   0x406a85)` and `stream_read(S3, 1Eh, [B+14h] + 30i)` for i < sx(word
   +12h, reread); `stream_read(S3, 6, B+18h)`; read(+1Eh, 2);
   `stream_read(S3, 4, B+20h)`; read of +24h, +26h; read of +28h..+32h,
   each address first stored in its local EBP−100h..EBP−114h and reloaded
   (EAX = ECX = the address). Then `[B+38h]` = `[0x4a95b0]` + 6·k (k = the
   counter) if word +30h ≠ 0, else 0 (EDX = 0).
4. **`basecol`** (context 2Ch): after the open EBX = `[0x4a95b0]` (read
   before the status test, so the report cannot move it);
   `collision_read(EBX + 6i, S4)` for i < word `[0x4a9598]` (compared as
   SI, reread every pass; EAX = S4); `stream_destroy(S4, 2)`.
5. **Count words:** `0x4a9580`, `0x4a9578`, `0x4a9572` ← 0; over the types
   (EAX against sx(`[0x4a9598]`), reread): v = word +2 + 1 (INC of the
   32-bit register, used as a word); if word +6 ≠ 0, `0x4a9572` =
   max(`0x4a9572`, v) (via local word EBP−11Ah; `jle` keeps v on a tie),
   else `0x4a9578` = max(`0x4a9578`, v) (stored through BX); then
   `0x4a9580` = max(`0x4a9580`, word +4 + 1). All signed words.
6. **Hulks:** `474cd8(0x4a9584, 0x492bec("bhulks", "dgs"), 2BC0001h, 0)`
   (returned word into dead local EBP−11Ch); `[0x4a957c]` =
   `new[](4·sx([0x4a9578]))`, `[0x4a9574]` = `new[](4·sx([0x4a9572]))`,
   both zeroed dword by dword (counts reread each pass).
7. **Textures** (directory `[0x4a0a10]`): `basetex` → bitmap `0x45cdf4` at
   `0x4a9588`, its table `0x46a03c([0x4a9588])` at `0x4a958c`,
   `[[0x6cbee4]]` += 1, `0x48349c([0x4a9588], 3)`, `0x4a9588` ← 0; `vehtex`
   the same at `0x4a9590`/`0x4a9594` (registers differ: EDX the directory,
   EAX the counter pointer, EDX the release argument; ECX = 0 at the end).
   Then `[[0x4a9584] + 4i] + 26h` = `0x4a9588` for i < sx(`[0x4a9580]`)
   (reread).
8. **Debris set** `0x40874c(0x4a95c0, "base_deb", 0)`, then
   `stream_destroy(S3, 2)`. Returns EAX = the head, EDX:ECX = the
   destroy's.

**`base_pools` 0x405df4** (count): BX = the argument word (caller's EBX
high word stays); `new(12h)` → EBP−4 (ECX = 12h); if nonzero, context 14h,
`pool_create([EBP−4], EBX, 26Dh)`, context 8; `[0x4a95a0]` = ECX =
`[EBP−4]`. `new(8)` → EBP−8 (ECX = 8); if nonzero, context 20h, `MOV
AX,19h` (EAX high word kept), word `[list+6]` = 25, `new[](100)`, ECX =
`[EBP−8]`, `[list]` = block, word `[list+4]` = 0, context 8; `[0x4a95a4]` =
EDX = `[EBP−8]`. This is section 15's pool/list pair with the count from
the argument and size 26Dh. The pushed capacity slot carries the caller's
EBX high word, which `pool_create` never reads (it uses SI); the adapter,
which cannot see EBX, passes 0.

**Callees and modes.** Guest calls with the original's entry registers:
`0x49630c` (synthetic EBP), new `0x47749c` and new[] `0x4774f0` (neither
writes its argument slot, so POP ECX values are the pushed ones),
`0x49667c` with element constructors `0x406a5c`, `0x406a64` (return the
argument), `0x406a6c` (EDX = +8), `0x406a77` (EDX = +0Ah, ECX = +18h),
`0x406a85` (EDX = +0Ah, ECX = +10h) (five empty bodies, reached only
through `0x49667c`); path builder `0x492bec`; the stream (`0x472f14`,
method +18h, hooks `stream_read`, `stream_destroy`); `0x479c78`; resources
`0x474cd8`, `0x45cdf4`, `0x46a03c`, `0x48349c` (`0x45cdf4` and `0x46a03c`
only read their argument slot); `0x40874c` (`debris_set`, section 31); hooks
`collision_read` (section 20) and `pool_create` (section 14) in their own
modes.

**Registers, stack, shims.** EDX:EAX exact; ECX exact in the core; the
adapters leave the framework's entry ECX because nothing reads it: the
dispatcher reads no register after `0x401dac`, and the next stage-2
callback (`light_pools` `0x4076e4`, record 4) loads EAX before `0x49630c`
and reaches `new` with EDX/ECX unread; after `0x4252a0` the world load pops
ECX and reloads CX and AX (the returned EAX's high word goes on into
`0x411df8`'s argument slot; EAX is exact). Both bodies read only locals
they wrote or that a callee given their address filled (stream objects via
`0x472f14`, the length byte via the read); no stack residue, no shim. The
frame is a zeroed scratch at the original's EBP offsets (`base_types` 124h
bytes, EBP at +120h, lowest local EBP−11Ch; `base_pools` 2Ch bytes, EBP at
+28h); the adapters write the returned head to FS:0 as the epilogues do.

**Compare:** refused for both (file I/O, allocation, resources, runtime
record, construction count); the after-load snapshot is the acceptance.

### In vitro (group `building_types`)

`invitro/building_types_cases.h`: 100,000 direct cases per hook (quick
tier) against logging stubs for all 16 direct callees
and the stream read method, with count changes, open and `new` failures;
256 installed 31/a7 cases per hook; real-callee pass (2,048 cases per
hook) with `collision_read` and `pool_create` nested in both modes. 21
branch sites, all both ways. One caught mutation per hook
(`MUTATIONS.md`). Core unit test `//core:building_types_test` (by hand:
one form with a list, two `lc_wpns` records, two building types, the four
open failures, both pool paths).

### Live

Same data in every suite (`BFORMS.DAT` 17 forms, 145 elements, 112 lists;
`LC_WPNS.DAT` 3 records; `BASES.DAT` 65 types, 110 children; `BASECOL.DAT`
65 entries).

**Snapshot reach.** Every global the two write lies in the fixed region
`0x4a9572..0x4a9dfc` of section 13 (`0x4a9572..0x4a95bf`, and `0x4a95c0`
onwards for the debris set), and every block hangs off it (`0x4a95a0`,
`0x4a95a4`, `0x4a95ac` with element and list blocks, `0x4a95bc`,
`0x4a959c` with child blocks, `0x4a95b0` with COL descriptors, `0x4a957c`,
`0x4a9574`, texture tables `0x4a958c`, `0x4a9594`). No `[snapshot] extra`
needed; stream objects and dead locals are stack.

**Result.** Every hook replaced, own
runtime: **all suites identical**; counters equal the predicted counts in
every suite; `collision_read` (76 in the C1_01 suites, 66 in
train1-timers) and `pool_create` 12 unchanged. Snapshot:
identical as in section 27.

### Open

- The five element constructors `0x406a5c`, `0x406a64`, `0x406a6c`,
  `0x406a77`, `0x406a85` stay original (reached only through `0x49667c`);
  verdict rows in function_verdicts.md.

## 29. L7 world load

L7 of section 10: the world load `0x424308` (script pass 1) and the four
leaves only it calls, read from the pinned listing (descriptor `0x49a7fc`,
file-name strings `0x49a9cb..0x49aa34` and stream table `0x4a1738` read from
the file). Adapter group `mission_world`, core in `core/src/mission_load.c`;
an own group because its harness (three synthetic files behind stub reads,
31 stub ids) shares nothing with `mission_actors`. **Live**
with the after-load snapshot (section 14).

| Hook | VA | Role | Call site |
|---|---|---|---|
| `world_load` | `0x424308` | script pass 1: launch files, mission tables, selection, compaction, pools, type loads, terrain | `0x4617c8` in the mission load |
| `actor_list_init` | `0x411df8` | the actor list `(list, count)`: `[list]` = `new[](4 × sx(count))`, word `list+4` = count | `0x4252c6` |
| `gun_slot_table` | `0x40fecc` | `[0x4a98e4]` = `new[](6 × sx(word 0x4a98e8))`, the gun-list table (also for 0) | `0x4252ce` |
| `dmg_slot_table` | `0x40d218` | `[0x4a9828]` = `new[](12 × sx(word 0x498856))`, only for a nonzero word | `0x4252d3` |
| `col_slot_table` | `0x40cbec` | `[0x4a9824]` = `new[](6 × sx(word 0x4987dc))`, only for a nonzero word | `0x4252d8` |

No dword of the file holds any of the five addresses, so these call sites
are all. Every other callee stays game code behind the guest bridge, in its
own mode:

| Callee | Role | Class / mode |
|---|---|---|
| `0x49630c` | runtime frame record (descriptor `0x49a7fc`: EBP−54h, context word EBP−44h) | RTL, through `weapons_impact_guest_call` |
| `0x49681a` | `_memset` (ten table and frame clears) | RTL |
| `0x45ee00`, `0x492b90` | the file names (`data\mission.var`, `data\player.mec`, `data\mission.str`, `data\script` + `[0x4a09f0]`) | platform (file) |
| `0x47ad40`, `0x47af98`, slot `+18h` (= `0x47af98`), `0x47ad78`, `0x479c78` | the three streams in the frame, reads, closes, fatal report of a failed open | platform (file) |
| `0x453878`, `0x4539a0`, `0x496766` | the STR reader into `0x4aa650`, its string pointers at `0x4aa660`, `_strlen` | platform, RTL |
| `0x42e010` | the world loader (WLD, palettes, shade and remap tables; the flat-object pool `0x4097a8` inside it) | H3 (below) |
| `0x405ebc` | the shape list of a building type (`bases_an.dts` / `bases.dgs` through `0x474de4`) | platform (below) |
| `0x47749c`, `0x4774f0`, `0x49667c` (element constructors `0x4953c4`, `0x427207`) | new, new[], array construction | RTL |
| `region_init`, `timer_init`, `group_array`, `mech_type_select`, `flyer_type_select`, `pool_create`, `base_pools`, `mech_types`, `flyer_types`, `zone_load` and the four leaves | | hooks, their own modes |

### Call path

One call site, `0x4617c8` in `0x461608`, once per process; each leaf has
one call site in it. With early arming (section 13) the counters are
constant from the first tick record on. The DBSIM map (every
hook original, early arming) counts `0x424308`, `0x411df8`, `0x40fecc`,
`0x40d218`, `0x40cbec`, `0x42e010`, `0x405df4`, `0x42049c`, `0x422fb4` and
`0x42789c` at 1 in all 40 suites, and `0x405ebc`, `0x41ff10`, `0x422e90`
(only caller: the world load) at the predicted counts of the Live
subsection in all 40.

### Contract (from the bytes)

No arguments; EBX, ESI, EDI saved and restored. Frame `add esp,-0E54h`: the
record EBP−54h; three 12-byte stream objects (table +0, status +4):
player.mec EBP−24h (in EDI at first), mission.var EBP−18h, script EBP−0Ch;
pool and list headers EBP−28h .. −34h; list count words EBP−56h (mechs M),
−58h (flyers), −5Ah (buildings), −62h (groups), −64h (orders), −66h
(objectives); compaction counters EBP−5Ch/−5Eh/−60h; cursors EBP−6Ch ..
−14Ch; local type arrays (mechs EBP−9E4h, 100 words; flyers EBP−0A48h, 50;
buildings EBP−0B60h); record buffers (point EBP−0B6Ch, trigger indices
−0B7Ch, timer −0B94h with ten trigger pointers −0BBCh, mech −0C44h, flyer
−0CA0h, building −0CD4h, order −0CE4h, group 0 −0D80h, group −0E1Ch,
objective −0E54h). EBX is the loop counter (compared as BX, signed words),
ESI and EDI the record cursors. "read" = `0x47af98(stream, size, buffer)`,
"vread" = the stream's slot +18h, same arguments, table reread from the
stream object before every call. In order:

1. `0x49630c` (EAX = descriptor, entry EDX/ECX). `_memset` of mech flags
   `0x4aa72a`, remap `0x4aa78e`, local mech types (200 bytes, EDX = the
   address), MEC flags `0x4aa7f2` (100 each), flyer flags `0x4aa856` and
   remap `0x4aa888` (50), local flyer types (100 bytes, ECX = the address),
   building flags `0x4aa8ba`, remap `0x4aa946`, factory bytes `0x4aa9d2`
   (140 each); every other register is the previous call's.
2. Context 8: `0x45ee00("data\mission.var")` (`POP ECX` = the string),
   `0x47ad40(EBP−18h, name)` (EAX = the stream); a nonzero status (read
   twice) reports `0x479c78(0x6c56c8, 5, 0, "WORLD.CPP", 0A5h,
   "mission.var")` (EDX = the SETE result); read 2000 bytes into
   `0x4a9e70`; words `0x4a9e98` (variable 20), `0x4a9e84` (10) and
   `0x4a9e9a..0x4a9ec5` (21..42) = 0 (EAX ends at 2Bh); `0x47ad78(stream,
   2)`.
3. Context 14h: `data\player.mec` the same way (EAX = the name; report line
   0B2h, "player.mec"); vread the player ordinal `0x4aaa64` (ECX = table)
   and N `0x4aaa5e` (EAX = table); `new[](126 × sx(N))` (EDX = sx(N), ECX =
   size; `POP ECX`) to `0x4aaa60`; per record (i < N, reread): vread words
   +0, +2, +4, +6 (EAX = address, EDX = table), read 2 × sx(word +6) bytes
   to +8 and to +20h (word reread), vread word +3Ah, read 26 bytes to +3Ch,
   20 to +56h, 20 to +6Ah. File record 76 + 4 × w bytes (w = word +6),
   runtime record 126 bytes; close.
4. Byte `0x4d2571` = (word +4 of MEC record 0 = 8), read through dword
   EBP−6Ch also when N is 0. `0x45ee00("data\mission.str")`,
   `0x453878(0x4aa650, name)`, `0x4539a0(0x4aa650, 0x4aa660, 32h, 0)`: the
   third argument is not read, the count is the word at the start of the
   STR body (AX). For i < sx(AX) (EBP−70h): `_strlen([0x4aa660 + 4i])`
   (`POP ECX` = string); a 0Ah in the byte before the terminator (for an
   empty string: the byte before the string) becomes 0.
5. Context 20h: `0x492b90(0x45ee00("data\script"), [0x4a09f0])` (the
   extension pushed first, before the inner call) to EBP−74h,
   `0x47ad40(EBP−0Ch, path)` (EAX = stream, ECX = path); a failed open
   reports "WORLD.CPP", line 0DCh, the path (ECX = SETE result, EDX =
   path). Read the 20-byte header to `0x4a9e4e`; word `0x4a9e52` = 0;
   `0x42e010(0x4a9e4e)`.
6. Coordinates: vread count `0x4a9e06`; when nonzero `0x49667c(0, 0Ch,
   sx(n), 1, 0x4953c4)` (EAX = sx(n)) to `0x4a9e0c`; rectangle min y
   `0x4aa644`, min x `0x4aa640` = 7FFFFFFFh, max y `0x4aa64c`, max x
   `0x4aa648` = 80000000h (that order); per 12-byte point (count reread) a
   read to EBP−0B6Ch, three dwords copied, four signed comparisons (each
   dword reread). Without coordinates the rectangle is untouched.
7. Angles: vread `0x4a9e08`; when nonzero `new[](2 × sx(n))` to `0x4a9e10`;
   per word read to EBP−0FEh, stored 182 × sx(word)
   (LEA/SHL/SUB/SHL/SUB/ADD) as a word.
8. Routes: vread `0x4a9e14`; when nonzero `new[](6 × sx(n))` to
   `0x4a9e18`; per 6-byte route vread the point count word +0, `new[](4 ×
   sx(count))` to +2, per point (CX = count word, reread, against counter
   word EBP−106h) vread an index to EBP−108h, store 12 × sx(index) +
   `[0x4a9e0c]`.
9. Regions: vread `0x4a9e1c`; when nonzero `new[](10 × sx(n))` to
   `0x4a9e20`; per region read 6 bytes to EBP−114h, `region_init(region,
   EBP−114h, [0x4a9e0c], ECX)`; the fourth slot is ECX with CX = the
   coordinate count (unread by `region_init`).
10. Triggers: vread `0x4a9e24`; when nonzero `new[](58 × sx(n))` to
    `0x4a9e28`; per runtime trigger T (58 bytes; 74 in the file): vread
    words +0, +2, read the 8 region indices (16 bytes) to EBP−0B7Ch, 20
    bytes to +0Ch and to +20h, 10 bytes to EBP−120h (never used), vread word
    +34h and a word to EBP−122h; dword +36h = its sign extension, word +34h
    − 1, word +0Ah = 0, word +4 = count of leading non-negative indices (at
    most 8); when nonzero `new[](4 × count)` to +6 with pointers 10 ×
    sx(index) + `[0x4a9e20]`.
11. Timers: vread `0x4a9e34`; when nonzero `0x49667c(0, 31h, sx(n), 1,
    0x427207)` (49-byte timers) to `0x4a9e38`; per 24-byte record: first
    trigger (word +0; negative gives 0, else 58 × sx + `[0x4a9e28]`) to
    EBP−134h, the ten pointers of words +4 .. +16h to EBP−0BBCh, then
    `timer_init(49 × sx(i) + [0x4a9e38], first, EBP−0BBCh, sx(word +2))`
    (EAX = first, EDX = sx(i), ECX = the timer).
12. Authored lists: vread the mech count to EBP−56h, per 134-byte record the
    type word +28h into the local array; flyers (92 bytes, +2Ch), buildings
    (52, +0) likewise; orders (14 bytes) only read.
13. Groups: compaction counters = 0; vread count to EBP−62h; when nonzero
    `group_array(0x4a9e00, ECX)` (CX = count), per 156-byte record: record 0
    to EBP−0D80h (kept), every other to EBP−0E1Ch with word +6Eh = 1; each
    of its 20 non-negative member words +32h marks `0x4aa72a`, `0x4aa856`
    or `0x4aa8ba` + sx(word) for class word +28h 0, 1 or 2 (reread per
    test; class 2 also sets factory byte `0x4aa9d2` + sx(word) = low byte of
    +6Eh, so 1).
14. Objectives (54 bytes) only read. String indices: vread count
    `0x4a9e44`; when nonzero `new[](2 × sx(n))` to `0x4a9e48` and one vread
    of 2n bytes into it.
15. Compaction: consecutive remap indices (counter EBP−5Ch, low byte) to
    `0x4aa78e + i` for selected mechs i < M; group 0's non-negative slots k
    get `0x4aa78e + sx(slot)` = counter + k (the remap index the k-th MEC
    actor receives next); flyers (EBP−5Eh into `0x4aa888`) and buildings
    (EBP−60h into `0x4aa946`) likewise; then each MEC record i < N (reread)
    is appended as mech M + i (AX = M + BX): flags `0x4aa7f2` and
    `0x4aa72a`, type word = MEC word +4 into the local array, remap =
    counter (+1); M += N.
16. Type selects: `mech_type_select(type)` per selected mech i < M (EDX =
    type word over the previous EDX; `POP ECX`), `flyer_type_select(type)`
    per selected flyer, `0x405ebc(type, sx(byte 0x4aa9d2 + i))` per selected
    building (EAX = type word over the flag pointer, EDX = the byte's
    address, ECX = the byte).
17. MECH pool: `new(12h)` (`POP ECX`) to EBP−28h; when not null, context
    38h, `pool_create(H, mech counter in DX, 36Ah)` (EAX = ECX = H), context
    2Ch; `[0x4a9b7a]` = H (also 0). Removal list: `new(8)`; when not null,
    context 44h, word +6 = 19h (CX), `new[](100)`, `[list]` = block, word +4
    = 0, context 2Ch; `[0x4a9b7e]` = list. FLYER pool (291h, contexts
    5Ch/50h) to `0x4a9db9`, its list (context 68h) to `0x4a9dbd`, the same
    way.
18. `base_pools(building counter)` (`POP ECX`); `actor_list_init(0x4a9af8,
    mechs + flyers + buildings)` (CX summed, word EBP−142h, AX = it);
    `gun_slot_table`, `dmg_slot_table`, `col_slot_table`, `mech_types`,
    `flyer_types` (no arguments, previous registers); `zone_load(word
    0x4a9e50, word 0x4a9e4e)` (ECX/EDX = the words over the previous
    registers).
19. Word `0x4a9e6a` = 0, `[0x4a9e6c]` = `new[](200)`, word `0x4aa728` =
    header word 4 (`0x4a9e56`); `0x47ad78(script stream, 2)`. EDX:EAX = its
    result; ECX = the saved head `[EBP−54h]`, which the epilogue stores to
    FS:0.

**Leaves.** `actor_list_init 0x411df8 (list, count)`: `new[]` with EAX = 4 ×
sx(word count), entry EDX/ECX; `POP ECX`; `[list]` = EAX, word `list+4` =
count. `gun_slot_table 0x40fecc`: EAX = sx(word `0x4a98e8`), EDX = 6 × EAX,
`new[](EDX)` with entry ECX, `POP ECX`, `[0x4a98e4]` = EAX (no zero test).
`dmg_slot_table 0x40d218` / `col_slot_table 0x40cbec`: BX = word `0x498856`
/ `0x4987dc`; for 0 nothing (EAX/EDX/ECX the entry's), else EAX = sx(BX),
EDX = 12 / 6 × EAX, `new[](EDX)`, `POP ECX`, `[0x4a9828]` / `[0x4a9824]` =
EAX. The tables are the gun-list, damage-slot and collision-slot tables
sized by the running slot words of the type selects. All four return
new[]'s EDX:EAX and ECX = the size.

**Registers, stack, shims.** The core follows EAX/EDX/ECX through every
instruction, so each guest call gets the original's entry registers and
slots (`POP ECX` reloads the pushed value; no popped callee writes its
slot). The results at the callers are dead, so a replaced leaf's framework
entry ECX changes nothing. The frame lies in a zeroed scratch at the
original's offsets (`ES2_WORLD_LOAD_FRAME`, the record through the bridge's
EBP). No local is read before written, except: **a script without groups**
(count 0 or negative) never reads record 0, so the compaction reads group
0's slots `EBP−0D4Eh .. EBP−0D27h` from stack residue; the replacement
reads zeros (outside the contract: every launch set and shipped script has
at least group 0 with the MEC actors). The guest callees read no local
below the frame before writing it and none reads the
bridge's EBX/ESI/EDI before saving it. No entry or exit shim.

**Guest verdicts** (rows in function_verdicts.md). `0x42e010` stays a
guest (H3): it writes the copied header `0x4cfcc4..0x4cfcd7`,
WLD words and table pointers `0x4cfcd8..0x4cfd07`, the horizon painter and
its words `0x49aee0..0x49aefb`, the draw state `0x4a5b00..0x4a5b0b` and,
through its callees, palettes, shade, remap and terrain texture tables; only
drawing and presentation code reads them (`xrefs.py`); its one simulation
callee, the flat-object pool `0x4097a8`, is own code (`flat_pools`, section
31). `0x405ebc` stays a guest (platform): a lazy cache of the
type's shape list in `[0x4a9574]` (word +6 of the type ≠ 0: `bases_an.dts`,
ANShape `1E0003h`) or `[0x4a957c]` (`bases.dgs`, `2BC0001h`), arrays
`base_types` allocated, loaded once through `0x474de4`, plus the shape's
material pointer +26h (`0x4a9588`, `0x4a9590`, or `0x4a9d9a` for type 34h);
**it reads only its first argument**, never the factory byte. `0x4539a0`
(STR splitter, also used by the HUD and dialog text loaders) is platform;
the timer element constructor `0x427207` is an empty body (EAX = its
argument), reached only through `0x49667c`, and stays original.

**Compare:** refused for all five (world load: G V A I R, streams and
allocations, the virtual read, the RNG draws of `terrain_cells` below
`zone_load`; leaves: G A, new[]); the after-load snapshot is the acceptance.

### In vitro (group `mission_world`)

`invitro/mission_world_cases.h`, quick tier: 100,000 direct cases per hook
(1,001,024 in the full tier), 256 installed 31/a7 cases each. World load:
31 logging stubs, three synthetic files per case, the core's scratch frame
at the original's frame address (frame offsets, registers and the whole
frame EBP−0E54h .. EBP compared raw), even cases also through the adapter
with the 31 callee counter deltas, plus one real-callee pass (4,096 cases;
real `0x49630c`, `0x49667c`, fourteen nested hooks in random modes). Leaves:
new[] stubbed, original vs core (with ECX) vs adapter. 84 branch sites (82
of the world load, one each of the damage and collision tables), all both
ways, none PROVED; one caught mutation per hook (`MUTATIONS.md`). Core unit
test `//core:mission_load_test`: a worked script with every list and two MEC
records (125 calls checked) and the four leaves.

### Live

Every hook replaced, own runtime: **all suites
identical**, every skipped/bypassed/mismatch counter 0. Each of the five
hooks counts 1 per suite and each leaf's `_call_4774f0` 1 (every suite
selects a mech, so the damage and collision slot words are nonzero). The
world load's callee counters equal the counts predicted from the launch sets
(e.g. `_call_49681a` 10, `_45ee00` 4, `_47749c` 4, `_471ad8` 2 everywhere;
`47af98`/`slot_18`/`405ebc` 254/108/50 in the C1_01 suites, 116/89/11 in
train1-timers). The nested hooks (`region_init`, `timer_init`,
`group_array`, the type selects, `pool_create` 12, `base_pools`,
`mech_types`, `flyer_types`, `zone_load` and the type loads below them,
`hk_rng_next_*` of the terrain draws) are reached and equal
the counts of section 27's run.

After-load snapshot (eight extra regions incl.
`0x4cfcc4:0x44`): O1 = O2 and O2 vs R **identical** (scope all and pools)
in acceptance-60s, c3_10-loadout-hercs and c3_09-loadout-b. Reach: every
global the world load writes lies in the fixed regions of section 13
(`0x4a9e00..0x4aa728` tables, header, variables, rectangle, STR object,
string pointers, result pairs; `0x4aa728..0x4aaa7c` word 4, selection,
remap, MEC fields; `0x4a9572..0x4a9dfc` the pools and lists
`0x4a9b7a`/`0x4a9b7e`/`0x4a9db9`/`0x4a9dbd`, the leaves' tables `0x4a98e4`,
`0x4a9828`, `0x4a9824` and the actor list `0x4a9af8`; `0x4d2571` in the
globals object), every block they allocate hangs off them, and the slot
words `0x4987dc`, `0x498856` are in the extras `0x4987d8:8,0x498850:8`. Only
the guest `0x42e010` writes outside them (header and WLD words
`0x4cfcc4..0x4cfd07`, painter globals); extra `0x4cfcc4:0x44` shows it got
the same header. A difference from these hooks would show in those blocks
or in the block pairing (the hooks allocate the same sizes in the same order
through the same new, new[] and `0x49667c`).

### Open

- A script without groups reads stack residue (above); outside the
  contract.
## 30. The mission-load root

L9 of section 10: the root `0x461608` itself, read from the pinned listing
(strings `0x49f89a..0x49f8b1` read from the file). Own code: hook `mission_root`
in adapter group `mission_root`, core `core/src/mission_root.c` (own module:
no helper shared with the load pieces), with an **entry shim**
(`mission_root_entry`). Of its 63 direct callees 15 are hooks
(`stage_dispatch`, `world_load`, `actor_load`, `removal_append`,
`flush_explosions`, `flush_lights`, `flush_attached`, `flush_debris`,
`pool_prev`, `terrain_height`, `order_setup`, `cockpit_sync`,
`camera_update`, `dist_gate`, `render_root`) in their own modes through the
bridge; the other 48 stay guest calls with a verdict row in
function_verdicts.md (H3, platform or, for the bare RET `0x463370`, "H4
(stays original)"). None of the 48 is simulation code: they draw, load
resources, talk to the window, the sound driver or MCI, or build cockpit and
HUD objects. With these verdicts the root's frontier row closes.

### Call path and arming

One call site, `0x45f42b` in the game main `0x45f160`, once per process
(DBSIM map: 1 in every suite). Next the game main pushes 3
and calls `stage_dispatch` (`0x45f430`), so EAX, EDX, ECX at the root's
return reach the stage-3 callbacks; the replacement returns all three
exactly (through the shim).

The proxy installs every hook in one step (`initialize_forwarding`); with
early arming (section 13, the default) that step runs in the stub at
`0x45f42b`, before the root's first instruction, and the stub (restoring the
call's rel32) then jumps to `0x461608`, which now holds the detour into the
entry shim. No new mechanism and no change to `early_arming.h`: the root's
descriptor and shim are installed with the others, and its own first MCI
call (`0x46313c` at `0x461dc3`) finds the `InitOnce` done. With
`ES2_EARLY_ARMING=0` the proxy arms at that MCI call while the original
`0x461608` is already running, so the one call is never intercepted and
`hk_mission_root_calls` stays 0, like every other load-only hook (section
8).

### Contract (from the bytes)

No arguments. Frame `add esp,-150h`; EBX, ESI, EDI saved at EBP−154h,
−158h, −15Ch (outgoing ESP EBP−15Ch for every call; the only other pushes,
`push esi; push edi` around the `movsd` at `0x4616be`, lie in the next
call's argument slots). EDI = the view block G = `0x4d24bc` throughout; ESI
= EBP−8 until the display walk; EBX = the caller's EBX until the first
drain. Runtime record at EBP−30h (descriptor `0x49f7c9`, context word
EBP−20h: 14h, 8, 2Ch, 20h, 44h, 38h, 5Ch, 50h around the constructions).
Locals (f = EBP):

| Offset | Local |
|---|---|
| f−4, f−8, f−0Ch, f−10h | graphics-library object; camera slot (`[ESI]`); main camera; cockpit |
| f−34h .. f−40h | border sizes |
| f−42h/f−44h | the launch peek's words |
| f−48h, f−4Ch | palette; sound byte |
| f−50h .. f−60h | never written |
| f−68h/f−6Ch, f−70h/f−74h, f−78h, f−7Ah/f−80h | terrain-object loop; MECH walk; terrain object; camera loop |
| f−84h .. f−94h, f−0C4h .. f−0D4h | dead stores (the new objects' `+26h`, `+2Ch`, `+38h`, `+3Eh`, `+44h`) |
| f−98h/f−9Ch, f−0A0h/f−0A4h | cursor; dead computation |
| f−0B0h .. f−0C0h | main camera's arguments, zero words f−0B8h .. f−0B4h |
| f−0D8h, f−0DAh/f−0E0h | display walk; building search |
| f−0E8h .. f−0F4h, f−150h .. f−144h | text rectangle |
| f−0F8h, f−108h/f−118h, f−128h | key device; window and client rectangles; loading-screen name |
| f−134h .. f−12Ch, f−140h .. f−138h | position; vector (0, −6000, 0) |

In order (sites are the original's CALLs):

1. `0x49630c` (EAX = descriptor, entry EDX/ECX); `0x479cfc(0x461304)` (`pop
   ecx`); input set-up `0x45a5dc`; `0x4955c4` when byte `0x4d267d` (`-m`).
2. `GetWindowRect(hwnd [0x4d3610], f−108h)`, `GetClientRect(hwnd, f−118h)`
   through the stdcall thunks `0x49694c`/`0x496970` (no caller clean-up);
   f−34h = right − left, f−38h = bottom − top of the window, f−3Ch/f−40h =
   those minus the client's right/bottom; never read again.
3. Graphics context `0x461294`, pump `0x464c78`, frame clear `0x46548c`,
   launch peek `0x4614c0(f−44h, f−42h)`: the world word to f−44h; a
   nonzero f−42h (section 1's vehicle-8 flag) selects "flyrX".
4. Loading screen: dword `0x49f89a` ("ld_") to f−128h, `_strcat(f−128h,
   flag ? "flyrX" (0x49f89e) : "hercX" (0x49f8a4))`, byte f−121h = '1' +
   world byte, `0x492bec(f−128h, [0x4a09fc])`, palette `0x465f7c` (`pop
   ecx`) to f−48h, `0x481fe0([f−48h])`, pump, `0x461450(f−128h)`.
5. `new(6)` (`pop ecx`) to f−4; when not null `0x480af0(obj, 26D3h)` and
   construction counter `[[0x6cbee4]]` − 1; G+4Eh = f−4.
6. Sound init `0x462410`, six slots: slot 0 the sound byte G+0Ch
   (zero-extended, stored at f−4Ch), slots 1..5 the words f−50h .. f−60h
   (never written: stack residue) under high words EAX, ECX, EDX, EAX, ECX.
   `0x462410` reads only slot 0 (section 1).
7. `0x463370` (bare RET), `0x48dd08`, `0x4913dc`, `stage_dispatch(2)`,
   lights `0x46134c`, limiter reference `0x467840`, `world_load`
   `0x424308`.
8. G+0BBh = 19h if byte G+0B5h else 0Dh (`mov ecx,19h; add ecx,-0Ch`).
9. Terrain objects: f−68h = word `0x4aaa7c`, f−6Ch = `[0x4aaa78]`; loop: CX
   = the word, word − 1 (before the test), CX = 0 or a null entry ends it;
   else `0x4282d8(entry)` (`pop ecx`; the next `mov cx` keeps that high
   word). A negative count runs until a null entry.
10. Mouse queue `0x452ae4`, byte `0x4d1dd6` ← 0, `actor_load` `0x4253d8`,
    `0x44f6f4`, pump.
11. Drains (section 5): while `pool_prev([0x4a961e], 0)` (EBX = the object)
    ≠ 0: `removal_append([0x4a9622], obj)` (EAX = obj), `0x407b3c`,
    `0x4077e8`, `0x409904`; then the same with `[0x4a9642]`/`[0x4a9646]` and
    `0x4083e0`. Globals reread every time.
12. Word `0x4cf880` ← word `0x49abba`, G+2Ah ← 0; MECH walk over
    `[0x4a9b7a]` (f−70h, copied to f−74h before each call): position
    `A+26h..A+31h` to f−134h, `terrain_height([0x4a0be0], f−134h)` to
    f−12Ch, + signed word `[[A+1F2h]+16h]`, + 5000 when word
    `[[A+1F2h]+50h]` ≠ 0; A+2Eh, A+26h, A+2Ah ← f−12Ch, f−134h, f−130h (that
    order); G+2Ah ← A when byte `A+0A3h` (`[A+92h+11h]`) is set.
13. `0x4d2684` ← G+2Ah; `[[0x4a0be0]+125h]` ← `0x428c60` (through f−78h).
14. Group cameras: count = word `[[G+2Ah]+45h]+10h` (f−7Ah, signed against
    dword counter f−80h); per member `new(4Eh)` to `[ESI]` (f−8); when not
    null (context 2Ch) `0x48bd84(obj)`, vtable ← `0x49f8e8`, dead stores
    f−84h .. f−94h, dwords `+32h`, `+4Ah` ← 0, words `+36h`, `+48h`, `+46h`,
    `+44h` ← 0, counter + 1 − 3 (context 20h); `0x4d2688[k]` ← `[ESI]` (EBX
    walks the array).
15. G+60h ← G+2Ah; pump, frame clear, pump. Below 640 (signed dword G+82h):
    `0x492bec("lo_curs" 0x49f8aa, [0x4a0a10])`, `0x45cdf4`, `0x465618`
    (results through f−98h/f−9Ch).
16. f−0A4h ← 0, f−0A0h ← (byte G+7Bh = 1: byte G+0Bh = 0 ? 1 : 0; else word
    G+0AAh ≠ 0: 0; else byte G+0Bh = 0 ? 2 : 0), never read; the CMP at
    `0x461b03` has no consumer. HUD set-up `0x4544c0`; `[0x49dd94]` ←
    `0x461250`, `[0x49dd98]` ← `0x461274`.
17. Main camera: f−0B0h = 9 if signed dword `0x4d3040` > 1200 else 8;
    `new(4Eh)` to f−0Ch; when not null (context 44h) zero words f−0B8h,
    f−0B6h, f−0B4h, vector f−140h = (0, −1770h, 0), f−0BCh = f−0B0h, f−0C0h
    = G+12h, `0x48bda4(cam, [f−0C0h], [f−0BCh], 100h, f−140h, f−0B8h)`
    (EAX/ECX/EDX = slots 0/1/2), vtable ← `0x49f8e8`, dead stores f−0C4h ..
    f−0D4h, the field clears of step 14, counter + 1 − 3 (context 38h);
    G+2Eh ← f−0Ch (also when null).
18. `0x45edb0([0x4d2684])`; `[G+2Eh]` reread: word `+36h` ← 2 when dword
    `+4Ah` is 0 (a null camera faults here in the original); word G+6Eh ← 1.
19. Cockpit: `new(37h)` to f−10h; when not null (context 5Ch)
    `0x429660(D, G+2Ah)` (context 50h); G+4 ← f−10h.
    `0x431530([0x49b088])`.
20. Display walk: `pool_prev([0x4a9b7a], [f−0D8h])` with ESI = previous
    object; per MECH `A+1FEh` ← `0x437c8c(A)`.
21. `cockpit_sync(G+2Ah)`, `camera_update` `0x4011a0` (G+2Eh, site
    `0x461d0c`), `0x42a1f0(G+4, 1)` unless byte G+7Ch is 2, then
    `0x42a1f0(G+4, 0)`; AX ← word `0x4a9e54` (stored at f−0DAh, never
    read).
22. Building search over `[0x4a95a0]` (f−0E0h, in ESI at the call): for a
    building whose group category byte `[[B+45h]+12h]` is 0,
    `dist_gate(B+26h, G+2Ah+26h)` with EBX = B+26h, ESI = player+26h; a
    distance below 40000 (signed) ends it. Nothing uses the result.
23. With a sound context `[0x49f8ec]` and word `0x4aa728` = 0: `[0x49f8fc]`
    = (dword G+0B7h `cdq; idiv 5` remainder) + 2 (EAX the quotient, ECX 5),
    byte `0x49f900` ← 1, `0x46313c`. (`0x461dca..0x461dd1` is dead.)
24. Word G+54h = 0: word `0x49aef6` ← 8, `0x42eb40(G+2Eh)`, `0x43048c`,
    `render_root`; else `0x45d548`. Word G+6Eh ← 0, pump, `0x43048c`, pump.
25. Word G+0AAh ≠ 0: `0x464a14`; else the rectangle: f−0ECh = 140h, f−0E8h
    = 190h (EDX), f−0F4h = 0, f−0F0h = 0 (EAX), f−150h/f−148h and
    f−14Ch/f−144h = min/max of the two pairs (both comparisons of constants
    fall through; the other edges are PROVED unreachable), `0x43122c(1,
    f−150h, 0)`.
26. `0x4328cc`, byte `0x49b105` ← 1, `0x433f7c`, `call [0x4a5824]`.
27. G+18h and G+16h ← byte `0x4d1f41` (EDX zero-extended), `0x45d490(G+16h
    in AX)`; G+0B3h ← byte `0x4d1f42`, G+0B1h ← G+0B3h, `0x45d50c(G+0B1h in
    DX)`; G+3Ah ← byte `0x4d1f40`.
28. Word G+54h ≠ 0: `0x45d85c`. Byte `0x4d1dd6` ← 1 unless word G+1Ah (tape
    playback). `0x45b758`, pump, `0x477abc([0x6c5eb8])` (through f−0F8h),
    word G+76h ← 0.
29. ECX ← f−30h (the record's saved head), `mov fs:[0],ecx`; EDX:EAX are
    `0x477abc`'s.

Every one-slot call is followed by `pop ecx` (the core rereads the slot at
the original coordinate); the image constants (`0x461304`, `0x49f8e8`,
`0x428c60`, `0x461250`, `0x461274`, the strings, the descriptor) are
relocated by the core. Faults of the original are not modelled: a null
player at step 14, a null main camera at step 18 (unless `0x45edb0` stores
one).

**Registers at the callees.** The ECX after `removal_append` and after
`dist_gate` is dead (overwritten before any read), so the framework's ECX of
those two replaced hooks changes nothing; the real-callee pass masks exactly
these two entry ECX values.

### Stack: the entry shim

Some guest callees read stack they did not write: the sound init gets five
residue words in its slots (unread by `0x462410`), and the loaders, the HUD
and cockpit construction, the render root and the start sequences are not
audited (the render root and the logic frame need shims for this reason,
main_loop.md). So the root runs at the original stack coordinate: the shim
keeps EBP = T−4 (T = entry ESP), stores the caller's EBX/ESI/EDI where the
prologue pushes them, reserves a guard-probed gap of 112 pages (448 KiB, the
logic frame's size) and leaves {EBP, EBP−15Ch, ECX} for the adapter; the
core's frame is the original's, and every game call goes through
`ai_behaviour_stack_call` with the EBX/ESI/EDI and return address the core
names (the return address left in the slot below the arguments, as a CALL
does). Each callee thus sees the original's EBP, registers, argument slots
and the bytes earlier calls left below the frame. The shims of
`render_root` (116 pages), `cockpit_sync` and `panel_frame` nest under the
root; the shared shim floor puts their host frames below the root's. The
in-vitro depth probe puts the shimmed root at 459,856 (fast) / 459,972
(preserving dispatch) bytes below the caller: the gap plus 1.1 KiB of
context and host frames. The peak under the load is the larger of this and
the render root's chain, well inside the 1 MiB main-thread stack.

**Compare:** refused (`compare_unsupported`): the root is the whole load
(allocation, file reads, both script passes, the stage-2 callbacks, the
draws). The acceptance is the after-load snapshot and the regression.

### In vitro (group `mission_root`)

`invitro/mission_root_cases.h` (README "Harness notes per group"): 62 stubbed callees,
the graphics slot stub, a 28 KiB arena with real pool links. Quick tier:
100,000 direct cases (core at the original's EBP, frame bytes and every
register raw; one in four also the adapter with callee counter deltas);
1,024 frame cases through the entry shim (fast and preserving dispatch);
the depth probe; 1,024 real-callee cases (all 32 mode combinations). 35
branch sites, 33 both ways, the two constant comparisons
`0x461e57`/`0x461e8d` PROVED. One caught mutation (`MUTATIONS.md`). Core
unit test `//core:mission_root_test` (two worked scenarios, every call in
order).

### Live

Regression with every registered hook replaced incl. `mission_root=replaced`,
early arming, own runtime: all suites identical to the goldens, every
skipped/bypassed/mismatch counter 0 ([hooks.md](../reference-game/hooks.md)).
Measured counters:

- `hk_mission_root_calls` 1 in every suite.
- In every suite `46548c`, `42a1f0`, `43048c` 2, `464c78` 8, and 1 for
  every other unconditional callee, `slot_4a5824`, `492bec` (the
  loading-screen name), `480af0` (new(6) not null), the first render
  (`42eb40`, `45fca8`) and `464a14`.
- 0 in every suite: `4955c4` (no suite sets `-m`); the drain flushes
  `407b3c`, `4077e8`, `409904`, `4083e0` and `478628` (the load leaves no
  explosion or debris object for the drains); `4282d8` (no terrain object
  binned at load); the cursor calls `45cdf4`, `465618` (no suite below 640
  pixels); the intro `45d548`, `45d85c` (word G+54h = 0: every suite takes
  the first render); `43122c` (word G+0AAh ≠ 0).
- `46313c` 1, except `train1-timers` 0.
- `437c8c` = `46e180` = the MECH pool's objects: 10 in acceptance-60s and
  the C1_01 suites except `c1_01-unarmed-keys` 9, `c1_01-tape-demo` 18,
  `c1_01-tape-play` 16; `c1_02-probe` 13; `c1_06-cheat-disable` 16; C1_08
  8; `c2_02-identify` 11; C3_09 and C3_10 16; `c4_05-convoy` 13; C4_08 11;
  `c4_10-probe` 9; C5_05 15; `train1-timers` 1.
- `47749c` = 3 + `48bd84` (the cameras of step 14: 1, 2 or 4); `471c70`
  18..91, always ≥ 2 × (MECH + 1) + 2; `49288c` (buildings the dead search
  measured) 0..54.

The 15 nested hooks are still reached through the bridge (`render_root`'s
count includes the load-time call).

### After-load snapshot reach

The root's simulation writes lie in the fixed regions of section 13: the
view block and camera array `0x4d2684..0x4d26a7` in the globals object
`0x4d24bc..0x4d27b8`, `0x4cf880` in `0x4cf878..0x4cf88c`, and the MECH
positions, `A+1FEh`, the cameras, cockpit and graphics objects in the heap
(reached from the pools and the view block). The root's other writes, the
modal callbacks `0x49dd94..0x49dd9b`, CD track and loop byte
`0x49f8fc..0x49f900`, word `0x49aef6`, byte `0x49b105` and input byte
`0x4d1dd6`, are covered by the extra
`0x49dd94:8,0x49f8fc:5,0x49aef6:2,0x49b105:1,0x4d1dd6:1` (five regions;
with the eight of section 29, 13 of 16).

### Open

- The after-load snapshot (section 14 procedure: `ES2_AFTER_LOAD_SNAPSHOT=1`,
  `ES2_AFTER_LOAD_SNAPSHOT_EXTRA` = the current extras plus the five above,
  O1/O2 original, R every hook replaced, `after_load_snapshot.py compare`
  with `--scope all` and `--scope pools`) has not been run with the root
  replaced.

## 31. Effect pools with resources

The eight load-time functions that load shapes and bitmaps next to their
pools (listing; the exception descriptors, the name template `0x497ba0`, the
base name `0x4a0a74` and the strings `0x497d24..0x4a0b70` read from the
file). Adapter group `effect_pools`, core `core/src/effect_pools.c` (the
pool/list steps of section 15 appear six times in two register variants;
nothing else is shared with `mission_load.c` or `building_types.c`). Every
function of the hook_frontier.md row is a hook; the element constructor
`0x46bbc1` (empty, returns its argument), reached only through `0x49667c`,
stays original (function_verdicts.md).

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

### Call path

Each stage-2 callback is referenced once in the file (the push before
`0x401d64(fn, 2)` at the address above), so the stage-2 dispatcher is its
only caller; `0x408fb8`, `0x4097a8` and `0x4083f8` have one call site each,
`0x40874c` three. All run inside the load after the early arming of section
13, so every hook is entered in every suite.

### Contracts (from the bytes)

All eight are cdecl. The seven with a `push ebp` frame and a descriptor set
up the runtime record through `0x49630c` (EAX = descriptor, EDX and ECX as
at entry, which `0x49630c` keeps); the descriptor's second dword gives the
record offset (`0x497be0` and `0x4a0aa9` −58h, `0x497da0` −50h, the others
−28h), the context word lies at record+10h. The core gets the synthetic EBP
f in a zeroed scratch (`..._FRAME` of `effect_pools.h`) and returns the
record's saved head, which the adapter writes to FS:0 as the epilogue does:
**in EAX** for `explosion_pools`, `meteor_pools`, `fire_pools`,
`smoke_balls`, `flat_pools`; **in ECX** for `debris_pools` (EAX = 0) and
`debris_set` (EAX:EDX = `stream_destroy`'s), whose epilogues load it into
ECX (`0x408931`, `0x40884f`).

Common steps (the pool and list of section 15; slots f−4, f−8; context
words 14h/8 and 20h/8):

- **pool(capacity, size)**: `new(12h)`, ECX = 12h (POP), [f−4] = EAX; when
  nonzero `pool_create([f−4], capacity, size)` with the header in EDX (ECX in
  `flat_pools`); then the header reloaded into ECX (EAX in `flat_pools`)
  and stored to the global.
- **list(capacity)**: `new(8)`, ECX = 8, [f−8] = EAX; when nonzero the
  capacity through AX (CX in `flat_pools`, high word kept) into word +6,
  `new[](4 × capacity)` with the size in EDX (EAX in `flat_pools`), ECX =
  the size (POP), the slot reloaded into ECX (EDX in `flat_pools`) for
  dword [list] = the block, word +4 = 0; then EDX = [f−8] into the global.

Resources: `0x492bec(name, directory)` builds a path (directories
`[0x4a09f8]` shapes, `[0x4a09f0]` data, `[0x4a0a10]` bitmaps; extension
`dts` for `debris_set`'s shapes); `0x474cd8(list slot, path, 140008h, 0)`
loads shapes and returns their count in AX; `0x45cdf4(path)` a bitmap (POP
ECX after it). Names and directories are passed as the original pushes
them; the cores never interpret a resource.

**`explosion_pools` 0x407b54** (record f−58h, stream f−38h, count word
f−5Ah, name buffer f−6Ch): the 16-byte template `0x497ba0` ("explo666")
into f−6Ch by REP MOVSD (four dword reads and writes in order, ECX = 0
after); pool(40, 5Bh) → `0x4a961e`; list(30) → `0x4a9622`;
`0x49667c(0, 8, 15, 1, 0x46bbc1)` → `0x4a9638` (15 bitmap pairs). With the
low-memory word `[0x4d2680]` (option `-l`, launcher.md) nonzero: `_strcat`
(`0x4967ea`) of `0x497d24` ("s") onto the name and 13 bitmaps, else 15;
bitmap k: `_itoa(k, f−67h, 10)` (`0x496820`), `0x492bec(f−6Ch,
[0x4a0a10])`, `0x45cdf4`, stored at `[0x4a9638] + 8k` (pointer reread per
bitmap). The word reread: shapes `explos2` (`0x497d26`) when set, else
`explos` (`0x497d2e`), into `0x4a9630` (count in SI); `new[](4 × count)` →
`0x4a963c` (the map); the data file (`0x492bec(name, [0x4a09f0])`, context
2Ch, `0x472f14(f−38h, path)`; nonzero status reports
`0x479c78(6C56C8h, 5, 0, "EXPLO.CPP", 56h, path)`); the read method (slot
+18h) reads the map count word into f−5Ah, `stream_read(S, 4 × count,
[0x4a963c])`, `stream_read(S, 2, 0x4a962c)` (record count), `new[](40 ×
count)` → `0x4a9628`, `stream_read(S, 40 × count, block)`,
`stream_destroy(S, 2)`. Then word `0x4a9634` = 0 and per record the 16-bit
(word +0) + 1 if not below the stored word (signed), the 32-bit INC of EAX
kept (ECX = EAX or the stored word); last, for every shape i < sx(SI):
[shape i + 26h] = `[0x4a9638]` + 8 × sx(map word 2i+1). Returns EAX = head,
EDX = sx(SI), ECX as the last loop left it.

**`debris_pools` 0x408860** (record f−28h): pool(150, 61h) → `0x4a9642`;
list(40) → `0x4a9646`; `debris_set(0x4a9650, "def_deb", 0)`; EAX = 0, dword
`0x4a964c` = 0, words `0x4a9664`/`0x4a9666`/`0x4a9668` = 6000, 16000, 420;
ECX = head, EDX = `debris_set`'s.

**`debris_set` 0x40874c** (set, name, tag; record f−50h, stream f−30h):
`0x492bec(name, "dts")`, `0x474cd8(set+6, path, 140008h, 0)` with EAX =
set+6, word set+0Ah = AX; `0x492bec(name, [0x4a09f0])`, context 8,
`0x472f14(f−30h, path)` (status: `0x479c78(…, "debris.cpp", A1h, path)`
with EDX = 0); the read method reads word set+4 (record count); `new[](10 ×
count)` → [set]; `debris_record(record k, S)` for k < sx(word set+4)
(reread per record, EAX = S); when the tag (argument slot, reread) is
nonzero, [shape i + 26h] = tag for i < sx(word set+0Ah) (EAX = the count
word at loop end); `stream_destroy(S, 2)` with EDX = S. Returns EDX:EAX of
`stream_destroy`, ECX = head.

**`debris_record` 0x4083f8** (record R, stream S; no runtime record): the
read method (table reloaded from [S] per read) reads word R+0 and word R+2
(piece count n); `new[](14 × sx(n))` (SHL 3, SUB, ADD) → R+4; word R+8 = 0;
while the 16-bit n (reread) > the counter: seven word reads into the piece
(+0..+0Ch, EAX = the word's address), words +8 and +0Ah × 182 (LEA/SHL/SUB/
SHL/SUB/ADD of the sign-extended word, 16-bit store) unless FFFFh, word R+8
+= word +2, the maximum of the signed words +0 (from 0), next piece 0Eh on.
So 2 + 7 stream reads per piece. The two words live in the entry PUSH ECX
slot (f−2 maximum, f−4 counter), so the final POP ECX returns **ECX =
maximum << 16 | counter**; AX = the maximum over EAX's high word.

**`flat_pools` 0x4097a8** (flag; record f−28h; EDI = the pair `0x4a969e`):
byte `0x498098` incremented when 0 (a once-guard nothing else reads); name
`flat2` (`0x498109`) when the flag word is nonzero, else `flat`
(`0x49810f`); shapes into `0x4a9698`, count word `0x4a969c` = AX; the bitmap
of the same name (`0x492bec(name, [0x4a0a10])`, `0x45cdf4`) into
`[0x4a969e]`, its descriptor `0x46a03c(bitmap)` into `[0x4a96a2]`, the
construction count `[[0x6cbee4]]` + 1, `0x48349c([0x4a969e], 3)` (slot
reread) and the slot cleared; [shape i + 26h] = `0x4a969e` while the 16-bit
i < word `0x4a969c` (reread); then pool(150, 41h) → `0x4a968d` and list(25)
→ `0x4a9691` in the ECX/EAX/CX/EDX variant.

**`meteor_pools` 0x409a34** and **`smoke_balls` 0x408fb8** (same code,
record f−28h): pool → `0x4a96aa` (10, 59h) / `0x4a966e` (200, 4Ch);
list(25) → +4; shapes `meteor` / `smoke` (`[0x4a09f8]`) into `0x4a96b4` /
`0x4a9678`, count in BX; bitmap `impact` / `smoke` into `0x4a96b8` /
`0x4a967c`; when sx(BX) > 0, [shape i + 26h] = the bitmap slot's address
for the 16-bit i while BX > i.

**`fire_pools` 0x46b1a8** (record f−58h, stream f−38h, base name f−60h,
name buffer f−74h): pool(10, 5Bh) → `0x6b4f30`; list(10) → `0x6b4f34`; the
five bytes `0x4a0a74` ("fire" and terminator) to f−60h; inline string copy
(REPNE SCASB with ECX = −1 to the terminator, then REP MOVSD and REP MOVSB
of length + 1 to f−74h; EAX = f−74h, EDX = length + 1, ECX = 0 after); with
`[0x4d2680]` nonzero `_strcat(name, "2")`; shapes (`[0x4a09f8]`) into
`0x6b4f28` (count in SI), context 2Ch; the data file of the base name
(`[0x4a09f0]`; status `0x479c78(…, "fire.cpp", 3Bh, f−60h)` with ECX = 0);
the read method reads a dword into f−64h (never read again) and sx(SI)
bytes into `new[](sx(SI))` (block not stored: leaked); `stream_destroy`;
`0x49667c(0, 8, 2, 1, 0x46bbc1)` → `0x6b4f2c`; two bitmaps k = 0, 1: the
copy again, `_itoa(k, f−66h, 10)`, `_strcat(name, number)`, with the word
`_strcat(name, "s")`, the bitmap (`[0x4a0a10]`) into `[0x6b4f2c] + 8k`;
then [shape i + 26h] = `[0x6b4f2c]` + 8 × (signed byte i) for i < sx(SI);
dword `0x6b4f38` = 0. Returns EAX = head, EDX = sx(SI), ECX as the last
loop left it.

**Callees.** Through the guest bridge with the original's entry registers:
`0x49630c` (with the synthetic EBP), `new` `0x47749c`, `new[]` `0x4774f0`,
`0x49667c` (with `0x46bbc1`), the CW3220 thunks `_strcat` `0x4967ea` and
`_itoa` `0x496820`, `0x492bec`, the stream constructor `0x472f14`, the read
method (slot +18h of [S]), `0x479c78`, the resources `0x474cd8`, `0x45cdf4`,
`0x46a03c`, `0x48349c` and the hooks `pool_create` `0x471ad8`,
`stream_read` `0x4735b8`, `stream_destroy` `0x472f64`; inside the group
`debris_pools` → `debris_set` → `debris_record` also go through the bridge,
so each keeps its mode.

**Registers and shims.** EDX:EAX exact; the cores compute the original's
ECX, the adapters return the framework's entry ECX, which no caller or
later callee reads before writing it. No
local is read before an own store or a callee write (the record by
`0x49630c`, the streams by `0x472f14`, f−5Ah and f−64h by the read method,
which with the complete CD files always writes them). Trap: `fire_pools`'
scan stops at the terminator of its five copied bytes; without a zero there
the original would scan into uninitialized stack at f−5Bh and the core into
the scratch's zeros (no writer of `0x4a0a74` exists in the file). No entry
or exit shim; no game instruction embedded. Compare mode refused for all
eight (allocation, file I/O, resources, the runtime record, FS:0).

### In vitro (group `effect_pools`)

`invitro/effect_pools_cases.h`: per hook 100,000 direct cases (quick tier;
1,001,024 in the full tier) against logging stubs for all 18
direct callees, 256 installed 31/a7 cases, a real-callee pass of 2,048
cases (real `0x49630c`, `0x49667c`, nested `pool_create`, `debris_set`,
`debris_record` in both modes): identical; 40 branch sites, all both ways;
one caught mutation per hook (`MUTATIONS.md`). Core unit test
`//core:effect_pools_test` (hand-worked cases).

### Live

Regression with all registered hooks replaced, early arming: all suites
identical to the goldens. In the hook counters every hook of the table is entered once per suite
except `debris_set` (D) and `debris_record` (R) below; the per-hook callee
counters are the same in every suite (explosion `_call_496820` and
`_call_45cdf4` 15, `_call_492bec` 17; fire `_call_492bec` 4; every
`_call_479c78`, `*_skipped`, `*_bypassed` 0; `hk_pool_create_calls` 12),
because `[0x4d2680]` = 0 (no suite sets `-l`; with it explosion's `496820`
and `45cdf4` would read 13, `492bec` 15, `4967ea` 1, and fire's `4967ea`
5).

Per suite D = `hk_debris_set_calls` and R = `hk_debris_record_calls`
(equal to the DBSIM map's entry counts of `0x40874c` and
`0x4083f8`):

| Suites | D | R |
|---|---:|---:|
| the C1_01 suites except `c1_01-tape-play` and `c1_01-tape-demo` (19), `acceptance-60s`, `c1_02-probe` | 6 | 42 |
| `c1_06-cheat-disable` | 7 | 49 |
| `c2_02-identify` | 7 | 50 |
| `c1_08-escort-route`, `c1_08-scan`, `c4_10-probe` | 5 | 35 |
| the C3_09, C3_10 and C5_05 suites (9) | 10 | 70 |
| `c4_05-convoy` | 9 | 64 |
| the C4_08 suites (3), `c1_01-tape-play`, `c1_01-tape-demo` | 8 | 56 |
| `train1-timers` | 3 | 21 |

In every suite `debris_set`'s `_call_49630c`, `_call_474cd8`,
`_call_472f14`, `_slot_18`, `_call_4774f0`, `_call_472f64` = D,
`_call_492bec` = 2D, `_call_4083f8` = R; `hk_debris_record_call_4774f0` =
R; `hk_mech_type_load_call_40874c` = D − 2; `hk_debris_record_slot_18` =
2R + 7 × the pieces (693 for R = 42). The eight
alone: `-Hooks "explosion_pools=replaced,debris_pools=replaced,debris_set=replaced,debris_record=replaced,flat_pools=replaced,meteor_pools=replaced,fire_pools=replaced,smoke_balls=replaced"`.

**Snapshot reach.** The globals written lie in the fixed regions of section
13 (`0x4a961e..0x4a96bb` in `0x4a9572..0x4a9dfc`, `0x6b4f2c..0x6b4f3b`)
except the fire shape list pointer `0x6b4f28` and the guard byte
`0x498098`; every allocated block is in the walked heap (pools with their
objects, lists, bitmap pairs, explosion map and records, debris records and
pieces, the unreferenced fire bytes block); shapes and bitmaps are the
resource loaders' and unchanged. Extra regions: `0x6b4f28:4,0x498098:1`
(with the 13 of section 30, 15 of 16). A difference from these hooks would
lie under those globals (the six pools, lists, bitmap pairs and the shapes'
+26h pointers, the explosion map, records and count word, the debris sets
at `0x4a9650`, `0x4a95c0` and in the MECH types, the three debris words,
the fire bytes block) or in the block pairing (same sizes, same order,
same `new`, `new[]` and `0x49667c`).

### Open

- The after-load snapshot of section 14 (O1/O2 original, R replaced, with
  these extras) has not been run with these hooks replaced.
- The roles of the explosion map words +0, the explosion record word +0
  (`0x4a9634` is their maximum + 1) and the debris piece words are not
  interpreted; the cores copy the original's arithmetic.

## 32. The remaining weapon_fit callees

The seven functions that section 16 left as guest callees of `weapon_fit`
(listing ranges `0x40e61c..0x40e629`, `0x40fea0..0x40feb3`,
`0x420634..0x420652`, `0x413ac8..0x413af5`, `0x417d08..0x417de0`,
`0x40fb2c..0x40fbd8`, `0x417530..0x4175db`; the byte table `0x40fb7a` and
the jump table `0x40fb89` read from the file). A whole-file scan for each
address (as a dword and as every `call`/`jmp rel32` target) finds only the
call sites below (no vtable or data reference). Adapter group `fit_leaves`,
core `core/src/fit_leaves.c`. Every callee of `weapon_fit` is a hook except
`0x40cec0`, `0x40d240`, `0x40d128`, `0x40cb84`, `0x40d284`, `0x40f540`
(sections 20 and 22) and the runtime calls.

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

All seven are cdecl, build no runtime record and do not touch FS:0.
Notation as in sections 15 to 17: `lo(r, w)` replaces the low word of r, sx
sign extension, R a record, W a weapon object, D = `[W+18h]` the mount
definition, C = `[W+1Ch]` the weapon definition.

- **`weapon_shape_word` (W)**: `mov eax,[ebp+8]; mov eax,[eax+18h]; mov
  ax,[eax]`. EAX = lo(D, word [D]); EDX, ECX unchanged. `weapon_fit` passes
  the word (its AX) to the shape virtual `+20h` of `[[A+34h]+4]`.
- **`mount_entry` (type)**: EAX = 6 × sx(low word of type) + `[0x4a98e4]`
  (`movsx eax,word [ebp+8]; add eax,eax; lea eax,[eax+2*eax]; add
  eax,[4a98e4h]`); EDX, ECX unchanged. `[0x4a98e8]` (the running slot base
  of `0x40feb4`) and the table pointer (allocated by `0x40fecc`, 6 × the
  count word) are not touched.
- **`mount_table` (A)**: EAX = A, EDX = `[A+1F2h]` (the type record), CX =
  word [EDX] + word `[0x4a9d70]` (16-bit adds into the entry ECX's low
  word), pushed; `mount_entry(pushed dword)` with those registers; `POP
  ECX` restores it. Result: EAX = entry, EDX = `[A+1F2h]`, ECX = the pushed
  dword. `weapon_fit` reads the entry as the mount table M (count word +0,
  mounts pointer +2); the display-object code at `0x437ce7` scans M's
  26-byte mounts. The gun-list core (`0x40ff15`) and `weapon_fit` read
  `mount_entry`'s and `mount_table`'s EDX after the call.
- **`shield_split` (S = A+222h)**: ESI = sx word `S+4` (the balance,
  `0..400h`, moved by the shield gauge keys, cockpit.md), EDI = sx word
  `S+6` (the total); `multiply_q10(ESI, EDI)` (`0x47e0b0`) with the entry
  registers; its AX to `S+0` (front share), then word `S+2` (rear share) =
  word `S+6` − word `S+0`, both reread after the store. EAX keeps
  `multiply_q10`'s high word; EDX, ECX are `multiply_q10`'s.
- **`energy_rate` (A)**: EDI = 20 (EBX = A, ESI = 20); EAX = EDX = A+92h,
  ECX = entry ECX. Byte `A+0ABh` (A+92h+19h) nonzero: EDI =
  `multiply_q10(200, 20)`; else byte `A+0AAh` nonzero: EDI =
  `multiply_q10(600, 20)`. Then W = dword `[A+313h]` (unaligned; the ENERGY
  slot of the record `A+307h` that `equip_slots` fills): when nonzero, b =
  sx byte `[[W+18h]+17h]`, the pushed ECX = lo(D, b + 13h) (D's high word
  rides along), c = sx(AX(`part_ratio(A+206h, ECX)`)) with EDX = b, EAX =
  A+206h; when c < 225 (E1h, signed) EDI += `multiply_q10(1024 − 204 ×
  trunc(c / 51), 20)` (quotient × 51 and shift by 2 at `0x417da0..0x417dab`;
  EAX = value, EDX = 20, ECX = value at the call). The low word of EDI goes
  to `A+256h`. The pair is the last call's: EAX = 0 without a weapon; for
  c ≥ 225 EAX = `part_ratio`'s, EDX = c; ECX likewise `part_ratio`'s (or
  `multiply_q10`'s). The term is the one of the speed command of
  `vehicle_control` `0x416a04` ([ai.md](ai.md#416a04-vehicle_control-speed-turn-and-the-walkstand-clips),
  item 4: k = 1024 − 204 (r / 51) for r < 225). `vehicle_update` passes
  `A+256h` to `movement_step` (vehicle.md, "vehicle_update").
- **`equip_slots` (S, R)**: `[ebp-8]` = dword `[S+4]` (cursor over the item
  pointers), `[ebp-4]` = word `[S+8]` (count), ECX = lo(entry ECX, count);
  `memset(R, 0, 20)` (`0x49681a`) with EAX = S+4, EDX = cursor, ECX = that.
  Loop (`0x40fbb7`): AX = counter word, the word decremented; while the OLD
  counter is nonzero EDX = cursor (then +4), else EDX = 0; a zero EDX ends
  the loop (a null item array ends it at once). Body: EAX = `[EDX]` (item W;
  null skips), EDX = `[W+1Ch]`, ECX = sx word `[C+56h]` − 12h; above 0Eh
  (unsigned) skips; the byte table `0x40fb7a` (15 entries: 5, ten zeros, 4,
  3, 2, 1) gives the jump-table index (`0x40fb89`: 0 skip, 1 → `R+0Ch`, 2 →
  `R+10h`, 3 → `R+8`, 4 → `R+4`, 5 → `R+0`): weapon ID 18 (ECM) → `R+0`, 29
  (TARG) → `+4`, 30 (SHIELD) → `+8`, 32 (ENERGY) → `+0Ch`, 31 (TURBO) →
  `+10h` (weapon_classes.md, "the five equipment classes"); every other ID
  is not stored; the last item of an ID wins. At the end EDX = 0, EAX =
  lo(EAX, last old counter); the two `POP ECX` leave ECX = dword `[ebp-4]`,
  whose high word was never written (stack residue; the core reports the
  low word).
- **`node_state_fill` (N, v)**: the dword v is kept whole (`push ebx` per
  child), only its low word is stored. EAX = `[N]`, `call [eax]` (slot 0:
  class id c, one stack argument N), `POP ECX`. Signed compare of c with
  140014h:
  - c > 140014h: 140015h → array fill; else return with EAX = c − 1E0005h,
    except c = 1E0003h, which leaves EAX = 0 (1E0005h gives 0 as well).
  - c = 140014h: word `[N+4]` ← v; return.
  - below: 140005h/140006h → return (EAX = c − 140007h); 140007h/140008h →
    children; 14000Bh → word `[N+4]` ← v, children; 14000Ch → children;
    every other value returns (EAX = c − 14000Ch).
  - **Array fill**: while sx(i) < word `[N+16h]` (reread per test): word
    `[[N+1Ch] + 2i]` ← v; then the children.
  - **Children**: ESI = dword `[N+12h]` read once; while sx(i) < word
    `[N+10h]` (reread per test): `node_state_fill([ESI], v)` with EAX = the
    child, EDX = sx(i), ECX = N, then ESI += 4. Pre-order, children left to
    right; the core recurses on the host stack in that order. The self call
    bypasses the detour, so replaced `hk_node_state_fill_calls` counts
    top-level calls only (the map's original-mode entries count every level,
    e.g. 467 in `acceptance-60s`).
  - Exit: after a children loop EAX = word `[N+10h]`, EDX = sx(i); on a
    return path EAX is the subtraction residue, EDX the class call's. The
    two `POP ECX` leave ECX = `[ebp-4]`, written only by the array fill (N),
    else stack residue (the core reports 0).
  - `weapon_fit` calls it for `[W+10h]` ≠ 0 with the node tree `[W+10h]` and
    the word `[result+4]` of the shape virtual `+20h`.

**Registers and shims.** EDX:EAX exact for all seven. ECX is dead at every
call site (`pop ecx`, or `add esp,8` followed by writes before reads), so
the adapters return the framework's entry ECX; the cores report the
original's ECX for the in-vitro compare (`equip_slots` the low word,
`node_state_fill` only for class 140015h). No local
or stack word is read before written: no entry or exit shim. All seven
refuse compare mode (guest calls, recursion, load-time state).

### In vitro (group `fit_leaves`)

`invitro/fit_leaves_cases.h` (identifiers `fit_`/`FIT_`): per hook
1,001,024 direct cases in the full tier (100,000 quick) over a 10 KiB arena
against logging stubs for every direct callee, 256 installed 31/a7 cases,
a real-callee pass of 3,072 cases (`mount_table` over the real
`mount_entry`, `shield_split` and `energy_rate` over the real
`multiply_q10`, `energy_rate` over the real `part_ratio`): identical; 19
branch sites (`energy_rate` 4, `equip_slots` 4, `node_state_fill` 11), all
both ways, the table `jmp` of `0x40fb73` through its five targets; one
caught mutation per hook (`MUTATIONS.md`). Core unit test
`//core:fit_leaves_test` (486 hand-worked checks).

### Live

All seven run during the load only. Regression with all registered hooks
replaced, early arming: all suites identical to the goldens; hook counters:

| Counter | a60 / c1s / c1c | c3_09 | c4_08 | c3_10 | c1_02 | c5_05 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `hk_equip_slots_calls`, `hk_shield_split_calls`, `hk_energy_rate_calls` (= `hk_weapon_fit_calls`) | 10 | 16 | 11 | 16 | 13 | 15 |
| `hk_weapon_shape_word_calls`, `hk_node_state_fill_calls` (= `hk_weapon_fit_call_40e61c` = `_call_417530`) | 15 | 58 | 31 | 48 | 19 | 55 |
| `hk_mount_table_calls` (fits + display-object code) | 59 | 133 | 82 | 114 | 77 | 132 |
| `hk_mount_entry_calls` (= `mount_table` + `hk_gun_list_load_call_40fea0`) | 63 | 141 | 89 | 122 | 81 | 140 |

These equal the map's entry counts of `0x40e61c`, `0x40fb2c`, `0x420634`,
`0x40fea0` in `acceptance-60s`, `c3_09-loadout-b`, `c4_08-razor-plasma`,
`c3_10-loadout-hercs`, `c1_02-probe`, `c5_05-moon-missiles`. Other C1_01
suites equal `acceptance-60s` except `c1_01-unarmed-keys` (9 / 11 / 55 /
59), `c1_01-tape-play` (16 / 51 / 136 / 142) and `c1_01-tape-demo` (18 / 56
/ 122 / 128). In every suite `hk_mount_table_call_40fea0` =
`hk_mount_table_calls`, `hk_shield_split_call_47e0b0` and
`hk_equip_slots_call_49681a` = their hook's calls;
`hk_energy_rate_call_47e0b0` = `_call_40dbc0` = 1 in the C3_09, C3_10 and
C5_05 suites and `c1_01-tape-play`, else 0; every `*_skipped`/`*_bypassed`
0. The seven
alone: `-Hooks "weapon_shape_word=replaced,mount_entry=replaced,mount_table=replaced,shield_split=replaced,energy_rate=replaced,equip_slots=replaced,node_state_fill=replaced"`.

**Snapshot reach.** The seven allocate nothing and write only heap blocks
the snapshot walks: the actor words `A+222h`/`A+224h`, `A+256h`, the record
`A+307h..A+31Ah`, and the state words `+4` and word arrays `[N+1Ch]` of
each weapon's node tree `[W+10h]`; no fixed global, so the extras are those
of sections 30 and 31. A difference would lie in a block reached through
`[A+202h]` or `A+307h`, in those actor words, in a node tree, or in the
block pairing.

### Open

- The after-load snapshot of section 14 (`acceptance-60s`,
  `c1_01-collision`, `c3_10-loadout-hercs`, `c3_09-loadout-b`) has not been
  run with these hooks replaced.
- The roles of the shape-node classes (140005h..140016h,
  1E0003h..1E0005h; their slot-0 bodies were not read), of the node state
  word `N+4`, of the fill and of the flags `A+0AAh`/`A+0ABh` are not
  interpreted; the cores copy the original's compare chain and arithmetic.

## 33. The mission load in the own process

The root `0x461608` (section 30) runs in `es2sim.exe`; every guest function
it reaches that is neither a hook nor an earlier service is answered by own
code in `sim/src/load.c`, in the root's call order (each function's comment
names its listing range). Validated by unit tests on a synthetic image and,
for the last callees, in vitro (section 34); the after-load snapshot of
`es2sim` against the original's is **identical** (section 14, "es2sim's
snapshot against the original").

### What runs

302 service rows (`sim/dispatch/services/mission_load.h`), in the order the
root reaches them:

| Root step | Own code (VAs) | Notes |
|---|---|---|
| graphics context | `0x461294` (object vtable `0x4a2e10`, the 0x22c-byte palette copy, `[0x4d24ce]`, `[0x6c5f70]`, `[0x4a3614]`) | |
| frame clear, launch peek | `0x46548c`; `0x4614c0` with the stream close and reopen; the path builders `0x492b90`, `0x492bec` | `_open` gets the mode slot 0 (read only, no `O_CREAT`) |
| loading screen and palette | `0x461450`, `0x45cdf4`/`0x465f7c` (load by name through the VOL stream `0x472f14`), `0x4820d4`, `0x4821cc`, `0x481fe0` (range table `0x4a4fa5`, `0x465e28`, `0x465a24` into the LOGPALETTE `0x4d3728`), `0x4816a8`, `0x464a14`, `0x48349c`, `0x4815b8` | headless, see below |
| arena and devices | `0x47a288`, `0x47a2c8`, `0x474bbc`, `0x474be4` (the pop to index -1 keeps the original's `0x6c595c` quirk), `0x480af0`, `0x496406` | |
| sound | `0x462410`, `0x463624`, `0x463694`, `0x463370` | own up to the driver load `0x473708` (audio rows) |
| renderer and classes | `0x48dd08`, `0x4913dc`, the TS/AN factories and reads (22 constructors, 12 reads, `0x48f9a0`, `0x48fea0`, `0x490e68`, `0x476710`, `0x490a34`, `0x4922d3`, `0x4922e6`, `0x49090f`), the grid `0x427568`/`0x42762c`, the shape loaders `0x474b30`, `0x474cd8`, `0x474de4`, `0x474c10`, `0x474c38` (its twin `0x474c74` has no static reference and no row), `0x474cb0`, `0x47a308`, `0x47a3b4`, `0x47a540`, the part lookups `0x47622c`, `0x476850`, `0x476890` | |
| stage-2 callbacks | texture pages `0x46a03c`/`0x469e14` (pages at `0x4d4f04`), lights `0x48bc40`, `0x406e44`, `0x4800c0`, `0x46134c`, `0x48bcec`, `0x48be50`, `0x407800`, `0x4953c4`, `0x415464`, `0x40ac38`, the binning tables `0x428230` with `0x428e10`, row buffers `0x43034c`, `0x492f54`, colours `0x467b64`, five inner-array constructors, the string holder `0x453878`/`0x453918`/`0x4539a0` | the effect pools `0x407b54`, `0x408860`, `0x409a34`, `0x46b1a8`, `0x408fb8`, `0x40874c`, `0x4083f8` (and `0x4097a8`, from the world loader) are section 31's hooks, reached through the dispatch |
| world load callees | `0x42e010` with the sky, range table `0x430d08`, shade tables `0x42e760`, cycle registration `0x430638` (reads the virtual clock), remap arena `0x46a464`, world textures `0x46bd9c`, reciprocals `0x4680a0` (the flat objects `0x4097a8` through the dispatch); `0x405ebc`, `0x492d00`, `0x492c84`, `0x492c4c`, the element stores, terrain shading `0x46c2e0` (section 34), the height cache `0x4952e8`/`0x4952fc`, `0x4954e4` | the world loader's 12-byte-stride overflow is kept |
| actor load callees | mounts `0x42067c`, `0x40fab0`, `0x40dd4c`, weapon displays `0x4378d8`, damage display `0x4379cc`, weapon shapes `0x40f998`/`0x40fa88`, volumes `0x40c834`/`0x40ce28`, `0x4205cc`, `0x411d64` (`0x40e61c`, `0x40fea0`, `0x420634`, `0x413ac8`, `0x417d08`, `0x40fb2c`, `0x417530` are section 32's hooks), the memory stream (`0x47a888`, `0x47a8a4`, `0x47b36c`, `0x481aea`, `0x47b3a0`, `0x47b400`, `0x47b410`, `0x47b45c`, `0x47b4b4`, `0x47b454`) | |
| trivial bodies | 63 constant returns, empty bodies, argument returns, member addresses | each row names its listing bytes |

### Headless decisions

- **Display answers** (`es2_sim_load_platform`, measured on the host):
  `GetSystemPaletteUse` 0, `GetDeviceCaps(NUMCOLORS)` -1 (index 0x18 at
  `0x465ae9`, section 34; field `num_colors`), `GetSystemPaletteEntries` 0;
  the palette handle and paint DC are constants. With these the original
  computes the same LOGPALETTE; the colour table a presenter would upload is
  kept in `es2_sim_load_platform.color_table` with an upload count (for the
  presentation frame, presentation.md).
- **Blit and present** (`0x4816a8`, `0x464a14`): `0x4816a8` writes no
  pixels; the row of `0x464a14` points at present.c's
  `es2_sim_service_present_capture` (section 34). The original's blit
  allocates and frees transient heap blocks, the own code does not, so the
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
| `0x473708` | SOS driver load (inside `0x463694`) | `sim/dispatch/services/audio.h` (audio rows) |
| `0x44f6f4` | root step 10: the map bitmap and graphics-context set-up (711 instructions, 82 callees; writes the map dimensions `0x4d1ce8`/`0x4d1cea` that the map hooks read) | `hud_load.h` |
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

**Audio rows.** The driver load `0x473708`, the table init `0x46254c`, the
CD start `0x46313c` (step 23) and `0x4328cc` (step 26) are audio rows
([audio.md, audio in the own process](audio.md#audio-in-the-own-process)).

### Tests

`//sim:load_test` (synthetic image: graphics context, path builders, the
palette set with the headless answers, the arena allocator, texture pages,
the sound init with and without a context, the memory stream's read, seek
and tell, the mount offset walk over class tags, small bodies), with a
caught mutation (the end-of-data status 2 -> 1 of `0x47b3a0`);
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

## 34. The root's last callees, the terrain shading pass

Every row below was compared in vitro against the original's machine code
(`--load-rest`, see Tests). Rows in
`sim/dispatch/services/mission_load_tail.h`, code in `sim/src/load.c`.

### NUMCOLORS, not reserved entries

The palette set's system-entry count is `GetDeviceCaps(dc, 0x18)`: the
index pushed at `0x465ae9` is 0x18, NUMCOLORS. `load.h`'s field is
`num_colors`; the count `r` is NUMCOLORS / 2 (`sar 1` with the `jns`/`adc`
fix-up, rounding toward zero like C's `/`). On a true-colour desktop GDI
answers -1, so `r` = 0. Section 33 and [flyer.md](flyer.md) use this.

### New rows (`sim/src/load.c`)

| VA | Row | Listing | What |
|---|---|---|---|
| `0x433f7c` | `cockpit_step` | `0x433f7c..0x43400f` | one step of the cockpit H = `[0x49b088]`: H+0x232 == `[H+0x226]` sets `[H+0x22a]` to H+0x236; otherwise `[H+0x226]` is set to H+0x236 and the empty `0x48992e` (word `0x4d2566` nonzero) or `0x486e70` (zero) is called with (`[P+4]`+4 or 0, P+8), P = `[H+0x236]`. Both callees only save and restore registers, so the row computes their effect, the registers the original leaves (EAX/EDX/ECX per path, in the code comment) |
| `0x465618` | `cursor_sprite_set` | `0x465618..0x465624` | `[0x4d3724]` gets the argument (the cursor sprite the present draws); EAX is the argument |
| `0x4955c4` | `mono_out` | `0x4955c4..0x4955d3`, `0x49571c..0x495748`, `0x4956e4..0x4956f4` | only with `-m`: `0x49571c(0x6cbc50, 0x20)`. With the DARKMONO.VXD handle `[0x6cbc50]` = -1 nothing happens: EAX 0, ECX 0x6cbc50. With any other handle the original calls `DeviceIoControl(h, 5, &0x20, 1, 0, 0, 0, 0)`; the own process opens no VxD, so the row refuses that path |
| `0x46c2e0` | `terrain_shading` | `0x46c2e0..0x46c3ed`, `0x46c3f0..0x46c49d`, `0x46bfdc..0x46c238` | own code, see below |

**The terrain shading pass `0x46c2e0(G)`:**

- First `0x48c444(0)` (the R3D object select, camera.md).
- For each row j < 1 << `[G+0x104]` and column i < 1 << `[G+0x100]` (both
  bounds signed, reread every iteration): `0x46c3f0(G, s, i, j)`, s = the
  row's cell + 16 i. The row's cell is NULL when its index is negative or
  ≥ 1 << ((`[G+0x100]` + `[G+0x104]`) & 31); the original would then write
  near NULL, so the own code refuses, but only if the inner loop runs.
- A second loop over the empty `0x46c4bc` has no effect and is not called.
- Exit: EAX = 1 << `[G+0x104]`; ECX = the second loop's row counter popped
  from its stack slot at `0x46c3eb` (1 << `[G+0x104]` when positive, else
  0); EDX = 1 << `[G+0x100]` when that loop ran at least one row, else 0.

**`0x46c3f0(G, s, i, j)`:**

- The last row or column (j ≥ (1 << `[G+0x104]`) − 1 or i ≥ (1 <<
  `[G+0x100]`) − 1, signed) gets flat normals (0, 0, 0x800) in both
  triangles; every other cell `0x46bfdc(G, i, j)` (own function, no row),
  then the hook `normalize(s+1, 0x800)` and `normalize(s+7, 0x800)`.
- Shade bytes: s+0xd = AL of `0x48c16c(s+1, 0x6b4f5d)`, s+0xe = AL of
  `0x48c16c(s+7, 0x6b4f5d)` (`0x48c16c` is the renderer's terrain
  lighting).
- `0x46c49f..0x46c4b5` is unreachable (after an unconditional `jmp`).

**`0x46bfdc(G, x, y)`:**

- Reads the corner heights (first byte of each cell): h00 (cell (x, y),
  which it writes), h01, h11, h10. d = (h00 + h11) − (h01 + h10) picks the
  split; split bits 0..1 of s+0xf and the two 16-bit normals:
  - d = 0: bits 1; both normals (h00−h10, h00−h01).
  - d > 0: bits 0; normal 1 (h00−h10, h00−h01), normal 2 (h01−h11,
    h10−h11).
  - d < 0: bits 2; normal 1 (h01−h11, h00−h01), normal 2 (h00−h10,
    h10−h11).
- The z words s+5 and s+0xb = (1 << `[G+0x108]`) idiv `[G+0x118]`; then all
  six words shifted left by one.
- Refusals: a NULL corner (impossible inside the grid for shift sums below
  32); a faulting idiv (reported at `0x46c208`).
- Trap: a zero normal faults inside the original `normalize` at its idiv
  `0x46c29e`, so the test inputs keep the z word nonzero.

### Slot `[0x4a5824]` and the rest of the root

Slot `[0x4a5824]` is zero in the image; `0x4893aa` fills it at run time by
copying the graphics driver's table to `0x4a57ac` (`rep movsd`). For the
BMP_8 driver (table `0x4a586c`) entry 30 is `0x4898df`, which has the row
`driver_saves_4898df`. Every direct callee of the root `0x461608..0x461f77`
has a row or a hook (section 33). The intro and drop sequence `0x45d548`,
`0x45d85c` runs only when G+0x54 is set.

### The present `0x464a14`

`0x464a14` pushes `0x4d2ff8` and `0x4d3018`, calls `0x465628`, `add esp,
8`, `ret`. `0x465628` with DirectDraw off takes the GDI branch at
`0x46589a`:

- `[0x4d3034]` = 0: nothing happens; EAX, ECX, EDX keep their entry values.
- Otherwise `GetClientRect`, `InvalidateRect(TRUE)`, `BeginPaint`; with a
  palette `[0x4d302c]` `SelectPalette(FALSE)` and `RealizePalette`; the
  scales by `idiv` with `[0x4d253e]`/`[0x4d2542]`; `StretchBlt(SRCCOPY)` at
  `0x4659ad`; the old palette back (`SelectPalette(TRUE)`,
  `RealizePalette`) and `EndPaint`. EAX = EndPaint's result.

`present()` in `sim/src/present.c` reproduces this, GDI calls and arguments
included, compared in vitro (`differential_present_test`). The row of
`0x464a14` (`sim/dispatch/services/mission_load.h`) points at
`es2_sim_service_present_capture`. It returns EAX = 1 on the no-DC path,
where the original keeps the entry EAX; no caller reads that EAX (the root
continues at `0x461e21`; `0x45719c`'s caller at `0x4570ba` ignores it; the
`0x466xxx` callers end in `xor eax, eax`).

### The cockpit step `0x433f7c`

Also called by the display transition `0x42a9c0`; one row
(`cockpit_step`). display.c's `0x433ee8` is its twin with the record
H+0x23a instead of H+0x236.

### Tests

- `//sim:load_test`, case `load_rest`: the shading pass on a 2 x 2 grid
  with stand-ins; a shift of 31 (no rows); a zero divisor (refused); the
  cockpit step's three paths; the cursor store; the mono output with handle
  -1 and with another handle (refused).
- `//tools/reference-game/invitro:differential_load_rest_test`
  (`--load-rest`, `load_rest_cases.h`): shading, 1500 cases identical
  (world, DATA, heap, EAX/EDX/ECX), with 0..3 lights of the three kinds and
  the view pointer as the identity or a random matrix; small, 1000 cases
  identical; the own-side refusals. Coverage of one run: 106 empty passes,
  78 without cells, 8482 edge cells, 9766 interior cells (split level 4860,
  positive 2458, negative 2448); the cockpit step took each of its four
  paths at least 230 times. normalize and view_cases.h's ten matrix and
  vector hooks run as the original's code on the own side. The ECX exit
  value of the shading pass is checked by these cases.
- Mutation: word s+3 = h01 − h00 instead of h00 − h01 on the d < 0 path is
  caught (the world differs in case 3).
