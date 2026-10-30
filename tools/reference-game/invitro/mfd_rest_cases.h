/* The HDD button executor 0x44a1a0 with its callees and three more HDD
 * functions (the radio selection 0x44a748, the map point conversion 0x44d24c,
 * the drag start 0x44d3cc), original machine code against sim/src/mfd.c
 * (--mfd-rest; target :differential_mfd_rest_test). Included after mfd_cases.h;
 * reuses its world (md_common, md_hdd), its case script (md_set*, md_w*,
 * md_rand) and its call hash (md_note), and runs with its own recorders and
 * dispatch (mr_/MR_), so the functions under test run unpatched on side 0 and
 * as the sim's rows on side 1.
 *
 * Run as the original's code on side 0 and the sim's rows on side 1:
 * 0x44a1a0, 0x44a748, 0x44d0c0, 0x44d124, 0x44cff8, 0x44d05c, 0x44cf90,
 * 0x450bf4, 0x450c18, 0x44ba08, 0x44b9b0, 0x44db9c, 0x44db4c, 0x44dd14,
 * 0x44dc6c, 0x433e3c, 0x44d24c, 0x44d3cc. Recorders on both sides (side 0:
 * a jump patched over the entry; side 1: a row at the same VA): hdd_page
 * 0x44a60c, status_mode 0x450b88, the fill 0x486704 (the rectangle and the
 * fill record), the panel copy 0x43122c (the rectangle's 16 bytes), the
 * port 0x433158, wingman_select 0x44da98, wingman_order 0x431610 (all
 * three dwords, so the high word 0x44db4c's ECX leaves is compared),
 * order_cancel 0x44dc10, canvas_push/pop 0x4311e0/0x431210, the label
 * 0x438920 (the text), the cursor setters 0x48992e/0x486e70, the zoom bar
 * 0x44eec8 and zoom out 0x44cfc4; the DialogPort's slot 0 (the message's
 * first 6 bytes, the ones the original writes) and the buttons' slot +4
 * are host functions with rows at their addresses on side 1. Compared:
 * the recorder hash and count, DATA, the world (masked: the two fill
 * record pointers, stack addresses on both sides) and the exit registers
 * the contract defines (per op). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "mfd_cases.h"
#include "audio_cases.h"

#define MR_PORT 0x0000u     /* the DialogPort: +0 its table */
#define MR_PORTVT 0x0010u   /* +0 the receive slot */
#define MR_CANVAS 0x0040u   /* canvas_push's object (+0x22c the fill record), 0x240 */
#define MR_CUR 0x0300u      /* 3 cursor entries of 0x10 */
#define MR_REC 0x0340u      /* an order record for 0x44db4c, 0x20 */
#define MR_PT 0x0380u       /* points for 0x44d24c/0x44d3cc */
#define MR_HDDREF 0x03c0u   /* [0x49d6b0]: +8 the HDD */
#define MR_SPEAKER 0x03d0u   /* [[0x49d230]+0x766]: the speaking actor */
#define MR_KINDS 41u

static es2_sim_entry mr_entries[4096];
static es2_sim_dispatch mr_dispatch;
static unsigned mr_kinds[MR_KINDS];
static uint32_t mr_ret[MR_KINDS][4];
static unsigned mr_calls[2][MR_KINDS];
static void mr_note(unsigned kind, const uint32_t *a, unsigned n) {
    uint32_t v[6] = {0x6d720000u | kind};
    for (unsigned i = 0; i < n && i < 5u; ++i) v[i + 1u] = a[i];
    md_note(0, v, n + 1u);
    if (md_side_now) ++mr_kinds[kind];
}
static uint32_t mr_record(unsigned kind, const uint32_t *a) {
    unsigned k = mr_calls[md_side_now][kind]++;
    uint32_t ret = mr_ret[kind][k & 3u];
    switch (kind) {
    case 3:   /* rect_fill(rect): the rectangle and the fill record */
        mr_note(3, a, 1);
        md_bytes(a[0], 16u);
        md_bytes(get32((BYTE *)(uintptr_t)(md_gc() + 0x22cu)), 8u);
        md_bytes(get32(md_world + MR_CANVAS + 0x22cu), 8u);
        return ret;
    case 4: {   /* panel_copy(1, rect, 0) */
        uint32_t v[2] = {a[0], a[2]};
        mr_note(4, v, 2);
        md_bytes(a[1], 16u);
        return ret;
    }
    case 5: mr_note(5, NULL, 0); return md_at(MR_PORT);
    case 6: mr_note(6, a, 1); md_bytes(a[1], 6u); return ret;   /* the port's receive: the 6 written bytes */
    case 9: mr_note(9, a, 3); md_bytes((uint32_t)(uintptr_t)(mapped + (0x004d03d4u - 0x400000u)), 0x16u); return ret;   /* wingman_order */
    case 11: mr_note(11, NULL, 0); return md_at(MR_CANVAS);   /* canvas_push */
    case 12: mr_note(12, NULL, 0); return ret;                /* canvas_pop */
    case 13: mr_note(13, a, 1); md_string(a[1]); return ret;  /* label */
    case 18: case 19: case 31: mr_note(kind, NULL, 0); return ret;   /* 0x4632c4, the clock, camera_current */
    case 30: mr_note(30, a, 1); md_bytes(a[0] + 4u, 0x208u); return ret;   /* gc_select(V): the context it selects */
    case 32: mr_note(32, a, 1); md_bytes(a[0], 0x16u); return ret;         /* view_setup(C) */
    case 34:   /* 0x483a04(a, b): two stack points and the fill record [V+0x228] */
        mr_note(34, NULL, 0);
        md_bytes(a[0], 8u);
        md_bytes(a[1], 8u);
        md_bytes(get32(md_world + MR_CANVAS + 0x228u), 8u);
        return ret;
    case 27: case 36: case 40: mr_note(kind, a, 3); return ret;   /* 0x44b148(H, S, e); button_press(G, n, 1); 0x447158(G, o, n) */                 /* 0x44b148(H, S, e) */
    case 1: case 2: case 8: case 14: case 15: case 20: case 21: case 22: case 25: case 26: mr_note(kind, a, 2); return ret;
    default: mr_note(kind, a, 1); return ret;
    }
}
#define MR_HOST0(k) static uint32_t __cdecl mr_o##k(void) { return mr_record(k, NULL); }
#define MR_HOST1(k) static uint32_t __cdecl mr_o##k(uint32_t a) { const uint32_t s[1] = {a}; return mr_record(k, s); }
#define MR_HOST2(k) static uint32_t __cdecl mr_o##k(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return mr_record(k, s); }
#define MR_HOST3(k) static uint32_t __cdecl mr_o##k(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return mr_record(k, s); }
MR_HOST2(1)
MR_HOST2(2)
MR_HOST1(3)
MR_HOST3(4)
MR_HOST0(5)
MR_HOST2(6)
MR_HOST1(7)
MR_HOST2(8)
MR_HOST3(9)
MR_HOST1(10)
MR_HOST0(11)
MR_HOST0(12)
MR_HOST2(13)
MR_HOST2(14)
MR_HOST2(15)
MR_HOST1(16)
MR_HOST1(17)
MR_HOST0(18)
MR_HOST0(19)
MR_HOST2(20)
MR_HOST2(21)
MR_HOST2(22)
MR_HOST1(23)
MR_HOST1(24)
MR_HOST2(25)
MR_HOST2(26)
MR_HOST3(27)
MR_HOST1(28)
MR_HOST1(29)
MR_HOST1(30)
MR_HOST0(31)
MR_HOST1(32)
MR_HOST1(33)
MR_HOST2(34)
MR_HOST1(35)
MR_HOST3(36)
MR_HOST1(37)
MR_HOST1(38)
MR_HOST1(39)
MR_HOST3(40)
static void mr_s_stub(es2_sim_service_call *sc, void *user) { sc->eax = mr_record((unsigned)(uintptr_t)user, sc->slots); }
static const struct { uint32_t va; void *stub; unsigned kind, slots; } mr_stubs[] = {
    {0x0044a60cu, (void *)mr_o1, 1, 2}, {0x00450b88u, (void *)mr_o2, 2, 2}, {0x00486704u, (void *)mr_o3, 3, 1},
    {0x0043122cu, (void *)mr_o4, 4, 3}, {0x00433158u, (void *)mr_o5, 5, 0}, {0x0044da98u, (void *)mr_o8, 8, 2},
    {0x00431610u, (void *)mr_o9, 9, 3}, {0x0044dc10u, (void *)mr_o10, 10, 1}, {0x004311e0u, (void *)mr_o11, 11, 0},
    {0x00431210u, (void *)mr_o12, 12, 0}, {0x00438920u, (void *)mr_o13, 13, 2}, {0x0048992eu, (void *)mr_o14, 14, 2},
    {0x00486e70u, (void *)mr_o15, 15, 2}, {0x0044eec8u, (void *)mr_o16, 16, 1}, {0x0044cfc4u, (void *)mr_o17, 17, 1},
    {0x004632c4u, (void *)mr_o18, 18, 0}, {0x00467828u, (void *)mr_o19, 19, 0}, {0x0044aea0u, (void *)mr_o20, 20, 2},
    {0x00435b14u, (void *)mr_o21, 21, 2}, {0x00435b38u, (void *)mr_o22, 22, 2}, {0x00463400u, (void *)mr_o23, 23, 1},
    {0x0046356cu, (void *)mr_o24, 24, 1}, {0x0044b3dcu, (void *)mr_o25, 25, 2}, {0x0044b344u, (void *)mr_o26, 26, 2},
    {0x0044b148u, (void *)mr_o27, 27, 3}, {0x00462af0u, (void *)mr_o28, 28, 1}, {0x00462830u, (void *)mr_o29, 29, 1},
    {0x00480d44u, (void *)mr_o30, 30, 1}, {0x0045edf8u, (void *)mr_o31, 31, 0}, {0x0048c2e4u, (void *)mr_o32, 32, 1},
    {0x0045030cu, (void *)mr_o33, 33, 1}, {0x00483a04u, (void *)mr_o34, 34, 2},
};
#define MR_STUBS (sizeof(mr_stubs) / sizeof(mr_stubs[0]))
static BYTE mr_saved[MR_STUBS][5];
/* The MFD keys' callees: recorders only in the keys part (panel_visible
 * runs as itself in the other parts). */
static const struct { uint32_t va; void *stub; unsigned kind, slots; } mr_key_stubs[] = {
    {0x00431410u, (void *)mr_o35, 35, 1}, {0x00438d9cu, (void *)mr_o36, 36, 3}, {0x00446ff0u, (void *)mr_o37, 37, 1},
    {0x00447070u, (void *)mr_o38, 38, 1}, {0x0044703cu, (void *)mr_o39, 39, 1}, {0x00447158u, (void *)mr_o40, 40, 3},
};
#define MR_KEY_STUBS (sizeof(mr_key_stubs) / sizeof(mr_key_stubs[0]))
static BYTE mr_key_saved[MR_KEY_STUBS][5];
static int mr_keys_on;
static int mr_put(uint32_t va, const char *name, es2_sim_service_fn fn, void *user, unsigned slots) {
    for (unsigned i = 0; i < mr_dispatch.count; ++i) {
        es2_sim_entry *e = &mr_dispatch.entries[i];
        if (e->va != va) continue;
        e->kind = ES2_SIM_SERVICE; e->name = name; e->service = fn; e->service_user = user; e->pop_bytes = 0; e->stack_slots = (uint8_t)slots;
        return 0;
    }
    return es2_sim_dispatch_add_service(&mr_dispatch, va, name, fn, user, 0, (uint8_t)slots);
}
static int mr_dispatch_setup(void) {
    es2_sim_dispatch_init(&mr_dispatch, mr_entries, 4096, md_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&mr_dispatch, &s)) return fail("mfd rest: dispatch");
    for (unsigned i = 0; i < MR_STUBS; ++i)
        if (mr_put(mr_stubs[i].va, "mfd rest recorder", mr_s_stub, (void *)(uintptr_t)mr_stubs[i].kind, mr_stubs[i].slots)) return fail("mfd rest: dispatch");
    if (mr_put((uint32_t)(uintptr_t)mr_o6, "mfd rest port slot", mr_s_stub, (void *)(uintptr_t)6, 2) ||
        mr_put((uint32_t)(uintptr_t)mr_o7, "mfd rest button slot", mr_s_stub, (void *)(uintptr_t)7, 1))
        return fail("mfd rest: dispatch");
    for (unsigned i = 0; mr_keys_on && i < MR_KEY_STUBS; ++i)
        if (mr_put(mr_key_stubs[i].va, "mfd rest key recorder", mr_s_stub, (void *)(uintptr_t)mr_key_stubs[i].kind, mr_key_stubs[i].slots))
            return fail("mfd rest: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&mr_dispatch, &error, &bad)) return fail("mfd rest: dispatch seal");
    return 0;
}

/* ---- the case script: md_common + md_hdd, then the executor's state */
typedef struct mr_op { uint32_t va, a[4], in[3]; unsigned n, regs; } mr_op;
static mr_op mr_ops[2];
static unsigned mr_op_count;
static uint32_t mr_out[2][2][3];
static void mr_op_add(uint32_t va, unsigned regs, unsigned n, const uint32_t *a) {
    mr_op *o = &mr_ops[mr_op_count++];
    memset(o, 0, sizeof(*o));
    o->va = va; o->regs = regs; o->n = n;
    for (unsigned i = 0; i < 3u; ++i) o->in[i] = md_rand(2u) ? random32() : 0u;
    for (unsigned i = 0; i < n; ++i) o->a[i] = a[i];
}
#define MR_OP(va, regs, ...) mr_op_add((va), (regs), (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (const uint32_t[]){__VA_ARGS__})
#define MR_EAX 1u
#define MR_EDX 2u
#define MR_ECX 4u
static uint32_t mr_index(void) { static const uint32_t v[] = {0xffffffffu, 0, 1, 2}; return v[md_rand(4u)]; }
static void mr_world(void) {
    md_common();
    md_hdd();
    mr_op_count = 0;
    for (unsigned k = 0; k < MR_KINDS; ++k)
        for (unsigned i = 0; i < 4u; ++i) mr_ret[k][i] = random32();
    uint32_t h = MD_H, s = MD_HP, x = MD_HP + 0x150u;
    /* the buttons 0..12 (10..12 the radio lamps) and their slot +4 */
    for (unsigned i = 0; i < 13u; ++i) md_w32(h + 0x18u + 4u * i, md_at(MD_W + 0x20u * i));
    md_w32(MD_WVT + 4u, (uint32_t)(uintptr_t)mr_o7);
    for (unsigned i = 0; i < 16u; ++i) md_w8(MD_W + 0x20u * i, md_rand(2u));   /* the lamps' +0x40 (button i-2's +0) */
    /* the request, the minimised flag, the page, the selection, the lamp */
    md_w8(h + 0x528u, md_rand(8u) ? md_rand(15u) : md_rand(2u) ? 0xffu : 15u + md_rand(241u));
    md_w8(h + 0x76u, md_rand(4u) ? 0u : 1u + md_rand(255u));
    md_w32(h + 0x109u, md_rand(8u) ? md_rand(2u) : 2u + md_rand(3u));
    md_w32(h + 0x517u, mr_index());
    md_w8(h + 0x51fu, md_rand(2u) ? 0u : 1u + md_rand(255u));
    md_w32(h + 0x14u, random32());
    md_w32(h + 0xf1u, md_signed(300u));
    for (unsigned i = 0; i < 5u; ++i) md_w32(h + 0x534u + 4u * i, md_rand(3u) ? md_at(MD_ENTRIES + 0xd0u * md_rand(4u)) : 0u);
    for (unsigned i = 0; i < 4u; ++i) md_w8(MD_ENTRIES + 0xd0u * i + 0xa3u, md_rand(2u) ? 0u : 1u + md_rand(255u));
    md_w16(h + 0x55cu, md_rand(6u) ? md_rand(5u) : 0xffffu);
    /* the radio records' +0x99 (md_hdd: 0/1) */
    for (unsigned k = 0; k < 3u; ++k) md_w8(MD_V + 0xa0u * k + 0x99u, md_rand(3u) ? 0u : 1u);
    /* the map page S: the order state, the label, the records */
    md_w8(s + 0x103u, md_rand(2u) ? 0u : 1u + md_rand(255u));
    md_w8(s + 0x104u, md_rand(2u) ? 0u : 1u + md_rand(255u));
    md_w32(s + 0x105u, mr_index());
    static const uint32_t modes[] = {0xbu, 0xcu, 0xdu, 0xeu, 0xau, 0xfu, 0u};
    md_w32(s + 0x5du, modes[md_rand(7u)]);
    md_w32(s + 0x13au, md_at(MD_TLABEL));
    for (unsigned n = 0; n < 3u; ++n) md_w32(s + 0x85u + 42u * n + 0x22u, md_rand(4u) ? md_rand(2u) : md_rand(4u));
    for (unsigned n = 0; n < 3u; ++n) md_w32(s + 0x85u + 42u * n + 0x26u, md_rand(4u) ? md_rand(2u) : md_rand(4u));
    /* the zoom */
    md_w32(s + 0x20u, md_signed(0x40000u));
    uint32_t zstep = md_rand(0x8000u);
    md_set32(0x004d1ce4u, zstep);
    md_set32(0x004d1ce0u, md_r32(s + 0x20u) + zstep + md_signed(3u));
    /* the vertical scroll: [HQ+0x224], P+0x1c near a room */
    md_w32(MD_HQ + 0x224u, md_signed(0x400u));
    uint32_t shift = get32(mapped + (0x0049d6bcu - 0x400000u));
    for (unsigned i = 0; i < md_preset_count; ++i) if (md_presets[i].va == 0x0049d6bcu) shift = md_presets[i].value;
    uint32_t k = (uint32_t)((int32_t)((0u - md_r32(MD_HQ + 0x224u)) * md_r32(s + 0x20u)) >> (shift & 31u));
    uint32_t lo = random32() % 0x100000u, hi = lo + random32() % 0x100000u, step = md_r32(s + 0x145u);
    md_set32(0x004aa644u, lo);
    md_set32(0x004aa64cu, hi);
    switch (md_rand(4u)) {
    case 0: md_w32(s + 0x1cu, lo - 0xea60u + k + step + md_signed(step + 2u)); break;
    case 1: md_w32(s + 0x1cu, hi + 0xea60u - k - step - md_signed(step + 2u)); break;
    case 2: md_w32(s + 0x1cu, hi + 0xea60u - k + md_signed(3u)); break;
    default: md_w32(s + 0x1cu, md_signed(0x200000u)); break;
    }
    /* the status page X */
    md_w32(x + 0x7cu, md_rand(5u) ? md_rand(3u) : md_signed(3u));
    /* the DialogPort, the canvas, the cursors */
    md_w32(MR_PORT, md_at(MR_PORTVT));
    md_w32(MR_PORTVT, (uint32_t)(uintptr_t)mr_o6);
    random_bytes(md_world_start + MR_CANVAS, 0x240);
    md_w32(MR_CANVAS + 0x22cu, md_at(MR_PT + 0x30u));   /* fill records the cases do not write: readable */
    md_w32(MD_GC + 0x22cu, md_at(MR_PT + 0x38u));
    for (unsigned i = 0; i < 3u; ++i) {
        md_w32(MR_CUR + 0x10u * i + 4u, md_rand(3u) ? random32() : 0u);
        md_w32(MR_CUR + 0x10u * i + 8u, random32());
        md_set32(0x0049b08cu + 4u * i, md_at(MR_CUR + 0x10u * i));
    }
    md_w32(MD_PANEL + 0x226u, md_rand(3u) ? md_at(MD_PANEL + 0x232u) : random32());
    md_set8(0x0049b105u, md_rand(4u) ? 1u + md_rand(255u) : 0u);
    md_set32(0x0049b630u, md_rand(3u));
    md_set16(0x004d2566u, md_rand(2u) ? 0u : random32());
    /* DATA: the lamp colours, the order texts and colour, the drag */
    md_set16(0x004d3b96u, md_rand(4u) ? random32() : 0u);
    md_set16(0x004d3b9au, md_rand(4u) ? random32() : 0u);
    md_set16(0x004d3ba2u, random32());
    for (unsigned i = 0; i < 4u; ++i) md_set32(0x004d1638u + 4u * i, md_text_at(md_rand(16u)));
    md_set32(0x004d03d4u, random32());
    md_set8(0x0049dbe0u, random32());
    md_set32(0x004d2556u, random32());
    md_set32(0x004d255au, random32());
    md_w32(MR_HDDREF + 8u, md_at(h));
    md_set32(0x0049d6b0u, md_at(MR_HDDREF));
    random_bytes(md_world_start + MR_PT, 0x40);
}
static void mr_side(unsigned side) {
    md_side(side);
    memset(mr_calls[side], 0, sizeof(mr_calls[side]));
    lc_loop.dispatch = &mr_dispatch;
}
static int mr_run(const char *what, unsigned k) {
    for (unsigned side = 0; side < 2u; ++side) {
        mr_side(side);
        for (unsigned i = 0; i < mr_op_count; ++i) {
            mr_op *o = &mr_ops[i];
            uint32_t out[3] = {0, 0, 0};
            static char where[200];
            snprintf(where, sizeof(where), "mfd rest (%s side %u op %u: 0x%08lx(0x%lx, 0x%lx))", what, side, i, (unsigned long)o->va,
                     (unsigned long)o->a[0], (unsigned long)o->a[1]);
            current_function = where;
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, o->in, out);
            else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&mr_dispatch, o->va, o->a, o->n, o->in, &r)) md_bad = 1;
                out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
            }
            memcpy(mr_out[side][i], out, sizeof(out));
            if (md_bad) { fprintf(stderr, "FAIL: mfd rest %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va); return 1; }
        }
        put32(md_world + MD_GC + 0x22cu, 0);       /* stack addresses on both sides (the fill records) */
        put32(md_world + MR_CANVAS + 0x22cu, 0);
        put32(md_world + MR_CANVAS + 0x228u, 0);
        if (!side) memcpy(md_world_start + MD_WORLD_SIZE, md_world, MD_WORLD_SIZE);
        su_take(side);
    }
    if (md_hash[0] != md_hash[1] || md_count[0] != md_count[1]) {
        fprintf(stderr, "FAIL: mfd rest %s case %u: recorded calls differ (%u / %u)\n  original:%s\n  own:     %s\n", what, k, md_count[0],
                md_count[1], md_text[0], md_text[1]);
        for (unsigned i = 0; i < md_count[0] && i < 64u; ++i)
            if (md_each[0][i] != md_each[1][i]) { fprintf(stderr, "  first differing call %u\n", i); break; }
        return 1;
    }
    if (su_compare(what, k)) return 1;
    if (memcmp(md_world, md_world_start + MD_WORLD_SIZE, MD_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < MD_WORLD_SIZE && shown < 16u; ++i)
            if (md_world[i] != md_world_start[MD_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, md_world_start[MD_WORLD_SIZE + i], md_world[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: mfd rest %s case %u: the world differs (world at 0x%08lx)\n", what, k, (unsigned long)(uintptr_t)md_world);
        return 1;
    }
    static const char *const names[3] = {"EAX", "EDX", "ECX"};
    for (unsigned i = 0; i < mr_op_count; ++i)
        for (unsigned r = 0; r < 3u; ++r)
            if ((mr_ops[i].regs >> r & 1u) && mr_out[0][i][r] != mr_out[1][i][r]) {
                fprintf(stderr, "FAIL: mfd rest %s case %u op %u (0x%08lx): %s original 0x%08lx own 0x%08lx\n", what, k, i,
                        (unsigned long)mr_ops[i].va, names[r], (unsigned long)mr_out[0][i][r], (unsigned long)mr_out[1][i][r]);
                return 1;
            }
    return 0;
}

/* ---- the parts */
static unsigned mr_cov_exec[16][2], mr_cov_ops[16];
static int mr_exec_case(unsigned k) {
    mr_world();
    uint32_t h = MD_H;
    uint8_t request = md_world_start[h + 0x528u];
    uint32_t page = md_r32(h + 0x109u);
    MR_OP(0x0044a1a0u, 0, md_at(h));
    unsigned before[MR_KINDS];
    memcpy(before, mr_kinds, sizeof(before));
    int bad = mr_run("executor", k);
    if (!bad) {
        unsigned i = request < 15u ? request : 15u;
        int done = md_world[h + 0x528u] == 0xffu && request != 0xffu;
        if (request == 0xffu) ++mr_cov_exec[15][0];
        else if (page < 2u) ++mr_cov_exec[i][page];
        if (done) ++mr_cov_ops[15];
        if (mr_kinds[3] != before[3]) ++mr_cov_ops[14];   /* the lamp fill */
        if (mr_kinds[9] != before[9]) ++mr_cov_ops[13];   /* wingman_order */
        if (mr_kinds[6] != before[6]) ++mr_cov_ops[12];   /* the port message */
    }
    return bad;
}
static int mr_leaf_case(unsigned k) {
    mr_world();
    uint32_t h = MD_H, s = MD_HP, x = MD_HP + 0x150u;
    unsigned which = md_rand(11u);
    switch (which) {
    case 0: MR_OP(0x0044a748u, MR_EAX, md_at(h), mr_index()); break;
    case 1: MR_OP(md_rand(2u) ? 0x0044d0c0u : 0x0044d124u, MR_EAX, md_at(s)); break;
    case 2: MR_OP(0x0044cf90u, MR_EAX, md_at(s)); break;
    case 3: MR_OP(md_rand(2u) ? 0x00450bf4u : 0x00450c18u, MR_EAX, md_at(x)); break;
    case 4: MR_OP(md_rand(2u) ? 0x0044ba08u : 0x0044b9b0u, MR_EAX | MR_EDX | MR_ECX, md_at(h)); break;
    case 5: MR_OP(0x0044db9cu, MR_EAX | MR_EDX | MR_ECX, md_at(s)); break;
    case 6: MR_OP(0x0044db4cu, MR_EAX | MR_EDX | MR_ECX, md_at(s), md_rand(2u) ? md_at(MR_REC) : 0x004d03d4u); break;
    case 7: MR_OP(0x0044dd14u, MR_EAX | MR_ECX, md_at(s)); break;
    case 8:
        MR_OP(0x0044dc6cu, MR_EAX, md_at(s), md_rand(3u) ? 0u : md_text_at(md_rand(16u)), md_rand(2u) ? 1u : md_rand(3u));
        mr_ops[0].in[0] = md_rand(4u);   /* the index the original takes from EAX on one path */
        break;
    case 9: MR_OP(0x00433e3cu, 0, md_rand(4u) ? md_rand(3u) : get32(mapped + (0x0049b630u - 0x400000u))); break;
    default: {
        /* the map point conversion and the drag start, with aliasing */
        uint32_t m = s, src = md_at(MR_PT), dst = md_at(MR_PT + 0x10u);
        for (unsigned i = 0; i < 4u; ++i) md_w32(m + 0x71u + 4u * i, md_signed(0x10000u));
        if (md_rand(2u)) {
            switch (md_rand(4u)) {
            case 0: dst = src; break;
            case 1: dst = md_at(m + 0x71u); break;
            case 2: src = md_at(m + 0x75u); break;
            default: dst = md_at(MR_PT + 4u); break;
            }
            MR_OP(0x0044d24cu, MR_EAX | MR_EDX | MR_ECX, md_at(m), src, dst);
        } else {
            uint32_t pt = md_rand(3u) ? src : md_at(m + 0x152u + 4u * md_rand(2u));
            MR_OP(0x0044d3ccu, MR_EAX | MR_EDX | MR_ECX, md_at(m), random32(), pt);
        }
        break;
    }
    }
    int bad = mr_run("leaves", k);
    if (!bad) ++mr_cov_ops[which];
    return bad;
}

/* The radio panel 0x44b620: the three records' states 0..4 with their
 * previous state, times next to the clock, the speaker, the voice and
 * lip-sync answers. */
static unsigned mr_cov_radio[8];
static int mr_radio_case(unsigned k) {
    mr_world();
    uint32_t h = MD_H, now = random32() >> 1;
    for (unsigned i = 0; i < 4u; ++i) mr_ret[19][i] = now;
    for (unsigned i = 0; i < 4u; ++i) {
        mr_ret[23][i] = md_rand(2u) ? random32() & 0xffffff00u : random32() | 1u;              /* voice: AL */
        mr_ret[24][i] = md_rand(2u) ? random32() | 0xffffu : md_rand(2u) ? 0xffffu : random32() & 0xfffffffeu;   /* lip value */
        mr_ret[28][i] = md_rand(2u) ? 0u : random32() | 1u;                                     /* sfx playing */
        mr_ret[4][i] = random32();                                                             /* the copy's AL */
    }
    uint32_t actors[3];
    for (unsigned r = 0; r < 3u; ++r) {
        uint32_t s = h + 0x12du + 0x14eu * r;
        actors[r] = md_rand(5u) ? random32() | 1u : 0u;
        md_w32(s + 0x137u, actors[r]);
        md_w32(s + 0x13bu, md_rand(8u) ? md_rand(4u) : md_rand(2u) ? 4u : random32());
        md_w32(s + 0x13fu, md_rand(2u) ? md_r32(s + 0x13bu) : md_rand(4u));
        md_w32(s + 0x143u, now + md_signed(2u));
        md_w8(s + 0x147u, md_rand(2u) ? 0u : 1u + md_rand(255u));
        md_w8(s + 0x12du, md_rand(2u) ? 0x25u : md_rand(0x40u));
        md_set32(0x004d03c8u + 4u * r, md_at(MD_V + 0xa0u * r));
    }
    md_w32(MR_SPEAKER, md_rand(2u) ? actors[md_rand(3u)] : random32());
    md_set32(0x0049d230u, md_at(MR_SPEAKER) - 0x766u);
    md_set8(0x0049d4f4u, md_rand(2u) ? 0u : 1u + md_rand(255u));
    MR_OP(0x0044b620u, 0, md_at(h));
    unsigned before[MR_KINDS];
    memcpy(before, mr_kinds, sizeof(before));
    int bad = mr_run("radio", k);
    if (!bad) {
        static const unsigned kinds[7] = {20, 25, 26, 27, 29, 22, 21};
        for (unsigned i = 0; i < 7u; ++i) if (mr_kinds[kinds[i]] != before[kinds[i]]) ++mr_cov_radio[i];
        if (get32(md_world + MR_SPEAKER) == 0u && md_r32(MR_SPEAKER) != 0u) ++mr_cov_radio[7];
    }
    return bad;
}

/* The camera page frame 0x44060c: the page X with its rectangle, the view
 * V (MR_CANVAS), the camera C (MR_REC), the display byte and the shifts. */
static unsigned mr_cov_camera[2];
static int mr_camera_case(unsigned k) {
    mr_world();
    uint32_t x = MD_X;
    random_bytes(md_world_start + x, 0x60);
    md_w32(x + 4u, md_at(MD_TLABEL + 4u));
    md_w32(x + 0x10u, md_at(MR_CANVAS));
    md_w32(x + 0x14u, md_at(MR_REC));
    random_bytes(md_world_start + MR_REC, 0x20);
    md_set16(0x0049c454u, random32());
    md_set16(0x004d3b9cu, random32());
    md_set16(0x004d253au, md_rand(4u) ? md_rand(32u) : random32());
    md_set16(0x004d253cu, md_rand(4u) ? md_rand(32u) : random32());
    MR_OP(0x0044060cu, 0, md_at(x));
    int bad = mr_run("camera", k);
    if (!bad) ++mr_cov_camera[get32(md_world + MD_E + 0x1cu) & 0xffu ? 0 : 1];
    return bad;
}

/* The MFD keys 0x4469e8(G, key): the gauge G (MD_ENTRIES, 0x100 bytes),
 * its page [G+0xc1], the button table 0x49cbd8 (13 bytes a page), the
 * order dword [G+0xfb]; panel_visible's AL scripted. */
static unsigned mr_cov_keys[8];
static int mr_keys_case(unsigned k) {
    mr_world();
    uint32_t g = MD_ENTRIES;
    random_bytes(md_world_start + g, 0x100);
    md_w32(g + 0xc1u, md_rand(8u) ? md_rand(6u) : md_rand(2u) ? 1u : random32() % 7u);
    for (unsigned i = 0; i < 7u * 13u; ++i) md_set8(0x0049cbd8u + i, md_rand(2u) ? 0u : 1u + md_rand(255u));
    for (unsigned i = 0; i < 4u; ++i) mr_ret[35][i] = md_rand(5u) ? random32() | 1u : random32() & 0xffffff00u;
    static const uint32_t keys[] = {0x20u, 0x213u, 0x214u, 0x2du, 0x33u, 0x34u, 0x12u, 0x18u, 0x1eu, 0x21u, 0x22u, 0x23u, 0x2eu,
                                    0x13u, 0x2fu, 0x11u, 0x30u, 0x1fu};
    uint32_t key = md_rand(6u) ? keys[md_rand(sizeof(keys) / sizeof(keys[0]))] : md_rand(0x300u);
    if (md_rand(3u) == 0u) key |= random32() & 0xffff0000u;
    MR_OP(0x004469e8u, MR_EAX, md_at(g), key);
    unsigned before[MR_KINDS];
    memcpy(before, mr_kinds, sizeof(before));
    uint32_t visible = mr_ret[35][0];
    int bad = mr_run("keys", k);
    if (!bad) {
        static const unsigned kinds[5] = {36, 37, 38, 39, 40};
        for (unsigned i = 0; i < 5u; ++i) if (mr_kinds[kinds[i]] != before[kinds[i]]) ++mr_cov_keys[i];
        ++mr_cov_keys[mr_out[0][0][0] & 0xffu ? 5 : 6];
        if (!(visible & 0xffu)) ++mr_cov_keys[7];
    }
    return bad;
}

static int mfd_rest_cases(void) {
    current_function = "mfd rest";
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
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !md_world || !md_world_start)
        return fail("mfd rest: memory");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("mfd rest: import slot not found");
        }
    if (mr_dispatch_setup()) return 1;
    for (unsigned i = 0; i < MR_STUBS; ++i) if (md_patch(mr_stubs[i].va, mr_stubs[i].stub, mr_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"executor", mr_exec_case, 6000}, {"leaves", mr_leaf_case, 6000}, {"radio", mr_radio_case, 4000}, {"camera", mr_camera_case, 2000},
        {"keys", mr_keys_case, 4000},
    };
    /* Each part's cases run in chunks of MR_CHUNK; chunk c of the whole run
     * belongs to test shard c % count (suite_part_in_shard) and starts from its
     * own seed, so a chunk's cases do not depend on the shard layout. The
     * coverage lines count the shard's own chunks. */
    enum { MR_CHUNK = 500 };
    unsigned chunk = 0;
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        if (parts[p].run == mr_keys_case) {
            /* the keys part last: its own dispatch and patches */
            mr_keys_on = 1;
            if (mr_dispatch_setup()) bad = 1;
            for (unsigned i = 0; !bad && i < MR_KEY_STUBS; ++i) if (md_patch(mr_key_stubs[i].va, mr_key_stubs[i].stub, mr_key_saved[i], 1)) bad = 1;
        }
        unsigned owned = 0;
        for (unsigned first = 0; !bad && first < parts[p].cases; first += MR_CHUNK, ++chunk) {
            if (!suite_part_in_shard(chunk)) continue;
            unsigned end = parts[p].cases - first < MR_CHUNK ? parts[p].cases : first + MR_CHUNK;
            random_state = 0x48c06e21u ^ (chunk * 0x9e3779b9u);
            for (unsigned k = first; !bad && k < end; ++k) {
                current_case = k; current_case_valid = 1;
                bad = parts[p].run(k);
            }
            owned += end - first;
        }
        if (!bad && owned) printf("PASS: mfd rest %s: %u of %u cases identical\n", parts[p].name, owned, parts[p].cases);
    }
    for (unsigned i = 0; i < MR_KEY_STUBS; ++i) if (md_patch(mr_key_stubs[i].va, mr_key_stubs[i].stub, mr_key_saved[i], 0)) bad = 1;
    for (unsigned i = 0; i < MR_STUBS; ++i) if (md_patch(mr_stubs[i].va, mr_stubs[i].stub, mr_saved[i], 0)) bad = 1;
    if (!bad) {
        printf("MFD rest coverage: executor case/page0/page1:");
        for (unsigned i = 0; i < 15u; ++i) printf(" %u:%u/%u", i, mr_cov_exec[i][0], mr_cov_exec[i][1]);
        printf("; none %u, done %u, lamp fill %u, wingman_order %u, port message %u\n", mr_cov_exec[15][0], mr_cov_ops[15],
               mr_cov_ops[14], mr_cov_ops[13], mr_cov_ops[12]);
        printf("MFD rest coverage: leaves 44a748 %u, scroll v %u, zoom in %u, status %u, target %u, commit %u, copy %u, text %u, "
               "label %u, cursor %u, point/drag %u; calls: hdd_page %u, status_mode %u, fill %u, copy %u, port %u, receive %u, "
               "button %u, select %u, order %u, cancel %u, push %u, pop %u, label %u, cursor %u/%u, zoom bar %u, zoom out %u\n",
               mr_cov_ops[0], mr_cov_ops[1], mr_cov_ops[2], mr_cov_ops[3], mr_cov_ops[4], mr_cov_ops[5], mr_cov_ops[6],
               mr_cov_ops[7], mr_cov_ops[8], mr_cov_ops[9], mr_cov_ops[10], mr_kinds[1], mr_kinds[2], mr_kinds[3], mr_kinds[4],
               mr_kinds[5], mr_kinds[6], mr_kinds[7], mr_kinds[8], mr_kinds[9], mr_kinds[10], mr_kinds[11], mr_kinds[12],
               mr_kinds[13], mr_kinds[14], mr_kinds[15], mr_kinds[16], mr_kinds[17]);
        printf("MFD rest coverage: radio draw %u, frame %u, talk %u, line %u, sound %u, port mark %u, port done %u, speaker cleared %u\n",
               mr_cov_radio[0], mr_cov_radio[1], mr_cov_radio[2], mr_cov_radio[3], mr_cov_radio[4], mr_cov_radio[5], mr_cov_radio[6],
               mr_cov_radio[7]);
        printf("MFD rest coverage: camera frame skipped %u, drawn %u (view_setup %u, map_terrain %u, view fill %u)\n", mr_cov_camera[0],
               mr_cov_camera[1], mr_kinds[32], mr_kinds[33], mr_kinds[34]);
        printf("MFD rest coverage: keys button_press %u, range %u, next %u, prev %u, order key %u; AL 1 %u, AL 0 %u, invisible %u\n",
               mr_cov_keys[0], mr_cov_keys[1], mr_cov_keys[2], mr_cov_keys[3], mr_cov_keys[4], mr_cov_keys[5], mr_cov_keys[6],
               mr_cov_keys[7]);
    }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
