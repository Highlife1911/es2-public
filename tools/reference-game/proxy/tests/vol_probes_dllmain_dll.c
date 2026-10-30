/* Fixture DLL: runs vol_probes_attach from a real DllMain (under the
 * loader lock) on the synthetic image of vol_probes_fixture.c, whose address
 * and stub layout arrive through ES2_VP_FIXTURE_IMAGE. Own code only. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include "../vol_probes.h"

static vp_spec specs[6] = {
    {0x1000, "register", 4, {1, 2}, VP_SNAP_GLOBAL, VP_NONE, 0, 8, {0}},
    {0x1100, "manager_update", 5, {2, 3}, VP_SNAP_ARG0, VP_NONE, 0, 8, {0}},
    {0x1200, "insert", 3, {1, VP_NONE}, VP_SNAP_ARG0, VP_NONE, 0, 8, {0}},
    {0x1300, "archive_open", 3, {0, VP_NONE}, VP_SNAP_NONE, VP_NONE, 0, 7, {0}},
    {0x1400, "lookup", 3, {1, VP_NONE}, VP_SNAP_NONE, VP_NONE, 1, 8, {0}},
    {0x1500, "member_lookup", 3, {1, VP_NONE}, VP_SNAP_NONE, 0, 1, 8, {0}},
};
static const BYTE anchor[] = {0x83, 0x3d, 0x00, 0x20, 0x00, 0x00, 0x00};
static vp_target target;
static DWORD attach_thread;

__declspec(dllexport) int __cdecl vp_fixture_state(void) { return vp_state; }
__declspec(dllexport) DWORD __cdecl vp_fixture_attach_thread(void) { return attach_thread; }
__declspec(dllexport) void __cdecl vp_fixture_forwarding(void) { vol_probes_forwarding_verified(TRUE); }
__declspec(dllexport) LONG __cdecl vp_fixture_events(void) { return vp_event_next; }

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)module; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        WCHAR text[32];
        DWORD n = GetEnvironmentVariableW(L"ES2_VP_FIXTURE_IMAGE", text, 32);
        if (!n || n >= 32) return TRUE;
        BYTE *image = (BYTE *)(uintptr_t)wcstoul(text, NULL, 16);
        /* The executable verified its own stubs; take their bytes as the
         * signatures, as the reference table does for DBSIM. */
        for (unsigned i = 0; i < 6; ++i) memcpy(specs[i].signature, image + specs[i].rva, specs[i].length);
        target = (vp_target){(uintptr_t)image, 0x10000u, specs, 6, 0x2000u, 0x1f00u, anchor, sizeof(anchor)};
        attach_thread = GetCurrentThreadId();
        vol_probes_attach(image, L"C:\\synthetic\\DBSIM.EXE", &target, NULL, 0);
    } else if (reason == DLL_PROCESS_DETACH) {
        vol_probes_shutdown("detach");
    }
    return TRUE;
}
