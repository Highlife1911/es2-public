/* The CW3220 heap against core/src/rtl_heap.c, in vitro.
 *
 * CW3220.DLL is mapped as in rtl_cases.h (an SHA-256 gate, relocations,
 * every import a named trap, no entry point). Its two KERNEL32 slots for
 * VirtualAlloc and VirtualFree then point to an emulated OS over one
 * pre-reserved window: reservations are handed out from the window by a
 * per-sequence policy (directly following the previous one, with gaps, mixed,
 * or refused), commits and decommits are real page operations inside it,
 * a release decommits its range; commits can be made to fail. Every call is
 * recorded with its arguments and result.
 *
 * Direct cases: generated operation sequences (malloc, calloc, free,
 * realloc, _expand, operator new/delete; sizes 0 to 4.6 MB, frees and
 * reallocs on random live blocks, user data written into every live block)
 * run on the original exports; after each operation the model replays it
 * with the recorded OS calls and must give the same result pointer (as an
 * offset in its region), the same OS calls, the same allocator variables and
 * the same value for every allocator word of the heap (walk of all regions);
 * it must never read a word it did not write. Fixed sequences: the 64-region
 * limit and each platform granularity.
 *
 * Installed cases: the proxy's import shim (proxy/rtl_imports.h) on the
 * mapped DBSIM with calloc/realloc/free in mode shadow: each sequence runs
 * once without and once with the shim (same emulated OS decisions), calloc,
 * realloc and free through DBSIM's thunks, malloc/_expand directly and
 * new/delete as CW3220-internal callers; registers, the 4 KiB of stack below
 * the arguments and the heap must be identical, shadow mismatches 0, every
 * operation observed with its origin. A planted model error must be caught
 * once and resynchronized.
 *
 * Replaced cases: calloc/realloc/free in mode replaced (the own heap of
 * proxy/rtl_heap_own.h) against a run on the original exports with the same OS
 * decisions and the same head-block address: results, OS calls, variables,
 * every committed byte, the modelled registers; and mixed sequences with
 * CW3220-heap blocks whose frees and reallocs must reach the original.
 * Selection: --rtl-heap or rtl_heap_<name> (mutation runs). */

#define HEAP_WINDOW (320u << 20)
#define HEAP_LOG 65536u
#define HEAP_WORD_BITS 18u
#define HEAP_LIVE 512u
static BYTE *heap_window;
static uint32_t heap_cursor, heap_os_random;
static unsigned heap_policy, heap_commit_failure_rate, heap_reserve_failure_rate, heap_os_unexpected;
static uint32_t heap_reservations[256][2];
static unsigned heap_reservation_count;
static es2_heap_os_call heap_log[HEAP_LOG];
static unsigned heap_log_count;
static uint32_t heap_os_next(void) { uint32_t x = heap_os_random; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return heap_os_random = x; }
static void heap_record(uint32_t kind, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t result) {
    if (heap_log_count < HEAP_LOG) { es2_heap_os_call call = {kind, a, b, c, d, result}; heap_log[heap_log_count] = call; }
    ++heap_log_count;
}
enum { HEAP_POLICY_FOLLOW, HEAP_POLICY_GAPS, HEAP_POLICY_MIXED, HEAP_POLICY_LIMIT };
static LPVOID WINAPI heap_os_valloc(LPVOID address, SIZE_T size, DWORD type, DWORD protect) {
    uint32_t result = 0, base = (uint32_t)(uintptr_t)heap_window;
    if (type == MEM_RESERVE && !address && protect == PAGE_NOACCESS) {
        uint32_t gap = heap_policy == HEAP_POLICY_FOLLOW ? 0u : heap_policy == HEAP_POLICY_GAPS ? (1u + heap_os_next() % 8u) << 16 :
                       heap_policy == HEAP_POLICY_LIMIT ? 0x100000u : (heap_os_next() & 1u) ? 0u : (1u + heap_os_next() % 16u) << 16;
        int refused = heap_reserve_failure_rate && heap_os_next() % heap_reserve_failure_rate == 0;
        if (!refused && heap_cursor + gap + size > heap_cursor && heap_cursor + gap + size <= base + HEAP_WINDOW && heap_reservation_count < 256) {
            result = heap_cursor + gap;
            heap_cursor = result + (uint32_t)size;
            heap_reservations[heap_reservation_count][0] = result;
            heap_reservations[heap_reservation_count++][1] = (uint32_t)size;
        }
    } else if (type == MEM_COMMIT && (uint32_t)(uintptr_t)address - base < HEAP_WINDOW) {
        if (!(heap_commit_failure_rate && heap_os_next() % heap_commit_failure_rate == 0))
            result = (uint32_t)(uintptr_t)VirtualAlloc(address, size, MEM_COMMIT, protect);
    } else ++heap_os_unexpected;
    heap_record(ES2_HEAP_OS_ALLOC, (uint32_t)(uintptr_t)address, (uint32_t)size, type, protect, result);
    return (LPVOID)(uintptr_t)result;
}
static BOOL WINAPI heap_os_vfree(LPVOID address, SIZE_T size, DWORD type) {
    uint32_t a = (uint32_t)(uintptr_t)address, result = 0;
    if (type == MEM_DECOMMIT && a - (uint32_t)(uintptr_t)heap_window < HEAP_WINDOW) result = (uint32_t)VirtualFree(address, size, MEM_DECOMMIT);
    else if (type == MEM_RELEASE && !size) {
        for (unsigned i = 0; i < heap_reservation_count; ++i)
            if (heap_reservations[i][0] == a) result = (uint32_t)VirtualFree(address, heap_reservations[i][1], MEM_DECOMMIT);
    } else ++heap_os_unexpected;
    heap_record(ES2_HEAP_OS_FREE, a, (uint32_t)size, type, 0, result);
    return (BOOL)result;
}

/* The mapped DLL's allocator variables and exports. */
static uint32_t heap_cw(uint32_t rva) { return (uint32_t)(uintptr_t)rtl_cw + rva; }
static uint32_t heap_word(uint32_t address) { return *(const volatile uint32_t *)(uintptr_t)address; }
static void heap_real_state(es2_heap *state) {
    memset(state, 0, sizeof(*state));
    state->sentinel = heap_cw(RTL_HEAP_SENTINEL);
    state->rover = heap_word(heap_cw(RTL_HEAP_ROVER));
    state->region_count = heap_word(heap_cw(RTL_HEAP_COUNT));
    for (uint32_t i = 0; i < state->region_count && i < ES2_HEAP_REGION_LIMIT; ++i) {
        state->region_base[i] = heap_word(heap_cw(RTL_HEAP_TABLE + 8u * i));
        state->region_size[i] = heap_word(heap_cw(RTL_HEAP_TABLE + 8u * i + 4u));
    }
    state->committed_end = heap_word(heap_cw(RTL_HEAP_COMMITTED));
    state->reserved_end = heap_word(heap_cw(RTL_HEAP_RESERVED));
    state->granularity = heap_word(heap_cw(RTL_HEAP_GRANULARITY));
    state->platform = heap_word(heap_cw(RTL_HEAP_PLATFORM));
}
static uint32_t heap_export[RTL_HEAP_ENTRY_COUNT], heap_new, heap_delete;

/* Model with the core's shadow env (word store, journal, OS replay). */
static uint32_t heap_keys[1u << HEAP_WORD_BITS], heap_values[1u << HEAP_WORD_BITS], heap_journal[4096];
static es2_heap_shadow heap_shadow;
static es2_heap_env heap_env;
static es2_heap heap_model;

/* Reset: the window decommitted, the DLL's variables cleared and its init
 * record (4170fc) run, the model started from the same point. */
static void heap_reset(uint32_t platform, unsigned policy, uint32_t os_seed) {
    VirtualFree(heap_window, HEAP_WINDOW, MEM_DECOMMIT);
    heap_cursor = (uint32_t)(uintptr_t)heap_window;
    heap_reservation_count = 0;
    heap_policy = policy;
    heap_os_random = os_seed | 1u;
    heap_commit_failure_rate = policy == HEAP_POLICY_MIXED ? 400u : 0u;
    heap_reserve_failure_rate = policy == HEAP_POLICY_MIXED ? 12u : 0u;
    *(volatile uint32_t *)(uintptr_t)heap_cw(RTL_HEAP_COUNT) = 0;
    *(volatile uint32_t *)(uintptr_t)heap_cw(RTL_HEAP_COMMITTED) = 0;
    *(volatile uint32_t *)(uintptr_t)heap_cw(RTL_HEAP_RESERVED) = 0;
    *(volatile uint32_t *)(uintptr_t)heap_cw(RTL_HEAP_GRANULARITY) = 0;
    *(volatile uint32_t *)(uintptr_t)heap_cw(RTL_HEAP_PLATFORM) = platform;
    ((void (__cdecl *)(void))(uintptr_t)heap_cw(0x170fcu))();
    es2_heap_words_clear(&heap_shadow.words);
    es2_heap_shadow_begin(&heap_shadow, NULL, 0);
    es2_heap_init(&heap_model, &heap_env, heap_cw(RTL_HEAP_SENTINEL), platform);
    heap_log_count = 0;
}

/* One call on the test stack: arguments at RTL_ARGS_ADDRESS. */
static rtl_vitro_frame heap_frame;
static int heap_call(uint32_t target, uint32_t a0, uint32_t a1) {
    rtl_vitro_frame *fr = &heap_frame;
    memset(fr, 0, sizeof(*fr));
    uint32_t *args = (uint32_t *)(uintptr_t)RTL_ARGS_ADDRESS;
    args[0] = a0; args[1] = a1;
    memcpy((BYTE *)(uintptr_t)(RTL_ARGS_ADDRESS - RTL_BELOW), rtl_below_pattern, RTL_BELOW);
    fr->esp = RTL_ARGS_ADDRESS; fr->target = target;
    fr->eax = 0x0eaeaeaeu; fr->ecx = 0x0c0c0c0cu; fr->edx = 0x0d0d0d0du;
    fr->ebx = 0xb0b0b0b0u; fr->esi = 0x51515151u; fr->edi = 0xd1d1d1d1u; fr->ebp = 0xe0e0e0e0u;
    rtl_vitro_call(fr);
    if (fr->out_ebx != 0xb0b0b0b0u || fr->out_esi != 0x51515151u || fr->out_edi != 0xd1d1d1d1u || fr->out_ebp != 0xe0e0e0e0u ||
        fr->out_esp != RTL_ARGS_ADDRESS || (fr->out_flags & 0x400u)) return fail("callee-saved registers, ESP or DF");
    return 0;
}

/* Operations. */
enum { HEAP_OP_MALLOC, HEAP_OP_CALLOC, HEAP_OP_FREE, HEAP_OP_REALLOC, HEAP_OP_EXPAND, HEAP_OP_NEW, HEAP_OP_DELETE, HEAP_OP_KINDS };
static const char *const heap_op_names[HEAP_OP_KINDS] = {"malloc", "calloc", "free", "realloc", "expand", "new", "delete"};
typedef struct heap_op { unsigned kind; uint32_t a0, a1; } heap_op;
static uint32_t heap_live[HEAP_LIVE];
static unsigned heap_live_count;
static uint32_t heap_size(void) {
    uint32_t r = random32() % 1000u;
    if (r < 20) return 0;
    if (r < 200) return random32() % 8u;
    if (r < 500) return 8u + random32() % 57u;
    if (r < 800) return 65u + random32() % 4032u;
    if (r < 965) return 4097u + random32() % 65000u;
    if (r < 997) return 70000u + random32() % 1000000u;
    return 4400000u + random32() % 200000u;
}
static uint32_t heap_pick(void) { return heap_live_count ? heap_live[random32() % heap_live_count] : 0u; }
static uint32_t heap_resize(uint32_t p) {
    uint32_t usable = p ? (heap_word(p - 4u) & ~1u) - 8u : 0u, r = random32() % 10u;
    if (r == 0) return 0;
    if (r < 3) return usable;                                         /* same block size */
    if (r < 5) return usable / 2u;                                    /* shrink */
    if (r < 8) return usable + 1u + random32() % (usable / 2u + 64u); /* grow a little */
    return heap_size();
}
static heap_op heap_generate(void) {
    heap_op op = {HEAP_OP_FREE, 0, 0};
    uint32_t r = random32() % 100u;
    if (heap_live_count >= HEAP_LIVE - 8u && r < 55u) r = 60u;
    if (r < 20) { op.kind = HEAP_OP_MALLOC; op.a0 = heap_size(); }
    else if (r < 28) {
        op.kind = HEAP_OP_CALLOC;
        if (random32() % 50u == 0) { op.a0 = 0x10000u; op.a1 = 0x10001u + random32() % 3u; }   /* the product wraps */
        else { op.a0 = random32() % 6u; op.a1 = heap_size() / (op.a0 ? op.a0 : 1u); }
    }
    else if (r < 36) { op.kind = HEAP_OP_NEW; op.a0 = random32() % 4u ? heap_size() % 70000u : 0u; }
    else if (r < 55) { op.kind = HEAP_OP_REALLOC; op.a0 = random32() % 20u ? heap_pick() : 0u; op.a1 = heap_resize(op.a0); }
    else if (r < 62) { op.kind = HEAP_OP_EXPAND; op.a0 = heap_pick(); op.a1 = heap_resize(op.a0); if (!op.a0) op.kind = HEAP_OP_FREE; }
    else if (r < 70) { op.kind = HEAP_OP_DELETE; op.a0 = heap_pick(); }
    else { op.kind = HEAP_OP_FREE; op.a0 = random32() % 30u ? heap_pick() : 0u; }
    return op;
}
static void heap_forget(uint32_t p) {
    for (unsigned i = 0; i < heap_live_count; ++i) if (heap_live[i] == p) { heap_live[i] = heap_live[--heap_live_count]; return; }
}
static void heap_scribble(uint32_t p) {
    uint32_t usable = (heap_word(p - 4u) & ~1u) - 8u;
    for (uint32_t i = 0; i < usable; i += 4u) *(volatile uint32_t *)(uintptr_t)(p + i) = random32();
}
/* Bookkeeping after the original's operation (live list, user data). */
static void heap_after(const heap_op *op, uint32_t result) {
    switch (op->kind) {
    case HEAP_OP_FREE: case HEAP_OP_DELETE: if (op->a0) heap_forget(op->a0); break;
    case HEAP_OP_REALLOC:
        if (op->a0 && (!op->a1 || result)) heap_forget(op->a0);
        if (result) { heap_live[heap_live_count++] = result; heap_scribble(result); }
        break;
    case HEAP_OP_EXPAND: if (result) heap_scribble(result); break;
    default: if (result) { heap_live[heap_live_count++] = result; if (op->kind != HEAP_OP_CALLOC) heap_scribble(result); } break;
    }
}
static uint32_t heap_target(const heap_op *op, int installed) {
    unsigned import = op->kind == HEAP_OP_CALLOC ? RTL_I_calloc : op->kind == HEAP_OP_FREE ? RTL_I_free :
                      op->kind == HEAP_OP_REALLOC ? RTL_I_realloc : RTL_IMPORT_COUNT;
    if (installed && import < RTL_IMPORT_COUNT) return (uint32_t)(uintptr_t)mapped + RTL_THUNK_RVA + 6u * import;
    switch (op->kind) {
    case HEAP_OP_MALLOC: return heap_export[RTL_HEAP_MALLOC];
    case HEAP_OP_CALLOC: return heap_export[RTL_HEAP_CALLOC];
    case HEAP_OP_FREE: return heap_export[RTL_HEAP_FREE];
    case HEAP_OP_REALLOC: return heap_export[RTL_HEAP_REALLOC];
    case HEAP_OP_EXPAND: return heap_export[RTL_HEAP_EXPAND];
    case HEAP_OP_NEW: return heap_new;
    default: return heap_delete;
    }
}
static uint32_t heap_model_op(const heap_op *op) {
    switch (op->kind) {
    case HEAP_OP_MALLOC: return es2_heap_malloc(&heap_model, &heap_env, op->a0);
    case HEAP_OP_CALLOC: return es2_heap_calloc(&heap_model, &heap_env, op->a0, op->a1);
    case HEAP_OP_FREE: case HEAP_OP_DELETE: es2_heap_free(&heap_model, &heap_env, op->a0); return 0;
    case HEAP_OP_REALLOC: return es2_heap_realloc(&heap_model, &heap_env, op->a0, op->a1);
    case HEAP_OP_EXPAND: return es2_heap_expand(&heap_model, &heap_env, op->a0, op->a1);
    default: return es2_heap_malloc(&heap_model, &heap_env, op->a0 ? op->a0 : 1u);   /* operator new: size 0 -> 1 */
    }
}

/* Every allocator word of the model against memory. */
static uint32_t heap_bad_address, heap_bad_model, heap_bad_real;
static int heap_bad;
static uint32_t heap_model_read(void *context, uint32_t address) {
    (void)context;
    uint32_t value = 0xdeadbeefu;
    es2_heap_words_find(&heap_shadow.words, address, &value);
    return value;
}
static uint32_t heap_real_read(void *context, uint32_t address) { (void)context; return heap_word(address); }
static void heap_compare_word(void *context, uint32_t address, uint32_t value) {
    (void)context;
    uint32_t real = heap_word(address);
    if (!heap_bad && real != value) { heap_bad = 1; heap_bad_address = address; heap_bad_model = value; heap_bad_real = real; }
}
/* Region and offset of an address, for messages. */
static void heap_where(uint32_t address, char *out, size_t size) {
    for (uint32_t i = 0; i < heap_model.region_count; ++i)
        if (address - heap_model.region_base[i] < heap_model.region_size[i]) {
            snprintf(out, size, "region %u +0x%x", (unsigned)i, (unsigned)(address - heap_model.region_base[i]));
            return;
        }
    snprintf(out, size, "0x%08x", (unsigned)address);
}
static unsigned heap_sequence;
static int heap_fail(unsigned step, const heap_op *op, const char *what, uint32_t model, uint32_t real, uint32_t address) {
    char where[48];
    heap_where(address, where, sizeof(where));
    fprintf(stderr, "FAIL: %s case %u: step %u %s(%08x, %08x): %s model %08x original %08x at %s\n", current_function, heap_sequence,
            step, heap_op_names[op->kind], (unsigned)op->a0, (unsigned)op->a1, what, (unsigned)model, (unsigned)real, where);
    return 1;
}
/* The model replays the operation the original just made and must agree. */
static int heap_check(unsigned step, const heap_op *op, uint32_t result) {
    es2_heap_shadow_begin(&heap_shadow, heap_log, heap_log_count < HEAP_LOG ? heap_log_count : HEAP_LOG);
    uint32_t missing = heap_shadow.words.missing;
    uint32_t predicted = heap_model_op(op);
    if (heap_log_count > HEAP_LOG) return heap_fail(step, op, "OS log overflow", 0, heap_log_count, 0);
    if (heap_os_unexpected) return heap_fail(step, op, "unexpected OS call", 0, heap_os_unexpected, 0);
    if (op->kind != HEAP_OP_FREE && op->kind != HEAP_OP_DELETE && predicted != result) return heap_fail(step, op, "result", predicted, result, result ? result : predicted);
    if (es2_heap_shadow_finish(&heap_shadow)) {
        uint32_t k = heap_shadow.os_mismatch_index;
        return heap_fail(step, op, "OS call", k, k < heap_log_count ? heap_log[k].address : 0xffffffffu, 0);
    }
    if (heap_shadow.words.missing != missing) return heap_fail(step, op, "model read a word it never wrote", 0, 0, 0);
    if (heap_shadow.words.overflow || heap_shadow.journal_overflow) return heap_fail(step, op, "word store", 0, 0, 0);
    es2_heap real;
    heap_real_state(&real);
#define HEAP_FIELD(f) if (heap_model.f != real.f) return heap_fail(step, op, #f, heap_model.f, real.f, 0);
    HEAP_FIELD(rover) HEAP_FIELD(region_count) HEAP_FIELD(committed_end) HEAP_FIELD(reserved_end) HEAP_FIELD(granularity) HEAP_FIELD(platform)
#undef HEAP_FIELD
    for (uint32_t i = 0; i < real.region_count && i < ES2_HEAP_REGION_LIMIT; ++i)
        if (heap_model.region_base[i] != real.region_base[i] || heap_model.region_size[i] != real.region_size[i])
            return heap_fail(step, op, "region table", heap_model.region_size[i], real.region_size[i], real.region_base[i]);
    heap_bad = 0;
    int32_t blocks = es2_heap_walk(&heap_model, heap_model_read, NULL, heap_compare_word, NULL);
    if (heap_bad) return heap_fail(step, op, "allocator word", heap_bad_model, heap_bad_real, heap_bad_address);
    if (blocks < 0 || es2_heap_walk(&real, heap_real_read, NULL, NULL, NULL) != blocks) return heap_fail(step, op, "heap walk", (uint32_t)blocks, 0, 0);
    if (op->kind == HEAP_OP_CALLOC && result) {
        uint32_t total = op->a0 * op->a1;
        for (uint32_t i = 0; i < total; ++i) if (*(const BYTE *)(uintptr_t)(result + i)) return heap_fail(step, op, "calloc zero fill", 0, i, result + i);
    }
    return 0;
}

typedef struct heap_tally { unsigned sequences, ops, kinds[HEAP_OP_KINDS], null_results, os_calls, regions_max, grown_in_place, moved; } heap_tally;
static heap_tally heap_counts;
/* Paths of the allocator, classified from memory before and after each
 * operation and from the OS calls; every one must occur (the analogue of
 * every branch site both ways). */
enum {
    HEAP_PATH_FREE_ALONE, HEAP_PATH_FREE_BELOW, HEAP_PATH_FREE_ABOVE, HEAP_PATH_FREE_BOTH, HEAP_PATH_SPLIT, HEAP_PATH_WHOLE,
    HEAP_PATH_REALLOC_IN_PLACE, HEAP_PATH_REALLOC_DOWN, HEAP_PATH_REALLOC_MOVED, HEAP_PATH_REALLOC_NULL, HEAP_PATH_EXPAND_SHRINK,
    HEAP_PATH_EXPAND_SAME, HEAP_PATH_EXPAND_GROW, HEAP_PATH_EXPAND_FAIL, HEAP_PATH_GROW_REST, HEAP_PATH_RESERVE_FOLLOWING,
    HEAP_PATH_RESERVE_ELSEWHERE, HEAP_PATH_RESERVE_REFUSED, HEAP_PATH_COMMIT_FAILED, HEAP_PATH_NULL, HEAP_PATH_COUNT
};
static const char *const heap_path_names[HEAP_PATH_COUNT] = {
    "free alone", "free joins below", "free joins above", "free joins both", "split", "whole block", "realloc in place",
    "realloc into the block below", "realloc moved", "realloc NULL", "_expand shrink", "_expand same", "_expand grow",
    "_expand fails", "growth from the reservation's rest", "reservation following the last", "reservation elsewhere",
    "reservation refused", "commit failed", "NULL result"};
typedef struct heap_pre { uint32_t current, below_free, above_free, committed_end, reserved_end; } heap_pre;
static heap_pre heap_before(const heap_op *op) {
    heap_pre pre = {0, 0, 0, heap_word(heap_cw(RTL_HEAP_COMMITTED)), heap_word(heap_cw(RTL_HEAP_RESERVED))};
    uint32_t p = op->a0;
    if (p && (op->kind == HEAP_OP_FREE || op->kind == HEAP_OP_DELETE || op->kind == HEAP_OP_REALLOC || op->kind == HEAP_OP_EXPAND)) {
        pre.current = heap_word(p - 4u) & ~1u;
        pre.below_free = !(heap_word(p - 8u) & 1u);
        pre.above_free = !(heap_word(p + pre.current - 4u) & 1u);
    }
    return pre;
}
static unsigned heap_paths[HEAP_PATH_COUNT];
static void heap_classify(const heap_op *op, const heap_pre *pre, uint32_t result) {
    unsigned k = op->kind;
    int frees = (k == HEAP_OP_FREE || k == HEAP_OP_DELETE || (k == HEAP_OP_REALLOC && !op->a1)) && op->a0;
    if (frees) ++heap_paths[pre->below_free ? (pre->above_free ? HEAP_PATH_FREE_BOTH : HEAP_PATH_FREE_BELOW) :
                            (pre->above_free ? HEAP_PATH_FREE_ABOVE : HEAP_PATH_FREE_ALONE)];
    int allocates = k == HEAP_OP_MALLOC || k == HEAP_OP_NEW || k == HEAP_OP_CALLOC || (k == HEAP_OP_REALLOC && !op->a0);
    if (allocates && result) {
        uint32_t n = k == HEAP_OP_CALLOC ? op->a0 * op->a1 : k == HEAP_OP_REALLOC ? op->a1 : k == HEAP_OP_NEW && !op->a0 ? 1u : op->a0;
        uint32_t block = heap_word(result - 4u) & ~1u;
        ++heap_paths[block > es2_heap_block_size(n) ? HEAP_PATH_WHOLE : !(heap_word(result + block - 4u) & 1u) ? HEAP_PATH_SPLIT : HEAP_PATH_WHOLE];
    }
    if (k != HEAP_OP_FREE && k != HEAP_OP_DELETE && !result && (k != HEAP_OP_REALLOC || op->a1)) ++heap_paths[HEAP_PATH_NULL];
    if (k == HEAP_OP_REALLOC && op->a0 && op->a1) {
        if (!result) ++heap_paths[HEAP_PATH_REALLOC_NULL];
        else if (result == op->a0) ++heap_paths[HEAP_PATH_REALLOC_IN_PLACE];
        else if (pre->below_free && result == op->a0 - heap_word(op->a0 - 8u)) ++heap_paths[HEAP_PATH_REALLOC_DOWN];
        else ++heap_paths[HEAP_PATH_REALLOC_MOVED];
    }
    if (k == HEAP_OP_EXPAND && op->a0) {
        uint32_t after = result ? heap_word(result - 4u) & ~1u : 0u;
        ++heap_paths[!result ? HEAP_PATH_EXPAND_FAIL : after < pre->current ? HEAP_PATH_EXPAND_SHRINK :
                     after == pre->current ? HEAP_PATH_EXPAND_SAME : HEAP_PATH_EXPAND_GROW];
    }
    for (unsigned i = 0; i < heap_log_count && i < HEAP_LOG; ++i) {
        const es2_heap_os_call *c = &heap_log[i];
        if (c->kind == ES2_HEAP_OS_ALLOC && c->type == MEM_RESERVE)
            ++heap_paths[!c->result ? HEAP_PATH_RESERVE_REFUSED : c->result == pre->reserved_end ? HEAP_PATH_RESERVE_FOLLOWING : HEAP_PATH_RESERVE_ELSEWHERE];
        if (c->kind == ES2_HEAP_OS_ALLOC && c->type == MEM_COMMIT && !c->result) ++heap_paths[HEAP_PATH_COMMIT_FAILED];
        if (c->kind == ES2_HEAP_OS_ALLOC && c->type == MEM_COMMIT && c->address == pre->committed_end && pre->reserved_end != pre->committed_end)
            ++heap_paths[HEAP_PATH_GROW_REST];
    }
}
static int heap_direct_sequence(unsigned steps, uint32_t platform, unsigned policy, uint32_t os_seed) {
    heap_reset(platform, policy, os_seed);
    heap_live_count = 0;
    for (unsigned step = 0; step < steps; ++step) {
        heap_op op = heap_generate();
        heap_pre pre = heap_before(&op);
        heap_log_count = 0;
        if (heap_call(heap_target(&op, 0), op.a0, op.a1)) return 1;
        uint32_t result = heap_frame.out_eax;
        if (heap_check(step, &op, result)) return 1;
        heap_classify(&op, &pre, result);
        ++heap_counts.ops; ++heap_counts.kinds[op.kind]; heap_counts.os_calls += heap_log_count;
        if (op.kind != HEAP_OP_FREE && op.kind != HEAP_OP_DELETE && !result) ++heap_counts.null_results;
        if (op.kind == HEAP_OP_REALLOC && op.a0 && result) { if (result == op.a0) ++heap_counts.grown_in_place; else ++heap_counts.moved; }
        if (heap_model.region_count > heap_counts.regions_max) heap_counts.regions_max = heap_model.region_count;
        heap_after(&op, result);
    }
    ++heap_counts.sequences;
    return 0;
}
/* 64 regions: 1 MiB reservations with 1 MiB gaps; the 65th and 66th
 * requests are committed but not added and return NULL. */
static int heap_region_limit(void) {
    heap_reset(1, HEAP_POLICY_LIMIT, 1);
    for (unsigned step = 0; step < 66; ++step) {
        heap_op op = {HEAP_OP_MALLOC, 0xf0000u, 0};
        heap_log_count = 0;
        if (heap_call(heap_target(&op, 0), op.a0, 0) || heap_check(step, &op, heap_frame.out_eax)) return 1;
        if ((step < 64) != (heap_frame.out_eax != 0)) return heap_fail(step, &op, "region limit result", step, heap_frame.out_eax, 0);
        if (step >= 64 && heap_log_count != 2u * 257u) return heap_fail(step, &op, "two growth attempts", 514, heap_log_count, 0);
    }
    if (heap_model.region_count != 64) return heap_fail(66, &(heap_op){HEAP_OP_MALLOC, 0, 0}, "regions", heap_model.region_count, 64, 0);
    printf("PASS: rtl_heap region limit: 64 regions, the 65th and 66th requests grow twice (2 x 257 OS calls) and return NULL\n");
    return 0;
}

/* ------------------------------------------------------------------------
 * Installed: the same sequence without and with the shadow. */
#define HEAP_STEPS_MAX 400u
typedef struct heap_observed { uint32_t eax, ecx, edx; uint64_t below; } heap_observed;
static heap_observed heap_plain[HEAP_STEPS_MAX];
static uint64_t heap_hash(const BYTE *p, size_t n) { uint64_t h = 1469598103934665603ull; for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull; return h; }
static WCHAR heap_trace_path[MAX_PATH];   /* The trace of the next installed sequence; empty: none */
static int heap_shadow_install(void) {
    rtl_options o;
    memset(&o, 0, sizeof(o));
    o.image = mapped; o.expected_base = 0x00400000u; o.image_size = RTL_IMAGE_SIZE;
    o.resolve = rtl_cw_resolve;
    o.stack_lo = (uintptr_t)rtl_stack; o.stack_hi = (uintptr_t)(rtl_stack + RTL_STACK_DWORDS);
    o.cw_image = rtl_cw;
    wcscpy(o.heap_trace, heap_trace_path);
    if (!rtl_module_range((const void *)heap_shadow_install, &o.hook_lo, &o.hook_hi)) return fail("harness module range");
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) o.selected[i] = rtl_imports[i].shim;
    o.shadow[RTL_I_calloc] = o.shadow[RTL_I_realloc] = o.shadow[RTL_I_free] = 1;
    if (!rtl_install(&o)) { fprintf(stderr, "FAIL: %s: rtl shim refused: %s\n", current_function, rtl_reason); return 1; }
    return 0;
}
static BYTE heap_code_before[RTL_HEAP_ENTRY_COUNT][6];
static uint32_t heap_slots_before[2];
/* The kind of each step of the plain run, for the trace check. */
static unsigned heap_plain_kind[HEAP_STEPS_MAX];
static int heap_trace_fail(const char *what) {
    fprintf(stderr, "FAIL: %s case %u: heap trace: %s\n", current_function, heap_sequence, what);
    return 1;
}
/* The trace of an installed sequence (rtl_heap_shadow.h): the attach
 * line with the heap's blocks, one op line per step with the function, the
 * origin and the original's result, the end line; the exit fields report it
 * complete. */
static int heap_trace_check(unsigned steps, const char *fields) {
    static const char *const fn[HEAP_OP_KINDS] = {"malloc", "calloc", "free", "realloc", "expand", "malloc", "free"};
    static const char *const from[HEAP_OP_KINDS] = {"external", "import", "import", "import", "external", "internal", "internal"};
    if (!strstr(fields, ",\"rtl_heap_trace_lost\":0,\"rtl_heap_trace_ok\":1")) return heap_trace_fail("exit fields");
    HANDLE f = CreateFileW(heap_trace_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return heap_trace_fail("no file");
    static char text[1u << 20];
    DWORD size = 0;
    BOOL read = ReadFile(f, text, sizeof(text) - 1u, &size, NULL);
    CloseHandle(f);
    DeleteFileW(heap_trace_path);
    if (!read || size >= sizeof(text) - 1u) return heap_trace_fail("unreadable");
    text[size] = 0;
    unsigned line = 0, blocks = 0, ops = 0, attach_blocks = ~0u;
    for (char *p = text, *next; *p; p = next, ++line) {
        char *end = strchr(p, '\n');
        if (!end) return heap_trace_fail("last line unterminated");
        *end = 0;
        next = end + 1;
        if (line == 0) {
            const char *b = strstr(p, ",\"blocks\":");
            if (strncmp(p, "{\"kind\":\"attach\",\"schema\":1,", 28) || !b || !strstr(p, ",\"env_lengths\":[")) return heap_trace_fail("attach line");
            attach_blocks = (unsigned)strtoul(b + 10, NULL, 10);
        } else if (!strncmp(p, "{\"kind\":\"block\",", 16)) {
            if (ops) return heap_trace_fail("block line after an operation");
            ++blocks;
        } else if (!strncmp(p, "{\"op\":", 6)) {
            if (ops >= steps || strtoul(p + 6, NULL, 10) != ops) return heap_trace_fail("op index");
            unsigned kind = heap_plain_kind[ops];
            char want[96];
            snprintf(want, sizeof(want), ",\"fn\":\"%s\",\"from\":\"%s\",", fn[kind], from[kind]);
            const char *r = strstr(p, ",\"result\":\"");
            uint32_t result = r ? (uint32_t)strtoul(r + 11, NULL, 16) : ~0u;
            if (!strstr(p, want) || result != (kind == HEAP_OP_FREE || kind == HEAP_OP_DELETE ? 0u : heap_plain[ops].eax)) {
                fprintf(stderr, "FAIL: %s case %u: heap trace step %u (%s): %s\n", current_function, heap_sequence, ops, heap_op_names[kind], p);
                return 1;
            }
            ++ops;
        } else if (!strncmp(p, "{\"kind\":\"end\",", 14)) {
            char want[64];
            snprintf(want, sizeof(want), "{\"kind\":\"end\",\"ops\":%u,\"lost\":0}", steps);
            if (strcmp(p, want) || *next) return heap_trace_fail("end line");
        } else return heap_trace_fail("unknown line");
    }
    if (blocks != attach_blocks || ops != steps) return heap_trace_fail("block or op count");
    return 0;
}
static int heap_installed_sequence(unsigned steps, uint32_t platform, unsigned policy, uint32_t seed, int plant) {
    uint32_t start = seed | 1u;
    /* Plain run. */
    random_state = start;
    heap_reset(platform, policy, seed);
    heap_live_count = 0;
    unsigned counts[HEAP_OP_KINDS] = {0}, os_total = 0;
    for (unsigned step = 0; step < steps; ++step) {
        heap_op op = heap_generate();
        heap_log_count = 0;
        if (heap_call(heap_target(&op, 1), op.a0, op.a1)) return 1;
        heap_observed *o = &heap_plain[step];
        o->eax = heap_frame.out_eax; o->ecx = heap_frame.out_ecx; o->edx = heap_frame.out_edx;
        o->below = heap_hash((const BYTE *)(uintptr_t)(RTL_ARGS_ADDRESS - RTL_BELOW), RTL_BELOW);
        heap_plain_kind[step] = op.kind;
        ++counts[op.kind]; os_total += heap_log_count;
        heap_after(&op, o->eax);
    }
    es2_heap plain_state;
    heap_real_state(&plain_state);
    /* Shadow run: same operations, same OS decisions. */
    random_state = start;
    heap_reset(platform, policy, seed);
    heap_live_count = 0;
    if (heap_shadow_install()) return 1;
    for (unsigned step = 0; step < steps; ++step) {
        heap_op op = heap_generate();
        if (plant && step == steps / 2u) {
            /* A planted model error: the rover block's size in the model. */
            uint32_t rover = rtl_heap_model.rover, size;
            if (es2_heap_words_find(&rtl_heap_state.words, rover - 4u, &size)) es2_heap_words_write(&rtl_heap_state.words, rover - 4u, size ^ 0x40u);
            op.kind = HEAP_OP_MALLOC; op.a0 = 24;   /* reaches the rover first */
        }
        heap_log_count = 0;   /* the emulated OS's own state as in the plain run */
        if (heap_call(heap_target(&op, 1), op.a0, op.a1)) return 1;
        const heap_observed *o = &heap_plain[step];
        if (!plant && (heap_frame.out_eax != o->eax || heap_frame.out_ecx != o->ecx || heap_frame.out_edx != o->edx)) {
            fprintf(stderr, "FAIL: %s case %u: installed step %u %s: EAX/ECX/EDX %08x %08x %08x, without the shim %08x %08x %08x\n",
                    current_function, heap_sequence, step, heap_op_names[op.kind], heap_frame.out_eax, heap_frame.out_ecx, heap_frame.out_edx, o->eax, o->ecx, o->edx);
            return 1;
        }
        if (!plant && heap_hash((const BYTE *)(uintptr_t)(RTL_ARGS_ADDRESS - RTL_BELOW), RTL_BELOW) != o->below) {
            fprintf(stderr, "FAIL: %s case %u: installed step %u %s: stack below the arguments differs from the run without the shim\n",
                    current_function, heap_sequence, step, heap_op_names[op.kind]);
            return 1;
        }
        heap_after(&op, heap_frame.out_eax);
    }
    es2_heap shadow_state;
    heap_real_state(&shadow_state);
    uint64_t mismatches = rtl_heap_import_mismatches[RTL_I_calloc] + rtl_heap_import_mismatches[RTL_I_realloc] +
                          rtl_heap_import_mismatches[RTL_I_free] + rtl_heap_detour_mismatches;
    if (plant) {
        if (mismatches != 1 || rtl_heap_resyncs != 1 || !rtl_heap_model_ok || rtl_heap_external_ops == 0 || !strstr(rtl_heap_mismatch_detail, "malloc:detour:"))
            { fprintf(stderr, "FAIL: %s case %u: planted error: mismatches %llu resyncs %llu detail %s\n", current_function, heap_sequence,
                      (unsigned long long)mismatches, (unsigned long long)rtl_heap_resyncs, rtl_heap_mismatch_detail); return 1; }
        printf("PASS: rtl_heap planted model error caught once (%s), model resynchronized, later operations agree\n", rtl_heap_mismatch_detail);
    } else {
        if (memcmp(&plain_state, &shadow_state, sizeof(plain_state))) { fprintf(stderr, "FAIL: %s case %u: heap state differs with the shim\n", current_function, heap_sequence); return 1; }
        if (mismatches || rtl_heap_resyncs || rtl_heap_skipped || !rtl_heap_model_ok) {
            fprintf(stderr, "FAIL: %s case %u: installed shadow mismatches %llu resyncs %llu skipped %llu first %s\n", current_function, heap_sequence,
                    (unsigned long long)mismatches, (unsigned long long)rtl_heap_resyncs, (unsigned long long)rtl_heap_skipped, rtl_heap_mismatch_detail);
            return 1;
        }
        unsigned imports = counts[HEAP_OP_CALLOC] + counts[HEAP_OP_REALLOC] + counts[HEAP_OP_FREE];
        uint64_t calls = rtl_counters[RTL_I_calloc].calls + rtl_counters[RTL_I_realloc].calls + rtl_counters[RTL_I_free].calls;
        if (calls != imports || rtl_counters[RTL_I_calloc].calls != counts[HEAP_OP_CALLOC] ||
            rtl_heap_internal_ops != counts[HEAP_OP_NEW] + counts[HEAP_OP_DELETE] ||
            rtl_heap_external_ops != counts[HEAP_OP_MALLOC] + counts[HEAP_OP_EXPAND] || rtl_heap_os_calls != os_total ||
            rtl_heap_foreign_ops || rtl_heap_os_unobserved || rtl_foreign_calls) {
            fprintf(stderr, "FAIL: %s case %u: installed counters: imports %llu/%u internal %llu external %llu os %llu/%u foreign %llu unobserved %llu\n",
                    current_function, heap_sequence, (unsigned long long)calls, imports, (unsigned long long)rtl_heap_internal_ops,
                    (unsigned long long)rtl_heap_external_ops, (unsigned long long)rtl_heap_os_calls, os_total,
                    (unsigned long long)rtl_heap_foreign_ops, (unsigned long long)rtl_heap_os_unobserved);
            return 1;
        }
    }
    char fields[2048];
    int n = rtl_heap_fields(fields, sizeof(fields));
    if (n <= 0 || !strstr(fields, "\"rtl_heap_internal_ops\":")) return fail("heap exit fields");
    if (heap_trace_path[0] && heap_trace_check(steps, fields)) return 1;
    static char tick[8192];
    if (rtl_tick_fields(tick, sizeof(tick), FALSE) <= 0 || !strstr(tick, plant ? ",\"rtl_heap_shadow_mismatches\":1" : ",\"rtl_heap_shadow_mismatches\":0"))
        return fail("heap tick field");
    if (!rtl_uninstall()) return fail("rtl shim uninstall");
    for (unsigned i = 0; i < RTL_HEAP_ENTRY_COUNT; ++i)
        if (memcmp(heap_code_before[i], rtl_cw + rtl_heap_entries[i].rva, rtl_heap_entries[i].length)) return fail("CW3220 entry bytes not restored");
    if (heap_word(heap_cw(RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VA_ENTRY)) != heap_slots_before[0] ||
        heap_word(heap_cw(RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VF_ENTRY)) != heap_slots_before[1]) return fail("CW3220 OS slots not restored");
    return 0;
}

/* ------------------------------------------------------------------------
 * Calloc/realloc/free replaced by the own heap (proxy/rtl_heap_own.h).
 *
 * Same addresses: one sequence of calloc/realloc/free through DBSIM's thunks
 * runs once on the original exports and once with the three imports
 * replaced; the own heap gets the emulated OS (the same decisions) and
 * CW3220's head-block address, so every result, every OS call, the
 * allocator's variables and every committed byte of the window (allocator
 * words and user data) must be equal. ECX/EDX must be equal where the own
 * heap reproduces them (free; calloc except an out-of-memory NULL's EDX);
 * nothing may be written below the return address; CW3220's own heap code
 * must not have run. */
static uint64_t heap_window_hash(void) {
    uint64_t h = 1469598103934665603ull;
    BYTE *p = heap_window, *end = heap_window + HEAP_WINDOW;
    while (p < end) {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery(p, &m, sizeof(m))) break;
        BYTE *next = (BYTE *)m.BaseAddress + m.RegionSize;
        if (next > end) next = end;
        if (m.State == MEM_COMMIT) {
            h = (h ^ (uint64_t)(uintptr_t)p) * 1099511628211ull;
            for (const uint32_t *q = (const uint32_t *)p; (const BYTE *)q < next; ++q) h = (h ^ *q) * 1099511628211ull;
        }
        p = next;
    }
    return h;
}
/* A failure of a replaced or mixed sequence, in the "case N" form the
 * mutation runs look for. */
static int heap_case_fail(const char *what) {
    fprintf(stderr, "FAIL: %s case %u: %s\n", current_function, heap_sequence, what);
    return 1;
}
static heap_op heap_generate_import(void) {
    for (;;) {
        heap_op op = heap_generate();
        if (op.kind == HEAP_OP_CALLOC || op.kind == HEAP_OP_FREE || op.kind == HEAP_OP_REALLOC) return op;
    }
}
static int heap_replaced_install(uint32_t platform, uint32_t sentinel) {
    rtl_options o;
    memset(&o, 0, sizeof(o));
    o.image = mapped; o.expected_base = 0x00400000u; o.image_size = RTL_IMAGE_SIZE;
    o.resolve = rtl_cw_resolve;
    o.stack_lo = (uintptr_t)rtl_stack; o.stack_hi = (uintptr_t)(rtl_stack + RTL_STACK_DWORDS);
    if (!rtl_module_range((const void *)heap_replaced_install, &o.hook_lo, &o.hook_hi)) return heap_case_fail("harness module range");
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) o.selected[i] = rtl_imports[i].shim;
    o.modes[RTL_I_calloc] = o.modes[RTL_I_realloc] = o.modes[RTL_I_free] = ES2_HOOK_REPLACED;
    rtl_heap_own_valloc = heap_os_valloc;
    rtl_heap_own_vfree = heap_os_vfree;
    rtl_heap_own_sentinel_at = sentinel;
    rtl_heap_own_platform_at = (int32_t)platform;
    if (!rtl_install(&o)) { fprintf(stderr, "FAIL: %s: rtl shim refused: %s\n", current_function, rtl_reason); return 1; }
    return 0;
}
static int heap_replaced_done(void) {
    if (!rtl_uninstall()) return heap_case_fail("rtl shim uninstall");
    rtl_heap_own_valloc = VirtualAlloc;
    rtl_heap_own_vfree = VirtualFree;
    rtl_heap_own_sentinel_at = 0;
    rtl_heap_own_platform_at = -1;
    return 0;
}
typedef struct heap_seen { uint32_t eax, ecx, edx, os_count; uint64_t os; } heap_seen;
static heap_seen heap_seen_plain[HEAP_STEPS_MAX];
static uint64_t heap_os_hash(void) { return heap_hash((const BYTE *)heap_log, (heap_log_count < HEAP_LOG ? heap_log_count : HEAP_LOG) * sizeof(heap_log[0])); }
static unsigned heap_replaced_ops, heap_replaced_null, heap_replaced_os, heap_replaced_registers;
static int heap_replaced_sequence(unsigned steps, uint32_t platform, unsigned policy, uint32_t seed) {
    uint32_t start = seed | 1u;
    random_state = start;
    heap_reset(platform, policy, seed);
    heap_live_count = 0;
    unsigned counts[HEAP_OP_KINDS] = {0}, os_total = 0;
    for (unsigned step = 0; step < steps; ++step) {
        heap_op op = heap_generate_import();
        heap_log_count = 0;
        if (heap_call(heap_target(&op, 1), op.a0, op.a1)) return 1;
        heap_seen s = {heap_frame.out_eax, heap_frame.out_ecx, heap_frame.out_edx, heap_log_count, heap_os_hash()};
        heap_seen_plain[step] = s;
        ++counts[op.kind]; os_total += heap_log_count;
        heap_after(&op, s.eax);
    }
    es2_heap plain;
    heap_real_state(&plain);
    uint64_t plain_window = heap_window_hash();
    /* Replaced: the same operations and OS decisions on the own heap. */
    random_state = start;
    heap_reset(platform, policy, seed);
    heap_live_count = 0;
    uint32_t sentinel = heap_cw(RTL_HEAP_SENTINEL);
    if (heap_replaced_install(platform, sentinel)) return 1;
    for (unsigned step = 0; step < steps; ++step) {
        heap_op op = heap_generate_import();
        heap_log_count = 0;
        if (heap_call(heap_target(&op, 1), op.a0, op.a1)) return 1;
        const heap_seen *s = &heap_seen_plain[step];
        uint32_t total = op.a0 * op.a1;
        BOOL refused = op.kind == HEAP_OP_CALLOC && (!total || (int32_t)es2_heap_block_size(total) < 16);
        BOOL registers = op.kind == HEAP_OP_FREE || (op.kind == HEAP_OP_CALLOC && (s->eax || refused));
        const char *what = heap_frame.out_eax != s->eax ? "EAX" :
                           heap_log_count != s->os_count || heap_os_hash() != s->os ? "OS calls" :
                           op.kind == HEAP_OP_CALLOC && heap_frame.out_ecx != s->ecx ? "ECX" :
                           registers && (heap_frame.out_ecx != s->ecx || heap_frame.out_edx != s->edx) ? "ECX/EDX" :
                           memcmp((const BYTE *)(uintptr_t)(RTL_ARGS_ADDRESS - RTL_BELOW), rtl_below_pattern, RTL_BELOW - 4u) ? "stack below the return address" : NULL;
        if (what) {
            fprintf(stderr, "FAIL: %s case %u: replaced step %u %s(%08x, %08x): %s: EAX/ECX/EDX %08x %08x %08x, original %08x %08x %08x\n",
                    current_function, heap_sequence, step, heap_op_names[op.kind], op.a0, op.a1, what,
                    heap_frame.out_eax, heap_frame.out_ecx, heap_frame.out_edx, s->eax, s->ecx, s->edx);
            return 1;
        }
        ++heap_replaced_ops; heap_replaced_os += heap_log_count;
        if (registers) ++heap_replaced_registers;
        if (op.kind != HEAP_OP_FREE && !s->eax) ++heap_replaced_null;
        heap_after(&op, heap_frame.out_eax);
    }
    const es2_heap *m = &rtl_heap_own;
    if (m->rover != plain.rover || m->region_count != plain.region_count || m->committed_end != plain.committed_end ||
        m->reserved_end != plain.reserved_end || m->granularity != plain.granularity ||
        memcmp(m->region_base, plain.region_base, sizeof(m->region_base)) || memcmp(m->region_size, plain.region_size, sizeof(m->region_size)))
        { fprintf(stderr, "FAIL: %s case %u: own heap variables differ from the original's\n", current_function, heap_sequence); return 1; }
    if (heap_window_hash() != plain_window) { fprintf(stderr, "FAIL: %s case %u: window bytes differ from the original run\n", current_function, heap_sequence); return 1; }
    es2_heap untouched;
    heap_real_state(&untouched);
    if (untouched.region_count || untouched.rover != sentinel || untouched.committed_end || untouched.reserved_end || untouched.granularity)
        { fprintf(stderr, "FAIL: %s case %u: CW3220's heap variables changed in the replaced run\n", current_function, heap_sequence); return 1; }
    uint64_t calls = rtl_counters[RTL_I_calloc].calls + rtl_counters[RTL_I_realloc].calls + rtl_counters[RTL_I_free].calls;
    if (calls != steps || rtl_counters[RTL_I_calloc].calls != counts[HEAP_OP_CALLOC] || rtl_skipped[RTL_I_calloc] ||
        rtl_skipped[RTL_I_realloc] || rtl_skipped[RTL_I_free] || rtl_heap_foreign_frees || rtl_heap_own_os_calls != os_total || rtl_foreign_calls)
        { fprintf(stderr, "FAIL: %s case %u: replaced counters: calls %llu/%u os %llu/%u foreign %llu\n", current_function, heap_sequence,
                  (unsigned long long)calls, steps, (unsigned long long)rtl_heap_own_os_calls, os_total, (unsigned long long)rtl_heap_foreign_frees); return 1; }
    if (es2_heap_walk(&rtl_heap_own, rtl_heap_own_read, NULL, NULL, NULL) < 0) return heap_case_fail("own heap walk");
    return heap_replaced_done();
}

/* Mixed: own blocks (calloc through the thunk) and blocks of CW3220's own
 * heap (its _malloc called directly, as CW3220-internal code does) side by
 * side over the emulated OS; frees and reallocs through the thunks. A
 * foreign pointer reaches the original export (counted), an own one the own
 * heap; both heaps stay valid and every live block keeps its data. */
typedef struct heap_block { uint32_t p, n, seed; BOOL own; } heap_block;
static heap_block heap_blocks[HEAP_LIVE];
static unsigned heap_block_count;
static uint32_t heap_pattern(uint32_t *state) { uint32_t x = *state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *state = x; }
static void heap_block_fill(const heap_block *b) {
    uint32_t s = b->seed | 1u;
    for (uint32_t i = 0; i < b->n; ++i) ((BYTE *)(uintptr_t)b->p)[i] = (BYTE)heap_pattern(&s);
}
static BOOL heap_block_intact(const heap_block *b, uint32_t n) {
    uint32_t s = b->seed | 1u;
    for (uint32_t i = 0; i < n && i < b->n; ++i) if (((const BYTE *)(uintptr_t)b->p)[i] != (BYTE)heap_pattern(&s)) return FALSE;
    return TRUE;
}
static unsigned heap_mixed_foreign, heap_mixed_ops;
static int heap_mixed_sequence(unsigned steps, uint32_t platform, uint32_t seed) {
    random_state = seed | 1u;
    heap_reset(platform, HEAP_POLICY_GAPS, seed);
    heap_block_count = 0;
    if (heap_replaced_install(platform, 0)) return 1;
    unsigned foreign = 0;
    for (unsigned step = 0; step < steps; ++step) {
        uint32_t r = random32() % 10u;
        if (heap_block_count >= HEAP_LIVE - 1u && r < 4u) r = 5u;
        if (r < 3u || r == 3u) {
            heap_block b = {0, heap_size() % 70000u, random32(), r < 3u};
            if (b.own) { if (heap_call(heap_target(&(heap_op){HEAP_OP_CALLOC, 1, b.n}, 1), 1, b.n)) return 1; }
            else if (heap_call(heap_export[RTL_HEAP_MALLOC], b.n, 0)) return 1;
            b.p = heap_frame.out_eax;
            if (!b.p) continue;
            if (b.own != rtl_heap_own_has(b.p)) return heap_case_fail("block on the wrong heap");
            if (b.own) for (uint32_t i = 0; i < b.n; ++i) if (((const BYTE *)(uintptr_t)b.p)[i]) return heap_case_fail("calloc block not zero-filled");
            heap_block_fill(&b);
            heap_blocks[heap_block_count++] = b;
        } else if (heap_block_count) {
            unsigned k = random32() % heap_block_count;
            heap_block *b = &heap_blocks[k];
            if (!heap_block_intact(b, b->n)) return heap_case_fail("live block data changed");
            if (!b->own) ++foreign;
            if (r < 7u) {
                if (heap_call(heap_target(&(heap_op){HEAP_OP_FREE, b->p, 0}, 1), b->p, 0)) return 1;
                heap_blocks[k] = heap_blocks[--heap_block_count];
            } else {
                uint32_t n = 1u + random32() % (b->n + 2000u);
                if (heap_call(heap_target(&(heap_op){HEAP_OP_REALLOC, b->p, n}, 1), b->p, n)) return 1;
                uint32_t q = heap_frame.out_eax;
                if (!q) continue;   /* out of memory: the old block stays */
                if (b->own != rtl_heap_own_has(q)) return heap_case_fail("realloc result on the wrong heap");
                b->p = q;
                if (!heap_block_intact(b, n)) return heap_case_fail("realloc lost data");
                b->n = n; b->seed = random32();
                heap_block_fill(b);
            }
        }
        ++heap_mixed_ops;
    }
    for (unsigned k = 0; k < heap_block_count; ++k) if (!heap_block_intact(&heap_blocks[k], heap_blocks[k].n)) return heap_case_fail("live block data changed");
    es2_heap cw;
    heap_real_state(&cw);
    if (es2_heap_walk(&cw, heap_real_read, NULL, NULL, NULL) < 0) return heap_case_fail("CW3220 heap walk after the mixed run");
    if (es2_heap_walk(&rtl_heap_own, rtl_heap_own_read, NULL, NULL, NULL) < 0) return heap_case_fail("own heap walk after the mixed run");
    if (rtl_heap_foreign_frees != foreign || rtl_skipped[RTL_I_free] || rtl_skipped[RTL_I_realloc])
        { fprintf(stderr, "FAIL: %s case %u: foreign frees %llu, expected %u\n", current_function, heap_sequence,
                  (unsigned long long)rtl_heap_foreign_frees, foreign); return 1; }
    heap_mixed_foreign += foreign;
    return heap_replaced_done();
}

/* Selection: --rtl-heap (all) or rtl_heap_<name> (a mutation run: the same
 * cases, failures reported under that name). */
static int rtl_heap_cases(const char *selection) {
    current_function = strcmp(selection, "--rtl-heap") ? selection : "rtl_heap";
    if (rtl_map_cw3220(getenv("ES2_DATA_ROOT"))) return 1;
    heap_window = VirtualAlloc(NULL, HEAP_WINDOW, MEM_RESERVE, PAGE_NOACCESS);
    if (!heap_window) return fail("heap window reservation");
    *(uint32_t *)(rtl_cw + RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VA_ENTRY) = (uint32_t)(uintptr_t)heap_os_valloc;
    *(uint32_t *)(rtl_cw + RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VF_ENTRY) = (uint32_t)(uintptr_t)heap_os_vfree;
    for (unsigned i = 0; i < RTL_HEAP_ENTRY_COUNT; ++i) {
        heap_export[i] = rtl_cw_export(rtl_heap_entries[i].symbol);
        if (heap_export[i] != heap_cw(rtl_heap_entries[i].rva)) return fail("heap export address");
        memcpy(heap_code_before[i], rtl_cw + rtl_heap_entries[i].rva, rtl_heap_entries[i].length);
    }
    heap_slots_before[0] = heap_word(heap_cw(RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VA_ENTRY));
    heap_slots_before[1] = heap_word(heap_cw(RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VF_ENTRY));
    heap_new = rtl_cw_export("@$bnew$qui");
    heap_delete = rtl_cw_export("@$bdele$qpv");
    if (!heap_new || !heap_delete) return fail("operator new/delete exports");
    /* operator new calls its new handler (statically set: it throws xalloc)
     * while malloc returns NULL; cleared here, so new returns NULL like malloc
     * when the emulated OS refuses memory. */
    *(volatile uint32_t *)(uintptr_t)heap_cw(0x21fb4u) = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        uint32_t *slot = (uint32_t *)(mapped + RTL_IAT_RVA + 4 * i);
        *slot = rtl_cw_export(rtl_imports[i].symbol);
        if (!*slot) return fail("CW3220 export of an import missing");
    }
    for (unsigned i = 0; i < RTL_BELOW; ++i) rtl_below_pattern[i] = (BYTE)random32();
    memset(&heap_shadow, 0, sizeof(heap_shadow));
    es2_heap_words_init(&heap_shadow.words, heap_keys, heap_values, HEAP_WORD_BITS);
    heap_shadow.journal = heap_journal;
    heap_shadow.journal_capacity = 4096;
    es2_heap_shadow_env(&heap_env, &heap_shadow);

    if (heap_region_limit()) return 1;
    unsigned sequences = quick_tier ? 160u : 1600u, installed = quick_tier ? 16u : 160u;
    static const uint32_t platforms[3] = {0, 1, 2};
    for (heap_sequence = 0; heap_sequence < sequences; ++heap_sequence) {
        current_case = heap_sequence; current_case_valid = 1;
        if (heap_direct_sequence(300, platforms[heap_sequence % 3u], heap_sequence % 3u == 0 ? HEAP_POLICY_FOLLOW :
                                 heap_sequence % 3u == 1 ? HEAP_POLICY_GAPS : HEAP_POLICY_MIXED, random32())) return 1;
    }
    printf("PASS: rtl_heap %u direct sequences, %u operations (malloc %u, calloc %u, free %u, realloc %u (%u in place, %u moved), "
           "_expand %u, new %u, delete %u; %u NULL results), %u OS calls, up to %u regions: result offsets, OS calls, "
           "allocator variables and every allocator word equal to the original\n",
           heap_counts.sequences, heap_counts.ops, heap_counts.kinds[HEAP_OP_MALLOC], heap_counts.kinds[HEAP_OP_CALLOC],
           heap_counts.kinds[HEAP_OP_FREE], heap_counts.kinds[HEAP_OP_REALLOC], heap_counts.grown_in_place, heap_counts.moved,
           heap_counts.kinds[HEAP_OP_EXPAND], heap_counts.kinds[HEAP_OP_NEW], heap_counts.kinds[HEAP_OP_DELETE], heap_counts.null_results,
           heap_counts.os_calls, heap_counts.regions_max);
    printf("PATHS:");
    for (unsigned i = 0; i < HEAP_PATH_COUNT; ++i) printf("%s %s %u", i ? "," : "", heap_path_names[i], heap_paths[i]);
    printf("\n");
    for (unsigned i = 0; i < HEAP_PATH_COUNT; ++i)
        if (!heap_paths[i]) { fprintf(stderr, "FAIL: %s: allocator path not reached: %s\n", current_function, heap_path_names[i]); return 1; }
    for (unsigned k = 0; k < installed; ++k, ++heap_sequence) {
        current_case = heap_sequence;
        /* The first installed sequence also writes the heap trace. */
        heap_trace_path[0] = 0;
        if (k == 0) {
            WCHAR temp[MAX_PATH];
            DWORD n = GetTempPathW(MAX_PATH, temp);
            if (!n || n > MAX_PATH - 40u) return fail("heap trace: temporary path");
            swprintf(heap_trace_path, MAX_PATH, L"%lses2-heap-trace-%lu.ndjson", temp, (unsigned long)GetCurrentProcessId());
            DeleteFileW(heap_trace_path);
        }
        if (heap_installed_sequence(200, platforms[k % 3u], k % 2u ? HEAP_POLICY_GAPS : HEAP_POLICY_MIXED, random32(), 0)) return 1;
    }
    heap_trace_path[0] = 0;
    current_case = heap_sequence;
    if (heap_installed_sequence(120, 0, HEAP_POLICY_FOLLOW, random32(), 1)) return 1;
    current_case_valid = 0;
    printf("PASS: rtl_heap %u installed sequences (calloc/realloc/free through DBSIM's thunks in mode shadow, malloc/_expand external, "
           "new/delete CW3220-internal): registers, stack below the arguments and heap identical to the run without the shim; "
           "0 shadow mismatches; CW3220 entries and slots restored\n", installed);
    /* Replaced. */
    for (unsigned k = 0; k < installed; ++k, ++heap_sequence) {
        current_case = heap_sequence;
        if (heap_replaced_sequence(HEAP_STEPS_MAX, platforms[k % 3u], k % 3u == 0 ? HEAP_POLICY_FOLLOW : k % 3u == 1 ? HEAP_POLICY_GAPS : HEAP_POLICY_MIXED,
                                   random32())) return 1;
    }
    printf("PASS: rtl_heap %u replaced sequences, %u operations (%u NULL results, %u OS calls, ECX/EDX compared in %u): "
           "calloc/realloc/free through DBSIM's thunks on the own heap give the original's results, OS calls, allocator variables, "
           "window bytes and modelled registers; nothing below the return address; CW3220's heap untouched\n",
           installed, heap_replaced_ops, heap_replaced_null, heap_replaced_os, heap_replaced_registers);
    unsigned mixed = quick_tier ? 8u : 80u;
    for (unsigned k = 0; k < mixed; ++k, ++heap_sequence) {
        current_case = heap_sequence;
        if (heap_mixed_sequence(300, platforms[k % 3u], random32())) return 1;
    }
    printf("PASS: rtl_heap %u mixed sequences, %u operations: %u frees/reallocs of CW3220-heap blocks through the thunks reached the "
           "original; both heaps valid, every live block kept its data, calloc blocks zero-filled\n", mixed, heap_mixed_ops, heap_mixed_foreign);
    puts("PASS: rtl_heap in-vitro run against the mapped CW3220.DLL; no entry point executed; no game process started");
    return 0;
}
