/* The leaf slots of the volume (0x4a0e10), write (0x4a1760) and memory
 * (0x4a1788) stream classes, the write class's error leaf 0x47b294 and the
 * class lookup 0x47a4a0 (file_io.md, "the stream slots and the class lookup
 * as rows"; src/services_files.c, appended). Slots whose bodies match the
 * read-from-handle class (runtime_leaves.h) share its services. */
ES2_SIM_SERVICE(class_lookup, 0x0047a4a0u, CDECL, EAX, 1, es2_sim_service_class_lookup_47a4a0)
ES2_SIM_SERVICE(vol_map, 0x00473510u, CDECL, EAX, 2, es2_sim_service_vol_map_473510)
ES2_SIM_SERVICE(vol_true, 0x00473578u, CDECL, EAX, 1, es2_sim_service_read_true_47af58)
ES2_SIM_SERVICE(vol_fail, 0x004735a4u, CDECL, EAX, 1, es2_sim_service_read_fail_47af84)
ES2_SIM_SERVICE(write_map, 0x0047b0d8u, CDECL, EAX, 2, es2_sim_service_write_map_47b0d8)
ES2_SIM_SERVICE(write_tell, 0x0047b1e4u, CDECL, EAX, 1, es2_sim_service_read_tell_47aeb0)
ES2_SIM_SERVICE(write_seek, 0x0047b200u, CDECL, EAX, 2, es2_sim_service_read_seek_47aecc)
ES2_SIM_SERVICE(write_true, 0x0047b28cu, CDECL, EAX, 1, es2_sim_service_read_true_47af58)
ES2_SIM_SERVICE(write_error, 0x0047b294u, CDECL, EAX, 1, es2_sim_service_read_error_47af60)
ES2_SIM_SERVICE(write_fail, 0x0047b2f8u, CDECL, EAX, 1, es2_sim_service_read_fail_47af84)
ES2_SIM_SERVICE(memory_map, 0x0047b3d4u, CDECL, EAX, 2, es2_sim_service_memory_map_47b3d4)
