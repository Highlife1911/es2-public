/* DBSIM's start-up before WinMain as own code (own_process.md, "static
 * initialisation, the 65 init records and the 26 exit records" and "the static
 * initialisation as own code"):
 * the c0w32 entry 0x401000 up to its jump into CW3220's __startup, the 65 init
 * records in the order CW3220's selector gives them, and the 26 exit records in
 * exit order. Every record is an own function keyed by the VA the user's
 * DBSIM.EXE holds in its record table; a VA without one is refused through the
 * dispatch. State is read and written only through the guest memory view; the
 * calls the original makes to other code (operator new and delete, _memset,
 * _memmove, CreateFileA, CloseHandle, GetModuleHandleA) go through
 * es2_sim_call, so the services of the dispatch answer them. */
#ifndef ES2_SIM_INIT_H
#define ES2_SIM_INIT_H
#include <stdint.h>
#include "es2/core/guest.h"
#include "es2/sim/dispatch.h"

/* Guest VAs (DBSIM.EXE). */
#define ES2_SIM_VA_ENTRY 0x00401000u            /* c0w32 entry (the PE entry point) */
#define ES2_SIM_VA_MODULE_TABLE 0x0049702cu     /* handed to __startup: init 0x4a79d8..0x4a7b5e, exit ..0x4a7bfa */
#define ES2_SIM_VA_BSS_START 0x004a7bfcu        /* the entry zero-fills [start, end) */
#define ES2_SIM_VA_BSS_END 0x006cbee8u
#define ES2_SIM_VA_EXCEPT_CONTEXT 0x004a7884u   /* the exception context block; StackBase-8 points at it */
#define ES2_SIM_VA_STAGE_TABLE 0x004a804cu      /* stage registry: {dword function, word stage} */
#define ES2_SIM_VA_STAGE_COUNT 0x00497074u      /* its signed count word */
#define ES2_SIM_VA_VXD_HANDLE 0x006cbc50u       /* init record 64: the DARKMONO.VXD handle */
#define ES2_SIM_VA_TABLE_808 0x004a08b4u        /* init record 53: the 0x808-byte block */
#define ES2_SIM_INIT_RECORDS 65u
#define ES2_SIM_EXIT_RECORDS 26u

typedef struct es2_sim_init {
    const es2_guest_memory *memory;
    es2_sim_dispatch *dispatch;
    /* The guest stack's StackBase (fs:[4] of the thread that runs the guest):
     * the entry keeps the exception context at StackBase-8 and the
     * construction counter at StackBase-4, as on the original's main thread. */
    uint32_t stack_base;
    /* Out: records run, the VA being run when a call was refused. */
    unsigned init_run, exit_run;
    uint32_t current;
} es2_sim_init;

/* 0x401000 up to `jmp __startup`. 0, or 1 when a call was refused. */
int es2_sim_c0w32_entry(es2_sim_init *c);

/* One step of CW3220's selector (mode 0 init, 1 exit; the runtime lifecycle model
 * es2_rtl_select_record over the module table 0x49702c): marks the next
 * record done and runs its own function. 0 a record ran (*va set), 2 none
 * left, 1 refused (no own function, or a call it made was refused). */
int es2_sim_run_next_record(es2_sim_init *c, unsigned mode, uint32_t *va);
/* Every pending init (exit) record in selector order: 0 or 1 (refused). */
int es2_sim_run_init_records(es2_sim_init *c);
int es2_sim_run_exit_records(es2_sim_init *c);

/* The own functions by VA (NULL when there is none); for tests. */
typedef int (*es2_sim_record_fn)(es2_sim_init *c);
es2_sim_record_fn es2_sim_record_function(uint32_t va, unsigned mode);
/* The record VAs of the reference file in table order (65 init, 26 exit). */
uint32_t es2_sim_record_va(unsigned mode, unsigned index);

#endif
