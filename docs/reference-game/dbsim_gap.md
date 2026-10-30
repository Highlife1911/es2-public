# Original DBSIM code inventory

What original code of `VER95/DBSIM.EXE` still runs when every registered hook
is replaced, how large each area of it is, which own code stands in for it in
the own process (`es2sim`), and what the own chain takes from the original
executables and DLLs. The module map of section 1 is also data
(`tools/reference-game/scripts/areas.py`) and gives the Area column of the
[function index](../functions/README.md).

**Sources.** The reference `VER95/DBSIM.EXE` (SHA-256 `8561aa47…c4ad`,
[reference_version.md](../reference_version.md), image base `0x400000`),
disassembled with the pinned `llvm-objdump -d -M intel`; the registry
`tools/reference-game/proxy/hook_registry.h`; the DBSIM map
([dbsim_map.md](dbsim_map.md); es2-data `logs\dbsim-map-20261012` with every
hook original, `logs\dbsim-map-20261020` with all hooks replaced); the
bridged-call inventory of [hook_frontier.md](hook_frontier.md); the verdicts of
[function_verdicts.md](function_verdicts.md); the [function index](../functions/README.md);
the subsystem documents named per area.

**Labels.** *Confirmed* = read from the bytes (address given) or from a cited
document that confirmed it. *Live* = the map's entry probes or a regression's
counters. *Estimate* = a number that depends on the static walk or on the
module map of section 1, which are approximations. *Grep* = a source scan and
an upper bound (constants in comments and write-only addresses count too).
Sizes are static instruction counts of whole functions, not executed
instructions. Two baselines appear: the **485-hook baseline** (registry of 485
hooks, map with every hook original; sections 2, 3 and 5) and the **current
map** (all hooks replaced, 782 hooks registered when it was taken; the
registry now holds 789).

## Overview

1. **Image (estimate).** The static walk reaches 3,065 function entries with
   177,771 instructions (the synchronized descent of
   [dbsim_map.md](dbsim_map.md#probe-inventory) reaches 176,549 instructions
   from 3,010 known starts). The 485 hooks of the baseline cover 46,528
   instructions (26 %).
2. **Original code that still runs (live).** With all hooks replaced, 1,286
   original functions with 82,559 instructions run from the mission-load call
   on (table in section 3). 78 % of the instructions are cockpit/HUD (39,069)
   and 3D renderer (25,469) code; the rest is simulation leaves 2,282, audio
   3,044, dialogs 3,888, input 2,099, files 1,092, presentation 1,594, shell
   1,813, math 964, runtime 368 and 877 in no range. No function runs that
   did not run with every hook original.
3. **Simulation is own code apart from leaves.** The SIM range
   `0x402000..0x428000` runs 66 original functions with 2,282 instructions:
   one- to ten-instruction virtual methods of the actor, weapon, fire and
   animation classes, load-time set-ups and draw-side slots. Of the 12
   functions with an H4 verdict that were original at 485 hooks, none needs a
   hook: nine are hooks, three are computed in core (section 4.1). The ranked
   open targets are [hook_candidates.md](hook_candidates.md) and
   [hook_frontier.md](hook_frontier.md).
4. **Before the arming point everything runs original.** In the oracle line
   the proxy installs hooks at the call of the mission load (`0x45f42b`, early
   arming, default; `ES2_EARLY_ARMING=0` arms at the first MCI call
   `0x461dc3`). WinMain, the 65 static initializers, the option parsers, the
   window and the VOL enumeration run original; only the 41 CW3220 imports of
   the IAT-slot shim are own code there (it arms at DBSIM's entry point).
   Static closures (upper bounds): start-up 327 functions / 10,914
   instructions, mission load 918 / 54,677, of which the load's simulation
   part (pieces L1 … L9, section 5) is 124 / 10,720.
5. **The coupling set (confirmed static, counts live).** 47 running original
   functions (4,674 instructions, 39 of them HUD) contain the 39 clock-call
   sites (`0x467828`, the intra-frame virtual clock whose value the tick log
   compares), 8 stream-B draw sites and 16 calls into the sound API (whose
   hooks draw stream B). A replacement of HUD, presentation, dialogs or audio
   reproduces these calls in count and order
   ([hud.md](../reverse-engineering/hud.md#for-h3-presentation-replacement),
   [presentation.md](../reverse-engineering/presentation.md#keep-list-for-a-presenter),
   [audio.md](../reverse-engineering/audio.md#isolation-rules-for-a-replacement)).
6. **The own process runs none of it as original.** `es2sim` maps a guest
   image at the original addresses and binds every original call site to own
   code (hooks, service rows, own functions). The played chain
   `es2launch → es2shell → es2sim` executes no original instruction and loads
   no game DLL. What remains is the gap list of [sim_gap.md](sim_gap.md)
   (original functions the own process still reaches without a row or hook;
   the dispatch refuses them and the run stops there). Section 6 maps the
   areas to own code.

## 1. Method

**Entries and sizes (estimate).** Function entries: the function index pages,
the registry, the map rows with their callees and callers, the 65 init and 26
exit records of the startup module table, WinMain `0x46538c`, the entry point
`0x401000`, every target of a base-relocated dword that starts with
`55 8b ec` (vtable slots and callbacks) and, iteratively, every direct call
target of reached code. Per entry a control-flow walk follows fall-through,
conditional and direct jumps and jump tables (consecutive relocated dwords); a
jump or fall-through into another entry is a tail call. For 856 of the 873 map
rows of the baseline the walk gives the map's instruction count; the 17 others
are larger because the map did not follow jump tables (`0x466034` 389 against
338; `0x42a9c0` 2,480 against 155; `0x45feb8` 599 against 448).

**Status (live, from the map).** *hook*: in the registry; *runs*: entered in at
least one map suite, plus the game main `0x45f160` (its main loop runs every
frame), the bridged callees with nonzero `call_<va>` counters, and the
stage-5/6 callbacks and render-root guest callees that the render root's
contract calls every frame
([main_loop.md](../reverse-engineering/main_loop.md#live-expectation-and-result-1));
*dead*: probed, 0 in all suites; *n/p*: no probe row. **Replaced-mode reach
(estimate):** the direct-call closure, through non-hook functions, from the
code still entered as original when every hook is replaced: the game main and
WinMain, MainWndProc `0x466034`, the graphics-driver slot target `0x4649d8`,
the 136 guest-call targets of the adapters, the stage callbacks and every live
pointer-referenced function (a vtable or callback target can be dispatched from
original or own code alike). A live function outside that closure is
*superseded*: it ran only below code that is own now (21 functions, 519
instructions in the baseline, e.g. the formation helpers that the `formation`
hooks compute in core). The current map re-measured this with all hooks
replaced: 41 baseline rows are superseded
([dbsim_map.md](dbsim_map.md#map-with-all-hooks-replaced)).

**Phases (estimate).** Static closures without stopping at hooks: *start-up* =
WinMain and the 65 init records up to the calls of the mission load, the main
loop's children and the mission end; *load* = `0x461608` without the render
root and presentation frame it calls after arming; *frame* = the main loop's
children, the window procedure and the live pointer-referenced functions; *end*
= the mission end `0x461ff0` and the 26 exit records. "Pre-arming only" in
section 3 = in the start-up or load closure and not entered after the arming
point of `0x461dc3`. Static closures include error and debug paths, so they are
upper bounds.

**Areas (estimate).** `areas.py` assigns each entry to an area by its recorded
verdict where one exists, else by a module map of address ranges (Borland links
one source module after the other; each range names the document it comes
from), and unverdicted functions in the simulation and math ranges take the
area of the majority of their callers. The map:

| Range | Area | Basis |
|---|---|---|
| `0x401078..0x401d64`, `0x401dc0..0x402000` | R3D | camera module ([camera.md](../reverse-engineering/camera.md)); `camera_update` is a hook |
| `0x402000..0x428000` | SIM | actor, weapon, effect, AI and mission modules ([vtables.md](../reverse-engineering/vtables.md)); functions with an H3 verdict there (draw slots, lights) go to R3D, message posts to HUD |
| `0x428000..0x429660`, `0x42e700..0x430390` | R3D | terrain binning and object draw, sort tree, sky and horizon ([terrain.md](../reverse-engineering/terrain.md), [horizon.md](../reverse-engineering/horizon.md)) |
| `0x429660..0x42e700` | HUD | cockpit object, layouts, display transitions, jolts ([display_modes.md](../reverse-engineering/display_modes.md)) |
| `0x430390..0x4311e0` | PRES | palettes ([presentation.md](../reverse-engineering/presentation.md)) |
| `0x4311e0..0x453824` | HUD | HUD, cockpit gadgets, message ports, widgets ([hud.md](../reverse-engineering/hud.md), [cockpit.md](../reverse-engineering/cockpit.md)) |
| `0x453824..0x453878`, `0x453a80..0x459000` | DLG | dialogs and alerts |
| `0x459000..0x45c8bc`, `0x4775c0..0x477d48` | INP | input devices, prefs/keyjoy, keyboard and joystick ([input.md](../reverse-engineering/input.md)); the mouse queue `0x480948`/`0x4809f8` by override |
| `0x45c8bc..0x461608` | SHELL | main module (game main, parsers, pause); view, detail and drop-sequence functions by override to R3D/HUD |
| `0x461608..0x461ff0` | LOAD | mission load ([mission_load.md](../reverse-engineering/mission_load.md)) |
| `0x462398..0x4649d8`, `0x473708..0x474b30`, `0x4958fd..0x49630c` | AUD | sound API, SFX manager, CD music, SOS glue, the SOS loader and its 97 stubs ([audio.md](../reverse-engineering/audio.md)) |
| `0x4649d8..0x464c68`, `0x46548c..0x466010`, `0x46658c..0x466a86` | PRES | present, frame clear, palettes, DirectDraw ([presentation.md](../reverse-engineering/presentation.md)) |
| `0x464c68..0x46548c`, `0x466010..0x46658c`, `0x467828..0x467b00`, `0x479c00..0x47a0b8` | SHELL | window, pump, WinMain, MainWndProc ([flyer.md](../reverse-engineering/flyer.md#the-win32-shell-region-0x464a140x466034)), clock and limiter ([timing.md](../reverse-engineering/timing.md)), fatal error |
| `0x4680e0..0x46a000`, `0x46ac14..0x46ae00`, `0x46be9c..0x46f000`, `0x470a1c..0x470d94`, `0x471e10..0x472e6c`, `0x474b30..0x476d00`, `0x477d48..0x478600`, `0x480738..0x4928d0`, `0x4930a0..0x495132` | R3D | polygon pipeline and rasterizer ([rasterizer.md](../reverse-engineering/rasterizer.md)), terrain renderer, DTS draw slots, graphics library, shapes, scan converter |
| `0x472e6c..0x473708`, `0x47a0b8..0x47d32c`, `0x453878..0x453a80`, `0x492b68..0x492c80` | FILE | stream library, VOL archive, persistent objects, paths, STR reader ([file_io.md](../reverse-engineering/file_io.md)) |
| `0x476d00..0x4775c0`, `0x4953c0..0x4958fd`, `0x49630c..0x496a60`, `0x401000..0x401078` | RTL | allocator, runtime pieces, import thunks ([rtl_imports.md](rtl_imports.md#4-the-runtime-inside-dbsimexe)) |
| `0x47d32c..0x480738`, `0x4928d0..0x492b68`, `0x495132..0x4952fc` | MATH | fixed-point, matrix, distance helpers (mostly hooks) |

`tools/reference-game/scripts/areas.py` holds the same ranges
(`areas.py --check-document` and its unit test fail when the two differ) and
gives the **Area** column of the [function index](../functions/README.md). Its
pure address map does not apply the verdict and caller-majority overrides named
above. The index page reports the map against the listing: entries, hooks and
instructions per area, the direct calls between areas, every range boundary
(whether a function entry starts there, and the areas of the callers on both
sides) and the gaps that no range covers (eleven between the ranges and the
tail after `0x496a60`; their entries have no area, and the report suggests
where they belong by the areas of their callers and callees, which is a
hypothesis).

Couplings are direct call sites inside a function: to the clock wrapper
`0x467828`, to `rng_next`/`rng_bounded` with `0x4d260b` (stream B) or
`0x4d2599` (stream A) among the eight preceding instructions (no site is
unresolved), to the sound API (`0x462830`, `0x4628e0`, `0x46297c`, `0x46299c`,
`0x462ac4`, `0x463114`, `0x462940`, `0x462b9c`) and to the heap (`0x47749c`,
`0x4774f0`).

## 2. A DBSIM process from entry to exit

Hook counts and sizes in the last column are the 485-hook baseline.

| Phase | What runs (confirmed unless marked) | Own code in the oracle line | Static size (estimate) |
|---|---|---|---|
| Entry and runtime start | c0w32 entry `0x401000` → CW3220 `__startup` → the 65 init records (all priority `0x20`, table order; record 47 `0x45caf4` seeds both RNG streams) → WinMain `0x46538c` ([rtl_imports.md](rtl_imports.md#41-borland-runtime-pieces-linked-into-dbsim-confirmed-static)) | the 41 CW3220 imports of the IAT-slot shim, which arms at DBSIM's entry point before the init records ([rtl_imports.md](rtl_imports.md#when-it-installs-timing)); no hook | init records 133 functions / 3,063 instructions (1,745 in the 65 functions themselves) |
| WinMain and game main | argv, early parser `0x45e6cc` (`-X`, `-Z`, `-v`), window class and window, DIB section, DirectDraw switch, game main `0x45f160`: `drive.cfg`, main parser `0x45e758`, stages 0 and 1, VOL enumeration `0x467cd8`, `error.str`, the `-l` arena ([launcher.md](../reverse-engineering/launcher.md), [file_io.md](../reverse-engineering/file_io.md)) | none before the mission-load call | start-up closure 327 / 10,914, of which 8 hooks (1,598) |
| Mission load `0x461608` | input set-up, graphics context, loading screen, sound init, the 19 stage-2 callbacks, world load (pass 1), actor load (pass 2), the drains, height fix and player pick, cameras, cockpit and HUD construction, `cockpit_sync` once, then the old arming point **`0x461dc3`**, first render, start sequence ([mission_load.md](../reverse-engineering/mission_load.md)) | hooks from `0x45f42b` on (early arming) | 918 / 54,677, of which 110 hooks (6,944) |
| Main loop (every frame) | in `0x45f160`: `stage_dispatch(4)`, limiter `0x4678c0`, pause/pump, `logic_frame`, `camera_update`, `render_root`, presentation frame `0x45fba4`, `stage_dispatch(6)`, frame counter ([main_loop.md](../reverse-engineering/main_loop.md#the-main-loop-around-it)) | the four hooked children and everything below them that is hooked; the loop itself, the limiter (calls the hook `time_scale`) and `0x45fba4` are original | frame closure 1,395 / 86,959, of which 265 hooks (25,387) |
| Window messages | MainWndProc `0x466034` with the handler list (keyboard `0x477bec`, mouse `0x4809f8`, `sfxWndProc` `0x462398`), focus pause, `WM_PAINT` present ([flyer.md](../reverse-engineering/flyer.md#mainwndproc-0x466034)) | none | in the frame closure |
| Mission end | logic frame returns 1 → `0x461ff0`: tape/dmp close, `results_writer` (hook), exit code `0x4d27b8`; prefs write, tape-mode file deletes, WinMain's pump until `WM_QUIT`, `0x466730`, CW3220 exit, 26 exit records ([file_io.md](../reverse-engineering/file_io.md#mission-end-and-exit)) | `results_writer` | end closure 129 / 3,252, of which 27 hooks (1,507) |

## 3. Original code after arming, by area

Counts are functions/instructions (estimate); "hot" = at least 1000 entries in
one map suite; "bridged" = called by an adapter through the guest bridge (live
counters); couplings count call sites inside the running functions. The table
is the 485-hook baseline (map with every hook original, arming at `0x461dc3`).

| Area | Runs | Hot | Bridged | Pre-arming only | Never entered, in reach | Clock | Stream B | Sound API | Heap |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| SIM simulation leaves | 76 / 1,109 | 48 | 13 | 57 / 2,864 | 5 / 98 | 0 | 0 | 1 | 0 |
| R3D 3D renderer, camera, view | 258 / 17,308 | 213 | 15 | 115 / 6,905 | 45 / 3,225 | 2 | 0 | 0 | 2 |
| HUD cockpit, HUD, MFD/HDD, gadgets, ports, widgets, display modes | 255 / 20,096 | 105 | 40 | 130 / 13,646 | 48 / 1,526 | 33 | 8 | 14 | 3 |
| DLG modal dialogs and alerts | 28 / 1,768 | 5 | 2 | 3 / 151 | 9 / 1,627 | 1 | 0 | 0 | 6 |
| PRES presentation | 15 / 1,203 | 8 | 0 | 23 / 1,484 | 3 / 170 | 1 | 0 | 0 | 0 |
| SHELL process shell | 8 / 840 | 3 | 2 | 36 / 1,930 | 8 / 112 | 0 | 0 | 0 | 0 |
| AUD audio | 53 / 2,079 | 19 | 7 | 58 / 2,028 | 19 / 483 | 2 | 0 | 1 | 0 |
| INP input | 20 / 637 | 11 | 6 | 37 / 1,587 | 2 / 7 | 0 | 0 | 0 | 0 |
| FILE files, streams, assets, configuration | 46 / 1,646 | 5 | 8 | 54 / 1,828 | 1 / 39 | 0 | 0 | 0 | 3 |
| RTL runtime, allocator, thunks | 17 / 337 | 13 | 11 | 114 / 395 | 8 / 8 | 0 | 0 | 0 | 3 |
| LOAD mission-load module and its own callees | 0 | 0 | 0 | 90 / 8,909 | 0 | 0 | 0 | 0 | 0 |
| MATH | 7 / 79 | 6 | 1 | 4 / 135 | 7 / 170 | 0 | 0 | 0 | 0 |
| **Total** | **783 / 47,102** | **436** | **105** | **721 / 41,862** | **155 / 7,465** | **39** | **8** | **16** | **17** |

Superseded (ran with every hook original, no longer reached): 21 / 519. Own
code: 485 hooks / 46,528. The rest of the 177,771 walked instructions (about
34,000) is outside the replaced-mode reach: code below hooks that the hooks no
longer call, the never-reached option paths (tape recording, demo, DirectDraw),
and functions nothing refers to.

### Current map: all hooks replaced

The map of `logs\dbsim-map-20261020` ([dbsim_map.md](dbsim_map.md#map-with-all-hooks-replaced))
counts the original functions that run from the mission-load call on, per area
of the function index (functions / instructions; the load window and the
frames together):

| Area | Runs | Load and frames | Frames only | Load only | Saturated in the load window |
|---|---:|---:|---:|---:|---:|
| HUD | 436 / 39,069 | 170 / 16,025 | 138 / 9,009 | 128 / 14,035 | 0 |
| R3D | 363 / 25,469 | 196 / 16,063 | 32 / 1,768 | 92 / 3,302 | 43 / 4,336 |
| SIM | 66 / 2,282 | 6 / 393 | 16 / 803 | 41 / 904 | 3 / 182 |
| AUD | 79 / 3,044 | 19 / 1,089 | 38 / 1,009 | 22 / 946 | 0 |
| DLG | 48 / 3,888 | 3 / 26 | 44 / 3,728 | 1 / 134 | 0 |
| FILE | 41 / 1,092 | 24 / 654 | 1 / 28 | 13 / 305 | 3 / 105 |
| INP | 54 / 2,099 | 21 / 831 | 4 / 61 | 29 / 1,207 | 0 |
| MATH | 35 / 964 | 25 / 770 | 3 / 45 | 4 / 115 | 3 / 34 |
| PRES | 25 / 1,594 | 15 / 1,152 | 2 / 55 | 8 / 387 | 0 |
| RTL | 79 / 368 | 48 / 169 | 8 / 8 | 13 / 13 | 10 / 178 |
| SHELL | 31 / 1,813 | 10 / 698 | 12 / 723 | 9 / 392 | 0 |
| (no range) | 29 / 877 | 11 / 196 | 4 / 144 | 12 / 478 | 2 / 59 |
| **all** | **1,286 / 82,559** | 548 / 38,066 | 302 / 17,381 | 372 / 22,218 | 64 / 4,894 |

The Runs column is the sum of the four others (derived from the map's
per-area table); it equals the Live and Instr. columns of the function index's
[area table](../functions/index.md#areas-module-map-of-dbsim_gapmd-section-1).
"Frames only" means not in the first pass of the loop body either: message,
dialog and event code. About a quarter of the remaining original code (372
rows, 22,218 instructions) runs only in the load window, most of it HUD code
(14,035; presumably the cockpit construction, not checked per function).
Start-up and static initialisation before the arming point are not counted.

**Bridged calls (live, `check-r6-set2`, baseline).** 140 original callees
appear as `call_<va>` in the adapters (136 real guest calls, 4 computed in
core); 82 of them were called, 6,159,880 times over the six suites, 4,277,253
of them the runtime's exception-frame set-up `0x49630c` and 633,189 the
`_memset` thunk. By verdict: H3 63, platform 30, RTL 19, H4 10, own code 4,
dead 1, and 13 without a verdict (the render root's stage callbacks and guest
callees).

**Imports reached by running original code (confirmed static).** USER32
(window, pump, paint, cursor), GDI32 (DIB section, palettes, `StretchBlt`),
KERNEL32 (`GetTickCount`, global memory, `Sleep`, `OpenFile`; before arming
also `LoadLibraryA`/`GetProcAddress` for SOS, the `_lopen` family,
`GetPrivateProfileStringA`), WINMM (`mciSendCommandA` for CD music;
`joyGetPosEx`/`joyGetDevCapsA` only in start-up and load code), DDRAW
(`DirectDrawCreate`, only on the never-taken DirectDraw branch), MSVFW32
(`ICSendMessage`, only in the unreferenced AVI block), CW3220 (53 imports, 41 of
them own code behind the shim) and, through `LoadLibraryA`, `SOS9503.DLL` (97
entry points) and `BWCC32.DLL` ([dll_imports.md](dll_imports.md) inventories
both).

## 4. Per area: what it is and what stands in for it

Own code for each area is in the `sim/src` files named; the contract of each
file is the subsystem document its header names.

### 4.1 Simulation leaves (SIM)

The SIM range holds the actor, weapon, effect, AI and mission modules. The
actor-slot leaves are hooks (groups `actor_leaves`, `slot_leaves`), as are the
weapon-class methods `0x40e62c`…`0x40f8ad` (group `weapon_syncs`), the fire and
animation-manager methods (`0x46b905`, `0x46b910`, `0x478760`…`0x478804`), the
eleven stage-5 callbacks, `0x408f84` and `0x409904` (groups `manager_slots`,
`stage_flush`). `0x46b8ea` and the lazy matrix `0x401fe4` stay original with H3
verdicts (draw side). Of the 12 H4-verdict functions that were original at 485
hooks, none needs a hook: nine are hooks, and `0x427d98`, `0x427da0` and
`0x47df04` are computed in core by hooked callers (`sweep`, `convoy_follow`,
`collision_walk`, `formation_steer`) and need no dispatch entry
([function_verdicts.md](function_verdicts.md)).

The function index counts the range per regeneration (area table in
[docs/functions](../functions/index.md#areas-module-map-of-dbsim_gapmd-section-1);
regenerate with `tools/reference-game/scripts/regenerate_function_docs.ps1`).
Of the entries that are not hooks, 18 H4-verdict ones are empty element
constructors and empty bodies that the hooks reach through the `0x49667c` array
constructor or a vtable (`0x406a5c`…`0x406a85`, `0x40c8be`, `0x40cf04`,
`0x41178d`, `0x4117a5`, `0x42067c`, `0x4207bb`, `0x4207db`, `0x420817`,
`0x423078`, `0x423098`, `0x425344`, `0x426514`, `0x427207`); 20 more are
`0x407b54`, `0x40874c`, `0x408860`, `0x408fb8`, `0x4097a8`, `0x409a34`
(load-time set-ups), `0x4083f8`, `0x40c834`, `0x40ce28`, `0x40e61c`,
`0x40fb2c`, `0x40fea0`, `0x413ac8`, `0x417530`, `0x417d08`, `0x420634`,
`0x427d98`, `0x427da0` and the two never-called type-index leaves
`0x41ff00`/`0x422e80` (computed in core). Each is reached through a vtable slot
or the array constructor, so a caller cannot compute it in core without knowing
the receiver class; every slot target must have own code in an own process
(the dispatch rows of [own_process.md](own_process.md)).

### 4.2 3D renderer, camera and view (R3D)

Terrain draw `0x46cb9c` with its walkers and cell draw, sky and horizon, object
binning and the sort tree, the DTS part and polygon draw slots, the lights, the
view transform and projection, the polygon fill and edge tables, texture
spans, Gouraud fills, the graphics library's bitmap and rectangle primitives,
the outside camera `0x401c74` and the view-subject code. Largest: `0x486ee8`
(919 instructions), `0x468760` (693), `0x46d448` (684), `0x477d48` (587),
`0x48817c` (556). Hooks in these modules include `camera_update`, `segment`,
`terrain_height`, `poly_clip`, `poly_spans`, `tex_edge`, `shape_instance`.
Couplings: two clock calls (`0x471e10`, `0x471e88`, rare), heap allocations of
the light module `0x407388`/`0x407444` (every frame), the `0x48c444(0)`
view-scratch rebuild that `sfx_place` also uses
([audio.md](../reverse-engineering/audio.md#open-questions-of-the-boundary)).
Own code in `es2sim`: `terrain_draw.c`, `frustum.c`, `scene.c` (sky, horizon,
binning, sort tree), `shape_draw.c`, `poly.c`, `fill.c`, `gouraud.c`, `gfx.c`,
`view.c`; contracts in [terrain.md](../reverse-engineering/terrain.md),
[horizon.md](../reverse-engineering/horizon.md),
[rasterizer.md](../reverse-engineering/rasterizer.md) and
[camera.md](../reverse-engineering/camera.md).

### 4.3 Cockpit, HUD and display modes (HUD)

The display transitions `0x42a4c4`/`0x42a9c0` (2,480 instructions with its
slides), the shield-colour palette `0x443918` (1,210), the gunsights, the MFD,
HDD and radar pages, the gauges, the message queue and ports, the widget
library, the cockpit startup and power-on sequences, the jolts and bumps. **The
compared side effects live here:** 33 clock sites, 8 stream-B sites, 14
sound-API calls. Hooks include `panel_frame`, `cockpit_*`, `hud_key`, `map_*`,
`mouse_dispatch`, `wingman_*` and the slider and throttle gadgets. A
replacement keeps the clock and stream-B sequence per frame and per message,
the target clear and `cockpit_sync`'s writes, and the panel-off byte and the
display mode the key dispatchers read
([display_modes.md](../reverse-engineering/display_modes.md#simulation-relevant-edges)).
Own code in `es2sim`: `hud_cockpit.c`, `hud_frame.c`, `hud_display.c`,
`hud_draw.c`, `hud_gadgets.c`, `hud_gauges.c`, `hud_radio.c`, `hud_state.c`,
`hud_widgets.c`, `hud_load.c`, `cockpit.c`, `gadgets.c`, `mfd.c`, `panel.c`,
`display.c`, `widget_leaves.c`; contracts in [hud.md](../reverse-engineering/hud.md),
[cockpit.md](../reverse-engineering/cockpit.md) and
[display_modes.md](../reverse-engineering/display_modes.md).

### 4.4 Modal dialogs and alerts (DLG)

The mission-end alert `0x455950` with `0x4541b8`, `0x45464c`, `0x4548c8`, the
modal loop `0x454acc` (one clock call per pass) and `0x4543b0`; entered once
per suite that ends in the logic frame (0/8/0/1/1/1 in the baseline suites).
The F11 options, Ctrl+Q and F12/Alt+P dialogs are never entered in the map
suites (9 / 1,627 in reach). The alerts are answered by pump-keyed journal
entries ([input_journal.md](input_journal.md)), so their pump count is part of
the oracle contract. Own code: `dialogs.c`
([main_loop.md](../reverse-engineering/main_loop.md)).

### 4.5 Presentation (PRES)

`0x45fba4` (presentation frame), `0x4306ac` (palette cycling, one clock call
per frame), `0x43048c`, `0x465628` (present), `0x465cc4`, `0x465e28`,
`0x465a24`, `0x46548c`, `0x4649d8`. The own presenters replace the sinks and
keep `0x45fba4`'s call sequence (item 5 of the keep-list in
[presentation.md](../reverse-engineering/presentation.md#keep-list-for-a-presenter)).
Own code in `es2sim`: `present.c` (the presentation frame and palettes),
`frame_buffer.c` (DIB section `0x464d44`), `platform_sdl.c` (the window).

### 4.6 Process shell (SHELL)

The game main and main loop `0x45f160`, MainWndProc `0x466034`, the pump
`0x464c78`, the limiter `0x4678c0` and the clock wrapper `0x467828`, the
mission end `0x461ff0`; before arming WinMain, the parsers, window class and
creation, pause/resume `0x45f0d4`/`0x45f108`, the abort-notify list. The
CW3220 lifecycle (startup, exit list) is own code behind the shim; WinMain and
the game main stay original in the oracle line (launcher contract: options,
exit code `0x4d27b8`, [launcher.md](../reverse-engineering/launcher.md)). Own
code in `es2sim`: `main.c`, `host.c`, `startup.c`, `loop.c`.

### 4.7 Audio (AUD)

The SFX manager below the `sfx` hooks (open, cache load, play, stop, volume,
pan, rate), the voice start `0x462940`/`0x462b9c` (clock), the lip timeline
rest `0x4632c4` (clock), the SOS glue and the stubs into `SOS9503.DLL`, the
instrument-bank loader, CD music through MCI
([dll_imports.md](dll_imports.md) inventories the SOS, WINMM and BWCC32 entry
points). Own code in `es2sim`: `audio.c` (the SOS driver stand-in and mixer)
and `audio_sdl.c` (the SDL3 sink); the contract is
[audio.md](../reverse-engineering/audio.md).

### 4.8 Input (INP)

The device poll `0x45baa8` with its readers, the joystick summary `0x477904`,
the keyboard ring and the mouse queue `0x480948`/`0x4809f8`, the flight-key
capture `0x45b68c`. The per-frame record is own code (`control_record`). Own
code in `es2sim`: `input.c` (the journal provider) and `joystick_sdl.c`;
contract [input.md](../reverse-engineering/input.md).

### 4.9 Files and asset loading (FILE)

The VOL archive's buffered reads `0x47c7d8`/`0x47c82c`/`0x47c8c4`, the read
stream constructor and destructor, the DTS persistent-object reader
`0x47a144`/`0x47a6b4` and the `BMP_8`/`GLBitmap` read slots (loaded on demand
during the mission), the STR access, path builders. The stream hooks and the
file imports of CW3220 are own code. Own code in `es2sim`: `volumes.c` (VOL
registration), `services_files.c`; the formats are read through `formats/`
([file_io.md](../reverse-engineering/file_io.md)).

### 4.10 Runtime (RTL)

`0x49630c` (exception-frame set-up, 4.28 million bridged calls), `0x49667c`,
`0x496406`, `0x4953c4`, the game allocator `0x47749c`/`0x4774f0`/`0x477548`/
`0x47758c`/`0x47747c`, the arena `0x47a288`, the import thunks. The pieces
linked into DBSIM stay as long as original DBSIM code runs
([rtl_imports.md](rtl_imports.md#6-what-the-own-runtime-must-keep)); DBSIM
never throws, so an own process needs none of the exception bookkeeping, but
it reproduces the heap contract. Own code in `es2sim`: `heap.c`,
`heap_win32.c`, `services_rtl.c`, `init.c`.

## 5. Before arming: start-up, static initialisation, mission load

**Static initialisation (confirmed static).** 65 init records
`0x4a79d8..0x4a7b5d` (6 bytes each; function at byte 2), all distinct, no hook
among them or in their closure (133 functions, 3,063 instructions): 31 are in
simulation modules (roles not examined; hypothesis: module globals and class
tables), 13 in the HUD, 9 in the renderer, 3 each in input and the shell (the
globals constructor `0x45caf4` with the RNG seed), 2 each in presentation and
the dialogs, one each in audio and the runtime. The 26 exit records
`0x4a7b5e..` reach 79 functions / 1,334 instructions. Own code: `init.c`
([own_process.md](own_process.md#static-initialisation-the-65-init-records-and-the-26-exit-records)).

**Mission load (confirmed, sizes estimate).** The pieces of
[mission_load.md](../reverse-engineering/mission_load.md#10-the-load-in-hookable-pieces),
each with its own simulation closure (non-hook functions in the simulation,
load and math areas; "new" = not already counted by an earlier piece):

| Piece | Roots | Closure | New |
|---|---|---:|---:|
| L1 pool construction | `0x471ad8` | 2 / 90 | 2 / 90 |
| L2 leaves | `0x425348`, `0x411b90`, `0x415068`, `0x423358`, `0x423104`, `0x423d38` | 7 / 286 | 7 / 286 |
| L3 initial damage | `0x4178e8` | 1 / 94 | 1 / 94 |
| L4 weapon fit | `0x4175dc`, `0x4104ec`, `0x40fff8` | 37 / 2,121 | 37 / 2,121 |
| L5 constructors | `0x415bb0`, `0x4215f4`, `0x405314`, `0x423b34`, `0x405db0` | 36 / 2,819 | 30 / 2,569 |
| L6 actor load | `0x4253d8` | 79 / 6,612 | 7 / 1,618 |
| L7 world load | `0x424308` | 47 / 3,640 | 29 / 2,930 |
| L8 stage-2 pool callbacks | `0x4076e4`, `0x4090cc`, `0x40a060` | 8 / 425 | 6 / 335 |
| L9 mission load | `0x461608` | 118 / 10,385 | 5 / 677 |
| **Union** | | | **124 / 10,720** |

Outside the simulation part the load builds presentation-side state: the
cockpit object and its gadgets (HUD 153 / 16,460, e.g. `0x43c800`, `0x445240`,
`0x448cf0`, `0x44f6f4`), terrain and renderer state (R3D 191 / 9,963), the sound
context and sample preload (AUD 71 / 3,079), palettes and the loading screen
(PRES 29 / 2,130), the input devices and `keyjoy.cfg` (INP 49 / 2,028) and the
file reads (FILE 63 / 1,904). Some of that state is simulation-visible (the
cockpit target `H+0x210`, the HUD records `cockpit_sync` copies, the input
device block), so an own load builds those fields even without drawing.

The current map arms at the mission-load call and measures the load and the
first pass of the loop body together (section 3); the start-up and static
initialisation before it are static upper bounds.

## 6. The own process against the areas

`es2sim` ([own_process.md](own_process.md)) is the own `dbsim.exe`: a host that
provides the guest address space, not a port. The core is written against the
original memory layout (32-bit guest addresses; `es2_guest_memory`,
[guest.h](../../core/include/es2/core/guest.h)), the adapters call remaining
original code through the guest bridge in the oracle line, and `es2sim`
dispatches every reached original address and vtable slot to own code.

| Step | What it is in the original | Own code |
|---|---|---|
| Host | DATA/BSS image (`0x497000..0x6cbee8`) with its tables, CW3220-compatible heap, dispatch from every reached VA and vtable slot, stack-residue values read by callees (input.md "Stack residue under the command handler", ai.md "Stack residue under aim_fire") | `host.c`, `image*.c`, `heap.c`, `dispatch*.c`, `residue.c`; [own_process.md](own_process.md) sections 2 to 5 |
| Process shell and lifecycle | WinMain, game main and main loop, both option parsers, window, pump, window procedure, limiter and clock under virtual time, mission end, exit code | `main.c`, `startup.c`, `loop.c`, virtual time of `sim/oracle` |
| Static initialisation | the 65 init records in table order, the 26 exit records | `init.c` |
| Data and configuration | VFS and VOL enumeration, STR, `prefs.cfg`/`keyjoy.cfg`/`sound.cfg`, DTS/DBM/DPL loading, on-demand bitmap and shape reads | `volumes.c`, `services_files.c`, `formats/` |
| Mission load | world and actor load, constructors, weapon fit, pools, stage-2 callbacks, initial damage (stream A), the simulation-visible fields of cockpit, HUD records and devices | `load.c`, `hud_load.c`, the load pieces as hooks in `core/` |
| Simulation frame | logic frame, render root and the leaves of section 4.1 | the hooks of `core/` and the service rows |
| Coupling set | every clock read, stream-B draw and sound call of HUD, presentation, dialogs and audio in the original order | `hud_*.c`, `present.c`, `dialogs.c`, `audio.c` |
| Input, audio, drawing | sections 4.2, 4.3, 4.7 and 4.8 | the files named there |

**What the headless run compares.** The own process replays every suite and
produces the tick log, the mission watches and the return files that
`oracle_regression.py` compares (completed clock, timing bytes, both RNG
states, player state, watches, `results.dat`/`mission.var`); nothing in the
comparison needs pixels or sound. It does need the coupling set, because the
clock and stream B are compared, and the pump-keyed modal answers, because the
journal addresses them by pump count. The frames are compared separately
against the original's captured frames (`//sim:frames_all`).

**Constraints (hypotheses unless a document confirms them).** (1) Memory
layout: core reads and writes the original object layout at guest addresses;
whether the heap must also keep CW3220's address sequence is measured in
[rtl_imports.md](rtl_imports.md). (2) Stack residue: callees that read stack
they did not write (the group order under `hud_key`, the AI state methods under
`order_update`) see the original's residue; once they are own code the residue
is a modelled value (`residue.c`). (3) The coupling set is measured per call
site ([hud.md](../reverse-engineering/hud.md#the-coupling-set-measured)): 10
clock reads per tick with the HUD drawn and 9 without, 35 running HUD clock
sites; the per-frame part of hud.md's "For H3" item 3 holds exactly, and the
extra reads come from messages, blips, jolts, the radio panel and the dialogs.

## What `es2sim` takes from `DBSIM.EXE`

`es2sim` builds its guest image from compiled-in sources
(`sim/data/dbsim_data.c`, `sim/data/dbsim_image_tables.c`, generated from the
hash-checked file by `tools/reference-game/scripts/dbsim_data.py` and
`dbsim_image_tables.py`) and opens no file of the CD for it
([own_process.md](own_process.md#the-initialised-data-and-the-code-bytes-as-own-runs)).
The file path (hash gate, PE parse) survives only in the testonly
`//sim:image_file`, which compares the own tables with the file. The sections
of the original (confirmed, PE headers; the figures agree with
own_process.md section 2.2):

| Section | Bytes in the file | Use in `es2sim` |
|---|---:|---|
| headers | 1,024 | the image layout (entry, sections, directories) as own tables |
| CODE | 613,376 (vsize 614,400) | mapped no-access except the pages of the 7 read sites below; the dispatch traps every execution |
| DATA, initialised | 68,608 | compiled-in runs, mapped read-write: the program's tables (below) |
| DATA, zero-filled (BSS) | none (vsize rest 2,245,632) | nothing: own code fills it |
| `.idata` | 3,584 | the 134 imports from which the host writes its refusal traps (`es2_sim_image_imports`) |
| `.edata` | 512 | not read (grep) |
| `.reloc` | 35,840 | the inventory only: the guest keeps the original VAs, nothing is applied |
| `.rsrc` | 1,024 | the class icon, set on the window of `es2sim_window` |

**Resources (confirmed, resource directory).** `DBSIM.EXE` has exactly two:
one `ICON` (744 bytes) and one named `GROUP_ICON` (20 bytes), the class icon
`"es2icon"` of the window-class registration `0x4650e8`. There is no `DIALOG`,
`MENU`, `STRING`, `CURSOR`, `BITMAP`, `ACCELERATOR` or `VERSION` resource: the
modal dialogs are drawn by the game itself (`sim/src/dialogs.c`), the class's
menu name 1 names no resource (`sim/src/input.c`), the two cursors are the
system's `IDC_ARROW` (stage 3 at `0x45f430`) and `IDC_WAIT` (WinMain), and the
texts come from the CD's STR files and from the string literals in DATA.
`VSHELL.EXE` and `ES.EXE` have the same two-entry resource directory. The
window icon of `es2sim_window` is built from the compiled-in icon
(`es2_sim_window_load_icon_own`, `sim/src/platform_sdl.c`).

**CODE read as data (grep, each site read in the source).** Seven sites, all
byte or dword tables inside CODE; their pages are the ones the CODE protection
leaves readable (`es2_sim_image_table_sites` in `sim/src/image_access.c`):

| VA | Reader | What |
|---|---|---|
| `0x4100fd` + i | `core/src/mission_load.c` | index bytes of a jump table |
| `0x405366` + t | `core/src/mission_load.c` | case bytes (t ≤ `0x3d`) |
| `0x426492` + i | `core/src/mission_load.c` | type-word bytes |
| `0x436be2` + code | `sim/src/hud_widgets.c` | index bytes for codes 0 … `0x38` |
| `0x44bb9f` + kind | `sim/src/hud_draw.c` | index bytes by kind |
| `0x44e1f4` + t | `sim/src/hud_draw.c` | a 29-row byte table |
| `0x4967ae` | `sim/src/input.c`, `sim/src/services_files.c` | a dword of an import thunk's code bytes (the stream error status) |

The adapters' image-base offsets into CODE (5 distinct in `*_hooks.h`) are call
targets of guest bridges, not reads. Measured with `--image-access` (CODE
no-access outside the seven table pages, every other page guarded): the 40
replays read 14 RTTI count dwords from CODE (served from the own runs by the
models of `0x496406`) and all 17 DATA pages, and make no other CODE read
([own_process.md](own_process.md#what-es2sim-really-reads-from-the-mapped-image)).

**The initialised DATA by kind (confirmed, bytes classified).** The 68,608
bytes:

| Kind | Bytes | Count |
|---|---:|---|
| code-pointer slots (relocated dwords into CODE: vtables, callback and handler tables) | 10,492 | 2,623 slots in 960 runs, the longest 36 slots |
| data-pointer slots (relocated dwords into DATA) | 3,324 | 831 slots (702 into the initialised part, 129 into BSS) |
| string literals (printable runs of 4 or more bytes ending in NUL) | 9,826 | 653 runs |
| zero bytes (outside the above) | 27,152 | — |
| other non-zero bytes (numeric tables, records, short strings, flags) | 17,814 | densest in `0x4a1000..0x4a2fff` (5,574) and `0x4a4000..0x4a4fff` (3,585) |

Relocated references (confirmed, `.reloc`, 16,850 HIGHLOW sites): from CODE
4,790 into the initialised DATA and 7,064 into BSS, with 2,852 distinct targets
in the initialised part and 1,336 in BSS. Distinct initialised-DATA targets by
the area of the referencing CODE site (a target referenced from two areas
counts in both): SIM 907, HUD 483, R3D 343, SHELL 171, DLG 125, INP 57, AUD 44,
FILE 32, PRES 25, LOAD 20, RTL 18, MATH 7, no area 74.

Own code names (grep) 1,172 distinct initialised-DATA addresses (`sim/src`
824, `core/src` 415, 67 in both); 305 of `sim/src`'s are the constant argument
of a read call (and 360 BSS addresses likewise); the adapters form 6
initialised-DATA and 5 BSS addresses from the image base (the math tables,
own_process.md section 2.3). Beyond the named addresses, every vtable slot own
code calls through and every pointer it follows is read from the initialised
DATA at run time.

## What `es2shell` and `es2launch` take

- **`es2shell` and `es2menu` carry VSHELL's data as a compiled-in store**
  (`shell/data/vshell_store_data.c`, generated from `VER95/VSHELL.EXE` by
  `tools/reference-game/scripts/vshell_store.py`, runs at their original VAs
  plus the class icon `ES2ICON`; checked against the file by
  `//shell:vshell_store_real_test`). Neither reads `VSHELL.EXE`. The data is
  the engine tables (`shell/src/engine.c`: 3 preparation spans, 9 preset paths
  and 9 ARM names through pointer tables, 7 results spans, the weapon rows), the
  practice tables (`practice.c`, 3 spans), the prefs defaults (`host.c`), the
  map's cosine and arctangent tables, the hot bytes and a few smaller spans
  (`gui/app.c`), and 91 distinct literal addresses (705 uses) named by the
  generated layout table (`ES2_UI_REF_EXE_STRING`,
  `shell/data/vshell_layout.json`). About 20 reader call sites in four files
  (grep).
- **`es2launch` reads no original executable** (grep): it reads `ERROR.STR`
  (CD data) and starts children: `es2shell` (or the original shell) and
  `es2sim_window.exe` (or the original `DBSIM.EXE` under the proxy in the
  oracle line). `ES.EXE` is not used by the own chain.

## Which DLLs the chain loads

**The own executables (confirmed, import tables of the builds):**
`es2launch.exe` imports KERNEL32, USER32 and the UCRT; `es2sim.exe` the same;
`es2sim_window.exe` adds `SDL3.dll` and WINMM (MCI); `es2shell.exe` adds
`SDL3.dll`, WINMM, GDI32 and SHELL32. No game DLL is imported. **Dynamic loads
(grep):** none of a game DLL in `sim/src` or `launcher/src`. `es2sim` stands in
for `BWCC32.DLL` (the load is only counted, `startup.c`) and for the SOS driver
(a stand-in module handle and the own mixer, `audio.c`), and runs the CW3220
imports as own code (`core/src/rtl*.c`), so no `cw3220.dll` is loaded either.
`es2shell`'s movie player loads the **system** `msvfw32.dll` and opens the
Indeo wrapper `ir32_32.dll` through WINMM's `OpenDriver`, both from System32
(`media/src/iv32_bind.c`): Windows components, not game files. `es2sim` plays
no AVI: the AVI class's MCI calls are out of scope
([audio.md](../reverse-engineering/audio.md)) and no mission path reaches the
AVI code (open point 3).

**The original line, for comparison** ([dll_imports.md](dll_imports.md),
[rtl_imports.md](rtl_imports.md)): `DBSIM.EXE` under the proxy loads the own
`cw3220.dll`, `BWCC32.DLL` (unless `ES2_BWCC32=refuse`; identical in the 40
suites, es2-data `logs\check-r13-3`), the SOS driver `SOS9503.DLL` (with
`SOSLIBS3.DLL`, `HMIDRV.WIN`, `HMIMDRV.WIN` and `SOSLIB.INI` as its
installation companions), the proxy `ddraw.dll`, WINMM and MSVFW32.

**Files of an installation that the own chain does not use:** `ES.EXE`,
`VSHELL.EXE`, `DBSIM.EXE`, `BWCC32.DLL`, `SOS9503.DLL`, `SOSLIBS3.DLL`,
`HMIDRV.WIN`, `HMIMDRV.WIN`, `SOSLIB.INI`, `CW3220.DLL` (own, for the original
line) and `CW3220-ORIGINAL.DLL`. `retire-originals.ps1` removes `DBSIM.EXE`
and `VSHELL.EXE` from played installations; the measurement clones keep both
for the oracle, the in-vitro harness and the reference tooling (es2prep,
es2loadout, es2campaign). The chain ran end to end on a CD copy without
`VER95\DBSIM.EXE` and `VSHELL.EXE`: signatures, audit, 40 live suites, own
shell, 40 replays and frames 40/40 passed (es2-data `logs\check-r13-1`).

What is left of the CD in a played session are its data files: the VOL
archives, the texts, the movies and the online manual, which the own
executables read like the original does (`cd_root`).

## Open points

1. **Start-up and static initialisation counts.** Code before the
   mission-load call is a static upper bound; the map arms there.
2. **Area boundaries.** The module map is an estimate; the function index
   lists the gaps without an area and the boundaries without a function entry.
   The windowed start-up with `--image-access` is open in
   [own_process.md](own_process.md).
3. **Lower counts under replaced hooks.** 27 rows of the current map run less
   often than in the baseline ([dbsim_map.md](dbsim_map.md#map-with-all-hooks-replaced));
   they are not traced. The suites are identical to their goldens.
