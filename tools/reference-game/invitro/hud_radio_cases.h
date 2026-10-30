/* The radio panel's port callbacks 0x44b514/0x44b5e8, the voice start 0x44aff0,
 * the radio record helpers 0x44ae1c/0x44b864/0x44b920/ 0x44b928, the HDD legend
 * 0x44ba54, the HDD button slots 0x44be18/ 0x44be78, the MFD select click
 * 0x447658 and the throttle slider slots 0x4481c4/0x448738/0x448b58, original
 * machine code against sim/src/hud_radio.c (target
 * :differential_hud_radio_test). Reuses mfd_cases.h's call hash (md_note,
 * md_bytes, md_string), its loop set-up (md_side) and patcher (md_patch), with
 * its own world, presets, recorders and dispatch (h12_/H12_).
 *
 * Side 0 runs the original's code, side 1 the rows (0x44aff0 also as the
 * callee of 0x44b514, unpatched on both sides). Recorders on both sides
 * (side 0: a jump patched over the entry; side 1: a row at the same VA)
 * for every callee: the clock, panel_visible, canvas push (which may
 * switch [0x6c5f70]) and pop, the panel copy, the label, gc_select,
 * condition_record (copying a scripted block), wingman_index, the radio
 * port and its two steps, the sound pair, hdd_radio (which may clear the
 * HDD's speaker), the call-sign number 0x434260, itoa/strcat (doing the
 * work), the path and wave helpers, the voice, mfd_click, the legend
 * colour, the slider rectangle record (scripted pointers, so a second
 * fetch can differ), the knob save/restore (the first may move
 * [0x6c5ed0]) and draw, and host functions for the driver slot [0x4a57ec]
 * and the button/owner tables (rows at their addresses). Pointers are
 * hashed only when they lie in the world or the image (stack addresses
 * differ between the sides; their bytes are hashed instead). Compared: the
 * call hash and count, DATA and heap, the world and the EAX of every
 * function whose callers use it. Not covered (the original overruns its
 * 12-byte locals there; the own code refuses): voice names longer than 11
 * characters. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "mfd_cases.h"
#include "audio_cases.h"

#define H12_SIZE 0x1300u
#define H12_H 0x0000u     /* the HDD gauge (0x800): radio records at +0x12d + 0x14e w */
#define H12_M 0x0800u     /* the message M (0x40) */
#define H12_RS 0x0840u    /* [S] (0x40) */
#define H12_GC 0x0880u    /* two graphic contexts of 0x240 */
#define H12_GC2 0x0ac0u
#define H12_R 0x0d00u     /* the radio context [0x4d1ccc] (0x240) */
#define H12_L 0x0f40u     /* a label (0x24) */
#define H12_TEXT 0x0f80u  /* 8 strings of 0x10 */
#define H12_B 0x1000u     /* the button or slider (0xa0) */
#define H12_VT 0x10a0u    /* three tables of 0x10 */
#define H12_OWN 0x10d0u   /* the owner (0x10) */
#define H12_OBJ 0x10e0u   /* [B+0x42] (0xc0) */
#define H12_E1 0x11a0u    /* [B+0x7e] (0x20) */
#define H12_E2 0x11c0u    /* [B+0x82] (0x20) */
#define H12_RECT 0x11e0u  /* 4 rectangle records of 0x10 */
#define H12_VIEW 0x1220u  /* 2 views of 0x40 */
#define H12_KINDS 33u

static BYTE *h12_world, *h12_start;
static es2_sim_entry h12_entries[4096];
static es2_sim_dispatch h12_dispatch;
static unsigned h12_kinds[H12_KINDS], h12_calls[2][H12_KINDS];
static uint32_t h12_ret[H12_KINDS][8];
static BYTE h12_cond[0x54];
static uint32_t h12_at(uint32_t offset) { return (uint32_t)(uintptr_t)h12_world + offset; }
static void h12_w32(uint32_t offset, uint32_t v) { put32(h12_start + offset, v); }
static void h12_w16(uint32_t offset, uint32_t v) { put16(h12_start + offset, (uint16_t)v); }
static void h12_w8(uint32_t offset, uint32_t v) { h12_start[offset] = (BYTE)v; }
static uint32_t h12_r32(uint32_t offset) { return get32(h12_start + offset); }
static uint32_t h12_p(uint32_t v) {
    uint32_t w = (uint32_t)(uintptr_t)h12_world;
    if ((v >= w && v - w < H12_SIZE) || (v >= 0x00400000u && v < 0x00800000u)) return v;
    return 0x57ac0000u;
}
static void h12_note(unsigned kind, const uint32_t *a, unsigned n) {
    uint32_t v[8] = {0x68310000u | kind};
    for (unsigned i = 0; i < n && i < 7u; ++i) v[i + 1u] = a[i];
    md_note(0, v, n + 1u);
    if (md_side_now) ++h12_kinds[kind];
}
static void h12_data32(uint32_t va, uint32_t v) { put32(mapped + (va - 0x400000u), v); }
static uint32_t h12_record(unsigned kind, const uint32_t *a) {
    unsigned k = h12_calls[md_side_now][kind]++;
    uint32_t ret = h12_ret[kind][k & 7u];
    switch (kind) {
    case 1: case 4: case 10: h12_note(kind, NULL, 0); return ret;   /* clock, canvas pop, port A */
    case 3:   /* canvas push: may switch the current context */
        h12_note(3, NULL, 0);
        if (ret & 0x100u) h12_data32(0x006c5f70u, h12_at(ret & 0x200u ? H12_GC2 : H12_GC));
        return ret;
    case 2: { uint32_t v[1] = {h12_p(a[0])}; h12_note(2, v, 1); return ret; }   /* panel_visible */
    case 5: {   /* panel_copy(f, rect, 0) */
        uint32_t v[3] = {a[0], h12_p(a[1]), a[2]};
        h12_note(5, v, 3);
        md_bytes(a[1], 16u);
        return ret;
    }
    case 6: {   /* label(L, text) */
        uint32_t v[1] = {h12_p(a[0])};
        h12_note(6, v, 1);
        md_bytes(a[0], 4u);
        md_bytes(a[0] + 0x1du, 4u);
        md_string(a[1]);
        return ret;
    }
    case 7: h12_note(7, a, 1); md_bytes(a[0] + 0x210u, 16u); return ret;   /* gc_select(R) */
    case 8: h12_note(8, a, 1); memcpy((BYTE *)(uintptr_t)a[1], h12_cond, sizeof(h12_cond)); return ret;   /* condition_record */
    case 9: h12_note(9, a, 1); return ret;                                    /* wingman_index: scripted */
    case 15:   /* hdd_radio(H): may clear the speaker */
        h12_note(15, a, 1);
        if (ret & 0x100u) put32((BYTE *)(uintptr_t)(a[0] + 0x766u), 0);
        return ret;
    case 16: h12_note(16, a, 1); return ret;   /* 0x434260: a small number */
    case 17: {   /* itoa(v, buf, radix) */
        uint32_t v[2] = {a[0], a[2]};
        h12_note(17, v, 2);
        snprintf((char *)(uintptr_t)a[1], 16, "%ld", (long)(int32_t)a[0]);
        return a[1];
    }
    case 18:   /* strcat(d, s) */
        h12_note(18, NULL, 0);
        md_string(a[0]);
        md_string(a[1]);
        strcat((char *)(uintptr_t)a[0], (const char *)(uintptr_t)a[1]);
        return a[0];
    case 19: case 20: {   /* 0x492bec(name, ext), 0x45ef9c(name, ext) */
        uint32_t v[1] = {a[1]};
        h12_note(kind, v, 1);
        md_string(a[0]);
        return ret;
    }
    case 24: {   /* 0x4390b8(e): one of the rectangle records */
        h12_note(24, a, 1);
        return h12_at(H12_RECT + 0x10u * (ret & 3u));
    }
    case 25: {   /* 0x488143(a, b, n): may move the view */
        uint32_t v[3] = {h12_p(a[0]), h12_p(a[1]), a[2]};
        h12_note(25, v, 3);
        if (ret & 0x100u) h12_data32(0x006c5ed0u, h12_at(H12_VIEW + (ret & 0x200u ? 0x20u : 0u)));
        return ret;
    }
    case 26: { uint32_t v[2] = {h12_p(a[0]), a[1]}; h12_note(26, v, 2); return ret; }
    case 27: {   /* 0x4816a8(knob, rect, flag) */
        uint32_t v[3] = {a[0], h12_p(a[1]), a[2]};
        h12_note(27, v, 3);
        md_bytes(a[1], 16u);
        return ret;
    }
    case 30: { uint32_t v[3] = {h12_p(a[0]), h12_p(a[1]), a[2]}; h12_note(30, v, 3); return ret; }   /* owner slot 0 */
    case 11: case 12: case 21: h12_note(kind, a, 2); return ret;
    case 22: h12_note(22, a, 3); return ret;
    default: h12_note(kind, a, 1); return ret;   /* 13 14 23 28 29 31 32 */
    }
}
#define H12_HOST0(k) static uint32_t __cdecl h12_o##k(void) { return h12_record(k, NULL); }
#define H12_HOST1(k) static uint32_t __cdecl h12_o##k(uint32_t a) { const uint32_t s[1] = {a}; return h12_record(k, s); }
#define H12_HOST2(k) static uint32_t __cdecl h12_o##k(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return h12_record(k, s); }
#define H12_HOST3(k) static uint32_t __cdecl h12_o##k(uint32_t a, uint32_t b, uint32_t c) { \
    const uint32_t s[3] = {a, b, c}; return h12_record(k, s); }
H12_HOST0(1)
H12_HOST1(2)
H12_HOST0(3)
H12_HOST0(4)
H12_HOST3(5)
H12_HOST2(6)
H12_HOST1(7)
H12_HOST2(8)
H12_HOST1(9)
H12_HOST0(10)
H12_HOST2(11)
H12_HOST2(12)
H12_HOST1(13)
H12_HOST1(14)
H12_HOST1(15)
H12_HOST1(16)
H12_HOST3(17)
H12_HOST2(18)
H12_HOST2(19)
H12_HOST2(20)
H12_HOST2(21)
H12_HOST3(22)
H12_HOST1(23)
H12_HOST1(24)
H12_HOST3(25)
H12_HOST2(26)
H12_HOST3(27)
H12_HOST1(28)
H12_HOST1(29)
H12_HOST3(30)
H12_HOST1(31)
H12_HOST1(32)
static void h12_s_stub(es2_sim_service_call *sc, void *user) { sc->eax = h12_record((unsigned)(uintptr_t)user, sc->slots); }
static const struct { uint32_t va; void *stub; unsigned kind, slots; } h12_stubs[] = {
    {0x00467828u, (void *)h12_o1, 1, 0}, {0x00431410u, (void *)h12_o2, 2, 1}, {0x004311e0u, (void *)h12_o3, 3, 0},
    {0x00431210u, (void *)h12_o4, 4, 0}, {0x0043122cu, (void *)h12_o5, 5, 3}, {0x00438920u, (void *)h12_o6, 6, 2},
    {0x00480d44u, (void *)h12_o7, 7, 1}, {0x0041b4ecu, (void *)h12_o8, 8, 2}, {0x00433134u, (void *)h12_o9, 9, 1},
    {0x00433158u, (void *)h12_o10, 10, 0}, {0x00435b14u, (void *)h12_o11, 11, 2}, {0x00435b38u, (void *)h12_o12, 12, 2},
    {0x00462af0u, (void *)h12_o13, 13, 1}, {0x00462830u, (void *)h12_o14, 14, 1}, {0x0044b620u, (void *)h12_o15, 15, 1},
    {0x00434260u, (void *)h12_o16, 16, 1}, {0x00496820u, (void *)h12_o17, 17, 3}, {0x004967eau, (void *)h12_o18, 18, 2},
    {0x00492becu, (void *)h12_o19, 19, 2}, {0x0045ef9cu, (void *)h12_o20, 20, 2}, {0x00462b9cu, (void *)h12_o21, 21, 2},
    {0x004470c0u, (void *)h12_o22, 22, 3}, {0x00434280u, (void *)h12_o23, 23, 1}, {0x004390b8u, (void *)h12_o24, 24, 1},
    {0x00488143u, (void *)h12_o25, 25, 3}, {0x00488012u, (void *)h12_o26, 26, 2}, {0x004816a8u, (void *)h12_o27, 27, 3},
};
#define H12_STUBS (sizeof(h12_stubs) / sizeof(h12_stubs[0]))
static BYTE h12_saved[H12_STUBS][5];
static int h12_put(uint32_t va, const char *name, es2_sim_service_fn fn, void *user, unsigned slots) {
    for (unsigned i = 0; i < h12_dispatch.count; ++i) {
        es2_sim_entry *e = &h12_dispatch.entries[i];
        if (e->va != va) continue;
        e->kind = ES2_SIM_SERVICE; e->name = name; e->service = fn; e->service_user = user; e->pop_bytes = 0; e->stack_slots = (uint8_t)slots;
        return 0;
    }
    return es2_sim_dispatch_add_service(&h12_dispatch, va, name, fn, user, 0, (uint8_t)slots);
}
static int h12_dispatch_setup(void) {
    es2_sim_dispatch_init(&h12_dispatch, h12_entries, 4096, md_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&h12_dispatch, &s)) return fail("hud radio: dispatch");
    for (unsigned i = 0; i < H12_STUBS; ++i)
        if (h12_put(h12_stubs[i].va, "hud radio recorder", h12_s_stub, (void *)(uintptr_t)h12_stubs[i].kind, h12_stubs[i].slots))
            return fail("hud radio: dispatch");
    static const struct { void *host; unsigned kind, slots; } hosts[] = {
        {(void *)h12_o28, 28, 1}, {(void *)h12_o29, 29, 1}, {(void *)h12_o30, 30, 3}, {(void *)h12_o31, 31, 1}, {(void *)h12_o32, 32, 1},
    };
    for (unsigned i = 0; i < sizeof(hosts) / sizeof(hosts[0]); ++i)
        if (h12_put((uint32_t)(uintptr_t)hosts[i].host, "hud radio slot", h12_s_stub, (void *)(uintptr_t)hosts[i].kind, hosts[i].slots))
            return fail("hud radio: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&h12_dispatch, &error, &bad)) return fail("hud radio: dispatch seal");
    return 0;
}

/* ---- the case script */
typedef struct h12_preset { uint32_t va, value; unsigned width; } h12_preset;
static h12_preset h12_presets[64];
static unsigned h12_preset_count;
static void h12_set32(uint32_t va, uint32_t v) { h12_presets[h12_preset_count++] = (h12_preset){va, v, 4}; }
static void h12_set16(uint32_t va, uint32_t v) { h12_presets[h12_preset_count++] = (h12_preset){va, v, 2}; }
static struct { uint32_t va, a[3], in[3]; unsigned n, eax; } h12_op;
static uint32_t h12_out[2];
#define H12_OP(va_, eax_, ...) do { \
    const uint32_t h12_args_[] = {__VA_ARGS__}; \
    h12_op.va = (va_); h12_op.eax = (eax_); h12_op.n = (unsigned)(sizeof(h12_args_) / sizeof(uint32_t)); \
    for (unsigned h12_i_ = 0; h12_i_ < h12_op.n; ++h12_i_) h12_op.a[h12_i_] = h12_args_[h12_i_]; \
    for (unsigned h12_i_ = 0; h12_i_ < 3u; ++h12_i_) h12_op.in[h12_i_] = md_rand(2u) ? random32() : 0u; \
} while (0)
static uint32_t h12_text(unsigned i) { return h12_at(H12_TEXT + 0x10u * (i & 7u)); }
/* The common world and DATA: H at [0x49d230], the contexts, the view. */
static void h12_common(void) {
    memset(md_world_start, 0, MD_WORLD_SIZE);
    h12_preset_count = 0;
    for (unsigned k = 0; k < H12_KINDS; ++k)
        for (unsigned i = 0; i < 8u; ++i) h12_ret[k][i] = random32();
    random_bytes(h12_start, H12_SIZE);
    for (unsigned i = 0; i < 8u; ++i) {
        static const char *const words[] = {"RADIO", "", "Wing", "a", "HDD-2", "Talk", "x", "Legend"};
        memset(h12_start + H12_TEXT + 0x10u * i, 0, 0x10);
        strcpy((char *)h12_start + H12_TEXT + 0x10u * i, words[md_rand(8u)]);
    }
    h12_set32(0x0049d230u, h12_at(H12_H));
    h12_set32(0x004d1ccc, h12_at(H12_R));
    h12_set32(0x004d1cc8u, random32());
    h12_set32(0x006c5f70u, h12_at(md_rand(2u) ? H12_GC : H12_GC2));
    h12_set32(0x006c5ed0u, h12_at(H12_VIEW + (md_rand(2u) ? 0x20u : 0u)));
    h12_set32(0x004a57ecu, (uint32_t)(uintptr_t)h12_o28);
    h12_set32(0x004a3686u, random32());
    h12_set16(0x004d253au, md_rand(4u) ? md_rand(3u) : random32());
    h12_set32(0x004d2556u, md_rand(2u) ? 0u : random32());
    h12_set32(0x004d255au, md_rand(2u) ? 0u : random32());
    h12_set16(0x004d3b88u, random32());
    h12_set16(0x004d3b9au, random32());
    h12_set32(0x0049b0b4u, random32());
    h12_set32(0x0049b0b8u, random32());
}
static void h12_side(unsigned side) {
    md_side(side);
    lc_loop.dispatch = &h12_dispatch;
    memcpy(h12_world, h12_start, H12_SIZE);
    for (unsigned i = 0; i < h12_preset_count; ++i) {
        BYTE *p = mapped + (h12_presets[i].va - 0x400000u);
        if (h12_presets[i].width == 4) put32(p, h12_presets[i].value);
        else put16(p, (uint16_t)h12_presets[i].value);
    }
    memset(h12_calls[side], 0, sizeof(h12_calls[side]));
}
static int h12_run(const char *what, unsigned k) {
    for (unsigned side = 0; side < 2u; ++side) {
        h12_side(side);
        uint32_t out[3] = {0, 0, 0};
        static char where[200];
        snprintf(where, sizeof(where), "hud radio (%s side %u: 0x%08lx(0x%lx, 0x%lx))", what, side, (unsigned long)h12_op.va,
                 (unsigned long)h12_op.a[0], (unsigned long)h12_op.a[1]);
        current_function = where;
        if (!side) au_call((uint32_t)(uintptr_t)(mapped + (h12_op.va - 0x400000u)), h12_op.a, h12_op.n, h12_op.in, out);
        else {
            es2_sim_result r;
            memset(&r, 0, sizeof(r));
            if (es2_sim_call(&h12_dispatch, h12_op.va, h12_op.a, h12_op.n, h12_op.in, &r)) md_bad = 1;
            out[0] = r.eax;
        }
        h12_out[side] = out[0];
        if (md_bad) { fprintf(stderr, "FAIL: hud radio %s case %u (0x%08lx): a call failed\n", what, k, (unsigned long)h12_op.va); return 1; }
        if (!side) memcpy(h12_start + H12_SIZE, h12_world, H12_SIZE);
        su_take(side);
    }
    if (md_hash[0] != md_hash[1] || md_count[0] != md_count[1]) {
        fprintf(stderr, "FAIL: hud radio %s case %u: recorded calls differ (%u / %u)\n", what, k, md_count[0], md_count[1]);
        for (unsigned i = 0; i < md_count[0] && i < 64u; ++i)
            if (md_each[0][i] != md_each[1][i]) { fprintf(stderr, "  first differing call %u\n", i); break; }
        return 1;
    }
    if (su_compare(what, k)) return 1;
    if (memcmp(h12_world, h12_start + H12_SIZE, H12_SIZE)) {
        for (unsigned i = 0, shown = 0; i < H12_SIZE && shown < 16u; ++i)
            if (h12_world[i] != h12_start[H12_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, h12_start[H12_SIZE + i], h12_world[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: hud radio %s case %u: the world differs\n", what, k);
        return 1;
    }
    if (h12_op.eax && h12_out[0] != h12_out[1]) {
        fprintf(stderr, "FAIL: hud radio %s case %u (0x%08lx): EAX original 0x%08lx own 0x%08lx\n", what, k, (unsigned long)h12_op.va,
                (unsigned long)h12_out[0], (unsigned long)h12_out[1]);
        return 1;
    }
    return 0;
}
static unsigned h12_count(unsigned kind, const unsigned *before) { return h12_kinds[kind] - before[kind]; }

/* ---- the radio records: the callbacks, the voice, the helpers */
static void h12_records(void) {
    for (unsigned r = 0; r < 3u; ++r) {
        uint32_t s = H12_H + 0x12du + 0x14eu * r;
        h12_w32(s, h12_at(H12_RS + 8u * md_rand(4u)));
        h12_w16(s + 0x135u, md_rand(4u) ? md_rand(26u) : md_rand(200u));
    }
    h12_w32(H12_H + 0x766u, md_rand(2u) ? 0u : random32());
    /* the message: the word below 10000, the byte any */
    h12_w16(H12_M, md_rand(3u) ? md_rand(10u) : md_rand(4u) ? md_rand(100u) : md_rand(10000u));
    h12_w8(H12_M + 0x16u, md_rand(3u) ? md_rand(10u) : md_rand(3u) ? md_rand(100u) : md_rand(256u));
    for (unsigned i = 0; i < 8u; ++i) {
        static const uint32_t w[] = {0xffffffffu, 0u, 1u, 2u};
        h12_ret[9][i] = w[md_rand(4u)];                                          /* wingman_index */
        h12_ret[16][i] = md_rand(4u) ? md_rand(12u) : (uint32_t)((int32_t)md_rand(109u) - 9);   /* the call-sign number */
        h12_ret[21][i] = md_rand(3u) ? random32() : md_rand(2u) ? 0u : random32() & 0xffffff00u;   /* the voice */
        h12_ret[13][i] = md_rand(2u) ? 0u : random32();                          /* sfx playing */
    }
}
static unsigned h12_cov_radio[12];
static int h12_radio_case(unsigned k) {
    h12_common();
    h12_records();
    unsigned which = md_rand(4u), w = md_rand(3u);
    uint32_t s = H12_H + 0x12du + 0x14eu * w;
    if (which == 0) H12_OP(0x0044b514u, 1, h12_at(H12_M));
    else if (which == 1) H12_OP(0x0044b5e8u, 1, h12_at(H12_M));
    else if (which == 2) H12_OP(0x0044aff0u, 1, random32(), h12_at(H12_M), h12_at(s));
    else H12_OP(0x0044b928u, 1, random32(), w);
    unsigned before[H12_KINDS];
    memcpy(before, h12_kinds, sizeof(before));
    int bad = h12_run("radio", k);
    if (!bad) {
        ++h12_cov_radio[which];
        if (which < 2 && h12_out[1] == 0xffffffffu) ++h12_cov_radio[4];   /* no wingman */
        if (which == 0 && h12_count(1, before)) ++h12_cov_radio[5];       /* the voice started */
        if (which == 0 && h12_count(10, before)) ++h12_cov_radio[6];      /* the port path */
        if (h12_count(14, before)) ++h12_cov_radio[7];                     /* the sound */
        if (which == 0 && h12_count(1, before)) {
            uint32_t s0 = H12_H + 0x12du + 0x14eu * h12_ret[9][0], now = get32(h12_world + H12_H + 0x766u);
            if (now == h12_r32(s0 + 0x137u) && now != h12_r32(H12_H + 0x766u)) ++h12_cov_radio[8];   /* the speaker taken */
            if (now == h12_r32(H12_H + 0x766u) && now) ++h12_cov_radio[9];                           /* the speaker kept */
        }
        if (which == 2 && (h12_out[1] & 0xffu)) ++h12_cov_radio[10];       /* the voice found */
        if (which == 2 && h12_start[H12_M + 0x16u] < 10u && get16(h12_start + H12_M) < 10u) ++h12_cov_radio[11];   /* all paddings */
    }
    return bad;
}
/* The condition class and the radio context. */
static unsigned h12_cov_helpers[8];
static int h12_helpers_case(unsigned k) {
    h12_common();
    unsigned which = md_rand(4u);
    if (which == 0) {
        static const int32_t means[] = {25, 26, 27, 40, 66, 67, 90, 125, 126, 200, 252, 253, 256, 300, 0, -5, 1000};
        int32_t mean = means[md_rand(17u)];
        for (unsigned i = 0; i < 0x54u; i += 2u) put16(h12_cond + i, (uint16_t)random32());
        unsigned noisy = md_rand(5u) == 0;
        for (unsigned i = 0; i < 19u; ++i) {
            int32_t v = noisy && md_rand(2u) ? (int32_t)(int16_t)random32() : mean + (int32_t)md_rand(5u) - 2;
            put16(h12_cond + 2u + 2u * i, (uint16_t)v);
        }
        H12_OP(0x0044ae1cu, 1, random32(), random32());
    } else if (which == 1) H12_OP(0x0044b864u, 1, h12_at(H12_B));
    else if (which == 2) H12_OP(0x0044b920u, 1, 0);
    else {
        uint16_t m = (uint16_t)md_rand(5u);
        h12_w16(H12_H + 0x55cu, m);
        for (unsigned i = 0; i < 5u; ++i) h12_w32(H12_H + 0x548u + 4u * i, h12_text(md_rand(8u)));
        H12_OP(0x0044ba54u, 1, h12_at(H12_H), h12_at(H12_L));
    }
    if (which == 2) h12_op.n = 0;
    int bad = h12_run("helpers", k);
    if (!bad) {
        if (which == 0) ++h12_cov_helpers[h12_out[1] < 5u ? h12_out[1] : 4u];
        else ++h12_cov_helpers[4u + which];
    }
    return bad;
}

/* ---- the HDD button, the MFD select and the throttle sliders: B = H12_B */
static unsigned h12_cov_button[8];
static int h12_button_case(unsigned k) {
    h12_common();
    uint32_t b = H12_B;
    for (unsigned i = 0; i < 8u; ++i) h12_ret[2][i] = md_rand(4u) ? random32() | 1u : random32() & 0xffffff00u;   /* visible */
    h12_w32(b + 0x17u, h12_at(H12_VT));
    h12_w32(b + 0x20u, h12_at(H12_VT + 0x10u));
    h12_w32(b + 0x24u, h12_at(H12_OWN));
    h12_w32(b + 0x28u, md_rand(3u) ? 10u + (uint32_t)md_signed(3u) : md_rand(3u) ? md_rand(3u) : random32());
    h12_w8(b + 0x1fu, md_rand(4u) ? md_rand(4u) : random32());
    h12_w8(b + 0x40u, md_rand(2u) ? 0u : random32());
    h12_w32(H12_VT, (uint32_t)(uintptr_t)h12_o29);
    h12_w32(H12_VT + 4u, (uint32_t)(uintptr_t)h12_o31);
    h12_w32(H12_VT + 0x18u, (uint32_t)(uintptr_t)h12_o32);
    h12_w32(H12_VT + 0x20u, (uint32_t)(uintptr_t)h12_o30);
    h12_w32(H12_OWN, h12_at(H12_VT + 0x20u));
    unsigned which = md_rand(4u);
    if (which == 0) H12_OP(0x0044be18u, 0, h12_at(b));
    else if (which == 1) H12_OP(0x0044be78u, 0, h12_at(b), random32());
    else if (which == 2) H12_OP(0x00447658u, 1, h12_at(b), random32());
    else H12_OP(0x00448b58u, 1, h12_at(b));
    unsigned before[H12_KINDS];
    memcpy(before, h12_kinds, sizeof(before));
    int bad = h12_run("button", k);
    if (!bad) {
        ++h12_cov_button[which];
        if (h12_count(29, before)) ++h12_cov_button[4];   /* slot 0 */
        if (h12_count(5, before)) ++h12_cov_button[5];    /* panel copy */
        if (h12_count(30, before)) ++h12_cov_button[6];   /* press chain */
        if (h12_count(22, before)) ++h12_cov_button[7];   /* mfd_click */
    }
    return bad;
}
static unsigned h12_cov_slider[10];
static uint32_t h12_view;   /* [0x6c5ed0] at entry */
static int h12_slider_case(unsigned k) {
    h12_common();
    uint32_t b = H12_B;
    for (unsigned i = 0; i < 8u; ++i) {
        h12_ret[2][i] = md_rand(5u) ? random32() | 1u : random32() & 0xffffff00u;
        h12_ret[25][i] = random32() & (md_rand(4u) ? 0xfffffcffu : 0xffffffffu);
    }
    h12_w8(b + 0x1fu, md_rand(5u) ? 2u : random32());
    h12_w32(b + 0x42u, h12_at(H12_OBJ));
    h12_w32(H12_OBJ + 0xb1u, md_rand(2u) ? h12_r32(b + 0x4au) : random32());
    h12_w32(b + 0x7eu, h12_at(H12_E1));
    h12_w32(b + 0x82u, h12_at(H12_E2));
    /* coordinates near each other so every comparison goes both ways */
    uint32_t base = md_rand(4u) ? md_rand(400u) : random32();
    static const uint32_t fields[] = {0x2eu, 0x32u, 0x36u, 0x3au};
    for (unsigned i = 0; i < 4u; ++i) h12_w32(b + fields[i], md_rand(6u) ? base + md_signed(40u) : random32());
    h12_w16(b + 0x28u, base + md_signed(40u));
    h12_w16(b + 0x2cu, md_rand(4u) ? md_rand(30u) : random32());
    for (unsigned i = 0; i < 2u; ++i) {
        h12_w32(H12_E1 + 0x18u + 4u * i, base + md_signed(40u));
        h12_w32(H12_E2 + 0x18u + 4u * i, base + md_signed(40u));
    }
    for (unsigned i = 0; i < 16u; ++i) h12_w32(H12_RECT + 4u * i, md_rand(6u) ? base + md_signed(40u) : random32());
    h12_view = 0;
    for (unsigned i = 0; i < h12_preset_count; ++i) if (h12_presets[i].va == 0x006c5ed0u) h12_view = h12_presets[i].value;
    unsigned vertical = md_rand(2u);
    H12_OP(vertical ? 0x004481c4u : 0x00448738u, 0, h12_at(b));
    unsigned before[H12_KINDS];
    memcpy(before, h12_kinds, sizeof(before));
    int bad = h12_run("slider", k);
    if (!bad) {
        ++h12_cov_slider[vertical];
        if (h12_count(27, before)) ++h12_cov_slider[2];        /* drawn */
        if (h12_count(28, before)) ++h12_cov_slider[3];        /* the driver slot */
        if (h12_count(24, before) > 2u) ++h12_cov_slider[4];   /* a record fetched twice */
        if (h12_count(24, before) == 2u) ++h12_cov_slider[5];  /* neither record won */
        if (h12_count(2, before) && !h12_count(25, before)) ++h12_cov_slider[6];   /* hidden */
        if (!h12_count(2, before)) ++h12_cov_slider[7];        /* not requested */
        if (h12_count(27, before) && get32(h12_world + b + 0x4au) != h12_r32(b + 0x4au)) ++h12_cov_slider[8];   /* value moved */
        if (h12_count(25, before) && get32(mapped + (0x006c5ed0u - 0x400000u)) != h12_view) ++h12_cov_slider[9];
    }
    return bad;
}

static int hud_radio_cases(void) {
    current_function = "hud radio";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    md_world = VirtualAlloc(NULL, MD_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    md_world_start = malloc(2u * MD_WORLD_SIZE);
    h12_world = VirtualAlloc(NULL, H12_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    h12_start = malloc(2u * H12_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !md_world || !md_world_start || !h12_world ||
        !h12_start)
        return fail("hud radio: memory");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("hud radio: import slot not found");
        }
    if (h12_dispatch_setup()) return 1;
    for (unsigned i = 0; i < H12_STUBS; ++i) if (md_patch(h12_stubs[i].va, h12_stubs[i].stub, h12_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"radio", h12_radio_case, 2000}, {"helpers", h12_helpers_case, 1500}, {"button", h12_button_case, 1500},
        {"slider", h12_slider_case, 2000},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: hud radio %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    for (unsigned i = 0; i < H12_STUBS; ++i) if (md_patch(h12_stubs[i].va, h12_stubs[i].stub, h12_saved[i], 0)) bad = 1;
    if (!bad) {
        printf("HUD radio coverage: start %u, end %u, voice %u, actor %u; no wingman %u, voice started %u, port path %u, sound %u, "
               "speaker taken %u, speaker kept %u, voice found %u, all paddings %u\n",
               h12_cov_radio[0], h12_cov_radio[1], h12_cov_radio[2], h12_cov_radio[3], h12_cov_radio[4], h12_cov_radio[5], h12_cov_radio[6],
               h12_cov_radio[7], h12_cov_radio[8], h12_cov_radio[9], h12_cov_radio[10], h12_cov_radio[11]);
        printf("HUD radio coverage: class 0/1/2/3/4 %u/%u/%u/%u/%u, context %u, context end %u, legend %u\n", h12_cov_helpers[0],
               h12_cov_helpers[1], h12_cov_helpers[2], h12_cov_helpers[3], h12_cov_helpers[4], h12_cov_helpers[5], h12_cov_helpers[6],
               h12_cov_helpers[7]);
        printf("HUD radio coverage: button redraw %u, press %u, select %u, dirty %u; slot 0 %u, panel copy %u, press chain %u, "
               "mfd_click %u\n",
               h12_cov_button[0], h12_cov_button[1], h12_cov_button[2], h12_cov_button[3], h12_cov_button[4], h12_cov_button[5],
               h12_cov_button[6], h12_cov_button[7]);
        printf("HUD radio coverage: slider h %u, v %u; drawn %u, driver %u, record twice %u, records lost %u, hidden %u, not requested %u, "
               "value moved %u, view moved %u\n",
               h12_cov_slider[0], h12_cov_slider[1], h12_cov_slider[2], h12_cov_slider[3], h12_cov_slider[4], h12_cov_slider[5],
               h12_cov_slider[6], h12_cov_slider[7], h12_cov_slider[8], h12_cov_slider[9]);
    }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
