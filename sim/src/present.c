/* The presentation frame of the own dbsim.exe (presentation.md, "the
 * presentation frame in the own process"). Every function names the listing
 * range it follows ("listing" = llvm-objdump -d -M intel of the reference
 * DBSIM.EXE, SHA-256 8561aa47...c4ad) and the registers it leaves; the palette
 * functions are compared with the original's machine code in vitro
 * (present_cases.h, :differential_present_test). Guest state only through the
 * bound loop's guest view; calls of other DBSIM code through the dispatch. */
#include "es2/sim/present.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"
#include "es2/sim/machine_sim.h"
#include "tools/reference-game/proxy/sha256.h"
#include "tools/reference-game/proxy/frame_regions.h"
#include <string.h>

/* ---- guest VAs */
#define VA_CLOCK 0x00467828u
#define VA_PALETTE_ENTRIES 0x004303c4u   /* row: the range copy into [P+8] with the dirty range */
#define VA_PALETTE_FLUSH 0x0043048cu     /* row */
#define VA_PALETTE_CYCLE 0x004306acu
#define VA_DISPLAY_ENTER 0x0042a9c0u     /* display.c's row */
#define VA_DISPLAY_LEAVE 0x0042a4c4u     /* display.c's row */
#define VA_PALETTE_CURRENT 0x0049b020u   /* P: the current game palette */
#define VA_DIRTY_FIRST 0x0049b028u
#define VA_DIRTY_LAST 0x0049b02cu
#define VA_DIRTY 0x0049b030u             /* byte */
#define VA_FADE_DEADLINE 0x0049b038u
#define VA_FADE_RUNNING 0x0049b03cu      /* byte */
#define VA_CYCLE_RANGES 0x004cfd64u      /* 5 dwords: the registered range records R */
#define VA_CYCLE_DEADLINES 0x004cfd78u   /* 5 dwords */
#define VA_CYCLE_INDEX 0x004cfd8cu       /* 5 words: the rotation index */
#define VA_FADE_FROM 0x004cfd98u         /* A */
#define VA_FADE_START 0x004cfd9cu        /* word: first entry (never written by code) */
#define VA_FADE_COUNT 0x004cfd9eu        /* word */
#define VA_FADE_DURATION 0x004cfda0u
#define VA_FADE_STEP0 0x004cfda4u        /* 256 words each: byte 0, 1, 2 steps */
#define VA_FADE_STEP1 0x004cffa4u
#define VA_FADE_STEP2 0x004d01a4u
#define VA_FADE_MODE 0x004d03a4u         /* word: 0 from black, 1 to black, 2 between palettes */
#define VA_SURFACE 0x004d24bcu           /* the frame surface */
#define VA_DISPLAY 0x004d24c0u
#define VA_PAGES_FLAG 0x004d24f2u        /* word */
#define VA_VIDEO_KIND 0x004d2537u        /* signed byte; 1: the surface switch around the present */
#define VA_SCREEN_W 0x004d253eu
#define VA_SCREEN_H 0x004d2542u
#define VA_DIB_W 0x004d2546u             /* also the DIB's row stride */
#define VA_DIB_H 0x004d254au
#define VA_DD_ON 0x004d255eu             /* the mode: 0 GDI, else DirectDraw */
#define VA_PRESENT_MODE 0x004d2566u      /* word: nonzero = present, 0 = page swap */
#define VA_PRESENT_SURFACE 0x004d2568u
#define VA_FRAME 0x004d257bu             /* the frame counter (unaligned dword) */
#define VA_PAGE_FLAG 0x004d267eu         /* word */
#define VA_RECT 0x004d2ff8u              /* R: left, top, right, bottom (inclusive) */
#define VA_PAGE 0x004d3018u              /* S: x, y */
#define VA_DIB_BITS 0x004d3028u
#define VA_PALETTE_HANDLE 0x004d302cu
#define VA_DIB_DC 0x004d3034u
#define VA_VIEW_DRAW 0x004a3682u
#define VA_VIEW_SHOW 0x004a368au
#define VA_SLOT_SHOW 0x004a57f0u         /* driver slot: the page shown */
#define VA_SLOT_DRAW 0x004a57f4u         /* driver slot: the page drawn */
#define VA_CURRENT_SURFACE 0x006c5ed0u
#define SITE_FRAME 0x0045fbe4u           /* return address of 0x45fba4's call of 0x464a14 */
#define VA_PRESENT_ROW 0x00464a14u
#define VA_CODE_LO 0x00401000u           /* DBSIM's CODE section [0x401000, 0x497000) */
#define VA_CODE_HI 0x00497000u
#define VA_HWND 0x004d3610u
#define VA_DD_SWITCH 0x004667c8u         /* fullscreen.h's row fullscreen_switch */
#define VA_FRAME_CLEAR 0x0046548cu       /* row frame_clear */
#define VA_DD_RELEASE 0x00466730u        /* presentation_roots.h's row dd_release */
#define VA_TIMER_KILL 0x004650d0u        /* shell_leftovers.h's row timer_kill */
#define VA_GET_WINDOW_RECT 0x0049694cu   /* thunk rows (input_layer.h, cursor_warp.h) */
#define VA_GET_CURSOR_POS 0x0049696au
#define VA_SET_CURSOR_POS 0x004969e2u
#define VA_DD_OBJECTS 0x004d3710u        /* DirectDraw, primary, back buffer, palette: 0x4d3710..0x4d371c */
#define VA_DD_PRIMARY 0x004d3714u
#define VA_DD_BACK 0x004d3718u
#define VA_DD_PALETTE 0x004d371cu
#define VA_WINDOW_RECT 0x004d36ccu       /* the windowed rectangle the way back restores */
#define VA_OWN_CHANGE 0x004d36fcu        /* 1: the next WM_DISPLAYCHANGE is the game's own (no 0x812 posted) */
#define VA_RECT_KEPT 0x004d3700u         /* 1: 0x4d36cc still holds the start-up's rectangle */
#define VA_CURSOR_SPRITE 0x004d3724u
#define VA_PAL_ENTRIES 0x004d372cu       /* the LOGPALETTE's 256 entries */
#define VA_HUD 0x0049b088u               /* H */

/* ---- the instance and the guest view */
static es2_sim_present *bound_present;
static es2_sim_present own_present;
void es2_sim_present_bind(es2_sim_present *p) { bound_present = p; }
es2_sim_present *es2_sim_present_bound(void) { return bound_present; }
void es2_sim_present_headless(es2_sim_present *p, es2_sim_loop *loop) {
    memset(p, 0, sizeof(*p));
    p->loop = loop;
    /* A 32-bit desktop: GetDeviceCaps(NUMCOLORS) is -1, GetSystemPaletteEntries
     * fills nothing (the same answers as the load layer's). */
    p->platform.num_colors = -1;
    p->platform.system_count = 0;
    for (uint32_t i = 0; i < 4u; ++i) p->platform.dd_object[i] = ES2_SIM_DD_STANDIN + i;
    p->ordinal_frame = 0xffffffffu;
}

typedef struct pctx { es2_sim_present *p; es2_sim_loop *l; const es2_guest_memory *m; } pctx;
static uint32_t r32(const pctx *c, uint32_t a) { return c->m->read32(c->m->context, a); }
static uint16_t r16(const pctx *c, uint32_t a) { return c->m->read16(c->m->context, a); }
static uint8_t r8(const pctx *c, uint32_t a) { return es2_sim_read8(c->m, a); }
static void w32(const pctx *c, uint32_t a, uint32_t v) { c->m->write32(c->m->context, a, v); }
static void w16(const pctx *c, uint32_t a, uint16_t v) { c->m->write16(c->m->context, a, v); }
static void w8(const pctx *c, uint32_t a, uint8_t v) { es2_sim_write8(c->m, a, v); }
static int32_t sx16(uint16_t v) { return (int32_t)(int16_t)v; }

static int context(es2_sim_present *p, pctx *c) {
    if (!p) {
        p = &own_present;
        if (!p->loop) es2_sim_present_headless(p, es2_sim_loop_bound());
        p->loop = es2_sim_loop_bound();
    }
    if (!p->loop || !p->loop->memory) return 1;
    c->p = p;
    c->l = p->loop;
    c->m = c->l->memory;
    return 0;
}
static int serving(es2_sim_service_call *sc, pctx *c) {
    if (context(bound_present, c)) { sc->refused = 1; sc->reason = "no main loop bound"; return 1; }
    return 0;
}
static void failed(es2_sim_service_call *sc) { sc->refused = 1; sc->reason = "a call inside the service was refused"; }
#define SERVE(sc, c) pctx c; if (serving((sc), &c)) return
static int refuse(pctx *c, uint32_t va, const char *name, const char *reason) {
    c->p->refused_va = va;
    return es2_sim_loop_refuse(c->l, va, name, reason);
}
/* The original reads, writes or calls through NULL at va (es2_sim_loop_null_refuse). */
static int null_refuse(pctx *c, uint32_t va, const char *name, const char *reason) {
    c->p->refused_va = va;
    return es2_sim_loop_null_refuse(c->l, va, name, reason);
}
/* D: the original's IDIV of EDX:EAX = dividend by the divisor faults at va
 * (es2_sim_loop_divide_refuse). */
static int divide_refuse(pctx *c, uint32_t va, uint64_t dividend, uint32_t divisor, const char *name, const char *reason) {
    c->p->refused_va = va;
    return es2_sim_loop_divide_refuse(c->l, va, dividend, divisor, 1, name, reason);
}
static int call(pctx *c, uint32_t va, const uint32_t *slots, unsigned count, es2_sim_result *out) {
    es2_sim_result r;
    if (es2_sim_loop_call(c->l, va, slots, count, out ? out : &r)) { c->p->refused_va = va; return 1; }
    return 0;
}
/* The table SetDIBColorTable received last: the load layer's record when
 * one is bound (0x465e28 writes it there), else the instance's own. */
static es2_sim_load_platform *table(pctx *c) {
    es2_sim_load *ld = es2_sim_load_bound();
    return ld && ld->loop == c->l ? &ld->platform : &c->p->own_table;
}
const es2_sim_load_platform *es2_sim_present_table(es2_sim_present *p) {
    pctx c;
    if (context(p, &c)) return NULL;
    return table(&c);
}

/* ---- the modelled DirectDraw and window calls (es2/sim/present.h) */
static void note(pctx *c, uint32_t id, const uint32_t *a, unsigned n) {
    es2_sim_present_platform *pl = &c->p->platform;
    uint32_t v[7] = {0, 0, 0, 0, 0, 0, 0};
    memcpy(v, a, 4u * n);
    ++pl->dd_calls[id];
    if (pl->dd_note) pl->dd_note(pl->dd_note_user, id, v);
}
#define NOTE(c, id, ...) do { const uint32_t a_[] = {__VA_ARGS__}; note((c), (id), a_, (unsigned)(sizeof(a_) / sizeof(a_[0]))); } while (0)
static uint32_t dd_answer(const pctx *c, uint32_t bit) { return c->p->platform.dd_fail & bit ? ES2_SIM_DD_ERROR : 0; }
/* Release: a stand-in is the platform's; any other object's slot +8
 * (stdcall this) through the dispatch. */
static int dd_object_release(pctx *c, uint32_t o) {
    if (ES2_SIM_DD_IS_STANDIN(o)) { NOTE(c, ES2_SIM_DD_RELEASE, o); return 0; }
    uint32_t a[1] = {o};
    return call(c, r32(c, r32(c, o) + 8u), a, 1, NULL);
}
/* A method call through a null object faults in the original. */
static int dd_null(pctx *c, uint32_t object_va, uint32_t va, const char *name) {
    if (r32(c, object_va)) return 0;
    return null_refuse(c, va, name, "a DirectDraw method called through a null object faults");
}

/* ======================================================================
 * The palette cycler 0x4306ac (listing 0x4306ac..0x4307ad): T = the clock
 * (the one call at 0x4306b6); for k = 0..4 with R = [0x4cfd64 + 4k] nonzero
 * and the deadline [0x4cfd78 + 4k] below T (unsigned): first = sx [R], n =
 * sx [R+2], base = [P+8] + 4 first, end = base + 4n - 4, src = base + 4 sx
 * (word [0x4cfd8c + 2k]); n dwords into a local buffer of 0x400 bytes, src
 * wrapping to base when above end (unsigned) before each read; then
 * 0x4303c4(first, n, buffer) (also for n <= 0); the word index + 1, reset
 * to 0 when it equals n; the deadline <- sx [R+4] + T. EAX 0 at the start;
 * per slot EDX the slot's address; a slot waiting: EAX its deadline's
 * address, EDX the deadline; a slot cycled: EAX the new deadline, EDX its
 * address, ECX the index word's address. No caller reads them. */
static int cycle(pctx *c, uint32_t regs[3]) {
    es2_sim_result t;
    if (call(c, VA_CLOCK, NULL, 0, &t)) return 1;
    uint32_t now = t.eax, eax = 0, edx = t.edx, ecx = t.ecx;
    for (uint32_t k = 0; k < 5u; ++k) {
        uint32_t slot = VA_CYCLE_RANGES + 4u * k, deadline = VA_CYCLE_DEADLINES + 4u * k, index = VA_CYCLE_INDEX + 2u * k;
        edx = slot;
        uint32_t r = r32(c, slot);
        if (!r) continue;
        eax = deadline;
        edx = r32(c, deadline);
        if (edx >= now) continue;
        int32_t first = sx16(r16(c, r)), n = sx16(r16(c, r + 2u));
        uint32_t base = r32(c, r32(c, VA_PALETTE_CURRENT) + 8u) + 4u * (uint32_t)first;
        uint32_t end = base + 4u * (uint32_t)n - 4u, src = base + 4u * (uint32_t)sx16(r16(c, index));
        if (n > 0x100)
            return refuse(c, 0x00430733u, "palette cycle", "a range of more than 256 entries overflows the local buffer");
        uint32_t top = c->l->stack_hi;
        if (top - c->l->stack_lo < 0x800u) return refuse(c, VA_PALETTE_CYCLE, "palette cycle", "the own code's call area is exhausted");
        c->l->stack_hi -= 0x400u;
        uint32_t buffer = c->l->stack_hi;
        for (int32_t i = 0; i < n; ++i) {
            if (src > end) src = base;
            w32(c, buffer + 4u * (uint32_t)i, r32(c, src));
            src += 4u;
        }
        uint32_t a[3] = {(uint32_t)first, (uint32_t)n, buffer};
        int refused = call(c, VA_PALETTE_ENTRIES, a, 3, NULL);
        c->l->stack_hi = top;
        if (refused) return 1;
        w16(c, index, (uint16_t)(r16(c, index) + 1u));
        if (sx16(r16(c, index)) == n) w16(c, index, 0);
        eax = (uint32_t)sx16(r16(c, r + 4u)) + now;
        w32(c, deadline, eax);
        edx = deadline;
        ecx = index;
    }
    regs[0] = eax; regs[1] = edx; regs[2] = ecx;
    return 0;
}
void es2_sim_service_palette_cycle(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t regs[3];
    if (cycle(&c, regs)) { failed(sc); return; }
    sc->eax = regs[0]; sc->edx = regs[1]; sc->ecx = regs[2];
}

/* ======================================================================
 * The fade set-up 0x4308fc(A, B, d) (listing 0x4308fc..0x430b31): A ->
 * [0x4cfd98], d -> [0x4cfda0], byte [0x49b03c] <- 0; three word tables of
 * per-entry steps (byte 0 at 0x4cfda4, byte 1 at 0x4cffa4, byte 2 at
 * 0x4d01a4), each a signed idiv:
 * - A and B (mode 2): n = sx [A+4]; per entry of [A+8] / [B+8]
 *   ((b & 0xff) - (a & 0xff)) << 8, (b & 0xff00) - (a & 0xff00),
 *   ((b & 0xff0000) >> 8) - ((a & 0xff0000) >> 8), each / d;
 * - A only (mode 1): the same of A's entry alone, / -d;
 * - B only (mode 0): n = sx [B+4]; B's entry's three values / d;
 * then word [0x4cfd9e] <- n. Neither: the count is an uninitialised local
 * (refused). EAX: mode 2 and 0 n (0 when n <= 0), mode 1 n. No caller
 * reads a register. */
static int fade_setup(pctx *c, uint32_t a, uint32_t b, uint32_t d, uint32_t *eax) {
    w32(c, VA_FADE_FROM, a);
    w32(c, VA_FADE_DURATION, d);
    w8(c, VA_FADE_RUNNING, 0);
    if (!a && !b) return refuse(c, 0x00430b20u, "fade set-up", "neither palette given: the count is an uninitialised local");
    int32_t n = sx16(r16(c, a ? a + 4u : b + 4u));
    if (n > 0x100) return refuse(c, 0x004308fcu, "fade set-up", "more than 256 entries overrun the step tables");
    if (n > 0 && !d) return divide_refuse(c, 0x00430984u, 0, 0, "fade set-up", "a zero duration divides by zero");
    int32_t div = a && !b ? (int32_t)(0u - d) : (int32_t)d;
    uint32_t s0 = VA_FADE_STEP0, s1 = VA_FADE_STEP1, s2 = VA_FADE_STEP2;
    uint32_t pa = a ? r32(c, a + 8u) : 0, pb = b ? r32(c, b + 8u) : 0;
    uint16_t mode = a && b ? 2u : a ? 1u : 0u;
    for (int32_t i = 0; i < n; ++i, s0 += 2u, s1 += 2u, s2 += 2u) {
        uint32_t v0, v1, v2;
        if (mode == 2u) {
            v0 = ((r32(c, pb) & 0xffu) - (r32(c, pa) & 0xffu)) << 8;
            w16(c, s0, (uint16_t)((int32_t)v0 / div));
            v1 = (r32(c, pb) & 0xff00u) - (r32(c, pa) & 0xff00u);
            w16(c, s1, (uint16_t)((int32_t)v1 / div));
            v2 = ((r32(c, pb) & 0xff0000u) >> 8) - ((r32(c, pa) & 0xff0000u) >> 8);
            w16(c, s2, (uint16_t)((int32_t)v2 / div));
            pa += 4u; pb += 4u;
        } else {
            uint32_t p = mode == 1u ? pa : pb;
            v0 = (r32(c, p) & 0xffu) << 8;
            w16(c, s0, (uint16_t)((int32_t)v0 / div));
            v1 = r32(c, p) & 0xff00u;
            w16(c, s1, (uint16_t)((int32_t)v1 / div));
            v2 = (r32(c, p) & 0xff0000u) >> 8;
            w16(c, s2, (uint16_t)((int32_t)v2 / div));
            if (mode == 1u) pa += 4u; else pb += 4u;
        }
    }
    w16(c, VA_FADE_MODE, mode);
    w16(c, VA_FADE_COUNT, (uint16_t)n);
    *eax = mode == 1u ? (uint32_t)n : n > 0 ? (uint32_t)n : 0u;
    return 0;
}
void es2_sim_service_fade_setup(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t eax = 0;
    if (fade_setup(&c, sc->slots[0], sc->slots[1], sc->slots[2], &eax)) { failed(sc); return; }
    sc->eax = eax;
}

/* ======================================================================
 * The fade step 0x430b34 (listing 0x430b34..0x430d07): T = the clock (the
 * one call at 0x430b3a); byte [0x49b03c] 0: the deadline [0x49b038] <- T +
 * [0x4cfda0], the byte <- 1; deadline below T (unsigned): the byte <- 0,
 * EAX 0 (EDX/ECX the clock call's). Otherwise e = [P+8] + 4 sx [0x4cfd9c],
 * t = [0x4cfda0] - (deadline - T); per entry i < sx [0x4cfd9e] (re-read
 * each pass) the step products v_j = ((word step_j[i] * t) & 0xff00) >> 8:
 * mode 0 bytes 0..2 <- v_j (byte 3 kept); modes 1 and 2 the dword <- A's
 * entry at the same index, then bytes 0..2 += v_j; other modes nothing.
 * Then the dirty range (start, start + count - 1) as 0x4303c4 widens it.
 * EAX deadline - T, EDX start + count - 1, ECX sx count. */
static int fade_step(pctx *c, uint32_t regs[3]) {
    es2_sim_result t;
    if (call(c, VA_CLOCK, NULL, 0, &t)) return 1;
    uint32_t now = t.eax;
    if (!r8(c, VA_FADE_RUNNING)) {
        w32(c, VA_FADE_DEADLINE, now + r32(c, VA_FADE_DURATION));
        w8(c, VA_FADE_RUNNING, 1);
    }
    uint32_t deadline = r32(c, VA_FADE_DEADLINE);
    if (deadline < now) {
        w8(c, VA_FADE_RUNNING, 0);
        regs[0] = 0; regs[1] = t.edx; regs[2] = t.ecx;
        return 0;
    }
    uint32_t e = r32(c, r32(c, VA_PALETTE_CURRENT) + 8u) + 4u * (uint32_t)sx16(r16(c, VA_FADE_START));
    uint32_t elapsed = r32(c, VA_FADE_DURATION) - (deadline - now);
    int32_t mode = sx16(r16(c, VA_FADE_MODE));
    uint32_t s0 = VA_FADE_STEP0, s1 = VA_FADE_STEP1, s2 = VA_FADE_STEP2;
    if (mode == 0) {
        for (int32_t i = 0; sx16(r16(c, VA_FADE_COUNT)) > i; ++i, e += 4u, s0 += 2u, s1 += 2u, s2 += 2u) {
            w8(c, e, (uint8_t)(((r16(c, s0) * elapsed) & 0xff00u) >> 8));
            w8(c, e + 1u, (uint8_t)(((r16(c, s1) * elapsed) & 0xff00u) >> 8));
            w8(c, e + 2u, (uint8_t)(((r16(c, s2) * elapsed) & 0xff00u) >> 8));
        }
    } else if (mode == 1 || mode == 2) {
        uint32_t src = r32(c, r32(c, VA_FADE_FROM) + 8u) + 4u * (uint32_t)sx16(r16(c, VA_FADE_START));
        for (int32_t i = 0; sx16(r16(c, VA_FADE_COUNT)) > i; ++i, e += 4u, src += 4u, s0 += 2u, s1 += 2u, s2 += 2u) {
            w32(c, e, r32(c, src));
            w8(c, e, (uint8_t)(r8(c, e) + (((r16(c, s0) * elapsed) & 0xff00u) >> 8)));
            w8(c, e + 1u, (uint8_t)(r8(c, e + 1u) + (((r16(c, s1) * elapsed) & 0xff00u) >> 8)));
            w8(c, e + 2u, (uint8_t)(r8(c, e + 2u) + (((r16(c, s2) * elapsed) & 0xff00u) >> 8)));
        }
    }
    int32_t first = sx16(r16(c, VA_FADE_START)), count = sx16(r16(c, VA_FADE_COUNT));
    uint32_t last = (uint32_t)first + (uint32_t)count - 1u;
    if (!r8(c, VA_DIRTY)) {
        w32(c, VA_DIRTY_FIRST, (uint32_t)first);
        w32(c, VA_DIRTY_LAST, last);
        w8(c, VA_DIRTY, 1);
    } else {
        if (first < (int32_t)r32(c, VA_DIRTY_FIRST)) w32(c, VA_DIRTY_FIRST, (uint32_t)first);
        if ((int32_t)last > (int32_t)r32(c, VA_DIRTY_LAST)) w32(c, VA_DIRTY_LAST, last);
    }
    regs[0] = r32(c, VA_FADE_DEADLINE) - now;
    regs[1] = last;
    regs[2] = (uint32_t)count;
    return 0;
}
void es2_sim_service_fade_step(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t regs[3];
    if (fade_step(&c, regs)) { failed(sc); return; }
    sc->eax = regs[0]; sc->edx = regs[1]; sc->ecx = regs[2];
}

/* ======================================================================
 * The palette upload 0x465cc4(P) (listing 0x465cc4..0x465e26): GetDC(0);
 * n = GetDeviceCaps(dc, 24 = NUMCOLORS); GetSystemPaletteEntries(dc, 0,
 * 256, E); r = n / 2 (sar with the sign fix-up: towards zero); flags of
 * E[0 .. r-1] <- 0; E[r .. 255-r] <- (byte 0, 1, 2 of [P+8]'s entry each
 * << 2, flags 5); flags of the rest <- 0; ReleaseDC. GDI mode: RGBQUADs
 * (blue, green, red from E; the fourth byte is never written, stack
 * residue: 0 here) to SetDIBColorTable([0x4d3034], 0, 256, quads), then
 * AnimatePalette([0x4d302c], 0, 256, E); EAX AnimatePalette's (TRUE).
 * DirectDraw mode (0x465e05..0x465e1e): neither of the two, but
 * [0x4d371c]->SetEntries(0, 0, 256, E), EAX its DD_OK (0); the presenter's
 * colour table is E as RGBQUADs, the table the hardware palette shows. r
 * above 128 leaves the middle range empty (every flag 0); r below 0 or above
 * 256 would write outside E in the original: refused. */
static int upload(pctx *c, uint32_t p, uint32_t *eax) {
    es2_sim_present_platform *pl = &c->p->platform;
    uint8_t e[1024];
    memset(e, 0, sizeof(e));
    ++pl->get_dc;
    ++pl->device_caps;
    int32_t n = pl->num_colors;
    ++pl->palette_entries;
    unsigned filled = pl->system_count < 256u ? pl->system_count : 256u;
    memcpy(e, pl->system, 4u * filled);
    int32_t r = n / 2;
    if (r < 0 || r > 0x100)
        return refuse(c, 0x00465d0fu, "palette upload", "a static colour count outside -1..513 writes outside the entry table");
    int32_t i = 0;
    for (; i < r; ++i) e[4 * i + 3] = 0;
    for (i = r; i < 0x100 - r; ++i) {
        uint32_t s = r32(c, p + 8u) + 4u * (uint32_t)i;
        e[4 * i] = (uint8_t)(r8(c, s) << 2);
        e[4 * i + 1] = (uint8_t)(r8(c, s + 1u) << 2);
        e[4 * i + 2] = (uint8_t)(r8(c, s + 2u) << 2);
        e[4 * i + 3] = 5;
    }
    for (; i < 0x100; ++i) e[4 * i + 3] = 0;
    ++pl->release_dc;
    uint8_t quads[1024];
    for (unsigned k = 0; k < 256u; ++k) {
        quads[4 * k] = e[4 * k + 2];
        quads[4 * k + 1] = e[4 * k + 1];
        quads[4 * k + 2] = e[4 * k];
        quads[4 * k + 3] = 0;
    }
    if (r32(c, VA_DD_ON)) {
        if (dd_null(c, VA_DD_PALETTE, 0x00465e1eu, "palette upload")) return 1;
        NOTE(c, ES2_SIM_DD_SET_ENTRIES, r32(c, VA_DD_PALETTE), 0u, 0u, 0x100u);
        es2_sim_load_platform *t = table(c);
        memcpy(t->color_table, quads, sizeof(quads));
        ++t->color_table_uploads;
        *eax = 0;
        return 0;
    }
    ++pl->color_table;
    if (r32(c, VA_DIB_DC)) {
        es2_sim_load_platform *t = table(c);
        memcpy(t->color_table, quads, sizeof(quads));
        ++t->color_table_uploads;
    }
    ++pl->animate;
    memcpy(pl->animate_table, e, sizeof(e));
    *eax = 1;
    return 0;
}
void es2_sim_service_palette_upload(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t eax = 0;
    if (upload(&c, sc->slots[0], &eax)) { failed(sc); return; }
    sc->eax = eax;
}

/* ======================================================================
 * The capture: one line per present in the record format of the proxy's
 * presenter (tools/reference-game/proxy/presenter.h), read by
 * tools/reference-game/scripts/dbsim_frames.py. */
static void capture_line(es2_sim_present *p, const char *line, int n) {
    if (!p->capture) return;
    if (n <= 0 || es2_os_fwrite(line, 1, (size_t)n, p->capture) != (size_t)n || es2_os_fflush(p->capture)) ++p->write_failures;
}
int es2_sim_present_capture_open(es2_sim_present *p, const char *path) {
    if (p->capture) es2_sim_present_capture_close(p);
    p->capture = es2_os_fopen(path, "wb");
    if (!p->capture) return 1;
    p->records = 0;
    p->write_failures = 0;
    char line[256];
    int n = snprintf(line, sizeof(line),
                     "{\"event\":\"start\",\"schema\":1,\"key\":\"frame_counter_0x4d257b\",\"presenter\":\"es2sim-headless\","
                     "\"dump_frames\":0,\"pid\":0,\"regions\":\"" FR_TABLE_ID "\"}\r\n");
    capture_line(p, line, n);
    return 0;
}
void es2_sim_present_capture_close(es2_sim_present *p) {
    if (!p->capture) return;
    /* The frame of the last record (no guest read: the close may run at exit). */
    pctx c;
    uint32_t frame = p->ordinal_frame == 0xffffffffu ? 0u : p->ordinal_frame;
    char line[320], dumped[96];
    dumped[0] = 0;
    if (p->dump_items) snprintf(dumped, sizeof(dumped), ",\"dumps\":%lu,\"dump_failures\":%lu", p->dumps, p->dump_failures);
    int n = snprintf(line, sizeof(line),
                     "{\"event\":\"stats\",\"frame\":%lu,\"records\":%lu,\"presents\":%lu,\"uploads\":%lu,\"write_failures\":%lu%s}\r\n",
                     (unsigned long)frame, p->records, p->presents,
                     context(p, &c) ? 0ul : (unsigned long)table(&c)->color_table_uploads, p->write_failures, dumped);
    capture_line(p, line, n);
    es2_os_fclose(p->capture);
    p->capture = NULL;
}
/* The frame dumps (present.h). */
int es2_sim_present_dump_open(es2_sim_present *p, const char *dir, const char *list) {
    p->dump_items = 0;
    p->dumps = p->dump_failures = 0;
    size_t length = strlen(dir);
    if (!length || length >= sizeof(p->dump_dir) - 40u) return 1;
    unsigned items = 0;
    uint32_t frames = 0;
    for (const char *s = list; *s;) {
        if (*s == ',' || *s == ' ') { ++s; continue; }
        uint32_t v[2] = {0, 0};
        for (unsigned k = 0; k < 2u; ++k) {
            if (*s < '0' || *s > '9') return 1;
            unsigned digits = 0;
            for (; *s >= '0' && *s <= '9'; ++s) {
                if (++digits > 9u) return 1;
                v[k] = v[k] * 10u + (uint32_t)(*s - '0');
            }
            if (k == 0u) {
                v[1] = v[0];
                if (*s != '-') break;
                ++s;
                v[1] = 0;
            }
        }
        if (*s && *s != ',' && *s != ' ') return 1;
        if (v[1] < v[0] || items == ES2_SIM_PRESENT_DUMP_ITEMS_MAX) return 1;
        frames += v[1] - v[0] + 1u;
        if (frames > ES2_SIM_PRESENT_DUMP_FRAMES_MAX) return 1;
        p->dump_from[items] = v[0];
        p->dump_to[items] = v[1];
        ++items;
    }
    if (!items) return 1;
    memcpy(p->dump_dir, dir, length + 1u);
    p->dump_items = items;
    return 0;
}
static int dump_wanted(const es2_sim_present *p, uint32_t frame) {
    if (p->dumps + p->dump_failures >= ES2_SIM_PRESENT_DUMP_FILES_MAX) return 0;
    for (unsigned i = 0; i < p->dump_items; ++i) if (frame >= p->dump_from[i] && frame <= p->dump_to[i]) return 1;
    return 0;
}
static void put32le(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
/* One ES2F file of the w x h window at `origin` (rows `stride` apart, each
 * row read through the guest view's host mapping) with the colour table rgb. */
static void dump_write(pctx *c, const es2_sim_present_record *r, uint32_t origin, int32_t stride, int32_t w, int32_t h, const uint8_t rgb[768]) {
    es2_sim_present *p = c->p;
    char path[320];
    if (snprintf(path, sizeof(path), "%s\\frame-%06lu-%06lx-%ld.es2f", p->dump_dir, (unsigned long)r->frame, (unsigned long)r->site,
                 (long)r->ordinal) >= (int)sizeof(path)) { ++p->dump_failures; return; }
    es2_os_stream *f = es2_os_fopen(path, "wb");
    if (!f) { ++p->dump_failures; return; }
    uint8_t header[32 + 768];
    memcpy(header, "ES2F", 4);
    const uint32_t fields[7] = {1u, r->frame, r->site, (uint32_t)r->ordinal, (uint32_t)w, (uint32_t)h, r->uploads};
    for (unsigned i = 0; i < 7u; ++i) put32le(header + 4u + 4u * i, fields[i]);
    memcpy(header + 32, rgb, 768);
    int ok = es2_os_fwrite(header, 1, sizeof(header), f) == sizeof(header);
    for (int32_t y = 0; ok && y < h; ++y) {
        uint32_t a = origin + (uint32_t)y * (uint32_t)stride;
        const uint8_t *row = c->m->host ? c->m->host(c->m->context, a) : NULL;
        const uint8_t *row_end = c->m->host ? c->m->host(c->m->context, a + (uint32_t)w - 1u) : NULL;
        ok = row && row_end == row + (w - 1) && es2_os_fwrite(row, 1, (size_t)w, f) == (size_t)w;
    }
    if (es2_os_fclose(f)) ok = 0;
    if (ok) ++p->dumps;
    else {
        ++p->dump_failures;
        es2_os_remove(path);
    }
}
static int32_t next_ordinal(es2_sim_present *p, uint32_t frame, uint32_t site) {
    if (frame != p->ordinal_frame) { p->ordinal_frame = frame; p->ordinal_used = 0; }
    for (unsigned i = 0; i < p->ordinal_used; ++i) if (p->ordinal_sites[i] == site) return (int32_t)p->ordinal_counts[i]++;
    if (p->ordinal_used == ES2_SIM_PRESENT_SITES_MAX) return -1;
    p->ordinal_sites[p->ordinal_used] = site;
    p->ordinal_counts[p->ordinal_used] = 1;
    ++p->ordinal_used;
    return 0;
}
/* SHA-256 of the w x h window at the page origin, rows `stride` apart, read
 * through the guest view's host mapping row by row. 0, or 1 when a row is
 * not mapped. */
static int hash_window(pctx *c, uint32_t origin, int32_t stride, int32_t w, int32_t h, char hex[65]) {
    sha256_ctx s;
    sha256_init(&s);
    for (int32_t y = 0; y < h; ++y) {
        uint32_t a = origin + (uint32_t)y * (uint32_t)stride;
        const uint8_t *row = c->m->host ? c->m->host(c->m->context, a) : NULL;
        const uint8_t *row_end = c->m->host ? c->m->host(c->m->context, a + (uint32_t)w - 1u) : NULL;
        if (!row || row_end != row + (w - 1)) return 1;
        sha256_update(&s, row, (size_t)w);
    }
    sha256_hex(&s, hex);
    return 0;
}
/* The region hashes' row source (frame_regions.h): row y of the window at
 * `origin`, through the guest view's host mapping. */
typedef struct region_rows { pctx *c; uint32_t origin; int32_t stride, w; } region_rows;
static const uint8_t *region_row(void *context, int32_t y) {
    region_rows *r = context;
    uint32_t a = r->origin + (uint32_t)y * (uint32_t)r->stride;
    const uint8_t *row = r->c->m->host ? r->c->m->host(r->c->m->context, a) : NULL;
    const uint8_t *row_end = r->c->m->host ? r->c->m->host(r->c->m->context, a + (uint32_t)r->w - 1u) : NULL;
    return row && row_end == row + (r->w - 1) ? row : NULL;
}
static void capture(pctx *c, uint32_t site, const int32_t client[4], const int32_t src[4], const int32_t dst[4]) {
    es2_sim_present *p = c->p;
    if (!p->capture && !p->on_present && !p->dump_items) return;
    es2_sim_present_record r;
    memset(&r, 0, sizeof(r));
    r.frame = r32(c, VA_FRAME);
    r.site = site;
    r.mode = r32(c, VA_DD_ON);
    r.page[0] = (int32_t)r32(c, VA_PAGE); r.page[1] = (int32_t)r32(c, VA_PAGE + 4u);
    for (unsigned i = 0; i < 4u; ++i) {
        r.rect[i] = (int32_t)r32(c, VA_RECT + 4u * i);
        r.client[i] = client[i]; r.src[i] = src[i]; r.dst[i] = dst[i];
    }
    int32_t stride = (int32_t)r32(c, VA_DIB_W), dib_h = (int32_t)r32(c, VA_DIB_H);
    uint32_t bits = r32(c, VA_DIB_BITS);
    int inside = bits && src[0] >= 0 && src[1] >= 0 && src[2] > 0 && src[3] > 0 && stride > 0 && dib_h > 0 &&
                 src[0] <= stride - src[2] && src[1] <= dib_h - src[3];
    if (inside && hash_window(c, bits + (uint32_t)src[1] * (uint32_t)stride + (uint32_t)src[0], stride, src[2], src[3], r.index_sha256)) {
        inside = 0;
        r.index_sha256[0] = 0;
    }
    r.source = inside ? "dib" : "dib_outside";
    /* The region hashes of the same window, by the display mode [[0x4d24c0]+0x14]. */
    char regions[FR_REGION_COUNT][FR_HASH_DIGITS + 1], region_fields[200];
    region_fields[0] = 0;
    if (inside && p->capture) {
        uint32_t d = r32(c, FR_DISPLAY_VA);
        int32_t display = d ? (int32_t)r32(c, d + FR_DISPLAY_MODE_OFFSET) : -1;
        region_rows rows = {c, bits + (uint32_t)src[1] * (uint32_t)stride + (uint32_t)src[0], stride, src[2]};
        if (fr_region_hashes(display, src[2], src[3], region_row, &rows, regions) ||
            fr_format(region_fields, sizeof(region_fields), display, regions) < 0)
            region_fields[0] = 0;
    }
    const es2_sim_load_platform *t = table(c);
    r.uploads = t->color_table_uploads;
    uint8_t rgb[768];
    if (t->color_table_uploads) {
        for (unsigned i = 0; i < 256u; ++i) {
            rgb[3 * i] = t->color_table[4 * i + 2];
            rgb[3 * i + 1] = t->color_table[4 * i + 1];
            rgb[3 * i + 2] = t->color_table[4 * i];
        }
        sha256_ctx s;
        sha256_init(&s);
        sha256_update(&s, rgb, sizeof(rgb));
        sha256_hex(&s, r.palette_sha256);
    }
    r.ordinal = next_ordinal(p, r.frame, r.site);
    /* The dump of a named frame (window inside the DIB, a table uploaded, as the proxy). */
    if (p->dump_items && inside && t->color_table_uploads && r.ordinal >= 0 && dump_wanted(p, r.frame))
        dump_write(c, &r, bits + (uint32_t)src[1] * (uint32_t)stride + (uint32_t)src[0], stride, src[2], src[3], rgb);
    if (p->capture) {
        int index = r.index_sha256[0] != 0, palette = r.palette_sha256[0] != 0;
        char line[1024];
        int n = snprintf(line, sizeof(line),
            "{\"frame\":%lu,\"site\":\"0x%06lx\",\"ordinal\":%ld,\"mode\":%lu,\"S\":[%ld,%ld],\"R\":[%ld,%ld,%ld,%ld],"
            "\"client\":[%ld,%ld,%ld,%ld],\"src\":[%ld,%ld,%ld,%ld],\"dst\":[%ld,%ld,%ld,%ld],\"source\":\"%s\","
            "\"index_sha256\":%s%s%s,\"palette_sha256\":%s%s%s,\"uploads\":%lu,\"presented\":\"headless\"%s}\r\n",
            (unsigned long)r.frame, (unsigned long)r.site, (long)r.ordinal, (unsigned long)r.mode, (long)r.page[0], (long)r.page[1],
            (long)r.rect[0], (long)r.rect[1], (long)r.rect[2], (long)r.rect[3],
            (long)r.client[0], (long)r.client[1], (long)r.client[2], (long)r.client[3],
            (long)r.src[0], (long)r.src[1], (long)r.src[2], (long)r.src[3],
            (long)r.dst[0], (long)r.dst[1], (long)r.dst[2], (long)r.dst[3], r.source,
            index ? "\"" : "", index ? r.index_sha256 : "null", index ? "\"" : "",
            palette ? "\"" : "", palette ? r.palette_sha256 : "null", palette ? "\"" : "",
            (unsigned long)r.uploads, region_fields);
        capture_line(p, line, n);
        ++p->records;
    }
    if (p->on_present) p->on_present(p->user, &r);
}

/* ======================================================================
 * The present 0x464a14 (listing 0x464a14..0x464a26: 0x465628(S = 0x4d3018,
 * R = 0x4d2ff8)) and the GDI branch of 0x465628 (listing 0x46589a..
 * 0x4659df): nothing without the DIB DC [0x4d3034]; GetClientRect,
 * InvalidateRect(client, TRUE), BeginPaint; with the palette [0x4d302c]
 * SelectPalette(FALSE) and RealizePalette; the scales sx = (client.right
 * << 16) / [0x4d253e], sy = (client.bottom << 16) / [0x4d2542] (idiv);
 * dx = (sx16 R.left * sx) >> 16, dy = (sx16 R.top * sy) >> 16; w = R.right
 * - R.left + 1, h = R.bottom - R.top + 1; dw = client.right when w is the
 * game width, else (sx16 w * sx) >> 16; dh alike; StretchBlt(dc, dx, dy,
 * dw, dh, [0x4d3034], S.x, S.y, w, h, SRCCOPY); with the palette (read
 * again) the old one reselected (TRUE) and RealizePalette once more;
 * EndPaint. No guest write. EAX 1 (as the load layer's row). The present
 * count of the load layer's row is kept.
 * The DirectDraw branch (0x465647..0x465895), no guest write either: the
 * primary [0x4d3714]'s IsLost, on DDERR_SURFACELOST its Restore; the target
 * is the primary, or the back buffer [0x4d3718] when [0x4d253e] < 640
 * (signed); its Lock (an error skips the frame); the window of w = R.right -
 * R.left + 1 columns and h = min(R.bottom - R.top + 1, [0x4d254a] - S.y)
 * rows from the DIB at S into the surface at (R.left, R.top); with the cursor
 * sprite [0x4d3724] GetCursorPos (the row; the sprite itself is not drawn,
 * the system cursor stays: presentation.md, "the DirectDraw full-screen
 * switch"); Unlock; Flip below 640. The record: client (0, 0, game size),
 * src (S, w, h), dst (R.left, R.top, w, h). */
static int present_dd(pctx *c, uint32_t site) {
    es2_sim_present_platform *pl = &c->p->platform;
    if (dd_null(c, VA_DD_PRIMARY, 0x00465647u, "present")) return 1;
    NOTE(c, ES2_SIM_DD_IS_LOST, r32(c, VA_DD_PRIMARY));
    if (pl->dd_fail & ES2_SIM_DD_FAIL_LOST) NOTE(c, ES2_SIM_DD_RESTORE, r32(c, VA_DD_PRIMARY));
    int32_t screen_w = (int32_t)r32(c, VA_SCREEN_W), screen_h = (int32_t)r32(c, VA_SCREEN_H);
    uint32_t target_va = screen_w < 0x280 ? VA_DD_BACK : VA_DD_PRIMARY;
    if (dd_null(c, target_va, 0x0046569eu, "present")) return 1;
    uint32_t target = r32(c, target_va);
    NOTE(c, ES2_SIM_DD_LOCK, target, 0u, 0x6cu, 1u, 0u);
    if (dd_answer(c, ES2_SIM_DD_FAIL_LOCK)) return 0;
    ++table(c)->presents;
    ++c->p->presents;
    int32_t left = (int32_t)r32(c, VA_RECT), top = (int32_t)r32(c, VA_RECT + 4u);
    int32_t right = (int32_t)r32(c, VA_RECT + 8u), bottom = (int32_t)r32(c, VA_RECT + 12u);
    int32_t sx = (int32_t)r32(c, VA_PAGE), sy = (int32_t)r32(c, VA_PAGE + 4u);
    int32_t w = (int32_t)((uint32_t)right - (uint32_t)left + 1u), h = (int32_t)((uint32_t)bottom - (uint32_t)top + 1u);
    int32_t rows = (int32_t)(r32(c, VA_DIB_H) - (uint32_t)sy);
    if (rows < h) h = rows;
    if (r32(c, VA_CURSOR_SPRITE)) {
        uint32_t a[1] = {0};
        if (call(c, VA_GET_CURSOR_POS, a, 1, NULL)) return 1;
    }
    NOTE(c, ES2_SIM_DD_UNLOCK, target, 0u);
    if (screen_w < 0x280) NOTE(c, ES2_SIM_DD_FLIP, r32(c, VA_DD_PRIMARY), 0u, 1u);
    int32_t client[4] = {0, 0, screen_w, screen_h}, src[4] = {sx, sy, w, h}, dst[4] = {left, top, w, h};
    memcpy(pl->src, src, sizeof(src));
    memcpy(pl->dst, dst, sizeof(dst));
    memcpy(pl->client, client, sizeof(client));
    capture(c, site, client, src, dst);
    return 0;
}
static int present(pctx *c, uint32_t site) {
    es2_sim_present_platform *pl = &c->p->platform;
    if (r32(c, VA_DD_ON)) return present_dd(c, site);
    if (!r32(c, VA_DIB_DC)) return 0;
    ++table(c)->presents;
    ++c->p->presents;
    int32_t screen_w = (int32_t)r32(c, VA_SCREEN_W), screen_h = (int32_t)r32(c, VA_SCREEN_H);
    int32_t client[4] = {0, 0, pl->client_w ? pl->client_w : screen_w, pl->client_h ? pl->client_h : screen_h};
    ++pl->client_rect; ++pl->invalidate; ++pl->begin_paint;
    int palette = r32(c, VA_PALETTE_HANDLE) != 0;
    if (palette) { ++pl->select_palette; ++pl->realize_palette; }
    if (!screen_w) return divide_refuse(c, 0x0046590du, 0, 0, "present", "a zero game size divides by zero");
    if (!screen_h) return divide_refuse(c, 0x00465920u, 0, 0, "present", "a zero game size divides by zero");
    int64_t qx = (int64_t)(int32_t)((uint32_t)client[2] << 16) / screen_w, qy = (int64_t)(int32_t)((uint32_t)client[3] << 16) / screen_h;
    if (qx != (int32_t)qx)
        return divide_refuse(c, 0x0046590du, (uint64_t)(int64_t)(int32_t)((uint32_t)client[2] << 16), (uint32_t)screen_w, "present",
                             "the scale overflows idiv");
    if (qy != (int32_t)qy)
        return divide_refuse(c, 0x00465920u, (uint64_t)(int64_t)(int32_t)((uint32_t)client[3] << 16), (uint32_t)screen_h, "present",
                             "the scale overflows idiv");
    int32_t sx = (int32_t)qx, sy = (int32_t)qy;
    int32_t left = (int32_t)r32(c, VA_RECT), top = (int32_t)r32(c, VA_RECT + 4u);
    int32_t right = (int32_t)r32(c, VA_RECT + 8u), bottom = (int32_t)r32(c, VA_RECT + 12u);
    int32_t w = (int32_t)((uint32_t)right - (uint32_t)left + 1u), h = (int32_t)((uint32_t)bottom - (uint32_t)top + 1u);
    int32_t dst[4];
    dst[0] = (int32_t)((uint32_t)sx16((uint16_t)left) * (uint32_t)sx) >> 16;
    dst[1] = (int32_t)((uint32_t)sx16((uint16_t)top) * (uint32_t)sy) >> 16;
    dst[2] = w == screen_w ? client[2] : (int32_t)((uint32_t)sx16((uint16_t)w) * (uint32_t)sx) >> 16;
    dst[3] = h == screen_h ? client[3] : (int32_t)((uint32_t)sx16((uint16_t)h) * (uint32_t)sy) >> 16;
    int32_t src[4] = {(int32_t)r32(c, VA_PAGE), (int32_t)r32(c, VA_PAGE + 4u), w, h};
    ++pl->stretch_blt;
    memcpy(pl->src, src, sizeof(src));
    memcpy(pl->dst, dst, sizeof(dst));
    memcpy(pl->client, client, sizeof(client));
    if (r32(c, VA_PALETTE_HANDLE)) { ++pl->select_palette; ++pl->realize_palette; }   /* re-read at 0x4659b2 */
    ++pl->end_paint;
    capture(c, site, client, src, dst);
    return 0;
}
int es2_sim_present_frame(es2_sim_present *p, uint32_t site) {
    pctx c;
    if (context(p, &c)) return 1;
    return present(&c, site);
}

/* The call site of a present through the row (es2/sim/present.h). The own
 * entries whose machine code calls 0x464a14 once, by the entry's VA:
 * the mission load 0x461608 (core mission_root, the call at 0x461e1c). */
static uint32_t pending_site;
void es2_sim_present_site(uint32_t site) { pending_site = site; }
static const struct { uint32_t entry, site; } entry_sites[] = {{0x00461608u, 0x00461e21u}};
static uint32_t call_site(pctx *c, uint32_t ret) {
    uint32_t site = pending_site;
    pending_site = 0;
    if (ret >= VA_CODE_LO && ret < VA_CODE_HI) return ret;
    if (site) return site;
    /* A call of the row (a machine call, return address the sentinel, or a
     * trapped one) from the code of the guest entry around it. */
    uint32_t outer = ret && es2_sim_guest_entry_va(0) == VA_PRESENT_ROW ? es2_sim_guest_entry_va(1) : 0u;
    if (outer) {
        for (unsigned i = 0; i < sizeof(entry_sites) / sizeof(entry_sites[0]); ++i)
            if (entry_sites[i].entry == outer) return entry_sites[i].site;
    }
    ++c->p->unattributed;
    return ret;
}
void es2_sim_service_present_capture(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    if (present(&c, call_site(&c, sc->return_address))) { failed(sc); return; }
    sc->eax = 1;
}

/* 0x4669c4(file) (listing 0x4669c4..0x4669ea): with DirectDraw on (dword
 * [0x4d255e]) the switch 0x4667c8 first (through the dispatch: back to
 * the window); then WinHelpA([0x4d3610], file, 3 = HELP_CONTENTS, 0)
 * through the thunk 0x4969ac. Headless WinHelp is a missing system
 * component: the call is counted with its arguments
 * and EAX is the platform's answer. EDX and ECX: the entry's (the
 * original's are WinHelpA's; the callers 0x4345d1 and 0x460827 pop ECX
 * and do not read EDX). */
void es2_sim_service_help_4669c4(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    if (r32(&c, VA_DD_ON) && call(&c, VA_DD_SWITCH, NULL, 0, NULL)) { failed(sc); return; }
    es2_sim_present_platform *pl = &c.p->platform;
    ++pl->win_help;
    pl->win_help_args[0] = r32(&c, VA_HWND);
    pl->win_help_args[1] = sc->slots[0];
    pl->win_help_args[2] = 3u;
    pl->win_help_args[3] = 0u;
    sc->eax = pl->win_help_answer;
}

/* 0x466730() (listing 0x466730..0x4667c7): with the DirectDraw object
 * [0x4d3710] the frame clear 0x46548c; then each object of [0x4d3718],
 * [0x4d3714], [0x4d371c], [0x4d3710] (in that order) that is set:
 * Release (vtable slot +8, stdcall this) and the dword zeroed; then
 * ClipCursor({0, 0, GetSystemMetrics(SM_CXSCREEN) - 1,
 * GetSystemMetrics(SM_CYSCREEN) - 1}). A stand-in object of the modelled
 * DirectDraw is released by the platform; any other object's Release goes
 * through the dispatch by the method's address. EAX: ClipCursor's TRUE; EDX and
 * ECX the entry's (the original's are the system calls'; WinMain 0x46547a
 * and 0x4667c8 at 0x466944 do not read them). */
static int dd_release(pctx *c) {
    if (r32(c, VA_DD_OBJECTS) && call(c, VA_FRAME_CLEAR, NULL, 0, NULL)) return 1;
    static const uint32_t order[4] = {VA_DD_OBJECTS + 8u, VA_DD_OBJECTS + 4u, VA_DD_OBJECTS + 0xcu, VA_DD_OBJECTS};
    for (unsigned i = 0; i < 4u; ++i) {
        uint32_t o = r32(c, order[i]);
        if (!o) continue;
        if (dd_object_release(c, o)) return 1;
        w32(c, order[i], 0);
    }
    es2_sim_present_platform *pl = &c->p->platform;
    pl->system_metrics += 2u;
    pl->clip_rect[0] = 0;
    pl->clip_rect[1] = 0;
    pl->clip_rect[2] = (int32_t)((pl->screen_cx ? pl->screen_cx : 640u) - 1u);
    pl->clip_rect[3] = (int32_t)((pl->screen_cy ? pl->screen_cy : 480u) - 1u);
    ++pl->clip_cursor;
    return 0;
}
void es2_sim_service_dd_release_466730(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    if (dd_release(&c)) { failed(sc); return; }
    sc->eax = 1;
}

/* ======================================================================
 * The DirectDraw full-screen switch 0x4667c8 (listing 0x4667c8..0x4669b1;
 * presentation.md, "The DirectDraw full-screen switch"), with 0x46658c
 * (0x46658c..0x4665c2), 0x4665c4 (0x4665c4..0x46672d) and 0x45f13c
 * (0x45f13c..0x45f15f) inline: the switch is their only caller. */
static uint32_t half(uint32_t v) { return (uint32_t)((int32_t)v / 2); }   /* sar with the sign fix-up */
static void display_request(pctx *c, int fullscreen) {
    es2_sim_present_platform *pl = &c->p->platform;
    pl->fullscreen = fullscreen;
    ++pl->display_requests;
    if (pl->display_mode) pl->display_mode(pl->display_user, fullscreen);
}
/* 0x46658c: DirectDrawCreate(NULL, 0x4d3710, NULL), then
 * SetCooperativeLevel(hwnd, 0x57 = DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT |
 * DDSCL_EXCLUSIVE | DDSCL_ALLOWMODEX); 1, or 0 at the first error. A modelled
 * error writes no object. */
static int dd_create(pctx *c) {
    NOTE(c, ES2_SIM_DD_CREATE, 0u, VA_DD_OBJECTS, 0u);
    if (dd_answer(c, ES2_SIM_DD_FAIL_CREATE)) return 0;
    w32(c, VA_DD_OBJECTS, c->p->platform.dd_object[0]);
    NOTE(c, ES2_SIM_DD_COOPERATIVE, r32(c, VA_DD_OBJECTS), r32(c, VA_HWND), 0x57u);
    return !dd_answer(c, ES2_SIM_DD_FAIL_COOPERATIVE);
}
/* 0x4665c4(w, h, bpp): SetDisplayMode(w, h, bpp); the back buffer, the
 * primary and the palette released and zeroed (each when set); the primary
 * from a zeroed 0x6c-byte DDSURFACEDESC on the stack: [0x4d253e] < 640
 * (signed) flags 0x20 (DDSD_BACKBUFFERCOUNT only) with 1 back buffer and caps
 * 0x4200 | 0x200018, else flags 0, count 0 and caps 0x4200; after an error
 * once more without caps bit 0x4000; below 640
 * the back buffer by GetAttachedSurface(DDSCAPS_BACKBUFFER 4). 1, or 0 at the
 * first error; -1 when a Release was refused. */
static int dd_mode(pctx *c, uint32_t w, uint32_t h, uint32_t bpp) {
    const es2_sim_present_platform *pl = &c->p->platform;
    NOTE(c, ES2_SIM_DD_MODE, r32(c, VA_DD_OBJECTS), w, h, bpp);
    if (dd_answer(c, ES2_SIM_DD_FAIL_MODE)) return 0;
    static const uint32_t order[3] = {VA_DD_BACK, VA_DD_PRIMARY, VA_DD_PALETTE};
    for (unsigned i = 0; i < 3u; ++i) {
        uint32_t o = r32(c, order[i]);
        if (!o) continue;
        if (dd_object_release(c, o)) return -1;
        w32(c, order[i], 0);
    }
    uint32_t flags = 0x20u, count = 2u, caps = 0x4200u;
    if ((int32_t)r32(c, VA_SCREEN_W) < 0x280) { caps |= 0x200018u; count = 1u; }
    else { flags = 0; count = 0; }
    NOTE(c, ES2_SIM_DD_SURFACE, r32(c, VA_DD_OBJECTS), 0x6cu, flags, count, caps, VA_DD_PRIMARY, 0u);
    uint32_t e = dd_answer(c, ES2_SIM_DD_FAIL_SURFACE);
    if (e) {
        caps &= ~0x4000u;
        NOTE(c, ES2_SIM_DD_SURFACE, r32(c, VA_DD_OBJECTS), 0x6cu, flags, count, caps, VA_DD_PRIMARY, 0u);
        e = dd_answer(c, ES2_SIM_DD_FAIL_SURFACE_RETRY);
    }
    if (e) return 0;
    w32(c, VA_DD_PRIMARY, pl->dd_object[1]);
    if ((int32_t)r32(c, VA_SCREEN_W) < 0x280) {
        NOTE(c, ES2_SIM_DD_ATTACHED, r32(c, VA_DD_PRIMARY), 4u, VA_DD_BACK);
        if (dd_answer(c, ES2_SIM_DD_FAIL_ATTACHED)) return 0;
        w32(c, VA_DD_BACK, pl->dd_object[2]);
    }
    return 1;
}
/* The switch. w = [0x4d253e], h = [0x4d2542] at the entry; GetSystemMetrics
 * 4, 0x20, 0x21 (dead).
 * To full screen ([0x4d255e] 0): [0x4d36fc] <- 1; [0x4d3700] set: cleared,
 * else GetWindowRect(hwnd, 0x4d36cc); [0x4d255e] <- 1; 0x46658c and
 * 0x4665c4(w, h, 8). An error: 0x4650d0 (the timer), PostQuitMessage(0) and
 * back, [0x4d255e] staying 1, EAX PostQuitMessage's (0 here).
 * Otherwise SetRect(rc, 0, 0, [0x4d253e], [0x4d2542]) (read again),
 * AdjustWindowRectEx(rc, 0xce0000, FALSE, 0), SetWindowPos(hwnd, HWND_TOPMOST,
 * rc.left, rc.top, width, height, 0x108) (the platform request: full
 * screen); CreatePalette(DDPCAPS_8BIT 4, 0x4d372c, 0x4d371c) and on DD_OK
 * SetPalette on the primary; ClipCursor({0, 0, w, h}); SetCursorPos(w / 2,
 * h / 2) (the row); with the cursor sprite [0x4d3724] ShowCursor(FALSE).
 * Back ([0x4d255e] nonzero): [0x4d36fc] <- 1; the release 0x466730;
 * [0x4d255e] <- 0; GetSystemMetrics 0 and 1 (dead); SetWindowPos(hwnd,
 * HWND_NOTOPMOST, x, y, right - x, bottom - y, 0x10) from 0x4d36cc (the
 * platform request: windowed); with the sprite ShowCursor(TRUE).
 * Both: 0x45f13c: H = [0x49b088]; with H and G = [H+0x1e9] G's slot +4 (G);
 * EAX its EAX, else H. EDX and ECX the entry's (no caller reads them). */
static int fullscreen_switch(pctx *c, uint32_t *eax) {
    const es2_sim_present_platform *pl = &c->p->platform;
    uint32_t w = r32(c, VA_SCREEN_W), h = r32(c, VA_SCREEN_H);
    NOTE(c, ES2_SIM_DD_METRICS, 4u);
    NOTE(c, ES2_SIM_DD_METRICS, 0x20u);
    NOTE(c, ES2_SIM_DD_METRICS, 0x21u);
    if (!r32(c, VA_DD_ON)) {
        w32(c, VA_OWN_CHANGE, 1);
        if (r32(c, VA_RECT_KEPT)) w32(c, VA_RECT_KEPT, 0);
        else {
            uint32_t a[2] = {r32(c, VA_HWND), VA_WINDOW_RECT};
            if (call(c, VA_GET_WINDOW_RECT, a, 2, NULL)) return 1;
        }
        w32(c, VA_DD_ON, 1);
        int ok = dd_create(c);
        if (ok) ok = dd_mode(c, w, h, 8u);
        if (ok < 0) return 1;
        if (!ok) {
            if (call(c, VA_TIMER_KILL, NULL, 0, NULL)) return 1;
            NOTE(c, ES2_SIM_DD_POST_QUIT, 0u);
            es2_sim_post_quit(c->l);
            *eax = 0;
            return 0;
        }
        uint32_t rc[4] = {0, 0, r32(c, VA_SCREEN_W), r32(c, VA_SCREEN_H)};
        NOTE(c, ES2_SIM_DD_SET_RECT, rc[0], rc[1], rc[2], rc[3]);
        NOTE(c, ES2_SIM_DD_ADJUST, rc[0], rc[1], rc[2], rc[3], 0xce0000u, 0u, 0u);
        for (unsigned i = 0; i < 4u; ++i) rc[i] += (uint32_t)pl->adjust[i];
        NOTE(c, ES2_SIM_DD_WINDOW_POS, r32(c, VA_HWND), 0xffffffffu, rc[0], rc[1], rc[2] - rc[0], rc[3] - rc[1], 0x108u);
        display_request(c, 1);
        NOTE(c, ES2_SIM_DD_PALETTE, r32(c, VA_DD_OBJECTS), 4u, VA_PAL_ENTRIES, VA_DD_PALETTE, 0u);
        if (!dd_answer(c, ES2_SIM_DD_FAIL_PALETTE)) {
            w32(c, VA_DD_PALETTE, pl->dd_object[3]);
            NOTE(c, ES2_SIM_DD_SET_PALETTE, r32(c, VA_DD_PRIMARY), r32(c, VA_DD_PALETTE));
        }
        NOTE(c, ES2_SIM_DD_CLIP, 0u, 0u, w, h);
        uint32_t a[2] = {half(w), half(h)};
        if (call(c, VA_SET_CURSOR_POS, a, 2, NULL)) return 1;
        if (r32(c, VA_CURSOR_SPRITE)) NOTE(c, ES2_SIM_DD_SHOW_CURSOR, 0u);
    } else {
        w32(c, VA_OWN_CHANGE, 1);
        if (call(c, VA_DD_RELEASE, NULL, 0, NULL)) return 1;
        w32(c, VA_DD_ON, 0);
        NOTE(c, ES2_SIM_DD_METRICS, 0u);
        NOTE(c, ES2_SIM_DD_METRICS, 1u);
        uint32_t x = r32(c, VA_WINDOW_RECT), y = r32(c, VA_WINDOW_RECT + 4u);
        NOTE(c, ES2_SIM_DD_WINDOW_POS, r32(c, VA_HWND), 0xfffffffeu, x, y, r32(c, VA_WINDOW_RECT + 8u) - x,
             r32(c, VA_WINDOW_RECT + 12u) - y, 0x10u);
        display_request(c, 0);
        if (r32(c, VA_CURSOR_SPRITE)) NOTE(c, ES2_SIM_DD_SHOW_CURSOR, 1u);
    }
    uint32_t hud = r32(c, VA_HUD);
    *eax = hud;
    if (hud) {
        uint32_t g = r32(c, hud + 0x1e9u);
        if (g) {
            uint32_t a[1] = {g};
            es2_sim_result r;
            if (call(c, r32(c, r32(c, g) + 4u), a, 1, &r)) return 1;
            *eax = r.eax;
        }
    }
    return 0;
}
void es2_sim_service_fullscreen_switch(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    uint32_t eax = 0;
    if (fullscreen_switch(&c, &eax)) { failed(sc); return; }
    sc->eax = eax;
}

/* The DirectDraw tail of the frame clear 0x46548c (0x4654a2..0x46554a): the
 * primary's IsLost (Restore on DDERR_SURFACELOST), the target as in the
 * present, its Lock (an error: EAX the error, nothing else), a memset of each
 * of the [0x4d2542] rows of [0x4d253e] bytes in the surface (not guest
 * memory), Unlock, Flip below 640; EAX the last call's DD_OK. */
int es2_sim_present_dd_clear(uint32_t *eax) {
    pctx c;
    if (context(bound_present, &c)) return 1;
    if (dd_null(&c, VA_DD_PRIMARY, VA_FRAME_CLEAR, "frame clear")) return 1;
    NOTE(&c, ES2_SIM_DD_IS_LOST, r32(&c, VA_DD_PRIMARY));
    if (c.p->platform.dd_fail & ES2_SIM_DD_FAIL_LOST) NOTE(&c, ES2_SIM_DD_RESTORE, r32(&c, VA_DD_PRIMARY));
    int below = (int32_t)r32(&c, VA_SCREEN_W) < 0x280;
    uint32_t target_va = below ? VA_DD_BACK : VA_DD_PRIMARY;
    if (dd_null(&c, target_va, VA_FRAME_CLEAR, "frame clear")) return 1;
    uint32_t target = r32(&c, target_va);
    NOTE(&c, ES2_SIM_DD_LOCK, target, 0u, 0x6cu, 1u, 0u);
    *eax = dd_answer(&c, ES2_SIM_DD_FAIL_LOCK);
    if (*eax) return 0;
    NOTE(&c, ES2_SIM_DD_UNLOCK, target, 0u);
    if (below) NOTE(&c, ES2_SIM_DD_FLIP, r32(&c, VA_DD_PRIMARY), 0u, 1u);
    *eax = 0;
    return 0;
}
/* The DirectDraw tail of the palette apply 0x465e28 (0x465f16..0x465f72):
 * the 256 entries of 0x4d372c <- the local table's bytes 2, 1, 0 and flags 1
 * (PC_RESERVED); CreatePalette(DDPCAPS_8BIT | DDPCAPS_ALLOW256 0x4c,
 * 0x4d372c, 0x4d371c) (the palette there is not released first) and on DD_OK
 * SetPalette on the primary. */
int es2_sim_present_dd_palette(const uint8_t local[1024]) {
    pctx c;
    if (context(bound_present, &c)) return 1;
    for (uint32_t i = 0; i < 0x100u; ++i) {
        w8(&c, VA_PAL_ENTRIES + 4u * i, local[4u * i + 2u]);
        w8(&c, VA_PAL_ENTRIES + 4u * i + 1u, local[4u * i + 1u]);
        w8(&c, VA_PAL_ENTRIES + 4u * i + 2u, local[4u * i]);
        w8(&c, VA_PAL_ENTRIES + 4u * i + 3u, 1);
    }
    if (dd_null(&c, VA_DD_OBJECTS, 0x00465e28u, "palette apply")) return 1;
    NOTE(&c, ES2_SIM_DD_PALETTE, r32(&c, VA_DD_OBJECTS), 0x4cu, VA_PAL_ENTRIES, VA_DD_PALETTE, 0u);
    if (dd_answer(&c, ES2_SIM_DD_FAIL_PALETTE)) return 0;
    w32(&c, VA_DD_PALETTE, c.p->platform.dd_object[3]);
    if (dd_null(&c, VA_DD_PRIMARY, 0x00465e28u, "palette apply")) return 1;
    NOTE(&c, ES2_SIM_DD_SET_PALETTE, r32(&c, VA_DD_PRIMARY), r32(&c, VA_DD_PALETTE));
    return 0;
}

/* ======================================================================
 * The frame sequence 0x45fba4 (listing 0x45fba4..0x45fca6).
 * Word [0x4d2566] nonzero (present): signed byte [0x4d2537] 1 ->
 * 0x481224([0x4d2568]); 0x42a9c0([0x4d24c0]); the cycler 0x4306ac; the
 * flush 0x43048c; the present 0x464a14 (site 0x45fbe4); 0x42a4c4([0x4d24c0]);
 * [0x4d2537] 1 -> 0x481224([0x4d24bc]).
 * Zero (page swap): word [0x4d267e] <- (word [0x4d24f2] == 0);
 * 0x42a9c0([0x4d24c0]); 0x4306ac; 0x43048c; with s = [0x4d24bc]: [s+0x34]
 * <- [s+0x8c] and, s being the current surface [0x6c5ed0], [0x4a3682] <- it
 * and the driver slot [0x4a57f4](it); [s+0x3c] <- [s+0x88] and, s current
 * (read again), [0x4a368a] <- it and the slot [0x4a57f0](it); the dwords
 * [s+0x88] and [s+0x8c] swapped; 0x42a4c4([0x4d24c0]).
 * The registers are the last callee's; none of the nine callers reads one
 * (0x45d1c9, 0x45d20d, 0x45d3f5, 0x45d43b, 0x45daf9, 0x45dd07, 0x45dd49,
 * 0x45dd9c, 0x45f4a0). The present is the own function (not the dispatch):
 * the row 0x464a14 (pointed at es2_sim_service_present_capture) serves the
 * same present() for the guest callers. */
static int frame(pctx *c) {
    uint32_t a[1];
    if (r16(c, VA_PRESENT_MODE)) {
        if ((int8_t)r8(c, VA_VIDEO_KIND) == 1 && es2_sim_gl_surface_select(c->l, r32(c, VA_PRESENT_SURFACE))) return 1;
        a[0] = r32(c, VA_DISPLAY);
        if (call(c, VA_DISPLAY_ENTER, a, 1, NULL) || call(c, VA_PALETTE_CYCLE, NULL, 0, NULL) ||
            call(c, VA_PALETTE_FLUSH, NULL, 0, NULL) || present(c, SITE_FRAME))
            return 1;
        a[0] = r32(c, VA_DISPLAY);
        if (call(c, VA_DISPLAY_LEAVE, a, 1, NULL)) return 1;
        if ((int8_t)r8(c, VA_VIDEO_KIND) == 1 && es2_sim_gl_surface_select(c->l, r32(c, VA_SURFACE))) return 1;
        return 0;
    }
    w16(c, VA_PAGE_FLAG, r16(c, VA_PAGES_FLAG) == 0 ? 1u : 0u);
    a[0] = r32(c, VA_DISPLAY);
    if (call(c, VA_DISPLAY_ENTER, a, 1, NULL) || call(c, VA_PALETTE_CYCLE, NULL, 0, NULL) || call(c, VA_PALETTE_FLUSH, NULL, 0, NULL))
        return 1;
    uint32_t s = r32(c, VA_SURFACE), v = r32(c, s + 0x8cu);
    w32(c, s + 0x34u, v);
    if (s == r32(c, VA_CURRENT_SURFACE)) {
        w32(c, VA_VIEW_DRAW, v);
        a[0] = v;
        if (call(c, r32(c, VA_SLOT_DRAW), a, 1, NULL)) return 1;
    }
    v = r32(c, s + 0x88u);
    w32(c, s + 0x3cu, v);
    if (s == r32(c, VA_CURRENT_SURFACE)) {
        w32(c, VA_VIEW_SHOW, v);
        a[0] = v;
        if (call(c, r32(c, VA_SLOT_SHOW), a, 1, NULL)) return 1;
    }
    uint32_t show = r32(c, s + 0x88u), draw = r32(c, s + 0x8cu);
    w32(c, s + 0x88u, draw);
    w32(c, s + 0x8cu, show);
    a[0] = r32(c, VA_DISPLAY);
    return call(c, VA_DISPLAY_LEAVE, a, 1, NULL);
}
void es2_sim_service_present_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    SERVE(sc, c);
    if (frame(&c)) { failed(sc); return; }
}
