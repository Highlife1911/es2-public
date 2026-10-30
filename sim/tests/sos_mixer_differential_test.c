/* The mixing device (sim/src/audio.c, audio.md "The mixing device") against
 * SOS9503.DLL's DIGI driver, in vitro.
 *
 * VER95/SOS9503.DLL is read from ES2_DATA_ROOT (CD root or extracted/cd),
 * size and SHA-256 checked on the read buffer, and mapped privately (at its
 * link base 0x400000 when free, else relocated): sections copied, HIGHLOW
 * relocations applied, every import bound by name to this test (an unbound
 * one ends the run). Its entry point is not called; the exercised paths need
 * no CRT state. Only the device boundary is replaced:
 *  - waveOut*: a recording device. Every waveOutWrite is captured; a buffer
 *    "plays" when the test returns it through the driver's callback (WOM_DONE);
 *    waveOutReset returns every queued buffer through the callback at once.
 *  - CreateThread/ResumeThread/SuspendThread: the service thread 0x403809
 *    runs as a fiber. "Refill first": ResumeThread switches to it at once, so
 *    its refill runs before WOM_DONE's removals; "removal first": it runs
 *    after the callback has returned. During a reset the resumes are dropped
 *    (the thread finds the stop counter of 0x4039f9 set).
 *  - IsWindow/PostMessageA recorded (0xdead is no window), timers never fire,
 *    waveOutGetDevCapsA one device with every format, LoadLibraryA fails (no
 *    DirectSound), Global* forwarded to the system.
 * The same calls go to es2sim's mixer over a recording sink in which a buffer
 * plays when the test pops it and pumps the mixer. Refill first, per call:
 * EAX, handle words, every written buffer (length and bytes), the waveOut
 * format against the sink's open, the posted notices, SampleDone, the
 * outstanding count. Removal first is the other possible thread order; there
 * the output is compared per scenario (tolerant_finish) and its differences
 * are counted. The voice callback +68h (default 0x40421c) is wrapped after
 * every start and must never be called. Without data: SKIP. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>
#include <wincrypt.h>
#include "es2/sim/audio.h"
#include "es2/sim/memory.h"

#define SOS_FILE_SIZE 95744u
#define SOS_SHA256 "8422475ce5e4bba1938920211be57f7adeb0dfffa3d3255c50a86a2b4a42d97e"
#define SOS_LINK_BASE 0x00400000u
#define SOS_IMAGE_SIZE 0x22000u
#define SOS_DRIVER 0x1766cu          /* driver 0's record, RVA (VA 0x41766c) */
#define SOS_VOICE_SIZE 0xfcu
#define SOS_DEFAULT_68 0x421cu       /* RVA of the default +68h callback 0x40421c */

static uint16_t get16(const BYTE *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const BYTE *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put16(BYTE *p, uint32_t x) { p[0] = (BYTE)x; p[1] = (BYTE)(x >> 8); }
static void put32(BYTE *p, uint32_t x) { for (unsigned i = 0; i < 4; ++i) p[i] = (BYTE)(x >> (8 * i)); }
static int extent(uint32_t p, uint32_t n, uint32_t size) { return p <= size && n <= size - p; }

/* ---- the run's verdicts */
static unsigned failures, scenarios, ops_run;
static char scenario_name[160];
static int scenario_failed;
static void failf(const char *fmt, ...) {
    va_list ap;
    if (!scenario_failed) fprintf(stdout, "FAIL [%s]\n", scenario_name);
    scenario_failed = 1;
    ++failures;
    fprintf(stdout, "  ");
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
}

/* ---- the mapped DLL */
static BYTE *file_bytes;
static BYTE *image;
static uint32_t trap_count;
static char trap_names[160][64];
static BYTE *trap_stubs;
static void __cdecl import_trap(unsigned id) {
    fprintf(stdout, "TRAP: SOS9503.DLL called the unbound import %s [%s]\n", id < trap_count ? trap_names[id] : "?", scenario_name);
    fflush(stdout);
    ExitProcess(3);
}

/* Recorded state of the device boundary. */
typedef struct buflog { uint32_t n, cap; uint32_t *len; uint8_t **data; } buflog;
static void log_add(buflog *l, const uint8_t *d, uint32_t n) {
    if (l->n == l->cap) {
        l->cap = l->cap ? 2u * l->cap : 64u;
        l->len = realloc(l->len, l->cap * sizeof(*l->len));
        l->data = realloc(l->data, l->cap * sizeof(*l->data));
        if (!l->len || !l->data) { fprintf(stdout, "out of memory\n"); ExitProcess(4); }
    }
    l->data[l->n] = malloc(n ? n : 1u);
    if (!l->data[l->n]) { fprintf(stdout, "out of memory\n"); ExitProcess(4); }
    memcpy(l->data[l->n], d, n);
    l->len[l->n++] = n;
}
static void log_clear(buflog *l) {
    for (uint32_t i = 0; i < l->n; ++i) free(l->data[i]);
    l->n = 0;
}
typedef struct postlog { uint32_t n; uint32_t w[4096][4]; } postlog;
static void post_add(postlog *p, uint32_t window, uint32_t message, uint32_t wparam, uint32_t lparam) {
    if (p->n < 4096u) { p->w[p->n][0] = window; p->w[p->n][1] = message; p->w[p->n][2] = wparam; p->w[p->n][3] = lparam; }
    ++p->n;
}

typedef void (CALLBACK *wave_callback)(HWAVEOUT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
static struct {
    int open;
    UINT device;
    WAVEFORMATEX format;
    wave_callback callback;
    DWORD_PTR instance;
    DWORD flags;
    WAVEHDR *queue[256];
    uint32_t head, count;
    uint32_t opens, closes, resets, pauses, restarts, prepares, unprepares, write_errors;
    buflog writes;
} wave;
#define FAKE_HWO ((HWAVEOUT)(uintptr_t)0x5150u)

static postlog dll_posts, sim_posts;

/* The service thread as a fiber. */
enum { REFILL_FIRST = 0, REMOVAL_FIRST = 1 };
static int thread_mode;
static LPVOID main_fiber;
static struct {
    LPVOID fiber;
    LPTHREAD_START_ROUTINE start;
    LPVOID param;
    int created, dead, running, pending, in_reset;
    uint32_t resumes, dropped, suspends, runs, exit_code;
} th;
#define FAKE_THREAD ((HANDLE)(uintptr_t)0x7a00u)

static void WINAPI thread_fiber(LPVOID unused) {
    (void)unused;
    th.exit_code = th.start(th.param);
    th.dead = 1;
    for (;;) SwitchToFiber(main_fiber);
}
static void run_thread(void) {
    if (!th.created || th.dead || th.running) return;
    th.running = 1;
    ++th.runs;
    SwitchToFiber(th.fiber);
    th.running = 0;
}
static void run_pending(void) {
    while (th.pending && !th.dead) { th.pending = 0; run_thread(); }
}

/* +68h recorder. */
typedef void (__cdecl *voice_callback)(uint32_t voice);
static uint32_t calls_68;
static voice_callback original_68;
static void __cdecl record_68(uint32_t voice) { ++calls_68; if (original_68) original_68(voice); }

/* Global* forwarded to the system, tracked so that a re-mapping frees them. */
static HGLOBAL globals[256];
static unsigned global_count;
static HGLOBAL WINAPI s_GlobalAlloc(UINT flags, SIZE_T bytes) {
    HGLOBAL h = GlobalAlloc(flags, bytes);
    if (h && global_count < 256u) globals[global_count++] = h;
    return h;
}
static HGLOBAL WINAPI s_GlobalFree(HGLOBAL h) {
    for (unsigned i = 0; i < global_count; ++i) if (globals[i] == h) { globals[i] = globals[--global_count]; break; }
    return GlobalFree(h);
}
static LPVOID WINAPI s_GlobalLock(HGLOBAL h) { return GlobalLock(h); }
static BOOL WINAPI s_GlobalUnlock(HGLOBAL h) { return GlobalUnlock(h); }
static HGLOBAL WINAPI s_GlobalHandle(LPCVOID p) { return GlobalHandle(p); }

static HANDLE WINAPI s_CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID param, DWORD flags, LPDWORD id) {
    (void)sa; (void)stack;
    if (th.created || !(flags & CREATE_SUSPENDED)) { fprintf(stdout, "unexpected CreateThread (flags %lx)\n", (unsigned long)flags); ExitProcess(5); }
    th.fiber = CreateFiber(0x40000, thread_fiber, NULL);
    if (!th.fiber) { fprintf(stdout, "CreateFiber failed\n"); ExitProcess(5); }
    th.start = start; th.param = param; th.created = 1;
    if (id) *id = 0x1234u;
    return FAKE_THREAD;
}
static BOOL WINAPI s_SetThreadPriority(HANDLE h, int priority) { (void)h; (void)priority; return TRUE; }
static DWORD WINAPI s_ResumeThread(HANDLE h) {
    (void)h;
    ++th.resumes;
    if (th.in_reset) { ++th.dropped; return 1; }
    if (thread_mode == REFILL_FIRST) run_thread();
    else th.pending = 1;
    return 1;
}
static DWORD WINAPI s_SuspendThread(HANDLE h) {
    ++th.suspends;
    if (th.running && h == FAKE_THREAD) SwitchToFiber(main_fiber);
    return 0;
}
static BOOL WINAPI s_CloseHandle(HANDLE h) { (void)h; return TRUE; }
static HMODULE WINAPI s_LoadLibraryA(LPCSTR name) { fprintf(stdout, "  LoadLibraryA(%s) refused\n", name ? name : "(null)"); return NULL; }
static BOOL WINAPI s_FreeLibrary(HMODULE h) { (void)h; return TRUE; }
static FARPROC WINAPI s_GetProcAddress(HMODULE h, LPCSTR name) { (void)h; (void)name; return NULL; }

static BOOL WINAPI s_IsWindow(HWND h) { return (uintptr_t)h != 0xdeadu; }
static BOOL WINAPI s_PostMessageA(HWND h, UINT m, WPARAM w, LPARAM l) {
    post_add(&dll_posts, (uint32_t)(uintptr_t)h, m, (uint32_t)w, (uint32_t)l);
    return TRUE;
}

static MMRESULT WINAPI s_timeGetDevCaps(LPTIMECAPS caps, UINT size) {
    if (size >= sizeof(TIMECAPS)) { caps->wPeriodMin = 1; caps->wPeriodMax = 1000000; }
    return 0;
}
static MMRESULT WINAPI s_timeBeginPeriod(UINT p) { (void)p; return 0; }
static MMRESULT WINAPI s_timeEndPeriod(UINT p) { (void)p; return 0; }
static MMRESULT WINAPI s_timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK proc, DWORD_PTR user, UINT flags) {
    (void)delay; (void)resolution; (void)proc; (void)user; (void)flags;
    return 1;
}
static MMRESULT WINAPI s_timeKillEvent(UINT id) { (void)id; return 0; }

static MMRESULT WINAPI s_waveOutGetDevCapsA(UINT_PTR device, LPWAVEOUTCAPSA caps, UINT size) {
    if (device != 0 && device != WAVE_MAPPER) return MMSYSERR_BADDEVICEID;
    if (size < sizeof(WAVEOUTCAPSA)) return MMSYSERR_INVALPARAM;
    memset(caps, 0, size);
    caps->dwFormats = 0xfffu;
    caps->wChannels = 2;
    return 0;
}
static MMRESULT WINAPI s_waveOutOpen(LPHWAVEOUT h, UINT device, LPCWAVEFORMATEX format, DWORD_PTR callback, DWORD_PTR instance, DWORD flags) {
    if (wave.open) return MMSYSERR_ALLOCATED;
    ++wave.opens;
    wave.device = device;
    wave.format = *format;
    wave.callback = (wave_callback)callback;
    wave.instance = instance;
    wave.flags = flags;
    wave.open = 1;
    wave.head = wave.count = 0;
    *h = FAKE_HWO;
    if ((flags & CALLBACK_TYPEMASK) == CALLBACK_FUNCTION && wave.callback) wave.callback(FAKE_HWO, WOM_OPEN, instance, 0, 0);
    return 0;
}
static MMRESULT WINAPI s_waveOutClose(HWAVEOUT h) {
    if (h != FAKE_HWO || !wave.open) return MMSYSERR_INVALHANDLE;
    if (wave.count) return WAVERR_STILLPLAYING;
    ++wave.closes;
    wave.open = 0;
    if (wave.callback) wave.callback(FAKE_HWO, WOM_CLOSE, wave.instance, 0, 0);
    return 0;
}
static MMRESULT WINAPI s_waveOutPrepareHeader(HWAVEOUT h, LPWAVEHDR hdr, UINT size) {
    if (h != FAKE_HWO || !wave.open) return MMSYSERR_INVALHANDLE;
    if (size < sizeof(WAVEHDR)) return MMSYSERR_INVALPARAM;
    ++wave.prepares;
    hdr->dwFlags |= WHDR_PREPARED;
    return 0;
}
static MMRESULT WINAPI s_waveOutUnprepareHeader(HWAVEOUT h, LPWAVEHDR hdr, UINT size) {
    (void)size;
    if (h != FAKE_HWO || !wave.open) return MMSYSERR_INVALHANDLE;
    if (hdr->dwFlags & WHDR_INQUEUE) return WAVERR_STILLPLAYING;
    ++wave.unprepares;
    hdr->dwFlags &= ~(DWORD)WHDR_PREPARED;
    return 0;
}
static MMRESULT WINAPI s_waveOutWrite(HWAVEOUT h, LPWAVEHDR hdr, UINT size) {
    (void)size;
    if (h != FAKE_HWO || !wave.open) { ++wave.write_errors; return MMSYSERR_INVALHANDLE; }
    if (!(hdr->dwFlags & WHDR_PREPARED)) { ++wave.write_errors; return WAVERR_UNPREPARED; }
    if (wave.count >= 256u) { fprintf(stdout, "device queue overflow\n"); ExitProcess(5); }
    log_add(&wave.writes, (const uint8_t *)hdr->lpData, hdr->dwBufferLength);
    hdr->dwFlags = (hdr->dwFlags | WHDR_INQUEUE) & ~(DWORD)WHDR_DONE;
    wave.queue[(wave.head + wave.count++) & 255u] = hdr;
    return 0;
}
static void wave_return(WAVEHDR *hdr) {
    hdr->dwFlags = (hdr->dwFlags & ~(DWORD)WHDR_INQUEUE) | WHDR_DONE;
    wave.callback(FAKE_HWO, WOM_DONE, wave.instance, (DWORD_PTR)hdr, 0);
}
static MMRESULT WINAPI s_waveOutReset(HWAVEOUT h) {
    if (h != FAKE_HWO || !wave.open) return MMSYSERR_INVALHANDLE;
    ++wave.resets;
    th.in_reset = 1;
    while (wave.count) {
        WAVEHDR *hdr = wave.queue[wave.head];
        wave.head = (wave.head + 1u) & 255u;
        --wave.count;
        wave_return(hdr);
    }
    th.in_reset = 0;
    return 0;
}
static MMRESULT WINAPI s_waveOutPause(HWAVEOUT h) { if (h != FAKE_HWO || !wave.open) return MMSYSERR_INVALHANDLE; ++wave.pauses; return 0; }
static MMRESULT WINAPI s_waveOutRestart(HWAVEOUT h) { if (h != FAKE_HWO || !wave.open) return MMSYSERR_INVALHANDLE; ++wave.restarts; return 0; }

typedef struct binding { const char *name; void *fn; } binding;
static const binding bindings[] = {
    {"KERNEL32.dll!GlobalAlloc", (void *)s_GlobalAlloc}, {"KERNEL32.dll!GlobalFree", (void *)s_GlobalFree},
    {"KERNEL32.dll!GlobalLock", (void *)s_GlobalLock}, {"KERNEL32.dll!GlobalUnlock", (void *)s_GlobalUnlock},
    {"KERNEL32.dll!GlobalHandle", (void *)s_GlobalHandle}, {"KERNEL32.dll!CreateThread", (void *)s_CreateThread},
    {"KERNEL32.dll!SetThreadPriority", (void *)s_SetThreadPriority}, {"KERNEL32.dll!ResumeThread", (void *)s_ResumeThread},
    {"KERNEL32.dll!SuspendThread", (void *)s_SuspendThread}, {"KERNEL32.dll!CloseHandle", (void *)s_CloseHandle},
    {"KERNEL32.dll!LoadLibraryA", (void *)s_LoadLibraryA}, {"KERNEL32.dll!FreeLibrary", (void *)s_FreeLibrary},
    {"KERNEL32.dll!GetProcAddress", (void *)s_GetProcAddress},
    {"USER32.dll!IsWindow", (void *)s_IsWindow}, {"USER32.dll!PostMessageA", (void *)s_PostMessageA},
    {"WINMM.dll!timeGetDevCaps", (void *)s_timeGetDevCaps}, {"WINMM.dll!timeBeginPeriod", (void *)s_timeBeginPeriod},
    {"WINMM.dll!timeEndPeriod", (void *)s_timeEndPeriod}, {"WINMM.dll!timeSetEvent", (void *)s_timeSetEvent},
    {"WINMM.dll!timeKillEvent", (void *)s_timeKillEvent}, {"WINMM.dll!waveOutGetDevCapsA", (void *)s_waveOutGetDevCapsA},
    {"WINMM.dll!waveOutOpen", (void *)s_waveOutOpen}, {"WINMM.dll!waveOutClose", (void *)s_waveOutClose},
    {"WINMM.dll!waveOutPrepareHeader", (void *)s_waveOutPrepareHeader},
    {"WINMM.dll!waveOutUnprepareHeader", (void *)s_waveOutUnprepareHeader},
    {"WINMM.dll!waveOutWrite", (void *)s_waveOutWrite}, {"WINMM.dll!waveOutReset", (void *)s_waveOutReset},
    {"WINMM.dll!waveOutPause", (void *)s_waveOutPause}, {"WINMM.dll!waveOutRestart", (void *)s_waveOutRestart},
};

static int read_dll(const char *root) {
    char candidates[2][MAX_PATH], path[MAX_PATH];
    if (snprintf(candidates[0], MAX_PATH, "%s/VER95/SOS9503.DLL", root) >= MAX_PATH ||
        snprintf(candidates[1], MAX_PATH, "%s/extracted/cd/VER95/SOS9503.DLL", root) >= MAX_PATH) return 1;
    unsigned found = 0;
    for (unsigned i = 0; i < 2; ++i) if (GetFileAttributesA(candidates[i]) != INVALID_FILE_ATTRIBUTES) { strcpy(path, candidates[i]); ++found; }
    if (found != 1) { fprintf(stdout, "ES2_DATA_ROOT must hold exactly one VER95/SOS9503.DLL\n"); return 1; }
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 1;
    LARGE_INTEGER size;
    DWORD got = 0;
    file_bytes = malloc(SOS_FILE_SIZE);
    BOOL ok = GetFileSizeEx(f, &size) && size.QuadPart == SOS_FILE_SIZE && file_bytes &&
              ReadFile(f, file_bytes, SOS_FILE_SIZE, &got, NULL) && got == SOS_FILE_SIZE;
    CloseHandle(f);
    if (!ok) { fprintf(stdout, "SOS9503.DLL size/read\n"); return 1; }
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0; BYTE digest[32] = {0}; DWORD length = 32;
    int hashed = CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
                 CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) && CryptHashData(hash, file_bytes, SOS_FILE_SIZE, 0) &&
                 CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0);
    char hex[65]; static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) { hex[2 * i] = digits[digest[i] >> 4]; hex[2 * i + 1] = digits[digest[i] & 15]; }
    hex[64] = 0;
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    if (!hashed || strcmp(hex, SOS_SHA256)) { fprintf(stdout, "SOS9503.DLL SHA-256 gate\n"); return 1; }
    printf("Read SHA-256 verified %s\n", path);
    return 0;
}

static uint32_t relocations_applied;
/* A fresh private mapping (every global of the DLL in its initial state). */
static int map_dll(void) {
    if (image) VirtualFree(image, 0, MEM_RELEASE);
    for (unsigned i = 0; i < global_count; ++i) GlobalFree(globals[i]);
    global_count = 0;
    const BYTE *bytes = file_bytes;
    uint32_t pe = get32(bytes + 0x3c);
    const BYTE *nt = bytes + pe, *opt = nt + 24;
    uint32_t link_base = get32(opt + 28), image_size = get32(opt + 56), headers = get32(opt + 60);
    if (link_base != SOS_LINK_BASE || image_size != SOS_IMAGE_SIZE || headers > SOS_FILE_SIZE) return 1;
    image = VirtualAlloc((void *)(uintptr_t)SOS_LINK_BASE, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!image) image = VirtualAlloc(NULL, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!image) return 1;
    memcpy(image, bytes, headers);
    const BYTE *section = opt + get16(nt + 20);
    for (unsigned i = 0; i < get16(nt + 6); ++i, section += 40) {
        uint32_t va = get32(section + 12), raw = get32(section + 16), offset = get32(section + 20);
        if (!extent(va, raw, image_size) || !extent(offset, raw, SOS_FILE_SIZE)) return 1;
        if (raw) memcpy(image + va, bytes + offset, raw);
    }
    uint32_t delta = (uint32_t)(uintptr_t)image - link_base;
    uint32_t reloc = get32(opt + 96 + 5 * 8), reloc_size = get32(opt + 96 + 5 * 8 + 4);
    relocations_applied = 0;
    if (!extent(reloc, reloc_size, image_size)) return 1;
    for (uint32_t at = reloc; at + 8 <= reloc + reloc_size;) {
        uint32_t page = get32(image + at), block = get32(image + at + 4);
        if (block < 8 || at + block > reloc + reloc_size) return 1;
        for (uint32_t k = 8; k + 2 <= block; k += 2) {
            uint16_t entry = get16(image + at + k);
            if ((entry >> 12) == 0) continue;
            if ((entry >> 12) != 3 || !extent(page + (entry & 0xfffu), 4, image_size)) return 1;
            if (delta) { BYTE *t = image + page + (entry & 0xfffu); put32(t, get32(t) + delta); }
            ++relocations_applied;
        }
        at += block;
    }
    if (!trap_stubs) {
        trap_stubs = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!trap_stubs) return 1;
    }
    DWORD previous;
    VirtualProtect(trap_stubs, 4096, PAGE_READWRITE, &previous);
    trap_count = 0;
    uint32_t imports = get32(opt + 96 + 8);
    for (uint32_t d = imports; get32(image + d + 12); d += 20) {
        uint32_t lookup = get32(image + d), iat = get32(image + d + 16);
        if (!lookup) lookup = iat;
        const char *dll = (const char *)image + get32(image + d + 12);
        for (unsigned slot = 0; get32(image + lookup + 4 * slot); ++slot) {
            uint32_t entry = get32(image + lookup + 4 * slot);
            if (trap_count >= 160u || (entry & 0x80000000u)) return 1;
            char name[64];
            snprintf(name, sizeof(name), "%s!%s", dll, (const char *)image + entry + 2);
            void *fn = NULL;
            for (unsigned b = 0; b < sizeof(bindings) / sizeof(bindings[0]); ++b)
                if (!_stricmp(bindings[b].name, name)) fn = bindings[b].fn;
            unsigned id = trap_count++;
            strcpy(trap_names[id], name);
            BYTE *s = trap_stubs + 16 * id;
            s[0] = 0x68; put32(s + 1, id); s[5] = 0xe8;
            put32(s + 6, (uint32_t)(uintptr_t)import_trap - (uint32_t)(uintptr_t)(s + 10)); s[10] = 0xcc;
            put32(image + iat + 4 * slot, fn ? (uint32_t)(uintptr_t)fn : (uint32_t)(uintptr_t)s);
        }
    }
    if (!VirtualProtect(trap_stubs, 4096, PAGE_EXECUTE_READ, &previous)) return 1;
    section = image + pe + 24 + get16(image + pe + 20);
    for (unsigned i = 0; i < get16(image + pe + 6); ++i, section += 40)
        if (get32(section + 36) & 0x20000000u)
            if (!VirtualProtect(image + get32(section + 12), get32(section + 8), PAGE_EXECUTE_READ, &previous)) return 1;
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    return 0;
}
static void *dll_export(const char *name) {
    uint32_t pe = get32(image + 0x3c), dir = get32(image + pe + 24 + 96);
    uint32_t count = get32(image + dir + 24), functions = get32(image + dir + 28), names = get32(image + dir + 32), ordinals = get32(image + dir + 36);
    for (uint32_t i = 0; i < count; ++i)
        if (!strcmp((const char *)image + get32(image + names + 4 * i), name))
            return image + get32(image + functions + 4 * get16(image + ordinals + 2 * i));
    fprintf(stdout, "no export %s\n", name);
    ExitProcess(5);
}
typedef uint32_t (WINAPI *fn0)(void);
typedef uint32_t (WINAPI *fn1)(uint32_t);
typedef uint32_t (WINAPI *fn2)(uint32_t, uint32_t);
typedef uint32_t (WINAPI *fn3)(uint32_t, uint32_t, uint32_t);
static uint32_t dll_call(const char *name, unsigned n, uint32_t a0, uint32_t a1, uint32_t a2) {
    void *f = dll_export(name);
    uint32_t r;
    switch (n) {
    case 0: r = ((fn0)f)(); break;
    case 1: r = ((fn1)f)(a0); break;
    case 2: r = ((fn2)f)(a0, a1); break;
    default: r = ((fn3)f)(a0, a1, a2); break;
    }
    run_pending();
    return r;
}
static BYTE *dll_driver(void) { return image + SOS_DRIVER; }
static uint32_t dll_outstanding(void) { return get16(dll_driver() + 0x87e); }
static BYTE *dll_voice(uint32_t h) { return (BYTE *)(uintptr_t)get32(dll_driver() + 0x50) + SOS_VOICE_SIZE * (h & 0xffffu); }

/* ---- the shared arena: caps records, handle words, sample structures, data */
#define ARENA_SIZE 0x00400000u
#define A_CAPS 0x0000u
#define B_CAPS 0x0100u
#define A_HDRV 0x0200u
#define B_HDRV 0x0204u
#define A_HSMP 0x0210u
#define B_HSMP 0x0214u
#define POOL 12u
#define A_SMP(i) (0x1000u + 0x100u * (uint32_t)(i))
#define B_SMP(i) (0x3000u + 0x100u * (uint32_t)(i))
#define DATA(i) (0x10000u + 0x20000u * (uint32_t)(i))
#define DATA_MAX 0x1fff0u
static uint8_t *arena_bytes;
static uint32_t arena_base;
static es2_sim_arena arena;
static es2_guest_memory memory;

/* ---- es2sim's side */
static es2_vt vt;
static es2_sim_sos_mixer mixer;
static es2_sim_audio_device device;
static struct {
    uint32_t rate, channels, bits, opens, closes, resets, flushes, queued_buffers, block_seen;
    buflog writes;
    uint32_t flush_at[1024], flush_n;
} sink;
static uint32_t k_open(void *c, uint32_t rate, uint32_t channels, uint32_t bits) {
    (void)c; ++sink.opens; sink.rate = rate; sink.channels = channels; sink.bits = bits; sink.queued_buffers = 0;
    return 0;
}
static uint32_t k_write(void *c, const uint8_t *d, uint32_t n) {
    (void)c;
    log_add(&sink.writes, d, n);
    sink.block_seen = n;
    ++sink.queued_buffers;
    return 0;
}
static uint32_t k_queued(void *c) { (void)c; return sink.queued_buffers * mixer.block; }
static void k_reset(void *c) { (void)c; ++sink.resets; sink.queued_buffers = 0; }
static void k_close(void *c) { (void)c; ++sink.closes; }
/* Every buffer es2sim flushes after, in every scenario, is silent. */
static uint32_t flushes_seen, flushes_sounding;
static void k_flush(void *c) {
    (void)c;
    ++flushes_seen;
    for (uint32_t k = 0; k < sink.writes.len[sink.writes.n - 1u]; ++k)
        if (sink.writes.data[sink.writes.n - 1u][k] != (sink.bits == 8u ? 0x80u : 0u)) { ++flushes_sounding; break; }
    if (sink.flush_n < 1024u) sink.flush_at[sink.flush_n] = sink.writes.n - 1u;
    ++sink.flush_n;
    ++sink.flushes;
}
static int k_is_window(void *u, uint32_t w) { (void)u; return w != 0xdeadu; }
static int k_post(void *u, uint32_t window, uint32_t message, uint32_t wparam, uint32_t lparam) {
    (void)u; post_add(&sim_posts, window, message, wparam, lparam); return 0;
}
static uint32_t sim_call(es2_sim_sos_call k, uint32_t a0, uint32_t a1, uint32_t a2) {
    uint32_t a[3] = {a0, a1, a2};
    return device.sos(device.context, &memory, k, a, 3);
}

/* ---- comparison */
static uint32_t compared_writes, compared_posts, total_writes, total_posts;
static uint64_t compared_bytes;
static const char *op_name;
static unsigned op_index;
/* "removal first": the two orders part at the end of a drain (see
 * tolerant_finish), so the calls are not compared one by one; the
 * scenario's audible output is compared at its end instead. */
static int tolerant, tolerant_stuck;
/* ES2_SOS_DIFFERENTIAL_TRACE=<seed>: every call of that random seed, both sides (SOS9503/es2sim). */
static int trace;
static uint32_t trace_seed;
static uint32_t tolerant_runs, tolerant_eax_differences, tolerant_extra_dll, tolerant_extra_sim, tolerant_scenario_answers, gap_drains_this;
static void compare_state(void) {
    if (scenario_failed) return;
    if (tolerant) {
        if (calls_68) failf("op %u (%s): the voice callback +68h was called %u times", op_index, op_name, calls_68);
        return;
    }
    if (wave.writes.n != sink.writes.n) {
        failf("op %u (%s): buffers written: SOS9503 %u, es2sim %u", op_index, op_name, wave.writes.n, sink.writes.n);
        return;
    }
    for (uint32_t i = compared_writes; i < wave.writes.n; ++i) {
        if (wave.writes.len[i] != sink.writes.len[i]) {
            failf("op %u (%s): buffer %u length SOS9503 %u, es2sim %u", op_index, op_name, i, wave.writes.len[i], sink.writes.len[i]);
            return;
        }
        if (memcmp(wave.writes.data[i], sink.writes.data[i], wave.writes.len[i])) {
            uint32_t k = 0;
            while (wave.writes.data[i][k] == sink.writes.data[i][k]) ++k;
            failf("op %u (%s): buffer %u differs from byte %u: SOS9503 %02x, es2sim %02x", op_index, op_name, i, k,
                  wave.writes.data[i][k], sink.writes.data[i][k]);
            return;
        }
        compared_bytes += wave.writes.len[i];
    }
    total_writes += wave.writes.n - compared_writes;
    compared_writes = wave.writes.n;
    if (dll_posts.n != sim_posts.n) {
        failf("op %u (%s): notices posted: SOS9503 %u, es2sim %u", op_index, op_name, dll_posts.n, sim_posts.n);
        return;
    }
    for (uint32_t i = compared_posts; i < dll_posts.n && i < 4096u; ++i)
        if (memcmp(dll_posts.w[i], sim_posts.w[i], sizeof(dll_posts.w[i]))) {
            failf("op %u (%s): notice %u: SOS9503 %x %x %x %x, es2sim %x %x %x %x", op_index, op_name, i, dll_posts.w[i][0],
                  dll_posts.w[i][1], dll_posts.w[i][2], dll_posts.w[i][3], sim_posts.w[i][0], sim_posts.w[i][1], sim_posts.w[i][2],
                  sim_posts.w[i][3]);
            return;
        }
    total_posts += dll_posts.n - compared_posts;
    compared_posts = dll_posts.n;
    if (wave.open && mixer.opened && dll_outstanding() != mixer.outstanding)
        failf("op %u (%s): outstanding SOS9503 %u, es2sim %u", op_index, op_name, dll_outstanding(), mixer.outstanding);
    if (calls_68) failf("op %u (%s): the voice callback +68h was called %u times", op_index, op_name, calls_68);
}
static void same_eax(uint32_t a, uint32_t b) {
    if (tolerant) { tolerant_eax_differences += a != b; tolerant_scenario_answers += a != b; return; }
    if (!scenario_failed && a != b) failf("op %u (%s): EAX SOS9503 %08x, es2sim %08x", op_index, op_name, a, b);
}

static int silent_buffer(const buflog *l, uint32_t i, uint32_t bits) {
    for (uint32_t k = 0; k < l->len[i]; ++k) if (l->data[i][k] != (bits == 8u ? 0x80u : 0u)) return 0;
    return 1;
}
static int post_order(const void *x, const void *y) { return memcmp(x, y, sizeof(uint32_t[4])); }
/* The scenario's output without the all-silent buffers, buffer for buffer,
 * and its notices. A strict scenario (one voice, two or more buffers) must
 * match, with es2sim writing one silent buffer more per drain. Otherwise the
 * kind of difference is counted: none; the notices only in their order;
 * the same sounding buffers with other notices or answers; other sounding
 * buffers. tolerant_stuck (one buffer): the notices are not compared. */
static int tolerant_strict;
static uint32_t tolerant_stuck_runs, tolerant_same, tolerant_order_only, tolerant_answers, tolerant_sound, tolerant_strict_runs;
static uint32_t sorted_a[4096][4], sorted_b[4096][4];
static void tolerant_finish(void) {
    if (!tolerant || scenario_failed) { tolerant = 0; return; }
    tolerant = 0;
    ++tolerant_runs;
    uint32_t bits = sink.bits, i = 0, j = 0, silent_a = 0, silent_b = 0;
    int sound_differs = 0;
    for (;;) {
        while (i < wave.writes.n && silent_buffer(&wave.writes, i, bits)) { ++i; ++silent_a; }
        while (j < sink.writes.n && silent_buffer(&sink.writes, j, bits)) { ++j; ++silent_b; }
        if (i == wave.writes.n || j == sink.writes.n) break;
        if (wave.writes.len[i] != sink.writes.len[j] || memcmp(wave.writes.data[i], sink.writes.data[j], wave.writes.len[i])) {
            sound_differs = 1;
            break;
        }
        compared_bytes += wave.writes.len[i];
        ++i; ++j;
    }
    if (!sound_differs && (i != wave.writes.n || j != sink.writes.n)) sound_differs = 1;
    int posts_same = 1, posts_order_only = 0;
    uint32_t n = dll_posts.n < 4096u ? dll_posts.n : 4096u;
    if (!tolerant_stuck && (dll_posts.n != sim_posts.n || memcmp(dll_posts.w, sim_posts.w, sizeof(dll_posts.w[0]) * n))) {
        posts_same = 0;
        if (dll_posts.n == sim_posts.n) {
            memcpy(sorted_a, dll_posts.w, sizeof(sorted_a[0]) * n);
            memcpy(sorted_b, sim_posts.w, sizeof(sorted_b[0]) * n);
            qsort(sorted_a, n, sizeof(sorted_a[0]), post_order);
            qsort(sorted_b, n, sizeof(sorted_b[0]), post_order);
            posts_order_only = !memcmp(sorted_a, sorted_b, sizeof(sorted_a[0]) * n);
        }
    }
    if (tolerant_strict) {
        ++tolerant_strict_runs;
        if (sound_differs) failf("removal first: the sounding buffers differ (SOS9503 #%u, es2sim #%u)", i, j);
        else if (!posts_same) failf("removal first: the notices differ (SOS9503 %u, es2sim %u)", dll_posts.n, sim_posts.n);
        else if (tolerant_scenario_answers) failf("removal first: %u answers differ", tolerant_scenario_answers);
        else if (silent_b != silent_a + gap_drains_this)
            failf("removal first: es2sim wrote %u silent buffers, SOS9503 %u, drains %u", silent_b, silent_a, gap_drains_this);
        return;
    }
    if (tolerant_stuck) {
        if (sound_differs) failf("removal first, one buffer: the sounding buffers differ (SOS9503 #%u, es2sim #%u)", i, j);
        else ++tolerant_stuck_runs;
        return;
    }
    if (sound_differs) {
        ++tolerant_sound;
        if (tolerant_sound <= 12u) printf("  note [%s]: sounding buffers part at SOS9503 #%u, es2sim #%u\n", scenario_name, i, j);
    }
    else if (!posts_same && !posts_order_only) ++tolerant_answers;
    else if (!posts_same) ++tolerant_order_only;
    else if (tolerant_scenario_answers) ++tolerant_answers;
    else ++tolerant_same;
    if (silent_a > silent_b) tolerant_extra_dll += silent_a - silent_b;
    else tolerant_extra_sim += silent_b - silent_a;
}

/* ---- the sides together */
static uint8_t *A(uint32_t off) { return arena_bytes + off; }
static uint32_t G(uint32_t off) { return arena_base + off; }

typedef struct voice_spec { uint32_t length, volume, loops, rate, bits, channels, pan, flags24; } voice_spec;
static voice_spec pool[POOL];
static void write_sample(unsigned i, const voice_spec *v) {
    for (unsigned side = 0; side < 2; ++side) {
        uint8_t *s = A(side ? B_SMP(i) : A_SMP(i));
        memset(s, 0, 0x100);
        put32(s + 0x00, G(DATA(i)));
        put32(s + 0x10, v->length);
        put32(s + 0x24, v->flags24);
        put32(s + 0x30, v->volume);
        put32(s + 0x34, v->loops);
        put32(s + 0x38, v->rate);
        put32(s + 0x3c, v->bits);
        put32(s + 0x40, v->channels);
        put32(s + 0x44, v->bits == 8u ? 0x8000u : 0u);
        put32(s + 0x48, v->pan);
    }
    pool[i] = *v;
}

static uint32_t rng;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint32_t below(uint32_t n) { return n ? next() % n : 0; }

/* A fresh pair: a new mapping and a new mixer, DBSIM's driver load on both
 * (0x473eac: detect init (device, 0), find first, detect uninit, init
 * system (0, 0), timer init (0); caps +0 device, +70h 1, +4Eh format, +56h
 * window, +5Ah 0, +68h 78h, +6Eh block, +6Ah 40h, +6Ch buffers; init driver;
 * set active window). Returns 0 when both drivers are up. */
static int begin(const char *name, int mode, uint32_t format, uint32_t buffers, uint32_t block, uint32_t window) {
    tolerant_finish();
    snprintf(scenario_name, sizeof(scenario_name), "%s", name);
    scenario_failed = 0;
    ++scenarios;
    thread_mode = mode;
    tolerant = mode == REMOVAL_FIRST;
    tolerant_stuck = mode == REMOVAL_FIRST && buffers == 1u;
    tolerant_strict = 0;
    trace = 0;
    tolerant_scenario_answers = 0;
    gap_drains_this = 0;
    if (th.fiber) DeleteFiber(th.fiber);
    memset(&th, 0, sizeof(th));
    log_clear(&wave.writes);
    buflog keep = wave.writes;
    memset(&wave, 0, sizeof(wave));
    wave.writes = keep;
    log_clear(&sink.writes);
    keep = sink.writes;
    memset(&sink, 0, sizeof(sink));
    sink.writes = keep;
    dll_posts.n = sim_posts.n = 0;
    compared_writes = compared_posts = 0;
    calls_68 = 0;
    original_68 = NULL;
    memset(arena_bytes, 0, 0x8000);
    if (map_dll()) { failf("mapping SOS9503.DLL"); return 1; }
    es2_sim_sos_sink s = {NULL, k_open, k_write, k_queued, k_reset, k_close, k_flush};
    es2_sim_sos_mixer_init(&mixer, &vt, &s);
    es2_sim_sos_mixer_device(&mixer, &device);
    mixer.is_window = k_is_window;
    mixer.post = k_post;
    op_name = "driver load";
    op_index = 0;

    uint32_t ra[6], rb[6];
    ra[0] = dll_call("sosDIGIDetectInit", 2, 1, 0, 0);
    rb[0] = sim_call(ES2_SOS_DETECT_INIT, 1, 0, 0);
    ra[1] = dll_call("sosDIGIDetectFindFirst", 1, G(A_CAPS), 0, 0);
    rb[1] = sim_call(ES2_SOS_DETECT_FIND_FIRST, G(B_CAPS), 0, 0);
    ra[2] = dll_call("sosDIGIDetectUnInit", 0, 0, 0, 0);
    rb[2] = sim_call(ES2_SOS_DETECT_UNINIT, 0, 0, 0);
    ra[3] = dll_call("sosDIGIInitSystem", 2, 0, 0, 0);
    rb[3] = sim_call(ES2_SOS_INIT_SYSTEM, 0, 0, 0);
    ra[4] = dll_call("sosTIMERInitSystem", 1, 0, 0, 0);
    rb[4] = sim_call(ES2_SOS_TIMER_INIT, 0, 0, 0);
    for (unsigned i = 0; i < 5; ++i) same_eax(ra[i], rb[i]);
    if (memcmp(A(A_CAPS), A(B_CAPS), 0x100)) {
        unsigned k = 0;
        while (A(A_CAPS)[k] == A(B_CAPS)[k]) ++k;
        failf("find first: caps byte +%xh SOS9503 %02x, es2sim %02x", k, A(A_CAPS)[k], A(B_CAPS)[k]);
    }
    for (unsigned side = 0; side < 2; ++side) {
        uint8_t *c = A(side ? B_CAPS : A_CAPS);
        put16(c + 0x00, 1);
        put16(c + 0x70, 1);
        put16(c + 0x4e, format);
        put32(c + 0x56, window);
        put32(c + 0x5a, 0);
        put16(c + 0x68, 0x78);
        put16(c + 0x6e, block);
        put16(c + 0x6a, 0x40);
        put16(c + 0x6c, buffers);
        put16(A(side ? B_HDRV : A_HDRV), 0xffff);
    }
    op_name = "init driver";
    ra[5] = dll_call("sosDIGIInitDriver", 2, G(A_CAPS), G(A_HDRV), 0);
    rb[5] = sim_call(ES2_SOS_INIT_DRIVER, G(B_CAPS), G(B_HDRV), 0);
    same_eax(ra[5], rb[5]);
    if (get16(A(A_HDRV)) != get16(A(B_HDRV))) failf("init driver: handle SOS9503 %x, es2sim %x", get16(A(A_HDRV)), get16(A(B_HDRV)));
    if (ra[5] || rb[5]) return 1;
    if (wave.format.wFormatTag != WAVE_FORMAT_PCM || wave.format.nSamplesPerSec != sink.rate || wave.format.nChannels != sink.channels ||
        wave.format.wBitsPerSample != sink.bits)
        failf("format: SOS9503 waveOutOpen %u Hz %u ch %u bits (tag %u), es2sim open %u Hz %u ch %u bits", (unsigned)wave.format.nSamplesPerSec,
              wave.format.nChannels, wave.format.wBitsPerSample, wave.format.wFormatTag, sink.rate, sink.channels, sink.bits);
    if (wave.format.nBlockAlign != wave.format.nChannels * wave.format.wBitsPerSample / 8u ||
        wave.format.nAvgBytesPerSec != wave.format.nSamplesPerSec * wave.format.nBlockAlign)
        failf("format: SOS9503 block align %u, bytes/s %u", wave.format.nBlockAlign, (unsigned)wave.format.nAvgBytesPerSec);
    if (get16(dll_driver() + 0x62) != mixer.buffers || get16(dll_driver() + 0x64) != mixer.block)
        failf("ring: SOS9503 %u buffers of %u bytes, es2sim %u of %u", get16(dll_driver() + 0x62), get16(dll_driver() + 0x64), mixer.buffers, mixer.block);
    op_name = "set active window";
    ra[0] = dll_call("sosDIGISetActiveWindow", 2, 0, window, 0);
    rb[0] = sim_call(ES2_SOS_SET_ACTIVE_WINDOW, 0, window, 0);
    same_eax(ra[0], rb[0]);
    compare_state();
    return scenario_failed;
}

/* The operations, each on both sides, then compared. */
enum { OP_START, OP_PLAY, OP_STOP, OP_STOP_ALL, OP_DONE, OP_VOLUME, OP_PAN, OP_RATE, OP_SERVICE, OP_RELEASE, OP_ACQUIRE, OP_UNINIT };
static const char *const op_names[] = {"start", "play", "stop", "stop all", "done", "volume", "pan", "rate", "user service", "release", "acquire", "uninit driver"};
static uint32_t started[256], started_n;
static int released;

static void play_dll(void) {
    if (!wave.count) return;
    WAVEHDR *hdr = wave.queue[wave.head];
    wave.head = (wave.head + 1u) & 255u;
    --wave.count;
    wave_return(hdr);
    run_pending();
}
static void play_sim(void) {
    if (!sink.queued_buffers) return;
    --sink.queued_buffers;
    es2_sim_sos_pump(&mixer, &memory);
}
/* Wraps the new voice's +68h after a start. */
static void wrap_68(uint32_t h) {
    BYTE *v = dll_voice(h);
    uint32_t cb = get32(v + 0x68), want = (uint32_t)(uintptr_t)image + SOS_DEFAULT_68;
    if (cb == (uint32_t)(uintptr_t)record_68) return;
    if (cb != want) { failf("op %u: voice %u +68h = %08x, not the default 0x40421c", op_index, h, cb); return; }
    original_68 = (voice_callback)(uintptr_t)want;
    put32(v + 0x68, (uint32_t)(uintptr_t)record_68);
}
static uint32_t release_limit_hit;
static void op(unsigned kind, uint32_t a, uint32_t b) {
    ++op_index;
    ++ops_run;
    op_name = op_names[kind];
    uint32_t ra = 0, rb = 0;
    switch (kind) {
    case OP_START:
        put16(A(A_HSMP), 0xeeee);
        put16(A(B_HSMP), 0xeeee);
        ra = dll_call("sosDIGIStartSample", 3, 0, G(A_SMP(a)), G(A_HSMP));
        rb = sim_call(ES2_SOS_START_SAMPLE, 0, G(B_SMP(a)), G(B_HSMP));
        same_eax(ra, rb);
        if (tolerant) tolerant_scenario_answers += get16(A(A_HSMP)) != get16(A(B_HSMP));
        else if (get16(A(A_HSMP)) != get16(A(B_HSMP))) failf("op %u (start): handle SOS9503 %x, es2sim %x", op_index, get16(A(A_HSMP)), get16(A(B_HSMP)));
        if (!ra) {
            wrap_68(get16(A(A_HSMP)));
            if (started_n < 256u) started[started_n++] = get16(A(A_HSMP));
        }
        break;
    case OP_PLAY:
        for (uint32_t i = 0; i < a; ++i) { play_dll(); play_sim(); }
        break;
    case OP_STOP:
        ra = dll_call("sosDIGIStopSample", 2, 0, a, 0);
        rb = sim_call(ES2_SOS_STOP_SAMPLE, 0, a, 0);
        same_eax(ra, rb);
        break;
    case OP_STOP_ALL:
        ra = dll_call("sosDIGIStopAllSamples", 1, 0, 0, 0);
        rb = sim_call(ES2_SOS_STOP_ALL, 0, 0, 0);
        same_eax(ra, rb);
        break;
    case OP_DONE:
        ra = dll_call("sosDIGISampleDone", 2, 0, a, 0);
        rb = sim_call(ES2_SOS_SAMPLE_DONE, 0, a, 0);
        same_eax(ra, rb);
        break;
    case OP_VOLUME:
        ra = dll_call("sosDIGISetSampleVolume", 3, 0, a, b);
        rb = sim_call(ES2_SOS_SET_VOLUME, 0, a, b);
        same_eax(ra, rb);
        break;
    case OP_PAN:
        ra = dll_call("sosDIGISetPanLocation", 3, 0, a, b);
        rb = sim_call(ES2_SOS_SET_PAN, 0, a, b);
        same_eax(ra, rb);
        break;
    case OP_RATE:
        ra = dll_call("sosDIGISetSampleRate", 3, 0, a, b);
        rb = sim_call(ES2_SOS_SET_RATE, 0, a, b);
        same_eax(ra, rb);
        break;
    case OP_SERVICE:
        ra = dll_call("sosDIGIUserService", 1, 0, 0, 0);
        rb = sim_call(ES2_SOS_USER_SERVICE, 0, 0, 0);
        same_eax(ra, rb);
        break;
    case OP_RELEASE: {
        /* The original spins on +87Eh until the device has returned every
         * buffer (0x403c73); the device plays them here first. */
        unsigned n = 0;
        while (dll_outstanding() && n < 4u * 64u + 4u) { play_dll(); ++n; }
        if (dll_outstanding()) { ++release_limit_hit; scenario_failed = 2; tolerant = 0; return; }
        ra = dll_call("sosDIGIReleaseDriver", 2, 0, 0x1000, 0);
        rb = sim_call(ES2_SOS_RELEASE_DRIVER, 0, 0x1000, 0);
        same_eax(ra, rb);
        released = 1;
        break;
    }
    case OP_ACQUIRE:
        ra = dll_call("sosDIGIAcquireDriver", 1, 0, 0, 0);
        rb = sim_call(ES2_SOS_ACQUIRE_DRIVER, 0, 0, 0);
        same_eax(ra, rb);
        released = 0;
        break;
    case OP_UNINIT:
        ra = dll_call("sosDIGIUnInitDriver", 1, 0, 0, 0);
        rb = sim_call(ES2_SOS_UNINIT_DRIVER, 0, 0, 0);
        same_eax(ra, rb);
        break;
    default: break;
    }
    if (trace)
        printf("  op %u %s %x %x: EAX %x/%x; written %u/%u, outstanding %u/%u, listed -/%u, notices %u/%u\n", op_index, op_name, a, b, ra, rb,
               wave.writes.n, sink.writes.n, wave.open ? dll_outstanding() : 0u, mixer.outstanding, mixer.list_n, dll_posts.n, sim_posts.n);
    compare_state();
}
/* Plays until both rings are empty (at most `limit` buffers). */
static uint32_t drain(uint32_t limit) {
    uint32_t n = 0;
    while ((wave.count || sink.queued_buffers) && n < limit && !scenario_failed) { op(OP_PLAY, 1, 0); ++n; }
    return n;
}
static int all_silent(const uint8_t *d, uint32_t n, uint32_t bits) {
    for (uint32_t i = 0; i < n; ++i) if (d[i] != (bits == 8u ? 0x80u : 0u)) return 0;
    return 1;
}
static uint32_t output_bits(uint32_t format) { return (format & 3u) == 2u ? 16u : 8u; }

static void random_data(unsigned i, uint32_t n) {
    uint8_t *d = A(DATA(i));
    for (uint32_t k = 0; k < n + 16u; ++k) d[k] = (uint8_t)next();
}
static const uint32_t rates[] = {11025u, 22050u, 44100u, 8000u, 11000u, 22000u, 16000u, 32000u, 5512u, 48000u, 22050u, 11025u};
static voice_spec random_voice(void) {
    voice_spec v;
    memset(&v, 0, sizeof(v));
    v.bits = below(3) ? 8u : 16u;
    v.channels = below(3) ? 1u : 2u;
    uint32_t frame = v.bits / 8u * v.channels;
    v.length = (1u + below(below(4) ? 0x600u : 0x4000u)) * frame;
    if (!below(10)) v.length += below(frame);   /* a length that is not whole frames */
    if (v.length > DATA_MAX) v.length = DATA_MAX;
    switch (below(5)) {
    case 0: v.volume = 0x7fff7fffu; break;
    case 1: v.volume = below(0x8000u) * 0x10001u; break;
    case 2: v.volume = below(0x8000u) | below(0x8000u) << 16; break;
    case 3: v.volume = next(); break;
    default: v.volume = 0x7ff0u + below(0x10u) | (0x7ff0u + below(0x10u)) << 16; break;
    }
    v.pan = below(2) ? 0x8000u : below(0x10000u);
    switch (below(8)) {
    case 0: v.loops = 1u; break;
    case 1: v.loops = 2u + below(3); break;
    case 2: v.loops = 0xffffffffu; break;
    default: v.loops = 0; break;
    }
    v.rate = rates[below(sizeof(rates) / sizeof(rates[0]))];
    return v;
}
static uint32_t pick_handle(void) {
    if (started_n && below(5)) return started[below(started_n)];
    return below(32);
}

/* A random sequence of DBSIM's calls. */
static void random_scenario(int mode, uint32_t format, uint32_t buffers, uint32_t block, uint32_t seed) {
    char name[160];
    snprintf(name, sizeof(name), "random seed %u, format %02xh, %u x %xh, %s", seed, format, buffers, block, mode ? "removal first" : "refill first");
    rng = seed * 2654435761u + 1u;
    if (!rng) rng = 1;
    started_n = 0;
    released = 0;
    if (begin(name, mode, format, buffers, block, below(8) ? 0x1234u : 0u)) return;
    trace = trace_seed && seed == trace_seed;
    if (trace) printf("%s\n", name);
    for (unsigned i = 0; i < POOL; ++i) {
        voice_spec v = random_voice();
        random_data(i, v.length);
        write_sample(i, &v);
    }
    unsigned count = 30u + below(90);
    for (unsigned n = 0; n < count && !scenario_failed; ++n) {
        uint32_t r = below(100);
        if (released) { op(OP_ACQUIRE, 0, 0); continue; }
        if (r < 30) op(OP_START, below(POOL), 0);
        else if (r < 60) op(OP_PLAY, 1u + below(4), 0);
        else if (r < 66) op(OP_STOP, pick_handle(), 0);
        else if (r < 67) op(OP_STOP_ALL, 0, 0);
        else if (r < 75) op(OP_DONE, pick_handle(), 0);
        else if (r < 80) op(OP_VOLUME, pick_handle(), below(2) ? next() : below(0x8000u) * 0x10001u);
        else if (r < 84) op(OP_PAN, pick_handle(), below(0x10000u));
        else if (r < 87) op(OP_RATE, pick_handle(), rates[below(sizeof(rates) / sizeof(rates[0]))]);
        else if (r < 97) op(OP_SERVICE, 0, 0);
        else if (r < 99) { op(OP_STOP_ALL, 0, 0); op(OP_RELEASE, 0, 0); }   /* as DBSIM's pause 0x46317c: its voices stopped, then the release */
        else op(OP_PLAY, 8u, 0);
    }
    if (scenario_failed == 2) { scenario_failed = 0; return; }   /* the original would spin in its release */
    if (released && !scenario_failed) op(OP_ACQUIRE, 0, 0);
    if (!scenario_failed) op(OP_STOP_ALL, 0, 0);
    drain(4u * buffers + 64u);
    for (uint32_t h = 0; h < 32 && !scenario_failed; ++h) op(OP_DONE, h, 0);
    if (!scenario_failed) op(OP_UNINIT, 0, 0);
}

/* ---- the named cases */
static const uint32_t cfg_formats[] = {0x15u, 0x19u, 0x25u, 0x29u};   /* DBSIM's sound.cfg codes (Rate 11/22 x Mono/Stereo, 8-bit) */
static const uint32_t cfg_buffers[] = {1u, 2u, 3u, 5u, 8u, 16u, 64u};
static unsigned gap_buffers_checked, second_start_cases, order_cases;

/* One voice, played out; the ring must run dry with a silent last buffer
 * (es2sim flushes after exactly that buffer), the voice removed. */
static void case_one_voice(int mode, uint32_t format, uint32_t buffers, uint32_t block, const voice_spec *v, int expect_removed) {
    char name[160];
    snprintf(name, sizeof(name), "one voice: format %02xh, %u x %xh, %u bytes %u-bit %u ch %u Hz loops %x, %s", format, buffers, block,
             v->length, v->bits, v->channels, v->rate, v->loops, mode ? "removal first" : "refill first");
    rng = format * 977u + buffers * 31u + block + v->length;
    started_n = 0;
    if (begin(name, mode, format, buffers, block, 0x1234u)) return;
    tolerant_strict = mode == REMOVAL_FIRST && buffers > 1u;
    random_data(0, v->length);
    write_sample(0, v);
    op(OP_START, 0, 0);
    uint32_t h = get16(A(A_HSMP));
    drain(4096u);
    ++gap_drains_this;
    if (scenario_failed) return;
    uint32_t done_a = dll_call("sosDIGISampleDone", 2, 0, h, 0);
    if (expect_removed) {
        op(OP_DONE, h, 0);
        if (!scenario_failed && done_a != 1u) failf("the voice was not removed (SampleDone %u)", done_a);
        if (!scenario_failed && wave.count) failf("the ring did not run dry");
        /* The gap after the flush: the last buffer written before the ring
         * ran dry is the one es2sim flushes after, and it is silent. */
        if (!scenario_failed) {
            if (!sink.flush_n || sink.flush_at[sink.flush_n - 1u] != sink.writes.n - 1u)
                failf("es2sim did not flush after the last buffer (%u flushes)", sink.flush_n);
            else if (!all_silent(wave.writes.data[wave.writes.n - 1u], wave.writes.len[wave.writes.n - 1u], output_bits(format)))
                failf("the last SOS9503 buffer before the ring ran dry is not silent");
            else ++gap_buffers_checked;
        }
        /* A start after the drain primes again on both sides. */
        if (!scenario_failed) {
            uint32_t before = wave.writes.n;
            op(OP_START, 0, 0);
            if (!scenario_failed && wave.writes.n - before != buffers) failf("the start after the drain wrote %u buffers, not %u", wave.writes.n - before, buffers);
            drain(4096u);
            ++gap_drains_this;
        }
    } else {
        /* removal first, one buffer: the original keeps the voice listed. */
        if (done_a != 0u) failf("expected the voice to stay listed with one buffer and the removal first");
        else if (!wave.count) failf("expected the ring to keep running");
        else if (order_cases == 0u)
            printf("  [%s] SOS9503 keeps the ended voice listed: SampleDone 0, ring running, outstanding %u, written %u\n", name,
                   dll_outstanding(), (unsigned)get32(dll_driver() + 0x992));
        return;
    }
    if (!scenario_failed) op(OP_UNINIT, 0, 0);
}

/* A second sosDIGIStartSample with the same sample structure while the
 * first voice still plays: a second voice, its own handle, both mixed. */
static void case_second_start(int mode, uint32_t format, uint32_t buffers, uint32_t gap) {
    char name[160];
    snprintf(name, sizeof(name), "second start of a playing sample: format %02xh, %u buffers, after %u plays, %s", format, buffers, gap,
             mode ? "removal first" : "refill first");
    rng = 0x5eedu + format + buffers * 7u + gap;
    started_n = 0;
    if (begin(name, mode, format, buffers, 0x200u, 0x1234u)) return;
    voice_spec v = {0x3000u, 0x7fff7fffu, 0, 22050u, 8u, 1u, 0x8000u, 0};
    random_data(0, v.length);
    write_sample(0, &v);
    op(OP_START, 0, 0);
    uint32_t h0 = get16(A(A_HSMP));
    op(OP_PLAY, gap, 0);
    op(OP_START, 0, 0);
    uint32_t h1 = get16(A(A_HSMP));
    if (!scenario_failed && h1 == h0) failf("the second start reused handle %u", h0);
    op(OP_DONE, h0, 0);
    op(OP_PLAY, 3, 0);
    op(OP_START, 0, 0);
    drain(4096u);
    op(OP_DONE, h0, 0);
    op(OP_DONE, h1, 0);
    if (!scenario_failed) op(OP_UNINIT, 0, 0);
    if (!scenario_failed) ++second_start_cases;
}

/* Release (0x1000) and acquire around playing voices, as DBSIM's pause. */
static void case_pause(int mode, uint32_t format, uint32_t buffers) {
    char name[160];
    snprintf(name, sizeof(name), "pause: format %02xh, %u buffers, %s", format, buffers, mode ? "removal first" : "refill first");
    rng = 0xb00u + format + buffers;
    started_n = 0;
    if (begin(name, mode, format, buffers, 0x200u, 0x1234u)) return;
    voice_spec v = {0x2000u, 0x40004000u, 0, 11025u, 8u, 1u, 0x8000u, 0}, w = {0x1800u, 0x7fff7fffu, 0xffffffffu, 22050u, 16u, 2u, 0x3000u, 0};
    random_data(0, v.length);
    write_sample(0, &v);
    random_data(1, w.length);
    write_sample(1, &w);
    op(OP_START, 0, 0);
    op(OP_START, 1, 0);
    op(OP_PLAY, 2, 0);
    op(OP_STOP, get16(A(A_HSMP)), 0);   /* the endless voice stops, as the pause stops its voices */
    op(OP_RELEASE, 0, 0);
    if (scenario_failed == 2) { failf("release: the original would spin"); return; }
    op(OP_SERVICE, 0, 0);
    op(OP_DONE, 0, 0);
    op(OP_ACQUIRE, 0, 0);
    op(OP_START, 1, 0);
    op(OP_PLAY, 3, 0);
    op(OP_STOP_ALL, 0, 0);
    drain(4096u);
    if (!scenario_failed) op(OP_UNINIT, 0, 0);
}

/* A start while the ring still drains (the start does not prime), then the
 * user service before the next buffer comes back. */
static void case_start_while_draining(int mode, uint32_t buffers) {
    char name[160];
    snprintf(name, sizeof(name), "start while the ring drains, then the user service: %u buffers, %s", buffers, mode ? "removal first" : "refill first");
    rng = 0xd1u + buffers;
    started_n = 0;
    if (begin(name, mode, 0x29u, buffers, 0x200u, 0x1234u)) return;
    voice_spec v = {0x40u, 0x7fff7fffu, 0, 22050u, 8u, 1u, 0x8000u, 0};
    random_data(0, v.length);
    write_sample(0, &v);
    random_data(1, 0x3000u);
    voice_spec w = {0x3000u, 0x7fff7fffu, 0, 22050u, 8u, 1u, 0x8000u, 0};
    write_sample(1, &w);
    op(OP_START, 0, 0);
    /* play until the list is empty and the ring drains, but not dry */
    for (unsigned n = 0; n < 64u && !scenario_failed && wave.count > 1u; ++n) op(OP_PLAY, 1, 0);
    op(OP_START, 1, 0);
    op(OP_SERVICE, 0, 0);
    op(OP_VOLUME, get16(A(A_HSMP)), 0x10001000u);
    op(OP_SERVICE, 0, 0);
    drain(4096u);
    if (!scenario_failed) op(OP_UNINIT, 0, 0);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) { printf("SKIP: ES2_DATA_ROOT not set\n"); return 0; }
    char probe[MAX_PATH];
    snprintf(probe, sizeof(probe), "%s/VER95/SOS9503.DLL", root);
    char probe2[MAX_PATH];
    snprintf(probe2, sizeof(probe2), "%s/extracted/cd/VER95/SOS9503.DLL", root);
    if (GetFileAttributesA(probe) == INVALID_FILE_ATTRIBUTES && GetFileAttributesA(probe2) == INVALID_FILE_ATTRIBUTES) {
        printf("SKIP: no VER95/SOS9503.DLL under ES2_DATA_ROOT\n");
        return 0;
    }
    if (read_dll(root)) return 1;
    main_fiber = ConvertThreadToFiber(NULL);
    if (!main_fiber) { printf("ConvertThreadToFiber failed\n"); return 1; }
    arena_bytes = VirtualAlloc(NULL, ARENA_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!arena_bytes) return 1;
    arena_base = (uint32_t)(uintptr_t)arena_bytes;
    es2_vt_enable(&vt, 1, 47, 0x01000000u, 0, 1);
    if (!es2_sim_arena_add(&arena, arena_base, arena_bytes, ARENA_SIZE)) return 1;
    es2_sim_arena_memory(&arena, &memory);
    if (map_dll()) { printf("mapping SOS9503.DLL failed\n"); return 1; }
    printf("Mapped SOS9503.DLL at 0x%08x (link base 0x%08x, %u relocations, %u imports, entry point not called)\n",
           (unsigned)(uintptr_t)image, SOS_LINK_BASE, relocations_applied, trap_count);

    /* sound.cfg: every code x buffer count, one voice of each kind. */
    for (unsigned mode = 0; mode < 2; ++mode)
        for (unsigned f = 0; f < 4; ++f)
            for (unsigned b = 0; b < 7; ++b) {
                static const voice_spec kinds[] = {
                    {0x0c00u, 0x7fff7fffu, 0, 22050u, 8u, 1u, 0x8000u, 0},
                    {0x0c00u, 0x30005000u, 1, 11025u, 8u, 2u, 0x2000u, 0},
                    {0x1802u, 0x7fff7fffu, 0, 44100u, 16u, 1u, 0xc000u, 0},
                    {0x0400u, 0x12345678u, 2, 16000u, 16u, 2u, 0x8000u, 0},
                    {0x0001u, 0x7fff7fffu, 0, 22050u, 8u, 1u, 0x8000u, 0},
                };
                for (unsigned k = 0; k < sizeof(kinds) / sizeof(kinds[0]); ++k) {
                    int removed = !(mode == REMOVAL_FIRST && cfg_buffers[b] == 1u);
                    case_one_voice((int)mode, cfg_formats[f], cfg_buffers[b], 0x200u, &kinds[k], removed);
                    if (!removed) ++order_cases;
                }
            }
    /* Other formats the decode accepts (16-bit, 44 kHz) and other blocks. */
    static const uint32_t more_formats[] = {0x16u, 0x1au, 0x26u, 0x2au, 0x45u, 0x49u, 0x46u, 0x4au};
    for (unsigned f = 0; f < 8; ++f)
        for (unsigned b = 0; b < 4; ++b) {
            voice_spec v = {0x0a00u, 0x55554444u, 1, 22050u, (f & 1u) ? 16u : 8u, 1u + (b & 1u), 0x9000u, 0};
            case_one_voice(REFILL_FIRST, more_formats[f], cfg_buffers[b + 1u], b == 3 ? 0x40u : 0x100u, &v, 1);
        }
    for (unsigned mode = 0; mode < 2; ++mode)
        for (unsigned f = 0; f < 4; ++f)
            for (unsigned b = 0; b < 7; ++b)
                for (unsigned g = 0; g < 3; ++g)
                    if (!(mode == REMOVAL_FIRST && cfg_buffers[b] == 1u)) case_second_start((int)mode, cfg_formats[f], cfg_buffers[b], g);
    for (unsigned mode = 0; mode < 2; ++mode)
        for (unsigned f = 0; f < 4; ++f)
            for (unsigned b = 0; b < 7; ++b)
                if (!(mode == REMOVAL_FIRST && cfg_buffers[b] == 1u)) case_pause((int)mode, cfg_formats[f], cfg_buffers[b]);
    for (unsigned mode = 0; mode < 2; ++mode)
        for (unsigned b = 1; b < 7; ++b) case_start_while_draining((int)mode, cfg_buffers[b]);

    unsigned random_runs = 0;
    const char *trace_env = getenv("ES2_SOS_DIFFERENTIAL_TRACE");
    trace_seed = trace_env && *trace_env ? (uint32_t)strtoul(trace_env, NULL, 10) : 0u;
    const char *count_env = getenv("ES2_SOS_DIFFERENTIAL_SEEDS");
    unsigned seeds = count_env && *count_env ? (unsigned)strtoul(count_env, NULL, 10) : 400u;
    for (unsigned seed = 1; seed <= seeds; ++seed) {
        uint32_t f = seed % 3u ? cfg_formats[seed % 4u] : more_formats[seed % 8u];
        uint32_t b = cfg_buffers[seed % 7u];
        if (b == 1u && seed % 2u) b = 2u;
        random_scenario(REFILL_FIRST, f, b, (seed % 5u) ? 0x200u : 0x80u, seed);
        ++random_runs;
        if (b > 1u) { random_scenario(REMOVAL_FIRST, f, b, (seed % 5u) ? 0x200u : 0x80u, seed); ++random_runs; }
    }
    tolerant_finish();
    printf("removal first: %u runs; %u strict one-voice runs equal but for one silent buffer per drain; %u one-buffer runs sound the same, "
           "the voice never removed; of the others: "
           "%u the same, %u notices in another order only, %u the same sounding buffers with other notices or answers, "
           "%u other sounding buffers; extra silent buffers SOS9503 %u, es2sim %u; %u call answers differ\n",
           tolerant_runs, tolerant_strict_runs, tolerant_stuck_runs, tolerant_same, tolerant_order_only, tolerant_answers, tolerant_sound, tolerant_extra_dll,
           tolerant_extra_sim, tolerant_eax_differences);
    printf("%u scenarios (%u random), %u operations; %u buffers (%llu bytes) and %u notices equal; +68h calls 0; "
           "%u silent last buffers before a dry ring; %u second-start cases; %u one-buffer removal-first cases keep the voice; "
           "%u random runs where the original would spin in its release\n",
           scenarios, random_runs, ops_run, total_writes, (unsigned long long)compared_bytes, total_posts, gap_buffers_checked,
           second_start_cases, order_cases, release_limit_hit);
    printf("es2sim flushed after %u buffers, %u of them not silent\n", flushes_seen, flushes_sounding);
    if (flushes_sounding) failf("es2sim flushed after a sounding buffer");
    if (failures) { printf("FAILED: %u mismatches\n", failures); return 1; }
    printf("PASS\n");
    return 0;
}
