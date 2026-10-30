# Display modes, panel scroll and screen jolt

The display-layout manager of DBSIM and the screen jolt: the region
`0x429660..0x42e700` (`0x429e60`, `0x42a9c0`, the `0x42d*` cluster), the
panel move `0x43271c`/`0x433968`, the jolt and bump drivers
`0x434010..0x434225` and the mission-start sequence `0x45d85c`. Model and
legend: [hud.md](hud.md) and [cockpit.md](cockpit.md).

**Sources.** Read from the bytes of the reference `VER95/DBSIM.EXE`
(SHA-256 `8561aa47…c4ad`, image base `0x400000`) with
`llvm-objdump -d --no-show-raw-insn -M intel` from the pinned LLVM-MinGW.
The jump-table functions `0x42a3f4`, `0x42a4c4` and `0x42a9c0` are
disassembled from the table targets, because a linear listing decodes
their tables as instructions. Initialized DATA is read at file offset
`0x96200` (tables, strings, Borland RTTI names). Live counts are the
saturated entry-probe counts of the DBSIM map
(`<es2-data>\logs\dbsim-map-20261008\map.json`, order
a60/c1s/c1c/c3_09/c4_08/c3_10, 1000 = at least 1000). "never" = a probed
entry that was not entered after the proxy armed in any suite; "n/p" = not
probeable; "no entry" = not in the map's entry list. Key counts of the
suites come from `tools/reference-game/input-scripts/*.jsonl`.
**Confirmed** = read from the instructions or the data; meanings and names
are **hypotheses** unless stated.

## Overview

1. **There is no wingman-order, radio or dialog code in this region
   (confirmed).** The "Nearest hooks" column of
   [hook_candidates.md](../reference-game/hook_candidates.md) lists the
   registered hooks nearest *by address* (`terrain_line` `0x4280f4` below,
   `wingman_order` `0x431610` above); none of the functions here calls
   either hook.
2. **The region is the display-layout manager and the screen jolt
   (confirmed).** The display object D = `[0x4d24c0]` = `[0x4cf99c]` keeps
   one of five layouts ("display modes" 0–4: cockpit, scrolled panel, two
   side views, full-screen outside view); `0x42a3f4` takes requests,
   `0x42a4c4` starts a transition, `0x429e60`/`0x42a1f0` switch the layout,
   `0x42a9c0` animates and finishes it. `0x42d2f8`/`0x42d4a8`/`0x42d624`
   are the start, per-frame step and end of a vertical jolt of the screen
   page (armed by a hit on the player, `0x434010`). `0x43271c`/`0x433968`
   move the cockpit panel, its widgets and its three scroll zones.
3. **`0x42d2f8`, `0x42da08`, `0x42d624`, `0x42d4a8` and `0x433968` change
   no compared simulation state (confirmed for the writes; "not compared"
   is a hypothesis for the rasterizer row table).** They write the display
   object, the graphics context `[0x6c5f70]`, the screen object
   `[0x6c5ed0]`, the rasterizer row table `[0x4a5af8]`,
   panel/widget/scroll-zone rectangles and their own globals, call
   graphics-driver pointers and the graphics library, and call neither the
   clock nor an RNG nor audio. Their verdict is H3 (display) in
   [function_verdicts.md](../reference-game/function_verdicts.md).
4. **Three edges from this presentation code reach simulation input
   (confirmed instructions, consequences partly hypotheses)**; the own code
   keeps them in place and order
   ([Simulation-relevant edges](#simulation-relevant-edges)):
   - the layout switch `0x429e60` sets or clears the cockpit's panel-off
     byte `H+0x20f` (`0x431500`/`0x431518`), which makes the HUD key handler
     `0x432bc8` ignore all keys (target, weapon and Enter keys write
     simulation state, [hud.md](hud.md));
   - the end of the 0↔1 transition in `0x42a9c0` switches the flight-key
     capture `0x45b68c` off (entering mode 1) or on (back to mode 0); in
     c3_09 the 18 Esc presses toggle it 18 times;
   - the display mode `D+0x14` itself is read by the key dispatchers
     `0x432bc8`, `0x45feb8` and `0x460870` and selects what keys do.
5. **Two of these presentation functions run inside the logic frame
   (confirmed).** `0x434010` (from `ground_collide` and `0x4188c8`) arms the
   jolt through `0x42d2f8`/`0x42d624`; the death sequence `0x45fa84` calls
   `0x42a4c4`, which can switch to the outside view through `0x429e60`
   (panel off, border draw `0x42da08`) in the same logic frame.
6. **`0x45d85c` is the mission-start drop sequence** (bitmap `intro`,
   mission load `0x461608`, before the proxy arms, "never" in the map). It
   moves the player vehicle's height in 115 steps and renders a frame per
   step, then shakes the screen for 30 clock ticks while drawing RNG stream
   B once per frame ([Open questions](#open-questions)). It writes the
   player position only temporarily (restored exactly, confirmed).

## Objects

| Object | Constructed | Fields used here (offsets confirmed, meanings hypotheses) |
|---|---|---|
| D = `[0x4d24c0]` = `[0x4cf99c]` | `0x429660(D, player)` from the mission load `0x461608` (`0x461ca0`, stored at `0x4d24bc+4`); `0x429834` sets `D+0x1f` and registers D in `0x4cf99c` through `0x429810` (`0x429aae`); getter `0x429820` | `+0x00` view count; `+0x04` pointer to the 32-byte layout (view) records R at `0x4cf9a8` (R+0..+0xc clip rectangle, R+0x10 canvas origin, R+0x18 panel offset point); `+0x08` per-mode bitmap (view shape) table, new[](0x10), one dword per view (`0x42a1f0`, `0x429c2c`); `+0x0c` per-mode item lists of `0x204` bytes (count, then pointers; copied into the graphics context), new[](0x810); `+0x10` per-mode row-table (span list) pointers (→ `0x4a5af8`), new[](0x10); **`+0x14` display mode** (−1 after the ctor, 0 after the mission load's `0x42a1f0(D,1)`, `0x42a1f0(D,0)`); `+0x18` pending transition (−1 none); `+0x1c` transition started; `+0x1d` cool-down word (2 after each transition); `+0x1f` cockpit H = `[0x49b088]`; `+0x23` mode before the outside view; `+0x27` a word; `+0x29` vehicle P; `+0x2d` name; `+0x36` border redraw pending |
| graphics context `[0x6c5f70]` | graphics library | `+0x04..+0x207` item list copy (`0x81` dwords), `+0x208`/`+0x20c` clip flags (saved, zeroed and restored around fills), `+0x210..+0x21c` clip rectangle, `+0x220`/`+0x224` origin, `+0x22c` fill-colour pointer |
| screen object `[0x6c5ed0]` | graphics library | `+0x34`/`+0x38`/`+0x3c` page selectors, `+0x48 + 8·page` page origins, `+0x88`/`+0x8c` the two pages (swapped by the presentation frame); width `+8`, height `+0xc` through the getter `0x481350` |
| strip regions `0x4cfa7a`, `0x4cfc9c` | static initializer `0x42db40` (pointer in the startup table at `0x4a7a82`) | `GLRectangleRegion` objects (vtable `0x4a3264`, RTTI); `0x4cfa7e` the jolt strip rectangle, `0x4cfa94` its save buffer (`0x4774f0` in `0x429660`) |
| jolt state | — | `0x49ac44` active, `0x49ac45` re-arm after a transition, `0x4cfa58/5c` saved page origin, `0x4cfa60/64` current offset (x, y), `0x4cfa68/6c` lower/upper bound, word `0x4cfa70` accumulated shift |
| view rectangle | `0x42d89c`/`0x42d944` (dialog `0x45cff0`, never live) | `0x4cfa40..0x4cfa4c` 3D view rectangle, `0x4cfa50/54` its origin, defaults `0x4cfa28..0x4cfa3c` |

The module initializer `0x42db40` builds the records at `0x4cf9a8` as
4 × 32 bytes; the view file read by `0x429834` supplies their count and
contents ([The view file](#the-view-file)). The layout of the panel H and
of the layout L is in
[Layout and panel reader contract](#layout-and-panel-reader-contract).

## Classification table

Legend as in [hud.md](hud.md#legend): **D** draws, **U** presentation
state, **P** pure helper, **S** simulation, **Q** message ports, **I**
input/UI logic feeding simulation; +B stream B, +C clock `0x467828`, +A
audio, +H heap. Added: **G** graphics-driver pointer call
(`[0x4a57e8]` page origin, `[0x4a57ec]`/`[0x4a57f0]`/`[0x4a57f4]` page
select, `[0x4a5824]`/`[0x4a5828]`, `[0x4a5838]`), **R** RTL frame helper
`0x49630c`.

**Display-mode manager**

| VA | Role | Instr. | Calls per suite | Callers (how reached) | Class | Evidence |
|---|---|---:|---|---|---|---|
| `0x42a3f4` | request a transition | 11 | 0/0/0/19/1/1 | `0x432b14`, `0x432bc8` (Esc, F1–F6, F9, F10), `0x433a88`, `0x45cff0`, `0x45fa84` (death), `0x45feb8` (V, Ctrl+F, Esc case), `0x460870` (actions 12/15) | U | only when `D+0x18` = −1; table `0x42a417` (7 cases): req 0 at mode 0 → 0; req 1 at mode 1 → 1; req 2 unless mode 4 → 2; req 3 at mode 4 → 3; req 4: mode 0 → 4, mode 3 → 6; req 5: mode 0 → 5, mode 2 → 6; req 6 at mode 2 or 3 → 6 ([contract](#0x42a3f4d-req-a-display-mode-request)) |
| `0x42a4c4` | start a transition | 214 | 1000×5/937 | presentation `0x45fba4` (`0x45fbea`, `0x45fc9d`), death sequence `0x45fa84` (`0x45faf8`, logic frame) | U+D+**I**+G+R | `D+0x36` → `0x42d9e8`; `D+0x1d` ≠ 0 → decrement, return; video word `0x4d2566` set: horizontal page scroll for transitions 4–6 (`0x487e60`, `[0x4a57e8]`); else page-parity checks and the vertical-retrace wait `0x45c638` (port `0x3da`); `0x4d267e` ← 0; unless `D+0x1c`: table `0x42a768`: 0 → `0x429e60(D, mode+1)` (or `0x42a1f0` when `0x4d2538` = 2 or `0x4d3018` = `0x4d253e`), 1 → `mode−1`, 2 → `0x429e60(D, 4)`, 3 → `0x42a1f0(D, D+0x23)`, 4/5 → `0x42a1f0(D, 2/3)`, 6 → `0x42a1f0(D, 0)` and `0x431518`; `0x42a1f0` calls are wrapped so `D+0x14` keeps the old mode; with byte `0x4d2537` = 1 and camera `[0x4d24ea]+0x4a` = 0, transitions 4–6 add word constants (`0xca00`, `0x3600`) to the camera's attachment angle `+0x48`; `D+0x1c` ← 1 and `0x434604(H)` (widgets disabled) |
| `0x429e60` | switch the layout | 277 | 0/0/0/19/1/1 | `0x42a4c4` (3 sites), `0x42a1f0` | U+D+**I**+G | see [below](#0x429e60d-m-layout-switch-u) |
| `0x42a1f0` | switch the layout and redraw the panel art | n/r | never | `0x42a4c4` (5), `0x45cff0` (2), mission load `0x461608` (2) | U+D+**I**+G | m > 4 or (`0x4d2538` = 1 and m = 1) → nothing; `0x429e60(D, m)`; for m < 4: with flag `0x4d2680` (`-l`) `0x429c2c` (reloads art through a stream), panel bitmap blit (`0x4817c8`/`0x4816a8`), `0x486704`; `0x4316c0` (whole panel redraw); `D+0x14` ← m ([contract](#0x42a1f0d-mode-the-layout-set-listing-0x42a1f00x42a3c6)) |
| `0x42a9c0` | animate and finish a transition | 155 (map, tables not followed; 2,481 listing lines) | 1000×5/936 | presentation `0x45fba4` (`0x45fbcf`, `0x45fc2b`) | U+D+**I**+A+G+R | returns at once unless `D+0x18` ≠ −1 and `D+0x1c`; erases the old area and redraws the ports (DialogPort `+0x14`/`+0x18`, EventPort `+0x18`); `D+0x1c` ← 0; table `0x42af1b`: 0 → slide, `D+0x14` += 1, **`0x45b68c(0)`**; 1 → slide, `D+0x14` −= 1, **`0x45b68c(1)`**; 2 → `D+0x14` ← 4, `D+0x36` ← 1, volume of sound channel `0x2d` saved (`0x462fe0`) and set to 100 (`0x463014`, if `0x49f8f8`); 3 → `D+0x14` ← `D+0x23`, volume restored; 4–6 slides; the slides blit (`0x487e60`), present (`0x464a14` when `0x4d2566`) and flip pages in loops without clock; end: `D+0x18` ← −1, `D+0x1d` ← 2, `0x49ac45` → `0x42d2f8(10)`, `0x434634(H)` |
| `0x45b68c` | flight-key capture on/off | 53 | 0/16/0/20/2/2 | `0x45a5dc`, `0x42a9c0` (2) | **I** | nothing if the byte argument equals `0x49eaa5`; needs `0x45bd84()` ≠ 0; on: registers the hot-key callbacks `0x45a324`/`0x45a498` (`0x477b18`, table `0x6bb8b8`) unless `0x49eb61`; toggles `0x49eaa5`; off: clears the three 15-byte key-state arrays `0x4d2394`, `0x4d23a3`, `0x4d23b2`. `0x45a324` maps scan codes through `0x49eb0b` into those arrays and consumes the key only while `0x49eaa5` is set ([contract](#0x45b68con-the-flight-key-capture-switch)) |
| `0x431500` / `0x431518` | panel off / on | 8 / n/r | 0/0/0/1/1/1 / never | `0x429e60` / `0x429e60`, `0x42a4c4` | **I** | `H+0x20f` ← 1 / ← 0 (the byte makes `0x432bc8` return 0, `0x431410` false; [hud.md](hud.md)) |
| `0x435574` | move the port rectangles | 22 | 0/0/0/18/0/0 | `0x429e60` | U | port `+0x4a7`, `+0x4af` −= delta ([hud.md](hud.md)) |
| `0x434604` / `0x434634` | widgets disable / restore | 22 / 22 | 0/0/0/19/1/1 | `0x42a4c4` / `0x42a9c0` | U+D | [hud.md](hud.md) |
| `0x42d9e8` | redraw the border if pending | 14 | 0/0/0/1/1/1 | `0x42a4c4` | D | mode 4 → `0x42da08(D)`; `D+0x36` ← 0 |
| **`0x42da08`** | border around the 3D view | 91 | 0/0/0/2/2/2 | `0x429e60` (entering mode 4), `0x42d9e8`, `0x45cff0` | **D** | fills up to four bands between the view rectangle `0x4cfa40..0x4cfa4c` and the screen size `0x4d253e`/`0x4d2542` with colour word `0x4d3ba2` (`0x486704`); context clip flags saved/zeroed/restored; `+0x22c` ← address of a local (see [Observations](#observations-original-quirks)); [contract](#0x42da08-the-view-border-listing-0x42da080x42daf0) |
| `0x42db18` | 3D view visible in this layout | 13 | n/p | rendering `0x45fca8` (`0x45fd5a`) | P | contract below |
| `0x42db10` | view rectangle getter | 2 | n/p | `0x45e208` | P | EAX = `0x4cfa40` |
| `0x42d89c` / `0x42d944` | set / reset the 3D view rectangle | n/r | never | `0x45cff0` | U | copies a rectangle and origin into `0x4cfa40..0x4cfa54` and `0x4cfca4..`; in mode 4 also into the context clip and origin |
| `0x429820` | display-object getter | 2 | n/p | many | P | EAX = `[0x4cf99c]` |
| `0x45c638` | wait for vertical retrace | 5 | n/p | `0x42a4c4` | platform | `in al, 0x3da` until bit 3 set (bytes `66 ba da 03 ec a8 08 74 fb c3`); only when word `0x4d2566` = 0 |

**Jolt (screen shake) and panel scroll**

| VA | Role | Instr. | Calls per suite | Callers (how reached) | Class | Evidence |
|---|---|---:|---|---|---|---|
| `0x434010` | jolt on damage | 37 | 0/0/0/5/0/0 | `ground_collide` `0x418f74` (`0x4195a3`, slide damage), `0x4188c8` (`0x4189c6`, player flag `V+0xa3` and damage < 100) — logic frame | U+**C+B** | mode 4 → nothing; pending jolt → `0x42ea94`, `0x42d624`; `0x49b0fc` ← clock + 60; if `0x49b100` = 0: `0x49b100` ← clock + B%10, `0x42ea44`, `0x42d2f8(5 << [0x4d253c])`. The stack argument (always 0) is not read. `0x4188c8` calls it when the vehicle's player flag `+0xa3` (read as byte `V+0x92+0x11`) is set, the word in DI (a damage value, hypothesis) is below `0x64` and ESI is 0 or 1 (`0x4189a9..0x4189c6`) |
| `0x43408c` | jolt per frame | 61 | 1000×5/937 | panel frame `0x4327ac` ← rendering `0x45fca8` | U+**C+B** | while clock < `0x49b0fc`: palette flicker `0x42ea70` and re-arm, `0x42d4a8(B%5)`; at the end `0x42ea94`, `0x42d624` ([hud.md](hud.md)) |
| `0x42ea44` / `0x42ea70` / `0x42ea94` | damage palette on / flicker / off | 15 / n/r / n/r | 0/0/0/5/0/0 / n/p / n/p | `0x434010` / `0x43408c` / `0x434010`, `0x43408c` | U | swap the current palette `[0x49b020]` with `[0x49aef8]` through the palette setter `0x4303b0` (stores its argument in `0x49b020` and returns the previous value; call sites `0x429b6a`, `0x429b80` in the cockpit set-up, `0x42ea51`, `0x42ea7f`, `0x42eaa3` here; live 0/0/0/55/0/0); `0x4cfd08/0c` saved palettes, `0x49aef3` phase |
| **`0x42d2f8`** | start a jolt | 108 | 0/0/0/5/0/0 | `0x434010`, `0x42a9c0` (re-arm, 10), `0x45d85c` (5), `0x45dc50` (10) | **U+D+G** | contract-level summary [below](#jolt-trio) |
| **`0x42d4a8`** | jolt step | 131 | 0/0/0/105/0/0 | `0x43408c`, `0x45d85c`, `0x45dc50` | **U+G** | below |
| **`0x42d624`** | end a jolt | 148 | 0/0/0/5/0/0 | `0x429e60`, `0x434010`, `0x43408c`, `0x45d85c`, `0x45dc50` | **U+D+G** | below |
| `0x42d870` | jolt offset point | 13 | n/p | rendering `0x45fca8` (`0x45fd05`), `0x43fe44`, `0x440d74` | U | `0x4cfcbc` ← 0, `0x4cfcc0` ← s16 `0x4cfa70`, EAX = `0x4cfcbc`; first call sets byte `0x49add5` |
| `0x42d820` / `0x42d82c` / `0x42d854` | bump: reset / set / undo | 6 / 11 / n/r | 60/229/132/348/0/25 / 895/1000/1000/1000/0/379 / n/p | `0x434144` / `0x434194` / `0x434144`, `0x434194` | U | bump of the context origin y: `+0x224` −= old `0x4cfcb8`, += new (zero-extended word), `0x4cfcb8` ← new; undo subtracts and clears; reset only clears `0x4cfcb8` |
| **`0x43271c`** | move the cockpit panel | 57 | 0/0/0/128/0/0 | `0x429e60` (2), `0x42d4a8`, `0x42d624` | **U** | below; 128 = 105 (`0x42d4a8`) + 5 (`0x42d624`) + 18 (`0x429e60`, the same 18 as `0x435574`) |
| `0x452bec` → `0x45275c` | move the panel object | 10, 39 | 0/0/0/128/0/0 | `0x43271c` | U | panel `+0x22b`, `+0x22f`, `+0x233`, `+0x237` += delta; `0x452434(panel, delta)`; two `0x481350` results (width−1, height−1) go to locals that are never read |
| `0x452434` | move a rectangle | 14 | 0/0/0/384/0/0 | `0x43271c`, `0x45275c` | U | `+0`,`+8` += dx; `+4`,`+0xc` += dy |
| **`0x433968`** | move the scroll zones | 110 | 0/0/0/128/0/0 | `0x43271c` (when `H+0x23e`) | **U** | below |
| `0x481350` | surface property getter | 10 | 4/4/4/516/4/4 | `0x433770`, `0x433968`, `0x45275c`, `0x4544c0`, `0x45b758` | P | table `0x481369` (17 cases): 0 → `[s+8]`, 1 → `[s+0xc]`, other fields or three globals; > 16 → 0 |

**Mission start and other members of `0x42d*`**

| VA | Role | Instr. | Calls per suite | Callers | Class | Evidence |
|---|---|---:|---|---|---|---|
| `0x45d85c` | mission-start drop sequence | n/r | never (runs before the proxy arms) | mission load `0x461608` (`0x461f2c`, when word `0x4d2510` ≠ 0) | **S (temporary)**+D+C+B+A+H+R | [below](#0x45d85c-mission-start-drop-sequence) |
| `0x45d548` | prepare the intro | n/r | never | `0x461608` (`0x461df8`, same word) | U+D+H | palette and heap work (`0x4303c4`, `0x4308fc`, `0x47749c`); [contract](#0x45d548-the-intro-set-up) |
| `0x45dc50` | death/"world" fade with jolt | n/r | no entry | **none** | — | no direct call, no immediate operand, no DATA pointer (confirmed); loads bitmaps `death1`, `death2`, `world0`, starts a jolt (10), sound `0x26`, freeze word `0x4d24f2` ← 1, fades, loops on `0x430b34` with `0x42d4a8(rng_bounded(B, 10))`, then an alert dialog `0x455950`. Unreferenced (hypothesis: dead code, a computed address cannot be excluded) |
| `0x42d2b8` | debug cockpit-name option | n/r | never | main option parser `0x45e758` (`0x45e8bc`) | U | compares the argument with 13 names at `0x49ac64` (`ROADRUNNER` … `OGRE`, `TEST3`) via `0x496868`; on a match word `0x49ac4c` ← index, `0x49ac50` ← argument; read by the D constructor `0x429660` and `0x451ef0` |
| `0x42db40` | module initializer | n/r | never | startup table (`0x4a7a82`) | U+R | builds `0x4cf9a8` (4 × 32 bytes, element ctor `0x42dc88` via `0x49667c`), the two `GLRectangleRegion` objects, `0x49ac54..` |
| `0x42dcf0` | layout file reader | n/r | never | `0x429834`, `0x448cf0` | U+H | stream `0x472f14`/`0x472f64`, records into item lists, coordinates shifted by `0x4d253c` |

`0x45cff0` (F12/Alt+P dialog, [input.md](input.md)), `0x429834`,
`0x429660`, `0x429c2c`, `0x429de4` and `0x4316c0` are never live; they
are listed here only for their calls into this region.

## `0x429e60(D, m)`: layout switch (U)

cdecl, two slots. EBX = D, EDI = screen `[0x6c5ed0]`.

1. Nothing if m = `D+0x14` or m > 4 (signed).
2. If the jolt is active (`0x49ac44`): `0x42d624()` and `0x49ac45` ← 1.
3. **m = 4 (outside view).** `0x49ac45` ← 0; (`D+0x14` = 4 is tested again,
   unreachable after step 1); `D+0x23` ← old mode; `0x81` dwords from
   `0x4cfa98` to context `+4`; context `+0x208` ← 2; `0x4a5af8` ← 0; context
   clip `+0x210..+0x21c` ← `0x4cfa40..0x4cfa4c`, origin ← `0x4cfa50/54`;
   `D+0x14` ← 4; **`0x431500(H)` (panel off)**; `0x4cf9a0/a4` ← 0;
   `0x42da08(D)`.
4. **m < 4.** Old record O = R[old] (R[`D+0x23`] when the old mode is 4,
   then also **`0x431518(H)` (panel on)**). From mode 2 or 3 (unless byte
   `0x4d2538` = 2) the panel is first moved back by O's offset
   (`0x43271c(H, O+0x18)`, `0x435574(port, O+0x18)`). With word `0x4d2566`
   the page-0 origin is set through `[0x4a57e8]`. N = R[m]; unless the old
   mode was 2, 3 or 4 or `0x4d2538` = 2, the visible page origin is set to
   N's offset and the panel moved by O+0x18 − N+0x18 (O's offset counts as
   0 when `D+0x14` = −1) through `0x43271c` and `0x435574`. Then the item
   list of m (`0x81` dwords from `[D+0xc] + 0x204·m`) to context `+4`,
   `0x4a5af8` ← `[D+0x10][m]`, context `+0x208` ← 2, clip ← N+0..+0xc,
   origin ← N+0x10; unless `0x4d2538` = 2: `0x4cf9a0/a4` ← N+0x18 and added
   to the origin, else `0x4d2556/5a` ← N+0x18.
5. `D+0x14` is **not** written for m < 4; `0x42a1f0` writes it, and the
   transitions started by `0x42a4c4` let `0x42a9c0` change it at the end of
   the animation.

The own code is the row `display_switch` (`sim/src/hud_display.c`).

## `0x42a3f4(D, req)`, a display mode request

Listing 0x42a3f4..0x42a4c2. The jump table is at 0x42a417: 0→0x42a446,
1→0x42a433, 2→0x42a453, 3→0x42a462, 4→0x42a471, 5→0x42a48f, 6→0x42a4ad.

Nothing is stored while a request is pending (`[D+0x18]` != -1) or when
req > 6 (unsigned). Otherwise, with the current mode m = `[D+0x14]`:

| req | `[D+0x18]` becomes |
| --- | --- |
| 0 | 0 when m = 0 |
| 1 | 1 when m = 1 |
| 2 | 2 when m != 4 |
| 3 | 3 when m = 4 |
| 4 | 4 when m = 0; 6 when m = 3 |
| 5 | 5 when m = 0; 6 when m = 2 |
| 6 | 6 when m = 2 or 3 |

Registers: EAX = D. EDX = req, except for req 6, where it is m. ECX = 0
after the req-0 store. Own code: row `display_request`
([Cockpit layout and panel readers, the display object's readers](#cockpit-layout-and-panel-readers-the-display-objects-readers)).

## Jolt trio

All three: cdecl; nothing when the display mode is 4; no clock, no RNG, no
audio, no heap (confirmed). The page-origin calls go through the driver
pointer `[0x4a57e8](page, point)`; the strip save/restore through the
graphics library `0x488012`/`0x488143`, fills through `0x486704`. The
register-level contracts are in
[The jolt and the bump as own code](#the-jolt-and-the-bump-as-own-code).

- **`0x42d2f8(amp)`** (word argument). Also nothing while a transition is
  started (`D+0x1c`). Base point = (`0x4d3018`, `0x4d301c`) when word
  `0x4d2566` is set, else (`0x4cf9a0`, `0x4cf9a4`); it goes to the current
  offset `0x4cfa60/64` and the saved origin `0x4cfa58/5c`. Bounds by mode:
  mode 0: lower `0x4cfa68` = `0x4cf9a4` (+ `0x4d301c` with `0x4d2566`),
  upper `0x4cfa6c` = lower + amp; mode 1: upper = `0x4cf9a4`, lower = upper
  − amp; modes 2/3: unless `0x4d2566`, `0x4cfa5c` and `0x4cfa60` ← 0; the
  strip of the region `0x4cfa7a` (fields `+4`/`+8`) is saved into
  `[0x4cfa94]` (`0x488012`) and filled with colour `0x4d3ba2`, the page is
  re-selected, then the mode-0 bounds; other modes (only −1, before the
  first layout): no bounds. Then `0x4cfa70` ← 0, `0x49ac44` ← 1.
- **`0x42d4a8(step)`** (only the low word of the dword argument is used
  except where noted). Active and not mode 4: down = cur − lower, up =
  upper − cur (16-bit, cur = low word of `0x4cfa64`); if down < up (signed
  words) s = min(step, up) else s = −min(step, down) (the comparison is
  between the sign-extended difference and the zero-extended step).
  `0x4cfa64` += s (dword); page origin ← page base + (`0x4cfa60`,
  `0x4cfa64`); every item of the current mode's list (`[D+0xc] + 0x204·m`)
  is moved by −s (kind = dword at the item; kind 0: dword at item+0x10;
  kind 2: word through the pointer at item+4 when that is not null; other
  kinds untouched; a null item pointer faults at the kind read);
  `0x4cfa70` += s; row table `[0x4a5af8]` (if not null): `+0` += 8·s,
  `+4` −= s; context `+0x218` and `+0x21c` −= s; `0x43271c(H, (0, −s))`.
- **`0x42d624()`**. Active and not mode 4: page origin ← page base + saved
  `0x4cfa58/5c`; the items, the row table and the context fields are moved
  back by the accumulated `0x4cfa70`; modes 2/3: the saved strip is put back
  into both pages (`0x488143` twice, `+0x88` and `+0x8c` selected through
  `[0x4a57f0]`); `0x43271c(H, (0, acc))`; `0x4cfa70` ← 0; `0x49ac44` ← 0.

Live evidence agrees: in c3_09 the five jolts (`0x434010` = `0x42ea44` =
`0x42d2f8` = `0x42d624` = 5) take 105 steps, every step and every end reach
`0x43271c` (105 + 5), and the re-arm from `0x42a9c0` never happened (else
`0x42d2f8` would exceed 5).

**Order in a frame (confirmed).** Logic frame: `ground_collide` →
`0x434010` → `0x42d2f8` (arm). Rendering frame `0x45fca8`: panel frame
`0x4327ac` → `0x43408c` → `0x42d4a8` → `0x43271c` (panel, widgets, zones);
the world view's origin is offset by `0x42d870`'s point at `0x45fd05`.
Presentation frame `0x45fba4`: `0x42a9c0`, palette, present, `0x42a4c4`.

## `0x43271c(H, delta)` and `0x433968(H, delta)` (U)

`0x43271c`: cdecl (H, pointer to (dx, dy)). `0x452bec(H+0, dx, dy)` → the
panel object and its rectangle move by +delta; canvas origin
`[H+4]+0x220/+0x224` −= delta; if byte `H+0x23e`: `0x433968(H, delta)`;
every widget `H+0x246[i]`, i < dword `H+0x25a`, moves by −delta
(`0x452434`). Registers at return are dead at all callers.

`0x433968`: w, h = low words of the screen's width and height
(`0x481350([0x6c5ed0], 0/1)`, sign-extended). Per axis: dx > 5 → (w, +1),
dx < −5 → (−w, −1), else (0, 0) (signed dwords); offset = amount − d. For
each of the three `ScrollTrigger` rectangles `[H+0x21a+4i]`: width and
height are taken first; direction +1 → x2 ← x1, x1 −= width; −1 → x1 ← x2,
x2 ← x1 + width (same for y); then both corners += offset. Only the
trigger rectangles are written. All branch edges are reachable in
principle (jolt steps are at most 4 pixels, ends and layout switches can
exceed 5).

The own code is the rows `panel_move` (`0x43271c`) and `panel_slide`
(`0x433968`).

## `0x42db18()` (pure helper)

Listing 0x42db18..0x42db3c. No arguments, no entry register read.
D = `[0x4cf99c]`; EDX = `D+0x14`·32; ECX = `D+4`. EAX = 1 if the dword
`[ECX + EDX + 0xc]` (bottom of the current layout's clip rectangle) > 0
(signed) or byte `D+0x1c` ≠ 0, else 0. Reads only. The rendering frame
skips a block (`0x45fd63..0x45fdbb`, camera set-up `0x48c2e4` and more)
when AL = 0 (hypothesis: the world draw). Own code: row `view_visible`.

## `0x45d85c`: mission-start drop sequence

Called from the mission load when word `0x4d2510` (block `0x4d24bc+0x54`)
≠ 0; `0x45d548` runs earlier under the same test. P = `[0x4d24e6]`.

1. Loads `intro.dba` (`0x45cdf4`), sound `0x21`; saves P's position
   `+0x26..+0x2e` and angles `+0xc..+0x10` (the angles are never written
   back and not changed here).
2. For z from P.z − 4000 while z < P.z, step 35 (115 iterations):
   P+0x26/+0x2a ← saved x/y, P+0x2e ← z; `camera_update(0x4d24ea)` (hook),
   projection (`0x48c2e4`, `0x48c444`, `0x48c5fc`, `0x48c6d0`) to clip the
   view at the projected ground; rendering frame `0x45fca8`, message pump
   `0x464c78`, intro bitmap and fills, HUD draw `[H+0x1f5]+8`, palette fade
   `0x430b34` while `0x49f138`, presentation frame `0x45fba4`.
3. P's position ← the saved one (exact restore). Sound stop `0x462ac4(0x21)`;
   jolt `0x42d2f8(5)`, sound `0x29`; until clock ≥ start + 30:
   `0x42d4a8(B%5)` (stream B `0x4d260b`), `0x45fca8`; `0x42d624`.
4. Frees `[0x4d274c]`, `[0x4d2750]` (`0x47758c`), shuts down `[0x4d2744]`,
   `[0x4d2748]` (virtual `+0x18(…, 3)`), byte `0x49aef4` ← 1.

The player's position is changed only inside the sequence; the rendering
frames inside it run `0x41b130` and the camera ring 115 times with the
intermediate heights (the rendering frame writes simulation state,
[hud.md](hud.md)). All of this happens before the proxy arms.

**Coupling** (`0x45d85c..`, read at `0x45d919..0x45db76`):
`sfx_play(0x21)`; the descent loop renders through `0x45fca8` and presents
through `0x45fba4` per step (each render-root call is a full HUD walk with
its coupling, hud.md "The HUD line in the own process"); then
`sfx_stop(0x21)`, C = t (end = t + 30), jolt start `0x42d2f8(5)`,
`sfx_play(0x29)`, and the loop: B → `0x42d4a8(B mod 5)`, render root, C,
until C ≥ end; then `0x42d624`. The function index has no entry for
`0x45dc50` (no call relation), so the sites of that dead function
(`0x45dcc3` C, `0x45dcd7` `sfx_play(0x26)`, `0x45dd0c` C, `0x45dd1c`
bounded B) are counted under `0x45d85c` there.

The own code is the row `drop_sequence` (`sim/src/cockpit.c`); its
instruction-level contract is
[cockpit.md "Drop sequence 45d85c and panel constructor 431bf8"](cockpit.md#drop-sequence-45d85c-and-panel-constructor-431bf8).
The intro set-up `0x45d548` is
[below](#0x45d548-the-intro-set-up).

## Simulation-relevant edges

The five functions `0x42d2f8`, `0x42da08`, `0x42d624`, `0x42d4a8` and
`0x433968` replace no compared state:

| Function | Class | Live | Note |
|---|---|---|---|
| `0x42d2f8` | U+D+G | c3_09 only (5) | graphics library and driver calls |
| `0x42da08` | D | c3_09, c4_08, c3_10 (2 each: the death's switch to mode 4) | fills only |
| `0x42d624` | U+D+G | c3_09 only (5) | |
| `0x42d4a8` | U+G | c3_09 only (105) | |
| `0x433968` | U | c3_09 only (128) | rectangle arithmetic on presentation state (callee `0x481350` is a pure getter) |

The display group `0x42a3f4`, `0x42a4c4`, `0x429e60`, `0x42a1f0`,
`0x42a9c0`, the jolt trio, `0x42da08`/`0x42d9e8`, `0x43271c` with
`0x452bec`/`0x45275c`/`0x452434`/`0x433968`, and
`0x434010`/`0x43408c`/`0x434144`/`0x434194` shares D, the graphics context,
the screen pages and the driver pointers. What any reimplementation must
keep (or reproduce) because it reaches simulation input or compared state:

- the panel-off byte `H+0x20f` writes (`0x431500`/`0x431518`) and their
  timing (death sequence in the logic frame, otherwise the presentation
  frame);
- `0x45b68c` at the end of the 0↔1 transitions (two presentation frames
  after the Esc key: request in logic frame N, start in presentation frame
  N, finish in presentation frame N+1);
- `D+0x14` as read by `0x432bc8`, `0x45feb8`, `0x460870`;
- the clock and stream-B calls of `0x434010`/`0x43408c`, in count and
  order;
- the camera attachment-angle adds of `0x42a4c4` when byte `0x4d2537` = 1
  (presentation, but `camera_update` is a hook);
- the vertical-retrace port read `0x45c638` must not be reproduced
  natively.

## Transitions, slides, jolt, bump and the shield palette in the HUD line

The display part of the HUD line contract
([hud.md](hud.md#the-hud-line-contract--coupling-set-and-frame-writes)
has the coupling-site table and the per-frame order). Sources: the pinned
listing of `VER95/DBSIM.EXE` (SHA-256 `8561aa47…c4ad`), a byte scan of the
call sites, the function index. C = clock read `0x467828`, B = stream-B
draw, A = sound-API call; D = `[0x4cf99c]`, H = `[0x49b088]`.

### Transitions `0x42a4c4` / `0x42a9c0`: no coupling call (confirmed static)

Neither function nor anything it calls directly contains a site of the
clock, stream B or the sound API (byte scan, `reach.py`). Their indirect
calls were followed one level:

- `0x42a9c0` redraws the EventPort through its slot `+0x18` (`0x436cec`)
  six times **before** it clears `D+0x1c` (`0x42aef9`); `0x436cec` returns
  at once while `D+0x1c` is set (`0x436d0b`), so its blink clock read
  (`0x436f27`) cannot happen there. After the clear, the slide loops call
  only the DialogPort's slots `+0x14` (`0x437044`) and `+0x18`
  (`0x437094`) (the port pointer `[EBP−0ACh]` is `0x433158`'s, stored at
  `0x42af02`), which have no site.
- The whole-panel redraw `0x4316c0` (from `0x42a1f0`) calls slot 1 of the
  panel objects; the slot-1 bodies of the eleven classes ([cockpit.md](cockpit.md#the-panel-element-classes)
  table) and the MFD and HDD page calls inside them (`0x43eef4` radar
  draw, the HDD page slot `+0xc`) have no site.
- The volume save and set of channel `0x2d` (`0x462fe0`, `0x463014`, in
  `0x42a9c0` cases 2 and 3) are SFX-manager state, not one of the sound
  hooks (audio.md "What can change the oracle's comparison": not compared).

So the HUD line needs no coupling call from the transitions, but it needs
their **state writes**, because the HUD gates read them: the display mode
`D+0x14`, the pending transition `D+0x18`, the transition byte `D+0x1c`,
the cool-down `D+0x1d`, `D+0x23`, `D+0x36`, the panel-off byte `H+0x20f`
(through `0x429e60`) and the key capture `0x45b68c`
([Simulation-relevant edges](#simulation-relevant-edges)). Order within a
pass: the presentation frame `0x45fba4` calls `0x42a9c0` (finish) before
the present and `0x42a4c4` (start) after it (`0x45fbcf`/`0x45fbea` and
`0x45fc2b`/`0x45fc9d` on its two paths), so a transition started in pass n
is visible to the render walk of pass n + 1 as `D+0x1c` = 1 (the gauges
then draw even when not visible, the HUD draw runs in mode 4, the
EventPort draw returns, the HDD marks dead wingmen) and is finished in
pass n + 1's presentation frame. The death sequence `0x45fa84` calls
`0x42a4c4` from the logic frame ([Classification table](#classification-table)).

**Frame writes.** The slide loops present inside themselves: `0x42a9c0`
has 21 calls of `0x464a14`, each behind a test of the word `0x4d2566`
(e.g. `0x42b12b`); these are frame-capture records with a `site` other
than the main loop's `0x45fbe4`, compared one by one (presentation.md,
"Frame capture per suite"). They move page origins through the driver
pointers `[0x4a57e8]`, `[0x4a57ec]`, `[0x4a57f0]`, `[0x4a57f4]` and blit
with `0x487e60`; own code is accepted on those records' index and palette
hashes and their page origin `S`.

### Jolt and bump: the coupling exactly

From `0x434010..0x434088`, `0x43408c..0x434143`, `0x434144..0x434191`,
`0x434194..0x434225` (globals: jolt end `0x49b0fc`, flicker deadline
`0x49b100`, bump start `0x49b634`, bump end `0x49b638`; mode = `D+0x14`
read through `0x429820`):

| Function | When | Coupling, in order | State written |
|---|---|---|---|
| `0x434010` (hit; logic frame) | mode ≠ 4 | pending jolt (`0x49b0fc` ≠ 0) first ends it (`0x42ea94`, `0x42d624`); C = t; `0x49b100` = 0: B → `0x49b100` ← t + (B as unsigned 16 bits) mod 10, `0x42ea44`, `0x42d2f8(5 << [0x4d253c])` | `0x49b0fc` ← t + 60 |
| `0x43408c` (panel frame) | always | mode 4: `0x49b0fc` ← 0. `0x49b0fc` ≠ 0: C = t; t < end: [t > `0x49b100`: `0x42ea70`, B, `0x49b100` ← t + B mod 10], B → `0x42d4a8(B mod 5)`, AL = 1; t ≥ end: `0x49b0fc` ← 0, AL = 1. `0x49b0fc` = 0 and `0x49b100` ≠ 0: `0x42ea94`, `0x49b100` ← 0, `0x42d624`, AL = 1. Else AL = 0 | as listed |
| `0x434144` (ground contact; logic frame) | mode ≠ 4 | `0x42d854`, `0x49b638` ← 0, C = t, `0x49b634` ← t, `0x49b638` ← t + 60, `0x42d820(10 << [0x4d253c])` | as listed |
| `0x434194` (panel frame) | always | mode 4: `0x49b638` ← 0. C = t; t > end (unsigned): end ← 0. Then end = 0: AL = 0, nothing more; t < end: `0x42d82c(v)` with v = word `0x49b046[(t − start)·10 / 60]` (unsigned division), halved (`sar`) when word `0x4d253c` = 0, else shifted left by `[0x4d253c]` − 1; AL = 1; t = end: `0x42d854`, end ← 0, AL = 0 | `0x49b638` |

(The B value enters every remainder as its low 16 bits zero-extended
(`movzx`), divided signed by 10 or 5: results 0..9 and 0..4.) Quirk of
`0x434194`: the undo `0x42d854` runs only in a pass whose clock equals the
end exactly; a pass past the end only clears it, leaving the last offset of
the table in the context origin until the next bump starts with
`0x42d854` (static; whether the table's last entries are 0 was not read).
The logic-frame calls come from `ground_collide` (`0x4195a3`),
`mech_part_hit` (`0x4189c6`) and `ground_effects`; they run as guest calls
inside those hooks, so their C and B fall between the simulation's own
draws in the original order.

**Frame writes:** the jolt moves the page origin S of the present
(`0x42d2f8`/`0x42d4a8`/`0x42d624`, section "Jolt trio") and the damage
palette swap changes the colour table (`0x42ea44`/`0x42ea70`/`0x42ea94`);
the bump moves the context origin `+0x224` for the frame's later draws.
Both reach the frame-capture hash.

### The shield palette `0x443918`

No coupling (confirmed: no site in its closure; only `multiply_q10` and
`0x4303c4`). Frame writes: the six palette entries `0x42..0x47` with the
dirty range, uploaded by the flush `0x43048c` in the next presentation
frame, so they reach the frame capture's `palette_sha256` one present
later than the draw that requested them. Runs from the Shields gauge draw
when its redraw counter is 2 (`0x443770`) and from the full redraw
`0x443758`.

### What the own code keeps

The transition state machine, the jolt and bump timers with their C and B,
and the panel-off byte are kept as in the original. The pixel work (the
slides, the border `0x42da08`, the palette writes `0x443918` and
`0x42ea44..`, the jolt and bump effects) is drawn through the graphics rows
as well.

## The display transitions and the shield palette as own code

`sim/src/display.c` reconstructs the transition pair and the shield
palette from the listing of the reference DBSIM.EXE. Each function names
its listing range. Service rows (`sim/dispatch/services/`):

- `0x42a4c4` (`display_leave`);
- `0x42a9c0` (`display_enter`, with the slides `0x42a9c0..0x42d2b7`);
- the small callees `0x42d9e8` (border off),
  `0x434604`/`0x434634` (all cockpit buttons down/up) and
  `0x434928`/`0x434944` (one button down/up);
- the retrace wait `0x45c638`;
- `shield_palette` `0x443918` (`es2_sim_service_shield_palette`).

The camera read `0x45edf8` is served by the hook `camera_current`
(core/src/camera_view.c); the own code calls it and adds no row for it.
The frame `0x45fba4` (present.c) reaches the two transitions through
these rows.

Contract (what the own code keeps):

- Every callee stays a call at the original's site and in the original
  order:
  - the scroll `0x487e60` (gfx.c's row);
  - `0x42a1f0`/`0x429e60`/`0x431518`/`0x42da08`;
  - the sound level pair `0x462fe0`/`0x463014`;
  - the port calls (DialogPort `+0x14`/`+0x18`, EventPort `+0x18`) and the
    driver slots `0x4a57e8..0x4a57f4`, `0x4a5824`/`0x4a5828`;
  - the callees served by other rows: the flight-key capture `0x45b68c`
    (`flight_capture`), the jolt start `0x42d2f8` (`cockpit_jolt_start`),
    the port redraw `0x437164` (`hud_port_redraw`), the cockpit step and
    done notices `0x433f7c`/`0x433ee8` (`cockpit_step`, `cockpit_done`)
    and the hook `0x47e0b0`.
- The present `0x464a14` becomes `es2_sim_present_frame` with the
  original call's return address as the site. The 21 slide sites are
  named in display.c. The present runs only when the word `0x4d2566` is set.
- The shield palette keeps its six entries `0x42..0x47`: it calls
  `0x4303c4(0x42, 6, array)` through its row. The array order and the
  sign-extended byte deltas of `0x49c9cb..0x49c9d0` are the original's.
  The HUD reaches it through the dispatch (`hud_cockpit.c`, `panel.c`).
- The original's stack locals live in a frame of the call area: the scroll
  region, the delta and point, the origin temporaries and the palette
  array.
- Leave returns the FS:0 head in EAX, as the original does (it is
  `[ebp-0x38]` at every exit).
- The res-2 page-swap copies of transitions 4, 5 and 6 skip
  `0x4a3646 <- 1`; the paged non-res-2 path sets it.

Refusals. The original reads undefined stack data or dereferences null in
these cases, so the own code refuses them:

- enter with a request above 6 (`0x42aaa5`). Unreachable: `D+0x18` is
  written only with −1 (`0x4297ab`, `0x42d260`) and 0..6
  (`0x42a3f4..0x42a4ba`), and enter skips −1;
- a view without a graphics context (enter `0x42aa49`, leave `0x42a5e7`),
  a NULL access ([own_process.md](../reference-game/own_process.md)).

Platform deviation. `0x45c638` polls VGA port `0x3da` for the vertical
retrace (`in al, dx`). That is not possible under Windows NT, so the own
row does nothing. The presenter paces the frames. No caller reads AL.

### The hook `0x47e0b0`

`0x47e0b0` is the registry hook `multiply_q10`. `display.c` calls it
through the dispatch (HCALL), so the display path needs no row of its
own.

### Evidence

In vitro only, not live: `//tools/reference-game/invitro:differential_display_test`
(`display_cases.h`, `--display`). It runs on synthetic worlds against the
original machine code: the display D with its records, two views with a
GC, the cockpit root with up to five buttons, the ports and the palette.
Stubs record the callees outside these rows. All identical:

| Part | Cases |
| --- | --- |
| rows | 600 |
| shield | 1500 |
| leave | 3000 |
| enter | 4000 |
| frame `0x45fba4` | 2000 |

The comparison covers EAX (frame excepted), DATA `0x497000..0x6cc000`,
the world bytes and the stub log. The stub log records each callee with
its arguments; for the scroll it records the region, the point, the GC's
`+0x208`/`+0x20c` and the counter. Coverage counters require:

- every request (-1..6 for enter, -1..7 for leave);
- res 0/1/2, kind 1 or not, paged or not, the port flag;
- the cool-down, the border and started;
- sound and jolt;
- the shield's high/low halves and both thresholds;
- every stub letter for each part.

The scroll's pixels are not compared here; the graphics suite's `--gfx`
tests the scroll row. The frame's EAX is the last callee's in the
original; no caller reads it, and the frame row leaves it 0. A mutation of
the case-3 present site was caught (enter case 40). The frame part's
kind-1 path is described in
[The frame with `[0x4d2537]` = 1](#the-frame-with-0x4d2537--1).

## The jolt and the bump as own code

`sim/src/cockpit.c` has five service rows for the functions behind the hit
entries:

- `0x434010` (`cockpit_jolt_hit`);
- `0x434144` (`cockpit_bump_hit`);
- `0x42d2f8` (`cockpit_jolt_start`);
- `0x42d4a8` (`cockpit_jolt_step`);
- `0x42d820` (`cockpit_bump_reset`).

The per-frame halves are other rows, called through the dispatch:

- `0x43408c`, `0x434194` (`src/hud_frame.c`), the jolt end
  `0x42d624` (`src/hud_display.c`), the GC bias `0x42d82c`/`0x42d854`, the
  jolt record `0x42d870` and the panel move `0x43271c`;
- the palette flashes `0x42ea44`/`0x42ea70`/`0x42ea94`;
- the graphics rows: the strip save `0x488012`, the fill `0x486704` and
  the restore `0x488143`.

The contract below is read from the listing (`llvm-objdump -d -M intel`
of the reference DBSIM.EXE). Each statement is confirmed in vitro by
`//tools/reference-game/invitro:differential_cockpit_test` (`--cockpit`;
see [Evidence](#evidence-1)). That is not a live check.

### Common to all five

- D is the display returned by `0x429820` (`[0x4cf99c]`); its mode is
  `D+0x14`.
- "Mode 4" means the full-screen view.
- The coupling sites are kept in their original order, as in the coupling
  table above: C at `0x434036`, B at `0x434053`, C at `0x434161`.

### `0x434010()`, the hit's jolt

1. In mode 4, EAX is 4 and nothing else happens.
2. If a jolt is running (end time `[0x49b0fc]` ≠ 0), call `0x42ea94` and
   then `0x42d624`.
3. Read C. The end time becomes C + 60.
4. If the flash time `[0x49b100]` is already set, return with EAX = C + 60.
5. Otherwise draw B. The flash time becomes C + (B & 0xffff) mod 10.
6. Call `0x42ea44`.
7. Call `0x42d2f8(5 << (word [0x4d253c] & 31))`. EAX is that call's result.

The callers push one argument, which is not read. No caller uses the
result.

### `0x434144()`, the hit's bump

1. In mode 4, EAX is 4 and nothing else happens.
2. Call `0x42d854`.
3. The end time `[0x49b638]` becomes 0.
4. Read C.
5. The original compares C against the end time with `jb` (`0x43416c`).
   That branch is unreachable, because the end time was just cleared; the
   own code keeps it (it would return with EAX = C).
6. Start `[0x49b634]` becomes C. The end time becomes C + 60.
7. Call `0x42d820(10 << (word [0x4d253c] & 31))`. EAX is 0.

### `0x42d2f8(amp)`, the jolt start (locals `ebp-0x10..-4`)

**Early exit.** In mode 4, or with the transition byte `D+0x1c` set,
nothing happens and EAX is D.

**Origin.** The origin is the page origin (`0x4d3018`, `0x4d301c`) when
word `0x4d2566` is set, otherwise the mode origin (`0x4cf9a0`,
`0x4cf9a4`). It is copied to `0x4cfa60/64`, then to `0x4cfa58/5c`.

**By mode** (D is re-read with `0x429820`; the dispatch is unsigned):

- **Modes 2 and 3**, in this order:
  1. If not paged, `[0x4cfa5c]` and `[0x4cfa60]` become 0.
  2. GC+0x208 and GC+0x20c are saved and cleared.
  3. Call `0x488012(0x4cfa7e, [0x4cfa94])`.
  4. Local `[ebp-0x10]` becomes 0, and GC+0x22c points at it.
  5. Local `[ebp-0xc]` becomes the sign-extended word `[0x4d3ba2]`.
  6. Call `0x486704(0x4cfa82)`.
  7. V+0x3c becomes V+0x88, where V is `[0x6c5ed0]` read before the calls.
     If V is still current, `[0x4a368a]` takes the same value and the
     driver slot `[0x4a57f0]` is called with it.
  8. GC+0x208 and GC+0x20c are restored.
  9. Continue as mode 0.

  GC+0x22c is left pointing at the dead local, as in the original.
- **Mode 0.** Low `[0x4cfa68]` becomes `[0x4cf9a4]`, plus `[0x4d301c]` when
  paged. High `[0x4cfa6c]` becomes low + (amp & 0xffff). EAX is
  `[0x4cf9a4]`.
- **Mode 1.** High becomes `[0x4cf9a4]`. When paged, low is increased by
  `[0x4d301c]`; this is a dead store that is kept. Low then becomes
  `[0x4cf9a4]` − (amp & 0xffff), and EAX holds that value.
- **Mode 5 or more.** EAX is D.

**End** (all modes except the early exit): word `[0x4cfa70]` becomes 0 and
byte `0x49ac44` becomes 1.

### `0x42d4a8(step)`, one shake step (locals `ebp-0x18..-4`)

**Early exits.**

- If byte `0x49ac44` is 0, nothing happens and EAX keeps its entry value.
- In mode 4, EAX is 4.

**Choosing the shift d.** Take the words y = `[0x4cfa64]`, down = y −
`[0x4cfa68]`, up = `[0x4cfa6c]` − y, and s = the low word of `step`.

- If down < up (as signed words), d = s when up (signed) > s (unsigned),
  otherwise d = up.
- Otherwise d = −s when down > s, otherwise d = −down.

d is used as a signed word from here on.

**Effects, in order:**

1. `[0x4cfa64]` is increased by d.
2. Call the driver slot `[0x4a57e8]` with the view's page `[V+0x8c]` and a
   local point. The point is that page's origin `V+0x48+8·page`, offset by
   (`[0x4cfa60]`, `[0x4cfa64]`).
3. Walk the mode's list `[D+0xc] + 0x204·mode` (a count, then entries; the
   loop compare is signed). For each entry e, read the kind `[e]`; e is
   read without a null check.
   - Kind 0: the dword at e − 4 + 0x14 is decreased by d.
   - Kind 2: the word at `[e − 4 + 8]` is decreased by d, if that pointer
     is not null.
   - Any other kind: nothing.
4. Word `[0x4cfa70]` is increased by d.
5. If the row record `[0x4a5af8]` is set, +0 is increased by 8d and +4
   (after re-reading the pointer) is decreased by d.
6. GC+0x218 and GC+0x21c are decreased by d.
7. Call `0x43271c([D+0x1f], &(0, −d))`. EAX is that call's leftover.

### `0x42d820(v)`

`[0x4cfcb8]` becomes 0 and EAX is 0. The argument is not read.

### Strip restore of the jolt end `0x42d624`

In modes 2 and 3 the original restores the strip on both pages with
`0x488143(0x4cfa7e, 0x4cfa82, [0x4cfa94])` (`0x42d76b`, `0x42d7a5`). Each
restore comes before the page select of V+0x88 and of V+0x8c
respectively. The own jolt end (`src/hud_display.c`) calls these restores
through the graphics row `gfx_restore`, so the drawn bytes are the
original's. A jolt end without the restores fails the in-vitro test
(single case 2 on `0x42d624`: "the stand-in calls differ").

### Evidence

`differential_cockpit_test` compares every call on synthetic random
states, with recording stand-ins on both sides for:

- the clock and stream B (scripted answers);
- `0x4303b0`, `0x488012`, `0x486704` (including the colour record it is
  handed), `0x488143` and `0x481350`;
- the two driver slots, including the point they are handed.

The stand-ins for the fill and the restore can switch `[0x6c5ed0]` to
another view. Stand-ins at VAs that have a service row are swapped in
after the seal (found by scanning the row table).

It compares four things:

- the stand-in logs (call order and arguments);
- DATA and the heap;
- the world;
- EAX: the full register for `0x434010`, `0x434144`, `0x42d2f8`,
  `0x42d820`, `0x42d82c`, `0x42d854` and `0x42d870`; AL only for
  `0x43408c` and `0x434194`; not at all for `0x42d4a8` and `0x42d624`
  (the panel move leaves EAX unset; no caller reads it).

GC+0x22c is compared only as "written or not", because the dead local
lies on a different stack on each side.

6000 single calls and 2000 sequences (a hit, then 2 to 8 frames, then
random calls) are identical. Every branch key the test predicts for the
first call is reached (33 keys). That covers every reachable branch of the
five functions both ways, the view-switched paths of `0x42d2f8` and
`0x42d624`, and the list kinds 0, 2, 2-null and other. A planted mutation
(kind 2 changed from "− d" to "+ d" in `0x42d4a8`) produced "world
differs" on single case 33.

The power-on sweeps `0x4437cc`, `0x440eac` and `0x441db0` are widget draw
helpers outside these rows.

## The capture switch, the done step, the surface frame and the intro set-up

| VA | Row | File | Listing |
| --- | --- | --- | --- |
| `0x45b68c` | `flight_capture` | `sim/src/display.c` | `0x45b68c..0x45b732` |
| `0x433ee8` | `cockpit_done` | `sim/src/display.c` | `0x433ee8..0x433f7b` |
| `0x45d548` | `intro_setup` | `sim/src/cockpit.c` | `0x45d548..0x45d859` |

`display_cases.h` and `cockpit_cases.h` put stand-ins at these VAs and
swap them after the seal. `sim/src/input.c` keeps a private copy of the
`0x45b68c` logic in the input set-up `0x45a5dc` (call at `0x45a655`);
that copy is not a row.

### `0x45b68c(on)`, the flight-key capture switch

Only the low byte of the slot counts:

1. If the byte equals `[0x49eaa5]` (captured), nothing happens.
2. Otherwise it looks up the current keyboard: the id `[0x4d249c]`. If the
   id is nonzero, the record is `[0x4d2468 + 4·id]`. If there is no
   keyboard, EAX is 0 and nothing else happens.
3. If `on` is set and the skip byte `[0x49eb61]` is clear, it makes two
   hot-key registrations `0x477b18(K, 0x45a324, 0x49eb0b)` and
   `0x477b18(K, 0x45a498, 0x49eb35)`. `0x477b18` takes the first free one
   of the ten 12-byte entries at `0x6bb8b8` and stores used = 1, `+8` = the
   callback and `+4` = the list. It returns the entry's address with
   AL = 1, or 0 when all ten entries are used.
4. `[0x49eaa5] ^= 1`. If it is now 0, the key arrays `0x4d2394`,
   `0x4d23a3` and `0x4d23b2` (15 bytes each) are cleared through memset
   `0x49681a`.

EAX is the second registration's result. On the other paths it is the
caller's EBX with BL = 1, which a service cannot see; the row returns 1.
No caller reads EAX:

- `0x42a9c0` calls it at `0x42b55c` (with 1) and `0x42bb34` (with 0);
- `0x45a655` discards it;
- the modal callbacks `0x461250` and `0x461274` save and restore the
  capture byte on the stack at `0x4d2a80[[0x49f704]]`. `0x461250` calls
  `0x45b68c(0)` and `0x461274` calls `0x45b68c(saved)`. Neither callback
  has a row.

### `0x433ee8()`, the cockpit done step

The twin of `cockpit_step` `0x433f7c`, using the record at `H+0x23a`.
H = `[0x49b088]`.

If `[H+0x226]` equals H+0x232: `[H+0x22a]` ← H+0x23a; EAX H, EDX H+0x23a.

Otherwise:

1. `[H+0x226]` ← H+0x23a.
2. P = `[H+0x23a]`, link = `[P+4]`, q = link + 4 (0 without a link).
3. The original calls one of two callees with (q, P+8): the empty
   `0x48992e` when the page word `[0x4d2566]` is set, else `0x486e70`.
   Both bodies are empty, so the row does not call them.
4. Registers:
   - paged: EAX is H with a link, else P; EDX is P with a link, else P+8;
   - unpaged: EAX q, EDX P+8;
   - ECX q in both cases.

The callers are the slides of `0x42a9c0`.

### The frame with `[0x4d2537]` = 1

The frame part of `differential_display_test` runs the kind-1 path through
`0x481224` (frame_buffer.c's `surface_select`) on both sides with real
surfaces:

- V and V2 have format `+0x30` = 0 or 9 and a clip record at `+0x40`.
- The pair table at `0x4a5840` points at a 36-entry driver table. Its
  slots 15–18 and 30–31 are permuted, so any wrong slot shows in the stub
  log.
- `[0x4d2568]` is V or V2.

Pristine pairs at `0x4a5840`: (0, 0), (1, 0), (3, `0x4a586c`), then −1;
`[0x4a583c]` = 0x24.

### `0x45d548()`, the intro set-up

Called by the mission load before the drop sequence `0x45d85c`:

1. It makes two palette objects of 0x14 bytes, stored in `[0x4d2744]` and
   `[0x4d2748]`. Each is made by `0x47749c(0x14)` with the inlined
   constructor's vtable chain (`0x4a69f0`, `0x4a3368`, `0x4a30f8`) and the
   counter changes +1, +1, −2. If new returns NULL, the row refuses at
   `0x45d56d`.
2. It makes two arrays from `0x4774f0(0x400)`, stored in `[0x4d274c]` and
   `[0x4d2750]`.
3. It copies P = `[0x49b020]`'s fields `+4/+6/+8/+0xc/+0xe/+0x10` into both
   objects and sets each object's `+8` to its array.
4. Word `[0x49aef6]` ← 8; then `0x42eb40([0x4d24ea])`.
5. It copies the 256 entries of `[P+8]` into both arrays.
6. The 16 entries at `+0x80` and the 16 at `+0x100` of the first array are
   rebuilt from the second as `0x01000000 | b2>>1 << 16 | b0>>1 << 8 |
   b1>>1`.
7. It calls `0x4303c4(0x20, 0x10, A+0x80)` and `0x4303c4(0x40, 0x10,
   A+0x100)`.
8. It calls `0x4308fc(A, B, 0x78)` (whose EAX is the result), then sets
   byte `[0x49aef4]` ← 0.

### Evidence

`differential_display_test` (in vitro):

| Part | Cases | Coverage |
| --- | --- | --- |
| frame | 2000 | the select path with a driver table 180 times, without one 157 times (in a 4000-case run) |
| capture | 2000 | in a 4000-case run: equal 985, no keyboard 340, registered 287, on skipped 174, off 214, one free entry 128; done unpaged 357/341/311 and paged 309/338/344 (equal/no link/link) |

In the capture part, `0x45b68c` and `0x433ee8` are not patched on the
original side; memset is a stand-in on both sides. DATA and EAX are
compared, EAX as AL where it is the caller's EBX. EDX is also compared for
`0x433ee8`.

`differential_cockpit_test`, part **intro**: 500 cases identical, 376
random palettes and 124 with only 0xff/0xfe bytes. `0x42eb40`, `0x4303c4`
and `0x4308fc` are stand-ins on both sides during the part. The 'E'
stand-in hashes the 16 entries it receives; 'F' notes both objects'
fields. Operator new and new[] run as code; on the original side they go
through `_calloc` on the harness heap. A planted mutation (the `+8`/`+4`
stores swapped in `capture_hotkey`) was caught ("capture case 36: 12 bytes
differ").

## Cockpit layout and panel readers, the display object's readers

Code: `sim/src/hud_cockpit.c` (the load path and, at the end, the readers
below), rows in `sim/dispatch/services/`, in-vitro suite `hud_cockpit`
(`tools/reference-game/invitro/hud_cockpit_cases.h`,
`differential_hud_cockpit_test`).

### Verdicts

| VA | Function | Verdict |
| --- | --- | --- |
| `0x429810` | display pointer getter | inlined in own `display_views` (row `display_ctor`) |
| `0x429820` | `[0x4cf99c]` getter | inlined in own code (`hud_input`, `cursor_zone`) |
| `0x429828` | `[D+0x1f]`, the panel | row `display_panel` (its callers are reached through hooks) |
| `0x429834` | the views of the display | own code inside the row `display_ctor` (`display_views`) |
| `0x429c2c` | the view shape | row `view_shape`; also own C inside `display_views` |
| `0x42a3f4` | a display mode request | row `display_request` (29 call sites in the original) |
| `0x42dcf0` | the region reader of a view file | own code (`view_regions`) inside `display_ctor` and `hdd_ctor` (0x448cf0); no row |
| `0x42db10` | the view rectangle | row `view_rect`; also inlined in `subject_label` (0x45e208) |
| `0x42db18` | whether the 3D view is drawn | row `view_visible` |
| `0x431778` | the panel layout reader | own code (`panel_layout`) inside the row `panel_ctor` |
| `0x432b14` | the panel's share of an input record | row `hud_input`; `0x433b20` and `0x45b750` inlined |
| `0x433b20` | the cursor zone | inlined in `hud_input` (its only caller) |
| `0x433c54` | the cursor image | row `cursor_image` |
| `0x437598` | the message tables | own code inside `panel_texts` (row `display_ctor`) |
| `0x453630`, `0x453684` | the font loads | own code (`hud_shape_one`, `src/hud_load.c`) inside the row `hud_setup` (0x4544c0) |

`display_views` calls `0x431bf8` through the dispatch (the row
`panel_ctor`), so that the suite can stand it in.

### Contracts of the rows

All of these are cdecl and take their arguments on the stack. The request
`0x42a3f4` is [above](#0x42a3f4d-req-a-display-mode-request), `0x42db18`
[above](#0x42db18-pure-helper).

#### `0x429c2c(D, i)`, the view shape

Listing 0x429c2c..0x429dd5. The function returns early when i = 4 or
when `[D+8][i]` is already set. Otherwise it loads the view shape, as the
comment above `view_shape` in `hud_cockpit.c` describes.

| Register | Early return | Otherwise |
| --- | --- | --- |
| EAX | the frame record's saved head | stream_destroy's EAX; for digit 2, `[D+8]` |
| ECX | the frame record's saved head | the frame record's saved head |
| EDX | `[D+8]` (when i != 4) | stream_destroy's EDX; for digit 2, `[D+8][i]` |

The frame record's saved head is the FS:0 value at entry.

#### `0x429828(D)` and `0x42db10()`

- `0x429828(D)`: EAX = `[D+0x1f]`.
- `0x42db10()`: EAX = 0x4cfa40.

#### `0x432b14(H, rec)`, the panel's share of an input record

Listings 0x432b14..0x432bc4 and 0x433b20..0x433c53. The steps run in
this order:

1. **The cursor zone** (only when byte H+0x23e is set and byte
   `[0x49b105]` is set).
   - The point is rec+2, or 0x4d2306 when word `[0x4d24d6]` is set (tape
     playback).
   - The point is moved from the canvas origin `[H+4]+0x210` to
     `[H+4]+0x220`.
   - `[H+0x22e]` is cleared.
   - The three rectangles `[H+0x21a + 4k]` are tested, signed and
     inclusive. The first one that holds the point sets the zone:

     | k | m = 0 | other m |
     | --- | --- | --- |
     | 0 | 4 | 3 when m = 1 |
     | 1 | 6 | 5 when m = 3 |
     | 2 | 5 | 6 when m = 2 |

2. **The mode requests.** When set, the bytes rec+0x1e, +0x1f, +0x20 and
   +0x21 request the modes 1, 0, 5 and 4, in that order, through
   `0x42a3f4`.
3. **The toggle at rec+0xb.**
   - When byte rec+0xb is set: if `[0x49b550]` is clear and the mode is 1,
     slot +0xc of the sub-object o = `[H+0x1f9]` is called with (o, 0xf).
     Then `[0x49b550]` <- 1.
   - When byte rec+0xb is clear: `[0x49b550]` <- 0.

The return value is AL = 1. The test also compares the upper bits of
EAX, which are D, mode - 1 or the slot's EAX.

#### `0x433c54(H)`, the cursor image

Listing 0x433c54..0x433d8f. Nothing happens unless byte `[0x49b105]` is
set. Let z = `[H+0x22e]` be the zone.

- **z != 0:**
  - H+0x232 <- `[0x49b08c + 4z]`.
  - If the pointer H+0x226 is not H+0x232: H+0x22a takes the old pointer,
    and H+0x226 <- H+0x232.
  - The image is s = `[H+0x232]`.
- **z = 0 and H+0x226 = H+0x232:**
  - H+0x226 <- H+0x22a.
  - The image is s = `[[H+0x226]]`.
- **z = 0 otherwise:** nothing is drawn.

The image s is drawn by `0x48992e` when word `[0x4d2566]` is set (the
paged driver), and by `0x486e70` otherwise. The arguments are (a, s+8),
where a = `[s+4]+4`, or 0 when `[s+4]` is 0. EAX is H, or the callee's
EAX when a draw happened.

### Layout and panel reader contract

#### The layout L

Read by `0x431778` (own `panel_layout`). L comes from new(0x6a4). Before
the read, the code builds six rectangle arrays (16 bytes per rectangle)
in place:

| Array | Rectangles | Element routine |
| --- | --- | --- |
| L+0x14 | 10 | 0x434ab9 |
| L+0x184 | 3 | 0x48de2f |
| L+0x1e4 | 4 | 0x48de2f |
| L+0x2e8 | 13 | 0x48de2f |
| L+0x50c | 15 | 0x48de2f |
| L+0x5fc | 3 | 0x48de2f |

Then the whole 0x6a4 bytes are read from the stream. If the offset
(dx, dy) = (L+0, L+4) is not zero, it is added to both corners of:

- the first L+0x10 rectangles of the array at L+0x14;
- the single rectangles at L+0x1d4, 0x224, 0x268, 0x2d8, 0x3e8, 0x440 and
  0x4bc.

`[VA_LAYOUT]` keeps L. `panel_ctor` later shifts L+0x684.

#### The display object D

Built by 0x429660 and 0x429834; the fields are in [Objects](#objects).
`+0x0c` holds the regions (0x204 bytes each, a count, then pointers),
`+0x10` the span lists.

#### The panel H fields that the per-frame rows read

| Offset | Content |
| --- | --- |
| +4 | canvas |
| +0x1f9 | sub-object |
| +0x21a | the three zone rectangle pointers |
| +0x226 | image pointer |
| +0x22a | the image before |
| +0x22e | zone |
| +0x232 | image cell |
| +0x23e | byte: the zones are set up |

#### The view file

Read by `0x429834`:

- a dword n, then n × 32 bytes of view records, which go to 0x4cf9a8;
- then, per view, a region file read by `0x42dcf0`, in the format given
  above `view_regions`.

`[D+8]` has room for 4 dwords. For i = 3, `0x429c2c` writes one past it,
at `[D+8][4]`. This overrun is the original's. It is not reached in the
suite.

### Evidence

`differential_hud_cockpit_test`, seed 0x48c06e21. All four parts are
identical. Each case compares:

- the stand-in logs;
- DATA and the heap;
- the 0x3800-byte world;
- EAX (for `0x432b14`, AL and the upper bits).

| Part | Cases | What runs |
| --- | --- | --- |
| display | 1500 | `0x429660` |
| fonts | 300 | `0x4544c0` |
| shape | 1000 | `0x429c2c` |
| frame | 6000 | 1 to 4 random calls of the six per-frame rows |

In the display part, the following run as code: 0x429834, 0x42dcf0,
0x48bab4, 0x429c2c, 0x431008, 0x431098, 0x437598, 0x45ef1c, the palette
pieces and the RTL thunks. The panel constructor `0x431bf8` is a
stand-in.

All 60 branch keys are reached. Some of the counts:

| Branches | Counts |
| --- | --- |
| display: res3 / row tables | 495 / 2465 |
| display: palette set / upload | 763 / 737 |
| shape: early4 / early-set / load / second / digit2 | 170 / 272 / 558 / 58 / 206 |
| request: pending / high / store / none | 573 / 233 / 291 / 632 |
| zone: hit / miss | 23 / 400 |
| cursor: new / same / back / none | 104 / 112 / 222 / 223 |

A planted mutation (in `display_request`, the req-2 test `m != 4` changed
to `m != 3`) was caught ("frame case 24: the world differs").

### Open

- These paths are not reached in vitro; verdicts from the listing and the
  CD data:
  - the fatal report on a stream error (0x4317c6) is no refusal: the own
    code calls `0x479c78` as the original does. Only a broken panel layout
    stream (`gau<herc>.GAU` through `0x431778`) reaches it.
  - operator new returning NULL (0x431a2f, 0x42dcf0 → 0x42de7c/0x42df17,
    0x45d56d) is a NULL access, reached only with exhausted address space
    ([own_process.md](../reference-game/own_process.md)); the guards stay.
  - a row-table region area (0x487f68) is unreachable: all seven callers
    of `0x488045` (`0x4297f2`, `0x43472b`, `0x435c13`, `0x4362af`,
    `0x447f91`, `0x448529`, `0x4542ff`) pass GLRectangleRegion objects
    (vtable `0x4a3264`, type 0 from the constructor or the static
    initialiser `0x42db40`); type-2 row tables exist only in the view
    region lists of 0x42dcf0, which never reach it.
  - more than 12 spans in one row (0x48bab4) is unreachable by data: the
    112 region-file reads (74 files: `MFDCLIP.EDG`, `HDDCLIP.EDG`, the
    `<herc>.ED0..3`/`.HD0..3` files) use shifts 0 and 1, at most 4 spans
    per row and 6 regions per file.
- The `[D+8][4]` overrun at i = 3 is not exercised.
- None of this is validated live.

## The view border and the layout set draw

Code: `sim/src/hud_display.c`, rows `view_border` (0x42da08) and
`display_set` (0x42a1f0). In-vitro suite `cockpit_pixels`
(`tools/reference-game/invitro/cockpit_pixels_cases.h`,
`differential_cockpit_pixels_test`). The cockpit half of that suite is in
[cockpit.md](cockpit.md#mfd-button-draws-and-the-panel-copy).

Every draw goes through the graphics rows as the listing calls it: the
fill 0x486704, the blits 0x488143 and 0x4816a8, and at 640x480 the stretch
0x4817c8. Nothing in `hud_display.c` counts `headless`.

The fill 0x486704 works on the graphics context record `[0x4a3614]`
(not `[0x6c5f70]`). Its fill record is `[C+0x228]`; kinds other than 0, 4
and 6 return 0 without drawing. `//sim:hud_test`'s `display_mode4`
fixture maps a context with kind 7, so the border fills stay off a
surface there.

### `0x42da08`, the view border (listing 0x42da08..0x42daf0)

- The frame is 0x48 bytes. The locals keep the listing's offsets: the
  pair (0, word `[0x4d3ba2]`) at ebp-8, and the four rectangles at
  ebp-0x18 (top), -0x28 (bottom), -0x38 (left) and -0x48 (right).
- GC+0x22c is set to the pair's address and is left dangling, as in the
  original. GC+0x208 and GC+0x20c are saved, cleared and restored.
- Order and conditions (signed): top (0, 0, W-1, T-1) when T-1 >= 0;
  bottom (0, B+1, W-1, H-1) when H-1 >= B+1; left (0, T, L-1, B) when
  L-1 >= 0; right (R+1, T, W-1, B) when W-1 >= R+1. The view rectangle is
  (L, T, R, B) = dwords 0x4cfa40 .. 0x4cfa4c; W = `[0x4d253e]`,
  H = `[0x4d2542]`.
- **EAX:** the right fill's result when it is drawn, else W-1
  (0x42daea). EDX and ECX are not part of the contract (the callers do not
  read them; not kept).

### `0x42a1f0(D, mode)`, the layout set (listing 0x42a1f0..0x42a3c6)

The listing is checked line by line, including the four-level bitmap read
`[[[[D+8]+4m]+8]]`.

- Mode > 4 (signed) returns at once with the entry EAX.
- Mode 1 at 400 lines returns EAX 0.
- Otherwise the switch 0x429e60(D, mode) is called through the dispatch
  (its row is in the same file), then the mode's bitmap is drawn:
  - The frame is 0x3c bytes: the point (ebp-8, ebp-4), the 640x480 size
    (ebp-0x18), the saved GC+0x21c (ebp-0x1c), the pair (ebp-0x28) and the
    fill rectangle (ebp-0x3c).
  - At 640x480 (listing 0x42a27d..0x42a2dd), the bitmap is stretched to
    0x140 x 0x1e0 through 0x4817c8. Mode 3 first sets GC+0x208 to 1,
    GC+0x21c to 0x1df and x to -1. GC+0x21c is restored after the blit,
    and GC+0x208 with the rest below.
  - At 400 lines, mode 0 adds the second bitmap at y 0xc5. Modes 2 and 3
    set GC+0x22c to the local pair (left dangling), select page `[V+0x8c]`
    and fill (0, 0xc5, 0x140, 0x1e0) through 0x486704.
- Then EAX takes the saved GC+0x208 (0x42a3b8). For modes 0..3 the
  panel's full draw 0x4316c0 runs, and when the word is set the free
  0x429de4 runs. Both go through the dispatch, and the last one that runs
  sets EAX. D+0x14 is set to the mode.
- **EAX:** the entry's for mode > 4; 0 for mode 1 at 400 lines; the
  switch's for mode 4; otherwise the free's, the panel draw's or the saved
  GC+0x208, whichever ran last. EDX and ECX are not kept (no caller reads
  them).

### Evidence

`differential_cockpit_pixels_test`, seed 0x48c06e21, 6000 cases, all identical.
Each case runs 1 to 4 random calls of the suite's five rows on a random
world. It compares DATA, the heap, the stand-in log hash and the
registers: EAX only for the border and the set, all three registers for
the button draws and the panel copy. The stand-ins cover the fill, the
blits, the stretch, the switch, the view shape, the panel draw and the
free.

| Branch | Count |
| --- | --- |
| border: top fill / skip | 4171 / 1253 |
| border: bottom fill / skip | 2602 / 2822 |
| border: left fill / skip | 4209 / 1215 |
| border: right fill / skip | 2550 / 2874 |
| set: mode > 4 / mode 4 / low | 1520 / 749 / 1275 |
| set: 400 lines mode 0 / mode 1 / modes 2, 3 | 238 / 208 / 441 |
| set: 640x480 / 640x480 mode 3 / split | 735 / 206 / 1455 |

Rows called: view border 5424, display set 5372. The planted mutation of
the suite is in
[cockpit.md "MFD button draws and the panel copy"](cockpit.md#mfd-button-draws-and-the-panel-copy).

**Frames.** Headless es2sim captures (`replay_suite.py --frames`) of
acceptance-60s, c1_01-cockpit-keys and c1_01-display-keys, compared with
`dbsim_frames.py compare` against the original's manifests
(`es2-data\logs\dbsim-frames-20261014`, `dbsim-frames-20261021`; the
original has no spread in these three suites) by (frame, site, ordinal)
on the 8-bit frame hash: with the border filled, the outside-view frames
331..480 of c1_01-display-keys (F10 at frame 320, display mode 4, panel
off) are equal to the original's, the 3D view included. Of the
display-switch presents at the sites 0x42b294 .. 0x42d045, the records at
0x42c066, 0x42c0fe and 0x42bb84 are equal.

### Open

- EDX and ECX of the border and of the set are not kept. No caller reads
  them; they would matter only for a caller that does.
- Negative modes of 0x42a1f0 are not generated in vitro. The suite draws
  modes 0..6, so only mode > 4 covers the early return.
- The set's "not current view" branch for mode 1 is not reached. It
  needs a 400-line display that is not current, which the suite's world
  does not build.

## Observations (original quirks)

All confirmed as instructions; consequences are hypotheses.

- `0x42d2f8`, mode 1: `0x4cfa68 += 0x4d301c` (`0x42d477`) is overwritten
  two instructions later (dead store), and the upper bound gets no
  `0x4d301c` offset although mode 0 adds it to both bounds.
- `0x42d2f8`, `0x42da08`, `0x42a1f0`, `0x45d85c` store the address of a
  stack local in context `+0x22c` (fill colour) and return without
  restoring it; presumably every fill sets it again first.
- `0x45275c` computes width − 1 and height − 1 of the screen and never uses
  them.
- `0x429e60` tests `D+0x14` = 4 after entering the m = 4 branch; this edge
  (`0x429eb4` taken) is unreachable.
- `0x42a4c4` reads VGA port `0x3da` (`0x45c638`) every presentation frame
  when word `0x4d2566` is 0. A user-mode port read faults on NT-family
  Windows, so the oracle presumably runs with `0x4d2566` ≠ 0 (set by the
  video preference code `0x45e510`, cleared by one case of the parser
  `0x45e758`).
- `0x42a9c0`'s map row counts 155 instructions because the map did not
  follow its two jump tables; the function spans `0x42a9c0..0x42d2b7`.
  The map's caller lists also miss the jump-table call sites (for example
  `0x42a9c0` → `0x45b68c`, `0x42a4c4` → `0x429e60`).
- `0x42d2f8`'s callers include the direct site `0x42d278` in `0x42a9c0`
  (tagged V in [hook_candidates.md](../reference-game/hook_candidates.md)).

## Open questions

- The meaning of the display modes 1–3 (hypotheses: 1 = panel scrolled
  for the MFDs, 2/3 = left/right side views) and of the bytes `0x4d2537`,
  `0x4d2538` (no absolute writer of 1 or 2 was found; they may be written
  through the block base `0x4d24bc`).
- What the flight-key capture switch changes in play: which keys
  `0x49eb0b` lists and which consumer reads `0x4d2394`/`0x4d23a3`/
  `0x4d23b2` (`0x45a5xx`, not followed). A counter on `0x45b68c` per frame
  together with the c3_09 Esc frames would show the timing.
- Whether the mission-start sequence runs in the oracle suites (word
  `0x4d2510`; the map cannot show it, because it runs before the proxy
  arms); if it does, its jolt loop draws stream B a number of times that
  depends on the clock during the mission load. Whether that clock is the
  proxy's virtual clock at that point, or stream B is reseeded later, is
  not checked.
- Whether a map click (`0x44d888`, [cockpit.md](cockpit.md)) during a jolt
  uses a shifted HDD map rectangle: the jolt moves widgets and the canvas
  origin, but the HDD map's own rectangle `M+0x71` is not traced.
- The role of `0x45cff0` (F12/Alt+P, view rectangle and layout options)
  and of `0x429c2c` (flag `-l`) beyond their calls into this region.
