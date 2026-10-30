/* es2sim's process on Windows (es2/sim/os_process.h) and its entry point. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "es2/platform/os.h"
#include "es2/sim/host.h"
#include "es2/sim/os_process.h"

static BYTE *reserved;
static DWORD reservation_error;
static int reservation_in_stack;

extern void __cdecl mainCRTStartup(void);
#include "tools/reference-game/invitro/startup_reservation.h"
void __cdecl es2sim_start(void);
void __cdecl es2sim_start(void) {
    /* Claim 0x400000..0x6d7fff before the CRT or any other thread can
     * allocate there (startup_reservation.h); this thread then runs the CRT
     * and main, which only starts and joins the simulation thread. */
    MEMORY_BASIC_INFORMATION here = {0};
    VirtualQuery(&here, &here, sizeof(here));
    reserved = claim_image_range(here.AllocationBase, &reservation_error, &reservation_in_stack);
    mainCRTStartup();
    es2_os_exit(ES2_SIM_EXIT_SETUP);
}

uint8_t *es2_sim_os_reserved(uint32_t *error) {
    *error = reservation_error;
    return reserved;
}

static void (*unhandled_report)(void *exception_pointers);
static LONG WINAPI unhandled(EXCEPTION_POINTERS *x) {
    unhandled_report(x);
    return EXCEPTION_EXECUTE_HANDLER;
}
void es2_sim_os_on_unhandled(void (*report)(void *exception_pointers)) {
    unhandled_report = report;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(unhandled);
}

void es2_sim_os_exception_decode(const void *exception_pointers, es2_sim_os_exception *out) {
    const EXCEPTION_RECORD *r = ((const EXCEPTION_POINTERS *)exception_pointers)->ExceptionRecord;
    out->code = (uint32_t)r->ExceptionCode;
    out->parameters = (unsigned)r->NumberParameters;
    out->information[0] = r->ExceptionInformation[0];
    out->information[1] = r->ExceptionInformation[1];
    out->address = r->ExceptionAddress;
    out->record = r;
}

static uint32_t (*thread_body)(void);
static DWORD WINAPI thread_entry(void *argument) {
    (void)argument;
    return thread_body();
}
int es2_sim_os_run_thread(uint32_t (*body)(void), uint32_t stack_reserve, uint32_t *result) {
    thread_body = body;
    HANDLE worker = CreateThread(NULL, stack_reserve, thread_entry, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!worker) return 0;
    DWORD code = 0;
    BOOL joined = WaitForSingleObject(worker, INFINITE) == WAIT_OBJECT_0 && GetExitCodeThread(worker, &code);
    CloseHandle(worker);
    *result = code;
    return joined != 0;
}

uint32_t es2_sim_os_stack_base(void) { return (uint32_t)(uintptr_t)((NT_TIB *)NtCurrentTeb())->StackBase; }

unsigned es2_sim_os_vk_scan(unsigned vk) { return MapVirtualKeyA(vk, 0 /* MAPVK_VK_TO_VSC */); }
int es2_sim_os_to_ascii(uint32_t vk, uint32_t scan, uint16_t *out) {
    BYTE state[256] = {0};
    WORD chars = 0;
    int n = ToAscii(vk, scan, state, &chars, 0);
    *out = chars;
    return n;
}

uint32_t es2_sim_os_message_box(uint32_t owner, const char *text, const char *caption, uint32_t style) {
    return (uint32_t)MessageBoxA((HWND)(uintptr_t)owner, text, caption, (UINT)style);
}
