/* The MFD status draw 0x43a5a0, the MFD target draw 0x43fe44, the HDD status
 * body 0x450c7c, the radio record draw/frame/line/talk and the MFD entry steps
 * and order key (sim/src/mfd.c; hud.md, "the MFD status and target draws, the
 * HDD status body, the radio records and the entry steps"). The draw rows leave
 * EAX alone (not part of the contract). */
ES2_SIM_SERVICE(mfd_entry_up, 0x0044703cu, CDECL, EAX, 1, es2_sim_service_mfd_entry_up)
ES2_SIM_SERVICE(mfd_entry_down, 0x00447070u, CDECL, EAX, 1, es2_sim_service_mfd_entry_down)
ES2_SIM_SERVICE(mfd_order_key, 0x00447158u, CDECL, EAX, 3, es2_sim_service_mfd_order_key)
ES2_SIM_SERVICE(radio_draw, 0x0044aea0u, CDECL, EAX, 2, es2_sim_service_radio_draw)
ES2_SIM_SERVICE(radio_frame, 0x0044b3dcu, CDECL, EAX, 2, es2_sim_service_radio_frame)
ES2_SIM_SERVICE(radio_line, 0x0044b148u, CDECL, EAX, 3, es2_sim_service_radio_line)
ES2_SIM_SERVICE(radio_talk, 0x0044b344u, CDECL, EAX, 2, es2_sim_service_radio_talk)
ES2_SIM_SERVICE(mfd_status_draw, 0x0043a5a0u, CDECL, EAX, 1, es2_sim_service_mfd_status_draw)
ES2_SIM_SERVICE(mfd_target_draw, 0x0043fe44u, CDECL, EAX, 1, es2_sim_service_mfd_target_draw)
ES2_SIM_SERVICE(hdd_status_body, 0x00450c7cu, CDECL, EAX, 2, es2_sim_service_hdd_status_body)
