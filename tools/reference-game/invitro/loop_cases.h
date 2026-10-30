/* The main loop's pieces, original machine code against sim/src/loop.c and
 * sim/src/frame_buffer.c (--loop; target :differential_loop_test). Included
 * after startup_cases.h, whose DATA reset (su_reset: DATA as the file maps it,
 * the construction counter, the emulated heap), snapshots (su_take, su_compare)
 * and helpers it reuses.
 *
 * - dib: the DIB section 0x464d44 (video modes 0 and 1, a DIB from an
 *   earlier call, random palette bytes) and the surface release 0x480f38.
 *   The GDI imports answer as the headless platform does (stand-in handles,
 *   GetSystemPaletteEntries 0 without writing, CreateDIBSection with the
 *   harness's bits region); _calloc/_free over the emulated heap, _atexit
 *   into a table, _strncpy/_memset over the rtl_cases.h models. Compared:
 *   DATA, the heap, the atexit table, the import-level call log, the GDI
 *   call counts.
 * - timing: the timing init 0x467840, the limiter 0x4678c0 and the clock
 *   0x467828 under virtual time (GetTickCount: es2_vt, the limiter's poll
 *   recognised by its return address 0x4678d5 on the original side, named
 *   on the own side), time_scale 0x467894 as the original's machine code on
 *   both sides; random steps, intra-frame on/off, references, clock reads
 *   per pass. Compared after every call: the 24 timing bytes, the clock
 *   values, the virtual-time state.
 * - pump: the pump 0x464c78 over a random journal (frames, pump keys,
 *   key-downs with and without a character, mouse messages), a posted
 *   WM_QUIT, the cursor word; PeekMessageA/TranslateMessage on the original
 *   side are an independent model of the proxy's replay semantics (journal
 *   first, then the posted queue, WM_QUIT last), DispatchMessageA records;
 *   on the own side a recording handler in slot 0 answers every message.
 *   Compared: the message sequence, the peeks, SetCursor, the result, the
 *   journal state.
 * - window: the window procedure 0x466034 with random handler lists
 *   (recording stdcall stubs answering 0 or 1) and the messages a replay
 *   delivers; DefWindowProcA records. Compared: handler calls, DefWindowProc
 *   calls, EAX.
 * - handlers: random add 0x465fec / remove 0x466010 sequences (the list,
 *   EAX, EDX); notify: 0x479cfc (DATA, heap, EAX, EDX). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "es2/sim/loop.h"

#define LC_BITS_SIZE (1280u * 960u)
static es2_sim_entry lc_entries[4096];
static es2_sim_dispatch lc_dispatch;
static es2_sim_services lc_services;
static es2_vt lc_vt;
static es2_sim_loop lc_loop;
static BYTE *lc_stack, *lc_bits;
static int lc_bad;
static char lc_log[2][256];
static unsigned lc_log_count[2], lc_side;
static void lc_note(char c) {
    if (lc_log_count[lc_side] + 1 < sizeof(lc_log[0])) lc_log[lc_side][lc_log_count[lc_side]++] = c;
    lc_log[lc_side][lc_log_count[lc_side]] = 0;
}
static uint32_t lc_atexit_table[32], lc_atexit_count;

/* ---- the original side's import stubs */
static void *__cdecl lc_calloc(size_t count, size_t n) { lc_note('C'); return ir_calloc(count, n); }
static void __cdecl lc_free(void *p) { lc_note('F'); ir_free(p); }
static uint32_t __cdecl lc_atexit(uint32_t f) {
    lc_note('A');
    if (lc_atexit_count == 32u) return 1u;
    lc_atexit_table[lc_atexit_count++] = f;
    return 0;
}
static uint32_t __cdecl lc_strncpy(uint32_t d, uint32_t s, uint32_t n) { lc_note('N'); uint32_t a[3] = {d, s, n}; return su_rtl_call(ES2_RTL_STRNCPY, a); }
static uint32_t __cdecl lc_memset(uint32_t d, uint32_t v, uint32_t n) { lc_note('S'); uint32_t a[3] = {d, v, n}; return su_rtl_call(ES2_RTL_MEMSET, a); }
static unsigned lc_gdi[8];   /* GetDC, CreateCompatibleDC, palette, ReleaseDC, CreateDIBSection, SelectObject, DeleteObject, MessageBoxA */
static uint32_t lc_selected;
static uint32_t __stdcall lc_get_dc(uint32_t w) { (void)w; ++lc_gdi[0]; return 0x00010011u; }
static uint32_t __stdcall lc_create_dc(uint32_t dc) { (void)dc; ++lc_gdi[1]; return 0x00010013u; }
static uint32_t __stdcall lc_palette(uint32_t dc, uint32_t first, uint32_t n, uint32_t p) { (void)dc; (void)first; (void)n; (void)p; ++lc_gdi[2]; return 0; }
static uint32_t __stdcall lc_release_dc(uint32_t w, uint32_t dc) { (void)w; (void)dc; ++lc_gdi[3]; return 1; }
static uint32_t __stdcall lc_create_dib(uint32_t dc, uint32_t bmi, uint32_t usage, uint32_t *bits, uint32_t section, uint32_t offset) {
    (void)dc; (void)bmi; (void)usage; (void)section; (void)offset;
    ++lc_gdi[4];
    *bits = (uint32_t)(uintptr_t)lc_bits;
    return 0x00010015u;
}
static uint32_t __stdcall lc_select(uint32_t dc, uint32_t object) {
    (void)dc; ++lc_gdi[5];
    uint32_t previous = lc_selected;
    lc_selected = object;
    return previous;
}
static uint32_t __stdcall lc_delete(uint32_t object) { (void)object; ++lc_gdi[6]; return 1; }
static uint32_t __stdcall lc_message_box(uint32_t w, uint32_t t, uint32_t c, uint32_t f) { (void)w; (void)t; (void)c; (void)f; ++lc_gdi[7]; return 1; }
/* GetTickCount: virtual time; the limiter's call returns to 0x4678d5. */
static uint32_t __stdcall lc_tick(void) {
    uint32_t back = (uint32_t)(uintptr_t)__builtin_return_address(0);
    if (back == 0x004678d5u) return es2_vt_limiter_poll(&lc_vt, get32(mapped + 0xd3b74u), NULL, NULL);
    return es2_vt_read(&lc_vt);
}
/* The message model of the original side (the proxy's replay semantics). */
static es2_journal lc_journal[2];
static struct { uint32_t message, wparam, lparam; } lc_posted[16];
static unsigned lc_posted_count, lc_quit, lc_peeks;
static uint32_t lc_dispatched[2][64][3], lc_dispatch_count[2], lc_cursor_calls[2], lc_last_cursor[2];
static uint32_t lc_char(uint32_t vk) { return vk >= 0x41u && vk <= 0x5au ? vk + 0x20u : vk == 0x0du || vk == 0x20u || vk == 0x1bu ? vk : 0u; }
static uint32_t lc_translate_own(void *u, uint32_t vk, uint32_t scan) { (void)u; (void)scan; return lc_char(vk); }
static uint32_t __stdcall lc_peek(uint32_t *msg, uint32_t window, uint32_t first, uint32_t last, uint32_t flags) {
    (void)window; (void)first; (void)last;
    if (!(flags & 1u)) { lc_bad = 1; return 0; }
    ++lc_peeks;
    uint32_t frame = get32(mapped + 0xd257bu);
    es2_journal *j = &lc_journal[0];
    es2_journal_count_pump(j, frame);
    es2_journal_entry *e = es2_journal_take(j, ES2_JOURNAL_MSG, frame, NULL, 0);
    memset(msg, 0, 28);
    msg[0] = get32(mapped + 0xd3610u);
    if (e) {
        msg[1] = (uint32_t)e->a; msg[2] = (uint32_t)e->b; msg[3] = (uint32_t)e->c;
        es2_journal_apply_message(j, msg[1], msg[2], msg[3]);
        ++j->injected;
        return 1;
    }
    if (lc_posted_count) {
        msg[1] = lc_posted[0].message; msg[2] = lc_posted[0].wparam; msg[3] = lc_posted[0].lparam;
        memmove(&lc_posted[0], &lc_posted[1], sizeof(lc_posted[0]) * --lc_posted_count);
        return 1;
    }
    if (lc_quit) { lc_quit = 0; msg[0] = 0; msg[1] = 0x12u; return 1; }
    return 0;
}
static uint32_t __stdcall lc_translate(const uint32_t *msg) {
    if ((msg[1] == 0x100u || msg[1] == 0x104u) && lc_char(msg[2]) && lc_posted_count < 16u) {
        lc_posted[lc_posted_count].message = msg[1] == 0x100u ? 0x102u : 0x106u;
        lc_posted[lc_posted_count].wparam = lc_char(msg[2]);
        lc_posted[lc_posted_count].lparam = msg[3];
        ++lc_posted_count;
        return 1;
    }
    return 0;
}
static void lc_record(unsigned side, uint32_t m, uint32_t w, uint32_t l) {
    if (lc_dispatch_count[side] < 64u) {
        lc_dispatched[side][lc_dispatch_count[side]][0] = m;
        lc_dispatched[side][lc_dispatch_count[side]][1] = w;
        lc_dispatched[side][lc_dispatch_count[side]][2] = l;
    }
    ++lc_dispatch_count[side];
}
static uint32_t __stdcall lc_dispatch_message(const uint32_t *msg) { lc_record(0, msg[1], msg[2], msg[3]); return 0; }
static uint32_t __stdcall lc_set_cursor(uint32_t c) { ++lc_cursor_calls[0]; lc_last_cursor[0] = c; return 0; }
/* The window procedure's handlers and DefWindowProcA. */
static uint32_t lc_answers[10], lc_handler_calls[2][64], lc_handler_count[2], lc_def_calls[2];
#define LC_HANDLER(k) static uint32_t __stdcall lc_handler##k(uint32_t h, uint32_t m, uint32_t w, uint32_t l) { \
    (void)h; (void)w; (void)l; if (lc_handler_count[0] < 64u) lc_handler_calls[0][lc_handler_count[0]] = (k) << 16 | m; \
    ++lc_handler_count[0]; return lc_answers[k]; }
LC_HANDLER(0) LC_HANDLER(1) LC_HANDLER(2) LC_HANDLER(3) LC_HANDLER(4)
LC_HANDLER(5) LC_HANDLER(6) LC_HANDLER(7) LC_HANDLER(8) LC_HANDLER(9)
typedef uint32_t (__stdcall *lc_handler_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
static const lc_handler_fn lc_handlers[10] = {lc_handler0, lc_handler1, lc_handler2, lc_handler3, lc_handler4,
                                              lc_handler5, lc_handler6, lc_handler7, lc_handler8, lc_handler9};
static uint32_t __stdcall lc_def(uint32_t h, uint32_t m, uint32_t w, uint32_t l) { (void)h; (void)m; (void)w; (void)l; ++lc_def_calls[0]; return 0; }

/* ---- the own side: the sim services (with the loop's rows over lc_loop),
 * logging wrappers, time_scale and the handler stubs as services */
static void lc_time_scale(es2_sim_service_call *call, void *user) {
    (void)user;
    es2_hook_result r;
    uint32_t a[2] = {call->slots[0], call->slots[1]};
    es2_hook_call c = {mapped + 0x67894u, ES2_ABI_CDECL, 2, a, initial_fpu, &r, NULL};
    es2_hook_machine_call(&c);
    call->eax = r.eax; call->edx = r.edx;
    /* ECX: time_scale leaves the sign-extended reference (mov ecx, edx at 0x4678a2). */
    call->ecx = (uint32_t)(int32_t)(int16_t)a[0];
}
static void lc_own_handler(es2_sim_service_call *call, void *user) {
    (void)user;
    unsigned k = 10;
    for (unsigned i = 0; i < 10u; ++i) if ((uint32_t)(uintptr_t)lc_handlers[i] == call->va) k = i;
    if (k == 10u) { lc_bad = 1; return; }
    if (lc_handler_count[1] < 64u) lc_handler_calls[1][lc_handler_count[1]] = k << 16 | call->slots[1];
    ++lc_handler_count[1];
    call->eax = lc_answers[k];
}
/* Slot 0 of the pump case: records every dispatched message, answers 1. */
static void lc_pump_handler(es2_sim_service_call *call, void *user) {
    (void)user;
    lc_record(1, call->slots[1], call->slots[2], call->slots[3]);
    call->eax = 1;
}
#define LC_PUMP_HANDLER 0x7ff00010u
static struct { uint32_t va; char letter; es2_sim_service_fn real; } lc_wrapped[] = {
    {0x0047749cu, 'C', NULL}, {0x0047747cu, 'C', NULL}, {0x00477548u, 'F', NULL}, {0x004967a8u, 'F', NULL},
    {0x0049675au, 'A', NULL}, {0x00496760u, 'N', NULL}, {0x0049681au, 'S', NULL},
};
static void lc_wrapper(es2_sim_service_call *call, void *user) {
    for (unsigned i = 0; i < sizeof(lc_wrapped) / sizeof(lc_wrapped[0]); ++i) {
        if (lc_wrapped[i].va != call->va) continue;
        if (lc_wrapped[i].letter != 'F' || call->slots[0] || call->va == 0x004967a8u) lc_note(lc_wrapped[i].letter);
        lc_wrapped[i].real(call, user);
        return;
    }
}
static void lc_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own loop: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    lc_bad = 1;
}
static int lc_dispatch_setup(void) {
    es2_sim_dispatch_init(&lc_dispatch, lc_entries, 4096, lc_refuse, NULL);
    lc_services.memory = &ir_memory;
    lc_services.heap = &ir_heap;
    lc_services.vt = &lc_vt;
    lc_services.fs0 = NULL;
    if (es2_sim_register_services(&lc_dispatch, &lc_services) ||
        es2_sim_dispatch_add_service(&lc_dispatch, 0x00467894u, "time_scale (original)", lc_time_scale, NULL, 0, 2) ||
        es2_sim_dispatch_add_service(&lc_dispatch, LC_PUMP_HANDLER, "pump handler", lc_pump_handler, NULL, 16, 4))
        return fail("loop: dispatch");
    for (unsigned i = 0; i < 10u; ++i)
        if (es2_sim_dispatch_add_service(&lc_dispatch, (uint32_t)(uintptr_t)lc_handlers[i], "handler", lc_own_handler, NULL, 16, 4))
            return fail("loop: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&lc_dispatch, &error, &bad)) return fail("loop: dispatch seal");
    for (unsigned i = 0; i < sizeof(lc_wrapped) / sizeof(lc_wrapped[0]); ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&lc_dispatch, lc_wrapped[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("loop: service missing");
        lc_wrapped[i].real = e->service;
        e->service = lc_wrapper;
    }
    return 0;
}

/* ---- passes */
static void lc_reset(unsigned side, uint32_t step, uint32_t start, int intra) {
    su_reset(side);
    lc_side = side;
    lc_log_count[side] = 0;
    lc_log[side][0] = 0;
    lc_atexit_count = 0;
    memset(lc_gdi, 0, sizeof(lc_gdi));
    lc_selected = 0x00010017u;   /* a new memory DC's default bitmap: the stand-in the own platform answers */
    memset(&lc_vt, 0, sizeof(lc_vt));
    es2_vt_enable(&lc_vt, 1, step, start, 0, intra);
    es2_sim_loop_defaults(&lc_loop);
    lc_loop.memory = &ir_memory;
    lc_loop.dispatch = &lc_dispatch;
    lc_loop.vt = &lc_vt;
    lc_loop.stack_lo = (uint32_t)(uintptr_t)lc_stack;
    lc_loop.stack_hi = (uint32_t)(uintptr_t)lc_stack + 0x10000u;
    lc_loop.platform.bits = (uint32_t)(uintptr_t)lc_bits;
    lc_loop.platform.bits_size = LC_BITS_SIZE;
    lc_loop.platform.translate = lc_translate_own;
    es2_sim_loop_bind(&lc_loop);
    ww_clear(lc_bits, LC_BITS_SIZE);
}
/* A call without arguments that keeps the harness's EBX/ESI/EDI whatever
 * the callee leaves in them (ESI 0x11111111, EDI 0x22222222, EBX 0x33333333
 * at entry); after[0..1] = ESI and EDI at the callee's return. */
extern void lc_call_clobber(uint32_t target, uint32_t *after);
__asm__(".text\n.globl _lc_call_clobber\n_lc_call_clobber:\n"
        "\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 8(%ebp), %eax\n\tmovl $0x11111111, %esi\n\tmovl $0x22222222, %edi\n\tmovl $0x33333333, %ebx\n"
        "\tcall *%eax\n"
        "\tmovl 12(%ebp), %ecx\n\tmovl %esi, 0(%ecx)\n\tmovl %edi, 4(%ecx)\n"
        "\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");
static uint32_t lc_machine(uint32_t va, const uint32_t *a, unsigned n, uint32_t *edx) {
    es2_hook_result r;
    es2_hook_call c = {mapped + (va - 0x400000u), ES2_ABI_CDECL, n, a, initial_fpu, &r, NULL};
    es2_hook_machine_call(&c);
    if (edx) *edx = r.edx;
    return r.eax;
}

/* ---- dib */
static int lc_dib_case(unsigned k) {
    unsigned mode = random32() % 2u;
    int prior = random32() % 3u == 0;
    BYTE palette[1024];
    random_bytes(palette, sizeof(palette));
    uint32_t atexit_side[2][32], atexit_count[2];
    unsigned gdi[2][8];
    for (unsigned side = 0; side < 2u; ++side) {
        lc_reset(side, 47, 0x01000000u, 1);
        put32(mapped + 0xd3610u, 0x00010001u);
        put32(mapped + 0xd253eu, mode ? 640u : 320u);
        put32(mapped + 0xd2542u, mode ? 480u : 240u);
        put32(mapped + 0xd3040u, mode ? 1280u : 640u);
        put32(mapped + 0xd3044u, mode ? 960u : 480u);
        memcpy(mapped + 0xd3074u, palette, sizeof(palette));
        if (prior) { put32(mapped + 0xd3038u, 0x00010099u); put32(mapped + 0xd303cu, 0x00010017u); }
        if (!side) {
            uint32_t a[1] = {0x00400000u};
            lc_machine(0x00464d44u, a, 1, NULL);
            /* 0x480f38 returns with the callee-saved registers shifted
             * (0x4894a5 pushes one slot and removes two): a thunk that
             * restores them, and the quirk checked. */
            uint32_t after[2];
            lc_call_clobber(0x00480f38u, after);
            if (after[0] != 0 || after[1] != 0x11111111u) {
                fprintf(stderr, "FAIL: loop dib case %u: 0x480f38 left ESI %08lx EDI %08lx (expected 0, the entry ESI)\n", k,
                        (unsigned long)after[0], (unsigned long)after[1]);
                return 1;
            }
            memcpy(gdi[0], lc_gdi, sizeof(lc_gdi));
            memcpy(atexit_side[0], lc_atexit_table, sizeof(lc_atexit_table));
            atexit_count[0] = lc_atexit_count;
        } else {
            if (es2_sim_dib_section(&lc_loop) || es2_sim_surface_release(&lc_loop)) lc_bad = 1;
            const es2_sim_loop_platform *p = &lc_loop.platform;
            unsigned own[8] = {p->get_dc, p->create_dc, p->palette_entries, p->release_dc, p->create_dib, p->select_object,
                               p->delete_object, p->message_box};
            memcpy(gdi[1], own, sizeof(own));
            memset(atexit_side[1], 0, sizeof(atexit_side[1]));
            memcpy(atexit_side[1], lc_loop.atexit_table, 4u * lc_loop.atexit_count);
            atexit_count[1] = lc_loop.atexit_count;
            if (atexit_count[0] < 32u) memset(&atexit_side[0][atexit_count[0]], 0, 4u * (32u - atexit_count[0]));
        }
        if (lc_bad) { fprintf(stderr, "FAIL: loop dib case %u: a call failed\n", k); return 1; }
        su_take(side);
    }
    if (su_compare("dib", k)) return 1;
    if (strcmp(lc_log[0], lc_log[1]) || memcmp(gdi[0], gdi[1], sizeof(gdi[0])) || atexit_count[0] != atexit_count[1] ||
        memcmp(atexit_side[0], atexit_side[1], 4u * atexit_count[0])) {
        fprintf(stderr, "FAIL: loop dib case %u: calls %s / %s, GDI %u%u%u%u%u%u%u%u / %u%u%u%u%u%u%u%u, atexit %lu / %lu\n", k,
                lc_log[0], lc_log[1], gdi[0][0], gdi[0][1], gdi[0][2], gdi[0][3], gdi[0][4], gdi[0][5], gdi[0][6], gdi[0][7],
                gdi[1][0], gdi[1][1], gdi[1][2], gdi[1][3], gdi[1][4], gdi[1][5], gdi[1][6], gdi[1][7],
                (unsigned long)atexit_count[0], (unsigned long)atexit_count[1]);
        return 1;
    }
    if (k == 0) printf("DIB mode %u prior %d: calls %s, heap 0x%lx bytes; identical\n", mode, prior, lc_log[0], (unsigned long)su_used[0]);
    return 0;
}

/* ---- timing */
static int lc_timing_case(unsigned k) {
    uint32_t step = 40u + random32() % 80u, start = random32();
    int intra = (int)(random32() & 1u);
    unsigned passes = 1u + random32() % 12u;
    uint16_t refs[16][2];
    unsigned reads[16];
    for (unsigned p = 0; p < passes; ++p) {
        reads[p] = random32() % 4u == 0 ? random32() % 80u : random32() % 6u;
        refs[p][0] = random32() % 4u == 0 ? (uint16_t)(1u + random32() % 400u) : 0;
        refs[p][1] = random32() % 4u == 0 ? (uint16_t)(1u + random32() % 400u) : 0;
    }
    static uint32_t values[2][2048];
    static BYTE timing[2][16][24];
    static es2_vt state[2][16];
    unsigned count[2] = {0, 0};
    for (unsigned side = 0; side < 2u; ++side) {
        lc_reset(side, step, start, intra);
        if (!side) values[0][count[0]++] = lc_machine(0x00467840u, NULL, 0, NULL);
        else values[1][count[1]++] = es2_sim_timing_init(&lc_loop);
        for (unsigned p = 0; p < passes; ++p) {
            if (refs[p][0]) put16(mapped + 0xd3b68u, refs[p][0]);
            if (refs[p][1]) put16(mapped + 0xd3b6au, refs[p][1]);
            if (!side) lc_machine(0x004678c0u, NULL, 0, NULL);
            else if (es2_sim_limiter(&lc_loop)) lc_bad = 1;
            for (unsigned r = 0; r < reads[p] && count[side] < 2048u; ++r)
                values[side][count[side]++] = !side ? lc_machine(0x00467828u, NULL, 0, NULL) : es2_sim_clock(&lc_loop);
            memcpy(timing[side][p], mapped + 0xd3b64u, 24);
            state[side][p] = lc_vt;
        }
        if (lc_bad) { fprintf(stderr, "FAIL: loop timing case %u: a call failed\n", k); return 1; }
        su_take(side);
    }
    if (count[0] != count[1] || memcmp(values[0], values[1], 4u * count[0])) {
        fprintf(stderr, "FAIL: loop timing case %u: clock values differ (%u / %u)\n", k, count[0], count[1]);
        return 1;
    }
    for (unsigned p = 0; p < passes; ++p)
        if (memcmp(timing[0][p], timing[1][p], 24) || memcmp(&state[0][p], &state[1][p], sizeof(es2_vt))) {
            fprintf(stderr, "FAIL: loop timing case %u pass %u: timing bytes or virtual time differ (step %lu intra %d)\n", k, p,
                    (unsigned long)step, intra);
            return 1;
        }
    return su_compare("timing", k);
}

/* ---- pump */
static int lc_pump_case(unsigned k) {
    /* A random journal over frames 0..3. */
    char text[4096];
    size_t n = 0;
    unsigned entries = random32() % 14u;
    static const uint32_t messages[] = {0x100u, 0x101u, 0x104u, 0x105u, 0x200u, 0x201u, 0x202u};
    static const uint32_t keys[] = {0x41u, 0x57u, 0x0du, 0x20u, 0x1bu, 0x25u, 0x70u, 0x10u};
    uint32_t frame = 0;
    for (unsigned i = 0; i < entries && n + 128 < sizeof(text); ++i) {
        if (random32() % 3u == 0) frame += random32() % 2u;
        uint32_t m = messages[random32() % 7u], w = m < 0x200u ? keys[random32() % 8u] : random32() % 3u;
        uint32_t l = m < 0x200u ? 1u | (random32() % 0x60u) << 16 : (random32() % 640u) | (random32() % 480u) << 16;
        unsigned p = random32() % 3u == 0 ? random32() % 6u : 0u;
        n += (size_t)sprintf(text + n, "{\"f\":%lu,\"p\":%u,\"api\":\"msg\",\"a\":%lu,\"b\":%lu,\"c\":%ld}\n", (unsigned long)frame, p,
                             (unsigned long)m, (unsigned long)w, (long)(int32_t)l);
    }
    unsigned calls = 1u + random32() % 8u;
    uint32_t frames[8], quit_at = random32() % 3u == 0 ? random32() % calls : 99u;
    for (unsigned c = 0; c < calls; ++c) frames[c] = random32() % 4u;
    uint32_t cursor = random32() % 2u ? 0x00010003u : 0u;
    uint32_t results[2][8], peeks[2];
    for (unsigned side = 0; side < 2u; ++side) {
        lc_reset(side, 47, 0x01000000u, 1);
        es2_journal_free(&lc_journal[side]);
        es2_journal_init(&lc_journal[side]);
        if (es2_journal_load_text(&lc_journal[side], text, n, NULL)) return fail("loop: journal");
        lc_posted_count = 0; lc_quit = 0; lc_peeks = 0;
        lc_dispatch_count[side] = 0; lc_cursor_calls[side] = 0; lc_last_cursor[side] = 0;
        put32(mapped + 0xd3720u, cursor);
        put32(mapped + 0xd3610u, 0x00010001u);
        if (side) {
            lc_loop.journal = &lc_journal[1];
            put32(mapped + 0xd3b30u, LC_PUMP_HANDLER);
        }
        for (unsigned c = 0; c < calls; ++c) {
            put32(mapped + 0xd257bu, frames[c]);
            if (c == quit_at) { if (!side) lc_quit = 1; else es2_sim_post_quit(&lc_loop); }
            if (!side) results[0][c] = lc_machine(0x00464c78u, NULL, 0, NULL);
            else if (es2_sim_pump(&lc_loop, &results[1][c])) lc_bad = 1;
        }
        peeks[side] = side ? lc_loop.platform.peeks : lc_peeks;
        if (side) { lc_cursor_calls[1] = lc_loop.platform.set_cursor; lc_last_cursor[1] = lc_loop.platform.last_cursor; put32(mapped + 0xd3b30u, 0); }
        if (lc_bad) { fprintf(stderr, "FAIL: loop pump case %u: a call failed\n", k); return 1; }
    }
    const es2_journal *a = &lc_journal[0], *b = &lc_journal[1];
    if (memcmp(results[0], results[1], 4u * calls) || peeks[0] != peeks[1] || lc_dispatch_count[0] != lc_dispatch_count[1] ||
        memcmp(lc_dispatched[0], lc_dispatched[1], sizeof(lc_dispatched[0][0]) * (lc_dispatch_count[0] < 64u ? lc_dispatch_count[0] : 64u)) ||
        lc_cursor_calls[0] != lc_cursor_calls[1] || lc_last_cursor[0] != lc_last_cursor[1] || a->injected != b->injected ||
        a->pump_count != b->pump_count || a->pump_frame != b->pump_frame || memcmp(a->keys, b->keys, sizeof(a->keys)) ||
        a->mouse_x != b->mouse_x || a->mouse_y != b->mouse_y || memcmp(a->cursor, b->cursor, sizeof(a->cursor))) {
        fprintf(stderr, "FAIL: loop pump case %u: peeks %u / %u, dispatched %lu / %lu, cursor %lu / %lu, injected %lu / %lu\n%s", k,
                peeks[0], peeks[1], (unsigned long)lc_dispatch_count[0], (unsigned long)lc_dispatch_count[1],
                (unsigned long)lc_cursor_calls[0], (unsigned long)lc_cursor_calls[1], (unsigned long)a->injected,
                (unsigned long)b->injected, text);
        return 1;
    }
    return 0;
}

/* ---- window procedure */
static int lc_window_case(unsigned k) {
    static const uint32_t messages[] = {0x100u, 0x101u, 0x102u, 0x104u, 0x106u, 0x200u, 0x201u, 0x202u, 0x113u, 0x020au};
    uint32_t list[10];
    for (unsigned i = 0; i < 10u; ++i) {
        list[i] = random32() % 3u == 0 ? (uint32_t)(uintptr_t)lc_handlers[random32() % 10u] : 0u;
        lc_answers[i] = random32() % 4u == 0;
    }
    unsigned count = 1u + random32() % 6u;
    uint32_t m[8], w[8], l[8], results[2][8];
    for (unsigned i = 0; i < count; ++i) { m[i] = messages[random32() % 10u]; w[i] = random32() % 0x60u; l[i] = random32(); }
    for (unsigned side = 0; side < 2u; ++side) {
        lc_reset(side, 47, 0x01000000u, 1);
        lc_handler_count[side] = 0; lc_def_calls[side] = 0;
        for (unsigned i = 0; i < 10u; ++i) put32(mapped + 0xd3b30u + 4u * i, list[i]);
        for (unsigned i = 0; i < count; ++i) {
            if (!side) {
                es2_hook_result r;
                uint32_t a[4] = {0x00010001u, m[i], w[i], l[i]};
                es2_hook_call c = {mapped + 0x66034u, ES2_ABI_STDCALL, 4, a, initial_fpu, &r, NULL};
                es2_hook_machine_call(&c);
                results[0][i] = r.eax;
            } else if (es2_sim_window_proc(&lc_loop, 0x00010001u, m[i], w[i], l[i], &results[1][i])) {
                lc_bad = 1;
            }
        }
        if (side) lc_def_calls[1] = lc_loop.platform.def_window_proc;
        if (lc_bad) { fprintf(stderr, "FAIL: loop window case %u: a call failed\n", k); return 1; }
        su_take(side);
    }
    if (memcmp(results[0], results[1], 4u * count) || lc_handler_count[0] != lc_handler_count[1] ||
        memcmp(lc_handler_calls[0], lc_handler_calls[1], 4u * (lc_handler_count[0] < 64u ? lc_handler_count[0] : 64u)) ||
        lc_def_calls[0] != lc_def_calls[1]) {
        fprintf(stderr, "FAIL: loop window case %u: handlers %lu / %lu, DefWindowProc %lu / %lu\n", k, (unsigned long)lc_handler_count[0],
                (unsigned long)lc_handler_count[1], (unsigned long)lc_def_calls[0], (unsigned long)lc_def_calls[1]);
        return 1;
    }
    for (unsigned i = 0; i < 10u; ++i) put32(mapped + 0xd3b30u + 4u * i, 0);
    return su_compare("window", k);
}

/* ---- the handler list rows and the abort-notify registration */
static int lc_rows_case(unsigned k) {
    uint32_t ops[16], args[16], eax[2][16], edx[2][16];
    unsigned count = 1u + random32() % 16u;
    for (unsigned i = 0; i < count; ++i) {
        ops[i] = random32() % 3u;
        args[i] = random32() % 4u == 0 ? 0u : 0x00470000u + 4u * (random32() % 6u);
    }
    for (unsigned side = 0; side < 2u; ++side) {
        lc_reset(side, 47, 0x01000000u, 1);
        for (unsigned i = 0; i < count; ++i) {
            static const uint32_t vas[3] = {0x00465fecu, 0x00466010u, 0x00479cfcu};
            uint32_t a[1] = {args[i]};
            if (!side) eax[0][i] = lc_machine(vas[ops[i]], a, 1, &edx[0][i]);
            else {
                es2_sim_result r;
                uint32_t regs[3] = {0, 0, 0};
                if (es2_sim_call(&lc_dispatch, vas[ops[i]], a, 1, regs, &r)) lc_bad = 1;
                eax[1][i] = r.eax; edx[1][i] = r.edx;
            }
        }
        if (lc_bad) { fprintf(stderr, "FAIL: loop rows case %u: a call failed\n", k); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < count; ++i)
        if (eax[0][i] != eax[1][i] || edx[0][i] != edx[1][i]) {
            fprintf(stderr, "FAIL: loop rows case %u call %u (op %lu): EAX %08lx/%08lx EDX %08lx/%08lx\n", k, i, (unsigned long)ops[i],
                    (unsigned long)eax[0][i], (unsigned long)eax[1][i], (unsigned long)edx[0][i], (unsigned long)edx[1][i]);
            return 1;
        }
    for (unsigned i = 0; i < 10u; ++i) put32(mapped + 0xd3b30u + 4u * i, 0);
    return su_compare("rows", k);
}

static int loop_cases(void) {
    current_function = "loop";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("loop: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("loop: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)lc_calloc}, {"cw3220.dll", "_free", (void *)lc_free},
        {"cw3220.dll", "_atexit", (void *)lc_atexit}, {"cw3220.dll", "_strncpy", (void *)lc_strncpy},
        {"cw3220.dll", "_memset", (void *)lc_memset}, {"USER32.dll", "GetDC", (void *)lc_get_dc},
        {"GDI32.dll", "CreateCompatibleDC", (void *)lc_create_dc}, {"GDI32.dll", "GetSystemPaletteEntries", (void *)lc_palette},
        {"USER32.dll", "ReleaseDC", (void *)lc_release_dc}, {"GDI32.dll", "CreateDIBSection", (void *)lc_create_dib},
        {"GDI32.dll", "SelectObject", (void *)lc_select}, {"GDI32.dll", "DeleteObject", (void *)lc_delete},
        {"USER32.dll", "MessageBoxA", (void *)lc_message_box}, {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
        {"USER32.dll", "PeekMessageA", (void *)lc_peek}, {"USER32.dll", "TranslateMessage", (void *)lc_translate},
        {"USER32.dll", "DispatchMessageA", (void *)lc_dispatch_message}, {"USER32.dll", "SetCursor", (void *)lc_set_cursor},
        {"USER32.dll", "DefWindowProcA", (void *)lc_def},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) return fail("loop: import slot not found");
    if (lc_dispatch_setup()) return 1;
    es2_journal_init(&lc_journal[0]);
    es2_journal_init(&lc_journal[1]);
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"dib", lc_dib_case, 64}, {"timing", lc_timing_case, 2000}, {"pump", lc_pump_case, 4000},
        {"window", lc_window_case, 4000}, {"rows", lc_rows_case, 2000},
    };
    /* Each part's cases run in chunks of LC_CHUNK; chunk c of the whole run
     * belongs to test shard c % count (suite_part_in_shard) and starts from its
     * own seed, so a chunk's cases do not depend on the shard layout. */
    enum { LC_CHUNK = 500 };
    unsigned chunk = 0;
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        unsigned owned = 0;
        for (unsigned first = 0; !bad && first < parts[p].cases; first += LC_CHUNK, ++chunk) {
            if (!suite_part_in_shard(chunk)) continue;
            unsigned end = parts[p].cases - first < LC_CHUNK ? parts[p].cases : first + LC_CHUNK;
            random_state = 0x48c06e21u ^ (chunk * 0x9e3779b9u);
            for (unsigned k = first; !bad && k < end; ++k) {
                current_case = k; current_case_valid = 1;
                bad = parts[p].run(k);
            }
            owned += end - first;
        }
        if (!bad && owned) printf("PASS: loop %s: %u of %u cases identical\n", parts[p].name, owned, parts[p].cases);
    }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
