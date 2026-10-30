/* The gap roots of SIM, R3D and HUD that own code calls (src/gfx.c; own_process.md, "SIM, R3D and HUD gap roots as rows and verdicts" has the verdicts of the others): the stream helpers 0x492447/0x49085d, the next MFD page 0x446e3c (a bridge of input_dispatch case 14) and the shield arrow button's draw and frame slots 0x444b84/0x444b98 (table 0x49c9dd). */
ES2_SIM_SERVICE(stream_put_word, 0x00492447u, CDECL, EAX, 2, es2_sim_service_gfx_stream_put_word)
ES2_SIM_SERVICE(stream_put_byte, 0x0049085du, CDECL, EAX, 2, es2_sim_service_gfx_stream_put_byte)
ES2_SIM_SERVICE(mfd_next, 0x00446e3cu, CDECL, EAX, 1, es2_sim_service_gfx_mfd_next)
ES2_SIM_SERVICE(shield_button_draw, 0x00444b84u, CDECL, EAX, 1, es2_sim_service_gfx_shield_button_draw)
ES2_SIM_SERVICE(shield_button_frame, 0x00444b98u, CDECL, EAX, 1, es2_sim_service_gfx_shield_button_frame)
