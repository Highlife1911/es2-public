/* The perspective fill and the shade lookup (src/poly.c; rasterizer.md, "Software polygon fills"). */
ES2_SIM_SERVICE(poly_fill_persp, 0x00468760u, CDECL, EAX, 8, es2_sim_service_poly_fill_persp)
ES2_SIM_SERVICE(poly_shade, 0x00430e34u, CDECL, PAIR, 2, es2_sim_service_poly_shade)
