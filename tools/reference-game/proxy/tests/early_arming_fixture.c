/* window-lint: allow-file inherits console: Bazel test helper; the child shares the windowless console of its parent (execv: non-Windows branch) */
/* L0: own-code fixture for proxy/early_arming.h (arming at the mission load
 * and the after-load snapshot). A synthetic DBSIM-shaped image carries the two
 * call sites (`call 0x461608` at 0x45f42b, `call 0x45f570` at 0x45f487) with
 * own recorder stubs as their targets; a synthetic CW3220 data block and two
 * synthetic heap regions stand in for the runtime's heap. No original game
 * code runs. Modes:
 *   off        nothing requested: no allocation, no byte changed, targets reached directly
 *   arm        [arming] early: the step runs once, before the target, on the private
 *              stack; the target sees the call's registers, ESP, stack, flags' DF,
 *              x87 state, LastError and TEB bounds; the site is original again
 *   refuse     bad site, bad target, bad image, busy: nothing is changed
 *   snapshot   [snapshot] after_load: the file layout, heap walk, flags, extra regions,
 *              truncation; with <python> <after_load_snapshot.py> also the comparison of
 *              a relocated heap (identical) and a changed byte (different)
 *   own        the own cw3220.dll loaded into the test (as DBSIM loads it, by the file
 *              name cw3220.dll): its heap is found through es2_own_runtime and walked (flag
 *              128), blocks allocated through its _calloc carry their contents, the block count
 *              equals es2_heap_walk's; with <python> <after_load_snapshot.py> the own heap
 *              against the same objects on the synthetic CW3220 heap (identical) and a changed
 *              byte (different)
 *   reference  ES2_DATA_ROOT's DBSIM.EXE: both sites, their single callers, the
 *              targets' prologues, no registered hook prologue at the sites */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ES2_EA_NO_PROCESS_ENTRY
#include "../early_arming.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

/* ---- recorders and the caller with known state ---- */
__attribute__((used)) static volatile LONG target_hits[2];
__attribute__((used)) static uint32_t rec_in[10], rec_fs4, rec_fs8, rec_last_error, rec_bad, rec_armed;
__attribute__((used)) static uint16_t rec_cw;
__attribute__((used)) static double rec_st0;
__attribute__((used)) static uint32_t probe_out[8], probe_target, probe_saved_ebp, probe_esp_call, fs4_before, fs8_before;
__attribute__((used)) static uint16_t probe_cw_orig, probe_cw_set = 0x0e7f;
__attribute__((used)) static double probe_st0_out;
__attribute__((used)) static volatile uint32_t fx_armed;
__asm__(
    /* Target stand-in: records what the call handed over, checks that the
     * 256 bytes below ESP still hold the caller's pattern, returns 0x77777777. */
    ".macro RECORDER name, index\n"
    ".text\n_\\name:\n"
    "    lock incl _target_hits+4*\\index\n"
    "    movl %eax, _rec_in\n    movl %ecx, _rec_in+4\n    movl %edx, _rec_in+8\n    movl %ebx, _rec_in+12\n"
    "    movl %esi, _rec_in+16\n    movl %edi, _rec_in+20\n    movl %ebp, _rec_in+24\n    movl %esp, _rec_in+28\n"
    "    movl (%esp), %eax\n    movl %eax, _rec_in+32\n    movl 4(%esp), %eax\n    movl %eax, _rec_in+36\n"
    "    movl %fs:4, %eax\n    movl %eax, _rec_fs4\n    movl %fs:8, %eax\n    movl %eax, _rec_fs8\n"
    "    movl %fs:0x34, %eax\n    movl %eax, _rec_last_error\n"
    "    fnstcw _rec_cw\n    fstl _rec_st0\n"
    "    movl _fx_armed, %eax\n    movl %eax, _rec_armed\n"
    "    movl $0, _rec_bad\n    leal -256(%esp), %esi\n    movl $64, %ecx\n"
    "1:  cmpl $0x5a5a5a5a, (%esi)\n    je 2f\n    incl _rec_bad\n"
    "2:  addl $4, %esi\n    decl %ecx\n    jnz 1b\n"
    "    movl _rec_in+16, %esi\n"
    "    movl $0x77777777, %eax\n"
    "    ret\n"
    ".endm\n"
    "RECORDER fake_load, 0\n"
    "RECORDER fake_frame, 1\n"
    ".purgem RECORDER\n"
    /* cdecl void probe_call(uint32_t target): known registers, x87 state,
     * LastError and a pattern below ESP, then one call of `target` (the call
     * site, which pushes its own return address below this one). */
    "_probe_call:\n"
    "    pushl %ebp\n    movl %esp, %ebp\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    movl 8(%ebp), %eax\n    movl %eax, _probe_target\n    movl %ebp, _probe_saved_ebp\n"
    "    movl %fs:4, %eax\n    movl %eax, _fs4_before\n    movl %fs:8, %eax\n    movl %eax, _fs8_before\n"
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
    "    movl _probe_saved_ebp, %ebp\n    leal -12(%ebp), %esp\n"
    "    popl %edi\n    popl %esi\n    popl %ebx\n    popl %ebp\n    ret\n");
extern void fake_load(void) __asm__("_fake_load");
extern void fake_frame(void) __asm__("_fake_frame");
extern void probe_return(void) __asm__("_probe_return");
extern void __cdecl probe_call(uint32_t target) __asm__("_probe_call");

/* ---- synthetic image ---- */
static BYTE *image, *copy;
static void put32(BYTE *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t va(uint32_t rva) { return (uint32_t)(uintptr_t)image + rva; }
static BYTE *at_va(uint32_t reference_va) { return image + (reference_va - EA_LINK_BASE); }
static void rel(BYTE *at, BYTE opcode, uint32_t target) { at[0] = opcode; put32(at + 1, target - ((uint32_t)(uintptr_t)at + 5)); }
static int make_image(void) {
    image = VirtualAlloc(NULL, EA_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    copy = malloc(EA_IMAGE_SIZE);
    CHECK(image && copy);
    image[0] = 'M'; image[1] = 'Z'; put32(image + 0x3c, 0x80);
    BYTE *nt = image + 0x80;
    put32(nt, 0x00004550u); nt[4] = 0x4c; nt[5] = 0x01; nt[24] = 0x0b; nt[25] = 0x01;
    put32(nt + 24 + 28, (uint32_t)(uintptr_t)image);
    put32(nt + 24 + 56, EA_IMAGE_SIZE);
    rel(image + EA_LOAD_CALL_RVA, 0xe8, va(EA_LOAD_TARGET_RVA)); image[EA_LOAD_CALL_RVA + 5] = 0xc3;
    rel(image + EA_LOAD_TARGET_RVA, 0xe9, (uint32_t)(uintptr_t)fake_load);
    rel(image + EA_FRAME_CALL_RVA, 0xe8, va(EA_FRAME_TARGET_RVA)); image[EA_FRAME_CALL_RVA + 5] = 0xc3;
    rel(image + EA_FRAME_TARGET_RVA, 0xe9, (uint32_t)(uintptr_t)fake_frame);
    return 0;
}
static void take_copy(void) { memcpy(copy, image, EA_IMAGE_SIZE); }
static BOOL image_unchanged(void) { return !memcmp(copy, image, EA_IMAGE_SIZE); }

/* The call reached its target with exactly the caller's state. */
static int check_call(unsigned which, uint32_t site_rva) {
    CHECK(rec_in[0] == 0xa0a0a0a0u && rec_in[1] == 0xc0c0c0c0u && rec_in[2] == 0xd0d0d0d0u && rec_in[3] == 0xb0b0b0b0u &&
          rec_in[4] == 0x51515151u && rec_in[5] == 0xd1d1d1d1u && rec_in[6] == 0xe0e0e0e0u);
    CHECK(rec_in[7] == probe_esp_call - 8 && rec_in[8] == va(site_rva) + 5 && rec_in[9] == (uint32_t)(uintptr_t)probe_return);
    CHECK(rec_fs4 == fs4_before && rec_fs8 == fs8_before && rec_last_error == 0x1234abcdu);
    CHECK(rec_cw == probe_cw_set && rec_st0 == 3.14159265358979323846 && rec_bad == 0);
    CHECK(probe_out[0] == 0x77777777u && probe_out[3] == 0xb0b0b0b0u && probe_out[4] == 0x51515151u &&
          probe_out[5] == 0xd1d1d1d1u && probe_out[6] == 0xe0e0e0e0u && probe_out[7] == probe_esp_call);
    (void)which;
    return 0;
}

/* ---- steps ---- */
static LONG arm_runs, snapshot_runs;
static uintptr_t step_stack, step_base, step_limit;
static BOOL step_saw_original;
static uint32_t step_original_rel;
static void record_step(uint32_t site_rva) {
    volatile BYTE here = 0;
    step_stack = (uintptr_t)&here;
    step_base = (uintptr_t)((NT_TIB *)NtCurrentTeb())->StackBase;
    step_limit = (uintptr_t)((NT_TIB *)NtCurrentTeb())->StackLimit;
    step_saw_original = ea_u32(image + site_rva + 1) == step_original_rel;
    SetLastError(0xdead);   /* restored by ea_run_step */
}
static void arm_step(void) { record_step(EA_LOAD_CALL_RVA); fx_armed = 1; ++arm_runs; }
static WCHAR snapshot_path[MAX_PATH];
static const BYTE *snapshot_cw;
static const ea_heap *cw_heap(void);
static BOOL snapshot_ok;
static void snapshot_step(void) {
    record_step(EA_FRAME_CALL_RVA); ++snapshot_runs;
    snapshot_ok = ea_snapshot_write(snapshot_path, snapshot_cw ? cw_heap() : NULL, NULL, 0, (uint64_t)1 << 30, fx_armed != 0);
}
static int check_private_stack(void) {
    CHECK(ea_private_base && ea_private_top - ea_private_base == EA_PRIVATE_STACK);
    CHECK(step_stack > ea_private_base && step_stack < ea_private_top);
    CHECK(step_base == ea_private_top && step_limit == ea_private_base);
    CHECK(step_saw_original);
    return 0;
}

static int off_case(void) {
    CHECK(!make_image());
    take_copy();
    CHECK(ea_install_image(image, (uintptr_t)image, FALSE, FALSE, arm_step, snapshot_step));
    CHECK(image_unchanged() && !ea_private_base && ea_sites[0].state == EA_OFF && ea_sites[1].state == EA_OFF);
    probe_call(va(EA_LOAD_CALL_RVA));
    CHECK(!check_call(0, EA_LOAD_CALL_RVA) && target_hits[0] == 1 && !arm_runs && rec_armed == 0);
    probe_call(va(EA_FRAME_CALL_RVA));
    CHECK(!check_call(1, EA_FRAME_CALL_RVA) && target_hits[1] == 1 && !snapshot_runs);
    printf("PASS off: nothing requested, no allocation, image unchanged, both calls reach their targets directly\n");
    return 0;
}

static int arm_case(void) {
    CHECK(!make_image());
    take_copy();
    step_original_rel = ea_u32(image + EA_LOAD_CALL_RVA + 1);
    CHECK(ea_install_image(image, (uintptr_t)image, TRUE, FALSE, arm_step, snapshot_step));
    CHECK(ea_sites[EA_SITE_LOAD].state == EA_PATCHED && ea_sites[EA_SITE_FRAME].state == EA_OFF);
    /* Only the rel32 of the load call changed; the frame call is untouched. */
    CHECK(image[EA_LOAD_CALL_RVA] == 0xe8 &&
          va(EA_LOAD_CALL_RVA) + 5 + ea_u32(image + EA_LOAD_CALL_RVA + 1) == (uint32_t)(uintptr_t)ea_entry_load);
    CHECK(!memcmp(image, copy, EA_LOAD_CALL_RVA + 1) &&
          !memcmp(image + EA_LOAD_CALL_RVA + 5, copy + EA_LOAD_CALL_RVA + 5, EA_IMAGE_SIZE - EA_LOAD_CALL_RVA - 5));
    probe_call(va(EA_LOAD_CALL_RVA));
    CHECK(arm_runs == 1 && target_hits[0] == 1 && rec_armed == 1);   /* armed before the target's first instruction */
    CHECK(!check_call(0, EA_LOAD_CALL_RVA) && !check_private_stack());
    CHECK(image_unchanged() && ea_sites[EA_SITE_LOAD].state == EA_RAN && ea_step_runs[EA_SITE_LOAD] == 1);
    /* A second call (there is none in DBSIM) goes straight to the target. */
    probe_call(va(EA_LOAD_CALL_RVA));
    CHECK(arm_runs == 1 && target_hits[0] == 2 && !check_call(0, EA_LOAD_CALL_RVA));
    probe_call(va(EA_FRAME_CALL_RVA));
    CHECK(!snapshot_runs && target_hits[1] == 1 && !check_call(1, EA_FRAME_CALL_RVA));
    /* A process has one installation. */
    CHECK(!ea_install_image(image, (uintptr_t)image, TRUE, TRUE, arm_step, snapshot_step) && image_unchanged());
    printf("PASS arm: armed once at the load call before the target, on the private stack (TEB bounds swapped), "
           "site original again; registers, ESP, the stack below it, x87, LastError and TEB bounds unchanged at the target\n");
    return 0;
}

static int refuse_case(void) {
    CHECK(!make_image());
    /* Wrong call target (the load call), non-call opcode (the frame call),
     * wrong image size: nothing patched, nothing allocated. */
    put32(image + EA_LOAD_CALL_RVA + 1, ea_u32(image + EA_LOAD_CALL_RVA + 1) + 1); take_copy();
    CHECK(!ea_install_image(image, (uintptr_t)image, TRUE, TRUE, arm_step, snapshot_step));
    CHECK(image_unchanged() && !ea_private_base && ea_sites[0].state == EA_REFUSED && ea_sites[1].state == EA_REFUSED);
    memset(ea_sites, 0, sizeof(ea_sites));
    CHECK(!ea_install_image(image, (uintptr_t)image, TRUE, FALSE, arm_step, snapshot_step) && image_unchanged() && !ea_private_base);
    memset(ea_sites, 0, sizeof(ea_sites));
    put32(image + EA_LOAD_CALL_RVA + 1, ea_u32(image + EA_LOAD_CALL_RVA + 1) - 1);
    image[EA_FRAME_CALL_RVA] = 0xe9; take_copy();
    CHECK(!ea_install_image(image, (uintptr_t)image, TRUE, TRUE, arm_step, snapshot_step) && image_unchanged() && !ea_private_base);
    memset(ea_sites, 0, sizeof(ea_sites));
    /* The load site alone is fine although the frame site is broken. */
    CHECK(ea_install_image(image, (uintptr_t)image, TRUE, FALSE, arm_step, snapshot_step));
    probe_call(va(EA_LOAD_CALL_RVA));
    CHECK(arm_runs == 1 && !check_call(0, EA_LOAD_CALL_RVA));
    memset(ea_sites, 0, sizeof(ea_sites));
    image[EA_FRAME_CALL_RVA] = 0xe8;
    put32(image + 0x80 + 24 + 56, EA_IMAGE_SIZE - 0x1000); take_copy();
    CHECK(!ea_install_image(image, (uintptr_t)image, FALSE, TRUE, arm_step, snapshot_step) && image_unchanged());
    CHECK(!ea_install_image(image, (uintptr_t)image + 0x10000, FALSE, TRUE, arm_step, snapshot_step) && image_unchanged());
    printf("PASS refuse: wrong target, wrong opcode, wrong image size or base refuse without a change; "
           "one request is all or nothing\n");
    return 0;
}

/* ---- snapshot ---- */
static BYTE *cw;
/* The synthetic CW3220 data block's region table, read as the proxy reads it. */
static ea_heap cw_heap_table;
static const ea_heap *cw_heap(void) { ea_heap_from_cw(cw, &cw_heap_table); return &cw_heap_table; }
typedef struct fx_heap { BYTE *r0, *r1; uint32_t b[5]; } fx_heap;
/* Region 0: b0 (in use, 0x20: pointer to b1+4 at 0, unaligned image pointer
 * at 5), b1 (in use, 0x30, text), b2 (free, 0x18, stale bytes `stale`), b3
 * (in use, 0x10). Region 1: b4 (in use, 0x20, pointer to b0). The MECH pool
 * pointer 0x4a9b7a (unaligned) points to b1. `mark` is the byte at b1+0x18;
 * it is 0 in the heaps compared against, so every 4-byte window over it there
 * reads 0 and no pointer class (the "foreign" one depends on the process's
 * memory map) can cover a changed mark. */
static void block(BYTE *p, uint32_t size, BOOL in_use) { put32(p - 4, size | (in_use ? 1u : 0u)); put32(p + size - 8, size | (in_use ? 1u : 0u)); }
static int make_heap(fx_heap *h, BYTE stale, BYTE mark) {
    h->r0 = VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    h->r1 = VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(h->r0 && h->r1);
    const uint32_t sizes[4] = {0x20, 0x30, 0x18, 0x10};
    put32(h->r0, 1);
    BYTE *p = h->r0 + 8;
    for (unsigned i = 0; i < 4; ++i) { h->b[i] = (uint32_t)(uintptr_t)p; block(p, sizes[i], i != 2); p += sizes[i]; }
    put32(p - 4, 1);   /* end marker */
    put32(h->r1, 1); h->b[4] = (uint32_t)(uintptr_t)(h->r1 + 8); block(h->r1 + 8, 0x20, TRUE); put32(h->r1 + 8 + 0x20 - 4, 1);
    put32((BYTE *)(uintptr_t)h->b[0], h->b[1] + 4);
    put32((BYTE *)(uintptr_t)h->b[0] + 5, va(0x9b7a));
    memcpy((BYTE *)(uintptr_t)h->b[1], "MECH pool A", 12); ((BYTE *)(uintptr_t)h->b[1])[0x18] = mark;
    memset((BYTE *)(uintptr_t)h->b[2], stale, 0x10);
    put32((BYTE *)(uintptr_t)h->b[4] + 8, h->b[0]);
    put32(cw + EA_CW_REGION_COUNT, 2);
    put32(cw + EA_CW_REGION_TABLE, (uint32_t)(uintptr_t)h->r0); put32(cw + EA_CW_REGION_TABLE + 4, 8 + 0x20 + 0x30 + 0x18 + 0x10);
    put32(cw + EA_CW_REGION_TABLE + 8, (uint32_t)(uintptr_t)h->r1); put32(cw + EA_CW_REGION_TABLE + 12, 8 + 0x20);
    put32(at_va(0x4a9b7a), h->b[1]);
    return 0;
}
static BYTE *read_file(const WCHAR *path, DWORD *size) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    *size = GetFileSize(f, NULL);
    BYTE *data = malloc(*size ? *size : 1);
    DWORD got = 0;
    BOOL ok = data && ReadFile(f, data, *size, &got, NULL) && got == *size;
    CloseHandle(f);
    return ok ? data : NULL;
}
static WCHAR tmp_dir[MAX_PATH];
static void tmp_path(WCHAR *out, const WCHAR *name) { swprintf(out, MAX_PATH, L"%ls\\%ls", tmp_dir, name); }

/* Parses the version-2 memory map at `at`: committed regions with
 * their allocation base, protection and type, then the modules of the image
 * allocations. `private_region` must lie in a committed MEM_PRIVATE region
 * and kernel32.dll must be listed with its size of image. Returns the end
 * offset or 0. */
static uint32_t check_map(const BYTE *d, DWORD size, uint32_t at, uint32_t private_region) {
    if (at + 4 > size) return 0;
    uint32_t count = ea_u32(d + at), found = 0, previous = 0;
    at += 4;
    if (!count || at + 20u * count > size) return 0;
    for (uint32_t k = 0; k < count; ++k, at += 20) {
        uint32_t base = ea_u32(d + at), length = ea_u32(d + at + 4), type = ea_u32(d + at + 16);
        if (!length || base < previous) return 0;
        previous = base + length;
        if (base <= private_region && private_region - base < length && type == MEM_PRIVATE) found = 1;
    }
    if (!found || at + 4 > size) return 0;
    uint32_t modules = ea_u32(d + at), kernel32 = 0;
    at += 4;
    for (uint32_t k = 0; k < modules; ++k) {
        if (at + 12 > size) return 0;
        uint32_t base = ea_u32(d + at), image = ea_u32(d + at + 4), length = ea_u32(d + at + 8);
        at += 12;
        if (length > 1024 || at + length > size) return 0;
        if (length == 12 && !_strnicmp((const char *)d + at, "kernel32.dll", 12) &&
            base == (uint32_t)(uintptr_t)GetModuleHandleW(L"kernel32.dll") && image) kernel32 = 1;
        at += length;
    }
    return kernel32 ? at : 0;
}

/* Parses the fixed part; returns the offset of the heap part or 0. */
static uint32_t check_fixed(const BYTE *d, DWORD size, uint32_t want_flags, unsigned extra) {
    if (size < 32 || memcmp(d, "ES2SNAP1", 8) || ea_u32(d + 8) != 2 || ea_u32(d + 12) != want_flags) return 0;
    if (ea_u32(d + 16) != 0x2a || ea_u32(d + 20) != (uint32_t)(uintptr_t)image || ea_u32(d + 24) != EA_IMAGE_SIZE) return 0;
    if (ea_u32(d + 28) != EA_FIXED_COUNT + extra) return 0;
    uint32_t at = 32;
    for (unsigned i = 0; i < EA_FIXED_COUNT + extra; ++i) {
        uint32_t v = ea_u32(d + at), length = ea_u32(d + at + 4), readable = ea_u32(d + at + 8);
        if (i < EA_FIXED_COUNT && (v != ea_fixed_regions[i].va || length != ea_fixed_regions[i].length)) return 0;
        if (readable != 1 || at + 12 + length > size || memcmp(d + at + 12, at_va(v), length)) return 0;
        at += 12 + length;
    }
    return at;
}

static int run_script(const char *python, const char *script, const WCHAR *a, const WCHAR *b) {
    char pa[MAX_PATH * 3], pb[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, a, -1, pa, sizeof(pa), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, b, -1, pb, sizeof(pb), NULL, NULL);
    const char *args[] = {python, "-B", script, "compare", pa, pb, NULL};
    fflush(stdout); fflush(stderr);
    intptr_t result = _spawnv(_P_WAIT, python, args);
    return result < 0 ? -1 : (int)result;
}
static int resolve(const char *name, char path[4096]) {
    FILE *f = fopen(name, "rb");
    if (f) { fclose(f); snprintf(path, 4096, "%s", name); return 1; }
    const char *manifest = getenv("RUNFILES_MANIFEST_FILE");
    f = manifest ? fopen(manifest, "rb") : NULL;
    if (!f) return 0;
    char key[1024], line[8192]; snprintf(key, sizeof(key), "_main/%s ", name);
    while (fgets(line, sizeof(line), f)) if (!strncmp(line, key, strlen(key))) {
        line[strcspn(line, "\r\n")] = 0;
        snprintf(path, 4096, "%s", line + strlen(key)); fclose(f); return 1;
    }
    fclose(f); return 0;
}

static int snapshot_case(const char *python, const char *script_name) {
    CHECK(!make_image());
    const WCHAR *t = _wgetenv(L"TEST_TMPDIR");
    DWORD n = t ? (DWORD)wcslen(t) : GetTempPathW(MAX_PATH, tmp_dir);
    CHECK(n && n < MAX_PATH - 64);
    if (t) wcscpy(tmp_dir, t);
    else if (tmp_dir[n - 1] == L'\\') tmp_dir[n - 1] = 0;
    cw = VirtualAlloc(NULL, EA_CW_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(cw);
    put32(at_va(0x004d24bcu + 0xbf), 0x2a);   /* frame counter */
    fx_heap a, b, c;
    CHECK(!make_heap(&a, 0x11, 0));
    tmp_path(snapshot_path, L"a.snap"); DeleteFileW(snapshot_path);
    snapshot_cw = cw;
    step_original_rel = ea_u32(image + EA_FRAME_CALL_RVA + 1);
    take_copy();
    CHECK(ea_install_image(image, (uintptr_t)image, FALSE, TRUE, arm_step, snapshot_step));
    probe_call(va(EA_FRAME_CALL_RVA));
    CHECK(snapshot_runs == 1 && snapshot_ok && target_hits[1] == 1 && !check_call(1, EA_FRAME_CALL_RVA) && !check_private_stack());
    CHECK(image_unchanged());
    probe_call(va(EA_FRAME_CALL_RVA));
    CHECK(snapshot_runs == 1 && target_hits[1] == 2);
    /* The layout: fixed regions, then the heap walk. */
    DWORD size = 0; BYTE *d = read_file(snapshot_path, &size);
    CHECK(d);
    uint32_t at = check_fixed(d, size, 1u | 16u, 0);
    CHECK(at && ea_u32(d + at) == 2);
    at += 4;
    CHECK(ea_u32(d + at) == (uint32_t)(uintptr_t)a.r0 && ea_u32(d + at + 4) == 0x80 && ea_u32(d + at + 8) == 1 && ea_u32(d + at + 12) == 4);
    at += 16;
    const uint32_t sizes[4] = {0x20, 0x30, 0x18, 0x10};
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(ea_u32(d + at) == a.b[i] && ea_u32(d + at + 4) == sizes[i] && ea_u32(d + at + 8) == (i == 2 ? 0u : 3u));
        at += 12;
        if (i != 2) { CHECK(!memcmp(d + at, (BYTE *)(uintptr_t)a.b[i], sizes[i] - 8)); at += sizes[i] - 8; }
    }
    CHECK(ea_u32(d + at) == (uint32_t)(uintptr_t)a.r1 && ea_u32(d + at + 12) == 1 && ea_u32(d + at + 16) == a.b[4]);
    at += 16 + 12 + 0x18;
    at = check_map(d, size, at, a.b[1]);
    CHECK(at && at == size);
    free(d);
    /* The same heap elsewhere (other stale bytes in the free block) and one
     * with a changed byte; an existing path is never overwritten. */
    CHECK(!ea_snapshot_write(snapshot_path, cw_heap(), NULL, 0, (uint64_t)1 << 30, FALSE));
    WCHAR pb[MAX_PATH], pc[MAX_PATH];
    CHECK(!make_heap(&b, 0x22, 0));
    tmp_path(pb, L"b.snap"); DeleteFileW(pb);
    CHECK(ea_snapshot_write(pb, cw_heap(), NULL, 0, (uint64_t)1 << 30, FALSE));
    CHECK(!make_heap(&c, 0x22, 'C'));
    tmp_path(pc, L"c.snap"); DeleteFileW(pc);
    CHECK(ea_snapshot_write(pc, cw_heap(), NULL, 0, (uint64_t)1 << 30, FALSE));
    CHECK(a.b[1] != b.b[1] && b.b[1] != c.b[1]);
    /* No CW3220: flag 2; a corrupt region (footer): flag 4, region status 0;
     * a byte limit: flag 8, later blocks without contents. */
    WCHAR pd[MAX_PATH]; tmp_path(pd, L"d.snap"); DeleteFileW(pd);
    CHECK(ea_snapshot_write(pd, NULL, NULL, 0, (uint64_t)1 << 30, TRUE));
    d = read_file(pd, &size); CHECK(d);
    at = check_fixed(d, size, 2u | 16u | 32u, 0); CHECK(at && ea_u32(d + at) == 0);
    CHECK(check_map(d, size, at + 4, (uint32_t)(uintptr_t)cw) == size); free(d);
    put32((BYTE *)(uintptr_t)c.b[1] + 0x30 - 8, 0x31 ^ 0x40);
    tmp_path(pd, L"e.snap"); DeleteFileW(pd);
    CHECK(ea_snapshot_write(pd, cw_heap(), NULL, 0, (uint64_t)1 << 30, FALSE));
    d = read_file(pd, &size); CHECK(d);
    at = check_fixed(d, size, 1u | 4u | 16u, 0);
    CHECK(at && ea_u32(d + at) == 2 && ea_u32(d + at + 4 + 8) == 0 && ea_u32(d + at + 4 + 12) == 0); free(d);
    put32((BYTE *)(uintptr_t)c.b[1] + 0x30 - 8, 0x31);
    tmp_path(pd, L"f.snap"); DeleteFileW(pd);
    CHECK(ea_snapshot_write(pd, cw_heap(), NULL, 0, 0x18 + 0x28, FALSE));
    d = read_file(pd, &size); CHECK(d);
    at = check_fixed(d, size, 1u | 8u | 16u, 0);
    CHECK(at); at += 4 + 16;
    CHECK(ea_u32(d + at + 8) == 3); at += 12 + 0x18;
    CHECK(ea_u32(d + at + 8) == 3); at += 12 + 0x28;
    CHECK(ea_u32(d + at + 8) == 0); at += 12;            /* free */
    CHECK(ea_u32(d + at + 8) == 1); free(d);             /* in use, over the limit: no contents */
    /* The INI path: [snapshot] path, extra regions, max_bytes_mib; a bad extra refuses. */
    WCHAR ini[MAX_PATH], pg[MAX_PATH], text[MAX_PATH + 64];
    tmp_path(ini, L"es2-oracle.ini"); tmp_path(pg, L"g.snap"); DeleteFileW(pg);
    CHECK(WritePrivateProfileStringW(L"snapshot", L"path", pg, ini));
    CHECK(WritePrivateProfileStringW(L"snapshot", L"extra", L"0x4aaad0:4, 0x401000:0x10", ini));
    CHECK(ea_snapshot_from_config(ini, cw_heap(), FALSE));
    d = read_file(pg, &size); CHECK(d);
    at = check_fixed(d, size, 1u | 16u, 2);
    CHECK(at); free(d);
    swprintf(text, MAX_PATH + 64, L"%ls", L"0x4aaad0:4,0x3ff000:4");
    CHECK(WritePrivateProfileStringW(L"snapshot", L"extra", text, ini));
    DeleteFileW(pg);
    CHECK(!ea_snapshot_from_config(ini, cw_heap(), FALSE) && GetFileAttributesW(pg) == INVALID_FILE_ATTRIBUTES);
    ea_region parsed[EA_EXTRA_MAX]; unsigned count = 99;
    CHECK(ea_parse_extra(L"", parsed, &count) && count == 0);
    CHECK(!ea_parse_extra(L"0x4aaad0", parsed, &count) && !ea_parse_extra(L"0x4aaad0:0", parsed, &count) &&
          !ea_parse_extra(L"0x6d7ffc:8", parsed, &count) && !ea_parse_extra(L"0x4aaad0:4,", parsed, &count));
    if (python && script_name) {
        char script[4096];
        CHECK(resolve(script_name, script));
        CHECK(run_script(python, script, snapshot_path, pb) == 0);   /* relocated heap: identical */
        CHECK(run_script(python, script, snapshot_path, pc) == 1);   /* one byte in b1 */
        printf("PASS snapshot script: relocated heap identical, changed byte different\n");
    }
    printf("PASS snapshot: written once at the first frame call on the private stack; fixed regions, heap walk "
           "(free blocks without contents), flags no-heap/walk-error/truncated/early, extra regions, no overwrite\n");
    return 0;
}

/* ---- the own runtime's heap ---- */
static uint32_t own_read32(void *context, uint32_t address) { (void)context; return ea_u32((const BYTE *)(uintptr_t)address); }
typedef void *(__cdecl *own_calloc_fn)(unsigned, unsigned);
typedef void (__cdecl *own_free_fn)(void *);
static int own_case(const char *python, const char *script_name, const char *dll_name) {
    CHECK(!make_image());
    const WCHAR *t = _wgetenv(L"TEST_TMPDIR");
    DWORD n = t ? (DWORD)wcslen(t) : GetTempPathW(MAX_PATH, tmp_dir);
    CHECK(n && n < MAX_PATH - 64);
    if (t) wcscpy(tmp_dir, t);
    else if (tmp_dir[n - 1] == L'\\') tmp_dir[n - 1] = 0;
    cw = VirtualAlloc(NULL, EA_CW_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(cw);
    put32(at_va(0x004d24bcu + 0xbf), 0x2a);   /* frame counter */
    ea_image = image; ea_image_base = (uintptr_t)image;
    /* The DLL under its own file name, so that the proxy's lookup finds it. */
    char dll[4096];
    CHECK(dll_name && resolve(dll_name, dll));
    HMODULE module = LoadLibraryA(dll);
    CHECK(module && GetModuleHandleW(L"cw3220.DLL") == module);
    const es2_own_runtime_info *info = ea_own_runtime(GetModuleHandleW(L"cw3220.DLL"));
    CHECK(info && info->mode == ES2_OWN_MODE_OWN && info->heap);
    CHECK(!ea_own_runtime(GetModuleHandleW(L"kernel32.dll")) && !ea_own_runtime(NULL));
    own_calloc_fn own_calloc = (own_calloc_fn)(void *)GetProcAddress(module, "_calloc");
    own_free_fn own_free = (own_free_fn)(void *)GetProcAddress(module, "_free");
    CHECK(own_calloc && own_free);
    /* The MECH text block reached from 0x4a9b7a, a block pointing into it, a freed block. */
    BYTE *mech = own_calloc(1, 0x28), *link = own_calloc(1, 0x18), *gone = own_calloc(1, 0x40);
    CHECK(mech && link && gone);
    memcpy(mech, "MECH pool A", 12);
    put32(link, (uint32_t)(uintptr_t)mech + 4);
    own_free(gone);
    put32(at_va(0x4a9b7a), (uint32_t)(uintptr_t)mech);
    ea_heap heap;
    CHECK(ea_heap_from_own(info, &heap) && heap.kind == EA_HEAP_OWN && heap.count == info->heap->region_count && heap.count >= 1);
    for (uint32_t r = 0; r < heap.count; ++r) CHECK(heap.base[r] == info->heap->region_base[r] && heap.size[r] == info->heap->region_size[r]);
    int32_t walked = es2_heap_walk(info->heap, own_read32, NULL, NULL, NULL);
    CHECK(walked > 0);
    WCHAR po[MAX_PATH]; tmp_path(po, L"own.snap"); DeleteFileW(po);
    CHECK(ea_snapshot_write(po, &heap, NULL, 0, (uint64_t)1 << 30, FALSE));
    /* Layout: flags heap|complete|own, every region walked, the blocks as the model walks them. */
    DWORD size = 0; BYTE *d = read_file(po, &size);
    CHECK(d);
    uint32_t at = check_fixed(d, size, 1u | 16u | 128u, 0);
    CHECK(at && ea_u32(d + at) == heap.count);
    at += 4;
    int32_t blocks = 0;
    BOOL mech_seen = FALSE, link_seen = FALSE;
    for (uint32_t r = 0; r < heap.count; ++r) {
        CHECK(ea_u32(d + at) == heap.base[r] && ea_u32(d + at + 4) == heap.size[r] && ea_u32(d + at + 8) == 1);
        uint32_t count = ea_u32(d + at + 12);
        at += 16;
        for (uint32_t k = 0; k < count; ++k) {
            uint32_t p = ea_u32(d + at), s = ea_u32(d + at + 4), f = ea_u32(d + at + 8);
            at += 12;
            CHECK(f == 0 || f == 3);
            if (p == (uint32_t)(uintptr_t)mech) { CHECK(f == 3 && s == 0x30 && !memcmp(d + at, mech, 0x28)); mech_seen = TRUE; }
            if (p == (uint32_t)(uintptr_t)link) { CHECK(f == 3 && s == 0x20 && ea_u32(d + at) == (uint32_t)(uintptr_t)mech + 4); link_seen = TRUE; }
            if (p == (uint32_t)(uintptr_t)gone) CHECK(f == 0);
            if (f & 2u) at += s - 8;
            ++blocks;
        }
    }
    CHECK(mech_seen && link_seen && blocks == walked);
    at = check_map(d, size, at, (uint32_t)(uintptr_t)mech);
    CHECK(at && at == size);
    free(d);
    if (python && script_name) {
        char script[4096];
        CHECK(resolve(script_name, script));
        /* The same objects on the synthetic CW3220 heap (make_heap points 0x4a9b7a at its MECH block). */
        WCHAR pc[MAX_PATH], pd[MAX_PATH];
        fx_heap a;
        CHECK(!make_heap(&a, 0x11, 0));
        tmp_path(pc, L"cw.snap"); DeleteFileW(pc);
        CHECK(ea_snapshot_write(pc, cw_heap(), NULL, 0, (uint64_t)1 << 30, FALSE));
        CHECK(run_script(python, script, pc, po) == 0);    /* own runtime against CW3220: identical */
        CHECK(run_script(python, script, po, po) == 0);
        put32(at_va(0x4a9b7a), (uint32_t)(uintptr_t)mech);
        mech[0x18] = 'C';                  /* the mark byte of make_heap: zero neighbours, never a pointer */
        tmp_path(pd, L"own-c.snap"); DeleteFileW(pd);
        CHECK(ea_heap_from_own(info, &heap) && ea_snapshot_write(pd, &heap, NULL, 0, (uint64_t)1 << 30, FALSE));
        CHECK(run_script(python, script, pc, pd) == 1);    /* one byte in the MECH block */
        printf("PASS own script: the own runtime's heap against the same objects on CW3220's identical, a changed byte different\n");
    }
    printf("PASS own: the own cw3220.dll found by its module name through es2_own_runtime; its %u region(s) walked "
           "(flag 128), %d blocks as es2_heap_walk counts them, the in-use blocks with their contents, the freed one without\n",
           heap.count, (int)walked);
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
    uint32_t pe = ea_u32(bytes + 0x3c);
    CHECK(ea_u32(bytes + pe + 24 + 56) == EA_IMAGE_SIZE && ea_u32(bytes + pe + 24 + 28) == EA_LINK_BASE);
    uint32_t headers = ea_u32(bytes + pe + 24 + 60);
    unsigned sections = bytes[pe + 6] | bytes[pe + 7] << 8, optional = bytes[pe + 20] | bytes[pe + 21] << 8;
    BYTE *map = calloc(1, EA_IMAGE_SIZE);
    CHECK(map);
    memcpy(map, bytes, headers);
    uint32_t code_rva = 0, code_size = 0;
    for (unsigned s = 0; s < sections; ++s) {
        const BYTE *h = bytes + pe + 24 + optional + 40 * s;
        uint32_t rva = ea_u32(h + 12), raw = ea_u32(h + 16), offset = ea_u32(h + 20);
        if (raw && rva < EA_IMAGE_SIZE) memcpy(map + rva, bytes + offset, raw < EA_IMAGE_SIZE - rva ? raw : EA_IMAGE_SIZE - rva);
        if (!code_size && (ea_u32(h + 36) & 0x20u)) { code_rva = rva; code_size = raw; }
    }
    CHECK(code_size);
    CHECK(ea_site_valid(map, EA_LOAD_CALL_RVA, EA_LOAD_TARGET_RVA) && ea_site_valid(map, EA_FRAME_CALL_RVA, EA_FRAME_TARGET_RVA));
    CHECK(map[EA_LOAD_TARGET_RVA] == 0x55 && !memcmp(map + EA_FRAME_TARGET_RVA, "\x55\x8b\xec", 3));
    /* Single callers: the only `call rel32` to each target in the code section. */
    unsigned load_calls = 0, frame_calls = 0;
    for (uint32_t r = code_rva; r + 5 <= code_rva + code_size; ++r) {
        if (map[r] != 0xe8) continue;
        uint32_t target = r + 5 + ea_u32(map + r + 1);
        load_calls += target == EA_LOAD_TARGET_RVA;
        frame_calls += target == EA_FRAME_TARGET_RVA;
    }
    CHECK(load_calls == 1 && frame_calls == 1);
    /* No registered hook prologue covers a site's bytes. */
    for (unsigned i = 0; i < sizeof(registry_rvas) / sizeof(registry_rvas[0]); ++i) {
        uint32_t r = registry_rvas[i];
        CHECK(!(r + 16 > EA_LOAD_CALL_RVA && r < EA_LOAD_CALL_RVA + 5));
        CHECK(!(r + 16 > EA_FRAME_CALL_RVA && r < EA_FRAME_CALL_RVA + 5));
    }
    printf("PASS reference: call 0x461608 at 0x45f42b and call 0x45f570 at 0x45f487 (each the only call of its "
           "target), target prologues, no hook prologue at the sites (%u hooks)\n",
           (unsigned)(sizeof(registry_rvas) / sizeof(registry_rvas[0])));
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "off")) return off_case();
    if (!strcmp(argv[1], "arm")) return arm_case();
    if (!strcmp(argv[1], "refuse")) return refuse_case();
    if (!strcmp(argv[1], "snapshot")) return snapshot_case(argc > 3 ? argv[2] : NULL, argc > 3 ? argv[3] : NULL);
    if (!strcmp(argv[1], "own")) return own_case(argc > 3 ? argv[2] : NULL, argc > 3 ? argv[3] : NULL, argc > 4 ? argv[4] : NULL);
    if (!strcmp(argv[1], "reference")) return reference_case();
    return 2;
}
