#ifndef ES2_CORE_RTL_START_H
#define ES2_CORE_RTL_START_H
#include <stdint.h>

/* The Borland runtime lifecycle of CW3220.DLL as own code: __startup,
 * the init/exit record selector, exit, _exit, _cexit, _c_exit, atexit,
 * abort (through raise(SIGABRT)), __ErrorExit and __ErrorMessage.
 * Contract: docs/reference-game/rtl_imports.md, section "the runtime
 * lifecycle".
 *
 * The model keeps the runtime variables CW3220 owns in `es2_rtl_runtime`
 * and reaches the module's data (the module table, its init and exit
 * records, the command line) only through `es2_rtl_start_env` with 32-bit
 * addresses. Every guest function the runtime calls (init and exit records,
 * WinMain or main, atexit functions, the stream clean-up hooks, a signal
 * handler) goes through env->call with the stack pointer the original would
 * give it; every Win32 call through the env's platform callbacks, in the
 * original's order. Stack pointers are 32-bit values relative to whatever
 * the caller passes as `esp` (the ESP at the entry of the modelled export,
 * pointing at its return address).
 *
 * rtl_imports.md, section "the lifecycle group as own code": what an env
 * needs to run the
 * lifecycle on a real stack in place of CW3220: the registers the original
 * holds at every guest call and Win32 call (es2_rtl_cpu, from the export's
 * entry registers in `entry`), the frame words its functions leave above
 * those calls (painted through env->write32 when it is set; the return
 * address of the call itself is the env's), the original's return address
 * of every call as a CW3220 RVA, and the guest addresses of the Win32 calls
 * (env->site). The stream hooks and the SIGABRT entry may live in guest
 * memory (CW3220's own variables) instead of the runtime struct. An env that
 * sets none of this gets the plain lifecycle model unchanged.
 *
 * The original never returns from exit_process; the model returns
 * ES2_RTL_EXITED after it (and sets `exited`), so that a test env can
 * continue. */

#define ES2_RTL_ATEXIT_LIMIT 32u
/* x87: fninit leaves 0x037f; __fpreset then calls _control87(0x1332, 0x1fff)
 * (default word at CW3220 0x421dc8). */
#define ES2_RTL_FPU_FNINIT 0x037fu
#define ES2_RTL_FPU_DEFAULT 0x1332u
#define ES2_RTL_FPU_MASK 0x1fffu
#define ES2_RTL_SHOW_DEFAULT 10u        /* nCmdShow handed to WinMain (SW_SHOWDEFAULT) */
#define ES2_RTL_SIGABRT 22u
#define ES2_RTL_SIGABRT_INDEX 9u        /* SIGABRT's place in raise's signal table (CW3220 0x4223a8) */
#define ES2_RTL_ABORT_STATUS 3u
#define ES2_RTL_ERROR_STATUS 1u
#define ES2_RTL_FILE_NAME_SIZE 0x50u    /* __ErrorMessage's GetModuleFileNameA buffer */
#define ES2_RTL_FRAME_HANDLER_RVA 0x5160u  /* CW3220's top-level SEH handler of __startup */
#define ES2_RTL_ENUM_PROC_RVA 0x18e88u     /* CW3220's EnumThreadWindows callback */
#define ES2_RTL_SIGNAL_TABLE_RVA 0x223a8u  /* raise's signal numbers; EDX ends at entry 9 */

/* The module table handed to __startup (DBSIM: 0x49702c). */
#define ES2_RTL_TABLE_INIT_START 0x00u
#define ES2_RTL_TABLE_INIT_END 0x04u
#define ES2_RTL_TABLE_EXIT_START 0x08u
#define ES2_RTL_TABLE_EXIT_END 0x0cu
#define ES2_RTL_TABLE_FLAGS 0x10u       /* bit 0: WinMain (GUI), else main */
#define ES2_RTL_TABLE_MAIN 0x18u
#define ES2_RTL_TABLE_MATHERR 0x1cu
#define ES2_RTL_TABLE_MATHERRL 0x20u
#define ES2_RTL_TABLE_FMODE 0x28u
/* A record: byte 0 done flag, byte 1 priority, dword 2 function. */
#define ES2_RTL_RECORD_SIZE 6u

/* The general registers of the modelled CW3220 code. */
typedef struct es2_rtl_cpu { uint32_t eax, ecx, edx, ebx, esi, edi, ebp; } es2_rtl_cpu;

typedef struct es2_rtl_runtime {
    uint32_t gui;                      /* 0x423f30: table flags & 1 */
    uint32_t matherr, matherrl;        /* 0x421df8, 0x421dfc */
    uint32_t fmode;                    /* 0x41fdb0: address of the module's _fmode word */
    uint32_t module_count;             /* 0x423f24: 0 before __startup, then 1 */
    uint32_t module_table;             /* 0x423f28 */
    uint32_t frame_record;             /* 0x423f0c: __startup's SEH record */
    uint32_t environment;              /* 0x423f10: GetEnvironmentStrings */
    uint32_t command_line;             /* 0x423f14: GetCommandLineA */
    uint32_t argc, argv, envp;         /* 0x423f18..0x423f20: main's arguments */
    uint32_t atexit_count;             /* 0x4222b8 */
    uint32_t atexit_table[ES2_RTL_ATEXIT_LIMIT]; /* 0x423a64 */
    uint32_t exitbuf, exitfopen, exitopen; /* 0x4222bc, 0x4222c0, 0x4222c4: stream clean-up hooks */
    uint32_t sigabrt;                  /* 0x4223a4: SIGABRT handler (0 default, 1 ignore) */
    uint8_t sigabrt_extra;             /* 0x4223d9: second handler argument when nonzero */
    uint32_t abort_message;            /* address of "Abnormal program termination" (CW3220 0x422298) */
    int exited;                        /* the model has called exit_process */
    /* (all 0 after es2_rtl_runtime_init: the plain lifecycle model). */
    uint32_t code_base;                /* CW3220's load address: return-address words are code_base + RVA */
    es2_rtl_cpu entry;                 /* the registers at the entry of the next export the caller runs */
    uint32_t stream_hooks;             /* guest address of the three hook words (0x4222bc) instead of the fields */
    uint32_t sigabrt_slot;             /* guest address of the SIGABRT entry (0x4223a4) instead of `sigabrt` */
    uint32_t sigabrt_extra_slot;       /* guest address of its extra byte (0x4223d9) instead of `sigabrt_extra` */
} es2_rtl_runtime;

typedef enum es2_rtl_call_kind {
    ES2_RTL_CALL_INIT_RECORD,
    ES2_RTL_CALL_WINMAIN,
    ES2_RTL_CALL_MAIN,
    ES2_RTL_CALL_ATEXIT,
    ES2_RTL_CALL_EXIT_RECORD,
    ES2_RTL_CALL_EXITBUF,
    ES2_RTL_CALL_EXITFOPEN,
    ES2_RTL_CALL_EXITOPEN,
    ES2_RTL_CALL_SIGNAL
} es2_rtl_call_kind;

typedef struct es2_rtl_guest_call {
    es2_rtl_call_kind kind;
    uint32_t target;
    uint32_t esp;          /* ESP at the target's entry (its return address slot) */
    uint32_t arg_count;
    uint32_t args[4];      /* args[0] at esp + 4 */
    uint32_t result;       /* out: EAX */
    uint32_t popped;       /* out: argument bytes the callee removed (stdcall: 4 * arg_count) */
    /* */
    uint32_t record;       /* init and exit records: the record's address; else 0 */
    uint32_t return_rva;   /* the original's return address at esp, as a CW3220 RVA */
    es2_rtl_cpu regs;      /* the registers the original holds at the target's entry */
    es2_rtl_cpu out;       /* out: the registers at the return (preset to regs; EAX is `result`) */
} es2_rtl_guest_call;

/* The Win32 calls, in the order of the platform callbacks below. */
typedef enum es2_rtl_api {
    ES2_RTL_API_ENVIRONMENT_STRINGS,
    ES2_RTL_API_COMMAND_LINE,
    ES2_RTL_API_MODULE_HANDLE,
    ES2_RTL_API_MODULE_FILE_NAME,
    ES2_RTL_API_VERSION,
    ES2_RTL_API_THREAD_ID,
    ES2_RTL_API_ENUM_THREAD_WINDOWS,
    ES2_RTL_API_MESSAGE_BOX,
    ES2_RTL_API_EXIT_PROCESS,
    ES2_RTL_API_COUNT
} es2_rtl_api;

/* Where the original makes the next Win32 call. */
typedef struct es2_rtl_site {
    es2_rtl_api api;
    uint32_t esp;          /* ESP at the API's entry (its return address slot) */
    uint32_t return_rva;   /* the original's return address there, as a CW3220 RVA */
    uint32_t guest;        /* guest memory the original passes: GetModuleFileNameA's buffer,
                            * EnumThreadWindows' flag, MessageBoxA's title; else 0 */
    es2_rtl_cpu cpu;       /* the registers at the API's entry (EBX, ESI, EDI, EBP as the original;
                            * EAX, ECX, EDX where the original defines them); an env may store the
                            * registers the API returns here before its platform callback returns */
} es2_rtl_site;

typedef struct es2_rtl_start_env {
    void *context;
    uint8_t (*read8)(void *context, uint32_t address);
    void (*write8)(void *context, uint32_t address, uint8_t value);
    uint32_t (*read32)(void *context, uint32_t address);
    void (*call)(void *context, es2_rtl_guest_call *call);
    /* fninit, then the control word (fldcw). */
    void (*fpu_reset)(void *context, uint16_t control);
    /* Links an SEH record at `record`: [record] = FS:0, [record+4] = the
     * handler (CW3220 RVA), FS:0 = record. */
    void (*register_frame)(void *context, uint32_t record, uint32_t handler_rva);
    uint32_t (*environment_strings)(void *context);           /* GetEnvironmentStrings() */
    uint32_t (*command_line)(void *context);                  /* GetCommandLineA() */
    uint32_t (*module_handle)(void *context, uint32_t name);  /* GetModuleHandleA(name) */
    /* GetModuleFileNameA(NULL, buffer, size): the buffer is zero-filled
     * before the call; returns the API's result. */
    uint32_t (*module_file_name)(void *context, char *buffer, uint32_t size);
    uint32_t (*version)(void *context);                       /* GetVersion() */
    uint32_t (*thread_id)(void *context);                     /* GetCurrentThreadId() */
    /* EnumThreadWindows(thread, CW3220's callback, &flag): nonzero when the
     * callback ran (the thread has a window). */
    int (*thread_has_window)(void *context, uint32_t thread);
    /* MessageBoxA(NULL, text, title, style); esp at MessageBoxA's entry. */
    void (*message_box)(void *context, uint32_t text, const char *title, uint32_t style, uint32_t esp);
    /* ExitProcess(status); esp at its entry. Does not return in the original. */
    void (*exit_process)(void *context, uint32_t status, uint32_t esp);
    /* Optional (NULL: not used). write32: one guest dword; the model
     * writes the frame words of the original's functions with it (and the
     * SIGABRT reset when sigabrt_slot is set). site: called right before
     * each platform callback above with the original's ESP, return address,
     * guest memory and registers of that Win32 call; the pointer stays valid
     * until the platform callback returns. */
    void (*write32)(void *context, uint32_t address, uint32_t value);
    void (*site)(void *context, es2_rtl_site *site);
} es2_rtl_start_env;

enum { ES2_RTL_RETURNED = 0, ES2_RTL_EXITED = 1 };

/* State as CW3220 has it after its own load (before __startup): everything
 * 0, the three stream hooks at `dummy_hook` (CW3220's 0x4191a0, a bare ret),
 * the abort message address. */
void es2_rtl_runtime_init(es2_rtl_runtime *rt, uint32_t dummy_hook, uint32_t abort_message);

/* __startup(table) with ESP `esp` at its entry ([esp] = return address,
 * [esp+4] = table) and rt->entry the registers there. Runs the init
 * records, WinMain or main, then exit with its result. */
int es2_rtl_startup(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp, uint32_t table);

/* The record selector (0x41a634) over the runtime's module list: mode 0
 * picks the first pending init record of the lowest priority, mode 1 the
 * last pending exit record of the highest; marks it done and returns its
 * function, or 0. */
uint32_t es2_rtl_select_record(const es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t mode);

/* lpCmdLine: the command line after the program name and the blanks after it. */
uint32_t es2_rtl_command_tail(const es2_rtl_start_env *env, uint32_t command_line);

/* atexit(fn): 0, or 1 when the table holds 32 functions. */
uint32_t es2_rtl_atexit(es2_rtl_runtime *rt, uint32_t function);

/* The exports with `esp` at their entry and rt->entry the caller's
 * registers there. */
int es2_rtl_exit(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp, uint32_t status);       /* exit */
int es2_rtl_quick_exit(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp, uint32_t status); /* _exit (CW3220 __exit) */
int es2_rtl_cexit(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp);                       /* _cexit */
int es2_rtl_c_exit(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp);                      /* _c_exit */
int es2_rtl_abort(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp);
int es2_rtl_error_exit(es2_rtl_runtime *rt, const es2_rtl_start_env *env, uint32_t esp, uint32_t message);
/* __ErrorMessage(text) with `esp` at its entry: a message box titled with
 * the module's file name after the last '\' or ':'. */
void es2_rtl_error_message(const es2_rtl_start_env *env, uint32_t esp, uint32_t text);

#endif
