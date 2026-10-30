/* The own process's host (own_process.md sections 2 to 4 and 9): the guest
 * image built at 0x400000 with the original VAs from the own tables (no
 * DBSIM.EXE is read), CODE not executable and no-access outside the pages of
 * the own CODE table runs, the 134 import slots filled with refusal traps, the
 * own heap, virtual time, the services, the registry's adapters and the stage-1
 * trap. Windows x86 only; the claim of 0x400000..0x6d7fff must happen before
 * the CRT runs (es2sim.exe's entry,
 * tools/reference-game/invitro/startup_reservation.h). */
#ifndef ES2_SIM_HOST_H
#define ES2_SIM_HOST_H
#include <stdint.h>
#include "es2/sim/adapters.h"
#include "es2/sim/dispatch.h"
#include "es2/sim/files.h"
#include "es2/sim/heap.h"
#include "es2/sim/image.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"
#include "es2/sim/trap.h"
#include "sim/oracle/virtual_time.h"
#include "sim/oracle/journal.h"
#include "sim/oracle/mem_watch.h"

/* Exit codes of es2sim.exe besides the guest's own (the dword 0x4d27b8 at
 * WinMain's return: 0, 1, 3, 4, 6; launcher.md, "DBSIM's process shell,
 * start-up and exit"). */
#define ES2_SIM_EXIT_USAGE 64
#define ES2_SIM_EXIT_IMAGE 65        /* the own layout or data tables refused (no file is read) */
#define ES2_SIM_EXIT_CLAIM 66        /* the range 0x400000..0x6d7fff could not be claimed */
#define ES2_SIM_EXIT_SETUP 67        /* DEP, heap, dispatch, trap or log set-up failed */
#define ES2_SIM_EXIT_FAULT 71        /* an exception nothing handled */
/* ES2_SIM_EXIT_REFUSED (70): a refused call (es2/sim/dispatch.h). */

#define ES2_SIM_VA_EXIT_CODE 0x004d27b8u   /* WinMain returns this dword (launcher.md, "DBSIM's process shell, start-up and exit") */
#define ES2_SIM_VA_FRAME_BOUNDARY 0x0045f4adu
#define ES2_SIM_VA_TICK_CONTEXT 0x004d24bcu   /* the globals object the tick record reads (frame counter at +0xbf) */
#define ES2_SIM_VA_TICK_CLOCK 0x004d3b64u     /* the 24 timing bytes */
#define ES2_SIM_HEAP_PREFERRED_BASE 0x20000000u
#define ES2_SIM_HEAP_PREFERRED_SIZE 0x40000000u
#define ES2_SIM_MAX_ENTRIES 4096u

/* Virtual time without a configuration: the parameters of the recorded
 * suites (record-suite.ps1: step 47 ms, start 16,777,216, intra-frame on);
 * no pacing (there is no wall clock in a headless run). */
#define ES2_SIM_VT_STEP 47u
#define ES2_SIM_VT_INTRA 1

typedef struct es2_sim {
    uint8_t *image;                         /* == image_base in es2sim.exe */
    es2_sim_image_info info;
    es2_sim_import imports[ES2_SIM_MAX_IMPORTS];
    unsigned import_count;
    uint32_t import_slots[ES2_SIM_MAX_IMPORTS];
    const char *import_names[ES2_SIM_MAX_IMPORTS];
    uint8_t *import_traps;                  /* one trap address per slot, never executable */
    es2_guest_memory memory;
    es2_sim_heap heap;
    uint32_t heap_head[4];                  /* the list head block (words -8, -4, links +0, +4) */
    es2_vt vt;
    es2_sim_services services;
    es2_sim_dispatch dispatch;
    es2_sim_entry entries[ES2_SIM_MAX_ENTRIES];
    void *log;                              /* HANDLE of the tick log, or NULL */
    unsigned long ticks;                    /* tick records written */
    es2_mem_watch_table watches;            /* [trace] watchN of the configuration */
    unsigned long dep_flags;
    int dep_permanent;
    const char *error;
} es2_sim;

/* Builds the guest image (own_process.md, "es2sim without DBSIM.EXE")
 * without a file. Takes the layout from the own table (es2_sim_image_info_own;
 * base 0x400000, size 0x2d8000 checked), commits the claimed range at
 * `reserved` (== 0x400000) zeroed, writes the own runs
 * (es2_sim_dbsim_data_write: the initialised DATA and the CODE runs), fills
 * every import slot of the own import table (es2_sim_imports_own) with a trap
 * address and sets the protections: headers and read-only sections read-only,
 * writable sections read-write, CODE never executable, read-only only on the
 * pages of the CODE_TABLE runs and no-access elsewhere (a stray read faults;
 * the RTTI counts come from es2_sim_dbsim_rtti_count). 0, or ES2_SIM_EXIT_IMAGE
 * / ES2_SIM_EXIT_CLAIM / ES2_SIM_EXIT_SETUP with sim->error. */
int es2_sim_map(es2_sim *sim, void *reserved);

/* The protection es2_sim_map gives the CODE page at `va` (PAGE_READONLY on a
 * CODE_TABLE run's page, else PAGE_NOACCESS); the page lies in
 * [info->code_lo, info->code_hi). */
unsigned long es2_sim_code_page_protection(uint32_t va);

/* Heap, virtual time, services, adapters, the sealed dispatch, DEP and the
 * trap, on the calling thread (the simulation thread). `log` may be NULL.
 * The memory watches of [trace] watch_count/watchN, read as the
 * proxy reads them; a malformed or refused watch fails the set-up (the
 * proxy refuses its whole tick log). 0 or ES2_SIM_EXIT_SETUP with
 * sim->error. */
int es2_sim_setup(es2_sim *sim, const wchar_t *config, const wchar_t *log);

/* CW3220's attach on the heap before DBSIM's entry, in its init
 * records' order (es2/sim/heap.h): the copy of this process's environment,
 * the file services on `files` in the runtime region `region` (the handles
 * and the stdin/stdout buffers; es2_sim_files_init), the C++ static
 * objects. Then [trace] heap_trace=<path> of `config` opens the heap trace
 * (created new; own_process.md, "the heap trace") with the heap as DBSIM's
 * entry finds it; es2_sim_finish completes it. After es2_sim_setup, before
 * the c0w32 entry. 0 or ES2_SIM_EXIT_SETUP with sim->error. */
int es2_sim_runtime_attach(es2_sim *sim, es2_sim_files *files, uint32_t region, const wchar_t *config);

/* The guest's exit code: the dword at 0x4d27b8. */
uint32_t es2_sim_exit_code(const es2_sim *sim);

/* One tick record (tick_log.md schema 2) at the frame-counter increment of the
 * own main loop, every field read from guest memory at its VA (own_process.md
 * section 9), the virtual-time counters and, with a journal in replay, its
 * counters. Then the watch events of the configured watches
 * (//sim/oracle:mem_watch; sequence = this record's, frame = the counter at
 * 0x4d24bc+0xbf before its increment), as the proxy writes them after its tick
 * record. */
void es2_sim_tick(es2_sim *sim, const es2_journal *journal);

/* Completes the heap trace (if any), writes the exit record to the log (if
 * any) and closes it. */
void es2_sim_finish(es2_sim *sim, const char *reason, int exit_code);

#endif
