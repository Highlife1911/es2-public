/* The fill layer's rest, original machine code against the service rows of
 * sim/src/fill.c (target :differential_fill_rest_test). Builds on
 * startup_cases.h's DATA snapshots (su_take, su_compare), loop_cases.h's loop
 * set-up (lc_reset) and audio_cases.h's register-capturing machine call
 * (au_call), like hud_frame_cases.h. Identifiers: fr_/FR_.
 *
 * Every case builds its objects (the palette, font and bitmap-array
 * objects, a stream, elements, the holder's inner object, polygons and
 * lines) in an identity-mapped world, then runs one call on both sides:
 * side 0 the original's code through au_call with random entry registers,
 * side 1 the row through es2_sim_call on a dispatch of the sim's real
 * services with the same entry registers.
 *
 * Recorders: the callees outside this package (0x486e10, 0x492447,
 * 0x48f3ce, 0x491156, 0x47a1ac, 0x47a55c, 0x49545b, 0x496406, 0x477548,
 * 0x47758c, 0x4838fd, 0x48d99e, 0x486704, 0x48817c, 0x4895ec, 0x483a04)
 * get a jump to a host trampoline over their entry on side 0 and a row at
 * the same VA on side 1; the stream's slots +0x14/+0x18, the elements'
 * slot +8, the inner object's slot +0x18 and the driver slot [0x4a57e0]
 * point at trampolines (rows at their host addresses on side 1). Each
 * trampoline returns a scripted EAX, EDX and ECX, so all three exit
 * registers are compared in every case. A stream pointer outside the world
 * (the argument slot 0x482f0a passes) is hashed by the bytes behind it,
 * never by its address. The frame record 0x49630c runs as the original
 * (0x4835c8's epilogue restores FS:0 from its frame). Compared: EAX, EDX,
 * ECX, the recorder hash and count, DATA (the span builder's saved frame
 * 0x4a33d4, a host value, copied across) and the world. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "audio_cases.h"
#include "es2/sim/fill.h"

#define FR_WORLD_SIZE 0x2000u
#define FR_POINTS 0x0000u   /* 12 points */
#define FR_RECT 0x0080u     /* the rect; the line's a at +0x10, b at +0x18 */
#define FR_OBJ 0x00c0u      /* the object under test, 0x40 */
#define FR_ENT 0x0100u      /* the palette's 8-byte entries */
#define FR_LIST 0x0140u     /* the bitmap array's element pointers */
#define FR_ELEM 0x0160u     /* 8 elements of 8 */
#define FR_VT 0x01a0u       /* the stream's and elements' vtable */
#define FR_VT2 0x01c0u      /* the inner object's vtable */
#define FR_S 0x01e0u        /* the stream */
#define FR_IN 0x01e8u       /* the inner object */
#define FR_DST 0x01f0u      /* stream data */
#define FR_OUT 0x0200u      /* span lists, 0x1000 */
static BYTE *fr_world, *fr_world_start;
static es2_sim_entry fr_entries[4096];
static es2_sim_dispatch fr_dispatch;
static int fr_bad;
static void fr_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own fill rest: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    fr_bad = 1;
}

/* ---- the recorders: kind 1..21 */
#define FR_KINDS 22u
static const unsigned fr_nargs[FR_KINDS] = {0, 4, 2, 3, 3, 3, 1, 1, 2, 1, 1, 2, 1, 1, 1, 1, 2, 3, 3, 1, 2, 3};
static uint64_t fr_hash[2];
static unsigned fr_count[2], fr_side_now, fr_calls[2][FR_KINDS], fr_kinds[FR_KINDS];
static char fr_text[2][512];
static uint32_t fr_eax[FR_KINDS][4], fr_edx[FR_KINDS][4], fr_ecx[FR_KINDS][4], fr_read[4];
uint32_t fr_ecx_out;
uint64_t fr_record(unsigned kind, const uint32_t *a);
static int fr_in_world(uint32_t p, uint32_t n) {
    uint32_t w = (uint32_t)(uintptr_t)fr_world;
    return p >= w && p - w <= FR_WORLD_SIZE - n;
}
static void fr_mix(uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) { fr_hash[fr_side_now] ^= (BYTE)(v >> (8u * i)); fr_hash[fr_side_now] *= 0x100000001b3ull; }
}
uint64_t fr_record(unsigned kind, const uint32_t *a) {
    unsigned side = fr_side_now, k = kind < FR_KINDS ? fr_calls[side][kind]++ : 0;
    if (kind >= FR_KINDS) { fr_ecx_out = 0; return 0; }
    fr_mix(0xabc00000u + kind);
    for (unsigned i = 0; i < fr_nargs[kind]; ++i) {
        if ((kind == 17u || kind == 18u) && i == 2u && !fr_in_world(a[2], 1)) {
            uint32_t n = a[1] > 8u ? 8u : a[1];   /* a stack slot: its bytes */
            for (uint32_t j = 0; j < n; ++j) fr_mix(*(const BYTE *)(uintptr_t)(a[2] + j));
            continue;
        }
        fr_mix(a[i]);
    }
    if (kind == 18u && a[1] == 4u && fr_in_world(a[2], 4)) put32((BYTE *)(uintptr_t)a[2], fr_read[k & 3u]);
    size_t used = strlen(fr_text[side]);
    if (fr_count[side] < 14u && used + 40u < sizeof(fr_text[side]))
        snprintf(fr_text[side] + used, sizeof(fr_text[side]) - used, " %u(%lx,%lx)", kind, (unsigned long)a[0],
                 (unsigned long)(fr_nargs[kind] > 1u ? a[1] : 0));
    ++fr_count[side];
    if (side) ++fr_kinds[kind];
    fr_ecx_out = fr_ecx[kind][k & 3u];
    return (uint64_t)fr_edx[kind][k & 3u] << 32 | fr_eax[kind][k & 3u];
}
/* trampoline N: EAX = N, then fr_record(N, the caller's arguments); ECX
 * the scripted value. */
#define FR_T(n) "\n.globl _fr_t" #n "\n_fr_t" #n ":\n\tmovl $" #n ", %eax\n\tjmp _fr_tcommon\n"
__asm__(".text\n" FR_T(1) FR_T(2) FR_T(3) FR_T(4) FR_T(5) FR_T(6) FR_T(7) FR_T(8) FR_T(9) FR_T(10) FR_T(11) FR_T(12) FR_T(13) FR_T(14)
            FR_T(15) FR_T(16) FR_T(17) FR_T(18) FR_T(19) FR_T(20) FR_T(21)
        "_fr_tcommon:\n\tleal 4(%esp), %edx\n\tpushl %edx\n\tpushl %eax\n\tcall _fr_record\n\taddl $8, %esp\n"
        "\tmovl _fr_ecx_out, %ecx\n\tret\n");
#undef FR_T
extern void fr_t1(void), fr_t2(void), fr_t3(void), fr_t4(void), fr_t5(void), fr_t6(void), fr_t7(void), fr_t8(void), fr_t9(void),
    fr_t10(void), fr_t11(void), fr_t12(void), fr_t13(void), fr_t14(void), fr_t15(void), fr_t16(void), fr_t17(void), fr_t18(void),
    fr_t19(void), fr_t20(void), fr_t21(void);
static void (*const fr_tramp[FR_KINDS])(void) = {NULL, fr_t1, fr_t2, fr_t3, fr_t4, fr_t5, fr_t6, fr_t7, fr_t8, fr_t9, fr_t10,
                                                  fr_t11, fr_t12, fr_t13, fr_t14, fr_t15, fr_t16, fr_t17, fr_t18, fr_t19, fr_t20, fr_t21};
static uint32_t fr_t(unsigned kind) { return (uint32_t)(uintptr_t)fr_tramp[kind]; }
static void fr_s_stub(es2_sim_service_call *sc, void *user) {
    uint64_t v = fr_record((unsigned)(uintptr_t)user, sc->slots);
    sc->eax = (uint32_t)v;
    sc->edx = (uint32_t)(v >> 32);
    sc->ecx = fr_ecx_out;
}
static const uint32_t fr_patched[17] = {0, 0x00486e10u, 0x00492447u, 0x0048f3ceu, 0x00491156u, 0x0047a1acu, 0x0047a55cu, 0x0049545bu,
                                        0x00496406u, 0x00477548u, 0x0047758cu, 0x004838fdu, 0x0048d99eu, 0x00486704u, 0x0048817cu,
                                        0x004895ecu, 0x00483a04u};
static BYTE fr_saved[17][5];
static int fr_patch(uint32_t va, uint32_t target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("fill rest: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("fill rest: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static int fr_put(uint32_t va, es2_sim_service_fn fn, void *user, unsigned slots) {
    for (unsigned i = 0; i < fr_dispatch.count; ++i) {
        es2_sim_entry *e = &fr_dispatch.entries[i];
        if (e->va != va) continue;
        e->kind = ES2_SIM_SERVICE; e->name = "fill rest recorder"; e->service = fn; e->service_user = user; e->pop_bytes = 0;
        e->stack_slots = (uint8_t)slots;
        return 0;
    }
    return es2_sim_dispatch_add_service(&fr_dispatch, va, "fill rest recorder", fn, user, 0, (uint8_t)slots);
}
static int fr_dispatch_setup(void) {
    es2_sim_dispatch_init(&fr_dispatch, fr_entries, 4096, fr_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&fr_dispatch, &s)) return fail("fill rest: dispatch");
    for (unsigned k = 1; k < FR_KINDS; ++k)
        if (fr_put(k < 17u ? fr_patched[k] : fr_t(k), fr_s_stub, (void *)(uintptr_t)k, fr_nargs[k])) return fail("fill rest: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&fr_dispatch, &error, &bad)) return fail("fill rest: dispatch seal");
    return 0;
}

/* ---- the case script */
static uint32_t fr_rand(uint32_t n) { return random32() % n; }
static int32_t fr_ri(int32_t lo, int32_t hi) { return lo + (int32_t)fr_rand((uint32_t)(hi - lo + 1)); }
static uint32_t fr_at(uint32_t offset) { return (uint32_t)(uintptr_t)fr_world + offset; }
static void fr_w32(uint32_t offset, uint32_t v) { put32(fr_world_start + offset, v); }
static void fr_w16(uint32_t offset, uint32_t v) { put16(fr_world_start + offset, (uint16_t)v); }
static uint32_t fr_g32(uint32_t offset) { return get32(fr_world_start + offset); }
static void fr_point(uint32_t offset, int32_t x, int32_t y) { fr_w32(offset, (uint32_t)x); fr_w32(offset + 4u, (uint32_t)y); }
static uint32_t fr_driver;   /* [0x4a57e0] for the case */
typedef struct fr_op { uint32_t va, a[5], in[3]; unsigned n; } fr_op;
static fr_op fr_op_now;
static uint32_t fr_res[2][3];
static void fr_common(void) {
    random_bytes(fr_world_start, FR_WORLD_SIZE);
    for (unsigned k = 0; k < FR_KINDS; ++k)
        for (unsigned i = 0; i < 4u; ++i) {
            fr_eax[k][i] = random32();
            fr_edx[k][i] = random32();
            fr_ecx[k][i] = random32();
        }
    for (unsigned i = 0; i < 4u; ++i) fr_read[i] = random32();
    for (unsigned i = 0; i < 4u; ++i) fr_eax[6][i] = fr_rand(5u) ? 1u + fr_rand(0x100u) : 0u;   /* 0x47a55c: 0 ends the sum */
    fr_w32(FR_VT + 8u, fr_t(19));
    fr_w32(FR_VT + 0x14u, fr_t(17));
    fr_w32(FR_VT + 0x18u, fr_t(18));
    fr_w32(FR_VT2 + 0x18u, fr_t(20));
    fr_w32(FR_S, fr_at(FR_VT));
    fr_w32(FR_IN, fr_at(FR_VT2));
    fr_driver = fr_t(21);
}
static void fr_side(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &fr_dispatch;
    memcpy(fr_world, fr_world_start, FR_WORLD_SIZE);
    put32(mapped + (0x004a57e0u - 0x400000u), fr_driver);
    fr_side_now = side;
    fr_hash[side] = 0xcbf29ce484222325ull;
    fr_count[side] = 0;
    fr_text[side][0] = 0;
    memset(fr_calls[side], 0, sizeof(fr_calls[side]));
}
static int fr_run(const char *what, unsigned k) {
    fr_op *o = &fr_op_now;
    for (unsigned side = 0; side < 2u; ++side) {
        fr_side(side);
        uint32_t out[3] = {0, 0, 0};
        static char where[200];
        snprintf(where, sizeof(where), "fill rest (%s side %u: 0x%08lx(0x%lx, 0x%lx))", what, side, (unsigned long)o->va,
                 (unsigned long)o->a[0], (unsigned long)o->a[1]);
        current_function = where;
        if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, o->in, out);
        else {
            es2_sim_result r;
            memset(&r, 0, sizeof(r));
            if (es2_sim_call(&fr_dispatch, o->va, o->a, o->n, o->in, &r)) fr_bad = 1;
            out[0] = r.eax;
            out[1] = r.edx;
            out[2] = r.ecx;
        }
        memcpy(fr_res[side], out, sizeof(out));
        if (fr_bad) { fprintf(stderr, "FAIL: fill rest %s case %u (0x%08lx): a call failed\n", what, k, (unsigned long)o->va); return 1; }
        if (!side) memcpy(fr_world_start + FR_WORLD_SIZE, fr_world, FR_WORLD_SIZE);
        su_take(side);
    }
    if (fr_hash[0] != fr_hash[1] || fr_count[0] != fr_count[1]) {
        fprintf(stderr, "FAIL: fill rest %s case %u: recorded calls differ (%u / %u)\n  original:%s\n  own:     %s\n", what, k, fr_count[0],
                fr_count[1], fr_text[0], fr_text[1]);
        return 1;
    }
    su_snap_same(ES2_SIM_VA_FILL_SAVED_FRAME, 4u);
    if (su_compare(what, k)) return 1;
    if (memcmp(fr_world, fr_world_start + FR_WORLD_SIZE, FR_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < FR_WORLD_SIZE && shown < 16u; ++i)
            if (fr_world[i] != fr_world_start[FR_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, fr_world_start[FR_WORLD_SIZE + i], fr_world[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: fill rest %s case %u: the world differs (world at 0x%08lx)\n", what, k, (unsigned long)(uintptr_t)fr_world);
        return 1;
    }
    for (unsigned i = 0; i < 3u; ++i)
        if (fr_res[0][i] != fr_res[1][i]) {
            static const char *const names[] = {"EAX", "EDX", "ECX"};
            fprintf(stderr, "FAIL: fill rest %s case %u (0x%08lx): %s original 0x%08lx own 0x%08lx\n", what, k, (unsigned long)o->va, names[i],
                    (unsigned long)fr_res[0][i], (unsigned long)fr_res[1][i]);
            return 1;
        }
    return 0;
}
static void fr_op_set(uint32_t va, unsigned n, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    fr_op_now = (fr_op){va, {a0, a1, a2, a3, a4}, {random32(), random32(), random32()}, n};
}

/* ---- the clipper 0x484338, the span builder 0x484720, the line's clip
 * 0x483c6c and span list 0x483d6c */
static unsigned fr_cov_clip[4], fr_cov_edge[6], fr_cov_last[7], fr_cov_flat, fr_cov_lclip[2], fr_cov_lspan[5];
static void fr_rect(int32_t l, int32_t t, int32_t r, int32_t b) {
    fr_w32(FR_RECT, (uint32_t)l); fr_w32(FR_RECT + 4u, (uint32_t)t); fr_w32(FR_RECT + 8u, (uint32_t)r); fr_w32(FR_RECT + 12u, (uint32_t)b);
}
static int fr_clip_case(unsigned k) {
    fr_common();
    uint32_t n = fr_rand(10u) ? 2u + fr_rand(9u) : fr_rand(3u);
    if (fr_rand(50u) == 0) n = 0xffffffffu;
    int32_t l = fr_ri(-10, 50), t = fr_ri(-10, 40), r = fr_ri(l, 75), b = fr_ri(t, 60);
    fr_rect(l, t, r, b);
    int inside = fr_rand(6u) == 0;
    for (uint32_t i = 0; i < 12u; ++i) {
        int32_t x = inside ? fr_ri(l, r) : fr_ri(l - 30, r + 30), y = inside ? fr_ri(t, b) : fr_ri(t - 30, b + 30);
        if (fr_rand(6u) == 0) x = fr_rand(2u) ? l : r;
        if (fr_rand(6u) == 0) y = fr_rand(2u) ? t : b;
        fr_point(FR_POINTS + 8u * i, x, y);
    }
    fr_op_set(0x00484338u, 4, n, fr_at(FR_POINTS), fr_at(FR_OUT), fr_at(FR_RECT), 0);
    int bad = fr_run("clip", k);
    if (!bad) ++fr_cov_clip[(int32_t)n <= 1 ? 0 : fr_res[0][0] == 0 ? 1 : fr_res[0][0] < n ? 2 : 3];
    return bad;
}
static void fr_walk(uint32_t n, int32_t lo_x, int32_t hi_x, int32_t lo_y, int32_t hi_y) {
    int32_t x = fr_ri(lo_x, hi_x), y = fr_ri(lo_y, hi_y);
    for (uint32_t i = 0; i < n; ++i) {
        fr_point(FR_POINTS + 8u * i, x, y);
        int32_t dx = 0, dy = 0, s = fr_rand(2u) ? 1 : -1, s2 = fr_rand(2u) ? 1 : -1;
        switch (fr_rand(6u)) {
        case 0: dy = s * fr_ri(1, 15); break;                                   /* vertical */
        case 1: dx = s * fr_ri(1, 20); break;                                   /* horizontal */
        case 2: dx = s * fr_ri(1, 12); dy = s2 * (dx < 0 ? -dx : dx); break;    /* diagonal */
        case 3: dx = fr_ri(-20, 20); dy = fr_ri(-15, 15); break;
        case 4: break;                                                          /* a duplicate */
        default: dx = s * fr_ri(5, 30); dy = s2 * fr_ri(1, 4); break;           /* x-major */
        }
        x += dx; y += dy;
        if (x < lo_x) x = lo_x; else if (x > hi_x) x = hi_x;
        if (y < lo_y) y = lo_y; else if (y > hi_y) y = hi_y;
    }
}
static int fr_spans_case(unsigned k) {
    fr_common();
    uint32_t n = 2u + fr_rand(12u);
    fr_walk(n, -20, 90, -20, 70);
    if (fr_rand(15u) == 0) {   /* a flat polygon */
        uint32_t y = fr_g32(FR_POINTS + 4u);
        for (uint32_t i = 0; i < n; ++i) fr_w32(FR_POINTS + 8u * i + 4u, y);
        ++fr_cov_flat;
    }
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t j = i + 1u == n ? 0 : i + 1u;
        int32_t dx = (int32_t)(fr_g32(FR_POINTS + 8u * j) - fr_g32(FR_POINTS + 8u * i));
        int32_t dy = (int32_t)(fr_g32(FR_POINTS + 8u * j + 4u) - fr_g32(FR_POINTS + 8u * i + 4u));
        dx = dx < 0 ? -dx : dx; dy = dy < 0 ? -dy : dy;
        ++fr_cov_edge[dx == 0 ? 0 : dy == 0 ? 1 : dx == dy ? 2 : dx < dy ? 3 : 4];
    }
    fr_op_set(0x00484720u, 3, n, fr_at(FR_POINTS), fr_at(FR_OUT), 0, 0);
    int bad = fr_run("spans", k);
    if (!bad) {
        /* the left chain's last edge (EDX and ECX come from it): from the
         * bottom (largest y, then smallest x, from n-1 down) forward to a
         * point of the smallest y */
        int32_t ymin = (int32_t)fr_g32(FR_POINTS + 4u), by = ymin, bx = (int32_t)fr_g32(FR_POINTS);
        uint32_t bottom = 0;
        for (uint32_t i = n - 1u; i != 0; --i) {
            int32_t x = (int32_t)fr_g32(FR_POINTS + 8u * i), y = (int32_t)fr_g32(FR_POINTS + 8u * i + 4u);
            if (y < ymin) ymin = y;
            if (y > by || (y == by && x <= bx)) { bottom = i; bx = x; by = y; }
        }
        if (by == ymin) { ++fr_cov_last[0]; return 0; }
        uint32_t i = bottom, j = bottom;
        while ((int32_t)fr_g32(FR_POINTS + 8u * j + 4u) > ymin) { i = j; if (++j >= n) j = 0; }
        int32_t dx = (int32_t)(fr_g32(FR_POINTS + 8u * j) - fr_g32(FR_POINTS + 8u * i));
        int32_t dy = (int32_t)(fr_g32(FR_POINTS + 8u * j + 4u) - fr_g32(FR_POINTS + 8u * i + 4u));
        int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        ++fr_cov_last[ax == 0 ? 1 : ay == 0 ? 2 : ax == ay ? 3 : ax < ay ? 4 : (dx > 0) == (dy > 0) ? 5 : 6];
    }
    return bad;
}
static void fr_line_points(int32_t x0, int32_t x1, int32_t y0, int32_t y1) {
    int32_t ax = fr_ri(x0, x1), ay = fr_ri(y0, y1), bx = fr_ri(x0, x1), by = fr_ri(y0, y1);
    switch (fr_rand(8u)) {
    case 0: bx = ax; break;
    case 1: by = ay; break;
    case 2: bx = ax; by = ay; break;
    case 3: { int32_t d = fr_ri(-30, 30); bx = ax + d; by = ay + (fr_rand(2u) ? d : -d); break; }
    default: break;
    }
    fr_point(FR_RECT + 0x10u, ax, ay);
    fr_point(FR_RECT + 0x18u, bx, by);
}
static int fr_line_clip_case(unsigned k) {
    fr_common();
    int32_t l = fr_ri(-5, 50), t = fr_ri(-5, 40), r = fr_ri(l, 70), b = fr_ri(t, 55);
    fr_rect(l, t, r, b);
    fr_line_points(l - 40, r + 40, t - 40, b + 40);
    for (unsigned e = 0; e < 2u; ++e) {
        if (fr_rand(5u) == 0) fr_w32(FR_RECT + 0x10u + 8u * e, (uint32_t)(fr_rand(2u) ? l : r));
        if (fr_rand(5u) == 0) fr_w32(FR_RECT + 0x14u + 8u * e, (uint32_t)(fr_rand(2u) ? t : b));
    }
    fr_op_set(0x00483c6cu, 3, fr_at(FR_RECT + 0x10u), fr_at(FR_RECT + 0x18u), fr_at(FR_RECT), 0, 0);
    int bad = fr_run("line clip", k);
    if (!bad) ++fr_cov_lclip[fr_res[0][0] != 0];
    return bad;
}
static int fr_line_spans_case(unsigned k) {
    fr_common();
    fr_line_points(-50, 120, -50, 100);
    int32_t dx = (int32_t)(fr_g32(FR_RECT + 0x18u) - fr_g32(FR_RECT + 0x10u)), dy = (int32_t)(fr_g32(FR_RECT + 0x1cu) - fr_g32(FR_RECT + 0x14u));
    dx = dx < 0 ? -dx : dx; dy = dy < 0 ? -dy : dy;
    ++fr_cov_lspan[dx == 0 ? 0 : dy == 0 ? 1 : dx == dy ? 2 : dx > dy ? 3 : 4];
    fr_op_set(0x00483d6cu, 3, fr_at(FR_RECT + 0x10u), fr_at(FR_RECT + 0x18u), fr_at(FR_OUT), 0, 0);
    return fr_run("line spans", k);
}

/* ---- the palette 0x4820b7, 0x482154, 0x482190, 0x4822cc, 0x4823da */
static unsigned fr_cov_op[34];
static uint32_t fr_count16(void) {   /* a signed word count: mostly 0..6, sometimes negative */
    return fr_rand(6u) ? fr_rand(7u) : 0x8000u + fr_rand(0x8000u);
}
static void fr_palette(void) {
    fr_w16(FR_OBJ + 4u, fr_rand(4u) ? fr_rand(20u) : random32());
    fr_w32(FR_OBJ + 8u, fr_at(FR_DST));
    fr_w16(FR_OBJ + 0xcu, fr_count16());
    fr_w32(FR_OBJ + 0x10u, fr_at(FR_ENT));
    for (unsigned i = 0; i < 8u; ++i) {
        fr_w32(FR_ENT + 8u * i, fr_at(FR_DST));
        fr_w16(FR_ENT + 8u * i + 4u, fr_rand(4u) ? fr_rand(30u) : random32());
    }
}
static int fr_palette_case(unsigned k) {
    fr_common();
    fr_palette();
    unsigned which = fr_rand(5u);
    static const uint32_t vas[5] = {0x004820b7u, 0x00482154u, 0x00482190u, 0x004822ccu, 0x004823dau};
    switch (which) {
    case 0: fr_op_set(vas[0], 5, random32(), random32(), random32(), random32(), random32()); break;
    case 3: fr_op_set(vas[3], 2, fr_at(FR_OBJ), fr_at(FR_S), 0, 0, 0); break;
    default: fr_op_set(vas[which], 1, fr_at(FR_OBJ), 0, 0, 0, 0); break;
    }
    int bad = fr_run("palette", k);
    if (!bad) ++fr_cov_op[which];
    return bad;
}

/* ---- the font 0x482410, 0x482528, 0x4827a1, 0x48287c, 0x4828b8, 0x4828f4 */
static unsigned fr_cov_font_arrays[5], fr_cov_font_null, fr_cov_font_delete;
static void fr_font(void) {
    for (uint32_t o = 4u; o < 0x1au; o += 2u) fr_w16(FR_OBJ + o, fr_rand(3u) ? fr_rand(4u) ? fr_rand(16u) : 0xffffu : random32());
    fr_w32(FR_OBJ + 0x1au, fr_rand(3u) ? 0u : fr_rand(4u) ? fr_rand(64u) : random32());
    for (uint32_t o = 0x1eu; o <= 0x2eu; o += 4u) fr_w32(FR_OBJ + o, fr_at(FR_DST));
}
static int fr_font_case(unsigned k) {
    fr_common();
    fr_font();
    unsigned which = fr_rand(6u);
    static const uint32_t vas[6] = {0x00482410u, 0x00482528u, 0x004827a1u, 0x0048287cu, 0x004828b8u, 0x004828f4u};
    switch (which) {
    case 0: {
        uint32_t f = fr_rand(8u) ? fr_at(FR_OBJ) : 0u;
        fr_op_set(vas[0], 2, f, random32(), 0, 0, 0);
        break;
    }
    case 2: fr_op_set(vas[2], 4, random32(), random32(), random32(), random32(), 0); break;
    case 5: fr_op_set(vas[5], 2, fr_at(FR_OBJ), fr_at(FR_S), 0, 0, 0); break;
    default: fr_op_set(vas[which], 1, fr_at(FR_OBJ), 0, 0, 0, 0); break;
    }
    int bad = fr_run("font", k);
    if (!bad) {
        ++fr_cov_op[5u + which];
        if (which == 0) {
            if (!fr_op_now.a[0]) ++fr_cov_font_null;
            else {
                const BYTE *f = fr_world_start + FR_OBJ;
                fr_cov_font_arrays[0] += get16(f + 0x18u) != 0;
                fr_cov_font_arrays[1] += get32(f + 0x1au) != 0;
                fr_cov_font_arrays[2] += get16(f + 4u) != 0;
                fr_cov_font_arrays[3] += get16(f + 0xcu) == 0xffffu;
                fr_cov_font_arrays[4] += get16(f + 8u) == 0xffffu;
                fr_cov_font_delete += fr_op_now.a[1] & 1u;
            }
        }
    }
    return bad;
}

/* ---- the stream thunks 0x482bd7, 0x482bf0, 0x482c6e, 0x482c8a, 0x482f0a,
 * 0x482f21 and the draw thunks 0x482cec, 0x482d44, 0x482d59, 0x482d68,
 * 0x482d77 */
static int fr_thunk_case(unsigned k) {
    fr_common();
    unsigned which = fr_rand(11u);
    static const uint32_t vas[11] = {0x00482bd7u, 0x00482bf0u, 0x00482c6eu, 0x00482c8au, 0x00482f0au, 0x00482f21u,
                                     0x00482cecu, 0x00482d44u, 0x00482d59u, 0x00482d68u, 0x00482d77u};
    uint32_t len = fr_rand(3u) ? fr_rand(16u) : random32();
    switch (which) {
    case 0: case 1: case 2: case 3: fr_op_set(vas[which], 3, fr_at(FR_S), len, fr_at(FR_DST), 0, 0); break;
    case 4: fr_op_set(vas[4], 2, fr_at(FR_S), random32(), 0, 0, 0); break;
    case 5: fr_op_set(vas[5], 2, fr_at(FR_S), fr_at(FR_DST), 0, 0, 0); break;
    case 6: fr_op_set(vas[6], 1, fr_at(FR_OBJ), 0, 0, 0, 0); break;
    case 7: fr_op_set(vas[7], 3, random32(), fr_at(FR_POINTS), fr_at(FR_POINTS + 8u), 0, 0); break;
    default: fr_op_set(vas[which], 2, random32(), fr_at(FR_RECT), 0, 0, 0); break;
    }
    int bad = fr_run("thunk", k);
    if (!bad) ++fr_cov_op[11u + which];
    return bad;
}

/* ---- the bitmap array 0x482e08, 0x482e48, 0x482ed0, 0x482f38, 0x482f80
 * and the holder 0x48358c, 0x4835c8 */
static unsigned fr_cov_zero_sum, fr_cov_holder[4];
static int fr_array_case(unsigned k) {
    fr_common();
    int32_t count = fr_rand(8u) ? (int32_t)fr_rand(9u) : -(int32_t)fr_rand(0x10000u) - 1;
    fr_w32(FR_OBJ + 4u, (uint32_t)count);
    fr_w32(FR_OBJ + 8u, fr_at(FR_LIST));
    for (unsigned i = 0; i < 8u; ++i) {
        fr_w32(FR_LIST + 4u * i, fr_at(FR_ELEM + 8u * i));
        fr_w32(FR_ELEM + 8u * i, fr_at(FR_VT));
    }
    unsigned which = fr_rand(7u);
    static const uint32_t vas[7] = {0x00482e08u, 0x00482e48u, 0x00482ed0u, 0x00482f38u, 0x00482f80u, 0x0048358cu, 0x004835c8u};
    uint32_t inner = fr_rand(4u) ? fr_at(FR_IN) : 0u;
    switch (which) {
    case 2: fr_op_set(vas[2], 2, fr_at(FR_OBJ), fr_at(FR_S), 0, 0, 0); break;
    case 4: fr_op_set(vas[4], 3, fr_at(FR_OBJ), random32(), random32(), 0, 0); break;
    case 6:
        fr_w32(FR_OBJ + 4u, inner);
        fr_op_set(vas[6], 2, fr_rand(6u) ? fr_at(FR_OBJ) : 0u, random32(), 0, 0, 0);
        break;
    default: fr_op_set(vas[which], 1, fr_at(FR_OBJ), 0, 0, 0, 0); break;
    }
    int bad = fr_run("array", k);
    if (!bad) {
        ++fr_cov_op[22u + which];
        if (which == 0 && fr_res[0][0] == 0) ++fr_cov_zero_sum;
        if (which == 6) ++fr_cov_holder[!fr_op_now.a[0] ? 0 : !inner ? 1 : (fr_op_now.a[1] & 1u) ? 3 : 2];
    }
    return bad;
}

static int fill_rest_cases(void) {
    current_function = "fill rest";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    fr_world = VirtualAlloc(NULL, FR_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    fr_world_start = malloc(2u * FR_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !fr_world || !fr_world_start)
        return fail("fill rest: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("fill rest: the counter word is not free");
    su_pristine_take();
    if (fr_dispatch_setup()) return 1;
    for (unsigned i = 1; i < 17u; ++i) if (fr_patch(fr_patched[i], fr_t(i), fr_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"clip", fr_clip_case, 2000}, {"spans", fr_spans_case, 2000}, {"line clip", fr_line_clip_case, 1500},
        {"line spans", fr_line_spans_case, 1500}, {"palette", fr_palette_case, 1500}, {"font", fr_font_case, 2400},
        {"thunks", fr_thunk_case, 1500}, {"arrays and holder", fr_array_case, 2100},
    };
    /* Each part is one test shard's (suite_part_in_shard) and starts from its
     * own seed, so its cases do not depend on the shard layout. The coverage
     * line counts the shard's own parts. */
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        if (!suite_part_in_shard(p)) continue;
        random_state = 0x48c06e21u ^ (p * 0x9e3779b9u);
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: fill rest %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    for (unsigned i = 1; i < 17u; ++i) if (fr_patch(fr_patched[i], fr_t(i), fr_saved[i], 0)) bad = 1;
    if (!bad) {
        printf("FILL REST coverage: clip n<=1/empty/fewer/more-or-equal %u/%u/%u/%u; edges vertical/horizontal/diagonal/y-major/x-major "
               "%u/%u/%u/%u/%u, flat %u; last left edge one row/vertical/horizontal/diagonal/y-major/x-major down/up %u/%u/%u/%u/%u/%u/%u; line clip out/in %u/%u; line spans vertical/horizontal/diagonal/x-major/y-major %u/%u/%u/%u/%u; "
               "font destroy null %u, arrays %u/%u/%u/%u/%u, delete %u; bitmaps zero sum %u; holder null/no inner/inner/inner+delete "
               "%u/%u/%u/%u\n  per row:",
               fr_cov_clip[0], fr_cov_clip[1], fr_cov_clip[2], fr_cov_clip[3], fr_cov_edge[0], fr_cov_edge[1], fr_cov_edge[2], fr_cov_edge[3],
               fr_cov_edge[4], fr_cov_flat, fr_cov_last[0], fr_cov_last[1], fr_cov_last[2], fr_cov_last[3], fr_cov_last[4], fr_cov_last[5],
               fr_cov_last[6], fr_cov_lclip[0], fr_cov_lclip[1], fr_cov_lspan[0], fr_cov_lspan[1], fr_cov_lspan[2],
               fr_cov_lspan[3], fr_cov_lspan[4], fr_cov_font_null, fr_cov_font_arrays[0], fr_cov_font_arrays[1], fr_cov_font_arrays[2],
               fr_cov_font_arrays[3], fr_cov_font_arrays[4], fr_cov_font_delete, fr_cov_zero_sum, fr_cov_holder[0], fr_cov_holder[1],
               fr_cov_holder[2], fr_cov_holder[3]);
        for (unsigned i = 0; i < 29u; ++i) printf(" %u", fr_cov_op[i]);
        printf("; calls:");
        for (unsigned i = 1; i < FR_KINDS; ++i) printf(" %u", fr_kinds[i]);
        printf("\n");
    }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
