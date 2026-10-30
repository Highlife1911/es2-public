/* CW3220's _time over the files model and its OS clock (src/services_files.c,
 * es2/sim/files.h); the -D demo pick 0x45ceb8 is its one caller. */
ES2_SIM_SERVICE(file_time, 0x004967fcu, CDECL, EAX, 1, es2_sim_service_file_time)
