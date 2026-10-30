/* The Gouraud fill, its triangle and the terrain's flat fill (src/gouraud.c; rasterizer.md, "Gouraud and flat fill"). */
ES2_SIM_SERVICE(fill_gouraud, 0x00487accu, CDECL, EAX, 2, es2_sim_service_fill_gouraud)
ES2_SIM_SERVICE(fill_gouraud_triangle, 0x00486ee8u, CDECL, EAX, 2, es2_sim_service_fill_gouraud_triangle)
ES2_SIM_SERVICE(flat_fill, 0x0046bc44u, CDECL, EAX, 3, es2_sim_service_flat_fill)
