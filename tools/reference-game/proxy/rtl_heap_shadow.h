/* Shadow mode for the CW3220 heap imports (rtl_imports.md, section
 * "the heap, shadow stage"). Included by rtl_imports.h after the adapters
 * of the pure imports.
 *
 * `calloc`, `realloc` and `free` in mode shadow (all three or none) reach the
 * CW3220 export unchanged; on the side, core/src/rtl_heap.c runs the same
 * request on its own state and predicts the result pointer and every word the
 * allocator writes. CW3220 itself also allocates (stream buffers, startup
 * argument and environment copies, operator new), bypassing DBSIM's import
 * table, so the shim also observes the export entries of _malloc, _free,
 * _realloc, _calloc and __expand inside the loaded CW3220.DLL (a jmp at each
 * entry to a detour; the displaced instructions run in a trampoline) and
 * CW3220's own import slots of VirtualAlloc and VirtualFree (a recorder).
 * Every operation, whoever calls it, is fed to the model; operations nested
 * inside one (calloc's malloc, realloc's free) are part of it.
 *
 * Per operation: entry registers saved, the return slot swapped for
 * rtl_heap_post (restored afterwards, as the export's `ret` leaves it), the
 * export runs on the game's stack with the entry registers; the recorder logs
 * each VirtualAlloc/VirtualFree with its arguments and result (pure asm, no
 * stack use beyond the swapped return slot). After the export returns, on
 * the private stack: the model replays the operation with the recorded OS
 * calls (it takes every reservation base from them and checks every
 * argument), then the result, every word the model wrote, the allocator's
 * variables and the OS calls are compared with the original. The original's
 * results always reach the caller. A mismatch is counted, the first one kept,
 * and the model re-learns the heap from memory (resync). */
#include <stdarg.h>
#include "es2/core/rtl_heap.h"

#define RTL_HEAP_CW_IMAGE 0x0002e000u
#define RTL_HEAP_CW_FILE_SIZE 176128u
#define RTL_HEAP_CW_SHA256 "6e90e1cad6d61bfaecc115286ba52b170bc05ef7f0b4cb1742a1fb28fbd3f505"
#define RTL_HEAP_LINK_BASE 0x00400000u
/* CW3220 data (RVAs): the list head block, the head pointer, rover, region
 * count and table, committed and reserved end, granularity, platform. */
#define RTL_HEAP_SENTINEL 0x23944u
#define RTL_HEAP_HEAD 0x2394cu
#define RTL_HEAP_ROVER 0x23950u
#define RTL_HEAP_COUNT 0x23938u
#define RTL_HEAP_TABLE 0x23738u
#define RTL_HEAP_COMMITTED 0x21f80u
#define RTL_HEAP_RESERVED 0x21f84u
#define RTL_HEAP_GRANULARITY 0x22098u
#define RTL_HEAP_PLATFORM 0x23a60u
/* CW3220's KERNEL32 import: lookup table, IAT, entries 84/85. */
#define RTL_HEAP_K32_LOOKUP 0x2403cu
#define RTL_HEAP_K32_IAT 0x241c0u
#define RTL_HEAP_VA_ENTRY 84u
#define RTL_HEAP_VF_ENTRY 85u
#define RTL_HEAP_WORD_BITS 20u
#define RTL_HEAP_JOURNAL 1024u
#define RTL_HEAP_LOG 65536u

enum { RTL_HEAP_MALLOC, RTL_HEAP_FREE, RTL_HEAP_REALLOC, RTL_HEAP_CALLOC, RTL_HEAP_EXPAND, RTL_HEAP_ENTRY_COUNT };
enum { RTL_HEAP_FROM_IMPORT = 1, RTL_HEAP_FROM_DETOUR = 2 };
typedef struct rtl_heap_entry { const char *symbol; uint32_t rva; BYTE length; BYTE prologue[6]; } rtl_heap_entry;
static const rtl_heap_entry rtl_heap_entries[RTL_HEAP_ENTRY_COUNT] = {
    {"_malloc", 0x1729cu, 6, {0x55, 0x8b, 0xec, 0x83, 0xc4, 0xf8}},   /* push ebp; mov ebp,esp; add esp,-8 */
    {"_free", 0x17160u, 6, {0x55, 0x8b, 0xec, 0x83, 0xc4, 0xf4}},     /* ... add esp,-0xc */
    {"_realloc", 0x17b8cu, 6, {0x55, 0x8b, 0xec, 0x83, 0xc4, 0xf0}},  /* ... add esp,-0x10 */
    {"_calloc", 0x16ffcu, 5, {0x55, 0x8b, 0xec, 0x53, 0x56}},         /* push ebp; mov ebp,esp; push ebx; push esi */
    {"__expand", 0x17a70u, 6, {0x55, 0x8b, 0xec, 0x83, 0xc4, 0xf8}},
};
static const char *const rtl_heap_names[RTL_HEAP_ENTRY_COUNT] = {"malloc", "free", "realloc", "calloc", "expand"};
/* Relocated operands of the heap code that name its variables (link-base
 * VAs): checked before anything is patched. */
typedef struct rtl_heap_operand { uint32_t rva, target; } rtl_heap_operand;
static const rtl_heap_operand rtl_heap_operands[] = {
    {0x172ceu, 0x423950u}, {0x171d8u, 0x42394cu}, {0x17216u, 0x423738u}, {0x1722bu, 0x423938u}, {0x17035u, 0x421f84u},
    {0x17041u, 0x421f80u}, {0x182beu, 0x422098u}, {0x182c6u, 0x423a60u}, {0x170fdu, 0x42394cu}, {0x17102u, 0x423944u},
};

/* State read by the asm below. */
__attribute__((used)) static volatile BYTE rtl_heap_busy;
typedef struct rtl_heap_call_state {
    uint32_t kind;                                    /*  0: function | from << 8 | import << 16 */
    uint32_t eax, ecx, edx, ebx, esi, edi, ebp, esp;  /*  4: entry registers, ESP -> return address */
    uint32_t out_eax, out_ecx, out_edx;               /* 36 */
    uint32_t target;                                  /* 48 */
    uint32_t saved_return;                            /* 52 */
    uint32_t post_esp;                                /* 56 */
} rtl_heap_call_state;
_Static_assert(offsetof(rtl_heap_call_state, esp) == 32 && offsetof(rtl_heap_call_state, out_eax) == 36 &&
               offsetof(rtl_heap_call_state, target) == 48 && offsetof(rtl_heap_call_state, saved_return) == 52 &&
               offsetof(rtl_heap_call_state, post_esp) == 56, "heap adapter offsets");
_Static_assert(sizeof(es2_heap_os_call) == 24, "recorder entry layout");
__attribute__((used)) static rtl_heap_call_state rtl_heap_call;
__attribute__((used)) static uint32_t rtl_heap_resume[RTL_HEAP_ENTRY_COUNT];
__attribute__((used)) static uint64_t rtl_heap_foreign_ops, rtl_heap_os_unobserved;
__attribute__((used)) static es2_heap_os_call *rtl_heap_log;
__attribute__((used)) static uint32_t rtl_heap_log_count, rtl_heap_log_capacity;
__attribute__((used)) static BYTE rtl_heap_log_overflow;
__attribute__((used)) static uint32_t rtl_heap_os_args[4], rtl_heap_os_kind, rtl_heap_os_ret, rtl_heap_os_esp, rtl_heap_os_ecx, rtl_heap_os_edx;
__attribute__((used)) static uint32_t rtl_heap_os_real_alloc, rtl_heap_os_real_free;

/* Detours (entry of each CW3220 function -> here), trampolines (the
 * displaced instructions, then back behind them), the import adapters, the
 * common pre/post path and the OS recorder. None of them writes below ESP;
 * the only stack write is the swapped return slot, restored afterwards. A
 * call from another thread than the owner, or one nested in an observed
 * operation, goes straight to the trampoline. */
__asm__(
    ".macro RTL_HEAP_DETOUR idx\n"
    ".text\n.p2align 4\n"
    "_rtl_heap_detour_\\idx:\n"
    "    cmpl _rtl_stack_lo, %esp\n    jb 8f\n"
    "    cmpl _rtl_stack_hi, %esp\n    jae 8f\n"
    "    cmpb $0, _rtl_heap_busy\n    jne _rtl_heap_tramp_\\idx\n"
    "    movl %eax, _rtl_heap_call+4\n"
    "    movl $(\\idx + 0x200), %eax\n"
    "    jmp _rtl_heap_common\n"
    "8:  lock addl $1, _rtl_heap_foreign_ops\n    lock adcl $0, _rtl_heap_foreign_ops+4\n"
    "    jmp _rtl_heap_tramp_\\idx\n"
    ".endm\n"
    "RTL_HEAP_DETOUR 0\nRTL_HEAP_DETOUR 1\nRTL_HEAP_DETOUR 2\nRTL_HEAP_DETOUR 3\nRTL_HEAP_DETOUR 4\n"
    ".purgem RTL_HEAP_DETOUR\n"
    ".p2align 4\n"
    "_rtl_heap_tramp_0:\n    pushl %ebp\n    movl %esp, %ebp\n    addl $-8, %esp\n    jmp *_rtl_heap_resume\n"
    "_rtl_heap_tramp_1:\n    pushl %ebp\n    movl %esp, %ebp\n    addl $-12, %esp\n    jmp *_rtl_heap_resume+4\n"
    "_rtl_heap_tramp_2:\n    pushl %ebp\n    movl %esp, %ebp\n    addl $-16, %esp\n    jmp *_rtl_heap_resume+8\n"
    "_rtl_heap_tramp_3:\n    pushl %ebp\n    movl %esp, %ebp\n    pushl %ebx\n    pushl %esi\n    jmp *_rtl_heap_resume+12\n"
    "_rtl_heap_tramp_4:\n    pushl %ebp\n    movl %esp, %ebp\n    addl $-8, %esp\n    jmp *_rtl_heap_resume+16\n"
    /* Import adapters: free (slot 14), calloc (47), realloc (49). */
    ".macro RTL_HEAP_ADAPTER import function\n"
    ".p2align 4\n"
    "_rtl_heap_shadow_\\import:\n"
    "    cmpb $0, _rtl_heap_busy\n    jne _rtl_heap_tramp_\\function\n"
    "    movl %eax, _rtl_heap_call+4\n"
    "    movl $(\\function + 0x100 + (\\import << 16)), %eax\n"
    "    jmp _rtl_heap_common\n"
    ".endm\n"
    "RTL_HEAP_ADAPTER 14 1\nRTL_HEAP_ADAPTER 47 3\nRTL_HEAP_ADAPTER 49 2\n"
    ".purgem RTL_HEAP_ADAPTER\n"
    ".p2align 4\n"
    "_rtl_heap_common:\n"
    "    movl %eax, _rtl_heap_call\n"
    "    movl %ecx, _rtl_heap_call+8\n    movl %edx, _rtl_heap_call+12\n    movl %ebx, _rtl_heap_call+16\n"
    "    movl %esi, _rtl_heap_call+20\n    movl %edi, _rtl_heap_call+24\n    movl %ebp, _rtl_heap_call+28\n    movl %esp, _rtl_heap_call+32\n"
    "    movb $1, _rtl_heap_busy\n"
    "    movl _rtl_private_top, %esp\n    cld\n"
    "    call _rtl_heap_pre_c\n"
    "    movl _rtl_heap_call+32, %esp\n"
    "    movl _rtl_heap_call+4, %eax\n    movl _rtl_heap_call+8, %ecx\n    movl _rtl_heap_call+12, %edx\n"
    "    jmp *_rtl_heap_call+48\n"
    /* The export returns here with ESP = entry ESP + 4. */
    ".p2align 4\n"
    "_rtl_heap_post:\n"
    "    movl %eax, _rtl_heap_call+36\n    movl %ecx, _rtl_heap_call+40\n    movl %edx, _rtl_heap_call+44\n"
    "    movl %esp, _rtl_heap_call+56\n"
    "    movl _rtl_private_top, %esp\n    cld\n"
    "    call _rtl_heap_post_c\n"
    "    movl _rtl_heap_call+56, %esp\n"
    "    movb $0, _rtl_heap_busy\n"
    "    movl _rtl_heap_call+36, %eax\n    movl _rtl_heap_call+40, %ecx\n    movl _rtl_heap_call+44, %edx\n"
    "    jmp *_rtl_heap_call+52\n"
    /* OS recorder in CW3220's VirtualAlloc/VirtualFree slots (stdcall). Logs
     * only inside an observed operation on the owner thread. */
    ".p2align 4\n"
    "_rtl_heap_os_alloc:\n"
    "    cmpb $0, _rtl_heap_busy\n    je 7f\n"
    "    cmpl _rtl_stack_lo, %esp\n    jb 7f\n"
    "    cmpl _rtl_stack_hi, %esp\n    jae 7f\n"
    "    movl $1, _rtl_heap_os_kind\n"
    "    movl 16(%esp), %eax\n    movl %eax, _rtl_heap_os_args+12\n"
    "    jmp 1f\n"
    "7:  lock addl $1, _rtl_heap_os_unobserved\n    lock adcl $0, _rtl_heap_os_unobserved+4\n"
    "    jmp *_rtl_heap_os_real_alloc\n"
    ".p2align 4\n"
    "_rtl_heap_os_free:\n"
    "    cmpb $0, _rtl_heap_busy\n    je 8f\n"
    "    cmpl _rtl_stack_lo, %esp\n    jb 8f\n"
    "    cmpl _rtl_stack_hi, %esp\n    jae 8f\n"
    "    movl $2, _rtl_heap_os_kind\n"
    "    movl $0, _rtl_heap_os_args+12\n"
    "1:  movl 4(%esp), %eax\n    movl %eax, _rtl_heap_os_args\n"
    "    movl 8(%esp), %eax\n    movl %eax, _rtl_heap_os_args+4\n"
    "    movl 12(%esp), %eax\n    movl %eax, _rtl_heap_os_args+8\n"
    "    movl (%esp), %eax\n    movl %eax, _rtl_heap_os_ret\n"
    "    movl %esp, _rtl_heap_os_esp\n"
    "    movl $_rtl_heap_os_post, (%esp)\n"
    "    cmpl $1, _rtl_heap_os_kind\n    jne 2f\n"
    "    jmp *_rtl_heap_os_real_alloc\n"
    "2:  jmp *_rtl_heap_os_real_free\n"
    "8:  lock addl $1, _rtl_heap_os_unobserved\n    lock adcl $0, _rtl_heap_os_unobserved+4\n"
    "    jmp *_rtl_heap_os_real_free\n"
    ".p2align 4\n"
    "_rtl_heap_os_post:\n"
    "    movl %ecx, _rtl_heap_os_ecx\n    movl %edx, _rtl_heap_os_edx\n"
    "    movl _rtl_heap_os_esp, %ecx\n    movl _rtl_heap_os_ret, %edx\n    movl %edx, (%ecx)\n"
    "    movl _rtl_heap_log_count, %ecx\n"
    "    cmpl _rtl_heap_log_capacity, %ecx\n    jae 6f\n"
    "    imull $24, %ecx, %ecx\n    addl _rtl_heap_log, %ecx\n"
    "    movl _rtl_heap_os_kind, %edx\n    movl %edx, (%ecx)\n"
    "    movl _rtl_heap_os_args, %edx\n    movl %edx, 4(%ecx)\n"
    "    movl _rtl_heap_os_args+4, %edx\n    movl %edx, 8(%ecx)\n"
    "    movl _rtl_heap_os_args+8, %edx\n    movl %edx, 12(%ecx)\n"
    "    movl _rtl_heap_os_args+12, %edx\n    movl %edx, 16(%ecx)\n"
    "    movl %eax, 20(%ecx)\n"
    "    addl $1, _rtl_heap_log_count\n"
    "    jmp 5f\n"
    "6:  movb $1, _rtl_heap_log_overflow\n"
    "5:  movl _rtl_heap_os_ecx, %ecx\n    movl _rtl_heap_os_edx, %edx\n"
    "    jmp *_rtl_heap_os_ret\n"
    ".section .rdata,\"dr\"\n.p2align 2\n"
    "_rtl_heap_detour_table:\n    .long _rtl_heap_detour_0, _rtl_heap_detour_1, _rtl_heap_detour_2, _rtl_heap_detour_3, _rtl_heap_detour_4\n"
    "_rtl_heap_tramp_table:\n    .long _rtl_heap_tramp_0, _rtl_heap_tramp_1, _rtl_heap_tramp_2, _rtl_heap_tramp_3, _rtl_heap_tramp_4\n"
    ".text\n");
extern const uint32_t rtl_heap_detour_table[RTL_HEAP_ENTRY_COUNT] __asm__("_rtl_heap_detour_table");
extern const uint32_t rtl_heap_tramp_table[RTL_HEAP_ENTRY_COUNT] __asm__("_rtl_heap_tramp_table");
extern void rtl_heap_post(void) __asm__("_rtl_heap_post");
extern void rtl_heap_shadow_14(void) __asm__("_rtl_heap_shadow_14");
extern void rtl_heap_shadow_47(void) __asm__("_rtl_heap_shadow_47");
extern void rtl_heap_shadow_49(void) __asm__("_rtl_heap_shadow_49");
extern void rtl_heap_os_alloc(void) __asm__("_rtl_heap_os_alloc");
extern void rtl_heap_os_free(void) __asm__("_rtl_heap_os_free");
static uint32_t rtl_heap_adapter(unsigned import) {
    return import == RTL_I_free ? (uint32_t)(uintptr_t)rtl_heap_shadow_14 :
           import == RTL_I_calloc ? (uint32_t)(uintptr_t)rtl_heap_shadow_47 :
           import == RTL_I_realloc ? (uint32_t)(uintptr_t)rtl_heap_shadow_49 : 0u;
}
static int rtl_heap_function_of(unsigned import) {
    return import == RTL_I_free ? RTL_HEAP_FREE : import == RTL_I_calloc ? RTL_HEAP_CALLOC : import == RTL_I_realloc ? RTL_HEAP_REALLOC : -1;
}

/* Model and bookkeeping. */
static BYTE rtl_shadowed[RTL_IMPORT_COUNT];
static BYTE *rtl_heap_cw;              /* the loaded CW3220.DLL while shadowing */
static BOOL rtl_heap_active, rtl_heap_model_ok, rtl_heap_patched_code[RTL_HEAP_ENTRY_COUNT], rtl_heap_patched_slot[2];
static uint32_t rtl_heap_slot_value[2];
static es2_heap rtl_heap_model;
static es2_heap_shadow rtl_heap_state;
static es2_heap_env rtl_heap_env;
static uint64_t rtl_heap_import_mismatches[RTL_IMPORT_COUNT];
static uint64_t rtl_heap_internal_ops, rtl_heap_external_ops, rtl_heap_detour_mismatches, rtl_heap_os_calls, rtl_heap_skipped, rtl_heap_resyncs;
static int32_t rtl_heap_attach_blocks;
static char rtl_heap_mismatch_detail[224];
typedef struct rtl_heap_op { unsigned function, from, import; uint32_t a0, a1, caller; } rtl_heap_op;
static rtl_heap_op rtl_heap_current;

static uint32_t rtl_heap_real_read(void *context, uint32_t address) { (void)context; return *(const volatile uint32_t *)(uintptr_t)address; }
static uint32_t rtl_heap_cw_word(uint32_t rva) { return rtl_heap_real_read(NULL, (uint32_t)(uintptr_t)rtl_heap_cw + rva); }
/* The original's allocator variables. */
static void rtl_heap_real_state(es2_heap *state) {
    memset(state, 0, sizeof(*state));
    uint32_t cw = (uint32_t)(uintptr_t)rtl_heap_cw;
    state->sentinel = cw + RTL_HEAP_SENTINEL;
    state->rover = rtl_heap_cw_word(RTL_HEAP_ROVER);
    state->region_count = rtl_heap_cw_word(RTL_HEAP_COUNT);
    for (uint32_t i = 0; i < ES2_HEAP_REGION_LIMIT && i < state->region_count; ++i) {
        state->region_base[i] = rtl_heap_cw_word(RTL_HEAP_TABLE + 8u * i);
        state->region_size[i] = rtl_heap_cw_word(RTL_HEAP_TABLE + 8u * i + 4u);
    }
    state->committed_end = rtl_heap_cw_word(RTL_HEAP_COMMITTED);
    state->reserved_end = rtl_heap_cw_word(RTL_HEAP_RESERVED);
    state->granularity = rtl_heap_cw_word(RTL_HEAP_GRANULARITY);
    state->platform = rtl_heap_cw_word(RTL_HEAP_PLATFORM);
}
/* (Re)learn the model from the original's memory. */
static BOOL rtl_heap_learn(void) {
    es2_heap state;
    rtl_heap_real_state(&state);
    es2_heap_words_clear(&rtl_heap_state.words);
    rtl_heap_state.journal_count = 0;
    if (state.region_count > ES2_HEAP_REGION_LIMIT) return FALSE;
    rtl_heap_attach_blocks = es2_heap_attach(&rtl_heap_model, &state, &rtl_heap_env, rtl_heap_real_read, NULL);
    return rtl_heap_attach_blocks >= 0 && !rtl_heap_state.words.overflow;
}

/* The optional trace ([rtl] heap_trace=<path>, own_process.md "the heap
 * trace"). After the attach: the allocator's variables, every block of
 * the heap as DBSIM's entry finds it and the sizes of the environment
 * strings (CW3220's copy 0x41a050 allocates one block per string); then one
 * line per observed operation: DBSIM's frame counter, the function, its
 * origin, caller, arguments, result and the reservations it made. Each
 * operation's line is written when it is complete: a suite that reaches its
 * frame limit is ended by the runner with TerminateProcess (no exit record),
 * which a buffered tail would not survive (the first live run lost its last
 * 64 KiB). The end line comes with the exit record (rtl_heap_fields);
 * later operations are counted as lost. Without the key nothing is opened
 * or written. */
#define RTL_HEAP_TRACE_BUFFER 65536u
#define RTL_HEAP_FRAME_VA 0x004d257bu   /* g_sim_ctx + 0xbf (tick_log.h) */
static HANDLE rtl_heap_trace_file = INVALID_HANDLE_VALUE;
static char *rtl_heap_trace_buffer;
static uint32_t rtl_heap_trace_used;
static uint64_t rtl_heap_trace_ops, rtl_heap_trace_lost;
static BOOL rtl_heap_trace_on;
static BOOL rtl_heap_trace_flush(void) {
    if (rtl_heap_trace_file == INVALID_HANDLE_VALUE) return FALSE;
    DWORD written = 0;
    BOOL ok = !rtl_heap_trace_used ||
              (WriteFile(rtl_heap_trace_file, rtl_heap_trace_buffer, rtl_heap_trace_used, &written, NULL) && written == rtl_heap_trace_used);
    rtl_heap_trace_used = 0;
    if (!ok) { CloseHandle(rtl_heap_trace_file); rtl_heap_trace_file = INVALID_HANDLE_VALUE; }
    return ok;
}
/* Appends one formatted line; FALSE when it was lost. */
static BOOL rtl_heap_trace_line(const char *format, ...) __attribute__((format(printf, 1, 2)));
static BOOL rtl_heap_trace_line(const char *format, ...) {
    if (rtl_heap_trace_file == INVALID_HANDLE_VALUE) return FALSE;
    if (RTL_HEAP_TRACE_BUFFER - rtl_heap_trace_used < 1024u && !rtl_heap_trace_flush()) return FALSE;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(rtl_heap_trace_buffer + rtl_heap_trace_used, RTL_HEAP_TRACE_BUFFER - rtl_heap_trace_used, format, args);
    va_end(args);
    if (n <= 0 || (uint32_t)n >= RTL_HEAP_TRACE_BUFFER - rtl_heap_trace_used) return FALSE;
    rtl_heap_trace_used += (uint32_t)n;
    return TRUE;
}
/* Opens the trace and writes the attach part (after rtl_heap_prepare). */
static BOOL rtl_heap_trace_open(const WCHAR *path) {
    if (!rtl_heap_trace_buffer) {
        rtl_heap_trace_buffer = VirtualAlloc(NULL, RTL_HEAP_TRACE_BUFFER, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!rtl_heap_trace_buffer) return rtl_refuse("heap_trace_memory", NULL);
    }
    rtl_heap_trace_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (rtl_heap_trace_file == INVALID_HANDLE_VALUE) return rtl_refuse("heap_trace_file", NULL);
    rtl_heap_trace_used = 0;
    rtl_heap_trace_ops = rtl_heap_trace_lost = 0;
    rtl_heap_trace_on = TRUE;
    es2_heap real;
    rtl_heap_real_state(&real);
    BOOL ok = rtl_heap_trace_line("{\"kind\":\"attach\",\"schema\":1,\"frame\":%lu,\"sentinel\":\"%08x\",\"rover\":\"%08x\",\"committed\":\"%08x\""
                                  ",\"reserved\":\"%08x\",\"granularity\":%lu,\"platform\":%lu,\"blocks\":%ld,\"regions\":[",
                                  (unsigned long)*(const volatile uint32_t *)(uintptr_t)RTL_HEAP_FRAME_VA, (unsigned)real.sentinel,
                                  (unsigned)real.rover, (unsigned)real.committed_end, (unsigned)real.reserved_end,
                                  (unsigned long)real.granularity, (unsigned long)real.platform, (long)rtl_heap_attach_blocks);
    for (uint32_t i = 0; ok && i < real.region_count && i < ES2_HEAP_REGION_LIMIT; ++i)
        ok = rtl_heap_trace_line("%s[\"%08x\",%lu]", i ? "," : "", (unsigned)real.region_base[i], (unsigned long)real.region_size[i]);
    /* The environment block CW3220's attach copied: the length of each
     * string (no content). */
    char *environment = GetEnvironmentStringsA();
    ok = ok && rtl_heap_trace_line("],\"env_lengths\":[");
    unsigned strings = 0;
    for (const char *s = environment; ok && s && *s; s += strlen(s) + 1u, ++strings)
        ok = rtl_heap_trace_line("%s%lu", strings ? "," : "", (unsigned long)strlen(s));
    if (environment) FreeEnvironmentStringsA(environment);
    ok = ok && rtl_heap_trace_line("],\"command_line\":%lu}\n", (unsigned long)strlen(GetCommandLineA()));
    /* Every block in address order (the walk of rtl_heap_learn succeeded). */
    for (uint32_t i = 0; ok && i < real.region_count && i < ES2_HEAP_REGION_LIMIT; ++i) {
        uint32_t end = real.region_base[i] + real.region_size[i];
        for (uint32_t p = real.region_base[i] + 8u; ok && p < end;) {
            uint32_t header = rtl_heap_real_read(NULL, p - 4u);
            if ((header & ~1u) < 16u) break;
            ok = rtl_heap_trace_line("{\"kind\":\"block\",\"p\":\"%08x\",\"size\":%lu,\"used\":%u}\n", (unsigned)p,
                                     (unsigned long)(header & ~1u), (unsigned)(header & 1u));
            p += header & ~1u;
        }
    }
    if (!ok || !rtl_heap_trace_flush()) {
        if (rtl_heap_trace_file != INVALID_HANDLE_VALUE) CloseHandle(rtl_heap_trace_file);
        rtl_heap_trace_file = INVALID_HANDLE_VALUE;
        rtl_heap_trace_on = FALSE;
        return rtl_refuse("heap_trace_file", NULL);
    }
    return TRUE;
}
/* One operation (rtl_heap_post_c, after the OS calls were recorded). */
static void rtl_heap_trace_op(const rtl_heap_op *op) {
    static const char origin[3][9] = {"import", "internal", "external"};
    uint32_t cw = (uint32_t)(uintptr_t)rtl_heap_cw;
    unsigned from = op->from == RTL_HEAP_FROM_IMPORT ? 0u : op->caller - cw < RTL_HEAP_CW_IMAGE ? 1u : 2u;
    uint64_t index = rtl_heap_trace_ops++;
    BOOL ok = rtl_heap_trace_line("{\"op\":%llu,\"frame\":%lu,\"fn\":\"%s\",\"from\":\"%s\",\"caller\":\"%08x\",\"a0\":\"%08x\",\"a1\":\"%08x\""
                                  ",\"result\":\"%08x\",\"rover\":\"%08x\"",
                                  (unsigned long long)index, (unsigned long)*(const volatile uint32_t *)(uintptr_t)RTL_HEAP_FRAME_VA,
                                  rtl_heap_names[op->function], origin[from], (unsigned)op->caller, (unsigned)op->a0, (unsigned)op->a1,
                                  (unsigned)(op->function != RTL_HEAP_FREE ? rtl_heap_call.out_eax : 0u),
                                  (unsigned)rtl_heap_cw_word(RTL_HEAP_ROVER));
    /* Reservations and releases (commits are per page and left out). */
    uint32_t count = rtl_heap_log_count < rtl_heap_log_capacity ? rtl_heap_log_count : rtl_heap_log_capacity;
    for (uint32_t k = 0; ok && k < count; ++k) {
        const es2_heap_os_call *c = &rtl_heap_log[k];
        if (c->kind == ES2_HEAP_OS_ALLOC && (c->type & 0x2000u))
            ok = rtl_heap_trace_line(",\"reserve\":[\"%08x\",%lu]", (unsigned)c->result, (unsigned long)c->size);
        else if (c->kind == ES2_HEAP_OS_FREE && (c->type & 0x8000u))
            ok = rtl_heap_trace_line(",\"release\":\"%08x\"", (unsigned)c->address);
    }
    if (!ok || !rtl_heap_trace_line("}\n") || !rtl_heap_trace_flush()) ++rtl_heap_trace_lost;
}

void __cdecl rtl_heap_pre_c(void) __asm__("_rtl_heap_pre_c") __attribute__((used));
void __cdecl rtl_heap_pre_c(void) {
    uint32_t e = rtl_heap_call.esp;
    const volatile uint32_t *stack = (const volatile uint32_t *)(uintptr_t)e;
    rtl_heap_op *op = &rtl_heap_current;
    op->function = rtl_heap_call.kind & 0xffu;
    op->from = (rtl_heap_call.kind >> 8) & 0xffu;
    op->import = rtl_heap_call.kind >> 16;
    op->caller = stack[0];
    op->a0 = stack[1];
    op->a1 = op->function == RTL_HEAP_FREE ? 0u : stack[2];
    rtl_heap_call.saved_return = stack[0];
    *(volatile uint32_t *)(uintptr_t)e = (uint32_t)(uintptr_t)rtl_heap_post;
    rtl_heap_call.target = rtl_heap_tramp_table[op->function];
    rtl_heap_log_count = 0;
    rtl_heap_log_overflow = 0;
}

/* Is `address` a word of the original's heap (a region, or the head block)? */
static BOOL rtl_heap_readable(uint32_t address, const es2_heap *real) {
    if (address - (real->sentinel - 8u) < 16u) return TRUE;
    for (uint32_t i = 0; i < real->region_count && i < ES2_HEAP_REGION_LIMIT; ++i)
        if (address - real->region_base[i] <= real->region_size[i] - 4u && real->region_size[i] >= 4u) return TRUE;
    return FALSE;
}
static BOOL rtl_heap_returns_pointer(unsigned function) { return function != RTL_HEAP_FREE; }

/* Compare the model's prediction with the original after one operation.
 * Returns the kind of the first difference, or NULL. */
static const char *rtl_heap_compare(uint32_t predicted, uint32_t *where, uint32_t *model_value, uint32_t *real_value) {
    const rtl_heap_op *op = &rtl_heap_current;
    *where = 0; *model_value = predicted; *real_value = rtl_heap_call.out_eax;
    if (rtl_heap_returns_pointer(op->function) && predicted != rtl_heap_call.out_eax) return "result";
    if (rtl_heap_log_overflow) return "os_log";
    if (es2_heap_shadow_finish(&rtl_heap_state)) {
        uint32_t k = rtl_heap_state.os_mismatch_index;
        *where = k;
        *real_value = k < rtl_heap_log_count ? rtl_heap_log[k].size : 0u;
        return "os";
    }
    if (rtl_heap_state.words.overflow || rtl_heap_state.journal_overflow) return "store";
    if (rtl_heap_state.words.missing) return "unwritten";
    es2_heap real;
    rtl_heap_real_state(&real);
    /* Words written in this operation that now lie in the user data of the
     * returned block (an end marker or header overtaken by the block) were
     * overwritten by calloc's zero fill or realloc's copy in the original;
     * the model keeps no data, so they are not compared. */
    uint32_t data = 0, data_size = 0, header;
    if (predicted && rtl_heap_returns_pointer(op->function) && es2_heap_words_find(&rtl_heap_state.words, predicted - 4u, &header) &&
        (header & ~1u) >= 16u) {
        data = predicted;
        data_size = (header & ~1u) - 8u;
    }
    for (uint32_t k = 0; k < rtl_heap_state.journal_count; ++k) {
        uint32_t a = rtl_heap_state.journal[k], mine;
        if (a - data < data_size) continue;
        if (!es2_heap_words_find(&rtl_heap_state.words, a, &mine)) mine = 0;
        *where = a; *model_value = mine;
        if (!rtl_heap_readable(a, &real)) { *real_value = 0; return "address"; }
        *real_value = rtl_heap_real_read(NULL, a);
        if (*real_value != mine) return "word";
    }
    const es2_heap *m = &rtl_heap_model;
    *where = 0;
#define RTL_HEAP_STATE(field, name) if (m->field != real.field) { *model_value = m->field; *real_value = real.field; return name; }
    RTL_HEAP_STATE(rover, "rover")
    RTL_HEAP_STATE(region_count, "regions")
    RTL_HEAP_STATE(committed_end, "committed")
    RTL_HEAP_STATE(reserved_end, "reserved")
    RTL_HEAP_STATE(granularity, "granularity")
#undef RTL_HEAP_STATE
    for (uint32_t i = 0; i < m->region_count && i < ES2_HEAP_REGION_LIMIT; ++i)
        if (m->region_base[i] != real.region_base[i] || m->region_size[i] != real.region_size[i]) {
            *where = i; *model_value = m->region_size[i]; *real_value = real.region_size[i];
            return "region";
        }
    return NULL;
}

void __cdecl rtl_heap_post_c(void) __asm__("_rtl_heap_post_c") __attribute__((used));
void __cdecl rtl_heap_post_c(void) {
    DWORD error = GetLastError();
    *(volatile uint32_t *)(uintptr_t)rtl_heap_call.esp = rtl_heap_call.saved_return;   /* as the export's `ret` leaves it */
    const rtl_heap_op *op = &rtl_heap_current;
    rtl_heap_os_calls += rtl_heap_log_count;
    if (op->from == RTL_HEAP_FROM_DETOUR) {
        uint32_t cw = (uint32_t)(uintptr_t)rtl_heap_cw;
        if (op->caller - cw < RTL_HEAP_CW_IMAGE) ++rtl_heap_internal_ops; else ++rtl_heap_external_ops;
    }
    if (rtl_heap_trace_on) rtl_heap_trace_op(op);
    if (!rtl_heap_model_ok) { ++rtl_heap_skipped; SetLastError(error); return; }
    es2_heap_shadow_begin(&rtl_heap_state, rtl_heap_log, rtl_heap_log_count < rtl_heap_log_capacity ? rtl_heap_log_count : rtl_heap_log_capacity);
    uint32_t predicted = 0;
    switch (op->function) {
    case RTL_HEAP_MALLOC: predicted = es2_heap_malloc(&rtl_heap_model, &rtl_heap_env, op->a0); break;
    case RTL_HEAP_FREE: es2_heap_free(&rtl_heap_model, &rtl_heap_env, op->a0); break;
    case RTL_HEAP_REALLOC: predicted = es2_heap_realloc(&rtl_heap_model, &rtl_heap_env, op->a0, op->a1); break;
    case RTL_HEAP_CALLOC: predicted = es2_heap_calloc(&rtl_heap_model, &rtl_heap_env, op->a0, op->a1); break;
    default: predicted = es2_heap_expand(&rtl_heap_model, &rtl_heap_env, op->a0, op->a1); break;
    }
    uint32_t where, mine, real;
    const char *what = rtl_heap_compare(predicted, &where, &mine, &real);
    if (what) {
        if (op->from == RTL_HEAP_FROM_IMPORT) ++rtl_heap_import_mismatches[op->import];
        else ++rtl_heap_detour_mismatches;
        if (!rtl_heap_mismatch_detail[0])
            snprintf(rtl_heap_mismatch_detail, sizeof(rtl_heap_mismatch_detail),
                     "%s:%s:%s@%08x:args=%08x,%08x:caller=%08x:model=%08x:orig=%08x:result=%08x",
                     rtl_heap_names[op->function], op->from == RTL_HEAP_FROM_IMPORT ? "import" : "detour", what, (unsigned)where,
                     (unsigned)op->a0, (unsigned)op->a1, (unsigned)op->caller, (unsigned)mine, (unsigned)real,
                     (unsigned)rtl_heap_call.out_eax);
        ++rtl_heap_resyncs;
        rtl_heap_model_ok = rtl_heap_learn();
    }
    SetLastError(error);
}

/* ------------------------------------------------------------------------
 * Install / uninstall. */
static BOOL rtl_heap_code_write(BYTE *at, const BYTE *bytes, unsigned length) {
    DWORD old, ignored;
    if (!rtl_protect(at, length, PAGE_EXECUTE_READWRITE, &old)) return FALSE;
    memcpy(at, bytes, length);
    BOOL ok = rtl_protect(at, length, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), at, length);
    return ok;
}
static void rtl_heap_unpatch(void) {
    BYTE *cw = rtl_heap_cw;
    if (!cw) return;
    for (unsigned i = 0; i < RTL_HEAP_ENTRY_COUNT; ++i) {
        if (!rtl_heap_patched_code[i]) continue;
        if (rtl_heap_code_write(cw + rtl_heap_entries[i].rva, rtl_heap_entries[i].prologue, rtl_heap_entries[i].length)) rtl_heap_patched_code[i] = FALSE;
    }
    static const uint32_t slots[2] = {RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VA_ENTRY, RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VF_ENTRY};
    static void (*const ours[2])(void) = {rtl_heap_os_alloc, rtl_heap_os_free};
    for (unsigned i = 0; i < 2; ++i)
        if (rtl_heap_patched_slot[i] && rtl_swap((DWORD *)(cw + slots[i]), (uint32_t)(uintptr_t)ours[i], rtl_heap_slot_value[i])) rtl_heap_patched_slot[i] = FALSE;
}
static BOOL rtl_heap_unpatched(void) {
    for (unsigned i = 0; i < RTL_HEAP_ENTRY_COUNT; ++i) if (rtl_heap_patched_code[i]) return FALSE;
    return !rtl_heap_patched_slot[0] && !rtl_heap_patched_slot[1];
}

/* Verification and preparation (nothing patched): the CW3220 image, its
 * heap exports, prologues and variable operands, its KERNEL32 import of
 * VirtualAlloc/VirtualFree, an initialized heap; memory for the model; the
 * model learns the heap as it is now. */
static BOOL rtl_heap_prepare(BYTE *cw, rtl_resolver resolve, void *context) {
    if (!cw) return rtl_refuse("cw_image", NULL);
    uint32_t pe = rtl_u32(cw + 0x3c);
    if (cw[0] != 'M' || cw[1] != 'Z' || pe < 64 || pe > 1024 || memcmp(cw + pe, "PE\0\0", 4) ||
        rtl_u32(cw + pe + 24 + 56) != RTL_HEAP_CW_IMAGE) return rtl_refuse("cw_image", NULL);
    uint32_t base = (uint32_t)(uintptr_t)cw;
    for (unsigned i = 0; i < RTL_HEAP_ENTRY_COUNT; ++i) {
        const rtl_heap_entry *e = &rtl_heap_entries[i];
        FARPROC address = resolve ? resolve(e->symbol, context) : NULL;
        if ((uint32_t)(uintptr_t)address != base + e->rva) return rtl_refuse("cw_export", rtl_heap_names[i]);
        if (memcmp(cw + e->rva, e->prologue, e->length)) return rtl_refuse("cw_prologue", rtl_heap_names[i]);
    }
    for (unsigned i = 0; i < sizeof(rtl_heap_operands) / sizeof(rtl_heap_operands[0]); ++i)
        if (rtl_u32(cw + rtl_heap_operands[i].rva) != base + rtl_heap_operands[i].target - RTL_HEAP_LINK_BASE) return rtl_refuse("cw_operand", NULL);
    static const char *const os_names[2] = {"VirtualAlloc", "VirtualFree"};
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t entry = rtl_u32(cw + RTL_HEAP_K32_LOOKUP + 4u * (RTL_HEAP_VA_ENTRY + i));
        if (!entry || entry > RTL_HEAP_CW_IMAGE - 16u || strcmp((const char *)cw + entry + 2, os_names[i])) return rtl_refuse("cw_imports", NULL);
        rtl_heap_slot_value[i] = rtl_u32(cw + RTL_HEAP_K32_IAT + 4u * (RTL_HEAP_VA_ENTRY + i));
        if (!rtl_heap_slot_value[i]) return rtl_refuse("cw_imports", NULL);
    }
    if (rtl_u32(cw + RTL_HEAP_HEAD) != base + RTL_HEAP_SENTINEL) return rtl_refuse("heap_uninitialized", NULL);
    if (!rtl_heap_log) {
        size_t words = (size_t)1u << RTL_HEAP_WORD_BITS;
        size_t bytes = words * 8u + (size_t)RTL_HEAP_JOURNAL * 4u + (size_t)RTL_HEAP_LOG * sizeof(es2_heap_os_call);
        BYTE *block = VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!block) return rtl_refuse("shadow_memory", NULL);
        es2_heap_words_init(&rtl_heap_state.words, (uint32_t *)block, (uint32_t *)block + words, RTL_HEAP_WORD_BITS);
        rtl_heap_state.journal = (uint32_t *)block + 2u * words;
        rtl_heap_state.journal_capacity = RTL_HEAP_JOURNAL;
        rtl_heap_log = (es2_heap_os_call *)(rtl_heap_state.journal + RTL_HEAP_JOURNAL);
        rtl_heap_log_capacity = RTL_HEAP_LOG;
    }
    es2_heap_shadow_env(&rtl_heap_env, &rtl_heap_state);
    rtl_heap_cw = cw;
    if (!rtl_heap_learn()) { rtl_heap_cw = NULL; return rtl_refuse("heap_attach", NULL); }
    rtl_heap_os_real_alloc = rtl_heap_slot_value[0];
    rtl_heap_os_real_free = rtl_heap_slot_value[1];
    for (unsigned i = 0; i < RTL_HEAP_ENTRY_COUNT; ++i) rtl_heap_resume[i] = base + rtl_heap_entries[i].rva + rtl_heap_entries[i].length;
    memset(rtl_heap_import_mismatches, 0, sizeof(rtl_heap_import_mismatches));
    rtl_heap_internal_ops = rtl_heap_external_ops = rtl_heap_detour_mismatches = rtl_heap_os_calls = rtl_heap_skipped = rtl_heap_resyncs = 0;
    rtl_heap_foreign_ops = rtl_heap_os_unobserved = 0;
    rtl_heap_mismatch_detail[0] = 0;
    rtl_heap_busy = 0;
    rtl_heap_model_ok = TRUE;
    return TRUE;
}
/* The recorder into CW3220's two slots, then a jmp at each entry. */
static BOOL rtl_heap_patch(void) {
    BYTE *cw = rtl_heap_cw;
    static const uint32_t slots[2] = {RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VA_ENTRY, RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VF_ENTRY};
    static void (*const ours[2])(void) = {rtl_heap_os_alloc, rtl_heap_os_free};
    for (unsigned i = 0; i < 2; ++i) {
        if (!rtl_swap((DWORD *)(cw + slots[i]), rtl_heap_slot_value[i], (uint32_t)(uintptr_t)ours[i])) return rtl_refuse("cw_patch", NULL);
        rtl_heap_patched_slot[i] = TRUE;
    }
    for (unsigned i = 0; i < RTL_HEAP_ENTRY_COUNT; ++i) {
        const rtl_heap_entry *e = &rtl_heap_entries[i];
        BYTE jump[6] = {0xe9, 0, 0, 0, 0, 0x90};
        uint32_t rel = rtl_heap_detour_table[i] - ((uint32_t)(uintptr_t)cw + e->rva + 5u);
        memcpy(jump + 1, &rel, 4);
        if (!rtl_heap_code_write(cw + e->rva, jump, e->length)) return rtl_refuse("cw_patch", rtl_heap_names[i]);
        rtl_heap_patched_code[i] = TRUE;
    }
    rtl_heap_active = TRUE;
    return TRUE;
}

/* All shadow mismatches (the three imports and the detours); saturates. */
static uint64_t rtl_heap_mismatch_total(void) {
    uint64_t total = rtl_heap_detour_mismatches;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        uint64_t add = rtl_heap_import_mismatches[i];
        total = total > UINT64_MAX - add ? UINT64_MAX : total + add;
    }
    return total;
}
/* Exit-record fields of the shadow (after rtl_first_mismatch). */
static int rtl_heap_fields(char *out, size_t capacity) {
    int n = snprintf(out, capacity,
                     ",\"rtl_heap_internal_ops\":%llu,\"rtl_heap_external_ops\":%llu,\"rtl_heap_detour_mismatches\":%llu"
                     ",\"rtl_heap_foreign_ops\":%llu,\"rtl_heap_os_calls\":%llu,\"rtl_heap_os_unobserved\":%llu"
                     ",\"rtl_heap_skipped\":%llu,\"rtl_heap_resyncs\":%llu,\"rtl_heap_attach_blocks\":%ld,\"rtl_heap_regions\":%lu",
                     (unsigned long long)rtl_heap_internal_ops, (unsigned long long)rtl_heap_external_ops,
                     (unsigned long long)rtl_heap_detour_mismatches, (unsigned long long)rtl_heap_foreign_ops,
                     (unsigned long long)rtl_heap_os_calls, (unsigned long long)rtl_heap_os_unobserved,
                     (unsigned long long)rtl_heap_skipped, (unsigned long long)rtl_heap_resyncs, (long)rtl_heap_attach_blocks,
                     (unsigned long)rtl_heap_model.region_count);
    if (n <= 0 || (size_t)n >= capacity) return 0;
    if (rtl_heap_trace_on) {
        /* The trace ends with the exit record. */
        BOOL ok = rtl_heap_trace_line("{\"kind\":\"end\",\"ops\":%llu,\"lost\":%llu}\n", (unsigned long long)rtl_heap_trace_ops,
                                      (unsigned long long)rtl_heap_trace_lost) && rtl_heap_trace_flush();
        if (rtl_heap_trace_file != INVALID_HANDLE_VALUE) CloseHandle(rtl_heap_trace_file);
        rtl_heap_trace_file = INVALID_HANDLE_VALUE;
        rtl_heap_trace_on = FALSE;
        int added = snprintf(out + n, capacity - (size_t)n, ",\"rtl_heap_trace_ops\":%llu,\"rtl_heap_trace_lost\":%llu,\"rtl_heap_trace_ok\":%d",
                             (unsigned long long)rtl_heap_trace_ops, (unsigned long long)rtl_heap_trace_lost, ok ? 1 : 0);
        if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
        n += added;
    }
    if (rtl_heap_mismatch_detail[0]) {
        int added = snprintf(out + n, capacity - (size_t)n, ",\"rtl_heap_first_mismatch\":\"%s\"", rtl_heap_mismatch_detail);
        if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
        n += added;
    }
    return n;
}
