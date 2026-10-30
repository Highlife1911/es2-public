/* The four rows of sim/dispatch/services/adapter_leaves.h
 * (sim/src/adapter_leaves.c), original machine code against the own rows:
 * the camera mode 0x401148, the flyer slot address 0x423044 and the sweep
 * word's clear 0x427d98 and read 0x427da0. It reuses startup_cases.h's
 * snapshots and loop_cases.h's loop, as gap_roots_sim_cases.h does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same seeded world: four cameras of random bytes. Before each row the
 * camera's dword +0x4a (0 or random), the flyer table dword 0x4a9dfc and
 * the sweep dword 0x4aaad0 are drawn again; the mode and slot arguments are
 * drawn from -2..5 and random dwords (the rows read their low words
 * sign-extended). Then 1..8 of the rows run, each with random entry EAX,
 * EDX and ECX. None of the rows calls anything. Compared: EAX, EDX and ECX
 * of every call, DATA 0x497000..0x6cc000 and the heap. Coverage: counters
 * from the original side; every one must be non-zero. Identifiers use the
 * prefix al_/AL_. */
#pragma once
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"

static es2_sim_entry al_entries[4096];
static es2_sim_dispatch al_dispatch;
static int al_bad;

/* ---- the original side's register thunk: pushes the arguments, enters
 * with EAX/EDX/ECX from in[3], stores EAX/EDX/ECX into out[3]. */
extern void al_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _al_call\n_al_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

static uint32_t al_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static void al_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void al_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }

enum { ALC_LOCKED, ALC_MODE0, ALC_MODE1, ALC_MODE2, ALC_MODE3, ALC_OTHER, ALC_NEGATIVE, ALC_HIGH_WORD, ALC_SLOT_NEGATIVE,
       ALC_COUNT };
static const char *const al_cv_names[ALC_COUNT] = {
    "camera locked", "mode 0", "mode 1", "mode 2", "mode 3", "mode above 3", "mode negative", "upper half set",
    "slot negative",
};
static unsigned al_cv[ALC_COUNT];

static void al_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own adapter leaves: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    al_bad = 1;
}
static int al_dispatch_setup(void) {
    es2_sim_dispatch_init(&al_dispatch, al_entries, 4096, al_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&al_dispatch, &s)) return fail("adapter leaves: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&al_dispatch, &error, &bad)) return fail("adapter leaves: dispatch seal");
    return 0;
}

/* ---- the synthetic world */
static uint32_t al_seed;
static uint32_t al_rand(uint32_t n) {
    al_seed = al_seed * 1103515245u + 12345u;
    return (al_seed >> 8) % n;
}
static uint32_t al_word(void) { return al_rand(65536u) << 16 | al_rand(65536u); }
static uint32_t al_cam[4], al_cam0[4];
static void al_build(void) {
    for (unsigned k = 0; k < 4u; ++k) {
        al_cam[k] = (uint32_t)(uintptr_t)ir_calloc(1, 0x60u);
        for (uint32_t i = 0; i < 0x60u; ++i) al_put8(al_cam[k] + i, (uint8_t)al_rand(256u));
    }
}
static void al_state(uint32_t cam) {
    al_put32(cam + 0x4au, al_rand(3u) ? 0 : al_word());
    al_put32(0x004a9dfcu, al_word());
    al_put32(0x004aaad0u, al_word());
}

/* ---- the rows */
enum { AL_CAMERA, AL_FLYER, AL_CLEAR, AL_READ, AL_OPS };
static const struct { uint32_t va; unsigned n; const char *name; } al_ops[AL_OPS] = {
    {0x00401148u, 2, "camera mode"}, {0x00423044u, 2, "flyer slot"}, {0x00427d98u, 0, "sweep clear"}, {0x00427da0u, 0, "sweep read"},
};
typedef struct al_op { unsigned op; uint32_t a[2], in[3]; } al_op;
static unsigned al_calls[AL_OPS];
static uint32_t al_small(void) {
    static const int32_t values[] = {-2, -1, 0, 1, 2, 3, 4, 5};
    uint32_t v = random32() % 3u ? (uint32_t)values[random32() % 8u] : random32();
    if (random32() % 4u == 0) v = (v & 0xffffu) | (random32() << 16);
    return v;
}
static void al_args(al_op *o) {
    o->a[0] = o->op == AL_CAMERA ? al_cam[random32() % 4u] : al_small();
    o->a[1] = al_small();
    for (unsigned k = 0; k < 3u; ++k) o->in[k] = random32();
}

static int al_side_begin(unsigned side, uint32_t seed) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &al_dispatch;
    al_seed = seed;
    al_build();
    if (side) {
        if (memcmp(al_cam, al_cam0, sizeof(al_cam))) return fail("adapter leaves: the sides' worlds differ (heap order)");
    } else {
        memcpy(al_cam0, al_cam, sizeof(al_cam));
    }
    return 0;
}
static void al_cover(const al_op *o) {
    if (o->op == AL_FLYER) {
        if ((int16_t)(uint16_t)o->a[1] < 0) ++al_cv[ALC_SLOT_NEGATIVE];
        return;
    }
    if (o->op != AL_CAMERA) return;
    int32_t mode = (int16_t)(uint16_t)o->a[1];
    if (o->a[1] >> 16) ++al_cv[ALC_HIGH_WORD];
    if (al_get(o->a[0] + 0x4au)) { ++al_cv[ALC_LOCKED]; return; }
    if (mode < 0) ++al_cv[ALC_NEGATIVE];
    else if (mode > 3) ++al_cv[ALC_OTHER];
    else ++al_cv[ALC_MODE0 + (unsigned)mode];
}
static int al_case(unsigned k) {
    unsigned ops = 1u + random32() % 8u;
    static al_op list[8];
    uint32_t seed = random32();
    static uint32_t reg[2][8][3];
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % AL_OPS;
    for (unsigned side = 0; side < 2u; ++side) {
        if (al_side_begin(side, seed)) return 1;
        for (unsigned i = 0; i < ops && !al_bad && !lc_bad; ++i) {
            al_op *o = &list[i];
            if (side == 0) al_args(o);
            al_state(o->op == AL_CAMERA ? o->a[0] : al_cam[0]);
            if (side == 0) {
                al_cover(o);
                al_call((uint32_t)(uintptr_t)(mapped + (al_ops[o->op].va - 0x400000u)), o->a, al_ops[o->op].n, o->in, reg[0][i]);
                ++al_calls[o->op];
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&al_dispatch, al_ops[o->op].va, o->a, al_ops[o->op].n, o->in, &r)) al_bad = 1;
                reg[1][i][0] = r.eax; reg[1][i][1] = r.edx; reg[1][i][2] = r.ecx;
            }
        }
        if (al_bad || lc_bad) { fprintf(stderr, "FAIL: adapter leaves case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (memcmp(reg[0][i], reg[1][i], sizeof(reg[0][i]))) {
            fprintf(stderr, "FAIL: adapter leaves case %u: %s (call %u, args %08lx %08lx) EAX %08lx / %08lx, EDX %08lx / %08lx, ECX %08lx / %08lx\n",
                    k, al_ops[list[i].op].name, i, (unsigned long)list[i].a[0], (unsigned long)list[i].a[1], (unsigned long)reg[0][i][0],
                    (unsigned long)reg[1][i][0], (unsigned long)reg[0][i][1], (unsigned long)reg[1][i][1], (unsigned long)reg[0][i][2],
                    (unsigned long)reg[1][i][2]);
            return 1;
        }
    if (su_compare("adapter leaves", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", al_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    return 0;
}

static int adapter_leaves_cases(void) {
    current_function = "adapter leaves";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("adapter leaves: memory");
    su_pristine_take();
    if (al_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 2000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = al_case(k);
    }
    if (!bad) {
        printf("PASS: adapter leaves: %u cases identical\n", cases);
        printf("Adapter leaves called:");
        for (unsigned i = 0; i < AL_OPS; ++i) printf(" %s %u%s", al_ops[i].name, al_calls[i], i + 1u < AL_OPS ? "," : "\n");
        printf("Adapter leaves coverage:");
        for (unsigned i = 0; i < ALC_COUNT; ++i) printf(" %s %u%s", al_cv_names[i], al_cv[i], i + 1u < ALC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < AL_OPS; ++i)
            if (!al_calls[i]) { fprintf(stderr, "FAIL: adapter leaves: row '%s' never called\n", al_ops[i].name); bad = 1; }
        for (unsigned i = 0; i < ALC_COUNT; ++i)
            if (!al_cv[i]) { fprintf(stderr, "FAIL: adapter leaves: path '%s' never taken\n", al_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
