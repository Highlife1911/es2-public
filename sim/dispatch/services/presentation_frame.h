/* The presentation frame and the palette functions (src/present.c, es2/sim/present.h; presentation.md, "the
 * presentation frame in the own process"). 0x464a14 is mission_load.h's row; it points at present.c's
 * es2_sim_service_present_capture. */
ES2_SIM_SERVICE(present_frame, 0x0045fba4u, CDECL, EAX, 0, es2_sim_service_present_frame)
ES2_SIM_SERVICE(palette_cycle, 0x004306acu, CDECL, EAX, 0, es2_sim_service_palette_cycle)
ES2_SIM_SERVICE(palette_upload, 0x00465cc4u, CDECL, EAX, 1, es2_sim_service_palette_upload)
ES2_SIM_SERVICE(palette_fade_setup, 0x004308fcu, CDECL, EAX, 3, es2_sim_service_fade_setup)
ES2_SIM_SERVICE(palette_fade_step, 0x00430b34u, CDECL, EAX, 0, es2_sim_service_fade_step)
