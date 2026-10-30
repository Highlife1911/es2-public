/* The cockpit panel pieces of row 48 (sim/src/panel.c; hud.md, "the scroll zones and the radar page"): the scroll zones 0x433770, MFDRadar slot +4 0x43eedc (the radar draw 0x43eef4 and the HUD radar 0x43f2d8 keep their rows in hud_load.h, which delegate to panel.c). */
ES2_SIM_SERVICE(scroll_zones, 0x00433770u, CDECL, EAX, 1, es2_sim_service_scroll_zones)
ES2_SIM_SERVICE(radar_update, 0x0043eedcu, CDECL, EAX, 1, es2_sim_service_radar_update)
