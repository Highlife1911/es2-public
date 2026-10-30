/* CW3220's file imports over the files model (src/services_files.c,
 * es2/sim/files.h). The slots are the named arguments the entry always reads:
 * _fscanf reads its format's pointers beyond its two (own callers pass
 * ES2_SIM_FILES_SCRATCH_SLOTS), _open its mode slot only with O_CREAT (DBSIM's
 * calls of _open(path, 8000h) push two). */
ES2_SIM_SERVICE(file_fopen, 0x00496802u, CDECL, EAX, 2, es2_sim_service_file_fopen)
ES2_SIM_SERVICE(file_fclose, 0x00496838u, CDECL, EAX, 1, es2_sim_service_file_fclose)
ES2_SIM_SERVICE(file_fread, 0x0049684au, CDECL, EAX, 4, es2_sim_service_file_fread)
ES2_SIM_SERVICE(file_fwrite, 0x00496832u, CDECL, EAX, 4, es2_sim_service_file_fwrite)
ES2_SIM_SERVICE(file_fseek, 0x0049682cu, CDECL, EAX, 3, es2_sim_service_file_fseek)
ES2_SIM_SERVICE(file_ftell, 0x004967deu, CDECL, EAX, 1, es2_sim_service_file_ftell)
ES2_SIM_SERVICE(file_rewind, 0x00496772u, CDECL, EAX, 1, es2_sim_service_file_rewind)
ES2_SIM_SERVICE(file_fscanf, 0x004967b4u, CDECL, EAX, 2, es2_sim_service_file_fscanf)
ES2_SIM_SERVICE(file_open, 0x004967c6u, CDECL, EAX, 2, es2_sim_service_file_open)
ES2_SIM_SERVICE(file_close, 0x00496874u, CDECL, EAX, 1, es2_sim_service_file_close)
ES2_SIM_SERVICE(file_read, 0x00496808u, CDECL, EAX, 3, es2_sim_service_file_read)
ES2_SIM_SERVICE(file_write, 0x0049677eu, CDECL, EAX, 3, es2_sim_service_file_write)
ES2_SIM_SERVICE(file_lseek, 0x00496826u, CDECL, EAX, 3, es2_sim_service_file_lseek)
ES2_SIM_SERVICE(file_filelength, 0x00496856u, CDECL, EAX, 1, es2_sim_service_file_filelength)
ES2_SIM_SERVICE(file_findfirst, 0x0049685cu, CDECL, EAX, 3, es2_sim_service_file_findfirst)
ES2_SIM_SERVICE(file_findnext, 0x00496862u, CDECL, EAX, 1, es2_sim_service_file_findnext)
ES2_SIM_SERVICE(file_flushall, 0x00496850u, CDECL, EAX, 0, es2_sim_service_file_flushall)
/* The install-root path helper 0x45ee00 (adapters of the config group call it). */
ES2_SIM_SERVICE(path_root, 0x0045ee00u, CDECL, EAX, 1, es2_sim_service_path_root)
