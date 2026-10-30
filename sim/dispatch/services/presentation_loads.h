/* The presentation rest (presentation.md, "rows and verdicts"). The two load
 * wrappers are byte-for-byte the load-by-name 0x45cdf4 (only the exception record
 * differs), so they reuse es2_sim_service_load_by_name (src/load.c) as the
 * row 0x465f7c does. The other VAs of the row are inlined in own rows or dead (the
 * contract lists each). */
ES2_SIM_SERVICE(panel_load, 0x00430f58u, CDECL, EAX, 1, es2_sim_service_load_by_name)
ES2_SIM_SERVICE(font_load, 0x00430fb0u, CDECL, EAX, 1, es2_sim_service_load_by_name)
