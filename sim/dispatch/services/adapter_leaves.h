/* Leaves reached only from hook adapters (src/adapter_leaves.c; own_process.md, "Called gap roots"): the camera mode 0x401148 (camera(C,3) of the playback keys 0x45fe6c), the flyer slot address 0x423044 (formation fm_flyer_slot) and the sweep word 0x4aaad0's clear 0x427d98 and read 0x427da0 (weapons sweep_leaf), which the adapters compute inline or only count, and the breakpoint 0x467ad8 (Ctrl+B of 0x45fe6c with -B), which the adapter calls. */
ES2_SIM_SERVICE(camera_mode, 0x00401148u, CDECL, EAX, 2, es2_sim_service_camera_mode)
ES2_SIM_SERVICE(flyer_slot, 0x00423044u, CDECL, EAX, 2, es2_sim_service_flyer_slot)
ES2_SIM_SERVICE(sweep_clear, 0x00427d98u, CDECL, EAX, 0, es2_sim_service_sweep_clear)
ES2_SIM_SERVICE(sweep_read, 0x00427da0u, CDECL, EAX, 0, es2_sim_service_sweep_read)
ES2_SIM_SERVICE(debug_break, 0x00467ad8u, CDECL, EAX, 0, es2_sim_service_debug_break)
