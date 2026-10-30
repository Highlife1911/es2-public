/* The HUD gadgets (sim/src/gadgets.c; hud.md, "the gunsights and gauges as own code"): the cursor shape 0x433d90, the mouse position 0x486ece, the two empty cursor-set functions 0x486e70/0x48992e, the nav marker point 0x43495c, the reverse gauge's step 0x43b67c and redraw 0x43b7cc. The gunsights 0x43b808/0x43b978/0x43c204, the nav gauge 0x43c40c and the fixed rotation gauge 0x43b4cc keep their rows in hud_load.h (hud_draw.c delegates to gadgets.c). */
ES2_SIM_SERVICE(cursor_shape, 0x00433d90u, CDECL, EAX, 1, es2_sim_service_cursor_shape)
ES2_SIM_SERVICE(mouse_position, 0x00486eceu, CDECL, EAX, 1, es2_sim_service_mouse_position)
ES2_SIM_SERVICE(cursor_set_486e70, 0x00486e70u, CDECL, EAX, 2, es2_sim_service_bare_return)
ES2_SIM_SERVICE(cursor_set_48992e, 0x0048992eu, CDECL, EAX, 2, es2_sim_service_bare_return)
ES2_SIM_SERVICE(nav_marker_point, 0x0043495cu, CDECL, EAX, 0, es2_sim_service_nav_marker_point)
ES2_SIM_SERVICE(rotation_step, 0x0043b67cu, CDECL, EAX, 2, es2_sim_service_rotation_step)
ES2_SIM_SERVICE(gadget_redraw, 0x0043b7ccu, CDECL, EAX, 1, es2_sim_service_gadget_redraw)
