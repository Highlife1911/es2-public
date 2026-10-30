/* The PRES roots on the GDI path (presentation.md,
 * "the help call and the DirectDraw release";
 * src/present.c): the help call 0x4669c4 and the DirectDraw release 0x466730.
 * 0x4669b4 is hud_mfd_leaves.h's row display_restore; 0x4667c8 is fullscreen.h's
 * row fullscreen_switch. */
ES2_SIM_SERVICE(help_contents, 0x004669c4u, CDECL, EAX, 1, es2_sim_service_help_4669c4)
ES2_SIM_SERVICE(dd_release, 0x00466730u, CDECL, EAX, 0, es2_sim_service_dd_release_466730)
