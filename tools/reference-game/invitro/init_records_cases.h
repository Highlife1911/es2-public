/* DBSIM's static initialisation, original against own code (--init-records;
 * target :differential_init_records_test).
 *
 * Two passes over the mapped image, each from the same start: DATA
 * 0x497000..0x6cc000 as the file maps it, the two dwords below the worker
 * thread's StackBase as the thread had them, an empty heap over an emulated
 * OS region at the same address, FS:0 as before.
 *
 * - original: the c0w32 entry 0x401000 runs as machine code (its jump into
 *   CW3220's __startup returns to the harness through a stub), then CW3220's
 *   selector (the model es2_rtl_select_record, confirmed against
 *   CW3220 in vitro there) marks and the harness machine-calls the 65 init
 *   records, then the 26 exit records. The import slots the path reaches
 *   point at logging stubs: _memset, _memmove, _calloc and _free (the
 *   heap model over the emulated region), CreateFileA (-1), CloseHandle
 *   (refused), GetModuleHandleA (0x400000) and __startup; every other slot
 *   stays a trap.
 * - own: sim/src/init.c over the identity view, its calls answered by the
 *   sim services (rtl_imports.h and kernel_calls.h rows) through es2_sim_call, the same heap
 *   model over the same region.
 *
 * After every step (the entry, each record) a hash of DATA, the two stack
 * words, the heap head and the first 64 KiB of the region is taken; the two
 * sequences, the selected VAs and the import-level call logs (memset, memmove,
 * calloc, free, CreateFileA with its arguments, CloseHandle, GetModuleHandleA)
 * must be equal. At the first differing step both passes run again up to it and
 * the differing bytes are printed. The original pass also prints, per step, the
 * bytes it changed and the counter delta (the evidence for own_process.md, "the
 * static initialisation as own code"). Finally a probe calls the real
 * CreateFileA with record 64's arguments and prints its result (the headless
 * service answers -1). */
#pragma once
#include "es2/sim/dispatch.h"
#include "es2/sim/heap.h"
#include "es2/sim/init.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"
#include "es2/core/rtl_start.h"

#define IR_DATA_RVA 0x97000u
#define IR_DATA_SIZE 0x235000u            /* DATA, 0x497000..0x6cc000 */
#define IR_REGION_SIZE 0x01000000u        /* the emulated OS's address space for the heap */
#define IR_REGION_COMPARED 0x10000u       /* the part hashed and compared (the heap uses less; checked) */
#define IR_STEPS (1u + ES2_SIM_INIT_RECORDS + ES2_SIM_EXIT_RECORDS)
#define IR_LOG 64u

static BYTE *ir_pristine, *ir_region, *ir_snapshot[2], *ir_previous;
static uint32_t ir_cursor, ir_stack_base, ir_stack_saved[2];
static uint32_t ir_head[4];
static es2_sim_heap ir_heap;
static int ir_failed;
/* Import-level call log of a pass: one letter per call (G module handle,
 * S memset, M memmove, C calloc, F free, V CreateFileA, X CloseHandle). */
static char ir_log[2][IR_LOG];
static unsigned ir_log_count[2], ir_side;
static uint32_t ir_create_args[2][7];
static char ir_create_name[2][64];
static uint32_t ir_selected[2][IR_STEPS];
static uint64_t ir_hash[2][IR_STEPS];

static uint32_t ir_fs0(void) { uint32_t v; __asm__ volatile("movl %%fs:0, %0" : "=r"(v)); return v; }
static void ir_note(char letter) {
    if (ir_log_count[ir_side] + 1 < IR_LOG) ir_log[ir_side][ir_log_count[ir_side]++] = letter;
}

/* ---- the emulated OS of both heaps: one committed region, reservations
 * handed out from its start in order, decommits zero-filled. */
static uint32_t ir_os_alloc(void *c, uint32_t address, uint32_t size, uint32_t type, uint32_t protect) {
    (void)c; (void)protect;
    uint32_t base = (uint32_t)(uintptr_t)ir_region;
    if (type & MEM_RESERVE) {
        if (address) return 0;
        uint32_t granular = (size + 0xffffu) & ~0xffffu;
        if (granular > IR_REGION_SIZE - ir_cursor) return 0;
        uint32_t at = base + ir_cursor;
        ir_cursor += granular;
        return at;
    }
    if (address - base >= IR_REGION_SIZE || size > IR_REGION_SIZE - (address - base)) return 0;
    return address;
}
static uint32_t ir_os_free(void *c, uint32_t address, uint32_t size, uint32_t type) {
    (void)c;
    uint32_t base = (uint32_t)(uintptr_t)ir_region;
    if ((type & MEM_DECOMMIT) && address - base < IR_REGION_SIZE && size <= IR_REGION_SIZE - (address - base))
        memset(ir_region + (address - base), 0, size);
    return 1;
}
static es2_guest_memory ir_memory;
/* ---- write-watched buffers. The buffers the harness zeroes per case and side
 * (this region, loop_cases.h's frame bits, startup_cases.h's runtime region)
 * are allocated with MEM_WRITE_WATCH: the OS lists the pages written since the
 * last clear, wherever the write landed (inside a reservation or not), and a
 * clear zeroes only those. Zeroing them in full (16 MiB, 1.2 MiB and 4 MiB per
 * case and side) made the sim suites memory-bandwidth bound. Without write
 * tracking (GetWriteWatch fails) a clear zeroes the whole buffer as before.
 *
 * ES2_INVITRO_CHECK_RESET=1 checks every shortcut against a full scan: a
 * cleared buffer is zero throughout, and su_reset, su_take and su_compare
 * (startup_cases.h) agree with a full restore, copy and compare. */
#define WW_PAGE 0x1000u
#define IR_PAGES (IR_REGION_SIZE / WW_PAGE)
static void *ww_dirty[IR_PAGES];
static const BYTE ww_zero[WW_PAGE];
static int ww_check = -1;
static int ww_checking(void) {
    if (ww_check < 0) {
        const char *v = getenv("ES2_INVITRO_CHECK_RESET");
        ww_check = v && *v && strcmp(v, "0");
    }
    return ww_check;
}
static BYTE *ww_alloc(SIZE_T size) { return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE); }
/* The pages of the buffer written since its last clear, into ww_dirty; -1
 * without write tracking. */
static long ww_written(BYTE *base, SIZE_T size) {
    ULONG_PTR count = IR_PAGES;
    ULONG granularity = 0;
    if (size > IR_REGION_SIZE || GetWriteWatch(0, base, size, ww_dirty, &count, &granularity) || granularity != WW_PAGE) return -1;
    return (long)count;
}
/* Whether the page at a differs from the page at b, or from a zero page when
 * b is NULL. It reads the whole page without an early exit, which the
 * compiler vectorises; memcmp was slower on the mostly equal pages. */
static int ww_page_differs(const BYTE *a, const BYTE *b) {
    uint64_t acc = 0, x, y;
    if (!b)
        for (uint32_t at = 0; at < WW_PAGE; at += 8) {
            memcpy(&x, a + at, 8);
            acc |= x;
        }
    else
        for (uint32_t at = 0; at < WW_PAGE; at += 8) {
            memcpy(&x, a + at, 8);
            memcpy(&y, b + at, 8);
            acc |= x ^ y;
        }
    return acc != 0;
}
static int ww_is_zero(const BYTE *p, SIZE_T size) {
    for (SIZE_T at = 0; at < size; at += WW_PAGE)
        if (memcmp(p + at, ww_zero, size - at < WW_PAGE ? size - at : WW_PAGE)) return 0;
    return 1;
}
static void ww_clear(BYTE *base, SIZE_T size) {
    long count = ww_written(base, size);
    if (count < 0) memset(base, 0, size);
    else {
        for (long i = 0; i < count; ++i) {
            SIZE_T at = (SIZE_T)((BYTE *)ww_dirty[i] - base);
            memset(base + at, 0, size - at < WW_PAGE ? size - at : WW_PAGE);
        }
        ResetWriteWatch(base, size);
    }
    if (ww_checking() && !ww_is_zero(base, size)) {
        fprintf(stderr, "FAIL: buffer %p (0x%lx bytes) not zero after the clear\n", (void *)base, (unsigned long)size);
        ExitProcess(1);
    }
}
static void ir_heap_reset(void) {
    ww_clear(ir_region, IR_REGION_SIZE);
    ir_cursor = 0;
    memset(ir_head, 0, sizeof(ir_head));
    es2_sim_heap_os os = {NULL, ir_os_alloc, ir_os_free};
    es2_sim_heap_init(&ir_heap, &ir_memory, &os, (uint32_t)(uintptr_t)&ir_head[2], es2_sim_heap_platform(), 0, 0);
}

/* ---- the original's import stubs */
static void *__cdecl ir_memset(void *d, int v, size_t n) { ir_note('S'); memset(d, v, n); return d; }
static void *__cdecl ir_memmove(void *d, const void *s, size_t n) { ir_note('M'); memmove(d, s, n); return d; }
static void *__cdecl ir_calloc(size_t count, size_t n) {
    ir_note('C');
    es2_rtl_regs r = {0, 0, 0};
    return (void *)(uintptr_t)es2_sim_heap_calloc(&ir_heap, (uint32_t)count, (uint32_t)n, &r);
}
static void __cdecl ir_free(void *p) {
    ir_note('F');
    es2_rtl_regs r = {0, 0, 0};
    if (es2_sim_heap_free(&ir_heap, (uint32_t)(uintptr_t)p, &r)) ir_failed = 1;
}
static uint32_t __stdcall ir_create_file(uint32_t name, uint32_t access, uint32_t share, uint32_t security,
                                         uint32_t disposition, uint32_t flags, uint32_t template_file) {
    ir_note('V');
    uint32_t a[7] = {name, access, share, security, disposition, flags, template_file};
    memcpy(ir_create_args[0], a, sizeof(a));
    snprintf(ir_create_name[0], sizeof(ir_create_name[0]), "%s", name ? (const char *)(uintptr_t)name : "");
    return 0xffffffffu;
}
static uint32_t __stdcall ir_close_handle(uint32_t handle) { (void)handle; ir_note('X'); ir_failed = 1; return 0; }
static uint32_t __stdcall ir_module_handle(uint32_t name) { ir_note('G'); return name ? 0 : 0x00400000u; }
/* __startup: the entry jumps here with [esp] = 0, [esp+4] = the module
 * table, [esp+8] = the machine call's return address. */
extern void ir_startup_stub(void);
__asm__(".text\n.globl _ir_startup_stub\n_ir_startup_stub:\n\taddl $8, %esp\n\tret\n");

/* Points the IAT slot of dll!name at `stub`; 0 when the import is absent. */
static int ir_patch_import(const char *dll, const char *name, void *stub) {
    uint32_t pe = get32(mapped + 0x3c);
    for (const BYTE *d = mapped + get32(mapped + pe + 24 + 104); get32(d + 12); d += 20) {
        if (_stricmp((const char *)(mapped + get32(d + 12)), dll)) continue;
        uint32_t lookup = get32(d), iat = get32(d + 16);
        if (!lookup) lookup = iat;
        for (unsigned slot = 0; get32(mapped + lookup + 4 * slot); ++slot) {
            uint32_t entry = get32(mapped + lookup + 4 * slot);
            if (entry & 0x80000000u) continue;
            if (strcmp((const char *)(mapped + entry + 2), name)) continue;
            put32(mapped + iat + 4 * slot, (uint32_t)(uintptr_t)stub);
            return 1;
        }
    }
    return 0;
}

/* ---- the own side's dispatch: the sim services, with logging wrappers on
 * the import-level ones */
static es2_sim_entry ir_entries[4096];
static es2_sim_dispatch ir_dispatch;
static es2_sim_services ir_services;
static struct { uint32_t va; char letter; es2_sim_service_fn real; } ir_wrapped[] = {
    {0x0049681au, 'S', NULL}, {0x00496814u, 'M', NULL}, {0x0047749cu, 'C', NULL}, {0x00477548u, 'F', NULL},
    {0x0047758cu, 'F', NULL}, {0x004968feu, 'V', NULL}, {0x004968b0u, 'X', NULL}, {0x00496898u, 'G', NULL},
};
static void ir_wrapper(es2_sim_service_call *call, void *user) {
    for (unsigned i = 0; i < sizeof(ir_wrapped) / sizeof(ir_wrapped[0]); ++i) {
        if (ir_wrapped[i].va != call->va) continue;
        /* delete and delete[] reach _free only for a nonzero pointer. */
        if (ir_wrapped[i].letter != 'F' || call->slots[0]) ir_note(ir_wrapped[i].letter);
        ir_wrapped[i].real(call, user);
        return;
    }
}
static void ir_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own init: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    ir_failed = 1;
}
static int ir_dispatch_setup(void) {
    es2_sim_dispatch_init(&ir_dispatch, ir_entries, 4096, ir_refuse, NULL);
    ir_services.memory = &ir_memory;
    ir_services.heap = &ir_heap;
    ir_services.vt = NULL;
    ir_services.fs0 = NULL;
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_register_services(&ir_dispatch, &ir_services) || es2_sim_dispatch_seal(&ir_dispatch, &error, &bad))
        return fail("own init: dispatch");
    for (unsigned i = 0; i < sizeof(ir_wrapped) / sizeof(ir_wrapped[0]); ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&ir_dispatch, ir_wrapped[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("own init: service missing");
        ir_wrapped[i].real = e->service;
        e->service = ir_wrapper;
    }
    return 0;
}

/* ---- state of a pass */
static uint64_t ir_fnv(uint64_t h, const BYTE *p, size_t n) {
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
    return h;
}
static uint32_t *ir_stack_word(unsigned k) { return (uint32_t *)(uintptr_t)(ir_stack_base - 8u + 4u * k); }
static uint64_t ir_state_hash(void) {
    uint64_t h = 0xcbf29ce484222325ull;
    h = ir_fnv(h, mapped + IR_DATA_RVA, IR_DATA_SIZE);
    h = ir_fnv(h, (const BYTE *)ir_stack_word(0), 8);
    h = ir_fnv(h, (const BYTE *)ir_head, sizeof(ir_head));
    return ir_fnv(h, ir_region, IR_REGION_COMPARED);
}
/* Snapshot layout: DATA, the two stack words, the heap head, the region part. */
#define IR_SNAPSHOT_SIZE (IR_DATA_SIZE + 8u + 16u + IR_REGION_COMPARED)
static void ir_take(BYTE *s) {
    memcpy(s, mapped + IR_DATA_RVA, IR_DATA_SIZE);
    memcpy(s + IR_DATA_SIZE, ir_stack_word(0), 8);
    memcpy(s + IR_DATA_SIZE + 8u, ir_head, 16);
    memcpy(s + IR_DATA_SIZE + 24u, ir_region, IR_REGION_COMPARED);
}
static uint32_t ir_snapshot_address(uint32_t offset) {
    if (offset < IR_DATA_SIZE) return 0x00497000u + offset;
    offset -= IR_DATA_SIZE;
    if (offset < 8u) return ir_stack_base - 8u + offset;
    offset -= 8u;
    if (offset < 16u) return (uint32_t)(uintptr_t)ir_head + offset;
    return (uint32_t)(uintptr_t)ir_region + offset - 16u;
}
static void ir_reset(unsigned side) {
    memcpy(mapped + IR_DATA_RVA, ir_pristine, IR_DATA_SIZE);
    ir_stack_word(0)[0] = ir_stack_saved[0];
    ir_stack_word(0)[1] = ir_stack_saved[1];
    ir_heap_reset();
    ir_side = side;
    ir_log_count[side] = 0;
    memset(ir_log[side], 0, IR_LOG);
    es2_sim_platform_calls_reset();
}

/* The selector's byte and dword access to the identity view. */
static uint8_t ir_sel_read8(void *c, uint32_t a) { return es2_sim_read8(c, a); }
static void ir_sel_write8(void *c, uint32_t a, uint8_t v) { es2_sim_write8(c, a, v); }
static uint32_t ir_sel_read32(void *c, uint32_t a) { const es2_guest_memory *m = c; return m->read32(m->context, a); }

/* Runs one pass; stops after step `stop` with a snapshot in `snap` (stop =
 * IR_STEPS: the whole pass, hashes only). 0, or 1 on a failure. */
static int ir_original_pass(unsigned stop, BYTE *snap, int report) {
    ir_reset(0);
    uint32_t none[1] = {0};
    es2_hook_result r;
    es2_rtl_runtime rt = {0};
    rt.module_count = 1;
    rt.module_table = ES2_SIM_VA_MODULE_TABLE;
    es2_rtl_start_env env = {0};
    env.context = &ir_memory;
    env.read8 = ir_sel_read8;
    env.write8 = ir_sel_write8;
    env.read32 = ir_sel_read32;
    for (unsigned step = 0; step < IR_STEPS; ++step) {
        uint32_t target = ES2_SIM_VA_ENTRY;
        if (step) {
            unsigned mode = step > ES2_SIM_INIT_RECORDS ? 1u : 0u;
            target = es2_rtl_select_record(&rt, &env, mode);
            if (!target) return fail("original: the selector ran out of records");
        }
        ir_selected[0][step] = target;
        uint32_t counter_before = *ir_stack_word(1);
        uint32_t fs_before = ir_fs0();
        current_function = "init_records original";
        es2_hook_call call = {mapped + (target - 0x400000u), ES2_ABI_CDECL, 0, none, initial_fpu, &r, NULL};
        es2_hook_machine_call(&call);
        if (ir_fs0() != fs_before) return fail("original: FS:0 changed by a step");
        if (ir_failed) return fail("original: a stub refused a call");
        ir_hash[0][step] = ir_state_hash();
        if (report) {
            unsigned changed = 0;
            for (uint32_t i = 0; i < IR_DATA_SIZE; ++i) changed += mapped[IR_DATA_RVA + i] != ir_previous[i];
            printf("STEP %2u %s 0x%08lx: %u DATA bytes changed, counter %+ld\n", step,
                   step == 0 ? "entry" : step <= ES2_SIM_INIT_RECORDS ? "init " : "exit ", (unsigned long)target, changed,
                   step ? (long)(int32_t)(*ir_stack_word(1) - counter_before) : 0L);
            memcpy(ir_previous, mapped + IR_DATA_RVA, IR_DATA_SIZE);
            if (step == ES2_SIM_INIT_RECORDS || step == IR_STEPS - 1u) {
                /* Against the file: initialised DATA (below 0x4a7c00), the rest
                 * (zero-filled), and the record flags among them. */
                unsigned data = 0, bss = 0, flags = 0;
                for (uint32_t i = 0; i < IR_DATA_SIZE; ++i) {
                    if (mapped[IR_DATA_RVA + i] == ir_pristine[i]) continue;
                    if (0x00497000u + i < 0x004a7c00u) ++data; else ++bss;
                    if (0x00497000u + i >= 0x004a79d8u && 0x00497000u + i < 0x004a7bfau) ++flags;
                }
                printf("AFTER %s: %u bytes differ from the file (%u initialised DATA, %u zero-filled; %u record flags), counter %lu\n",
                       step == ES2_SIM_INIT_RECORDS ? "init records" : "exit records", data + bss, data, bss, flags,
                       (unsigned long)*ir_stack_word(1));
            }
        }
        if (step == stop) { ir_take(snap); return 0; }
    }
    if (es2_rtl_select_record(&rt, &env, 0) || es2_rtl_select_record(&rt, &env, 1)) return fail("original: records left");
    return 0;
}
static int ir_own_pass(unsigned stop, BYTE *snap) {
    ir_reset(1);
    es2_sim_init c = {&ir_memory, &ir_dispatch, ir_stack_base, 0, 0, 0};
    for (unsigned step = 0; step < IR_STEPS; ++step) {
        uint32_t va = ES2_SIM_VA_ENTRY;
        int result;
        if (!step) result = es2_sim_c0w32_entry(&c);
        else result = es2_sim_run_next_record(&c, step > ES2_SIM_INIT_RECORDS ? 1u : 0u, &va);
        ir_selected[1][step] = va;
        if (result == 2) return fail("own: the selector ran out of records");
        if (result || ir_failed) { fprintf(stderr, "FAIL: own init: step %u (0x%08lx) refused\n", step, (unsigned long)va); return 1; }
        ir_hash[1][step] = ir_state_hash();
        if (step == stop) { ir_take(snap); return 0; }
    }
    uint32_t va = 0;
    if (es2_sim_run_next_record(&c, 0, &va) != 2 || es2_sim_run_next_record(&c, 1, &va) != 2) return fail("own: records left");
    if (c.init_run != ES2_SIM_INIT_RECORDS || c.exit_run != ES2_SIM_EXIT_RECORDS) return fail("own: record counts");
    const es2_sim_platform_calls *p = es2_sim_platform_calls_get();
    memcpy(ir_create_args[1], p->create_file_args, sizeof(ir_create_args[1]));
    memcpy(ir_create_name[1], p->create_file_name, sizeof(ir_create_name[1]));
    return 0;
}
static void ir_print_differences(unsigned step) {
    unsigned shown = 0, total = 0;
    for (uint32_t i = 0; i < IR_SNAPSHOT_SIZE; ++i) {
        if (ir_snapshot[0][i] == ir_snapshot[1][i]) continue;
        ++total;
        if (shown++ < 48)
            fprintf(stderr, "  step %u: 0x%08lx original %02x own %02x (file %02x)\n", step, (unsigned long)ir_snapshot_address(i),
                    ir_snapshot[0][i], ir_snapshot[1][i], i < IR_DATA_SIZE ? ir_pristine[i] : 0u);
    }
    fprintf(stderr, "  step %u: %u bytes differ\n", step, total);
}

static int init_records_cases(void) {
    current_function = "init_records";
    es2_sim_identity_memory(&ir_memory);
    ir_stack_base = (uint32_t)(uintptr_t)((NT_TIB *)NtCurrentTeb())->StackBase;
    ir_stack_saved[0] = ir_stack_word(0)[0];
    ir_stack_saved[1] = ir_stack_word(0)[1];
    ir_pristine = malloc(IR_DATA_SIZE);
    ir_previous = malloc(IR_DATA_SIZE);
    ir_snapshot[0] = malloc(IR_SNAPSHOT_SIZE);
    ir_snapshot[1] = malloc(IR_SNAPSHOT_SIZE);
    ir_region = ww_alloc(IR_REGION_SIZE);
    if (!ir_pristine || !ir_previous || !ir_snapshot[0] || !ir_snapshot[1] || !ir_region) return fail("init_records: memory");
    memcpy(ir_pristine, mapped + IR_DATA_RVA, IR_DATA_SIZE);
    memcpy(ir_previous, ir_pristine, IR_DATA_SIZE);
    /* The original's imports on this path. */
    if (!ir_patch_import("cw3220.dll", "_memset", (void *)ir_memset) || !ir_patch_import("cw3220.dll", "_memmove", (void *)ir_memmove) ||
        !ir_patch_import("cw3220.dll", "_calloc", (void *)ir_calloc) || !ir_patch_import("cw3220.dll", "_free", (void *)ir_free) ||
        !ir_patch_import("cw3220.dll", "__startup", (void *)ir_startup_stub) ||
        !ir_patch_import("KERNEL32.dll", "CreateFileA", (void *)ir_create_file) ||
        !ir_patch_import("KERNEL32.dll", "CloseHandle", (void *)ir_close_handle) ||
        !ir_patch_import("KERNEL32.dll", "GetModuleHandleA", (void *)ir_module_handle))
        return fail("init_records: import slot not found");
    if (ir_dispatch_setup()) return 1;
    printf("INIT RECORDS: StackBase 0x%08lx (words below: 0x%08lx 0x%08lx), heap region 0x%08lx\n", (unsigned long)ir_stack_base,
           (unsigned long)ir_stack_saved[0], (unsigned long)ir_stack_saved[1], (unsigned long)(uintptr_t)ir_region);
    int bad = 0;
    if (ir_original_pass(IR_STEPS, NULL, 1)) bad = 1;
    char log0[IR_LOG];
    memcpy(log0, ir_log[0], IR_LOG);
    uint32_t heap_bytes = get32(mapped + 0x2bb30c), table = get32(mapped + 0xa08b4);
    if (!bad && ir_own_pass(IR_STEPS, NULL)) bad = 1;
    if (!bad) {
        unsigned first = IR_STEPS;
        for (unsigned s = 0; s < IR_STEPS && first == IR_STEPS; ++s)
            if (ir_selected[0][s] != ir_selected[1][s] || ir_hash[0][s] != ir_hash[1][s]) first = s;
        if (first < IR_STEPS) {
            fprintf(stderr, "FAIL: init_records: step %u differs (original 0x%08lx, own 0x%08lx)\n", first,
                    (unsigned long)ir_selected[0][first], (unsigned long)ir_selected[1][first]);
            if (!ir_original_pass(first, ir_snapshot[0], 0) && !ir_own_pass(first, ir_snapshot[1])) ir_print_differences(first);
            bad = 1;
        }
    }
    if (!bad && (table - (uint32_t)(uintptr_t)ir_region > IR_REGION_COMPARED - 0x80cu)) { fprintf(stderr, "FAIL: init_records: the heap block 0x%08lx lies outside the compared region part\n", (unsigned long)table); bad = 1; }
    if (!bad && strcmp(log0, ir_log[1])) { fprintf(stderr, "FAIL: init_records: call logs differ: original %s own %s\n", log0, ir_log[1]); bad = 1; }
    if (!bad && (memcmp(ir_create_args[0], ir_create_args[1], sizeof(ir_create_args[0])) || strcmp(ir_create_name[0], ir_create_name[1]))) {
        fprintf(stderr, "FAIL: init_records: CreateFileA arguments differ\n");
        bad = 1;
    }
    /* Leave the thread's words as they were. */
    ir_stack_word(0)[0] = ir_stack_saved[0];
    ir_stack_word(0)[1] = ir_stack_saved[1];
    if (bad) return 1;
    printf("PASS: init_records: %u steps (c0w32 entry, %u init, %u exit records) identical after every step: DATA 0x497000..0x6cc000, "
           "StackBase-8/-4, heap head and region; calls %s; CreateFileA(\"%s\", %lu, %lu, %lu, %lu, 0x%08lx, %lu) = -1 on both; "
           "[0x6bb30c] after the exit records 0x%lx, [0x4a08b4] 0x%08lx\n",
           IR_STEPS, ES2_SIM_INIT_RECORDS, ES2_SIM_EXIT_RECORDS, log0, ir_create_name[0], (unsigned long)ir_create_args[0][1],
           (unsigned long)ir_create_args[0][2], (unsigned long)ir_create_args[0][3], (unsigned long)ir_create_args[0][4],
           (unsigned long)ir_create_args[0][5], (unsigned long)ir_create_args[0][6], (unsigned long)heap_bytes, (unsigned long)table);
    /* The platform probe: the real CreateFileA with record 64's arguments. */
    SetLastError(0);
    HANDLE h = CreateFileA(ir_create_name[0], 0, 0, NULL, 0, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    DWORD error = GetLastError();
    printf("PLATFORM: CreateFileA(\"%s\", 0, 0, NULL, 0, FILE_FLAG_DELETE_ON_CLOSE, NULL) on this host = 0x%08lx, error %lu\n",
           ir_create_name[0], (unsigned long)(uintptr_t)h, (unsigned long)error);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    return 0;
}
