/* //rtl:cw3220_fault_test: the exception and error paths of the own
 * cw3220.dll (rtl_imports.md "The exception paths of the own runtime").
 *
 * `faults <dll>`: frame records on this thread's stack (fs:0) and faults the
 * CPU raises, dispatched by Windows, not handlers called by hand:
 * - `_fputs` on a FILE in a read-only page (DBSIM's assert notifier passes
 *   one inside its CODE): the write CW3220 makes at 406287 (or word
 *   [fp+0x12], 0x10) faults at fp+0x12, on the DLL's private stack, and the
 *   dispatcher reaches this stack's record; the empty string and NULL touch
 *   no FILE (405b84, 405b93); a writable FILE keeps its bytes.
 * - the handler of __startup's frame record (CW3220's 405160): its
 *   disposition is UnhandledExceptionFilter's answer. Through the real filter
 *   with this test's top-level filter answering EXCEPTION_EXECUTE_HANDLER (1,
 *   ExceptionContinueSearch) the record outside it gets the exception; with
 *   the DLL's import slot at a stub answering 0 (ExceptionContinueExecution)
 *   the faulting instruction runs again and the outer record is not asked.
 *   An access violation and an integer divide fault in DBSIM's chain shape
 *   (two __ExceptionHandler records above __startup's).
 * - an unwind (RtlUnwind) through __startup's record and __ExceptionHandler:
 *   both are called with EXCEPTION_UNWINDING; the former asks the filter
 *   then too (405160 tests no flag).
 * - `__ExceptionHandler` as a record's handler: the search goes on to the
 *   outer record (the DLL's handlers pass the OS's SafeSEH
 *   check: no handler table in the DLL); called directly with every flag
 *   combination and the C++ code 0EEFFACEh it answers
 *   ExceptionContinueSearch and writes nothing.
 * - `_calloc` and `_realloc` when the reservation is refused: NULL, CW3220's
 *   registers of both outcomes, GetLastError kept, the old block intact.
 * - the detach (CW3220's exit record 405260 with 4052c1): __startup's record
 *   off the chain when it is the head, deeper in it, absent or never set.
 * The outer record makes the page writable (or the divisor 1) and continues
 * the faulting instruction.
 *
 * `child <scenario> <dll>`: what ends the process. The `faults` mode runs
 * this executable once per scenario and checks the exit code and the lines
 * it wrote: an access violation (fputs) and a divide fault under
 * __startup's record with the real UnhandledExceptionFilter (the exception
 * code is the exit code; no atexit function runs), and _abort, __ErrorExit,
 * __exit and @_CatchCleanup$qv with an exception on the list (exit codes 3,
 * 1, the argument, 3; MessageBoxA at a stub through the DLL's import slot;
 * no atexit function runs). */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "es2/rtl/own_runtime.h"

static unsigned failures, checks;
static void check(int passed, const char *expression, int line)
{
    ++checks;
    if (!passed) {
        if (failures < 40) fprintf(stderr, "cw3220_fault:%d: %s\n", line, expression);
        ++failures;
    }
}
#define CHECK(expression) check((expression) ? 1 : 0, #expression, __LINE__)

typedef struct frame_record {
    struct frame_record *next;
    void *handler;
} frame_record;
static void push_record(frame_record *record, void *handler)
{
    frame_record *head;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(head));
    record->next = head;
    record->handler = handler;
    __asm__ volatile("movl %0, %%fs:0" ::"r"(record) : "memory");
}
static void pop_record(frame_record *record) { __asm__ volatile("movl %0, %%fs:0" ::"r"(record->next) : "memory"); }

/* The outer record: a write fault on the guarded page makes it writable and
 * continues there. Counts calls and keeps the faulting address and the order
 * relative to the top-level filter. */
static BYTE *guarded;
static unsigned recovered, filtered, sequence, recovered_at, filtered_at;
static uint32_t fault_address, fault_code, filter_code, fault_esp;
static EXCEPTION_DISPOSITION __cdecl recover(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatcher)
{
    (void)frame; (void)dispatcher;
    if (record->ExceptionFlags & 6u) return ExceptionContinueSearch;   /* unwinding */
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2 || record->ExceptionInformation[0] != 1 ||
        record->ExceptionInformation[1] - (ULONG_PTR)guarded >= 4096u)
        return ExceptionContinueSearch;
    DWORD old;
    if (!VirtualProtect(guarded, 4096, PAGE_READWRITE, &old)) return ExceptionContinueSearch;
    ++recovered;
    recovered_at = ++sequence;
    fault_code = record->ExceptionCode;
    fault_address = (uint32_t)record->ExceptionInformation[1];
    fault_esp = (uint32_t)context->Esp;
    return ExceptionContinueExecution;
}
static LONG WINAPI top_filter(EXCEPTION_POINTERS *pointers)
{
    ++filtered;
    filtered_at = ++sequence;
    filter_code = pointers->ExceptionRecord->ExceptionCode;
    return EXCEPTION_EXECUTE_HANDLER;
}
static void reset(DWORD protection)
{
    DWORD old;
    VirtualProtect(guarded, 4096, protection, &old);
    recovered = filtered = sequence = recovered_at = filtered_at = 0;
    fault_address = fault_code = filter_code = fault_esp = 0;
}

typedef int (__cdecl *fn_fputs)(const char *, void *);
typedef unsigned (__cdecl *fn_strlen)(const char *);

static fn_fputs thread_fputs;
static BYTE *thread_file;
static int thread_result;
static DWORD WINAPI fputs_thread(void *unused)
{
    (void)unused;
    frame_record outer;
    push_record(&outer, (void *)recover);
    thread_result = thread_fputs("Cannot allocate memory.", thread_file);
    pop_record(&outer);
    return 0;
}

static void check_fputs(HMODULE dll, const es2_own_runtime_info *info)
{
    fn_fputs my_fputs = (fn_fputs)(void *)GetProcAddress(dll, "_fputs");
    CHECK(my_fputs != NULL);
    if (!my_fputs) return;
    /* A FILE as DBSIM's notifier finds it at __streams' thunk+0x30: level
     * 0x006cc3c8, flags byte 0xff (406248: level >= -1; 40627b: flags & 0x90). */
    BYTE *fp = guarded + 0x30;
    memset(guarded, 0, 4096);
    memcpy(fp + 8, "\xc8\xc3\x6c\x00", 4);
    fp[0x12] = 0xff;
    fp[0x13] = 0x25;
    BYTE before[0x40];
    memcpy(before, fp, sizeof(before));
    frame_record outer;
    reset(PAGE_EXECUTE_READ);
    push_record(&outer, (void *)recover);
    int result = my_fputs("Cannot allocate memory.", fp);
    pop_record(&outer);
    CHECK(recovered == 1 && fault_code == EXCEPTION_ACCESS_VIOLATION);
    CHECK(fault_address == (uint32_t)(uintptr_t)(fp + 0x12));
    /* The write is made before the switch: ESP on this thread's stack. */
    uint32_t stack_lo, stack_hi, here = (uint32_t)(uintptr_t)&outer;
    __asm__ volatile("movl %%fs:8, %0\n\tmovl %%fs:4, %1" : "=r"(stack_lo), "=r"(stack_hi));
    CHECK(fault_esp >= stack_lo && fault_esp < here && here < stack_hi);
    CHECK(fault_esp - info->private_stack_lo >= info->private_stack_hi - info->private_stack_lo);
    CHECK(result == '.');
    CHECK(!memcmp(before, fp, sizeof(before)));
    /* The page is writable now: no fault, nothing changed. */
    recovered = 0;
    push_record(&outer, (void *)recover);
    result = my_fputs("abc", fp);
    pop_record(&outer);
    CHECK(recovered == 0 && result == 'c' && !memcmp(before, fp, sizeof(before)));
    /* NULL and "" return 0 before the FILE is read (405b84, 405b93). */
    reset(PAGE_NOACCESS);
    push_record(&outer, (void *)recover);
    int empty = my_fputs("", fp), none = my_fputs(NULL, fp);
    pop_record(&outer);
    CHECK(empty == 0 && none == 0 && recovered == 0);
    /* Another thread (the caller's-stack path): the same fault there. */
    reset(PAGE_EXECUTE_READ);
    thread_fputs = my_fputs;
    thread_file = fp;
    HANDLE thread = CreateThread(NULL, 0, fputs_thread, NULL, 0, NULL);
    CHECK(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
    if (thread) CloseHandle(thread);
    CHECK(recovered == 1 && fault_address == (uint32_t)(uintptr_t)(fp + 0x12) && thread_result == '.');
    CHECK(fault_esp - info->private_stack_lo >= info->private_stack_hi - info->private_stack_lo);
    CHECK(!memcmp(before, fp, sizeof(before)));
    reset(PAGE_READWRITE);
}

/* An access violation inside the records [inner, outer]. */
static void fault_inside(void *inner_handler)
{
    frame_record outer, inner;
    reset(PAGE_READONLY);
    push_record(&outer, (void *)recover);
    push_record(&inner, inner_handler);
    *(volatile uint32_t *)(uintptr_t)(guarded + 0x100) = 0x12345678u;
    pop_record(&inner);
    pop_record(&outer);
}

/* The DLL's import slot of dll!name. */
static uint32_t *import_slot(HMODULE module, const char *dll, const char *name)
{
    const BYTE *image = (const BYTE *)module;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(image + ((const IMAGE_DOS_HEADER *)image)->e_lfanew);
    const IMAGE_DATA_DIRECTORY *id = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (const IMAGE_IMPORT_DESCRIPTOR *d = (const IMAGE_IMPORT_DESCRIPTOR *)(image + id->VirtualAddress); d->Name; ++d) {
        if (_stricmp((const char *)image + d->Name, dll)) continue;
        const uint32_t *names = (const uint32_t *)(image + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        for (unsigned i = 0; names[i]; ++i)
            if (!(names[i] & 0x80000000u) && !strcmp((const char *)image + names[i] + 2, name)) return (uint32_t *)(image + d->FirstThunk) + i;
    }
    return NULL;
}
static uint32_t point_slot(uint32_t *slot, const void *target)
{
    DWORD old, ignored;
    uint32_t saved = *slot;
    if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) return saved;
    *slot = (uint32_t)(uintptr_t)target;
    VirtualProtect(slot, 4, old, &ignored);
    return saved;
}

/* uint32_t divide_zero(uint32_t dividend): EDX:EAX = dividend, DIV by ECX = 0
 * at divide_site; a handler that sets ECX to 1 lets it return the dividend. */
__asm__(".text\n.p2align 4\n_divide_zero:\n"
        "    movl 4(%esp), %eax\n    xorl %edx, %edx\n    xorl %ecx, %ecx\n"
        "_divide_site:\n    divl %ecx\n    ret\n");
extern uint32_t __cdecl divide_zero(uint32_t dividend) __asm__("_divide_zero");
extern const char divide_site[] __asm__("_divide_site");

/* The outer record of the divide cases: ECX = 1, continue. */
static EXCEPTION_DISPOSITION __cdecl recover_divide(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatcher)
{
    (void)frame; (void)dispatcher;
    if (record->ExceptionFlags & 6u) return ExceptionContinueSearch;
    if (record->ExceptionCode != EXCEPTION_INT_DIVIDE_BY_ZERO || record->ExceptionAddress != (void *)divide_site) return ExceptionContinueSearch;
    ++recovered;
    recovered_at = ++sequence;
    fault_code = record->ExceptionCode;
    fault_address = (uint32_t)(uintptr_t)record->ExceptionAddress;
    context->Ecx = 1;
    return ExceptionContinueExecution;
}
/* UnhandledExceptionFilter's import slot of the DLL at this stub: it keeps
 * what it is given and answers stub_answer; for the answer 0 it repairs the
 * divisor itself (the dispatcher then continues the instruction). */
static LONG stub_answer;
static unsigned stub_calls, stub_unwinds;
static uint32_t stub_code, stub_flags, stub_record, stub_context;
static LONG WINAPI filter_stub(EXCEPTION_POINTERS *pointers)
{
    EXCEPTION_RECORD *record = pointers->ExceptionRecord;
    if (record->ExceptionFlags & 6u) { ++stub_unwinds; stub_flags = record->ExceptionFlags; stub_code = record->ExceptionCode; return 1; }
    ++stub_calls;
    filtered_at = ++sequence;
    stub_code = record->ExceptionCode;
    stub_flags = record->ExceptionFlags;
    stub_record = (uint32_t)(uintptr_t)record;
    stub_context = (uint32_t)(uintptr_t)pointers->ContextRecord;
    if (stub_answer == 0 && record->ExceptionCode == EXCEPTION_INT_DIVIDE_BY_ZERO) pointers->ContextRecord->Ecx = 1;
    return stub_answer;
}
static void stub_reset(LONG answer)
{
    stub_answer = answer;
    stub_calls = stub_unwinds = 0;
    stub_code = stub_flags = stub_record = stub_context = 0;
    recovered = sequence = recovered_at = filtered_at = 0;
    fault_code = fault_address = 0;
}

/* A divide fault in DBSIM's chain shape: outer, __startup's record, then two
 * __ExceptionHandler records (as observed live: DBSIM's handler twice above
 * the CW3220 handler). */
static uint32_t divide_inside(void *startup_handler, void *frame_handler)
{
    frame_record outer, startup, frame1, frame2;
    push_record(&outer, (void *)recover_divide);
    push_record(&startup, startup_handler);
    push_record(&frame1, frame_handler);
    push_record(&frame2, frame_handler);
    uint32_t result = divide_zero(0x1234567u);
    pop_record(&frame2);
    pop_record(&frame1);
    pop_record(&startup);
    pop_record(&outer);
    return result;
}

/* RtlUnwind(unwind_target, the label, NULL, 0); the registers and ESP come
 * back from the words below. */
uint32_t unwind_target, unwind_api, unwind_saved[5];
__asm__(".text\n.p2align 4\n_unwind_to:\n"
        "    movl %ebx, _unwind_saved\n    movl %esi, _unwind_saved+4\n    movl %edi, _unwind_saved+8\n"
        "    movl %ebp, _unwind_saved+12\n    movl %esp, _unwind_saved+16\n"
        "    pushl $0\n    pushl $0\n    pushl $1f\n    pushl _unwind_target\n    call *_unwind_api\n"
        "1:  movl _unwind_saved+16, %esp\n    movl _unwind_saved, %ebx\n    movl _unwind_saved+4, %esi\n"
        "    movl _unwind_saved+8, %edi\n    movl _unwind_saved+12, %ebp\n    ret\n");
extern void __cdecl unwind_to(void) __asm__("_unwind_to");

static unsigned seen_unwinds;
static uint32_t seen_flags;
static EXCEPTION_DISPOSITION __cdecl count_unwind(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatcher)
{
    (void)frame; (void)context; (void)dispatcher;
    if (record->ExceptionFlags & 6u) { ++seen_unwinds; seen_flags = record->ExceptionFlags; }
    return ExceptionContinueSearch;
}

static void check_dispositions(HMODULE dll, const es2_own_runtime_info *info)
{
    void *startup = (void *)(uintptr_t)info->startup_handler, *handler = (void *)GetProcAddress(dll, "__ExceptionHandler");
    uint32_t *slot = import_slot(dll, "KERNEL32.dll", "UnhandledExceptionFilter");
    CHECK(slot != NULL && handler != NULL);
    if (!slot || !handler) return;
    /* Through the real filter: answer 1, the outer record continues. */
    LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(top_filter);
    filtered = 0;
    stub_reset(1);
    filter_code = 0;
    uint32_t result = divide_inside(startup, handler);
    CHECK(result == 0x1234567u && filtered == 1 && filter_code == EXCEPTION_INT_DIVIDE_BY_ZERO);
    CHECK(recovered == 1 && fault_address == (uint32_t)(uintptr_t)divide_site);
    SetUnhandledExceptionFilter(previous);
    /* Through the slot: answer 1, then 0. */
    uint32_t saved = point_slot(slot, (const void *)filter_stub);
    CHECK(*slot == (uint32_t)(uintptr_t)filter_stub);
    stub_reset(1);
    result = divide_inside(startup, handler);
    CHECK(result == 0x1234567u && stub_calls == 1 && stub_code == EXCEPTION_INT_DIVIDE_BY_ZERO && stub_flags == 0);
    CHECK(recovered == 1 && filtered_at == 1 && recovered_at == 2 && stub_record && stub_context);
    stub_reset(0);
    result = divide_inside(startup, handler);
    CHECK(result == 0x1234567u && stub_calls == 1 && recovered == 0);
    /* The access violation of fault_inside through the stub. */
    stub_reset(1);
    reset(PAGE_READONLY);
    fault_inside(startup);
    CHECK(stub_calls == 1 && stub_code == EXCEPTION_ACCESS_VIOLATION && recovered == 1);
    /* An unwind through [outer, __startup's record, __ExceptionHandler,
     * a counting record]: each handler above the target is called with
     * EXCEPTION_UNWINDING, __startup's asks the filter, and fs:0 is the
     * target afterwards. RtlUnwind wants the records at rising addresses
     * towards the target: one array, the target last. */
    frame_record chain[4];
    stub_reset(1);
    seen_unwinds = 0;
    seen_flags = 0;
    push_record(&chain[3], (void *)recover);
    push_record(&chain[2], startup);
    push_record(&chain[1], handler);
    push_record(&chain[0], (void *)count_unwind);
    unwind_target = (uint32_t)(uintptr_t)&chain[3];
    unwind_api = (uint32_t)(uintptr_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "RtlUnwind");
    CHECK(unwind_api != 0);
    if (unwind_api) unwind_to();
    frame_record *head;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(head));
    CHECK(head == &chain[3]);
    if (head != &chain[3]) __asm__ volatile("movl %0, %%fs:0" ::"r"(&chain[3]) : "memory");
    pop_record(&chain[3]);
    CHECK(seen_unwinds == 1 && (seen_flags & 2u) && stub_calls == 0 && stub_unwinds == 1 && (stub_flags & 2u));
    printf("unwind: __startup's handler asked the filter with code %08x flags %x\n", (unsigned)stub_code, (unsigned)stub_flags);
    point_slot(slot, (const void *)(uintptr_t)saved);
    CHECK(*slot == saved);
}

/* __ExceptionHandler called directly: every flag combination, the C++ code
 * and CPU codes; ExceptionContinueSearch and no write. */
static void check_handler_matrix(HMODULE dll)
{
    typedef int (__cdecl *fn_handler)(void *, void *, void *, void *);
    fn_handler handler = (fn_handler)(void *)GetProcAddress(dll, "__ExceptionHandler");
    CHECK(handler != NULL);
    if (!handler) return;
    static const uint32_t codes[] = {0x0eefface, EXCEPTION_ACCESS_VIOLATION, EXCEPTION_INT_DIVIDE_BY_ZERO, EXCEPTION_STACK_OVERFLOW,
                                     EXCEPTION_BREAKPOINT, 0xc0000027u};
    static const uint32_t flags[] = {0, 1, 2, 4, 6, 3, 0x10};
    unsigned calls = 0;
    for (unsigned c = 0; c < sizeof(codes) / sizeof(codes[0]); ++c)
        for (unsigned f = 0; f < sizeof(flags) / sizeof(flags[0]); ++f) {
            EXCEPTION_RECORD record, record_before;
            CONTEXT context, context_before;
            uint32_t frame[8] = {0xffffffffu, 0x4967f0u, 0x4a0000u, 0x12f000u, 3u, 0, 0, 0x12fe00u}, frame_before[8];
            memset(&record, 0, sizeof(record));
            record.ExceptionCode = codes[c];
            record.ExceptionFlags = flags[f];
            memset(&context, 0x5c, sizeof(context));
            memcpy(&record_before, &record, sizeof(record));
            memcpy(&context_before, &context, sizeof(context));
            memcpy(frame_before, frame, sizeof(frame));
            CHECK(handler(&record, frame, &context, NULL) == ExceptionContinueSearch);
            CHECK(!memcmp(&record, &record_before, sizeof(record)) && !memcmp(&context, &context_before, sizeof(context)) &&
                  !memcmp(frame, frame_before, sizeof(frame)));
            ++calls;
        }
    CHECK(calls == 42);
}

/* calloc(count, n) and realloc(p, n) with ECX/EDX preset to 0c0c0c0c and
 * 0d0d0d0d; EAX, ECX, EDX afterwards. */
uint32_t x_target, x_args[2], x_out[3];
__asm__(".text\n.p2align 4\n_x_call2:\n"
        "    pushl %ebx\n    pushl %esi\n    pushl %edi\n    pushl %ebp\n"
        "    movl $0x0c0c0c0c, %ecx\n    movl $0x0d0d0d0d, %edx\n"
        "    pushl _x_args+4\n    pushl _x_args\n    call *_x_target\n    addl $8, %esp\n"
        "    movl %eax, _x_out\n    movl %ecx, _x_out+4\n    movl %edx, _x_out+8\n"
        "    popl %ebp\n    popl %edi\n    popl %esi\n    popl %ebx\n    ret\n");
extern void __cdecl x_call2(void) __asm__("_x_call2");
static uint32_t call2(FARPROC target, uint32_t a, uint32_t b)
{
    x_target = (uint32_t)(uintptr_t)target;
    x_args[0] = a; x_args[1] = b;
    x_call2();
    return x_out[0];
}

/* Allocation failure: a request no 32-bit address space can reserve. */
static void check_allocation(HMODULE dll, const es2_own_runtime_info *info)
{
    FARPROC my_calloc = GetProcAddress(dll, "_calloc"), my_realloc = GetProcAddress(dll, "_realloc"), my_free = GetProcAddress(dll, "_free");
    CHECK(my_calloc && my_realloc && my_free);
    if (!my_calloc || !my_realloc || !my_free) return;
    const uint32_t huge = 0x7ff00000u;
    /* Both ways of calloc: a block (ECX 0, EDX count*n) and NULL (ECX
     * count*n, EDX unchanged); GetLastError kept in both. */
    SetLastError(0x4d2);
    uint32_t p = call2(my_calloc, 4, 6);
    CHECK(p && x_out[1] == 0 && x_out[2] == 24 && GetLastError() == 0x4d2);
    uint32_t regions = info->heap->region_count;
    SetLastError(0x4d3);
    uint32_t q = call2(my_calloc, 1, huge);
    CHECK(q == 0 && x_out[1] == huge && x_out[2] == 0x0d0d0d0du && GetLastError() == 0x4d3);
    CHECK(info->heap->region_count == regions);
    q = call2(my_calloc, 0x10, huge / 0x10u);
    CHECK(q == 0 && x_out[1] == huge && x_out[2] == 0x0d0d0d0du);
    /* malloc(0) is NULL: calloc with a zero count or size. */
    q = call2(my_calloc, 0, 8);
    CHECK(q == 0 && x_out[1] == 0 && x_out[2] == 0x0d0d0d0du);
    /* realloc: NULL for the huge size, the block and its header unchanged;
     * then a growth that succeeds and keeps the bytes. */
    if (p) {
        memcpy((void *)(uintptr_t)p, "0123456789abcdefghijklmn", 24);
        uint32_t header = *(const uint32_t *)(uintptr_t)(p - 4u);
        SetLastError(0x4d4);
        CHECK(call2(my_realloc, p, huge) == 0 && GetLastError() == 0x4d4);
        CHECK(*(const uint32_t *)(uintptr_t)(p - 4u) == header && !memcmp((const void *)(uintptr_t)p, "0123456789abcdefghijklmn", 24));
        CHECK(info->heap->region_count == regions);
        uint32_t r = call2(my_realloc, p, 0x200);
        CHECK(r && !memcmp((const void *)(uintptr_t)r, "0123456789abcdefghijklmn", 24));
        if (r) call2(my_free, r, 0);
    }
}

/* At detach the DLL takes __startup's record (lifecycle->frame_record) off
 * fs:0 like CW3220's 4052c1: the head is replaced by its successor; deeper
 * in the chain its predecessor skips it; absent or 0, nothing changes. Each
 * case loads the DLL afresh (its state starts at attach). */
static void check_detach(const char *dll_path)
{
    for (unsigned c = 0; c < 4; ++c) {
        HMODULE dll = LoadLibraryA(dll_path);
        CHECK(dll != NULL);
        if (!dll) return;
        es2_own_runtime_fn describe = (es2_own_runtime_fn)(void *)GetProcAddress(dll, ES2_OWN_RUNTIME_EXPORT);
        const es2_own_runtime_info *info = describe ? describe() : NULL;
        CHECK(info && info->lifecycle->frame_record == 0);
        frame_record *head, r1, r2, r3, absent = {NULL, (void *)recover};
        __asm__ volatile("movl %%fs:0, %0" : "=r"(head));
        push_record(&r1, (void *)recover);
        push_record(&r2, (void *)recover);
        push_record(&r3, (void *)recover);
        uint32_t target = c == 0 ? (uint32_t)(uintptr_t)&r3 : c == 1 ? (uint32_t)(uintptr_t)&r2 : c == 2 ? (uint32_t)(uintptr_t)&absent : 0u;
        if (info) info->lifecycle->frame_record = target;
        CHECK(FreeLibrary(dll));
        CHECK(GetModuleHandleA("cw3220.dll") == NULL);
        frame_record *now;
        __asm__ volatile("movl %%fs:0, %0" : "=r"(now));
        if (c == 0) CHECK(now == &r2 && r2.next == &r1 && r1.next == head);
        else if (c == 1) CHECK(now == &r3 && r3.next == &r1 && r1.next == head);
        else CHECK(now == &r3 && r3.next == &r2 && r2.next == &r1 && r1.next == head);
        __asm__ volatile("movl %0, %%fs:0" ::"r"(head) : "memory");
    }
}

static void check_handlers(HMODULE dll, const es2_own_runtime_info *info)
{
    LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(top_filter);
    /* __startup's record: UnhandledExceptionFilter decides (EXECUTE_HANDLER,
     * returned as the disposition 1, ExceptionContinueSearch). */
    CHECK(info->startup_handler != 0);
    fault_inside((void *)(uintptr_t)info->startup_handler);
    CHECK(filtered == 1 && filter_code == EXCEPTION_ACCESS_VIOLATION);
    CHECK(recovered == 1 && filtered_at == 1 && recovered_at == 2);
    CHECK(fault_address == (uint32_t)(uintptr_t)(guarded + 0x100) && *(volatile uint32_t *)(uintptr_t)(guarded + 0x100) == 0x12345678u);
    /* __ExceptionHandler: the search goes on; the filter is not asked. */
    void *handler = (void *)GetProcAddress(dll, "__ExceptionHandler");
    CHECK(handler != NULL);
    if (handler) {
        uint64_t calls = info->counters->calls, declined = info->counters->declined;
        fault_inside(handler);
        CHECK(recovered == 1 && filtered == 0);
        /* The export jumps to the handler without the adapters (no count).
         * Had the OS refused it (SafeSEH), the dispatch would have stopped
         * at the inner record and the outer one would not have run. */
        CHECK(info->counters->calls == calls && info->counters->declined == declined);
    }
    SetUnhandledExceptionFilter(previous);
}

static int run_faults(const char *dll_path)
{
    HMODULE dll = LoadLibraryA(dll_path);
    CHECK(dll != NULL);
    if (!dll) return 1;
    es2_own_runtime_fn describe = (es2_own_runtime_fn)(void *)GetProcAddress(dll, ES2_OWN_RUNTIME_EXPORT);
    CHECK(describe != NULL);
    if (!describe) return 1;
    const es2_own_runtime_info *info = describe();
    CHECK(info->mode == ES2_OWN_MODE_OWN);
    guarded = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(guarded != NULL);
    if (!guarded) return 1;
    /* The first call makes this thread the owner: the stubs run on the
     * DLL's private stack, the fault in fputs too. */
    fn_strlen my_strlen = (fn_strlen)(void *)GetProcAddress(dll, "_strlen");
    CHECK(my_strlen && my_strlen("x") == 1 && info->counters->foreign_calls == 0);
    check_fputs(dll, info);
    check_handlers(dll, info);
    check_dispositions(dll, info);
    check_handler_matrix(dll);
    check_allocation(dll, info);
    CHECK(info->counters->foreign_calls == 1 && info->counters->declined == 0);
    VirtualFree(guarded, 0, MEM_RELEASE);
    CHECK(FreeLibrary(dll));
    check_detach(dll_path);
    return 0;
}

/* ------------------------------------------------------------------------
 * `child <scenario> <dll>`: lines through WriteFile (no buffer an exit could
 * lose). */
static void say(const char *format, ...)
{
    char line[512];
    va_list list;
    va_start(list, format);
    int n = vsnprintf(line, sizeof(line), format, list);
    va_end(list);
    DWORD written;
    if (n > 0) WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line, (DWORD)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1), &written, NULL);
}
static const es2_own_runtime_info *child_info;
static LONG WINAPI child_filter(EXCEPTION_POINTERS *pointers)
{
    say("filter %08x %x\n", (unsigned)pointers->ExceptionRecord->ExceptionCode, (unsigned)pointers->ExceptionRecord->ExceptionFlags);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void __cdecl child_atexit(void) { say("atexit\n"); }
static int WINAPI child_box(HWND window, LPCSTR text, LPCSTR title, UINT style)
{
    (void)window;
    say("box %s|%s|%x|%u\n", text, title, style, (unsigned)child_info->counters->declined);
    return IDOK;
}

static int run_child(const char *scenario, const char *dll_path)
{
    SetUnhandledExceptionFilter(child_filter);
    HMODULE dll = LoadLibraryA(dll_path);
    es2_own_runtime_fn describe = dll ? (es2_own_runtime_fn)(void *)GetProcAddress(dll, ES2_OWN_RUNTIME_EXPORT) : NULL;
    uint32_t *box = dll ? import_slot(dll, "USER32.dll", "MessageBoxA") : NULL;
    typedef int (__cdecl *fn_atexit)(void (__cdecl *)(void));
    fn_atexit my_atexit = dll ? (fn_atexit)(void *)GetProcAddress(dll, "_atexit") : NULL;
    if (!describe || !box || !my_atexit) { say("setup failed\n"); return 2; }
    child_info = describe();
    point_slot(box, (const void *)child_box);
    if (my_atexit(child_atexit)) { say("atexit refused\n"); return 2; }
    say("start\n");
    frame_record record;
    if (!strcmp(scenario, "divide")) {
        push_record(&record, (void *)(uintptr_t)child_info->startup_handler);
        say("returned %u\n", (unsigned)divide_zero(5));
    } else if (!strcmp(scenario, "fputs")) {
        BYTE *page = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!page) return 2;
        memcpy(page + 0x38, "\xc8\xc3\x6c\x00", 4);
        page[0x42] = 0xff;
        DWORD old;
        VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old);
        push_record(&record, (void *)(uintptr_t)child_info->startup_handler);
        say("returned %d\n", ((fn_fputs)(void *)GetProcAddress(dll, "_fputs"))("Cannot allocate memory.", page + 0x30));
    } else if (!strcmp(scenario, "offstack")) {
        /* No runtime call: the same write fault with ESP outside the
         * thread's stack. */
        static BYTE *page, *stack;
        page = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READONLY);
        stack = VirtualAlloc(NULL, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!page || !stack) return 2;
        push_record(&record, (void *)(uintptr_t)child_info->startup_handler);
        __asm__ volatile("movl %%esp, %%ebx\n\tmovl %1, %%esp\n\tmovl $1, 0x12(%0)\n\tmovl %%ebx, %%esp" : : "r"(page), "r"(stack + 65536) : "ebx", "memory");
        say("returned 0\n");
    } else if (!strcmp(scenario, "abort")) {
        ((void (__cdecl *)(void))(void *)GetProcAddress(dll, "_abort"))();
    } else if (!strcmp(scenario, "errorexit")) {
        ((void (__cdecl *)(const char *))(void *)GetProcAddress(dll, "__ErrorExit"))("Illegal mode");
    } else if (!strcmp(scenario, "exit")) {
        ((void (__cdecl *)(int))(void *)GetProcAddress(dll, "__exit"))(7);
    } else if (!strcmp(scenario, "catch")) {
        /* The context block at StackBase-8 names a list holding an exception. */
        static uint32_t exception_block[16], list;
        uint32_t base;
        __asm__ volatile("movl %%fs:4, %0" : "=r"(base));
        list = (uint32_t)(uintptr_t)exception_block;
        *(volatile uint32_t *)(uintptr_t)(base - 8u) = (uint32_t)(uintptr_t)&list;
        ((void (__cdecl *)(void))(void *)GetProcAddress(dll, "@_CatchCleanup$qv"))();
    } else {
        say("unknown scenario\n");
        return 2;
    }
    say("not ended\n");
    return 2;
}

/* This executable as `child <scenario> <dll>`: exit code and output. */
static DWORD spawn_child(const char *scenario, const char *dll_path, char *out, size_t size)
{
    char self[MAX_PATH], command[MAX_PATH * 3];
    DWORD n = GetModuleFileNameA(NULL, self, sizeof(self));
    if (!n || n >= sizeof(self)) return 0xffffffffu;
    snprintf(command, sizeof(command), "\"%s\" child %s \"%s\"", self, scenario, dll_path);
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE read_end, write_end;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) return 0xffffffffu;
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end; si.hStdError = write_end; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi;
    BOOL started = CreateProcessA(NULL, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(write_end);
    if (!started) { CloseHandle(read_end); return 0xffffffffu; }
    size_t used = 0;
    DWORD got;
    while (used + 1 < size && ReadFile(read_end, out + used, (DWORD)(size - 1 - used), &got, NULL) && got) used += got;
    out[used] = 0;
    CloseHandle(read_end);
    DWORD code = 0xffffffffu;
    if (WaitForSingleObject(pi.hProcess, 20000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 0xfffffffeu);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return code;
}

/* What ends the process, run as children. A fault under __startup's record
 * with this executable's filter answering EXCEPTION_EXECUTE_HANDLER: the
 * filter is asked by that record and by the thread start's record, the
 * thread start's unwind calls __startup's record again (STATUS_UNWIND,
 * which asks the filter once more), and the exception code is the exit
 * code. The control "offstack" (no runtime call, ESP outside the thread's
 * stack) shows what a fault on the private stack would get: the OS refuses
 * the unwind (STATUS_BAD_STACK or STATUS_INVALID_UNWIND_TARGET, by where
 * the block lies). */
static void check_endings(const char *dll_path)
{
    /* The box title: what GetModuleFileNameA leaves in CW3220's 50h-byte
     * buffer (418ef4), after the last '\' or ':'. */
    char self[0x51], expected[512], out[2048];
    memset(self, 0, sizeof(self));
    GetModuleFileNameA(NULL, self, 0x50);
    const char *name = strrchr(self, '\\');
    if (!name) name = strrchr(self, ':');
    name = name ? name + 1 : self;
    static const char abnormal[] = "start\nbox Abnormal program termination|%s|12010|%u\n";
    struct { const char *scenario; DWORD code, other; const char *format; unsigned declined; } cases[] = {
        {"abort", 3, 3, abnormal, 0},
        {"errorexit", 1, 1, "start\nbox Illegal mode|%s|12010|%u\n", 0},
        {"exit", 7, 7, "start\n", 0},
        {"catch", 3, 3, abnormal, 1},
        {"divide", 0xc0000094u, 0xc0000094u, "start\nfilter c0000094 0\nfilter c0000094 0\nfilter c0000027 2\n", 0},
        {"fputs", 0xc0000005u, 0xc0000005u, "start\nfilter c0000005 0\nfilter c0000005 0\nfilter c0000027 2\n", 0},
        {"offstack", 0xc0000028u, 0xc0000029u, "start\nfilter c0000005 0\nfilter c0000005 0\n", 0},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        DWORD code = spawn_child(cases[i].scenario, dll_path, out, sizeof(out));
        printf("child %s: exit %08lx\n%s", cases[i].scenario, (unsigned long)code, out);
        snprintf(expected, sizeof(expected), cases[i].format, name, cases[i].declined);
        CHECK(code == cases[i].code || code == cases[i].other);
        CHECK(!strcmp(out, expected));
    }
}

/* A data file's runfiles path: Windows tests get a manifest (one line
 * "_main/<path> <absolute path>" per file) instead of a tree. */
static const char *resolve(const char *path)
{
    static char result[MAX_PATH];
    const char *manifest = getenv("RUNFILES_MANIFEST_FILE");
    FILE *stream = manifest && *manifest ? fopen(manifest, "rb") : NULL;
    if (!stream) return path;
    char line[4096], key[MAX_PATH + 8];
    snprintf(key, sizeof(key), "_main/%s ", path);
    size_t n = strlen(key);
    const char *found = path;
    while (fgets(line, sizeof(line), stream)) {
        if (strncmp(line, key, n)) continue;
        snprintf(result, sizeof(result), "%s", line + n);
        result[strcspn(result, "\r\n")] = 0;
        for (char *p = result; *p; ++p) if (*p == '/') *p = '\\';
        found = result;
        break;
    }
    fclose(stream);
    return found;
}

int main(int argc, char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 4 && !strcmp(argv[1], "child")) return run_child(argv[2], argv[3]);
    if (argc != 3 || strcmp(argv[1], "faults")) { fprintf(stderr, "usage: faults <dll> | child <scenario> <dll>\n"); return 2; }
    char dll_path[MAX_PATH];
    DWORD n = GetFullPathNameA(resolve(argv[2]), sizeof(dll_path), dll_path, NULL);
    if (!n || n >= sizeof(dll_path)) { fprintf(stderr, "dll path\n"); return 2; }
    run_faults(dll_path);
    check_endings(dll_path);
    printf("cw3220_fault: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
