// DBSIM 1.11 in-process tick log. Replaces the 6-byte frame-counter increment
// at the active-loop boundary (DBSIM 0x0045f4ad, docs/reverse-engineering/
// main_loop.md) in memory with a call to an own thunk. The thunk records one
// state line, then performs the original increment with the original flags.
// The executable file and its reference hash remain untouched.
#ifndef ES2_TICK_LOG_H
#define ES2_TICK_LOG_H
#include <stdint.h>
#include "mem_watch.h"
#include "tick_log_io.h"
// The line formatting is the shared library //sim/oracle:tick_record
// (also linked by the own process); this header keeps the site patch, the
// guest reads, the I/O and the proxy-only fields.
#include "sim/oracle/tick_record.h"

#define TICK_LOG_IMAGE_SIZE 0x002d8000u
#define TICK_LOG_SITE_RVA 0x0005f4adu       // inc dword [ebx+0xbf]; jmp loop top
#define TICK_LOG_CTX_SETUP_RVA 0x0005f176u  // mov ebx, g_sim_ctx
#define TICK_LOG_RNG_CORE_RVA 0x00092ee0u   // rng_next (docs/reverse-engineering/rng.md)
#define TICK_LOG_CTX_RVA 0x000d24bcu        // g_sim_ctx; frame counter at +0xbf
#define TICK_LOG_CLOCK_RVA 0x000d3b64u      // timing scales, references, limiter clocks (24 bytes)
#define TICK_LOG_RNG_A_RVA 0x000d2599u
#define TICK_LOG_RNG_B_RVA 0x000d260bu
#define TICK_LOG_RNG_SIZE 0x72u
#define TICK_LOG_ACTOR_SIZE 0x292u
#define TICK_LOG_BUFFER_SIZE 262144u
#define TICK_LOG_LINE_MAX 262144u  // 163 hooks outgrew 32 KiB, 271 hooks 64 KiB, 472 hooks 128 KiB (every counter at UINT64_MAX)
#define TICK_LOG_WATCH_LINE_MAX (4u * MEM_WATCH_MAX_BYTES + 1024u)

static const BYTE tick_log_site_signature[] = {0xff,0x83,0xbf,0x00,0x00,0x00,0xeb,0xa3};
static const BYTE tick_log_rng_signature[] = {0x55,0x8b,0xec,0x8b,0x45,0x08,0x0f,0xbe,0x50,0x70};

static BYTE *tick_log_image;
static HANDLE tick_log_file = INVALID_HANDLE_VALUE;
static char tick_log_buffer[TICK_LOG_BUFFER_SIZE];
static DWORD tick_log_used;
static DWORD tick_log_sequence, tick_log_limit;
static BOOL tick_log_actor_raw;
static BOOL tick_log_compact_rng, tick_log_rng_saved;
static BYTE tick_log_previous_rng[2][TICK_LOG_RNG_SIZE];
static LONG tick_log_state;
/* One formatting buffer for the tick record and the exit record. Both are
 * formatted under the tick I/O lock (or with no other logging thread), so
 * static storage keeps the game's stack use unchanged as the counters grow. */
static char tick_log_line[TICK_LOG_LINE_MAX];
static BOOL tick_log_exit_written;
/* [trace] hold_frame=N: after the N-th tick record the game thread parks
 * inside the thunk, before the frame counter increment at 0x45f4ad and the
 * jump to the loop top 0x45f458, so nothing of the next frame runs. The
 * runner stops the process. 0 (default) never parks. */
static DWORD tick_log_hold;
static volatile LONG tick_log_holding;

static void tick_log_park(void) {
    InterlockedExchange(&tick_log_holding, 1);
    for (;;) Sleep(INFINITE);
}

static BOOL tick_log_readable(const void *pointer, SIZE_T size) {
    uintptr_t cursor = (uintptr_t)pointer;
    if (!cursor || size > UINTPTR_MAX - cursor) return FALSE;
    uintptr_t end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery((void *)cursor, &region, sizeof(region)) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return FALSE;
        uintptr_t next = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (next <= cursor) return FALSE;
        cursor = next;
    }
    return TRUE;
}

static void tick_log_flush(void) {
    if (!tick_log_used || tick_log_file == INVALID_HANDLE_VALUE) return;
#ifdef ES2_ORACLE_PERF_H
    unsigned phase = perf_enabled && GetCurrentThreadId() == perf_thread ? perf_begin(PERF_WRITE) : UINT_MAX;
    BOOL background = perf_enabled && !perf_complete && GetCurrentThreadId() != perf_thread;
    LONGLONG began = background ? perf_now() : 0;
#endif
    DWORD written = 0;
    DWORD offset = 0;
    while (offset < tick_log_used && tick_log_file != INVALID_HANDLE_VALUE) {
        if (!WriteFile(tick_log_file, tick_log_buffer + offset, tick_log_used - offset, &written, NULL) || !written) {
            tick_io_failed = TRUE; break;
        }
        offset += written;
#ifdef ES2_ORACLE_PERF_H
        if (perf_enabled && !perf_complete) {
            EnterCriticalSection(&perf_lock); ++perf_write_calls; perf_write_bytes += written; LeaveCriticalSection(&perf_lock);
        }
#endif
    }
    if (offset) { tick_log_used -= offset; memmove(tick_log_buffer, tick_log_buffer + offset, tick_log_used); }
#ifdef ES2_ORACLE_PERF_H
    perf_end(phase);
    if (background) {
        LONGLONG elapsed = perf_now()-began;
        EnterCriticalSection(&perf_lock);
        perf_worker_write_qpc += (uint64_t)elapsed; ++perf_worker_write_count;
        LeaveCriticalSection(&perf_lock);
    }
#endif
}

static void tick_log_append(const char *text, int length) {
    if (length <= 0) return;
    if (tick_log_used + (DWORD)length > TICK_LOG_BUFFER_SIZE) tick_log_flush();
    if ((DWORD)length > TICK_LOG_BUFFER_SIZE - tick_log_used) { tick_io_failed = TRUE; return; }
    memcpy(tick_log_buffer + tick_log_used, text, (size_t)length);
    tick_log_used += (DWORD)length;
}

static DWORD tick_log_u32(const BYTE *bytes) { DWORD value; memcpy(&value, bytes, 4); return value; }

// Schema 3 is explicitly selected. Changes retain all 114 RNG bytes; no
// digest weakens the evidence. Schema 2 never reads or updates this cache.
static int tick_log_rng_field(char *out, SIZE_T capacity, unsigned bank, const BYTE *bytes) {
    return es2_tick_record_rng(out, capacity, bank, bytes, tick_log_compact_rng, tick_log_rng_saved,
                               tick_log_previous_rng[bank]);
}

// Called by the thunk with the loop's EBX, before the original increment.
// Runs on the game's main thread only; no locking is needed.
void __cdecl es2_tick_record(const BYTE *context) __attribute__((used));
void __cdecl es2_tick_record(const BYTE *context) {
#ifdef ES2_VIRTUAL_TIME_H
    vt_frame_boundary();   // before the state test: playback frames keep their step after the record limit
#endif
    if (tick_log_state != 1) return;
    tick_io_enter();
    if (tick_log_sequence >= tick_log_limit) {
        static const char done[] = "{\"event\":\"limit_reached\"}\r\n";
        tick_log_append(done, (int)sizeof(done) - 1);
        tick_log_flush();
        CloseHandle(tick_log_file);
        tick_log_file = INVALID_HANDLE_VALUE;
        InterlockedExchange(&tick_log_state, 3);
        tick_io_leave();
        return;
    }
#ifdef ES2_ORACLE_PERF_H
    unsigned phase = perf_begin(PERF_FORMAT);
#endif
    /* Main thread only and not reentrant; the static line keeps the game's
     * stack use unchanged while the hook counters grow. */
    char *const line = tick_log_line;
    const size_t line_size = sizeof(tick_log_line);
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    const BYTE *clock = tick_log_image + TICK_LOG_CLOCK_RVA;
    unsigned long sequence = (unsigned long)++tick_log_sequence;
    int n = es2_tick_record_head(line, line_size, tick_log_compact_rng, sequence, (long long)qpc.QuadPart,
                                 (uint32_t)(uintptr_t)context, context, clock);
    n += tick_log_rng_field(line + n, line_size - (size_t)n, 0, tick_log_image + TICK_LOG_RNG_A_RVA);
    n += tick_log_rng_field(line + n, line_size - (size_t)n, 1, tick_log_image + TICK_LOG_RNG_B_RVA);
    if (tick_log_compact_rng) tick_log_rng_saved = TRUE;

    const BYTE *actor = (const BYTE *)(uintptr_t)tick_log_u32(context + 0x2a);
    n += es2_tick_record_actor(line + n, line_size - (size_t)n, (uint32_t)(uintptr_t)actor,
                               actor && tick_log_readable(actor, TICK_LOG_ACTOR_SIZE) ? actor : NULL, tick_log_actor_raw);
#ifdef ES2_VIRTUAL_TIME_H
    if (vt_enabled)
        n += es2_tick_record_vt(line + n, line_size - (size_t)n, vt_intra, (unsigned long)vt_limiter_polls,
                                (unsigned long)vt_stall_advances, (unsigned long)vt_late_polls, (unsigned long)vt_intra_advances);
#endif
#ifdef ES2_INPUT_JOURNAL_H
    if (ij_mode != IJ_OFF)
        n += es2_tick_record_input(line + n, line_size - (size_t)n, ij_mode == IJ_REPLAY, (unsigned long)ij_recorded,
                                   (unsigned long)ij_injected, (unsigned long)ij_dropped, (unsigned long)ij_fallbacks,
                                   (unsigned long)ij_focus_dropped);
#endif
#ifdef ES2_RNG_HOOKS_H
    n += rng_hooks_tick_fields(line + n, line_size - (size_t)n);
#endif
#ifdef ES2_RTL_IMPORTS_H
    n += rtl_tick_fields(line + n, line_size - (size_t)n, FALSE);
#endif
    n += snprintf(line + n, line_size - (size_t)n, "}\r\n");
    tick_log_append(line, n);
#ifdef ES2_ORACLE_PERF_H
    perf_end(phase);
#endif
    if (mem_watch_count) {
        static char watch_line[TICK_LOG_WATCH_LINE_MAX];
        mem_watch_check((unsigned long)tick_log_sequence, (unsigned long)tick_log_u32(context + 0xbf), tick_log_readable,
                        tick_log_append, watch_line, sizeof(watch_line));
    }
    BOOL hold = tick_log_hold && tick_log_sequence == tick_log_hold;
    if (hold) {
        char held[64];
        tick_log_append(held, snprintf(held, sizeof(held), "{\"event\":\"hold\",\"sequence\":%lu}\r\n",
                                       (unsigned long)tick_log_sequence));
    }
    // The default stays immediate. Target publication includes the watches.
    if (!tick_io_buffered || hold || (tick_io_target && tick_log_sequence >= tick_io_target)) tick_log_flush();
    tick_io_leave();
#ifdef ES2_LIVE_COVERAGE_H
    live_coverage_tick(tick_log_u32(context + 0xbf));
#endif
#ifdef ES2_VOL_PROBES_H
    vol_probes_tick(tick_log_u32(context + 0xbf));
#endif
#ifdef ES2_ORACLE_PERF_H
    perf_tick(tick_log_u32(context + 0xbf));
#endif
    // Outside the tick I/O lock: the flush worker still answers the runner.
    if (hold) tick_log_park();
}

// Replaces `inc dword [ebx+0xbf]` (6 bytes). EFLAGS and all registers are
// restored before the original increment runs, so the increment produces the
// original flags. Returns to the original `jmp` after the padding NOP.
__attribute__((naked)) static void tick_log_thunk(void) {
    __asm__ volatile(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl %ebx\n\t"
        "call _es2_tick_record\n\t"
        "addl $4, %esp\n\t"
        "popal\n\t"
        "popfl\n\t"
        "incl 0xbf(%ebx)\n\t"
        "ret\n\t");
}

static BOOL tick_log_image_valid(BYTE *image, uintptr_t expected_base) {
    if ((uintptr_t)image != expected_base || sizeof(void *) != 4 || !tick_log_readable(image, 4096)) return FALSE;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < (LONG)sizeof(*dos) ||
        dos->e_lfanew > 4096 - (LONG)sizeof(IMAGE_NT_HEADERS32)) return FALSE;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.ImageBase != expected_base || nt->OptionalHeader.SizeOfImage != TICK_LOG_IMAGE_SIZE) return FALSE;
    BYTE setup[5] = {0xbb};
    DWORD context = (DWORD)(expected_base + TICK_LOG_CTX_RVA);
    memcpy(setup + 1, &context, sizeof(context));
    return tick_log_readable(image + TICK_LOG_SITE_RVA, sizeof(tick_log_site_signature)) &&
        !memcmp(image + TICK_LOG_SITE_RVA, tick_log_site_signature, sizeof(tick_log_site_signature)) &&
        !memcmp(image + TICK_LOG_RNG_CORE_RVA, tick_log_rng_signature, sizeof(tick_log_rng_signature)) &&
        !memcmp(image + TICK_LOG_CTX_SETUP_RVA, setup, sizeof(setup)) &&
        tick_log_readable(image + TICK_LOG_CLOCK_RVA, 24) &&
        tick_log_readable(image + TICK_LOG_RNG_A_RVA, TICK_LOG_RNG_SIZE) &&
        tick_log_readable(image + TICK_LOG_RNG_B_RVA, TICK_LOG_RNG_SIZE);
}

// Validation and patching are separate from configuration so that the own-code
// fixture can use a synthetic image at another base.
static BOOL install_tick_log_image(BYTE *image, uintptr_t expected_base, const WCHAR *path, DWORD limit, BOOL actor_raw) {
    LONG previous = InterlockedCompareExchange(&tick_log_state, 2, 0);
    if (previous) return previous == 1;
    if (!tick_log_image_valid(image, expected_base)) goto refused;
    // Never overwrite an earlier log: every run needs a new output path.
    tick_log_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (tick_log_file == INVALID_HANDLE_VALUE) { log_event("tick_log_output_refused"); goto refused; }
    tick_log_image = image;
    tick_log_limit = limit ? limit : 1000000u;
    tick_log_actor_raw = actor_raw;
    tick_log_rng_saved = FALSE;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    char header[512];
    es2_tick_start start = {tick_log_compact_rng ? 3u : 2u, "in_process_boundary_hook",
        (unsigned long)GetCurrentProcessId(), (unsigned long)GetCurrentThreadId(), (uint32_t)(uintptr_t)image,
        (uint32_t)(uintptr_t)(image + TICK_LOG_SITE_RVA), (long long)frequency.QuadPart,
        (unsigned long)tick_log_limit, actor_raw, mem_watch_count};
    int n = es2_tick_record_start(header, sizeof(header), &start);
    tick_log_append(header, n);
    tick_log_flush();

    BYTE *site = image + TICK_LOG_SITE_RVA;
    BYTE original[6], replacement[6] = {0xe8, 0, 0, 0, 0, 0x90};
    memcpy(original, site, sizeof(original));
    DWORD displacement = (DWORD)(uintptr_t)tick_log_thunk - (DWORD)(uintptr_t)(site + 5);
    memcpy(replacement + 1, &displacement, sizeof(displacement));
    DWORD protection, ignored;
    if (!VirtualProtect(site, sizeof(replacement), PAGE_EXECUTE_READWRITE, &protection)) goto close_refused;
    // Publish the state before the bytes: the main thread may reach the site at once.
    InterlockedExchange(&tick_log_state, 1);
    memcpy(site, replacement, sizeof(replacement));
    if (!FlushInstructionCache(GetCurrentProcess(), site, sizeof(replacement)) ||
        !VirtualProtect(site, sizeof(replacement), protection, &ignored)) {
        memcpy(site, original, sizeof(original));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(original));
        VirtualProtect(site, sizeof(original), protection, &ignored);
        goto close_refused;
    }
    log_event("tick_log_installed");
    return TRUE;
close_refused:
    CloseHandle(tick_log_file);
    tick_log_file = INVALID_HANDLE_VALUE;
refused:
    InterlockedExchange(&tick_log_state, -1);
    log_event("tick_log_refused");
    return FALSE;
}

/* One `exit_counters` record with every hk_* field a tick would carry
 * at this moment (same names and order as rng_hooks_tick_fields), so hooks
 * that run after the last tick (the result writer 0x42412c at mission end)
 * become visible. Written at most once per log, only with a [hooks] section
 * (without one the log stays byte-identical), only while the file is open
 * (not after limit_reached). Callers hold the tick I/O lock: the clean exit
 * (tick_log_shutdown) or the runner's flush request (tick_io_run). No tick
 * record changes; no allocation; it reuses the static tick line buffer. */
static void tick_log_exit_record(const char *reason) {
#ifdef ES2_RNG_HOOKS_H
#ifdef ES2_RTL_IMPORTS_H
    /* An installed (or refused) CW3220 import shim also asks for the
     * record (rtl_imports.h); without ES2_RTL_CONFIG nothing changes. */
    BOOL rtl = rtl_reporting();
#else
    const BOOL rtl = FALSE;
#endif
    if (tick_log_exit_written || (!rng_hooks_configured && !rtl) || tick_log_file == INVALID_HANDLE_VALUE) return;
    tick_log_exit_written = TRUE;
    char *const line = tick_log_line;
    const size_t line_size = sizeof(tick_log_line);
    int n = snprintf(line, line_size, "{\"event\":\"exit_counters\",\"reason\":\"%s\",\"last_sequence\":%lu",
                     reason, (unsigned long)tick_log_sequence);
    const BYTE *counter = tick_log_image ? tick_log_image + TICK_LOG_CTX_RVA + 0xbf : NULL;
    if (counter && tick_log_readable(counter, 4))
        n += snprintf(line + n, line_size - (size_t)n, ",\"frame_counter\":%lu", (unsigned long)tick_log_u32(counter));
    /* rng_hooks_tick_fields returns 0 only when the fields do not fit (a
     * record without [hooks] exists only for the import shim). */
    int fields = rng_hooks_configured ? rng_hooks_tick_fields(line + n, line_size - (size_t)n) : 0;
    BOOL overflow = rng_hooks_configured && !fields;
    n += fields;
#ifdef ES2_RTL_IMPORTS_H
    int rtl_fields = rtl ? rtl_tick_fields(line + n, line_size - (size_t)n, TRUE) : 0;
    overflow = overflow || (rtl && !rtl_fields);
    n += rtl_fields;
#endif
    if (overflow) n += snprintf(line + n, line_size - (size_t)n, ",\"counters_overflow\":true");
    n += snprintf(line + n, line_size - (size_t)n, "}\r\n");
    tick_log_append(line, n);
#else
    (void)reason;
#endif
}

// Flushes buffered records; called on process detach.
static void tick_log_shutdown(void) {
#ifdef ES2_LIVE_COVERAGE_H
    live_coverage_shutdown("tick_shutdown");
#endif
    tick_io_finish();
    tick_io_enter();
    if (tick_log_file == INVALID_HANDLE_VALUE) { tick_io_leave(); return; }
    tick_log_exit_record("shutdown");
    tick_log_flush();
    CloseHandle(tick_log_file);
    tick_log_file = INVALID_HANDLE_VALUE;
    tick_io_leave();
}

static BOOL install_tick_log(void) {
    WCHAR path[MAX_PATH];
    if (!GetPrivateProfileStringW(L"trace", L"tick_log", L"", path, MAX_PATH, config_path)) return FALSE;
    WCHAR exe[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!length || length >= MAX_PATH) return FALSE;
    WCHAR *name = wcsrchr(exe, L'\\');
    if (!name || _wcsicmp(name + 1, L"DBSIM.EXE")) return FALSE;
    DWORD limit = GetPrivateProfileIntW(L"trace", L"tick_log_max", 0, config_path);
    BOOL actor_raw = GetPrivateProfileIntW(L"trace", L"tick_log_actor_raw", 0, config_path) != 0;
    tick_log_compact_rng = GetPrivateProfileIntW(L"trace", L"tick_log_compact_rng", 0, config_path) != 0;
    tick_log_hold = GetPrivateProfileIntW(L"trace", L"hold_frame", 0, config_path);
    // Watches are optional; a malformed entry refuses the whole log so that a
    // measurement never silently lacks a requested range.
    UINT watches = GetPrivateProfileIntW(L"trace", L"watch_count", 0, config_path);
    if (watches > MEM_WATCH_MAX) { log_event("tick_log_watch_refused"); return FALSE; }
    for (UINT i = 1; i <= watches; ++i) {
        WCHAR key[16], wide[256];
        char spec[256];
        swprintf(key, 16, L"watch%u", i);
        DWORD got = GetPrivateProfileStringW(L"trace", key, L"", wide, 256, config_path);
        if (!got || got >= 255) { log_event("tick_log_watch_refused"); return FALSE; }
        for (DWORD c = 0; c <= got; ++c) {
            if (wide[c] > 0x7e) { log_event("tick_log_watch_refused"); return FALSE; }
            spec[c] = (char)wide[c];
        }
        if (!mem_watch_add(spec)) { log_event("tick_log_watch_refused"); return FALSE; }
    }
    BOOL installed = install_tick_log_image((BYTE *)GetModuleHandleW(NULL), (uintptr_t)0x00400000, path, limit, actor_raw);
    if (installed && GetPrivateProfileIntW(L"trace", L"tick_log_buffered", 0, config_path)) {
        DWORD target = GetPrivateProfileIntW(L"trace", L"tick_log_target", 0, config_path);
        DWORD interval = GetPrivateProfileIntW(L"trace", L"tick_log_flush_ms", 100, config_path);
        if (!tick_io_start(target, interval)) log_event("tick_log_buffer_refused");
    }
    return installed;
}
#endif
