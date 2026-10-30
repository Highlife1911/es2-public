/* The load root's last guest callees (sim/src/load.c). 0x433f7c is also
 * called by the display transition 0x42a9c0 (display.c's area). */
ES2_SIM_SERVICE(cockpit_step, 0x00433f7cu, CDECL, EAX, 0, es2_sim_service_cockpit_step)
ES2_SIM_SERVICE(cursor_sprite_set, 0x00465618u, CDECL, EAX, 1, es2_sim_service_cursor_sprite_set)
ES2_SIM_SERVICE(mono_out, 0x004955c4u, CDECL, EAX, 0, es2_sim_service_mono_out)
