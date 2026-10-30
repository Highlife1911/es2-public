/* The shell leftovers (src/loop.c, main_loop.md, "the shell leftovers as own
 * rows"): the flight-key capture push and pop of the modal dialogs (the
 * callbacks [0x49dd94]/[0x49dd98]), the HUD sub-object getter, the view's
 * subject label, the language path of the string resources and the voice
 * archive name, the cursor store, and the two window-teardown helpers. */
ES2_SIM_SERVICE(subject_label, 0x0045e208u, CDECL, EAX, 0, es2_sim_service_subject_label)
ES2_SIM_SERVICE(language_path, 0x0045ef1cu, CDECL, EAX, 2, es2_sim_service_language_path)
ES2_SIM_SERVICE(voice_name, 0x0045ef84u, CDECL, EAX, 0, es2_sim_service_voice_name)
ES2_SIM_SERVICE(hud_sub_object, 0x004610adu, CDECL, EAX, 0, es2_sim_service_hud_sub_object)
ES2_SIM_SERVICE(key_capture_push, 0x00461250u, CDECL, EAX, 0, es2_sim_service_key_capture_push)
ES2_SIM_SERVICE(key_capture_pop, 0x00461274u, CDECL, EAX, 0, es2_sim_service_key_capture_pop)
ES2_SIM_SERVICE(cursor_store, 0x00464c68u, CDECL, EAX, 1, es2_sim_service_cursor_store)
ES2_SIM_SERVICE(window_nothing, 0x004650ccu, CDECL, EAX, 0, es2_sim_service_nothing)
ES2_SIM_SERVICE(timer_kill, 0x004650d0u, CDECL, EAX, 0, es2_sim_service_timer_kill)
