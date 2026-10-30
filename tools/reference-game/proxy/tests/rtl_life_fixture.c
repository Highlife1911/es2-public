/* Own-code fixture for the lifecycle group (proxy/rtl_life.h) through
 * the real arming path, with the proxy's own exit hooks installed on top in
 * the order the proxy uses (live coverage, then the speed runtime;
 * rtl_imports.md, section "the lifecycle group as own code"). A synthetic DBSIM-shaped image and a
 * synthetic CW3220 image (the words the lifecycle verifies, its variables,
 * the nine Win32 thunks with their import slots pointing to stubs of this
 * file) are built in memory; no original game code runs.
 *
 * Modes: `order` (WinMain installs both hooks and returns: the own atexit
 * table holds the record function's registration, live coverage's and the
 * speed runtime's in that order, the own exit runs them last first, then the
 * exit record, the stream hooks, ExitProcess with WinMain's result),
 * `quick` (WinMain installs both and calls __exit through DBSIM's thunk: live
 * coverage's slot, the speed runtime's chain, the shim's thunk, the own quick
 * exit, ExitProcess with its status), `fields` (the exit record's lifecycle
 * fields at the end of `order`). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
static WCHAR config_path[MAX_PATH];
static HMODULE self_module;
static void log_event(const char *s) { (void)s; }
#include "../oracle_perf.h"
#include "../virtual_time.h"
#include "../input_journal.h"
#include "../rng_hooks.h"
#define ES2_RTL_NO_PROCESS_ENTRY
#include "../rtl_imports.h"
#include "../live_coverage.h"
#include "../tick_log.h"
#include "../oracle_perf_runtime.h"
void __cdecl __real_es2_hook_dispatch_extended(void *frame) { (void)frame; }

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

/* ---- entry and escape: DBSIM's `push 0; jmp <__startup thunk>` with the
 * table above it; ExitProcess comes back here. ---- */
__attribute__((used)) static uint32_t fx_host_esp, fx_host_fs0, fx_target, fx_argument;
__asm__(
    ".text\n"
    "_fx_enter:\n"                                  /* cdecl fx_enter(target, argument) */
    "    pushl %ebp\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    movl %esp, _fx_host_esp\n"
    "    movl %fs:0, %eax\n    movl %eax, _fx_host_fs0\n"
    "    movl 20(%esp), %eax\n    movl %eax, _fx_target\n    movl 24(%esp), %eax\n"
    "    subl $256, %esp\n"                          /* room for the host frame's red zone */
    "    pushl %eax\n    pushl $0\n"
    "    movl $0x0e0e0e0e, %ebx\n    movl $0x51515151, %esi\n    movl $0xd1d1d1d1, %edi\n    movl $0xe0e0e0e0, %ebp\n"
    "    jmp *_fx_target\n"
    "_fx_escape:\n"
    "    movl _fx_host_fs0, %eax\n    movl %eax, %fs:0\n"
    "    movl _fx_host_esp, %esp\n"
    "    popl %edi\n    popl %esi\n    popl %ebx\n    popl %ebp\n    cld\n    ret\n");
extern void __cdecl fx_enter(uint32_t target, uint32_t argument) __asm__("_fx_enter");
extern void __cdecl fx_escape(void) __asm__("_fx_escape") __attribute__((noreturn));

/* ---- the synthetic DBSIM image ---- */
#define FX_IMPORT_DIR 0x002cc000u
#define FX_LOOKUP 0x002cc0a0u
#define FX_NAMES 0x002cc400u
#define FX_DLL_NAME 0x002cc800u
static BYTE *image, *cw;
static uint32_t exports[RTL_IMPORT_COUNT];
static void put32(BYTE *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t va(uint32_t rva) { return (uint32_t)(uintptr_t)image + rva; }
static uint32_t thunk_va(unsigned i) { return va(RTL_THUNK_RVA + 6 * i); }
static uint32_t *slot(unsigned i) { return (uint32_t *)(image + RTL_IAT_RVA + 4 * i); }
__asm__(".text\n_fx_plain_export:\n    movl $0x600d600d, %eax\n    ret\n");
extern void fx_plain_export(void) __asm__("_fx_plain_export");
static FARPROC fx_resolve(const char *symbol, void *context) {
    (void)context;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i)
        if (!strcmp(rtl_imports[i].symbol, symbol)) return (FARPROC)(uintptr_t)exports[i];
    return NULL;
}
static void call_stub(BYTE *at, uint32_t target) { at[0] = 0xe8; put32(at + 1, target - ((uint32_t)(uintptr_t)at + 5)); at[5] = 0xc3; }

/* ---- the synthetic CW3220: the lifecycle's words and the Win32 stubs ---- */
static uint32_t fx_status = 0xffffffffu, fx_boxes;
static int fx_exited;
static char fx_command[] = "FX.EXE -X5";
static LPCH WINAPI fx_environment(void) { return (LPCH)"A=1\0"; }
static LPSTR WINAPI fx_command_line(void) { return fx_command; }
static HMODULE WINAPI fx_module(LPCSTR name) { (void)name; return (HMODULE)(uintptr_t)0x10000000u; }
static DWORD WINAPI fx_file(HMODULE m, LPSTR buffer, DWORD size) { (void)m; snprintf(buffer, size, "C:\\FX\\FX.EXE"); return 12; }
static DWORD WINAPI fx_version(void) { return 0x0a280105u; }
static DWORD WINAPI fx_thread(void) { return 1; }
static BOOL WINAPI fx_enum(DWORD t, WNDENUMPROC p, LPARAM l) { (void)t; (void)p; (void)l; return TRUE; }
static int WINAPI fx_box(HWND w, LPCSTR text, LPCSTR title, UINT style) { (void)w; (void)text; (void)title; (void)style; ++fx_boxes; return IDOK; }
static void WINAPI fx_exit_process(UINT status) { fx_status = status; fx_exited = 1; fx_escape(); }
static const void *const fx_apis[ES2_RTL_API_COUNT] = {
    (const void *)fx_environment, (const void *)fx_command_line, (const void *)fx_module, (const void *)fx_file, (const void *)fx_version,
    (const void *)fx_thread, (const void *)fx_enum, (const void *)fx_box, (const void *)fx_exit_process,
};
static int make_cw(void) {
    cw = VirtualAlloc(NULL, RTL_HEAP_CW_IMAGE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    CHECK(cw);
    uint32_t base = (uint32_t)(uintptr_t)cw;
    cw[0] = 'M'; cw[1] = 'Z'; put32(cw + 0x3c, 0x80); put32(cw + 0x80, 0x00004550u);
    put32(cw + 0x80 + 24 + 56, RTL_HEAP_CW_IMAGE);
    for (unsigned k = 0; k < sizeof(rtl_life_operands) / sizeof(rtl_life_operands[0]); ++k)
        put32(cw + rtl_life_operands[k][0], base + rtl_life_operands[k][1]);
    for (unsigned k = 0; k < ES2_RTL_API_COUNT; ++k) {
        BYTE *t = cw + rtl_life_apis[k].thunk;
        t[0] = 0xff; t[1] = 0x25; put32(t + 2, base + rtl_life_apis[k].slot);
        BOOL user = rtl_life_apis[k].slot >= RTL_LIFE_U32_IAT;
        uint32_t lookup = user ? RTL_LIFE_U32_LOOKUP + (rtl_life_apis[k].slot - RTL_LIFE_U32_IAT) : RTL_LIFE_K32_LOOKUP + (rtl_life_apis[k].slot - RTL_LIFE_K32_IAT);
        uint32_t name = 0x2d000u + 64u * k;
        put32(cw + lookup, name);
        memcpy(cw + name + 2, rtl_life_apis[k].name, strlen(rtl_life_apis[k].name) + 1);
        put32(cw + rtl_life_apis[k].slot, (uint32_t)(uintptr_t)fx_apis[k]);
    }
    cw[RTL_LIFE_DUMMY_HOOK] = 0xc3;   /* the bare ret of the stream hooks */
    for (unsigned k = 0; k < 3; ++k) put32(cw + RTL_LIFE_STREAM_HOOKS + 4 * k, base + RTL_LIFE_DUMMY_HOOK);
    memcpy(cw + RTL_LIFE_ABORT_MESSAGE, "Abnormal program termination", 29);
    return 0;
}
static int make_image(void) {
    image = VirtualAlloc(NULL, RTL_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    CHECK(image);
    image[0] = 'M'; image[1] = 'Z'; put32(image + 0x3c, 0x80);
    BYTE *nt = image + 0x80;
    put32(nt, 0x00004550u); nt[4] = 0x4c; nt[5] = 0x01;
    nt[24] = 0x0b; nt[25] = 0x01;
    put32(nt + 24 + 28, (uint32_t)(uintptr_t)image);
    put32(nt + 24 + 56, RTL_IMAGE_SIZE);
    put32(nt + 24 + 92, 16);                         /* NumberOfRvaAndSizes (live_import_slot) */
    put32(nt + 128, FX_IMPORT_DIR); put32(nt + 132, 40);
    BYTE *d = image + FX_IMPORT_DIR;
    put32(d, FX_LOOKUP); put32(d + 12, FX_DLL_NAME); put32(d + 16, RTL_IAT_RVA);
    memcpy(image + FX_DLL_NAME, "cw3220.DLL", 11);
    uint32_t name = FX_NAMES;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        put32(image + FX_LOOKUP + 4 * i, name);
        size_t length = strlen(rtl_imports[i].symbol) + 1;
        memcpy(image + name + 2, rtl_imports[i].symbol, length);
        name += (uint32_t)((2 + length + 1) & ~1u);
        BYTE *t = image + RTL_THUNK_RVA + 6 * i;
        t[0] = 0xff; t[1] = 0x25; put32(t + 2, va(RTL_IAT_RVA + 4 * i));
        exports[i] = (uint32_t)(uintptr_t)fx_plain_export;
    }
    /* The group's exports are CW3220's (the lifecycle checks them). */
    for (unsigned k = 0; k < RTL_LIFE_GROUP; ++k) exports[rtl_life_group[k].import] = (uint32_t)(uintptr_t)cw + rtl_life_group[k].rva;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) *slot(i) = exports[i];
    for (unsigned i = 0; i < RTL_SIM_SITE_COUNT; ++i) call_stub(image + rtl_sim_sites[i].ret_rva - 5, thunk_va(rtl_sim_sites[i].import));
    return 0;
}

/* ---- the synthetic module: one init record, one exit record, WinMain ---- */
static unsigned fx_mode;   /* 0 order, 1 quick */
static int fx_failed;
static uint32_t fx_atexit_order[8], fx_fmode_word = 0x4000u;
static unsigned fx_record_ran, fx_exit_record_ran, fx_own_callback_ran;
static char fx_shutdowns[8][24];
static unsigned fx_shutdown_count, fx_shutdowns_seen_by_callback;
static BOOL fx_live_ok, fx_speed_ok;
static void fx_shutdown(const char *reason) { if (fx_shutdown_count < 8) snprintf(fx_shutdowns[fx_shutdown_count], 24, "%s", reason); ++fx_shutdown_count; }
static void __cdecl fx_own_callback(void) { ++fx_own_callback_ran; fx_shutdowns_seen_by_callback = fx_shutdown_count; }
typedef int (__cdecl *fx_atexit_fn)(void (__cdecl *)(void));
static void __cdecl fx_init_record(void) {
    ++fx_record_ran;
    /* DBSIM's own registration (a device constructor's), through its thunk. */
    if (((fx_atexit_fn)(uintptr_t)thunk_va(RTL_I_atexit))(fx_own_callback)) fx_failed = 1;
}
static void __cdecl fx_exit_record(void) { ++fx_exit_record_ran; }
static uint32_t __stdcall fx_winmain(uint32_t instance, uint32_t previous, uint32_t command, uint32_t show) {
    (void)previous;
    if (instance != 0x10000000u || show != 10u || strcmp((const char *)(uintptr_t)command, "-X5")) fx_failed = 2;
    /* As initialize_forwarding: live coverage's exit hooks, then the speed runtime's. */
    live_extra_shutdown = fx_shutdown;
    fx_live_ok = live_install_exit(image, RTL_IMAGE_SIZE);
    fx_speed_ok = speed_install_exit(image);
    for (unsigned i = 0; i < rtl_life_rt.atexit_count && i < 8; ++i) fx_atexit_order[i] = rtl_life_rt.atexit_table[i];
    if (fx_mode == 1) ((void (__cdecl *)(int))(uintptr_t)thunk_va(RTL_I_exit))(7);
    return 5;
}
static uint8_t fx_table_memory[0x80];
static uint32_t fx_table(void) {
    BYTE *t = fx_table_memory;
    uint32_t base = (uint32_t)(uintptr_t)t;
    put32(t + 0x00, base + 0x40); put32(t + 0x04, base + 0x46);
    put32(t + 0x08, base + 0x50); put32(t + 0x0c, base + 0x56);
    put32(t + 0x10, 1); put32(t + 0x18, (uint32_t)(uintptr_t)fx_winmain);
    put32(t + 0x1c, 0x11111111u); put32(t + 0x20, 0x22222222u); put32(t + 0x28, (uint32_t)(uintptr_t)&fx_fmode_word);
    t[0x40] = 0; t[0x41] = 0x20; put32(t + 0x42, (uint32_t)(uintptr_t)fx_init_record);
    t[0x50] = 0; t[0x51] = 0x20; put32(t + 0x52, (uint32_t)(uintptr_t)fx_exit_record);
    return base;
}

/* ---- the arming step (ES2_RTL_NO_PROCESS_ENTRY): the group installs and
 * the arming continues in the own start-up, as rtl_arm does. ---- */
static void __cdecl fx_refused(void) { fx_failed = 9; fx_escape(); }
static void fx_options(rtl_options *o, uintptr_t stack_lo, uintptr_t stack_hi) {
    memset(o, 0, sizeof(*o));
    o->image = image; o->expected_base = (uintptr_t)image; o->image_size = RTL_IMAGE_SIZE;
    o->resolve = fx_resolve;
    o->stack_lo = stack_lo; o->stack_hi = stack_hi;
    rtl_module_range((const void *)fx_options, &o->hook_lo, &o->hook_hi);
    for (unsigned k = 0; k < RTL_LIFE_GROUP; ++k) { o->selected[rtl_life_group[k].import] = TRUE; o->modes[rtl_life_group[k].import] = ES2_HOOK_REPLACED; }
    o->selected[RTL_I_memset] = TRUE;   /* an ordinary import beside the group */
    o->cw_image = cw;
}
static void rtl_arm(void) {
    rtl_options o;
    fx_options(&o, rtl_stack_floor((const void *)(uintptr_t)rtl_entry_esp), rtl_entry_stack_base);
    BOOL ok = rtl_install(&o);
    if (!ok) fprintf(stderr, "lifecycle refused: %s\n", rtl_reason);
    rtl_startup_target = ok && rtl_life_active ? (uint32_t)(uintptr_t)rtl_life_startup_entry : (uint32_t)(uintptr_t)fx_refused;
    InterlockedExchange(&rtl_state, ok ? RTL_INSTALLED : RTL_REFUSED);
}

static int run(unsigned mode) {
    fx_mode = mode;
    CHECK(!make_cw() && !make_image());
    CHECK(rtl_install_startup_image(image, (uintptr_t)image, RTL_IMAGE_SIZE, fx_resolve, NULL));
    CHECK(*slot(RTL_STARTUP_INDEX) == (uint32_t)(uintptr_t)rtl_startup_entry);
    fx_enter(thunk_va(RTL_STARTUP_INDEX), fx_table());
    CHECK(!fx_failed && fx_exited && rtl_state == RTL_INSTALLED && rtl_life_active);
    /* The start-up: the record, WinMain; the arming slot back to CW3220's export. */
    CHECK(fx_record_ran == 1 && rtl_life_init_records == 1 && rtl_life_winmain == 1 && *slot(RTL_STARTUP_INDEX) == exports[RTL_STARTUP_INDEX]);
    CHECK(*(uint32_t *)(cw + RTL_LIFE_FMODE) == (uint32_t)(uintptr_t)&fx_fmode_word && *(uint32_t *)(cw + RTL_LIFE_MODULE_COUNT) == 1);
    /* Both hooks installed over the shim's thunks: the own atexit table holds
     * DBSIM's registration, live coverage's, the speed runtime's. */
    CHECK(fx_live_ok && fx_speed_ok && live_previous_exit == speed_exit && speed_previous_exit == (void *)(uintptr_t)rtl_thunk_table[RTL_I_exit]);
    CHECK(fx_atexit_order[0] == (uint32_t)(uintptr_t)fx_own_callback && fx_atexit_order[1] == (uint32_t)(uintptr_t)live_coverage_on_exit &&
          fx_atexit_order[2] == (uint32_t)(uintptr_t)speed_on_exit);
    CHECK(rtl_life_registered == 3 && rtl_counters[RTL_I_atexit].calls == 3 && rtl_counters[RTL_I_atexit].hook == 3);
    if (mode == 0) {
        /* exit(5): last registered first, then the exit record, ExitProcess(5). */
        CHECK(fx_status == 5 && rtl_life_atexit_run == 3 && rtl_life_exit_records == 1 && fx_exit_record_ran == 1 && rtl_life_hooks == 3);
        CHECK(fx_own_callback_ran == 1 && fx_shutdowns_seen_by_callback == 3 && fx_shutdown_count == 3);
        CHECK(!strcmp(fx_shutdowns[0], "exit") && !strcmp(fx_shutdowns[1], "tick_shutdown") && !strcmp(fx_shutdowns[2], "exit"));
        CHECK(rtl_life_rt.atexit_count == 0 && rtl_life_status == 5 && rtl_counters[RTL_I_exit].calls == 0);
    } else {
        /* __exit(7) from WinMain: live coverage's slot, the speed runtime's
         * chain, the shim's thunk (counted), the own quick exit. */
        CHECK(fx_status == 7 && rtl_life_atexit_run == 0 && rtl_life_exit_records == 0 && fx_own_callback_ran == 0);
        CHECK(rtl_counters[RTL_I_exit].calls == 1 && rtl_counters[RTL_I_exit].hook == 1 && rtl_life_status == 7);
        CHECK(fx_shutdown_count == 3 && !strcmp(fx_shutdowns[0], "exit") && !strcmp(fx_shutdowns[1], "exit") && !strcmp(fx_shutdowns[2], "tick_shutdown"));
    }
    /* Live coverage restored its slot to the shim's thunk; the shim then
     * restores every slot it owns. */
    CHECK(*slot(RTL_I_exit) == rtl_thunk_table[RTL_I_exit] && !live_exit_slot);
    CHECK(rtl_foreign_calls == 0 && fx_boxes == 0);
    rtl_life_current = 0;
    CHECK(rtl_uninstall() && *slot(RTL_I_exit) == exports[RTL_I_exit] && *slot(RTL_I_atexit) == exports[RTL_I_atexit] && !rtl_life_active);
    return 0;
}

static int fields_case(void) {
    CHECK(!run(0));
    /* After uninstall the counters remain; report them as an installed group would. */
    rtl_life_active = TRUE;
    static char line[4096];
    int n = rtl_life_fields(line, sizeof(line), TRUE);
    CHECK(n > 0 && !strcmp(line, ",\"rtl_life_init_records\":1,\"rtl_life_winmain\":1,\"rtl_life_atexit_registered\":3,"
                                 "\"rtl_life_atexit_refused\":0,\"rtl_life_atexit_pending\":0,\"rtl_life_atexit_run\":3,"
                                 "\"rtl_life_exit_records\":1,\"rtl_life_stream_hooks\":3,\"rtl_life_signals\":0,"
                                 "\"rtl_life_exit_status\":5,\"rtl_life_cw_atexit\":0"));
    n = rtl_life_fields(line, sizeof(line), FALSE);
    CHECK(n > 0 && !strcmp(line, ",\"rtl_life_init_records\":1,\"rtl_life_winmain\":1"));
    CHECK(rtl_life_fields(line, 20, TRUE) == 0);
    rtl_life_active = FALSE;
    puts("PASS fields: the lifecycle's tick and exit-record fields after an own start-up and exit");
    return 0;
}

/* The CW3220 checks: each refusal leaves both images as they were. */
static BYTE *snapshot_image, *snapshot_cw;
static int unchanged(void) { return !memcmp(snapshot_image, image, RTL_IMAGE_SIZE) && !memcmp(snapshot_cw, cw, RTL_HEAP_CW_IMAGE); }
static int refuse_case(void) {
    CHECK(!make_cw() && !make_image());
    snapshot_image = malloc(RTL_IMAGE_SIZE); snapshot_cw = malloc(RTL_HEAP_CW_IMAGE);
    CHECK(snapshot_image && snapshot_cw);
    volatile BYTE here = 0;
    uintptr_t lo = rtl_stack_floor((const void *)&here), hi = (uintptr_t)((NT_TIB *)NtCurrentTeb())->StackBase;
    rtl_options o;
    struct { uint32_t rva, value; const char *reason; } cases[] = {
        {0, 0, "cw_life_export_abort"},                                   /* the abort export elsewhere */
        {rtl_life_operands[5][0], 0x1234u, "cw_life_operand"},             /* the module table's operand */
        {rtl_life_apis[7].thunk + 2u, 0x5678u, "cw_life_imports"},         /* MessageBoxA's thunk names another slot */
        {rtl_life_apis[8].slot, 0, "cw_life_imports"},                     /* ExitProcess not bound */
        {RTL_LIFE_ATEXIT_COUNT, 33, "cw_life_atexit"},                     /* CW3220's atexit table past its end */
        {0x3cu, 0x40u, "cw_image"},                                        /* no PE header */
    };
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        uint32_t saved = 0, saved_export = exports[RTL_I_abort];
        if (cases[c].rva) { memcpy(&saved, cw + cases[c].rva, 4); put32(cw + cases[c].rva, cases[c].value); }
        else exports[RTL_I_abort] = (uint32_t)(uintptr_t)fx_plain_export, *slot(RTL_I_abort) = exports[RTL_I_abort];
        memcpy(snapshot_image, image, RTL_IMAGE_SIZE); memcpy(snapshot_cw, cw, RTL_HEAP_CW_IMAGE);
        fx_options(&o, lo, hi);
        CHECK(!rtl_install(&o));
        if (strcmp(rtl_reason, cases[c].reason)) { fprintf(stderr, "case %u: reason %s, want %s\n", c, rtl_reason, cases[c].reason); return 1; }
        CHECK(unchanged() && !rtl_life_active && !rtl_installed_image);
        if (cases[c].rva) put32(cw + cases[c].rva, saved);
        else exports[RTL_I_abort] = saved_export, *slot(RTL_I_abort) = saved_export;
    }
    /* Intact: installs, adopts CW3220's atexit table, uninstalls byte-exactly. */
    put32(cw + RTL_LIFE_ATEXIT_COUNT, 2); put32(cw + RTL_LIFE_ATEXIT_TABLE, 0x1111u); put32(cw + RTL_LIFE_ATEXIT_TABLE + 4, 0x2222u);
    memcpy(snapshot_image, image, RTL_IMAGE_SIZE); memcpy(snapshot_cw, cw, RTL_HEAP_CW_IMAGE);
    fx_options(&o, lo, hi);
    CHECK(rtl_install(&o) && rtl_life_active && rtl_life_rt.atexit_count == 2 && rtl_life_rt.atexit_table[1] == 0x2222u);
    CHECK(*slot(RTL_STARTUP_INDEX) == exports[RTL_STARTUP_INDEX] && *slot(RTL_I_atexit) == rtl_thunk_table[RTL_I_atexit] &&
          rtl_targets[RTL_I_atexit] == rtl_replaced_table[RTL_I_atexit] && rtl_targets[RTL_I_exit] == (uint32_t)(uintptr_t)rtl_life_exit_entry &&
          rtl_targets[RTL_I_abort] == (uint32_t)(uintptr_t)rtl_life_abort_entry && rtl_targets[RTL_I_ErrorExit] == (uint32_t)(uintptr_t)rtl_life_error_entry);
    CHECK(rtl_uninstall() && unchanged() && !rtl_life_active);
    puts("PASS refuse: CW3220 export, operand, Win32 thunk and slot, atexit table and image refusals leave both images "
         "byte-identical; an intact install adopts CW3220's atexit table and uninstalls byte-exactly");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "refuse")) return refuse_case();
    if (!strcmp(argv[1], "order")) {
        if (run(0)) return 1;
        puts("PASS order: the arming ran the own start-up; DBSIM's registration, live coverage's and the speed runtime's "
             "in the own atexit table in that order, run last first by the own exit; exit record, stream hooks, ExitProcess(5)");
        return 0;
    }
    if (!strcmp(argv[1], "quick")) {
        if (run(1)) return 1;
        puts("PASS quick: __exit(7) through live coverage's slot, the speed runtime's chain and the shim's thunk to the own "
             "quick exit; no atexit function, ExitProcess(7)");
        return 0;
    }
    if (!strcmp(argv[1], "fields")) return fields_case();
    return 2;
}
