/* The main loop's services (src/loop.c, es2/sim/loop.h) over the bound loop
 * instance: the abort-notify registration, CW3220's atexit, the clock, the
 * timing init and the limiter under virtual time, the pump with the journal as
 * its message source, the window procedure's handler list, and the empty
 * abort-notify function of the DIB section's surface. */
ES2_SIM_SERVICE(abort_notify_add, 0x00479cfcu, CDECL, EAX, 1, es2_sim_service_abort_notify)
ES2_SIM_SERVICE(rt_atexit, 0x0049675au, CDECL, EAX, 1, es2_sim_service_atexit)
ES2_SIM_SERVICE(clock, 0x00467828u, CDECL, EAX, 0, es2_sim_service_clock)
ES2_SIM_SERVICE(timing_init, 0x00467840u, CDECL, EAX, 0, es2_sim_service_timing_init)
ES2_SIM_SERVICE(limiter, 0x004678c0u, CDECL, EAX, 0, es2_sim_service_limiter)
ES2_SIM_SERVICE(pump, 0x00464c78u, CDECL, EAX, 0, es2_sim_service_pump)
ES2_SIM_SERVICE(handler_add, 0x00465fecu, CDECL, EAX, 1, es2_sim_service_handler_add)
ES2_SIM_SERVICE(handler_remove, 0x00466010u, CDECL, EAX, 1, es2_sim_service_handler_remove)
ES2_SIM_SERVICE(surface_notify, 0x00480f30u, CDECL, EAX, 0, es2_sim_service_nothing)
