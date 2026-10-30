/* The mission-start drop sequence 0x45d85c (sim/src/cockpit.c) and the cockpit panel constructor 0x431bf8 as a row of its own (sim/src/hud_cockpit.c); cockpit.md, "drop sequence 45d85c and panel constructor 431bf8". */
ES2_SIM_SERVICE(drop_sequence, 0x0045d85cu, CDECL, EAX, 0, es2_sim_service_drop_sequence)
ES2_SIM_SERVICE(panel_ctor, 0x00431bf8u, CDECL, EAX, 2, es2_sim_service_panel_ctor)
