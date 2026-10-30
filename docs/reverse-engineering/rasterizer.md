# Software rasterizer and graphics library

The software polygon fills (`0x468414`, `0x468760`), the renderer line of
`DBSIM.EXE` (draw order, modules, frame writes), the view transform, the
graphics library (GL) primitives, fills, objects and surfaces, the DTS shape
and actor draw slots, text and the cursor, and the own code that implements
them (`sim/src`, `sim/dispatch/services*`). Addresses refer to the reference
`VER95/DBSIM.EXE` (SHA-256
`8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad`,
[reference_version.md](../reference_version.md)) at image base
`0x400000`; they are virtual addresses, not file offsets. Every contract was
read from the `llvm-objdump -d -M intel` listing; vtable slots and Borland
RTTI names (dword at `vtable − 12` → type descriptor, name offset at `+6`,
as in [hud.md](hud.md)) from the image bytes.

Status words:

- **Confirmed** means read from the instructions. For own code it also
  means equal to the original in vitro (the named differential test, on
  synthetic inputs). The own code in this document is **confirmed in vitro;
  not checked live** unless a live result is named.
- **Hypothesis** marks role names and field meanings that the bytes do not
  prove. Role names in the tables are hypotheses unless stated otherwise.

Live call counts come from the entry-probe counts of the
[DBSIM map](../reference-game/dbsim_map.md) (the six-suite `map.json` of `dbsim-map-20261008`/`-20261010`, which es2-data no longer holds; the current map has 40 suites; in the order acceptance-60s / c1_01-success / c1_01-collision / c3_09-loadout-b /
c4_08-razor-plasma / c3_10-loadout-hercs, saturated at 1000).

The rendering code writes only rasterizer-private state, the frame and
depth buffers and draw scratch; nothing the simulation reads later. It is
presentation (H3), not a simulation hook (H4).

## The polygon cluster

`0x468414` and `0x468760` are the software polygon fill routines: they clip
a polygon of screen-space vertices to the viewport, build edge tables, and
fill scanlines with flat, shaded or textured spans into the current drawing
surface (optionally with a depth buffer). Every caller is a draw method or
the terrain draw pass, and every write lands in rasterizer-private globals,
caller draw scratch, or the frame/depth buffer. No hook was added.

### The cluster

Vertices are 28 bytes (seven dwords) `{x, y, z +8, u +0xc, v +0x10,
w +0x14, shade +0x18}`. A flag word selects which attributes the clippers
interpolate: 8 = z, 1 = u and v, 2 = w (the perspective subdivision, only
together with 1), 4 = shade. The full layouts are under
[Software polygon fills](#software-polygon-fills).

| VA | Instr. | Role (from the bytes) | Calls | Writes |
|---|---:|---|---|---|
| `0x468414` | 252 | 2D polygon fill, cdecl six slots: count, vertices, mode, colour/texture word (→ `0x4a099c`), shade-record pointer, flags (→ `0x4a0994`). Flag word = 1, plus 4 for mode 2, plus 8 while the depth buffer is on (`[0x49f248]`). Clips in x and then y, builds the edge record, walks the rows: span width as reciprocal from table `0x4d3cbc` (width < 640) or `0x47e068`, destination row from the surface base `[[0x4a3652]]` and pitch `[0x4a3676]`, then a span filler | `0x4699c8`, `0x469c64`, `0x477d48`; Q16 helpers `0x47e068`/`0x47e08d`; span routines `0x48b854` (depth path, buffer `[0x6c5f90]`), `0x48b63a`, `0x48b5af`, `0x48b524`; inner loops `0x46aeb1`, `0x46ad4c`, `0x46ac14` | `0x4a0968`–`0x4a09b0`, `0x4a5938`–`0x4a5ae8`; in mode 1 the caller's shade record `[p] ← ([0x4d3cb4] − 1)·[p] + [0x4a08b0]` (palette remap) |
| `0x468760` | 691 | 3D variant: camera transform `0x48c57c`, near-plane clip at `[0x6c602c]`, projection `0x48c5fc`/`0x48c6d0`, then the same 2D clip, edge and span steps | as `0x468414`, plus `0x471cd8`/`0x471cf0`/`0x471d26`/`0x471d61` (64-bit add, subtract and divide helpers) | same globals as `0x468414` |
| `0x46817c` | 230 | 3D front end of `0x468414` (transform, near-plane clip, projection into the scratch vertices `0x4d46e4`), then `0x468414` | `0x48c57c`, `0x48c5fc`, `0x48c6d0`, Q16 helpers, `0x468414` | `0x4d46e4`…`0x4d4d74` scratch |
| `0x4699c8` | 137 | clip a polygon at the viewport's left and right edge (graphics context `[0x6c5f70]+0x210`, `+0x218` + 1); output into the caller's array (`0x4d4914`), returns the new count | `0x469894` | output array only |
| `0x469c64` | 137 | the same at the top and bottom edge (`+0x214`, `+0x21c` + 1); output `0x4d4b44` | `0x469b2c` | output array only |
| `0x469894` / `0x469b2c` | 118 each | intersection vertex of one edge with `x = c` / `y = c`; interpolates the other coordinate and the flagged attributes with `0x47e068` (Q16 ratio) and `0x47e08d` (Q16 product); returns 0 if either end point lies on the line, else 1 | Q16 helpers | output vertex only |
| `0x477d48` | 587 | edge-table setup for the clipped polygon; returns a record used as {first row, row count, left edges, right edges} (roles from the use in `0x468414`) | Q16 helpers, `0x471cd8`, `0x49667c` (RTL) | `0x6c5390`, `0x6c5394`, `0x6c565c`–`0x6c56a4` |
| `0x46ad4c` | 116 | frameless hand-written span loop: affine texture span through the 256 × 256 texel page `0x4d4f04` and the shade table `[0x4a0998]` (row = shade `>> 16 << 8`), writes bytes/words to `[0x4a0968]`…`+[0x4a096c]`; saves ESP in `0x4a09cc` and uses ESP as the end pointer | none | the destination row, `0x4a09a0`, `0x4a09cc` |

`0x468760` spans 691 instructions in the listing; the map counts 693.

`0x4690f0` (a third fill variant, the only other caller of `0x4699c8`,
`0x469c64`, `0x477d48` at `0x4693c9`/`0x4693f0`/`0x469412`) and `0x46a10c`
(a rotated quad, the caller at `0x46a3ae`) have no direct caller and no
pointer anywhere in the image: dead code.

### Callers and roots

| Caller | Call sites | What it is | Root |
|---|---|---|---|
| `0x46d448` | `0x46dc6f`, `0x46dd90` (`0x468760`); `0x46dc9f`, `0x46ddb9` (`0x46817c`) | terrain cell draw, reached through the per-cell callbacks `0x46dddc`/`0x46dfa8` of the terrain walk ([terrain.md](terrain.md)) | rendering `0x45fca8` and `0x43fe44` (terrain.md) |
| `0x474fa8` | `0x4754f5` (`0x468760`, depth buffer `[0x49f24c]` on), `0x475528` (`0x46817c`) | textured-face draw, slot `+0x1c` of `TSTexture4Poly` (vtable `0x4a5f0c`) and of `TexPoly` (vtable `0x49afe6`); [dts.md](../formats/dts.md) | shape rendering |
| `0x42fc34` | `0x42fd88` (`0x46817c`) | slot `+0x2c` of `TexPoly` (vtable `0x49afe6`, table entry `0x49b012`); fills the vertex scratch `0x4cfd10`… and draws. The `TexPoly` vtable is never installed (see [Modules and boundaries](#modules-and-boundaries)), so this caller is dead | none |
| `0x40bc14` | `0x40bf44` (`0x468414`) | slot `+0` of the `BEAM` table `0x4987c4` (the draw slot; see the `0x40bb4c` note in [weapons.md](weapons.md)); the logic frame calls only slots `+0x14`/`+0x18` of object tables ([main_loop.md](main_loop.md), stages 9 and 10) | object draw (not the logic frame) |

A caller walk over direct calls and immediate references (function starts
from call targets and `push ebp; mov ebp, esp` prologues) reaches the
rendering root `0x45fca8` and the draw slots above; it finds no path from
the logic frame `0x45f570`, the presentation root `0x45fba4` or the camera
`0x4011a0`. The probe counts (1000 in all six suites for every function of
the cluster) fit a per-frame draw pass.

### What the cluster writes and who reads it

| Memory | Written by | Read outside the cluster by |
|---|---|---|
| `0x4a0968`–`0x4a09cc` (span state) | `0x468414`, `0x468760`, the span routines | only the span routines `0x48b524`, `0x48b5af`, `0x48b63a` and the inner loops `0x46ac14`–`0x46b0a0`; `0x4a0994` is also set by `TexPoly` slot `0x42fa18`, `0x4a0998` by `0x46a464` |
| `0x4a5938`–`0x4a5ae8` (depth span state) | the same and `0x48b854` | only `0x48b854` and the fill routines |
| `0x6c5390`–`0x6c56a4` (edge tables) | `0x477d48` | nobody (the record is returned by pointer); the neighbouring default record `0x6c56a8` read by simulation code is not touched |
| `0x4d4914`, `0x4d4b44`, `0x4d46e4`…`0x4d4d74` (vertex scratch) | the clippers, `0x46817c`, `0x468760` | only the fill routines |
| caller's shade record (mode 1) | `0x468414`, `0x468760` | the caller: draw-local memory (`TSTexture4Poly` passes `EBP−0x40` with mode 0; `BEAM` and `TexPoly` pass 0 with mode 0; the terrain draw passes `0x6b4f6c` with mode `[0x4aaaac]`, read only by `0x46d448`) |
| surface rows `[[0x4a3652]]`, depth buffer `[0x6c5f90]` | the span routines | the blitters / page flip |

No function of the cluster calls the RNG, reads the clock or the frame
counter, touches an actor, pool, timer or the tick log's compared state, or
calls a sound function. Reads are the graphics context `[0x6c5f70]`, the
surface pointer and pitch, the depth-buffer switches `[0x49f248]`/`[0x49f24c]`,
the palette-remap pair `0x4d3cb4`/`0x4a08b0`, the reciprocal table `0x4d3cbc`,
the texel page `0x4d4f04` and the shade-table pointer `0x4a0998`; the near
plane `[0x6c602c]` for the 3D variants.

Unlike the terrain draw helpers ([terrain.md](terrain.md)), which only
compute geometry for the draw pass, this cluster writes pixels itself, and
`0x46ad4c` and the `0x48b5xx` span routines are frameless code that hijacks
ESP, so it is replaced as one unit by the own fills. The pure clip helpers
`0x4699c8`/`0x469c64`/`0x469894`/`0x469b2c` would be bit-exact hook
candidates, but the live oracle cannot see them and they have no
simulation caller.

### Neighbours outside the cluster

**`0x477bec` (110 instr., "vtable/callback")** is not called from the
rasterizer. It is the keyboard message handler of the window procedure
([input_journal.md](../reference-game/input_journal.md)): stdcall, four
slots (window, message, wParam, lParam; `ret 0x10`), registered in the
WndProc handler list `0x4d3b30` by `0x477a4c` (through `0x465fec`) and
removed by `0x477a68` (through `0x466010`). For `WM_KEYDOWN`/`WM_KEYUP`/
`WM_SYSKEYDOWN`/`WM_SYSKEYUP` it maps the virtual key through `0x4a10ec`
(index below `0xdf`), adds Shift `0x800`, Alt `0x200`, Ctrl `0x400` via
`GetKeyState` (thunk `0x49695e`, IAT `0x6cc438`), sends the codes `0x20f`,
`0x401`, `0x201` (Alt+Tab, Ctrl+Esc, Alt+Esc) to `0x4669b4`, adds `0x80`
for key-up, offers the code to ten registered hot-key entries at `0x6bb8b8`
(12 bytes: active flag, zero-terminated code list, callback called with the
code; result 1 = consumed), otherwise appends it to the 256-word key ring
`0x6bb6b0` (write index `0x6bb8b0`, full when it would reach the read index
`0x6bb8b4`) and sets bit 1 of `[[0x6c5eb8]]`. It returns 1 (message
handled) for the key-down code `0x44` (F10) and for unmapped keys while Alt
is down. It runs inside the logic frame through the message pump
(`0x45f570` → `0x45a810` → `0x464c78` → WndProc `0x466034`) and fills the
input that `0x460870` consumes, so it is simulation input, not rendering;
it belongs to the input/platform layer (Win32 message, `GetKeyState`,
indirect hot-key callbacks).

**`0x46a89c` (65 instr.)** is object simulation, not rasterizer. Its only
callers `0x46a958`, `0x46a9e8`, `0x46aa60` hang under `0x46a810`, called
from `0x46a6d4`, slot `+0x18` of the table `0x497818` (logic-frame stage 10
in [main_loop.md](main_loop.md)). cdecl (obj, s, t): if
`timer_word_step(obj+0x222)` (`0x467a48`) returns nonzero, t ← −200 when
byte `obj+0x227` is set, else 200; s clamped to [−256, 256];
`approach_word(obj+0x220, t, 30)` (`0x467adc`); word `obj+0x10` +=
`multiply_q8(200, s)` (`0x47e0a0`); word `obj+0x32` ← 0. All three callees
are hooks, so it is a ready hook candidate for the `0x497818` update.

### The affine texture span loop `0x46ac14`

One of the inner span loops of the cluster (107 instructions, no callees;
entry probes 0/1000/1000/1000/1000/1000). It writes pixels and its own
scratch word, nothing else, and is not hooked. Why the acceptance-60s
suite never enters it was not investigated.

Contract (from the bytes):

- Entry `0x46ac14` (the byte before it, `0x46ac13`, is padding). No frame,
  no arguments, no return value; saves and restores EBP, ESI, EDI, EBX,
  `ret` without operand. It calls nothing, touches no stack memory besides
  the four saved registers, and uses no x87.
- Reads (all span state): `[0x4a0968]` destination pointer, `[0x4a096c]`
  span length in bytes, the u/v accumulators `0x4a0978`/`0x4a0980` and
  steps `0x4a0988`/`0x4a0990` (Q16, read as the unaligned dwords
  `0x4a0976`, `0x4a097e`, `0x4a0986`, `0x4a098e` so that the 16 fraction
  bits sit in the top half of EAX, EDX, ESI, EBP, plus the bytes
  `0x4a097a`, `0x4a0982`, `0x4a098a`, `0x4a0992` for the integer parts in
  CL, CH, DL, DH), the texture word `[0x4a099c]` (shifted left 16, the page
  index above the 256 × 256 texel offset `v·256 + u` in CX), the low byte
  of the flag word `[0x4a0994]`, and the texel bytes `0x4d4f04 + ECX`. One
  step is `add`/`adc` per axis: the fraction carry goes into the integer
  byte, which wraps mod 256. The low halves of the step dwords (bytes
  `0x4a0986..87`, `0x4a098e..8f`, i.e. the high halves of
  `0x4a0984`/`0x4a098c`) are added into AX/DX too; the image has no
  absolute write to `0x4a0984` or `0x4a098c` and their initial bytes are 0,
  so they are zero in practice.
- Writes: `[0x4a09a0] ← [0x4a0968] + [0x4a096c] − 2` (the start of the last
  pixel pair), then the destination bytes `[0x4a0968] .. +len − 1`: one
  leading byte when the destination is odd, words for the pairs, one
  trailing byte if one is left (the trailing byte reuses the texel of the
  current u/v without another step). The u/v accumulators are **not**
  written back; the callers advance them.
- Flag byte `[0x4a0994]` = 0: every texel is stored (opaque). ≠ 0: texel
  value 0 is transparent, the destination byte is kept (in the pair loop a
  zero low texel is replaced by the current destination byte before the
  word store, a zero high texel stores only the low byte).
- `0x4a09a0` is read only by the inner loops `0x46ac14`–`0x46b16b`, and
  each of them writes it before it reads it (absolute-reference scan), so
  it carries nothing between calls.

Callers (three direct call sites, no absolute reference):

| Site | In | Path |
|---|---|---|
| `0x468725` | `0x468414` (2D fill) | mode other than 1 and 2, no run list |
| `0x46982e` | `0x468760` (3D fill) | the same selection (the map lists only `0x468414` and `0x48b524` as callers) |
| `0x48b59a` | `0x48b524` | run-clipped variant, selected at `0x468704`/`0x46980d` in the same two fill routines |

The run clipper `0x48b524` (`enter 4,0`, no arguments, called only from
`0x468704` and `0x46980d`, no absolute reference) walks the sorted list of
8-byte {start, length} runs at `[0x4a593c]` (count `[0x4a5938]`; the fill
routines load both per row from the run table `[0x4a5af8]` when the
graphics context `[0x6c5f70]` selects it, e.g. `0x4684ff`…`0x46855f`; span
start x in `[0x4a5940]`). For each run that overlaps the span it moves the
start: `[0x4a0968] += d`, `[0x4a0978] += d·[0x4a0988]`,
`[0x4a0980] += d·[0x4a0990]` (32-bit `imul`, low half), sets `[0x4a096c]`
to the overlap length and calls `0x46ac14`; a run that reaches the span end
stops the walk. It writes only that span state. Who fills the per-row run
lists `[[0x4a5af8]]` was not traced.

The destination is the drawing surface: the fill routines build
`[0x4a0968]` from `[[0x4a3652]]` (surface base) plus row × `[0x4a3676]`
(pitch) and x (`0x4684bf`…`0x46866c`, `0x469456`…`0x46969e`). No RNG,
clock, actor, pool, timer, mission or tick-log field is read or written,
and both callers are reached only from the draw passes.

## The renderer line (R3D)

The 3D renderer under the render root `0x45fca8` (own code), read per
module as (a) its place in the frame, the guest state it reads and every
coupling call (clock wrapper `0x467828`, RNG `0x492ee0`/`0x492f24` with
stream A `0x4d2599` or B `0x4d260b`, the sound API, the heap
`0x47749c`/`0x4774f0`/`0x477548`/`0x47758c`), and (b) its frame writes, the
input of the frame hash ([presentation.md](presentation.md)). The HUD line
(panel, HUD draw, ports, widgets, palette cycler) is in
[hud.md](hud.md#the-hud-line-contract--coupling-set-and-frame-writes);
the module details are in
[terrain.md](terrain.md#the-terrain-pass-in-the-frame),
[horizon.md](horizon.md#place-in-the-frame)
and [camera.md](camera.md#render-camera).

Every statement in this section is static unless a map count is quoted;
sizes are estimates of a walk. The linear listing is misaligned in the
graphics library (hand-written code with embedded data), so function
bodies are walked from their entry over an entry-aligned disassembly
(`--start-address` per entry, fall-through and direct jumps, stopping at
`ret`, at indirect jumps and at other entries). Coupling sites come from a
byte scan of the whole CODE section for `E8`/`E9` rel32 transfers to the
targets, the reach of the line from a direct-call walk from the root's
guest callees plus the 41 pointer-referenced draw entries named below
(vtable slots and code pointers, found with a dword scan of the file).
Function boundaries, areas, counts, roles and verdicts come from the
generated index [functions/](../functions/README.md).

### Couplings and headless reach

1. **The renderer line has no clock read, no RNG draw and no sound call.**
   The byte scan finds in the R3D area 2 clock sites (`0x471e1f`,
   `0x471ea9`: the animation start/step `0x471e10`/`0x471e88`, called only
   from the HUD line), 4 RNG sites (`0x42f874`, `0x42f9cc` in the `TexPoly`
   class, which is never constructed: its constructor `0x42f700` has no
   caller and no reference, so its vtable `0x49afe6` is never installed;
   `0x46c69d`, `0x46cac0` in the load-time hooks
   `terrain_cells`/`terrain_materials`) and no sound call. The direct reach
   of the line (311 entries, 256 live original ones with 19,342
   instructions, estimate) contains none of the coupling targets. The
   frame's virtual-clock reads and stream-B draws are the HUD line's and
   the presentation's.
2. **Heap per frame: only the light objects.** Per drawn object the light
   application `0x407098` takes a light of kind 1 (`0x407388`, 52 bytes) or
   kind 2 (`0x407444`, 64 bytes) from a free list of the light table
   `L = [0x4a9608]` (`L+0x35c`/`+0x360`, `L+0x3b0`/`+0x3b4`) and calls `new`
   (`0x4073bc`, `0x407478`) only when that list is empty; released lights
   go back to the list (`0x407500`), none is freed in the frame. The other
   heap sites in these modules are load time (the stage-2 callback
   `0x428230` with three `new[]`, the texture builds `0x46a03c`/`0x469e14`,
   the light set-up `0x46134c`), on a never-entered path (the
   compressed-bitmap path `0x481910` of the bitmap blits
   `0x4816a8`/`0x48185c`, map 0) or a `0x49667c` array construction on a
   caller-supplied (non-null) array, which does not allocate
   ([horizon.md](horizon.md)). Under the own heap the address sequence does
   not matter ([own_process.md](../reference-game/own_process.md) §3); an
   own renderer that keeps the free lists allocates at the same moments.
3. **The frame order is the render root's**
   ([main_loop.md](main_loop.md#render_root-0x45fca8-cdecl-no-argument)
   table): camera setup `0x48c2e4` (step 5), terrain visibility `0x46cb9c`
   (6), `panel_frame` (7, HUD), object binning `0x42841c` (8), the 3D-view
   test `0x42db18` (9, HUD), then only when it says "visible": camera setup
   again (10), `vector_copy` into the light table (11, hook), terrain
   visibility again (12), view transform `0x48c444(0)` (13), sky, horizon
   and terrain cells with their objects `0x42e700` (14), far and eye-cell
   objects `0x42883c` (15); then the HUD line (16–21) and the presentation
   frame. The 3D view is therefore drawn **after** the panel and **before**
   the HUD draw, and the binning runs every frame even when the layout
   hides the 3D view.
4. **The renderer writes into compared memory only through idempotent
   paths** (keep list below): the lazy object matrix `0x401fe4`, the shape
   detail pointer that `0x4033e4` writes and restores, the per-frame cell
   marks (cleared by the binning itself every frame), the actor cell words
   `+0x1e8`/`+0x1ea` (written by the binning for buildings, MECHs and
   FLYERs, read only by the renderer) and the light source's object pointer
   `+0x1f`. None of them changes a value that own simulation code reads
   (the lazy matrix holds what the simulation would compute from the same
   angles, terrain.md keep list item 3) or that the oracle compares.
5. **The camera is not renderer-only.** The camera object `[0x4d24ea]` is
   read by five simulation hooks (`projectile_flight` `0x40a7b3`: camera
   distance < 40,000 and a per-object latch gate `sfx_play(0x32)`, i.e. a
   stream-B draw and an object write; `ground_effects`, `flyer_move`,
   `meteor_update`, `fire_update`), the view subject `[0x4d2684]` by six
   (`weapon_prepare`, `cockpit_sync`, `flyer_hit_test`, `cockpit_valid`,
   `cockpit_arc`, `command_key`) and the view word `0x4d24f0` by
   `player_input`, `leg_input`, `control_record`, `command_key`. The
   outside-camera and view-subject code (`0x401c74`, `0x45de30`,
   `0x45df34`, `0x45edb0`, `0x40109c`, `0x40111c`) runs in the **logic
   frame** and must run exactly in a headless process; their H3 verdicts
   hold for the drawing split only ([camera.md](camera.md)).
6. **The five largest functions of the line:** `0x486ee8` 919, `0x468760`
   693, `0x46d448` 684, `0x477d48` 587, `0x48817c` 556 instructions (map
   counts; the listing span of `0x468760` is 691). Four belong to the 3D
   line; **`0x48817c` does not**: its callers are the HUD radar and map
   pages (`0x43eef4`, `0x43f2d8`, `0x44f1bc`), the never-live MFD camera
   view `0x43fe44` and the `GLRect` chain `0x482d77`; it is a GL primitive
   of the HUD line. `0x486ee8` is reached from the 3D line only through the
   GL polygon fill's Gouraud path (`0x483eb8` → `0x487acc`), and from the
   cockpit set-up `0x44f6f4`.
7. **Headless** ([own_process.md](../reference-game/own_process.md) §13
   question 4): every renderer entry under the render root may be headless
   **except** the camera chain of item 5 (logic frame) and `0x48c444`'s
   callers outside the frame (positional sound, audio.md "What can change the oracle's comparison" item 3); the
   keep list under [May be headless](#may-be-headless) names the writes a
   headless process may drop and why.

### The frame, step by step

The render root's step numbers; B = the view block `0x4d24bc`, cam =
`[B+2Eh]` = `[0x4d24ea]`, G = the terrain grid `[0x4a0be0]`
(= `[0x49aac8]`), ctx = the current graphics context `[0x6c5f70]`.
"Writes" are the frame's writes outside the callee's own stack.

| Step | Entry (module) | Inputs read (guest state) | Writes | Detail |
|---:|---|---|---|---|
| 2, 3 | `0x45d490`, `0x45d50c` detail levels (only when the option bytes `0x4d1f41`/`0x4d1f42` differ from their words; map 2 per suite, load included) | the option byte | `0x497368`, `0x49736c`, `0x4a9868`; `0x4a95b4`, `0x4a9dc4` (read by the draw slots) | functions index |
| 4 | `0x42d870` jolt point (HUD line) | `0x4cfa70`, `0x49add5` | `0x4cfcbc`/`0x4cfcc0`; the root adds it to ctx origin `+220h`/`+224h` | display_modes.md |
| 5, 10 | `0x48c2e4(cam)` camera setup | cam `+0x16` (context), `+0x1a`, `+0x1e`, `+0x22`, pose; context clip `+0x210..+0x21c`, origin `+0x220`/`+0x224` | `0x6c5f94` (camera pose: constructed once, guard `0x4a5b0c`, refilled every call), `0x6c6028`..`0x6c6048`, `0x6c5fd4..`, `0x6c5ff4`, `0x6c601c` | camera.md |
| 6, 12 | `0x46cb9c(G, cam, 0)` terrain visibility | G, cam `+4..+0xc`, detail byte `0x4d1f3f`, table `0x4a0bb4` | eye `0x6b4f44..0x6b4f4c`, G `+0x10c`, `+0xc8`/`+0xcc`, `+0x11c`, the view-space cell basis G `+0x4..+0x27`, draw distances `0x4cf988[5]`, the view transform (`0x48c444(0)`), the height cache G `+0xd0` | terrain.md |
| 8 | `0x42841c` object binning | the pools (flat objects, buildings, MECH, FLYER, projectiles, explosions, debris, smoke balls, fires, meteors), cam position, the eye, G | the cell marks `[G+0xf0]` (cleared, then counted), actor `+0x1e8`/`+0x1ea`, the bucket table `0x4cf4f0`/`0x4cf874`, the buckets `0x4aab00`, the far list `0x4cf88c`/`0x4cf92c` | terrain.md |
| 9 | `0x42db18` 3D view in this layout (HUD line) | `[0x4cf99c]` | — | display_modes.md |
| 11 | `vector_copy(L, cam+4)` (hook) | cam position | `L+0..+0xb` | [main_loop.md](main_loop.md#render_root-0x45fca8-cdecl-no-argument) |
| 13 | `0x48c444(0)` | `0x6c5ff4`, the active light list `0x6c60a8`/`0x6c60ac` when `[0x6cbc04]` | `0x6c5fb4..0x6c6024`, `0x6c5ff8`; the lights' object-space copies | camera.md |
| 14 | `0x42e700(cam)` sky, horizon, terrain cells and their objects | G, cam, `hzline` `[0x49aee0]`, the jolt offset `0x4cf9a0`, the buckets | the pixels of the 3D view (below), the cell marks of drawn cells (cleared), the draw scratch, the lazy object matrices, the lights | horizon.md, terrain.md |
| 15 | `0x42883c(cam, B+9)` far and eye-cell objects | the far list, the player's cell words and chassis | the pixels of those objects, `0x49ac3c` | terrain.md |

### Modules and boundaries

Sizes: live (map ≠ 0) original non-platform functions and their
instructions in the module's ranges and named entries (estimate, before
the own code below replaced them); hooks are own code. The 3D line and
the HUD line share the GL, the view transform and the lights.

| Module | Entries and ranges | Live / instr. | Hooks in or under it | Coupling | Frame writes |
|---|---|---:|---|---|---|
| GL and scan converter ([Graphics library primitives](#graphics-library-primitives)) | `0x480738..0x4928d0` without the view and light functions; scan converter `0x4930a0..0x495132` (`0x4930c1`, `0x493141`, `0x493192`, the span walk `0x49495f`) | 164 / 10,859 | `bitmap_new`, `bitmap_read`, `bitmap_array_new`, `bitmap_array_read`, `tex_edge`, `shape_instance`, `shape_dtor`, `shape_base_dtor`, `pose_interpolate`, `set_length`, `dist_gate` | none per frame | DIB pixels through the driver slots `[0x4a57ac..]` (below) |
| Polygon pipeline ([Software polygon fills](#software-polygon-fills)) | `0x4680e0..0x46a000`, the span loops `0x46ac14..0x46b188`, the edge tables `0x477d48`, the span clippers `0x48b524`/`0x48b5af`/`0x48b63a`/`0x48b854` (counted under the GL by address) | 15 / 2,753 | none | none per frame (`0x469e14`'s `new` is load time) | DIB rows `[[0x4a3652]]` + pitch `[0x4a3676]`, depth buffer `[0x6c5f90]` |
| View, projection, lights ([View transform, projection and lights](#view-transform-projection-and-lights)) | `0x48c2e4`, `0x48c3b0`, `0x48c444`, `0x48c57c`, `0x48c5cc`, `0x48c5fc`, `0x48c6d0`, `0x48d7ec`, `0x48db12` and their leaves; lights `0x407098`, `0x407388`, `0x407444`, `0x407500`, `0x407878`, the active list `0x48be50`/`0x48be98`, polygon lighting `0x48bfe8`/`0x48c16c`, constructors `0x48bc40`/`0x48bc70`/`0x48bd84`/`0x48bda4` | 46 / 1,485 | none (`vector_copy` in the root) | heap: the two light `new`s | none (globals `0x6c5f94..0x6c6048`, `0x6c60a8..`) |
| DTS draw slots ([DTS shape draw](#dts-shape-draw), [Actor draw slots](#actor-draw-slots-and-explosion-binning)) | `0x474b30..0x476d00` (part and polygon slots), shape draw `0x490ab8`/`0x490c1c`, instance slot `0x478da4`, actor draw slots `0x4033e4`, `0x4034f4`, `0x4174c8`, `0x4215cc`, `0x408e6c`, `0x409cd0`, `0x46b8ea`, `0x40316c`, `0x40ded8`, tracer `0x40bc14`/`0x40bb4c`, lazy matrix `0x401fe4`, detail levels `0x45d490`/`0x45d50c`, polygon transform `0x48c72c`/`0x48c954`/`0x48ca70`/`0x48cf20`/`0x48d5c0`/`0x48d624`/`0x48d38c`/`0x4912a0`, shade ranges `0x42e8e8`/`0x42e878` | 56 / 3,446 | `track_advance` | none | through the GL and the polygon pipeline |
| Terrain renderer ([terrain.md](terrain.md)) | `0x46cb9c`, `0x428140`, `0x46d1a8`, the walkers `0x471f30..0x472b14`, the callbacks `0x46dddc`/`0x46dfa8`, `0x46d448` and callees, the geometry `0x4949a4..0x495132`, the height cache | 23 / 3,385 (`0x46be9c..0x46f000`, `0x470a1c..0x470d94`, `0x471e10..0x472e6c`) | `view_rect`, `cell_uv`, `poly_clip`, `poly_spans`, `terrain_height`, `terrain_face`, `segment` (and the load-time `grid_ctor`, `terrain_cells`, `terrain_materials`, `normalize`, `cell_mark_set`) | none per frame | through the polygon pipeline |
| Scene: sky, horizon, binning, sort tree ([horizon.md](horizon.md), [terrain.md](terrain.md)) | `0x428000..0x429660`, `0x42e700..0x430390` | 35 / 2,007 | `sweep_terrain`, `terrain_line` (simulation hooks in the range) | none | through the GL (sky, haze) |
| Camera and view ([camera.md](camera.md)) | `0x401078..0x401d64`, `0x45de30`, `0x45df34`, `0x45edb0`, `0x45e208` | 10 / 647 | `camera_update` | none; **simulation input** (item 5) | none |

Boundary notes. The range `0x471e10..0x472e6c` holds the HUD-line
animation `0x471e10`/`0x471e88` (two clock sites; HUD line) and the terrain
walkers with the hooks `poly_clip`/`poly_spans` (terrain renderer). The
`TexPoly` methods `0x42f6d2..0x430390` in the scene range are dead
(item 1). `0x48817c` and the text, glyph and bitmap blits (`0x482534`,
`0x482634`, `0x4816a8`, `0x488974`, `0x487bdc`) are drawn only by the HUD
line; the GL code still provides them for it. The shade-range writers
`0x430518`/`0x430590` lie in the PRES range of the module map but are
3D-line code (callees of the shade ranges `0x42e8e8`, see
[View-space projection and shade ranges](#view-space-projection-and-shade-ranges)).

### The polygon pipeline: four fill families

| Family | Entry | Used by | Steps | Pixels through |
|---|---|---|---|---|
| Software rasterizer, 3D | `0x468760` (with the perspective helpers), `0x46817c` (affine, via `0x468414`) | the terrain cell draw `0x46d448` (`0x468760` when 2·`[0x49aad0]` > depth, else `0x46817c`), `TSTexture4Poly` `0x474fa8` | camera transform `0x48c57c`, near-plane clip `[0x6c602c]`, projection `0x48c5fc`/`0x48c6d0`, the 2D clippers `0x4699c8`/`0x469c64`, the edge tables `0x477d48`, the span loops | `[[0x4a3652]]` rows, `[0x4a3676]` pitch, depth `[0x6c5f90]` when `[0x49f248]` |
| Software rasterizer, 2D | `0x468414` | the tracer `0x40bc14`, texture builds (load) | as above without the 3D front end | same |
| GL polygon fill | `0x484222` (reverses the vertex order) → `0x483eb8`, edges `0x484720` (with the unrolled block whose exit label is `0x4866f0`)/`0x484338`, Gouraud `0x487acc` → `0x486ee8`, spans `0x483a04` | the DTS solid, shaded and Gouraud polygons through `0x48d624` (`0x48dbac`/`0x48dbc1`), the sky bands and the ground haze `0x42ee88`/`0x42f0b0` | the context's fill record `[ctx+0x228]` and fill slots `[0x4a5830]`, `[0x4a5834]`, the clip helpers `0x486c00`/`0x486d15` | the driver slots of `0x4a57ac` (copy of `0x4a586c`, presentation.md) |
| GL rectangle and bitmap | `0x486704` (rectangle), `0x48185c` → `0x481c58` → `0x488b98` (bitmap), `0x4895ec` (line) | the sky with roll 0 (`0x486704`), the DTS bitmap parts `0x4763f4` (`0x48185c`), the HUD line | blit slots `[0x4a57ac]`, `[0x4a57c0]`/`[0x4a57c4]` | the same driver slots |

**What the hooks cover.** `poly_clip` `0x472bcc` and `poly_spans`
`0x472b14` compute the walkers' cell geometry: the view polygon clipped at
an axis line and its span list per grid line, the latter through the guest
scan converter `0x4930c1` (`0x493192`, code pointer `0x49495f`); no pixel.
`tex_edge` `0x48aaac` is the texture edge set-up of the GL textured span
slot `0x48a924` (table entry `0x4a58f0`). `shape_instance` `0x490b64`
builds an object's shape instance when the object is created; the
per-frame draw is the instance's slot `+0x10` `0x478da4` → `0x490c1c`
([DTS shape draw](#dts-shape-draw)). `view_rect`, `cell_uv`,
`cell_flag_get`/`_set`, `terrain_face` and the math hooks
(`transform_point`, `rotate_point`, `matrix_compose`, `euler_matrix`,
`dist3d_delta`, `heading_cos`, …) are called by the renderer as guest
calls; the renderer's share of their hook counters is not compared state.

### Frame writes

The frame hash covers the DIB at `[0x4d3028]` (W × H window at the page
origin S) and the colour table at every present. What the 3D line
contributes:

| Module | Writes | Rectangle |
|---|---|---|
| sky and horizon `0x42ee88`, `0x42f0b0` | sky bands above the horizon line and one haze fill down to the projected terrain edge (or the clip bottom for roll 0) | ctx clip `[ctx+0x210..+0x21c]`; the fill colour pointer `[ctx+0x22c]` is set and restored |
| terrain cells `0x46d448` | one quad or two triangles per visible cell, back-faced cells skipped; textured with the haze remap `[0x4a08b0]` | the same clip (the 2D clippers read it) |
| cell objects (sort tree, DTS) | the objects of each drawn cell, sorted per cell by `0x429620`, after that cell's terrain | the same clip; per polygon its own clip in the render context `0x6c60d4` |
| far and eye-cell objects `0x42883c` | objects outside the grid, then the player's own cell for a flyer chassis | the same clip |
| none | the colour table: the 3D line writes no palette entry (the haze is an index remap) | — |

The jolt reaches the 3D view only through the context origin the root
adds before step 5 (the projection offset `0x6c6034`/`0x6c6038` = clip
left/top − origin, [camera.md](camera.md)). The page origin S is the
presentation's and the HUD line's (display_modes.md). None of these
static frame-write claims was compared against a captured frame.

### May be headless

The renderer side of [own_process.md](../reference-game/own_process.md)
§13 question 4:

- **May be headless (no compared effect, static):** steps 2–3 when their
  writes are kept (the detail words are read only by draw slots), 5, 6, 8,
  10, 12–15 and everything below them: no clock, RNG, sound or simulation
  write that a simulation reader sees (couplings items 1, 4).
- **Keep list when skipping them:** (1) the lazy matrix of `0x401fe4`
  moves to the simulation's own rebuild (call counters differ, state does
  not; terrain.md keep list item 3); (2) actor `+0x1e8`/`+0x1ea`, the cell
  marks, the buckets, the light table's `+0x1f` pointers and free lists
  are read only by the renderer; (3) `0x48c444` must still be callable
  where audio calls it (`0x46299c`), with the camera pose `0x6c5f94` that
  step 5 builds: positional volume and pan depend on it, stream B does not
  (audio.md "What can change the oracle's comparison"); (4) the heap counter `0x6bb30c` will differ by the light
  objects; nothing branches on it (own_process.md §3).
- **Must run:** the camera chain of couplings item 5 (logic frame,
  [camera.md](camera.md)) and `camera_update` (hook).

### Guest state the renderer reads

View block B `+9`, `+0x12`, `+0x2a`, `+0x2e`; camera `+4..+0x4d`
(camera.md); graphics context `+0x4`, `+0x8`, `+0x208`, `+0x210..+0x21c`,
`+0x220`/`+0x224`, `+0x228`, `+0x22c`; `[0x6c5f70]`, `[0x4a3614]`,
`[0x4a3652]`, `[0x4a3676]`, `[0x4a03d0]`, the driver table
`0x4a57ac..0x4a583c`; render context `0x6c5f94..0x6c6064`,
`0x6c60a8..0x6c60d0`, `0x6c60d4..`; grid G `+0x4..+0x27`, `+0xc8`,
`+0xcc`, `+0xd0`, `+0xec`, `+0xf0`, `+0x100..+0x11c`, `+0x121`, `+0x125`;
`0x4d1f3f`, `0x4d1f40`, `0x4aaaa8`, `0x4aaaac`, `0x49aad0`, `0x4a0bb4`,
`0x49abb0`, `0x4cf988`; light table L (`[0x4a9608]`); `hzline`
`[0x49aee0]`; the pools named in step 8; the detail words of steps 2–3;
depth switches `0x49f248`/`0x49f24c`; haze `0x4a08ac`/`0x4a08b0`;
`0x4d3cb4`, `0x4d3cbc`, the texel page `0x4d4f04`, `[0x4a0998]`.

### Renderer-line quirks

1. The object pick is dead: the request `0x4282a0` (point → `0x4cf931`,
   byte `0x4cf930` ← 1) has no caller and no reference, so the pixel probes
   through the driver slots `[0x4a57bc]`/`[0x4a57c0]` in `0x42876c` and
   `0x42883c` never run and the HUD draw's pick read (`0x43d7bb` →
   `0x4282c8`/`0x4282c0`) always sees "none". The second pick
   (`0x428960`, rectangle test, byte `0x49abbc`) has no caller either.
2. The scene-light re-add `0x407878` in `SortObj` `0x42876c` runs only when
   word `0x4cf984` (= byte `0x4d24c5`, B+9) is set; the map has it at 0 in
   all suites.
3. The binning skips buildings, MECHs and FLYERs whose group dword
   `[[obj+0x45]+0x14]` is nonzero, and bins the flat-object pool
   `[0x4a968d]` only within 30,000 of the camera (`dist_gate`, `0x428451`);
   no other pool has a distance test there (the per-kind draw distances
   come later, `0x428c08`).
4. `TexPoly` (`0x42f6d2..0x430390`) is complete code with two stream-B
   draws but is never constructed.

## View transform, projection and lights

The R3D view set-up, the view and object selection, the projection leaves,
the light objects, the active light list, the polygon and terrain lighting
and the light table: the module that writes no pixels. Own code:
`sim/src/view.c` (`sim/include/es2/sim/view.h`), service rows
`sim/dispatch/services/view_transform.h`. Confirmed in vitro; not checked live. The
module is headless-exact by construction.

### View and light data

| Address | Meaning |
|---|---|
| `0x6c5f94` | the camera matrix (0x20 bytes: nine words, kind byte at +0x12, translation dwords at +0x14) |
| `0x6c5fb4` | the view matrix; `0x6c5fd4` its inverse (what the projection uses); `0x6c5ffc` the identity |
| `0x6c5ff4`, `0x6c5ff8` | the current view and object matrix pointers (NULL selects the identity) |
| `0x6c601c` | the eye (3 dwords); `0x6c6028` the focal shift; `0x6c602c` the view flag; `0x6c6030` the focal divisor |
| `0x6c6034`, `0x6c6038` | the screen centre; `0x6c603c` the viewport (4 dwords) |
| `0x6c60a8`, `0x6c60ac` | the active light count and list (at most 10 pointers) |
| `0x6cbc04` | the lights-on byte (every light transform is skipped when 0) |
| `0x4a5b0c` | "camera matrix built" byte (the first frame constructs it with the hook `matrix_ctor 0x495132`) |
| `0x4a9608` | the light table pointer; `0x497ae4`/`0x4a960c` the scene light count (word) and pointers |
| `0x6cbee4` | the constructor/destructor counter every light object ctor/dtor adjusts |

A light object (`0x34` bytes for a spot light, vtable `0x49f8c0`, kind 1;
`0x40` for a point light, vtable `0x497ac8`, kind 2; `0x49f8d4` the base,
kind 0): `+0` vtable, `+4` position (3 dwords), `+0x10` direction (dword
+ word), `+0x16` kind, `+0x1a` level, `+0x1e` a second GL light
(`0x48bc40`'s shape: vtable `0x4a5b39`, the transformed position at
`+0x22`, direction `+0x2e`/`+0x32`); point lights add `+0x34` inner range,
`+0x38` outer range, `+0x3c` = level x outer range.

A light table (`0x4a9608`): `+0x10`/`+0x14` default ranges, `+0x18` the
active entry count, `+0x1c` the active entry pointers, `+0x328` the
table's own light, `+0x35c`/`+0x360` the spot pool (count, pointers),
`+0x3b0`/`+0x3b4` the point pool, `+0x6c` twenty entries of 0x23 bytes:
`+0` free byte, `+1` position (3 dwords), `+0xd` level word, `+0xf` inner
and `+0x13` outer range, `+0x17` cut-off distance, `+0x1b` level dword,
`+0x1f` the entry's light.

### View and light functions

All cdecl; EAX as named, EDX/ECX only where named.

| VA | Name (row) | Behaviour |
|---|---|---|
| `0x48c2e4` | `view_setup(cam)` | per frame: the camera matrix from the camera's position (+4) and angles (+0x10) (hooks `matrix_ctor` once, `vector_subtract`, `euler_matrix`); the view = the camera matrix's inverse (`0x48db12`); the object pointer's product (`matrix_compose`); the inverse and the eye (`transform_point` of the origin); focal shift and divisor from `cam+0x1a`/`+0x22` (shift 0 takes the divisor); the viewport from the context (`[cam+0x16]+0x210..`), the centre = viewport origin minus the context's `+0x220`/`+0x224`; the view and object pointers reset to the identity. EAX = the eye call's (incidental; no caller reads it) |
| `0x48c3b0` | `view_select(M)` | the view matrix M (NULL: the identity) into `0x6c5fb4`, its inverse and the eye |
| `0x48c444` | `object_select(M)` | the object matrix M (NULL: the identity) composed with the view; with lights on, every active light's position through the inverse into `L+0x22` (`0x48bcfc`). EAX the last light's `L+0x1e`, else a stack address (incidental, not compared; no caller reads it) |
| `0x48c57c` | `view_transform(p, v)` | p through the view matrix (hook `transform_point`) |
| `0x48c5fc` | `project_point(v, s)` | (x, depth, z) onto the screen: shift 0 divides by the focal divisor, else shifts; a zero depth is a refusal (the original faults) |
| `0x48c5cc` | `project_size(size, depth)` | the projected size; EAX it, EDX as the original leaves it |
| `0x48c6d0` | `to_screen(s, t)` | t = (s.x + centre.x, centre.y - s.y); EAX t, EDX s, ECX t.y |
| `0x48d7ec` | `view_load(P)` | a saved view: the pointers from P+0x20/+0x24, the view matrix from P, its inverse, the eye from P+0x28; with lights on the saved copies P+0x34+0x16 i **all land in the first active light** (the original never advances its list pointer, `0x48d884..0x48d8aa`); EAX that light's +0x22 |
| `0x48db12` | `matrix_invert(M)` | in place: transpose (hook), the translation negated and rotated (`rotate_point`) |
| `0x48bfe8`, `0x48c16c` | `polygon_lighting(n, p)`, `terrain_lighting(n, p)` | the light sum over the active list (ambient 16 + the directional and point terms, clamped to 0..0xff); the two division sites are refusals on a zero divisor |
| `0x48be98` | `light_list_remove(L)` | the last entry moves into L's slot; found: AL 1 (EAX the slot address with its low byte 1), EDX the moved entry, ECX the new count; else EAX 0 |
| `0x48bc70`, `0x48bcc4` | light ctor/dtor | counter +1/-1; dtor flags bit 0 deletes (EAX after the delete is the run-time's, not compared) |
| `0x48bd6c` | `point_light_level(L, v)` | `[L+0x1a] = v`, `[L+0x3c] = [L+0x38] * v` |
| `0x406ee4`, `0x406f38`, `0x406fbc`, `0x407018`, `0x407048`, `0x40735c` | the light table's entry life cycle | the ranges `a >> 5`/`b >> 5`; the first free entry activated (a full table: refusal, the original writes through a stale pointer); freed with its light released; the position; the level (word + sign-extended dword) and the cut-off `(level * (outer << 5)) / 10 + (inner << 5)` (EAX it, EDX `inner << 5`, ECX the entry) |
| `0x407098` | `light_update(T, obj)` | per object: the table's own light leaves the list; per active entry with a level: the distance `0x47d734`; beyond the cut-off the light is released; else a spot light when `vector_angle` (hook) of the horizontal offset and the heading is below 0x1f40 (level `min((outer << 8) / (s + inner), 0xff)`, direction words `(offset << 11) / max(d, 1)`), else a point light with the entry's ranges; new lights from the pools or `new` + `0x48bc40` twice |
| `0x407388`, `0x407444`, `0x407500`, `0x4075c1`, `0x40765c`, `0x407878` | spot/point light new, release, point dtor, light-object dtor, scene lights into the list | as the listing; `0x40765c`'s EAX is the counter word's address (not compared) |
| 27 leaves | accessors, vector and matrix leaves, forwarding rows (`0x48db57` to `matrix_compose`, `0x48dc43` to `matrix_transpose`, `0x48d9e4`/`0x48db93` to `transform_point`/`matrix_point`, `0x48da08` to `0x4800c0`) | one-liners; EDX/ECX where the in-vitro compare confirmed them (`0x48d9a7`, `0x48bd6c`, `0x48be98`, `0x40735c`, `0x406ee4`) |

Not rows (core hooks, called through the dispatch): `light_create
0x407604`, `light_level 0x4076a0`, `vector_copy 0x40707c`. Without a
static reference in the image (no row, `table_test` refuses them):
`0x406f00`, `0x4076c4`, `0x48d94c`. Dead by the call graph: `0x48bed8`,
`0x48c598`, `0x48c658`, `0x48c6f4`, `0x407284`, `0x407820`, `0x4078a0`,
`0x48bde4`, `0x47d76c`, `0x407548`.

`matrix_apply 0x4800c0` (`sim/src/load.c`) traps: the original re-reads
the input vector for every output word, so an in-place call (out == v)
feeds the new x into y and z; and the copy path (kind < 1, `movsd` +
`movsw`) returns EAX = out + 6.

Refusal sites (they model the original's faults; whether a mission reaches
one is a replay question): `0x48c5e2`, `0x48c5f6`, `0x48c616`,
`0x48c625`, `0x48c63b`, `0x48c64c`, `0x48c10a`, `0x48c133`, `0x48c281`,
`0x48c2aa`, `0x40712f`, `0x40717a`, `0x407210`, `0x406f69`.

Evidence: listing ranges `0x48bc40..0x48c6f3`, `0x48d7ec..0x48dc5e`,
`0x406e44..0x407930`, `0x495132..0x49522f`, `0x492a30..0x492ad4`,
`0x47dce7..0x47dd3e`, `0x47d734..0x47d76b`, `0x4800c0..0x4801d3`. In vitro
`//tools/reference-game/invitro:differential_view_test` (`view_cases.h`,
ops `view`, `table`, `leaves`): the original's machine code against the
own rows through a dispatch of the sim's real services plus the ten hooks
as passed rows; random worlds (camera, context, matrices, 0..5 lights of
the three kinds, the light table), compared after each sequence: EAX of
every call (EDX/ECX where named), the world buffer, the DATA segment and
the heap. Unit `//sim:view_test` (synthetic image, stub hooks): the
projection, the view set-up (hook order), the object selection, the
lighting, the list and the saved view, the light table; refusals at
`0x48c5e2`, `0x48c133`, `0x406f69`. `//sim:table_test` holds the entry
evidence for every row.

## Graphics library primitives

The GL clip helpers, rectangle, span, point, bitmap, save/scroll/restore,
ellipse, text and the scan converter, slot 29 and the stretched and
rotated blits. Own code: `sim/src/gfx.c` (`sim/include/es2/sim/gfx.h`),
service rows `sim/dispatch/services/gfx_primitives.h`, unit test `//sim:gfx_test`,
in vitro `//tools/reference-game/invitro:differential_gfx_test`
(`gfx_cases.h`). Confirmed in vitro on synthetic surfaces; not checked
live. The jump tables `0x489ac3` and `0x489d85` and their first entries
(which the linear listing garbles) were read as bytes with REA on a
scratch copy of the reference file.

### Fill slots and the driver table

The driver table `0x4a57ac..0x4a583c` is a copy of the BMP_8 table
`0x4a586c` (36 dwords) made by the surface attach (`frame_buffer.c`);
`[0x4a5830]`/`[0x4a5834]` are slots 33 and 34 of that copy, read only (no
draw mode reselects them). The primitives pick their writer by the
pen/fill record's *kind* (solid, table, ramp), not by a mode switch.

Driver slots `[0x4a57ac + 4i]` (BMP_8): 0 `0x489f84` span fill solid,
1 `0x48a016` span fill table, 2 `0x48a6c4` rect fill solid, 3 `0x48a74c`
rect fill table, 4 `0x48a340` get pixel, 5 `0x48a361` put pixel,
6 `0x48a384` pixel through a table, 19 `0x48a08a` span scroll,
20 `0x48a7a7` rect scroll, 26 `0x4899ac` bitmap draw, 27 `0x489c6e`
bitmap grab, 28 `0x489ede` region draw, 29 `0x48afc8` stretched bitmap
(below). The own code calls a slot through the table
(`es2_sim_gfx_slot`), with own bodies for 0–6, 19, 20, 26–29 and the
dispatch for the rest.

### GL guest state

All static DATA, read through the guest view.

- Graphics context `C = [0x4a3614]`: `+0` region count, `+4+4i` region
  entry pointers (entry: dword kind, kind 0 = rect `{l,t,r,b}` at `e+4`,
  kind 2 = span-list pointer at `e+4`), `+0x204` clip mode (0 none, 1 the
  rect, 2 the region list; every mode other than 0 and 1 takes the region
  path), `+0x208` origin flag, `+0x20c..+0x218` clip rect `l,t,r,b`,
  `+0x21c/+0x220` origin `x,y`, `+0x224` pen record pointer, `+0x228` fill
  record pointer. Pen record: kind 0 = colour word at `+4`, kind 2 = table
  pointer at `+4`. Fill record: kind 0 solid colour word at `+4`, kind 4
  ramp (`+4` = colour word for the ramp line pen), kind 6 table pointer at
  `+4`.
- Origin shift: a rect, point or span list is moved by
  `(clip.l − origin.x, clip.t − origin.y)` when `[C+0x208]` is nonzero
  (`0x486704`, `0x486a34`, `0x4895ec`, `0x487bdc`, `0x487d0d`, `0x487eac`;
  `0x4887d8` and `0x488974` test `== 1`).
- Surface view `0x4a3652..`: `[0x4a3652]` row table `T` (`T[0]` = bits,
  `T[y+1]` = byte offset of row `y`; pixel `(x,y)` = `T[0] + T[y+1] + x`),
  `[0x4a3656]` width, `[0x4a365a]` height, `[0x4a3662]` format (8),
  `[0x4a3676]` pitch, `[0x4a367a]` height. The writers step rows by the
  pitch after the first row (`0x48a004`); the unclipped bitmap blit clips
  against `[0x4a3676]`/`[0x4a367a]` (`0x489a3e`, `0x489a5a`).
- Span buffers A `[0x4a3691]` = `0x4a3699` and B `[0x4a3695]` = `0x4a431d`,
  0xc84 bytes each (800 rows). Span list: word y0, word count, then
  `count` pairs (word left, word right), inclusive; an empty row is
  `(1, 0)`.
- Colour word resolution (`0x486704` `0x486745..`, `0x486a34`,
  `0x4895ec`): high byte 0 → the colour as is; 4 → ramp entry
  `e = 0x4a4fa5 + 8·(int16)c`,
  `i = ((((c >> 16) & 0xff) · (int16)[e+4]) >> 7) & 0xfffe`, colour =
  `(int16)[[e] + i]`; any other high byte → the primitive returns without
  drawing.
- Bitmap header `H`: word `+0` height, `+2` width, byte `+4` bpp, `+5`
  flags (0x10 opaque, low nibble 1 = RLE), dword `+6` size, `+0xa` pixels.
  GLBitmap object `B`: header at `B+4`, word `B+0x12` nonzero = compressed.
- Font `F` (from `0x482498`/`0x482534`): word `+8` first character, `+0xa`
  height, `+0xc` fixed width or −1, byte `+0x12` bpp, word `+0x16` baseline
  offset, dword `+0x22` pixel base, `+0x26` glyph offset table (dword per
  glyph), `+0x2a` width table (byte per glyph). GC object `[0x6c5f70]`
  (`0x490d34` returns it): byte `+0x230` underline, dword `+0x231`
  alignment (1 right, 2 centre), dword `+0x235` field width. `0x48d99e`
  returns `[0x6c5ed0]` (the current surface, device kind 7).

### Clip helpers

- `0x48699c(a, b, out)` rect ∩ rect: `l = max`, `r = min`; `l > r` → 0
  with nothing written; else `out.l`, `out.r` are written, then `t = max`,
  `b = min`; `t > b` → 0 (**quirk**: `out.l/out.r` already written); else
  `out.t`, `out.b`, returns `out`.
- `0x4869fc(rect, out)` rect → span list: word `t`, word `b−t+1`, then that
  many pairs `(l, r)`; returns `out`. A row count of 0 writes the header
  only; a negative count is an endless `rep stosd` (refused); more than
  800 rows → refused (the buffers hold 800).
- `0x486c00(a, b, out)` span ∩ span (`0x486d15(rect, spans, out)` rect ∩
  span the same way with the rect's rows and `l/r`): rows outside the
  overlap of the two row ranges → 0; the output starts at the later y; a
  row with `la ≤ rb` and `ra ≥ lb` writes `(max(la,lb), min(ra,rb))`; a
  row without overlap after the first overlapping row writes `(1, 0)`;
  rows before the first overlap are dropped (y0 moved down); no overlap at
  all → 0; count = last overlapping row − first + 1; returns `out`.

### The primitives

- Rect fill `0x486704(rect)`: fill kind 0 → slot 2 with the resolved
  colour; kind 6 → slot 3 with the table pointer; kind 4 → the ramp
  outline `0x48690a` (a temporary kind-0 pen `{0, [fill+4]}` at `C+0x224`,
  four `0x483a04` line calls `(r,t)-(l,t)`, `(l,b)-(l,t)`, `(l,b)-(r,b)`,
  `(r,t)-(r,b)` on the **unshifted** rect, pen restored); other kinds
  return. Mode 0: writer(local rect); mode 1: writer(`0x48699c`
  (local, clip)) when nonzero; other modes: entries from the last to the
  first, kind 0 → `0x48699c(local, e+4)` → writer; kind 2 → the first time
  the local rect is clipped in place against the clip rect (0 → return)
  and converted to spans in B (`0x4869fc`), then `0x486c00(B, [e+4], A)` →
  slot 0 (`[0x4a57ac]`) with the colour argument (**quirk**: for kind 6
  that argument is the table pointer, whose low byte becomes the colour).
- Span fill `0x486a34(spans, point)`: kind 0 → slot 0, kind 6 → slot 1,
  others return; the list is copied into A with `(dx, dy)` = point +
  origin shift added as 16-bit words; count 0 loops 65535 times (refused),
  count > 800 refused. Mode 0: writer(A); mode 1: writer(`0x486d15(clip,
  A, B)`); other modes: kind 0 → `0x486d15(e+4, A, B)`, kind 2 →
  `0x486c00(A, [e+4], B)`, each nonzero result → writer.
- Point `0x4895ec(point)`: pen kind 0 → slot 5 with the resolved colour,
  kind 2 → slot 6 with the table, others return. Mode 1: inside the clip
  rect; mode 2: from the last entry to the first, kind 0 inside the rect,
  kind 2 the row's pair `spans + 4(y − y0) + 4` contains x → the writer is
  called once and the function returns.
- Slots 0/1: count 0 → nothing; y0 < 0 → `count −= −y0` (≤ 0 → nothing),
  skip the pairs above row 0 (**bug in slot 1**: it advances the row-table
  pointer instead of the span pointer → refused for y0 < 0); per row
  `n = r − l + 1 > 0` → `n` bytes from `T[0] + T[y+1] + l` (colour byte or
  `table[byte]`), rows step the pitch. Slot 2 counts rows in a 32-bit
  counter when the width is past 16, else in the 16-bit `bx` (`dec bx`:
  65536 rows for a height that is 0 mod 65536); a non-positive height or a
  negative width runs away (refused). Slot 3: a non-positive width or
  height runs away (refused). Slots 4/5/6: one byte.
- Bitmap draw: `0x4816a8(B, point, flags)` → compressed (`word[B+0x12]`)
  → `0x481910` (unreachable by data, refused; see below); else `0x4827bb(0x48d99e(), B+4, point,
  flags)` → `0x4887d8(H, point, flags)` (the surface argument is unused).
  `0x4887d8`: RLE (`[H+5] & 0xf == 1`) → `0x488df8` (dead, refused); mode
  0 → slot 26 `(H, pt, 0, flags)`; mode 1, or mode 2 with entries → slot
  28 `(H, pt, C+0x20c, flags)`; mode 2 without entries → nothing.
- Slot 26 `0x4899ac(H, point, cliprect, flags)`: with a clip rect: `x >
  clip.r` or `y > clip.b` → nothing; `x0 = max(x, clip.l)`, `y0 = max(y,
  clip.t)`; `xe = x + w ≤ clip.l` or `ye = y + h ≤ clip.t` → nothing; `xe =
  min(xe, clip.r + 1)`, `ye = min(ye, clip.b + 1)`, `cols = xe − x0`, `rows
  = ye − y0`. Without: the same against `0` and the surface pitch/height.
  Placement `flags & 3` (jump table `0x489ac3`): 0 as is; 1 flipped
  vertically (the bitmap's rows bottom-up onto the surface's rows top-down
  with the last row at `y0`, source starts at row `h − skipy − rows`);
  2 each row reversed (source read backwards from the end of row
  `skipy + rows − 1` into the surface from row `y0 + rows − 1` upwards);
  3 rotated by 180 degrees (source read backwards from the end of row
  `h − skipy − 1` into the surface from row `y0` downwards). Opaque
  (`[H+5] & 0x10`) copies every byte; transparent skips source bytes of 0.
  The transparent kernel (`0x489c2d..0x489c46`) runs away on a
  non-positive column count, which a reversed region span pair gives
  through slot 28's one-row rects (own code: refused).
- Slot 27 `0x489c6e` (grab, surface → bitmap): the same clipping and the
  same four placements with source and destination swapped (table
  `0x489d85`); always opaque.
- Slot 28 `0x489ede(H, point, clip, flags)`: mode ≠ 2 → slot 26's body
  with `clip`; mode 2: entries from the first to the last (**reverse of
  the fills**), kind 0 → slot 26 `(H, point, e+4, flags)`, kind 2 → one
  slot-26 call per span row with the rect `{left, y, right, y}`.
- Grab `0x488974(H, point, flags)`: the RLE nibble of `[H+5]` is cleared;
  `[H+4]` is forced to 8 when the surface format is 8 (or 4) and differs;
  the pixels are zeroed (`0x49681a`, size `[H+6]`); mode 0 → slot 27 `(H,
  pt, 0)`; mode 1 → slot 27 `(H, pt, C+0x20c)`; mode 2 → slot 27 per
  kind-0 entry from the first to the last (kind 2 skipped).
- Ellipse `0x48817c(rect)`: fill kind must be 0 or 4 (other kinds draw
  nothing); centre `((l+r) >> 1, (t+b) >> 1)`, `a = |r−l| >> 1`,
  `b = |b−t| >> 1` (`0x4887cb` = abs). `a = 0` or `b = 0` takes the
  degenerate line paths (`0x483a04`, vertical or horizontal) whose
  temporary pen's kind is stack residue: refused by the own code. Else a
  midpoint ellipse into B: word `−b`, word `2b` (**quirk**: the list has
  `2b` rows for `2b + 1` scan lines; the bottom row is written from the
  list's end backwards, so row `y` of the lower half lands one row up;
  `2b` above 800 is refused), the top half forward from `B+4`, the bottom
  half from the last pair backwards; region 1 (`dxa < dya`) emits on
  `d > 0` before the step, region 2 (`y > 0`) emits every step; then
  `0x486a34(B, centre)`. Kind 4: two passes with the fill kind forced to 0
  (`(−x, −x_prev)` / `(x_prev, x)` per row, `x_prev = x + 1` after an
  emitted row in region 1, `x_prev = x` after an x step in region 2), the
  kind restored. The `0x4953c4`/`0x49545b` calls of the ellipse have no
  guest effect (not modelled).
- Text: `0x482498(F, ch)` glyph width (fixed `word[F+0xc]` or
  `byte[[F+0x2a] + ch − first]`; the `sprintf` thunk `0x496796` it calls
  first has no lasting effect); `0x4824ec(F, str)` the sum; `0x48251c(F)`
  the height; `0x482534(F, point, ch)` draws the glyph: a header `{height,
  width, byte[F+0x12], flags = stack residue & 0xe0 (transparent, not
  RLE), w·h, [[F+0x26] + 4(ch − first)] + [F+0x22]}` at `(x, y −
  word[F+0x16])` through `0x4827bb`; the glyph table is indexed with the
  low word of `ch` sign-extended (`0x48257d`) and the width taken from the
  low byte; glyph headers are never opaque. The underline (`[GC+0x230]` set):
  `0x482d44(surface, ends, ends+8)` from `(x, y)` to `(x + width, y)`, as
  in the own code (see
  [The fill layer's object slots](#fill-layer-object-slots-and-exit-registers)).
  Every GC constructor clears `[GC+0x230]` (`0x480d21`, `0x430f2e`) and
  nothing else writes it, so play never draws an underline. `0x482634(F, point, str)`: alignment `[GC+0x231]` 1 →
  `x += [GC+0x235] − width`, 2 → `x += ([GC+0x235] − width) >> 1`; the
  glyphs left to right; the GC pointer is read unconditionally (NULL →
  refused).
- Scan converter: `0x4930c1(count, points, out)`: count 0 → return
  (EAX unchanged); ≤ 2 → `0x493192`; else the 64-bit twice-signed-area sum
  `x[n−1]·y[0] − y[n−1]·x[0] + Σ x[i]·y[i+1] − y[i]·x[i+1]` > 0 →
  `0x493192`, else `0x493141` (the vertices reversed into a local array of
  50; more → refused). `0x493192(count, points, out)`: the minimum vertex
  (smallest y, then the *largest* x among equals when scanning from the
  last vertex down), the maximum (largest y, smallest x); `ymax = ymin` →
  `out = {ymin, 1, x of the maximum vertex (the smaller), x of the minimum
  vertex (the larger)}`, EAX = the minimum vertex's x; else chain A from
  the minimum forward to the first vertex with `y ≥ ymax`, chain B from the
  maximum forward to the first with `y ≤ ymin`; chain A writes the right
  ends (`+4` of the row slot `out + 8·(y − ymin + 1)`), chain B the left
  ends; per edge: horizontal (`dy == 0`, `0x493335..0x49335f`) → only its
  own chain's field, the larger x into a right chain and the smaller into
  a left chain; vertical → the x into every row; diagonal → x ± 1 per
  row; y-major → Bresenham from the lower-x end; x-major (right chain) →
  x walks down from the larger x with `e = 2dy − dx`, the first row gets
  the start x, every later row the last x of the run; x-major (left
  chain) the mirror, x walking up. Result `out = {ymin, ymax − ymin + 1,
  pairs}`, EAX = the count. More than 400 rows per vertical/diagonal edge
  jumps outside the unrolled block (refused); 54 local vertices.

### Save, scroll and restore

`0x488012` (`..0x488044`) is the background **save**: it dispatches on the
record kind, 0 to `0x487eac` (copy the rectangle from the surface into a
pixel buffer), 2 to `0x487f6a` (refused). `0x487e60` (`..0x487e92`) is the
**scroll**: 0 to `0x487bdc`, 2 to `0x487d0d` (move the rectangle by a
point). `0x488143` (`..0x48817b`) is the **restore**: 0 to `0x488078`
(`..0x4880bb`, copy the buffer back; own entry
`es2_sim_gfx_restore_rect`, kind not read). `dialogs.c` names `0x488012`
and `0x487e60` `VA_SURFACE_FILL` and `VA_RECT_CLEAR`: misnomers only, its
calls match the save/scroll contract (open saves `(d+0x18, [d+0x2c])`,
close restores `(d+0x18, d+0x1c, surface)`, `options_run` scrolls
`(d+0x18, pt)`).

- Save `0x488012(rec, pixels)`, size `0x488045(rec)`, restore
  `0x488143(rec, point, pixels)`: kind 0 → `0x487eac` (a header
  `{h, w, 8, 0, w·h, pixels}` built from the rect `rec+4` + origin shift
  and `0x488974` at `(l, t)`), `0x487e94` (= `(b−t+1)·(r−l+1)`),
  `0x488078` (the header with flags 0x10 and `0x4887d8(H, point, 0)`);
  kind 2 → `0x487f6a`, `0x487f48`, `0x4880bc` (dead or broken: a
  misaligned copy, an endless loop for count 0; refused).
- Scroll `0x487e60(rec, point)`: a kind-0 record is the *source*; the
  slots get the rect moved by the point plus the origin shift
  (`0x487be6..0x487c32`) and read the source from destination minus
  point. Kind 0 → `0x487bdc`: mode 0 → slot 20 `(rect, point)`; mode 1 →
  slot 20 `(0x48699c(rect, clip))`; mode 2: entries from the last to the
  first, kind 0 → slot 20 of `0x48699c(rect, e+4)`, kind 2 → slot 19 of
  `0x486d15(rect, [e+4], A)`. Kind 2 → `0x487d0d` (dead in the game): a
  kind-2 record's `+4` is a pointer to the list; the list is copied into
  A moved by the point plus the origin shift (count 0 guarded by
  `jecxz`), mode 0 → slot 19 `(A)`, mode 1 → slot 19 `(0x486d15(clip, A,
  B))`, mode 2 → kind 0 `0x486d15(e+4, A, B)`, kind 2 `0x486c00(A, [e+4],
  B)` → slot 19. The slot always gets the *unshifted* point.
- Slot 19 `0x48a08a(spans, point)`: `(dx, dy)` = point; count 0 or `dx =
  dy = 0` → nothing; `dy < 0` or (`dy = 0`, `dx < 0`): rows ascending,
  forward byte copy from the left end; `dy > 0`: rows descending, forward
  copy; `dy = 0`, `dx > 0`: rows ascending, backward copy from the right
  end; per row `n = r − l + 1 > 0` bytes from `(x − dx, row − dy)` to `(x,
  row)` (a byte-wise copy in the original's direction reproduces every
  overlap). **Quirk:** the rightward path (`0x48a13b std`) returns with
  the direction flag set (no `cld` before `0x48a1c9`) and, for widths
  below 4, skips its `pop ds` (`0x48a19f jcxz`; healed by the caller's own
  register pops); the in-vitro harness clears the flag after every
  original call, the own code has no flag to leak. Slot 20
  `0x48a7a7(rect, point)` the same for a rect: `|dx| ≥ w` and `|dy| ≥ h` →
  the forward case; `dy < 0` forward from the top; `dy > 0` forward from
  the bottom; `dy = 0`: `dx < 0` forward, `dx > 0` backward from the
  right.

### Slot 29 `0x48afc8`: the stretched bitmap

`0x48afc8..0x48b520`; own code `slot_stretch` and the section "The
stretched and the rotated blit" of `gfx.c`. Arguments (header H, dw, dh,
point, clip, flags). `dw` or `dh` of 0 returns at once. The steps are
integer quotient and remainder of the source by the destination size: per
column `bw / dw` source pixels plus an error term of `bw % dw`, per row
`(bh / dh) * bw` bytes plus an error term of `bh % dh`; the error terms
start negative and step the source by one more pixel (row) whenever they
reach 0 or above (`0x48b489 js`), then subtract `dw` (`dh`). Flags 1, 2, 3
negate the column step, the row step, or both and move the first source
pixel to the matching corner (same placements as slot 28: 1 flipped
vertically, 2 each row reversed, 3 rotated by 180 degrees). Clip mode 0
and 1 use one rectangle (1 the clip argument); mode 2 walks `[C]` region
entries, more than 0x80 entries are refused; kind 0 is a rectangle, kind
2 a span list (x `0..pitch-1`, rows from the list header, every row
clipped to its own pair); any other kind is skipped without advancing the
entry pointer (the original's `continue` re-reads the same pointer; the
next entry is still taken by index). Rows are addressed as
`T[0] + y * pitch` (`[0x4a3652]`, `[0x4a3676]`), not through the per-row
offsets. The kernel writes a source byte if the header is opaque
(`H+5 & 0x10`) or the byte is non-zero. Refused: a negative row count
after the clip, or a negative column count with rows to draw and no span
list.

### Stretched and rotated blit callers

- `0x488aa8` (`..0x488b96`), (H, point, flags, size): shifts the point by
  the origin only when `[C+0x208] == 1`; mode 0 calls slot 29 with clip 0,
  mode 1 and mode 2 with a positive `[C]` pass `C+0x20c`; mode 2 with an
  empty region list draws nothing.
- `0x481c3b` (`..0x481c57`): the same with a surface argument that is not
  read.
- `0x4817c8` (`..0x48185a`): the bitmap object form; a compressed object
  (word `+0x12` non-zero) goes to the dead decoder `0x481910` and is
  refused, otherwise the header at object `+4`.
- `0x488b98` (`..0x488dc6`), (H, point, flags, size, angle): builds the
  four source corners `(0,0) (w-1,0) (w-1,h-1) (0,h-1)`, swaps 0/1 and 2/3
  for flags & 2, then 0/3 and 1/2 for flags & 1; the destination corners
  `(0,0) (sx,0) (sx,sy) (0,sy)` are rotated with `0x47eb30` (sine/cosine
  pair from the low word of the angle) and `0x47ffe8` (2-D rotate, once
  per corner), moved by the point, and filled as a 4-vertex polygon
  through the GL polygon fill `0x483eb8` with a temporary fill record
  `{5, 4, corners, H}` at `[C+0x228]` (kind 5 reaches slot 33);
  `[C+0x228]` is restored afterwards.
- `0x481c58` (`..0x481c79`): the same with the angle masked to 16 bits.
- `0x48185c` (`..0x4818f7`): the object form of `0x488b98`; compressed is
  refused.
- `0x4888c4` (slot 28 with a rect-offset point): no caller in the index.

The rows `rect_fill 0x486704`, `bitmap_rect_blit 0x488143`,
`bitmap_blit 0x4816a8` and `rotated_blit 0x48185c` call the `gfx.c`
services; the HUD paths that reach them draw into the DIB bits. A
compressed GLBitmap through `0x4816a8` (word `B+0x12` ≠ 0, which sends
`0x4816c2`, `0x48174e`, `0x4817e2` and `0x481876` to the decoder `0x481910`)
stops a replay with a named refusal. It is unreachable by data: the only
writer of `B+0x12` is the stream reader `0x481d8c` (vtable entry
`0x4a3078`), which stores the record's byte `+0x0e` at `0x481e0b`, and that
byte is 0 in all 3,518 GLBitmap records (tag `0e 00 28 00`) of SHELL0,
SIMALERT, SIMPATCH, SIMVOL0 and ZONES.

### GL refusals

Named refusals (`es2_sim_loop_refuse`) for dead or broken original paths:
compressed bitmaps `0x481910` (no CD record, see above), RLE `0x488df8`,
the kind-2 save/size/restore `0x487f6a`/`0x487f48`/`0x4880bc`, `0x482634`
with a NULL GC, slot 1 with `y0 < 0`, span counts 0
(65535-row loops) or > 800, rect fills with a run-away height or width
through slots 2/3, `0x4869fc` with a negative count, the ellipse with a
zero half axis or more than 800 rows, scan edges of more than 400 rows or
polygons of more than 50/54 vertices, slot 29's cases above, slot 26 on a
reversed region span pair.

### GL evidence

Every function above: the listing read in full. In vitro
`differential_gfx_test` (`gfx_cases.h`, ops `clip`, `fill`, `bitmap`,
`text`, `scan`, `stretch`, `rotate`) on synthetic surfaces with DATA
(`su_compare`) and the world compared after every call and EAX where the
original defines it. The fixtures order region span pairs (the own code
refuses a reversed pair). In vitro `0x483eb8`, `0x47eb30` and `0x47ffe8`
are original passthroughs in the harness dispatch. The `rotate` op zeroes
four DATA dwords on both sides before comparing: `0x6c5f88`/`0x6c5f8c`
(slot 33 stores local stack addresses at `0x48a940`/`0x48a955`) and
`0x4a591c`/`0x4a5920` (the slot 33 kernel parks ESP/EBP at
`0x48af6d`/`0x48af79`); they hold host stack addresses, not drawing
state. The degenerate ellipse and the kind-4 ramp outline are not in the
`fill` cases (the cases stay on the non-degenerate paths).

## Software polygon fills

The software-rasterizer fill family (the first two rows of
[the four fill families](#the-polygon-pipeline-four-fill-families)), the
perspective fill `0x468760` and the shade lookup `0x430e34`. Own code:
`sim/src/poly.c` (`sim/include/es2/sim/poly.h`), service rows
`sim/dispatch/services/poly_fills.h` and `poly_persp.h` (rows `poly_fill_persp`
`0x468760`, `poly_shade` `0x430e34`). Confirmed in vitro; not checked
live. The hooks `poly_clip` `0x472bcc`, `poly_spans` `0x472b14` and
`tex_edge` `0x48aaac` stay hooks; none of these functions calls them.
`0x469dc8` and `0x471d08` are reconstructed but get no row: nothing in the
original references them statically (no relocation, no rel32 call or
jump; `//sim:table_test`). `0x469e14` keeps its row `texture_place`.
`0x477d48` and `0x471cd8..0x471d61` (the edge tables and the 64-bit
helpers of the perspective path) belong to the fill.

### Fill data

All static DATA.

**Vertex** (used by the clippers and the edge tables): 28 bytes
`{x, y, z +8, u +0xc, v +0x10, w +0x14, shade +0x18}`.

**Flag word:** 8 z; 1 u/v, and inside it 2 w (the perspective
subdivision); 4 shade.

**Edge entry:** 0x28 bytes per row `{x, z >> 4 +4, u +8, v +0xc, w +0x10,
shade +0x14, u/w +0x18, v/w +0x1c, u high +0x20, v high +0x24}`. The
result `0x6c5390` is `{first row, rows, left table 0x6c0890, right table
0x6bbd90}`.

**Span state** `0x4a0968..0x4a09cc`:

| Field | Location |
|---|---|
| destination, width | `+0`, `+4` |
| u, v (Q16) | `+0x10`, `+0x18` |
| du, dv (Q16) | `+0x20`, `+0x28` |
| transparency byte | `0x4a0994` |
| shade table (256-byte rows) | `[0x4a0998]` |
| texture page | `0x4a099c` |
| end words | `0x4a09a0`, `0x4a09b8` |
| shade and its step | `0x4a09ac`, `0x4a09b0` |
| the caller's ESP | `0x4a09cc` |

**Run lists:**

- the pairs `[0x4a593c]` (`{start, length}`), the count `0x4a5938` and the
  span's left edge `0x4a5940`;
- the depth runs' own list `0x4a5944`;
- the per-row lists `[[0x4a5af8]]` (`{count, pointer}` per absolute row).

**Depth:** the state `0x4a5ad8..0x4a5ae8`, the row tag base `0x4a5aec`,
the switch `0x49f248` and the buffer `[0x6c5f90]`.

**Tables and levels:**

- the recip table `0x4d3cbc` (640 dwords, `0x10000 / n`);
- shade levels `0x4d3cb4`, haze levels `0x4d3cb8`;
- far `0x4a08ac`, the haze term `0x4a08b0`;
- texture pages `0x4d4f04`, 0x10000 bytes each, texel
  `[page << 16 | v << 8 | u]`.

**Buffers:** the x-clip output `0x4d4914`, the y-clip output `0x4d4b44`,
the 3D records `0x4d4d74` (0x28 bytes each) and the near-clip output
`0x4d46e4`.

### Fill functions

cdecl; EAX as named.

**Fixed point**

- `0x47e068(a, b)` = `(a << 16) / b`: a 64-bit dividend, `idiv`.
- `0x47e07d`, `0x47e08d` = bits 16..47 of `a * b`.
- `0x471cd8` / `0x471cf0` add / subtract a 64-bit pair to two dwords. EAX
  is the low addend.
- `0x471d08`: `shrd` / `sar`. EAX is the old high dword.
- `0x471d26(out, rec)`: unless `[rec+0x10]` is 0, divides
  `rec[0..1] << 16` and `rec[2..3] << 16` by it. EAX is the second
  quotient, or unchanged.
- `0x471d61(out, in, d, n)`: stores `(in64[0] / d) << n` and
  `(in64[1] / d) << n` as sign-extended 64-bit pairs, then
  `(in[4] << n) / d`.

**Haze**

- `0x4680e0` sets far.
- `0x4680f0(d)`: `e = min(d, far)`.
  - At or below `far >> 1`: the haze is 0 and EAX is `e`.
  - Above it: `r = min(0x47e068(2 (e - far/2), far), 0x10000)` and the
    haze is `(0x47e08d(r, haze levels - 1) * shades) << 8`. EAX is the
    haze.
- `0x468158(s)` = `((s (shades - 1) + haze) & ~0xff) + [0x4a0998]`.

**Cuts** `0x469894` (x) / `0x469b2c` (y) `(c, A, B, out, flags)`

- 0 when A or B lies on `c`.
- P is the end with the smaller coordinate on the axis (B on ties).
- `r = 0x47e068(c - P, Q - P)`. The other axis and each flagged field
  become `P + 0x47e08d(r, Q - P)`. Unflagged fields are not written.
- EAX 1.

**Clippers** `0x4699c8` / `0x469c64` `(n, in, out, flags)`

One pass against `[GC+0x210]` and `[GC+0x218] + 1` (y: `+0x214` and
`+0x21c` + 1). `prev` starts at the last vertex.

1. An inside `prev` is copied.
2. An outside `prev` with `cur` not on its side gives the entering cut at
   `prev`'s bound.
3. Then an outside `cur` gives the leaving cut at `cur`'s bound.
4. A cut returning 0 adds nothing.

EAX is the count. Because `hi + 1` counts as inside, a fill reaches
column `hi + 1` in x, while the edge tables stop at row `[GC+0x21c] - 1`.

**Pages**

- `0x469dc8(p)` writes `[[0x4a08b4]+0x804]`.
- `0x469ddc(t, p)` clears `t+0x800`, sets `t+0x804 = p` and writes 256
  pairs `{0, 0x100}`.

**Edge tables** `0x477d48(n, verts, flags)`

1. Duplicates of the last kept vertex or of the first are dropped into
   `0x6c53a0`. Fewer than 3 left: rows 0.
2. The top vertex (ties: larger x) starts chain 1, walking forward; the
   top with ties to the smaller x starts chain 2, walking backward. Each
   chain runs until a vertex at the maximum y.
3. Chain 1 fills the right table `0x6bbd90` and chain 2 the left table
   `0x6c0890`, from row `max(top, [GC+0x214])`. An edge starting at or
   below `[GC+0x21c]` ends its chain. An edge ending at or above
   `[GC+0x214]`, or with height 0, is skipped.
4. Per row:
   - x in Q16, stepped by `dx * recip`, stored `>> 16`;
   - z `<< 8`, stepped by `(dz * recip) >> 8`, stored `>> 4`;
   - shade stepped by `0x47e08d(ds, recip)`;
   - u/v/w stepped by `0x47e08d(d, recip << 8)`. With flag 2 the shift is
     `<< 10`, u/v are 64-bit pairs, and the quadratic u/w, v/w of the end
     points and the midpoint live at `0x6c565c..0x6c56a7`.

EAX is `0x6c5390`; its rows field is chain 2's.

**Span loops** (no arguments)

- `0x46ac14` writes texels straight, two per 16-bit store from an even
  address, with odd head and tail bytes (contract under
  [The affine texture span loop](#the-affine-texture-span-loop-0x46ac14)).
- `0x46ad4c` sends each texel through shade row `[0x4a09ac] >> 16`.
- `0x46aeb1` sends each texel through row `S >> 8`, with `S` stepping
  `[0x4a09b0]` per pixel. No transparency.
- u/v step with byte carries: `add edx, ebp; adc ch, dh; add eax, esi;
  adc cl, dl`.
- Transparency (`0x4a0994` byte nonzero) keeps the pixel for texel 0,
  also inside a pair (byte store).

**Run clippers** `0x48b524` / `0x48b5af` / `0x48b63a`

These send the span through the row's sorted run list. The first run is
read even when the count is 0. A run starting right of the left edge moves
the destination, u, v (and the shade) by the difference. A run reaching
the end is the last.

**Depth runs** `0x48b854`

Starts at `[0x4a5adc]` with z `[0x4a5ae0]`, stepping `[0x4a5ae4]`. Every
run of pixels whose z is below the buffer (unsigned) is written to the
buffer and listed. The search reads four buffer dwords per step, up to
three past the span.

**2D fill** `0x468414(n, verts, mode, page, shade*, flags)`

1. The flag word is 1 | 4 (mode 2) | 8 (depth on).
2. Clip x, clip y, then the edge tables.
3. Mode 1 turns `[shade]` into `(shades - 1) [shade] + haze`, and the span
   shade into that `<< 8`.
4. A run list is used when depth is on or `[GC+0x208] == 2`, and only if
   `[0x4a5af8]` is set. A row whose list is empty is skipped.
5. The span loop follows the mode: 2 Gouraud, 1 flat, otherwise texture.

**Quirk:** with depth on and no run pointer, every row is drawn whole
after the depth buffer is updated.

EAX is 0 when a clipper left nothing, otherwise `0x6c5390`.

**3D fill** `0x46817c(points, uv, n, page, shades, mode, transformed, flags)`

1. Each point goes into a record, copied or through `0x48c57c`.
2. It is projected (`0x48c5fc`, `0x48c6d0`) only at or beyond the near
   distance `[0x6c602c]`.
3. u, v are stored `<< 8`. Depth on: `+8` is the depth. Mode 2:
   `+0x18 = ((shades - 1) shade + haze) << 8`.
4. Near clip into `0x4d46e4`: crossings are interpolated in view space,
   projected with `0x48c6d0` alone, and get the near distance as depth.
5. Then `0x468414`.

### The perspective fill `0x468760`

`0x468760(points, uv, n, page, shades, mode, transformed, flags)`, listing
`0x468760..0x4690f0`: the perspective-correct textured fill (the sibling
of `0x46817c`; the near-terrain path of `0x46d448`).

- **Records.** Each point is transformed (unless `transformed`) and
  projected as `0x46817c` does; the record keeps u and v shifted left by
  16 and the depth at `+8`. Points in front of the near distance get
  `+0x14 = div16(1 << [0x6c6028], depth)` (the 1/w factor) and u, v are
  multiplied by it when they are copied. Near-clip crossings get
  `+0x14 = 0x10000` and the near distance as depth.
- **Clip and edges** as in `0x46817c`, with the flag word
  `0xb | 4 (mode 2) | 8 (depth on)`.
- **EAX** is what the listing leaves: 0 for nothing drawn, the edge
  builder's result, the page or the pitch, depending on the exit.
- **Mode 1** sets the shade up as `0x468414` does.
- **Row loop.** Width = right.x - left.x, with no +1. The row recip comes
  from the recip table, or from `div16(1, w)` for rows of 640 pixels and
  more. Then the depth runs or the run list. A and D (64-bit, in the
  guest frame) give u/w and v/w at the row ends. P (u, v at the left end)
  comes from the edge's `+0x18/+0x1c` when the far flag `[0x49f250]` is
  set, otherwise from `0x471d26`. The step shift s is 10 when both ends
  have the same w; otherwise it is 4/5/6/7, by `|lw - rw| sar 12` above
  3000/1000/300. Each step of `1 << s` pixels divides once (`0x471d61`,
  then `0x471d26`), gets Q, and spans with du/dv = `(Q - P) sar s`. The
  remainder uses the recip table at `rest` without a bound check: a
  remainder of 640 or more reads past the table into `0x4d46e4..`. That
  read is reproduced as a guest read.
- **Undefined values.** u/w and v/w are stack values that `0x471d26`
  defines. A read before they are defined (w == 0) is refused ("stack
  garbage"). The in-vitro inputs keep w > 0, so this path is not
  compared.

### The shade lookup `0x430e34`

`0x430e34(colour, level)`, listing `0x430e34..0x430e8c`. The palette is
T = `[0x49b020]`, with an int16 entry count at `+0xc` and an entry table at
`+0x10`. Each entry is 8 bytes: {pointer, int16 count}.

1. idx = colour & 0xff, or 0 when idx >= the entry count (a signed
   compare).
2. An entry count of 0 gives 0.
3. Otherwise i = `0x47e0a0(level, count)`. That leaf is the q8 product,
   the low dword of `(level * count) >> 8`. It has no row, so it is
   computed inline.
4. When i == count, i becomes count - 1. Other values are not bounded.
5. The result is the int16 at `[entry] + 2i`, sign-extended into EAX and
   EDX. ECX is T, or `[entry]` once an entry is used.

### Dead fill code

`0x4690f0` (the third fill variant), `0x48b6d2` and `0x48b789` (run
clippers) have no call, jump or address reference in the listing and their
addresses do not occur as a dword anywhere in the image (no table entry,
no relocation). `0x46afdc` is only called from `0x48b770` (inside
`0x48b6d2`) and `0x46b0a0` only from `0x48b838` (inside `0x48b789`). All
five are dead code; they get no rows.

### Fill refusals

Named refusals (`es2_sim_loop_refuse`):

- Integer divide faults (#DE) of `0x47e068` and of the 64-bit divides: a
  zero divisor or a quotient outside int32. es2sim ends the process there
  with the original's code; the refusal stays without a process record
  ([own_process.md](../reference-game/own_process.md) "The original's
  fault paths").
- `0x477d48` with a vertex y outside `(-1000000, 1000000)`; the original's
  search compares against those sentinels.
- `0x477d48` with flag 2 but not flag 1 (uninitialised accumulators).
- An exhausted call area in `0x46817c`.
- `0x468760` reading u/w, v/w before they are defined (w == 0).

### Fill evidence

In vitro `//tools/reference-game/invitro:differential_poly_test`
(`poly_cases.h`, ops `helpers`, `cut`, `clip`, `edges`, `span`, `fill`,
`fill3d`). Both sides start from one random world: surface, GC, polygons,
depth buffer, run lists, shade table, texture pages. After every call the
harness compares EAX where defined, DATA (`su_compare`, with the host ESP
at `0x4a09cc` masked) and the world. The `edges` cases cover flags 0..15
(flag 2 only with 1), duplicates, flat and clipped polygons; `span` all
seven loops, odd/even ends, opaque and transparent; `fill` modes 0..2,
depth on/off, region mode 2 with and without run lists; `fill3d`
transformed and through the original `transform_point`, near cuts,
modes 0..2. Unit `//sim:poly_test` checks the contract by pixel counts and
samples.

In vitro `differential_poly_rest_test` (`pfill_cases.h`, ops `persp` and
`shade`): `persp` compares EAX, DATA and the world, and covers near cuts,
far-flag fills, every step shift, rows of 640 pixels and more and
remainders of 640 and more; cases whose 64-bit quotient does not fit are
skipped (the own side refuses with `#DE`; the original faults as well and
the harness cannot catch it). `shade` compares EAX, EDX, DATA and the
world (ECX is not visible to the harness).

## GL fill layer and line

The GC polygon fill `0x483eb8` with its entries `0x484222`/`0x4842bb`, the
clipper, the span builder, the line and BMP_8 slot 34. Own code:
`sim/src/fill.c` (`sim/include/es2/sim/fill.h`), service rows
`sim/dispatch/services/fill_layer.h` (`0x483eb8`, `0x484222`, `0x4842bb`,
`0x483a04`, `0x48a3b0`). Confirmed in vitro; not checked live. Calls the
own code does not reconstruct go through the dispatch: slot 33
`[0x4a5830]` (kinds 5 and 7) and any slot or writer pointer that is not
the BMP_8 body; the Gouraud fill `0x487acc` (kind 1) has its own row
([Gouraud and flat fill](#gouraud-and-flat-fill)).

### The GC polygon fill `0x483eb8`

`0x483eb8(n, pts)`, C = `[0x4a3614]`, F = `[C+0x228]`.

- **Region offset.** With a region (`[C+0x208] != 0`), the points are
  translated by `[C+0x20c]-[C+0x21c]` / `[C+0x210]-[C+0x220]` into a local
  copy, and `[C+0x208]` is 0 during the call. The origin copy is 21 points
  at `[ebp-0xbc]`; the copy loop runs n times without a bound: n = 0 or
  n > 21 is refused (a runaway copy, or the saved flag overwritten).
- **Outline.** n < 3 and fill kind 4 draw the outline with
  `0x4895ec`/`0x483a04`, using the pen `{0, [F+4]}` in `[C+0x224]`. The
  pen lives at `[ebp-0x1c]`, which is the origin copy's point 20: with an
  origin and 21 points the outline's last line therefore ends at
  `{0, colour}` (reproduced; the guest frame has the original's layout).
  The outline tests n with `jcxz` (low word only); n = 1 draws the point
  `0x4895ec`, n = 2 one line `pts[1] -> pts[0]`, n >= 3 the lines
  `pts[i+1] -> pts[i]` and the closing `pts[0] -> pts[n-1]`.
- **Fill kind** `[F]`:
  - 0: plain colour, or a ramp through `0x4a4fa5` when the high byte is 4;
    a colour whose high byte is neither 0 nor 4 draws nothing.
  - 1: Gouraud `0x487acc`.
  - 5 and 7: `[0x4a5830]` (slot 33).
  - 6: the pattern writer `[0x4a57b0]`.
  - Other kinds draw nothing.
- **Clip mode** `[C+0x204]`:
  - 0: span build `0x484720` only.
  - 1: `0x484338` against `C+0x20c`, then `0x484720`.
  - Otherwise: the region list `[C]`, where kind 0 entries clip and fill
    and kind 2 entries combine through `0x486c00`. The region loop tests
    the full dword count (`or ecx,ecx`), unlike the line's `jcxz`.
- The clip output buffer is 20 points at `[ebp-0x15c]` followed by the
  copy; an output of more than 41 points reaches the saved registers and
  is refused.
- EAX is the saved `[C+0x208]` on every return.

**Region count 0x10000.** The region loop takes the full dword count and
walks the entries from `[C + 4 + 4 * (count - 1)]` down. With 0x10000, the
first entry pointer is read 0x40000 bytes past C. C is a record of 0x230
bytes; its region table (`+4..+0x203`) holds at most 0x80 entries. So the
walk reads foreign memory as entry pointers and dereferences them; in the
game the result depends on whatever follows the record, and no natural
case keeps the original on defined memory. The own loop (`fill.c`,
`do { --count; e = [C+4+4*count]; ... }`) reads the same guest addresses
in the same order, so on equal memory it does the same thing. Not
compared in vitro.

### Orientation entries `0x484222` and `0x4842bb`

- **`0x484222(n, pts)`** reverses the points into a local copy (20 at
  most) and calls `0x483eb8`. n = 0 returns with EAX undefined; n <= 2
  (signed) goes to `0x483eb8` unchanged; more than 20 points overwrite the
  saved registers (refused). For kind 5 the table `[F+8]` is replaced by a
  reversed copy at `[ebp-0x140]` during the call and restored after; EAX
  is then F.
- **`0x4842bb(n, pts)`** takes the 64-bit signed doubled area. n = 0
  returns with EAX undefined; n <= 2 goes to `0x483eb8`. A positive high
  dword calls `0x483eb8`; a negative one calls `0x484222`. With a high
  dword of 0, the low dword is tested signed (`jg`), so an area of
  `0x80000000..0xffffffff` goes to `0x484222`.

### Clipper `0x484338` and span builder `0x484720`

- **`0x484338(n, pts, out, rect)`** is a two-pass clipper: x against
  left/right into a local buffer of 50 points, then y against top/bottom
  into `out`; n <= 1 returns n, and the result is the output count. A 51st
  point in the x pass overwrites the counter (refused). The cut is
  `a * b / d` with a 64-bit product and a 32-bit `idiv` (#DE refused).
- **`0x484720` span builder**: the top point is the lowest y, ties to the
  greatest x scanning from `n-1` down to 1; the bottom the greatest y,
  ties to the least x. One row gives `{y, 1, x_bottom, x_top}` and EAX =
  x_top; otherwise EAX = rows. The right chain runs forward from the top,
  the left chain forward from the bottom; more than 50 chain points,
  fewer than 2 points (the top search never ends) and more than 800 rows
  are refused. Vertical and 45-degree edges go through the unrolled block
  `0x484df0..0x4866f0` entered by a computed jump; it stores EBP at
  `0x4a33d4` (a host value in the own code, masked in the comparison).

### The line `0x483a04` and slot 34

- **`0x483a04(p0, p1)`** the line with the pen `[C+0x224]`: kind 0 slot 0
  with the resolved colour, kind 2 slot 1 with `pen+4`, other kinds
  nothing. Shifted by the origin when `[C+0x208]` is set (not cleared).
  Mode 0 and 1 (after `0x483c6c` against the clip rect) call slot 34
  `[0x4a5834]` when the writer is still slot 0, else build spans with
  `0x483d6c`. Other modes walk the regions from the last (`jcxz` on the
  low word): kind 0 clips copies of both points against the entry's rect,
  kind 2 converts once and combines through `0x486c00`. EAX undefined.
- **`0x483c6c(a, b, rect)`** clips against top, left, bottom, right in
  that order; the outside end moves onto the bound (cut `a * b / d`,
  64-bit product, 32-bit `idiv`). When b is the outside end the two ends
  swap and stay swapped in the written result. EAX = 1 (both points
  written) or 0 (nothing written).
- **`0x483d6c(a, b, out)`** writes the line's span list from the left end
  (Bresenham, one `{left, right}` pair per row, filled backwards for a
  rising line); more than 800 rows are refused.
- **`0x48a3b0(colour, p0, p1)`** BMP_8 slot 34: Bresenham from the left
  end with doubled deltas, `big/2 + 1` pixels; more than 65536 pixels are
  refused. It clobbers EBX/ESI/EDI, so the harness cannot call it directly
  through the machine-call thunk; it is covered through the line.

### GL fill evidence

In vitro `//tools/reference-game/invitro:differential_gl_fill_test`
(`gl_cases.h`, ops `clip`, `spans`, `line clip`, `line spans`, `line`,
`polygon`, `orient`, `rotate`): the original and the own code on the gx
world, slot 33 and (sometimes) slot 34 replaced by recorders in both,
`0x4a33d4` masked. `polygon` covers the three entries, fill kinds 0..8,
clip modes 0..2, with and without origin, n = 0..21 and odd counts;
`orient` large coordinates through `0x4842bb`; `rotate` the gfx rotate
cases with the fill rows in the dispatch. The harness avoids paths where
the original faults: a Gouraud ramp outside 0..255 (`0x486ee8`'s colour
tables overflow), a fill region count of 0x10000, more than 20 points into
`0x484222` (also via `0x4842bb`), and 21 origin points with a kind-4
outline in mode 0 (the last line ends at `{0, colour}` outside the
surface).

## Gouraud and flat fill

The Gouraud fill `0x487acc`, its triangle `0x486ee8` and the flat fill
`0x46bc44`. Own code: `sim/src/gouraud.c`
(`sim/include/es2/sim/gouraud.h`), service rows
`sim/dispatch/services/gouraud_fill.h` (`fill_gouraud`,
`fill_gouraud_triangle`, `flat_fill`). Listing ranges
`0x487acc..0x487bd9`, `0x486ee8..0x487acb` and `0x46bc44..0x46bd9a`.
Confirmed in vitro; not checked live.

### The Gouraud triangle `0x486ee8`

`0x486ee8(P, S)`: three dword points and three shade words.

- Two equal points (tested P0/P1, P0/P2, P2/P1 in that order) return
  at once; EAX is the y of the last pair read.
- Equal shades: `[F+4]` = the shade, then `0x483eb8(3, P)`.
- Otherwise T0 is the point with the highest shade, T2 the one with the
  lowest (the last index wins on ties) and T1 the third.
- The steps are D = hi - lo along T0->T2 (edge A), Dh = hi - mid along
  T0->T1 (B) and Dl = mid - lo along T1->T2 (C). Step i of an edge is
  `low32((b - a) * i) / d + a` per coordinate (signed `idiv`); the first
  and last entries are the end points themselves.
- Band i is the quad between A and B (or C). Its colour word `hi - i`
  (on C `mid - i`) goes into `[F+4]` (F read again per band), then
  `0x483eb8(4, Q)`.
- Q is `{b_i, b_i+1, a_i+1, a_i}` when T2 does not follow T0 in the
  input order (winding flag `(ihi + 1) % 3 != ilo`), else
  `{a_i, a_i+1, b_i+1, b_i}`.
- Dh = 0: only C bands, coloured `hi - i`. Dl = 0: only B bands.
- EAX is that of the last fill.
- The three edge tables are 257-entry locals. D > 256 writes past them
  into the other locals (from 285) and the return address (from 295),
  so it is refused.

### The Gouraud fill `0x487acc`

`0x487acc(n, P)`: frame 0x60, kept in guest memory with the original's
layout (`ebp-0x60` the triangle's points, `-0x48` the colours L[],
`-0x20`/`-0x1c` the point pointers, `-0x18` the three colour words, `-8`
the ramp table pointer, `-4` the saved colour word).

- `[F] = 0` for the run.
- The ramp entry is `0x4a4fa5 + 8 * (low word of [F+4])`.
- `L[i] = table[(S[i] * count) >> 8]`, where S are the shade words
  `[F+0xc]` and count is the entry's int16. The 32-bit product mixes the
  unsigned shade and the signed count. The table pointer is read again
  from the frame for every point.
- Triangle k is `{P0, P[k+1], P[k+2]}` with the colours
  `{L0, L[k+1], L[k+2]}`. The pointers are stored before the colours
  are read.
- At the end `[F+4]` is restored from the saved word (F read again) and
  `[F] = 1`. EAX = F, EDX = the restored colour, ECX = n - 2.
- Past 20 points L runs into the frame: the pointers, the colour words,
  the table pointer (points 32/33, which the colour loop then reads
  back) and the saved colour (34/35, which becomes EDX). The own code
  reproduces all of this through the guest frame. Past 20 points the
  later triangles repeat a point, so the colours from the frame are never
  drawn; from 33 points the ramp words chosen by S[32]/S[33] are the
  table pointer's own halves.
- From 37 points the saved EBP is overwritten: refused. Also refused:
  n = 0x80000000/0x80000001 (signed negative with n - 2 > 0), where the
  fan runs over unset colours.

### The flat fill `0x46bc44`

`0x46bc44(P, n, S)`: points of three dwords, frame 0x50 in guest memory.

- It sets the render context's polygon: word count `0x6c68f0`, points
  `0x6c68ec`, the default index list `0x6c6068` in `0x6c68f2`, n
  vertices from 0 (`0x6c68e4`/`0x6c68e6`). It clears n state bytes from
  `0x6c68fa`.
- It calls the view-space projection `0x48cce4`. EAX = 1 means a point
  lies behind the near plane: then the clip `0x48cf20` runs, and with the
  shaded terrain (`[0x4aaaac] == 2`) also `0x48d38c(S, ebp-0x40)` for the
  shade words.
- GC+0x22c (GC = `[0x6c5f70]`) is set to one of two records:
  - mode 2: the Gouraud record `{1, 2, 0, S or the clipped words}` at
    `ebp-0x50`;
  - otherwise: the plain record `{0, byte [0x468158(0x4b) +
    0x430e34(2, S[0])]}` at `ebp-0x34`.

  The pointer is left on the dead local after the return (normalised in
  the in-vitro harness).
- The construction counter `[[0x6cbee4]]` is raised by two around
  `0x42e8e8({0x4a0ca0, word 7}, 0, 0)` (the shade ranges) and the shape
  fill `0x48d5c0`, then lowered by two.
- EAX = EDX = `[0x6cbee4]`. ECX = the FS:0 head restored at the return:
  the own code reads the calling thread's FS:0, as `frame_buffer.c` and
  `hud_widgets.c` do. The frame record of `0x49630c` (from `ebp-0x2c`)
  is not linked. Its context word at `ebp-0x1c` is written (8).
- Refused: `[0x6cbee4]` = 0 (the frame record set-up faults); n > 1000
  (the state bytes reach the point copies at `0x6c6ce2`); more than 10
  clipped shade words (they overwrite the saved FS:0 at `ebp-0x2c`).
- The callees `0x48cce4` and `0x42e8e8` are own rows
  ([View-space projection and shade ranges](#view-space-projection-and-shade-ranges));
  the clips, the shape fill and the two lookups are rows too.

### Gouraud evidence

In vitro `//tools/reference-game/invitro:differential_gl_gouraud_test`
(`gouraud_cases.h`, ops `triangle`, `gouraud`, `polygon`, `flat fill`):

- `triangle`: `0x486ee8` direct; fill kinds 0, 2..8 (5/7 with the slot-33
  recorder), clip modes 0..2, spreads 0..256 (D = 256 included), every
  path (equal points, one colour, B empty, C empty, both edges) and both
  windings.
- `gouraud`: `0x487acc` direct through the register-capturing call,
  EAX/EDX/ECX compared; n below 3, 3..20, 21..32, 33..36 and negative.
- `polygon`: fill kind 1 through `0x483eb8`, `0x484222` and `0x4842bb`.
- `flat fill`: `0x46bc44` on the shape draw world, shaded and plain, with
  a shift and with the divisor, points behind the near plane. `0x468158`
  and `0x430e34` are replaced by the shape draw stand-ins on both sides;
  `0x4842bb` is a recorder on both sides and also logs the GC+0x22c
  record (kind, colour, shade words). `0x42e8e8` takes its early return
  (`[0x49aef2]` = 0, an empty mark entry); its other path is covered by
  the shade-ranges cases.

`gl_cases.h`'s dispatch passes `0x487acc` to the original only while it
has no row, so `differential_gl_fill_test`'s kind-1 polygons run
`gouraud.c` too.

## DTS shape draw

The part of the renderer that walks a shape instance and hands polygons
to the fill layer: the part and polygon draw slots, the detail choice,
the textured, plain, lit and BSP polygon paths, the sprite part, the
projections with their clips, the segment clip and the shade ranges. Own
code: `sim/src/shape_draw.c` (`sim/include/es2/sim/shape_draw.h`),
service rows `sim/dispatch/services/shape_draw.h`, `shape_draw_callees.h` and
`shade_ranges.h`. Confirmed in vitro; not checked live. The shape draw
lives at `0x474ec0..0x476c6d`, `0x478d60..0x478de1`,
`0x48c72c..0x48dbd6` and `0x490ab8..0x4912a0`, with the actor callers at
`0x401fe4`, `0x4033e4..0x4034f4` and `0x46b8ea`. The range
`0x46be9c..0x46f000` is the terrain renderer, not the shape draw.

### The draw path

An actor (`0x46b8ea`) builds its matrix once (`0x401fe4`: `euler_matrix`,
selected with `0x48c444`) and calls its instance's slot `+0x1c`. The
instance slot `0x478da4` pushes the instance matrix (`0x478d60`:
`matrix_compose` with the current object matrix into a stack local,
selected), lends the shape the instance's transform table and draws it
with `0x490c1c`, then pops (`0x478d90`). The shape draw
(`0x490ab8(shape, cells)` / `0x490c1c(inst)`) stores the cell words
(`0x6b7b6c`) and the transform table (`0x4760e4`, which saves view 0 with
`0x48d718` when there is a table). When `0x4a62e0` is set it runs the
detail test `0x476248`. A result of 0 draws nothing. A result of 2 with a
clipping context turns the clip flag `+0x208` off around the draw. The
shape record goes into `0x6c6064` around the part list `0x476808`.

Each part's slot runs its transform first (`0x476120` -> `0x47613c`). A
transform's view is composed once per shape draw (its 0x20-byte matrix
with saved view 0's object matrix, `0x48c444`, saved into the
`0x6b7b70 + 0x110 * i` record and marked in `0x6bb2b0[i]`). Afterwards it
is loaded with `0x48d7ec`.

### Parts and the BSP

The part kinds (slot `+0x1c` of their vtables):

| Slot | Part | Children visited | EAX |
|---|---|---|---|
| `0x476808` | list | all, in order | the child count |
| `0x4759d4` | polygon group | its polygons, with the context's `+0x228/+0x22c/+0x20c` swapped for the pen, the mode and 0; colours, points, point count and index list become the render context's; the point states are cleared | the restored `+0x20c` |
| `0x4768f0` | cell animation | child (cell word `(int16)+0x16`, read unsigned) mod count | the child's |
| `0x4769c8` | detail | one child by projected size against the threshold words `+0x16`, from the index `0x4a1020` on, capped at count - 1 | the frame address (`lea eax,[ebp-0x30]` for the empty `0x476224`) |
| `0x476c18` | BSP | the tree `0x476b28` from node 0, the eye at `0x6bb2e8` | the tree's |
| `0x475c04` | BSP polygon group | as `0x4759d4`, with the polygons drawn by `0x475b2c` from node 0 | the frame address (`0x476224` is empty) |
| `0x4763f4` | sprite | — (below) | below |

The BSP node of `0x476b28` (14 bytes at `+0x18`): normal words, distance
dword `+6`, links `+0xa`/`+0xc`. With the transform word (`+0x1c` table)
`0xffff`, the eye side is d = n . eye - dist (`0x47dd1a`, a wrapping
dword dot). Otherwise d comes through the transform with `0x4911c0`: per
row a 64-bit sum plus 0x2000, `>> 14`, times the normal word, summed in
32 bits. `(d < 0) != [0x49f248]` visits `+0xa` first. A link with
`0x4000` is a child slot, another nonnegative one a subtree, a negative
one nothing. EAX is the last visited link's, or the first link itself
when nothing was visited.

The polygon-group BSP `0x475b2c(g, node)`: the nodes are 10 bytes each,
at `g+0x2a`, with a distance dword, a polygon word at `+4` and links at
`+6`/`+8`. The eye side is `d = point . eye - distance`, with the point's
words sign-extended. For `d < 0` the walk visits link `+6` first,
otherwise `+8` first. The node's own polygon is drawn between the two
links. A link with bit `0x4000` is a polygon draw slot (`& 0x3fff`). A
nonnegative link without that bit is a subtree. A negative link draws
nothing.

### Polygon slots

`0x474ec0` flat (both colours through the haze table `0x468158(0x80)`),
`0x475538` lit (`0x48bfe8` light level, the normal negated for a back
face, the shade lookup `0x430e34`, the haze table), `0x4756d4` Gouraud
(one level per vertex into 32 stack words, the clip `0x48d38c` when a
point was not projected, mode 1 with the intensities at `0x6c6060`). All
three first run the back-face test (`0x48c72c`, or `0x48c844` with dword
points). With a perspective shift this is the dot of the normal with
eye - point; without one, the y sign of the rotated normal. They then
project (`0x48c954`, or `0x4912a0` with a shift), clip at the near plane
(`0x48cf20`/`0x48d38c`) and fill through `0x48d624(back)` (`0x484222`
back, `0x483eb8` front) or `0x48d5c0` (`0x4842bb`). A second pass follows
with the second colour and mode 4 when the colours differ (not in mode 5
for `0x48d624`). `0x48d9ba`/`0x48dbac`/`0x48dbc1` forward (count, points)
to the fill layer.

The thunks `0x48d700`/`0x48d708` front the two fills; only `0x48d708`
has a static reference and a row (`shape_fill_polygon`); `0x48d700` and
the part scale setter `0x4769a8` have none and stay unrowed.
`0x48d708(back)` loads its argument into EAX before it calls `0x48d624`,
so an empty polygon leaves EAX = `back`. The callers that go through the
thunk are `0x475cd0`, `0x475d8c`, `0x475eec`; `0x474ec0`, `0x475538`,
`0x4756d4` and `0x474fa8` call `0x48d624` directly.

| VA | Row | What |
|---|---|---|
| `0x48ca70` | `shape_project_dwords` | projection of the polygon's vertices from the dword points `0x6c68ec` |
| `0x48d0f8` | `shape_clip_uv` | `0x48cf20`'s near-plane clip with the vertices' UV pairs |
| `0x475cd0` | `shape_polygon_plain` | flat polygon without the haze table |
| `0x475d8c` | `shape_polygon_lit` | lit polygon without the haze table |
| `0x475eec` | `shape_polygon_gouraud_plain` | `0x4756d4`'s code, filling through the thunk `0x48d708` |
| `0x474fa8` | `shape_polygon_textured` | textured polygon (fill layer or 3D fill) |
| `0x475b2c` | `shape_group_bsp_node` | BSP walk of a polygon group |
| `0x475c04` | `shape_part_group_bsp` | BSP polygon group part |
| `0x4763f4` | `shape_part_sprite` | sprite part (the rotated blit `0x48185c`) |
| `0x40bb4c` | `shape_segment_clip` | near-plane clip of a segment |

- **`0x48ca70`.** Without a shift it works like `0x48c954`: an
  untransformed point is marked 2 and projected with the divisor, a
  projected one is copied, and other points keep their entry. With a
  shift, an untransformed point whose depth is strictly below the near
  plane is marked 1. Otherwise it is marked 2 and projected as
  `(x << shift) / depth + cx`, `cy - (z << shift) / depth`. A point marked
  1 makes the result 1. The index word is read again before each use. The
  output count is set to the vertex count only when the result is 0. EAX
  is the result.
- **`0x48d0f8(in, out)`.** It clips like `0x48cf20` and also carries the
  UV pairs (two dwords per vertex): a copied point takes its own pair; a
  crossing gets `low32((in[i] - prev) * k) / d + prev` for u and v; the
  starting pair is `in[count - 1]`; the pairs are read from guest memory
  at each use. EAX holds the output count in its low word. The high word
  comes from the last v quotient.
- **`0x475cd0`.** Flat polygon: the colours by side, the projection, the
  clip, mode 0, then the fill through the thunk `0x48d708`. EAX is `0x14`
  when the polygon is not drawn. **`0x475d8c`** first ORs the light level
  `0x48bfe8(normal, point) & 0xff` into bits 16..23 of both colours. For a
  back face the normal is negated as words.
- **`0x474fa8`.** Textured polygon:
  - **Static arrays.** The three static arrays are built once by
    `vector_new` (`0x49667c`), under the guard bytes `0x4a0f4c`/`d`/`e`.
  - **Texture index.** Colour dword 0 selects the front: the back-face
    test is positive and the index is not -1. Dword 2 selects the back.
    When that index is -1, nothing is drawn and EAX is -1.
  - **Which texture.** With `0x49f244` set, the texture is the shape
    record's bitmap entry (`+8` table) plus 4. Otherwise it is the shape's
    texture record (`+4` table, 0x14 bytes each). Nothing is drawn when
    the texture is 0.
  - **Lights.** With `0x4a5b04` set, the light level is computed. When
    `0x4aaaac == 2` the code also takes the level, half the level and
    `min(level + half, 0xff)`. With `0x49f244` it takes the shade row
    `((0x4a5b04 - 1) * level & ~0xff) + 0x4a5b00`.
  - **Drawing with `0x49f244`.** The fill layer draws the clipped polygon.
    The mode record `{7, count, UV, texture, shade}` goes into the
    context's `+0x22c`, and `0x6c6054` is restored afterwards. The fill
    reads the mode from that record, not from `0x6c6054`.
  - **Drawing without `0x49f244`.** The vertices and the UV go to the 3D
    fill: `0x468760` when `0x49f24c` is set, otherwise `0x46817c`.
    Vertices 1 and 3 are swapped for a back face.
  - **Dead block.** The block `0x47533b..0x475385` has no entry: no jump
    or call targets it.
- **`0x4763f4(part)`.** Sprite part:
  - **Placement.** The bitmap is entry word `+0x10` of the record's `+8`
    table, placed at the part's centre in view space.
  - **Nothing drawn.** At or behind the near plane, EAX is the depth.
    Outside ±1000 on screen, EAX is the projected x. Without a bitmap, EAX
    is the frame address.
  - **Scale.** The scale is `0x48c5cc(radius << 2, depth)`.
  - **Angle.** The view's up direction is taken from `(0,0,0)` and
    `(0,0,0x800)`. Their x/z distance gives the height factor. Their
    projections (a zero depth is taken as 1) give the angle
    `angle_ratio(-dx, dy) + 0xc000`, which is 0 when dx is 0.
  - **Height and offsets.** The height `h` is the factor applied between
    the bitmap words `+6` and `+4`, rounded toward zero over 2048. The
    offsets come from bytes `+0x13`/`+0x12`, scaled by `multiply_q8`,
    negated and rotated by the angle.
  - **Blit.** Then `0x48185c(bitmap, point, 0, sizes, angle)`. EAX is the
    blit's result.
- **`0x40bb4c(a, b)`.** When both depths are below the near plane,
  nothing changes and EAX is 0. Otherwise the end below the plane (`a`
  first) moves to the crossing: `k = 0x47e068(near - depth, other -
  depth)`, x and z move by `0x47e08d(other - own, k)`, and the depth
  becomes the near plane. AL is 1; the upper bytes come from the last
  product.

### Detail levels of actors

`0x4033e4` projects the size as `0x4769c8` does, with the scale
`0x497368`. Its level starts at `min(count - 1, 0x49736c)` and advances
while the size is below the threshold dwords. The shape of the chosen
level goes into the instance for the draw. `0x4034f4` wraps it with
`0x4a1020 = 0x4a95b4`. The tables are set by `0x45d490` (five level
words: `0x497368`, `0x49736c`, `0x4a9868`) and `0x45d50c` (`0x4a95b4`,
`0x4a9dc4`); both are rows of `hud_load.c`, and the in-vitro harness
presets their words.

### Shape draw quirks

All reproduced.

- `0x48d718` copies one light record per active light, but always from the
  FIRST light (the source pointer is never advanced).
- The near-plane clips interpolate in view space and omit the `<< 8` /
  divisor scaling of the shift-less projection.
- `0x4769c8` keeps a dead compare (`cmp edx, eax` at `0x476a58` before the
  loop).
- `0x4033e4` reads its thresholds from the table's first entry, not from
  the starting level.
- `0x48c954` leaves points marked 1 unwritten in the output.
- A Gouraud polygon with more than 32 vertices overruns the original's
  locals (the buffer pointer first). The own code refuses it.
- The Gouraud clip writes its output below its input on the stack and
  runs into it. The own code re-reads the input from guest memory per use.
- `0x476248` pushes EDX with a dirty upper half to `heading_cos`. The own
  code passes the same value.
- A matrix's byte `+0x13` is padding: `matrix_compose` writes only `+0x12`.
  When it composes into a stack local, `0x48c444`, `0x48d718` and `0x48d7ec`
  carry the stack garbage into `0x6c5fc7`, `0x6c5fe7` and the saved views.
- A shape draw called directly leaves the object matrix pointer
  `0x6c5ff8` on its last part's dead stack local. Through `0x478da4` the
  pop selects the previous matrix. A Gouraud polygon likewise leaves
  `0x6c6060` on its dead locals.
- `0x4760e4` with no table stores 0 into `0x6b7b68`. A BSP node with a
  transform word then reads at `(word << 5) + 0`.

### View-space projection and shade ranges

Listing ranges `0x42e878..0x42ea42`, `0x430518..0x430637`,
`0x486e10..0x486e3e`, `0x48cce4..0x48cf1c`.

| VA | Row | What |
|---|---|---|
| `0x48cce4` | `shape_project_view` | projection of the polygon's vertices from the dword points `0x6c68ec`, already in view space |
| `0x42e8e8` | `shade_ranges` | the shade ranges of an object (args: object, level, flag) |
| `0x42e878` | `shade_ranges_level` | the level index of an object (args: object, level) |
| `0x42ea0c` | `shade_ranges_default` | the default ranges |
| `0x430518` | `shade_ranges_runs` | range records from one run pair (args: first record, count, pair) |
| `0x430590` | `shade_ranges_spans` | range records from lists of run pairs (args: first record, count, records) |

`0x486e10(a, n, src, 0)` (copy `n` records of 8 bytes to the range table
`0x4a4fa5 + 8a`) is inlined. It leaves EAX at the last dword copied
(`src` when `n` is 0) and ECX at 0.

- **`0x48cce4`.** The vertex list is `[0x6c68f2] + 2 * [0x6c68e6]`,
  `[0x6c68e4]` vertices; the points are the dwords at `[0x6c68ec]`, 12
  bytes each, with no transform.
  - **Divisor path** (`[0x6c6028]` = 0). A point in state 0 is projected:
    x = `(x << 8) / [0x6c6030] + [0x6c6034]`, y = `-(z << 8) / [0x6c6030] +
    [0x6c6038]`, into the cache `0x6c9bc2` and the output `0x6cbb02`. Its
    state byte is **not** set. A point in state 2 is copied from the cache;
    other states leave the output entry. The result is 0.
  - **Shift path.** A point in state 0 whose depth is below `[0x6c602c]`
    (signed) gets state 1 and makes the result 1. Otherwise it gets state 2
    and is projected with `x << shift` and `-(z << shift)` over the depth.
    A point in state 1 makes the result 1; one in state 2 is copied.
  - **End.** Result 1: every vertex's point is copied to the view points
    `0x6c6ce2` (dwords +8, +0, +4 in that order). Result 0: the output
    count `0x6cbc02` becomes the vertex count. EAX = ECX = the result.
    EDX is the vertex count with a shift. Without a shift EDX is an
    uninitialised local of the original; it is not part of the contract
    and not modelled.
  - A zero divisor faults in the original (`#DE`); the own code refuses,
    and es2sim ends the process with the original's code.
- **`0x430518(a, n, R)`.** For each record `e = [0x49b034] + 8(a + i)`,
  `i < n`: the words `[R]`, `[R] + 1`, ... are written from the pointer
  `[e]` on, `(int16) [R + 2]` of them (the length is read again on every
  step), then `[e + 4]` = `[R + 2]`. Then the copy `0x486e10`. EDX = n.
  The counters are words compared signed, so a count of 0x8000 or more
  never ends and a negative `n` makes the copy's `LOOP` run until it
  faults; the own code refuses both.
- **`0x430590(a, n, R)`.** Like `0x430518`, but record `i` has its own
  pairs: `R_i = R + 6i` holds a count word and a list pointer, and the
  pairs start at `[[R_i + 2] + 4i]` (indexed by `i` twice; every caller
  passes n = 1). The pairs follow each other (start word, length word).
  One running destination pointer is used for all pairs of a record, and
  `[e + 4]` gets the sum of the lengths.
- **`0x42e878(obj, level)`.** The kind is `(int16) [obj + 4]`. Kind 0
  uses the thresholds `[0x4cfd04]` and the count `[0x4cfcda]`; the others
  `[0x4cfd00]` and `[0x4cfcd8]`. Kind 5 first calls the object's vtable
  slot `+0x10` and adds `[0x4cfce8]` (AX of that call below 5000) or
  `[0x4cfcec]` (5000 or more, signed) to the level; a negative sum becomes
  0. The result is the first `i < (int16)(count - 1)` whose threshold is
  above the level (unsigned), else `count - 1`. EAX the index, EDX the
  threshold's address, ECX `(ECX & 0xffff0000 | count) - 1`, where ECX is
  the object after kind 5's call.
- **`0x42ea0c`.** `0x430518(2i, 1, R)` for `i = 0..6`, then
  `0x430518(0x18, 1, R)`, with the stack record `R = {0x10, 0x10}`. EDX is
  R's dword `0x00100010`.
- **`0x42e8e8(obj, level, flag)`.**
  - With the byte `[0x49aef2]` set it is `0x42ea0c`.
  - Otherwise the kind entry is `e = [0x4cfce0] + 5 * kind`: a count byte
    and a pointer to `{range, row}` byte pairs. A zero count returns at
    once (EAX = the object's high word with the kind word, EDX the kind
    sign-extended, ECX unchanged).
  - Otherwise a negative level counts as 0, `k = 0x42e878(obj, level)`,
    `P = [0x4cfcf8] + 4k`, `Q = [0x4cfcfc] + 4k`. For each byte pair the
    run pair is `A = P + 4 * row * (int16) [0x4cfcde]`.
  - **Flag low byte 0:** `0x430518(range, 1, A)`.
  - **Otherwise:** `0x430590(range, 1, D)` with a stack record `D = {2,
    list}`, `list = {&p1, &p2}`. `p1` is A's pair shortened by `q + 1`
    (`q` = the length >> 2, signed), `p2` is Q's pair moved to its last
    `q + 1` words.
  - **Original bug, kept.** `0x430590` takes the pairs from `[list + 0]`
    on, so its second pair is the list's own first dword, `&p1`: the low
    word of that stack address is the start and the high word the length.
    `p2` is never read. The words written therefore depend on the stack
    address. The own code builds the same frame (EBP = the arguments - 8,
    the locals below it), so it writes the same words when its call area
    has the same address. The path is unreachable in the game
    ([own_process.md](../reference-game/own_process.md) section 5): both
    callers pass a flag with a clear low byte. `0x46bd6e` pushes 0.
    `0x4287b3` pushes the byte `0x4cf984`, which `0x428d0a` copies from
    byte `0x4d24c5` (view block B+9). That byte is cleared by the globals
    constructor's memset (`0x45caf4`); a listing scan found no code that
    writes it (direct addresses, base registers loaded with `0x4d24bc` at
    offsets 6..9, the six callees that take B, and the one DATA pointer to
    B, the destructor record at `0x49ef3c`).
  - EAX the count byte; EDX and ECX the last call's (P and Q without
    pairs).

### Shape draw evidence

In vitro `//tools/reference-game/invitro:differential_shape_draw_test`
(`shape_draw_cases.h`, ops `draw`, `polygons`, `leaves`, `textures`).
Each case runs once in the original's machine code (the fill layer, the
haze table, the shade lookup, the leaf, the 3D fills `0x46817c`/`0x468760`
and the blit `0x48185c` patched to recording stubs) and once through the
own rows. It compares EAX/EDX/ECX (EAX of `0x48d708` included), every DATA
write, the world and the stubs' call log (fill kind, count, colours, mode,
points, intensities, the `+0x22c` mode record and its UV; leaf draws with
view and clip state). `draw` covers random part trees of all kinds,
instances, actors with detail tables, lights and degenerate BSP nodes
(d == 0); `textures` a texture world (shape record, bitmap table, texture
records, dword points, BSP group nodes) over all rows plus the list part
`0x476808`. The stack-only values are normalised: the pointers above, the
padding bytes, and EAX values that are each side's own stack addresses.
Whether the `+0x22c` record lies in a stack is decided by value, so the
generated `+0x22c` word keeps bit 31 set (invitro/README.md,
"shape_draw, actor_draw and gl_gouraud: stack values"). For a textured
polygon's fill the stub does not log the intensities behind `0x6c6060`:
the fill reads its mode from the `+0x22c` record (7), and a mode 1 left in
`0x6c6054` by an earlier Gouraud polygon points at that polygon's dead
stack locals, which differ between the two sides. `vector_new` runs as the
original's code (`sc_original`, 5 slots).

In vitro `//tools/reference-game/invitro:differential_shade_ranges_test`
(`shade_ranges_cases.h`, ops `runs`, `spans`, `level`, `default`,
`select`, `project`): each case builds a synthetic world (object and
vtable, kind table, byte pairs, thresholds, run pairs, range records,
`0x430590` records, dword points, index list) and a destination buffer,
presets the DATA words and calls one function on both sides; the vtable
slot is a recording stand-in on both sides. Because of the stack-address
bug, both sides run with the arguments at the same address: side 0
through a trampoline that switches ESP into a buffer of the harness,
side 1 with the call area's top at that address; the address varies per
case and depends on the process layout, so the stack-dependent pair's
length differs between runs while both sides always share it. Compared:
DATA (with the range table `0x4a4fa5`, the cache and the output polygon),
the world with the stand-in's log, the destination buffer, and
EAX/EDX/ECX (`0x48cce4`'s EDX only with a shift).

Unit `//sim:shape_draw_test`: the group, detail, kind and level paths on
a synthetic image, the refusals (`#DE` at `0x4762f7`, `0x47691c`,
`0x476a25`, the 33-vertex Gouraud, the NULL actor instance at
`0x40341d`), and the block "ranges" (`0x430518` with its refusal,
`0x42e8e8` flag 0 and the empty entry, `0x48cce4`'s divisor path and its
`#DE` refusal). `//sim:scene_test` expects a row at `0x42e8e8`.

## Actor draw slots and explosion binning

The draw slots of beams, mechs, flyers, debris and meteors, the
configuration parts and the two explosion tests of the scene binning.
Own code in `sim/src/shape_draw.c`, service rows in
`sim/dispatch/services/actor_draw.h`. None of the nine VAs is in
`tools/reference-game/proxy/hook_registry.h`. Confirmed in vitro; not
checked live.

| VA | Row | Listing | Called from |
|---|---|---|---|
| `0x40bc14` | `shape_beam_draw` | `0x40bc14..0x40bf71` | the beam's draw slot |
| `0x4174c8` | `shape_mech_draw` | `0x4174c8..0x41752f` | the mech's draw slot |
| `0x4215cc` | `shape_flyer_draw` | `0x4215cc..0x4215f0` | the flyer's draw slot |
| `0x408e6c` | `shape_debris_draw` | `0x408e6c..0x408e98` | the debris draw slot |
| `0x409cd0` | `shape_meteor_draw` | `0x409cd0..0x409d29` | the meteor's draw slot |
| `0x40316c` | `shape_config_part` | `0x40316c..0x4031d3` | configuration parts, `0x40ded8` |
| `0x40ded8` | `shape_config_part_offset` | `0x40ded8..0x40defc` | a draw slot |
| `0x408228` | `explosion_parent_cells` | `0x408228..0x40823f` | `0x42841c` (scene binning, at `0x42866c`) |
| `0x408240` | `explosion_hidden` | `0x408240..0x408282` | `0x42841c` (at `0x428661`) |

Leaf callees done inline: the detail offset setter `0x4769b8` is a direct
store to `0x4a1020` (as in `0x4034f4`); the empty `0x476224`
(push/mov/pop/ret) is not repeated; `0x40bc14`'s `memset` of the point
states is done as direct byte stores. Everything else goes through the
dispatch: the mech's part callee `0x4030d0`, the hooks
dist_octagonal `0x47de4c` and camera_attached `0x401078`, the 2D fill
`0x468414`, the view rows, `vector_new` and every part slot (vtable
`+0x1c`). `0x4030d0` is the registry hook `part_shapes_bind`
([vtables.md](vtables.md#simulation-leaf-hooks-confirmed-against-the-listing-and-in-vitro)):
es2sim registers every registry hook as a hook entry of its dispatch
(`sim/src/adapters.c`), and that entry runs core's
`es2_part_shapes_bind_registers`. It needs no service row, and the call
does not refuse. ECX after it is the entry ECX, where the original leaves
its pool cursor; `0x4174c8` does not read ECX after `0x417510`.

### The actor draw functions

- **`0x40bc14(beam)`**, a beam. Its frame is `0xcc` bytes; EBP is the
  frame + `0xcc`.
  - Setup:
    - The gc's `+0x22c` is saved, then pointed at a stack record
      {0, the kind's record word}.
    - `0x48c444(0)`.
    - The beam's dword points `[beam+0x58]` and its count word `+0x56`
      become the render context's points and count.
    - The states for that count are cleared.
    - The index list is `0x4a9712`. The listing's test at `0x40bc85` is on
      a constant, so it is always taken.
  - Kind table `[0x4a9804]`: 6 bytes per kind `+0x52` (width word, gc
    record word, record index word). Records `[0x4a9808]`: 20 bytes each
    (four dwords, then the page word at `+0x10`).
  - Iterations run while `i` is below the signed count byte `+0x54`, which is
    re-read each iteration. In each iteration:
    1. Kinds 1 and 7 first draw a 4-vertex polygon. Its first index word
       is `(uint16)(i << ((3 - shift byte +0x55) & 31))`. The calls are
       `0x48ca70`, then `0x48cf20` when that returns nonzero, then
       `0x48d5c0`.
    2. Two `vector_new` locals are made.
    3. The segment between the first two points is view-transformed
       (`0x48c57c`) and clipped (`0x40bb4c`). The iteration ends when AL is 0.
    4. Both ends are projected and put on screen.
    5. The end widths are `0x48c5cc` of the kind's width and each end's
       depth, each raised to at least 2.
    6. The screen delta (dy, -dx) is divided, each component `<< 11`, by
       its dist_octagonal. The iteration ends when that length is 0.
    7. The four corner offsets are `(n * w + 0x400) >> 11`, signed.
    8. The quad is four 0x1c-byte points at EBP - `0xcc`:
       - x and y come from the ends and the offsets;
       - `+0xc` and `+0x10` are the record's dwords `<< 8`;
       - `+8`, `+0x14` and `+0x18` are never written. They are
         uninitialised stack in the original and zeroed call area in ours.
    9. `0x468414(4, quad, 0, page, 0, 0)`.
  - End: the gc pointer is re-read and its `+0x22c` restored.
  - Registers: EAX the saved `+0x22c`, ECX the gc, EDX the sign-extended
    count byte.
- **`0x4174c8(obj)`**, a mech.
  - While the word `0x4a9b82` is 0 it does nothing; EAX and ECX are as on
    entry.
  - Otherwise cfg = `[obj+0x1f2]`, kept in a local. It walks the 6-byte
    entries from `[cfg+0xe6]` while the index DI is below the signed word
    `[cfg+0xea]`, re-read through the local at every test. For each entry
    e it calls `0x4030d0(e, [obj+0x202] + 4, [obj+0x1f2] + 0xec)`; both
    arguments are read again from obj for every call, after the previous
    call's stores.
  - Then `0x4033e4(obj)`.
  - Registers: EAX `0x4033e4`'s, ECX cfg.
- **`0x4215cc(obj)`**, a flyer: `0x4a1020 = [0x4a9dc4]`, then
  `0x4033e4(obj)`, then `0x4a1020 = 0`. EAX = ECX = 0.
- **`0x408e6c(obj)`**, debris: `0x4a1020 = [obj+0x50]`, then
  `0x401fe4(obj)`, then the draw slot of `[obj+0x34]`, then
  `0x4a1020 = 0`. EAX = ECX = 0.
- **`0x409cd0(obj)`**, a meteor.
  - The matrix is built exactly as `0x401fe4` does it. The original
    inlines that code when the byte `+0x4b` is set.
  - Then it calls the draw slot of `[obj+0x41]` when the byte `+0x4b` is
    set, else of `[obj+0x34]`.
  - Registers: EAX the slot's, ECX the part.
- **`0x40316c(part)`**, a configuration part.
  - Without a shape `[part+0x10]`, EAX is 0.
  - Otherwise, with a frame of `0x24`:
    1. The cell words `0x6b7b6c` and the shape record `0x6c6064` are
       saved.
    2. The record becomes `[shape+0x26]`.
    3. `0x476120(part, frame)`.
    4. The cells become `[part+0x14]`.
    5. `0x476808([part+0x10])`.
    6. Both saved values are restored.
  - Registers: EAX the saved record, ECX the shape.
- **`0x40ded8(part)`**: `0x4a1020 = [0x4a9868]`, then `0x40316c(part)`,
  then `0x4a1020 = 0`. EAX = ECX = 0.

### The explosion tests

- **`0x408228(e)`**: the parent `[e+0x57]` + `0x1e8`, or 0 without a
  parent. EDX and ECX are as on entry.
- **`0x408240(e)`**: whether the scene binning skips an explosion.
  - EAX is 0 for the type bytes `+0x41` = 2 and `0xb..0xe`, and when there
    is no parent.
  - Otherwise it calls camera_attached(`[0x4d24ea]`, parent); EAX is 1
    when that call's AX is nonzero.
  - After the call EDX holds camera_attached's result, which it also
    returns in EDX. Without the call, EDX is as on entry.
  - The caller at `0x428661` skips the explosion when EAX is nonzero
    (`jne 0x428689`). The types 2 and `0xb..0xe` give EAX 0, so they are
    **never** skipped; the only explosion skipped is one whose parent the
    camera is attached to (mode word `+0x36` = 2, `+0x32` = the parent).
    [terrain.md](terrain.md#object-binning-0x42841c)'s binning table
    states the same.

### Actor draw evidence

In vitro `//tools/reference-game/invitro:differential_actor_draw_test`
(`actor_draw_cases.h`, built on `shape_draw_cases.h`'s world and fill
stand-ins; parts beam, actors, config, explosion). Every op runs on both
sides with random entry registers. Compared: DATA; the world, including
the stand-ins' log; EAX, EDX and ECX where the contract above defines
them. An EAX that is a stack address on both sides counts as equal.
Stand-ins, on both sides: `0x468414`
logs the quad's x, y, `+0xc` and `+0x10`, its other arguments, and the
gc's `+0x22c` record; the leaf part slot logs the part offset, the cell
words, the shape record, the current transform and the view matrix.
dist_octagonal and camera_attached run as the original's machine code on
side 1 too. `0x4030d0` runs for real: the original's machine code on side
0; on side 1 `es2_part_shapes_bind_registers`, as es2sim's hook entry
runs it (EAX and EDX from it, ECX the entry ECX). The mech's lists are
built for it: per mount a list A of 0 to 4 slots, the instance's list B
of 0 to 4 entries, arrays missing, zero slots and zero entries, more A
slots than B entries (B's count wraps and the walk reads on past B's
array), and records that overlap each other and the pool. `0x4030d0` is
also called directly, with EAX and EDX compared. One run's coverage:
2,311 calls (840 direct); 1,569 slots bound to a B record, 693 to a pool
record, 710 zero slots, 899 slots that got no B entry, 170 A lists and 155 B
lists without an array. Two mutants fail the test: EDX off by one bit,
and both source words read before the two stores. `sd_part` lets the BSP
parts of a world share node records, so a node written for a part with
more children can link a child index past another part's array. The
actor and config parts therefore reduce every child link in the node
records from a BSP part's first node on below that part's child count
(`r8_bsp_links`); otherwise the original calls through the zero dword at
`0x476bd5`. `scene_cases.h` and
`sim/tests/scene_test.c` list `0x408228` and `0x408240` as replaced (they
have rows).

The replays reach the call. In `//sim:replay_c1_01-free-camera_test`
(identical to its golden over 1000 ticks), es2sim calls `0x4030d0` 14,875
times and `0x4174c8` 2,125 times, with no refusal. The counts are from the
`calls` list of the tick log's exit record; es2-data
`logs\a1-heap-trace-20261023\es2sim\c1_01-free-camera\tick-log.jsonl`
shows the same counts. The mission constant block `0x415464` sets the
word `0x4a9b82` ([mission_load.md](mission_load.md)), so every mission
with a drawn mech makes these calls.

## Graphics objects and surfaces

The graphics library's device objects, graphics contexts, surfaces and
bitmaps in the listing ranges `0x480738..0x480ffc` and
`0x481028..0x481f3d`. Own code in `sim/src/gfx.c`, 35 service rows in
`sim/dispatch/services/gfx_objects.h`. The four surface rows `0x480f58`,
`0x481028`, `0x4811e8` and `0x481224` call `frame_buffer.c`'s pieces, so
the code exists only once. Confirmed in vitro; not checked live.

### Object and surface functions

| VA | Row | What |
|---|---|---|
| `0x480738` | `gl_device_rect_get` | Copies the device's dwords `+0xe..+0x1a` into `out[0..3]`. EAX `out+8`. |
| `0x480764` | `gl_device_rect_set` | Writes the words `in+0x10..+0x14` to `+0x1e..+0x22`, then the dwords `in+0..+0xc` to `+0xe..+0x1a`. EAX `obj+0xe`. |
| `0x480848` | `gl_handler_add` | The first free entry of the 10-entry table `0x6c5ed8` (12 bytes each) gets `{1, fn, mask}`. EAX is its index. In a full table, AX is `0xffff` over the walk's end pointer, so EAX is `0x006cffff`. |
| `0x480880` | `gl_handler_remove` | `[0x6c5ed8 + 12*(arg2 & 0xffff)] = 0`. |
| `0x480ae4`, `0x480bba` | `gl_device_slot2`, `gl_device_slot0` | EAX is `[0x6c5ebc]` and `[0x6c5eb4]`. |
| `0x480b38` | `gl_device_destroy` | The device destructor. It decrements the object counter `[[0x6cbee4]]`. For a non-NULL object it clears device slot 0 `[0x6c5eb4]` when that is set (`0x480bba`, `0x480ba9` inlined), then calls `delete` when flags bit 0 is set. A NULL object leaves the counter pointer in EAX. |
| `0x480b78` | `gl_device_word` | Writes word `+4 = arg2`. EAX is `[obj] & 1`. |
| `0x480ba9` | `gl_device_clear` | `[0x6c5eb4 + 4*arg2] = 0`; EAX is `arg2`. |
| `0x480c34` | `gl_gc_construct` | The graphics-context constructor: vtable stores, the element constructors `0x48de2f`/`0x49545b`, and the current surface's first clip record copied (0x81 dwords, then the tail fields in the listing's order). Refused when the current surface or its `+0x40` is NULL; the original faults there. |
| `0x480dcf` | `gl_gc_destroy` | `0x4838fd(gc, 0)`, then `delete` when flags bit 0 is set. |
| `0x480ec4` | `gl_surface_clip` | Reads `[s+0x40]`. |
| `0x480ecf` | `gl_surface_set_clip` | `+0x44 = clip`. For the current surface, `0x489558` (inlined) also sets `[0x4a3614] = clip`. EAX is `clip` for the current surface, otherwise the current surface. |
| `0x480ef4` | `gl_register` | Runs `0x47a0b8(0x4a2eb8, [0x4a2f24])` (inlined). A nonzero count `c` is stored as entry `i = [0x4a1690]` of the pair list at `0x6c5834`: count at `+4`, table at `+0`. Then `[0x4a1690]` is incremented. EAX is 0 for a zero count, else `i`. |
| `0x480f08` | `gl_minus_one` | EAX is -1. |
| `0x480f38` | `gl_surface_release` | The atexit function: releases the current surface when it is attached. EAX is 0, or the surface's flag word when bit 0 is clear. |
| `0x480f58` | `gl_surface_construct` | `frame_buffer.c`'s `es2_sim_gl_surface_construct`. |
| `0x480ffc` | `gl_surface_delete` | `0x4811e8`, then `delete` when flags bit 0 is set. |
| `0x481028` | `gl_surface_create` | `frame_buffer.c`'s attach (`0x489416` inlined: driver slots 8, 9, 10, 11 and 7). |
| `0x4811e8` | `gl_surface_destroy` | `frame_buffer.c`'s destroy. |
| `0x481224` | `gl_surface_select` | `frame_buffer.c`'s `es2_sim_gl_surface_select`. |
| `0x4812f0` | `gl_surface_format` | The format code of a name. The codes `{1, 0, 3, 4}` are copied from `0x4a2e94` and the names are at `0x4a2e88`. The walk stops at code 4 or at the first equal name. Refused when the four copied codes hold no 4 (the original would walk past its copy); the image has one. |
| `0x4813f8` | `gl_surface_set` | All cases of the byte table `0x481418`; detail below. |
| `0x48151d`, `0x481526` | `gl_surface_current`, `gl_object_flags` | EAX is `[0x6c5ed0]` and `[obj]`. |
| `0x48154c` | `gl_surface_unselect` | `0x4805b4(7, 0)`: `[0x6c5ed0] = 0`, EAX 0. |
| `0x481559` | `gl_surface_claim` | Runs `0x4805b4(7, s)` (inlined). When `s` and `[0x6c5ed0]` are both nonzero, EAX is 1 and nothing changes. Otherwise `[0x6c5ed0] = s` and EAX is 0. |
| `0x48156c` | `gl_bitmap_construct` | The bitmap constructor. EAX is `b`. |
| `0x481bf1` | `gl_stream_destroy` | `0x47a8a4(st, 0)`, then `delete` when flags bit 0 is set. |
| `0x481c91`, `0x481cba` | `gl_bitmap_blit_origin`, `gl_bitmap_blit_at` | `0x4816a8(b, point, 0)`. The origin form builds a local point with `0x48d9a7(&p, 0, 0)`. |
| `0x481d5c`, `0x481d74` | `gl_bitmap_size_d`, `gl_bitmap_size_1a` | EAX is `sx16([b+0x14])*4 + [b+0xa]`, plus `0xd` or `0x1a`. |
| `0x481e98` | `gl_bitmap_write` | Writes the bitmap to a stream; detail below. EAX is the last slot call's. |
| `0x481f3d` | `gl_stream_read_byte` | Calls stream slot 6 for one byte. |

`0x4813f8(s, which, value)`, by `which`:

- 2: driver slot 7 `(value, s+4)`. When `s` is the current surface, it then
  writes the origin pair `0x4a3676`/`0x4a367a` from `s+0x28`/`s+0x2c`.
- 9, 10, 11: writes the field `+0x1c`/`+0x20`/`+0x24`. For the current
  surface it also writes `0x4a366a`/`0x4a366e`/`0x4a3672`.
- 16: writes `[0x4a368e]`.
- 17: calls driver slot 32. When slot 32 is BMP_8's empty body `0x4898f6`,
  that call is skipped.
- 2, 9, 10, 11, 16 and 17 return EAX 1. Every other value, and any value
  above `0x11`, returns EAX 0.

`frame_buffer.c`'s partial `es2_sim_gl_surface_set` refuses cases 2 and
17; it stays for `hud_load.c`'s `which == 9` call, where the two behave
the same.

`0x481e98`, the write sequence (`0x481f54`, a stream slot call with one
byte, is inlined):

1. word `+4`
2. word `+6`
3. byte `+8`
4. byte `(b9 & 0xf) | (b9 & 0x10)`
5. byte `+0x12`
6. `0x482f0a(st, +0xa)`
7. word `+0x14`
8. `0x482bf0(st, +0xa, +0xe)`
9. stream slot 5 `(st, sx16(+0x14)*4, +0x16)`

`0x481224`'s kind-1 frame path (`present.c`) and `hud_load.c` call
`es2_sim_gl_surface_select`, the same function the `gl_surface_select`
row calls.

### The `0x4894a5` register quirk

The destroy path's `0x4894a5` pushes one slot and removes two (`add esp, 8`
after one push). After a destroy through `0x4811e8`, `0x480ffc` or
`0x480f38` (when the surface is attached), the caller is left with ESI = 0
and EDI = the caller's ESI; EBX survives. The in-vitro suite calls these
three through a thunk that keeps the harness's registers and checks
exactly this: ESI 0, EDI equal to the entry ESI, EBX unchanged, and
untouched ESI/EDI on the other paths. The own code does not reproduce it:
no own caller relies on these registers, and in the original's callers
ESI/EDI are dead after the call.

### Verdicts for the object ranges

| Verdict | VAs |
|---|---|
| Rows | The 35 VAs in the table above. |
| Dead (no caller, no table entry, no reference in the image) | `0x4807a8`, `0x4807d4`, `0x480824`, `0x48082c`, `0x480b90`, `0x480bd0`, `0x480fa4`, `0x481614`, `0x481638`, `0x481678`, `0x481734` |
| Called only from dead code | `0x481c1e` (from `0x481734`) |
| Unreachable by data | `0x481910`, the decoder of compressed GLBitmaps (no CD record is compressed) |
| Own code without a row | `0x480ad8` (`init.c`) |
| Inlined in platform code | `0x480898`, `0x4808dc` |
| Labels inside other functions | `0x4808cf`, `0x480909`, `0x480c91`, `0x480c93`, `0x4810ca`, `0x481129` |
| Inlined surface-layer pieces ([Cursor point and textured slot 33](#cursor-point-and-textured-slot-33)) | `0x489558`, `0x4893aa`, `0x47a0b8`, `0x4805b4`, and `0x489416` with its slot calls. |
| Inlined, outside the ranges | `0x481f54` (in `0x481e98`) |

BMP_8's empty slot `0x4898f6` has no row. It lies in the range
`0x489390..0x48993a`, and `0x4813f8` skips it.

Roots of `sim_gap.md` closed by these rows: behind `0x480c34`,
`0x480ec4`, `0x480ef4`, `0x480f58`, `0x481028`, `0x481224`, `0x4812f0`,
`0x4813f8`, `0x48151d`, `0x48154c`, `0x481559`; virtual `0x480dcf`,
`0x480f08`, `0x481c91`, `0x481cba`, `0x481d5c`, `0x481d74`, `0x481e98`;
referenced only `0x480f38`.

### Object evidence

In vitro `//tools/reference-game/invitro:differential_gfx_objects_test`.
Both sides start from the pristine DATA and an empty heap and build the
same synthetic world: four devices and five surfaces (formats 5, 5, 3, 9,
5; attached or detached; with clip records), three graphics contexts,
three bitmaps and two streams, random device slots and a random handler
table. Format 5 is a synthetic driver table of recording stubs, entered
as the first pair at `0x4a5840`; format 3 is the image's BMP_8 table; the
current table is a copy of one of the two. 1 to 10 random rows run per
case, with arguments chosen on the original side from the world's state.
Other callees are recording stubs on both sides: `0x4838fd`, `0x47a8a4`,
`0x4816a8`, `0x492447`, `0x49085d`, `0x482f0a` and `0x482bf0`. Compared:
DATA and the heap; EAX (masked after a delete, and where the entry EAX
stays); the stub log; the calloc/free/atexit/strncpy/memset log; the
atexit table. The path counters cover a full handler table, NULL/keep/
delete for each destructor, set-clip on the current surface and on
another, register with count 0 and nonzero, release of none/detached/
attached, select of format 5/BMP_8/none, all four format codes, each
`0x4813f8` case including slot 32's empty body, claim null/set/taken,
negative and positive sizes, the write with high bits in `+9`, and driver
slot 8 and 9 failures.

## Fill-layer object slots and exit registers

Rows in `sim/src/fill.c`, fragment `sim/dispatch/services/fill_objects.h`. They
cover:

- **The exit registers of the fill helpers.** The clipper `0x484338`, the
  span builder `0x484720`, the line's clip `0x483c6c` and its span list
  `0x483d6c` (see [GL fill layer and line](#gl-fill-layer-and-line)) get
  rows for a direct call. Each row sets EAX, EDX and ECX as the original
  leaves them. The fill functions themselves are unchanged; a shadow reads
  the same inputs first and supplies EDX and ECX.
- **The small functions of `0x4820b7..0x482f94` and
  `0x48358c..0x483655`**: the slots of the palette, font and bitmap-array
  objects (vtables `0x4a30f8`, `0x4a31ac`, `0x4a32ec`), the stream and
  draw thunks, and the holder (vtable `0x4a3340`).

Confirmed in vitro; not checked live. "The callee's" below means the
registers the last call returned.

### Exit registers of the fill helpers

- **`0x484338(n, points, out, rect)`**
  - n <= 1: EAX = n; EDX and ECX keep their entry values.
  - Otherwise:
    - EDX is the last `idiv`'s remainder, or the entry EDX when no edge
      was cut.
    - ECX keeps the entry high word. Its low word is the last outcode in
      CL and CH (`mov cl, ch` after every step). When the x pass emits no
      point, that is the x outcode of point n-1 (EAX is then 0).
      Otherwise it is the y outcode of the x pass's last point.
- **`0x484720(n, points, out)`**
  - A polygon of one row: ECX = 0 (the `loop`s) and EDX = ymax.
  - Otherwise EDX and ECX come from the left chain's last edge (from the
    bottom forward to the first point at ymin; always two points or more):

    | Last edge | ECX | EDX |
    |---|---|---|
    | vertical, diagonal | 2 (the block's stride) | the computed jump `0x4866f0 - 8 * rows` |
    | y-major | 0 | the error step of its last row (inc when e >= 0, else 2dx) |
    | x-major | 0 | the stride 2, or `0xfffffffa` for an upward edge after the swap to the smaller x |

  - The horizontal branch (ECX = start y, EDX = 0) cannot be the last left
    edge, because that edge ends at ymin from a point below it. The branch
    is kept but never exercised.
- **`0x483c6c(a, b, rect)`**: ECX is the final a.y on both exits. EDX is
  the last remainder, or the entry EDX.
- **`0x483d6c(a, b, out)`**: all three registers are defined (after the
  swap to the smaller x):

  | Case | EAX | ECX | EDX |
  |---|---|---|---|
  | a point | ax | ay | by |
  | vertical | the words bx:ax | 0 (`rep stosd`) | the sign of by - ay (`cdq`) |
  | horizontal | bx | ay | by |
  | diagonal | ax + dy + 1 | 0 | dy |
  | x-major, y-major | the walk's final x (x - 1 on the x-major early exit) | 0 | the final error |

- The row `0x483a04` (`fill_line`) keeps EDX and ECX from entry; the
  original's EDX and ECX after the line are not modelled.
- `0x484338` counts as a "called" root in `sim_gap.md` only because its
  VA is passed to `refuse`/`cut`.

### The palette `0x4a30f8`

- **`0x4820b7(a0..a4)`**: calls `0x486e10(a1..a4)`. The registers are the
  callee's.
- **`0x482154(P)` and `0x482190(P)`, slots 1 and 2**: the stream size.
  - It starts at `sx[P+4] * 4 + 8` (or `+ 0x14` for `0x482190`).
  - It adds `(sx[E+4] + 1) * 2` for each of the `sx[P+0xc]` 8-byte entries
    E at `[P+0x10]`. The index is compared as a zero-extended word against
    the re-read count.
  - EAX = the size, EDX = the entries counted, ECX = P.
- **`0x4822cc(P, S)`**: the write, in this order:
  1. the words P+4 and P+6, through `0x492447`;
  2. `sx[P+4]` dwords from `[P+8]`, through stream slot +0x14;
  3. the words P+0xc and P+0xe;
  4. per entry, its word +4 and `sx(word)` words from `[E]` (a signed
     word compare).

  The pushed words' upper halves are the registers the previous call left
  (`mov r16; push r32`). EAX and EDX are the callee's. ECX is the dword the
  prologue's `push ecx` made: the entry ECX, with its high word replaced by
  the last entry word (the local `[ebp-2]`), popped at the exit.
- **`0x4823da(P)`**:
  - `[P]` = `0x4a69f0`, then `0x4a3368`; the counter + 1; `[P]` =
    `0x4a30f8`;
  - the words P+4 and P+0xc = 0; the counter + 1.
  - EAX = P. EDX and ECX are the counter pointer (read twice).

### The font `0x4a31ac`

- **`0x482410(F, flags)`**: the counter - 1 and EAX = the counter pointer.
  F == 0 stops there. Otherwise:
  1. `[F]` = `0x4a31ac`.
  2. `0x47758c` deletes these arrays (`pop ecx` sets ECX to the argument):
     - `[F+0x1e]` if the word F+0x18 is set;
     - `[F+0x22]` if the dword F+0x1a is set;
     - `[F+0x26]` if the word F+4 is set;
     - `[F+0x2a]` if the word F+0xc is 0xffff;
     - `[F+0x2e]` if the word F+8 is 0xffff.
  3. `0x4838fd(F, 0)`.
  4. If flags & 1: `0x477548(F)`, with ECX = F.
- **`0x482528(F)`**: EAX = `sx[F+0xa]`. EDX and ECX keep their entry
  values.
- **`0x4827a1(a0..a3)`**: calls `[0x4a57e0](a1, a2, a3)`.
- **`0x48287c(F)` and `0x4828b8(F)`**: the size.
  - Let c = `sx[F+4] * 4`. The size is
    `sx[F+0x18] * 4 + [F+0x1a] + c + 0x1a` (or `+ 0x32` for `0x4828b8`).
  - Add c when the word F+0xc is 0xffff, and `sx[F+6] * 4` when the word
    F+8 is.
  - EAX = EDX = the size, ECX = c.
- **`0x4828f4(F, S)`**: the write, in this order:
  1. the eleven words F+4..F+0x18 (the upper halves from EAX, EDX and ECX
     in turn);
  2. `0x482f0a(S, [F+0x1a])`;
  3. `0x48f3ce(S, sx[F+0x18] * 4, [F+0x1e])`;
  4. `0x482bf0(S, [F+0x1a], [F+0x22])`;
  5. `0x482c6e(S, sx[F+4], [F+0x26])`;
  6. if the word F+0xc is 0xffff, `0x482bf0(S, sx[F+4], [F+0x2a])`;
  7. if the word F+8 is 0xffff, `0x491156(S, sx[F+6], [F+0x2e])`.

  The own row calls the stream-thunk rows through the dispatch.

### Stream and draw thunks

The stream thunks' registers are the slot's.

| Thunk | Call |
|---|---|
| `0x482bd7(S, a, b)` | S slot +0x18 (read) with (S, a, b) |
| `0x482bf0(S, a, b)` | S slot +0x14 (write) with (S, a, b) |
| `0x482c6e(S, a, b)` | S slot +0x14 with (S, a * 4, b) |
| `0x482c8a(S, a, b)` | S slot +0x18 with (S, a * 4, b) |
| `0x482f0a(S, v)` | writes its own argument slot: (S, 4, &v) |
| `0x482f21(S, p)` | reads 4 bytes into p |

- **`0x482cec(O)`** (`gl_object_rect_fill`): calls `0x48d99e(O+4)` (its
  argument is unused), then `0x482d68(result, O+4)`, which is the rect
  fill `0x486704(O+4)`. EAX and EDX are the fill's; ECX = O+4.
- **`0x482d44(a, p0, p1)`**: calls `0x483a04(p0, p1)`. Its only caller is
  the underline in `0x482622`, gated by `[GC+0x230]`, which is always 0 in
  play; the own code calls it as the original does.
- **`0x482d59(a, p)`, `0x482d68(a, p)` and `0x482d77(a, p)`**: call
  `0x4895ec(p)`, `0x486704(p)` and `0x48817c(p)`. ECX = p.

### The bitmap array `0x4a32ec`

- **`0x482e08(A)`**:
  - The sum is 4 plus `0x47a55c(e)` for each element e of `[A+8]` while
    i < `[A+4]` (signed, re-read). A zero result ends it with EAX = 0.
  - ECX is the sum so far: the prologue's `push ecx` local, popped at the
    exit.
  - EDX is the callee's (the entry EDX without an element).
- **`0x482e48(A)`**: `[A+4] * 4 + 0xc` plus each element's slot +8 result.
  ECX = the last element; EDX is the callee's.
- **`0x482ed0(A, S)`**: `0x482f0a(S, [A+4])`, then `0x47a1ac(e, S, 0)` for
  each element.
- **`0x482f38(A)`**: `[A]` = `0x4a69f0`, then `0x4a32ec`; `[A+4]` =
  `[A+8]` = 0; the counter + 1. EAX = A, EDX = ECX = 0.
- **`0x482f80(A, b, c)`**: `[A+4]` = b, `[A+8]` = c. EAX = A, EDX = b,
  ECX = c.

### The holder `0x4a3340`

- **`0x48358c(P)`**:
  - `[P]` = `0x4a69f0`, then `0x4a3368`; the counter + 1; `[P]` =
    `0x4a3340`;
  - `0x49545b(P+8)`; `[P+4]` = 0; the counter + 1.
  - EAX = P, EDX is the callee's, ECX = 0.
- **`0x4835c8(P, flags)`**:
  - First the frame record `0x49630c`, which reads `[[0x6cbee4]]` (the
    own row refuses when the pointer is 0). Then the counter - 1.
  - P == 0: EAX = the FS:0 head, EDX = the counter pointer.
  - Otherwise:
    1. `[P]` = `0x4a3340`.
    2. If the inner object I = `[P+4]` is set: the counter +=
       `0x496406(I, 0)`, then I's slot +0x18 with (I, 3).
    3. `0x4838fd(P, 0)`.
    4. If flags & 1: `0x477548(P)`.
  - ECX is the FS:0 head the epilogue restores on every path. The own row
    reads FS:0 on the host thread, so in the own process that value is the
    host thread's head, not a guest value.

### Without a row

- `0x482d08` and `0x482d28` have no references in the listing and are
  taken as dead.
- No own code reaches the other functions of the range, which stay
  dispatch-only: `0x4820d4`, `0x482148`, `0x4821cc`, `0x482498`,
  `0x4824ec`, `0x48251c`, `0x482534`, `0x482e7c` and the readers between
  them. `sim_gap.md` is the reference for this list.

### Fill-layer object evidence

In vitro `//tools/reference-game/invitro:differential_fill_rest_test`
(`fill_rest_cases.h`, 4 shards; parts clip, spans, line clip, line spans,
palette, font, thunks, arrays and holder). Every case runs twice on copies
of the same world: side 0 runs the original's code through `au_call` with
random entry registers, side 1 runs the row through `es2_sim_call`. The
callees outside these rows are recorders (a jump patched over the entry on
side 0, a row at the same VA on side 1): `0x486e10`, `0x492447`,
`0x48f3ce`, `0x491156`, `0x47a1ac`, `0x47a55c`, `0x49545b`, `0x496406`,
`0x477548`, `0x47758c`, `0x4838fd`, `0x48d99e`, `0x486704`, `0x48817c`,
`0x4895ec` and `0x483a04`. The stream, element and inner-object slots and
the driver slot `[0x4a57e0]` are assembly trampolines that return a
scripted EAX, EDX and ECX, so all three registers are compared in every
case. The frame record `0x49630c` runs as the original. Compared: EAX, EDX
and ECX; the recorder hash and count (a stack pointer is hashed by its
bytes); DATA (`0x4a33d4` copied across); and the world. The span cases
cover every last-left-edge kind except the unreachable horizontal one.

## Blit, clear and cursor rows

Seven rows in `sim/dispatch/services/gfx_blit.h`, bodies in `sim/src/gfx.c`:
the ramp copy, the pen point, the kind-0 bodies of the scroll and the
save (see [Save, scroll and restore](#save-scroll-and-restore)), the rect
area and the two rotation thunks. Confirmed in vitro; not checked live.

### The ramp, cursor, scroll and save rows

- **`0x486e10(first, count, src, d)`** (`..0x486e3e`, `gfx_ramps_set`).
  - Copies `count` 8-byte records from `src` into the ramp table
    `0x4a4fa5 + 8 * first`, using `lodsd`/`stosd` twice per record and
    `loop` on ECX.
  - `jecxz` skips the copy when `count` is 0.
  - `d` is loaded into EBX and never used.
  - Registers on exit:
    - EAX is the last dword copied, or the entry EAX when `count` is 0. The
      `mov ax, ds` at `0x486e1c` is overwritten by the first `lodsd`.
    - ECX is 0.
    - EDX is unchanged.
  - A copy that would leave DATA..BSS (`0x497000..0x6cc000`) is refused.
    The original writes on until it faults. Unreachable by data: the shade
    ranges pass a byte index with count 1 (`0x42e9db`, `0x42e9ed`) or the
    constants 0..12 and 0x18 (`0x42ea0c`); `palette_set 0x481fe0` (callers
    `0x43049a`, `0x461722`) passes the DPL's sign-extended first slot `+0xe`
    and ramp count `+0xc`. Leaving DATA needs a first slot ≤ −7157 or a
    negative count; every DPL has count 0 or 256 with first slot 0
    ([dpl.md](../formats/dpl.md)).
  - `load.c` and `shape_draw.c` (callers `0x430583`, `0x43062a`) inline it
    with the same behaviour. `0x4820b7` (the palette row) forwards
    `(a, n, src, d)` to it.
- **`0x486e7c(point)`** (`..0x486ecd`, `gfx_cursor_point_set`).
  - When `[0x4a3646]` is non-zero:
    - `[0x4a361c]` is read once, into EBX.
    - If that value is set, the code calls the driver's hide `[0x4a5828]`.
    - The point's two dwords go to `0x4a3620`.
    - If the value is set, the code calls the show `[0x4a5824]`.
  - When `[0x4a3646]` is 0, the two dwords go to `0x4a364a` and no call is
    made.
  - Registers on exit, without a call: EAX is the point's y, ECX the point,
    EDX the target. After the show call they are the show's registers.
  - Own callers: `hud_cockpit.c`'s `pen_set`, `dialogs.c` and `input.c`
    call the row through the dispatch.
- **`0x487bdc(record, point)`** (`..0x487d0c`, `gfx_scroll_rect`): the
  kind-0 body of `0x487e60`. It does not read the record's kind.
  1. The point is read first.
  2. When `[C+0x208]` is non-zero, the point is shifted by the clip rect's
     (l, t) minus the origin.
  3. The record's rect at `+4`, moved by the shifted point, is the
     destination.
  4. The destination goes out by the clip mode:
     - Mode 0: slot 20.
     - Mode 1: `0x48699c` against `C+0x20c`, then slot 20.
     - Any other mode: the regions, from `count - 1` down. A kind-0 region
       goes through `0x48699c` and slot 20. A kind-2 region goes through
       `0x486d15(rect, [e+4], A)` and slot 19. Other kinds are skipped.
       The loop runs `jecxz`, `dec`, then `dec [ebp-4]`/`jge`: it goes on
       while the previous index was at least 1 (signed), and C is re-read
       for each region.
  5. The slots get the unshifted point.
- **`0x487e94(rect)`** (`..0x487eab`, `gfx_rect_area`): `(b - t + 1) * (r - l + 1)`
  by a signed `imul`.
  - EAX is the low dword and EDX the high one.
  - ECX is `b - t + 1`.
- **`0x487eac(record, pixels)`** (`..0x487f47`, `gfx_save_rect`): the kind-0
  body of `0x488012`. It does not read the kind.
  - The rect is the record's own (`record + 4`) or, when `[C+0x208]` is
    non-zero, a shifted copy in a local.
  - A header `{b - t + 1, r - l + 1, 8, 0, w * h, pixels}` is grabbed
    through `0x488974(hdr, rect, 0)`.
- **`0x488dc7(m, angle)`** (`..0x488ddc`, `gfx_rotation_matrix`): calls
  `sincos_pair 0x47eb30(angle', m)`.
  - `angle'` is the angle's low word under m's high word: `mov ax` over
    EAX = m at `0x488dce`.
  - The registers on exit are the callee's.
- **`0x488ddd(v, m, out)`** (`..0x488df5`, `gfx_rotate_point`): calls
  `rotate_2d 0x47ffe8(v, m, out)`. The registers on exit are the callee's.

`es2_sim_gfx_save`, `es2_sim_gfx_size` and `es2_sim_gfx_scroll` hand
kind-0 records to these bodies; the size and save products are computed
without a signed C `int` multiply. `es2_sim_gfx_rotate` (`0x488b98`) calls
the two rotation thunks.

### Verdicts next to the blit rows

- **`0x4866f0`** is no function. It is the exit label of the unrolled span
  block inside `0x484720`: `0x484de5` computes `0x4866f0 - eax` and jumps
  there. `fill.c`'s `span_run` covers the whole block; it has no row.
- **`0x486e3f`** is the reverse copy. Nothing calls it with a rel32 call
  and nothing points at it: it is dead and has no row.
- **`0x488012` and `0x487e60`** are save and scroll, as the bytes show:
  `0x488012` dispatches kind 0 to `0x487eac`, which grabs the surface into
  a buffer through `0x488974`; `0x487e60` dispatches to
  `0x487bdc`/`0x487d0d`, which move a rect or span list by a point through
  slots 20/19. The constants `VA_SURFACE_FILL`/`VA_RECT_CLEAR` in
  `dialogs.c` (and `VA_FILL`/`VA_CLEAR` in `dialogs_test.c`) are misnomers
  of these two.

### Blit row evidence

In vitro `//tools/reference-game/invitro:differential_gfx_blit_test`
(`gfx_blit_cases.h`, built on `gfx_cases.h`'s world; parts ramps, cursor,
scroll, area, save, matrix, rotate). Side 0 runs the machine code with
random entry registers and captures EAX/EDX/ECX; side 1 runs the row
through `es2_sim_call` on the real service table. Two host stubs serve as
the cursor's hide and show; each logs the cursor point at the time of the
call and leaves fixed registers. Compared: DATA and the world; EAX/EDX/ECX
for the ramps, cursor and area cases (and the stub log for the cursor);
EAX/EDX for the thunks (the pass-throughs of `0x47eb30` and `0x47ffe8`
hand back no ECX, so ECX after the rotation thunks is not compared). The
coverage counters cover empty copies, all three cursor paths, the three
modes with span and empty regions, the origin shift, full-range areas and
angles with a high word. `differential_gfx_test`'s save, size, scroll and
rotate parts run through these bodies. Tests that register `0x486e7c`
repoint the row after the seal (`dialogs_cases.h`,
`input_layer_cases.h`, which pass it to the original;
`sim/tests/dialogs_test.c`, which uses a recording stub).

## Cursor point and textured slot 33

The BMP_8 driver's remaining code in `0x489390..0x48993a` and
`0x48a1d4..0x48abe4`, plus `0x48bab4`. Own code in `sim/src/gfx.c`, two
service rows in `sim/dispatch/services/gfx_bmp8.h`. Confirmed in vitro; not
checked live.

| VA | Row | What |
|---|---|---|
| `0x48993a` | `gfx_cursor_point` | Sets the cursor point. When `[0x4a3646]` is set: reads `shown = [0x4a361c]`; when `shown`, calls slot 31 (`[0x4a5828]`, hide); copies the point's two dwords to `0x4a3620`; writes `[0x4a361c] = shown` back (whatever slot 31 left there); when `shown`, calls `0x4898d4` (show). Otherwise the point goes to `0x4a364a`. Exit registers: EAX = point.y, EDX = the target (`0x4a3620` or `0x4a364a`), ECX = the point. Called from `dialogs.c`. |
| `0x48a924` | `gfx_slot_polygon` | Driver slot 33, the textured polygon `(n, points)`; detail below. `es2_sim_gfx_slot(33)` runs it as own code when the table entry is `0x48a924`. |

### Slot 33 `0x48a924`: the textured polygon

- `[0x6c5f84] = n`. Nothing else happens for `n < 3` (signed). Otherwise
  `[0x6c5f88] = points` and `[0x6c5f8c] = [fill+8]`, the texture corners
  `{u, v}` per corner, with `fill = [C+0x228]`. The texture header is
  `[fill+0xc]` (word `+2` the width, dword `+0xa` the pixels). Kind 7
  (`[fill] == 7`) takes the shade table `[fill+0x10]`; every other kind
  draws transparent (texel 0 skipped).
- The y scan starts at min 0x7fff and max -0x8000. The first lowest corner
  is the top; the last highest one goes to `[0x6c5f80]`. A flat list
  (`ymax <= ymin`) draws nothing.
- Two edge records of 0x34 bytes sit on the frame: `+0` direction, `+4`
  rows, `+8` next corner, `+c`/`+10` u, v (16.16), `+14`/`+18` du, dv,
  `+1c` x, `+20` x step, `+24` x sign, `+28` error, `+2c`/`+30` its
  increments. The left one walks with direction -1, the right one with +1.
  Both are set up by `0x48aaac` (tex_edge).
- Rows run from `[0x6c5f7c] = ymin` while the row is not below the clip
  bottom `C+0x218` and left x <= right x. Rows at or below the clip top
  `C+0x210` draw a span (`0x48abe4`). Then both edges step (`0x48aa50`);
  either edge running out ends the polygon.
- `0x48abe4`: nothing when the clip rect lies beside the span. du and dv
  are `0x47e068(right - left, width << 16)`. Their integer parts become
  signed steps `su`/`sv` (±1 and ±width) plus the integer stride, rounded
  toward zero. Modes other than 2 clip to `C+0x20c..0x218` and advance
  u, v by `0x47e07d(du, skipped)`. Mode 2 walks the regions from the last
  to the first: kind 0 (a rect containing the row) and kind 2 (the row's
  span pair) are clipped likewise; any other kind ends the span. u starts
  half a step on (`u += du >> 1`).
- The kernel (`0x48aec3..0x48afb5`) rounds u and v (`+0x8000` on the low
  words). For a negative step it negates the fraction word and complements
  the start word. The fractions sit in the upper halves of ECX/EDX.
  - The shade loop (`0x48af19`) adds each carry's step separately.
  - The transparent loop (`0x48af87`) uses ESP and EBP as accumulators.
    The step index `2*carry_v + carry_u` selects from `{step, step+su,
    step+sv, step+su+sv}` at `0x4a590c..0x4a5918`. ESP and EBP are parked
    in `[0x4a591c]`/`[0x4a5920]`. The own code writes its call-area top
    and frame there; the values depend on the call depth, so the suites
    mask them.
- The sign test of `sv` is a word compare (`cmp word ptr`), so a texture
  at least 0x8000 wide would take a positive row step as negative. This is
  modelled but untested: no case builds such a texture. No shipped texture
  comes near it: the widest is 640 (`domain_textures.py`).

`0x48aa50` (edge step) and `0x48abe4` (span) are inlined; their only
caller is `0x48a924`. tex_edge `0x48aaac` is a registry hook (`math_lib`
group). The own code calls its core `es2_tex_edge_registers` directly,
after a read-only walk that refuses what would fault in the original (the
core's `q16_div` has undefined C behaviour there):

- at `0x48aaac` when the walk passes `n` corners;
- at `0x47e078` when `0x47e068` divides by zero or its quotient
  overflows 32 bits;
- at `0x48aba2` for `IDIV` of `INT_MIN` by -1.

Other refusals of the block:

- `0x48a9bb`: no corner above y 0x7fff (the original reads an unset
  local).
- `0x48af42`: a span with no visible column (the original's kernel runs
  on until it faults).
- `0x48aa41`: the row counter at `INT32_MAX`.

Inputs the original does not survive, kept out of the suite:

- **Negative texture coordinates.** The kernel takes the integer v as an
  unsigned word. A v just below 0 addresses about 0xffff texture rows on,
  and the original faults at `0x48af1b`. In the transparent kernel, where
  ESP is an accumulator, the harness process dies without a report.
- **Non-convex corner lists.** A star-shaped list with non-negative
  corners also faulted at `0x48af1b`: its edge interpolation leaves the
  texture.

The own code reads the same wild addresses there (identity memory); it
does not refuse them.

### Inlined surface pieces

`frame_buffer.c` and the object block in `gfx.c`
([Graphics objects and surfaces](#graphics-objects-and-surfaces)) contain
this range's surface attach, select and destroy pieces as inlined code:
`0x4893aa`, `0x489408`, `0x48940f`, `0x489416`, `0x4894a5`, `0x4894cc`,
`0x489558`, `0x489570`. All their callers are own rows (`0x480ecf`,
`0x481028`, `0x4811e8`, `0x481224`, `0x4813f8`), so they get no rows.

### Verdicts for the BMP_8 ranges

| Verdict | VAs |
|---|---|
| Rows | `0x48993a`, `0x48a924` |
| Inlined in `0x48a924` | `0x48aa50`, `0x48abe4` |
| Registry hook, called as its core | `0x48aaac` (tex_edge) |
| Inlined in `frame_buffer.c` / `gfx.c` | `0x4893aa`, `0x489408`, `0x48940f`, `0x489416`, `0x4894a5`, `0x4894cc`, `0x489558`, `0x489570` |
| Own code without a row | `0x489390`, `0x48a1d4`, `0x48a32c` (`frame_buffer.c`); `0x48bab4` (`hud_cockpit.c`'s span lists; its caller `0x429834` has no row either) |
| Empty BMP_8 bodies with rows | `0x4898d4`, `0x4898df` (`driver_saves_*`) |
| No caller, no table use reached | `0x4893e1`, `0x489568`, `0x48978c`, `0x4897cb` (VGA palette port I/O, slots 13/14), `0x489813`, `0x48983c`, `0x4898ea`, `0x4898f0`, `0x489923`, `0x489997` |
| Empty slot 32, skipped by `0x4813f8` | `0x4898f6` (no row) |
| Reached only from refused or dead paths | slots 21–25 `0x48a440`, `0x48a48e`, `0x48a4d4`, `0x48a5b3`, `0x48a601` (from `0x487f6a`, `0x4880bc` and the RLE path `0x488df8`); slot 35 `0x48a8e2` (from the unreferenced `0x45dc50`) |
| Label inside another function | `0x489c30` ("slot 26"): the runaway point inside `0x4899ac` (refused there) |

`differential_gfx_test`'s text and scan parts run with
`gx_regions_ordered`; the fault at `0x489c30` came from reversed span
pairs, not from the glyph offsets.

### Slot 33 evidence

In vitro `//tools/reference-game/invitro:differential_gfx_polygon_test`
(`gfx_polygon_cases.h`, parts cursor and polygon):

- cursor: EAX and EDX compared. Slot 31 is BMP_8's `0x4898df` or a
  recorder that logs the flag and changes `[0x4a361c]`.
- polygon: the world and DATA compared; the own side is called through
  the function, the driver slot and the row. The coverage counters cover
  n < 3, flat, kind 7 shaded, transparent, modes 0/1/2, mode-2 rect, span
  and other-kind regions, an empty mode-2 list, left, top and bottom
  clipping, both windings and both step signs.

`differential_gl_fill_test`'s rotate part (`fill.c` calls
`es2_sim_gfx_slot(33)`) runs this own slot 33.

## Renderer leftovers

Rows in `sim/dispatch/services/renderer_leaves.h`:

- in `sim/src/terrain_draw.c`: the grid's binning leaves `0x46e62c`,
  `0x46e8c4`, `0x46e944` and `0x470db8`, and the binning vector
  `0x428efc`;
- in `sim/src/view.c`: the horizon point `0x470a1c`, the small renderer
  leaves `0x48de70`, `0x48de78`, `0x4758b4`, `0x42eb5c`, `0x42ebbc`,
  `0x476940` and `0x476aac`, the six stream-read thunks and `0x4930ab`.

Every row sets EAX, EDX and ECX as the original leaves them. Confirmed in
vitro; not checked live. "The callee's" means the registers the last call
returned. Compares are signed unless stated.

### The grid leaves

- **`0x470db8(G, cell)`** (listing `0x470db8..0x470ddb`): the cell's mark
  byte at `[G+0xf0] + ((cell - [G+0xec]) >> 4)` (a logical shift) plus 1,
  as a byte. EAX = the new mark (zero-extended), EDX = the index, ECX = the
  entry ECX with its low byte replaced by the mark.
- **`0x46e8c4(G, w)`** (listing `0x46e8c4..0x46e943`): col = the word
  `[w]`, row = the word `[w+2]` (both sign-extended).
  - idx = `(row << [G+0x100]) + col`.
  - Outside `0 <= idx < 1 << ([G+0x100] + [G+0x104])`: EAX = 0, EDX = idx.
  - Otherwise the record at `0x6b5058` gets the bytes `[w]`, `[w+2]` and
    the cell's new mark (`0x470db8` on cell `idx * 16 + [G+0xec]`).
    EAX = `0x6b5058`, EDX = `0x470db8`'s (idx).
  - ECX is the sign-extended col on both paths: a prologue local, not a
    callee register. The epilogue's `pop ecx` takes the local slot the col
    was stored in (`0x46e8d7`, `0x46e941`).
- **`0x46e62c(G, P, size)`** (listing `0x46e62c..0x46e8c0`): the binning
  cell of the point P for an object of radius `size`.
  1. With s = `[G+0x108]`: the cell (cx, cy) = (P.x >> s, P.y >> s) and
     the eye's cell (ex, ey) from `0x6b4f44`/`0x6b4f48` (arithmetic
     shifts).
  2. The eye's quadrant decides which neighbour the object leans into when
     it lies within `size` of the cell's edge:

     | Quadrant | Condition | x | y |
     |---|---|---|---|
     | 0 | ex < cx, cy > ey | cx - 1 if P.x < (cx << s) + size | cy - 1 if (cy << s) + size > P.y |
     | 1 | ex >= cx, cy > ey | cx + 1 if P.x > ((cx+1) << s) - size and ex > cx | as quadrant 0 |
     | 2 | ex >= cx, cy <= ey | as quadrant 1 | cy + 1 if ((cy+1) << s) - size < P.y and cy < ey |
     | 3 | ex < cx, cy <= ey | as quadrant 0 | as quadrant 2 |

  3. When the cell moved: the terrain face under P (hook `0x46e498`) takes
     the move back when it does not face the eye (`0x46be9c`, own code).
     Then the bounds of the view polygon (`[G+0xc8]` points at `[G+0xcc]`)
     take a coordinate back that lands one beyond the minimum or maximum.
  4. Outside the grid (cx or cy negative, or not below `1 << [G+0x100]`
     resp. `1 << [G+0x104]`): EAX = 0. Otherwise the result of `0x46e8c4`
     on the words (cx, cy).
  - EDX and ECX follow each path: EDX is the last threshold or polygon
    coordinate; ECX is P.x (quadrants 0–2) or P (quadrant 3), the polygon
    count after the polygon loop, or `[G+0x100]`/`[G+0x104]` after the
    bound that failed.
- **`0x46e944(G)`** (listing `0x46e944..0x46e97c`): clears the marks with
  `memset([G+0xf0], 0, (1 << [G+0x100]) * (1 << [G+0x104]))`. The
  registers are the memset's.
- **`0x428efc(n)`** (listing `0x428efc..0x428f1d`): `[0x49ac40]` =
  `0x49667c(0, 0x1d, sx(n as a word), 1, 0x429655)`. The registers are the
  callee's.

### The horizon point `0x470a1c`

`0x470a1c(G, cam, out)` (listing `0x470a1c..0x470b26`): the screen point
of the grid's horizon ahead of the camera.

1. With s = `[G+0x108]` and F = `[G+0x10c]`: d = F << s, or 1000 when
   the camera's cell lies within F cells of the grid's edge (signed
   compares against `(1 << [G+0x100]) - F` and `(1 << [G+0x104]) - F`).
2. The rotation comes from `0x47eb30`. Its argument is the heading word
   `[cam+0x14]`; the dword's high half is the camera pointer's (a word
   load into a register that held cam).
3. (0, d) is rotated by `0x480050` with the camera's x and y as offset,
   the height is `[G+0x110]`, and the point is transformed by the view in
   use (`0x48043c` with `0x6c5ff4`), projected (`0x48c5fc`) and put on
   the screen (`0x48c6d0`).
4. out = the screen point. EAX = out, EDX = its y, ECX = its x.

- `0x480050` (listing `0x480050..0x4800bc`) is computed inline: out0 =
  the low dword of `(a0*m0 + a1*m2 + 0x2000) >> 14` plus `[m+8]`, out1
  the same with m1, m3 and `[m+0xc]` (m0..m3 the sign-extended words at
  m+0..+6, 64-bit sums, `shrd`). The VA lies in the math catalogue range
  `0x47dbca..0x480694` and has no row of its own; when it gets one,
  `0x470a1c` can call it with the same behaviour.
- A division fault in the projection (`0x48c616`, `0x48c625`, `0x48c63b`,
  `0x48c64c`) refuses the call, as in `0x48cce4`.

### The small leaves

| Function | Effect | EAX | EDX | ECX |
|---|---|---|---|---|
| `0x48de70()` | byte `0x6cbc04` = 1 | unchanged | unchanged | unchanged |
| `0x48de78(P)` | `memset(P, 0, 0x20)`; the words +0x10, +6, +0 = 0x4000 | P | the memset's | the memset's |
| `0x4758b4(P)` | vtables `0x4a69f0`, `0x4a6a14` (counter + 1), `0x4a6250` (counter + 1), `0x4a0fbf`; +0x10..+0x1c = 0; counter + 1 | P | the counter pointer | 0 |
| `0x42eb5c(S)` | `0x4758b4(S)`; `[S]` = `0x49afb0`; +0x6c 1, +0x54 3, +0x58 0x10, +0x5c 1, +0x60 8, +0x64 1, +0x68 1, +0x50 0; bytes +0x70 0, +0x71 1; counter + 1 | S | the counter pointer | S |
| `0x42ebbc(S, a, b, d)` | +0x28 = `(a + [S+0x58] - 1) & 0xffffff`, +0x2c = `b & 0xffffff`, +0x30 = d | S | d | `b & 0xffffff` |
| `0x476940(P)` | `0x476710(P)`; `[P]` = `0x4a1088`; dword +0x16 = 0; counter + 1 | P | the counter pointer | P |
| `0x476aac(P)` | `0x476710(P)`; `[P]` = `0x4a1054`; word +0x16 = 0; +0x18, +0x1c = 0; counter + 1 | P | 0 | the counter pointer |
| `0x4930ab(S)` | the stream's error state | `[S+4]` | unchanged | unchanged |

"The counter" is the object counter the other constructors use.

**The stream-read thunks.** Each calls S's slot +0x18 (read) with
(S, n, p). The slot is entered with EAX = the vtable, EDX = p and ECX = n
where the thunk computes n (else the entry ECX). The registers are the
slot's.

| Thunk | n |
|---|---|
| `0x48f3ea(S, k, p)` | k << 2 |
| `0x490874(S, p)` | 1 |
| `0x491171(S, k, p)`, `0x49245e(S, k, p)` | 2k |
| `0x49118c(S, p)`, `0x492430(S, p)` | 2 |

### Verdicts for the leftovers

| VA | Verdict | Evidence |
|---|---|---|
| `0x46e62c`, `0x46e8c4`, `0x46e944`, `0x470a1c` | row | `sim_gap.md` roots called from `scene.c` |
| `0x470db8` | row (behind `0x46e8c4`) | only caller `0x46e8c4` |
| `0x42e760` | inlined in an own row | only caller `0x42e010` (world body, own code) |
| `0x42eb5c`, `0x42ebbc` | row; also inlined in `0x42e010` | caller `0x42e010` |
| `0x46bfdc` | inlined in an own row | only caller `0x46c3f0` |
| `0x46c3f0` | inlined in an own row | only caller `0x46c2e0` (terrain shading, own code) |
| `0x428efc` | row; also inlined in `0x428230` | caller `0x428230` (binning tables, own code) |
| `0x48de70`, `0x48de78` | row; also inlined in `0x48dd08` | caller `0x48dd08` (renderer reset, own code) |
| `0x4758b4` | row | callers `0x42eb5c`, `0x475ac0`, `0x48e8b0` (no own code yet) |
| `0x476940`, `0x476aac` | row | callers `0x4901a0` resp. `0x490388` (no own code yet) |
| the six stream thunks | row | callers without rows, e.g. `0x482a14`, `0x48e9f0`, `0x47b704`, `0x48fce0`, `0x490e68`, `0x48e02c`, `0x48ef20`, `0x4914f0`, `0x491bb0`; `0x481d8c` is a hook |
| `0x4930ab` | row | the FILE functions `0x47a34a`..`0x47cd8c`, `0x492c84`, `0x492f54` |
| `0x49495f` | not a function; inlined in the scan converter's row | a label inside `0x493192`: the target of the computed jump into the unrolled `stosd` table, part of `gfx_scan_convert` |
| `0x480050` | math catalogue; computed inline in `0x470a1c` | catalogue range `0x47dbca..0x480694` |

The rows of the small leaves and the thunks are for direct calls; their
callers have no rows yet.

### Leftover evidence

In vitro `//tools/reference-game/invitro:differential_renderer_leaves_test`
(`renderer_leaves_cases.h`, 3 shards; parts grid, horizon point, leaves). Side 0
runs the original through `au_call` with random entry registers; side 1
runs the row through `es2_sim_call`; both sides use copies of the same
world. Recorders (a jump patch on side 0, a row at the same VA on side 1):
the memset `0x49681a`, the vector `0x49667c`, the terrain face `0x46e498`
with a scripted result (0 or a face), and the stream slot +0x18, an
assembly trampoline with scripted EAX, EDX and ECX. `0x47eb30` and
`0x48043c` run as the original on both sides. Compared: EAX, EDX and ECX;
the recorder hash and count; DATA; and the world. A case whose own side
refuses at one of the projection's divisions is skipped, so the refusal
path is not compared against the original's exception. `scene_test.c`
and `scene_cases.h` list the four grid roots as replaced.

## Open questions

- **Live behaviour.** Every own row here is confirmed in vitro only. No
  static frame-write claim of the renderer line was compared against a
  captured frame, and the visible fill rules (edge ownership, texture
  projection; see [dts.md](../formats/dts.md)) still need a visual
  oracle.
- **Refusal sites as a replay question.** The view and projection
  refusals (`0x48c5e2`, `0x48c5f6`, `0x48c616`, `0x48c625`, `0x48c63b`,
  `0x48c64c`, `0x48c10a`, `0x48c133`, `0x48c281`, `0x48c2aa`, `0x40712f`,
  `0x40717a`, `0x407210`, `0x406f69`) and the fill and shape-draw
  refusals model the original's faults (integer divide faults, run-away
  loops, stale pointers); whether an input reaches one is the divide-site
  question of [own_process.md](../reference-game/own_process.md) "Inputs
  that reach a site". The GL refusals are settled: dead, unreachable by
  data (compressed GLBitmap, ramp copy) or a NULL access.
- **Incidental registers.** EAX of `0x48c2e4` and `0x48c444` (lights off)
  are the originals' incidental values; no caller reads them.
  `0x48cce4`'s EDX without a shift (an uninitialised local) and the line
  `0x483a04`'s EDX/ECX are not modelled. ECX after the rotation thunks
  `0x488dc7`/`0x488ddd` is not compared. The `0x4894a5` register shift is
  checked but not modelled.
- **Owners not traced.** Who fills the per-row run lists `[[0x4a5af8]]`;
  which suites set the depth switches `0x49f248`/`0x49f24c` (static
  reading only).
- **Not reconstructed.** `0x4785e8`, a load-time constructor next to the
  edge tables; the walkers and HUD code in `0x471e10..0x472e6c`
  ([terrain.md](terrain.md), [hud.md](hud.md)).
- **Not walked.** The renderer's pointer-referenced entries beyond the 41
  roots (other DTS part classes) were taken from the function index.
- **Untested inputs.** The fill region count 0x10000 in `0x483eb8`; a
  slot-33 texture at least 0x8000 wide (no shipped texture is wider than
  640, `domain_textures.py`; the region count is counted at run time, not
  read from a file); whether `0x468414` reads the beam
  quad's uninitialised `+8`, `+0x14` and `+0x18` for page 0 and mode 0 (the
  stand-in does not log them).
- **Refused where the original faults.** `gl_gc_construct` with a NULL
  current surface or clip record is a NULL access
  ([own_process.md](../reference-game/own_process.md)); the ramp copy and
  the compressed GLBitmap are unreachable by the CD data (above).
- **Host values.** `0x4835c8` and `0x46bc44` return the FS:0 head in ECX;
  in the own process that is the host thread's head, not a guest value.
- **Live callers.** The callers and objects of the actor draw slots, the
  shade ranges and the explosion tests are not checked live.
- **Code comments.** The `dialogs.c` constants `VA_SURFACE_FILL`/
  `VA_RECT_CLEAR` name the save `0x488012` and the scroll `0x487e60`.
