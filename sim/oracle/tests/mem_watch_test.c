/* //sim/oracle:mem_watch on a synthetic guest view (own bytes only): the
 * parser, whole and diff records, pointer chains, counted lengths, the
 * unreadable event, the view callback, the order of the saved copy after
 * emit, and the phase callbacks. The proxy's mem_watch_* fixtures compare
 * the same code with the frozen baseline in the DBSIM process layout. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim/oracle/mem_watch.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define BASE 0x00500000u
#define SIZE 0x10000u
#define HOLE 0x00508000u   /* one unreadable page */
static uint8_t arena[SIZE];
static char out[1 << 16];
static size_t used;
static unsigned lines, copies, views, null_views, begins[2], ends, depth;
static char line[ES2_MEM_WATCH_LINE_MIN + 4096];
static int change_on_emit;

static int in_range(uint32_t a, uint32_t n) {
    if (a < BASE || n > SIZE || a - BASE > SIZE - n) return 0;
    return !(a < HOLE + 0x1000u && HOLE < a + n);
}
static int copy(void *user, void *dst, uint32_t a, uint32_t n) {
    (void)user;
    ++copies;
    if (!in_range(a, n)) return 0;
    memcpy(dst, arena + (a - BASE), n);
    return 1;
}
static const uint8_t *view(void *user, uint32_t a, uint32_t n) {
    (void)user;
    ++views;
    if (null_views) { --null_views; return NULL; }
    return in_range(a, n) ? arena + (a - BASE) : NULL;
}
static void emit(void *user, const char *text, int n) {
    (void)user;
    if (n <= 0 || used + (size_t)n >= sizeof(out)) { fputs("FAIL: output overflow\n", stderr); exit(1); }
    memcpy(out + used, text, (size_t)n);
    used += (size_t)n;
    out[used] = 0;
    ++lines;
    if (change_on_emit) ++arena[0x100];
}
static unsigned begin(void *user, unsigned phase) { (void)user; if (phase < 2) ++begins[phase]; return depth++; }
static void end(void *user, unsigned token) { (void)user; --depth; if (token != depth) { fputs("FAIL: phase order\n", stderr); exit(1); } ++ends; }

static es2_mem_watch_table table;
static const es2_mem_watch_access access = {NULL, copy, view, emit, begin, end};
static const es2_mem_watch_access plain = {NULL, copy, view, emit, NULL, NULL};

static void run(unsigned long sequence, unsigned long frame) {
    used = 0; out[0] = 0; lines = 0;
    es2_mem_watch_check(&table, sequence, frame, &access, line, sizeof(line));
}
static void put32(uint32_t a, uint32_t v) { for (unsigned i = 0; i < 4; ++i) arena[a - BASE + i] = (uint8_t)(v >> (8 * i)); }

static int parser(void) {
    static const char *bad[] = {
        "", " ", "name", "name 0x500000", "na-me 0x500000 4", "x 0x500000 0", "x 0x500000 16385", "x zz 4",
        "x 0x500000@1@2@3@4@5@6@7 4", "x 0x500000@ 4", "x 0x500000 4 junk", "x 0x500000 w0x500010", "x 0x500000 w0x500010*0",
        "x 0x500000 w*4", "x 0x500000\t4x", "abcdefghijabcdefghijabcdefghijab 0x500000 4",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        if (es2_mem_watch_add(&table, bad[i]) != -1) { fprintf(stderr, "FAIL: accepted \"%s\"\n", bad[i]); return 1; }
        CHECK(table.count == 0);
    }
    CHECK(!es2_mem_watch_add(&table, "abcdefghijabcdefghijabcdefghija 0x500000 4"));  /* 31 characters */
    CHECK(!es2_mem_watch_add(&table, "\t chain 0x500010@0x10@2 6\r\n"));
    CHECK(!es2_mem_watch_add(&table, "counted 5242912 w0x500040*3"));
    CHECK(table.count == 3 && !strcmp(table.watches[1].name, "chain") && table.watches[1].base == 0x500010u &&
          table.watches[1].steps == 2 && table.watches[1].offsets[0] == 0x10u && table.watches[1].offsets[1] == 2u &&
          table.watches[1].length == 6u && !table.watches[1].count_address);
    CHECK(table.watches[2].base == 0x500020u && table.watches[2].count_address == 0x500040u && table.watches[2].length == 3u);
    es2_mem_watch_clear(&table);
    CHECK(table.count == 0);
    for (unsigned i = 0; i < ES2_MEM_WATCH_MAX; ++i) CHECK(!es2_mem_watch_add(&table, "full 0x500000 1"));
    CHECK(es2_mem_watch_add(&table, "full 0x500000 1") == -1 && table.count == ES2_MEM_WATCH_MAX);
    es2_mem_watch_clear(&table);
    return 0;
}

static int records(void) {
    for (unsigned i = 0; i < 8; ++i) arena[i] = (uint8_t)(0xa0 + i);
    CHECK(!es2_mem_watch_add(&table, "small 0x500000 8"));
    run(1, 7);
    CHECK(lines == 1 && !strcmp(out, "{\"event\":\"watch\",\"sequence\":1,\"frame\":7,\"name\":\"small\",\"address\":\"0x00500000\","
                                    "\"length\":8,\"hex\":\"a0a1a2a3a4a5a6a7\"}\r\n"));
    run(2, 8);
    CHECK(lines == 0 && used == 0);
    arena[3] = 0x33;
    run(3, 9);   /* a range up to 256 bytes is written whole on every change */
    CHECK(!strcmp(out, "{\"event\":\"watch\",\"sequence\":3,\"frame\":9,\"name\":\"small\",\"address\":\"0x00500000\","
                       "\"length\":8,\"hex\":\"a0a1a233a4a5a6a7\"}\r\n"));
    es2_mem_watch_clear(&table);

    /* Above 256 bytes: diff runs; gaps of fewer than 4 unchanged bytes join. */
    memset(arena + 0x1000, 0, 300);
    CHECK(!es2_mem_watch_add(&table, "large 0x501000 300"));
    run(1, 0);
    CHECK(lines == 1 && strstr(out, "\"length\":300,\"hex\":\"0000"));
    arena[0x1000 + 10] = 1; arena[0x1000 + 14] = 2;     /* 3 unchanged between: one run */
    arena[0x1000 + 20] = 3; arena[0x1000 + 25] = 4;     /* 4 unchanged between: two runs */
    arena[0x1000 + 299] = 5;
    run(2, 1);
    CHECK(!strcmp(out, "{\"event\":\"watch\",\"sequence\":2,\"frame\":1,\"name\":\"large\",\"address\":\"0x00501000\","
                       "\"length\":300,\"diff\":[[10,\"0100000002\"],[20,\"03\"],[25,\"04\"],[299,\"05\"]]}\r\n"));
    es2_mem_watch_clear(&table);
    return 0;
}

static int chains(void) {
    /* [[0x502000]+4]+2 */
    put32(0x502000u, 0x502100u);
    put32(0x502104u, 0x502200u);
    memcpy(arena + 0x2202, "\x11\x22\x33\x44", 4);
    CHECK(!es2_mem_watch_add(&table, "route 0x502000@4@2 4"));
    run(1, 0);
    CHECK(!strcmp(out, "{\"event\":\"watch\",\"sequence\":1,\"frame\":0,\"name\":\"route\",\"address\":\"0x00502202\","
                       "\"length\":4,\"hex\":\"11223344\"}\r\n"));
    /* Same bytes at a new address: written whole again. */
    put32(0x502104u, 0x502300u);
    memcpy(arena + 0x2302, "\x11\x22\x33\x44", 4);
    run(2, 1);
    CHECK(lines == 1 && strstr(out, "\"address\":\"0x00502302\""));
    /* A null pointer: one unreadable event, then silence, then whole again. */
    put32(0x502104u, 0);
    run(3, 2);
    CHECK(!strcmp(out, "{\"event\":\"watch\",\"sequence\":3,\"frame\":2,\"name\":\"route\",\"unreadable\":true}\r\n"));
    run(4, 3);
    CHECK(lines == 0);
    put32(0x502104u, 0x502300u);
    run(5, 4);
    CHECK(lines == 1 && strstr(out, "\"hex\":\"11223344\""));
    /* A pointer into the hole, and a range that crosses into it. */
    put32(0x502104u, HOLE - 2u);
    run(6, 5);
    CHECK(lines == 1 && strstr(out, "\"unreadable\":true"));
    es2_mem_watch_clear(&table);
    /* The view refusing a probed range counts as unreadable. */
    CHECK(!es2_mem_watch_add(&table, "viewed 0x500000 4"));
    null_views = 1;
    run(1, 0);
    CHECK(lines == 1 && strstr(out, "\"name\":\"viewed\",\"unreadable\":true"));
    run(2, 1);
    CHECK(lines == 1 && strstr(out, "\"hex\":\"a0a1a233\""));
    es2_mem_watch_clear(&table);
    return 0;
}

static int counted(void) {
    for (unsigned i = 0; i < 16; ++i) arena[0x3000 + i] = (uint8_t)i;
    arena[0x3100] = 3; arena[0x3101] = 0;       /* 3 elements of 2 bytes */
    CHECK(!es2_mem_watch_add(&table, "list 0x503000 w0x503100*2"));
    run(1, 0);
    CHECK(lines == 1 && strstr(out, "\"length\":6,\"hex\":\"000102030405\""));
    arena[0x3100] = 4;                           /* length change: whole */
    run(2, 1);
    CHECK(lines == 1 && strstr(out, "\"length\":8,\"hex\":\"0001020304050607\""));
    arena[0x3100] = 0;                           /* zero */
    run(3, 2);
    CHECK(lines == 1 && strstr(out, "\"unreadable\":true"));
    arena[0x3100] = 0xff; arena[0x3101] = 0xff;  /* -1 as a signed word */
    run(4, 3);
    CHECK(lines == 0);
    es2_mem_watch_clear(&table);
    CHECK(!es2_mem_watch_add(&table, "big 0x503000 w0x503100*16384"));
    arena[0x3100] = 2; arena[0x3101] = 0;        /* 2 * 16384 exceeds the limit */
    run(1, 0);
    CHECK(lines == 1 && strstr(out, "\"unreadable\":true"));
    es2_mem_watch_clear(&table);
    return 0;
}

static int order_and_phases(void) {
    /* emit changes the watched byte: the saved copy is read after emit, so
     * the next check sees no change (the proxy's order). */
    arena[0x100] = 1;
    CHECK(!es2_mem_watch_add(&table, "during_emit 0x500100 16"));
    change_on_emit = 1;
    run(1, 0);
    change_on_emit = 0;
    CHECK(lines == 1 && arena[0x100] == 2 && table.watches[0].previous[0] == 2);
    run(2, 1);
    CHECK(lines == 0);
    es2_mem_watch_clear(&table);
    /* Phases: one WATCH per watch and one more per written record, one FORMAT
     * per line, every begin ended; copies: steps + count + probe. */
    put32(0x502104u, 0x502300u);
    CHECK(!es2_mem_watch_add(&table, "a 0x500000 4"));
    CHECK(!es2_mem_watch_add(&table, "b 0x502000@4@2 4"));
    CHECK(!es2_mem_watch_add(&table, "c 0x503000 w0x503100*2"));
    begins[0] = begins[1] = ends = copies = views = 0;
    run(1, 0);
    CHECK(lines == 3 && begins[0] == 6 && begins[1] == 3 && ends == 9 && depth == 0 && copies == 1 + 3 + 2 && views == 3);
    begins[0] = begins[1] = ends = 0;
    run(2, 1);
    CHECK(lines == 0 && begins[0] == 3 && begins[1] == 0 && ends == 3);
    /* Without a timer the output is the same. */
    for (unsigned i = 0; i < table.count; ++i) table.watches[i].previous_length = 0;
    used = 0; out[0] = 0; lines = 0;
    es2_mem_watch_check(&table, 1, 0, &plain, line, sizeof(line));
    CHECK(lines == 3);
    es2_mem_watch_clear(&table);
    return 0;
}

int main(void) {
    if (parser() || records() || chains() || counted() || order_and_phases()) return 1;
    puts("PASS: mem_watch: parser, whole and diff records, chains, counted lengths, unreadable, emit order, phases");
    return 0;
}
