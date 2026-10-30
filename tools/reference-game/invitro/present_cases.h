/* The palette functions and the present of DBSIM, original machine code against
 * sim/src/present.c (--present; target :differential_present_test). Included
 * after gfx_cases.h; reuses startup_cases.h's DATA reset and snapshots
 * (su_reset, su_take, su_compare) and loop_cases.h's loop reset and clock
 * (lc_reset, lc_machine, lc_tick: GetTickCount on the virtual clock, so both
 * sides read the same clock values exactly when they make the same number of
 * clock calls).
 *
 * Both sides start from one synthetic world (the current palette P with
 * 256 entries, two more palettes A and B, five range records) that the case
 * builds at random; the DATA globals are set identically after each side's
 * reset. The original side runs the machine code with GDI/USER imports
 * stubbed (recording their arguments and the tables they receive); the own
 * side calls the service rows through its dispatch (0x4303c4 is hud_load.h's
 * row) and es2_sim_present_frame for the present. Compared after every
 * case: EAX (and EDX where the function defines it), DATA, the world, and
 * for the upload and the present the platform calls and their arguments.
 *
 * - cycle: 0x4306ac with five random slots (empty, due, waiting; the wrap;
 *   index past n; n <= 0).
 * - setup: 0x4308fc in modes 2, 1, 0 with random counts and durations.
 * - step: 0x430b34 with random state: the start, expiry, modes 0/1/2/other,
 *   the dirty range set and widened.
 * - upload: 0x465cc4 with random NUMCOLORS answers (-1..513), the DIB DC
 *   and palette handles set or not.
 * - present: 0x464a14 -> 0x465628 (GDI) with random game sizes, client
 *   areas, rectangles and page origins, the DIB DC and palette set or not. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/present.h"

#define PC_WORLD_SIZE 0x8000u
#define PC_P 0x0000u            /* palette records: +4 count, +8 entries */
#define PC_A 0x0040u
#define PC_B 0x0080u
#define PC_RANGES 0x0100u       /* five range records, 0x10 apart */
#define PC_P_ENTRIES 0x1800u    /* 0x800 bytes of slack before and after */
#define PC_A_ENTRIES 0x3000u
#define PC_B_ENTRIES 0x3800u

static BYTE *pc_world, *pc_world_start, *pc_snap[2];
static int pc_bad;
static es2_sim_present pc_present;
static char pc_desc[256];
static uint32_t pc_va(uint32_t off) { return (uint32_t)(uintptr_t)(pc_world + off); }
static void pc_put32(uint32_t off, uint32_t v) { put32(pc_world_start + off, v); }
static void pc_put16(uint32_t off, uint32_t v) { put16(pc_world_start + off, (uint16_t)v); }
static uint32_t pc_r(uint32_t n) { return random32() % n; }
static int32_t pc_ri(int32_t lo, int32_t hi) { return lo + (int32_t)pc_r((uint32_t)(hi - lo + 1)); }

/* ---- the original side's GDI/USER stubs and what they saw */
typedef struct pc_calls {
    unsigned get_dc, release_dc, device_caps, palette_entries, color_table, animate;
    unsigned client_rect, invalidate, begin_paint, select_palette, realize_palette, stretch_blt, end_paint;
    uint32_t color_table_dc, animate_palette, stretch[11], erase;
    uint8_t quads[1024], entries[1024];
} pc_calls;
static pc_calls pc_seen;
static int32_t pc_num_colors, pc_client_w, pc_client_h;
static uint8_t pc_system[1024];
static uint32_t __stdcall pc_get_dc(uint32_t w) { (void)w; ++pc_seen.get_dc; return 0x00020001u; }
static uint32_t __stdcall pc_release_dc(uint32_t w, uint32_t dc) { (void)w; (void)dc; ++pc_seen.release_dc; return 1; }
static uint32_t __stdcall pc_device_caps(uint32_t dc, uint32_t index) {
    ++pc_seen.device_caps;
    if (dc != 0x00020001u || index != 24u) pc_bad = 1;
    return (uint32_t)pc_num_colors;
}
static uint32_t __stdcall pc_palette_entries(uint32_t dc, uint32_t first, uint32_t n, uint8_t *out) {
    ++pc_seen.palette_entries;
    if (dc != 0x00020001u || first != 0 || n != 256u) pc_bad = 1;
    memcpy(out, pc_system, sizeof(pc_system));
    return 256u;
}
static uint32_t __stdcall pc_color_table(uint32_t dc, uint32_t first, uint32_t n, const uint8_t *quads) {
    ++pc_seen.color_table;
    if (first != 0 || n != 256u) pc_bad = 1;
    pc_seen.color_table_dc = dc;
    memcpy(pc_seen.quads, quads, sizeof(pc_seen.quads));
    return 256u;
}
static uint32_t __stdcall pc_animate(uint32_t palette, uint32_t first, uint32_t n, const uint8_t *entries) {
    ++pc_seen.animate;
    if (first != 0 || n != 256u) pc_bad = 1;
    pc_seen.animate_palette = palette;
    memcpy(pc_seen.entries, entries, sizeof(pc_seen.entries));
    return 1;
}
static uint32_t __stdcall pc_client_rect(uint32_t w, int32_t *rc) {
    (void)w; ++pc_seen.client_rect;
    rc[0] = 0; rc[1] = 0; rc[2] = pc_client_w; rc[3] = pc_client_h;
    return 1;
}
static uint32_t __stdcall pc_invalidate(uint32_t w, const int32_t *rc, uint32_t erase) {
    (void)w; ++pc_seen.invalidate;
    if (rc[2] != pc_client_w || rc[3] != pc_client_h) pc_bad = 1;
    pc_seen.erase = erase;
    return 1;
}
static uint32_t __stdcall pc_begin_paint(uint32_t w, void *ps) { (void)w; (void)ps; ++pc_seen.begin_paint; return 0x00020003u; }
static uint32_t __stdcall pc_select_palette(uint32_t dc, uint32_t palette, uint32_t background) {
    ++pc_seen.select_palette;
    if (dc != 0x00020003u) pc_bad = 1;
    /* the first selects the game's palette (FALSE), the second restores the old one (TRUE) */
    if (pc_seen.select_palette == 1u ? background != 0 : background != 1u || palette != 0x00020005u) pc_bad = 1;
    return 0x00020005u;
}
static uint32_t __stdcall pc_realize_palette(uint32_t dc) { (void)dc; ++pc_seen.realize_palette; return 0; }
static uint32_t __stdcall pc_stretch_blt(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                         uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10) {
    ++pc_seen.stretch_blt;
    uint32_t a[11] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10};
    memcpy(pc_seen.stretch, a, sizeof(a));
    return 1;
}
static uint32_t __stdcall pc_end_paint(uint32_t w, const void *ps) { (void)w; (void)ps; ++pc_seen.end_paint; return 1; }

/* ---- the own side's dispatch: the service rows */
static es2_sim_entry pc_entries[4096];
static es2_sim_dispatch pc_dispatch;
static void pc_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own present: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?", r->reason ? r->reason : "");
    pc_bad = 1;
}
static int pc_dispatch_setup(void) {
    es2_sim_dispatch_init(&pc_dispatch, pc_entries, 4096, pc_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&pc_dispatch, &s)) return fail("present: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&pc_dispatch, &error, &bad)) return fail("present: dispatch seal");
    return 0;
}
static uint32_t pc_own(uint32_t va, const uint32_t *slots, unsigned n, uint32_t *edx) {
    uint32_t regs[3] = {0, 0, 0};
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    if (es2_sim_call(&pc_dispatch, va, slots, n, regs, &r)) pc_bad = 1;
    if (edx) *edx = r.edx;
    return r.eax;
}

/* ---- sides */
static BYTE pc_steps[0x600];   /* the fade step tables 0x4cfda4.. when pc_steps_on */
static int pc_steps_on;
static uint32_t pc_data[64][2];   /* DATA dwords the case sets after the reset */
static unsigned pc_data_count;
static void pc_set(uint32_t va, uint32_t v) { pc_data[pc_data_count][0] = va; pc_data[pc_data_count][1] = v; ++pc_data_count; }
static void pc_side(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &pc_dispatch;
    memcpy(pc_world, pc_world_start, PC_WORLD_SIZE);
    BYTE *d = mapped - 0x400000u;
    put32(d + 0x4d3b78u, 0);                 /* the clock base: T = vt >> 4 */
    put32(d + 0x49b020u, pc_va(PC_P));
    for (unsigned i = 0; i < pc_data_count; ++i) put32(d + pc_data[i][0], pc_data[i][1]);
    if (pc_steps_on) memcpy(d + 0x004cfda4u, pc_steps, sizeof(pc_steps));
    memset(&pc_seen, 0, sizeof(pc_seen));
    es2_sim_present_headless(&pc_present, &lc_loop);
    pc_present.platform.num_colors = pc_num_colors;
    pc_present.platform.system_count = 256;
    memcpy(pc_present.platform.system, pc_system, sizeof(pc_system));
    pc_present.platform.client_w = pc_client_w;
    pc_present.platform.client_h = pc_client_h;
    es2_sim_present_bind(&pc_present);
}
static void pc_end(unsigned side) {
    su_take(side);
    memcpy(pc_snap[side], pc_world, PC_WORLD_SIZE);
}
static int pc_compare(const char *what, unsigned k, const uint32_t eax[2], const uint32_t edx[2], int edx_defined) {
    int shown = 0;
#define PC_FAIL(...) do { if (!shown++) fprintf(stderr, "  case %u: %s\n", k, pc_desc); fprintf(stderr, __VA_ARGS__); return 1; } while (0)
    if (pc_bad) PC_FAIL("FAIL: present %s case %u: a call was refused or a stub saw a wrong argument\n", what, k);
    if (eax[0] != eax[1]) PC_FAIL("FAIL: present %s case %u: EAX %08lx original, %08lx own\n", what, k, (unsigned long)eax[0], (unsigned long)eax[1]);
    if (edx_defined && edx[0] != edx[1])
        PC_FAIL("FAIL: present %s case %u: EDX %08lx original, %08lx own\n", what, k, (unsigned long)edx[0], (unsigned long)edx[1]);
    if (su_compare(what, k)) PC_FAIL("  (DATA)\n");
    if (memcmp(pc_snap[0], pc_snap[1], PC_WORLD_SIZE)) {
        unsigned count = 0;
        for (uint32_t i = 0; i < PC_WORLD_SIZE; ++i)
            if (pc_snap[0][i] != pc_snap[1][i] && count++ < 16)
                fprintf(stderr, "  world +0x%04lx original %02x own %02x\n", (unsigned long)i, pc_snap[0][i], pc_snap[1][i]);
        PC_FAIL("FAIL: present %s case %u: %u world bytes differ\n", what, k, count);
    }
#undef PC_FAIL
    return 0;
}

/* ---- the world a case starts from */
static void pc_world_reset(void) {
    memset(pc_world_start, 0, PC_WORLD_SIZE);
    random_bytes(pc_world_start + PC_P_ENTRIES - 0x800u, 0x1400u);
    random_bytes(pc_world_start + PC_A_ENTRIES, 0x400u);
    random_bytes(pc_world_start + PC_B_ENTRIES, 0x400u);
    pc_put16(PC_P + 4u, 256u); pc_put32(PC_P + 8u, pc_va(PC_P_ENTRIES));
    pc_put32(PC_A + 8u, pc_va(PC_A_ENTRIES));
    pc_put32(PC_B + 8u, pc_va(PC_B_ENTRIES));
    pc_data_count = 0;
    pc_steps_on = 0;
    pc_num_colors = -1;
    pc_client_w = 320; pc_client_h = 240;
    random_bytes(pc_system, sizeof(pc_system));
}
/* A clock value near the one both sides read first (T = (0x01000000 + a few steps) >> 4). */
static uint32_t pc_near_clock(void) { return 0x00100000u + (uint32_t)pc_ri(-3, 5); }

/* ---- cycle */
static unsigned pc_cov_due, pc_cov_waiting, pc_cov_empty, pc_cov_wrap, pc_cov_reset, pc_cov_n0;
static int pc_cycle_case(unsigned k) {
    pc_world_reset();
    int n_out = 0;
    for (uint32_t s = 0; s < 5u; ++s) {
        uint32_t rec = PC_RANGES + 0x10u * s, kind = pc_r(4u);
        if (kind == 0) { pc_set(0x004cfd64u + 4u * s, 0); ++pc_cov_empty; continue; }
        int32_t first = pc_ri(0, 200), n = pc_r(8u) ? pc_ri(1, 56) : pc_ri(-3, 0);
        if (pc_r(40u) == 0) { first = 0; n = 256; }
        int32_t index = pc_r(6u) ? pc_ri(0, n > 0 ? n - 1 : 0) : pc_ri(0, (n > 0 ? n : 0) + 3);
        pc_put16(rec, (uint32_t)first); pc_put16(rec + 2u, (uint32_t)n); pc_put16(rec + 4u, (uint32_t)pc_ri(-20, 200));
        pc_set(0x004cfd64u + 4u * s, pc_va(rec));
        uint32_t deadline = kind == 1u ? 0 : kind == 2u ? 0xffffffffu : pc_near_clock();
        pc_set(0x004cfd78u + 4u * s, deadline);
        if (kind == 1u) { ++pc_cov_due; if (n > 0 && index > 0) ++pc_cov_wrap; if (n > 0 && index == n - 1) ++pc_cov_reset; if (n <= 0) ++pc_cov_n0; }
        if (kind == 2u) ++pc_cov_waiting;
        n_out += snprintf(pc_desc + n_out, sizeof(pc_desc) - (size_t)n_out, "[%lu: %ld+%ld ix %ld dl %08lx] ", (unsigned long)s, (long)first, (long)n,
                          (long)index, (unsigned long)deadline);
        /* the index words share dwords: set both halves */
        uint32_t word = 0x004cfd8cu + 2u * s, dword = word & ~3u;
        uint32_t prior = 0;
        for (unsigned i = 0; i < pc_data_count; ++i) if (pc_data[i][0] == dword) prior = pc_data[i][1];
        uint32_t v = word & 2u ? (prior & 0xffffu) | ((uint32_t)index << 16) : (prior & 0xffff0000u) | ((uint32_t)index & 0xffffu);
        pc_set(dword, v);
    }
    /* the dirty range state of 0x4303c4 */
    pc_set(0x0049b030u, pc_r(2u)); pc_set(0x0049b028u, (uint32_t)pc_ri(0, 255)); pc_set(0x0049b02cu, (uint32_t)pc_ri(0, 255));
    uint32_t eax[2], edx[2];
    for (unsigned side = 0; side < 2; ++side) {
        pc_side(side);
        eax[side] = side ? pc_own(0x004306acu, NULL, 0, &edx[1]) : lc_machine(0x004306acu, NULL, 0, &edx[0]);
        pc_end(side);
    }
    return pc_compare("cycle", k, eax, edx, 1);
}

/* ---- fade set-up */
static unsigned pc_cov_mode[3];
static int pc_setup_case(unsigned k) {
    pc_world_reset();
    uint32_t mode = pc_r(3u);
    int32_t n = pc_r(10u) ? pc_ri(1, 256) : pc_ri(-3, 0);
    int32_t d = pc_r(4u) ? pc_ri(1, 200) : (int32_t)(random32() | 1u);
    if (pc_r(4u) == 0) d = -pc_ri(1, 64);
    pc_put16(PC_A + 4u, (uint32_t)(mode == 0 ? pc_ri(-3, 256) : n));
    pc_put16(PC_B + 4u, (uint32_t)(mode == 0 ? n : pc_ri(-3, 256)));
    uint32_t a = mode == 0 ? 0 : pc_va(PC_A), b = mode == 1 ? 0 : pc_va(PC_B);
    pc_set(0x0049b03cu, 0x01010101u);
    ++pc_cov_mode[mode];
    snprintf(pc_desc, sizeof(pc_desc), "setup mode %lu n %ld d %ld", (unsigned long)mode, (long)n, (long)d);
    uint32_t eax[2], edx[2] = {0, 0}, args[3] = {a, b, (uint32_t)d};
    for (unsigned side = 0; side < 2; ++side) {
        pc_side(side);
        eax[side] = side ? pc_own(0x004308fcu, args, 3, NULL) : lc_machine(0x004308fcu, args, 3, NULL);
        pc_end(side);
    }
    return pc_compare("setup", k, eax, edx, 0);
}

/* ---- fade step */
static unsigned pc_cov_expired, pc_cov_started, pc_cov_running, pc_cov_step_mode[4], pc_cov_dirty_set, pc_cov_dirty_widen;
static int pc_step_case(unsigned k) {
    pc_world_reset();
    int32_t start = pc_ri(0, 200), count = pc_r(10u) ? pc_ri(1, 56) : pc_ri(-2, 0);
    uint32_t mode = pc_r(5u);
    uint32_t mode_word = mode == 3u ? (uint32_t)pc_ri(3, 9) : mode == 4u ? 0xffffu : mode;
    int running = (int)pc_r(4u) != 0;
    uint32_t d = (uint32_t)pc_ri(1, 64);
    uint32_t deadline = pc_near_clock() + (uint32_t)pc_ri(-8, 64);
    pc_set(0x004cfd98u, pc_va(PC_A));
    pc_set(0x004cfda0u, d);
    pc_set(0x0049b03cu, running ? 1u : 0u);
    pc_set(0x0049b038u, deadline);
    pc_set(0x004cfd9cu, ((uint32_t)count << 16) | (uint32_t)start);   /* start word, count word */
    pc_set(0x004d03a4u, mode_word);
    int dirty = (int)pc_r(2u);
    pc_set(0x0049b030u, (uint32_t)dirty);
    pc_set(0x0049b028u, (uint32_t)pc_ri(-5, 255)); pc_set(0x0049b02cu, (uint32_t)pc_ri(-5, 255));
    random_bytes(pc_steps, sizeof(pc_steps));   /* the three step tables */
    pc_steps_on = 1;
    snprintf(pc_desc, sizeof(pc_desc), "step start %ld count %ld mode %04lx running %d d %lu deadline %08lx dirty %d", (long)start,
             (long)count, (unsigned long)mode_word, running, (unsigned long)d, (unsigned long)deadline, dirty);
    ++pc_cov_step_mode[mode < 3u ? mode : 3u];
    if (!running) ++pc_cov_started; else ++pc_cov_running;
    if (running && deadline < 0x00100000u) ++pc_cov_expired;
    if (dirty) ++pc_cov_dirty_widen; else ++pc_cov_dirty_set;
    uint32_t eax[2], edx[2];
    for (unsigned side = 0; side < 2; ++side) {
        pc_side(side);
        eax[side] = side ? pc_own(0x00430b34u, NULL, 0, &edx[1]) : lc_machine(0x00430b34u, NULL, 0, &edx[0]);
        pc_end(side);
    }
    /* expired: EDX is the clock call's (GetTickCount's on the original side) */
    int expired = mapped[0x0049b03cu - 0x400000u] == 0;
    return pc_compare("step", k, eax, edx, !expired);
}

/* ---- upload */
static unsigned pc_cov_static, pc_cov_all, pc_cov_dib_dc[2], pc_cov_wide;
static int pc_upload_case(unsigned k) {
    pc_world_reset();
    static const int32_t answers[] = {-1, 0, 1, 2, 3, 20, 21, 236, 256, 257, 300, 512, 513};   /* -2 and below, 514 and above: refused (outside E) */
    pc_num_colors = pc_r(3u) ? answers[pc_r(sizeof(answers) / sizeof(answers[0]))] : pc_ri(-1, 513);
    uint32_t dc = pc_r(3u) ? 0x00020007u : 0, palette = pc_r(3u) ? 0x00020009u : 0;
    pc_set(0x004d3034u, dc);
    pc_set(0x004d302cu, palette);
    int32_t r = pc_num_colors / 2;
    if (r > 0 && r <= 128) ++pc_cov_static;
    if (r == 0) ++pc_cov_all;
    if (r > 128) ++pc_cov_wide;
    ++pc_cov_dib_dc[dc != 0];
    snprintf(pc_desc, sizeof(pc_desc), "upload NUMCOLORS %ld dc %08lx palette %08lx", (long)pc_num_colors, (unsigned long)dc, (unsigned long)palette);
    uint32_t eax[2], edx[2] = {0, 0}, args[1];
    pc_calls seen[2];
    es2_sim_load_platform table[2];
    for (unsigned side = 0; side < 2; ++side) {
        pc_side(side);
        args[0] = pc_va(PC_P);
        eax[side] = side ? pc_own(0x00465cc4u, args, 1, NULL) : lc_machine(0x00465cc4u, args, 1, NULL);
        seen[side] = pc_seen;
        table[side] = *es2_sim_present_table(&pc_present);
        pc_end(side);
    }
    if (pc_compare("upload", k, eax, edx, 0)) return 1;
    const es2_sim_present_platform *own = &pc_present.platform;
    /* the original's tables against the own platform record */
    int bad = seen[0].get_dc != own->get_dc || seen[0].release_dc != own->release_dc || seen[0].device_caps != own->device_caps ||
              seen[0].palette_entries != own->palette_entries || seen[0].color_table != own->color_table || seen[0].animate != own->animate;
    if (bad) fprintf(stderr, "FAIL: present upload case %u: call counts differ (%s)\n", k, pc_desc);
    if (!bad && memcmp(seen[0].entries, own->animate_table, 1024)) {
        fprintf(stderr, "FAIL: present upload case %u: the AnimatePalette tables differ (%s)\n", k, pc_desc);
        bad = 1;
    }
    if (!bad && (seen[0].color_table_dc != dc || seen[0].animate_palette != palette)) {
        fprintf(stderr, "FAIL: present upload case %u: SetDIBColorTable/AnimatePalette handles\n", k);
        bad = 1;
    }
    /* the table SetDIBColorTable received: blue, green, red (the fourth byte is stack residue in the original) */
    if (!bad && dc) {
        if (table[1].color_table_uploads != 1u) bad = 1;
        for (unsigned i = 0; !bad && i < 256u; ++i)
            if (memcmp(seen[0].quads + 4u * i, table[1].color_table + 4u * i, 3)) bad = 1;
        if (bad) fprintf(stderr, "FAIL: present upload case %u: the colour tables differ (%s)\n", k, pc_desc);
    }
    if (!bad && !dc && table[1].color_table_uploads != 0) { fprintf(stderr, "FAIL: present upload case %u: a table without the DIB DC\n", k); bad = 1; }
    return bad;
}

/* ---- present */
static unsigned pc_cov_palette[2], pc_cov_dc[2], pc_cov_full_w[2], pc_cov_full_h[2];
static int pc_present_case(unsigned k) {
    pc_world_reset();
    static const int32_t sizes[][2] = {{320, 240}, {640, 480}, {512, 384}};
    int32_t w = 320, h = 240;
    if (pc_r(4u)) { uint32_t i = pc_r(3u); w = sizes[i][0]; h = sizes[i][1]; }
    else { w = pc_ri(1, 800); h = pc_ri(1, 600); }
    pc_client_w = pc_r(3u) ? w * pc_ri(1, 3) : pc_ri(1, 2000);
    pc_client_h = pc_r(3u) ? h * pc_ri(1, 3) : pc_ri(1, 1500);
    int32_t l = pc_ri(-40, w), t = pc_ri(-40, h), r = pc_ri(l - 5, w + 40), b = pc_ri(t - 5, h + 40);
    if (pc_r(3u) == 0) { l = 0; r = w - 1; }
    if (pc_r(3u) == 0) { t = 0; b = h - 1; }
    if (pc_r(8u) == 0) { l = pc_ri(-70000, 70000); r = l + pc_ri(-70000, 70000); }
    uint32_t dc = pc_r(6u) ? 0x00020007u : 0, palette = pc_r(3u) ? 0x00020009u : 0;
    pc_set(0x004d253eu, (uint32_t)w); pc_set(0x004d2542u, (uint32_t)h);
    pc_set(0x004d3034u, dc); pc_set(0x004d302cu, palette); pc_set(0x004d3610u, 0x0002000bu);
    pc_set(0x004d2ff8u, (uint32_t)l); pc_set(0x004d2ffcu, (uint32_t)t); pc_set(0x004d3000u, (uint32_t)r); pc_set(0x004d3004u, (uint32_t)b);
    pc_set(0x004d3018u, (uint32_t)pc_ri(-10, 700)); pc_set(0x004d301cu, (uint32_t)pc_ri(-10, 500));
    ++pc_cov_palette[palette != 0]; ++pc_cov_dc[dc != 0];
    ++pc_cov_full_w[r - l + 1 == w]; ++pc_cov_full_h[b - t + 1 == h];
    snprintf(pc_desc, sizeof(pc_desc), "present game %ldx%ld client %ldx%ld R %ld,%ld,%ld,%ld dc %08lx palette %08lx", (long)w, (long)h,
             (long)pc_client_w, (long)pc_client_h, (long)l, (long)t, (long)r, (long)b, (unsigned long)dc, (unsigned long)palette);
    uint32_t eax[2] = {0, 0}, edx[2] = {0, 0};
    pc_calls seen;
    for (unsigned side = 0; side < 2; ++side) {
        pc_side(side);
        if (!side) lc_machine(0x00464a14u, NULL, 0, NULL);
        else if (es2_sim_present_frame(&pc_present, 0x0045fbe4u)) pc_bad = 1;
        if (!side) seen = pc_seen;
        pc_end(side);
    }
    if (pc_compare("present", k, eax, edx, 0)) return 1;
    const es2_sim_present_platform *own = &pc_present.platform;
    int bad = seen.client_rect != own->client_rect || seen.invalidate != own->invalidate || seen.begin_paint != own->begin_paint ||
              seen.select_palette != own->select_palette || seen.realize_palette != own->realize_palette ||
              seen.stretch_blt != own->stretch_blt || seen.end_paint != own->end_paint;
    if (!bad && seen.stretch_blt) {
        const uint32_t *a = seen.stretch;
        bad = a[0] != 0x00020003u || a[5] != dc || a[10] != 0x00cc0020u || seen.erase != 1u;
        for (unsigned i = 0; !bad && i < 4u; ++i) bad = a[1 + i] != (uint32_t)own->dst[i] || a[6 + i] != (uint32_t)own->src[i];
        if (bad)
            fprintf(stderr, "  StretchBlt original dst %ld,%ld,%ld,%ld src %ld,%ld,%ld,%ld; own dst %ld,%ld,%ld,%ld src %ld,%ld,%ld,%ld\n",
                    (long)a[1], (long)a[2], (long)a[3], (long)a[4], (long)a[6], (long)a[7], (long)a[8], (long)a[9], (long)own->dst[0],
                    (long)own->dst[1], (long)own->dst[2], (long)own->dst[3], (long)own->src[0], (long)own->src[1], (long)own->src[2],
                    (long)own->src[3]);
    }
    if (bad) fprintf(stderr, "FAIL: present present case %u: the GDI calls differ (%s)\n", k, pc_desc);
    return bad;
}

static LONG CALLBACK pc_fault(EXCEPTION_POINTERS *fault) {
    DWORD code = fault->ExceptionRecord->ExceptionCode;
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_INT_OVERFLOW) && pc_desc[0])
        fprintf(stderr, "  case parameters: %s\n", pc_desc);
    return EXCEPTION_CONTINUE_SEARCH;
}
static int present_cases(void) {
    current_function = "present";
    AddVectoredExceptionHandler(1, pc_fault);
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    pc_world = VirtualAlloc(NULL, PC_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    pc_world_start = malloc(PC_WORLD_SIZE);
    pc_snap[0] = malloc(PC_WORLD_SIZE);
    pc_snap[1] = malloc(PC_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !pc_world || !pc_world_start || !pc_snap[0] || !pc_snap[1])
        return fail("present: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("present: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
        {"USER32.dll", "GetDC", (void *)pc_get_dc}, {"USER32.dll", "ReleaseDC", (void *)pc_release_dc},
        {"GDI32.dll", "GetDeviceCaps", (void *)pc_device_caps}, {"GDI32.dll", "GetSystemPaletteEntries", (void *)pc_palette_entries},
        {"GDI32.dll", "SetDIBColorTable", (void *)pc_color_table}, {"GDI32.dll", "AnimatePalette", (void *)pc_animate},
        {"USER32.dll", "GetClientRect", (void *)pc_client_rect}, {"USER32.dll", "InvalidateRect", (void *)pc_invalidate},
        {"USER32.dll", "BeginPaint", (void *)pc_begin_paint}, {"GDI32.dll", "SelectPalette", (void *)pc_select_palette},
        {"GDI32.dll", "RealizePalette", (void *)pc_realize_palette}, {"GDI32.dll", "StretchBlt", (void *)pc_stretch_blt},
        {"USER32.dll", "EndPaint", (void *)pc_end_paint},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("present: import slot not found");
        }
    if (pc_dispatch_setup()) return 1;
    es2_sim_load_bind(NULL);   /* the colour table goes to the present instance's own record */
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"cycle", pc_cycle_case, 1000}, {"setup", pc_setup_case, 1000}, {"step", pc_step_case, 1500},
        {"upload", pc_upload_case, 800}, {"present", pc_present_case, 1000},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            pc_bad = 0;
            pc_desc[0] = 0;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: present %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) {
        printf("PRESENT coverage: cycle slots due %u waiting %u empty %u, wrapped %u, reset %u, n<=0 %u; setup modes %u/%u/%u; step "
               "started %u running %u expired %u, modes 0 %u 1 %u 2 %u other %u, dirty set %u widened %u; upload static %u all %u "
               "wide %u, DIB DC %u/%u; present palette %u/%u, DIB DC %u/%u, full width %u/%u, full height %u/%u\n",
               pc_cov_due, pc_cov_waiting, pc_cov_empty, pc_cov_wrap, pc_cov_reset, pc_cov_n0, pc_cov_mode[0], pc_cov_mode[1],
               pc_cov_mode[2], pc_cov_started, pc_cov_running, pc_cov_expired, pc_cov_step_mode[0], pc_cov_step_mode[1],
               pc_cov_step_mode[2], pc_cov_step_mode[3], pc_cov_dirty_set, pc_cov_dirty_widen, pc_cov_static, pc_cov_all, pc_cov_wide,
               pc_cov_dib_dc[0], pc_cov_dib_dc[1], pc_cov_palette[0], pc_cov_palette[1], pc_cov_dc[0], pc_cov_dc[1], pc_cov_full_w[0],
               pc_cov_full_w[1], pc_cov_full_h[0], pc_cov_full_h[1]);
        unsigned need[] = {pc_cov_due, pc_cov_waiting, pc_cov_empty, pc_cov_wrap, pc_cov_reset, pc_cov_n0, pc_cov_mode[0], pc_cov_mode[1],
                           pc_cov_mode[2], pc_cov_started, pc_cov_running, pc_cov_expired, pc_cov_step_mode[0], pc_cov_step_mode[1],
                           pc_cov_step_mode[2], pc_cov_step_mode[3], pc_cov_dirty_set, pc_cov_dirty_widen, pc_cov_static, pc_cov_all,
                           pc_cov_wide, pc_cov_dib_dc[0], pc_cov_dib_dc[1], pc_cov_palette[0], pc_cov_palette[1], pc_cov_dc[0], pc_cov_dc[1],
                           pc_cov_full_w[0], pc_cov_full_w[1], pc_cov_full_h[0], pc_cov_full_h[1]};
        for (unsigned i = 0; i < sizeof(need) / sizeof(need[0]); ++i)
            if (!need[i]) { fprintf(stderr, "FAIL: present coverage item %u never taken\n", i); bad = 1; }
    }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
