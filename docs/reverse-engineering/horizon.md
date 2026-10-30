# Horizon painter `hzline`

The sky and ground colour bands behind the terrain, drawn by the cluster under DBSIM `0x42ec08`: `0x42ec68` (181 instructions), `0x42ee88` (197), `0x42f0b0` (273) and the runtime helper `0x49667c` they call. The frame's place in the renderer line is in [rasterizer.md](rasterizer.md#the-renderer-line-r3d).

**Sources.** The bytes of the reference `VER95/DBSIM.EXE` (image base `0x400000`, see [reference_version.md](../reference_version.md)), read with `llvm-objdump -d` from the pinned LLVM-MinGW. Functions that a linear listing decodes misaligned (`0x483eb8`, `0x486704`, `0x49545b`) were read from their entry. Callers come from a scan of every `E8`/`E9` byte in CODE whose rel32 target is the function (not only the aligned listing) and from a scan of the whole file for the function addresses as data dwords (vtables, tables, pushes). Static reach follows direct calls and intra-function jumps from the entry. Live counts are the saturated entry-probe counts of the DBSIM map (order a60/c1s/c1c/c3_09/c4_08/c3_10, 1000 = at least 1000). Names and meanings are **hypotheses** unless stated.

## What the cluster is

1. **The cluster is the horizon painter.** The object is an instance of the Borland class `hzline` (RTTI name string at `0x42f6b2`, size `0x7a` = the `0x47749c(0x7a)` allocation called at `0x42e0bf`), held in the global `[0x49aee0]`. `0x42ec68` computes the horizon line on screen from the camera's pitch and roll, `0x42ee88` fills the sky bands above it, `0x42f0b0` fills the area below it down to the projected terrain edge. All drawing goes through the graphics library's polygon and span fills (`0x484222`, `0x483eb8`, `0x486704`).
2. **The only caller chain is the rendering root**: `0x45fca8` → `0x42e700` (call at `0x45fda3`, argument the camera `[0x4d24bc+0x2e]`) → `0x42ebe8` (`0x42e734`) → `0x42ec08` (`0x42ebfb`) → `0x42ec68` (`0x42ec2c`), `0x42ee88` (`0x42ec40`), `0x42f0b0` (`0x42ec59`). Each of these has exactly one call site in the whole CODE section and no data reference (by the two scans). Counts: all six functions 1000×5/937, the same as `0x45fca8`, i.e. once per rendered frame.
3. **None of it changes compared simulation state.** Writes go only to the `hzline` object (fields `+0x34..+0x4c`, read back only by this cluster in the same call), to the caller's stack, and temporarily to the graphics context's fill-colour pointer `[0x6c5f70]+0x22c`, which both painters save on entry and restore on every exit path. Reads of simulation-owned data are the camera's two angle words. The static reach (27 functions: the four cluster functions, `heading_cos` `0x47dff0`, `0x49667c` with its allocator and error stubs, and the rasterizer `0x483a04`…`0x4895ec` including the Gouraud band painter `0x486ee8`) contains no reference to RNG stream A `0x4d2599`, stream B `0x4d260b`, the clock `0x467828`, the frame counter `0x4d257b`, the pause flag `0x4d2572` or the terrain grid `[0x4a0be0]`/`[0x49aac8]`. Its 24 indirect calls/jumps are rasterizer span/driver slots (`0x4a57ac`, `0x4a5830`, `0x4a5834`, locals loaded from them), the RTL element constructor of `0x49667c` and three import thunks of the runtime DLL.
4. **Classification: presentation (H3), no hooks.** Nothing else reads what the cluster computes. `0x49667c` stays original (RTL array construction); on this path it is called with a non-null stack array of five 8-byte points (`0x42eeab`, `0x42f0df`; flags 1, element constructor `0x49545b`, which only returns `this`), so it never reaches its allocator `0x4774f0` here. The one hooked callee of `0x42ec68` is `heading_cos` (`0x47dff0`, four calls per frame): rendering contributes to `hk_heading_cos_calls`, not to compared state.

## The `hzline` object

Created once per mission by the world loader `0x42e010` (only caller `0x4246c9`; it builds the resource name `world<n>.wld`, see [mission.md](../formats/mission.md#worldwld-grammar)): allocation `0x42e0bf`, constructor `0x42eb5c` at `0x42e0d6`, pointer stored to `0x49aee0` at `0x42e0f3`. The first eight WLD words (read at `0x42e0f8..0x42e182` into locals `-0x86..-0x94`) configure it at `0x42e185..0x42e1ee`. The constructor first runs the base constructor `0x4758b4` and then installs vtable `0x49afb0`, which equals the base vtable `0x4a0fbf` except slot 6 (`0x42f6d2`, the destructor: base destructor `0x475900`, free `0x477548` when bit 0 of the flag is set). No vtable slot is called on `[0x49aee0]`; the pointer is loaded only at `0x42e18c`, `0x42e703` and `0x42e724` (all references to `0x49aee0`).

| Offset | Written by | Read by | Meaning (from the use) |
|---|---|---|---|
| `+0x28` | setter `0x42ebbc` (`0x42ebcf`): (WLD word 1 + `[+0x58]` − 1) & `0xffffff` | `0x42eeed`, `0x42f1e7`, `0x42f338` | first sky colour; decremented locally once per band |
| `+0x2c` | setter (`0x42ebdb`): WLD word 4 & `0xffffff` | `0x42f129` | ground colour base (feeds only the dead band loop, see Quirks) |
| `+0x30` | setter (`0x42ebe1`): WLD word 7 | `0x42ebf2` (third argument of `0x42ec08`), `0x42ed9e`, `0x42edad` | vertical offset of the horizon in rows |
| `+0x34` | `0x42ec1e`: −camera word `+0x10` | `0x42ec68` | negated camera pitch |
| `+0x36` | `0x42ec26`: camera word `+0x12` | `0x42ec68`, `0x42eef3`, `0x42f143` | camera roll; 0 selects the axis-aligned (span) paths |
| `+0x38,+0x3c` / `+0x40,+0x44` | `0x42ec68`; `+0x3c`/`+0x44` also `0x42ec39..0x42ec49` | `0x42ee88`, `0x42f0b0` | the two screen end points (x, y) of the horizon line |
| `+0x48,+0x4c` | `0x42ec68` (`0x42ee5e..0x42ee7b`) | `0x42ee88`, `0x42f0b0` | band step direction, Q14 (from the roll) |
| `+0x54`, `+0x58` | loader (WLD words 2, 3) | `0x42ee88` | sky band height and band count |
| `+0x5c`, `+0x60` | loader (WLD words 5, 6) | `0x42f0b0` | ground band height and band count (dead use, see Quirks) |
| `+0x6c` | loader (WLD word 0) | `0x42ee88`, `0x42f0b0` | band rounding term (`+0x6c >> 1`) |
| `+0x71` | constructor (`0x42ebac`) and `0x42e70c`, both 1 | `0x42f184`, `0x42f332` | always 1 (see Quirks) |
| `+0x72`, `+0x76` | `0x470a1c(G, camera, H+0x72)` at `0x42e71c` | `0x42f1a1` | screen point of the terrain edge along the view heading (projection of a point `[G+0x10c]` cells ahead, or 1000 near the grid edge, at height `[G+0x110]`) |

The global word `0x49aee8` = WLD word 4 + word 6 − 1 is written by the loader at `0x42e1d6` and has no reader in the image.

## Place in the frame

The render root runs `0x42e700(cam)` as step 14, only when `0x42db18` reports the 3D view: after the camera setup (step 10), `vector_copy` (11), terrain visibility (12) and `0x48c444(0)` (13). With the 3D view hidden nothing of the cluster runs and the panel or HUD pixels stay in that rectangle. `0x42e700(cam)` (38 instructions, H = `[0x49aee0]`) then runs in this order:

1. `H+0x71` ← 1 (written without a null test).
2. `0x470a1c([0x4a0be0], cam, H+0x72)`: the terrain-edge point. It reads G `+0x100`, `+0x104`, `+0x108`, `+0x10c`, cam `+4`, `+8`, the heading word `+0x14` and the camera matrix pointer `[0x6c5ff4]`; it calls `sincos_pair` (hook), `0x480050`, `transform_point` (hook) and the projection `0x48c5fc`/`0x48c6d0`, and writes only `H+0x72`/`+0x76` (aligned body `0x470a1c..0x470b28`). The globals `0x6b5068`/`0x6b506c` are written by the next function (`0x470b97`, `0x470b9f`, `0x470bac` in `0x470b2c`), not by `0x470a1c`.
3. H ≠ 0: `0x42ebe8(H, cam, 0x4cf9a0)` → `0x42ec08`: the horizon line, the sky bands, the ground haze. `0x4cf9a0`/`0x4cf9a4` is the screen-jolt offset (x, y) of [display_modes.md](display_modes.md) (written at `0x429f3d`, `0x42a1ac`, also added to the canvas origin at `0x42a1c0`).
4. `[0x49aee4]` ≠ 0: its slot `+0x34`. Never taken: the dword is 0 in the image and this load (`0x42e73c`) is its only reference.
5. `0x428140(cam)`: the terrain cells and their objects ([terrain.md](terrain.md#the-terrain-pass-in-the-frame), [terrain.md](terrain.md#call-chain-of-the-terrain-pass)).
6. `0x42eb40(cam)`: empty body (`push ebp; mov ebp, esp; pop ebp; ret`), so EAX after `0x42e700` is the terrain draw's `0x46d1a8` return passed through `0x428140`.

The backdrop is painted first and the terrain and objects overdraw it. Coupling: none (no clock, RNG, sound or heap call in the closure of steps 1–3).

## Painter functions

`0x42ebe8(H, camera, J)`: `0x42ec08(H, camera, [H+0x30], J)`.

`0x42ec08(H, C, off, J)` (41): `H+0x34 = −C.w10`, `H+0x36 = C.w12`; `0x42ec68(H, J)`; `H+0x3c += off >> 1`, `H+0x44 += off >> 1` (SAR); `0x42ee88(H)`; `H+0x3c += 1`, `H+0x44 += 1`; `0x42f0b0(H, C, J ? J[0] : 0)` (`0x42f0b0` ignores its second and third argument).

| VA | Instr. | Role | Reads | Writes | Callees |
|---|---:|---|---|---|---|
| `0x42ec68(H, J)` | 181 | horizon line: sines of pitch and roll through `heading_cos` (`0x47dff0`, 4×), half extents from the screen-size shifts (words `0x4d253a`/`0x4d253c` + 8), offset `(sin(pitch+0xc000) << [0x6c6028]) / sin(pitch)` (IDIV; divisor forced to 1 when 0 at `0x42ecc6`, so no fault), screen centre `[0x6c6034]`/`[0x6c6038]`, `H+0x30`; then the jolt J (`J[1]` ≠ 0, else `J[0]` with the roll quadrant) widens or shifts the line | H, J, the globals listed | H `+0x38..+0x4c` | `0x47dff0` |
| `0x42ee88(H)` | 197 | sky: with roll ≠ 0, `[H+0x58]` − 1 quadrilateral bands through `0x484222` plus one far band of 5000·direction; with roll = 0, axis-aligned spans through `0x486704` up to the clip top `[ctx+0x214]` | H, `[0x6c5f70]` | stack; `[ctx+0x22c]` (saved at `0x42eedb`, set to a local colour record {0, colour} at `0x42eee7`, restored at `0x42f0a1`) | `0x49667c`, `0x484222`, `0x486704` |
| `0x42f0b0(H, -, -)` | 273 | ground: one polygon from the horizon line to the terrain-edge point `H+0x72` (`0x483eb8`, with the clip corners `[ctx+0x210]`/`[ctx+0x218]` added as vertices), or for roll = 0 one span fill down to the clip bottom `[ctx+0x21c]` | H, `[0x6c5f70]` | stack; `[ctx+0x22c]` (saved `0x42f117`, set `0x42f123`, restored `0x42f38a`) | `0x49667c`, `0x483eb8`, `0x486704` |

The colour record pointer at `[ctx+0x22c]` is the one the fills read as `[[0x4a3614]+0x228]`: `0x480d44(ctx)` stores `ctx` to `0x6c5f70` and `ctx+4` to `0x4a3614` (`0x489558` at `0x480d65`), so both name the same field.

The jolt record J: `+4` nonzero pushes both ends of the line; otherwise the sign of `+0` and whether the roll lies in `(0x4000, 0xc000)` choose which end moves.

### Frame writes

| Painter | Pixels | Through | Rectangle |
|---|---|---|---|
| `0x42ee88` sky | `[H+0x58]` − 1 bands from the horizon line upward plus one far band (roll ≠ 0); spans up to the clip top (roll = 0) | `0x484222` → `0x483eb8` (GL polygon fill), `0x486704` (rectangle fill) | ctx clip `[ctx+0x210..+0x21c]`, colour record at `[ctx+0x22c]` (saved, set, restored) |
| `0x42f0b0` ground haze | one polygon from the horizon line to the terrain-edge point `H+0x72` in the first sky colour; for roll = 0 one span fill to the clip bottom | `0x483eb8`, `0x486704` | the same |

No palette entry is written.

## Quirks

- **Dead "upside-down" flag in `0x42ec68`.** `0x42ec80..0x42ec91` sets the local at `-0x14` only if the pitch word is above `0x4000` (unsigned) *and* its zero-extended value compares signed-less than `0xffffc000` (−0x4000). A zero-extended word is never negative, so the `JGE` at `0x42ec8f` is always taken, the flag stays 0 and the branch `0x42ee5e..0x42ee6d` (negated direction, `H+0x48 = -cos(roll)`) never runs; the direction is always `(+0x48, +0x4c) = (s16 sin(roll+0xc000), −s16 sin(roll))`.
- **`H+0x71` is always 1**, so the per-band ground loops of `0x42f0b0` (`0x42f1ef..0x42f24e` for roll ≠ 0, `0x42f340..0x42f36d` for roll = 0) never run, and the ground is a single fill in the first sky colour `[H+0x28]` (the haze between the horizon and the terrain edge). The ground band parameters `+0x2c`, `+0x5c`, `+0x60` reach only those dead loops (their colours would count down from `+0x2c + count − 1`).
- `0x42f0b0` reads `[0x6c5ed0]` into an unused local and reuses EBX as a scratch register at `0x42f2e4` (after its last read of H); harmless, EBX is restored from the stack at `0x42f392`.
- `0x42e700` writes through `[0x49aee0]` without a null test.

## The `TexPoly` class in this range is dead

The code at `0x42f6ff..0x430390` (besides `hzline`'s destructor `0x42f6d2`) is the class `TexPoly` (RTTI name at `0x4302da`): constructor `0x42f700`, vtable `0x49afe6` with slots `+0x18` `0x4302fa` (destructor), `+0x20` `0x42f970`, `+0x24` `0x42f860`, `+0x2c` `0x42fc34`, `+0x30` `0x42ff88`, `+0x34` `0x42fa18`. **No `E8`/`E9` transfer reaches `0x42f700` and no dword in the file equals it; the only reference to the vtable `0x49afe6` is the constructor's own store (the instruction at `0x42f75c`).** So no `TexPoly` object exists and none of its methods runs. That includes the two stream-B draws `0x42f874` (`0x42f860`) and `0x42f9cc` (`0x42f970`) that [rng.md](rng.md#rng-call-sites) lists as dead sites, and the rasterizer callers `0x42fc34` → `0x46817c` and `0x42fa18` writing `0x4a0994`. The map has no probe row for these entries (pointer-referenced only), consistent with this reading. An own renderer needs none of it.

## What a replacement painter must keep

The exact save/restore of `[ctx+0x22c]` around the fills (other draw code reads it); the `heading_cos` calls (their hook counter is part of the live comparison of call counts, not of state); the jolt offset `0x4cf9a0` input; the fill order sky → ground before the terrain cells (`0x428140` runs after `0x42ebe8` in `0x42e700`, so the terrain overdraws the backdrop). Nothing outside the painter reads `H+0x34..+0x76`.

## The sky and the horizon as own code

`sim/src/scene.c` holds the sky and horizon functions as service rows of `sim/dispatch/services.h` (`sim/dispatch/services/scene.h`): the entry `0x42e700(cam)` (`scene_sky_horizon`), the horizon forward `0x42ebe8(H, C, J)`, the painter `0x42ec08(H, C, off, J)`, the line `0x42ec68(H, J)`, the sky bands `0x42ee88(H)`, the ground `0x42f0b0(H, C, J)`, the terrain cells pass `0x428140(cam)`, the jolt palette switches `0x42ea44`/`0x42ea70`/`0x42ea94` and the line object's destructor `0x42f6d2` (`scene_hzline_dtor`: counter − 1, base destructor `0x475900(obj, 0)`, free when flag bit 0). Each was written from the `llvm-objdump` listing; the H fields are those of the object table above.

- The flip test of `0x42ec68` is kept as written (pitch word against `0x4000` unsigned, then as a signed dword against `0xffffc000`); its branch stays dead.
- The bands' colour record: `[gc+0x22c]` points at a local `{0, colour}` record while the bands are drawn, and the old pointer comes back at the end (EAX). Sky colours count down from `+0x28`. On flat ground (`+0x71`) the ground uses one band to the terrain edge point `+0x72`/`+0x76`, in the sky colour.
- `0x42f0b0` reads `[0x6c5ed0]` into an unused local and does not read the jolt pointer.
- `0x42e700` after its work calls `0x42eb40` through the dispatch; EAX stays the terrain draw's. The own code refuses a NULL horizon (the original faults at `0x42e70c`) and the integer division fault of `0x42ec68` at `0x42ed4e`.
- The fills `0x484222`, `0x483eb8` and `0x486704`, the terrain draw `0x46d1a8`, the edge `0x470a1c` and the palette swap `0x4303b0` are separate services reached through the dispatch.

Tests:

- `//sim:scene_test` runs the own code on a synthetic image.
- `//tools/reference-game/invitro:differential_scene_test` (`--scene`, `scene_cases.h`) runs the horizon part (1500 cases) and the leaves part (500 cases). Each matches the original's machine code: the callees' call order and arguments (the polygon points and the colour record), DATA and the heap, the world and EAX.
