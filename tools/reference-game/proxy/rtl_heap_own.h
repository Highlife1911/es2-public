/* The heap imports replaced by own code (rtl_imports.md, section
 * "the heap, replaced"). Included by rtl_imports.h after rtl_heap_shadow.h.
 *
 * `calloc`, `realloc` and `free` in mode replaced (all three, all selected):
 * DBSIM's calls reach an own instance of the core heap model
 * (core/src/rtl_heap.c), which is the allocator itself. Its head block is
 * a static of this file, its regions are own reservations made with the
 * original's VirtualAlloc/VirtualFree calls (reserve per 1 or 4 MiB, commit
 * and decommit per page), and user data is zero-filled and moved in real
 * memory. CW3220's own allocations (stream buffers, start-up copies, operator
 * new inside CW3220, the file buffers of rtl_io_shadow.h) stay on CW3220's heap; nothing in
 * CW3220 is patched. A free or realloc of a pointer outside the own regions
 * goes to the original export and is counted (rtl_heap_foreign_frees; none is
 * expected: the shim is armed before DBSIM's first instruction, so every
 * block DBSIM frees comes from its own calloc import).
 *
 * The C part runs on the private stack of rtl_imports.h through the
 * replaced adapters and writes nothing below the caller's ESP (the
 * original's frames leave their residue there; no DBSIM code reads it,
 * rtl_imports.md "the heap, replaced").
 * EAX/ECX/EDX as the original leaves them:
 *   calloc  EAX = the block; ECX = 0 and EDX = count*n after the zero fill;
 *           for NULL ECX = count*n and EDX unchanged (exact for a refused
 *           size; out of memory the original's EDX comes from the growth
 *           code and is not reproduced);
 *   free    every path (EAX = the merged block, ECX/EDX from the join or the
 *           list insertion, see rtl_heap_own_free);
 *   realloc EAX = the block; ECX/EDX unchanged (the original's depend on the
 *           nested calls; dead at both DBSIM call sites).
 * LastError is preserved. */

typedef LPVOID (WINAPI *rtl_heap_valloc_fn)(LPVOID, SIZE_T, DWORD, DWORD);
typedef BOOL (WINAPI *rtl_heap_vfree_fn)(LPVOID, SIZE_T, DWORD);
/* The OS calls of the own heap; the in-vitro harness points them to its
 * emulated OS before installing. */
static rtl_heap_valloc_fn rtl_heap_own_valloc = VirtualAlloc;
static rtl_heap_vfree_fn rtl_heap_own_vfree = VirtualFree;
/* 0: the static head block below; the harness may name another address. */
static uint32_t rtl_heap_own_sentinel_at;
/* -1: detected as CW3220's start-up does (rtl_heap_platform_detect). */
static int32_t rtl_heap_own_platform_at = -1;

static uint32_t rtl_heap_own_head[4];   /* words -8, -4 (0), links +0, +4 */
static es2_heap rtl_heap_own;
static es2_heap_env rtl_heap_own_env;
static BOOL rtl_heap_own_active, rtl_heap_own_ready;   /* rtl_heap_replaced: rtl_imports.h */
static uint64_t rtl_heap_foreign_frees, rtl_heap_own_os_calls, rtl_heap_own_os_failed;
static int32_t rtl_heap_own_blocks = -1;   /* the exit record's walk when the heap is not ready (fixtures) */

static uint32_t rtl_heap_own_read(void *context, uint32_t address) { (void)context; return *(const volatile uint32_t *)(uintptr_t)address; }
static void rtl_heap_own_write(void *context, uint32_t address, uint32_t value) { (void)context; *(volatile uint32_t *)(uintptr_t)address = value; }
static void rtl_heap_own_fill(void *context, uint32_t address, uint32_t count) { (void)context; memset((void *)(uintptr_t)address, 0, count); }
static void rtl_heap_own_move(void *context, uint32_t dest, uint32_t source, uint32_t count) {
    (void)context;
    memmove((void *)(uintptr_t)dest, (const void *)(uintptr_t)source, count);
}
static uint32_t rtl_heap_own_alloc(void *context, uint32_t address, uint32_t size, uint32_t type, uint32_t protect) {
    (void)context;
    ++rtl_heap_own_os_calls;
    uint32_t result = (uint32_t)(uintptr_t)rtl_heap_own_valloc((LPVOID)(uintptr_t)address, size, type, protect);
    if (!result) ++rtl_heap_own_os_failed;
    return result;
}
static uint32_t rtl_heap_own_release(void *context, uint32_t address, uint32_t size, uint32_t type) {
    (void)context;
    ++rtl_heap_own_os_calls;
    uint32_t result = (uint32_t)rtl_heap_own_vfree((LPVOID)(uintptr_t)address, size, type);
    if (result != 1u) ++rtl_heap_own_os_failed;
    return result;
}

/* CW3220's platform word (start-up code 0x418fc1): 2 when kernel32 exports
 * "Borland32", else 1 when GetVersion has bit 31 set, else 0. It selects the
 * reservation granularity (1 MiB for 1 and 2, else 4 MiB). */
static uint32_t rtl_heap_platform_detect(void) {
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    if (kernel && GetProcAddress(kernel, "Borland32")) return 2u;
    return (GetVersion() & 0x80000000u) ? 1u : 0u;
}

/* Is p inside one of the own regions? */
static BOOL rtl_heap_own_has(uint32_t p) {
    for (uint32_t i = 0; i < rtl_heap_own.region_count && i < ES2_HEAP_REGION_LIMIT; ++i)
        if (p - rtl_heap_own.region_base[i] < rtl_heap_own.region_size[i]) return TRUE;
    return FALSE;
}

/* free with the registers of CW3220's _free (0x417160): p = 0 leaves ECX/EDX
 * and gives EAX 0; a block without the in-use flag leaves ECX and gives
 * EAX = p, EDX = its header word; otherwise EAX = the start of the result
 * (the free block below when it absorbed p) and, when the block below
 * absorbed it, ECX = start + size and EDX = size, else (inserted after the
 * head block) ECX = the head block's former next block and EDX = the head
 * block. */
static void rtl_heap_own_free(uint32_t p, es2_rtl_regs *r) {
    r->eax = p;
    if (!p) return;
    uint32_t header = rtl_heap_own_read(NULL, p - 4u);
    if (!(header & 1u)) { r->edx = header; return; }
    uint32_t below = rtl_heap_own_read(NULL, p - 8u);
    BOOL joined = !(below & 1u);
    uint32_t start = joined ? p - below : p;
    es2_heap_free(&rtl_heap_own, &rtl_heap_own_env, p);
    uint32_t size = rtl_heap_own_read(NULL, start - 4u);
    r->eax = start;
    if (joined) { r->ecx = start + size; r->edx = size; }
    else { r->ecx = rtl_heap_own_read(NULL, start + 4u); r->edx = rtl_heap_own.sentinel; }
}

/* The C part of a replaced heap import (from rtl_replaced_c, on the private
 * stack). 0: done, rtl_call.out_* set; 1: a pointer outside the own regions,
 * the adapter jumps to the original export with the entry registers. */
static int rtl_heap_own_call(unsigned import) {
    DWORD error = GetLastError();
    const volatile uint32_t *a = (const volatile uint32_t *)(uintptr_t)(rtl_call.esp + 4u);
    es2_rtl_regs r = {rtl_call.eax, rtl_call.ecx, rtl_call.edx};
    if (import == RTL_I_calloc) {
        uint32_t count = a[0], n = a[1], total = count * n;
        r.eax = es2_heap_calloc(&rtl_heap_own, &rtl_heap_own_env, count, n);
        if (r.eax) { r.ecx = 0; r.edx = total; }
        else r.ecx = total;
    } else {
        uint32_t p = a[0];
        if (p && !rtl_heap_own_has(p)) {
            ++rtl_heap_foreign_frees;
            SetLastError(error);
            return 1;
        }
        if (import == RTL_I_free) rtl_heap_own_free(p, &r);
        else r.eax = es2_heap_realloc(&rtl_heap_own, &rtl_heap_own_env, p, a[1]);
    }
    rtl_call.out_eax = r.eax; rtl_call.out_ecx = r.ecx; rtl_call.out_edx = r.edx;
    SetLastError(error);
    return 0;
}

/* At install (nothing patched yet): the head block and the start state. */
static BOOL rtl_heap_own_prepare(void) {
    es2_heap_env env = {NULL, rtl_heap_own_read, rtl_heap_own_write, rtl_heap_own_fill, rtl_heap_own_move,
                        rtl_heap_own_alloc, rtl_heap_own_release};
    rtl_heap_own_env = env;
    uint32_t sentinel = rtl_heap_own_sentinel_at ? rtl_heap_own_sentinel_at : (uint32_t)(uintptr_t)&rtl_heap_own_head[2];
    uint32_t platform = rtl_heap_own_platform_at >= 0 ? (uint32_t)rtl_heap_own_platform_at : rtl_heap_platform_detect();
    es2_heap_init(&rtl_heap_own, &rtl_heap_own_env, sentinel, platform);
    rtl_heap_foreign_frees = rtl_heap_own_os_calls = rtl_heap_own_os_failed = 0;
    rtl_heap_own_ready = TRUE;
    return TRUE;
}

/* Exit-record fields (after the heap shadow's): the own regions, the blocks
 * of a walk of the own heap (-1 if its words do not form a valid heap), the
 * OS calls and the failed ones. */
static int rtl_heap_own_fields(char *out, size_t capacity) {
    int32_t blocks = rtl_heap_own_ready ? es2_heap_walk(&rtl_heap_own, rtl_heap_own_read, NULL, NULL, NULL) : rtl_heap_own_blocks;
    int n = snprintf(out, capacity, ",\"rtl_heap_own_regions\":%lu,\"rtl_heap_own_blocks\":%ld,\"rtl_heap_own_os_calls\":%llu"
                     ",\"rtl_heap_own_os_failed\":%llu",
                     (unsigned long)rtl_heap_own.region_count, (long)blocks, (unsigned long long)rtl_heap_own_os_calls,
                     (unsigned long long)rtl_heap_own_os_failed);
    return n > 0 && (size_t)n < capacity ? n : 0;
}
