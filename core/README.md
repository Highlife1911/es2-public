# Portable simulation core

`core/` holds our own implementations of original DBSIM simulation functions.
The proxy runs them inside the original process behind hooks
([hooks.md](../docs/reference-game/hooks.md)); our own processes (`sim/`,
`shell/`) host the same code.

Rules for all modules:

- C17 with no Win32, SDL, allocation, file access, clock or hidden global
  state. Callers own every buffer and supply original tables (for example the
  quarter sine table) as borrowed pointers; no original table or asset is
  stored in this repository.
- Original memory layouts are read and written with explicit little-endian
  byte operations, so unaligned and aliased buffers behave as in the original.
  Integer widths, wrapping, low-32-bit products and arithmetic right shifts are
  explicit; there is no signed overflow or negative shift in C.
- Original CPU faults (division by zero, quotient overflow, invalid pointers)
  are outside the API: callers must stay in the documented domain, the core
  does not emulate the fault.
- `_registers` variants return the original's full EDX:EAX, including
  incoming-register-dependent and pointer-derived bits, because callers in the
  game consume them. The plain variants return only the useful result.

## Modules

A module `<m>` is the target `//core:<m>`: the header `include/es2/core/<m>.h`
(it names the original addresses, registers and the domain of every function),
`src/<m>.c` and the test `<m>_test` from `tests/test_<m>.c`
([core_module.bzl](../bazel/defs/core_module.bzl); exceptions under Tests).
`//core` links all modules in the proxy's static link order. The order fixes
the proxy DLL's bytes: a new module is appended. A module's hook group has its
name unless the row says otherwise. Hook names, groups and VAs are those of
`tools/reference-game/proxy/hook_registry.h` and the generated table in
[hooks.md](../docs/reference-game/hooks.md#registry).

| Modules | Role |
| --- | --- |
| `rng`, `divide`, `geometry`, `timing`, `movement` | additive RNG 492ee0/492f24; the x86 IDIV/DIV with its fault codes and the raise of that fault by a host divide (no hook: the shield balance, cockpit sync, type loads and HUD speed check their divides with it); octagonal distances 4928d0 …, distance display 47d694; time scale 467894 and timers 467a48/467aa8/467ac4; heading cos/sin, Q8 multiply, movement step, HUD speed, point transform, vector subtract |
| `matrix`, `pose`, `trig`, `energy`, `track` | Euler matrix, composition, transpose, rotation, pose-to-matrix; pose and scalar interpolation, track count, `clip_pose` 478ef4; inverse sine, angle ratio, matrix-to-angles; Q10 multiply, word approach, energy accumulator; the five track wrappers 478bc4, 478f6c, 478ff4, 4790b4, 479194. Groups are named by function (`euler_matrix`, `pose_interpolate`, `inverse_sine`, `multiply_q10`, `track_*`) |
| `animation`, `track_pose` | manager reset, movement step, track advance with its subtree, clip/target/rate, actor and ground animation, snapshot/rollback, ground vehicle update 41a360, the eight clip methods 478760 … 47882c (group `manager_slots`); track seek, turret aim |
| `terrain`, `terrain_load`, `world`, `camera`, `camera_view` | terrain height 46e180, segment walk 46e980, line/plane 47e610 (groups `segment`, `line_plane`); the zone load, grid object, materials, cells and base flattening (iterative floods, explicit path stack; groups `terrain_load`, `base_flatten`); terrain draw pass helpers; camera update 4011a0 with `terrain_face` (group `math`), the outside camera and view subject |
| `collision`, `collision_data`, `col`, `obstacle` | ground collision 418f74, swept terrain/object query 426528, sweep receiver object query (group `object_query`); the COL/DMG readers and part tables of the mission load; the COL traversal; static-obstacle test 404ae4 (140-entry frame list, overflow modelled), convoy collision test 46a614 |
| `effects`, `effect_pools`, `pools`, `debris_burst`, `meteor`, `fire`, `removal`, `pool_iter`, `destructors`, `logic` | attached transforms and ground effects 4195c8, effect creation with the pool allocator 471b30 (group `effect`); the pools with resources; smoke, debris, meteor, fire, explosion updates; deferred object removal; the pool iterator; object destructors (`fire` also holds `object_position`, `shape_word` of `manager_slots`) |
| `damage`, `region_damage`, `shield`, `death`, `results`, `results_writer` | 3-D metric/forwarder, point heading, shield absorption, vehicle/building hit bodies, building part damage, the damage data of the mission load (groups `damage`, `damage_data`); region absorb/spread/damage (`region_damage`, `region_break`); the shield balance; the player's death sequence; the results records and writer |
| `weapons` (with `building_weapons.h`), `weapon_helpers`, `weapon_system`, `weapon_slots`, `weapon_classes`, `weapon_eject`, `projectile`, `mount`, `panel_weapons`, `fit_leaves`, `stage2_types` | selectors, mask merge, radius, maintenance, fire and preparation, bullet/projectile flight, emitters, immediate impact, the building turret 404100, the beam constructor (groups `weapons`, `beam`); the weapon group pass; the player's weapon system and range gauge; the weapon-class methods, constructors and gauge syncs (groups `weapon_methods`, `weapon_ctors`, `weapon_syncs`); eject; steering; mount helpers; panel selection; the weapon fit; the type data of stage 2 |
| `vehicle`, `vehicle_control`, `vehicle_update`, `torso`, `flyer`, `flyer_part`, `flyer_states`, `ai_motion`, `mech_fly` | vehicle leaves and aux leaves (groups `vehicle`, `vehicle_aux`, `part_helpers`); vehicle control; the per-vehicle update 41aa5c; torso servos; the flyer flight model and controls (groups `flyer`, `flyer_control`), part damage and AI states; flyer move step and formation steering; the MECH player-fly movement |
| `ai`, `ai_helpers`, `ai_order`, `ai_behaviour`, `ai_behaviour2`, `mech_order`, `mech_combat`, `mech_travel`, `mech_notices`, `mech_part_hit`, `lc_base`, `convoy` | AI obstacle steering, route and behaviour helpers, order helpers, the AI behaviours and combat/travel states of the MECH, the wingman order, MECH notices and part hit, the LC_BASE update, the `VEHICLE` update chain 46a6d4 … 46a89c |
| `building`, `base_update`, `building_types`, `group_joins`, `groups`, `formation`, `node`, `actor_pass`, `actor_step`, `actor_leaves`, `actor_virtuals`, `hit_test`, `sim_leaves`, `bridged_leaves`, `object_shapes` | building target selection and destruction (groups `building`, `building_aim`); the row-10 building update (group `base`); the building module of base.cpp; group joins and footprint marks; group activation and orders; the formation slot; graph nodes; the actor-list pass and per-actor step; the actor slot leaves (groups `actor_leaves`, `slot_leaves`) and virtual getters; the actor hit tests; the remaining simulation leaves; the leaves own code still reaches through the guest bridge (the swap 47df04 is computed in `convoy.c`/`obstacle.c`); the object shape set-up |
| `mission`, `mission_objectives`, `mission_end`, `mission_root`, `mission_load`, `type_loads` | the trigger pass, route callback, timers and status check (groups `mission`, `route`); objectives and the outcome poll; the mission end with the process exit code 4d27b8; the mission-load root; the load's pools, weapons, constructors, actors and world (groups `mission_load`, `mission_pools`, `mission_weapons`, `mission_ctors`, `mission_actors`, `mission_world`); the MECH and FLYER type loads |
| `logic_frame`, `control_record`, `input`, `input_dispatch`, `key_axes`, `keyboard`, `commands`, `mouse_events`, `cockpit_target`, `cockpit_sync`, `render_root`, `panel_frame` | the logic frame 45f570; the control record builder and keyboard axes; the input children and the dispatcher 460870; the keyboard message handler 477bec (the first stdcall hook); command keys; the GUI mouse queue; cockpit target cycling and synchronisation; the render root with the stage flushes (groups `render_root`, `stage_flush`); the panel frame |
| `hud_helpers`, `hud_keys`, `throttle`, `map_pick`, `hdd_map`, `hdd_map_keys`, `hdd_orders`, `hdd_exec`, `mfd_orders`, `mfd_keys` | the cockpit panel visibility test (the other HUD helpers are service rows of `sim/`); HUD key handler; throttle slider; the HDD map pick, frame, keys, order controls (groups `hdd_orders`, `hdd_keys`) and button executor; the MFD order path and key handlers |
| `sfx`, `math_lib`, `transform_helpers` | the sound API calls and the voice/lip timeline; texture edge setup 48aaac (the rest of the graphics library is not core code); enter-prologue helpers 47ff8c, 4801d4 |
| `vfs_stream`, `asset_load`, `config` | the VOL-aware read stream over `formats/` (group `vfs`); the persistent-object loader and bitmap factories (group `assets`); the configuration readers and tape header records over `formats/cfg` |
| `rtl`, `rtl_heap`, `rtl_io`, `rtl_start` | the CW3220 runtime imports: 20 pure imports over a byte accessor, the heap model, the 21 file and console imports, the lifecycle (`__startup`, exit, atexit, abort). No hook group: the proxy's import shim and the own `cw3220.dll` (`rtl/`) use them |

## Guest memory and callbacks

Functions that walk the game's object graphs take an `es2_guest_memory`
([guest.h](include/es2/core/guest.h)): 32-bit guest addresses that wrap modulo
2^32, mapped by the host (identity inside the original process, an arena in
tests). They replay the original's reads and writes in order, so unproved
object extents need no bounds.

Where the original calls code that is not in core, the core calls back into
the host (`es2_guest_call`, `es2_effects_call`, `es2_weapon_call`, the
`es2_track_advance` and `es2_actor_step` callbacks, `es2_clip_classes.other`,
`es2_ground_calls`). The proxy routes them to the original addresses; each
callee keeps its own hook mode.

- `es2_guest_call` arguments may be guest addresses of the core's own stack
  buffers, so `collision` (and every host of `es2_guest_call`) requires guest
  and host addresses to coincide: it is a 32-bit-process API.
- `es2_effects_call` and `es2_weapon_call` add entry EAX/EDX/ECX (in and out)
  to the cdecl callback contract. Feedback 434144 can preserve incoming EDX;
  the host must pass it through. The 12-byte scratch point of `effects` is a
  caller-owned guest address, so it works with an arena on x86 or x64. The
  ground-update effects callback also receives entry EAX/EDX, including the
  last collision/clip/rollback result on the no-track path.
- The five track wrappers leave byte 19 of a local matrix uninitialized in the
  original, so that output byte can be stack residue. Their core functions take
  it as `padding` (the proxy passes 0); see the
  [padding contract](../docs/reverse-engineering/player_movement.md#padding-contract-for-the-five-wrappers).

## RNG

The DBSIM additive generator described in
[rng.md](../docs/reverse-engineering/rng.md). The caller owns each stream and
supplies the original seed data; the original 112-byte table is not present in
this repository. The state is caller-owned byte storage of exactly 114 bytes
(56 little-endian words at `0x00..0x6f`, source/destination indices at `0x70`
and `0x71`, set to 24/55 by `es2_rng_init`); copying these bytes saves and
restores a stream. Draws require both indices in `0..55`, `es2_rng_bounded` a
bound in `1..32767` (one draw, low 15 bits, no zero-bound guard, no rejection
sampling). Invalid indices or bounds are not normalized; there is no
synchronization. `rng.h` has the rest.

## Numeric contracts the headers do not state

- Distances and coordinates are raw `uint32_t` patterns with wrapping
  arithmetic. `es2_distance3` is the player-gate metric: separately truncated
  weights on the three sorted magnitudes.
- `es2_time_scale`: elapsed times 256, divided toward zero by the reference,
  the quotient's signed low word clamped to 64..450 with its upper word
  preserved; the full EAX is returned. `es2_timer_arm` sign-extends the
  duration's low word and shifts it left 11. Timer storage is three or five
  bytes; the tag byte at +0 is unchanged.
- `es2_heading_sin` is `es2_heading_cos` of the heading minus 16384 (SAR/carry
  rounding and quarter-table reflection; the caller supplies 1025
  little-endian words). `es2_movement_step` clamps to -32767..32767. `es2_hud_speed` covers both
  actor/config branches and keeps the sign-dependent bias of the original's
  full-width branch; a zero divisor or a quotient outside signed32 raises the
  original's fault through `es2_divide_raise`.
- `es2_transform_point` and `es2_vector_subtract` return void; the adapter
  reproduces the mode-dependent EAX and the destination pointer.
- `es2_angle_ratio_valid`: INT_MIN is illegal in either argument when y != 0.
  Inverse sine accepts all 16-bit inputs, also values outside geometric Q14.
  Pose interpolation is sequential over six words and decides the negative
  half-turn tie by the signed low word.
- `es2_track_clip_registers` (4792ac) keeps the virtual +20 and `clip_pose`
  boundaries behind the generic guest-call callback; entry EAX supplies the
  high frame-argument word.

## Damage and weapons

- `damage`: the shield helper writes one selected word of a four-byte
  caller-owned buffer, so it is bounded state, not a pure leaf. The two hit
  bodies use `es2_damage_context` (guest function addresses, RNG address,
  observer, a caller-owned 12-byte scratch point); every game call, also
  heading, shield, RNG and distance, goes through `es2_guest_call`, and V58/V74
  are resolved at each call. The falloff truncates the product to 32 bits,
  sign-extends it, then divides by the signed range word; zero divisors and
  INT_MIN/-1 overflow are outside the API. The building acceptance gate is
  always true, but its RNG advance is kept. `es2_building_part_registers`
  (404d70) keeps its state and configuration pointers across callbacks,
  reloads the flag array at the write and reads the selector table through
  guest memory, never a copy.
- `weapons`: contracts and the live probe request are in
  [weapons.md](../docs/reverse-engineering/weapons.md). The graph functions
  take `es2_weapon_context` or `es2_weapon_graph` (scratch sizes in
  `weapons.h`). Radius forwards ECX's high word in the V70 range slot. The
  proxy bridge is x86-specific; the core and the mapped-arena tests work on
  x86 and x64.
- Stack residue: the original leaves some bytes uninitialized (the two matrix
  padding bytes of the preparation, the excluded-actor word of the flight
  packet, the turret's composed-matrix byte 19). The native adapter captures
  them from the entry ESP before detour dispatch and passes them in the
  context; the core never guesses a host stack layout or normalizes them. The
  emitter shim runs preparation and impact at the original outgoing stack
  coordinate behind a 128 KiB guard-probed gap ("Nested frame residues" in
  weapons.md). Exception unwinding through a detour is outside the framework's
  supported domain.
- `es2_impact_registers` (40bf74): its runtime callback uses a synthetic frame at
  scratch+208 for Borland's initializer 49630c and restores the prior thread
  exception head at normal return; that head is the original's EAX result and
  EDX is the last allocator or constructor result.
- Live compare is refused for the weapon graph (callback writes are not bounded
  regions), the two damage hit bodies and the turret (their lifecycle methods
  and point transforms stay guest code); the four bounded damage helpers
  support it. The per-hook table is in
  [compare_mode.md](../docs/reference-game/compare_mode.md).

## Tests

`bazel test //core/...` runs the synthetic tests (add
`--platforms=//bazel/platforms:windows_x86_64` for x64; `core_test` also
builds `<name>_wasm`). They hold only generated tables and test
instruction-derived expectations, not live equivalence: that is the job of the
[in-vitro harness](../tools/reference-game/invitro/README.md) and the oracle
regression. `collision`, `camera`, `ai`, `node` and `weapon_helpers` have no
unit test of their own; the ground collision and the swept query rely on the
harness and the oracle.

| Test | What it checks |
| --- | --- |
| `<module>_test` | the module's cases from `tests/test_<module>.c` |
| `per_vehicle_test`, `input_frame_test`, `base_parts_test` | `vehicle_update`, `input_dispatch`, `base_update`; the names avoid Windows' installer heuristic (error 740 for "update", "patch") |
| `rng_test`, `divide_test` | the generator against an independent model; the divide's code, quotient and remainder at the IDIV and DIV boundaries (the sweep against the CPU is `//sim:fault_paths_test`) |
| `movement_test`, `math_test` | all movement functions with an independently generated exhaustive heading table; matrix, pose, inverse trig and energy (`matrix`, `pose`, `trig`, `energy`) |
| `clip_pose_test`, `track_test`, `animation_test`, `track_advance_test`, `track_clip_test`, `terrain_test`, `line_plane_test` | the animation and terrain walkers on mapped arenas; `track_clip_test` covers all pairs of the indices 0/1/32767/-32768/-1, callback graph changes, write order and the complete return pair |
| `damage_test`, `effects_test` | guest memory, strict boundaries, full return pairs, callback order, mapped scratch and latch-pointer quirks (x86 and x64) |
| `object_query_test`, `effect_create_test`, `beam_test`, `bullet_test`, `route_test`, `building_aim_test`, `region_break_test`, `weapon_leaves_test`, `rotation_helpers_test`, `damage_data_test` | the sub-areas of `collision`, `effects`, `weapons`, `mission`, `building`, `region_damage`, `matrix` and `damage` that have a test of their own |

### Opt-in original-data check for the RNG

`//core:rng_real_helper` is a manual target. Its Python standard-library driver
exits with `SKIP` before reading any original data when `ES2_DATA_ROOT` is
unset or empty. The root names the extracted CD directory containing `VER95`,
or a directory containing `extracted/cd/VER95` (`build` of this repository).

```powershell
& bazel build //core:rng_real_helper
$rngBin = (& bazel info bazel-bin).Trim()
$env:ES2_DATA_ROOT = 'build\extracted\cd'
& python -B core/tests/check_rng_real.py --helper (Join-Path $rngBin 'core/rng_real_helper.exe')
```

The driver verifies the `DBSIM.EXE` and the table against the pins of
rng.md, reads exactly 112 bytes at file offset `0xa5b40` and pipes them into
the helper through stdin; it neither saves the table nor executes original
code. The helper initializes our RNG and returns eight outputs, which the
driver checks against the documented static predictions.
