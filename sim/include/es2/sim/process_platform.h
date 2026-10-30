/* The process's platform calls on DBSIM's error paths (own_process.md, "the
 * error paths' MessageBoxA, PostQuitMessage, __exit and _abort windowed"):
 * MessageBoxA and the process ends __exit 0x49679c and _abort 0x49688c.
 *
 * The start-up (es2_sim_startup.process) and the main loop
 * (es2_sim_loop.process) take a pointer to one record. NULL, every headless
 * run: no box is shown and MessageBoxA answers IDOK (counted), __exit and
 * _abort are called through the dispatch, which has no row for them (the
 * run stops there, refused). The windowed es2sim sets a
 * record whose message_box is Win32's MessageBoxA with the original's
 * arguments and whose process_exit ends the process with the original's
 * exit code; tests set a recorder. es2sim sets a record in
 * headless runs too, without message_box (no box, IDOK) but with
 * process_exit and process_fault, so the error paths end the process as
 * the original's instead of refusing; a NULL record (the library's
 * default, most tests) keeps the refusals. */
#ifndef ES2_SIM_PROCESS_PLATFORM_H
#define ES2_SIM_PROCESS_PLATFORM_H
#include <stdint.h>

/* MessageBoxA's answer the headless platform gives (IDOK). */
#define ES2_SIM_IDOK 1u
/* _abort (CW3220 0x419060, rtl_imports.md "abort, __ErrorExit,
 * __ErrorMessage"): __ErrorMessage's box with CW3220's text, titled with
 * the module's file name after its last '\' or ':', style MB_ICONHAND |
 * MB_SETFOREGROUND (0x10010) | the modality, then __exit(3). The modality
 * is MB_TASKMODAL (0x2000) on every reachable call: CW3220 takes
 * MB_SYSTEMMODAL only when GetVersion() has bit 31 set and the thread has no
 * window, and the main window exists from WinMain's 0x465158 on, before any
 * code that can reach _abort. */
#define ES2_SIM_ABORT_TEXT "Abnormal program termination"
#define ES2_SIM_ABORT_STYLE 0x00012010u
#define ES2_SIM_ABORT_STATUS 3u
/* __exit's status on the fatal report 0x479c78 (push 1 at 0x479cd9). */
#define ES2_SIM_FATAL_STATUS 1u
/* STATUS_ACCESS_VIOLATION: the original's read, write or call through a
 * NULL pointer (own_process.md "NULL accesses"). */
#define ES2_SIM_ACCESS_VIOLATION 0xc0000005u

typedef struct es2_sim_process_platform {
    /* MessageBoxA(owner, text, caption, style) with the guest's bytes (ANSI,
     * NUL-terminated); returns the box's answer. NULL: IDOK. */
    uint32_t (*message_box)(void *user, uint32_t owner, const char *text, const char *caption, uint32_t style);
    /* __exit(status): ends the process (ExitProcess(status) after the own
     * process's log is finished; no atexit functions, no exit records, no
     * stream flush, as CW3220's __exit). Does not return in es2sim; a test's
     * recorder returns, and the caller then ends its run with the status
     * (es2_sim_loop: ES2_SIM_LOOP_EXITED; es2_sim_startup:
     * ES2_SIM_STARTUP_EXIT). NULL: the dispatch call as headless. */
    void (*process_exit)(void *user, uint32_t status);
    /* GetModuleFileNameA(NULL)'s file name for _abort's title (the user's
     * DBSIM.EXE). NULL: "DBSIM.EXE". */
    const char *module_name;
    void *user;
    /* D (own_process.md "The original's fault paths"): an unhandled CPU
     * fault of the original at the guest VA `site` (es2_sim_loop_fault).
     * DBSIM has no handler for it: its frame records' __ExceptionHandler
     * continues the search, CW3220's __startup record passes it to
     * UnhandledExceptionFilter, and the OS ends the process with the
     * exception code as its exit code (no atexit functions, no exit
     * records, no stream flush). Ends the process with `code`; does not
     * return in es2sim; a test's recorder returns (es2_sim_loop:
     * ES2_SIM_LOOP_EXITED, exit_code the code). NULL: the fault is refused
     * where it happens, as before D. */
    void (*process_fault)(void *user, uint32_t code, uint32_t site);
} es2_sim_process_platform;

/* MessageBoxA through the record: its answer, IDOK without one. */
uint32_t es2_sim_process_message_box(const es2_sim_process_platform *p, uint32_t owner, const char *text, const char *caption,
                                     uint32_t style);
/* _abort with a record that has process_exit: the box, then __exit(3).
 * Returns (only under a recorder) the status. */
uint32_t es2_sim_process_abort(const es2_sim_process_platform *p);

#endif
