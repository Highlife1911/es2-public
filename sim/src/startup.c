/* DBSIM's start-up after the init records as own code (es2/sim/startup.h).
 * Contract: launcher.md, "DBSIM's process shell, start-up and exit" and
 * "DBSIM's start-up as own code"
 * (every store and call below with its listing address range); "listing" =
 * llvm-objdump -d -M intel of the reference DBSIM.EXE (SHA-256
 * 8561aa47...c4ad). */
#include <stdio.h>
#include <string.h>
#include "startup_internal.h"
#include "es2/core/rtl_start.h"

/* ---- guest VAs (DBSIM.EXE) */
#define VA_HINSTANCE 0x004d36c4u
#define VA_EXIT_CODE 0x004d27b8u
#define VA_DIRECTDRAW 0x004d255eu      /* dword: DirectDraw requested (prefs byte 6, -Z1) */
#define VA_CURSOR 0x004d3720u          /* 0x464c68's cursor handle */
#define VA_GAME_W 0x004d253eu
#define VA_GAME_H 0x004d2542u
#define VA_DIB_W 0x004d2546u
#define VA_DIB_H 0x004d254au
#define VA_WINDOW_RECT 0x004d36ccu
#define VA_DISPLAY 0x004d3020u
#define VA_SCREEN_W 0x004d36f4u
#define VA_SCREEN_H 0x004d36f8u
#define VA_HWND 0x004d3610u
#define VA_TIMER 0x004d3608u
#define VA_DD_OBJECTS 0x004d3710u      /* DirectDraw, primary, back buffer, palette: 0x4d3710..0x4d371c */
#define VA_ROOT_LOG 0x006c56dcu        /* 0x479e18: the error report's root */
#define VA_NOTIFY_HEAD 0x006c56d8u
#define VA_EGGPLANT 0x004d2524u
#define VA_TAPE 0x004d24e2u
#define VA_DUMP 0x004d24deu
#define VA_LOW_MEMORY 0x004d2680u
#define VA_TAPE_NAME 0x004d24dau
#define VA_PLAYBACK 0x004d24d6u
#define VA_RECORD 0x004d24d8u
#define VA_DEMO 0x004d2530u
#define VA_PREFS_PATH 0x0049e81cu
#define VA_PREFS_VIDEO 0x0045e510u
#define VA_STAGE_DISPATCH 0x00401d94u
#define VA_STREAM_OPEN 0x004730e8u
#define VA_STREAM_DESTROY 0x00472f64u
#define VA_TAPE_SAVE 0x0045cca4u
#define VA_TAPE_RESTORE 0x0045cd5cu
#define VA_DEMO_PICK 0x0045ceb8u
#define VA_DD_SWITCH 0x004667c8u
#define VA_FRAME_SHOW 0x0046548cu
#define VA_DD_RELEASE 0x00466730u      /* row dd_release (src/present.c) */
#define VA_ARENA 0x00476df4u
#define VA_STRICMP 0x00496868u
#define VA_STRNCPY 0x00496760u
#define VA_PATH_EXT 0x00492b90u

/* ---- shared helpers (startup_internal.h) */
int su_call(es2_sim_startup *s, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    uint32_t regs[3] = {0, 0, 0};
    es2_sim_result r;
    /* The argument slots go into the frame area: the services that read
     * them by guest address (the runtime models) find them there, and
     * es2_sim_call hands on their host address, which the frame area's
     * mapping makes the guest address (identity in es2sim and in the tests'
     * arena). */
    uint32_t top = s->frame_hi, at = su_frame_push(s, 4u * (count ? count : 1u));
    for (unsigned i = 0; i < count; ++i) su_w32(s, at + 4u * i, slots[i]);
    int refused = es2_sim_call(s->dispatch, va, s->memory->host(s->memory->context, at), count, regs, &r);
    su_frame_pop(s, top);
    if (refused) {
        s->outcome = ES2_SIM_STARTUP_REFUSED;
        s->refused_va = va;
        return 1;
    }
    if (eax) *eax = r.eax;
    return 0;
}
int su_refuse(es2_sim_startup *s, uint32_t va, const char *name, const char *reason) {
    es2_sim_refusal refusal = {va, name, reason, 0, 0};
    ++s->dispatch->refusals;
    if (s->dispatch->refuse) s->dispatch->refuse(&refusal, s->dispatch->refuse_user);
    s->outcome = ES2_SIM_STARTUP_REFUSED;
    s->refused_va = va;
    return 1;
}
int su_null_refuse(es2_sim_startup *s, uint32_t va, const char *name, const char *reason) {
    if (!s->process || !s->process->process_fault) return su_refuse(s, va, name, reason);
    s->process->process_fault(s->process->user, ES2_SIM_ACCESS_VIOLATION, va);
    s->exit_code = ES2_SIM_ACCESS_VIOLATION;
    s->outcome = ES2_SIM_STARTUP_EXIT;
    return 1;
}
/* The frame area grows down from frame_hi; blocks are zero-filled (the
 * original's locals hold stack residue: own_process.md section 5; none of
 * these is read before it is written except where noted). */
uint32_t su_frame_push(es2_sim_startup *s, uint32_t bytes) {
    bytes = (bytes + 3u) & ~3u;
    if (s->frame_hi - s->frame_lo < bytes) return s->frame_lo;   /* the callers give at least 0x2000 bytes; not reached */
    s->frame_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) su_w32(s, s->frame_hi + i, 0);
    return s->frame_hi;
}
void su_frame_pop(es2_sim_startup *s, uint32_t to) { s->frame_hi = to; }

static void event(es2_sim_startup *s, const char *kind, const char *text) {
    if (s->event) s->event(s->event_user, kind, text);
}
static void guest_text(const es2_sim_startup *s, uint32_t p, char *out, unsigned capacity) {
    unsigned n = 0;
    while (p && n + 1 < capacity) {
        uint8_t b = su_r8(s, p + n);
        if (!b) break;
        out[n++] = (char)b;
    }
    out[n] = 0;
}

void es2_sim_startup_defaults(es2_sim_startup *s) {
    memset(&s->platform, 0, sizeof(s->platform));
    s->platform.screen_cx = 640;
    s->platform.screen_cy = 480;
    s->platform.frame_cx = 4;
    s->platform.frame_cy = 4;
    s->platform.caption = 18;
    s->platform.total_phys = 0x04000000u;
    s->platform.window = 0x00010001u;
    s->platform.cursor_wait = 0x00010003u;
    s->platform.cursor_arrow = 0x00010005u;
    s->platform.class_atom = 0xc001u;
    s->defer_dib = 1;
    s->outcome = ES2_SIM_STARTUP_RETURNED;
}

/* ---- path helpers (static result buffers, as the original) */

static uint32_t path_root(es2_sim_startup *s, uint32_t p) {
    uint32_t regs[3];
    es2_sim_path_root(s->memory, p, regs);
    return regs[0];
}
/* 0x45eec0(n): "tapes\" (playback word set) or "data\" + n into 0x4d28fc. */
static uint32_t path_tapes(es2_sim_startup *s, uint32_t n) {
    su_strcpy(s, 0x004d28fcu, su_r16(s, VA_PLAYBACK) ? 0x0049f3efu : 0x0049f3f6u);
    su_strcat(s, 0x004d28fcu, n);
    return 0x004d28fcu;
}
/* 0x492b90(n, ext): n up to its first '.' (0x492b68 with strchr), "." and
 * ext, in 0x6cbc0c. */
static uint32_t path_ext(es2_sim_startup *s, uint32_t n, uint32_t ext) {
    su_strcpy(s, 0x006cbc0cu, n);
    for (uint32_t p = 0x006cbc0cu;; ++p) {
        uint8_t b = su_r8(s, p);
        if (b == '.') { su_w8(s, p, 0); break; }
        if (!b) break;
    }
    su_strcat(s, 0x006cbc0cu, 0x004a6939u);
    su_strcat(s, 0x006cbc0cu, ext);
    return 0x006cbc0cu;
}

/* ---- the fatal report 0x479c78 (severities 4..6; the start-up passes 5) */

/* 0x479e8c(text): root + "assert.log" (0x479e48 into 0x6c57dc), _open
 * 0x901/0x180 (a failure calls _abort), the separator once (flag 0x4a1610),
 * the text, CR LF (bytes 0x4a1614), _close. */
static int assert_log(es2_sim_startup *s, uint32_t text) {
    su_strcpy(s, 0x006c57dcu, VA_ROOT_LOG);
    su_strcat(s, 0x006c57dcu, 0x004a1540u);
    uint32_t a[3] = {0x006c57dcu, 0x901u, 0x180u}, h = 0;
    if (su_call(s, VA_OPEN, a, 3, &h)) return 1;
    if ((int32_t)h < 0) {
        /* _abort: refused headless; with a process record that has process_exit
         * its box and __exit(3). */
        if (!s->process || !s->process->process_exit) return su_call(s, 0x0049688cu, NULL, 0, NULL);
        ++s->platform.message_box;
        event(s, "MessageBoxA", ES2_SIM_ABORT_TEXT);
        s->exit_code = es2_sim_process_abort(s->process);
        s->outcome = ES2_SIM_STARTUP_EXIT;
        return 1;
    }
    if (su_r32(s, 0x004a1610u)) {
        uint32_t w[3] = {h, 0x004a154bu, 0x50u};
        if (su_call(s, VA_WRITE, w, 3, NULL)) return 1;
        su_w32(s, 0x004a1610u, 0);
    }
    uint32_t w[3] = {h, text, su_strlen(s, text)};
    if (su_call(s, VA_WRITE, w, 3, NULL)) return 1;
    uint32_t crlf = su_frame_push(s, 4u);
    su_w16(s, crlf, su_r16(s, 0x004a1614u));
    su_w8(s, crlf + 2u, su_r8(s, 0x004a1616u));
    uint32_t w2[3] = {h, crlf, 2u};
    if (su_call(s, VA_WRITE, w2, 3, NULL)) return 1;
    uint32_t c[1] = {h};
    return su_call(s, VA_CLOSE, c, 1, NULL);
}
/* 0x479dd0(buf, severity, file, line, text): "Assert::%s: Line %d in "%s""
 * with the severity's name, then ": %s" with the text when there is one. */
static int assert_text(es2_sim_startup *s, uint32_t buf, uint32_t severity, uint32_t file, uint32_t line, uint32_t text) {
    uint32_t a[16] = {buf, 0x004a1635u, su_r32(s, 0x004a15f4u + 4u * severity), line, file}, n = 0;
    if (su_call(s, VA_SPRINTF, a, 16, &n)) return 1;
    if (!text) return 0;
    uint32_t b[16] = {buf + n, 0x004a1651u, text};
    return su_call(s, VA_SPRINTF, b, 16, NULL);
}
/* MessageBoxA(0, text, caption, style): recorded (the first 255 and 63 bytes)
 * and, with a process record, shown with the whole texts; the answer (headless
 * IDOK) is not read by either caller. */
static void message_box(es2_sim_startup *s, uint32_t text, uint32_t caption, uint32_t style) {
    ++s->platform.message_box;
    guest_text(s, text, s->platform.message_text, sizeof(s->platform.message_text));
    guest_text(s, caption, s->platform.message_caption, sizeof(s->platform.message_caption));
    event(s, "MessageBoxA", s->platform.message_text);
    if (s->process && s->process->message_box) {
        static char t[0x400], c[0x100];
        guest_text(s, text, t, sizeof(t));
        guest_text(s, caption, c, sizeof(c));
        (void)es2_sim_process_message_box(s->process, 0, t, c, style);
    }
}
/* 0x4669b4: back to GDI when DirectDraw is on (the switch 0x4667c8). */
static int back_to_gdi(es2_sim_startup *s) {
    return su_r32(s, VA_DIRECTDRAW) ? su_call(s, VA_DD_SWITCH, NULL, 0, NULL) : 0;
}
static int fatal(es2_sim_startup *s, uint32_t severity, uint32_t file, uint32_t line, uint32_t text) {
    /* 0x479c54: the notify list, last registered first, fn(severity). */
    for (uint32_t node = su_r32(s, VA_NOTIFY_HEAD); node; node = su_r32(s, node)) {
        uint32_t a[1] = {severity};
        if (su_call(s, su_r32(s, node + 4u), a, 1, NULL)) return 1;
    }
    /* 0x479f24 for severity 5 (0x47a00b..0x47a0a0). */
    uint32_t top = s->frame_hi;
    uint32_t buf = su_frame_push(s, 0x200u), box = su_frame_push(s, 0x3e8u);
    if (severity == 5u) {
        if (back_to_gdi(s)) return 1;
        su_strcpy(s, box, 0x004a159cu);
        su_strcat(s, box, 0x004a1662u);
        if (assert_text(s, buf, severity, file, line, text)) return 1;
        su_strcat(s, box, buf);
        message_box(s, box, 0x004a1664u, 0);   /* push 0 x2 at 0x47a08e */
        if (assert_log(s, buf)) return 1;
    } else {
        su_frame_pop(s, top);
        return su_refuse(s, 0x00479f24u, "error report", "a severity other than 5 at start-up");
    }
    su_frame_pop(s, top);
    /* 0x479f24(1, 1, 0, 0, 0): nothing; 0x4650cc: ret; 0x4650d0: KillTimer
     * only with [0x4d3608] (never written); PostQuitMessage(0); __exit(1). */
    if (su_r32(s, VA_TIMER)) return su_refuse(s, 0x004650d9u, "KillTimer", "a timer at the fatal report");
    ++s->platform.post_quit;
    event(s, "fatal", s->platform.message_text);
    /* __exit(1): the process ends here with a process record that has
     * process_exit; a recorder returns, and so does headless. */
    if (s->process && s->process->process_exit) s->process->process_exit(s->process->user, ES2_SIM_FATAL_STATUS);
    s->outcome = ES2_SIM_STARTUP_EXIT;
    s->exit_code = ES2_SIM_FATAL_STATUS;
    return 1;
}

/* ---- WinMain's pieces */

/* 0x4652f8(cmd, argv) (listing 0x4652f8..0x46538b). */
int es2_sim_startup_split(es2_sim_startup *s, uint32_t command, uint32_t argv, uint32_t *argc) {
    uint32_t count = 1, slot = argv + 4u;
    su_w32(s, argv, 0x004a0876u);
    for (uint32_t p = command; su_r8(s, p);) {
        if (su_r8(s, p) == ' ') { ++p; continue; }
        /* strchr(p, ' ') (thunk 0x4967a2), else strlen + 1 (0x496766). */
        uint32_t run = 0;
        while (su_r8(s, p + run) && su_r8(s, p + run) != ' ') ++run;
        if (count > 26u)
            return su_refuse(s, 0x00465351u, "command line split", "more than 26 arguments (the original writes past its argv array)");
        uint32_t a[1] = {run + 2u}, block = 0;
        if (su_call(s, VA_NEW_ARRAY, a, 1, &block)) return 1;
        su_w32(s, slot, block);
        slot += 4u;
        ++count;
        for (uint32_t i = 0; i < run; ++i) su_w8(s, block + i, su_r8(s, p + i));
        su_w8(s, block + run, 0);
        p += run;
    }
    *argc = count;
    return 0;
}

/* 0x45e6cc(argc, argv) (listing 0x45e6cc..0x45e757). */
int es2_sim_startup_early_parser(es2_sim_startup *s, uint32_t argc, uint32_t argv) {
    for (uint32_t i = 1; (int32_t)i < (int32_t)argc; ++i) {
        uint32_t a = su_r32(s, argv + 4u * i);
        if (su_r8(s, a) != '-') continue;
        switch (su_r8(s, a + 1u)) {
        case 'X': {
            uint32_t v[1] = {a + 2u}, n = 0;
            if (su_call(s, VA_ATOL, v, 1, &n)) return 1;
            su_w32(s, VA_EXIT_CODE, n);
            break;
        }
        case 'Z': su_w32(s, VA_DIRECTDRAW, su_r8(s, a + 2u) == '1' ? 1u : 0u); break;
        case 'c': su_w16(s, 0x004d252eu, 1); break;
        case 'v': {
            /* AL = the digit - '0', bits 8..31 zero (EAX held the sign-
             * extended '-' after movsx; 0x45e729..0x45e72d). */
            uint32_t v[1] = {(uint32_t)(uint8_t)(su_r8(s, a + 2u) - '0')};
            if (su_call(s, VA_PREFS_VIDEO, v, 1, NULL)) return 1;
            break;
        }
        default: break;
        }
    }
    return 0;
}

/* The -p/-D restore (0x45e9fe..0x45ebdf). */
static int tape_restore_all(es2_sim_startup *s, uint32_t frame) {
    uint32_t a[3] = {VA_PREFS_PATH, path_root(s, path_tapes(s, 0x0049f2b2u)), 0xffu};
    if (su_call(s, VA_STRNCPY, a, 3, NULL)) return 1;
    static const struct { uint32_t name; int tapes; } files[7] = {
        {0x0049f2bcu, 0}, {0x0049f2ccu, 0}, {0x0049f2dcu, 0}, {0x0049f2edu, 1}, {0x0049f2f7u, 0}, {0x0049f308u, 0}, {0x0049f318u, 1}};
    for (unsigned i = 0; i < 7u; ++i) {
        uint32_t path = path_root(s, files[i].tapes ? path_tapes(s, files[i].name) : files[i].name);
        uint32_t r[2] = {su_r32(s, VA_TAPE), path};
        if (su_call(s, VA_TAPE_RESTORE, r, 2, NULL)) return 1;
    }
    /* Bytes 0..3 and 8..10 of data\prefs.cfg into tapes\prefs.cfg. */
    uint32_t original = frame + 0x90u - 0x58u, copy = frame + 0x90u - 0x90u;
    uint32_t o[3] = {path_root(s, 0x0049f323u), 0x0049f332u, 0}, f1 = 0;
    if (su_call(s, VA_FOPEN, o, 2, &f1)) return 1;
    if (!f1) return 0;
    uint32_t rd[4] = {original, 1, 0x36u, f1};
    if (su_call(s, VA_FREAD, rd, 4, NULL)) return 1;
    uint32_t o2[3] = {path_root(s, 0x0049f335u), 0x0049f345u, 0}, f2 = 0;
    if (su_call(s, VA_FOPEN, o2, 2, &f2)) return 1;
    if (f2) {
        uint32_t rd2[4] = {copy, 1, 0x36u, f2};
        if (su_call(s, VA_FREAD, rd2, 4, NULL)) return 1;
        uint32_t c[1] = {f2};
        if (su_call(s, VA_FCLOSE, c, 1, NULL)) return 1;
        for (uint32_t i = 0; i < 0x36u; ++i)
            if (i < 4u || i - 8u < 3u) su_w8(s, copy + i, su_r8(s, original + i));
        uint32_t o3[3] = {path_root(s, 0x0049f348u), 0x0049f358u, 0}, f3 = 0;
        if (su_call(s, VA_FOPEN, o3, 2, &f3)) return 1;
        if (f3) {
            uint32_t wr[4] = {copy, 1, 0x36u, f3};
            if (su_call(s, VA_FWRITE, wr, 4, NULL)) return 1;
            uint32_t c3[1] = {f3};
            if (su_call(s, VA_FCLOSE, c3, 1, NULL)) return 1;
        }
    }
    uint32_t c1[1] = {f1};
    return su_call(s, VA_FCLOSE, c1, 1, NULL);
}

static int atol_at(es2_sim_startup *s, uint32_t p, uint32_t *value) {
    uint32_t a[1] = {p};
    return su_call(s, VA_ATOL, a, 1, value);
}

/* 0x45e758(argc, argv) (listing 0x45e758..0x45edad, jump table 0x45e7a3 /
 * 0x45e7d6 over the second byte 'B'..'t'). */
static int main_option(es2_sim_startup *s, uint32_t a, uint32_t frame) {
    uint32_t v = 0;
    switch (su_r8(s, a + 1u)) {
    case 'R': if (atol_at(s, a + 2u, &v)) return 1; su_w32(s, 0x004d2573u, v); break;
    case 'b': su_w16(s, 0x004d2566u, 0); break;
    case 'B': su_w16(s, 0x004d252cu, 1); break;
    case 'S':
        if (su_streq(s, a + 1u, 0x0049f299u)) su_w32(s, 0x0049ef38u, su_r32(s, 0x0049ef38u) == 0 ? 1u : 0u);
        else su_w8(s, 0x004d24c7u, 1);
        break;
    case 'm': su_w8(s, 0x004d267du, 1); break;
    case 'C': {
        /* 0x42d2b8(name): the first of 13 names [0x49ac64 + 4i] equal under
         * _stricmp: word 0x49ac4c = i, dword 0x49ac50 = name. */
        uint32_t name = a + 2u;
        for (uint32_t i = 0; i < 13u; ++i) {
            uint32_t c[2] = {name, su_r32(s, 0x0049ac64u + 4u * i)}, r = 0;
            if (su_call(s, VA_STRICMP, c, 2, &r)) return 1;
            if (!r) { su_w16(s, 0x0049ac4cu, (uint16_t)i); su_w32(s, 0x0049ac50u, name); break; }
        }
        break;
    }
    case 's': su_w8(s, 0x004d24c8u, (uint8_t)(su_r8(s, 0x004d24c8u) ^ 1u)); break;
    case 'P': su_w8(s, 0x004d24c9u, 1); break;
    case 't': if (atol_at(s, a + 2u, &v)) return 1; su_w32(s, 0x0049d248u, v); break;   /* 0x44bac4(v) */
    case 'V': if (atol_at(s, a + 2u, &v)) return 1; su_w16(s, 0x004d2512u, (uint16_t)v); break;
    case 'W': if (atol_at(s, a + 2u, &v)) return 1; su_w16(s, 0x004d2514u, (uint16_t)v); break;
    case 'T': if (atol_at(s, a + 2u, &v)) return 1; su_w16(s, 0x004d2516u, (uint16_t)v); break;
    case 'D': {
        uint32_t name = 0;
        if (su_call(s, VA_DEMO_PICK, NULL, 0, &name)) return 1;
        su_w32(s, VA_TAPE_NAME, name);
        if (name) {
            su_w16(s, VA_DEMO, 1);
            su_w16(s, VA_PLAYBACK, 1);
            uint32_t path = path_root(s, path_tapes(s, path_ext(s, su_r32(s, VA_TAPE_NAME), 0x0049f2a4u)));
            uint32_t o[3] = {path, 0x0049f2a8u, 0}, f = 0;
            if (su_call(s, VA_FOPEN, o, 2, &f)) return 1;
            su_w32(s, VA_TAPE, f);
            if (su_r32(s, VA_TAPE)) return tape_restore_all(s, frame);
        }
        su_w16(s, VA_DEMO, 0);
        su_w16(s, VA_PLAYBACK, 0);
        break;
    }
    case 'p': {
        if (!su_r32(s, VA_TAPE_NAME)) su_w32(s, VA_TAPE_NAME, a + 2u);
        uint32_t o[3] = {path_ext(s, su_r32(s, VA_TAPE_NAME), 0x0049f2abu), 0x0049f2afu, 0}, f = 0;
        if (su_call(s, VA_FOPEN, o, 2, &f)) return 1;
        su_w32(s, VA_TAPE, f);
        su_w16(s, VA_PLAYBACK, 1);
        return tape_restore_all(s, frame);
    }
    case 'r': {
        if (!su_r32(s, VA_TAPE_NAME)) su_w32(s, VA_TAPE_NAME, a + 2u);
        uint32_t o[3] = {path_ext(s, su_r32(s, VA_TAPE_NAME), 0x0049f35bu), 0x0049f35fu, 0}, f = 0;
        if (su_call(s, VA_FOPEN, o, 2, &f)) return 1;
        static const uint32_t files[7] = {0x0049f362u, 0x0049f372u, 0x0049f382u, 0x0049f393u, 0x0049f3a2u, 0x0049f3b3u, 0x0049f3c3u};
        for (unsigned i = 0; i < 7u; ++i) {
            uint32_t t[2] = {f, path_root(s, files[i])};
            if (su_call(s, VA_TAPE_SAVE, t, 2, NULL)) return 1;
        }
        uint32_t c[1] = {f};
        if (su_call(s, VA_FCLOSE, c, 1, NULL)) return 1;
        su_w16(s, VA_RECORD, 1);
        break;
    }
    case 'd': {
        uint32_t mode = su_r16(s, VA_RECORD) ? 0x0049f3d3u : 0x0049f3d6u;
        uint32_t o[3] = {path_ext(s, su_r32(s, VA_TAPE_NAME), 0x0049f3d9u), mode, 0}, f = 0;
        if (su_call(s, VA_FOPEN, o, 2, &f)) return 1;
        su_w32(s, VA_DUMP, f);
        if (su_r16(s, VA_RECORD)) {
            uint32_t c[1] = {su_r32(s, VA_DUMP)};
            if (su_call(s, VA_FCLOSE, c, 1, NULL)) return 1;   /* the stale pointer stays as a flag */
        }
        break;
    }
    case 'l': su_w16(s, VA_LOW_MEMORY, 1); break;
    case 'e':
        /* "-EGGPLANT" never reaches this case: its second byte selects 'E'. */
        if (su_streq(s, a + 1u, 0x0049f3ddu) || su_streq(s, a + 1u, 0x0049f3e6u)) su_w16(s, VA_EGGPLANT, 1);
        break;
    case 'E': su_w8(s, 0x004d2536u, 's'); break;
    case 'F': su_w8(s, 0x004d2536u, 'f'); break;
    case 'G': su_w8(s, 0x004d2536u, 'g'); break;
    case 'a': su_w8(s, 0x004d2539u, 0); break;
    default: break;
    }
    return 0;
}
int es2_sim_startup_main_parser(es2_sim_startup *s, uint32_t argc, uint32_t argv) {
    uint32_t top = s->frame_hi, frame = su_frame_push(s, 0x90u);   /* [ebp-0x90..ebp) */
    int refused = 0;
    for (uint32_t i = 1; !refused && (int32_t)i < (int32_t)argc; ++i) {
        uint32_t a = su_r32(s, argv + 4u * i);
        if (su_r8(s, a) == '-') refused = main_option(s, a, frame);
    }
    su_frame_pop(s, top);
    return refused;
}

/* ---- error.str: the string holder 0x453824 / 0x453878 / 0x4539a0 / 0x453918 */

/* 0x453878(holder, name): the VOL-aware stream S on the stack (0x472f14:
 * base class, vtable 0x4a0e10, fields, handle 0xffff, open flag 0, then the
 * hooked open 0x4730e8, counter + 1), 0x4538d0 (the size dword into
 * holder+8, new[] of it into holder+0, the bytes, holder+4 = holder+0; AL
 * 1 whatever the reads did), the hooked destructor 0x472f64(S, 2). */
static int load_strings(es2_sim_startup *s, uint32_t holder, uint32_t name) {
    uint32_t top = s->frame_hi, stream = su_frame_push(s, 0x30u);
    su_w32(s, stream, 0x004a16e8u);
    su_w32(s, stream + 4u, 0);
    su_counter(s, 1);
    su_w32(s, stream, 0x004a0e10u);
    su_w32(s, stream + 0x27u, 0);
    su_w32(s, stream + 0x2bu, 0);
    su_w32(s, stream + 0xau, 0);
    su_w32(s, stream + 0xeu, 0);
    su_w32(s, stream + 0x16u, 0);
    su_w16(s, stream + 8u, 0xffffu);
    su_w8(s, stream + 0x26u, 0);
    uint32_t o[2] = {stream, name};
    int refused = su_call(s, VA_STREAM_OPEN, o, 2, NULL);
    if (!refused) {
        su_counter(s, 1);
        uint32_t read = su_r32(s, su_r32(s, stream) + 0x18u);
        uint32_t r1[3] = {stream, 4u, holder + 8u};
        refused = su_call(s, read, r1, 3, NULL);
        uint32_t n[1] = {su_r32(s, holder + 8u)}, buf = 0;
        if (!refused) refused = su_call(s, VA_NEW_ARRAY, n, 1, &buf);
        if (!refused) {
            su_w32(s, holder, buf);
            uint32_t r2[3] = {stream, su_r32(s, holder + 8u), buf};
            refused = su_call(s, read, r2, 3, NULL);
        }
        if (!refused) {
            su_w32(s, holder + 4u, su_r32(s, holder));
            uint32_t d[2] = {stream, 2u};
            refused = su_call(s, VA_STREAM_DESTROY, d, 2, NULL);
        }
    }
    su_frame_pop(s, top);
    return refused;
}
/* 0x453918(h, out, out2): the next string of the current group. */
static int next_string(es2_sim_startup *s, uint32_t h, uint32_t out, uint32_t out2) {
    uint32_t cursor = su_r32(s, h + 4u);
    if (!(su_r32(s, h) + su_r32(s, h + 8u) > cursor)) return 0;
    if (!su_r16(s, h + 0xcu)) {
        su_w16(s, h + 0xcu, su_r16(s, cursor));
        cursor += 2u;
        su_w32(s, h + 4u, cursor);
        su_w16(s, h + 0xeu, 0);
    }
    if ((int16_t)su_r16(s, h + 0xeu) >= (int16_t)su_r16(s, h + 0xcu)) {
        su_w16(s, h + 0xeu, 0);
        su_w16(s, h + 0xcu, 0);
        return 0;
    }
    int16_t length = (int16_t)su_r16(s, cursor);
    cursor += 2u;
    su_w32(s, out, cursor);
    uint32_t p = cursor + (uint32_t)(int32_t)length;
    int8_t extra = (int8_t)su_r8(s, p);
    ++p;
    if (out2) { su_w32(s, out2, p); p += (uint32_t)(int32_t)extra; }
    su_w32(s, h + 4u, p);
    su_w16(s, h + 0xeu, (uint16_t)(su_r16(s, h + 0xeu) + 1u));
    return 1;
}
/* 0x4539a0(h, array, n, out2): as many strings as the group's count word
 * says (n is not read); the count word of the holder cleared after. */
int es2_sim_startup_read_group(es2_sim_startup *s, uint32_t h, uint32_t array, uint32_t limit, uint32_t out2) {
    int16_t count = (int16_t)su_r16(s, su_r32(s, h + 4u));
    for (int32_t i = 0; i < count; ++i) {
        if (array + 4u * (uint32_t)i + 4u > limit)
            return su_refuse(s, 0x004539c4u, "error.str group", "more strings than the frame holds before the holder");
        next_string(s, h, array + 4u * (uint32_t)i, out2 ? out2 + 4u * (uint32_t)i : 0);
    }
    su_w16(s, h + 0xcu, 0);
    return 0;
}

/* ---- the game main's start-up part (0x45f160..0x45f42b) */
static int game_main(es2_sim_startup *s, uint32_t argc, uint32_t argv) {
    uint32_t ebp = su_frame_push(s, 0x1c4u) + 0x1c4u;
#define L(k) (ebp - (k))
    su_w16(s, L(0x3e), 0);
    /* drive.cfg (0x45f180..0x45f26a). */
    uint32_t o[3] = {0x0049f442u, 0x0049f451u, 0}, f = 0;
    if (su_call(s, VA_FOPEN, o, 2, &f)) return 1;
    if (!f) return fatal(s, 5, 0x0049f454u, 0x23cu, 0x0049f442u);
    uint32_t sc[SU_SCAN_SLOTS] = {f, 0x0049f45du, L(0x90)}, r = 0;
    if (su_call(s, VA_FSCANF, sc, SU_SCAN_SLOTS, &r)) return 1;
    su_strcat(s, L(0x90), 0x0049f460u);
    su_w32(s, L(0x3c), r ? L(0x90) : 0x0049f462u);
    if (su_strlen(s, L(0x90)) + 1u > 0x50u)
        return su_refuse(s, 0x0045f1d7u, "drive.cfg", "a first token longer than the original's 80-byte buffer");
    uint32_t sc2[SU_SCAN_SLOTS] = {f, 0x0049f463u, L(0xe0)};
    if (su_call(s, VA_FSCANF, sc2, SU_SCAN_SLOTS, NULL)) return 1;
    /* The second token is read into [ebp-0xe0] (80 bytes) and runs into the
     * first buffer when longer: the original's overlap is kept (the root
     * copy below happens after this read), beyond both buffers refused. */
    if (su_strlen(s, L(0xe0)) + 1u > 0xa0u)
        return su_refuse(s, 0x0045f21eu, "drive.cfg", "a second token longer than the original's two 80-byte buffers");
    su_strcpy(s, ES2_SIM_VA_DRIVE_ROOT, L(0x90));
    su_w8(s, ES2_SIM_VA_INSTALL_ROOT, su_r8(s, 0x0049f466u));
    su_strcpy(s, VA_ROOT_LOG, ES2_SIM_VA_INSTALL_ROOT);       /* 0x479e18 */
    su_w32(s, VA_EXIT_CODE, 0);
    if (es2_sim_startup_main_parser(s, argc, argv)) return 1;
    uint32_t st[1] = {0};
    if (su_call(s, VA_STAGE_DISPATCH, st, 1, NULL)) return 1;
    su_w32(s, 0x004a088cu, 0x200u >= 0x80u ? 0x200u : 0x80u); /* 0x467c34(0x200) */
    if (es2_sim_startup_register_volumes(s, 1, 0x0049f467u, su_r32(s, L(0x3c)), (uint32_t)(int32_t)(int16_t)su_r16(s, L(0x3e))))
        return 1;
    st[0] = 1;
    if (su_call(s, VA_STAGE_DISPATCH, st, 1, NULL)) return 1;
    /* error.str (0x45f2b8..0x45f330): 0x453824 (five zero stores, counter
     * + 1), 0x453878 always answers AL 1 (0x45390e), so the fatal branch at
     * 0x45f30f is never taken. */
    uint32_t holder = L(0x10);
    su_w16(s, L(0x20), 8);
    su_w32(s, holder + 4u, 0); su_w32(s, holder, 0); su_w32(s, holder + 8u, 0);
    su_w16(s, holder + 0xeu, 0); su_w16(s, holder + 0xcu, 0);
    su_counter(s, 1);
    if (load_strings(s, holder, 0x0049f46cu)) return 1;
    if (es2_sim_startup_read_group(s, holder, L(0x110), L(0x10), 0) || es2_sim_startup_read_group(s, holder, L(0x11c), L(0x10), 0))
        return 1;
    /* The -eggplant check (0x45f333..0x45f394). */
    if (!su_r16(s, VA_EGGPLANT)) {
        uint32_t tape = su_r32(s, VA_TAPE);
        if (tape) {
            uint32_t c[1] = {tape};
            if (su_call(s, VA_FCLOSE, c, 1, NULL)) return 1;
            su_w32(s, VA_TAPE, 0);
            uint32_t dump = su_r32(s, VA_DUMP);
            if (dump) {
                c[0] = dump;
                if (su_call(s, VA_FCLOSE, c, 1, NULL)) return 1;
                su_w32(s, VA_DUMP, 0);
            }
        }
        if (back_to_gdi(s)) return 1;
        message_box(s, su_r32(s, L(0x100)), su_r32(s, L(0x11c)), 0x30u);   /* MB_ICONEXCLAMATION, owner 0 (0x45f377) */
        /* 0x453848(holder, 2): counter - 1, delete[] the buffer. */
        su_counter(s, -1);
        uint32_t buffer = su_r32(s, holder);
        if (buffer) {
            uint32_t d[1] = {buffer};
            if (su_call(s, VA_DELETE_ARRAY, d, 1, NULL)) return 1;
        }
        return 0;
    }
    /* Memory (0x45f399..0x45f428). */
    ++s->platform.memory_status;
    uint32_t total = s->platform.total_phys;
    su_w32(s, L(0x13c), 0x20u);
    su_w32(s, L(0x134), total);
    if (su_r32(s, L(0x134)) < 0x73a000u) return fatal(s, 5, 0x0049f4a3u, 0x291u, 0x0049f4acu);
    if (su_r32(s, L(0x134)) < 0xbb8000u) su_w16(s, VA_LOW_MEMORY, 1);
    if (su_r16(s, VA_LOW_MEMORY)) {
        uint32_t ar[3] = {0x006bb2f8u, 0xb0000u, 0x00604f04u};
        if (su_call(s, VA_ARENA, ar, 3, NULL)) return 1;
        su_w8(s, 0x004d256cu, 0);
    }
    /* The mission load (0x45f42b). */
    if (s->defer_dib)
        return su_refuse(s, ES2_SIM_VA_DIB_SECTION, "DIB section",
                         "deferred headless: the frame-buffer objects of 0x464d44 are not built, so the start-up stops before the mission load 0x461608");
    if (su_call(s, ES2_SIM_VA_MISSION_LOAD, NULL, 0, NULL)) return 1;
    if (!s->after_load) {
        s->outcome = ES2_SIM_STARTUP_LOADED;
        return 1;
    }
    /* The rest of the game main (the loop and its tail, which also frees the
     * error.str holder); the game main then returns to WinMain. */
    uint32_t refused = 0;
    switch (s->after_load(s->own_user, ebp, &refused)) {
    case 0: return 0;
    case 1: s->outcome = ES2_SIM_STARTUP_STOPPED; return 1;
    default: s->outcome = ES2_SIM_STARTUP_REFUSED; s->refused_va = refused; return 1;
    }
#undef L
}

/* ---- the window, headless (0x465158 with 0x464c10, 0x464cc4) */
static uint32_t half(uint32_t v) { return (uint32_t)((int32_t)v / 2); }   /* sar with the carry added back */
static int window(es2_sim_startup *s, uint32_t instance) {
    const es2_sim_startup_platform *p = &s->platform;
    /* SetRect(r, 0, 0, game w, game h); AdjustWindowRectEx(r, 0xce0000, 0, 0):
     * headless the frame and caption metrics around the client area. */
    uint32_t w = su_r32(s, VA_GAME_W), h = su_r32(s, VA_GAME_H);
    uint32_t left = 0u - p->frame_cx, top = 0u - p->frame_cy - p->caption, right = w + p->frame_cx, bottom = h + p->frame_cy;
    /* 0x464c10(left, top, right, bottom, 0, 0x4d36cc): centred,
     * SetRect(0x4d36cc, x, y, width, height). */
    uint32_t rw = right - left, rh = bottom - top;
    su_w32(s, VA_WINDOW_RECT, half(p->screen_cx - rw));
    su_w32(s, VA_WINDOW_RECT + 4u, half(p->screen_cy - rh));
    su_w32(s, VA_WINDOW_RECT + 8u, rw);
    su_w32(s, VA_WINDOW_RECT + 0xcu, rh);
    /* 0x464cc4: the display record 0x4d3020 (stores in listing order). */
    uint32_t d = VA_DISPLAY;
    su_w32(s, d, 0); su_w32(s, d + 4u, 0); su_w32(s, d + 8u, 0); su_w32(s, d + 0x14u, 0); su_w32(s, d + 0x18u, 0);
    su_w32(s, d + 0x20u, su_r32(s, VA_DIB_W)); su_w32(s, d + 0x24u, su_r32(s, VA_DIB_H));
    su_w32(s, d + 0x28u, 0); su_w32(s, d + 0xcu, 0); su_w32(s, d + 0x5e8u, 0); su_w32(s, d + 0x2cu, 0x28u);
    su_w32(s, d + 0x30u, 0); su_w32(s, d + 0x34u, 0); su_w16(s, d + 0x38u, 1); su_w16(s, d + 0x3au, 8);
    su_w32(s, d + 0x3cu, 0); su_w32(s, d + 0x40u, 0); su_w32(s, d + 0x44u, 0); su_w32(s, d + 0x48u, 0);
    su_w32(s, d + 0x4cu, 0); su_w32(s, d + 0x50u, 0);
    uint32_t cx = w, cy = h;
    if (su_r32(s, VA_DIRECTDRAW)) {
        su_w32(s, 0x004d36fcu, 1);
        su_w32(s, 0x004d3700u, 1);
        cx = p->screen_cx;
        cy = p->screen_cy;
    }
    (void)cx; (void)cy;   /* the window's position and size go only to CreateWindowExA */
    su_w32(s, VA_SCREEN_W, p->screen_cx);
    su_w32(s, VA_SCREEN_H, p->screen_cy);
    /* CreateWindowExA(0, "ES2", "EarthSiege II", 0x100e0000, ...): a stand-in
     * handle. SetWindowPos (DirectDraw mode), the DIB section 0x464d44,
     * ShowWindow and UpdateWindow (the synchronous WM_PAINT) need a window:
     * the DIB section is deferred (es2_sim_startup.defer_dib), the others
     * have no headless effect on guest state. */
    ++s->platform.create_window;
    su_w32(s, VA_HWND, p->window);
    (void)instance;
    if (s->defer_dib) return 0;
    if (!s->dib_section) return su_call(s, ES2_SIM_VA_DIB_SECTION, &instance, 1, NULL);
    uint32_t refused = s->dib_section(s->own_user);
    if (refused) { s->outcome = ES2_SIM_STARTUP_REFUSED; s->refused_va = refused; return 1; }
    return 0;
}

/* ---- WinMain 0x46538c (listing 0x46538c..0x465489) */
static int win_main(es2_sim_startup *s, uint32_t instance, uint32_t command, uint32_t *result) {
    uint32_t top = s->frame_hi, argv = su_frame_push(s, 0x6cu);
    uint32_t argc = 0;
    if (es2_sim_startup_split(s, command, argv, &argc)) return 1;
    s->argc = argc;
    su_w32(s, VA_HINSTANCE, instance);
    /* 0x4650e8: RegisterClassA (WndProc 0x466034, icon "es2icon", arrow
     * cursor, menu 1, class "ES2") after LoadLibraryA("BWCC32.DLL") whose
     * handle is dropped: headless an atom, nothing in guest memory. */
    ++s->platform.register_class;
    ++s->platform.load_library;
    if (!s->platform.class_atom) { *result = 0; return 0; }
    uint32_t zero[1] = {0};
    if (su_call(s, VA_PREFS_VIDEO, zero, 1, NULL)) return 1;
    if (es2_sim_startup_early_parser(s, argc, argv)) return 1;
    if (window(s, instance)) return 1;
    if (su_r32(s, VA_DIRECTDRAW)) {
        su_w32(s, VA_DIRECTDRAW, 0);
        if (su_call(s, VA_DD_SWITCH, NULL, 0, NULL) || su_call(s, VA_FRAME_SHOW, NULL, 0, NULL)) return 1;
    }
    /* LoadCursorA(0, IDC_WAIT) -> 0x464c68: [0x4d3720] = the handle. */
    if (s->platform.cursor_wait) su_w32(s, VA_CURSOR, s->platform.cursor_wait);
    if (game_main(s, argc, argv)) return 1;
    /* The tail: PostQuitMessage(0); 0x464c78 until it returns nonzero (its
     * first PeekMessageA removes the WM_QUIT just posted, headless the only
     * message, and returns 1 before the cursor is set); the release 0x466730
     * (listing 0x466730..0x4667c7). With no DirectDraw object (the GDI
     * path) it is only ClipCursor to the screen (counted here); with an
     * object it is presentation_roots.h's row dd_release
     * (src/present.c) through the dispatch, which calls the frame clear and
     * each object's Release through the dispatch too. */
    ++s->platform.post_quit;
    if (s->quit_pump) {
        uint32_t refused = s->quit_pump(s->own_user);
        if (refused) { s->outcome = ES2_SIM_STARTUP_REFUSED; s->refused_va = refused; return 1; }
    } else {
        ++s->platform.peeks;
    }
    int objects = 0;
    for (uint32_t i = 0; i < 4u; ++i) objects |= su_r32(s, VA_DD_OBJECTS + 4u * i) != 0;
    if (objects) {
        if (su_call(s, VA_DD_RELEASE, NULL, 0, NULL)) return 1;
    } else {
        ++s->platform.clip_cursor;
    }
    su_frame_pop(s, top);
    *result = su_r32(s, VA_EXIT_CODE);
    return 0;
}

es2_sim_startup_outcome es2_sim_startup_run(es2_sim_startup *s) {
    s->outcome = ES2_SIM_STARTUP_RETURNED;
    s->archives = 0;
    /* hInstance = GetModuleHandleA(NULL) through the dispatch (after the init
     * records); lpCmdLine = the command tail (rtl_imports.md, "the runtime
     * lifecycle"). */
    uint32_t none[1] = {0};
    if (su_call(s, VA_MODULE_HANDLE, none, 1, &s->h_instance)) return s->outcome;
    es2_rtl_start_env env;
    memset(&env, 0, sizeof(env));
    es2_rtl_memory rtl;
    es2_sim_rtl_memory(s->memory, &rtl);
    env.context = rtl.context;
    env.read8 = rtl.read8;
    s->cmd_line = es2_rtl_command_tail(&env, s->command_line);
    uint32_t result = 0;
    if (win_main(s, s->h_instance, s->cmd_line, &result)) return s->outcome;
    s->exit_code = result;
    s->outcome = ES2_SIM_STARTUP_RETURNED;
    return s->outcome;
}
