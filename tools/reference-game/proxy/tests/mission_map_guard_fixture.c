// Only synthetic executable memory and own-code stubs are executed here.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static WCHAR config_path[MAX_PATH];
static unsigned installed_events, cleared_events;
static BOOL fail_first_flush, fail_first_restore;
static unsigned guard_protection_calls, guard_flush_calls;
static BOOL guard_test_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous) {
    ++guard_protection_calls;
    if (fail_first_restore && guard_protection_calls == 2) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return VirtualProtect(address, size, protection, previous);
}
static BOOL guard_test_flush(HANDLE process, LPCVOID address, SIZE_T size) {
    ++guard_flush_calls;
    if (fail_first_flush && guard_flush_calls == 1) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return FlushInstructionCache(process, address, size);
}
static void log_event(const char *event) {
    if (!strcmp(event, "mission_map_guard_installed")) ++installed_events;
    if (!strcmp(event, "mission_map_reference_cleared")) ++cleared_events;
}
// Test-only placement: Windows already reserves low process memory in this
// fixture. The production header's default remains the verified 0x00400000.
// Absolute reference instruction bytes are compared, never executed here.
#define MAP_GUARD_BASE ((uintptr_t)0x30000000)
#define VirtualProtect guard_test_protect
#define FlushInstructionCache guard_test_flush
#include "../mission_map_guard.h"
#undef VirtualProtect
#undef FlushInstructionCache

static BYTE panel[0x61];
static unsigned cleanup_calls;
static DWORD expected_at_cleanup;
static int ordering_failed;
static void __cdecl observe_cleanup(void) {
    DWORD bitmap;
    memcpy(&bitmap, panel + 0x5d, 4);
    if (bitmap != expected_at_cleanup) ordering_failed = 1;
    ++cleanup_calls;
}

static void put_pointer(BYTE *target, DWORD value) { memcpy(target, &value, 4); }

static int invoke_and_check_abi(void (__cdecl *function)(void)) {
    int valid;
    __asm__ volatile (
        "push %%ebx\n\tpush %%esi\n\tpush %%edi\n\t"
        "mov $0x12345678, %%ebx\n\tmov $0x23456789, %%esi\n\tmov $0x3456789a, %%edi\n\t"
        "call *%%eax\n\t"
        "xor %%eax, %%eax\n\t"
        "cmp $0x12345678, %%ebx\n\tjne 1f\n\t"
        "cmp $0x23456789, %%esi\n\tjne 1f\n\t"
        "cmp $0x3456789a, %%edi\n\tjne 1f\n\t"
        "inc %%eax\n1:\n\tpop %%edi\n\tpop %%esi\n\tpop %%ebx"
        : "=a"(valid) : "a"(function) : "ecx", "edx", "memory", "cc");
    return valid;
}

#define CHECK(condition, code) do { if (!(condition)) { fprintf(stderr, "FAIL %d: %s\n", code, #condition); return code; } } while (0)

int main(int argc, char **argv) {
    CHECK(argc == 2, 2);
    BYTE *image = VirtualAlloc((void *)MAP_GUARD_BASE, MAP_GUARD_IMAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!image) {
        MEMORY_BASIC_INFORMATION occupied;
        VirtualQuery((void *)MAP_GUARD_BASE, &occupied, sizeof(occupied));
        fprintf(stderr, "Synthetic range unavailable: error=%lu allocation=%p state=%lx size=%lu\n", (unsigned long)GetLastError(), occupied.AllocationBase, (unsigned long)occupied.State, (unsigned long)occupied.RegionSize);
    }
    CHECK(image == (BYTE *)MAP_GUARD_BASE, 4);
    // A non-VSHELL process must never install through the production entry.
    CHECK(!install_mission_map_guard(), 3);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.ImageBase = MAP_GUARD_BASE;
    nt->OptionalHeader.SizeOfImage = MAP_GUARD_IMAGE_SIZE;
    for (unsigned i = 0; i < sizeof(map_guard_signatures) / sizeof(map_guard_signatures[0]); ++i)
        memcpy(image + map_guard_signatures[i].rva, map_guard_signatures[i].bytes, map_guard_signatures[i].size);
    put_pointer(panel + 1, MAP_GUARD_PANEL_VTABLE);
    put_pointer(panel + 0x5d, 0xdeadbeef);
    put_pointer(image + MAP_GUARD_PANEL_RVA, (DWORD)(uintptr_t)panel);
    if (!strcmp(argv[1], "mismatch")) image[0x44609] ^= 1;
    if (!strcmp(argv[1], "image-size")) nt->OptionalHeader.SizeOfImage -= 0x1000;
    fail_first_flush = !strcmp(argv[1], "flush-failure");
    fail_first_restore = !strcmp(argv[1], "restore-failure");
    DWORD previous;
    CHECK(VirtualProtect(image + 0x1000, 0x6b000, PAGE_EXECUTE_READ, &previous), 5);
    BYTE original_call[5];
    memcpy(original_call, image + MAP_GUARD_CALL_RVA, 5);
    BOOL installed = install_mission_map_guard_image(image);
    MEMORY_BASIC_INFORMATION region;
    CHECK(VirtualQuery(image + MAP_GUARD_CALL_RVA, &region, sizeof(region)), 6);
    CHECK(region.Protect == PAGE_EXECUTE_READ, 7);
    if (strcmp(argv[1], "valid")) {
        CHECK(!installed && !installed_events, 8);
        CHECK(!memcmp(original_call, image + MAP_GUARD_CALL_RVA, 5), 9);
        CHECK(!install_mission_map_guard_image(image), 10);
        puts("PASS: mismatched image refused, call and page protection unchanged.");
        VirtualFree(image, 0, MEM_RELEASE);
        return 0;
    }
    CHECK(installed && installed_events == 1, 11);
    CHECK(install_mission_map_guard_image(image) && installed_events == 1, 12);
    CHECK(!memcmp(panel + 0x5d, "\xef\xbe\xad\xde", 4), 13);
    CHECK(map_guard_original_cleanup == (MapGuardCleanup)(image + MAP_GUARD_CLEANUP_RVA), 14);
    // After validation, replace only the synthetic cleanup body with an own
    // observer, and stop the synthetic caller immediately after its real CALL.
    CHECK(VirtualProtect(image + 0x1000, 0x6b000, PAGE_EXECUTE_READWRITE, &previous), 15);
    BYTE *stub = image + MAP_GUARD_CLEANUP_RVA;
    stub[0] = 0xe9;
    put_pointer(stub + 1, (DWORD)(uintptr_t)observe_cleanup - (DWORD)(uintptr_t)(stub + 5));
    image[MAP_GUARD_CALL_RVA + 5] = 0xc3;
    CHECK(VirtualProtect(image + 0x1000, 0x6b000, PAGE_EXECUTE_READ, &previous), 16);
    CHECK(FlushInstructionCache(GetCurrentProcess(), image, MAP_GUARD_IMAGE_SIZE), 17);
    expected_at_cleanup = 0;
    CHECK(invoke_and_check_abi((MapGuardCleanup)(image + MAP_GUARD_CALL_RVA)), 18);
    CHECK(!ordering_failed && cleanup_calls == 1 && cleared_events == 1, 19);
    // Simulate the next mission initialization. Its new bitmap remains intact
    // until a subsequent teardown, then is cleared before exactly one cleanup.
    put_pointer(panel + 0x5d, 0x12345678);
    CHECK(install_mission_map_guard_image(image), 20);
    CHECK(!memcmp(panel + 0x5d, "\x78\x56\x34\x12", 4), 21);
    CHECK(invoke_and_check_abi((MapGuardCleanup)(image + MAP_GUARD_CALL_RVA)), 22);
    CHECK(!ordering_failed && cleanup_calls == 2 && cleared_events == 2, 23);
    // A mismatched object must be left intact while original cleanup runs.
    put_pointer(panel + 1, 0x11111111);
    put_pointer(panel + 0x5d, 0x12345678);
    expected_at_cleanup = 0x12345678;
    CHECK(invoke_and_check_abi((MapGuardCleanup)(image + MAP_GUARD_CALL_RVA)), 24);
    CHECK(!ordering_failed && cleanup_calls == 3 && cleared_events == 2, 25);
    puts("PASS: real near-CALL hook; clear before one cleanup; cdecl preserved; idempotence; next-mission pointer; mismatched panel retained; protection restored.");
    VirtualFree(image, 0, MEM_RELEASE);
    return 0;
}
