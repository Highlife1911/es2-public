/* The memory watches of the tick log (own_process.md section 9), shared by the
 * proxy (tools/reference-game/proxy/mem_watch.h, the DBSIM process) and the own
 * process (es2sim, sim/src/host.c). Pure C17: the caller supplies the memory
 * access, the output and the optional phase timer; nothing here touches the OS.
 *
 * A watch names a byte range whose address may be reached through pointer
 * chains. After every tick record each range is compared with its previous
 * contents and a change is written as one "watch" JSON line. The format and
 * the order of the memory accesses are the proxy's (moved here unchanged; the
 * proxy's mem_watch_* fixtures compare it byte for byte with the frozen
 * baseline tests/mem_watch_baseline.h).
 *
 * Specification ([trace] watchN of the proxy's INI): "NAME ADDRESS LENGTH".
 * NAME is [A-Za-z0-9_]{1,31}. ADDRESS is a number (strtoul, base 0)
 * optionally followed by "@OFFSET" steps: each step replaces the current
 * address by the 32-bit pointer stored there plus OFFSET, so
 * "0x4d24e6@0x45@0" means [[0x4d24e6]+0x45]+0. LENGTH is a byte count
 * (1..ES2_MEM_WATCH_MAX_BYTES) or "wADDRESS*STRIDE": a signed 16-bit element
 * count read at ADDRESS times STRIDE, re-evaluated on every check.
 *
 * Events: the first record of a watch, and any record after its address or
 * length changed, is written whole ("hex"); a later change of a range up to
 * ES2_MEM_WATCH_FULL_LIMIT bytes too; a larger range writes runs of
 * [offset, hex] ("diff"), runs joined across unchanged gaps shorter than
 * ES2_MEM_WATCH_MERGE_GAP. A range that cannot be resolved writes one
 * "unreadable" event and nothing more until it is readable again. */
#ifndef ES2_SIM_ORACLE_MEM_WATCH_H
#define ES2_SIM_ORACLE_MEM_WATCH_H
#include <stddef.h>
#include <stdint.h>

#define ES2_MEM_WATCH_MAX 48u
#define ES2_MEM_WATCH_STEPS 6u
#define ES2_MEM_WATCH_NAME 32u
#define ES2_MEM_WATCH_MAX_BYTES 16384u
#define ES2_MEM_WATCH_FULL_LIMIT 256u
#define ES2_MEM_WATCH_MERGE_GAP 4u
/* The smallest `line` es2_mem_watch_check accepts for a whole record of the
 * largest range (the proxy passes 4 * MAX_BYTES + 1024 for diff runs). */
#define ES2_MEM_WATCH_LINE_MIN (2u * ES2_MEM_WATCH_MAX_BYTES + 512u)

typedef struct es2_mem_watch {
    char name[ES2_MEM_WATCH_NAME];
    uint32_t base;
    uint32_t offsets[ES2_MEM_WATCH_STEPS];
    unsigned steps;
    uint32_t length;              /* fixed length, or the stride when count_address != 0 */
    uint32_t count_address;
    uint8_t *previous;            /* ES2_MEM_WATCH_MAX_BYTES, malloc'd by es2_mem_watch_add */
    uint32_t previous_length;     /* 0: nothing recorded yet */
    uint32_t previous_address;
    int previous_unreadable;
} es2_mem_watch;

typedef struct es2_mem_watch_table {
    es2_mem_watch watches[ES2_MEM_WATCH_MAX];
    unsigned count;
} es2_mem_watch_table;

/* Phases for the optional timer (the proxy's PERF_WATCH and PERF_FORMAT). */
enum { ES2_MEM_WATCH_PHASE_WATCH, ES2_MEM_WATCH_PHASE_FORMAT };

/* The caller's side of one check. `copy` copies `size` bytes at guest
 * `address` into `out` if, and only if, the whole range is readable now (1),
 * else 0; it is used for the pointer steps, the count word and the final
 * accessibility probe of the range. `view` gives the host pointer of the
 * range the probe just accepted; comparison, formatting and the saved copy
 * read the live bytes through it, in that order, so a change made during
 * `emit` is what the saved copy holds (the proxy's order). NULL from `view`
 * counts as unreadable. `phase_begin`/`phase_end` may be NULL. */
typedef struct es2_mem_watch_access {
    void *user;
    int (*copy)(void *user, void *out, uint32_t address, uint32_t size);
    const uint8_t *(*view)(void *user, uint32_t address, uint32_t size);
    void (*emit)(void *user, const char *text, int length);
    unsigned (*phase_begin)(void *user, unsigned phase);
    void (*phase_end)(void *user, unsigned token);
} es2_mem_watch_access;

/* Parses one specification and appends it. 0 on success; -1 (table
 * unchanged) on a syntax error, a full table or a failed allocation. */
int es2_mem_watch_add(es2_mem_watch_table *table, const char *spec);

/* Frees every watch's saved copy and empties the table. */
void es2_mem_watch_clear(es2_mem_watch_table *table);

/* Checks every watch in table order and emits one line per changed watch:
 * {"event":"watch","sequence":S,"frame":F,"name":"N",...}\r\n. `line` holds
 * the formatted line (at least ES2_MEM_WATCH_LINE_MIN bytes; diff runs that
 * do not fit are cut off as in the proxy). Main thread only, not reentrant
 * (one static probe buffer). */
void es2_mem_watch_check(es2_mem_watch_table *table, unsigned long sequence, unsigned long frame,
                         const es2_mem_watch_access *access, char *line, size_t line_size);

#endif
