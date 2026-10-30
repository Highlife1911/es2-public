/* src/host_fault.c: a write fault of own code under a frame record on FS:0
 * whose handler lies in a no-access page (as DBSIM's 0x4967f0 in the CODE
 * range) ends a child process through the callback (exit 71); without the
 * handler the OS refuses the record's handler and the child's unhandled-
 * exception filter (the same callback) is never reached. A child whose
 * callback ends NULL-page faults with their code (as es2sim's inside a guest
 * entry) ends with 0xc0000005 for the write to 0x1b and with 71 for a write
 * to a no-access page above the NULL page. Plus the ownership and NULL-page
 * tests on codes and addresses. No game data. */
#include "es2/sim/host_fault.h"
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
#define CHILD_EXIT 71u

typedef struct frame_record { struct frame_record *next; void *handler; } frame_record;

static void on_fault(void *pointers) {
    const EXCEPTION_RECORD *r = ((EXCEPTION_POINTERS *)pointers)->ExceptionRecord;
    fprintf(stderr, "child: own fault 0x%08lx at %p\n", (unsigned long)r->ExceptionCode, r->ExceptionAddress);
    fflush(stderr);
    ExitProcess(CHILD_EXIT);
}

static void on_fault_null_page(void *pointers) {
    const EXCEPTION_RECORD *r = ((EXCEPTION_POINTERS *)pointers)->ExceptionRecord;
    unsigned address;
    if (es2_sim_host_fault_null_page(r, &address)) ExitProcess((UINT)r->ExceptionCode);
    on_fault(pointers);
}

static LONG WINAPI filter(EXCEPTION_POINTERS *x) { on_fault(x); return EXCEPTION_EXECUTE_HANDLER; }

static __attribute__((noinline)) void write_through(volatile uint16_t *p) { *p = 1; }

/* Links a record with a no-access handler on FS:0 and writes to 0x1b (the
 * garbage gadget's +0x1b of the DEMO1 list overrun), or with `high` to the
 * no-access page itself. `null_page`: the callback ends NULL-page faults with
 * their code. */
static int child(int with_handler, int null_page, int high) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    void *page = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    if (!page) return 2;
    SetUnhandledExceptionFilter(filter);
    if (with_handler && es2_sim_host_fault_install(null_page ? on_fault_null_page : on_fault)) return 3;
    frame_record record;
    frame_record *previous;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(previous));
    record.next = previous;
    record.handler = (uint8_t *)page + 0x7f0;
    __asm__ volatile("movl %0, %%fs:0" : : "r"(&record) : "memory");
    write_through(high ? (volatile uint16_t *)page : (volatile uint16_t *)(uintptr_t)0x1bu);
    __asm__ volatile("movl %0, %%fs:0" : : "r"(previous) : "memory");
    return 4;
}

static int run_child(const char *mode, DWORD *code) {
    char exe[MAX_PATH], line[MAX_PATH + 32];
    if (!GetModuleFileNameA(NULL, exe, MAX_PATH)) return 1;
    snprintf(line, sizeof line, "\"%s\" %s", exe, mode);
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (!CreateProcessA(NULL, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return 1;
    int bad = WaitForSingleObject(pi.hProcess, 60000) != WAIT_OBJECT_0 || !GetExitCodeProcess(pi.hProcess, code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return bad;
}

static int owned(void) {
    CHECK(!es2_sim_host_fault_owned(0xc0000005ul, (const void *)(uintptr_t)write_through));
    CHECK(es2_sim_host_fault_install(NULL) == 1);
    CHECK(es2_sim_host_fault_install(on_fault) == 0);
    CHECK(es2_sim_host_fault_install(on_fault) == 1);
    CHECK(es2_sim_host_fault_owned(0xc0000005ul, (const void *)(uintptr_t)write_through));
    CHECK(es2_sim_host_fault_owned(0xc0000094ul, (const void *)(uintptr_t)child));
    CHECK(!es2_sim_host_fault_owned(0x80000003ul, (const void *)(uintptr_t)write_through));
    CHECK(!es2_sim_host_fault_owned(0x40010006ul, (const void *)(uintptr_t)write_through));
    CHECK(!es2_sim_host_fault_owned(0xc0000005ul, (const void *)(uintptr_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "Sleep")));
    void *heap = HeapAlloc(GetProcessHeap(), 0, 16);
    CHECK(heap && !es2_sim_host_fault_owned(0xc0000005ul, heap));
    HeapFree(GetProcessHeap(), 0, heap);
    es2_sim_host_fault_remove();
    CHECK(!es2_sim_host_fault_owned(0xc0000005ul, (const void *)(uintptr_t)write_through));
    return 0;
}

static int null_page(void) {
    EXCEPTION_RECORD r;
    unsigned address = 0;
    memset(&r, 0, sizeof r);
    r.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    r.NumberParameters = 2;
    r.ExceptionInformation[0] = 0; r.ExceptionInformation[1] = 0xf;
    CHECK(es2_sim_host_fault_null_page(&r, &address) && address == 0xfu);
    r.ExceptionInformation[0] = 1; r.ExceptionInformation[1] = 0xffff;
    CHECK(es2_sim_host_fault_null_page(&r, &address) && address == 0xffffu);
    CHECK(es2_sim_host_fault_null_page(&r, NULL));
    r.ExceptionInformation[1] = ES2_SIM_NULL_PAGE_END;
    CHECK(!es2_sim_host_fault_null_page(&r, &address));
    r.ExceptionInformation[0] = 8; r.ExceptionInformation[1] = 0xf;
    CHECK(!es2_sim_host_fault_null_page(&r, &address));
    r.ExceptionInformation[0] = 0;
    r.NumberParameters = 1;
    CHECK(!es2_sim_host_fault_null_page(&r, &address));
    r.NumberParameters = 2;
    r.ExceptionCode = 0xc0000094ul;
    CHECK(!es2_sim_host_fault_null_page(&r, &address));
    CHECK(!es2_sim_host_fault_null_page(NULL, &address));
    return 0;
}

static int divide(void) {
    EXCEPTION_RECORD r;
    memset(&r, 0, sizeof r);
    r.ExceptionCode = EXCEPTION_INT_DIVIDE_BY_ZERO;
    CHECK(es2_sim_host_fault_divide(&r));
    r.ExceptionCode = EXCEPTION_INT_OVERFLOW;
    CHECK(es2_sim_host_fault_divide(&r));
    r.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    CHECK(!es2_sim_host_fault_divide(&r));
    r.ExceptionCode = EXCEPTION_FLT_DIVIDE_BY_ZERO;
    CHECK(!es2_sim_host_fault_divide(&r));
    CHECK(!es2_sim_host_fault_divide(NULL));
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "child-handler")) return child(1, 0, 0);
    if (argc > 1 && !strcmp(argv[1], "child-bare")) return child(0, 0, 0);
    if (argc > 1 && !strcmp(argv[1], "child-null")) return child(1, 1, 0);
    if (argc > 1 && !strcmp(argv[1], "child-null-high")) return child(1, 1, 1);
    if (owned() || null_page() || divide()) return 1;
    DWORD with = 0, without = 0, null_low = 0, null_high = 0;
    CHECK(!run_child("child-handler", &with));
    CHECK(!run_child("child-bare", &without));
    CHECK(!run_child("child-null", &null_low));
    CHECK(!run_child("child-null-high", &null_high));
    printf("child exit codes: with the handler %lu, without 0x%08lx; NULL page 0x%08lx, above it %lu\n",
           (unsigned long)with, (unsigned long)without, (unsigned long)null_low, (unsigned long)null_high);
    CHECK(with == CHILD_EXIT);
    CHECK(without != CHILD_EXIT && without >= 0xc0000000ul);
    CHECK(null_low == 0xc0000005ul);
    CHECK(null_high == CHILD_EXIT);
    puts("PASS: host_fault");
    return 0;
}
