/* Own-code fixture for proxy/new_fail_switch.h (the injection switch
 * for DBSIM's `new` 0x47749c and the report of the CODE pages' protection).
 * A synthetic DBSIM-shaped image carries a `new` with the reference
 * prologue, the callback registration 0x479cfc with its call of `new`, its
 * NULL path (calls of own recorders for _fputs and _abort) and the six
 * call sites; the CODE section is PAGE_EXECUTE_READ as the loader maps it.
 * No original game code runs. Modes:
 *   parse      the ES2_NEW_FAIL text (hex site or `any`, decimal n) and the environment
 *   switch     installed: only the rel32 of the call at 0x479d02 changes; count mode passes
 *              every request through with registers, stack and size as the real `new`
 *              leaves them; the n-th counted request (any site, or one site) gets NULL once,
 *              reaches the NULL path (_fputs with the FILE, _abort) and later requests pass;
 *              other sizes and unknown callers are not counted; the log's lines
 *   refuse     a changed byte of `new`, the registration, a call site, the FILE operand,
 *              a wrong image or base, or a bad configuration: nothing changed
 *   pages      the page scan: patched pages (the switch and a framework hook) keep
 *              PAGE_EXECUTE_READ; a page left writable by a patch is reported (positive
 *              control), as is a writable unpatched page; the report line; the file mapper
 *   exception  after the injection, a real access violation on a read-only page is recorded
 *              (code, write access, target, protection, stack) and goes on unhandled
 *   seh        the exception registration chain walk: module and offset, stack membership, hostile links
 *   suite      the {suite} of the log path: the tick log's stem from the INI, the expansion
 *   import     the import table walk on synthetic images, damaged and hostile tables
 *   probe      the exit-path probe: arming (signature), the stdcall thunk, its records, refusals, disarming
 *   own        the own cw3220.dll's import of UnhandledExceptionFilter (the built DLL as argument)
 *   reference  ES2_DATA_ROOT's DBSIM.EXE: the signature, CODE read-only and containing the
 *              FILE, the six sites, no registered hook prologue at the call (skips if unset) */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ES2_NF_NO_PROCESS_ENTRY
#include "../new_fail_switch.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

/* ---- recorders and the caller with known registers ---- */
static volatile LONG fputs_calls, abort_calls;
static uint32_t fputs_string, fputs_file;
static uint32_t __cdecl test_fputs(uint32_t string, uint32_t file) { ++fputs_calls; fputs_string = string; fputs_file = file; return 0; }
static void __cdecl test_abort(void) { ++abort_calls; }

__attribute__((used)) static uint32_t probe_out[6], probe_target, probe_esp;
__asm__(
    /* cdecl void probe_call(uint32_t start): known callee-saved registers, then a call of
     * the code at `start`; records EAX, EBX, ESI, EDI, EBP and ESP after it. */
    ".text\n_probe_call:\n"
    "    pushl %ebp\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    movl 20(%esp), %eax\n    movl %eax, _probe_target\n"
    "    movl $0xb0b0b0b0, %ebx\n    movl $0x51515151, %esi\n    movl $0xd1d1d1d1, %edi\n    movl %esp, %ebp\n"
    "    movl %esp, _probe_esp\n"
    "    call *_probe_target\n"
    "    movl %eax, _probe_out\n    movl %ebx, _probe_out+4\n    movl %esi, _probe_out+8\n    movl %edi, _probe_out+12\n"
    "    movl %ebp, _probe_out+16\n    movl %esp, _probe_out+20\n"
    "    popl %edi\n    popl %esi\n    popl %ebx\n    popl %ebp\n    ret\n"
    /* cdecl uint32_t fault_store(uint8_t *file): the update _fputc makes, `or word [file+0x12], 0x10`;
     * the exception handler of the test resumes at fault_resume (same ESP, so the ret is valid). */
    "_fault_store:\n"
    "    movl 4(%esp), %eax\n"
    "    orw $0x10, 0x12(%eax)\n"
    "    xorl %eax, %eax\n    ret\n"
    "_fault_resume:\n"
    "    movl $1, %eax\n    ret\n");
extern void __cdecl probe_call(uint32_t start) __asm__("_probe_call");
extern uint32_t __cdecl fault_store(uint8_t *file) __asm__("_fault_store");
extern void fault_resume(void) __asm__("_fault_resume");

/* ---- synthetic image ---- */
#define CODE_RVA 0x1000u
#define CODE_VSIZE 0x96000u
#define CODE_RAW 0x95c00u
#define HOOK_RVA 0x2000u      /* an own function for a framework hook */
#define STRING_RVA 0x97100u
#define OPERAND_RVA 0x97110u
#define NEW_TOKEN 0x1000u
static BYTE *image, *copy;
static void put32(BYTE *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t va(uint32_t rva) { return (uint32_t)(uintptr_t)image + rva; }
static void put_call(BYTE *at, const void *target) { at[0] = 0xe8; put32(at + 1, (uint32_t)(uintptr_t)target - ((uint32_t)(uintptr_t)at + 5)); }
static const BYTE hook_prologue[] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08};
static const BYTE hook_lengths[] = {1, 2, 3};

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
    put32(nt, 0x00004550u); nt[4] = 0x4c; nt[5] = 0x01; nt[6] = 1; nt[20] = 224; nt[24] = 0x0b; nt[25] = 0x01;
    put32(nt + 24 + 28, (uint32_t)(uintptr_t)image);
    put32(nt + 24 + 56, NF_IMAGE_SIZE);
    put32(nt + 24 + 60, 0x600);
    BYTE *h = nt + 24 + 224;                 /* the CODE section header */
    memcpy(h, "CODE", 4);
    put32(h + 8, CODE_VSIZE); put32(h + 12, CODE_RVA); put32(h + 16, CODE_RAW); put32(h + 20, 0x600); put32(h + 36, 0x60000020u);
    /* new: the reference prologue, then lea eax,[ebx+0x1000] (the answer shows the size it saw). */
    BYTE *p = image + NF_NEW_RVA;
    static const BYTE new_head[10] = {0x55, 0x8b, 0xec, 0x53, 0x56, 0x8b, 0x5d, 0x08, 0x3b, 0x1d};
    memcpy(p, new_head, 10); put32(p + 10, va(OPERAND_RVA));
    memcpy(p + 14, "\x8d\x83\x00\x10\x00\x00\x5e\x5b\x5d\xc3", 10);
    /* the registration */
    p = image + NF_REG_RVA;
    memcpy(p, "\x55\x8b\xec\x53\x6a\x08", 6);
    p[6] = 0xe8; put32(p + 7, va(NF_NEW_RVA) - (va(NF_REG_RVA) + 11));
    memcpy(p + 11, "\x59\x8b\xd8\x85\xc0\x75\x17", 7);
    p[18] = 0x68; put32(p + 19, va(NF_FILE_RVA));
    p[23] = 0x68; put32(p + 24, va(STRING_RVA));
    put_call(p + 28, test_fputs);
    memcpy(p + 33, "\x83\xc4\x08", 3);
    put_call(p + 36, test_abort);
    memcpy(p + 41, "\x8b\xc3\x5b\x5d\xc3", 5);   /* mov eax,ebx; pop ebx; pop ebp; ret */
    /* the six sites: push callback; call registration; pop ecx; ret */
    for (unsigned i = 0; i < NF_SITE_COUNT; ++i) {
        BYTE *c = image + nf_site_rvas[i];
        c[-5] = 0x68; put32(c - 4, 0xca11b000u + i);
        c[0] = 0xe8; put32(c + 1, va(NF_REG_RVA) - (va(nf_site_rvas[i]) + 5));
        c[5] = 0x59; c[6] = 0xc3;
    }
    /* an own function for a framework hook */
    memcpy(image + HOOK_RVA, hook_prologue, sizeof(hook_prologue));
    memcpy(image + HOOK_RVA + 6, "\x5d\xc3", 2);
    /* the FILE's bytes: flags word as a stand-in */
    memcpy(image + NF_FILE_RVA, "ES2-FILE-STAND-IN\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 32);
    memcpy(image + STRING_RVA, "message\0", 8);
    /* the loader's protection: CODE read-execute, the rest read-write */
    CHECK(VirtualProtect(image + CODE_RVA, CODE_VSIZE, PAGE_EXECUTE_READ, &ignored));
    FlushInstructionCache(GetCurrentProcess(), image, NF_IMAGE_SIZE);
    memcpy(copy, image, NF_IMAGE_SIZE);
    return 0;
}

static void reset_state(void) {
    if (nf_veh_handle) { RemoveVectoredExceptionHandler(nf_veh_handle); nf_veh_handle = NULL; }
    nf_state = NF_OFF; nf_image = NULL; nf_filter_rva = 0; nf_nth = 0; nf_log_path[0] = 0; nf_site = NULL;
    nf_requests = nf_other = nf_counted = nf_injected = nf_logged = nf_exceptions = 0;
    for (unsigned i = 0; i < NF_SITE_COUNT; ++i) nf_by_site[i] = 0;
    free(nf_pristine); nf_pristine = NULL; nf_pristine_tried = FALSE;
    fputs_calls = abort_calls = 0;
    nf_disarm_exit_probe();
    nf_probe_state = NF_PROBE_OFF; nf_probe_slot = NULL; nf_probe_real = 0; nf_filter_calls = nf_filter_returns = 0;
}
static BOOL unchanged(void) { return !memcmp(copy, image, NF_IMAGE_SIZE); }

static WCHAR log_path[MAX_PATH];
static int fresh_log(const char *name) {
    const char *dir = getenv("TEST_TMPDIR");
    char narrow[MAX_PATH];
    snprintf(narrow, sizeof(narrow), "%s\\%s", dir && *dir ? dir : ".", name);
    for (size_t i = 0; narrow[i]; ++i) if (narrow[i] == '/') narrow[i] = '\\';
    MultiByteToWideChar(CP_ACP, 0, narrow, -1, log_path, MAX_PATH);
    DeleteFileW(log_path);
    return 0;
}
static char *read_log(void) {
    HANDLE f = CreateFileW(log_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
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

/* One call of site i through the registration; registers and ESP must come back.
 * Returns the registration's answer (EAX): the node, here the size + 0x1000, or 0. */
static int run_site(unsigned i, uint32_t *answer) {
    probe_call(va(nf_site_rvas[i] - 5));
    if (probe_out[1] != 0xb0b0b0b0u || probe_out[2] != 0x51515151u || probe_out[3] != 0xd1d1d1d1u ||
        probe_out[4] != probe_esp || probe_out[5] != probe_esp) { fprintf(stderr, "FAIL: registers or stack after site %u\n", i); return 1; }
    *answer = probe_out[0];
    return 0;
}

/* ---- parse ---- */
static int parse_case(void) {
    uint32_t caller = 7, nth = 7;
    CHECK(nf_parse(L"46162b:1", &caller, &nth) && caller == 0x46162bu && nth == 1);
    CHECK(nf_parse(L"0X480FBC:25", &caller, &nth) && caller == 0x480fbcu && nth == 25);
    CHECK(nf_parse(L"ANY:3", &caller, &nth) && caller == 0 && nth == 3);
    CHECK(nf_parse(L"any:0", &caller, &nth) && nth == 0);
    CHECK(nf_parse(L"any:1000000", &caller, &nth) && nth == 1000000);
    CHECK(!nf_parse(L"any:1000001", &caller, &nth) && !nf_parse(L"any", &caller, &nth) && !nf_parse(L"any:", &caller, &nth));
    CHECK(!nf_parse(L":1", &caller, &nth) && !nf_parse(L"zz:1", &caller, &nth) && !nf_parse(L"0:1", &caller, &nth));
    CHECK(!nf_parse(L"any:1x", &caller, &nth) && !nf_parse(L"any:-1", &caller, &nth) && !nf_parse(L"123456789:1", &caller, &nth));
    BOOL malformed;
    SetEnvironmentVariableW(L"ES2_NEW_FAIL", NULL);
    CHECK(!nf_requested(&caller, &nth, &malformed) && !malformed);
    SetEnvironmentVariableW(L"ES2_NEW_FAIL", L"nonsense");
    CHECK(!nf_requested(&caller, &nth, &malformed) && malformed);
    SetEnvironmentVariableW(L"ES2_NEW_FAIL", L"4779e5:2");
    CHECK(nf_requested(&caller, &nth, &malformed) && !malformed && caller == 0x4779e5u && nth == 2);
    SetEnvironmentVariableW(L"ES2_NEW_FAIL", NULL);
    printf("PASS parse: <hex site>|any : <n 0..1000000>, case-insensitive; unset is off, a bad value is malformed\n");
    return 0;
}

/* ---- switch ---- */
static int switch_case(void) {
    uint32_t answer;
    CHECK(!make_image()); reset_state();
    CHECK(!fresh_log("new_fail_switch.ndjson"));
    /* Not installed: the real `new` answers (size + 0x1000), nothing is counted. */
    CHECK(!run_site(0, &answer) && answer == NEW_TOKEN + 8 && nf_requests == 0 && fputs_calls == 0);
    /* Count mode: n = 0 passes everything through. */
    CHECK(nf_install_image(image, (uintptr_t)image, 0, 0, log_path) && nf_state == NF_INSTALLED);
    CHECK(image[NF_CALL_RVA] == 0xe8 && va(NF_CALL_RVA) + 5 + nf_u32(image + NF_CALL_RVA + 1) == (uint32_t)(uintptr_t)nf_entry);
    CHECK(!memcmp(image, copy, NF_CALL_RVA + 1) && !memcmp(image + NF_CALL_RVA + 5, copy + NF_CALL_RVA + 5, NF_IMAGE_SIZE - NF_CALL_RVA - 5));
    CHECK(nf_original_rel == nf_u32(copy + NF_CALL_RVA + 1));
    CHECK(nf_real_target == va(NF_NEW_RVA));
    DWORD page_protect = nf_page_protect(image + NF_CALL_RVA);
    CHECK(page_protect == PAGE_EXECUTE_READ);   /* our own patch restored the page */
    for (unsigned round = 0; round < 2; ++round)
        for (unsigned i = 0; i < NF_SITE_COUNT; ++i) { CHECK(!run_site(i, &answer) && answer == NEW_TOKEN + 8); }
    CHECK(nf_requests == 12 && nf_counted == 12 && nf_other == 0 && nf_injected == 0 && fputs_calls == 0 && abort_calls == 0);
    for (unsigned i = 0; i < NF_SITE_COUNT; ++i) CHECK(nf_by_site[i] == 2);
    /* Other sizes and unknown callers go straight to `new`, uncounted. */
    CHECK(nf_decide(16, va(nf_site_rvas[0]) + 5) == 0 && nf_decide(8, 0x12345678u) == 0 && nf_decide(8, va(nf_site_rvas[0]) + 4) == 0);
    CHECK(nf_other == 3 && nf_counted == 12 && nf_requests == 15);
    char *log = read_log();
    CHECK(log && nf_probe_state == NF_PROBE_OFF && !strstr(log, "new_fail_exit_probe"));   /* Nothing is probed or patched before an injection */
    CHECK(log && count_of(log, "\"event\":\"new_fail_installed\"") == 1 && count_of(log, "\"event\":\"new_fail_request\"") == 12);
    CHECK(strstr(log, "\"site\":\"0x46162b\",\"ordinal\":1,\"ordinal_site\":1,\"inject\":0") && strstr(log, "\"site\":\"0x480fbc\",\"ordinal\":12,\"ordinal_site\":2,\"inject\":0"));
    CHECK(!strstr(log, "new_fail_pages"));
    free(log);

    /* any:5 -> the fifth request (site index 4, 0x480f70) gets NULL, once. */
    CHECK(!make_image()); reset_state(); CHECK(!fresh_log("new_fail_any5.ndjson"));
    CHECK(nf_install_image(image, (uintptr_t)image, 0, 5, log_path));
    for (unsigned i = 0; i < 4; ++i) CHECK(!run_site(i, &answer) && answer == NEW_TOKEN + 8);
    CHECK(fputs_calls == 0 && nf_injected == 0);
    CHECK(!run_site(4, &answer) && answer == 0);     /* NULL: the registration's NULL path ran */
    CHECK(fputs_calls == 1 && fputs_file == va(NF_FILE_RVA) && fputs_string == va(STRING_RVA) && abort_calls == 1 && nf_injected == 1);
    CHECK(!run_site(5, &answer) && answer == NEW_TOKEN + 8);
    CHECK(!run_site(4, &answer) && answer == NEW_TOKEN + 8 && fputs_calls == 1 && abort_calls == 1);
    log = read_log();
    CHECK(log && strstr(log, "\"site\":\"0x480f70\",\"ordinal\":5,\"ordinal_site\":1,\"inject\":1") && count_of(log, "\"inject\":1") == 1);
    CHECK(count_of(log, "\"event\":\"new_fail_pages\"") == 1 && strstr(log, "\"when\":\"inject\""));
    /* The probe is looked for at the injection, once (no CW3220.DLL in this process). */
    CHECK(count_of(log, "\"event\":\"new_fail_exit_probe\"") == 1 && strstr(log, "\"state\":\"no_module\"") && nf_probe_state == NF_PROBE_NO_MODULE);
    free(log);
    CHECK(nf_veh_handle != NULL);

    /* One site: 0x480b23 (index 3), the 2nd of its own requests; the other sites do not count. */
    CHECK(!make_image()); reset_state(); CHECK(!fresh_log("new_fail_site.ndjson"));
    CHECK(nf_install_image(image, (uintptr_t)image, 0x480b23u, 2, log_path));
    CHECK(!run_site(3, &answer) && answer == NEW_TOKEN + 8);
    for (unsigned k = 0; k < 3; ++k) CHECK(!run_site(0, &answer) && answer == NEW_TOKEN + 8);
    CHECK(fputs_calls == 0 && nf_counted == 1 && nf_by_site[0] == 3);
    CHECK(!run_site(3, &answer) && answer == 0 && fputs_calls == 1 && nf_counted == 2);
    CHECK(!run_site(3, &answer) && answer == NEW_TOKEN + 8 && fputs_calls == 1);
    log = read_log();
    CHECK(log && count_of(log, "\"event\":\"new_fail_request\"") == 3 && strstr(log, "\"site\":\"0x480b23\",\"ordinal\":2,\"ordinal_site\":2,\"inject\":1"));
    free(log);

    /* A process has one installation. */
    CHECK(!nf_install_image(image, (uintptr_t)image, 0, 1, NULL) && nf_state == NF_INSTALLED);
    reset_state();
    printf("PASS switch: only the rel32 of the call at 0x479d02 changes and its page keeps PAGE_EXECUTE_READ; count mode passes every request "
           "through with EBX/ESI/EDI/EBP/ESP kept; the n-th counted request (any site or one site) gets NULL once, reaches _fputs(string, FILE) "
           "and _abort, later requests pass; other sizes and unknown callers are not counted\n");
    return 0;
}

/* ---- refuse ---- */
static int refuse_one(const char *what, uint32_t rva, BYTE value, LONG state) {
    if (make_image()) return 1;
    reset_state();
    DWORD old, ignored;
    VirtualProtect(image + (rva & ~0xfffu), 0x1000, PAGE_READWRITE, &old);
    image[rva] ^= value;
    VirtualProtect(image + (rva & ~0xfffu), 0x1000, old, &ignored);
    memcpy(copy, image, NF_IMAGE_SIZE);
    if (nf_install_image(image, (uintptr_t)image, 0, 1, NULL) || nf_state != state || !unchanged()) {
        fprintf(stderr, "FAIL refuse: %s (state %ld)\n", what, (long)nf_state);
        return 1;
    }
    return 0;
}
static int refuse_case(void) {
    if (refuse_one("new prologue", NF_NEW_RVA + 3, 0x01, NF_REFUSED_SITE) || refuse_one("new cmp", NF_NEW_RVA + 9, 0x01, NF_REFUSED_SITE) ||
        refuse_one("registration push ebp", NF_REG_RVA, 0x01, NF_REFUSED_SITE) || refuse_one("registration push 8", NF_REG_RVA + 5, 0x01, NF_REFUSED_SITE) ||
        refuse_one("call opcode", NF_CALL_RVA, 0x02, NF_REFUSED_SITE) || refuse_one("call target", NF_CALL_RVA + 1, 0x10, NF_REFUSED_SITE) ||
        refuse_one("pop ecx after the call", NF_CALL_RVA + 5, 0x01, NF_REFUSED_SITE) || refuse_one("jne", NF_REG_RVA + 16, 0x04, NF_REFUSED_SITE) ||
        refuse_one("FILE operand", NF_REG_RVA + 19, 0x01, NF_REFUSED_SITE) || refuse_one("second push", NF_REG_RVA + 23, 0x01, NF_REFUSED_SITE)) return 1;
    for (unsigned i = 0; i < NF_SITE_COUNT; ++i) {
        if (refuse_one("site opcode", nf_site_rvas[i], 0x02, NF_REFUSED_SITE) || refuse_one("site target", nf_site_rvas[i] + 1, 0x10, NF_REFUSED_SITE) ||
            refuse_one("site pop", nf_site_rvas[i] + 5, 0x01, NF_REFUSED_SITE) || refuse_one("site push", nf_site_rvas[i] - 5, 0x01, NF_REFUSED_SITE)) return 1;
    }
    CHECK(!make_image()); reset_state();
    CHECK(!nf_install_image(image, (uintptr_t)image + 0x10000, 0, 1, NULL) && nf_state == NF_REFUSED_IMAGE && unchanged());
    reset_state();
    put32(image + 0x80 + 24 + 56, NF_IMAGE_SIZE - 0x1000);
    CHECK(!nf_install_image(image, (uintptr_t)image, 0, 1, NULL) && nf_state == NF_REFUSED_IMAGE);
    CHECK(!make_image()); reset_state();
    CHECK(!nf_install_image(image, (uintptr_t)image, 0x461630u, 1, NULL) && nf_state == NF_REFUSED_CONFIG && unchanged());   /* not a call site */
    reset_state();
    CHECK(!nf_install_image(image, (uintptr_t)image, 0, 1000001u, NULL) && nf_state == NF_REFUSED_CONFIG && unchanged());
    reset_state();
    uint32_t answer;
    CHECK(!run_site(0, &answer) && answer == NEW_TOKEN + 8 && nf_requests == 0);   /* a refusal leaves the original call */
    printf("PASS refuse: a changed byte of new, the registration, the call, the FILE operand or any of the six sites, a wrong image, base, "
           "site or n: nothing changed\n");
    return 0;
}

/* ---- pages ---- */
static BOOL WINAPI leave_writable(LPVOID address, SIZE_T bytes, DWORD access, PDWORD previous) {
    if (access == PAGE_EXECUTE_READ) return TRUE;   /* the mutation: the restoration is skipped */
    return VirtualProtect(address, bytes, access, previous);
}
static uint32_t __cdecl hook_replacement(const uint32_t args[ES2_HOOK_MAX_ARGS], void *user) { (void)user; return args[0] + 1; }
static int install_framework_hook(es2_hook *hook, uintptr_t base, const es2_hook_patch_api *api, uint32_t rva) {
    static es2_hook_descriptor d;   /* the framework keeps the pointer */
    d.name = "own_add"; d.rva = rva; d.expected = hook_prologue; d.expected_size = sizeof(hook_prologue);
    d.relocate_size = sizeof(hook_prologue); d.instruction_lengths = hook_lengths; d.instruction_count = sizeof(hook_lengths);
    d.arg_count = 1; d.replacement = hook_replacement;
    es2_hook_options o = {0};
    o.image = image; o.expected_base = base; o.expected_image_size = NF_IMAGE_SIZE; o.mode = ES2_HOOK_REPLACED;
    if (api) o.patch_api = *api;
    return es2_hook_install(hook, &d, &o) ? 0 : 1;
}
static int pages_case(void) {
    CHECK(!make_image()); reset_state(); CHECK(!fresh_log("new_fail_pages.ndjson"));
    nf_scan s;
    /* Unpatched: every page of the section read-execute, none different from the file. */
    CHECK(nf_scan_code(image, copy, &s) == 1 && s.pages == CODE_VSIZE / 0x1000u && s.patched == 0 && s.unpatched == s.pages && s.unknown == 0);
    CHECK(s.kinds == 1 && s.protect[0] == PAGE_EXECUTE_READ && s.writable_patched == 0 && s.writable_unpatched == 0 && s.query_failed == 0);
    /* Without the file the patched state is unknown, the protections are still read. */
    CHECK(nf_scan_code(image, NULL, &s) == 1 && s.unknown == s.pages && s.patched == 0 && s.unpatched == 0 && s.kind_unknown[0] == s.pages);
    /* The switch's patch and a framework hook (framework code in hook/hook.c, as every registered hook). */
    CHECK(nf_install_image(image, (uintptr_t)image, 0, 1, log_path));
    es2_hook hook;
    memset(&hook, 0, sizeof(hook));
    CHECK(!install_framework_hook(&hook, (uintptr_t)image, NULL, HOOK_RVA) && hook.installed);
    CHECK(memcmp(image + HOOK_RVA, copy + HOOK_RVA, 5) != 0 && memcmp(image + NF_CALL_RVA, copy + NF_CALL_RVA, 5) != 0);
    CHECK(nf_scan_code(image, copy, &s) == 1 && s.patched == 2 && s.unpatched == s.pages - 2 && s.kind_patched[0] == 2);
    CHECK(s.kinds == 1 && s.protect[0] == PAGE_EXECUTE_READ && s.writable_patched == 0 && s.writable_unpatched == 0 && s.writable_count == 0);
    CHECK(nf_page_protect(image + (NF_FILE_RVA & ~0xfffu)) == PAGE_EXECUTE_READ);
    /* The report line (the pristine image set directly). */
    nf_pristine = malloc(NF_IMAGE_SIZE); CHECK(nf_pristine); memcpy(nf_pristine, copy, NF_IMAGE_SIZE); nf_pristine_tried = TRUE;
    nf_report_armed();
    char *log = read_log();
    CHECK(log && count_of(log, "\"event\":\"new_fail_pages\"") == 1 && strstr(log, "\"when\":\"armed\"") && strstr(log, "\"pristine\":true"));
    CHECK(strstr(log, "\"pages\":150,\"patched\":2,\"unpatched\":148,\"unknown\":0,\"writable_patched\":0,\"writable_unpatched\":0"));
    CHECK(strstr(log, "\"by_protect\":[{\"protect\":\"0x20\",\"patched\":2,\"unpatched\":148,\"unknown\":0}]") && strstr(log, "\"writable_pages\":[]"));
    CHECK(strstr(log, "\"file_page\":{\"rva\":\"0x96000\",\"protect\":\"0x20\",\"patched\":0}") && strstr(log, "\"call_page\":{\"rva\":\"0x79000\",\"protect\":\"0x20\",\"patched\":1}"));
    free(log);
    CHECK(es2_hook_uninstall(&hook));
    /* Positive control (the mutation): a patch that never restores the protection is reported. */
    es2_hook_patch_api leaky = {leave_writable, NULL};
    memset(&hook, 0, sizeof(hook));
    CHECK(!install_framework_hook(&hook, (uintptr_t)image, &leaky, HOOK_RVA));
    CHECK(nf_page_protect(image + HOOK_RVA) == PAGE_EXECUTE_READWRITE || nf_page_protect(image + HOOK_RVA) == PAGE_EXECUTE_WRITECOPY);
    CHECK(nf_scan_code(image, copy, &s) == 1 && s.writable_patched == 1 && s.writable_count == 1 && s.writable_rva[0] == (HOOK_RVA & ~0xfffu) && s.kinds == 2);
    nf_report_armed();
    log = read_log();
    CHECK(log && strstr(log, "\"writable_patched\":1,\"writable_unpatched\":0") && strstr(log, "\"writable_pages\":[\"0x2000\"]"));
    free(log);
    DWORD ignored;
    CHECK(es2_hook_uninstall(&hook));
    CHECK(VirtualProtect(image + HOOK_RVA, 0x1000, PAGE_EXECUTE_READ, &ignored));
    /* A writable page nobody patched (here the FILE's) is reported as unpatched and writable. */
    CHECK(VirtualProtect(image + NF_FILE_RVA, 0x1000, PAGE_EXECUTE_READWRITE, &ignored));
    CHECK(nf_scan_code(image, copy, &s) == 1 && s.writable_unpatched == 1 && s.writable_rva[0] == (NF_FILE_RVA & ~0xfffu));
    CHECK(VirtualProtect(image + NF_FILE_RVA, 0x1000, PAGE_EXECUTE_READ, &ignored));
    /* The file mapper: sections at their RVAs, bounds-checked. */
    BYTE file[0x2000];
    memset(file, 0, sizeof(file));
    file[0] = 'M'; file[1] = 'Z'; put32(file + 0x3c, 0x80);
    BYTE *nt = file + 0x80;
    put32(nt, 0x00004550u); nt[6] = 1; nt[20] = 224; put32(nt + 24 + 56, 0x4000); put32(nt + 24 + 60, 0x400);
    BYTE *h = nt + 24 + 224;
    put32(h + 8, 0x1000); put32(h + 12, 0x1000); put32(h + 16, 0x200); put32(h + 20, 0x400); put32(h + 36, 0x60000020u);
    memcpy(file + 0x400, "section-bytes", 14);
    BYTE *mapped = nf_map_file(file, sizeof(file), 0x4000);
    CHECK(mapped && !memcmp(mapped + 0x1000, "section-bytes", 14) && mapped[0] == 'M' && mapped[0x1300] == 0);
    free(mapped);
    CHECK(!nf_map_file(file, sizeof(file), 0x5000));            /* SizeOfImage differs */
    CHECK(!nf_map_file(file, 0x500, 0x4000));                   /* raw data beyond the file */
    put32(h + 16, 0x4000); put32(h + 12, 0x2000);
    CHECK(!nf_map_file(file, sizeof(file), 0x4000));            /* section beyond the image */
    CHECK(!nf_map_file(file, 0x100, 0x4000));
    reset_state();
    printf("PASS pages: patched pages (the switch's call page, a framework hook) keep PAGE_EXECUTE_READ and the report says 2 patched, 148 unpatched, "
           "0 writable; a patch that never restores the protection and a writable unpatched page are reported; the file mapper is bounds-checked\n");
    return 0;
}

/* ---- exception ---- */
static volatile LONG resumed;
static LONG CALLBACK resume_handler(EXCEPTION_POINTERS *x) {
    if (x->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement(&resumed);
    x->ContextRecord->Eip = (DWORD)(uintptr_t)fault_resume;
    return EXCEPTION_CONTINUE_EXECUTION;
}
static int exception_case(void) {
    uint32_t answer;
    CHECK(!make_image()); reset_state(); CHECK(!fresh_log("new_fail_exception.ndjson"));
    /* Before the injection the recorder is not even registered; a direct call records nothing. */
    CHECK(nf_install_image(image, (uintptr_t)image, 0, 2, log_path));
    CHECK(nf_veh_handle == NULL);
    EXCEPTION_RECORD record = {0};
    CONTEXT context = {0};
    EXCEPTION_POINTERS pointers = {&record, &context};
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    CHECK(nf_veh(&pointers) == EXCEPTION_CONTINUE_SEARCH && nf_exceptions == 0);
    CHECK(!run_site(0, &answer) && answer == NEW_TOKEN + 8);
    CHECK(!run_site(1, &answer) && answer == 0 && nf_injected == 1 && nf_veh_handle != NULL);
    PVOID resume = AddVectoredExceptionHandler(0, resume_handler);   /* registered after ours (at the end), so ours sees the fault first */
    CHECK(resume);
    /* The FILE's page is read-execute: the update _fputc makes faults, the record names it. */
    CHECK(fault_store(image + NF_FILE_RVA) == 1 && resumed == 1);
    CHECK(fault_store(image + NF_FILE_RVA) == 1 && resumed == 2);
    CHECK(nf_exceptions == 2);
    char *log = read_log();
    CHECK(log && count_of(log, "\"event\":\"new_fail_exception\"") == 2);
    CHECK(strstr(log, "\"code\":\"0xc0000005\"") && strstr(log, "\"access\":1,") && strstr(log, "\"target_protect\":\"0x20\"") && strstr(log, "\"esp_on_thread_stack\":true"));
    char want[64];
    snprintf(want, sizeof(want), "\"target\":\"0x%lx\"", (unsigned long)(va(NF_FILE_RVA) + 0x12));
    CHECK(strstr(log, want));
    CHECK(count_of(log, "\"event\":\"new_fail_pages\"") == 1);
    CHECK(count_of(log, "\"seh_end\":\"") == 2 && count_of(log, "\"seh\":[") == 2);   /* The chain is part of the record */
    free(log);
    /* Only severe codes are recorded (not a debugger message). */
    record.ExceptionCode = 0x40010006;
    CHECK(nf_veh(&pointers) == EXCEPTION_CONTINUE_SEARCH && nf_exceptions == 2);
    RemoveVectoredExceptionHandler(resume);
    reset_state();
    printf("PASS exception: after the injection the access violation on the read-only FILE page is recorded (write, target %s, "
           "PAGE_EXECUTE_READ, ESP on the thread's stack) and passed on; before it nothing is registered\n", "0x4968c8");
    return 0;
}

/* ---- The SEH chain walk ---- */
static void exe_name(char *out, size_t size) {
    WCHAR path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    const WCHAR *name = n && n < MAX_PATH ? wcsrchr(path, L'\\') : NULL;
    name = name ? name + 1 : path;
    size_t i = 0;
    for (; name[i] && i + 1 < size; ++i) out[i] = (char)name[i];
    out[i] = 0;
}
static int seh_case(void) {
    BYTE *region = VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(region);
    char chain[1536], want[160], name[MAX_PATH];
    const char *end;
    exe_name(name, sizeof(name));
    uint32_t handler = (uint32_t)(uintptr_t)probe_call;
    uint32_t *r = (uint32_t *)region;      /* 12 records of two words: next, handler */
    for (unsigned i = 0; i < 12; ++i) { r[2 * i] = (uint32_t)(uintptr_t)&r[2 * (i + 1)]; r[2 * i + 1] = handler; }
    /* Three records, the last links to -1: all listed, in order, the handler named by module and offset. */
    r[2 * 2] = 0xffffffffu;
    CHECK(nf_seh_walk((uintptr_t)r, chain, sizeof(chain), &end) == 3 && !strcmp(end, "terminated"));
    snprintf(want, sizeof(want), "{\"frame\":\"0x%lx\",\"handler\":\"0x%lx\",\"module\":\"%s\",\"offset\":\"0x%lx\",\"on_thread_stack\":false}",
             (unsigned long)(uintptr_t)r, (unsigned long)handler, name, (unsigned long)(handler - (uint32_t)(uintptr_t)GetModuleHandleW(NULL)));
    CHECK(!strncmp(chain, want, strlen(want)) && count_of(chain, "\"frame\"") == 3);
    /* No chain, and an empty head. */
    CHECK(nf_seh_walk(0xffffffffu, chain, sizeof(chain), &end) == 0 && !strcmp(end, "terminated") && !chain[0]);
    /* Misaligned, unreadable and backwards links stop the walk without a read. */
    CHECK(nf_seh_walk((uintptr_t)r + 1, chain, sizeof(chain), &end) == 0 && !strcmp(end, "bad"));
    CHECK(nf_seh_walk(0x10, chain, sizeof(chain), &end) == 0 && !strcmp(end, "bad"));
    CHECK(nf_seh_walk(0, chain, sizeof(chain), &end) == 0 && !strcmp(end, "bad"));
    r[2 * 2] = (uint32_t)(uintptr_t)&r[2 * 1];                       /* record 2 links back to record 1 */
    CHECK(nf_seh_walk((uintptr_t)r, chain, sizeof(chain), &end) == 3 && !strcmp(end, "backwards"));
    r[2 * 2] = 0x00000007u;                                            /* misaligned next */
    CHECK(nf_seh_walk((uintptr_t)r, chain, sizeof(chain), &end) == 3 && !strcmp(end, "bad"));
    r[2 * 2] = (uint32_t)(uintptr_t)&r[2 * 3];
    /* A last record at the end of the region: its handler word is unreadable. */
    BYTE *edge = region + 0x1000 - 4;
    DWORD ignored;
    CHECK(VirtualProtect(region + 0x1000, 0x1000, PAGE_NOACCESS, &ignored));
    r[2 * 3] = (uint32_t)(uintptr_t)edge;
    CHECK(nf_seh_walk((uintptr_t)r, chain, sizeof(chain), &end) == 4 && !strcmp(end, "bad"));
    CHECK(VirtualProtect(region + 0x1000, 0x1000, PAGE_READWRITE, &ignored));
    r[2 * 3] = (uint32_t)(uintptr_t)&r[2 * 4];
    /* More than NF_SEH_MAX records: the first eight, "truncated". */
    CHECK(nf_seh_walk((uintptr_t)r, chain, sizeof(chain), &end) == NF_SEH_MAX && !strcmp(end, "truncated") && count_of(chain, "\"frame\"") == (unsigned)NF_SEH_MAX);
    /* A buffer that cannot hold the items ends the walk with what fits (valid JSON items only). */
    unsigned fit = nf_seh_walk((uintptr_t)r, chain, 300, &end);
    CHECK(fit >= 1 && fit < NF_SEH_MAX && !strcmp(end, "truncated") && count_of(chain, "{") == fit && chain[strlen(chain) - 1] == '}');
    /* Records on the thread's own stack are marked so. */
    uint32_t local[4] = {(uint32_t)(uintptr_t)&local[2], handler, 0xffffffffu, handler};
    CHECK(nf_seh_walk((uintptr_t)local, chain, sizeof(chain), &end) == 2 && count_of(chain, "\"on_thread_stack\":true") == 2);
    VirtualFree(region, 0, MEM_RELEASE);
    printf("PASS seh: the exception registration chain is listed with handler module and offset and stack membership; misaligned, unreadable, "
           "backwards links and more than %u records stop the walk; a small buffer keeps whole items\n", (unsigned)NF_SEH_MAX);
    return 0;
}

/* ---- The log path's {suite} ---- */
static int suite_case(void) {
    const char *dir = getenv("TEST_TMPDIR");
    char narrow[MAX_PATH];
    snprintf(narrow, sizeof(narrow), "%s\\suite-test.ini", dir && *dir ? dir : ".");
    for (size_t i = 0; narrow[i]; ++i) if (narrow[i] == '/') narrow[i] = '\\';
    WCHAR ini[MAX_PATH], suite[65], out[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, narrow, -1, ini, MAX_PATH);
    const char *texts[] = {
        "[video]\r\nlog=x\r\n[trace]\r\ntick_log=D:\\ev\\r\\acceptance-60s\\run\\acceptance-60s.jsonl\r\ntick_log_max=9\r\n",
        "[trace]\r\ntick_log=D:\\ev\\a b$c.v2.jsonl\r\n",
        "[trace]\r\ntick_log=D:/ev/forward-slash/run/slashes_1.jsonl\r\n",
        "[trace]\r\ntick_log_max=9\r\n",
        "[trace]\r\ntick_log=D:\\ev\\run\\.jsonl\r\n"};
    const WCHAR *want[] = {L"acceptance-60s", L"a_b_c_v2", L"slashes_1", L"unknown", L"unknown"};
    for (unsigned i = 0; i < 5; ++i) {
        DeleteFileW(ini);
        HANDLE f = CreateFileW(ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(f != INVALID_HANDLE_VALUE);
        DWORD n;
        CHECK(WriteFile(f, texts[i], (DWORD)strlen(texts[i]), &n, NULL));
        CloseHandle(f);
        nf_suite_name(ini, suite, 65);
        if (wcscmp(suite, want[i])) { fprintf(stderr, "FAIL suite %u: got %ls\n", i, suite); return 1; }
    }
    DeleteFileW(ini);
    nf_suite_name(ini, suite, 65);
    CHECK(!wcscmp(suite, L"unknown"));                       /* no INI */
    nf_suite_name(NULL, suite, 65);
    CHECK(!wcscmp(suite, L"unknown"));
    WCHAR long_name[200];
    snprintf(narrow, sizeof(narrow), "[trace]\r\ntick_log=D:\\x\\%0100d.jsonl\r\n", 7);
    {
        HANDLE f = CreateFileW(ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(f != INVALID_HANDLE_VALUE);
        DWORD n;
        WriteFile(f, narrow, (DWORD)strlen(narrow), &n, NULL);
        CloseHandle(f);
    }
    nf_suite_name(ini, long_name, 200);
    CHECK(wcslen(long_name) == 64);
    DeleteFileW(ini);
    /* The expansion. */
    CHECK(nf_expand_suite(L"D:\\ev\\new-fail-{suite}.ndjson", L"acceptance-60s", out, MAX_PATH) && !wcscmp(out, L"D:\\ev\\new-fail-acceptance-60s.ndjson"));
    CHECK(nf_expand_suite(L"{suite}\\{suite}.log", L"s", out, MAX_PATH) && !wcscmp(out, L"s\\s.log"));
    CHECK(nf_expand_suite(L"D:\\ev\\plain.ndjson", L"s", out, MAX_PATH) && !wcscmp(out, L"D:\\ev\\plain.ndjson"));
    CHECK(nf_expand_suite(L"{Suite}{suite", L"s", out, MAX_PATH) && !wcscmp(out, L"{Suite}{suite"));
    CHECK(!nf_expand_suite(L"abc{suite}", L"0123456789", out, 12) && nf_expand_suite(L"abc{suite}", L"012345678", out, 13) && !wcscmp(out, L"abc012345678"));
    CHECK(!nf_expand_suite(L"abcdefghijkl", L"s", out, 12));
    printf("PASS suite: the tick log's stem names the suite (sanitised, 64 characters at most, `unknown` without INI or key); {suite} is replaced "
           "everywhere in the log path and nowhere else, an overflow is refused\n");
    return 0;
}

/* ---- The exit-path probe ---- */
static volatile LONG filter_calls;
static EXCEPTION_POINTERS *volatile filter_argument;
static LONG WINAPI fake_filter(EXCEPTION_POINTERS *x) { ++filter_calls; filter_argument = x; return 7; }
__attribute__((used)) static uint32_t std_before, std_after;
__asm__(
    /* cdecl uint32_t std_probe(void *slot_target, void *argument): pushes the argument, calls the target as
     * a stdcall function, records ESP before the push and after the call (equal when the callee popped 4). */
    ".text\n_std_probe:\n"
    "    pushl %ebp\n    movl %esp, %ebp\n"
    "    movl %esp, _std_before\n"
    "    pushl 12(%ebp)\n"
    "    call *8(%ebp)\n"
    "    movl %esp, _std_after\n"
    "    popl %ebp\n    ret\n");
extern uint32_t __cdecl std_probe(void *target, void *argument) __asm__("_std_probe");

#define PE_SIZE 0x4000u
#define PE_SLOT_RVA 0x1308u             /* kernel32.dll's UnhandledExceptionFilter */
#define PE_OTHER_SLOT_RVA 0x1340u       /* user32.dll's (a decoy with the same function name) */
static void put_name(BYTE *m, uint32_t rva, const char *text) { m[rva] = 0; m[rva + 1] = 0; strcpy((char *)m + rva + 2, text); }
/* A PE32 image in memory layout with two imported DLLs: user32.dll (a decoy that also "imports"
 * UnhandledExceptionFilter) first, then kernel32.dll with ExitProcess, an ordinal and UnhandledExceptionFilter
 * (slot PE_SLOT_RVA, content `real`). Without `oft` the original-first-thunk fields are zero. */
static BYTE *make_pe(uint32_t real, int oft) {
    BYTE *m = VirtualAlloc(NULL, PE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!m) return NULL;
    m[0] = 'M'; m[1] = 'Z'; put32(m + 0x3c, 0x80);
    BYTE *nt = m + 0x80;
    put32(nt, 0x00004550u); nt[4] = 0x4c; nt[5] = 0x01; nt[6] = 0; nt[20] = 224; nt[24] = 0x0b; nt[25] = 0x01;
    put32(nt + 8, 0x12345678u);
    put32(nt + 24 + 56, PE_SIZE);
    put32(nt + 24 + 92, 16);
    put32(nt + 24 + 96 + 8, 0x1000); put32(nt + 24 + 96 + 12, 0x40);
    BYTE *d0 = m + 0x1000, *d1 = m + 0x1014;
    put32(d0, oft ? 0x1140u : 0); put32(d0 + 12, 0x1210); put32(d0 + 16, 0x1340);
    put32(d1, oft ? 0x1100u : 0); put32(d1 + 12, 0x1200); put32(d1 + 16, 0x1300);
    strcpy((char *)m + 0x1200, "KERNEL32.dll");
    strcpy((char *)m + 0x1210, "user32.dll");
    put_name(m, 0x1400, "ExitProcess");
    put_name(m, 0x1420, "UnhandledExceptionFilter");
    put_name(m, 0x1440, "UnhandledExceptionFilter");
    put32(m + 0x1100, 0x80000001u); put32(m + 0x1104, 0x1400); put32(m + 0x1108, 0x1420);
    put32(m + 0x1140, 0x1440);
    put32(m + 0x1300, 0xaaaa1111u); put32(m + 0x1304, 0xbbbb2222u); put32(m + 0x1308, real);
    put32(m + 0x1340, 0xcccc3333u);
    if (!oft) { put32(m + 0x1300, 0x77001000u); put32(m + 0x1304, 0x77002000u); put32(m + 0x1308, real); }   /* the first thunks hold the loader's addresses */
    return m;
}

static int import_case(void) {
    uint32_t rva = 0;
    BYTE *m = make_pe(0x00401234u, 1);
    CHECK(m);
    CHECK(nf_find_import(m, PE_SIZE, "kernel32.dll", "UnhandledExceptionFilter", &rva) && rva == PE_SLOT_RVA);      /* the DLL name matches without case */
    CHECK(nf_find_import(m, PE_SIZE, "USER32.DLL", "UnhandledExceptionFilter", &rva) && rva == PE_OTHER_SLOT_RVA);
    CHECK(nf_find_import(m, PE_SIZE, "kernel32.dll", "ExitProcess", &rva) && rva == 0x1304u);                        /* the ordinal entry is skipped, not counted away */
    CHECK(!nf_find_import(m, PE_SIZE, "kernel32.dll", "unhandledexceptionfilter", &rva));                           /* the function name is exact */
    CHECK(!nf_find_import(m, PE_SIZE, "kernel32.dll", "SetUnhandledExceptionFilter", &rva) && !nf_find_import(m, PE_SIZE, "advapi32.dll", "ExitProcess", &rva));
    /* Hostile or damaged tables: nothing is read outside the image, nothing is found. */
    BYTE *nt = m + 0x80;
    BYTE saved[0x2000];
    memcpy(saved, m, sizeof(saved));
    put32(nt + 24 + 96 + 8, PE_SIZE - 10);       CHECK(!nf_find_import(m, PE_SIZE, "kernel32.dll", "ExitProcess", &rva)); memcpy(m, saved, sizeof(saved));
    put32(nt + 24 + 96 + 8, 0);                  CHECK(!nf_find_import(m, PE_SIZE, "kernel32.dll", "ExitProcess", &rva)); memcpy(m, saved, sizeof(saved));
    put32(nt + 24 + 92, 1);                      CHECK(!nf_find_import(m, PE_SIZE, "kernel32.dll", "ExitProcess", &rva)); memcpy(m, saved, sizeof(saved));
    put32(nt, 0x00004551u);                      CHECK(!nf_find_import(m, PE_SIZE, "kernel32.dll", "ExitProcess", &rva)); memcpy(m, saved, sizeof(saved));
    nt[24] = 0x0b; nt[25] = 0x02;                CHECK(!nf_find_import(m, PE_SIZE, "kernel32.dll", "ExitProcess", &rva)); memcpy(m, saved, sizeof(saved));
    put32(m + 0x1014 + 12, PE_SIZE + 4);         CHECK(!nf_find_import(m, PE_SIZE, "kernel32.dll", "ExitProcess", &rva)); memcpy(m, saved, sizeof(saved));
    put32(m + 0x1014, PE_SIZE - 2);              CHECK(!nf_find_import(m, PE_SIZE, "kernel32.dll", "ExitProcess", &rva)); memcpy(m, saved, sizeof(saved));
    put32(m + 0x1100 + 8, PE_SIZE - 1);          CHECK(!nf_find_import(m, PE_SIZE, "kernel32.dll", "UnhandledExceptionFilter", &rva)); memcpy(m, saved, sizeof(saved));
    memset(m + 0x1200, 'k', 0x100);              CHECK(!nf_find_import(m, PE_SIZE, "kernel32.dll", "ExitProcess", &rva)); memcpy(m, saved, sizeof(saved));   /* a name without a terminator */
    put32(m + 0x1014 + 16, PE_SIZE - 6);         CHECK(!nf_find_import(m, PE_SIZE, "kernel32.dll", "UnhandledExceptionFilter", &rva)); memcpy(m, saved, sizeof(saved));   /* the slot would lie outside */
    CHECK(!nf_find_import(m, 0x800, "kernel32.dll", "ExitProcess", &rva));
    CHECK(nf_find_import(m, PE_SIZE, "kernel32.dll", "ExitProcess", &rva));
    VirtualFree(m, 0, MEM_RELEASE);
    /* Once the loader has filled the first thunks and there is no original table, no name is left to find. */
    m = make_pe(0x00401234u, 0);
    CHECK(m && !nf_find_import(m, PE_SIZE, "kernel32.dll", "UnhandledExceptionFilter", &rva));
    VirtualFree(m, 0, MEM_RELEASE);
    printf("PASS import: the import table walk finds a function by DLL (case-insensitive) and exact name, skips ordinals, uses the original "
           "table, and finds nothing in damaged or hostile tables\n");
    return 0;
}

__attribute__((noinline)) static uint32_t call_slot(const BYTE *m, EXCEPTION_POINTERS *x) {
    uint32_t target = nf_u32(m + PE_SLOT_RVA);
    return std_probe((void *)(uintptr_t)target, x);
}

static int probe_case(void) {
    CHECK(!make_image()); reset_state(); CHECK(!fresh_log("new_fail_probe.ndjson")); wcscpy(nf_log_path, log_path);
    const uintptr_t real = (uintptr_t)fake_filter;
    uint32_t rva = 0;
    DWORD ignored;
    /* Arming: only the slot of kernel32's UnhandledExceptionFilter changes, to the thunk; the page keeps its protection. */
    BYTE *m = make_pe((uint32_t)real, 1);
    CHECK(m);
    BYTE *whole = malloc(PE_SIZE);
    CHECK(whole);
    memcpy(whole, m, PE_SIZE);
    CHECK(nf_arm_exit_probe_in(m, m, PE_SIZE, real, &rva) == NF_PROBE_ARMED && rva == PE_SLOT_RVA && nf_probe_state == NF_PROBE_ARMED);
    CHECK(nf_u32(m + PE_SLOT_RVA) == (uint32_t)(uintptr_t)nf_exit_probe && nf_probe_real == (uint32_t)real && nf_probe_slot == (uint32_t *)(m + PE_SLOT_RVA));
    CHECK(!memcmp(m, whole, PE_SLOT_RVA) && !memcmp(m + PE_SLOT_RVA + 4, whole + PE_SLOT_RVA + 4, PE_SIZE - PE_SLOT_RVA - 4));
    CHECK(nf_page_protect(m + PE_SLOT_RVA) == PAGE_READWRITE);
    /* A call through the slot: stdcall with one argument, the real filter gets the same pointer and its answer comes back. */
    EXCEPTION_RECORD record = {0};
    CONTEXT context = {0};
    EXCEPTION_POINTERS pointers = {&record, &context};
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    record.ExceptionAddress = (PVOID)(uintptr_t)fault_store;
    context.Esp = (DWORD)(uintptr_t)&record;
    CHECK(call_slot(m, &pointers) == 7 && filter_calls == 1 && filter_argument == &pointers);
    CHECK(std_after == std_before);                                           /* the callee popped exactly its one argument */
    CHECK(nf_filter_calls == 1 && nf_filter_returns == 1);
    char *log = read_log();
    CHECK(log && count_of(log, "\"event\":\"new_fail_exit_filter\"") == 1 && count_of(log, "\"event\":\"new_fail_exit_filter_result\"") == 1);
    CHECK(strstr(log, "\"call\":1,\"return\":\"0x") && strstr(log, "\"code\":\"0xc0000005\"") && strstr(log, "\"sp_on_thread_stack\":true") &&
          strstr(log, "\"context_esp_on_thread_stack\":true") && strstr(log, "\"seh_end\":\"") && strstr(log, "\"call\":1,\"result\":7"));
    char name[MAX_PATH], want[256];
    exe_name(name, sizeof(name));
    snprintf(want, sizeof(want), "\"caller_module\":\"%s\"", name);
    CHECK(strstr(log, want));
    snprintf(want, sizeof(want), "\"fault_module\":\"%s\",\"fault_offset\":\"0x%lx\"", name, (unsigned long)((uintptr_t)fault_store - (uintptr_t)GetModuleHandleW(NULL)));
    CHECK(strstr(log, want));
    const char *at = strstr(log, "\"return\":\"0x");
    unsigned long returned = at ? strtoul(at + 10, NULL, 16) : 0;
    CHECK(returned > (unsigned long)(uintptr_t)std_probe && returned < (unsigned long)(uintptr_t)std_probe + 32);   /* the return address is in the caller */
    free(log);
    /* Only the first NF_FILTER_MAX calls are recorded; every call is forwarded and counted. */
    for (unsigned i = 0; i < 5; ++i) CHECK(call_slot(m, &pointers) == 7);
    CHECK(nf_filter_calls == 6 && nf_filter_returns == 6 && filter_calls == 6);
    log = read_log();
    CHECK(log && count_of(log, "\"event\":\"new_fail_exit_filter\"") == NF_FILTER_MAX && count_of(log, "\"event\":\"new_fail_exit_filter_result\"") == NF_FILTER_MAX);
    free(log);
    /* An argument that cannot be read is recorded as such and still forwarded. */
    CHECK(call_slot(m, (EXCEPTION_POINTERS *)(uintptr_t)0x10) == 7 && filter_argument == (EXCEPTION_POINTERS *)(uintptr_t)0x10);
    nf_disarm_exit_probe();
    CHECK(nf_u32(m + PE_SLOT_RVA) == (uint32_t)real && nf_probe_state == NF_PROBE_OFF && !memcmp(m, whole, PE_SIZE));
    /* Refusals leave the image alone: a slot that holds another address (the signature), a missing import, a missing original table. */
    CHECK(nf_arm_exit_probe_in(m, m, PE_SIZE, real + 1, &rva) == NF_PROBE_MISMATCH && rva == PE_SLOT_RVA && !memcmp(m, whole, PE_SIZE) && nf_probe_state == NF_PROBE_OFF);
    CHECK(nf_arm_exit_probe_in(m, m, PE_SIZE, 0, &rva) == NF_PROBE_NO_IMPORT && !memcmp(m, whole, PE_SIZE));
    BYTE *bare = make_pe((uint32_t)real, 0);
    CHECK(bare && nf_arm_exit_probe_in(bare, bare, PE_SIZE, real, &rva) == NF_PROBE_NO_IMPORT && nf_u32(bare + PE_SLOT_RVA) == (uint32_t)real);
    VirtualFree(bare, 0, MEM_RELEASE);
    CHECK(VirtualProtect(m + 0x1000, 0x1000, PAGE_NOACCESS, &ignored));
    CHECK(nf_arm_exit_probe_in(m, whole, PE_SIZE, real, &rva) == NF_PROBE_MISMATCH);          /* the slot cannot be read */
    CHECK(VirtualProtect(m + 0x1000, 0x1000, PAGE_READWRITE, &ignored));
    VirtualFree(m, 0, MEM_RELEASE);
    free(whole);

    /* The module-level entry, on this process's own image (it imports kernel32's UnhandledExceptionFilter by name):
     * the page-wise copy, the signature against kernel32's export, the record. */
    HMODULE self = GetModuleHandleW(NULL), kernel32 = GetModuleHandleW(L"kernel32.dll");
    uintptr_t export_address = (uintptr_t)GetProcAddress(kernel32, "UnhandledExceptionFilter");
    CHECK(export_address);
    uint32_t self_rva = 0;
    {
        BYTE *copy = malloc(nf_u32((const BYTE *)self + nf_u32((const BYTE *)self + 0x3c) + 24 + 56));
        uint32_t size = nf_u32((const BYTE *)self + nf_u32((const BYTE *)self + 0x3c) + 24 + 56);
        CHECK(copy);
        for (uint32_t at_page = 0; at_page < size; at_page += 0x1000) {
            SIZE_T got;
            if (!ReadProcessMemory(GetCurrentProcess(), (BYTE *)self + at_page, copy + at_page, size - at_page < 0x1000 ? size - at_page : 0x1000, &got)) memset(copy + at_page, 0, 0x1000);
        }
        CHECK(nf_find_import(copy, size, "kernel32.dll", "UnhandledExceptionFilter", &self_rva));
        free(copy);
    }
    uint32_t *self_slot = (uint32_t *)((BYTE *)self + self_rva);
    CHECK(*self_slot == (uint32_t)export_address);
    CHECK(!fresh_log("new_fail_probe_module.ndjson"));
    nf_log_path[0] = 0; wcscpy(nf_log_path, log_path);
    CHECK(nf_arm_exit_probe_module(self, export_address + 1) == NF_PROBE_MISMATCH && *self_slot == (uint32_t)export_address);
    reset_state(); wcscpy(nf_log_path, log_path);
    CHECK(nf_arm_exit_probe_module(self, export_address) == NF_PROBE_ARMED && *self_slot == (uint32_t)(uintptr_t)nf_exit_probe && nf_probe_real == (uint32_t)export_address);
    CHECK(nf_arm_exit_probe_module(self, export_address) == NF_PROBE_ARMED);                    /* once: no second record, no second patch */
    nf_disarm_exit_probe();
    CHECK(*self_slot == (uint32_t)export_address);
    reset_state(); wcscpy(nf_log_path, log_path);
    CHECK(nf_arm_exit_probe_module(GetModuleHandleW(L"ntdll.dll"), export_address) == NF_PROBE_NO_IMPORT);
    reset_state(); wcscpy(nf_log_path, log_path);
    CHECK(nf_arm_exit_probe_module(NULL, export_address) == NF_PROBE_NO_MODULE);
    reset_state(); wcscpy(nf_log_path, log_path);
    BYTE *zero = VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(zero && nf_arm_exit_probe_module((HMODULE)zero, export_address) == NF_PROBE_BAD_IMAGE);
    VirtualFree(zero, 0, MEM_RELEASE);
    reset_state(); wcscpy(nf_log_path, log_path);
    CHECK(nf_arm_exit_probe() == NF_PROBE_NO_MODULE);                                             /* no CW3220.DLL in this process */
    log = read_log();
    CHECK(log && count_of(log, "\"event\":\"new_fail_exit_probe\"") == 6);
    CHECK(count_of(log, "\"state\":\"slot_mismatch\"") == 1 && count_of(log, "\"state\":\"armed\"") == 1 && count_of(log, "\"state\":\"no_import\"") == 1 &&
          count_of(log, "\"state\":\"no_module\"") == 2 && count_of(log, "\"state\":\"bad_image\"") == 1);
    snprintf(want, sizeof(want), "\"slot_rva\":\"0x%lx\",\"slot_was\":\"0x%lx\",\"kernel32_export\":\"0x%lx\"", (unsigned long)self_rva, (unsigned long)export_address, (unsigned long)export_address);
    CHECK(strstr(log, want));
    free(log);
    reset_state();
    printf("PASS probe: only the import slot of kernel32's UnhandledExceptionFilter changes, and only when it holds kernel32's export; the thunk is "
           "stdcall, forwards the argument and the answer, records the first %u calls with return address, exception, stack and SEH chain; "
           "refusals (mismatch, no import, no module, bad image) change nothing; nothing is probed before an injection\n", (unsigned)NF_FILTER_MAX);
    return 0;
}

/* ---- the reference file (ES2_DATA_ROOT) ---- */
static uint32_t file_exit_filter_slot(const char *path, BYTE **file_out, BYTE **map_out, uint32_t *size_out);
static const uint32_t registry_rvas[] = {
#define ES2_HOOK(name, rva, descriptor, extension, group) rva,
#include "../hook_registry.h"
#undef ES2_HOOK
};
static int reference_case(void) {
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) { puts("SKIP: ES2_DATA_ROOT unset"); return 0; }
    char path[1024];
    snprintf(path, sizeof(path), "%s/VER95/DBSIM.EXE", root);
    FILE *f = fopen(path, "rb");
    CHECK(f);
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    CHECK(size == 724512);
    BYTE *bytes = malloc((size_t)size);
    CHECK(bytes && fread(bytes, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    BYTE *map = nf_map_file(bytes, (size_t)size, NF_IMAGE_SIZE);
    CHECK(map);
    CHECK(nf_site_valid(map, NF_LINK_BASE));
    /* The scan on the unpatched mapping: one code section, 150 pages, none different from the file. */
    nf_scan s;
    CHECK(nf_scan_code(map, map, &s) == 1 && s.pages == 150 && s.patched == 0);
    /* CODE: read-execute only, and it contains the FILE (file-backed). */
    uint32_t pe = nf_u32(map + 0x3c);
    const BYTE *h = map + pe + 24 + nf_u16(map + pe + 20);
    CHECK(!memcmp(h, "CODE", 4) && nf_u32(h + 36) == 0x60000020u && nf_u32(h + 12) == CODE_RVA && nf_u32(h + 8) == CODE_VSIZE && nf_u32(h + 16) == CODE_RAW);
    CHECK(NF_FILE_RVA >= CODE_RVA && NF_FILE_RVA + 0x30 < CODE_RVA + CODE_RAW);
    /* Every `call 0x479cfc` of the code section: the six sites among them; `call 0x47749c` inside the registration only once. */
    unsigned in_site_list = 0, new_calls_in_registration = 0;
    for (uint32_t r = CODE_RVA; r + 5 <= CODE_RVA + CODE_RAW; ++r) {
        if (map[r] != 0xe8) continue;
        uint32_t target = r + 5 + nf_u32(map + r + 1);
        if (target == NF_REG_RVA) for (unsigned i = 0; i < NF_SITE_COUNT; ++i) in_site_list += r == nf_site_rvas[i];
        if (target == NF_NEW_RVA && r >= NF_REG_RVA && r < NF_REG_RVA + 0x50) ++new_calls_in_registration;
    }
    CHECK(in_site_list == NF_SITE_COUNT && new_calls_in_registration == 1);
    /* No registered hook prologue covers the call's five bytes. */
    for (unsigned i = 0; i < sizeof(registry_rvas) / sizeof(registry_rvas[0]); ++i)
        CHECK(!(registry_rvas[i] + 32 > NF_CALL_RVA && registry_rvas[i] < NF_CALL_RVA + 5));
    free(map); free(bytes);
    /* The original runtime. CW3220.DLL's import of UnhandledExceptionFilter (the target of __startup's record,
     * rtl_imports.md "the exit path under the `new` failure") is found by name; the slot is RVA 0x24308, so VA 0x424308 at its link base. */
    snprintf(path, sizeof(path), "%s/CW3220.DLL", root);
    uint32_t dll_size = 0, slot = file_exit_filter_slot(path, &bytes, &map, &dll_size);
    CHECK(slot == 0x24308u && dll_size == 0x2e000u && map);
    CHECK(nf_u32(bytes + nf_u32(bytes + 0x3c) + 24 + 28) == 0x400000u);
    free(map); free(bytes);
    printf("PASS reference: DBSIM.EXE's `new` prologue, the registration 0x479cfc with its call at 0x479d02 and the FILE operand 0x4968b6, "
           "the six call sites; CODE is 0x60000020 with the FILE in its file-backed bytes; no hook prologue at the call (%u hooks); "
           "the original CW3220.DLL imports UnhandledExceptionFilter at RVA 0x24308\n",
           (unsigned)(sizeof(registry_rvas) / sizeof(registry_rvas[0])));
    return 0;
}

/* ---- the runtimes' import tables ---- */
/* Reads the PE file `path`, maps it (nf_map_file) with the SizeOfImage from its own header, finds the import slot
 * of kernel32's UnhandledExceptionFilter and returns its RVA (0 when absent). */
static uint32_t file_exit_filter_slot(const char *path, BYTE **file_out, BYTE **map_out, uint32_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 0; }
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    BYTE *bytes = size > 0x400 && size < (16 << 20) ? malloc((size_t)size) : NULL;
    if (!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(bytes); return 0; }
    fclose(f);
    uint32_t pe = nf_u32(bytes + 0x3c), image_size = pe < (uint32_t)size - 100 ? nf_u32(bytes + pe + 24 + 56) : 0;
    BYTE *map = image_size ? nf_map_file(bytes, (size_t)size, image_size) : NULL;
    uint32_t rva = 0;
    if (!map || !nf_find_import(map, image_size, "kernel32.dll", "UnhandledExceptionFilter", &rva)) rva = 0;
    *file_out = bytes; *map_out = map; *size_out = image_size;
    return rva;
}

/* A data file's runfiles path: Windows tests get a manifest (one line "_main/<path> <absolute path>" per
 * file) instead of a tree. */
static const char *runfile(const char *path) {
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
        found = result;
        break;
    }
    fclose(stream);
    return found;
}

/* The own runtime (argv[2]): its import of UnhandledExceptionFilter is found by name. */
static int own_case(const char *path) {
    BYTE *bytes = NULL, *map = NULL;
    uint32_t size = 0, rva = file_exit_filter_slot(runfile(path), &bytes, &map, &size);
    CHECK(rva && rva + 4 <= size && map);
    CHECK(!nf_find_import(map, size, "kernel32.dll", "NoSuchFunctionAnywhere", &rva));
    free(map); free(bytes);
    printf("PASS own: the own cw3220.dll imports kernel32's UnhandledExceptionFilter by name (found by the walk, slot inside the image)\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 100) return (int)(uintptr_t)UnhandledExceptionFilter;   /* never taken: the probe test needs this image to import it by name */
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "seh")) return seh_case();
    if (!strcmp(argv[1], "suite")) return suite_case();
    if (!strcmp(argv[1], "import")) return import_case();
    if (!strcmp(argv[1], "probe")) return probe_case();
    if (!strcmp(argv[1], "own")) return argc > 2 ? own_case(argv[2]) : 2;
    if (!strcmp(argv[1], "parse")) return parse_case();
    if (!strcmp(argv[1], "switch")) return switch_case();
    if (!strcmp(argv[1], "refuse")) return refuse_case();
    if (!strcmp(argv[1], "pages")) return pages_case();
    if (!strcmp(argv[1], "exception")) return exception_case();
    if (!strcmp(argv[1], "reference")) return reference_case();
    return 2;
}
