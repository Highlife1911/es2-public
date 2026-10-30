/* The DirectDraw full-screen switch 0x4667c8 with 0x46658c, 0x4665c4 and
 * 0x45f13c inline (presentation.md, "The DirectDraw full-screen switch";
 * src/present.c). The platform's answer to the mode change is SDL's desktop
 * full screen (es2/sim/present.h, display_mode). */
ES2_SIM_SERVICE(fullscreen_switch, 0x004667c8u, CDECL, EAX, 0, es2_sim_service_fullscreen_switch)
