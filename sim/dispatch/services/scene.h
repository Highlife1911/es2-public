/* Sky, horizon, object binning and the sort tree (src/scene.c; horizon.md, "the
 * sky and the horizon as own code", terrain.md, "binning, the cell callback and
 * the sort tree as own code"). The per-frame draw order under the render root:
 * the sky entry 0x42e700 with the horizon line, sky bands and ground fill, the
 * terrain cells pass, the jolt palette switches, the object binning 0x42841c
 * and its cell table, the cell callback 0x428c60, the distance cull, the sort
 * tree 0x429460..0x429620, the per-object draw 0x42876c, the scene finish
 * 0x42883c and the pick save/restore. */
ES2_SIM_SERVICE(scene_bin_cell_index, 0x004281b0u, CDECL, EAX, 2, es2_sim_service_scene_bin_cell_index)
ES2_SIM_SERVICE(scene_pick_object, 0x004282c0u, CDECL, EAX, 0, es2_sim_service_scene_pick_object)
ES2_SIM_SERVICE(scene_pick_idle, 0x004282c8u, CDECL, EAX, 0, es2_sim_service_scene_pick_idle)
ES2_SIM_SERVICE(scene_bin_object, 0x004282d8u, CDECL, EAX, 1, es2_sim_service_scene_bin_object)
ES2_SIM_SERVICE(scene_bin_store, 0x004282f8u, CDECL, EAX, 2, es2_sim_service_scene_bin_store)
ES2_SIM_SERVICE(scene_bin_at_position, 0x0042837cu, CDECL, EAX, 2, es2_sim_service_scene_bin_at_position)
ES2_SIM_SERVICE(scene_bin_at_cell, 0x004283b4u, CDECL, EAX, 2, es2_sim_service_scene_bin_at_cell)
ES2_SIM_SERVICE(scene_bin_objects, 0x0042841cu, CDECL, EAX, 0, es2_sim_service_scene_bin_objects)
ES2_SIM_SERVICE(scene_sort_obj_draw, 0x0042876cu, CDECL, EAX, 1, es2_sim_service_scene_sort_obj_draw)
ES2_SIM_SERVICE(scene_finish, 0x0042883cu, CDECL, EAX, 1, es2_sim_service_scene_finish)
ES2_SIM_SERVICE(scene_pick_restore, 0x00428ae0u, CDECL, EAX, 0, es2_sim_service_scene_pick_restore)
ES2_SIM_SERVICE(scene_pick_save, 0x00428b38u, CDECL, EAX, 0, es2_sim_service_scene_pick_save)
ES2_SIM_SERVICE(scene_draw_distances, 0x00428bc0u, CDECL, EAX, 1, es2_sim_service_scene_draw_distances)
ES2_SIM_SERVICE(scene_distance_cull, 0x00428c08u, CDECL, EAX, 2, es2_sim_service_scene_distance_cull)
ES2_SIM_SERVICE(scene_cell_objects_draw, 0x00428c60u, CDECL, EAX, 3, es2_sim_service_scene_cell_objects_draw)
ES2_SIM_SERVICE(scene_sort_camera, 0x00428eecu, CDECL, EAX, 1, es2_sim_service_scene_sort_camera)
ES2_SIM_SERVICE(scene_sort_compare, 0x00429460u, CDECL, EAX, 2, es2_sim_service_scene_sort_compare)
ES2_SIM_SERVICE(scene_sort_insert, 0x004294d4u, CDECL, EAX, 2, es2_sim_service_scene_sort_insert)
ES2_SIM_SERVICE(scene_sort_pass1, 0x00429524u, CDECL, EAX, 2, es2_sim_service_scene_sort_pass1)
ES2_SIM_SERVICE(scene_sort_pass2, 0x0042958cu, CDECL, EAX, 3, es2_sim_service_scene_sort_pass2)
ES2_SIM_SERVICE(scene_sort_walk, 0x004295f0u, CDECL, EAX, 1, es2_sim_service_scene_sort_walk)
ES2_SIM_SERVICE(scene_sort_draw, 0x00429620u, CDECL, EAX, 2, es2_sim_service_scene_sort_draw)
ES2_SIM_SERVICE(scene_sky_horizon, 0x0042e700u, CDECL, EAX, 1, es2_sim_service_scene_sky_horizon)
ES2_SIM_SERVICE(scene_terrain_cells_draw, 0x00428140u, CDECL, EAX, 1, es2_sim_service_scene_terrain_cells_draw)
ES2_SIM_SERVICE(scene_jolt_on, 0x0042ea44u, CDECL, EAX, 0, es2_sim_service_scene_jolt_on)
ES2_SIM_SERVICE(scene_jolt_flicker, 0x0042ea70u, CDECL, EAX, 0, es2_sim_service_scene_jolt_flicker)
ES2_SIM_SERVICE(scene_jolt_off, 0x0042ea94u, CDECL, EAX, 0, es2_sim_service_scene_jolt_off)
ES2_SIM_SERVICE(scene_horizon_forward, 0x0042ebe8u, CDECL, EAX, 3, es2_sim_service_scene_horizon_forward)
ES2_SIM_SERVICE(scene_horizon_paint, 0x0042ec08u, CDECL, EAX, 4, es2_sim_service_scene_horizon_paint)
ES2_SIM_SERVICE(scene_horizon_line, 0x0042ec68u, CDECL, EAX, 2, es2_sim_service_scene_horizon_line)
ES2_SIM_SERVICE(scene_sky_bands, 0x0042ee88u, CDECL, EAX, 1, es2_sim_service_scene_sky_bands)
ES2_SIM_SERVICE(scene_ground_fill, 0x0042f0b0u, CDECL, EAX, 1, es2_sim_service_scene_ground_fill)
ES2_SIM_SERVICE(scene_hzline_dtor, 0x0042f6d2u, CDECL, EAX, 2, es2_sim_service_scene_hzline_dtor)

ES2_SIM_SERVICE(gfx_slot_stretch, 0x0048afc8u, CDECL, EAX, 6, es2_sim_service_gfx_slot_stretch)
ES2_SIM_SERVICE(gfx_stretch, 0x00488aa8u, CDECL, EAX, 4, es2_sim_service_gfx_stretch)
ES2_SIM_SERVICE(gfx_stretch_on, 0x00481c3bu, CDECL, EAX, 5, es2_sim_service_gfx_stretch_on)
ES2_SIM_SERVICE(gfx_stretch_blit, 0x004817c8u, CDECL, EAX, 4, es2_sim_service_gfx_stretch_blit)
ES2_SIM_SERVICE(gfx_rotate, 0x00488b98u, CDECL, EAX, 5, es2_sim_service_gfx_rotate)
ES2_SIM_SERVICE(gfx_rotate_on, 0x00481c58u, CDECL, EAX, 6, es2_sim_service_gfx_rotate_on)
