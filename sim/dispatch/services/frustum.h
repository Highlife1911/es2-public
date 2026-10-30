/* The terrain draw's view frustum library of the own dbsim.exe as own code
 * (terrain.md, "the frustum library as own code", src/frustum.c): the frustum
 * constructor, placement and polygon clip, the plane from a normal, the polygon
 * split at a plane and its side test, the polygon offsets, the plane element
 * constructor, the dword swap, the vertical-plane test and the plane shift. */
ES2_SIM_SERVICE(frustum_new, 0x004949a4u, CDECL, EAX, 3, es2_sim_service_frustum_new)
ES2_SIM_SERVICE(frustum_place, 0x004949e4u, CDECL, EAX, 4, es2_sim_service_frustum_place)
ES2_SIM_SERVICE(frustum_clip, 0x00494e58u, CDECL, EAX, 5, es2_sim_service_frustum_clip)
ES2_SIM_SERVICE(poly_offset_sub, 0x004950a4u, CDECL, EAX, 3, es2_sim_service_poly_offset_sub)
ES2_SIM_SERVICE(poly_offset_add, 0x004950ecu, CDECL, EAX, 3, es2_sim_service_poly_offset_add)
ES2_SIM_SERVICE(plane_ctor, 0x0049520eu, CDECL, EAX, 1, es2_sim_service_plane_ctor)
ES2_SIM_SERVICE(swap_dwords, 0x00495221u, CDECL, EAX, 2, es2_sim_service_swap_dwords)
ES2_SIM_SERVICE(plane_vertical, 0x00495236u, CDECL, EAX, 1, es2_sim_service_plane_vertical)
ES2_SIM_SERVICE(plane_shift, 0x00495254u, CDECL, EAX, 2, es2_sim_service_plane_shift)
ES2_SIM_SERVICE(plane_from_normal, 0x0047e450u, CDECL, EAX, 3, es2_sim_service_plane_from_normal)
ES2_SIM_SERVICE(poly_plane_clip, 0x0047e73cu, CDECL, EAX, 4, es2_sim_service_poly_plane_clip)
ES2_SIM_SERVICE(plane_side, 0x0047eaf8u, CDECL, EAX, 2, es2_sim_service_plane_side)
