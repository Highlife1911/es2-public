/* The display object's per-frame readers and requests and the view shape (src/hud_cockpit.c, display_modes.md, "cockpit layout and panel readers, the display object's readers"). */
ES2_SIM_SERVICE(view_shape, 0x00429c2cu, CDECL, EAX, 2, es2_sim_service_view_shape)
ES2_SIM_SERVICE(display_request, 0x0042a3f4u, CDECL, EAX, 2, es2_sim_service_display_request)
ES2_SIM_SERVICE(display_panel, 0x00429828u, CDECL, EAX, 1, es2_sim_service_display_panel)
ES2_SIM_SERVICE(view_rect, 0x0042db10u, CDECL, EAX, 0, es2_sim_service_view_rect)
ES2_SIM_SERVICE(view_visible, 0x0042db18u, CDECL, EAX, 0, es2_sim_service_view_visible)
ES2_SIM_SERVICE(hud_input, 0x00432b14u, CDECL, EAX, 2, es2_sim_service_hud_input)
ES2_SIM_SERVICE(cursor_image, 0x00433c54u, CDECL, EAX, 1, es2_sim_service_cursor_image)
