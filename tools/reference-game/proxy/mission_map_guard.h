// VSHELL 1.11 mission-map lifetime repair. Only a loaded process is changed;
// the original executable file and its reference hash remain untouched.
#ifndef ES2_MISSION_MAP_GUARD_H
#define ES2_MISSION_MAP_GUARD_H
#include <stdint.h>

#ifndef MAP_GUARD_BASE
#define MAP_GUARD_BASE ((uintptr_t)0x00400000)
#endif
#define MAP_GUARD_IMAGE_SIZE 0x001ea000u
#define MAP_GUARD_CALL_RVA 0x0004509eu
#define MAP_GUARD_CLEANUP_RVA 0x000446d1u
#define MAP_GUARD_PANEL_RVA 0x0008d8c4u
#define MAP_GUARD_PANEL_VTABLE ((uintptr_t)0x0046edcc)

typedef void (__cdecl *MapGuardCleanup)(void);
static MapGuardCleanup map_guard_original_cleanup;
static BYTE *map_guard_image;
static LONG map_guard_state;

struct MapGuardSignature { DWORD rva; const BYTE *bytes; SIZE_T size; };
static const BYTE map_guard_cleanup_signature[] = {
    0xff,0x35,0xf4,0xd8,0x48,0x00,0xe8,0xa6,0x7a,0xfc,0xff,0x59,0x83,0x3d,0x5c,0x80,
    0x47,0x00,0x00,0x74,0x1f,0xa1,0xa0,0xdf,0x5d,0x00,0xff,0x00,0x6a,0x03,0xff,0x35,
    0x5c,0x80,0x47,0x00,0xe8,0x36,0x0b,0x01,0x00,0x83,0xc4,0x08,0x33,0xd2,0x89,0x15,
    0x5c,0x80,0x47,0x00,0x8b,0x0d,0x60,0x80,0x47,0x00,0x85,0xc9,0x74,0x1f,0xa1,0xa0,
    0xdf,0x5d,0x00,0xff,0x00,0x6a,0x03,0xff,0x35,0x60,0x80,0x47,0x00,0xe8,0x0d,0x0b,
    0x01,0x00,0x83,0xc4,0x08,0x33,0xd2,0x89,0x15,0x60,0x80,0x47,0x00,0xc3
};
static const BYTE map_guard_constructor_signature[] = {
    0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0xff,0x75,0x1c,0xff,0x75,0x18,0xff,0x75,0x10,
    0xff,0x75,0x0c,0x53,0xe8,0x43,0xf0,0xff,0xff,0x83,0xc4,0x14,0xc7,0x43,0x01,0xcc,
    0xed,0x46,0x00,0x8b,0x45,0x14,0x89,0x43,0x5d,0x33,0xd2,0x89,0x53,0x51,0x53,0xe8,
    0x41,0x3e,0x01,0x00,0x59,0x33,0xc9,0x89,0x4b,0x59,0x89,0x4b,0x55,0x8b,0xc3,0x5b,
    0x5d,0xc3
};
static const BYTE map_guard_draw_signature[] = {
    0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x53,0xe8,0xaa,0x3e,0x01,0x00,0x59,0x8b,0x43,
    0x5d,0x85,0xc0,0x74,0x11,0x8d,0x43,0x55,0x6a,0x00,0x50,0xff,0x73,0x5d,0xe8,0xa3,
    0x7a,0x04,0x00,0x83,0xc4,0x0c,0x6a,0x00,0x53,0xe8,0x86,0xef,0xff,0xff,0x83,0xc4,
    0x08,0x5b,0x5d,0xc3
};
static const BYTE map_guard_assignment_signature[] = {
    0x8b,0x15,0x5c,0x80,0x47,0x00,0x8b,0x4a,0x08,0x8b,0x01,0x8b,0x15,0xc4,0xd8,0x48,
    0x00,0x89,0x42,0x5d,0x8b,0x0d,0xc4,0xd8,0x48,0x00,0x51,0x8b,0x41,0x01,0xff,0x50,
    0x04,0x59
};
static const BYTE map_guard_call_signature[] = {
    0x66,0xc7,0x05,0x44,0x79,0x47,0x00,0x00,0x00,0xe8,0x2e,0xf6,0xff,0xff,0xe8,0xfc,
    0xfe,0xff,0xff
};
static const struct MapGuardSignature map_guard_signatures[] = {
    {MAP_GUARD_CLEANUP_RVA, map_guard_cleanup_signature, sizeof(map_guard_cleanup_signature)},
    {0x0000bc48, map_guard_constructor_signature, sizeof(map_guard_constructor_signature)},
    {0x0000bd22, map_guard_draw_signature, sizeof(map_guard_draw_signature)},
    {0x00044609, map_guard_assignment_signature, sizeof(map_guard_assignment_signature)},
    {0x00045095, map_guard_call_signature, sizeof(map_guard_call_signature)}
};

static BOOL map_guard_accessible(const void *pointer, SIZE_T size, BOOL writable) {
    uintptr_t cursor = (uintptr_t)pointer;
    if (!cursor || size > UINTPTR_MAX - cursor) return FALSE;
    uintptr_t end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery((void *)cursor, &region, sizeof(region)) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return FALSE;
        DWORD protection = region.Protect & 0xff;
        if (writable) {
            if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
                protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return FALSE;
        } else if (protection != PAGE_READONLY && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
                   protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
                   protection != PAGE_EXECUTE_WRITECOPY) return FALSE;
        uintptr_t next = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (next <= cursor) return FALSE;
        cursor = next;
    }
    return TRUE;
}

// The replaced x86 call has no arguments and no return value. Standard cdecl
// preserves EBX/ESI/EDI/EBP and the caller's stack exactly as the original did.
static void __cdecl mission_map_cleanup_guard(void) {
    BYTE *panel;
    memcpy(&panel, map_guard_image + MAP_GUARD_PANEL_RVA, sizeof(panel));
    if (panel && map_guard_accessible(panel, 0x61, TRUE)) {
        uintptr_t vtable;
        memcpy(&vtable, panel + 1, sizeof(vtable));
        if (vtable == MAP_GUARD_PANEL_VTABLE) {
            DWORD empty = 0;
            // Packed original object: use memcpy for its unaligned DWORD.
            memcpy(panel + 0x5d, &empty, sizeof(empty));
            log_event("mission_map_reference_cleared");
        } else log_event("mission_map_panel_mismatch");
    } else if (panel) log_event("mission_map_panel_unwritable");
    map_guard_original_cleanup();
}

// The public entry point below is called once by proxy startup initialization.
// Keeping validation separate lets the own-code fixture use a synthetic image.
static BOOL install_mission_map_guard_image(BYTE *image) {
    LONG previous = InterlockedCompareExchange(&map_guard_state, 2, 0);
    if (previous) return previous == 1;
    if ((uintptr_t)image != MAP_GUARD_BASE || sizeof(void *) != 4 ||
        !map_guard_accessible(image, 4096, FALSE)) goto refused;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < (LONG)sizeof(*dos) ||
        dos->e_lfanew > 4096 - (LONG)sizeof(IMAGE_NT_HEADERS32)) goto refused;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.ImageBase != MAP_GUARD_BASE || nt->OptionalHeader.SizeOfImage != MAP_GUARD_IMAGE_SIZE)
        goto refused;
    for (unsigned i = 0; i < sizeof(map_guard_signatures) / sizeof(map_guard_signatures[0]); ++i) {
        const struct MapGuardSignature *signature = &map_guard_signatures[i];
        if (!map_guard_accessible(image + signature->rva, signature->size, FALSE) ||
            memcmp(image + signature->rva, signature->bytes, signature->size)) goto refused;
    }
    if (!map_guard_accessible(image + MAP_GUARD_PANEL_RVA, sizeof(DWORD), TRUE)) goto refused;
    BYTE *site = image + MAP_GUARD_CALL_RVA;
    BYTE original[5], replacement[5] = {0xe8, 0, 0, 0, 0};
    memcpy(original, site, sizeof(original));
    DWORD displacement = (DWORD)(uintptr_t)mission_map_cleanup_guard - (DWORD)(uintptr_t)(site + 5);
    memcpy(replacement + 1, &displacement, sizeof(displacement));
    DWORD protection, ignored;
    if (!VirtualProtect(site, sizeof(replacement), PAGE_EXECUTE_READWRITE, &protection)) goto refused;
    map_guard_image = image;
    map_guard_original_cleanup = (MapGuardCleanup)(image + MAP_GUARD_CLEANUP_RVA);
    memcpy(site, replacement, sizeof(replacement));
    if (!FlushInstructionCache(GetCurrentProcess(), site, sizeof(replacement))) {
        // Still writable: restore bytes before attempting protection changes.
        memcpy(site, original, sizeof(original));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(original));
        VirtualProtect(site, sizeof(original), protection, &ignored);
        goto refused;
    }
    if (!VirtualProtect(site, sizeof(replacement), protection, &ignored)) {
        // This five-byte site occupies one page. A failed restore leaves it
        // writable, so rollback must not depend on another writable transition.
        memcpy(site, original, sizeof(original));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(original));
        VirtualProtect(site, sizeof(original), protection, &ignored);
        goto refused;
    }
    InterlockedExchange(&map_guard_state, 1);
    log_event("mission_map_guard_installed");
    return TRUE;
refused:
    InterlockedExchange(&map_guard_state, -1);
    log_event("mission_map_guard_refused");
    return FALSE;
}

static BOOL install_mission_map_guard(void) {
    if (!GetPrivateProfileIntW(L"compat", L"mission_map_lifetime", 0, config_path)) return FALSE;
    WCHAR path[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return FALSE;
    WCHAR *name = wcsrchr(path, L'\\');
    if (!name || _wcsicmp(name + 1, L"VSHELL.EXE")) return FALSE;
    return install_mission_map_guard_image((BYTE *)GetModuleHandleW(NULL));
}
#endif
