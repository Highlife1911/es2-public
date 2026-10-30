/* The view-space projection 0x48cce4 and the shade ranges 0x42e8e8 with its
 * callees 0x42e878, 0x42ea0c, 0x430518 and 0x430590 (0x486e10 inlined),
 * original machine code against sim/src/shape_draw.c (--shade-ranges; target
 * :differential_shade_ranges_test). Included after gouraud_cases.h; reuses
 * startup_cases.h's DATA reset and snapshots (su_reset, su_take, su_compare),
 * loop_cases.h's loop set-up (lc_reset) and shape_draw_cases.h's presets
 * (sd_set8/16/32) and shape draw binding.
 *
 * The stack: 0x42e8e8's flag path hands 0x430590 a record whose second pair is
 * a dword of the original's own frame (rasterizer.md "View-space projection and
 * shade ranges"), so the words it writes depend on the stack address. Both
 * sides therefore run with the arguments at the same address S in a buffer of
 * this harness: side 0 through sr_call, which switches ESP to S + 4n and pushes
 * the arguments there; side 1 with the call area's top at S. S moves per case
 * (its low and high words are the start and length of that pair).
 *
 * Every case builds a synthetic world (an object with its vtable, the kind
 * table and its byte pairs, the level thresholds, the run pairs, the range
 * records, the 0x430590 records with their lists and pairs, dword points
 * and an index list) and a destination buffer the range records point
 * into, presets the DATA words, and calls one function. The object's
 * vtable slot +0x10 (kind 5) is a recording stand-in of this harness on
 * both sides (a cdecl function; on side 1 a service row at its address)
 * that logs the object into the world and answers a per-case value.
 *
 * Compared: DATA (with the range table 0x4a4fa5, the projection cache and
 * the output polygon), the world (with the log), the destination buffer,
 * EAX/EDX/ECX (0x48cce4's EDX only with a shift: without one it is the
 * original's uninitialised local).
 *
 * - runs: 0x430518 directly (record counts 0..6, lengths negative, zero,
 *   short and long).
 * - spans: 0x430590 directly (1..6 records of 0..8 pairs, negative counts).
 * - level: 0x42e878 (kinds 0, 5, others and negative; threshold counts
 *   0..8; levels negative and large; the kind 5 bias both ways).
 * - default: 0x42ea0c.
 * - select: 0x42e8e8 (the default byte, an empty kind entry, flag 0 and
 *   flags with the low byte set or clear, kind 5, strides negative to 4).
 * - project: 0x48cce4 (the divisor path and shifts 1..12, near planes,
 *   point states 0..3, cached points, vertex counts 0..32, shuffled
 *   indices). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "shape_draw_cases.h"

#define SR_WORLD_SIZE 0x10000u
#define SR_DEST_AREA 0x4000u
#define SR_DEST_SIZE (0x20u * SR_DEST_AREA)
/* World layout (offsets). */
#define SR_OBJ 0x0000u       /* the object: vtable pointer, kind word */
#define SR_VT 0x0040u        /* its vtable (slot +0x10) */
#define SR_KINDS 0x0100u     /* the kind table (5 bytes per kind), its pointer at +0x28 (kind 0) */
#define SR_BYTEPAIRS 0x0200u /* 8 kinds x 0x20 bytes of {range, row} pairs */
#define SR_THR 0x0400u       /* kind 0's thresholds; the others' at +0x40 */
#define SR_RUNS 0x0500u      /* run pairs 0x500..0xd00; P = 0x900 */
#define SR_P 0x0900u
#define SR_Q 0x1000u         /* the last row's run pairs */
#define SR_REC 0x1800u       /* 0x20 range records of 8 bytes */
#define SR_R 0x1a00u         /* the direct calls' record(s) */
#define SR_LISTS 0x1b00u     /* 6 lists of 8 dwords */
#define SR_SPAIRS 0x1c00u    /* 8 pair areas of 0x40 bytes */
#define SR_PTS 0x2000u       /* 64 dword points */
#define SR_INDEX 0x2400u     /* the index words */
#define SR_LOG 0xf000u       /* the stand-in's log: count, then the objects */

static BYTE *sr_world, *sr_world_start, *sr_dest, *sr_dest_start, *sr_stack;
static es2_sim_entry sr_entries[4096];
static es2_sim_dispatch sr_dispatch;
static int sr_bad;
static uint32_t sr_slot_answer, sr_top, sr_out[2][3];
static unsigned sr_cov_runs[4], sr_cov_spans[3], sr_cov_level[5], sr_cov_select[7], sr_cov_project[6];

static uint32_t sr_at(uint32_t offset) { return (uint32_t)(uintptr_t)sr_world + offset; }
static void sr_w32(uint32_t offset, uint32_t v) { put32(sr_world_start + offset, v); }
static void sr_w16(uint32_t offset, uint32_t v) { put16(sr_world_start + offset, (uint16_t)v); }
static void sr_w8(uint32_t offset, uint32_t v) { sr_world_start[offset] = (BYTE)v; }
static uint32_t sr_rand(uint32_t n) { return random32() % n; }
static uint32_t sr_signed(uint32_t range) { return (uint32_t)((int32_t)sr_rand(2u * range + 1u) - (int32_t)range); }
/* A run length: mostly short, some long, zero or negative. */
static uint32_t sr_length(void) {
    uint32_t r = sr_rand(16u);
    return r == 0 ? 0u : r == 1u ? (uint32_t)-(int32_t)(1u + sr_rand(0x40u)) : r == 2u ? 0x41u + sr_rand(0x1c0u) : 1u + sr_rand(0x40u);
}

static void sr_log(uint32_t obj) {
    uint32_t n = get32(sr_world + SR_LOG);
    if (n < 0x100u) put32(sr_world + SR_LOG + 4u + 4u * n, obj);
    put32(sr_world + SR_LOG, n + 1u);
}
/* The vtable slot +0x10 of a kind 5 object: side 0's machine code calls it
 * (cdecl, the object as the argument); side 1 reaches the row below. */
static uint32_t __cdecl sr_slot(uint32_t obj) {
    sr_log(obj);
    return sr_slot_answer;
}
static void sr_s_slot(es2_sim_service_call *c, void *u) {
    (void)u;
    sr_log(c->slots[0]);
    c->eax = sr_slot_answer;
}
static void sr_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own shade ranges: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    sr_bad = 1;
}
static int sr_setup(void) {
    es2_sim_dispatch_init(&sr_dispatch, sr_entries, 4096, sr_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&sr_dispatch, &s)) return fail("shade ranges: dispatch");
    if (es2_sim_dispatch_add_service(&sr_dispatch, (uint32_t)(uintptr_t)sr_slot, "slot stand-in", sr_s_slot, NULL, 0, 1))
        return fail("shade ranges: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&sr_dispatch, &error, &bad)) {
        fprintf(stderr, "seal: %s 0x%08lx\n", error ? error : "?", (unsigned long)bad);
        return fail("shade ranges: dispatch seal");
    }
    static const uint32_t rows[] = {0x0048cce4u, 0x0042e8e8u, 0x0042e878u, 0x0042ea0cu, 0x00430518u, 0x00430590u};
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        const es2_sim_entry *e = es2_sim_dispatch_find(&sr_dispatch, rows[i]);
        if (!e || e->kind != ES2_SIM_SERVICE) {
            fprintf(stderr, "no service row at 0x%08lx\n", (unsigned long)rows[i]);
            return fail("shade ranges: a row is missing");
        }
    }
    return 0;
}

/* The original with its arguments at top (ESP = top at the call's entry
 * plus the return address): EAX/EDX/ECX in and out. */
extern void sr_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out, uint32_t top);
__asm__(".text\n.globl _sr_call\n_sr_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n\tmovl 28(%ebp), %eax\n\tleal (%eax,%ecx,4), %esp\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

static void sr_begin(void) {
    memset(sr_world_start, 0, SR_WORLD_SIZE);
    for (uint32_t i = 0; i < SR_DEST_SIZE; i += 4u) put32(sr_dest_start + i, random32());
    sd_preset_count = 0;
    /* The arguments' address: the low word varies, the high word stays small. */
    sr_top = (uint32_t)(uintptr_t)sr_stack + 0x8000u + 4u * sr_rand(0x1000u);
    /* The range records: each points into its own destination area. */
    for (uint32_t i = 0; i < 0x20u; ++i) {
        sr_w32(SR_REC + 8u * i, (uint32_t)(uintptr_t)sr_dest + SR_DEST_AREA * i + 2u * sr_rand(0x100u));
        sr_w32(SR_REC + 8u * i + 4u, random32());
    }
    sd_set32(0x0049b034u, sr_at(SR_REC));
}
/* Runs the call on both sides and compares; mask: which of EAX/EDX/ECX. */
static int sr_run(const char *what, unsigned k, uint32_t va, const uint32_t *a, unsigned n, const uint32_t in[3], unsigned mask) {
    uint32_t (*out)[3] = sr_out;
    for (unsigned side = 0; side < 2u; ++side) {
        lc_reset(side, 47, 0x01000000u, 1);
        lc_loop.dispatch = &sr_dispatch;
        lc_loop.stack_lo = (uint32_t)(uintptr_t)sr_stack;
        lc_loop.stack_hi = sr_top;
        memset(&sd_draw, 0, sizeof(sd_draw));
        sd_draw.loop = &lc_loop;
        es2_sim_shape_draw_bind(&sd_draw);
        memcpy(sr_world, sr_world_start, SR_WORLD_SIZE);
        memcpy(sr_dest, sr_dest_start, SR_DEST_SIZE);
        for (unsigned i = 0; i < sd_preset_count; ++i) {
            BYTE *p = mapped + (sd_presets[i].va - 0x400000u);
            if (sd_presets[i].width == 4) put32(p, sd_presets[i].value);
            else if (sd_presets[i].width == 2) put16(p, (uint16_t)sd_presets[i].value);
            else *p = (BYTE)sd_presets[i].value;
        }
        sr_bad = 0;
        if (!side) sr_call((uint32_t)(uintptr_t)(mapped + (va - 0x400000u)), a, n, in, out[0], sr_top);
        else {
            es2_sim_result res;
            memset(&res, 0, sizeof(res));
            if (es2_sim_call(&sr_dispatch, va, a, n, in, &res)) sr_bad = 1;
            out[1][0] = res.eax; out[1][1] = res.edx; out[1][2] = res.ecx;
        }
        if (sr_bad) { fprintf(stderr, "FAIL: shade ranges %s case %u: the own call was refused\n", what, k); return 1; }
        su_take(side);
        if (!side) {
            memcpy(sr_world_start + SR_WORLD_SIZE, sr_world, SR_WORLD_SIZE);
            memcpy(sr_dest_start + SR_DEST_SIZE, sr_dest, SR_DEST_SIZE);
        }
    }
    char label[96];
    snprintf(label, sizeof(label), "shade ranges %s", what);
    if (su_compare(label, k)) return 1;
    static const struct { const char *name; BYTE *const *own; BYTE *const *start; uint32_t size; } areas[] = {
        {"world", &sr_world, &sr_world_start, SR_WORLD_SIZE}, {"destination", &sr_dest, &sr_dest_start, SR_DEST_SIZE},
    };
    for (unsigned j = 0; j < 2u; ++j) {
        BYTE *own = *areas[j].own, *orig = *areas[j].start + areas[j].size;
        if (!memcmp(own, orig, areas[j].size)) continue;
        for (uint32_t i = 0, shown = 0; i < areas[j].size && shown < 24u; ++i)
            if (own[i] != orig[i]) {
                fprintf(stderr, "  %s +0x%05lx original %02x own %02x\n", areas[j].name, (unsigned long)i, orig[i], own[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: %s case %u: the %s differs (arguments at 0x%08lx)\n", label, k, areas[j].name, (unsigned long)sr_top);
        return 1;
    }
    static const char *const names[3] = {"EAX", "EDX", "ECX"};
    for (unsigned j = 0; j < 3u; ++j)
        if ((mask >> j & 1u) && out[0][j] != out[1][j]) {
            fprintf(stderr, "FAIL: %s case %u: %s original 0x%08lx own 0x%08lx\n", label, k, names[j], (unsigned long)out[0][j],
                    (unsigned long)out[1][j]);
            return 1;
        }
    return 0;
}
static void sr_entry_regs(uint32_t in[3]) { in[0] = random32(); in[1] = random32(); in[2] = random32(); }

/* ---- 0x430518 and 0x430590 directly */
static int sr_runs_case(unsigned k) {
    sr_begin();
    uint32_t n = sr_rand(8u) == 0 ? 0u : 1u + sr_rand(sr_rand(4u) ? 2u : 6u), a = sr_rand(0x20u - n + 1u), len = sr_length();
    sr_w16(SR_R, random32());
    sr_w16(SR_R + 2u, len);
    uint32_t args[3] = {a, n, sr_at(SR_R)}, in[3];
    sr_entry_regs(in);
    ++sr_cov_runs[n == 0 ? 0 : n == 1u ? 1 : 2];
    sr_cov_runs[3] += (int32_t)(int16_t)len <= 0;
    return sr_run("runs", k, 0x00430518u, args, 3, in, 7u);
}
/* n records R + 6i: count word, list pointer; list i's entry i points to the pairs. */
static void sr_span_records(uint32_t r, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t count = sr_rand(10u) == 0 ? (uint32_t)-(int32_t)(1u + sr_rand(4u)) : sr_rand(9u);
        sr_w16(r + 6u * i, count);
        sr_w32(r + 6u * i + 2u, sr_at(SR_LISTS + 0x20u * i));
        for (uint32_t j = 0; j < 8u; ++j) sr_w32(SR_LISTS + 0x20u * i + 4u * j, sr_at(SR_SPAIRS + 0x40u * sr_rand(8u) + 4u * sr_rand(4u)));
    }
    for (uint32_t i = 0; i < 0x200u; i += 4u) {
        sr_w16(SR_SPAIRS + i, random32());
        sr_w16(SR_SPAIRS + i + 2u, sr_rand(4u) ? sr_rand(0x30u) : sr_length());
    }
}
static int sr_spans_case(unsigned k) {
    sr_begin();
    uint32_t n = sr_rand(8u) == 0 ? 0u : 1u + sr_rand(sr_rand(2u) ? 1u : 6u), a = sr_rand(0x20u - n + 1u);
    sr_span_records(SR_R, n);
    uint32_t args[3] = {a, n, sr_at(SR_R)}, in[3];
    sr_entry_regs(in);
    ++sr_cov_spans[n == 0 ? 0 : n == 1u ? 1 : 2];
    return sr_run("spans", k, 0x00430590u, args, 3, in, 7u);
}

/* ---- the object world of 0x42e878 and 0x42e8e8 */
static uint32_t sr_kind(void) {
    uint32_t r = sr_rand(10u);
    return r < 3u ? 5u : r == 3u ? 0u : r == 4u ? (uint32_t)-(int32_t)(1u + sr_rand(4u)) : sr_rand(8u);
}
static void sr_object(uint32_t kind) {
    sr_w32(SR_OBJ, sr_at(SR_VT));
    sr_w16(SR_OBJ + 4u, kind);
    sr_w32(SR_VT + 0x10u, (uint32_t)(uintptr_t)sr_slot);
    sr_slot_answer = sr_rand(3u) ? 4900u + sr_rand(200u) : random32();
    for (uint32_t t = 0; t < 2u; ++t) {
        uint32_t v = sr_rand(4u) == 0 ? random32() : sr_rand(0x100u);
        for (uint32_t i = 0; i < 16u; ++i) {
            sr_w32(SR_THR + 0x40u * t + 4u * i, v);
            v += sr_rand(4u) == 0 ? 0u : sr_rand(0x400u);
        }
    }
    sd_set32(0x004cfd04u, sr_at(SR_THR));
    sd_set32(0x004cfd00u, sr_at(SR_THR + 0x40u));
    sd_set16(0x004cfcdau, sr_rand(8u) == 0 ? 0u : 1u + sr_rand(8u));
    sd_set16(0x004cfcd8u, sr_rand(8u) == 0 ? 0u : 1u + sr_rand(8u));
    sd_set32(0x004cfce8u, sr_signed(0x800u));
    sd_set32(0x004cfcecu, sr_signed(0x800u));
}
static uint32_t sr_level(void) {
    uint32_t r = sr_rand(6u);
    return r == 0 ? random32() : r == 1u ? (uint32_t)-(int32_t)sr_rand(0x800u) : sr_rand(0x2000u);
}
static int sr_level_case(unsigned k) {
    sr_begin();
    uint32_t kind = sr_kind(), level = sr_level();
    sr_object(kind);
    uint32_t args[2] = {sr_at(SR_OBJ), level}, in[3];
    sr_entry_regs(in);
    ++sr_cov_level[kind == 0 ? 0 : kind == 5u ? 1 : (int32_t)kind < 0 ? 2 : 3];
    sr_cov_level[4] += kind == 5u && (int32_t)(int16_t)sr_slot_answer >= 5000;
    return sr_run("level", k, 0x0042e878u, args, 2, in, 7u);
}
static int sr_default_case(unsigned k) {
    sr_begin();
    uint32_t in[3];
    sr_entry_regs(in);
    return sr_run("default", k, 0x0042ea0cu, NULL, 0, in, 7u);
}
static int sr_select_case(unsigned k) {
    sr_begin();
    uint32_t kind = sr_kind(), level = sr_level(), dflt = sr_rand(16u) == 0, flag;
    uint32_t r = sr_rand(4u);
    flag = r == 0 ? 0u : r == 1u ? 0x100u * (1u + sr_rand(0xffu)) : r == 2u ? 1u : random32();
    sr_object(kind);
    sd_set8(0x0049aef2u, dflt);
    sd_set32(0x004cfce0u, sr_at(SR_KINDS + 0x28u));
    for (uint32_t i = 0; i < 0x30u; ++i) {
        uint32_t e = SR_KINDS + 5u * i;
        sr_w8(e, sr_rand(6u) == 0 ? 0u : sr_rand(5u) == 0 ? 1u + sr_rand(8u) : 1u + sr_rand(3u));
        sr_w32(e + 1u, sr_at(SR_BYTEPAIRS + 0x20u * sr_rand(8u)));
    }
    for (uint32_t i = 0; i < 0x100u; i += 2u) {
        sr_w8(SR_BYTEPAIRS + i, sr_rand(0x20u));
        sr_w8(SR_BYTEPAIRS + i + 1u, sr_rand(8u));
    }
    uint32_t stride = sr_rand(5u) == 0 ? (uint32_t)-(int32_t)(1u + sr_rand(4u)) : sr_rand(5u);
    sd_set16(0x004cfcdeu, stride);
    sd_set32(0x004cfcf8u, sr_at(SR_P));
    sd_set32(0x004cfcfcu, sr_at(SR_Q));
    for (uint32_t i = SR_RUNS; i < SR_Q + 0x100u; i += 4u) {
        sr_w16(i, random32());
        sr_w16(i + 2u, sr_length());
    }
    uint32_t args[3] = {sr_at(SR_OBJ), level, flag}, in[3];
    sr_entry_regs(in);
    uint32_t count = sr_world_start[(SR_KINDS + 0x28u + 5u * (uint32_t)(int32_t)(int16_t)kind) & 0xffffu];
    ++sr_cov_select[dflt ? 0 : count == 0 ? 1 : (flag & 0xffu) ? 2 : 3];
    sr_cov_select[4] += !dflt && count && kind == 5u;
    sr_cov_select[5] += !dflt && count && (int32_t)(int16_t)stride < 0;
    sr_cov_select[6] += !dflt && count && !(flag & 0xffu) && flag;
    return sr_run("select", k, 0x0042e8e8u, args, 3, in, 7u);
}

/* ---- 0x48cce4 */
static int sr_project_case(unsigned k) {
    sr_begin();
    uint32_t shift = sr_rand(3u) == 0 ? 0u : 1u + sr_rand(12u), near_y = 1u + sr_rand(0x100u);
    int32_t div;
    do div = (int32_t)sr_signed(0x400u); while (div == 0 || div == -1);
    sd_set32(0x006c6028u, shift);
    sd_set32(0x006c602cu, near_y);
    sd_set32(0x006c6030u, (uint32_t)div);
    sd_set32(0x006c6034u, sr_signed(0x200u));
    sd_set32(0x006c6038u, sr_signed(0x200u));
    uint32_t count = sr_rand(8u) == 0 ? sr_rand(3u) : 3u + sr_rand(30u), first = sr_rand(16u);
    sd_set16(0x006c68e4u, count);
    sd_set16(0x006c68e6u, first);
    sd_set32(0x006c68f2u, sr_at(SR_INDEX));
    sd_set32(0x006c68ecu, sr_at(SR_PTS));
    for (uint32_t i = 0; i < 64u; ++i) sr_w16(SR_INDEX + 2u * i, sr_rand(64u));
    int behind = sr_rand(2u);
    for (uint32_t i = 0; i < 64u; ++i) {
        uint32_t p = SR_PTS + 12u * i;
        sr_w32(p, sr_signed(0x4000u));
        sr_w32(p + 4u, behind && sr_rand(4u) == 0 ? near_y - sr_rand(near_y + 0x40u) : near_y + sr_rand(0x4000u));
        sr_w32(p + 8u, sr_signed(0x4000u));
        uint32_t s = sr_rand(5u);
        sd_set8(0x006c68fau + i, s == 4u ? 0u : s);
        sd_set32(0x006c9bc2u + 8u * i, random32());
        sd_set32(0x006c9bc2u + 8u * i + 4u, random32());
    }
    uint32_t in[3];
    sr_entry_regs(in);
    ++sr_cov_project[shift ? 0 : 1];
    sr_cov_project[2] += count < 3u;
    int bad = sr_run("project", k, 0x0048cce4u, NULL, 0, in, shift ? 7u : 5u);
    if (!bad) {
        ++sr_cov_project[sr_out[0][0] ? 3 : 4];
        sr_cov_project[5] += count > 16u;
    }
    return bad;
}

static int shade_ranges_cases(void) {
    current_function = "shade ranges";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    sr_world = VirtualAlloc(NULL, SR_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    sr_world_start = malloc(2u * SR_WORLD_SIZE);
    sr_dest = VirtualAlloc(NULL, SR_DEST_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    sr_dest_start = malloc(2u * SR_DEST_SIZE);
    /* The stack at a low address: its high word is a run length (0x42e8e8's flag path). */
    for (uint32_t hint = 0x00100000u; !sr_stack && hint < 0x08000000u; hint += 0x10000u)
        sr_stack = VirtualAlloc((void *)(uintptr_t)hint, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !sr_world || !sr_world_start || !sr_dest ||
        !sr_dest_start || !sr_stack)
        return fail("shade ranges: memory");
    if (((uint32_t)(uintptr_t)sr_stack >> 16) > 0x800u) return fail("shade ranges: the stack is not low enough");
    su_pristine_take();
    if (sr_setup()) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"runs", sr_runs_case, 2500}, {"spans", sr_spans_case, 2500}, {"level", sr_level_case, 2500},
        {"default", sr_default_case, 200}, {"select", sr_select_case, 4000}, {"project", sr_project_case, 2500},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: shade ranges %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) {
        printf("SHADE RANGES coverage: runs n=0 %u n=1 %u n>1 %u, length <= 0 %u; spans n=0 %u n=1 %u n>1 %u; arguments at 0x%08lx..\n",
               sr_cov_runs[0], sr_cov_runs[1], sr_cov_runs[2], sr_cov_runs[3], sr_cov_spans[0], sr_cov_spans[1], sr_cov_spans[2],
               (unsigned long)((uint32_t)(uintptr_t)sr_stack + 0x8000u));
        printf("SHADE RANGES coverage: level kind 0 %u kind 5 %u negative %u other %u, slot answer >= 5000 %u\n", sr_cov_level[0], sr_cov_level[1],
               sr_cov_level[2], sr_cov_level[3], sr_cov_level[4]);
        printf("SHADE RANGES coverage: select default %u empty entry %u flag path %u run path %u, kind 5 %u, negative stride %u, flag low byte 0 but set %u\n",
               sr_cov_select[0], sr_cov_select[1], sr_cov_select[2], sr_cov_select[3], sr_cov_select[4], sr_cov_select[5], sr_cov_select[6]);
        printf("SHADE RANGES coverage: project shift %u divisor %u, n<3 %u, result 1 %u result 0 %u, n>16 %u\n", sr_cov_project[0],
               sr_cov_project[1], sr_cov_project[2], sr_cov_project[3], sr_cov_project[4], sr_cov_project[5]);
    }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
