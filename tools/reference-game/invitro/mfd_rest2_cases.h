/* The MFD status draw 0x43a5a0, the MFD target draw 0x43fe44, the HDD status
 * body 0x450c7c, the radio record draw/frame/line/talk
 * 0x44aea0/0x44b3dc/0x44b148/0x44b344 and the MFD entry steps and order key
 * 0x44703c/0x447070/0x447158, original machine code against sim/src/mfd.c
 * (--mfd-rest2; target :differential_mfd_rest2_test). Included after
 * mfd_rest_cases.h; reuses mfd_cases.h's call hash (md_note, md_bytes,
 * md_string), its loop set-up (md_side) and patcher (md_patch), with its own
 * world, presets, recorders and dispatch (m2_/M2_).
 *
 * Run as the original's code on side 0 and the sim's rows on side 1: the
 * ten functions above (0x44b148 and 0x44b3dc also as callees of 0x44b344
 * and 0x44aea0). Recorders on both sides (side 0: a jump patched over the
 * entry; side 1: a row at the same VA) for every callee: the clock, the
 * stream-B draws, panel_visible, canvas push/pop, the fill (the rectangle
 * and the fill records), the panel copy, the label (the label's colour and
 * +0x1d, the text), the rotated and plain blits, the radio helpers, the
 * entry set, the list step and angle helpers, the jolt record, gc_select,
 * view_setup, the scene/terrain draws (with the DATA words they depend
 * on), the view fill and ellipse, the part values (copying a scripted
 * block), the part state (scripted out[0]/out[1]), the bars and part
 * bitmaps, the runtime strupr/strcat/strlen/itoa/memcmp (as recorders that
 * do the work), the weapon name, the HDD legend/parts, status_mode, the
 * object's slot +0x40 and the driver slot [0x4a57ec] (host functions with
 * rows at their addresses). Pointers are hashed only when they lie in the
 * worlds or the image (stack addresses differ between the sides; their
 * bytes are hashed instead). Compared: the call hash and count, DATA, both
 * worlds (masked: the fill-record pointers, stack addresses) and EAX of the
 * entry steps and the order key. Not covered (the original faults or runs
 * on undefined locals there; the own code refuses): a part type with no
 * value, no part, HDD mode above 2, a condition above 4 before a class. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "mfd_cases.h"
#include "audio_cases.h"

#define M2_SIZE 0x2400u
#define M2_X 0x0000u       /* the page object under test (0x100) */
#define M2_LBL 0x0100u     /* 32 label objects of 0x24 */
#define M2_H 0x0580u       /* the HDD gauge (0x7a0) */
#define M2_V 0x0d20u       /* 3 radio objects of 0xa0 */
#define M2_O 0x0f00u       /* the target object O (0x200) */
#define M2_OVT 0x1100u     /* its table: +0x40 the slot */
#define M2_OX 0x1150u      /* [O+0x45] */
#define M2_D2 0x1170u      /* [O+0x1f2] */
#define M2_D 0x1270u       /* [d2+0xda]: 3 part tables of 0x1c */
#define M2_PARTS 0x12e0u   /* 12 part records of 0x1c */
#define M2_PARTS1 0x1430u  /* 12 more */
#define M2_L 0x1580u       /* [O+0x1fe]: the count, the entries */
#define M2_LE 0x1590u      /* 12 entries of 0x22 */
#define M2_BUF 0x1730u     /* the part values 0x41b4ec copies */
#define M2_BMP 0x1790u     /* 8 bitmap headers of 0x10 */
#define M2_BTBL 0x1810u    /* a bitmap table: +8 the pointers */
#define M2_BPTRS 0x1820u   /* 32 pointers */
#define M2_VIEW 0x18a0u    /* the target view V (0x240) */
#define M2_CAM 0x1ae0u     /* its camera */
#define M2_JOLT 0x1b00u    /* 0x42d870's record */
#define M2_TGT 0x1b10u     /* the target t */
#define M2_CANVAS 0x1b50u  /* canvas_push's object (0x240) */
#define M2_TEXT 0x1d90u    /* 16 strings of 0x10 */
#define M2_RECT 0x1e90u    /* [X+4] */
#define M2_REF 0x1ea0u     /* [X+8] of the status page */
#define M2_PANEL 0x1eb0u   /* [0x49b088] (0x290) */
#define M2_F5 0x2140u      /* [panel+0x1f5] */
#define M2_SELF 0x2240u    /* [panel+0x203] */
#define M2_G 0x2280u       /* [0x6c5ed0] */
#define M2_WOBJ 0x22c0u    /* [O+0x202]: +4 the weapons */
#define M2_WARR 0x22d0u    /* 12 weapon pointers */
#define M2_FILLREC 0x2300u /* readable fill records */
#define M2_E 0x2320u       /* the entry flags of 0x44703c/0x447070 */
#define M2_KINDS 49u

static BYTE *m2_world, *m2_start;
static es2_sim_entry m2_entries[4096];
static es2_sim_dispatch m2_dispatch;
static unsigned m2_kinds[M2_KINDS], m2_calls[2][M2_KINDS];
static uint32_t m2_ret[M2_KINDS][8], m2_clock[8];
static uint32_t m2_at(uint32_t offset) { return (uint32_t)(uintptr_t)m2_world + offset; }
static void m2_w32(uint32_t offset, uint32_t v) { put32(m2_start + offset, v); }
static void m2_w16(uint32_t offset, uint32_t v) { put16(m2_start + offset, (uint16_t)v); }
static void m2_w8(uint32_t offset, uint32_t v) { m2_start[offset] = (BYTE)v; }
static uint32_t m2_r32(uint32_t offset) { return get32(m2_start + offset); }
static uint32_t m2_data32(uint32_t va) { return get32(mapped + (va - 0x400000u)); }
/* A pointer argument: its value in the worlds and the image, else a mark
 * (a stack address, different on the two sides). */
static uint32_t m2_p(uint32_t v) {
    uint32_t w = (uint32_t)(uintptr_t)m2_world, d = (uint32_t)(uintptr_t)md_world;
    if ((v >= w && v - w < M2_SIZE) || (v >= d && v - d < MD_WORLD_SIZE) || (v >= 0x00400000u && v < 0x00800000u)) return v;
    return 0x57ac0000u;
}
static int m2_readable(uint32_t v) {
    uint32_t w = (uint32_t)(uintptr_t)m2_world, d = (uint32_t)(uintptr_t)md_world;
    return (v >= w && v - w + 8u <= M2_SIZE) || (v >= d && v - d + 8u <= MD_WORLD_SIZE);
}
static void m2_note(unsigned kind, const uint32_t *a, unsigned n) {
    uint32_t v[8] = {0x6d320000u | kind};
    for (unsigned i = 0; i < n && i < 7u; ++i) v[i + 1u] = a[i];
    md_note(0, v, n + 1u);
    if (md_side_now) ++m2_kinds[kind];
}
static void m2_rec8(uint32_t holder) {   /* the 8 bytes a fill-record pointer at `holder` points to */
    uint32_t r = get32((BYTE *)(uintptr_t)holder);
    if (m2_readable(r) || (r < 0x00400000u || r >= 0x00800000u)) md_bytes(r, 8u);
}
static uint32_t m2_record(unsigned kind, const uint32_t *a) {
    unsigned k = m2_calls[md_side_now][kind]++;
    uint32_t ret = m2_ret[kind][k & 7u];
    switch (kind) {
    case 1: m2_note(1, NULL, 0); return m2_clock[k & 7u];
    case 5: m2_note(5, NULL, 0); return m2_at(M2_CANVAS);
    case 6: case 13: m2_note(kind, NULL, 0); return ret;
    case 7: {   /* fill(rect): the rectangle and the fill records */
        uint32_t v[1] = {m2_p(a[0])};
        m2_note(7, v, 1);
        md_bytes(a[0], 16u);
        m2_rec8(md_gc() + 0x22cu);
        m2_rec8(m2_at(M2_CANVAS + 0x22cu));
        m2_rec8(m2_at(M2_VIEW + 0x22cu));
        return ret;
    }
    case 8: {   /* panel_copy(1, rect, 0) */
        uint32_t v[3] = {a[0], m2_p(a[1]), a[2]};
        m2_note(8, v, 3);
        md_bytes(a[1], 16u);
        return ret;
    }
    case 9: {   /* label(label, text) */
        uint32_t v[1] = {m2_p(a[0])};
        m2_note(9, v, 1);
        md_bytes(a[0], 4u);
        md_bytes(a[0] + 0x1du, 4u);
        md_string(a[1]);
        return ret;
    }
    case 14: {   /* 0x45caa4(desc, bitmap, point, angle, size, flags) */
        uint32_t v[6] = {a[0], a[1], m2_p(a[2]), a[3], m2_p(a[4]), a[5]};
        m2_note(14, v, 6);
        md_bytes(a[2], 8u);
        md_bytes(a[4], 8u);
        return ret;
    }
    case 16: m2_note(16, a, 2); return k < 8u ? ret : 0u;   /* the list step: the scripted sequence, then 0 */
    case 19: m2_note(19, NULL, 0); return m2_at(M2_JOLT);
    case 20: m2_note(20, a, 1); md_bytes(a[0] + 4u, 0x208u); return ret;   /* gc_select(V) */
    case 21: m2_note(21, a, 1); md_bytes(a[0], 0x16u); return ret;         /* view_setup(C) */
    case 22: case 23: case 24: {   /* 0x42841c(), 0x46cb9c(a, C, 0), 0x428140(C): with the DATA they read */
        uint32_t v[6] = {m2_data32(0x004d24eau), m2_data32(0x004aaaa8u), m2_data32(0x004aaaacu), 0, 0, 0};
        unsigned n = kind == 22 ? 0u : kind == 23 ? 3u : 1u;
        for (unsigned i = 0; i < n; ++i) v[3u + i] = a[i];
        m2_note(kind, v, 3u + n);
        return ret;
    }
    case 25:   /* 0x483a04(a, b): two stack points and the fill record [V+0x228] */
        m2_note(25, NULL, 0);
        md_bytes(a[0], 8u);
        md_bytes(a[1], 8u);
        m2_rec8(m2_at(M2_VIEW + 0x228u));
        return ret;
    case 26: m2_note(26, NULL, 0); md_bytes(a[0], 16u); m2_rec8(m2_at(M2_VIEW + 0x22cu)); return ret;   /* the ellipse */
    case 28: {   /* blit_at(desc, bitmap, point) */
        uint32_t v[2] = {a[0], a[1]};
        m2_note(28, v, 2);
        md_bytes(a[2], 8u);
        return ret;
    }
    case 29:   /* 0x41b4ec(O, buf): the scripted part values */
        m2_note(29, a, 1);
        memcpy((BYTE *)(uintptr_t)a[1], m2_world + M2_BUF, 0x54u);
        return ret;
    case 31: m2_note(31, a, 2); return m2_at(M2_TEXT + 0x10u * (ret & 15u));   /* 0x44b928: a text */
    case 32: {   /* strupr(s) */
        m2_note(32, NULL, 0);
        md_string(a[0]);
        for (BYTE *s = (BYTE *)(uintptr_t)a[0]; *s; ++s) if (*s >= 'a' && *s <= 'z') *s = (BYTE)(*s - 32);
        return a[0];
    }
    case 33: {   /* 0x438624(value, record, out): out[0], out[1] scripted */
        uint32_t v[2] = {a[0], m2_p(a[1])};
        m2_note(33, v, 2);
        put32((BYTE *)(uintptr_t)a[2], ret % 5u);
        put32((BYTE *)(uintptr_t)a[2] + 4u, ret >> 8);
        return random32();
    }
    case 34: {   /* the bar 0x437e94(rect, a, b, c) */
        uint32_t v[3] = {a[1], a[2], a[3]};
        m2_note(34, v, 3);
        md_bytes(a[0], 16u);
        return ret;
    }
    case 37: m2_note(37, NULL, 0); md_string(a[0]); md_string(a[1]); strcat((char *)(uintptr_t)a[0], (const char *)(uintptr_t)a[1]); return a[0];
    case 38: m2_note(38, NULL, 0); md_bytes(a[0], 12u); md_bytes(a[1], 12u); return ret;   /* 0x49288c */
    case 40: m2_note(40, NULL, 0); md_string(a[0]); return (uint32_t)strlen((const char *)(uintptr_t)a[0]);
    case 41: {   /* itoa(v, buf, radix) */
        uint32_t v[2] = {a[0], a[2]};
        m2_note(41, v, 2);
        snprintf((char *)(uintptr_t)a[1], 16, "%ld", (long)(int32_t)a[0]);
        return a[1];
    }
    case 45: m2_note(45, a, 1); return m2_at(M2_TEXT + 0x10u * (ret & 15u));   /* weapon_name: a text */
    case 46: {   /* memcmp(a, b, n) */
        m2_note(46, a + 2, 1);
        uint32_t n = a[2] < 0x40u ? a[2] : 0x40u;
        md_bytes(a[0], n);
        md_bytes(a[1], n);
        return ret;
    }
    case 47: {   /* 0x451de0(X, L, point) */
        m2_note(47, a, 2);
        md_bytes(a[2], 8u);
        return ret;
    }
    case 48: {   /* 0x438230(rect, bitmap, point, a, b, c) */
        uint32_t v[4] = {a[1], a[3], a[4], a[5]};
        m2_note(48, v, 4);
        md_bytes(a[0], 16u);
        md_bytes(a[2], 8u);
        return ret;
    }
    case 2: case 4: case 11: case 12: case 17: case 27: case 30: case 39: case 43:
        m2_note(kind, a, 1); return ret;
    case 35: m2_note(35, a, 3); return ret;
    case 36: m2_note(36, a, 4); return ret;   /* 0x43b288(t, i, flag, k): the method +0x40 leaves flag and k */
    default: m2_note(kind, a, 2); return ret;   /* 3 10 15 18 42 44 */
    }
}
#define M2_HOST0(k) static uint32_t __cdecl m2_o##k(void) { return m2_record(k, NULL); }
#define M2_HOST1(k) static uint32_t __cdecl m2_o##k(uint32_t a) { const uint32_t s[1] = {a}; return m2_record(k, s); }
#define M2_HOST2(k) static uint32_t __cdecl m2_o##k(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return m2_record(k, s); }
#define M2_HOST3(k) static uint32_t __cdecl m2_o##k(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return m2_record(k, s); }
#define M2_HOST4(k) static uint32_t __cdecl m2_o##k(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { \
    const uint32_t s[4] = {a, b, c, d}; return m2_record(k, s); }
#define M2_HOST6(k) static uint32_t __cdecl m2_o##k(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { \
    const uint32_t s[6] = {a, b, c, d, e, f}; return m2_record(k, s); }
M2_HOST0(1)
M2_HOST1(2)
M2_HOST2(3)
M2_HOST1(4)
M2_HOST0(5)
M2_HOST0(6)
M2_HOST1(7)
M2_HOST3(8)
M2_HOST2(9)
M2_HOST2(10)
M2_HOST1(11)
M2_HOST1(12)
M2_HOST0(13)
M2_HOST6(14)
M2_HOST2(15)
M2_HOST2(16)
M2_HOST1(17)
M2_HOST2(18)
M2_HOST0(19)
M2_HOST1(20)
M2_HOST1(21)
M2_HOST0(22)
M2_HOST3(23)
M2_HOST1(24)
M2_HOST2(25)
M2_HOST1(26)
M2_HOST1(27)
M2_HOST3(28)
M2_HOST2(29)
M2_HOST1(30)
M2_HOST2(31)
M2_HOST1(32)
M2_HOST3(33)
M2_HOST4(34)
M2_HOST3(35)
M2_HOST4(36)
M2_HOST2(37)
M2_HOST2(38)
M2_HOST1(39)
M2_HOST1(40)
M2_HOST3(41)
M2_HOST2(42)
M2_HOST1(43)
M2_HOST2(44)
M2_HOST1(45)
M2_HOST3(46)
M2_HOST3(47)
M2_HOST6(48)
static void m2_s_stub(es2_sim_service_call *sc, void *user) { sc->eax = m2_record((unsigned)(uintptr_t)user, sc->slots); }
static const struct { uint32_t va; void *stub; unsigned kind, slots; } m2_stubs[] = {
    {0x00467828u, (void *)m2_o1, 1, 0}, {0x00492ee0u, (void *)m2_o2, 2, 1}, {0x00492f24u, (void *)m2_o3, 3, 2},
    {0x00431410u, (void *)m2_o4, 4, 1}, {0x004311e0u, (void *)m2_o5, 5, 0}, {0x00431210u, (void *)m2_o6, 6, 0},
    {0x00486704u, (void *)m2_o7, 7, 1}, {0x0043122cu, (void *)m2_o8, 8, 3}, {0x00438920u, (void *)m2_o9, 9, 2},
    {0x0044ae1cu, (void *)m2_o10, 10, 2}, {0x0041bac8u, (void *)m2_o11, 11, 1}, {0x0044b864u, (void *)m2_o12, 12, 1},
    {0x0044b920u, (void *)m2_o13, 13, 0}, {0x0045caa4u, (void *)m2_o14, 14, 6}, {0x004470a4u, (void *)m2_o15, 15, 2},
    {0x00471c70u, (void *)m2_o16, 16, 2}, {0x0047dff0u, (void *)m2_o17, 17, 1}, {0x0047e0c0u, (void *)m2_o18, 18, 2},
    {0x0042d870u, (void *)m2_o19, 19, 0}, {0x00480d44u, (void *)m2_o20, 20, 1}, {0x0048c2e4u, (void *)m2_o21, 21, 1},
    {0x0042841cu, (void *)m2_o22, 22, 0}, {0x0046cb9cu, (void *)m2_o23, 23, 3}, {0x00428140u, (void *)m2_o24, 24, 1},
    {0x00483a04u, (void *)m2_o25, 25, 2}, {0x0048817cu, (void *)m2_o26, 26, 1}, {0x00440370u, (void *)m2_o27, 27, 1},
    {0x0045c8bcu, (void *)m2_o28, 28, 3}, {0x0041b4ecu, (void *)m2_o29, 29, 2}, {0x00433134u, (void *)m2_o30, 30, 1},
    {0x0044b928u, (void *)m2_o31, 31, 2}, {0x00496754u, (void *)m2_o32, 32, 1}, {0x00438624u, (void *)m2_o33, 33, 3},
    {0x00437e94u, (void *)m2_o34, 34, 4}, {0x0043b288u, (void *)m2_o36, 36, 4}, {0x004967eau, (void *)m2_o37, 37, 2},
    {0x0049288cu, (void *)m2_o38, 38, 2}, {0x00434228u, (void *)m2_o39, 39, 1}, {0x00496766u, (void *)m2_o40, 40, 1},
    {0x00496820u, (void *)m2_o41, 41, 3}, {0x00450b88u, (void *)m2_o42, 42, 2}, {0x0044ba54u, (void *)m2_o44, 44, 2},
    {0x0040e18cu, (void *)m2_o45, 45, 1}, {0x004967bau, (void *)m2_o46, 46, 3}, {0x00451de0u, (void *)m2_o47, 47, 3},
    {0x00438230u, (void *)m2_o48, 48, 6},
};
#define M2_STUBS (sizeof(m2_stubs) / sizeof(m2_stubs[0]))
static BYTE m2_saved[M2_STUBS][5];
static int m2_put(uint32_t va, const char *name, es2_sim_service_fn fn, void *user, unsigned slots) {
    for (unsigned i = 0; i < m2_dispatch.count; ++i) {
        es2_sim_entry *e = &m2_dispatch.entries[i];
        if (e->va != va) continue;
        e->kind = ES2_SIM_SERVICE; e->name = name; e->service = fn; e->service_user = user; e->pop_bytes = 0; e->stack_slots = (uint8_t)slots;
        return 0;
    }
    return es2_sim_dispatch_add_service(&m2_dispatch, va, name, fn, user, 0, (uint8_t)slots);
}
static int m2_dispatch_setup(void) {
    es2_sim_dispatch_init(&m2_dispatch, m2_entries, 4096, md_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&m2_dispatch, &s)) return fail("mfd rest2: dispatch");
    for (unsigned i = 0; i < M2_STUBS; ++i)
        if (m2_put(m2_stubs[i].va, "mfd rest2 recorder", m2_s_stub, (void *)(uintptr_t)m2_stubs[i].kind, m2_stubs[i].slots))
            return fail("mfd rest2: dispatch");
    if (m2_put((uint32_t)(uintptr_t)m2_o35, "mfd rest2 object slot", m2_s_stub, (void *)(uintptr_t)35, 3) ||
        m2_put((uint32_t)(uintptr_t)m2_o43, "mfd rest2 driver slot", m2_s_stub, (void *)(uintptr_t)43, 1))
        return fail("mfd rest2: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&m2_dispatch, &error, &bad)) return fail("mfd rest2: dispatch seal");
    return 0;
}

/* ---- the case script */
typedef struct m2_preset { uint32_t va, value; unsigned width; } m2_preset;
static m2_preset m2_presets[512];
static unsigned m2_preset_count;
static void m2_set32(uint32_t va, uint32_t v) { m2_presets[m2_preset_count++] = (m2_preset){va, v, 4}; }
static void m2_set16(uint32_t va, uint32_t v) { m2_presets[m2_preset_count++] = (m2_preset){va, v, 2}; }
static void m2_set8(uint32_t va, uint32_t v) { m2_presets[m2_preset_count++] = (m2_preset){va, v, 1}; }
typedef struct m2_op { uint32_t va, a[4], in[3]; unsigned n, eax; } m2_op;
static m2_op m2_ops[2];
static unsigned m2_op_count;
static uint32_t m2_out[2][2];
static void m2_op_add(uint32_t va, unsigned eax, unsigned n, const uint32_t *a) {
    m2_op *o = &m2_ops[m2_op_count++];
    memset(o, 0, sizeof(*o));
    o->va = va; o->eax = eax; o->n = n;
    for (unsigned i = 0; i < 3u; ++i) o->in[i] = md_rand(2u) ? random32() : 0u;
    for (unsigned i = 0; i < n; ++i) o->a[i] = a[i];
}
#define M2_OP(va, eax, ...) m2_op_add((va), (eax), (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (const uint32_t[]){__VA_ARGS__})
static uint32_t m2_text(unsigned i) { return m2_at(M2_TEXT + 0x10u * (i & 15u)); }
static uint32_t m2_label(unsigned i) { return m2_at(M2_LBL + 0x24u * (i & 31u)); }
static uint32_t m2_flag(void) { return md_rand(2u) ? 0u : md_rand(2u) ? 1u : 1u + md_rand(255u); }
static void m2_clocks(const uint32_t *bounds, unsigned n) { for (unsigned i = 0; i < 8u; ++i) m2_clock[i] = md_near(bounds[md_rand(n)]); }
static void m2_common(void) {
    memset(md_world_start, 0, MD_WORLD_SIZE);
    memset(m2_start, 0, M2_SIZE);
    md_preset_count = 0;
    m2_preset_count = 0;
    m2_op_count = 0;
    for (unsigned k = 0; k < M2_KINDS; ++k)
        for (unsigned i = 0; i < 8u; ++i) m2_ret[k][i] = random32();
    for (unsigned i = 0; i < 8u; ++i) m2_clock[i] = random32();
    random_bytes(md_world_start + MD_GC, 0x240);
    md_w32(MD_GC + 0x22cu, m2_at(M2_FILLREC));
    random_bytes(m2_start, M2_SIZE);
    for (unsigned i = 0; i < 16u; ++i) {
        static const char *const words[] = {"MFD", "", "STATUS", "Orders", "TGT", "a", "RADAR-2", "dmg"};
        memset(m2_start + M2_TEXT + 0x10u * i, 0, 0x10);
        strcpy((char *)m2_start + M2_TEXT + 0x10u * i, words[md_rand(8u)]);
    }
    m2_w32(M2_CANVAS + 0x22cu, m2_at(M2_FILLREC + 8u));
    m2_w32(M2_VIEW + 0x22cu, m2_at(M2_FILLREC + 0x10u));
    m2_w32(M2_VIEW + 0x228u, m2_at(M2_FILLREC + 0x18u));
    /* bitmaps and the bitmap table */
    for (unsigned i = 0; i < 8u; ++i) {
        m2_w16(M2_BMP + 0x10u * i + 4u, md_rand(4u) ? md_rand(64u) : random32());
        m2_w16(M2_BMP + 0x10u * i + 6u, md_rand(4u) ? md_rand(64u) : random32());
    }
    m2_w32(M2_BTBL + 8u, m2_at(M2_BPTRS));
    for (unsigned i = 0; i < 32u; ++i) m2_w32(M2_BPTRS + 4u * i, m2_at(M2_BMP + 0x10u * md_rand(8u)));
    /* the panel */
    m2_w32(M2_PANEL + 0x203u, md_rand(4u) ? m2_at(M2_SELF) : m2_at(M2_O));
    m2_w32(M2_PANEL + 0x1f5u, m2_at(M2_F5));
    m2_w8(M2_F5 + 0xd6u, m2_flag());
    /* DATA every part shares */
    m2_set32(0x006c5f70u, md_at(MD_GC));
    m2_set32(0x0049b088u, m2_at(M2_PANEL));
    m2_set32(0x006cbee4u, md_at(MD_CNT));
    m2_set16(0x004d253au, md_rand(4u) ? md_rand(3u) : random32());
    m2_set16(0x004d253cu, md_rand(4u) ? md_rand(3u) : random32());
    m2_set32(0x004d2556u, md_rand(2u) ? 0u : random32());
    m2_set32(0x004d255au, md_rand(2u) ? 0u : random32());
    static const uint32_t colours[] = {0x004d3b82u, 0x004d3b84u, 0x004d3b88u, 0x004d3b8eu, 0x004d3b94u, 0x004d3b9cu, 0x004d3ba2u};
    for (unsigned i = 0; i < 7u; ++i) m2_set16(colours[i], md_rand(3u) ? md_rand(4u) : random32());
    for (unsigned i = 0; i < 10u; ++i) m2_set32(0x0049b0acu + 4u * i, random32());
}
static void m2_side(unsigned side) {
    md_side(side);
    lc_loop.dispatch = &m2_dispatch;
    memcpy(m2_world, m2_start, M2_SIZE);
    for (unsigned i = 0; i < m2_preset_count; ++i) {
        BYTE *p = mapped + (m2_presets[i].va - 0x400000u);
        if (m2_presets[i].width == 4) put32(p, m2_presets[i].value);
        else if (m2_presets[i].width == 2) put16(p, (uint16_t)m2_presets[i].value);
        else *p = (BYTE)m2_presets[i].value;
    }
    memset(m2_calls[side], 0, sizeof(m2_calls[side]));
}
static int m2_run(const char *what, unsigned k) {
    for (unsigned side = 0; side < 2u; ++side) {
        m2_side(side);
        for (unsigned i = 0; i < m2_op_count; ++i) {
            m2_op *o = &m2_ops[i];
            uint32_t out[3] = {0, 0, 0};
            static char where[200];
            snprintf(where, sizeof(where), "mfd rest2 (%s side %u op %u: 0x%08lx(0x%lx, 0x%lx))", what, side, i, (unsigned long)o->va,
                     (unsigned long)o->a[0], (unsigned long)o->a[1]);
            current_function = where;
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, o->in, out);
            else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&m2_dispatch, o->va, o->a, o->n, o->in, &r)) md_bad = 1;
                out[0] = r.eax;
            }
            m2_out[side][i] = out[0];
            if (md_bad) { fprintf(stderr, "FAIL: mfd rest2 %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va); return 1; }
        }
        put32(md_world + MD_GC + 0x22cu, 0);   /* stack addresses on both sides (the fill records) */
        put32(m2_world + M2_CANVAS + 0x22cu, 0);
        put32(m2_world + M2_VIEW + 0x22cu, 0);
        put32(m2_world + M2_VIEW + 0x228u, 0);
        if (!side) {
            memcpy(md_world_start + MD_WORLD_SIZE, md_world, MD_WORLD_SIZE);
            memcpy(m2_start + M2_SIZE, m2_world, M2_SIZE);
        }
        su_take(side);
    }
    if (md_hash[0] != md_hash[1] || md_count[0] != md_count[1]) {
        fprintf(stderr, "FAIL: mfd rest2 %s case %u: recorded calls differ (%u / %u)\n  original:%s\n  own:     %s\n", what, k, md_count[0],
                md_count[1], md_text[0], md_text[1]);
        for (unsigned i = 0; i < md_count[0] && i < 64u; ++i)
            if (md_each[0][i] != md_each[1][i]) { fprintf(stderr, "  first differing call %u\n", i); break; }
        return 1;
    }
    if (su_compare(what, k)) return 1;
    for (unsigned w = 0; w < 2u; ++w) {
        BYTE *now = w ? m2_world : md_world, *was = w ? m2_start + M2_SIZE : md_world_start + MD_WORLD_SIZE;
        uint32_t size = w ? M2_SIZE : MD_WORLD_SIZE;
        if (!memcmp(now, was, size)) continue;
        for (unsigned i = 0, shown = 0; i < size && shown < 16u; ++i)
            if (now[i] != was[i]) {
                fprintf(stderr, "  %s +0x%04x original %02x own %02x\n", w ? "m2 world" : "md world", i, was[i], now[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: mfd rest2 %s case %u: the world differs\n", what, k);
        return 1;
    }
    for (unsigned i = 0; i < m2_op_count; ++i)
        if (m2_ops[i].eax && m2_out[0][i] != m2_out[1][i]) {
            fprintf(stderr, "FAIL: mfd rest2 %s case %u op %u (0x%08lx): EAX original 0x%08lx own 0x%08lx\n", what, k, i,
                    (unsigned long)m2_ops[i].va, (unsigned long)m2_out[0][i], (unsigned long)m2_out[1][i]);
            return 1;
        }
    return 0;
}

/* ---- the entry steps and the order key: G = M2_X, its flags E = M2_E */
static unsigned m2_cov_entry[6];
static int m2_entry_case(unsigned k) {
    m2_common();
    uint32_t g = M2_X;
    m2_w32(g + 0xd1u, m2_at(M2_E));
    for (unsigned i = 0; i < 0x30u; ++i) m2_w8(M2_E + 0x2cu + i, md_rand(3u) ? random32() | 1u : random32() & 0xfeu);
    m2_w8(M2_E + 0x2cu + md_rand(6u), random32() & 0xfeu);   /* one entry free: the original loops otherwise */
    m2_w8(M2_E + 0x2cu + 14u, random32() & 0xfeu);            /* the up step from 6/7 ends there */
    m2_w32(g + 0xb1u, md_rand(8u) ? md_rand(6u) : 6u + md_rand(2u));
    unsigned which = md_rand(3u);
    if (which == 0) M2_OP(0x0044703cu, 1, m2_at(g));
    else if (which == 1) M2_OP(0x00447070u, 1, m2_at(g));
    else {
        uint32_t o = random32();
        m2_w32(g + 0xfbu, md_rand(2u) ? o : random32());
        m2_w32(g + 0xc1u, md_rand(2u) ? 1u : md_rand(6u));
        M2_OP(0x00447158u, 1, m2_at(g), o, random32());
    }
    unsigned before = m2_kinds[15];
    int bad = m2_run("entries", k);
    if (!bad) {
        ++m2_cov_entry[which];
        if (which == 2 && m2_kinds[15] != before) ++m2_cov_entry[3];
        if (which < 2 && m2_out[0][0] != m2_r32(g + 0xb1u)) ++m2_cov_entry[4];
        if (which < 2 && md_r32(0) == 0) ++m2_cov_entry[5];
    }
    return bad;
}

/* ---- the radio records: H = M2_H, V = M2_V.. */
static void m2_radio_world(void) {
    uint32_t h = M2_H;
    m2_w32(h + 0x766u, m2_text(md_rand(16u)));   /* the speaker's name */
    for (unsigned r = 0; r < 3u; ++r) {
        uint32_t s = h + 0x12du + 0x14eu * r, v = M2_V + 0xa0u * r;
        for (unsigned i = 0; i < 5u; ++i) m2_w32(s + 4u * i, m2_label(5u * r + i));
        m2_w32(s + 0x18u, m2_at(M2_BTBL));
        m2_w8(s + 0x12eu, md_rand(4u) ? md_rand(5u) : md_rand(32u));
        m2_w8(s + 0x133u, m2_flag());
        m2_w32(s + 0x137u, md_rand(3u) ? m2_r32(h + 0x766u) : m2_text(md_rand(16u)));
        m2_w8(s + 0x148u, md_rand(4u) ? md_rand(2u) : random32());
        m2_w8(s + 0x149u, md_rand(2u) ? m2_start[s + 0x148u] : random32());
        m2_w8(v + 0x99u, m2_flag());
        m2_set32(0x004d03c8u + 4u * r, m2_at(v));
    }
    m2_set32(0x0049d234u, m2_at(M2_BTBL));
    for (unsigned e = 0; e < 0x1cu; ++e) m2_set16(0x0049d1f6u + 2u * e, md_rand(32u));
    m2_set32(0x0049d240u, random32());
    m2_set32(0x0049d244u, random32());
    for (unsigned i = 0; i < 4u; ++i) {
        m2_set32(0x004d1614u + 4u * i, m2_text(md_rand(16u)));
        m2_set32(0x004d16b0u + 4u * i, m2_text(md_rand(16u)));
    }
    m2_set32(0x004d1648u, m2_text(md_rand(16u)));
    m2_set32(0x004d164cu, m2_text(md_rand(16u)));
    for (unsigned i = 0; i < 8u; ++i) {
        m2_ret[4][i] = md_rand(4u) ? random32() | 1u : random32() & 0xffffff00u;   /* visible: AL */
        m2_ret[10][i] = md_rand(4u);                                              /* the class */
        m2_ret[11][i] = (random32() & 0xffff0000u) | md_rand(4u);                 /* the order symbol */
        m2_ret[3][i] = md_rand(4u) ? md_rand(20u) : random32();                   /* rng_bounded */
    }
}
static unsigned m2_cov_radio[10];
static int m2_radio_case(unsigned k) {
    m2_common();
    m2_radio_world();
    uint32_t h = M2_H, r = md_rand(3u), s = h + 0x12du + 0x14eu * r;
    uint32_t bounds[2] = {m2_r32(s + 0x143u), random32()};
    m2_clocks(bounds, 1);
    unsigned which = md_rand(4u);
    if (which == 0) M2_OP(0x0044aea0u, 0, m2_at(h), r);
    else if (which == 1) M2_OP(0x0044b3dcu, 0, m2_at(h), m2_at(s));
    else if (which == 2) {
        uint32_t e = md_rand(4u) ? md_rand(0x1cu) : 0x1bu;
        if (md_rand(4u) == 0) e |= random32() & 0xffff0000u;
        if (m2_start[s + 0x133u] && (e & 0xffffu) >= 32u) e &= 0xffff001fu;
        M2_OP(0x0044b148u, 0, m2_at(h), m2_at(s), e);
    } else {
        if (m2_start[s + 0x133u]) m2_w8(s + 0x133u, md_rand(2u) ? 0u : 1u);   /* 0x1b < 32 either way */
        M2_OP(0x0044b344u, 0, m2_at(h), m2_at(s));
    }
    unsigned before[M2_KINDS];
    memcpy(before, m2_kinds, sizeof(before));
    int bad = m2_run("radio", k);
    if (!bad) {
        ++m2_cov_radio[which];
        if (m2_kinds[14] != before[14]) ++m2_cov_radio[4];   /* the rotated blit */
        if (m2_kinds[7] != before[7]) ++m2_cov_radio[5];     /* the fill */
        if (m2_kinds[2] != before[2]) ++m2_cov_radio[6];     /* rng_next */
        if (m2_kinds[3] != before[3]) ++m2_cov_radio[7];     /* rng_bounded */
        if (get32(m2_world + h + 0x782u) == 1u && m2_r32(h + 0x782u) != 1u) ++m2_cov_radio[8];   /* the speaker */
        if (which == 0 && m2_kinds[12] != before[12]) ++m2_cov_radio[9];   /* the draw's frame path */
    }
    return bad;
}

/* ---- the target draw: X = M2_X, V = M2_VIEW, C = M2_CAM, t = M2_TGT */
static unsigned m2_cov_target[8];
static int m2_target_case(unsigned k) {
    m2_common();
    uint32_t x = M2_X;
    m2_w32(x + 0x10u, m2_at(M2_VIEW));
    m2_w32(x + 0x14u, m2_at(M2_CAM));
    m2_w32(x + 0x33u, m2_label(0));
    m2_w32(x + 0x37u, m2_label(1));
    m2_w32(x + 0x3fu, m2_label(2));
    uint8_t blink = (uint8_t)(md_rand(2u) ? 0u : 1u + md_rand(3u));
    m2_set8(0x0049c398u, blink);
    m2_w8(x + 0x45u, md_rand(2u) ? blink : md_rand(4u));
    m2_w8(x + 0x20u, md_rand(3u));
    uint32_t t46 = random32() >> 1;
    m2_w32(x + 0x46u, t46);
    uint32_t bounds[2] = {t46, random32() >> 1};
    m2_clocks(bounds, 2);
    for (unsigned i = 0; i < 8u; ++i) if (md_rand(4u) == 0) m2_clock[i] = t46 - 0x1eu + md_signed(2u);
    uint32_t target = md_rand(4u) ? m2_at(M2_TGT) : 0u;
    m2_set32(0x0049c394u, target);
    unsigned steps = md_rand(7u);
    for (unsigned i = 0; i < 8u; ++i) m2_ret[16][i] = i < steps ? random32() | 1u : i == steps && md_rand(4u) ? target : 0u;
    m2_set32(0x004a96c2u, random32());
    m2_set32(0x004a0be0u, random32());
    m2_set32(0x004aaaa8u, random32());
    m2_set32(0x004aaaacu, random32());
    m2_set32(0x004d24eau, random32());
    m2_set32(0x004d1a4cu, random32());
    for (unsigned i = 0; i < 4u; ++i) m2_set32(0x004d1a44u + 4u * i, random32());
    for (unsigned i = 0; i < 4u; ++i) m2_set32(0x004d1668u + 4u * i, m2_text(md_rand(16u)));
    m2_set32(0x0049b0b8u, random32());
    m2_w32(M2_VIEW + 0x220u, md_signed(0x400u));
    m2_w32(M2_VIEW + 0x224u, md_rand(8u) ? md_signed(0x400u) : random32());
    for (unsigned i = 0; i < 8u; ++i) m2_ret[27][i] = md_rand(2u) ? 0u : random32();
    m2_ret[27][0] = md_rand(2u) ? 0u : random32() | 1u;
    M2_OP(0x0043fe44u, 0, m2_at(x));
    unsigned before[M2_KINDS];
    memcpy(before, m2_kinds, sizeof(before));
    int bad = m2_run("target", k);
    if (!bad) {
        if (blink && m2_kinds[1] != before[1] && !m2_kinds[16] == !before[16] && m2_kinds[27] == before[27]) ++m2_cov_target[0];   /* blink fill only */
        if (m2_kinds[16] != before[16]) ++m2_cov_target[1];   /* the list walk */
        if (m2_kinds[26] != before[26]) ++m2_cov_target[2];   /* the view drawn */
        if (target && m2_kinds[16] != before[16] && m2_kinds[26] == before[26]) ++m2_cov_target[3];   /* not found */
        if (m2_kinds[27] != before[27]) ++m2_cov_target[4];   /* the tail */
        if (m2_kinds[27] != before[27] && m2_ret[27][0]) ++m2_cov_target[5];
        if (m2_world[M2_F5 + 0xd6u] && m2_kinds[27] != before[27]) ++m2_cov_target[6];
        if (blink && !mapped[0x0049c398u - 0x400000u]) ++m2_cov_target[7];   /* the blink ended */
    }
    return bad;
}

/* ---- the status draw: X = M2_X, O = M2_O */
static const uint8_t m2_types[2][9] = {{0, 4, 5, 6, 7, 8, 13, 14, 0}, {0, 4, 5, 6, 7, 8, 0, 4, 5}};
static void m2_object(void) {
    uint32_t o = M2_O;
    m2_w32(o, m2_at(M2_OVT));
    m2_w32(M2_OVT + 0x40u, (uint32_t)(uintptr_t)m2_o35);
    m2_w32(o + 0x45u, m2_at(M2_OX));
    m2_w8(M2_OX + 0x12u, md_rand(3u) ? md_rand(2u) : random32());
    m2_w32(o + 0x1f2u, m2_at(M2_D2));
    m2_w32(o + 0x1fau, m2_text(md_rand(16u)) - 0x12u);
    m2_w32(o + 0x1feu, m2_at(M2_L));
    m2_w32(o + 0x202u, m2_at(M2_WOBJ));
    m2_w32(M2_WOBJ + 4u, m2_at(M2_WARR));
    for (unsigned i = 0; i < 12u; ++i) m2_w32(M2_WARR + 4u * i, md_rand(3u) ? md_at(MD_TEXT + 4u * i) : 0u);
    m2_w8(M2_D2 + 0x50u, m2_flag());
    m2_w16(M2_D2 + 0x28u, md_rand(8u));
    m2_w16(M2_D2 + 0x32u, md_rand(2u) ? 0u : random32());
    memset(m2_start + M2_D2 + 0x58u, 0, 0x10);
    strcpy((char *)m2_start + M2_D2 + 0x58u, md_rand(2u) ? "atlas" : "Mk-2b");
    m2_w32(M2_D2 + 0xdau, m2_at(M2_D));
    for (unsigned t = 0; t < 3u; ++t) {
        uint32_t p = M2_D + 0x1cu * t;
        m2_w32(p, m2_at(M2_BMP + 0x10u * md_rand(8u)));
        m2_w32(p + 0x14u, 1u + md_rand(10u));
        m2_w32(p + 0x18u, m2_at(t == 1u ? M2_PARTS1 : M2_PARTS));
    }
    /* the part values: words next to the thresholds */
    for (unsigned i = 0; i < 0x2au; ++i) {
        static const int16_t values[] = {0, 1, -1, 0x80, 0x81, 0x100, 0x7f, 50, 200};
        m2_w16(M2_BUF + 2u * i, md_rand(4u) ? (uint16_t)values[md_rand(9u)] : (uint16_t)random32());
    }
}
static unsigned m2_cov_status[12];
static int m2_status_case(unsigned k) {
    m2_common();
    m2_object();
    uint32_t x = M2_X, o = M2_O;
    for (unsigned i = 0; i < 5u; ++i) m2_w32(x + 0x10u + 4u * i, m2_label(i));
    m2_w32(x + 8u, m2_at(M2_REF));
    m2_w32(M2_REF + 8u, md_rand(4u) ? m2_at(o) : 0u);
    m2_w32(x + 0x3eu, md_rand(2u) ? m2_at(o) : 0u);
    m2_w8(x + 0x34u, md_rand(3u) == 0);
    m2_w8(x + 0x39u, m2_flag());
    m2_w8(o + 0xa4u, md_rand(3u) ? 0u : random32());
    m2_w8(o + 0x99u, md_rand(3u) ? 0u : random32());
    m2_w8(o + 0x92u + 7u, md_rand(4u) ? 0u : random32());
    m2_w8(o + 0x92u + 0x1eu, m2_flag());
    uint32_t t = random32() >> 1;
    m2_w32(x + 0x3au, t);
    uint32_t bounds[1] = {t};
    m2_clocks(bounds, 1);
    static const uint16_t kinds[] = {0, 0, 0, 1, 2, 3, 4, 0xffff};
    m2_w16(o + 0x1a8u, kinds[md_rand(8u)]);
    /* the parts of the status table D+0x38, valid for the damage flag */
    int flag = m2_start[M2_D2 + 0x50u] != 0;
    uint32_t p = M2_D + 0x38u;
    m2_w32(p + 4u, md_signed(40u));
    m2_w32(p + 8u, md_signed(40u));
    for (unsigned i = 0; i < 12u; ++i) {
        uint32_t rec = M2_PARTS + 0x1cu * i;
        m2_w32(rec, m2_types[flag][md_rand(flag ? 9u : 8u)]);
    }
    m2_w32(M2_PANEL + 0x1f9u, random32());
    m2_w16(M2_PANEL + 0x26cu + 0x10u, md_rand(2u) ? 0u : random32());
    m2_w16(M2_PANEL + 0x26cu + 0x12u, md_rand(4u) ? md_rand(20u) : random32());
    for (unsigned i = 0; i < 8u; ++i) {
        m2_ret[30][i] = md_rand(2u) ? 0xffffffffu : md_rand(4u);
        m2_ret[33][i] = random32();
    }
    m2_set32(0x0049bda4u, m2_at(M2_BTBL));
    m2_set32(0x0049bda8u, m2_at(M2_BTBL));
    m2_set32(0x0049bdacu, m2_at(M2_BTBL));
    m2_set8(0x004d2537u, md_rand(2u) ? 3u : md_rand(5u));
    m2_set8(0x004d256cu, md_rand(2u) ? 0u : 1u);
    static const uint32_t texts[] = {0x004d14dcu, 0x004d14ecu, 0x004d14f0u, 0x004d14f4u, 0x004d14f8u, 0x004d160cu, 0x004d1610u};
    for (unsigned i = 0; i < 7u; ++i) m2_set32(texts[i], m2_text(md_rand(16u)));
    for (unsigned i = 0; i < 8u; ++i) {
        m2_set32(0x004d15f0u + 4u * i, m2_text(md_rand(16u)));
        m2_set32(0x004d1550u + 4u * i, m2_text(md_rand(16u)));
    }
    M2_OP(0x0043a5a0u, 0, m2_at(x));
    unsigned before[M2_KINDS];
    memcpy(before, m2_kinds, sizeof(before));
    int bad = m2_run("status", k);
    if (!bad) {
        uint32_t now = get32(m2_world + x + 0x3eu);
        uint16_t kind = now ? get16(m2_world + o + 0x1a8u) : 0xfffeu;
        ++m2_cov_status[kind == 0xfffeu ? 0 : kind == 0u ? 1 : kind == 1u || kind == 3u ? 2 : kind == 2u ? 3 : 4];
        if (m2_kinds[34] != before[34]) ++m2_cov_status[5];             /* part bars */
        if (m2_kinds[7] - before[7] > 1u) ++m2_cov_status[6];           /* the highlight */
        if (m2_kinds[41] != before[41]) ++m2_cov_status[7];             /* the range */
        if (m2_kinds[37] != before[37]) ++m2_cov_status[8];             /* the range, hidden */
        if (m2_kinds[31] != before[31]) ++m2_cov_status[9];             /* a wingman name */
        if (m2_kinds[32] != before[32]) ++m2_cov_status[10];            /* strupr */
        if (m2_kinds[1] != before[1]) ++m2_cov_status[11];              /* the clock */
    }
    return bad;
}

/* ---- the HDD status body: X = M2_X (the page), H = M2_H, O = M2_O */
static unsigned m2_cov_hdd[12];
static int m2_hdd_case(unsigned k) {
    m2_common();
    m2_object();
    uint32_t x = M2_X, h = M2_H, o = M2_O;
    m2_w32(x + 4u, m2_at(M2_RECT));
    m2_w32(x + 8u, m2_at(h));
    for (unsigned j = 0; j < 26u; ++j) m2_w32(x + 0x10u + 4u * j, m2_label(j));
    m2_w32(x + 0xf0u, m2_label(26));
    m2_w8(h + 0x76u, md_rand(8u) ? 0u : 1u + md_rand(255u));
    m2_w32(h + 0x524u, md_rand(6u) ? m2_at(o) : 0u);
    m2_w32(h + 0x55eu, m2_text(md_rand(16u)));
    uint32_t mode = md_rand(3u);
    m2_w32(x + 0x7cu, mode);
    m2_w32(x + 0x80u, md_rand(3u) ? mode : md_rand(3u));
    m2_w32(x + 0xe8u, md_rand(3u) ? m2_r32(h + 0x524u) : md_rand(2u) ? 0u : m2_at(o));
    uint32_t scroll = md_rand(4u);
    m2_w32(x + 0x84u, scroll);
    m2_w32(x + 0x88u, md_rand(3u) ? scroll : md_rand(4u));
    m2_w32(x + 0x90u, md_rand(11u));
    /* the part records: types inside the label tables */
    for (unsigned t = 0; t < 2u; ++t) {
        uint32_t p = M2_D + 0x1cu * t;
        m2_w32(p + 4u, md_signed(40u));
        m2_w32(p + 8u, md_signed(40u));
        if (mode == 0u) m2_w32(p + 0x14u, 1u + md_rand(12u));
        for (unsigned i = 0; i < 12u; ++i) {
            uint32_t rec = (t ? M2_PARTS1 : M2_PARTS) + 0x1cu * i;
            m2_w32(rec, md_rand(t ? 12u : 19u));
            m2_w32(rec + 0x14u, md_rand(2u) ? random32() : 0u);
        }
    }
    m2_w32(M2_L, md_rand(11u));
    m2_w32(M2_L + 4u, m2_at(M2_LE));
    for (unsigned i = 0; i < 12u; ++i) m2_w16(M2_LE + 0x22u * i + 0x20u, md_rand(10u));
    /* the old values: the scripted block with a few words changed */
    memcpy(m2_start + x + 0x94u, m2_start + M2_BUF, 0x54u);
    for (unsigned n = md_rand(4u); n; --n) m2_w16(x + 0x94u + 2u * md_rand(0x2au), random32());
    for (unsigned i = 0; i < 8u; ++i) {
        static const uint32_t states[] = {0, 1, 2, 3, 4, 0, 0, 2};
        m2_ret[33][i] = (random32() & 0xffffff00u) | states[md_rand(8u)];   /* out[0] = ret % 5 */
        m2_ret[33][i] -= m2_ret[33][i] % 5u;
        m2_ret[33][i] += states[md_rand(8u)];
        m2_ret[46][i] = md_rand(2u) ? random32() & 0xffffff00u : random32() | 1u;
    }
    m2_set32(0x006c5ed0u, md_rand(4u) ? m2_at(M2_G) : m2_at(M2_G + 0x20u));
    m2_set32(0x004a57ecu, (uint32_t)(uintptr_t)m2_o43);
    m2_set32(0x004a3686u, random32());
    m2_set8(0x0049da9cu, md_rand(2u));
    m2_set32(0x004d1db0u, random32());
    for (unsigned i = 0; i < 0x14u; ++i) m2_set32(0x004d1d38u + 4u * i, 0);
    static const struct { uint32_t va; unsigned n; } tables[] = {{0x004d13e4u, 19}, {0x004d1430u, 19}, {0x004d147cu, 12}, {0x004d14acu, 12}, {0x004d1d10u, 10}};
    for (unsigned t = 0; t < 5u; ++t)
        for (unsigned i = 0; i < tables[t].n; ++i) m2_set32(tables[t].va + 4u * i, md_rand(4u) ? m2_text(md_rand(16u)) : 0u);
    M2_OP(0x00450c7cu, 0, m2_at(x), md_rand(2u) ? 0u : md_rand(2u) ? 1u : random32());
    unsigned before[M2_KINDS];
    memcpy(before, m2_kinds, sizeof(before));
    int bad = m2_run("hdd", k);
    if (!bad) {
        if (m2_start[h + 0x76u]) ++m2_cov_hdd[0];
        else if (!m2_r32(h + 0x524u)) ++m2_cov_hdd[1];
        else ++m2_cov_hdd[2 + mode];
        if (m2_kinds[47] != before[47]) ++m2_cov_hdd[5];   /* the redraw */
        if (m2_kinds[48] != before[48]) ++m2_cov_hdd[6];   /* part bitmaps */
        if (m2_kinds[41] != before[41]) ++m2_cov_hdd[7];   /* labels with a value */
        if (m2_kinds[46] != before[46]) ++m2_cov_hdd[8];   /* memcmp */
        if (m2_kinds[45] != before[45]) ++m2_cov_hdd[9];   /* weapon names */
        if (m2_kinds[43] != before[43]) ++m2_cov_hdd[10];  /* the driver slot */
        if (m2_kinds[8] != before[8]) ++m2_cov_hdd[11];    /* panel copies */
    }
    return bad;
}

static int mfd_rest2_cases(void) {
    current_function = "mfd rest2";
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
    m2_world = VirtualAlloc(NULL, M2_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    m2_start = malloc(2u * M2_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !md_world || !md_world_start || !m2_world ||
        !m2_start)
        return fail("mfd rest2: memory");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("mfd rest2: import slot not found");
        }
    if (m2_dispatch_setup()) return 1;
    for (unsigned i = 0; i < M2_STUBS; ++i) if (md_patch(m2_stubs[i].va, m2_stubs[i].stub, m2_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"entries", m2_entry_case, 1000}, {"radio", m2_radio_case, 2000}, {"target", m2_target_case, 1500},
        {"status", m2_status_case, 2000}, {"hdd", m2_hdd_case, 2500},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: mfd rest2 %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    for (unsigned i = 0; i < M2_STUBS; ++i) if (md_patch(m2_stubs[i].va, m2_stubs[i].stub, m2_saved[i], 0)) bad = 1;
    if (!bad) {
        printf("MFD rest2 coverage: entry up %u, down %u, order key %u (set %u), moved %u\n", m2_cov_entry[0], m2_cov_entry[1],
               m2_cov_entry[2], m2_cov_entry[3], m2_cov_entry[4]);
        printf("MFD rest2 coverage: radio draw %u, frame %u, line %u, talk %u; rotated blit %u, fill %u, rng_next %u, rng_bounded %u, "
               "speaker %u, draw->frame %u\n",
               m2_cov_radio[0], m2_cov_radio[1], m2_cov_radio[2], m2_cov_radio[3], m2_cov_radio[4], m2_cov_radio[5], m2_cov_radio[6],
               m2_cov_radio[7], m2_cov_radio[8], m2_cov_radio[9]);
        printf("MFD rest2 coverage: target blink-only %u, list walk %u, view drawn %u, not found %u, tail %u, locked %u, flashing %u, "
               "blink ended %u\n",
               m2_cov_target[0], m2_cov_target[1], m2_cov_target[2], m2_cov_target[3], m2_cov_target[4], m2_cov_target[5],
               m2_cov_target[6], m2_cov_target[7]);
        printf("MFD rest2 coverage: status none %u, mech %u, kind 1/3 %u, kind 2 %u, other %u; bars %u, highlight %u, range %u, "
               "hidden range %u, wingman %u, strupr %u, clock %u\n",
               m2_cov_status[0], m2_cov_status[1], m2_cov_status[2], m2_cov_status[3], m2_cov_status[4], m2_cov_status[5],
               m2_cov_status[6], m2_cov_status[7], m2_cov_status[8], m2_cov_status[9], m2_cov_status[10], m2_cov_status[11]);
        printf("MFD rest2 coverage: hdd minimised %u, no target %u, mode 0/1/2 %u/%u/%u; redraw %u, part bitmaps %u, value labels %u, "
               "memcmp %u, weapon names %u, driver %u, panel copies %u\n",
               m2_cov_hdd[0], m2_cov_hdd[1], m2_cov_hdd[2], m2_cov_hdd[3], m2_cov_hdd[4], m2_cov_hdd[5], m2_cov_hdd[6], m2_cov_hdd[7],
               m2_cov_hdd[8], m2_cov_hdd[9], m2_cov_hdd[10], m2_cov_hdd[11]);
    }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
