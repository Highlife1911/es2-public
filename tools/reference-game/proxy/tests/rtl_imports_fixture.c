/* Own-code fixture for proxy/rtl_imports.h (the CW3220 IAT-slot shim).
 * A synthetic DBSIM-shaped image (import descriptor, the 53 jump thunks, the
 * IAT, the 85 simulation call sites) and a synthetic import "DLL" (own stub
 * functions behind a resolver) are built in memory; no original game code
 * runs. Mode `reference` reads DBSIM.EXE and CW3220.DLL from ES2_DATA_ROOT
 * (skipped when unset) and checks the shim's tables against them. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ES2_RTL_NO_PROCESS_ENTRY
#include "../rtl_imports.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

/* ---- synthetic import DLL: one counting stub per export, plus probes ---- */
__attribute__((used)) static volatile LONG fake_hits[RTL_IMPORT_COUNT];
__attribute__((used)) static uint32_t probe_in[10], probe_out[8], probe_bad, probe_arg_after, probe_target, probe_saved_ebp, probe_esp_call;
__attribute__((used)) static uint16_t probe_cw_in, probe_cw_orig, probe_cw_set = 0x0e7f;
__attribute__((used)) static double probe_st0_in, probe_st0_out;
/* Read only by the assembly stubs (globals: no set-but-unused warning). */
uint32_t nested_target, entry_static_init;
__attribute__((used)) static uint32_t entry_in[10], entry_fs4, entry_fs8, entry_last_error, entry_bad;
__attribute__((used)) static uint32_t entry_fs4_before, entry_fs8_before;
__attribute__((used)) static uint16_t entry_cw;
__attribute__((used)) static double entry_st0;
__asm__(
    ".macro FAKE idx\n"
    ".text\n_fake_\\idx:\n"
    "    lock incl _fake_hits+4*\\idx\n"
    "    movl $\\idx, %eax\n"
    "    ret\n"
    ".endm\n"
#define F(i) "FAKE " #i "\n"
    F(0) F(1) F(2) F(3) F(4) F(5) F(6) F(7) F(8) F(9) F(10) F(11) F(12) F(13) F(14) F(15) F(16) F(17)
    F(18) F(19) F(20) F(21) F(22) F(23) F(24) F(25) F(26) F(27) F(28) F(29) F(30) F(31) F(32) F(33) F(34)
    F(35) F(36) F(37) F(38) F(39) F(40) F(41) F(42) F(43) F(44) F(45) F(46) F(47) F(48) F(49) F(50) F(51) F(52)
#undef F
    ".purgem FAKE\n"
    ".section .rdata,\"dr\"\n.p2align 2\n_fake_table:\n"
#define F(i) ".long _fake_" #i "\n"
    F(0) F(1) F(2) F(3) F(4) F(5) F(6) F(7) F(8) F(9) F(10) F(11) F(12) F(13) F(14) F(15) F(16) F(17)
    F(18) F(19) F(20) F(21) F(22) F(23) F(24) F(25) F(26) F(27) F(28) F(29) F(30) F(31) F(32) F(33) F(34)
    F(35) F(36) F(37) F(38) F(39) F(40) F(41) F(42) F(43) F(44) F(45) F(46) F(47) F(48) F(49) F(50) F(51) F(52)
#undef F
    ".text\n"
    /* memset stand-in: records every register, ESP, the return address, the
     * first argument, the x87 control word and ST0, writes nothing to the
     * stack, returns EAX/ECX/EDX like CW3220's memset would (other values). */
    "_probe_memset:\n"
    "    lock incl _fake_hits+4*33\n"
    "    movl %eax, _probe_in\n    movl %ecx, _probe_in+4\n    movl %edx, _probe_in+8\n    movl %ebx, _probe_in+12\n"
    "    movl %esi, _probe_in+16\n    movl %edi, _probe_in+20\n    movl %ebp, _probe_in+24\n    movl %esp, _probe_in+28\n"
    "    movl (%esp), %eax\n    movl %eax, _probe_in+32\n    movl 4(%esp), %eax\n    movl %eax, _probe_in+36\n"
    "    fnstcw _probe_cw_in\n    fstl _probe_st0_in\n"
    "    movl $0x11111111, %eax\n    movl $0x33333333, %ecx\n    movl $0x22222222, %edx\n"
    "    ret\n"
    /* sprintf stand-in: re-enters the shim through the strlen thunk. */
    "_nested_sprintf:\n"
    "    lock incl _fake_hits+4*11\n"
    "    call *_nested_target\n"
    "    movl $0x5f5f5f5f, %eax\n"
    "    ret\n"
    /* __startup stand-in: records the entry state the trampoline passed on,
     * checks that nothing below ESP changed, runs one static-init call. */
    "_fake_startup:\n"
    "    lock incl _fake_hits+4*22\n"
    "    movl %eax, _entry_in\n    movl %ecx, _entry_in+4\n    movl %edx, _entry_in+8\n    movl %ebx, _entry_in+12\n"
    "    movl %esi, _entry_in+16\n    movl %edi, _entry_in+20\n    movl %ebp, _entry_in+24\n    movl %esp, _entry_in+28\n"
    "    movl (%esp), %eax\n    movl %eax, _entry_in+32\n    movl 4(%esp), %eax\n    movl %eax, _entry_in+36\n"
    "    movl %fs:4, %eax\n    movl %eax, _entry_fs4\n    movl %fs:8, %eax\n    movl %eax, _entry_fs8\n"
    "    movl %fs:0x34, %eax\n    movl %eax, _entry_last_error\n"
    "    fnstcw _entry_cw\n    fstl _entry_st0\n"
    "    movl $0, _entry_bad\n    leal -260(%esp), %esi\n    movl $64, %ecx\n"
    "1:  cmpl $0x5a5a5a5a, (%esi)\n    je 2f\n    incl _entry_bad\n"
    "2:  addl $4, %esi\n    decl %ecx\n    jnz 1b\n"
    "    movl _entry_in+16, %esi\n"
    "    call *_entry_static_init\n"
    "    movl $0x77777777, %eax\n"
    "    ret\n"
    /* cdecl void probe_call(uint32_t target): known registers, x87 state and
     * a pattern below ESP, one call, then everything recorded and the pattern
     * checked before any other code can touch the stack. */
    "_probe_call:\n"
    "    pushl %ebp\n    movl %esp, %ebp\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    movl 8(%ebp), %eax\n    movl %eax, _probe_target\n    movl %ebp, _probe_saved_ebp\n"
    "    movl %fs:4, %eax\n    movl %eax, _entry_fs4_before\n    movl %fs:8, %eax\n    movl %eax, _entry_fs8_before\n"
    "    pushl $0xa1b2c3d4\n"
    "    leal -264(%esp), %edi\n    movl $64, %ecx\n    movl $0x5a5a5a5a, %eax\n    cld\n    rep stosl\n"
    "    fnstcw _probe_cw_orig\n    fldcw _probe_cw_set\n    fldpi\n"
    "    movl $0x1234abcd, %eax\n    movl %eax, %fs:0x34\n"
    "    movl $0xa0a0a0a0, %eax\n    movl $0xb0b0b0b0, %ebx\n    movl $0xc0c0c0c0, %ecx\n    movl $0xd0d0d0d0, %edx\n"
    "    movl $0x51515151, %esi\n    movl $0xd1d1d1d1, %edi\n    movl $0xe0e0e0e0, %ebp\n"
    "    movl %esp, _probe_esp_call\n"
    "    call *_probe_target\n"
    "_probe_return:\n"
    "    movl %eax, _probe_out\n    movl %ecx, _probe_out+4\n    movl %edx, _probe_out+8\n    movl %ebx, _probe_out+12\n"
    "    movl %esi, _probe_out+16\n    movl %edi, _probe_out+20\n    movl %ebp, _probe_out+24\n    movl %esp, _probe_out+28\n"
    "    fstpl _probe_st0_out\n    fldcw _probe_cw_orig\n"
    "    movl $0, _probe_bad\n    leal -264(%esp), %esi\n    movl $64, %ecx\n"
    "3:  cmpl $0x5a5a5a5a, (%esi)\n    je 4f\n    incl _probe_bad\n"
    "4:  addl $4, %esi\n    decl %ecx\n    jnz 3b\n"
    "    movl (%esp), %eax\n    movl %eax, _probe_arg_after\n"
    "    movl _probe_saved_ebp, %ebp\n    leal -12(%ebp), %esp\n"
    "    popl %edi\n    popl %esi\n    popl %ebx\n    popl %ebp\n    ret\n");
extern const uint32_t fake_table[RTL_IMPORT_COUNT] __asm__("_fake_table");
extern void probe_memset(void) __asm__("_probe_memset");
extern void nested_sprintf(void) __asm__("_nested_sprintf");
extern void fake_startup(void) __asm__("_fake_startup");
extern void probe_return(void) __asm__("_probe_return");
extern void __cdecl probe_call(uint32_t target) __asm__("_probe_call");

static uint32_t exports[RTL_IMPORT_COUNT];
static const char *resolve_missing;
static FARPROC fixture_resolve(const char *symbol, void *context) {
    (void)context;
    if (resolve_missing && !strcmp(symbol, resolve_missing)) return NULL;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i)
        if (!strcmp(rtl_imports[i].symbol, symbol)) return (FARPROC)(uintptr_t)exports[i];
    return NULL;
}

/* ---- synthetic DBSIM-shaped image ---- */
#define FX_IMPORT_DIR 0x002cc000u
#define FX_LOOKUP 0x002cc0a0u
#define FX_NAMES 0x002cc400u
#define FX_DLL_NAME 0x002cc800u
#define FX_DBSIM_CALLER 0x00001000u
#define FX_DBSIM_HIGH_CALLER 0x00200000u   /* beyond the sim bitmap, inside the image */
static BYTE *image, *other_block, *snapshot;
static void put32(BYTE *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t va(uint32_t rva) { return (uint32_t)(uintptr_t)image + rva; }
static uint32_t thunk_va(unsigned i) { return va(RTL_THUNK_RVA + 6 * i); }
static uint32_t *slot(unsigned i) { return (uint32_t *)(image + RTL_IAT_RVA + 4 * i); }
static void call_stub(BYTE *at, uint32_t target) {
    at[0] = 0xe8; put32(at + 1, target - ((uint32_t)(uintptr_t)at + 5)); at[5] = 0xc3;
}
static int make_image(void) {
    image = VirtualAlloc(NULL, RTL_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    other_block = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    snapshot = malloc(RTL_IMAGE_SIZE);
    CHECK(image && other_block && snapshot);
    image[0] = 'M'; image[1] = 'Z'; put32(image + 0x3c, 0x80);
    BYTE *nt = image + 0x80;
    put32(nt, 0x00004550u); nt[4] = 0x4c; nt[5] = 0x01;
    nt[24] = 0x0b; nt[25] = 0x01;
    put32(nt + 24 + 28, (uint32_t)(uintptr_t)image);
    put32(nt + 24 + 56, RTL_IMAGE_SIZE);
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
        exports[i] = fake_table[i];
        BYTE *t = image + RTL_THUNK_RVA + 6 * i;
        t[0] = 0xff; t[1] = 0x25; put32(t + 2, va(RTL_IAT_RVA + 4 * i));
    }
    exports[RTL_I_memset] = (uint32_t)(uintptr_t)probe_memset;
    exports[RTL_I_sprintf] = (uint32_t)(uintptr_t)nested_sprintf;
    exports[RTL_STARTUP_INDEX] = (uint32_t)(uintptr_t)fake_startup;
    nested_target = thunk_va(RTL_I_strlen);
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) *slot(i) = exports[i];
    for (unsigned i = 0; i < RTL_SIM_SITE_COUNT; ++i)
        call_stub(image + rtl_sim_sites[i].ret_rva - 5, thunk_va(rtl_sim_sites[i].import));
    call_stub(image + FX_DBSIM_CALLER, thunk_va(RTL_I_memcpy));
    call_stub(image + FX_DBSIM_HIGH_CALLER, thunk_va(RTL_I_memcpy));
    call_stub(other_block, thunk_va(RTL_I_memcpy));
    entry_static_init = va(0x5cb2au - 5);   /* the globals constructor's memset */
    return 0;
}
static void take_snapshot(void) { memcpy(snapshot, image, RTL_IMAGE_SIZE); }
static BOOL image_unchanged(void) { return !memcmp(snapshot, image, RTL_IMAGE_SIZE); }
static void options(rtl_options *o) {
    memset(o, 0, sizeof(*o));
    o->image = image; o->expected_base = (uintptr_t)image; o->image_size = RTL_IMAGE_SIZE;
    o->resolve = fixture_resolve;
    volatile BYTE here = 0;
    o->stack_lo = rtl_stack_floor((const void *)&here);
    o->stack_hi = (uintptr_t)((NT_TIB *)NtCurrentTeb())->StackBase;
    rtl_module_range((const void *)options, &o->hook_lo, &o->hook_hi);
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) o->selected[i] = rtl_imports[i].shim;
}
typedef void (__cdecl *stub)(void);
static void call_at(uint32_t address) { ((stub)(uintptr_t)address)(); }
static uint64_t total(const rtl_counter *c) { return c->hook + c->sim + c->dbsim + c->other; }

/* ---- sha256 ---- */
static void sha_hex(const BYTE *data, size_t n, char out[65]) {
    sha256_ctx s; sha256_init(&s);
    /* Odd chunking exercises block boundaries. */
    size_t step = 1;
    while (n) { size_t take = step < n ? step : n; sha256_update(&s, data, take); data += take; n -= take; step = step * 3 + 1; }
    sha256_hex(&s, out);
}
static int sha_case(void) {
    char hex[65];
    sha_hex((const BYTE *)"", 0, hex);
    CHECK(!strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    sha_hex((const BYTE *)"abc", 3, hex);
    CHECK(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha_hex((const BYTE *)two, strlen(two), hex);
    CHECK(!strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    /* Against the CryptoAPI provider on pseudo-random data of many lengths. */
    static BYTE data[70000];
    uint32_t x = 12345;
    for (size_t i = 0; i < sizeof(data); ++i) { x = x * 1103515245u + 12345u; data[i] = (BYTE)(x >> 16); }
    HCRYPTPROV provider = 0;
    CHECK(CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT));
    unsigned checked = 0;
    for (size_t n = 0; n <= sizeof(data); n = n < 300 ? n + 1 : n * 2 + 7) {
        HCRYPTHASH hash = 0; BYTE digest[32]; DWORD length = 32; char want[65];
        CHECK(CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) && CryptHashData(hash, data, (DWORD)n, 0) &&
              CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0));
        CryptDestroyHash(hash);
        for (unsigned i = 0; i < 32; ++i) sprintf(want + 2 * i, "%02x", digest[i]);
        sha_hex(data, n, hex);
        CHECK(!strcmp(hex, want));
        ++checked;
    }
    CryptReleaseContext(provider, 0);
    /* The arming's own entry point (rtl_file_matches: file read in
     * 16 KiB pieces, size and digest compared) over files around the block and
     * read-buffer boundaries; the right digest and size match, a wrong size,
     * a changed digit or a missing file do not. */
    WCHAR dir[MAX_PATH], file[MAX_PATH];
    DWORD dir_length = GetTempPathW(MAX_PATH - 40, dir);
    CHECK(dir_length && dir_length < MAX_PATH - 40);
    static const DWORD sizes[] = {0, 1, 55, 56, 63, 64, 65, 119, 120, 16383, 16384, 16385, 32768, 40000};
    unsigned files = 0;
    for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); ++k) {
        swprintf(file, MAX_PATH, L"%lsq09_sha_%lu_%lu.bin", dir, GetCurrentProcessId(), (unsigned long)sizes[k]);
        HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(h != INVALID_HANDLE_VALUE);
        DWORD wrote = 0;
        CHECK(WriteFile(h, data, sizes[k], &wrote, NULL) && wrote == sizes[k]);
        CloseHandle(h);
        char want_hex[65], changed[65];
        sha_hex(data, sizes[k], want_hex);
        memcpy(changed, want_hex, 65); changed[63] = changed[63] == '0' ? '1' : '0';
        BOOL right = rtl_file_matches(file, sizes[k], want_hex);
        BOOL wrong_size = rtl_file_matches(file, sizes[k] + 1, want_hex);
        BOOL wrong_hex = rtl_file_matches(file, sizes[k], changed);
        DeleteFileW(file);
        CHECK(right && !wrong_size && !wrong_hex);
        ++files;
    }
    CHECK(!rtl_file_matches(L"Z:\\q09_no_such_directory\\x.bin", 0, "00"));
    printf("PASS sha256: 3 FIPS vectors, %u lengths against CryptoAPI, %u files through rtl_file_matches\n", checked, files);
    return 0;
}

/* ---- install, forward, categories, registers, uninstall ---- */
static int install_case(void) {
    CHECK(!make_image());
    take_snapshot();
    /* Baseline without the shim: the probe sees exactly what probe_call set. */
    probe_call(thunk_va(RTL_I_memset));
    uint32_t base_in[10], base_out[8];
    memcpy(base_in, probe_in, sizeof(base_in)); memcpy(base_out, probe_out, sizeof(base_out));
    CHECK(probe_bad == 0 && probe_arg_after == 0xa1b2c3d4u && base_in[8] == (uint32_t)(uintptr_t)probe_return);

    rtl_options o; options(&o);
    CHECK(rtl_install(&o) && rtl_state == RTL_INSTALLED);
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i)
        CHECK(*slot(i) == (rtl_imports[i].shim ? rtl_thunk_table[i] : exports[i]));
    unsigned shimmed = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) shimmed += rtl_imports[i].shim;
    CHECK(shimmed == 47);

    /* Original mode forwards exactly: registers, ESP, return address,
     * argument, x87 control word and ST0, nothing written below ESP. */
    probe_call(thunk_va(RTL_I_memset));
    CHECK(!memcmp(base_in, probe_in, sizeof(base_in)) && !memcmp(base_out, probe_out, sizeof(base_out)));
    CHECK(probe_in[0] == 0xa0a0a0a0u && probe_in[1] == 0xc0c0c0c0u && probe_in[2] == 0xd0d0d0d0u && probe_in[3] == 0xb0b0b0b0u &&
          probe_in[4] == 0x51515151u && probe_in[5] == 0xd1d1d1d1u && probe_in[6] == 0xe0e0e0e0u &&
          probe_in[7] == probe_esp_call - 4 && probe_in[9] == 0xa1b2c3d4u);
    CHECK(probe_out[0] == 0x11111111u && probe_out[1] == 0x33333333u && probe_out[2] == 0x22222222u &&
          probe_out[3] == 0xb0b0b0b0u && probe_out[4] == 0x51515151u && probe_out[5] == 0xd1d1d1d1u &&
          probe_out[6] == 0xe0e0e0e0u && probe_out[7] == probe_esp_call);
    CHECK(probe_bad == 0 && probe_arg_after == 0xa1b2c3d4u && probe_cw_in == probe_cw_set);
    CHECK(probe_st0_in == 3.14159265358979323846 && probe_st0_out == probe_st0_in);
    CHECK(rtl_counters[RTL_I_memset].calls == 1 && rtl_counters[RTL_I_memset].hook == 1);

    /* Categories: sim sites, another DBSIM caller, another module, the proxy. */
    LONG before = fake_hits[RTL_I_memset];
    for (unsigned i = 0; i < RTL_SIM_SITE_COUNT; ++i) call_at(va(rtl_sim_sites[i].ret_rva - 5));
    unsigned want_sim[RTL_IMPORT_COUNT] = {0};
    for (unsigned i = 0; i < RTL_SIM_SITE_COUNT; ++i) ++want_sim[rtl_sim_sites[i].import];
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) CHECK(rtl_counters[i].sim == want_sim[i]);
    CHECK(want_sim[RTL_I_memset] == 54 && want_sim[RTL_I_memcpy] == 12 && fake_hits[RTL_I_memset] == before + 54);
    call_at(va(FX_DBSIM_CALLER)); call_at(va(FX_DBSIM_CALLER)); call_at(va(FX_DBSIM_HIGH_CALLER));
    call_at((uint32_t)(uintptr_t)other_block);
    call_at(thunk_va(RTL_I_memcpy));
    const rtl_counter *m = &rtl_counters[RTL_I_memcpy];
    CHECK(m->sim == 12 && m->dbsim == 3 && m->other == 1 && m->hook == 1 && m->calls == 17 && m->mismatches == 0);
    CHECK(fake_hits[RTL_I_memcpy] == 17);
    /* Reentrancy: the sprintf stand-in calls strlen through its thunk. */
    LONG strlen_hits = fake_hits[RTL_I_strlen];
    uint64_t strlen_hook = rtl_counters[RTL_I_strlen].hook;   /* the sprintf sim site already nested once */
    call_at(thunk_va(RTL_I_sprintf));
    CHECK(rtl_counters[RTL_I_sprintf].hook == 1 && rtl_counters[RTL_I_strlen].hook == strlen_hook + 1 &&
          fake_hits[RTL_I_strlen] == strlen_hits + 1 && strlen_hook == 1);
    /* Every shimmed import forwards to its own export once per call. */
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        if (!rtl_imports[i].shim || i == RTL_I_memset || i == RTL_I_sprintf) continue;
        LONG hits = fake_hits[i]; uint64_t calls = rtl_counters[i].calls;
        CHECK(((uint32_t (__cdecl *)(void))(uintptr_t)thunk_va(i))() == i);
        CHECK(fake_hits[i] == hits + 1 && rtl_counters[i].calls == calls + 1);
    }
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) CHECK(rtl_counters[i].calls == total(&rtl_counters[i]));
    CHECK(rtl_foreign_calls == 0);

    /* Uninstall restores every slot byte-exactly (the whole image). */
    CHECK(rtl_uninstall() && rtl_state == RTL_OFF && image_unchanged());
    uint64_t calls = rtl_counters[RTL_I_memcpy].calls;
    LONG hits = fake_hits[RTL_I_memcpy];
    call_at(va(FX_DBSIM_CALLER));
    CHECK(rtl_counters[RTL_I_memcpy].calls == calls && fake_hits[RTL_I_memcpy] == hits + 1);

    /* A slot changed by someone else after install is left alone. */
    CHECK(rtl_install(&o));
    uint32_t foreign_value = 0x12345678u;
    *slot(RTL_I_strcat) = foreign_value;
    CHECK(!rtl_uninstall() && *slot(RTL_I_strcat) == foreign_value && *slot(RTL_I_memset) == exports[RTL_I_memset]);
    *slot(RTL_I_strcat) = rtl_thunk_table[RTL_I_strcat];
    CHECK(rtl_uninstall() && image_unchanged());
    printf("PASS forward: 47 slots forwarded, registers/ESP/x87/stack below ESP unchanged, "
           "categories hook/sim/dbsim/other, 85 sim sites, reentrant, uninstall byte-exact\n");
    return 0;
}

/* ---- refusals: nothing changes ---- */
typedef void (*mutation)(int undo);
static uint32_t saved_word;
static void bad_thunk(int undo) { BYTE *t = image + RTL_THUNK_RVA + 6 * 5 + 2; if (!undo) { saved_word = rtl_u32(t); put32(t, saved_word + 4); } else put32(t, saved_word); }
static void bad_name(int undo) { BYTE *n = image + rtl_u32(image + FX_LOOKUP + 4 * RTL_I_memset) + 2 + 6; *n = undo ? 't' : 'x'; }
static void bad_site(int undo) { BYTE *s = image + 0x0d317u - 4; if (!undo) { saved_word = rtl_u32(s); put32(s, saved_word + 6); } else put32(s, saved_word); }
static void bad_slot(int undo) { *slot(RTL_I_memcpy) = undo ? exports[RTL_I_memcpy] : exports[RTL_I_memcpy] + 1; }
static void bad_base(int undo) { put32(image + 0x80 + 24 + 28, undo ? (uint32_t)(uintptr_t)image : 0x00400000u); }
static void bad_iat(int undo) { put32(image + FX_IMPORT_DIR + 16, undo ? RTL_IAT_RVA : RTL_IAT_RVA + 4); }
static void bad_count(int undo) { put32(image + FX_LOOKUP + 4 * RTL_IMPORT_COUNT, undo ? 0 : FX_NAMES); }
static void bad_dll(int undo) { image[FX_DLL_NAME] = undo ? 'c' : 'k'; }
static int refuse_case(void) {
    CHECK(!make_image());
    take_snapshot();
    static const struct { mutation change; const char *reason; } cases[] = {
        {bad_thunk, "thunk_rewind"}, {bad_name, "import_name_memset"}, {bad_site, "sim_site_0d312"},
        {bad_slot, "slot_memcpy"}, {bad_base, "image"}, {bad_iat, "iat_rva"}, {bad_count, "import_count"},
        {bad_dll, "import_descriptor"},
    };
    rtl_options o;
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        cases[c].change(0);
        memcpy(snapshot, image, RTL_IMAGE_SIZE);
        options(&o);
        CHECK(!rtl_install(&o));
        if (strcmp(rtl_reason, cases[c].reason)) { fprintf(stderr, "case %u: reason %s, want %s\n", c, rtl_reason, cases[c].reason); return 1; }
        CHECK(image_unchanged() && !rtl_installed_image && rtl_state != RTL_INSTALLED);
        cases[c].change(1);
    }
    memcpy(snapshot, image, RTL_IMAGE_SIZE);
    resolve_missing = "_free"; options(&o);
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "slot_free") && image_unchanged());
    resolve_missing = NULL;
    /* compare/replaced only for the imports with own code;
     * replaced and shadow for the file imports (no compare), only with a
     * CW3220 image; the console stubs have no shadow. */
    options(&o); o.modes[RTL_I_fopen] = ES2_HOOK_COMPARE;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "mode_reserved_fopen") && image_unchanged());
    options(&o); o.modes[RTL_I_fclose] = ES2_HOOK_REPLACED;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "cw_image") && image_unchanged() && !rtl_io_active);
    options(&o); o.shadow[RTL_I_fopen] = 1;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "cw_image") && image_unchanged() && !rtl_io_active);
    options(&o); o.shadow[21] = 1;   /* _printf */
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "mode_reserved_printf") && image_unchanged());
    options(&o); o.shadow[RTL_I_fopen] = 1; o.modes[RTL_I_fopen] = ES2_HOOK_REPLACED;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "mode_reserved_fopen") && image_unchanged());
    options(&o); o.selected[1] = TRUE;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "not_shimmable_atexit") && image_unchanged());
    options(&o); o.stack_hi = o.stack_lo;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "options") && image_unchanged());
    /* Shadow only for calloc/realloc/free, all three, and only with
     * a CW3220 image (the in-vitro harness tests the real one). */
    options(&o); o.shadow[RTL_I_memset] = 1;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "mode_reserved_memset") && image_unchanged());
    options(&o); o.shadow[RTL_I_calloc] = o.shadow[RTL_I_free] = 1;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "shadow_set") && image_unchanged());
    options(&o); o.shadow[RTL_I_calloc] = o.shadow[RTL_I_free] = o.shadow[RTL_I_realloc] = 1;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "cw_image") && image_unchanged() && !rtl_heap_active);
    /* The heap trace only with the shadow (the in-vitro harness writes one). */
    options(&o); wcscpy(o.heap_trace, L"trace.ndjson");
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "heap_trace_needs_shadow") && image_unchanged() && !rtl_heap_trace_on);
    /* Replaced for calloc/realloc/free, all three selected; no compare. */
    options(&o); o.modes[RTL_I_calloc] = o.modes[RTL_I_free] = ES2_HOOK_REPLACED;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "heap_set") && image_unchanged() && !rtl_heap_own_active);
    options(&o); o.modes[RTL_I_calloc] = o.modes[RTL_I_free] = o.modes[RTL_I_realloc] = ES2_HOOK_REPLACED; o.selected[RTL_I_realloc] = FALSE;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "heap_set") && image_unchanged() && !rtl_heap_own_active);
    options(&o); o.modes[RTL_I_realloc] = ES2_HOOK_COMPARE;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "mode_reserved_realloc") && image_unchanged());
    options(&o); o.modes[RTL_I_calloc] = o.modes[RTL_I_free] = ES2_HOOK_REPLACED; o.shadow[RTL_I_realloc] = 1;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "shadow_set") && image_unchanged());
    /* The lifecycle group, all five replaced and selected, only with a
     * CW3220 image (the lifecycle fixture and the in-vitro harness test the
     * rest of its refusals); startup/atexit/exit not outside it. */
    static const unsigned life[5] = {RTL_STARTUP_INDEX, RTL_I_atexit, RTL_I_exit, RTL_I_abort, RTL_I_ErrorExit};
    options(&o); for (unsigned k = 0; k < 5; ++k) { o.selected[life[k]] = TRUE; o.modes[life[k]] = ES2_HOOK_REPLACED; }
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "cw_image") && image_unchanged() && !rtl_life_active);
    options(&o); for (unsigned k = 0; k < 4; ++k) { o.selected[life[k]] = TRUE; o.modes[life[k]] = ES2_HOOK_REPLACED; }
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "lifecycle_set") && image_unchanged() && !rtl_life_active);
    options(&o); o.selected[RTL_STARTUP_INDEX] = TRUE;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "not_shimmable_startup") && image_unchanged());
    options(&o); o.modes[RTL_I_abort] = ES2_HOOK_COMPARE;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "mode_reserved_abort") && image_unchanged());
    options(&o); o.selected[RTL_I_exit] = TRUE; o.modes[RTL_I_exit] = ES2_HOOK_REPLACED; o.shadow[RTL_I_exit] = 1;
    CHECK(!rtl_install(&o) && !strcmp(rtl_reason, "not_shimmable_exit") && image_unchanged());
    /* The image is still good: the next install succeeds. */
    options(&o);
    CHECK(rtl_install(&o) && rtl_uninstall() && image_unchanged());
    printf("PASS refuse: 28 refusals with their reasons, image byte-identical after each\n");
    return 0;
}

/* ---- rollback and calls during installation ---- */
static unsigned protect_calls, protect_fail_at;
static BOOL during_done;
static uint64_t during_memset, during_itoa;
static LONG during_itoa_hits;
static BOOL WINAPI fixture_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD old) {
    ++protect_calls;
    if (protect_fail_at && protect_calls == protect_fail_at) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    BOOL ok = VirtualProtect(address, size, protection, old);
    /* Restoring the memset slot: memset is installed, itoa (next) is not. */
    if (ok && address == (LPVOID)slot(RTL_I_memset) && protection != PAGE_READWRITE && !during_done && rtl_state != RTL_OFF) {
        during_done = TRUE;
        LONG hits = fake_hits[RTL_I_itoa];
        call_at(va(0x0225du - 5));                 /* object_init's memset: sim */
        call_at(va(0x27936u - 5));                 /* terrain load's itoa: not yet shimmed */
        during_memset = rtl_counters[RTL_I_memset].sim; during_itoa = rtl_counters[RTL_I_itoa].calls;
        during_itoa_hits = fake_hits[RTL_I_itoa] - hits;
    }
    return ok;
}
static int rollback_case(void) {
    CHECK(!make_image());
    take_snapshot();
    rtl_protect = fixture_protect;
    rtl_options o; options(&o);
    /* Fail the first protection change of the 20th selected slot. */
    unsigned twentieth = 0, seen = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) if (rtl_imports[i].shim && ++seen == 20) twentieth = i;
    protect_calls = 0; protect_fail_at = 2 * 19 + 1;
    CHECK(!rtl_install(&o));
    char want[64]; snprintf(want, sizeof(want), "patch_%s", rtl_imports[twentieth].key);
    CHECK(!strcmp(rtl_reason, want) && image_unchanged() && !rtl_installed_image);
    /* Fail the restoring protection call of a slot: that slot is rolled back too. */
    protect_calls = 0; protect_fail_at = 2 * 7 + 2;
    CHECK(!rtl_install(&o) && image_unchanged() && !rtl_installed_image);
    /* That failed restore left the IAT page read-write; put it back. */
    DWORD old; CHECK(VirtualProtect(slot(0), 4, PAGE_EXECUTE_READWRITE, &old) && old == PAGE_READWRITE);
    /* Calls during installation reach either the old export or the thunk. */
    protect_calls = 0; protect_fail_at = 0; rtl_state = RTL_PENDING;
    CHECK(rtl_install(&o));
    if (!(during_done && during_memset == 1 && during_itoa == 0 && during_itoa_hits == 1))
        fprintf(stderr, "during: done %d memset %llu itoa %llu hits %ld\n", during_done, (unsigned long long)during_memset,
                (unsigned long long)during_itoa, (long)during_itoa_hits);
    CHECK(during_done && during_memset == 1 && during_itoa == 0 && during_itoa_hits == 1);
    CHECK(rtl_uninstall() && image_unchanged());
    printf("PASS rollback: failed patch (%s) and failed restore leave the image byte-identical; "
           "calls during install are forwarded\n", want);
    return 0;
}

/* ---- calls from another thread ---- */
static DWORD WINAPI foreign_thread(LPVOID parameter) {
    (void)parameter;
    for (unsigned i = 0; i < 1000; ++i) { call_at(thunk_va(RTL_I_memcpy)); call_at(va(0x0225du - 5)); }
    return 0;
}
static int foreign_case(void) {
    CHECK(!make_image());
    rtl_options o; options(&o);
    CHECK(rtl_install(&o));
    HANDLE threads[4];
    for (unsigned i = 0; i < 4; ++i) CHECK((threads[i] = CreateThread(NULL, 0, foreign_thread, NULL, 0, NULL)) != NULL);
    for (unsigned i = 0; i < 1000; ++i) call_at(thunk_va(RTL_I_memcpy));
    CHECK(WaitForMultipleObjects(4, threads, TRUE, 30000) == WAIT_OBJECT_0);
    for (unsigned i = 0; i < 4; ++i) CloseHandle(threads[i]);
    CHECK(rtl_foreign_calls == 8000 && rtl_counters[RTL_I_memcpy].calls == 1000 && rtl_counters[RTL_I_memset].calls == 0);
    CHECK(fake_hits[RTL_I_memcpy] == 5000 && fake_hits[RTL_I_memset] == 4000);
    printf("PASS foreign: 8000 calls from four other threads forwarded, counted only as foreign\n");
    return 0;
}

/* ---- configuration ---- */
static WCHAR ini[MAX_PATH];
static int put(const WCHAR *key, const WCHAR *value) { return WritePrivateProfileStringW(L"rtl", key, value, ini) ? 0 : 1; }
static int config_case(void) {
    WCHAR dir[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"TEST_TMPDIR", dir, MAX_PATH);
    if (!n || n >= MAX_PATH - 40) n = GetTempPathW(MAX_PATH, dir);
    CHECK(n && n < MAX_PATH - 40);
    swprintf(ini, MAX_PATH, L"%ls\\rtl-%lu.ini", dir, (unsigned long)GetCurrentProcessId());
    DeleteFileW(ini);
    rtl_options o;
    CHECK(rtl_configure(ini, &o) == 0);                     /* no file, no section */
    CHECK(!put(L"enabled", L"0") && rtl_configure(ini, &o) == 0);
    CHECK(!put(L"enabled", L"yes") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "config_enabled"));
    CHECK(!put(L"enabled", L"1") && rtl_configure(ini, &o) == 1);
    unsigned count = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) { count += o.selected[i] != 0; CHECK(o.modes[i] == ES2_HOOK_ORIGINAL && o.selected[i] == rtl_imports[i].shim); }
    CHECK(count == 47);
    CHECK(!put(L"imports", L" memset , MEMCPY,strcat ") && rtl_configure(ini, &o) == 1);
    CHECK(o.selected[RTL_I_memset] && o.selected[RTL_I_memcpy] && o.selected[RTL_I_strcat] && !o.selected[RTL_I_memmove]);
    CHECK(!put(L"imports", L"memset,memset") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "config_duplicate_memset"));
    CHECK(!put(L"imports", L"memset,") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "config_unknown_import"));
    CHECK(!put(L"imports", L"_memset") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "config_unknown_import"));
    CHECK(!put(L"imports", L"memset,atexit") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "not_shimmable_atexit"));
    CHECK(!put(L"imports", L"all") && rtl_configure(ini, &o) == 1);
    CHECK(!put(L"memset", L"Original") && !put(L"exit", L"original") && rtl_configure(ini, &o) == 1);
    CHECK(!put(L"memset", L"compare") && rtl_configure(ini, &o) == 1 && o.modes[RTL_I_memset] == ES2_HOOK_COMPARE);
    CHECK(!put(L"memset", L"REPLACED") && rtl_configure(ini, &o) == 1 && o.modes[RTL_I_memset] == ES2_HOOK_REPLACED);
    CHECK(!put(L"memset", NULL));
    /* Exactly the 20 imports with own code accept compare/replaced. */
    unsigned own = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        if (!rtl_imports[i].shim) continue;
        WCHAR key[24];
        for (size_t c = 0; c <= strlen(rtl_imports[i].key); ++c) key[c] = (WCHAR)(BYTE)rtl_imports[i].key[c];
        CHECK(!put(key, L"Compare"));
        int r = rtl_configure(ini, &o);
        char want[64]; snprintf(want, sizeof(want), "mode_reserved_%s", rtl_imports[i].key);
        if (rtl_own_function(i) >= 0) { ++own; CHECK(r == 1 && o.modes[i] == ES2_HOOK_COMPARE); }
        else CHECK(r == -1 && !strcmp(rtl_reason, want));
        CHECK(!put(key, NULL));
    }
    CHECK(own == 20);
    /* Replaced for the 21 file and console imports, shadow for the 18
     * file imports (not printf, fputc, fputs), independent of the heap set. */
    unsigned io_replaced = 0, io_shadow = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        if (!rtl_imports[i].shim) continue;
        WCHAR key[24];
        for (size_t c = 0; c <= strlen(rtl_imports[i].key); ++c) key[c] = (WCHAR)(BYTE)rtl_imports[i].key[c];
        int io = rtl_io_function_of(i);
        CHECK(!put(key, L"Replaced"));
        int r = rtl_configure(ini, &o);
        if (io >= 0) { ++io_replaced; CHECK(r == 1 && o.modes[i] == ES2_HOOK_REPLACED); }
        CHECK(!put(key, L"shadow"));
        r = rtl_configure(ini, &o);
        char want[64]; snprintf(want, sizeof(want), "mode_reserved_%s", rtl_imports[i].key);
        if (io >= 0 && !es2_io_is_stub((es2_io_function)io)) { ++io_shadow; CHECK(r == 1 && o.shadow[i] && o.modes[i] == ES2_HOOK_ORIGINAL); }
        else if (rtl_heap_function_of(i) >= 0) CHECK(r == -1 && !strcmp(rtl_reason, "shadow_set"));
        else CHECK(r == -1 && !strcmp(rtl_reason, want));
        CHECK(!put(key, NULL));
    }
    CHECK(io_replaced == 21 && io_shadow == 18);
    CHECK(!put(L"memset", L"replace") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "mode_malformed_memset"));
    CHECK(!put(L"memset", NULL) && !put(L"CatchCleanup", L"compare") && rtl_configure(ini, &o) == -1 &&
          !strcmp(rtl_reason, "mode_reserved_CatchCleanup"));
    /* Shadow for calloc, realloc and free, all three selected. */
    CHECK(!put(L"CatchCleanup", NULL) && !put(L"memset", L"shadow") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "mode_reserved_memset"));
    CHECK(!put(L"memset", NULL) && !put(L"calloc", L"Shadow") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "shadow_set"));
    CHECK(!put(L"realloc", L"shadow") && !put(L"free", L"SHADOW") && rtl_configure(ini, &o) == 1);
    CHECK(o.shadow[RTL_I_calloc] && o.shadow[RTL_I_realloc] && o.shadow[RTL_I_free] && o.modes[RTL_I_calloc] == ES2_HOOK_ORIGINAL);
    CHECK(!put(L"imports", L"calloc,free") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "shadow_set"));
    CHECK(!put(L"imports", L"free,calloc,realloc,memset") && rtl_configure(ini, &o) == 1 && o.selected[RTL_I_memset]);
    CHECK(!put(L"malloc", L"shadow") && rtl_configure(ini, &o) == 1);   /* not an import of DBSIM: the key is not read */
    /* heap_trace=<path> only with the heap shadow. */
    CHECK(!o.heap_trace[0] && !put(L"heap_trace", L"trace.ndjson") && rtl_configure(ini, &o) == 1 &&
          !wcscmp(o.heap_trace, L"trace.ndjson"));
    CHECK(!put(L"imports", L"all") && !put(L"free", L"compare") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "mode_reserved_free"));
    CHECK(!put(L"calloc", NULL) && !put(L"realloc", NULL) && !put(L"free", NULL) && !put(L"malloc", NULL));
    CHECK(rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "heap_trace_needs_shadow"));
    CHECK(!put(L"heap_trace", NULL) && rtl_configure(ini, &o) == 1 && !o.shadow[RTL_I_calloc] && !o.heap_trace[0]);
    /* Replaced for calloc, realloc and free, all three selected; not mixed with shadow. */
    CHECK(!put(L"calloc", L"replaced") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "heap_set"));
    CHECK(!put(L"realloc", L"Replaced") && !put(L"free", L"REPLACED") && rtl_configure(ini, &o) == 1);
    CHECK(o.modes[RTL_I_calloc] == ES2_HOOK_REPLACED && o.modes[RTL_I_realloc] == ES2_HOOK_REPLACED &&
          o.modes[RTL_I_free] == ES2_HOOK_REPLACED && !o.shadow[RTL_I_free]);
    CHECK(!put(L"imports", L"calloc,realloc") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "heap_set"));
    CHECK(!put(L"imports", L"all") && !put(L"free", L"shadow") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "shadow_set"));
    CHECK(!put(L"calloc", NULL) && !put(L"realloc", NULL) && !put(L"free", NULL));
    CHECK(rtl_configure(ini, &o) == 1 && o.modes[RTL_I_calloc] == ES2_HOOK_ORIGINAL);
    /* The lifecycle group, all five replaced and selected; `all`
     * selects startup, atexit and exit then; no compare or shadow; those
     * three exist only in the group. */
    static const WCHAR *const life[5] = {L"startup", L"atexit", L"exit", L"abort", L"ErrorExit"};
    CHECK(!put(L"startup", L"replaced") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "lifecycle_set"));
    for (unsigned k = 1; k < 4; ++k) CHECK(!put(life[k], L"Replaced"));
    CHECK(rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "lifecycle_set"));
    CHECK(!put(L"ErrorExit", L"REPLACED") && rtl_configure(ini, &o) == 1);
    unsigned life_count = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        BOOL member = rtl_life_index(i) >= 0;
        life_count += member;
        CHECK(o.selected[i] == (member || rtl_imports[i].shim) && (o.modes[i] == ES2_HOOK_REPLACED) == member);
    }
    CHECK(life_count == 5);
    CHECK(!put(L"imports", L"startup,atexit,exit,abort,ErrorExit,memset") && rtl_configure(ini, &o) == 1 && o.selected[RTL_STARTUP_INDEX] &&
          o.selected[RTL_I_memset] && !o.selected[RTL_I_memcpy]);
    CHECK(!put(L"imports", L"startup,atexit,exit,abort") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "lifecycle_set"));
    CHECK(!put(L"imports", L"all") && !put(L"atexit", L"compare") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "mode_reserved_atexit"));
    CHECK(!put(L"atexit", L"replaced") && !put(L"exit", L"shadow") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "mode_reserved_exit"));
    for (unsigned k = 0; k < 5; ++k) CHECK(!put(life[k], NULL));
    CHECK(!put(L"imports", L"memset,startup") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "not_shimmable_startup"));
    CHECK(!put(L"imports", L"exit") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "not_shimmable_exit"));
    CHECK(!put(L"imports", L"errno") && rtl_configure(ini, &o) == -1 && !strcmp(rtl_reason, "not_shimmable_errno"));
    CHECK(!put(L"imports", L"all") && rtl_configure(ini, &o) == 1 && !o.selected[RTL_STARTUP_INDEX] && !o.selected[RTL_I_atexit] &&
          !o.selected[RTL_I_exit] && o.selected[RTL_I_abort] && o.modes[RTL_I_abort] == ES2_HOOK_ORIGINAL);
    DeleteFileW(ini);
    printf("PASS config: absent/0 disabled, 1 = all 47 original, lists, duplicates, unknown and "
           "non-shimmable names, compare/replaced for the 20 own imports only, shadow or replaced for calloc/realloc/free together, "
           "replaced for the 21 I/O imports and shadow for the 18 file imports, the lifecycle group of five replaced together, "
           "malformed modes\n");
    return 0;
}

/* ---- arming at the entry point ---- */
static BOOL entry_install, entry_ran;
static BYTE *entry_inside_stack;
static uintptr_t entry_inside_base, entry_inside_limit;
static void rtl_arm(void) {
    volatile BYTE here = 0;
    entry_ran = TRUE;
    entry_inside_stack = (BYTE *)&here;
    entry_inside_base = (uintptr_t)((NT_TIB *)NtCurrentTeb())->StackBase;
    entry_inside_limit = (uintptr_t)((NT_TIB *)NtCurrentTeb())->StackLimit;
    SetLastError(0xdead);                          /* restored by rtl_arm_at_entry */
    if (!entry_install) { InterlockedExchange(&rtl_state, RTL_OFF); return; }
    rtl_options o; options(&o);
    o.stack_lo = rtl_stack_floor((const void *)(uintptr_t)rtl_entry_esp);
    o.stack_hi = rtl_entry_stack_base;
    InterlockedExchange(&rtl_state, rtl_install(&o) ? RTL_INSTALLED : RTL_REFUSED);
}
static int entry_round(BOOL install) {
    entry_install = install; entry_ran = FALSE;
    uint32_t original = *slot(RTL_STARTUP_INDEX);
    CHECK(rtl_install_startup_image(image, (uintptr_t)image, RTL_IMAGE_SIZE, fixture_resolve, NULL));
    CHECK(rtl_state == RTL_PENDING && *slot(RTL_STARTUP_INDEX) == (uint32_t)(uintptr_t)rtl_startup_entry);
    LONG memset_hits = fake_hits[RTL_I_memset];
    probe_call(thunk_va(RTL_STARTUP_INDEX));
    CHECK(entry_ran && fake_hits[RTL_STARTUP_INDEX] >= 1 && fake_hits[RTL_I_memset] == memset_hits + 1);
    /* The private stack and its TEB bounds were in force while arming. */
    CHECK((uintptr_t)entry_inside_stack > rtl_private_base && (uintptr_t)entry_inside_stack < rtl_private_top &&
          rtl_private_top - rtl_private_base == RTL_PRIVATE_STACK);
    CHECK(entry_inside_base == rtl_private_top && entry_inside_limit == rtl_private_base);
    /* __startup saw exactly the entry state. */
    CHECK(entry_in[0] == 0xa0a0a0a0u && entry_in[1] == 0xc0c0c0c0u && entry_in[2] == 0xd0d0d0d0u && entry_in[3] == 0xb0b0b0b0u &&
          entry_in[4] == 0x51515151u && entry_in[5] == 0xd1d1d1d1u && entry_in[6] == 0xe0e0e0e0u &&
          entry_in[7] == probe_esp_call - 4 && entry_in[8] == (uint32_t)(uintptr_t)probe_return && entry_in[9] == 0xa1b2c3d4u);
    CHECK(entry_fs4 == entry_fs4_before && entry_fs8 == entry_fs8_before && entry_last_error == 0x1234abcdu);
    CHECK(entry_cw == probe_cw_set && entry_st0 == 3.14159265358979323846 && entry_bad == 0);
    CHECK(probe_out[0] == 0x77777777u && probe_out[3] == 0xb0b0b0b0u && probe_out[4] == 0x51515151u &&
          probe_out[5] == 0xd1d1d1d1u && probe_out[6] == 0xe0e0e0e0u && probe_out[7] == probe_esp_call);
    CHECK(*slot(RTL_STARTUP_INDEX) == original && !rtl_startup_slot);
    return 0;
}
static int entry_case(void) {
    CHECK(!make_image());
    take_snapshot();
    /* Disabled at entry: the slot is restored, nothing else changes. */
    CHECK(!entry_round(FALSE));
    CHECK(rtl_state == RTL_OFF && image_unchanged() && rtl_counters[RTL_I_memset].calls == 0);
    /* Enabled: installed before the first static initializer runs. */
    CHECK(!entry_round(TRUE));
    CHECK(rtl_state == RTL_INSTALLED && rtl_counters[RTL_I_memset].sim == 1 && rtl_counters[RTL_I_memset].calls == 1);
    CHECK(*slot(RTL_I_memset) == rtl_thunk_table[RTL_I_memset]);
    CHECK(rtl_uninstall() && image_unchanged());
    /* DllMain refusals: wrong __startup slot or thunk, nothing patched. */
    *slot(RTL_STARTUP_INDEX) += 1; take_snapshot();
    CHECK(!rtl_install_startup_image(image, (uintptr_t)image, RTL_IMAGE_SIZE, fixture_resolve, NULL) &&
          !strcmp(rtl_reason, "slot_startup") && image_unchanged());
    *slot(RTL_STARTUP_INDEX) -= 1;
    image[RTL_THUNK_RVA + 6 * RTL_STARTUP_INDEX + 1] = 0x15; take_snapshot();
    CHECK(!rtl_install_startup_image(image, (uintptr_t)image, RTL_IMAGE_SIZE, fixture_resolve, NULL) &&
          !strcmp(rtl_reason, "thunk_startup") && image_unchanged());
    printf("PASS entry: armed through the __startup slot on a private stack; registers, ESP, x87, "
           "LastError, TEB bounds and the stack below ESP unchanged; static-init call counted\n");
    return 0;
}

/* ---- the reference files (ES2_DATA_ROOT) ---- */
static BYTE *load_mapped(const char *path, size_t *mapped_size, BYTE **file_bytes, size_t *file_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    BYTE *bytes = malloc((size_t)size);
    if (!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size) { fclose(f); return NULL; }
    fclose(f);
    uint32_t pe = rtl_u32(bytes + 0x3c);
    uint32_t image_size = rtl_u32(bytes + pe + 24 + 56), headers = rtl_u32(bytes + pe + 24 + 60);
    unsigned sections = bytes[pe + 6] | bytes[pe + 7] << 8, optional = bytes[pe + 20] | bytes[pe + 21] << 8;
    BYTE *map = calloc(1, image_size);
    if (!map) return NULL;
    memcpy(map, bytes, headers);
    for (unsigned s = 0; s < sections; ++s) {
        const BYTE *h = bytes + pe + 24 + optional + 40 * s;
        uint32_t rva = rtl_u32(h + 12), raw = rtl_u32(h + 16), offset = rtl_u32(h + 20);
        if (raw && rva < image_size) memcpy(map + rva, bytes + offset, raw < image_size - rva ? raw : image_size - rva);
    }
    *mapped_size = image_size;
    if (file_bytes) { *file_bytes = bytes; *file_size = (size_t)size; } else free(bytes);
    return map;
}
static int reference_case(void) {
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) { puts("SKIP: ES2_DATA_ROOT unset"); return 0; }
    char path[1024];
    snprintf(path, sizeof(path), "%s/VER95/DBSIM.EXE", root);
    size_t size = 0, file_size = 0; BYTE *bytes = NULL;
    BYTE *map = load_mapped(path, &size, &bytes, &file_size);
    CHECK(map && file_size == RTL_FILE_SIZE);
    char hex[65]; sha_hex(bytes, file_size, hex);
    CHECK(!strcmp(hex, "8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad"));
    WCHAR wide[1024]; CHECK(MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 1024) > 0);
    CHECK(rtl_reference_file(wide));
    /* Import names, thunks and all 85 simulation call sites of the tables. */
    if (!rtl_verify_layout(map, size, 0x00400000u)) { fprintf(stderr, "layout refused: %s\n", rtl_reason); return 1; }
    /* The entry code ends with `push 0; jmp <__startup thunk>`. */
    const BYTE *entry = map + 0x1067;
    CHECK(entry[0] == 0x6a && entry[1] == 0 && entry[2] == 0xe9 &&
          0x106e + rtl_u32(entry + 3) == RTL_THUNK_RVA + 6 * RTL_STARTUP_INDEX);
    /* Every one of the 53 symbols is a named export of the reference CW3220. */
    snprintf(path, sizeof(path), "%s/CW3220.DLL", root);
    size_t dll_size = 0;
    BYTE *dll = load_mapped(path, &dll_size, NULL, NULL);
    CHECK(dll);
    uint32_t pe = rtl_u32(dll + 0x3c), exports_rva = rtl_u32(dll + pe + 120);
    CHECK(exports_rva && exports_rva < dll_size);
    uint32_t count = rtl_u32(dll + exports_rva + 24), names = rtl_u32(dll + exports_rva + 32);
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        BOOL found = FALSE;
        for (uint32_t j = 0; j < count && !found; ++j) found = !strcmp((const char *)dll + rtl_u32(dll + names + 4 * j), rtl_imports[i].symbol);
        if (!found) { fprintf(stderr, "not exported: %s\n", rtl_imports[i].symbol); return 1; }
    }
    printf("PASS reference: DBSIM.EXE hash (own SHA-256), 53 imports in IAT order, 53 thunks, "
           "85 simulation sites, entry jump to __startup, 53 CW3220 exports\n");
    return 0;
}

/* ---- Modes replaced and compare through the adapters ---- */
/* fx_call: calls fx_target with the eight argument slots fx_args on a
 * 16-byte aligned stack, known registers and a 0x5a pattern in the 256 bytes
 * below the arguments; records the registers after the call, the 256 bytes
 * below the arguments (fx_below[63] = the return-address slot) and the
 * argument slots after the call (back into fx_args). */
__attribute__((used)) static uint32_t fx_args[8], fx_out[8], fx_below[64], fx_esp_call, fx_saved_ebp;
uint32_t fx_target;   /* read only by fx_call (a global: no set-but-unused warning) */
__asm__(
    ".text\n"
    "_fx_call:\n"
    "    pushl %ebp\n    movl %esp, %ebp\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    movl %ebp, _fx_saved_ebp\n"
    "    subl $64, %esp\n    andl $-16, %esp\n"
    "    leal _fx_args, %esi\n    movl %esp, %edi\n    movl $8, %ecx\n    cld\n    rep movsl\n"
    "    leal -256(%esp), %edi\n    movl $64, %ecx\n    movl $0x5a5a5a5a, %eax\n    rep stosl\n"
    "    movl $0xa0a0a0a0, %eax\n    movl $0xb0b0b0b0, %ebx\n    movl $0xc0c0c0c0, %ecx\n    movl $0xd0d0d0d0, %edx\n"
    "    movl $0x51515151, %esi\n    movl $0xd1d1d1d1, %edi\n    movl $0xe0e0e0e0, %ebp\n"
    "    movl %esp, _fx_esp_call\n"
    "    call *_fx_target\n"
    "_fx_return:\n"
    "    movl %eax, _fx_out\n    movl %ecx, _fx_out+4\n    movl %edx, _fx_out+8\n    movl %ebx, _fx_out+12\n"
    "    movl %esi, _fx_out+16\n    movl %edi, _fx_out+20\n    movl %ebp, _fx_out+24\n    movl %esp, _fx_out+28\n"
    "    movl -4(%esp), %ecx\n    pushfl\n    popl %eax\n    movl %ecx, -4(%esp)\n    movl %eax, _fx_flags\n"
    "    leal -256(%esp), %esi\n    leal _fx_below, %edi\n    movl $64, %ecx\n    cld\n    rep movsl\n"
    "    movl %esp, %esi\n    leal _fx_args, %edi\n    movl $8, %ecx\n    rep movsl\n"
    "    movl _fx_saved_ebp, %ebp\n    leal -12(%ebp), %esp\n    popl %edi\n    popl %esi\n    popl %ebx\n    popl %ebp\n    ret\n"
    /* Stand-in exports for compare mode: own code with the CW3220 contracts
     * of memset and strlen (rtl_imports.md 4.4), each counting its calls,
     * plus three deliberately wrong variants. */
    "_cw_memset:\n"
    "    lock incl _fake_hits+4*33\n"
    "_cw_memset_body:\n"
    "    pushl %ebp\n    movl %esp, %ebp\n    pushl %edi\n    movl 8(%ebp), %edi\n"
    "    movzbl 12(%ebp), %eax\n    imull $0x01010101, %eax, %eax\n"
    "    movl 16(%ebp), %ecx\n    movl %ecx, %edx\n    shrl $2, %ecx\n    cld\n    rep stosl\n"
    "    movl %edx, %ecx\n    andl $3, %ecx\n    rep stosb\n"
    "    movl 8(%ebp), %eax\n    popl %edi\n    popl %ebp\n    ret\n"
    "_cw_memset_last:\n"                     /* the last byte wrong */
    "    lock incl _fake_hits+4*33\n"
    "    pushl %ebp\n    movl %esp, %ebp\n    pushl %edi\n    movl 8(%ebp), %edi\n"
    "    movzbl 12(%ebp), %eax\n    imull $0x01010101, %eax, %eax\n"
    "    movl 16(%ebp), %ecx\n    movl %ecx, %edx\n    shrl $2, %ecx\n    cld\n    rep stosl\n"
    "    movl %edx, %ecx\n    andl $3, %ecx\n    rep stosb\n"
    "    movb $0x77, -1(%edi)\n"
    "    movl 8(%ebp), %eax\n    popl %edi\n    popl %ebp\n    ret\n"
    "_cw_memset_over:\n"                     /* one byte too many */
    "    lock incl _fake_hits+4*33\n"
    "    pushl %ebp\n    movl %esp, %ebp\n    pushl %edi\n    movl 8(%ebp), %edi\n"
    "    movzbl 12(%ebp), %eax\n    imull $0x01010101, %eax, %eax\n"
    "    movl 16(%ebp), %ecx\n    movl %ecx, %edx\n    shrl $2, %ecx\n    cld\n    rep stosl\n"
    "    movl %edx, %ecx\n    andl $3, %ecx\n    rep stosb\n"
    "    movb $0x66, (%edi)\n"
    "    movl 8(%ebp), %eax\n    popl %edi\n    popl %ebp\n    ret\n"
    "_cw_strlen:\n"
    "    lock incl _fake_hits+4*3\n"
    "    pushl %ebp\n    movl %esp, %ebp\n    pushl %edi\n    movl 8(%ebp), %edi\n"
    "    movl $-1, %ecx\n    xorb %al, %al\n    cld\n    repne scasb\n    notl %ecx\n    leal -1(%ecx), %eax\n"
    "    popl %edi\n    popl %ebp\n    ret\n"
    "_cw_strlen_edx:\n"                      /* EDX cleared (CW3220 leaves it) */
    "    lock incl _fake_hits+4*3\n"
    "    pushl %ebp\n    movl %esp, %ebp\n    pushl %edi\n    movl 8(%ebp), %edi\n"
    "    movl $-1, %ecx\n    xorb %al, %al\n    cld\n    repne scasb\n    notl %ecx\n    leal -1(%ecx), %eax\n"
    "    xorl %edx, %edx\n"
    "    popl %edi\n    popl %ebp\n    ret\n");
__attribute__((used)) static uint32_t fx_flags;
extern void __cdecl fx_call(void) __asm__("_fx_call");
extern void fx_return(void) __asm__("_fx_return");
extern void cw_memset(void) __asm__("_cw_memset");
extern void cw_memset_last(void) __asm__("_cw_memset_last");
extern void cw_memset_over(void) __asm__("_cw_memset_over");
extern void cw_strlen(void) __asm__("_cw_strlen");
extern void cw_strlen_edx(void) __asm__("_cw_strlen_edx");

static BYTE scratch[512], scratch_before[512], scratch_after[512];
static void scratch_fill(void) {
    memset(scratch, 0x33, sizeof(scratch));
    static const struct { unsigned at; const char *text; } texts[] = {
        {20, "ABCDEFGHIJKL"}, {40, "Hello, world"}, {60, "Help"}, {200, "lo"}, {220, "HELLO, WORLD"},
        {240, "mIxEd\x80"}, {260, "MiXeD\xc4"}, {300, "  -42z"}, {400, "%d-%s|%5x"},
    };
    for (unsigned i = 0; i < sizeof(texts) / sizeof(texts[0]); ++i) memcpy(scratch + texts[i].at, texts[i].text, strlen(texts[i].text) + 1);
    uint32_t ap[3] = {9, (uint32_t)(uintptr_t)(scratch + 40), 0xbeef};
    memcpy(scratch + 440, ap, sizeof(ap));
}
typedef struct fx_scenario { const char *symbol; uint32_t args[5]; } fx_scenario;
#define S(o) ((uint32_t)(uintptr_t)(scratch + (o)))
static int fx_scenarios(fx_scenario *out) {
    const fx_scenario list[] = {
        {"_memset", {S(3), 0x1ab, 13}}, {"_memcpy", {S(21), S(20), 9}}, {"_memmove", {S(21), S(20), 9}},
        {"_memcmp", {S(40), S(60), 4}}, {"_strlen", {S(40)}}, {"_strcat", {S(80), S(40)}},
        {"_strncat", {S(120), S(40), 3}}, {"_strncpy", {S(160), S(40), 15}}, {"_strchr", {S(40), 'l'}},
        {"_strrchr", {S(40), 'l'}}, {"_strstr", {S(40), S(200)}}, {"_strncmp", {S(40), S(60), 5}},
        {"_stricmp", {S(40), S(220)}}, {"_strnicmp", {S(40), S(220), 30}}, {"_strupr", {S(240)}},
        {"_strlwr", {S(260)}}, {"_itoa", {(uint32_t)-1234, S(280), 10}}, {"_atol", {S(300)}},
        {"_sprintf", {S(320), S(400), 7, S(40), 0xbeef}}, {"_vsprintf", {S(360), S(400), S(440)}},
    };
    memcpy(out, list, sizeof(list));
    return (int)(sizeof(list) / sizeof(list[0]));
}
static unsigned import_index(const char *symbol) {
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) if (!strcmp(rtl_imports[i].symbol, symbol)) return i;
    return RTL_IMPORT_COUNT;
}
/* One call through DBSIM's thunk; registers kept, ESP back, DF clear. */
static int fx_thunk_call(unsigned i, const uint32_t args[5]) {
    memset(fx_args, 0, sizeof(fx_args));
    memcpy(fx_args, args, 5 * sizeof(uint32_t));
    fx_target = thunk_va(i);
    fx_call();
    CHECK(fx_out[3] == 0xb0b0b0b0u && fx_out[4] == 0x51515151u && fx_out[5] == 0xd1d1d1d1u && fx_out[6] == 0xe0e0e0e0u);
    CHECK(fx_out[7] == fx_esp_call && !(fx_flags & 0x400u) && fx_below[63] == (uint32_t)(uintptr_t)fx_return);
    return 0;
}
static unsigned residue_length(unsigned i) {
    int f = rtl_own_function(i);
    return f >= 0 && rtl_residue(f) ? (unsigned)strlen(rtl_residue(f)) : 0u;
}
static uint32_t residue_expected(char reg) {
    return reg == 'p' ? 0xe0e0e0e0u : reg == 'b' ? 0xb0b0b0b0u : reg == 's' ? 0x51515151u : 0xd1d1d1d1u;
}
static int replaced_case(void) {
    CHECK(!make_image());
    /* Plain counting exports (the probes record or re-enter). */
    exports[RTL_I_memset] = fake_table[RTL_I_memset]; *slot(RTL_I_memset) = exports[RTL_I_memset];
    exports[RTL_I_sprintf] = fake_table[RTL_I_sprintf]; *slot(RTL_I_sprintf) = exports[RTL_I_sprintf];
    take_snapshot();
    rtl_options o; options(&o);
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) if (rtl_own_function(i) >= 0) o.modes[i] = ES2_HOOK_REPLACED;
    CHECK(rtl_install(&o));
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i)
        CHECK(*slot(i) == (rtl_imports[i].shim ? rtl_thunk_table[i] : exports[i]) &&
              rtl_targets[i] == (rtl_own_function(i) >= 0 ? rtl_replaced_table[i] : exports[i]));
    fx_scenario list[20];
    int count = fx_scenarios(list);
    CHECK(count == 20);
    for (int c = 0; c < count; ++c) {
        unsigned i = import_index(list[c].symbol);
        int f = rtl_own_function(i);
        CHECK(i < RTL_IMPORT_COUNT && f >= 0);
        scratch_fill();
        memcpy(scratch_before, scratch, sizeof(scratch));
        LONG hits = fake_hits[i];
        CHECK(!fx_thunk_call(i, list[c].args));
        memcpy(scratch_after, scratch, sizeof(scratch));
        uint32_t cursor = fx_args[0];
        /* The same call straight into the own code, from the same state. */
        memcpy(scratch, scratch_before, sizeof(scratch));
        uint32_t direct[8] = {0};
        memcpy(direct, list[c].args, 5 * sizeof(uint32_t));
        es2_rtl_regs r = {0xa0a0a0a0u, 0xc0c0c0c0u, 0xd0d0d0d0u};
        CHECK(es2_rtl_call((es2_rtl_function)f, &rtl_direct_memory, (uint32_t)(uintptr_t)direct, &r) == ES2_RTL_DONE);
        if (memcmp(scratch, scratch_after, sizeof(scratch)) || fake_hits[i] != hits) { fprintf(stderr, "%s: memory or export\n", list[c].symbol); return 1; }
        uint32_t stack_ecx = f == ES2_RTL_ITOA ? fx_esp_call - 72u : fx_esp_call - 220u;
        BOOL stack_result = f == ES2_RTL_ITOA || f == ES2_RTL_SPRINTF || f == ES2_RTL_VSPRINTF;
        if (fx_out[0] != r.eax || (stack_result ? fx_out[1] != stack_ecx : fx_out[1] != r.ecx) ||
            (f == ES2_RTL_ITOA ? fx_out[2] != stack_ecx : fx_out[2] != r.edx) || cursor != direct[0]) {
            fprintf(stderr, "%s: registers %08x %08x %08x, direct %08x %08x %08x\n", list[c].symbol,
                    fx_out[0], fx_out[1], fx_out[2], r.eax, r.ecx, r.edx);
            return 1;
        }
        /* Leaf residue below the return address; nothing else written. */
        unsigned length = residue_length(i);
        for (unsigned k = 0; k < length; ++k) CHECK(fx_below[62 - k] == residue_expected(rtl_residue(f)[k]));
        for (unsigned k = 0; k + length < 63; ++k) CHECK(fx_below[k] == 0x5a5a5a5au);
        CHECK(rtl_counters[i].calls == 1 && rtl_counters[i].hook == 1 && rtl_skipped[i] == 0);
    }
    /* A format outside the own subset goes to the export (counted skipped). */
    scratch_fill();
    memcpy(scratch + 400, "%5.2f", 6);
    unsigned sp = import_index("_sprintf");
    LONG hits = fake_hits[sp];
    uint32_t args[5] = {S(320), S(400), 0, 0, 0};
    CHECK(!fx_thunk_call(sp, args));
    CHECK(fake_hits[sp] == hits + 1 && fx_out[0] == sp && rtl_skipped[sp] == 1 && scratch[320] == 0x33);
    /* A call while an adapter runs goes to the export. */
    unsigned ms = import_index("_memset");
    hits = fake_hits[ms];
    rtl_busy = 1;
    uint32_t margs[5] = {S(3), 0, 4, 0, 0};
    CHECK(!fx_thunk_call(ms, margs));
    rtl_busy = 0;
    CHECK(fake_hits[ms] == hits + 1 && fx_out[0] == ms && rtl_skipped[ms] == 1 && scratch[3] == 0x33);
    CHECK(rtl_counters[ms].calls == 2);
    /* Other threads reach the export. */
    HANDLE thread = CreateThread(NULL, 0, foreign_thread, NULL, 0, NULL);
    CHECK(thread && WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CHECK(fake_hits[ms] == hits + 1 + 1000 && rtl_foreign_calls == 2000);
    CHECK(rtl_uninstall() && image_unchanged());
    printf("PASS replaced: 20 imports through their thunks equal the own code (memory, EAX/ECX/EDX, sprintf cursor), "
           "EBX/ESI/EDI/EBP/ESP/DF kept, leaf residue written, nothing else below ESP; fallback, busy and foreign calls reach the export\n");
    return 0;
}

static int compare_round(void (*memset_export)(void), void (*strlen_export)(void), const char *expect, BOOL keep) {
    unsigned ms = import_index("_memset"), sl = import_index("_strlen"), sp = import_index("_sprintf");
    exports[ms] = (uint32_t)(uintptr_t)memset_export; *slot(ms) = exports[ms];
    exports[sl] = (uint32_t)(uintptr_t)strlen_export; *slot(sl) = exports[sl];
    take_snapshot();
    rtl_options o; options(&o);
    o.modes[ms] = o.modes[sl] = o.modes[sp] = ES2_HOOK_COMPARE;
    CHECK(rtl_install(&o) && rtl_targets[ms] == rtl_compare_table[ms]);
    scratch_fill();
    LONG hits = fake_hits[ms];
    uint32_t margs[5] = {S(3), 0x1ab, 13, 0, 0};
    CHECK(!fx_thunk_call(ms, margs));
    /* The caller has the export's results and memory. */
    CHECK(fake_hits[ms] == hits + 1 && fx_out[0] == S(3) && fx_out[1] == 0 && fx_out[2] == 13);
    CHECK(fx_below[62] == 0xe0e0e0e0u && fx_below[61] == 0xd1d1d1d1u && fx_below[60] == 0x5a5a5a5au);
    uint32_t sargs[5] = {S(40), 0, 0, 0, 0};
    CHECK(!fx_thunk_call(sl, sargs));
    CHECK(fx_out[0] == 12 && fx_out[1] == 13);
    uint64_t mismatches = rtl_counters[ms].mismatches + rtl_counters[sl].mismatches;
    if (!expect) CHECK(mismatches == 0 && !rtl_mismatch_detail[0]);
    else {
        if (mismatches != 1 || strncmp(rtl_mismatch_detail, expect, strlen(expect)))
            fprintf(stderr, "mismatches %llu detail %s, want %s\n", (unsigned long long)mismatches, rtl_mismatch_detail, expect);
        CHECK(mismatches == 1 && !strncmp(rtl_mismatch_detail, expect, strlen(expect)));
    }
    CHECK(rtl_skipped[ms] == 0 && rtl_skipped[sl] == 0);
    /* sprintf outside the own subset: not compared, the export answers. */
    memcpy(scratch + 400, "%e", 3);
    uint32_t pargs[5] = {S(320), S(400), 0, 0, 0};
    CHECK(!fx_thunk_call(sp, pargs));
    CHECK(fx_out[0] == sp && rtl_skipped[sp] == 1 && rtl_counters[sp].mismatches == 0);
    if (!keep) CHECK(rtl_uninstall() && image_unchanged());
    return 0;
}
static int compare_case(void) {
    CHECK(!make_image());
    exports[RTL_I_sprintf] = fake_table[RTL_I_sprintf]; *slot(RTL_I_sprintf) = exports[RTL_I_sprintf];
    CHECK(!compare_round(cw_memset, cw_strlen, NULL, FALSE));
    CHECK(!compare_round(cw_memset_last, cw_strlen, "memset:memory@", FALSE));
    CHECK(!compare_round(cw_memset_over, cw_strlen, "memset:guard@", FALSE));
    CHECK(!compare_round(cw_memset, cw_strlen_edx, "strlen:registers@00000000", TRUE));
    /* The exit record carries the first mismatch and the skipped counters. */
    static char line[16384];
    int n = rtl_tick_fields(line, sizeof(line), TRUE);
    CHECK(n > 0 && strstr(line, ",\"rtl_memset_skipped\":0,") && strstr(line, ",\"rtl_sprintf_skipped\":1,") &&
          !strstr(line, "rtl_memcpy_skipped") && strstr(line, ",\"rtl_first_mismatch\":\"strlen:registers@00000000:args="));
    CHECK(rtl_uninstall() && image_unchanged());
    printf("PASS compare: stand-in exports compared with the own code, the caller gets the export's results; "
           "a wrong byte, a byte too many and a wrong EDX are counted; formats outside the subset are skipped\n");
    return 0;
}

/* ---- calloc/realloc/free replaced by the own heap ---- */
static int heap_call3(unsigned i, uint32_t a0, uint32_t a1, uint32_t *eax) {
    uint32_t args[5] = {a0, a1, 0, 0, 0};
    CHECK(!fx_thunk_call(i, args));
    for (unsigned k = 0; k < 63; ++k) CHECK(fx_below[k] == 0x5a5a5a5au);   /* nothing below the return address */
    *eax = fx_out[0];
    return 0;
}
static uint32_t heap_header(uint32_t p) { return *(const uint32_t *)(uintptr_t)(p - 4u); }
static int heap_case(void) {
    CHECK(!make_image());
    take_snapshot();
    rtl_options o; options(&o);
    o.modes[RTL_I_calloc] = o.modes[RTL_I_realloc] = o.modes[RTL_I_free] = ES2_HOOK_REPLACED;
    rtl_heap_own_platform_at = 1;   /* 1 MiB reservations */
    CHECK(rtl_install(&o) && rtl_heap_own_active);
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i)
        CHECK(rtl_targets[i] == (rtl_heap_function_of(i) >= 0 ? rtl_replaced_table[i] : exports[i]) &&
              rtl_heap_replaced[i] == (rtl_heap_function_of(i) >= 0));
    const uint32_t sentinel = (uint32_t)(uintptr_t)&rtl_heap_own_head[2];
    CHECK(rtl_heap_own.sentinel == sentinel && rtl_heap_own_head[2] == sentinel && rtl_heap_own_head[3] == sentinel);
    LONG calloc_hits = fake_hits[RTL_I_calloc], free_hits = fake_hits[RTL_I_free], realloc_hits = fake_hits[RTL_I_realloc];
    /* calloc: the first one reserves and commits; EAX the block, ECX 0, EDX count*n; LastError kept. */
    uint32_t x, y, z, eax;
    SetLastError(0x1234abcdu);
    CHECK(!heap_call3(RTL_I_calloc, 1, 16, &x));
    CHECK(GetLastError() == 0x1234abcdu && fx_out[1] == 0 && fx_out[2] == 16);
    CHECK(rtl_heap_own.region_count == 1 && x == rtl_heap_own.region_base[0] + 8u && heap_header(x) == (24u | 1u));
    CHECK(rtl_heap_own_os_calls == 1u + 16u && rtl_heap_own_os_failed == 0);   /* a reservation, 16 pages of 4 KiB */
    CHECK(!heap_call3(RTL_I_calloc, 2, 8, &y) && y == x + 24u && fx_out[1] == 0 && fx_out[2] == 16);
    CHECK(!heap_call3(RTL_I_calloc, 4, 4, &z) && z == y + 24u);
    for (unsigned k = 0; k < 16; ++k) CHECK(((const BYTE *)(uintptr_t)z)[k] == 0);
    /* A refused size: NULL, ECX = count*n, EDX unchanged. */
    CHECK(!heap_call3(RTL_I_calloc, 0, 9, &eax) && eax == 0 && fx_out[1] == 0 && fx_out[2] == 0xd0d0d0d0u);
    CHECK(!heap_call3(RTL_I_calloc, 0x10000u, 0x10000u, &eax) && eax == 0 && fx_out[1] == 0 && fx_out[2] == 0xd0d0d0d0u);
    /* free: inserted after the head block (EAX p, ECX the former next, EDX the head block). */
    uint32_t rest = z + 24u;
    CHECK(rtl_heap_own_head[3] == rest);
    CHECK(!heap_call3(RTL_I_free, x, 0, &eax) && eax == x && fx_out[1] == rest && fx_out[2] == sentinel);
    /* free joined into the free block below: EAX the start, ECX its end, EDX the size. */
    CHECK(!heap_call3(RTL_I_free, y, 0, &eax) && eax == x && fx_out[1] == x + 48u && fx_out[2] == 48u);
    /* A block without the in-use flag: nothing; EAX p, ECX kept, EDX its header word. */
    CHECK(!heap_call3(RTL_I_free, x, 0, &eax) && eax == x && fx_out[1] == 0xc0c0c0c0u && fx_out[2] == 48u);
    CHECK(!heap_call3(RTL_I_free, 0, 0, &eax) && eax == 0 && fx_out[1] == 0xc0c0c0c0u && fx_out[2] == 0xd0d0d0d0u);
    /* realloc: moved (the block above in use), the data kept; ECX/EDX unchanged. */
    uint32_t a, b, c;
    CHECK(!heap_call3(RTL_I_calloc, 1, 100, &a) && !heap_call3(RTL_I_calloc, 1, 100, &b));
    for (unsigned k = 0; k < 100; ++k) ((BYTE *)(uintptr_t)a)[k] = (BYTE)(k * 7u + 1u);
    CHECK(!heap_call3(RTL_I_realloc, a, 300, &c) && c != a && c > b && fx_out[1] == 0xc0c0c0c0u && fx_out[2] == 0xd0d0d0d0u);
    for (unsigned k = 0; k < 100; ++k) CHECK(((const BYTE *)(uintptr_t)c)[k] == (BYTE)(k * 7u + 1u));
    CHECK(!(heap_header(a) & 1u) && heap_header(c) == (308u | 1u));
    /* realloc in place into the free rest above; realloc(p, 0) frees; realloc(NULL, n) allocates. */
    CHECK(!heap_call3(RTL_I_realloc, c, 5000, &eax) && eax == c && heap_header(c) == (5008u | 1u));
    CHECK(!heap_call3(RTL_I_realloc, b, 0, &eax) && eax == 0 && heap_header(a) == 216u);   /* joined into a */
    CHECK(!heap_call3(RTL_I_realloc, 0, 40, &eax) && eax && heap_header(eax) == (48u | 1u));
    CHECK(fake_hits[RTL_I_calloc] == calloc_hits && fake_hits[RTL_I_free] == free_hits && fake_hits[RTL_I_realloc] == realloc_hits);
    /* Pointers outside the own regions go to the export (counted, not skipped). */
    CHECK(!heap_call3(RTL_I_free, S(64), 0, &eax) && eax == RTL_I_free && fake_hits[RTL_I_free] == free_hits + 1);
    CHECK(!heap_call3(RTL_I_realloc, S(64), 8, &eax) && eax == RTL_I_realloc && fake_hits[RTL_I_realloc] == realloc_hits + 1);
    CHECK(rtl_heap_foreign_frees == 2 && rtl_skipped[RTL_I_free] == 0 && rtl_skipped[RTL_I_realloc] == 0);
    CHECK(rtl_counters[RTL_I_calloc].calls == 7 && rtl_counters[RTL_I_free].calls == 5 && rtl_counters[RTL_I_realloc].calls == 5);
    /* The heap is valid (a walk), the fields report it. */
    int32_t blocks = es2_heap_walk(&rtl_heap_own, rtl_heap_own_read, NULL, NULL, NULL);
    CHECK(blocks >= 5);
    static char line[16384];
    CHECK(rtl_tick_fields(line, sizeof(line), FALSE) > 0 && strstr(line, ",\"rtl_heap_foreign_frees\":2"));
    char want[96];
    snprintf(want, sizeof(want), ",\"rtl_heap_own_regions\":1,\"rtl_heap_own_blocks\":%ld,", (long)blocks);
    CHECK(rtl_tick_fields(line, sizeof(line), TRUE) > 0 && strstr(line, want) && strstr(line, ",\"rtl_calloc_skipped\":0,") &&
          strstr(line, ",\"rtl_heap_own_os_failed\":0") && !strstr(line, "rtl_heap_shadow_mismatches"));
    /* Busy (a nested call): the export answers, counted skipped. */
    rtl_busy = 1;
    CHECK(!heap_call3(RTL_I_calloc, 1, 8, &eax));
    rtl_busy = 0;
    CHECK(eax == RTL_I_calloc && rtl_skipped[RTL_I_calloc] == 1);
    CHECK(rtl_uninstall() && image_unchanged() && !rtl_heap_own_active && !rtl_heap_replaced[RTL_I_free]);
    rtl_heap_own_platform_at = -1;
    printf("PASS heap: calloc/realloc/free through their thunks on the own heap (own reservation, block layout, zero fill, "
           "both free paths and their EAX/ECX/EDX, refused sizes, realloc moved/in place/0/NULL with the data kept), "
           "nothing below ESP, LastError kept, foreign pointers to the export, busy calls skipped, fields\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "heap")) return heap_case();
    if (!strcmp(argv[1], "replaced")) return replaced_case();
    if (!strcmp(argv[1], "compare")) return compare_case();
    if (!strcmp(argv[1], "sha256")) return sha_case();
    if (!strcmp(argv[1], "forward")) return install_case();
    if (!strcmp(argv[1], "refuse")) return refuse_case();
    if (!strcmp(argv[1], "rollback")) return rollback_case();
    if (!strcmp(argv[1], "foreign")) return foreign_case();
    if (!strcmp(argv[1], "config")) return config_case();
    if (!strcmp(argv[1], "entry")) return entry_case();
    if (!strcmp(argv[1], "reference")) return reference_case();
    return 2;
}
