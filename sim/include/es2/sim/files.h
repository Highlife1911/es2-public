/* The CW3220 file imports of the own process (own_process.md section 6;
 * launcher.md, "DBSIM's start-up as own code"): _fopen, _fclose, _fread,
 * _fwrite, _fseek, _ftell, _rewind, _fscanf, _open, _close, _read, _write,
 * _lseek, _filelength, _findfirst, _findnext, _flushall and the console stubs,
 * as services keyed by DBSIM's thunks (sim/dispatch/services.h), all served by
 * the file model (core/src/rtl_io.c, confirmed in vitro against CW3220 there).
 *
 * CW3220's variables that the model keeps in guest memory (the handle table,
 * the FILE array, errno, the find list, the two constant tables) live in a
 * runtime region the host provides (es2sim: host memory at its own address,
 * a test: an arena range); CW3220's own blocks (stream buffers, find nodes)
 * come from the game's heap, as in the original, whose CW3220 has one heap
 * for both (own_process.md, "the heap trace"; with a second heap the game's
 * blocks lie elsewhere than the original's). The Win32 calls the model makes go to
 * `os`: headless, the real file API on the working directory (the install
 * directory, as for the original), GetStdHandle answers NULL and
 * GetStartupInfoA an empty block (a GUI process without a console), every
 * other call is the system's. */
#ifndef ES2_SIM_FILES_H
#define ES2_SIM_FILES_H
#include <stdint.h>
#include "es2/core/guest.h"
#include "es2/core/rtl_io.h"
#include "es2/sim/heap.h"

/* Bytes of the runtime region: the layout's tables (about 0x900 bytes) and
 * an argument scratch block the services copy each call's slots into. */
#define ES2_SIM_FILES_REGION_SIZE 0x1000u
#define ES2_SIM_FILES_SCRATCH_SLOTS 16u

typedef struct es2_sim_files {
    const es2_guest_memory *memory;
    es2_io_layout layout;
    es2_io_env env;
    es2_sim_heap *heap;                /* the process's heap: CW3220 has one for DBSIM's blocks and its own */
    uint32_t region, scratch, fmode_word, dummy_hook;
    /* The Win32 calls; es2_sim_files_init sets the headless ones with this
     * instance as their context. */
    void *os_context;
    void (*os)(void *context, const es2_guest_memory *memory, es2_io_os *call);
    uint8_t local_time[16];            /* the SYSTEMTIME the headless GetLocalTime answers */
    uint64_t calls[ES2_IO_FUNCTION_COUNT];
    uint64_t os_calls;
} es2_sim_files;

/* Lays CW3220's I/O variables out in the region at guest `region`
 * (ES2_SIM_FILES_REGION_SIZE writable bytes), writes their static values and
 * runs the start-up of the handle table and of the FILE array (CW3220's init
 * records 409014 and 407248, es2_io_startup) and _tzset without TZ
 * (es2_io_tzset: the headless zone, UTC) over the headless OS calls.
 * CW3220's own blocks (stream buffers, find nodes) come from `heap`, the
 * heap DBSIM's imports use (own_process.md, "the heap trace"). 0 on
 * success. */
int es2_sim_files_init(es2_sim_files *f, const es2_guest_memory *memory, uint32_t region, es2_sim_heap *heap);
/* The headless OS calls (Win32 file API; installed by es2_sim_files_init;
 * `context` the es2_sim_files). The clock is the replay's: GetLocalTime
 * answers `local_time`, GetTimeZoneInformation TIME_ZONE_ID_UNKNOWN with
 * every bias 0. */
void es2_sim_files_os_headless(void *context, const es2_guest_memory *memory, es2_io_os *call);
/* The windowed run's: GetLocalTime and GetTimeZoneInformation of the
 * system, every other call the headless one's. */
void es2_sim_files_os_wall_clock(void *context, const es2_guest_memory *memory, es2_io_os *call);
/* The headless clock: `local_time` = `seconds` since 1970 as a UTC
 * SYSTEMTIME, so that _time after es2_io_tzset with no TZ over the headless
 * zone returns `seconds` (0 gives -1, as CW3220 for a value <= 0;
 * 1..0x7fffffff are exact). es2_sim_files_init sets the default. */
void es2_sim_files_set_time(es2_sim_files *f, uint32_t seconds);
/* 1997-01-01 00:00:00 UTC: 852076800 % 3 = 0, the CD's demo list of three
 * gives DEMO1. */
#define ES2_SIM_FILES_TIME_DEFAULT 852076800u

/* The instance the service rows use (one per process). */
void es2_sim_files_bind(es2_sim_files *f);
es2_sim_files *es2_sim_files_bound(void);

/* One import with its cdecl argument slots (host copies; up to
 * ES2_SIM_FILES_SCRATCH_SLOTS). 0 and *eax, or 1 when the model does not
 * cover the call (ES2_IO_UNSUPPORTED: nothing was done). */
int es2_sim_files_call(es2_sim_files *f, es2_io_function function, const uint32_t *slots, unsigned count, uint32_t *eax);

/* 0x45ee00(p) (listing 0x45ee00..0x45ee5c): the install root 0x4d27bc + p
 * (a leading "X:" of p dropped) in the static buffer 0x4d285c, through the
 * strcat import. regs = the registers it leaves: EAX 0x4d285c, ECX 0 and
 * EDX the appended length + 1 (strcat's, core rtl.c). The service row
 * serves the adapters (prefs_video, keyjoy_load, sound_config); the start-up
 * calls it directly. */
void es2_sim_path_root(const es2_guest_memory *m, uint32_t p, uint32_t regs[3]);

#endif
