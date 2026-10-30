#ifndef ES2_RTL_OWN_RUNTIME_H
#define ES2_RTL_OWN_RUNTIME_H
/* The own cw3220.dll (rtl/src/cw3220.c; docs/reference-game/
 * rtl_imports.md, section "the own cw3220.dll").
 *
 * Besides the 65 runtime names that DBSIM.EXE, ES.EXE and VSHELL.EXE import
 * from cw3220.dll, the DLL exports `es2_own_runtime`, which returns the
 * descriptor below. The proxy uses it to recognise the own runtime and to
 * report its counters; the tests use it to reach the DLL's state. No game
 * executable calls it. */
#include <stdint.h>

#include "es2/core/rtl_heap.h"
#include "es2/core/rtl_io.h"
#include "es2/core/rtl_start.h"

#define ES2_OWN_RUNTIME_VERSION 1u
#define ES2_OWN_RUNTIME_EXPORT "es2_own_runtime"
/* The file name of the original runtime next to the own DLL in a
 * deployment (setup/configure-runtime.ps1); the delegate mode loads it. */
#define ES2_OWN_ORIGINAL_FILE "CW3220-ORIGINAL.DLL"

enum {
    /* DBSIM.EXE and every other process: the 53 imports of DBSIM run own code. */
    ES2_OWN_MODE_OWN = 0,
    /* ES.EXE, VSHELL.EXE: the DLL loads the original and binds the
     * executable's cw3220 imports to it. */
    ES2_OWN_MODE_DELEGATE = 1
};

/* Counters of the DLL (64-bit, never reset except by `reinitialize`). */
typedef struct es2_own_counters {
    uint64_t calls;              /* export calls through the private-stack adapters */
    uint64_t foreign_calls;      /* calls from other threads, run on their own stack */
    uint64_t nested_calls;       /* calls while the private stack was busy (none expected) */
    uint64_t declined;           /* calls outside the models (formats with floats, empty
                                  * sprintf results, ...; DBSIM makes none) */
    uint64_t delegated;          /* calls of names outside DBSIM's 53 (loaded the original) */
    uint64_t init_records, winmain, atexit_registered, atexit_refused, atexit_run;
    uint64_t exit_records, stream_hooks, signals;
    int64_t exit_status;         /* -1 until WinMain returns or an exit runs */
} es2_own_counters;

/* For the tests only: `reinitialize` reruns the attach-time initialisation
 * (static state, heap, I/O start-up, time zone, lifecycle state) after the
 * harness pointed the DLL's imports at an emulated OS. A lifecycle left by
 * an emulated ExitProcess is abandoned. */
typedef struct es2_own_test_setup {
    uint32_t sentinel;           /* the heap's head block (0: the DLL's own) */
    int32_t platform;            /* the heap's platform word (-1: detected) */
    uint32_t environment;        /* environment block for _tzset (0: GetEnvironmentStrings) */
    uint32_t code_base;          /* base of the lifecycle's return-address words (0: the DLL's) */
    uint32_t stack_lo, stack_hi; /* the owner thread's stack (0, 0: the calling thread's) */
    uint32_t flags;              /* ES2_OWN_SETUP_* */
} es2_own_test_setup;
/* The static I/O state only: no handle and stream start-up (which allocates
 * the stdin/stdout buffers) and no _tzset. */
#define ES2_OWN_SETUP_NO_IO_START 1u

typedef struct es2_own_runtime_info {
    uint32_t version;            /* ES2_OWN_RUNTIME_VERSION */
    uint32_t mode;               /* ES2_OWN_MODE_* */
    uint32_t image_base, image_size;
    es2_io_layout *io;           /* the I/O tables of the DLL */
    es2_heap *heap;              /* the allocator's variables */
    es2_rtl_runtime *lifecycle;  /* the start-up and exit state */
    const es2_own_counters *counters;
    uint32_t private_stack_lo, private_stack_hi;   /* the adapters' stack */
    uint32_t life_stack_lo, life_stack_hi;         /* the lifecycle's stack */
    uint32_t exit_hooks;         /* address of the three stream hook words */
    uint32_t dummy_hook;         /* the bare `ret` the hooks hold until installed */
    int (__cdecl *reinitialize)(const es2_own_test_setup *setup);
    /* CW3220's tolower table (256 bytes) with __ctype (257 bytes) right
     * above it, as the original lays them out (4217b4, 4218b4). */
    uint32_t ctype_block;
    /* For the in-vitro comparison of the lifecycle with the original. */
    uint32_t life_return;                       /* return address of the lifecycle's guest and Win32 calls */
    const volatile uint32_t *life_return_rva;   /* the original's return RVA for the call in progress */
    uint32_t startup_handler;                   /* handler of __startup's frame record (CW3220: 405160) */
    uint32_t window_callback;                   /* EnumThreadWindows callback (CW3220: ES2_RTL_ENUM_PROC_RVA) */
    uint32_t abort_text;                        /* "Abnormal program termination" (CW3220: 422298) */
} es2_own_runtime_info;

typedef const es2_own_runtime_info *(__cdecl *es2_own_runtime_fn)(void);

#endif
