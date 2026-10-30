/* The input layer (src/input.c, es2/sim/input.h) over the bound input instance:
 * the input set-up the load root calls, the device poll and its readers that
 * control_record and the joystick users call, the mouse window handler, its
 * event and queue callback, the mouse queue set-up, the mouse bounds and the
 * key flush the load root calls later, the empty hot-key callback, the prefs
 * setter and the five prefs callbacks of the set-up with the two sound flags
 * they reach (audio's), the two empty constructors and the
 * runtime's array constructor, the surface attribute getter of the graphics
 * library, the platform imports of the provider seam
 * (headless: the input journal), and the read stream for the prefs adapters'
 * bridges. */
ES2_SIM_SERVICE(input_setup, 0x0045a5dcu, CDECL, EAX, 0, es2_sim_service_input_setup)
ES2_SIM_SERVICE(device_poll, 0x0045baa8u, CDECL, EAX, 0, es2_sim_service_device_poll)
ES2_SIM_SERVICE(key_reader, 0x0045bbc4u, CDECL, EAX, 2, es2_sim_service_key_reader)
ES2_SIM_SERVICE(joystick_object, 0x0045c524u, CDECL, EAX, 1, es2_sim_service_joystick_object)
ES2_SIM_SERVICE(joystick_summary, 0x00477904u, CDECL, EAX, 0, es2_sim_service_joystick_summary)
ES2_SIM_SERVICE(mouse_handler, 0x004809f8u, STDCALL, EAX, 4, es2_sim_service_mouse_handler)
ES2_SIM_SERVICE(mouse_event, 0x00480948u, CDECL, EAX, 3, es2_sim_service_mouse_event)
ES2_SIM_SERVICE(mouse_button, 0x00452cdcu, CDECL, EAX, 1, es2_sim_service_mouse_button)
ES2_SIM_SERVICE(mouse_queue_setup, 0x00452ae4u, CDECL, EAX, 0, es2_sim_service_mouse_queue_setup)
ES2_SIM_SERVICE(mouse_bounds, 0x0045b758u, CDECL, EAX, 0, es2_sim_service_mouse_bounds)
ES2_SIM_SERVICE(key_flush, 0x00477abcu, CDECL, EAX, 1, es2_sim_service_key_flush)
ES2_SIM_SERVICE(hotkey_none, 0x00477a7cu, CDECL, EAX, 1, es2_sim_service_hotkey_none)
ES2_SIM_SERVICE(prefs_set, 0x00459958u, CDECL, EAX, 3, es2_sim_service_prefs_set)
ES2_SIM_SERVICE(prefs_sound_a, 0x00459cb4u, CDECL, EAX, 1, es2_sim_service_prefs_sound_a)
ES2_SIM_SERVICE(prefs_sound_b, 0x00459c88u, CDECL, EAX, 1, es2_sim_service_prefs_sound_b)
ES2_SIM_SERVICE(prefs_flag_49f966, 0x00459ce0u, CDECL, EAX, 1, es2_sim_service_prefs_flag_49f966)
ES2_SIM_SERVICE(prefs_lever, 0x00459d3cu, CDECL, EAX, 1, es2_sim_service_prefs_lever)
ES2_SIM_SERVICE(prefs_flag_4aaaa8, 0x00459d68u, CDECL, EAX, 1, es2_sim_service_prefs_flag_4aaaa8)
ES2_SIM_SERVICE(sound_flag_a, 0x00462d40u, CDECL, EAX, 1, es2_sim_service_sound_flag_a)
ES2_SIM_SERVICE(sound_flag_b, 0x00462d5cu, CDECL, EAX, 1, es2_sim_service_sound_flag_b)
ES2_SIM_SERVICE(empty_ctor_event, 0x004531b5u, CDECL, EAX, 1, es2_sim_service_empty_ctor)
ES2_SIM_SERVICE(empty_ctor, 0x0049545bu, CDECL, EAX, 1, es2_sim_service_empty_ctor)
ES2_SIM_SERVICE(vector_new, 0x0049667cu, CDECL, EAX, 5, es2_sim_service_vector_new)
ES2_SIM_SERVICE(surface_attribute, 0x00481350u, CDECL, EAX, 2, es2_sim_service_surface_attribute)
ES2_SIM_SERVICE(get_key_state, 0x0049695eu, STDCALL, EAX, 1, es2_sim_service_get_key_state)
ES2_SIM_SERVICE(joy_caps, 0x00496a66u, STDCALL, EAX, 3, es2_sim_service_joy_caps)
ES2_SIM_SERVICE(joy_pos, 0x00496a6cu, STDCALL, EAX, 2, es2_sim_service_joy_pos)
ES2_SIM_SERVICE(cursor_pos, 0x0049696au, STDCALL, EAX, 1, es2_sim_service_cursor_pos)
ES2_SIM_SERVICE(client_rect, 0x00496970u, STDCALL, EAX, 2, es2_sim_service_client_rect)
ES2_SIM_SERVICE(window_rect, 0x0049694cu, STDCALL, EAX, 2, es2_sim_service_window_rect)
ES2_SIM_SERVICE(profile_string, 0x00496910u, STDCALL, EAX, 6, es2_sim_service_profile_string)
ES2_SIM_SERVICE(stream_open, 0x0047ad40u, CDECL, EAX, 2, es2_sim_service_stream_open)
ES2_SIM_SERVICE(stream_read, 0x0047af98u, CDECL, EAX, 3, es2_sim_service_stream_read)
ES2_SIM_SERVICE(stream_destroy, 0x0047ad78u, CDECL, EAX, 2, es2_sim_service_stream_destroy)
/* The write stream for prefs_store's bridge (the joystick suites store the prefs). */
ES2_SIM_SERVICE(stream_write_open, 0x0047b064u, CDECL, EAX, 2, es2_sim_service_stream_write_open)
ES2_SIM_SERVICE(stream_write, 0x0047b2b8u, CDECL, EAX, 3, es2_sim_service_stream_write)
ES2_SIM_SERVICE(stream_write_destroy, 0x0047b09cu, CDECL, EAX, 2, es2_sim_service_stream_write_destroy)
