/* The rows of sim/dispatch/services/presentation_roots.h (sim/src/present.c,
 * presentation.md, "the help call and the DirectDraw release"), original
 * machine code against the own rows (target
 * :differential_presentation_roots_test): the help call 0x4669c4 (row help_contents) and the
 * DirectDraw release 0x466730 (row dd_release). It reuses startup_cases.h's
 * snapshots and loop_cases.h's loop, as presentation_loads_cases.h does. Identifiers use
 * pq_/PQ_.
 *
 * Each case is 1..6 calls of the two rows. Before every call both sides get the
 * same seeded state: DirectDraw's dword [0x4d255e] (0, 1 or a random value),
 * the window handle [0x4d3610], and each of the four object dwords
 * 0x4d3710..0x4d371c either 0 or one of four objects in the case's world, whose
 * vtable's slot +8 is a recording stdcall Release stub. The help call's file
 * argument and the entry EAX, EDX and ECX are random. The callees are recording
 * stubs on both sides: the switch 0x4667c8 (fullscreen.h's row) and the
 * frame clear 0x46548c (mission_load.h's row), both replaced after the seal, and Release (on
 * the own side a service at the stub's host address); on the original side
 * USER32's WinHelpA, GetSystemMetrics and ClipCursor are import stubs, on the
 * own side the present instance's platform record is read back into the same
 * log (WinHelpA's four arguments, two GetSystemMetrics calls with SM_CXSCREEN
 * and SM_CYSCREEN, ClipCursor's rectangle). GetSystemMetrics answers a seeded
 * screen size (the own platform's screen_cx/screen_cy), WinHelpA a seeded
 * answer (the own win_help_answer), ClipCursor TRUE. Compared: EAX of every
 * call, DATA 0x497000..0x6cc000 with the heap, the stub log. EDX and ECX are
 * not compared: the original leaves the system calls' (WinHelpA, ClipCursor),
 * the own rows the entry's; the callers 0x4345d1 and 0x460827 pop ECX and do
 * not read EDX, WinMain 0x46547a and 0x4667c8 (0x466944) do not read either
 * (presentation.md, "the help call and the DirectDraw release"). Coverage:
 * counters from the original side; every one
 * must be non-zero. */
#pragma once
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/present.h"

#define PQ_WORLD_SIZE 0x1000u
#define PQ_OBJECTS 0x0000u   /* four objects of 0x10 */
#define PQ_VTABLE 0x0100u

static es2_sim_entry pq_entries[4096];
static es2_sim_dispatch pq_dispatch;
static es2_sim_present pq_present;
static BYTE *pq_world;
static int pq_bad;
static unsigned pq_side;
static uint32_t pq_va(uint32_t off) { return (uint32_t)(uintptr_t)(pq_world + off); }
static uint32_t pq_r(uint32_t n) { return random32() % n; }

/* ---- the original side's register thunk: pushes the arguments, enters
 * with EAX/EDX/ECX from in[3], stores EAX/EDX/ECX into out[3]. */
extern void pq_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _pq_call\n_pq_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

/* ---- the stub log of a side */
static uint64_t pq_hash[2];
static unsigned pq_count[2];
static char pq_text[2][2048];
static size_t pq_len[2];
static void pq_note(char letter, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    const uint32_t v[4] = {a, b, c, d};
    uint64_t h = pq_hash[pq_side] ^ (uint32_t)letter;
    h *= 0x100000001b3ull;
    for (unsigned i = 0; i < 4u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    pq_hash[pq_side] = h;
    ++pq_count[pq_side];
    if (pq_len[pq_side] + 64u < sizeof(pq_text[0]))
        pq_len[pq_side] += (size_t)sprintf(pq_text[pq_side] + pq_len[pq_side], "%c(%lx,%lx,%lx,%lx) ", letter, (unsigned long)a,
                                           (unsigned long)b, (unsigned long)c, (unsigned long)d);
}
static uint32_t pq_object_index(uint32_t o) { return (o - pq_va(PQ_OBJECTS)) / 0x10u; }

/* ---- the answers of a call */
static uint32_t pq_screen[2], pq_help_answer;

/* ---- the original side's stubs */
static void __stdcall pq_o_release(uint32_t self) { pq_note('R', pq_object_index(self), 0, 0, 0); }
static uint32_t __cdecl pq_o_switch(void) { pq_note('S', 0, 0, 0, 0); return 0x0badf00du; }
static uint32_t __cdecl pq_o_clear(void) { pq_note('C', 0, 0, 0, 0); return 0x0badf00du; }
static uint32_t __stdcall pq_o_win_help(uint32_t w, uint32_t file, uint32_t command, uint32_t data) {
    pq_note('H', w, file, command, data);
    return pq_help_answer;
}
static uint32_t __stdcall pq_o_metrics(uint32_t index) {
    pq_note('M', index, 0, 0, 0);
    return index == 0 ? pq_screen[0] : index == 1u ? pq_screen[1] : 0x7777u;
}
static uint32_t __stdcall pq_o_clip(const int32_t *r) {
    pq_note('K', (uint32_t)r[0], (uint32_t)r[1], (uint32_t)r[2], (uint32_t)r[3]);
    return 1;
}
/* ---- the own side's stubs */
static void pq_s_release(es2_sim_service_call *sc, void *user) { (void)user; pq_note('R', pq_object_index(sc->slots[0]), 0, 0, 0); }
static void pq_s_switch(es2_sim_service_call *sc, void *user) { (void)user; pq_note('S', 0, 0, 0, 0); sc->eax = 0x0badf00du; }
static void pq_s_clear(es2_sim_service_call *sc, void *user) { (void)user; pq_note('C', 0, 0, 0, 0); sc->eax = 0x0badf00du; }

static const struct { uint32_t va; void *stub; } pq_patches[2] = {
    {0x004667c8u, (void *)pq_o_switch}, {0x0046548cu, (void *)pq_o_clear},
};
static BYTE pq_saved[2][5];
static int pq_patch(unsigned i, int install) {
    BYTE *site = mapped + (pq_patches[i].va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("pr-03: stub protection");
    if (install) {
        memcpy(pq_saved[i], site, 5);
        site[0] = 0xe9;
        put32(site + 1, (uint32_t)(uintptr_t)pq_patches[i].stub - (uint32_t)(uintptr_t)(site + 5));
    } else memcpy(site, pq_saved[i], 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("pr-03: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}

/* ---- the own side */
static void pq_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own pr-03: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    pq_bad = 1;
}
static int pq_dispatch_setup(void) {
    es2_sim_dispatch_init(&pq_dispatch, pq_entries, 4096, pq_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&pq_dispatch, &s)) return fail("pr-03: dispatch");
    if (es2_sim_dispatch_add_service(&pq_dispatch, (uint32_t)(uintptr_t)pq_o_release, "pr-03 Release stub", pq_s_release, NULL, 4, 1))
        return fail("pr-03: dispatch (stubs)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&pq_dispatch, &error, &bad)) return fail("pr-03: dispatch seal");
    es2_sim_entry *clear = (es2_sim_entry *)es2_sim_dispatch_find(&pq_dispatch, 0x0046548cu);
    if (!clear || clear->kind != ES2_SIM_SERVICE) return fail("pr-03: the frame clear row is missing");
    clear->service = pq_s_clear;
    clear->service_user = NULL;
    es2_sim_entry *sw = (es2_sim_entry *)es2_sim_dispatch_find(&pq_dispatch, 0x004667c8u);
    if (!sw || sw->kind != ES2_SIM_SERVICE) return fail("pr-03: the switch row is missing");
    sw->service = pq_s_switch;
    sw->service_user = NULL;
    const uint32_t rows[] = {0x004669c4u, 0x00466730u};
    for (unsigned i = 0; i < 2u; ++i) {
        const es2_sim_entry *e = es2_sim_dispatch_find(&pq_dispatch, rows[i]);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("pr-03: a row is missing");
    }
    return 0;
}
static void pq_side_begin(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &pq_dispatch;
    pq_side = side;
    pq_hash[side] = 0xcbf29ce484222325ull;
    pq_count[side] = 0;
    pq_len[side] = 0;
    pq_text[side][0] = 0;
    if (side) {
        es2_sim_present_headless(&pq_present, &lc_loop);
        pq_present.platform.screen_cx = pq_screen[0];
        pq_present.platform.screen_cy = pq_screen[1];
        pq_present.platform.win_help_answer = pq_help_answer;
        es2_sim_present_bind(&pq_present);
    }
}
/* The own platform record's new calls since `before`, into the log in the
 * original's order. */
static void pq_own_platform(const es2_sim_present_platform *before) {
    const es2_sim_present_platform *p = &pq_present.platform;
    if (p->win_help != before->win_help)
        pq_note('H', p->win_help_args[0], p->win_help_args[1], p->win_help_args[2], p->win_help_args[3]);
    for (unsigned i = before->system_metrics; i < p->system_metrics; ++i) pq_note('M', (i - before->system_metrics) & 1u, 0, 0, 0);
    if (p->clip_cursor != before->clip_cursor)
        pq_note('K', (uint32_t)p->clip_rect[0], (uint32_t)p->clip_rect[1], (uint32_t)p->clip_rect[2], (uint32_t)p->clip_rect[3]);
}

/* ---- coverage */
enum { PQC_HELP_GDI, PQC_HELP_DD, PQC_CLEAR, PQC_NO_CLEAR, PQC_OBJ0, PQC_OBJ1, PQC_OBJ2, PQC_OBJ3, PQC_NONE0, PQC_NONE1, PQC_NONE2,
       PQC_NONE3, PQC_COUNT };
static const char *const pq_cv_names[PQC_COUNT] = {"help GDI", "help DirectDraw", "clear", "no clear", "release 0x4d3710",
                                                   "release 0x4d3714", "release 0x4d3718", "release 0x4d371c", "none 0x4d3710",
                                                   "none 0x4d3714", "none 0x4d3718", "none 0x4d371c"};
static unsigned pq_cv[PQC_COUNT];

static int pq_case(unsigned k) {
    unsigned n = 1u + pq_r(6u);
    uint32_t va[6], file[6], in[6][3], dd[6], hwnd[6], objects[6][4], eax[2][6];
    for (unsigned i = 0; i < n; ++i) {
        va[i] = pq_r(2u) ? 0x004669c4u : 0x00466730u;
        file[i] = random32();
        for (unsigned j = 0; j < 3u; ++j) in[i][j] = random32();
        uint32_t r = pq_r(4u);
        dd[i] = r < 2u ? 0 : r == 2u ? 1u : random32();
        hwnd[i] = random32();
        for (unsigned j = 0; j < 4u; ++j) objects[i][j] = pq_r(2u) ? pq_va(PQ_OBJECTS + 0x10u * pq_r(4u)) : 0;
    }
    pq_screen[0] = pq_r(3u) ? 640u : 1u + random32() % 4095u;
    pq_screen[1] = pq_r(3u) ? 480u : 1u + random32() % 4095u;
    pq_help_answer = random32();
    for (unsigned side = 0; side < 2u; ++side) {
        pq_side_begin(side);
        for (unsigned i = 0; i < n && !pq_bad && !lc_bad; ++i) {
            put32(mapped + (0x004d255eu - 0x400000u), dd[i]);
            put32(mapped + (0x004d3610u - 0x400000u), hwnd[i]);
            for (unsigned j = 0; j < 4u; ++j) put32(mapped + (0x004d3710u + 4u * j - 0x400000u), objects[i][j]);
            const uint32_t *args = va[i] == 0x004669c4u ? &file[i] : NULL;
            unsigned count = va[i] == 0x004669c4u ? 1u : 0u;
            if (!side) {
                uint32_t out[3];
                pq_call((uint32_t)(uintptr_t)(mapped + (va[i] - 0x400000u)), args, count, in[i], out);
                eax[0][i] = out[0];
                if (va[i] == 0x004669c4u) ++pq_cv[dd[i] ? PQC_HELP_DD : PQC_HELP_GDI];
                else {
                    ++pq_cv[objects[i][0] ? PQC_CLEAR : PQC_NO_CLEAR];
                    for (unsigned j = 0; j < 4u; ++j) ++pq_cv[(objects[i][j] ? PQC_OBJ0 : PQC_NONE0) + j];
                }
            } else {
                es2_sim_present_platform before = pq_present.platform;
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&pq_dispatch, va[i], args, count, in[i], &r)) pq_bad = 1;
                eax[1][i] = r.eax;
                pq_own_platform(&before);
            }
        }
        if (pq_bad || lc_bad) { fprintf(stderr, "FAIL: pr-03 case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < n; ++i)
        if (eax[0][i] != eax[1][i]) {
            fprintf(stderr, "FAIL: pr-03 case %u: 0x%08lx (call %u) EAX %08lx / %08lx\n", k, (unsigned long)va[i], i,
                    (unsigned long)eax[0][i], (unsigned long)eax[1][i]);
            return 1;
        }
    if (su_compare("pr-03", k)) return 1;
    if (pq_hash[0] != pq_hash[1] || pq_count[0] != pq_count[1]) {
        fprintf(stderr, "FAIL: pr-03 case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, pq_count[0], pq_count[1],
                pq_text[0], pq_text[1]);
        return 1;
    }
    return 0;
}

static int presentation_roots_cases(void) {
    current_function = "pr-03";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    pq_world = VirtualAlloc(NULL, PQ_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !pq_world) return fail("pr-03: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("pr-03: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"USER32.dll", "WinHelpA", (void *)pq_o_win_help},
        {"USER32.dll", "GetSystemMetrics", (void *)pq_o_metrics},
        {"USER32.dll", "ClipCursor", (void *)pq_o_clip},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("pr-03: import slot not found");
        }
    for (unsigned i = 0; i < 2u; ++i)
        if (pq_patch(i, 1)) return 1;
    if (pq_dispatch_setup()) return 1;
    /* the objects and their vtable (the same on both sides) */
    for (uint32_t j = 0; j < 4u; ++j) put32(pq_world + PQ_OBJECTS + 0x10u * j, pq_va(PQ_VTABLE));
    put32(pq_world + PQ_VTABLE + 8u, (uint32_t)(uintptr_t)pq_o_release);
    int bad = 0;
    const unsigned cases = 2000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = pq_case(k);
    }
    if (!bad) {
        printf("PASS: pr-03: %u cases identical\n", cases);
        printf("PR-03 coverage:");
        for (unsigned i = 0; i < PQC_COUNT; ++i) printf(" %s %u%s", pq_cv_names[i], pq_cv[i], i + 1u < PQC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < PQC_COUNT; ++i)
            if (!pq_cv[i]) { fprintf(stderr, "FAIL: pr-03: path '%s' never taken\n", pq_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < 2u; ++i) pq_patch(i, 0);
    es2_sim_present_bind(NULL);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
