/* The cockpit's pixel calls, original
 * machine code against sim/src/hud_display.c and hud_state.c: the view border
 * 0x42da08, the display set 0x42a1f0, the MFD toggle and push buttons' draw
 * slots 0x44750c / 0x447380 and the panel copy 0x43122c. It reuses
 * startup_cases.h's snapshots and loop_cases.h's loop, as hdd_map_leaves_cases.h does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same synthetic world from the same seeded generator: the GC [0x6c5f70],
 * the canvas context the push stub answers, the views V and V2 (the push
 * stub makes V2 current in a quarter of its calls), the display D with its
 * mode table, the view geometry 0x4d253e, 0x4d2542, 0x4cfa40..0x4cfa4c,
 * the resolution byte 0x4d2538 (0, 1, 2, 3), the split word 0x4d2680, four
 * MFD buttons with ids -1..14 and their labels, the id text table
 * 0x4d135c, the panel-copy gates (0x4d2566, [[0x4cf99c]+0x1c], 0x4d252a),
 * the shift words 0x4d253a / 0x4d253c, the scroll dwords 0x4d2556 /
 * 0x4d255a and the driver slots 0x4a57ec, 0x4a57f0, 0x4a5824, 0x4a5828
 * (host stubs). Then 1..8 of the 5 rows run on it, each with random entry
 * EAX, EDX and ECX. Every callee is a recording stub on both sides (the
 * fill 0x486704, the blits 0x4816a8 / 0x4817c8, the switch 0x429e60, the
 * view shape 0x429c2c, the panel draw 0x4316c0, the free 0x429de4, the
 * canvas push / pop 0x4311e0 / 0x431210, the blit 0x45c8bc, the label
 * 0x438920, the menu code 0x43f9c0, the tab mark 0x43fa5c, panel_visible
 * 0x431410, the scroll 0x487e60); each logs its arguments with the values
 * behind them (the rectangles, points and sizes, GC+0x208 / +0x20c /
 * +0x21c, the pair behind GC+0x22c and its distance to the rectangle, the
 * label's three text bytes, the scroll's region, the counter and the view
 * pages) and leaves seeded EAX, EDX and ECX. The dangling GC+0x22c (a
 * stack address on both sides) is logged as changed or not and put back
 * after every call. Compared: EAX of every call (EDX and ECX too for the
 * buttons and the panel copy), DATA 0x497000..0x6cc000 and the heap, the
 * stub log. Coverage: per-path counters from the original side; every one
 * must be non-zero. Modes below 0 are not generated (the original indexes
 * the mode table with them). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/hud.h"

#define HQ_BUTTONS 4u
static es2_sim_entry hq_entries[4096];
static es2_sim_dispatch hq_dispatch;
static es2_sim_hud hq_hud;
static es2_sim_hud_event hq_trace[64];
static int hq_bad;
static unsigned hq_side;

/* ---- the original side's register thunk: pushes the arguments, enters
 * with EAX/EDX/ECX from in[3], stores EAX/EDX/ECX into out[3]. */
extern void hq_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _hq_call\n_hq_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

static uint32_t hq_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static uint8_t hq_get8(uint32_t a) { return *(BYTE *)(uintptr_t)a; }
static uint16_t hq_get16(uint32_t a) { return (uint16_t)(hq_get8(a) | hq_get8(a + 1u) << 8); }
static void hq_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void hq_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static void hq_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }

/* ---- the stub log of a side */
static uint64_t hq_hash[2];
static unsigned hq_count[2], hq_letters[2][128];
static char hq_text[2][8192];
static size_t hq_len[2];
static void hq_note(char letter, const uint32_t *v, unsigned n) {
    uint64_t h = hq_hash[hq_side];
    h ^= (uint32_t)letter;
    h *= 0x100000001b3ull;
    for (unsigned i = 0; i < n; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    hq_hash[hq_side] = h;
    ++hq_count[hq_side];
    ++hq_letters[hq_side][(unsigned char)letter & 127u];
    if (hq_len[hq_side] + 16u * (n + 1u) < sizeof(hq_text[0])) {
        hq_len[hq_side] += (size_t)sprintf(hq_text[hq_side] + hq_len[hq_side], "%c(", letter);
        for (unsigned i = 0; i < n; ++i)
            hq_len[hq_side] += (size_t)sprintf(hq_text[hq_side] + hq_len[hq_side], "%s%lx", i ? "," : "", (unsigned long)v[i]);
        hq_len[hq_side] += (size_t)sprintf(hq_text[hq_side] + hq_len[hq_side], ") ");
    }
}

/* ---- the synthetic world */
typedef struct hq_world {
    uint32_t canvas, gc, gc22c, counter, v, v2, d, table, panel, button[HQ_BUTTONS], label[HQ_BUTTONS], owner[HQ_BUTTONS], disp, rect, point;
} hq_world;
static hq_world hq_w, hq_w0;

/* ---- the stubs' answers: a generator reset per side */
static uint32_t hq_answer_seed;
static uint32_t hq_answer(void) {
    hq_answer_seed = hq_answer_seed * 1103515245u + 12345u;
    return hq_answer_seed ^ (hq_answer_seed >> 13);
}
static uint8_t hq_last_visible;
static int hq_switched;
/* One stub body for both sides: logs the arguments and the values behind
 * them, returns seeded EAX, EDX and ECX in out[3]. */
static void hq_stub(char letter, const uint32_t *s, uint32_t out[3]) {
    uint32_t v[12], n = 0, gc = hq_get(0x006c5f70u), view = hq_get(0x006c5ed0u);
    switch (letter) {
    case 'F': {   /* fill 0x486704(rect) */
        uint32_t pair = hq_get(gc + 0x22cu);
        for (unsigned i = 0; i < 4u; ++i) v[n++] = hq_get(s[0] + 4u * i);
        v[n++] = hq_get(gc + 0x208u); v[n++] = hq_get(gc + 0x20cu); v[n++] = hq_get(gc + 0x21cu);
        v[n++] = pair - s[0];
        v[n++] = hq_get(pair); v[n++] = hq_get(pair + 4u);
        break;
    }
    case 'K':   /* blit 0x4816a8(bitmap, pt, flag) */
        v[n++] = s[0]; v[n++] = hq_get(s[1]); v[n++] = hq_get(s[1] + 4u); v[n++] = s[2]; v[n++] = hq_get(gc + 0x208u);
        break;
    case 'S':   /* stretch 0x4817c8(bitmap, pt, flag, size) */
        v[n++] = s[0]; v[n++] = hq_get(s[1]); v[n++] = hq_get(s[1] + 4u); v[n++] = s[2]; v[n++] = hq_get(s[3]); v[n++] = hq_get(s[3] + 4u);
        v[n++] = hq_get(gc + 0x208u); v[n++] = hq_get(gc + 0x21cu); v[n++] = s[3] - s[1];
        break;
    case 'w': case 'h': case 'r': case 'm': v[n++] = s[0]; v[n++] = s[1]; break;
    case 'p': case 'q': case 'd': case 'e': v[n++] = s[0]; break;
    case 'b': v[n++] = s[0]; v[n++] = s[1]; v[n++] = s[2]; v[n++] = hq_get(hq_w.canvas + 0x208u); break;
    case 'k': v[n++] = s[0]; v[n++] = s[1]; v[n++] = s[2]; break;
    case 'l':   /* label 0x438920(L, s): the three text bytes and the font field */
        v[n++] = s[0]; v[n++] = (uint32_t)hq_get8(s[1]) | (uint32_t)hq_get8(s[1] + 1u) << 8 | (uint32_t)hq_get8(s[1] + 2u) << 16;
        v[n++] = hq_get(s[0]);
        break;
    case 'c':   /* scroll 0x487e60(region, point) */
        for (unsigned i = 0; i < 5u; ++i) v[n++] = hq_get(s[0] + 4u * i);
        v[n++] = s[1]; v[n++] = hq_get(hq_get(0x006cbee4u));
        v[n++] = hq_get(view + 0x38u); v[n++] = hq_get(view + 0x3cu); v[n++] = hq_get(0x004a3686u); v[n++] = hq_get(0x004a368au);
        break;
    default: break;   /* 'u', 'o', 'L', 'U' */
    }
    hq_note(letter, v, n);
    out[0] = hq_answer();
    out[1] = hq_answer();
    out[2] = hq_answer();
    if (letter == 'u') {
        out[0] = hq_w.canvas;
        if (hq_answer() % 4u == 0) { hq_put32(0x006c5ed0u, hq_w.v2); hq_switched = 1; }
    }
    if (letter == 'q') {
        /* panel_visible: AL zero in a third of the calls */
        if (hq_answer() % 3u == 0) out[0] &= 0xffffff00u;
        else if (!(out[0] & 0xffu)) out[0] |= 1u;
        hq_last_visible = (uint8_t)out[0];
    }
}

/* ---- the original side's stubs: naked entries that keep the registers */
uint32_t hq_reg_out[3];
void __cdecl hq_stub_c(uint32_t letter, const uint32_t *args) { hq_stub((char)letter, args, hq_reg_out); }
#define HQ_STUB(name, letter)                                                                                                      \
    extern void name(void);                                                                                                        \
    __asm__(".text\n.globl _" #name "\n_" #name ":\n"                                                                              \
            "\tleal 4(%esp), %eax\n\tpushl %eax\n\tpushl $" #letter "\n\tcall _hq_stub_c\n\taddl $8, %esp\n"                     \
            "\tmovl _hq_reg_out, %eax\n\tmovl _hq_reg_out+4, %edx\n\tmovl _hq_reg_out+8, %ecx\n\tret\n");
HQ_STUB(hq_o_fill, 70)      /* 'F' */
HQ_STUB(hq_o_blit, 75)      /* 'K' */
HQ_STUB(hq_o_stretch, 83)   /* 'S' */
HQ_STUB(hq_o_switch, 119)   /* 'w' */
HQ_STUB(hq_o_shape, 104)    /* 'h' */
HQ_STUB(hq_o_panel, 112)    /* 'p' */
HQ_STUB(hq_o_free, 114)     /* 'r' */
HQ_STUB(hq_o_push, 117)     /* 'u' */
HQ_STUB(hq_o_pop, 111)      /* 'o' */
HQ_STUB(hq_o_blit_at, 98)   /* 'b' */
HQ_STUB(hq_o_label, 108)    /* 'l' */
HQ_STUB(hq_o_menu, 109)     /* 'm' */
HQ_STUB(hq_o_mark, 107)     /* 'k' */
HQ_STUB(hq_o_visible, 113)  /* 'q' */
HQ_STUB(hq_o_scroll, 99)    /* 'c' */
HQ_STUB(hq_drv_draw, 100)   /* 'd': [0x4a57ec] */
HQ_STUB(hq_drv_show, 101)   /* 'e': [0x4a57f0] */
HQ_STUB(hq_drv_lock, 76)    /* 'L': [0x4a5828] */
HQ_STUB(hq_drv_unlock, 85)  /* 'U': [0x4a5824] */

static int hq_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("hd-14: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("hd-14: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static const struct { uint32_t va; void *stub; char letter; unsigned slots; } hq_patched[] = {
    {0x00486704u, (void *)hq_o_fill, 'F', 1},     {0x004816a8u, (void *)hq_o_blit, 'K', 3},  {0x004817c8u, (void *)hq_o_stretch, 'S', 4},
    {0x00429e60u, (void *)hq_o_switch, 'w', 2},   {0x00429c2cu, (void *)hq_o_shape, 'h', 2}, {0x004316c0u, (void *)hq_o_panel, 'p', 1},
    {0x00429de4u, (void *)hq_o_free, 'r', 2},     {0x004311e0u, (void *)hq_o_push, 'u', 0},  {0x00431210u, (void *)hq_o_pop, 'o', 0},
    {0x0045c8bcu, (void *)hq_o_blit_at, 'b', 3},  {0x00438920u, (void *)hq_o_label, 'l', 2}, {0x0043f9c0u, (void *)hq_o_menu, 'm', 2},
    {0x0043fa5cu, (void *)hq_o_mark, 'k', 3},     {0x00431410u, (void *)hq_o_visible, 'q', 1}, {0x00487e60u, (void *)hq_o_scroll, 'c', 2},
};
#define HQ_PATCHED (sizeof(hq_patched) / sizeof(hq_patched[0]))
static BYTE hq_saved[HQ_PATCHED][5];

/* ---- the own side */
static void hq_s_stub(es2_sim_service_call *call, void *user) {
    uint32_t out[3];
    hq_stub((char)(uintptr_t)user, call->slots, out);
    call->eax = out[0]; call->edx = out[1]; call->ecx = out[2];
}
static void hq_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own hd-14: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    hq_bad = 1;
}
static int hq_has_row(uint32_t va) {
    for (unsigned i = 0; i < es2_sim_service_count(); ++i) {
        uint32_t row = 0;
        es2_sim_service_row(i, &row);
        if (row == va) return 1;
    }
    return 0;
}
static int hq_dispatch_setup(void) {
    es2_sim_dispatch_init(&hq_dispatch, hq_entries, 4096, hq_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&hq_dispatch, &s)) return fail("hd-14: dispatch");
    for (unsigned i = 0; i < HQ_PATCHED; ++i)
        if (!hq_has_row(hq_patched[i].va) &&
            es2_sim_dispatch_add_service(&hq_dispatch, hq_patched[i].va, "hd-14 stub", hq_s_stub, (void *)(uintptr_t)hq_patched[i].letter, 0,
                                         (uint8_t)hq_patched[i].slots))
            return fail("hd-14: dispatch (stub)");
    const struct { void *host; char letter; unsigned slots; } hosts[] = {
        {(void *)hq_drv_draw, 'd', 1}, {(void *)hq_drv_show, 'e', 1}, {(void *)hq_drv_lock, 'L', 0}, {(void *)hq_drv_unlock, 'U', 0}};
    for (unsigned i = 0; i < sizeof(hosts) / sizeof(hosts[0]); ++i)
        if (es2_sim_dispatch_add_service(&hq_dispatch, (uint32_t)(uintptr_t)hosts[i].host, "hd-14 driver slot", hq_s_stub,
                                         (void *)(uintptr_t)hosts[i].letter, 0, (uint8_t)hosts[i].slots))
            return fail("hd-14: dispatch (driver slot)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&hq_dispatch, &error, &bad)) return fail("hd-14: dispatch seal");
    /* Rows of the callees: replaced by the recording stubs after the seal. */
    for (unsigned i = 0; i < HQ_PATCHED; ++i) {
        if (!hq_has_row(hq_patched[i].va)) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&hq_dispatch, hq_patched[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("hd-14: service missing");
        e->service = hq_s_stub;
        e->service_user = (void *)(uintptr_t)hq_patched[i].letter;
    }
    return 0;
}

/* ---- the world builder */
static uint32_t hq_seed;
static uint32_t hq_rand(uint32_t n) {
    hq_seed = hq_seed * 1103515245u + 12345u;
    return (hq_seed >> 8) % n;
}
static uint32_t hq_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
static void hq_fill(uint32_t a, uint32_t n) { for (uint32_t k = 0; k < n; ++k) hq_put8(a + k, (uint8_t)hq_rand(256u)); }
/* A view-geometry value: near the border compares in a third of the draws. */
static uint32_t hq_geometry(void) { return hq_rand(3u) ? hq_rand(330u) : hq_rand(3u) - 1u; }
static void hq_build(void) {
    hq_world *w = &hq_w;
    memset(w, 0, sizeof(*w));
    w->canvas = hq_alloc(0x240u);
    hq_fill(w->canvas, 0x240u);
    w->gc = hq_alloc(0x240u);
    hq_fill(w->gc, 0x240u);
    w->gc22c = hq_get(w->gc + 0x22cu);
    hq_put32(0x006c5f70u, w->gc);
    w->counter = hq_alloc(4u);
    hq_put32(w->counter, hq_rand(100u));
    hq_put32(0x006cbee4u, w->counter);
    w->v = hq_alloc(0x100u);
    hq_fill(w->v, 0x100u);
    w->v2 = hq_alloc(0x100u);
    hq_fill(w->v2, 0x100u);
    hq_put32(0x006c5ed0u, w->v);
    hq_put32(0x004a57ecu, (uint32_t)(uintptr_t)hq_drv_draw);
    hq_put32(0x004a57f0u, (uint32_t)(uintptr_t)hq_drv_show);
    hq_put32(0x004a5828u, (uint32_t)(uintptr_t)hq_drv_lock);
    hq_put32(0x004a5824u, (uint32_t)(uintptr_t)hq_drv_unlock);
    hq_put32(0x004a3686u, hq_rand(0x10000u));
    hq_put32(0x004a368au, hq_rand(0x10000u));
    /* the display and its mode table (records +8 -> the bitmap holder) */
    w->d = hq_alloc(0x40u);
    hq_fill(w->d, 0x40u);
    w->table = hq_alloc(0x20u);
    for (uint32_t m = 0; m < 8u; ++m) {
        uint32_t rec = hq_alloc(0x10u), holder = hq_alloc(4u);
        hq_fill(rec, 0x10u);
        hq_fill(holder, 4u);
        hq_put32(rec + 8u, holder);
        hq_put32(w->table + 4u * m, rec);
    }
    hq_put32(w->d + 8u, w->table);
    hq_put32(w->d + 0x14u, hq_rand(6u));
    w->panel = hq_alloc(0x10u);
    hq_put32(w->d + 0x1fu, w->panel);
    static const uint8_t res[] = {0, 1, 2, 0, 1, 2, 3};
    hq_put8(0x004d2538u, res[hq_rand(7u)]);
    hq_put16(0x004d2680u, (uint16_t)(hq_rand(2u) ? 0u : 1u + hq_rand(0xffffu)));
    hq_put16(0x004d3ba2u, (uint16_t)hq_rand(0x10000u));
    hq_put32(0x004d253eu, hq_geometry());
    hq_put32(0x004d2542u, hq_geometry());
    for (uint32_t k = 0; k < 4u; ++k) hq_put32(0x004cfa40u + 4u * k, hq_geometry());
    /* the MFD buttons: ids -1..14, the state byte and the bitmap index */
    for (unsigned j = 0; j < HQ_BUTTONS; ++j) {
        uint32_t b = w->button[j] = hq_alloc(0x80u);
        hq_fill(b, 0x80u);
        hq_put32(b + 0x28u, hq_rand(16u) - 1u);
        hq_put8(b + 0x40u, (uint8_t)hq_rand(2u));
        hq_put16(b + 0x1bu, (uint16_t)hq_rand(2u));
        w->label[j] = hq_alloc(0x10u);
        hq_fill(w->label[j], 0x10u);
        hq_put32(b + 0x2cu, w->label[j]);
        w->owner[j] = hq_alloc(0x100u);
        hq_fill(w->owner[j], 0x100u);
        hq_put32(b + 0x24u, w->owner[j]);
    }
    for (uint32_t id = 0; id < 15u; ++id) {
        uint32_t t = hq_alloc(4u);
        hq_fill(t, 4u);
        hq_put32(0x004d135cu + 4u * id, t);
    }
    /* the panel copy */
    hq_put16(0x004d2566u, (uint16_t)(hq_rand(6u) ? 0u : 1u));
    w->disp = hq_alloc(0x40u);
    hq_fill(w->disp, 0x40u);
    hq_put8(w->disp + 0x1cu, (uint8_t)(hq_rand(6u) ? 0u : 1u + hq_rand(255u)));
    hq_put32(0x004cf99cu, w->disp);
    hq_put16(0x004d252au, (uint16_t)(hq_rand(3u) ? 0u : 1u));
    hq_put16(0x004d253au, (uint16_t)hq_rand(40u));
    hq_put16(0x004d253cu, (uint16_t)hq_rand(40u));
    hq_put32(0x004d2556u, hq_rand(400u) - 200u);
    hq_put32(0x004d255au, hq_rand(400u) - 200u);
    w->rect = hq_alloc(0x10u);
    hq_fill(w->rect, 0x10u);
    w->point = hq_alloc(8u);
    hq_fill(w->point, 8u);
}

/* ---- the rows */
enum { HQ_BORDER, HQ_SET, HQ_TOGGLE, HQ_PUSH, HQ_COPY, HQ_OPS };
static const struct { uint32_t va; unsigned n; const char *name; int regs; } hq_ops[HQ_OPS] = {
    {0x0042da08u, 1, "view border", 1}, {0x0042a1f0u, 2, "display set", 1}, {0x0044750cu, 1, "toggle draw", 3},
    {0x00447380u, 1, "push draw", 3},   {0x0043122cu, 3, "panel copy", 3},
};
typedef struct hq_op { unsigned op; uint32_t a[3], in[3]; } hq_op;
static void hq_args(hq_op *o) {
    const hq_world *w = &hq_w;
    o->a[0] = o->a[1] = o->a[2] = 0;
    switch (o->op) {
    case HQ_BORDER: o->a[0] = w->d; break;   /* the row's slot (callers push D; it is not read) */
    case HQ_SET: o->a[0] = w->d; o->a[1] = random32() % 7u; break;
    case HQ_TOGGLE: case HQ_PUSH: o->a[0] = w->button[random32() % HQ_BUTTONS]; break;
    case HQ_COPY: {
        static const uint32_t modes[] = {0, 1, 1, 2, 3};
        o->a[0] = random32() % 6u ? modes[random32() % 5u] : random32();
        o->a[1] = w->rect;
        o->a[2] = random32() % 3u ? w->point : 0;
        break;
    }
    default: break;
    }
    for (unsigned k = 0; k < 3u; ++k) o->in[k] = random32();
}

/* ---- coverage (from the world before a call on the original side) */
enum { HQC_TOP_FILL, HQC_TOP_SKIP, HQC_BOTTOM_FILL, HQC_BOTTOM_SKIP, HQC_LEFT_FILL, HQC_LEFT_SKIP, HQC_RIGHT_FILL, HQC_RIGHT_SKIP,
       HQC_SET_HIGH, HQC_SET_400_1, HQC_SET_4, HQC_SET_LOW, HQC_SET_400_0, HQC_SET_400_23, HQC_SET_640, HQC_SET_640_3, HQC_SET_SPLIT,
       HQC_TOGGLE_DIGIT, HQC_TOGGLE_FONT, HQC_TOGGLE_TEXT, HQC_TOGGLE_ON, HQC_PUSH_DIGIT, HQC_PUSH_ORDERS, HQC_PUSH_TEXT,
       HQC_COPY_PAGED, HQC_COPY_HIDDEN, HQC_COPY_SLIDING, HQC_COPY_LOCKED, HQC_COPY_SWAP, HQC_COPY_PLAIN, HQC_COPY_OTHER_VIEW,
       HQC_COPY_POINT0, HQC_COPY_SHIFT, HQC_COPY_SHIFT31, HQC_COUNT };
static const char *const hq_cv_names[HQC_COUNT] = {
    "top fill", "top skip", "bottom fill", "bottom skip", "left fill", "left skip", "right fill", "right skip",
    "set mode > 4", "set 400 mode 1", "set mode 4", "set low", "set 400 mode 0", "set 400 mode 2/3", "set 640x480", "set 640x480 mode 3", "set split",
    "toggle digit", "toggle font", "toggle text", "toggle on", "push digit", "push orders", "push text",
    "copy paged", "copy hidden", "copy sliding", "copy mode-1 lock", "copy swapped", "copy plain", "copy other view",
    "copy point 0", "copy shifted", "copy shift 31",
};
static unsigned hq_cv[HQC_COUNT];
typedef struct hq_pre { uint32_t w1, t1, b1, h1, l1, r1; int8_t res; uint16_t split, paged, lock, sx, sy; uint8_t sliding; } hq_pre;
static void hq_take_pre(hq_pre *p) {
    p->w1 = hq_get(0x004d253eu) - 1u; p->t1 = hq_get(0x004cfa44u) - 1u; p->b1 = hq_get(0x004cfa4cu) + 1u;
    p->h1 = hq_get(0x004d2542u) - 1u; p->l1 = hq_get(0x004cfa40u) - 1u; p->r1 = hq_get(0x004cfa48u) + 1u;
    p->res = (int8_t)hq_get8(0x004d2538u);
    p->split = hq_get16(0x004d2680u);
    p->paged = hq_get16(0x004d2566u);
    p->lock = hq_get16(0x004d252au);
    p->sx = hq_get16(0x004d253au);
    p->sy = hq_get16(0x004d253cu);
    p->sliding = hq_get8(hq_get(0x004cf99cu) + 0x1cu);
}
static void hq_cover(const hq_op *o, const hq_pre *p) {
    switch (o->op) {
    case HQ_BORDER:
        ++hq_cv[(int32_t)p->t1 >= 0 ? HQC_TOP_FILL : HQC_TOP_SKIP];
        ++hq_cv[(int32_t)p->h1 >= (int32_t)p->b1 ? HQC_BOTTOM_FILL : HQC_BOTTOM_SKIP];
        ++hq_cv[(int32_t)p->l1 >= 0 ? HQC_LEFT_FILL : HQC_LEFT_SKIP];
        ++hq_cv[(int32_t)p->w1 >= (int32_t)p->r1 ? HQC_RIGHT_FILL : HQC_RIGHT_SKIP];
        break;
    case HQ_SET: {
        uint32_t m = o->a[1];
        if ((int32_t)m > 4) { ++hq_cv[HQC_SET_HIGH]; break; }
        if (p->res == 1 && m == 1u) { ++hq_cv[HQC_SET_400_1]; break; }
        if (m == 4u) { ++hq_cv[HQC_SET_4]; break; }
        if (p->split) ++hq_cv[HQC_SET_SPLIT];
        if (p->res == 2) ++hq_cv[m == 3u ? HQC_SET_640_3 : HQC_SET_640];
        else if (p->res == 1) ++hq_cv[m == 0u ? HQC_SET_400_0 : HQC_SET_400_23];
        else ++hq_cv[HQC_SET_LOW];
        break;
    }
    case HQ_TOGGLE: case HQ_PUSH: {
        uint32_t id = hq_get(o->a[0] + 0x28u);
        if (o->op == HQ_TOGGLE) {
            if (id < 6u || id - 11u < 2u) ++hq_cv[HQC_TOGGLE_FONT];
            ++hq_cv[(int32_t)id < 6 ? HQC_TOGGLE_DIGIT : HQC_TOGGLE_TEXT];
            if (hq_get8(o->a[0] + 0x40u)) ++hq_cv[HQC_TOGGLE_ON];
        } else {
            ++hq_cv[(int32_t)id < 6 ? HQC_PUSH_DIGIT : HQC_PUSH_TEXT];
            if (id == 10u) ++hq_cv[HQC_PUSH_ORDERS];
        }
        break;
    }
    case HQ_COPY:
        if (p->paged) { ++hq_cv[HQC_COPY_PAGED]; break; }
        if (!hq_last_visible) { ++hq_cv[HQC_COPY_HIDDEN]; break; }
        if (p->sliding) { ++hq_cv[HQC_COPY_SLIDING]; break; }
        if (o->a[0] == 1u && p->lock) { ++hq_cv[HQC_COPY_LOCKED]; break; }
        ++hq_cv[o->a[0] == 1u ? HQC_COPY_SWAP : HQC_COPY_PLAIN];
        if (hq_switched) ++hq_cv[HQC_COPY_OTHER_VIEW];
        if (!o->a[2]) ++hq_cv[HQC_COPY_POINT0];
        if ((p->sx & 31u) && (p->sy & 31u)) ++hq_cv[HQC_COPY_SHIFT];
        if ((p->sx & 31u) == 31u || (p->sy & 31u) == 31u) ++hq_cv[HQC_COPY_SHIFT31];
        break;
    default: break;
    }
}

static int hq_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &hq_dispatch;
    hq_side = side;
    hq_hash[side] = 0xcbf29ce484222325ull;
    hq_count[side] = 0;
    memset(hq_letters[side], 0, sizeof(hq_letters[side]));
    hq_len[side] = 0;
    hq_text[side][0] = 0;
    hq_answer_seed = answers;
    hq_seed = seed;
    hq_build();
    if (side) {
        if (memcmp(&hq_w, &hq_w0, sizeof(hq_w))) return fail("hd-14: the sides' worlds differ (heap order)");
        es2_sim_hud_init(&hq_hud, &lc_loop);
        hq_hud.trace = hq_trace;
        hq_hud.trace_capacity = 64;
        es2_sim_hud_bind(&hq_hud);
    } else {
        hq_w0 = hq_w;
    }
    return 0;
}
/* The dangling GC+0x22c: logged as changed or not, then put back. */
static void hq_settle(void) {
    uint32_t gc = hq_get(0x006c5f70u), changed = hq_get(gc + 0x22cu) != hq_w.gc22c;
    hq_note('Z', &changed, 1);
    hq_put32(gc + 0x22cu, hq_w.gc22c);
}
static unsigned hq_calls[HQ_OPS];
static int hq_case(unsigned k) {
    unsigned ops = 1u + random32() % 8u;
    static hq_op list[8];
    uint32_t seed = random32(), answers = random32();
    static uint32_t reg[2][8][3];
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % HQ_OPS;
    for (unsigned side = 0; side < 2u; ++side) {
        if (hq_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !hq_bad && !lc_bad; ++i) {
            hq_op *o = &list[i];
            hq_pre pre;
            if (side == 0) {
                hq_args(o);
                hq_take_pre(&pre);
                hq_last_visible = 0;
                hq_switched = 0;
                hq_call((uint32_t)(uintptr_t)(mapped + (hq_ops[o->op].va - 0x400000u)), o->a, hq_ops[o->op].n, o->in, reg[0][i]);
                hq_cover(o, &pre);
                ++hq_calls[o->op];
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&hq_dispatch, hq_ops[o->op].va, o->a, hq_ops[o->op].n, o->in, &r)) hq_bad = 1;
                reg[1][i][0] = r.eax; reg[1][i][1] = r.edx; reg[1][i][2] = r.ecx;
            }
            hq_settle();
        }
        if (hq_bad || lc_bad) { fprintf(stderr, "FAIL: hd-14 case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (memcmp(reg[0][i], reg[1][i], sizeof(uint32_t) * (unsigned)hq_ops[list[i].op].regs)) {
            fprintf(stderr, "FAIL: hd-14 case %u: %s (call %u) EAX %08lx / %08lx, EDX %08lx / %08lx, ECX %08lx / %08lx\n", k,
                    hq_ops[list[i].op].name, i, (unsigned long)reg[0][i][0], (unsigned long)reg[1][i][0], (unsigned long)reg[0][i][1],
                    (unsigned long)reg[1][i][1], (unsigned long)reg[0][i][2], (unsigned long)reg[1][i][2]);
            return 1;
        }
    if (su_compare("hd-14", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", hq_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (hq_hash[0] != hq_hash[1] || hq_count[0] != hq_count[1]) {
        fprintf(stderr, "FAIL: hd-14 case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, hq_count[0], hq_count[1],
                hq_text[0], hq_text[1]);
        return 1;
    }
    if (hq_hud.trace_count) {
        fprintf(stderr, "FAIL: hd-14 case %u: coupling call of kind %u at 0x%08lx (none expected)\n", k, (unsigned)hq_trace[0].kind,
                (unsigned long)hq_trace[0].site);
        return 1;
    }
    return 0;
}

static int cockpit_pixels_cases(void) {
    current_function = "hd-14";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("hd-14: memory");
    su_pristine_take();
    for (unsigned i = 0; i < HQ_PATCHED; ++i)
        if (hq_patch(hq_patched[i].va, hq_patched[i].stub, hq_saved[i], 1)) return 1;
    if (hq_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 6000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = hq_case(k);
    }
    if (!bad) {
        printf("PASS: hd-14: %u cases identical\n", cases);
        printf("HD-14 rows called:");
        for (unsigned i = 0; i < HQ_OPS; ++i) printf(" %s %u%s", hq_ops[i].name, hq_calls[i], i + 1u < HQ_OPS ? "," : "\n");
        printf("HD-14 coverage:");
        for (unsigned i = 0; i < HQC_COUNT; ++i) printf(" %s %u%s", hq_cv_names[i], hq_cv[i], i + 1u < HQC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < HQC_COUNT; ++i)
            if (!hq_cv[i]) { fprintf(stderr, "FAIL: hd-14: path '%s' never taken\n", hq_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < HQ_PATCHED; ++i) hq_patch(hq_patched[i].va, NULL, hq_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
