// Memory watches for the DBSIM tick log. Each watch names a byte range whose
// address may be reached through pointer chains; after every tick record the
// ranges are compared with their previous contents and changes are written as
// separate "watch" events. ReadProcessMemory checks and copies each requested
// range without scanning to its allocation's end. Game memory is never written.
//
// The parser, the resolution, the comparison and the event format are
// the shared library //sim/oracle:mem_watch (also linked by the own process);
// this header keeps the proxy's memory access (the checked copy below, the
// live bytes at their own addresses), the perf phases and the old names, which
// map onto the library's table (the fixtures use them unchanged).
//
// Configuration ([trace] in the proxy INI):
//   watch_count = N                  (at most MEM_WATCH_MAX)
//   watch1 = NAME ADDRESS LENGTH     (1..N)
// ADDRESS is a hexadecimal virtual address optionally followed by "@OFFSET"
// steps: each step replaces the current address by the 32-bit pointer stored
// there plus OFFSET. "0x4d24e6@0x45@0" therefore means [[0x4d24e6]+0x45]+0.
// LENGTH is a byte count, or "wADDRESS*STRIDE" for a signed 16-bit element
// count read at ADDRESS times STRIDE (re-evaluated on every check).
#ifndef ES2_MEM_WATCH_H
#define ES2_MEM_WATCH_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sim/oracle/mem_watch.h"

#define MEM_WATCH_MAX 48u
#define MEM_WATCH_STEPS 6u
#define MEM_WATCH_NAME 32u
#define MEM_WATCH_MAX_BYTES 16384u
#define MEM_WATCH_FULL_LIMIT 256u   // ranges up to this size are always written whole
#define MEM_WATCH_MERGE_GAP 4u      // unchanged gaps shorter than this join two diff runs
_Static_assert(MEM_WATCH_MAX == ES2_MEM_WATCH_MAX && MEM_WATCH_STEPS == ES2_MEM_WATCH_STEPS &&
               MEM_WATCH_NAME == ES2_MEM_WATCH_NAME && MEM_WATCH_MAX_BYTES == ES2_MEM_WATCH_MAX_BYTES &&
               MEM_WATCH_FULL_LIMIT == ES2_MEM_WATCH_FULL_LIMIT && MEM_WATCH_MERGE_GAP == ES2_MEM_WATCH_MERGE_GAP,
               "the proxy's watch limits are the library's");

typedef BOOL (*mem_watch_readable_fn)(const void *pointer, SIZE_T size);
typedef void (*mem_watch_emit_fn)(const char *text, int length);

typedef es2_mem_watch mem_watch;
static es2_mem_watch_table mem_watch_table;
#define mem_watches (mem_watch_table.watches)
#define mem_watch_count (mem_watch_table.count)

// Parses "NAME ADDRESS LENGTH". Returns FALSE and leaves the table unchanged on
// any syntax error.
static BOOL mem_watch_add(const char *spec) { return es2_mem_watch_add(&mem_watch_table, spec) == 0; }

// No cached addresses/protections: even two reads in the same frame must see a
// changed mapping. The callback is the legacy VirtualQuery-based check, used
// only if the checked copy fails (and to retain its LastError behavior); only
// that slow path has a race between the query and the copy.
static BOOL mem_watch_copy(void *out, uintptr_t address, SIZE_T size, mem_watch_readable_fn readable) {
    if (!address || size > UINTPTR_MAX - address) return FALSE;
    DWORD error = GetLastError();
    SIZE_T copied = 0;
    BOOL ok = ReadProcessMemory(GetCurrentProcess(), (const void *)address, out, size, &copied);
    SetLastError(error);
    if (ok && copied == size) return TRUE;
    // Keep the old acceptance rule even on a system where a readable page is
    // refused by ReadProcessMemory. Failed/partial copies are never emitted.
    if (!readable((const void *)address, size)) return FALSE;
    memcpy(out, (const void *)address, size);
    return TRUE;
}

typedef struct { mem_watch_readable_fn readable; mem_watch_emit_fn emit; } mem_watch_proxy_access;

static int mem_watch_proxy_copy(void *user, void *out, uint32_t address, uint32_t size) {
    return mem_watch_copy(out, (uintptr_t)address, size, ((const mem_watch_proxy_access *)user)->readable) ? 1 : 0;
}
// The proxy runs inside DBSIM: the live bytes are at their own addresses.
static const uint8_t *mem_watch_proxy_view(void *user, uint32_t address, uint32_t size) {
    (void)user; (void)size;
    return (const uint8_t *)(uintptr_t)address;
}
static void mem_watch_proxy_emit(void *user, const char *text, int length) {
    ((const mem_watch_proxy_access *)user)->emit(text, length);
}
#ifdef ES2_ORACLE_PERF_H
static unsigned mem_watch_proxy_begin(void *user, unsigned phase) {
    (void)user;
    return perf_begin(phase == ES2_MEM_WATCH_PHASE_FORMAT ? PERF_FORMAT : PERF_WATCH);
}
static void mem_watch_proxy_end(void *user, unsigned token) { (void)user; perf_end(token); }
#endif

// Checks every watch and emits one JSON line per changed watch (the library's
// es2_mem_watch_check). `line` must hold at least 2*MEM_WATCH_MAX_BYTES + 512
// bytes plus 16 bytes per possible run.
static void mem_watch_check(unsigned long sequence, unsigned long frame, mem_watch_readable_fn readable,
                            mem_watch_emit_fn emit, char *line, size_t line_size) {
    mem_watch_proxy_access proxy = {readable, emit};
    es2_mem_watch_access access = {&proxy, mem_watch_proxy_copy, mem_watch_proxy_view, mem_watch_proxy_emit,
#ifdef ES2_ORACLE_PERF_H
                                   mem_watch_proxy_begin, mem_watch_proxy_end
#else
                                   NULL, NULL
#endif
    };
    es2_mem_watch_check(&mem_watch_table, sequence, frame, &access, line, line_size);
}
#endif
