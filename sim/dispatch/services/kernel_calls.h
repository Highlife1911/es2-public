/* The KERNEL32 calls of the c0w32 entry and the init/exit records, headless (src/services_platform.c). */
ES2_SIM_SERVICE(create_file, 0x004968feu, STDCALL, EAX, 7, es2_sim_service_create_file)
ES2_SIM_SERVICE(close_handle, 0x004968b0u, STDCALL, EAX, 1, es2_sim_service_close_handle)
ES2_SIM_SERVICE(module_handle, 0x00496898u, STDCALL, EAX, 1, es2_sim_service_module_handle)
