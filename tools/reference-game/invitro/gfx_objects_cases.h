/* The graphics library's device objects, graphics contexts, surfaces and
 * bitmaps (the 35 rows of sim/dispatch/services/gfx_objects.h), original machine code
 * against sim/src/gfx.c and the frame_buffer.c pieces it calls
 * (differential_gfx_objects_test). It reuses startup_cases.h's snapshots and
 * loop_cases.h's loop and import stubs, as panel_gadgets_cases.h does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same synthetic world from the same seeded generator: four device
 * objects, five surfaces (formats 5, 5, 3, 9, 5: format 5 is a synthetic
 * driver table of recording host stubs entered as the first (format,
 * table) pair at 0x4a5840, 3 is the image's BMP_8 table, 9 has none),
 * attached or not, with clip records; two blocks for the surface
 * constructor, three graphics contexts, three bitmaps, two streams whose
 * method table holds host stubs, points, names; random device slots, a
 * random handler table, the current driver table a copy of format 5's or
 * BMP_8's. Then 1..10 random rows run on it with arguments chosen on the
 * original side from the world's state (only objects the original can
 * use: a live object, a surface with a clip record, a format with a
 * table where the row calls through it; a deleted object is not used
 * again). Other packages' callees (0x4838fd, 0x47a8a4, 0x4816a8, 0x492447,
 * 0x49085d, 0x482f0a, 0x482bf0) are recording stubs on both sides; the
 * heap, the atexit table and the strncpy/memset models are loop_cases.h's.
 * 0x480f38, 0x480ffc and 0x4811e8 run on the original side through a thunk
 * that keeps the harness's EBX/ESI/EDI and checks the 0x4894a5 quirk (ESI
 * 0, EDI the entry ESI after a destroy; untouched otherwise). Compared:
 * DATA 0x497000..0x6cc000 and the heap, EAX by mask (0 after a delete and
 * where the entry EAX stays), the stub log, the import-level log and the
 * atexit table. Coverage: per-path counters from the original side; every
 * one must be non-zero. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"

static es2_sim_entry go_entries[4096];
static es2_sim_dispatch go_dispatch;
static int go_bad;
static unsigned go_side;

/* ---- the stub log of a side */
static uint64_t go_hash[2];
static unsigned go_count[2];
static char go_text[2][4096];
static size_t go_len[2];
static void go_note(char letter, uint32_t a, uint32_t b, uint32_t c) {
    uint64_t h = go_hash[go_side];
    const uint32_t v[4] = {(uint32_t)letter, a, b, c};
    for (unsigned i = 0; i < 4u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    go_hash[go_side] = h;
    ++go_count[go_side];
    if (go_len[go_side] + 48u < sizeof(go_text[0]))
        go_len[go_side] += (size_t)sprintf(go_text[go_side] + go_len[go_side], "%c(%lx,%lx,%lx) ", letter, (unsigned long)a,
                                           (unsigned long)b, (unsigned long)c);
}
static uint32_t go_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static uint16_t go_get16(uint32_t a) { return *(uint16_t *)(uintptr_t)a; }
static uint8_t go_get8(uint32_t a) { return *(BYTE *)(uintptr_t)a; }
static void go_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }
static void go_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }

/* ---- the world (filled by go_build) */
#define GO_DEVICES 4u
#define GO_SURFACES 5u
#define GO_FRESH 2u
#define GO_GCS 3u
#define GO_BITMAPS 3u
#define GO_STREAMS 2u
#define GO_NAMES 6u
typedef struct go_world {
    uint32_t device[GO_DEVICES], surface[GO_SURFACES], fresh[GO_FRESH], gc[GO_GCS], bitmap[GO_BITMAPS], stream[GO_STREAMS];
    uint32_t svt, table, point[2], block[2], desc, name[GO_NAMES];
    uint8_t dead_device[GO_DEVICES], dead_surface[GO_SURFACES], dead_gc[GO_GCS], dead_bitmap[GO_BITMAPS], dead_stream[GO_STREAMS];
} go_world;
static go_world go_w, go_w0;
static const uint32_t go_format[GO_SURFACES] = {5u, 5u, 3u, 9u, 5u};

/* ---- the stubs' answers: a generator reset per side */
static uint32_t go_answer_seed;
static uint32_t go_answer(uint32_t n) {
    go_answer_seed = go_answer_seed * 1103515245u + 12345u;
    return (go_answer_seed >> 8) % n;
}
enum { GOE_SLOT8_FAIL, GOE_SLOT9_FAIL, GOE_ATTACH_FIELD, GOE_COUNT };
static unsigned go_effects[GOE_COUNT];
static void go_effect(unsigned kind) { if (go_side == 0) ++go_effects[kind]; }
/* One stub body for both sides; s = the arguments. */
static uint32_t go_stub(char letter, const uint32_t *s) {
    switch (letter) {
    case 'e':   /* driver slot 8 (desc): nonzero ends the attach */
        go_note(letter, s[0], 0, 0);
        if (go_answer(4u) == 0) { go_effect(GOE_SLOT8_FAIL); return 1u + go_answer(0x100u); }
        return 0;
    case 'f':   /* driver slot 9 () */
        go_note(letter, 0, 0, 0);
        if (go_answer(4u) == 0) { go_effect(GOE_SLOT9_FAIL); return 1u + go_answer(0x100u); }
        return 0;
    case 't': go_note(letter, 0, 0, 0); return 0x7100u + go_answer(0x100u);   /* slot 10 () */
    case 'a':   /* slot 11 (desc, view): the attach fills the view (+0x24 read next) */
        go_note(letter, s[0], s[1], 0);
        if (go_answer(2u) == 0) { go_put32(s[1] + 0x24u, 0x100u + go_answer(0x1000u)); go_effect(GOE_ATTACH_FIELD); }
        return 0x7200u + go_answer(0x100u);
    case 's': go_note(letter, s[0], s[1], 0); return 0x7300u + go_answer(0x100u);   /* slot 7 (value, view) */
    case 'r': case 'p': case 'q': case 'u': case 'z':   /* slots 12, 16, 17, 18, 32 (one argument) */
        go_note(letter, s[0], 0, 0);
        return 0x7400u + go_answer(0x100u);
    case 'W':   /* stream slot 5 (st, n, p): one byte read from p, else p */
        go_note(letter, s[0], s[1], s[1] == 1u ? go_get8(s[2]) : s[2]);
        return 0x7500u + go_answer(0x100u);
    case 'R': go_note(letter, s[0], s[1], s[2]); return 0x7600u + go_answer(0x100u);   /* stream slot 6 */
    case 'L':   /* 0x4816a8 (bitmap, point, flags): the point's contents */
        go_note(letter, s[0], go_get(s[1]) ^ (go_get(s[1] + 4u) << 16), s[2]);
        return 0x7700u + go_answer(0x100u);
    case 'w': go_note(letter, s[0], s[1] & 0xffffu, 0); return 0x7800u + go_answer(0x100u);   /* 0x492447 (st, word) */
    case 'y': go_note(letter, s[0], s[1] & 0xffu, 0); return 0x7900u + go_answer(0x100u);     /* 0x49085d (st, byte) */
    case 'h': go_note(letter, s[0], s[1], 0); return 0x7a00u + go_answer(0x100u);             /* 0x482f0a (st, x) */
    case 'c': go_note(letter, s[0], s[1], s[2]); return 0x7b00u + go_answer(0x100u);          /* 0x482bf0 (st, x, y) */
    case 'B': case 'b': go_note(letter, s[0], s[1], 0); return 0x7c00u + go_answer(0x100u);  /* 0x4838fd, 0x47a8a4 (obj, 0) */
    default: go_note('x', 0, 0, 0); return 0x7f00u + go_answer(0x100u);                      /* a slot no row calls */
    }
}
/* ---- the host stubs (the original side calls them directly; their
 * addresses are the VAs the own side's dispatch knows) */
#define GO_HOST0(n, letter) static uint32_t __cdecl n(void) { return go_stub(letter, NULL); }
#define GO_HOST1(n, letter) static uint32_t __cdecl n(uint32_t a) { const uint32_t s[1] = {a}; return go_stub(letter, s); }
#define GO_HOST2(n, letter) static uint32_t __cdecl n(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return go_stub(letter, s); }
#define GO_HOST3(n, letter) \
    static uint32_t __cdecl n(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return go_stub(letter, s); }
GO_HOST2(go_slot7, 's')
GO_HOST1(go_slot8, 'e')
GO_HOST0(go_slot9, 'f')
GO_HOST0(go_slot10, 't')
GO_HOST2(go_slot11, 'a')
GO_HOST1(go_slot12, 'r')
GO_HOST1(go_slot16, 'p')
GO_HOST1(go_slot17, 'q')
GO_HOST1(go_slot18, 'u')
GO_HOST1(go_slot32, 'z')
GO_HOST0(go_slot_other, 'x')
GO_HOST3(go_stream_write, 'W')
GO_HOST3(go_stream_read, 'R')
GO_HOST3(go_o_blit, 'L')
GO_HOST2(go_o_word, 'w')
GO_HOST2(go_o_byte, 'y')
GO_HOST2(go_o_header, 'h')
GO_HOST3(go_o_colours, 'c')
GO_HOST2(go_o_base_dtor, 'B')
GO_HOST2(go_o_stream_dtor, 'b')
static const struct { void *host; char letter; unsigned slots; } go_hosts[] = {
    {(void *)go_slot7, 's', 2}, {(void *)go_slot8, 'e', 1}, {(void *)go_slot9, 'f', 0}, {(void *)go_slot10, 't', 0},
    {(void *)go_slot11, 'a', 2}, {(void *)go_slot12, 'r', 1}, {(void *)go_slot16, 'p', 1}, {(void *)go_slot17, 'q', 1},
    {(void *)go_slot18, 'u', 1}, {(void *)go_slot32, 'z', 1}, {(void *)go_slot_other, 'x', 0},
    {(void *)go_stream_write, 'W', 3}, {(void *)go_stream_read, 'R', 3},
};
static uint32_t go_slot_host(unsigned slot) {
    switch (slot) {
    case 7: return (uint32_t)(uintptr_t)go_slot7;
    case 8: return (uint32_t)(uintptr_t)go_slot8;
    case 9: return (uint32_t)(uintptr_t)go_slot9;
    case 10: return (uint32_t)(uintptr_t)go_slot10;
    case 11: return (uint32_t)(uintptr_t)go_slot11;
    case 12: return (uint32_t)(uintptr_t)go_slot12;
    case 16: return (uint32_t)(uintptr_t)go_slot16;
    case 17: return (uint32_t)(uintptr_t)go_slot17;
    case 18: return (uint32_t)(uintptr_t)go_slot18;
    case 32: return (uint32_t)(uintptr_t)go_slot32;
    default: return (uint32_t)(uintptr_t)go_slot_other;
    }
}
static int go_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("gfx objects: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("gfx objects: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
/* Other packages' callees: `row` 1 where the own dispatch has a row (it is
 * replaced by the stub after the seal), 0 where the stub is added. */
static const struct { uint32_t va; void *stub; char letter; unsigned slots; int row; } go_patched[] = {
    {0x004838fdu, (void *)go_o_base_dtor, 'B', 2, 1}, {0x0047a8a4u, (void *)go_o_stream_dtor, 'b', 2, 1},
    {0x004816a8u, (void *)go_o_blit, 'L', 3, 1}, {0x00492447u, (void *)go_o_word, 'w', 2, 1},
    {0x0049085du, (void *)go_o_byte, 'y', 2, 1}, {0x00482f0au, (void *)go_o_header, 'h', 2, 1},
    {0x00482bf0u, (void *)go_o_colours, 'c', 3, 1},
};
#define GO_PATCHED (sizeof(go_patched) / sizeof(go_patched[0]))
static BYTE go_saved[GO_PATCHED][5];

/* ---- the own side */
static void go_s_stub(es2_sim_service_call *call, void *user) { call->eax = go_stub((char)(uintptr_t)user, call->slots); }
static void go_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own gfx objects: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    go_bad = 1;
}
/* The import-level services noted in lc_log as the original side's
 * import stubs note them. */
static struct { uint32_t va; char letter; es2_sim_service_fn real; } go_wrapped[] = {
    {0x0047749cu, 'C', NULL}, {0x0047747cu, 'C', NULL}, {0x00477548u, 'F', NULL}, {0x004967a8u, 'F', NULL},
    {0x0049675au, 'A', NULL}, {0x00496760u, 'N', NULL}, {0x0049681au, 'S', NULL},
};
static void go_wrapper(es2_sim_service_call *call, void *user) {
    for (unsigned i = 0; i < sizeof(go_wrapped) / sizeof(go_wrapped[0]); ++i) {
        if (go_wrapped[i].va != call->va) continue;
        if (go_wrapped[i].letter != 'F' || call->slots[0] || call->va == 0x004967a8u) lc_note(go_wrapped[i].letter);
        go_wrapped[i].real(call, user);
        return;
    }
}
static int go_dispatch_setup(void) {
    es2_sim_dispatch_init(&go_dispatch, go_entries, 4096, go_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&go_dispatch, &s)) return fail("gfx objects: dispatch");
    for (unsigned i = 0; i < GO_PATCHED; ++i)
        if (!go_patched[i].row &&
            es2_sim_dispatch_add_service(&go_dispatch, go_patched[i].va, "gfx objects stub", go_s_stub, (void *)(uintptr_t)go_patched[i].letter,
                                         0, (uint8_t)go_patched[i].slots))
            return fail("gfx objects: dispatch (stub)");
    for (unsigned i = 0; i < sizeof(go_hosts) / sizeof(go_hosts[0]); ++i)
        if (es2_sim_dispatch_add_service(&go_dispatch, (uint32_t)(uintptr_t)go_hosts[i].host, "gfx objects slot", go_s_stub,
                                         (void *)(uintptr_t)go_hosts[i].letter, 0, (uint8_t)go_hosts[i].slots))
            return fail("gfx objects: dispatch (slot)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&go_dispatch, &error, &bad)) return fail("gfx objects: dispatch seal");
    for (unsigned i = 0; i < GO_PATCHED; ++i) {
        if (!go_patched[i].row) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&go_dispatch, go_patched[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("gfx objects: service missing");
        e->service = go_s_stub;
        e->service_user = (void *)(uintptr_t)go_patched[i].letter;
    }
    for (unsigned i = 0; i < sizeof(go_wrapped) / sizeof(go_wrapped[0]); ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&go_dispatch, go_wrapped[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("gfx objects: heap service missing");
        go_wrapped[i].real = e->service;
        e->service = go_wrapper;
    }
    return 0;
}

/* ---- the synthetic world */
static uint32_t go_seed;
static uint32_t go_rand(uint32_t n) {
    go_seed = go_seed * 1103515245u + 12345u;
    return (go_seed >> 8) % n;
}
static uint32_t go_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
static uint32_t go_random_block(uint32_t n) {
    uint32_t b = go_alloc(n);
    for (uint32_t i = 0; i < n; ++i) go_put8(b + i, (uint8_t)go_rand(256u));
    return b;
}
static uint32_t go_text_block(const char *t) {
    uint32_t b = go_alloc(16u);
    for (uint32_t i = 0; t[i]; ++i) go_put8(b + i, (uint8_t)t[i]);
    return b;
}
static void go_build(void) {
    go_world *w = &go_w;
    memset(w, 0, sizeof(*w));
    /* The synthetic driver table (format 5) and the current table. */
    w->table = go_alloc(4u * 0x24u);
    for (unsigned i = 0; i < 0x24u; ++i) go_put32(w->table + 4u * i, go_slot_host(i));
    go_put32(0x004a5840u, 5u);
    go_put32(0x004a5844u, w->table);
    uint32_t current = go_rand(2u) ? w->table : 0x004a586cu;
    for (unsigned i = 0; i < 0x24u; ++i) go_put32(0x004a57acu + 4u * i, go_get(current + 4u * i));
    for (unsigned k = 0; k < GO_DEVICES; ++k) w->device[k] = go_random_block(0x40u);
    for (unsigned k = 0; k < GO_SURFACES; ++k) {
        uint32_t s = w->surface[k] = go_random_block(0xa0u);
        go_put32(s + 0x30u, go_format[k]);
        int attached = go_format[k] != 9u && go_rand(3u) != 0;
        /* The flag word: bit 0 = attached; sometimes other bits. */
        go_put32(s, (attached ? 1u : 0u) | (go_rand(4u) == 0 ? (go_rand(0x100u) << 4) : 0u));
        if (attached || go_format[k] == 9u) {
            go_put32(s + 0x40u, go_random_block(0x22cu));
            go_put32(s + 0x44u, go_random_block(0x22cu));
        } else {
            go_put32(s + 0x40u, 0);
            go_put32(s + 0x44u, 0);
        }
        if (go_format[k] == 3u) go_put32(s + 4u, go_rand(4u) ? go_random_block(0x20u) : 0u);   /* the row table slot 12 frees */
    }
    for (unsigned k = 0; k < GO_FRESH; ++k) w->fresh[k] = go_random_block(0xa0u);
    for (unsigned k = 0; k < GO_GCS; ++k) w->gc[k] = go_random_block(0x240u);
    for (unsigned k = 0; k < GO_BITMAPS; ++k) w->bitmap[k] = go_random_block(0x20u);
    w->svt = go_alloc(0x20u);
    go_put32(w->svt + 0x14u, (uint32_t)(uintptr_t)go_stream_write);
    go_put32(w->svt + 0x18u, (uint32_t)(uintptr_t)go_stream_read);
    for (unsigned k = 0; k < GO_STREAMS; ++k) {
        w->stream[k] = go_random_block(0x10u);
        go_put32(w->stream[k], w->svt);
    }
    for (unsigned k = 0; k < 2u; ++k) { w->point[k] = go_random_block(8u); w->block[k] = go_random_block(0x20u); }
    w->desc = go_random_block(0x10u);
    w->name[0] = 0x004a2ea4u;   /* "VGA_8" */
    w->name[1] = 0x004a2eaau;   /* "VGA_4" */
    w->name[2] = 0x004a2eb0u;   /* "BMP_8" */
    w->name[3] = 0x004a0841u;   /* "BMP_8" elsewhere */
    w->name[4] = go_text_block("BMP_");
    w->name[5] = go_text_block(go_rand(2u) ? "VGA_88" : "XYZ");
    /* Device slots 0, 2 and 7 (the current surface), the handler table, the
     * registration count. */
    go_put32(0x006c5eb4u, go_rand(2u) ? w->device[go_rand(GO_DEVICES)] : 0u);
    go_put32(0x006c5ebcu, go_rand(2u) ? w->device[go_rand(GO_DEVICES)] : 0u);
    go_put32(0x006c5ed0u, go_rand(4u) ? w->surface[go_rand(GO_SURFACES)] : 0u);
    for (unsigned i = 0; i < 10u; ++i)
        if (go_rand(4u) != 0) {
            go_put32(0x006c5ed8u + 12u * i, 1u);
            go_put32(0x006c5edcu + 12u * i, 0x5000u + i);
        }
    if (go_rand(4u) == 0) go_put32(0x004a2f24u, 0);
}

/* ---- the rows */
enum { GO_RECT_GET, GO_RECT_SET, GO_HANDLER_ADD, GO_HANDLER_REMOVE, GO_SLOT2, GO_DEVICE_DESTROY, GO_DEVICE_WORD, GO_DEVICE_CLEAR,
       GO_SLOT0, GO_GC_CONSTRUCT, GO_GC_DESTROY, GO_SURFACE_CLIP, GO_SET_CLIP, GO_REGISTER, GO_MINUS_ONE, GO_RELEASE,
       GO_SURFACE_CONSTRUCT, GO_SURFACE_DELETE, GO_CREATE, GO_DESTROY, GO_SELECT, GO_FORMAT, GO_SET, GO_CURRENT, GO_FLAGS,
       GO_UNSELECT, GO_CLAIM, GO_BITMAP_CONSTRUCT, GO_STREAM_DESTROY, GO_BLIT_ORIGIN, GO_BLIT_AT, GO_SIZE_D, GO_SIZE_1A, GO_WRITE,
       GO_READ_BYTE, GO_OPS };
static const struct { uint32_t va; unsigned n; int clobber; const char *name; } go_ops[GO_OPS] = {
    {0x00480738u, 2, 0, "rect get"}, {0x00480764u, 2, 0, "rect set"}, {0x00480848u, 3, 0, "handler add"},
    {0x00480880u, 2, 0, "handler remove"}, {0x00480ae4u, 0, 0, "slot 2"}, {0x00480b38u, 2, 0, "device destroy"},
    {0x00480b78u, 2, 0, "device word"}, {0x00480ba9u, 2, 0, "device clear"}, {0x00480bbau, 0, 0, "slot 0"},
    {0x00480c34u, 1, 0, "gc construct"}, {0x00480dcfu, 2, 0, "gc destroy"}, {0x00480ec4u, 1, 0, "surface clip"},
    {0x00480ecfu, 2, 0, "set clip"}, {0x00480ef4u, 0, 0, "register"}, {0x00480f08u, 0, 0, "minus one"},
    {0x00480f38u, 0, 1, "release"}, {0x00480f58u, 2, 0, "surface construct"}, {0x00480ffcu, 2, 1, "surface delete"},
    {0x00481028u, 2, 0, "create"}, {0x004811e8u, 1, 1, "destroy"}, {0x00481224u, 1, 0, "select"},
    {0x004812f0u, 2, 0, "format"}, {0x004813f8u, 3, 0, "set"}, {0x0048151du, 0, 0, "current"},
    {0x00481526u, 1, 0, "flags"}, {0x0048154cu, 0, 0, "unselect"}, {0x00481559u, 1, 0, "claim"},
    {0x0048156cu, 1, 0, "bitmap construct"}, {0x00481bf1u, 2, 0, "stream destroy"}, {0x00481c91u, 1, 0, "blit origin"},
    {0x00481cbau, 2, 0, "blit at"}, {0x00481d5cu, 1, 0, "size d"}, {0x00481d74u, 1, 0, "size 1a"},
    {0x00481e98u, 2, 0, "write"}, {0x00481f3du, 2, 0, "read byte"},
};
typedef struct go_op { unsigned op; uint32_t a[3]; uint32_t mask; int quirk; } go_op;

/* ---- coverage (from the world before a call on the original side) */
enum { GOC_HANDLER_FREE, GOC_HANDLER_FULL, GOC_DESTROY_NULL, GOC_DESTROY_SLOT, GOC_DESTROY_NO_SLOT, GOC_DESTROY_DELETE,
       GOC_WORD_ODD, GOC_WORD_EVEN, GOC_GC_NULL, GOC_GC_KEEP, GOC_GC_DELETE, GOC_CLIP_CURRENT, GOC_CLIP_OTHER, GOC_REGISTER_ZERO,
       GOC_REGISTER, GOC_RELEASE_NONE, GOC_RELEASE_DETACHED, GOC_RELEASE_ATTACHED, GOC_CONSTRUCT_SLOT_FREE, GOC_CONSTRUCT_SLOT_SET,
       GOC_DELETE_NULL, GOC_DELETE_DETACHED, GOC_DELETE_ATTACHED, GOC_DELETE_FREE, GOC_CREATE_SET, GOC_CREATE_RUN,
       GOC_DESTROY_ATTACHED, GOC_DESTROY_DETACHED, GOC_DESTROY_BMP8, GOC_SELECT_5, GOC_SELECT_3, GOC_SELECT_NONE,
       GOC_SELECT_CURRENT_SET, GOC_FORMAT_1, GOC_FORMAT_0, GOC_FORMAT_3, GOC_FORMAT_4, GOC_SET_HIGH, GOC_SET_DEFAULT,
       GOC_SET_2_CURRENT, GOC_SET_2_OTHER, GOC_SET_FIELD_CURRENT, GOC_SET_FIELD_OTHER, GOC_SET_16, GOC_SET_17_STUB,
       GOC_SET_17_EMPTY, GOC_CLAIM_NULL, GOC_CLAIM_SET, GOC_CLAIM_TAKEN, GOC_STREAM_NULL, GOC_STREAM_KEEP, GOC_STREAM_DELETE,
       GOC_SIZE_NEGATIVE, GOC_SIZE_POSITIVE, GOC_WRITE_HIGH_BITS, GOC_COUNT };
static const char *const go_cv_names[GOC_COUNT] = {
    "handler free", "handler full", "device destroy null", "device destroy slot", "device destroy no slot",
    "device destroy delete", "device word odd", "device word even", "gc destroy null", "gc destroy keep", "gc destroy delete",
    "set clip current", "set clip other", "register zero", "register", "release none", "release detached", "release attached",
    "construct slot free", "construct slot set", "delete null", "delete detached", "delete attached", "delete free",
    "create flag set", "create run", "destroy attached", "destroy detached", "destroy BMP_8", "select format 5",
    "select BMP_8", "select no table", "select with a current", "format 1", "format 0", "format 3", "format 4",
    "set which > 0x11", "set default", "set 2 current", "set 2 other", "set field current", "set field other", "set 16",
    "set 17 stub", "set 17 empty body", "claim null", "claim set", "claim taken", "stream destroy null", "stream destroy keep",
    "stream destroy delete", "size negative", "size positive", "write high bits",
};
static unsigned go_cv[GOC_COUNT];
static void go_c(unsigned k) { ++go_cv[k]; }

/* ---- argument choice (original side, before the call) */
static uint32_t go_current(void) { return go_get(0x006c5ed0u); }
static int go_surface_index(uint32_t s) {
    for (unsigned k = 0; k < GO_SURFACES; ++k) if (go_w.surface[k] == s) return (int)k;
    return -1;
}
static int go_live_surface(uint32_t s) {
    int k = go_surface_index(s);
    return k >= 0 && !go_w.dead_surface[k];
}
/* A clip record the original can read: format 9's always, else attached. */
static int go_has_clip(uint32_t s) {
    int k = go_surface_index(s);
    return k >= 0 && !go_w.dead_surface[k] && (go_format[k] == 9u || (go_get(s) & 1u));
}
static uint32_t go_pick_surface(void) {
    for (unsigned t = 0; t < 16u; ++t) {
        unsigned k = random32() % GO_SURFACES;
        if (!go_w.dead_surface[k]) return go_w.surface[k];
    }
    return 0;
}
static int go_pick_live(const uint32_t *objs, const uint8_t *dead, unsigned n, uint32_t *out) {
    for (unsigned t = 0; t < 16u; ++t) {
        unsigned k = random32() % n;
        if (!dead[k]) { *out = objs[k]; return (int)k; }
    }
    return -1;
}
/* 1 when the arguments are set, 0 when the row cannot run on this world. */
static int go_args(go_op *o) {
    go_world *w = &go_w;
    uint32_t *a = o->a;
    uint32_t cur = go_current();
    int k;
    a[0] = a[1] = a[2] = 0;
    o->mask = ~0u;
    o->quirk = 0;
    switch (o->op) {
    case GO_RECT_GET: case GO_RECT_SET:
        if (go_pick_live(w->device, w->dead_device, GO_DEVICES, &a[0]) < 0) return 0;
        a[1] = w->block[random32() % 2u];
        return 1;
    case GO_HANDLER_ADD: {
        a[0] = random32(); a[1] = random32(); a[2] = random32();
        unsigned used = 0;
        for (unsigned i = 0; i < 10u; ++i) used += go_get(0x006c5ed8u + 12u * i) != 0;
        go_c(used == 10u ? GOC_HANDLER_FULL : GOC_HANDLER_FREE);
        return 1;
    }
    case GO_HANDLER_REMOVE: a[0] = random32(); a[1] = (random32() << 16) | (random32() % 10u); return 1;
    case GO_SLOT2: case GO_SLOT0: case GO_MINUS_ONE: case GO_CURRENT: return 1;
    case GO_DEVICE_DESTROY:
        a[1] = random32() % 4u;
        if (random32() % 5u == 0 || (k = go_pick_live(w->device, w->dead_device, GO_DEVICES, &a[0])) < 0) {
            a[0] = 0;
            go_c(GOC_DESTROY_NULL);
            return 1;
        }
        go_c(go_get(0x006c5eb4u) ? GOC_DESTROY_SLOT : GOC_DESTROY_NO_SLOT);
        if (a[1] & 1u) { w->dead_device[k] = 1; o->mask = 0; go_c(GOC_DESTROY_DELETE); }
        return 1;
    case GO_DEVICE_WORD:
        if (go_pick_live(w->device, w->dead_device, GO_DEVICES, &a[0]) < 0) return 0;
        a[1] = random32();
        go_c(go_get(a[0]) & 1u ? GOC_WORD_ODD : GOC_WORD_EVEN);
        return 1;
    case GO_DEVICE_CLEAR: a[0] = random32(); a[1] = random32() % 10u; return 1;
    case GO_GC_CONSTRUCT:
        if (!go_has_clip(cur)) return 0;
        return go_pick_live(w->gc, w->dead_gc, GO_GCS, &a[0]) >= 0;
    case GO_GC_DESTROY:
        a[1] = random32() % 4u;
        if (random32() % 5u == 0 || (k = go_pick_live(w->gc, w->dead_gc, GO_GCS, &a[0])) < 0) {
            a[0] = 0; o->mask = 0; go_c(GOC_GC_NULL);
            return 1;
        }
        if (a[1] & 1u) { w->dead_gc[k] = 1; o->mask = 0; go_c(GOC_GC_DELETE); } else go_c(GOC_GC_KEEP);
        return 1;
    case GO_SURFACE_CLIP: case GO_FLAGS: a[0] = go_pick_surface(); return a[0] != 0;
    case GO_SET_CLIP:
        a[0] = go_pick_surface();
        if (!a[0]) return 0;
        a[1] = random32();
        go_c(a[0] == cur ? GOC_CLIP_CURRENT : GOC_CLIP_OTHER);
        return 1;
    case GO_REGISTER:
        if (go_get(0x004a1690u) >= 4u) return 0;
        go_c(go_get(0x004a2f24u) ? GOC_REGISTER : GOC_REGISTER_ZERO);
        return 1;
    case GO_RELEASE:
        if (!cur) { go_c(GOC_RELEASE_NONE); return 1; }
        if (go_surface_index(cur) >= 0 && !go_live_surface(cur)) return 0;
        if (go_get(cur) & 1u) { o->quirk = 1; go_c(GOC_RELEASE_ATTACHED); } else go_c(GOC_RELEASE_DETACHED);
        return 1;
    case GO_SURFACE_CONSTRUCT:
        a[0] = w->fresh[random32() % GO_FRESH];
        a[1] = w->name[random32() % GO_NAMES];
        go_c(go_get(0x006c5ed0u) ? GOC_CONSTRUCT_SLOT_SET : GOC_CONSTRUCT_SLOT_FREE);
        return 1;
    case GO_SURFACE_DELETE:
        a[1] = random32() % 4u;
        if (random32() % 5u == 0) { go_c(GOC_DELETE_NULL); return 1; }
        a[0] = go_pick_surface();
        if (!a[0]) return 0;
        if ((a[1] & 1u) && a[0] == cur) a[1] &= ~1u;
        if (go_get(a[0]) & 1u) { o->quirk = 1; go_c(GOC_DELETE_ATTACHED); } else go_c(GOC_DELETE_DETACHED);
        if (a[1] & 1u) { w->dead_surface[go_surface_index(a[0])] = 1; o->mask = 0; go_c(GOC_DELETE_FREE); }
        return 1;
    case GO_CREATE:
        for (unsigned t = 0; t < 8u && !a[0]; ++t) {
            unsigned j = random32() % GO_SURFACES;
            if (go_format[j] == 5u && !w->dead_surface[j]) a[0] = w->surface[j];
        }
        if (!a[0]) return 0;
        a[1] = w->desc;
        go_c(go_get(a[0]) ? GOC_CREATE_SET : GOC_CREATE_RUN);
        return 1;
    case GO_DESTROY:
        a[0] = go_pick_surface();
        if (!a[0]) return 0;
        if (go_get(a[0]) & 1u) {
            o->quirk = 1;
            go_c(GOC_DESTROY_ATTACHED);
            if (go_get(a[0] + 0x30u) == 3u) go_c(GOC_DESTROY_BMP8);
        } else {
            go_c(GOC_DESTROY_DETACHED);
        }
        return 1;
    case GO_SELECT:
        for (unsigned t = 0; t < 8u && !a[0]; ++t) {
            uint32_t s = go_pick_surface();
            if (go_has_clip(s)) a[0] = s;
        }
        if (!a[0]) return 0;
        switch (go_get(a[0] + 0x30u)) {
        case 5: go_c(GOC_SELECT_5); break;
        case 3: go_c(GOC_SELECT_3); break;
        default: go_c(GOC_SELECT_NONE); break;
        }
        if (cur) go_c(GOC_SELECT_CURRENT_SET);
        return 1;
    case GO_FORMAT: {
        a[0] = random32();
        unsigned j = random32() % GO_NAMES;
        a[1] = w->name[j];
        static const unsigned code[GO_NAMES] = {GOC_FORMAT_1, GOC_FORMAT_0, GOC_FORMAT_3, GOC_FORMAT_3, GOC_FORMAT_4, GOC_FORMAT_4};
        go_c(code[j]);
        return 1;
    }
    case GO_SET: {
        a[0] = go_pick_surface();
        if (!a[0]) return 0;
        if (random32() % 3u == 0 && cur && go_live_surface(cur)) a[0] = cur;
        static const uint32_t which[] = {2u, 2u, 9u, 10u, 11u, 16u, 17u, 17u, 0u, 1u, 5u, 0x12u, 0x80000001u};
        a[1] = which[random32() % (sizeof(which) / sizeof(which[0]))];
        a[2] = random32();
        int current = a[0] == cur;
        switch (a[1]) {
        case 2: go_c(current ? GOC_SET_2_CURRENT : GOC_SET_2_OTHER); break;
        case 9: case 10: case 11: go_c(current ? GOC_SET_FIELD_CURRENT : GOC_SET_FIELD_OTHER); break;
        case 16: go_c(GOC_SET_16); break;
        case 17: go_c(go_get(0x004a582cu) == 0x004898f6u ? GOC_SET_17_EMPTY : GOC_SET_17_STUB); break;
        default: go_c(a[1] > 0x11u ? GOC_SET_HIGH : GOC_SET_DEFAULT); break;
        }
        return 1;
    }
    case GO_UNSELECT: return 1;
    case GO_CLAIM:
        a[0] = random32() % 4u == 0 ? 0u : go_pick_surface();
        go_c(!a[0] ? GOC_CLAIM_NULL : cur ? GOC_CLAIM_TAKEN : GOC_CLAIM_SET);
        return 1;
    case GO_BITMAP_CONSTRUCT: return go_pick_live(w->bitmap, w->dead_bitmap, GO_BITMAPS, &a[0]) >= 0;
    case GO_STREAM_DESTROY:
        a[1] = random32() % 4u;
        if (random32() % 5u == 0) { o->mask = 0; go_c(GOC_STREAM_NULL); return 1; }
        if (random32() % 2u) {
            if ((k = go_pick_live(w->stream, w->dead_stream, GO_STREAMS, &a[0])) < 0) return 0;
            if (a[1] & 1u) w->dead_stream[k] = 1;
        } else {
            if ((k = go_pick_live(w->bitmap, w->dead_bitmap, GO_BITMAPS, &a[0])) < 0) return 0;
            if (a[1] & 1u) w->dead_bitmap[k] = 1;
        }
        if (a[1] & 1u) { o->mask = 0; go_c(GOC_STREAM_DELETE); } else go_c(GOC_STREAM_KEEP);
        return 1;
    case GO_BLIT_ORIGIN: case GO_SIZE_D: case GO_SIZE_1A:
        if (go_pick_live(w->bitmap, w->dead_bitmap, GO_BITMAPS, &a[0]) < 0) return 0;
        if (o->op != GO_BLIT_ORIGIN) go_c((int16_t)go_get16(a[0] + 0x14u) < 0 ? GOC_SIZE_NEGATIVE : GOC_SIZE_POSITIVE);
        return 1;
    case GO_BLIT_AT:
        if (go_pick_live(w->bitmap, w->dead_bitmap, GO_BITMAPS, &a[0]) < 0) return 0;
        a[1] = w->point[random32() % 2u];
        return 1;
    case GO_WRITE:
        if (go_pick_live(w->bitmap, w->dead_bitmap, GO_BITMAPS, &a[0]) < 0) return 0;
        if (go_pick_live(w->stream, w->dead_stream, GO_STREAMS, &a[1]) < 0) return 0;
        if (go_get8(a[0] + 9u) & 0xe0u) go_c(GOC_WRITE_HIGH_BITS);
        return 1;
    case GO_READ_BYTE:
        if (go_pick_live(w->stream, w->dead_stream, GO_STREAMS, &a[0]) < 0) return 0;
        a[1] = w->point[random32() % 2u];
        return 1;
    default: return 0;
    }
}

/* ---- the original side's calls. A thunk with arguments that keeps the
 * harness's EBX/ESI/EDI (ESI 0x11111111, EDI 0x22222222, EBX 0x33333333 at
 * entry); after[0..2] = ESI, EDI and EBX at the callee's return. */
extern uint32_t go_call_clobber(uint32_t target, const uint32_t *args, uint32_t n, uint32_t *after);
__asm__(".text\n.globl _go_call_clobber\n_go_call_clobber:\n"
        "\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %edx\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%edx,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 8(%ebp), %eax\n\tmovl $0x11111111, %esi\n\tmovl $0x22222222, %edi\n\tmovl $0x33333333, %ebx\n"
        "\tcall *%eax\n"
        "\tmovl 20(%ebp), %ecx\n\tmovl %esi, 0(%ecx)\n\tmovl %edi, 4(%ecx)\n\tmovl %ebx, 8(%ecx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");
static unsigned go_quirks;
static uint32_t go_machine(const go_op *o, unsigned k) {
    if (!go_ops[o->op].clobber) return lc_machine(go_ops[o->op].va, o->a, go_ops[o->op].n, NULL);
    uint32_t after[3];
    uint32_t eax = go_call_clobber(go_ops[o->op].va, o->a, go_ops[o->op].n, after);
    uint32_t esi = o->quirk ? 0u : 0x11111111u, edi = o->quirk ? 0x11111111u : 0x22222222u;
    if (after[0] != esi || after[1] != edi || after[2] != 0x33333333u) {
        fprintf(stderr, "FAIL: gfx objects case %u: %s left ESI %08lx EDI %08lx EBX %08lx (expected %08lx %08lx 33333333)\n", k,
                go_ops[o->op].name, (unsigned long)after[0], (unsigned long)after[1], (unsigned long)after[2], (unsigned long)esi,
                (unsigned long)edi);
        go_bad = 1;
    }
    if (o->quirk) ++go_quirks;
    return eax;
}
static uint32_t go_own_call(uint32_t va, const uint32_t *a, unsigned n) {
    es2_sim_result r;
    uint32_t regs[3] = {0, 0, 0};
    memset(&r, 0, sizeof(r));
    if (es2_sim_call(&go_dispatch, va, a, n, regs, &r)) go_bad = 1;
    return r.eax;
}

static int go_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u + (seed % 0x100000u) * 16u, 1);
    lc_loop.dispatch = &go_dispatch;
    go_side = side;
    go_hash[side] = 0xcbf29ce484222325ull;
    go_count[side] = 0;
    go_len[side] = 0;
    go_text[side][0] = 0;
    go_answer_seed = answers;
    go_seed = seed;
    go_build();
    if (side) {
        if (memcmp(go_w.surface, go_w0.surface, sizeof(go_w.surface)) || go_w.stream[1] != go_w0.stream[1])
            return fail("gfx objects: the sides' worlds differ (heap order)");
        go_w = go_w0;   /* the dead flags as the original side's choices left them */
    }
    return 0;
}
static unsigned go_calls[GO_OPS];
static int go_case(unsigned k) {
    unsigned ops = 1u + random32() % 10u;
    static go_op list[10];
    uint32_t seed = random32(), answers = random32();
    static uint32_t eax[2][10];
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % GO_OPS;
    uint32_t atexit_side[2][32], atexit_count[2];
    unsigned done = 0;
    for (unsigned side = 0; side < 2u; ++side) {
        if (go_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !go_bad && !lc_bad; ++i) {
            go_op *o = &list[i];
            if (side == 0) {
                /* A row that cannot run on this world becomes "current". */
                if (!go_args(o)) { o->op = GO_CURRENT; go_args(o); }
                ++go_calls[o->op];
            }
            uint32_t r = side ? go_own_call(go_ops[o->op].va, o->a, go_ops[o->op].n) : go_machine(o, k);
            eax[side][i] = r & o->mask;
        }
        if (side == 0) go_w0 = go_w;
        if (go_bad || lc_bad) { fprintf(stderr, "FAIL: gfx objects case %u: a call failed (side %u)\n", k, side); return 1; }
        if (side == 0) {
            memcpy(atexit_side[0], lc_atexit_table, sizeof(lc_atexit_table));
            atexit_count[0] = lc_atexit_count;
        } else {
            memcpy(atexit_side[1], lc_loop.atexit_table, sizeof(atexit_side[1]));
            atexit_count[1] = lc_loop.atexit_count;
        }
        su_take(side);
        done = side + 1u;
    }
    (void)done;
    for (unsigned i = 0; i < ops; ++i)
        if (eax[0][i] != eax[1][i]) {
            fprintf(stderr, "FAIL: gfx objects case %u: %s (call %u) EAX %08lx / %08lx\n", k, go_ops[list[i].op].name, i,
                    (unsigned long)eax[0][i], (unsigned long)eax[1][i]);
            return 1;
        }
    if (su_compare("gfx objects", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", go_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (go_hash[0] != go_hash[1] || go_count[0] != go_count[1]) {
        fprintf(stderr, "FAIL: gfx objects case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, go_count[0], go_count[1],
                go_text[0], go_text[1]);
        return 1;
    }
    if (strcmp(lc_log[0], lc_log[1]) || atexit_count[0] != atexit_count[1] ||
        memcmp(atexit_side[0], atexit_side[1], 4u * (atexit_count[0] < 32u ? atexit_count[0] : 32u))) {
        fprintf(stderr, "FAIL: gfx objects case %u: imports %s / %s, atexit %lu / %lu\n", k, lc_log[0], lc_log[1],
                (unsigned long)atexit_count[0], (unsigned long)atexit_count[1]);
        return 1;
    }
    return 0;
}

static int gfx_objects_cases(void) {
    current_function = "gfx objects";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("gfx objects: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("gfx objects: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)lc_calloc}, {"cw3220.dll", "_free", (void *)lc_free},
        {"cw3220.dll", "_atexit", (void *)lc_atexit}, {"cw3220.dll", "_strncpy", (void *)lc_strncpy},
        {"cw3220.dll", "_memset", (void *)lc_memset}, {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) return fail("gfx objects: import slot not found");
    for (unsigned i = 0; i < GO_PATCHED; ++i)
        if (go_patch(go_patched[i].va, go_patched[i].stub, go_saved[i], 1)) return 1;
    if (go_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 6000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = go_case(k);
    }
    if (!bad) {
        printf("PASS: gfx objects: %u cases identical\n", cases);
        printf("GFX OBJECTS rows called:");
        for (unsigned i = 0; i < GO_OPS; ++i) printf(" %s %u%s", go_ops[i].name, go_calls[i], i + 1u < GO_OPS ? "," : "\n");
        printf("GFX OBJECTS coverage:");
        for (unsigned i = 0; i < GOC_COUNT; ++i) printf(" %s %u%s", go_cv_names[i], go_cv[i], i + 1u < GOC_COUNT ? "," : "\n");
        printf("GFX OBJECTS stub effects: slot 8 fails %u, slot 9 fails %u, attach field %u; 0x4894a5 register quirk seen %u\n",
               go_effects[GOE_SLOT8_FAIL], go_effects[GOE_SLOT9_FAIL], go_effects[GOE_ATTACH_FIELD], go_quirks);
        for (unsigned i = 0; i < GOC_COUNT; ++i)
            if (!go_cv[i]) { fprintf(stderr, "FAIL: gfx objects: path '%s' never taken\n", go_cv_names[i]); bad = 1; }
        for (unsigned i = 0; i < GO_OPS; ++i)
            if (!go_calls[i]) { fprintf(stderr, "FAIL: gfx objects: row '%s' never called\n", go_ops[i].name); bad = 1; }
        for (unsigned i = 0; i < GOE_COUNT; ++i)
            if (!go_effects[i]) { fprintf(stderr, "FAIL: gfx objects: stub effect %u never taken\n", i); bad = 1; }
        if (!go_quirks) { fprintf(stderr, "FAIL: gfx objects: the register quirk never seen\n"); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < GO_PATCHED; ++i) go_patch(go_patched[i].va, NULL, go_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
