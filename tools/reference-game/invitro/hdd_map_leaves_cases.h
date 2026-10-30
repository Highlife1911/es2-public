/* The HDD map's button and icon leaves (the button lamp 0x44ede0, the button
 * rows on and off 0x44ee00/0x44ee24, the map tick 0x44d370, the icon's
 * method-table slots 0x44f5d0/0x44f5f4/0x44f630, the wingman button's blink
 * 0x44f644 and the drag widget's press 0x44f6d4), original machine code against
 * sim/src/mfd.c (hdd_map_leaves.h). It reuses startup_cases.h's snapshots and
 * loop_cases.h's loop, as widget_leaves_cases.h does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same synthetic world from the same seeded generator: the map page M
 * (holder 0x49d6b0) with its HDD [M+8], sixteen buttons behind [M+0x31],
 * the countdown word M+0x13e near zero, four icons with every state byte
 * +0x1f and lock byte +0x37, a drag widget whose [W+0x28] is M, the blink
 * bytes 0x49d6ac..0x49d6ae, the drag point 0x4d1de3, 0x49dbe0 and the
 * scroll words 0x4d2556/0x4d255a. Then 1..8 of the 9 rows run on it, each
 * with random entry EAX, EDX and ECX. The original side calls the rows
 * through an own thunk that loads and stores the three registers; the drag
 * start 0x44d3cc runs for real (the own side: its mfd_frames.h row). The callees
 * of other packages (panel_visible 0x431410, a hook, and the panel copy
 * 0x43122c, an hud_load.h row) are recording stubs on both sides, and the
 * method-table slots hold host stubs; every stub logs its entry EAX, EDX,
 * ECX and arguments and leaves seeded EAX, EDX and ECX. Compared: EAX, EDX
 * and ECX of every call, DATA 0x497000..0x6cc000 and the heap, the stub
 * log; no own coupling call. Coverage: per-path counters from the original
 * side; every one must be non-zero. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/hud.h"

#define HM_TRACE 256u
#define HM_BUTTONS 16u
#define HM_ICONS 4u
static es2_sim_entry hm_entries[4096];
static es2_sim_dispatch hm_dispatch;
static es2_sim_hud hm_hud;
static es2_sim_hud_event hm_trace[HM_TRACE];
static int hm_bad;
static unsigned hm_side;

/* ---- the original side's register thunk: pushes the arguments, enters
 * with EAX/EDX/ECX from in[3], stores EAX/EDX/ECX into out[3]. */
extern void hm_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _hm_call\n_hm_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

/* ---- the stub log of a side */
static uint64_t hm_hash[2];
static unsigned hm_count[2], hm_letters[2][128];
static char hm_text[2][8192];
static size_t hm_len[2];
static void hm_note(char letter, const uint32_t in[3], const uint32_t *s, unsigned n) {
    uint64_t h = hm_hash[hm_side];
    uint32_t v[7] = {(uint32_t)letter, in[0], in[1], in[2], 0, 0, 0};
    for (unsigned i = 0; i < n && i < 3u; ++i) v[4 + i] = s[i];
    for (unsigned i = 0; i < 7u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    hm_hash[hm_side] = h;
    ++hm_count[hm_side];
    ++hm_letters[hm_side][(unsigned char)letter & 127u];
    if (hm_len[hm_side] + 80u < sizeof(hm_text[0]))
        hm_len[hm_side] += (size_t)sprintf(hm_text[hm_side] + hm_len[hm_side], "%c[%lx %lx %lx](%lx,%lx,%lx) ", letter, (unsigned long)v[1],
                                           (unsigned long)v[2], (unsigned long)v[3], (unsigned long)v[4], (unsigned long)v[5],
                                           (unsigned long)v[6]);
}
static uint8_t hm_get8(uint32_t a) { return *(BYTE *)(uintptr_t)a; }
static uint16_t hm_get16(uint32_t a) { return (uint16_t)(hm_get8(a) | hm_get8(a + 1u) << 8); }
static void hm_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void hm_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static void hm_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }

/* ---- the stubs' answers: a generator reset per side */
static uint32_t hm_answer_seed;
static uint32_t hm_answer(void) {
    hm_answer_seed = hm_answer_seed * 1103515245u + 12345u;
    return hm_answer_seed ^ (hm_answer_seed >> 13);
}
static uint8_t hm_last_visible;
/* One stub body for both sides: logs the entry registers and arguments,
 * returns seeded EAX, EDX and ECX in out[3]. */
static void hm_stub(char letter, const uint32_t in[3], const uint32_t *s, uint32_t out[3]) {
    unsigned n = letter == 'C' ? 3u : 1u;
    hm_note(letter, in, s, n);
    out[0] = hm_answer();
    out[1] = hm_answer();
    out[2] = hm_answer();
    if (letter == 'q') {
        /* panel_visible: AL zero in a third of the calls */
        if (hm_answer() % 3u == 0) out[0] &= 0xffffff00u;
        else if (!(out[0] & 0xffu)) out[0] |= 1u;
        hm_last_visible = (uint8_t)out[0];
    }
}

/* ---- the original side's stubs: naked entries that keep the registers */
uint32_t hm_reg_in[3], hm_reg_out[3];
void __cdecl hm_stub_c(uint32_t letter, const uint32_t *args) { hm_stub((char)letter, hm_reg_in, args, hm_reg_out); }
#define HM_STUB(name, letter)                                                                                                      \
    extern void name(void);                                                                                                        \
    __asm__(".text\n.globl _" #name "\n_" #name ":\n\tmovl %eax, _hm_reg_in\n\tmovl %edx, _hm_reg_in+4\n\tmovl %ecx, _hm_reg_in+8\n" \
            "\tleal 4(%esp), %eax\n\tpushl %eax\n\tpushl $" #letter "\n\tcall _hm_stub_c\n\taddl $8, %esp\n"                     \
            "\tmovl _hm_reg_out, %eax\n\tmovl _hm_reg_out+4, %edx\n\tmovl _hm_reg_out+8, %ecx\n\tret\n");
HM_STUB(hm_o_visible, 113)  /* 'q' */
HM_STUB(hm_o_copy, 67)      /* 'C' */
HM_STUB(hm_slot_draw, 100)  /* 'd': slot 0 */
HM_STUB(hm_slot_lamp, 108)  /* 'l': slot +4 */

static int hm_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("hd-11: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("hd-11: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static const struct { uint32_t va; void *stub; char letter; unsigned slots; int row; } hm_patched[] = {
    {0x00431410u, (void *)hm_o_visible, 'q', 1, 0}, {0x0043122cu, (void *)hm_o_copy, 'C', 3, 1},
};
#define HM_PATCHED (sizeof(hm_patched) / sizeof(hm_patched[0]))
static BYTE hm_saved[HM_PATCHED][5];

/* ---- the own side */
static void hm_s_stub(es2_sim_service_call *call, void *user) {
    const uint32_t in[3] = {call->eax, call->edx, call->ecx};
    uint32_t out[3];
    hm_stub((char)(uintptr_t)user, in, call->slots, out);
    call->eax = out[0]; call->edx = out[1]; call->ecx = out[2];
}
static void hm_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own hd-11: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    hm_bad = 1;
}
static int hm_dispatch_setup(void) {
    es2_sim_dispatch_init(&hm_dispatch, hm_entries, 4096, hm_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&hm_dispatch, &s)) return fail("hd-11: dispatch");
    for (unsigned i = 0; i < HM_PATCHED; ++i)
        if (!hm_patched[i].row &&
            es2_sim_dispatch_add_service(&hm_dispatch, hm_patched[i].va, "hd-11 stub", hm_s_stub, (void *)(uintptr_t)hm_patched[i].letter, 0,
                                         (uint8_t)hm_patched[i].slots))
            return fail("hd-11: dispatch (stub)");
    const struct { void *host; char letter; } hosts[] = {{(void *)hm_slot_draw, 'd'}, {(void *)hm_slot_lamp, 'l'}};
    for (unsigned i = 0; i < sizeof(hosts) / sizeof(hosts[0]); ++i)
        if (es2_sim_dispatch_add_service(&hm_dispatch, (uint32_t)(uintptr_t)hosts[i].host, "hd-11 slot", hm_s_stub,
                                         (void *)(uintptr_t)hosts[i].letter, 0, 1))
            return fail("hd-11: dispatch (slot)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&hm_dispatch, &error, &bad)) return fail("hd-11: dispatch seal");
    /* Rows of other packages: replaced by the recording stubs after the seal. */
    for (unsigned i = 0; i < HM_PATCHED; ++i) {
        if (!hm_patched[i].row) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&hm_dispatch, hm_patched[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("hd-11: service missing");
        e->service = hm_s_stub;
        e->service_user = (void *)(uintptr_t)hm_patched[i].letter;
    }
    return 0;
}

/* ---- the synthetic world */
static uint32_t hm_seed;
static uint32_t hm_rand(uint32_t n) {
    hm_seed = hm_seed * 1103515245u + 12345u;
    return (hm_seed >> 8) % n;
}
typedef struct hm_world {
    uint32_t map, hdd, buttons, button[HM_BUTTONS], icon[HM_ICONS], drag, vt;
} hm_world;
static hm_world hm_w, hm_w0;
static uint32_t hm_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
static void hm_fill(uint32_t a, uint32_t n) { for (uint32_t k = 0; k < n; ++k) hm_put8(a + k, (uint8_t)hm_rand(256u)); }
static uint8_t hm_flag(void) { uint32_t r = hm_rand(5u); return (uint8_t)(r < 2u ? 0u : r < 4u ? 1u : hm_rand(256u)); }
static void hm_build(void) {
    hm_world *w = &hm_w;
    memset(w, 0, sizeof(*w));
    w->vt = hm_alloc(0x10u);
    hm_put32(w->vt, (uint32_t)(uintptr_t)hm_slot_draw);
    hm_put32(w->vt + 4u, (uint32_t)(uintptr_t)hm_slot_lamp);
    w->hdd = hm_alloc(0x120u);
    hm_fill(w->hdd, 0x120u);
    w->map = hm_alloc(0x160u);
    hm_fill(w->map, 0x160u);
    hm_put32(w->map + 8u, w->hdd);
    static const uint16_t counts[] = {0, 0, 1, 1, 2, 3, 0xffffu, 0x8000u};
    hm_put16(w->map + 0x13eu, counts[hm_rand(8u)]);
    w->buttons = hm_alloc(4u * HM_BUTTONS);
    for (unsigned j = 0; j < HM_BUTTONS; ++j) {
        uint32_t b = w->button[j] = hm_alloc(0x40u);
        hm_fill(b, 0x40u);
        hm_put32(b + 0x17u, w->vt);
        hm_put8(b + 0x37u, hm_flag());
        hm_put32(w->buttons + 4u * j, b);
    }
    hm_put32(w->map + 0x31u, w->buttons);
    put32(mapped + (0x0049d6b0u - 0x400000u), w->map);
    for (unsigned j = 0; j < HM_ICONS; ++j) {
        uint32_t i = w->icon[j] = hm_alloc(0x40u);
        hm_fill(i, 0x40u);
        hm_put32(i + 0x17u, w->vt);
        static const uint8_t states[] = {0, 1, 2, 2, 2, 3, 0x82u};
        hm_put8(i + 0x1fu, states[hm_rand(7u)]);
        hm_put8(i + 0x37u, hm_flag());
    }
    w->drag = hm_alloc(0x30u);
    hm_fill(w->drag, 0x30u);
    hm_put32(w->drag + 0x17u, w->vt);
    hm_put32(w->drag + 0x28u, w->map);
    for (uint32_t a = 0x0049d6acu; a < 0x0049d6afu; ++a) hm_put8(a, hm_flag());
    hm_put8(0x0049dbe0u, (uint8_t)hm_rand(256u));
    for (uint32_t k = 0; k < 8u; ++k) hm_put8(0x004d1de3u + k, (uint8_t)hm_rand(256u));
    hm_put32(0x004d2556u, hm_rand(400u) - 200u);
    hm_put32(0x004d255au, hm_rand(400u) - 200u);
}

/* ---- the rows */
enum { HM_LAMP, HM_ON, HM_OFF, HM_TICK, HM_MARK, HM_FRAME, HM_RELEASE, HM_BLINK, HM_DRAG, HM_OPS };
static const struct { uint32_t va; unsigned n; const char *name; } hm_ops[HM_OPS] = {
    {0x0044ede0u, 2, "map lamp"}, {0x0044ee00u, 1, "buttons on"}, {0x0044ee24u, 1, "buttons off"},
    {0x0044d370u, 0, "map tick"}, {0x0044f5d0u, 1, "icon mark"}, {0x0044f5f4u, 1, "icon frame"},
    {0x0044f630u, 2, "icon release"}, {0x0044f644u, 1, "map blink"}, {0x0044f6d4u, 2, "drag press"},
};
typedef struct hm_op { unsigned op; uint32_t a[2], in[3]; } hm_op;
static uint32_t hm_flags(void) { return random32() % 3u ? random32() % 4u : random32(); }
static void hm_args(hm_op *o) {
    const hm_world *w = &hm_w;
    o->a[0] = o->a[1] = 0;
    switch (o->op) {
    case HM_LAMP: o->a[0] = w->map; o->a[1] = random32() % HM_BUTTONS; break;
    case HM_ON: case HM_OFF: o->a[0] = w->map; break;
    case HM_MARK: case HM_FRAME: o->a[0] = w->icon[random32() % HM_ICONS]; break;
    case HM_RELEASE: o->a[0] = w->icon[random32() % HM_ICONS]; o->a[1] = hm_flags(); break;
    case HM_BLINK: o->a[0] = random32() % 2u ? w->button[random32() % HM_BUTTONS] : w->icon[random32() % HM_ICONS]; break;
    case HM_DRAG: o->a[0] = w->drag; o->a[1] = hm_flags(); break;
    default: break;
    }
    for (unsigned k = 0; k < 3u; ++k) o->in[k] = random32();
}

/* ---- coverage (from the world before and after a call on the original side) */
enum { HMC_LAMP, HMC_ON, HMC_OFF, HMC_TICK_IDLE, HMC_TICK_COUNT, HMC_TICK_ZERO, HMC_MARK, HMC_FRAME_HIDDEN, HMC_FRAME_STATE,
       HMC_FRAME_DRAW, HMC_RELEASE_CLEAR, HMC_RELEASE_KEEP, HMC_BLINK_SKIP, HMC_BLINK_CALL, HMC_DRAG_SKIP, HMC_DRAG_START, HMC_COUNT };
static const char *const hm_cv_names[HMC_COUNT] = {
    "lamp", "buttons on", "buttons off", "tick idle", "tick count", "tick zero", "mark", "frame hidden", "frame state",
    "frame draw", "release clear", "release keep", "blink skip", "blink call", "drag skip", "drag start",
};
static unsigned hm_cv[HMC_COUNT];
typedef struct hm_pre { uint16_t count; uint8_t lock; unsigned letters[128]; } hm_pre;
static void hm_take_pre(const hm_op *o, hm_pre *p) {
    p->count = hm_get16(hm_w.map + 0x13eu);
    p->lock = o->a[0] ? hm_get8(o->a[0] + 0x37u) : 0;
    memcpy(p->letters, hm_letters[0], sizeof(p->letters));
}
static unsigned hm_new(const hm_pre *p, char letter) { return hm_letters[0][(unsigned)letter] - p->letters[(unsigned)letter]; }
static void hm_cover(const hm_op *o, const hm_pre *p) {
    switch (o->op) {
    case HM_LAMP: ++hm_cv[HMC_LAMP]; break;
    case HM_ON: ++hm_cv[HMC_ON]; break;
    case HM_OFF: ++hm_cv[HMC_OFF]; break;
    case HM_TICK: ++hm_cv[!p->count ? HMC_TICK_IDLE : p->count == 1u ? HMC_TICK_ZERO : HMC_TICK_COUNT]; break;
    case HM_MARK: ++hm_cv[HMC_MARK]; break;
    case HM_FRAME: ++hm_cv[!hm_last_visible ? HMC_FRAME_HIDDEN : hm_new(p, 'd') ? HMC_FRAME_DRAW : HMC_FRAME_STATE]; break;
    case HM_RELEASE: ++hm_cv[o->a[1] & 1u ? HMC_RELEASE_CLEAR : HMC_RELEASE_KEEP]; break;
    case HM_BLINK: ++hm_cv[p->lock ? HMC_BLINK_SKIP : HMC_BLINK_CALL]; break;
    case HM_DRAG: ++hm_cv[o->a[1] & 1u ? HMC_DRAG_START : HMC_DRAG_SKIP]; break;
    default: break;
    }
}

static int hm_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &hm_dispatch;
    hm_side = side;
    hm_hash[side] = 0xcbf29ce484222325ull;
    hm_count[side] = 0;
    memset(hm_letters[side], 0, sizeof(hm_letters[side]));
    hm_len[side] = 0;
    hm_text[side][0] = 0;
    hm_answer_seed = answers;
    hm_seed = seed;
    hm_build();
    if (side) {
        if (memcmp(&hm_w, &hm_w0, sizeof(hm_w))) return fail("hd-11: the sides' worlds differ (heap order)");
        es2_sim_hud_init(&hm_hud, &lc_loop);
        hm_hud.trace = hm_trace;
        hm_hud.trace_capacity = HM_TRACE;
        es2_sim_hud_bind(&hm_hud);
    } else {
        hm_w0 = hm_w;
    }
    return 0;
}
static unsigned hm_calls[HM_OPS];
static int hm_case(unsigned k) {
    unsigned ops = 1u + random32() % 8u;
    static hm_op list[8];
    uint32_t seed = random32(), answers = random32();
    static uint32_t reg[2][8][3];
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % HM_OPS;
    for (unsigned side = 0; side < 2u; ++side) {
        if (hm_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !hm_bad && !lc_bad; ++i) {
            hm_op *o = &list[i];
            hm_pre pre;
            if (side == 0) {
                hm_args(o);
                hm_take_pre(o, &pre);
                hm_last_visible = 0;
                hm_call((uint32_t)(uintptr_t)(mapped + (hm_ops[o->op].va - 0x400000u)), o->a, hm_ops[o->op].n, o->in, reg[0][i]);
                hm_cover(o, &pre);
                ++hm_calls[o->op];
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&hm_dispatch, hm_ops[o->op].va, o->a, hm_ops[o->op].n, o->in, &r)) hm_bad = 1;
                reg[1][i][0] = r.eax; reg[1][i][1] = r.edx; reg[1][i][2] = r.ecx;
            }
        }
        if (hm_bad || lc_bad) { fprintf(stderr, "FAIL: hd-11 case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (memcmp(reg[0][i], reg[1][i], sizeof(reg[0][i]))) {
            fprintf(stderr, "FAIL: hd-11 case %u: %s (call %u) EAX %08lx / %08lx, EDX %08lx / %08lx, ECX %08lx / %08lx\n", k,
                    hm_ops[list[i].op].name, i, (unsigned long)reg[0][i][0], (unsigned long)reg[1][i][0], (unsigned long)reg[0][i][1],
                    (unsigned long)reg[1][i][1], (unsigned long)reg[0][i][2], (unsigned long)reg[1][i][2]);
            return 1;
        }
    if (su_compare("hd-11", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", hm_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (hm_hash[0] != hm_hash[1] || hm_count[0] != hm_count[1]) {
        fprintf(stderr, "FAIL: hd-11 case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, hm_count[0], hm_count[1],
                hm_text[0], hm_text[1]);
        return 1;
    }
    if (hm_hud.trace_count) {
        fprintf(stderr, "FAIL: hd-11 case %u: coupling call of kind %u at 0x%08lx (none expected)\n", k, (unsigned)hm_trace[0].kind,
                (unsigned long)hm_trace[0].site);
        return 1;
    }
    return 0;
}

static int hdd_map_leaves_cases(void) {
    current_function = "hd-11";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("hd-11: memory");
    su_pristine_take();
    for (unsigned i = 0; i < HM_PATCHED; ++i)
        if (hm_patch(hm_patched[i].va, hm_patched[i].stub, hm_saved[i], 1)) return 1;
    if (hm_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 6000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = hm_case(k);
    }
    if (!bad) {
        printf("PASS: hd-11: %u cases identical\n", cases);
        printf("HD-11 rows called:");
        for (unsigned i = 0; i < HM_OPS; ++i) printf(" %s %u%s", hm_ops[i].name, hm_calls[i], i + 1u < HM_OPS ? "," : "\n");
        printf("HD-11 coverage:");
        for (unsigned i = 0; i < HMC_COUNT; ++i) printf(" %s %u%s", hm_cv_names[i], hm_cv[i], i + 1u < HMC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < HMC_COUNT; ++i)
            if (!hm_cv[i]) { fprintf(stderr, "FAIL: hd-11: path '%s' never taken\n", hm_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < HM_PATCHED; ++i) hm_patch(hm_patched[i].va, NULL, hm_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
