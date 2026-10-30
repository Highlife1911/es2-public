/* The HDD button executor 0x44a1a0 with its callees (the map scroll down/up 0x44d0c0/0x44d124, zoom in 0x44cf90, the status mode next/previous 0x450bf4/0x450c18, the target previous/next 0x44ba08/0x44b9b0, the order commit/copy 0x44db9c/0x44db4c, the order label 0x44dc6c and text 0x44dd14, the panel cursor 0x433e3c) and the leftovers: the radio selection 0x44a748 (the DialogPort message), the map point conversion 0x44d24c and the drag start 0x44d3cc with their exit EDX/ECX (sim/src/mfd.c; hud.md, "the HDD button executor"). */
ES2_SIM_SERVICE(hdd_execute, 0x0044a1a0u, CDECL, EAX, 1, es2_sim_service_hdd_execute)
ES2_SIM_SERVICE(hdd_select, 0x0044a748u, CDECL, EAX, 2, es2_sim_service_hdd_select)
ES2_SIM_SERVICE(map_scroll_down, 0x0044d0c0u, CDECL, EAX, 1, es2_sim_service_map_scroll_down)
ES2_SIM_SERVICE(map_scroll_up, 0x0044d124u, CDECL, EAX, 1, es2_sim_service_map_scroll_up)
ES2_SIM_SERVICE(map_zoom_in, 0x0044cf90u, CDECL, EAX, 1, es2_sim_service_map_zoom_in)
ES2_SIM_SERVICE(status_next, 0x00450bf4u, CDECL, EAX, 1, es2_sim_service_status_next)
ES2_SIM_SERVICE(status_prev, 0x00450c18u, CDECL, EAX, 1, es2_sim_service_status_prev)
ES2_SIM_SERVICE(target_prev, 0x0044ba08u, CDECL, EAX, 1, es2_sim_service_target_prev)
ES2_SIM_SERVICE(target_next, 0x0044b9b0u, CDECL, EAX, 1, es2_sim_service_target_next)
ES2_SIM_SERVICE(order_commit, 0x0044db9cu, CDECL, EAX, 1, es2_sim_service_order_commit)
ES2_SIM_SERVICE(order_copy, 0x0044db4cu, CDECL, EAX, 2, es2_sim_service_order_copy)
ES2_SIM_SERVICE(order_text, 0x0044dd14u, CDECL, EAX, 1, es2_sim_service_order_text)
ES2_SIM_SERVICE(order_label, 0x0044dc6cu, CDECL, EAX, 3, es2_sim_service_order_label)
ES2_SIM_SERVICE(cursor_select, 0x00433e3cu, CDECL, EAX, 1, es2_sim_service_cursor_select)
ES2_SIM_SERVICE(map_point_screen, 0x0044d24cu, CDECL, EAX, 3, es2_sim_service_map_point_screen)
ES2_SIM_SERVICE(map_drag_start, 0x0044d3ccu, CDECL, EAX, 3, es2_sim_service_map_drag_start)
/* The HDD radio panel 0x44b620 (sim/src/mfd.c; hud.md, "the HDD button executor"). */
ES2_SIM_SERVICE(hdd_radio, 0x0044b620u, CDECL, EAX, 1, es2_sim_service_hdd_radio)
/* The MFD camera page frame 0x44060c (sim/src/mfd.c; hud.md, "the HDD button executor"). */
ES2_SIM_SERVICE(mfd_camera_frame, 0x0044060cu, CDECL, EAX, 1, es2_sim_service_mfd_camera_frame)
/* The MFD key handler 0x4469e8 (sim/src/mfd.c; hud.md, "the HDD button executor"). */
ES2_SIM_SERVICE(mfd_keys, 0x004469e8u, CDECL, EAX, 2, es2_sim_service_mfd_keys)
