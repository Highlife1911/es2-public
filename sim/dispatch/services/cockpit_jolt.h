/* The cockpit jolt and bump (src/cockpit.c; display_modes.md, "the jolt and the bump as own code"). */
ES2_SIM_SERVICE(cockpit_jolt_hit, 0x00434010u, CDECL, EAX, 0, es2_sim_service_cockpit_jolt_hit)
ES2_SIM_SERVICE(cockpit_bump_hit, 0x00434144u, CDECL, EAX, 0, es2_sim_service_cockpit_bump_hit)
ES2_SIM_SERVICE(cockpit_jolt_start, 0x0042d2f8u, CDECL, EAX, 1, es2_sim_service_cockpit_jolt_start)
ES2_SIM_SERVICE(cockpit_jolt_step, 0x0042d4a8u, CDECL, EAX, 1, es2_sim_service_cockpit_jolt_step)
ES2_SIM_SERVICE(cockpit_bump_reset, 0x0042d820u, CDECL, EAX, 1, es2_sim_service_cockpit_bump_reset)
