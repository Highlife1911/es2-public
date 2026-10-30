/* Unit test of core/src/rtl_heap.c with a scripted fake OS. Expected values
 * are hand-traced from the contract in docs/reference-game/rtl_imports.md
 * ("the heap, shadow stage"); the in-vitro harness compares the model with the
 * original DLL. */
#include "es2/core/rtl_heap.h"

#include <stdio.h>
#include <string.h>

static unsigned failures, checks;
static void check(int passed, const char *expression, int line)
{
    ++checks;
    if (!passed) {
        if (failures < 30) fprintf(stderr, "rtl_heap:%d: %s\n", line, expression);
        ++failures;
    }
}
#define CHECK(expression) check((expression) ? 1 : 0, #expression, __LINE__)

/* Memory: the word store; data operations over a small flat window. */
#define BITS 16u
static uint32_t keys[1u << BITS], values[1u << BITS];
static es2_heap_words store;
#define FLAT 0x10000000u
#define FLAT_SIZE 0x100000u
static uint8_t flat[FLAT_SIZE];
static unsigned fills, moves, outside;
static uint32_t read32(void *context, uint32_t address) { (void)context; return es2_heap_words_read(&store, address); }
static void write32(void *context, uint32_t address, uint32_t value) { (void)context; es2_heap_words_write(&store, address, value); }
static void fill(void *context, uint32_t address, uint32_t count)
{
    (void)context;
    ++fills;
    for (uint32_t i = 0; i < count; ++i) if (address + i - FLAT < FLAT_SIZE) flat[address + i - FLAT] = 0; else ++outside;
}
static void move(void *context, uint32_t dest, uint32_t source, uint32_t count)
{
    (void)context;
    ++moves;
    if (dest - FLAT >= FLAT_SIZE || source - FLAT >= FLAT_SIZE || count > FLAT_SIZE - (dest - FLAT) || count > FLAT_SIZE - (source - FLAT)) { ++outside; return; }
    memmove(flat + (dest - FLAT), flat + (source - FLAT), count);
}

/* Fake OS: reservations from a script, commits succeed except a chosen one. */
static es2_heap_os_call calls[8192];
static unsigned call_count, commits, fail_commit_at;
static uint32_t script[128];
static unsigned script_count, script_next;
static void record(uint32_t kind, uint32_t address, uint32_t size, uint32_t type, uint32_t protect, uint32_t result)
{
    if (call_count < sizeof(calls) / sizeof(calls[0])) {
        es2_heap_os_call c = {kind, address, size, type, protect, result};
        calls[call_count] = c;
    }
    ++call_count;
}
static uint32_t fake_alloc(void *context, uint32_t address, uint32_t size, uint32_t type, uint32_t protect)
{
    (void)context;
    uint32_t result;
    if (type == ES2_HEAP_MEM_RESERVE) result = script_next < script_count ? script[script_next++] : 0u;
    else result = (++commits == fail_commit_at) ? 0u : address;
    record(ES2_HEAP_OS_ALLOC, address, size, type, protect, result);
    return result;
}
static uint32_t fake_free(void *context, uint32_t address, uint32_t size, uint32_t type)
{
    (void)context;
    record(ES2_HEAP_OS_FREE, address, size, type, 0, 1u);
    return 1u;
}
static const es2_heap_env env = {NULL, read32, write32, fill, move, fake_alloc, fake_free};

#define S 0x00500044u     /* head block (as CW3220's data) */
static es2_heap heap;
static void fresh(uint32_t platform)
{
    es2_heap_words_init(&store, keys, values, BITS);
    memset(flat, 0, sizeof(flat));
    call_count = commits = fail_commit_at = script_count = script_next = fills = moves = 0;
    es2_heap_init(&heap, &env, S, platform);
}
static uint32_t w(uint32_t address) { return es2_heap_words_read(&store, address); }
static int all_commits(unsigned from, unsigned count, uint32_t address)
{
    for (unsigned i = 0; i < count; ++i) {
        const es2_heap_os_call *c = &calls[from + i];
        if (c->kind != ES2_HEAP_OS_ALLOC || c->address != address + i * 0x1000u || c->size != 0x1000u ||
            c->type != ES2_HEAP_MEM_COMMIT || c->protect != ES2_HEAP_PAGE_READWRITE) return 0;
    }
    return 1;
}

static void sizes(void)
{
    CHECK(es2_heap_block_size(0) == 16u && es2_heap_block_size(7) == 16u && es2_heap_block_size(8) == 16u);
    CHECK(es2_heap_block_size(9) == 20u && es2_heap_block_size(13) == 24u && es2_heap_block_size(0x18000) == 0x18008u);
    fresh(0);
    CHECK(es2_heap_malloc(&heap, &env, 0) == 0 && es2_heap_malloc(&heap, &env, 0x80000000u) == 0);
    CHECK(es2_heap_malloc(&heap, &env, 0xfffffffdu) == 0 && call_count == 0);
    CHECK(es2_heap_calloc(&heap, &env, 0, 5) == 0 && fills == 0);
    CHECK(w(S - 8) == 0 && w(S - 4) == 0 && w(S) == S && w(S + 4) == S && heap.rover == S);
}

/* The first allocations, next fit, coalescing and _expand. */
#define R1 0x10400000u
static void basics(void)
{
    fresh(0);
    script[script_count++] = R1;
    uint32_t a = es2_heap_malloc(&heap, &env, 1);
    CHECK(a == R1 + 8u);
    CHECK(call_count == 17 && calls[0].kind == ES2_HEAP_OS_ALLOC && calls[0].address == 0 && calls[0].size == 0x400000u &&
          calls[0].type == ES2_HEAP_MEM_RESERVE && calls[0].protect == ES2_HEAP_PAGE_NOACCESS && all_commits(1, 16, R1));
    CHECK(heap.region_count == 1 && heap.region_base[0] == R1 && heap.region_size[0] == 0x10000u);
    CHECK(heap.committed_end == R1 + 0x10000u && heap.reserved_end == R1 + 0x400000u && heap.granularity == 0x400000u);
    CHECK(w(R1) == 1 && w(R1 + 4) == 0x11 && w(R1 + 0x10) == 0x11 && w(R1 + 0x14) == 0xffe8u && w(R1 + 0xfff8) == 0xffe8u &&
          w(R1 + 0xfffc) == 1);
    uint32_t f = R1 + 0x18;
    CHECK(heap.rover == f && w(f) == S && w(f + 4) == S && w(S) == f && w(S + 4) == f);
    uint32_t b = es2_heap_malloc(&heap, &env, 8), c = es2_heap_malloc(&heap, &env, 13);
    CHECK(b == R1 + 0x18 && c == R1 + 0x28 && w(c - 4) == 0x19 && w(c + 16) == 0x19 && heap.rover == R1 + 0x40);
    es2_heap_free(&heap, &env, b);                     /* inserted after the head */
    CHECK(w(b - 4) == 16 && w(b + 8) == 16 && w(S + 4) == b && w(b) == S && w(b + 4) == R1 + 0x40 && w(R1 + 0x40) == b);
    es2_heap_free(&heap, &env, a);                     /* joins b above, takes its list place at the head */
    CHECK(w(a - 4) == 32 && w(a + 24) == 32 && w(S + 4) == a && w(a + 4) == R1 + 0x40 && w(R1 + 0x40) == a);
    uint32_t d = es2_heap_malloc(&heap, &env, 20);     /* next fit: from the rover, not the first block */
    CHECK(d == R1 + 0x40 && w(d - 4) == 29 && heap.rover == R1 + 0x5c && w(R1 + 0x54) == 29 && w(R1 + 0x58) == 0xffa4u);
    CHECK(w(a + 4) == R1 + 0x5c && w(R1 + 0x5c) == a);
    es2_heap_free(&heap, &env, c);                     /* joins a below: a keeps its list place */
    CHECK(w(a - 4) == 56 && w(a + 48) == 56 && w(S + 4) == a);
    CHECK(es2_heap_expand(&heap, &env, d, 40) == d);   /* grows into the free block above, which was the rover */
    CHECK(heap.rover == S && w(d - 4) == 49 && w(R1 + 0x68) == 49 && w(R1 + 0x6c) == 0xff90u && w(R1 + 0xfff8) == 0xff90u);
    CHECK(w(a + 4) == R1 + 0x70 && w(R1 + 0x70) == a && w(R1 + 0x74) == S && w(S) == R1 + 0x70);
    CHECK(es2_heap_realloc(&heap, &env, d, 40) == d && moves == 0);   /* same block size */
    CHECK(es2_heap_expand(&heap, &env, d, 12) == d);   /* shrink by 32: the rest joins the free block above */
    CHECK(w(d - 4) == 21 && w(d + 12) == 21 && w(d + 16) == 0xffacu && w(R1 + 0xfff8) == 0xffacu);
    CHECK(es2_heap_expand(&heap, &env, d, 16) == d && w(d - 4) == 25);   /* takes 8 of the block above (rest 0xffa8) */
    CHECK(w(d + 16) == 25 && w(d + 20) == 0xffa8u && w(d + 24) == S && w(d + 28) == a && w(S + 4) == d + 24 && w(a) == d + 24);
    es2_heap_free(&heap, &env, 0);
    es2_heap_free(&heap, &env, a);                     /* a is free: no change */
    CHECK(w(a - 4) == 56 && w(S + 4) == d + 24 && call_count == 17);
    CHECK(es2_heap_walk(&heap, read32, NULL, NULL, NULL) == 3);
}

/* realloc into the free block below (data moves down, remainder freed) and
 * the allocate-copy-free path. */
static void reallocs(void)
{
    fresh(0);
    script[script_count++] = FLAT;
    uint32_t a = es2_heap_malloc(&heap, &env, 24), b = es2_heap_malloc(&heap, &env, 24), c = es2_heap_malloc(&heap, &env, 24);
    uint32_t tail = FLAT + 0x68;
    CHECK(a == FLAT + 8 && b == FLAT + 0x28 && c == FLAT + 0x48 && heap.rover == tail);
    for (unsigned i = 0; i < 24; ++i) flat[b - FLAT + i] = (uint8_t)(0xa0 + i);
    es2_heap_free(&heap, &env, a);
    CHECK(es2_heap_realloc(&heap, &env, b, 40) == a);
    CHECK(moves == 1 && flat[a - FLAT] == 0xa0 && flat[a - FLAT + 23] == 0xb7);
    CHECK(w(a - 4) == 49 && w(FLAT + 0x30) == 49 && w(FLAT + 0x34) == 16 && w(FLAT + 0x40) == 16);
    CHECK(w(S + 4) == FLAT + 0x38 && w(FLAT + 0x38) == S && w(FLAT + 0x3c) == tail && w(tail) == FLAT + 0x38);
    /* No room in place: a new block from the rover, 32 = 40 - 8 bytes copied, the old block freed. */
    uint32_t moved = es2_heap_realloc(&heap, &env, a, 100);
    CHECK(moved == tail && moves == 2 && flat[tail - FLAT] == 0xa0 && w(tail - 4) == 109);
    CHECK(w(a - 4) == 64 && w(a + 56) == 64);           /* freed: joined the 16-byte block above */
    uint32_t rover = heap.rover;
    CHECK(es2_heap_realloc(&heap, &env, 0, 5) == rover && heap.rover == rover + 16u);
    CHECK(es2_heap_realloc(&heap, &env, c, 0) == 0 && w(a - 4) == 96 && w(c + 24) == 96);   /* c joined a below */
    CHECK(es2_heap_calloc(&heap, &env, 0x10000, 0x10001) != 0 && fills == 1);   /* 0x100010000 wraps to 0x10000 */
    CHECK(outside == 0);
}

/* Growth: the rest of the reservation first, then a reservation that
 * directly follows (only the missing part committed, the region extended),
 * then one elsewhere (a new region). 1 MiB granularity. */
#define G1 0x20000000u
static void growth(void)
{
    fresh(1);
    script[script_count++] = G1;
    script[script_count++] = G1 + 0x100000u;   /* contiguous */
    script[script_count++] = 0x30000000u;      /* elsewhere */
    uint32_t a = es2_heap_malloc(&heap, &env, 0x18000);
    CHECK(a == G1 + 8 && calls[0].size == 0x100000u && call_count == 33 && all_commits(1, 32, G1) && heap.granularity == 0x100000u);
    unsigned before = call_count;
    uint32_t b = es2_heap_malloc(&heap, &env, 0x100000);
    /* 0xe0000 committed from the first reservation, then 0x200000 reserved
     * at its end: only 0x110000 - 0xe0000 = 0x30000 more committed. */
    CHECK(call_count == before + 224 + 1 + 48 && all_commits(before, 224, G1 + 0x20000u));
    CHECK(calls[before + 224].type == ES2_HEAP_MEM_RESERVE && calls[before + 224].size == 0x200000u && all_commits(before + 225, 48, G1 + 0x100000u));
    CHECK(heap.region_count == 1 && heap.region_size[0] == 0x130000u && heap.committed_end == G1 + 0x130000u && heap.reserved_end == G1 + 0x300000u);
    CHECK(b == G1 + 0x18010u && w(b - 4) == 0x100009u && w(G1 + 0x12fffc) == 1);
    before = call_count;
    uint32_t c = es2_heap_malloc(&heap, &env, 0x1d0000);   /* rest 0x1d0000 of the reservation, then a new one elsewhere */
    CHECK(call_count == before + 464 + 1 + 0x1e0 && calls[before + 464].size == 0x200000u && all_commits(before + 465, 0x1e0, 0x30000000u));
    /* Both parts are added before the search resumes: the rover block (the
     * rest above b, now joined with the 0x1d0000) satisfies it. */
    CHECK(heap.region_count == 2 && heap.region_base[1] == 0x30000000u && heap.region_size[1] == 0x1e0000u && c == G1 + 0x118018u);
    CHECK(heap.region_size[0] == 0x300000u && w(G1 + 0x2ffffc) == 1 && w(0x30000000u) == 1);
    CHECK(es2_heap_walk(&heap, read32, NULL, NULL, NULL) == 5);
}

/* OS failures: a refused reservation, a failed commit (the committed pages
 * are decommitted and the reservation released). */
static void failures_of_os(void)
{
    fresh(0);
    CHECK(es2_heap_malloc(&heap, &env, 100) == 0 && call_count == 1 && heap.granularity == 0x400000u && heap.region_count == 0);
    fresh(0);
    script[script_count++] = R1;
    fail_commit_at = 3;
    CHECK(es2_heap_malloc(&heap, &env, 100) == 0 && call_count == 1 + 3 + 2 + 1);
    CHECK(calls[4].kind == ES2_HEAP_OS_FREE && calls[4].address == R1 && calls[4].size == 0x1000u && calls[4].type == ES2_HEAP_MEM_DECOMMIT);
    CHECK(calls[5].address == R1 + 0x1000u && calls[6].address == R1 && calls[6].size == 0 && calls[6].type == ES2_HEAP_MEM_RELEASE);
    CHECK(heap.committed_end == 0 && heap.reserved_end == 0 && heap.region_count == 0);
}

/* 64 regions: the 65th reservation's range is committed but not added; the
 * search grows twice and gives up. */
static void region_limit(void)
{
    fresh(2);
    for (unsigned i = 0; i < 66; ++i) script[script_count++] = 0x40000000u + i * 0x200000u;
    for (unsigned i = 0; i < 64; ++i) CHECK(es2_heap_malloc(&heap, &env, 0xf0000) == 0x40000008u + i * 0x200000u);
    CHECK(heap.region_count == 64 && call_count == 64 * 257);
    unsigned before = call_count;
    CHECK(es2_heap_malloc(&heap, &env, 0xf0000) == 0 && call_count == before + 2 * 257 && heap.region_count == 64);
    CHECK(heap.committed_end == 0x40000000u + 65u * 0x200000u + 0x100000u);
}

/* Shadow env: replaying a recorded run gives the same results and no
 * difference; a changed record is reported at its index. Walk and attach. */
static void shadow(void)
{
    static uint32_t skeys[1u << 12], svalues[1u << 12], journal[256];
    static es2_heap_os_call recorded[64];
    fresh(0);
    script[script_count++] = R1;
    uint32_t a = es2_heap_malloc(&heap, &env, 1000);
    memcpy(recorded, calls, 17 * sizeof(calls[0]));
    es2_heap_shadow s;
    memset(&s, 0, sizeof(s));
    es2_heap_words_init(&s.words, skeys, svalues, 12);
    s.journal = journal; s.journal_capacity = 256;
    es2_heap_env senv;
    es2_heap_shadow_env(&senv, &s);
    es2_heap model;
    es2_heap_init(&model, &senv, S, 0);
    es2_heap_shadow_begin(&s, recorded, 17);
    CHECK(es2_heap_malloc(&model, &senv, 1000) == a && !es2_heap_shadow_finish(&s) && s.journal_count > 8 && !s.words.missing);
    CHECK(model.rover == heap.rover && model.region_size[0] == heap.region_size[0]);
    recorded[0].size = 0x100000u;
    es2_heap_init(&model, &senv, S, 0);
    es2_heap_shadow_begin(&s, recorded, 17);
    es2_heap_malloc(&model, &senv, 1000);
    CHECK(es2_heap_shadow_finish(&s) && s.os_mismatch_index == 0);
    es2_heap_shadow_begin(&s, recorded + 1, 16);   /* the reservation missing */
    es2_heap_init(&model, &senv, S, 0);
    CHECK(es2_heap_malloc(&model, &senv, 1000) == 0 && es2_heap_shadow_finish(&s));
    /* Attach: a copy of the running heap continues identically. */
    uint32_t b = es2_heap_malloc(&heap, &env, 50);
    es2_heap_free(&heap, &env, a);
    es2_heap_words_clear(&s.words);
    CHECK(es2_heap_attach(&model, &heap, &senv, read32, NULL) == 3 && s.words.count == 4 + 1 + 3 * 2 + 2 * 2 + 1);
    es2_heap_shadow_begin(&s, NULL, 0);
    CHECK(es2_heap_malloc(&model, &senv, 30) == es2_heap_malloc(&heap, &env, 30) && !es2_heap_shadow_finish(&s) && !s.words.missing);
    CHECK(es2_heap_realloc(&model, &senv, b, 4000) == es2_heap_realloc(&heap, &env, b, 4000) && model.rover == heap.rover);
    /* A broken end marker or footer makes the walk fail. */
    /* a + b (freed, joined) 1068, the 40-byte block, the 4008-byte block, the rest. */
    CHECK(es2_heap_walk(&heap, read32, NULL, NULL, NULL) == 4 && w(a - 4) == 1068);
    es2_heap_words_write(&store, R1 + 0xfffc, 0);
    CHECK(es2_heap_walk(&heap, read32, NULL, NULL, NULL) == -1);
    es2_heap_words_write(&store, R1 + 0xfffc, 1);
    es2_heap_words_write(&store, a + 1060, 7);
    CHECK(es2_heap_walk(&heap, read32, NULL, NULL, NULL) == -1);
}

int main(void)
{
    sizes();
    basics();
    reallocs();
    growth();
    failures_of_os();
    region_limit();
    shadow();
    if (failures) { fprintf(stderr, "rtl_heap: %u of %u checks failed\n", failures, checks); return 1; }
    printf("rtl_heap: %u checks passed\n", checks);
    return 0;
}
