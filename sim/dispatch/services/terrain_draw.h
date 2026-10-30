/* The terrain renderer (src/terrain_draw.c; terrain.md, "the terrain renderer
 * as own code"). The frame's terrain set-up, the walk with its row and column
 * walkers and their helpers, the per-line callbacks, the cell draw with the
 * texture selection, the height cache lookup, the cell object event, the mark
 * read and the facing test. 0x46be9c lies in the shape draw's slot range but is
 * the cell draw's own callee. */
ES2_SIM_SERVICE(terrain_draw, 0x0046cb9cu, CDECL, EAX, 3, es2_sim_service_terrain_draw)
ES2_SIM_SERVICE(terrain_walk, 0x0046d1a8u, CDECL, EAX, 2, es2_sim_service_terrain_walk)
ES2_SIM_SERVICE(terrain_walk_setup, 0x00471f30u, CDECL, EAX, 3, es2_sim_service_terrain_walk_setup)
ES2_SIM_SERVICE(terrain_row_walk, 0x00471f44u, CDECL, EAX, 3, es2_sim_service_terrain_row_walk)
ES2_SIM_SERVICE(terrain_column_walk, 0x00472504u, CDECL, EAX, 3, es2_sim_service_terrain_column_walk)
ES2_SIM_SERVICE(terrain_turn, 0x004724b8u, CDECL, EAX, 3, es2_sim_service_terrain_turn)
ES2_SIM_SERVICE(terrain_turn_back, 0x004724e0u, CDECL, EAX, 3, es2_sim_service_terrain_turn_back)
ES2_SIM_SERVICE(terrain_row_cells, 0x0046dddcu, CDECL, EAX, 4, es2_sim_service_terrain_row_cells)
ES2_SIM_SERVICE(terrain_column_cells, 0x0046dfa8u, CDECL, EAX, 4, es2_sim_service_terrain_column_cells)
ES2_SIM_SERVICE(terrain_cell_draw, 0x0046d448u, CDECL, EAX, 4, es2_sim_service_terrain_cell_draw)
ES2_SIM_SERVICE(terrain_cell_event, 0x0046e5a4u, CDECL, EAX, 3, es2_sim_service_terrain_cell_event)
ES2_SIM_SERVICE(height_cache_get, 0x0049534cu, CDECL, EAX, 2, es2_sim_service_height_cache_get)
ES2_SIM_SERVICE(terrain_cell_mark, 0x00470d94u, CDECL, EAX, 2, es2_sim_service_terrain_cell_mark)
ES2_SIM_SERVICE(terrain_facing, 0x0046be9cu, CDECL, EAX, 2, es2_sim_service_terrain_facing)
