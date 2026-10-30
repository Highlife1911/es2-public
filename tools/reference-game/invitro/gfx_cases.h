/* The graphics library's primitives and the scan converter of DBSIM, original
 * machine code against sim/src/gfx.c (--gfx; target :differential_gfx_test).
 * Included after audio_cases.h; reuses startup_cases.h's DATA reset and
 * snapshots (su_reset, su_take, su_compare) and loop_cases.h's loop reset
 * (lc_reset, lc_machine).
 *
 * Both sides draw into one synthetic world (a 64x48 8-bit surface with its
 * row table, the graphics context with up to three clip regions, pens,
 * fills, byte tables, ramp targets, bitmaps, a font, polygons) that the
 * case builds at random and both sides start from; the DATA globals the
 * primitives read (the view 0x4a3652.., the context pointer 0x4a3614, the
 * BMP_8 driver table copied to 0x4a57ac, the ramp entries 0x4a4fa5, the
 * surface and GC pointers) are set identically after each side's reset.
 * The original side runs the machine code (lc_machine); the own side
 * calls gfx.c directly on the harness loop, whose dispatch answers poly_fills.h's
 * line 0x483a04 with the original's code. Compared after every case: EAX
 * where the function defines it, DATA (the span buffers A and B among it)
 * and the world (as a count of differing bytes; no pixels are kept).
 *
 * - clip: 0x48699c, 0x4869fc, 0x486c00, 0x486d15 over random rects and
 *   span lists.
 * - fill: 0x486704 (fill kinds 0 with plain and ramp colours, 4 through
 *   the line, 6 through a table; modes 0, 1, 2 with kind-0 and kind-2
 *   regions; the origin shift), 0x486a34, 0x4895ec (pen kinds 0 and 2),
 *   0x48817c (kinds 0 and 4) and the pixel read slot 0x48a340.
 * - bitmap: 0x4887d8 and 0x4816a8 (placements 0..3, opaque and
 *   transparent, every mode), 0x488974, 0x488012/0x488045/0x488143 with
 *   kind-0 records, 0x487e60 with kind-0 and kind-2 records.
 * - text: 0x482498, 0x4824ec, 0x48251c, 0x482534, 0x482634 over random
 *   fonts (fixed and per-glyph widths), GC alignments 0..2.
 * - scan: 0x4930c1, 0x493141, 0x493192 over random polygons of 1..8
 *   vertices. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "input_layer_cases.h"
#include "es2/sim/gfx.h"

#define GX_WORLD_SIZE 0x10000u
#define GX_W 64
#define GX_H 48
/* World offsets. */
#define GX_ROWS 0x100u      /* the row table: dword bits, then dwords y * pitch */
#define GX_CONTEXT 0x400u   /* the clip record C (0x230 bytes) */
#define GX_ENTRIES 0x700u   /* three region entries, 0x20 each */
#define GX_SPANS 0x800u     /* four span lists, 0x100 each */
#define GX_PEN 0xc00u       /* kind + colour word or inline table */
#define GX_FILL 0xd10u
#define GX_RAMP 0xe20u      /* two int16 tables of 256 */
#define GX_RECT 0x1300u     /* the call's rect, point, record */
#define GX_OUT 0x1400u      /* clip outputs */
#define GX_BITS 0x2000u     /* the surface (0xc00 bytes) */
#define GX_BITMAP 0x3000u   /* a header; the GLBitmap object at +0x40; pixels at +0x100 */
#define GX_FONT 0x3800u     /* F; widths at +0x40; glyph offsets at +0x140 */
#define GX_GLYPHS 0x4000u   /* 0x1000 bytes of glyph pixels */
#define GX_POINTS 0x5000u
#define GX_SCAN 0x5100u
#define GX_GC 0x6000u       /* the GC object (0x240 bytes) */
#define GX_SAVE 0x6400u     /* saved pixels (0x800) */
#define GX_TEXT 0x6c00u
#define GX_SURFACE 0x6d00u  /* the current surface stand-in (pointer value only) */

static BYTE *gx_world, *gx_world_start, *gx_snap[2];
static int gx_bad;
static unsigned gx_cases_kind4, gx_cases_regions, gx_cases_grab, gx_cases_transparent, gx_cases_reversed, gx_cases_text_glyphs;
static uint32_t gx_va(uint32_t off) { return (uint32_t)(uintptr_t)(gx_world + off); }
static BYTE *gx_p(uint32_t off) { return gx_world_start + off; }   /* the start image the case builds */
static void gx_put32(uint32_t off, uint32_t v) { put32(gx_world_start + off, v); }
static void gx_put16(uint32_t off, uint32_t v) { put16(gx_world_start + off, (uint16_t)v); }
static uint32_t gx_r(uint32_t n) { return random32() % n; }
static int32_t gx_ri(int32_t lo, int32_t hi) { return lo + (int32_t)gx_r((uint32_t)(hi - lo + 1)); }
static void gx_rect(uint32_t off, int32_t l, int32_t t, int32_t r, int32_t b) {
    gx_put32(off, (uint32_t)l); gx_put32(off + 4u, (uint32_t)t); gx_put32(off + 8u, (uint32_t)r); gx_put32(off + 12u, (uint32_t)b);
}
static void gx_point(uint32_t off, int32_t x, int32_t y) { gx_put32(off, (uint32_t)x); gx_put32(off + 4u, (uint32_t)y); }
static void gx_fill_random(uint32_t off, uint32_t n) { for (uint32_t i = 0; i < n; ++i) gx_world_start[off + i] = (BYTE)random32(); }
static int __cdecl gx_sprintf(char *buffer, const char *format, ...) { (void)buffer; (void)format; return 0; }

/* The own side's dispatch: the service rows and poly_fills.h's line as the
 * original. */
static es2_sim_entry gx_entries[4096];
static es2_sim_dispatch gx_dispatch;
static void gx_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own gfx: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?", r->reason ? r->reason : "");
    gx_bad = 1;
}
static void gx_original(es2_sim_service_call *call, void *user) {
    unsigned n = (unsigned)(uintptr_t)user;
    es2_hook_result r;
    uint32_t a[4] = {0, 0, 0, 0};
    for (unsigned i = 0; i < n; ++i) a[i] = call->slots[i];
    uint32_t regs[3] = {call->eax, call->edx, call->ecx};
    es2_hook_call c = {mapped + (call->va - 0x400000u), ES2_ABI_CDECL, n, a, initial_fpu, &r, regs};
    es2_hook_machine_call(&c);
    call->eax = r.eax; call->edx = r.edx;
}
static int gx_dispatch_setup(void) {
    es2_sim_dispatch_init(&gx_dispatch, gx_entries, 4096, gx_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&gx_dispatch, &s)) return fail("gfx: dispatch");
    /* poly_fills.h's line and (for the rotated blit) polygon fill and the two rotation helpers as the original; a VA that
     * has a row (0x483a04, 0x483eb8) is repointed to the original after the seal. */
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
        if (!row[i] && es2_sim_dispatch_add_service(&gx_dispatch, passed[i].va, passed[i].name, gx_original, (void *)(uintptr_t)passed[i].slots, 0,
                                                    (uint8_t)passed[i].slots))
            return fail("gfx: dispatch");
    }
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&gx_dispatch, &error, &bad)) return fail("gfx: dispatch seal");
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i) {
        if (!row[i]) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&gx_dispatch, passed[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("gfx: service missing");
        e->service = gx_original;
        e->service_user = (void *)(uintptr_t)passed[i].slots;
    }
    return 0;
}

/* The DATA globals both sides read, after the side's reset. The own side's
 * dispatch is gx_dispatch; gl_cases.h runs the rotated blit with its own (the
 * fill layer as own code). */
static es2_sim_dispatch *gx_active_dispatch = &gx_dispatch;
static int gx_gc_present;
static void gx_side(unsigned side) {
    lc_reset(side, 47, 0, 1);
    lc_loop.dispatch = gx_active_dispatch;
    memcpy(gx_world, gx_world_start, GX_WORLD_SIZE);
    BYTE *d = mapped - 0x400000u;
    put32(d + 0x4a3614u, gx_va(GX_CONTEXT));
    put32(d + 0x4a3652u, gx_va(GX_ROWS));
    put32(d + 0x4a3656u, GX_W);
    put32(d + 0x4a365au, GX_H);
    put32(d + 0x4a3662u, 8u);
    put32(d + 0x4a3676u, GX_W);
    put32(d + 0x4a367au, GX_H);
    put32(d + 0x6c5ed0u, gx_va(GX_SURFACE));
    put32(d + 0x6c5f70u, gx_gc_present ? gx_va(GX_GC) : 0);
    for (unsigned i = 0; i < 36u; ++i) put32(d + 0x4a57acu + 4u * i, get32(d + 0x4a586cu + 4u * i));
    put32(d + 0x4a583cu, 36u);
    for (unsigned i = 0; i < 4u; ++i) {
        put32(d + 0x4a4fa5u + 8u * i, gx_va(GX_RAMP + 0x200u * (i & 1u)));
        put16(d + 0x4a4fa5u + 8u * i + 4u, (uint16_t)(0x20u + 0x30u * i));
    }
}
static void gx_end(unsigned side) {
    /* Slot 19's rightward path (0x48a13b std) returns with the direction
     * flag set (no cld before 0x48a1c9); the C runtime's string moves
     * after it need it clear. The own code has no flag to leak. */
    __asm__ volatile("cld" ::: "cc");
    su_take(side);
    memcpy(gx_snap[side], gx_world, GX_WORLD_SIZE);
}
static char gx_desc[512];   /* the case's parameters, printed with a failure */
static int gx_compare(const char *what, unsigned k, uint32_t eax0, uint32_t eax1, int eax_defined) {
    if (gx_bad || (eax_defined && eax0 != eax1) || memcmp(gx_snap[0], gx_snap[1], GX_WORLD_SIZE)) fprintf(stderr, "  case %u: %s\n", k, gx_desc);
    if (gx_bad) { fprintf(stderr, "FAIL: gfx %s case %u: a call was refused\n", what, k); return 1; }
    if (eax_defined && eax0 != eax1) {
        fprintf(stderr, "FAIL: gfx %s case %u: EAX %08lx original, %08lx own\n", what, k, (unsigned long)eax0, (unsigned long)eax1);
        return 1;
    }
    if (su_compare(what, k)) return 1;
    if (memcmp(gx_snap[0], gx_snap[1], GX_WORLD_SIZE)) {
        unsigned count = 0, shown = 0;
        for (uint32_t i = 0; i < GX_WORLD_SIZE; ++i) {
            if (gx_snap[0][i] == gx_snap[1][i]) continue;
            if (shown++ < 16) fprintf(stderr, "  world +0x%04lx original %02x own %02x\n", (unsigned long)i, gx_snap[0][i], gx_snap[1][i]);
            ++count;
        }
        fprintf(stderr, "FAIL: gfx %s case %u: %u world bytes differ\n", what, k, count);
        return 1;
    }
    return 0;
}

/* ---- the world a case starts from */
static int gx_mode;
static void gx_world_reset(void) {
    memset(gx_world_start, 0, GX_WORLD_SIZE);
    gx_put32(GX_ROWS, gx_va(GX_BITS));
    for (int32_t y = 0; y < GX_H + 16; ++y) gx_put32(GX_ROWS + 4u + 4u * (uint32_t)y, (uint32_t)(y * GX_W));
    gx_fill_random(GX_BITS, GX_W * GX_H);
    gx_fill_random(GX_PEN + 4u, 0x100u);
    gx_fill_random(GX_FILL + 4u, 0x100u);
    for (uint32_t i = 0; i < 0x200u; ++i) gx_put16(GX_RAMP + 2u * i, (uint32_t)gx_ri(-300, 300));
    gx_gc_present = 0;
}
/* A span list at `off` with `count` rows from y0, pairs inside [x_lo, x_hi]. */
static void gx_spans(uint32_t off, int32_t y0, uint32_t count, int32_t x_lo, int32_t x_hi) {
    gx_put16(off, (uint32_t)y0);
    gx_put16(off + 2u, count);
    for (uint32_t i = 0; i < count; ++i) {
        int32_t l = gx_ri(x_lo, x_hi), r = gx_r(5) ? gx_ri(l, x_hi) : gx_ri(x_lo, x_hi);
        gx_put16(off + 4u + 4u * i, (uint32_t)l);
        gx_put16(off + 6u + 4u * i, (uint32_t)r);
    }
}
/* The clip record: mode 0, 1 or 2; the clip rect inside the surface; the
 * origin flag and origin (a shift of -3..3); up to three regions. */
static void gx_context(int with_regions) {
    uint32_t c = GX_CONTEXT;
    gx_mode = (int)gx_r(3u);
    if (!with_regions && gx_mode == 2) gx_mode = 1;
    gx_put32(c + 0x204u, (uint32_t)gx_mode);
    int32_t l = gx_ri(0, GX_W - 8), t = gx_ri(0, GX_H - 8);
    gx_rect(c + 0x20cu, l, t, gx_ri(l, GX_W - 1), gx_ri(t, GX_H - 1));
    gx_put32(c + 0x208u, gx_r(2u) ? (gx_r(4u) ? 1u : 2u) : 0);
    gx_point(c + 0x21cu, l + gx_ri(-3, 3), t + gx_ri(-3, 3));
    gx_put32(c + 0x224u, gx_va(GX_PEN));
    gx_put32(c + 0x228u, gx_va(GX_FILL));
    uint32_t count = with_regions ? 1u + gx_r(3u) : 0;
    gx_put32(c, count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t e = GX_ENTRIES + 0x20u * i;
        gx_put32(c + 4u + 4u * i, gx_va(e));
        uint32_t kind = gx_r(8u) ? (gx_r(2u) ? 0 : 2u) : 5u;
        gx_put32(e, kind);
        if (kind == 0) {
            int32_t rl = gx_ri(0, GX_W - 4), rt = gx_ri(0, GX_H - 4);
            gx_rect(e + 4u, rl, rt, gx_ri(rl, GX_W - 1), gx_ri(rt, GX_H - 1));
        } else {
            uint32_t s = GX_SPANS + 0x100u * i;
            gx_put32(e + 4u, gx_va(s));
            uint32_t rows = 1u + gx_r(20u);
            gx_spans(s, gx_ri(0, GX_H - (int32_t)rows), rows, 0, GX_W - 1);
            ++gx_cases_regions;
        }
    }
}
/* A kind-0 colour word: plain (high byte 0), a ramp colour (high byte 4)
 * or, rarely, an undrawable one. */
static uint32_t gx_colour(void) {
    uint32_t r = gx_r(10u);
    if (r < 6u) return gx_r(256u);
    if (r < 9u) return 0x04000000u | (gx_r(256u) << 16) | gx_r(4u);
    return 0x07000000u | gx_r(0x10000u);
}

/* ---- clip */
static int gx_clip_case(unsigned k) {
    gx_world_reset();
    uint32_t which = gx_r(4u), a = GX_RECT, b = GX_RECT + 0x10u, out = GX_OUT, eax[2] = {0, 0};
    int32_t l = gx_ri(-8, 70), t = gx_ri(-8, 50);
    gx_rect(a, l, t, gx_ri(l - 2, 72), gx_ri(t - 1, 52));
    l = gx_ri(-8, 70); t = gx_ri(-8, 50);
    gx_rect(b, l, t, gx_ri(l - 2, 72), gx_ri(t - 1, 52));
    gx_spans(GX_SPANS, gx_ri(-4, 20), 1u + gx_r(12u), -8, 40);
    gx_spans(GX_SPANS + 0x100u, gx_ri(-4, 20), 1u + gx_r(12u), -8, 40);
    for (unsigned side = 0; side < 2; ++side) {
        gx_side(side);
        uint32_t s1 = gx_va(GX_SPANS), s2 = gx_va(GX_SPANS + 0x100u), va = gx_va(a), vb = gx_va(b), vo = gx_va(out);
        if (which == 0) {
            uint32_t args[3] = {va, vb, vo};
            if (!side) eax[0] = lc_machine(0x0048699cu, args, 3, NULL); else es2_sim_gfx_rect_clip(&lc_loop, va, vb, vo, &eax[1]);
        } else if (which == 1u) {
            uint32_t args[2] = {va, vo};
            if (!side) eax[0] = lc_machine(0x004869fcu, args, 2, NULL); else if (es2_sim_gfx_rect_to_spans(&lc_loop, va, vo, &eax[1])) gx_bad = 1;
        } else if (which == 2u) {
            uint32_t args[3] = {s1, s2, vo};
            if (!side) eax[0] = lc_machine(0x00486c00u, args, 3, NULL); else es2_sim_gfx_spans_clip_spans(&lc_loop, s1, s2, vo, &eax[1]);
        } else {
            uint32_t args[3] = {va, s1, vo};
            if (!side) eax[0] = lc_machine(0x00486d15u, args, 3, NULL); else es2_sim_gfx_rect_clip_spans(&lc_loop, va, s1, vo, &eax[1]);
        }
        gx_end(side);
    }
    return gx_compare("clip", k, eax[0], eax[1], 1);
}

/* ---- fill */
static int gx_fill_case(unsigned k) {
    gx_world_reset();
    gx_context(1);
    uint32_t which = gx_r(5u), eax[2] = {0, 0};
    uint32_t fk = gx_r(10u) < 6u ? 0 : gx_r(3u) ? 6u : 4u;
    gx_put32(GX_FILL, fk);
    if (fk == 0) gx_put32(GX_FILL + 4u, gx_colour());
    if (fk == 4u) { gx_put32(GX_FILL + 4u, gx_r(256u)); ++gx_cases_kind4; }
    gx_put32(GX_PEN, gx_r(2u) ? 0 : 2u);
    if (!get32(gx_p(GX_PEN))) gx_put32(GX_PEN + 4u, gx_colour());
    uint32_t rect = GX_RECT, pt = GX_RECT + 0x10u, spans = GX_SPANS + 0x300u, va_rect = 0, va_pt = 0, va_spans = 0;
    if (which == 0) {
        int32_t l = gx_ri(4, GX_W - 6), t = gx_ri(4, GX_H - 6);
        gx_rect(rect, l, t, gx_ri(l, GX_W - 5), gx_ri(t, GX_H - 5));
    } else if (which == 1u) {
        uint32_t rows = 1u + gx_r(12u);
        gx_spans(spans, gx_ri(6, GX_H - 7 - (int32_t)rows), rows, 6, GX_W - 7);
        gx_point(pt, gx_ri(-2, 2), gx_ri(-2, 2));
    } else if (which == 2u || which == 4u) {
        gx_point(pt, gx_ri(3, GX_W - 4), gx_ri(3, GX_H - 4));   /* the shifted point stays on the surface (row -1's table entry is the bits pointer) */
    } else {
        if (fk == 6u) { gx_put32(GX_FILL, 0); gx_put32(GX_FILL + 4u, gx_colour()); }   /* the ellipse draws kinds 0 and 4 only */
        int32_t l = gx_ri(3, GX_W - 10), t = gx_ri(3, GX_H - 10);
        gx_rect(rect, l, t, gx_ri(l + 2, GX_W - 4), gx_ri(t + 2, GX_H - 4));
        if (gx_r(2u)) gx_rect(rect, gx_ri(l + 2, GX_W - 4), gx_ri(t + 2, GX_H - 4), l, t);   /* r < l, b < t: the half axes are absolute */
    }
    for (unsigned side = 0; side < 2; ++side) {
        gx_side(side);
        va_rect = gx_va(rect); va_pt = gx_va(pt); va_spans = gx_va(spans);
        if (which == 0) {
            uint32_t args[1] = {va_rect};
            if (!side) lc_machine(0x00486704u, args, 1, NULL); else if (es2_sim_gfx_rect_fill(&lc_loop, va_rect)) gx_bad = 1;
        } else if (which == 1u) {
            uint32_t args[2] = {va_spans, va_pt};
            if (!side) lc_machine(0x00486a34u, args, 2, NULL); else if (es2_sim_gfx_span_fill(&lc_loop, va_spans, va_pt)) gx_bad = 1;
        } else if (which == 2u) {
            uint32_t args[1] = {va_pt};
            if (!side) lc_machine(0x004895ecu, args, 1, NULL); else if (es2_sim_gfx_point(&lc_loop, va_pt)) gx_bad = 1;
        } else if (which == 3u) {
            uint32_t args[1] = {va_rect};
            if (!side) lc_machine(0x0048817cu, args, 1, NULL); else if (es2_sim_gfx_ellipse(&lc_loop, va_rect)) gx_bad = 1;
        } else {
            uint32_t args[1] = {va_pt};
            if (!side) eax[0] = lc_machine(0x0048a340u, args, 1, NULL); else if (es2_sim_gfx_slot(&lc_loop, 4u, args, 1, &eax[1])) gx_bad = 1;
        }
        gx_end(side);
    }
    return gx_compare("fill", k, eax[0], eax[1], which == 4u);
}

/* ---- bitmap */
static int gx_bitmap_case(unsigned k) {
    gx_world_reset();
    gx_context(1);
    uint32_t which = gx_r(7u), eax[2] = {0, 0};
    int32_t w = gx_ri(1, 16), h = gx_ri(1, 16);
    uint32_t hdr = GX_BITMAP, obj = GX_BITMAP + 0x40u, rec = GX_RECT, pt = GX_RECT + 0x20u;
    gx_put16(hdr, (uint32_t)h);
    gx_put16(hdr + 2u, (uint32_t)w);
    gx_world_start[hdr + 4u] = (BYTE)(gx_r(2u) ? 8u : gx_r(256u));
    gx_world_start[hdr + 5u] = (BYTE)((gx_r(2u) ? 0x10u : 0) | (gx_r(3u) ? 0 : 0xe0u) | (which == 2u && gx_r(2u) ? gx_r(16u) : 0));
    if ((gx_world_start[hdr + 5u] & 0xfu) == 1u) gx_world_start[hdr + 5u] &= 0xf0u;
    gx_put32(hdr + 6u, (uint32_t)(w * h));
    gx_put32(hdr + 0xau, gx_va(GX_BITMAP + 0x100u));
    for (int32_t i = 0; i < w * h; ++i) gx_world_start[GX_BITMAP + 0x100u + (uint32_t)i] = (BYTE)(gx_r(3u) ? gx_r(256u) : 0);
    memcpy(gx_world_start + obj + 4u, gx_world_start + hdr, 0xeu);
    gx_point(pt, gx_ri(-20, GX_W + 4), gx_ri(-20, GX_H + 4));
    uint32_t flags = gx_r(4u) | (gx_r(2u) ? 0 : gx_r(256u) & ~3u);
    if (!(gx_world_start[hdr + 5u] & 0x10u)) ++gx_cases_transparent;
    if (which == 3u || which == 4u || which == 5u) {
        int32_t l = gx_ri(0, GX_W - 1), t = gx_ri(0, GX_H - 1);
        gx_put32(rec, 0);
        gx_rect(rec + 4u, l, t, gx_ri(l, l + 31 < GX_W ? l + 31 : GX_W - 1), gx_ri(t, t + 31 < GX_H ? t + 31 : GX_H - 1));
        gx_fill_random(GX_SAVE, 0x800u);
    }
    int32_t dx = gx_ri(-3, 3), dy = gx_ri(-3, 3);
    if (which == 6u) {
        gx_point(pt, dx, dy);
        if (gx_r(2u)) {
            gx_put32(rec, 0);
            int32_t l = gx_ri(4, GX_W - 9), t = gx_ri(4, GX_H - 9);
            int32_t rw = gx_r(4u) ? gx_ri(1, GX_W - 8 - l) : gx_ri(1, 3), rh = gx_r(4u) ? gx_ri(1, GX_H - 8 - t) : gx_ri(1, 3);
            gx_rect(rec + 4u, l, t, l + rw - 1, t + rh - 1);
        } else {
            gx_put32(rec, 2u);
            gx_put32(rec + 4u, gx_va(GX_SPANS + 0x300u));
            uint32_t rows = 1u + gx_r(12u);
            /* Rows and columns stay 8 inside the surface: the point plus the origin shift moves them by up to 6. */
            gx_spans(GX_SPANS + 0x300u, gx_ri(8, GX_H - 9 - (int32_t)rows), rows, 8, GX_W - 9);
        }
    }
    snprintf(gx_desc, sizeof(gx_desc), "bitmap which %lu w %ld h %ld bpp %u flags %02x call flags %lx point (%ld,%ld) mode %d regions %lu origin %lu clip (%ld,%ld,%ld,%ld) org (%ld,%ld) rec kind %lu (%ld,%ld,%ld,%ld)",
             (unsigned long)which, (long)w, (long)h, gx_world_start[hdr + 4u], gx_world_start[hdr + 5u], (unsigned long)flags, (long)get32(gx_p(pt)), (long)get32(gx_p(pt + 4u)), gx_mode,
             (unsigned long)get32(gx_p(GX_CONTEXT)), (unsigned long)get32(gx_p(GX_CONTEXT + 0x208u)), (long)get32(gx_p(GX_CONTEXT + 0x20cu)), (long)get32(gx_p(GX_CONTEXT + 0x210u)),
             (long)get32(gx_p(GX_CONTEXT + 0x214u)), (long)get32(gx_p(GX_CONTEXT + 0x218u)), (long)get32(gx_p(GX_CONTEXT + 0x21cu)), (long)get32(gx_p(GX_CONTEXT + 0x220u)),
             (unsigned long)get32(gx_p(rec)), (long)get32(gx_p(rec + 4u)), (long)get32(gx_p(rec + 8u)), (long)get32(gx_p(rec + 12u)), (long)get32(gx_p(rec + 16u)));
    for (unsigned side = 0; side < 2; ++side) {
        gx_side(side);
        uint32_t vh = gx_va(hdr), vo = gx_va(obj), vp = gx_va(pt), vr = gx_va(rec), vs = gx_va(GX_SAVE);
        if (which == 0) {
            uint32_t args[3] = {vh, vp, flags};
            if (!side) lc_machine(0x004887d8u, args, 3, NULL); else if (es2_sim_gfx_bitmap_draw(&lc_loop, vh, vp, flags)) gx_bad = 1;
        } else if (which == 1u) {
            uint32_t args[3] = {vo, vp, flags};
            if (!side) lc_machine(0x004816a8u, args, 3, NULL); else if (es2_sim_gfx_bitmap_blit(&lc_loop, vo, vp, flags)) gx_bad = 1;
        } else if (which == 2u) {
            uint32_t args[3] = {vh, vp, flags};
            ++gx_cases_grab;
            if (!side) lc_machine(0x00488974u, args, 3, NULL); else if (es2_sim_gfx_bitmap_grab(&lc_loop, vh, vp, flags)) gx_bad = 1;
        } else if (which == 3u) {
            uint32_t args[2] = {vr, vs};
            if (!side) lc_machine(0x00488012u, args, 2, NULL); else if (es2_sim_gfx_save(&lc_loop, vr, vs)) gx_bad = 1;
        } else if (which == 4u) {
            uint32_t args[1] = {vr};
            if (!side) eax[0] = lc_machine(0x00488045u, args, 1, NULL); else if (es2_sim_gfx_size(&lc_loop, vr, &eax[1])) gx_bad = 1;
        } else if (which == 5u) {
            uint32_t args[3] = {vr, vp, vs};
            if (!side) lc_machine(0x00488143u, args, 3, NULL); else if (es2_sim_gfx_restore(&lc_loop, vr, vp, vs)) gx_bad = 1;
        } else {
            uint32_t args[2] = {vr, vp};
            if (!side) lc_machine(0x00487e60u, args, 2, NULL); else if (es2_sim_gfx_scroll(&lc_loop, vr, vp)) gx_bad = 1;
        }
        gx_end(side);
    }
    return gx_compare("bitmap", k, eax[0], eax[1], which == 4u);
}

/* ---- text */
/* The region span lists with every pair ordered (left <= right): the
 * glyph headers are never opaque (flags = residue & 0xe0), and slot 26's
 * transparent kernel (0x489c2d..0x489c46) runs away on the non-positive
 * column count a reversed pair gives through slot 28's one-row rects
 * (the original faults at 0x489c30; the own code refuses). */
static void gx_regions_ordered(void) {
    uint32_t c = GX_CONTEXT, count = get32(gx_p(c));
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t e = GX_ENTRIES + 0x20u * i;
        if (get32(gx_p(e)) != 2u) continue;
        uint32_t s = GX_SPANS + 0x100u * i, rows = get16(gx_p(s + 2u));
        for (uint32_t r = 0; r < rows; ++r) {
            uint32_t p = s + 4u + 4u * r;
            int16_t left = (int16_t)get16(gx_p(p)), right = (int16_t)get16(gx_p(p + 2u));
            if (left > right) { gx_put16(p, (uint32_t)(uint16_t)right); gx_put16(p + 2u, (uint32_t)(uint16_t)left); }
        }
    }
}
static int gx_text_case(unsigned k) {
    gx_world_reset();
    gx_context(1);
    gx_regions_ordered();
    gx_put32(GX_FILL, 0);
    uint32_t f = GX_FONT, widths = GX_FONT + 0x40u, offsets = GX_FONT + 0x140u, eax[2] = {0, 0};
    int32_t first = 0x20, height = gx_ri(3, 12), fixed = gx_r(2u) ? -1 : gx_ri(2, 9);
    gx_put16(f + 8u, (uint32_t)first);
    gx_put16(f + 0xau, (uint32_t)height);
    gx_put16(f + 0xcu, (uint32_t)fixed);
    gx_world_start[f + 0x12u] = 8u;
    gx_put16(f + 0x16u, (uint32_t)gx_ri(0, height));
    gx_put32(f + 0x22u, gx_va(GX_GLYPHS));
    gx_put32(f + 0x26u, gx_va(offsets));
    gx_put32(f + 0x2au, gx_va(widths));
    for (uint32_t ch = 0; ch < 0x60u; ++ch) {
        gx_world_start[widths + ch] = (BYTE)gx_ri(1, 9);
        gx_put32(offsets + 4u * ch, 16u * gx_r(0xf0u));
    }
    for (uint32_t i = 0; i < 0x1000u; ++i) gx_world_start[GX_GLYPHS + i] = (BYTE)(gx_r(3u) ? gx_r(256u) : 0);
    uint32_t len = 1u + gx_r(8u);
    for (uint32_t i = 0; i < len; ++i) gx_world_start[GX_TEXT + i] = (BYTE)(first + gx_ri(0, 0x3f));
    gx_world_start[GX_TEXT + len] = 0;
    gx_gc_present = 1;
    gx_fill_random(GX_GC, 0x240u);
    gx_world_start[GX_GC + 0x230u] = 0;
    gx_put32(GX_GC + 0x231u, gx_r(3u));
    gx_put32(GX_GC + 0x235u, gx_r(80u));
    uint32_t which = gx_r(5u), pt = GX_RECT, ch = (uint32_t)(first + gx_ri(0, 0x3f)) | (gx_r(2u) ? 0 : gx_r(0x10000u) << 16);   /* the high word is ignored; the low word is the glyph index (0x48257d) */
    gx_point(pt, gx_ri(-10, GX_W), gx_ri(-4, GX_H + 4));
    snprintf(gx_desc, sizeof(gx_desc), "text which %lu height %ld fixed %ld baseline %u ch %lx len %lu point (%ld,%ld) mode %d origin %lu clip (%ld,%ld,%ld,%ld) org (%ld,%ld) align %lu width %lu",
             (unsigned long)which, (long)height, (long)fixed, get16(gx_p(f + 0x16u)), (unsigned long)ch, (unsigned long)len, (long)get32(gx_p(pt)), (long)get32(gx_p(pt + 4u)), gx_mode,
             (unsigned long)get32(gx_p(GX_CONTEXT + 0x208u)), (long)get32(gx_p(GX_CONTEXT + 0x20cu)), (long)get32(gx_p(GX_CONTEXT + 0x210u)),
             (long)get32(gx_p(GX_CONTEXT + 0x214u)), (long)get32(gx_p(GX_CONTEXT + 0x218u)), (long)get32(gx_p(GX_CONTEXT + 0x21cu)), (long)get32(gx_p(GX_CONTEXT + 0x220u)),
             (unsigned long)get32(gx_p(GX_GC + 0x231u)), (unsigned long)get32(gx_p(GX_GC + 0x235u)));
    for (unsigned side = 0; side < 2; ++side) {
        gx_side(side);
        uint32_t vf = gx_va(f), vp = gx_va(pt), vt = gx_va(GX_TEXT);
        if (which == 0) {
            uint32_t args[2] = {vf, ch};
            if (!side) eax[0] = lc_machine(0x00482498u, args, 2, NULL); else es2_sim_gfx_char_width(&lc_loop, vf, ch, &eax[1]);
        } else if (which == 1u) {
            uint32_t args[2] = {vf, vt};
            if (!side) eax[0] = lc_machine(0x004824ecu, args, 2, NULL); else es2_sim_gfx_string_width(&lc_loop, vf, vt, &eax[1]);
        } else if (which == 2u) {
            uint32_t args[1] = {vf};
            if (!side) eax[0] = lc_machine(0x0048251cu, args, 1, NULL); else es2_sim_gfx_font_height(&lc_loop, vf, &eax[1]);
        } else if (which == 3u) {
            uint32_t args[3] = {vf, vp, ch};
            ++gx_cases_text_glyphs;
            if (!side) lc_machine(0x00482534u, args, 3, NULL); else if (es2_sim_gfx_char(&lc_loop, vf, vp, ch)) gx_bad = 1;
        } else {
            uint32_t args[3] = {vf, vp, vt};
            gx_cases_text_glyphs += len;
            if (!side) lc_machine(0x00482634u, args, 3, NULL); else if (es2_sim_gfx_string(&lc_loop, vf, vp, vt)) gx_bad = 1;
        }
        gx_end(side);
    }
    return gx_compare("text", k, eax[0], eax[1], which <= 2u);
}

/* ---- scan */
static int gx_scan_case(unsigned k) {
    gx_world_reset();
    uint32_t count = 1u + gx_r(8u), which = gx_r(4u), eax[2] = {0, 0};
    if (gx_r(4u) == 0) count = 3u + gx_r(2u);
    for (uint32_t i = 0; i < count; ++i) gx_point(GX_POINTS + 8u * i, gx_ri(-10, 70), gx_ri(0, 40));
    if (gx_r(3u) == 0) {   /* a convex polygon: points around a centre, by angle */
        int32_t cx = gx_ri(10, 50), cy = gx_ri(10, 30);
        for (uint32_t i = 0; i < count; ++i) {
            double a = 6.283185307 * ((double)i + 0.3 * (double)gx_r(100u) / 100.0) / (double)count;
            int32_t rx = gx_ri(2, 14), ry = gx_ri(2, 10);
            gx_point(GX_POINTS + 8u * i, cx + (int32_t)(rx * __builtin_cos(a)), cy + (int32_t)(ry * __builtin_sin(a)));
        }
    }
    {
        int n = snprintf(gx_desc, sizeof(gx_desc), "scan which %lu count %lu points", (unsigned long)which, (unsigned long)count);
        for (uint32_t i = 0; i < count && n > 0 && (size_t)n < sizeof(gx_desc); ++i)
            n += snprintf(gx_desc + n, sizeof(gx_desc) - (size_t)n, " (%ld,%ld)", (long)get32(gx_p(GX_POINTS + 8u * i)), (long)get32(gx_p(GX_POINTS + 8u * i + 4u)));
    }
    for (unsigned side = 0; side < 2; ++side) {
        gx_side(side);
        uint32_t vp = gx_va(GX_POINTS), vo = gx_va(GX_SCAN), args[3] = {count, vp, vo};
        uint32_t va = which <= 1u ? 0x004930c1u : which == 2u ? 0x00493141u : 0x00493192u;
        if (!side) eax[0] = lc_machine(va, args, 3, NULL);
        else {
            int bad = va == 0x004930c1u ? es2_sim_gfx_scan(&lc_loop, count, vp, vo, &eax[1])
                    : va == 0x00493141u ? es2_sim_gfx_scan_reversed(&lc_loop, count, vp, vo, &eax[1])
                                        : es2_sim_gfx_scan_convert(&lc_loop, count, vp, vo, &eax[1]);
            if (bad) gx_bad = 1;
            if (va == 0x00493141u && count > 2u) ++gx_cases_reversed;
        }
        gx_end(side);
    }
    return gx_compare("scan", k, eax[0], eax[1], 1);
}

/* ---- stretch and rotate */
static unsigned gx_cases_stretch_spans, gx_cases_stretch_flipped, gx_cases_rotated;
/* A w x h bitmap header at GX_BITMAP (opaque or not, residue flags), its
 * pixels at +0x100 and a GLBitmap object at +0x40 holding a copy. */
static void gx_bitmap_random(int32_t w, int32_t h) {
    uint32_t hdr = GX_BITMAP, obj = GX_BITMAP + 0x40u;
    gx_put16(hdr, (uint32_t)h);
    gx_put16(hdr + 2u, (uint32_t)w);
    gx_world_start[hdr + 4u] = 8u;
    gx_world_start[hdr + 5u] = (BYTE)((gx_r(2u) ? 0x10u : 0) | (gx_r(3u) ? 0 : 0xe0u));
    gx_put32(hdr + 6u, (uint32_t)(w * h));
    gx_put32(hdr + 0xau, gx_va(GX_BITMAP + 0x100u));
    for (int32_t i = 0; i < w * h; ++i) gx_world_start[GX_BITMAP + 0x100u + (uint32_t)i] = (BYTE)(gx_r(3u) ? gx_r(256u) : 0);
    memcpy(gx_world_start + obj + 4u, gx_world_start + hdr, 0xeu);
}
/* 0x488aa8, 0x4817c8, 0x481c3b and slot 29 (0x48afc8) called directly
 * (with or without a clip rect): sizes 0..40, flags 0..3 or any byte. */
static int gx_stretch_case(unsigned k) {
    gx_world_reset();
    gx_context(1);
    uint32_t which = gx_r(4u), hdr = GX_BITMAP, obj = GX_BITMAP + 0x40u, pt = GX_RECT + 0x20u, size = GX_RECT + 0x30u, clip = GX_RECT + 0x40u;
    int32_t w = gx_ri(1, 16), h = gx_ri(1, 16);
    gx_bitmap_random(w, h);
    int32_t sx = gx_r(16u) ? gx_ri(1, 40) : 0, sy = gx_r(16u) ? gx_ri(1, 40) : 0;
    gx_point(size, sx, sy);
    gx_point(pt, gx_ri(-20, GX_W + 4), gx_ri(-20, GX_H + 4));
    int32_t l = gx_ri(0, GX_W - 1), t = gx_ri(0, GX_H - 1);
    gx_rect(clip, l, t, gx_ri(l, GX_W - 1), gx_ri(t, GX_H - 1));
    int with_clip = which == 3u && gx_r(2u);
    uint32_t flags = gx_r(4u) ? gx_r(4u) : gx_r(256u);
    if (flags >= 1u && flags <= 3u) ++gx_cases_stretch_flipped;
    if (gx_mode == 2) for (uint32_t i = 0; i < get32(gx_p(GX_CONTEXT)); ++i) if (get32(gx_p(GX_ENTRIES + 0x20u * i)) == 2u) { ++gx_cases_stretch_spans; break; }
    snprintf(gx_desc, sizeof(gx_desc), "stretch which %lu w %ld h %ld flags %02x call flags %lx size (%ld,%ld) point (%ld,%ld) mode %d regions %lu origin %lu clip %d",
             (unsigned long)which, (long)w, (long)h, gx_world_start[hdr + 5u], (unsigned long)flags, (long)sx, (long)sy, (long)get32(gx_p(pt)), (long)get32(gx_p(pt + 4u)), gx_mode,
             (unsigned long)get32(gx_p(GX_CONTEXT)), (unsigned long)get32(gx_p(GX_CONTEXT + 0x208u)), with_clip);
    for (unsigned side = 0; side < 2; ++side) {
        gx_side(side);
        uint32_t vh = gx_va(hdr), vo = gx_va(obj), vp = gx_va(pt), vs = gx_va(size), vc = with_clip ? gx_va(clip) : 0;
        if (which == 0) {
            uint32_t args[4] = {vh, vp, flags, vs};
            if (!side) lc_machine(0x00488aa8u, args, 4, NULL); else if (es2_sim_gfx_stretch(&lc_loop, vh, vp, flags, vs)) gx_bad = 1;
        } else if (which == 1u) {
            uint32_t args[4] = {vo, vp, flags, vs};
            if (!side) lc_machine(0x004817c8u, args, 4, NULL); else if (es2_sim_gfx_stretch_blit(&lc_loop, vo, vp, flags, vs)) gx_bad = 1;
        } else if (which == 2u) {
            uint32_t args[5] = {gx_va(GX_SURFACE), vh, vp, flags, vs};
            if (!side) lc_machine(0x00481c3bu, args, 5, NULL); else if (es2_sim_gfx_stretch_on(&lc_loop, args[0], vh, vp, flags, vs)) gx_bad = 1;
        } else {
            uint32_t args[6] = {vh, (uint32_t)sx, (uint32_t)sy, vp, vc, flags};
            if (!side) lc_machine(0x0048afc8u, args, 6, NULL); else if (es2_sim_gfx_slot(&lc_loop, 29u, args, 6u, NULL)) gx_bad = 1;
        }
        gx_end(side);
    }
    return gx_compare("stretch", k, 0, 0, 0);
}
/* 0x488b98, 0x48185c and 0x481c58 over random angles, sizes 1..30 and
 * flags 0..3; sincos_pair, rotate_2d and poly_fills.h's polygon fill 0x483eb8
 * run as the original on both sides (gx_dispatch_setup). */
static int gx_rotate_case(unsigned k) {
    gx_world_reset();
    gx_context(1);
    uint32_t which = gx_r(3u), hdr = GX_BITMAP, obj = GX_BITMAP + 0x40u, pt = GX_RECT + 0x20u, size = GX_RECT + 0x30u;
    int32_t w = gx_ri(1, 16), h = gx_ri(1, 16);
    gx_bitmap_random(w, h);
    int32_t sx = gx_ri(1, 30), sy = gx_ri(1, 30);
    gx_point(size, sx, sy);
    gx_point(pt, gx_ri(-10, GX_W + 4), gx_ri(-10, GX_H + 4));
    uint32_t flags = gx_r(4u), angle = gx_r(0x10000u) | (gx_r(2u) ? gx_r(0x10000u) << 16 : 0);
    ++gx_cases_rotated;
    snprintf(gx_desc, sizeof(gx_desc), "rotate which %lu w %ld h %ld flags %02x call flags %lx size (%ld,%ld) point (%ld,%ld) angle %08lx mode %d regions %lu origin %lu",
             (unsigned long)which, (long)w, (long)h, gx_world_start[hdr + 5u], (unsigned long)flags, (long)sx, (long)sy, (long)get32(gx_p(pt)), (long)get32(gx_p(pt + 4u)),
             (unsigned long)angle, gx_mode, (unsigned long)get32(gx_p(GX_CONTEXT)), (unsigned long)get32(gx_p(GX_CONTEXT + 0x208u)));
    for (unsigned side = 0; side < 2; ++side) {
        gx_side(side);
        uint32_t vh = gx_va(hdr), vo = gx_va(obj), vp = gx_va(pt), vs = gx_va(size);
        if (which == 0) {
            uint32_t args[5] = {vh, vp, flags, vs, angle};
            if (!side) lc_machine(0x00488b98u, args, 5, NULL); else if (es2_sim_gfx_rotate(&lc_loop, vh, vp, flags, vs, angle & 0xffffu)) gx_bad = 1;
        } else if (which == 1u) {
            uint32_t args[5] = {vo, vp, flags, vs, angle};
            if (!side) lc_machine(0x0048185cu, args, 5, NULL); else if (es2_sim_gfx_rotated_blit(&lc_loop, vo, vp, flags, vs, angle)) gx_bad = 1;
        } else {
            uint32_t args[6] = {gx_va(GX_SURFACE), vh, vp, flags, vs, angle};
            if (!side) lc_machine(0x00481c58u, args, 6, NULL); else if (es2_sim_gfx_rotate_on(&lc_loop, args[0], vh, vp, flags, vs, angle)) gx_bad = 1;
        }
        /* Slot 33 (0x48a940, 0x48a955) keeps the corner list and the fill's
         * source corners as [0x6c5f88] and [0x6c5f8c]: addresses of
         * 0x488b98's locals, on the machine stack or the own call area; its
         * span kernel parks ESP and EBP in [0x4a591c] and [0x4a5920]
         * (0x48af6d, 0x48af79), which depend on the call depth. */
        put32(mapped - 0x400000u + 0x6c5f88u, 0);
        put32(mapped - 0x400000u + 0x6c5f8cu, 0);
        put32(mapped - 0x400000u + 0x4a591cu, 0);
        put32(mapped - 0x400000u + 0x4a5920u, 0);
        gx_end(side);
    }
    return gx_compare("rotate", k, 0, 0, 0);
}

/* Prints the running case's parameters before the harness's fault handler
 * reports an access violation (registered first; never handles). */
static LONG CALLBACK gx_fault(EXCEPTION_POINTERS *fault) {
    DWORD code = fault->ExceptionRecord->ExceptionCode;
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_INT_OVERFLOW) && gx_desc[0])
        fprintf(stderr, "  case parameters: %s\n", gx_desc);
    return EXCEPTION_CONTINUE_SEARCH;
}
static int gfx_cases(void) {
    current_function = "gfx";
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
        return fail("gfx: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("gfx: the counter word is not free");
    if (get32(mapped + (0x4a586cu - 0x400000u) + 4u * 26u) != 0x004899acu) return fail("gfx: the BMP_8 driver table is not at 0x4a586c");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)su_calloc}, {"cw3220.dll", "_free", (void *)su_free},
        {"cw3220.dll", "_memset", (void *)ic_memset}, {"cw3220.dll", "_sprintf", (void *)gx_sprintf},
        {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("gfx: import slot not found");
        }
    if (gx_dispatch_setup()) return 1;
    int bad = 0;
    /* The text part runs with ordered region spans (gx_regions_ordered: the
     * fault at 0x489c30 came from a reversed span pair, not from the glyph
     * offsets); the scan part found the horizontal-edge field (0x493335). */
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"clip", gx_clip_case, 3000}, {"fill", gx_fill_case, 3000}, {"bitmap", gx_bitmap_case, 3000},
        {"text", gx_text_case, 1500}, {"scan", gx_scan_case, 3000},
        {"stretch", gx_stretch_case, 3000}, {"rotate", gx_rotate_case, 1500},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            gx_bad = 0;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: gfx %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) printf("GFX coverage: %u kind-4 fills, %u span-list regions, %u grabs, %u transparent bitmaps, %u reversed polygons, %u glyphs\n",
                     gx_cases_kind4, gx_cases_regions, gx_cases_grab, gx_cases_transparent, gx_cases_reversed, gx_cases_text_glyphs);
    if (!bad) printf("GFX coverage (stretch, rotate): %u stretches over span-list regions, %u flipped stretches, %u rotated blits\n",
                     gx_cases_stretch_spans, gx_cases_stretch_flipped, gx_cases_rotated);
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
