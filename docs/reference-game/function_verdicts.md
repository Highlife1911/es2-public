# H4 candidate verdicts

One verdict for every function of the generated lists in [hook_candidates.md](hook_candidates.md) and
[hook_frontier.md](hook_frontier.md), plus the callees and helpers around them: the functions that hooks still reach
through the guest bridge, the single-callee helpers of hooked groups and the import thunks they call.
`tools/reference-game/scripts/hook_candidates.py --classified` reads the table under [Verdicts](#verdicts) and leaves
every function whose verdict is not H4 out of the generated lists (it prints them in a short section at the end), so
the lists show only open targets. `tools/reference-game/scripts/function_index.py` reads the same rows for the
function pages in [docs/functions](../functions/README.md).

Method: [How verdicts are derived](#how-verdicts-are-derived). Every verdict is a hypothesis for the hook that
implements it to confirm from the bytes. Reasoning that does not fit a table cell is in
[Evidence for single verdicts](#evidence-for-single-verdicts); the tables of the subsystem sections after it carry
the hooks per area, their contracts and the live counts.

## Legend

| Verdict | Meaning |
|---|---|
| H4 | simulation: changes compared state; an open H4 target, or a hook (the package column names it) |
| in-progress | taken by a running task (named in the package column); none at present, every former entry is a hook with its name in the package column |
| H3 | presentation: draws, or writes only cockpit/HUD/widget/raster/palette state that only presentation reads (side effects such as clock calls or stream-B draws are listed in the linked document and stay original until H3) |
| platform | operating-system or driver layer: Win32 shell, file I/O (VFS), sound driver and SFX manager; replaced by the H3 platform leaves, never by an H4 hook |
| RTL | runtime library (CW3220/Borland helpers); H6 |
| dead | no caller and no reference in the image |

Counts in the reasons are the map's entry probes per suite (a60/c1s/c1c/c3_09/c4_08/c3_10, saturated at 1000) unless
a section says otherwise.

## Verdicts

| VA | Verdict | Package or document | Reason |
|---|---|---|---|
| `0x486ee8` | H3 | [player_movement.md](../reverse-engineering/player_movement.md#graphics-library-candidates-and-the-texture-edge-setup-48aaac) | Gouraud triangle: flat bands through the polygon fill `0x483eb8`; writes only the graphics context colour |
| `0x477d48` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-polygon-cluster) | edge-table setup of the polygon fill `0x468414`; writes the rasterizer-private tables `0x6c5390..0x6c56a4` |
| `0x465628` | H3 | [flyer.md](../reverse-engineering/flyer.md#function-table) | frame presentation (DirectDraw/GDI blit, cursor sprite) |
| `0x463a14` | platform | [audio.md](../reverse-engineering/audio.md#the-sound-api-functions) | SFX manager open: handle and file record in the audio-owned context tables |
| `0x42ec68` | H3 | [horizon.md](../reverse-engineering/horizon.md#what-the-cluster-is) | horizon line of the `hzline` painter; writes only the painter object `H+0x38..+0x4c` |
| `0x465a24` | H3 | [flyer.md](../reverse-engineering/flyer.md#function-table) | builds the GDI logical palette |
| `0x43d9b4` | H3 | [hud.md](../reverse-engineering/hud.md#h4-candidates) | HUD record update: copies the 0x26-byte record into HUD gadgets and calls their virtual `+0xc` ("never H4" in the HUD notes) |
| `0x434e8c` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#classification-table) | cockpit message-queue insert (Q+C, two original defects); only with the whole queue in H3 |
| `0x47d198` | platform | [file_io.md](../reverse-engineering/file_io.md#the-stream-library-and-path-helpers) | `fnsplit`-like path splitter of the VOL member lookup on CW3220 string imports |
| `0x469894` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-polygon-cluster) | polygon clip intersection with `x = c`; output vertex only |
| `0x469b2c` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-polygon-cluster) | polygon clip intersection with `y = c`; output vertex only |
| `0x46548c` | H3 | [flyer.md](../reverse-engineering/flyer.md#function-table) | clears the frame buffer and shows it |
| `0x46ad4c` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-polygon-cluster) | hand-written affine texture span loop (writes the destination row) |
| `0x465cc4` | H3 | [flyer.md](../reverse-engineering/flyer.md#function-table) | palette upload (`SetDIBColorTable`/`AnimatePalette`/DirectDraw palette) |
| `0x42d2f8` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#simulation-relevant-edges) | start a screen jolt (U+D+G): page origin and driver calls |
| `0x488974` | H3 | [player_movement.md](../reverse-engineering/player_movement.md#graphics-library-candidates-and-the-texture-edge-setup-48aaac) | bitmap draw through the blit slots `[0x4a5818]`/`[0x4a5820]` |
| `0x4735b8` | platform | hook `stream_read` ([file_io.md](../reverse-engineering/file_io.md#the-stream-library-and-path-helpers)) | read method of the VOL-aware stream |
| `0x433b20` | H3 | [hud.md](../reverse-engineering/hud.md#hud-region-helpers) | cursor zone `H+0x22e`, read only by the cursor image draw |
| `0x433c54` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | cursor image draw (`0x48992e`/`0x486e70`) |
| `0x440d74` | H3 | [hud.md](../reverse-engineering/hud.md#hud-region-helpers) | `EnergyWeaponGauge` slot `+0x10`: copies the record into gauge fields (U+D) |
| `0x42da08` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#simulation-relevant-edges) | border fill around the 3D view (`0x486704`) |
| `0x40bb4c` | H3 | [weapons.md](../reverse-engineering/weapons.md#weapon-group-and-sphere-helpers) | near-plane clip of the tracer draw slot `0x40bc14` |
| `0x439758` | H3 | [hud.md](../reverse-engineering/hud.md#hud-region-helpers) | LED segment rows through `0x483a04` |
| `0x4887d8` | H3 | [player_movement.md](../reverse-engineering/player_movement.md#graphics-library-candidates-and-the-texture-edge-setup-48aaac) | bitmap draw (RLE path `0x488df8` or the blit slots) |
| `0x48c954` | H3 | [player_movement.md](../reverse-engineering/player_movement.md#graphics-library-candidates-and-the-texture-edge-setup-48aaac) | DTS polygon vertex transform and projection into the render context `0x6c60d4` |
| `0x43dcd4` | H3 | [hud.md](../reverse-engineering/hud.md#hud-region-helpers) | mission time text (one clock call per entry, `MM:SS` digits) |
| `0x443918` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#classification-table) | shield colours into the palette (72 × `multiply_q10`, `0x4303c4`) |
| `0x471f44` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-cell-draw-0x46d448) | terrain draw walk (one of the two walkers under `0x46d1a8`) |
| `0x466034` | platform | [flyer.md](../reverse-engineering/flyer.md#function-table) | MainWndProc (Win32 shell) |
| `0x43dd98` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | flyer altitude ladder draw |
| `0x42f0b0` | H3 | [horizon.md](../reverse-engineering/horizon.md#what-the-cluster-is) | ground fill below the horizon |
| `0x42ee88` | H3 | [horizon.md](../reverse-engineering/horizon.md#what-the-cluster-is) | sky bands above the horizon |
| `0x48ca70` | H3 | [below](#0x48ca70-and-0x48cf20-projection-and-near-plane-clip) | vertex transform and projection of the current polygon into the render context `0x6c60d4` (tracer and `TexPoly` draws) |
| `0x46dfa8` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-cell-draw-0x46d448) | terrain walk callback (column) → cell draw `0x46d448` |
| `0x46dddc` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-cell-draw-0x46d448) | terrain walk callback (row) → cell draw `0x46d448` |
| `0x42d624` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#simulation-relevant-edges) | end a screen jolt (U+D+G) |
| `0x4699c8` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-polygon-cluster) | polygon clip at the viewport's left and right edge |
| `0x469c64` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-polygon-cluster) | polygon clip at the viewport's top and bottom edge |
| `0x42d4a8` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#simulation-relevant-edges) | jolt step (U+G) |
| `0x4469e8` | H4 | hook `mfd_key` (group `mfd_keys`; [cockpit.md](../reverse-engineering/cockpit.md#mfd-keys)) | `MFDGauge` key method (slot `+0xc`): D, 213h, 214h, X press MFD buttons 7..10 (the cockpit target, range and order through `mfd_button`), entry steps `0x44703c`/`0x447070` and the jump table to `0x447158`; its parent is the `hud_key` hook ([cockpit.md](../reverse-engineering/cockpit.md#verdicts)) |
| `0x48cf20` | H3 | [below](#0x48ca70-and-0x48cf20-projection-and-near-plane-clip) | near-plane clip and projection of the current polygon (render context `0x6c60d4`) |
| `0x433968` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#simulation-relevant-edges) | move the scroll zones (rectangle arithmetic on presentation state) |
| `0x4033e4` | H3 | [vtables.md](../reverse-engineering/vtables.md#21-slot-matrix), section 2.2 | ACTOR draw slot `+0x00`: LOD pick and shape draw; keep its idempotent lazy pose-matrix fill `0x401fe4` ([terrain.md](../reverse-engineering/terrain.md#the-cell-draw-0x46d448) keep list) |
| `0x487acc` | H3 | [player_movement.md](../reverse-engineering/player_movement.md#graphics-library-candidates-and-the-texture-edge-setup-48aaac) | Gouraud polygon (fans into `0x486ee8`); related, not in the current list |
| `0x468414` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-polygon-cluster) | 2D polygon fill; related |
| `0x468760` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-polygon-cluster) | 2D polygon fill (second variant); related |
| `0x46d448` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-cell-draw-0x46d448) | terrain cell draw (684 instructions, seven live callees); related |
| `0x472504` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-cell-draw-0x46d448) | second terrain draw walker; related |
| `0x46d1a8` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-cell-draw-0x46d448) | terrain draw pass (eye cell, walker choice); related |
| `0x464a14` | H3 | [flyer.md](../reverse-engineering/flyer.md#function-table) | present the whole frame through `0x465628`; related |
| `0x465c40` | H3 | [flyer.md](../reverse-engineering/flyer.md#function-table) | flush the system palette to black; related |
| `0x465e28` | H3 | [flyer.md](../reverse-engineering/flyer.md#function-table) | install a game palette; related (the live callee of `0x466034`) |
| `0x464c78` | platform | [flyer.md](../reverse-engineering/flyer.md#function-table) | message pump (`PeekMessageA`/`DispatchMessageA`); related |
| `0x4303c4` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#classification-table) | palette range copy (presentation); related |
| `0x43271c` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#simulation-relevant-edges) | move the cockpit panel (U); related |
| `0x434010` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#classification-table) | panel jolt on damage (U+C+B; stays original under `ground_collide`); related |
| `0x43408c` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#classification-table) | jolt per frame (U+C+B); related |
| `0x47cdf8` | platform | hook `member_lookup` ([file_io.md](../reverse-engineering/file_io.md#the-stream-library-and-path-helpers)) | VOL member lookup |
| `0x47d2c4` | platform | [file_io.md](../reverse-engineering/file_io.md#the-stream-library-and-path-helpers) | path concatenation (inverse of `0x47d198`); related |
| `0x49667c` | RTL | [weapons.md](../reverse-engineering/weapons.md) | Borland runtime array construction; stays original |
| `0x49630c` | RTL | `NEVER` in `hook_candidates.py` | exception-frame setup of the runtime |
| `0x496406` | RTL | `NEVER` in `hook_candidates.py` | runtime wrapper |
| `0x4953c4` | RTL | `NEVER` in `hook_candidates.py` | runtime wrapper |
| `0x464a28` | dead | [flyer.md](../reverse-engineering/flyer.md#function-table) | no caller (window-rectangle present) |
| `0x464ad8` | dead | [flyer.md](../reverse-engineering/flyer.md#function-table) | no caller (`LoadStringA`/`MessageBoxA`) |
| `0x465fd4` | dead | [flyer.md](../reverse-engineering/flyer.md#function-table) | no caller (`SetWindowTextA`) |
| `0x432b14` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | HUD part of the input record: cursor zone `0x433b20` (H3) when `H+0x23e`, record bytes `+0x1e..+0x21` → display requests `0x42a3f4` 1/0/5/4, mouse latch `0x49b550`, panel virtual `+0xc(0xf)`; no simulation write (bridged from `player_input`) |
| `0x401c74` | H4 | hook `outside_camera` ([camera.md](../reverse-engineering/camera.md#outside-camera-0x401c74c-a-b-c-logic-frame)) | outside camera control: unless `C+0x4a`, by mode word `C+0x36` ramps camera words `+0x30`, `+0x24`, `+0x2c` through `0x401c20`/`0x401c4c`; logic frame (the camera feeds `projectile_flight`'s sound gate and four other simulation hooks), bridged from `player_input` and `death_sequence` |
| `0x45297c` | H3 | `mouse_dispatch` ([input.md](../reverse-engineering/input.md#mouse-event-queue-452d40--453078)) | widget library: move with a pressed button (gadget lookup `0x452a28`, redraw `0x452ab8`, drag virtual); modal-screen GUI, no compared field (bridged from `mouse_dispatch`) |
| `0x4527c8` | H3 | `mouse_dispatch` ([input.md](../reverse-engineering/input.md#mouse-event-queue-452d40--453078)) | widget library: press (gadget state word `+0x1b`, drag word `+0x1d`, drag virtual `+0x18`, cursor `0x4d2556`/`0x4d255a`); modal-screen GUI |
| `0x452898` | H3 | `mouse_dispatch` ([input.md](../reverse-engineering/input.md#mouse-event-queue-452d40--453078)) | widget library: release (activate virtual `+8` with the code of `+0x10`, button word `0x49db6c`); the effect reaches the game only through screen code |
| `0x42a4c4` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#simulation-relevant-edges) | start a display transition (page scroll `0x487e60`, retrace port `0x45c638`, layout switch `0x429e60`/`0x42a1f0`); part of the H3 display group; keep the panel-off timing listed there |
| `0x45bbc4` | platform | [input.md](../reverse-engineering/input.md#the-input-layer-as-own-code) | keyboard ring reader of the device poll `0x45baa8`: device slot `0x45bf04`, ring test `0x45bef8`, key fetch `0x45c0a0` → `0x477ad4`; the result enters the control record |
| `0x45df34` | H4 | hook `view_subject_set` ([camera.md](../reverse-engineering/camera.md#view-mode-and-view-subject-logic-frame)) | view subject setter: view subject `0x4d2684`, view word `0x4d24f0`, camera `0x4d24ea` (group camera, a free group camera or the static camera `0x4d2754`), view offsets through `0x45edb0`; logic frame (the subject and the view word are read by simulation hooks) |
| `0x432bc8` | H4 | hook `hud_key` | HUD key handler ([hud.md](../reverse-engineering/hud.md#the-hud-key-handler-0x432bc8)): S cases (weapon selection `0x432a50`, Tab lock `0x40e4ac`, R `0x41b468`, Enter `cockpit_next`, `;`) next to display requests and the F11 dialog; bridged from `player_input` and `0x45feb8` |
| `0x45feb8` | H4 | hook `command_key` | command handler (table `0x45ff50`, [input.md](../reverse-engineering/input.md#command-handler-0x45feb8-key--handler)): cheat and alert commands change simulation state (Alt+Ctrl+N actor damage through `+0x74`, VK_Q alert `0x455950`) next to view, dialogs and an exception frame; after `0x432bc8` |
| `0x40da38` | H4 | hook `region_hit` | region damage of an actor ([hit_tests.md](../reverse-engineering/hit_tests.md#simulation-state-touched)): thresholds, then `0x40cf44`, `0x40d3ec`, `0x40d434` (debris and explosion objects); bridged from `vehicle_part` and `mech_part_hit` |
| `0x48db12` | H3 | — | inverts a pose in place (transpose `0x48dc43`, negate `0x4951d6`, `rotate_point`); pure math whose callers are all render-context code (`0x48c2e4`, `0x48c3b0`, `0x48c444`, `0x48d7ec`) |
| `0x436a3c` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#0x434e8cq-rec-row-queue-insert-message-ports) | `EventPort` receive slot 0 (vtable `0x49ba7c`): stream B variant draw (rng.md) and queue insert `0x434e8c` (H3); registers flow back to the trigger dispatcher |
| `0x435c48` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#0x434e8cq-rec-row-queue-insert-message-ports) | `DialogPort` receive slot 0 (vtable `0x49bad4`): as `0x436a3c` |
| `0x43122c` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#classification-table) | text in a rectangle (panel, MFD and mission-load text through the driver pointers) |
| `0x476248` | H3 | — | DTS detail selection: projected size from the near plane `[0x6c602c]` and focal shift `[0x6c6028]` (`angle_ratio`, `heading_cos`, `multiply_q10`), writes `0x4a0fe8`/`0x6bb2f4`, view helpers `0x48c57c`/`0x48c5fc`/`0x48c6d0`; callers are the shape draws `0x490ab8`/`0x490c1c` |
| `0x43ec08` | H3 | — | `MFDRadar` blip draw (caller slot `+4` `0x43eedc` of vtable `0x49c328`, and `0x43f2d8`): actor-list scan, `dist_octagonal`, `sincos_pair`/`rotate_2d`, colours `0x4d3b88..`, clock; MFD button `0x438d9c` like `0x4469e8` |
| `0x445008` | H3 | — | `EnergyPoolGauge` slot `+8` (vtable `0x49cabc`): panel text `0x43122c`, labels |
| `0x428c60` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-cell-draw-0x46d448) | terrain cell-object draw callback (installed at `0x461982`): cell buckets `0x4aab00`, camera cull `0x401078`, actor draw slots |
| `0x4411dc` | H3 | — | `ProjectileWeaponGauge` slot `+4` (vtable `0x49c8fc`): label draw `0x438920`, bitmap `0x4887d8` |
| `0x443770` | H3 | — | `ShieldsGauge` slot `+8` (vtable `0x49ca1d`): shield colours `0x443918` (H3), gauge draws |
| `0x441744` | H3 | — | `TogglePodGauge`/`PodWeaponGauge` slot `+4` (vtables `0x49c8a4`, `0x49c8d0`): label draws |
| `0x441c3c` | H3 | — | `EnergyPodGauge` slot `+4` (vtable `0x49c878`): label draws |
| `0x4481c4` | H3 | — | HUD gadget slot `+0xc` (vtable `0x49d12c`): `panel_visible`, panel text `0x43122c`, bitmap blits through `[0x4a57ec]`, `0x4816a8` |
| `0x442cb0` | H3 | — | HUD gadget slot 0 (vtable `0x49c758`): `panel_visible`, label draw `0x438920`, canvas clip `0x45c8bc` |
| `0x446160` | H3 | — | `MFDGauge` slot `+4` (vtable `0x49cfa0`, sibling of `0x4469e8`): label draw, canvas clip |
| `0x407098` | H3 | light_level note ([main_loop.md](../reverse-engineering/main_loop.md)) | per-object light application of the draw routine `0x42876c`; the light table is presentation state; `0x407388`/`0x407444` allocate from the game heap |
| `0x436cec` | H3 | — | `EventPort` slot `+0x18`: message text draw (`0x480c34`, `0x4824ec`, `0x482634`, fill `0x486704`), clock |
| `0x4423bc` | H3 | — | helper of the `S4D` gadget (vtable `0x49c7dc`) methods `0x4426e8`/`0x4428fc`: `panel_visible`, label draw, canvas clip |
| `0x43c40c` | H3 | — | `HNavGauge` slot 0 (vtable `0x49c154`): graphics-context push `0x4311e0`, label draws |
| `0x440cb8` | H3 | — | `EnergyWeaponGauge` slot `+8` (vtable `0x49c91c`; slot `+0x10` `0x440d74` is H3, H4-41) |
| `0x4426e8` | H3 | — | `S4D` slot 0 (vtable `0x49c7dc`): label draws, `0x4423bc` |
| `0x409cd0` | H3 | — | `METEOR` draw slot `+0` (vtable `0x4982b8`): idempotent lazy pose matrix `0x401fe4` and view transform `0x48c444`, as the ACTOR draw slot `0x4033e4` |
| `0x44b3dc` | H3 | — | cockpit panel graphic state machine (with `0x44b148`, `0x44b344`): `panel_visible`, labels, canvas clip, cursor `0x4d2556`/`0x4d255a` |
| `0x44b148` | H3 | — | same state machine: graphic variant from `rng_next` stream B (rng.md), label draws; stream B stays original until H3 |
| `0x44b344` | H3 | — | same state machine: dispatches to `0x44b148`/`0x44b3dc` |
| `0x492b10` | dead | — | dead in replaced mode: the only caller `0x4929fc` is the hook `point_offset`, which computes it in core ([mission_runtime.md](../reverse-engineering/mission_runtime.md)); no pointer reference in the image |
| `0x492b31` | dead | — | as `0x492b10` |
| `0x48c444` | H3 | — | rebuilds the shared view transform `0x6c5fb4..0x6c6028` of the render context (positional sounds call it too; no simulation reader, audio.md) |
| `0x438920` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | draw a label (39 HUD callers) |
| `0x4311e0` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | push graphics context (29 draw functions) |
| `0x438bc0` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | gauge blink animation (clock and stream B through `0x438d6c`; stay original until H3) |
| `0x45c8bc` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | canvas clip rewrite and restore around a draw |
| `0x480d44` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | select a graphics context (`0x6c5f70`) |
| `0x4816a8` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#classification-table) | panel bitmap blit (GL library) |
| `0x47749c` | RTL | — | game heap allocation (95 callers; kept behind the bridge by `object_init`); memory layer, H6 like `0x49630c` |
| `0x47a288` | RTL | — | arena allocation (bump pointer in the arena `[0x6c5830]`, else heap `0x4774f0`); memory layer, H6 |
| `0x4774f0` | RTL | — | heap allocation used by `0x47a288` and the audio sample cache; memory layer, H6 |
| `0x45ee00` | platform | [file_io.md](../reverse-engineering/file_io.md#the-stream-library-and-path-helpers) | install-root path builder (35 call sites) |
| `0x47b064` | platform | [file_io.md](../reverse-engineering/file_io.md#the-stream-library-and-path-helpers) | write stream constructor (vtable `0x4a1760`) |
| `0x47b09c` | platform | [file_io.md](../reverse-engineering/file_io.md#the-stream-library-and-path-helpers) | stream destructor/close |
| `0x47b2b8` | platform | [file_io.md](../reverse-engineering/file_io.md#the-stream-library-and-path-helpers) | write stream write |
| `0x462784` | platform | [audio.md](../reverse-engineering/audio.md#the-sound-api-functions) | SFX gate |
| `0x464038` | platform | [audio.md](../reverse-engineering/audio.md#the-sound-api-functions) | SOS sample play (may load the sample) |
| `0x464618` | platform | [audio.md](../reverse-engineering/audio.md#the-sound-api-functions) | SOS volume |
| `0x4646dc` | platform | [audio.md](../reverse-engineering/audio.md#the-sound-api-functions) | SOS pan |
| `0x40d434` | H4 | hook `region_break` | body of the region damage `0x40da38` ([hit_tests.md](../reverse-engineering/hit_tests.md#simulation-state-touched)): debris and explosion objects from the pools (`0x46b430`, `0x46b48c`, `0x46b62c`), `0x40ca8c`, `0x40cf44`; six replaced callees |
| `0x44dd14` | H3 | — | HDD map label: graphics-context push `0x4311e0`, text `0x43122c`, fill `0x486704`, pop |
| `0x44aea0` | H3 | — | `HDDGauge` status page (from slot `+4` `0x449a78`): labels `0x438920`, getters `0x41bac8`, `0x44ae1c` (`condition_record`), `0x44b3dc` |
| `0x441878` | H3 | — | `TogglePodGauge`/`PodWeaponGauge` slot `+8`: panel text, blink `0x438bc0` |
| `0x441290` | H3 | — | `ProjectileWeaponGauge` slot `+8`: panel text, labels, blink |
| `0x43d704` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | `HUD` gadget slot `+8` (vtable `0x49c138`): labels, mission time `0x43dcd4`, altitude ladder `0x43dd98` (both H3) |
| `0x43b978` | H3 | — | `HUDLockingGunsight` slot 0 (vtable `0x49c1c4`): view transform and projection helpers, bitmap blit |
| `0x446350` | H3 | — | `MFDGauge` slot `+8` (vtable `0x49cfa0`): MFD page draw |
| `0x449bf8` | H3 | — | `HDDGauge` slot `+8` (vtable `0x49d64c`): HDD page draws (map, status) |
| `0x456000` | H3 | — | `GeneralAlert` slot `+8` (vtable `0x49e120`; also `0x49e1e4`): modal alert screen code (gadgets `0x45464c`, `0x4548c8`, `0x454acc`, `0x454c98`, message pump) |
| `0x456084` | H3 | — | `GeneralAlert` slot 0: canvas clip, label draw |
| `0x4769c8` | H3 | — | DTS part draw: slot `+0x1c` of the `hiG` vtable `0x4a1088` |
| `0x4763f4` | H3 | — | DTS part draw: slot `+0x1c` of the `TSBitmapPart` vtable `0x4a62b8` (view helpers, bitmap `0x48185c`) |
| `0x47613c` | H3 | — | DTS transform of a part for drawing: `matrix_compose`, view transform `0x48c444`, `0x48d718`/`0x48d7ec`; writes `0x6bb2e4` |
| `0x40bc14` | H3 | — | `BEAM` draw slot `+0` (vtable `0x4987c4`; tracer draw, near-plane clip `0x40bb4c`) |
| `0x4174c8` | H3 | — | `MECH` draw slot `+0` (vtable `0x49a282`): `0x4030d0`, ACTOR draw `0x4033e4` |
| `0x4215cc` | H3 | — | `FLYER` draw slot `+0` (vtable `0x49a5e0`): ACTOR draw `0x4033e4` |
| `0x44e954` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#0x44d888m-p-hdd-map-pick-input) | HDD map draw (reads the wingman records; view transform, clock) |
| `0x4428fc` | H3 | — | `S4D` slot `+0xc`: panel text, `0x4423bc` |
| `0x455014` | H3 | — | screen gadget slot 0 (vtable `0x49dff4`): blit slots `[0x4a5824]`/`[0x4a5828]`, label, clip |
| `0x447380` | H3 | — | `MFDSelectGadget` slot 0: clip, labels |
| `0x44b864` | H3 | — | panel graphic context select (`0x4311e0`, `0x480d44`, `0x4d1cc8`) of the `0x44b148` state machine |
| `0x43b4cc` | H3 | — | `HFixRotationGauge` slot 0: bitmap blits |
| `0x43b704` | H3 | — | `HRevRotationGauge` slot 0: clip, draws |
| `0x44750c` | H3 | — | `MFDStateGadget` slot 0: clip, labels |
| `0x434748` | H3 | — | `SystemGadget` slot 0: bitmap draws |
| `0x477548` | RTL | — | game heap free (pool test `0x476e2c` on `0x6bb2f8`, else CW3220 free `0x4967a8`; byte count `0x6bb30c`); memory layer, H6 |
| `0x47758c` | RTL | — | game heap free (second entry, same pool test and counter); memory layer, H6 |
| `0x476120` | H3 | — | DTS part transform for drawing: forwards to `0x47613c` (H3); eight callers, all DTS part draw slots |
| `0x42a9c0` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#simulation-relevant-edges) | finish a display transition (display group; presentation frame) |
| `0x429e60` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#0x429e60d-m-layout-switch-u) | layout switch (display group; panel-off byte `0x431500`) |
| `0x42a3f4` | H3 | [display_modes.md](../reverse-engineering/display_modes.md#classification-table) | request a display transition (`D+0x18` by table `0x42a417`); display group, called through the bridge by `player_input` and `death_sequence` |
| `0x4543b0` | H3 | [hud.md](../reverse-engineering/hud.md#the-hud-key-handler-0x432bc8) | run a modal dialog object (F11 options, alert screens): screen activation, message loop; UI |
| `0x446c38` | H4 | hook `mfd_alt_key` (group `mfd_keys`; [cockpit.md](../reverse-engineering/cockpit.md#mfd-keys)) | the MFD's Alt order handler called by `hud_key` for 212h..22Eh (not a slot forward): entry select `0x43f9e0`, the order `mfd_order` `0x447248`, the selection `0x447158`; on the Alt+O residue path, needs the original stack coordinate ([cockpit.md](../reverse-engineering/cockpit.md#verdicts)) |
| `0x477904` | platform | [input.md](../reverse-engineering/input.md#callees-of-the-input-dispatcher) | joystick summary: fills the device state `0x6bb6a8` |
| `0x46ac14` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-affine-texture-span-loop-0x46ac14) | frameless affine texture span loop (opaque, or texel 0 transparent when byte `[0x4a0994]` ≠ 0): reads the span state `0x4a0968..0x4a099c` and the texel page `0x4d4f04`, writes only the destination row `[0x4a0968]`, `[0x4a096c]` bytes long, of the drawing surface `[[0x4a3652]]` and its own scratch `0x4a09a0`; three call sites, all in the H3 fill routines `0x468414`, `0x468760` and the span clipper `0x48b524` |
| `0x48b524` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-affine-texture-span-loop-0x46ac14) | span clipper of the affine path: cuts the span against the sorted {start, length} run list `[0x4a593c]` (count `[0x4a5938]`), adjusts the span state `0x4a0968`/`0x4a096c`/`0x4a0978`/`0x4a0980`, calls `0x46ac14` per run; callers `0x468414`, `0x468760` only; related |
| `0x438d9c` | H4 | hook `button_press` | press an MFD button (taken with the HUD key handler `0x432bc8`) |
| `0x4530bc` | H4 | hook `button_release` | clock helper of the HUD key handler `0x432bc8` |
| `0x45fe6c` | H4 | hook `playback_key` | playback key check of the command handler `0x45feb8` |
| `0x429620` | H3 | — | sorted cell-object draw (only caller the terrain cell-object callback `0x428c60`, H3): builds a binary sort tree of draw items in the node buffer `[0x49ac40]` (`0x1d`-byte nodes; `0x429524` for item kinds 1/2, `0x42958c` for the rest), then walks it with `0x4295f0`; writes only the node buffer |
| `0x429524` | H3 | — | sort-tree pass 1 of `0x429620`: one node per draw item of kind 1 or 2 (`[item+8]+0x16`), inserted with `0x4294d4`; returns the next free node |
| `0x42958c` | H3 | — | sort-tree pass 2 of `0x429620`: the items of the other kinds |
| `0x4295f0` | H3 | — | in-order walk of the sort tree; calls each item's virtual `+0` (`SortObj` draw `0x42876c`) |
| `0x4294d4` | H3 | — | recursive tree insert (node links `+0`/`+4`) ordered by `0x429460` |
| `0x429460` | H3 | — | depth order of two draw items: kinds 1/2 through `0x42937c`, both kind 0 by the dword `+0x12`, else `0x42900c`; result only |
| `0x42876c` | H3 | — | `SortObj` slot 0 (vtable `0x49ac38`): per-object light `0x407098` (H3), ground marks `0x42e8e8`, `0x4680f0`, the item's draw virtual; writes the draw globals `0x4cf93c`/`0x4cf97c`/`0x4cf980` |
| `0x42e8e8` | H3 | — | ground-mark sprites of a draw item (tables `0x4cfce0`/`0x4cfcf8`/`0x4cfcfc`, sprite draws `0x430518`/`0x430590`); locals only |
| `0x472f14` | platform | [file_io.md](../reverse-engineering/file_io.md#the-stream-library-and-path-helpers) | VOL-aware read stream: constructor and open (vtable `0x4a0e10`) |
| `0x4730e8` | platform | hook `stream_open` ([file_io.md](../reverse-engineering/file_io.md#the-stream-library-and-path-helpers), [vol.md](../formats/vol.md)) | stream open |
| `0x472f64` | platform | hook `stream_destroy` ([file_io.md](../reverse-engineering/file_io.md#the-stream-library-and-path-helpers)) | stream destructor |
| `0x47c8c4` | platform | — | stream buffer helper below the stream reads `0x47c7d8`/`0x47c82c` (counts equal to the stream opens 28/74/36/137/45/57); stream virtuals `+4`/`+8`, `0x47c984` |
| `0x47a144` | platform | hook `dts_load` ([dts.md](../formats/dts.md)) | load one persistent DTS object from a stream |
| `0x47a6b4` | platform | [dts.md](../formats/dts.md) | DTS block header read |
| `0x47a83c` | platform | [dts.md](../formats/dts.md) | drains the stream to the saved block end |
| `0x481cd4` | platform | hook `bitmap_new` | `BMP_8` read slot `+0x2c` |
| `0x482d88` | platform | hook `bitmap_array_new` | `BMP_8` read slot `+0x38`, the same pattern |
| `0x483374` | platform | — | persistent read slot `+0x18` of vtable `0x4a2f04`, the same pattern |
| `0x481d8c` | platform | hook `bitmap_read` | `GLBitmap` read slot `+0xc` |
| `0x482e7c` | platform | hook `bitmap_array_read` | `GLBitmapArray` read slot `+0xc` |
| `0x45cdf4` | platform | [display_modes.md](../reverse-engineering/display_modes.md) | load a DTS/DBA file by name: stream `0x472f14`, `0x47a144`, destructor `0x472f64` (e.g. `intro.dba`) |
| `0x430fb0` | platform | — | the same load pattern (stream, `0x47a144`, destructor) for the dialog code `0x45464c` |
| `0x453878` | platform | [file_io.md](../reverse-engineering/file_io.md) | STR reader (`error.str`, `gnl_alrt.str`) |
| `0x47b198` | platform | — | file helper of `0x47b064` (platform): stream virtuals `+4`/`+8`, `0x47b158`, `0x47b35d` |
| `0x474360` | platform | [file_io.md](../reverse-engineering/file_io.md) | instrument bank loader (`MELODIC.BNK`, `DRUM.BNK`) through the VOL-aware stream |
| `0x473828` | platform | [audio.md](../reverse-engineering/audio.md#the-sound-api-functions) | sample load/unload (`_lopen` path `0x47465c`) |
| `0x463d4c` | platform | [audio.md](../reverse-engineering/audio.md#the-sound-api-functions) | SFX cache load (VOL stream or `_lopen`, `GlobalAlloc`, eviction `0x464390`) |
| `0x463c50` | platform | [audio.md](../reverse-engineering/audio.md#the-sound-api-functions) | SFX close |
| `0x4641e8` | platform | [audio.md](../reverse-engineering/audio.md#the-sound-api-functions) | SFX is-done (`sosDIGISampleDone`) |
| `0x463374` | platform | [audio.md](../reverse-engineering/audio.md) | lip file read through the VOL-aware stream |
| `0x462b9c` | platform | [audio.md](../reverse-engineering/audio.md#the-sound-api-functions) | voice start on an LRU channel; its clock calls (`0x467828`) are simulation-visible like those of the `sfx` hooks, so a V follow-up rather than H4 |
| `0x462940` | platform | [audio.md](../reverse-engineering/audio.md) | voice start wrapper (`0x462b9c`, `0x463578`, `0x463400`) |
| `0x407e48` | H4 | hook `explosion_dtor` | `EXPLOSION` destructor |
| `0x46b574` | H4 | hook `fire_dtor` | `FIRE` destructor |
| `0x40f7e6` | H4 | hook `debris_dtor` | `DEBRIS` destructor |
| `0x41bd46` | H4 | hook `flat_dtor` | `FlatObj` destructor |
| `0x424034` | H4 | hook `meteor_dtor` | `METEOR` destructor |
| `0x40b64d` | H4 | hook `bullet_dtor` | `BULLET` destructor |
| `0x40ab8d` | H4 | hook `rocket_dtor` | `ROCKET` destructor |
| `0x40ba94` | H4 | hook `beam_dtor` | `BEAM` destructor |
| `0x490bdc` | H4 | hook `shape_dtor` | shape-instance destructor |
| `0x40765c` | H3 | — | `LightSource` destructor (vtable `0x497b8a`): unlinks the light from the light table `[0x4a9608]` (`0x406fbc`; presentation state, see `0x407098`), heap free |
| `0x4034f4` | H3 | — | draw slot `+0` of `LC_BASE`, `VEHICLE`, `GUN_BASE`, `BASE`, `RADAR_BASE`: detail word `0x4769b8`, ACTOR draw `0x4033e4` (H3) |
| `0x408e6c` | H3 | — | `DEBRIS` draw slot `+0`: idempotent lazy pose matrix `0x401fe4`, part draw virtual `+0x1c` (as `METEOR` `0x409cd0`) |
| `0x40ded8` | H3 | — | part draw slot `+0x1c` of eleven weapon and pod classes (`ENERGY_POD` … `ELF_WPN`): `0x4769b8` around `0x40316c` |
| `0x40316c` | H3 | — | `CONFIG_PART` draw slot `+0x1c`: part transform `0x476120` (H3), part list `0x476808`, `0x476224`; saves and restores `0x6b7b6c`/`0x6c6064` |
| `0x4768f0` | H3 | — | `TSCellAnimPart` draw slot `+0x1c` |
| `0x476808` | H3 | — | `TSPartList` draw slot `+0x1c` |
| `0x4759d4` | H3 | — | `TSGroup`/`hzline` draw slot `+0x1c`: render globals `0x6c68e8..0x6c68f6` |
| `0x476c18` | H3 | — | `TSBSPPart` draw slot `+0x1c`: render globals `0x6bb2e8..0x6bb2f0` |
| `0x474ec0` | H3 | — | `TSSolidPoly` draw slot `+0x1c` |
| `0x475538` | H3 | — | `TSShadedPoly` draw slot `+0x1c` |
| `0x4756d4` | H3 | — | `TSGouraudPoly` draw slot `+0x1c` |
| `0x474fa8` | H3 | — | `TexPoly`/`TSTexture4Poly` draw slot `+0x1c` |
| `0x478da4` | H3 | — | shape-instance draw slot `+0x10` (vtable `0x4a14d0`): view compose `0x478d60` (`matrix_compose`, `0x48c444`), shape draw `0x490c1c`, view restore `0x478d90` |
| `0x490c1c` | H3 | — | shape draw: detail selection `0x476248` (H3), part list `0x476808`, render-state save/restore `0x490d4d`/`0x490d58` |
| `0x48c72c` | H3 | — | polygon vertex transform against the view transform `0x6c5fb4..` (graphics library) |
| `0x48bfe8` | H3 | — | polygon lighting from the active light list `0x6c60a8`/`0x6c60ac` |
| `0x48d624` | H3 | — | polygon render state `0x6c6054`/`0x6c6058` |
| `0x46817c` | H3 | — | polygon to the rasterizer `0x468414` (H3) through the view helpers |
| `0x48d7ec` | H3 | — | view matrix `0x6c5fe8..0x6c6024` of a DTS part (only caller `0x47613c`, H3) |
| `0x48bcfc` | H3 | — | part of the view-transform build (only caller `0x48c444`, H3) |
| `0x48c2e4` | H3 | — | camera setup of the render context: focal/clip globals `0x6c6028..0x6c6038` (readers: draw code only), frame counter `0x4a5b0c` (read only here), view matrix `0x48c3b0` |
| `0x48c3b0` | H3 | — | inverse camera matrix (`0x48db12`, H3) into the render context |
| `0x48bc40` | H3 | — | light object constructor (vtable `0x4a5b39`); callers are the light pool of `0x407098` and the render setup |
| `0x407388` | H3 | — | light object from the free list `+0x35c`/`+0x360` or the heap, then `0x48be50` adds it to the active light list `0x6c60ac` (max. 10, count `0x6c60a8`; readers: polygon lighting only); caller `0x407098` (H3) |
| `0x407444` | H3 | — | the same for the second light kind |
| `0x47d734` | H3 | — | pure vector helper (`0x492a5e`, normalise `0x492a7d`) with render callers only (`0x407098`, `0x428f38`) |
| `0x48817c` | H3 | — | graphics library: rectangle fill |
| `0x488b98` | H3 | — | graphics library: bitmap blit body of `0x481c58` |
| `0x48185c` | H3 | — | graphics library: bitmap draw (GL context `0x6c5f74`/`0x6c5f78`) |
| `0x480c34` | H3 | — | graphics library: graphics-context constructor (copies the current context, vtable `0x4a2e10`) |
| `0x482534` | H3 | — | graphics library: glyph draw (`GLFont`) |
| `0x482634` | H3 | [hud.md](../reverse-engineering/hud.md#legend) | graphics library: text draw |
| `0x4815b8` | H3 | — | `SOrthoCamera` slot `+4` (graphics library) |
| `0x4835c8` | H3 | — | `GLCursor` destructor (slot `+0x18`) |
| `0x48349c` | H3 | — | `GLBitmapArray` destructor |
| `0x481fe0` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | select a palette (palette flush `0x43048c`) |
| `0x495132` | H4 | hook `matrix_ctor` (group `bridged_leaves`) | pure default constructor of the matrix type (identity rotation `0x495194`, vector `0x4953c4`); also the element constructor that simulation code passes to the array constructor `0x49667c` (`0x402be8`, `0x40d81f`), so the runtime's array constructor `0x49667c` (a service of the own process) calls the original through the guest |
| `0x4952fc` | H3 | [terrain.md](../reverse-engineering/terrain.md#keep-list-for-an-own-h3-terrain-renderer) | terrain height-cache reset (keep list item 2) |
| `0x4949a4` | H3 | [terrain.md](../reverse-engineering/terrain.md) | constructor of a terrain-draw work record (vector, six-entry array through `0x49667c`) on the stack of `0x46cb9c` |
| `0x4949e4` | H3 | [terrain.md](../reverse-engineering/terrain.md) | terrain draw geometry under `0x46cb9c` (`0x47e450`, `0x47e73c`, vector helpers); no absolute write |
| `0x494e58` | H3 | [terrain.md](../reverse-engineering/terrain.md) | terrain draw geometry under `0x46cb9c`; no absolute write |
| `0x47e450` | H3 | — | recursive geometry helper of `0x4949e4` (terrain draw); no absolute write |
| `0x47e73c` | H3 | — | geometry helper of `0x494e58`/`0x4949e4` (terrain draw); no absolute write |
| `0x46cb9c` | H3 | [terrain.md](../reverse-engineering/terrain.md) | terrain draw (render root step 2): eye position `0x6b4f44..`, view rectangle, height cache reset |
| `0x42841c` | H3 | [terrain.md](../reverse-engineering/terrain.md) | bins the objects into terrain cells (cell marks, keep list item 1); draw globals `0x4cf874`/`0x4cf92c` |
| `0x42837c` | H3 | [terrain.md](../reverse-engineering/terrain.md) | binning step (`0x46e62c`) |
| `0x4283b4` | H3 | [terrain.md](../reverse-engineering/terrain.md) | binning step (`0x4282f8`, `0x46e8c4`) |
| `0x46e62c` | H3 | [terrain.md](../reverse-engineering/terrain.md) | binning into cells with the back-face test `0x46be9c` |
| `0x42e700` | H3 | [terrain.md](../reverse-engineering/terrain.md), [horizon.md](../reverse-engineering/horizon.md) | render root step 4: sky `0x470a1c`, horizon `0x42ebe8`, terrain cells `0x428140` |
| `0x428140` | H3 | [terrain.md](../reverse-engineering/terrain.md) | terrain cell draw entry (`0x48c444`, `0x46d1a8`, both H3) |
| `0x42883c` | H3 | [terrain.md](../reverse-engineering/terrain.md) | last render root step: eye cell objects `0x46e5a4`, debug draw hooks `[0x4a57bc]`/`[0x4a57c0]`; draw globals only |
| `0x46e5a4` | H3 | [terrain.md](../reverse-engineering/terrain.md) | objects of one cell through `[G+0x125]` (`0x428c60`), then clears the cell mark (keep list item 1) |
| `0x42ec08` | H3 | [horizon.md](../reverse-engineering/horizon.md) | horizon painter dispatch (`0x42ec68`, `0x42ee88`, `0x42f0b0`, all H3) |
| `0x470a1c` | H3 | [horizon.md](../reverse-engineering/horizon.md) | sky geometry: `sincos_pair`, `transform_point`, view helpers; globals `0x6b5068`/`0x6b506c` (read only by `0x470b2c`..`0x470e26`, the same draw) |
| `0x45fba4` | H3 | [display_modes.md](../reverse-engineering/display_modes.md) | presentation frame: display transitions `0x42a4c4`/`0x42a9c0` (H3), palette flush, present |
| `0x45b758` | H3 | [display_modes.md](../reverse-engineering/display_modes.md) | display setup once from `0x461608`: surface properties `0x480738`/`0x480764`/`0x481350` |
| `0x45edb0` | H4 | hook `view_offsets` ([camera.md](../reverse-engineering/camera.md#view-mode-and-view-subject-logic-frame)) | view object `[0x4d24ea]`: subject virtual `+0x30`, then the camera setters `0x40111c`/`0x40109c` |
| `0x43f2d8` | H3 | — | `MFDRadar` map draw: blips `0x43ec08` (H3), rectangle fills `0x48817c`, line/marker draws `0x483a04`/`0x4895ec`, canvas clip `0x45c8bc`; writes only its own `+0x2c` and the GL context |
| `0x43eef4` | H3 | — | `MFDRadar` slot 0 (vtable `0x49c328`) draw |
| `0x45030c` | H3 | — | HDD map icon draw (view transform `0x48c444`, `0x44c250`, bitmap `0x48185c`); callers the map draws `0x44e334`, `0x44060c` |
| `0x44c250` | H3 | — | view helpers `0x48c57c`/`0x48c5fc`/`0x48c6d0` for the map icon |
| `0x44e334` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD map draw |
| `0x44f1bc` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | `HDDMapGadget` slot 0 (vtable `0x49d9b0`) draw |
| `0x44c8bc` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | `HDDMap` slot 0 draw (`0x44de14`, `0x44e334`) |
| `0x44de14` | H3 | — | HDD map labels (`0x438aac`, `0x44dd74`) |
| `0x44dc6c` | H3 | — | HDD map mode label: text `M+0x140`, label timer `M+0x13e`, `0x438920`/`0x43122c` (H3) |
| `0x438aac` | H3 | [hud.md](../reverse-engineering/hud.md) | draw a label with a highlighted character; text position `0x4d17b4` |
| `0x43b808` | H3 | [hud.md](../reverse-engineering/hud.md) | `HUDCrosshairGunsight` draw |
| `0x4342e0` | H3 | [hud.md](../reverse-engineering/hud.md) | weapon range class: pure, presentation callers only |
| `0x435a74` | H3 | [hud.md](../reverse-engineering/hud.md) | text variant record `0x4d0ce6..0x4d0cee` (U) |
| `0x435610` | H3 | [hud.md](../reverse-engineering/hud.md) | `MsgPort` frame (slot `+0x10`): message queue (Q, clock); message ports are cockpit presentation |
| `0x435d0c` | H3 | [hud.md](../reverse-engineering/hud.md) | `DialogPort` text (slot 2) |
| `0x435d9c` | H3 | [hud.md](../reverse-engineering/hud.md) | `DialogPort` show (slot 1): voice `0x462940`, text box; draws |
| `0x436130` | H3 | [hud.md](../reverse-engineering/hud.md) | port erase (`CommandPort`/`DialogPort` slot 3) |
| `0x436fd0` | H3 | [hud.md](../reverse-engineering/hud.md) | `EventPort` erase (slot 3) |
| `0x436abc` | H3 | [hud.md](../reverse-engineering/hud.md) | `EventPort` show (slot 1): message state `0x4d1290`/`0x4d1294`, clock, sound rows `0x19`/`0x1a` (stream B), voice |
| `0x44718c` | H3 | — | MFD page auto-switch for the selected weapon (`0x446e60`); state `0x49cbc4..0x49cbc8` read only by MFD code (`0x4465b4`, `0x446eb6`) |
| `0x44b620` | H3 | [audio.md](../reverse-engineering/audio.md) | cockpit radio panel per frame (from `0x449bf8`, H3): lip timeline `0x4632c4`, state machine with stream-B draws in `0x44b148`/`0x44b344` (H3); latch `0x49d4f4` |
| `0x44b514` | H3 | [audio.md](../reverse-engineering/audio.md) | cockpit radio panel (voice start through `0x44aff0`) |
| `0x44aff0` | H3 | [audio.md](../reverse-engineering/audio.md) | radio panel voice start (`0x462b9c`), channel pointer into the panel object `+0x12f` |
| `0x432924` | H3 | [hud.md](../reverse-engineering/hud.md) | cockpit startup sequence (U+C+Q): element power-on, EventPort message |
| `0x433770` | H3 | [hud.md](../reverse-engineering/hud.md) | creates the panel scroll zones (heap, U) |
| `0x45275c` | H3 | [display_modes.md](../reverse-engineering/display_modes.md) | move the panel object (U) |
| `0x441fa4` | H3 | — | `WeaponRangeGauge` lamp toggle: gauge byte `+0xb3`, lamp virtuals, EventPort message `0x26`/`0x27` |
| `0x455950` | H3 | [main_loop.md](../reverse-engineering/main_loop.md) | modal alert dialog (`gnl_alrt.str`; main loop: UI, never H4) |
| `0x4541b8` | H3 | — | alert dialog setup (graphics context, bitmaps, heap) |
| `0x4548c8` | H3 | [hud.md](../reverse-engineering/hud.md) | dialog close: UI globals, resume of the message clocks `0x435b80` |
| `0x45464c` | H3 | [hud.md](../reverse-engineering/hud.md) | dialog open: UI globals, pause of the message clocks `0x435b58` |
| `0x454acc` | H3 | [audio.md](../reverse-engineering/audio.md) | dialog message loop |
| `0x454e2c` | H3 | — | `Alert` slot `+0x10` (five alert classes) |
| `0x453848` | H3 | — | destructor of a dialog-owned buffer holder (callers `0x4543b0`, `0x45f160`) |
| `0x452c04` | H3 | — | frees a dialog object (caller `0x4548c8`) |
| `0x45e208` | H3 | [main_loop.md](../reverse-engineering/main_loop.md) | view/HUD countdown step of `0x45de30` (presentation side) |
| `0x45baa8` | platform | [input.md](../reverse-engineering/input.md) | device poll: fills the device block from the keyboard ring, joystick and mouse objects |
| `0x45bc74` | platform | [input.md](../reverse-engineering/input.md) | mouse/joystick reader of the device poll (device block `0x4d241f..`, `0x49ec64`) |
| `0x410b40` | H4 | hook `weapon_frame` | player weapon system per rendered frame |
| `0x410a3c` | H4 | hook `ready_step` | steps the selection while the selected weapon is not ready |
| `0x410970` | H4 | hook `chain_ready` | weapon ready test |
| `0x410f04` | H4 | hook `latch_set` | sets the fire latch `W+0x14` |
| `0x410f14` | H4 | hook `link_toggle` | link toggle of the selected weapon and its partner |
| `0x4420c8` | H4 | hook `gauge_sync` | `WeaponRangeGauge` sync from the weapon system |
| `0x442120` | H4 | hook `gauge_state` | copies the gauge state `+0xb1`/`+0xb3`/`+0xb4` to `0x4d1a5c..` and returns it |
| `0x442154` | H4 | hook `gauge_button` | `WeaponRangeGauge` slot 0 |
| `0x41b130` | H4 | hook `cockpit_sync` | cockpit↔player synchronisation in the render root |
| `0x4327ac` | H4 | hook `panel_frame` | cockpit panel frame |
| `0x45fca8` | H4 | hook `render_root` | render root |
| `0x45f570` | H4 | hook `logic_frame` ([main_loop.md](../reverse-engineering/main_loop.md)) | logic frame |
| `0x4483a0` | H4 | hook `slider_event` | `ThrottleVSliderGadget` event |
| `0x44c988` | H4 | hook `map_frame` ([cockpit.md](../reverse-engineering/cockpit.md#hdd-map-frame)) | `HDDMap` slot 1, the map frame: clock read, blink counter, 30-tick timer, click dispatch (`map_click`, wingman pick `0x44d82c`), the order-record check; the button, label and map draws stay guest calls; the frame address it leaves in the graphics context is kept by an entry shim |
| `0x44d450` | H4 | hook `map_areas` | HDD map click areas |
| `0x44d9f4` | H4 | hook `map_mode` | set the HDD map mode `M+0x5d` |
| `0x44da98` | H4 | hook `wingman_select` | select a wingman `M+0x105` |
| `0x44dc10` | H4 | hook `order_cancel` | cancel the selected wingman's pending order |
| `0x48be20` | H4 | hook `shape_base_dtor` | base destructor below the shape-instance destructor `0x490bdc` |
| `0x442054` | H4 | hook `gauge_link` | gauge link byte `+0xb4`, then the link toggle `0x410f14` of the player's weapon system |
| `0x43904c` | H4 | hook `slider_base` | widget event of the throttle slider |
| `0x447df8` | H4 | hook `throttle_record` | address of the throttle gauge record `+0xb1` |
| `0x447da8` | H4 | hook `throttle_gauge` | throttle gauge set |
| `0x43dbb0` | H4 | hook `hud_record` | pure getter `G+0xb1` of a gauge record |
| `0x401d94` | H4 | hook `stage_dispatch` ([main_loop.md](../reverse-engineering/main_loop.md)) | stage-callback dispatcher |
| `0x40707c` | H4 | hook `vector_copy` | pure 12-byte vector copy |
| `0x4450f8` | H3 | [hud.md](../reverse-engineering/hud.md) | gauge value set `+0xb1` with redraw flag `+0xb0` (gauge `[H+0x1e5]` of `0x41b130`; nothing reads it back) |
| `0x434a24` | H3 | [hud.md](../reverse-engineering/hud.md) | HUD target and aim point `H+0x26c..+0x27e` (read only by the HUD) |
| `0x434520` | H3 | [hud.md](../reverse-engineering/hud.md) | widget hover and draw (render root) |
| `0x434194` | H3 | [hud.md](../reverse-engineering/hud.md) | panel bump per frame (U+C) |
| `0x42d82c` | H3 | [hud.md](../reverse-engineering/hud.md) | panel bump set (U) |
| `0x4349ac` | H3 | [hud.md](../reverse-engineering/hud.md) | nav marker arrival message (U+Q) |
| `0x4530fc` | H3 | — | timed widget states of a panel object (`+0x356` list, `+0x396` count): clock, widget state `+0x1b`, redraw virtual `+4` |
| `0x433e3c` | H3 | [hud.md](../reverse-engineering/hud.md) | MFD cursor image |
| `0x45d490` | H3 | — | detail levels from a setting: `0x497368`, `0x49736c`, `0x4a9868` (readers: the draw slots `0x4033e4`, `0x40ded8`, `0x40f726`) |
| `0x45d50c` | H3 | — | detail levels `0x4a95b4`, `0x4a9dc4` (readers: the draw slots `0x4034f4`, `0x4215cc`) |
| `0x410a04` | H4 | hook `selected_ready` | ready test of the selected weapon |
| `0x441f90` | H4 | hook `gauge_mode` | gauge mode word `+0xb1` store |
| `0x410cd0` | H4 | hook `member_toggle` ([weapons.md](../reverse-engineering/weapons.md)) | toggles one fire-group word of the weapon system |
| `0x44ede0` | H3 | — | HDD map button lamp: toggles button byte `+0x36`, redraw virtual `+4` |
| `0x44ee00` | H3 | — | HDD map: enables the eight button bytes `M+0x131..` and `M+0x139` |
| `0x44ee24` | H3 | — | HDD map: disables the eight button bytes `M+0x131..`, sets `M+0x139` |
| `0x413af8` | H4 | hook `shield_step` ([cockpit.md](../reverse-engineering/cockpit.md)) | shield balance step |
| `0x4224c4` | H4 | hook `flyer_route` ([flyer.md](../reverse-engineering/flyer.md)) | FLYER leader route step |
| `0x422108` | H4 | hook `flyer_altitude` ([flyer.md](../reverse-engineering/flyer.md)) | FLYER pitch helper of the AI states |
| `0x422260` | H4 | hook `flyer_throttle` ([flyer.md](../reverse-engineering/flyer.md)) | FLYER AI helper fed with `0x4222fc`'s EDX high word |
| `0x427c68` | H4 | hook `collision_walk` ([mission_runtime.md](../reverse-engineering/mission_runtime.md)) | collision walk of `obstacle_test` |
| `0x46a5bc` | H4 | hook `convoy_leader` ([convoy.md](../reverse-engineering/convoy.md)) | convoy leader search |
| `0x49681a` | RTL | — | CW3220 `_memset` import thunk (`jmp [0x6cc358]`); bridged by 11 hooks |
| `0x49680e` | RTL | — | CW3220 `_memcpy` import thunk (`jmp [0x6cc350]`); bridged by `shape_instance` |
| `0x496796` | RTL | — | CW3220 `_sprintf` import thunk; bridged by `pool_next` (error path, 0 calls) |
| `0x496802` | RTL | — | CW3220 `_fopen` import thunk; bridged by `control_record` (tape path, 0 calls) |
| `0x496832` | RTL | — | CW3220 `_fwrite` import thunk; `control_record` tape path |
| `0x496838` | RTL | — | CW3220 `_fclose` import thunk; `control_record` tape path |
| `0x49684a` | RTL | — | CW3220 `_fread` import thunk; `control_record` tape path |
| `0x49695e` | platform | [input.md](../reverse-engineering/input.md) | USER32 `GetKeyState` import thunk; `key_message` calls it through the bridge so the input journal records it (bridged call) |
| `0x45c524` | platform | [input.md](../reverse-engineering/input.md) | device slot getter `[0x4d2468+4n] − 0x1f` (input.md: platform); bridged by `control_record`, `player_input` |
| `0x45b734` | platform | [input.md](../reverse-engineering/input.md) | button latch: bytes `0x4d22f5+n` (input.md: keep original, platform state) |
| `0x486ece` | platform | [input.md](../reverse-engineering/input.md) | copies the software cursor position `0x4a3620` (tape path of `control_record`) |
| `0x477a7c` | platform | [input.md](../reverse-engineering/input.md) | empty hot-key callback (`xor eax,eax`), no state; called by `key_message` through the callback table |
| `0x492b90` | platform | [file_io.md](../reverse-engineering/file_io.md) | file-name helper `(n, ext)`; tape/results names |
| `0x479c78` | platform | [rtl_imports.md](rtl_imports.md) | fatal error report and quick exit (`__exit`); error paths of `control_record`/`results_writer` only |
| `0x41a6d0` | platform | hook `twist_sound_latch` (group `sim_leaves`; [audio.md](../reverse-engineering/audio.md)) | torso-twist servo sound latch: `0x49a1c9`/`0x49a1ca` from the driver's sample-done state (`0x462af0`); bridged by `torso_twist` |
| `0x41a994` | platform | hook `pitch_sound_latch` (group `sim_leaves`; [audio.md](../reverse-engineering/audio.md)) | torso-pitch servo sound latch: `0x49a1cc`/`0x49a1ce`, as `0x41a6d0`; bridged by `torso_pitch` |
| `0x46466c` | platform | [audio.md](../reverse-engineering/audio.md) | SFX manager: rate; bridged by `sfx_rate` |
| `0x464118` | platform | [audio.md](../reverse-engineering/audio.md) | SFX manager: stop; bridged by `sfx_stop` |
| `0x467828` | platform | virtual clock ([audio.md](../reverse-engineering/audio.md)) | `(GetTickCount − base) >> 4` wrapper; the virtual clock covers it; bridged by `sfx_*`, `ready_tone` |
| `0x4930c1` | H3 | [terrain.md](../reverse-engineering/terrain.md) (terrain renderer keep list) | hand-written scan converter (`enter`), writes the span buffer; bridged by `poly_spans` |
| `0x48c57c` | H3 | — | view-space transform of a point (`0x48d9e4`); `sfx_place` uses it for the pan only |
| `0x443880` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | shield gauge record setter (class U: writes the gauge object `G+0xb0..+0xd0` only); bridged by `shield_balance` |
| `0x434144` | H3 | [display_modes.md](../reverse-engineering/display_modes.md) | ground-feedback screen jolt: display-object mode test, `0x42d854`, private timers `0x49b634`/`0x49b638` (read only by `0x4341xx`), `0x42d820`; like `0x434010` |
| `0x406f38` | H3 | [main_loop.md](../reverse-engineering/main_loop.md#explosion-and-fire-pool-updates) | light module: claims an entry of the light table `[0x4a9608]` (read only by the renderer and the light module); its index goes to word `L+0xc` of the explosion, read only by the light code |
| `0x407048` | H3 | [main_loop.md](../reverse-engineering/main_loop.md#explosion-and-fire-pool-updates) | light module: entry setter (same table) |
| `0x420a68` | H3 | message-port rule | cockpit message post `(id)` through the message port's slot 0 |
| `0x420a98` | H3 | message-port rule | radio message post `(actor, id, flag)` |
| `0x433158` | H3 | message-port rule | message post of `trigger_fire` |
| `0x435ac8` | H3 | [hud.md](../reverse-engineering/hud.md) | HUD message queue cancel (D-19 note) |
| `0x4523b0` | H3 | `mouse_dispatch` ([input.md](../reverse-engineering/input.md)) | widget library: screen-rectangle hit test |
| `0x452ab8` | H3 | `mouse_dispatch` ([input.md](../reverse-engineering/input.md)) | widget library: redraw request (screen `+0x227` ← 1, gadget virtual `+4`) |
| `0x486e7c` | H3 | `mouse_dispatch` ([input.md](../reverse-engineering/input.md)) | cursor follow through the graphics-library slots `[0x4a5824]`/`[0x4a5828]` |
| `0x45de30` | H4 | [main_loop.md](../reverse-engineering/main_loop.md), hook `view_apply` ([camera.md](../reverse-engineering/camera.md#view-mode-and-view-subject-logic-frame)) | view request apply (`0x45df34`, `0x45e208`), logic frame step 15; bridged by `death_sequence`; `0x45e208` stays a guest call (H3, the subject label) |
| `0x429820` | H3 | [display_modes.md](../reverse-engineering/display_modes.md) | display-object getter `[0x4cf99c]` |
| `0x446e3c` | H3 | [input.md](../reverse-engineering/input.md) | HUD panel mode cycle |
| `0x4610ad` | H3 | [weapons.md](../reverse-engineering/weapons.md) | getter of the HUD sub-object `[[0x49b088]+0x1e1]` |
| `0x4669b4` | H3 | [display_modes.md](../reverse-engineering/display_modes.md) | focus/display-mode switch on Alt+Tab, Ctrl+Esc, Alt+Esc (never in the suites) |
| `0x422098` | H4 | hook `flyer_pitch` ([flyer.md](../reverse-engineering/flyer.md)) | FLYER AI pitch helper |
| `0x47df04` | H4 | computed in core | pure 2×2 word swap (`enter`); computed in core by `convoy_follow` and `collision_walk` since H4-105 (counter only) |
| `0x409904` | H4 | hook `flush_attached` (group `stage_flush`; [main_loop.md](../reverse-engineering/main_loop.md#the-stage-5-flushes-and-the-manager-slot-methods)) | stage-5 flush callback (removal list of a pool through `removal_flush`); also bridged by `vehicle_part` |
| `0x412dc4` | H4 | hook `subject_status` | group status of the current order |
| `0x412bd0` | H4 | hook `subject_group` | current-order field of a group |
| `0x412bbc` | H4 | hook `subject_actor` | current-order field of a group, byte-identical twin of `0x412bd0` |
| `0x412be4` | H4 | hook `group_down` | every actor of the group down |
| `0x41324c` | H4 | hook `route_flag` | route completion byte of a group |
| `0x413920` | H4 | hook `state_reached` | objective predicate 1 |
| `0x413950` | H4 | hook `group_safe` | objective predicate 2 |
| `0x412d90` | H4 | hook `group_marked` | objective predicates 6/8 |
| `0x412c58` | H4 | hook `group_flagged` | objective predicate 7 |
| `0x413780` | H4 | hook `bounds_margin` | margin test around `bounds_outside` |
| `0x40ffb0` | H4 | hook `proj_record` | `PROJ` definition by index |
| `0x49295c` | H4 | hook `pitch_point` | pitch toward a point |
| `0x40501c` | H4 | hook `damage_words` | default damage words `0x4a95f2`/`0x4a95f4` |
| `0x40b5a0` | H4 | hook `bullet_charge` | low-charge emitter wrapper around the bullet constructor |
| `0x40a948` | H4 | hook `rocket_init` | rocket projectile constructor |
| `0x427d98` | H4 | computed in core by `sweep`, counter only | hit-accumulator clear of `sweep`: dword `0x4aaad0` ← 0 ([weapons.md](../reverse-engineering/weapons.md)) |
| `0x427da0` | H4 | computed in core by `sweep`, counter only | hit-accumulator read of `sweep`: EAX ← dword `0x4aaad0` ([weapons.md](../reverse-engineering/weapons.md)) |
| `0x40d9f8` | H4 | hook `part_group_dead` | part-group destroyed test over `0x40cf10` |
| `0x40cf10` | H4 | hook `part_list_dead` | all-parts-destroyed test |
| `0x40dc58` | H4 | hook `part_group_max` | maximum of a part |
| `0x4240f4` | H4 | hook `mission_guard` | mission guard predicate over the words `0x4a9e52`/`0x4a9e5a`/`0x4a9e5c` |
| `0x41194a` | H4 | hook `group_fire_sel` | weapon-group slot-0 body |
| `0x411a1d` | H4 | hook `group_fire_any` | weapon-group slot-0 body |
| `0x411a74` | H4 | hook `actor_offsets` | ACTOR/FLYER slot `+0x30` |
| `0x40e4dc` | H4 | hook `targ_aim` (group `cold_paths`; in-vitro evidence only, 0 live in all suites) | weapon timer step of `aim_point` |
| `0x40e4ac` | H4 | hook `targ_next` (group `cold_paths`; in-vitro evidence only, 0 live in all suites) | next-living-index walk through slot `+0x80` |
| `0x40a488` | H4 | hook `missile_steer` (group `cold_paths`; in-vitro evidence only, 0 live in all suites) | rocket guidance step |
| `0x423974` | H4 | hook `ally_nearest` (group `cold_paths`; in-vitro evidence only, 0 live in all suites) | ally selection, read-only query over `dist_gate` |
| `0x40f09c` | H4 | hook `turbo_engage` (group `cold_paths`; in-vitro evidence only, 0 live in all suites) | weapon overheat latch `+0x81` |
| `0x41b804` | H4 | hook `member_assess` (group `cold_paths`; in-vitro evidence only, 0 live in all suites) | AI evaluation of a group member |
| `0x45a498` | H4 | hook `command_queue` | command-key queue callback |
| `0x47f058` | H4 | hook `rotation_compose` ([flyer.md](../reverse-engineering/flyer.md)) | rotation-only matrix compose |
| `0x47f4f4` | H4 | hook `rotation_place` ([weapons.md](../reverse-engineering/weapons.md)) | matrix compose variant |
| `0x41b468` | H4 | hook `r_key_toggle` (group `bridged_leaves`; [hud.md](../reverse-engineering/hud.md#the-hud-key-handler-0x432bc8)) | R key: toggles the player byte `P+0x96` when the player flag `P+0xa3` is set, sound rows `0x1a`/`0x1b`, cancels and sends EventPort messages `0x2c`/`0x2d`; still a guest call of `hud_key` |
| `0x434974` | H4 | hook `nav_marker_set` (group `bridged_leaves`; [hud.md](../reverse-engineering/hud.md#the-hud-key-handler-0x432bc8)) | Alt+D: sets the nav marker to the player position (active, not armed); its readers are not classified yet, so it stays H4 until a package shows it is display-only; still a guest call of `hud_key` |
| `0x457538` | H3 | [hud.md](../reverse-engineering/hud.md#the-hud-key-handler-0x432bc8) | F11 modal dialog constructor (after `0x47749c(0x3b8)`; virtual `+8`, `0x4543b0(obj, 3)`): UI only; a guest call of `hud_key` |
| `0x429828` | H3 | [input.md](../reverse-engineering/input.md#command-handler-as-own-code) | called by `command_key` with result unused before the gadget step `0x4610ad`/`0x441fa4`: a HUD/display helper; guest call |
| `0x401148` | dead | [input.md](../reverse-engineering/input.md#command-handler-as-own-code) | `command_key` calls it only when camera word `+36h` = 1, unreachable under its mask; guest call (coordinator, 2026-10-11) |
| `0x411e40` | H4 | hook `list_next` (group `bridged_leaves`; [input.md](../reverse-engineering/input.md#command-handler-as-own-code)) | list step over the actor list `0x4a9af8` used by `command_key`'s view-subject cycling (reads `+2Eh`, the group word `+45h`); still a guest call, small; candidate for a package with `0x411e60` |
| `0x411e60` | H4 | hook `list_prev` (group `bridged_leaves`; [input.md](../reverse-engineering/input.md#command-handler-as-own-code)) | the other list step of the view-subject cycling (with `0x411e40`); still a guest call (coordinator, 2026-10-11) |
| `0x4561dc` | H3 | [input.md](../reverse-engineering/input.md#command-handler-as-own-code) | modal dialog (`obj, flags`): UI; its result ends the main loop through word `0x4d24fe` in one case; guest call (coordinator, 2026-10-11) |
| `0x45cff0` | H3 | [input.md](../reverse-engineering/input.md#command-handler-as-own-code) | dialog `0x4566e0` (338 instructions), never entered in the suites, role open; UI until shown otherwise; guest call |
| `0x45f070` | platform | [input.md](../reverse-engineering/input.md#command-handler-as-own-code) | builds the WinHelp file name for `0x4669c4` (UI/platform); guest call (coordinator, 2026-10-11) |
| `0x45fe38` | H4 | hook `view_pref_toggle` (group `bridged_leaves`; [input.md](../reverse-engineering/input.md#command-handler-as-own-code)) | view-preference toggle: word `0x4d2518`, view countdown `0x4d2520` ← 2 when the camera subject is the player; still a guest call, small |
| `0x4667c8` | H3 | [presentation.md](../reverse-engineering/presentation.md) | window/full-screen switch (Alt+Enter); keeps its call site (D-15 keep-list); guest call of `command_key` (coordinator, 2026-10-11) |
| `0x4669c4` | platform | [input.md](../reverse-engineering/input.md#command-handler-as-own-code) | WinHelpA wrapper; guest call of `command_key` (coordinator, 2026-10-11) |
| `0x467ad8` | platform | [input.md](../reverse-engineering/input.md#command-handler-as-own-code) | `int3; ret` debug break (Ctrl+B with the debug flag); guest call of `command_key` (coordinator, 2026-10-11) |
| `0x47c82c` | platform | [file_io.md](../reverse-engineering/file_io.md#the-vol-aware-read-stream-as-own-code) | VOL archive's buffered stream read (58 bytes): refills the archive's own buffer; a guest call of `stream_read` and `member_lookup`, whose replacements leave the buffer at the original's logical position for it |
| `0x47c7d8` | platform | [file_io.md](../reverse-engineering/file_io.md#the-vol-aware-read-stream-as-own-code) | VOL archive's buffered stream seek (29 bytes); a guest call of `member_lookup` and `stream_seek` (coordinator, 2026-10-11) |
| `0x47b5cc` | platform | [file_io.md](../reverse-engineering/file_io.md#the-vol-aware-read-stream-as-own-code) | member decoder teardown on `stream_close` for decoder kinds 7/9; never entered in the 15 suites (coordinator, 2026-10-11) |
| `0x47b9cc` | platform | [file_io.md](../reverse-engineering/file_io.md#the-vol-aware-read-stream-as-own-code) | second decoder teardown on `stream_close` (kinds 7/9); never entered in the 15 suites (coordinator, 2026-10-11) |
| `0x4967c6` | RTL | [rtl_imports.md](rtl_imports.md) | `cw3220.dll!_open` import thunk, a guest call of `stream_open` for loose files (coordinator, 2026-10-11) |
| `0x496808` | RTL | [rtl_imports.md](rtl_imports.md) | `cw3220.dll!_read` import thunk, a guest call of `stream_read` for loose files (coordinator, 2026-10-11) |
| `0x496826` | RTL | [rtl_imports.md](rtl_imports.md) | `cw3220.dll!_lseek` import thunk, a guest call of `stream_seek`/`stream_tell` (coordinator, 2026-10-11) |
| `0x496856` | RTL | [rtl_imports.md](rtl_imports.md) | `cw3220.dll!_filelength` import thunk, a guest call of `stream_size` (coordinator, 2026-10-11) |
| `0x496874` | RTL | [rtl_imports.md](rtl_imports.md) | `cw3220.dll!_close` import thunk, a guest call of `stream_close` (coordinator, 2026-10-11) |
| `0x431210` | H3 | [hud.md](../reverse-engineering/hud.md#cockpit-panel-frame-as-own-code) | pop graphics context (the counterpart of `0x4311e0`); `panel_frame` calls it as a guest after the six sub-object draws; unprobed |
| `0x40e484` | H4 | hook `weapon_target_word` (group `bridged_leaves`; [hud.md](../reverse-engineering/hud.md#cockpitplayer-synchronisation-as-own-code)) | simulation leaf (stays a guest call of `cockpit_sync` for now): writes a weapon word from the target's type (details in the [Cockpit and player synchronisation](#cockpit-and-player-synchronisation)) |
| `0x405ea4` | H4 | hook `flush_buildings` (group `stage_flush`) ([main_loop.md](../reverse-engineering/main_loop.md#the-render-root-as-own-code)) | stage-5 callbacks of `stage_dispatch` (render root): each drains one pool's removal list through `removal_flush` `0x478644`; `0x46b188` also resets `0x6b4f3c`; see [main_loop.md](../reverse-engineering/main_loop.md#the-stage-5-flushes-and-the-manager-slot-methods) |
| `0x4077e8` | H4 | hook `flush_lights` (group `stage_flush`) ([main_loop.md](../reverse-engineering/main_loop.md#the-render-root-as-own-code)) | stage-5 callbacks of `stage_dispatch` (render root): each drains one pool's removal list through `removal_flush` `0x478644`; `0x46b188` also resets `0x6b4f3c`; see [main_loop.md](../reverse-engineering/main_loop.md#the-stage-5-flushes-and-the-manager-slot-methods) |
| `0x407b3c` | H4 | hook `flush_explosions` (group `stage_flush`) ([main_loop.md](../reverse-engineering/main_loop.md#the-render-root-as-own-code)) | stage-5 callbacks of `stage_dispatch` (render root): each drains one pool's removal list through `removal_flush` `0x478644`; `0x46b188` also resets `0x6b4f3c`; see [main_loop.md](../reverse-engineering/main_loop.md#the-stage-5-flushes-and-the-manager-slot-methods) |
| `0x4083e0` | H4 | hook `flush_debris` (group `stage_flush`) ([main_loop.md](../reverse-engineering/main_loop.md#the-render-root-as-own-code)) | stage-5 callbacks of `stage_dispatch` (render root): each drains one pool's removal list through `removal_flush` `0x478644`; `0x46b188` also resets `0x6b4f3c`; see [main_loop.md](../reverse-engineering/main_loop.md#the-stage-5-flushes-and-the-manager-slot-methods) |
| `0x408f9c` | H4 | hook `flush_smoke` (group `stage_flush`) ([main_loop.md](../reverse-engineering/main_loop.md#the-render-root-as-own-code)) | stage-5 callbacks of `stage_dispatch` (render root): each drains one pool's removal list through `removal_flush` `0x478644`; `0x46b188` also resets `0x6b4f3c`; see [main_loop.md](../reverse-engineering/main_loop.md#the-stage-5-flushes-and-the-manager-slot-methods) |
| `0x409a1c` | H4 | hook `flush_meteors` (group `stage_flush`) ([main_loop.md](../reverse-engineering/main_loop.md#the-render-root-as-own-code)) | stage-5 callbacks of `stage_dispatch` (render root): each drains one pool's removal list through `removal_flush` `0x478644`; `0x46b188` also resets `0x6b4f3c`; see [main_loop.md](../reverse-engineering/main_loop.md#the-stage-5-flushes-and-the-manager-slot-methods) |
| `0x40a108` | H4 | hook `flush_projectiles` (group `stage_flush`) ([main_loop.md](../reverse-engineering/main_loop.md#the-render-root-as-own-code)) | stage-5 callbacks of `stage_dispatch` (render root): each drains one pool's removal list through `removal_flush` `0x478644`; `0x46b188` also resets `0x6b4f3c`; see [main_loop.md](../reverse-engineering/main_loop.md#the-stage-5-flushes-and-the-manager-slot-methods) |
| `0x41544c` | H4 | hook `flush_mechs` (group `stage_flush`) ([main_loop.md](../reverse-engineering/main_loop.md#the-render-root-as-own-code)) | stage-5 callbacks of `stage_dispatch` (render root): each drains one pool's removal list through `removal_flush` `0x478644`; `0x46b188` also resets `0x6b4f3c`; see [main_loop.md](../reverse-engineering/main_loop.md#the-stage-5-flushes-and-the-manager-slot-methods) |
| `0x4215a8` | H4 | hook `flush_flyers` (group `stage_flush`) ([main_loop.md](../reverse-engineering/main_loop.md#the-render-root-as-own-code)) | stage-5 callbacks of `stage_dispatch` (render root): each drains one pool's removal list through `removal_flush` `0x478644`; `0x46b188` also resets `0x6b4f3c`; see [main_loop.md](../reverse-engineering/main_loop.md#the-stage-5-flushes-and-the-manager-slot-methods) |
| `0x46b188` | H4 | hook `flush_fires` (group `stage_flush`) ([main_loop.md](../reverse-engineering/main_loop.md#the-render-root-as-own-code)) | stage-5 callbacks of `stage_dispatch` (render root): each drains one pool's removal list through `removal_flush` `0x478644`; `0x46b188` also resets `0x6b4f3c`; see [main_loop.md](../reverse-engineering/main_loop.md#the-stage-5-flushes-and-the-manager-slot-methods) |
| `0x401e5c` | H4 | [main_loop.md](../reverse-engineering/main_loop.md#the-render-root-as-own-code) | the stage-6 callback of `stage_dispatch`; acts only in record mode (tape), guest call |
| `0x42d870` | H3 | — | jolt offset point and the 3D-view-visible test of the display layout; guest calls of `render_root` |
| `0x42db18` | H3 | — | jolt offset point and the 3D-view-visible test of the display layout; guest calls of `render_root` |
| `0x47b400` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | tell of the memory stream (slot `+4`): cursor minus start; 85,032 calls from `dts_load`; computable in core if `dts_load` ever drops the stream object |
| `0x47b4b4` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | read of the memory stream (slot `+0x18`): copies from the buffer (`_memcpy` thunk `0x49680e`), refills through `0x47b3a0` |
| `0x47a2c8` | platform | reproduced inside the hooks ([asset_loading.md](../reverse-engineering/asset_loading.md)) | arena allocator (size, arena): bump allocation inside the arena or `new[]` `0x4774f0` as fallback |
| `0x47ad40` | platform | [file_io.md](../reverse-engineering/file_io.md#contracts) | read stream constructor and open (`_open(path, 0x8000)`, vtable `0x4a1738`) |
| `0x47ad78` | platform | [file_io.md](../reverse-engineering/file_io.md#contracts) | read stream destructor (closes, frees on delete flag bit 0) |
| `0x47af98` | platform | [file_io.md](../reverse-engineering/file_io.md#contracts) | read stream read: CW3220 `_read` on the handle, status 2 on a short read |
| `0x496772` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_rewind` import thunk (tape header) |
| `0x4967de` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_ftell` import thunk (tape header) |
| `0x49682c` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_fseek` import thunk (tape header) |
| `0x496868` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_stricmp` import thunk (options and sound config readers) |
| `0x496880` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_atol` import thunk (options and sound config readers) |
| `0x496910` | platform | [file_io.md](../reverse-engineering/file_io.md#contracts) | KERNEL32 `GetPrivateProfileStringA` import thunk (stdcall, six arguments): option and sound config files |
| `0x406e44` | H3 | [mission_load.md](../reverse-engineering/mission_load.md) | light-table constructor of `light_pools` (20 entries of `0x23` bytes, two graphics-library lights `0x48bc40`); read only by polygon lighting |
| `0x408fb8` | H4 | hook `smoke_balls` (group `effect_pools`) | smoke-ball set-up of `smoke_pools`: pool `[0x4a966e]` (`pool_create`), shapes `0x474cd8`, bitmap `0x45cdf4`, `[0x4a967c]`; the pool and the records are simulation, the loads stay guest calls |
| `0x40cec0` | H4 | hook `collision_scan` (group `collision_data`) | part-list scan at mission load: walks the 10-byte entries of `[A+0x1f6]`, `0x40ce78` match then `0x40ccc8` update |
| `0x40cb84` | H4 | hook `col_part_collect` (group `damage_data`) | collects the part-list entries into three buffers through `0x40cafc` and returns their count (30/30/29 entries) |
| `0x40d128` | H4 | hook `dmg_capacity_set` (group `damage_data`) | scatters n words into the word array of the part table `[A+0x1fa]` by an index list |
| `0x40d240` | H4 | hook `dmg_region_set` (group `damage_data`) | writes n values into rows (18 bytes) of the part table `[A+0x1fa]` through `0x40d050` |
| `0x40d284` | H4 | hook `dmg_part_store` (group `damage_data`) | stores n entries (word `+0xc`, dword `+0xe`) into rows of the part table from the three buffers of `0x40cb84` |
| `0x40fb2c` | H4 | hook `equip_slots` (group `fit_leaves`) | files the equipment items of a weapon system into the five slots of a record (`A+0x307`) by weapon ID: ECM `+0`, TARG `+4`, SHIELD `+8`, ENERGY `+0xc`, TURBO `+0x10` (byte table `0x40fb7a`, jump table `0x40fb89`); `memset` first |
| `0x413ac8` | H4 | hook `shield_split` (group `fit_leaves`) | share split of the shield record `A+0x222`: word `+0` = `multiply_q10(+4, +6)` (balance × total), word `+2` = `+6` minus word `+0` |
| `0x417530` | H4 | hook `node_state_fill` (group `fit_leaves`) | recursive fill of a state word over a shape-node tree (class id from virtual `+0`; classes `0x14000b`/`0x140014` store word `+4`, `0x140015` fills the word array `[N+0x1c]`, `0x140007`/`8`/`b`/`c`/`15` recurse into the children `[N+0x12]`); the readers of the written words are not traced (the walk itself is simulation-neutral guest data, hooked as a plain tree walk) |
| `0x417d08` | H4 | hook `energy_rate` (group `fit_leaves`) | per-actor derived word `A+0x256`: 20, or `multiply_q10(200/600, 20)` by the flags `A+0xab`/`A+0xaa`, plus `multiply_q10(1024 - 204 (c / 51), 20)` for the ENERGY item `[A+0x313]` when its part condition `c` (`part_ratio`) is below 225 |
| `0x420634` | H4 | hook `mount_table` (group `fit_leaves`) | mount table of an actor: `mount_entry` with the type word `[[A+0x1f2]] + word [0x4a9d70]` (pure lookup; also called by the display-object code `0x437c8c`) |
| `0x40e61c` | H4 | hook `weapon_shape_word` (group `fit_leaves`) | getter: first word of the mount definition `[W+0x18]` (the shape id that `weapon_fit` passes to the shape virtual `+0x20`) |
| `0x40e074` | H4 | hook `beam_ctor` (group `weapon_ctors`) | weapon constructor, energy class (vtable `0x498dcc`; ELF through `0x4992c0`) |
| `0x40e140` | H4 | hook `gun_ctor` (group `weapon_ctors`) | weapon constructor, ballistic class (vtable `0x498d54`; LAEW through `0x499248`) |
| `0x40e274` | H4 | hook `ecm_ctor` (group `weapon_ctors`) | weapon constructor, ECM (vtable `0x498c64`) |
| `0x40e2bc` | H4 | hook `turbo_ctor` (group `weapon_ctors`) | weapon constructor, TURBO (vtable `0x498bec`) |
| `0x40e308` | H4 | hook `targ_ctor` (group `weapon_ctors`) | weapon constructor, TARG (vtable `0x498b74`) |
| `0x40e344` | H4 | hook `shield_ctor` (group `weapon_ctors`) | weapon constructor, SHIELD (vtable `0x498afc`) |
| `0x40e380` | H4 | hook `energy_ctor` (group `weapon_ctors`) | weapon constructor, ENERGY (vtable `0x498a84`) |
| `0x474bbc` | RTL | [mission_load.md](../reverse-engineering/mission_load.md) | game heap arena mark: saves the byte count `0x6bb30c` to `0x6b7a14`, requests the arena (`0x47747c`, `0x47a3fc`); memory layer, H6 |
| `0x474be4` | RTL | [mission_load.md](../reverse-engineering/mission_load.md) | game heap arena release: copies the header back (`0x49687a`), restores the byte count, `0x47a474`; memory layer, H6 |
| `0x44a60c` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD page switch (lamp, page slots `+4`/`+8`/`+0xc`, button states, redraw bytes); presentation state, never reached in the six map suites |
| `0x450b88` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD status page view (`P+0x7c`, `+0x84`, `+0x88`, `+0x90`); presentation state, never reached in the six map suites |
| `0x402278` | H4 | hook `ts_obj_ctor` (group `object_shapes`) | `TS_OBJ` base constructor: vtable chain `DRAWABLE`/`MAT_OBJ`/`TS_OBJ`, zeroed pose block `+0xc..`, the shape instance at `+0x34` (`new(0x42)` + `0x478838`), flag bytes `+0x38..` |
| `0x40332c` | H4 | hook `det_anim_shape` (group `object_shapes`) | `ANIM_OBJ` constructor (`0x402278` with the shape, vtables `0x4973ac`/`0x4973cc`, `+0x41` ← 0); bridged by `building_factory`, `mech_ctor` |
| `0x403368` | H4 | hook `det_anim_static` (group `object_shapes`) | `DET_ANIM_OBJ` constructor over `object_init` (`+0x41` ← 0); bridged by `mech_ctor`, `flyer_ctor` |
| `0x4033a4` | H4 | hook `det_anim_record` (group `object_shapes`) | `DET_ANIM_OBJ` constructor from a shape record (`0x402278` with `[[rec]]`, `+0x41` ← the record); bridged by `building_factory` |
| `0x478838` | H4 | hook `anim_instance_ctor` (group `object_shapes`) | shape-instance constructor (`0x42` bytes, vtable-less): `shape_instance` and `node_matrices` (hooks), arrays from `new[]`; the node pose that the hit tests read |
| `0x402374` | H4 | hook `object_track` (group `object_shapes`) | animation thread of an object: `track_clip`/`track_rate` (hooks), `0x478a3c`, `0x478e48`; bridged by `building_factory`, `mech_ctor`, called by `0x40df30` |
| `0x402fc0` | H4 | hook `shape_word_block` (group `object_shapes`) | copies a shape's word array (`[S+0x1c]`, count `S+0x24`) into an arena block (`0x47a288`) at `+4`; set-up of weapon and object records (`0x40df30`, `0x42067c`; also the H3 `0x40316c`) |
| `0x417aa8` | H4 | hook `mech_join` (group `group_joins`) | MECH group join (virtual of `group_ctor`): group `A+0x45`, slot `A+0x49`, default angles and position from the group point or `route_point`, `terrain_height`, formation slot `+0x78` ([mission_load.md](../reverse-engineering/mission_load.md#who-places-mec-actors-section-12-answered)) |
| `0x421ee8` | H4 | hook `flyer_join` (group `group_joins`) | FLYER group join, the same placement as `0x417aa8` without the height term ([mission_load.md](../reverse-engineering/mission_load.md#who-places-mec-actors-section-12-answered)) |
| `0x405c3c` | H4 | hook `building_join` (group `group_joins`) | building group join: placement as `0x417aa8` plus the footprint marks `0x470ed4` and two slot calls (`+0x78`, `+0x10`) |
| `0x470ed4` | H4 | hook `footprint_marks` (group `group_joins`) | marks the terrain cells inside a building's radius (`dist_gate`, cell flag writer `0x470ddc`); read by the flattening `0x47129c` ([terrain.md](../reverse-engineering/terrain.md)) |
| `0x42789c` | H4 | hook `zone_load` (group `terrain_load`) | terrain zone load (`zoneXXXX`, `terrain.cpp`): grid object `0x46befc`, materials `0x46c754`, object table `0x4aaa74..0x4aaa7c`, the zone matrix (`euler_matrix`) and the terrain pointer `0x4a0be0`, which the terrain hooks read (`ground_height`, `ground_update`, `terrain_pose`, …) |
| `0x46befc` | H4 | hook `grid_ctor` (group `terrain_load`) | terrain grid object constructor (vtable `0x4a0c74`; arrays through `0x495268`) |
| `0x495268` | H4 | hook `height_cache_ctor` (group `terrain_load`) | height-grid base constructor (vtable `0x4a6a40`; three arrays through `0x49667c`/`new[]`) |
| `0x46c754` | H4 | hook `terrain_materials` (group `terrain_load`) | terrain materials `mat0.dba` (`_fscanf` records `%d %d %d %d`), cell generation `0x46c4c4`; draws `rng_next` on the state `0x4d2599`; height-cache reset `0x4952fc` (H3) as a guest |
| `0x46c4c4` | H4 | hook `terrain_cells` (group `terrain_load`) | fills the cell grid from the material records (cell writes through `0x470d74`, `rng_next` on `0x4d2599`), then the height-cache reset |
| `0x47129c` | H4 | hook `base_flatten` (group `base_flatten`) | after the actor load: for every marked cell region the mean height (`0x470fe8`) is written back (`0x471128`), then the shading pass `0x46c2e0` (H3) as a guest |
| `0x470fe8` | H4 | hook `flood_sum` (the recursion runs iteratively in the core) | recursive flood sum of the height bytes of a marked region into `0x6b5470`/`0x6b5474` |
| `0x471128` | H4 | hook `flood_fill` (the recursion runs iteratively in the core) | recursive flood fill: height byte of each marked cell ← the mean, visit flag bit 2 through `0x470ddc` |
| `0x47136c` | H4 | hook `mark_raster` (the +0Fh bytes written exactly) | base-mark rasterisation (bridged by `base_marks`): cell marks `0x470d74` for the layout; texture bits `+0xf` (unless `-l`) are presentation |
| `0x40cd88` | H4 | hook `collision_load` (group `collision_data`) | collision data of a type (`collide.cpp`): next slot of the table `[0x4a9824]` (counter `0x4987de`), `0x40ccf8`; bridged by `mech_ctor`, called by `0x422ed0` |
| `0x40ccf8` | H4 | hook `collision_read` (group `collision_data`) | collision data reader: count, `new[]` of 10-byte entries, per entry `0x40cc50` and the bounds pass `0x40ccc8` |
| `0x40cc50` | H4 | hook `collision_entry` (group `collision_data`) | collision entry reader: two words, an array of 16-byte volumes (`0x49667c`) read through `0x40cc14` |
| `0x40c5d0` | H4 | hook `collision_radius` (group `collision_data`) | bounding radius of a collision volume (`dist3d_delta`), stored at `+0xe` by `0x40ccc8` (603..1000 calls per suite at load) |
| `0x40d160` | H4 | hook `dmg_load` (group `damage_data`) | damage data of a type (`damage.cpp`): next slot of `[0x4a9828]` (counter `0x498854`), reader `0x40d09c`; bridged by `mech_ctor`, called by `0x422ed0` |
| `0x40d09c` | H4 | hook `dmg_read` (group `damage_data`) | damage table reader: word arrays and 18-byte rows (`0x40cff8` each) |
| `0x40d2cc` | H4 | hook `damage_state_ctor` (group `damage_data`) | part-table constructor: three zeroed word arrays, counts `+0x18`/`+0x1a`; bridged by `mech_ctor`, `flyer_ctor` |
| `0x40d354` | H4 | hook `damage_state_bind` (group `damage_data`) | part-state constructor: fields `+0xc..+0x14`, lazily the shared 20-entry block `0x498864`; bridged by `mech_ctor`, `flyer_ctor` |
| `0x40d050` | H4 | hook `dmg_region_copy` (group `damage_data`) | copies a row's dword list (`_memcpy`) into a new array; called by the Q-13 row `0x40d240` |
| `0x41ff10` | H4 | hook `mech_type_select` (group `type_loads`) | MECH type record of the selection: index byte table `0x4a9d08`, count `0x4a9d06`, type word `0x4a9d70`, `0x40feb4`/`0x40cbd4`/`0x40d200` (guest calls) |
| `0x42049c` | H4 | hook `mech_types` (group `type_loads`) | MECH type table: `mechs` file through `0x492d00`, arrays `0x4a9d60`/`0x4a9d64`, display set-up `0x4378d8` (H3 guest), each type through `0x4201a8` |
| `0x4201a8` | H4 | hook `mech_type_load` (group `type_loads`) | one MECH type: OVL record (`0x492f54`), shape (`0x474cd8`), part tables `0x420090`, Q10 stats (`multiply_q10`), gun list `0x40fee8`, `0x40fc50`, `0x40304c`, debris `0x40874c`; damage display `0x4379cc` (H3 guest) |
| `0x420090` | H4 | hook `lod_remap` (group `type_loads`) | per-type part flags: clears a byte array and fills it through the shape-node walk `0x41ff88` (slots `+0x20`) |
| `0x41ff88` | H4 | hook `node_remap` (group `type_loads`) | recursive shape-node walk by class id (virtual `+0`), marks parts through `0x41ff68` (1000 calls per suite at load) |
| `0x40fee8` | H4 | hook `gun_list_load` (group `type_loads`) | gun list of a MECH type (`GUNLIST.CPP` records, mount table `0x40fea0`) |
| `0x422e90` | H4 | hook `flyer_type_select` (group `type_loads`) | FLYER type record of the selection: byte table `0x4a9dcc`, count `0x4a9dca`, `0x40d200`/`0x40cbd4` |
| `0x422fb4` | H4 | hook `flyer_types` (group `type_loads`) | FLYER type table: `flyers` file through `0x492d00`, arrays `0x4a9df0`/`0x4a9df4`, each type through `0x422ed0` |
| `0x422ed0` | H4 | hook `flyer_type_load` (group `type_loads`) | one FLYER type: OVL record, shape (`0x474c38`), damage `0x40d160` and collision `0x40cd88` data |
| `0x40fc8c` | H4 | hook `weapon_types` (group `stage2_types`) | stage-2 callback: weapon list (`weapons`, `GUNLIST.CPP`) into `0x4a98ec`/`0x4a98f0`, per entry `0x40f8bc`; projectile table `proj` into `0x4a98fc`; shapes and textures (`0x474cd8`, `0x45cdf4`, `0x46a03c`) guest |
| `0x40f8bc` | H4 | hook `weapon_entry` (group `stage2_types`) | weapon-list entry: damage `0x40cff8` and collision `0x40cc14` readers on the stream |
| `0x40a818` | H4 | hook `rocket_types` (group `stage2_types`) | stage-2 callback: rocket type records (`rockets`, `rocket.cpp`) into `0x4a96d0`/`0x4a96d4`, shapes guest |
| `0x40ade0` | H4 | hook `bullet_types` (group `stage2_types`) | stage-2 callback: bullet type records (`bullets`, `bullet.cpp`) into `0x4a96f8`/`0x4a96fc`, shapes and bitmap guest |
| `0x40b6e0` | H4 | hook `beam_types` (group `stage2_types`) | stage-2 callback: beam records (`beam`, `BEAM.CPP`) into `0x4a9804`, texture guest |
| `0x41fdb0` | H4 | hook `mech_forms` (group `stage2_types`) | stage-2 callback: MECH forms (`mforms`, `mechsys.cpp`) into `0x4a9d6c`/`0x4a9d68`, count `0x4a9d06` ← 0, bitmaps guest |
| `0x422d90` | H4 | hook `flyer_forms` (group `stage2_types`) | stage-2 callback: FLYER forms (`fforms`, `flyersys.cpp`) into `0x4a9dfc`/`0x4a9df8`, count `0x4a9dca` ← 0 |
| `0x405fac` | H4 | hook `base_types` (group `building_types`) | stage-2 callback of the building module: forms `bforms`, weapon table `lc_wpns`, types `bases`, collision `basecol` (`0x40ccf8`), the count words, hulks, textures, debris (`0x40874c`); globals `0x4a9572..0x4a95bf`; shapes and textures guest |
| `0x405df4` | H4 | hook `base_pools` (group `building_types`) | building pools of pass 1 (`pool_create`, `0x4a95a0`, `0x4a95a4`); called by the world load `0x424308` |
| `0x407b54` | H4 | hook `explosion_pools` (group `effect_pools`) | stage-2 callback: explosion pool (`pool_create`, `0x4a961e`/`0x4a9622`) and the explosion records (`EXPLO.CPP`) into `0x4a9628..0x4a963c`; shapes and bitmaps guest |
| `0x408860` | H4 | hook `debris_pools` (group `effect_pools`) | stage-2 callback: debris pool (`0x4a9642`/`0x4a9646`), default debris `def_deb` (`0x40874c`), constants `0x4a9664..0x4a9668` (6000, 16000, 420) |
| `0x40874c` | H4 | hook `debris_set` (group `effect_pools`) | debris set of a name (`debris.cpp`): shape list guest, records `0x4083f8` |
| `0x4083f8` | H4 | hook `debris_record` (group `effect_pools`) | debris record reader: two word reads, the pieces (`new[]` of 0Eh each, seven word reads per piece, +8/+0Ah x 182 unless FFFFh) |
| `0x4097a8` | H4 | hook `flat_pools` (group `effect_pools`) | flat-object pool (`pool_create`, `0x4a968d`/`0x4a9691`) and shapes `flat`/`flat2`; called by the world loader `0x42e010` |
| `0x409a34` | H4 | hook `meteor_pools` (group `effect_pools`) | stage-2 callback: meteor pool (`0x4a96aa`/`0x4a96ae`), shapes `meteor`, bitmap `impact` |
| `0x46b1a8` | H4 | hook `fire_pools` (group `effect_pools`) | stage-2 callback: fire pool (`0x6b4f30`/`0x6b4f34`) and fire records (`fire.cpp`) into `0x6b4f2c`/`0x6b4f38`; shapes and bitmaps guest |
| `0x40df30` | H4 | hook `weapon_ctor` (group `weapon_ctors`) | common weapon set-up of the energy and ballistic constructors: weapon shape `0x40fab0` (platform guest), mesh offset `0x40dd4c` (H3 guest), `0x402fc0`, `0x402374`, `mount_offset` |
| `0x4231a4` | H4 | hook `group_order` (group `mfd_orders`) | group order (`0x43166c` only): picks members by the order code's byte (table `0x423239`/`0x423249`) and distance (`dist_gate`), member virtual `+0x28`; reads an uninitialised stack word for codes 3, 6, 9..0Eh ([input.md](../reverse-engineering/input.md#the-residue-reader-the-group-order-0x4231a4)) |
| `0x446844` | H4 | hook `mfd_button` (group `mfd_orders`) | `MFDSelectGadget` button owner (vtable slot `+0x40`): page buttons `0x446e60` (H3), range cycle `0x446ff0`, `cockpit_next` through `0x446fb0`, the order `0x447248`, the byte `+0x96` of the actor `[[0x49b088]+0x203]` (read by the group order `0x4231a4`) |
| `0x4470c0` | H4 | hook `mfd_click` (group `mfd_orders`) | MFD entry click: point in one of six rectangles → `button_press` and the order `0x447248`, else entry select `0x4470a4` |
| `0x447248` | H4 | hook `mfd_order` (group `mfd_orders`) | MFD order: code `0x43f9c0(menu)` into `0x4d03d4`, `0x43166c` → group order `0x4231a4`, then `0x43f9f8` |
| `0x431530` | H4 | hook `order_setup` (group `mfd_orders`) | mission-load set-up of the player's group `0x49b0f8` (read by `0x43166c`) and up to three wingmen `0x4d03c8` (read by `0x433134`, the HDD selection); HDD panels `0x44a7e8` (H3 guest) |
| `0x44cc68` | H4 | hook `map_key` ([cockpit.md](../reverse-engineering/cockpit.md#hdd-map-key-path)) | `HDDMap` slot 4 (key handler, jump table `0x44cccb`): the map keys → `0x44d5c0`, mode buttons → `button_press`, `0x44ee48`/`0x44ee88` |
| `0x44d5c0` | H4 | hook `map_press` ([cockpit.md](../reverse-engineering/cockpit.md#hdd-map-key-path)) | map key path: event point from a mode button, the cursor point `0x4a3620` (via `0x486ece`; D-34 said the player's position) or the current target projected onto the map (`0x48c2e4`/`0x48c444`/`0x48c57c` H3 guests, `0x44d24c`), recorded by `0x44d3cc` for `map_areas` ([cockpit.md](../reverse-engineering/cockpit.md)) |
| `0x44d82c` | H4 | hook `wingman_pick` ([cockpit.md](../reverse-engineering/cockpit.md#hdd-map-key-path)) | map click: `map_pick`, a living wingman (`0x433134`) → selection `0x44a748` (`wingman_select`); `[0x49d6ae]` ← 1 ([cockpit.md](../reverse-engineering/cockpit.md)) |
| `0x44a1a0` | H4 | hook `hdd_execute` (group `hdd_exec`, [cockpit.md](../reverse-engineering/cockpit.md#hdd-button-executor)) | HDD action executor under `HDDGauge` slot `+8` (`0x449bf8`): the button index `+0x528` that the button owner `0x44a174` records → page switch `0x44a60c`, map scroll, status-page cycling (H3 guests) and the wingman selection `0x44a748` (`wingman_select`) |
| `0x461ff0` | H4 | hook `mission_end` (group `mission_end`) | mission end of the game main: stream close, `results_writer` (hook), exit code `0x4d27b8`, byte `0x4d1dd6` ← 0 ([launcher.md](../reverse-engineering/launcher.md#the-mission-end-0x461ff0-as-own-code)); the prefs write and the tape-mode deletes are in the caller `0x45f160` |
| `0x424308` | H4 | hook `world_load` (group `mission_world`) | world load, pass 1 ([mission_load.md](../reverse-engineering/mission_load.md#2-pass-1-world-load-0x424308)) |
| `0x4253d8` | H4 | hook `actor_load` (group `mission_actors`) | actor load, pass 2 ([mission_load.md](../reverse-engineering/mission_load.md#3-pass-2-actor-load-0x4253d8)) |
| `0x461608` | H4 | hook `mission_root` (group `mission_root`, entry shim) | the mission-load root: the load's sequence from the input set-up to the start sequence, its callees through the bridge ([mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root)) |
| `0x405ebc` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) (stays a guest of `world_load`; reads only its first argument) | shape lists of the base types (`bases_an`/`bases`, `dts`/`dgs`) through `0x474de4`, material pointers `+0x26` |
| `0x40f998` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | weapon shapes `mechwpns` into the memory images `0x4a9904`/`0x4a99f4` (`0x47a540`, `0x47a308`), count `0x4a9ae4` |
| `0x40fab0` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | weapon shape from the memory image (memory stream `0x47b36c`, `0x474c10`, destructor `0x47a8a4`) |
| `0x427568` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `GridShape` (registry tag `0x2bc0001`; arena `0x47a2c8`, read virtual `+0xc`) |
| `0x42762c` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | `GridShape` read method (slot `+0xc`): part list `0x490e68`, arena arrays |
| `0x430f58` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | load-by-name wrapper (`0x472f14`, `dts_load`, `stream_destroy`), as `0x45cdf4` |
| `0x453630` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | load-by-name wrapper for the HUD fonts, as `0x45cdf4` |
| `0x453684` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | load-by-name wrapper for the HUD fonts, as `0x45cdf4` |
| `0x465f7c` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | load-by-name wrapper, as `0x45cdf4` |
| `0x474b30` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | shape-list load (`0x47a308`, `dts_load`) |
| `0x474c38` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | shape load of a type inside a heap arena mark/release (`0x474bbc`/`0x474be4`), `dts` |
| `0x474cb0` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | palette load (`dpl`) through `0x474b30` |
| `0x474cd8` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | shape-list load into an arena (`libio.cpp`; `0x47a3b4`, `0x47a308`, `dts_load`); 11 callers, all loaders |
| `0x474de4` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | shape-list load inside an arena mark (`libio.cpp`) |
| `0x47a308` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | shape-list header and member load (`0x47a4a0` registry, `0x482bd7`, `0x4930ab`, `0x47a874`) |
| `0x47ae68` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | read-stream open helper (`0x47ae28`, CW3220 `_open`), called by `0x47ad40` and `0x4614c0` |
| `0x4820d4` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `GLPalette` |
| `0x4821cc` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | `GLPalette` read method (slot `+0xc`) |
| `0x4827d4` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `GLFont` |
| `0x482a14` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | `GLFont` read method (slot `+0xc`) |
| `0x48df64` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `TSPoly` |
| `0x48e178` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `TSSolidPoly` |
| `0x48e258` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | read method (slot `+0xc`) of `TSSolidPoly`, `TSTexture4Poly`, `TSShadedPoly` and the alias classes |
| `0x48e31c` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `TSTexture4Poly` |
| `0x48e3e0` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `TSShadePoly` |
| `0x48e4a4` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `TSGouraudPoly` |
| `0x48e588` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | read method (slot `+0xc`) of `TSGouraudPoly`/`TSAliasGouraudPoly` |
| `0x48e8b0` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `TSGroup` |
| `0x48e9f0` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | read method (slot `+0xc`) of the horizon-line class (`hzline`, [horizon.md](../reverse-engineering/horizon.md)): arena arrays, `dts_load` |
| `0x48fb74` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `TSBitmapPart` |
| `0x48fce0` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | `TSBitmapPart` read method (slot `+0xc`) |
| `0x48fd98` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `TSPartList` |
| `0x48fea0` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | `TSPartList` read method (slot `+0xc`): `n` sub-objects through `dts_load` |
| `0x48fff4` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `TSCellAnimPart` |
| `0x4900dc` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | `TSCellAnimPart` read method (slot `+0xc`) |
| `0x4901a0` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `TSDetailPart` |
| `0x490288` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | `TSDetailPart` read method (slot `+0xc`) |
| `0x490388` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `TSBSPPart` |
| `0x490488` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | `TSBSPPart` read method (slot `+0xc`) |
| `0x490d68` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `TSShape` |
| `0x490e68` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | shared read helper (`TSPartList` read `0x48fea0`, arena arrays) of `GridShape` and `0x492244` |
| `0x4913f0` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `ANSequence` |
| `0x4914f0` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | read method (slot `+0xc`) of `ANSequence`/`ANCyclicSequence` |
| `0x4919a0` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `ANCyclicSequence` |
| `0x491a50` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `ANAnimList` |
| `0x491bb0` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | `ANAnimList` read method (slot `+0xc`): sequences through `dts_load` |
| `0x492164` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | persistent-object factory `ANShape` |
| `0x492bec` | platform | [file_io.md](../reverse-engineering/file_io.md) | path builder: directory `0x492b90` + name + extension into the static buffer `0x6cbc2a` (`_strcat`); 57 callers, all loaders |
| `0x492c84` | platform | [file_io.md](../reverse-engineering/file_io.md) | file size by name (`stream_size`), 0 when the open fails |
| `0x492d00` | platform | [file_io.md](../reverse-engineering/file_io.md) | reads a whole file into a `new[]` buffer (`0x492c84`, `stream_read`, `0x492c4c`) |
| `0x492f54` | platform | [file_io.md](../reverse-engineering/file_io.md) | reads n records of a file (`OVL.CPP`) into a caller buffer (`stream_read`) |
| `0x45ef1c` | platform | [file_io.md](../reverse-engineering/file_io.md) | string resource load (`str`, language byte `0x49f257`) into the caller buffer |
| `0x45ef9c` | platform | [file_io.md](../reverse-engineering/file_io.md) | string resource load (`str`) for the cockpit message ports |
| `0x4614c0` | platform | [file_io.md](../reverse-engineering/file_io.md) | launch peek: `script.dat` header word 0, `player.mec` words 0..4 ([mission_load.md](../reverse-engineering/mission_load.md#1-call-tree-of-0x461608-in-execution-order) "file") |
| `0x452ae4` | platform | [input.md](../reverse-engineering/input.md) | mouse event queue (two lists of 100 × 14 bytes) and the `0x452cdc` callback (mission_load.md "input") |
| `0x452cdc` | platform | [input.md](../reverse-engineering/input.md) | mouse callback: `mouse_queue` (hook), clock `0x467828`, cursor `0x486e7c` |
| `0x45a5dc` | platform | [input.md](../reverse-engineering/input.md) | input set-up: device objects, flight-key capture, prefs, the device block `0x4d2310..0x4d238c` (mission_load.md "input") |
| `0x45b68c` | platform | [input.md](../reverse-engineering/input.md) | flight-key capture on/off ([display_modes.md](../reverse-engineering/display_modes.md)) "I") |
| `0x45bc10` | platform | [input.md](../reverse-engineering/input.md) | input device helper (`0x45bf04`, `0x45bef8`, `_memcpy`), only from `0x45baa8` (platform) |
| `0x45bda4` | platform | [input.md](../reverse-engineering/input.md) | input device id store (`0x4d2498..0x4d24a4`; [input.md](../reverse-engineering/input.md) mouse id 2) |
| `0x45be50` | platform | [input.md](../reverse-engineering/input.md) | input device id store (`0x4d24a0`, `0x4d24a4`) |
| `0x45c01c` | platform | [input.md](../reverse-engineering/input.md) | keyboard device object (constructor `0x4779a8`) |
| `0x45c1d4` | platform | [input.md](../reverse-engineering/input.md) | mouse device object (constructor `0x4805e0`) |
| `0x45c298` | platform | [input.md](../reverse-engineering/input.md) | joystick device object (constructor `0x4775dc`) |
| `0x45c2e0` | platform | [input.md](../reverse-engineering/input.md) | `IMJoystick` slot `+0` (destructor through `0x477630`) |
| `0x45c330` | platform | [input.md](../reverse-engineering/input.md) | `IMJoystick` slot `+4`: device poll (`0x47785c`, axes into `0x4d242c..0x4d2466`) |
| `0x45972c` | platform | [input.md](../reverse-engineering/input.md) | prefs path (`data\prefs.cfg`) and the prefs read `0x45a1b8` ([file_io.md](../reverse-engineering/file_io.md)) |
| `0x459d88` | platform | [input.md](../reverse-engineering/input.md) | voice VOL existence test (`vol\simvoic<lang>.vol`, `_fopen`) |
| `0x459df0` | platform | [input.md](../reverse-engineering/input.md) | joystick defaults into the prefs (`prefs_store`, `0x45be50`, `0x45c298`) |
| `0x45a1b8` | platform | [input.md](../reverse-engineering/input.md) | prefs read (`prefs_load`) and the device callbacks `0x459c74..0x459d68` |
| `0x45a274` | platform | [input.md](../reverse-engineering/input.md) | joystick summary of the prefs (`0x477904`, `0x4d20b5`) |
| `0x4775dc` | platform | [input.md](../reverse-engineering/input.md) | joystick device constructor (`0x480590`, `0x477674`) |
| `0x477630` | platform | [input.md](../reverse-engineering/input.md) | joystick device destructor |
| `0x477674` | platform | [input.md](../reverse-engineering/input.md) | joystick capabilities (`joyGetDevCapsA`, `joyGetPosEx`) into `0x6bb310..0x6bb6a0` |
| `0x477720` | platform | [input.md](../reverse-engineering/input.md) | joystick poll (`joyGetPosEx`) into the device block `0x6bb318..0x6bb3e4` |
| `0x4779a8` | platform | [input.md](../reverse-engineering/input.md) | keyboard device constructor (`0x480590`, `0x477a4c`, `0x479cfc`, `0x477b18`) |
| `0x4805e0` | platform | [input.md](../reverse-engineering/input.md) | mouse device constructor (`0x480590`, `0x480694`) |
| `0x480694` | platform | [input.md](../reverse-engineering/input.md) | mouse device set-up (`0x465fec`, `0x480738`, `0x480764`) |
| `0x4808dc` | platform | [input.md](../reverse-engineering/input.md) | mouse cursor placement (`GetClientRect`, `ClientToScreen`, `SetCursorPos`; `0x6c5f62`/`0x6c5f66`) |
| `0x480948` | platform | [input.md](../reverse-engineering/input.md) | mouse event into the handler table `0x6c5ed8` slot `+8` (`0x6c5f50..0x6c5f58`) |
| `0x4809f8` | platform | [input.md](../reverse-engineering/input.md) | mouse window-message handler of the MainWndProc list (WM_MOUSEMOVE/BUTTON → `0x480948`) |
| `0x480af0` | platform | [input.md](../reverse-engineering/input.md) | graphics-library input object `(obj, 0x26d3)` ([mission_load.md](../reverse-engineering/mission_load.md#1-call-tree-of-0x461608-in-execution-order) H3 row) |
| `0x466730` | platform | [input.md](../reverse-engineering/input.md) | window client rectangle and cursor clip (`GetSystemMetrics`, `ClipCursor`), `0x46548c` |
| `0x462398` | platform | [audio.md](../reverse-engineering/audio.md) | `sfxWndProc`, the sound window-message handler of the MainWndProc list (sound context `[0x49f8ec]`, CD track `0x49f8fc`, loop byte `0x49f900`) |
| `0x462410` | platform | [audio.md](../reverse-engineering/audio.md) | sound init (window hook, voice channels, `sound_config`, driver, sample preload; sound context `[0x49f8ec]`) |
| `0x46254c` | platform | [audio.md](../reverse-engineering/audio.md) | sound sample table (`sounds`, `str`) and preloads through the SFX manager |
| `0x462734` | platform | [audio.md](../reverse-engineering/audio.md) | sound context release (`[0x49f8ec]` written, `0x464888`, `0x466010`, driver release `0x4638f0`) |
| `0x463014` | platform | [audio.md](../reverse-engineering/audio.md) | SFX manager call `0x464618` with a scaled value (`0x47e08d`); caller `0x42a9c0` (H3) |
| `0x463694` | platform | [audio.md](../reverse-engineering/audio.md) | sound driver start (`new[]`, `0x473708`), from the sound init `0x462410` |
| `0x4638f0` | platform | [audio.md](../reverse-engineering/audio.md) | sound driver release (`0x4737a4`) |
| `0x473708` | platform | [audio.md](../reverse-engineering/audio.md) | SOS driver initialisation (`0x4958fd` DLL load, `0x473eac`) |
| `0x4737a4` | platform | [audio.md](../reverse-engineering/audio.md) | SOS driver release path (`0x4741fc`, SOS stub `0x495a1f`) |
| `0x4737d8` | platform | [audio.md](../reverse-engineering/audio.md) | SOS call wrapper (`0x474350`, SOS stub `0x495b2b`) of the SFX manager (`0x4639e8`, `0x464260`) |
| `0x473898` | platform | [audio.md](../reverse-engineering/audio.md) | SOS call wrapper (`0x474350`, SOS stubs) of the SFX manager `0x464038` |
| `0x4739ac` | platform | [audio.md](../reverse-engineering/audio.md) | SOS call wrapper of the SFX manager `0x464118` |
| `0x473a5c` | platform | [audio.md](../reverse-engineering/audio.md) | SOS call wrapper of the SFX manager `0x4641e8` |
| `0x473aec` | platform | [audio.md](../reverse-engineering/audio.md) | SOS call wrapper of the SFX manager `0x464618` (writes `0x4a0e30`) |
| `0x473b54` | platform | [audio.md](../reverse-engineering/audio.md) | SOS call wrapper of the SFX manager `0x46466c` |
| `0x473b80` | platform | [audio.md](../reverse-engineering/audio.md) | SOS call wrapper of the SFX manager `0x4646dc` |
| `0x473c48` | platform | [audio.md](../reverse-engineering/audio.md) | CD audio device (`cdaudio`, `mciSendCommandA`) |
| `0x473eac` | platform | [audio.md](../reverse-engineering/audio.md) | MIDI driver and banks (`MELODIC.BNK`, `DRUM.BNK`) |
| `0x4741fc` | platform | [audio.md](../reverse-engineering/audio.md) | SOS driver shutdown (SOS stubs `0x495a59` … `0x495ead`, state `0x4a0e50..0x4a0e58`) |
| `0x474508` | platform | [audio.md](../reverse-engineering/audio.md) | SOS memory release (`GlobalUnlock`, `GlobalFree`), caller `0x473828` (platform) |
| `0x4958fd` | platform | [audio.md](../reverse-engineering/audio.md) | SOS DLL load (`sos9503.dll`/`sos32s03.dll`, `GetProcAddress`) |
| `0x483a04` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | span or line fill through the blit slots `[0x4a5834]`, graphics context `0x4a3614` |
| `0x483c6c` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | span helper of `0x483a04` |
| `0x483d6c` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | span helper of `0x483a04` |
| `0x483eb8` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | polygon fill (edge setup `0x484720`/`0x484338`, slot `[0x4a5830]`) |
| `0x4842bb` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | polygon fill entry (`0x484222`, `0x483eb8`) |
| `0x484338` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | polygon edge walk of `0x483eb8` |
| `0x484720` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | polygon edge setup of `0x483eb8` (vertex min/max, `0x4a33d4`) |
| `0x4866f0` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | polygon edge walk (code pointer of `0x484720`) |
| `0x486704` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | rectangle fill through the blit slot `[0x4a57ac]` (38 callers, all draws) |
| `0x486a34` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | bitmap span copy of `0x48817c` |
| `0x486c00` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | clip helper of the fills |
| `0x486d15` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | clip helper of the bitmap copies |
| `0x487bdc` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | bitmap blit through `[0x4a57f8]`/`[0x4a57fc]` |
| `0x4895ec` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | line draw (slots `[0x4a57c0]`/`[0x4a57c4]`) |
| `0x4899ac` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | raster slot of the table `0x4a58d4` |
| `0x48a1d4` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | raster surface set-up (frees and allocates `[0x4a03d0]`, rows into `0x4d2ff4..0x4d301c`); slot of `0x4a5898` |
| `0x48a924` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | textured span set-up (slot of `0x4a58f0`): `tex_edge` (hook), `0x48abe4`, `0x48aa50`; state `0x6c5f7c..0x6c5f8c` |
| `0x48abe4` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | texture gradient set-up into `0x4a5900..0x4a5920` |
| `0x493192` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | span walk of the terrain scan converter `0x4930c1` (H3) |
| `0x49495f` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | span walk of the terrain scan converter (code pointer of `0x493192`) |
| `0x480ecf` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | graphics-context helper of `0x480d44` (`0x48151d`, `0x489558`) |
| `0x480f38` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | surface object method (`0x48151d`, `0x481526`, `0x4811e8`) |
| `0x480f58` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | surface object constructor (`0x480590`, `0x4812f0`) |
| `0x480ffc` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | surface object destructor (`0x4811e8`) |
| `0x481028` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | surface creation (`0x489416`, `0x48de2f`, `0x489570`), count `0x4a2e2c` |
| `0x4811e8` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | surface release, count `0x4a2e2c` |
| `0x481224` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | viewport and clip rectangle `0x4a3652..0x4a367a`, blit slots `[0x4a57ec]`/`[0x4a57f0]`/`[0x4a57f4]` |
| `0x481f6c` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | destructor of a graphics-library object (vtable `0x4a30f8`): frees its arrays, base destructor `0x4838fd` |
| `0x482cec` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | `GLRect` slot `+0x1c` (`0x48d99e`, `0x482d68`) |
| `0x48bab4` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | graphics-library table into `[0x4a5af8]` (cockpit set-up) |
| `0x48bc70` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | graphics-library light object constructor (vtable `0x4a5b39`) |
| `0x48c16c` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | light intensity at a normal from the light list `0x6c60a8`/`0x6c60ac` (terrain shading) |
| `0x48cce4` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | polygon projection into the render context (`0x6c6028..0x6c6038`, `0x6c68e4..`) |
| `0x48d38c` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | near-plane clip into the render context `0x6c60d4` |
| `0x48d5c0` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | render-context state (`0x6c6050`/`0x6c6058`) |
| `0x48dd08` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | graphics-library state (`memset 0x6c60d4`) and class registrations `0x47a0b8` ([mission_load.md](../reverse-engineering/mission_load.md#1-call-tree-of-0x461608-in-execution-order)) |
| `0x4911c0` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | DTS node draw helper of `0x476b28` |
| `0x4912a0` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | DTS vertex projection (`matrix_point`, render context `0x6c6028..`), caller `0x48c954` (H3) |
| `0x4760e4` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | DTS draw state (`0x6b7b68`, `0x6bb2b0`, `0x6bb2e4`), callers `0x490a64`, `0x490c1c` (H3) |
| `0x476b28` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | recursive DTS node draw (slot `+0x1c`), caller `0x476c18` (H3) |
| `0x4800c0` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md) | Q14 vector × matrix (`enter` prologue); callers the light set-ups `0x46134c` and `0x48da08` only |
| `0x46c2e0` | H3 | [terrain.md](../reverse-engineering/terrain.md) | shading pass over the grid: per cell `0x46c3f0`, view reset `0x48c444` |
| `0x46c3f0` | H3 | [terrain.md](../reverse-engineering/terrain.md) | cell normals `+1..+0xc` (`0x46bfdc`, `normalize`) and shade bytes `+0xd`/`+0xe` (`0x48c16c`); read by the back-face test and the cell draw only (the simulation's `segment` computes its own normal) |
| `0x46bfdc` | H3 | [terrain.md](../reverse-engineering/terrain.md) | cell normal from the four corner heights |
| `0x46a03c` | H3 | [terrain.md](../reverse-engineering/terrain.md) | texture table of a bitmap (`0x469e14`), 14 callers, all loaders |
| `0x469e14` | H3 | [terrain.md](../reverse-engineering/terrain.md) | recursive texture level build of `0x46a03c` |
| `0x46bc44` | H3 | [terrain.md](../reverse-engineering/terrain.md) | flat terrain cell variant (render context, `0x48cf20`, `0x48cce4`) |
| `0x46bd9c` | H3 | [terrain.md](../reverse-engineering/terrain.md) | terrain texture table `[0x6b4f40]` (`0x45cdf4`, `0x46a03c`) |
| `0x4680f0` | H3 | [terrain.md](../reverse-engineering/terrain.md) | haze remap (`0x4a08ac`/`0x4a08b0`) of the terrain and sky draws |
| `0x42e010` | H3 | [horizon.md](../reverse-engineering/horizon.md) (stays a guest of `world_load`; every other reader of its globals draws) | world loader: WLD header (horizon painter `[0x49aee0]`, words `0x4cfcc4..0x4cfd04`, read only by the horizon and damage-palette code), palette `cyc`/`shades`/`rmp` tables; its flat-object pool `0x4097a8` is H4 |
| `0x42e760` | H3 | [horizon.md](../reverse-engineering/horizon.md) | shade tables (`shades`) into `0x4cfce0` |
| `0x430d08` | H3 | [horizon.md](../reverse-engineering/horizon.md) | table transform of the world loader (no calls, no globals) |
| `0x461294` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#1-call-tree-of-0x461608-in-execution-order) | graphics context `[0x4d24ce]` (`0x480c34`, `0x480d44`) |
| `0x46134c` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#1-call-tree-of-0x461608-in-execution-order) | scene lights (`0x48bc40`), `euler_matrix`, `0x4800c0`, `0x48be50`, `0x407800` |
| `0x461450` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#1-call-tree-of-0x461608-in-execution-order) | loading-screen bitmap, blitted and presented |
| `0x44f6f4` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#1-call-tree-of-0x461608-in-execution-order) | bitmap and graphics-context set-up (`BMP_8`, `VGA_8`, Gouraud fills) |
| `0x4544c0` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#1-call-tree-of-0x461608-in-execution-order) | HUD fonts and bitmaps (`hfn`), surface properties `0x4d1e68..0x4d1e84` |
| `0x429660` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#1-call-tree-of-0x461608-in-execution-order) | cockpit object D (`cockpit.dpl`, layout records), layout rectangles `0x4cfa28..0x4cfa54` |
| `0x437c8c` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#1-call-tree-of-0x461608-in-execution-order) | per-MECH display object at `A+0x1fe` from the mount table `0x420634` |
| `0x43048c` | H3 | [presentation.md](../reverse-engineering/presentation.md#keep-list-for-a-presenter) | palette flush: `0x481fe0`, upload `0x465cc4` when `[0x49b020]` changed |
| `0x4306ac` | H3 | [presentation.md](../reverse-engineering/presentation.md#keep-list-for-a-presenter) | palette cycling of up to five ranges; reads the clock at `0x4306b6` every call, so it stays original (row of the presentation section) |
| `0x436f70` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | EventPort frame (Q+U+C: message queue, port draw slot, clock); stays a guest call of `render_root` (row of the render root section) |
| `0x429834` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | cockpit layout (`vue`, `impactcp`): regions `0x42dcf0`, mech view `0x429c2c`, panel `0x431bf8`, palettes |
| `0x429c2c` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | mech-view model load (`MECHVIEW.CPP`, `dts_load`) |
| `0x42a1f0` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | mech-view draw (`0x4816a8`, `0x486704`, `0x4316c0`) |
| `0x42dcf0` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | cockpit region objects (`GLRectangleRegion`/`GLRegion`) from a stream |
| `0x430ea0` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | cockpit graphics object (`0x480c34`) |
| `0x431008` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | panel strings `0x431098`, buffers `0x4d040c`/`0x4d0410` |
| `0x431098` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | panel string and font tables (`hfn`, `dfn`, `strings0`), message tables `0x437598` |
| `0x437598` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | cockpit message tables `0x4d1298..0x4d16b0` (string resources `0x4539a0`) |
| `0x4316c0` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | panel draw helper (`0x4311e0`/`0x431210`) |
| `0x431778` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | panel layout reader (`PANEL.CPP`) |
| `0x431bf8` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | panel constructor: the gauges (`0x432074` … `0x432634`), message ports, MFD and HDD objects |
| `0x432074` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | weapon gauge factory (energy class, `0x440a90`), `[0x49b0a8]` |
| `0x432124` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | weapon gauge factory (ballistic class, `0x440fa0`) |
| `0x4321d4` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | weapon gauge factory (equipment classes, `0x44154c`/`0x441a5c`) |
| `0x432340` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | gauge factory (`0x441df8`, `gauge_mode`) |
| `0x4323e0` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | gauge factory (`0x444d84`, `0x445138`) |
| `0x432454` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | gauge factory (`0x443524`, `0x444bc4`) |
| `0x4324c8` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | MFD factory (`0x445240`, `0x447678`, page `0x446e60`) |
| `0x43254c` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | throttle gauge factory (`0x447bac`, `0x4488f4`) |
| `0x4325c0` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HUD factory (`0x43c800`, `0x43e140`) |
| `0x432634` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD factory (`0x448cf0`, `0x44bef8`) |
| `0x434368` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | system buttons (`sysbuttn`), `0x434664`, `0x452c6c` |
| `0x434664` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | button gadget constructor (`0x4524a0`, `0x488045`) |
| `0x440834` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | weapon gauge bitmaps (`pweapons`, `wpn_dmg`) |
| `0x440a90` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | energy weapon gauge constructor |
| `0x440fa0` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | ballistic weapon gauge constructor |
| `0x44154c` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | equipment gauge constructor |
| `0x441a5c` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | equipment gauge constructor |
| `0x441df8` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | gauge constructor (`0x442c28`) |
| `0x442204` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | panel select gadget constructor |
| `0x4424b0` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | panel gadget constructor (`0x442204`) |
| `0x442978` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | slider gadget constructor (`PanelSliderGadget`/`PanelHSliderGadget`) |
| `0x443524` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | shield gauge constructor (`0x443918` H3, `0x444b14`) |
| `0x443758` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | shield gauge draw (`0x443918`, `0x444a90`) |
| `0x444a90` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | gauge value text (`panel_visible`, `_itoa`, `0x438920`) |
| `0x444d84` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | gauge constructor (`0x4395c4`) |
| `0x444f48` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | `WeaponSelectGadget` draw method (`0x4390b8`, `panel_visible`, `0x480d44`) |
| `0x445240` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | MFD constructor (`mfd`, `radar` bitmaps, pages `0x43a2e0`, `0x43e734`, `0x43f600`, `0x43faf4`, `0x4404bc`) |
| `0x446e60` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | MFD page switch (`button_press`), state `0x49cbc4..0x49cbc8` read only by MFD code (D-19 rule) |
| `0x447bac` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | throttle gauge constructor (`throttle`, `0x447e4c`) |
| `0x447e4c` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | throttle slider constructor (`ThrottleVSliderGadget`, `0x439344`) |
| `0x4480c8` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | `ThrottleVSliderGadget` draw (slot `+0`) |
| `0x448cf0` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD constructor (`hdd`, `hddclip`, `pilots`; map `0x44c28c`, status `0x4507c4`) |
| `0x449a78` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | `HDDSelectGadget` draw (slot `+0x44`) |
| `0x44a7e8` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD panels (`pilot`, `ofs`, `PHDD.CPP`) |
| `0x44ba54` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD status draw helper |
| `0x44bb60` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | `HDDSelectGadget` slot `+0` draw |
| `0x44c28c` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD map constructor (`HDDMap`, icons `0x44f158`, records `0x450617`) |
| `0x44df00` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD map object icons from the actor pools (draw list of `0x44c8bc`) |
| `0x44e0a8` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD map icon of one actor (`0x433134`, `0x44f65c`) |
| `0x44f0c4` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD map projection draw (`0x48c2e4`, `0x48c444`, `0x48c57c`, `0x48c5fc`) |
| `0x44f158` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD map icons (`icons`) |
| `0x4507c4` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD status page constructor |
| `0x450c7c` | H3 | [cockpit.md](../reverse-engineering/cockpit.md) | HDD status page draw (0 in all suites) |
| `0x4351ec` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | message-port frame helper (clock `0x467828`, `0x435140`), caller `0x435610` (H3) |
| `0x435970` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | message strings (`system`, `commandX`, `pilotX`) |
| `0x435b80` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | EventPort post (clock, `0x435298`, `0x4d1294`) |
| `0x435bf0` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | message port constructor (`0x435490`) |
| `0x436244` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | message port constructor (`0x435bf0`) |
| `0x436318` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | `CommandPort` slot `+8`: message text composition (`0x453a10`, `0x4824ec`) |
| `0x43660c` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | `CommandPort` slot `+4`: port draw (blit slots, `0x437164`), voice name `TMx_0000` |
| `0x437094` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | `CommandPort`/`DialogPort` slot `+0x18`: port draw |
| `0x437164` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | port text draw (`0x487e60`, blit slots) |
| `0x4378d8` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | player damage display bitmaps (`weapons`, `hba`/`dba`) |
| `0x4379cc` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | damage display of a MECH type (`pdamage.cpp`) |
| `0x437e94` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | radar draw (0 in all suites) |
| `0x438230` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | radar draw (0 in all suites) |
| `0x438624` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | radar draw helper (0 in all suites) |
| `0x4395e8` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | LED bar graph draw (`0x439758`, `0x486704`) |
| `0x43a2e0` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | radar icons (`bases`, `vehicles`, `flyers`) |
| `0x43a5a0` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | radar page draw (`condition_record` and `dist_gate` read only; 0 in all suites) |
| `0x43b238` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | radar page slot (clock, `0x43122c`) |
| `0x43b288` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | radar text (`_itoa`, `0x438920`) |
| `0x43b5a4` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | HUD tick bitmap (`hudhtick`) |
| `0x43c148` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | HUD gadget constructor |
| `0x43c204` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | HUD gadget slot (`0x486ece`, `0x433d90`) |
| `0x43c290` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | HUD layout records `0x4d1808..0x4d1824` |
| `0x43c800` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | HUD constructor (`hud`, digits, speed and range gadgets) |
| `0x43d5f0` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | HUD draw (`0x43dcd4`, `0x43dd98`, `0x43dca0`) |
| `0x43dca0` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | speed text (`hud_speed`, `_itoa`, ` K/H`) |
| `0x43e734` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | MFD page constructor |
| `0x43f600` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | MFD page constructor |
| `0x43f7cc` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | MFD page draw |
| `0x43f8a0` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | MFD page draw |
| `0x43fa5c` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | MFD page draw helper |
| `0x43faf4` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | MFD page constructor (camera view) |
| `0x43fe44` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | MFD camera view draw (terrain detail `0x4aaaa8`/`0x4aaaac` saved and restored; 0 in all suites) |
| `0x4404bc` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | MFD page constructor (`0x480c34`, `0x48bda4`) |
| `0x44060c` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | MFD page draw (`0x45030c`, `0x483a04`) |
| `0x456cf0` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | alert helper (`0x4543b0`, `0x48349c`) |
| `0x4566e0` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | preferences alert constructor (`prf_alrt`) |
| `0x456d68` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | `PrefsAlert` slot `+8` |
| `0x4570fc` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | `PrefsAlert` slot `+0` draw |
| `0x45719c` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | preferences alert helper |
| `0x457210` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | preferences alert helper |
| `0x457b00` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | `ObjectiveAlert` slot `+8` |
| `0x457b74` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | `ObjectiveAlert` slot `+0` draw |
| `0x454c98` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | modal alert input poll (`control_record`, cursor `0x4808dc`, `0x486e7c`); the alerts are H3 (`0x45464c`, `0x4548c8`, `0x454acc`) |
| `0x471e10` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | blink timer (clock `0x467828`, `0x45c8bc`), callers `0x438bc0`, `0x446350` (H3) |
| `0x471e88` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | blink timer (clock `0x467828`, `0x45c8bc`), callers `0x438bc0`, `0x446350` (H3) |
| `0x40dd4c` | H3 | guest of the weapon constructors | recursive walk over a weapon shape's parts that moves the vertices of the selected part classes by the mount offset; mesh geometry for the draw |
| `0x40e0e0` | H3 | — | `ENERGY_WPN`/`ELF_WPN` slot `+0x64`: creates the weapon's cockpit gauge (`0x432074`, `0x440e9c`, gauge slot `+0x10`) at `W+0x77` |
| `0x496754` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_strupr` import thunk |
| `0x49675a` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_atexit` import thunk |
| `0x496760` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_strncpy` import thunk |
| `0x496766` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_strlen` import thunk |
| `0x496784` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_strstr` import thunk |
| `0x49678a` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_strlwr` import thunk |
| `0x4967a2` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_strchr` import thunk |
| `0x4967a8` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_free` import thunk |
| `0x4967b4` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_fscanf` import thunk |
| `0x4967ba` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_memcmp` import thunk |
| `0x4967c0` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_strncat` import thunk |
| `0x4967ea` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_strcat` import thunk |
| `0x496820` | RTL | [rtl_imports.md](rtl_imports.md) | CW3220 `_itoa` import thunk |
| `0x4968b6` | platform | [dll_imports.md](dll_imports.md) | KERNEL32 `GlobalFree` import thunk |
| `0x4968d4` | platform | [dll_imports.md](dll_imports.md) | KERNEL32 `GetVersion` import thunk |
| `0x4968e6` | platform | [dll_imports.md](dll_imports.md) | KERNEL32 `SetLastError` import thunk |
| `0x4968f8` | platform | [dll_imports.md](dll_imports.md) | KERNEL32 `GetProcAddress` import thunk |
| `0x496904` | platform | [dll_imports.md](dll_imports.md) | KERNEL32 `LoadLibraryA` import thunk |
| `0x49690a` | platform | [dll_imports.md](dll_imports.md) | KERNEL32 `GlobalUnlock` import thunk |
| `0x49694c` | platform | [dll_imports.md](dll_imports.md) | USER32 `GetWindowRect` import thunk |
| `0x496952` | platform | [dll_imports.md](dll_imports.md) | USER32 `GetSystemMetrics` import thunk |
| `0x496970` | platform | [dll_imports.md](dll_imports.md) | USER32 `GetClientRect` import thunk |
| `0x496994` | platform | [dll_imports.md](dll_imports.md) | USER32 `ClipCursor` import thunk |
| `0x49699a` | platform | [dll_imports.md](dll_imports.md) | USER32 `ClientToScreen` import thunk |
| `0x4969e2` | platform | [dll_imports.md](dll_imports.md) | USER32 `SetCursorPos` import thunk |
| `0x496a5a` | platform | [dll_imports.md](dll_imports.md) | WINMM `mciSendCommandA` import thunk |
| `0x496a66` | platform | [dll_imports.md](dll_imports.md) | WINMM `joyGetDevCapsA` import thunk |
| `0x496a6c` | platform | [dll_imports.md](dll_imports.md) | WINMM `joyGetPosEx` import thunk |
| `0x40e234` | H4 | hook `equip_ctor` (group `weapon_ctors`) | `EQUIP` base constructor of the five equipment classes (`0x40df30`, vtable `0x498cdc`, `W+0x4c` ← 0, `W+0x77` ← −1) |
| `0x478a3c` | H4 | hook `thread_add` (group `object_shapes`) | adds a thread to the first free of ten slots `+0x1a`, then `track_sort` (hook) |
| `0x478e48` | H4 | hook `thread_ctor` (group `object_shapes`) | animation-thread constructor: owner `+0`, `track_clip` (hook), 32-byte state at `+0x2e` with the rates 4000h |
| `0x470ddc` | H4 | hook `cell_flag_set` (group `group_joins`) | cell flag write `[G+0xf0][y<<n + x]` (also the per-frame clear in `0x46e5a4`, H3) |
| `0x470e00` | H4 | hook `cell_flag_get` (hooked, accepts compare) | cell flag read `[G+0xf0][y<<n + x]` (also the cell-object draw `0x46e5a4`, H3) |
| `0x470d74` | H4 | hook `cell_mark_set` (group `terrain_load`) | cell write helper of the terrain load and the base marks |
| `0x40c7c4` | H4 | hook `collision_points` (group `collision_data`) | collision volume reader: count word and 8-byte points (`0x49667c`, stream slot `+0x18`) |
| `0x40cc14` | H4 | hook `collision_volume` (group `collision_data`) | collision volume header and points (`0x40c7c4`); also the weapon-list entry `0x40f8bc` |
| `0x40ccc8` | H4 | hook `collision_bounds` (group `collision_data`) | bounds pass: `0x40c5d0` for each volume, radius word `+0xe` |
| `0x40ce78` | H4 | hook `collision_match` (group `collision_data`) | volume match of the part-list scan (`0x40ce28` per 16-byte volume) |
| `0x40cff8` | H4 | hook `dmg_region_read` (group `damage_data`) | damage row reader (18 bytes, dword list, `+0xc` ← −1, `+0xe` ← 0); also `0x40f8bc` |
| `0x40cafc` | H4 | hook `col_part_gather` (group `damage_data`) | gathers the volume ids, point pointers and owner words of a collision record into three arrays (for `0x40cb84`) |
| `0x41ff68` | H4 | hook `remap_word` (group `type_loads`) | remaps a part word through a byte table (−1 stays) |
| `0x40feb4` | H4 | hook `gun_slot_base` (group `type_loads`) | running gun-list slot base `0x4a98e8` (returns the old value, adds n) |
| `0x40cbd4` | H4 | hook `col_slot_base` (group `type_loads`) | running collision slot base `0x4987dc` (returns the old value, adds n) |
| `0x40d200` | H4 | hook `dmg_slot_base` (group `type_loads`) | running damage slot base `0x498856` (returns the old value, adds n) |
| `0x40fea0` | H4 | hook `mount_entry` (group `fit_leaves`) | 6-byte entry of the table `[0x4a98e4]` for a type word (pure lookup; `0x40fee8`, `0x420634`) |
| `0x40fc50` | H4 | hook `gun_classes` (group `type_loads`) | copies the 26-byte gun entries' class words (−1 for class ≥ 4) into a word list |
| `0x40304c` | H4 | hook `node_list` (group `type_loads`) | pointer list of n shape nodes: `new[]`, each entry the shape virtual `+0x24(index)` or 0 for a negative index (`0x4201a8`) |
| `0x44d3cc` | H4 | — | records a map event for `map_areas` (`M+0x14d`, `+0x14e`, `+0x152`, `+0x15b`) and requests the map redraw ([cockpit.md](../reverse-engineering/cockpit.md)) |
| `0x44a748` | H4 | — | wingman selection: lamp gadgets, DialogPort message `0x22` with `0x4d03c8[i]`, `wingman_select` (hook) |
| `0x433134` | H4 | hook `wingman_index` ([cockpit.md](../reverse-engineering/cockpit.md#hdd-map-key-path)) | wingman index of an actor in `0x4d03c8` (0..2, else −1) |
| `0x44a174` | H4 | — | HDD button owner: records the pressed button's index at `+0x528` for `0x44a1a0` |
| `0x43166c` | H4 | hook `order_call` (group `mfd_orders`) | order call: `[A+0x203]` into the order record, group order `0x4231a4(group [0x49b0f8], record)` |
| `0x43f9c0` | H4 | hook `menu_code` (group `mfd_orders`) | order code of an MFD menu: entry `+0x32`, plus 3 when bit 1 of its flag byte is set |
| `0x43f9f8` | H4 | hook `menu_toggle` (group `mfd_orders`) | toggles bit 1 of the flag byte for menu entries 4 and 5, redraw byte `+0x36` |
| `0x4470a4` | H4 | hook `mfd_entry` (group `mfd_orders`) | MFD entry select `G+0xb1` |
| `0x446fb0` | H4 | hook `mfd_target` (group `mfd_orders`) | `cockpit_next` of the player (`[0x49b088]`) for MFD buttons 7/9 ([hud.md](../reverse-engineering/hud.md)) |
| `0x446fc4` | H4 | hook `mfd_view_next` (group `mfd_orders`) | cycles the counter `+0x318` modulo `+0x31c` |
| `0x446ff0` | H4 | hook `mfd_range` (group `mfd_orders`) | range cycle `G+0xc5` 0..2, `G+0xbd` ← `[0x4d1c70 + 4·i]` |
| `0x426514` | H4 | stays original | empty body (`ret`, 1 byte, bytes checked): called last by `mission_end`; not hookable (below the detour size) and not mirrored by the core (no effect) |
| `0x425344` | H4 | stays original | empty body (`ret`, 1 byte, bytes checked): called last by `mission_end`; not hookable (below the detour size) and not mirrored by the core (no effect) |
| `0x40ce28` | H4 | stays original; next candidate | the volume match of one volume: region word against the n weapon volumes, on a hit `0x40c834`; only caller `collision_match` |
| `0x40c834` | H4 | stays original; next candidate | copies a weapon volume's spheres, moved by an offset vector, into a `new[]` array of the matched volume; only caller `0x40ce28` |
| `0x40fa88` | platform | [asset_loading.md](../reverse-engineering/asset_loading.md) | the weapon shape cache with `0x40f998` (bridged by `actor_load`, H4-99); asset loading, not read from the bytes yet |
| `0x4952e8` | H3 | stays original | the height cache's setter (offset and scale words): called by `terrain_cells`, `terrain_materials` and per frame by the terrain draw |
| `0x4715e1` | H4 | stays original | the element constructor of the cell array: writes nothing; reached only through `0x49667c`'s element pointer |
| `0x470e20` | dead | — | a span fill of marks through `cell_mark_set`: no call site and no dword reference in the file |
| `0x41178d` | H4 | bridged through `0x49667c`; stays original | element constructor of the weapon-list entries: empty, returns its argument |
| `0x4207bb` | H4 | bridged through `0x49667c`; stays original | element constructor of the MECH forms: `0x49667c(form, 4, 7, 1, 0x492428)` in place |
| `0x423078` | H4 | bridged through `0x49667c`; stays original | element constructor of the FLYER forms: `0x49667c(form, 6, 3, 1, 0x4922d3)` in place |
| `0x420817` | H4 | stays original | the MECH type record's element constructor (H4-114 section); reached only through `0x49667c` |
| `0x42067c` | H4 | stays original | the MECH type record's element constructor (H4-114 section); reached only through `0x49667c` |
| `0x4207db` | H4 | stays original | an element constructor of the node-list array (H4-114 section); reached only through `0x49667c` |
| `0x4117a5` | H4 | stays original | an element constructor of the node-list array (H4-114 section); reached only through `0x49667c` |
| `0x423098` | H4 | stays original | an element constructor of the node-list array (H4-114 section); reached only through `0x49667c` |
| `0x4472bc` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#verdicts) | the MFD status-view list `+308h..+31Ch` (player, wingmen, count + 1), read by `mfd_view_next`, the page switch `0x446e60` and the per-frame draw `0x446350`; bridged by `order_setup` |
| `0x447014` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#verdicts) | the MFD radar range: index `+C5h` and value `+BDh` = `[4D1C70h + 4i]` (read by the radar filter `0x447284`); bridged by `mfd_range` |
| `0x43f9e0` | H4 | hook `menu_select` (group `mfd_keys`; [cockpit.md](../reverse-engineering/cockpit.md#mfd-keys)) | menu entry select: word `+37h` = the old entry, `[menu+32h]` = k; only caller `0x446c38` |
| `0x447158` | H4 | hook `mfd_key_select` (group `mfd_keys`; [cockpit.md](../reverse-engineering/cockpit.md#mfd-keys)) | MFD selection after an Alt order or a key: `mfd_entry(G, k)` and byte `G+E6h` when k is `[G+FBh]` on page 1 |
| `0x44703c` | H4 | hook `mfd_entry_next` (group `mfd_keys`; [cockpit.md](../reverse-engineering/cockpit.md#mfd-keys)) | next enabled menu entry (flag bit 0 clear, wrapping at 6), `mfd_entry` |
| `0x447070` | H4 | hook `mfd_entry_prev` (group `mfd_keys`; [cockpit.md](../reverse-engineering/cockpit.md#mfd-keys)) | previous enabled menu entry (wrapping below 0 to 5), `mfd_entry` |
| `0x447658` | H4 | next candidate | slot +8 of the `MFDSelectGadget` table `0x49cee0`: a left click (bit 0) goes to the owner's `mfd_click(W+24h, W, 4D1DE3h)`; needs the original stack coordinate like `mfd_click` |
| `0x406a5c` | H4 | bridged through `0x49667c`; stays original | element constructor of the building forms: empty, returns its argument |
| `0x406a64` | H4 | bridged through `0x49667c`; stays original | element constructor of the form elements: empty, returns its argument |
| `0x406a6c` | H4 | bridged through `0x49667c`; stays original | element constructor of the `lc_wpns` records: empty (EAX = the argument, EDX = +8) |
| `0x406a77` | H4 | bridged through `0x49667c`; stays original | element constructor of the building types: empty (EAX = the argument, EDX = +0Ah, ECX = +18h) |
| `0x406a85` | H4 | bridged through `0x49667c`; stays original | element constructor of the type children: empty (EAX = the argument, EDX = +0Ah, ECX = +10h) |
| `0x411df8` | H4 | hook `actor_list_init` (group `mission_world`) | the actor list `(list, count)`: `new[](4 × count)` and the capacity word; only caller the world load |
| `0x40fecc` | H4 | hook `gun_slot_table` | the gun-list table `[0x4a98e4]` = `new[](6 × word 0x4a98e8)`; only caller the world load |
| `0x40d218` | H4 | hook `dmg_slot_table` | the damage slot table `[0x4a9828]` = `new[](12 × word 0x498856)` for a nonzero word; only caller the world load |
| `0x40cbec` | H4 | hook `col_slot_table` | the collision slot table `[0x4a9824]` = `new[](6 × word 0x4987dc)` for a nonzero word; only caller the world load |
| `0x4539a0` | platform | [mission_load.md](../reverse-engineering/mission_load.md) section 29 | the STR string-pointer splitter (count = the body's word 0, a pointer per string through `0x453918`); also the HUD and dialog text loaders; bridged by `world_load` |
| `0x427207` | H4 | bridged through `0x49667c`; stays original | element constructor of the timers: empty, returns its argument; reached only through `0x49667c` |
| `0x401fe4` | H3 | [Audit of the fire and animation-manager methods](function_verdicts.md#audit-of-the-fire-and-animation-manager-methods) | the lazy object matrix (cdecl O): `euler_matrix` once per object (flag word O+32h), then the view transform of the render context `0x48c444`; seven call sites, called by the draw slot `0x46b8ea` among them; stays original with the draw side |
| `0x46b8ea` | H3 | — | slot `+00` (draw) of five class tables: `0x401fe4`, then the shape's draw slot; no state of its own, a hook would only forward both calls |
| `0x40c8be` | H4 | bridged through `0x49667c`; stays original | element constructor that the collision readers pass to `0x49667c`: returns the element; reached only through the element pointer |
| `0x40cf04` | H4 | bridged through `0x49667c`; stays original | element constructor as `0x40c8be`, EDX = element + 8 |
| `0x41ff00` | H4 | computed in core | the type-index byte leaf of MECH; no longer called when `mech_ctor` and the H4-114 hooks are replaced |
| `0x422e80` | H4 | computed in core | the type-index byte leaf of FLYER; no longer called when `flyer_ctor` and the H4-114 hooks are replaced |
| `0x430b34` | H3 | [presentation.md](../reverse-engineering/presentation.md) | palette fade step of the mission-start sequence (dead caller `0x45dc50`); reads the clock at `0x430b3a`; stays original |
| `0x4361cc` | H3 | [hud.md](../reverse-engineering/hud.md#classification-table) | DialogPort and EventPort frames (message queue, port draw slot, clock), reached by the render root's virtual `+0x10` calls at `0x45fde0`/`0x45fdf2`; stays a guest call |
| `0x45266c` | H3 | — | slider set value (class-table slot `+0x14` of `0x49d12c`/`0x49d19c`/`0x49d170`): clamps into `+0x24..+0x26`, knob word `+0x28`, forwards to slot `+8`; widget fields only; stays original |
| `0x452584` | H3 | — | the same slot `+0x14` of the table `0x49d0e8`; widget fields only; stays original |
| `0x45f13c` | H3 | — | cockpit widget `[H+0x1e9]` virtual `+4` after a window toggle (called by `0x4667c8`) |
| `0x4649d8` | H3 | — | graphics-driver page origin (`[0x4a57e8]` → `0x48990e`): present source point `0x4d3018` and the renderer's pixel/row base |
| `0x464d44` | platform | — | DIB section and memory DC of the 8-bit frame buffer (window creation) |
| `0x46658c` | platform | — | DirectDraw create and cooperative level (never live in the oracle: GDI branch) |
| `0x4665c4` | platform | — | DirectDraw display mode and surfaces (never live in the oracle: GDI branch) |
| `0x47a4a0` | platform | reproduced inside the hooks | registry lookup of the persistent-object loader (tag to class entry), reproduced inside `dts_load`; its other callers keep the original |
| `0x47a8a4` | platform | reproduced inside the hooks | base destructor of the VOL-aware stream (counter -1), reproduced inside `stream_destroy`; its other callers keep the original |
| `0x48156c` | platform | reproduced inside the hooks | constructor of a loader object (bitmap side), reproduced inside the V-09 factories; its other callers keep the original |
| `0x482f38` | platform | reproduced inside the hooks | constructor of a loader object (bitmap-array side), reproduced inside the V-09 factories; its other callers keep the original |
| `0x49545b` | RTL | — | the 5-instruction leaf of the vector constructor `0x4953c4`, empty (returns the argument); stays a guest call |
| `0x479cfc` | platform | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | registers an abort-notify callback (`new(8)`, linked into the list `0x6c56d8`); the root passes `0x461304` (the tape and dump close); process shell |
| `0x4955c4` | platform | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | only with the `-m` byte `0x4d267d`: `0x49571c(0x6cbc50, 0x20)`, a system call through the thunk `0x4968f2` on the record `0x6cbc50`; role not identified (no oracle suite sets `-m`) |
| `0x463370` | H4 | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | empty body (`ret`, 1 byte, bytes checked), stays original: called once by the root; not hookable (below the detour size); the core calls it through the bridge like the original |
| `0x4913dc` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | one registration through the persistent-class registry `0x47a0b8` (`0x4a687c`, `[0x4a68ac]`), next to `0x48dd08`'s; graphics library |
| `0x467840` | platform | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | the limiter's reference: the words `0x4d3b64..0x4d3b72`, `0x4d3b6c`, and one clock read (`0x4968ec`) into both `0x4d3b74` and `0x4d3b78`; timing, limiter `0x4678c0` |
| `0x4282d8` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | bins one terrain object: its virtual `+10h`, then the binning step `0x42837c` (H3); called by the root per entry of `[0x4aaa78]` and by `0x42841c` |
| `0x48bd84` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | camera-like object constructor of the graphics library (`0x48bc40`, vtable `0x4a5b29`, construction count); the root builds one per member of the player's group and overwrites the vtable with `0x49f8e8` |
| `0x48bda4` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | camera constructor of the graphics library (`0x48bc70`, vtable `0x4a5b29`, fields `+16h`, `+1Ah`, `+1Eh`); the root's main camera, also the MFD pages |
| `0x465618` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | stores the cursor sprite at `0x4d3724` (presentation); the root calls it below 640 pixels |
| `0x46313c` | platform | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | starts the CD track `[0x49f8fc]` through `0x464858` (MCI) when the sound context `[0x49f8ec]`, the track `[0x49f8fc]` and `[0x49f8f4]` are nonzero; the first MCI call of the load (the old arming point); audio |
| `0x42eb40` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | empty body (`push ebp; mov ebp,esp; pop ebp; ret`, 5 bytes): a view callback of the horizon code (also called by `0x42e700` and `0x45d548`) |
| `0x45d548` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | prepares the intro view when the view word `+54h` is set ([display_modes.md](../reverse-engineering/display_modes.md)); the root calls it instead of the first render |
| `0x4328cc` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | arms the cockpit start sequence: `H+241h` <- the clock `0x467828` unless `H+245h`, sound `0x13`, for a flyer chassis sound `0x2d` and `0x463114(0x2d, 0xa410)` ([hud.md](../reverse-engineering/hud.md)); coupling: one clock read, `sfx_play` calls |
| `0x433f7c` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | one HUD step of the cockpit `[0x49b088]` (`+226h`/`+232h`/`+236h`, `0x48992e`); also called by `0x42a9c0` |
| `0x45d85c` | H3 | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | the mission-start drop sequence ([display_modes.md](../reverse-engineering/display_modes.md#0x45d85c-mission-start-drop-sequence)), when the view word `+54h` is set; coupling: stream-B draws per pass |
| `0x477abc` | platform | [mission_load.md](../reverse-engineering/mission_load.md#30-the-mission-load-root) | flushes the key ring of a device (`[0x6bb8b4]` <- `[0x6bb8b0]`, clears bit 1 of the device word); input layer ([input.md](../reverse-engineering/input.md)) |
| `0x435298` | H3 | [hud.md](../reverse-engineering/hud.md#the-hud-line-contract--coupling-set-and-frame-writes) | message queue: adds the pause length to the future start/end clocks; reads the clock once per call (coupling) |
| `0x435b58` | H3 | [hud.md](../reverse-engineering/hud.md#the-hud-line-contract--coupling-set-and-frame-writes) | DialogPort pause: the first pause stores the clock (`+0x4cc`), depth `+0x4d0` + 1 (coupling) |
| `0x438d6c` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#gauges-mfd-hdd-and-the-radio-panel-in-the-hud-line) | random in [lo, hi) for the gauge blink: one stream-B draw unless lo = hi (coupling) |
| `0x438ddc` | H3 | [hud.md](../reverse-engineering/hud.md#the-hud-line-contract--coupling-set-and-frame-writes) | power-on start of a gauge: `+0x8c` ← 1, `+0x90` ← the clock (coupling) |
| `0x438e2c` | H3 | [hud.md](../reverse-engineering/hud.md#the-hud-line-contract--coupling-set-and-frame-writes) | UI click callback of 15 widget tables: `sfx_play(0x11)` (coupling) |
| `0x43e114` | H3 | [hud.md](../reverse-engineering/hud.md#the-hud-line-contract--coupling-set-and-frame-writes) | HUD radar: forwards to `0x43f2d8` with the MFD's radar object |
| `0x43eedc` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#gauges-mfd-hdd-and-the-radio-panel-in-the-hud-line) | `MFDRadar` slot `+4`: blip scan `0x43ec08` (clock per blinking blip), then the radar draw slot 0 |
| `0x440eac` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#gauges-mfd-hdd-and-the-radio-panel-in-the-hud-line) | `EnergyWeaponGauge` power-on sweep: one clock read per draw until done (coupling) |
| `0x441da0` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#gauges-mfd-hdd-and-the-radio-panel-in-the-hud-line) | `EnergyPodGauge` slot 2: forwards to the pod gauge draw `0x441878` |
| `0x441db0` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#gauges-mfd-hdd-and-the-radio-panel-in-the-hud-line) | `EnergyPodGauge` slot `+0x1c`: power-on sweep, one clock read per draw until done (coupling) |
| `0x443444` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#gauges-mfd-hdd-and-the-radio-panel-in-the-hud-line) | `TogglePodGauge`/`PodWeaponGauge` slot `+0x1c`: sweep done at once (`+0x8d` ← 1) |
| `0x4437cc` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#gauges-mfd-hdd-and-the-radio-panel-in-the-hud-line) | `ShieldsGauge` power-on sweep: one clock read per draw until done (coupling) |
| `0x4421c8` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#gauges-mfd-hdd-and-the-radio-panel-in-the-hud-line) | `WeaponRangeGauge` key method (slot 3): codes `0x26`/`0x29` press button 1/0 through `button_press` |
| `0x4419a0` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#the-widget-slots-classification) | pod gauges slot `+0x18` (H4-124's widget getter): EAX = G + `0xc2` |
| `0x4419ec` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#the-widget-slots-classification) | `TogglePodGauge` slot `+0x14`: takes the 8-byte record into `G+0xc2`, redraw request |
| `0x441970` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#the-widget-slots-classification) | `PodWeaponGauge` slot `+0x14`: takes the record when byte 1 changed, redraw request |
| `0x441bac` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#the-widget-slots-classification) | `EnergyPodGauge` slot `+0x14`: takes the record (and the sweep target `+0xe2` before the sweep is done), redraw request |
| `0x44141c` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#the-widget-slots-classification) | `ProjectileWeaponGauge` slot `+0x10`: takes the 23-byte gauge record of `gun_sync`, button states, redraw |
| `0x44343f` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#the-widget-slots-classification) | pod gauges slot `+0x10`: empty body |
| `0x440e9c` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#the-widget-slots-classification) | gauge-record getter of `beam_sync`: EAX = G + `0xc2` |
| `0x4414cc` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#the-widget-slots-classification) | gauge-record getter of `gun_sync`: EAX = G + `0xc2` |
| `0x441a24` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#the-widget-slots-classification) | `TogglePodGauge` click: button 0 toggles byte `G+0xc2`, the toggle the H4-124 syncs read |
| `0x441c04` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#the-widget-slots-classification) | `EnergyPodGauge` click: button 0 toggles byte `G+0xc2`, the toggle the H4-124 syncs read |
| `0x4632c4` | platform | [cockpit.md](../reverse-engineering/cockpit.md#gauges-mfd-hdd-and-the-radio-panel-in-the-hud-line) | the lip queue per frame (from the radio panel `0x44b620`): one clock read, `sfx_lip_due` for a due entry (audio; coupling) |
| `0x428bc0` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-terrain-pass-in-the-frame) | the five draw distances `0x4cf988[k]` = `multiply_q10(far, word 0x49abb0[k])` (from `0x46cb9c`; read by `0x428c08`) |
| `0x4281b0` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-terrain-pass-in-the-frame) | bucket index of a cell: linear search of the table `0x4cf4f0` (count `0x4cf874`), a new cell appended; 150 cells → fatal report `0x479c78` |
| `0x4282f8` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-terrain-pass-in-the-frame) | bins an object: into its cell's bucket `0x4aab00` (count from the cell record) or, without a cell, the far list `0x4cf88c`/`0x4cf92c` |
| `0x46e8c4` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-terrain-pass-in-the-frame) | cell record `0x6b5058` {column, row, count} of a cell, incrementing its mark (`0x470db8`); 0 outside the grid |
| `0x46e944` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-terrain-pass-in-the-frame) | clears the whole cell-mark array `[G+0xf0]` at the start of every binning |
| `0x470db8` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-terrain-pass-in-the-frame) | increments a cell's mark byte and returns it (binning) |
| `0x470d94` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-terrain-pass-in-the-frame) | reads a cell's mark byte (the eye cell in `0x46d1a8`; the load-time shading `0x46c3f0`) |
| `0x401078` | H4 | hook `camera_attached` ([camera.md](../reverse-engineering/camera.md#the-hooks-group-camera_view)) | draw cull: 1 when the camera is attached (mode 2) to the object (the own vehicle in the cockpit view); a pure reader with drawing callers, hooked with the camera group |
| `0x428c08` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-terrain-pass-in-the-frame) | distance cull of a cell object against the draw distance of its type (`0x4cf988`) |
| `0x428eec` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-terrain-pass-in-the-frame) | stores the camera for the sort tree (`0x49ac3c`) |
| `0x42ebe8` | H3 | [horizon.md](../reverse-engineering/horizon.md#place-in-the-frame) | forwards to the horizon painter `0x42ec08` with the vertical offset `[H+0x30]` |
| `0x408240` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-terrain-pass-in-the-frame) | binning test of an explosion: skips type bytes 2 and `0xb..0xe` and one whose parent is the attached camera's target |
| `0x408228` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-terrain-pass-in-the-frame) | the parent's cell words (`[obj+0x57]+0x1e8`) of an explosion, 0 without a parent (binning) |
| `0x46b850` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-terrain-pass-in-the-frame) | the parent's cell words (`[obj+0x4a]+0x1e8`) of a fire (binning) |
| `0x484222` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-renderer-line-r3d) | GL polygon fill entry: reverses the point order, then by the context's fill record `0x483eb8` |
| `0x48c5fc` | H3 | [camera.md](../reverse-engineering/camera.md#projection) | perspective projection of a view-space point by the focal shift `[0x6c6028]` (or the divisor `[0x6c6030]`) |
| `0x48c6d0` | H3 | [camera.md](../reverse-engineering/camera.md#projection) | adds the projection offset `0x6c6034`/`0x6c6038` |
| `0x48c5cc` | H3 | [camera.md](../reverse-engineering/camera.md#projection) | projected size: size << `[0x6c6028]` / depth |
| `0x48be50` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-renderer-line-r3d) | adds a light to the active list `0x6c60a8`/`0x6c60ac` (at most 10, no duplicates) |
| `0x48be98` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-renderer-line-r3d) | removes a light from the active list `0x6c60a8`/`0x6c60ac` |
| `0x407500` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-renderer-line-r3d) | releases a light object: off the active list, onto the free list of its kind (`L+0x35c`/`+0x360` or `L+0x3b0`/`+0x3b4`) |
| `0x471f30` | H3 | [terrain.md](../reverse-engineering/terrain.md#the-terrain-pass-in-the-frame) | walker record set-up: callback `+0x14`, grid `+0x18` |
| `0x493141` | H3 | [rasterizer.md](../reverse-engineering/rasterizer.md#the-renderer-line-r3d) | second step of the scan converter `0x4930c1` (into the span walk `0x493192`), bridged by `poly_spans` |
| `0x4282a0` | dead | [rasterizer.md](../reverse-engineering/rasterizer.md#the-renderer-line-r3d) | object-pick request (point → `0x4cf931`, byte `0x4cf930` ← 1); no caller, no reference |
| `0x428960` | dead | [rasterizer.md](../reverse-engineering/rasterizer.md#the-renderer-line-r3d) | second pick (screen rectangle of an object, byte `0x49abbc`); no caller, no reference |
| `0x42f700` | dead | [horizon.md](../reverse-engineering/horizon.md#the-texpoly-class-in-this-range-is-dead) | `TexPoly` constructor; no caller, no reference, so the vtable `0x49afe6` is never installed |
| `0x42f860` | H3 | [horizon.md](../reverse-engineering/horizon.md#the-texpoly-class-in-this-range-is-dead) | `TexPoly` slot `+0x24` with a stream-B draw (`0x42f874`); unreachable: the class is never constructed |
| `0x44d370` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#hdd-map-frame) | the map frame's 30-tick tick: bytes `0x49d6ac` (button blink) and `0x49d6ae` (map redraw) set, `0x49d6ad` toggled, redraw request of the gadget `[[0x49d6b0]+8]`, the label timer word `[[0x49d6b0]+0x13e]` counted down (at 0 the label redraw byte `+0x144`); no clock, B or sound |
| `0x44f644` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#hdd-map-frame) | wingman-button blink: slot 0 of the button's table `[B+0x17]` unless byte `B+0x37` is set |
| `0x44dd74` | H3 | [cockpit.md](../reverse-engineering/cockpit.md#hdd-map-frame) | mode-button bitmap: button i's position less the page shift words `0x4d253a`/`0x4d253c`, bitmap 4 or the state byte + 2 of `[[0x49d23c]+8]`, blit `0x45c8bc` into the view `0x4d24bc` |
| `0x44ee48` | H4 | hook `mode_next` ([cockpit.md](../reverse-engineering/cockpit.md#hdd-map-key-path)) | next map mode (0Ah..11h, cyclic) through `map_mode`, map gadget redraw request; never reached in the 40 suites |
| `0x44ee88` | H4 | hook `mode_prev` ([cockpit.md](../reverse-engineering/cockpit.md#hdd-map-key-path)) | previous map mode (11h..0Ah, cyclic) through `map_mode`, map gadget redraw request; never reached in the 40 suites |
| `0x44ef08` | H4 | hook `target_cycle1` ([cockpit.md](../reverse-engineering/cockpit.md#hdd-map-key-path)) | `0x44ef30(M, 1)` (map key Tab in mode 0Bh) |
| `0x44ef1c` | H4 | hook `target_cycle0` ([cockpit.md](../reverse-engineering/cockpit.md#hdd-map-key-path)) | `0x44ef30(M, 0)` (map key Tab in mode 0Ch) |
| `0x44ef30` | H4 | hook `target_cycle` ([cockpit.md](../reverse-engineering/cockpit.md#hdd-map-key-path)) | next actor of a side after the current target in the actor list `0x4a9af8` that `0x44f03c` accepts (wraps once) |
| `0x46bbc1` | H4 | bridged through `0x49667c`; stays original | element constructor of the explosion and fire bitmap pairs (8 bytes): empty, returns its argument |
| `0x401c20` | H4 | hook `camera_turn_step` ([camera.md](../reverse-engineering/camera.md#the-hooks-group-camera_view)) | `approach_word(p, multiply_q8(0x800, w), 0xc0)`: the turn and pitch deltas of the outside camera |
| `0x401c4c` | H4 | hook `camera_speed_step` ([camera.md](../reverse-engineering/camera.md#the-hooks-group-camera_view)) | `approach_word(p, multiply_q8(0x400, w), 0x50)`: the step word of the outside camera |
| `0x40109c` | H4 | hook `camera_orbit_set` ([camera.md](../reverse-engineering/camera.md#the-hooks-group-camera_view)) | camera orbit setter: unless `+0x4a`, orbit offset `+0x38`, target `+0x32`, orbit angles, distance, deltas and step cleared, mode 1 |
| `0x40111c` | H4 | hook `camera_attach_set` ([camera.md](../reverse-engineering/camera.md#the-hooks-group-camera_view)) | camera attachment setter: unless `+0x4a`, attachment offset `+0x3e`, target `+0x32`, mode 2 |
| `0x45edf8` | H4 | hook `camera_current` ([camera.md](../reverse-engineering/camera.md#the-hooks-group-camera_view)) | getter of the camera `[0x4d24ea]` (callers `0x42a4c4`, `0x43ec08`, `0x44060c`, `0x45030c`: drawing); hooked with the camera group |
| `0x462af0` | platform | [audio.md](../reverse-engineering/audio.md#the-sound-api-functions); own code: service row `sfx_playing` (`sim/dispatch/services/audio.h`) | SFX is-playing (cdecl, index i): 0 when the driver context `[0x49f8ec]` is 0 or the handle `[0x4d2c78+4i]` is -1, else 1 while `0x4641e8` (`sosDIGISampleDone`) answers 0 (listing `0x462af0..0x462b2d`); a guest call of `twist_sound_latch`/`pitch_sound_latch` |

## Evidence for single verdicts

Verdicts whose reasoning does not fit a table cell. Rows of the Verdicts table link here.

### `0x4469e8`: `MFDGauge` key handler

- **Reached:** no direct caller (map kind "pointer"); the dword `0x4469e8`
  sits at `0x49cfac`, slot `+0xc` of the vtable at `0x49cfa0` whose type
  descriptor `0x447711` (at `0x49cf94`) names `MFDGauge`. The HUD key
  handler `0x432bc8` offers an unhandled key code to each panel object's
  slot `+0xc` in turn (`0x432fcc` … `0x4330f8`; the MFD is `[H+0x1ed]`,
  `0x4330aa`). Counts 0/118/0/721/118/59.
- **Contract (cdecl, G = MFD gadget, code = word):** returns AL = 0 when
  `panel_visible(G+0xeb)` (hook) is false (`0x446a04`). Code `0x20` (D),
  `0x213`, `0x214`, `0x2d` (X): if the per-mode enable byte
  `0x49cbdf + 13·[G+0xc1] + k` is set, `0x438d9c(G, 7/8/9/10, 1)` (press
  MFD button); `0x213` with the byte clear calls `0x446ff0` (range cycle
  `G+0xc5` 0..2, `G+0xbd` ← `[0x4d1c70 + 4·i]`). In MFD mode 1: `0x34`/`0x33`
  step the selected entry `G+0xb1` forwards/backwards over the six entries
  not disabled in `[G+0xd1]+0x2c` (`0x44703c`/`0x447070`), and the jump
  table `0x446b78`/`0x446b95` maps codes `0x12..0x2e` to entries 0..5
  (`0x447158` → `G+0xb1`, `G+0xe6` ← 1); redraw bytes `G+0xe5+[G+0xc1]` ← 1.
  No absolute-address write, no clock or RNG call of its own.
- **Effect on compared state:** only through the original `0x438d9c`, which
  calls the button owner `0x446844` (button 7/9 in a mode ≠ 0 → `0x446fb0`
  → `cockpit_next`; button 10 → `0x447248`, the DialogPort message of the
  selected entry; `0x4530bc` calls the clock). `0x4469e8`'s own writes are
  MFD gadget fields.
- **Verdict H4, hooked** (`mfd_key`, group `mfd_keys`; see
  [MFD keys](#mfd-keys)): the key method presses the buttons whose owner
  issues the orders, so it belongs to the order path. It is the MFD branch
  of the HUD key handler `0x432bc8` (hook `hud_key`).

### `0x48ca70` and `0x48cf20`: projection and near-plane clip

- **Reached:** `0x48ca70` from the tracer draw slot `0x40bc14` and the
  `TexPoly` draw slot `0x42fc34` ([rasterizer.md](../reverse-engineering/rasterizer.md#callers-and-roots));
  `0x48cf20` from `0x40bc14`, the flat terrain variant `0x46bc44` and the
  DTS polygon draw methods `0x474ec0`, `0x475538`, `0x475cd0`, `0x475d8c`.
  Counts 0/0/1000/0/0/315 and 714/1000/1000/1000/437/569.
- **`0x48ca70`:** for each index of the current polygon in the render
  context `0x6c60d4` (count `+0x810`, index list `+0x81e`/`+0x812`) whose
  vertex state byte `+0x826` is 0: transforms the vertex with the camera
  matrix `0x6c5fb4` (`0x48d9e4` → `transform_point`), projects it with the
  focal shift `[0x6c6028]` or divisor `[0x6c6030]` and the screen centre
  `[0x6c6034]`/`[0x6c6038]` into `+0x3aee`, marks it 2 (or 1 when behind
  the near plane `[0x6c602c]`), and copies the screen points into `+0x5a2e`
  with the count `+0x5b2e`; returns 1 when a vertex is behind the plane.
- **`0x48cf20`:** walks the polygon's edges and builds the clipped screen
  polygon in `+0x5a2e`: projected points pass through, an edge crossing the
  near plane gets an interpolated point projected by `0x48d9cf`.
- **Verdict H3:** both write only the render context (`0x6c60d4` block,
  `0x6c6054`/`0x6c6058`) like their sibling `0x48c954`; every caller is a
  draw method.

## How verdicts are derived

Every verdict is a hypothesis for the hook that implements it to confirm from the bytes; the contracts live in the
subsystem documents linked per row. The evidence base is an `llvm-objdump -d -M intel` listing of the reference
`VER95/DBSIM.EXE` (SHA-256 `8561aa47…c4ad`, image base `0x400000`), re-decoded from the entry where the linear
sweep is misaligned (padding before `enter` and `push` prologues, inline jump tables), the vtable and RTTI bytes of
the image, the DBSIM maps (`<es2-data>\logs\dbsim-map-20261008\map.json`, `dbsim-map-20261010`, and
`dbsim-map-20261012` with 40 suites, every function start probed and the load counted: counts, callers, callees) and
the hook registry.

**Rules by area.**

- **Loading.** File and asset loading is platform: the persistent-object factories and read methods (`TS*`, `AN*`,
  `GL*`, `GridShape`; the factory is the second dword of a registry entry tag, factory, name), shape-list and
  palette loads (`libio.cpp`), load-by-name wrappers, the path builder `0x492bec` and the whole-file readers of the
  OVL tables. Loaders that build **simulation tables** from the records (type stats, collision volumes and radii,
  damage rows, part tables, forms, weapon and projectile lists, pools, terrain cells and heights) are H4, with their
  stream reads and shape loads as guest calls. This puts the stage-2 callbacks that create pools or type records
  next to the pool-only callbacks.
- **Cockpit and HUD.** Construction and drawing of panels, gauges, gadgets, message ports, alerts, the HUD, the
  radar, the MFD and HDD pages are H3 (mission_load.md section 10 "not H4: cockpit, HUD"), also where a constructor
  calls a hook (`gauge_mode`) or initialises a gadget field that an H4 hook reads later (throttle gauge): the
  after-load snapshot of the load hooks covers those fields. The exceptions are the paths that issue orders or
  selections into the simulation: the [MFD order path](#mfd-order-path) (including the load-time set-up `0x431530`
  of the order group and wingman table) and the HDD map key path with the wingman selection (hooks `map_key`,
  `map_press`, `wingman_pick`, `wingman_index`; [cockpit.md](../reverse-engineering/cockpit.md#hdd-map-key-path)).
- **Graphics and terrain shading.** The rasterizer, the graphics library (surfaces, viewport, lights, render
  context, DTS draw helpers), the terrain shading pass `0x46c2e0` (cell normals and shade bytes; the simulation's
  `segment` hook computes its own normal from the heights) and the world loader `0x42e010` (WLD words read only by
  the horizon and the damage palette, [horizon.md](../reverse-engineering/horizon.md)) are H3. The flat-object pool
  `0x4097a8` that the world loader calls is H4.
- **Input and audio.** Device objects, the mouse and keyboard handlers of the MainWndProc list, the prefs and
  joystick configuration and the SOS and MIDI wrappers are platform ([input.md](../reverse-engineering/input.md),
  [audio.md](../reverse-engineering/audio.md)). The simulation-visible part of the SFX manager (the stream-B draw,
  the clock reads, the volume and pan arithmetic, the lip queue) is own code (group `sfx`); the sound driver behind
  it stays original.
- **Message ports.** The cockpit and radio message posts build an event record and call the port; the port stays
  original and is a guest call of the hook, the record content is simulation state. The port frames (DialogPort,
  EventPort: message queue, port draw slot, clock) stay with the presentation side.

**Methods.**

- **Control flow.** Per function a script walked the control flow from the entry (jump tables read from the file)
  and listed direct calls with the callee's verdict or hook name, slot calls, absolute-address reads and writes,
  string and code immediates, the dword references in the data sections and the callers. Import thunks were
  resolved through the import directory.
- **Vtable slots by RTTI.** For each function the image was searched for its address as a dword; a hit in DATA was
  resolved to its vtable by the Borland type descriptor at `vtable − 12` (name at descriptor + word `+6`), as in
  [vtables.md](../reverse-engineering/vtables.md) 1.1. This identified the destructors (slot `+0x8` of `EXPLOSION`,
  `FIRE`, `DEBRIS`, `FlatObj`, `METEOR`, `BULLET`, `ROCKET`, `BEAM`, `LightSource`), the draw slots (`+0` of the
  `BASE` family and `DEBRIS`; `+0x1c` of `CONFIG_PART`, the eleven weapon and pod classes and the
  `TS*Part`/`TS*Poly` classes), the read slots of `BMP_8`/`GLBitmap`/`GLBitmapArray`, the HUD gadget and port slots
  and the `Alert` classes. Registry entries are found by the name pointer after the factory.
- **Writes and their readers.** Every absolute-address write of a function was listed and each written global
  searched for readers in the listing: the light list `0x6c60a8`/`0x6c60ac` (polygon lighting only); the detail
  words `0x4a95b4`, `0x4a9dc4`, `0x497368`, `0x49736c`, `0x4a9868` (draw slots only); the MFD state
  `0x49cbc4..0x49cbc8` (MFD code only); the WLD words `0x4cfcc4..0x4cfd04` (horizon and damage palette only); the
  terrain detail words `0x4aaaa8`/`0x4aaaac` (terrain draws only); the terrain pointer `0x4a0be0` (read by the
  terrain and movement hooks); the lever word `0x49a06e` (read by vehicle control `0x4160f8..0x4161f5`, also written
  by the key paths at `0x459d57` and `0x4607fc`, hence H4 for the throttle slider). Object-field writes were judged
  from the subsystem documents (cockpit.md: the HDD map's order records become wingman orders; hud.md: the HUD
  letter codes).
- **Bridged calls.** The adapter headers say per group which counted callees are real guest calls and which are
  computed in core; only the former get rows. The four `formation` slot helpers and the shield step `0x413af8` are
  computed in core and only counted. Every original function that a hook still calls through the guest bridge (the
  `call_<va>` counters of the adapters) has a row whose reason names the bridging hooks; the generated list is
  [hook_frontier.md](hook_frontier.md#bridged-call-inventory). Evidence for those rows: the import table of the image
  (thunk targets), the adapter comments and `core/src` (which counters are real guest calls), the counters of
  `oracle-parallel-391hooks-20261010T223840Z` and `check-r7-2b`.
- **Slot dispatch.** `0x41903a` and `0x46a685` call slot `+0x68` of an actor, not of a weapon: the only dispatch to
  the weapon damage bodies is `0x417fdf` in `vehicle_part` ([weapons.md](../reverse-engineering/weapons.md)).

**Row conventions.** A row stays in the table as long as its function is original or computed in core; the script
prints classified entries that have become hooks so stale rows can be pruned. A regenerated list that shows an open
candidate needs a new row. The column order (VA, verdict, package or document, reason) and the heading
`## Verdicts` are parsed by the scripts. A hook in the package column is written as ``hook `name` `` with its group.
Path verdicts inside hooked functions are no rows: see
[Dead and defensive paths](#dead-and-defensive-paths-of-hooked-functions).

## Presentation and palette functions

Functions of the present and palette path; contracts in
[presentation.md](../reverse-engineering/presentation.md). Evidence: the `llvm-objdump -d -M intel` listing, the
import table and `dbsim-map-20261010`.

| VA | Verdict | Package or document | Reason |
|---|---|---|---|
| `0x4306ac` | H3 | [presentation.md](../reverse-engineering/presentation.md#keep-list-for-a-presenter) | palette cycling of up to five ranges (presentation frame, 1000×5/936); **reads the clock** at `0x4306b6` every call, so it stays original (intra-frame clock) |
| `0x430b34` | H3 | [presentation.md](../reverse-engineering/presentation.md) | palette fade step (mission-start sequence, dead `0x45dc50`); reads the clock at `0x430b3a`; stays original |
| `0x4649d8` | H3 | [presentation.md](../reverse-engineering/presentation.md) | graphics-driver page origin (`[0x4a57e8]` → `0x48990e`): present source point `0x4d3018` and the renderer's pixel/row base; 0/0/0/1000/0/0 |
| `0x48a1d4` | H3 | [presentation.md](../reverse-engineering/presentation.md) | graphics-driver init (`[0x4a57ac]` descriptor `0x4a586c`): present rectangle `0x4d2ff8`, source point, row table |
| `0x464d44` | platform | [presentation.md](../reverse-engineering/presentation.md) | DIB section and memory DC of the 8-bit frame buffer (window creation) |
| `0x46658c`, `0x4665c4`, `0x466730` | platform | [presentation.md](../reverse-engineering/presentation.md) | DirectDraw create/cooperative level, display mode and surfaces, teardown (never live in the oracle: GDI branch) |
| `0x4667c8` | platform | [presentation.md](../reverse-engineering/presentation.md) | window/full-screen toggle (writes `[0x4d255e]`, calls `0x45f13c`); keep its call sites |
| `0x45f13c` | H3 | [presentation.md](../reverse-engineering/presentation.md) | cockpit widget `[H+0x1e9]` virtual `+4` after a toggle (not followed) |

## Dead and defensive paths of hooked functions

Path verdicts inside functions that are already hooks, so they stay out of the [Verdicts](#verdicts) table (which
`hook_candidates.py --classified` reads). Each table lists never-reached sites that no input can reach in the retail
binary, read from an `llvm-objdump -d -M intel` listing of `VER95/DBSIM.EXE` (SHA-256 `8561aa47…c4ad`) and raw scans
of the file for dword references; the PROVED candidates are listed in
[live_coverage.md](live_coverage.md#never-reached-sites).

### Dead branch paths

Never-reached sites of the 370-hook union.

| Function (hook) | Sites | Verdict | Proof |
|---|---|---|---|
| `0x47e027` (`heading_sin`) | 47e047 | dead | no call, no jump and no dword `0x0047e027` in the image; the function follows `heading_cos` and is never entered |
| `0x4152ec` (`condition_ratio`) | 415300, 415303 | dead | selector word 1/2 never occurs: the only caller `0x41b514` (`0x41b52a`) forwards its second argument, and both callers of `0x41b514` (`0x432a03`, `0x4342f8`) push 0; neither function has a dword reference. PROVED candidate 0x4152fe (taken only) |
| `0x4332dc` (`cockpit_select`) | 433301, 43331b, 433329, 433337, 43334e, 433368, 433387 | dead | the T ≠ 0 path: caller `0x432ea1` (key `;`) pushes 0, caller `0x43d7d2` runs only while byte `0x49be74` is set (0 in DATA, only write the clear at `0x43d7da`, no other reference). PROVED candidates 0x4332eb and 0x43338f (fall only) |
| `0x40e788` (`weapon_prepare`) | 40e7ef, 40e88d | dead | inside `cmp dword ptr [0x49ef34], 0` / `je`: the dword has no writer (its 7 file references are all such compares; no immediate points into `0x49ef2b..0x49ef37`; DATA holds 0). PROVED candidates 0x40e7e7, 0x40e885 (taken only) |
| `0x421c8c` (`flyer_hit_test`) | 421cb6 | dead | the same `0x49ef34` test (0x421cae, taken only) |
| `0x433174` (`cockpit_valid`), `0x433250` (`cockpit_arc`), `0x45feb8` (`command_key`) | 0x43318a, 0x433261, 0x460836 | PROVED candidates | the same `0x49ef34` test; only the taken edge exists (these sites are not in the never list: their taken edges are live) |

### Dead smoke hooks

Whole hooked functions that no run of the retail game enters, read from the listing and a raw scan of the file for
the dwords `0x004a9684`, `0x004a966e`, `0x004092dc`, `0x00409178` and `0x004091cc`
([oracle_suites.md](oracle_suites.md#smoke-no-allocation-site)).

| Function (hook) | Sites | Verdict | Proof |
|---|---|---|---|
| `0x4092dc` (`smoke_update`) | all 10 | dead | its only caller is the update loop over the `SMOKE` pool `[0x4a9684]` (`0x45f644`); the pool pointer has five code references (flush `0x408f9c`, store `0x40910f`, clear `0x4094d2`, destruction `0x4094f7`, update `0x45f665`) and two DATA references in the static-object cleanup table (`0x497fc2`, `0x497fce`); no `pool_alloc` call receives it, so the pool stays empty |
| `0x409178` (`smoke_ball_init`) | none | dead | single call `0x409442` inside `smoke_update` |
| `0x4091cc` (`smoke_ball_update`) | 409203, 40922c | dead | its pool `[0x4a966e]` is allocated from only at `0x40941b` inside `smoke_update`; the other references are flush, store, clear, destruction, the draw pass `0x4286e8` and the update loop `0x45f624` |

### Dead branch paths of the 36-suite union

Never-reached sites of the 36-suite edge union, in addition to the two tables above (which still hold). None is a
PROVED line, because every group's in-vitro cases drive both edges.

| Function (hook) | Sites | Verdict | Proof |
|---|---|---|---|
| `0x45a810` (`control_record`) | 45a86c | dead | the skip loop for a key word with a zero low byte: the key ring `0x6bb6b0` has the single writer `0x477d00` (`key_message`), whose codes are `0x4a10ec` table entries (89 nonzero, low bytes 01h..58h) plus 80h, 200h, 400h, 800h; the modifier keys are unmapped; the 0C000h blocks of `0x45c0a0` have no predecessor. PROVED candidate 45a87b (fall only) |
| `0x45feb8` (`command_key`) | 4600a9 | dead (tape words not checked) | Shift+V 82Fh: word `0x49eab8` (47FFh) masks every code before `command_key` (`0x45a87d`, `0x45ad55`, `0x45b67d`) and has no writer and no base pointer within 64 bytes |
| `0x432bc8` (`hud_key`) | 4330de | dead | byte `0x4d2538` = 1 needed: no instruction of any width writes it (about 25 `movsx` reads; neighbours have byte writes only), no base pointer within 64 bytes, BSS zero. PROVED candidates 4330ca (taken only), 432d56 (fall only) |
| `0x404bc0` (`pool_query`) | 404bf9 | dead | the side argument is −1 from every caller: only `0x404ca0` calls it and forwards its fifth argument, which `0x416407`, `0x416563`, `0x41dccc` push as −1; no dword reference to either function. PROVED candidate 404bed (taken only) |
| `0x41b130` (`cockpit_sync`) | 41b192 | dead | inside `cmp dword [0x49ef34], 0` (41b18a taken only): the dword has no writer |
| `0x45f570` (`logic_frame`) | 45f60c, 45f64d | dead | the SMOKE_BALL and SMOKE update loops: both pools stay empty. PROVED candidates 45f63a, 45f67b (fall only) |
| `0x41c194` (`player_route`) | 41c286 | defensive | mission mode 5: no prepared mission has it (survey of the prepared missions) |
| `0x4730e8`, `0x4733a4`, `0x472f64` (`stream_open`, `stream_seek`, `stream_destroy`) | 47317f … 473203, 473416 … 4734bf, 472f9c, 472fdb | defensive | VOL member kinds 7 and 9: every outer member on the CD is kind 02 ([vol.md](../formats/vol.md)) |
| `0x45e510`, `0x45cca4`, `0x45cd5c` (`prefs_video`, `tape_save`, `tape_restore`) | all 16 | unarmed | start-up code before the arming call `0x45f42b`; coverage cannot observe it |

## Render root, stage 5 and manager slots

### The render root

Contracts in [main_loop.md](../reverse-engineering/main_loop.md#the-render-root-as-own-code), read from the
`llvm-objdump -d -M intel` listing of `VER95/DBSIM.EXE` (SHA-256 `8561aa47…c4ad`).

| VA | Verdict | Package or document | Reason |
|---|---|---|---|
| `0x45fca8`, `0x401d94`, `0x40707c` | H4, hooked | hooks `render_root`, `stage_dispatch`, `vector_copy` (group `render_root`) | the root's simulation part is the stage-5 dispatch (removal flushes), `panel_frame` and `cockpit_sync` (hooks in their own modes); `0x40707c` has one caller, `0x45fd7b` |
| `0x42d870` | H3 (stays a guest call) | [display_modes.md](../reverse-engineering/display_modes.md#classification-table) | jolt offset point (U): `0x4cfcbc` ← 0, `0x4cfcc0` ← s16 `0x4cfa70`, EAX = `0x4cfcbc`; the root adds it to the canvas origin of the graphics context `[0x4d24ce]` for the draws and subtracts it again |
| `0x42db18` | H3 (stays a guest call) | [display_modes.md](../reverse-engineering/display_modes.md#classification-table) | 3D view visible in this layout (P); AL gates the world draws and `0x40707c` |
| `0x4361cc` | H3 (stay guest calls) | [hud.md](../reverse-engineering/hud.md#classification-table) | DialogPort and EventPort frames (Q+U+C: message queue, port draw slot, clock), reached by the root's virtual `+0x10` calls at `0x45fde0`/`0x45fdf2`; the message-port rule keeps the ports with the presentation side |
| `0x436f70` | H3 (stay guest calls) | [hud.md](../reverse-engineering/hud.md#classification-table) | DialogPort and EventPort frames (Q+U+C: message queue, port draw slot, clock), reached by the root's virtual `+0x10` calls at `0x45fde0`/`0x45fdf2`; the message-port rule keeps the ports with the presentation side |

### Stage-5 flushes and manager slots

Contracts in
[main_loop.md](../reverse-engineering/main_loop.md#the-stage-5-flushes-and-the-manager-slot-methods). The eleven
stage-5 callbacks `0x405ea4`, `0x4077e8`, `0x407b3c`, `0x4083e0`, `0x408f9c`, `0x409904`, `0x409a1c`, `0x40a108`,
`0x41544c`, `0x4215a8` and `0x46b188` are hooks of group `stage_flush` (their Verdicts rows name them); the rows below
are the twelfth flush, the manager slot methods and the one draw slot that stays original.

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x408f84` | H4 | hook `flush_smoke_balls` (group `stage_flush`) | the twelfth removal flush (SMOKE_BALL pool `0x4a966e`), not registered in the table: called only by `0x408f9c` after its own flush |
| `0x46b905` | H4, hooked | hook `object_position` (group `manager_slots`) | slot `+04` of 22 tables: EAX = O + 26h |
| `0x46b910` | H4, hooked | hook `shape_word` (group `manager_slots`) | slot `+10` of 22 tables: the shape word `[[[O+34h]+4]+8]`, 0 without shape |
| `0x478760`, `0x47877c`, `0x47878c`, `0x4787b8` | H4, hooked | hooks `sequence_next`, `sequence_prev`, `sequence_reach_up`, `sequence_reach_down` (group `manager_slots`) | ANSequence (`0x4a684c`) slots `+20h`..`+2Ch`; the animation hooks compute them in core (`es2_clip_call`), so they are entered only under an original animation function |
| `0x4787e4`, `0x478804`, `0x478820`, `0x47882c` | H4, hooked | hooks `cyclic_next`, `cyclic_prev`, `cyclic_reach_up`, `cyclic_reach_down` (group `manager_slots`) | ANCyclicSequence (`0x4a6810`) slots `+20h`..`+2Ch`; `0x478820`/`0x47882c` (MOV AX,1) complete the class |

### Audit of the fire and animation-manager methods

The 23 VAs of the fire and animation-manager methods (`0x46b8ea`…`0x46b910`, `0x478760`…`0x478804`, the eleven
stage-5 callbacks `0x405ea4`…`0x46b188`) need no further hook: 22 are hooks and the last one carries an H3 verdict.
How this was established (all on the reference `DBSIM.EXE`, hash as in `data/reference_hashes.txt`):

- Registry: each of the 22 VAs has an `ES2_HOOK` line in `tools/reference-game/proxy/hook_registry.h` (offset = VA −
  `0x400000`); `check_signatures.py` verifies the bytes at every registered VA against the registered signature.
- Stage-5 callbacks: a scan of the listing for `push 5` ahead of the stage-registration call `0x401d64` finds exactly
  eleven callback VAs (the list above); all eleven are hooks of group `stage_flush` (the twelfth flush `0x408f84` is
  not registered and is a hook too). The flushes are reached from the render root through `stage_dispatch`, group
  `render_root`; their own group is `stage_flush`.
- Manager methods: `0x46b905` (`object_position`), `0x46b910` (`shape_word`), `0x478760`, `0x47877c`, `0x47878c`,
  `0x4787b8` (`sequence_*`), `0x4787e4`, `0x478804`, `0x478820`, `0x47882c` (`cyclic_*`) are hooks of group
  `manager_slots`.

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x46b8ea` | H3 (stays original) | — | listing `0x46b8ea`…`0x46b904`: `push ebx` with `ebx` = the argument O, `call 0x401fe4(O)`, then `call [[O+34h]+1Ch]` (the shape's draw slot) with O+34h as its argument; no state of its own. The two callees are the matrix cache and a draw slot, so a hook of the 27 bytes would only forward both calls through the bridge. Entered in every suite (function table `docs/functions/dbsim-460000.md`: 885/1000/519/1000/1000/1000 probes) |
| `0x401fe4` | H3 | — | the lazy object matrix (listing `0x401fe4`…`0x40201a`, cdecl O): with `b` = O+0Ch, if the word at `b+26h` (O+32h) is 0 it calls `0x47ebb8` (`euler_matrix`, hook) on `b` and `b+6` (O+12h) and sets the word to 1, then in every case `0x48c444(O+12h)` (the view transform of the render context, H3). Seven call sites in the listing: `0x4033f1`, `0x4034cb` (both in `0x4033e4`), `0x408e7e` (`0x408e6c`), `0x40992b` (`0x40991c`), `0x409d15` (`0x409cd0`), `0x40a12f` (`0x40a120`) and `0x46b8f2` (`0x46b8ea`). The matrix cache is the same one that [terrain.md](../reverse-engineering/terrain.md) lists as simulation-rebuilt on each update; the draw-time fill is idempotent for an unchanged pose, so it stays with the draw verdicts of `0x4033e4`. An own process needs it with the draw slots (presenter side) |

`0x46b8ea` is slot `+00` (draw) of five class tables (`0x4973ac`, `0x497d4c`, `0x498084`, `0x4a0b80`, `0x4a0ba0`): it calls
`0x401fe4` (lazy matrix, view transform) and then the shape's draw slot `+1Ch`.

Whether the own `dbsim.exe` wants `0x46b8ea`/`0x401fe4` as own code is a question of the draw track, not of the
simulation hooks. [dbsim_gap.md](dbsim_gap.md) section 4.1 lists the fire and animation-manager methods, the eleven
stage-5 callbacks and `0x409904` as hooks.

### Bridged leaves

Contracts in [hud.md](../reverse-engineering/hud.md#the-bridged-leaves). Hooks of group `bridged_leaves` (the
default constructors of the matrix type among them); the Verdicts rows of the same functions name the hooks.

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x40e484` | H4, hooked | hook `weapon_target_word` (group `bridged_leaves`) | the weapon word under `cockpit_sync` |
| `0x411e40`, `0x411e60` | H4, hooked | hooks `list_next`, `list_prev` | `command_key`'s view-subject steps over the actor list `0x4a9af8` (IDIV by the count word) |
| `0x41b468` | H4, hooked | hook `r_key_toggle` (entry shim) | the R key: toggles P+0x96 under the player flag P+0xa3, sound row, two message cancels and the EventPort message; the sound and message callees stay guest calls |
| `0x434974` | H4, hooked | hook `nav_marker_set` | Alt+D: the nav marker H+0x25e from the player position, active, not armed |
| `0x447e08` | H4, hooked | hook `throttle_handler` | ThrottleGauge slot 0 under `slider_base` |
| `0x45fe38` | H4, hooked | hook `view_pref_toggle` | the view-preference word `0x4d2518` and the view countdown `0x4d2520` |
| `0x47df04` | H4, own code (computed in core) | — | the 2x2 word swap; computed by `convoy_follow` and `collision_walk` (counter `call_47df04` kept) and already by `formation_steer`; no other caller, no hook |
| `0x495132`, `0x495194`, `0x49514f` | H4, hooked (changed description) | hooks `matrix_ctor`, `rotation_ctor`, `rotation_base_ctor` | "identity rotation" is wrong: the three are the empty default constructors of the 32-byte matrix type and write nothing (`0x49514f` returns its argument; `0x4953c4` → `0x49545b` likewise). Not renderer state: they are called by H3 code (`0x48c2e4`, `0x4949e4`, `0x488b98`), at mission start (`0x48dd08`) and as the element constructor of `0x49667c` from simulation code (`0x402be8`, `0x40d81f`, `0x4788c6`, `0x490ecc`) |
| `0x4953c4`, `0x49545b` | runtime (unchanged; stays a guest call) | — | the vector constructor and its 5-instruction leaf, both empty (return the argument); `0x4953c4` keeps the `NEVER` list's runtime verdict |

### Functions counted as still original

Twelve functions with an H4 verdict can look like original code that still runs ([dbsim_gap.md](dbsim_gap.md)
section 4.1 counts them). **None of them needs a hook or a dead verdict**: nine are hooks, three are computed in core
by hooked callers and have no other reference. Their verdict stays H4 (own
code). Every fact is read from `DBSIM.EXE` (SHA-256 `8561aa47…c4ad`, image base `0x400000`): the `llvm-objdump -d -M
intel` listing, a byte scan of the whole image for `E8`/`E9` rel32 targets in CODE and for the dword VA in CODE and
DATA, and a function-start walk (`push ebp` or `enter` after `ret`/`nop`/`jmp`) to name the container of each call
site. The map counts are from `dbsim-map-20261012\map.json` (40 suites); that map predates the six leaf hooks of
group `bridged_leaves`, which still appear as plain rows there.

| Function | State at 728 hooks | Every reference in the image | Map 20261012 |
|---|---|---|---|
| `0x409904` | hook `flush_attached` (group `stage_flush`) | `call` from `0x4187c4` (`vehicle_part`, hook) and `0x461858` (`0x461608`, the mission load, original code that calls the hooked VA); `push 0x409904` at `0x409952` (stage-5 registration, priority 5, in the static init record `0x409940`) | hook row: 35,316 calls, 40 suites |
| `0x40e484` | hook `weapon_target_word` (`bridged_leaves`) | one `call` from `0x41b39b` (`cockpit_sync`, hook); no pointer reference | 4 calls, 1 suite |
| `0x411e40` | hook `list_next` | `call` from `0x460250`, `0x460491` (both `command_key`, hook); no pointer reference | 69 calls, 3 suites |
| `0x411e60` | hook `list_prev` | `call` from `0x46026c` (`command_key`); no pointer reference | 22 calls, 3 suites |
| `0x41b468` | hook `r_key_toggle` (entry shim, no compare) | one `call` from `0x432f7d` (`hud_key`, hook); no pointer reference | no row (not reached by any suite) |
| `0x434974` | hook `nav_marker_set` | one `call` from `0x432f88` (`hud_key`); no pointer reference | no row (not reached by any suite) |
| `0x447e08` | hook `throttle_handler` | only the vtable dword at `0x49d1e0` (slot 0 of the ThrottleGauge class, DATA); no `call` | 1,734 calls, 31 suites |
| `0x45fe38` | hook `view_pref_toggle` | one `call` from `0x4604f1` (`command_key`); no pointer reference | 1 call, 1 suite |
| `0x495132` | hook `matrix_ctor` | `call` from `0x48c2ff` (in `0x48c2e4`), `0x48dd8d`/`0x48dd98`/`0x48dda3` (in `0x48dd00`, the mission-start graphics state) and `0x494aeb` (in `0x4949e4`): original code that calls the hooked VA; `push 0x495132` at `0x402be8`, `0x40d81f`, `0x4788c6`, `0x490ecc` (the element constructor passed to the runtime's array constructor `0x49667c`, a *service* of the own process, which then dispatches to the hook) | 39,388 calls, 40 suites |
| `0x427d98` | **computed in core** by hook `sweep` (`0x426528`) | `call` from `0x426650` and `0x426694`, both inside `sweep`; no pointer reference | 19,710 calls, 24 suites (saturated at 1,000 per suite) |
| `0x427da0` | **computed in core** by `sweep` | `call` from `0x42668a` and `0x42684b`, both inside `sweep`; no pointer reference | same function: 19,710 calls, 24 suites |
| `0x47df04` | **computed in core** by `convoy_follow`, `collision_walk` and `formation_steer` | `call` from `0x46ab46` (`convoy_follow`), `0x427d13` (`collision_walk`), `0x41fd37` (`formation_steer`); no pointer reference | 32,166 calls, 36 suites |

What this means for an own `dbsim.exe` (hypotheses from the static scan, not live-validated):

- The nine hooks are dispatched by VA like every hook; `0x447e08` through the vtable slot, `0x409904` through the
  stage-5 registration, `0x495132` through the array-constructor service.
- The three computed ones need **no dispatch entry**: no vtable, no pushed pointer, and each of their callers
  (`sweep` `0x426528`, `convoy_follow` `0x46aa60`, `collision_walk` `0x427c68`, `formation_steer` `0x41fbb8`) is a
  hook. The core mirrors every call site: `sweep` has the two clears and two reads in the order of the four sites
  (`core/src/weapons.c`), and the swap is done at the three sites (`convoy.c`, `obstacle.c`, `ai_motion.c`). They
  count as "own code (computed in core)", not as original code that still runs. The accumulator dword `0x4aaad0`
  they clear and read is also written by `object_query` (`0x427da8`, a hook, at `0x427e34`/`0x427e47`), so it lives
  in guest memory for both.
- Nothing here is dead: the two functions without a map row (`0x41b468`, `0x434974`) are the R key and Alt+D of
  `hud_key`, reachable from a key press and hooked. The map's rows list only functions with a nonzero count and it
  probed 3,009 of its 3,010 starts (`unprobed`: only `0x467ad8`), so no suite entered them if they were among the
  starts (the start list was not checked here); the in-vitro cases are their only coverage.

## Cockpit, HUD and MFD

### HDD map order controls

Contracts in [cockpit.md](../reverse-engineering/cockpit.md#hdd-map-order-controls), read from the
`llvm-objdump -d -M intel` listing of `VER95/DBSIM.EXE` (SHA-256 `8561aa47…c4ad`).

| VA | Verdict | Package or document | Reason |
|---|---|---|---|
| `0x44d450`, `0x44d9f4`, `0x44da98`, `0x44dc10` | H4, hooked | hooks `map_areas`, `map_mode`, `wingman_select`, `order_cancel` (group `hdd_orders`) | the label/lamp/cursor callees `0x44dc6c`, `0x44dd14`, `0x44ede0`, `0x44ee00`, `0x44ee24`, `0x433e3c` and `0x486e7c` stay guest calls (H3) |

`HDDMap` slot 1 (`0x44c988`) is the map frame, hook `map_frame` (group `hdd_map`, entry shim): besides the event
dispatch (hooks `map_areas`, `map_click`, `map_pick`) and a small order-record check it colours and draws the mode
buttons, labels and the map through guest calls. It stores the address of its local `[EBP−0xc]` in the graphics
context `[[0x6c5f70]+0x22c]` before the draws (`0x44cb2e`, `0x44cc32`), with `[EBP−8]` unwritten on the `0x44cb28`
path: a frame-address and residue contract with the graphics library that the entry shim keeps.

### Cockpit and player synchronisation

Contracts in [hud.md](../reverse-engineering/hud.md#cockpitplayer-synchronisation-as-own-code), read from the
`llvm-objdump -d -M intel` listing of `VER95/DBSIM.EXE` (SHA-256 `8561aa47…c4ad`). The two leaves under
`cockpit_sync` are hooks of group `bridged_leaves` ([Bridged leaves](#bridged-leaves)).

| VA | Verdict | Package or document | Reason |
|---|---|---|---|
| `0x41b130`, `0x447df8`, `0x447da8`, `0x43dbb0` | H4, hooked | hooks `cockpit_sync`, `throttle_record`, `throttle_gauge`, `hud_record` (group `cockpit_sync`) | `0x4450f8`, `0x43d9b4`, `0x434a24` (H3) and `0x40e484` stay guest calls, the nested hooks keep their modes |
| `0x45266c`, `0x452584` | H3 (stays original) | — | slider set value, class-table slot `+0x14` (`0x49d12c`/`0x49d19c`/`0x49d170` resp. `0x49d0e8`): clamps into `+0x24..+0x26`, knob word `+0x28`, forwards the clamped value to slot `+8` (`slider_event` for the throttle slider); widget fields only, reached from `throttle_gauge` (map 48/88/62/446/50/70 for `0x45266c`) |
| `0x447e08` | H4 | hook `throttle_handler` (group `bridged_leaves`) | `ThrottleGauge` handler (slot 0 of `0x49d1e0`, called by `slider_base` through the slider's `+0x42`): `G+0xb5` ← the event value (negated unless byte `G+0xc1`) when the event's gadget is its first one `[G+0x18]` (the walk over the count `G+0x68` returns at a later match without a write); the set point that `0x41b130` reads back into `P+0x290`; no calls |

`0x40e484` (hook `weapon_target_word`, group `bridged_leaves`) writes the weapon word `W'+0x7d` of the player's weapon: 0 when
the target's word `+0x1a8` is 0, else 0FFFFh (14 instructions, only caller `0x41b130`).

### MFD order path

Contracts in [cockpit.md](../reverse-engineering/cockpit.md#mfd-order-path). The twelve functions of group
`mfd_orders` are hooks; the rows below name them and the helpers that stay original. `0x431530` is the load-time
set-up of the order group `0x49b0f8` and the wingman table `0x4d03c8` (simulation, not HUD). The effect of
`mfd_view_next` (`0x446fc4`) is the MFD status view and that of `mfd_range` (`0x446ff0`) the MFD radar range, both
presentation state; they are hooked with the button owner. The key paths into the order handlers are the
[MFD keys](#mfd-keys).

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x446844` | H4, hooked | hook `mfd_button` (group `mfd_orders`) | slot 0 of the `MFDGauge` table `0x49cfa0`: the owner call of a pressed button from `0x438e3c`; buttons 11/12 are the radio pair `[G+44h]`/`[G+48h]` |
| `0x4470c0` | H4, hooked | hook `mfd_click` |  |
| `0x447248` | H4, hooked | hook `mfd_order` |  |
| `0x43166c` | H4, hooked | hook `order_call` |  |
| `0x4231a4` | H4, hooked | hook `group_order` | the uninitialized priority word (codes 3, 6, 9..0Eh and above 0Fh) read from the original frame through its entry shim |
| `0x43f9c0` | H4, hooked | hook `menu_code` (accepts compare) |  |
| `0x43f9f8` | H4, hooked | hook `menu_toggle` (accepts compare) |  |
| `0x4470a4` | H4, hooked | hook `mfd_entry` (accepts compare) |  |
| `0x446fb0` | H4, hooked | hook `mfd_target` |  |
| `0x446fc4` | H4, hooked | hook `mfd_view_next` (accepts compare) | its effect is the MFD status view (presentation state); hooked with the button owner |
| `0x446ff0` | H4, hooked | hook `mfd_range` | its effect is the MFD radar range (presentation state); hooked with the button owner |
| `0x431530` | H4, hooked | hook `order_setup` | the load-time set-up of the order group `0x49b0f8` and the wingman table `0x4d03c8`: simulation, not HUD |
| `0x4472bc`, `0x447014` | H3 | — | bridged by `order_setup` and `mfd_range`: MFD status-view list and radar range |

`0x447658` is slot +8 of the `MFDSelectGadget` table `0x49cee0` (a left click, see its Verdicts row); it stays an
open candidate.

### MFD keys

Contract in [cockpit.md](../reverse-engineering/cockpit.md#mfd-keys). The MFD key method `0x4469e8`, its helper
`0x447158`, the Alt order handler `0x446c38` with `0x43f9e0`, and the two entry steps are six hooks of group
`mfd_keys`. "Live" is the entry probe per suite of `dbsim-map-20261012/map.json` (saturated at 1000); a suite not
listed has a zero count.

| Function | Verdict | Package | Contract | Live |
|---|---|---|---|---|
| `0x4469e8` | H4, hooked | hook `mfd_key` (group `mfd_keys`) | `MFDGauge` key method (slot `+0xc`): MFD buttons 7..10 through the enable table `0x49cbd8`, `mfd_range`, the entry steps and the jump table to `0x447158` | 33 suites (1 to 721) |
| `0x446c38` | H4, hooked | hook `mfd_alt_key` (group `mfd_keys`) | `hud_key`'s Alt order keys: `menu_select`, `menu_code`, `mfd_order`, `0x447158` | 33 suites (1 to 735) |
| `0x447158` | H4, hooked | hook `mfd_key_select` (group `mfd_keys`) | `mfd_entry(G, n)` and byte `G+E6h` when o is `[G+FBh]` in MFD mode 1 | 6 suites: c3_09-orders-matrix=20, c5_05-orders-flyer=19, c3_09-wingman-orders=15, c3_10-hunt-targ=4, c1_01-cheat-disable-late=1, c1_01-cheat-leader=1 |
| `0x43f9e0` | H4, hooked | hook `menu_select` (group `mfd_keys`) | word `[menu+37h]` = the old entry, `[menu+32h]` = k; ECX = k (the shim reloads it) | 6 suites, as `0x447158` |
| `0x44703c` | H4, hooked | hook `mfd_entry_next` (group `mfd_keys`) | next entry with flag bit 0 clear (6 wraps to 0), `mfd_entry` | not probed (reached from `0x4469e8`, key 34h) |
| `0x447070` | H4, hooked | hook `mfd_entry_prev` (group `mfd_keys`) | previous entry with flag bit 0 clear (below 0 wraps to 5), `mfd_entry` | not probed (reached from `0x4469e8`, key 33h) |

The sim serves five of the six VAs (all but `0x43f9e0`, which `sim/src/panel.c` does inline) and the HDD button
executor `0x44a1a0` through service rows (`sim/src/mfd.c`; `0x446c38` in `sim/src/panel.c`); `served_rvas` in
`sim/src/adapters.c` skips their adapters.

### HDD button executor

Contract in [cockpit.md](../reverse-engineering/cockpit.md#hdd-button-executor). "Live" as in the MFD keys section.

| Function | Verdict | Package | Contract | Live |
|---|---|---|---|---|
| `0x44a1a0` | H4, hooked | hook `hdd_execute` (group `hdd_exec`, entry shim) | the HDD button executor (only caller `0x449c64`): early exit on `[E+528h]`, the 15-entry jump table at `0x44a1f7` (page switches, status pages, map scroll, lamps, wingmen, the send/cancel order paths), the frame pointer EBP-8 left in `[[0x6c5f70]+22Ch]` (`0x44a3e9`); EDX:EAX exact, ECX popped by the caller | all 40 suites (202 to 1000) |

The sim serves `0x44a1a0` as `VA_HDD_EXECUTE` in `sim/src/mfd.c`; its RVA `0x4a1a0` is in `served_rvas`.

## Weapons

### Weapon-class methods

Contracts and the slot matrix in
[weapon_classes.md](../reverse-engineering/weapon_classes.md#the-weapon-class-methods) and
[weapon_classes.md](../reverse-engineering/weapon_classes.md#the-weapon-gauge-syncs-the-laew-emission-and-the-empty-bodies).
29 bodies of the weapon vtables' slots `+2Ch` … `+60h` are hooks of group `weapon_methods`. The rest of the slots
are hooks of group `weapon_syncs`: the six slot `+50h` gauge syncs, the LAEW emission `0x40ea04` (slot `+28h`) and the
six empty bodies of the slots `+28h`, `+38h`, `+3Ch`, `+40h`, `+44h` and `+4Ch`. The empty bodies are five bytes
(`push ebp; mov ebp,esp; pop ebp; ret`); the hook framework accepts a final `ret`, so they are hooks too. The range
`0x40e62c` … `0x40f8ad` has no original function left except the type data `0x40f77f..0x40f7e5` (not code) and, before
the range, `0x40e61c` (hook `weapon_shape_word`, group `fit_leaves`). `0x4111ce` … `0x4111dd` lie outside the range.

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x40f144` | H4, hooked | hook `equip_sync` | slot `+50h` of EQUIP, TARG, SHIELD, ENERGY: widget `[W+79h]` slot `+18h` record, byte 1 = `W+49h`, slot `+14h` |
| `0x40f184` | H4, hooked | hook `ecm_sync` | ECM slot `+50h`: toggle byte against sx16(`W+7Dh`), cockpit message `0x420a68(2Ah/2Bh)`, words `W+7Dh`/`+7Fh` = the toggle |
| `0x40f1f0` | H4, hooked | hook `turbo_sync` | TURBO slot `+50h`: overheat latch `W+81h` against charge 600, `sfx_play(2Ch)`, the local's dword 4 = `W+7Dh` |
| `0x40f288` | H4, hooked | hook `beam_sync` | BEAM/ELF slot `+50h`: gauge record `0x440e9c([W+77h])`, level scaled 1024/1200, widget slot `+10h` |
| `0x40f330` | H4, hooked | hook `gun_sync` | GUN slot `+50h`: gauge record `0x4414cc([W+77h])`, `W+7Dh` ramps down by `movement_step(250)` to ammunition·256, widget slot `+10h` |
| `0x40f3b8` | H4, hooked | hook `laew_sync` | LAEW slot `+50h`: `gun_sync` with the argument words (computed in core) |
| `0x40ea04` | H4, hooked (entry shim) | hook `laew_fire` | LAEW slot `+28h`: preparation, charge, flag `W+46h`, emission `0x40acac`; chained shim = weapon_fire's residue bytes; never entered in the suites |
| `0x40e95c`, `0x4111ce`, `0x4111d3`, `0x4111d8`, `0x4111dd`, `0x40f89f` | H4, hooked | hooks `weapon_none_28`, `_38`, `_3c`, `_40`, `_44`, `_4c` | the empty bodies of the slots `+28h`, `+38h`, `+3Ch`, `+40h`, `+44h`, `+4Ch` (the framework accepts a final RET) |

### Weapon constructors

Contracts in [weapon_classes.md](../reverse-engineering/weapon_classes.md#the-weapon-constructors). The nine
constructors are hooks of group `weapon_ctors`. Every callee has a verdict (`0x40fab0` platform, `0x40dd4c` H3,
`0x49681a` RTL; the others are hooks), and none of the nine is too short to hook or shares a tail.

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x40df30` | H4, hooked | hook `weapon_ctor` (group `weapon_ctors`) | the `WEAPON` base: vtable chain to `0x498e44`, the shape block (`shape_word_block`), the two animation tracks (`object_track`), the mount offset (`mount_offset`, then the H3 vertex edit `0x40dd4c`), the zeroed state, `W+4Ch` = 1, `W+63h` = 400h |
| `0x40e074` | H4, hooked | hook `beam_ctor` | BEAM (`0x498dcc`): level and charge words from `multiply_q10(334h, 4B0h)`, `W+7Fh` = 14h; the factory turns it into ELF with `0x4992c0` |
| `0x40e140` | H4, hooked | hook `gun_ctor` | GUN (`0x498d54`): rounds `W+7Bh` and `W+7Dh` (<< 8) from the definition's capacity word +3Ah; LAEW through `0x499248` |
| `0x40e234` | H4, hooked | hook `equip_ctor` | `EQUIP` base (`0x498cdc`): `W+4Ch` = 0, word `W+77h` = FFFFh |
| `0x40e274` | H4, hooked | hook `ecm_ctor` | ECM (`0x498c64`), code word 12h |
| `0x40e2bc` | H4, hooked | hook `turbo_ctor` | TURBO (`0x498bec`), code word 1Fh, `W+7Dh` = 7D0h, `W+81h` = 0 |
| `0x40e308` | H4, hooked | hook `targ_ctor` | TARG (`0x498b74`), code word 1Dh |
| `0x40e344` | H4, hooked | hook `shield_ctor` | SHIELD (`0x498afc`), code word 1Eh |
| `0x40e380` | H4, hooked | hook `energy_ctor` | ENERGY (`0x498a84`), code word 20h |

### Simulation leaves

Contract in [vtables.md](../reverse-engineering/vtables.md#simulation-leaves). Seventeen functions are hooks of group
`sim_leaves`. The "Live" column is the entry probe per suite of `dbsim-map-20261012/map.json` (saturated at 1000); a
suite not listed has a zero count and is not expected to count in a replaced run.

- `0x41a6d0` and `0x41a994` (the torso servo sound latches) are simulation: they decide the flag cells from the
  vehicle's servo words. The sound driver is the guest call `sfx_playing` `0x462af0`, a service row, which they reach as
  a callee.
- `0x420a68` and `0x420a98` build an event record and call the message port; the port stays original and is a guest
  call of the hook, the record content is simulation state.
- `0x41ff00` and `0x422e80` are computed in core by `mech_ctor` and `flyer_ctor` and also hooked for the original
  call sites that stay (the map counts them in all or 32 of the 40 suites).
- The `MECH` draw slot `0x4174c8` calls `0x4030d0` (`part_shapes_bind`); `sim/src/shape_draw.c` reaches it through the
  dispatch.

| Function | Verdict | Package | Contract | Live |
|---|---|---|---|---|
| `0x4030d0` | H4, hooked | hook `part_shapes_bind` (group `sim_leaves`) | the part-shape pairing of the MECH draw slot `0x4174c8`: the two counted lists A and B and the pool, words `+4`/`+6` copied | 36 suites (372 to 1000); zero in c1_01-unarmed-keys, c1_02-probe, c4_05-convoy, train1-timers |
| `0x403668` | H4, hooked | hook `parts_intact` (group `sim_leaves`) | 1 when no part record is below its cap (the walk over `[[O+1F2h]+14h]` and the words from `[O+205h]`) | 8 suites: c3_09-loadout-b=13, c3_09-orders-matrix=6, c3_09-wingman-orders=2, c3_10-detail-options=1, c3_10-own-building-fire=1, c4_08-razor-plasma=10, c4_08-slope-dive=1, c5_05-moon-missiles=13 |
| `0x405b9c` | H4, hooked | hook `formation_slot` (group `sim_leaves`) | formation table entry: the cell `0x4975d6` (first-call clear byte `0x4975e2`) gets the two dwords of the entry | 39 suites (3 to 1000); zero in c4_05-convoy |
| `0x407b20` | H4, hooked | hook `explosion_entry` (group `sim_leaves`) | `[0x4a9628]` + 40 x the s8 byte `[O+41h]` | 27 suites (22 to 1000); zero in c1_01-cheat-keys-plain, c1_01-display-keys, c1_01-free-camera, c1_01-keys, c1_01-keys-extra, c1_01-mouse, c1_01-options-dialog, c1_01-quit-dialog, c1_01-unarmed-keys, c1_08-escort-route, c4_05-convoy, c4_10-probe, train1-timers |
| `0x40991c` | H4, hooked | hook `flat_draw` (group `sim_leaves`) | flat object draw slot: `0x4029d8`, `0x401fe4`, the child's slot `+1Ch` | 38 suites (408 to 1000); zero in c4_08-joystick, c4_08-slope-dive |
| `0x40a120` | H4, hooked | hook `projectile_draw` (group `sim_leaves`) | projectile draw slot: `0x401fe4`, the child's slot `+1Ch`, `0x4a5b04` <- `[0x4a5b08]` | 23 suites (12 to 1000); zero in c1_01-cheat-destroy, c1_01-cheat-disable-leg, c1_01-cheat-keys, c1_01-cheat-keys-plain, c1_01-display-keys, c1_01-free-camera, c1_01-keys, c1_01-keys-extra, c1_01-mouse, c1_01-options-dialog, c1_01-quit-dialog, c1_01-unarmed-keys, c1_06-cheat-disable, c1_08-escort-route, c4_05-convoy, c4_10-probe, train1-timers |
| `0x40c2a0` | H4, hooked | hook `beam_done` (group `sim_leaves`) | beam done slot: `timer_word_step(O+5Ch)` is zero | 16 suites (27 to 1000); zero in acceptance-60s, c1_01-cheat-destroy, c1_01-cheat-disable-leg, c1_01-cheat-keys, c1_01-cheat-keys-plain, c1_01-cockpit-keys, c1_01-display-keys, c1_01-free-camera, c1_01-keys, c1_01-keys-extra, c1_01-mouse, c1_01-options-dialog, c1_01-quit-dialog, c1_01-success, c1_01-tape-record, c1_01-unarmed-keys, c1_02-probe, c1_06-cheat-disable, c1_08-escort-route, c4_05-convoy, c4_08-joystick, c4_10-probe, c5_05-orders-flyer, train1-timers |
| `0x410670` | H4, hooked | hook `weapon_by_mount` (group `sim_leaves`) | first weapon of the list whose mount definition byte `+17h` equals the word | 5 suites: acceptance-60s=1, c1_01-success=1, c3_09-loadout-b=4, c3_10-hunt-targ=2, c5_05-moon-missiles=3 |
| `0x411dd4` | H4, hooked | hook `ptr_list_add` (group `sim_leaves`) | append a pointer to a counted list and store the old count at `[p+4Bh]` | all 40 suites (12 to 74) |
| `0x413a90` | H4, hooked | hook `box_square_init` (group `sim_leaves`) | the square box record: words `+6`, `+8` = w, `+4` = 200h, `+2`/`+0` = half of `+6` | all 40 suites (1 to 16) |
| `0x41a6d0` | H4, hooked | hook `twist_sound_latch` (group `sim_leaves`) | torso-twist servo sound latch (cells `0x49a1c9`/`0x49a1ca`, vehicle word `+298h`), `sfx_playing(21h)` as a guest call | 37 suites (202 to 1000); zero in c4_08-joystick, c4_08-razor-plasma, c4_08-slope-dive |
| `0x41a994` | H4, hooked | hook `pitch_sound_latch` (group `sim_leaves`) | torso-pitch servo sound latch (cells `0x49a1cc`/`0x49a1ce`, vehicle word `+29Ah`), as the twist latch | 37 suites (202 to 1000); zero in c4_08-joystick, c4_08-razor-plasma, c4_08-slope-dive |
| `0x41ff00` | H4, hooked | hook `mech_type_index` (group `sim_leaves`) | type-index byte `0x4a9d08 + s16(w)` (MECH) | all 40 suites (89 to 141) |
| `0x420a68` | H4, hooked | hook `cockpit_notice` (group `sim_leaves`) | cockpit message post: the 24-byte record `{id, 0}` through the main object's port slot 0 | 14 suites (1 to 20); zero in acceptance-60s, c1_01-cheat-keys, c1_01-cheat-keys-plain, c1_01-cheat-leader, c1_01-cockpit-keys, c1_01-display-keys, c1_01-free-camera, c1_01-joystick, c1_01-keys, c1_01-keys-extra, c1_01-mouse, c1_01-options-dialog, c1_01-quit-dialog, c1_01-tape-record, c1_02-probe, c1_06-cheat-disable, c1_08-escort-route, c2_02-identify, c3_09-orders-matrix, c3_09-wingman-orders, c3_10-detail-options, c3_10-hunt-targ, c3_10-own-building-fire, c4_05-convoy, c4_10-probe, c5_05-orders-flyer |
| `0x420a98` | H4, hooked | hook `radio_notice` (group `sim_leaves`) | radio message post: the record `{id, A}` through `port_a` `0x433158` and the port's slot 0, unless the byte `A+99h` is set and the flag is zero | 16 suites (1 to 60); zero in acceptance-60s, c1_01-cheat-destroy, c1_01-cheat-disable-leg, c1_01-cheat-keys, c1_01-cheat-keys-plain, c1_01-display-keys, c1_01-free-camera, c1_01-keys, c1_01-keys-extra, c1_01-mouse, c1_01-options-dialog, c1_01-quit-dialog, c1_01-unarmed-keys, c1_02-probe, c1_06-cheat-disable, c1_08-escort-route, c1_08-scan, c2_02-identify, c4_05-convoy, c4_08-joystick, c4_08-razor-plasma, c4_08-slope-dive, c4_10-probe, train1-timers |
| `0x422e80` | H4, hooked | hook `flyer_type_index` (group `sim_leaves`) | type-index byte `0x4a9dcc + s16(w)` (FLYER) | 32 suites (38 to 47); zero in c3_09-loadout-b, c3_09-orders-matrix, c3_09-wingman-orders, c3_10-detail-options, c3_10-hunt-targ, c3_10-loadout-hercs, c3_10-own-building-fire, train1-timers |
| `0x426aec` | H4, hooked | hook `notice_time` (group `sim_leaves`) | message display time: 7530h, doubled unless the word `0x4a9e5e` is 1 | 16 suites (12 to 1000); zero in acceptance-60s, c1_01-cheat-destroy, c1_01-cheat-disable-leg, c1_01-cheat-keys, c1_01-cheat-keys-plain, c1_01-collision, c1_01-free-camera, c1_01-keys, c1_01-keys-extra, c1_01-mouse, c1_01-options-dialog, c1_01-quit-dialog, c1_01-tape-record, c1_01-unarmed-keys, c1_02-probe, c1_06-cheat-disable, c1_08-escort-route, c1_08-scan, c2_02-identify, c3_10-detail-options, c3_10-loadout-hercs, c3_10-own-building-fire, c4_05-convoy, c4_10-probe |

Not hooked or computed in core:

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x425344` | H4 (stays original) | — | one-byte `ret` body, below the detour size; the map counts 1 call in 14 suites (`mission_end`); not mirrored by the core |
| `0x426514` | H4 (stays original) | — | as `0x425344`: one-byte `ret` body, 1 call in 14 suites |
| `0x427d98` | H4 (computed in core) | — | hit-accumulator clear of `sweep` (`0x426528`); called only from inside `sweep`, no hook needed |
| `0x427da0` | H4 (computed in core) | — | hit-accumulator read of `sweep`; as `0x427d98` |

## Asset loading

### Shape and bitmap loading

Contracts in [asset_loading.md](../reverse-engineering/asset_loading.md), read from the `llvm-objdump -d -M intel`
listing of `VER95/DBSIM.EXE` (SHA-256 `8561aa47…c4ad`). The platform verdicts stand (file loading, not simulation);
the functions below are own code behind hooks as H3 platform leaves (group `assets`). `0x481cd4`, `0x482d88` and
`0x483374` are not vtable slots of a `BMP_8` class but the factory fields (`+4`) of the GL class registry `0x4a2eb8`
(nine entries tag, factory, name; `0x4a2e9c+0x2c` = `0x4a2ec8` is entry 1's factory).

| VA | Verdict | Package or document | Reason |
|---|---|---|---|
| `0x47a144`, `0x481cd4`, `0x481d8c`, `0x482d88`, `0x482e7c`, `0x472f64` | platform, hooked | hooks `dts_load`, `bitmap_new`, `bitmap_read`, `bitmap_array_new`, `bitmap_array_read`, `stream_destroy` (group `assets`) | persistent-object loader (the header `0x47a6b4`, the registry lookup `0x47a4a0` and the drain `0x47a83c` are reproduced inside `dts_load`), `GLBitmap` and `GLBitmapArray` factories and reads, VOL-aware stream destructor (with the base destructor `0x47a8a4`) |
| `0x4730e8` | platform, hooked | hook `stream_open` (group `vfs`) | the stream open of the virtual file system; own code |
| `0x483374` | platform (stays original) | [asset_loading.md](../reverse-engineering/asset_loading.md) | `GLCursor` factory (registry `0x4a2eb8` entry 8, object `0x10` bytes, constructor `0x48358c`, read virtual `+0xc`); one load per suite after arming; `dts_load` calls it through the guest bridge, its compare skips the class |
| `0x45cdf4`, `0x430fb0` | platform (stay original) | [asset_loading.md](../reverse-engineering/asset_loading.md) | load-by-name wrappers: a VOL-aware stream on their frame (`0x472f14`), `0x47a144(S, 0)`, `0x472f64(S, 2)`; every call reaches the hooks |
| `0x47a6b4`, `0x47a4a0`, `0x47a83c`, `0x47a288`, `0x47a2c8`, `0x48156c`, `0x482f38`, `0x47a8a4` | platform (reproduced inside the asset hooks) | [asset_loading.md](../reverse-engineering/asset_loading.md) | header read, registry lookup, drain, arena allocations (the same code at two addresses), the two constructors and the base stream destructor; their other callers keep the original |

## Mission load

The hooks below build the type tables, pools and world at mission start; contracts in
[mission_load.md](../reverse-engineering/mission_load.md). Element constructors that a hook reaches only through the
runtime array construction `0x49667c` (a service of the own process) stay original and have rows.

### Object shape set-up

Contracts in [mission_load.md](../reverse-engineering/mission_load.md) section 19. Nine hooks of group `object_shapes`:
`ts_obj_ctor` `0x402278`, `det_anim_shape` `0x40332c`, `det_anim_static` `0x403368`, `det_anim_record` `0x4033a4`,
`anim_instance_ctor` `0x478838`, `object_track` `0x402374`, `thread_add` `0x478a3c`, `thread_ctor` `0x478e48` and
`shape_word_block` `0x402fc0`. Details beyond the Verdicts rows:

| VA | Verdict | Source | Note |
|---|---|---|---|
| `0x40332c` | H4 (hook `det_anim_shape`) | hook `det_anim_shape` (group `object_shapes`) | stores `ANIM_OBJ` `0x4973ac` and then `DET_ANIM_OBJ` `0x4973cc` like `0x403368` and `0x4033a4`: a `DET_ANIM_OBJ` constructor over `0x402278`, not an `ANIM_OBJ` constructor |
| `0x402fc0` | H4 (hook `shape_word_block`) | hook `shape_word_block` (group `object_shapes`) | its third call `0x403274` lies in `0x4031d4` (own function after `0x40316c`'s `RET` at `0x4031d3`), not in the H3 `0x40316c`; `0x4031d4` has no call, jump or file-dword reference |
| `0x4922e6` | open | [mission_load.md](../reverse-engineering/mission_load.md) | element constructor of the 12-byte pose array of `0x478838` (two calls of `0x4922d3` at +0 and +6), reached only through `0x49667c`'s element pointer; stays original behind the bridge |
| `0x4031d4` | open | [mission_load.md](../reverse-engineering/mission_load.md) | no caller or reference found (see `0x402fc0`); runtime record `0x497348`, a loop of `new(18h)` objects with `0x402fc0` |

### Collision data

Contracts in [mission_load.md](../reverse-engineering/mission_load.md#20-collision-data). The COL loaders are hooks of
group `collision_data`; the two bridged original callees without a hook and the element constructors have rows:

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x40cd88` | H4, hooked | hook `collision_load` (group `collision_data`) | the COL resource of a type into the slot `[0x4a9824]` + 6 × the word `0x4987de` (incremented); stream `0x472f14`/`0x472f64` and the runtime record stay guest calls |
| `0x40ccf8` | H4, hooked | hook `collision_read` | the group reader (also `BASECOL.DAT` through `0x405fac`) |
| `0x40cc50` | H4, hooked | hook `collision_entry` | one group: part word, volume count, volumes (new[] `0x49667c`) |
| `0x40cc14` | H4, hooked | hook `collision_volume` | one volume's region word and spheres (also the weapon-list entry `0x40f8bc`) |
| `0x40c7c4` | H4, hooked | hook `collision_points` | the sphere array; the read size is 8 × count as a signed word |
| `0x40ccc8` | H4, hooked | hook `collision_bounds` | the bounding sphere of each volume (radius word +0Eh) |
| `0x40c5d0` | H4, hooked | hook `collision_radius` (accepts compare) | centre (16-bit box midpoint) and `dist3d_delta` radius |
| `0x40cec0` | H4, hooked | hook `collision_scan` | `weapon_fit`'s part-list scan: the first group with a matching volume gets `collision_bounds` |
| `0x40ce78` | H4, hooked | hook `collision_match` | the volume match of one group through `0x40ce28` |
| `0x40ce28` | H4 (stays original; next candidate) | [mission_load.md](../reverse-engineering/mission_load.md#20-collision-data) | the volume match of one volume: region word against the n weapon volumes, on a hit `0x40c834`; only caller `collision_match` |
| `0x40c834` | H4 (stays original; next candidate) | [mission_load.md](../reverse-engineering/mission_load.md#20-collision-data) | copies a weapon volume's spheres, moved by an offset vector, into a new[] array of the matched volume; only caller `0x40ce28` |
| `0x40c8be`, `0x40cf04` | H4 (stay original) | [mission_load.md](../reverse-engineering/mission_load.md#20-collision-data) | the element constructors that the readers pass to `0x49667c` (return the element; `0x40cf04` also EDX = element + 8); reached only through `0x49667c` |

### Damage data and part tables

Contracts in [mission_load.md](../reverse-engineering/mission_load.md#22-damage-data-and-part-tables); names after the
DMG grammar of [collision.md](../formats/collision.md). Hooks of group `damage_data`:

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x40d160` | H4, hooked | hook `dmg_load` (group `damage_data`) | the DMG file of a type by name into slot `[0x4a9828]` + 12 × counter `0x498854` (stream `0x472f14`, `dmg_read`, `stream_destroy`) |
| `0x40d09c` | H4, hooked | hook `dmg_read` | DMG descriptor: capacities, regions (18 bytes, `dmg_region_read` each) |
| `0x40cff8` | H4, hooked | hook `dmg_region_read` | region prefix, link list, `+0Ch` ← −1, `+0Eh` ← 0; also the weapon-list entry `0x40f8bc` |
| `0x40d050` | H4, hooked | hook `dmg_region_copy` | region copy (`_memcpy` 12h) with its own link list |
| `0x40d128` | H4, hooked | hook `dmg_capacity_set` | n capacity words by an index list |
| `0x40d240` | H4, hooked | hook `dmg_region_set` | n regions replaced by an index list (`dmg_region_copy`) |
| `0x40d284` | H4, hooked | hook `dmg_part_store` | region words `+0Ch`/`+0Eh` from `col_part_collect`'s buffers |
| `0x40cb84` | H4, hooked | hook `col_part_collect` | `col_part_gather` per COL record, outputs advanced by the gathered count |
| `0x40cafc` | H4, hooked | hook `col_part_gather` | pure gatherer of one COL record (no call; kept here, not with collision readers) |
| `0x40d2cc` | H4, hooked (corrected description) | hook `damage_state_ctor` | three word arrays (capacities, regions, regions) and the counts `+18h`/`+1Ah`; the third array is filled with bytes 01h, not zeroed |
| `0x40d354` | H4, hooked | hook `damage_state_bind` | `+0Ch` DMG, `+10h` COL, `+14h` owner; the shared 20-word block `[0x498864]` once |

### Group joins and the building footprint

Contracts in [mission_load.md](../reverse-engineering/mission_load.md#23-group-joins-and-the-building-footprint).
Hooks of group `group_joins`:

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x417aa8` | H4, hooked | hook `mech_join` (group `group_joins`) | the MECH join of `group_ctor`: group `A+0x45`, slot word `A+0x49`; at the load's default angle (word `A+0xc` = 8000h) the angles (0, 0, `G+0xa`); at the default height (`A+0x2e` = FFFF8000h) the group point or `route_point(G+4, word G+4)`, `terrain_height` + type word `+0x16` (+5000 for a type with word `+0x50`), the formation slot `+0x78`, stored at `A+0x26` |
| `0x421ee8` | H4, hooked | hook `flyer_join` | as `0x417aa8` without the height; afterwards a z of 0 becomes 5000 on every path |
| `0x405c3c` | H4, hooked | hook `building_join` | as `0x417aa8`, the angle plus the base layout's slot word, the point optionally moved to the layout's grid (fourth argument: `sx(word R)` of the group record), slots `+0x78` and `+0x10` (`shape_word`), the footprint `0x470ed4` with the shape word as radius, z = `terrain_height` |
| `0x470ed4` | H4, hooked | hook `footprint_marks` | the centre cell and every cell of the box within the radius (`dist_gate` of the cell corner `(x << s, y << s, z)` against the point), with its three lower neighbours, set to 1 through `0x470ddc`; callers `0x405d79` (the building join) and `0x425d08` (the actor load, buildings with a coordinate) |
| `0x470ddc` | H4, hooked | hook `cell_flag_set` | one byte `[[T+0xf0] + (y << [T+0x100]) + x]`; long enough for a hook (34 bytes, prologue `55 8b ec 8b 45 08` relocated); also reached per frame by the H3 cell clear `0x46e5a4` and at load by the flattening `0x470fe8`/`0x471128`; ECX (the shift with the value in its low byte) is dead at all eight call sites |

### Terrain zone load

Contracts in [terrain.md](../reverse-engineering/terrain.md#the-terrain-zone-load), live counts in
[mission_load.md](../reverse-engineering/mission_load.md) section 24. Hooks of group `terrain_load`; the callees
without a hook have verdicts here:

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x42789c` | H4, hooked | hook `zone_load` (group `terrain_load`) | the zone file `dat\zone<N>.dat` (cell shift, scale), the grid object, the materials and cells, the zone transform `0x4aaa84` (`euler_matrix`), `[0x4a0be0]`; the object table `0x4aaa74..0x4aaa7c` only for a negative zone (cleared); the block `0x427a97..0x427b39` is dead code |
| `0x46befc` | H4, hooked | hook `grid_ctor` | the grid object constructor (vtable `0x4a0c74`): twenty 8-byte elements in place (`0x49667c` with `0x49545b`), the height cache at +0D0h; it ignores its second argument |
| `0x495268` | H4, hooked | hook `height_cache_ctor` | the constructor of the grid's height cache (vtable `0x4a6a40`): a load-time allocation of three arrays, so own code; the cache's per-frame methods `0x4952e8`, `0x4952fc`, `0x49534c` stay H3 |
| `0x46c754` | H4, hooked | hook `terrain_materials` | the materials of `dat\mat0.dat` (13 records) and the grid from the first bitmap of `dba\zone<N>.dba` (`dts_load`, then `terrain_cells`); the text grid (`_fopen`/`_fscanf`) runs only for a name without ".dba", never in the suites |
| `0x46c4c4` | H4, hooked | hook `terrain_cells` | heights from the bitmap (rows reversed), one stream-A draw per block (1,024, 4,096 or 16,384 per zone), the material bits copied within a block |
| `0x470d74` | H4, hooked | hook `cell_mark_set` | one mark byte by cell pointer; hookable (prologue 1, 2, 3 bytes); also called by the base marks `0x47136c` |
| `0x4952e8` | H3 (stays original) | [terrain.md](../reverse-engineering/terrain.md#the-terrain-zone-load) | the height cache's setter ([C+8] = offset, [C+4] = scale): called by `terrain_cells`, `terrain_materials` and per frame by the terrain draw `0x46cb9c`; the cache class is the draw's (`0x4952fc`, `0x49534c`) |
| `0x4715e1` | H4 (stays original) | [terrain.md](../reverse-engineering/terrain.md#the-terrain-zone-load) | the element constructor of the cell array: writes nothing (EAX = the cell, EDX = +1, ECX = +7); reached only through `0x49667c`'s element pointer |
| `0x470e20` | dead | [terrain.md](../reverse-engineering/terrain.md#the-terrain-zone-load) | a span fill of marks (table `0x6b5068`, `cell_mark_set(G, cell, 1)`): no call site and no dword reference in the file |

### Stage-2 callbacks with type data

Contracts in [mission_load.md](../reverse-engineering/mission_load.md#25-stage-2-callbacks-with-type-data). Hooks of
group `stage2_types`; the three element constructors that the hooks reach only through `0x49667c` stay original (they
are also in the Verdicts table):

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x40fc8c` | H4, hooked | hook `weapon_types` (group `stage2_types`) | stage-2 record 16: `wpntex.dba` (bitmap `0x4a9ae8`, texture table `0x4a9aec`, bitmap released), shapes `mechwpn2` into `0x4a98f4`/`0x4a98f8`, weapon list `weapons` (58h-byte entries at `0x4a98ec`, count `0x4a98f0`, `weapon_entry` each, name pointer +52h, index +56h), `proj` (36-byte records at `0x4a98fc`, count `0x4a9900`) |
| `0x40f8bc` | H4, hooked | hook `weapon_entry` | one entry: `dmg_region_read` +0, `collision_volume` +12h, 30h file bytes +22h, word +48h ← 0 |
| `0x40a818` | H4, hooked | hook `rocket_types` | stage-2 record 11: 14-byte records at `0x4a96d0`/`0x4a96d4`, the word `0x4a96dc` = max(0, word +0 + 1), shapes into `0x4a96d8` |
| `0x40ade0` | H4, hooked | hook `bullet_types` | stage-2 record 13: the flag `0x498514`, 14-byte records at `0x4a96f8`/`0x4a96fc`, maximum word `0x4a9704`, shapes `0x4a9700`, bitmap `0x4a9706`, texture table `0x4a970a` |
| `0x40b6e0` | H4, hooked (corrected description) | hook `beam_types` | stage-2 record 14: also 120 step words at `0x4a9712`; 6-byte records at `0x4a9804` (count in a local); `0x4a9808` holds the texture table of `beamtex.dba`, the bitmap is released |
| `0x41fdb0` | H4, hooked (corrected description) | hook `mech_forms` | stage-2 record 21: `_memset(0x4a9d08, FFh, 55h)`, `0x4a9d06` ← 0, 28-byte forms at `0x4a9d6c`/`0x4a9d68`; seven bitmaps (not one `.dba`), texture tables at `0x4a9d76 + 8k` |
| `0x422d90` | H4, hooked | hook `flyer_forms` | stage-2 record 23: `_memset(0x4a9dcc, FFh, 22h)`, `0x4a9dca` ← 0, 18-byte forms at `0x4a9dfc`/`0x4a9df8` |
| `0x41178d` | H4 | bridged through `0x49667c` | element constructor of the weapon-list entries: empty (computes three addresses, returns its argument); stays original |
| `0x4207bb` | H4 | bridged through `0x49667c` | element constructor of the MECH forms: `0x49667c(form, 4, 7, 1, 0x492428)` in place; stays original |
| `0x423078` | H4 | bridged through `0x49667c` | element constructor of the FLYER forms: `0x49667c(form, 6, 3, 1, 0x4922d3)` in place; stays original |

### MECH and FLYER type loads

Contracts in [mission_load.md](../reverse-engineering/mission_load.md#26-mech-and-flyer-type-loads). Hooks of group
`type_loads`; the bridged element constructors have rows:

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x42049c` | H4, hooked | hook `mech_types` (group `type_loads`) | the MECH type table: names (`0x492d00`, `mechs`), the 21Eh-byte type records (new[] + `0x49667c`), the H3 display set-up `0x4378d8` (guest), each selected type |
| `0x4201a8` | H4, hooked | hook `mech_type_load` | one MECH type: OVL record, LOD shape list (`0x474cd8`), `lod_remap`, the Q16 scaling with `multiply_q10` or the "fm" record, the gun list, the node lists, the H3 damage display `0x4379cc` (guest), the debris set `0x40874c` (guest) |
| `0x420090` | H4, hooked (corrected description) | hook `lod_remap` | not a flag array: for the LOD shapes 1 .. n−1 it builds remap rows (memset FFh) from the shape virtual +20h of each part of the type's part list (rec+30h, which covers the scaling flag rec+50h at bytes 32/33) and walks each LOD shape with `node_remap` |
| `0x41ff88` | H4, hooked | hook `node_remap` | recursive walk by class id (virtual +0): TSGroup and TSCellAnimPart remap their word +4, TSBSPPart its word list (0 < w < limit, table byte ≥ 0), containers recurse |
| `0x41ff68` | H4, hooked (accepts compare) | hook `remap_word` | a word ≥ 0 becomes the sign-extended table byte; a negative word stays |
| `0x41ff10` | H4, hooked | hook `mech_type_select` | the index byte of a selected mech's type (new types: the count `0x4a9d06`, a gun slot, the first one's base word `0x4a9d70`); every call reserves a COL and a DMG slot |
| `0x40fee8` | H4, hooked | hook `gun_list_load` | the gun list (`GUNLIST.CPP`, 26-byte entries) into the mount entry `0x40fea0(type)`; the pushed type word carries the caller's EBX high word (dead) |
| `0x40fc50` | H4, hooked | hook `gun_classes` | the entries' class words (FFFFh for class bytes ≥ 4) |
| `0x40304c` | H4, hooked | hook `node_list` | new[] of n node pointers, each the shape virtual +24h of a class word or 0 |
| `0x40feb4`, `0x40cbd4`, `0x40d200` | H4, hooked (accept compare) | hooks `gun_slot_base`, `col_slot_base`, `dmg_slot_base` | the running slot words `0x4a98e8`, `0x4987dc`, `0x498856`; 22 bytes each, hookable (`66 A1` prologue) |
| `0x422fb4` | H4, hooked | hook `flyer_types` | the FLYER type table (`flyers`, 70h-byte records) when a FLYER is selected |
| `0x422ed0` | H4, hooked | hook `flyer_type_load` | one FLYER type: records, shape `0x474c38` (guest), `dmg_load`, `collision_load`, the "fm" ratio |
| `0x422e90` | H4, hooked | hook `flyer_type_select` | the index byte of a selected flyer's type; only a new type reserves a DMG and a COL slot |
| `0x41ff00`, `0x422e80` | H4 (computed in core) | hooks `mech_type_index`, `flyer_type_index` (group `sim_leaves`) | the type-index byte leaves; not called when `mech_ctor`, `flyer_ctor` and the hooks are replaced |
| `0x47e068`, `0x47e08d` | pure (computed in core) | — | the Q16 quotient and product helpers (`enter` bodies, ECX untouched); computed wherever a hook calls them |
| `0x420817`, `0x42067c` | H4 (stay original) | [mission_load.md](../reverse-engineering/mission_load.md#26-mech-and-flyer-type-loads) | the MECH type record's element constructor (ten 18h-byte entries at +0ECh through `0x49667c`) and their constructor (vtables, `shape_word_block(+10h, 0)`); reached only through `0x49667c` |
| `0x4207db`, `0x4117a5`, `0x423098` | H4 (stay original) | [mission_load.md](../reverse-engineering/mission_load.md#26-mech-and-flyer-type-loads) | the element constructors of the node-list array (clears the pointer, counts twice), of the gun entries (EDX = element + 10h) and of the FLYER type records (writes nothing); reached only through `0x49667c` |

### Base flattening and the base-mark raster

Contracts in [terrain.md](../reverse-engineering/terrain.md#base-flattening-and-the-base-mark-raster), live counts in
[mission_load.md](../reverse-engineering/mission_load.md) section 27. Hooks of group `base_flatten`; the bridged
callees `0x470ddc`, `0x470d74` and `0x46c2e0` have their own verdicts.

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x47129c` | H4, hooked | hook `base_flatten` (group `base_flatten`) | after the actor load (`0x4263d9`): every 8-connected region of marked cells gets the integer mean of its heights (`flood_sum`, IDIV, `flood_fill`), then the shading pass `0x46c2e0` (H3) as a guest |
| `0x470fe8` | H4, hooked | hook `flood_sum` | recursive sum of the heights and count into `0x6b5470`/`0x6b5474`, visit bit 1 through `cell_flag_set`; the replacement runs the recursion as a loop with an explicit path stack in the original's order |
| `0x471128` | H4, hooked | hook `flood_fill` | recursive store of the mean, visit bit 2; the write-through flag of the neighbours (x, y+1), (x+1, y+1), (x+1, y) also sets the row and column beyond the region; iterative like the sum |
| `0x47136c` | H4, hooked | hook `mark_raster` | the base layout's marks (`cell_mark_set`) and material bytes +0Fh around a building (unless `-l`); the material byte is presentation data the simulation's `terrain_height` also reads (bits 0..1), written exactly |
| `0x470e00` | H4, hooked | hook `cell_flag_get` | one mark byte by (x, y); 32 bytes, hookable (prologue 1, 2, 3 bytes); also read per frame by the H3 cell-object draw `0x46e5a4`; accepts compare (a pure read) |

### Building types and pools

Contracts in [mission_load.md](../reverse-engineering/mission_load.md) section 25 ("building types and pools"). Hooks
of group `building_types`; the five element constructors that the hook `base_types` reaches only through `0x49667c`
stay original (they are also in the Verdicts table):

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x405fac` | H4, hooked | hook `base_types` (group `building_types`) | stage-2 record 3: `bforms` (dword count `0x4a95a8`, 1Ch-byte forms at `0x4a95ac` with element arrays and byte lists), `lc_wpns` (16h-byte records at `0x4a95bc`, count `0x4a95b8`), `bases` (3Ch-byte types at `0x4a959c`, count `0x4a9598`, 1Eh-byte children, 6-byte COL slots at `0x4a95b0`), `basecol` (`collision_read` per type), the count words `0x4a9572`/`0x4a9578`/`0x4a9580`, the hulk shapes `0x4a9584` and tables `0x4a957c`/`0x4a9574`, the textures `basetex`/`vehtex` (`0x4a958c`, `0x4a9594`, bitmaps released), the debris set `0x40874c(0x4a95c0)` |
| `0x405df4` | H4, hooked | hook `base_pools` | the building pool (`pool_create(new(12h), count, 26Dh)`) at `0x4a95a0` and its removal list (25 slots) at `0x4a95a4`; pool/list pair |
| `0x406a5c` | H4 | bridged through `0x49667c` | element constructor of the forms: empty, returns its argument; stays original |
| `0x406a64` | H4 | bridged through `0x49667c` | element constructor of the form elements: empty; stays original |
| `0x406a6c` | H4 | bridged through `0x49667c` | element constructor of the `lc_wpns` records: empty (EDX = +8); stays original |
| `0x406a77` | H4 | bridged through `0x49667c` | element constructor of the building types: empty (EDX = +0Ah, ECX = +18h); stays original |
| `0x406a85` | H4 | bridged through `0x49667c` | element constructor of the type children: empty (EDX = +0Ah, ECX = +10h); stays original |

### Effect pools with resources

Contracts in [mission_load.md](../reverse-engineering/mission_load.md#31-effect-pools-with-resources). Hooks of group
`effect_pools`; the element constructor that two of the hooks reach only through `0x49667c` stays original (it is
also in the Verdicts table):

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x407b54` | H4, hooked | hook `explosion_pools` (group `effect_pools`) | stage-2 record 5: pool (40 x 5Bh) `0x4a961e`, list (30) `0x4a9622`, 15 bitmap pairs `0x4a9638` (13 with the low-memory word `0x4d2680`), shapes `explos`/`explos2` `0x4a9630` and their map `0x4a963c`, 28h-byte records `0x4a9628` (count `0x4a962c`), `0x4a9634` = max(word +0) + 1, +26h of every shape |
| `0x408860` | H4, hooked | hook `debris_pools` | stage-2 record 6: pool (150 x 61h) `0x4a9642`, list (40) `0x4a9646`, `debris_set(0x4a9650, def_deb, 0)`, `0x4a964c` = 0, words `0x4a9664..0x4a9668` = 6000, 16000, 420 |
| `0x40874c` | H4, hooked | hook `debris_set` | the shapes (`.dts`) into set+6 (count +0Ah), the 10-byte records at [set] (count +4, one `debris_record` each), +26h of every shape = the tag when nonzero; FS:0 restored from ECX |
| `0x4083f8` | H4, hooked | hook `debris_record` | see the Verdicts row; returns AX = the largest piece word +0, ECX = the two local words |
| `0x4097a8` | H4, hooked | hook `flat_pools` | the guard byte `0x498098`, shapes `flat`/`flat2` (flag word) `0x4a9698`, their bitmap pair `0x4a969e` (bitmap released), pool (150 x 41h) `0x4a968d`, list (25) `0x4a9691` |
| `0x409a34` | H4, hooked | hook `meteor_pools` | stage-2 record 9: pool (10 x 59h) `0x4a96aa`, list (25), shapes `meteor` `0x4a96b4`, bitmap `impact` `0x4a96b8` |
| `0x46b1a8` | H4, hooked | hook `fire_pools` | stage-2 record 55: pool (10 x 5Bh) `0x6b4f30`, list (10), shapes `fire` `0x6b4f28`, the per-shape bytes (leaked `new[]` block), two bitmap pairs `0x6b4f2c`, `0x6b4f38` = 0 |
| `0x408fb8` | H4, hooked | hook `smoke_balls` | pool (200 x 4Ch) `0x4a966e`, list (25), shapes `smoke` `0x4a9678`, bitmap `smoke` `0x4a967c` |
| `0x46bbc1` | H4 | bridged through `0x49667c` | element constructor of the bitmap pairs: empty, returns its argument; stays original |

### World load

Contracts in [mission_load.md](../reverse-engineering/mission_load.md) section 29. Hooks of group `mission_world`;
the four leaves only the world load calls are hooks too, and the two bridged callees without a verdict have rows here
and in the Verdicts table:

| Function | Verdict | Package | Notes |
|---|---|---|---|
| `0x424308` | H4, hooked | hook `world_load` (group `mission_world`) | script pass 1: mission.var, player.mec (variable-length records), mission.str, the script lists, the selection and compaction, the MEC actors appended, the type selects, the MECH and FLYER pools and lists, `base_pools`, the actor list and slot tables, the type loads, `zone_load`, the result pairs |
| `0x411df8` | H4, hooked | hook `actor_list_init` | the actor list `0x4a9af8`: `new[](4 × count)` and the capacity word +4 |
| `0x40fecc`, `0x40d218`, `0x40cbec` | H4, hooked | hooks `gun_slot_table`, `dmg_slot_table`, `col_slot_table` | the gun-list, damage-slot and collision-slot tables sized by the running slot words of the type selects (6, 12, 6 bytes per slot; the last two only for a nonzero word); hookable (prologues `0F BF /r`, `53 66 8B /r`) |
| `0x405ebc` | platform (stays original) | [mission_load.md](../reverse-engineering/mission_load.md) | the shape-list cache of a building type (`0x474de4`) and the material pointer +26h; reads only its first argument (the factory byte the world load pushes is unread) |
| `0x42e010` | H3 (stays original) | [mission_load.md](../reverse-engineering/mission_load.md) | the world loader: its globals (`0x4cfcc4..0x4cfd07`, `0x49aee0..0x49aefb`, `0x4a5b00..0x4a5b0b`) are read only by drawing and presentation code; its simulation callee `0x4097a8` is H4 (the effect pools) |
| `0x4539a0` | platform | [mission_load.md](../reverse-engineering/mission_load.md) | the STR string-pointer splitter (count = the body's word 0); bridged by `world_load` |
| `0x427207` | H4 (stays original) | [mission_load.md](../reverse-engineering/mission_load.md) | the timers' element constructor: empty; reached only through `0x49667c` |
