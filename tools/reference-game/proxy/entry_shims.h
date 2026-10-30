#ifndef ES2_ADAPTER_ENTRY_SHIMS_H
#define ES2_ADAPTER_ENTRY_SHIMS_H
/* The adapters' entry shims (entry_shim.h), one ES2_SHIM object each in its
 * group's header; included after every adapter header (rng_hooks.h).
 * adapter_shim finds a hook's shim by RVA: the proxy's capture redirects the
 * hook's jump to it (entry_shims_x86.h), es2sim's dispatch calls its entry
 * (sim/src/adapters.c). */
#define ES2_ENTRY_SHIMS(X) \
    X(weapons_building_shim) X(weapons_variant_shim) X(weapons_alternate_shim) X(weapons_impact_shim) \
    X(weapons_bullet_shim) X(weapons_projectile_shim) X(weapons_prepare_shim) X(weapons_fire_shim) \
    X(weapons_emit_shim) X(weapons_face_shim) \
    X(mission_fire_shim) X(objectives_eval_shim) X(vehicle_leaf_ratio_shim) X(flyer_shim) \
    X(flyer_control_input_shim) X(flyer_control_command_shim) X(flyer_control_bank_shim) X(flyer_control_steer_shim) \
    X(route_status_shim) X(projectile_vector_shim) X(projectile_aim_shim) X(torso_keys_shim) \
    X(weapon_system_select_shim) X(weapon_system_toggle_shim) X(vehicle_aux_keys_shim) \
    X(ai_behaviour_range_shim) X(ai_behaviour_aim_fire_shim) X(ai_behaviour_weapon_choose_shim) X(input_dispatch_torso_aim_shim) \
    X(input_dispatch_player_input_shim) X(ai_behaviour2_aim_select_shim) X(ai_behaviour2_retreat_fire_shim) X(control_record_shim) \
    X(actor_step_actor_step_shim) X(actor_step_mech_move_shim) X(actor_step_mech_act_shim) X(actor_step_mech_decide_shim) \
    X(actor_step_flyer_move_call_shim) X(actor_step_flyer_act_shim) X(actor_step_flyer_decide_shim) X(mech_combat_attack_step_shim) \
    X(mech_combat_base_attack_shim) X(mech_combat_flyer_attack_shim) X(mech_combat_face_off_shim) X(mech_combat_flank_attack_shim) \
    X(mech_combat_drive_off_shim) X(flyer_states_attack_shim) X(flyer_states_follow_shim) X(flyer_states_formation_shim) \
    X(flyer_states_search_shim) X(flyer_states_patrol_shim) X(flyer_states_scout_shim) \
    X(mech_travel_travel_shim) X(mech_travel_follow_shim) X(lc_base_update_shim) X(lc_base_projectile_shim) \
    X(mouse_events_shim) X(flyer_part_shim) X(sfx_play_shim) X(sfx_play_at_shim) \
    X(sfx_place_shim) X(sfx_refresh_shim) X(sfx_stop_shim) X(sfx_rate_shim) \
    X(sfx_voice_shim) X(sfx_lip_shim) X(sfx_lip_due_shim) X(panel_weapons_select_shim) \
    X(panel_weapons_find_shim) X(panel_weapons_panel_shim) X(bullet_ctor_create_shim) X(transform_helpers_rotate_shim) \
    X(transform_helpers_point_shim) X(commands_playback_shim) X(commands_command_key_shim) X(hud_keys_hud_key_shim) \
    X(weapon_slots_frame_shim) X(weapon_slots_ready_shim) X(weapon_slots_selected_shim) X(weapon_slots_chain_shim) \
    X(weapon_slots_link_shim) X(weapon_slots_member_shim) X(weapon_slots_sync_shim) X(weapon_slots_state_shim) \
    X(weapon_slots_glink_shim) X(weapon_slots_button_shim) X(panel_frame_shim) X(logic_frame_shim) \
    X(flyer_ai_route_shim) X(cockpit_sync_shim) X(rotation_helpers_compose_shim) X(rotation_helpers_place_shim) \
    X(render_root_shim) \
    X(bridged_leaves_r_key_shim) X(mfd_orders_mfd_button_shim) X(mfd_orders_mfd_click_shim) X(mfd_orders_mfd_order_shim) \
    X(mfd_orders_order_call_shim) X(mfd_orders_mfd_target_shim) X(mfd_orders_group_order_shim) X(mfd_orders_mfd_range_shim) \
    X(mfd_orders_menu_code_shim) X(mfd_orders_menu_toggle_shim) X(mfd_orders_mfd_entry_shim) X(mfd_orders_mfd_view_next_shim) \
    X(slot_leaves_radial_hit_shim) X(weapon_syncs_laew_fire_shim) X(mission_root_shim) X(hdd_map_shim) \
    X(mfd_keys_mfd_key_shim) X(mfd_keys_mfd_alt_key_shim) X(mfd_keys_mfd_key_select_shim) X(mfd_keys_mfd_entry_next_shim) \
    X(mfd_keys_mfd_entry_prev_shim) X(mfd_keys_menu_select_shim) X(hdd_exec_shim)

static es2_shim *const adapter_shims[] = {
#define ES2_ENTRY_SHIM_ROW(shim) &shim,
    ES2_ENTRY_SHIMS(ES2_ENTRY_SHIM_ROW)
#undef ES2_ENTRY_SHIM_ROW
};
#define ADAPTER_SHIM_COUNT (sizeof(adapter_shims) / sizeof(adapter_shims[0]))

static inline es2_shim *adapter_shim(uint32_t rva) {
    for (unsigned i = 0; i < ADAPTER_SHIM_COUNT; ++i)
        if (adapter_shims[i]->rva == rva) return adapter_shims[i];
    return NULL;
}
#endif
