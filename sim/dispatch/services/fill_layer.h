/* The 2D polygon fill layer and the line (src/fill.c; rasterizer.md, "GL fill layer and line"). */
ES2_SIM_SERVICE(fill_polygon, 0x00483eb8u, CDECL, EAX, 2, es2_sim_service_fill_polygon)
ES2_SIM_SERVICE(fill_polygon_reversed, 0x00484222u, CDECL, EAX, 2, es2_sim_service_fill_polygon_reversed)
ES2_SIM_SERVICE(fill_polygon_any, 0x004842bbu, CDECL, EAX, 2, es2_sim_service_fill_polygon_any)
ES2_SIM_SERVICE(fill_line, 0x00483a04u, CDECL, EAX, 2, es2_sim_service_fill_line)
ES2_SIM_SERVICE(fill_line_slot, 0x0048a3b0u, CDECL, EAX, 3, es2_sim_service_fill_line_slot)
