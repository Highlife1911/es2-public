/* Modes `shadow` and `replaced` for the file and console imports
 * (rtl_imports.md, section "file and console imports"). Included by
 * rtl_imports.h after the
 * heap shadow.
 *
 * Own code: core/src/rtl_io.c. It works on CW3220's own I/O tables (handle
 * table, FILE array, errno, the find list, exit hooks) and allocates from
 * CW3220's heap, so every import of the set may be in any mode, mixed with
 * the others and with CW3220's internal callers (fopen's open, exit's fclose).
 *
 * replaced: the adapter saves the entry registers, runs the own function on
 * a private stack of its own (not the one of rtl_imports.h: the heap shadow's
 * detour switches to that one while the own code waits for CW3220's
 * malloc) and returns its EAX (ECX = EDX = 0: the original leaves kernel32
 * residue there, which no DBSIM caller reads). Win32 calls and CW3220's
 * _malloc/_free run on the game's stack below the caller's ESP through
 * rtl_io_guest_call, with the function pointers CW3220's own import slots
 * held at install. A call the own code declines (ES2_IO_UNSUPPORTED) goes to
 * the export with the entry registers (rtl_<key>_skipped).
 * shadow (not for the console stubs): the tracked state is copied, the return
 * slot swapped for rtl_io_post, and the export runs on the game's stack with
 * the entry registers while a recorder in CW3220's KERNEL32 slots logs each
 * Win32 call it makes (arguments, result, the bytes it reads, writes or
 * returns). Afterwards, on the private stack, the model replays the call on
 * the state before it with those results (core es2_io_replay) and every
 * difference is counted. The caller always gets the export's results. */
#include "es2/core/rtl_io.h"

#define RTL_IO_STACK 0x40000u
#define RTL_IO_OVERLAY_BITS 16u
#define RTL_IO_SNAPSHOT 0x30000u
#define RTL_IO_LOG 4096u
#define RTL_IO_POOL 0x200000u
#define RTL_IO_COPY_LIMIT 0x10000u

/* The reference DLL's export RVAs of the set and of its allocator. */
static const uint32_t rtl_io_export_rva[ES2_IO_FUNCTION_COUNT] = {
    0x5354u, 0x5304u, 0x535cu, 0x537cu, 0x534cu, 0x8b2cu, 0x5a44u, 0x555cu, 0x5cecu, 0x5ed4u, 0x5dd4u,
    0x5e38u, 0x6610u, 0x5d54u, 0x532cu, 0x57d0u, 0x57f4u, 0x1eb64u, 0x655cu, 0x7da0u, 0x5b7cu,
};
#define RTL_IO_MALLOC_RVA 0x1729cu
#define RTL_IO_FREE_RVA 0x17160u
/* Relocated operands of the I/O code naming the variables of es2_io_layout
 * (RVA of the operand, RVA it must name): checked before anything is patched. */
static const uint32_t rtl_io_operands[][2] = {
    {0x9a7du, 0x1fce0u}, {0x9a91u, 0x234e8u}, {0x9aa5u, 0x1fce4u}, {0x5a19u, 0x1f830u}, {0x9193u, 0x22248u},
    {0x916fu, 0x235d0u}, {0x9d94u, 0x1fdb0u}, {0x5958u, 0x222c0u}, {0x595cu, 0x7c7cu}, {0x53c2u, 0x222bcu},
    {0x53c6u, 0x7ca4u}, {0x61d9u, 0x235b0u}, {0x86f6u, 0x235ccu}, {0x66eau, 0x20244u}, {0x1e643u, 0x23070u},
    {0x1e649u, 0x23074u}, {0x9e21u, 0x20354u}, {0x9176u, 0x20358u}, {0x9182u, 0x1fe8cu}, {0x6568u, 0x1f848u},
};
/* The KERNEL32 functions CW3220 calls for the set, in es2_io_os order, with
 * their number of stack arguments. */
static const char *const rtl_io_os_names[ES2_IO_OS_FUNCTION_COUNT] = {
    "CreateFileA", "CloseHandle", "ReadFile", "WriteFile", "SetFilePointer", "GetFileSize", "GetFileType",
    "GetFileAttributesA", "GetLastError", "FindFirstFileA", "FindNextFileA", "FindClose", "FileTimeToLocalFileTime",
    "FileTimeToDosDateTime", "GetLocalTime",
};
static const BYTE rtl_io_os_args[ES2_IO_OS_FUNCTION_COUNT] = {7, 1, 5, 5, 4, 2, 1, 1, 0, 2, 2, 1, 2, 3, 1};

/* State read by the asm below. */
typedef struct rtl_io_call_state {
    uint32_t index;                                   /*  0 */
    uint32_t eax, ecx, edx, ebx, esi, edi, ebp, esp;  /*  4: entry registers, ESP -> return address */
    uint32_t out_eax, out_ecx, out_edx;               /* 36 */
    uint32_t target;                                  /* 48 */
    uint32_t saved_return;                            /* 52 */
    uint32_t post_esp;                                /* 56 */
} rtl_io_call_state;
_Static_assert(offsetof(rtl_io_call_state, esp) == 32 && offsetof(rtl_io_call_state, out_eax) == 36 &&
               offsetof(rtl_io_call_state, target) == 48 && offsetof(rtl_io_call_state, saved_return) == 52 &&
               offsetof(rtl_io_call_state, post_esp) == 56, "io adapter offsets");
__attribute__((used)) static rtl_io_call_state rtl_io_call;
__attribute__((used)) static volatile BYTE rtl_io_busy, rtl_io_recording;
__attribute__((used)) static uint32_t rtl_io_stack_top, rtl_io_guest_saved, rtl_io_guest_target;
__attribute__((used)) static uint32_t rtl_io_real[ES2_IO_OS_FUNCTION_COUNT];
__attribute__((used)) static uint32_t rtl_io_rec_kind, rtl_io_rec_args[7], rtl_io_rec_ret, rtl_io_rec_esp, rtl_io_rec_post_esp;
__attribute__((used)) static uint32_t rtl_io_rec_eax, rtl_io_rec_ecx, rtl_io_rec_edx;
__attribute__((used)) static uint64_t rtl_io_os_unobserved;

/* Per import: the es2_io function (-1: not in the set) and the mode. */
static signed char rtl_io_of[RTL_IMPORT_COUNT];
static BYTE rtl_io_shadowed[RTL_IMPORT_COUNT], rtl_io_replaced[RTL_IMPORT_COUNT];
static uint64_t rtl_io_mismatches[RTL_IMPORT_COUNT], rtl_io_shadow_skipped[RTL_IMPORT_COUNT];
static uint64_t rtl_io_os_calls;
static const char *rtl_io_last_skip = "";   /* why the last shadow call was not judged (diagnostics) */
static char rtl_io_mismatch_detail[192];
static BYTE *rtl_io_cw;
static BOOL rtl_io_active, rtl_io_recorder_patched[ES2_IO_OS_FUNCTION_COUNT];
static uint32_t rtl_io_slot_rva[ES2_IO_OS_FUNCTION_COUNT];
static uint32_t rtl_io_malloc_address, rtl_io_free_address;
static es2_io_layout rtl_io_layout;
static es2_io_replay rtl_io_replay;
static es2_io_record *rtl_io_log;
static uint8_t *rtl_io_pool;
static uint32_t rtl_io_log_count, rtl_io_pool_used;
static BOOL rtl_io_log_overflow;

static int rtl_io_function_of(unsigned import) {
    for (int f = 0; f < ES2_IO_FUNCTION_COUNT; ++f)
        if (!strcmp(rtl_imports[import].symbol, es2_io_name((es2_io_function)f))) return f;
    return -1;
}

__asm__(
    /* Adapters: the thunk's target for an import of the set. */
    ".macro RTL_IO_ADAPTER idx\n"
    ".text\n.p2align 4\n"
    "_rtl_io_adapter_\\idx:\n"
    "    cmpb $0, _rtl_io_busy\n    jne 1f\n"
    "    movl %eax, _rtl_io_call+4\n    movl $\\idx, %eax\n    jmp _rtl_io_common\n"
    "1:  addl $1, _rtl_skipped+8*\\idx\n    adcl $0, _rtl_skipped+8*\\idx+4\n"
    "    jmp *_rtl_original+4*\\idx\n"
    ".endm\n"
#define RTL_T(i) "RTL_IO_ADAPTER " #i "\n"
    RTL_T(0) RTL_T(1) RTL_T(2) RTL_T(3) RTL_T(4) RTL_T(5) RTL_T(6) RTL_T(7) RTL_T(8) RTL_T(9)
    RTL_T(10) RTL_T(11) RTL_T(12) RTL_T(13) RTL_T(14) RTL_T(15) RTL_T(16) RTL_T(17) RTL_T(18) RTL_T(19)
    RTL_T(20) RTL_T(21) RTL_T(22) RTL_T(23) RTL_T(24) RTL_T(25) RTL_T(26) RTL_T(27) RTL_T(28) RTL_T(29)
    RTL_T(30) RTL_T(31) RTL_T(32) RTL_T(33) RTL_T(34) RTL_T(35) RTL_T(36) RTL_T(37) RTL_T(38) RTL_T(39)
    RTL_T(40) RTL_T(41) RTL_T(42) RTL_T(43) RTL_T(44) RTL_T(45) RTL_T(46) RTL_T(47) RTL_T(48) RTL_T(49)
    RTL_T(50) RTL_T(51) RTL_T(52)
#undef RTL_T
    ".purgem RTL_IO_ADAPTER\n"
    /* Entry registers saved, the C part on the private stack; it returns
     * 0 (replaced: return its results), 1 (run the export as called) or
     * 2 (shadow: the return slot points to rtl_io_post, run the export). */
    "_rtl_io_common:\n"
    "    movl %eax, _rtl_io_call\n"
    "    movl %ecx, _rtl_io_call+8\n    movl %edx, _rtl_io_call+12\n    movl %ebx, _rtl_io_call+16\n"
    "    movl %esi, _rtl_io_call+20\n    movl %edi, _rtl_io_call+24\n    movl %ebp, _rtl_io_call+28\n    movl %esp, _rtl_io_call+32\n"
    "    movb $1, _rtl_io_busy\n"
    "    movl _rtl_io_stack_top, %esp\n    cld\n"
    "    call _rtl_io_pre_c\n"
    "    movl _rtl_io_call+32, %esp\n"
    "    cmpl $0, %eax\n    jne 3f\n"
    "    movb $0, _rtl_io_busy\n"
    "    movl _rtl_io_call+16, %ebx\n    movl _rtl_io_call+20, %esi\n    movl _rtl_io_call+24, %edi\n    movl _rtl_io_call+28, %ebp\n"
    "    movl _rtl_io_call+36, %eax\n    movl _rtl_io_call+40, %ecx\n    movl _rtl_io_call+44, %edx\n"
    "    ret\n"
    "3:  cmpl $2, %eax\n    je 4f\n"
    "    movb $0, _rtl_io_busy\n"
    "4:  movl _rtl_io_call, %eax\n    movl _rtl_original(,%eax,4), %eax\n    movl %eax, _rtl_io_call+48\n"
    "    movl _rtl_io_call+16, %ebx\n    movl _rtl_io_call+20, %esi\n    movl _rtl_io_call+24, %edi\n    movl _rtl_io_call+28, %ebp\n"
    "    movl _rtl_io_call+4, %eax\n    movl _rtl_io_call+8, %ecx\n    movl _rtl_io_call+12, %edx\n"
    "    jmp *_rtl_io_call+48\n"
    /* The export returns here with ESP = entry ESP + 4 (shadow). */
    ".p2align 4\n"
    "_rtl_io_post:\n"
    "    movl %eax, _rtl_io_call+36\n    movl %ecx, _rtl_io_call+40\n    movl %edx, _rtl_io_call+44\n"
    "    movl %esp, _rtl_io_call+56\n"
    "    movl _rtl_io_stack_top, %esp\n    cld\n"
    "    call _rtl_io_post_c\n"
    "    movl _rtl_io_call+56, %esp\n"
    "    movb $0, _rtl_io_busy\n"
    "    movl _rtl_io_call+36, %eax\n    movl _rtl_io_call+40, %ecx\n    movl _rtl_io_call+44, %edx\n"
    "    jmp *_rtl_io_call+52\n"
    /* A Win32 function or CW3220's allocator on the game's stack:
     * uint32_t rtl_io_guest_call(target, count, args) from the private stack. */
    ".p2align 4\n"
    "_rtl_io_guest_call:\n"
    "    pushl %ebp\n    movl %esp, %ebp\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    movl %esp, _rtl_io_guest_saved\n"
    "    movl 8(%ebp), %eax\n    movl %eax, _rtl_io_guest_target\n"
    "    movl 12(%ebp), %ecx\n    movl 16(%ebp), %esi\n"
    "    movl _rtl_io_call+32, %esp\n    subl $0x20, %esp\n"
    "    leal (%esi,%ecx,4), %esi\n"
    "5:  testl %ecx, %ecx\n    jz 6f\n    subl $4, %esi\n    pushl (%esi)\n    decl %ecx\n    jmp 5b\n"
    "6:  call *_rtl_io_guest_target\n"
    "    movl _rtl_io_guest_saved, %esp\n"
    "    popl %edi\n    popl %esi\n    popl %ebx\n    popl %ebp\n    ret\n"
    /* Recorder in CW3220's KERNEL32 slots (stdcall): logs only inside an
     * observed shadow call on the owner thread; the return slot is swapped
     * and restored, nothing else is written to the game's stack. */
    ".macro RTL_IO_RECORDER k\n"
    ".p2align 4\n"
    "_rtl_io_rec_\\k:\n"
    "    cmpb $0, _rtl_io_recording\n    je 7f\n"
    "    cmpl _rtl_stack_lo, %esp\n    jb 7f\n"
    "    cmpl _rtl_stack_hi, %esp\n    jae 7f\n"
    "    movl $\\k, _rtl_io_rec_kind\n"
    "    movl 4(%esp), %eax\n    movl %eax, _rtl_io_rec_args\n"
    "    movl 8(%esp), %eax\n    movl %eax, _rtl_io_rec_args+4\n"
    "    movl 12(%esp), %eax\n    movl %eax, _rtl_io_rec_args+8\n"
    "    movl 16(%esp), %eax\n    movl %eax, _rtl_io_rec_args+12\n"
    "    movl 20(%esp), %eax\n    movl %eax, _rtl_io_rec_args+16\n"
    "    movl 24(%esp), %eax\n    movl %eax, _rtl_io_rec_args+20\n"
    "    movl 28(%esp), %eax\n    movl %eax, _rtl_io_rec_args+24\n"
    "    movl (%esp), %eax\n    movl %eax, _rtl_io_rec_ret\n"
    "    movl %esp, _rtl_io_rec_esp\n"
    "    movl $_rtl_io_rec_post, (%esp)\n"
    "    jmp *_rtl_io_real+4*\\k\n"
    "7:  jmp *_rtl_io_real+4*\\k\n"
    ".endm\n"
    "RTL_IO_RECORDER 0\nRTL_IO_RECORDER 1\nRTL_IO_RECORDER 2\nRTL_IO_RECORDER 3\nRTL_IO_RECORDER 4\n"
    "RTL_IO_RECORDER 5\nRTL_IO_RECORDER 6\nRTL_IO_RECORDER 7\nRTL_IO_RECORDER 8\nRTL_IO_RECORDER 9\n"
    "RTL_IO_RECORDER 10\nRTL_IO_RECORDER 11\nRTL_IO_RECORDER 12\nRTL_IO_RECORDER 13\nRTL_IO_RECORDER 14\n"
    ".purgem RTL_IO_RECORDER\n"
    ".p2align 4\n"
    "_rtl_io_rec_post:\n"
    "    movl %eax, _rtl_io_rec_eax\n    movl %ecx, _rtl_io_rec_ecx\n    movl %edx, _rtl_io_rec_edx\n"
    "    movl _rtl_io_rec_esp, %ecx\n    movl _rtl_io_rec_ret, %edx\n    movl %edx, (%ecx)\n"
    "    movl %esp, _rtl_io_rec_post_esp\n"
    "    movl _rtl_io_stack_top, %esp\n    cld\n"
    "    call _rtl_io_rec_post_c\n"
    "    movl _rtl_io_rec_post_esp, %esp\n"
    "    movl _rtl_io_rec_eax, %eax\n    movl _rtl_io_rec_ecx, %ecx\n    movl _rtl_io_rec_edx, %edx\n"
    "    jmp *_rtl_io_rec_ret\n"
    ".section .rdata,\"dr\"\n.p2align 2\n"
    "_rtl_io_adapter_table:\n"
#define RTL_T(i) ".long _rtl_io_adapter_" #i "\n"
    RTL_T(0) RTL_T(1) RTL_T(2) RTL_T(3) RTL_T(4) RTL_T(5) RTL_T(6) RTL_T(7) RTL_T(8) RTL_T(9)
    RTL_T(10) RTL_T(11) RTL_T(12) RTL_T(13) RTL_T(14) RTL_T(15) RTL_T(16) RTL_T(17) RTL_T(18) RTL_T(19)
    RTL_T(20) RTL_T(21) RTL_T(22) RTL_T(23) RTL_T(24) RTL_T(25) RTL_T(26) RTL_T(27) RTL_T(28) RTL_T(29)
    RTL_T(30) RTL_T(31) RTL_T(32) RTL_T(33) RTL_T(34) RTL_T(35) RTL_T(36) RTL_T(37) RTL_T(38) RTL_T(39)
    RTL_T(40) RTL_T(41) RTL_T(42) RTL_T(43) RTL_T(44) RTL_T(45) RTL_T(46) RTL_T(47) RTL_T(48) RTL_T(49)
    RTL_T(50) RTL_T(51) RTL_T(52)
#undef RTL_T
    "_rtl_io_rec_table:\n"
    "    .long _rtl_io_rec_0, _rtl_io_rec_1, _rtl_io_rec_2, _rtl_io_rec_3, _rtl_io_rec_4, _rtl_io_rec_5, _rtl_io_rec_6, _rtl_io_rec_7\n"
    "    .long _rtl_io_rec_8, _rtl_io_rec_9, _rtl_io_rec_10, _rtl_io_rec_11, _rtl_io_rec_12, _rtl_io_rec_13, _rtl_io_rec_14\n"
    ".text\n");
extern const uint32_t rtl_io_adapter_table[RTL_IMPORT_COUNT] __asm__("_rtl_io_adapter_table");
extern const uint32_t rtl_io_rec_table[ES2_IO_OS_FUNCTION_COUNT] __asm__("_rtl_io_rec_table");
extern void rtl_io_post(void) __asm__("_rtl_io_post");
extern uint32_t __cdecl rtl_io_guest_call(uint32_t target, uint32_t count, const uint32_t *args) __asm__("_rtl_io_guest_call");

/* ------------------------------------------------------------------------
 * The recorder's C part: the bytes each Win32 call reads, writes or returns
 * (pool layout as es2_io_record documents). Pure copies, no Win32 call. */
static void rtl_io_pool_copy(es2_io_record *k, uint32_t address, uint32_t count) {
    if (!count) return;
    if (!address || count > RTL_IO_POOL - rtl_io_pool_used) { rtl_io_log_overflow = TRUE; return; }
    memcpy(rtl_io_pool + rtl_io_pool_used, (const void *)(uintptr_t)address, count);
    rtl_io_pool_used += count;
    k->size += count;
}
void __cdecl rtl_io_rec_post_c(void) __asm__("_rtl_io_rec_post_c") __attribute__((used));
void __cdecl rtl_io_rec_post_c(void) {
    ++rtl_io_os_calls;
    if (rtl_io_log_count >= RTL_IO_LOG) { rtl_io_log_overflow = TRUE; return; }
    es2_io_record *k = &rtl_io_log[rtl_io_log_count++];
    memset(k, 0, sizeof(*k));
    k->function = rtl_io_rec_kind;
    for (unsigned i = 0; i < 7u; ++i) k->arg[i] = i < rtl_io_os_args[rtl_io_rec_kind] ? rtl_io_rec_args[i] : 0u;
    k->result = rtl_io_rec_eax;
    k->data = rtl_io_pool_used;
    const uint32_t *a = k->arg;
    switch (k->function) {
    case ES2_IO_OS_CREATE_FILE: rtl_io_pool_copy(k, a[3], 12); break;
    case ES2_IO_OS_READ_FILE:
        k->count = a[3] ? *(const volatile uint32_t *)(uintptr_t)a[3] : 0u;
        if (k->result == 1u) {
            uint32_t n = k->count < RTL_IO_COPY_LIMIT ? k->count : RTL_IO_COPY_LIMIT;
            if (n == k->count) rtl_io_pool_copy(k, a[1], n);
        }
        break;
    case ES2_IO_OS_WRITE_FILE:
        k->count = a[3] ? *(const volatile uint32_t *)(uintptr_t)a[3] : 0u;
        if (a[2] <= RTL_IO_COPY_LIMIT) rtl_io_pool_copy(k, a[1], a[2]);
        break;
    case ES2_IO_OS_FIND_FIRST: case ES2_IO_OS_FIND_NEXT: rtl_io_pool_copy(k, a[1], ES2_IO_FIND_DATA); break;
    case ES2_IO_OS_FILE_TIME_TO_LOCAL: rtl_io_pool_copy(k, a[0], 8); rtl_io_pool_copy(k, a[1], 8); break;
    case ES2_IO_OS_FILE_TIME_TO_DOS: rtl_io_pool_copy(k, a[0], 8); rtl_io_pool_copy(k, a[1], 2); rtl_io_pool_copy(k, a[2], 2); break;
    case ES2_IO_OS_GET_LOCAL_TIME: rtl_io_pool_copy(k, a[0], 16); break;
    default: break;
    }
}

/* ------------------------------------------------------------------------
 * The replaced env: direct memory, Win32 and the allocator through
 * rtl_io_guest_call. */
static uint8_t rtl_io_direct_read(void *context, uint32_t a) { (void)context; return *(const volatile uint8_t *)(uintptr_t)a; }
static void rtl_io_direct_write(void *context, uint32_t a, uint8_t v) { (void)context; *(volatile uint8_t *)(uintptr_t)a = v; }
static void rtl_io_real_os(void *context, es2_io_os *call) {
    (void)context;
    static uint32_t sa[3], number, filetime[2], local[2];
    static WIN32_FIND_DATAA found;
    static SYSTEMTIME now;
    uint32_t args[7] = {0}, *a = call->arg, target = rtl_io_real[call->function], n = rtl_io_os_args[call->function];
    switch (call->function) {
    case ES2_IO_OS_CREATE_FILE:
        sa[0] = 12; sa[1] = 0; sa[2] = a[3];
        args[0] = a[0]; args[1] = a[1]; args[2] = a[2]; args[3] = (uint32_t)(uintptr_t)sa; args[4] = a[4]; args[5] = a[5]; args[6] = 0;
        break;
    case ES2_IO_OS_READ_FILE: args[0] = a[0]; args[1] = a[1]; args[2] = a[2]; args[3] = (uint32_t)(uintptr_t)&number; args[4] = 0; break;
    case ES2_IO_OS_WRITE_FILE:
        args[0] = a[0]; args[1] = a[1] ? a[1] : (uint32_t)(uintptr_t)call->data; args[2] = a[2];
        args[3] = (uint32_t)(uintptr_t)&number; args[4] = 0;
        break;
    case ES2_IO_OS_SET_FILE_POINTER: args[0] = a[0]; args[1] = a[1]; args[2] = 0; args[3] = a[2]; break;
    case ES2_IO_OS_GET_FILE_SIZE: args[0] = a[0]; args[1] = 0; break;
    case ES2_IO_OS_FIND_FIRST: case ES2_IO_OS_FIND_NEXT: args[0] = a[0]; args[1] = (uint32_t)(uintptr_t)&found; break;
    case ES2_IO_OS_FILE_TIME_TO_LOCAL:
        filetime[0] = a[0]; filetime[1] = a[1];
        args[0] = (uint32_t)(uintptr_t)filetime; args[1] = (uint32_t)(uintptr_t)local;
        break;
    case ES2_IO_OS_FILE_TIME_TO_DOS:
        filetime[0] = a[0]; filetime[1] = a[1];
        args[0] = (uint32_t)(uintptr_t)filetime; args[1] = a[2]; args[2] = a[3];
        break;
    case ES2_IO_OS_GET_LOCAL_TIME: args[0] = (uint32_t)(uintptr_t)&now; break;
    default: args[0] = a[0]; break;
    }
    number = 0;
    call->result = rtl_io_guest_call(target, n, args);
    switch (call->function) {
    case ES2_IO_OS_READ_FILE: case ES2_IO_OS_WRITE_FILE: call->count = number; break;
    case ES2_IO_OS_FIND_FIRST: case ES2_IO_OS_FIND_NEXT: memcpy(call->out, &found, ES2_IO_FIND_DATA); break;
    case ES2_IO_OS_FILE_TIME_TO_LOCAL: memcpy(call->out, local, 8); break;
    case ES2_IO_OS_GET_LOCAL_TIME: memcpy(call->out, &now, 16); break;
    default: break;
    }
}
static uint32_t rtl_io_real_malloc(void *context, uint32_t n, uint32_t hint) {
    (void)context; (void)hint;
    return rtl_io_guest_call(rtl_io_malloc_address, 1, &n);
}
static void rtl_io_real_free(void *context, uint32_t p) { (void)context; rtl_io_guest_call(rtl_io_free_address, 1, &p); }
static const es2_io_env rtl_io_real_env = {NULL, rtl_io_direct_read, rtl_io_direct_write, rtl_io_real_os, rtl_io_real_malloc, rtl_io_real_free};

/* The FILE / find block argument of a call (for the snapshot). */
static void rtl_io_tracked_arguments(es2_io_function f, uint32_t args, uint32_t *fp, uint32_t *ffblk) {
    const volatile uint32_t *a = (const volatile uint32_t *)(uintptr_t)args;
    *fp = *ffblk = 0;
    switch (f) {
    case ES2_IO_FCLOSE: case ES2_IO_FSEEK: case ES2_IO_FTELL: case ES2_IO_REWIND: case ES2_IO_FSCANF: *fp = a[0]; break;
    case ES2_IO_FREAD: case ES2_IO_FWRITE: *fp = a[3]; break;
    case ES2_IO_FINDFIRST: *ffblk = a[1]; break;
    case ES2_IO_FINDNEXT: *ffblk = a[0]; break;
    default: break;
    }
}

uint32_t __cdecl rtl_io_pre_c(void) __asm__("_rtl_io_pre_c") __attribute__((used));
uint32_t __cdecl rtl_io_pre_c(void) {
    unsigned i = rtl_io_call.index;
    es2_io_function f = (es2_io_function)rtl_io_of[i];
    uint32_t args = rtl_io_call.esp + 4u;
    if (rtl_io_replaced[i]) {
        DWORD error = GetLastError();
        uint32_t eax = 0;
        if (es2_io_call(f, &rtl_io_real_env, &rtl_io_layout, args, &eax) != ES2_IO_DONE) {
            ++rtl_skipped[i];
            SetLastError(error);
            return 1;
        }
        rtl_io_call.out_eax = eax; rtl_io_call.out_ecx = 0; rtl_io_call.out_edx = 0;
        return 0;
    }
    /* Shadow: track the state before the call, record its Win32 calls. */
    uint32_t fp, ffblk;
    rtl_io_tracked_arguments(f, args, &fp, &ffblk);
    es2_io_replay_reset(&rtl_io_replay);
    if (!es2_io_replay_track_input(&rtl_io_replay, args, 64, rtl_io_direct_read, NULL) ||
        !es2_io_replay_track_call(&rtl_io_replay, &rtl_io_layout, fp, ffblk, rtl_io_direct_read, NULL)) {
        ++rtl_io_shadow_skipped[i]; rtl_io_last_skip = "track";
        return 1;
    }
    rtl_io_log_count = 0;
    rtl_io_pool_used = 0;
    rtl_io_log_overflow = FALSE;
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)rtl_io_call.esp;
    rtl_io_call.saved_return = *slot;
    *slot = (uint32_t)(uintptr_t)rtl_io_post;
    rtl_io_recording = 1;
    return 2;
}

void __cdecl rtl_io_post_c(void) __asm__("_rtl_io_post_c") __attribute__((used));
void __cdecl rtl_io_post_c(void) {
    rtl_io_recording = 0;
    *(volatile uint32_t *)(uintptr_t)rtl_io_call.esp = rtl_io_call.saved_return;   /* as the export's `ret` leaves it */
    unsigned i = rtl_io_call.index;
    es2_io_function f = (es2_io_function)rtl_io_of[i];
    if (rtl_io_log_overflow) { ++rtl_io_shadow_skipped[i]; rtl_io_last_skip = "log"; return; }
    es2_io_env env;
    rtl_io_replay.live_read8 = rtl_io_direct_read;
    rtl_io_replay.live_context = NULL;
    es2_io_replay_env(&env, &rtl_io_replay, rtl_io_log, rtl_io_log_count, rtl_io_pool, rtl_io_pool_used);
    uint32_t eax = 0;
    if (es2_io_call(f, &env, &rtl_io_layout, rtl_io_call.esp + 4u, &eax) != ES2_IO_DONE) { ++rtl_io_shadow_skipped[i]; rtl_io_last_skip = "declined"; return; }
    const char *what = es2_io_replay_finish(&rtl_io_replay, eax, rtl_io_call.out_eax);
    if (rtl_io_replay.skip) { ++rtl_io_shadow_skipped[i]; rtl_io_last_skip = "replay"; return; }
    if (!what) return;
    ++rtl_io_mismatches[i];
    if (!rtl_io_mismatch_detail[0]) {
        const volatile uint32_t *a = (const volatile uint32_t *)(uintptr_t)(rtl_io_call.esp + 4u);
        snprintf(rtl_io_mismatch_detail, sizeof(rtl_io_mismatch_detail),
                 "%s:%s@%08x:model=%08x:orig=%08x:args=%08x,%08x,%08x,%08x:eax=%08x,%08x",
                 rtl_imports[i].key, what, (unsigned)rtl_io_replay.where, (unsigned)rtl_io_replay.model_value,
                 (unsigned)rtl_io_replay.real_value, (unsigned)a[0], (unsigned)a[1], (unsigned)a[2], (unsigned)a[3],
                 (unsigned)eax, (unsigned)rtl_io_call.out_eax);
    }
}

/* ------------------------------------------------------------------------
 * Install / uninstall. */
static BOOL rtl_io_prepare(BYTE *cw, rtl_resolver resolve, void *context, BOOL shadow) {
    if (!cw) return rtl_refuse("cw_image", NULL);
    uint32_t pe = rtl_u32(cw + 0x3c);
    if (cw[0] != 'M' || cw[1] != 'Z' || pe < 64 || pe > 1024 || memcmp(cw + pe, "PE\0\0", 4) ||
        rtl_u32(cw + pe + 24 + 56) != RTL_HEAP_CW_IMAGE) return rtl_refuse("cw_image", NULL);
    uint32_t base = (uint32_t)(uintptr_t)cw;
    for (int f = 0; f < ES2_IO_FUNCTION_COUNT; ++f) {
        FARPROC address = resolve ? resolve(es2_io_name((es2_io_function)f), context) : NULL;
        if ((uint32_t)(uintptr_t)address != base + rtl_io_export_rva[f]) return rtl_refuse("cw_io_export", es2_io_name((es2_io_function)f) + 1);
    }
    FARPROC m = resolve ? resolve("_malloc", context) : NULL, fr = resolve ? resolve("_free", context) : NULL;
    if ((uint32_t)(uintptr_t)m != base + RTL_IO_MALLOC_RVA || (uint32_t)(uintptr_t)fr != base + RTL_IO_FREE_RVA)
        return rtl_refuse("cw_io_export", "malloc");
    for (unsigned k = 0; k < sizeof(rtl_io_operands) / sizeof(rtl_io_operands[0]); ++k)
        if (rtl_u32(cw + rtl_io_operands[k][0]) != base + rtl_io_operands[k][1]) return rtl_refuse("cw_io_operand", NULL);
    /* KERNEL32's lookup table (the first descriptor) by name. */
    for (unsigned k = 0; k < ES2_IO_OS_FUNCTION_COUNT; ++k) {
        rtl_io_slot_rva[k] = 0;
        for (uint32_t e = 0; e < 128u; ++e) {
            uint32_t entry = rtl_u32(cw + RTL_HEAP_K32_LOOKUP + 4u * e);
            if (!entry) break;
            if (!(entry & 0x80000000u) && entry < RTL_HEAP_CW_IMAGE - 32u && !strcmp((const char *)cw + entry + 2, rtl_io_os_names[k])) {
                rtl_io_slot_rva[k] = RTL_HEAP_K32_IAT + 4u * e;
                break;
            }
        }
        if (!rtl_io_slot_rva[k]) return rtl_refuse("cw_io_imports", NULL);
        rtl_io_real[k] = rtl_u32(cw + rtl_io_slot_rva[k]);
        if (!rtl_io_real[k]) return rtl_refuse("cw_io_imports", NULL);
    }
    if (!rtl_io_stack_top) {
        BYTE *stack = VirtualAlloc(NULL, RTL_IO_STACK, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!stack) return rtl_refuse("io_stack", NULL);
        rtl_io_stack_top = (uint32_t)(uintptr_t)stack + RTL_IO_STACK;
    }
    if (shadow && !rtl_io_log) {
        size_t slots = (size_t)1u << RTL_IO_OVERLAY_BITS;
        size_t bytes = slots * 4u * 2u + slots + (slots / 2u) * 4u + RTL_IO_SNAPSHOT + (size_t)RTL_IO_LOG * sizeof(es2_io_record) + RTL_IO_POOL;
        BYTE *block = VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!block) return rtl_refuse("shadow_memory", NULL);
        uint32_t *keys = (uint32_t *)block, *stamps = keys + slots, *written = stamps + slots;
        uint8_t *values = (uint8_t *)(written + slots / 2u), *snapshot = values + slots;
        es2_io_replay_init(&rtl_io_replay, keys, stamps, values, written, RTL_IO_OVERLAY_BITS, (uint32_t)(slots / 2u), snapshot, RTL_IO_SNAPSHOT);
        rtl_io_log = (es2_io_record *)(snapshot + RTL_IO_SNAPSHOT);
        rtl_io_pool = (uint8_t *)(rtl_io_log + RTL_IO_LOG);
    }
    es2_io_layout_cw3220(&rtl_io_layout, base);
    rtl_io_malloc_address = base + RTL_IO_MALLOC_RVA;
    rtl_io_free_address = base + RTL_IO_FREE_RVA;
    memset(rtl_io_mismatches, 0, sizeof(rtl_io_mismatches));
    memset(rtl_io_shadow_skipped, 0, sizeof(rtl_io_shadow_skipped));
    rtl_io_os_calls = rtl_io_os_unobserved = 0;
    rtl_io_mismatch_detail[0] = 0;
    rtl_io_busy = 0;
    rtl_io_recording = 0;
    rtl_io_cw = cw;
    return TRUE;
}
/* The recorder into CW3220's KERNEL32 slots (shadow only). */
static BOOL rtl_io_patch(void) {
    for (unsigned k = 0; k < ES2_IO_OS_FUNCTION_COUNT; ++k) {
        if (!rtl_swap((DWORD *)(rtl_io_cw + rtl_io_slot_rva[k]), rtl_io_real[k], rtl_io_rec_table[k])) return rtl_refuse("cw_io_patch", NULL);
        rtl_io_recorder_patched[k] = TRUE;
    }
    return TRUE;
}
static void rtl_io_unpatch(void) {
    for (unsigned k = 0; k < ES2_IO_OS_FUNCTION_COUNT; ++k)
        if (rtl_io_recorder_patched[k] && rtl_swap((DWORD *)(rtl_io_cw + rtl_io_slot_rva[k]), rtl_io_rec_table[k], rtl_io_real[k]))
            rtl_io_recorder_patched[k] = FALSE;
}
static BOOL rtl_io_unpatched(void) {
    for (unsigned k = 0; k < ES2_IO_OS_FUNCTION_COUNT; ++k) if (rtl_io_recorder_patched[k]) return FALSE;
    return TRUE;
}

/* All shadow mismatches of the set; saturates. */
static uint64_t rtl_io_mismatch_total(void) {
    uint64_t total = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        uint64_t add = rtl_io_mismatches[i];
        total = total > UINT64_MAX - add ? UINT64_MAX : total + add;
    }
    return total;
}
static BOOL rtl_io_any_shadow(void) {
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) if (rtl_io_shadowed[i]) return TRUE;
    return FALSE;
}
/* Exit-record fields of the set (after the heap's). */
static int rtl_io_fields(char *out, size_t capacity) {
    int n = snprintf(out, capacity, ",\"rtl_io_os_calls\":%llu", (unsigned long long)rtl_io_os_calls);
    if (n <= 0 || (size_t)n >= capacity) return 0;
    if (rtl_io_mismatch_detail[0]) {
        int added = snprintf(out + n, capacity - (size_t)n, ",\"rtl_io_first_mismatch\":\"%s\"", rtl_io_mismatch_detail);
        if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
        n += added;
    }
    return n;
}
