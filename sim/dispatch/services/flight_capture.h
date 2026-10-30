/* The flight-key capture switch and the cockpit done step that the slides of 0x42a9c0 call (sim/src/display.c), and the intro set-up 0x45d548 of the mission load (sim/src/cockpit.c); display_modes.md, "the capture switch, the done step, the surface frame and the intro set-up". */
ES2_SIM_SERVICE(flight_capture, 0x0045b68cu, CDECL, EAX, 1, es2_sim_service_flight_capture)
ES2_SIM_SERVICE(cockpit_done, 0x00433ee8u, CDECL, EAX, 0, es2_sim_service_cockpit_done)
ES2_SIM_SERVICE(intro_setup, 0x0045d548u, CDECL, EAX, 0, es2_sim_service_intro_setup)
