/* The cockpit construction of root step 19 (0x429660 and what it reaches) as
 * own code, state yes, pixels no (es2/sim/hud.h). Contract: hud.md, "the HUD
 * line in the own process"; every function below names its listing range of the
 * reference DBSIM.EXE (llvm-objdump -d -M intel, SHA-256 8561aa47...c4ad).
 * Original callees that are already own code here are called in C; the others
 * (hooks, other packages' services, imports, and the constructors not written
 * yet) go through the dispatch, so a missing one is a named refusal. The frame
 * records of 0x49630c are not linked (FS:0 is the same at the return as in the
 * original). The fatal reports of the original (0x479c78 on a missing file) are
 * called as the original calls them (hud_fatal_report), never stepped over. */
#include "hud_internal.h"

#define VA_DISPLAY_OBJECT 0x004cf99cu    /* D: [0x4cf99c] (0x429810) */
#define VA_PANEL 0x0049b088u             /* H: the cockpit panel */
#define VA_COCKPIT_SHAPE 0x0049ac48u
#define VA_SHIFT_X 0x004d253au           /* word: the x scale shift of the game size */
#define VA_SHIFT_Y 0x004d253cu
#define VA_RESOLUTION 0x004d2537u        /* byte: 3 = the high-resolution set */
#define VA_PALETTE_CURRENT 0x0049b020u
#define VA_PALETTE_SET 0x00481fe0u
#define VA_GC_CURRENT 0x006c5f70u

static unsigned shx(const hud_ctx *c) { return hr16(c, VA_SHIFT_X) & 31u; }
static unsigned shy(const hud_ctx *c) { return hr16(c, VA_SHIFT_Y) & 31u; }
static void shl32(const hud_ctx *c, uint32_t a, unsigned s) { hw32(c, a, hr32(c, a) << s); }
static uint16_t shl16(uint16_t v, unsigned s) { return (uint16_t)((uint32_t)v << s); }

/* ======================================================================
 * The palette object pieces the construction uses (presentation; their
 * guest writes are kept, the upload is present.c's).
 *
 * 0x4303c4(first, n, src) (listing 0x4303c4..0x43043e): n dwords from src
 * into the entries [[0x49b020]+8] + 4 first; the dirty range
 * 0x49b028/0x49b02c (first, first + n - 1) widened (set with byte
 * 0x49b030 <- 1 when it was clear). 0x4303b0(p) (0x4303b0..0x4303c2):
 * [0x49b020] <- p, EAX the previous. 0x43048c() (0x43048c..0x4304db): when
 * [0x49b020] differs from [0x49b024], 0x481fe0([0x49b020]) and [0x49b024]
 * <- it; else with a dirty range 0x465cc4([0x49b020]) (the GDI upload of
 * the range; present.c's); then byte 0x49b030, 0x49b02c, 0x49b028 <- 0.
 * EAX 0, EDX 0. */
static void palette_entries(hud_ctx *c, uint32_t first, uint32_t n, uint32_t src) {
    uint32_t e = hr32(c, hr32(c, VA_PALETTE_CURRENT) + 8u) + (first << 2);
    for (int32_t i = 0; i < (int32_t)n; ++i) hw32(c, e + 4u * (uint32_t)i, hr32(c, src + 4u * (uint32_t)i));
    uint32_t last = first + n - 1u;
    if (!hr8(c, 0x0049b030u)) {
        hw32(c, 0x0049b028u, first);
        hw32(c, 0x0049b02cu, last);
        hw8(c, 0x0049b030u, 1);
    } else {
        if ((int32_t)first < (int32_t)hr32(c, 0x0049b028u)) hw32(c, 0x0049b028u, first);
        if ((int32_t)last > (int32_t)hr32(c, 0x0049b02cu)) hw32(c, 0x0049b02cu, last);
    }
}
static uint32_t palette_swap(hud_ctx *c, uint32_t p) {
    uint32_t old = hr32(c, VA_PALETTE_CURRENT);
    hw32(c, VA_PALETTE_CURRENT, p);
    return old;
}
static int palette_flush(hud_ctx *c) {
    uint32_t p = hr32(c, VA_PALETTE_CURRENT);
    if (p != hr32(c, 0x0049b024u)) {
        if (HCALL(c, VA_PALETTE_SET, NULL, p)) return 1;
        hw32(c, 0x0049b024u, hr32(c, VA_PALETTE_CURRENT));
    } else if (hr8(c, 0x0049b030u)) {
        if (HCALL(c, 0x00465cc4u, NULL, hr32(c, VA_PALETTE_CURRENT))) return 1;
    }
    hw8(c, 0x0049b030u, 0);
    hw32(c, 0x0049b02cu, 0);
    hw32(c, 0x0049b028u, 0);
    return 0;
}
void es2_sim_service_palette_entries(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    palette_entries(&c, sc->slots[0], sc->slots[1], sc->slots[2]);
    sc->eax = sc->slots[0] + sc->slots[1] - 1u;
}
void es2_sim_service_palette_swap(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = palette_swap(&c, sc->slots[0]);
    sc->edx = sc->slots[0];
}
void es2_sim_service_palette_flush(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (palette_flush(&c)) { hud_failed(sc); return; }
    sc->eax = 0; sc->edx = 0;
}

/* ======================================================================
 * Regions (the panel's screen areas): type 0 a rectangle {x0, y0, x1, y1}
 * at +4, type 2 a row table at [+4] (word y, word rows, then per row the
 * words x0, x1).
 *
 * 0x488045(o) (listing 0x488045..0x488077): type 0 -> 0x487e94(o+4)
 * (0x487e94..0x487eab: (x1 - x0 + 1) * (y1 - y0 + 1)); type 2 ->
 * 0x487f48([o+4]), which runs into the next function without a return
 * (0x487f68; refused); any other type: EAX the type. */
static int region_area(hud_ctx *c, uint32_t o, uint32_t *area) {
    uint32_t type = hr32(c, o);
    if (type == 0) {
        uint32_t r = o + 4u;
        *area = (hr32(c, r + 8u) - hr32(c, r) + 1u) * (hr32(c, r + 0xcu) - hr32(c, r + 4u) + 1u);
        return 0;
    }
    if (type == 2) return hud_refuse(c, 0x00487f68u, "region area", "the row-table area 0x487f48 falls into the next function");
    *area = type;
    return 0;
}

/* 0x42dcf0(path, R, scale) (listing 0x42dcf0..0x42e00d): the regions of one
 * view file into R (R+0 the count, R+4 the region pointers). scale = two
 * dwords (x shift, y shift). From the VOL stream: n1 rectangles (word),
 * each new(0x18) = o ([o] <- 0x4a69f0, 0x4a3368, counter + 1, 0x4a3390,
 * counter + 1, 0x4a3264, [o+4] <- 0 (type 0), counter + 1, counter - 3),
 * R[k] <- o+4, the words a, b (shifted left by the y shift, b + (1 << s) -
 * 1 when s is not 0) and c, d (the same with the x shift) into o+0xc, o+0x14,
 * o+8, o+0x10; then n2 row tables (word), each new(8) = q, R[k] <- q, the
 * words y, rows (y shifted), [q] <- 2, b = new[](4 rows + 8), words b+2 <-
 * rows, b <- y, [q+4] <- b; the rows: with a y shift s, every 1 << s-th
 * row read (4 bytes) and copied to the next, else 4 rows bytes at once;
 * with an x shift t every row's x0 and x1 shifted by t and x1 + 1. R+0 <-
 * k; stream_destroy(S, 2). A NULL rectangle object (the original writes
 * through it) is refused. */
static int view_regions(hud_ctx *c, uint32_t path, uint32_t r, uint32_t scale) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x78u);
    if (!f) return hud_refuse(c, 0x0042dcf0u, "view regions", "the own code's call area is exhausted");
    f += 0x78u;
    uint32_t s = f - 0x30u, k = 0;
    if (HCALL(c, HUD_VA_VOL_STREAM, NULL, s, path)) return 1;
    if (hud_sread(c, s, 2u, f - 0x5eu, NULL)) return 1;
    for (int32_t j = 0; (int32_t)hsx16(hr16(c, f - 0x5eu)) > j; ++j) {
        uint32_t o = 0;
        if (HCALL(c, HUD_VA_NEW, &o, 0x18u)) return 1;
        if (!o) return hud_null_refuse(c, 0x0042de7cu, "view regions", "operator new returned NULL (the original writes through it)");
        hw32(c, o, 0x004a69f0u);
        hw32(c, o, 0x004a3368u);
        hud_counter(c, 1);
        hw32(c, o, 0x004a3390u);
        hud_counter(c, 1);
        hw32(c, o, 0x004a3264u);
        hw32(c, o + 4u, 0);
        hud_counter(c, 1);
        hud_counter(c, -3);
        hw32(c, r + 4u + 4u * k, o + 4u);
        ++k;
        if (hud_sread(c, s, 2u, f - 0x60u, NULL) || hud_sread(c, s, 2u, f - 0x62u, NULL)) return 1;
        unsigned sy = hr32(c, scale + 4u) & 31u, sx = hr32(c, scale) & 31u;
        hw16(c, f - 0x60u, shl16(hr16(c, f - 0x60u), sy));
        hw16(c, f - 0x62u, shl16(hr16(c, f - 0x62u), sy));
        if (hr32(c, scale + 4u)) hw16(c, f - 0x62u, (uint16_t)(shl16(1u, sy) + hr16(c, f - 0x62u) - 1u));
        if (hud_sread(c, s, 2u, f - 0x64u, NULL) || hud_sread(c, s, 2u, f - 0x66u, NULL)) return 1;
        hw16(c, f - 0x64u, shl16(hr16(c, f - 0x64u), sx));
        hw16(c, f - 0x66u, shl16(hr16(c, f - 0x66u), sx));
        if (hr32(c, scale)) hw16(c, f - 0x66u, (uint16_t)(shl16(1u, sx) + hr16(c, f - 0x66u) - 1u));
        hw32(c, o + 0xcu, hsx16(hr16(c, f - 0x60u)));
        hw32(c, o + 0x14u, hsx16(hr16(c, f - 0x62u)));
        hw32(c, o + 8u, hsx16(hr16(c, f - 0x64u)));
        hw32(c, o + 0x10u, hsx16(hr16(c, f - 0x66u)));
    }
    if (hud_sread(c, s, 2u, f - 0x5eu, NULL)) return 1;
    for (int32_t j = 0; (int32_t)hsx16(hr16(c, f - 0x5eu)) > j; ++j) {
        uint32_t q = 0;
        if (HCALL(c, HUD_VA_NEW, &q, 8u)) return 1;
        if (!q) return hud_null_refuse(c, 0x0042df17u, "view regions", "operator new returned NULL (the original writes through it)");
        hw32(c, r + 4u + 4u * k, q);
        ++k;
        if (hud_sread(c, s, 2u, f - 0x6eu, NULL) || hud_sread(c, s, 2u, f - 0x70u, NULL)) return 1;
        unsigned sy = hr32(c, scale + 4u) & 31u;
        hw16(c, f - 0x6eu, shl16(hr16(c, f - 0x6eu), sy));
        hw16(c, f - 0x70u, shl16(hr16(c, f - 0x70u), sy));
        hw32(c, q, 2);
        int32_t rows = (int32_t)hsx16(hr16(c, f - 0x70u));
        uint32_t b = 0;
        if (HCALL(c, HUD_VA_NEW_ARRAY, &b, (uint32_t)rows * 4u + 8u)) return 1;
        hw16(c, b + 2u, hr16(c, f - 0x70u));
        hw16(c, b, hr16(c, f - 0x6eu));
        hw32(c, q + 4u, b);
        uint32_t e = b + 4u;
        if (hr32(c, scale + 4u)) {
            for (int32_t y = 0; y < rows;) {
                if (hud_sread(c, s, 4u, e, NULL)) return 1;
                hw32(c, e + 4u, hr32(c, e));
                unsigned t = hr32(c, scale + 4u) & 31u;
                y += (int32_t)(1u << t);
                e += 4u << t;
            }
        } else if (hud_sread(c, s, (uint32_t)rows << 2, e, NULL)) {
            return 1;
        }
        if (hr32(c, scale)) {
            e = b + 4u;
            for (int32_t y = 0; y < rows; ++y, e += 4u) {
                unsigned t = hr32(c, scale) & 31u;
                hw16(c, e, shl16(hr16(c, e), t));
                hw16(c, e + 2u, shl16(hr16(c, e + 2u), t));
                hw16(c, e + 2u, (uint16_t)(hr16(c, e + 2u) + 1u));
            }
        }
    }
    hw32(c, r, k);
    if (HCALL(c, HUD_VA_STREAM_DESTROY, NULL, s, 2u)) return 1;
    hud_frame_pop(c, top);
    return 0;
}

/* 0x48bab4(out, R, h) (listing 0x48bab4..0x48bc3d): the span lists of R's
 * regions for the rows 0 .. h-1. out+4 <- h, out+0 <- new[](8h); per row y
 * the spans of the regions that cover it into a local table of 12 (row
 * table: x0 and the length x1 - x0 + 1 when positive; rectangle: x0 and
 * x1, as stored), sorted by x (exchange sort, the original's order of
 * compares); the row's entry {count, new[](8 count) with the spans}.
 * [0x4a5af8] <- out. EAX out. More than 12 spans in a row (the original
 * overwrites its other locals) is refused. */
static int span_lists(hud_ctx *c, uint32_t out, uint32_t r, uint32_t h) {
    hw32(c, out + 4u, h);
    uint32_t rows = 0;
    if (HCALL(c, HUD_VA_NEW_ARRAY, &rows, h << 3)) return 1;
    hw32(c, out, rows);
    uint32_t entry = rows;
    for (int32_t y = 0; (int32_t)hr32(c, out + 4u) > y; ++y, entry += 8u) {
        uint32_t span[12][2];
        int32_t n = 0;
        for (int32_t k = 0; (int32_t)hr32(c, r) > k; ++k) {
            uint32_t g = hr32(c, r + 4u + 4u * (uint32_t)k);
            if (hr32(c, g) == 2) {
                uint32_t t = hr32(c, g + 4u);
                int32_t y0 = (int32_t)hsx16(hr16(c, t));
                if (y0 > y || y0 + (int32_t)hsx16(hr16(c, t + 2u)) <= y) continue;
                uint32_t p = t + 4u + 4u * (uint32_t)(y - y0);
                int32_t x0 = (int32_t)hsx16(hr16(c, p)), len = (int32_t)hsx16(hr16(c, p + 2u)) - x0 + 1;
                if (len <= 0) continue;
                if (n == 12) return hud_refuse(c, 0x0048bb3fu, "span lists", "more than 12 spans in a row (the original overwrites its locals)");
                span[n][0] = (uint32_t)x0;
                span[n][1] = (uint32_t)len;
                ++n;
            } else if (hr32(c, g) == 0) {
                uint32_t q = g + 4u;
                if ((int32_t)hr32(c, q + 4u) > y || (int32_t)hr32(c, q + 0xcu) < y) continue;
                if (n == 12) return hud_refuse(c, 0x0048bb66u, "span lists", "more than 12 spans in a row (the original overwrites its locals)");
                span[n][0] = hr32(c, q);
                span[n][1] = hr32(c, q + 8u);
                ++n;
            }
        }
        for (int32_t i = 0; n - 1 > i; ++i)
            for (int32_t j = i + 1; j < n; ++j)
                if ((int32_t)span[j][0] < (int32_t)span[i][0]) {
                    uint32_t a = span[j][0], b = span[j][1];
                    span[j][0] = span[i][0]; span[j][1] = span[i][1];
                    span[i][0] = a; span[i][1] = b;
                }
        hw32(c, entry, (uint32_t)n);
        if (n) {
            uint32_t b = 0;
            if (HCALL(c, HUD_VA_NEW_ARRAY, &b, (uint32_t)n << 3)) return 1;
            hw32(c, entry + 4u, b);
            for (int32_t i = 0; i < n; ++i) {
                hw32(c, b + 8u * (uint32_t)i, span[i][0]);
                hw32(c, b + 8u * (uint32_t)i + 4u, span[i][1]);
            }
        }
    }
    hw32(c, 0x004a5af8u, out);
    return 0;
}

/* 0x429c2c(D, i) (listing 0x429c2c..0x429de2): the view shape of mode i
 * unless i is 4 or [D+8][i] is set. The extension [0x4a0a28] (byte
 * [0x4d2537] = 3) or [0x4a0a10] copied to f-54 and digit di = i (i < 3)
 * or i - 1 written at f-52 (_itoa base 10); the VOL stream of
 * 0x492bec(D+0x2d, f-54) (an error: the fatal report 0x429d14 with the
 * path built again by 0x492bec(D+0x2d, f-54)); [D+8][i] <-
 * dts_load(S, 0); with byte [0x4d2538] = 1 and i = 0 the extension's third
 * byte + 1 ([0x4a0a10]+2, the global string), the stream reopened on the
 * same path (stream_open) and [D+8][i+1] <- dts_load(S, 0); stream_destroy
 * (S, 2); di = 2: [D+8][i+1] <- [D+8][i]. */
static int view_shape(hud_ctx *c, uint32_t d, int32_t i, es2_sim_result *out) {
    if (i == 4 || hr32(c, hr32(c, d + 8u) + 4u * (uint32_t)i)) {
        if (out) { out->eax = out->ecx = hud_fs0(); if (i != 4) out->edx = hr32(c, d + 8u); }
        return 0;
    }
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x54u);
    if (!f) return hud_refuse(c, 0x00429c2cu, "view shape", "the own code's call area is exhausted");
    f += 0x54u;
    uint32_t ext = (int8_t)hr8(c, VA_RESOLUTION) == 3 ? hr32(c, 0x004a0a28u) : hr32(c, 0x004a0a10u);
    hud_gstrcpy(c, f - 0x54u, ext);
    int32_t di = i < 3 ? i : i - 1;
    if (HCALL(c, HUD_VA_ITOA, NULL, hsx16((uint16_t)di), f - 0x52u, 10u)) return 1;
    uint32_t s = f - 0x30u, path = 0, x = 0;
    if (HCALL(c, HUD_VA_PATH_DIR, &path, d + 0x2du, f - 0x54u)) return 1;
    if (HCALL(c, HUD_VA_VOL_STREAM, NULL, s, path)) return 1;
    if (hr32(c, f - 0x2cu) && (HCALL(c, HUD_VA_PATH_DIR, &path, d + 0x2du, f - 0x54u) ||
                               hud_fatal_report(c, 0, 0x0049ae7au, 0x12eu, path)))   /* 0x429d14 */
        return 1;
    if (HCALL(c, HUD_VA_DTS_LOAD, &x, s, 0u)) return 1;
    hw32(c, hr32(c, d + 8u) + 4u * (uint32_t)i, x);
    if ((int8_t)hr8(c, 0x004d2538u) == 1 && i == 0) {
        uint32_t g = hr32(c, 0x004a0a10u) + 2u;
        hw8(c, g, (uint8_t)(hr8(c, g) + 1u));
        if (HCALL(c, HUD_VA_PATH_DIR, &path, d + 0x2du, f - 0x54u)) return 1;
        if (HCALL(c, HUD_VA_STREAM_OPEN, NULL, s, path)) return 1;
        if (hr32(c, f - 0x2cu) && (HCALL(c, HUD_VA_PATH_DIR, &path, d + 0x2du, f - 0x54u) ||
                                   hud_fatal_report(c, 0, 0x0049ae87u, 0x135u, path)))   /* 0x429d97 */
            return 1;
        if (HCALL(c, HUD_VA_DTS_LOAD, &x, s, 0u)) return 1;
        hw32(c, hr32(c, d + 8u) + 4u * (uint32_t)i + 4u, x);
    }
    es2_sim_result r;
    if (hud_call_regs(c, HUD_VA_STREAM_DESTROY, (const uint32_t[]){s, 2u}, 2, NULL, &r)) return 1;
    if (di == 2) {
        r.eax = hr32(c, d + 8u);
        r.edx = hr32(c, r.eax + 4u * (uint32_t)i);
        hw32(c, r.eax + 4u * (uint32_t)i + 4u, r.edx);
    }
    hud_frame_pop(c, top);
    if (out) { out->eax = r.eax; out->edx = r.edx; out->ecx = hud_fs0(); }
    return 0;
}

/* 0x437598(path) (listing 0x437598..0x437895): strings_load(0x4d1298,
 * path) and the 41 groups strings_group(0x4d1298, table, n, extra) in the
 * listing's order (one table per HUD text set). */
static int hud_strings(hud_ctx *c, uint32_t path) {
    static const struct { uint32_t table; uint8_t n; uint32_t extra; } groups[] = {
        {0x004d12a8u, 0x12, 0x004d12f0u}, {0x004d1338u, 3, 0}, {0x004d1344u, 1, 0}, {0x004d1348u, 1, 0},
        {0x004d134cu, 4, 0}, {0x004d135cu, 0xd, 0}, {0x004d1390u, 4, 0}, {0x004d13a0u, 3, 0}, {0x004d13acu, 1, 0},
        {0x004d13b0u, 3, 0}, {0x004d13bcu, 5, 0}, {0x004d13d0u, 2, 0}, {0x004d13d8u, 3, 0}, {0x004d13e4u, 0x13, 0},
        {0x004d1430u, 0x13, 0}, {0x004d147cu, 0xc, 0}, {0x004d14acu, 0xc, 0}, {0x004d14dcu, 1, 0}, {0x004d14e0u, 1, 0},
        {0x004d14e4u, 2, 0}, {0x004d14ecu, 3, 0}, {0x004d14f8u, 1, 0}, {0x004d14fcu, 0x15, 0}, {0x004d1550u, 0x28, 0},
        {0x004d15f0u, 5, 0}, {0x004d1604u, 2, 0}, {0x004d160cu, 1, 0}, {0x004d1610u, 1, 0}, {0x004d1614u, 5, 0},
        {0x004d1628u, 2, 0}, {0x004d1630u, 1, 0}, {0x004d1634u, 1, 0}, {0x004d1638u, 4, 0}, {0x004d1648u, 2, 0},
        {0x004d1660u, 1, 0}, {0x004d1664u, 0xf, 0}, {0x004d16a0u, 4, 0}, {0x004d1650u, 2, 0}, {0x004d1658u, 1, 0},
        {0x004d165cu, 1, 0}, {0x004d16b0u, 8, 0},
    };
    const uint32_t set = 0x004d1298u;
    if (HCALL(c, 0x00453878u, NULL, set, path)) return 1;
    for (unsigned i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i)
        if (HCALL(c, 0x004539a0u, NULL, set, groups[i].table, groups[i].n, groups[i].extra)) return 1;
    return 0;
}

/* 0x45ef1c(name, ext) (listing 0x45ef1c..0x45ef81): byte 0x49f257 <- byte
 * [0x4d2536] (the language letter in the template "...\X\"), b =
 * 0x492b90(name, ext); the template 0x49f255 copied to 0x4d291c,
 * _strcat(it, 0x49f3fc), _strcat(it, b). EAX 0x4d291c. */
static int language_path(hud_ctx *c, uint32_t name, uint32_t ext, uint32_t *out) {
    hw8(c, 0x0049f257u, hr8(c, 0x004d2536u));
    uint32_t b = 0, r = 0;
    if (HCALL(c, HUD_VA_PATH_EXT, &b, name, ext)) return 1;
    hud_gstrcpy(c, 0x004d291cu, 0x0049f255u);
    if (HCALL(c, HUD_VA_STRCAT, &r, 0x004d291cu, 0x0049f3fcu)) return 1;
    if (HCALL(c, HUD_VA_STRCAT, &r, r, b)) return 1;
    *out = r;
    return 0;
}

/* 0x431098(H) (listing 0x431098..0x4311dc): the panel's texts and shared
 * shapes. ext = "hfn" (byte [0x4d2537] = 3) or "dfn"; 0x437598(0x45ef1c(
 * "strings0", "str")); when [0x49b0ac] is 0 the 18 shapes [0x49b0ac + 4k]
 * <- 0x430f58(0x492bec([0x49b108 + 4k], ext)); when [0x49b08c] is 0 the 7
 * shapes [0x49b08c + 4k] <- 0x430fb0(0x492bec([0x49b150 + 4k], [0x4a0a14]))
 * and H+0x236 <- [0x49b090], H+0x23a <- [0x49b08c]; 0x4d03c8..0x4d03d0 <-
 * 0; _memset(H+0xa20, 0x284, 0) (pushed 0, 0x284, H+0xa20 at 0x431183:
 * a zero length, so nothing is written past the 0x288-byte panel object;
 * the call is kept); [0x49b088] <- H when 0; byte
 * H+0x20f <- 0, H+0x226 <- 0, word H+0x23f <- -1, byte H+0x245 <- 0,
 * H+0x241 <- 0, byte H+0x26a <- 0. */
static int panel_texts(hud_ctx *c, uint32_t h) {
    uint32_t ext = (int8_t)hr8(c, VA_RESOLUTION) == 3 ? 0x0049b73fu : 0x0049b743u, path = 0;
    if (language_path(c, 0x0049b747u, 0x0049b750u, &path) || hud_strings(c, path)) return 1;
    if (!hr32(c, 0x0049b0acu))
        for (uint32_t k = 0; k < 0x12u; ++k)
            if (HCALL(c, HUD_VA_PATH_DIR, &path, hr32(c, 0x0049b108u + 4u * k), ext) || hud_shape_one(c, 0x0049b0acu + 4u * k, path)) return 1;
    if (!hr32(c, 0x0049b08cu)) {
        for (uint32_t k = 0; k < 7u; ++k)
            if (HCALL(c, HUD_VA_PATH_DIR, &path, hr32(c, 0x0049b150u + 4u * k), hr32(c, 0x004a0a14u)) ||
                hud_shape_one(c, 0x0049b08cu + 4u * k, path)) return 1;
        hw32(c, h + 0x236u, hr32(c, 0x0049b090u));
        hw32(c, h + 0x23au, hr32(c, 0x0049b08cu));
    }
    for (uint32_t k = 0; k < 3u; ++k) hw32(c, 0x004d03c8u + 4u * k, 0);
    if (HCALL(c, HUD_VA_MEMSET, NULL, h + 0xa20u, 0x284u, 0u)) return 1;
    if (!hr32(c, VA_PANEL)) hw32(c, VA_PANEL, h);
    hw8(c, h + 0x20fu, 0);
    hw32(c, h + 0x226u, 0);
    hw16(c, h + 0x23fu, 0xffffu);
    hw8(c, h + 0x245u, 0);
    hw32(c, h + 0x241u, 0);
    hw8(c, h + 0x26au, 0);
    return 0;
}

/* 0x431008(H, P) (listing 0x431008..0x431094): word H+0x218 <- 0, H+0x214
 * <- [0x4a9af8], H+0x203 <- P, word H+0x201 <- word [[P+0x1f2]+0x6c];
 * 0x431098(H); [0x4d040c] <- new[](0x10); [0x4d0410] <- b = new[](0x30)
 * and per entry e of its 4 (12 bytes): e+4 <- new[](0x10), e+8 <-
 * new[](0x10), e+0 <- 0. EAX H. */
static int panel_object(hud_ctx *c, uint32_t h, uint32_t p) {
    hw16(c, h + 0x218u, 0);
    hw32(c, h + 0x214u, hr32(c, 0x004a9af8u));
    hw32(c, h + 0x203u, p);
    hw16(c, h + 0x201u, hr16(c, hr32(c, p + 0x1f2u) + 0x6cu));
    if (panel_texts(c, h)) return 1;
    uint32_t b = 0;
    if (HCALL(c, HUD_VA_NEW_ARRAY, &b, 0x10u)) return 1;
    hw32(c, 0x004d040cu, b);
    if (HCALL(c, HUD_VA_NEW_ARRAY, &b, 0x30u)) return 1;
    hw32(c, 0x004d0410u, b);
    for (uint32_t i = 0; i < 4u; ++i, b += 0xcu) {
        uint32_t x = 0;
        if (HCALL(c, HUD_VA_NEW_ARRAY, &x, 0x10u)) return 1;
        hw32(c, b + 4u, x);
        if (HCALL(c, HUD_VA_NEW_ARRAY, &x, 0x10u)) return 1;
        hw32(c, b + 8u, x);
        hw32(c, b, 0);
    }
    return 0;
}

/* ======================================================================
 * The panel constructor 0x431bf8(H, name) and its direct pieces. */

/* 0x431778(path) (listing 0x431778..0x431bf6): the panel layout L: the VOL
 * stream (an error: the fatal report 0x4317c6), L = new(0x6a4) with its element
 * arrays (0x49667c(L+0x14, 0x10, 10, 1, 0x434ab9), (L+0x184, 0x10, 3, 1,
 * 0x48de2f), (L+0x1e4, .., 4, ..), (L+0x2e8, .., 13, ..), (L+0x50c, ..,
 * 15, ..), (L+0x5fc, .., 3, ..); the other stores of 0x4317e4..0x431a15 are
 * locals), stream_read(S, 0x6a4, L) (the whole object from the file); a
 * nonzero offset (dx, dy) = (L+0, L+4) added to both corners of the L+0x10
 * rectangles at L+0x14 (16 bytes each) and to the first rectangle of
 * L+0x1d4, 0x224, 0x268, 0x2d8, 0x3e8, 0x440 and 0x4bc; stream_destroy(S,
 * 2). EAX L. A NULL L (the original reads into it) is refused. */
static int panel_layout(hud_ctx *c, uint32_t path, uint32_t *out) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0xe0u);
    if (!f) return hud_refuse(c, 0x00431778u, "panel layout", "the own code's call area is exhausted");
    f += 0xe0u;
    uint32_t s = f - 0x30u, l = 0;
    if (HCALL(c, HUD_VA_VOL_STREAM, NULL, s, path)) return 1;
    if (hr32(c, f - 0x2cu) && hud_fatal_report(c, 0, 0x0049b754u, 0x1d6u, path)) return 1;   /* 0x4317c6 */
    if (HCALL(c, HUD_VA_NEW, &l, 0x6a4u)) return 1;
    if (!l) return hud_null_refuse(c, 0x00431a2fu, "panel layout", "operator new returned NULL (the original reads into it)");
    if (HCALL(c, HUD_VA_VECTOR_NEW, NULL, l + 0x14u, 0x10u, 0xau, 1u, 0x00434ab9u)) return 1;
    static const struct { uint16_t off; uint8_t n; } arrays[] = {{0x184, 3}, {0x1e4, 4}, {0x2e8, 0xd}, {0x50c, 0xf}, {0x5fc, 3}};
    for (unsigned i = 0; i < sizeof(arrays) / sizeof(arrays[0]); ++i)
        if (HCALL(c, HUD_VA_VECTOR_NEW, NULL, l + arrays[i].off, 0x10u, arrays[i].n, 1u, 0x0048de2fu)) return 1;
    if (HCALL(c, HUD_VA_STREAM_READ, NULL, s, 0x6a4u, l)) return 1;
    uint32_t dx = hr32(c, l), dy = hr32(c, l + 4u);
    if (dx || dy) {
        uint32_t e = l + 0x14u;
        for (int32_t i = 0; (int32_t)hr32(c, l + 0x10u) > i; ++i, e += 0x10u)
            for (uint32_t k = 0; k < 2u; ++k) {
                hw32(c, e + 8u * k, hr32(c, e + 8u * k) + dx);
                hw32(c, e + 8u * k + 4u, hr32(c, e + 8u * k + 4u) + dy);
            }
        static const uint16_t singles[] = {0x1d4, 0x224, 0x268, 0x2d8, 0x3e8, 0x440, 0x4bc};
        for (unsigned i = 0; i < sizeof(singles) / sizeof(singles[0]); ++i)
            for (uint32_t k = 0; k < 2u; ++k) {
                uint32_t a = l + singles[i] + 8u * k;
                hw32(c, a, hr32(c, a) + dx);
                hw32(c, a + 4u, hr32(c, a + 4u) + dy);
            }
    }
    if (HCALL(c, HUD_VA_STREAM_DESTROY, NULL, s, 2u)) return 1;
    hud_frame_pop(c, top);
    *out = l;
    return 0;
}

/* 0x430ea0(rect) (listing 0x430ea0..0x430f54): the panel canvas context:
 * new(0x239) = O (NULL refused: the original writes through it), 0x480c34(O),
 * counter - 2; [0x4d03a8] <- O; O+0x208 <- 1, O+0x20c <- 1, the 16 bytes of
 * rect to O+0x210; byte O+0x230 <- 0, O+0x231 <- 0, O+0x235 <- 0xc8. EAX O. */
static int panel_canvas(hud_ctx *c, uint32_t rect, uint32_t *out) {
    uint32_t o = 0;
    if (HCALL(c, HUD_VA_NEW, &o, 0x239u)) return 1;
    if (!o) return hud_null_refuse(c, 0x00430ef4u, "panel canvas", "operator new returned NULL (the original writes through it)");
    if (hud_gc_construct(c, o)) return 1;
    hud_counter(c, -2);
    hw32(c, 0x004d03a8u, o);
    hw32(c, o + 0x208u, 1);
    hw32(c, o + 0x20cu, 1);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, o + 0x210u + i, hr32(c, rect + i));
    hw8(c, o + 0x230u, 0);
    hw32(c, o + 0x231u, 0);
    hw32(c, o + 0x235u, 0xc8u);
    *out = o;
    return 0;
}

/* 0x4526ec(W, rect) (listing 0x4526ec..0x45275a): the widget root: byte
 * W+0x10 <- 0, W+0x17 <- 0x49db7a (its method table), 0x480c34(W+0x1b),
 * the 16 bytes of rect to W+0 and W+0x22b, W+0x223 <- 1, word W+0x254 <-
 * 0, counter + 1. EAX W. */
static int widget_root(hud_ctx *c, uint32_t w, uint32_t rect) {
    hw8(c, w + 0x10u, 0);
    hw32(c, w + 0x17u, 0x0049db7au);
    if (hud_gc_construct(c, w + 0x1bu)) return 1;
    for (uint32_t i = 0; i < 16u; i += 4u) {
        uint32_t v = hr32(c, rect + i);
        hw32(c, w + i, v);
        hw32(c, w + 0x22bu + i, v);
    }
    hw32(c, w + 0x223u, 1);
    hw16(c, w + 0x254u, 0);
    hud_counter(c, 1);
    return 0;
}

/* 0x431bf8(H, name) (listing 0x431bf8..0x432017). */
#define VA_LAYOUT 0x0049b0a8u
static int shift_rect(hud_ctx *c, uint32_t a) {
    shl32(c, a, shx(c));
    shl32(c, a + 4u, shy(c));
    shl32(c, a + 8u, shx(c));
    shl32(c, a + 12u, shy(c));
    return 0;
}
static int child(hud_ctx *c, uint32_t va, uint32_t h, uint32_t store) {
    uint32_t x = 0;
    if (HCALL(c, va, &x, h, hr32(c, VA_LAYOUT))) return 1;
    hw32(c, h + store, x);
    return 0;
}
/* 0x438ddc(G) of the element in `slot`, then byte +0x8d <- 1 of the slot
 * re-read after the call (0x431fa7, 0x431fc6, ..). EAX: 0x438ddc's. */
static int power_on(hud_ctx *c, uint32_t slot, uint32_t *eax) {
    if (HCALL(c, 0x00438ddcu, eax, hr32(c, slot))) return 1;
    hw8(c, hr32(c, slot) + 0x8du, 1);
    return 0;
}
/* EAX: the last 0x438ddc's with the power-on sweep, else the word's
 * container [[[0x49b088]+0x203]+0x1f2] (0x431f81). */
static int panel_ctor(hud_ctx *c, uint32_t h, uint32_t name, uint32_t *eax) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x40u);
    if (!f) return hud_refuse(c, 0x00431bf8u, "panel", "the own code's call area is exhausted");
    f += 0x40u;
    uint32_t path = 0, l = 0, o = 0;
    if (HCALL(c, HUD_VA_PATH_DIR, &path, name, hr32(c, 0x004a0a1cu)) || panel_layout(c, path, &l)) return 1;
    hw32(c, VA_LAYOUT, l);
    hw32(c, f - 0x3cu, 0);
    hw32(c, f - 0x40u, 0);
    hw32(c, f - 0x38u, hr32(c, 0x004d254eu) - 1u);
    hw32(c, f - 0x34u, hr32(c, 0x004d2552u) - 1u);
    if (panel_canvas(c, f - 0x40u, &o)) return 1;
    hw32(c, h + 4u, o);
    uint32_t w = 0;
    if (HCALL(c, HUD_VA_NEW, &w, 0x398u)) return 1;
    if (w) {
        if (widget_root(c, w, hr32(c, VA_LAYOUT))) return 1;
        hud_counter(c, -3);
    }
    hw32(c, h, w);
    hw32(c, 0x0049dbd8u, w);       /* 0x452bbc(W) */
    hw16(c, 0x0049dbdcu, 0xffffu);
    if (HCALL(c, 0x00434368u, NULL, h)) return 1;
    shift_rect(c, hr32(c, VA_LAYOUT) + 0x684u);
    /* The third slot of 0x435970 is EAX with the byte [0x4aa728] in AL
     * (mov al; push eax): the high bytes are the last constructor's EAX. */
    uint32_t port = 0, last = 0;
    if (hr16(c, 0x004aa728u)) {
        unsigned s = shy(c);
        uint32_t a = hr32(c, VA_LAYOUT), d = hr32(c, a + 0x680u) << s;
        hw32(c, a + 0x688u, hr32(c, a + 0x688u) - d);
        hw32(c, hr32(c, VA_LAYOUT) + 0x690u, hr32(c, hr32(c, VA_LAYOUT) + 0x690u) - d);
        if (HCALL(c, HUD_VA_NEW, &port, 0x4efu)) return 1;
        if (port) {
            if (HCALL(c, 0x00436244u, &last, port, hr32(c, VA_LAYOUT) + 0x684u)) return 1;
            hud_counter(c, -6);
        }
    } else {
        if (HCALL(c, HUD_VA_NEW, &port, 0x4dfu)) return 1;
        if (port) {
            if (HCALL(c, 0x00435bf0u, &last, port, hr32(c, VA_LAYOUT) + 0x684u)) return 1;
            hud_counter(c, -5);
        }
    }
    hw32(c, h + 0x207u, port);
    if (HCALL(c, 0x00435970u, NULL, hr32(c, h + 0x207u), 1u, (last & 0xffffff00u) | hr8(c, 0x004aa728u), 0u)) return 1;
    shift_rect(c, hr32(c, VA_LAYOUT) + 0x694u);
    if (HCALL(c, HUD_VA_NEW, &port, 0x4eau)) return 1;
    if (port) {
        if (HCALL(c, 0x004369a4u, NULL, port, hr32(c, VA_LAYOUT) + 0x694u)) return 1;
        hud_counter(c, -5);
    }
    hw32(c, h + 0x20bu, port);
    if (HCALL(c, 0x00435970u, NULL, port, 0u, 0u, 0u)) return 1;
    if (child(c, 0x00432340u, h, 0x1e1u) || child(c, 0x004323e0u, h, 0x1e5u) || child(c, 0x00432454u, h, 0x1e9u) ||
        child(c, 0x004324c8u, h, 0x1edu) || child(c, 0x0043254cu, h, 0x1f1u) || child(c, 0x004325c0u, h, 0x1f5u) ||
        child(c, 0x00432634u, h, 0x1f9u))
        return 1;
    if (HCALL(c, 0x00442ea0u, NULL, hr32(c, VA_LAYOUT) + 0x104u) || HCALL(c, 0x00442e60u, NULL, hr32(c, VA_LAYOUT) + 0xb4u) ||
        HCALL(c, 0x00442ef4u, NULL, hr32(c, VA_LAYOUT) + 0x164u))
        return 1;
    uint32_t e = hr32(c, VA_LAYOUT) + 0x14u;
    for (int32_t i = 0; (int32_t)hr32(c, hr32(c, VA_LAYOUT) + 0x10u) > i; ++i, e += 0x10u) shift_rect(c, e);
    if (HCALL(c, 0x00410644u, NULL, hr32(c, hr32(c, h + 0x203u) + 0x202u))) return 1;
    uint32_t panel = hr32(c, VA_PANEL), rec = hr32(c, hr32(c, panel + 0x203u) + 0x1f2u);
    if (eax) *eax = rec;
    if (hr16(c, rec + 0x50u)) {
        hw8(c, h + 0x245u, 1);
        for (uint32_t i = 0; i < 10u; ++i)
            if (hr32(c, h + 0x70u + 4u * i) && power_on(c, h + 0x70u + 4u * i, eax)) return 1;
        if (power_on(c, h + 0x1e9u, eax) || power_on(c, h + 0x1f5u, eax) || power_on(c, h + 0x1edu, eax)) return 1;
    }
    hud_frame_pop(c, top);
    return 0;
}
/* 0x431bf8 as a row of its own (the in-vitro part "panel" of --cockpit-rest
 * calls it directly); 0x429834 calls it through the dispatch too, so the
 * --hud-cockpit display part can stand it in. */
void es2_sim_service_panel_ctor(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (panel_ctor(&c, sc->slots[0], sc->slots[1], &sc->eax)) hud_failed(sc);
}

/* ======================================================================
 * 0x429834(D, name) (listing 0x429834..0x429c29): the display object's
 * views and the panel.
 *
 * Byte 0x49aca4 = 0: 0x49ac9c, 0x49aca0 <- 0, the byte + 1. The VOL stream
 * of 0x492bec(name, "vue"); D+4 <- 0x4cf9a8; S.read(4, D) (the view
 * count); stream_read(S, 32 [D], [D+4]) and every view's four corner pairs
 * shifted by the scale shifts; D+8 <- new[](0x10), D+0xc <- new[](0x810),
 * D+0x10 <- new[](0x10); the scale: 0x49ac9c (zeros) with the extension
 * [0x4a0a24] for byte [0x4d2537] = 3, else the shift words (f-68, f-64)
 * with [0x4a0a0c]. Per view i: the digit i at f-6a after the extension
 * (_itoa), 0x42dcf0(0x492bec(name, f-6c), [D+0xc] + 0x204 i, scale), the
 * span lists new(8) -> 0x48bab4(it, that, 0xf0 << y shift) into
 * [D+0x10][i], [D+8][i] <- 0, and when word [0x4d2680] is 0 the view shape
 * 0x429c2c(D, i). The panel: new(0x288) -> 0x431008(it, [D+0x29]) into
 * D+0x1f; [0x4cf99c] <- D (0x429810); 0x431bf8([D+0x1f], name). The
 * damage colours: k = (word [[[D+0x29]+0x1f2]+0x52] * 24 + 0x20) * 4;
 * 0x4303c4(0x2a, 0x18, [[0x49ac48]+8] + k); the cockpit shape destroyed
 * (0x496406(it, 0) into the counter, its slot +0x18 with 3); [0x49ac48] <-
 * 0x474cb0(0x492bec("impactcp", [0x4a09fc])); its colours the same way
 * into the palette [0x49aef8] (0x4303b0 around); that shape destroyed too
 * ([0x49ac48] keeps the pointer: an original dangling pointer, kept);
 * 0x43048c; 0x4cfa98 <- 1, 0x4cfa9c <- 0x4cfca0, 0x4cfca4..0x4cfcb0 <-
 * 0x4cfa40..0x4cfa4c; stream_destroy(S, 2). */
static int destroy_shape(hud_ctx *c, uint32_t s) {
    if (!s) return 0;
    uint32_t n = 0;
    if (HCALL(c, HUD_VA_RTTI_COUNT, &n, s, 0u)) return 1;
    hud_counter(c, (int32_t)n);
    return HVCALL(c, s, 0x18u, NULL, s, 3u);
}
static int display_views(hud_ctx *c, uint32_t d, uint32_t name) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x6cu);
    if (!f) return hud_refuse(c, 0x00429834u, "display views", "the own code's call area is exhausted");
    f += 0x6cu;
    if (!hr8(c, 0x0049aca4u)) {
        hw32(c, 0x0049ac9cu, 0);
        hw32(c, 0x0049aca0u, 0);
        hw8(c, 0x0049aca4u, (uint8_t)(hr8(c, 0x0049aca4u) + 1u));
    }
    uint32_t s = f - 0x30u, path = 0;
    if (HCALL(c, HUD_VA_PATH_DIR, &path, name, 0x0049ac98u) || HCALL(c, HUD_VA_VOL_STREAM, NULL, s, path)) return 1;
    hw32(c, d + 4u, 0x004cf9a8u);
    if (hud_sread(c, s, 4u, d, NULL)) return 1;
    if (HCALL(c, HUD_VA_STREAM_READ, NULL, s, hr32(c, d) << 5, hr32(c, d + 4u))) return 1;
    uint32_t v = hr32(c, d + 4u);
    for (int32_t i = 0; (int32_t)hr32(c, d) > i; ++i, v += 0x20u)
        for (uint32_t k = 0; k < 4u; ++k) {
            shl32(c, v + 8u * k, shx(c));
            shl32(c, v + 8u * k + 4u, shy(c));
        }
    uint32_t b = 0;
    if (HCALL(c, HUD_VA_NEW_ARRAY, &b, 0x10u)) return 1;
    hw32(c, d + 8u, b);
    if (HCALL(c, HUD_VA_NEW_ARRAY, &b, 0x810u)) return 1;
    hw32(c, d + 0xcu, b);
    if (HCALL(c, HUD_VA_NEW_ARRAY, &b, 0x10u)) return 1;
    hw32(c, d + 0x10u, b);
    hw32(c, f - 0x68u, hsx16(hr16(c, VA_SHIFT_X)));
    hw32(c, f - 0x64u, hsx16(hr16(c, VA_SHIFT_Y)));
    uint32_t scale;
    if ((int8_t)hr8(c, VA_RESOLUTION) == 3) {
        hud_gstrcpy(c, f - 0x6cu, hr32(c, 0x004a0a24u));
        scale = 0x0049ac9cu;
    } else {
        hud_gstrcpy(c, f - 0x6cu, hr32(c, 0x004a0a0cu));
        scale = f - 0x68u;
    }
    for (int32_t i = 0; (int32_t)hr32(c, d) > i; ++i) {
        uint32_t region = hr32(c, d + 0xcu) + 0x204u * (uint32_t)i, o = 0;
        if (HCALL(c, HUD_VA_ITOA, NULL, (uint32_t)i, f - 0x6au, 10u)) return 1;
        if (HCALL(c, HUD_VA_PATH_DIR, &path, name, f - 0x6cu) || view_regions(c, path, region, scale)) return 1;
        if (HCALL(c, HUD_VA_NEW, &o, 8u)) return 1;
        if (o && span_lists(c, o, hr32(c, d + 0xcu) + 0x204u * (uint32_t)i, 0xf0u << (hr32(c, f - 0x64u) & 31u))) return 1;
        hw32(c, hr32(c, d + 0x10u) + 4u * (uint32_t)i, o);
        hw32(c, hr32(c, d + 8u) + 4u * (uint32_t)i, 0);
        if (!hr16(c, 0x004d2680u) && view_shape(c, d, i, NULL)) return 1;
    }
    uint32_t h = 0;
    if (HCALL(c, HUD_VA_NEW, &h, 0x288u)) return 1;
    if (h && panel_object(c, h, hr32(c, d + 0x29u))) return 1;
    hw32(c, d + 0x1fu, h);
    hw32(c, VA_DISPLAY_OBJECT, d);
    if (HCALL(c, 0x00431bf8u, NULL, hr32(c, d + 0x1fu), name)) return 1;   /* the row panel_ctor (through the dispatch) */
    int32_t chassis = (int32_t)hsx16(hr16(c, hr32(c, hr32(c, d + 0x29u) + 0x1f2u) + 0x52u));
    uint32_t k = ((uint32_t)chassis * 24u + 0x20u) << 2;
    palette_entries(c, 0x2au, 0x18u, hr32(c, hr32(c, VA_COCKPIT_SHAPE) + 8u) + k);
    if (destroy_shape(c, hr32(c, VA_COCKPIT_SHAPE))) return 1;
    uint32_t shape = 0;
    if (HCALL(c, HUD_VA_PATH_DIR, &path, 0x0049ae71u, hr32(c, 0x004a09fcu)) || HCALL(c, HUD_VA_SHAPE_BY_NAME, &shape, path)) return 1;
    hw32(c, VA_COCKPIT_SHAPE, shape);
    uint32_t src = hr32(c, shape + 8u) + k;
    uint32_t old = palette_swap(c, hr32(c, 0x0049aef8u));
    palette_entries(c, 0x2au, 0x18u, src);
    palette_swap(c, old);
    if (destroy_shape(c, hr32(c, VA_COCKPIT_SHAPE))) return 1;
    if (palette_flush(c)) return 1;
    hw32(c, 0x004cfa98u, 1);
    hw32(c, 0x004cfa9cu, 0x004cfca0u);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, 0x004cfca4u + i, hr32(c, 0x004cfa40u + i));
    if (HCALL(c, HUD_VA_STREAM_DESTROY, NULL, s, 2u)) return 1;
    hud_frame_pop(c, top);
    return 0;
}

/* ======================================================================
 * 0x429660(D, P) (listing 0x429660..0x42980f), root step 19: D+0x14 <- -1;
 * when [0x49ac48] is 0 the cockpit shape 0x474cb0(0x492bec("cockpit",
 * [0x4a09fc])) and the view rectangles 0x4cfa28 = {0, 0, (0x140 << sx) - 1,
 * (0xc3 << sy) - 1, 0xffffff60 << sx, 0xffffffa6 << sy} copied to 0x4cfa40;
 * word D+0x27 <- word [[P+0x1f2]+0x6c], D+0x29 <- P; the chassis name
 * [[0x4a9d60] + 4 (word [[P+0x1f2]+0x6c])], or with [0x49ac50] set word
 * D+0x27 <- word [0x49ac4c] and the name [0x49ac50]; _strncpy(D+0x2d, name,
 * 8), byte D+0x35 <- 0; 0x429834(D, D+0x2d); D+0x18 <- -1, byte D+0x1c <- 0,
 * word D+0x1d <- 0; 0x49ac54/0x49ac5c <<= sx, 0x49ac58/0x49ac60 <<= sy;
 * [0x4cfa94] <- new[](0x488045(0x4cfa7e)); byte D+0x36 <- 0. EAX D, ECX the
 * size (the POP ECX), EDX new[]'s. */
static int display_ctor(hud_ctx *c, uint32_t d, uint32_t p, es2_sim_result *out) {
    hw32(c, d + 0x14u, 0xffffffffu);
    if (!hr32(c, VA_COCKPIT_SHAPE)) {
        uint32_t path = 0, shape = 0;
        if (HCALL(c, HUD_VA_PATH_DIR, &path, 0x0049ae69u, hr32(c, 0x004a09fcu)) || HCALL(c, HUD_VA_SHAPE_BY_NAME, &shape, path)) return 1;
        hw32(c, VA_COCKPIT_SHAPE, shape);
        hw32(c, 0x004cfa28u, 0u << shx(c));
        hw32(c, 0x004cfa2cu, 0u << shy(c));
        hw32(c, 0x004cfa30u, (0x140u << shx(c)) - 1u);
        hw32(c, 0x004cfa34u, (0xc3u << shy(c)) - 1u);
        hw32(c, 0x004cfa38u, 0xffffff60u << shx(c));
        hw32(c, 0x004cfa3cu, 0xffffffa6u << shy(c));
        for (uint32_t i = 0; i < 0x18u; i += 4u) hw32(c, 0x004cfa40u + i, hr32(c, 0x004cfa28u + i));
    }
    uint32_t type = hr32(c, p + 0x1f2u);
    hw16(c, d + 0x27u, hr16(c, type + 0x6cu));
    hw32(c, d + 0x29u, p);
    uint32_t name = hr32(c, hr32(c, 0x004a9d60u) + 4u * hsx16(hr16(c, type + 0x6cu)));
    if (hr32(c, 0x0049ac50u)) {
        hw16(c, d + 0x27u, hr16(c, 0x0049ac4cu));
        name = hr32(c, 0x0049ac50u);
    }
    if (HCALL(c, HUD_VA_STRNCPY, NULL, d + 0x2du, name, 8u)) return 1;
    hw8(c, d + 0x35u, 0);
    if (display_views(c, d, d + 0x2du)) return 1;
    hw32(c, d + 0x18u, 0xffffffffu);
    hw8(c, d + 0x1cu, 0);
    hw16(c, d + 0x1du, 0);
    shl32(c, 0x0049ac54u, shx(c));
    shl32(c, 0x0049ac58u, shy(c));
    shl32(c, 0x0049ac5cu, shx(c));
    shl32(c, 0x0049ac60u, shy(c));
    uint32_t size = 0;
    if (region_area(c, 0x004cfa7eu, &size)) return 1;
    es2_sim_result r;
    if (hud_call_regs(c, HUD_VA_NEW_ARRAY, (const uint32_t[]){size}, 1, NULL, &r)) return 1;
    hw32(c, 0x004cfa94u, r.eax);
    hw8(c, d + 0x36u, 0);
    out->eax = d; out->ecx = size; out->edx = r.edx;
    return 0;
}
void es2_sim_service_display_ctor(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    if (display_ctor(&c, sc->slots[0], sc->slots[1], &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
/* 0x48de2f(p) (listing 0x48de2f..0x48de4b): the element constructor
 * 0x49545b on p and p+8 (no stores). EAX p, ECX p+8. */
void es2_sim_service_pair_ctor(es2_sim_service_call *sc, void *user) {
    (void)user;
    sc->eax = sc->slots[0];
    sc->ecx = sc->slots[0] + 8u;
}

/* ======================================================================
 * The widget library pieces (hud.md, "the HUD line contract — coupling set and
 * frame writes": the panel's widgets).
 *
 * 0x4524a0(W, rect) (listing 0x4524a0..0x4524cf): byte W+0x10 <- 0, W+0x17
 * <- 0x49dbb6, the 16 bytes of rect to W+0, byte W+0x1f <- 0. EAX W.
 * 0x452c6c(R, W) (0x452c6c..0x452c8b): [R + 0x256 + 4 count] <- W, word
 * count R+0x254 + 1 (no bound, as the original). */
static void widget_init(hud_ctx *c, uint32_t w, uint32_t rect) {
    hw8(c, w + 0x10u, 0);
    hw32(c, w + 0x17u, 0x0049dbb6u);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, w + i, hr32(c, rect + i));
    hw8(c, w + 0x1fu, 0);
}
static void widget_add(hud_ctx *c, uint32_t root, uint32_t w) {
    uint16_t n = hr16(c, root + 0x254u);
    hw32(c, root + 0x256u + 4u * hsx16(n), w);
    hw16(c, root + 0x254u, (uint16_t)(n + 1u));
}
/* A type-0 region object at o (o+4 the type, o+8 the rectangle): [o] <-
 * 0x4a69f0, 0x4a3368, counter + 1, 0x4a3390, counter + 1, 0x4a3264, o+4
 * <- 0, counter + 1 (the inline constructor of 0x434664, 0x435490, ...). */
static void region_ctor(hud_ctx *c, uint32_t o) {
    hw32(c, o, 0x004a69f0u);
    hw32(c, o, 0x004a3368u);
    hud_counter(c, 1);
    hw32(c, o, 0x004a3390u);
    hud_counter(c, 1);
    hw32(c, o, 0x004a3264u);
    hw32(c, o + 4u, 0);
    hud_counter(c, 1);
}
/* 0x434664(B, rect) (listing 0x434664..0x434747): the system button:
 * 0x4524a0(B, rect); B+0x17 <- 0x49defc, word B+0x1d <- 0, B+0x20 <-
 * 0x49dea0, B+0x24 <- 0, B+0x17 <- 0x49debc, B+0x20 <- 0x49dee0, B+0x28 <-
 * 0, word B+0x1b <- 0, counter + 1; B+0x17 <- 0x49b774, B+0x20 <- 0x49b798;
 * the region at B+0x40; B+0x5c <- 3, word B+0x1b <- 3, rect to B+0x48;
 * B+0x58 <- new[](0x488045(B+0x44)); counter + 1. EAX B. */
static int system_button(hud_ctx *c, uint32_t b, uint32_t rect) {
    widget_init(c, b, rect);
    hw32(c, b + 0x17u, 0x0049defcu);
    hw16(c, b + 0x1du, 0);
    hw32(c, b + 0x20u, 0x0049dea0u);
    hw32(c, b + 0x24u, 0);
    hw32(c, b + 0x17u, 0x0049debcu);
    hw32(c, b + 0x20u, 0x0049dee0u);
    hw32(c, b + 0x28u, 0);
    hw16(c, b + 0x1bu, 0);
    hud_counter(c, 1);
    hw32(c, b + 0x17u, 0x0049b774u);
    hw32(c, b + 0x20u, 0x0049b798u);
    region_ctor(c, b + 0x40u);
    hw32(c, b + 0x5cu, 3);
    hw16(c, b + 0x1bu, 3);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, b + 0x48u + i, hr32(c, rect + i));
    uint32_t n = 0, x = 0;
    if (region_area(c, b + 0x44u, &n) || HCALL(c, HUD_VA_NEW_ARRAY, &x, n)) return 1;
    hw32(c, b + 0x58u, x);
    hud_counter(c, 1);
    return 0;
}
/* 0x434368(H) (listing 0x434368..0x43451c): the two system buttons. Byte
 * 0x49b640 = 0: [0x49b63c] <- 0x45cdf4(0x492bec("sysbuttn", [0x4a0a28] for
 * byte [0x4d2537] = 3 else [0x4a0a10])), the byte + 1. Button 1 on (x0, y0,
 * x1, y1) = ((0x13d << sx) - (0xc << sx), 2 << sy, 0x13d << sx, (0xb <<
 * sy) + (2 << sy)): new(0x60) -> 0x434664, counter - 5, H+0x246, added to
 * [H] (0x452c6c), its +0x30 <- [[[0x49b63c]+8]]; button 2 with x1 = (0x13d
 * << sx) - (0xc << sx) - (2 << sx), x0 = x1 - (0xc << sx): H+0x24a, its
 * +0x30 <- [[[0x49b63c]+8]+4]; H+0x25a <- 2. A NULL button (the original
 * writes through it) is refused. */
static int system_buttons(hud_ctx *c, uint32_t h) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x38u);
    if (!f) return hud_refuse(c, 0x00434368u, "system buttons", "the own code's call area is exhausted");
    f += 0x38u;
    if (!hr8(c, 0x0049b640u)) {
        uint32_t ext = (int8_t)hr8(c, VA_RESOLUTION) == 3 ? hr32(c, 0x004a0a28u) : hr32(c, 0x004a0a10u), path = 0, x = 0;
        if (HCALL(c, HUD_VA_PATH_DIR, &path, 0x0049b75eu, ext) || HCALL(c, HUD_VA_LOAD_BY_NAME, &x, path)) return 1;
        hw32(c, 0x0049b63cu, x);
        hw8(c, 0x0049b640u, (uint8_t)(hr8(c, 0x0049b640u) + 1u));
    }
    unsigned sx = shx(c), sy = shy(c);
    hw32(c, f - 0x34u, 2u << sy);
    hw32(c, f - 0x2cu, (0xbu << sy) + (2u << sy));
    hw32(c, f - 0x30u, 0x13du << sx);
    hw32(c, f - 0x38u, (0x13du << sx) - (0xcu << sx));
    for (unsigned k = 0; k < 2u; ++k) {
        if (k) {
            hw32(c, f - 0x30u, (0x13du << sx) - (0xcu << sx) - (2u << sx));
            hw32(c, f - 0x38u, hr32(c, f - 0x30u) - (0xcu << sx));
        }
        uint32_t b = 0;
        if (HCALL(c, HUD_VA_NEW, &b, 0x60u)) return 1;
        if (!b) return hud_null_refuse(c, 0x0043445eu, "system buttons", "operator new returned NULL (the original writes through it)");
        if (system_button(c, b, f - 0x38u)) return 1;
        hud_counter(c, -5);
        hw32(c, h + 0x246u + 4u * k, b);
        widget_add(c, hr32(c, h), b);
        hw32(c, b + 0x30u, hr32(c, hr32(c, hr32(c, 0x0049b63cu) + 8u) + 4u * k));
    }
    hw32(c, h + 0x25au, 2);
    hud_frame_pop(c, top);
    return 0;
}
void es2_sim_service_system_buttons(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (system_buttons(&c, sc->slots[0])) { hud_failed(sc); return; }
    sc->eax = hud_fs0();
}

/* ======================================================================
 * The message ports.
 *
 * 0x434e50(Q) (listing 0x434e50..0x434e8a): the queue: 0x49667c(Q, 0x31,
 * 10, 1, 0x437558) (ten entries), word Q+0x21c <- 0, the ten bytes Q+0x212
 * <- 0. 0x435490(P, rect) (0x435490..0x43556a): [P] <- 0x49bb00, the
 * queues at P+4 and P+0x222, the region at P+0x49f with rect at P+0x4a7,
 * P+0x49a <- 0, bytes P+0x49e, +0x4d2, +0x4b8, +0x4b7, +0x4cb, +0x4ca,
 * +0x4c9 <- 0, word P+0x4d0 <- 0, P+0x4cc <- 0, counter + 1. */
static int port_queue(hud_ctx *c, uint32_t q) {
    if (HCALL(c, HUD_VA_VECTOR_NEW, NULL, q, 0x31u, 0xau, 1u, 0x00437558u)) return 1;
    hw16(c, q + 0x21cu, 0);
    for (uint32_t i = 0; i < 10u; ++i) hw8(c, q + 0x212u + i, 0);
    return 0;
}
static int port_base(hud_ctx *c, uint32_t p, uint32_t rect) {
    hw32(c, p, 0x0049bb00u);
    if (port_queue(c, p + 4u) || port_queue(c, p + 0x222u)) return 1;
    region_ctor(c, p + 0x49fu);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, p + 0x4a7u + i, hr32(c, rect + i));
    hw32(c, p + 0x49au, 0);
    static const uint16_t bytes[] = {0x49e, 0x4d2, 0x4b8, 0x4b7, 0x4cb, 0x4ca, 0x4c9};
    for (unsigned i = 0; i < sizeof(bytes) / sizeof(bytes[0]); ++i) hw8(c, p + bytes[i], 0);
    hw16(c, p + 0x4d0u, 0);
    hw32(c, p + 0x4ccu, 0);
    hud_counter(c, 1);
    return 0;
}
/* 0x435bf0(P, rect) (0x435bf0..0x435c44): the DialogPort: 0x435490; [P] <-
 * 0x49bad4; n = 0x488045(P+0x4a3); P+0x4d3, P+0x4d7 <- new[](n); counter +
 * 1. EAX P (the last new[]'s EDX). */
static int dialog_port(hud_ctx *c, uint32_t p, uint32_t rect) {
    if (port_base(c, p, rect)) return 1;
    hw32(c, p, 0x0049bad4u);
    uint32_t n = 0, x = 0;
    if (region_area(c, p + 0x4a3u, &n)) return 1;
    if (HCALL(c, HUD_VA_NEW_ARRAY, &x, n)) return 1;
    hw32(c, p + 0x4d3u, x);
    if (HCALL(c, HUD_VA_NEW_ARRAY, &x, n)) return 1;
    hw32(c, p + 0x4d7u, x);
    hud_counter(c, 1);
    return 0;
}
void es2_sim_service_dialog_port(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (dialog_port(&c, sc->slots[0], sc->slots[1])) { hud_failed(sc); return; }
    sc->eax = sc->slots[0];
}
/* 0x436244(P, rect) (0x436244..0x4362e0): the CommandPort: 0x435bf0; [P]
 * <- 0x49baa8, P+0x4df <- [0x49b0d4]; the two buffers deleted (delete[]
 * each when not NULL), rect y1 P+0x4b3 + (0x58 << sy), the two buffers
 * again with the new area; counter + 1. EAX P. */
void es2_sim_service_command_port(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t p = sc->slots[0], n = 0, x = 0;
    if (dialog_port(&c, p, sc->slots[1])) { hud_failed(sc); return; }
    hw32(&c, p, 0x0049baa8u);
    hw32(&c, p + 0x4dfu, hr32(&c, 0x0049b0d4u));
    if (hr32(&c, p + 0x4d3u) && HCALL(&c, HUD_VA_DELETE_ARRAY, NULL, hr32(&c, p + 0x4d3u))) { hud_failed(sc); return; }
    if (hr32(&c, p + 0x4d7u) && HCALL(&c, HUD_VA_DELETE_ARRAY, NULL, hr32(&c, p + 0x4d7u))) { hud_failed(sc); return; }
    hw32(&c, p + 0x4b3u, hr32(&c, p + 0x4b3u) + (0x58u << shy(&c)));
    if (region_area(&c, p + 0x4a3u, &n) || HCALL(&c, HUD_VA_NEW_ARRAY, &x, n)) { hud_failed(sc); return; }
    hw32(&c, p + 0x4d3u, x);
    if (HCALL(&c, HUD_VA_NEW_ARRAY, &x, n)) { hud_failed(sc); return; }
    hw32(&c, p + 0x4d7u, x);
    hud_counter(&c, 1);
    sc->eax = p;
}
/* 0x4369a4(P, rect) (0x4369a4..0x436a0a): the EventPort: 0x435490; [P] <-
 * 0x49ba7c, byte P+0x4d3 <- 0 (the blink byte), the rectangle P+0x4a7 to
 * P+0x4d4, P+0x4e4 <- 0, word P+0x4e8 <- -1 (the repeat filter), counter +
 * 1. EAX P. */
void es2_sim_service_event_port(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t p = sc->slots[0];
    if (port_base(&c, p, sc->slots[1])) { hud_failed(sc); return; }
    hw32(&c, p, 0x0049ba7cu);
    hw8(&c, p + 0x4d3u, 0);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(&c, p + 0x4d4u + i, hr32(&c, p + 0x4a7u + i));
    hw32(&c, p + 0x4e4u, 0);
    hw16(&c, p + 0x4e8u, 0xffffu);
    hud_counter(&c, 1);
    sc->eax = p;
}
/* 0x435970(P, kind, a, b) (listing 0x435970..0x435a73): a port's message
 * texts. kind 0: the file "system", the string set 0x4d0454, the table
 * 0x4d0aaf; kind 1: "commandX" with X = a + '0', 0x4d0444, 0x4d08ed; kind
 * 2: "pilotX" with X = b + '0', the set 0x4d0414 + 16 a, the table
 * 0x4d0464 + 387 a (a sign-extended); other kinds use uninitialised locals
 * (refused). strings_load(set, 0x45ef1c(name, "str")); then until two
 * consecutive empty results of strings_next(set, &text, &record): a
 * nonzero result files the record under its first byte b: entry = table
 * + 9 b; when byte entry+4 is 0, entry+0 <- text and entry+5 <- record;
 * byte entry+4 + 1. EAX the last result. */
static int port_texts(hud_ctx *c, uint32_t kind, uint32_t a, uint32_t b, uint32_t *eax) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x24u);
    if (!f) return hud_refuse(c, 0x00435970u, "port texts", "the own code's call area is exhausted");
    f += 0x24u;
    uint32_t name = f - 0x24u, set, table;
    switch ((uint8_t)kind) {
    case 0: hud_gstrcpy(c, name, 0x0049ba42u); set = 0x004d0454u; table = 0x004d0aafu; break;
    case 1: hud_gstrcpy(c, name, 0x0049ba49u); hw8(c, name + 7u, (uint8_t)((uint8_t)a + 0x30u)); set = 0x004d0444u; table = 0x004d08edu; break;
    case 2: {
        hud_gstrcpy(c, name, 0x0049ba52u);
        hw8(c, name + 5u, (uint8_t)((uint8_t)b + 0x30u));
        uint32_t x = hsx8((uint8_t)a);
        set = 0x004d0414u + (x << 4);
        table = 0x004d0464u + 387u * x;
        break;
    }
    default: return hud_refuse(c, 0x00435986u, "port texts", "a text kind above 2 (the original reads uninitialised locals)");
    }
    uint32_t path = 0, r = 0;
    if (language_path(c, name, 0x0049ba59u, &path) || HCALL(c, 0x00453878u, NULL, set, path)) return 1;
    uint8_t previous = 0;
    for (;;) {
        if (HCALL(c, 0x00453918u, &r, set, f - 0xcu, f - 0x10u)) return 1;
        int stop = 0;
        if ((uint8_t)r) {
            uint32_t e = table + 9u * hr8(c, hr32(c, f - 0x10u));
            if (!hr8(c, e + 4u)) {
                hw32(c, e, hr32(c, f - 0xcu));
                hw32(c, e + 5u, hr32(c, f - 0x10u));
            }
            hw8(c, e + 4u, (uint8_t)(hr8(c, e + 4u) + 1u));
        } else if (!previous) {
            stop = 1;
        }
        previous = (uint8_t)r;
        if (stop) break;
    }
    hud_frame_pop(c, top);
    *eax = r;
    return 0;
}
void es2_sim_service_port_texts(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (port_texts(&c, sc->slots[1], sc->slots[2], sc->slots[3], &eax)) { hud_failed(sc); return; }
    sc->eax = eax;
}
/* ======================================================================
 * The layout scaling helpers: each shifts a fixed list of (x, y) pairs of
 * the layout by the game-size shifts (x by word [0x4d253a], y by word
 * [0x4d253c]; the same words as G+0x7e/G+0x80 of the globals object).
 * Listings: 0x438df8 (0x438df8..0x438e28: pairs +0, +8), 0x442e20
 * (0x442e20..0x442e5c: +0..+0x18), 0x442ea0 (..0x442ef2: 0x442e20, +0x20,
 * +0x28, +0x30), 0x442e60 (..0x442e9c: 0x442e20, +0x20, +0x28), 0x442ef4
 * (..0x442f3e: 0x442e20, +0x20 .. +0x48), 0x442f40 (..0x442f8a: +0 ..
 * +0x48), 0x445138 (..0x445174: +0 .. +0x18), 0x444bc4 (..0x444c73: +0 ..
 * +0x48), 0x447678 (..0x4476ed: +0 .. +0xe8), 0x4488f4 (..0x44897d: +0 ..
 * +0x38), 0x43e140 (..0x43e23b: +0 .. +0x20, +0x28, +0x30, +0x3c .. +0x6c),
 * 0x44bef8 (..0x44c028: first the dword +4 + 0x28, then +0 .. +0x168). */
static void scale_pair(hud_ctx *c, uint32_t a) { shl32(c, a, shx(c)); shl32(c, a + 4u, shy(c)); }
static void scale_run(hud_ctx *c, uint32_t base, uint32_t from, uint32_t to) {
    for (uint32_t o = from; o <= to; o += 8u) scale_pair(c, base + o);
}
static void scale_layout(hud_ctx *c, uint32_t va, uint32_t r) {
    switch (va) {
    case 0x00438df8u: scale_run(c, r, 0, 8); break;
    case 0x00442e20u: case 0x00445138u: scale_run(c, r, 0, 0x18); break;
    case 0x00442ea0u: scale_run(c, r, 0, 0x30); break;
    case 0x00442e60u: scale_run(c, r, 0, 0x28); break;
    case 0x00442ef4u: case 0x00442f40u: case 0x00444bc4u: scale_run(c, r, 0, 0x48); break;
    case 0x00447678u: scale_run(c, r, 0, 0xe8); break;
    case 0x004488f4u: scale_run(c, r, 0, 0x38); break;
    case 0x0043e140u: scale_run(c, r, 0, 0x30); scale_run(c, r, 0x3c, 0x6c); break;
    case 0x0044bef8u: hw32(c, r + 4u, hr32(c, r + 4u) + 0x28u); scale_run(c, r, 0, 0x168); break;
    default: break;
    }
}
void es2_sim_service_scale_layout(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    scale_layout(&c, sc->va, sc->slots[0]);
}

/* ======================================================================
 * The panel element (gauge) base and the label widgets.
 *
 * 0x438b20(G, rect, H) (listing 0x438b20..0x438bbd): [G] <- 0x49bc6c, the
 * rect to G+4, G+0x14 <- H, byte G+0xb0 <- 0, word G+0x74 <- 0, bytes
 * G+0x76, G+0x77 <- 0, G+0x6c <- 0, G+0x78 <- 0, bytes G+0x8c, G+0x8d <- 0,
 * G+0x90 <- 0, the blink periods G+0x7c <- 0xb4, G+0x80 <- 0x168, G+0x84
 * <- 0xb4, G+0x88 <- 0x168, words G+0x70 <- 0, G+0x72 <- 1. EAX G. */
static void gauge_base(hud_ctx *c, uint32_t g, uint32_t rect, uint32_t h) {
    hw32(c, g, 0x0049bc6cu);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, g + 4u + i, hr32(c, rect + i));
    hw32(c, g + 0x14u, h);
    hw8(c, g + 0xb0u, 0);
    hw16(c, g + 0x74u, 0);
    hw8(c, g + 0x76u, 0);
    hw8(c, g + 0x77u, 0);
    hw32(c, g + 0x6cu, 0);
    hw32(c, g + 0x78u, 0);
    hw8(c, g + 0x8cu, 0);
    hw8(c, g + 0x8du, 0);
    hw32(c, g + 0x90u, 0);
    hw32(c, g + 0x7cu, 0xb4u);
    hw32(c, g + 0x80u, 0x168u);
    hw32(c, g + 0x84u, 0xb4u);
    hw32(c, g + 0x88u, 0x168u);
    hw16(c, g + 0x70u, 0);
    hw16(c, g + 0x72u, 1);
}
/* 0x4387ac(L, font, flag) (listing 0x4387ac..0x4387cf): _memset(L, 0,
 * 0x21), L+0 <- font, byte L+0x18 <- flag. A NULL L (the original writes
 * through it) is refused. */
static int label_init(hud_ctx *c, uint32_t l, uint32_t font, uint32_t flag) {
    if (!l) return hud_null_refuse(c, 0x004387c3u, "label", "operator new returned NULL (the original writes through it)");
    if (HCALL(c, HUD_VA_MEMSET, NULL, l, 0u, 0x21u)) return 1;
    hw32(c, l, font);
    hw8(c, l + 0x18u, (uint8_t)flag);
    return 0;
}
/* 0x438884(L, offset, rect, flags) (listing 0x438884..0x43891f): the rect
 * to L+4; word L+0x14 <- the x anchor: flag 2 ((x1 - x0) >> 1) + x0 +
 * offset.x, flag 4 x1 - offset.x, else offset.x + x0 (16-bit sums); word
 * L+0x16 <- ((y1 - y0) >> 1) + y0 + (word [font+0x16] >> 1) + offset.y + 1;
 * L+0x19 <- flags. EAX L+4. The shifts are arithmetic. */
static int label_in_rect(hud_ctx *c, uint32_t l, uint32_t off, uint32_t rect, uint32_t flags) {
    if (!l) return hud_null_refuse(c, 0x00438896u, "label", "a NULL label (the original writes through it)");
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, l + 4u + i, hr32(c, rect + i));
    uint16_t x;
    if (flags & 2u) x = (uint16_t)((uint32_t)((int32_t)(hr32(c, l + 0xcu) - hr32(c, l + 4u)) >> 1) + hr16(c, l + 4u) + hr16(c, off));
    else if (flags & 4u) x = (uint16_t)(hr16(c, l + 0xcu) - hr16(c, off));
    else x = (uint16_t)(hr16(c, off) + hr16(c, l + 4u));
    hw16(c, l + 0x14u, x);
    uint32_t font = hr32(c, l);
    uint16_t y = (uint16_t)((uint32_t)((int32_t)(hr32(c, l + 0x10u) - hr32(c, l + 8u)) >> 1) + hr16(c, l + 8u));
    y = (uint16_t)(y + (uint16_t)((int32_t)hsx16(hr16(c, font + 0x16u)) >> 1));
    y = (uint16_t)(y + hr16(c, off + 4u) + 1u);
    hw16(c, l + 0x16u, y);
    hw32(c, l + 0x19u, flags);
    return 0;
}
/* 0x442c28(B, rect, G) (listing 0x442c28..0x442cae): the weapon-range
 * toggle: 0x4524a0(B, rect); B+0x17 <- 0x49defc, word B+0x1d <- 0, B+0x20
 * <- 0x49dea0, B+0x24 <- G, B+0x17 <- 0x49debc, B+0x20 <- 0x49dee0, B+0x28
 * <- 0, word B+0x1b <- 0, counter + 1; B+0x17 <- 0x49d5a4, B+0x20 <-
 * 0x49d5c8, byte B+0x40 <- 0, counter + 1; B+0x17 <- 0x49c758, B+0x20 <-
 * 0x49c77c, counter + 1. EAX B. */
static void range_toggle(hud_ctx *c, uint32_t b, uint32_t rect, uint32_t g) {
    widget_init(c, b, rect);
    hw32(c, b + 0x17u, 0x0049defcu);
    hw16(c, b + 0x1du, 0);
    hw32(c, b + 0x20u, 0x0049dea0u);
    hw32(c, b + 0x24u, g);
    hw32(c, b + 0x17u, 0x0049debcu);
    hw32(c, b + 0x20u, 0x0049dee0u);
    hw32(c, b + 0x28u, 0);
    hw16(c, b + 0x1bu, 0);
    hud_counter(c, 1);
    hw32(c, b + 0x17u, 0x0049d5a4u);
    hw32(c, b + 0x20u, 0x0049d5c8u);
    hw8(c, b + 0x40u, 0);
    hud_counter(c, 1);
    hw32(c, b + 0x17u, 0x0049c758u);
    hw32(c, b + 0x20u, 0x0049c77cu);
    hud_counter(c, 1);
}
/* 0x441df8(G, rect, H) (listing 0x441df8..0x441f8f): the WeaponRange
 * gauge: 0x438b20; [G] <- 0x49c85c; with the origin (x0, y0) of rect, the
 * widget root [[0x49b088]] and the font [0x49b0d8], per button i of 4 the
 * rectangle rect+0x10+16i moved by the origin (f-54), new(0x41) ->
 * 0x442c28(B, f-54, G), counter - 3, added to the root, G+0x18+4i <- B;
 * the label new(0x21) -> 0x4387ac(L, font, 1), 0x438884(L, {0, 0}, f-54,
 * 2), B+0x2c <- L, B+0x28 <- i; G+0x68 <- 4, byte G+0xb3 <- 0; for a
 * flyer chassis word [[G+0x20]+0x1b] <- 2. EAX G. NULL objects (the
 * original writes through them) are refused. */
static int range_gauge(hud_ctx *c, uint32_t g, uint32_t rect, uint32_t h) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x54u);
    if (!f) return hud_refuse(c, 0x00441df8u, "range gauge", "the own code's call area is exhausted");
    f += 0x54u;
    gauge_base(c, g, rect, h);
    hw32(c, g, 0x0049c85cu);
    uint32_t ox = hr32(c, rect), oy = hr32(c, rect + 4u), root = hr32(c, hr32(c, VA_PANEL)), font = hr32(c, 0x0049b0d8u);
    hw32(c, f - 0x2cu, 0);
    hw32(c, f - 0x30u, 0);
    for (uint32_t i = 0; i < 4u; ++i) {
        uint32_t sub = rect + 0x10u + 16u * i, b = 0, l = 0;
        hw32(c, f - 0x54u, hr32(c, sub) + ox);
        hw32(c, f - 0x50u, hr32(c, sub + 4u) + oy);
        hw32(c, f - 0x4cu, hr32(c, sub + 8u) + ox);
        hw32(c, f - 0x48u, hr32(c, sub + 12u) + oy);
        if (HCALL(c, HUD_VA_NEW, &b, 0x41u)) return 1;
        if (!b) return hud_null_refuse(c, 0x00441f2eu, "range gauge", "operator new returned NULL (the original writes through it)");
        range_toggle(c, b, f - 0x54u, g);
        hud_counter(c, -3);
        widget_add(c, root, b);
        hw32(c, g + 0x18u + 4u * i, b);
        if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u) || label_in_rect(c, l, f - 0x30u, f - 0x54u, 2u)) return 1;
        hw32(c, b + 0x2cu, l);
        hw32(c, b + 0x28u, i);
    }
    hw32(c, g + 0x68u, 4);
    hw8(c, g + 0xb3u, 0);
    if (hr16(c, hr32(c, hr32(c, hr32(c, VA_PANEL) + 0x203u) + 0x1f2u) + 0x50u)) hw16(c, hr32(c, g + 0x20u) + 0x1bu, 2);
    hud_frame_pop(c, top);
    return 0;
}

/* ======================================================================
 * The seven element creators of the panel constructor (listings
 * 0x432340..0x4323dc, 0x4323e0..0x432452, 0x432454..0x4324c6,
 * 0x4324c8..0x432549, 0x43254c..0x4325be, 0x4325c0..0x432632,
 * 0x432634..0x4326a6): r = L + offset; the layout scaling of r; new(size)
 * -> the constructor (obj, r, H) when not NULL; the panel's element list
 * [H + 8 + 4 [H+0x6c]] <- obj, H+0x6c + 1. 0x432340 (WeaponRange) first
 * calls gauge_mode(obj, 2) and sets byte H+0x1e0 <- 1, H+0x1d4 <- 0,
 * H+0x1d8 <- [H+0x16c]; 0x4324c8 (MFD) adds the element first and then
 * calls 0x446e60(obj, 3). EAX obj. */
static int element_add(hud_ctx *c, uint32_t h, uint32_t obj) {
    uint32_t n = hr32(c, h + 0x6cu);
    hw32(c, h + 0x6cu, n + 1u);
    hw32(c, h + 8u + 4u * n, obj);
    return 0;
}
static const struct { uint32_t va, offset, scale, size, ctor; } creators[] = {
    {0x00432340u, 0x1d4u, 0x00442f40u, 0xb5u, 0x00441df8u},
    {0x004323e0u, 0x224u, 0x00445138u, 0xc3u, 0x00444d84u},
    {0x00432454u, 0x268u, 0x00444bc4u, 0xd4u, 0x00443524u},
    {0x004324c8u, 0x2d8u, 0x00447678u, 0x349u, 0x00445240u},
    {0x0043254cu, 0x3e8u, 0x004488f4u, 0xcau, 0x00447bacu},
    {0x004325c0u, 0x440u, 0x0043e140u, 0x11bu, 0x0043c800u},
    {0x00432634u, 0x4bcu, 0x0044bef8u, 0x78au, 0x00448cf0u},
};
static int element_create(hud_ctx *c, uint32_t va, uint32_t h, uint32_t l, uint32_t *out) {
    unsigned k = 0;
    while (k < sizeof(creators) / sizeof(creators[0]) && creators[k].va != va) ++k;
    if (k == sizeof(creators) / sizeof(creators[0])) return hud_refuse(c, va, "panel element", "no creator for this VA");
    uint32_t r = l + creators[k].offset, obj = 0;
    scale_layout(c, creators[k].scale, r);
    if (HCALL(c, HUD_VA_NEW, &obj, creators[k].size)) return 1;
    if (obj) {
        if (creators[k].ctor == 0x00441df8u) {
            if (range_gauge(c, obj, r, h)) return 1;
        } else if (HCALL(c, creators[k].ctor, NULL, obj, r, h)) {
            return 1;
        }
    }
    if (va == 0x00432340u) {
        if (HCALL(c, 0x00441f90u, NULL, obj, 2u)) return 1;
        hw8(c, h + 0x1e0u, 1);
        hw32(c, h + 0x1d4u, 0);
        hw32(c, h + 0x1d8u, hr32(c, h + 0x16cu));
    }
    element_add(c, h, obj);
    if (va == 0x004324c8u && HCALL(c, 0x00446e60u, NULL, obj, 3u)) return 1;
    *out = obj;
    return 0;
}
void es2_sim_service_element_create(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t obj = 0;
    if (element_create(&c, sc->va, sc->slots[0], sc->slots[1], &obj)) { hud_failed(sc); return; }
    sc->eax = obj;
}
/* ======================================================================
 * The bars (0x43907c family). 0x43907c(B, rect) (listing
 * 0x43907c..0x4390ad): [B] <- 0x49bd78, rect to B+4. 0x4390c4(B, rect, n)
 * (0x4390c4..0x439117): 0x43907c, [B] <- 0x49bd60, B+0x14 <- ((x1 - x0)
 * << 16) / n (signed, n a sign-extended word), B+0x18 <- x0, B+0x1c <- x1,
 * B+0x20 <- x0 when B+0x14 > 0 else x1. 0x4395c4(B, rect, n)
 * (0x4395c4..0x4395e7): 0x4390c4, [B] <- 0x49bd30. A zero n (the original's
 * idiv faults) is refused. */
/* 0x439344(B, rect, n) (listing 0x439344..0x439396) is the vertical twin:
 * [B] <- 0x49bd48, the y extent, B+0x20 <- y1 when B+0x14 > 0 else y0. */
static int bar_base(hud_ctx *c, uint32_t b, uint32_t rect, uint16_t n, int vertical) {
    hw32(c, b, 0x0049bd78u);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, b + 4u + i, hr32(c, rect + i));
    hw32(c, b, vertical ? 0x0049bd48u : 0x0049bd60u);
    int32_t d = (int32_t)(int16_t)n;
    if (!d) return hud_divide_refuse(c, vertical ? 0x0043936fu : 0x004390eeu, 0, 0, "bar", "a zero step count (the original's idiv faults)");
    uint32_t lo = vertical ? 4u : 0u;
    hw32(c, b + 0x14u, (uint32_t)((int32_t)((hr32(c, rect + 8u + lo) - hr32(c, rect + lo)) << 16) / d));
    hw32(c, b + 0x18u, hr32(c, rect + lo));
    hw32(c, b + 0x1cu, hr32(c, rect + 8u + lo));
    if (vertical) hw32(c, b + 0x20u, (int32_t)hr32(c, b + 0x14u) > 0 ? hr32(c, b + 0x1cu) : hr32(c, b + 0x18u));
    else hw32(c, b + 0x20u, (int32_t)hr32(c, b + 0x14u) > 0 ? hr32(c, b + 0x18u) : hr32(c, b + 0x1cu));
    return 0;
}
static int bar_horizontal(hud_ctx *c, uint32_t b, uint32_t rect, uint16_t n) {
    if (bar_base(c, b, rect, n, 0)) return 1;
    hw32(c, b, 0x0049bd30u);
    return 0;
}
/* 0x444d84(G, R, H) (listing 0x444d84..0x444f46): the EnergyPool gauge:
 * 0x438b20; [G] <- 0x49cabc; O = new(0x239) -> 0x480c34(O), counter - 2,
 * [0x4d1a68] <- O; G+4 <- R+0x10's rectangle moved by R's origin; R+0x28
 * <- 6, R+0x2c <- 5 (the layout itself); _memset(G+0xb1, 0, 4), G+0xbf <-
 * 0, R+0x20 <- 0 and then the case of R+0x20 (always 0): B = new(0x34) ->
 * 0x4395c4(B, G+4, 0x400), B+0x2c <- word [0x4d3b7c + 2 word R+0x28],
 * B+0x30 <- word [0x4d3b7c + 2 word R+0x2c], B+0x24 <- word [0x4d3ba2]
 * (all sign-extended), G+0xb5 <- B. (Case 1, the vertical bar 0x439834,
 * is unreachable.) EAX G. A NULL bar (the original writes through it) is
 * refused. */
static int pool_gauge(hud_ctx *c, uint32_t g, uint32_t r, uint32_t h) {
    gauge_base(c, g, r, h);
    hw32(c, g, 0x0049cabcu);
    uint32_t ox = hr32(c, r), oy = hr32(c, r + 4u), o = 0, b = 0;
    if (HCALL(c, HUD_VA_NEW, &o, 0x239u)) return 1;
    if (o) {
        if (hud_gc_construct(c, o)) return 1;
        hud_counter(c, -2);
    }
    hw32(c, 0x004d1a68u, o);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, g + 4u + i, hr32(c, r + 0x10u + i));
    hw32(c, g + 4u, hr32(c, g + 4u) + ox);
    hw32(c, g + 8u, hr32(c, g + 8u) + oy);
    hw32(c, g + 0xcu, hr32(c, g + 0xcu) + ox);
    hw32(c, g + 0x10u, hr32(c, g + 0x10u) + oy);
    hw32(c, r + 0x28u, 6);
    hw32(c, r + 0x2cu, 5);
    if (HCALL(c, HUD_VA_MEMSET, NULL, g + 0xb1u, 0u, 4u)) return 1;
    hw32(c, g + 0xbfu, 0);
    hw32(c, r + 0x20u, 0);
    if (HCALL(c, HUD_VA_NEW, &b, 0x34u)) return 1;
    if (!b) return hud_null_refuse(c, 0x00444ebeu, "pool gauge", "operator new returned NULL (the original writes through it)");
    if (bar_horizontal(c, b, g + 4u, 0x400u)) return 1;
    hw32(c, b + 0x2cu, hsx16(hr16(c, 0x004d3b7cu + 2u * hsx16(hr16(c, r + 0x28u)))));
    hw32(c, b + 0x30u, hsx16(hr16(c, 0x004d3b7cu + 2u * hsx16(hr16(c, r + 0x2cu)))));
    hw32(c, b + 0x24u, hsx16(hr16(c, 0x004d3ba2u)));
    hw32(c, g + 0xb5u, b);
    return 0;
}
void es2_sim_service_pool_gauge(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (pool_gauge(&c, sc->slots[0], sc->slots[1], sc->slots[2])) { hud_failed(sc); return; }
    sc->eax = sc->slots[0];
}
/* 0x444b14(B, rect, G) (listing 0x444b14..0x444b80): the shield arrow
 * button: as 0x442c28 up to the first counter step, then B+0x17 <-
 * 0x49c9dd, B+0x20 <- 0x49ca01, counter + 1. */
static void shield_button(hud_ctx *c, uint32_t b, uint32_t rect, uint32_t g) {
    widget_init(c, b, rect);
    hw32(c, b + 0x17u, 0x0049defcu);
    hw16(c, b + 0x1du, 0);
    hw32(c, b + 0x20u, 0x0049dea0u);
    hw32(c, b + 0x24u, g);
    hw32(c, b + 0x17u, 0x0049debcu);
    hw32(c, b + 0x20u, 0x0049dee0u);
    hw32(c, b + 0x28u, 0);
    hw16(c, b + 0x1bu, 0);
    hud_counter(c, 1);
    hw32(c, b + 0x17u, 0x0049c9ddu);
    hw32(c, b + 0x20u, 0x0049ca01u);
    hud_counter(c, 1);
}
/* 0x443524(G, R, H) (listing 0x443524..0x443755): the Shields gauge:
 * 0x438b20; [G] <- 0x49ca1d; with R's origin, the root [[0x49b088]] and
 * the font [0x49b0d4], for i = 0, 1: the button rectangle R+0x10 (i = 0)
 * or R+0x20 moved by the origin, new(0x40) -> 0x444b14, counter - 2, G+0x18
 * + 4i <- B, added to the root; the label new(0x21) -> 0x4387ac(L, font,
 * 1) in R+0x30 / R+0x40 moved (0x438884(L, {0, 0}, rect, 2)), L+0x1d <-
 * word [0x4d3ba2], G+0xc4 / G+0xc8 <- L; G+0x68 <- 2; [0x4d1a64] <- 0x1000000
 * | byte [0x49c972] << 16 | byte [0x49c971] << 8 | byte [0x49c970];
 * _memset(G+0xb5, 0, 15); the shield palette 0x443918(G) through its row
 * (display.c; it writes the colour table entries 0x42..0x47 through 0x4303c4,
 * display_modes.md, "transitions, slides, jolt, bump and the shield palette in
 * the HUD line"). EAX G. */
static int shields_gauge(hud_ctx *c, uint32_t g, uint32_t r, uint32_t h) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x54u);
    if (!f) return hud_refuse(c, 0x00443524u, "shields gauge", "the own code's call area is exhausted");
    f += 0x54u;
    gauge_base(c, g, r, h);
    hw32(c, g, 0x0049ca1du);
    uint32_t ox = hr32(c, r), oy = hr32(c, r + 4u), root = hr32(c, hr32(c, VA_PANEL)), font = hr32(c, 0x0049b0d4u), rect = f - 0x54u;
    hw32(c, f - 0x2cu, 0);
    hw32(c, f - 0x30u, 0);
    for (uint32_t i = 0; i < 2u; ++i) {
        uint32_t sub = r + (i ? 0x20u : 0x10u), b = 0, l = 0;
        for (uint32_t k = 0; k < 16u; k += 4u) hw32(c, rect + k, hr32(c, sub + k) + ((k & 4u) ? oy : ox));
        if (HCALL(c, HUD_VA_NEW, &b, 0x40u)) return 1;
        if (b) {
            shield_button(c, b, rect, g);
            hud_counter(c, -2);
        }
        hw32(c, g + 0x18u + 4u * i, b);
        widget_add(c, root, b);
        if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u)) return 1;
        sub = r + (i ? 0x40u : 0x30u);
        for (uint32_t k = 0; k < 16u; k += 4u) hw32(c, rect + k, hr32(c, sub + k) + ((k & 4u) ? oy : ox));
        if (label_in_rect(c, l, f - 0x30u, rect, 2u)) return 1;
        hw32(c, l + 0x1du, hsx16(hr16(c, 0x004d3ba2u)));
        hw32(c, g + (i ? 0xc8u : 0xc4u), l);
    }
    hw32(c, g + 0x68u, 2);
    hw32(c, 0x004d1a64u, 0x1000000u | ((uint32_t)hr8(c, 0x0049c972u) << 16) | ((uint32_t)hr8(c, 0x0049c971u) << 8) | hr8(c, 0x0049c970u));
    if (HCALL(c, HUD_VA_MEMSET, NULL, g + 0xb5u, 0u, 0xfu)) return 1;
    if (HCALL(c, 0x00443918u, NULL, g)) return 1;   /* the shield palette (display.c) */
    hud_frame_pop(c, top);
    return 0;
}
void es2_sim_service_shields_gauge(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (shields_gauge(&c, sc->slots[0], sc->slots[1], sc->slots[2])) { hud_failed(sc); return; }
    sc->eax = sc->slots[0];
}
/* ======================================================================
 * The MFD and its pages.
 *
 * 0x445218(X, rect) (listing 0x445218..0x445230): the page base: [X] <-
 * 0x49cfbc, X+4 <- rect (the pointer), X+0xc <- 0. */
static void page_base(hud_ctx *c, uint32_t x, uint32_t rect) {
    hw32(c, x, 0x0049cfbcu);
    hw32(c, x + 4u, rect);
    hw32(c, x + 0xcu, 0);
}
static uint32_t sx16shl(hud_ctx *c, uint32_t word_va, unsigned s) {
    return hsx16(shl16(hr16(c, word_va), s));
}
/* 0x43a2e0(X, rect) (listing 0x43a2e0..0x43a59c): the status page: base,
 * [X] <- 0x49be40; when [0x49bda4] is 0 the bitmaps "bases", "vehicles",
 * "flyers" (extension "hba" with byte [0x4d256c], else "dba") into
 * 0x49bda4, 0x49bdac, 0x49bda8 and 0x4d17bc..0x4d17c8 <- 5 << sx, 0x14 <<
 * sy, 2 << sx, 5 << sy; X+0x24 <- (x0 + (0x2d << sx), y0 + (0xd << sy)),
 * X+0x2c <- (x0 + (0x5f << sx), y0 + (0x3a << sy)); five labels i: new(0x21)
 * -> 0x4387ac(L, font [0x49b0d4] or [0x49b0e4] by word [0x49bd98 + 2i], 1),
 * the rectangle (x0 + (word [0x49bd84+2i] << sx), y0 + (word [0x49bd8e+2i]
 * << sy), x0 + (0x2d << sx), that y + (5 << sy)) (16-bit shifts,
 * sign-extended), 0x438884(L, {0, 0}, it, 1), L+0x1d <- 0x11, X+0x10+4i <-
 * L; X+0x3e <- 0, byte X+0x39 <- 0. EAX X. (0x48251c's two height reads
 * are dead.) */
static int load_bitmap(hud_ctx *c, uint32_t name, uint32_t ext_h, uint32_t ext_d, uint32_t out) {
    uint32_t path = 0, x = 0;
    if (HCALL(c, HUD_VA_PATH_DIR, &path, name, hr8(c, 0x004d256cu) ? ext_h : ext_d) || HCALL(c, HUD_VA_LOAD_BY_NAME, &x, path)) return 1;
    hw32(c, out, x);
    return 0;
}
static int status_page(hud_ctx *c, uint32_t x, uint32_t rect) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x60u);
    if (!f) return hud_refuse(c, 0x0043a2e0u, "status page", "the own code's call area is exhausted");
    f += 0x60u;
    unsigned sx = shx(c), sy = shy(c);
    page_base(c, x, rect);
    hw32(c, x, 0x0049be40u);
    if (!hr32(c, 0x0049bda4u)) {
        if (load_bitmap(c, 0x0049bdf5u, 0x0049bdfbu, 0x0049bdffu, 0x0049bda4u) ||
            load_bitmap(c, 0x0049be03u, 0x0049be0cu, 0x0049be10u, 0x0049bdacu) ||
            load_bitmap(c, 0x0049be14u, 0x0049be1bu, 0x0049be1fu, 0x0049bda8u))
            return 1;
        hw32(c, 0x004d17bcu, 5u << sx);
        hw32(c, 0x004d17c0u, 0x14u << sy);
        hw32(c, 0x004d17c4u, 2u << sx);
        hw32(c, 0x004d17c8u, 5u << sy);
    }
    uint32_t x0 = hr32(c, rect), y0 = hr32(c, rect + 4u);
    hw32(c, x + 0x2cu, x0);
    hw32(c, x + 0x30u, y0);
    hw32(c, x + 0x24u, x0 + (0x2du << sx));
    hw32(c, x + 0x28u, y0 + (0xdu << sy));
    hw32(c, x + 0x2cu, hr32(c, x + 0x2cu) + (0x5fu << sx));
    hw32(c, x + 0x30u, hr32(c, x + 0x30u) + (0x3au << sy));
    hw32(c, f - 0x3cu, 0);
    hw32(c, f - 0x38u, 0);
    hw32(c, f - 0x2cu, hr32(c, 0x0049b0d4u));
    hw32(c, f - 0x28u, hr32(c, 0x0049b0e4u));
    for (uint32_t i = 0; i < 5u; ++i) {
        uint32_t l = 0, font = hr32(c, f - 0x2cu + 4u * hsx16(hr16(c, 0x0049bd98u + 2u * i)));
        if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u)) return 1;
        hw32(c, f - 0x60u, x0 + sx16shl(c, 0x0049bd84u + 2u * i, sx));
        hw32(c, f - 0x5cu, y0 + sx16shl(c, 0x0049bd8eu + 2u * i, sy));
        hw32(c, f - 0x58u, x0 + hsx16(shl16(0x2du, sx)));
        hw32(c, f - 0x54u, (5u << sy) + hr32(c, f - 0x5cu));
        if (label_in_rect(c, l, f - 0x3cu, f - 0x60u, 1u)) return 1;
        hw32(c, l + 0x1du, 0x11u);
        hw32(c, x + 0x10u + 4u * i, l);
    }
    hw32(c, x + 0x3eu, 0);
    hw8(c, x + 0x39u, 0);
    hud_frame_pop(c, top);
    return 0;
}
/* 0x43f600(X, rect, G) (listing 0x43f600..0x43f7c8): the orders page:
 * base, [X] <- 0x49c36c, X+0x10 <- G; X+0x39 = (x0 + (2 << sx) + (1 <<
 * sx), y0 + (0xd << sy) + (1 << sx)), X+0x41 = (x0 + (0x60 << sx) - (1 <<
 * sx), y0 + (0x3a << sy) - (1 << sx)) (the original adds the x step to y);
 * six labels i: new(0x25) -> 0x438a70(L, font [0x49b0b0], 1) (0x4387ac and
 * L+0x21 <- font) in the row (X+0x39's x0, y, x1, y + (7 << sy)) with y
 * advancing by 7 << sy, 0x438884(L, {2 << sx, 0}, row, 1), L+0x21 <-
 * [0x49b0b4], L+0x1d <- 0x11, X+0x14+4i <- L, byte X+0x2c+i <- 0, byte
 * X+0x36 <- 0; word X+0x37 <- -1. EAX X. */
static int orders_page(hud_ctx *c, uint32_t x, uint32_t rect, uint32_t g) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x50u);
    if (!f) return hud_refuse(c, 0x0043f600u, "orders page", "the own code's call area is exhausted");
    f += 0x50u;
    unsigned sx = shx(c), sy = shy(c);
    page_base(c, x, rect);
    hw32(c, x, 0x0049c36cu);
    hw32(c, x + 0x10u, g);
    uint32_t font = hr32(c, 0x0049b0b0u), step = 7u << sy;
    hw32(c, x + 0x41u, hr32(c, rect));
    hw32(c, x + 0x45u, hr32(c, rect + 4u));
    hw32(c, x + 0x39u, hr32(c, x + 0x41u));
    hw32(c, x + 0x3du, hr32(c, x + 0x45u));
    hw32(c, x + 0x39u, hr32(c, x + 0x39u) + (2u << sx));
    hw32(c, x + 0x3du, hr32(c, x + 0x3du) + (0xdu << sy));
    hw32(c, x + 0x41u, hr32(c, x + 0x41u) + (0x60u << sx));
    hw32(c, x + 0x45u, hr32(c, x + 0x45u) + (0x3au << sy));
    hw32(c, x + 0x39u, hr32(c, x + 0x39u) + (1u << sx));
    hw32(c, x + 0x3du, hr32(c, x + 0x3du) + (1u << sx));
    hw32(c, x + 0x41u, hr32(c, x + 0x41u) - (1u << sx));
    hw32(c, x + 0x45u, hr32(c, x + 0x45u) - (1u << sx));
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, f - 0x50u + i, hr32(c, x + 0x39u + i));
    hw32(c, f - 0x44u, hr32(c, f - 0x4cu) + step);
    hw32(c, f - 0x30u, 2u << sx);
    hw32(c, f - 0x2cu, 0);
    for (uint32_t i = 0; i < 6u; ++i) {
        uint32_t l = 0;
        if (HCALL(c, HUD_VA_NEW, &l, 0x25u)) return 1;
        if (l) {
            if (label_init(c, l, font, 1u)) return 1;
            hw32(c, l + 0x21u, font);
        }
        if (label_in_rect(c, l, f - 0x30u, f - 0x50u, 1u)) return 1;
        hw32(c, l + 0x21u, hr32(c, 0x0049b0b4u));
        hw32(c, l + 0x1du, 0x11u);
        hw32(c, x + 0x14u + 4u * i, l);
        hw32(c, f - 0x4cu, hr32(c, f - 0x4cu) + step);
        hw32(c, f - 0x44u, hr32(c, f - 0x44u) + step);
        hw8(c, x + 0x2cu + i, 0);
        hw8(c, x + 0x36u, 0);
    }
    hw16(c, x + 0x37u, 0xffffu);
    hud_frame_pop(c, top);
    return 0;
}
/* 0x4404bc(X, rect) (listing 0x4404bc..0x440603): the camera page: base,
 * [X] <- 0x49c4be; a context O = new(0x239) -> 0x480c34, counter - 2, X+0x10
 * <- O; rect to X+0x18; O+0x220 <- -((x1 - x0) >> 1), O+0x224 <- -((y1 -
 * y0) >> 1), O+0x208 <- 1; the camera K = new(0x22) -> 0x48bda4(K, O, 8,
 * 0x100, {0, 0, 0}, 0x49c454), counter - 2, X+0x14 <- K; 0x480d44 of the
 * context current at entry. EAX X. A NULL context is refused. */
static int camera_page(hud_ctx *c, uint32_t x, uint32_t rect) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x44u);
    if (!f) return hud_refuse(c, 0x004404bcu, "camera page", "the own code's call area is exhausted");
    f += 0x44u;
    page_base(c, x, rect);
    hw32(c, x, 0x0049c4beu);
    uint32_t saved = hr32(c, VA_GC_CURRENT), o = 0, k = 0;
    if (HCALL(c, HUD_VA_NEW, &o, 0x239u)) return 1;
    if (!o) return hud_null_refuse(c, 0x00440575u, "camera page", "operator new returned NULL (the original writes through it)");
    if (hud_gc_construct(c, o)) return 1;
    hud_counter(c, -2);
    hw32(c, x + 0x10u, o);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, x + 0x18u + i, hr32(c, rect + i));
    hw32(c, o + 0x220u, 0u - (uint32_t)((int32_t)(hr32(c, rect + 8u) - hr32(c, rect)) >> 1));
    hw32(c, o + 0x224u, 0u - (uint32_t)((int32_t)(hr32(c, rect + 0xcu) - hr32(c, rect + 4u)) >> 1));
    hw32(c, o + 0x208u, 1);
    if (HCALL(c, HUD_VA_NEW, &k, 0x22u)) return 1;
    if (k) {
        hw32(c, f - 0x44u, 0);
        hw32(c, f - 0x40u, 0);
        hw32(c, f - 0x3cu, 0);
        if (HCALL(c, 0x0048bda4u, NULL, k, hr32(c, x + 0x10u), 8u, 0x100u, f - 0x44u, 0x0049c454u)) return 1;
        hud_counter(c, -2);
    }
    hw32(c, x + 0x14u, k);
    hud_gc_select(c, saved);
    hud_frame_pop(c, top);
    return 0;
}
/* 0x43faf4(X, rect) (listing 0x43faf4..0x43fe43): the target page: base,
 * [X] <- 0x49c449; rect to 0x4d1a44; O = new(0x239) -> 0x480c34, counter -
 * 2, X+0x10 <- O; the rect to O+0x210, O+0x218 - 1, O+0x220/O+0x224 <- the
 * negative half extents; the clip record 0x4d1828 = {1, 0x4d1a30, ...} with
 * O+0x210's rect at 0x4d1a34, O+0x208 <- 2, 0x81 dwords of 0x4d1828 to
 * O+4, O+0x20c <- 0; the camera K = new(0x4e) -> 0x48bda4(K, O, 7, 0x80,
 * {0, 0, 0}, {0, 0, 0}), [K] <- 0x49f8e8, K+0x32, K+0x4a <- 0, words K+0x36,
 * K+0x48, K+0x46, K+0x44 <- 0, counter + 1 - 3, X+0x14 <- K; four labels i:
 * new(0x21) -> 0x4387ac(L, [0x49b0b4], 1), X+0x33+4i <- L, the rectangle
 * (x0 + (word [0x49c374+2i] << sx), y0 + (word [0x49c37c+2i] << sy), that
 * + (word [0x49c384+2i] << sx), that + (word [0x49c38c+2i] << sy)) (16-bit
 * shifts), 0x438884(L, {0, 0}, it, 2); [[X+0x3b]] <- [0x49b0b8],
 * [X+0x3b]+0x1d <- word [0x4d3b86]; byte X+0x45 <- 0. EAX X. */
static int target_page(hud_ctx *c, uint32_t x, uint32_t rect) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x80u);
    if (!f) return hud_refuse(c, 0x0043faf4u, "target page", "the own code's call area is exhausted");
    f += 0x80u;
    unsigned sx = shx(c), sy = shy(c);
    page_base(c, x, rect);
    hw32(c, x, 0x0049c449u);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, 0x004d1a44u + i, hr32(c, rect + i));
    uint32_t o = 0, k = 0;
    if (HCALL(c, HUD_VA_NEW, &o, 0x239u)) return 1;
    if (!o) return hud_null_refuse(c, 0x0043fb80u, "target page", "operator new returned NULL (the original writes through it)");
    if (hud_gc_construct(c, o)) return 1;
    hud_counter(c, -2);
    hw32(c, x + 0x10u, o);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, o + 0x210u + i, hr32(c, 0x004d1a44u + i));
    hw32(c, o + 0x218u, hr32(c, o + 0x218u) - 1u);
    hw32(c, o + 0x220u, 0u - (uint32_t)((int32_t)(hr32(c, rect + 8u) - hr32(c, rect)) >> 1));
    hw32(c, o + 0x224u, 0u - (uint32_t)((int32_t)(hr32(c, rect + 0xcu) - hr32(c, rect + 4u)) >> 1));
    hw32(c, 0x004d1828u, 1);
    hw32(c, 0x004d182cu, 0x004d1a30u);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, 0x004d1a34u + i, hr32(c, o + 0x210u + i));
    hw32(c, o + 0x208u, 2);
    for (uint32_t i = 0; i < 0x81u; ++i) hw32(c, o + 4u + 4u * i, hr32(c, 0x004d1828u + 4u * i));
    hw32(c, o + 0x20cu, 0);
    if (HCALL(c, HUD_VA_NEW, &k, 0x4eu)) return 1;
    if (k) {
        for (uint32_t i = 0; i < 6u; i += 2u) hw16(c, f - 0x38u + i, 0);
        for (uint32_t i = 0; i < 12u; i += 4u) hw32(c, f - 0x70u + i, 0);
        if (HCALL(c, 0x0048bda4u, NULL, k, hr32(c, x + 0x10u), 7u, 0x80u, f - 0x70u, f - 0x38u)) return 1;
        hw32(c, k, 0x0049f8e8u);
        hw32(c, k + 0x32u, 0);
        hw32(c, k + 0x4au, 0);
        hw16(c, k + 0x36u, 0);
        hw16(c, k + 0x48u, 0);
        hw16(c, k + 0x46u, 0);
        hw16(c, k + 0x44u, 0);
        hud_counter(c, 1);
        hud_counter(c, -3);
    }
    hw32(c, x + 0x14u, k);
    hw32(c, f - 0x50u, 0);
    hw32(c, f - 0x54u, 0);
    uint32_t font = hr32(c, 0x0049b0b4u);
    for (uint32_t i = 0; i < 4u; ++i) {
        uint32_t l = 0;
        if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u)) return 1;
        hw32(c, x + 0x33u + 4u * i, l);
        hw32(c, f - 0x80u, hr32(c, rect) + sx16shl(c, 0x0049c374u + 2u * i, sx));
        hw32(c, f - 0x7cu, hr32(c, rect + 4u) + sx16shl(c, 0x0049c37cu + 2u * i, sy));
        hw32(c, f - 0x78u, hr32(c, f - 0x80u) + sx16shl(c, 0x0049c384u + 2u * i, sx));
        hw32(c, f - 0x74u, hr32(c, f - 0x7cu) + sx16shl(c, 0x0049c38cu + 2u * i, sy));
        if (label_in_rect(c, l, f - 0x54u, f - 0x80u, 2u)) return 1;
    }
    uint32_t l2 = hr32(c, x + 0x3bu);
    hw32(c, l2, hr32(c, 0x0049b0b8u));
    hw32(c, l2 + 0x1du, hsx16(hr16(c, 0x004d3b86u)));
    hw8(c, x + 0x45u, 0);
    hud_frame_pop(c, top);
    return 0;
}

/* The MFD buttons: 0x44730c(B, rect, G, i) (listing 0x44730c..0x44737c)
 * and 0x447444(B, rect, G, i) (0x447444..0x4474cd): 0x4524a0; B+0x17 <-
 * 0x49defc, word B+0x1d <- 0, B+0x20 <- 0x49dea0, B+0x24 <- G, B+0x17 <-
 * 0x49debc, B+0x20 <- 0x49dee0, B+0x28 <- i, word B+0x1b <- 0, counter + 1;
 * the push button then B+0x17 <- 0x49cf60, B+0x20 <- 0x49cf84, counter +
 * 1; the toggle B+0x17 <- 0x49d5a4, B+0x20 <- 0x49d5c8, byte B+0x40 <- 0,
 * counter + 1, B+0x17 <- 0x49cf20, B+0x20 <- 0x49cf44, counter + 1.
 * 0x4475fc(P, rect, G) (0x4475fc..0x447654): 0x4524a0; P+0x17 <- 0x49defc,
 * word P+0x1d <- 0, P+0x20 <- 0x49dea0, P+0x24 <- G, P+0x17 <- 0x49d904,
 * P+0x20 <- 0x49d928, word P+0x1b <- 0, P+0x17 <- 0x49cee0, P+0x20 <-
 * 0x49cf04 (no counter step). */
static void mfd_button(hud_ctx *c, uint32_t b, uint32_t rect, uint32_t g, uint32_t i, int toggle) {
    widget_init(c, b, rect);
    hw32(c, b + 0x17u, 0x0049defcu);
    hw16(c, b + 0x1du, 0);
    hw32(c, b + 0x20u, 0x0049dea0u);
    hw32(c, b + 0x24u, g);
    hw32(c, b + 0x17u, 0x0049debcu);
    hw32(c, b + 0x20u, 0x0049dee0u);
    hw32(c, b + 0x28u, i);
    hw16(c, b + 0x1bu, 0);
    hud_counter(c, 1);
    if (!toggle) {
        hw32(c, b + 0x17u, 0x0049cf60u);
        hw32(c, b + 0x20u, 0x0049cf84u);
        hud_counter(c, 1);
        return;
    }
    hw32(c, b + 0x17u, 0x0049d5a4u);
    hw32(c, b + 0x20u, 0x0049d5c8u);
    hw8(c, b + 0x40u, 0);
    hud_counter(c, 1);
    hw32(c, b + 0x17u, 0x0049cf20u);
    hw32(c, b + 0x20u, 0x0049cf44u);
    hud_counter(c, 1);
}
static void mfd_pager(hud_ctx *c, uint32_t p, uint32_t rect, uint32_t g) {
    widget_init(c, p, rect);
    hw32(c, p + 0x17u, 0x0049defcu);
    hw16(c, p + 0x1du, 0);
    hw32(c, p + 0x20u, 0x0049dea0u);
    hw32(c, p + 0x24u, g);
    hw32(c, p + 0x17u, 0x0049d904u);
    hw32(c, p + 0x20u, 0x0049d928u);
    hw16(c, p + 0x1bu, 0);
    hw32(c, p + 0x17u, 0x0049cee0u);
    hw32(c, p + 0x20u, 0x0049cf04u);
}
/* 0x471dac(A, shape, frames) (listing 0x471dac..0x471dde): an animation:
 * A+0x1e <- shape, A+0x16 <- frames, A+0x1a <- 0, word A+0 <- 0, A+0x12
 * <- [frames+4], words A+4, A+2 <- 0, A+6 <- 0. */
static void animation_init(hud_ctx *c, uint32_t a, uint32_t shape, uint32_t frames) {
    hw32(c, a + 0x1eu, shape);
    hw32(c, a + 0x16u, frames);
    hw32(c, a + 0x1au, 0);
    hw16(c, a, 0);
    hw32(c, a + 0x12u, hr32(c, frames + 4u));
    hw16(c, a + 4u, 0);
    hw16(c, a + 2u, 0);
    hw32(c, a + 6u, 0);
}

/* 0x445240(G, R, H) (listing 0x445240..0x44615d): the MFD (see hud.md, "the HUD
 * line in the own process" for the field list). One clock read at 0x445c27
 * (G+0x100 <- clock
 * + 30). */
#define VA_MFD_BITMAP 0x0049cbccu
static int mfd_label(hud_ctx *c, uint32_t font, uint32_t off, uint32_t rect, uint32_t flags, uint32_t *out) {
    uint32_t l = 0;
    if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u) || label_in_rect(c, l, off, rect, flags)) return 1;
    *out = l;
    return 0;
}
static int mfd_ctor(hud_ctx *c, uint32_t g, uint32_t r, uint32_t h) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x154u);
    if (!f) return hud_refuse(c, 0x00445240u, "MFD", "the own code's call area is exhausted");
    f += 0x154u;
    unsigned sx = shx(c), sy = shy(c);
    gauge_base(c, g, r, h);
    hw32(c, g, 0x0049cfa0u);
    if (!hr32(c, VA_MFD_BITMAP) && load_bitmap(c, 0x0049cea9u, 0x0049ceadu, 0x0049ceb1u, VA_MFD_BITMAP)) return 1;
    uint32_t ox = hr32(c, r), oy = hr32(c, r + 4u), root = hr32(c, hr32(c, VA_PANEL)), rect = f - 0x154u, off = f - 0x6cu;
    hw32(c, g + 0x320u, hr32(c, r + 0xe0u));
    hw32(c, g + 0x324u, hr32(c, r + 0xe4u));
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, g + 0xebu + i, hr32(c, r + 0xe0u + i));
    hw32(c, g + 0xebu, hr32(c, g + 0xebu) + (0x12u << sx));
    hw32(c, g + 0xefu, hr32(c, g + 0xefu) + (0u << sy));
    hw32(c, g + 0xebu, hr32(c, g + 0xebu) + ox);
    hw32(c, g + 0xefu, hr32(c, g + 0xefu) + oy);
    hw32(c, g + 0xf3u, hr32(c, g + 0xf3u) + ox);
    hw32(c, g + 0xf7u, hr32(c, g + 0xf7u) + oy);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, g + 4u + i, hr32(c, g + 0xebu + i));
    hw32(c, f - 0x68u, 0);
    hw32(c, f - 0x6cu, 0);
    hw32(c, g + 0x68u, 0);
    static const uint8_t kind[13] = {5, 5, 5, 5, 5, 5, 4, 2, 3, 3, 2, 1, 1};
    uint32_t toggle = 0, row = 0, font = 0;
    for (uint32_t i = 0; i < 13u; ++i) {
        uint32_t x0 = hr32(c, g + 0xebu), y0 = hr32(c, g + 0xefu);
        hw32(c, rect, x0 + (hsx16(hr16(c, 0x0049caccu + 2u * i)) << sx));
        hw32(c, rect + 4u, y0 + (hsx16(hr16(c, 0x0049cae6u + 2u * i)) << sy));
        hw32(c, rect + 8u, x0 + (hsx16(hr16(c, 0x0049cb00u + 2u * i)) << sx));
        hw32(c, rect + 12u, y0 + (hsx16(hr16(c, 0x0049cb1au + 2u * i)) << sy));
        switch (kind[i]) {
        case 5: toggle = 1; row = 3; font = hr32(c, 0x0049b0dcu); break;
        case 2: toggle = 0; row = 5; font = hr32(c, 0x0049b0d4u); break;
        case 3: toggle = 0; row = 7; font = hr32(c, 0x0049b0d4u); break;
        case 1: toggle = 1; row = 9; font = hr32(c, 0x0049b0d4u); break;
        default: break;
        }
        uint32_t b = 0, l = 0;
        if (HCALL(c, HUD_VA_NEW, &b, toggle ? 0x41u : 0x40u)) return 1;
        if (!b) return hud_null_refuse(c, 0x00445699u, "MFD", "operator new returned NULL (the original writes through it)");
        mfd_button(c, b, rect, g, i, (int)toggle);
        hud_counter(c, toggle ? -3 : -2);
        uint32_t table = hr32(c, hr32(c, VA_MFD_BITMAP) + 8u);
        hw32(c, b + 0x30u, hr32(c, table + 4u * row));
        hw32(c, b + 0x34u, hr32(c, table + 4u * row + 4u));
        if (mfd_label(c, font, off, rect, 2u, &l)) return 1;
        hw32(c, l + 0x1du, 0);
        hw32(c, b + 0x2cu, l);
        hw32(c, g + 0x18u + 4u * i, b);
        widget_add(c, root, b);
        hw32(c, g + 0x68u, hr32(c, g + 0x68u) + 1u);
    }
    uint32_t l = 0;
    if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u)) return 1;
    hw32(c, g + 0xc9u, l);
    uint32_t x0 = hr32(c, g + 0xebu), y0 = hr32(c, g + 0xefu);
    hw32(c, rect, x0 + (4u << sx));
    hw32(c, rect + 4u, y0 + (0u << sy));
    hw32(c, rect + 8u, x0 + (0x28u << sx));
    hw32(c, rect + 12u, y0 + (9u << sy));
    if (label_in_rect(c, hr32(c, g + 0xc9u), off, rect, 1u)) return 1;
    hw32(c, hr32(c, g + 0xc9u) + 0x1du, 0);
    font = hr32(c, 0x0049b0dcu);
    if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u)) return 1;
    hw32(c, g + 0x329u, l);
    hw32(c, rect, x0 + (0x16u << sx));
    hw32(c, rect + 4u, y0 + (0x2eu << sy));
    hw32(c, rect + 8u, x0 + (0x4au << sx));
    hw32(c, rect + 12u, y0 + (0x34u << sy));
    if (label_in_rect(c, hr32(c, g + 0x329u), off, rect, 2u)) return 1;
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, rect + i, hr32(c, g + 0xebu + i));
    uint32_t p = 0;
    if (HCALL(c, HUD_VA_NEW, &p, 0x28u)) return 1;
    if (p) mfd_pager(c, p, rect, g);
    hw32(c, g + 0xfbu, p);
    widget_add(c, root, hr32(c, g + 0xfbu));
    hw32(c, 0x004d1c70u, 50000u);
    hw32(c, 0x004d1c74u, 100000u);
    hw32(c, 0x004d1c78u, 200000u);
    hw32(c, g + 0xc1u, 0xffffffffu);
    hw32(c, g + 0xc5u, 2);
    if (HCALL(c, HUD_VA_MEMSET, NULL, g + 0xb1u, 0u, 0x10u)) return 1;
    hw32(c, g + 0xb9u, 0);
    hw32(c, g + 0xbdu, hr32(c, 0x004d1c70u + 4u * hr32(c, g + 0xc5u)));
    hw32(c, g + 0xb1u, 0);
    static const struct { uint32_t size, slot; uint8_t kind; } pages[] = {
        {0x42u, 0xcdu, 0}, {0x49u, 0xd1u, 1}, {0x28u, 0xd5u, 2}, {0x524u, 0xd9u, 3}, {0x42u, 0xddu, 0}, {0x4au, 0xe1u, 4},
    };
    for (unsigned k = 0; k < sizeof(pages) / sizeof(pages[0]); ++k) {
        uint32_t x = 0;
        if (HCALL(c, HUD_VA_NEW, &x, pages[k].size)) return 1;
        if (!x) return hud_null_refuse(c, 0x00445a85u, "MFD", "operator new returned NULL (the original writes through it)");
        int bad = 0;
        switch (pages[k].kind) {
        case 0: bad = status_page(c, x, g + 0xebu); break;
        case 1: bad = orders_page(c, x, g + 0xebu, g); break;
        case 2: bad = camera_page(c, x, g + 0xebu); break;
        case 3: bad = HCALL(c, 0x0043e734u, NULL, x, g + 0xebu); break;
        default: bad = target_page(c, x, g + 0xebu); break;
        }
        if (bad) return 1;
        hw32(c, x + 8u, g + 0xb1u);
        hw32(c, g + pages[k].slot, x);
    }
    uint32_t now = 0;
    if (hud_clock(c, 0x00445c27u, &now)) return 1;
    hw32(c, g + 0x100u, now + 0x1eu);
    if (!hr32(c, 0x0049cbd0u)) {
        if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u)) return 1;
        hw32(c, 0x0049cbd0u, l);
        if (label_in_rect(c, hr32(c, 0x0049cbd0u), off, g + 0xebu, 2u)) return 1;
        hw32(c, hr32(c, 0x0049cbd0u) + 0x1du, hsx16(hr16(c, 0x004d3ba2u)));
    }
    hw8(c, g + 0xffu, 0);
    uint32_t b = hr32(c, g + 0x44u);
    if (!hr8(c, b + 0x40u)) {
        hw8(c, b + 0x40u, 1);
        if (HCALL(c, hr32(c, hr32(c, b + 0x17u) + 4u), NULL, b)) return 1;
    }
    b = hr32(c, g + 0x48u);
    if (hr8(c, b + 0x40u)) hw8(c, b + 0x40u, 0);
    if (HCALL(c, hr32(c, hr32(c, b + 0x17u) + 4u), NULL, b)) return 1;
    if (!hr8(c, 0x0049cc26u)) {
        region_ctor(c, 0x004d1cb0u);
        hud_counter(c, -3);
        hw8(c, 0x0049cc26u, (uint8_t)(hr8(c, 0x0049cc26u) + 1u));
    }
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, 0x004d1cb8u + i, hr32(c, g + 0xebu + i));
    hw32(c, g + 0x104u, 1);
    hw32(c, g + 0x108u, 0x004d1cb4u);
    if (!hr32(c, 0x0049cbd4u)) {
        if (load_bitmap(c, 0x0049ceb5u, 0x0049cebdu, 0x0049cec1u, 0x0049cbd4u)) return 1;
        uint32_t lists = 0, frames = 0, heads = 0, sum = 0;
        if (HCALL(c, HUD_VA_NEW_ARRAY, &lists, 0x18u)) return 1;
        hw32(c, 0x004d1c84u, lists);
        for (uint32_t k = 0; k < 3u; ++k) {
            hw32(c, lists + 8u * k, hsx16(hr16(c, 0x0049cb40u + 2u * k)));
            sum += hr32(c, lists + 8u * k);
        }
        if (HCALL(c, HUD_VA_NEW_ARRAY, &frames, sum << 3)) return 1;
        hw32(c, 0x004d1c7cu, frames);
        if (HCALL(c, HUD_VA_NEW_ARRAY, &heads, 0x18u)) return 1;
        hw32(c, 0x004d1c80u, heads);
        uint32_t e = hr32(c, 0x004d1c7cu), list = hr32(c, 0x004d1c84u), head = hr32(c, 0x004d1c80u), slot = 0x004d1c88u, step = 0;
        static const uint32_t tables[6] = {0x0049cb4cu, 0x0049cb58u, 0x0049cb64u, 0x0049cb70u, 0x0049cb7cu, 0x0049cb88u};
        for (uint32_t k = 0; k < 3u; ++k, head += 8u, list += 8u) {
            hw32(c, list + 4u, e);
            for (uint32_t j = 0; hr32(c, list) > j; ++j, ++step, e += 8u) {
                hw32(c, e, hsx16(hr16(c, tables[k] + 2u * step)));
                hw32(c, e + 4u, 4);
            }
            hw32(c, head, 1);
            hw32(c, head + 4u, list);
            uint32_t a = 0;
            if (HCALL(c, HUD_VA_NEW, &a, 0x2au)) return 1;
            if (a) animation_init(c, a, hr32(c, 0x0049cbd4u), head);
            hw32(c, slot, a);
            slot += 4u;
        }
    }
    if (load_bitmap(c, 0x0049cec5u, 0x0049cecbu, 0x0049cecfu, g + 0x341u)) return 1;
    uint32_t list = 0, one = 0, two = 0, a = 0;
    if (HCALL(c, HUD_VA_NEW_ARRAY, &list, 0x50u)) return 1;
    hw32(c, g + 0x339u, list);
    for (uint32_t i = 0; i < 10u; ++i) {
        hw32(c, hr32(c, g + 0x339u) + 8u * i, i);
        hw32(c, hr32(c, g + 0x339u) + 8u * i + 4u, 7);
    }
    if (HCALL(c, HUD_VA_NEW, &one, 8u)) return 1;
    hw32(c, g + 0x335u, one);
    hw32(c, hr32(c, g + 0x335u), 0xau);
    hw32(c, hr32(c, g + 0x335u) + 4u, hr32(c, g + 0x339u));
    if (HCALL(c, HUD_VA_NEW, &two, 8u)) return 1;
    hw32(c, g + 0x33du, two);
    hw32(c, hr32(c, g + 0x33du), 1);
    hw32(c, hr32(c, g + 0x33du) + 4u, hr32(c, g + 0x335u));
    if (HCALL(c, HUD_VA_NEW, &a, 0x2au)) return 1;
    if (a) animation_init(c, a, hr32(c, g + 0x341u), hr32(c, g + 0x33du));
    hw32(c, g + 0x331u, a);
    hw16(c, g + 0x74u, 0);
    hud_frame_pop(c, top);
    return 0;
}
void es2_sim_service_mfd_ctor(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (mfd_ctor(&c, sc->slots[0], sc->slots[1], sc->slots[2])) { hud_failed(sc); return; }
    sc->eax = sc->slots[0];
}
/* 0x43e734(X, rect) (listing 0x43e734..0x43ec04): the radar page (page 3):
 * base, [X] <- 0x49c328; the 100 blip entries 0x49667c(X+0x68, 0xc, 100,
 * 1, 0x43f5f5); X+0x10/X+0x14 <- the MFD bitmap's entries 14 and 15
 * ([[0x49cbcc]+8] + 0x38/0x3c); X+0x28 <- 0x19, X+0x20 = (0x41 << sx) + x0,
 * X+0x24 = (0x1c << sy) + y0; X+0x18 = x0 + (0x26 << sx), X+0x1c = y0 + (1
 * << sy); X+0x2c <- 1, X+0x30 <- 0; four labels (font [0x49b0e0]) into
 * X+0x38, X+0x3c, X+0x40, X+0x44 at ((a << sx) + x0, (b << sy) + y0) with
 * the width w << sx and the height 5 << sy (16-bit) for (a, b, w, flags) =
 * (0x26, 0x33, 0xc, 1), (0x4f, 1, 0x10, 4), (0x26, 1, 0xc, 1), (0x4f, 0x33,
 * 0x10, 4), each L+0x1d <- 0x11; X+0x50 = X+0x48 = X+0x18, then X+0x50/0x54
 * + the bitmap's words +6/+4 (shifted by sx/sy for byte [0x4d2537] = 3 and
 * byte [0x4d256c] = 0, 16-bit); X+0x58 = (x0 + (0x40 << sx), y0 + (3 <<
 * sy)); X+0x60 = X+0x20 with x - (3 << sx); X+0x34 <- 1. EAX X. */
static int radar_page(hud_ctx *c, uint32_t x, uint32_t rect) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x58u);
    if (!f) return hud_refuse(c, 0x0043e734u, "radar page", "the own code's call area is exhausted");
    f += 0x58u;
    unsigned sx = shx(c), sy = shy(c);
    page_base(c, x, rect);
    hw32(c, x, 0x0049c328u);
    if (HCALL(c, HUD_VA_VECTOR_NEW, NULL, x + 0x68u, 0xcu, 0x64u, 1u, 0x0043f5f5u)) return 1;
    uint32_t table = hr32(c, hr32(c, VA_MFD_BITMAP) + 8u);
    hw32(c, x + 0x10u, hr32(c, table + 0x38u));
    hw32(c, x + 0x14u, hr32(c, table + 0x3cu));
    uint32_t x0 = hr32(c, rect), y0 = hr32(c, rect + 4u);
    hw32(c, x + 0x28u, 0x19u);
    hw32(c, x + 0x20u, (0x41u << sx) + x0);
    hw32(c, x + 0x24u, (0x1cu << sy) + y0);
    hw32(c, x + 0x18u, x0 + (0x26u << sx));
    hw32(c, x + 0x1cu, y0 + (1u << sy));
    hw32(c, x + 0x2cu, 1);
    hw32(c, x + 0x30u, 0);
    uint32_t font = hr32(c, 0x0049b0e0u), r = f - 0x58u;
    hw32(c, f - 0x44u, 0);
    hw32(c, f - 0x48u, 0);
    static const struct { uint8_t a, b, w, flags; } labels[4] = {{0x26, 0x33, 0xc, 1}, {0x4f, 1, 0x10, 4}, {0x26, 1, 0xc, 1}, {0x4f, 0x33, 0x10, 4}};
    for (uint32_t i = 0; i < 4u; ++i) {
        uint32_t l = 0;
        if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u)) return 1;
        hw32(c, x + 0x38u + 4u * i, l);
        hw32(c, r, ((uint32_t)labels[i].a << sx) + x0);
        hw32(c, r + 4u, ((uint32_t)labels[i].b << sy) + y0);
        hw32(c, r + 8u, hr32(c, r) + hsx16(shl16(labels[i].w, sx)));
        hw32(c, r + 12u, hr32(c, r + 4u) + hsx16(shl16(5u, sy)));
        if (label_in_rect(c, l, f - 0x48u, r, labels[i].flags)) return 1;
        hw32(c, l + 0x1du, 0x11u);
    }
    hw32(c, x + 0x50u, hr32(c, x + 0x18u));
    hw32(c, x + 0x54u, hr32(c, x + 0x1cu));
    hw32(c, x + 0x48u, hr32(c, x + 0x50u));
    hw32(c, x + 0x4cu, hr32(c, x + 0x54u));
    int scaled = (int8_t)hr8(c, VA_RESOLUTION) == 3 && !hr8(c, 0x004d256cu);
    uint16_t w = hr16(c, hr32(c, x + 0x10u) + 6u);
    if (scaled) w = shl16(w, sx);
    hw32(c, x + 0x50u, hr32(c, x + 0x50u) + hsx16(w));
    uint16_t hgt = hr16(c, hr32(c, x + 0x10u) + 4u);
    if (scaled) hgt = shl16(hgt, sy);
    hw32(c, x + 0x54u, hr32(c, x + 0x54u) + hsx16(hgt));
    hw32(c, x + 0x58u, x0 + (0x40u << sx));
    hw32(c, x + 0x5cu, y0 + (3u << sy));
    hw32(c, x + 0x60u, hr32(c, x + 0x20u) - (3u << sx));
    hw32(c, x + 0x64u, hr32(c, x + 0x24u));
    hw32(c, x + 0x34u, 1);
    hud_frame_pop(c, top);
    return 0;
}
void es2_sim_service_radar_page(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (radar_page(&c, sc->slots[0], sc->slots[1])) { hud_failed(sc); return; }
    sc->eax = sc->slots[0];
}
/* The widget methods of the button classes' tables (listing
 * 0x455540..0x455560): 0x455540(W): byte W+0x1f <- 2 (redraw wanted), EAX
 * W; 0x45554c(): AX <- word [0x49db6c] (the high word of EAX kept);
 * (0x455557 and 0x45555c are empty bodies: load.c's rows.) */
void es2_sim_service_widget_redraw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    hw8(&c, sc->slots[0] + 0x1fu, 2);
    sc->eax = sc->slots[0];
}
void es2_sim_service_widget_word(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = (sc->eax & 0xffff0000u) | hr16(&c, 0x0049db6cu);
}

/* ======================================================================
 * The throttle gauge and its sliders.
 *
 * 0x4525d0(S) / 0x4526bc(S) (listings 0x4525d0..0x4525fd, 0x4526bc..0x4526ea),
 * the sliders' slot +0x1c: S+0x20 <- ((S+8 - (S+0x36 - S+0x2e) - S+0) <<
 * 16) / (word S+0x26 - word S+0x24) for the horizontal slider, and the same
 * with S+0xc, S+0x3a, S+0x32, S+4 for the vertical one (signed). EAX the
 * quotient, EDX the remainder, ECX S. A zero range (idiv faults) is
 * refused. */
static int slider_scale(hud_ctx *c, uint32_t s, int vertical, es2_sim_result *r) {
    int32_t range = (int32_t)hsx16(hr16(c, s + 0x26u)) - (int32_t)hsx16(hr16(c, s + 0x24u));
    uint32_t knob = vertical ? hr32(c, s + 0x3au) - hr32(c, s + 0x32u) : hr32(c, s + 0x36u) - hr32(c, s + 0x2eu);
    uint32_t span = vertical ? hr32(c, s + 0xcu) - knob - hr32(c, s + 4u) : hr32(c, s + 8u) - knob - hr32(c, s);
    if (!range) return hud_divide_refuse(c, vertical ? 0x004526e3u : 0x004525f6u, 0, 0, "slider", "a zero range (the original's idiv faults)");
    int64_t num = (int64_t)(int32_t)(span << 16);
    hw32(c, s + 0x20u, (uint32_t)(int32_t)(num / range));
    r->eax = (uint32_t)(int32_t)(num / range);
    r->edx = (uint32_t)(int32_t)(num % range);
    r->ecx = s;
    return 0;
}
void es2_sim_service_slider_scale(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    if (slider_scale(&c, sc->slots[0], sc->va == 0x004526bcu, &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
/* 0x4524d0(W, rect, lo, hi, v, step) (listing 0x4524d0..0x45251e): the
 * slider base: 0x4524a0; W+0x17 <- 0x49db8a, W+0x20 <- 0, words W+0x24 <-
 * lo, W+0x26 <- hi, W+0x2a and W+0x28 <- v, W+0x2c <- step, W+0x1d <- 1. */
static void slider_base(hud_ctx *c, uint32_t w, uint32_t rect, uint16_t lo, uint16_t hi, uint16_t v, uint16_t step) {
    widget_init(c, w, rect);
    hw32(c, w + 0x17u, 0x0049db8au);
    hw32(c, w + 0x20u, 0);
    hw16(c, w + 0x24u, lo);
    hw16(c, w + 0x26u, hi);
    hw16(c, w + 0x2au, v);
    hw16(c, w + 0x28u, v);
    hw16(c, w + 0x2cu, step);
    hw16(c, w + 0x1du, 1);
}
/* 0x447e4c(S, rect, G, L) / 0x4483e8(S, rect, G, L) (listings
 * 0x447e4c..0x4480c5, 0x4483e8..0x44865d): the vertical and the horizontal
 * throttle slider: 0x4524d0(S, rect, 0, 0x100, 0, 1); S+0x17 <- 0x49d170 /
 * 0x49df40 and its slot +0x1c; the event base at S+0x3e (0x49dea0, S+0x42 <-
 * G, 0x49df24) with the region at S+0x56, counter + 1; S+0x17/S+0x3e <-
 * 0x49d19c/0x49d1c4 (vertical) or 0x49df6c/0x49df94, word S+0x1b <- 0,
 * counter + 1, then 0x49d12c/0x49d154 or 0x49d0e8/0x49d110; S+0x4e <- the
 * throttle bitmap's entry 1 ([[0x49cfc4]+8]+4); the knob: vertical word
 * S+0x2c <- the bitmap's height (word +4), S+0x2e <- x0, S+0x36 <- x1,
 * S+0x3a <- y1 - ((y1 - y0) >> 1) + (h >> 1), S+0x32 <- S+0x3a - h;
 * horizontal word S+0x2c <- 0xc, S+0x2e <- ((x1 - x0) >> 1) + x0 - 6,
 * S+0x36 <- S+0x2e + 0xc, S+0x32 <- y0, S+0x3a <- y1; S+0x6e <- the knob's
 * corner, the region rectangle S+0x5e <- the knob, S+0x52 <- new[](its
 * area); the bars at L+0x20 and L+0x30 (moved by L's origin) new(0x2c) ->
 * 0x439344 / 0x4390c4 with 0x400 and -0x400 into S+0x7e, S+0x82; S+0x4a,
 * S+0x7a <- 0; words S+0x2a, S+0x28 <- word S+0x3a (vertical) or S+0x2e,
 * S+0x26 <- 0x400, S+0x24 <- 0xfc00; the slot +0x1c again; counter + 1.
 * EAX S. */
static int throttle_slider(hud_ctx *c, uint32_t s, uint32_t rect, uint32_t g, uint32_t l, int vertical) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x40u);
    if (!f) return hud_refuse(c, vertical ? 0x00447e4cu : 0x004483e8u, "throttle slider", "the own code's call area is exhausted");
    f += 0x40u;
    slider_base(c, s, rect, 0, 0x100u, 0, 1);
    hw32(c, s + 0x17u, vertical ? 0x0049d170u : 0x0049df40u);
    if (HVCALL(c, s + 0x17u, 0x1cu, NULL, s)) return 1;
    uint32_t e = s + 0x3eu;
    hw32(c, e, 0x0049dea0u);
    hw32(c, e + 4u, g);
    hw32(c, e, 0x0049df24u);
    region_ctor(c, e + 0x18u);
    hud_counter(c, 1);
    hw32(c, s + 0x17u, vertical ? 0x0049d19cu : 0x0049df6cu);
    hw32(c, s + 0x3eu, vertical ? 0x0049d1c4u : 0x0049df94u);
    hw16(c, s + 0x1bu, 0);
    hud_counter(c, 1);
    hw32(c, s + 0x17u, vertical ? 0x0049d12cu : 0x0049d0e8u);
    hw32(c, s + 0x3eu, vertical ? 0x0049d154u : 0x0049d110u);
    uint32_t ox = hr32(c, l), oy = hr32(c, l + 4u);
    uint32_t bitmap = hr32(c, hr32(c, hr32(c, 0x0049cfc4u) + 8u) + 4u);
    hw32(c, s + 0x4eu, bitmap);
    if (vertical) {
        hw16(c, s + 0x2cu, hr16(c, bitmap + 4u));
        hw32(c, s + 0x2eu, hr32(c, rect));
        hw32(c, s + 0x36u, hr32(c, rect + 8u));
        uint32_t y1 = hr32(c, rect + 0xcu), half = (uint32_t)((int32_t)(y1 - hr32(c, rect + 4u)) >> 1);
        uint32_t h = hsx16(hr16(c, s + 0x2cu));
        hw32(c, s + 0x3au, y1 - half + (uint32_t)((int32_t)h >> 1));
        hw32(c, s + 0x32u, hr32(c, s + 0x3au) - h);
    } else {
        hw16(c, s + 0x2cu, 0xcu);
        uint32_t x0 = hr32(c, rect);
        hw32(c, s + 0x2eu, (uint32_t)((int32_t)(hr32(c, rect + 8u) - x0) >> 1) + x0 - 6u);
        hw32(c, s + 0x36u, hsx16(hr16(c, s + 0x2cu)) + hr32(c, s + 0x2eu));
        hw32(c, s + 0x32u, hr32(c, rect + 4u));
        hw32(c, s + 0x3au, hr32(c, rect + 0xcu));
    }
    hw32(c, s + 0x6eu, hr32(c, s + 0x2eu));
    hw32(c, s + 0x72u, hr32(c, s + 0x32u));
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, s + 0x5eu + i, hr32(c, s + 0x2eu + i));
    uint32_t n = 0, x = 0;
    if (region_area(c, s + 0x5au, &n) || HCALL(c, HUD_VA_NEW_ARRAY, &x, n)) return 1;
    hw32(c, s + 0x52u, x);
    for (uint32_t k = 0; k < 2u; ++k) {
        uint32_t src = l + (k ? 0x30u : 0x20u), b = 0, r = f - 0x40u;
        for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, r + i, hr32(c, src + i) + ((i & 4u) ? oy : ox));
        if (HCALL(c, HUD_VA_NEW, &b, 0x2cu)) return 1;
        if (b && bar_base(c, b, r, k ? 0xfc00u : 0x400u, vertical)) return 1;
        hw32(c, s + (k ? 0x82u : 0x7eu), b);
    }
    hw32(c, s + 0x4au, 0);
    hw32(c, s + 0x7au, 0);
    uint16_t v = hr16(c, s + (vertical ? 0x3au : 0x2eu));
    hw16(c, s + 0x2au, v);
    hw16(c, s + 0x28u, v);
    hw16(c, s + 0x26u, 0x400u);
    hw16(c, s + 0x24u, 0xfc00u);
    if (HVCALL(c, s + 0x17u, 0x1cu, NULL, s)) return 1;
    hud_counter(c, 1);
    hud_frame_pop(c, top);
    return 0;
}
/* 0x447bac(G, R, H) (listing 0x447bac..0x447d5e): the Throttle gauge:
 * 0x438b20; [G] <- 0x49d1e0; when [0x49cfc4] is 0 the bitmap "throttle"
 * (extension [0x4a0a28] for byte [0x4d2537] = 3, else [0x4a0a10]) into
 * it; the slider rectangle R+0x10 moved by R's origin; R+0x40 = 1: the
 * vertical slider (byte G+0xc1 <- 0), else the horizontal one (byte G+0xc1
 * <- 1): new(0x86) -> 0x447e4c / 0x4483e8(S, rect, G, R), counter - 6,
 * G+0x18 <- S; S added to the root; G+0x68 <- 1; _memset(G+0xb1, 0, 8),
 * G+0xb5 <- 0; G+0xc2 <- S's knob corner (S+0x2e, S+0x32) with x + ([R+0x48]
 * << sx). EAX G. A NULL slider (the original reads through it) is
 * refused. */
static int throttle_gauge(hud_ctx *c, uint32_t g, uint32_t r, uint32_t h) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x48u);
    if (!f) return hud_refuse(c, 0x00447bacu, "throttle", "the own code's call area is exhausted");
    f += 0x48u;
    gauge_base(c, g, r, h);
    hw32(c, g, 0x0049d1e0u);
    uint32_t ox = hr32(c, r), oy = hr32(c, r + 4u), root = hr32(c, hr32(c, VA_PANEL));
    if (!hr32(c, 0x0049cfc4u)) {
        uint32_t ext = (int8_t)hr8(c, VA_RESOLUTION) == 3 ? hr32(c, 0x004a0a28u) : hr32(c, 0x004a0a10u), path = 0, x = 0;
        if (HCALL(c, HUD_VA_PATH_DIR, &path, 0x0049d0d0u, ext) || HCALL(c, HUD_VA_LOAD_BY_NAME, &x, path)) return 1;
        hw32(c, 0x0049cfc4u, x);
    }
    uint32_t rect = f - 0x48u, s = 0;
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, rect + i, hr32(c, r + 0x10u + i) + ((i & 4u) ? oy : ox));
    int vertical = hr32(c, r + 0x40u) == 1u;
    if (HCALL(c, HUD_VA_NEW, &s, 0x86u)) return 1;
    if (!s) return hud_null_refuse(c, 0x00447d24u, "throttle", "operator new returned NULL (the original reads through it)");
    if (throttle_slider(c, s, rect, g, r, vertical)) return 1;
    hud_counter(c, -6);
    hw32(c, g + 0x18u, s);
    hw8(c, g + 0xc1u, vertical ? 0 : 1);
    widget_add(c, root, hr32(c, g + 0x18u));
    hw32(c, g + 0x68u, 1);
    if (HCALL(c, HUD_VA_MEMSET, NULL, g + 0xb1u, 0u, 8u)) return 1;
    hw32(c, g + 0xb5u, 0);
    s = hr32(c, g + 0x18u);
    hw32(c, g + 0xc2u, hr32(c, s + 0x2eu));
    hw32(c, g + 0xc6u, hr32(c, s + 0x32u));
    hw32(c, g + 0xc2u, hr32(c, g + 0xc2u) + (hr32(c, r + 0x48u) << shx(c)));
    hud_frame_pop(c, top);
    return 0;
}
void es2_sim_service_throttle_gauge(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (throttle_gauge(&c, sc->slots[0], sc->slots[1], sc->slots[2])) { hud_failed(sc); return; }
    sc->eax = sc->slots[0];
}
/* ======================================================================
 * The HUD (0x43c800) and its gadgets.
 *
 * 0x4824ec(font, text) (listing 0x4824ec..0x482519): the text width, the
 * sum of 0x482498(font, ch) per byte (0x482498..0x4824e8: the width of
 * the zero-extended character c is the byte [[font+0x2a] + c - word
 * font+8] when word font+0xc is -1, else word font+0xc; its _sprintf into a
 * local buffer is dead). */
static uint32_t text_width(hud_ctx *c, uint32_t font, uint32_t text) {
    uint32_t sum = 0;
    for (uint8_t ch; (ch = hr8(c, text)) != 0; ++text) {
        uint16_t w;
        if (hr16(c, font + 0xcu) == 0xffffu) w = hr8(c, hr32(c, font + 0x2au) + (uint32_t)ch - hsx16(hr16(c, font + 8u)));
        else w = hr16(c, font + 0xcu);
        sum += hsx16(w);
    }
    return sum;
}
/* 0x43b36c(D, rect) (listing 0x43b36c..0x43b39d): [D] <- 0x49c26c, rect to
 * D+4. 0x43b3a0 (0x43b3a0..0x43b3e4): 0x43b36c, [D] <- 0x49c24c, words
 * D+0x24 <- 0xb800, D+0x26 <- 0x4800, D+0x2c <- their difference, D+0x3a <-
 * their 16-bit sum >> 1 (logical). 0x43b460 (0x43b460..0x43b485): 0x43b3a0,
 * [D] <- 0x49c22c, its slot +8. 0x43b574 (0x43b574..0x43b592): 0x43b3a0,
 * [D] <- 0x49c20c. 0x43b950 (0x43b950..0x43b977): 0x43b36c, [D] <-
 * 0x49c1d8, 0x49c1c4. 0x43c268 (0x43c268..0x43c286): 0x43b950, [D] <-
 * 0x49c174. */
static void gadget_base(hud_ctx *c, uint32_t d, uint32_t rect) {
    hw32(c, d, 0x0049c26cu);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, d + 4u + i, hr32(c, rect + i));
}
static void gadget_range(hud_ctx *c, uint32_t d, uint16_t lo, uint16_t hi) {
    hw16(c, d + 0x24u, lo);
    hw16(c, d + 0x26u, hi);
    hw16(c, d + 0x2cu, (uint16_t)(hi - lo));
    hw16(c, d + 0x3au, (uint16_t)(lo + hi));
    hw16(c, d + 0x3au, (uint16_t)(hr16(c, d + 0x3au) >> 1));
}
static void gadget_scale_base(hud_ctx *c, uint32_t d, uint32_t rect) {
    gadget_base(c, d, rect);
    hw32(c, d, 0x0049c24cu);
    gadget_range(c, d, 0xb800u, 0x4800u);
}
static int gadget_tape(hud_ctx *c, uint32_t d, uint32_t rect) {
    gadget_scale_base(c, d, rect);
    hw32(c, d, 0x0049c22cu);
    return HVCALL(c, d, 8u, NULL, d);
}
/* 0x43b488(D) (listing 0x43b488..0x43b4c9): D+0x28 <- x1 - x0; D+0x2e <-
 * (x0 - (0 << sx), y0 - (2 << sy)). 0x43b604(D) (0x43b604..0x43b67a): D+0x28
 * <- x1 - x0, D+0x3c <- 0, with the tick bitmap T = [0x49be70] (n = [T+4]):
 * D+0x40 <- ((n * D+0x28) << 16, rounded toward zero) >> 16, D+0x44 <- x0,
 * D+0x48 <- [[T+8] + 4 (n - 1)], D+0x4c <- [[T+8]], words D+0x50 <- 1,
 * D+0x52 <- n - 2 (n - 1 when that is negative as a word). */
void es2_sim_service_tape_layout(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t d = sc->slots[0];
    hw32(&c, d + 0x28u, hr32(&c, d + 0xcu) - hr32(&c, d + 4u));
    hw32(&c, d + 0x2eu, hr32(&c, d + 4u));
    hw32(&c, d + 0x32u, hr32(&c, d + 8u));
    hw32(&c, d + 0x2eu, hr32(&c, d + 0x2eu) - (0u << shx(&c)));
    hw32(&c, d + 0x32u, hr32(&c, d + 0x32u) - (2u << shy(&c)));
    sc->eax = d;
}
void es2_sim_service_tick_layout(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t d = sc->slots[0], t = hr32(&c, 0x0049be70u), n = hr32(&c, t + 4u);
    hw32(&c, d + 0x28u, hr32(&c, d + 0xcu) - hr32(&c, d + 4u));
    hw32(&c, d + 0x3cu, 0);
    int32_t v = (int32_t)((n * hr32(&c, d + 0x28u)) << 16);
    if (v < 0) v += 0xffff;
    hw32(&c, d + 0x40u, (uint32_t)(v >> 16));
    hw32(&c, d + 0x44u, hr32(&c, d + 4u));
    hw32(&c, d + 0x48u, hr32(&c, hr32(&c, t + 8u) + 4u * (n - 1u)));
    hw32(&c, d + 0x4cu, hr32(&c, hr32(&c, t + 8u)));
    hw16(&c, d + 0x50u, 1);
    uint16_t k = (uint16_t)(n - 2u);
    hw16(&c, d + 0x52u, (int16_t)k < 0 ? (uint16_t)(n - 1u) : k);
    sc->eax = d;
}
/* 0x43b3e8(D, lo, hi) (listing 0x43b3e8..0x43b41e): the range words as in
 * 0x43b3a0 from the arguments, then D's slot +8. 0x43b7dc(D, lo, hi)
 * (0x43b7dc..0x43b807): D+0x40 <- (hi - lo) / D+0x28 (sign-extended words,
 * signed; a zero divisor is refused), then 0x43b3e8. */
static int gadget_set_range(hud_ctx *c, uint32_t d, uint32_t lo, uint32_t hi) {
    gadget_range(c, d, (uint16_t)lo, (uint16_t)hi);
    return HVCALL(c, d, 8u, NULL, d);
}
void es2_sim_service_gadget_range(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (gadget_set_range(&c, sc->slots[0], sc->slots[1], sc->slots[2])) { hud_failed(sc); return; }
}
void es2_sim_service_tick_range(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t d = sc->slots[0];
    int32_t div = (int32_t)hr32(&c, d + 0x28u);
    if (!div) { hud_divide_refuse(&c, 0x0043b7f3u, 0, 0, "tick gadget", "a zero width (the original's idiv faults)"); hud_failed(sc); return; }
    hw32(&c, d + 0x40u, (uint32_t)(((int32_t)hsx16((uint16_t)sc->slots[2]) - (int32_t)hsx16((uint16_t)sc->slots[1])) / div));
    if (gadget_set_range(&c, d, sc->slots[1], sc->slots[2])) { hud_failed(sc); return; }
}
/* 0x43b5a4(D, rect) (listing 0x43b5a4..0x43b601): the heading tape:
 * 0x43b460, [D] <- 0x49c1ec; when [0x49be70] is 0 the bitmap "hudhtick"
 * (extension "hba" with byte [0x4d256c], else "dba") into it; D's slot +8
 * (0x43b604). 0x43c148(D, rect) (0x43c148..0x43c202): 0x43b36c, [D] <-
 * 0x49c1d8, 0x49c1b0; a widget W = new(0x20) on rect (0x4524a0, W+0x17 <-
 * 0x49defc, word W+0x1d <- 0, W+0x17 <- 0x49c188, word W+0x1b <- 0) into
 * D+0x4a, added to the root. 0x43c290(D, rect) (0x43c290..0x43c3f4): 0x43b460,
 * [D] <- 0x49c154, bytes D+0x4d, D+0x4c <- 0, a label (font [0x49b0f0]) in
 * rect moved down by 3 << sy into D+0x4e (0x438884 flags 2), and
 * 0x4d1808..0x4d1824 <- {5, 5, 4, 5, 0, -4, 2, -4} shifted alternately by
 * sx and sy. */
static int heading_tape(hud_ctx *c, uint32_t d, uint32_t rect) {
    if (gadget_tape(c, d, rect)) return 1;
    hw32(c, d, 0x0049c1ecu);
    if (!hr32(c, 0x0049be70u) && load_bitmap(c, 0x0049c0e8u, 0x0049c0f1u, 0x0049c0f5u, 0x0049be70u)) return 1;
    return HVCALL(c, d, 8u, NULL, d);
}
static int pip_gadget(hud_ctx *c, uint32_t d, uint32_t rect) {
    gadget_base(c, d, rect);
    hw32(c, d, 0x0049c1d8u);
    hw32(c, d, 0x0049c1b0u);
    uint32_t root = hr32(c, hr32(c, VA_PANEL)), w = 0;
    if (HCALL(c, HUD_VA_NEW, &w, 0x20u)) return 1;
    if (w) {
        widget_init(c, w, rect);
        hw32(c, w + 0x17u, 0x0049defcu);
        hw16(c, w + 0x1du, 0);
        hw32(c, w + 0x17u, 0x0049c188u);
        hw16(c, w + 0x1bu, 0);
    }
    hw32(c, d + 0x4au, w);
    widget_add(c, root, hr32(c, d + 0x4au));
    return 0;
}
static int speed_tape(hud_ctx *c, uint32_t d, uint32_t rect) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x3cu);
    if (!f) return hud_refuse(c, 0x0043c290u, "speed tape", "the own code's call area is exhausted");
    f += 0x3cu;
    if (gadget_tape(c, d, rect)) return 1;
    hw32(c, d, 0x0049c154u);
    hw8(c, d + 0x4du, 0);
    hw8(c, d + 0x4cu, 0);
    hw32(c, f - 0x2cu, 0);
    hw32(c, f - 0x28u, 0);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, f - 0x3cu + i, hr32(c, rect + i));
    uint32_t step = 3u << shy(c), l = 0;
    hw32(c, f - 0x38u, hr32(c, f - 0x38u) + step);
    hw32(c, f - 0x30u, hr32(c, f - 0x30u) + step);
    if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, hr32(c, 0x0049b0f0u), 1u)) return 1;
    hw32(c, d + 0x4eu, l);
    if (label_in_rect(c, l, f - 0x2cu, f - 0x3cu, 2u)) return 1;
    static const int32_t marks[8] = {5, 5, 4, 5, 0, -4, 2, -4};
    for (uint32_t i = 0; i < 8u; ++i) hw32(c, 0x004d1808u + 4u * i, (uint32_t)marks[i]);
    for (uint32_t i = 0; i < 8u; ++i) shl32(c, 0x004d1808u + 4u * i, (i & 1u) ? shy(c) : shx(c));
    hud_frame_pop(c, top);
    return 0;
}
/* 0x43d5cc(G, D) (listing 0x43d5cc..0x43d5ed): a non-NULL gadget into the
 * HUD's list G+0xd7[G+0xff++]. */
static void hud_add(hud_ctx *c, uint32_t g, uint32_t d) {
    if (!d) return;
    uint32_t n = hr32(c, g + 0xffu);
    hw32(c, g + 0xffu, n + 1u);
    hw32(c, g + 0xd7u + 4u * n, d);
}
/* The gadget record copy of 0x43c800 (G+0xb1 to D+0x14; listing
 * 0x43cbfb..0x43cd11): words +0, +2, +4, dword +6, words +0x16, +0x18,
 * +0x1a, dwords +0x1c, +0x20, bytes +0x24, +0x25, then the dwords +0x12,
 * +0xa, +0xe (the order of the stores). */
static void hud_record_copy(hud_ctx *c, uint32_t s, uint32_t t) {
    hw16(c, t, hr16(c, s));
    hw16(c, t + 2u, hr16(c, s + 2u));
    hw16(c, t + 4u, hr16(c, s + 4u));
    hw32(c, t + 6u, hr32(c, s + 6u));
    hw16(c, t + 0x16u, hr16(c, s + 0x16u));
    hw16(c, t + 0x18u, hr16(c, s + 0x18u));
    hw16(c, t + 0x1au, hr16(c, s + 0x1au));
    hw32(c, t + 0x1cu, hr32(c, s + 0x1cu));
    hw32(c, t + 0x20u, hr32(c, s + 0x20u));
    hw8(c, t + 0x24u, hr8(c, s + 0x24u));
    hw8(c, t + 0x25u, hr8(c, s + 0x25u));
    hw32(c, t + 0x12u, hr32(c, s + 0x12u));
    hw32(c, t + 0xau, hr32(c, s + 0xau));
    hw32(c, t + 0xeu, hr32(c, s + 0xeu));
}
/* 0x486e7c(p): gfx.c's row (es2_sim_gfx_cursor_point_set) through the
 * dispatch; it also keeps the point in 0x4a364a when [0x4a3646] is 0. */
static int pen_set(hud_ctx *c, uint32_t p) { return HCALL(c, 0x00486e7cu, NULL, p); }
/* 0x4808dc(dev, p) (listing 0x4808dc..0x480947): GetClientRect(hwnd, 0x6c5f5a)
 * (the input layer's service, result not read); the local point x = ([p] *
 * [0x6c5f62]) / ([0x4d3040] >> 1), then y = ([p+4] * [0x6c5f66]) / ([0x4d3044]
 * >> 1) (imul's low dword, cdq, idiv: a zero divisor or 0x80000000 / -1 faults
 * in the original and is refused at its idiv); ClientToScreen(hwnd, &point) and
 * SetCursorPos(x, y) through their rows (headless no effect, windowed the
 * window's origin and the real cursor; es2/sim/cursor.h). `dev` is not read. */
static int cursor_divide(hud_ctx *c, uint32_t site, uint32_t a, uint32_t b, uint32_t half, int32_t *out) {
    int32_t n = (int32_t)(a * b), d = (int32_t)hr32(c, half) >> 1;
    if (!d || (n == INT32_MIN && d == -1)) return hud_divide_refuse(c, site, (uint64_t)(int64_t)n, (uint32_t)d, "cursor place", "the original's idiv faults (zero or overflowing quotient)");
    *out = n / d;
    return 0;
}
static int cursor_place(hud_ctx *c, uint32_t p) {
    if (HCALL(c, 0x00496970u, NULL, hr32(c, 0x004d3610u), 0x006c5f5au)) return 1;
    int32_t x = 0, y = 0;
    if (cursor_divide(c, 0x00480909u, hr32(c, p), hr32(c, 0x006c5f62u), 0x004d3040u, &x) ||
        cursor_divide(c, 0x00480922u, hr32(c, p + 4u), hr32(c, 0x006c5f66u), 0x004d3044u, &y))
        return 1;
    uint32_t top = c->l->stack_hi, pt = hud_frame_push(c, 8u);
    if (!pt) return hud_refuse(c, 0x004808dcu, "cursor place", "the own code's call area is exhausted");
    hw32(c, pt, (uint32_t)x);
    hw32(c, pt + 4u, (uint32_t)y);
    if (HCALL(c, 0x0049699au, NULL, hr32(c, 0x004d3610u), pt) ||
        HCALL(c, 0x004969e2u, NULL, hr32(c, pt), hr32(c, pt + 4u)))
        return 1;
    hud_frame_pop(c, top);
    return 0;
}
static int moved(hud_ctx *c, uint32_t dst, uint32_t src, uint32_t ox, uint32_t oy) {
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, dst + i, hr32(c, src + i) + ((i & 4u) ? oy : ox));
    return 0;
}
static int hud_gadget(hud_ctx *c, uint32_t g, uint32_t size, int kind, uint32_t rect, uint32_t *out) {
    (void)g;
    uint32_t d = 0;
    if (HCALL(c, HUD_VA_NEW, &d, size)) return 1;
    if (d) {
        int bad = 0;
        switch (kind) {
        case 0: bad = pip_gadget(c, d, rect); break;
        case 1: bad = heading_tape(c, d, rect); break;
        case 2: bad = gadget_tape(c, d, rect); break;
        case 3: gadget_scale_base(c, d, rect); hw32(c, d, 0x0049c20cu); break;
        case 4: gadget_base(c, d, rect); hw32(c, d, 0x0049c1d8u); hw32(c, d, 0x0049c124u); break;
        case 5: gadget_base(c, d, rect); hw32(c, d, 0x0049c1d8u); hw32(c, d, 0x0049c1c4u); break;
        case 6: gadget_base(c, d, rect); hw32(c, d, 0x0049c1d8u); hw32(c, d, 0x0049c1c4u); hw32(c, d, 0x0049c174u); break;
        default: bad = speed_tape(c, d, rect); break;
        }
        if (bad) return 1;
    }
    *out = d;
    return 0;
}
static int hud_label(hud_ctx *c, uint32_t font, uint32_t off, uint32_t rect, uint32_t flags, uint32_t store) {
    uint32_t l = 0;
    if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u)) return 1;
    hw32(c, store, l);
    return label_in_rect(c, hr32(c, store), off, rect, flags);
}
static int hud_ctor(hud_ctx *c, uint32_t g, uint32_t r, uint32_t h) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x134u);
    if (!f) return hud_refuse(c, 0x0043c800u, "HUD", "the own code's call area is exhausted");
    f += 0x134u;
    unsigned sx = shx(c), sy = shy(c);
    gauge_base(c, g, r, h);
    hw32(c, g, 0x0049c138u);
    if (!hr32(c, 0x0049be78u)) {
        uint32_t ext = (int8_t)hr8(c, VA_RESOLUTION) == 3 ? hr32(c, 0x004a0a28u) : hr32(c, 0x004a0a10u), path = 0, x = 0;
        if (HCALL(c, HUD_VA_PATH_DIR, &path, 0x0049c100u, ext) || HCALL(c, HUD_VA_LOAD_BY_NAME, &x, path)) return 1;
        hw32(c, 0x0049be78u, x);
    }
    uint32_t ox = hr32(c, r), oy = hr32(c, r + 4u), rect = f - 0x134u, d = 0;
    moved(c, rect, r + 0x3cu, ox, oy);
    if (hud_gadget(c, g, 0x4eu, 0, rect, &d)) return 1;
    hud_add(c, g, d);
    if (pen_set(c, r + 0x30u)) return 1;
    uint32_t dev = 0;
    uint32_t id = hr32(c, 0x004d24a0u);
    if (id) {
        uint32_t o = hr32(c, 0x004d2468u + 4u * id);
        dev = o ? o - 0x3au : 0;
    }
    if (dev) {
        hw32(c, f - 0x9cu, hr32(c, r + 0x30u));
        hw32(c, f - 0x98u, hr32(c, 0x004d2542u) - 0x14u);
        if (cursor_place(c, f - 0x9cu)) return 1;
    }
    hw32(c, f - 0xa8u, hr32(c, r + 0x30u) << 1);
    hw32(c, f - 0xa4u, hr32(c, r + 0x34u));
    hw16(c, f - 0xa0u, 0);
    if (HCALL(c, 0x00480840u, NULL, dev, f - 0xa8u, 1u)) return 1;
    moved(c, rect, r + 0x10u, ox, oy);
    if (hud_gadget(c, g, 0x54u, 1, rect, &d)) return 1;
    if (!d) return hud_null_refuse(c, 0x0043ca6bu, "HUD", "operator new returned NULL (the original calls through it)");
    hud_add(c, g, d);
    if (HVCALL(c, d, 0x10u, NULL, d, 0xf1c8u, 0xe38u)) return 1;
    hw32(c, rect, hr32(c, rect) + (0xfu << sx));
    hw32(c, rect + 4u, hr32(c, rect + 4u) + (0xfffffff6u << sy));
    hw32(c, rect + 8u, (0x5au << sx) + hr32(c, rect));
    hw32(c, rect + 12u, (4u << sy) + hr32(c, rect + 4u));
    if (hud_gadget(c, g, 0x3cu, 2, rect, &d)) return 1;
    if (!d) return hud_null_refuse(c, 0x0043cb12u, "HUD", "operator new returned NULL (the original calls through it)");
    hud_add(c, g, d);
    if (HVCALL(c, d, 0x10u, NULL, d, 0xc71du, 0x38e3u)) return 1;
    if (hud_gadget(c, g, 0x3cu, 3, rect, &d)) return 1;
    hud_add(c, g, d);
    uint32_t rad = hr32(c, r + 0x38u);
    hw32(c, rect, hr32(c, r + 0x30u) - rad + ox);
    hw32(c, rect + 4u, hr32(c, r + 0x34u) - rad + oy);
    hw32(c, rect + 8u, hr32(c, r + 0x30u) + rad + ox);
    hw32(c, rect + 12u, hr32(c, r + 0x34u) + rad + oy);
    for (int k = 0; k < 3; ++k) {
        if (k == 1) moved(c, rect, r + 0x3cu, ox, oy);
        if (hud_gadget(c, g, k == 0 ? 0x4au : 0x5au, 4 + k, rect, &d)) return 1;
        if (!d) return hud_null_refuse(c, 0x0043cc13u, "HUD", "operator new returned NULL (the original writes through it)");
        hud_record_copy(c, g + 0xb1u, d + 0x14u);
        if (k < 2) {
            hw32(c, d + 0x42u, hr32(c, r + 0x30u));
            hw32(c, d + 0x46u, hr32(c, r + 0x34u));
        }
        hud_add(c, g, d);
    }
    moved(c, rect, r + 0x10u, ox, oy);
    for (int k = 0; k < 2; ++k) {
        if (hud_gadget(c, g, 0x52u, 7, rect, &d)) return 1;
        if (!d) return hud_null_refuse(c, 0x0043d095u, "HUD", "operator new returned NULL (the original calls through it)");
        hud_add(c, g, d);
        if (HVCALL(c, d, 0x10u, NULL, d, 0xf1c8u, 0xe38u)) return 1;
        if (!k) hw8(c, d + 0x45u, 1);
    }
    uint32_t font = hr32(c, 0x0049b0dcu), off = f - 0x6cu;
    hw32(c, f - 0x68u, 0);
    hw32(c, f - 0x6cu, 0);
    moved(c, rect, r + 0x4cu, ox, oy);
    if (hud_label(c, font, off, rect, 2u, g + 0x103u)) return 1;
    moved(c, rect, r + 0x5cu, ox, oy);
    if (hud_label(c, font, off, rect, 2u, g + 0x107u)) return 1;
    hw32(c, g + 0x68u, 0);
    if (HCALL(c, HUD_VA_MEMSET, NULL, g + 0xb1u, 0u, 0x26u)) return 1;
    hw16(c, g + 0x74u, 0);
    hw8(c, 0x004d17ddu, 0x39);
    hw8(c, 0x004d17dcu, 0x39);
    hw8(c, 0x004d17deu, 0);
    hw8(c, 0x004d17dfu, 0x35);
    hw8(c, 0x004d17e0u, 0x39);
    hw8(c, 0x004d17e2u, 0);
    hw8(c, 0x004d17e2u, 0x3a);
    hw8(c, 0x004d17e3u, 0);
    hw32(c, 0x004d1804u, 0);
    font = hr32(c, 0x0049b0ecu);
    hw32(c, rect + 4u, hr32(c, r + 0x24u));
    hw32(c, rect + 8u, hr32(c, r + 0x20u));
    hw32(c, rect, hr32(c, rect + 8u) - text_width(c, font, 0x0049c104u));
    hw32(c, rect + 12u, (5u << sy) + hr32(c, rect + 4u));
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, rect + i, hr32(c, rect + i) + ((i & 4u) ? oy : ox));
    if (hud_label(c, font, off, rect, 1u, 0x004d17d0u)) return 1;
    hw32(c, rect + 8u, hr32(c, rect) - (2u << sx));
    hw32(c, rect, hr32(c, rect + 8u) - text_width(c, font, hr32(c, 0x004d1658u)));
    if (hud_label(c, font, off, rect, 1u, 0x004d17ccu)) return 1;
    hw32(c, rect, hr32(c, r + 0x28u));
    hw32(c, rect + 4u, hr32(c, r + 0x2cu));
    hw32(c, rect + 12u, (5u << sy) + hr32(c, rect + 4u));
    hw32(c, rect + 8u, text_width(c, font, hr32(c, 0x004d165cu)) + hr32(c, rect));
    hw32(c, rect, hr32(c, rect) + ox);
    hw32(c, rect + 4u, hr32(c, rect + 4u) + oy);
    if (hud_label(c, font, off, rect, 1u, 0x004d17d4u)) return 1;
    hw32(c, rect, (2u << sx) + hr32(c, rect + 8u));
    hw32(c, rect + 8u, text_width(c, font, 0x0049c10au) + hr32(c, rect));
    if (hud_label(c, font, off, rect, 1u, 0x004d17d8u)) return 1;
    hw32(c, hr32(c, 0x004d17d8u), hr32(c, 0x0049b0f0u));
    hw32(c, hr32(c, 0x004d17d0u), hr32(c, 0x0049b0f0u));
    hw32(c, g + 0x10bu, hr32(c, r + 0x6cu));
    hw32(c, g + 0x10fu, hr32(c, r + 0x70u));
    hw32(c, g + 0x113u, hr32(c, r + 0x30u));
    hw32(c, g + 0x117u, hr32(c, r + 0x34u));
    hw32(c, g + 0x113u, hr32(c, g + 0x113u) + (0x46u << sx));
    hw32(c, g + 0x117u, hr32(c, g + 0x117u) - (0x12u << sy));
    hud_frame_pop(c, top);
    return 0;
}
void es2_sim_service_hud_ctor(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (hud_ctor(&c, sc->slots[0], sc->slots[1], sc->slots[2])) { hud_failed(sc); return; }
    sc->eax = sc->slots[0];
}
/* ======================================================================
 * The HDD (0x448cf0), its map page (0x44c28c) and its status page
 * (0x4507c4).
 *
 * 0x448cd4(X, rect, H) (listing 0x448cd4..0x448ced): the HDD page base:
 * [X] <- 0x49d668, X+4 <- rect, X+8 <- H. 0x44c10f(rec) (0x44c10f..0x44c13b):
 * the radio record's constructor, 0x49667c(rec+0x3d, 8, 30, 1, 0x49545b).
 * EAX rec. */
static void hdd_page_base(hud_ctx *c, uint32_t x, uint32_t rect, uint32_t h) {
    hw32(c, x, 0x0049d668u);
    hw32(c, x + 4u, rect);
    hw32(c, x + 8u, h);
}
void es2_sim_service_radio_record_ctor(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (HCALL(&c, HUD_VA_VECTOR_NEW, NULL, sc->slots[0] + 0x3du, 8u, 0x1eu, 1u, HUD_VA_EMPTY_CTOR)) { hud_failed(sc); return; }
    sc->eax = sc->slots[0];
}
/* 0x44eec8(M) (listing 0x44eec8..0x44ef07): the map zoom bar: M+0x145 <-
 * (int32)(((M+0x20 - 60000) >> 8) * 0xafc8) / ([0x4d1ce0] - 60000), << 8,
 * + 5000 (signed; a zero divisor is refused). 0x44cfc4(M) (0x44cfc4..0x44cff4):
 * when M+0x20 - [0x4d1ce4] >= 60000 (signed): M+0x20 - [0x4d1ce4],
 * 0x44eec8, byte M+0x30 <- 1, byte 0x49d6ae <- 1 (the map redraw). */
static int map_zoom_bar(hud_ctx *c, uint32_t m) {
    int32_t b = (int32_t)hr32(c, 0x004d1ce0u) - 60000;
    if (!b) return hud_divide_refuse(c, 0x0044eef3u, 0, 0, "map zoom", "a zero range (the original's idiv faults)");
    int32_t a = (int32_t)(hr32(c, m + 0x20u) - 60000u) >> 8;
    int32_t q = (int32_t)((uint32_t)a * 0xafc8u) / b;
    hw32(c, m + 0x145u, ((uint32_t)q << 8) + 5000u);
    return 0;
}
static int map_zoom_out(hud_ctx *c, uint32_t m) {
    uint32_t step = hr32(c, 0x004d1ce4u);
    if ((int32_t)(hr32(c, m + 0x20u) - step) < 60000) return 0;
    hw32(c, m + 0x20u, hr32(c, m + 0x20u) - step);
    if (map_zoom_bar(c, m)) return 1;
    hw8(c, m + 0x30u, 1);
    hw8(c, 0x0049d6aeu, 1);
    return 0;
}
void es2_sim_service_map_zoom_bar(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (map_zoom_bar(&c, sc->slots[0])) { hud_failed(sc); return; }
}
void es2_sim_service_map_zoom_out(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (map_zoom_out(&c, sc->slots[0])) { hud_failed(sc); return; }
}
/* 0x44f678(W, rect, M) (listing 0x44f678..0x44f6d3): the map's drag
 * widget: 0x4524a0; W+0x17 <- 0x49defc, word W+0x1d <- 0, W+0x20 <-
 * 0x49dea0, W+0x24 <- 0, W+0x17 <- 0x49d904, W+0x20 <- 0x49d928, word
 * W+0x1b <- 0, W+0x17 <- 0x49d944, W+0x20 <- 0x49d968, W+0x28 <- M.
 * 0x44f158(I, rect, M) (0x44f158..0x44f1ba) with 0x44bebc (0x44bebc..0x44bef4):
 * a map icon: 0x4524a0; I+0x17 <- 0x49defc, word I+0x1d <- 0, I+0x17 <-
 * 0x49d5e4, I+0x20 <- M, word I+0x1b <- 0; I+0x17 <- 0x49d9b0; when
 * [0x49d6b4] is 0 the bitmap "icons" (extension [0x4a0a28] for byte
 * [0x4d2537] = 3, else [0x4a0a10]) into it; byte I+0x36 <- 0. */
static void map_drag(hud_ctx *c, uint32_t w, uint32_t rect, uint32_t m) {
    widget_init(c, w, rect);
    hw32(c, w + 0x17u, 0x0049defcu);
    hw16(c, w + 0x1du, 0);
    hw32(c, w + 0x20u, 0x0049dea0u);
    hw32(c, w + 0x24u, 0);
    hw32(c, w + 0x17u, 0x0049d904u);
    hw32(c, w + 0x20u, 0x0049d928u);
    hw16(c, w + 0x1bu, 0);
    hw32(c, w + 0x17u, 0x0049d944u);
    hw32(c, w + 0x20u, 0x0049d968u);
    hw32(c, w + 0x28u, m);
}
static int map_icon(hud_ctx *c, uint32_t i, uint32_t rect, uint32_t m) {
    widget_init(c, i, rect);
    hw32(c, i + 0x17u, 0x0049defcu);
    hw16(c, i + 0x1du, 0);
    hw32(c, i + 0x17u, 0x0049d5e4u);
    hw32(c, i + 0x20u, m);
    hw16(c, i + 0x1bu, 0);
    hw32(c, i + 0x17u, 0x0049d9b0u);
    if (!hr32(c, 0x0049d6b4u)) {
        uint32_t ext = (int8_t)hr8(c, VA_RESOLUTION) == 3 ? hr32(c, 0x004a0a28u) : hr32(c, 0x004a0a10u), path = 0, x = 0;
        if (HCALL(c, HUD_VA_PATH_DIR, &path, 0x0049d8d7u, ext) || HCALL(c, HUD_VA_LOAD_BY_NAME, &x, path)) return 1;
        hw32(c, 0x0049d6b4u, x);
    }
    hw8(c, i + 0x36u, 0);
    return 0;
}
/* 0x44c28c(M, r1, r2, H) (listing 0x44c28c..0x44c8ba): the HDD map page:
 * 0x448cd4(M, r1, H), [M] <- 0x49d9d8; the three pick records
 * 0x49667c(M+0x85, 0x2a, 3, 1, 0x450617); M+0x149 <- clock + 30 (the
 * construction clock read, site 0x44c303); the map rectangle M+0x71 = (r1.x0
 * + (4 << sx), r1.y0 + (2 << sy), r2.x0 - (4 << sx), r1.y1 - (2 << sy))
 * (16-bit shifts); the panel context pushed (0x4311e0); the map context O =
 * new(0x239) -> 0x480c34, counter - 2, M+0x10 <- O, O+0x220/O+0x224 <- the
 * negative half extents of M+0x71, O+0x208, O+0x20c <- 1; M+0x18, M+0x1c
 * <- 0; the map bounds 0x4aa640.. to M+0x160; M+0x20 <- the smaller of
 * (half width / -O+0x220) << 8 and (half height / -O+0x224) << 8 (the
 * scale; 0x49d6bc = 8), [0x4d1ce0] <- it, [0x4d1ce4] <- max((it - 60000) /
 * 25, 5000); the camera new(0x22) -> 0x48bda4(K, O, 8, 0x100, M+0x18,
 * 0x49d6c0), counter - 2, M+0x14; 0x44eec8, 0x44cfc4; the drag widgets on
 * r2 (M+0x35) and on M+0x71 (M+0x39) added to the root; the order title
 * label (font [0x49b0b4], row height (r2.y1 - r2.y0) / 9) into M+0x13a
 * with L+0x1d <- word [0x4d3b98], eight row labels (0x438a70 with
 * [0x49b0b0], offset (5, 0), L+0x21 <- [0x49b0b4]) into M+0x3d..; 0x44ee24
 * (bytes M+0x131..0x138 <- 0, M+0x139 <- 1); M+0x81, M+0x5d <- -1, M+0x61
 * <- r2.x0 + 1, M+0x69 <- that + 2; M+0x31 <- new[](0x230) and its 140 map
 * icons on a zero rectangle (0x44f158); the colour tables 0x4d1cd0/0x4d1cd8
 * from every second byte of 0x49d68c/0x49d69c; byte M+0x109, word M+0x10a,
 * byte 0x49d6ae <- 0, byte M+0x30 <- 1, byte 0x49d6ad <- 1, [0x49d6b0] <-
 * M; the context popped; the pick records' bytes +0xa5/+0xa6 <- 0; M+0x105
 * <- -1, bytes M+0x380, M+0x381 <- 0, M+0x118, M+0x114 <- 0, byte M+0x11c
 * <- 0, M+0x12d, M+0x129, M+0x125 <- 0, word M+0x13e <- 0, bytes M+0x144,
 * M+0x15a <- 0. EAX M. NULL objects written through are refused. */
static int map_page(hud_ctx *c, uint32_t m, uint32_t r1, uint32_t r2, uint32_t h) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0xa0u);
    if (!f) return hud_refuse(c, 0x0044c28cu, "map page", "the own code's call area is exhausted");
    f += 0xa0u;
    unsigned sx = shx(c), sy = shy(c);
    hdd_page_base(c, m, r1, h);
    hw32(c, m, 0x0049d9d8u);
    if (HCALL(c, HUD_VA_VECTOR_NEW, NULL, m + 0x85u, 0x2au, 3u, 1u, 0x00450617u)) return 1;
    uint32_t now = 0;
    if (hud_clock(c, 0x0044c303u, &now)) return 1;
    hw32(c, m + 0x149u, now + 0x1eu);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, m + 0x71u + i, hr32(c, r1 + i));
    hw32(c, m + 0x71u, hr32(c, m + 0x71u) + hsx16(shl16(4u, sx)));
    hw32(c, m + 0x75u, hr32(c, m + 0x75u) + hsx16(shl16(2u, sy)));
    hw32(c, m + 0x79u, hr32(c, r2) - hsx16(shl16(4u, sx)));
    hw32(c, m + 0x7du, hr32(c, m + 0x7du) - hsx16(shl16(2u, sy)));
    es2_sim_result r;
    if (hud_call_regs(c, 0x004311e0u, NULL, 0, NULL, &r)) return 1;
    uint32_t o = 0;
    if (HCALL(c, HUD_VA_NEW, &o, 0x239u)) return 1;
    if (!o) return hud_null_refuse(c, 0x0044c3d6u, "map page", "operator new returned NULL (the original writes through it)");
    if (hud_gc_construct(c, o)) return 1;
    hud_counter(c, -2);
    hw32(c, m + 0x10u, o);
    hw32(c, o + 0x220u, 0u - (uint32_t)((int32_t)(hr32(c, m + 0x79u) - hr32(c, m + 0x71u)) >> 1));
    hw32(c, o + 0x224u, 0u - (uint32_t)((int32_t)(hr32(c, m + 0x7du) - hr32(c, m + 0x75u)) >> 1));
    hw32(c, o + 0x208u, 1);
    hw32(c, o + 0x20cu, 1);
    hw32(c, m + 0x1cu, 0);
    hw32(c, m + 0x18u, 0);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, m + 0x160u + i, hr32(c, 0x004aa640u + i));
    unsigned zs = hr32(c, 0x0049d6bcu) & 31u;
    int32_t dx = -(int32_t)hr32(c, o + 0x220u), dy = -(int32_t)hr32(c, o + 0x224u);
    if (!dx) return hud_divide_refuse(c, 0x0044c461u, 0, 0, "map page", "a zero map extent (the original's idiv faults)");
    if (!dy) return hud_divide_refuse(c, 0x0044c47fu, 0, 0, "map page", "a zero map extent (the original's idiv faults)");
    int32_t sxv = (int32_t)((uint32_t)(((int32_t)(hr32(c, m + 0x168u) - hr32(c, m + 0x160u)) >> 1) / dx) << zs);
    int32_t syv = (int32_t)((uint32_t)(((int32_t)(hr32(c, m + 0x16cu) - hr32(c, m + 0x164u)) >> 1) / dy) << zs);
    hw32(c, m + 0x20u, (uint32_t)(syv > sxv ? sxv : syv));
    hw32(c, 0x004d1ce0u, hr32(c, m + 0x20u));
    hw32(c, 0x004d1ce4u, (uint32_t)(((int32_t)hr32(c, 0x004d1ce0u) - 60000) / 25));
    if ((int32_t)hr32(c, 0x004d1ce4u) < 5000) hw32(c, 0x004d1ce4u, 5000);
    uint32_t k = 0;
    if (HCALL(c, HUD_VA_NEW, &k, 0x22u)) return 1;
    if (k) {
        if (HCALL(c, 0x0048bda4u, NULL, k, hr32(c, m + 0x10u), hr32(c, 0x0049d6bcu), 1u << zs, m + 0x18u, 0x0049d6c0u)) return 1;
        hud_counter(c, -2);
    }
    hw32(c, m + 0x14u, k);
    if (map_zoom_bar(c, m) || map_zoom_out(c, m)) return 1;
    uint32_t root = hr32(c, hr32(c, VA_PANEL)), w = 0;
    for (uint32_t i = 0; i < 2u; ++i) {
        if (HCALL(c, HUD_VA_NEW, &w, 0x2cu)) return 1;
        if (w) map_drag(c, w, i ? m + 0x71u : r2, m);
        hw32(c, m + (i ? 0x39u : 0x35u), w);
        widget_add(c, root, hr32(c, m + (i ? 0x39u : 0x35u)));
    }
    uint32_t font_rows = hr32(c, 0x0049b0b0u), font_title = hr32(c, 0x0049b0b4u);
    int32_t step = (int32_t)(hr32(c, r2 + 0xcu) - hr32(c, r2 + 4u)) / 9;
    uint32_t rect = f - 0x90u, off = f - 0x64u, l = 0;
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, rect + i, hr32(c, r2 + i));
    hw32(c, rect + 12u, (7u << sy) + hr32(c, rect + 4u));
    if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font_title, 1u)) return 1;
    hw32(c, m + 0x13au, l);
    hw32(c, f - 0x60u, 0);
    hw32(c, f - 0x64u, 0);
    if (label_in_rect(c, hr32(c, m + 0x13au), off, rect, 2u)) return 1;
    hw32(c, hr32(c, m + 0x13au) + 0x1du, hsx16(hr16(c, 0x004d3b98u)));
    hw32(c, rect + 4u, hr32(c, rect + 4u) + (uint32_t)step);
    hw32(c, rect + 12u, hr32(c, rect + 12u) + (uint32_t)step);
    hw32(c, f - 0x64u, 5);
    for (uint32_t i = 0; i < 8u; ++i) {
        if (HCALL(c, HUD_VA_NEW, &l, 0x25u)) return 1;
        if (l) {
            if (label_init(c, l, font_rows, 1u)) return 1;
            hw32(c, l + 0x21u, font_rows);
        }
        if (label_in_rect(c, l, off, rect, 1u)) return 1;
        hw32(c, l + 0x21u, font_title);
        hw32(c, m + 0x3du + 4u * i, l);
        hw32(c, rect + 4u, hr32(c, rect + 4u) + (uint32_t)step);
        hw32(c, rect + 12u, hr32(c, rect + 12u) + (uint32_t)step);
    }
    for (uint32_t i = 0; i < 8u; ++i) hw8(c, m + 0x131u + i, 0);
    hw8(c, m + 0x139u, 1);
    hw32(c, m + 0x81u, 0xffffffffu);
    hw32(c, m + 0x5du, 0xffffffffu);
    hw32(c, m + 0x61u, hr32(c, r2) + 1u);
    hw32(c, m + 0x69u, hr32(c, m + 0x61u) + 2u);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, f - 0xa0u + i, 0);
    uint32_t icons = 0;
    if (HCALL(c, HUD_VA_NEW_ARRAY, &icons, 0x230u)) return 1;
    hw32(c, m + 0x31u, icons);
    for (uint32_t i = 0; i < 8u; ++i) {
        hw8(c, 0x004d1cd0u + i, hr8(c, 0x0049d68cu + 2u * i));
        hw8(c, 0x004d1cd8u + i, hr8(c, 0x0049d69cu + 2u * i));
    }
    for (uint32_t i = 0; i < 0x8cu; ++i) {
        uint32_t ic = 0;
        if (HCALL(c, HUD_VA_NEW, &ic, 0x5bu)) return 1;
        if (ic && map_icon(c, ic, f - 0xa0u, m)) return 1;
        hw32(c, hr32(c, m + 0x31u) + 4u * i, ic);
    }
    hw8(c, m + 0x109u, 0);
    hw16(c, m + 0x10au, 0);
    hw8(c, 0x0049d6aeu, 0);
    hw8(c, m + 0x30u, 1);
    hw8(c, 0x0049d6adu, 1);
    hw32(c, 0x0049d6b0u, m);
    if (hud_call_regs(c, 0x00431210u, NULL, 0, NULL, &r)) return 1;
    for (uint32_t i = 0; i < 3u; ++i) {
        hw8(c, m + 0xa5u + 0x2au * i, 0);
        hw8(c, m + 0xa6u + 0x2au * i, 0);
    }
    hw32(c, m + 0x105u, 0xffffffffu);
    hw8(c, m + 0x380u, 0);
    hw8(c, m + 0x381u, 0);
    hw32(c, m + 0x118u, 0);
    hw32(c, m + 0x114u, 0);
    hw8(c, m + 0x11cu, 0);
    hw32(c, m + 0x12du, 0);
    hw32(c, m + 0x129u, 0);
    hw32(c, m + 0x125u, 0);
    hw16(c, m + 0x13eu, 0);
    hw8(c, m + 0x144u, 0);
    hw8(c, m + 0x15au, 0);
    hud_frame_pop(c, top);
    return 0;
}
/* 0x4507c4(S, r1, r2, H) (listing 0x4507c4..0x450b50): the HDD status page:
 * 0x448cd4, [S] <- 0x49dab0; the four colour words 0x49d9ec.. mapped in
 * place through the colour table 0x4d3b7c (word [0x4d3b7c + 2 w]); 13 rows
 * of two labels (font [0x49b0b0], offset (0, -1)): the left at (r2.x0 +
 * (0x1e << sx), y, r2.x1 - w, y + (4 << sy)), the right at (r2.x1 - w, y,
 * r2.x1, y + (4 << sy)) with w the width of "100", y from r2.y0 by 7 << sy,
 * both L+0x1d <- word [0x4d3ba2], into S+0x10+4i and S+0x44+4i; the label
 * S+0x78 at (r1.x0 + (0x1c << sx), y1 - (7 << sy), that + (0x28 << sx),
 * y1 = r1.y1 - (2 << sy)) (flags 2); 0x450b88(S, 0); S+0x80 <- -1, S+0xe8,
 * S+0xec <- [[S+8]+0x524], words S+0x96, S+0xbc, S+0xd4 <- 1; the label
 * S+0xf0 (font [0x49b0b4]) at (mid x - 0x28, mid y, that + (0x50 << sx),
 * mid y + (0xa << sy)) of r1 with L+0x1d <- word [0x4d3b9a]. EAX S. */
static int status_page_hdd(hud_ctx *c, uint32_t s, uint32_t r1, uint32_t r2, uint32_t h) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x60u);
    if (!f) return hud_refuse(c, 0x004507c4u, "status page", "the own code's call area is exhausted");
    f += 0x60u;
    unsigned sx = shx(c), sy = shy(c);
    hdd_page_base(c, s, r1, h);
    hw32(c, s, 0x0049dab0u);
    uint32_t font = hr32(c, 0x0049b0b0u);
    for (uint32_t i = 0; i < 4u; ++i) {
        uint32_t a = 0x0049d9ecu + 2u * i;
        hw16(c, a, hr16(c, 0x004d3b7cu + 2u * hsx16(hr16(c, a))));
    }
    hw32(c, f - 0x3cu, 0);
    hw32(c, f - 0x38u, 0xffffffffu);
    uint32_t a = f - 0x60u, b = f - 0x50u, step = 7u << sy;
    hw32(c, a, hr32(c, r2) + (0x1eu << sx));
    hw32(c, a + 4u, hr32(c, r2 + 4u) + (0u << sy));
    hw32(c, a + 8u, hr32(c, r2 + 8u));
    hw32(c, a + 12u, (4u << sy) + hr32(c, a + 4u));
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, b + i, hr32(c, a + i));
    hw32(c, a + 8u, hr32(c, a + 8u) - text_width(c, font, 0x0049da9du));
    hw32(c, b, hr32(c, a + 8u));
    for (uint32_t i = 0; i < 13u; ++i) {
        uint32_t l = 0;
        if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u) || label_in_rect(c, l, f - 0x3cu, a, 1u)) return 1;
        hw32(c, l + 0x1du, hsx16(hr16(c, 0x004d3ba2u)));
        hw32(c, s + 0x10u + 4u * i, l);
        if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u) || label_in_rect(c, l, f - 0x3cu, b, 1u)) return 1;
        hw32(c, l + 0x1du, hsx16(hr16(c, 0x004d3ba2u)));
        hw32(c, s + 0x44u + 4u * i, l);
        hw32(c, a + 4u, hr32(c, a + 4u) + step);
        hw32(c, a + 12u, hr32(c, a + 12u) + step);
        hw32(c, b + 4u, hr32(c, b + 4u) + step);
        hw32(c, b + 12u, hr32(c, b + 12u) + step);
    }
    uint32_t l = 0;
    if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u)) return 1;
    uint32_t rect1 = hr32(c, s + 4u);
    hw32(c, a + 12u, hr32(c, rect1 + 0xcu) - (2u << sy));
    hw32(c, a + 4u, hr32(c, a + 12u) - hsx16(shl16(7u, sy)));
    hw32(c, a, (0x1cu << sx) + hr32(c, rect1));
    hw32(c, a + 8u, (0x28u << sx) + hr32(c, a));
    if (label_in_rect(c, l, f - 0x3cu, a, 2u)) return 1;
    hw32(c, s + 0x78u, l);
    if (HCALL(c, 0x00450b88u, NULL, s, 0u)) return 1;
    hw32(c, s + 0x80u, 0xffffffffu);
    uint32_t sel = hr32(c, hr32(c, s + 8u) + 0x524u);
    hw32(c, s + 0xe8u, sel);
    hw32(c, s + 0xecu, sel);
    hw16(c, s + 0x96u, 1);
    hw16(c, s + 0xbcu, 1);
    hw16(c, s + 0xd4u, 1);
    if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, hr32(c, 0x0049b0b4u), 1u)) return 1;
    uint32_t y0 = hr32(c, r1 + 4u), mid = (uint32_t)((int32_t)(hr32(c, r1 + 0xcu) - y0) >> 1) + y0;
    uint32_t x0 = hr32(c, r1), midx = (uint32_t)((int32_t)(hr32(c, r1 + 8u) - x0) >> 1) + x0 - 0x28u;
    hw32(c, a + 4u, mid);
    hw32(c, a + 12u, (0xau << sy) + mid);
    hw32(c, a, midx);
    hw32(c, a + 8u, (0x50u << sx) + midx);
    if (label_in_rect(c, l, f - 0x3cu, a, 2u)) return 1;
    hw32(c, l + 0x1du, hsx16(hr16(c, 0x004d3b9au)));
    hw32(c, s + 0xf0u, l);
    hud_frame_pop(c, top);
    return 0;
}
/* 0x44bad4(B, rect, H, i) (listing 0x44bad4..0x44bb5d): the HDD button:
 * as 0x447444 with the classes 0x49d60c/0x49d630 last. */
static void hdd_button(hud_ctx *c, uint32_t b, uint32_t rect, uint32_t h, uint32_t i) {
    mfd_button(c, b, rect, h, i, 1);
    hw32(c, b + 0x17u, 0x0049d60cu);
    hw32(c, b + 0x20u, 0x0049d630u);
}
/* 0x448cf0(H, R, P) (listing 0x448cf0..0x449a75): the HDD (hud.md, "the HUD
 * line in the own process" lists the stores in order). One clock read at
 * 0x449964 (H+0x530 <- clock + 30). */
static int hdd_ctor(hud_ctx *c, uint32_t h, uint32_t r, uint32_t p) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x164u);
    if (!f) return hud_refuse(c, 0x00448cf0u, "HDD", "the own code's call area is exhausted");
    f += 0x164u;
    unsigned sx = shx(c), sy = shy(c);
    gauge_base(c, h, r, p);
    hw32(c, h, 0x0049d64cu);
    if (HCALL(c, HUD_VA_VECTOR_NEW, NULL, h + 0x12du, 0x14eu, 3u, 1u, 0x0044c10fu)) return 1;
    if (HCALL(c, HUD_VA_VECTOR_NEW, NULL, h + 0x772u, 8u, 2u, 1u, HUD_VA_EMPTY_CTOR)) return 1;
    uint32_t ox = hr32(c, r), oy = hr32(c, r + 4u), root = hr32(c, hr32(c, VA_PANEL)), font = hr32(c, 0x0049b0d8u), o = 0;
    if (HCALL(c, HUD_VA_NEW, &o, 0x239u)) return 1;
    if (o) {
        if (hud_gc_construct(c, o)) return 1;
        hud_counter(c, -2);
    }
    hw32(c, 0x004d1cccu, o);
    if (!hr32(c, 0x0049d234u)) {
        uint32_t path = 0, x = 0;
        if (HCALL(c, HUD_VA_PATH_DIR, &path, 0x0049d4f8u, hr32(c, 0x004a0a10u)) || HCALL(c, HUD_VA_LOAD_BY_NAME, &x, path)) return 1;
        hw32(c, 0x0049d234u, x);
        if (load_bitmap(c, 0x0049d4ffu, 0x0049d503u, 0x0049d507u, 0x0049d23cu)) return 1;
    }
    moved(c, h + 0xc1u, r + 0x10u, ox, oy);
    moved(c, h + 0xd1u, r + 0x30u, ox, oy);
    moved(c, h + 0xe1u, r + 0x20u, ox, oy);
    hw32(c, f - 0xb8u, hsx16(hr16(c, VA_SHIFT_X)));
    hw32(c, f - 0xb4u, hsx16(hr16(c, VA_SHIFT_Y)));
    uint32_t bx = hr32(c, h + 0xc1u) - ox, by = hr32(c, h + 0xc5u) - oy, path = 0;
    if (HCALL(c, HUD_VA_PATH_DIR, &path, 0x0049d50bu, 0x0049d513u) || view_regions(c, path, h + 0x562u, f - 0xb8u)) return 1;
    for (int32_t k = 0; (int32_t)hr32(c, h + 0x562u) > k; ++k) {
        uint32_t q = hr32(c, h + 0x566u + 4u * (uint32_t)k);
        if (hr32(c, q) != 2u) return hud_refuse(c, 0x004490abu, "HDD", "a clip region that is not a row table (the original reads its rectangle as one)");
        uint32_t t = hr32(c, q + 4u);
        hw16(c, t, (uint16_t)(hr16(c, t) + (uint16_t)by));
        for (int32_t j = 0; j < 2 * (int32_t)hsx16(hr16(c, t + 2u)); ++j)
            hw16(c, t + 4u + 2u * (uint32_t)j, (uint16_t)(hr16(c, t + 4u + 2u * (uint32_t)j) + (uint16_t)bx));
    }
    hw32(c, h + 0x109u, 0xffffffffu);
    uint32_t m = 0, s = 0;
    if (HCALL(c, HUD_VA_NEW, &m, 0x382u)) return 1;
    if (m && map_page(c, m, h + 0xc1u, h + 0xe1u, h)) return 1;
    hw32(c, h + 0x101u, m);
    if (HCALL(c, HUD_VA_NEW, &s, 0xf4u)) return 1;
    if (s && status_page_hdd(c, s, h + 0xc1u, h + 0xd1u, h)) return 1;
    hw32(c, h + 0x105u, s);
    uint32_t off = f - 0xdcu, rect = f - 0x154u, slot = 0;
    hw32(c, f - 0xdcu, 0);
    hw32(c, f - 0xd8u, 0);
    hw32(c, f - 0xd4u, 0);
    uint32_t table = 0;
    for (uint32_t i = 0; i < 15u; ++i) {
        moved(c, rect, r + 0x50u + 16u * i, ox, oy);
        uint32_t b = 0;
        if (HCALL(c, HUD_VA_NEW, &b, 0x41u)) return 1;
        if (b) {
            hdd_button(c, b, rect, h, i);
            hud_counter(c, -3);
        }
        if (i < 8u) {
            if (!b) return hud_null_refuse(c, 0x004492ebu, "HDD", "operator new returned NULL (the original writes through it)");
            table = hr32(c, hr32(c, 0x0049d23cu) + 8u);
            if (i < 2u) {
                slot = 0x19u;
                uint32_t l = 0;
                if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u) || label_in_rect(c, l, off, b, 2u)) return 1;
                hw32(c, b + 0x2cu, l);
            } else if (i == 2u) {
                slot = (hr32(c, r + 0x170u) << 3) + 5u;
            } else if (i == 6u) {
                slot = 0x15u;
            }
            hw32(c, b + 0x30u, hr32(c, table + 4u * slot));
            ++slot;
            hw32(c, b + 0x34u, hr32(c, table + 4u * slot));
            ++slot;
        }
        hw32(c, h + 0x18u + 4u * i, b);
        widget_add(c, root, b);
    }
    table = hr32(c, hr32(c, 0x0049d23cu) + 8u);
    for (uint32_t k = 0; k < 2u; ++k) {
        uint32_t b = hr32(c, h + (k ? 0x50u : 0x4cu)), l = 0;
        if (k) font = hr32(c, 0x0049b0bcu);
        if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, font, 1u)) return 1;
        uint32_t lr = f - 0x164u;
        hw32(c, lr, hr32(c, b));
        hw32(c, lr + 4u, hr32(c, b + 4u));
        hw32(c, lr + 8u, hr32(c, b) + (0x1bu << sx));
        hw32(c, lr + 12u, hr32(c, b + 4u) + (0xau << sy));
        hw32(c, f - 0xdcu, 0);
        hw32(c, f - 0xd8u, 0xffffffffu);
        if (label_in_rect(c, l, off, lr, 2u)) return 1;
        hw32(c, b + 0x2cu, l);
        hw32(c, b + 0x30u, hr32(c, table));
        hw32(c, b + 0x34u, hr32(c, table + 4u));
    }
    hw32(c, h + 0x68u, 0xfu);
    hw32(c, f - 0xdcu, 0);
    hw32(c, f - 0xd8u, 0);
    uint32_t b9 = hr32(c, h + 0x3cu), l = 0, lr = f - 0x164u;
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, lr + i, hr32(c, b9 + i));
    if (HCALL(c, HUD_VA_NEW, &l, 0x21u) || label_init(c, l, hr32(c, 0x0049b0d4u), 1u) || label_in_rect(c, l, off, lr, 2u)) return 1;
    hw32(c, l + 0x1du, hsx16(hr16(c, 0x004d3ba2u)));
    hw32(c, h + 0x51bu, l);
    moved(c, h + 0xf1u, r + 0x40u, ox, oy);
    for (uint32_t i = 0; i < 3u; ++i) {
        uint32_t rec = h + 0x12du + 0x14eu * i;
        moved(c, rec + 0x2du, r + 0x140u + 16u * i, ox, oy);
        hw32(c, rec + 0x137u, 0);
    }
    hw32(c, h + 0x52bu, 0xffffffffu);
    hw32(c, h + 0x517u, 0xffffffffu);
    if (HCALL(c, HUD_VA_MEMSET, NULL, h + 0xb1u, 0u, 0x10u)) return 1;
    if (HCALL(c, 0x0044a60cu, NULL, h, 0u)) return 1;
    uint32_t panel = hr32(c, VA_PANEL), port = panel ? hr32(c, panel + 0x207u) : 0;
    if (HCALL(c, 0x004355a8u, NULL, port, 0x0044b514u) || HCALL(c, 0x004355dcu, NULL, port, 0x0044b5e8u)) return 1;
    hw32(c, 0x0049d230u, h);
    hw8(c, h + 0x528u, 0xff);
    uint32_t gc = hr32(c, 0x004d1cccu);
    hw32(c, gc + 0x224u, 0);
    hw32(c, hr32(c, 0x004d1cccu) + 0x220u, 0);
    hw32(c, hr32(c, 0x004d1cccu) + 0x20cu, 1);
    hw32(c, hr32(c, 0x004d1cccu) + 0x208u, 1);
    hw16(c, h + 0x529u, hr16(c, r + 0x178u));
    hw8(c, h + 0x52fu, hr8(c, h + 0x76u));
    uint32_t now = 0;
    if (hud_clock(c, 0x00449964u, &now)) return 1;
    hw32(c, h + 0x530u, now + 0x1eu);
    if (!hr32(c, 0x0049d238u)) {
        uint32_t x = 0;
        if (HCALL(c, HUD_VA_NEW, &x, 0x10u)) return 1;
        if (x) {
            hw32(c, x + 4u, 0);
            hw32(c, x, 0);
            hw32(c, x + 8u, 0);
            hw16(c, x + 0xeu, 0);
            hw16(c, x + 0xcu, 0);
            hud_counter(c, 1);
            hud_counter(c, -1);
        }
        hw32(c, 0x0049d238u, x);
        if (HCALL(c, HUD_VA_PATH_DIR, &path, 0x0049d517u, 0x0049d51eu) || HCALL(c, 0x00453878u, NULL, hr32(c, 0x0049d238u), path)) return 1;
    }
    hw32(c, h + 0x534u, hr32(c, hr32(c, VA_PANEL) + 0x203u));
    hw32(c, h + 0x548u, hr32(c, 0x004d14dcu));
    for (uint32_t i = 0; i < 4u; ++i) {
        hw32(c, h + 0x538u + 4u * i, 0);
        hw32(c, h + 0x54cu + 4u * i, 0x0049d522u);
    }
    hw32(c, h + 0x558u, hr32(c, 0x004d14e0u));
    hw16(c, h + 0x55cu, 0);
    hw32(c, h + 0x524u, hr32(c, h + 4u * hsx16(hr16(c, h + 0x55cu)) + 0x534u));
    hw8(c, h + 0x51fu, 1);
    hw32(c, h + 0x55eu, hr32(c, 0x004d14e4u));
    if (HCALL(c, HUD_VA_MEMSET, NULL, h + 0x766u, 0u, 0x24u)) return 1;
    hud_frame_pop(c, top);
    return 0;
}
void es2_sim_service_hdd_ctor(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (hdd_ctor(&c, sc->slots[0], sc->slots[1], sc->slots[2])) { hud_failed(sc); return; }
    sc->eax = sc->slots[0];
}
/* 0x410644(W) (listing 0x410644..0x41066e): every weapon of the list W (count
 * word W+8, entries [W+4]) gets its slot +0x64 (the panel gauge's creator)
 * when not NULL. */
void es2_sim_service_weapon_gauges(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t w = sc->slots[0];
    for (int16_t i = 0; i < (int16_t)hr16(&c, w + 8u); ++i) {
        uint32_t x = hr32(&c, hr32(&c, w + 4u) + 4u * hsx16((uint16_t)i));
        if (x && HVCALL(&c, x, 0x64u, NULL, x)) { hud_failed(sc); return; }
    }
}
/* Rows of the label helpers for the other HUD files: 0x4387ac(L, font,
 * flag) and 0x438884(L, offset, rect, flags) as above (EAX L and L+4). */
void es2_sim_service_label_init(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (label_init(&c, sc->slots[0], sc->slots[1], sc->slots[2])) { hud_failed(sc); return; }
    sc->eax = sc->slots[0];
}
void es2_sim_service_label_in_rect(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (label_in_rect(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3])) { hud_failed(sc); return; }
    sc->eax = sc->slots[0] + 4u;
}
/* 0x440e9c(G) / 0x4414cc(G) (listings 0x440e9c..0x440ea1,
 * 0x4414cc..0x4414d1): EAX G+0xc2, the gauge's sync record (the weapon
 * sync hooks call them through the bridge). */
void es2_sim_service_gauge_record(es2_sim_service_call *sc, void *user) {
    (void)user;
    sc->eax = sc->slots[0] + 0xc2u;
}
/* The base destructor 0x4838fd(B, flags) (listing 0x4838fd..0x483923):
 * counter - 1; B: [B] <- 0x4a3368, flags & 1 operator delete(B). The
 * GLPalette destructor 0x481f6c(P, flags) (0x481f6c..0x481fdd), its slot
 * +0x18: counter - 1; P: [P] <- 0x4a30f8, word P+4: delete[]([P+8]); word
 * P+0xc: each of its strings delete([[P+0x10] + 8i]) and delete[]([P+0x10]);
 * 0x4838fd(P, 0); flags & 1 delete(P). */
static int base_destroy(hud_ctx *c, uint32_t b, uint32_t flags) {
    hud_counter(c, -1);
    if (!b) return 0;
    hw32(c, b, 0x004a3368u);
    return (flags & 1u) ? HCALL(c, HUD_VA_DELETE, NULL, b) : 0;
}
void es2_sim_service_base_destroy(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (base_destroy(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}
void es2_sim_service_palette_destroy(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t p = sc->slots[0], flags = sc->slots[1];
    hud_counter(&c, -1);
    if (!p) return;
    hw32(&c, p, 0x004a30f8u);
    if (hr16(&c, p + 4u) && HCALL(&c, HUD_VA_DELETE_ARRAY, NULL, hr32(&c, p + 8u))) { hud_failed(sc); return; }
    if (hr16(&c, p + 0xcu)) {
        for (int16_t i = 0; i < (int16_t)hr16(&c, p + 0xcu); ++i)
            if (HCALL(&c, HUD_VA_DELETE, NULL, hr32(&c, hr32(&c, p + 0x10u) + 8u * (uint32_t)(int32_t)i))) { hud_failed(sc); return; }
        if (HCALL(&c, HUD_VA_DELETE_ARRAY, NULL, hr32(&c, p + 0x10u))) { hud_failed(sc); return; }
    }
    if (base_destroy(&c, p, 0)) { hud_failed(sc); return; }
    if ((flags & 1u) && HCALL(&c, HUD_VA_DELETE, NULL, p)) { hud_failed(sc); return; }
}uint32_t hud_text_width(hud_ctx *c, uint32_t font, uint32_t text) { return text_width(c, font, text); }

/* ======================================================================
 * The display object's per-frame readers and requests, and the view shape as
 * rows of their own (display_modes.md, "cockpit layout and panel readers, the
 * display object's readers"). The load path above (0x429660 -> 0x429834 ->
 * 0x42dcf0, 0x48bab4, 0x429c2c, 0x431008 -> 0x431098 -> 0x437598, the font
 * loads 0x453630/0x453684 in src/hud_load.c) stays own code inside the rows
 * display_ctor and hud_setup.
 *
 * 0x429c2c(D, i): the view shape above as a row (display_set of
 * src/hud_display.c reaches it through the dispatch). EAX/EDX/ECX: on the
 * early return (i = 4 or [D+8][i] set) EAX = ECX = the frame record's
 * saved head (FS:0 at entry), EDX [D+8] (i != 4) or the caller's; else ECX
 * the saved head and EAX/EDX stream_destroy's, or [D+8] and [D+8][i] when
 * the digit is 2 (listing 0x429c53..0x429c5f, 0x429dc2..0x429dd5). */
void es2_sim_service_view_shape(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r = {.eax = sc->eax, .edx = sc->edx, .ecx = sc->ecx};
    if (view_shape(&c, sc->slots[0], (int32_t)sc->slots[1], &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}

/* 0x42a3f4(D, req) (listing 0x42a3f4..0x42a4c2): a display mode request.
 * Nothing changes while a request is pending ([D+0x18] != -1) or for req
 * > 6 (unsigned); else, by the jump table 0x42a417 and the current mode m
 * = [D+0x14], [D+0x18] <- 0 (req 0, m 0), 1 (req 1, m 1), 2 (req 2, m != 4),
 * 3 (req 3, m 4), 4 or 6 (req 4: m 0 or 3), 5 or 6 (req 5: m 0 or 2), 6
 * (req 6, m 2 or 3). EAX D; EDX req once [D+0x18] was -1 (m for req 6);
 * ECX 0 on the req-0 store. */
static void display_request(hud_ctx *c, uint32_t d, uint32_t req, es2_sim_result *r) {
    r->eax = d;
    if (hr32(c, d + 0x18u) != 0xffffffffu) return;
    r->edx = req;
    if (req > 6u) return;
    uint32_t m = hr32(c, d + 0x14u), v = 0xffffffffu;
    switch (req) {
    case 0: if (m == 0) { r->ecx = 0; v = 0; } break;
    case 1: if (m == 1) v = 1; break;
    case 2: if (m != 4) v = 2; break;
    case 3: if (m == 4) v = 3; break;
    case 4: if (m == 0) v = 4; else if (m == 3) v = 6; break;
    case 5: if (m == 0) v = 5; else if (m == 2) v = 6; break;
    default: r->edx = m; if (m == 3 || m == 2) v = 6; break;
    }
    if (v != 0xffffffffu) hw32(c, d + 0x18u, v);
}
void es2_sim_service_display_request(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r = {.eax = sc->eax, .edx = sc->edx, .ecx = sc->ecx};
    display_request(&c, sc->slots[0], sc->slots[1], &r);
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}

/* 0x429828(D) (listing 0x429828..0x429832): EAX [D+0x1f], the panel. */
void es2_sim_service_display_panel(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = hr32(&c, sc->slots[0] + 0x1fu);
}

/* 0x42db10() (listing 0x42db10..0x42db15): EAX 0x4cfa40, the current view
 * rectangle. */
void es2_sim_service_view_rect(es2_sim_service_call *sc, void *user) {
    (void)user;
    sc->eax = 0x004cfa40u;
}

/* 0x42db18() (listing 0x42db18..0x42db3c): whether the 3D view is drawn:
 * D = [0x4cf99c], EDX = [D+0x14] << 5, ECX = [D+4]; EAX 1 when the view
 * record's dword [ECX+EDX+0xc] is greater than 0 (signed) or byte D+0x1c
 * (a transition) is set, else 0. */
void es2_sim_service_view_visible(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t d = hr32(&c, VA_DISPLAY_OBJECT);
    sc->edx = hr32(&c, d + 0x14u) << 5;
    sc->ecx = hr32(&c, d + 4u);
    sc->eax = ((int32_t)hr32(&c, sc->ecx + sc->edx + 0xcu) > 0 || hr8(&c, d + 0x1cu)) ? 1u : 0u;
}

/* 0x433b20(H, p) (listing 0x433b20..0x433c53), reached only from 0x432b14:
 * with byte [0x49b105] set, the cursor point ([p], [p+4], or with word
 * [0x4d24d6] (tape playback) the point 0x4d2306 of 0x45b750) moved from
 * the canvas [H+4]'s origin +0x210/+0x214 to its +0x220/+0x224; m =
 * [[0x4cf99c]+0x14] (0x429820 inline); [H+0x22e] <- 0; the first of the
 * three rectangles [H+0x21a + 4k] = {x0, y0, x1, y1} that holds the point
 * (signed, inclusive) sets the cursor zone: k 0: 4 (m 0) or 3 (m 1); k 1:
 * 6 (m 0) or 5 (m 3); k 2: 5 (m 0) or 6 (m 2); then the scan ends. */
static void cursor_zone(hud_ctx *c, uint32_t h, uint32_t p) {
    if (!hr8(c, 0x0049b105u)) return;
    uint32_t pt = hr16(c, 0x004d24d6u) ? 0x004d2306u : p, canvas = hr32(c, h + 4u);
    int32_t x = (int32_t)(hr32(c, pt) - hr32(c, canvas + 0x210u) + hr32(c, canvas + 0x220u));
    int32_t y = (int32_t)(hr32(c, pt + 4u) - hr32(c, canvas + 0x214u) + hr32(c, canvas + 0x224u));
    uint32_t m = hr32(c, hr32(c, VA_DISPLAY_OBJECT) + 0x14u);
    hw32(c, h + 0x22eu, 0);
    static const uint32_t zone[3][2][2] = {{{0, 4}, {1, 3}}, {{0, 6}, {3, 5}}, {{0, 5}, {2, 6}}};
    for (uint32_t k = 0; k < 3u; ++k) {
        uint32_t r = hr32(c, h + 0x21au + 4u * k);
        if (x < (int32_t)hr32(c, r) || x > (int32_t)hr32(c, r + 8u) || y < (int32_t)hr32(c, r + 4u) || y > (int32_t)hr32(c, r + 0xcu))
            continue;
        for (unsigned j = 0; j < 2u; ++j)
            if (m == zone[k][j][0]) hw32(c, h + 0x22eu, zone[k][j][1]);
        return;
    }
}

/* 0x432b14(H, rec) (listing 0x432b14..0x432bc4): the panel's share of an
 * input record (called by the player input 0x460854 at 0x4608ab). With
 * byte H+0x23e (the scroll zones are set up) the cursor zone 0x433b20(H,
 * rec+2); D = [0x4cf99c] (0x429820 inline); the bytes rec+0x1e, +0x1f,
 * +0x20, +0x21 each, when set, request mode 1, 0, 5, 4 (0x42a3f4(D, ..) in
 * that order); with byte rec+0xb set: when byte [0x49b550] is clear and
 * [[0x4cf99c]+0x14] is 1, the sub-object [H+0x1f9]'s slot +0xc with
 * ([H+0x1f9], 0xf); byte [0x49b550] <- 1; else byte [0x49b550] <- 0. AL 1;
 * the upper bits of EAX: D, or mode - 1 (mode != 1), or the slot's EAX. */
static int hud_input(hud_ctx *c, uint32_t h, uint32_t rec, uint32_t *eax) {
    if (hr8(c, h + 0x23eu)) cursor_zone(c, h, rec + 2u);
    uint32_t d = hr32(c, VA_DISPLAY_OBJECT), a = d;
    static const uint8_t requests[4] = {1, 0, 5, 4};
    for (uint32_t k = 0; k < 4u; ++k) {
        if (!hr8(c, rec + 0x1eu + k)) continue;
        es2_sim_result r = {.eax = 0};
        display_request(c, d, requests[k], &r);
        a = r.eax;
    }
    if (hr8(c, rec + 0xbu)) {
        if (!hr8(c, 0x0049b550u)) {
            uint32_t mode = hr32(c, hr32(c, VA_DISPLAY_OBJECT) + 0x14u);
            a = mode - 1u;
            if (mode == 1u) {
                uint32_t o = hr32(c, h + 0x1f9u);
                if (HVCALL(c, o, 0xcu, &a, o, 0xfu)) return 1;
            }
        }
        hw8(c, 0x0049b550u, 1);
    } else {
        hw8(c, 0x0049b550u, 0);
    }
    *eax = (a & 0xffffff00u) | 1u;
    return 0;
}
void es2_sim_service_hud_input(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (hud_input(&c, sc->slots[0], sc->slots[1], &sc->eax)) hud_failed(sc);
}

/* 0x433c54(H) (listing 0x433c54..0x433d8f), the panel frame 0x4327ac's
 * cursor image: with byte [0x49b105] set and a cursor zone z = [H+0x22e]:
 * H+0x232 <- the cursor shape [0x49b08c + 4z]; when the current image
 * pointer H+0x226 is not H+0x232, H+0x22a <- it and H+0x226 <- H+0x232;
 * the image s = [H+0x232]. With no zone and H+0x226 = H+0x232: H+0x226 <-
 * H+0x22a and s = [[H+0x226]] (the image before). Then with word
 * [0x4d2566] (the paged driver) 0x48992e(a, s+8), else 0x486e70(a, s+8),
 * a = [s+4] + 4 when [s+4] is set, else 0. EAX H, or the callee's. */
static int cursor_image(hud_ctx *c, uint32_t h, uint32_t *eax) {
    *eax = h;
    if (!hr8(c, 0x0049b105u)) return 0;
    uint32_t z = hr32(c, h + 0x22eu), s;
    if (z) {
        hw32(c, h + 0x232u, hr32(c, 0x0049b08cu + 4u * z));
        if (h + 0x232u != hr32(c, h + 0x226u)) {
            hw32(c, h + 0x22au, hr32(c, h + 0x226u));
            hw32(c, h + 0x226u, h + 0x232u);
        }
        s = hr32(c, h + 0x232u);
    } else if (hr32(c, h + 0x226u) == h + 0x232u) {
        hw32(c, h + 0x226u, hr32(c, h + 0x22au));
        s = hr32(c, hr32(c, h + 0x226u));
    } else {
        return 0;
    }
    uint32_t a = hr32(c, s + 4u) ? hr32(c, s + 4u) + 4u : 0u;
    return HCALL(c, hr16(c, 0x004d2566u) ? 0x0048992eu : 0x00486e70u, eax, a, s + 8u);
}
void es2_sim_service_cursor_image(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (cursor_image(&c, sc->slots[0], &sc->eax)) hud_failed(sc);
}
