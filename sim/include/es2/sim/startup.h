/* DBSIM's process start-up after the init records as own code (launcher.md,
 * "DBSIM's process shell, start-up and exit" and "DBSIM's start-up as own
 * code"): CW3220's call of
 * WinMain (hInstance, lpCmdLine, nCmdShow 10), WinMain 0x46538c with the
 * command-line split 0x4652f8, the class 0x4650e8, prefs_video, the early
 * parser 0x45e6cc, the window 0x465158 headless, the wait cursor, the game
 * main's start-up part 0x45f160..0x45f42b (drive.cfg, the root strings, the
 * exit-code reset, the main parser 0x45e758, stages 0 and 1, the stream buffer
 * size 0x467c34, the VOL registration 0x473260, error.str, the -eggplant
 * refusal, the memory check, the -l arena call), the fatal report 0x479c78 of
 * the start-up and, after an early return of the game main, WinMain's tail
 * (PostQuitMessage, the pump until WM_QUIT, 0x466730).
 *
 * The run stops at the game main's call of the mission load 0x461608: with
 * the window deferred (below) it stops before it with a named refusal; with
 * a window it calls the load through the dispatch and, should the load
 * return, stops there (the main loop is loop.c's).
 *
 * Guest state is read and written only through the guest view; the
 * original's stack locals (WinMain's argv array, the game main's frame, the
 * registration's find block, the error.str holder and stream) live in a
 * frame area the caller provides; every call of other DBSIM code (hooks,
 * services: operator new, the file imports, stage_dispatch, prefs_video, the
 * VOL-aware stream) goes through es2_sim_call, so the dispatch answers it or
 * refuses it by name. */
#ifndef ES2_SIM_STARTUP_H
#define ES2_SIM_STARTUP_H
#include <stdint.h>
#include "es2/core/guest.h"
#include "es2/sim/dispatch.h"
#include "es2/sim/process_platform.h"

/* Guest VAs (DBSIM.EXE). */
#define ES2_SIM_VA_MISSION_LOAD 0x00461608u
#define ES2_SIM_VA_DIB_SECTION 0x00464d44u     /* deferred headless (see es2_sim_startup.defer_dib) */
#define ES2_SIM_VA_DRIVE_ROOT 0x004d280cu      /* the CD root from drive.cfg */
#define ES2_SIM_VA_INSTALL_ROOT 0x004d27bcu    /* the install root ("") */
#define ES2_SIM_VA_ARCHIVES 0x004a0d18u        /* the archive manager */

/* Outcome of es2_sim_startup_run. */
typedef enum es2_sim_startup_outcome {
    ES2_SIM_STARTUP_RETURNED = 0,   /* WinMain returned; exit code in `exit_code` (the caller runs exit records) */
    ES2_SIM_STARTUP_EXIT,           /* __exit(code): the fatal report (1); no atexit functions, no exit records */
    ES2_SIM_STARTUP_REFUSED,        /* a call was refused (logged by the dispatch's refusal callback) */
    ES2_SIM_STARTUP_LOADED,         /* the mission load returned and no `after_load` is set */
    ES2_SIM_STARTUP_STOPPED         /* `after_load` stopped the run (frame_limit) */
} es2_sim_startup_outcome;

/* The headless platform's answers and the record of what was asked. */
typedef struct es2_sim_startup_platform {
    uint32_t screen_cx, screen_cy;        /* GetSystemMetrics(SM_CXSCREEN / SM_CYSCREEN) */
    uint32_t frame_cx, frame_cy, caption; /* SM_CXFRAME, SM_CYFRAME, SM_CYCAPTION */
    uint32_t total_phys;                  /* GlobalMemoryStatus dwTotalPhys */
    uint32_t window, cursor_wait, cursor_arrow, class_atom;   /* stand-in handles */
    /* Out: the calls (counts) and the last message box. */
    unsigned register_class, load_library, create_window, message_box, post_quit, peeks, clip_cursor, memory_status;
    char message_text[256], message_caption[64];
} es2_sim_startup_platform;

typedef struct es2_sim_startup {
    const es2_guest_memory *memory;
    es2_sim_dispatch *dispatch;
    /* Guest bytes for the original's stack locals: [frame_lo, frame_hi),
     * at least 0x2000 bytes, writable. */
    uint32_t frame_lo, frame_hi;
    /* The process command line as GetCommandLineA would give it (program
     * name first) at a guest address, NUL-terminated. */
    uint32_t command_line;
    /* 1: the window step 0x465158 is done headless and the DIB section
     * 0x464d44 (the frame-buffer objects of the graphics library) is
     * deferred: the run stops with a refusal of 0x464d44 where the mission
     * load would be called (it is the first code that reads those objects).
     * 0: the window step builds the DIB section (dib_section, else through
     * the dispatch) and the start-up goes on to the mission load. */
    int defer_dib;
    /* Own code for the steps the start-up cannot take itself. Each NULL: the
     * call goes through the dispatch (refused unless served), or the run stops
     * with ES2_SIM_STARTUP_LOADED after the load.
     *   dib_section  0x464d44 (with defer_dib 0): 0, or the refused VA;
     *   after_load   the game main from 0x45f430 with its frame at `ebp`:
     *                0 returned (WinMain's tail follows), 1 stopped, 2
     *                refused (*refused_va set);
     *   quit_pump    WinMain's tail after PostQuitMessage: the pump until it
     *                removes WM_QUIT; 0, or the refused VA. */
    uint32_t (*dib_section)(void *user);
    int (*after_load)(void *user, uint32_t ebp, uint32_t *refused_va);
    uint32_t (*quit_pump)(void *user);
    void *own_user;
    es2_sim_startup_platform platform;
    /* Out. */
    es2_sim_startup_outcome outcome;
    uint32_t exit_code, h_instance, cmd_line, argc;
    uint32_t refused_va;
    unsigned archives;                    /* archives the VOL registration accepted */
    /* Optional: a platform event (message box, fatal report) for the log. */
    void (*event)(void *user, const char *kind, const char *text);
    void *event_user;
    /* MessageBoxA, __exit and _abort (es2/sim/process_platform.h); NULL
     * headless (IDOK, the fatal report's exit as the outcome only, _abort
     * refused). */
    const es2_sim_process_platform *process;
} es2_sim_startup;

/* Defaults of the headless platform (640 x 480 screen, 4/4/18 frame and
 * caption, 64 MiB of physical memory: no arena, as own_process.md 6). */
void es2_sim_startup_defaults(es2_sim_startup *s);

/* CW3220's GUI branch after the init records: hInstance =
 * GetModuleHandleA(NULL) through the dispatch, lpCmdLine = the command tail
 * (es2_rtl_command_tail), WinMain(hInstance, 0, lpCmdLine, 10). */
es2_sim_startup_outcome es2_sim_startup_run(es2_sim_startup *s);

/* Pieces, for tests and the in-vitro comparison. Each returns 0, or 1 when
 * a call was refused (s->outcome set). */
int es2_sim_startup_split(es2_sim_startup *s, uint32_t command, uint32_t argv, uint32_t *argc);   /* 0x4652f8 */
int es2_sim_startup_early_parser(es2_sim_startup *s, uint32_t argc, uint32_t argv);                /* 0x45e6cc */
int es2_sim_startup_main_parser(es2_sim_startup *s, uint32_t argc, uint32_t argv);                 /* 0x45e758 */
int es2_sim_startup_register_volumes(es2_sim_startup *s, uint32_t mask, uint32_t name, uint32_t root,
                                     uint32_t alternate_first);                                    /* 0x473260 */
/* 0x4539a0(holder, array, n, out2) with 0x453918: the strings of the
 * holder's current group into array (and the pointers past them into out2);
 * n is not read by the original; a string slot at or above the guest
 * address `limit` is refused (the original writes on into its frame). */
int es2_sim_startup_read_group(es2_sim_startup *s, uint32_t holder, uint32_t array, uint32_t limit, uint32_t out2);

#endif
