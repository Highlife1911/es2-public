/* Own synthetic memory only. mem_watch_baseline.h is a frozen copy of the
 * project's header (not third-party code).
 * Compile both implementations in this process so addresses and event bytes
 * can be compared directly, without normalization. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../oracle_perf.h"

#define mem_watch baseline_watch
#define mem_watches baseline_watches
#define mem_watch_count baseline_count
#define mem_watch_number baseline_number
#define mem_watch_add baseline_add
#define mem_watch_resolve baseline_resolve
#define mem_watch_hex baseline_hex
#define mem_watch_check baseline_check
#define mem_watch_readable_fn baseline_readable_fn
#define mem_watch_emit_fn baseline_emit_fn
#include "mem_watch_baseline.h"
#undef mem_watch
#undef mem_watches
#undef mem_watch_count
#undef mem_watch_number
#undef mem_watch_add
#undef mem_watch_resolve
#undef mem_watch_hex
#undef mem_watch_check
#undef mem_watch_readable_fn
#undef mem_watch_emit_fn
#undef ES2_MEM_WATCH_H

static uint64_t queries, copies, copied_bytes;
static unsigned copy_fault;
static SIZE_T WINAPI count_query(const void *p, MEMORY_BASIC_INFORMATION *m, SIZE_T n) {
    ++queries;
    return VirtualQuery(p, m, n);
}
static BOOL WINAPI count_copy(HANDLE process, const void *p, void *out, SIZE_T n, SIZE_T *got) {
    ++copies; copied_bytes += n;
    if (copy_fault) {
        memset(out, 0xcc, n); *got = n - 1; SetLastError(ERROR_PARTIAL_COPY);
        return copy_fault == 2;
    }
    return ReadProcessMemory(process, p, out, n, got);
}
static WCHAR config_path[MAX_PATH];
static void log_event(const char *event) { (void)event; }
#define VirtualQuery count_query
#define ReadProcessMemory count_copy
#include "../tick_log.h"
#undef VirtualQuery
#undef ReadProcessMemory

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s (error %lu)\n", \
    __func__, __LINE__, #c, (unsigned long)GetLastError()); exit(1); } } while (0)
static char events[2][2 * 1024 * 1024], line[TICK_LOG_WATCH_LINE_MAX];
static size_t event_size[2];
static unsigned output, frame, frames_checked, events_checked;
static uint64_t bytes_checked;
static DWORD page_size;
static BYTE *change_protection;
static BYTE *change_watched;
static DWORD *change_pointer, changed_pointer_value;

static void emit(const char *text, int length) {
    REQUIRE(length > 0 && (size_t)length < sizeof(events[output]) - event_size[output]);
    memcpy(events[output] + event_size[output], text, (size_t)length);
    event_size[output] += (size_t)length;
    events[output][event_size[output]] = 0;
    if (change_protection) {
        DWORD ignored;
        REQUIRE(VirtualProtect(change_protection, page_size, PAGE_NOACCESS, &ignored));
    }
    if (change_pointer) *change_pointer = changed_pointer_value;
    if (change_watched) ++*change_watched;
}
static void reset(void) {
    for (unsigned i = 0; i < mem_watch_count; ++i) free(mem_watches[i].previous);
    for (unsigned i = 0; i < baseline_count; ++i) free(baseline_watches[i].previous);
    memset(mem_watches, 0, sizeof(mem_watches)); memset(baseline_watches, 0, sizeof(baseline_watches));
    mem_watch_count = baseline_count = 0;
    frame = 0; copy_fault = 0; change_protection = NULL; change_pointer = NULL; change_watched = NULL;
}
static void add(const char *spec) {
    REQUIRE(baseline_add(spec)); REQUIRE(mem_watch_add(spec));
    REQUIRE(baseline_count == mem_watch_count);
}
static void fixed(const char *name, const void *p, unsigned length) {
    char spec[256];
    snprintf(spec, sizeof(spec), "%s 0x%lx %u", name, (unsigned long)(uintptr_t)p, length);
    add(spec);
}
static void counted(const char *name, const void *p, const void *count, unsigned stride) {
    char spec[256];
    snprintf(spec, sizeof(spec), "%s 0x%lx w0x%lx*%u", name,
        (unsigned long)(uintptr_t)p, (unsigned long)(uintptr_t)count, stride);
    add(spec);
}
static void put32(void *p, uintptr_t value) {
    BYTE *b = p;
    for (unsigned i = 0; i < 4; ++i) b[i] = (BYTE)(value >> (i * 8));
}
static void protect(void *p, SIZE_T size, DWORD protection) {
    DWORD old; REQUIRE(VirtualProtect(p, size, protection, &old));
}
static BYTE *allocate(SIZE_T size) {
    BYTE *p = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(p); return p;
}
static void release(void *p) { REQUIRE(VirtualFree(p, 0, MEM_RELEASE)); }
static void compare_state(void) {
    REQUIRE(event_size[0] == event_size[1]);
    if (memcmp(events[0], events[1], event_size[0])) {
        fprintf(stderr, "different events at frame %u\nold: %.1000s\nnew: %.1000s\n", frame, events[0], events[1]);
        exit(1);
    }
    for (unsigned i = 0; i < mem_watch_count; ++i) {
        const mem_watch *a = &mem_watches[i]; const baseline_watch *b = &baseline_watches[i];
        REQUIRE(a->previous_length == b->previous_length);
        REQUIRE(a->previous_address == b->previous_address);
        REQUIRE(a->previous_unreadable == b->previous_unreadable);
        REQUIRE(!memcmp(a->previous, b->previous, a->previous_length));
    }
    for (size_t i = 0; i < event_size[0]; ++i) if (events[0][i] == '\n') ++events_checked;
    bytes_checked += event_size[0]; ++frames_checked;
}
static void check(void) {
    DWORD errors[2];
    for (output = 0; output < 2; ++output) {
        event_size[output] = 0; events[output][0] = 0; SetLastError(0x1234);
        if (!output) baseline_check(frame + 1, frame, tick_log_readable, emit, line, sizeof(line));
        else mem_watch_check(frame + 1, frame, tick_log_readable, emit, line, sizeof(line));
        errors[output] = GetLastError();
    }
    REQUIRE(errors[0] == errors[1]); compare_state(); ++frame;
}
static void unchanged(void) { check(); REQUIRE(!event_size[0]); }

static void parser_fixture(void) {
    static const char *bad[] = {"", "bad-name 0x10 4", "x 0x10", "x 0x10 0", "x 0x10 16385",
        "x 0x10@ 4", "x 0x10 4 junk", "x 0x10 w0x20*0", "x 0x10 w0x20*16385",
        "x 0x10@0@0@0@0@0@0@0 4", "abcdefghijklmnopqrstuvwxyzABCDEF 0x10 4"};
    reset();
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        REQUIRE(!baseline_add(bad[i])); REQUIRE(!mem_watch_add(bad[i]));
        REQUIRE(!baseline_count && !mem_watch_count);
    }
    BYTE *p = allocate(MEM_WATCH_MAX_BYTES);
    for (unsigned i = 0; i < MEM_WATCH_MAX; ++i) {
        char name[32]; snprintf(name, sizeof(name), "max_%u", i);
        fixed(name, p, MEM_WATCH_MAX_BYTES);
    }
    REQUIRE(!baseline_add("overflow 0 1") && !mem_watch_add("overflow 0 1"));
    check(); REQUIRE(event_size[0] > 1500000); unchanged();
    // Maximum diff size: alternate changes with four unchanged bytes. Runs
    // must neither merge across four bytes nor truncate at the line limit.
    for (unsigned i = 0; i < MEM_WATCH_MAX_BYTES; i += 5) p[i] = (BYTE)(i + 1);
    check(); REQUIRE(strstr(events[0], "\"diff\":[[0,\"01\"]")); unchanged();
    memset(p, 0xff, MEM_WATCH_MAX_BYTES); check(); unchanged();
    reset(); release(p);
}

static void resolution_fixture(void) {
    reset(); BYTE *data = allocate(8 * page_size), *nodes = allocate(6 * page_size), *counts = allocate(page_size);
    char spec[256];
    for (unsigned i = 0; i < 8 * page_size; ++i) data[i] = (BYTE)i;
    unsigned lengths[] = {1, 255, 256, 257, MEM_WATCH_MAX_BYTES};
    for (unsigned i = 0; i < sizeof(lengths)/sizeof(lengths[0]); ++i) {
        char name[32]; snprintf(name, sizeof(name), "length_%u", lengths[i]); fixed(name, data, lengths[i]);
    }
    for (unsigned i = 0; i < 6; ++i)
        put32(nodes + i * page_size + 1, (uintptr_t)(i == 5 ? data : nodes + (i + 1) * page_size + 1) - 3);
    for (unsigned hops = 1; hops <= 6; ++hops) {
        int n = snprintf(spec, sizeof(spec), "chain_%u 0x%lx", hops,
            (unsigned long)(uintptr_t)(nodes + (6 - hops) * page_size + 1));
        for (unsigned i = 0; i < hops; ++i) n += snprintf(spec + n, sizeof(spec) - (size_t)n, "@3");
        snprintf(spec + n, sizeof(spec) - (size_t)n, " 257"); add(spec);
    }
    counted("counted", data, counts, 8); counted("max_stride", data, counts, MEM_WATCH_MAX_BYTES);
    counts[0] = 1;
    fixed("null", NULL, 1); fixed("overflow", (void *)(uintptr_t)0xfffffff0, 32);
    fixed("invalid", (void *)(uintptr_t)0xffff0000, 1);
    check(); unchanged();
    data[0] ^= 0x80; data[3] ^= 0x40; data[8] ^= 0x20; data[256] ^= 1;
    check(); REQUIRE(strstr(events[0], "\"diff\":[[0,\"80010243\"],[8,\"28\"],[256,\"01\"]]"));
    unchanged();
    for (unsigned i = 0; i < 6; ++i) {
        BYTE saved[4]; BYTE *node = nodes + i * page_size + 1; memcpy(saved, node, 4);
        memset(node, 0, 4); check(); unchanged(); memcpy(node, saved, 4); check(); unchanged();
        protect(node, 4, PAGE_NOACCESS); check(); unchanged(); protect(node, 4, PAGE_READWRITE); check();
        protect(node, 4, PAGE_READWRITE | PAGE_GUARD); check(); unchanged();
        MEMORY_BASIC_INFORMATION info; REQUIRE(VirtualQuery(node, &info, sizeof(info)));
        REQUIRE(info.Protect == (PAGE_READWRITE | PAGE_GUARD));
        protect(node, 4, PAGE_READWRITE); check();
    }
    int values[] = {0, -1, -32768, 32767, 2049, 2048, 2, 1};
    for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
        counts[0] = (BYTE)values[i]; counts[1] = (BYTE)((unsigned)values[i] >> 8);
        check(); unchanged();
    }
    protect(counts, page_size, PAGE_NOACCESS); check(); unchanged(); protect(counts, page_size, PAGE_READWRITE); check();
    protect(counts, page_size, PAGE_READWRITE | PAGE_GUARD); check(); unchanged(); protect(counts, page_size, PAGE_READWRITE); check();
    // An address change with identical bytes still emits a whole record.
    memcpy(data + 4 * page_size, data, MEM_WATCH_MAX_BYTES);
    put32(nodes + 5 * page_size + 1, (uintptr_t)(data + 4 * page_size) - 3); check();
    REQUIRE(strstr(events[0], "\"name\":\"chain_6\"") && !strstr(events[0], "\"diff\"")); unchanged();
    // Win32 unsigned pointer-plus-offset wrap is retained.
    reset(); DWORD wrap = 0xfffffff0;
    snprintf(spec, sizeof(spec), "wrap 0x%lx@0x%lx 4", (unsigned long)(uintptr_t)&wrap,
        (unsigned long)((DWORD)(uintptr_t)data + 16)); add(spec); check(); unchanged();
    // API refusal and short-success injection must fall back to the exact
    // previous acceptance and LastError contract, overwriting partial bytes.
    for (copy_fault = 1; copy_fault <= 2; ++copy_fault) { data[0]++; check(); unchanged(); }
    reset(); release(counts); release(nodes); release(data);
}

static void protection_fixture(void) {
    reset(); BYTE *p = allocate(8 * page_size);
    for (unsigned i = 0; i < 8 * page_size; ++i) p[i] = (BYTE)(i * 17);
    fixed("first_partial", p + page_size - 3, 8);
    fixed("last_partial", p + 2 * page_size - 3, 8);
    fixed("inside", p + page_size + 1, page_size - 2);
    fixed("max_partial", p + page_size - 1, MEM_WATCH_MAX_BYTES);
    // Pointer and count split across two different regions, unaligned.
    put32(p + 3 * page_size - 2, (uintptr_t)(p + 4 * page_size));
    p[4 * page_size - 1] = 2; p[4 * page_size] = 0;
    char spec[256]; snprintf(spec, sizeof(spec), "split_pointer 0x%lx@0 16",
        (unsigned long)(uintptr_t)(p + 3 * page_size - 2)); add(spec);
    counted("split_count", p + 5 * page_size, p + 4 * page_size - 1, 8);
    check(); unchanged();
    DWORD modes[] = {PAGE_READONLY, PAGE_EXECUTE, PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE,
        PAGE_NOACCESS, PAGE_READWRITE | PAGE_GUARD, PAGE_READWRITE};
    for (unsigned page = 0; page < 5; ++page) {
        for (unsigned i = 0; i < sizeof(modes)/sizeof(modes[0]); ++i) {
            protect(p + page * page_size, page_size, modes[i]); check(); unchanged();
            MEMORY_BASIC_INFORMATION info; REQUIRE(VirtualQuery(p + page * page_size, &info, sizeof(info)));
            REQUIRE(info.Protect == modes[i]);
        }
    }
    REQUIRE(VirtualFree(p + page_size, page_size, MEM_DECOMMIT)); check(); unchanged();
    REQUIRE(VirtualAlloc(p + page_size, page_size, MEM_COMMIT, PAGE_READWRITE) == p + page_size); check(); unchanged();
    reset(); release(p);

    // A freed range, then reserve-only, then a new allocation at the same VA.
    p = allocate(65536); memset(p, 0x5a, 65536); fixed("lifetime", p, MEM_WATCH_MAX_BYTES);
    check(); release(p); check(); unchanged();
    REQUIRE(VirtualAlloc(p, 65536, MEM_RESERVE, PAGE_NOACCESS) == p); check();
    REQUIRE(VirtualAlloc(p, 65536, MEM_COMMIT, PAGE_READWRITE) == p); memset(p, 0x5a, 65536);
    check(); REQUIRE(strstr(events[0], "\"hex\":\"5a5a")); unchanged(); reset(); release(p);

    // Two adjacent allocations with a range crossing their boundary.
    p = allocate(131072); release(p);
    REQUIRE(VirtualAlloc(p, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) == p);
    REQUIRE(VirtualAlloc(p + 65536, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) == p + 65536);
    fixed("alloc_boundary", p + 65536 - 8, 16); check();
    release(p + 65536); check(); unchanged();
    reset(); release(p);

    // Anonymous mapped/COW memory, including executable COW. No disk file.
    for (unsigned execute = 0; execute < 2; ++execute) {
        HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL,
            execute ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, 0, 2 * page_size, NULL);
        REQUIRE(map);
        p = MapViewOfFile(map, FILE_MAP_COPY | (execute ? FILE_MAP_EXECUTE : 0), 0, 0, 0); REQUIRE(p);
        fixed("cow", p, 256); check(); p[0] = 0x5a; check(); unchanged();
        protect(p, page_size, execute ? PAGE_EXECUTE_WRITECOPY : PAGE_WRITECOPY); check(); unchanged();
        reset(); REQUIRE(UnmapViewOfFile(p)); REQUIRE(CloseHandle(map));
    }
}

static void same_frame_fixture(void) {
    reset(); BYTE *p = allocate(2 * page_size); p[0] = 1;
    DWORD pointer = (DWORD)(uintptr_t)(p + page_size);
    fixed("control", p, 1); fixed("target", p + page_size, 8);
    char spec[256]; snprintf(spec, sizeof(spec), "pointer 0x%lx@0 8", (unsigned long)(uintptr_t)&pointer); add(spec);
    for (output = 0; output < 2; ++output) {
        protect(p, 2 * page_size, PAGE_READWRITE); pointer = (DWORD)(uintptr_t)(p + page_size);
        change_protection = p + page_size; change_pointer = &pointer; changed_pointer_value = 0;
        event_size[output] = 0;
        if (!output) baseline_check(1, 0, tick_log_readable, emit, line, sizeof(line));
        else mem_watch_check(1, 0, tick_log_readable, emit, line, sizeof(line));
    }
    compare_state(); REQUIRE(strstr(events[0], "\"name\":\"target\",\"unreadable\":true"));
    REQUIRE(strstr(events[0], "\"name\":\"pointer\",\"unreadable\":true"));
    reset(); release(p);
}

static void emission_fixture(void) {
    reset(); BYTE *p = allocate(page_size);
    fixed("during_emit", p, 16); change_watched = p;
    for (output = 0; output < 2; ++output) {
        p[0] = 1; event_size[output] = 0;
        if (!output) baseline_check(1, 0, tick_log_readable, emit, line, sizeof(line));
        else mem_watch_check(1, 0, tick_log_readable, emit, line, sizeof(line));
    }
    compare_state(); REQUIRE(p[0] == 2 && mem_watches[0].previous[0] == 2);
    change_watched = NULL; frame = 1; unchanged();
    reset(); release(p);
}

static uint32_t random_state = 0x4d575332;
static uint32_t random32(void) {
    random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5;
    return random_state;
}
static void random_fixture(void) {
    BYTE *data = allocate(16 * page_size), *nodes = allocate(48 * page_size), *counts = allocate(page_size);
    for (unsigned set = 0; set < 64; ++set) {
        reset(); protect(data, 16 * page_size, PAGE_READWRITE); protect(nodes, 48 * page_size, PAGE_READWRITE);
        for (unsigned i = 0; i < 16 * page_size; ++i) data[i] = (BYTE)random32();
        unsigned watches = set % 8 ? 1 + random32() % MEM_WATCH_MAX : MEM_WATCH_MAX;
        for (unsigned w = 0; w < watches; ++w) {
            unsigned hops = random32() % 7, length = 1 + random32() % MEM_WATCH_MAX_BYTES;
            BYTE *dest = data + random32() % (12 * page_size);
            BYTE *node = nodes + w * page_size;
            for (unsigned h = 0; h < hops; ++h)
                put32(node + h * 8 + 1, (uintptr_t)(h + 1 == hops ? dest : node + (h + 1) * 8 + 1) - 3);
            counts[2*w] = (BYTE)(1 + random32() % 8); counts[2*w + 1] = 0;
            char spec[256]; int n = snprintf(spec, sizeof(spec), "random_%u 0x%lx", w,
                (unsigned long)(uintptr_t)(hops ? node + 1 : dest));
            for (unsigned h = 0; h < hops; ++h) n += snprintf(spec + n, sizeof(spec) - (size_t)n, "@3");
            if (random32() & 1) snprintf(spec + n, sizeof(spec) - (size_t)n, " w0x%lx*%u",
                (unsigned long)(uintptr_t)(counts + 2*w), length);
            else snprintf(spec + n, sizeof(spec) - (size_t)n, " %u", length);
            add(spec);
        }
        check(); unchanged();
        for (unsigned step = 0; step < 48; ++step) {
            protect(data, 16 * page_size, PAGE_READWRITE); protect(nodes, 48 * page_size, PAGE_READWRITE);
            for (unsigned i = 0; i < 32; ++i) data[random32() % (16 * page_size)] ^= (BYTE)random32();
            unsigned w = random32() % watches;
            unsigned hops = mem_watches[w].steps;
            if (hops) put32(nodes + w * page_size + (hops - 1) * 8 + 1,
                random32() & 1 ? 0 : (uintptr_t)(data + random32() % (12 * page_size)) - 3);
            counts[2*w] = (BYTE)random32(); counts[2*w + 1] = random32() % 3 ? 0 : 0xff;
            DWORD mode = random32() & 1 ? PAGE_NOACCESS : PAGE_READWRITE | PAGE_GUARD;
            protect(data + random32() % 16 * page_size, page_size, mode);
            protect(nodes + random32() % watches * page_size, page_size, mode);
            check(); unchanged();
        }
    }
    reset(); release(counts); release(nodes); release(data);
}

static LONGLONG clock_now(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static volatile unsigned benchmark_sink;
static void discard(const char *text, int length) { benchmark_sink += (unsigned)length + (unsigned char)text[0]; }
static void benchmark_fixture(void) {
    BYTE *arena = allocate(16 * 1024 * 1024);
    memset(arena, 0x5a, 16 * 1024 * 1024);
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    // A controlled remaining-region sweep reproduces the initial probe.
    unsigned sizes[] = {4096, 65536, 1048576, 16777216};
    BYTE sample[16];
    for (unsigned s = 0; s < sizeof(sizes)/sizeof(sizes[0]); ++s) {
        BYTE *p = arena + 16 * 1024 * 1024 - sizes[s];
        for (unsigned version = 0; version < 2; ++version) {
            LONGLONG began = clock_now(); queries = copies = 0;
            for (unsigned i = 0; i < 10000; ++i) {
                if (!version) { REQUIRE(tick_log_readable(p, 4)); memcpy(sample, p, 4); }
                else REQUIRE(mem_watch_copy(sample, (uintptr_t)p, 4, tick_log_readable));
                benchmark_sink += sample[0];
            }
            double us = (double)(clock_now() - began) * 1e6 / frequency.QuadPart / 10000;
            printf("probe remaining=%u version=%s us=%.3f queries=%llu copies=%llu\n", sizes[s],
                version ? "new" : "old", us, (unsigned long long)queries, (unsigned long long)copies);
        }
    }
    // 48 watches with the real hostile set's hop distribution: two direct,
    // one 1-hop, one 2-hop, one 3-hop, five 5-hop, two counted 1-hop, 36 2-hop.
    reset();
    for (unsigned w = 0; w < 48; ++w) {
        unsigned hops = w < 2 ? 0 : w == 2 ? 1 : w == 3 ? 2 : w == 4 ? 3 : w < 10 ? 5 : w < 12 ? 1 : 2;
        BYTE *node = arena + 12 * 1024 * 1024 + w * 64;
        BYTE *value = arena + 14 * 1024 * 1024 + w * 2048;
        for (unsigned h = 0; h < hops; ++h) put32(node + h * 8, (uintptr_t)(h + 1 == hops ? value : node + (h + 1) * 8));
        node[56] = 2; node[57] = 0;
        char spec[256]; int n = snprintf(spec, sizeof(spec), "bench_%u 0x%lx", w,
            (unsigned long)(uintptr_t)(hops ? node : value));
        for (unsigned h = 0; h < hops; ++h) n += snprintf(spec + n, sizeof(spec) - (size_t)n, "@0");
        if (w == 10 || w == 11) snprintf(spec + n, sizeof(spec) - (size_t)n, " w0x%lx*%u",
            (unsigned long)(uintptr_t)(node + 56), w == 10 ? 76 : 58);
        else snprintf(spec + n, sizeof(spec) - (size_t)n, " %u", w == 0 ? 80 : w == 1 ? 2000 : 13);
        add(spec);
    }
    check(); unchanged();
    InitializeCriticalSection(&perf_lock); perf_lock_ready = TRUE; perf_thread = GetCurrentThreadId();
    for (unsigned profiled = 0; profiled < 2; ++profiled) {
        perf_enabled = profiled; perf_started = TRUE; perf_last = perf_now();
        for (unsigned trial = 0; trial < 3; ++trial) for (unsigned v = 0; v < 2; ++v) {
            unsigned version = v ^ (trial & 1); queries = copies = copied_bytes = 0;
            memset(perf_counts, 0, sizeof(perf_counts)); memset(perf_totals, 0, sizeof(perf_totals));
            LONGLONG began = clock_now();
            for (unsigned i = 0; i < 240; ++i) {
                if (!version) baseline_check(i + 1, i, tick_log_readable, discard, line, sizeof(line));
                else mem_watch_check(i + 1, i, tick_log_readable, discard, line, sizeof(line));
            }
            double elapsed = (double)(clock_now() - began) / frequency.QuadPart;
            REQUIRE(queries == (version ? 0 : 240 * 155)); REQUIRE(copies == (version ? 240 * 155 : 0));
            printf("watchset profile=%u trial=%u version=%s frames=240 seconds=%.6f us_per_watch=%.3f queries=%llu copies=%llu watch_phases=%llu\n",
                profiled, trial, version ? "new" : "old", elapsed, elapsed * 1e6 / (240 * 48),
                (unsigned long long)queries, (unsigned long long)copies, (unsigned long long)perf_counts[PERF_WATCH]);
        }
    }
    perf_enabled = FALSE; reset(); release(arena);
}

int main(int argc, char **argv) {
    REQUIRE(sizeof(void *) == 4); SYSTEM_INFO info; GetSystemInfo(&info); page_size = info.dwPageSize;
    REQUIRE(page_size == 4096); if (argc != 2) return 2;
    if (!strcmp(argv[1], "differential")) {
        parser_fixture(); resolution_fixture(); protection_fixture(); same_frame_fixture(); emission_fixture();
    } else if (!strcmp(argv[1], "random")) random_fixture();
    else if (!strcmp(argv[1], "benchmark")) benchmark_fixture();
    else return 2;
    printf("PASS %s: %u frames, %u byte-identical events, %llu bytes; state and LastError also equal\n",
        argv[1], frames_checked, events_checked, (unsigned long long)bytes_checked);
    return 0;
}
