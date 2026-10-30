/* The main loop, the pump, the window procedure, the limiter and the clock, the
 * loop tail and the process exit as own code (es2/sim/loop.h). Contract:
 * main_loop.md, "the main loop, pump, window procedure, pause, limiter and
 * clock" and "the main loop, pump, window procedure, limiter and clock as own
 * code", launcher.md, "DBSIM's process shell, start-up and exit"
 * (every store and call below with its listing address range); "listing" =
 * llvm-objdump -d -M intel of the reference DBSIM.EXE (SHA-256
 * 8561aa47...c4ad). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "es2/sim/loop.h"
#include "es2/sim/cpu_fault.h"
#include "es2/sim/files.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"
#include "es2/sim/present.h"

/* ---- guest VAs */
#define VA_COMMAND_GATE 0x004d2564u    /* G+0xa8: word, the command handler's gate */
#define VA_PLAYBACK 0x004d24d6u        /* G+0x1a: word, tape playback (no limiter) */
#define VA_PAUSE 0x004d2572u           /* G+0xb6: byte */
#define VA_STOP 0x004d2532u            /* G+0x76: word (demo stop, a pump quit in control_record) */
#define VA_QUIT 0x004d24feu            /* G+0x42: word (the quit dialog's second button) */
#define VA_CAMERA 0x004d24eau          /* G+0x2e */
#define VA_DEMO 0x004d2530u            /* word */
#define VA_DIRECTDRAW 0x004d255eu      /* G+0xa2: dword */
#define VA_PREFS_BYTE6 0x004d1f3eu
#define VA_SOUND 0x0049f8ecu
#define VA_TIMER 0x004d3608u
#define VA_CURSOR 0x004d3720u
#define VA_HWND 0x004d3610u
#define VA_CLOSE_RESULT 0x004d370cu    /* word: the command handler's answer to WM_CLOSE */
#define VA_COCKPIT 0x0049b088u
#define VA_CLOCK_BASE 0x004d3b78u
#define VA_LIMITER_PREVIOUS 0x004d3b74u
#define VA_LIMITER_SCRATCH 0x004d3b6cu
#define VA_SCALE_A 0x004d3b64u
#define VA_SCALE_B 0x004d3b66u
#define VA_REFERENCE_A 0x004d3b68u
#define VA_REFERENCE_B 0x004d3b6au
#define VA_TIMING_BASE 0x004d3b70u
#define VA_TIMING_SHIFTED 0x004d3b72u
#define VA_TAPE_PREFS 0x0049f4d2u      /* "tapes\prefs.cfg" (EDI 0x49f287 + 0x24b) */
#define VA_TAPE_KEYJOY 0x0049f4e2u     /* "tapes\keyjoy.cfg" (EDI + 0x25b) */
/* Callees. */
#define VA_STAGE_DISPATCH 0x00401d94u
#define VA_LOGIC_FRAME 0x0045f570u
#define VA_CAMERA_UPDATE 0x004011a0u
#define VA_RENDER_ROOT 0x0045fca8u
#define VA_PRESENT_FRAME 0x0045fba4u
#define VA_SOUND_DRIVER 0x00464874u
#define VA_SOUND_RELEASE 0x00462734u
#define VA_FRAME_SHOW 0x0046548cu
#define VA_PREFS_SET 0x00459958u
#define VA_PREFS_WRITE 0x00459b80u
#define VA_MISSION_END 0x00461ff0u
#define VA_HOLDER_FREE 0x0047758cu     /* delete[] (0x453848's) */
#define VA_TIME_SCALE 0x00467894u
#define VA_COMMAND_KEY 0x0045feb8u
#define VA_SOUND_PAUSE 0x0046317cu
#define VA_SOUND_RESUME 0x00463238u
#define VA_PORT_A 0x00433158u
#define VA_PORT_PAUSE 0x00435b58u
#define VA_PORT_RESUME 0x00435b80u
#define VA_PRESENT 0x00464a14u
#define VA_NEW 0x0047749cu

static es2_sim_loop *bound_loop;
void es2_sim_loop_bind(es2_sim_loop *l) { bound_loop = l; }
es2_sim_loop *es2_sim_loop_bound(void) { return bound_loop; }

static uint32_t r32(const es2_sim_loop *l, uint32_t a) { return l->memory->read32(l->memory->context, a); }
static uint16_t r16(const es2_sim_loop *l, uint32_t a) { return l->memory->read16(l->memory->context, a); }
static uint8_t r8(const es2_sim_loop *l, uint32_t a) { return es2_sim_read8(l->memory, a); }
static void w32(const es2_sim_loop *l, uint32_t a, uint32_t v) { l->memory->write32(l->memory->context, a, v); }
static void w16(const es2_sim_loop *l, uint32_t a, uint16_t v) { l->memory->write16(l->memory->context, a, v); }
static void w8(const es2_sim_loop *l, uint32_t a, uint8_t v) { es2_sim_write8(l->memory, a, v); }

void es2_sim_loop_defaults(es2_sim_loop *l) {
    memset(l, 0, sizeof(*l));
    l->platform.dc_window = 0x00010011u;
    l->platform.dc_memory = 0x00010013u;
    l->platform.dib = 0x00010015u;
    l->platform.old_bitmap = 0x00010017u;
    l->platform.cursor_arrow = 0x00010005u;   /* the start-up's stand-in (es2_sim_startup_defaults) */
}

/* ---- own calls */
int es2_sim_loop_call(es2_sim_loop *l, uint32_t va, const uint32_t *slots, unsigned count, es2_sim_result *out) {
    uint32_t bytes = 4u * (count ? count : 1u), top = l->stack_hi;
    if (top - l->stack_lo < bytes) return es2_sim_loop_refuse(l, va, NULL, "the own code's call area is exhausted");
    l->stack_hi -= bytes;
    for (unsigned i = 0; i < count; ++i) w32(l, l->stack_hi + 4u * i, slots[i]);
    uint32_t regs[3] = {0, 0, 0};
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    int refused = es2_sim_call(l->dispatch, va, l->memory->host(l->memory->context, l->stack_hi), count, regs, &r);
    l->stack_hi = top;
    if (refused) {
        if (l->outcome == ES2_SIM_LOOP_EXITED) return 1;   /* the process ended inside */
        l->outcome = ES2_SIM_LOOP_REFUSED;
        l->refused_va = va;
        return 1;
    }
    if (out) *out = r;
    return 0;
}
static int call(es2_sim_loop *l, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    es2_sim_result r;
    if (es2_sim_loop_call(l, va, slots, count, &r)) return 1;
    if (eax) *eax = r.eax;
    return 0;
}
int es2_sim_loop_refuse(es2_sim_loop *l, uint32_t va, const char *name, const char *reason) {
    es2_sim_refusal refusal = {va, name, reason, 0, 0};
    ++l->dispatch->refusals;
    if (l->dispatch->refuse) l->dispatch->refuse(&refusal, l->dispatch->refuse_user);
    l->outcome = ES2_SIM_LOOP_REFUSED;
    l->refused_va = va;
    return 1;
}

/* ---- virtual time: the clock 0x467828, the timing init 0x467840 and the
 * limiter 0x4678c0 (own_process.md section 7: the three GetTickCount sites
 * name their caller; the limiter's call is the poll). */
uint32_t es2_sim_clock(es2_sim_loop *l) {
    /* call 0x4968ec; sub eax, [0x4d3b78]; shr eax, 4 (logical). */
    return (es2_vt_read(l->vt) - r32(l, VA_CLOCK_BASE)) >> 4;
}

uint32_t es2_sim_timing_init(es2_sim_loop *l) {
    /* 0x467840..0x467892: the scales 256, the base 1000, 1000 / 8 (sar with
     * the sign bias: 125) into the shifted base, both references and the
     * scratch dword; then one read into the clock base and the previous
     * limiter time. */
    w16(l, VA_SCALE_A, 0x100u);
    w16(l, VA_SCALE_B, 0x100u);
    w16(l, VA_TIMING_BASE, 1000u);
    int32_t base = (int16_t)1000;
    uint16_t eighth = (uint16_t)((base < 0 ? base + 7 : base) >> 3);
    w16(l, VA_TIMING_SHIFTED, eighth);
    w16(l, VA_REFERENCE_A, eighth);
    w16(l, VA_REFERENCE_B, eighth);
    w32(l, VA_LIMITER_SCRATCH, (uint32_t)(int32_t)(int16_t)eighth);
    uint32_t now = es2_vt_read(l->vt);
    w32(l, VA_CLOCK_BASE, now);
    w32(l, VA_LIMITER_PREVIOUS, now);
    return now;
}

int es2_sim_limiter(es2_sim_loop *l) {
    /* 0x4678c0..0x46791f: T = previous + 40; poll until T <= value (signed). */
    uint32_t threshold = r32(l, VA_LIMITER_PREVIOUS) + 0x28u, value;
    do {
        /* Windowed, the clock follows the wall clock and the poll waits for it
         * (own_process.md, "the windowed platform layer"). */
        if (l->window && l->window->clock) l->window->clock(l->window->user, l->vt);
        value = es2_vt_limiter_poll(l->vt, r32(l, VA_LIMITER_PREVIOUS), l->window ? l->window->pace : NULL,
                                    l->window ? l->window->user : NULL);
        w32(l, VA_LIMITER_SCRATCH, value);
    } while ((int32_t)threshold > (int32_t)value);
    uint32_t elapsed = r32(l, VA_LIMITER_SCRATCH) - r32(l, VA_LIMITER_PREVIOUS);
    w32(l, VA_LIMITER_SCRATCH, elapsed);
    /* time_scale (hook 0x467894) with the reference word and the elapsed
     * word; it reads only the low words (movsx). The slots' high words are
     * the original's: EAX's from the last poll value, ECX's from the loop's
     * `pop ecx` (0), then the first call's ECX and EDX. */
    es2_sim_result r;
    uint32_t a[2] = {(value & 0xffff0000u) | r16(l, VA_REFERENCE_A), r16(l, VA_LIMITER_SCRATCH)};
    if (es2_sim_loop_call(l, VA_TIME_SCALE, a, 2, &r)) return 1;
    w16(l, VA_SCALE_A, (uint16_t)r.eax);
    uint32_t b[2] = {(r.ecx & 0xffff0000u) | r16(l, VA_REFERENCE_B), (r.edx & 0xffff0000u) | r16(l, VA_LIMITER_SCRATCH)};
    if (es2_sim_loop_call(l, VA_TIME_SCALE, b, 2, &r)) return 1;
    w16(l, VA_SCALE_B, (uint16_t)r.eax);
    w32(l, VA_LIMITER_PREVIOUS, r32(l, VA_LIMITER_PREVIOUS) + r32(l, VA_LIMITER_SCRATCH));
    return 0;
}

/* ---- the message queue: the journal first (the proxy injects before the
 * real peek), then the messages the own code posted, WM_QUIT last. */
typedef struct message { uint32_t hwnd, message, wparam, lparam; } message;

static int peek_from(es2_sim_loop *l, message *m, int *source) {
    ++l->platform.peeks;
    uint32_t window = r32(l, VA_HWND);
    if (l->journal) {
        uint32_t frame = r32(l, ES2_SIM_VA_FRAME_COUNTER);
        es2_journal_count_pump(l->journal, frame);
        es2_journal_entry *e = es2_journal_take(l->journal, ES2_JOURNAL_MSG, frame, NULL, 0);
        if (e) {
            if (!window) ++l->journal->no_window;
            m->hwnd = window;
            m->message = (uint32_t)e->a;
            m->wparam = (uint32_t)e->b;
            m->lparam = (uint32_t)e->c;
            es2_journal_apply_message(l->journal, m->message, m->wparam, m->lparam);
            ++l->journal->injected;
            *source = ES2_SIM_PEEK_JOURNAL;
            return 1;
        }
    }
    if (l->posted_count) {
        m->hwnd = window;
        m->message = l->posted[l->posted_head].message;
        m->wparam = l->posted[l->posted_head].wparam;
        m->lparam = l->posted[l->posted_head].lparam;
        l->posted_head = (l->posted_head + 1u) % ES2_SIM_POSTED_DEPTH;
        --l->posted_count;
        *source = ES2_SIM_PEEK_POSTED;
        return 1;
    }
    if (l->window && l->window->peek) {
        /* The window's queue (input and paint); sent messages were dispatched
         * inside. */
        int got = l->window->peek(l->window->user, l, &m->message, &m->wparam, &m->lparam);
        if (got < 0) return -1;
        if (got) { m->hwnd = r32(l, VA_HWND); *source = ES2_SIM_PEEK_WINDOW; return 1; }
    }
    if (l->quit_posted) {
        l->quit_posted = 0;
        m->hwnd = 0;
        m->message = ES2_WM_QUIT;
        m->wparam = 0;
        m->lparam = 0;
        *source = ES2_SIM_PEEK_QUIT;
        return 1;
    }
    return 0;
}
/* Every peek's source to the message instrument (NULL: none). */
static int peek(es2_sim_loop *l, message *m) {
    int source = ES2_SIM_PEEK_EMPTY;
    int got = peek_from(l, m, &source);
    if (got >= 0 && l->peek_note)
        l->peek_note(l->peek_note_user, l, got ? source : ES2_SIM_PEEK_EMPTY, got ? m->hwnd : 0u, got ? m->message : 0u,
                     got ? m->wparam : 0u, got ? m->lparam : 0u);
    return got;
}
/* Appends at the ring's tail; a full queue drops the message (a failed
 * PostMessageA) and returns 1. */
static int post(es2_sim_loop *l, uint32_t message, uint32_t wparam, uint32_t lparam) {
    if (l->posted_count >= ES2_SIM_POSTED_DEPTH) return 1;
    unsigned tail = (l->posted_head + l->posted_count) % ES2_SIM_POSTED_DEPTH;
    l->posted[tail].message = message;
    l->posted[tail].wparam = wparam;
    l->posted[tail].lparam = lparam;
    ++l->posted_count;
    return 0;
}
int es2_sim_loop_post(void *user, uint32_t window, uint32_t message, uint32_t wparam, uint32_t lparam) {
    es2_sim_loop *l = user;
    /* DBSIM gives the SOS driver the window of the driver init (audio.c
     * driver_init); the pump peeks for every window and dispatches with the
     * main window's handle. */
    (void)window;
    return post(l, message, wparam, lparam);
}
void es2_sim_post_quit(es2_sim_loop *l) {
    ++l->platform.post_quit;
    l->quit_posted = 1;
}
/* TranslateMessage: a key-down with a character posts WM_CHAR (WM_SYSCHAR for
 * WM_SYSKEYDOWN) with the key-down's lParam; a dead key one WM_DEADCHAR
 * (WM_SYSDEADCHAR); a key after a dead key that does not combine with it two
 * characters, the dead key's first (measured on the host). Each costs one
 * PM_REMOVE peek; no handler and no case of the window procedure reads them
 * (main_loop.md, "the shell leftovers as own rows"). */
static void translate(es2_sim_loop *l, const message *m) {
    ++l->platform.translates;
    if ((m->message != ES2_WM_KEYDOWN && m->message != ES2_WM_SYSKEYDOWN) || !l->platform.translate) return;
    uint32_t t = l->platform.translate(l->platform.translate_user, m->wparam, (m->lparam >> 16) & 0xffu);
    if (!t) return;
    int down = m->message == ES2_WM_KEYDOWN;
    if (t & ES2_SIM_TRANSLATE_DEAD) {
        post(l, down ? ES2_WM_DEADCHAR : ES2_WM_SYSDEADCHAR, t & 0xffu, m->lparam);
        return;
    }
    post(l, down ? ES2_WM_CHAR : ES2_WM_SYSCHAR, t & 0xffu, m->lparam);
    if (t & ES2_SIM_TRANSLATE_TWO) post(l, down ? ES2_WM_CHAR : ES2_WM_SYSCHAR, (t >> 8) & 0xffu, m->lparam);
}

/* ---- the pump 0x464c78 (listing 0x464c78..0x464cc0) */
int es2_sim_pump(es2_sim_loop *l, uint32_t *result) {
    message m;
    for (int got; (got = peek(l, &m)) != 0;) {
        if (got < 0) return 1;
        if (m.message == ES2_WM_QUIT) { *result = 1; return 0; }
        translate(l, &m);
        ++l->platform.dispatches;
        uint32_t ignored = 0;
        if (es2_sim_window_proc(l, m.hwnd, m.message, m.wparam, m.lparam, &ignored)) return 1;
    }
    uint32_t cursor = r32(l, VA_CURSOR);
    if (cursor) {
        ++l->platform.set_cursor;
        l->platform.last_cursor = cursor;
        if (l->window && l->window->set_cursor) l->window->set_cursor(l->window->user, cursor);
    }
    *result = 0;
    return 0;
}

/* ---- pause and resume 0x45f0d4 / 0x45f108 (listing 0x45f0d4..0x45f13b) */
static int ports(es2_sim_loop *l, uint32_t method) {
    if (!r32(l, VA_COCKPIT)) return 0;
    uint32_t port = 0;
    if (call(l, VA_PORT_A, NULL, 0, &port)) return 1;
    uint32_t a[1] = {port};
    if (call(l, method, a, 1, NULL)) return 1;
    a[0] = r32(l, r32(l, VA_COCKPIT) + 0x20bu);
    return call(l, method, a, 1, NULL);
}
int es2_sim_pause(es2_sim_loop *l) {
    w8(l, VA_PAUSE, 1);
    if (call(l, VA_SOUND_PAUSE, NULL, 0, NULL)) return 1;
    return ports(l, VA_PORT_PAUSE);
}
int es2_sim_resume(es2_sim_loop *l) {
    w8(l, VA_PAUSE, 0);
    if (call(l, VA_SOUND_RESUME, NULL, 0, NULL)) return 1;
    return ports(l, VA_PORT_RESUME);
}

/* ---- the window procedure 0x466034 (listing 0x466034..0x466586) */
static int def_window_proc(es2_sim_loop *l, uint32_t message, uint32_t wparam, uint32_t lparam, uint32_t *result) {
    if (l->window && l->window->def_window_proc) {
        /* The windowed platform's DefWindowProcA. */
        ++l->platform.def_window_proc;
        *result = 0;
        return l->window->def_window_proc(l->window->user, l, message, wparam, lparam, result);
    }
    (void)lparam;
    /* Headless DefWindowProcA has no effect on guest state for the messages a
     * replay delivers. The keyboard menu activation (WM_SYSKEYUP of F10 or Alt,
     * which sends WM_SYSCOMMAND(SC_KEYMENU)) is counted and has no effect
     * either: the recording's window was hidden and the golden of
     * c5_05-moon-missiles goes on after its three F10 presses (ticks
     * 8113..8134) with the frame counter running, so no modal menu loop held
     * the game's loop (own_process.md, "the headless acceptance"; a live check
     * of the visible window is open). */
    if (message == ES2_WM_SYSKEYUP && (wparam == 0x12u || wparam == 0x79u)) ++l->platform.menu_keys;
    ++l->platform.def_window_proc;
    *result = 0;
    return 0;
}
int es2_sim_window_proc(es2_sim_loop *l, uint32_t hwnd, uint32_t message, uint32_t wparam, uint32_t lparam,
                        uint32_t *result) {
    /* The handler list: every nonzero slot in slot order (stdcall, the four
     * arguments); the first nonzero answer ends the procedure with 0. */
    for (uint32_t i = 0; i < 10u; ++i) {
        uint32_t handler = r32(l, ES2_SIM_VA_HANDLERS + 4u * i);
        if (!handler) continue;
        uint32_t a[4] = {hwnd, message, wparam, lparam}, r = 0;
        if (call(l, handler, a, 4, &r)) return 1;
        if (r) { *result = 0; return 0; }
    }
    *result = 0;
    switch (message) {
    case ES2_WM_SETFOCUS: return es2_sim_resume(l);
    case ES2_WM_KILLFOCUS: return es2_sim_pause(l);
    case ES2_WM_SYSCHAR: return 0;
    case ES2_WM_CLOSE: {
        /* 0x466508: the demo word stops the loop; else the command handler's
         * quit dialog (0x45feb8, command 0x410, a hook: refused by name until
         * it is own code) and its answer kept at 0x4d370c. */
        if (r16(l, VA_DEMO)) { w16(l, VA_STOP, 1); return 0; }
        uint32_t a[1] = {0x410u}, answer = 0;
        if (call(l, VA_COMMAND_KEY, a, 1, &answer)) return 1;
        w16(l, VA_CLOSE_RESULT, (uint16_t)answer);
        return 0;
    }
    case ES2_WM_QUERYENDSESSION: {
        /* 0x466530: the quit dialog; the session may end when its answer
         * (sign-extended) is 2. */
        uint32_t a[1] = {0x410u}, answer = 0;
        if (call(l, VA_COMMAND_KEY, a, 1, &answer)) return 1;
        w16(l, VA_CLOSE_RESULT, (uint16_t)answer);
        *result = (int16_t)answer == 2 ? 1u : 0u;
        return 0;
    }
    case ES2_WM_DESTROY:
        /* 0x4650cc (ret); 0x4650d0 (KillTimer only with [0x4d3608]); PostQuitMessage(0). */
        if (r32(l, VA_TIMER)) return es2_sim_loop_refuse(l, 0x004650d9u, "KillTimer", "a timer at WM_DESTROY");
        es2_sim_post_quit(l);
        return 0;
    case ES2_WM_PAINT:
        if (r32(l, VA_DIRECTDRAW)) return def_window_proc(l, message, wparam, lparam, result);
        es2_sim_present_site(0x004662acu);   /* the call at 0x4662a7 */
        return call(l, VA_PRESENT, NULL, 0, NULL);   /* the present */
    case ES2_WM_ENDSESSION:
        if (wparam == 1u) return es2_sim_loop_refuse(l, 0x00466565u, "DestroyWindow", "WM_ENDSESSION is not reached headless");
        return 0;
    case 0x0024u: case 0x007eu: case 0x0218u: case 0x0219u: case 0x030fu: case 0x0311u: case 0x0812u:
        /* GETMINMAXINFO, DISPLAYCHANGE, POWERBROADCAST, DEVICECHANGE,
         * QUERYNEWPALETTE, PALETTECHANGED, the private 0x812: window and
         * display cases no replay reaches (input.c with the SDL3 window). */
        return es2_sim_loop_refuse(l, ES2_SIM_VA_WINDOW_PROC, "MainWndProc", "a window or display message the headless platform does not send");
    default: return def_window_proc(l, message, wparam, lparam, result);
    }
}

/* ---- the abort-notify list 0x479cfc and CW3220's atexit */
int es2_sim_abort_notify_add(es2_sim_loop *l, uint32_t function, uint32_t *node) {
    uint32_t a[1] = {8u}, n = 0;
    if (call(l, VA_NEW, a, 1, &n)) return 1;
    if (!n) return es2_sim_loop_refuse(l, 0x00479d0eu, "abort-notify node", "operator new returned NULL (the original prints and aborts)");
    w32(l, n, r32(l, ES2_SIM_VA_NOTIFY_HEAD));
    w32(l, n + 4u, function);
    w32(l, ES2_SIM_VA_NOTIFY_HEAD, n);
    if (node) *node = n;
    return 0;
}
uint32_t es2_sim_atexit(es2_sim_loop *l, uint32_t function) {
    if (l->atexit_count == 32u) return 1u;
    l->atexit_table[l->atexit_count++] = function;
    return 0;
}
int es2_sim_exit(es2_sim_loop *l, int (*exit_records)(void *user), void *user) {
    /* The atexit functions, last registered first; the count is reread
     * after each (rtl_imports.md "Exit paths"). */
    while (l->atexit_count) {
        uint32_t f = l->atexit_table[--l->atexit_count];
        if (f == ES2_SIM_VA_SURFACE_RELEASE) {
            if (es2_sim_surface_release(l)) return 1;
        } else if (call(l, f, NULL, 0, NULL)) {
            return 1;
        }
    }
    if (exit_records && exit_records(user)) {
        l->outcome = ES2_SIM_LOOP_REFUSED;
        return 1;
    }
    /* The stream exit hooks, each only when the stream code installed it. */
    es2_sim_files *f = es2_sim_files_bound();
    if (f) {
        if (r32(l, f->layout.exit_buffers) == f->layout.exit_buffers_value) es2_io_exit_buffers(&f->env, &f->layout);
        if (r32(l, f->layout.exit_files) == f->layout.exit_files_value) es2_io_exit_files(&f->env, &f->layout);
    }
    return 0;
}

/* ---- the game main after the mission load (0x45f430..0x45f568) */
static int stage(es2_sim_loop *l, uint32_t n) {
    uint32_t a[1] = {n};
    return call(l, VA_STAGE_DISPATCH, a, 1, NULL);
}
static int tail(es2_sim_loop *l, uint32_t ebp) {
    /* 0x45f4c5..0x45f4da: the sound driver call, the sound release, the frame clear. */
    uint32_t sound = r32(l, VA_SOUND);
    if (sound) {
        uint32_t a[1] = {sound};
        if (call(l, VA_SOUND_DRIVER, a, 1, NULL)) return 1;
    }
    if (call(l, VA_SOUND_RELEASE, NULL, 0, NULL) || call(l, VA_FRAME_SHOW, NULL, 0, NULL)) return 1;
    /* 0x45f4df..0x45f509: prefs byte 6 when the display mode changed. The
     * second slot's upper bytes are ECX's after the frame clear (not
     * modelled: zero; 0x459958 reads the byte, hypothesis from its name in
     * cfg.md). */
    if ((uint32_t)r8(l, VA_PREFS_BYTE6) != r32(l, VA_DIRECTDRAW)) {
        uint32_t a[3] = {6u, r8(l, VA_DIRECTDRAW), 1u};
        if (call(l, VA_PREFS_SET, a, 3, NULL)) return 1;
        uint32_t b[1] = {6u};
        if (call(l, VA_PREFS_WRITE, b, 1, NULL)) return 1;
    }
    /* 0x45f50a..0x45f53c: tape playback deletes the two tape copies. */
    if (r16(l, VA_PLAYBACK)) {
        static const uint32_t names[2] = {VA_TAPE_PREFS, VA_TAPE_KEYJOY};
        for (unsigned i = 0; i < 2u; ++i) {
            char name[64];
            unsigned n = 0;
            for (uint8_t b; n + 1u < sizeof(name) && (b = r8(l, names[i] + n)) != 0; ++n) name[n] = (char)b;
            name[n] = 0;
            ++l->platform.open_file;
            memcpy(l->platform.deleted[i], name, n + 1u);
            if (!l->delete_file) return es2_sim_loop_refuse(l, 0x004968a4u, "OpenFile", "OF_DELETE without a file platform");
            l->delete_file(l->delete_user, name);   /* the result is not read (0x45f529) */
        }
        (void)ebp;   /* the OFSTRUCT at ebp-0x1c4 is the original's stack only */
    }
    /* 0x4650cc (ret); 0x4650d0 (KillTimer only with [0x4d3608], which nothing writes). */
    if (r32(l, VA_TIMER)) return es2_sim_loop_refuse(l, 0x004650d9u, "KillTimer", "a timer at the loop tail");
    /* The mission end (hook mission_end), then 0x453848(holder, 2): counter
     * - 1, delete[] the error.str buffer. */
    if (call(l, VA_MISSION_END, NULL, 0, NULL)) return 1;
    uint32_t counter = r32(l, 0x006cbee4u);
    w32(l, counter, r32(l, counter) - 1u);
    uint32_t holder = ebp - 0x10u, buffer = r32(l, holder);
    if (buffer) {
        uint32_t a[1] = {buffer};
        if (call(l, VA_HOLDER_FREE, a, 1, NULL)) return 1;
    }
    return 0;
}

es2_sim_loop_outcome es2_sim_main_loop(es2_sim_loop *l, uint32_t ebp) {
    l->outcome = ES2_SIM_LOOP_RETURNED;
    /* 0x45f430..0x45f44f: stage 3, LoadCursorA(0, IDC_ARROW) -> 0x464c68,
     * the command gate. */
    if (stage(l, 3)) return l->outcome;
    ++l->platform.load_cursor;
    if (l->platform.cursor_arrow) w32(l, VA_CURSOR, l->platform.cursor_arrow);
    w16(l, VA_COMMAND_GATE, 1);
    for (;;) {
        ++l->passes;
        if (stage(l, 4)) return l->outcome;
        if (!r16(l, VA_PLAYBACK) && es2_sim_limiter(l)) return l->outcome;
        if (r8(l, VA_PAUSE)) {
            /* Sleep(100) returns at once headless (windowed it sleeps); the
             * pump. */
            ++l->platform.sleeps;
            if (l->window && l->window->sleep) l->window->sleep(l->window->user, 100u);
            uint32_t quit = 0;
            if (es2_sim_pump(l, &quit)) return l->outcome;
            if (quit) break;
            continue;
        }
        if (r16(l, VA_STOP) || r16(l, VA_QUIT)) break;
        es2_sim_result r;
        if (l->before_logic) (void)l->before_logic(l, l->before_logic_user);
        if (es2_sim_loop_call(l, VA_LOGIC_FRAME, NULL, 0, &r)) return l->outcome;
        if ((uint16_t)r.eax) break;
        uint32_t a[1] = {r32(l, VA_CAMERA)};
        if (call(l, VA_CAMERA_UPDATE, a, 1, NULL) || call(l, VA_RENDER_ROOT, NULL, 0, NULL) ||
            call(l, VA_PRESENT_FRAME, NULL, 0, NULL) || stage(l, 6))
            return l->outcome;
        /* 0x45f4ad: the frame boundary of virtual time, the tick record, the
         * increment. */
        es2_vt_frame_boundary(l->vt);
        int stop = l->tick ? l->tick(l, l->tick_user) : 0;
        ++l->ticks;
        w32(l, ES2_SIM_VA_FRAME_COUNTER, r32(l, ES2_SIM_VA_FRAME_COUNTER) + 1u);
        if (stop) {
            l->outcome = ES2_SIM_LOOP_STOPPED;
            return l->outcome;
        }
    }
    if (tail(l, ebp)) return l->outcome;
    l->outcome = ES2_SIM_LOOP_RETURNED;
    return l->outcome;
}

/* ---- service rows (sim/dispatch/services.h) over the bound instance */
static es2_sim_loop *serving(es2_sim_service_call *call) {
    es2_sim_loop *l = bound_loop;
    if (!l) { call->refused = 1; call->reason = "no main loop bound (H5-04)"; }
    return l;
}
/* A refusal inside a service is logged by the dispatch's caller: the
 * service reports it as its own. */
static void failed(es2_sim_service_call *call, es2_sim_loop *l) {
    call->refused = 1;
    call->reason = l->outcome == ES2_SIM_LOOP_REFUSED  ? "a call inside the service was refused"
                   : l->outcome == ES2_SIM_LOOP_EXITED ? "the process ended (__exit, _abort or a fault; H5-25b, D)"
                                                       : "refused";
}
/* 0x479cfc(fn) (listing 0x479cfc..0x479d3b): EAX the node, EDX the old
 * head, ECX fn. */
void es2_sim_service_abort_notify(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint32_t head = r32(l, ES2_SIM_VA_NOTIFY_HEAD), node = 0;
    if (es2_sim_abort_notify_add(l, call->slots[0], &node)) { failed(call, l); return; }
    call->eax = node; call->edx = head; call->ecx = call->slots[0];
}
/* _atexit (thunk 0x49675a): EAX 0, or 1 with a full table; ECX/EDX as at
 * the call (DBSIM's three callers, 0x480f7b, 0x480fc7 and 0x4779da, pop or
 * overwrite both before reading them). */
void es2_sim_service_atexit(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    call->eax = es2_sim_atexit(l, call->slots[0]);
}
/* 0x467828: EAX the clock; ECX/EDX as at the call (GetTickCount's under the
 * host leave them, own_process.md, "the host skeleton"). */
void es2_sim_service_clock(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    if (!l->vt || !l->vt->enabled) { call->refused = 1; call->reason = "virtual time disabled"; return; }
    call->eax = es2_sim_clock(l);
}
/* 0x467840: EAX the clock read, EDX 125 (movsx edx, ax), ECX as at the call. */
void es2_sim_service_timing_init(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    if (!l->vt || !l->vt->enabled) { call->refused = 1; call->reason = "virtual time disabled"; return; }
    call->eax = es2_sim_timing_init(l);
    call->edx = (uint32_t)(int32_t)(int16_t)r16(l, VA_REFERENCE_B);
}
/* 0x4678c0: only the main loop calls it (0x45f467); as a row for the
 * dispatch's completeness: EAX the elapsed time, ECX/EDX time_scale's. */
void es2_sim_service_limiter(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    if (!l->vt || !l->vt->enabled) { call->refused = 1; call->reason = "virtual time disabled"; return; }
    if (es2_sim_limiter(l)) { failed(call, l); return; }
    call->eax = r32(l, VA_LIMITER_SCRATCH);
}
/* 0x464c78: EAX 0, or 1 when it removed WM_QUIT; ECX/EDX as at the call
 * (the original leaves the last USER32 call's: host values no DBSIM caller
 * reads; hypothesis, the 15 call sites test only EAX). */
void es2_sim_service_pump(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint32_t result = 0;
    if (es2_sim_pump(l, &result)) { failed(call, l); return; }
    call->eax = result;
}
/* 0x480f30 (listing 0x480f30..0x480f34: push ebp; mov ebp, esp; pop ebp;
 * ret): the surface's abort-notify function; no register changes. */
void es2_sim_service_nothing(es2_sim_service_call *call, void *user) { (void)call; (void)user; }
/* 0x465fec(fn) (listing 0x465fec..0x46600c): the first free slot; EAX the
 * slot's address (0x4d3b58 when full), EDX its index (10), ECX fn. */
void es2_sim_service_handler_add(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint32_t i = 0, slot = ES2_SIM_VA_HANDLERS;
    for (; i < 10u; ++i, slot += 4u)
        if (!r32(l, slot)) { w32(l, slot, call->slots[0]); break; }
    call->eax = slot; call->edx = i; call->ecx = call->slots[0];
}
/* 0x466010(fn) (listing 0x466010..0x466031): the first slot equal to fn
 * cleared; EAX the slot's address, EDX its index, ECX 0 when found (fn
 * otherwise). */
void es2_sim_service_handler_remove(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint32_t i = 0, slot = ES2_SIM_VA_HANDLERS, ecx = call->slots[0];
    for (; i < 10u; ++i, slot += 4u)
        if (r32(l, slot) == call->slots[0]) { ecx = 0; w32(l, slot, 0); break; }
    call->eax = slot; call->edx = i; call->ecx = ecx;
}

/* ======================================================================
 * The shell leftovers as rows (main_loop.md, "the shell leftovers as own rows";
 * each body from the listing range named at its function). The own code that
 * already does their work inline (startup.c, the window procedure, the loop
 * tail, audio.c, input.c, hud_cockpit.c, hud_widgets.c) stays as it is; the
 * rows serve the guest callers the dispatch reaches (dialogs.c's callbacks
 * [0x49dd94]/[0x49dd98], the hooks' callees, the dialog constructors). */
#define VA_KEY_CAPTURE 0x0049eaa5u     /* byte: the flight-key capture flag */
#define VA_CAPTURE_DEPTH 0x0049f704u   /* byte: the modal nesting depth */
#define VA_CAPTURE_SAVED 0x004d2a80u   /* bytes: the capture flag per depth */
#define VA_KEY_CAPTURE_SET 0x0045b68cu
#define VA_LANGUAGE 0x004d2536u        /* byte: the language letter */
#define VA_LANGUAGE_DIR 0x0049f255u    /* "st?" with the letter at 0x49f257 */
#define VA_LANGUAGE_PATH 0x004d291cu
#define VA_BACKSLASH 0x0049f3fcu       /* "\\" */
#define VA_VOICE_NAME 0x0049f259u      /* "simvoice" with the letter at 0x49f260 */
#define VA_PATH_EXT 0x00492b90u
#define VA_STRCAT 0x004967eau
#define VA_RECT_FILL 0x00486704u
#define VA_GFX_STRING 0x00482634u
#define VA_WINGMAN_INDEX 0x00433134u
#define VA_LABEL_ONCE 0x0049f211u      /* byte: set on the first label */
#define VA_LABEL_FONT 0x004d1e2cu
#define VA_LABEL_FONT_KEPT 0x004d27a4u
#define VA_LABEL_RECT 0x004d27a8u      /* x0, y0, x1, y1 (dwords) */
#define VA_VIEW_W 0x004d253eu
#define VA_VIEW_H 0x004d2542u
#define VA_VIEW_SHIFT 0x004d253au      /* word: the resolution shift (cl) */
#define VA_VIEW_RECT 0x004cfa40u       /* 0x42db10's answer (mov eax, 0x4cfa40; ret) */
#define VA_GC 0x006c5f70u
#define VA_FILL_COLOUR 0x004d3ba2u     /* word */
#define VA_LABEL_X 0x0049f02eu         /* word */
#define VA_VALUE_X 0x0049f030u         /* word */
#define VA_SUBJECT_HOLDER 0x004d24eau  /* G+0x2e: [ ]+0x32 the subject */
#define VA_PLAYER 0x004d24e6u
#define VA_PLAYER_NAME 0x004d14dcu
#define VA_NO_NAME 0x0049f240u
#define VA_NAMES 0x0049d230u           /* 334-byte records, the name pointer at +0x264 */
#define VA_LABEL_STRINGS 0x004d16a0u   /* string pointers: label, value, value 2, value 3 */
#define VA_LABEL_MODE 0x004d24f0u      /* word */

static uint32_t gstrlen(const es2_sim_loop *l, uint32_t s, uint32_t bound) {
    uint32_t n = 0;
    while (n < bound && r8(l, s + n)) ++n;
    return n;
}
static void gstrcpy(const es2_sim_loop *l, uint32_t d, uint32_t s) {
    uint8_t v;
    do { v = r8(l, s++); w8(l, d++, v); } while (v);
}

/* 0x461250 (listing 0x461250..0x461271): the capture flag saved at the
 * depth, the depth + 1, then 0x45b68c(0) (capture off); EAX and EDX
 * 0x45b68c's, ECX 0 (pop ecx of the pushed 0). */
void es2_sim_service_key_capture_push(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint8_t depth = r8(l, VA_CAPTURE_DEPTH);
    w8(l, VA_CAPTURE_SAVED + depth, r8(l, VA_KEY_CAPTURE));
    w8(l, VA_CAPTURE_DEPTH, (uint8_t)(depth + 1u));
    uint32_t a[1] = {0};
    es2_sim_result r;
    if (es2_sim_loop_call(l, VA_KEY_CAPTURE_SET, a, 1, &r)) { failed(call, l); return; }
    call->eax = r.eax; call->edx = r.edx; call->ecx = 0;
}
/* 0x461274 (listing 0x461274..0x461291): the depth - 1, then 0x45b68c(the
 * flag saved there), pushed as ECX with CL the flag (0x45b68c reads the
 * byte); EAX and EDX 0x45b68c's, ECX that dword (pop ecx). */
void es2_sim_service_key_capture_pop(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint8_t depth = (uint8_t)(r8(l, VA_CAPTURE_DEPTH) - 1u);
    w8(l, VA_CAPTURE_DEPTH, depth);
    uint32_t a[1] = {(call->ecx & 0xffffff00u) | r8(l, VA_CAPTURE_SAVED + depth)};
    es2_sim_result r;
    if (es2_sim_loop_call(l, VA_KEY_CAPTURE_SET, a, 1, &r)) { failed(call, l); return; }
    call->eax = r.eax; call->edx = r.edx; call->ecx = a[0];
}
/* 0x4610ad (listing 0x4610ad..0x4610b8): EAX [[0x49b088] + 0x1e1]; ECX and
 * EDX as at the call. */
void es2_sim_service_hud_sub_object(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    call->eax = r32(l, r32(l, VA_COCKPIT) + 0x1e1u);
}
/* 0x45ef1c(name, ext) (listing 0x45ef1c..0x45ef81): byte 0x49f257 <- the
 * language letter; b = 0x492b90(name, ext); 0x4d291c <- "st<letter>" (the
 * inline copy), _strcat(0x4d291c, "\\"), _strcat(that, b). EAX, ECX and EDX
 * the second _strcat's. */
void es2_sim_service_language_path(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    w8(l, VA_LANGUAGE_DIR + 2u, r8(l, VA_LANGUAGE));
    uint32_t a[2] = {call->slots[0], call->slots[1]};
    es2_sim_result r;
    if (es2_sim_loop_call(l, VA_PATH_EXT, a, 2, &r)) { failed(call, l); return; }
    uint32_t b = r.eax;
    gstrcpy(l, VA_LANGUAGE_PATH, VA_LANGUAGE_DIR);
    a[0] = VA_LANGUAGE_PATH; a[1] = VA_BACKSLASH;
    if (es2_sim_loop_call(l, VA_STRCAT, a, 2, &r)) { failed(call, l); return; }
    a[0] = r.eax; a[1] = b;
    if (es2_sim_loop_call(l, VA_STRCAT, a, 2, &r)) { failed(call, l); return; }
    call->eax = r.eax; call->edx = r.edx; call->ecx = r.ecx;
}
/* 0x45ef84 (listing 0x45ef84..0x45ef9b): byte 7 of "simvoice" <- the
 * language letter unless it is 'r' (movsx, cmp 0x72); EAX 0x49f259, EDX the
 * letter sign-extended, ECX as at the call. */
void es2_sim_service_voice_name(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint8_t language = r8(l, VA_LANGUAGE);
    int32_t s = (int8_t)language;
    if (s != 0x72) w8(l, VA_VOICE_NAME + 7u, language);
    call->eax = VA_VOICE_NAME;
    call->edx = (uint32_t)s;
}
/* 0x464c68(handle) (listing 0x464c68..0x464c74): [0x4d3720] <- handle; EAX
 * the handle, ECX and EDX as at the call. */
void es2_sim_service_cursor_store(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    w32(l, VA_CURSOR, call->slots[0]);
    call->eax = call->slots[0];
}
/* 0x4650d0 (listing 0x4650d0..0x4650e6): EAX [0x4d3608]; without a timer
 * nothing else; with one KillTimer([0x4d3610], it), which headless has no
 * timer for (nothing in DBSIM writes 0x4d3608): refused by name. */
void es2_sim_service_timer_kill(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint32_t timer = r32(l, VA_TIMER);
    call->eax = timer;
    if (timer) {
        es2_sim_loop_refuse(l, 0x004650d9u, "KillTimer", "a timer the headless platform never set");
        failed(call, l);
    }
}

/* 0x434298(x) inline (listing 0x434298..0x4342b5, with 0x44b928 at
 * 0x44b928..0x44b94f, which never reads its first argument
 * [[0x49b088]+0x1f9]): [[0x49d230] + 334 * x + 0x264]. */
static uint32_t subject_name(const es2_sim_loop *l, int32_t x) {
    return r32(l, r32(l, VA_NAMES) + 334u * (uint32_t)x + 0x264u);
}
/* The 0x54-byte frame S of 0x45e208: S+0 x, S+4 y, S+8 the colour pair
 * {0, colour} that GC+0x22c points at, S+0x10 the 0x34-byte text buffer,
 * S+0x44 the second rectangle. A text longer than the bytes up to the
 * saved registers (0x44 from S+0x10) would overwrite them: refused. */
#define LABEL_FRAME 0x54u
#define LABEL_TEXT_ROOM 0x44u
static int label_text(es2_sim_loop *l, uint32_t text, uint32_t first, uint32_t second) {
    uint32_t n = gstrlen(l, first, LABEL_TEXT_ROOM) + gstrlen(l, second, LABEL_TEXT_ROOM);
    if (n + 1u > LABEL_TEXT_ROOM)
        return es2_sim_loop_refuse(l, 0x0045e208u, "subject label", "the text overruns the function's frame");
    gstrcpy(l, text, first);
    uint32_t a[2] = {text, second};
    return es2_sim_loop_call(l, VA_STRCAT, a, 2, NULL);
}
/* 0x45e208 (listing 0x45e208..0x45e45e): the subject label of the view
 * (callers 0x45d427/0x45d44c in 0x45cff0, 0x45df29 in view_apply). */
static int subject_label(es2_sim_loop *l, es2_sim_result *out) {
    if (!r8(l, VA_LABEL_ONCE)) w8(l, VA_LABEL_ONCE, 1);
    uint32_t font = r32(l, VA_LABEL_FONT);
    uint32_t w1 = r32(l, VA_VIEW_W) - 1u, h1 = r32(l, VA_VIEW_H) - 1u;
    w32(l, VA_LABEL_FONT_KEPT, font);
    int32_t h = (int16_t)r16(l, font + 0xau);
    /* y = ((h1 - top) sar 1) + top - (h sar 1) in 16 bits, at most h1 - 1 - h. */
    uint32_t top = r32(l, VA_VIEW_RECT + 0xcu);
    uint32_t half = (uint32_t)((int32_t)(h1 - top) >> 1);
    uint16_t y16 = (uint16_t)((uint16_t)half + r16(l, VA_VIEW_RECT + 0xcu));
    y16 = (uint16_t)(y16 - (uint16_t)(h >> 1));
    if ((int32_t)(h1 - 1u - (uint32_t)h) < (int32_t)(int16_t)y16) y16 = (uint16_t)((uint16_t)(h1 - 1u) - (uint16_t)h);
    int32_t y = (int16_t)y16;
    w32(l, VA_LABEL_RECT, 0);
    w32(l, VA_LABEL_RECT + 4u, (uint32_t)y);
    w32(l, VA_LABEL_RECT + 8u, w1);
    w32(l, VA_LABEL_RECT + 12u, (uint32_t)(y + h));
    uint32_t gc = r32(l, VA_GC);
    uint32_t keep_208 = r32(l, gc + 0x208u), keep_20c = r32(l, gc + 0x20cu);
    w32(l, gc + 0x208u, 0);
    w32(l, gc + 0x20cu, 0);
    uint32_t top_area = l->stack_hi;
    if (top_area - l->stack_lo < LABEL_FRAME + 0x400u)
        return es2_sim_loop_refuse(l, 0x0045e20cu, "subject label", "the own code's call area is exhausted");
    l->stack_hi -= LABEL_FRAME;
    uint32_t s = l->stack_hi;
    w32(l, s + 8u, 0);
    w32(l, s + 0xcu, (uint32_t)(int32_t)(int16_t)r16(l, VA_FILL_COLOUR));
    w32(l, gc + 0x22cu, s + 8u);
    uint32_t a[3] = {VA_LABEL_RECT, 0, 0};
    int bad = es2_sim_loop_call(l, VA_RECT_FILL, a, 1, NULL);
    unsigned shift = r16(l, VA_VIEW_SHIFT) & 31u;   /* shl r32, cl */
    if (!bad) {
        w32(l, s, (uint32_t)(int32_t)(int16_t)r16(l, VA_LABEL_X) << shift);
        w32(l, s + 4u, r32(l, VA_LABEL_RECT + 12u));
    }
    /* The subject's name: the player's, "" (0x49f240) without a wingman
     * index (low word 0xffff), else the wingman record's. */
    uint32_t name = 0;
    if (!bad) {
        uint32_t subject = r32(l, r32(l, VA_SUBJECT_HOLDER) + 0x32u);
        if (subject == r32(l, VA_PLAYER)) name = r32(l, VA_PLAYER_NAME);
        else {
            uint32_t b[1] = {subject}, index = 0;
            bad = call(l, VA_WINGMAN_INDEX, b, 1, &index);
            if (!bad) name = (uint16_t)index == 0xffffu ? VA_NO_NAME : subject_name(l, (int16_t)(uint16_t)index);
        }
    }
    if (!bad) bad = label_text(l, s + 0x10u, r32(l, VA_LABEL_STRINGS), name);
    if (!bad) {
        if (r8(l, s + 0x10u)) {
            a[0] = r32(l, VA_LABEL_FONT_KEPT); a[1] = s; a[2] = s + 0x10u;
            bad = es2_sim_loop_call(l, VA_GFX_STRING, a, 3, NULL);
        } else {
            /* An empty text: a box 0x32 << shift wide, the font's height above y. */
            uint32_t x0 = r32(l, s), y1 = r32(l, s + 4u);
            uint32_t fh = (uint32_t)(int32_t)(int16_t)r16(l, r32(l, VA_LABEL_FONT_KEPT) + 0xau);
            w32(l, s + 0x44u, x0);
            w32(l, s + 0x48u, y1 - fh);
            w32(l, s + 0x4cu, (0x32u << shift) + x0);
            w32(l, s + 0x50u, y1);
            w32(l, s + 0xcu, (uint32_t)(int32_t)(int16_t)r16(l, VA_FILL_COLOUR));
            a[0] = s + 0x44u;
            bad = es2_sim_loop_call(l, VA_RECT_FILL, a, 1, NULL);
        }
    }
    if (!bad) {
        w32(l, s, (uint32_t)(int32_t)(int16_t)r16(l, VA_VALUE_X) << shift);
        uint32_t first = r32(l, VA_LABEL_STRINGS + 4u);
        uint32_t second = r32(l, VA_LABEL_STRINGS + (r16(l, VA_LABEL_MODE) ? 8u : 12u));
        bad = label_text(l, s + 0x10u, first, second);
    }
    if (!bad) {
        a[0] = r32(l, VA_LABEL_FONT_KEPT); a[1] = s; a[2] = s + 0x10u;
        bad = es2_sim_loop_call(l, VA_GFX_STRING, a, 3, out);
    }
    l->stack_hi = top_area;
    if (bad) return 1;
    w32(l, gc + 0x208u, keep_208);
    w32(l, gc + 0x20cu, keep_20c);
    return 0;
}
/* EAX, ECX and EDX the last 0x482634's. */
void es2_sim_service_subject_label(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    if (subject_label(l, &r)) { failed(call, l); return; }
    call->eax = r.eax; call->edx = r.edx; call->ecx = r.ecx;
}

/* ======================================================================
 * The rows the headless replay reached next (own_process.md, "the headless
 * acceptance"). */
#define VA_DUMP_OPTION 0x004d24deu     /* dword: the -d option's flag */
#define VA_DUMP_RECORD 0x004d24d8u     /* word: recording on */
#define VA_DUMP_NAME 0x004d24dau       /* dword: the tape's base name */
#define VA_DUMP_EXT 0x00497087u        /* "dmp" */
#define VA_DUMP_MODE 0x0049708bu       /* "ab" */
#define VA_DUMP_SOURCE 0x0049708eu     /* "vcr.cpp" */
#define VA_DUMP_ERROR 0x006c56c8u      /* the error reporter's record */
#define VA_DUMP_CURSOR 0x00497078u
#define VA_DUMP_COUNT 0x0049707cu
#define VA_DUMP_BUFFER 0x004a84fcu
#define VA_FATAL_REPORT 0x00479c78u
#define VA_FOPEN 0x00496802u
#define VA_FWRITE 0x00496832u
#define VA_FCLOSE 0x00496838u
#define VA_DISPLAY_SWITCH 0x004667c8u
/* 0x401e5c, the stage-6 callback (listing 0x401e5c..0x401ef7; file_io.md
 * "The dmp file"). Without the option or without recording it returns at
 * once and leaves EAX, EDX and ECX as at the call (the compare of
 * [0x4d24d6] at 0x401eea has no branch). Otherwise: path =
 * 0x492b90([0x4d24da], "dmp"), f = _fopen(path, "ab"); a NULL f goes to the
 * error reporter 0x479c78(0x6c56c8, 5, 0, "vcr.cpp", 0x2f, path) and the
 * original goes on with f = 0; _fwrite(0x4a84fc, 1, [0x49707c], f),
 * _fclose(f), the cursor 0x497078 = 0x4a84fc and the count 0x49707c = 0.
 * EAX _fclose's, EDX 0, ECX f (the pop ecx at 0x401ee7). */
static int dump_call(es2_sim_loop *l, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    es2_sim_result r;
    if (es2_sim_loop_call(l, va, slots, count, &r)) return 1;
    if (eax) *eax = r.eax;
    return 0;
}
void es2_sim_service_dump_flush_401e5c(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    if (!r32(l, VA_DUMP_OPTION) || !r16(l, VA_DUMP_RECORD)) return;
    uint32_t path = 0, f = 0, closed = 0;
    uint32_t a[2] = {r32(l, VA_DUMP_NAME), VA_DUMP_EXT};
    if (dump_call(l, VA_PATH_EXT, a, 2, &path)) { failed(call, l); return; }
    uint32_t b[2] = {path, VA_DUMP_MODE};
    if (dump_call(l, VA_FOPEN, b, 2, &f)) { failed(call, l); return; }
    if (!f) {
        uint32_t e[6] = {VA_DUMP_ERROR, 5u, 0u, VA_DUMP_SOURCE, 0x2fu, path};
        if (dump_call(l, VA_FATAL_REPORT, e, 6, NULL)) { failed(call, l); return; }
    }
    uint32_t w[4] = {VA_DUMP_BUFFER, 1u, r32(l, VA_DUMP_COUNT), f};
    if (dump_call(l, VA_FWRITE, w, 4, NULL)) { failed(call, l); return; }
    uint32_t c[1] = {f};
    if (dump_call(l, VA_FCLOSE, c, 1, &closed)) { failed(call, l); return; }
    w32(l, VA_DUMP_CURSOR, VA_DUMP_BUFFER);
    w32(l, VA_DUMP_COUNT, 0);
    call->eax = closed; call->edx = 0; call->ecx = f;
}
/* 0x4669b4 (listing 0x4669b4..0x4669c2): the display switch 0x4667c8 only
 * when DirectDraw's full screen is on (dword [0x4d255e]); otherwise EAX, EDX
 * and ECX as at the call. With the switch, its registers. */
void es2_sim_service_display_restore_4669b4(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    if (!r32(l, VA_DIRECTDRAW)) return;
    es2_sim_result r;
    if (es2_sim_loop_call(l, VA_DISPLAY_SWITCH, NULL, 0, &r)) { failed(call, l); return; }
    call->eax = r.eax; call->edx = r.edx; call->ecx = r.ecx;
}

/* ======================================================================
 * The error report 0x479c78 and its pieces (own_process.md, "SHELL, RTL, FILE
 * and INP gap roots as rows and verdicts"; rows in
 * sim/dispatch/services/gap_roots_shell.h). Severity names, formats, the log's name and
 * the captions are the original's strings, read from the guest image. EAX
 * follows the original; EDX and ECX are zero (the callers 0x401e5c and the
 * scene asserts do not read them).
 *
 * MessageBoxA is counted (l->platform.message_box; its caption and the text's
 * hash are kept for es2_sim_fatal_box_last) and goes to the process platform
 * l->process (es2/sim/process_platform.h): the windowed es2sim shows Win32's
 * MessageBoxA(0, text, caption, 0) and returns its answer; headless (no record)
 * it answers IDOK without showing anything. PostQuitMessage(0) posts WM_QUIT
 * (es2_sim_post_quit), which the pump's peek returns after the posted messages
 * and the window's input, as Windows does. The process end __exit(1) 0x49679c
 * and _abort 0x49688c: with a record that has process_exit, the process ends
 * there with the original's status (1, or 3 after _abort's box); a test's
 * recorder returns, and the run ends with ES2_SIM_LOOP_EXITED. Without one (the
 * library's default; es2sim sets one headless too) they are called through
 * the dispatch, which has no row for them: the run stops there, refused. */
#define VA_REPORT_NOTIFY 0x006c56d8u    /* head of the notify list (0x479cfc) */
#define VA_REPORT_ROOT 0x006c56dcu      /* the root the start-up copies (0x479e18) */
#define VA_REPORT_LOG 0x006c57dcu       /* the log's path */
#define VA_REPORT_LOG_NAME 0x004a1540u
#define VA_REPORT_RULE 0x004a154bu      /* the separator, 0x50 bytes */
#define VA_REPORT_RULE_FLAG 0x004a1610u
#define VA_REPORT_CRLF 0x004a1614u
#define VA_REPORT_NAMES 0x004a15f4u     /* the severity names */
#define VA_REPORT_FORMAT 0x004a1635u
#define VA_REPORT_FORMAT_TEXT 0x004a1651u
#define VA_REPORT_WARNINGS 0x006c582cu  /* severity 3's count */
#define VA_REPORT_BOX_HEAD 0x004a159cu
#define VA_REPORT_BOX_GAP 0x004a1662u
#define VA_REPORT_CAPTION_3 0x004a165cu
#define VA_REPORT_CAPTION_4 0x004a1656u
#define VA_REPORT_CAPTION_5 0x004a1664u
#define VA_REPORT_TEXT_6 0x004a166au
#define VA_REPORT_DISPLAY 0x004669b4u   /* hud_mfd_leaves.h's row display_restore */
#define VA_REPORT_WINDOW_NOTHING 0x004650ccu
#define VA_REPORT_TIMER_KILL 0x004650d0u
#define VA_RT_STRCAT 0x004967eau
#define VA_RT_SPRINTF 0x00496796u
#define VA_RT_OPEN 0x004967c6u
#define VA_RT_WRITE 0x0049677eu
#define VA_RT_CLOSE 0x00496874u
#define VA_RT_ABORT 0x0049688cu
#define VA_RT_EXIT 0x0049679cu

static struct { uint32_t caption; uint64_t hash; } report_box;
void es2_sim_fatal_box_last(uint32_t *caption, uint64_t *hash);
void es2_sim_fatal_box_last(uint32_t *caption, uint64_t *hash) {
    *caption = report_box.caption;
    *hash = report_box.hash;
}
static uint32_t report_frame(es2_sim_loop *l, uint32_t bytes) {
    if (l->stack_hi - l->stack_lo < bytes + 0x400u) return 0;
    l->stack_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) w32(l, l->stack_hi + i, 0);
    return l->stack_hi;
}
static int report_call(es2_sim_loop *l, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    es2_sim_result r;
    if (es2_sim_loop_call(l, va, slots, count, &r)) return 1;
    *eax = r.eax;
    return 0;
}
static void report_strcpy(es2_sim_loop *l, uint32_t to, uint32_t from) {
    uint8_t b;
    uint32_t i = 0;
    do { b = r8(l, from + i); w8(l, to + i, b); ++i; } while (b);
}
/* ---- the process platform (es2/sim/process_platform.h) */
uint32_t es2_sim_process_message_box(const es2_sim_process_platform *p, uint32_t owner, const char *text, const char *caption,
                                     uint32_t style) {
    if (!p || !p->message_box) return ES2_SIM_IDOK;
    return p->message_box(p->user, owner, text, caption, style);
}
uint32_t es2_sim_process_abort(const es2_sim_process_platform *p) {
    /* CW3220's _abort: raise(SIGABRT) with the default handler, then
     * __ErrorMessage(text) = MessageBoxA(NULL, text, the module's file
     * name, 0x10010 | MB_TASKMODAL) and __exit(3) (rtl_imports.md). */
    es2_sim_process_message_box(p, 0, ES2_SIM_ABORT_TEXT, p && p->module_name ? p->module_name : "DBSIM.EXE",
                                ES2_SIM_ABORT_STYLE);
    if (p && p->process_exit) p->process_exit(p->user, ES2_SIM_ABORT_STATUS);
    return ES2_SIM_ABORT_STATUS;
}
/* D: the original's unhandled faults (es2/sim/loop.h). */
int es2_sim_loop_fault(es2_sim_loop *l, uint32_t code, uint32_t site) {
    if (!l || !l->process || !l->process->process_fault) return 0;
    l->process->process_fault(l->process->user, code, site);
    l->outcome = ES2_SIM_LOOP_EXITED;
    l->exit_code = code;
    return 1;
}
int es2_sim_loop_divide_fault(es2_sim_loop *l, uint32_t site, uint64_t dividend, uint32_t divisor, int is_signed) {
    if (!l || !l->process || !l->process->process_fault) return 0;
    uint32_t code = es2_sim_divide_fault(dividend, divisor, is_signed);
    return code ? es2_sim_loop_fault(l, code, site) : 0;
}
int es2_sim_loop_divide_refuse(es2_sim_loop *l, uint32_t va, uint64_t dividend, uint32_t divisor, int is_signed, const char *name,
                               const char *reason) {
    if (es2_sim_loop_divide_fault(l, va, dividend, divisor, is_signed)) return 1;
    return es2_sim_loop_refuse(l, va, name, reason);
}
int es2_sim_loop_null_refuse(es2_sim_loop *l, uint32_t va, const char *name, const char *reason) {
    if (es2_sim_loop_fault(l, ES2_SIM_ACCESS_VIOLATION, va)) return 1;
    return es2_sim_loop_refuse(l, va, name, reason);
}
/* The guest's NUL-terminated bytes (at most capacity - 1). */
static void report_guest_text(es2_sim_loop *l, uint32_t va, char *out, uint32_t capacity) {
    uint32_t i = 0;
    for (; va && i + 1u < capacity; ++i) {
        uint8_t b = r8(l, va + i);
        if (!b) break;
        out[i] = (char)b;
    }
    out[i] = 0;
}
/* MessageBoxA(0, text, caption, 0): the process platform's answer
 * (headless IDOK). The box texts are at most 0x3e8 bytes (0x479f24's
 * buffers); 0x1000 is the hash's bound as before. */
static uint32_t report_box_show(es2_sim_loop *l, uint32_t text, uint32_t caption) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < 0x1000u; ++i) {
        uint8_t b = r8(l, text + i);
        if (!b) break;
        h = (h ^ b) * 0x100000001b3ull;
    }
    ++l->platform.message_box;
    report_box.caption = caption;
    report_box.hash = h;
    if (!l->process || !l->process->message_box) return ES2_SIM_IDOK;
    static char t[0x1000], c[0x100];
    report_guest_text(l, text, t, sizeof(t));
    report_guest_text(l, caption, c, sizeof(c));
    return es2_sim_process_message_box(l->process, 0, t, c, 0);
}
/* __exit(status) 0x49679c or _abort 0x49688c: through the dispatch without
 * a process record with process_exit (headless: no row, refused); else the
 * process ends (es2sim does not return; a recorder does, and the run ends
 * with ES2_SIM_LOOP_EXITED and the status). Returns 1 when the run ends. */
static int report_process_end(es2_sim_loop *l, uint32_t va, uint32_t status, uint32_t *eax) {
    if (!l->process || !l->process->process_exit) {
        uint32_t code[1] = {status};
        return report_call(l, va, va == VA_RT_EXIT ? code : NULL, va == VA_RT_EXIT ? 1u : 0u, eax);
    }
    if (va == VA_RT_ABORT) status = es2_sim_process_abort(l->process);
    else l->process->process_exit(l->process->user, status);
    l->outcome = ES2_SIM_LOOP_EXITED;
    l->exit_code = status;
    return 1;
}
/* 0x479c54(record, severity): every node of the list [0x6c56d8] (next at
 * +0, function at +4) from the head: fn(severity); the record is not read.
 * EAX the last function's, else as at the entry (listing 0x479c54..
 * 0x479c74). */
static int report_notify(es2_sim_loop *l, uint32_t severity, uint32_t *eax) {
    for (uint32_t node = r32(l, VA_REPORT_NOTIFY); node; node = r32(l, node)) {
        uint32_t a[1] = {severity};
        if (report_call(l, r32(l, node + 4u), a, 1, eax)) return 1;
    }
    return 0;
}
/* 0x479e48: 0x6c57dc = the root 0x6c56dc (copied inline) + 0x4a1540
 * (_strcat); EAX 0x6c57dc (listing 0x479e48..0x479e8a). */
static int report_log_path(es2_sim_loop *l, uint32_t *eax) {
    report_strcpy(l, VA_REPORT_LOG, VA_REPORT_ROOT);
    uint32_t a[2] = {VA_REPORT_LOG, VA_REPORT_LOG_NAME}, x;
    if (report_call(l, VA_RT_STRCAT, a, 2, &x)) return 1;
    *eax = VA_REPORT_LOG;
    return 0;
}
/* 0x479e8c(text): h = _open(0x479e48(), 0x901, 0x180); a negative h calls
 * _abort. Otherwise, once (the dword 0x4a1610, cleared after), the 0x50
 * bytes at 0x4a154b; the text (its length counted inline); the two bytes
 * at 0x4a1614 (copied with the third, 0x4a1616, into a local); _close(h):
 * EAX its (listing 0x479e8c..0x479f20). */
static int report_log(es2_sim_loop *l, uint32_t text, uint32_t *eax) {
    uint32_t top = l->stack_hi, crlf = report_frame(l, 4u);
    if (!crlf) return es2_sim_loop_refuse(l, 0x00479e8cu, "error log", "the own code's call area is exhausted");
    w16(l, crlf, r16(l, VA_REPORT_CRLF));
    w8(l, crlf + 2u, r8(l, VA_REPORT_CRLF + 2u));
    uint32_t path = 0, h = 0, x = 0;
    int refused = report_log_path(l, &path);
    uint32_t o[3] = {path, 0x901u, 0x180u};
    if (!refused) refused = report_call(l, VA_RT_OPEN, o, 3, &h);
    if (!refused && (int32_t)h < 0) refused = report_process_end(l, VA_RT_ABORT, ES2_SIM_ABORT_STATUS, eax);
    else if (!refused) {
        if (r32(l, VA_REPORT_RULE_FLAG)) {
            uint32_t w[3] = {h, VA_REPORT_RULE, 0x50u};
            refused = report_call(l, VA_RT_WRITE, w, 3, &x);
            if (!refused) w32(l, VA_REPORT_RULE_FLAG, 0);
        }
        uint32_t n = 0;
        while (r8(l, text + n)) ++n;
        uint32_t w1[3] = {h, text, n}, w2[3] = {h, crlf, 2u}, c[1] = {h};
        if (!refused) refused = report_call(l, VA_RT_WRITE, w1, 3, &x);
        if (!refused) refused = report_call(l, VA_RT_WRITE, w2, 3, &x);
        if (!refused) refused = report_call(l, VA_RT_CLOSE, c, 1, eax);
    }
    l->stack_hi = top;
    return refused;
}
/* 0x479dd0(buf, severity, file, line, text): sprintf(buf, 0x4a1635, the
 * name [0x4a15f4 + 4 * severity], line, file), then with a text
 * sprintf(buf + n, 0x4a1651, text); EAX the last sprintf's (listing
 * 0x479dd0..0x479e14). */
static int report_text(es2_sim_loop *l, uint32_t buf, uint32_t severity, uint32_t file, uint32_t line, uint32_t text, uint32_t *eax) {
    uint32_t a[5] = {buf, VA_REPORT_FORMAT, r32(l, VA_REPORT_NAMES + 4u * severity), line, file}, n = 0;
    if (report_call(l, VA_RT_SPRINTF, a, 5, &n)) return 1;
    *eax = n;
    if (!text) return 0;
    uint32_t b[3] = {buf + n, VA_REPORT_FORMAT_TEXT, text};
    return report_call(l, VA_RT_SPRINTF, b, 3, eax);
}
/* 0x479f24(severity, a, file, line, text), the jump table at 0x479f45
 * (a is not read; listing 0x479f24..0x47a0b4). 0, 1 and above 6: nothing
 * (EAX the severity). 2: the log of the text. 3: the display guard
 * 0x4669b4, the count 0x6c582c + 1, the report text into a 0x200-byte
 * buffer, a box with caption 0x4a165c, the log of the buffer. 4: the guard,
 * the text, its log, a box with caption 0x4a1656 (EAX the box's). 5: the
 * guard; a 0x3e8-byte box text = 0x4a159c (copied inline) + 0x4a1662 + the
 * report text (_strcat), a box with caption 0x4a1664, the log of the
 * report text. 6: the log of 0x4a166a. */
static int report(es2_sim_loop *l, uint32_t severity, uint32_t file, uint32_t line, uint32_t text, uint32_t *eax) {
    uint32_t x = 0;
    if (severity < 2u || severity > 6u) { *eax = severity; return 0; }
    if (severity == 2u) return report_log(l, text, eax);
    if (severity == 6u) return report_log(l, VA_REPORT_TEXT_6, eax);
    uint32_t top = l->stack_hi, box = report_frame(l, 0x5e8u), buf = box + 0x3e8u;
    if (!box) return es2_sim_loop_refuse(l, 0x00479f24u, "error report", "the own code's call area is exhausted");
    int refused = report_call(l, VA_REPORT_DISPLAY, NULL, 0, &x);
    if (!refused && severity == 3u) {
        w32(l, VA_REPORT_WARNINGS, r32(l, VA_REPORT_WARNINGS) + 1u);
        refused = report_text(l, buf, severity, file, line, text, &x);
        if (!refused) {
            report_box_show(l, buf, VA_REPORT_CAPTION_3);
            refused = report_log(l, buf, eax);
        }
    } else if (!refused && severity == 4u) {
        refused = report_text(l, buf, severity, file, line, text, &x);
        if (!refused) refused = report_log(l, buf, &x);
        if (!refused) *eax = report_box_show(l, buf, VA_REPORT_CAPTION_4);
    } else if (!refused) {
        report_strcpy(l, box, VA_REPORT_BOX_HEAD);
        uint32_t a[2] = {box, VA_REPORT_BOX_GAP}, b[2] = {box, buf};
        refused = report_call(l, VA_RT_STRCAT, a, 2, &x);
        if (!refused) refused = report_text(l, buf, severity, file, line, text, &x);
        if (!refused) refused = report_call(l, VA_RT_STRCAT, b, 2, &x);
        if (!refused) {
            report_box_show(l, box, VA_REPORT_CAPTION_5);
            refused = report_log(l, buf, eax);
        }
    }
    l->stack_hi = top;
    return refused;
}
/* 0x479c78(record, severity, a, file, line, text) (listing 0x479c78..
 * 0x479cfa): severities 4, 5 and 6 run the notify list (0x479c54), the
 * report, the report 0x479f24(1, 1, 0, 0, 0) (nothing), 0x4650cc (ret),
 * the timer's KillTimer 0x4650d0, PostQuitMessage(0) and __exit(1) (EAX
 * its, when it returns); the others only the report (EAX its). */
static int fatal_report(es2_sim_loop *l, const uint32_t *slot, uint32_t *eax) {
    uint32_t severity = slot[1], x = 0;
    if (severity != 4u && severity != 5u && severity != 6u) return report(l, severity, slot[3], slot[4], slot[5], eax);
    if (report_notify(l, severity, &x) || report(l, severity, slot[3], slot[4], slot[5], &x) || report(l, 1u, 0, 0, 0, &x) ||
        report_call(l, VA_REPORT_WINDOW_NOTHING, NULL, 0, &x) || report_call(l, VA_REPORT_TIMER_KILL, NULL, 0, &x))
        return 1;
    es2_sim_post_quit(l);
    return report_process_end(l, VA_RT_EXIT, ES2_SIM_FATAL_STATUS, eax);
}

/* The service functions (EAX as above, EDX and ECX zero). The one of 0x479c78
 * has no row yet (own_process.md, "SHELL, RTL, FILE and INP gap roots as rows
 * and verdicts": three suites of other packages serve that VA with a stub);
 * gap_roots_shell_cases.h registers it. */
void es2_sim_service_fatal_report_479c78(es2_sim_service_call *call, void *user);
void es2_sim_service_fatal_report_479c78(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint32_t eax = 0;
    if (fatal_report(l, call->slots, &eax)) { failed(call, l); return; }
    call->eax = eax; call->edx = 0; call->ecx = 0;
}
void es2_sim_service_report_notify_479c54(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint32_t eax = call->eax;
    if (report_notify(l, call->slots[1], &eax)) { failed(call, l); return; }
    call->eax = eax; call->edx = 0; call->ecx = 0;
}
void es2_sim_service_report_text_479dd0(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    const uint32_t *a = call->slots;
    uint32_t eax = 0;
    if (report_text(l, a[0], a[1], a[2], a[3], a[4], &eax)) { failed(call, l); return; }
    call->eax = eax; call->edx = 0; call->ecx = 0;
}
void es2_sim_service_report_log_path_479e48(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint32_t eax = 0;
    if (report_log_path(l, &eax)) { failed(call, l); return; }
    call->eax = eax; call->edx = 0; call->ecx = 0;
}
void es2_sim_service_report_log_479e8c(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint32_t eax = 0;
    if (report_log(l, call->slots[0], &eax)) { failed(call, l); return; }
    call->eax = eax; call->edx = 0; call->ecx = 0;
}
void es2_sim_service_report_479f24(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    const uint32_t *a = call->slots;
    uint32_t eax = 0;
    if (report(l, a[0], a[2], a[3], a[4], &eax)) { failed(call, l); return; }
    call->eax = eax; call->edx = 0; call->ecx = 0;
}

/* ======================================================================
 * The mission start's abort-notify clean-up 0x461304 and the remap table's
 * release 0x46a4ac (own_process.md, "the mission start's clean-up 0x461304 and
 * the call helpers' EXITED"; rows in sim/dispatch/services/mission_abort_cleanup.h). The
 * mission start 0x461608 registers 0x461304 with 0x479cfc (0x461626), so the
 * error report 0x479c78 runs it from its notify list before its box. Both keep
 * EAX, EDX and ECX as the original's bytes leave them: a register no
 * instruction writes keeps its value from the entry or from the callee before. */
#define VA_CLEANUP_TAPE 0x004d24e2u     /* the playback FILE* (control_record) */
#define VA_CLEANUP_DUMP 0x004d24deu     /* the dump FILE* (the -d option) */
#define VA_CLEANUP_CODE 0x004d27b8u     /* the process exit code */
#define VA_REMAP_FLAG 0x004a0918u       /* word: the remap table is held */
#define VA_REMAP_TABLE 0x006b4f20u      /* its block (0x46a464's new[]) */
#define VA_REMAP_CLEARED 0x006b4f12u    /* the dword 0x46a4ac clears (not the block's own pointer) */
#define VA_CLEANUP_FCLOSE 0x00496838u   /* _fclose (thunk, row file_fclose) */
#define VA_CLEANUP_DELETE 0x00477548u   /* operator delete (row) */
#define VA_REMAP_RELEASE 0x0046a4acu

/* A call through the dispatch entered with r[3] (EAX, EDX, ECX); r gets the
 * callee's registers. With one argument, *back is its slot as read after
 * the call (the caller's `pop ecx`). */
static int cleanup_call(es2_sim_loop *l, uint32_t va, const uint32_t *slot, uint32_t r[3], uint32_t *back) {
    uint32_t top = l->stack_hi;
    if (top - l->stack_lo < 4u) return es2_sim_loop_refuse(l, va, NULL, "the own code's call area is exhausted");
    l->stack_hi -= 4u;
    w32(l, l->stack_hi, slot ? *slot : 0u);
    es2_sim_result out;
    memset(&out, 0, sizeof(out));
    int refused = es2_sim_call(l->dispatch, va, l->memory->host(l->memory->context, l->stack_hi), slot ? 1u : 0u, r, &out);
    uint32_t slot_after = r32(l, l->stack_hi);
    l->stack_hi = top;
    if (refused) {
        if (l->outcome == ES2_SIM_LOOP_EXITED) return 1;   /* the process ended inside */
        l->outcome = ES2_SIM_LOOP_REFUSED;
        l->refused_va = va;
        return 1;
    }
    r[0] = out.eax; r[1] = out.edx; r[2] = out.ecx;
    if (back) *back = slot_after;
    return 0;
}
/* 0x46a4ac() (listing 0x46a4ac..0x46a4d3): with the word [0x4a0918] set,
 * delete 0x477548([0x6b4f20]) (EAX the block at the call), ECX the slot
 * popped back, EDX 0, [0x6b4f12] <- 0, the word [0x4a0918] <- 0. Without
 * it a bare RET: every register as at the entry. */
static int remap_release(es2_sim_loop *l, uint32_t r[3]) {
    if (!r16(l, VA_REMAP_FLAG)) return 0;                            /* 0x46a4ac, 0x46a4b4 */
    uint32_t block = r32(l, VA_REMAP_TABLE);                         /* 0x46a4b6 */
    r[0] = block;
    if (cleanup_call(l, VA_CLEANUP_DELETE, &block, r, &r[2])) return 1;   /* 0x46a4bc, pop ecx */
    r[1] = 0;                                                        /* 0x46a4c2 */
    w32(l, VA_REMAP_CLEARED, 0);                                     /* 0x46a4c4 */
    w16(l, VA_REMAP_FLAG, 0);                                        /* 0x46a4ca */
    return 0;
}
/* 0x461304() (listing 0x461304..0x46134a): the sound close 0x462734 (row
 * sound_close); with the tape stream [0x4d24e2]: _fclose it, EDX 0,
 * [0x4d24e2] <- 0, and then with the dump stream [0x4d24de]: _fclose it,
 * EDX 0, [0x4d24de] <- 0 (both tests jump to 0x46133c: without a tape the
 * dump is not looked at); 0x46a4ac (row remap_release); ECX 0 and the exit
 * code [0x4d27b8] <- 0. EAX and EDX are 0x46a4ac's. */
static int abort_cleanup(es2_sim_loop *l, uint32_t r[3]) {
    if (cleanup_call(l, VA_SOUND_RELEASE, NULL, r, NULL)) return 1;   /* 0x461307 */
    static const uint32_t streams[2] = {VA_CLEANUP_TAPE, VA_CLEANUP_DUMP};
    for (unsigned i = 0; i < 2u; ++i) {
        uint32_t f = r32(l, streams[i]);                             /* 0x46130c, 0x461324 */
        r[0] = f;
        if (!f) break;                                               /* je 0x46133c */
        if (cleanup_call(l, VA_CLEANUP_FCLOSE, &f, r, &r[2])) return 1;   /* 0x461316, 0x46132e; pop ecx */
        r[1] = 0;
        w32(l, streams[i], 0);                                       /* 0x46131e, 0x461336 */
    }
    if (cleanup_call(l, VA_REMAP_RELEASE, NULL, r, NULL)) return 1;   /* 0x46133c */
    r[2] = 0;                                                        /* 0x461341 */
    w32(l, VA_CLEANUP_CODE, 0);                                      /* 0x461343 */
    return 0;
}
void es2_sim_service_abort_cleanup_461304(es2_sim_service_call *call, void *user);
void es2_sim_service_abort_cleanup_461304(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint32_t r[3] = {call->eax, call->edx, call->ecx};
    if (abort_cleanup(l, r)) { failed(call, l); return; }
    call->eax = r[0]; call->edx = r[1]; call->ecx = r[2];
}
void es2_sim_service_remap_release_46a4ac(es2_sim_service_call *call, void *user);
void es2_sim_service_remap_release_46a4ac(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_sim_loop *l = serving(call);
    if (!l) return;
    uint32_t r[3] = {call->eax, call->edx, call->ecx};
    if (remap_release(l, r)) { failed(call, l); return; }
    call->eax = r[0]; call->edx = r[1]; call->ecx = r[2];
}

/* ---- the message instrument (es2/sim/loop.h, main_loop.md, "the
 * windowed notices measured, and the windowed replay"). Records only: it reads
 * the frame counter and the peeks' results, and the post wrapper passes every
 * notice on unchanged. */
static void trace_flush(es2_sim_message_trace *t) {
    if (t->used && t->write && !t->failed && t->write(t->write_user, t->buffer, t->used)) t->failed = 1;
    t->used = 0;
}
static void trace_line(es2_sim_message_trace *t, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void trace_line(es2_sim_message_trace *t, const char *format, ...) {
    if (!t->write || t->failed) return;
    char line[512];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof(line) - 2u, format, args);
    va_end(args);
    if (n < 0 || (unsigned)n >= sizeof(line) - 2u) return;
    line[n++] = '\r';
    line[n++] = '\n';
    if (t->used + (unsigned)n > sizeof(t->buffer)) trace_flush(t);
    memcpy(t->buffer + t->used, line, (size_t)n);
    t->used += (unsigned)n;
}
/* The frame's record (pumps = empty + msgs + inject; this pump has no drop and
 * no PM_NOREMOVE call) and its modal passes. */
static void trace_frame_end(es2_sim_message_trace *t) {
    if (!t->peeks) return;
    unsigned calls = t->empty + t->quits;
    if (calls > 1u) { t->modal_passes += calls - 1u; ++t->frames_modal; }
    if (t->frame_notices > t->most_in_frame) { t->most_in_frame = t->frame_notices; t->most_frame = t->frame; }
    ++t->frames;
    trace_line(t, "{\"k\":\"frame\",\"f\":%lu,\"pumps\":%u,\"empty\":%u,\"msgs\":%u,\"inject\":%u,\"drop\":0,\"noremove\":0}",
               (unsigned long)t->frame, t->peeks, t->empty, t->msgs, t->inject);
    trace_flush(t);
    t->peeks = t->empty = t->msgs = t->inject = t->quits = t->frame_notices = 0;
}
static void trace_note(void *user, const es2_sim_loop *l, int source, uint32_t hwnd, uint32_t message, uint32_t wparam,
                       uint32_t lparam) {
    es2_sim_message_trace *t = user;
    if (t->closed) return;
    uint32_t frame = r32(l, ES2_SIM_VA_FRAME_COUNTER);
    if (!t->started || frame != t->frame) {
        if (t->started) trace_frame_end(t);
        t->started = 1;
        t->frame = frame;
    }
    unsigned p = t->peeks++, pass = t->empty + t->quits;
    if (source == ES2_SIM_PEEK_EMPTY) { ++t->empty; return; }
    uint32_t time = 0;
    if (source == ES2_SIM_PEEK_JOURNAL) ++t->inject;
    else {
        ++t->msgs;
        if (source == ES2_SIM_PEEK_QUIT) ++t->quits;
        time = t->now ? t->now(t->now_user) : 0u;
        if (source == ES2_SIM_PEEK_POSTED && message == ES2_SIM_NOTICE_MESSAGE) {
            /* MSG.time is the post's time. */
            if (t->post_count) {
                time = t->post_time[t->post_head];
                t->post_head = (t->post_head + 1u) % ES2_SIM_TRACE_POST_TIMES;
                --t->post_count;
            }
            ++t->retrieved;
            ++t->frame_notices;
            if (pass) ++t->retrieved_modal;
            else ++t->retrieved_first;
        }
    }
    trace_line(t, "{\"k\":\"%s\",\"f\":%lu,\"p\":%u,\"m\":%lu,\"w\":%lu,\"l\":%lu,\"h\":%lu,\"t\":%lu,\"c\":\"0x464ca2\",\"r\":\"0x464ca7\"}",
               source == ES2_SIM_PEEK_JOURNAL ? "inject" : "msg", (unsigned long)frame, p, (unsigned long)message,
               (unsigned long)wparam, (unsigned long)lparam, (unsigned long)hwnd, (unsigned long)time);
}
void es2_sim_message_trace_attach(es2_sim_message_trace *t, es2_sim_loop *l,
                                  int (*write)(void *user, const char *bytes, unsigned length), void *write_user) {
    memset(t, 0, sizeof(*t));
    t->loop = l;
    t->write = write;
    t->write_user = write_user;
    l->peek_note = trace_note;
    l->peek_note_user = t;
    trace_line(t, "{\"event\":\"start\",\"schema\":1,\"kind\":\"message_trace\",\"key\":\"frame_counter_0x4d257b\","
                  "\"pump\":\"pm_remove_calls_in_frame\",\"source\":\"es2sim\"}");
}
int es2_sim_message_trace_post(void *user, uint32_t window, uint32_t message, uint32_t wparam, uint32_t lparam) {
    es2_sim_message_trace *t = user;
    int refused = es2_sim_loop_post(t->loop, window, message, wparam, lparam);
    if (message != ES2_SIM_NOTICE_MESSAGE) return refused;
    if (refused) { ++t->refused; return refused; }
    ++t->posted;
    if (t->post_count < ES2_SIM_TRACE_POST_TIMES) {
        t->post_time[(t->post_head + t->post_count) % ES2_SIM_TRACE_POST_TIMES] = t->now ? t->now(t->now_user) : 0u;
        ++t->post_count;
    }
    return 0;
}
void es2_sim_message_trace_close(es2_sim_message_trace *t) {
    if (t->closed) return;
    if (t->started) trace_frame_end(t);
    trace_line(t, "{\"event\":\"end\",\"other_thread\":0,\"notices_posted\":%llu,\"notices_refused\":%llu,\"notices_retrieved\":%llu,"
                  "\"retrieved_first_pass\":%llu,\"retrieved_modal\":%llu,\"modal_passes\":%llu,\"frames_modal\":%llu}",
               (unsigned long long)t->posted, (unsigned long long)t->refused, (unsigned long long)t->retrieved,
               (unsigned long long)t->retrieved_first, (unsigned long long)t->retrieved_modal,
               (unsigned long long)t->modal_passes, (unsigned long long)t->frames_modal);
    trace_flush(t);
    t->closed = 1;
}
