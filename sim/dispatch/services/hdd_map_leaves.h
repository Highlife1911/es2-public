/* The HDD map's button and icon leaves (src/mfd.c, hud.md, "the MFD pages, the
 * HDD map and status page, and the map's button and icon leaves"): the leaves
 * that the hdd_orders and hdd_map hooks bridge, and the method table targets of
 * the map icon (0x49d9b0) and the drag widget (0x49d944). The page constructors
 * of the row are own code inside mfd_ctor and hdd_ctor and have no row. */
ES2_SIM_SERVICE(map_lamp, 0x0044ede0u, CDECL, EAX, 2, es2_sim_service_map_lamp)
ES2_SIM_SERVICE(map_buttons_on, 0x0044ee00u, CDECL, EAX, 1, es2_sim_service_map_buttons_on)
ES2_SIM_SERVICE(map_buttons_off, 0x0044ee24u, CDECL, EAX, 1, es2_sim_service_map_buttons_off)
ES2_SIM_SERVICE(map_tick, 0x0044d370u, CDECL, EAX, 0, es2_sim_service_map_tick)
ES2_SIM_SERVICE(icon_mark, 0x0044f5d0u, CDECL, EAX, 1, es2_sim_service_icon_mark)
ES2_SIM_SERVICE(icon_frame, 0x0044f5f4u, CDECL, EAX, 1, es2_sim_service_icon_frame)
ES2_SIM_SERVICE(icon_release, 0x0044f630u, CDECL, EAX, 2, es2_sim_service_icon_release)
ES2_SIM_SERVICE(map_blink, 0x0044f644u, CDECL, EAX, 1, es2_sim_service_map_blink)
ES2_SIM_SERVICE(drag_press, 0x0044f6d4u, CDECL, EAX, 2, es2_sim_service_drag_press)
