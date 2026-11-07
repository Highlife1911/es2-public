// DBSIM 1.11 virtual time. DBSIM reads GetTickCount only through its thunk
// 0x004968ec (IAT 0x006cc3e8), called from exactly three sites
// (docs/reverse-engineering/timing.md):
//   0x00467828  wrapper: (GetTickCount - base) >> 4
//   0x00467883  timing init: base = previous = GetTickCount
//   0x004678d0  frame limiter poll: wait until value >= previous + 40
// The proxy replaces the IAT slot in memory (in DllMain, before any DBSIM code
// runs). A limiter poll receives exactly previous + step, so each active loop
// iteration consumes a fixed virtual step; every other caller sees the current
// virtual clock. With [time] virtual_time_intra_frame=1 that clock also moves
// inside a frame: every read after the first since the last limiter poll
// advances it by 1 ms, at most step - 1 times. The cockpit instruments (weapon
// list, radar, damage display) are not drawn while time stands still within a
// frame (visual check). The cap keeps every frame step exact and
// makes modal screens deterministic: their message loops read the clock a
// number of times that depends on real message traffic, but always more than
// the cap. Tape playback (-p, -D) skips the limiter in every frame, so no poll
// ever comes and the clock would stand still after the intra-frame cap: the
// tick log's frame boundary calls vt_frame_boundary, which gives a frame that
// made no poll the step a poll would have given it. No golden of the 32 suites
// has such a frame. The DEMO1 tape still overflows the timed widget list of
// a panel (fault at 0x453127, oracle_suites.md "tape record and playback"); the original does so
// as well at any frame rate above about 21 fps, the rate of that step. The executable file and its reference hash remain untouched.
#ifndef ES2_VIRTUAL_TIME_H
#define ES2_VIRTUAL_TIME_H
#include <stdint.h>
// The state machine (poll, read, frame boundary, counters) is the
// shared library //sim/oracle:virtual_time, which the own process links too.
// This header keeps the IAT wrapper, the configuration and the wall-clock
// pacing; the vt_* names below are the library state's fields, so the
// fixtures and tick_log.h read and write them as before.
#include "sim/oracle/virtual_time.h"

#define VT_IMAGE_SIZE 0x002d8000u
#define VT_IAT_RVA 0x002cc3e8u
#define VT_THUNK_RVA 0x000968ecu
#define VT_LIMITER_RVA 0x000678c0u
#define VT_LIMITER_POLL_RETURN_RVA 0x000678d5u
#define VT_PREVIOUS_CLOCK_RVA 0x000d3b74u
#define VT_SCRATCH_CLOCK_RVA 0x000d3b6cu
#define VT_DEFAULT_START ES2_VT_DEFAULT_START
#define VT_DEFAULT_STALL_LIMIT ES2_VT_DEFAULT_STALL_LIMIT

typedef DWORD (WINAPI *VtTickSource)(void);
static BYTE *vt_image;
static VtTickSource vt_real_tick;
static volatile LONG vt_iat_state;      // 0 none, 1 installed, -1 refused
static INIT_ONCE vt_once = INIT_ONCE_STATIC_INIT;
static BOOL vt_pace;
static es2_vt vt_state;
#define vt_enabled (vt_state.enabled)
#define vt_intra (vt_state.intra)
#define vt_step (vt_state.step)
#define vt_stall_advances (vt_state.stall_advances)
#define vt_limiter_polls (vt_state.limiter_polls)
#define vt_late_polls (vt_state.late_polls)
#define vt_intra_advances (vt_state.intra_advances)
#define vt_bypass_steps (vt_state.bypass_steps)
static LARGE_INTEGER vt_frequency, vt_next_real;

static BOOL vt_image_valid(BYTE *image, uintptr_t expected_base) {
    if ((uintptr_t)image != expected_base || sizeof(void *) != 4) return FALSE;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < (LONG)sizeof(*dos) ||
        dos->e_lfanew > 4096 - (LONG)sizeof(IMAGE_NT_HEADERS32)) return FALSE;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.ImageBase != expected_base || nt->OptionalHeader.SizeOfImage != VT_IMAGE_SIZE) return FALSE;
    // jmp dword [IAT]
    BYTE thunk[6] = {0xff, 0x25};
    DWORD iat = (DWORD)(expected_base + VT_IAT_RVA);
    memcpy(thunk + 2, &iat, 4);
    // push ebx; push esi; mov esi, scratch; mov ebx, [previous]; add ebx, 40; call thunk
    BYTE limiter[21] = {0x53, 0x56, 0xbe, 0, 0, 0, 0, 0x8b, 0x1d, 0, 0, 0, 0, 0x83, 0xc3, 0x28, 0xe8};
    DWORD scratch = (DWORD)(expected_base + VT_SCRATCH_CLOCK_RVA);
    DWORD previous = (DWORD)(expected_base + VT_PREVIOUS_CLOCK_RVA);
    DWORD call = VT_THUNK_RVA - VT_LIMITER_POLL_RETURN_RVA;
    memcpy(limiter + 3, &scratch, 4);
    memcpy(limiter + 9, &previous, 4);
    memcpy(limiter + 17, &call, 4);
    return !memcmp(image + VT_THUNK_RVA, thunk, sizeof(thunk)) &&
        !memcmp(image + VT_LIMITER_RVA, limiter, sizeof(limiter));
}

// Called once, on the first GetTickCount call by DBSIM, outside the loader lock.
// Only the fixture calls vt_enable directly.
static void vt_enable(BOOL enabled, DWORD step, DWORD start, BOOL pace, DWORD stall_limit, BOOL intra) {
    es2_vt_enable(&vt_state, enabled, step, start, stall_limit, intra);
    vt_pace = pace;
    QueryPerformanceFrequency(&vt_frequency);
}

static BOOL CALLBACK vt_configure(PINIT_ONCE once, PVOID arg, PVOID *context) {
    (void)once; (void)arg; (void)context;
    if (!config_path[0]) {
        DWORD n = GetModuleFileNameW(self_module, config_path, MAX_PATH);
        WCHAR *slash = n && n < MAX_PATH ? wcsrchr(config_path, L'\\') : NULL;
        if (!slash || slash - config_path + 1 + wcslen(L"es2-oracle.ini") >= MAX_PATH) { config_path[0] = 0; return TRUE; }
        wcscpy(slash + 1, L"es2-oracle.ini");
    }
    DWORD step = GetPrivateProfileIntW(L"time", L"virtual_time_step_ms", 0, config_path);
    DWORD start = GetPrivateProfileIntW(L"time", L"virtual_time_start_ms", VT_DEFAULT_START, config_path);
    BOOL pace = GetPrivateProfileIntW(L"time", L"virtual_time_pace", 1, config_path) != 0;
    DWORD stall = GetPrivateProfileIntW(L"time", L"virtual_time_stall_limit", VT_DEFAULT_STALL_LIMIT, config_path);
    BOOL intra = GetPrivateProfileIntW(L"time", L"virtual_time_intra_frame", 0, config_path) != 0;
    vt_enable(step != 0, step, start, pace, stall, intra);
    if (step && !vt_enabled) log_event("virtual_time_step_refused");
    else log_event(vt_enabled ? "virtual_time_enabled" : "virtual_time_disabled");
    return TRUE;
}

// Keeps the virtual step at wall-clock speed when pacing is enabled; never
// changes the returned values, only when they are returned.
static void vt_wait_real(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LONGLONG step = (LONGLONG)vt_step * vt_frequency.QuadPart / 1000;
    if (!vt_next_real.QuadPart || now.QuadPart > vt_next_real.QuadPart + step) {
        vt_next_real.QuadPart = now.QuadPart + step;   // first poll or far behind: no catch-up burst
        if (vt_limiter_polls) ++vt_late_polls;
        return;
    }
    while (now.QuadPart < vt_next_real.QuadPart) {
        LONGLONG remaining_ms = (vt_next_real.QuadPart - now.QuadPart) * 1000 / vt_frequency.QuadPart;
        if (remaining_ms > 2) Sleep(1); else YieldProcessor();
        QueryPerformanceCounter(&now);
    }
    vt_next_real.QuadPart += step;
}
static void vt_pace_poll(void *user) { (void)user; vt_wait_real(); }

static DWORD WINAPI vt_get_tick_count(void) {
    BYTE *caller = __builtin_return_address(0);
    InitOnceExecuteOnce(&vt_once, vt_configure, NULL, NULL);
    if (!vt_enabled) return vt_real_tick();
    if (caller == vt_image + VT_LIMITER_POLL_RETURN_RVA) {
        DWORD previous;
        memcpy(&previous, vt_image + VT_PREVIOUS_CLOCK_RVA, 4);
        return es2_vt_limiter_poll(&vt_state, previous, vt_pace ? vt_pace_poll : NULL, NULL);
    }
    return es2_vt_read(&vt_state);
}

// Once per main-loop frame, on the game's main thread (tick_log.h, before the
// frame counter increment). A frame since the previous boundary without a
// limiter poll bypassed the limiter: advance as a poll would (the step from
// the clock after the last step, never backwards) and restart the intra-frame
// budget. Frames with a poll are untouched.
static void vt_frame_boundary(void) { es2_vt_frame_boundary(&vt_state); }

static BOOL install_virtual_time_image(BYTE *image, uintptr_t expected_base) {
    if (InterlockedCompareExchange(&vt_iat_state, -1, 0) != 0) return vt_iat_state == 1;
    if (!vt_image_valid(image, expected_base)) return FALSE;
    DWORD *slot = (DWORD *)(image + VT_IAT_RVA);
    DWORD protection, ignored;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return FALSE;
    vt_image = image;
    vt_real_tick = (VtTickSource)(uintptr_t)*slot;
    InterlockedExchange((LONG *)slot, (LONG)(uintptr_t)vt_get_tick_count);
    VirtualProtect(slot, sizeof(*slot), protection, &ignored);
    InterlockedExchange(&vt_iat_state, 1);
    return TRUE;
}

// DllMain-safe: no file I/O, no loading. Configuration is read on first use.
static void install_virtual_time(void) {
    WCHAR exe[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    WCHAR *name = wcsrchr(exe, L'\\');
    if (!name || _wcsicmp(name + 1, L"DBSIM.EXE")) return;
    install_virtual_time_image((BYTE *)GetModuleHandleW(NULL), (uintptr_t)0x00400000);
}
#endif
