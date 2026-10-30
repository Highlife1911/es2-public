// Own-code fixture for proxy/virtual_time.h. Only a synthetic image with own
// stubs is executed; no original game code runs here.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static WCHAR config_path[MAX_PATH];
static HMODULE self_module;
static void log_event(const char *event) { (void)event; }
#include "../virtual_time.h"

static DWORD real_calls;
static DWORD WINAPI fake_real_tick(void) { return 777 + real_calls++; }

typedef DWORD (*Poll)(void);
static BYTE *image;
static Poll limiter_poll, other_caller;

static BOOL make_image(void) {
    image = VirtualAlloc(NULL, VT_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!image) return FALSE;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.ImageBase = (DWORD)(uintptr_t)image;
    nt->OptionalHeader.SizeOfImage = VT_IMAGE_SIZE;
    DWORD real = (DWORD)(uintptr_t)fake_real_tick, iat = (DWORD)(uintptr_t)(image + VT_IAT_RVA);
    memcpy(image + VT_IAT_RVA, &real, 4);
    image[VT_THUNK_RVA] = 0xff; image[VT_THUNK_RVA + 1] = 0x25;
    memcpy(image + VT_THUNK_RVA + 2, &iat, 4);
    // Limiter prologue as validated, then an own `ret` where the original continues.
    BYTE *limiter = image + VT_LIMITER_RVA;
    DWORD scratch = (DWORD)(uintptr_t)(image + VT_SCRATCH_CLOCK_RVA);
    DWORD previous = (DWORD)(uintptr_t)(image + VT_PREVIOUS_CLOCK_RVA);
    DWORD call = VT_THUNK_RVA - VT_LIMITER_POLL_RETURN_RVA;
    static const BYTE head[] = {0x53, 0x56, 0xbe};
    memcpy(limiter, head, 3);
    memcpy(limiter + 3, &scratch, 4);
    limiter[7] = 0x8b; limiter[8] = 0x1d;
    memcpy(limiter + 9, &previous, 4);
    limiter[13] = 0x83; limiter[14] = 0xc3; limiter[15] = 0x28; limiter[16] = 0xe8;
    memcpy(limiter + 17, &call, 4);
    image[VT_LIMITER_POLL_RETURN_RVA] = 0xc3;
    // Another caller: call thunk; ret.
    BYTE *other = image + 0x00067828;
    DWORD other_call = VT_THUNK_RVA - (0x00067828 + 5);
    other[0] = 0xe8; memcpy(other + 1, &other_call, 4); other[5] = 0xc3;
    limiter_poll = (Poll)(void *)(image + VT_LIMITER_RVA + 16);
    other_caller = (Poll)(void *)other;
    return TRUE;
}

static void set_previous(DWORD value) { memcpy(image + VT_PREVIOUS_CLOCK_RVA, &value, 4); }

static DWORD init_step, init_start, init_stall;
static BOOL init_enabled, init_pace, init_intra;
static BOOL CALLBACK fixture_configure(PINIT_ONCE once, PVOID arg, PVOID *context) {
    (void)once; (void)arg; (void)context;
    vt_enable(init_enabled, init_step, init_start, init_pace, init_stall, init_intra);
    return TRUE;
}
static void configure(BOOL enabled, DWORD step, DWORD start, BOOL pace, DWORD stall) {
    init_enabled = enabled; init_step = step; init_start = start; init_pace = pace; init_stall = stall;
    InitOnceExecuteOnce(&vt_once, fixture_configure, NULL, NULL);
}

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

int wmain(int argc, WCHAR **argv) {
    (void)install_virtual_time; (void)vt_configure; // production entry points, not exercised here
    if (argc != 2 || !make_image()) return 2;
    const WCHAR *mode = argv[1];
    DWORD original_slot;
    memcpy(&original_slot, image + VT_IAT_RVA, 4);

    if (!wcscmp(mode, L"valid")) {
        CHECK(install_virtual_time_image(image, (uintptr_t)image));
        DWORD slot; memcpy(&slot, image + VT_IAT_RVA, 4);
        CHECK(slot == (DWORD)(uintptr_t)vt_get_tick_count);
        configure(TRUE, 47, 1000, FALSE, 5);
        CHECK(other_caller() == 1000);               // before any limiter poll: start value
        set_previous(1000);
        CHECK(limiter_poll() == 1047);
        set_previous(1047);                          // what the original limiter stores
        CHECK(limiter_poll() == 1094);
        // The original wait condition `previous + 40 > value` ends after one poll.
        CHECK(!(1094 + 40 > 1094 + 47));
        for (int i = 0; i < 5; ++i) CHECK(other_caller() == 1094);  // up to the stall limit: no advance
        CHECK(other_caller() == 1095 && other_caller() == 1096 && vt_stall_advances == 2);
        set_previous(1094);
        CHECK(limiter_poll() == 1141);
        set_previous(1000);                          // stale previous: never move backwards
        CHECK(limiter_poll() == 1141);
        CHECK(vt_limiter_polls == 4 && real_calls == 0);
    } else if (!wcscmp(mode, L"intra")) {
        CHECK(install_virtual_time_image(image, (uintptr_t)image));
        init_intra = TRUE;
        configure(TRUE, 47, 1000, FALSE, 5);
        CHECK(other_caller() == 1000 && other_caller() == 1001 && other_caller() == 1002);
        set_previous(1000);
        CHECK(limiter_poll() == 1047);               // the frame step stays exact
        set_previous(1047);
        CHECK(other_caller() == 1047);               // first read after a poll: no advance
        for (DWORD i = 1; i <= 10; ++i) CHECK(other_caller() == 1047 + i);
        CHECK(vt_intra_advances == 12 && vt_stall_advances == 0);
        CHECK(limiter_poll() == 1094);
        set_previous(1094);
        // More reads than the step (a modal message loop): capped at step - 1,
        // then the stall guard (limit 5 here) takes over for the remaining reads.
        for (DWORD i = 0; i < 47; ++i) CHECK(other_caller() == 1094 + i);
        CHECK(vt_intra_advances == 58 && vt_stall_advances == 0);
        CHECK(other_caller() == 1141 && vt_stall_advances == 1);
        CHECK(limiter_poll() == 1141 + 0 && vt_limiter_polls == 3);  // previous + 47 equals the clock: exact step
        CHECK(real_calls == 0);
    } else if (!wcscmp(mode, L"bypass")) {
        // Tape playback skips the limiter: the frame boundary gives such a frame one step.
        CHECK(install_virtual_time_image(image, (uintptr_t)image));
        init_intra = TRUE;
        configure(TRUE, 47, 1000, FALSE, 1000);
        vt_frame_boundary();                         // the first boundary only starts the count
        CHECK(vt_bypass_steps == 0 && other_caller() == 1000 && other_caller() == 1001);
        vt_frame_boundary();                         // a frame without a poll: base 1000 + 47
        CHECK(vt_bypass_steps == 1 && other_caller() == 1047 && other_caller() == 1048);
        for (int i = 0; i < 100; ++i) other_caller(); // intra budget: at most step - 1 per frame
        CHECK(other_caller() == 1093);
        vt_frame_boundary();
        CHECK(vt_bypass_steps == 2 && other_caller() == 1094);   // exact step, the budget restarts
        set_previous(1094);
        CHECK(limiter_poll() == 1141);
        vt_frame_boundary();                         // a frame with a poll is untouched
        CHECK(vt_bypass_steps == 2 && other_caller() == 1141);
        vt_frame_boundary();                         // the next frame bypasses again: from the poll's clock
        CHECK(vt_bypass_steps == 3 && other_caller() == 1188);
        CHECK(vt_limiter_polls == 1 && vt_stall_advances == 0 && real_calls == 0);
    } else if (!wcscmp(mode, L"disabled")) {
        CHECK(install_virtual_time_image(image, (uintptr_t)image));
        configure(FALSE, 47, 1000, FALSE, 5);
        CHECK(limiter_poll() == 777 && other_caller() == 778);
    } else if (!wcscmp(mode, L"small-step")) {
        CHECK(install_virtual_time_image(image, (uintptr_t)image));
        configure(TRUE, 30, 1000, FALSE, 5);
        CHECK(!vt_enabled);
        CHECK(limiter_poll() == 777);
    } else if (!wcscmp(mode, L"mismatch")) {
        image[VT_LIMITER_RVA + 14] = 0xc0;
        CHECK(!install_virtual_time_image(image, (uintptr_t)image));
        DWORD slot; memcpy(&slot, image + VT_IAT_RVA, 4);
        CHECK(slot == original_slot);
    } else if (!wcscmp(mode, L"pace")) {
        CHECK(install_virtual_time_image(image, (uintptr_t)image));
        configure(TRUE, 47, 1000, TRUE, 5);
        LARGE_INTEGER frequency, t0, t1;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&t0);
        DWORD previous = 1000;
        for (int i = 0; i < 6; ++i) { set_previous(previous); previous = limiter_poll(); }
        QueryPerformanceCounter(&t1);
        double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)frequency.QuadPart;
        CHECK(previous == 1000 + 6 * 47);
        CHECK(ms >= 5 * 47 - 2 && ms < 5 * 47 + 150);  // first poll does not wait
        printf("paced 6 polls in %.1f ms\n", ms);
    } else {
        return 2;
    }
    printf("PASS %ls\n", mode);
    return 0;
}
