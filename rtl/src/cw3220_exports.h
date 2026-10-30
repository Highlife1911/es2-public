/* The exports of the own cw3220.dll. rtl/cw3220.def names the same
 * symbols: a code export `name` is the stub own_x_<id>, a data export the
 * variable own_data_<id>; //rtl:cw3220_dll_test checks the export table
 * against this list.
 *
 * CODE(name, id, kind, model), DATA(name, id). Kinds of DBSIM's 53 imports:
 *   PURE    es2_rtl_function `model` (core/src/rtl.c)
 *   HEAP    calloc 0, realloc 1, free 2 (core/src/rtl_heap.c)
 *   IO      es2_io_function `model` (core/src/rtl_io.c)
 *   ATEXIT, EXIT, ABORT, ERROREXIT, STARTUP  the lifecycle (core/src/rtl_start.c)
 *   SEH     the frame records' exception handler, CATCH its clean-up
 * EXTRA: names only ES.EXE and VSHELL.EXE import; always the original's. */
#define OWN_EXPORTS(CODE, DATA) \
    CODE("_memset", memset, PURE, ES2_RTL_MEMSET) \
    CODE("_memcpy", memcpy, PURE, ES2_RTL_MEMCPY) \
    CODE("_memmove", memmove, PURE, ES2_RTL_MEMMOVE) \
    CODE("_memcmp", memcmp, PURE, ES2_RTL_MEMCMP) \
    CODE("_strlen", strlen, PURE, ES2_RTL_STRLEN) \
    CODE("_strcat", strcat, PURE, ES2_RTL_STRCAT) \
    CODE("_strncat", strncat, PURE, ES2_RTL_STRNCAT) \
    CODE("_strncpy", strncpy, PURE, ES2_RTL_STRNCPY) \
    CODE("_strchr", strchr, PURE, ES2_RTL_STRCHR) \
    CODE("_strrchr", strrchr, PURE, ES2_RTL_STRRCHR) \
    CODE("_strstr", strstr, PURE, ES2_RTL_STRSTR) \
    CODE("_strncmp", strncmp, PURE, ES2_RTL_STRNCMP) \
    CODE("_stricmp", stricmp, PURE, ES2_RTL_STRICMP) \
    CODE("_strnicmp", strnicmp, PURE, ES2_RTL_STRNICMP) \
    CODE("_strupr", strupr, PURE, ES2_RTL_STRUPR) \
    CODE("_strlwr", strlwr, PURE, ES2_RTL_STRLWR) \
    CODE("_itoa", itoa, PURE, ES2_RTL_ITOA) \
    CODE("_atol", atol, PURE, ES2_RTL_ATOL) \
    CODE("_sprintf", sprintf, PURE, ES2_RTL_SPRINTF) \
    CODE("_vsprintf", vsprintf, PURE, ES2_RTL_VSPRINTF) \
    CODE("_calloc", calloc, HEAP, 0) \
    CODE("_realloc", realloc, HEAP, 1) \
    CODE("_free", free, HEAP, 2) \
    CODE("_open", open, IO, ES2_IO_OPEN) \
    CODE("_close", close, IO, ES2_IO_CLOSE) \
    CODE("_read", read, IO, ES2_IO_READ) \
    CODE("_write", write, IO, ES2_IO_WRITE) \
    CODE("_lseek", lseek, IO, ES2_IO_LSEEK) \
    CODE("_filelength", filelength, IO, ES2_IO_FILELENGTH) \
    CODE("_fopen", fopen, IO, ES2_IO_FOPEN) \
    CODE("_fclose", fclose, IO, ES2_IO_FCLOSE) \
    CODE("_fread", fread, IO, ES2_IO_FREAD) \
    CODE("_fwrite", fwrite, IO, ES2_IO_FWRITE) \
    CODE("_fseek", fseek, IO, ES2_IO_FSEEK) \
    CODE("_ftell", ftell, IO, ES2_IO_FTELL) \
    CODE("_rewind", rewind, IO, ES2_IO_REWIND) \
    CODE("_fscanf", fscanf, IO, ES2_IO_FSCANF) \
    CODE("_flushall", flushall, IO, ES2_IO_FLUSHALL) \
    CODE("_findfirst", findfirst, IO, ES2_IO_FINDFIRST) \
    CODE("_findnext", findnext, IO, ES2_IO_FINDNEXT) \
    CODE("_time", time, IO, ES2_IO_TIME) \
    CODE("_printf", printf, IO, ES2_IO_PRINTF) \
    CODE("__fputc", fputc, IO, ES2_IO_FPUTC) \
    CODE("_fputs", fputs, IO, ES2_IO_FPUTS) \
    CODE("_atexit", atexit, ATEXIT, 0) \
    CODE("__exit", exit, EXIT, 0) \
    CODE("_abort", abort, ABORT, 0) \
    CODE("__ErrorExit", ErrorExit, ERROREXIT, 0) \
    CODE("__startup", startup, STARTUP, 0) \
    CODE("__ExceptionHandler", ExceptionHandler, SEH, 0) \
    CODE("@_CatchCleanup$qv", CatchCleanup, CATCH, 0) \
    DATA("_errno", errno) \
    DATA("__streams", streams) \
    CODE("@$bdele$qpv", bdele, EXTRA, 0) \
    CODE("@$bdla$qpv", bdla, EXTRA, 0) \
    CODE("@$bnew$qui", bnew, EXTRA, 0) \
    CODE("@$bnwa$qui", bnwa, EXTRA, 0) \
    CODE("_spawnv", spawnv, EXTRA, 0) \
    CODE("_scanf", scanf, EXTRA, 0) \
    CODE("_toupper", toupper, EXTRA, 0) \
    CODE("_strcmp", strcmp, EXTRA, 0) \
    CODE("_strcpy", strcpy, EXTRA, 0) \
    CODE("_strnset", strnset, EXTRA, 0) \
    CODE("_remove", remove, EXTRA, 0) \
    DATA("__ctype", ctype)

/* 62 code and 3 data exports: DBSIM imports 53 of the 65 names. */
#define OWN_DBSIM_IMPORTS 53u
#define OWN_ALL_NAMES 65u
