/* The CommandPort receive/text/frame/show, the CD-root path, the phrase table and the voice select (sim/src/hud_widgets.c; hud.md, "the CommandPort, the CD-root path, the phrase table and the voice select"). */
ES2_SIM_SERVICE(hud_command_receive, 0x004362e4u, CDECL, EAX, 2, es2_sim_service_hud_command_receive)
ES2_SIM_SERVICE(hud_command_text, 0x00436318u, CDECL, EAX, 2, es2_sim_service_hud_command_text)
ES2_SIM_SERVICE(hud_command_frame, 0x004365d0u, CDECL, EAX, 1, es2_sim_service_hud_command_frame)
ES2_SIM_SERVICE(hud_command_show, 0x0043660cu, CDECL, EAX, 1, es2_sim_service_hud_command_show)
ES2_SIM_SERVICE(hud_cd_path, 0x0045ee60u, CDECL, EAX, 1, es2_sim_service_hud_cd_path)
ES2_SIM_SERVICE(hud_phrase_next, 0x00453a10u, CDECL, EAX, 2, es2_sim_service_hud_phrase_next)
ES2_SIM_SERVICE(hud_phrase_row, 0x00453a4cu, CDECL, EAX, 2, es2_sim_service_hud_phrase_row)
ES2_SIM_SERVICE(hud_voice_select, 0x00471de0u, CDECL, EAX, 2, es2_sim_service_hud_voice_select)
