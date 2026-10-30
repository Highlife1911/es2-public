/* The joystick vtable 0x49ec74's two slots: the destructor 0x45c2e0 with
 * 0x477630/0x477714 inline and the poll 0x45c330 with 0x47785c/0x4778a8
 * inline (input.md, "the joystick virtuals as rows"; src/input.c). */
ES2_SIM_SERVICE(joystick_dtor, 0x0045c2e0u, CDECL, EAX, 2, es2_sim_service_joystick_dtor_45c2e0)
ES2_SIM_SERVICE(joystick_poll, 0x0045c330u, CDECL, EAX, 1, es2_sim_service_joystick_poll_45c330)
