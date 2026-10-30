/* The BMP_8 driver's cursor point and slot 33, the textured polygon (sim/src/gfx.c; rasterizer.md, "Cursor point and textured slot 33"). 0x48a924's edge step 0x48aa50 and span 0x48abe4 are inlined; its edge set-up 0x48aaac (the registry hook tex_edge) runs as a core function. */
ES2_SIM_SERVICE(gfx_cursor_point, 0x0048993au, CDECL, EAX, 1, es2_sim_service_gfx_cursor_point)
ES2_SIM_SERVICE(gfx_slot_polygon, 0x0048a924u, CDECL, EAX, 2, es2_sim_service_gfx_slot_polygon)
