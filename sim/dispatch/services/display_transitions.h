/* The display transitions, their small callees and the shield palette (src/display.c; display_modes.md, "the display
 * transitions and the shield palette as own code"). 0x443918 (the shield palette) is row shield_palette of hud_load.h
 * (es2_sim_service_shield_palette). 0x45c638 (the retrace wait) is a documented
 * platform deviation (no port I/O). */
ES2_SIM_SERVICE(display_leave, 0x0042a4c4u, CDECL, EAX, 1, es2_sim_service_display_leave)
ES2_SIM_SERVICE(display_enter, 0x0042a9c0u, CDECL, EAX, 1, es2_sim_service_display_enter)
ES2_SIM_SERVICE(display_border_off, 0x0042d9e8u, CDECL, EAX, 1, es2_sim_service_display_border_off)
ES2_SIM_SERVICE(cockpit_buttons_down, 0x00434604u, CDECL, EAX, 1, es2_sim_service_buttons_down)
ES2_SIM_SERVICE(cockpit_buttons_up, 0x00434634u, CDECL, EAX, 1, es2_sim_service_buttons_up)
ES2_SIM_SERVICE(cockpit_button_down, 0x00434928u, CDECL, EAX, 1, es2_sim_service_button_down)
ES2_SIM_SERVICE(cockpit_button_up, 0x00434944u, CDECL, EAX, 1, es2_sim_service_button_up)
ES2_SIM_SERVICE(retrace_wait, 0x0045c638u, CDECL, EAX, 0, es2_sim_service_retrace_wait)
