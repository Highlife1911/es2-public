/* Own-code fixture for proxy/bwcc32_switch.h (DBSIM's LoadLibraryA of
 * BWCC32.DLL answered with failure). A synthetic DBSIM-shaped image carries
 * the three instructions (push of the name at 0x46513b, the call of the
 * LoadLibraryA thunk at 0x465140, the `lea edx, [ebp-0x28]` after it), the
 * thunk 0x496904 and the IAT slot 0x6cc3f8 with an own recorder in it. No
 * original game code runs. Modes:
 *   switch     the switch requested (ES2_BWCC32) or not; installed: the call returns 0 with
 *              LastError 126, pops its argument, keeps EBX/ESI/EDI/EBP, never reaches the slot;
 *              only the call's rel32 changed; not installed: the slot is reached
 *   refuse     a changed push, call target, thunk, slot, name or following instruction, a
 *              wrong image size or base: nothing changed; a second installation is refused
 *   reference  ES2_DATA_ROOT's DBSIM.EXE: the three instructions, the slot is KERNEL32's
 *              LoadLibraryA in the import directory, the site is one of the thunk's callers and
 *              the only push of the name, no registered hook prologue covers it */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ES2_BW_NO_PROCESS_ENTRY
#include "../bwcc32_switch.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

/* ---- the slot's recorder and the caller with known registers ---- */
__attribute__((used)) static volatile LONG slot_calls;
__attribute__((used)) static uint32_t slot_argument;
__attribute__((used)) static uint32_t probe_out[6], probe_target, probe_esp;
__asm__(
    /* stdcall HMODULE recorder(LPCSTR): what the real LoadLibraryA would be. */
    ".text\n_slot_recorder:\n"
    "    lock incl _slot_calls\n"
    "    movl 4(%esp), %eax\n    movl %eax, _slot_argument\n"
    "    movl $0x10000000, %eax\n"
    "    ret $4\n"
    /* cdecl void probe_call(uint32_t start): known callee-saved registers, then
     * a call of the code at `start` (push, call, lea, ret); records EAX, EBX,
     * ESI, EDI, EBP and ESP after it. */
    "_probe_call:\n"
    "    pushl %ebp\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    movl 20(%esp), %eax\n    movl %eax, _probe_target\n"
    "    movl $0xb0b0b0b0, %ebx\n    movl $0x51515151, %esi\n    movl $0xd1d1d1d1, %edi\n    movl %esp, %ebp\n"
    "    movl %esp, _probe_esp\n"
    "    call *_probe_target\n"
    "    movl %eax, _probe_out\n    movl %ebx, _probe_out+4\n    movl %esi, _probe_out+8\n    movl %edi, _probe_out+12\n"
    "    movl %ebp, _probe_out+16\n    movl %esp, _probe_out+20\n"
    "    popl %edi\n    popl %esi\n    popl %ebx\n    popl %ebp\n    ret\n");
extern void slot_recorder(void) __asm__("_slot_recorder");
extern void __cdecl probe_call(uint32_t start) __asm__("_probe_call");

/* ---- synthetic image ---- */
static BYTE *image, *copy;
static void put32(BYTE *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t va(uint32_t rva) { return (uint32_t)(uintptr_t)image + rva; }
static int make_image(void) {
    if (!image) image = VirtualAlloc(NULL, BW_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!copy) copy = malloc(BW_IMAGE_SIZE);
    CHECK(image && copy);
    memset(image, 0xcc, BW_IMAGE_SIZE);
    memset(image, 0, 0x1000);
    image[0] = 'M'; image[1] = 'Z'; put32(image + 0x3c, 0x80);
    BYTE *nt = image + 0x80;
    put32(nt, 0x00004550u); nt[4] = 0x4c; nt[5] = 0x01; nt[24] = 0x0b; nt[25] = 0x01;
    put32(nt + 24 + 28, (uint32_t)(uintptr_t)image);
    put32(nt + 24 + 56, BW_IMAGE_SIZE);
    BYTE *p = image + BW_PUSH_RVA;
    p[0] = 0x68; put32(p + 1, va(BW_NAME_RVA));
    p = image + BW_CALL_RVA;
    p[0] = 0xe8; put32(p + 1, va(BW_THUNK_RVA) - (va(BW_CALL_RVA) + 5));
    memcpy(image + BW_NEXT_RVA, "\x8d\x55\xd8\xc3", 4);   /* lea edx, [ebp-0x28]; ret */
    p = image + BW_THUNK_RVA;
    p[0] = 0xff; p[1] = 0x25; put32(p + 2, va(BW_SLOT_RVA));
    put32(image + BW_SLOT_RVA, (uint32_t)(uintptr_t)slot_recorder);
    memcpy(image + BW_NAME_RVA - 4, "ES2\0BWCC32.DLL", 15);
    memcpy(copy, image, BW_IMAGE_SIZE);
    bw_state = BW_OFF; bw_refusals = 0; bw_forwarded = 0; slot_calls = 0;
    return 0;
}
static BOOL unchanged(void) { return !memcmp(copy, image, BW_IMAGE_SIZE); }

/* One call through the site; the callee-saved registers and ESP must come back. */
static int run_site(void) {
    SetLastError(0x1234);
    probe_call(va(BW_PUSH_RVA));
    CHECK(probe_out[1] == 0xb0b0b0b0u && probe_out[2] == 0x51515151u && probe_out[3] == 0xd1d1d1d1u);
    CHECK(probe_out[4] == probe_esp && probe_out[5] == probe_esp);
    return 0;
}

static int switch_case(void) {
    CHECK(!make_image());
    /* Requested only by ES2_BWCC32=refuse (any case). */
    SetEnvironmentVariableW(L"ES2_BWCC32", NULL); CHECK(!bw_requested());
    SetEnvironmentVariableW(L"ES2_BWCC32", L"load"); CHECK(!bw_requested());
    SetEnvironmentVariableW(L"ES2_BWCC32", L"Refuse"); CHECK(bw_requested());
    SetEnvironmentVariableW(L"ES2_BWCC32", NULL);
    /* Not installed: the original call reaches the slot. */
    CHECK(!run_site() && slot_calls == 1 && slot_argument == va(BW_NAME_RVA) && probe_out[0] == 0x10000000u);
    CHECK(GetLastError() == 0x1234 && unchanged());
    /* Installed: only the call's rel32 changed; the call fails as a missing DLL. */
    CHECK(bw_install_image(image, (uintptr_t)image) && bw_state == BW_INSTALLED);
    CHECK(image[BW_CALL_RVA] == 0xe8 && va(BW_CALL_RVA) + 5 + bw_u32(image + BW_CALL_RVA + 1) == (uint32_t)(uintptr_t)bw_refuse_load);
    CHECK(!memcmp(image, copy, BW_CALL_RVA + 1) &&
          !memcmp(image + BW_CALL_RVA + 5, copy + BW_CALL_RVA + 5, BW_IMAGE_SIZE - BW_CALL_RVA - 5));
    CHECK(bw_original_rel == bw_u32(copy + BW_CALL_RVA + 1));
    for (unsigned i = 1; i <= 3; ++i) {
        CHECK(!run_site() && probe_out[0] == 0 && GetLastError() == ERROR_MOD_NOT_FOUND);
        CHECK(slot_calls == 1 && bw_refusals == (LONG)i && bw_forwarded == 0);
    }
    /* Another name (not possible at the site) is forwarded to the slot. */
    CHECK(bw_refuse_load("SOS9503.DLL") == (HMODULE)0x10000000 && slot_calls == 2 && bw_forwarded == 1);
    /* A process has one installation. */
    CHECK(!bw_install_image(image, (uintptr_t)image) && bw_state == BW_INSTALLED);
    printf("PASS switch: off unless ES2_BWCC32=refuse; installed, only the rel32 of the call at 0x465140 changes and the call "
           "returns 0 with LastError 126, pops its argument and keeps EBX/ESI/EDI/EBP without reaching LoadLibraryA\n");
    return 0;
}

static int refuse_one(const char *what, void (*spoil)(void), LONG state) {
    if (make_image()) return 1;
    spoil();
    memcpy(copy, image, BW_IMAGE_SIZE);
    if (bw_install_image(image, (uintptr_t)image) || bw_state != state || !unchanged()) {
        fprintf(stderr, "FAIL refuse: %s (state %ld)\n", what, (long)bw_state);
        return 1;
    }
    return 0;
}
static void spoil_push(void) { put32(image + BW_PUSH_RVA + 1, va(BW_NAME_RVA) + 1); }
static void spoil_call(void) { put32(image + BW_CALL_RVA + 1, bw_u32(image + BW_CALL_RVA + 1) + 6); }
static void spoil_opcode(void) { image[BW_CALL_RVA] = 0xe9; }
static void spoil_thunk(void) { put32(image + BW_THUNK_RVA + 2, va(BW_SLOT_RVA) + 4); }
static void spoil_name(void) { image[BW_NAME_RVA + 5] = '3'; }
static void spoil_next(void) { image[BW_NEXT_RVA + 2] = 0xd4; }
static void spoil_size(void) { put32(image + 0x80 + 24 + 56, BW_IMAGE_SIZE - 0x1000); }
static int refuse_case(void) {
    if (refuse_one("push", spoil_push, BW_REFUSED_SITE) || refuse_one("call target", spoil_call, BW_REFUSED_SITE) ||
        refuse_one("call opcode", spoil_opcode, BW_REFUSED_SITE) || refuse_one("thunk slot", spoil_thunk, BW_REFUSED_SITE) ||
        refuse_one("name", spoil_name, BW_REFUSED_SITE) || refuse_one("next instruction", spoil_next, BW_REFUSED_SITE) ||
        refuse_one("image size", spoil_size, BW_REFUSED_IMAGE)) return 1;
    CHECK(!make_image());
    CHECK(!bw_install_image(image, (uintptr_t)image + 0x10000) && bw_state == BW_REFUSED_IMAGE && unchanged());
    CHECK(!run_site() && slot_calls == 1);   /* a refusal leaves the original call */
    printf("PASS refuse: a changed push, call, thunk, name or following instruction, a wrong image size or base: nothing changed\n");
    return 0;
}

/* ---- the reference file (ES2_DATA_ROOT) ---- */
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
    uint32_t pe = bw_u32(bytes + 0x3c);
    CHECK(bw_u32(bytes + pe + 24 + 56) == BW_IMAGE_SIZE && bw_u32(bytes + pe + 24 + 28) == BW_LINK_BASE);
    uint32_t headers = bw_u32(bytes + pe + 24 + 60);
    unsigned sections = bytes[pe + 6] | bytes[pe + 7] << 8, optional = bytes[pe + 20] | bytes[pe + 21] << 8;
    /* Mapped at a private address; the checks compare against the link base. */
    BYTE *map = calloc(1, BW_IMAGE_SIZE);
    CHECK(map);
    memcpy(map, bytes, headers);
    uint32_t code_rva = 0, code_size = 0;
    for (unsigned s = 0; s < sections; ++s) {
        const BYTE *h = bytes + pe + 24 + optional + 40 * s;
        uint32_t rva = bw_u32(h + 12), raw = bw_u32(h + 16), offset = bw_u32(h + 20);
        if (raw && rva < BW_IMAGE_SIZE) memcpy(map + rva, bytes + offset, raw < BW_IMAGE_SIZE - rva ? raw : BW_IMAGE_SIZE - rva);
        if (!code_size && (bw_u32(h + 36) & 0x20u)) { code_rva = rva; code_size = raw; }
    }
    CHECK(code_size);
    /* The three instructions with link-base addresses. */
    const BYTE *push = map + BW_PUSH_RVA, *call = map + BW_CALL_RVA, *thunk = map + BW_THUNK_RVA;
    CHECK(push[0] == 0x68 && bw_u32(push + 1) == BW_LINK_BASE + BW_NAME_RVA);
    CHECK(call[0] == 0xe8 && BW_CALL_RVA + 5 + bw_u32(call + 1) == BW_THUNK_RVA);
    CHECK(thunk[0] == 0xff && thunk[1] == 0x25 && bw_u32(thunk + 2) == BW_LINK_BASE + BW_SLOT_RVA);
    CHECK(!memcmp(map + BW_NAME_RVA, "BWCC32.DLL", 11) && !memcmp(map + BW_NEXT_RVA, "\x8d\x55\xd8", 3));
    /* The slot is KERNEL32's LoadLibraryA. */
    uint32_t imports = bw_u32(map + pe + 24 + 104);
    int found = 0;
    for (const BYTE *d = map + imports; bw_u32(d + 12); d += 20) {
        if (_stricmp((const char *)map + bw_u32(d + 12), "KERNEL32.dll")) continue;
        uint32_t names = bw_u32(d) ? bw_u32(d) : bw_u32(d + 16), slots = bw_u32(d + 16);
        for (uint32_t k = 0; bw_u32(map + names + 4 * k); ++k) {
            uint32_t entry = bw_u32(map + names + 4 * k);
            if (!(entry & 0x80000000u) && !strcmp((const char *)map + entry + 2, "LoadLibraryA")) found = slots + 4 * k == BW_SLOT_RVA;
        }
    }
    CHECK(found);
    /* The site is a caller of the thunk and the only push of the name. */
    unsigned callers = 0, site_seen = 0, pushes = 0;
    for (uint32_t r = code_rva; r + 5 <= code_rva + code_size; ++r) {
        if (map[r] == 0xe8 && r + 5 + bw_u32(map + r + 1) == BW_THUNK_RVA) { ++callers; site_seen += r == BW_CALL_RVA; }
        if (map[r] == 0x68 && bw_u32(map + r + 1) == BW_LINK_BASE + BW_NAME_RVA) ++pushes;
    }
    CHECK(site_seen == 1 && pushes == 1);
    /* No registered hook prologue covers the five bytes. */
    for (unsigned i = 0; i < sizeof(registry_rvas) / sizeof(registry_rvas[0]); ++i)
        CHECK(!(registry_rvas[i] + 16 > BW_CALL_RVA && registry_rvas[i] < BW_CALL_RVA + 5));
    printf("PASS reference: push 0x4a0859 (\"BWCC32.DLL\") at 0x46513b, call 0x496904 at 0x465140 (one of its %u callers), "
           "jmp [0x6cc3f8] = KERNEL32!LoadLibraryA, lea edx after it; no hook prologue at the site (%u hooks)\n",
           callers, (unsigned)(sizeof(registry_rvas) / sizeof(registry_rvas[0])));
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "switch")) return switch_case();
    if (!strcmp(argv[1], "refuse")) return refuse_case();
    if (!strcmp(argv[1], "reference")) return reference_case();
    return 2;
}
