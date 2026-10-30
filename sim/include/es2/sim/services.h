/* The services of sim/dispatch/services.h: their shared context and the
 * registration into a dispatch table. */
#ifndef ES2_SIM_SERVICES_H
#define ES2_SIM_SERVICES_H
#include "es2/core/guest.h"
#include "es2/core/rtl.h"
#include "es2/sim/dispatch.h"
#include "es2/sim/heap.h"
#include "sim/oracle/virtual_time.h"

/* Guest VAs the runtime services read or write (DBSIM.EXE). */
#define ES2_SIM_VA_HEAP_BYTES 0x006bb30cu        /* sum of CW3220 block headers of live game blocks */
#define ES2_SIM_VA_ARENA 0x006bb2f8u             /* the arena header (base, size, ...; set up only with -l) */
#define ES2_SIM_VA_ARENA_LIMIT 0x006bb304u       /* new's arena threshold (0 without the arena) */
#define ES2_SIM_VA_COUNTER_POINTER 0x006cbee4u   /* -> the construction counter (StackBase - 4, set by the c0w32 entry) */
#define ES2_SIM_VA_RECORD_HANDLER 0x004967f0u    /* thunk of CW3220's __ExceptionHandler, the records' handler field */
#define ES2_SIM_VA_LIMITER_RETURN 0x004678d5u    /* return address of the limiter's GetTickCount call */
#define ES2_SIM_VA_LIMITER_PREVIOUS 0x004d3b74u  /* the limiter's previous clock */

typedef struct es2_sim_services {
    const es2_guest_memory *memory;
    es2_sim_heap *heap;
    es2_vt *vt;
    es2_rtl_memory rtl;                  /* set by es2_sim_register_services */
    /* FS:0 of the simulation thread; NULL: es2_machine_fs0 (es2sim: the sim
     * backend's variable, never the real fs:[0]). Tests
     * point it at a word of their own. */
    uint32_t *fs0;
} es2_sim_services;

/* Registers every row of sim/dispatch/services.h with `s` as user data.
 * 0, or 1 when the table is full. */
int es2_sim_register_services(es2_sim_dispatch *d, es2_sim_services *s);
/* The number of rows (for tests and the start record). */
unsigned es2_sim_service_count(void);
/* Row i's VA and name (NULL past the end). */
const char *es2_sim_service_row(unsigned i, uint32_t *va);

/* The headless KERNEL32 services of the start-up (src/services_platform.c):
 * their calls, for the log and the tests. */
typedef struct es2_sim_platform_calls {
    unsigned create_file, close_handle, module_handle;
    uint32_t create_file_args[7];
    uint32_t create_file_result;
    char create_file_name[64];
} es2_sim_platform_calls;
const es2_sim_platform_calls *es2_sim_platform_calls_get(void);
void es2_sim_platform_calls_reset(void);

#endif
