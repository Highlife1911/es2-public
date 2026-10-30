/* The presentation rest's rows and the palette path no suite had compared,
 * original machine code against the own rows (target :differential_presentation_loads_test).
 * It reuses startup_cases.h's snapshots and loop_cases.h's loop, as
 * joystick_prefs_cases.h does. Identifiers use pz_/PZ_.
 *
 * - load: the load wrappers 0x430f58 (row panel_load) and 0x430fb0 (row
 *   font_load), 1..4 calls per case with random paths (pointers into the
 *   case's world) and random entry EAX, EDX and ECX. The original side runs
 *   the wrapper, the frame record 0x49630c and the stream constructor
 *   0x472f14 as machine code; the own side is mission_load.h's
 *   es2_sim_service_load_by_name through the dispatch. The callees
 *   stream_open 0x4730e8, dts_load 0x47a144 and stream_destroy 0x472f64
 *   (hooks, no rows) are recording stubs on both sides: they log the
 *   stream's fields (vtable, status, handle word, the zeroed fields),
 *   whether the stream is the one opened, the path and the second
 *   argument; dts_load answers a random result. Compared: EAX and EDX (the
 *   fs:0 head at entry) of every call, DATA (the construction counter
 *   included), the stub log. ECX is not compared: the original leaves
 *   stream_destroy's, the own row the entry ECX; all three callers
 *   (0x431104, 0x431145, 0x454837) pop ECX right after the call.
 * - palette: the palette set 0x481fe0 (mission_load.h's row palette_set, which
 *   inlines 0x4820b7 -> 0x486e10 and the apply 0x465e28 with 0x465c40 and
 *   0x465a24) against the original's machine code with the USER/GDI imports
 *   stubbed. Random record P: range start -4..40, count 0..12 (and 0), the
 *   range source; 256 colours whose byte 3 selects the shifted copy (bit
 *   0), the raw copy (bit 1) or neither (entries below 200 only, see
 *   below), colour bytes from a small set so that the nudge of 0x465a24
 *   (+3 and -3) happens; DATA's palette entries 0x4d372c.. from the same
 *   set; GetSystemPaletteUse 2 or not, NUMCOLORS -1, 0, 1, 2, 20, 256 or
 *   random 0..512 (a negative answer other than -1 makes 0x465a24 read
 *   before its source table, in the original too). CreatePalette answers
 *   one nonzero handle (the own record's palette_handle);
 *   GetSystemPaletteEntries writes nothing (the headless answer).
 *   Compared: DATA (the LOGPALETTE 0x4d3728.., the handle 0x4d302c, the
 *   current palette 0x4d3b2c, the ranges at 0x4a4fa5), the colour table
 *   SetDIBColorTable received against the own record's color_table, and
 *   the USER/GDI call counts against the own record's counts.
 *   The local table of 0x465e28 keeps the stack's bytes for an entry with
 *  neither flag (the own code writes 0, presentation.md,
 *   "deviations"); the thunk zeroes 8 KiB below its stack pointer before each
 *   original call, so such an entry reads 0 unless an earlier callee's
 *   frame lies there
 *   (0x4820b7 and 0x486e10 use the top of that area: entries 232..255),
 *   hence neither flag only below entry 200. The loop of 0x465e28 never
 *   writes byte 3 of an entry (RGBQUAD's reserved byte), so that byte is
 *   the stack's for every entry: compared below entry 200 (0 on both
 *   sides), not above. Registers are not compared
 *   (the row leaves the entry registers; the original 0x465e28's; no
 *   caller reads them: after 0x43049a a pop, EDX loaded, EAX zeroed at
 *   0x4304c4; after 0x461722 a pop and 0x464c78, which writes them before
 *   reading). DirectDraw ([0x4d255e]) is 0.
 */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/load.h"

#define PZ_WORLD_SIZE 0x4000u
#define PZ_P 0x0000u          /* the palette record (0x20) */
#define PZ_RANGES 0x0100u     /* the range source: 16 pairs of dwords */
#define PZ_PATHS 0x0200u      /* 8 path strings of 0x20 */
#define PZ_COLOURS 0x1000u    /* 256 colours */

static es2_sim_entry pz_entries[4096];
static es2_sim_dispatch pz_dispatch;
static es2_sim_load pz_load;
static BYTE *pz_world;
static int pz_bad;
static unsigned pz_side;
static uint32_t pz_va(uint32_t off) { return (uint32_t)(uintptr_t)(pz_world + off); }
static uint32_t pz_r(uint32_t n) { return random32() % n; }

/* ---- the original side's register thunk: zeroes 8 KiB below its stack
 * pointer, pushes the arguments, enters with EAX/EDX/ECX from in[3],
 * stores EAX/EDX/ECX into out[3]. */
extern void pz_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _pz_call\n_pz_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tleal -0x2000(%esp), %edi\n\tmovl $0x800, %ecx\n\txorl %eax, %eax\n\tcld\n\trep stosl\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");
/* Commits the stack pages the thunk zeroes (once, before the first case). */
static __attribute__((noinline)) void pz_touch_stack(void) {
    volatile uint8_t area[0x4000];
    for (unsigned i = 0; i < sizeof(area); i += 0x100u) area[i] = 0;
}

/* ---- the stub log of a side */
static uint64_t pz_hash[2];
static unsigned pz_count[2];
static char pz_text[2][4096];
static size_t pz_len[2];
static void pz_note(char letter, const uint32_t v[6]) {
    uint64_t h = pz_hash[pz_side] ^ (uint32_t)letter;
    h *= 0x100000001b3ull;
    for (unsigned i = 0; i < 6u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    pz_hash[pz_side] = h;
    ++pz_count[pz_side];
    if (pz_len[pz_side] + 100u < sizeof(pz_text[0]))
        pz_len[pz_side] += (size_t)sprintf(pz_text[pz_side] + pz_len[pz_side], "%c(%lx,%lx,%lx,%lx,%lx,%lx) ", letter, (unsigned long)v[0],
                                           (unsigned long)v[1], (unsigned long)v[2], (unsigned long)v[3], (unsigned long)v[4],
                                           (unsigned long)v[5]);
}

/* ---- the load stubs' bodies, one for both sides */
static uint32_t pz_stream[2], pz_answer;
static uint32_t pz_g32(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static uint32_t pz_g16(uint32_t a) { return *(uint16_t *)(uintptr_t)a; }
static uint32_t pz_g8(uint32_t a) { return *(BYTE *)(uintptr_t)a; }
static void pz_open(uint32_t s, uint32_t path) {
    pz_stream[pz_side] = s;
    const uint32_t v[6] = {path, pz_g32(s), pz_g32(s + 4u), pz_g16(s + 8u) | pz_g8(s + 0x26u) << 16,
                           pz_g32(s + 0xau) ^ pz_g32(s + 0xeu) ^ pz_g32(s + 0x16u), pz_g32(s + 0x27u) ^ pz_g32(s + 0x2bu)};
    pz_note('o', v);
    /* what an open leaves: a handle and a status */
    put16((BYTE *)(uintptr_t)(s + 8u), (uint16_t)(pz_answer >> 8));
    put32((BYTE *)(uintptr_t)(s + 4u), pz_answer & 1u);
}
static uint32_t pz_dts(uint32_t s, uint32_t flags) {
    const uint32_t v[6] = {s == pz_stream[pz_side], flags, pz_g32(s), pz_g32(s + 4u), pz_g16(s + 8u), 0};
    pz_note('d', v);
    return pz_answer;
}
static void pz_destroy(uint32_t s, uint32_t flags) {
    const uint32_t v[6] = {s == pz_stream[pz_side], flags, pz_g32(s), pz_g32(s + 4u), pz_g16(s + 8u), 0};
    pz_note('k', v);
}
static uint32_t __cdecl pz_o_open(uint32_t s, uint32_t path) { pz_open(s, path); return 0x0badf00du; }
static uint32_t __cdecl pz_o_dts(uint32_t s, uint32_t flags) { return pz_dts(s, flags); }
static uint32_t __cdecl pz_o_destroy(uint32_t s, uint32_t flags) { pz_destroy(s, flags); return 0x0badf00du; }
static void pz_s_open(es2_sim_service_call *sc, void *user) { (void)user; pz_open(sc->slots[0], sc->slots[1]); sc->eax = 0x0badf00du; }
static void pz_s_dts(es2_sim_service_call *sc, void *user) { (void)user; sc->eax = pz_dts(sc->slots[0], sc->slots[1]); }
static void pz_s_destroy(es2_sim_service_call *sc, void *user) { (void)user; pz_destroy(sc->slots[0], sc->slots[1]); sc->eax = 0x0badf00du; }

static const struct { uint32_t va; void *stub; } pz_patches[3] = {
    {0x004730e8u, (void *)pz_o_open}, {0x0047a144u, (void *)pz_o_dts}, {0x00472f64u, (void *)pz_o_destroy},
};
static BYTE pz_saved[3][5];
static int pz_patch(unsigned i, int install) {
    BYTE *site = mapped + (pz_patches[i].va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("pr-02: stub protection");
    if (install) {
        memcpy(pz_saved[i], site, 5);
        site[0] = 0xe9;
        put32(site + 1, (uint32_t)(uintptr_t)pz_patches[i].stub - (uint32_t)(uintptr_t)(site + 5));
    } else memcpy(site, pz_saved[i], 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("pr-02: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}

/* ---- the original side's USER/GDI stubs */
#define PZ_DC 0x00020011u
#define PZ_HANDLE 0x0002c0deu
typedef struct pz_gdi {
    unsigned get_dc, release_dc, select_palette, realize_palette, create_palette, delete_object, color_table, palette_use,
        device_caps, palette_entries;
    uint8_t quads[1024];
} pz_gdi;
static pz_gdi pz_seen;
static uint32_t pz_palette_use;
static int32_t pz_num_colors;
static uint32_t __stdcall pz_get_dc(uint32_t w) { if (w) pz_bad = 1; ++pz_seen.get_dc; return PZ_DC; }
static uint32_t __stdcall pz_release_dc(uint32_t w, uint32_t dc) { if (w || dc != PZ_DC) pz_bad = 1; ++pz_seen.release_dc; return 1; }
static uint32_t __stdcall pz_select_palette(uint32_t dc, uint32_t palette, uint32_t background) {
    (void)palette;
    if (dc != PZ_DC || background) pz_bad = 1;
    ++pz_seen.select_palette;
    return 0x00020013u;
}
static uint32_t __stdcall pz_realize_palette(uint32_t dc) { if (dc != PZ_DC) pz_bad = 1; ++pz_seen.realize_palette; return 0; }
static uint32_t __stdcall pz_create_palette(const void *log) { (void)log; ++pz_seen.create_palette; return PZ_HANDLE; }
static uint32_t __stdcall pz_delete_object(uint32_t h) { (void)h; ++pz_seen.delete_object; return 1; }
static uint32_t __stdcall pz_palette_use_fn(uint32_t dc) { if (dc != PZ_DC) pz_bad = 1; ++pz_seen.palette_use; return pz_palette_use; }
static uint32_t __stdcall pz_device_caps(uint32_t dc, uint32_t index) {
    if (dc != PZ_DC || index != 24u) pz_bad = 1;
    ++pz_seen.device_caps;
    return (uint32_t)pz_num_colors;
}
static uint32_t __stdcall pz_palette_entries(uint32_t dc, uint32_t first, uint32_t n, void *out) {
    if (dc != PZ_DC || first || n != 256u || (uint32_t)(uintptr_t)out != 0x004d372cu) pz_bad = 1;
    ++pz_seen.palette_entries;
    return 0;   /* the headless answer: no system palette */
}
static uint32_t __stdcall pz_color_table(uint32_t dc, uint32_t first, uint32_t n, const uint8_t *quads) {
    if (dc != pz_g32(0x004d3034u) || first || n != 256u) pz_bad = 1;
    ++pz_seen.color_table;
    memcpy(pz_seen.quads, quads, sizeof(pz_seen.quads));
    return 256u;
}

/* ---- the own side */
static void pz_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own pr-02: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    pz_bad = 1;
}
static int pz_dispatch_setup(void) {
    es2_sim_dispatch_init(&pz_dispatch, pz_entries, 4096, pz_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&pz_dispatch, &s)) return fail("pr-02: dispatch");
    if (es2_sim_dispatch_add_service(&pz_dispatch, 0x004730e8u, "pr-02 stream_open stub", pz_s_open, NULL, 0, 2) ||
        es2_sim_dispatch_add_service(&pz_dispatch, 0x0047a144u, "pr-02 dts_load stub", pz_s_dts, NULL, 0, 2) ||
        es2_sim_dispatch_add_service(&pz_dispatch, 0x00472f64u, "pr-02 stream_destroy stub", pz_s_destroy, NULL, 0, 2))
        return fail("pr-02: dispatch (stubs)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&pz_dispatch, &error, &bad)) return fail("pr-02: dispatch seal");
    const uint32_t rows[] = {0x00430f58u, 0x00430fb0u, 0x00481fe0u};
    for (unsigned i = 0; i < 3u; ++i) {
        const es2_sim_entry *e = es2_sim_dispatch_find(&pz_dispatch, rows[i]);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("pr-02: a row is missing");
    }
    return 0;
}
static void pz_side_begin(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &pz_dispatch;
    pz_side = side;
    pz_hash[side] = 0xcbf29ce484222325ull;
    pz_count[side] = 0;
    pz_len[side] = 0;
    pz_text[side][0] = 0;
    pz_stream[side] = 0;
    memset(&pz_seen, 0, sizeof(pz_seen));
    if (side) {
        es2_sim_load_headless(&pz_load, &lc_loop);
        pz_load.platform.palette_use = pz_palette_use;
        pz_load.platform.num_colors = (uint32_t)pz_num_colors;
        pz_load.platform.palette_handle = PZ_HANDLE;
        es2_sim_load_bind(&pz_load);
    }
}

/* ---- coverage */
enum { PZC_PANEL, PZC_FONT, PZC_USE2, PZC_USE_OTHER, PZC_R0, PZC_R_WIDE, PZC_SHIFTED, PZC_RAW, PZC_NEITHER, PZC_UP, PZC_DOWN,
       PZC_RANGES, PZC_NO_RANGE, PZC_COUNT };
static const char *const pz_cv_names[PZC_COUNT] = {"panel load", "font load", "palette use 2", "palette use other", "NUMCOLORS/2 0",
                                                   "NUMCOLORS/2 > 0", "shifted copy", "raw copy", "neither flag", "nudge +3",
                                                   "nudge -3", "ranges", "no range"};
static unsigned pz_cv[PZC_COUNT];

/* ---- load */
static int pz_load_case(unsigned k) {
    unsigned n = 1u + pz_r(4u);
    uint32_t va[4], path[4], in[4][3], answer[4], reg[2][4][3];
    for (unsigned i = 0; i < n; ++i) {
        va[i] = pz_r(2u) ? 0x00430f58u : 0x00430fb0u;
        path[i] = pz_va(PZ_PATHS + 0x20u * pz_r(8u));
        for (unsigned j = 0; j < 3u; ++j) in[i][j] = random32();
        answer[i] = random32();
    }
    for (unsigned side = 0; side < 2u; ++side) {
        pz_side_begin(side);
        for (unsigned i = 0; i < n && !pz_bad && !lc_bad; ++i) {
            pz_answer = answer[i];
            if (!side) {
                pz_call((uint32_t)(uintptr_t)(mapped + (va[i] - 0x400000u)), &path[i], 1, in[i], reg[0][i]);
                ++pz_cv[va[i] == 0x00430f58u ? PZC_PANEL : PZC_FONT];
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&pz_dispatch, va[i], &path[i], 1, in[i], &r)) pz_bad = 1;
                reg[1][i][0] = r.eax; reg[1][i][1] = r.edx; reg[1][i][2] = r.ecx;
            }
        }
        if (pz_bad || lc_bad) { fprintf(stderr, "FAIL: pr-02 load case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < n; ++i)
        if (reg[0][i][0] != reg[1][i][0] || reg[0][i][1] != reg[1][i][1]) {
            fprintf(stderr, "FAIL: pr-02 load case %u: 0x%08lx (call %u) EAX %08lx / %08lx, EDX %08lx / %08lx\n", k, (unsigned long)va[i], i,
                    (unsigned long)reg[0][i][0], (unsigned long)reg[1][i][0], (unsigned long)reg[0][i][1], (unsigned long)reg[1][i][1]);
            return 1;
        }
    if (su_compare("pr-02 load", k)) return 1;
    if (pz_hash[0] != pz_hash[1] || pz_count[0] != pz_count[1]) {
        fprintf(stderr, "FAIL: pr-02 load case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, pz_count[0], pz_count[1],
                pz_text[0], pz_text[1]);
        return 1;
    }
    return 0;
}

/* ---- palette */
static uint8_t pz_byte(void) {
    static const uint8_t set[] = {0x00, 0x03, 0x10, 0x3f, 0xf9, 0xfc, 0xff};
    return pz_r(4u) ? set[pz_r(sizeof(set))] : (uint8_t)random32();
}
static uint8_t pz_local_blue[256];   /* local byte 0 of each entry: what 0x465a24 stores at entry + 2 before a nudge */
static uint8_t pz_kind[256];         /* 1 shifted, 2 raw, 0 neither */
static void pz_build_palette(void) {
    BYTE *w = pz_world;
    memset(w, 0, PZ_WORLD_SIZE);
    put16(w + PZ_P + 6u, (uint16_t)random32());
    put16(w + PZ_P + 0xeu, (uint16_t)(int16_t)((int32_t)pz_r(45u) - 4));
    put16(w + PZ_P + 0xcu, (uint16_t)(pz_r(3u) ? pz_r(13u) : 0u));
    put32(w + PZ_P + 8u, pz_va(PZ_COLOURS));
    put32(w + PZ_P + 0x10u, pz_va(PZ_RANGES));
    random_bytes(w + PZ_RANGES, 0x80u);
    int neither = pz_r(3u) == 0;
    for (uint32_t i = 0; i < 256u; ++i) {
        BYTE *e = w + PZ_COLOURS + 4u * i;
        e[0] = pz_byte(); e[1] = pz_byte(); e[2] = pz_byte();
        uint8_t flags = (uint8_t)random32();
        if ((flags & 3u) == 0 && !(neither && i < 200u)) flags |= (uint8_t)(1u + pz_r(2u));
        e[3] = flags;
        pz_kind[i] = flags & 1u ? 1u : flags & 2u ? 2u : 0u;
        pz_local_blue[i] = pz_kind[i] == 1u ? (uint8_t)(e[2] << 2) : pz_kind[i] == 2u ? e[2] : 0u;
    }
    uint32_t r = pz_r(8u);
    pz_palette_use = r < 3u ? 2u : r < 4u ? 1u : 0u;
    static const int32_t answers[] = {-1, 0, 1, 2, 20, 256};
    r = pz_r(8u);
    pz_num_colors = r < 6u ? answers[r] : (int32_t)pz_r(513u);
}
static void pz_build_data(void) {
    for (uint32_t i = 0; i < 0x400u; ++i) mapped[0x004d372cu - 0x400000u + i] = (i & 3u) == 3u ? (uint8_t)random32() : pz_byte();
    put32(mapped + (0x004d3034u - 0x400000u), random32() | 1u);
    put32(mapped + (0x004d302cu - 0x400000u), random32());
    put32(mapped + (0x004d3b2cu - 0x400000u), random32());
    put32(mapped + (0x004d255eu - 0x400000u), 0);
}
static void pz_cover_palette(void) {
    ++pz_cv[get16(pz_world + PZ_P + 0xcu) ? PZC_RANGES : PZC_NO_RANGE];
    for (uint32_t i = 0; i < 256u; ++i) ++pz_cv[pz_kind[i] == 1u ? PZC_SHIFTED : pz_kind[i] == 2u ? PZC_RAW : PZC_NEITHER];
    if (pz_palette_use == 2u) { ++pz_cv[PZC_USE2]; return; }
    ++pz_cv[PZC_USE_OTHER];
    int32_t r = pz_num_colors / 2;
    ++pz_cv[r ? PZC_R_WIDE : PZC_R0];
    for (int32_t i = r; i < 256 - r; ++i) {
        uint8_t v = mapped[0x004d372cu - 0x400000u + 4u * (uint32_t)i + 2u];
        if (v == (uint8_t)(pz_local_blue[i] + 3u)) ++pz_cv[PZC_UP];
        else if (v == (uint8_t)(pz_local_blue[i] - 3u)) ++pz_cv[PZC_DOWN];
    }
}
static int pz_palette_case(unsigned k) {
    pz_build_palette();
    uint32_t data_seed = random32(), in[3] = {random32(), random32(), random32()}, out[3];
    uint32_t p = pz_va(PZ_P);
    static pz_gdi seen[2];
    for (unsigned side = 0; side < 2u; ++side) {
        pz_side_begin(side);
        uint32_t keep = random_state;
        random_state = data_seed | 1u;
        pz_build_data();
        random_state = keep;
        if (!side) {
            pz_call((uint32_t)(uintptr_t)(mapped + (0x00481fe0u - 0x400000u)), &p, 1, in, out);
            pz_cover_palette();
            seen[0] = pz_seen;
        } else {
            es2_sim_result r;
            memset(&r, 0, sizeof(r));
            if (es2_sim_call(&pz_dispatch, 0x00481fe0u, &p, 1, in, &r)) pz_bad = 1;
            const es2_sim_load_platform *pl = &pz_load.platform;
            memset(&seen[1], 0, sizeof(seen[1]));
            seen[1].get_dc = pl->get_dc; seen[1].release_dc = pl->release_dc; seen[1].select_palette = pl->select_palette;
            seen[1].realize_palette = pl->realize_palette; seen[1].create_palette = pl->create_palette;
            seen[1].delete_object = pl->delete_object; seen[1].color_table = pl->color_table_uploads;
            seen[1].palette_use = pl->palette_use_calls; seen[1].device_caps = pl->device_caps;
            seen[1].palette_entries = pl->palette_entries;
            memcpy(seen[1].quads, pl->color_table, sizeof(seen[1].quads));
        }
        if (pz_bad || lc_bad) { fprintf(stderr, "FAIL: pr-02 palette case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    if (su_compare("pr-02 palette", k)) {
        fprintf(stderr, "  palette use %lu, NUMCOLORS %ld\n", (unsigned long)pz_palette_use, (long)pz_num_colors);
        return 1;
    }
    if (memcmp(&seen[0], &seen[1], offsetof(pz_gdi, quads))) {
        fprintf(stderr, "FAIL: pr-02 palette case %u: GDI calls differ (GetDC %u/%u, ReleaseDC %u/%u, SelectPalette %u/%u, RealizePalette "
                        "%u/%u, CreatePalette %u/%u, DeleteObject %u/%u, SetDIBColorTable %u/%u, GetSystemPaletteUse %u/%u, GetDeviceCaps "
                        "%u/%u, GetSystemPaletteEntries %u/%u)\n",
                k, seen[0].get_dc, seen[1].get_dc, seen[0].release_dc, seen[1].release_dc, seen[0].select_palette, seen[1].select_palette,
                seen[0].realize_palette, seen[1].realize_palette, seen[0].create_palette, seen[1].create_palette, seen[0].delete_object,
                seen[1].delete_object, seen[0].color_table, seen[1].color_table, seen[0].palette_use, seen[1].palette_use,
                seen[0].device_caps, seen[1].device_caps, seen[0].palette_entries, seen[1].palette_entries);
        return 1;
    }
    for (unsigned i = 0; i < 1024u; ++i)
        if (seen[0].quads[i] != seen[1].quads[i] && !((i & 3u) == 3u && i >= 4u * 200u)) {
            fprintf(stderr, "FAIL: pr-02 palette case %u: colour table byte %u: %02x / %02x (entry kind %u)\n", k, i, seen[0].quads[i],
                    seen[1].quads[i], pz_kind[i / 4u]);
            return 1;
        }
    return 0;
}

static int presentation_loads_cases(void) {
    current_function = "pr-02";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    pz_world = VirtualAlloc(NULL, PZ_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !pz_world) return fail("pr-02: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("pr-02: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"USER32.dll", "GetDC", (void *)pz_get_dc}, {"USER32.dll", "ReleaseDC", (void *)pz_release_dc},
        {"GDI32.dll", "SelectPalette", (void *)pz_select_palette}, {"GDI32.dll", "RealizePalette", (void *)pz_realize_palette},
        {"GDI32.dll", "CreatePalette", (void *)pz_create_palette}, {"GDI32.dll", "DeleteObject", (void *)pz_delete_object},
        {"GDI32.dll", "GetSystemPaletteUse", (void *)pz_palette_use_fn}, {"GDI32.dll", "GetDeviceCaps", (void *)pz_device_caps},
        {"GDI32.dll", "GetSystemPaletteEntries", (void *)pz_palette_entries}, {"GDI32.dll", "SetDIBColorTable", (void *)pz_color_table},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("pr-02: import slot not found");
        }
    for (unsigned i = 0; i < 3u; ++i)
        if (pz_patch(i, 1)) return 1;
    if (pz_dispatch_setup()) return 1;
    pz_touch_stack();
    for (uint32_t i = 0; i < 8u; ++i) sprintf((char *)pz_world + PZ_PATHS + 0x20u * i, "data\\pr02_%u.dts", (unsigned)i);
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"load", pz_load_case, 600}, {"palette", pz_palette_case, 1400},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: pr-02 %s: %u cases identical\n", parts[p].name, parts[p].cases);
        if (p == 0 && !bad)   /* the palette part's world is rebuilt per case */
            for (uint32_t i = 0; i < 8u; ++i) if (strncmp((char *)pz_world + PZ_PATHS + 0x20u * i, "data\\pr02_", 10)) bad = fail("pr-02: path changed");
    }
    if (!bad) {
        printf("PR-02 coverage:");
        for (unsigned i = 0; i < PZC_COUNT; ++i) printf(" %s %u%s", pz_cv_names[i], pz_cv[i], i + 1u < PZC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < PZC_COUNT; ++i)
            if (!pz_cv[i]) { fprintf(stderr, "FAIL: pr-02: path '%s' never taken\n", pz_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < 3u; ++i) pz_patch(i, 0);
    es2_sim_load_bind(NULL);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
