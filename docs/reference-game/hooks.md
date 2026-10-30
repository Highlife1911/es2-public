# Function hooks

A hook replaces one function of the original `DBSIM.EXE` in memory, in a mode
chosen per hook: `original`, `replaced` or `compare` (see
[How a hook runs](#how-a-hook-runs)). The registry holds 789 hooks in 142
groups, all `original` by default; 247 of them accept `compare`
(`compare_audit.py`).

With all 789 hooks `replaced`, the regression of all 40 suites is identical
to the goldens, the DBSIM exit code equals the golden's and every
skipped, bypassed and mismatch counter is 0 (`<es2-data>\logs\check-r15-1`;
the run before it is `check-r12-3`).
`check.ps1 -Live` runs DBSIM with the own `cw3220.dll` deployed on five
clones (hidden window, early arming): all 53 imports are own code, the
original DLL is not loaded and the exit record says `rtl_runtime` own. The
shim's count-only configuration is `[rtl] enabled=1 imports=all`.

Own code (`core/`) implements the logic frame (`logic_frame`), the render
root (`render_root`), the mission-load pieces and root (`mission_root`),
mission end (`mission_end`), the stage-5 flushes (`flush_*`) and manager
slots, the actor slot leaves, the weapon-class methods, the bridged leaves,
the object shapes and collision data of the load, the HDD map (`hdd_execute`)
and the outside camera (`outside_camera`). A replacement that still calls
unreplaced game code makes its hook `compare_unsupported`; every game callee
keeps its own independently configured hook mode.

The last edge-mode coverage union (all 40 suites, 507 hooks) covers 2,397 of
3,776 sites both ways and 302 never
([live_coverage.md](live_coverage.md#current-union)).

Layout: the hook framework is [hook/](../../hook/README.md) (tests in
`hook/tests`); the simulation code is `core/`; adapters, the registry and the
proxy are in `tools/reference-game/proxy` (fixtures in `proxy/tests`).
Everything builds and tests with Bazel; deployment scripts build through
`tools/reference-game/setup/Es2Bazel.psm1`. Contracts of the movement
functions are in [player_movement.md](../reverse-engineering/player_movement.md),
of the RNG in [rng.md](../reverse-engineering/rng.md); the other contracts are
in the subsystem documents under `docs/reverse-engineering/` and the adapter
headers `tools/reference-game/proxy/*_hooks.h`. The contracts come from the
reference bytes, not from any reference repository.

## Registry

`tools/reference-game/proxy/hook_registry.h` is the only list of hook names,
RVAs, descriptors, extensions and groups; the proxy, the in-vitro harness,
`check_signatures.py` and the tick counters all read it.
`proxy/hook-names.txt` is the generated name list for the PowerShell scripts;
`//tools/reference-game/proxy:hook_registry_test` checks both against each
other and against this document: the marker line below, the complete
replacement selection and one compare line per hook that accepts `compare`
(see [Validation](#validation)). `setup/test-hook-registry.ps1` makes the same
checks from the checkout.

<!-- H4 registry names: rng_next,rng_bounded,dist2d_point,dist2d_vector,dist2d_delta,dist_octagonal,dist_gate,time_scale,timer_word_step,timer_dword_step,timer_arm,heading_cos,heading_sin,multiply_q8,movement_step,hud_speed,transform_point,vector_subtract,distance_display,euler_matrix,matrix_compose,rotate_point,matrix_transpose,pose_matrix,pose_interpolate,scalar_interpolate,track_count,inverse_sine,angle_ratio,matrix_angles,multiply_q10,approach_word,energy_accumulate,clip_pose,pose_parent,track_forward,track_inverse,track_scaled,track_scaled_inverse,manager_reset,animation_step,track_advance,track_target,track_rate,actor_animate,ground_animate,terrain_height,actor_snapshot,actor_rollback,ground_update,ground_collide,ground_effects,track_clip,dist3d_delta,dist3d_forward,heading_point,shield_damage,vehicle_hit,building_hit,building_part,projectile_def,bullet_def,mask_merge,radius_hit,weapon_step,weapon_fire,weapon_prepare,bullet_flight,projectile_flight,vehicle_part,target_emit,emit,alt_emit,impact,turret,sweep,side_scale,cell,segment,sweep_terrain,slope_test,normalize,diagonal,edge_test,face_test,face_cross,contact,terrain_line,terrain_face,camera_update,object_line,obstacle_steer,track_sort,track_step,track_list,node_matrices,terrain_pose,shield_geometry,debris_heading,trigger_scan,trigger_regions,region_test,trigger_group,trigger_fire,explosion_update,light_level,fire_update,group_fire,weapon_ready,part_condition,sphere_hits,sphere_test,track_route,track_poses,pose_list,track_seek,turret_aim,part_ratio,slope_speed,random_damage,vehicle_control,objective_eval,actor_safe,target_known,order_target,order_done,subject_done,rivals_done,route_point,outcome_poll,status_eval,bounds_check,bounds_outside,explosion_create,object_init,light_create,pool_alloc,condition_table,condition_ratio,sight_check,group_state,target_bearing,part_position,fire_selected,fire_line,aim_point,leg_input,line_plane,target_select,target_valid,death_actions,group_death,combat_rating,condition_record,device_state,device_word,ready_tone,weapon_tally,vehicle_update,poly_clip,poly_spans,view_rect,cell_uv,flight_model,flyer_move,formation_steer,cockpit_valid,cockpit_arc,cockpit_next,cockpit_nearest,cockpit_select,player_route,route_closing,route_advance,timer_scan,mission_timer,status_check,object_query,grid_sweep,set_length,flyer_input,flyer_command,flyer_bank,flyer_steer,vector_angle,aim_angles,projectile_steer,bullet_steer,torso_twist,torso_pitch,weapon_converge,weapon_keys,smoke_ball_update,smoke_update,smoke_ball_init,debris_update,ground_height,meteor_update,group_activate,spawn_point,order_update,group_assess,order_actor,order_point,actor_destination,actor_pass,contact_expire,contact_scan,contact_share,mech_order,wingman_order,sincos_pair,rotate_2d,mount_offset,mount_base,target_class,script_start,attack_target,order_resume,torso_center,torso_aim,player_input,select_step,fire_group,weapons_v3c,group_toggle,tex_edge,convoy_steer,convoy_seek,convoy_lead,convoy_follow,convoy_drive,convoy_think,turret_target,part_facing,point_offset,threat_sum,engage_done,flank_step,retreat_check,part_pick,drive_apply,obstacle_test,convoy_collide,shield_max_set,shield_capacity,damage_score,facing_part,vehicle_keys,beam_create,weapon_range,weapon_choose,aim_fire,aim_target,target_search,hunt,line_blocked,clear_shot,group_move,death_sequence,shield_balance,result_conditions,map_pick,key_message,panel_visible,aim_select,attack_plan,attack_replan,group_scan,order_scan,retreat_fire,control_record,actor_step,state_frame,mech_move,mech_act,mech_decide,flyer_move_call,flyer_act,flyer_decide,formation_base,formation_mech,formation_flyer,base_update,base_parts,base_part_chain,base_part_blast,base_part_debris,engage_end,skirt_check,attack_step,base_attack,flyer_attack,face_off,flank_attack,drive_off,flyer_attacking,flyer_follow,flyer_formation,flyer_search,flyer_patrol,flyer_scout,flyer_decision,flyer_engage,mech_contact,mech_kill,mech_attacked,mech_alert,base_aim_node,base_offsets,base_condition,actor_attacked,base_part_position,mech_aim_node,mech_offsets,mech_armour,mech_target_scale,mech_engage_roll,mech_link_position,col_query,col_group,col_shape,col_bound,debris_table,debris_burst,debris_pick,debris_piece,debris_init,debris_spin,debris_spin_any,travel_step,follow_step,sleep_step,ram_step,ram_move,dead_step,leader_route,point_move,fire_permit,lc_base_update,projectile_create,weapon_record,key_axes,mech_part_hit,mouse_dispatch,mouse_queue,base_hit_test,mech_hit_test,flyer_hit_test,flyer_part,weapon_eject,weapon_damage,beam_damage,gun_damage,targ_damage,mech_fly,sfx_play,sfx_play_at,sfx_place,sfx_refresh,sfx_stop,sfx_rate,sfx_voice,sfx_lip,sfx_lip_due,removal_flush,pool_free,removal_append,pool_prev,pool_next,pool_query,map_click,meteor_create,shape_instance,region_absorb,region_spread,region_entry,region_find,weapon_select,weapon_find,panel_weapon,bullet_create,bullet_init,def_lookup,fire_alloc,fire_create,fire_release,results_writer,score_sum,vehicle_value,list_append,flight_key,rotate_words,matrix_point,region_hit,region_break,hud_key,button_press,button_release,command_key,playback_key,stream_open,stream_close,stream_read,stream_seek,stream_tell,stream_size,vol_lookup,member_lookup,explosion_dtor,fire_dtor,beam_dtor,debris_dtor,flat_dtor,meteor_dtor,bullet_dtor,rocket_dtor,shape_dtor,shape_base_dtor,weapon_frame,ready_step,selected_ready,chain_ready,latch_set,link_toggle,member_toggle,gauge_sync,gauge_state,gauge_link,gauge_button,gauge_mode,collision_walk,shape_cell,convoy_leader,map_areas,map_mode,wingman_select,order_cancel,slider_event,slider_base,shield_step,panel_frame,subject_status,subject_group,subject_actor,group_down,route_flag,state_reached,group_safe,group_marked,group_flagged,bounds_margin,logic_frame,proj_record,pitch_point,damage_words,bullet_charge,rocket_init,flyer_route,flyer_altitude,flyer_pitch,flyer_throttle,group_fire_any,group_fire_sel,part_list_dead,part_group_dead,part_group_max,mission_guard,actor_offsets,command_queue,cockpit_sync,throttle_record,throttle_gauge,hud_record,rotation_compose,rotation_place,render_root,stage_dispatch,vector_copy,targ_aim,targ_next,missile_steer,ally_nearest,turbo_engage,member_assess,dts_load,bitmap_new,bitmap_read,bitmap_array_new,bitmap_array_read,stream_destroy,prefs_video,prefs_load,prefs_store,keyjoy_load,sound_config,tape_save,tape_restore,pool_create,pool_nth,subject_resolve,action_copy,condition_init,region_init,timer_init,group_array,initial_damage,light_pools,smoke_pools,projectile_pools,weapon_fit,player_weapons,weapon_factory,hdd_key,flush_buildings,flush_lights,flush_explosions,flush_debris,flush_smoke_balls,flush_smoke,flush_attached,flush_meteors,flush_projectiles,flush_mechs,flush_flyers,flush_fires,object_position,shape_word,sequence_next,sequence_prev,sequence_reach_up,sequence_reach_down,cyclic_next,cyclic_prev,cyclic_reach_up,cyclic_reach_down,gun_angle,base_radius,base_sight_radius,base_part_word,base_next_part,base_in_front,base_decide,actor_armour,actor_target_scale,actor_angle,actor_order,actor_keys,actor_aim_node,actor_radius,actor_sight_radius,actor_move,actor_engage_roll,actor_in_front,actor_mount_word,actor_part_word,actor_next_part,mech_angle,mech_condition,mech_radius,mech_sight_radius,mech_part_word,mech_next_part,mech_mount_word,flyer_target_scale,flyer_keys,flyer_condition,actor_mark,object_zero,equip_ready,beam_ready,gun_ready,elf_ready,weapon_trigger,elf_trigger,laew_trigger,equip_trigger,beam_step,elf_step,turbo_step,beam_adjust,beam_preset,beam_fill,beam_drain,weapon_priority,beam_priority,gun_clear,beam_clear,weapon_stock,gun_stock,weapon_gauge_match,beam_gauge_match,gun_gauge_match,weapon_spent,beam_spent,gun_spent,weapon_ammo,gun_ammo,mech_ctor,flyer_ctor,building_factory,group_ctor,base_marks,weapon_target_word,list_next,list_prev,r_key_toggle,nav_marker_set,throttle_handler,view_pref_toggle,matrix_ctor,rotation_ctor,rotation_base_ctor,ts_obj_ctor,det_anim_shape,det_anim_static,det_anim_record,anim_instance_ctor,object_track,thread_add,thread_ctor,shape_word_block,collision_load,collision_read,collision_entry,collision_volume,collision_points,collision_bounds,collision_radius,collision_scan,collision_match,actor_load,dmg_load,dmg_read,dmg_region_read,dmg_region_copy,dmg_capacity_set,dmg_region_set,dmg_part_store,col_part_collect,col_part_gather,damage_state_ctor,damage_state_bind,mech_join,flyer_join,building_join,footprint_marks,cell_flag_set,weapon_ctor,beam_ctor,gun_ctor,equip_ctor,ecm_ctor,turbo_ctor,targ_ctor,shield_ctor,energy_ctor,zone_load,grid_ctor,height_cache_ctor,terrain_materials,terrain_cells,cell_mark_set,weapon_types,weapon_entry,rocket_types,bullet_types,beam_types,mech_forms,flyer_forms,mech_types,mech_type_load,lod_remap,node_remap,remap_word,mech_type_select,gun_list_load,gun_classes,node_list,gun_slot_base,col_slot_base,dmg_slot_base,flyer_types,flyer_type_load,flyer_type_select,mfd_button,mfd_click,mfd_order,order_call,group_order,menu_code,menu_toggle,mfd_entry,mfd_target,mfd_view_next,mfd_range,order_setup,base_flatten,flood_sum,flood_fill,mark_raster,cell_flag_get,base_types,base_pools,world_load,actor_list_init,gun_slot_table,dmg_slot_table,col_slot_table,actor_contact,actor_part_position,actor_part_facing,actor_kill,actor_link_position,actor_radial_hit,equip_sync,ecm_sync,turbo_sync,beam_sync,gun_sync,laew_sync,laew_fire,weapon_none_28,weapon_none_38,weapon_none_3c,weapon_none_40,weapon_none_44,weapon_none_4c,mission_end,mission_root,map_frame,map_key,map_press,wingman_pick,wingman_index,mode_next,mode_prev,target_cycle1,target_cycle0,target_cycle,explosion_pools,debris_pools,debris_set,debris_record,flat_pools,meteor_pools,fire_pools,smoke_balls,weapon_shape_word,mount_entry,mount_table,shield_split,energy_rate,equip_slots,node_state_fill,camera_attached,camera_orbit_set,camera_attach_set,camera_turn_step,camera_speed_step,outside_camera,view_apply,view_subject_set,view_offsets,camera_current,part_shapes_bind,parts_intact,formation_slot,explosion_entry,flat_draw,projectile_draw,beam_done,weapon_by_mount,ptr_list_add,box_square_init,twist_sound_latch,pitch_sound_latch,mech_type_index,flyer_type_index,cockpit_notice,radio_notice,notice_time,mfd_key,mfd_alt_key,mfd_key_select,menu_select,mfd_entry_next,mfd_entry_prev,hdd_execute -->

All entries are cdecl. "Compare" is *no* for hooks marked
`compare_unsupported` (their replacement calls unreplaced game code, so a
second phase would change game state twice; installing them in compare mode
is refused). The table is generated by
`tools/reference-game/scripts/hooks_table.py render --write` from the
registry, the compare lines under [Validation](#validation) and
[hook_live_counts.json](hook_live_counts.json), the hook counters of an
all-replaced regression (`hooks_table.py counts`); `hooks_table_unit_test`
fails when it is stale. "Group" is the registry group (the in-vitro group and
the adapter header), not necessarily the core module's file name. Once a
caller is itself replaced, the core calls its callee directly and that hook's
own counter drops (for example the track wrappers to 0 after `track_advance`),
so a 0 is not a dead hook.

<!-- hooks table: generated by tools/reference-game/scripts/hooks_table.py from hook_registry.h, the compare lines below and hook_live_counts.json; do not edit -->
| Hook | VA | Group | Compare | c1_01-success calls | Suites with calls | Calls, all suites |
| --- | --- | --- | --- | ---: | ---: | ---: |
| `rng_next` | 492ee0 | rng | yes | 18,119 | 40 | 495,817 |
| `rng_bounded` | 492f24 | rng | yes | 40,707 | 40 | 1,032,021 |
| `dist2d_point` | 4928d0 | geometry | yes | 2,060,796 | 40 | 14,003,559 |
| `dist2d_vector` | 492a45 | geometry | yes | 0 | 0 | 0 |
| `dist2d_delta` | 492a9a | geometry | yes | 1,480 | 15 | 4,880 |
| `dist_octagonal` | 47de4c | geometry | yes | 222,176 | 40 | 1,379,733 |
| `dist_gate` | 49288c | geometry | yes | 3,135,060 | 40 | 36,096,083 |
| `time_scale` | 467894 | timing | yes | 12,396 | 40 | 144,632 |
| `timer_word_step` | 467a48 | timing | yes | 872,710 | 40 | 10,289,213 |
| `timer_dword_step` | 467aa8 | timing | yes | 80,142 | 40 | 1,715,146 |
| `timer_arm` | 467ac4 | timing | yes | 0 | 7 | 43 |
| `heading_cos` | 47dff0 | movement | yes | 38,402 | 40 | 378,915 |
| `heading_sin` | 47e027 | movement | yes | 0 | 0 | 0 |
| `multiply_q8` | 47e0a0 | movement | yes | 802,494 | 40 | 9,976,142 |
| `movement_step` | 467924 | movement | yes | 282,370 | 40 | 4,892,478 |
| `hud_speed` | 41bb3c | movement | yes | 6,200 | 40 | 63,125 |
| `transform_point` | 48043c | movement | yes | 1,444,701 | 40 | 17,278,235 |
| `vector_subtract` | 492a30 | movement | yes | 33,877 | 40 | 630,701 |
| `distance_display` | 47d694 | movement | yes | 10,124 | 40 | 86,787 |
| `euler_matrix` | 47ebb8 | math | yes | 260,328 | 40 | 2,990,061 |
| `matrix_compose` | 47fa20 | math | yes | 394,834 | 40 | 4,355,938 |
| `rotate_point` | 480304 | math | yes | 868,524 | 40 | 10,183,173 |
| `matrix_transpose` | 47df19 | math | yes | 674,569 | 40 | 8,230,909 |
| `pose_matrix` | 478c1c | math | yes | 0 | 0 | 0 |
| `pose_interpolate` | 49270c | math | yes | 0 | 0 | 0 |
| `scalar_interpolate` | 47df48 | math | yes | 2,255 | 22 | 83,254 |
| `track_count` | 4789b0 | math | yes | 0 | 0 | 0 |
| `inverse_sine` | 47da4c | math | yes | 0 | 0 | 0 |
| `angle_ratio` | 47d32c | math | yes | 151,012 | 40 | 1,037,577 |
| `matrix_angles` | 47f9a0 | math | yes | 18,879 | 40 | 154,900 |
| `multiply_q10` | 47e0b0 | math | yes | 616,833 | 40 | 12,108,768 |
| `approach_word` | 467adc | math | yes | 87,826 | 40 | 1,158,666 |
| `energy_accumulate` | 413b38 | math | yes | 17,087 | 40 | 369,425 |
| `clip_pose` | 478ef4 | math | yes | 33,789 | 40 | 743,119 |
| `pose_parent` | 478bc4 | math | yes | 0 | 0 | 0 |
| `track_forward` | 478f6c | math | yes | 0 | 0 | 0 |
| `track_inverse` | 478ff4 | math | yes | 0 | 0 | 0 |
| `track_scaled` | 4790b4 | math | yes | 0 | 0 | 0 |
| `track_scaled_inverse` | 479194 | math | yes | 0 | 0 | 0 |
| `manager_reset` | 478b7c | math | yes | 0 | 0 | 0 |
| `animation_step` | 40250c | math | no | 82,969 | 40 | 811,187 |
| `track_advance` | 478d38 | math | no | 82,969 | 40 | 811,187 |
| `track_target` | 47967c | math | yes | 163 | 35 | 2,652 |
| `track_rate` | 4796d8 | math | yes | 26,594 | 40 | 460,671 |
| `actor_animate` | 402604 | math | no | 82,969 | 40 | 811,187 |
| `ground_animate` | 418f40 | math | no | 26,506 | 40 | 457,153 |
| `terrain_height` | 46e180 | math | yes | 455,112 | 40 | 4,323,031 |
| `actor_snapshot` | 402628 | math | no | 26,506 | 40 | 447,294 |
| `actor_rollback` | 4027fc | math | no | 0 | 10 | 19,520 |
| `ground_update` | 41a360 | math | no | 26,506 | 40 | 447,294 |
| `ground_collide` | 418f74 | math | no | 26,506 | 40 | 457,153 |
| `ground_effects` | 4195c8 | math | no | 26,506 | 40 | 447,294 |
| `track_clip` | 4792ac | math | no | 33,789 | 40 | 743,119 |
| `dist3d_delta` | 47de72 | damage | yes | 426,600 | 40 | 10,248,946 |
| `dist3d_forward` | 492aaf | damage | yes | 100,866 | 40 | 1,386,252 |
| `heading_point` | 492934 | damage | yes | 75,132 | 40 | 994,702 |
| `shield_damage` | 413c68 | damage | yes | 5 | 13 | 243 |
| `vehicle_hit` | 4187d0 | damage | no | 5 | 13 | 243 |
| `building_hit` | 404f20 | damage | no | 1 | 4 | 8 |
| `building_part` | 404d70 | damage | no | 0 | 12 | 326 |
| `projectile_def` | 40a234 | weapons | yes | 278 | 13 | 18,199 |
| `bullet_def` | 40adc0 | weapons | yes | 9,630 | 24 | 85,341 |
| `mask_merge` | 40f881 | weapons | yes | 45,583 | 40 | 1,971,397 |
| `radius_hit` | 426a20 | weapons | no | 4 | 21 | 47 |
| `weapon_step` | 40ef94 | weapons | no | 45,683 | 40 | 2,012,216 |
| `weapon_fire` | 40e964 | weapons | no | 218 | 14 | 2,773 |
| `weapon_prepare` | 40e788 | weapons | no | 262 | 20 | 4,605 |
| `bullet_flight` | 40b124 | weapons | no | 9,070 | 24 | 78,933 |
| `projectile_flight` | 40a538 | weapons | no | 272 | 13 | 17,814 |
| `vehicle_part` | 417de4 | weapons | no | 52 | 32 | 1,944 |
| `target_emit` | 4226a0 | weapons | no | 11,756 | 16 | 39,613 |
| `emit` | 40ea58 | weapons | no | 44 | 18 | 1,477 |
| `alt_emit` | 40ec64 | weapons | no | 0 | 10 | 355 |
| `impact` | 40bf74 | weapons | no | 0 | 16 | 1,680 |
| `turret` | 404100 | weapons | no | 37,188 | 40 | 703,671 |
| `sweep` | 426528 | weapons | no | 9,259 | 24 | 117,271 |
| `side_scale` | 426b04 | weapons | no | 18,518 | 24 | 197,118 |
| `cell` | 427238 | weapons | yes | 50,349 | 22 | 1,894,676 |
| `segment` | 46e980 | weapons | no | 61,179 | 40 | 1,536,460 |
| `sweep_terrain` | 428048 | weapons | no | 9,259 | 24 | 117,271 |
| `slope_test` | 46ff4c | weapons | yes | 89,368 | 33 | 3,243,317 |
| `normalize` | 46c23c | weapons | no | 146,899 | 40 | 4,127,692 |
| `diagonal` | 46fdb8 | weapons | no | 0 | 8 | 3,810 |
| `edge_test` | 470468 | weapons | no | 217,571 | 40 | 7,367,574 |
| `face_test` | 46ff90 | weapons | no | 33,702 | 33 | 952,100 |
| `face_cross` | 470080 | weapons | no | 27,833 | 33 | 1,147,095 |
| `contact` | 470798 | weapons | no | 19,816 | 38 | 801,435 |
| `terrain_line` | 4280f4 | weapons | no | 35,071 | 40 | 928,825 |
| `terrain_face` | 46e498 | weapons | yes | 236,074 | 40 | 2,064,304 |
| `camera_update` | 4011a0 | weapons | no | 6,199 | 40 | 73,866 |
| `object_line` | 404ca0 | ai | no | 16,849 | 33 | 490,364 |
| `obstacle_steer` | 416274 | ai | no | 18,143 | 38 | 316,753 |
| `track_sort` | 4789d0 | math | yes | 59 | 40 | 2,432 |
| `track_step` | 479720 | math | no | 0 | 0 | 0 |
| `track_list` | 478aac | math | no | 0 | 0 | 0 |
| `node_matrices` | 478c64 | math | no | 25 | 40 | 1,009 |
| `terrain_pose` | 4029d8 | node | no | 73,550 | 38 | 500,637 |
| `shield_geometry` | 413cc4 | node | no | 753 | 21 | 12,450 |
| `debris_heading` | 4089e4 | node | no | 242 | 25 | 5,783 |
| `trigger_scan` | 426b70 | mission | no | 6,198 | 40 | 72,275 |
| `trigger_regions` | 4234b8 | mission | no | 61,447 | 40 | 646,895 |
| `region_test` | 4233a4 | mission | yes | 42,325 | 40 | 479,338 |
| `trigger_group` | 423508 | mission | no | 30,990 | 28 | 280,276 |
| `trigger_fire` | 423430 | mission | no | 19,132 | 28 | 105,982 |
| `explosion_update` | 40813c | logic | no | 1,664 | 27 | 39,681 |
| `light_level` | 4076a0 | logic | no | 601 | 25 | 12,376 |
| `fire_update` | 46b678 | logic | no | 3,119 | 15 | 31,483 |
| `group_fire` | 4107e4 | weapon_helpers | no | 17,087 | 40 | 369,425 |
| `weapon_ready` | 40ede8 | weapon_helpers | yes | 45,683 | 40 | 2,035,516 |
| `part_condition` | 40db2c | weapon_helpers | yes | 53,039 | 35 | 296,746 |
| `sphere_hits` | 40c524 | weapon_helpers | no | 156 | 21 | 7,460 |
| `sphere_test` | 40c428 | weapon_helpers | no | 559 | 21 | 36,947 |
| `track_route` | 4793d4 | track_pose | yes | 0 | 0 | 0 |
| `track_poses` | 479ab0 | track_pose | no | 0 | 0 | 0 |
| `pose_list` | 478b00 | track_pose | no | 0 | 0 | 0 |
| `track_seek` | 479344 | track_pose | no | 33,616 | 40 | 737,932 |
| `turret_aim` | 403d5c | track_pose | no | 671 | 18 | 22,201 |
| `part_ratio` | 40dbc0 | vehicle_control | yes | 298 | 37 | 25,742 |
| `slope_speed` | 41693c | vehicle_control | yes | 24,341 | 40 | 365,313 |
| `random_damage` | 417a04 | vehicle_control | no | 10 | 39 | 400 |
| `vehicle_control` | 416a04 | vehicle_control | no | 26,496 | 40 | 448,241 |
| `objective_eval` | 413280 | mission_objectives | no | 64 | 40 | 698 |
| `actor_safe` | 4137b4 | mission_objectives | no | 64 | 40 | 1,062 |
| `target_known` | 411c58 | mission_objectives | yes | 5,378 | 40 | 154,053 |
| `order_target` | 423918 | mission_objectives | yes | 5,354 | 38 | 105,413 |
| `order_done` | 4239fc | mission_objectives | no | 38,908 | 40 | 308,642 |
| `subject_done` | 412e58 | mission_objectives | no | 26,512 | 38 | 195,737 |
| `rivals_done` | 412e74 | mission_objectives | no | 0 | 10 | 20,531 |
| `route_point` | 423b0c | mission_objectives | yes | 44,887 | 40 | 397,604 |
| `outcome_poll` | 4131ac | mission_objectives | no | 6,198 | 40 | 71,506 |
| `status_eval` | 413180 | mission_objectives | no | 67 | 40 | 817 |
| `bounds_check` | 41379c | mission_objectives | yes | 6,195 | 40 | 71,521 |
| `bounds_outside` | 41373c | mission_objectives | yes | 6,252 | 40 | 72,235 |
| `explosion_create` | 407f1c | effect | no | 253 | 27 | 5,492 |
| `object_init` | 402188 | effect | no | 865 | 40 | 25,678 |
| `light_create` | 407604 | effect | no | 72 | 25 | 1,318 |
| `pool_alloc` | 471b30 | effect | no | 968 | 40 | 28,608 |
| `condition_table` | 4151a4 | vehicle | no | 140 | 40 | 15,342 |
| `condition_ratio` | 4152ec | vehicle | no | 37,131 | 40 | 575,695 |
| `sight_check` | 412608 | vehicle | no | 41,633 | 40 | 992,747 |
| `group_state` | 412c8c | vehicle | no | 26,512 | 38 | 196,443 |
| `target_bearing` | 41e758 | vehicle | no | 2,370 | 23 | 88,205 |
| `part_position` | 41b60c | vehicle | no | 565 | 14 | 26,419 |
| `fire_selected` | 410dbc | input | no | 6,198 | 40 | 68,312 |
| `fire_line` | 415608 | input | no | 6,198 | 40 | 68,312 |
| `aim_point` | 41b728 | input | no | 4,259 | 12 | 10,244 |
| `leg_input` | 4160dc | input | no | 6,198 | 37 | 67,863 |
| `line_plane` | 47e610 | line_plane | yes | 127,563 | 40 | 2,207,369 |
| `target_select` | 411fa0 | building | no | 390 | 40 | 10,167 |
| `target_valid` | 411e80 | building | no | 24,960 | 40 | 510,616 |
| `death_actions` | 411bc8 | building | no | 4 | 21 | 80 |
| `group_death` | 423f30 | building | no | 4 | 21 | 80 |
| `combat_rating` | 41edd8 | vehicle_update | no | 139 | 40 | 3,240 |
| `condition_record` | 41b4ec | vehicle_update | no | 140 | 40 | 15,342 |
| `device_state` | 41aa10 | vehicle_update | yes | 17,087 | 40 | 369,425 |
| `device_word` | 41aa44 | vehicle_update | yes | 4,979 | 23 | 112,625 |
| `ready_tone` | 41b0bc | vehicle_update | no | 2,133 | 12 | 7,065 |
| `weapon_tally` | 40fbdc | vehicle_update | no | 17,087 | 40 | 369,425 |
| `vehicle_update` | 41aa5c | vehicle_update | no | 17,087 | 40 | 369,425 |
| `poly_clip` | 472bcc | world | no | 37,194 | 40 | 415,152 |
| `poly_spans` | 472b14 | world | no | 21,858 | 40 | 258,925 |
| `view_rect` | 46d324 | world | no | 12,398 | 40 | 143,097 |
| `cell_uv` | 46bdf8 | world | no | 1,158,532 | 40 | 12,731,334 |
| `flight_model` | 466b58 | flyer | no | 12,394 | 31 | 81,599 |
| `flyer_move` | 4218c4 | ai_motion | no | 12,394 | 28 | 77,148 |
| `formation_steer` | 41fbb8 | ai_motion | no | 6,197 | 35 | 119,510 |
| `cockpit_valid` | 433174 | cockpit_target | no | 2,393 | 16 | 9,893 |
| `cockpit_arc` | 433250 | cockpit_target | no | 10 | 10 | 131 |
| `cockpit_next` | 43349c | cockpit_target | no | 4 | 12 | 47 |
| `cockpit_nearest` | 4333c8 | cockpit_target | no | 0 | 6 | 32 |
| `cockpit_select` | 4332dc | cockpit_target | no | 0 | 1 | 2 |
| `player_route` | 41c194 | route | no | 6,197 | 40 | 71,243 |
| `route_closing` | 423170 | route | no | 4 | 4 | 7 |
| `route_advance` | 42313c | route | no | 6 | 19 | 92 |
| `timer_scan` | 426b48 | route | no | 6,198 | 40 | 72,275 |
| `mission_timer` | 4230a4 | route | no | 0 | 7 | 38,808 |
| `status_check` | 4135e8 | route | no | 67 | 40 | 817 |
| `object_query` | 427da8 | object_query | yes | 274,729 | 30 | 1,919,987 |
| `grid_sweep` | 4273c8 | object_query | yes | 1,136 | 22 | 55,307 |
| `set_length` | 4927f0 | object_query | yes | 1,136 | 24 | 53,704 |
| `flyer_input` | 41bb9c | flyer_control | no | 0 | 3 | 4,453 |
| `flyer_command` | 4221a8 | flyer_control | no | 12,394 | 28 | 77,146 |
| `flyer_bank` | 42215c | flyer_control | no | 6,181 | 28 | 35,372 |
| `flyer_steer` | 4222fc | flyer_control | no | 12,394 | 28 | 77,146 |
| `vector_angle` | 49290c | projectile | yes | 33,521 | 40 | 447,406 |
| `aim_angles` | 492990 | projectile | no | 32,680 | 34 | 1,111,034 |
| `projectile_steer` | 40a254 | projectile | no | 271 | 13 | 17,728 |
| `bullet_steer` | 40aff0 | projectile | no | 0 | 5 | 378 |
| `torso_twist` | 41a550 | torso | no | 16,130 | 40 | 346,500 |
| `torso_pitch` | 41a808 | torso | no | 16,130 | 40 | 346,500 |
| `weapon_converge` | 41a74c | torso | no | 16,130 | 40 | 350,950 |
| `weapon_keys` | 410f78 | torso | no | 6,190 | 40 | 70,744 |
| `smoke_ball_update` | 4091cc | pools | no | 0 | 0 | 0 |
| `smoke_update` | 4092dc | pools | no | 0 | 0 | 0 |
| `smoke_ball_init` | 409178 | pools | no | 0 | 0 | 0 |
| `debris_update` | 408bd8 | pools | no | 16,727 | 25 | 645,594 |
| `ground_height` | 427d80 | pools | yes | 16,727 | 25 | 645,594 |
| `meteor_update` | 409d2c | pools | no | 351 | 12 | 1,419 |
| `group_activate` | 4236c4 | groups | no | 47,864 | 39 | 422,217 |
| `spawn_point` | 42354c | groups | no | 4 | 12 | 17 |
| `order_update` | 423a74 | groups | no | 187,660 | 40 | 1,503,782 |
| `group_assess` | 423f08 | groups | no | 0 | 0 | 0 |
| `order_actor` | 4238a0 | groups | no | 0 | 5 | 9,528 |
| `order_point` | 4238d4 | groups | no | 0 | 9 | 36,939 |
| `actor_destination` | 41dbcc | groups | no | 0 | 11 | 38,604 |
| `actor_pass` | 4123ac | actor_pass | no | 6,198 | 40 | 72,275 |
| `contact_expire` | 41251c | actor_pass | no | 3,208 | 40 | 28,485 |
| `contact_scan` | 4128f8 | actor_pass | yes | 322,296 | 40 | 1,970,943 |
| `contact_share` | 412704 | actor_pass | no | 3,531 | 31 | 44,923 |
| `mech_order` | 420ad4 | mech_order | no | 0 | 8 | 231 |
| `wingman_order` | 431610 | mech_order | no | 0 | 4 | 52 |
| `sincos_pair` | 47eb30 | mount | yes | 244,194 | 40 | 1,521,618 |
| `rotate_2d` | 47ffe8 | mount | yes | 146,826 | 40 | 1,312,820 |
| `mount_offset` | 40f540 | mount | no | 10,756 | 40 | 490,302 |
| `mount_base` | 40f904 | mount | no | 10,756 | 40 | 490,302 |
| `target_class` | 411cb4 | ai_order | yes | 0 | 8 | 7,902 |
| `script_start` | 413e50 | ai_order | no | 68 | 40 | 3,065 |
| `attack_target` | 41c0f4 | ai_order | no | 0 | 8 | 30 |
| `order_resume` | 41eb34 | ai_order | no | 6 | 40 | 684 |
| `torso_center` | 41e8d4 | input_dispatch | no | 11,050 | 38 | 205,795 |
| `torso_aim` | 41ef14 | input_dispatch | no | 4,381 | 22 | 86,471 |
| `player_input` | 460870 | input_dispatch | no | 6,198 | 40 | 72,316 |
| `select_step` | 41074c | weapon_system | no | 4,033 | 19 | 29,932 |
| `fire_group` | 410ae4 | weapon_system | no | 6,200 | 40 | 73,910 |
| `weapons_v3c` | 410d04 | weapon_system | no | 0 | 1 | 1 |
| `group_toggle` | 4110ac | weapon_system | no | 0 | 4 | 6 |
| `tex_edge` | 48aaac | math_lib | no | 48,027 | 40 | 396,134 |
| `convoy_steer` | 46a89c | convoy | yes | 30,990 | 22 | 138,990 |
| `convoy_seek` | 46a958 | convoy | no | 24,371 | 22 | 111,208 |
| `convoy_lead` | 46a9e8 | convoy | no | 12,396 | 22 | 55,176 |
| `convoy_follow` | 46aa60 | convoy | no | 18,594 | 22 | 83,814 |
| `convoy_drive` | 46a810 | convoy | no | 30,990 | 22 | 138,990 |
| `convoy_think` | 46a6d4 | convoy | no | 37,188 | 32 | 220,368 |
| `turret_target` | 403eec | building_aim | no | 671 | 18 | 22,201 |
| `part_facing` | 406868 | building_aim | no | 0 | 10 | 26,685 |
| `point_offset` | 4929fc | ai_helpers | yes | 50,209 | 36 | 262,215 |
| `threat_sum` | 41cb44 | ai_helpers | no | 0 | 1 | 1 |
| `engage_done` | 41c4a8 | ai_helpers | no | 2,310 | 21 | 57,737 |
| `flank_step` | 41c72c | ai_helpers | no | 0 | 15 | 34,633 |
| `retreat_check` | 41cb94 | ai_helpers | no | 15 | 23 | 421 |
| `part_pick` | 41ce08 | ai_helpers | no | 7 | 21 | 171 |
| `drive_apply` | 41e828 | ai_helpers | no | 2,370 | 23 | 88,205 |
| `obstacle_test` | 404ae4 | obstacle | no | 57,500 | 40 | 595,974 |
| `convoy_collide` | 46a614 | obstacle | no | 30,990 | 22 | 138,990 |
| `shield_max_set` | 413ab8 | vehicle_aux | no | 60 | 40 | 2,337 |
| `shield_capacity` | 417bec | vehicle_aux | no | 60 | 40 | 2,337 |
| `damage_score` | 41537c | vehicle_aux | no | 0 | 0 | 0 |
| `facing_part` | 41b534 | vehicle_aux | no | 5 | 13 | 319 |
| `vehicle_keys` | 4157c8 | vehicle_aux | no | 6,190 | 40 | 70,743 |
| `beam_create` | 40b804 | beam | no | 0 | 16 | 8,283 |
| `weapon_range` | 40e5f8 | ai_behaviour | yes | 9,297 | 22 | 439,990 |
| `weapon_choose` | 41f358 | ai_behaviour | no | 2,255 | 22 | 83,254 |
| `aim_fire` | 41f5a0 | ai_behaviour | no | 2,255 | 22 | 83,566 |
| `aim_target` | 41e984 | ai_behaviour | no | 2,370 | 21 | 60,129 |
| `target_search` | 41e0e0 | ai_behaviour | no | 0 | 6 | 107 |
| `hunt` | 41e224 | ai_behaviour | no | 0 | 11 | 38,601 |
| `line_blocked` | 41dc24 | ai_behaviour | no | 9 | 9 | 346 |
| `clear_shot` | 41dd64 | ai_behaviour | no | 521 | 9 | 18,834 |
| `group_move` | 41d598 | ai_behaviour | no | 7,041 | 37 | 130,323 |
| `death_sequence` | 45fa84 | death | no | 0 | 6 | 810 |
| `shield_balance` | 413bc8 | shield | no | 6,200 | 40 | 73,910 |
| `result_conditions` | 423d68 | results | no | 0 | 0 | 0 |
| `map_pick` | 44d888 | map_pick | no | 0 | 4 | 31 |
| `key_message` | 477bec | keyboard | no | 1,430 | 40 | 24,089 |
| `panel_visible` | 431410 | hud_helpers | no | 77,949 | 40 | 1,303,807 |
| `aim_select` | 41ea7c | ai_behaviour2 | no | 2,370 | 23 | 90,887 |
| `attack_plan` | 41cf18 | ai_behaviour2 | no | 8 | 23 | 751 |
| `attack_replan` | 41d5ec | ai_behaviour2 | no | 7 | 23 | 160 |
| `group_scan` | 41d60c | ai_behaviour2 | no | 844 | 26 | 66,462 |
| `order_scan` | 41d7d0 | ai_behaviour2 | no | 6,197 | 27 | 63,861 |
| `retreat_fire` | 41d2c4 | ai_behaviour2 | no | 60 | 6 | 2,096 |
| `control_record` | 45a810 | control_record | no | 8,261 | 40 | 87,954 |
| `actor_step` | 411cec | actor_step | no | 348,808 | 40 | 3,032,626 |
| `state_frame` | 413eb0 | actor_step | no | 0 | 0 | 0 |
| `mech_move` | 415afc | actor_step | no | 26,512 | 40 | 451,929 |
| `mech_act` | 415b38 | actor_step | no | 26,512 | 40 | 451,929 |
| `mech_decide` | 415b74 | actor_step | no | 7 | 40 | 783 |
| `flyer_move_call` | 4217fc | actor_step | no | 12,396 | 28 | 88,051 |
| `flyer_act` | 42184c | actor_step | no | 12,396 | 28 | 88,051 |
| `flyer_decide` | 421888 | actor_step | no | 14 | 28 | 756 |
| `formation_base` | 405c04 | formation | yes | 18,644 | 40 | 85,229 |
| `formation_mech` | 417898 | formation | yes | 6,207 | 40 | 119,963 |
| `formation_flyer` | 421e98 | formation | yes | 4 | 32 | 109 |
| `base_update` | 403ca8 | base | no | 272,712 | 39 | 1,802,222 |
| `base_parts` | 403914 | base | no | 309,900 | 40 | 2,492,646 |
| `base_part_chain` | 403890 | base | no | 0 | 8 | 40 |
| `base_part_blast` | 4036c0 | base | no | 0 | 8 | 42 |
| `base_part_debris` | 40379c | base | no | 0 | 8 | 42 |
| `engage_end` | 41c478 | mech_combat | yes | 0 | 11 | 55 |
| `skirt_check` | 41de9c | mech_combat | no | 2,321 | 23 | 89,691 |
| `attack_step` | 41c594 | mech_combat | no | 1,374 | 10 | 4,340 |
| `base_attack` | 41c86c | mech_combat | no | 0 | 8 | 28,298 |
| `flyer_attack` | 41c9cc | mech_combat | no | 0 | 3 | 101 |
| `face_off` | 41d41c | mech_combat | no | 947 | 15 | 47,107 |
| `flank_attack` | 41d4e4 | mech_combat | no | 0 | 8 | 6,373 |
| `drive_off` | 41def0 | mech_combat | no | 0 | 4 | 3,573 |
| `flyer_attacking` | 422ca8 | flyer_states | no | 11,756 | 16 | 39,615 |
| `flyer_follow` | 422598 | flyer_states | no | 0 | 0 | 0 |
| `flyer_formation` | 422a50 | flyer_states | no | 638 | 28 | 37,533 |
| `flyer_search` | 422a80 | flyer_states | no | 638 | 26 | 27,447 |
| `flyer_patrol` | 422b34 | flyer_states | no | 0 | 2 | 10,086 |
| `flyer_scout` | 422bdc | flyer_states | no | 0 | 0 | 0 |
| `flyer_decision` | 422d00 | flyer_states | no | 14 | 28 | 756 |
| `flyer_engage` | 422bf0 | flyer_states | no | 2 | 16 | 32 |
| `mech_contact` | 412800 | mech_notices | no | 940 | 30 | 37,597 |
| `mech_kill` | 415710 | mech_notices | no | 3 | 17 | 71 |
| `mech_attacked` | 41f7b8 | mech_notices | no | 77 | 16 | 2,703 |
| `mech_alert` | 41f790 | mech_notices | no | 0 | 6 | 75 |
| `base_aim_node` | 403548 | actor_virtuals | no | 56,076 | 39 | 1,130,344 |
| `base_offsets` | 40351c | actor_virtuals | no | 0 | 1 | 2,063 |
| `base_condition` | 4052b4 | actor_virtuals | yes | 0 | 15 | 562,284 |
| `actor_attacked` | 411aec | actor_virtuals | no | 76 | 20 | 2,814 |
| `base_part_position` | 406808 | actor_virtuals | no | 0 | 11 | 28,823 |
| `mech_aim_node` | 417b98 | actor_virtuals | no | 63,660 | 40 | 1,331,999 |
| `mech_offsets` | 4155c4 | actor_virtuals | no | 316 | 40 | 28,036 |
| `mech_armour` | 4154d0 | actor_virtuals | yes | 2,255 | 20 | 52,510 |
| `mech_target_scale` | 415498 | actor_virtuals | yes | 12,505 | 27 | 56,768 |
| `mech_engage_roll` | 41cabc | actor_virtuals | no | 25 | 26 | 1,374 |
| `mech_link_position` | 41dd2c | actor_virtuals | no | 23 | 10 | 201 |
| `col_query` | 40c9d4 | col | no | 125 | 24 | 5,459 |
| `col_group` | 40c8fc | col | no | 803 | 24 | 17,773 |
| `col_shape` | 40c8c8 | col | no | 1,194 | 24 | 40,966 |
| `col_bound` | 40c4c4 | col | no | 1,194 | 24 | 40,966 |
| `debris_table` | 4089bc | debris_burst | yes | 48 | 25 | 1,086 |
| `debris_burst` | 408530 | debris_burst | no | 59 | 25 | 1,481 |
| `debris_pick` | 4086bc | debris_burst | no | 84 | 25 | 1,922 |
| `debris_piece` | 408588 | debris_burst | no | 238 | 25 | 5,741 |
| `debris_init` | 408940 | debris_burst | no | 242 | 25 | 5,783 |
| `debris_spin` | 408b74 | debris_burst | no | 238 | 25 | 5,667 |
| `debris_spin_any` | 408bb4 | debris_burst | no | 224 | 25 | 5,507 |
| `travel_step` | 41d9cc | mech_travel | no | 0 | 3 | 2,796 |
| `follow_step` | 41daac | mech_travel | no | 0 | 2 | 5,427 |
| `sleep_step` | 41c418 | mech_travel | no | 0 | 0 | 0 |
| `ram_step` | 41e570 | mech_travel | no | 0 | 0 | 0 |
| `ram_move` | 41e488 | mech_travel | no | 0 | 0 | 0 |
| `dead_step` | 41e554 | mech_travel | no | 10,366 | 15 | 96,200 |
| `leader_route` | 41fb60 | mech_travel | no | 844 | 29 | 33,449 |
| `point_move` | 41fac4 | mech_travel | no | 844 | 30 | 55,750 |
| `fire_permit` | 41c3c8 | mech_travel | yes | 7,041 | 38 | 168,916 |
| `lc_base_update` | 4045c8 | lc_base | no | 0 | 6 | 59,304 |
| `projectile_create` | 40a9c4 | lc_base | no | 6 | 13 | 385 |
| `weapon_record` | 40fe84 | lc_base | yes | 31 | 40 | 1,967 |
| `key_axes` | 45a4cc | key_axes | yes | 8,261 | 40 | 87,954 |
| `mech_part_hit` | 4188c8 | mech_part_hit | no | 31 | 15 | 1,276 |
| `mouse_dispatch` | 452d40 | mouse_events | no | 8,261 | 40 | 87,954 |
| `mouse_queue` | 453078 | mouse_events | no | 0 | 8 | 514 |
| `base_hit_test` | 405038 | hit_test | no | 460,234 | 24 | 3,508,754 |
| `mech_hit_test` | 418ba8 | hit_test | no | 34,631 | 24 | 848,861 |
| `flyer_hit_test` | 421c8c | hit_test | no | 17,953 | 13 | 71,725 |
| `flyer_part` | 421bb4 | flyer_part | no | 0 | 5 | 10 |
| `weapon_eject` | 40f57c | weapon_eject | no | 7 | 16 | 218 |
| `weapon_damage` | 40ee0c | weapon_eject | no | 100 | 32 | 10,571 |
| `beam_damage` | 40ee38 | weapon_eject | no | 45 | 31 | 4,521 |
| `gun_damage` | 40ee90 | weapon_eject | no | 55 | 31 | 4,370 |
| `targ_damage` | 40ef6c | weapon_eject | no | 0 | 2 | 76 |
| `mech_fly` | 4198f4 | mech_fly | no | 0 | 3 | 4,450 |
| `sfx_play` | 462830 | sfx | no | 60 | 40 | 1,105 |
| `sfx_play_at` | 4628e0 | sfx | no | 738 | 36 | 12,869 |
| `sfx_place` | 46299c | sfx | no | 3,849 | 36 | 41,175 |
| `sfx_refresh` | 46297c | sfx | no | 3,111 | 16 | 28,306 |
| `sfx_stop` | 462ac4 | sfx | no | 3 | 11 | 211 |
| `sfx_rate` | 463114 | sfx | no | 0 | 3 | 4,267 |
| `sfx_voice` | 463400 | sfx | no | 13 | 40 | 332 |
| `sfx_lip` | 4634b0 | sfx | no | 39 | 40 | 2,029 |
| `sfx_lip_due` | 4634a0 | sfx | no | 26 | 15 | 1,697 |
| `removal_flush` | 478644 | removal | no | 74,438 | 40 | 888,324 |
| `pool_free` | 471bc8 | removal | no | 866 | 30 | 24,295 |
| `removal_append` | 478628 | removal | no | 866 | 30 | 24,295 |
| `pool_prev` | 471c70 | pool_iter | no | 4,940,351 | 40 | 50,947,290 |
| `pool_next` | 471c18 | pool_iter | no | 4,941,619 | 40 | 50,984,603 |
| `pool_query` | 404bc0 | pool_query | no | 16,849 | 33 | 490,364 |
| `map_click` | 44d6e0 | map_click | no | 0 | 4 | 30 |
| `meteor_create` | 409b44 | meteor | no | 4 | 12 | 17 |
| `shape_instance` | 490b64 | meteor | no | 894 | 40 | 18,421 |
| `region_absorb` | 40d3ec | region_damage | no | 96 | 33 | 2,658 |
| `region_spread` | 40cf44 | region_damage | no | 49 | 22 | 907 |
| `region_entry` | 40ca58 | region_damage | yes | 150 | 22 | 2,102 |
| `region_find` | 40ca8c | region_damage | no | 26 | 22 | 452 |
| `weapon_select` | 410708 | panel_weapons | no | 5,297 | 19 | 35,408 |
| `weapon_find` | 4106ac | panel_weapons | no | 0 | 5 | 81 |
| `panel_weapon` | 432a50 | panel_weapons | no | 0 | 5 | 81 |
| `bullet_create` | 40b43c | bullet_ctor | no | 280 | 24 | 3,204 |
| `bullet_init` | 40af6c | bullet_ctor | no | 280 | 24 | 3,204 |
| `def_lookup` | 40ffc8 | bullet_ctor | yes | 293 | 38 | 5,731 |
| `fire_alloc` | 46b430 | fire | no | 7 | 15 | 83 |
| `fire_create` | 46b48c | fire | no | 7 | 15 | 83 |
| `fire_release` | 46b62c | fire | no | 3 | 9 | 28 |
| `results_writer` | 42412c | results_writer | no | 0 | 0 | 0 |
| `score_sum` | 423e88 | results_writer | no | 0 | 0 | 0 |
| `vehicle_value` | 418e60 | results_writer | no | 0 | 0 | 0 |
| `list_append` | 426ac8 | results_writer | no | 0 | 0 | 0 |
| `flight_key` | 45a324 | flight_keys | no | 224 | 32 | 10,590 |
| `rotate_words` | 47ff8c | transform_helpers | no | 4 | 12 | 17 |
| `matrix_point` | 4801d4 | transform_helpers | no | 1,003,420 | 40 | 12,234,078 |
| `region_hit` | 40da38 | region_break | no | 51 | 33 | 1,905 |
| `region_break` | 40d434 | region_break | no | 26 | 22 | 452 |
| `hud_key` | 432bc8 | hud_keys | no | 6,202 | 40 | 72,412 |
| `button_press` | 438d9c | hud_keys | no | 5 | 40 | 307 |
| `button_release` | 4530bc | hud_keys | no | 12 | 40 | 328 |
| `command_key` | 45feb8 | commands | no | 6,202 | 40 | 72,412 |
| `playback_key` | 45fe6c | commands | no | 6,198 | 40 | 71,943 |
| `stream_open` | 4730e8 | vfs | yes | 307 | 40 | 11,257 |
| `stream_close` | 47307c | vfs | yes | 686 | 40 | 24,446 |
| `stream_read` | 4735b8 | vfs | yes | 111,072 | 40 | 4,782,088 |
| `stream_seek` | 4733a4 | vfs | yes | 763 | 40 | 27,488 |
| `stream_tell` | 473368 | vfs | yes | 783 | 40 | 28,374 |
| `stream_size` | 473334 | vfs | yes | 74 | 40 | 2,004 |
| `vol_lookup` | 468064 | vfs | yes | 306 | 40 | 11,199 |
| `member_lookup` | 47cdf8 | vfs | yes | 2,049 | 40 | 79,013 |
| `explosion_dtor` | 407e48 | destructors | no | 253 | 27 | 5,481 |
| `fire_dtor` | 46b574 | destructors | no | 7 | 9 | 61 |
| `beam_dtor` | 40ba94 | destructors | no | 0 | 16 | 8,275 |
| `debris_dtor` | 40f7e6 | destructors | no | 242 | 25 | 5,533 |
| `flat_dtor` | 41bd46 | destructors | no | 2 | 14 | 48 |
| `meteor_dtor` | 424034 | destructors | no | 4 | 12 | 17 |
| `bullet_dtor` | 40b64d | destructors | no | 280 | 23 | 3,184 |
| `rocket_dtor` | 40ab8d | destructors | no | 6 | 13 | 382 |
| `shape_dtor` | 490bdc | destructors | no | 798 | 30 | 14,723 |
| `shape_base_dtor` | 48be20 | destructors | no | 798 | 30 | 14,723 |
| `weapon_frame` | 410b40 | weapon_slots | no | 6,200 | 40 | 73,910 |
| `ready_step` | 410a3c | weapon_slots | no | 6,200 | 40 | 73,910 |
| `selected_ready` | 410a04 | weapon_slots | no | 10,233 | 39 | 103,221 |
| `chain_ready` | 410970 | weapon_slots | no | 28,833 | 40 | 557,864 |
| `latch_set` | 410f04 | weapon_slots | yes | 5,648 | 6 | 12,282 |
| `link_toggle` | 410f14 | weapon_slots | no | 0 | 4 | 138 |
| `member_toggle` | 410cd0 | weapon_slots | no | 0 | 2 | 3 |
| `gauge_sync` | 4420c8 | weapon_slots | no | 6,200 | 40 | 73,910 |
| `gauge_state` | 442120 | weapon_slots | no | 6,201 | 40 | 73,923 |
| `gauge_link` | 442054 | weapon_slots | no | 0 | 4 | 92 |
| `gauge_button` | 442154 | weapon_slots | no | 0 | 5 | 82 |
| `gauge_mode` | 441f90 | weapon_slots | yes | 2 | 40 | 95 |
| `collision_walk` | 427c68 | obstacle_walk | yes | 57,500 | 40 | 595,974 |
| `shape_cell` | 427360 | obstacle_walk | yes | 334 | 9 | 26,993 |
| `convoy_leader` | 46a5bc | obstacle_walk | yes | 49,584 | 22 | 222,804 |
| `map_areas` | 44d450 | hdd_orders | no | 0 | 5 | 91 |
| `map_mode` | 44d9f4 | hdd_orders | no | 0 | 5 | 114 |
| `wingman_select` | 44da98 | hdd_orders | no | 0 | 5 | 162 |
| `order_cancel` | 44dc10 | hdd_orders | no | 0 | 4 | 52 |
| `slider_event` | 4483a0 | throttle | no | 88 | 31 | 1,734 |
| `slider_base` | 43904c | throttle | no | 88 | 31 | 1,734 |
| `shield_step` | 413af8 | throttle | yes | 0 | 0 | 0 |
| `panel_frame` | 4327ac | panel_frame | no | 6,199 | 40 | 73,870 |
| `subject_status` | 412dc4 | objective_preds | no | 26,512 | 38 | 195,737 |
| `subject_group` | 412bd0 | objective_preds | yes | 26,512 | 38 | 195,737 |
| `subject_actor` | 412bbc | objective_preds | yes | 0 | 0 | 0 |
| `group_down` | 412be4 | objective_preds | yes | 0 | 7 | 25,538 |
| `route_flag` | 41324c | objective_preds | yes | 64 | 26 | 329 |
| `state_reached` | 413920 | objective_preds | no | 0 | 14 | 706 |
| `group_safe` | 413950 | objective_preds | no | 0 | 8 | 170 |
| `group_marked` | 412d90 | objective_preds | yes | 0 | 1 | 18 |
| `group_flagged` | 412c58 | objective_preds | yes | 0 | 1 | 4 |
| `bounds_margin` | 413780 | objective_preds | yes | 57 | 40 | 714 |
| `logic_frame` | 45f570 | logic_frame | no | 6,198 | 40 | 72,316 |
| `proj_record` | 40ffb0 | weapon_leaves | yes | 989 | 40 | 10,813 |
| `pitch_point` | 49295c | weapon_leaves | yes | 1,480 | 15 | 4,880 |
| `damage_words` | 40501c | weapon_leaves | yes | 0 | 9 | 1,686 |
| `bullet_charge` | 40b5a0 | weapon_leaves | no | 44 | 14 | 239 |
| `rocket_init` | 40a948 | weapon_leaves | no | 6 | 13 | 385 |
| `flyer_route` | 4224c4 | flyer_ai | no | 638 | 28 | 37,533 |
| `flyer_altitude` | 422108 | flyer_ai | yes | 11,429 | 28 | 73,843 |
| `flyer_pitch` | 422098 | flyer_ai | yes | 12,394 | 28 | 77,146 |
| `flyer_throttle` | 422260 | flyer_ai | yes | 0 | 0 | 0 |
| `group_fire_any` | 411a1d | part_helpers | no | 10,889 | 38 | 297,960 |
| `group_fire_sel` | 41194a | part_helpers | no | 6,198 | 40 | 71,465 |
| `part_list_dead` | 40cf10 | part_helpers | yes | 11 | 17 | 247 |
| `part_group_dead` | 40d9f8 | part_helpers | yes | 80 | 32 | 3,554 |
| `part_group_max` | 40dc58 | part_helpers | yes | 14 | 29 | 285 |
| `mission_guard` | 4240f4 | part_helpers | yes | 0 | 10 | 846 |
| `actor_offsets` | 411a74 | part_helpers | no | 356 | 11 | 5,611 |
| `command_queue` | 45a498 | command_queue | no | 26 | 18 | 132 |
| `cockpit_sync` | 41b130 | cockpit_sync | no | 6,200 | 40 | 73,910 |
| `throttle_record` | 447df8 | cockpit_sync | yes | 6,200 | 40 | 73,910 |
| `throttle_gauge` | 447da8 | cockpit_sync | no | 6,200 | 40 | 73,910 |
| `hud_record` | 43dbb0 | cockpit_sync | yes | 6,200 | 40 | 73,910 |
| `rotation_compose` | 47f058 | rotation_helpers | yes | 14 | 17 | 160 |
| `rotation_place` | 47f4f4 | rotation_helpers | yes | 262 | 20 | 4,605 |
| `render_root` | 45fca8 | render_root | no | 6,199 | 40 | 73,870 |
| `stage_dispatch` | 401d94 | render_root | no | 18,597 | 40 | 218,582 |
| `vector_copy` | 40707c | render_root | yes | 6,199 | 40 | 68,712 |
| `targ_aim` | 40e4dc | cold_paths | no | 0 | 1 | 104 |
| `targ_next` | 40e4ac | cold_paths | no | 0 | 1 | 7 |
| `missile_steer` | 40a488 | cold_paths | no | 0 | 1 | 50 |
| `ally_nearest` | 423974 | cold_paths | yes | 0 | 4 | 7 |
| `turbo_engage` | 40f09c | cold_paths | no | 0 | 2 | 174 |
| `member_assess` | 41b804 | cold_paths | no | 0 | 0 | 0 |
| `dts_load` | 47a144 | assets | yes | 20,674 | 40 | 951,457 |
| `bitmap_new` | 481cd4 | assets | yes | 1,059 | 40 | 42,138 |
| `bitmap_read` | 481d8c | assets | yes | 1,059 | 40 | 42,138 |
| `bitmap_array_new` | 482d88 | assets | yes | 69 | 40 | 2,552 |
| `bitmap_array_read` | 482e7c | assets | yes | 69 | 40 | 2,552 |
| `stream_destroy` | 472f64 | assets | yes | 305 | 40 | 11,177 |
| `prefs_video` | 45e510 | config | no | 0 | 0 | 0 |
| `prefs_load` | 459770 | config | no | 1 | 40 | 40 |
| `prefs_store` | 459b94 | config | no | 0 | 4 | 6 |
| `keyjoy_load` | 45b7a8 | config | no | 1 | 40 | 40 |
| `sound_config` | 46379c | config | no | 1 | 40 | 40 |
| `tape_save` | 45cca4 | config | no | 0 | 0 | 0 |
| `tape_restore` | 45cd5c | config | no | 0 | 0 | 0 |
| `pool_create` | 471ad8 | mission_load | no | 12 | 40 | 480 |
| `pool_nth` | 471cb0 | mission_load | yes | 65 | 40 | 2,101 |
| `subject_resolve` | 425348 | mission_load | yes | 15 | 40 | 518 |
| `action_copy` | 411b90 | mission_load | yes | 62 | 40 | 1,979 |
| `condition_init` | 415068 | mission_load | yes | 2 | 40 | 86 |
| `region_init` | 423358 | mission_load | yes | 8 | 40 | 248 |
| `timer_init` | 423104 | mission_load | yes | 0 | 7 | 28 |
| `group_array` | 423d38 | mission_load | no | 1 | 40 | 40 |
| `initial_damage` | 4178e8 | mission_load | no | 8 | 39 | 367 |
| `light_pools` | 4076e4 | mission_pools | no | 1 | 40 | 40 |
| `smoke_pools` | 4090cc | mission_pools | no | 1 | 40 | 40 |
| `projectile_pools` | 40a060 | mission_pools | no | 1 | 40 | 40 |
| `weapon_fit` | 4175dc | mission_weapons | no | 10 | 40 | 453 |
| `player_weapons` | 4104ec | mission_weapons | no | 1 | 40 | 40 |
| `weapon_factory` | 40fff8 | mission_weapons | no | 10 | 40 | 453 |
| `hdd_key` | 449ff4 | hdd_keys | no | 0 | 8 | 457 |
| `flush_buildings` | 405ea4 | stage_flush | no | 6,199 | 40 | 73,870 |
| `flush_lights` | 4077e8 | stage_flush | no | 6,199 | 40 | 73,870 |
| `flush_explosions` | 407b3c | stage_flush | no | 6,199 | 40 | 73,870 |
| `flush_debris` | 4083e0 | stage_flush | no | 6,199 | 40 | 73,870 |
| `flush_smoke_balls` | 408f84 | stage_flush | no | 6,199 | 40 | 73,870 |
| `flush_smoke` | 408f9c | stage_flush | no | 6,199 | 40 | 73,870 |
| `flush_attached` | 409904 | stage_flush | no | 6,249 | 40 | 75,754 |
| `flush_meteors` | 409a1c | stage_flush | no | 6,199 | 40 | 73,870 |
| `flush_projectiles` | 40a108 | stage_flush | no | 6,199 | 40 | 73,870 |
| `flush_mechs` | 41544c | stage_flush | no | 6,199 | 40 | 73,870 |
| `flush_flyers` | 4215a8 | stage_flush | no | 6,199 | 40 | 73,870 |
| `flush_fires` | 46b188 | stage_flush | no | 6,199 | 40 | 73,870 |
| `object_position` | 46b905 | manager_slots | yes | 494,122 | 40 | 5,357,924 |
| `shape_word` | 46b910 | manager_slots | yes | 505,467 | 40 | 6,216,888 |
| `sequence_next` | 478760 | manager_slots | yes | 0 | 1 | 2 |
| `sequence_prev` | 47877c | manager_slots | yes | 0 | 0 | 0 |
| `sequence_reach_up` | 47878c | manager_slots | yes | 0 | 0 | 0 |
| `sequence_reach_down` | 4787b8 | manager_slots | yes | 0 | 0 | 0 |
| `cyclic_next` | 4787e4 | manager_slots | yes | 33,789 | 40 | 743,117 |
| `cyclic_prev` | 478804 | manager_slots | yes | 0 | 0 | 0 |
| `cyclic_reach_up` | 478820 | manager_slots | yes | 0 | 0 | 0 |
| `cyclic_reach_down` | 47882c | manager_slots | yes | 0 | 0 | 0 |
| `gun_angle` | 403594 | actor_leaves | yes | 510 | 28 | 61,055 |
| `base_radius` | 4035a4 | actor_leaves | yes | 1,507,404 | 40 | 16,539,231 |
| `base_sight_radius` | 4035b8 | actor_leaves | yes | 3,264,810 | 40 | 29,222,012 |
| `base_part_word` | 40360c | actor_leaves | yes | 0 | 0 | 0 |
| `base_next_part` | 403624 | actor_leaves | yes | 0 | 0 | 0 |
| `base_in_front` | 405308 | actor_leaves | yes | 3,113 | 31 | 126,319 |
| `base_decide` | 406cfe | actor_leaves | yes | 0 | 0 | 0 |
| `actor_armour` | 411a4c | actor_leaves | yes | 0 | 11 | 30,744 |
| `actor_target_scale` | 411a54 | actor_leaves | yes | 0 | 13 | 3,984 |
| `actor_angle` | 411a5c | actor_leaves | yes | 6,306 | 30 | 113,766 |
| `actor_order` | 411a64 | actor_leaves | yes | 0 | 0 | 0 |
| `actor_keys` | 411a6c | actor_leaves | yes | 0 | 0 | 0 |
| `actor_aim_node` | 411a9c | actor_leaves | yes | 4,921 | 28 | 23,715 |
| `actor_radius` | 411aa4 | actor_leaves | yes | 12,406 | 28 | 90,379 |
| `actor_sight_radius` | 411aac | actor_leaves | yes | 131,832 | 28 | 1,026,281 |
| `actor_move` | 411ab4 | actor_leaves | yes | 0 | 0 | 0 |
| `actor_engage_roll` | 411ac0 | actor_leaves | yes | 38 | 28 | 1,790 |
| `actor_in_front` | 411acc | actor_leaves | yes | 8,561 | 31 | 187,283 |
| `actor_mount_word` | 411b04 | actor_leaves | yes | 6 | 9 | 78 |
| `actor_part_word` | 411b10 | actor_leaves | yes | 0 | 0 | 0 |
| `actor_next_part` | 411b1c | actor_leaves | yes | 0 | 0 | 0 |
| `mech_angle` | 415488 | actor_leaves | yes | 13,534 | 40 | 308,541 |
| `mech_condition` | 415504 | actor_leaves | yes | 53,039 | 35 | 296,746 |
| `mech_radius` | 415518 | actor_leaves | yes | 53,035 | 40 | 924,940 |
| `mech_sight_radius` | 41552c | actor_leaves | yes | 257,349 | 39 | 6,095,121 |
| `mech_part_word` | 415540 | actor_leaves | yes | 0 | 1 | 104 |
| `mech_next_part` | 415558 | actor_leaves | yes | 0 | 1 | 7 |
| `mech_mount_word` | 4155ac | actor_leaves | yes | 0 | 7 | 276 |
| `flyer_target_scale` | 421584 | actor_leaves | yes | 11,970 | 16 | 41,058 |
| `flyer_keys` | 4215c0 | actor_leaves | yes | 0 | 1 | 759 |
| `flyer_condition` | 421e84 | actor_leaves | yes | 0 | 0 | 0 |
| `actor_mark` | 42200c | actor_leaves | yes | 0 | 6 | 186 |
| `object_zero` | 4786cb | actor_leaves | yes | 0 | 0 | 0 |
| `equip_ready` | 40ecd4 | weapon_methods | yes | 0 | 18 | 69,989 |
| `beam_ready` | 40ecdc | weapon_methods | yes | 9,811 | 39 | 328,719 |
| `gun_ready` | 40ed6c | weapon_methods | yes | 26,719 | 31 | 271,115 |
| `elf_ready` | 40eda0 | weapon_methods | yes | 0 | 17 | 18,942 |
| `weapon_trigger` | 40f8ad | weapon_methods | yes | 5,196 | 39 | 62,909 |
| `elf_trigger` | 40e680 | weapon_methods | yes | 0 | 3 | 1,122 |
| `laew_trigger` | 40e71c | weapon_methods | no | 0 | 0 | 0 |
| `equip_trigger` | 40e678 | weapon_methods | yes | 0 | 0 | 0 |
| `beam_step` | 40f00c | weapon_methods | no | 22,147 | 39 | 814,314 |
| `elf_step` | 40f3d8 | weapon_methods | no | 2,294 | 27 | 116,107 |
| `turbo_step` | 40f0d0 | weapon_methods | no | 0 | 13 | 38,607 |
| `beam_adjust` | 40f48c | weapon_methods | yes | 0 | 2 | 5 |
| `beam_preset` | 40f4d8 | weapon_methods | yes | 0 | 1 | 5 |
| `beam_fill` | 40f4f0 | weapon_methods | yes | 0 | 0 | 0 |
| `beam_drain` | 4116ad | weapon_methods | yes | 0 | 0 | 0 |
| `weapon_priority` | 4111e2 | weapon_methods | yes | 24,461 | 38 | 6,516,511 |
| `beam_priority` | 40f504 | weapon_methods | yes | 34,595 | 39 | 4,130,824 |
| `gun_clear` | 4115f8 | weapon_methods | yes | 0 | 0 | 0 |
| `beam_clear` | 4116cf | weapon_methods | yes | 0 | 0 | 0 |
| `weapon_stock` | 4111e9 | weapon_methods | yes | 125 | 40 | 12,648 |
| `gun_stock` | 40f520 | weapon_methods | yes | 183 | 38 | 7,629 |
| `weapon_gauge_match` | 40df00 | weapon_methods | yes | 0 | 2 | 31 |
| `beam_gauge_match` | 40df08 | weapon_methods | yes | 0 | 4 | 275 |
| `gun_gauge_match` | 40df1c | weapon_methods | yes | 0 | 2 | 232 |
| `weapon_spent` | 40f8a4 | weapon_methods | yes | 0 | 6 | 22,182 |
| `beam_spent` | 40ed34 | weapon_methods | yes | 1,992 | 22 | 128,915 |
| `gun_spent` | 40ed48 | weapon_methods | yes | 2,050 | 17 | 281,956 |
| `weapon_ammo` | 40e62c | weapon_methods | yes | 29,424 | 40 | 1,368,413 |
| `gun_ammo` | 40e644 | weapon_methods | yes | 41,531 | 38 | 1,479,006 |
| `mech_ctor` | 415bb0 | mission_ctors | no | 10 | 40 | 453 |
| `flyer_ctor` | 4215f4 | mission_ctors | no | 4 | 32 | 109 |
| `building_factory` | 405314 | mission_ctors | no | 50 | 40 | 1,503 |
| `group_ctor` | 423b34 | mission_ctors | no | 38 | 40 | 1,149 |
| `base_marks` | 405db0 | mission_ctors | no | 2 | 37 | 86 |
| `weapon_target_word` | 40e484 | bridged_leaves | yes | 0 | 1 | 4 |
| `list_next` | 411e40 | bridged_leaves | yes | 0 | 3 | 69 |
| `list_prev` | 411e60 | bridged_leaves | yes | 0 | 3 | 22 |
| `r_key_toggle` | 41b468 | bridged_leaves | no | 0 | 0 | 0 |
| `nav_marker_set` | 434974 | bridged_leaves | yes | 0 | 0 | 0 |
| `throttle_handler` | 447e08 | bridged_leaves | yes | 88 | 31 | 1,734 |
| `view_pref_toggle` | 45fe38 | bridged_leaves | yes | 0 | 1 | 1 |
| `matrix_ctor` | 495132 | bridged_leaves | yes | 12,574 | 40 | 150,528 |
| `rotation_ctor` | 495194 | bridged_leaves | yes | 12,574 | 40 | 150,528 |
| `rotation_base_ctor` | 49514f | bridged_leaves | yes | 24,395 | 40 | 244,046 |
| `ts_obj_ctor` | 402278 | object_shapes | no | 25 | 40 | 1,009 |
| `det_anim_shape` | 40332c | object_shapes | no | 15 | 40 | 556 |
| `det_anim_static` | 403368 | object_shapes | no | 39 | 40 | 1,056 |
| `det_anim_record` | 4033a4 | object_shapes | no | 10 | 40 | 453 |
| `anim_instance_ctor` | 478838 | object_shapes | no | 25 | 40 | 1,009 |
| `object_track` | 402374 | object_shapes | no | 59 | 40 | 2,432 |
| `thread_add` | 478a3c | object_shapes | no | 59 | 40 | 2,432 |
| `thread_ctor` | 478e48 | object_shapes | no | 59 | 40 | 2,432 |
| `shape_word_block` | 402fc0 | object_shapes | no | 71 | 40 | 3,930 |
| `collision_load` | 40cd88 | collision_data | no | 11 | 40 | 485 |
| `collision_read` | 40ccf8 | collision_data | no | 76 | 40 | 3,085 |
| `collision_entry` | 40cc50 | collision_data | no | 103 | 40 | 4,339 |
| `collision_volume` | 40cc14 | collision_data | no | 296 | 40 | 12,993 |
| `collision_points` | 40c7c4 | collision_data | no | 296 | 40 | 12,993 |
| `collision_bounds` | 40ccc8 | collision_data | no | 134 | 40 | 6,219 |
| `collision_radius` | 40c5d0 | collision_data | yes | 603 | 40 | 34,090 |
| `collision_scan` | 40cec0 | collision_data | no | 31 | 40 | 1,880 |
| `collision_match` | 40ce78 | collision_data | no | 189 | 40 | 11,255 |
| `actor_load` | 4253d8 | mission_actors | no | 1 | 40 | 40 |
| `dmg_load` | 40d160 | damage_data | no | 11 | 40 | 485 |
| `dmg_read` | 40d09c | damage_data | no | 11 | 40 | 485 |
| `dmg_region_read` | 40cff8 | damage_data | no | 324 | 40 | 14,489 |
| `dmg_region_copy` | 40d050 | damage_data | no | 31 | 40 | 1,880 |
| `dmg_capacity_set` | 40d128 | damage_data | no | 31 | 40 | 1,880 |
| `dmg_region_set` | 40d240 | damage_data | no | 31 | 40 | 1,880 |
| `dmg_part_store` | 40d284 | damage_data | no | 10 | 40 | 453 |
| `col_part_collect` | 40cb84 | damage_data | no | 10 | 40 | 453 |
| `col_part_gather` | 40cafc | damage_data | no | 66 | 40 | 2,867 |
| `damage_state_ctor` | 40d2cc | damage_data | no | 14 | 40 | 562 |
| `damage_state_bind` | 40d354 | damage_data | no | 14 | 40 | 562 |
| `mech_join` | 417aa8 | group_joins | no | 10 | 40 | 453 |
| `flyer_join` | 421ee8 | group_joins | no | 4 | 32 | 109 |
| `building_join` | 405c3c | group_joins | no | 50 | 40 | 1,503 |
| `footprint_marks` | 470ed4 | group_joins | no | 50 | 40 | 1,503 |
| `cell_flag_set` | 470ddc | group_joins | no | 55,155 | 40 | 690,121 |
| `weapon_ctor` | 40df30 | weapon_ctors | no | 31 | 40 | 1,880 |
| `beam_ctor` | 40e074 | weapon_ctors | no | 9 | 40 | 536 |
| `gun_ctor` | 40e140 | weapon_ctors | no | 22 | 39 | 1,282 |
| `equip_ctor` | 40e234 | weapon_ctors | no | 0 | 20 | 62 |
| `ecm_ctor` | 40e274 | weapon_ctors | no | 0 | 17 | 17 |
| `turbo_ctor` | 40e2bc | weapon_ctors | no | 0 | 13 | 16 |
| `targ_ctor` | 40e308 | weapon_ctors | no | 0 | 6 | 9 |
| `shield_ctor` | 40e344 | weapon_ctors | no | 0 | 8 | 11 |
| `energy_ctor` | 40e380 | weapon_ctors | no | 0 | 9 | 9 |
| `zone_load` | 42789c | terrain_load | no | 1 | 40 | 40 |
| `grid_ctor` | 46befc | terrain_load | no | 1 | 40 | 40 |
| `height_cache_ctor` | 495268 | terrain_load | no | 1 | 40 | 40 |
| `terrain_materials` | 46c754 | terrain_load | no | 1 | 40 | 40 |
| `terrain_cells` | 46c4c4 | terrain_load | no | 1 | 40 | 40 |
| `cell_mark_set` | 470d74 | terrain_load | no | 65,688 | 40 | 1,838,719 |
| `weapon_types` | 40fc8c | stage2_types | no | 1 | 40 | 40 |
| `weapon_entry` | 40f8bc | stage2_types | no | 33 | 40 | 1,320 |
| `rocket_types` | 40a818 | stage2_types | no | 1 | 40 | 40 |
| `bullet_types` | 40ade0 | stage2_types | no | 1 | 40 | 40 |
| `beam_types` | 40b6e0 | stage2_types | no | 1 | 40 | 40 |
| `mech_forms` | 41fdb0 | stage2_types | no | 1 | 40 | 40 |
| `flyer_forms` | 422d90 | stage2_types | no | 1 | 40 | 40 |
| `mech_types` | 42049c | type_loads | no | 1 | 40 | 40 |
| `mech_type_load` | 4201a8 | type_loads | no | 4 | 40 | 205 |
| `lod_remap` | 420090 | type_loads | no | 4 | 40 | 205 |
| `node_remap` | 41ff88 | type_loads | no | 1,303 | 40 | 69,745 |
| `remap_word` | 41ff68 | type_loads | yes | 1,647 | 40 | 88,711 |
| `mech_type_select` | 41ff10 | type_loads | no | 10 | 40 | 453 |
| `gun_list_load` | 40fee8 | type_loads | no | 4 | 40 | 205 |
| `gun_classes` | 40fc50 | type_loads | no | 4 | 40 | 205 |
| `node_list` | 40304c | type_loads | no | 28 | 40 | 1,430 |
| `gun_slot_base` | 40feb4 | type_loads | yes | 4 | 40 | 205 |
| `col_slot_base` | 40cbd4 | type_loads | yes | 11 | 40 | 485 |
| `dmg_slot_base` | 40d200 | type_loads | yes | 11 | 40 | 485 |
| `flyer_types` | 422fb4 | type_loads | no | 1 | 40 | 40 |
| `flyer_type_load` | 422ed0 | type_loads | no | 1 | 32 | 32 |
| `flyer_type_select` | 422e90 | type_loads | no | 4 | 32 | 109 |
| `mfd_button` | 446844 | mfd_orders | no | 4 | 10 | 91 |
| `mfd_click` | 4470c0 | mfd_orders | no | 0 | 1 | 17 |
| `mfd_order` | 447248 | mfd_orders | no | 0 | 7 | 65 |
| `order_call` | 43166c | mfd_orders | no | 0 | 7 | 65 |
| `group_order` | 4231a4 | mfd_orders | no | 0 | 7 | 65 |
| `menu_code` | 43f9c0 | mfd_orders | yes | 0 | 7 | 92 |
| `menu_toggle` | 43f9f8 | mfd_orders | yes | 0 | 7 | 28 |
| `mfd_entry` | 4470a4 | mfd_orders | yes | 0 | 1 | 6 |
| `mfd_target` | 446fb0 | mfd_orders | no | 4 | 10 | 44 |
| `mfd_view_next` | 446fc4 | mfd_orders | yes | 0 | 1 | 2 |
| `mfd_range` | 446ff0 | mfd_orders | no | 0 | 1 | 11 |
| `order_setup` | 431530 | mfd_orders | no | 1 | 40 | 40 |
| `base_flatten` | 47129c | base_flatten | no | 1 | 40 | 40 |
| `flood_sum` | 470fe8 | base_flatten | no | 21 | 40 | 658 |
| `flood_fill` | 471128 | base_flatten | no | 21 | 40 | 658 |
| `mark_raster` | 47136c | base_flatten | no | 2 | 37 | 62 |
| `cell_flag_get` | 470e00 | base_flatten | yes | 1,228,192 | 40 | 14,670,723 |
| `base_types` | 405fac | building_types | no | 1 | 40 | 40 |
| `base_pools` | 405df4 | building_types | no | 1 | 40 | 40 |
| `world_load` | 424308 | mission_world | no | 1 | 40 | 40 |
| `actor_list_init` | 411df8 | mission_world | no | 1 | 40 | 40 |
| `gun_slot_table` | 40fecc | mission_world | no | 1 | 40 | 40 |
| `dmg_slot_table` | 40d218 | mission_world | no | 1 | 40 | 40 |
| `col_slot_table` | 40cbec | mission_world | no | 1 | 40 | 40 |
| `actor_contact` | 4127f8 | slot_leaves | yes | 10,120 | 28 | 147,088 |
| `actor_part_position` | 411a3c | slot_leaves | yes | 0 | 0 | 0 |
| `actor_part_facing` | 411a44 | slot_leaves | yes | 1 | 2 | 2 |
| `actor_kill` | 411b2c | slot_leaves | yes | 1 | 5 | 10 |
| `actor_link_position` | 411b34 | slot_leaves | yes | 10 | 7 | 28 |
| `actor_radial_hit` | 411b3c | slot_leaves | no | 0 | 0 | 0 |
| `equip_sync` | 40f144 | weapon_syncs | no | 0 | 7 | 23,253 |
| `ecm_sync` | 40f184 | weapon_syncs | no | 0 | 12 | 16,674 |
| `turbo_sync` | 40f1f0 | weapon_syncs | no | 0 | 13 | 30,062 |
| `beam_sync` | 40f288 | weapon_syncs | no | 6,200 | 39 | 219,404 |
| `gun_sync` | 40f330 | weapon_syncs | no | 12,400 | 28 | 163,519 |
| `laew_sync` | 40f3b8 | weapon_syncs | no | 0 | 0 | 0 |
| `laew_fire` | 40ea04 | weapon_syncs | no | 0 | 0 | 0 |
| `weapon_none_28` | 40e95c | weapon_syncs | yes | 0 | 0 | 0 |
| `weapon_none_38` | 4111ce | weapon_syncs | yes | 0 | 1 | 1 |
| `weapon_none_3c` | 4111d3 | weapon_syncs | yes | 0 | 2 | 6 |
| `weapon_none_40` | 4111d8 | weapon_syncs | yes | 0 | 0 | 0 |
| `weapon_none_44` | 4111dd | weapon_syncs | yes | 0 | 0 | 0 |
| `weapon_none_4c` | 40f89f | weapon_syncs | yes | 0 | 0 | 0 |
| `mission_end` | 461ff0 | mission_end | no | 0 | 0 | 0 |
| `mission_root` | 461608 | mission_root | no | 1 | 40 | 40 |
| `map_frame` | 44c988 | hdd_map | no | 0 | 8 | 4,961 |
| `map_key` | 44cc68 | hdd_map_keys | no | 0 | 8 | 385 |
| `map_press` | 44d5c0 | hdd_map_keys | no | 0 | 3 | 66 |
| `wingman_pick` | 44d82c | hdd_map_keys | no | 0 | 1 | 1 |
| `wingman_index` | 433134 | hdd_map_keys | no | 11 | 31 | 1,184 |
| `mode_next` | 44ee48 | hdd_map_keys | no | 0 | 0 | 0 |
| `mode_prev` | 44ee88 | hdd_map_keys | no | 0 | 0 | 0 |
| `target_cycle1` | 44ef08 | hdd_map_keys | no | 0 | 3 | 11 |
| `target_cycle0` | 44ef1c | hdd_map_keys | no | 0 | 2 | 6 |
| `target_cycle` | 44ef30 | hdd_map_keys | no | 0 | 3 | 17 |
| `explosion_pools` | 407b54 | effect_pools | no | 1 | 40 | 40 |
| `debris_pools` | 408860 | effect_pools | no | 1 | 40 | 40 |
| `debris_set` | 40874c | effect_pools | no | 6 | 40 | 281 |
| `debris_record` | 4083f8 | effect_pools | no | 42 | 40 | 1,969 |
| `flat_pools` | 4097a8 | effect_pools | no | 1 | 40 | 40 |
| `meteor_pools` | 409a34 | effect_pools | no | 1 | 40 | 40 |
| `fire_pools` | 46b1a8 | effect_pools | no | 1 | 40 | 40 |
| `smoke_balls` | 408fb8 | effect_pools | no | 1 | 40 | 40 |
| `weapon_shape_word` | 40e61c | fit_leaves | no | 15 | 40 | 1,066 |
| `mount_entry` | 40fea0 | fit_leaves | no | 63 | 40 | 3,216 |
| `mount_table` | 420634 | fit_leaves | no | 59 | 40 | 3,011 |
| `shield_split` | 413ac8 | fit_leaves | no | 10 | 40 | 453 |
| `energy_rate` | 417d08 | fit_leaves | no | 10 | 40 | 453 |
| `equip_slots` | 40fb2c | fit_leaves | no | 10 | 40 | 453 |
| `node_state_fill` | 417530 | fit_leaves | no | 15 | 40 | 1,066 |
| `camera_attached` | 401078 | camera_view | no | 81,775 | 40 | 1,077,681 |
| `camera_orbit_set` | 40109c | camera_view | no | 1 | 40 | 88 |
| `camera_attach_set` | 40111c | camera_view | no | 1 | 40 | 88 |
| `camera_turn_step` | 401c20 | camera_view | no | 0 | 16 | 11,054 |
| `camera_speed_step` | 401c4c | camera_view | no | 0 | 16 | 5,527 |
| `outside_camera` | 401c74 | camera_view | no | 6,198 | 40 | 74,624 |
| `view_apply` | 45de30 | camera_view | no | 6,198 | 40 | 72,322 |
| `view_subject_set` | 45df34 | camera_view | no | 0 | 14 | 55 |
| `view_offsets` | 45edb0 | camera_view | no | 1 | 40 | 88 |
| `camera_current` | 45edf8 | camera_view | no | 6,092 | 40 | 60,568 |
| `part_shapes_bind` | 4030d0 | sim_leaves | no | 87,297 | 36 | 698,876 |
| `parts_intact` | 403668 | sim_leaves | yes | 0 | 0 | 0 |
| `formation_slot` | 405b9c | sim_leaves | no | 0 | 0 | 0 |
| `explosion_entry` | 407b20 | sim_leaves | yes | 0 | 0 | 0 |
| `flat_draw` | 40991c | sim_leaves | no | 42,560 | 38 | 361,647 |
| `projectile_draw` | 40a120 | sim_leaves | no | 9,130 | 23 | 56,824 |
| `beam_done` | 40c2a0 | sim_leaves | no | 0 | 16 | 8,275 |
| `weapon_by_mount` | 410670 | sim_leaves | yes | 0 | 0 | 0 |
| `ptr_list_add` | 411dd4 | sim_leaves | no | 0 | 0 | 0 |
| `box_square_init` | 413a90 | sim_leaves | no | 0 | 0 | 0 |
| `twist_sound_latch` | 41a6d0 | sim_leaves | no | 6,198 | 37 | 67,863 |
| `pitch_sound_latch` | 41a994 | sim_leaves | no | 6,198 | 37 | 67,863 |
| `mech_type_index` | 41ff00 | sim_leaves | yes | 0 | 0 | 0 |
| `flyer_type_index` | 422e80 | sim_leaves | yes | 0 | 0 | 0 |
| `cockpit_notice` | 420a68 | sim_leaves | no | 7 | 14 | 87 |
| `radio_notice` | 420a98 | sim_leaves | no | 2 | 16 | 190 |
| `notice_time` | 426aec | sim_leaves | yes | 0 | 0 | 0 |
| `mfd_key` | 4469e8 | mfd_keys | no | 117 | 31 | 2,336 |
| `mfd_alt_key` | 446c38 | mfd_keys | no | 117 | 31 | 2,873 |
| `mfd_key_select` | 447158 | mfd_keys | no | 0 | 6 | 60 |
| `menu_select` | 43f9e0 | mfd_keys | yes | 0 | 6 | 60 |
| `mfd_entry_next` | 44703c | mfd_keys | no | 0 | 0 | 0 |
| `mfd_entry_prev` | 447070 | mfd_keys | no | 0 | 0 | 0 |
| `hdd_execute` | 44a1a0 | hdd_exec | no | 6,199 | 40 | 73,866 |

Live counts: check-r15-1 (40 suites, 789 hooks replaced, own runtime); 40 suites. "–" means the hook has no counter in that source.
<!-- end of the generated hooks table -->

"Nested" hooks are reached only through outer distance functions whose
replacements call core directly. `heading_sin`, `inverse_sine` and
`manager_reset` (standalone) have no live calls in any suite; their evidence
is in vitro only.

## How a hook runs

The framework contract (ABIs, spans/regions, compare isolation, rollback,
limits) is in [hook/README.md](../../hook/README.md). In short:

- Modes per hook: `original` (no patch), `replaced` (only our code runs),
  `compare` (the original runs on real state, the replacement on a private
  copy; the original result is returned and differences are counted and
  logged). During a compare's original phase every nested hook calls its
  original, whatever its own mode.
- Installation gates: the proxy checks executable name, file size and the
  full SHA-256 of `DBSIM.EXE`
  (`8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad`,
  724,512 bytes, image base `0x00400000`, image size `0x002d8000`) before any
  hook is installed. The framework then checks the loaded PE headers, the
  exact entry signature and the instruction boundaries of the relocated
  prologue. Any failure rolls back all hooks; nothing is patched on disk.
- Initialization runs once, outside DllMain, at the first forwarded call. The
  tick logger installs first (it checks the unpatched RNG prologue), then all
  function hooks, then the optional [live coverage](live_coverage.md).
- Adapters (`*_hooks.h`) translate the original arguments and memory layout
  into core calls. Inside the original process guest addresses equal host
  addresses (`math_native_memory`); tables are read from the loaded image
  (for example the quarter sine table at RVA `0xa25c4`), never embedded.
- Replacements that still need unreplaced game code call it at its original
  address (`es2_guest_call`, clip/callee callbacks); those hooks are
  `compare_unsupported`.

## Configuration

```ini
[hooks]
rng_next=compare
rng_bounded=original
mismatch_log=<es2-data>\logs\<fresh-run>\hooks.jsonl
mismatch_limit=32
```

Only `original`, `replaced` and `compare` are accepted, case-insensitively;
missing keys mean `original`. Malformed or truncated values refuse all
function hooks. An absent section or all-original selection returns before
hashing, allocation or log creation, and tick records gain no hook fields.
`mismatch_log` is required whenever a hook compares; it is created with
`CREATE_NEW` and never overwritten. `mismatch_limit` (decimal, default and 0
= 32) caps the attempted records shared by all hooks; counters continue past
the cap.

`configure-tick-log.ps1`, `run-tick-log-mission.ps1` and
`run-oracle-regression.ps1` accept `-Hooks "name=mode,..."`; compare mode
defaults the mismatch log to `<TickLogPath>.hooks.jsonl`
(`-HookMismatchLog` overrides it). Names, duplicates, modes, absolute paths,
existing outputs and collisions with the other run outputs are checked before
deployment. Omitting `-Hooks` removes a previous `[hooks]` section. Unless
`-SkipTests` is given, the hook, core and proxy fixture tests run first.

The CW3220 import shim is configured separately: an `[rtl]` section in the INI
named by the environment variable `ES2_RTL_CONFIG` (keys `enabled`,
`imports`, one mode per import; `compare`/`replaced` for the 20 pure
imports), see [rtl_imports.md](rtl_imports.md#the-iat-slot-shim) and
[the pure imports as own code](rtl_imports.md#the-pure-imports-as-own-code).

## Tick fields and mismatch records

While any hook is configured, each tick record (schema 2, [tick_log.md](tick_log.md))
appends `hk_<name>_calls`, `_mismatches`, `_skipped` and `_bypassed` for
both RNG hooks and for every other non-original hook. They count hook
entries, not abstract operations. `skipped` means no usable snapshot (alias,
unreadable state, nesting limit); `bypassed` counts nested original phases
and pass-through. Zero mismatches is evidence only together with nonzero,
non-skipped calls.

With `ground_update` non-original, ticks also carry cumulative counters of
the calls made by the replacement, in callback order:
`hk_ground_update_call_402628`, `_418f40`, `_46e180`, `_418f74`, `_4027fc`,
`_492ee0`, `_462830`, `_4792ac`, `_4195c8`. With `ground_collide`
non-original: `hk_ground_collide_call_404ae4`, `_46e498`, `_492f24`,
`_434010`, `_462830`, the virtual slots `_slot_20`, `_slot_5c`, `_slot_68`,
`_slot_70`, `_slot_74`, `_slot_7c` (attributed by vtable offset, not by the
resolved method) and three path counters:

| Field (after `hk_ground_collide_`) | Meaning |
| --- | --- |
| `path_contact` | Step 3 entered; `slot_68` also increments. `slot_20`/`slot_70` distinguish the impact-damage branch. |
| `path_slope_drift` | Step 6 actually accumulates drift; the uphill early return does not increment it. |
| `path_slide_damage` | Step 7 exceeds the slide threshold: six `call_492f24` and `slot_74` calls, then one `call_434010` and `call_462830`. |

Other groups with callee or path counters declare them in their adapter header
(`*_hook_counters`) and in `proxy/callee_counters.h`; `ground_effects` is
described under [ground_effects 4195c8](#ground_effects-4195c8).

The counters come from the optional read-only observer in `core/include/es2/core/collision.h`
and the proxy's callee table (`callee_counters.h`); they never change
arguments, memory or results. Tick lines are limited to `TICK_LOG_LINE_MAX`
(`proxy/tick_log.h`, 262,144 bytes). `path_counters_bound_test` sets every
counter of an all-replaced run to UINT64_MAX, including all optional fields,
and checks that the line stays below the limit: calls, bypasses and callee
counters use UINT64_MAX, mismatches/skipped are necessarily zero for fixed
replaced mode (no compare or snapshots). This is a reachable-mode bound, not a
claim about arbitrarily assigned diagnostic counters. The no-hooks tick
fixture is 2,505 bytes with SHA-256
`405eb398f723f39f37b683cf9e49fede35b7eddeb85d88f8b1ec416aa369a718`
(`path_counters_original_test`). Both oracle comparison scripts ignore all
diagnostic fields (fixed simulation-field lists, checked by
`counter_normalization_test`).

A compare mismatch is logged as one JSON line (synthetic fixture example):

```json
{"event":"hook_mismatch","hook":"own_add","call":1,"frame":70,"args":[123456,3],"original":13,"replacement":14,"first_diff_span":0,"first_diff_offset":0}
```

`call` is the per-hook entry count, `frame` the counter at context RVA
`0xd24bc+0xbf` (null if unreadable). Arguments and results are unsigned
32-bit decimals. The differing offset is relative to the named span;
return-only or custom-comparator mismatches have null span fields. No state
contents are written.

## Validation

Every hook passes, in this order:

1. Synthetic core tests (`bazel test //core/...`, x86 and x64).
2. The in-vitro harness: the original function runs in a private mapping of
   the hash-checked `DBSIM.EXE` with all imports trapped, against the core on
   about one million generated inputs, plus 256 installed-adapter cases per
   mode; every original conditional branch site is taken both ways or proved
   unreachable (`PROVED(...)` lines of `invitro/branch_sites.h`), and every
   hook has at least one deliberate mutation that the harness catches
   ([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md); one caught
   mutation per hook in `check_mutations.ps1`).
3. `bazel test //...` (`.bazelrc` supplies `ES2_DATA_ROOT`). `hook_registry_test`
   reads this file as a data input and checks the marker above and the
   selections below.
4. One [oracle regression](oracle_regression.md) in `replaced` mode with all
   registered hooks; `compare` runs only to diagnose a difference.

Complete replacement selection:

```powershell
-Hooks "rng_next=replaced,rng_bounded=replaced,dist2d_point=replaced,dist2d_vector=replaced,dist2d_delta=replaced,dist_octagonal=replaced,dist_gate=replaced,time_scale=replaced,timer_word_step=replaced,timer_dword_step=replaced,timer_arm=replaced,heading_cos=replaced,heading_sin=replaced,multiply_q8=replaced,movement_step=replaced,hud_speed=replaced,transform_point=replaced,vector_subtract=replaced,distance_display=replaced,euler_matrix=replaced,matrix_compose=replaced,rotate_point=replaced,matrix_transpose=replaced,pose_matrix=replaced,pose_interpolate=replaced,scalar_interpolate=replaced,track_count=replaced,inverse_sine=replaced,angle_ratio=replaced,matrix_angles=replaced,multiply_q10=replaced,approach_word=replaced,energy_accumulate=replaced,clip_pose=replaced,pose_parent=replaced,track_forward=replaced,track_inverse=replaced,track_scaled=replaced,track_scaled_inverse=replaced,manager_reset=replaced,animation_step=replaced,track_advance=replaced,track_target=replaced,track_rate=replaced,actor_animate=replaced,ground_animate=replaced,terrain_height=replaced,actor_snapshot=replaced,actor_rollback=replaced,ground_update=replaced,ground_collide=replaced,ground_effects=replaced,track_clip=replaced,dist3d_delta=replaced,dist3d_forward=replaced,heading_point=replaced,shield_damage=replaced,vehicle_hit=replaced,building_hit=replaced,building_part=replaced,projectile_def=replaced,bullet_def=replaced,mask_merge=replaced,radius_hit=replaced,weapon_step=replaced,weapon_fire=replaced,weapon_prepare=replaced,bullet_flight=replaced,projectile_flight=replaced,vehicle_part=replaced,target_emit=replaced,emit=replaced,alt_emit=replaced,impact=replaced,turret=replaced,sweep=replaced,side_scale=replaced,cell=replaced,segment=replaced,sweep_terrain=replaced,slope_test=replaced,normalize=replaced,diagonal=replaced,edge_test=replaced,face_test=replaced,face_cross=replaced,contact=replaced,terrain_line=replaced,terrain_face=replaced,camera_update=replaced,object_line=replaced,obstacle_steer=replaced,track_sort=replaced,track_step=replaced,track_list=replaced,node_matrices=replaced,terrain_pose=replaced,shield_geometry=replaced,debris_heading=replaced,trigger_scan=replaced,trigger_regions=replaced,region_test=replaced,trigger_group=replaced,trigger_fire=replaced,explosion_update=replaced,light_level=replaced,fire_update=replaced,group_fire=replaced,weapon_ready=replaced,part_condition=replaced,sphere_hits=replaced,sphere_test=replaced,track_route=replaced,track_poses=replaced,pose_list=replaced,track_seek=replaced,turret_aim=replaced,part_ratio=replaced,slope_speed=replaced,random_damage=replaced,vehicle_control=replaced,objective_eval=replaced,actor_safe=replaced,target_known=replaced,order_target=replaced,order_done=replaced,subject_done=replaced,rivals_done=replaced,route_point=replaced,outcome_poll=replaced,status_eval=replaced,bounds_check=replaced,bounds_outside=replaced,explosion_create=replaced,object_init=replaced,light_create=replaced,pool_alloc=replaced,condition_table=replaced,condition_ratio=replaced,sight_check=replaced,group_state=replaced,target_bearing=replaced,part_position=replaced,fire_selected=replaced,fire_line=replaced,aim_point=replaced,leg_input=replaced,line_plane=replaced,target_select=replaced,target_valid=replaced,death_actions=replaced,group_death=replaced,combat_rating=replaced,condition_record=replaced,device_state=replaced,device_word=replaced,ready_tone=replaced,weapon_tally=replaced,vehicle_update=replaced,poly_clip=replaced,poly_spans=replaced,view_rect=replaced,cell_uv=replaced,flight_model=replaced,flyer_move=replaced,formation_steer=replaced,cockpit_valid=replaced,cockpit_arc=replaced,cockpit_next=replaced,cockpit_nearest=replaced,cockpit_select=replaced,player_route=replaced,route_closing=replaced,route_advance=replaced,timer_scan=replaced,mission_timer=replaced,status_check=replaced,object_query=replaced,grid_sweep=replaced,set_length=replaced,flyer_input=replaced,flyer_command=replaced,flyer_bank=replaced,flyer_steer=replaced,vector_angle=replaced,aim_angles=replaced,projectile_steer=replaced,bullet_steer=replaced,torso_twist=replaced,torso_pitch=replaced,weapon_converge=replaced,weapon_keys=replaced,smoke_ball_update=replaced,smoke_update=replaced,smoke_ball_init=replaced,debris_update=replaced,ground_height=replaced,meteor_update=replaced,group_activate=replaced,spawn_point=replaced,order_update=replaced,group_assess=replaced,order_actor=replaced,order_point=replaced,actor_destination=replaced,actor_pass=replaced,contact_expire=replaced,contact_scan=replaced,contact_share=replaced,mech_order=replaced,wingman_order=replaced,sincos_pair=replaced,rotate_2d=replaced,mount_offset=replaced,mount_base=replaced,target_class=replaced,script_start=replaced,attack_target=replaced,order_resume=replaced,torso_center=replaced,torso_aim=replaced,player_input=replaced,select_step=replaced,fire_group=replaced,weapons_v3c=replaced,group_toggle=replaced,tex_edge=replaced,convoy_steer=replaced,convoy_seek=replaced,convoy_lead=replaced,convoy_follow=replaced,convoy_drive=replaced,convoy_think=replaced,turret_target=replaced,part_facing=replaced,point_offset=replaced,threat_sum=replaced,engage_done=replaced,flank_step=replaced,retreat_check=replaced,part_pick=replaced,drive_apply=replaced,obstacle_test=replaced,convoy_collide=replaced,shield_max_set=replaced,shield_capacity=replaced,damage_score=replaced,facing_part=replaced,vehicle_keys=replaced,beam_create=replaced,weapon_range=replaced,weapon_choose=replaced,aim_fire=replaced,aim_target=replaced,target_search=replaced,hunt=replaced,line_blocked=replaced,clear_shot=replaced,group_move=replaced,death_sequence=replaced,shield_balance=replaced,result_conditions=replaced,map_pick=replaced,key_message=replaced,panel_visible=replaced,aim_select=replaced,attack_plan=replaced,attack_replan=replaced,group_scan=replaced,order_scan=replaced,retreat_fire=replaced,control_record=replaced,actor_step=replaced,state_frame=replaced,mech_move=replaced,mech_act=replaced,mech_decide=replaced,flyer_move_call=replaced,flyer_act=replaced,flyer_decide=replaced,formation_base=replaced,formation_mech=replaced,formation_flyer=replaced,base_update=replaced,base_parts=replaced,base_part_chain=replaced,base_part_blast=replaced,base_part_debris=replaced,engage_end=replaced,skirt_check=replaced,attack_step=replaced,base_attack=replaced,flyer_attack=replaced,face_off=replaced,flank_attack=replaced,drive_off=replaced,flyer_attacking=replaced,flyer_follow=replaced,flyer_formation=replaced,flyer_search=replaced,flyer_patrol=replaced,flyer_scout=replaced,flyer_decision=replaced,flyer_engage=replaced,mech_contact=replaced,mech_kill=replaced,mech_attacked=replaced,mech_alert=replaced,base_aim_node=replaced,base_offsets=replaced,base_condition=replaced,actor_attacked=replaced,base_part_position=replaced,mech_aim_node=replaced,mech_offsets=replaced,mech_armour=replaced,mech_target_scale=replaced,mech_engage_roll=replaced,mech_link_position=replaced,col_query=replaced,col_group=replaced,col_shape=replaced,col_bound=replaced,debris_table=replaced,debris_burst=replaced,debris_pick=replaced,debris_piece=replaced,debris_init=replaced,debris_spin=replaced,debris_spin_any=replaced,travel_step=replaced,follow_step=replaced,sleep_step=replaced,ram_step=replaced,ram_move=replaced,dead_step=replaced,leader_route=replaced,point_move=replaced,fire_permit=replaced,lc_base_update=replaced,projectile_create=replaced,weapon_record=replaced,key_axes=replaced,mech_part_hit=replaced,mouse_dispatch=replaced,mouse_queue=replaced,base_hit_test=replaced,mech_hit_test=replaced,flyer_hit_test=replaced,flyer_part=replaced,weapon_eject=replaced,weapon_damage=replaced,beam_damage=replaced,gun_damage=replaced,targ_damage=replaced,mech_fly=replaced,sfx_play=replaced,sfx_play_at=replaced,sfx_place=replaced,sfx_refresh=replaced,sfx_stop=replaced,sfx_rate=replaced,sfx_voice=replaced,sfx_lip=replaced,sfx_lip_due=replaced,removal_flush=replaced,pool_free=replaced,removal_append=replaced,pool_prev=replaced,pool_next=replaced,pool_query=replaced,map_click=replaced,meteor_create=replaced,shape_instance=replaced,region_absorb=replaced,region_spread=replaced,region_entry=replaced,region_find=replaced,weapon_select=replaced,weapon_find=replaced,panel_weapon=replaced,bullet_create=replaced,bullet_init=replaced,def_lookup=replaced,fire_alloc=replaced,fire_create=replaced,fire_release=replaced,results_writer=replaced,score_sum=replaced,vehicle_value=replaced,list_append=replaced,flight_key=replaced,rotate_words=replaced,matrix_point=replaced,region_hit=replaced,region_break=replaced,hud_key=replaced,button_press=replaced,button_release=replaced,command_key=replaced,playback_key=replaced,stream_open=replaced,stream_close=replaced,stream_read=replaced,stream_seek=replaced,stream_tell=replaced,stream_size=replaced,vol_lookup=replaced,member_lookup=replaced,explosion_dtor=replaced,fire_dtor=replaced,beam_dtor=replaced,debris_dtor=replaced,flat_dtor=replaced,meteor_dtor=replaced,bullet_dtor=replaced,rocket_dtor=replaced,shape_dtor=replaced,shape_base_dtor=replaced,weapon_frame=replaced,ready_step=replaced,selected_ready=replaced,chain_ready=replaced,latch_set=replaced,link_toggle=replaced,member_toggle=replaced,gauge_sync=replaced,gauge_state=replaced,gauge_link=replaced,gauge_button=replaced,gauge_mode=replaced,collision_walk=replaced,shape_cell=replaced,convoy_leader=replaced,map_areas=replaced,map_mode=replaced,wingman_select=replaced,order_cancel=replaced,slider_event=replaced,slider_base=replaced,shield_step=replaced,panel_frame=replaced,subject_status=replaced,subject_group=replaced,subject_actor=replaced,group_down=replaced,route_flag=replaced,state_reached=replaced,group_safe=replaced,group_marked=replaced,group_flagged=replaced,bounds_margin=replaced,logic_frame=replaced,proj_record=replaced,pitch_point=replaced,damage_words=replaced,bullet_charge=replaced,rocket_init=replaced,flyer_route=replaced,flyer_altitude=replaced,flyer_pitch=replaced,flyer_throttle=replaced,group_fire_any=replaced,group_fire_sel=replaced,part_list_dead=replaced,part_group_dead=replaced,part_group_max=replaced,mission_guard=replaced,actor_offsets=replaced,command_queue=replaced,cockpit_sync=replaced,throttle_record=replaced,throttle_gauge=replaced,hud_record=replaced,rotation_compose=replaced,rotation_place=replaced,render_root=replaced,stage_dispatch=replaced,vector_copy=replaced,targ_aim=replaced,targ_next=replaced,missile_steer=replaced,ally_nearest=replaced,turbo_engage=replaced,member_assess=replaced,dts_load=replaced,bitmap_new=replaced,bitmap_read=replaced,bitmap_array_new=replaced,bitmap_array_read=replaced,stream_destroy=replaced,prefs_video=replaced,prefs_load=replaced,prefs_store=replaced,keyjoy_load=replaced,sound_config=replaced,tape_save=replaced,tape_restore=replaced,pool_create=replaced,pool_nth=replaced,subject_resolve=replaced,action_copy=replaced,condition_init=replaced,region_init=replaced,timer_init=replaced,group_array=replaced,initial_damage=replaced,light_pools=replaced,smoke_pools=replaced,projectile_pools=replaced,weapon_fit=replaced,player_weapons=replaced,weapon_factory=replaced,hdd_key=replaced,flush_buildings=replaced,flush_lights=replaced,flush_explosions=replaced,flush_debris=replaced,flush_smoke_balls=replaced,flush_smoke=replaced,flush_attached=replaced,flush_meteors=replaced,flush_projectiles=replaced,flush_mechs=replaced,flush_flyers=replaced,flush_fires=replaced,object_position=replaced,shape_word=replaced,sequence_next=replaced,sequence_prev=replaced,sequence_reach_up=replaced,sequence_reach_down=replaced,cyclic_next=replaced,cyclic_prev=replaced,cyclic_reach_up=replaced,cyclic_reach_down=replaced,gun_angle=replaced,base_radius=replaced,base_sight_radius=replaced,base_part_word=replaced,base_next_part=replaced,base_in_front=replaced,base_decide=replaced,actor_armour=replaced,actor_target_scale=replaced,actor_angle=replaced,actor_order=replaced,actor_keys=replaced,actor_aim_node=replaced,actor_radius=replaced,actor_sight_radius=replaced,actor_move=replaced,actor_engage_roll=replaced,actor_in_front=replaced,actor_mount_word=replaced,actor_part_word=replaced,actor_next_part=replaced,mech_angle=replaced,mech_condition=replaced,mech_radius=replaced,mech_sight_radius=replaced,mech_part_word=replaced,mech_next_part=replaced,mech_mount_word=replaced,flyer_target_scale=replaced,flyer_keys=replaced,flyer_condition=replaced,actor_mark=replaced,object_zero=replaced,equip_ready=replaced,beam_ready=replaced,gun_ready=replaced,elf_ready=replaced,weapon_trigger=replaced,elf_trigger=replaced,laew_trigger=replaced,equip_trigger=replaced,beam_step=replaced,elf_step=replaced,turbo_step=replaced,beam_adjust=replaced,beam_preset=replaced,beam_fill=replaced,beam_drain=replaced,weapon_priority=replaced,beam_priority=replaced,gun_clear=replaced,beam_clear=replaced,weapon_stock=replaced,gun_stock=replaced,weapon_gauge_match=replaced,beam_gauge_match=replaced,gun_gauge_match=replaced,weapon_spent=replaced,beam_spent=replaced,gun_spent=replaced,weapon_ammo=replaced,gun_ammo=replaced,mech_ctor=replaced,flyer_ctor=replaced,building_factory=replaced,group_ctor=replaced,base_marks=replaced,weapon_target_word=replaced,list_next=replaced,list_prev=replaced,r_key_toggle=replaced,nav_marker_set=replaced,throttle_handler=replaced,view_pref_toggle=replaced,matrix_ctor=replaced,rotation_ctor=replaced,rotation_base_ctor=replaced,ts_obj_ctor=replaced,det_anim_shape=replaced,det_anim_static=replaced,det_anim_record=replaced,anim_instance_ctor=replaced,object_track=replaced,thread_add=replaced,thread_ctor=replaced,shape_word_block=replaced,collision_load=replaced,collision_read=replaced,collision_entry=replaced,collision_volume=replaced,collision_points=replaced,collision_bounds=replaced,collision_radius=replaced,collision_scan=replaced,collision_match=replaced,actor_load=replaced,dmg_load=replaced,dmg_read=replaced,dmg_region_read=replaced,dmg_region_copy=replaced,dmg_capacity_set=replaced,dmg_region_set=replaced,dmg_part_store=replaced,col_part_collect=replaced,col_part_gather=replaced,damage_state_ctor=replaced,damage_state_bind=replaced,mech_join=replaced,flyer_join=replaced,building_join=replaced,footprint_marks=replaced,cell_flag_set=replaced,weapon_ctor=replaced,beam_ctor=replaced,gun_ctor=replaced,equip_ctor=replaced,ecm_ctor=replaced,turbo_ctor=replaced,targ_ctor=replaced,shield_ctor=replaced,energy_ctor=replaced,zone_load=replaced,grid_ctor=replaced,height_cache_ctor=replaced,terrain_materials=replaced,terrain_cells=replaced,cell_mark_set=replaced,weapon_types=replaced,weapon_entry=replaced,rocket_types=replaced,bullet_types=replaced,beam_types=replaced,mech_forms=replaced,flyer_forms=replaced,mech_types=replaced,mech_type_load=replaced,lod_remap=replaced,node_remap=replaced,remap_word=replaced,mech_type_select=replaced,gun_list_load=replaced,gun_classes=replaced,node_list=replaced,gun_slot_base=replaced,col_slot_base=replaced,dmg_slot_base=replaced,flyer_types=replaced,flyer_type_load=replaced,flyer_type_select=replaced,mfd_button=replaced,mfd_click=replaced,mfd_order=replaced,order_call=replaced,group_order=replaced,menu_code=replaced,menu_toggle=replaced,mfd_entry=replaced,mfd_target=replaced,mfd_view_next=replaced,mfd_range=replaced,order_setup=replaced,base_flatten=replaced,flood_sum=replaced,flood_fill=replaced,mark_raster=replaced,cell_flag_get=replaced,base_types=replaced,base_pools=replaced,world_load=replaced,actor_list_init=replaced,gun_slot_table=replaced,dmg_slot_table=replaced,col_slot_table=replaced,actor_contact=replaced,actor_part_position=replaced,actor_part_facing=replaced,actor_kill=replaced,actor_link_position=replaced,actor_radial_hit=replaced,equip_sync=replaced,ecm_sync=replaced,turbo_sync=replaced,beam_sync=replaced,gun_sync=replaced,laew_sync=replaced,laew_fire=replaced,weapon_none_28=replaced,weapon_none_38=replaced,weapon_none_3c=replaced,weapon_none_40=replaced,weapon_none_44=replaced,weapon_none_4c=replaced,mission_end=replaced,mission_root=replaced,map_frame=replaced,map_key=replaced,map_press=replaced,wingman_pick=replaced,wingman_index=replaced,mode_next=replaced,mode_prev=replaced,target_cycle1=replaced,target_cycle0=replaced,target_cycle=replaced,explosion_pools=replaced,debris_pools=replaced,debris_set=replaced,debris_record=replaced,flat_pools=replaced,meteor_pools=replaced,fire_pools=replaced,smoke_balls=replaced,weapon_shape_word=replaced,mount_entry=replaced,mount_table=replaced,shield_split=replaced,energy_rate=replaced,equip_slots=replaced,node_state_fill=replaced,camera_attached=replaced,camera_orbit_set=replaced,camera_attach_set=replaced,camera_turn_step=replaced,camera_speed_step=replaced,outside_camera=replaced,view_apply=replaced,view_subject_set=replaced,view_offsets=replaced,camera_current=replaced,part_shapes_bind=replaced,parts_intact=replaced,formation_slot=replaced,explosion_entry=replaced,flat_draw=replaced,projectile_draw=replaced,beam_done=replaced,weapon_by_mount=replaced,ptr_list_add=replaced,box_square_init=replaced,twist_sound_latch=replaced,pitch_sound_latch=replaced,mech_type_index=replaced,flyer_type_index=replaced,cockpit_notice=replaced,radio_notice=replaced,notice_time=replaced,mfd_key=replaced,mfd_alt_key=replaced,mfd_key_select=replaced,menu_select=replaced,mfd_entry_next=replaced,mfd_entry_prev=replaced,hdd_execute=replaced"
```

Diagnostic compare selections, one hook per run; `hook_registry_test` requires
exactly one such line for every hook that accepts compare (the hooks before
`euler_matrix` in the registry always accept it and have no line; core wrappers
may bypass or eliminate nested hook entries, so check each hook's own
counters):

```powershell
-Hooks "euler_matrix=compare"
-Hooks "matrix_compose=compare"
-Hooks "rotate_point=compare"
-Hooks "matrix_transpose=compare"
-Hooks "pose_matrix=compare"
-Hooks "pose_interpolate=compare"
-Hooks "scalar_interpolate=compare"
-Hooks "track_count=compare"
-Hooks "inverse_sine=compare"
-Hooks "angle_ratio=compare"
-Hooks "matrix_angles=compare"
-Hooks "multiply_q10=compare"
-Hooks "approach_word=compare"
-Hooks "energy_accumulate=compare"
-Hooks "clip_pose=compare"
-Hooks "pose_parent=compare"
-Hooks "track_forward=compare"
-Hooks "track_inverse=compare"
-Hooks "track_scaled=compare"
-Hooks "track_scaled_inverse=compare"
-Hooks "manager_reset=compare"
-Hooks "track_target=compare"
-Hooks "track_rate=compare"
-Hooks "terrain_height=compare"
-Hooks "bullet_def=compare"
-Hooks "cell=compare"
-Hooks "device_state=compare"
-Hooks "device_word=compare"
-Hooks "dist3d_delta=compare"
-Hooks "dist3d_forward=compare"
-Hooks "heading_point=compare"
-Hooks "mask_merge=compare"
-Hooks "part_condition=compare"
-Hooks "part_ratio=compare"
-Hooks "projectile_def=compare"
-Hooks "shield_damage=compare"
-Hooks "slope_test=compare"
-Hooks "terrain_face=compare"
-Hooks "track_route=compare"
-Hooks "track_sort=compare"
-Hooks "vector_angle=compare"
-Hooks "weapon_ready=compare"
-Hooks "point_offset=compare"
-Hooks "route_point=compare"
-Hooks "region_test=compare"
-Hooks "ground_height=compare"
-Hooks "weapon_range=compare"
-Hooks "bounds_outside=compare"
-Hooks "bounds_check=compare"
-Hooks "target_known=compare"
-Hooks "order_target=compare"
-Hooks "fire_permit=compare"
-Hooks "mech_target_scale=compare"
-Hooks "mech_armour=compare"
-Hooks "base_condition=compare"
-Hooks "sincos_pair=compare"
-Hooks "rotate_2d=compare"
-Hooks "line_plane=compare"
-Hooks "object_query=compare"
-Hooks "grid_sweep=compare"
-Hooks "set_length=compare"
-Hooks "convoy_steer=compare"
-Hooks "slope_speed=compare"
-Hooks "key_axes=compare"
-Hooks "formation_base=compare"
-Hooks "formation_mech=compare"
-Hooks "formation_flyer=compare"
-Hooks "stream_open=compare"
-Hooks "stream_close=compare"
-Hooks "stream_read=compare"
-Hooks "stream_seek=compare"
-Hooks "stream_tell=compare"
-Hooks "stream_size=compare"
-Hooks "vol_lookup=compare"
-Hooks "member_lookup=compare"
-Hooks "contact_scan=compare"
-Hooks "collision_walk=compare"
-Hooks "shape_cell=compare"
-Hooks "convoy_leader=compare"
-Hooks "shield_step=compare"
-Hooks "subject_group=compare"
-Hooks "subject_actor=compare"
-Hooks "group_down=compare"
-Hooks "route_flag=compare"
-Hooks "group_marked=compare"
-Hooks "group_flagged=compare"
-Hooks "bounds_margin=compare"
-Hooks "proj_record=compare"
-Hooks "pitch_point=compare"
-Hooks "damage_words=compare"
-Hooks "flyer_altitude=compare"
-Hooks "flyer_pitch=compare"
-Hooks "flyer_throttle=compare"
-Hooks "part_list_dead=compare"
-Hooks "part_group_dead=compare"
-Hooks "part_group_max=compare"
-Hooks "mission_guard=compare"
-Hooks "throttle_record=compare"
-Hooks "hud_record=compare"
-Hooks "target_class=compare"
-Hooks "engage_end=compare"
-Hooks "debris_table=compare"
-Hooks "weapon_record=compare"
-Hooks "region_entry=compare"
-Hooks "def_lookup=compare"
-Hooks "rotation_compose=compare"
-Hooks "rotation_place=compare"
-Hooks "vector_copy=compare"
-Hooks "ally_nearest=compare"
-Hooks "dts_load=compare"
-Hooks "bitmap_new=compare"
-Hooks "bitmap_read=compare"
-Hooks "bitmap_array_new=compare"
-Hooks "bitmap_array_read=compare"
-Hooks "stream_destroy=compare"
-Hooks "object_position=compare"
-Hooks "shape_word=compare"
-Hooks "sequence_next=compare"
-Hooks "sequence_prev=compare"
-Hooks "sequence_reach_up=compare"
-Hooks "sequence_reach_down=compare"
-Hooks "cyclic_next=compare"
-Hooks "cyclic_prev=compare"
-Hooks "cyclic_reach_up=compare"
-Hooks "cyclic_reach_down=compare"
-Hooks "gun_angle=compare"
-Hooks "base_radius=compare"
-Hooks "base_sight_radius=compare"
-Hooks "base_part_word=compare"
-Hooks "base_next_part=compare"
-Hooks "base_in_front=compare"
-Hooks "base_decide=compare"
-Hooks "actor_armour=compare"
-Hooks "actor_target_scale=compare"
-Hooks "actor_angle=compare"
-Hooks "actor_order=compare"
-Hooks "actor_keys=compare"
-Hooks "actor_aim_node=compare"
-Hooks "actor_radius=compare"
-Hooks "actor_sight_radius=compare"
-Hooks "actor_move=compare"
-Hooks "actor_engage_roll=compare"
-Hooks "actor_in_front=compare"
-Hooks "actor_mount_word=compare"
-Hooks "actor_part_word=compare"
-Hooks "actor_next_part=compare"
-Hooks "mech_angle=compare"
-Hooks "mech_condition=compare"
-Hooks "mech_radius=compare"
-Hooks "mech_sight_radius=compare"
-Hooks "mech_part_word=compare"
-Hooks "mech_next_part=compare"
-Hooks "mech_mount_word=compare"
-Hooks "flyer_target_scale=compare"
-Hooks "flyer_keys=compare"
-Hooks "flyer_condition=compare"
-Hooks "actor_mark=compare"
-Hooks "object_zero=compare"
-Hooks "equip_ready=compare"
-Hooks "gun_ready=compare"
-Hooks "elf_ready=compare"
-Hooks "weapon_trigger=compare"
-Hooks "equip_trigger=compare"
-Hooks "weapon_priority=compare"
-Hooks "beam_priority=compare"
-Hooks "weapon_stock=compare"
-Hooks "gun_stock=compare"
-Hooks "weapon_gauge_match=compare"
-Hooks "beam_gauge_match=compare"
-Hooks "gun_gauge_match=compare"
-Hooks "weapon_spent=compare"
-Hooks "beam_spent=compare"
-Hooks "gun_spent=compare"
-Hooks "weapon_target_word=compare"
-Hooks "list_next=compare"
-Hooks "list_prev=compare"
-Hooks "nav_marker_set=compare"
-Hooks "throttle_handler=compare"
-Hooks "view_pref_toggle=compare"
-Hooks "matrix_ctor=compare"
-Hooks "rotation_ctor=compare"
-Hooks "rotation_base_ctor=compare"
-Hooks "pool_nth=compare"
-Hooks "subject_resolve=compare"
-Hooks "action_copy=compare"
-Hooks "condition_init=compare"
-Hooks "region_init=compare"
-Hooks "timer_init=compare"
-Hooks "latch_set=compare"
-Hooks "gauge_mode=compare"
-Hooks "beam_ready=compare"
-Hooks "elf_trigger=compare"
-Hooks "beam_adjust=compare"
-Hooks "beam_preset=compare"
-Hooks "beam_fill=compare"
-Hooks "beam_drain=compare"
-Hooks "gun_clear=compare"
-Hooks "beam_clear=compare"
-Hooks "weapon_ammo=compare"
-Hooks "gun_ammo=compare"
-Hooks "collision_radius=compare"
-Hooks "remap_word=compare"
-Hooks "gun_slot_base=compare"
-Hooks "col_slot_base=compare"
-Hooks "dmg_slot_base=compare"
-Hooks "menu_code=compare"
-Hooks "menu_toggle=compare"
-Hooks "mfd_entry=compare"
-Hooks "mfd_view_next=compare"
-Hooks "cell_flag_get=compare"
-Hooks "actor_contact=compare"
-Hooks "actor_part_position=compare"
-Hooks "actor_part_facing=compare"
-Hooks "actor_kill=compare"
-Hooks "actor_link_position=compare"
-Hooks "weapon_none_28=compare"
-Hooks "weapon_none_38=compare"
-Hooks "weapon_none_3c=compare"
-Hooks "weapon_none_40=compare"
-Hooks "weapon_none_44=compare"
-Hooks "weapon_none_4c=compare"
-Hooks "parts_intact=compare"
-Hooks "explosion_entry=compare"
-Hooks "weapon_by_mount=compare"
-Hooks "mech_type_index=compare"
-Hooks "flyer_type_index=compare"
-Hooks "notice_time=compare"
-Hooks "menu_select=compare"
```

Inspect calls, mismatches, skipped, bypassed, the complete tick/RNG trace and
the return files; an empty mismatch log alone is not evidence.

## Function notes

Contracts are derived from the reference bytes; addresses are DBSIM VAs at
image base `0x400000`. The detailed derivations are in
[player_movement.md](../reverse-engineering/player_movement.md) and
[rng.md](../reverse-engineering/rng.md).

### RNG: rng_next 492ee0, rng_bounded 492f24

| Hook | Signature | Relocated prefix |
| --- | --- | --- |
| `rng_next` | `55 8b ec 8b 45 08 0f be 50 70` | 6 bytes, lengths `1,2,3` |
| `rng_bounded` | `55 8b ec 53 8b 5d 08 53` | 7 bytes, lengths `1,2,1,3` |

Both take the 114-byte (`0x72`) generator state as argument 0 (compare span
offset 0) and clean up as the caller. The bounded wrapper reads the signed
low 16 bits of its second slot; its call at `0x492f2c` lies outside the
relocated prefix. With `rng_bounded=compare,rng_next=replaced` the bounded
original phase still draws through the original next; a replaced bounded
wrapper calls core directly. The 112-byte seed table is never included in
source or fixtures.

### Distance and timing

All entries use cdecl stack slots, caller cleanup and EAX/AX results. The
scale reference must have a nonzero low word; division faults and invalid
pointers are outside the contract.

| Hook | VA | Arguments and result | State / callees |
| --- | --- | --- | --- |
| `dist2d_point` | `0x4928d0` | Two pointers to XY dwords; EAX is their 2-D distance | Read-only 8 bytes each; `0x49545b`, `0x492a30`, `0x492a45` |
| `dist2d_vector` | `0x492a45` | **One** pointer to XY deltas; EAX is the vector magnitude | Read-only 8 bytes; `0x492a9a` |
| `dist2d_delta` | `0x492a9a` | Two 32-bit deltas; EAX distance | Pure wrapper around `0x47de4c` |
| `dist_octagonal` | `0x47de4c` | Two signed 32-bit deltas, including negative inputs; EAX distance bits | Leaf, no state |
| `dist_gate` | `0x49288c` | Two XYZ pointers; EAX **3-D** distance | Read-only 12 bytes each; `0x4953c4`, `0x492a5e`, `0x492a7d` |
| `time_scale` | `0x467894` | Reference low word first, elapsed low word second; full EAX quotient/clamp pattern | Leaf, no globals |
| `timer_word_step` | `0x467a48` | Timer pointer; AX countdown, upper EAX word from the incoming pointer | Reads word RVA `0xd3b64`; writes only object+1..2 |
| `timer_dword_step` | `0x467aa8` | Timer pointer; EAX countdown | Reads signed word RVA `0xd3b64`; writes only object+1..4 |
| `timer_arm` | `0x467ac4` | Timer pointer, signed low-word duration; EAX returns the pointer | Writes only object+1..4; no globals |

`0x49545b` and `0x4953c4` return their argument unchanged without memory
effects. `0x492a30` subtracts XY in place; `0x492a5e` subtracts Z and calls the
XY subtractor, only on the outer function's stack temporary.
`0x492a7d -> 0x492aaf -> 0x47de72` computes the 3-D metric. These helpers are
represented by pure core arithmetic and not hooked separately.

For ordinary deltas, the 2-D distance is `max(|dx|,|dy|) + floor(min/2)`.
Subtractions happen modulo 2^32 **before** taking magnitudes; the absolute
value leaves `0x80000000` unchanged, signed comparisons rank it below zero and
halving is an arithmetic shift. Examples: `(INT32_MIN,0)` returns
`0xc0000000`, `(INT32_MIN,INT32_MIN)` returns `0x40000000`, `(INT32_MAX,2)`
returns `0x80000000`. The core preserves all wrapped bits.

The 3-D routine sorts the wrapped magnitudes in signed descending order
`L,M,S` and returns, modulo 2^32, `L + SAR(M,2) + SAR(M,4) + SAR(M,5) + SAR(S,2)`.
Each term is truncated separately; one rounded multiplication would differ.
Equal magnitudes 31 return 46, 32 return 51; `(INT32_MIN,0,0)` returns
`0xe0000000`.

The scale helper sign-extends both low words, multiplies elapsed by 256 and
divides toward zero. It compares **AX as signed**, clamps it to `0x40..0x1c2`
and leaves EAX's quotient high word intact: `(reference=1,elapsed=256)`
returns `0x00010040`, `(125,-47)` returns `0xffff0040`, `(125,47)` returns 96.

The word timer subtracts modulo 2^16 and tests the sign; the dword timer
subtracts the sign-extended scale modulo 2^32 and tests SF (`JNS`, not a
saturating subtraction). So `0x8000 - 1` yields 32767 and `0x80000000 - 1`
yields INT32_MAX. Negative scales can increase a timer. Arm stores the signed
duration shifted left by 11. The timing descriptors' `user` is the loaded
image (for the scale word). Timer comparison spans are argument 0, offset 1,
size 2 or 4; the comparators check each pointer-derived upper result word
against its own object's address, so private snapshot addresses do not mask
unexpected result bits.

| Hook | Signature / relocated prefix | Instruction lengths |
| --- | --- | --- |
| dist2d_point | `55 8b ec 83 c4 f8` | 1,2,3 |
| dist2d_vector | `55 8b ec 53 8b 5d 08` | 1,2,1,3 |
| dist2d_delta | `55 8b ec 8b 45 0c` | 1,2,3 |
| dist_octagonal | `c8 00 00 00 8b 45 08` | 4,3 |
| dist_gate | `55 8b ec 83 c4 f4` | 1,2,3 |
| time_scale | `55 8b ec 0f bf 45 0c` | 1,2,4 |
| timer_word_step / timer_dword_step / timer_arm | `55 8b ec 8b 45 08` | 1,2,3 |

### Movement leaves

| Hook / VA | ABI and state | Quirks |
| --- | --- | --- |
| heading_cos / 47dff0 | s16 -> EAX; read-only RVA a25c4, 2,050 bytes | SAR/carry rounding and reflection; exhaustive 65,536 cases |
| heading_sin / 47e027 | s16 -> EAX; same table | subtract 16384, then wrap the low word; exhaustive |
| multiply_q8 / 47e0a0 | s32,s32 -> EAX; no state | signed 64-bit product, shifted low 32 bits |
| movement_step / 467924 | s16 -> EAX; read-only scale RVA d3b64, 2 bytes | Q8 floor and signed clamp -32767..32767 |
| hud_speed / 41bb3c | actor -> EAX; actor span 0..2c0, read-only indirect config at actor+1f2, 522 bytes | both branches, signed truncation and bias |
| transform_point / 48043c | point,matrix,out -> EAX; spans 12,32,12 | signed mode, Q14 rounding, wrap, sequential aliases; EAX=Z for translation-only, else the output pointer |
| vector_subtract / 492a30 | dst,src -> EAX=dst; spans 8,8 | sequential X/Y subtraction with wrap |
| distance_display / 47d694 | XY,XY -> EAX; spans 8,8 | the wrapped octagonal metric |

All movement candidates use stack cdecl; the Borland and stdcall bridges of
the framework are exercised only by fixtures. Startup x87 CW is `0x1332`
(hardware readback `0x1372`).

### Matrix, pose, inverse trig and energy

| Hook / VA | cdecl slots | Original Jcc sites |
| --- | ---: | ---: |
| `euler_matrix` / `47ebb8` | 2 | 18 |
| `matrix_compose` / `47fa20` | 3 | 5 |
| `rotate_point` / `480304` | 3 | 2 |
| `matrix_transpose` / `47df19` | 1 | 0 |
| `pose_matrix` / `478c1c` | 2 | 0 |
| `pose_interpolate` / `49270c` | 4 | 6 |
| `scalar_interpolate` / `47df48` | 5 | 1 |
| `track_count` / `4789b0` | 1 | 2 |
| `inverse_sine` / `47da4c` | 1 | 1 |
| `angle_ratio` / `47d32c` | 2 | 6 |
| `matrix_angles` / `47f9a0` | 2 | 1 |
| `multiply_q10` / `47e0b0` | 2 | 0 |
| `approach_word` / `467adc` | 3 | 4 |
| `energy_accumulate` / `413b38` | 2 | 3 |

All compare EDX:EAX (including unchanged incoming EDX and address-derived
results), all declared state and guard bytes. Tables are read through the
descriptor's image pointer with explicit read-only extents; the only embedded
original bytes are the short entry signatures. Euler modes 0–3, all nine
composition mode-class pairs, signed rotate classes, all eleven track counts,
pose half-turn ties, inverse sign/table branches, ratio axes/quadrants,
approach clamps and energy limits are exercised in vitro.

### clip_pose 478ef4

`es2_clip_pose_registers` replays the original's reads and writes in order
through the `es2_guest_memory` interface (`core/include/es2/core/guest.h`;
32-bit guest addresses, identity mapping in the proxy, an arena in
`//core:clip_pose_test`), so the unproved clip/frame/pose extents need no
bounds. Result EDX:EAX = T : clip (disabled clip) or T : 3*pose index. Span
T+0x00..0x2d, comparator `math_pointer0`, signature `55 8b ec 53 56 8b 55 0c`
(5 relocated bytes). Installed compare cases skip the two alias modes, which
compare mode cannot isolate. Mutation: dropping the pose-base reread fails
`//core:clip_pose_test` and in-vitro case 4.

### Padding wrappers 478bc4, 478f6c, 478ff4, 4790b4, 479194

`pose_parent`, `track_forward`, `track_inverse`, `track_scaled`,
`track_scaled_inverse` (`core/src/track.c`). They leave byte 19 of a local
matrix uninitialized, so that byte of the output matrix can be stack residue
([padding contract](../reverse-engineering/player_movement.md#padding-contract-for-the-five-wrappers)).
The proxy writes 0 there (`MATH_PADDING`); the comparators
(`math_padding_equal`) accept every byte except that one and treat EAX/EDX as
equal when both point to the same offset of the same span (real object versus
clone). Disabled tracks (T+0x20 == 0) in 478f6c/478ff4 return the incoming
EAX/EDX. Spans: pose 12, parent/out/input 32, track T+0x00..0x4d; quarter
table read-only region. Signatures are the frame prologue plus the next
instruction (6 relocated bytes). The scaled variants fault (#DE) like the
original when enabled with s16(T+0x1e) == 0. In vitro exact under stack poison
31/a7.

### manager_reset 478b7c and animation_step 40250c

([contracts](../reverse-engineering/player_movement.md#animation-step-40250c-and-manager-reset-478b7c))
`manager_reset`: spans M (0x1e) and R (32), writable indirect regions
[M+0x16] (32) and [M+0x1a] (0x4e; a null first track makes compare skip); the
comparator masks the nested padding byte T+0x41. `animation_step` calls the
track advance through a host callback (the game's 478d38, hooked or not);
`compare_unsupported`. The 65,536 in-vitro branch-coverage cases mask T+0x41
because the INT3 dispatch writes below ESP. Mutation: dropping the A+0x32
reset fails in-vitro case 0.

### track_advance 478d38

([contract](../reverse-engineering/player_movement.md#track-advance-478d38))
`es2_track_advance_registers` replaces the whole subtree (478aac, 4789d0,
478e8c, 479720, 478b00, 479ab0, 478c64). Clip frame steps of `ANSequence`
(vtable RVA 0xa684c) and `ANCyclicSequence` (0xa6810) run in core; any other
clip class calls its original virtual method. `compare_unsupported`. Its
prologue contains `66 8b 45 0c`, so the framework's whitelist accepts the 0x66
prefix for MOV opcodes 89/8b without immediate (`hook/tests/hook_fixture.c`).
In vitro: synthetic managers exact except byte 19 of written matrices.

### track_target 47967c, track_rate 4796d8, actor_animate 402604, ground_animate 418f40

([contracts](../reverse-engineering/player_movement.md#ground-animation-418f40-and-track-target-47967c))
`track_target` (with the route search 4793d4) and `track_rate` change only the
track (span T+0x00..0x51) and call the pure clip methods, so both support
compare mode (`math_track_state_equal`). `actor_animate` calls the game's
40250c, `ground_animate` the game's 4796d8 and 402604; both
`compare_unsupported`. `track_target` receives the entry EDX (its high word
survives the early exits). The clip fallback (`es2_clip_classes.other`)
passes all argument slots and returns EDX:EAX, because 4793d4 returns what the
reached methods leave in EAX/EDX.

### terrain_height 46e180

([contract](../reverse-engineering/player_movement.md#terrain-height-46e180))
Read-only grid lookup; compare mode is supported without spans (both phases
read the same grid). EDX is reproduced on every path (x, the corner pointer,
an interpolation term or the high half of the last IMUL).

### actor_snapshot 402628, actor_rollback 4027fc

([contract](../reverse-engineering/player_movement.md#snapshot-402628-and-rollback-4027fc))
Copies between the actor/manager and the global block at RVA 0xa8cd0 with
CW3220 `memcpy` semantics (dwords, then bytes; EAX = destination,
EDX = count). `compare_unsupported`. The in-vitro harness emulates CW3220
`memcpy` and compares a 4 KiB window of the global block.

### ground_update 41a360

([contract](../reverse-engineering/player_movement.md#ground-vehicle-update-41a360))
The callback's own logic runs in core; its nine callees are called at their
game addresses (`es2_ground_calls`), the height probe through a local point.
The effects callback forwards the last collision/clip/rollback EAX/EDX,
because effects with no track preserves those registers (confirmed 41a436,
41a4e5..41a53e). Four original/replaced update/effects combinations are
checked on 256 states each, including no-track actors. `compare_unsupported`.
In vitro the callees are logging stubs; the harness compares the call log,
arena, globals, registers and the callee-counter deltas.

### ground_collide 418f74

([contract](../reverse-engineering/player_movement.md#ground-collision-418f74))
The arithmetic helpers run in core; the virtual methods (+20/5c/68/70/74/7c)
and the game functions 404ae4, 46e498, 492f24, 434010, 462830 are called at
their addresses through one cdecl caller (`es2_guest_call`), with the
original's stack buffers passed as guest addresses. `compare_unsupported`.
Signature `55 8b ec 81 c4 38 ff ff ff` (9 relocated bytes). In vitro
(`tools/reference-game/invitro/collide_cases.h`): synthetic scenes exact
(EDX:EAX, arena, globals, call log); mutations caught at cases 6 (steep
threshold) and 33 (pushed high word).

Live counters of the all-replaced regression (`check-r15-1`,
`hook-counters.json`, last compared tick of each of the 40 suites):

| Suite | update calls | collide calls | `call_404ae4` | `call_46e498` | `slot_7c` / `slot_5c` | rollbacks `4027fc` / sound `462830` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| acceptance-60s | 4,236 | 4,236 | 4,236 | 4,236 | 228,224 / 190,620 | 0 / 0 |
| c1_01-success | 26,506 | 26,506 | 26,506 | 26,506 | 1,475,106 / 1,192,770 | 0 / 0 |
| c1_01-collision | 7,368 | 7,370 | 7,370 | 7,368 | 401,593 / 331,650 | 2 / 2 |

In `c1_01-collision` two collide entries return through step 4 (404ae4 AX
nonzero: an obstacle call without a following face call). The player vehicle
always has A+a3 set (sound, never the RNG recoil). The object contact (step 3,
`path_contact`) is reached in six suites (`c3_09-loadout-b` 38,
`c3_10-loadout-hercs` 100, `c3_09-wingman-orders` 8, `c5_05-moon-missiles`
17, `c3_09-orders-matrix` 16, `c3_10-hunt-targ` 7), slope drift (step 6) and
slide damage (step 7) only in `c3_09-loadout-b` (25 and 1), and the clip call
4792ac once, in `c3_10-hunt-targ`. Effects 4195c8 runs once per update in
every suite. Counter totals include ticks past the fixed comparison windows,
so they vary slightly between runs of the same suite.

### ground_effects 4195c8

([contract](../reverse-engineering/player_movement.md#ground-effects-4195c8))
`core/src/effects.c` owns attachment positioning and animation-latch
sequencing. The name follows `ground_update`/`ground_collide`; the detour is
on 4195c8 itself, so its second caller 41e488 also uses it. Five direct
callees (47ebb8, 48043c, 47de72, 4628e0, 434144) and virtual +20 go through
`es2_effects_call` (the guest-call contract plus optional entry registers);
independently configured Euler/transform hooks still apply. The callback
bridge accepts zero arguments for 434144 and passes EDX=A+92 through the
machine-call API. A caller-owned 12-byte guest scratch point avoids narrowing
host pointers inside core, including on x64.

Signature `55 8b ec 83 c4 a0 53 56 57`, 6 relocated bytes, lengths 1/2/3.
Actor cache span A+12..33; read-only regions 4d24ea (4 bytes) and
6c56bc (12 bytes). Additional writes are explicitly outside fixed regions:
runtime-indexed attachment positions/headings/cache flags, variable latch
bytes, and sound/feedback state. `compare_unsupported` refuses installation;
these spans are not a claim of compare isolation. The diagnostic selection
is `-Hooks "ground_effects=compare"` (expected refusal). Use the complete
replacement selection above for a live regression.

Only while this hook is non-original, ticks include
`hk_ground_effects_call_47ebb8`, `_48043c`, `_47de72`, `_4628e0`, `_434144`,
and `hk_ground_effects_slot_20`. These count actual callback invocations.
One effects entry per ground update is expected; the second caller may add
entries. Per effects call, each processed attachment yields one lookup,
transform and distance call; Euler is conditional on cache invalidity, and
sound/feedback counts are bounded by processed attachments. Feedback can
occur without sound when A+a3 is set and the actor is distant.

In vitro: 1,001,024 direct cases and 256 installed replacement cases; all 30
branches both ways (240 total). Four update/effects mode combinations pass,
including the no-track entry-register contract. The real feedback probe
verifies EDX preservation; three effects mutations are caught. Synthetic
effects tests pass on x86 and x64.

Live (`check-r15-1`): exactly one effects entry per ground update in all 40
suites, so the second caller adds none (4,236 / 26,506 / 7,368 in
`acceptance-60s` / `c1_01-success` / `c1_01-collision`). In `c1_01-success`:
77,363 attachment lookups (slot +20), transforms 48043c and distances 47de72,
305 sound calls 4628e0 and 229 feedback calls 434144.

## Framework limits that matter for new hooks

- One simulation thread; install and uninstall at a quiescent point on that
  thread. Calls from other threads pass through to the original without
  counting.
- At most six integer argument slots (`ES2_HOOK_MAX_ARGS`) and four spans plus
  regions with 16,384 state bytes in total (`hook/hook.h`); compare nesting is
  bounded to eight levels, replaced nesting to 32. Same-argument spans,
  overlapping regions and unremapped aliases are refused or skipped.
- Regions use in-place isolation (snapshot, original, restore, replacement,
  restore the original's post-state); multi-hop indirect regions must be
  read-only. Runtime-indexed pointers, allocation, I/O and undeclared state are
  outside the contract; use `compare_unsupported` for such functions.
- Thiscall, varargs, floating-point or aggregate arguments, exception
  unwinding and longjmp through a detour are unsupported.
- Prologues: the relocated instructions must be on the whitelist of
  `hook/hook.c` (`instruction_size`; list in `hook/README.md`). It includes
  `enter imm16,imm8` (`c8`, four bytes; fixture `hook/tests/hook_fixture.c`),
  `mov r16,imm16` (`66 b8+r`, four bytes, for `0x45fe6c`),
  `add/sub/cmp r/m16,imm8` (`66 83 /0,/5,/7`, for `0x4240f4`), `83 /1`
  (OR r/m32, imm8) and a near RET as the last byte of the relocated range, so
  a function that is exactly the detour's five bytes (`push ebp; mov ebp,esp;
  pop ebp; ret`) can be hooked whole; the trampoline then returns by itself
  ([hook README](../../hook/README.md),
  [vtables.md](../reverse-engineering/vtables.md#actor-slot-leaves)). At least
  19 registered hooks start with `enter 0,0` (e.g. `euler_matrix`,
  `sincos_pair`, `matrix_point`). The DBSIM map's entry probe cannot sit on
  such an entry, so these functions have no map probe row; their live counts
  come from the hook counters.
- Stack: `es2_hook_dispatch_extended` reserves 208 bytes of its own stack on
  the replaced path; compare mode needs the large buffers. The legacy
  dispatcher reserves about 16 KB. DBSIM's main thread has a 1 MiB stack; the
  replaced ground callback nests about five extended frames
  ([hook/README.md](../../hook/README.md), "Stack use").

## How to add a hook group

Every per-group list of the proxy, the in-vitro harness and their tests
derives from the registry (the last `ES2_HOOK` argument is the group), so a
group `G` touches only its own files and appends:

- own: `core/` module and test, `proxy/<G>_hooks.h` (one descriptor array,
  extensions, own callee counter arrays and the required
  `static callee_counter *G_hook_counters(unsigned hook,unsigned *count)`,
  NULL and 0 for a hook without game calls), `invitro/<G>_cases.h` (defines
  `static int G_cases(unsigned index,unsigned limit)`);
- appended: `proxy/hook_registry.h` and `proxy/hook-names.txt` (one line per
  hook), `invitro/branch_sites.h` (own `BRANCH` lines and
  `PROVED(hook,site,edge,"reason")` lines for proved unreachable edges),
  `check_mutations.ps1` (and a line in `invitro/MUTATIONS.md`), `core/BUILD.bazel`, this
  document (marker line, replacement selection, one compare line per hook
  that accepts compare, the table regenerated by `hooks_table.py render
  --write`, a topic section), the in-vitro README and the subsystem document.

Not edited per group: `invitro/main.c`, `invitro/BUILD.bazel`, the
`hook_adapters` list in `proxy/BUILD.bazel`, `proxy/rng_hooks.h`,
`invitro/coverage.h`, `invitro/check_shards.py`, the graph counters of
`proxy/callee_counters.h` (the legacy groups' list is generated by
`scripts/legacy_graph_counters.py`), the leaf list of
`proxy/tests/path_counters_fixture.c` and `LIVE_SITE_COUNT` in
`proxy/tests/live_coverage_integration_fixture.c`. The include lists are
generated at build time by `proxy/hook_groups.py` (rule in
`proxy/hook_groups.bzl`) in registry group order; details in the
[in-vitro README](../../tools/reference-game/invitro/README.md).
Still shared: a group that needs an own-assembly entry shim chains it from
the `default:` of the last `*_capture_entry` of the chain (for example
`weapon_slots_capture_entry` in `weapon_slots_hooks.h`, reached through
`sfx_capture_entry`, `panel_weapons_capture_entry`, `bullet_ctor_capture_entry`,
`transform_helpers_capture_entry`, `commands_capture_entry` and
`vfs_capture_entry`).

## Live validation

One replaced-mode regression with every registered hook over all 40 suites on
the five clones after each merge batch ([oracle_regression.md](oracle_regression.md),
`tools\check.ps1 -Full -Live`); `compare` runs only to diagnose a difference.
The current result is the one stated at the top of this document
(`<es2-data>\logs\check-r15-1`: 789 hooks and the 53 CW3220 imports of
the shim `replaced`, own `cw3220.dll` on the clones, all suites identical,
skipped/bypassed/mismatches 0). Hook counters at the last compared tick, the
last record and process exit are in `hook-counters.json` of the run directory.
Between identical runs only the input-side counters that follow the Windows
message pump vary (`key_message_calls`, one `control_record` pump iteration in
`c1_01-success`); simulation counters are equal. Run directories are
`<es2-data>\logs\check-*` and `oracle-parallel-*`.
