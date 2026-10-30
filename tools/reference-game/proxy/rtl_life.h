/* The runtime lifecycle switched to own code (rtl_imports.md, section
 * "the lifecycle group as own code"). Included by rtl_imports.h after
 * rtl_io_shadow.h.
 *
 * The group: __startup, _atexit, __exit, _abort and __ErrorExit, all five in
 * mode replaced or none (`lifecycle_set`). The model is core/src/rtl_start.c
 * (rtl_imports.md, "the runtime lifecycle", with the registers and frame
 * words of "the lifecycle group as own code"); this file is its env on the
 * real stack:
 *
 * - Start-up. The arming at DBSIM's entry (rtl_imports.h) installs the
 *   group, then jumps
 *   here (rtl_life_startup_entry) instead of to CW3220's __startup, with the
 *   entry registers and ESP E ([E] = 0, [E+4] = the module table). The C part
 *   runs on a lifecycle stack of its own (64 KiB; not the private stack of
 *   rtl_imports.h,
 *   whose adapters run while the lifecycle waits for a guest function).
 * - Guest calls (init and exit records, WinMain, atexit functions, the stream
 *   hooks, a SIGABRT handler) and Win32 calls go through rtl_life_call: the
 *   registers the model gives, ESP exactly where the original enters them,
 *   the arguments and frame words already written by the model
 *   (env->write32); the return address at [ESP] is rtl_life_return, the only
 *   word of the stack image that differs from the original's. ExitProcess is
 *   entered by a jump with the original's return address (41a8cc) in place,
 *   since it does not return. The Win32 functions are the values of
 *   CW3220's own import slots at install (the functions the original calls).
 * - The adapters. _atexit is a leaf through the replaced adapter
 *   (rtl_replaced_c): EAX/ECX/EDX and the pushed EBP below the return address
 *   as CW3220's 419070. __exit, _abort and __ErrorExit save the caller's
 *   registers, continue on the lifecycle stack below the frames still active
 *   (rtl_life_current) and never return.
 * - Shared state. The model's runtime is the lifecycle's state; CW3220's
 *   variables that its other code reads are written as __startup writes them
 *   (the _fmode pointer that open/fopen read, the matherr pair, the module
 *   list, the frame record that CW3220's exit record 405260 unlinks at
 *   detach, environment and command line); the stream hooks and the SIGABRT
 *   entry are read from CW3220 at call time (installed lazily by its stream
 *   code; the reset written back); CW3220's atexit table and main's arguments
 *   are adopted at install. CW3220's own __startup and exit never run.
 * - The proxy's own exit hooks (live coverage, entry probes, the speed
 *   runtime) install later, at the first DirectDraw/MCI call or the
 *   mission-load call: their `live_import` accepts the shim's thunk in the
 *   _atexit/__exit slots, so they register with the own table through it and
 *   chain the own __exit (live_coverage.h).
 *
 * Calls from other threads take the thunk's foreign path to CW3220 (an
 * _atexit there would be lost; rtl_life_cw_atexit reports CW3220's own count,
 * expected 0). */
#include "es2/core/rtl_start.h"

#define RTL_LIFE_STACK 0x10000u
/* CW3220 RVAs (link base 0x400000) of what the lifecycle shares with CW3220. */
#define RTL_LIFE_GUI 0x23f30u
#define RTL_LIFE_MATHERR 0x21df8u
#define RTL_LIFE_MATHERRL 0x21dfcu
#define RTL_LIFE_FMODE 0x1fdb0u
#define RTL_LIFE_MODULE_COUNT 0x23f24u
#define RTL_LIFE_MODULE_TABLE 0x23f28u
#define RTL_LIFE_FRAME 0x23f0cu
#define RTL_LIFE_ENVIRONMENT 0x23f10u
#define RTL_LIFE_COMMAND 0x23f14u
#define RTL_LIFE_ARGC 0x23f18u
#define RTL_LIFE_ATEXIT_COUNT 0x222b8u
#define RTL_LIFE_ATEXIT_TABLE 0x23a64u
#define RTL_LIFE_STREAM_HOOKS 0x222bcu
#define RTL_LIFE_SIGABRT 0x223a4u
#define RTL_LIFE_SIGABRT_EXTRA 0x223d9u
#define RTL_LIFE_DUMMY_HOOK 0x191a0u
#define RTL_LIFE_ABORT_MESSAGE 0x22298u
#define RTL_LIFE_K32_LOOKUP 0x2403cu
#define RTL_LIFE_K32_IAT 0x241c0u
#define RTL_LIFE_U32_LOOKUP 0x241b0u
#define RTL_LIFE_U32_IAT 0x24334u

/* The five imports of the group and their CW3220 exports. */
static const struct { unsigned import; const char *symbol; uint32_t rva; } rtl_life_group[] = {
    {RTL_STARTUP_INDEX, "__startup", 0x1a6e0u}, {RTL_I_atexit, "_atexit", 0x19070u}, {RTL_I_exit, "__exit", 0x19214u},
    {RTL_I_abort, "_abort", 0x19060u}, {RTL_I_ErrorExit, "__ErrorExit", 0x18fa4u},
};
#define RTL_LIFE_GROUP (sizeof(rtl_life_group) / sizeof(rtl_life_group[0]))
static int rtl_life_index(unsigned import) {
    for (unsigned k = 0; k < RTL_LIFE_GROUP; ++k) if (rtl_life_group[k].import == import) return (int)k;
    return -1;
}
/* Relocated operands of the code the model mirrors (operand RVA, RVA it must
 * name): the variables above, the record's handler 405160, raise's tables,
 * the abort message and the EnumThreadWindows callback. */
static const uint32_t rtl_life_operands[][2] = {
    {0x1a6f3u, 0x23f30u}, {0x14e36u, 0x21df8u}, {0x14e3cu, 0x21dfcu}, {0x05877u, 0x1fdb0u}, {0x1a718u, 0x23f24u},
    {0x1a722u, 0x23f28u}, {0x1a72bu, 0x23f0cu}, {0x05209u, 0x05160u}, {0x1a73au, 0x23f10u}, {0x1a744u, 0x23f14u},
    {0x1a82du, 0x23f18u}, {0x1a827u, 0x23f1cu}, {0x1a820u, 0x23f20u}, {0x19075u, 0x222b8u}, {0x1908fu, 0x23a64u},
    {0x191d5u, 0x222bcu}, {0x191e5u, 0x222c0u}, {0x191ebu, 0x222c4u}, {0x1a30eu, 0x22380u}, {0x1a34du, 0x223d0u},
    {0x1a0deu, 0x223a8u}, {0x1904du, 0x22298u}, {0x18ec1u, 0x18e88u},
};
/* The Win32 calls in es2_rtl_api order: the jump thunk the original calls,
 * its import slot, and the name in the slot's lookup table. */
static const struct { uint32_t thunk, slot; const char *name; } rtl_life_apis[ES2_RTL_API_COUNT] = {
    {0x1ec51u, 0x2422cu, "GetEnvironmentStrings"}, {0x1ec1bu, 0x24208u, "GetCommandLineA"},
    {0x1ec9fu, 0x24260u, "GetModuleHandleA"}, {0x1ec99u, 0x2425cu, "GetModuleFileNameA"}, {0x1ecc9u, 0x2427cu, "GetVersion"},
    {0x1ec3fu, 0x24220u, "GetCurrentThreadId"}, {0x1ede3u, 0x2433cu, "EnumThreadWindows"}, {0x1edd7u, 0x24334u, "MessageBoxA"},
    {0x1ebdfu, 0x241e0u, "ExitProcess"},
};

/* A call on the game's stack (read by the asm below). */
typedef struct rtl_life_frame {
    uint32_t target;        /*  0 */
    uint32_t esp;           /*  4: ESP at the target's entry (its return-address slot) */
    es2_rtl_cpu in;         /*  8: EAX ECX EDX EBX ESI EDI EBP at the entry */
    es2_rtl_cpu out;        /* 36: at the return */
    uint32_t out_esp;       /* 64 */
    uint32_t c_esp;         /* 68: the lifecycle stack while the callee runs */
    uint32_t outer;         /* 72: the enclosing frame */
    uint32_t jump;          /* 76: enter by jmp ([esp] already holds a return address) */
} rtl_life_frame;
_Static_assert(offsetof(rtl_life_frame, in) == 8 && offsetof(rtl_life_frame, out) == 36 && offsetof(rtl_life_frame, out_esp) == 64 &&
               offsetof(rtl_life_frame, c_esp) == 68 && offsetof(rtl_life_frame, outer) == 72 && offsetof(rtl_life_frame, jump) == 76 &&
               offsetof(es2_rtl_cpu, ebp) == 24, "lifecycle frame offsets");

__attribute__((used)) static uint32_t rtl_life_current, rtl_life_target, rtl_life_scratch, rtl_life_top, rtl_life_caller_esp;
__attribute__((used)) static es2_rtl_cpu rtl_life_caller;
static uint32_t rtl_life_stack_base;
static es2_rtl_runtime rtl_life_rt;
static es2_rtl_start_env rtl_life_env;
static es2_rtl_site *rtl_life_site;
static BYTE *rtl_life_cw;
static uint32_t rtl_life_api[ES2_RTL_API_COUNT];
static BOOL rtl_life_active;   /* rtl_life_replaced: rtl_imports.h */
static uint64_t rtl_life_init_records, rtl_life_winmain, rtl_life_registered, rtl_life_refused, rtl_life_atexit_run;
static uint64_t rtl_life_exit_records, rtl_life_hooks, rtl_life_signals;
static int64_t rtl_life_status = -1;
/* The model's return address of the call in progress (a CW3220 RVA; the
 * in-vitro harness compares it with the original's). */
static uint32_t rtl_life_return_rva;

__asm__(
    ".text\n.p2align 4\n"
    /* cdecl void rtl_life_call(rtl_life_frame *): the callee on the game's
     * stack, back on the lifecycle stack with every register it returned. */
    "_rtl_life_call:\n"
    "    pushl %ebp\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    movl 20(%esp), %eax\n"
    "    movl %esp, 68(%eax)\n"
    "    movl _rtl_life_current, %ecx\n    movl %ecx, 72(%eax)\n    movl %eax, _rtl_life_current\n"
    "    movl 0(%eax), %ecx\n    movl %ecx, _rtl_life_target\n"
    "    movl 4(%eax), %esp\n"
    "    cmpl $0, 76(%eax)\n    jne 1f\n"
    "    addl $4, %esp\n"
    "    movl 32(%eax), %ebp\n    movl 28(%eax), %edi\n    movl 24(%eax), %esi\n    movl 20(%eax), %ebx\n"
    "    movl 16(%eax), %edx\n    movl 12(%eax), %ecx\n    movl 8(%eax), %eax\n"
    "    cld\n"
    "    call *_rtl_life_target\n"
    "_rtl_life_return:\n"
    "    movl %eax, _rtl_life_scratch\n"
    "    movl _rtl_life_current, %eax\n"
    "    movl %ecx, 40(%eax)\n    movl %edx, 44(%eax)\n    movl %ebx, 48(%eax)\n    movl %esi, 52(%eax)\n"
    "    movl %edi, 56(%eax)\n    movl %ebp, 60(%eax)\n    movl %esp, 64(%eax)\n"
    "    movl _rtl_life_scratch, %ecx\n    movl %ecx, 36(%eax)\n"
    "    movl 72(%eax), %ecx\n    movl %ecx, _rtl_life_current\n"
    "    movl 68(%eax), %esp\n"
    "    popl %edi\n    popl %esi\n    popl %ebx\n    popl %ebp\n"
    "    cld\n    ret\n"
    "1:  movl 32(%eax), %ebp\n    movl 28(%eax), %edi\n    movl 24(%eax), %esi\n    movl 20(%eax), %ebx\n"
    "    movl 16(%eax), %edx\n    movl 12(%eax), %ecx\n    movl 8(%eax), %eax\n"
    "    cld\n"
    "    jmp *_rtl_life_target\n"
    /* The start-up in place of CW3220's __startup: ESP = E, DBSIM's entry
     * registers; the C part on the lifecycle stack never returns. */
    ".p2align 4\n"
    "_rtl_life_startup_entry:\n"
    "    movl %eax, _rtl_life_caller\n    movl %ecx, _rtl_life_caller+4\n    movl %edx, _rtl_life_caller+8\n"
    "    movl %ebx, _rtl_life_caller+12\n    movl %esi, _rtl_life_caller+16\n    movl %edi, _rtl_life_caller+20\n"
    "    movl %ebp, _rtl_life_caller+24\n    movl %esp, _rtl_life_caller_esp\n"
    "    movl _rtl_life_top, %esp\n    cld\n"
    "    call _rtl_life_startup_c\n"
    "    ud2\n"
    /* __exit, _abort, __ErrorExit from the thunk: [esp] = the caller's
     * return address; nothing is written to the game's stack here. */
    ".p2align 4\n"
    "_rtl_life_exit_entry:\n    movl %eax, _rtl_life_caller\n    movl $1, %eax\n    jmp _rtl_life_adapter_common\n"
    ".p2align 4\n"
    "_rtl_life_abort_entry:\n    movl %eax, _rtl_life_caller\n    movl $2, %eax\n    jmp _rtl_life_adapter_common\n"
    ".p2align 4\n"
    "_rtl_life_error_entry:\n    movl %eax, _rtl_life_caller\n    movl $3, %eax\n    jmp _rtl_life_adapter_common\n"
    ".p2align 4\n"
    "_rtl_life_adapter_common:\n"
    "    movl %ecx, _rtl_life_caller+4\n    movl %edx, _rtl_life_caller+8\n    movl %ebx, _rtl_life_caller+12\n"
    "    movl %esi, _rtl_life_caller+16\n    movl %edi, _rtl_life_caller+20\n    movl %ebp, _rtl_life_caller+24\n"
    "    movl %esp, _rtl_life_caller_esp\n"
    "    movl _rtl_life_current, %ecx\n    testl %ecx, %ecx\n    jz 1f\n"
    "    movl 68(%ecx), %esp\n    subl $64, %esp\n    andl $-16, %esp\n    jmp 2f\n"
    "1:  movl _rtl_life_top, %esp\n"
    "2:  cld\n    subl $12, %esp\n    pushl %eax\n"
    "    call _rtl_life_adapter_c\n"
    "    ud2\n");
extern void __cdecl rtl_life_call(rtl_life_frame *frame) __asm__("_rtl_life_call");
extern void rtl_life_return(void) __asm__("_rtl_life_return");
extern void rtl_life_startup_entry(void) __asm__("_rtl_life_startup_entry");
extern void rtl_life_exit_entry(void) __asm__("_rtl_life_exit_entry");
extern void rtl_life_abort_entry(void) __asm__("_rtl_life_abort_entry");
extern void rtl_life_error_entry(void) __asm__("_rtl_life_error_entry");

/* ------------------------------------------------------------------------
 * The env. */
static uint8_t rtl_life_read8(void *c, uint32_t a) { (void)c; return *(const volatile uint8_t *)(uintptr_t)a; }
static void rtl_life_write8(void *c, uint32_t a, uint8_t v) { (void)c; *(volatile uint8_t *)(uintptr_t)a = v; }
static uint32_t rtl_life_read32(void *c, uint32_t a) { (void)c; return *(const volatile uint32_t *)(uintptr_t)a; }
static void rtl_life_write32(void *c, uint32_t a, uint32_t v) { (void)c; *(volatile uint32_t *)(uintptr_t)a = v; }
static void rtl_life_cw_set(uint32_t rva, uint32_t v) { *(volatile uint32_t *)(rtl_life_cw + rva) = v; }
static uint32_t rtl_life_cw_get(uint32_t rva) { return *(const volatile uint32_t *)(rtl_life_cw + rva); }

static void rtl_life_env_call(void *c, es2_rtl_guest_call *k) {
    (void)c;
    switch (k->kind) {
    case ES2_RTL_CALL_INIT_RECORD: ++rtl_life_init_records; break;
    case ES2_RTL_CALL_WINMAIN: case ES2_RTL_CALL_MAIN: ++rtl_life_winmain; break;
    case ES2_RTL_CALL_ATEXIT: ++rtl_life_atexit_run; break;
    case ES2_RTL_CALL_EXIT_RECORD: ++rtl_life_exit_records; break;
    case ES2_RTL_CALL_SIGNAL: ++rtl_life_signals; break;
    default: ++rtl_life_hooks; break;
    }
    rtl_life_frame f;
    memset(&f, 0, sizeof(f));
    f.target = k->target;
    f.esp = k->esp;
    f.in = k->regs;
    rtl_life_return_rva = k->return_rva;
    rtl_life_call(&f);
    k->out = f.out;
    k->result = f.out.eax;
    k->popped = f.out_esp - (k->esp + 4u);
    if (k->kind == ES2_RTL_CALL_WINMAIN || k->kind == ES2_RTL_CALL_MAIN) rtl_life_status = (int64_t)k->result;
}
static void rtl_life_env_fpu(void *c, uint16_t w) {
    (void)c;
    __asm__ volatile("finit\n\tfldcw %0" ::"m"(w));   /* __fpreset: wait; fninit; then _control87 */
}
static void rtl_life_env_frame(void *c, uint32_t record, uint32_t handler_rva) {
    (void)c;
    uint32_t head;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(head));
    *(volatile uint32_t *)(uintptr_t)record = head;
    *(volatile uint32_t *)(uintptr_t)(record + 4u) = rtl_life_rt.code_base + handler_rva;
    __asm__ volatile("movl %0, %%fs:0" ::"r"(record) : "memory");
    /* What __startup has stored in CW3220 by now. */
    rtl_life_cw_set(RTL_LIFE_GUI, rtl_life_rt.gui);
    rtl_life_cw_set(RTL_LIFE_MATHERR, rtl_life_rt.matherr);
    rtl_life_cw_set(RTL_LIFE_MATHERRL, rtl_life_rt.matherrl);
    rtl_life_cw_set(RTL_LIFE_FMODE, rtl_life_rt.fmode);
    rtl_life_cw_set(RTL_LIFE_MODULE_COUNT, rtl_life_rt.module_count);
    rtl_life_cw_set(RTL_LIFE_MODULE_TABLE, rtl_life_rt.module_table);
    rtl_life_cw_set(RTL_LIFE_FRAME, rtl_life_rt.frame_record);
}
static void rtl_life_env_site(void *c, es2_rtl_site *site) { (void)c; rtl_life_site = site; }
/* The announced Win32 call: its arguments are on the stack already. */
static uint32_t rtl_life_api_call(void) {
    es2_rtl_site *s = rtl_life_site;
    rtl_life_frame f;
    memset(&f, 0, sizeof(f));
    f.target = rtl_life_api[s->api];
    f.esp = s->esp;
    f.in = s->cpu;
    rtl_life_return_rva = s->return_rva;
    if (s->api == ES2_RTL_API_EXIT_PROCESS) {
        *(volatile uint32_t *)(uintptr_t)s->esp = rtl_life_rt.code_base + s->return_rva;
        f.jump = 1;
    }
    rtl_life_call(&f);
    s->cpu = f.out;
    return f.out.eax;
}
static uint32_t rtl_life_env_environment(void *c) {
    (void)c;
    uint32_t r = rtl_life_api_call();
    rtl_life_cw_set(RTL_LIFE_ENVIRONMENT, r);
    return r;
}
static uint32_t rtl_life_env_command(void *c) {
    (void)c;
    uint32_t r = rtl_life_api_call();
    rtl_life_cw_set(RTL_LIFE_COMMAND, r);
    return r;
}
static uint32_t rtl_life_env_handle(void *c, uint32_t name) { (void)c; (void)name; return rtl_life_api_call(); }
static uint32_t rtl_life_env_file(void *c, char *buffer, uint32_t size) {
    (void)c;
    uint32_t guest = rtl_life_site->guest, r = rtl_life_api_call();
    memcpy(buffer, (const void *)(uintptr_t)guest, size);   /* the model's buffer holds size + 1 zeroed bytes */
    return r;
}
static uint32_t rtl_life_env_version(void *c) { (void)c; return rtl_life_api_call(); }
static uint32_t rtl_life_env_thread(void *c) { (void)c; return rtl_life_api_call(); }
static int rtl_life_env_window(void *c, uint32_t thread) {
    (void)c; (void)thread;
    uint32_t flag = rtl_life_site->guest;
    rtl_life_api_call();
    return *(const volatile uint32_t *)(uintptr_t)flag != 0;
}
static void rtl_life_env_box(void *c, uint32_t text, const char *title, uint32_t style, uint32_t esp) {
    (void)c; (void)text; (void)title; (void)style; (void)esp;
    rtl_life_api_call();
}
static void rtl_life_env_exit(void *c, uint32_t status, uint32_t esp) {
    (void)c; (void)esp;
    rtl_life_status = (int64_t)status;
    rtl_life_api_call();
}

/* ------------------------------------------------------------------------
 * The C parts of the entries (on the lifecycle stack). */
void __cdecl rtl_life_startup_c(void) __asm__("_rtl_life_startup_c") __attribute__((used));
void __cdecl rtl_life_startup_c(void) {
    uint32_t esp = rtl_life_caller_esp;
    rtl_life_rt.entry = rtl_life_caller;
    es2_rtl_startup(&rtl_life_rt, &rtl_life_env, esp, *(const volatile uint32_t *)(uintptr_t)(esp + 4u));
    __builtin_trap();   /* only if ExitProcess returned */
}
void __cdecl rtl_life_adapter_c(uint32_t which) __asm__("_rtl_life_adapter_c") __attribute__((used));
void __cdecl rtl_life_adapter_c(uint32_t which) {
    uint32_t esp = rtl_life_caller_esp, argument = *(const volatile uint32_t *)(uintptr_t)(esp + 4u);
    rtl_life_rt.entry = rtl_life_caller;   /* the caller's registers at the import */
    if (which == 1u) es2_rtl_quick_exit(&rtl_life_rt, &rtl_life_env, esp, argument);
    else if (which == 2u) es2_rtl_abort(&rtl_life_rt, &rtl_life_env, esp);
    else es2_rtl_error_exit(&rtl_life_rt, &rtl_life_env, esp, argument);
    __builtin_trap();
}
/* _atexit from rtl_replaced_c (the private stack of rtl_imports.h): CW3220's 419070
 * leaves EAX 0, ECX the function and EDX the old count, or EAX 1 and ECX/EDX
 * unchanged for a full table, and its pushed EBP below the return address. */
static int rtl_life_atexit_call(void) {
    uint32_t function = *(const volatile uint32_t *)(uintptr_t)(rtl_call.esp + 4u), count = rtl_life_rt.atexit_count;
    if (es2_rtl_atexit(&rtl_life_rt, function)) {
        ++rtl_life_refused;
        rtl_call.out_eax = 1u; rtl_call.out_ecx = rtl_call.ecx; rtl_call.out_edx = rtl_call.edx;
    } else {
        ++rtl_life_registered;
        rtl_call.out_eax = 0; rtl_call.out_ecx = function; rtl_call.out_edx = count;
    }
    *(volatile uint32_t *)(uintptr_t)(rtl_call.esp - 4u) = rtl_call.ebp;
    return 0;
}

/* ------------------------------------------------------------------------
 * Install (nothing patched yet): the CW3220 image, the Win32 functions, the
 * lifecycle stack and the start state. */
static BOOL rtl_life_prepare(BYTE *cw, rtl_resolver resolve, void *context) {
    if (!cw) return rtl_refuse("cw_image", NULL);
    uint32_t pe = rtl_u32(cw + 0x3c);
    if (cw[0] != 'M' || cw[1] != 'Z' || pe < 64 || pe > 1024 || memcmp(cw + pe, "PE\0\0", 4) ||
        rtl_u32(cw + pe + 24 + 56) != RTL_HEAP_CW_IMAGE) return rtl_refuse("cw_image", NULL);
    uint32_t base = (uint32_t)(uintptr_t)cw;
    for (unsigned k = 0; k < RTL_LIFE_GROUP; ++k) {
        FARPROC address = resolve ? resolve(rtl_life_group[k].symbol, context) : NULL;
        if ((uint32_t)(uintptr_t)address != base + rtl_life_group[k].rva) return rtl_refuse("cw_life_export", rtl_imports[rtl_life_group[k].import].key);
    }
    for (unsigned k = 0; k < sizeof(rtl_life_operands) / sizeof(rtl_life_operands[0]); ++k)
        if (rtl_u32(cw + rtl_life_operands[k][0]) != base + rtl_life_operands[k][1]) return rtl_refuse("cw_life_operand", NULL);
    for (unsigned k = 0; k < ES2_RTL_API_COUNT; ++k) {
        const BYTE *thunk = cw + rtl_life_apis[k].thunk;
        BOOL user = rtl_life_apis[k].slot >= RTL_LIFE_U32_IAT;
        uint32_t lookup = user ? RTL_LIFE_U32_LOOKUP + (rtl_life_apis[k].slot - RTL_LIFE_U32_IAT) :
                                 RTL_LIFE_K32_LOOKUP + (rtl_life_apis[k].slot - RTL_LIFE_K32_IAT);
        uint32_t entry = rtl_u32(cw + lookup);
        if (thunk[0] != 0xff || thunk[1] != 0x25 || rtl_u32(thunk + 2) != base + rtl_life_apis[k].slot || !entry ||
            (entry & 0x80000000u) || entry >= RTL_HEAP_CW_IMAGE - 32u || strcmp((const char *)cw + entry + 2, rtl_life_apis[k].name))
            return rtl_refuse("cw_life_imports", NULL);
        rtl_life_api[k] = rtl_u32(cw + rtl_life_apis[k].slot);
        if (!rtl_life_api[k]) return rtl_refuse("cw_life_imports", NULL);
    }
    if (!rtl_life_stack_base) {
        BYTE *stack = VirtualAlloc(NULL, RTL_LIFE_STACK, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!stack) return rtl_refuse("life_stack", NULL);
        rtl_life_stack_base = (uint32_t)(uintptr_t)stack;
    }
    rtl_life_top = rtl_life_stack_base + RTL_LIFE_STACK - 64u;
    rtl_life_cw = cw;
    es2_rtl_runtime_init(&rtl_life_rt, base + RTL_LIFE_DUMMY_HOOK, base + RTL_LIFE_ABORT_MESSAGE);
    rtl_life_rt.code_base = base;
    rtl_life_rt.stream_hooks = base + RTL_LIFE_STREAM_HOOKS;
    rtl_life_rt.sigabrt_slot = base + RTL_LIFE_SIGABRT;
    rtl_life_rt.sigabrt_extra_slot = base + RTL_LIFE_SIGABRT_EXTRA;
    /* Adopted: main's arguments and anything registered with CW3220 before. */
    rtl_life_rt.argc = rtl_life_cw_get(RTL_LIFE_ARGC);
    rtl_life_rt.argv = rtl_life_cw_get(RTL_LIFE_ARGC + 4u);
    rtl_life_rt.envp = rtl_life_cw_get(RTL_LIFE_ARGC + 8u);
    uint32_t count = rtl_life_cw_get(RTL_LIFE_ATEXIT_COUNT);
    if (count > ES2_RTL_ATEXIT_LIMIT) return rtl_refuse("cw_life_atexit", NULL);
    rtl_life_rt.atexit_count = count;
    for (uint32_t i = 0; i < ES2_RTL_ATEXIT_LIMIT; ++i) rtl_life_rt.atexit_table[i] = rtl_life_cw_get(RTL_LIFE_ATEXIT_TABLE + 4u * i);
    es2_rtl_start_env env = {NULL, rtl_life_read8, rtl_life_write8, rtl_life_read32, rtl_life_env_call, rtl_life_env_fpu,
                             rtl_life_env_frame, rtl_life_env_environment, rtl_life_env_command, rtl_life_env_handle,
                             rtl_life_env_file, rtl_life_env_version, rtl_life_env_thread, rtl_life_env_window,
                             rtl_life_env_box, rtl_life_env_exit, rtl_life_write32, rtl_life_env_site};
    rtl_life_env = env;
    rtl_life_current = 0;
    rtl_life_site = NULL;
    rtl_life_init_records = rtl_life_winmain = rtl_life_registered = rtl_life_refused = rtl_life_atexit_run = 0;
    rtl_life_exit_records = rtl_life_hooks = rtl_life_signals = 0;
    rtl_life_status = -1;
    return TRUE;
}
/* The thunk targets of the slot imports. */
static uint32_t rtl_life_adapter(unsigned import) {
    return import == RTL_I_atexit ? rtl_replaced_table[import] : import == RTL_I_exit ? (uint32_t)(uintptr_t)rtl_life_exit_entry :
           import == RTL_I_abort ? (uint32_t)(uintptr_t)rtl_life_abort_entry : (uint32_t)(uintptr_t)rtl_life_error_entry;
}

/* Fields: every tick the init records and WinMain entries; the exit record
 * adds the atexit table's traffic, the records and hooks run, the signal
 * handler calls, the exit status (-1 before an exit) and CW3220's own
 * atexit count (functions registered there instead of with the own table). */
static int rtl_life_fields(char *out, size_t capacity, BOOL full) {
    int n = snprintf(out, capacity, ",\"rtl_life_init_records\":%llu,\"rtl_life_winmain\":%llu",
                     (unsigned long long)rtl_life_init_records, (unsigned long long)rtl_life_winmain);
    if (n <= 0 || (size_t)n >= capacity) return 0;
    if (!full) return n;
    int added = snprintf(out + n, capacity - (size_t)n,
                         ",\"rtl_life_atexit_registered\":%llu,\"rtl_life_atexit_refused\":%llu,\"rtl_life_atexit_pending\":%lu"
                         ",\"rtl_life_atexit_run\":%llu,\"rtl_life_exit_records\":%llu,\"rtl_life_stream_hooks\":%llu"
                         ",\"rtl_life_signals\":%llu,\"rtl_life_exit_status\":%lld,\"rtl_life_cw_atexit\":%lu",
                         (unsigned long long)rtl_life_registered, (unsigned long long)rtl_life_refused,
                         (unsigned long)rtl_life_rt.atexit_count, (unsigned long long)rtl_life_atexit_run,
                         (unsigned long long)rtl_life_exit_records, (unsigned long long)rtl_life_hooks,
                         (unsigned long long)rtl_life_signals, (long long)rtl_life_status,
                         (unsigned long)(rtl_life_cw ? rtl_life_cw_get(RTL_LIFE_ATEXIT_COUNT) : 0u));
    if (added <= 0 || (size_t)added >= capacity - (size_t)n) return 0;
    return n + added;
}
