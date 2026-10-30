/* The input rest (src/input.c, input.md, "the input rest"): the joystick button
 * map for the control mapping page that is still original, and the one-byte
 * prefs store of the frame end (loop.c). */
ES2_SIM_SERVICE(joystick_map, 0x0045a274u, CDECL, EAX, 1, es2_sim_service_joystick_map)
ES2_SIM_SERVICE(prefs_write, 0x00459b80u, CDECL, EAX, 1, es2_sim_service_prefs_write)
