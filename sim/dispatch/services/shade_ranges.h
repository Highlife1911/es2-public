/* The view-space projection 0x48cce4 and the shade ranges 0x42e8e8 with its callees that gouraud.c's flat fill calls (src/shape_draw.c; rasterizer.md, "View-space projection and shade ranges"). 0x486e10 is inlined. */
ES2_SIM_SERVICE(shape_project_view, 0x0048cce4u, CDECL, EAX, 0, es2_sim_service_shape_project_view)
ES2_SIM_SERVICE(shade_ranges, 0x0042e8e8u, CDECL, EAX, 3, es2_sim_service_shade_ranges)
ES2_SIM_SERVICE(shade_ranges_level, 0x0042e878u, CDECL, EAX, 2, es2_sim_service_shade_ranges_level)
ES2_SIM_SERVICE(shade_ranges_default, 0x0042ea0cu, CDECL, EAX, 0, es2_sim_service_shade_ranges_default)
ES2_SIM_SERVICE(shade_ranges_runs, 0x00430518u, CDECL, EAX, 3, es2_sim_service_shade_ranges_runs)
ES2_SIM_SERVICE(shade_ranges_spans, 0x00430590u, CDECL, EAX, 3, es2_sim_service_shade_ranges_spans)
