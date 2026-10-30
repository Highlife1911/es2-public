/* Own-code fixture for proxy/fault_trace.h (the trace of DBSIM's frame
 * records after a fault). A synthetic DBSIM-shaped image carries the thunk
 * 0x4967f0 (`jmp [slot]`), the record set-up's store of the thunk at
 * 0x49631c and the import slot, which holds a fake handler of this test in
 * place of CW3220's __ExceptionHandler; the fake clears the context word at
 * +10h on an unwinding call, as 0x40415d does. No original game code runs.
 * Modes:
 *   arming    only the slot changes, to ft_handler; refusals (no handler, a wrong
 *             image or base, a changed thunk or set-up byte, a slot that holds another
 *             address) change nothing and are logged
 *   calls     calls through the thunk: arguments and answer forwarded, search and
 *             unwinding flags, the context word before and after, the caps, an
 *             unreadable frame, the summary
 *   raise     a real exception through a frame record on fs:0 (search phase, the fake
 *             continues execution) and RtlUnwind through it (EXCEPTION_UNWINDING)
 *   probe     the exit-path probe goes to the same log when the new-fail switch is off
 *             (no CW3220.DLL here: no_module) and is left alone when it is on */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ES2_NF_NO_PROCESS_ENTRY
#include "../fault_trace.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

/* ---- the fake __ExceptionHandler ---- */
static volatile LONG fake_calls;
static EXCEPTION_RECORD *fake_record;
static void *fake_frame, *fake_context, *fake_dispatcher;
static uint32_t fake_flags;
static int fake_search_answer = ExceptionContinueSearch;
static int __cdecl fake_handler(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatcher) {
    ++fake_calls;
    fake_record = record; fake_frame = frame; fake_context = context; fake_dispatcher = dispatcher;
    fake_flags = nf_readable((uintptr_t)record, 16) ? record->ExceptionFlags : 0xffffffffu;
    if (fake_flags != 0xffffffffu && (fake_flags & 6u)) {
        if (nf_readable((uintptr_t)frame, 0x20)) ((BYTE *)frame)[0x10] = ((BYTE *)frame)[0x11] = 0;
        return ExceptionContinueSearch;
    }
    return fake_search_answer;
}

/* ---- synthetic image ---- */
static BYTE *image, *copy;
static void put32(BYTE *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t va(uint32_t rva) { return (uint32_t)(uintptr_t)image + rva; }

static int make_image(void) {
    if (!image) image = VirtualAlloc(NULL, NF_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!copy) copy = malloc(NF_IMAGE_SIZE);
    CHECK(image && copy);
    DWORD ignored;
    CHECK(VirtualProtect(image, NF_IMAGE_SIZE, PAGE_READWRITE, &ignored));
    memset(image, 0xcc, NF_IMAGE_SIZE);
    memset(image, 0, 0x1000);
    image[0] = 'M'; image[1] = 'Z'; put32(image + 0x3c, 0x80);
    BYTE *nt = image + 0x80;
    put32(nt, 0x00004550u); nt[4] = 0x4c; nt[5] = 0x01; nt[20] = 224; nt[24] = 0x0b; nt[25] = 0x01;
    put32(nt + 24 + 28, (uint32_t)(uintptr_t)image);
    put32(nt + 24 + 56, NF_IMAGE_SIZE);
    BYTE *t = image + FT_THUNK_RVA;
    t[0] = 0xff; t[1] = 0x25; put32(t + 2, va(FT_SLOT_RVA));
    BYTE *s = image + FT_SETUP_RVA;
    s[0] = 0xc7; s[1] = 0x43; s[2] = 0x04; put32(s + 3, va(FT_THUNK_RVA));
    put32(image + FT_SLOT_RVA, (uint32_t)(uintptr_t)fake_handler);
    /* the loader's protection: code read-execute, the import table read-only */
    CHECK(VirtualProtect(image + 0x1000, 0x2cb000, PAGE_EXECUTE_READ, &ignored));
    CHECK(VirtualProtect(image + (FT_SLOT_RVA & ~0xfffu), 0x1000, PAGE_READONLY, &ignored));
    FlushInstructionCache(GetCurrentProcess(), image, NF_IMAGE_SIZE);
    memcpy(copy, image, NF_IMAGE_SIZE);
    return 0;
}
static BOOL unchanged(void) { return !memcmp(copy, image, NF_IMAGE_SIZE); }
static DWORD protect_of(const void *p) {
    MEMORY_BASIC_INFORMATION m;
    return VirtualQuery(p, &m, sizeof(m)) ? m.Protect : 0;
}
static void reset_state(void) {
    ft_uninstall();
    ft_state = FT_OFF; ft_slot = NULL; ft_real = NULL; ft_log_path[0] = 0;
    ft_calls = ft_search = ft_unwinding = ft_returns = ft_severe_logged = ft_other_logged = ft_cleared = 0;
    fake_calls = 0; fake_search_answer = ExceptionContinueSearch;
    nf_disarm_exit_probe();
    nf_state = NF_OFF; nf_log_path[0] = 0;
    nf_probe_state = NF_PROBE_OFF; nf_probe_slot = NULL; nf_probe_real = 0; nf_filter_calls = nf_filter_returns = 0;
}

static WCHAR log_path[MAX_PATH];
static void fresh_log(const char *name) {
    const char *dir = getenv("TEST_TMPDIR");
    char narrow[MAX_PATH];
    snprintf(narrow, sizeof(narrow), "%s\\%s", dir && *dir ? dir : ".", name);
    for (size_t i = 0; narrow[i]; ++i) if (narrow[i] == '/') narrow[i] = '\\';
    MultiByteToWideChar(CP_ACP, 0, narrow, -1, log_path, MAX_PATH);
    DeleteFileW(log_path);
}
static char *read_log(void) {
    HANDLE f = CreateFileW(log_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return calloc(1, 1);
    char *text = calloc(1, 1 << 20);
    DWORD n = 0;
    if (text) ReadFile(f, text, (1 << 20) - 1, &n, NULL);
    CloseHandle(f);
    return text;
}
static unsigned count_of(const char *text, const char *needle) {
    unsigned n = 0;
    for (const char *p = text; (p = strstr(p, needle)); ++p) ++n;
    return n;
}
static int install(void) {
    CHECK(!make_image());
    reset_state();
    CHECK(ft_install_image(image, (uintptr_t)image, (uintptr_t)fake_handler, log_path) && ft_state == FT_INSTALLED);
    return 0;
}
static int through_thunk(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatcher) {
    return ((ft_handler_fn)(uintptr_t)va(FT_THUNK_RVA))(record, frame, context, dispatcher);
}

/* ---- install ---- */
static int refuse_one(const char *what, uint32_t rva, BYTE value, LONG state) {
    CHECK(!make_image());
    reset_state();
    DWORD protection, ignored;
    CHECK(VirtualProtect(image + rva, 1, PAGE_READWRITE, &protection));
    image[rva] = value;
    VirtualProtect(image + rva, 1, protection, &ignored);
    memcpy(copy, image, NF_IMAGE_SIZE);
    if (ft_install_image(image, (uintptr_t)image, (uintptr_t)fake_handler, log_path) || ft_state != state || !unchanged()) {
        fprintf(stderr, "FAIL: %s not refused (state %ld)\n", what, (long)ft_state);
        return 1;
    }
    return 0;
}
static int install_case(void) {
    fresh_log("fault_trace_install.ndjson");
    CHECK(!install());
    uint32_t slot_now;
    memcpy(&slot_now, image + FT_SLOT_RVA, 4);
    CHECK(slot_now == (uint32_t)(uintptr_t)ft_handler && ft_real == (ft_handler_fn)fake_handler && ft_slot == (uint32_t *)(image + FT_SLOT_RVA));
    CHECK(!memcmp(copy, image, FT_SLOT_RVA) && !memcmp(copy + FT_SLOT_RVA + 4, image + FT_SLOT_RVA + 4, NF_IMAGE_SIZE - FT_SLOT_RVA - 4));
    CHECK(protect_of(image + FT_SLOT_RVA) == PAGE_READONLY && protect_of(image + FT_THUNK_RVA) == PAGE_EXECUTE_READ);
    CHECK(!ft_install_image(image, (uintptr_t)image, (uintptr_t)fake_handler, log_path));          /* once */
    ft_uninstall();
    CHECK(unchanged() && ft_state == FT_OFF);
    char *log = read_log();
    CHECK(count_of(log, "\"event\":\"fault_trace_installed\"") == 1 && strstr(log, "\"handler_reference_bytes\":false") && strstr(log, "\"error_mode\":\"0x"));
    free(log);
    /* refusals */
    CHECK(!make_image()); reset_state();
    CHECK(!ft_install_image(image, (uintptr_t)image, 0, log_path) && ft_state == FT_REFUSED_HANDLER && unchanged());
    reset_state();
    CHECK(!ft_install_image(image, (uintptr_t)image + 0x10000, (uintptr_t)fake_handler, log_path) && ft_state == FT_REFUSED_IMAGE && unchanged());
    reset_state();
    CHECK(!ft_install_image(image, (uintptr_t)image, (uintptr_t)fake_handler + 1, log_path) && ft_state == FT_REFUSED_SLOT && unchanged());
    if (refuse_one("thunk opcode", FT_THUNK_RVA, 0xe9, FT_REFUSED_SITE)) return 1;
    if (refuse_one("thunk operand", FT_THUNK_RVA + 2, 0x40, FT_REFUSED_SITE)) return 1;
    if (refuse_one("set-up opcode", FT_SETUP_RVA + 1, 0x41, FT_REFUSED_SITE)) return 1;
    if (refuse_one("set-up operand", FT_SETUP_RVA + 3, 0xf6, FT_REFUSED_SITE)) return 1;
    log = read_log();
    CHECK(count_of(log, "\"event\":\"fault_trace_refused\"") == 7 && count_of(log, "\"state\":\"site_mismatch\"") == 4 &&
          count_of(log, "\"state\":\"no_handler\"") == 1 && count_of(log, "\"state\":\"bad_image\"") == 1 && count_of(log, "\"state\":\"slot_mismatch\"") == 1);
    free(log);
    /* without a log nothing is written, the slot still changes */
    fresh_log("fault_trace_install_quiet.ndjson");
    CHECK(!make_image()); reset_state();
    CHECK(ft_install_image(image, (uintptr_t)image, (uintptr_t)fake_handler, NULL) && !ft_log_path[0]);
    log = read_log();
    CHECK(!log[0]);
    free(log);
    reset_state();
    printf("PASS arming: only DBSIM's import slot of __ExceptionHandler changes, its page keeps its protection; no handler, a wrong image or base, "
           "a changed thunk or set-up byte and a slot with another address are refused, logged and change nothing\n");
    return 0;
}

/* ---- calls ---- */
static int calls_case(void) {
    fresh_log("fault_trace_calls.ndjson");
    CHECK(!install());
    EXCEPTION_RECORD record = {0};
    CONTEXT context = {0};
    uint32_t frame[8] = {0xffffffffu, (uint32_t)va(FT_THUNK_RVA), 0x004a7948u, 0x0012ff00u, 0x00000003u, 0, 0, 0x00000002u};
    void *dispatcher = (void *)(uintptr_t)0xd15d15d1u;
    record.ExceptionCode = EXCEPTION_INT_DIVIDE_BY_ZERO;
    record.ExceptionAddress = (PVOID)(uintptr_t)0x0046c208u;
    /* search: the fake gets the four arguments, its answer comes back, the word stays */
    CHECK(through_thunk(&record, frame, &context, dispatcher) == ExceptionContinueSearch);
    CHECK(fake_calls == 1 && fake_record == &record && fake_frame == frame && fake_context == &context && fake_dispatcher == dispatcher);
    CHECK(ft_calls == 1 && ft_search == 1 && ft_unwinding == 0 && ft_returns == 1 && ft_cleared == 0 && frame[4] == 3u);
    fake_search_answer = ExceptionContinueExecution;
    CHECK(through_thunk(&record, frame, &context, dispatcher) == ExceptionContinueExecution);
    fake_search_answer = ExceptionContinueSearch;
    /* unwinding: the fake clears the word as 0x40415d does */
    record.ExceptionFlags = EXCEPTION_UNWINDING;
    CHECK(through_thunk(&record, frame, &context, dispatcher) == ExceptionContinueSearch && frame[4] == 0);
    CHECK(ft_calls == 3 && ft_search == 2 && ft_unwinding == 1 && ft_cleared == 1);
    /* unwinding with a word that is already 0: nothing to clear */
    CHECK(through_thunk(&record, frame, &context, dispatcher) == ExceptionContinueSearch && ft_cleared == 1 && ft_unwinding == 2);
    char *log = read_log();
    CHECK(count_of(log, "\"event\":\"fault_trace_handler\"") == 4 && count_of(log, "\"event\":\"fault_trace_handler_result\"") == 4);
    CHECK(strstr(log, "\"call\":1,\"code\":\"0xc0000094\",\"flags\":\"0x0\",\"unwinding\":false,\"address\":\"0x46c208\""));
    CHECK(strstr(log, "\"table\":\"0x4a7948\",\"context_word\":\"0x3\",\"word_12\":\"0x0\",\"counter\":\"0x2\",\"frame_on_thread_stack\":true"));
    CHECK(strstr(log, "\"call\":2,\"result\":0,"));
    CHECK(strstr(log, "\"call\":3,\"code\":\"0xc0000094\",\"flags\":\"0x2\",\"unwinding\":true"));
    CHECK(strstr(log, "\"call\":3,\"result\":1,\"context_word_after\":\"0x0\""));
    free(log);
    /* an unreadable frame: recorded as such, still forwarded */
    record.ExceptionFlags = 0;
    CHECK(through_thunk(&record, (void *)(uintptr_t)0x10, &context, dispatcher) == ExceptionContinueSearch && fake_frame == (void *)(uintptr_t)0x10);
    log = read_log();
    CHECK(strstr(log, "\"frame\":\"0x10\",\"frame_readable\":false") && strstr(log, "\"context_word_after\":\"0xffffffff\""));
    free(log);
    /* caps: 64 records with an error code or an unwinding flag, 16 others; every call forwarded and counted */
    for (unsigned i = 0; i < 70; ++i) through_thunk(&record, frame, &context, dispatcher);
    record.ExceptionCode = 0x0eefface;
    for (unsigned i = 0; i < 20; ++i) through_thunk(&record, frame, &context, dispatcher);
    CHECK(ft_calls == 95 && fake_calls == 95 && ft_returns == 95);
    log = read_log();
    CHECK(count_of(log, "\"event\":\"fault_trace_handler\"") == FT_SEVERE_MAX + FT_OTHER_MAX);
    CHECK(count_of(log, "\"code\":\"0x0eefface\"") == FT_OTHER_MAX);
    free(log);
    ft_shutdown();
    log = read_log();
    CHECK(strstr(log, "\"event\":\"fault_trace_summary\"") && strstr(log, "\"calls\":95,\"search\":93,\"unwinding\":2,\"returns\":95,\"cleared\":1"));
    free(log);
    reset_state();
    printf("PASS calls: arguments and answer pass through the thunk unchanged; search and unwinding calls are told apart, a context word cleared by "
           "an unwinding call is counted; an unreadable frame is recorded; %u severe and %u other records at most; the summary\n",
           (unsigned)FT_SEVERE_MAX, (unsigned)FT_OTHER_MAX);
    return 0;
}

/* ---- dispatch ---- */
typedef struct frame_record { struct frame_record *next; void *handler; } frame_record;
static frame_record *fs0(void) { frame_record *h; __asm__ volatile("movl %%fs:0, %0" : "=r"(h)); return h; }
static void set_fs0(void *r) { __asm__ volatile("movl %0, %%fs:0" ::"r"(r) : "memory"); }

/* The record's handler: through the image's thunk, as DBSIM's records name 0x4967f0. */
static EXCEPTION_DISPOSITION __cdecl record_handler(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatcher) {
    return (EXCEPTION_DISPOSITION)through_thunk(record, frame, context, dispatcher);
}
static unsigned outer_calls;
static EXCEPTION_DISPOSITION __cdecl outer_handler(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatcher) {
    (void)record; (void)frame; (void)context; (void)dispatcher;
    ++outer_calls;
    return ExceptionContinueSearch;
}

/* RtlUnwind(unwind_target, the label, NULL, 0); the registers and ESP come back from the words below. */
uint32_t unwind_target, unwind_api, unwind_saved[5];
__asm__(".text\n.p2align 4\n_ft_unwind_to:\n"
        "    movl %ebx, _unwind_saved\n    movl %esi, _unwind_saved+4\n    movl %edi, _unwind_saved+8\n"
        "    movl %ebp, _unwind_saved+12\n    movl %esp, _unwind_saved+16\n"
        "    pushl $0\n    pushl $0\n    pushl $1f\n    pushl _unwind_target\n    call *_unwind_api\n"
        "1:  movl _unwind_saved+16, %esp\n    movl _unwind_saved, %ebx\n    movl _unwind_saved+4, %esi\n"
        "    movl _unwind_saved+8, %edi\n    movl _unwind_saved+12, %ebp\n    ret\n");
extern void __cdecl ft_unwind_to(void) __asm__("_ft_unwind_to");

static int dispatch_case(void) {
    fresh_log("fault_trace_dispatch.ndjson");
    CHECK(!install());
    /* RtlUnwind wants the records at rising addresses towards the target: the DBSIM-shaped record (20h bytes) first, the outer one last. */
    struct { uint32_t dbsim[8]; frame_record outer; } chain;
    memset(&chain, 0, sizeof(chain));
    frame_record *head = fs0();
    chain.outer.next = head;
    chain.outer.handler = (void *)outer_handler;
    chain.dbsim[0] = (uint32_t)(uintptr_t)&chain.outer;
    chain.dbsim[1] = (uint32_t)(uintptr_t)record_handler;
    chain.dbsim[2] = 0x004a1234u;
    chain.dbsim[4] = 0x00000005u;
    chain.dbsim[7] = 0x00000001u;
    set_fs0(&chain.outer);
    set_fs0(chain.dbsim);
    /* search: the fake continues execution, RaiseException returns */
    fake_search_answer = ExceptionContinueExecution;
    RaiseException(EXCEPTION_INT_DIVIDE_BY_ZERO, 0, 0, NULL);
    fake_search_answer = ExceptionContinueSearch;
    CHECK(fs0() == (frame_record *)chain.dbsim);
    CHECK(ft_calls == 1 && ft_search == 1 && fake_calls == 1 && fake_flags == 0 && fake_frame == chain.dbsim && outer_calls == 0 && chain.dbsim[4] == 5u);
    /* unwind to the outer record: the DBSIM-shaped record's handler is called with EXCEPTION_UNWINDING */
    unwind_target = (uint32_t)(uintptr_t)&chain.outer;
    unwind_api = (uint32_t)(uintptr_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "RtlUnwind");
    CHECK(unwind_api != 0);
    ft_unwind_to();
    frame_record *after = fs0();
    if (after != &chain.outer) set_fs0(&chain.outer);
    set_fs0(head);
    CHECK(after == &chain.outer);
    CHECK(ft_calls == 2 && ft_unwinding == 1 && (fake_flags & EXCEPTION_UNWINDING) && fake_frame == chain.dbsim && ft_cleared == 1 && (chain.dbsim[4] & 0xffffu) == 0);
    CHECK(outer_calls == 0);
    char *log = read_log();
    CHECK(count_of(log, "\"event\":\"fault_trace_handler\"") == 2);
    CHECK(strstr(log, "\"call\":1,\"code\":\"0xc0000094\",\"flags\":\"0x0\""));
    CHECK(strstr(log, "\"unwinding\":true") && strstr(log, "\"table\":\"0x4a1234\",\"context_word\":\"0x5\"") && strstr(log, "\"call\":2,\"result\":1,\"context_word_after\":\"0x0\""));
    free(log);
    reset_state();
    printf("PASS raise: a raised divide fault reaches ft_handler through a frame record on fs:0 (search, flags 0) and RtlUnwind through it "
           "calls it with EXCEPTION_UNWINDING; the cleared context word is seen\n");
    return 0;
}

/* ---- probe ---- */
static int probe_case(void) {
    fresh_log("fault_trace_probe.ndjson");
    CHECK(!install());
    ft_arm_exit_probe();
    CHECK(nf_probe_state == NF_PROBE_NO_MODULE && !wcscmp(nf_log_path, log_path));
    char *log = read_log();
    CHECK(count_of(log, "\"event\":\"new_fail_exit_probe\"") == 1 && strstr(log, "\"state\":\"no_module\""));
    free(log);
    /* with the new-fail switch installed the probe is its own: nothing armed here */
    CHECK(!install());
    nf_state = NF_INSTALLED;
    ft_arm_exit_probe();
    CHECK(nf_probe_state == NF_PROBE_OFF && !nf_log_path[0]);
    nf_state = NF_OFF;
    /* not installed: nothing */
    reset_state();
    ft_arm_exit_probe();
    CHECK(nf_probe_state == NF_PROBE_OFF);
    log = read_log();
    CHECK(count_of(log, "\"event\":\"new_fail_exit_probe\"") == 1);
    free(log);
    reset_state();
    printf("PASS probe: the exit-path probe writes into the trace's log when the new-fail switch is off and is left to it when it is on\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "arming")) return install_case();
    if (!strcmp(argv[1], "calls")) return calls_case();
    if (!strcmp(argv[1], "raise")) return dispatch_case();
    if (!strcmp(argv[1], "probe")) return probe_case();
    return 2;
}
