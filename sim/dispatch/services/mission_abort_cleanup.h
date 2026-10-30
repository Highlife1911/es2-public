/* The mission start's abort-notify clean-up 0x461304 (registered at 0x461626, run by the error report 0x479c78 from its notify list) and the remap table's release 0x46a4ac it calls (src/loop.c; own_process.md, "the mission start's clean-up 0x461304 and the call helpers' EXITED"). */
ES2_SIM_SERVICE(abort_cleanup, 0x00461304u, CDECL, EAX, 0, es2_sim_service_abort_cleanup_461304)
ES2_SIM_SERVICE(remap_release, 0x0046a4acu, CDECL, EAX, 0, es2_sim_service_remap_release_46a4ac)
