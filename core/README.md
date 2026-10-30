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

| Library / header | Original functions | Hooks |
| --- | --- | --- |
| `//core:rng` / `rng.h` | additive lagged RNG 492ee0, bounded draw 492f24 | `rng_next`, `rng_bounded` |
| `//core:geometry` / `geometry.h` | 2-D/3-D octagonal distances 4928d0, 492a45, 492a9a, 47de4c, 49288c; distance display 47d694 | `dist2d_*`, `dist_octagonal`, `dist_gate`, `distance_display` |
| `//core:timing` / `timing.h` | time scale 467894, timers 467a48, 467aa8, 467ac4 | `time_scale`, `timer_*` |
| `//core:movement` / `movement.h` | heading cos/sin, Q8 multiply, movement step, HUD speed, point transform, vector subtract | the movement hooks |
| `//core:matrix` / `matrix.h` | Euler matrix, composition, transpose, rotation, pose-to-matrix | `euler_matrix`, `matrix_compose`, `rotate_point`, `matrix_transpose`, `pose_matrix` |
| `//core:pose` / `pose.h` | pose and scalar interpolation, track count, `clip_pose` 478ef4 (through `guest.h`) | `pose_interpolate`, `scalar_interpolate`, `track_count`, `clip_pose` |
| `//core:trig` / `trig.h` | inverse sine, angle ratio, matrix-to-angles | `inverse_sine`, `angle_ratio`, `matrix_angles` |
| `//core:energy` / `energy.h` | signed Q10 multiply, word approach, energy accumulator | `multiply_q10`, `approach_word`, `energy_accumulate` |
| `//core:track` / `track.h` | the five track/pose matrix wrappers 478bc4, 478f6c, 478ff4, 4790b4, 479194 | `pose_parent`, `track_*` |
| `//core:animation` / `animation.h` | manager reset 478b7c, movement step 40250c, track advance 478d38 with its subtree, track clip/target/rate, actor and ground animation, snapshot/rollback, ground vehicle update 41a360; the eight clip methods 478760 … 47882c by address | `manager_reset`, `animation_step`, `track_advance`, `track_clip`, `track_target`, `track_rate`, `actor_animate`, `ground_animate`, `actor_snapshot`, `actor_rollback`, `ground_update`; the clip hooks of group `manager_slots` |
| `//core:terrain` / `terrain.h` | terrain height 46e180; the terrain segment walk 46e980 with its wrapper 428048 and callees; line/plane intersection 47e610 | `terrain_height`, the `segment` subtree, group `line_plane` |
| `//core:collision` / `collision.h` | ground collision 418f74; the swept terrain/object query 426528 with 426b04/427238; the sweep receiver object query 427da8, 4273c8, 4927f0 | `ground_collide`, the sweep hooks, group `object_query` |
| `//core:effects` / `effects.h`, `effect_create.h` | attached transforms and ground effects 4195c8; effect creation 407f1c, 402188, 407604 with the pool allocator 471b30 | `ground_effects`, group `effect` |
| `//core:damage` / `damage.h` | 3-D metric/forwarder 47de72/492aaf, point heading 492934, shield absorption 413c68, vehicle/building +70 4187d0/404f20 and building +74 404d70; the damage data (damage.cpp) and part tables of the mission load: dmg_load 40d160, dmg_read 40d09c, dmg_region_read 40cff8, dmg_region_copy 40d050, dmg_capacity_set 40d128, dmg_region_set 40d240, dmg_part_store 40d284, col_part_collect 40cb84, col_part_gather 40cafc, damage_state_ctor 40d2cc, damage_state_bind 40d354 (`damage_data_test`) | groups `damage` (`dist3d_delta`, `dist3d_forward`, `heading_point`, `shield_damage`, `vehicle_hit`, `building_hit`, `building_part`), `damage_data` |
| `//core:weapons` / `weapons.h`, `building_weapons.h` | selectors 40a234/40adc0, mask 40f881, radius 426a20, maintenance 40ef94, fire and preparation, bullet/projectile flight, the V28 emitters, target emitter 4226a0, immediate impact 40bf74, the building turret 404100 (`building_weapons.c`), the beam constructor 40b804 | group `weapons`, group `beam` |
| `//core:camera` / `camera.h` | camera update 4011a0 with `terrain_face` 46e498 | `camera_update`, `terrain_face` (group `math`) |
| `//core:ai` / `ai.h` | AI obstacle steering 416274 with the object line query 404ca0 | group `ai` |
| `//core:node` / `node.h` | graph nodes 4029d8, 413cc4, 4089e4 | group `node` |
| `//core:mission` / `mission.h` | mission trigger pass 426b70 with the region predicates; the player route callback 41c194, mission timers 426b48/4230a4, status check 4135e8 | groups `mission`, `route` |
| `//core:logic` / `logic.h` | explosion and fire pool updates 40813c, 4076a0, 46b678 with the light setter | group `logic` |
| `//core:weapon_helpers` / `weapon_helpers.h` | weapon group pass 4107e4, 40ede8, sphere hit helpers 40db2c, 40c524, 40c428 | group `weapon_helpers` |
| `//core:track_pose` / `track_pose.h` | track seek 479344, turret aim 403d5c and the split track hooks | group `track_pose` |
| `//core:vehicle_control` / `vehicle_control.h` | vehicle control 416a04 with `slope_speed`, `part_ratio`, `random_damage` | group `vehicle_control` |
| `//core:mission_objectives` / `mission_objectives.h` | objectives 413280/4137b4, route completion, the per-frame outcome poll 4131ac | group `mission_objectives` |
| `//core:vehicle` / `vehicle.h` | vehicle leaves 4151a4, 4152ec, 412608, 412c8c, 41e758, 41b60c; the vehicle aux leaves 413ab8, 417bec, 41537c, 41b534, 4157c8; part helpers 40cf10, 40d9f8, 40dc58, guard 4240f4, ACTOR/FLYER +30h 411a74 | groups `vehicle`, `vehicle_aux`, `part_helpers` |
| `//core:input` / `input.h` | input children 410dbc, 415608, 41b728, 4160dc of the dispatcher | group `input` |
| `//core:building` / `building.h` | building target selection 411fa0/411e80, destruction actions 411bc8/423f30; turret target 403eec, part facing 406868 | groups `building`, `building_aim` |
| `//core:vehicle_update` / `vehicle_update.h` | the per-vehicle update 41aa5c with its six leaves; the weapon group slot-0 bodies 411a1d, 41194a | group `vehicle_update`, `part_helpers` |
| `//core:world` / `world.h` | terrain draw pass helpers 472bcc, 472b14, 46d324, 46bdf8 | group `world` |
| `//core:flyer` / `flyer.h` | flyer flight model 466b58 with its leaves; flyer controls 41bb9c, 4221a8, 42215c, 4222fc | groups `flyer`, `flyer_control` |
| `//core:ai_motion` / `ai_motion.h` | AI flyer move step 4218c4, formation steering 41fbb8 | group `ai_motion` |
| `//core:cockpit_target` / `cockpit_target.h` | cockpit target cycling 433174, 433250, 43349c, 4333c8, 4332dc | group `cockpit_target` |
| `//core:projectile` / `projectile.h` | projectile/bullet steering 40a254, 40aff0 and the angle helpers 492990, 49290c | group `projectile` |
| `//core:torso` / `torso.h` | torso servos 41a550/41a808, weapon convergence 41a74c, weapon keys 410f78 | group `torso` |
| `//core:pools` / `pools.h` | smoke ball 4091cc/409178, smoke emitter 4092dc, debris 408bd8/427d80, meteor 409d2c | group `pools` |
| `//core:groups` / `groups.h` | group activation 4236c4/42354c, current-order update 423a74, 423f08 and leaves 4238a0, 4238d4, 41dbcc | group `groups` |
| `//core:actor_pass` / `actor_pass.h` | actor-list pass 4123ac with 41251c, 4128f8, 412704 | group `actor_pass` |
| `//core:mech_order` / `mech_order.h` | MECH order handler 420ad4, wingman order 431610 | group `mech_order` |
| `//core:mount` / `mount.h` | trig leaves 47eb30/47ffe8, weapon mount helper 40f540/40f904 | group `mount` |
| `//core:ai_order` / `ai_order.h` | AI order helpers 411cb4, 413e50, 41c0f4, 41eb34 | group `ai_order` |
| `//core:input_dispatch` / `input_dispatch.h` | torso centering 41e8d4, torso aim 41ef14, the input dispatcher 460870 | group `input_dispatch` |
| `//core:weapon_system` / `weapon_system.h` | weapon-system routines 41074c, 410ae4, 410d04, 4110ac | group `weapon_system` |
| `//core:math_lib` / `math_lib.h` | texture edge setup 48aaac (the rest of the graphics library is not core code) | group `math_lib` |
| `//core:convoy` / `convoy.h` | the `VEHICLE` update chain 46a6d4 … 46a89c | group `convoy` |
| `//core:ai_helpers` / `ai_helpers.h` | route/behaviour helpers 4929fc, 41cb44, 41c4a8, 41c72c, 41cb94, 41ce08, 41e828 | group `ai_helpers` |
| `//core:obstacle` / `obstacle.h` | static-obstacle test 404ae4 (140-entry frame list, overflow modelled), convoy collision test 46a614 | group `obstacle` |
| `//core:ai_behaviour` / `ai_behaviour.h` | the AI behaviours around the torso servos 40e5f8, 41f358, 41f5a0, 41e984, 41e0e0, 41e224, 41dc24, 41dd64, 41d598 | group `ai_behaviour` |
| `//core:death` / `death.h` | the player's death sequence 45fa84, step 18 of the logic frame | group `death` |
| `//core:shield` / `shield.h` | the shield balance 413bc8 with its step 413af8 inside | group `shield` |
| `//core:results` / `results.h` | the team actor records of `results.dat` 423d68 | group `results` |
| `//core:map_pick` / `map_pick.h` | the HDD map pick 44d888 with the rectangle helper 44d288 inside | group `map_pick` |
| `//core:keyboard` / `keyboard.h` | the keyboard message handler 477bec of the window procedure (the first stdcall hook) | group `keyboard` |
| `//core:hud_helpers` / `hud_helpers.h` | the cockpit panel visibility test 431410 (the other HUD helpers are service rows of `sim/`) | group `hud_helpers` |
| `//core:ai_behaviour2` / `ai_behaviour2.h` | the second set of AI behaviours: aim select 41ea7c, attack plan 41cf18 with 41d5ec, group/order scan 41d60c/41d7d0, retreat fire 41d2c4 | group `ai_behaviour2` |
| `//core:control_record` / `control_record.h` | the per-frame control record builder 45a810, row 2 of the logic frame (pump and device reads stay original) | group `control_record` |
| `//core:actor_step` / `actor_step.h` | the row-11 per-actor step 411cec with 413eb0 and the six MECH/FLYER slot trampolines (entry shims at the original stack coordinate) | group `actor_step` |
| `//core:formation` / `formation.h` | the formation-slot virtual +78h of BASE, MECH and FLYER (405c04, 417898, 421e98) | group `formation` |
| `//core:base_update` / `base_update.h` | the row-10 building update 403ca8 with the part-destruction subtree 403914, 403890, 4036c0, 40379c | group `base` |
| `//core:mech_combat` / `mech_combat.h` | the MECH combat AI states 41c594, 41c86c, 41c9cc, 41d41c, 41d4e4, 41def0 with the helpers 41c478, 41de9c (six entry shims) | group `mech_combat` |
| `//core:flyer_states` / `flyer_states.h` | the FLYER AI states 422ca8, 422598, 422a50, 422a80, 422b34, 422bdc, 422d00, 422bf0 (residue-transfer shims) | group `flyer_states` |
| `//core:mech_notices` / `mech_notices.h` | the MECH notice virtuals 412800, 415710, 41f7b8 with 41f790 | group `mech_notices` |
| `//core:actor_virtuals` / `actor_virtuals.h` | eleven actor getters and checks of the BASE/MECH/ACTOR vtables | group `actor_virtuals` |
| `//core:col` / `col.h` | the COL traversal 40c9d4, 40c8fc, 40c8c8, 40c4c4 | group `col` |
| `//core:debris_burst` / `debris_burst.h` | the debris burst 408530 with 4086bc, 408588, 408940, 408b74, 408bb4 and the leaf 4089bc | group `debris_burst` |
| `//core:mech_travel` / `mech_travel.h` | the MECH travel/follow/sleep/ram/dead states 41d9cc, 41daac, 41c418, 41e570, 41e488, 41e554 with 41fb60, 41fac4, 41c3c8 (two entry shims) | group `mech_travel` |
| `//core:lc_base` / `lc_base.h` | the LC_BASE update 4045c8 with projectile creation 40a9c4 and the weapon record 40fe84 (two entry shims) | group `lc_base` |
| `//core:key_axes` / `key_axes.h` | the keyboard axes 45a4cc of the control record | group `key_axes` |
| `//core:mech_part_hit` / `mech_part_hit.h` | the MECH part hit 4188c8 with 410670 and 426ac8 inside | group `mech_part_hit` |
| `//core:mouse_events` / `mouse_events.h` | the GUI mouse event queue: dispatch 452d40, append 453078 | group `mouse_events` |
| `//core:hit_test` / `hit_test.h` | the actor hit tests of slot +20h: BASE 405038, MECH 418ba8, FLYER 421c8c | group `hit_test` |
| `//core:flyer_part` / `flyer_part.h` | the FLYER part damage 421bb4, slot +74h (ECX entry shim) | group `flyer_part` |
| `//core:weapon_eject` / `weapon_eject.h` | the weapon eject 40f57c and the equipment damage slot +68h 40ee0c, 40ee38, 40ee90, 40ef6c | group `weapon_eject` |
| `//core:mech_fly` / `mech_fly.h` | the MECH player-fly movement 4198f4 | group `mech_fly` |
| `//core:sfx` / `sfx.h` | the sound API calls 462830, 4628e0, 46299c, 46297c, 462ac4, 463114 and the voice/lip timeline 463400, 4634b0, 4634a0 (ECX exit shims) | group `sfx` |
| `//core:removal` / `removal.h` | the deferred object removal: flush 478644 (runtime record), pool free 471bc8, append 478628 | group `removal` |
| `//core:pool_iter` / `pool_iter.h` | the pool iterator: previous object 471c70, next object 471c18 | group `pool_iter` |
| `//core:fire` / `fire.h` | FIRE pool helpers: fire_alloc 46b430, fire_create 46b48c, fire_release 46b62c; the object base methods 46b905, 46b910 | group `fire`, `object_position` and `shape_word` of group `manager_slots` |
| `//core:region_damage` / `region_damage.h` | region-damage helpers: region_absorb 40d3ec, region_spread 40cf44, region_entry 40ca58, region_find 40ca8c; the region damage 40da38 and its body 40d434 | groups `region_damage`, `region_break` |
| `//core:meteor` / `meteor.h` | meteor constructor 409b44 and shape instance 490b64 | group `meteor` |
| `//core:panel_weapons` / `panel_weapons.h` | panel weapon selection: panel_weapon 432a50, weapon_find 4106ac, weapon_select 410708 | group `panel_weapons` |
| `//core:results_writer` / `results_writer.h` | results writer 42412c with score_sum 423e88, vehicle_value 418e60, list_append 426ac8 | group `results_writer` |
| `//core:transform_helpers` / `transform_helpers.h` | enter-prologue helpers rotate_words 47ff8c, matrix_point 4801d4 | group `transform_helpers` |
| `//core:hud_keys` / `hud_keys.h` | HUD key handler hud_key 432bc8, button_press 438d9c, button_release 4530bc | group `hud_keys` |
| `//core:commands` / `commands.h` | command handler command_key 45feb8 and playback_key 45fe6c | group `commands` |
| `//core:vfs_stream` / `vfs_stream.h` | the VOL-aware read stream (open/read/seek/tell/size/close, lookup, member lookup) over `formats/` | group `vfs` |
| `//core:destructors` / `destructors.h` | object destructors (explosion, fire, beam, debris, flat object, meteor, bullet, rocket) and the shape-instance pair 490bdc/48be20 | group `destructors` |
| `//core:weapon_slots` / `weapon_slots.h` | the player weapon system: weapon_frame 410b40, ready_step 410a3c, selected_ready 410a04, chain_ready 410970, latch_set 410f04, link_toggle 410f14, member_toggle 410cd0, the range gauge gauge_sync 4420c8, gauge_state 442120, gauge_link 442054, gauge_button 442154, gauge_mode 441f90 | group `weapon_slots` |
| `//core:hdd_orders` / `hdd_orders.h` | HDD map order controls map_areas 44d450, map_mode 44d9f4, wingman_select 44da98, order_cancel 44dc10; the HDD key method hdd_key 449ff4 | group `hdd_orders`, group `hdd_keys` |
| `//core:throttle` / `throttle.h` | throttle slider slot 4483a0 with its base gadget event 43904c and the shield step 413af8 | group `throttle` |
| `//core:panel_frame` / `panel_frame.h` | the cockpit panel frame 4327ac (draw sequence, target clear) | group `panel_frame` |
| `//core:logic_frame` / `logic_frame.h` | the logic frame 45f570: pool loops, groups, input, timers, MECH loop, death sequence or outcome with the alert dialog | group `logic_frame` |
| `//core:cockpit_sync` / `cockpit_sync.h` | the cockpit<->player synchronisation 41b130 with throttle_record 447df8, throttle_gauge 447da8, hud_record 43dbb0 | group `cockpit_sync` |
| `//core:render_root` / `render_root.h` | the render root 45fca8 (stage 5, detail settings, jolt, draws, panel frame and cockpit sync as calls), the stage dispatcher 401d94, vector_copy 40707c; the twelve removal flushes 405ea4 … 46b188 | group `render_root`, group `stage_flush` |
| `//core:asset_load` / `asset_load.h` | the persistent-object loader 47a144 (DTS), the GLBitmap and GLBitmapArray factories and reads 481cd4, 481d8c, 482d88, 482e7c, the stream destructor 472f64, the weapon shape cache | group `assets` |
| `//core:config` / `config.h` | the configuration readers 45e510, 459770, 459b94, 45b7a8, 46379c and the tape header records 45cca4, 45cd5c over `formats/cfg` | group `config` |
| `//core:mission_load` / `mission_load.h` | pool_create 471ad8, pool_nth 471cb0, subject_resolve 425348, action_copy 411b90, condition_init 415068, region_init 423358, timer_init 423104, group_array 423d38, initial_damage 4178e8; the pool-only stage-2 callbacks light_pools 4076e4, smoke_pools 4090cc, projectile_pools 40a060; the weapon fit of the actor load: weapon_fit 4175dc, player_weapons 4104ec, weapon_factory 40fff8; the constructors of the actor load: mech_ctor 415bb0, flyer_ctor 4215f4, building_factory 405314, group_ctor 423b34, base_marks 405db0 (with the leaves 41ff00, 422e80, 411dd4, 413a90); the actor load actor_load 4253d8; the world load world_load 424308 and the leaves only it calls, actor_list_init 411df8, gun_slot_table 40fecc, dmg_slot_table 40d218, col_slot_table 40cbec | groups `mission_load`, `mission_pools`, `mission_weapons`, `mission_ctors`, `mission_actors`, `mission_world` |
| `//core:rtl` / `rtl.h` | the 20 pure CW3220 imports (memory, strings, itoa, atol, integer sprintf/vsprintf) over a byte accessor; used by the proxy's import shim and the own `cw3220.dll` | the proxy's import shim, `rtl/` |
| `//core:rtl_heap` / `rtl_heap.h` | the CW3220 heap model: malloc/free/calloc/realloc/_expand, region growth through VirtualAlloc/VirtualFree | the proxy's import shim, `rtl/` |
| `//core:rtl_io` / `rtl_io.h` | the 21 file and console imports (open/read/write/seek/close, fopen family, fprintf/fscanf, findfirst/findnext, time, console stubs) over guest bytes, Win32 calls and the heap; the static I/O state, `_tzset` and the exit hooks | the proxy's import shim, `rtl/` |
| `//core:rtl_start` / `rtl_start.h` | the CW3220 lifecycle: `__startup`, the init/exit record selector, exit/_exit/_cexit/_c_exit, atexit, abort, `__ErrorExit` | the proxy's import shim, `rtl/` |
| `//core:actor_leaves` / `actor_leaves.h` | the actor slot leaves of the ACTOR, BASE family, MECH and FLYER tables: constants, word getters 403594, 4035a4, 4035b8, 415488, 415518, 41552c, part words 40360c, 415540, 4155ac, part walks 403624, 415558, 411b1c, the front test 411acc, the mark 42200c, the condition and scale callers 415504, 421e84, 421584; plus the five not hookable bodies; the six small actor slot methods: the empty and constant bodies 4127f8, 411a3c, 411a44, 411b2c, 411b34 and the default radial hit 411b3c | groups `actor_leaves`, `slot_leaves` |
| `//core:weapon_classes` / `weapon_classes.h` | the weapon-class methods of the vtable slots +2Ch ... +60h: readiness 40ecd4/40ecdc/40ed6c/40eda0, triggers 40e678/40e680/40e71c/40f8ad, steps 40f00c/40f0d0/40f3d8, beam level setters 40f48c/40f4d8/40f4f0/4116ad/4116cf, priority 4111e2/40f504, gun_clear 4115f8, stock 4111e9/40f520, gauge match 40df00/40df08/40df1c, spent 40f8a4/40ed34/40ed48, ammunition 40e62c/40e644; the weapon constructors: weapon_ctor 40df30, beam_ctor 40e074, gun_ctor 40e140, equip_ctor 40e234, ecm_ctor 40e274, turbo_ctor 40e2bc, targ_ctor 40e308, shield_ctor 40e344, energy_ctor 40e380; the gauge syncs of slot +50h and the LAEW emission: equip_sync 40f144, ecm_sync 40f184, turbo_sync 40f1f0, beam_sync 40f288, gun_sync 40f330, laew_sync 40f3b8, laew_fire 40ea04 | groups `weapon_methods`, `weapon_ctors`, `weapon_syncs` (six empty bodies are in `actor_leaves.c`) |
| `//core:bridged_leaves` / `bridged_leaves.h` | the leaves own code still reached through the guest bridge: the cockpit weapon word 40e484, the view-subject list steps 411e40/411e60, the R toggle 41b468, the nav marker 434974, the ThrottleGauge handler 447e08, the view toggle 45fe38, the empty matrix constructors 495132/495194/49514f; the swap 47df04 is computed in `convoy.c`/`obstacle.c` | group `bridged_leaves` |
| `//core:object_shapes` / `object_shapes.h` | the object shape set-up of the mission load: ts_obj_ctor 402278, det_anim_shape 40332c, det_anim_static 403368, det_anim_record 4033a4, anim_instance_ctor 478838, object_track 402374, thread_add 478a3c, thread_ctor 478e48, shape_word_block 402fc0 | group `object_shapes` |
| `//core:collision_data` / `collision_data.h` | the collision data of collide.cpp: the COL readers collision_load 40cd88, collision_read 40ccf8, collision_entry 40cc50, collision_volume 40cc14, collision_points 40c7c4, the bounding spheres collision_bounds 40ccc8 / collision_radius 40c5d0, the part-list scan collision_scan 40cec0 / collision_match 40ce78 | group `collision_data` |
| `//core:group_joins` / `group_joins.h` | the group joins of group_ctor (the members' placement during the load: MECH 417aa8, FLYER 421ee8, building 405c3c) and the building footprint marks 470ed4 with the cell flag writer 470ddc | group `group_joins` |
| `//core:terrain_load` / `terrain_load.h` | the terrain zone load of terrain.cpp: zone_load 42789c, the grid object grid_ctor 46befc with its height-cache base height_cache_ctor 495268, the materials terrain_materials 46c754, the cell generation terrain_cells 46c4c4 (the stream-A draws) and the cell-mark write cell_mark_set 470d74; the base flattening base_flatten 47129c with its floods flood_sum 470fe8 and flood_fill 471128 (iterative, an explicit path stack), the base-mark raster mark_raster 47136c and the mark read cell_flag_get 470e00 | groups `terrain_load`, `base_flatten` |
| `//core:stage2_types` / `stage2_types.h` | the stage-2 callbacks with type data: weapon_types 40fc8c (weapon list and projectile table) with weapon_entry 40f8bc, rocket_types 40a818, bullet_types 40ade0, beam_types 40b6e0, mech_forms 41fdb0, flyer_forms 422d90; test `stage2_types_test` | group `stage2_types` |
| `//core:type_loads` / `type_loads.h` | the MECH and FLYER type loads of the world load's pass 1: mech_types 42049c, mech_type_load 4201a8, lod_remap 420090, node_remap 41ff88, remap_word 41ff68, mech_type_select 41ff10, gun_list_load 40fee8, gun_classes 40fc50, node_list 40304c, the slot bases gun_slot_base 40feb4 / col_slot_base 40cbd4 / dmg_slot_base 40d200, flyer_types 422fb4, flyer_type_load 422ed0, flyer_type_select 422e90 (the leaves 41ff00, 422e80, 47e068, 47e08d computed) | group `type_loads` |
| `//core:mfd_orders` / `mfd_orders.h` | the MFD order path: the MFDGauge's button owner mfd_button 446844, the entry click mfd_click 4470c0, the order mfd_order 447248 with the order code menu_code 43f9c0 and its toggle menu_toggle 43f9f8, the order call order_call 43166c, the group order group_order 4231a4 (its uninitialized priority word read from the original frame), the entry select mfd_entry 4470a4, the target step mfd_target 446fb0, the status-view and range cycles mfd_view_next 446fc4 / mfd_range 446ff0, and the mission-load set-up of the order group and wingman table order_setup 431530 | group `mfd_orders` |
| `//core:building_types` / `building_types.h` | the building module of base.cpp: its stage-2 callback base_types 405fac (bforms, lc_wpns, bases, basecol, the count words, hulks, textures, debris) and the building pools base_pools 405df4 of the world load; test `building_types_test` | group `building_types` |
| `//core:mission_end` / `mission_end.h` | the mission end mission_end 461ff0 of the game main: dump and tape stream close, the results writer call and the process exit code 4d27b8 (6 demo, 0 quit, 4 or 3) | group `mission_end` |
| `//core:mission_root` / `mission_root.h` | the mission-load root mission_root 461608: the load's sequence from the input set-up to the start sequence with every callee through the bridge, the drains, the height fix and player pick, the camera and cockpit fields, the start-up words of the view block; test `mission_root_test` | group `mission_root` |
| `//core:hdd_map` / `hdd_map.h` | the HDD map frame map_frame 44c988 (HDDMap slot 1): event, clock and blink counter, the 30-tick timer, the click dispatch, the order-record check, the mode buttons, labels and map redraw with every callee through the bridge, the locals at the original frame offsets | group `hdd_map` |
| `//core:hdd_map_keys` / `hdd_map_keys.h` | the HDD map key path: map_key 44cc68 (HDDMap slot 4), the synthetic map click map_press 44d5c0 (locals at the original frame offsets), the wingman pick 44d82c and index 433134, the mode steps 44ee48/44ee88 and the target cycle 44ef08/44ef1c/44ef30, every callee through the bridge | group `hdd_map_keys` |
| `//core:effect_pools` / `effect_pools.h` | the effect pools with resources: the stage-2 callbacks explosion_pools 407b54, debris_pools 408860, meteor_pools 409a34 and fire_pools 46b1a8, the smoke-ball set-up smoke_balls 408fb8, the flat-object pool flat_pools 4097a8 of the world loader and the debris readers debris_set 40874c and debris_record 4083f8; test `effect_pools_test` | group `effect_pools` |
| `//core:fit_leaves` / `fit_leaves.h` | the further callees of the weapon fit: the mount table mount_table 420634 and its entry mount_entry 40fea0, weapon_shape_word 40e61c, the shield share split shield_split 413ac8, the derived actor word energy_rate 417d08, the equipment sort equip_slots 40fb2c and the recursive node_state_fill 417530 over a shape-node tree; test `fit_leaves_test` | group `fit_leaves` |
| `//core:camera_view` / `camera_view.h` | the outside camera and the view subject: camera_attached 401078, the camera setters camera_orbit_set 40109c and camera_attach_set 40111c, outside_camera 401c74 with camera_turn_step 401c20 and camera_speed_step 401c4c, view_apply 45de30, view_subject_set 45df34 (runtime record at the original frame offsets), view_offsets 45edb0 and camera_current 45edf8, every callee through the bridge; test `camera_view_test` | group `camera_view` |
| `//core:sim_leaves` / `sim_leaves.h` | the remaining simulation leaves: part_shapes_bind 4030d0, parts_intact 403668, formation_slot 405b9c, explosion_entry 407b20, the draw/done slots flat_draw 40991c, projectile_draw 40a120 and beam_done 40c2a0, weapon_by_mount 410670, ptr_list_add 411dd4, box_square_init 413a90, the sound latches twist_sound_latch 41a6d0 and pitch_sound_latch 41a994, mech_type_index 41ff00, flyer_type_index 422e80, the event notices cockpit_notice 420a68 and radio_notice 420a98 and notice_time 426aec; test `sim_leaves_test` | group `sim_leaves` |
| `//core:mfd_keys` / `mfd_keys.h` | the MFD key handlers: the MFDGauge's key method mfd_key 4469e8 (slot +0Ch), the Alt order keys mfd_alt_key 446c38, the order-entry select mfd_key_select 447158, the menu entry store menu_select 43f9e0 and the entry steps mfd_entry_next 44703c / mfd_entry_prev 447070 | group `mfd_keys` |
| `//core:hdd_exec` / `hdd_exec.h` | the HDD button executor hdd_execute 44a1a0: the pending HDD command [H+528h] of the HDD draw 449bf8 (page buttons, page commands, the wingman lamp toggle with the frame pointer it stores in the graphics context, wingman select, orders) | group `hdd_exec` |
| `//core` | all of the above, in the proxy's static link order | — |

Group names are those of `tools/reference-game/proxy/hook_registry.h`; each module is declared with
`core_module` from [bazel/defs/core_module.bzl](../bazel/defs/core_module.bzl)
(its comment names the original functions). The table is hand-maintained; the
registry and the generated table in [hooks.md](../docs/reference-game/hooks.md)
are the authority when they differ.

Exact contracts: [hooks.md](../docs/reference-game/hooks.md#function-notes)
and the subsystem documents listed in [docs/README.md](../docs/README.md)
(for example [player_movement.md](../docs/reverse-engineering/player_movement.md),
[rng.md](../docs/reverse-engineering/rng.md), [weapons.md](../docs/reverse-engineering/weapons.md)).
The sections below state the API-level contracts that callers of the first
modules must respect; the other modules document theirs in their subsystem
document and the hooks.md function notes.

## Guest memory and callbacks

Functions that walk the game's object graphs (`clip_pose`, the animation
functions, terrain, collision, effects) take an `es2_guest_memory`
([guest.h](include/es2/core/guest.h)): 32-bit guest addresses that wrap
modulo 2^32, mapped by the host (identity inside the original process, an
arena in tests). They replay the original's reads and writes in order, so
unproved object extents need no bounds.

Where the original calls code that is not in core, the core calls back into
the host: `es2_track_advance` and `es2_actor_step` callbacks,
`es2_clip_classes.other` for unknown clip classes, `es2_ground_calls` for the
ground update's callees and `es2_guest_call` for the collision's virtual
methods and game functions. The proxy routes them to the original addresses.
`es2_guest_call` arguments may be guest addresses of the core's own stack
buffers, so `collision` (and every host of `es2_guest_call`) requires guest
and host addresses to coincide; it is a 32-bit-process API.

`effects` uses `es2_effects_call`, which adds optional entry registers to
the same cdecl callback contract. Feedback 434144 can preserve incoming EDX;
the host must pass it through. Its 12-byte scratch point is a caller-owned
guest address, so effects itself works with an arena on x86 or x64 without
casting host pointers to guest addresses. It preserves signed thresholds,
the nonuniform latch-pointer advance, wrapped deltas, and every return bit.
The ground-update effects callback also receives entry EAX/EDX, including
the last collision/clip/rollback result on the no-track path.

The five track wrappers leave byte 19 of a local matrix uninitialized in the
original, so that output byte can be stack residue. Their core functions take
the residue as `padding` (the proxy passes 0); see the
[padding contract](../docs/reverse-engineering/player_movement.md#padding-contract-for-the-five-wrappers).

## RNG

The DBSIM additive generator described in
[rng.md](../docs/reverse-engineering/rng.md). The caller owns each stream and
supplies the original seed data; the original 112-byte table is not present in
this repository.

| API | Contract |
| --- | --- |
| `es2_rng_init(state, seed)` | Copies the caller's 112 seed bytes and sets source/destination indices to 24/55. As in the original, it writes the indices before the overlap-safe byte copy. The seed must not overlap those two index bytes. |
| `es2_rng_next(state)` | Adds the source word into the destination modulo 65536, returns that word zero-extended to 32 bits, then advances both indices modulo 56. |
| `es2_rng_bounded(state, bound)` | Consumes exactly one draw and returns its low 15 bits modulo the positive signed-16-bit bound. There is deliberately no zero-bound guard or rejection sampling. |

State storage is exactly 114 bytes: 56 little-endian 16-bit words at
`0x00..0x6f`, then one-byte indices at `0x70` and `0x71`; it can be saved and
restored by copying these bytes. Draw calls require both indices in
`0..55`, bounded calls `1..32767`. Invalid indices, pointers, counts or bounds
are not normalized. The library adds no synchronization.

## Geometry and timing

| API | Contract |
| --- | --- |
| `es2_distance2(dx,dy)` | Octagonal metric using wrapped absolute values, signed ordering, arithmetic halving and 32-bit wrapped addition. Inputs/results are raw `uint32_t` bit patterns. |
| `es2_distance2_vector(p)` | Reads two little-endian dwords and applies the 2-D metric. |
| `es2_distance2_points(a,b)` | Subtracts the two XY coordinates modulo 2^32 before applying the metric. |
| `es2_distance3(dx,dy,dz)` / `es2_distance3_points(a,b)` | Player-gate metric with separately truncated weights on the three sorted magnitudes; the points form subtracts XYZ modulo 2^32. |
| `es2_time_scale(reference,elapsed)` | Signed low-word inputs, elapsed times 256 divided toward zero by reference; clamps the quotient's signed low word to 64..450 while preserving its upper word. Returns the full EAX pattern. The reference's low word must be nonzero. |
| `es2_timer_word_step(timer,scale)` | Subtracts modulo 65536 at timer+1, clamps a set sign bit to zero, returns the resulting word. |
| `es2_timer_dword_step(timer,scale)` | Sign-extends scale, subtracts modulo 2^32 at timer+1, clamps a set sign bit to zero, returns the dword. |
| `es2_timer_arm(timer,duration)` | Sign-extends the duration's low word, shifts left 11 modulo 2^32 and writes timer+1. |

Point arguments are read-only and may overlap. Timer storage is three or five
accessible bytes; its tag byte is unchanged. No function reads a clock or
global variable: the adapter supplies the loaded image's current scale.

## Movement

| API | Contract |
| --- | --- |
| `es2_heading_cos/sin` | Signed low-word heading, SAR/carry rounding and quarter-table reflection; the caller supplies 1025 little-endian words. Sin wraps heading minus 16384. |
| `es2_multiply_q8` | Signed 32-bit product in 64 bits, arithmetic shift by eight, low 32 bits; no saturation. |
| `es2_movement_step` | Signed16 value times signed16 scale, Q8 floor, clamp to -32767..32767. |
| `es2_hud_speed` | Both actor/config branches, signed division toward zero and the original full-width branch's sign-dependent bias. Nonzero divisor and a signed32-fitting quotient required. |
| `es2_transform_point` | Signed mode byte; translation / planar / full Q14 transform, +8192 before the shift, wrapping translation, sequential X/Y/Z writes preserving input aliases. Returns void; the adapter reproduces the mode-dependent EAX. |
| `es2_vector_subtract` | Sequential wrapped XY subtraction, including overlapping inputs. Returns void; the adapter returns the destination pointer. |

Transform storage is 32 bytes, points 12 bytes and XY vectors 8 bytes.

## Matrix, pose, inverse trig and energy

| Header | APIs and storage |
| --- | --- |
| `matrix.h` | Euler, composition, transpose, rotate-only and pose-to-matrix; 32-byte matrices, 6-byte angles and 12-byte poses/points. Heading lookup reuses the movement APIs. |
| `pose.h` | Sequential six-word interpolation with negative half-turn ties; five-argument scalar interpolation; bounded ten-slot track count; `es2_clip_pose_registers`. |
| `trig.h` | Two-domain inverse-sine lookup, corrected angle ratio and matrix-to-angles. All table bytes are borrowed from the caller. |
| `energy.h` | Signed Q10 multiply, signed-word approach with wrapped steps, and the eight-byte accumulator state. |

The scalar interpolator requires a nonzero wrapped denominator and an
int32-fitting quotient. `es2_angle_ratio_valid` checks the ratio domain:
INT_MIN is illegal in either argument when y != 0. Inverse sine accepts all
16-bit inputs, including values outside geometric Q14. Its large-table pointer
denotes index 0, but only indices 3584..8192 are read; small-table indices are
0..448 and ratio-correction indices 1..512.

## Animation

`es2_track_clip_registers` (4792ac) keeps the virtual +20 and `clip_pose`
boundaries behind the generic guest-call callback; entry EAX supplies the high
frame-argument word. `track_clip_test` checks all pairs of the indices
0/1/32767/-32768/-1, callback graph changes, write order and the complete return
pair on a mapped synthetic arena.

## Damage

`damage` preserves signed wrapped distance ordering and the Z-sign EDX,
word-wrapped shield subtraction, quarter-turn front/rear boundaries and
path-dependent return registers. The shield helper writes one selected word
in a four-byte caller-owned buffer; it is bounded state, not a pure leaf.
Heading composes the existing ratio implementation and borrows its table.

The two hit bodies use `es2_damage_context`: guest function addresses, RNG
address, observer and a caller-owned 12-byte scratch point. All game calls,
including heading, shield, RNG and distance, go through `es2_guest_call`;
V58/V74 are resolved at each invocation. No host pointer is narrowed by core.
The test uses guest addresses different from host pointers and checks
callback-driven config/vtable changes and retained flag pointers.

Falloff truncates the product to 32 bits, sign-extends it, then divides by
the signed range word. Zero divisors and INT_MIN/-1 quotient overflow are
outside the API; the original's faults and bypasses are probed in vitro.
The building acceptance gate is always true, but its RNG advance is retained.
Lifecycle methods and point transforms remain guest code, so live compare
is refused for the two hit bodies. All four bounded helpers support compare.

`es2_building_part_registers` (404d70) additionally owns indexed part state
and flags with lifecycle/math/RNG calls delegated. It retains state and
configuration-record pointers across callbacks, reloads the flag array at
the write, and preserves full path-specific register results. The signed
selector table is read through guest memory, never copied into core.

## Weapons

[Exact contracts and live probe request](../docs/reverse-engineering/weapons.md).
The selectors take a borrowed base address and signed-byte bits. Mask merge
uses guest addresses and eight ordered byte writes, including every overlap;
its full return pair is source+8 / destination+8. None embeds game tables.

The graph functions take `es2_weapon_context`: mapped guest memory, game entry
addresses and an explicit call callback. Unlike the pair-only callback,
`es2_weapon_call` carries EAX/EDX/ECX in and out: radius forwards ECX's high
word in the V70 range slot. The proxy bridge is x86-specific; the core and
the mapped-arena test are C17 and work on x86 and x64. Original, replaced and
compare callee modes stay independent of the parent. Graph compare is refused
because callback writes are not bounded regions.

- **Radius** retains its initial list cursor, rereads signed count and virtual
  targets, wraps signed-radius subtraction, and excludes the strict boundary.
  The excluded fifth argument differs from the source forwarded in argument four.
- **Maintenance** clears the next mask after merging, steps the timer and
  updates the indirect animation word; divisor zero is outside its API.
- **Emission** debits and clamps a signed wrapped word and delegates
  preparation, owner V38 and both constructors, retaining original narrow-slot
  high bits.
- **Preparation and both flight bodies** go through `es2_weapon_graph`. Its
  128-byte scratch mapping is disjoint from the graph and holds the full
  42-byte swept segment separately from the 24-byte impact packet. All game
  math and effects use the call adapter, preserving each callee's hook mode.
  The context carries two preparation matrix residues and the flight packet's
  excluded-actor word. The native adapter captures them before detour dispatch;
  core never guesses a host stack layout or normalizes the values. The native
  `weapon_fire` adapter also preserves the two original caller-frame bytes when
  it delegates preparation, including local-matrix paths.
- **`es2_vehicle_part_registers`** owns part-damage ratios, warnings, target
  release and actor/attachment writes; lifecycle entries and V60/V68/V74 stay
  guest calls. Signed ratio denominators must be nonzero, equipment occupancy
  stable between snapshots, equipment count 0..59 and attachment configuration
  count 0..4. Early exits and the final cleanup return retain the complete pair.
  See [Validation of preparation, flight and vehicle part](../docs/reverse-engineering/weapons.md#validation-of-preparation-flight-and-vehicle-part).
- **`es2_target_emit_registers`** (4226a0): target lead, steering state, firing
  gates, mirrored muzzle points and cooldown. Both constructors and every
  math/timer/control callee remain guest calls. The context's `excluded` field
  supplies the original unassigned control-local residue; the native adapter
  captures entry ESP-20.
- **`es2_type_emit_registers`** (40ea58) and **`es2_alt_emit_registers`**
  (40ec64) retain signed word charge/cost comparisons, narrow argument-slot high
  words, repeated graph reads, mirrored muzzle positions and post-emission
  charge/latch writes.
- **`es2_impact_registers`** (40bf74) owns the packed query, contact/range
  choice, strict 5000 segment loop, matrix-translation commits and runtime
  allocation accounting; query, sound, transforms, pool allocation and
  constructors remain guest calls. It requires 256 scratch bytes. Its native
  runtime callback uses a synthetic frame at scratch+208 for Borland's
  initializer 49630c and restores the prior thread exception head at normal
  return. That head is also the original's complete EAX result; EDX remains the
  last allocator or constructor result. Exception unwinding through a detour is
  outside the framework's supported domain. The core never embeds runtime tables.
- **Emitter capture** preserves two preparation padding bytes and the future
  immediate-query exclusion word. Its call bridge seeds and reads the latter
  around preparation, then seeds the immediate frame; passing entry poison alone
  would miss preparation's intervening stack writes. Immediate impact captures
  entry ESP-136. The native emitter shim reserves a 128-KiB guard-probed gap and
  invokes preparation and impact at the original outgoing stack coordinate,
  keeping callee-produced pointer residue exact as well as explicit poison
  inputs. See [Nested frame residues](../docs/reverse-engineering/weapons.md#nested-frame-residues).
- **`es2_turret_registers`** (404100) owns building target selection/counts,
  range and angle gates, indexed animation, RNG-controlled barrel choice, local
  muzzle transforms and cooldowns. Every game call, including V20/V30/V38,
  remains a guest call. The native adapter supplies composed-matrix byte19 from
  entry ESP-101. Its graph needs 128 disjoint scratch bytes and a valid nonzero
  animation divisor. Normal return is EAX=0 with path-specific EDX; the disabled
  path forwards 403ca8's pair. Compare is refused. A separate header and source
  (`building_weapons`) keep this larger actor layout apart from the handheld
  weapon layouts.

## Tests

`bazel test //core/...` runs the synthetic tests; add
`--platforms=//bazel/platforms:windows_x86_64` for x64. They contain only
generated tables and test instruction-derived expectations, not live
equivalence (that is the job of the
[in-vitro harness](../tools/reference-game/invitro/README.md) and the oracle
regression). Each module has a `tests/test_<module>.c` target
`<module>_test` (`core_module` registers it); the exceptions are named in
`core/BUILD.bazel`.

| Target | What it checks |
| --- | --- |
| `rng_test` | the additive generator against an independent model |
| `geometry_test`, `timing_test` | the octagonal metrics, time scale and timers on edge and random inputs |
| `movement_test` | all movement functions, including an independently generated exhaustive heading table |
| `math_test` | matrix, pose, inverse trig and energy functions |
| `clip_pose_test`, `track_test`, `animation_test`, `track_advance_test`, `track_clip_test`, `terrain_test` | the animation and terrain walkers on mapped arenas |
| `damage_test` | guest memory, strict boundaries, full return pairs and callback ordering (x86 and x64) |
| `effects_test` | all branch predicates, strict threshold ties, callback order, mapped scratch and latch-pointer quirks (x86 and x64) |

`collision`'s synthetic test covers only the sweep receiver object query
(`object_query_test`); the ground collision and the swept query rely on the
in-vitro harness (needs the original executable) and the oracle regression.

### Opt-in original-data check for the RNG

The real-data helper is a manual target. Its Python standard-library driver
exits with `SKIP` before reading any original data when `ES2_DATA_ROOT` is
unset or empty. The root may name either the extracted CD directory
containing `VER95`, or a directory containing `extracted/cd/VER95`
(`build`).

```powershell
& bazel build //core:rng_real_helper
$rngBin = (& bazel info bazel-bin).Trim()
$env:ES2_DATA_ROOT = 'build\extracted\cd'
& '<es2-dependencies>\python\python.exe' -B core/tests/check_rng_real.py --helper (Join-Path $rngBin 'core/rng_real_helper.exe')
```

The driver verifies the `DBSIM.EXE` SHA-256 and the table's SHA-256 from
rng.md, reads exactly 112 bytes at file offset `0xa5b40` and pipes them into
the helper through stdin; it neither saves the table nor executes original
code. The helper initializes our RNG and returns eight outputs, which the
driver checks against the documented static predictions.
