/* The blit and clear rest (the seven rows of sim/dispatch/services/gfx_blit.h),
 * original machine code against sim/src/gfx.c called through its rows
 * (target :differential_gfx_blit_test). Builds on gfx_cases.h: its world (the
 * 64x48 surface, the graphics context with up to three regions, the BMP_8
 * driver table), its side reset (gx_side, gx_end) and its comparison of DATA
 * and the world (gx_compare).
 *
 * The own side's dispatch is gx_dispatch_setup's (the service rows, poly_fills.h's
 * line and polygon fill, sincos_pair 0x47eb30 and rotate_2d 0x47ffe8 as the
 * original) plus two host stubs for the driver's cursor hide [0x4a5828]
 * and show [0x4a5824]. The stubs are the same code on both sides: they log
 * their letter and the cursor point 0x4a3620 at the time of the call (the
 * order against the write) and leave fixed EAX/EDX/ECX.
 *
 * - ramps: 0x486e10(first, count 0..8, src, d) with random entry
 *   registers: DATA (the ramp table 0x4a4fa5), EAX, EDX, ECX.
 * - cursor: 0x486e7c(point) with [0x4a3646] and [0x4a361c] each 0 or
 *   not: DATA (0x4a3620, 0x4a364a), the stub log, EAX, EDX, ECX.
 * - scroll: 0x487bdc(record, point), kind-0 rects (the record's kind
 *   random: it is not read), modes 0, 1 and 2 (regions of kinds 0, 2 and
 *   5, or none), the origin flag 0 or not: DATA and the world.
 * - area: 0x487e94(rect) over small and full-range dwords: EAX, EDX, ECX.
 * - save: 0x487eac(record, pixels), as scroll: DATA and the world.
 * - matrix: 0x488dc7(m, angle) with full 32-bit angles: the world, EAX,
 *   EDX (ECX is the callee's; the pass-through of 0x47eb30 hands back
 *   EAX and EDX only).
 * - rotate: 0x488ddd(v, m, out) over random vectors and matrices: the
 *   world, EAX, EDX (ECX as for matrix). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "input_layer_cases.h"
#include "gfx_cases.h"

/* A machine call with entry registers and EAX/EDX/ECX after it. */
extern void rd11_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _rd11_call\n_rd11_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

/* ---- the cursor stubs: a log per side */
static unsigned rd11_side;
static char rd11_log[2][256];
static size_t rd11_log_len[2];
static void rd11_note(char letter) {
    BYTE *d = mapped - 0x400000u;
    if (rd11_log_len[rd11_side] + 24u < sizeof(rd11_log[0]))
        rd11_log_len[rd11_side] += (size_t)sprintf(rd11_log[rd11_side] + rd11_log_len[rd11_side], "%c(%lx,%lx) ", letter,
                                                   (unsigned long)get32(d + 0x4a3620u), (unsigned long)get32(d + 0x4a3624u));
}
void __cdecl rd11_note_hide(void);
void __cdecl rd11_note_hide(void) { rd11_note('h'); }
void __cdecl rd11_note_show(void);
void __cdecl rd11_note_show(void) { rd11_note('s'); }
#define RD11_HIDE_EAX 0x48494445u
#define RD11_HIDE_EDX 0x0000d0d0u
#define RD11_HIDE_ECX 0x0000c0c0u
#define RD11_SHOW_EAX 0x53484f57u
#define RD11_SHOW_EDX 0x0000d1d1u
#define RD11_SHOW_ECX 0x0000c1c1u
extern void rd11_hide(void);
extern void rd11_show(void);
__asm__(".text\n.globl _rd11_hide\n_rd11_hide:\n\tcall _rd11_note_hide\n"
        "\tmovl $0x48494445, %eax\n\tmovl $0x0000d0d0, %edx\n\tmovl $0x0000c0c0, %ecx\n\tret\n"
        ".globl _rd11_show\n_rd11_show:\n\tcall _rd11_note_show\n"
        "\tmovl $0x53484f57, %eax\n\tmovl $0x0000d1d1, %edx\n\tmovl $0x0000c1c1, %ecx\n\tret\n");
static void rd11_s_hide(es2_sim_service_call *sc, void *user) {
    (void)user;
    rd11_note('h');
    sc->eax = RD11_HIDE_EAX; sc->edx = RD11_HIDE_EDX; sc->ecx = RD11_HIDE_ECX;
}
static void rd11_s_show(es2_sim_service_call *sc, void *user) {
    (void)user;
    rd11_note('s');
    sc->eax = RD11_SHOW_EAX; sc->edx = RD11_SHOW_EDX; sc->ecx = RD11_SHOW_ECX;
}

/* ---- the own side's dispatch: gx_dispatch_setup's with the two stubs */
static es2_sim_entry rd11_entries[4096];
static es2_sim_dispatch rd11_dispatch;
static int rd11_dispatch_setup(void) {
    es2_sim_dispatch_init(&rd11_dispatch, rd11_entries, 4096, gx_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&rd11_dispatch, &s)) return fail("gfx_blit: dispatch");
    static const struct { uint32_t va; unsigned slots; const char *name; } passed[] = {
        {0x00483a04u, 2, "line (original)"}, {0x00483eb8u, 2, "polygon fill (original)"},
        {0x0047eb30u, 2, "sincos_pair (original)"}, {0x0047ffe8u, 3, "rotate_2d (original)"},
    };
    int row[sizeof(passed) / sizeof(passed[0])] = {0};
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i) {
        for (unsigned k = 0; k < es2_sim_service_count(); ++k) {
            uint32_t va = 0;
            es2_sim_service_row(k, &va);
            if (va == passed[i].va) row[i] = 1;
        }
        if (!row[i] && es2_sim_dispatch_add_service(&rd11_dispatch, passed[i].va, passed[i].name, gx_original,
                                                    (void *)(uintptr_t)passed[i].slots, 0, (uint8_t)passed[i].slots))
            return fail("gfx_blit: dispatch");
    }
    if (es2_sim_dispatch_add_service(&rd11_dispatch, (uint32_t)(uintptr_t)rd11_hide, "cursor hide stub", rd11_s_hide, NULL, 0, 0) ||
        es2_sim_dispatch_add_service(&rd11_dispatch, (uint32_t)(uintptr_t)rd11_show, "cursor show stub", rd11_s_show, NULL, 0, 0))
        return fail("gfx_blit: dispatch (stub)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&rd11_dispatch, &error, &bad)) return fail("gfx_blit: dispatch seal");
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i) {
        if (!row[i]) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&rd11_dispatch, passed[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("gfx_blit: service missing");
        e->service = gx_original;
        e->service_user = (void *)(uintptr_t)passed[i].slots;
    }
    return 0;
}

/* ---- a row on both sides */
static unsigned rd11_ramps_empty, rd11_cursor_off, rd11_cursor_bare, rd11_cursor_shown, rd11_scroll_mode[3], rd11_scroll_spans,
    rd11_scroll_none, rd11_shifted, rd11_area_wide, rd11_angle_high;
static void rd11_regs_random(uint32_t in[3]) { in[0] = random32(); in[1] = random32(); in[2] = random32(); }
/* side 0: the machine code with the entry registers; side 1: the row. */
static void rd11_run(unsigned side, uint32_t va, const uint32_t *a, unsigned n, const uint32_t in[3], uint32_t out[3]) {
    if (!side) { rd11_call((uint32_t)(uintptr_t)(mapped + (va - 0x400000u)), a, n, in, out); return; }
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    if (es2_sim_call(&rd11_dispatch, va, a, n, in, &r)) gx_bad = 1;
    out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
}
/* side 0 through lc_machine (the harness's FPU state; EAX and EDX). */
static void rd11_run_fpu(unsigned side, uint32_t va, const uint32_t *a, unsigned n, uint32_t out[3]) {
    if (!side) { out[0] = lc_machine(va, a, n, &out[1]); out[2] = 0; return; }
    es2_sim_result r;
    uint32_t in[3] = {0, 0, 0};
    memset(&r, 0, sizeof(r));
    if (es2_sim_call(&rd11_dispatch, va, a, n, in, &r)) gx_bad = 1;
    out[0] = r.eax; out[1] = r.edx; out[2] = 0;
}
static int rd11_regs_compare(const char *what, unsigned k, uint32_t out[2][3], unsigned regs) {
    static const char *names[3] = {"EAX", "EDX", "ECX"};
    for (unsigned i = 0; i < regs; ++i)
        if (out[0][i] != out[1][i]) {
            fprintf(stderr, "  case %u: %s\nFAIL: gfx_blit %s case %u: %s %08lx original, %08lx own\n", k, gx_desc, what, k, names[i],
                    (unsigned long)out[0][i], (unsigned long)out[1][i]);
            return 1;
        }
    return 0;
}
static void rd11_begin(unsigned side) {
    gx_side(side);
    rd11_side = side;
    rd11_log_len[side] = 0;
    rd11_log[side][0] = 0;
}

/* ---- ramps */
static int rd11_ramps_case(unsigned k) {
    gx_world_reset();
    uint32_t first = gx_r(40u), count = gx_r(4u) ? 1u + gx_r(8u) : 0, in[3], out[2][3];
    gx_fill_random(GX_SAVE, 0x100u);
    rd11_regs_random(in);
    if (!count) ++rd11_ramps_empty;
    snprintf(gx_desc, sizeof(gx_desc), "ramps first %lu count %lu entry %08lx %08lx %08lx", (unsigned long)first, (unsigned long)count,
             (unsigned long)in[0], (unsigned long)in[1], (unsigned long)in[2]);
    for (unsigned side = 0; side < 2; ++side) {
        rd11_begin(side);
        uint32_t a[4] = {first, count, gx_va(GX_SAVE), random32()};
        rd11_run(side, 0x00486e10u, a, 4, in, out[side]);
        gx_end(side);
    }
    return rd11_regs_compare("ramps", k, out, 3) || gx_compare("ramps", k, 0, 0, 0);
}

/* ---- cursor */
static int rd11_cursor_case(unsigned k) {
    gx_world_reset();
    uint32_t on = gx_r(3u) ? (gx_r(4u) ? 1u : random32() | 1u) : 0, shown = gx_r(2u) ? (gx_r(4u) ? 1u : random32() | 1u) : 0;
    uint32_t pt = GX_RECT, in[3], out[2][3], pre[4] = {random32(), random32(), random32(), random32()};
    gx_point(pt, (int32_t)random32(), (int32_t)random32());
    rd11_regs_random(in);
    if (!on) ++rd11_cursor_off; else if (!shown) ++rd11_cursor_bare; else ++rd11_cursor_shown;
    snprintf(gx_desc, sizeof(gx_desc), "cursor on %08lx shown %08lx point (%08lx,%08lx)", (unsigned long)on, (unsigned long)shown,
             (unsigned long)get32(gx_p(pt)), (unsigned long)get32(gx_p(pt + 4u)));
    for (unsigned side = 0; side < 2; ++side) {
        rd11_begin(side);
        BYTE *d = mapped - 0x400000u;
        put32(d + 0x4a3646u, on);
        put32(d + 0x4a361cu, shown);
        put32(d + 0x4a3620u, pre[0]); put32(d + 0x4a3624u, pre[1]);
        put32(d + 0x4a364au, pre[2]); put32(d + 0x4a364eu, pre[3]);
        put32(d + 0x4a5828u, (uint32_t)(uintptr_t)rd11_hide);
        put32(d + 0x4a5824u, (uint32_t)(uintptr_t)rd11_show);
        uint32_t a[1] = {gx_va(pt)};
        rd11_run(side, 0x00486e7cu, a, 1, in, out[side]);
        gx_end(side);
    }
    if (strcmp(rd11_log[0], rd11_log[1])) {
        fprintf(stderr, "  case %u: %s\nFAIL: gfx_blit cursor case %u: stub log\n  original %s\n  own      %s\n", k, gx_desc, k, rd11_log[0], rd11_log[1]);
        return 1;
    }
    return rd11_regs_compare("cursor", k, out, 3) || gx_compare("cursor", k, 0, 0, 0);
}

/* ---- scroll and save: a kind-0 rect record in the surface */
static void rd11_record(uint32_t rec, int32_t inset, int32_t span) {
    gx_put32(rec, gx_r(3u) ? 0 : gx_r(8u));   /* the kind is not read */
    int32_t l = gx_ri(inset, GX_W - inset - 1), t = gx_ri(inset, GX_H - inset - 1);
    int32_t r = gx_ri(l, l + span < GX_W - inset ? l + span : GX_W - inset - 1), b = gx_ri(t, t + span < GX_H - inset ? t + span : GX_H - inset - 1);
    gx_rect(rec + 4u, l, t, r, b);
}
static void rd11_context(void) {
    gx_context(1);
    if (gx_mode == 2 && !gx_r(6u)) { gx_put32(GX_CONTEXT, 0); ++rd11_scroll_none; }
    if (get32(gx_p(GX_CONTEXT + 0x208u))) ++rd11_shifted;
}
static void rd11_desc(const char *what, uint32_t rec, uint32_t pt) {
    snprintf(gx_desc, sizeof(gx_desc), "%s mode %d regions %lu origin %lu clip (%ld,%ld,%ld,%ld) org (%ld,%ld) rec kind %lu (%ld,%ld,%ld,%ld) point (%ld,%ld)", what,
             gx_mode, (unsigned long)get32(gx_p(GX_CONTEXT)), (unsigned long)get32(gx_p(GX_CONTEXT + 0x208u)), (long)get32(gx_p(GX_CONTEXT + 0x20cu)),
             (long)get32(gx_p(GX_CONTEXT + 0x210u)), (long)get32(gx_p(GX_CONTEXT + 0x214u)), (long)get32(gx_p(GX_CONTEXT + 0x218u)),
             (long)get32(gx_p(GX_CONTEXT + 0x21cu)), (long)get32(gx_p(GX_CONTEXT + 0x220u)), (unsigned long)get32(gx_p(rec)), (long)get32(gx_p(rec + 4u)),
             (long)get32(gx_p(rec + 8u)), (long)get32(gx_p(rec + 12u)), (long)get32(gx_p(rec + 16u)), (long)get32(gx_p(pt)), (long)get32(gx_p(pt + 4u)));
}
static int rd11_scroll_case(unsigned k) {
    gx_world_reset();
    rd11_context();
    uint32_t rec = GX_RECT, pt = GX_RECT + 0x20u, out[2][3], in[3];
    /* Rows and columns stay 8 inside the surface: the point plus the origin shift moves them by up to 6. */
    rd11_record(rec, 8, gx_r(4u) ? GX_W : 3);
    gx_point(pt, gx_ri(-3, 3), gx_ri(-3, 3));
    rd11_regs_random(in);
    ++rd11_scroll_mode[gx_mode];
    for (uint32_t i = 0; i < get32(gx_p(GX_CONTEXT)); ++i)
        if (gx_mode == 2 && get32(gx_p(GX_ENTRIES + 0x20u * i)) == 2u) { ++rd11_scroll_spans; break; }
    rd11_desc("scroll", rec, pt);
    for (unsigned side = 0; side < 2; ++side) {
        rd11_begin(side);
        uint32_t a[2] = {gx_va(rec), gx_va(pt)};
        rd11_run(side, 0x00487bdcu, a, 2, in, out[side]);
        gx_end(side);
    }
    return gx_compare("scroll", k, 0, 0, 0);
}
static int rd11_save_case(unsigned k) {
    gx_world_reset();
    rd11_context();
    uint32_t rec = GX_RECT, out[2][3], in[3];
    rd11_record(rec, 0, 31);
    gx_fill_random(GX_SAVE, 0x800u);
    rd11_regs_random(in);
    rd11_desc("save", rec, rec);
    for (unsigned side = 0; side < 2; ++side) {
        rd11_begin(side);
        uint32_t a[2] = {gx_va(rec), gx_va(GX_SAVE)};
        rd11_run(side, 0x00487eacu, a, 2, in, out[side]);
        gx_end(side);
    }
    return gx_compare("save", k, 0, 0, 0);
}

/* ---- area */
static int rd11_area_case(unsigned k) {
    gx_world_reset();
    uint32_t rect = GX_RECT, in[3], out[2][3];
    int wide = gx_r(2u);
    for (unsigned i = 0; i < 4u; ++i) gx_put32(rect + 4u * i, wide ? random32() : (uint32_t)gx_ri(-80, 80));
    if (wide) ++rd11_area_wide;
    rd11_regs_random(in);
    snprintf(gx_desc, sizeof(gx_desc), "area (%08lx,%08lx,%08lx,%08lx)", (unsigned long)get32(gx_p(rect)), (unsigned long)get32(gx_p(rect + 4u)),
             (unsigned long)get32(gx_p(rect + 8u)), (unsigned long)get32(gx_p(rect + 12u)));
    for (unsigned side = 0; side < 2; ++side) {
        rd11_begin(side);
        uint32_t a[1] = {gx_va(rect)};
        rd11_run(side, 0x00487e94u, a, 1, in, out[side]);
        gx_end(side);
    }
    return rd11_regs_compare("area", k, out, 3) || gx_compare("area", k, 0, 0, 0);
}

/* ---- the rotation thunks */
static int rd11_matrix_case(unsigned k) {
    gx_world_reset();
    uint32_t m = GX_OUT, angle = random32(), out[2][3];
    if (angle >> 16) ++rd11_angle_high;
    gx_fill_random(GX_OUT, 0x20u);
    snprintf(gx_desc, sizeof(gx_desc), "matrix angle %08lx", (unsigned long)angle);
    for (unsigned side = 0; side < 2; ++side) {
        rd11_begin(side);
        uint32_t a[2] = {gx_va(m), angle};
        rd11_run_fpu(side, 0x00488dc7u, a, 2, out[side]);
        gx_end(side);
    }
    return rd11_regs_compare("matrix", k, out, 2) || gx_compare("matrix", k, 0, 0, 0);
}
static int rd11_rotate_case(unsigned k) {
    gx_world_reset();
    uint32_t v = GX_POINTS, m = GX_OUT, o = GX_POINTS + 0x10u, out[2][3];
    gx_point(v, gx_ri(-0x8000, 0x8000), gx_ri(-0x8000, 0x8000));
    for (unsigned i = 0; i < 4u; ++i) gx_put32(m + 4u * i, gx_r(4u) ? (uint32_t)gx_ri(-0x4000, 0x4000) : random32());
    gx_fill_random(o, 8u);
    snprintf(gx_desc, sizeof(gx_desc), "rotate v (%ld,%ld) m %08lx %08lx %08lx %08lx", (long)get32(gx_p(v)), (long)get32(gx_p(v + 4u)),
             (unsigned long)get32(gx_p(m)), (unsigned long)get32(gx_p(m + 4u)), (unsigned long)get32(gx_p(m + 8u)), (unsigned long)get32(gx_p(m + 12u)));
    for (unsigned side = 0; side < 2; ++side) {
        rd11_begin(side);
        uint32_t a[3] = {gx_va(v), gx_va(m), gx_va(o)};
        rd11_run_fpu(side, 0x00488dddu, a, 3, out[side]);
        gx_end(side);
    }
    return rd11_regs_compare("rotate", k, out, 2) || gx_compare("rotate", k, 0, 0, 0);
}

static int gfx_blit_cases(void) {
    current_function = "gfx_blit";
    AddVectoredExceptionHandler(1, gx_fault);
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    gx_world = VirtualAlloc(NULL, GX_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    gx_world_start = malloc(GX_WORLD_SIZE);
    gx_snap[0] = malloc(GX_WORLD_SIZE);
    gx_snap[1] = malloc(GX_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !gx_world || !gx_world_start || !gx_snap[0] || !gx_snap[1])
        return fail("gfx_blit: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("gfx_blit: the counter word is not free");
    if (get32(mapped + (0x4a586cu - 0x400000u) + 4u * 26u) != 0x004899acu) return fail("gfx_blit: the BMP_8 driver table is not at 0x4a586c");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)su_calloc}, {"cw3220.dll", "_free", (void *)su_free},
        {"cw3220.dll", "_memset", (void *)ic_memset}, {"cw3220.dll", "_sprintf", (void *)gx_sprintf},
        {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("gfx_blit: import slot not found");
        }
    if (rd11_dispatch_setup()) return 1;
    gx_active_dispatch = &rd11_dispatch;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"ramps", rd11_ramps_case, 600}, {"cursor", rd11_cursor_case, 800}, {"scroll", rd11_scroll_case, 1200},
        {"area", rd11_area_case, 600}, {"save", rd11_save_case, 1000}, {"matrix", rd11_matrix_case, 400},
        {"rotate", rd11_rotate_case, 600},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            gx_bad = 0;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: gfx_blit %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) {
        printf("gfx blit coverage: %u empty ramp copies, cursor %u held / %u bare / %u bracketed, scroll modes %u/%u/%u (%u with span regions, %u without regions), "
               "%u shifted, %u wide areas, %u angles with a high word\n",
               rd11_ramps_empty, rd11_cursor_off, rd11_cursor_bare, rd11_cursor_shown, rd11_scroll_mode[0], rd11_scroll_mode[1], rd11_scroll_mode[2],
               rd11_scroll_spans, rd11_scroll_none, rd11_shifted, rd11_area_wide, rd11_angle_high);
        if (!rd11_ramps_empty || !rd11_cursor_off || !rd11_cursor_bare || !rd11_cursor_shown || !rd11_scroll_mode[0] || !rd11_scroll_mode[1] ||
            !rd11_scroll_mode[2] || !rd11_scroll_spans || !rd11_scroll_none || !rd11_shifted || !rd11_area_wide || !rd11_angle_high)
            bad = fail("gfx_blit: a path was not covered");
    }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
