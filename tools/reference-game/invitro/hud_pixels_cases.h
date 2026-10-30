/* The HUD's pixels. Ten of hud_load.h's full draws, whose fills, texts, blits and
 * save-unders call the graphics rows (the label 0x438920, the key label
 * 0x438aac, the system button 0x434748, the throttle sliders 0x4480c8/0x448660,
 * the power slider 0x442b60, the weapon toggle 0x4426e8 with the weapon button
 * 0x4423bc, the range toggle 0x442cb0, the HDD button 0x44bb60 and the HDD
 * 0x449a78), original machine code against sim/src/hud_draw.c. It reuses
 * startup_cases.h's snapshots and loop_cases.h's loop, as hdd_map_leaves_cases.h does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same synthetic world from the same seeded generator: two GCs (the current
 * one [0x6c5f70] and the label's [[0x6c5ed0]+0x44] - 4, often the same),
 * two fonts (fixed or table widths), labels with and without a background
 * colour, the objects of every row, the bitmap tables 0x49c4d0/0x49c4d4,
 * the text pointers 0x4d1344/0x4d134c/0x4d13b0/0x4d13b4, the blit
 * descriptor 0x4d24bc, the scale words 0x4d253a/0x4d253c, the 640x480
 * offsets and the canvas 0x4d03a8. Then 1..6 of the 11 rows run on it.
 *
 * Every callee the draws reach is a recording stub on both sides: the
 * graphics rows (0x486704, 0x482634, 0x482534, 0x4816a8, 0x4817c8,
 * 0x488012, 0x488143), panel_visible 0x431410 (a hook), the canvas
 * push/pop 0x4311e0/0x431210, label_in_rect 0x438884, the HDD title
 * 0x44a704 and the radio draw 0x44aea0, and the method-table and driver
 * slots (host stubs). A stub logs its arguments and what they point at:
 * the rectangle, point, save-under and text contents, and the colour record
 * GC+0x22c of both GCs points at (style and colour). A pointer into the
 * stack is logged as a tag, since the two sides' stacks differ; so are
 * GC+0x22c/+0x228 after a call when they are left pointing at a local.
 * Compared: the stub log, DATA 0x497000..0x6cc000 and the heap, no own
 * coupling call. Not compared: EAX/EDX/ECX (the callers of these slot draws
 * ignore them). Coverage: per-path counters from the original side; every
 * one must be non-zero. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/hud.h"

#define HX_TRACE 256u
static es2_sim_entry hx_entries[4096];
static es2_sim_dispatch hx_dispatch;
static es2_sim_hud hx_hud;
static es2_sim_hud_event hx_trace[HX_TRACE];
static int hx_bad;
static unsigned hx_side;

/* ---- the original side's call thunk: pushes the arguments, enters with
 * EAX/EDX/ECX from in[3]. */
extern void hx_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in);
__asm__(".text\n.globl _hx_call\n_hx_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

static uint32_t hx_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static uint8_t hx_get8(uint32_t a) { return *(BYTE *)(uintptr_t)a; }
static void hx_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void hx_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static void hx_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }

/* ---- a pointer into the running side's stack */
#define HX_STACK_TAG 0x5ac0005au
static int hx_in_stack(uint32_t a) {
    if (hx_side) return a >= (uint32_t)(uintptr_t)lc_stack && a < (uint32_t)(uintptr_t)lc_stack + 0x10000u;
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    return a >= (uint32_t)(uintptr_t)tib->StackLimit && a < (uint32_t)(uintptr_t)tib->StackBase;
}
static uint32_t hx_tag(uint32_t a) { return hx_in_stack(a) ? HX_STACK_TAG : a; }

/* ---- the stub log of a side */
#define HX_VALUES 24u
static uint64_t hx_hash[2];
static unsigned hx_count[2], hx_letters[2][128];
static char hx_text[2][8192];
static size_t hx_len[2];
static uint32_t hx_last_flag;
static void hx_note(char letter, const uint32_t *v, unsigned n) {
    uint64_t h = hx_hash[hx_side];
    h ^= (uint32_t)letter; h *= 0x100000001b3ull;
    for (unsigned i = 0; i < n; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    hx_hash[hx_side] = h;
    ++hx_count[hx_side];
    ++hx_letters[hx_side][(unsigned char)letter & 127u];
    if (hx_len[hx_side] + 16u * (n + 1u) < sizeof(hx_text[0])) {
        hx_len[hx_side] += (size_t)sprintf(hx_text[hx_side] + hx_len[hx_side], "%c(", letter);
        for (unsigned i = 0; i < n; ++i)
            hx_len[hx_side] += (size_t)sprintf(hx_text[hx_side] + hx_len[hx_side], "%lx%s", (unsigned long)v[i], i + 1u < n ? "," : "");
        hx_len[hx_side] += (size_t)sprintf(hx_text[hx_side] + hx_len[hx_side], ") ");
    }
}

/* ---- the world (pointers the stubs need) */
typedef struct hx_world {
    uint32_t gc, gc2, view, rec, font[2], label[4], text[6], keytext, canvas, sys, slider, power, bar, gauge, toggle, tgauge,
        range, rgauge, button, hdd, page, pvt, wvt, bvt, widget[3], holder, table, holder2, table2, bitmap[16];
} hx_world;
static hx_world hx_w, hx_w0;
static void hx_push(uint32_t *v, unsigned *n, uint32_t x) { if (*n < HX_VALUES) v[(*n)++] = x; }
/* the colour record GC+0x22c points at: tag and contents for a local or the world's record, else the raw pointer */
static void hx_record(uint32_t *v, unsigned *n, uint32_t gc) {
    uint32_t r = hx_get(gc + 0x22cu);
    if (hx_in_stack(r) || r == hx_w.rec) {
        hx_push(v, n, hx_tag(r));
        hx_push(v, n, hx_get(r));
        hx_push(v, n, hx_get(r + 4u));
    } else {
        hx_push(v, n, r);
    }
}
static void hx_words(uint32_t *v, unsigned *n, uint32_t p, unsigned k) {
    hx_push(v, n, hx_tag(p));
    for (unsigned i = 0; i < k; ++i) hx_push(v, n, hx_get(p + 4u * i));
}
static uint32_t hx_string_hash(uint32_t p) {
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < 64u; ++i) {
        uint8_t ch = hx_get8(p + i);
        h = (h ^ ch) * 16777619u;
        if (!ch) break;
    }
    return h;
}

/* ---- the stubs' answers: a generator reset per side */
static uint32_t hx_answer_seed;
static uint32_t hx_answer(void) {
    hx_answer_seed = hx_answer_seed * 1103515245u + 12345u;
    return hx_answer_seed ^ (hx_answer_seed >> 13);
}
/* One stub body for both sides: logs, returns seeded EAX, EDX and ECX. */
static void hx_stub(char letter, const uint32_t *s, uint32_t out[3]) {
    uint32_t v[HX_VALUES];
    unsigned n = 0;
    switch (letter) {
    case 'q': hx_push(v, &n, s[0]); break;
    case 'P': case 'p': break;
    case 'F': hx_words(v, &n, s[0], 4); hx_record(v, &n, hx_w.gc); hx_record(v, &n, hx_w.gc2); break;
    case 'S': hx_push(v, &n, s[0]); hx_words(v, &n, s[1], 2); hx_push(v, &n, hx_tag(s[2])); hx_push(v, &n, hx_string_hash(s[2])); break;
    case 'K': hx_push(v, &n, s[0]); hx_words(v, &n, s[1], 2); hx_push(v, &n, s[2]); break;
    case 'B': hx_push(v, &n, s[0]); hx_words(v, &n, s[1], 2); hx_push(v, &n, s[2]); hx_last_flag = s[2]; break;
    case 'Z': hx_push(v, &n, s[0]); hx_words(v, &n, s[1], 2); hx_push(v, &n, s[2]); hx_words(v, &n, s[3], 2); break;
    case 'V': hx_words(v, &n, s[0], 5); hx_push(v, &n, s[1]); break;
    case 'R': hx_words(v, &n, s[0], 5); hx_words(v, &n, s[1], 4); hx_push(v, &n, s[2]); break;
    case 'I': hx_push(v, &n, s[0]); hx_push(v, &n, hx_tag(s[1])); hx_words(v, &n, s[2], 4); hx_push(v, &n, s[3]); break;
    case 'T': hx_push(v, &n, s[0]); hx_record(v, &n, hx_w.gc); break;
    case 'D': hx_push(v, &n, s[0]); hx_push(v, &n, s[1]); hx_record(v, &n, hx_w.gc); break;
    case 'b': hx_push(v, &n, s[0]); hx_push(v, &n, s[1]); break;
    default: hx_push(v, &n, s[0]); break;  /* 'd', 'o', 'c', 'g': one argument */
    }
    hx_note(letter, v, n);
    out[0] = hx_answer();
    out[1] = hx_answer();
    out[2] = hx_answer();
    if (letter == 'q') {
        /* panel_visible: AL zero in a fifth of the calls */
        if (hx_answer() % 5u == 0) out[0] &= 0xffffff00u;
        else if (!(out[0] & 0xffu)) out[0] |= 1u;
    }
}

/* ---- the original side's stubs: entries that keep the registers */
uint32_t hx_reg_out[3];
void __cdecl hx_stub_c(uint32_t letter, const uint32_t *args) { hx_stub((char)letter, args, hx_reg_out); }
#define HX_STUB(name, letter)                                                                                         \
    extern void name(void);                                                                                           \
    __asm__(".text\n.globl _" #name "\n_" #name ":\n\tleal 4(%esp), %eax\n\tpushl %eax\n\tpushl $" #letter "\n"         \
            "\tcall _hx_stub_c\n\taddl $8, %esp\n"                                                                  \
            "\tmovl _hx_reg_out, %eax\n\tmovl _hx_reg_out+4, %edx\n\tmovl _hx_reg_out+8, %ecx\n\tret\n");
HX_STUB(hx_o_visible, 113)  /* 'q' */
HX_STUB(hx_o_push, 80)      /* 'P' */
HX_STUB(hx_o_pop, 112)      /* 'p' */
HX_STUB(hx_o_fill, 70)      /* 'F' */
HX_STUB(hx_o_string, 83)    /* 'S' */
HX_STUB(hx_o_char, 75)      /* 'K' */
HX_STUB(hx_o_blit, 66)      /* 'B' */
HX_STUB(hx_o_scaled, 90)    /* 'Z' */
HX_STUB(hx_o_save, 86)      /* 'V' */
HX_STUB(hx_o_restore, 82)   /* 'R' */
HX_STUB(hx_o_place, 73)     /* 'I' */
HX_STUB(hx_o_title, 84)     /* 'T' */
HX_STUB(hx_o_radio, 68)     /* 'D' */
HX_STUB(hx_slot_widget, 100) /* 'd': a widget's slot 0 */
HX_STUB(hx_slot_page, 111)   /* 'o': the HDD page's slot 0 */
HX_STUB(hx_slot_page_c, 99)  /* 'c': the HDD page's slot +0xc */
HX_STUB(hx_slot_bar, 98)     /* 'b': the power bar's slot 0 */
HX_STUB(hx_slot_driver, 103) /* 'g': the driver slot [0x4a57ec] */

static int hx_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("hud pixels: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("hud pixels: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static const struct { uint32_t va; void *stub; char letter; unsigned slots; int row; } hx_patched[] = {
    {0x00431410u, (void *)hx_o_visible, 'q', 1, 0}, {0x004311e0u, (void *)hx_o_push, 'P', 0, 1},
    {0x00431210u, (void *)hx_o_pop, 'p', 0, 1},     {0x00486704u, (void *)hx_o_fill, 'F', 1, 1},
    {0x00482634u, (void *)hx_o_string, 'S', 3, 1},  {0x00482534u, (void *)hx_o_char, 'K', 3, 1},
    {0x004816a8u, (void *)hx_o_blit, 'B', 3, 1},    {0x004817c8u, (void *)hx_o_scaled, 'Z', 4, 1},
    {0x00488012u, (void *)hx_o_save, 'V', 2, 1},    {0x00488143u, (void *)hx_o_restore, 'R', 3, 1},
    {0x00438884u, (void *)hx_o_place, 'I', 4, 1},   {0x0044a704u, (void *)hx_o_title, 'T', 1, 1},
    {0x0044aea0u, (void *)hx_o_radio, 'D', 2, 1},
};
#define HX_PATCHED (sizeof(hx_patched) / sizeof(hx_patched[0]))
static BYTE hx_saved[HX_PATCHED][5];
static const struct { void *host; char letter; unsigned slots; } hx_hosts[] = {
    {(void *)hx_slot_widget, 'd', 1}, {(void *)hx_slot_page, 'o', 1}, {(void *)hx_slot_page_c, 'c', 1},
    {(void *)hx_slot_bar, 'b', 2},    {(void *)hx_slot_driver, 'g', 1},
};
#define HX_HOSTS (sizeof(hx_hosts) / sizeof(hx_hosts[0]))

/* ---- the own side */
static void hx_s_stub(es2_sim_service_call *call, void *user) {
    uint32_t out[3];
    hx_stub((char)(uintptr_t)user, call->slots, out);
    call->eax = out[0]; call->edx = out[1]; call->ecx = out[2];
}
static void hx_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own hud pixels: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    hx_bad = 1;
}
static int __cdecl hx_sprintf(char *buffer, const char *format, ...) { (void)buffer; (void)format; return 0; }
static size_t __cdecl hx_strlen(const char *text) { return strlen(text); }
static int hx_dispatch_setup(void) {
    es2_sim_dispatch_init(&hx_dispatch, hx_entries, 4096, hx_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&hx_dispatch, &s)) return fail("hud pixels: dispatch");
    for (unsigned i = 0; i < HX_PATCHED; ++i)
        if (!hx_patched[i].row &&
            es2_sim_dispatch_add_service(&hx_dispatch, hx_patched[i].va, "hud pixels stub", hx_s_stub, (void *)(uintptr_t)hx_patched[i].letter, 0,
                                         (uint8_t)hx_patched[i].slots))
            return fail("hud pixels: dispatch (stub)");
    for (unsigned i = 0; i < HX_HOSTS; ++i)
        if (es2_sim_dispatch_add_service(&hx_dispatch, (uint32_t)(uintptr_t)hx_hosts[i].host, "hud pixels slot", hx_s_stub,
                                         (void *)(uintptr_t)hx_hosts[i].letter, 0, (uint8_t)hx_hosts[i].slots))
            return fail("hud pixels: dispatch (slot)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&hx_dispatch, &error, &bad)) return fail("hud pixels: dispatch seal");
    /* The graphics rows and the other packages' rows: replaced by the recording stubs after the seal. */
    for (unsigned i = 0; i < HX_PATCHED; ++i) {
        if (!hx_patched[i].row) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&hx_dispatch, hx_patched[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("hud pixels: service missing");
        e->service = hx_s_stub;
        e->service_user = (void *)(uintptr_t)hx_patched[i].letter;
    }
    return 0;
}

/* ---- the synthetic world */
static uint32_t hx_seed;
static uint32_t hx_rand(uint32_t n) {
    hx_seed = hx_seed * 1103515245u + 12345u;
    return (hx_seed >> 8) % n;
}
static uint32_t hx_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
static void hx_fill(uint32_t a, uint32_t n) { for (uint32_t k = 0; k < n; ++k) hx_put8(a + k, (uint8_t)hx_rand(256u)); }
static uint8_t hx_flag(void) { uint32_t r = hx_rand(5u); return (uint8_t)(r < 2u ? 0u : r < 4u ? 1u : hx_rand(256u)); }
static uint32_t hx_small(void) { return hx_rand(600u) - 300u; }
static void hx_rect(uint32_t a) { for (uint32_t k = 0; k < 4u; ++k) hx_put32(a + 4u * k, hx_small()); }
/* a text of 0..12 bytes, mostly printable */
static void hx_string(uint32_t a, unsigned max) {
    unsigned len = hx_rand(max + 1u);
    for (unsigned i = 0; i < len; ++i) hx_put8(a + i, (uint8_t)(hx_rand(8u) ? 0x20u + hx_rand(0x5fu) : 1u + hx_rand(255u)));
    hx_put8(a + len, 0);
}
static uint32_t hx_vtable(void *slot0, uint32_t at, void *slot) {
    uint32_t v = hx_alloc(0x10u);
    hx_put32(v, (uint32_t)(uintptr_t)slot0);
    if (slot) hx_put32(v + at, (uint32_t)(uintptr_t)slot);
    return v;
}
static uint32_t hx_anim(void) {
    if (hx_rand(3u)) return 0;
    uint32_t a = hx_alloc(4u);
    hx_put16(a, (uint16_t)hx_rand(2u));
    return a;
}
static void hx_label(uint32_t l) {
    hx_fill(l, 0x28u);
    hx_put32(l, hx_w.font[hx_rand(2u)]);
    hx_rect(l + 4u);
    hx_put16(l + 0x14u, (uint16_t)hx_small());
    hx_put16(l + 0x16u, (uint16_t)hx_small());
    hx_put8(l + 0x18u, hx_flag());
    hx_put8(l + 0x19u, (uint8_t)hx_rand(8u));
    hx_put32(l + 0x1du, hx_rand(3u) ? 0u : hx_rand(256u) + 1u);
    hx_put32(l + 0x21u, hx_w.font[hx_rand(2u)]);
}
static void hx_build(void) {
    hx_world *w = &hx_w;
    memset(w, 0, sizeof(*w));
    w->rec = hx_alloc(8u);
    hx_put32(w->rec, hx_rand(5u));
    hx_put32(w->rec + 4u, hx_rand(256u));
    w->gc = hx_alloc(0x240u);
    hx_fill(w->gc, 0x240u);
    w->gc2 = hx_rand(4u) ? w->gc : hx_alloc(0x240u);
    if (w->gc2 != w->gc) hx_fill(w->gc2, 0x240u);
    for (unsigned g = 0; g < 2u; ++g) {
        uint32_t gc = g ? w->gc2 : w->gc;
        hx_put32(gc + 0x22cu, w->rec);
        hx_put32(gc + 0x228u, w->rec);
        hx_put32(gc + 0x220u, hx_small());
        hx_put32(gc + 0x224u, hx_small());
    }
    put32(mapped + (0x006c5f70u - 0x400000u), w->gc);
    w->view = hx_alloc(0x60u);
    hx_fill(w->view, 0x60u);
    hx_put32(w->view + 0x44u, w->gc2 + 4u);
    put32(mapped + (0x006c5ed0u - 0x400000u), w->view);
    for (unsigned f = 0; f < 2u; ++f) {
        uint32_t font = w->font[f] = hx_alloc(0x40u), widths = hx_alloc(0x200u);
        hx_fill(font, 0x40u);
        for (uint32_t k = 0; k < 0x200u; ++k) hx_put8(widths + k, (uint8_t)hx_rand(9u));
        hx_put16(font + 8u, 0x20u);
        hx_put16(font + 0xcu, (uint16_t)(hx_rand(3u) ? 0xffffu : 1u + hx_rand(8u)));
        hx_put32(font + 0x2au, widths + 0x100u);
    }
    /* the label font table 0x49b0ac (the draws' "colours" are fonts): 0x49b0a8..0x49b0e7 */
    for (uint32_t a = 0x0049b0a8u; a < 0x0049b0e8u; a += 4u) hx_put32(a, w->font[hx_rand(2u)]);
    for (unsigned k = 0; k < 4u; ++k) hx_label(w->label[k] = hx_alloc(0x40u));
    for (unsigned k = 0; k < 6u; ++k) hx_string(w->text[k] = hx_alloc(0x18u), 12u);
    w->keytext = hx_alloc(0x18u);
    hx_fill(w->keytext, 0x18u);
    hx_string(w->keytext + 2u, 12u);
    hx_put32(0x004d1344u, w->text[2]);
    hx_put32(0x004d1350u, w->text[3]);
    hx_put32(0x004d1354u, w->text[4]);
    hx_put32(0x004d13b0u, w->text[5]);
    hx_put32(0x004d13b4u, w->text[1]);
    for (unsigned k = 0; k < 16u; ++k) { w->bitmap[k] = hx_alloc(0x10u); hx_fill(w->bitmap[k], 0x10u); }
    w->table = hx_alloc(0x40u);
    for (unsigned k = 0; k < 16u; ++k) hx_put32(w->table + 4u * k, hx_rand(6u) ? w->bitmap[k] : 0u);
    w->holder = hx_alloc(0x10u);
    hx_put32(w->holder + 8u, w->table);
    hx_put32(0x0049c4d0u, w->holder);
    w->table2 = hx_alloc(0x10u);
    hx_put32(w->table2, w->bitmap[hx_rand(16u)]);
    w->holder2 = hx_alloc(0x10u);
    hx_put32(w->holder2 + 8u, w->table2);
    hx_put32(0x0049c4d4u, w->holder2);
    /* the blit descriptor, scale words, offsets, canvas and driver slot */
    hx_put8(0x004d24bcu + 0x7bu, (uint8_t)(hx_rand(2u) ? 3u : hx_rand(5u)));
    hx_put8(0x004d24bcu + 0xb0u, (uint8_t)(hx_rand(3u) ? 0u : 1u));
    hx_put16(0x004d24bcu + 0x7eu, (uint16_t)hx_rand(3u));
    hx_put16(0x004d24bcu + 0x80u, (uint16_t)hx_rand(3u));
    hx_put32(0x004d24bcu + 0x9au, hx_small());
    hx_put32(0x004d24bcu + 0x9eu, hx_small());
    hx_put16(0x004d253au, (uint16_t)hx_rand(2u));
    hx_put16(0x004d253cu, (uint16_t)hx_rand(2u));
    hx_put32(0x004d2556u, hx_rand(3u) ? 0u : hx_small());
    hx_put32(0x004d255au, hx_rand(3u) ? 0u : hx_small());
    w->canvas = hx_alloc(0x230u);
    hx_fill(w->canvas, 0x230u);
    for (uint32_t k = 0; k < 4u; ++k) hx_put32(w->canvas + 0x210u + 4u * k, hx_small());
    hx_put32(0x004d03a8u, w->canvas);
    hx_put32(0x004a57ecu, (uint32_t)(uintptr_t)hx_slot_driver);
    w->wvt = hx_vtable((void *)hx_slot_widget, 0, NULL);
    /* the system button */
    w->sys = hx_alloc(0x60u);
    hx_fill(w->sys, 0x60u);
    hx_rect(w->sys);
    static const uint16_t states[] = {0, 0, 3, 3, 1, 2, 0x8000u};
    hx_put16(w->sys + 0x1bu, states[hx_rand(7u)]);
    static const uint32_t last[] = {0, 3, 1, 0xffffffffu};
    hx_put32(w->sys + 0x5cu, last[hx_rand(4u)]);
    /* the slider (both draws) */
    w->slider = hx_alloc(0x80u);
    hx_fill(w->slider, 0x80u);
    hx_put16(w->slider + 0x28u, (uint16_t)(hx_rand(700u) - 100u));
    hx_put16(w->slider + 0x2cu, (uint16_t)hx_rand(40u));
    hx_put32(w->slider + 0x2eu, hx_rand(700u) - 100u);
    /* the power slider */
    w->gauge = hx_alloc(0x100u);
    hx_fill(w->gauge, 0x100u);
    hx_put8(w->gauge + 0x76u, (uint8_t)(hx_rand(4u) ? 0u : 1u));
    hx_put32(w->gauge + 0x6cu, hx_anim());
    hx_put8(w->gauge + 0x8cu, (uint8_t)(hx_rand(4u) ? 1u : 0u));
    w->bvt = hx_vtable((void *)hx_slot_bar, 0, NULL);
    w->bar = hx_alloc(0x20u);
    hx_put32(w->bar, w->bvt);
    w->power = hx_alloc(0x80u);
    hx_fill(w->power, 0x80u);
    hx_put16(w->power + 0x1bu, (uint16_t)hx_rand(3u));
    hx_put32(w->power + 0x42u, w->gauge);
    hx_put32(w->power + 0x46u, w->bar);
    /* the weapon toggle and its gauge */
    w->tgauge = hx_alloc(0x100u);
    hx_fill(w->tgauge, 0x100u);
    hx_put8(w->tgauge + 0x76u, (uint8_t)(hx_rand(4u) ? 0u : 1u));
    hx_put32(w->tgauge + 0x6cu, hx_anim());
    hx_put8(w->tgauge + 0x8cu, (uint8_t)(hx_rand(5u) ? 1u : 0u));
    hx_put8(w->tgauge + 0xd3u, (uint8_t)(hx_rand(3u) ? 0u : 1u));
    hx_put8(w->tgauge + 0xd6u, hx_flag());
    hx_put8(w->tgauge + 0xd7u, hx_flag());
    hx_put8(w->tgauge + 0xd8u, hx_flag());
    hx_string(w->tgauge + 0xb1u, 8u);
    w->toggle = hx_alloc(0x80u);
    hx_fill(w->toggle, 0x80u);
    hx_rect(w->toggle);
    hx_put16(w->toggle + 0x1bu, (uint16_t)hx_rand(3u));
    hx_put32(w->toggle + 0x24u, w->tgauge);
    hx_put32(w->toggle + 0x2cu, w->label[0]);
    hx_put32(w->toggle + 0x40u, w->label[1]);
    hx_string(w->toggle + 0x44u, 1u);
    hx_rect(w->toggle + 0x46u);
    hx_rect(w->toggle + 0x56u);
    hx_put8(w->toggle + 0x66u, hx_flag());
    /* the range toggle */
    w->rgauge = hx_alloc(0x100u);
    hx_fill(w->rgauge, 0x100u);
    hx_put16(w->rgauge + 0xb1u, (uint16_t)hx_rand(4u));
    w->range = hx_alloc(0x60u);
    hx_fill(w->range, 0x60u);
    hx_rect(w->range);
    hx_put16(w->range + 0x1bu, (uint16_t)hx_rand(3u));
    hx_put32(w->range + 0x24u, w->rgauge);
    hx_put32(w->range + 0x28u, hx_rand(4u));
    hx_put8(w->range + 0x40u, (uint8_t)hx_rand(2u));
    hx_put32(w->range + 0x2cu, w->label[2]);
    /* the HDD and its button */
    w->hdd = hx_alloc(0x600u);
    hx_fill(w->hdd, 0x600u);
    uint32_t h = w->hdd;
    unsigned buttons = hx_rand(4u);
    hx_put32(h + 0x68u, buttons);
    for (unsigned k = 0; k < 3u; ++k) {
        w->widget[k] = hx_alloc(0x20u);
        hx_put32(w->widget[k] + 0x17u, w->wvt);
        hx_put32(h + 0x18u + 4u * k, w->widget[k]);
    }
    w->pvt = hx_alloc(0x10u);
    hx_put32(w->pvt, (uint32_t)(uintptr_t)hx_slot_page);
    hx_put32(w->pvt + 0xcu, (uint32_t)(uintptr_t)hx_slot_page_c);
    w->page = hx_alloc(0x20u);
    hx_put32(w->page, w->pvt);
    hx_put32(h + 0x109u, 1u);
    hx_put32(h + 0x105u, hx_rand(3u) ? w->page : 0u);
    hx_put8(h + 0x51fu, hx_flag());
    hx_rect(h + 0xf1u);
    for (unsigned k = 0; k < 3u; ++k) {
        uint32_t r = hx_alloc(0x10u);
        hx_rect(r);
        hx_put32(h + 0x40u + 4u * k, r);
        hx_put32(h + 0x12du + 0x14eu * k + 0x137u, hx_rand(3u) ? 0u : hx_rand(0x1000u) + 1u);
    }
    static const uint16_t words[] = {0, 0, 1, 1, 2, 0xffffu};
    hx_put16(h + 0x529u, words[hx_rand(6u)]);
    static const uint32_t records[] = {0, 1, 2, 0xffffffffu};
    hx_put32(h + 0x517u, records[hx_rand(4u)]);
    hx_put32(h + 0x52bu, records[hx_rand(4u)]);
    w->button = hx_alloc(0x60u);
    hx_fill(w->button, 0x60u);
    hx_rect(w->button);
    static const uint16_t bstates[] = {0, 0, 1, 1, 0xffffu, 2};
    hx_put16(w->button + 0x1bu, bstates[hx_rand(6u)]);
    hx_put32(w->button + 0x24u, h);
    hx_put32(w->button + 0x28u, hx_rand(8u) ? (hx_rand(3u) ? 10u + hx_rand(3u) : hx_rand(15u)) : hx_rand(0x20u));
    hx_put32(w->button + 0x2cu, w->label[3]);
    for (unsigned k = 0; k < 2u; ++k) hx_put32(w->button + 0x30u + 4u * k, w->bitmap[hx_rand(16u)]);
    hx_put8(w->button + 0x40u, (uint8_t)hx_rand(2u));
}

/* ---- the rows */
enum { HX_LABEL, HX_KEY, HX_SYS, HX_SLIDER_V, HX_SLIDER_H, HX_POWER, HX_TOGGLE, HX_RANGE, HX_BUTTON, HX_HDD, HX_WEAPON, HX_OPS };
static const struct { uint32_t va; unsigned n; const char *name; } hx_ops[HX_OPS] = {
    {0x00438920u, 2, "label"},       {0x00438aacu, 3, "key label"},    {0x00434748u, 1, "system button"},
    {0x004480c8u, 1, "slider v"},    {0x00448660u, 1, "slider h"},     {0x00442b60u, 1, "power slider"},
    {0x004426e8u, 1, "weapon toggle"}, {0x00442cb0u, 1, "range toggle"}, {0x0044bb60u, 1, "HDD button"},
    {0x00449a78u, 1, "HDD"},         {0x004423bcu, 1, "weapon button"},
};
typedef struct hx_op { unsigned op; uint32_t a[3], in[3]; } hx_op;
static void hx_args(hx_op *o) {
    const hx_world *w = &hx_w;
    o->a[0] = o->a[1] = o->a[2] = 0;
    switch (o->op) {
    case HX_LABEL: o->a[0] = w->label[random32() % 4u]; o->a[1] = w->text[random32() % 6u]; break;
    case HX_KEY: {
        static const uint32_t ns[] = {0, 1, 2, 3, 5, 8, 12, 0xffffu, 0x10000u};
        o->a[0] = w->label[random32() % 4u]; o->a[1] = w->keytext + 2u; o->a[2] = ns[random32() % 9u];
        break;
    }
    case HX_SYS: o->a[0] = w->sys; break;
    case HX_SLIDER_V: case HX_SLIDER_H: o->a[0] = w->slider; break;
    case HX_POWER: o->a[0] = w->power; break;
    case HX_TOGGLE: case HX_WEAPON: o->a[0] = w->toggle; break;
    case HX_RANGE: o->a[0] = w->range; break;
    case HX_BUTTON: o->a[0] = w->button; break;
    case HX_HDD: o->a[0] = w->hdd; break;
    default: break;
    }
    for (unsigned k = 0; k < 3u; ++k) o->in[k] = random32();
}
/* GC+0x22c/+0x228 left pointing at a local of the draw: tagged on both sides */
static void hx_settle(void) {
    for (unsigned g = 0; g < 2u; ++g) {
        uint32_t gc = g ? hx_w.gc2 : hx_w.gc;
        for (uint32_t off = 0x228u; off <= 0x22cu; off += 4u)
            if (hx_in_stack(hx_get(gc + off))) hx_put32(gc + off, HX_STACK_TAG);
    }
}

/* ---- coverage (from the stub letters of a call on the original side) */
enum { HXC_LABEL_FILL, HXC_LABEL_PLAIN, HXC_KEY_CHAR, HXC_KEY_SHORT, HXC_SYS_SAVE, HXC_SYS_FACE, HXC_SYS_RESTORE, HXC_SYS_NONE,
       HXC_SLIDER_FLIP, HXC_SLIDER_FLAT, HXC_SLIDER_HIDDEN, HXC_POWER_BAR, HXC_POWER_SKIP, HXC_TOGGLE_FILL, HXC_TOGGLE_JAM,
       HXC_TOGGLE_FRAME_ONLY, HXC_TOGGLE_OFF, HXC_RANGE_TEXT, HXC_RANGE_NONE, HXC_BUTTON_FILL, HXC_BUTTON_LABEL, HXC_BUTTON_FACE,
       HXC_BUTTON_NONE, HXC_HDD_RADIO, HXC_HDD_FILLS, HXC_HDD_PAGE, HXC_HDD_HIDDEN, HXC_WEAPON_LABEL, HXC_WEAPON_NONE, HXC_COUNT };
static const char *const hx_cv_names[HXC_COUNT] = {
    "label fill", "label plain", "key char", "key short", "system save", "system face", "system restore", "system none",
    "slider flip", "slider flat", "slider hidden", "power bar", "power skip", "toggle fill", "toggle jam", "toggle frame only",
    "toggle off", "range text", "range none", "button fill", "button label", "button face", "button none", "hdd radio",
    "hdd fills", "hdd page", "hdd hidden", "weapon label", "weapon none",
};
static unsigned hx_cv[HXC_COUNT];
static unsigned hx_before[128];
static unsigned hx_new(char letter) { return hx_letters[0][(unsigned)letter] - hx_before[(unsigned)letter]; }
static void hx_cover(const hx_op *o) {
    unsigned *cv = hx_cv;
    switch (o->op) {
    case HX_LABEL: ++cv[hx_new('F') ? HXC_LABEL_FILL : HXC_LABEL_PLAIN]; break;
    case HX_KEY: ++cv[hx_new('K') ? HXC_KEY_CHAR : HXC_KEY_SHORT]; break;
    case HX_SYS: ++cv[hx_new('V') ? HXC_SYS_SAVE : hx_new('R') ? HXC_SYS_RESTORE : hx_new('B') ? HXC_SYS_FACE : HXC_SYS_NONE]; break;
    case HX_SLIDER_V: case HX_SLIDER_H:
        ++cv[!hx_new('B') ? HXC_SLIDER_HIDDEN : hx_last_flag == 2u ? HXC_SLIDER_FLIP : HXC_SLIDER_FLAT];
        break;
    case HX_POWER: ++cv[hx_new('b') ? HXC_POWER_BAR : HXC_POWER_SKIP]; break;
    case HX_TOGGLE:
        ++cv[hx_new('I') ? HXC_TOGGLE_JAM : hx_new('F') ? HXC_TOGGLE_FILL : hx_new('B') + hx_new('Z') ? HXC_TOGGLE_FRAME_ONLY : HXC_TOGGLE_OFF];
        break;
    case HX_RANGE: ++cv[hx_new('S') ? HXC_RANGE_TEXT : HXC_RANGE_NONE]; break;
    case HX_BUTTON:
        ++cv[hx_new('F') ? HXC_BUTTON_FILL : hx_new('S') ? HXC_BUTTON_LABEL : hx_new('B') + hx_new('Z') ? HXC_BUTTON_FACE : HXC_BUTTON_NONE];
        break;
    case HX_HDD:
        if (!hx_new('T')) { ++cv[HXC_HDD_HIDDEN]; break; }
        if (hx_new('D')) ++cv[HXC_HDD_RADIO];
        if (hx_new('F') > 1u) ++cv[HXC_HDD_FILLS];
        if (hx_new('o')) ++cv[HXC_HDD_PAGE];
        break;
    case HX_WEAPON: ++cv[hx_new('S') ? HXC_WEAPON_LABEL : HXC_WEAPON_NONE]; break;
    default: break;
    }
}

static int hx_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &hx_dispatch;
    hx_side = side;
    hx_hash[side] = 0xcbf29ce484222325ull;
    hx_count[side] = 0;
    memset(hx_letters[side], 0, sizeof(hx_letters[side]));
    hx_len[side] = 0;
    hx_text[side][0] = 0;
    hx_answer_seed = answers;
    hx_seed = seed;
    hx_build();
    if (side) {
        if (memcmp(&hx_w, &hx_w0, sizeof(hx_w))) return fail("hud pixels: the sides' worlds differ (heap order)");
        es2_sim_hud_init(&hx_hud, &lc_loop);
        hx_hud.trace = hx_trace;
        hx_hud.trace_capacity = HX_TRACE;
        es2_sim_hud_bind(&hx_hud);
    } else {
        hx_w0 = hx_w;
    }
    return 0;
}
static unsigned hx_calls[HX_OPS];
static int hx_case(unsigned k) {
    unsigned ops = 1u + random32() % 6u;
    static hx_op list[6];
    uint32_t seed = random32(), answers = random32();
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % HX_OPS;
    for (unsigned side = 0; side < 2u; ++side) {
        if (hx_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !hx_bad && !lc_bad; ++i) {
            hx_op *o = &list[i];
            if (side == 0) {
                hx_args(o);
                memcpy(hx_before, hx_letters[0], sizeof(hx_before));
                hx_last_flag = 0xffffffffu;
                hx_call((uint32_t)(uintptr_t)(mapped + (hx_ops[o->op].va - 0x400000u)), o->a, hx_ops[o->op].n, o->in);
                hx_settle();
                hx_cover(o);
                ++hx_calls[o->op];
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&hx_dispatch, hx_ops[o->op].va, o->a, hx_ops[o->op].n, o->in, &r)) hx_bad = 1;
                hx_settle();
            }
        }
        if (hx_bad || lc_bad) { fprintf(stderr, "FAIL: hud pixels case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    if (hx_hash[0] != hx_hash[1] || hx_count[0] != hx_count[1]) {
        fprintf(stderr, "FAIL: hud pixels case %u: stub calls differ (%u / %u)\n  rows:", k, hx_count[0], hx_count[1]);
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", hx_ops[list[i].op].name);
        fprintf(stderr, "\n  original: %s\n  own:      %s\n", hx_text[0], hx_text[1]);
        return 1;
    }
    if (su_compare("hud pixels", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", hx_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (hx_hud.trace_count) {
        fprintf(stderr, "FAIL: hud pixels case %u: coupling call of kind %u at 0x%08lx (none expected)\n", k, (unsigned)hx_trace[0].kind,
                (unsigned long)hx_trace[0].site);
        return 1;
    }
    return 0;
}

static int hud_pixels_cases(void) {
    current_function = "hud pixels";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("hud pixels: memory");
    su_pristine_take();
    /* the glyph width 0x482498's sprintf has no lasting effect (gfx_cases.h) */
    if (!ir_patch_import("cw3220.dll", "_sprintf", (void *)hx_sprintf)) return fail("hud pixels: import slot not found");
    /* the key label's strlen (0x496766): the C library's, as the own side's RTL row */
    if (!ir_patch_import("cw3220.dll", "_strlen", (void *)hx_strlen)) return fail("hud pixels: import slot not found");
    for (unsigned i = 0; i < HX_PATCHED; ++i)
        if (hx_patch(hx_patched[i].va, hx_patched[i].stub, hx_saved[i], 1)) return 1;
    if (hx_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 6000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = hx_case(k);
    }
    if (!bad) {
        printf("PASS: hud pixels: %u cases identical\n", cases);
        printf("hud pixels rows called:");
        for (unsigned i = 0; i < HX_OPS; ++i) printf(" %s %u%s", hx_ops[i].name, hx_calls[i], i + 1u < HX_OPS ? "," : "\n");
        printf("hud pixels coverage:");
        for (unsigned i = 0; i < HXC_COUNT; ++i) printf(" %s %u%s", hx_cv_names[i], hx_cv[i], i + 1u < HXC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < HXC_COUNT; ++i)
            if (!hx_cv[i]) { fprintf(stderr, "FAIL: hud pixels: path '%s' never taken\n", hx_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < HX_PATCHED; ++i) hx_patch(hx_patched[i].va, NULL, hx_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
