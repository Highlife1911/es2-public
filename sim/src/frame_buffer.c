/* The DIB section 0x464d44 headless and the graphics library's surface objects
 * it builds (es2/sim/loop.h; main_loop.md, "the main loop, pump, window
 * procedure, limiter and clock as own code"). "Listing" = llvm-objdump -d -M
 * intel of the reference DBSIM.EXE (SHA-256 8561aa47...c4ad); every store below
 * is in the listing's order. The GDI calls are answered by the headless
 * platform record (no window, no device context, no palette); the bits of the
 * DIB are a guest region the host provides (own_process.md section 6). Nothing
 * is drawn: present.c presents from these objects. */
#include <string.h>
#include "es2/sim/loop.h"
#include "es2/sim/memory.h"

/* ---- guest VAs */
#define VA_DISPLAY 0x004d3020u          /* the display record D: +0 descriptor, +4 surface, +8 bits, +0x14 memory DC, +0x18 DIB, +0x1c old bitmap */
#define VA_HWND 0x004d3610u             /* D+0x5f0 */
#define VA_HEADER 0x004d304cu           /* D+0x2c: BITMAPINFOHEADER, colours at D+0x54 */
#define VA_COLOURS 0x004d3074u
#define VA_GAME_W 0x004d253eu
#define VA_GAME_H 0x004d2542u
#define VA_TABLE_COUNT 0x004a1690u      /* 0x47a0b8's registered tables: count, pairs at 0x6c5834 */
#define VA_TABLE_PAIRS 0x006c5834u
#define VA_GL_TABLE 0x004a2eb8u
#define VA_GL_TABLE_COUNT 0x004a2f24u
#define VA_DEVICES 0x006c5eb4u          /* the device table; kind 7 (0x6c5ed0) is the current surface */
#define VA_CURRENT 0x006c5ed0u
#define VA_SURFACE_COUNT 0x004a2e2cu
#define VA_FORCE_W 0x004a03c8u          /* the next surface's size (-1: the descriptor's) */
#define VA_FORCE_H 0x004a03ccu
#define VA_ROWS 0x004a03d0u             /* the row table of the current 8-bit surface */
#define VA_DRIVERS 0x004a5840u          /* (format, table) pairs, a negative format ends them */
#define VA_DRIVER_SLOTS 0x004a57acu     /* the current driver table (count [0x4a583c] dwords) */
#define VA_DRIVER_COUNT 0x004a583cu
#define VA_VIEW 0x004a3652u             /* 0x481224: the surface's dwords +4..+0x2c */
#define VA_CLIP_STATE 0x004a33d8u       /* 0x4894cc: a copy of the surface's first clip record */
#define VA_CLIP_CURRENT 0x004a3614u
#define VA_FORMAT_NAMES 0x004a2e88u
#define VA_FORMAT_CODES 0x004a2e94u
#define VA_BMP_8 0x004a0841u            /* "BMP_8" */
#define VA_VGA_8 0x004a0847u            /* "VGA_8" */
#define VA_NEW 0x0047749cu
#define VA_GAME_CALLOC 0x0047747cu
#define VA_DELETE 0x00477548u
#define VA_FREE 0x004967a8u
#define VA_STRNCPY 0x00496760u
#define VA_MEMSET 0x0049681au
#define VA_ATEXIT 0x0049675au
#define VA_ABORT_NOTIFY 0x00479cfcu

static uint32_t r32(const es2_sim_loop *l, uint32_t a) { return l->memory->read32(l->memory->context, a); }
static uint16_t r16(const es2_sim_loop *l, uint32_t a) { return l->memory->read16(l->memory->context, a); }
static uint8_t r8(const es2_sim_loop *l, uint32_t a) { return es2_sim_read8(l->memory, a); }
static void w32(const es2_sim_loop *l, uint32_t a, uint32_t v) { l->memory->write32(l->memory->context, a, v); }
static void w16(const es2_sim_loop *l, uint32_t a, uint16_t v) { l->memory->write16(l->memory->context, a, v); }
static void w8(const es2_sim_loop *l, uint32_t a, uint8_t v) { es2_sim_write8(l->memory, a, v); }

static int call(es2_sim_loop *l, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    es2_sim_result r;
    if (es2_sim_loop_call(l, va, slots, count, &r)) return 1;
    if (eax) *eax = r.eax;
    return 0;
}
static void counter_add(const es2_sim_loop *l, int32_t n) {
    uint32_t p = r32(l, 0x006cbee4u);
    w32(l, p, r32(l, p) + (uint32_t)n);
}

/* ---- the driver table of a pixel format (0x489390): the pairs at
 * 0x4a5840; -1 when the format has none. */
static uint32_t driver_table(const es2_sim_loop *l, uint32_t format) {
    for (uint32_t p = VA_DRIVERS;; p += 8u) {
        uint32_t key = r32(l, p);
        if ((int32_t)key < 0) return key;
        if (key == format) return r32(l, p + 4u);
    }
}

/* 0x48a1d4(desc, out): the 8-bit memory driver's surface set-up (listing
 * 0x48a1d4..0x48a320): the row table (game calloc 0x47747c), the size
 * (forced by 0x4a03c8/0x4a03cc once), the format fields, the driver's
 * globals 0x4d2ff0..0x4d301c. */
static int driver_attach(es2_sim_loop *l, uint32_t desc, uint32_t out) {
    w32(l, 0x004d2ff0u, desc);
    uint32_t old = r32(l, VA_ROWS);
    if (old) {
        uint32_t f[1] = {old};
        if (call(l, VA_FREE, f, 1, NULL)) return 1;
    }
    uint32_t n[1] = {(uint32_t)((int32_t)(int16_t)r16(l, r32(l, 0x004d2ff0u)) * 4 + 8)}, rows = 0;
    if (call(l, VA_GAME_CALLOC, n, 1, &rows)) return 1;
    w32(l, VA_ROWS, rows);
    uint32_t bits = r32(l, r32(l, 0x004d2ff0u) + 0xau);
    w32(l, 0x004d2ff4u, bits);
    w32(l, rows, bits);
    w32(l, out, rows);
    if ((int32_t)r32(l, VA_FORCE_W) < 0) w32(l, out + 4u, (uint32_t)(int32_t)(int16_t)r16(l, r32(l, 0x004d2ff0u) + 2u));
    else { w32(l, out + 4u, r32(l, VA_FORCE_W)); w32(l, VA_FORCE_W, 0xffffffffu); }
    if ((int32_t)r32(l, VA_FORCE_H) < 0) w32(l, out + 8u, (uint32_t)(int32_t)(int16_t)r16(l, r32(l, 0x004d2ff0u)));
    else { w32(l, out + 8u, r32(l, VA_FORCE_H)); w32(l, VA_FORCE_H, 0xffffffffu); }
    w32(l, out + 0x24u, (uint32_t)(int32_t)(int16_t)r16(l, r32(l, 0x004d2ff0u) + 2u));
    w32(l, out + 0x28u, (uint32_t)(int32_t)(int16_t)r16(l, r32(l, 0x004d2ff0u)));
    w32(l, out + 0xcu, 1);
    w32(l, out + 0x10u, (uint32_t)(int32_t)(int8_t)r8(l, r32(l, 0x004d2ff0u) + 4u));
    w32(l, out + 0x14u, 0x100u);
    w32(l, out + 0x18u, 0);
    w32(l, out + 0x1cu, 0);
    w32(l, out + 0x20u, 1);
    w32(l, 0x004d3010u, r32(l, out + 4u));
    w32(l, 0x004d3014u, r32(l, out + 8u));
    uint32_t offset = 0;
    for (int32_t i = 0; i < (int32_t)(int16_t)r16(l, r32(l, 0x004d2ff0u)); ++i) {
        w32(l, r32(l, VA_ROWS) + 4u * (uint32_t)i + 4u, offset);
        offset += (uint32_t)(int32_t)(int16_t)r16(l, r32(l, 0x004d2ff0u) + 2u);
    }
    w32(l, 0x004d2ffcu, 0);
    w32(l, 0x004d2ff8u, 0);
    w32(l, 0x004d3000u, r32(l, out + 4u) - 1u);
    w32(l, 0x004d3004u, r32(l, out + 8u) - 1u);
    w32(l, 0x004d301cu, 0);
    w32(l, 0x004d3018u, 0);
    w32(l, 0x004d3008u, r32(l, out + 0x24u));
    w32(l, 0x004d300cu, r32(l, out + 0x28u));
    return 0;
}

/* A driver slot: the bodies of the BMP_8 table's slots the DIB section and
 * the release reach are own code (each read from the listing); any other
 * target goes through the dispatch. */
static int driver_slot(es2_sim_loop *l, uint32_t target, const uint32_t *a, unsigned n, uint32_t *eax) {
    switch (target) {
    case 0x0048a1ccu: case 0x00489830u: case 0x00489836u: if (eax) *eax = 0; return 0;   /* return 0 */
    case 0x0048a324u: case 0x004898fcu: case 0x00489902u: case 0x00489908u: return 0;     /* empty bodies */
    case 0x0048a1d4u:
        if (n < 2u) return es2_sim_loop_refuse(l, target, "graphics driver slot", "the attach slot without its two arguments");
        return driver_attach(l, a[0], a[1]);
    case 0x0048a32cu: {                                                                  /* free the row table */
        if (n < 1u) return es2_sim_loop_refuse(l, target, "graphics driver slot", "the release slot without its argument");
        uint32_t f[1] = {r32(l, a[0])};
        return call(l, VA_FREE, f, 1, eax);
    }
    default: return call(l, target, a, n, eax);
    }
}
static int driver(es2_sim_loop *l, uint32_t format, unsigned slot, const uint32_t *a, unsigned n, uint32_t *eax) {
    uint32_t table = driver_table(l, format);
    if ((int32_t)table < 0 || !table)
        return es2_sim_loop_null_refuse(l, 0x00489390u, "graphics driver table", "a pixel format without a driver table (the original calls through it)");
    return driver_slot(l, r32(l, table + 4u * slot), a, n, eax);
}
/* An indirect call through the current driver table copy (0x4a57ac..). */
static int current_slot(es2_sim_loop *l, uint32_t pointer, uint32_t argument) {
    uint32_t a[1] = {argument};
    return driver_slot(l, r32(l, pointer), a, 1, NULL);
}

/* 0x480590(obj, 7, obj): [obj] = 0, the device slot when it is free. */
static void device_init(es2_sim_loop *l, uint32_t obj) {
    w32(l, obj, 0);
    if (!r32(l, VA_DEVICES + 4u * 7u)) w32(l, VA_DEVICES + 4u * 7u, obj);
}
/* 0x4805b4(7, s): store s unless both s and the slot are set (then AL 1). */
static void device_set(es2_sim_loop *l, uint32_t s) {
    if (s && r32(l, VA_CURRENT)) return;
    w32(l, VA_CURRENT, s);
}

/* 0x4812f0(obj, name): the format code of a name ("VGA_8" 1, "VGA_4" 0,
 * "BMP_8" 3, else 4), names 0x4a2e88, codes copied from 0x4a2e94. */
static int same_text(const es2_sim_loop *l, uint32_t a, uint32_t b) {
    for (uint32_t k = 0;; ++k) {
        uint8_t x = r8(l, a + k);
        if (x != r8(l, b + k)) return 0;
        if (!x) return 1;
    }
}
static uint32_t format_code(es2_sim_loop *l, uint32_t name) {
    uint32_t codes[4];
    for (unsigned i = 0; i < 4u; ++i) codes[i] = r32(l, VA_FORMAT_CODES + 4u * i);
    /* The image's last code is 4, which ends the walk; i stops at 3 anyway. */
    unsigned i = 0;
    while (i < 3u && codes[i] != 4u && !same_text(l, r32(l, VA_FORMAT_NAMES + 4u * i), name)) ++i;
    return codes[i];
}

/* 0x480f58(obj, name) (listing 0x480f58..0x480fa1). */
static int surface_construct(es2_sim_loop *l, uint32_t obj, uint32_t name) {
    device_init(l, obj);
    uint32_t a[1] = {ES2_SIM_VA_SURFACE_NOTIFY};
    if (call(l, VA_ABORT_NOTIFY, a, 1, NULL)) return 1;
    a[0] = ES2_SIM_VA_SURFACE_RELEASE;
    if (call(l, VA_ATEXIT, a, 1, NULL)) return 1;
    w32(l, obj, 0);
    w32(l, obj + 0x30u, format_code(l, name));
    counter_add(l, 1);
    return 0;
}

/* 0x489570(p, w, h): a clip record of 0x22c bytes. */
static void clip_init(es2_sim_loop *l, uint32_t p, uint32_t w, uint32_t h) {
    w32(l, p, 0);
    for (uint32_t i = 0; i < 0x80u; ++i) w32(l, p + 4u + 4u * i, 0);
    w32(l, p + 0x204u, 0);
    w32(l, p + 0x208u, 0);
    w32(l, p + 0x20cu, 0);
    w32(l, p + 0x210u, 0);
    w32(l, p + 0x214u, w - 1u);
    w32(l, p + 0x218u, h - 1u);
    w32(l, p + 0x21cu, 0);
    w32(l, p + 0x220u, 0);
    w32(l, p + 0x224u, 0x004a3604u);
    w32(l, p + 0x228u, 0x004a360cu);
}

/* 0x481028(surface, desc) (listing 0x481028..0x4811e6): 1 when attached. */
static int surface_create(es2_sim_loop *l, uint32_t s, uint32_t desc, uint32_t *result) {
    *result = 0;
    if (r32(l, s)) return 0;
    uint32_t n = r32(l, VA_SURFACE_COUNT);
    w32(l, VA_SURFACE_COUNT, n + 1u);   /* n = 0: 0x489408 (an empty body) */
    /* 0x489416(format, desc, s+4): driver slots 8, 9 (a nonzero result ends
     * it with 0), 10, 11 (desc, s+4), 7 ([s+0x28], s+4). */
    uint32_t format = r32(l, s + 0x30u), r = 0;
    uint32_t a8[1] = {desc};
    if (driver(l, format, 8, a8, 1, &r)) return 1;
    if (r) return 0;
    if (driver(l, format, 9, NULL, 0, &r)) return 1;
    if (r) return 0;
    if (driver(l, format, 10, NULL, 0, NULL)) return 1;
    uint32_t a11[2] = {desc, s + 4u};
    if (driver(l, format, 11, a11, 2, NULL)) return 1;
    uint32_t a7[2] = {r32(l, s + 4u + 0x24u), s + 4u};
    if (driver(l, format, 7, a7, 2, NULL)) return 1;
    /* Two clip records; 0x48de2f and 0x49545b on +0x20c/+0x214/+0x21c are
     * element constructors that store nothing. */
    uint32_t size[1] = {0x22cu}, p1 = 0, p2 = 0;
    if (call(l, VA_NEW, size, 1, &p1)) return 1;
    w32(l, s + 0x40u, p1);
    if (!p1) return es2_sim_loop_null_refuse(l, 0x004810cau, "surface clip record", "operator new returned NULL");
    clip_init(l, p1, r32(l, s + 8u), r32(l, s + 0xcu));
    if (call(l, VA_NEW, size, 1, &p2)) return 1;
    w32(l, s + 0x44u, p2);
    if (!p2) return es2_sim_loop_null_refuse(l, 0x00481129u, "surface clip record", "operator new returned NULL");
    for (uint32_t i = 0; i < 0x81u; ++i) w32(l, p2 + 4u * i, r32(l, p1 + 4u * i));
    static const uint32_t tail[] = {0x204u, 0x208u, 0x224u, 0x228u, 0x20cu, 0x210u, 0x214u, 0x218u, 0x21cu, 0x220u};
    for (unsigned i = 0; i < sizeof(tail) / sizeof(tail[0]); ++i) w32(l, p2 + tail[i], r32(l, p1 + tail[i]));
    w32(l, s + 0x3cu, 0);
    w32(l, s + 0x38u, 0);
    w32(l, s + 0x34u, 0);
    w32(l, s, r32(l, s) | 1u);
    *result = 1;
    return 0;
}

/* 0x481224(surface) (listing 0x481224..0x4812ed): make it current. */
static int surface_select(es2_sim_loop *l, uint32_t s) {
    if (r32(l, VA_CURRENT)) w32(l, VA_CURRENT, 0);   /* 0x48154c: 0x4805b4(7, 0); its AL 0 continues */
    device_set(l, s);
    /* 0x4893aa(format): the driver table copied to 0x4a57ac. */
    uint32_t table = driver_table(l, r32(l, s + 0x30u));
    if ((int32_t)table != -1) {
        uint32_t count = r32(l, VA_DRIVER_COUNT);
        for (uint32_t i = 0; i < count; ++i) w32(l, VA_DRIVER_SLOTS + 4u * i, r32(l, table + 4u * i));
    }
    for (uint32_t i = 0; i < 11u; ++i) w32(l, VA_VIEW + 4u * i, r32(l, s + 4u + 4u * i));
    if (current_slot(l, 0x004a57f4u, r32(l, s + 0x34u)) || current_slot(l, 0x004a57ecu, r32(l, s + 0x38u)) ||
        current_slot(l, 0x004a57f0u, r32(l, s + 0x3cu)))
        return 1;
    /* 0x4894cc(p1): the clip record into 0x4a33d8; 0x489558(p2). */
    uint32_t p = r32(l, s + 0x40u);
    if (!p) return es2_sim_loop_null_refuse(l, 0x004894d1u, "surface clip record", "no clip record (the original reads through NULL)");
    for (uint32_t i = 0; i < 0x81u; ++i) w32(l, VA_CLIP_STATE + 4u * i, r32(l, p + 4u * i));
    static const struct { uint32_t from, to; } tail[] = {
        {0x204u, 0x4a35dcu}, {0x208u, 0x4a35e0u}, {0x224u, 0x4a35fcu}, {0x228u, 0x4a3600u}, {0x20cu, 0x4a35e4u},
        {0x210u, 0x4a35e8u}, {0x214u, 0x4a35ecu}, {0x218u, 0x4a35f0u}, {0x21cu, 0x4a35f4u}, {0x220u, 0x4a35f8u}};
    for (unsigned i = 0; i < sizeof(tail) / sizeof(tail[0]); ++i) w32(l, tail[i].to, r32(l, p + tail[i].from));
    w32(l, VA_CLIP_CURRENT, r32(l, s + 0x44u));
    return 0;
}

/* ---- 0x464d44 (listing 0x464d44..0x4650cb). The frame record 0x49630c
 * is not linked (own code; FS:0 is the same at the return, as in the
 * original); its context words live on the original's stack only. */
int es2_sim_dib_section(es2_sim_loop *l) {
    es2_sim_loop_platform *p = &l->platform;
    const uint32_t d = VA_DISPLAY;
    /* 0x480ef4: 0x47a0b8(0x4a2eb8, [0x4a2f24]): the GL table registered. */
    uint32_t count = r32(l, VA_GL_TABLE_COUNT);
    if (count) {
        w32(l, VA_TABLE_PAIRS + 4u + 8u * r32(l, VA_TABLE_COUNT), count);
        w32(l, VA_TABLE_PAIRS + 8u * r32(l, VA_TABLE_COUNT), VA_GL_TABLE);
        w32(l, VA_TABLE_COUNT, r32(l, VA_TABLE_COUNT) + 1u);
    }
    /* GetDC(hwnd); CreateCompatibleDC(NULL) -> D+0x14; the header's size;
     * GetSystemPaletteEntries(dc, 0, 256, D+0x54): a display without a
     * palette answers 0 and writes nothing (headless the same); ReleaseDC. */
    ++p->get_dc;
    ++p->create_dc;
    w32(l, d + 0x14u, p->dc_memory);
    w32(l, d + 0x30u, r32(l, d + 0x20u));
    w32(l, d + 0x34u, 0u - r32(l, d + 0x24u));
    ++p->palette_entries;
    ++p->release_dc;
    /* PALETTEENTRY to RGBQUAD: bytes 0 and 2 of the 256 entries swapped. */
    for (uint32_t i = 0; i < 0x100u; ++i) {
        uint32_t e = VA_COLOURS + 4u * i;
        uint8_t red = r8(l, e), blue = r8(l, e + 2u);
        w8(l, e, blue);
        w8(l, e + 2u, red);
    }
    if (r32(l, d + 0x18u)) {
        /* A DIB from an earlier call: SelectObject(dc, old) and DeleteObject. */
        ++p->select_object;
        ++p->delete_object;
    }
    /* The BITMAPINFOHEADER at D+0x2c (stores in the listing's order). */
    w32(l, d + 0x2cu, 0x28u);
    w16(l, d + 0x2cu + 0xcu, 1);
    w32(l, d + 0x2cu + 0x10u, 0);
    w32(l, d + 0x2cu + 0x20u, 0);
    w32(l, d + 0x30u, r32(l, d + 0x20u));
    w32(l, d + 0x34u, 0u - r32(l, d + 0x24u));
    w32(l, d + 0x40u, r32(l, d + 0x24u) * r32(l, d + 0x20u));
    /* CreateDIBSection(dc, D+0x2c, DIB_PAL_COLORS, &D+8, 0, 0): headless the
     * host region (a failure would only show a message box). */
    ++p->create_dib;
    uint32_t need = r32(l, d + 0x24u) * r32(l, d + 0x20u);
    if (!p->bits || need > p->bits_size)
        return es2_sim_loop_refuse(l, 0x00464e1fu, "CreateDIBSection", "the host's DIB region is smaller than the DIB");
    w32(l, d + 8u, p->bits);
    w32(l, d + 0x18u, p->dib);
    ++p->select_object;
    w32(l, d + 0x1cu, p->old_bitmap);
    /* The 14-byte descriptor. */
    uint32_t size[1] = {0xeu}, desc = 0;
    if (call(l, VA_NEW, size, 1, &desc)) return 1;
    if (!desc) return es2_sim_loop_null_refuse(l, 0x00464e5cu, "DIB descriptor", "operator new returned NULL");
    w32(l, d, desc);
    w16(l, desc + 2u, r16(l, d + 0x20u));
    w16(l, desc, r16(l, d + 0x24u));
    w8(l, desc + 4u, 8);
    w8(l, desc + 5u, (uint8_t)(r8(l, desc + 5u) & 0xf0u));
    w8(l, r32(l, d) + 5u, (uint8_t)(r8(l, r32(l, d) + 5u) | 0x10u));
    w32(l, r32(l, d) + 6u, r32(l, d + 0x20u) * r32(l, d + 0x24u));
    w32(l, r32(l, d) + 0xau, r32(l, d + 8u));
    desc = r32(l, d);
    /* The surface object (0xa0 bytes). */
    size[0] = 0xa0u;
    uint32_t s = 0;
    if (call(l, VA_NEW, size, 1, &s)) return 1;
    if (s) {
        if (surface_construct(l, s, VA_BMP_8)) return 1;
        /* 0x49667c(s+0x48, 8, 4, 1, 0x49545b) and (s+0x68, ...): four
         * elements whose constructor returns its argument; flags 1 has no
         * count word and leaves the counter (listing 0x49667c..0x496745). */
        uint32_t c[3] = {s + 0x90u, VA_BMP_8, 0xfu};
        if (call(l, VA_STRNCPY, c, 3, NULL)) return 1;
        counter_add(l, 1);
        counter_add(l, -2);
    }
    w32(l, d + 4u, s);
    int32_t h = (int32_t)r32(l, VA_GAME_H), w = (int32_t)r32(l, VA_GAME_W);
    if (w >= 0) w32(l, VA_FORCE_W, (uint32_t)w);
    if (h >= 0) w32(l, VA_FORCE_H, (uint32_t)h);
    if (!s) return es2_sim_loop_null_refuse(l, 0x00464f4eu, "surface", "operator new returned NULL (the original goes on with a NULL surface)");
    uint32_t attached = 0;
    if (surface_create(l, s, desc, &attached)) return 1;
    if (attached) {
        uint32_t name = s + 0x90u;
        if (surface_select(l, s)) return 1;
        w32(l, s + 0x88u, 0);
        w32(l, s + 0x8cu, 1);
        w32(l, s + 0x3cu, r32(l, s + 0x8cu));
        if (s == r32(l, VA_CURRENT)) {
            w32(l, 0x004a368au, r32(l, s + 0x3cu));
            if (current_slot(l, 0x004a57f0u, r32(l, s + 0x3cu))) return 1;
        }
        w32(l, s + 0x34u, r32(l, s + 0x88u));
        if (s == r32(l, VA_CURRENT)) {
            w32(l, 0x004a3682u, r32(l, s + 0x34u));
            if (current_slot(l, 0x004a57f4u, r32(l, s + 0x34u))) return 1;
        }
        if (same_text(l, name, VA_VGA_8)) {
            for (uint32_t i = 0; i < 4u; ++i) {
                w32(l, s + 0x48u + 8u * i, 0);
                w32(l, s + 0x4cu + 8u * i, 200u * i);
            }
        } else {
            uint32_t m[3] = {s + 0x48u, 0, 0x20u};
            if (call(l, VA_MEMSET, m, 3, NULL)) return 1;
        }
        uint32_t m[3] = {s + 0x68u, 0, 0x20u};
        if (call(l, VA_MEMSET, m, 3, NULL)) return 1;
    }
    if (surface_select(l, r32(l, d + 4u))) return 1;
    /* 0x4813f8(surface, 9, 0): case 5 of its byte table: +0x1c, and
     * 0x4a366a for the current surface. */
    s = r32(l, d + 4u);
    w32(l, s + 0x1cu, 0);
    if (s == r32(l, VA_CURRENT)) w32(l, 0x004a366au, r32(l, s + 0x1cu));
    return 0;
}

/* 0x480f38 (listing 0x480f38..0x480f57) with 0x4811e8: the current
 * surface's first clip record deleted, [s] = 0, driver slot 12 (frees the
 * row table through _free directly), the surface count - 1 (0: 0x48940f, an
 * empty body). The second clip record stays (as in the original). */
int es2_sim_surface_release(es2_sim_loop *l) {
    uint32_t s = r32(l, VA_CURRENT);
    if (!s || !(r32(l, s) & 1u)) return 0;
    uint32_t a[1] = {r32(l, s + 0x40u)};
    if (call(l, VA_DELETE, a, 1, NULL)) return 1;
    w32(l, s, 0);
    uint32_t b[1] = {s + 4u};
    if (driver(l, r32(l, s + 0x30u), 12, b, 1, NULL)) return 1;
    w32(l, VA_SURFACE_COUNT, r32(l, VA_SURFACE_COUNT) - 1u);
    return 0;
}

/* ---- the graphics library's surface pieces for other own callers
 * (the HUD map set-up 0x44f6f4 builds two more surfaces the same way). */
int es2_sim_gl_surface_construct(es2_sim_loop *l, uint32_t obj, uint32_t name) { return surface_construct(l, obj, name); }
int es2_sim_gl_surface_create(es2_sim_loop *l, uint32_t s, uint32_t desc, uint32_t *attached) {
    return surface_create(l, s, desc, attached);
}
int es2_sim_gl_surface_select(es2_sim_loop *l, uint32_t s) { return surface_select(l, s); }
/* 0x4811e8(s) (listing 0x4811e8..0x481223): for an attached surface the
 * first clip record deleted, [s] = 0, driver slot 12 of its format with
 * s+4, the surface count - 1 (0: the empty body 0x48940f). EAX 0. */
int es2_sim_gl_surface_destroy(es2_sim_loop *l, uint32_t s) {
    if (!(r32(l, s) & 1u)) return 0;
    uint32_t a[1] = {r32(l, s + 0x40u)};
    if (call(l, VA_DELETE, a, 1, NULL)) return 1;
    w32(l, s, 0);
    uint32_t b[1] = {s + 4u};
    if (driver(l, r32(l, s + 0x30u), 12, b, 1, NULL)) return 1;
    w32(l, VA_SURFACE_COUNT, r32(l, VA_SURFACE_COUNT) - 1u);
    return 0;
}
/* The finishing steps the DIB section and 0x44f6f4 share after 0x481028
 * attached `s` (listing 0x464f8b..0x465064 and 0x44fb4d..0x44fcf2): select
 * it, +0x88 0 and +0x8c 1 into +0x3c and +0x34 with the current-surface
 * driver slots, the page table +0x48 ("VGA_8": 4 x {0, 200 i}, else 32
 * zero bytes) and +0x68 (32 zero bytes). */
int es2_sim_gl_surface_finish(es2_sim_loop *l, uint32_t s) {
    uint32_t name = s + 0x90u;
    if (surface_select(l, s)) return 1;
    w32(l, s + 0x88u, 0);
    w32(l, s + 0x8cu, 1);
    w32(l, s + 0x3cu, r32(l, s + 0x8cu));
    if (s == r32(l, VA_CURRENT)) {
        w32(l, 0x004a368au, r32(l, s + 0x3cu));
        if (current_slot(l, 0x004a57f0u, r32(l, s + 0x3cu))) return 1;
    }
    w32(l, s + 0x34u, r32(l, s + 0x88u));
    if (s == r32(l, VA_CURRENT)) {
        w32(l, 0x004a3682u, r32(l, s + 0x34u));
        if (current_slot(l, 0x004a57f4u, r32(l, s + 0x34u))) return 1;
    }
    if (same_text(l, name, VA_VGA_8)) {
        for (uint32_t i = 0; i < 4u; ++i) {
            w32(l, s + 0x48u + 8u * i, 0);
            w32(l, s + 0x4cu + 8u * i, 200u * i);
        }
    } else {
        uint32_t m[3] = {s + 0x48u, 0, 0x20u};
        if (call(l, VA_MEMSET, m, 3, NULL)) return 1;
    }
    uint32_t m[3] = {s + 0x68u, 0, 0x20u};
    return call(l, VA_MEMSET, m, 3, NULL);
}
/* 0x4813f8(s, which, value) (listing 0x4813f8..0x4814e4) for the cases its
 * byte table 0x481418 maps to a field: 9 +0x1c (0x4a366a), 10 +0x20
 * (0x4a366e), 11 +0x24 (0x4a3672), each mirrored for the current surface,
 * 16 0x4a368e; 1 for those, 0 for the default (which > 0x11 or index 0).
 * Cases 2 (the clip-origin pair) and 17 (driver slot [0x4a582c]) refuse. */
int es2_sim_gl_surface_set(es2_sim_loop *l, uint32_t s, uint32_t which, uint32_t value, uint32_t *result) {
    *result = 1;
    switch (which) {
    case 9: w32(l, s + 0x1cu, value); if (s == r32(l, VA_CURRENT)) w32(l, 0x004a366au, r32(l, s + 0x1cu)); return 0;
    case 10: w32(l, s + 0x20u, value); if (s == r32(l, VA_CURRENT)) w32(l, 0x004a366eu, r32(l, s + 0x20u)); return 0;
    case 11: w32(l, s + 0x24u, value); if (s == r32(l, VA_CURRENT)) w32(l, 0x004a3672u, r32(l, s + 0x24u)); return 0;
    case 16: w32(l, 0x004a368eu, value); return 0;
    case 2: case 17: return es2_sim_loop_refuse(l, 0x004813f8u, "surface set", "case 2 or 17 (not modelled)");
    default: *result = 0; return 0;
    }
}