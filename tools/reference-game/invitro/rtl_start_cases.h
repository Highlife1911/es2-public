/* The CW3220 runtime lifecycle against core/src/rtl_start.c, in vitro.
 *
 * CW3220.DLL is mapped as in rtl_cases.h (an SHA-256 gate, relocations,
 * no entry point). Its import slots then point to own stubs: the nine Win32
 * functions the start-up and exit paths call (GetEnvironmentStrings,
 * GetCommandLineA, GetModuleHandleA, GetModuleFileNameA, GetVersion,
 * GetCurrentThreadId, EnumThreadWindows, MessageBoxA, ExitProcess) record
 * their arguments, ESP, return address and registers and return scripted
 * values (ECX/EDX fixed per function); every other slot ends the run as a
 * failure. ExitProcess abandons the private stack and returns to the harness
 * (FS:0 and the x87 state restored).
 *
 * Each case builds a synthetic module table (init and exit records with
 * random done flags, priorities with ties, records with function 0, flags
 * for WinMain or main), a command line, initial runtime variables (atexit
 * table, stream hooks, SIGABRT handler) and scripts for the synthetic guest
 * functions: 64 record functions (some register atexit functions or call
 * exit, _exit, _cexit, _c_exit), WinMain (stdcall or cdecl) or main, which
 * register atexit functions and then return or call exit, _exit, _cexit,
 * _c_exit, abort or __ErrorExit through the exports, and a SIGABRT handler.
 * The guest functions return fixed EAX/ECX/EDX per function.
 *
 * Model comparison: the export __startup runs on a private stack (entry ESP =
 * the anchor; [anchor] = 0, [anchor+4] = the table, as DBSIM's entry leaves
 * them). Own run: es2_rtl_startup on the same table with the same scripts; the
 * env calls the same synthetic functions directly and gives them the stack
 * pointer the model predicts. Both runs log every guest call and Win32 call
 * with its arguments, the ESP at the callee's entry relative to the entry of
 * the outermost runtime function (the anchor), the return address there as a
 * CW3220 RVA (the model's return_rva and site), the x87 control word seen by
 * the guest functions, WinMain's x87 status/tags and the SEH record at FS:0;
 * the logs, the table bytes after the run (done flags) and the runtime
 * variables must be equal.
 *
 * Switched comparison: the proxy's lifecycle group (proxy/rtl_life.h) installed
 * on the mapped DBSIM, against the original. Cases are restricted to what DBSIM
 * can call (atexit, __exit, abort and __ErrorExit through DBSIM's import
 * thunks, from WinMain, the record functions, the atexit functions and the
 * handler). The original run takes DBSIM's thunks to the mapped exports; the
 * switched run enters rtl_life_startup_entry as the arming does, with the shim
 * in DBSIM's slots, from the same stack pattern, registers, table and CW3220
 * variables. Equal: the event logs, the general registers at every guest call
 * (all seven) and Win32 call (EBX, ESI, EDI, EBP; ExitProcess all seven), every
 * stack word from the guest's ESP+4 (for ExitProcess its ESP) up to the
 * runtime's entry ([anchor+8]: the frames the model writes), the table bytes
 * and CW3220's variables (the atexit table against the lifecycle's). The
 * guest's own return address is checked to be rtl_life_return in the switched
 * run and the model's return RVA is compared with the original's. Selection:
 * --rtl-start or rtl_start_<mutation>. */
#include "es2/core/rtl_start.h"

/* CW3220 RVAs (link base 0x400000). */
#define LS_GUI 0x23f30u
#define LS_MATHERR 0x21df8u
#define LS_MATHERRL 0x21dfcu
#define LS_FMODE 0x1fdb0u
#define LS_MODULE_COUNT 0x23f24u
#define LS_MODULE_TABLE 0x23f28u
#define LS_FRAME 0x23f0cu
#define LS_ENVIRONMENT 0x23f10u
#define LS_COMMAND 0x23f14u
#define LS_ARGC 0x23f18u
#define LS_ARGV 0x23f1cu
#define LS_ENVP 0x23f20u
#define LS_SECOND_LIST 0x23f04u
#define LS_ATEXIT_COUNT 0x222b8u
#define LS_ATEXIT_TABLE 0x23a64u
#define LS_EXITBUF 0x222bcu
#define LS_EXITFOPEN 0x222c0u
#define LS_EXITOPEN 0x222c4u
#define LS_SIGABRT 0x223a4u
#define LS_SIGABRT_EXTRA 0x223d9u
#define LS_DUMMY_HOOK 0x191a0u
#define LS_ABORT_MESSAGE 0x22298u
#define LS_FPU_DEFAULT_WORD 0x21dc8u
#define LS_GUI_MESSAGE_BYTE 0x1f069u

enum { LS_X_STARTUP, LS_X_EXIT, LS_X_QUICK_EXIT, LS_X_CEXIT, LS_X_C_EXIT, LS_X_ATEXIT, LS_X_ABORT, LS_X_ERROR_EXIT, LS_X_COUNT };
static const struct { const char *name; uint32_t rva; int import; } ls_export_names[LS_X_COUNT] = {
    {"__startup", 0x1a6e0u, RTL_STARTUP_INDEX}, {"_exit", 0x191fcu, -1}, {"__exit", 0x19214u, RTL_I_exit}, {"__cexit", 0x1922cu, -1},
    {"__c_exit", 0x1923cu, -1}, {"_atexit", 0x19070u, RTL_I_atexit}, {"_abort", 0x19060u, RTL_I_abort},
    {"__ErrorExit", 0x18fa4u, RTL_I_ErrorExit},
};
static uint32_t ls_exports[LS_X_COUNT];
static uint32_t ls_cw_base(void) { return (uint32_t)(uintptr_t)rtl_cw; }
static volatile uint32_t *ls_var(uint32_t rva) { return (volatile uint32_t *)(uintptr_t)(ls_cw_base() + rva); }

/* What runs: the export __startup, the model with the direct env, or the
 * proxy's lifecycle group. ls_rich: the switched comparison (services
 * through DBSIM's thunks, frames captured), in both of its runs. */
enum { LS_ORIGINAL, LS_MODEL, LS_SWITCHED };
static unsigned ls_mode;
static int ls_rich;

/* ------------------------------------------------------------------------
 * Event log of the run in progress, then copied per side (0 original, 1
 * own); one buffer during every run, so that pointers into it are the same
 * in both runs of the switched comparison. */
enum {
    LS_EV_VOID = 1, LS_EV_WINMAIN, LS_EV_FPU, LS_EV_FRAME, LS_EV_MAIN, LS_EV_ATEXIT_RESULT, LS_EV_SIGNAL,
    LS_EV_ENV, LS_EV_CMD, LS_EV_HANDLE, LS_EV_FILE, LS_EV_VERSION, LS_EV_THREAD, LS_EV_ENUM, LS_EV_BOX, LS_EV_EXIT
};
typedef struct ls_event { uint32_t kind, id, a0, a1, a2, a3, esp, fpu, ret; char text[96]; } ls_event;
#define LS_EVENTS 1536u
static ls_event ls_cur[LS_EVENTS], ls_log[2][LS_EVENTS];
static unsigned ls_cur_count, ls_log_count[2];
static ls_event *ls_add(uint32_t kind) {
    static ls_event spare;
    if (ls_cur_count >= LS_EVENTS) { ++ls_cur_count; memset(&spare, 0, sizeof(spare)); return &spare; }
    ls_event *e = &ls_cur[ls_cur_count++];
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    return e;
}
static uint16_t ls_fpu_cw(void) { uint16_t w; __asm__ volatile("fnstcw %0" : "=m"(w)); return w; }

/* Frames of the switched comparison: the registers at a callee's entry and
 * the stack words from its ESP+4 (ExitProcess: its ESP) up to [anchor+8].
 * Win32 calls: all registers for the start-up's three and
 * GetModuleFileNameA (the model defines them there), EBX/ESI/EDI/EBP for the
 * rest of __ErrorMessage's (after CW3220's strrchr). */
enum { LS_FRAME_GUEST, LS_FRAME_API, LS_FRAME_API_FULL, LS_FRAME_EXIT };
typedef struct ls_frame { uint32_t event, kind, regs[7], esp, first, count; } ls_frame;
#define LS_FRAMES 1536u
#define LS_WORDS (1u << 19)
static ls_frame ls_cur_frames[LS_FRAMES], ls_frames[2][LS_FRAMES];
static uint32_t ls_cur_words[LS_WORDS], ls_words[2][LS_WORDS];
static unsigned ls_cur_frame_count, ls_cur_word_count, ls_frame_count[2], ls_word_count[2];
static unsigned ls_bad_return;    /* a switched callee whose [ESP] is not rtl_life_return */

/* Shared with the assembly below. */
uint32_t ls_entry_esp, ls_entry_id, ls_anchor, ls_host_esp, ls_host_fs0, ls_run_target, ls_api_id, ls_ret_ecx, ls_ret_edx;
uint32_t ls_regs[7], ls_entry_regs[7];   /* EAX ECX EDX EBX ESI EDI EBP */
extern void __cdecl ls_run(uint32_t target, uint32_t top, uint32_t argument) __asm__("_ls_run");
extern void __cdecl ls_escape(void) __asm__("_ls_escape") __attribute__((noreturn));
extern uint32_t __cdecl ls_invoke(uint32_t target, uint32_t argument) __asm__("_ls_invoke");
static uint32_t ls_rel(uint32_t esp) { return esp - ls_anchor; }

/* The own cw3220.dll's run of the switched comparison (rtl_own_cases.h) sets
 * its trampoline's return address, where its model keeps the return RVA, and a
 * map of its own addresses (frame handler, window callback, abort text, dummy
 * hook) to CW3220's for the words and registers it captures. */
static uint32_t ls_switched_return;                    /* 0: rtl_life_return */
static const volatile uint32_t *ls_switched_rva;       /* NULL: rtl_life_return_rva */
static uint32_t (*ls_translate)(uint32_t word);        /* NULL: none */
static uint32_t ls_word(uint32_t word) { return ls_translate ? ls_translate(word) : word; }

static void ls_capture(unsigned kind, const uint32_t *regs, uint32_t esp) {
    if (!ls_rich) return;
    if (ls_cur_frame_count >= LS_FRAMES) { ++ls_cur_frame_count; return; }
    ls_frame *f = &ls_cur_frames[ls_cur_frame_count++];
    f->event = ls_cur_count - 1u;
    f->kind = kind;
    for (unsigned r = 0; r < 7; ++r) f->regs[r] = ls_word(regs[r]);
    f->esp = esp;
    f->first = ls_cur_word_count;
    if (kind == LS_FRAME_GUEST || kind == LS_FRAME_EXIT)
        for (uint32_t a = kind == LS_FRAME_EXIT ? esp : esp + 4u; a < ls_anchor + 8u; a += 4u) {
            if (ls_cur_word_count < LS_WORDS) ls_cur_words[ls_cur_word_count] = ls_word(*(const volatile uint32_t *)(uintptr_t)a);
            ++ls_cur_word_count;
        }
    f->count = ls_cur_word_count - f->first;
}
/* The return address at a callee's entry as a CW3220 RVA: the model's in
 * the direct run (model_rva), the original's [esp], and in the switched run
 * the model's after checking that [esp] returns into the lifecycle's
 * trampoline (ExitProcess is entered with the original's word in place). */
static uint32_t ls_model_rva;
static uint32_t ls_return_rva(uint32_t esp, int exit_process) {
    if (ls_mode == LS_MODEL) return ls_model_rva;
    uint32_t word = *(const volatile uint32_t *)(uintptr_t)esp;
    if (ls_mode == LS_SWITCHED && !exit_process) {
        if (word != (ls_switched_return ? ls_switched_return : (uint32_t)(uintptr_t)rtl_life_return)) ++ls_bad_return;
        return ls_switched_rva ? *ls_switched_rva : rtl_life_return_rva;
    }
    return word - ls_cw_base();
}

/* ------------------------------------------------------------------------
 * A case: the module table, the scripts and the scripted Win32 answers. */
#define LS_VOIDS 64u
enum { LS_ACT_NONE, LS_ACT_ATEXIT, LS_ACT_EXIT, LS_ACT_QUICK_EXIT, LS_ACT_CEXIT, LS_ACT_C_EXIT, LS_ACT_ABORT, LS_ACT_ERROR_EXIT };
enum { LS_FIN_RETURN, LS_FIN_EXIT, LS_FIN_QUICK_EXIT, LS_FIN_CEXIT, LS_FIN_C_EXIT, LS_FIN_ABORT, LS_FIN_ERROR_EXIT, LS_FIN_COUNT };
enum { LS_MAIN_STDCALL, LS_MAIN_CDECL, LS_MAIN_CONSOLE };
#define LS_RECORDS 14u
typedef struct ls_case {
    uint32_t flags;
    unsigned main_kind;
    unsigned init_count, exit_count;
    uint8_t init_done[LS_RECORDS], init_priority[LS_RECORDS], exit_done[LS_RECORDS], exit_priority[LS_RECORDS];
    int init_id[LS_RECORDS], exit_id[LS_RECORDS];        /* -1: function 0 */
    uint8_t action[LS_VOIDS];
    uint32_t action_arg[LS_VOIDS];
    unsigned registrations;
    uint8_t registration_id[40];
    unsigned final;
    uint32_t code;
    char command[128];
    uint32_t environment, instance, matherr, matherrl, fmode, argc, argv, envp;
    uint32_t atexit_count, atexit_table[ES2_RTL_ATEXIT_LIMIT];
    int hook_id[3];                                       /* -1: CW3220's dummy */
    uint32_t sigabrt_kind;                                /* 0 default, 1 ignore, 2 handler */
    uint8_t sigabrt_extra;
    uint32_t version, thread, has_window;
    char file_name[112];
    uint32_t entry_regs[7];                               /* DBSIM's entry registers */
} ls_case;
static ls_case ls_c;
static uint8_t ls_remaining[LS_VOIDS];
static const char ls_error_text[] = "H6-5 synthetic error message";

/* Guest memory of a case: the table at +0, init records at +0x100, exit
 * records at +0x300, the command line at +0x800. */
static uint8_t ls_memory[0x1000] __attribute__((aligned(16)));
static uint8_t ls_memory_after[2][0x1000];
#define LS_TABLE ((uint32_t)(uintptr_t)ls_memory)
#define LS_INIT (LS_TABLE + 0x100u)
#define LS_EXITS (LS_TABLE + 0x300u)
#define LS_COMMAND_LINE (LS_TABLE + 0x800u)

/* ------------------------------------------------------------------------
 * Synthetic guest functions and Win32 stubs. Each thunk records ESP and the
 * registers at entry, runs its C body and returns fixed EAX/ECX/EDX (the
 * runtime passes them on to the next callee); the own run of the model
 * comparison calls the bodies directly with ls_entry_esp set. */
#define LS_T(e) ".balign 32\n\tmovl $(" e "), _ls_entry_id\n\tjmp _ls_void_common\n"
#define LS_T8(h) LS_T(#h "*8+0") LS_T(#h "*8+1") LS_T(#h "*8+2") LS_T(#h "*8+3") LS_T(#h "*8+4") LS_T(#h "*8+5") LS_T(#h "*8+6") LS_T(#h "*8+7")
#define LS_SAVE "\tmovl %esp, _ls_entry_esp\n\tmovl %eax, _ls_regs\n\tmovl %ecx, _ls_regs+4\n\tmovl %edx, _ls_regs+8\n" \
                "\tmovl %ebx, _ls_regs+12\n\tmovl %esi, _ls_regs+16\n\tmovl %edi, _ls_regs+20\n\tmovl %ebp, _ls_regs+24\n"
/* Win32 stub: id, argument count, C body (stdcall). */
#define LS_API(name, id, k, copies) ".balign 16\n_ls_api_" name ":\n" LS_SAVE "\tmovl $" #id ", _ls_api_id\n" copies \
    "\tcall _ls_os_" name "_c\n\tmovl $(0xec000000+" #id "), %ecx\n\tmovl $(0xed000000+" #id "), %edx\n\tret $" #k "\n"
#define LS_COPY1(o) "\tpushl " o "(%esp)\n"
__asm__(
    ".text\n"
    ".balign 32\n"
    "_ls_void_thunks:\n"
    LS_T8(0) LS_T8(1) LS_T8(2) LS_T8(3) LS_T8(4) LS_T8(5) LS_T8(6) LS_T8(7)
    ".balign 16\n"
    "_ls_void_common:\n" LS_SAVE
    "\tpushl _ls_entry_id\n\tcall _ls_void_c\n\tpopl %ecx\n"
    "\tmovl %ecx, %eax\n\torl $0xea000000, %eax\n\tmovl %ecx, %edx\n\torl $0xed000000, %edx\n\torl $0xec000000, %ecx\n\tret\n"
    ".balign 32\n"
    "_ls_winmain_std_thunk:\n" LS_SAVE LS_COPY1("16") LS_COPY1("16") LS_COPY1("16") LS_COPY1("16")
    "\tcall _ls_winmain_std_c\n\tmovl $0xec5eec5e, %ecx\n\tmovl $0xed5eed5e, %edx\n\tret $16\n"
    ".balign 16\n"
    "_ls_winmain_cdecl_thunk:\n" LS_SAVE LS_COPY1("16") LS_COPY1("16") LS_COPY1("16") LS_COPY1("16")
    "\tcall _ls_winmain_cdecl_c\n\taddl $16, %esp\n\tmovl $0xec5ecdec, %ecx\n\tmovl $0xed5ecded, %edx\n\tret\n"
    ".balign 16\n"
    "_ls_main_thunk:\n" LS_SAVE LS_COPY1("12") LS_COPY1("12") LS_COPY1("12")
    "\tcall _ls_main_c\n\taddl $12, %esp\n\tmovl $0xec3a1dec, %ecx\n\tmovl $0xed3a1ded, %edx\n\tret\n"
    ".balign 16\n"
    "_ls_signal_thunk:\n" LS_SAVE LS_COPY1("8") LS_COPY1("8")
    "\tcall _ls_signal_c\n\taddl $8, %esp\n\tmovl $0xea51a5ea, %eax\n\tmovl $0xec51a5ec, %ecx\n\tmovl $0xed51a5ed, %edx\n\tret\n"
    LS_API("environment", 1, 0, "")
    LS_API("command", 2, 0, "")
    LS_API("handle", 3, 4, LS_COPY1("4"))
    LS_API("file", 4, 12, LS_COPY1("12") LS_COPY1("12") LS_COPY1("12"))
    LS_API("version", 5, 0, "")
    LS_API("thread", 6, 0, "")
    LS_API("enum", 7, 12, LS_COPY1("12") LS_COPY1("12") LS_COPY1("12"))
    LS_API("box", 8, 16, LS_COPY1("16") LS_COPY1("16") LS_COPY1("16") LS_COPY1("16"))
    ".balign 16\n_ls_api_exit:\n" LS_SAVE "\tmovl $9, _ls_api_id\n\tjmp _ls_os_exit_c\n"
    /* ls_run(target, top, argument): the target on a private stack with
     * [esp] = 0, [esp+4] = argument and the case's entry registers, like
     * DBSIM's entry jump. */
    ".balign 16\n"
    "_ls_run:\n"
    "\tpushl %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
    "\tmovl %esp, _ls_host_esp\n"
    "\tmovl %fs:0, %eax\n\tmovl %eax, _ls_host_fs0\n"
    "\tmovl 20(%esp), %eax\n\tmovl %eax, _ls_run_target\n\tmovl 28(%esp), %ecx\n\tmovl 24(%esp), %edx\n"
    "\tmovl %edx, %esp\n"
    "\tpushl %ecx\n\tpushl $0\n"
    "\tmovl %esp, _ls_anchor\n"
    "\tmovl _ls_entry_regs, %eax\n\tmovl _ls_entry_regs+4, %ecx\n\tmovl _ls_entry_regs+8, %edx\n\tmovl _ls_entry_regs+12, %ebx\n"
    "\tmovl _ls_entry_regs+16, %esi\n\tmovl _ls_entry_regs+20, %edi\n\tmovl _ls_entry_regs+24, %ebp\n"
    "\tcld\n"
    "\tjmp *_ls_run_target\n"
    /* ls_escape(): back to ls_run's caller from anywhere on the private stack. */
    ".balign 16\n"
    "_ls_escape:\n"
    "\tmovl _ls_host_fs0, %eax\n\tmovl %eax, %fs:0\n"
    "\tmovl _ls_host_esp, %esp\n"
    "\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tcld\n\tret\n"
    /* ls_invoke(target, argument): calls an export with one argument; the
     * anchor becomes the export's entry ESP; ECX/EDX after the call kept. */
    ".balign 16\n"
    "_ls_invoke:\n"
    "\tmovl 4(%esp), %eax\n\tmovl 8(%esp), %ecx\n"
    "\tpushl %ecx\n"
    "\tleal -4(%esp), %edx\n\tmovl %edx, _ls_anchor\n"
    "\tcall *%eax\n"
    "\tmovl %ecx, _ls_ret_ecx\n\tmovl %edx, _ls_ret_edx\n"
    "\taddl $4, %esp\n\tret\n");
extern const uint8_t ls_void_thunks[] __asm__("_ls_void_thunks");
extern const uint8_t ls_winmain_std_thunk[] __asm__("_ls_winmain_std_thunk");
extern const uint8_t ls_winmain_cdecl_thunk[] __asm__("_ls_winmain_cdecl_thunk");
extern const uint8_t ls_main_thunk[] __asm__("_ls_main_thunk");
extern const uint8_t ls_signal_thunk[] __asm__("_ls_signal_thunk");
extern const uint8_t ls_api_environment[] __asm__("_ls_api_environment");
extern const uint8_t ls_api_command[] __asm__("_ls_api_command");
extern const uint8_t ls_api_handle[] __asm__("_ls_api_handle");
extern const uint8_t ls_api_file[] __asm__("_ls_api_file");
extern const uint8_t ls_api_version[] __asm__("_ls_api_version");
extern const uint8_t ls_api_thread[] __asm__("_ls_api_thread");
extern const uint8_t ls_api_enum[] __asm__("_ls_api_enum");
extern const uint8_t ls_api_box[] __asm__("_ls_api_box");
extern const uint8_t ls_api_exit[] __asm__("_ls_api_exit");
static uint32_t ls_void_address(unsigned id) { return (uint32_t)(uintptr_t)ls_void_thunks + 32u * id; }
#define LS_ADDR(x) ((uint32_t)(uintptr_t)(x))

/* Services: the exports in the original run of the model comparison, the
 * model in its own run; DBSIM's import thunks in both runs of the switched
 * comparison. */
static es2_rtl_runtime ls_model;
static es2_rtl_start_env ls_env;
static unsigned ls_nested_exits, ls_nested_registrations, ls_full_tables, ls_kind_calls[9];
static uint32_t ls_residue;
static uint32_t ls_dbsim_thunk(int import) { return (uint32_t)(uintptr_t)mapped + RTL_THUNK_RVA + 6u * (uint32_t)import; }
static uint32_t ls_service(unsigned which, uint32_t argument) {
    if (ls_mode == LS_MODEL) {
        switch (which) {
        case LS_X_ATEXIT: return es2_rtl_atexit(&ls_model, argument);
        case LS_X_EXIT: es2_rtl_exit(&ls_model, &ls_env, 0, argument); return 0;
        case LS_X_QUICK_EXIT: es2_rtl_quick_exit(&ls_model, &ls_env, 0, argument); return 0;
        case LS_X_CEXIT: es2_rtl_cexit(&ls_model, &ls_env, 0); return 0;
        case LS_X_C_EXIT: es2_rtl_c_exit(&ls_model, &ls_env, 0); return 0;
        case LS_X_ABORT: es2_rtl_abort(&ls_model, &ls_env, 0); return 0;
        case LS_X_ERROR_EXIT: es2_rtl_error_exit(&ls_model, &ls_env, 0, argument); return 0;
        }
        return 0;
    }
    uint32_t target = ls_exports[which];
    if (ls_rich) {
        if (ls_export_names[which].import < 0) { fprintf(stderr, "FAIL: %s: DBSIM has no import %s\n", current_function, ls_export_names[which].name); ls_escape(); }
        target = ls_dbsim_thunk(ls_export_names[which].import);
    }
    uint32_t saved = ls_anchor, result = ls_invoke(target, argument);
    ls_residue = *(const volatile uint32_t *)(uintptr_t)(ls_anchor - 4u);   /* below the import's return address */
    ls_anchor = saved;
    return result;
}
/* atexit's result: EAX, and in the switched comparison ECX, EDX and the
 * word its frame leaves below the return address. */
static void ls_atexit_result(uint32_t eax) {
    ls_event *e = ls_add(LS_EV_ATEXIT_RESULT);
    e->a0 = eax;
    if (ls_rich) { e->a1 = ls_ret_ecx; e->a2 = ls_ret_edx; e->a3 = ls_residue; }
}
static void ls_perform(unsigned action, uint32_t argument) {
    int original = ls_mode == LS_ORIGINAL;
    switch (action) {
    case LS_ACT_ATEXIT: {
        uint32_t r = ls_service(LS_X_ATEXIT, ls_void_address(argument));
        ls_atexit_result(r);
        if (original) { ++ls_nested_registrations; if (r) ++ls_full_tables; }
        break;
    }
    case LS_ACT_EXIT: if (original) ++ls_nested_exits; ls_service(LS_X_EXIT, argument); break;
    case LS_ACT_QUICK_EXIT: if (original) ++ls_nested_exits; ls_service(LS_X_QUICK_EXIT, argument); break;
    case LS_ACT_CEXIT: if (original) ++ls_nested_exits; ls_service(LS_X_CEXIT, 0); break;
    case LS_ACT_C_EXIT: if (original) ++ls_nested_exits; ls_service(LS_X_C_EXIT, 0); break;
    case LS_ACT_ABORT: if (original) ++ls_nested_exits; ls_service(LS_X_ABORT, 0); break;
    case LS_ACT_ERROR_EXIT: if (original) ++ls_nested_exits; ls_service(LS_X_ERROR_EXIT, LS_ADDR(ls_error_text)); break;
    }
}
void __cdecl ls_void_c(void) __asm__("_ls_void_c");
void __cdecl ls_void_c(void) {
    uint32_t esp = ls_entry_esp, id = ls_entry_id, regs[7];
    memcpy(regs, ls_regs, sizeof(regs));
    uint32_t ret = ls_return_rva(esp, 0);
    ls_event *e = ls_add(LS_EV_VOID);
    e->id = id; e->esp = ls_rel(esp); e->fpu = ls_fpu_cw(); e->ret = ret;
    ls_capture(LS_FRAME_GUEST, regs, esp);
    if (id < LS_VOIDS && ls_remaining[id]) { --ls_remaining[id]; ls_perform(ls_c.action[id], ls_c.action_arg[id]); }
}
/* WinMain and main: log, then the script. */
static uint32_t ls_own_frame, ls_own_handler;
static uint32_t ls_entry_body(uint32_t kind, uint32_t esp, const uint32_t *regs, const uint32_t *args, unsigned count) {
    uint32_t ret = ls_return_rva(esp, 0);
    ls_event *e = ls_add(kind);
    e->esp = ls_rel(esp); e->fpu = ls_fpu_cw(); e->ret = ret;
    e->a0 = args[0]; e->a1 = args[1]; e->a2 = args[2]; e->a3 = count > 3 ? args[3] : 0;
    if (kind == LS_EV_WINMAIN && count == 4) {
        /* Command line pointer relative to the buffer, as both runs use it. */
        e->a2 = args[2] - LS_COMMAND_LINE;
    }
    ls_capture(LS_FRAME_GUEST, regs, esp);
    uint8_t environment[28];
    __asm__ volatile("fnstenv %0\n\tfldenv %0" : "+m"(environment));
    ls_event *f = ls_add(LS_EV_FPU);
    f->a0 = (uint32_t)environment[4] | (uint32_t)environment[5] << 8;   /* status word */
    f->a1 = (uint32_t)environment[8] | (uint32_t)environment[9] << 8;   /* tag word */
    ls_event *r = ls_add(LS_EV_FRAME);
    if (ls_mode == LS_MODEL) { r->a0 = ls_own_frame; r->a1 = 1; r->a2 = ls_own_handler; }
    else {
        uint32_t head;
        __asm__ volatile("movl %%fs:0, %0" : "=r"(head));
        r->a0 = ls_rel(head);
        r->a1 = *(const uint32_t *)(uintptr_t)head == ls_host_fs0;
        r->a2 = ls_word(*(const uint32_t *)(uintptr_t)(head + 4u)) - ls_cw_base();
    }
    for (unsigned i = 0; i < ls_c.registrations; ++i) {
        uint32_t result = ls_service(LS_X_ATEXIT, ls_void_address(ls_c.registration_id[i]));
        ls_atexit_result(result);
        if (ls_mode == LS_ORIGINAL && result) ++ls_full_tables;
    }
    switch (ls_c.final) {
    case LS_FIN_EXIT: ls_service(LS_X_EXIT, ls_c.code); break;
    case LS_FIN_QUICK_EXIT: ls_service(LS_X_QUICK_EXIT, ls_c.code); break;
    case LS_FIN_CEXIT: ls_service(LS_X_CEXIT, 0); break;
    case LS_FIN_C_EXIT: ls_service(LS_X_C_EXIT, 0); break;
    case LS_FIN_ABORT: ls_service(LS_X_ABORT, 0); break;
    case LS_FIN_ERROR_EXIT: ls_service(LS_X_ERROR_EXIT, LS_ADDR(ls_error_text)); break;
    }
    return ls_c.code;
}
uint32_t __stdcall ls_winmain_std(uint32_t a, uint32_t b, uint32_t c, uint32_t d) __asm__("_ls_winmain_std_c");
uint32_t __stdcall ls_winmain_std(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t esp = ls_entry_esp, args[4] = {a, b, c, d}, regs[7];
    memcpy(regs, ls_regs, sizeof(regs));
    return ls_entry_body(LS_EV_WINMAIN, esp, regs, args, 4);
}
uint32_t __cdecl ls_winmain_cdecl(uint32_t a, uint32_t b, uint32_t c, uint32_t d) __asm__("_ls_winmain_cdecl_c");
uint32_t __cdecl ls_winmain_cdecl(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t esp = ls_entry_esp, args[4] = {a, b, c, d}, regs[7];
    memcpy(regs, ls_regs, sizeof(regs));
    return ls_entry_body(LS_EV_WINMAIN, esp, regs, args, 4);
}
uint32_t __cdecl ls_main(uint32_t a, uint32_t b, uint32_t c) __asm__("_ls_main_c");
uint32_t __cdecl ls_main(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t esp = ls_entry_esp, args[4] = {a, b, c, 0}, regs[7];
    memcpy(regs, ls_regs, sizeof(regs));
    return ls_entry_body(LS_EV_MAIN, esp, regs, args, 3);
}
void __cdecl ls_signal(uint32_t sig, uint32_t extra) __asm__("_ls_signal_c");
void __cdecl ls_signal(uint32_t sig, uint32_t extra) {
    uint32_t esp = ls_entry_esp, regs[7];
    memcpy(regs, ls_regs, sizeof(regs));
    uint32_t ret = ls_return_rva(esp, 0);
    ls_event *e = ls_add(LS_EV_SIGNAL);
    e->esp = ls_rel(esp); e->a0 = sig; e->a1 = ls_c.sigabrt_extra ? extra : 0; e->ret = ret;
    ls_capture(LS_FRAME_GUEST, regs, esp);
}

/* ------------------------------------------------------------------------
 * Win32 stubs (original and switched runs, through the thunks above) and
 * the own run's platform callbacks (model comparison): the same events,
 * the same answers. A stub's event carries the ESP and return address of
 * the call; the model's are those of its site. */
static uint32_t ls_unexpected;
static es2_rtl_site ls_model_site;
static ls_event *ls_api_event(uint32_t kind, int exit_process) {
    uint32_t regs[7], esp = ls_entry_esp;
    memcpy(regs, ls_regs, sizeof(regs));
    uint32_t ret = ls_mode == LS_MODEL ? ls_model_site.return_rva : ls_return_rva(esp, exit_process);
    ls_event *e = ls_add(kind);
    e->esp = ls_mode == LS_MODEL ? ls_model_site.esp : ls_rel(esp);
    e->ret = ret;
    unsigned frame = exit_process ? LS_FRAME_EXIT :
                     kind == LS_EV_ENV || kind == LS_EV_CMD || kind == LS_EV_HANDLE || kind == LS_EV_FILE ? LS_FRAME_API_FULL : LS_FRAME_API;
    if (ls_mode != LS_MODEL) ls_capture(frame, regs, esp);
    return e;
}
static void ls_log_file(char *buffer, uint32_t module, uint32_t size) {
    ls_event *e = ls_api_event(LS_EV_FILE, 0);
    e->a0 = module; e->a1 = size;
    size_t n = strlen(ls_c.file_name);
    if (size && n >= size) n = size - 1u;
    if (size) { memcpy(buffer, ls_c.file_name, n); buffer[n] = 0; }
}
static uint32_t ls_window_answer(uint32_t thread, uint32_t proc_rva) {
    ls_event *e = ls_api_event(LS_EV_ENUM, 0);
    e->a0 = thread; e->a1 = proc_rva;
    return ls_c.has_window;
}
static void ls_log_box(uint32_t window, uint32_t text, const char *title, uint32_t style) {
    ls_event *e = ls_api_event(LS_EV_BOX, 0);
    e->a0 = window; e->a1 = text; e->a2 = style;
    snprintf(e->text, sizeof(e->text), "%s", title);
}
LPCH WINAPI ls_os_environment(void) __asm__("_ls_os_environment_c");
LPCH WINAPI ls_os_environment(void) { ls_api_event(LS_EV_ENV, 0); return (LPCH)(uintptr_t)ls_c.environment; }
LPSTR WINAPI ls_os_command(void) __asm__("_ls_os_command_c");
LPSTR WINAPI ls_os_command(void) { ls_api_event(LS_EV_CMD, 0); return (LPSTR)(uintptr_t)LS_COMMAND_LINE; }
HMODULE WINAPI ls_os_handle(LPCSTR name) __asm__("_ls_os_handle_c");
HMODULE WINAPI ls_os_handle(LPCSTR name) { ls_api_event(LS_EV_HANDLE, 0)->a0 = LS_ADDR(name); return (HMODULE)(uintptr_t)ls_c.instance; }
DWORD WINAPI ls_os_file(HMODULE module, LPSTR buffer, DWORD size) __asm__("_ls_os_file_c");
DWORD WINAPI ls_os_file(HMODULE module, LPSTR buffer, DWORD size) {
    ls_log_file(buffer, LS_ADDR(module), size);
    return (DWORD)strlen(buffer);
}
DWORD WINAPI ls_os_version(void) __asm__("_ls_os_version_c");
DWORD WINAPI ls_os_version(void) { ls_api_event(LS_EV_VERSION, 0); return ls_c.version; }
DWORD WINAPI ls_os_thread(void) __asm__("_ls_os_thread_c");
DWORD WINAPI ls_os_thread(void) { ls_api_event(LS_EV_THREAD, 0); return ls_c.thread; }
BOOL WINAPI ls_os_enum(DWORD thread, WNDENUMPROC proc, LPARAM parameter) __asm__("_ls_os_enum_c");
BOOL WINAPI ls_os_enum(DWORD thread, WNDENUMPROC proc, LPARAM parameter) {
    if (ls_window_answer(thread, ls_word(LS_ADDR(proc)) - ls_cw_base())) { proc((HWND)(uintptr_t)0x10010u, parameter); return FALSE; }
    return TRUE;
}
int WINAPI ls_os_box(HWND window, LPCSTR text, LPCSTR title, UINT style) __asm__("_ls_os_box_c");
int WINAPI ls_os_box(HWND window, LPCSTR text, LPCSTR title, UINT style) {
    ls_log_box(LS_ADDR(window), ls_word(LS_ADDR(text)), title, style);
    return IDOK;
}
void WINAPI ls_os_exit(UINT status) __asm__("_ls_os_exit_c");
void WINAPI ls_os_exit(UINT status) {
    ls_api_event(LS_EV_EXIT, 1)->a0 = status;
    ls_escape();
}
void __cdecl ls_unexpected_import(uint32_t id) __asm__("_ls_unexpected_import");
void __cdecl ls_unexpected_import(uint32_t id) {
    ls_unexpected = id + 1u;
    ls_escape();
}

/* The own run's env (model comparison). */
static jmp_buf ls_own_escape;
static uint32_t ls_bad_target;
static uint8_t ls_env_read8(void *c, uint32_t a) { (void)c; return *(const volatile uint8_t *)(uintptr_t)a; }
static void ls_env_write8(void *c, uint32_t a, uint8_t v) { (void)c; *(volatile uint8_t *)(uintptr_t)a = v; }
static uint32_t ls_env_read32(void *c, uint32_t a) { (void)c; return *(const volatile uint32_t *)(uintptr_t)a; }
static void ls_env_call(void *c, es2_rtl_guest_call *k) {
    (void)c;
    uint32_t t = k->target;
    k->result = 0; k->popped = 0;
    if ((unsigned)k->kind < 9u) ++ls_kind_calls[k->kind];
    ls_entry_esp = k->esp;
    ls_model_rva = k->return_rva;
    if (t == LS_ADDR(ls_winmain_std_thunk)) { k->result = ls_winmain_std(k->args[0], k->args[1], k->args[2], k->args[3]); k->popped = 16; }
    else if (t == LS_ADDR(ls_winmain_cdecl_thunk)) k->result = ls_winmain_cdecl(k->args[0], k->args[1], k->args[2], k->args[3]);
    else if (t == LS_ADDR(ls_main_thunk)) k->result = ls_main(k->args[0], k->args[1], k->args[2]);
    else if (t == LS_ADDR(ls_signal_thunk)) ls_signal(k->args[0], k->arg_count > 1 ? k->args[1] : 0);
    else if (t - ls_void_address(0) < 32u * LS_VOIDS && (t - ls_void_address(0)) % 32u == 0) { ls_entry_id = (t - ls_void_address(0)) / 32u; ls_void_c(); }
    else if (t == ls_cw_base() + LS_DUMMY_HOOK) { /* CW3220's bare ret */ }
    else ls_bad_target = t ? t : 1u;
}
static void ls_env_site(void *c, es2_rtl_site *s) { (void)c; ls_model_site = *s; }
static void ls_env_fpu(void *c, uint16_t w) { (void)c; __asm__ volatile("fninit\n\tfldcw %0" ::"m"(w)); }
static void ls_env_frame(void *c, uint32_t record, uint32_t handler) { (void)c; ls_own_frame = record; ls_own_handler = handler; }
static uint32_t ls_env_environment(void *c) { (void)c; return LS_ADDR(ls_os_environment()); }
static uint32_t ls_env_command(void *c) { (void)c; return LS_ADDR(ls_os_command()); }
static uint32_t ls_env_handle(void *c, uint32_t name) { (void)c; return LS_ADDR(ls_os_handle((LPCSTR)(uintptr_t)name)); }
static uint32_t ls_env_file(void *c, char *buffer, uint32_t size) { (void)c; ls_log_file(buffer, 0, size); return (uint32_t)strlen(buffer); }
static uint32_t ls_env_version(void *c) { (void)c; return ls_os_version(); }
static uint32_t ls_env_thread(void *c) { (void)c; return ls_os_thread(); }
static int ls_env_window(void *c, uint32_t thread) { (void)c; return (int)ls_window_answer(thread, ES2_RTL_ENUM_PROC_RVA); }
static void ls_env_box(void *c, uint32_t text, const char *title, uint32_t style, uint32_t esp) {
    (void)c; (void)esp;
    ls_log_box(0, text, title, style);
}
static void ls_env_exit(void *c, uint32_t status, uint32_t esp) {
    (void)c; (void)esp;
    ls_api_event(LS_EV_EXIT, 1)->a0 = status;
    longjmp(ls_own_escape, 1);
}

/* ------------------------------------------------------------------------
 * Generation. The switched comparison draws its own cases: only what DBSIM
 * can call (atexit, __exit, abort and __ErrorExit), also from the record
 * functions and the atexit functions. */
static int ls_switched_cases;
static uint32_t ls_pick(const uint32_t *values, unsigned count) { return values[random32() % count]; }
static void ls_command(char *out, unsigned n) {
    char *p = out;
    if (n % 41u == 7u) { *p = 0; return; }
    static const char blanks[] = " \t";
    for (unsigned i = random32() % 4u; i; --i) *p++ = blanks[random32() % 2u];
    unsigned style = random32() % 10u;
    if (style < 4u) {                      /* quoted name */
        *p++ = '"';
        for (unsigned i = random32() % 14u; i; --i) {
            unsigned r = random32() % 20u;
            *p++ = r == 0u ? '\t' : r < 4u ? ' ' : (char)('A' + random32() % 26u);
        }
        if (random32() % 5u) *p++ = '"';
    } else {                               /* plain name, sometimes with a quote inside */
        for (unsigned i = random32() % 14u; i; --i) {
            unsigned r = random32() % 25u;
            *p++ = r == 0u ? '"' : r == 1u ? ':' : (char)('a' + random32() % 26u);
        }
    }
    for (unsigned i = random32() % 4u; i; --i) *p++ = blanks[random32() % 2u];
    for (unsigned i = random32() % 24u; i; --i) {
        unsigned r = random32() % 16u;
        *p++ = r == 0u ? '\t' : r == 1u ? ' ' : r == 2u ? '"' : (char)('!' + random32() % 90u);
    }
    *p = 0;
}
static void ls_file_name(char *out, unsigned n) {
    static const char *const fixed[] = {"C:\\GAMES\\ES2\\DBSIM.EXE", "D:DBSIM.EXE", "DBSIM.EXE", "A:\\B:C", "x:y:z", "\\", ":", ""};
    if (n % 3u) { snprintf(out, 112, "%s", fixed[random32() % 8u]); return; }
    unsigned length = 1u + random32() % 100u;
    for (unsigned i = 0; i < length; ++i) {
        unsigned r = random32() % 12u;
        out[i] = r == 0u ? '\\' : r == 1u ? ':' : (char)('A' + random32() % 26u);
    }
    out[length] = 0;
}
static void ls_generate(unsigned n) {
    static const uint32_t priorities[] = {0, 1, 0x10, 0x1f, 0x20, 0x20, 0x20, 0x20, 0x7f, 0xff};
    ls_case *c = &ls_c;
    memset(c, 0, sizeof(*c));
    c->flags = (random32() & ~1u) | (random32() % 10u < 7u ? 1u : 0u);
    c->main_kind = (c->flags & 1u) ? (random32() % 10u < 7u ? LS_MAIN_STDCALL : LS_MAIN_CDECL) : LS_MAIN_CONSOLE;
    c->init_count = n % 17u == 3u ? 0u : random32() % (LS_RECORDS + 1u);
    c->exit_count = n % 19u == 5u ? 0u : random32() % (LS_RECORDS + 1u);
    int ties = random32() % 3u == 0u;     /* DBSIM-like: every priority 0x20 */
    for (unsigned i = 0; i < LS_RECORDS; ++i) {
        unsigned d = random32() % 40u;
        c->init_done[i] = d < 34u ? 0u : d < 38u ? 1u : (uint8_t)(1u + random32() % 255u);
        d = random32() % 40u;
        c->exit_done[i] = d < 34u ? 0u : d < 38u ? 1u : (uint8_t)(1u + random32() % 255u);
        c->init_priority[i] = (uint8_t)(ties ? 0x20u : ls_pick(priorities, 10));
        c->exit_priority[i] = (uint8_t)(ties ? 0x20u : ls_pick(priorities, 10));
        c->init_id[i] = random32() % 24u == 0u ? -1 : (int)(random32() % LS_VOIDS);
        c->exit_id[i] = random32() % 24u == 0u ? -1 : (int)(random32() % LS_VOIDS);
    }
    int active = random32() % 10u < 4u;
    for (unsigned i = 0; i < LS_VOIDS; ++i) {
        unsigned r = random32() % 100u;
        if (ls_switched_cases)
            c->action[i] = !active ? LS_ACT_NONE : r < 80u ? LS_ACT_NONE : r < 93u ? LS_ACT_ATEXIT : r < 96u ? LS_ACT_QUICK_EXIT :
                           r < 98u ? LS_ACT_ABORT : LS_ACT_ERROR_EXIT;
        else
            c->action[i] = !active ? LS_ACT_NONE : r < 80u ? LS_ACT_NONE : r < 92u ? LS_ACT_ATEXIT : r < 94u ? LS_ACT_EXIT :
                           r < 96u ? LS_ACT_QUICK_EXIT : r < 98u ? LS_ACT_CEXIT : LS_ACT_C_EXIT;
        c->action_arg[i] = c->action[i] == LS_ACT_ATEXIT ? random32() % LS_VOIDS : random32() % 8u;
    }
    unsigned r = random32() % 10u;
    c->registrations = r < 3u ? 0u : r < 6u ? 1u + random32() % 3u : r < 8u ? 4u + random32() % 5u : 30u + random32() % 6u;
    for (unsigned i = 0; i < c->registrations; ++i) c->registration_id[i] = (uint8_t)(random32() % LS_VOIDS);
    static const uint32_t finals[] = {LS_FIN_RETURN, LS_FIN_RETURN, LS_FIN_RETURN, LS_FIN_RETURN, LS_FIN_EXIT, LS_FIN_EXIT,
                                      LS_FIN_QUICK_EXIT, LS_FIN_CEXIT, LS_FIN_C_EXIT, LS_FIN_ABORT, LS_FIN_ABORT, LS_FIN_ERROR_EXIT};
    static const uint32_t switched_finals[] = {LS_FIN_RETURN, LS_FIN_RETURN, LS_FIN_RETURN, LS_FIN_RETURN, LS_FIN_QUICK_EXIT,
                                               LS_FIN_ABORT, LS_FIN_ABORT, LS_FIN_ERROR_EXIT};
    if (ls_switched_cases) c->final = ls_pick(switched_finals, 8);
    else c->final = n < LS_FIN_COUNT ? n : ls_pick(finals, 12);
    c->code = random32() % 3u ? random32() % 8u : random32();
    ls_command(c->command, n);
    c->environment = random32(); c->instance = random32(); c->matherr = random32(); c->matherrl = random32();
    c->fmode = random32(); c->argc = random32(); c->argv = random32(); c->envp = random32();
    r = random32() % 10u;
    c->atexit_count = r < 5u ? 0u : r < 8u ? 1u + random32() % 4u : random32() % (ES2_RTL_ATEXIT_LIMIT + 1u);
    for (unsigned i = 0; i < ES2_RTL_ATEXIT_LIMIT; ++i) c->atexit_table[i] = i < c->atexit_count ? ls_void_address(random32() % LS_VOIDS) : random32();
    for (unsigned i = 0; i < 3; ++i) c->hook_id[i] = random32() % 2u ? -1 : (int)(random32() % LS_VOIDS);
    r = random32() % 10u;
    c->sigabrt_kind = r < 4u ? 0u : r < 6u ? 1u : 2u;
    c->sigabrt_extra = random32() % 2u ? 0u : (uint8_t)(1u + random32() % 255u);
    c->version = (random32() & 0x7fffffffu) | (random32() % 2u ? 0x80000000u : 0u);
    c->thread = random32();
    c->has_window = random32() % 2u;
    ls_file_name(c->file_name, n);
    /* Drawn for the switched comparison only (the model comparison keeps
     * its own case stream). */
    for (unsigned i = 0; ls_switched_cases && i < 7; ++i) c->entry_regs[i] = random32();
}

/* Writes the case's guest memory (both runs start from these bytes). */
static void ls_put32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t ls_main_address(void) {
    return ls_c.main_kind == LS_MAIN_STDCALL ? LS_ADDR(ls_winmain_std_thunk) : ls_c.main_kind == LS_MAIN_CDECL ? LS_ADDR(ls_winmain_cdecl_thunk) : LS_ADDR(ls_main_thunk);
}
static void ls_prepare_memory(void) {
    memset(ls_memory, 0xcc, sizeof(ls_memory));
    uint8_t *t = ls_memory;
    ls_put32(t + 0x00, LS_INIT);
    ls_put32(t + 0x04, LS_INIT + ES2_RTL_RECORD_SIZE * ls_c.init_count);
    ls_put32(t + 0x08, LS_EXITS);
    ls_put32(t + 0x0c, LS_EXITS + ES2_RTL_RECORD_SIZE * ls_c.exit_count);
    ls_put32(t + 0x10, ls_c.flags);
    ls_put32(t + 0x14, 0);
    ls_put32(t + 0x18, ls_main_address());
    ls_put32(t + 0x1c, ls_c.matherr);
    ls_put32(t + 0x20, ls_c.matherrl);
    ls_put32(t + 0x28, ls_c.fmode);
    for (unsigned i = 0; i < LS_RECORDS; ++i) {
        uint8_t *r = ls_memory + 0x100 + ES2_RTL_RECORD_SIZE * i;
        r[0] = ls_c.init_done[i]; r[1] = ls_c.init_priority[i];
        ls_put32(r + 2, ls_c.init_id[i] < 0 ? 0u : ls_void_address((unsigned)ls_c.init_id[i]));
        r = ls_memory + 0x300 + ES2_RTL_RECORD_SIZE * i;
        r[0] = ls_c.exit_done[i]; r[1] = ls_c.exit_priority[i];
        ls_put32(r + 2, ls_c.exit_id[i] < 0 ? 0u : ls_void_address((unsigned)ls_c.exit_id[i]));
    }
    memcpy(ls_memory + 0x800, ls_c.command, strlen(ls_c.command) + 1u);
    for (unsigned i = 0; i < LS_VOIDS; ++i) ls_remaining[i] = 1;
    memcpy(ls_entry_regs, ls_c.entry_regs, sizeof(ls_entry_regs));
}
static uint32_t ls_hook(unsigned i) { return ls_c.hook_id[i] < 0 ? ls_cw_base() + LS_DUMMY_HOOK : ls_void_address((unsigned)ls_c.hook_id[i]); }
static uint32_t ls_handler(void) { return ls_c.sigabrt_kind == 2u ? LS_ADDR(ls_signal_thunk) : ls_c.sigabrt_kind; }

/* The runtime variables of both sides in one layout. */
typedef struct ls_vars {
    uint32_t gui, matherr, matherrl, fmode, module_count, module_table, frame, environment, command, argc, argv, envp;
    uint32_t atexit_count, atexit_table[ES2_RTL_ATEXIT_LIMIT], exitbuf, exitfopen, exitopen, sigabrt, sigabrt_extra;
} ls_vars;
static void ls_set_original(void) {
    *ls_var(LS_GUI) = 0; *ls_var(LS_MATHERR) = 0; *ls_var(LS_MATHERRL) = 0; *ls_var(LS_FMODE) = 0;
    *ls_var(LS_MODULE_COUNT) = 0; *ls_var(LS_MODULE_TABLE) = 0; *ls_var(LS_FRAME) = 0;
    *ls_var(LS_ENVIRONMENT) = 0; *ls_var(LS_COMMAND) = 0;
    *ls_var(LS_ARGC) = ls_c.argc; *ls_var(LS_ARGV) = ls_c.argv; *ls_var(LS_ENVP) = ls_c.envp;
    *ls_var(LS_ATEXIT_COUNT) = ls_c.atexit_count;
    for (unsigned i = 0; i < ES2_RTL_ATEXIT_LIMIT; ++i) *ls_var(LS_ATEXIT_TABLE + 4u * i) = ls_c.atexit_table[i];
    *ls_var(LS_EXITBUF) = ls_hook(0); *ls_var(LS_EXITFOPEN) = ls_hook(1); *ls_var(LS_EXITOPEN) = ls_hook(2);
    *ls_var(LS_SIGABRT) = ls_handler();
    *(volatile uint8_t *)(uintptr_t)(ls_cw_base() + LS_SIGABRT_EXTRA) = ls_c.sigabrt_extra;
}
static void ls_set_own(void) {
    es2_rtl_runtime_init(&ls_model, ls_cw_base() + LS_DUMMY_HOOK, ls_cw_base() + LS_ABORT_MESSAGE);
    ls_model.argc = ls_c.argc; ls_model.argv = ls_c.argv; ls_model.envp = ls_c.envp;
    ls_model.atexit_count = ls_c.atexit_count;
    memcpy(ls_model.atexit_table, ls_c.atexit_table, sizeof(ls_model.atexit_table));
    ls_model.exitbuf = ls_hook(0); ls_model.exitfopen = ls_hook(1); ls_model.exitopen = ls_hook(2);
    ls_model.sigabrt = ls_handler();
    ls_model.sigabrt_extra = ls_c.sigabrt_extra;
}
static void ls_read_original(ls_vars *v, uint32_t anchor) {
    v->gui = *ls_var(LS_GUI); v->matherr = *ls_var(LS_MATHERR); v->matherrl = *ls_var(LS_MATHERRL); v->fmode = *ls_var(LS_FMODE);
    v->module_count = *ls_var(LS_MODULE_COUNT); v->module_table = *ls_var(LS_MODULE_TABLE);
    v->frame = *ls_var(LS_FRAME) ? *ls_var(LS_FRAME) - anchor : 0;
    v->environment = *ls_var(LS_ENVIRONMENT); v->command = *ls_var(LS_COMMAND);
    v->argc = *ls_var(LS_ARGC); v->argv = *ls_var(LS_ARGV); v->envp = *ls_var(LS_ENVP);
    v->atexit_count = *ls_var(LS_ATEXIT_COUNT);
    for (unsigned i = 0; i < ES2_RTL_ATEXIT_LIMIT; ++i) v->atexit_table[i] = *ls_var(LS_ATEXIT_TABLE + 4u * i);
    v->exitbuf = *ls_var(LS_EXITBUF); v->exitfopen = *ls_var(LS_EXITFOPEN); v->exitopen = *ls_var(LS_EXITOPEN);
    v->sigabrt = *ls_var(LS_SIGABRT);
    v->sigabrt_extra = *(volatile uint8_t *)(uintptr_t)(ls_cw_base() + LS_SIGABRT_EXTRA);
}
static void ls_read_own(ls_vars *v) {
    const es2_rtl_runtime *m = &ls_model;
    v->gui = m->gui; v->matherr = m->matherr; v->matherrl = m->matherrl; v->fmode = m->fmode;
    v->module_count = m->module_count; v->module_table = m->module_table; v->frame = m->frame_record;
    v->environment = m->environment; v->command = m->command_line; v->argc = m->argc; v->argv = m->argv; v->envp = m->envp;
    v->atexit_count = m->atexit_count;
    memcpy(v->atexit_table, m->atexit_table, sizeof(v->atexit_table));
    v->exitbuf = m->exitbuf; v->exitfopen = m->exitfopen; v->exitopen = m->exitopen;
    v->sigabrt = m->sigabrt; v->sigabrt_extra = m->sigabrt_extra;
}

/* ------------------------------------------------------------------------
 * Paths reached (from the scripts and the original run's log). */
enum {
    LS_P_INIT_TIE, LS_P_INIT_LOWER, LS_P_INIT_DONE, LS_P_INIT_ZERO, LS_P_INIT_NONE, LS_P_EXIT_TIE, LS_P_EXIT_HIGHER,
    LS_P_EXIT_DONE, LS_P_EXIT_ZERO, LS_P_GUI_STDCALL, LS_P_GUI_CDECL, LS_P_CONSOLE, LS_P_CMD_LEADING, LS_P_CMD_QUOTED,
    LS_P_CMD_UNCLOSED, LS_P_CMD_TAB_NAME, LS_P_CMD_PLAIN, LS_P_CMD_TRAILING, LS_P_CMD_EMPTY_TAIL, LS_P_RETURN, LS_P_EXIT,
    LS_P_QUICK_EXIT, LS_P_CEXIT, LS_P_C_EXIT, LS_P_ABORT_DEFAULT, LS_P_ABORT_IGNORE, LS_P_ABORT_HANDLER, LS_P_ABORT_EXTRA,
    LS_P_ERROR_EXIT, LS_P_ATEXIT_LIFO, LS_P_ATEXIT_FULL, LS_P_NESTED_REGISTRATION, LS_P_NESTED_EXIT, LS_P_HOOK_STUB,
    LS_P_HOOK_DUMMY, LS_P_NT, LS_P_9X_WINDOW, LS_P_9X_NO_WINDOW, LS_P_TITLE_BACKSLASH, LS_P_TITLE_COLON, LS_P_TITLE_PLAIN,
    LS_P_COUNT
};
static const char *const ls_path_names[LS_P_COUNT] = {
    "init tie in table order", "init lower priority first", "init done skipped", "init scan ends at function 0", "no init record",
    "exit tie reversed", "exit higher priority first", "exit done skipped", "exit scan ends at function 0", "WinMain stdcall",
    "WinMain cdecl", "main (console)", "command leading blanks", "command quoted name", "command unclosed quote",
    "command tab ends name", "command plain name", "command blanks after name", "command empty tail", "WinMain returns",
    "exit", "_exit", "_cexit", "_c_exit", "abort default", "abort ignored", "abort handler", "abort handler extra argument",
    "__ErrorExit", "atexit LIFO", "atexit table full", "atexit from a record function", "exit from a record function", "stream hook called",
    "stream hook dummy", "message box NT", "message box 9x window", "message box 9x no window", "title after backslash",
    "title after colon", "title whole name",
};
static unsigned ls_paths[LS_P_COUNT];
static void ls_record_paths(unsigned *paths, const ls_event *log, unsigned log_count) {
    const ls_case *c = &ls_c;
    for (int exits = 0; exits < 2; ++exits) {
        unsigned count = exits ? c->exit_count : c->init_count, pending = 0, done = 0, zero = 0, tie = 0, distinct = 0;
        const uint8_t *flags = exits ? c->exit_done : c->init_done, *priority = exits ? c->exit_priority : c->init_priority;
        const int *id = exits ? c->exit_id : c->init_id;
        unsigned seen[256] = {0};
        for (unsigned i = 0; i < count; ++i) {
            if (id[i] < 0) { if (i + 1u < count) zero = 1; break; }
            if (flags[i]) { done = 1; continue; }
            if (seen[priority[i]]++) tie = 1; else ++distinct;
            ++pending;
        }
        if (!exits) {
            if (tie) ++paths[LS_P_INIT_TIE];
            if (distinct > 1u) ++paths[LS_P_INIT_LOWER];
            if (done) ++paths[LS_P_INIT_DONE];
            if (zero) ++paths[LS_P_INIT_ZERO];
            if (!pending) ++paths[LS_P_INIT_NONE];
        } else if (ls_kind_calls[ES2_RTL_CALL_EXIT_RECORD]) {
            if (tie) ++paths[LS_P_EXIT_TIE];
            if (distinct > 1u) ++paths[LS_P_EXIT_HIGHER];
            if (done) ++paths[LS_P_EXIT_DONE];
            if (zero) ++paths[LS_P_EXIT_ZERO];
        }
    }
    int entered = ls_kind_calls[ES2_RTL_CALL_WINMAIN] || ls_kind_calls[ES2_RTL_CALL_MAIN];
    if (ls_kind_calls[ES2_RTL_CALL_WINMAIN]) {
        ++paths[c->main_kind == LS_MAIN_STDCALL ? LS_P_GUI_STDCALL : LS_P_GUI_CDECL];
        const char *p = c->command;
        if (*p == ' ' || *p == '\t') ++paths[LS_P_CMD_LEADING];
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '"') {
            ++paths[LS_P_CMD_QUOTED];
            const char *q = p + 1;
            while (*q && *q != '"' && *q != '\t') ++q;
            if (*q == '\t') ++paths[LS_P_CMD_TAB_NAME];
            if (!*q) ++paths[LS_P_CMD_UNCLOSED];
            p = q;
        } else {
            ++paths[LS_P_CMD_PLAIN];
            while (*p && *p != ' ' && *p != '\t') ++p;
            if (*p == '\t') ++paths[LS_P_CMD_TAB_NAME];
        }
        if (*p == '"') ++p;
        if (*p == ' ' || *p == '\t') ++paths[LS_P_CMD_TRAILING];
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) ++paths[LS_P_CMD_EMPTY_TAIL];
    }
    if (ls_kind_calls[ES2_RTL_CALL_MAIN]) ++paths[LS_P_CONSOLE];
    if (entered) {
        static const unsigned fin[LS_FIN_COUNT] = {LS_P_RETURN, LS_P_EXIT, LS_P_QUICK_EXIT, LS_P_CEXIT, LS_P_C_EXIT, LS_P_ABORT_DEFAULT, LS_P_ERROR_EXIT};
        unsigned p = fin[c->final];
        if (c->final == LS_FIN_ABORT) p = c->sigabrt_kind == 0u ? LS_P_ABORT_DEFAULT : c->sigabrt_kind == 1u ? LS_P_ABORT_IGNORE : LS_P_ABORT_HANDLER;
        ++paths[p];
        if (c->final == LS_FIN_ABORT && c->sigabrt_kind == 2u && c->sigabrt_extra) ++paths[LS_P_ABORT_EXTRA];
    }
    if (ls_kind_calls[ES2_RTL_CALL_ATEXIT] > 1u) ++paths[LS_P_ATEXIT_LIFO];
    if (ls_full_tables) ++paths[LS_P_ATEXIT_FULL];
    if (ls_nested_registrations) ++paths[LS_P_NESTED_REGISTRATION];
    if (ls_nested_exits) ++paths[LS_P_NESTED_EXIT];
    unsigned hooks = ls_kind_calls[ES2_RTL_CALL_EXITBUF] + ls_kind_calls[ES2_RTL_CALL_EXITFOPEN] + ls_kind_calls[ES2_RTL_CALL_EXITOPEN];
    if (hooks) ++paths[c->hook_id[0] >= 0 || c->hook_id[1] >= 0 || c->hook_id[2] >= 0 ? LS_P_HOOK_STUB : LS_P_HOOK_DUMMY];
    for (unsigned i = 0; i < log_count && i < LS_EVENTS; ++i) {
        const ls_event *e = &log[i];
        if (e->kind == LS_EV_VERSION) ++paths[!(c->version & 0x80000000u) ? LS_P_NT : c->has_window ? LS_P_9X_WINDOW : LS_P_9X_NO_WINDOW];
        if (e->kind == LS_EV_BOX) {
            char name[ES2_RTL_FILE_NAME_SIZE];
            snprintf(name, sizeof(name), "%s", c->file_name);       /* what GetModuleFileNameA leaves */
            ++paths[strchr(name, '\\') ? LS_P_TITLE_BACKSLASH : strchr(name, ':') ? LS_P_TITLE_COLON : LS_P_TITLE_PLAIN];
        }
    }
}

/* ------------------------------------------------------------------------
 * A run: the event log, frames and table bytes of the run in progress are
 * kept per side. */
#define LS_STACK_DWORDS 65536u
#define LS_FILL_DWORDS 16384u
static uint32_t ls_stack[LS_STACK_DWORDS] __attribute__((aligned(16)));
static uint32_t ls_startup_anchor, ls_winmain_offset, ls_exit_offset;
static void ls_fpu_garbage(void) {
    uint16_t w = 0x0a7fu;
    __asm__ volatile("fninit\n\tfld1\n\tfldz\n\tfldcw %0" ::"m"(w));
}
static uint32_t ls_top(void) { return LS_ADDR(ls_stack + LS_STACK_DWORDS - 16u); }
static void ls_begin_run(unsigned mode, uint32_t pattern) {
    ls_mode = mode;
    ls_cur_count = ls_cur_frame_count = ls_cur_word_count = 0;
    ls_unexpected = 0;
    ls_bad_return = 0;
    if (ls_rich) for (unsigned i = LS_STACK_DWORDS - LS_FILL_DWORDS; i < LS_STACK_DWORDS; ++i) ls_stack[i] = pattern ^ i;
}
static void ls_end_run(unsigned side) {
    unsigned n = ls_cur_count < LS_EVENTS ? ls_cur_count : LS_EVENTS;
    memcpy(ls_log[side], ls_cur, n * sizeof(ls_event));
    ls_log_count[side] = ls_cur_count;
    n = ls_cur_frame_count < LS_FRAMES ? ls_cur_frame_count : LS_FRAMES;
    memcpy(ls_frames[side], ls_cur_frames, n * sizeof(ls_frame));
    ls_frame_count[side] = ls_cur_frame_count;
    n = ls_cur_word_count < LS_WORDS ? ls_cur_word_count : LS_WORDS;
    memcpy(ls_words[side], ls_cur_words, n * sizeof(uint32_t));
    ls_word_count[side] = ls_cur_word_count;
    memcpy(ls_memory_after[side], ls_memory, sizeof(ls_memory));
}
static void ls_print_event(const char *side, const ls_event *e) {
    fprintf(stderr, "  %s: kind %u id %u a %08x %08x %08x %08x esp %08x fpu %04x ret %05x \"%s\"\n", side, e->kind, e->id, e->a0, e->a1,
            e->a2, e->a3, e->esp, e->fpu, e->ret, e->text);
}
static int ls_compare_events(unsigned n) {
    if (ls_log_count[0] > LS_EVENTS || ls_log_count[1] > LS_EVENTS) return fail("event log overflow");
    unsigned count = ls_log_count[0] < ls_log_count[1] ? ls_log_count[0] : ls_log_count[1];
    for (unsigned i = 0; i < count; ++i)
        if (memcmp(&ls_log[0][i], &ls_log[1][i], sizeof(ls_event))) {
            fprintf(stderr, "FAIL: %s case %u: event %u differs\n", current_function, n, i);
            ls_print_event("original", &ls_log[0][i]);
            ls_print_event("own", &ls_log[1][i]);
            return 1;
        }
    if (ls_log_count[0] != ls_log_count[1]) {
        fprintf(stderr, "FAIL: %s case %u: %u original events, %u own\n", current_function, n, ls_log_count[0], ls_log_count[1]);
        if (count < ls_log_count[0]) ls_print_event("original", &ls_log[0][count]);
        if (count < ls_log_count[1]) ls_print_event("own", &ls_log[1][count]);
        return 1;
    }
    if (memcmp(ls_memory_after[0], ls_memory_after[1], sizeof(ls_memory))) {
        for (unsigned i = 0; i < sizeof(ls_memory); ++i)
            if (ls_memory_after[0][i] != ls_memory_after[1][i]) {
                fprintf(stderr, "FAIL: %s case %u: table byte +0x%03x original %02x own %02x\n", current_function, n, i,
                        ls_memory_after[0][i], ls_memory_after[1][i]);
                break;
            }
        return 1;
    }
    return 0;
}
static int ls_compare_vars(unsigned n, const ls_vars *vars) {
    if (!memcmp(&vars[0], &vars[1], sizeof(ls_vars))) return 0;
    const uint32_t *a = (const uint32_t *)&vars[0], *b = (const uint32_t *)&vars[1];
    for (unsigned i = 0; i < sizeof(ls_vars) / 4u; ++i)
        if (a[i] != b[i]) { fprintf(stderr, "FAIL: %s case %u: runtime variable %u original %08x own %08x\n", current_function, n, i, a[i], b[i]); break; }
    return 1;
}

/* One case of the model comparison: the original, then the model, then the
 * comparison. */
static int ls_case_run(unsigned n) {
    current_case = n; current_case_valid = 1;
    ls_generate(n);
    ls_vars vars[2];
    BYTE host_fpu[108];
    /* Original. */
    ls_prepare_memory();
    ls_set_original();
    ls_begin_run(LS_ORIGINAL, 0);
    ls_nested_exits = ls_nested_registrations = ls_full_tables = 0;
    __asm__ volatile("fnsave %0" : "=m"(host_fpu));
    ls_fpu_garbage();
    ls_run(ls_exports[LS_X_STARTUP], ls_top(), LS_TABLE);
    __asm__ volatile("frstor %0" ::"m"(host_fpu));
    ls_startup_anchor = ls_top() - 8u;
    if (ls_unexpected) {
        fprintf(stderr, "FAIL: %s case %u: the original called the unexpected import %s\n", current_function, n,
                rtl_cw_imports[ls_unexpected - 1u]);
        return 1;
    }
    ls_end_run(0);
    ls_read_original(&vars[0], ls_startup_anchor);
    if (*ls_var(LS_SECOND_LIST)) return fail("CW3220's second module list (0x423f04) was written");
    /* Own. */
    ls_prepare_memory();
    ls_set_own();
    ls_begin_run(LS_MODEL, 0);
    ls_anchor = 0; ls_bad_target = 0; ls_own_frame = ls_own_handler = 0;
    memset(ls_kind_calls, 0, sizeof(ls_kind_calls));
    __asm__ volatile("fnsave %0" : "=m"(host_fpu));
    ls_fpu_garbage();
    int returned = 0;
    if (!setjmp(ls_own_escape)) { es2_rtl_startup(&ls_model, &ls_env, 0, LS_TABLE); returned = 1; }
    __asm__ volatile("frstor %0" ::"m"(host_fpu));
    ls_mode = LS_ORIGINAL;
    ls_end_run(1);
    ls_read_own(&vars[1]);
    if (returned) return fail("the model returned without ExitProcess");
    if (ls_bad_target) { fprintf(stderr, "FAIL: %s case %u: the model called %08x\n", current_function, n, ls_bad_target); return 1; }
    /* Compare. */
    if (ls_compare_events(n) || ls_compare_vars(n, vars)) return 1;
    /* Observed depths of a DBSIM-shaped case (stdcall WinMain returning). */
    for (unsigned i = 0; i < ls_log_count[0]; ++i) {
        if (ls_log[0][i].kind == LS_EV_WINMAIN) ls_winmain_offset = ls_log[0][i].esp;
        if (ls_log[0][i].kind == LS_EV_EXIT && ls_c.final == LS_FIN_RETURN && ls_c.main_kind == LS_MAIN_STDCALL &&
            ls_kind_calls[ES2_RTL_CALL_WINMAIN] && !ls_nested_exits) ls_exit_offset = ls_log[0][i].esp;
    }
    ls_record_paths(ls_paths, ls_log[0], ls_log_count[0]);
    return 0;
}

/* ------------------------------------------------------------------------
 * One case of the switched comparison. */
static unsigned ls_switched_paths[LS_P_COUNT];
static uint64_t ls_switched_frames, ls_switched_words;
static const char *const ls_register_names[7] = {"EAX", "ECX", "EDX", "EBX", "ESI", "EDI", "EBP"};
static int ls_install_life(void) {
    rtl_options o;
    memset(&o, 0, sizeof(o));
    o.image = mapped; o.expected_base = 0x00400000u; o.image_size = RTL_IMAGE_SIZE;
    o.resolve = rtl_cw_resolve;
    o.stack_lo = LS_ADDR(ls_stack); o.stack_hi = LS_ADDR(ls_stack + LS_STACK_DWORDS);
    if (!rtl_module_range((const void *)ls_install_life, &o.hook_lo, &o.hook_hi)) return fail("harness module range");
    for (unsigned k = 0; k < RTL_LIFE_GROUP; ++k) {
        o.selected[rtl_life_group[k].import] = TRUE;
        o.modes[rtl_life_group[k].import] = ES2_HOOK_REPLACED;
    }
    o.cw_image = rtl_cw;
    if (!rtl_install(&o)) { fprintf(stderr, "FAIL: %s: lifecycle group refused: %s\n", current_function, rtl_reason); return 1; }
    if (!rtl_life_active) return fail("lifecycle group not active");
    return 0;
}
static int ls_compare_frames(unsigned n) {
    if (ls_frame_count[0] > LS_FRAMES || ls_frame_count[1] > LS_FRAMES || ls_word_count[0] > LS_WORDS || ls_word_count[1] > LS_WORDS)
        return fail("frame log overflow");
    if (ls_frame_count[0] != ls_frame_count[1]) {
        fprintf(stderr, "FAIL: %s case %u: %u original frames, %u own\n", current_function, n, ls_frame_count[0], ls_frame_count[1]);
        return 1;
    }
    for (unsigned i = 0; i < ls_frame_count[0]; ++i) {
        const ls_frame *a = &ls_frames[0][i], *b = &ls_frames[1][i];
        const ls_event *e = &ls_log[0][a->event];
        unsigned first = a->kind == LS_FRAME_API ? 3u : 0u;   /* Win32 calls: EBX ESI EDI EBP */
        for (unsigned r = first; r < 7; ++r)
            if (a->regs[r] != b->regs[r]) {
                fprintf(stderr, "FAIL: %s case %u: event %u (kind %u) %s original %08x own %08x\n", current_function, n, a->event,
                        e->kind, ls_register_names[r], a->regs[r], b->regs[r]);
                return 1;
            }
        if (a->esp != b->esp || a->count != b->count || a->kind != b->kind) {
            fprintf(stderr, "FAIL: %s case %u: event %u (kind %u) ESP original %08x own %08x, %u/%u words\n", current_function, n,
                    a->event, e->kind, a->esp, b->esp, a->count, b->count);
            return 1;
        }
        for (unsigned w = 0; w < a->count; ++w)
            if (ls_words[0][a->first + w] != ls_words[1][b->first + w]) {
                uint32_t address = a->esp + (a->kind == LS_FRAME_EXIT ? 0u : 4u) + 4u * w;
                fprintf(stderr, "FAIL: %s case %u: event %u (kind %u at ESP anchor%+d) stack word anchor%+d original %08x own %08x\n",
                        current_function, n, a->event, e->kind, (int)(a->esp - ls_startup_anchor), (int)(address - ls_startup_anchor),
                        ls_words[0][a->first + w], ls_words[1][b->first + w]);
                return 1;
            }
        ++ls_switched_frames;
        ls_switched_words += a->count;
    }
    return 0;
}
static int ls_switched_run(unsigned n) {
    current_case = n; current_case_valid = 1;
    ls_switched_cases = 1;
    ls_generate(n);
    ls_switched_cases = 0;
    ls_vars vars[2];
    BYTE host_fpu[108];
    uint32_t pattern = 0x5a000000u | (random32() & 0x00ffffffu);
    ls_rich = 1;
    /* Original, through DBSIM's thunks to the mapped exports. */
    ls_prepare_memory();
    ls_set_original();
    ls_begin_run(LS_ORIGINAL, pattern);
    ls_nested_exits = ls_nested_registrations = ls_full_tables = 0;
    memset(ls_kind_calls, 0, sizeof(ls_kind_calls));
    __asm__ volatile("fnsave %0" : "=m"(host_fpu));
    ls_fpu_garbage();
    ls_run(ls_exports[LS_X_STARTUP], ls_top(), LS_TABLE);
    __asm__ volatile("frstor %0" ::"m"(host_fpu));
    ls_startup_anchor = ls_top() - 8u;
    if (ls_unexpected) {
        ls_rich = 0;
        fprintf(stderr, "FAIL: %s case %u: the original called the unexpected import %s\n", current_function, n, rtl_cw_imports[ls_unexpected - 1u]);
        return 1;
    }
    ls_end_run(0);
    ls_read_original(&vars[0], ls_startup_anchor);
    /* Switched: the lifecycle group in DBSIM's slots; the arming's jump. */
    ls_prepare_memory();
    ls_set_original();
    if (ls_install_life()) { ls_rich = 0; return 1; }
    ls_begin_run(LS_SWITCHED, pattern);
    __asm__ volatile("fnsave %0" : "=m"(host_fpu));
    ls_fpu_garbage();
    ls_run(LS_ADDR(rtl_life_startup_entry), ls_top(), LS_TABLE);
    __asm__ volatile("frstor %0" ::"m"(host_fpu));
    ls_end_run(1);
    ls_read_original(&vars[1], ls_startup_anchor);
    /* The atexit table is the lifecycle's; CW3220's own stays as it was. */
    vars[1].atexit_count = rtl_life_rt.atexit_count;
    memcpy(vars[1].atexit_table, rtl_life_rt.atexit_table, sizeof(vars[1].atexit_table));
    uint32_t cw_count = *ls_var(LS_ATEXIT_COUNT), cw_first = *ls_var(LS_ATEXIT_TABLE);
    /* The kinds of call, for the paths (the events are compared below). */
    memset(ls_kind_calls, 0, sizeof(ls_kind_calls));
    ls_kind_calls[ES2_RTL_CALL_INIT_RECORD] = (unsigned)rtl_life_init_records;
    ls_kind_calls[ls_c.main_kind == LS_MAIN_CONSOLE ? ES2_RTL_CALL_MAIN : ES2_RTL_CALL_WINMAIN] = (unsigned)rtl_life_winmain;
    ls_kind_calls[ES2_RTL_CALL_ATEXIT] = (unsigned)rtl_life_atexit_run;
    ls_kind_calls[ES2_RTL_CALL_EXIT_RECORD] = (unsigned)rtl_life_exit_records;
    ls_kind_calls[ES2_RTL_CALL_EXITBUF] = (unsigned)rtl_life_hooks;
    rtl_life_current = 0;   /* abandoned by ExitProcess */
    int uninstalled = rtl_uninstall();
    ls_rich = 0;
    ls_mode = LS_ORIGINAL;
    if (ls_unexpected) {
        fprintf(stderr, "FAIL: %s case %u: the switched run called the unexpected import %s\n", current_function, n, rtl_cw_imports[ls_unexpected - 1u]);
        return 1;
    }
    if (!uninstalled) return fail("lifecycle uninstall");
    if (ls_bad_return) { fprintf(stderr, "FAIL: %s case %u: %u calls without the lifecycle's return address\n", current_function, n, ls_bad_return); return 1; }
    if (cw_count != ls_c.atexit_count || cw_first != ls_c.atexit_table[0]) return fail("the switched run wrote CW3220's atexit table");
    if (ls_compare_events(n) || ls_compare_frames(n) || ls_compare_vars(n, vars)) return 1;
    ls_record_paths(ls_switched_paths, ls_log[0], ls_log_count[0]);
    return 0;
}

/* ------------------------------------------------------------------------
 * Set-up: exports, Win32 slots, own env. */
static uint32_t *ls_cw_slot(const char *dll, const char *name) {
    uint32_t pe = get32(rtl_cw + 0x3c), imports = get32(rtl_cw + pe + 24 + 96 + 8);
    for (uint32_t d = imports; get32(rtl_cw + d + 12); d += 20) {
        if (_stricmp((const char *)rtl_cw + get32(rtl_cw + d + 12), dll)) continue;
        uint32_t lookup = get32(rtl_cw + d), iat = get32(rtl_cw + d + 16);
        if (!lookup) lookup = iat;
        for (unsigned i = 0; get32(rtl_cw + lookup + 4 * i); ++i) {
            uint32_t entry = get32(rtl_cw + lookup + 4 * i);
            if (!(entry & 0x80000000u) && !strcmp((const char *)rtl_cw + entry + 2, name)) return (uint32_t *)(rtl_cw + iat + 4 * i);
        }
    }
    return NULL;
}
static int rtl_start_cases(const char *selection) {
    current_function = strcmp(selection, "--rtl-start") ? selection : "rtl_start";
    if (rtl_map_cw3220(getenv("ES2_DATA_ROOT"))) return 1;
    for (unsigned i = 0; i < LS_X_COUNT; ++i) {
        ls_exports[i] = rtl_cw_export(ls_export_names[i].name);
        if (ls_exports[i] != ls_cw_base() + ls_export_names[i].rva) return fail("lifecycle export address");
    }
    if (*ls_var(LS_FPU_DEFAULT_WORD) != ES2_RTL_FPU_DEFAULT) return fail("CW3220 FPU default word");
    if (*(volatile uint8_t *)(uintptr_t)(ls_cw_base() + LS_GUI_MESSAGE_BYTE) != 1u) return fail("CW3220 message-box byte");
    if (*ls_var(LS_EXITBUF) != ls_cw_base() + LS_DUMMY_HOOK || *ls_var(LS_EXITFOPEN) != ls_cw_base() + LS_DUMMY_HOOK ||
        *ls_var(LS_EXITOPEN) != ls_cw_base() + LS_DUMMY_HOOK || *(const uint8_t *)(rtl_cw + LS_DUMMY_HOOK) != 0xc3u)
        return fail("CW3220 stream hooks at load");
    if (strcmp((const char *)rtl_cw + LS_ABORT_MESSAGE, "Abnormal program termination")) return fail("abort message");
    /* Every import slot: an own stub that ends the run, nine with behaviour. */
    BYTE *stubs = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!stubs) return fail("stub allocation");
    static uint32_t saved_slots[128];
    static uint32_t *slots[128];
    unsigned slot_count = 0;
    uint32_t pe = get32(rtl_cw + 0x3c), imports = get32(rtl_cw + pe + 24 + 96 + 8);
    for (uint32_t d = imports; get32(rtl_cw + d + 12); d += 20) {
        uint32_t lookup = get32(rtl_cw + d), iat = get32(rtl_cw + d + 16);
        if (!lookup) lookup = iat;
        for (unsigned i = 0; get32(rtl_cw + lookup + 4 * i); ++i) {
            if (slot_count >= 128) return fail("CW3220 import count");
            uint32_t *slot = (uint32_t *)(rtl_cw + iat + 4 * i);
            BYTE *s = stubs + 16 * slot_count;
            s[0] = 0x68; put32(s + 1, slot_count); s[5] = 0xe8;
            put32(s + 6, (uint32_t)(uintptr_t)ls_unexpected_import - (uint32_t)(uintptr_t)(s + 10)); s[10] = 0xcc;
            slots[slot_count] = slot; saved_slots[slot_count++] = *slot;
            *slot = (uint32_t)(uintptr_t)s;
        }
    }
    DWORD previous;
    if (!VirtualProtect(stubs, 4096, PAGE_EXECUTE_READ, &previous)) return fail("stub protection");
    static const struct { const char *dll, *name; const void *stub; } named[] = {
        {"KERNEL32.dll", "GetEnvironmentStrings", ls_api_environment},
        {"KERNEL32.dll", "GetCommandLineA", ls_api_command},
        {"KERNEL32.dll", "GetModuleHandleA", ls_api_handle},
        {"KERNEL32.dll", "GetModuleFileNameA", ls_api_file},
        {"KERNEL32.dll", "GetVersion", ls_api_version},
        {"KERNEL32.dll", "GetCurrentThreadId", ls_api_thread},
        {"USER32.dll", "EnumThreadWindows", ls_api_enum},
        {"USER32.dll", "MessageBoxA", ls_api_box},
        {"KERNEL32.dll", "ExitProcess", ls_api_exit},
    };
    for (unsigned i = 0; i < sizeof(named) / sizeof(named[0]); ++i) {
        uint32_t *slot = ls_cw_slot(named[i].dll, named[i].name);
        if (!slot) return fail("CW3220 import slot missing");
        *slot = (uint32_t)(uintptr_t)named[i].stub;
    }
    memset(&ls_env, 0, sizeof(ls_env));
    ls_env.read8 = ls_env_read8; ls_env.write8 = ls_env_write8; ls_env.read32 = ls_env_read32; ls_env.call = ls_env_call;
    ls_env.fpu_reset = ls_env_fpu; ls_env.register_frame = ls_env_frame; ls_env.environment_strings = ls_env_environment;
    ls_env.command_line = ls_env_command; ls_env.module_handle = ls_env_handle; ls_env.module_file_name = ls_env_file;
    ls_env.version = ls_env_version; ls_env.thread_id = ls_env_thread; ls_env.thread_has_window = ls_env_window;
    ls_env.message_box = ls_env_box; ls_env.exit_process = ls_env_exit; ls_env.site = ls_env_site;

    unsigned cases = quick_tier ? 4000u : 40000u;
    unsigned long long events = 0;
    for (unsigned n = 0; n < cases; ++n) {
        if (ls_case_run(n)) return 1;
        events += ls_log_count[0];
    }
    current_case_valid = 0;
    printf("PASS: rtl_start %u cases, %llu events: init/exit record order and done flags, WinMain/main arguments and stack "
           "depth, x87 state, SEH record, atexit order, exit paths, abort/__ErrorExit message boxes, ExitProcess status and "
           "depth, the ESP and return address of every guest and Win32 call, runtime variables equal to the original\n", cases, events);
    printf("DEPTH: WinMain entry anchor%+d, ExitProcess after WinMain returns anchor%+d (stdcall WinMain)\n",
           (int)ls_winmain_offset, (int)ls_exit_offset);
    printf("PATHS:");
    for (unsigned i = 0; i < LS_P_COUNT; ++i) printf("%s %s %u", i ? "," : "", ls_path_names[i], ls_paths[i]);
    printf("\n");
    for (unsigned i = 0; i < LS_P_COUNT; ++i)
        if (!ls_paths[i]) { fprintf(stderr, "FAIL: %s: path not reached: %s\n", current_function, ls_path_names[i]); return 1; }

    /* The switched comparison. DBSIM's CW3220 slots point to the mapped exports
     * while it runs. */
    uint32_t dbsim_saved[RTL_IMPORT_COUNT];
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        uint32_t *slot = (uint32_t *)(mapped + RTL_IAT_RVA + 4 * i);
        dbsim_saved[i] = *slot;
        *slot = rtl_cw_export(rtl_imports[i].symbol);
        if (!*slot) return fail("CW3220 export of an import missing");
    }
    unsigned switched = quick_tier ? 1500u : 15000u;
    unsigned long long switched_events = 0;
    for (unsigned n = 0; n < switched; ++n) {
        if (ls_switched_run(0x100000u + n)) return 1;
        switched_events += ls_log_count[0];
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) *(uint32_t *)(mapped + RTL_IAT_RVA + 4 * i) = dbsim_saved[i];
    for (unsigned i = 0; i < slot_count; ++i) *slots[i] = saved_slots[i];
    printf("PASS: rtl_start switched %u cases, %llu events, %llu frames, %llu stack words: the lifecycle group (proxy/rtl_life.h) "
           "equal to the original in events, registers at every guest and Win32 call, the stack words above them, table "
           "bytes and CW3220's variables\n", switched, switched_events, (unsigned long long)ls_switched_frames,
           (unsigned long long)ls_switched_words);
    static const unsigned switched_required[] = {
        LS_P_INIT_TIE, LS_P_INIT_LOWER, LS_P_INIT_DONE, LS_P_INIT_ZERO, LS_P_EXIT_TIE, LS_P_EXIT_HIGHER, LS_P_GUI_STDCALL,
        LS_P_GUI_CDECL, LS_P_CONSOLE, LS_P_CMD_QUOTED, LS_P_CMD_PLAIN, LS_P_RETURN, LS_P_QUICK_EXIT, LS_P_ABORT_DEFAULT,
        LS_P_ABORT_IGNORE, LS_P_ABORT_HANDLER, LS_P_ABORT_EXTRA, LS_P_ERROR_EXIT, LS_P_ATEXIT_LIFO, LS_P_ATEXIT_FULL,
        LS_P_NESTED_REGISTRATION, LS_P_NESTED_EXIT, LS_P_HOOK_STUB, LS_P_HOOK_DUMMY, LS_P_NT, LS_P_9X_WINDOW,
        LS_P_9X_NO_WINDOW, LS_P_TITLE_BACKSLASH, LS_P_TITLE_COLON, LS_P_TITLE_PLAIN,
    };
    printf("SWITCHED PATHS:");
    for (unsigned i = 0; i < sizeof(switched_required) / sizeof(switched_required[0]); ++i)
        printf("%s %s %u", i ? "," : "", ls_path_names[switched_required[i]], ls_switched_paths[switched_required[i]]);
    printf("\n");
    for (unsigned i = 0; i < sizeof(switched_required) / sizeof(switched_required[0]); ++i)
        if (!ls_switched_paths[switched_required[i]]) {
            fprintf(stderr, "FAIL: %s: switched path not reached: %s\n", current_function, ls_path_names[switched_required[i]]);
            return 1;
        }
    puts("PASS: rtl_start in-vitro run against the mapped CW3220.DLL; no entry point executed; no game process started");
    return 0;
}
