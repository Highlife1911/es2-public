/* The rows the headless replay reached next (sim/dispatch/services/hud_mfd_leaves.h),
 * original machine code against the own rows: the stage-6 callback 0x401e5c
 * (row dump_flush, sim/src/loop.c), the callsign digit 0x434260 (row
 * callsign_digit, sim/src/hud_radio.c) and the display guard 0x4669b4 (row
 * display_restore, sim/src/loop.c); the part state 0x438624 and the HDD map's
 * target test 0x44f03c with its 0x44f0c4 (rows part_state, map_target,
 * map_inside, sim/src/mfd.c); the gauges' slot-0 methods 0x4414dc, 0x441a24 and
 * 0x443834 (rows *_gauge_event, sim/src/hud_gauges.c); the MFD radar range
 * 0x447014 and the HDD part bar 0x437e94 (rows range_set, part_bar,
 * sim/src/mfd.c); the lock sum 0x440370 and the status page's condition tail
 * 0x43b288 (rows target_locked, status_tail, sim/src/mfd.c); the HDD part
 * bitmaps 0x451de0 and the bitmap blit with z 0x45c964 (rows hdd_parts,
 * sim/src/mfd.c, and blit_at_z, sim/src/hud_draw.c); the HDD part bitmap
 * 0x438230 (row part_bitmap, sim/src/mfd.c); the weapon gauges' slot-0 method
 * 0x440f18 (hud_load.h's row weapon_gauge_event, sim/src/hud_gauges.c, which had
 * no suite). It reuses startup_cases.h's snapshots and loop_cases.h's loop, as
 * joystick_prefs_cases.h does.
 *
 * Both sides start from the pristine DATA and fill the same random state:
 * the -d flag 0x4d24de, the recording word 0x4d24d8, the playback word
 * 0x4d24d6, the name pointer 0x4d24da, the dump cursor 0x497078 and count
 * 0x49707c, and DirectDraw's full-screen dword 0x4d255e (each zero or not).
 * Then 1..8 calls of the three rows with random entry EAX, EDX and ECX; the
 * digit's argument is a small signed value or a random dword. The original
 * side calls the rows through an own thunk that loads and stores the three
 * registers. The callees are recording stubs on both sides: the path
 * builder 0x492b90 (mission_load.h's row), _fopen, _fwrite and _fclose (the
 * original's import slots; the own side's file_imports.h rows replaced after the
 * seal), the error reporter 0x479c78 and the display switch 0x4667c8 (rows
 * replaced after the seal); the view calls 0x48c2e4, 0x48c444, 0x48c57c,
 * 0x48c5fc and map_point_screen 0x44d24c (rows replaced), the hooks
 * group_toggle 0x4110ac, panel_weapon 0x432a50 and target_known 0x411c58
 * (stub services: no adapters here), the graphics driver's slots
 * 0x4a57bc (pixel read), 0x4a57c0 (pixel write) and 0x4a57ec (canvas), which
 * both sides point at the original side's stubs (the own side serves those
 * addresses with stub services). A stub logs its letter and arguments
 * (not the entry registers: the own side enters with zero registers; for a
 * pointer to the caller's locals the bytes it points at) and leaves seeded
 * EAX, EDX and ECX; _fopen answers NULL in a quarter of the calls; the three
 * point stubs fill their output (map_point_screen near the map's edges);
 * the pixel stubs log the point they are given (and the colour), the read
 * answers a, b, the s16 [0x4d3ba2], a small or a random value. The range
 * objects' method +0x60 (a stub at both sides' vtable slot) writes its word
 * output every time (the original's residue under it is never read) and
 * answers AX 5 in a third of the calls; label_draw 0x438920 and
 * damage_class 0x438700 are stubs (rows replaced), the label stub logs a
 * pointer into the image (or unreadable) as the value and other text (the
 * tail's local buffer) as its first 12 bytes; strlen, itoa and strcat (the original's import
 * slots; the own side's rtl_imports.h rows replaced) are stubs that do the work
 * through the own runtime and leave seeded EDX and ECX. The blits 0x4816a8
 * and 0x4817c8 are stubs (rows replaced) that log the bitmap, the point
 * they are given, z and the scaled size.
 * The gauges, the part record, the map's records, the bar's rectangle,
 * surface and port (the part bitmap shares them; its bitmaps, point and
 * pixel bytes follow), the MFD entries with their range objects and the tail's
 * label record live in a scratch area of DATA at 0x6b0000.
 * Compared: EAX, EDX and ECX of every call (not ECX where the original pops
 * an unwritten local: 0x45c964 without a bitmap, 0x451de0 without a drawn
 * entry), DATA 0x497000..0x6cc000 and the
 * heap, the stub log. Coverage: per-path counters; every one must be
 * non-zero. Identifiers use the prefix hx_/HX_. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"

static es2_sim_entry hx_entries[4096];
static es2_sim_dispatch hx_dispatch;
static int hx_bad;
static unsigned hx_side;

/* ---- the original side's register thunk: pushes the arguments, enters
 * with EAX/EDX/ECX from in[3], stores EAX/EDX/ECX into out[3]. */
extern void hx_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _hx_call\n_hx_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

/* ---- the stub log of a side */
static uint64_t hx_hash[2];
static unsigned hx_count[2];
static char hx_text[2][4096];
static size_t hx_len[2];
static void hx_note(char letter, const uint32_t *s, unsigned n) {
    uint64_t h = hx_hash[hx_side];
    uint32_t v[7] = {(uint32_t)letter, 0, 0, 0, 0, 0, 0};
    for (unsigned i = 0; i < n && i < 6u; ++i) v[1 + i] = s[i];
    for (unsigned i = 0; i < 7u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    hx_hash[hx_side] = h;
    ++hx_count[hx_side];
    if (hx_len[hx_side] + 100u < sizeof(hx_text[0]))
        hx_len[hx_side] += (size_t)sprintf(hx_text[hx_side] + hx_len[hx_side], "%c(%lx,%lx,%lx,%lx,%lx,%lx) ", letter,
                                           (unsigned long)v[1], (unsigned long)v[2], (unsigned long)v[3], (unsigned long)v[4],
                                           (unsigned long)v[5], (unsigned long)v[6]);
}
static void hx_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static void hx_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }
static uint32_t hx_get32(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }

/* ---- the stubs' bodies, one for both sides */
static uint32_t hx_answer_seed;
static uint32_t hx_answer(void) {
    hx_answer_seed = hx_answer_seed * 1103515245u + 12345u;
    return hx_answer_seed ^ (hx_answer_seed >> 13);
}
enum { HXC_NO_OPTION, HXC_NO_RECORD, HXC_FLUSH, HXC_OPEN_NULL, HXC_DIGIT_NEGATIVE, HXC_DIGIT_THREE, HXC_DIGIT_OTHER,
       HXC_WINDOWED, HXC_FULL_SCREEN, HXC_PART_PLAIN, HXC_PART_OWN, HXC_PART_OTHER, HXC_PART_LOW, HXC_PROJ_TOGGLE,
       HXC_PROJ_PANEL, HXC_PROJ_LATER, HXC_GAUGE_NONE, HXC_POD_TOGGLE, HXC_SHIELDS_0, HXC_SHIELDS_1, HXC_SHIELDS_LATER,
       HXC_MAP_SIDE, HXC_MAP_KNOWN, HXC_MAP_UNKNOWN, HXC_MAP_PASS, HXC_INSIDE, HXC_OUTSIDE, HXC_RANGE_SAME,
       HXC_RANGE_SET, HXC_BAR_NEGATIVE, HXC_BAR_EQUAL, HXC_BAR_OTHER_MODE, HXC_BAR_WRITE_0, HXC_BAR_WRITE_1,
       HXC_BAR_WRITE_2, HXC_BAR_WRITE_3, HXC_BAR_BLOCK, HXC_LOCK_NONE, HXC_LOCK_EMPTY, HXC_LOCK_FIVE, HXC_LOCK_ADD,
       HXC_TAIL_MARK, HXC_TAIL_MARK_FLAG, HXC_TAIL_CLASS, HXC_TAIL_GIVEN, HXC_TAIL_PERCENT, HXC_PARTS_NONE,
       HXC_PARTS_EMPTY, HXC_PARTS_DRAWN, HXC_BLIT_NULL, HXC_BLIT_PLAIN, HXC_BLIT_SCALED, HXC_PB_NEGATIVE, HXC_PB_EQUAL,
       HXC_PB_OTHER_MODE, HXC_PB_WRITE_0, HXC_PB_WRITE_1, HXC_PB_WRITE_2, HXC_PB_WRITE_3, HXC_PB_BLOCK,
       HXC_WEAPON_TOGGLE, HXC_WEAPON_HIGH_WORD, HXC_WEAPON_PANEL, HXC_WEAPON_LOW, HXC_WEAPON_MID, HXC_WEAPON_HIGH,
       HXC_WEAPON_LATER, HXC_WEAPON_NONE, HXC_COUNT };
static const char *const hx_cv_names[HXC_COUNT] = {"no -d", "not recording", "flush", "fopen NULL", "digit negative",
                                                   "digit 3 -> 4", "digit other", "guard windowed", "guard full screen",
                                                   "part kinds 0/2/3", "part kind 1", "part other kind", "part state 4",
                                                   "projectile toggle", "projectile panel", "projectile later",
                                                   "gauge no match", "pod toggle", "shields 0", "shields 1",
                                                   "shields later", "map side differs", "map target known",
                                                   "map target unknown", "map target passes", "map inside",
                                                   "map outside", "range same", "range set", "bar y below 0",
                                                   "bar b == a", "bar other mode", "bar write mode 0",
                                                   "bar write mode 1", "bar write mode 2", "bar write mode 3",
                                                   "bar block write", "lock no entries", "lock empty entry",
                                                   "lock answer 5", "lock adds", "tail marked", "tail marked with flag",
                                                   "tail class looked up", "tail class given", "tail percentage",
                                                   "parts none", "parts empty entry", "parts drawn", "blit no bitmap",
                                                   "blit plain", "blit scaled", "bitmap y below 0", "bitmap b == a",
                                                   "bitmap other mode", "bitmap write mode 0", "bitmap write mode 1",
                                                   "bitmap write mode 2", "bitmap write mode 3", "bitmap block write",
                                                   "weapon toggle", "weapon toggle, W's high word differs",
                                                   "weapon panel", "weapon slider <= 0", "weapon slider inside",
                                                   "weapon slider >= 0x400", "weapon later", "weapon no match"};
static unsigned hx_cv[HXC_COUNT];
static void hx_cover(unsigned c) { if (!hx_side) ++hx_cv[c]; }
static unsigned hx_nargs(char letter) {
    switch (letter) {
    case 'p': case 'o': case 'g': case 'n': case 'k': case 'v': return 2;
    case 'w': return 4;
    case 'c': case 's': case 'l': case 'e': case 'y': return 1;
    case 'f': return 6;
    default: return 0;
    }
}
/* The bar's or bitmap's arguments and steps (set before a part-bar or
 * part-bitmap call, both sides). */
static uint32_t hx_bar_a, hx_bar_b, hx_bar_mode, hx_bar_block, hx_bar_bitmap;
static void hx_stub(char letter, const uint32_t *s, uint32_t out[3]) {
    uint32_t v[6] = {0, 0, 0, 0, 0, 0}, rtl = 0;
    if (letter == 'r' || letter == 'u') {   /* pixel read(&pt) / write(&pt, colour): *pt (and the colour) */
        v[0] = hx_get32(s[0]);
        v[1] = hx_get32(s[0] + 4u);
        v[2] = letter == 'u' ? s[1] : 0u;
        hx_note(letter, v, letter == 'u' ? 3u : 2u);
        if (letter == 'u') {
            hx_cover((hx_bar_bitmap ? HXC_PB_WRITE_0 : HXC_BAR_WRITE_0) + (hx_bar_mode & 3u));
            if (hx_bar_block) hx_cover(hx_bar_bitmap ? HXC_PB_BLOCK : HXC_BAR_BLOCK);
        }
    } else if (letter == 'v') {            /* range method +0x60(Q, &w): Q; w <- a word */
        hx_note(letter, s, 1);
        hx_put16(s[1], (uint16_t)hx_answer());
    } else if (letter == 'b') {            /* label_draw(L, text): L, an image text as its value, other text's bytes */
        v[0] = s[0];
        MEMORY_BASIC_INFORMATION mi;
        if ((s[1] >= 0x00400000u && s[1] < 0x006d8000u) || !VirtualQuery((const void *)(uintptr_t)s[1], &mi, sizeof mi) ||
            mi.State != MEM_COMMIT || (uintptr_t)mi.BaseAddress + mi.RegionSize < (uintptr_t)s[1] + 12u) {
            v[1] = s[1];
        } else {
            const BYTE *t = (const BYTE *)(uintptr_t)s[1];
            int end = 0;
            for (unsigned i = 0; i < 12u; ++i) {
                if (!end && !t[i]) end = 1;
                if (!end) v[2u + i / 4u] |= (uint32_t)t[i] << (8u * (i % 4u));
            }
        }
        hx_note(letter, v, 5);
    } else if (letter == 'L' || letter == 'I' || letter == 'C') {   /* strlen, itoa, strcat: the own runtime */
        v[0] = s[0];
        v[1] = s[2];
        hx_note(letter, v, letter == 'I' ? 2u : 0u);
        uint32_t a[3] = {s[0], s[1], s[2]};
        rtl = su_rtl_call(letter == 'L' ? ES2_RTL_STRLEN : letter == 'I' ? ES2_RTL_ITOA : ES2_RTL_STRCAT, a);
    } else if (letter == 'B' || letter == 'S') {   /* blit(B, &pt, z) / scaled blit(B, &pt, z, &size): *pt, *size */
        v[0] = s[0];
        v[1] = hx_get32(s[1]);
        v[2] = hx_get32(s[1] + 4u);
        v[3] = s[2];
        if (letter == 'S') { v[4] = hx_get32(s[3]); v[5] = hx_get32(s[3] + 4u); }
        hx_note(letter, v, 6);
        hx_cover(letter == 'S' ? HXC_BLIT_SCALED : HXC_BLIT_PLAIN);
    } else if (letter == 't') {            /* view_transform(pos, t): pos and *pos; t <- 3 dwords */
        v[0] = s[0];
        for (unsigned i = 0; i < 3u; ++i) v[1 + i] = hx_get32(s[0] + 4u * i);
        hx_note(letter, v, 4);
        for (unsigned i = 0; i < 3u; ++i) hx_put32(s[1] + 4u * i, hx_answer());
    } else if (letter == 'j') {     /* project_point(t, s): *t; s <- 2 dwords */
        for (unsigned i = 0; i < 3u; ++i) v[i] = hx_get32(s[0] + 4u * i);
        hx_note(letter, v, 3);
        for (unsigned i = 0; i < 2u; ++i) hx_put32(s[1] + 4u * i, hx_answer());
    } else if (letter == 'm') {     /* map_point_screen(M, s, xy): M and *s; xy near the map's edges */
        v[0] = s[0];
        v[1] = hx_get32(s[1]);
        v[2] = hx_get32(s[1] + 4u);
        hx_note(letter, v, 3);
        for (unsigned i = 0; i < 2u; ++i) {
            uint32_t r = hx_answer();
            uint32_t edge = hx_get32(s[0] + (r & 1u ? 0x79u : 0x71u) + 4u * i);
            hx_put32(s[2] + 4u * i, edge + (r >> 8) % 3u - 1u);
        }
    } else {
        hx_note(letter, s, hx_nargs(letter));
    }
    out[0] = hx_answer();
    out[1] = hx_answer();
    out[2] = hx_answer();
    if (letter == 'o' && hx_answer() % 4u == 0) { out[0] = 0; hx_cover(HXC_OPEN_NULL); }
    if (letter == 'k') {
        if (hx_answer() % 2u) out[0] &= 0xffff0000u;
        hx_cover(out[0] & 0xffffu ? HXC_MAP_KNOWN : HXC_MAP_UNKNOWN);
    }
    if (letter == 'r') {
        uint32_t k = hx_answer() % 6u;
        out[0] = k == 0 ? hx_bar_a : k == 1 ? hx_bar_b : k == 2 ? (uint32_t)(int32_t)(int16_t)(uint16_t)hx_get32(0x004d3ba2u)
               : k == 3 ? hx_answer() % 8u : k == 4 ? hx_bar_a : hx_answer();
    }
    if (letter == 'L' || letter == 'I' || letter == 'C') out[0] = rtl;
    if (letter == 'v') {
        if (hx_answer() % 3u == 0) out[0] = (out[0] & 0xffff0000u) | 5u;
        hx_cover((uint16_t)out[0] == 5u ? HXC_LOCK_FIVE : HXC_LOCK_ADD);
    }
}

/* ---- the original side's stubs: naked entries that keep the registers */
uint32_t hx_reg_out[3];
void __cdecl hx_stub_c(uint32_t letter, const uint32_t *args) { hx_stub((char)letter, args, hx_reg_out); }
#define HX_STUB(name, letter)                                                                                     \
    extern void name(void);                                                                                       \
    __asm__(".text\n.globl _" #name "\n_" #name ":\n\tleal 4(%esp), %eax\n\tpushl %eax\n\tpushl $" #letter "\n" \
            "\tcall _hx_stub_c\n\taddl $8, %esp\n"                                                                \
            "\tmovl _hx_reg_out, %eax\n\tmovl _hx_reg_out+4, %edx\n\tmovl _hx_reg_out+8, %ecx\n\tret\n");
HX_STUB(hx_o_path, 112)     /* 'p' */
HX_STUB(hx_o_fopen, 111)    /* 'o' */
HX_STUB(hx_o_fwrite, 119)   /* 'w' */
HX_STUB(hx_o_fclose, 99)    /* 'c' */
HX_STUB(hx_o_fatal, 102)    /* 'f' */
HX_STUB(hx_o_switch, 100)   /* 'd' */
HX_STUB(hx_o_toggle, 103)   /* 'g' */
HX_STUB(hx_o_panel, 110)    /* 'n' */
HX_STUB(hx_o_known, 107)    /* 'k' */
HX_STUB(hx_o_setup, 115)    /* 's' */
HX_STUB(hx_o_select, 108)   /* 'l' */
HX_STUB(hx_o_transform, 116) /* 't' */
HX_STUB(hx_o_project, 106)  /* 'j' */
HX_STUB(hx_o_screen, 109)   /* 'm' */
HX_STUB(hx_o_pixel_read, 114)  /* 'r' */
HX_STUB(hx_o_pixel_write, 117) /* 'u' */
HX_STUB(hx_o_canvas, 101)   /* 'e' */
HX_STUB(hx_o_method, 118)   /* 'v' */
HX_STUB(hx_o_label, 98)     /* 'b' */
HX_STUB(hx_o_class, 121)    /* 'y' */
HX_STUB(hx_o_strlen, 76)    /* 'L' */
HX_STUB(hx_o_itoa, 73)      /* 'I' */
HX_STUB(hx_o_strcat, 67)    /* 'C' */
HX_STUB(hx_o_blit, 66)      /* 'B' */
HX_STUB(hx_o_scaled, 83)    /* 'S' */

/* Code patches of the original side: a jmp to the stub at the VA. */
#define HX_SITES 15u
static const struct { uint32_t va; void (*stub)(void); } hx_sites[HX_SITES] = {
    {0x00492b90u, hx_o_path},   {0x00479c78u, hx_o_fatal},     {0x004667c8u, hx_o_switch},  {0x004110acu, hx_o_toggle},
    {0x00432a50u, hx_o_panel},  {0x00411c58u, hx_o_known},     {0x0048c2e4u, hx_o_setup},   {0x0048c444u, hx_o_select},
    {0x0048c57cu, hx_o_transform}, {0x0048c5fcu, hx_o_project}, {0x0044d24cu, hx_o_screen},
    {0x00438920u, hx_o_label},  {0x00438700u, hx_o_class},  {0x004816a8u, hx_o_blit},    {0x004817c8u, hx_o_scaled},
};
static BYTE hx_saved[HX_SITES][5];
static int hx_patch(int install) {
    for (unsigned i = 0; i < HX_SITES; ++i) {
        BYTE *site = mapped + (hx_sites[i].va - 0x400000u);
        DWORD previous;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("hud mfd leaves: stub protection");
        if (install) {
            memcpy(hx_saved[i], site, 5);
            site[0] = 0xe9;
            put32(site + 1, (uint32_t)(uintptr_t)hx_sites[i].stub - (uint32_t)(uintptr_t)(site + 5));
        } else {
            memcpy(site, hx_saved[i], 5);
        }
        if (!VirtualProtect(site, 5, previous, &previous)) return fail("hud mfd leaves: stub restore protection");
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    return 0;
}

/* ---- the own side */
#define HX_OWN(name, letter)                                         \
    static void name(es2_sim_service_call *call, void *user) {       \
        (void)user;                                                  \
        uint32_t out[3];                                             \
        hx_stub(letter, call->slots, out);                           \
        call->eax = out[0]; call->edx = out[1]; call->ecx = out[2];  \
    }
HX_OWN(hx_s_path, 'p')
HX_OWN(hx_s_fopen, 'o')
HX_OWN(hx_s_fwrite, 'w')
HX_OWN(hx_s_fclose, 'c')
HX_OWN(hx_s_fatal, 'f')
HX_OWN(hx_s_switch, 'd')
HX_OWN(hx_s_toggle, 'g')
HX_OWN(hx_s_panel, 'n')
HX_OWN(hx_s_known, 'k')
HX_OWN(hx_s_setup, 's')
HX_OWN(hx_s_select, 'l')
HX_OWN(hx_s_transform, 't')
HX_OWN(hx_s_project, 'j')
HX_OWN(hx_s_screen, 'm')
HX_OWN(hx_s_pixel_read, 'r')
HX_OWN(hx_s_pixel_write, 'u')
HX_OWN(hx_s_canvas, 'e')
HX_OWN(hx_s_method, 'v')
HX_OWN(hx_s_label, 'b')
HX_OWN(hx_s_class, 'y')
HX_OWN(hx_s_strlen, 'L')
HX_OWN(hx_s_itoa, 'I')
HX_OWN(hx_s_strcat, 'C')
HX_OWN(hx_s_blit, 'B')
HX_OWN(hx_s_scaled, 'S')
static void hx_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own hud mfd leaves: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    hx_bad = 1;
}
static int hx_dispatch_setup(void) {
    es2_sim_dispatch_init(&hx_dispatch, hx_entries, 4096, hx_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&hx_dispatch, &s)) return fail("hud mfd leaves: dispatch");
    if (es2_sim_dispatch_add_service(&hx_dispatch, 0x004110acu, "hud mfd leaves group_toggle stub", hx_s_toggle, NULL, 0, 2) ||
        es2_sim_dispatch_add_service(&hx_dispatch, 0x00432a50u, "hud mfd leaves panel_weapon stub", hx_s_panel, NULL, 0, 2) ||
        es2_sim_dispatch_add_service(&hx_dispatch, 0x00411c58u, "hud mfd leaves target_known stub", hx_s_known, NULL, 0, 2) ||
        es2_sim_dispatch_add_service(&hx_dispatch, (uint32_t)(uintptr_t)hx_o_pixel_read, "hud mfd leaves pixel read stub",
                                     hx_s_pixel_read, NULL, 0, 1) ||
        es2_sim_dispatch_add_service(&hx_dispatch, (uint32_t)(uintptr_t)hx_o_pixel_write, "hud mfd leaves pixel write stub",
                                     hx_s_pixel_write, NULL, 0, 2) ||
        es2_sim_dispatch_add_service(&hx_dispatch, (uint32_t)(uintptr_t)hx_o_canvas, "hud mfd leaves canvas stub", hx_s_canvas,
                                     NULL, 0, 1) ||
        es2_sim_dispatch_add_service(&hx_dispatch, (uint32_t)(uintptr_t)hx_o_method, "hud mfd leaves range method stub",
                                     hx_s_method, NULL, 0, 2))
        return fail("hud mfd leaves: dispatch (stubs)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&hx_dispatch, &error, &bad)) return fail("hud mfd leaves: dispatch seal");
    es2_sim_entry *sw = (es2_sim_entry *)es2_sim_dispatch_find(&hx_dispatch, 0x004667c8u);
    if (!sw || sw->kind != ES2_SIM_SERVICE) return fail("hud mfd leaves: the switch row is missing");
    sw->service = hx_s_switch;
    sw->service_user = NULL;
    /* Other packages' rows: replaced by the recording stubs after the seal. */
    static const struct { uint32_t va; es2_sim_service_fn fn; } rows[17] = {
        {0x00492b90u, hx_s_path},  {0x00496802u, hx_s_fopen},  {0x00496832u, hx_s_fwrite},
        {0x00496838u, hx_s_fclose}, {0x0048c2e4u, hx_s_setup}, {0x0048c444u, hx_s_select},
        {0x0048c57cu, hx_s_transform}, {0x0048c5fcu, hx_s_project}, {0x0044d24cu, hx_s_screen},
        {0x00438920u, hx_s_label}, {0x00438700u, hx_s_class}, {0x00496766u, hx_s_strlen},
        {0x00496820u, hx_s_itoa}, {0x004967eau, hx_s_strcat}, {0x004816a8u, hx_s_blit},
        {0x004817c8u, hx_s_scaled}, {0x00479c78u, hx_s_fatal},
    };
    for (unsigned i = 0; i < 17u; ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&hx_dispatch, rows[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("hud mfd leaves: a callee row is missing");
        e->service = rows[i].fn;
        e->service_user = NULL;
    }
    return 0;
}

/* ---- the random state (DATA); the scratch area HX_X: the panel chain at
 * +0x400/+0x500, the gauge G, the part P and its out, the map M with its
 * camera at +0xa00, the actor A with its record at +0xe00; the bar's
 * rectangle R, surface S and port Q (its origins at +0x210/+0x220) */
#define HX_X 0x006b0000u
#define HX_G (HX_X + 0x000u)
#define HX_W0 (HX_X + 0x1000u)
#define HX_P (HX_X + 0x700u)
#define HX_OUT (HX_X + 0x780u)
#define HX_M (HX_X + 0x800u)
#define HX_A (HX_X + 0xc00u)
#define HX_R (HX_X + 0x1100u)
#define HX_S (HX_X + 0x1140u)
#define HX_Q (HX_X + 0x1200u)
#define HX_E (HX_X + 0x1300u)   /* MFD entries, 0x100 apart */
#define HX_RO (HX_X + 0x1800u)  /* range objects, 0x10 apart */
#define HX_V (HX_X + 0x1900u)   /* their vtable */
#define HX_STR (HX_X + 0x1a00u) /* the strlen text */
#define HX_T (HX_X + 0x1a40u)   /* the tail's label record */
#define HX_L (HX_X + 0x1b00u)   /* the HDD part list: count, entries */
#define HX_LE (HX_X + 0x1c00u)  /* its 0x22-byte entries */
#define HX_B (HX_X + 0x1e00u)   /* bitmaps, 0x10 apart */
#define HX_PT (HX_X + 0x1f00u)  /* the points */
#define HX_PB (HX_X + 0x2000u)  /* the part bitmap's bitmaps, 0x10 apart */
#define HX_PP (HX_X + 0x2040u)  /* its point */
#define HX_PD (HX_X + 0x2100u)  /* its pixel bytes (0x800) */
#define HX_WX 0x7f310000u       /* the widgets with another high word */
static uint32_t hx_seed;
static uint32_t hx_rand(uint32_t n) {
    hx_seed = hx_seed * 1103515245u + 12345u;
    return (hx_seed >> 8) % n;
}
static uint32_t hx_flag(void) { return hx_rand(3u) ? (hx_rand(2u) ? 1u : hx_rand(0x10000u) << 16 | hx_rand(0x10000u)) : 0u; }
static void hx_build(void) {
    hx_put32(0x004d24deu, hx_flag());
    hx_put16(0x004d24d8u, (uint16_t)(hx_rand(3u) ? 1u + hx_rand(0xffffu) : 0u));
    hx_put16(0x004d24d6u, (uint16_t)hx_rand(3u));
    hx_put32(0x004d24dau, hx_rand(0x10000u) << 16 | hx_rand(0x10000u));
    hx_put32(0x00497078u, hx_rand(2u) ? 0x004a84fcu + hx_rand(0x100u) : hx_rand(0x10000u) << 16 | hx_rand(0x10000u));
    hx_put32(0x0049707cu, hx_rand(2u) ? hx_rand(0x100u) : hx_rand(0x10000u) << 16 | hx_rand(0x10000u));
    hx_put32(0x004d255eu, hx_flag());
    /* the scratch records */
    for (uint32_t a = HX_X; a < HX_X + 0x1000u; a += 2u) hx_put16(a, (uint16_t)hx_rand(0x10000u));
    for (uint32_t a = 0x004d3b88u; a < 0x004d3ba8u; a += 2u) hx_put16(a, (uint16_t)hx_rand(0x10000u));
    hx_put32(0x0049b088u, HX_X + 0x400u);
    hx_put32(HX_X + 0x400u + 0x203u, HX_X + 0x500u);
    /* the count: small, a long walk inside the scratch area, or negative */
    static const int32_t counts[7] = {-1, 0, 1, 2, 2, 3, 4};
    uint32_t r = hx_rand(10u);
    hx_put32(HX_G + 0x68u, r == 9u ? 0x80000000u | hx_rand(0x10000u) << 12 : r == 8u ? 5u + hx_rand(0x3f0u) : (uint32_t)counts[r % 7u]);
    /* the widgets: in the scratch area or, half of them, with another high
     * word (never dereferenced) so G's and W's high words differ */
    for (uint32_t i = 0; i < 4u; ++i) hx_put32(HX_G + 0x18u + 4u * i, (hx_rand(2u) ? HX_W0 : HX_WX) + 0x10u * hx_rand(5u));
    hx_put32(HX_P + 0x18u, hx_rand(4u) ? hx_rand(6u) : hx_rand(0x10000u) << 16 | hx_rand(0x10000u));
    for (unsigned i = 0; i < 2u; ++i) {     /* the map's rectangle x0, y0, x1, y1 at M+0x71 */
        uint32_t lo = hx_rand(0x200u) - 0x100u;
        hx_put32(HX_M + 0x71u + 4u * i, lo);
        hx_put32(HX_M + 0x79u + 4u * i, lo + 2u + hx_rand(0x40u));
    }
    hx_put32(HX_M + 0x14u, HX_X + 0xa00u);
    hx_put32(HX_A + 0x45u, HX_X + 0xe00u);
    hx_put16(HX_X + 0xe12u, (uint16_t)(hx_rand(3u) ? hx_rand(3u) : hx_rand(0x100u)));
    hx_put16(HX_A + 0x98u, (uint16_t)((hx_rand(3u) ? 1u + hx_rand(0xffu) : 0u) | (hx_rand(3u) ? 0u : (1u + hx_rand(0xffu)) << 8)));
    /* the radar range index, and the bar: steps, surface, port origins, rectangle */
    hx_put32(HX_G + 0xc5u, hx_rand(3u) ? hx_rand(4u) : hx_rand(0x10000u) << 16 | hx_rand(0x10000u));
    *(BYTE *)(uintptr_t)0x004d2537u = (BYTE)(hx_rand(3u) ? 3u : hx_rand(0x100u));
    *(BYTE *)(uintptr_t)0x004d256cu = (BYTE)(hx_rand(3u) ? 0u : 1u + hx_rand(0xffu));
    static const uint16_t shifts[6] = {0, 1, 2, 0x20, 0x21, 0xffe1};
    hx_put16(0x004d253au, shifts[hx_rand(6u)]);
    hx_put16(0x004d253cu, shifts[hx_rand(6u)]);
    hx_put16(0x004d3ba2u, (uint16_t)(hx_rand(3u) ? hx_rand(8u) : hx_rand(0x10000u)));
    hx_put32(0x006c5ed0u, HX_S);
    hx_put32(HX_S + 0x3cu, hx_rand(0x10000u) << 16 | hx_rand(0x10000u));
    hx_put32(0x006c5f70u, HX_Q);
    for (uint32_t k = 0; k < 4u; ++k) hx_put32(HX_Q + 0x210u + 4u * k + (k & 2u ? 8u : 0u), hx_rand(9u) - 4u);
    uint32_t x0 = hx_rand(16u) - 4u, y0 = hx_rand(16u) - 6u;
    hx_put32(HX_R, x0);
    hx_put32(HX_R + 4u, y0);
    hx_put32(HX_R + 8u, x0 + hx_rand(10u) - 1u);
    hx_put32(HX_R + 12u, y0 + hx_rand(10u) - 1u);
    hx_put32(0x004a57bcu, (uint32_t)(uintptr_t)hx_o_pixel_read);
    hx_put32(0x004a57c0u, (uint32_t)(uintptr_t)hx_o_pixel_write);
    hx_put32(0x004a57ecu, (uint32_t)(uintptr_t)hx_o_canvas);
    /* the lock sum: [P+0x98] entries at P+0x70 (P = HX_X+0x400), each none,
     * an entry without a range object, or one with an object; the text */
    static const int32_t entries[8] = {-1, 0, 1, 2, 3, 4, 5, 6};
    hx_put32(HX_X + 0x400u + 0x98u, (uint32_t)entries[hx_rand(8u)]);
    for (uint32_t j = 0; j < 6u; ++j) {
        uint32_t k = hx_rand(5u);
        hx_put32(HX_X + 0x470u + 4u * j, k ? HX_E + 0x100u * (k - 1u) : 0u);
    }
    for (uint32_t j = 0; j < 4u; ++j) {
        hx_put32(HX_E + 0x100u * j + 0xbdu, hx_rand(4u) ? HX_RO + 0x10u * j : 0u);
        hx_put32(HX_RO + 0x10u * j, HX_V);
    }
    hx_put32(HX_V + 0x60u, (uint32_t)(uintptr_t)hx_o_method);
    uint32_t len = hx_rand(16u);
    for (uint32_t j = 0; j < len; ++j) *(BYTE *)(uintptr_t)(HX_STR + j) = (BYTE)(1u + hx_rand(0xffu));
    *(BYTE *)(uintptr_t)(HX_STR + len) = 0;
    hx_put32(0x004d1664u, HX_STR);
    /* the tail's record: labels +0x1c/+0x20, the marked byte +0x34 */
    hx_put32(HX_T + 0x1cu, hx_rand(0x10000u) << 16 | hx_rand(0x10000u));
    hx_put32(HX_T + 0x20u, hx_rand(0x10000u) << 16 | hx_rand(0x10000u));
    *(BYTE *)(uintptr_t)(HX_T + 0x34u) = (BYTE)(hx_rand(3u) ? 0u : 1u + hx_rand(0xffu));
    /* the part list (its count read on every pass), the bitmaps' sizes, the
     * descriptor's origin 0x4d24bc+0x9a/+0x9e and the points */
    static const int32_t parts[8] = {-1, 0, 1, 2, 3, 4, 5, 6};
    hx_put32(HX_L, (uint32_t)parts[hx_rand(8u)]);
    hx_put32(HX_L + 4u, HX_LE);
    for (uint32_t j = 0; j < 6u; ++j) {
        uint32_t e = HX_LE + 0x22u * j;
        hx_put32(e, hx_rand(4u) ? HX_B + 0x10u * hx_rand(4u) : 0u);
        hx_put32(e + 4u, hx_rand(0x400u) - 0x200u);
        hx_put32(e + 8u, hx_rand(0x400u) - 0x200u);
        hx_put32(e + 0x1cu, hx_rand(3u) ? hx_rand(4u) : hx_rand(0x10000u) << 16 | hx_rand(0x10000u));
    }
    for (uint32_t j = 0; j < 4u; ++j) {
        hx_put16(HX_B + 0x10u * j + 4u, (uint16_t)(hx_rand(3u) ? hx_rand(0x200u) : hx_rand(0x10000u)));
        hx_put16(HX_B + 0x10u * j + 6u, (uint16_t)(hx_rand(3u) ? hx_rand(0x200u) : hx_rand(0x10000u)));
    }
    hx_put32(0x004d2556u, hx_rand(3u) ? hx_rand(0x100u) : hx_rand(0x10000u) << 16 | hx_rand(0x10000u));
    hx_put32(0x004d255au, hx_rand(3u) ? hx_rand(0x100u) : hx_rand(0x10000u) << 16 | hx_rand(0x10000u));
    hx_put32(HX_PT, hx_rand(0x400u) - 0x200u);
    hx_put32(HX_PT + 4u, hx_rand(0x400u) - 0x200u);
    /* the part bitmap: two bitmaps (height any word, a small width, the
     * pixels inside HX_PD), the point near the rectangle, the pixel bytes */
    for (uint32_t j = 0; j < 2u; ++j) {
        hx_put16(HX_PB + 0x10u * j + 4u, (uint16_t)(hx_rand(3u) ? hx_rand(0x20u) : hx_rand(0x10000u)));
        hx_put16(HX_PB + 0x10u * j + 6u, (uint16_t)hx_rand(17u));
        hx_put32(HX_PB + 0x10u * j + 0xeu, HX_PD + 0x380u + 0x100u * j);
    }
    hx_put32(HX_PP, hx_get32(HX_R) + hx_rand(17u) - 8u);
    hx_put32(HX_PP + 4u, hx_get32(HX_R + 4u) + hx_rand(17u) - 8u);
    for (uint32_t a = HX_PD; a < HX_PD + 0x800u; ++a) *(BYTE *)(uintptr_t)a = (BYTE)(hx_rand(3u) ? hx_rand(8u) : hx_rand(0x100u));
}

/* ---- the rows */
enum { HX_DUMP, HX_DIGIT, HX_GUARD, HX_PART, HX_PROJ, HX_POD, HX_SHIELDS, HX_MAP_TARGET, HX_MAP_INSIDE, HX_RANGE, HX_BAR,
       HX_LOCKED, HX_TAIL, HX_PARTS, HX_BLIT_Z, HX_BITMAP, HX_WEAPON, HX_OPS };
static const struct { uint32_t va; unsigned args; const char *name; } hx_ops[HX_OPS] = {
    {0x00401e5cu, 0, "dump flush"}, {0x00434260u, 1, "callsign digit"}, {0x004669b4u, 0, "display guard"},
    {0x00438624u, 3, "part state"}, {0x004414dcu, 3, "projectile gauge"}, {0x00441a24u, 2, "pod gauge"},
    {0x00443834u, 2, "shields gauge"}, {0x0044f03cu, 3, "map target"}, {0x0044f0c4u, 2, "map inside"},
    {0x00447014u, 2, "range set"}, {0x00437e94u, 4, "part bar"}, {0x00440370u, 1, "target locked"},
    {0x0043b288u, 4, "status tail"}, {0x00451de0u, 3, "hdd parts"}, {0x0045c964u, 4, "blit at z"},
    {0x00438230u, 6, "part bitmap"}, {0x00440f18u, 3, "weapon gauge"},
};
typedef struct hx_op { unsigned op; uint32_t a[6], in[3]; } hx_op;
static uint32_t hx_widget(void) {
    uint32_t r = random32() % 6u;
    return r == 0 ? random32() : (r < 3u ? HX_WX : HX_W0) + 0x10u * (random32() % 5u);
}
static void hx_args(hx_op *o) {
    uint32_t r = random32() % 4u;
    o->a[0] = r < 2u ? (uint32_t)((int32_t)(random32() % 32u) - 16) : r < 3u ? 8u + random32() % 4u : random32();
    o->a[1] = o->a[2] = o->a[3] = o->a[4] = o->a[5] = 0;
    switch (o->op) {
    case HX_PART:
        o->a[0] = random32() % 5u ? random32() % 0x140u - 0x20u : random32();
        o->a[1] = HX_P;
        o->a[2] = HX_OUT;
        break;
    case HX_PROJ: case HX_POD: case HX_SHIELDS:
        o->a[0] = HX_G;
        o->a[1] = hx_widget();
        o->a[2] = random32() % 4u;
        break;
    case HX_MAP_TARGET:
        o->a[0] = HX_M;
        o->a[1] = HX_A;
        o->a[2] = random32() % 4u ? random32() % 3u | (random32() % 2u ? 0u : random32() << 16) : random32();
        break;
    case HX_MAP_INSIDE:
        o->a[0] = HX_M;
        o->a[1] = HX_A + 0x26u;
        break;
    case HX_WEAPON: {   /* v: the button bit, or a slider value around the clamp */
        o->a[0] = HX_G;
        o->a[1] = hx_widget();
        uint32_t r = random32() % 5u;
        o->a[2] = r == 0 ? random32() % 4u : r == 1 ? 0x3fcu + random32() % 8u : r == 2 ? random32() % 4u - 2u
                : r == 3 ? random32() % 0x400u : random32();
        break;
    }
    case HX_RANGE:
        o->a[0] = HX_G;
        o->a[1] = random32() % 4u;
        break;
    case HX_BAR:
        o->a[0] = HX_R;
        o->a[1] = random32() % 8u;
        o->a[2] = random32() % 4u ? random32() % 8u : o->a[1];
        o->a[3] = random32() % 6u ? random32() % 4u : random32() % 2u ? 4u + random32() % 4u : random32();
        break;
    case HX_LOCKED:
        o->a[0] = random32();
        break;
    case HX_TAIL: {
        /* i: a condition value (itoa's text must fit the 12-byte local) */
        o->a[0] = HX_T;
        o->a[1] = random32() % 0x180u - 0x40u;
        uint32_t f = random32() % 4u;
        o->a[2] = f == 0 ? 0u : f == 1 ? 1u : f == 2 ? random32() & 0xffffff00u : random32();
        uint32_t r = random32() % 4u;
        o->a[3] = r == 0 ? 0xffffffffu : r == 1 ? (random32() << 16) | 0xffffu : (random32() << 16) | random32() % 8u;
        break;
    }
    case HX_PARTS:   /* x is not read */
        o->a[0] = random32();
        o->a[1] = HX_L;
        o->a[2] = HX_PT;
        break;
    case HX_BLIT_Z:
        o->a[0] = 0x004d24bcu;
        o->a[1] = random32() % 5u ? HX_B + 0x10u * (random32() % 4u) : 0u;
        o->a[2] = HX_PT;
        o->a[3] = random32() % 3u ? random32() % 4u : random32();
        break;
    case HX_BITMAP:   /* R, B, P, a, b, mode */
        o->a[0] = HX_R;
        o->a[1] = HX_PB + 0x10u * (random32() % 2u);
        o->a[2] = HX_PP;
        o->a[3] = random32() % 8u;
        o->a[4] = random32() % 4u ? random32() % 8u : o->a[3];
        o->a[5] = random32() % 6u ? random32() % 4u : random32() % 2u ? 4u + random32() % 4u : random32();
        break;
    }
    for (unsigned k = 0; k < 3u; ++k) o->in[k] = random32();
}
/* The index of the first list entry W matches (-1 none), as the walks see it. */
static int32_t hx_match(uint32_t w) {
    for (int32_t i = 0; i < (int32_t)hx_get32(HX_G + 0x68u); ++i)
        if (hx_get32(HX_G + 0x18u + 4u * (uint32_t)i) == w) return i;
    return -1;
}
static void hx_cover_op(const hx_op *o) {
    if (o->op == HX_DUMP) {
        if (!hx_get32(0x004d24deu)) hx_cover(HXC_NO_OPTION);
        else if (!(hx_get32(0x004d24d8u) & 0xffffu)) hx_cover(HXC_NO_RECORD);
        else hx_cover(HXC_FLUSH);
    } else if (o->op == HX_DIGIT) {
        int32_t v = (int32_t)o->a[0];
        hx_cover(v < 0 ? HXC_DIGIT_NEGATIVE : (v >= 8 && v < 12) ? HXC_DIGIT_THREE : HXC_DIGIT_OTHER);
    } else if (o->op == HX_GUARD) {
        hx_cover(hx_get32(0x004d255eu) ? HXC_FULL_SCREEN : HXC_WINDOWED);
    } else if (o->op == HX_PART) {
        uint32_t kind = hx_get32(HX_P + 0x18u);
        hx_cover(kind == 1u ? HXC_PART_OWN : kind <= 3u ? HXC_PART_PLAIN : HXC_PART_OTHER);
        if (kind <= 3u && (int32_t)((uint32_t)((int32_t)((0x100u - o->a[0]) * 100u) >> 8)) < 1) hx_cover(HXC_PART_LOW);
    } else if (o->op == HX_PROJ || o->op == HX_POD || o->op == HX_SHIELDS) {
        int32_t i = hx_match(o->a[1]);
        if (i < 0) hx_cover(HXC_GAUGE_NONE);
        else if (o->op == HX_PROJ) hx_cover(i > 0 ? HXC_PROJ_LATER : o->a[2] & 2u ? HXC_PROJ_TOGGLE : HXC_PROJ_PANEL);
        else if (o->op == HX_POD && i == 0) hx_cover(HXC_POD_TOGGLE);
        else if (o->op == HX_SHIELDS) hx_cover(i == 0 ? HXC_SHIELDS_0 : i == 1 ? HXC_SHIELDS_1 : HXC_SHIELDS_LATER);
    } else if (o->op == HX_WEAPON) {
        int32_t i = hx_match(o->a[1]), v = (int32_t)o->a[2];
        if (i < 0) hx_cover(HXC_WEAPON_NONE);
        else if (i == 0 && (v & 2)) {
            hx_cover(HXC_WEAPON_TOGGLE);
            if (o->a[1] >> 16 != HX_G >> 16) hx_cover(HXC_WEAPON_HIGH_WORD);
        } else if (i == 0) hx_cover(HXC_WEAPON_PANEL);
        else if (i == 1) hx_cover(v <= 0 ? HXC_WEAPON_LOW : v >= 0x400 ? HXC_WEAPON_HIGH : HXC_WEAPON_MID);
        else hx_cover(HXC_WEAPON_LATER);
    } else if (o->op == HX_MAP_TARGET) {
        if ((uint16_t)(int16_t)(int8_t)*(const BYTE *)(uintptr_t)(HX_X + 0xe12u) != (uint16_t)o->a[2]) hx_cover(HXC_MAP_SIDE);
    } else if (o->op == HX_RANGE) {
        hx_cover(hx_get32(HX_G + 0xc5u) == o->a[1] ? HXC_RANGE_SAME : HXC_RANGE_SET);
    } else if (o->op == HX_BAR) {
        uint32_t q = hx_get32(0x006c5f70u);
        if ((int32_t)(hx_get32(HX_R + 4u) + hx_get32(q + 0x214u) - hx_get32(q + 0x224u)) < 0) hx_cover(HXC_BAR_NEGATIVE);
        else if (o->a[3] > 3u) hx_cover(HXC_BAR_OTHER_MODE);
        else if (o->a[3] < 2u && o->a[1] == o->a[2]) hx_cover(HXC_BAR_EQUAL);
    } else if (o->op == HX_LOCKED) {
        int32_t n = (int32_t)hx_get32(HX_X + 0x400u + 0x98u);
        if (n <= 0) hx_cover(HXC_LOCK_NONE);
        for (int32_t j = 0; j < n; ++j) {
            uint32_t e = hx_get32(HX_X + 0x470u + 4u * (uint32_t)j);
            if (!e || !hx_get32(e + 0xbdu)) hx_cover(HXC_LOCK_EMPTY);
        }
    } else if (o->op == HX_TAIL) {
        int flag = (o->a[2] & 0xffu) != 0;
        if (*(const BYTE *)(uintptr_t)(HX_T + 0x34u)) {
            hx_cover(flag ? HXC_TAIL_MARK_FLAG : HXC_TAIL_MARK);
        } else {
            hx_cover((uint16_t)o->a[3] == 0xffffu ? HXC_TAIL_CLASS : HXC_TAIL_GIVEN);
            if (flag) hx_cover(HXC_TAIL_PERCENT);
        }
    } else if (o->op == HX_PARTS) {
        int32_t n = (int32_t)hx_get32(HX_L);
        if (n <= 0) hx_cover(HXC_PARTS_NONE);
        for (int32_t j = 0; j < n; ++j) hx_cover(hx_get32(HX_LE + 0x22u * (uint32_t)j) ? HXC_PARTS_DRAWN : HXC_PARTS_EMPTY);
    } else if (o->op == HX_BLIT_Z) {
        if (!o->a[1]) hx_cover(HXC_BLIT_NULL);
    } else if (o->op == HX_BITMAP) {
        uint32_t q = hx_get32(0x006c5f70u);
        if ((int32_t)(hx_get32(HX_R + 4u) + hx_get32(q + 0x214u) - hx_get32(q + 0x224u)) < 0) hx_cover(HXC_PB_NEGATIVE);
        else if (o->a[5] > 3u) hx_cover(HXC_PB_OTHER_MODE);
        else if (o->a[5] == 1u && o->a[3] == o->a[4]) hx_cover(HXC_PB_EQUAL);
    }
}

static void hx_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &hx_dispatch;
    hx_side = side;
    hx_hash[side] = 0xcbf29ce484222325ull;
    hx_count[side] = 0;
    hx_len[side] = 0;
    hx_text[side][0] = 0;
    hx_answer_seed = answers;
    hx_seed = seed;
    hx_build();
}
static unsigned hx_calls[HX_OPS];
static int hx_case(unsigned k) {
    unsigned ops = 1u + random32() % 8u;
    static hx_op list[8];
    uint32_t seed = random32(), answers = random32();
    static uint32_t reg[2][8][3];
    static int residue[8];   /* ECX is an unwritten local's residue */
    for (unsigned i = 0; i < ops; ++i) {
        list[i].op = random32() % HX_OPS;
        hx_args(&list[i]);
    }
    for (unsigned side = 0; side < 2u; ++side) {
        hx_side_begin(side, seed, answers);
        for (unsigned i = 0; i < ops && !hx_bad && !lc_bad; ++i) {
            hx_op *o = &list[i];
            unsigned n = hx_ops[o->op].args;
            if (o->op == HX_BAR || o->op == HX_BITMAP) {
                int dbl = *(const signed char *)(uintptr_t)0x004d2537u == 3 && !*(const BYTE *)(uintptr_t)0x004d256cu;
                unsigned at = o->op == HX_BAR ? 1u : 3u;
                hx_bar_bitmap = o->op == HX_BITMAP;
                hx_bar_a = o->a[at];
                hx_bar_b = o->a[at + 1u];
                hx_bar_mode = o->a[at + 2u];
                /* a block only in mode 0 of the bitmap (its other modes write one pixel) */
                hx_bar_block = dbl && ((*(const BYTE *)(uintptr_t)0x004d253au & 31u) || (*(const BYTE *)(uintptr_t)0x004d253cu & 31u)) &&
                               (!hx_bar_bitmap || hx_bar_mode == 0u);
            }
            if (side == 0) {
                residue[i] = 0;
                if (o->op == HX_BLIT_Z) residue[i] = !o->a[1];
                if (o->op == HX_PARTS) {
                    residue[i] = 1;
                    for (int32_t j = 0; j < (int32_t)hx_get32(HX_L); ++j)
                        if (hx_get32(HX_LE + 0x22u * (uint32_t)j)) residue[i] = 0;
                }
                hx_cover_op(o);
                hx_call((uint32_t)(uintptr_t)(mapped + (hx_ops[o->op].va - 0x400000u)), o->a, n, o->in, reg[0][i]);
                ++hx_calls[o->op];
                if (o->op == HX_MAP_TARGET && reg[0][i][0]) hx_cover(HXC_MAP_PASS);
                if (o->op == HX_MAP_INSIDE) hx_cover(reg[0][i][0] ? HXC_INSIDE : HXC_OUTSIDE);
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&hx_dispatch, hx_ops[o->op].va, o->a, n ? n : 1u, o->in, &r)) hx_bad = 1;
                reg[1][i][0] = r.eax; reg[1][i][1] = r.edx; reg[1][i][2] = r.ecx;
            }
        }
        if (hx_bad || lc_bad) { fprintf(stderr, "FAIL: hud mfd leaves case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (reg[0][i][0] != reg[1][i][0] || reg[0][i][1] != reg[1][i][1] || (!residue[i] && reg[0][i][2] != reg[1][i][2])) {
            fprintf(stderr, "FAIL: hud mfd leaves case %u: %s (call %u) EAX %08lx / %08lx, EDX %08lx / %08lx, ECX %08lx / %08lx\n", k,
                    hx_ops[list[i].op].name, i, (unsigned long)reg[0][i][0], (unsigned long)reg[1][i][0], (unsigned long)reg[0][i][1],
                    (unsigned long)reg[1][i][1], (unsigned long)reg[0][i][2], (unsigned long)reg[1][i][2]);
            return 1;
        }
    if (su_compare("hud mfd leaves", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", hx_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (hx_hash[0] != hx_hash[1] || hx_count[0] != hx_count[1]) {
        fprintf(stderr, "FAIL: hud mfd leaves case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, hx_count[0], hx_count[1],
                hx_text[0], hx_text[1]);
        return 1;
    }
    return 0;
}

static int hud_mfd_leaves_cases(void) {
    current_function = "hud mfd leaves";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("hud mfd leaves: memory");
    su_pristine_take();
    if (!ir_patch_import("cw3220.dll", "_fopen", (void *)hx_o_fopen) || !ir_patch_import("cw3220.dll", "_fwrite", (void *)hx_o_fwrite) ||
        !ir_patch_import("cw3220.dll", "_fclose", (void *)hx_o_fclose) || !ir_patch_import("cw3220.dll", "_strlen", (void *)hx_o_strlen) ||
        !ir_patch_import("cw3220.dll", "_itoa", (void *)hx_o_itoa) || !ir_patch_import("cw3220.dll", "_strcat", (void *)hx_o_strcat))
        return fail("hud mfd leaves: import slot not found");
    if (hx_patch(1)) return 1;
    if (hx_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 3000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = hx_case(k);
    }
    if (!bad) {
        printf("PASS: hud mfd leaves: %u cases identical\n", cases);
        printf("hud mfd leaves rows called:");
        for (unsigned i = 0; i < HX_OPS; ++i) printf(" %s %u%s", hx_ops[i].name, hx_calls[i], i + 1u < HX_OPS ? "," : "\n");
        printf("hud mfd leaves coverage:");
        for (unsigned i = 0; i < HXC_COUNT; ++i) printf(" %s %u%s", hx_cv_names[i], hx_cv[i], i + 1u < HXC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < HXC_COUNT; ++i)
            if (!hx_cv[i]) { fprintf(stderr, "FAIL: hud mfd leaves: path '%s' never taken\n", hx_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    hx_patch(0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
