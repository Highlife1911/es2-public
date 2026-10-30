# VSHELL mission map

Contract of the mission map of the reference `VER95/VSHELL.EXE` (SHA-256
`1534aa8f…a16e`, [reference_version.md](../reference_version.md), image base
`0x400000`): the map object built by `0x40e75f` (`src\shellmap.cpp`, code
`0x4242a0..0x4284d4`), its inputs, the projection, the cached terrain image,
the draw order, the icons, the fly-over animation on the MISSION page, the
six navigation arrows and the raster primitives the map draws with. The own
implementation is `shell/src/gui/mission_map.c` (section 14).

**Evidence.** **C** = read from the canonical `llvm-objdump -d -M intel`
listing at the cited addresses, with jump tables and constants read from the
file bytes and strings from the data section. **V** = in addition matched
byte for byte by `//shell:mission_map_real_test` against VSHELL's own code in
the mapped original (section 14.1). Sections 4 to 10 and 13 are V except
where marked **refused** (section 14.2) or **H** (hypothesis). Equality in
vitro is not a live capture.

Related contracts: [vshell_presentation.md](vshell_presentation.md) (frame,
blitter, viewport/clip state, DPL table, the page present `0x406264`),
[vshell_process.md](vshell_process.md) (main loop step 6, the image panels of
its §11.4), [vshell_widgets.md](vshell_widgets.md) (the MISSION screen
controls), [vshell_ui.md](vshell_ui.md) §4.8 (the MISSION pages),
[mission.md](../formats/mission.md) (the prepared `script.dat` framing and
`player.mec`), [dat.md](../formats/dat.md) (`ZONE*.DAT`, `MAT0.DAT`,
`MFORMS.DAT`), [str.md](../formats/str.md), [dbm.md](../formats/dbm.md),
[terrain.md](terrain.md) (DBSIM's zone load, whose VSHELL twin is `hgrid.cpp`
below), [rng.md](rng.md). The DFN format of `dfn\map.dfn` is not needed: the
map never draws text (§4.2).

## 1. Summary

- The map is **not a widget**. It is a 3Space view object (`0x3c5` bytes,
  vtable `0x4722c8` at offset 0, base constructor `0x42041c`) that draws
  straight into the 640 × 480 frame inside the rectangle (291, 67)–(585, 292)
  and is shown only by the page present `0x406264` (§2, §6).
- It is a **perspective top-down view** of a cached terrain image: a camera
  at altitude `z` looks straight down (pitch `0xc000`), focal shift 7
  (= 128 pixels), centre pixel (438, 180), north up (§6).
- The terrain image is built once per map object from the zone height grid:
  two colour-banded triangles per cell, palette indices `0xd1 +
  min(h, 127) / 5` (§7).
- Drawn on top, in this order: grid lines every 200,000 world units (index
  15), the mission bounding box (index 10), the player route (index 14),
  base/object icons from `script.dat`'s base list, numbered waypoint markers,
  the player squad icons (§8).
- **No text is drawn.** `data\mission.str`, `data\maplabel.str`,
  `dfn\map.dfn` and `dba\mapshp.dba` are loaded but never drawn; enemy mechs
  and flyers get no icon (§4.2, §8.5).
- On the first opening of page 1 after the map was built, a timed fly-over
  runs (overview → squad → each route waypoint → overview); Esc, Space or a
  click skips to the final zoom-out (§9). Later openings repaint the final
  view once.
- The six arrow buttons right of the map pan north/south/west/east and zoom
  in/out, one to four steps per click (§10).

## 2. Lifetime and call sites

| What | Address | Effect |
|---|---|---|
| shared tables | `0x424408` (start-up `0x401d62`) | `dba\mis_icon.dba` → `[0x471db8]`, `dba\mapshp.dba` → `[0x471dbc]` (both by `0x4242a0`: VOL-aware stream `0x403065`, DBA loader `0x4041ab`), `dfn\map.dfn` → `[0x471dc0]` (`0x424354`); each only if not loaded; `[0x48a39a]` := 0 |
| free tables | `0x4244d6` (loop end `0x40220d`) | `0x455230(table, 3)` for both DBAs, the font's virtual destructor |
| build | `0x40e75f` (callers `0x40ec14`, `0x44dba3`, `0x44dca9`: load and mission preparation, vshell_ui.md) | deletes the old object `[0x46f3a4]` (`0x42489e(obj, 3)`), `new(0x3c5)`, constructor `0x424597(obj, &rect)` with rect = (min, min, max, max) of (0x123, 0x43), (0x249, 0x124) = **(291, 67, 585, 292)**, stores `[0x46f3a4]`, then `0x427634(obj)` (the terrain image, §7) |
| destructor | `0x42489e` | frees the camera object `+0x4e`, the arrays `+0x52`, `+0x56`, the route children and `+0x5a`, `+0x5e`, `+0x62`, `+0x6e`, the route copy `+0x66`, both STR readers, then the base destructor `0x42057b` |
| animation | main-loop step 6, `0x4019c6` (vshell_process.md) | when `[0x46c269]` ≠ 0, §9 |
| arrows | `0x445577`, `0x445646`, `0x445715`, `0x4457e4`, `0x4458b3`, `0x445982` | §10 |

`0x40e75f` is the only constructor call; `0x427634` runs only there, so
every paint finds the cached image (§8.2). `0x4249ec` (copy the overview
point to the camera, clear, set `+0x186`) has no caller and no pointer
(dead: a byte scan of the file for the address finds nothing).

The constructor `0x424597` builds the base view (`0x42041c`), stores the
vtable, constructs a 20-element array at `+0x82`, two STR readers at
`+0x1b7` and `+0x219` (`0x4284d4`), sets `+0x3c1` = 0, the animation flag
`+0x182` = 1, the counters `+0x183..+0x186` = 0, the start point `+0x187`,
`+0x18b` = 0, `+0x18f` = 100,000; reads `data\mission.str` group 0 into the
pointer table `+0x1c7` (count `+0x217`) and `data\maplabel.str` groups 0 and
1 into `+0x229` (count `+0x2f1`) and `+0x2f3` (count `+0x3bb`) (STR readers
`0x428521`/`0x4286e2`, max 0 = no bound, str.md). It creates a `0x34`-byte
object at `+0x4e` (vtables `0x4722a0`/`0x4722b4`, `+0x16` = 1, slot 1 with
256, words `+0x10..+0x14` = `0xf4f2`, `0xf61a`, `0xfa04`, registered with
`0x45deec`); **H:** a light source of the 3Space library; nothing in the map
draw path reads it. Then the script reader `0x424a2b` (§4.3), the overview
`0x4258a2` and squad view `0x425935` (§5), and the animation state `+0x172`
= 0, `+0x174` = 0, `+0x176` = 0, `+0x17c` = 0, `+0x17e` = 0, `+0x17b` = 2,
`+0x17a` = 0 (§9).

## 3. Object layout

| Offset | Size | Meaning |
|---|---|---|
| `+0x00` | 4 | vtable `0x4722c8` (§10 lists the slots) |
| `+0x04` | 4 | drawing context (`0x239` bytes, ctor `0x452da0`); its `+0x210..+0x21c` = the rectangle (291, 67, 585, 292), `+0x220`/`+0x224` = −(294 sar 1) = −147, −(225 sar 1) = −113, clip mode `+0x208` = 1 (clip to the rectangle) |
| `+0x08` | 4 | the previous current context, saved by `0x420686`, restored by `0x4206a4` |
| `+0x0c` | 4 | camera (`0x22` bytes, `0x45de44(cam, ctx, 7, 128, &+0x12, 0x4719b4)`): position copy at camera `+4`, angles at camera `+0x10` = (`0xc000`, 0, 0) (`0x420839`), context `+0x16`, perspective shift `+0x1a` = 7, `+0x1e` = 128 |
| `+0x10` | 2 | shift 7 (used in the altitude and icon-size formulas) |
| `+0x12`, `+0x16`, `+0x1a` | 3 × 4 | view position x, y and altitude z; initially (0, 0, 200,000) (`[0x471988]`) |
| `+0x26` | 4 | grid line spacing in 1/16 pixel (§8.3) |
| `+0x3a`, `+0x3e` | 2 × 4 | pan offset x, y (arrows) |
| `+0x42` | 4 | pan step (§10) |
| `+0x46`, `+0x4a` | 2 × 4 | rectangle width, height = (585 − 291, 292 − 67) = (294, 225) |
| `+0x4e` | 4 | the `0x34`-byte object of §2 |
| `+0x52` | 4 | coordinates, 12 bytes each (x, y, z dwords), count word `+0x74` |
| `+0x56` | 4 | angle words, count `+0x76` |
| `+0x5a` | 4 | routes, 6 bytes each (count word, pointer to the child words), count `+0x72` |
| `+0x5e` | 4 | mech records (134 bytes), count `+0x7a` |
| `+0x62` | 4 | base/object records (52 bytes), count `+0x78` |
| `+0x66` | 4 | 6-byte copy of the player route (count word, child pointer) or 0 |
| `+0x6a` | 4 | order records (14 bytes), count `+0x7c` |
| `+0x6e` | 4 | actor groups (156 bytes), count `+0x7e` (asserted ≠ 0) |
| `+0x80` | 2 | N = the record count of `data\player.mec` (squad size) |
| `+0x82` | 20 × 12 | squad member positions (§4.5) |
| `+0x172` | 2 | animation state (§9) |
| `+0x174` | 2 | the state after a wait |
| `+0x176` | 4 | timer (ticks of 16 ms) |
| `+0x17a`, `+0x17b` | 1, 1 | done flag; final-paint countdown (2) |
| `+0x17c` | 2 | mission.str line index |
| `+0x17e` | 4 | last non-empty line pointer (§8.8) |
| `+0x182` | 1 | first-paint flag: delays, no bounding-box outline |
| `+0x183`, `+0x184`, `+0x185` | 3 × 1 | revealed squad icons, revealed route points, revealed waypoint markers |
| `+0x187`, `+0x18b`, `+0x18f` | 3 × 4 | overview point (x, y, altitude) |
| `+0x193`, `+0x197`, `+0x19b` | 3 × 4 | squad view point |
| `+0x19f`, `+0x1a3` / `+0x1ab`, `+0x1af` | 4 × 4 | mission bounding box min x, min y / max x, max y |
| `+0x1b7`, `+0x1c7`, `+0x217` | | mission.str reader, group-0 pointers (20 slots before `+0x217`), count |
| `+0x219`, `+0x229`, `+0x2f1`, `+0x2f3`, `+0x3bb` | | maplabel.str reader, group 0 (50 slots), count, group 1 (50 slots), count |
| `+0x3bd` | 4 | maximum altitude (= the overview altitude) |
| `+0x3c1` | 4 | the cached terrain bitmap (`0x48a3cf`) |

The STR group tables have no bound (max 0): a group with more strings than
slots would overwrite the following fields (the missing bound is read from
the listing; whether any CD file has such a group was not checked).

## 4. Inputs

### 4.1 Shared tables

`dba\mis_icon.dba` (`[0x471db8]`, element array `+8`): the icon bitmaps,
frames 3, 5, 8, 9, 10, 11, 14..22, 24, 25 are used (§8). `dba\mapshp.dba`
(`[0x471dbc]`) and `dfn\map.dfn` (`[0x471dc0]`) are loaded and freed only:
no other reference to either global exists (every literal reference in the
listing).

### 4.2 Text files: loaded, never drawn

- `data\mission.str` group 0: only `strlen` (`0x46ba5a`) of its lines is
  used, in the animation (§9) and in `0x427584` (§8.8). Every delay derived
  from it is multiplied by `[0x471db4]` = 0 (data value 0, no writer).
- `data\maplabel.str` groups 0 and 1: no read of `+0x229`/`+0x2f3`/
  `+0x2f1`/`+0x3bb` after the load.
- No text routine is called anywhere in `0x4242a0..0x4284d4` (the full
  callee list of the range). So the map shows **no labels**.

### 4.3 `data\script.dat` (`0x424a2b`)

Read stream `0x44ed84` (the script, not VOL-aware), header of 20 bytes
(10 words) at EBP−0x50, then the 13 lists of [mission.md, prepared script
framing](../formats/mission.md#prepared-script-exact-framing), in order:

| List | VSHELL map | Kept as |
|---|---|---|
| coordinates (12) | read; each (x, y) widens the bounding box | `+0x52`, `+0x74` |
| angles (2) | read | `+0x56`, `+0x76` |
| routes (2 + 2K) | read, children allocated | `+0x5a`, `+0x72` |
| regions (6), triggers (74), timed triggers (24) | skipped by seek (`0x44ef10`) | — |
| mechs (134) | read | `+0x5e`, `+0x7a` (only the count is used, §4.5) |
| flyers (92) | skipped | — |
| base/objects (52) | read | `+0x62`, `+0x78` |
| orders (14) | read | `+0x6a`, `+0x7c` |
| actor groups (156) | read, count asserted ≠ 0 | `+0x6e`, `+0x7e` |
| objectives (54) | skipped | — |
| mission-string indices | not read | — |

**Bounding box:** min/max of the coordinates' x and y, then min −= 10,000 and
max += 10,000 (`[0x471d34]`, `0x424bc1..0x424be6`; z untouched). An empty
coordinate list leaves (INT_MAX, INT_MAX, INT_MIN, INT_MIN) ± 10,000.

**Group patch of the bases** (`0x424f73..0x425006`), per group record G, for
member slot k = 0..19 with member word m = `G+0x32+2k` ≥ 0 and the group type
word `G+0x28` = 2: base record B = `+0x62 + 52m`: `B+0x1a` := `G+0x9a`,
`B+0x1c` := `G+0x6e`; `B+2` (its coordinate index) := `G+0x2c` if that is not
−1, else, if `G+0x30` ≠ −1, the first child of route `G+0x30`. The member
index is not range-checked against the base count (refused, §14.2).

**Zone** (`0x42519c(obj, header word 1, header word 0)`), §4.4. Then
`0x4290b3(grid)` (§4.4) and the **player route** (`0x4250a2..0x425173`): with
group 0's order word `G0+0x5a` ≠ −1 and that order's route word `O+6` ≠ −1
(asserted < route count), `+0x66` = a 6-byte copy of that route; else 0.
Then `0x425404` (§4.5). Header word 0 is passed but not used by `0x42519c`
(only `[ebp+0xc]` is read).

### 4.4 Zone and height grid (`0x42519c`, `hgrid.cpp`)

`0x42519c` asserts the zone word ≥ 0, builds `dat\zone<N>.dat`
(`sprintf("zone%d")`, prefix `dat\`, suffix `.dat`), opens it VOL-aware
(`0x403065`), reads two dwords (unused, as in DBSIM), `new(0x125)` grid
`0x428d5f(G, &those)`, the third dword → `G+0x104` (cell shift), the fourth
× word `[0x471d42]` (= 1) → `G+0x114`, `G+0x108` = word `[0x471d40]` = 15,
origin `G+0xf0..+0xf8` = 0, then `0x429664(G, "dba\zone<N>.dba", 0)` and
`[0x472370]` := G (the grid is global, not in the map object; nothing frees
it in this range).

VSHELL grid layout (`0x428d5f`, `0x4293af`): cells `[G+0xec]`, **28 bytes
each** (DBSIM: 16), height byte `+0`; origin `+0xf0..+0xf8`; log2 columns
`+0xfc`, log2 rows `+0x100`, cell shift `+0x104` (default 10), `+0x108`
(default 8), minimum `+0x10c`, maximum `+0x110`, scale `+0x114`, material
count `+0x119`, material table `+0x11d`. Every offset from `+0xfc` on is 4
below DBSIM's (terrain.md, "The grid object").

`0x429664(G, name, b)` reads `data\mat0.dat` (count, 8-byte records) like
DBSIM's `terrain_materials`; a name whose first dot starts `.dba` (always,
here) takes the bitmap path: DBA element 0 → `0x4293af(G, B, b)`.
`0x4293af`: columns c = smallest k with 2^k ≥ width word `[B+6]`, rows r
likewise from `[B+4]`; `2^c × 2^r` cells; bitmap row y fills grid row
`2^r − 1 − y`, cells x = 0 .. 2^c − 1 read pixel `[B+0xe] + (y << c) + x`;
**height byte = pixel − b** (low byte; b = 0 here). The rest (min/max, the
material byte `+0x19` with draws from VSHELL's RNG state `0x48110a`, the
height cache) does not reach the map image, which reads only the height byte
(§7). `0x4290b3` runs `0x429181` and `0x429233` over every cell (per-cell
vertex data for the 3D renderer; they write cell fields `+2..+0x16` and never
the height byte); the map does not draw through them (§8.2).

**RNG:** the material choice draws on `0x48110a` at `0x42959c` (threshold
`(draw & 0xfff) < 30 × 41 = 0x4ce`, `[0x472368]` = 30, one block anchor per
2^(8 − level) cells, level = `mat0` record 0 `+4`), so building a map
advances that state by a data-dependent count. The state is not saved and no
other code reads it (rng.md); the drawn map does not depend on it. The own
code reproduces the draws with VSHELL's generator (the raw draw of `0x465ce8`
on an `es2_shell_rng` initialised from the table `0x47f850`) and the state
after a build equals VSHELL's on every prepared mission. The text path of the
same choice is `0x429b10`.

### 4.5 `data\player.mec`, `data\mforms.dat`: the squad (`0x425404`)

In this order:

1. `data\player.mec` (VOL-aware `0x425442`): word 0 skipped, word 1 → `+0x80`
   = N (the record count, mission.md).
2. f = group 0's order (`G0+0x5a`) word `O+2`, asserted ≠ −1.
3. `data\mforms.dat` (VOL-aware `0x425516`): count word, f asserted < count;
   records of 28 bytes read f + 1 times into one 7-element local array, so
   the local holds record f: **seven (x, y) word pairs = the formation
   offsets of member slots 1..7** (dat.md's "14 words" are 7 offset pairs).
4. Heading a: if `G0+0x2e` ≥ 0, a = angle word `[+0x56 + 2·G0+0x2e]`
   **as stored** (no ×182, unlike DBSIM's load; mission.md); else, with a
   route of ≥ 2 children, a = `0x450c80(p1.x − p0.x, p1.y − p0.y) + 0xc000`
   (p0, p1 = the first two route points), else 0. `0x450c80` is an
   arc-tangent through the table `0x47aaf4` (the 64-bit dividend gets
   `d sar 1` without a carry into its high half); `0x451b90(a, &sc)` yields
   sine and cosine from the table `0x47b480`. Both tables are read from the
   hash-checked EXE at run time by the host, never embedded.
5. Anchor A, first that applies: coordinate `G0+0x2c`; the player route's
   first point; coordinate `O+4`; the first point of route `O+6`; else an
   assertion (line 0x230, `0x425791`) and A uninitialized (refused).
6. For member slot k = 0..19 with member word m ≠ −1 and m < the mech count
   `+0x7a`: position `+0x82 + 12k` = A; for k ≥ 1 plus `0x452624(&(dx, dy,
   0), &sc)` (rotation of the offset by a) with (dx, dy) = the words at
   EBP−0xe4 + 4k. For k = 1..7 that is form entry k − 1. **k ≥ 8 reads past
   the array**: slots 8 to 11 read the path buffer holding `data\mforms.dat`
   (at EBP−0xc4) as (x, y) words (reproduced; no CD mission uses them),
   slots 12 and up read uninitialised stack (refused: the member keeps the
   anchor position).

## 5. View parameters

**Overview** `0x4258a2`: dx = (max x − min x) sar 1, dy = (max y − min y)
sar 1 of the bounding box; centre `+0x187` = min x + dx, `+0x18b` = min y +
dy; altitude `+0x18f` = `+0x3bd` = **max**((dx << 7) / (294 sar 1),
(dy << 7) / (225 sar 1)) (signed `idiv`, truncation; shift = word `+0x10`).

**Squad view** `0x425935`: the box of the member positions `+0x82 + 12k` for
k < N with member word ≠ −1, widened by 250,000 (`[0x471d4c]`); centre and
altitude by the same formulas into `+0x193`, `+0x197`, `+0x19b`. With N = 0
the box stays inverted (INT_MAX/INT_MIN ± 250,000).

**Clamp** `0x427ee5(view, &p)`: p.z := min(p.z, `+0x3bd`); e_x = (147 · p.z)
sar 7, e_y = (113 · p.z) sar 7 (147, 113 = −`ctx+0x220`, −`ctx+0x224`; 7 =
camera `+0x1a`); p.x := max(p.x, min x − 100,000 + e_x), then p.x := min(p.x,
max x + 100,000 − e_x) (the second wins when they cross); p.y likewise with
min y / max y and e_y (100,000 = `[0x471d38]`).

## 6. Projection

`0x420646(view, &P, &out)`: `0x45e4d8(0)` (object transform = identity,
composed with the bound camera into the matrix `0x5d8040`), `0x45e610`
(transform P → camera space c), `0x45e690` (perspective), `0x45e764`
(screen):

- `0x45e690`: with the perspective shift `[0x5d80b4]` = camera `+0x1a` = 7:
  sx = (c.x << 7) / c.y, sy = (c.z << 7) / c.y (signed `idiv`, truncation
  toward zero, the shift in 32 bits; no guard for c.y ≤ 0, so a zero c.y is
  refused).
- `0x45e764`: X = sx + `[0x5d80c0]`, Y = `[0x5d80c4]` − sy, the centre set
  by the camera bind `0x45e37c` to the context rectangle's corner minus
  `ctx+0x220/+0x224`: **(291 + 147, 67 + 113) = (438, 180)**.

The camera is bound with the view position: `0x4206ba` (camera `+4` :=
(`+0x12` + `+0x3a`, `+0x16` + `+0x3e`, `+0x1a`), `0x45e37c`, `0x45e4d8(0)`).

With the angles (`0xc000`, 0, 0) the camera looks straight down with north
up; the camera matrix is exact:

    X = 438 + ((P.x − cam.x) << 7) / (cam.z − P.z)
    Y = 180 − ((P.y − cam.y) << 7) / (cam.z − P.z)

Supporting evidence: the up-arrow increases y (§10), the terrain image puts
the highest grid row at the top (§7), the clamp's e_x/e_y match a focal
length of 128 pixels. Icon and route points are projected **with their own
z** (the script coordinate's third dword); the terrain corners, the grid and
the camera ground point with z = 0.

## 7. The cached terrain image (`0x427634`, once per build)

G = `[0x472370]`, c = `G+0xfc`, r = `G+0x100`, s = `G+0x104`.

1. Free the previous pixel buffer `[0x48a39a]`.
2. Height-index map T (a `2^c × 2^c` byte bitmap; **both dimensions from
   c**): T[y][x] = `0xd1 + min(h, 127) / 5` for the cell `(y << c) + x`,
   h = its height byte (unsigned; 5 = 128 / 24 integer; result 0xd1..0xea).
3. Cell window: x0 = (min x − 100,000) sar s, y0 = (min y − 100,000) sar s,
   x1 = (max x + 100,000) sar s, y1 = (max y + 100,000) sar s (bounding box
   of §4.3, so ±110,000 around the coordinates); ncols = x1 − x0 + 1, nrows =
   y1 − y0 + 1; **k = min(640 / ncols, 400 / nrows)** (`[0x471d3c]`,
   `[0x471d3e]`; signed `idiv`).
4. Image: width ncols·k, height nrows·k, 8 bit, flag `0x10` (opaque),
   pixels zeroed (`0x46b9c4`), header at `0x48a390`, copied to the bitmap
   object `0x48a3cf` = `+0x3c1`. The context of this step copies the viewport
   of the image's `BMP_8` (`0x452da0` copies the current screen
   `[0x48e25c]`'s viewport): clip mode 1 against the image, offset 0.
5. The screen object `[0x48194c]` is replaced by a new `BMP_8` bound to the
   image (`0x427ae6`); a fresh context with paint type 0.
6. For grid row y = y1 down to **y0 + 1** (y0 itself is skipped), image row
   py0 = (y1 − y)·k, py1 = py0 + k; for x = x0 to **x1 − 1** (x1 skipped),
   px0 = (x − x0)·k, px1 = px0 + k:
   - colours: outside the grid (y < 0, y ≥ 2^r, x < 0, x ≥ 2^c) all four = 0;
     else t00 = T[y][x], t01 = T[y+1][x], t11 = T[y+1][x+1], t10 = T[y][x+1],
     with the guards "y < 2^c" (else t01 = t11 = t10 = t00) and "x < 2^c"
     (else t11 = t10 = t00). **The guards test y and x, not y + 1 and x + 1**,
     so the last grid row/column reads the next row/byte (quirk; see the
     refusal in §14.2).
   - triangle 1 `0x457de8`: (px0, py1) t00, (px0, py0) t01, (px1, py0) t11;
     triangle 2: (px0, py1) t00, (px1, py0) t11, (px1, py1) t10.
   So the right-most k columns and bottom k rows of the image are never
   drawn and stay index 0.
7. `[0x48194c]` := a new `BMP_8` bound to the frame `[0x481948]` with the
   size 640 × 400 (`[0x471f50]`, `[0x471f54]`, `0x427e75`), context restored,
   `+0x3c1` := `0x48a3cf`.

`0x457de8` fills a triangle with colour bands, not smooth shading (§13). After
the first map build the page present `0x406264` copies the frame rectangle
(0, 0)–(639, 399), which contains the map rectangle (vshell_presentation.md).

## 8. Paint (`0x425a5e`, vtable slot 0)

`0x420686` (save the current context, make the map context current); camera
placement; then the layers below in this order; `0x4206a4` (restore). The
paint draws into the frame only; it never presents (the callers do, §9,
§10).

**Camera placement.** State `+0x172` = 0x13 (final): q = view position +
pan, clamp q (§5), camera := q, `+0x12/+0x16` := q − pan (so the stored
position stays clamped). Any other state: clamp the view position in place,
camera := clamped + pan (`0x4206ba`), then restore the unclamped view
position.

Coordinates: the context's offset flag `ctx+0x20c` = 1 makes drawing
coordinates relative to the centre (438, 180); 0 = absolute frame pixels.
Everything is clipped to (291, 67)–(585, 292) inclusive (clip mode 1,
vshell_presentation.md). Paint records: `ctx+0x228` = lines and pixels,
`ctx+0x22c` = fills; type 0 = solid index, 4 = outline (`0x4576a4`,
`0x45886c`).

| # | Function | Offset flag | What |
|---|---|---|---|
| 1 | `0x425b73` | 1 | fill (−147, −113)–(147, 112) with **index 16**; if `+0x182`: busy-wait 10 ticks (`[0x471d44]`) |
| 2 | `0x425c0b` | 0 | terrain image (§8.2); if `+0x182`: wait 10 ticks (`[0x471d50]`) |
| 3 | `0x425f4a` | 1 | grid lines index 15 (§8.3); if not `+0x182`: bounding-box outline index 10 |
| 4 | `0x426d61` | 0 | player route, index 14 (§8.4) |
| 5 | `0x426fe1` | 0 | base/object icons (§8.5) |
| 6 | `0x427393` | 0 | waypoint markers (§8.6) |
| 7 | `0x4274b0` | 0 | squad icons (§8.7) |
| 8 | `0x427584` | — | no drawing (§8.8) |

A tick is `(GetTickCount() − [0x5ddcac]) >> 4` (`0x465d5c`, 16 ms). Every
wait updates `[0x471dc4]` (the step's tick).

### 8.2 Terrain (`0x425c0b`)

With `+0x3c1` ≠ 0 (always after the build): P1 = project(min x − 100,000,
max y + 100,000, 0), P2 = project(max x + 100,000, min y − 100,000, 0) (the
north-west and south-east corners of §7's window in world units); a rotation
by angle 0 (`0x451b90(0)`, `0x452680`) is exact and leaves P1;
`0x45399c(0x48a3cf, &P1, 0, &(P2 − P1), 0)`: the image drawn into the
quadrilateral at P1 of size P2 − P1 (textured quad, §13). The other branch
(live 3D terrain, `0x429bc2(grid, camera, &quad)` and `0x42a19e(grid,
camera)`) needs `+0x3c1` = 0 and is not reached (refused if it were).

The image covers the cells x0 .. x1 − 1 / y0 + 1 .. y1 scaled by k plus the
undrawn index-0 strip, while P1/P2 are the world corners of the whole window;
the original stretches exactly that, and the own code reproduces it. Index 0
is transparent in the quad, so the undrawn strip shows the background, index
16.

### 8.3 Grid lines and outline (`0x425f4a`)

O = projection of (0, 0, 0) without the screen step (`0x45e610`,
`0x45e690`, y negated): (ox, oy) relative to the centre; Q likewise for
(3,200,000, 3,200,000, 0) (`[0x471d54]` = 200,000 << 4). Spacing d =
|Q.x − O.x| (1/16 pixel per 200,000 units) → `+0x26`. Line paint {0, 15}.

- Vertical lines (centre-relative): x = ox + ((i·d) sar 4) for i = 0, 1, …
  while x < 147, each from (x, −113) to (x, 113); then x = ox − (d sar 4),
  and x = ox + ((i·d) sar 4) for i = −2, −3, … while x > −147.
- Horizontal lines: y = oy + ((i·d) sar 4) the same way, bounds 113 / −113,
  each from (−147, y) to (147, y).
- With `+0x182`: wait 1 tick (`[0x471d58]`) after each line.
- Without `+0x182`: the bounding box (§4.3) projected at z = 0, corners
  (min x, min y) and (max x, max y), y negated, as an outline rectangle
  {4, 10} (`0x4576a4`: four lines of index 10).

The negative branch's first step uses `ox − (d sar 4)`, the later ones
`(i·d) sar 4` with i < 0 (arithmetic shift rounds toward −∞); both are kept.

### 8.4 Player route (`0x426d61`)

Only with a route `+0x66` and `+0x184` ≥ 1. Line paint {0, 14}.

- `+0x184` = the route's child count: lines between consecutive route points
  p0–p1, p1–p2, … (all children; each child index asserted < coordinate
  count).
- Otherwise (during the fly-over): one line from route point `+0x184 − 1` to
  the **camera ground point** G = project(view x, view y, 0) (the view
  position `+0x12/+0x16`, without pan), then a filled ellipse {0, 14}
  (`0x45886c`) in the box G ± 3 (`[0x471d74]`; §13).

### 8.5 Base/object icons (`0x426fe1`)

For each base record B (52 bytes, `+0x62`, in file order) with type word t
= `B+0`: side = 0 if t < 24 or 45 ≤ t ≤ 54, else 1 (`0x42700d..0x42702f`).
Class from the jump table `0x42704a` (t = 4..26) and the range tests
`0x4270a6..0x4270de`:

| Class | Types t | Frame | Dot index | D | Half (1:1) | Dot below |
|---|---|---:|---:|---:|---:|---:|
| A | 4–6, 8–11, 13, 14, 16, 18–20 | 11 (`[0x471d78]`) | 10 | 50,000 | 10 | 5 |
| B | 26, 28–30, 32–35, 37, 39, 40 | 9 (`[0x471d76]`) | 33 | 50,000 | 10 | 5 |
| C | 45–54 | 24 (`[0x471d86]`) | 10 | 50,000 | 5 | 3 |
| D | 55–64 | 25 (`[0x471d84]`) | 33 | 50,000 | 5 | 3 |
| default, side 1 | 24, 25, 27, 31, 36, 38, 41–44, ≥ 65 | 8 (`[0x471d90]`) | 33 | 50,000 | 16 | 8 |
| default, side 0 | < 4, 7, 12, 15, 17, 21–23 | 10 (`[0x471d92]`) | 10 | 50,000 | 16 | 8 |

(All A types are side 0, all B types side 1.) A record with `B+2` = −1
asserts "Base %d has no loc." (`0x42723d`, severity 4) and is then drawn
with coordinate −1 (**H:** whether the assertion returns at severity 4 is
vshell_process.md's; the own code reads outside the coordinates there and
refuses). **Visibility:** side 0 is drawn when `B+0x1a` ≠ 0, side 1 only
when `B+0x1a` = 1 (`0x42727c..0x427294`); `B+0x1a` is the script value or
the group patch of §4.3. Position: coordinate `B+2` with its z. Mechs and
flyers have no icons.

**Icon primitive** `0x4272e0(view, &P, D, half, dot, index, bmp)`: (X, Y) =
project(P) (absolute); s = (D << 7) / view z (`+0x1a`, signed `idiv`):

- dot > s: one pixel of `index` at (X, Y) (`0x459cdc`, paint {0, index});
- else bitmap height (word `bmp+4`) > s: the bitmap scaled to s × s at
  (X − (s sar 1), Y − (s sar 1)) (`0x453908`);
- else the bitmap 1:1 at (X − (half sar 1), Y − (half sar 1)) (`0x4537e8`,
  mirror 0, index 0 transparent).

`half` is a constant per class, not the bitmap's size.

### 8.6 Waypoint markers (`0x427393`)

With a route and `+0x185` ≥ 1: n = min(22 − 14 + 1 = 9, child count − 1);
for i = 0 .. n − 1 while i < `+0x185`: point = route child i + 1 (child 0,
the start, gets none), (X, Y) = project, mis_icon frame **14 + i** drawn 1:1
at (X − 6, Y − 6) (13 sar 1 = 6, `[0x471da0]`).

### 8.7 Squad icons (`0x4274b0`)

With `+0x183` ≥ 1: for member slot k = 0..19 while the drawn count <
`+0x183`: member word m ≠ −1 → `0x4272e0(view, +0x82 + 12k, 100,000, 10, 5,
33, frame)` with frame **3 if m = 0, else 5** (`[0x471da4]`, `[0x471da2]`).
m = 0 is mech record 0 (**H:** the player's own Herc).

### 8.8 `0x427584`

Computes, for states 7, 13, 14 and 19, the line pointer `+0x17e` (the
current mission.str line if non-empty); no drawing, no other effect.
**H:** the remains of a removed caption.

## 9. The fly-over (`0x4262cf` and the loop `0x4019c6`)

**Trigger:** opening MISSION page 1 (`0x444873`) sets the palette request
`[0x46c26a]` = theatre − 1 + 5 (`dpl\br_w<theatre>.dpl`, vshell_presentation.md
indices 5..9; page 4 sets theatre − 1 + 10, `db_w`) and `[0x46c269]` = 1.
Main-loop step 6 (`0x4019c6`): apply the palette (`0x407b62`, `0x407c1b`),
flush the key ring, `[0x46c26c]` = 1, `[0x46c270]` = 0; then repeat: `bl =
0x4262cf(map)`, present `0x406264`, a key with code 1 or 0x39 (`0x404d0d`;
Esc, Space) or `[0x46c270]` ≠ 0 (a click) → `0x425a43`; pump `0x406328`;
until bl = 0; `[0x46c26c]` = 0. One step = at most one paint and exactly one
present.

**Skip** `0x425a43`: state := 0x11 if it is ≤ 0x11 (so it still plays the
final zoom-out, 60 ticks).

**Step** `0x4262cf`: T = current tick (`[0x471dc4]`); dispatch on state
`+0x172` (table `0x426331`, 20 entries; states > 0x13 do nothing); return 0
when `+0x17a` is set, else 1. "Interpolate over n" = position := start +
δ·(n − (`+0x176` − T)) while T < `+0x176`, with δ = (target − start) / n per
component (signed `idiv`); at the end position := target. "Paint" = slot 0.
L = mission.str line count `+0x217`, ℓ = `+0x17c`, j = `[0x48a3cc]` (next
route child), R = the route's child count. Waits use `[0x471db0]` = 0 and
`strlen · [0x471db4]` = 0 ticks (both constants 0).

| State | Address | Action |
|---|---|---|
| 0 | `0x426381` | first entry (`+0x176` = 0): `+0x176` = T + 60; position := overview (`+0x187`); paint with `+0x182` = 1 (the page-select toggle around it calls the empty driver slot 17, vshell_presentation.md); `+0x182` := 0; paint. Later entries: T < `+0x176` → paint; else state 1 |
| 1 | `0x42643c` | `+0x176` += 60; start = position, target = squad view (`+0x193`); δ over 60; state 2 |
| 2 | `0x4264d6` | interpolate over 60, paint; at the end state 4 (no icon increment) |
| 3 | `0x42658d` | paint until T ≥ `+0x176`; then `+0x183`++ (`0x4265a8`, one more squad icon) and state 4's code |
| 4 | `0x4265ae` | paint; if `+0x183` < N: `+0x176` = T + 15, state 3; else j = 1, ℓ = 0, child cursor = first child, next = 5, `+0x176` = T + 15, state 0xf |
| 5 | `0x42663b` | ℓ < L: (route and R ≤ j) → ℓ−−, flag `[0x48a3ce]` = 1, state 16's code; else wait 0, next 6, state 0xd, paint. ℓ ≥ L → state 6's code |
| 6 | `0x4266aa` | `+0x184`++, `+0x185`++, cursor += 1 child, j++; no route or R < j → state 0x11; else start = position, target = the child's coordinate with z := view z; n = (len(start − target) / 50,000 + 1) · 15 (`0x450f12`, below); δ over n; `+0x176` = T + n; state 7 |
| 7 | `0x42681c` | interpolate over n, paint; at the end state 8 |
| 8 | `0x4268cb` | ℓ < L and strlen·0 − n > 0 (never) → wait; else state 9's code |
| 9 | `0x42692e` | ℓ++; ℓ < L → wait 0, next 10, state 0xe; ℓ = L and route and R > j → ℓ−−, state 5; else `+0x176` = T + 15, next 10, state 0xf |
| 10 | `0x4269de` | ℓ < L → next 11, state 0xc; else state 11's code |
| 11 | `0x426a08` | ℓ < L: ℓ++; ℓ < L − 1 → wait 0, next 5, state 0xe; else state 5. ℓ ≥ L → state 5 |
| 12 | `0x426a7b` | ℓ < L → `+0x176` = T + 0, state 0xd; falls into 13 |
| 13 | `0x426ac3` | paint until T ≥ `+0x176`, then state := next |
| 14, 15 | `0x426af0` | same as 13 |
| 16 | `0x426b1e` | ℓ < L: flag set → wait 0, next 16, state 0xe, flag 0; else ℓ++, next 16, state 0xc, flag 1. ℓ ≥ L → state 5 |
| 17 | `0x426b9f` | `+0x182` = 0, `+0x183` = N, with a route `+0x184` = R, `+0x185` = R − 1; `+0x176` = T + 60; start = position, target = overview; δ over 60; state 18 |
| 18 | `0x426c75` | interpolate over 60; at the end state 19; paint |
| 19 | `0x426d20` | paint; unless done: `+0x17b`−−, at 0 `+0x17a` = 1 |

`0x450f12(a, b, c)`: with the absolute values sorted max ≥ mid ≥ min, len =
max + (mid sar 2) + (mid sar 4) + (mid sar 5) + (min sar 2).

**Visible sequence** (timings at 62.5 ticks/s): overview for 60 ticks (with
the outline), 60-tick flight to the squad, squad icons one by one every 15
ticks (the first 15 ticks after the flight), then per route point: its
numbered marker appears and the camera flies there at constant altitude over
n ticks while the leg's line follows the moving ground point (§8.4); after
the last point the 60-tick zoom-out to the overview with everything
revealed. The text states only add steps with identical frames (all their
waits are 0). The first paint's busy-waits (state 0, `+0x182` = 1) are not
presented in between (VSHELL presents after the step), so they only delay
the first frame (no present inside the paint is read from the listing;
**H** that nothing else shows the frame meanwhile).

**Later openings:** the state stays 0x13 and `+0x17a` = 1, so the loop runs
one step: one paint of the final view, one present. A new map object
(preparation or load) starts again at state 0.

## 10. Navigation arrows

The six ESButtonBitmaps of the MISSION screen (`mission.04` children,
auto-repeat, vshell_widgets.md) right of the map; handler: n = 1, 2, 3 or 4
steps for the button's repeat counter `+0x65` < 3, < 6, < 9, else; each step:
the slot, paint (slot 0), present `0x406264` (only while `[0x46f3a4]` ≠ 0).

| Button (screen rect, miss_arw frames) | Handler | Slot | Action |
|---|---|---|---|
| (590, 67)–(620, 97), 1/0 | `0x445577` | +0x0c `0x428093` | north: q = position + pan, q.y += step; clamp; if q.y unchanged by the clamp, pan y += step |
| (590, 105)–(620, 135), 3/2 | `0x445646` | +0x10 `0x4280e7` | south: same with −step |
| (590, 143)–(620, 173), 10/8 | `0x445715` | +0x14 `0x427feb` | west: x, −step |
| (590, 181)–(620, 211), 11/9 | `0x4457e4` | +0x18 `0x42803f` | east: x, +step |
| (590, 224)–(620, 254), 7/6 | `0x4458b3` | +0x04 `0x420738` | zoom in: if z − 50,000 ≥ 50,000: z −= 50,000, step by `0x4207e9` |
| (590, 262)–(620, 292), 5/4 | `0x445982` | +0x08 `0x427f9a` | zoom out: if z + 50,000 is not reduced by the clamp: z += 50,000, step by `0x42813b` |

Pan step `+0x42`: `0x4207e9` (base constructor and zoom in): ((z −
50,000) sar 3 · (100,000 − 5,000) sar 3 / ((1,000,000 − 50,000) sar 3)) << 3
+ 5,000 (= 20,000 at the initial z = 200,000); `0x42813b` (zoom out): ((z −
50,000) sar 8 · 95,000 / (`+0x3bd` − 50,000)) << 8 + 5,000. The fly-over
changes z without recomputing the step (quirk). Slot +0x1c `0x4207d9` (pan
:= 0) is not called by the arrow handlers; other virtual callers were not
searched.

## 11. The other MISSION images

From `0x4445c3` (called with the page by `0x444a1b`):

- `dba\terradef.dba` element 0 is the bitmap of **the Telecomm panel**
  `[0x48d8c4]`, screen (17, 63)–(256, 299), the child of `mission.00`
  whose click replays the briefing movie (`0x4454b8`). It is loaded on every
  MISSION opening and freed by the teardown (`0x4446d1`). vshell_process.md
  §11.4 names it the Telecomm panel; it is not the map.
- `dba\th_earth.dba` (theatre < 5) or `dba\th_moon.dba` (theatre 5) element
  0 is the single layer of the ESGrid `[0x48d8f4]`, screen (290, 67)–(584,
  292), one pixel left of the map rectangle; loaded only for page 0, shown
  at `0x444b4f` (page 0). Whether it is hidden when page 1 opens was not
  traced (its other show/hide sites `0x445114`, `0x445121` have no static
  caller): open.
- `dba\miss_arw.dba`: the arrow frames of §10 (and of the two text-scroll
  buttons, which do not touch the map).

## 12. Palette indices used

Under `dpl\br_w<theatre>.dpl` (page 1): 16 (background), 0xd1..0xea
(terrain heights, banded), 0 (undrawn strip of the image, transparent in the
quad), 15 (grid), 10 (bounding box; side-0 dots), 14 (route, ground point),
33 (side-1 and squad dots), plus the mis_icon bitmaps' own indices.

## 13. Raster primitives

The routines the map draws with, matched byte for byte in vitro (slot-mode
op `map-raster`, §14.1).

- **Polygon `0x455ad8`** (brush kind 0): the offset, then in clip mode 1
  `0x455f58` (Sutherland-Hodgman against x, then y; intersections with a
  64-bit product and truncating division, a boundary point computed from
  the vertex that is outside), then `0x456340` builds the span table and
  driver slot 0 (`0x45a674`) fills each row from word 0 to word 1 (nothing
  when right < left). `0x456340`: the top vertex is the least y, of equal
  ones the greatest x; the bottom vertex the greatest y, of equal ones the
  least x; chain 1 runs forward in index order from the top vertex to the
  first vertex on the bottom row and writes the right word, chain 2 forward
  from the bottom vertex to the first on the top row and writes the left
  word; per edge: vertical, horizontal (chain 1 keeps the greater x, chain
  2 the lesser), 45 degrees, y-major (Bresenham from the left end), x-major
  (chain 1 from its right end stepping left, chain 2 from its left end
  stepping right; each row gets the x where the run leaves it). One row
  stores the bottom vertex's x, then the top vertex's. **Refused:** a clip
  result of one vertex reaches `0x456340` with a zero loop count (the
  `loop` at `0x45637d` runs about 2^32 times).
- **Triangle `0x457de8`**: two equal vertices draw nothing; equal colour
  words fill the triangle in that colour; otherwise the triangle is cut
  into one band per colour level: the long edge max-to-min and the short
  edges max-to-mid and mid-to-min get one point per level (`(b - a) * i /
  n + a`, 32-bit product, truncating division), each band is a 4-vertex
  polygon of colour `max - i` (second part `mid - i`), its vertex order
  chosen by whether `(max index + 1) % 3` is the min index. So the terrain
  is banded (flat colour steps), not smoothly shaded.
- **Textured quad `0x45399c`** -> `0x4592a8` -> `0x455ad8` with a kind-5
  record -> driver slot 33 (`0x45ae5c`): the screen quad (x, y)..(x + sx,
  y + sy) (the rotation by angle 0 is exact), texture corners (0, 0)..(w -
  1, h - 1); two edge walkers from the top vertex (16.16 u and v, x by an
  integer step and a remainder), rows from the top until the clip bottom
  or until left > right, rows above the clip top skipped; per row the texel
  address steps by the integer parts and 16-bit fraction carries, starting
  half a step in and rounded by half a texel; columns are clipped to the
  context rectangle in clip mode 0 too; **index 0 is transparent**.
- **Line `0x455624`** (pen kind 0): clip mode 1 clips with `0x45588c`
  (top, left, bottom, right; the end point outside moves; 64-bit
  arithmetic), then driver slot 34 (`0x45a8e8`): Bresenham from the left
  end, the error starting at minus half the major span, both ends drawn.
- **Ellipse `0x45886c`** (brush kind 0; the route's 7 x 7 box): a midpoint
  ellipse whose span table covers rows -ry .. ry - 1: the lower half is
  written one row higher (row y lands on y - 1), so the ellipse is 2ry rows
  high; clipped per pixel in clip mode 1 (`0x457c8d`). A zero radius draws
  a line with an uninitialised pen kind (refused).
- **Rectangle `0x4576a4`**: kind 0 fills the inclusive rectangle (clip
  mode 1: the intersection `0x457914`, nothing when it is empty), kind 4
  draws four pen lines of the brush colour.
- **Pixel `0x459cdc`**, **bitmap `0x4537e8`** (the offset, then slot 26 or
  28, the blitter `0x45a09c` with the context rectangle), **scaled
  bitmap `0x453908`** -> `0x4591b8` -> slot 29 `0x45b4c0`: columns step by
  w / sx plus a remainder error term (rows likewise), clipped to the
  context rectangle (clip mode 1) or the screen, index 0 skipped unless the
  bitmap is opaque.

## 14. Own implementation

### 14.1 Code and verification

Code: `shell/src/gui/mission_map.c`,
`shell/include/es2/shell/gui/mission_map.h` (in `//shell:screens`); the
hosting is in `shell/src/gui/app.c` (§14.3). `//shell:mission_map_real_test`
runs VSHELL's own code in the mapped original (slot-mode ops `map-raster`,
`map-build`, `map-step`, `map-arrow`, `map-skip`, `map-paint`, `map-frame`;
tools/reference-game/shell-invitro/README.md) and compares byte for byte:
3,106 primitive cases (106 fixed, 3,000 seeded) and 5,291 probes (frame
SHA-256, state, tick, view, pan, pan step; per build the terrain image's
SHA-256 and size and the RNG state `0x48110a`) over 11 prepared missions
(training, demo, all five theatres). A run over all 58 missions es2prep
prepares (TRAIN1 and TRAIN3 do not prepare without a SAV) is equal in every
probe for 56; the two refused rows (below) are equal in every state probe.
`//shell:mission_map_test` covers synthetic state.

### 14.2 Refusals and deviations

A **refusal** is a case where the original reads or divides outside
anything defined; the own code sets `es2_mission_map.refused`, reads 0 and
the real test does not compare the frame:

- **Height table read past its end:** `0x427634`'s guards test y and x, not
  y + 1 / x + 1 (§7), so a window reaching the grid's last row reads the
  bytes after the table's 2^c × 2^c, whatever VSHELL's heap holds there.
  C2_08 and C3_09 (zones 22 and 39, 128 × 128 grids, cell shift 13) do; the
  real test compares their state probes but not their frames. Rows past a
  grid with fewer rows than columns and texels outside a bitmap are read as
  0 as well.
- **Formation slots ≥ 12** read uninitialised stack (§4.5); more than 20
  squad slots; a member past the mech list; a group member past the base
  count; a base without location (§8.5); a missing angle, route or
  coordinate index; a zero perspective divisor (§6); a missing icon frame;
  no terrain image (the 3D branch of §8.2); a clip result of one vertex and
  a zero ellipse radius (§13).
- **Fly-over timing:** the busy waits of the first paint (10 + 10 + one tick
  per grid line) are not performed inside the paint: their ticks are added
  to the tick and reported in `wait_ticks`. In vitro the tick grows by one
  per read, so a wait of n ticks takes n reads on both sides.

### 14.3 Interface and host

`es2_mission_map_build` (on preparation or load, `0x40e75f`'s callers);
`es2_mission_map_step` once per main-loop pass while it returns 1 (step 6,
`0x4019c6`; the caller presents after each step; Esc, Space or a click call
`es2_mission_map_skip`); `es2_mission_map_arrow_steps` and
`es2_mission_map_arrow_step` for the six arrows (one present per step);
`es2_mission_map_paint` for a later opening (state 0x13);
`es2_mission_map_wait` spends the first paint's `wait_ticks`;
`es2_mission_map_project` projects a world point. The host
(`map_build`, `map_animation`, `mission_map_arrow` in `shell/src/gui/app.c`)
presents each step as `0x406264` does after the build, the rectangle
(0, 0)–(639, 399) (the capture's `dst` (0, 0, 640, 400) on every fly-over
present), and keeps one `es2_mission_map_globals` per process (the
fly-over globals and the RNG state `0x48110a`), reads the files VOL-aware into
`es2_mission_map_inputs` (script.dat, player.mec, mission.str, mforms.dat,
mat0.dat, the zone's dat and dba, the executable's cosine and arc-tangent
tables) and decodes the icons. Outside headless mode it spends `wait_ticks`
in wall time before the step's present, with no present and no pump
meanwhile, as VSHELL waits inside its paint; the headless clock runs only in
the pump, so there the wait is only noted in the present record. Nothing
draws text.

## 15. Frame states for captures

Per mission (run, screen `mission`, state): the first frame of page 1 after
a preparation (overview with outline), a frame of the squad flight, the
squad icons appearing, a frame of a route leg (the line to the ground point
and the ellipse), the final view (state 0x13), after Esc during the
fly-over (the zoom-out), after each arrow at the overview and zoomed in, and
a second opening of page 1 (one paint of the final view). The map frames are
presented by `0x406264` (rect (0, 0)–(639, 399) after the build): one present
per fly-over step and per arrow step. Ticks of 16 ms drive the fly-over, so a
pixel comparison needs the frame journal per step, not wall-clock frames: the
journal must record each step's tick, since a step's frame depends on it.
`shell/tests/screen_mission_map.c` hashes the first frame, the squad and
route flights, the end of the fly-over, a skip and the arrows of a synthetic
mission on the scripted platform; a capture-tour reference does not exist
yet.

## 16. Open points

1. Whether `[0x48d8f4]` (theatre image) stays visible on page 1 and whether a
   later widget repaint of `mission.04` can overwrite the map without a map
   repaint (**H:** yes, nothing repaints the map except §9/§10).
2. The `0x34`-byte object `+0x4e` (**H:** a light) and the per-cell passes
   `0x429181`/`0x429233`: they matter only for the unreachable 3D terrain
   branch.
3. Whether `0x427e75`'s 640 × 400 rebinding also clips later VSHELL drawing
   through the blitter bounds (`0x45335c`): vshell_presentation.md's area.
4. Whether any CD file has an STR group with more strings than the map
   object's slots (§3).
5. Other virtual callers of slot +0x1c `0x4207d9` (§10).
6. Whether the assertion "Base %d has no loc." returns at severity 4 (§8.5).
