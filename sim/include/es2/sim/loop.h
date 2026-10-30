/* DBSIM's main loop, pump, window procedure, limiter and clock, the mission
 * end's caller and the process exit as own code (main_loop.md, "the main loop,
 * pump, window procedure, pause, limiter and clock" and "the main loop, pump,
 * window procedure, limiter and clock as own code", launcher.md,
 * "DBSIM's process shell, start-up and exit"): the game main's part after the
 * mission load (0x45f430..0x45f568: stage 3, the arrow cursor, the command
 * gate, the loop with stage_dispatch, the limiter, the pause path, the frame
 * counter; the loop tail with the prefs write, the tape deletes and the mission
 * end), the limiter 0x4678c0, the clock 0x467828 and the timing init 0x467840
 * under virtual time (own_process.md section 7), the pump 0x464c78 with the
 * input journal as its message source (section 8), the window procedure
 * 0x466034 with its handler list 0x4d3b30 (add 0x465fec, remove 0x466010),
 * pause and resume 0x45f0d4/0x45f108, the abort-notify registration 0x479cfc,
 * CW3220's atexit list and exit (atexit functions, exit records, the stream
 * exit hooks), and the frame-buffer objects of the DIB section 0x464d44
 * headless (src/frame_buffer.c).
 *
 * Guest state is read and written only through the guest view; every call
 * of other DBSIM code goes through es2_sim_call (a hook, a service, or a
 * named refusal). Headless the platform has no window, no message queue
 * besides the journal and the messages the own code posts, and no clock
 * besides virtual time. */
#ifndef ES2_SIM_LOOP_H
#define ES2_SIM_LOOP_H
#include <stdint.h>
#include "es2/core/guest.h"
#include "es2/sim/dispatch.h"
#include "sim/oracle/journal.h"
#include "sim/oracle/virtual_time.h"
#include "es2/sim/process_platform.h"

/* Guest VAs (DBSIM.EXE). */
#define ES2_SIM_VA_FRAME_COUNTER 0x004d257bu   /* G+0xbf */
#define ES2_SIM_VA_HANDLERS 0x004d3b30u        /* the window procedure's handler list (10 dwords) */
#define ES2_SIM_VA_NOTIFY_HEAD 0x006c56d8u     /* the abort-notify list's head */
#define ES2_SIM_VA_PUMP 0x00464c78u
#define ES2_SIM_VA_CLOCK 0x00467828u
#define ES2_SIM_VA_WINDOW_PROC 0x00466034u
#define ES2_SIM_VA_SURFACE_RELEASE 0x00480f38u /* the atexit function the DIB section registers */
#define ES2_SIM_VA_SURFACE_NOTIFY 0x00480f30u  /* its abort-notify function (an empty body) */

/* Windows message numbers the own code handles (winuser.h). */
#define ES2_WM_DESTROY 0x0002u
#define ES2_WM_SETFOCUS 0x0007u
#define ES2_WM_KILLFOCUS 0x0008u
#define ES2_WM_PAINT 0x000fu
#define ES2_WM_CLOSE 0x0010u
#define ES2_WM_QUERYENDSESSION 0x0011u
#define ES2_WM_QUIT 0x0012u
#define ES2_WM_ENDSESSION 0x0016u
#define ES2_WM_CHAR 0x0102u
#define ES2_WM_DEADCHAR 0x0103u
#define ES2_WM_SYSCHAR 0x0106u
#define ES2_WM_SYSDEADCHAR 0x0107u
/* The flags of the platform's translate answer. */
#define ES2_SIM_TRANSLATE_ONE 0x01000000u
#define ES2_SIM_TRANSLATE_TWO 0x02000000u
#define ES2_SIM_TRANSLATE_DEAD 0x80000000u

/* The depth of the posted queue: Windows' per-thread limit of posted messages,
 * USERPostMessageLimit, whose default is 10000 and which a post beyond fails
 * with ERROR_NOT_ENOUGH_QUOTA; measured 10000 on Windows 10 build 19045 for
 * PostThreadMessageA and PostMessageA (main_loop.md, "the posted queue's
 * depth"). */
#define ES2_SIM_POSTED_DEPTH 10000u

typedef enum es2_sim_loop_outcome {
    ES2_SIM_LOOP_RETURNED = 0,   /* the game main returned (after the loop tail) */
    ES2_SIM_LOOP_STOPPED,        /* the tick callback asked to stop (frame_limit) */
    ES2_SIM_LOOP_REFUSED,        /* a call was refused (logged through the dispatch) */
    ES2_SIM_LOOP_EXITED          /* __exit, _abort or a fault through `process` returned (a test's recorder); exit_code */
} es2_sim_loop_outcome;

/* The headless platform: stand-in handles, the DIB bits region and the
 * record of the platform calls (counts). */
typedef struct es2_sim_loop_platform {
    uint32_t dc_window, dc_memory, dib, old_bitmap, cursor_arrow;
    uint32_t bits, bits_size;              /* guest region for the DIB bits (at least DIB w * h bytes) */
    /* TranslateMessage of a key-down: 0 for none; else the first character in
     * bits 0..7 and, with ES2_SIM_TRANSLATE_TWO, the second in bits 8..15;
     * ES2_SIM_TRANSLATE_DEAD: a dead key (one WM_DEADCHAR or WM_SYSDEADCHAR); a
     * nonzero value without flag bits is one character (ES2_SIM_TRANSLATE_ONE
     * marks a character 0). NULL: no key produces one. es2sim passes the host's
     * ToAscii with an all-up key state, whose dead-key state is the thread's,
     * as TranslateMessage's (main_loop.md, "the shell leftovers as own rows"). */
    uint32_t (*translate)(void *user, uint32_t vk, uint32_t scan);
    void *translate_user;
    unsigned get_dc, create_dc, palette_entries, release_dc, create_dib, select_object, delete_object, message_box;
    unsigned peeks, translates, dispatches, set_cursor, def_window_proc, sleeps, open_file, post_quit, load_cursor;
    unsigned menu_keys;                    /* headless: WM_SYSKEYUP of F10 or Alt through DefWindowProcA */
    uint32_t last_cursor;
    char deleted[2][64];                   /* the tape-mode OpenFile(OF_DELETE) names */
} es2_sim_loop_platform;

typedef struct es2_sim_loop es2_sim_loop;

/* A windowed platform below the pump, the window procedure, the pause path and
 * the limiter (own_process.md, "the windowed platform layer"). NULL (the
 * default, every headless run): none of these is called and the loop is the
 * headless one. */
typedef struct es2_sim_loop_window {
    /* After the journal's and the posted messages, before WM_QUIT: 1 and a
     * queued message (the hwnd is the main window's), 0 when the queue is
     * empty, -1 when a sent message the platform dispatched inside the call
     * (es2_sim_window_proc) was refused (outcome set). */
    int (*peek)(void *user, es2_sim_loop *l, uint32_t *message, uint32_t *wparam, uint32_t *lparam);
    /* The pump's SetCursor([0x4d3720]) with a nonzero handle. */
    void (*set_cursor)(void *user, uint32_t handle);
    /* The pause path's Sleep(100). */
    void (*sleep)(void *user, uint32_t ms);
    /* Before each limiter poll: may move the virtual clock forward (never
     * back); then the poll's pace (es2_vt_limiter_poll). */
    void (*clock)(void *user, es2_vt *vt);
    es2_vt_pace pace;
    /* DefWindowProcA of the window procedure's default case (counted by the
     * loop): 0 and *result, or 1 when refused. It may send a message
     * through es2_sim_window_proc (Alt+F4's WM_CLOSE). */
    int (*def_window_proc)(void *user, es2_sim_loop *l, uint32_t message, uint32_t wparam, uint32_t lparam, uint32_t *result);
    void *user;
} es2_sim_loop_window;

struct es2_sim_loop {
    const es2_guest_memory *memory;
    es2_sim_dispatch *dispatch;
    es2_vt *vt;                            /* virtual time (the clock, the limiter, the timing init) */
    es2_journal *journal;                  /* the message source in replay; NULL: none */
    es2_sim_loop_platform platform;
    /* Guest bytes for the slots of own calls: [stack_lo, stack_hi), grows down. */
    uint32_t stack_lo, stack_hi;
    /* At the frame-counter increment 0x45f4ad, before it (the tick record,
     * the boundary of virtual time is called by the loop first). Nonzero:
     * stop after the increment (frame_limit). NULL: no record. */
    int (*tick)(es2_sim_loop *loop, void *user);
    void *tick_user;
    /* The OpenFile(OF_DELETE) of the loop tail (tape playback). NULL: refused. */
    int (*delete_file)(void *user, const char *name);
    void *delete_user;
    /* Messages the own code posted (TranslateMessage's characters, the SOS
     * notices; PostQuitMessage is the flag below): retrieved after the
     * journal's, WM_QUIT last. A ring of ES2_SIM_POSTED_DEPTH entries, the
     * oldest at `posted_head` (FIFO), `posted_count` entries in use
     * (main_loop.md, "the posted queue's depth"). */
    struct { uint32_t message, wparam, lparam; } posted[ES2_SIM_POSTED_DEPTH];
    unsigned posted_head, posted_count;
    int quit_posted;
    /* CW3220's atexit table (host state: CW3220's variables are not compared). */
    uint32_t atexit_table[32], atexit_count;
    /* Out. */
    es2_sim_loop_outcome outcome;
    uint32_t refused_va;
    uint64_t passes, ticks;
    /* Called right before every call of the logic frame 0x45f570 (the proxy's
     * [snapshot] after_load step sits at its first call, 0x45f487). NULL: none.
     * Its result is not read. */
    int (*before_logic)(es2_sim_loop *loop, void *user);
    void *before_logic_user;
    /* The windowed platform; NULL headless. */
    const es2_sim_loop_window *window;
    /* MessageBoxA, __exit and _abort of the error report 0x479c78
     * (es2/sim/process_platform.h); NULL headless. exit_code: the status of
     * ES2_SIM_LOOP_EXITED. */
    const es2_sim_process_platform *process;
    uint32_t exit_code;
    /* The message instrument (main_loop.md, "the windowed notices measured, and
     * the windowed replay"). Called after every PM_REMOVE peek of the pump with
     * the peek's source (ES2_SIM_PEEK_*) and, unless empty, the message it
     * returned; it only records. NULL (the default): none. */
    void (*peek_note)(void *user, const es2_sim_loop *l, int source, uint32_t hwnd, uint32_t message, uint32_t wparam,
                      uint32_t lparam);
    void *peek_note_user;
};
/* The sources of a peek (peek_note). */
#define ES2_SIM_PEEK_EMPTY 0      /* nothing: the pump's pass ends */
#define ES2_SIM_PEEK_JOURNAL 1    /* a journal entry (the proxy trace's `inject`) */
#define ES2_SIM_PEEK_POSTED 2     /* the own posted queue (TranslateMessage's characters, the SOS notices) */
#define ES2_SIM_PEEK_WINDOW 3     /* the windowed platform's queue (input, paint, MM_MCINOTIFY) */
#define ES2_SIM_PEEK_QUIT 4       /* WM_QUIT */

/* Zeroes the record and sets the headless stand-ins. */
void es2_sim_loop_defaults(es2_sim_loop *l);
/* The instance the service rows use (one per process). */
void es2_sim_loop_bind(es2_sim_loop *l);
es2_sim_loop *es2_sim_loop_bound(void);

/* D: the original's unhandled CPU fault with exception `code` at the guest
 * VA `site` (process_platform.h, process_fault). With a record that has
 * process_fault the process ends there (a test's recorder returns: outcome
 * ES2_SIM_LOOP_EXITED, exit_code `code`) and 1 is returned; the caller
 * then fails without a refusal of its own. Without one: 0, and the caller
 * refuses as before. */
int es2_sim_loop_fault(es2_sim_loop *l, uint32_t code, uint32_t site);
/* The same for the original's divide fault at `site`: IDIV (is_signed) or
 * DIV of EDX:EAX = dividend by the divisor, the code the OS raises for
 * those operands (es2/sim/cpu_fault.h). 0 also when they do not fault. */
int es2_sim_loop_divide_fault(es2_sim_loop *l, uint32_t site, uint64_t dividend, uint32_t divisor, int is_signed);
/* An own row's divide that faults in the original at `va` (the divide
 * instruction, or the call that reaches it): es2_sim_loop_divide_fault,
 * and without a record the refusal (es2_sim_loop_refuse) as before D. A
 * zero divisor faults whatever the dividend. Returns 1. */
int es2_sim_loop_divide_refuse(es2_sim_loop *l, uint32_t va, uint64_t dividend, uint32_t divisor, int is_signed, const char *name,
                               const char *reason);
/* An own row whose guest pointer is NULL where the original reads, writes
 * or calls through it below 0x10000 (the instruction at `va`, or the call
 * that reaches it): es2_sim_loop_fault with ES2_SIM_ACCESS_VIOLATION, and
 * without a record the refusal (es2_sim_loop_refuse). Returns 1. */
int es2_sim_loop_null_refuse(es2_sim_loop *l, uint32_t va, const char *name, const char *reason);

/* The game main from 0x45f430 to its return 0x45f568, with the game main's
 * frame at `ebp` (the error.str holder at ebp-0x10, the OpenFile buffer at
 * ebp-0x1c4). Sets and returns l->outcome. */
es2_sim_loop_outcome es2_sim_main_loop(es2_sim_loop *l, uint32_t ebp);

/* The pieces, each 0 or 1 when refused (l->outcome, l->refused_va set). */
int es2_sim_limiter(es2_sim_loop *l);                                   /* 0x4678c0 */
uint32_t es2_sim_clock(es2_sim_loop *l);                                /* 0x467828 */
uint32_t es2_sim_timing_init(es2_sim_loop *l);                          /* 0x467840: returns the clock base */
int es2_sim_pump(es2_sim_loop *l, uint32_t *result);                    /* 0x464c78: 1 when it removed WM_QUIT */
int es2_sim_window_proc(es2_sim_loop *l, uint32_t hwnd, uint32_t message, uint32_t wparam, uint32_t lparam,
                        uint32_t *result);                              /* 0x466034 */
int es2_sim_pause(es2_sim_loop *l);                                     /* 0x45f0d4 */
int es2_sim_resume(es2_sim_loop *l);                                    /* 0x45f108 */
void es2_sim_post_quit(es2_sim_loop *l);                                /* PostQuitMessage(0) */
/* PostMessageA to the main window from the platform (the SOS voice notices
 * 0xa000): appended to the `posted` ring, retrieved after the journal's
 * messages and before the window's queue. 0 queued, 1 the queue is full
 * (ES2_SIM_POSTED_DEPTH entries; the message is dropped, as a failed
 * PostMessageA is). `user` is the loop (an es2_sim_sos_mixer post). */
int es2_sim_loop_post(void *user, uint32_t window, uint32_t message, uint32_t wparam, uint32_t lparam);
/* 0x479cfc(fn): a node {head, fn} from operator new pushed at the head. */
int es2_sim_abort_notify_add(es2_sim_loop *l, uint32_t function, uint32_t *node);
/* _atexit(fn): 0, or 1 when 32 are registered. */
uint32_t es2_sim_atexit(es2_sim_loop *l, uint32_t function);

/* CW3220's exit(status) after WinMain returned: the atexit functions last
 * registered first (the count is reread after each), then `exit_records`
 * (the 26 exit records), then the stream exit hooks of the file model (the
 * buffers' and the open files', each only when installed). 0, or 1 when a
 * call was refused. The caller then ends the process with `status`. */
int es2_sim_exit(es2_sim_loop *l, int (*exit_records)(void *user), void *user);

/* frame_buffer.c: the DIB section 0x464d44 headless (the GDI calls answered
 * by the platform record; the graphics library's surface objects built as
 * the original builds them) and the surface release 0x480f38. */
int es2_sim_dib_section(es2_sim_loop *l);
int es2_sim_surface_release(es2_sim_loop *l);

/* Own calls of other DBSIM code: the slots are copied to the guest stack
 * area; regs = {EAX, EDX, ECX} at the call (NULL: zero). 0 and *out, or 1
 * when refused (outcome and refused_va set). */
int es2_sim_loop_call(es2_sim_loop *l, uint32_t va, const uint32_t *slots, unsigned count, es2_sim_result *out);
/* A refusal of the own code's (no dispatch entry involved). Returns 1. */
int es2_sim_loop_refuse(es2_sim_loop *l, uint32_t va, const char *name, const char *reason);

/* frame_buffer.c's graphics-library surface pieces for other own callers (each
 * 0, or 1 when refused): 0x480f58, 0x481028, 0x481224, 0x4811e8, the shared
 * finish after an attach, and 0x4813f8's field cases. */
int es2_sim_gl_surface_construct(es2_sim_loop *l, uint32_t obj, uint32_t name);
int es2_sim_gl_surface_create(es2_sim_loop *l, uint32_t s, uint32_t desc, uint32_t *attached);
int es2_sim_gl_surface_select(es2_sim_loop *l, uint32_t s);
int es2_sim_gl_surface_destroy(es2_sim_loop *l, uint32_t s);
int es2_sim_gl_surface_finish(es2_sim_loop *l, uint32_t s);
int es2_sim_gl_surface_set(es2_sim_loop *l, uint32_t s, uint32_t which, uint32_t value, uint32_t *result);

/* The message instrument (main_loop.md, "the windowed notices measured, and the
 * windowed replay"): the counts of the SOS voice notices (0xa000) posted,
 * refused (the mixer's posts_lost) and retrieved, per frame and per modal pass
 * (a pump call after the frame's first; the frame counter stands), and, with a
 * writer, the per-frame trace in the format of the proxy's message trace
 * (main_loop.md, "the message trace of the replay pump"): the same start,
 * msg/inject and frame records, so the same script reads both. It only records;
 * without es2_sim_message_trace_attach nothing of it runs. */
#define ES2_SIM_NOTICE_MESSAGE 0xa000u
#define ES2_SIM_TRACE_POST_TIMES ES2_SIM_POSTED_DEPTH   /* as many times as the queue holds notices */
typedef struct es2_sim_message_trace {
    /* The trace's bytes (NULL: counters only); 0 written, nonzero failed
     * (the trace then stops writing, the counters go on). */
    int (*write)(void *user, const char *bytes, unsigned length);
    void *write_user;
    /* MSG.time's clock (GetTickCount in es2sim); NULL: 0. */
    uint32_t (*now)(void *user);
    void *now_user;
    es2_sim_loop *loop;
    char buffer[65536];
    unsigned used;
    int failed, started, closed;
    /* The current frame. */
    uint32_t frame;
    unsigned peeks, empty, msgs, inject, quits, frame_notices;
    /* The accepted notices' post times, oldest first (their order in the
     * posted queue). */
    uint32_t post_time[ES2_SIM_TRACE_POST_TIMES];
    unsigned post_head, post_count;
    /* The counts. */
    uint64_t posted, refused, retrieved, retrieved_first, retrieved_modal, modal_passes, frames_modal, frames;
    uint32_t most_in_frame, most_frame;
} es2_sim_message_trace;
/* Zeroes the record and puts it behind l->peek_note; with `write`, the
 * start record. */
void es2_sim_message_trace_attach(es2_sim_message_trace *t, es2_sim_loop *l,
                                  int (*write)(void *user, const char *bytes, unsigned length), void *write_user);
/* The mixer's post (es2_sim_sos_mixer.post, `user` the trace): counts the
 * notice and its time, then es2_sim_loop_post on the trace's loop. */
int es2_sim_message_trace_post(void *user, uint32_t window, uint32_t message, uint32_t wparam, uint32_t lparam);
/* The last frame record and the end record, then the buffer's rest (once). */
void es2_sim_message_trace_close(es2_sim_message_trace *t);

#endif
