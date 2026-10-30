/* DBSIM's LoadLibraryA("BWCC32.DLL") answered with failure.
 * Contract and evidence: docs/reference-game/dll_imports.md, sections "BWCC32: loaded and never used" and
 * "The BWCC32 switch".
 *
 * DBSIM's window-class registration 0x4650e8 (called by WinMain 0x46538c)
 * pushes the string "BWCC32.DLL" (0x4a0859) and calls the LoadLibraryA thunk
 * 0x496904 (`jmp [0x6cc3f8]`, KERNEL32's IAT slot) at 0x465140; the handle in
 * EAX is overwritten unread (`lea edx, [ebp-0x28]` follows), no
 * GetProcAddress or FreeLibrary uses it, and DBSIM instantiates no Borland
 * control. The switch replaces only that call's rel32 so that the call enters
 * bw_refuse_load, which loads nothing: EAX 0 and LastError
 * ERROR_MOD_NOT_FOUND, as a failed load of a missing DLL. ECX and EDX are not
 * read after the call (LoadLibraryA leaves them undefined as well); the
 * argument is popped (stdcall, `ret 4`). Every other LoadLibraryA of DBSIM (the
 * SOS driver 0x49593e/0x4959b1) and of any other module is untouched.
 *
 * Off unless the environment has ES2_BWCC32=refuse (tools\check.ps1 -Bwcc32
 * refuse). DBSIM.EXE only. DllMain-safe: kernel32 only, memory only; the image
 * and the three instructions are checked first, otherwise nothing is changed
 * (bw_state says why). */
#ifndef ES2_BWCC32_SWITCH_H
#define ES2_BWCC32_SWITCH_H
#include <stdint.h>
#include <string.h>
#include "hook/hook.h"

#define BW_LINK_BASE 0x00400000u
#define BW_IMAGE_SIZE 0x002d8000u
#define BW_PUSH_RVA 0x0006513bu       /* push 0x4a0859 ("BWCC32.DLL") */
#define BW_CALL_RVA 0x00065140u       /* call 0x496904 */
#define BW_NEXT_RVA 0x00065145u       /* lea edx, [ebp-0x28]: EAX is not read */
#define BW_NAME_RVA 0x000a0859u
#define BW_THUNK_RVA 0x00096904u      /* jmp dword ptr [0x6cc3f8] */
#define BW_SLOT_RVA 0x002cc3f8u       /* KERNEL32 LoadLibraryA */

enum { BW_OFF = 0, BW_INSTALLED = 1, BW_REFUSED_IMAGE = -1, BW_REFUSED_SITE = -2, BW_REFUSED_WRITE = -3 };
static volatile LONG bw_state;
static volatile LONG bw_refusals;       /* calls answered with failure */
static volatile LONG bw_forwarded;      /* calls with another name (none expected) */
static BYTE *bw_site;
static const volatile uint32_t *bw_slot;   /* the image's LoadLibraryA slot */
static uint32_t bw_original_rel;

static uint32_t bw_u32(const BYTE *p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* The call's new target. A name other than BWCC32.DLL (not possible at this
 * site; the push is checked) goes to the real LoadLibraryA through the slot. */
static HMODULE WINAPI bw_refuse_load(LPCSTR name)
{
    if (!name || _stricmp(name, "BWCC32.DLL")) {
        InterlockedIncrement(&bw_forwarded);
        HMODULE (WINAPI *real)(LPCSTR) = (HMODULE (WINAPI *)(LPCSTR))(uintptr_t)*bw_slot;
        return real(name);
    }
    InterlockedIncrement(&bw_refusals);
    SetLastError(ERROR_MOD_NOT_FOUND);
    return NULL;
}

/* The three instructions as in the reference DBSIM: push of the name, the call
 * of the LoadLibraryA thunk, the thunk's indirect jump through the IAT slot. */
static BOOL bw_site_valid(const BYTE *image)
{
    const BYTE *push = image + BW_PUSH_RVA, *call = image + BW_CALL_RVA, *thunk = image + BW_THUNK_RVA;
    return push[0] == 0x68 && bw_u32(push + 1) == (uint32_t)(uintptr_t)image + BW_NAME_RVA &&
        call[0] == 0xe8 && (uint32_t)((uintptr_t)call + 5 + bw_u32(call + 1)) == (uint32_t)(uintptr_t)image + BW_THUNK_RVA &&
        thunk[0] == 0xff && thunk[1] == 0x25 && bw_u32(thunk + 2) == (uint32_t)(uintptr_t)image + BW_SLOT_RVA &&
        !memcmp(image + BW_NAME_RVA, "BWCC32.DLL", 11) && !memcmp(image + BW_NEXT_RVA, "\x8d\x55\xd8", 3);
}

/* Memory only. `image` must be a PE image based at expected_base. */
static BOOL bw_install_image(BYTE *image, uintptr_t expected_base)
{
    if (bw_state != BW_OFF) return FALSE;
    if (!es2_hook_image_valid(image, expected_base, BW_IMAGE_SIZE)) { bw_state = BW_REFUSED_IMAGE; return FALSE; }
    if (!bw_site_valid(image)) { bw_state = BW_REFUSED_SITE; return FALSE; }
    BYTE *site = image + BW_CALL_RVA;
    uint32_t rel = (uint32_t)((uintptr_t)bw_refuse_load - ((uintptr_t)site + 5));
    DWORD protection, ignored;
    if (!VirtualProtect(site + 1, 4, PAGE_EXECUTE_READWRITE, &protection)) { bw_state = BW_REFUSED_WRITE; return FALSE; }
    bw_site = site;
    bw_slot = (const volatile uint32_t *)(image + BW_SLOT_RVA);
    bw_original_rel = bw_u32(site + 1);
    memcpy(site + 1, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    VirtualProtect(site + 1, 4, protection, &ignored);
    bw_state = BW_INSTALLED;
    return TRUE;
}

/* ES2_BWCC32=refuse (case-insensitive); anything else, or no variable: off. */
static BOOL bw_requested(void)
{
    WCHAR value[16];
    DWORD n = GetEnvironmentVariableW(L"ES2_BWCC32", value, 16);
    return n && n < 16 && !_wcsicmp(value, L"refuse");
}

#ifndef ES2_BW_NO_PROCESS_ENTRY
/* DllMain (media_proxy.c): DBSIM.EXE only. */
static void install_bwcc32_switch(void)
{
    if (!bw_requested()) return;
    WCHAR exe[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, exe, MAX_PATH);
    WCHAR *name = length && length < MAX_PATH ? wcsrchr(exe, L'\\') : NULL;
    if (!name || _wcsicmp(name + 1, L"DBSIM.EXE")) return;
    bw_install_image((BYTE *)GetModuleHandleW(NULL), BW_LINK_BASE);
}
#endif
#endif
