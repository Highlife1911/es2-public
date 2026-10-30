/* The blit and clear rest (sim/src/gfx.c; rasterizer.md, "Blit, clear and cursor rows"): the ramp copy, the cursor point, the kind-0 scroll and save bodies, the rect area and the two rotation thunks. 0x4866f0 is no function (fill.c's span_run covers it). */
ES2_SIM_SERVICE(gfx_ramps_set, 0x00486e10u, CDECL, EAX, 4, es2_sim_service_gfx_ramps_set)
ES2_SIM_SERVICE(gfx_cursor_point_set, 0x00486e7cu, CDECL, EAX, 1, es2_sim_service_gfx_cursor_point_set)
ES2_SIM_SERVICE(gfx_scroll_rect, 0x00487bdcu, CDECL, EAX, 2, es2_sim_service_gfx_scroll_rect)
ES2_SIM_SERVICE(gfx_rect_area, 0x00487e94u, CDECL, EAX, 1, es2_sim_service_gfx_rect_area)
ES2_SIM_SERVICE(gfx_save_rect, 0x00487eacu, CDECL, EAX, 2, es2_sim_service_gfx_save_rect)
ES2_SIM_SERVICE(gfx_rotation_matrix, 0x00488dc7u, CDECL, EAX, 2, es2_sim_service_gfx_rotation_matrix)
ES2_SIM_SERVICE(gfx_rotate_point, 0x00488dddu, CDECL, EAX, 3, es2_sim_service_gfx_rotate_point)
