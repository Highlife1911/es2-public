/* The rows the headless replay reached next (own_process.md, "the headless
 * acceptance"; src/loop.c, src/hud_radio.c, src/hud_gauges.c and src/mfd.c,
 * appended). */
ES2_SIM_SERVICE(dump_flush, 0x00401e5cu, CDECL, EAX, 0, es2_sim_service_dump_flush_401e5c)
ES2_SIM_SERVICE(callsign_digit, 0x00434260u, CDECL, EAX, 1, es2_sim_service_callsign_digit_434260)
ES2_SIM_SERVICE(display_restore, 0x004669b4u, CDECL, EAX, 0, es2_sim_service_display_restore_4669b4)
ES2_SIM_SERVICE(part_state, 0x00438624u, CDECL, EAX, 3, es2_sim_service_part_state)
ES2_SIM_SERVICE(projectile_gauge_event, 0x004414dcu, CDECL, EAX, 3, es2_sim_service_projectile_gauge_event)
ES2_SIM_SERVICE(pod_gauge_event, 0x00441a24u, CDECL, EAX, 2, es2_sim_service_pod_gauge_event)
ES2_SIM_SERVICE(shields_gauge_event, 0x00443834u, CDECL, EAX, 2, es2_sim_service_shields_gauge_event)
ES2_SIM_SERVICE(map_target, 0x0044f03cu, CDECL, EAX, 3, es2_sim_service_map_target)
ES2_SIM_SERVICE(map_inside, 0x0044f0c4u, CDECL, EAX, 2, es2_sim_service_map_inside)
ES2_SIM_SERVICE(range_set, 0x00447014u, CDECL, EAX, 2, es2_sim_service_range_set)
ES2_SIM_SERVICE(part_bar, 0x00437e94u, CDECL, EAX, 4, es2_sim_service_part_bar)
ES2_SIM_SERVICE(target_locked, 0x00440370u, CDECL, EAX, 1, es2_sim_service_target_locked)
ES2_SIM_SERVICE(status_tail, 0x0043b288u, CDECL, EAX, 4, es2_sim_service_status_tail)
ES2_SIM_SERVICE(hdd_parts, 0x00451de0u, CDECL, EAX, 3, es2_sim_service_hdd_parts)
ES2_SIM_SERVICE(blit_at_z, 0x0045c964u, CDECL, EAX, 4, es2_sim_service_blit_at_z)
ES2_SIM_SERVICE(part_bitmap, 0x00438230u, CDECL, EAX, 6, es2_sim_service_part_bitmap)
