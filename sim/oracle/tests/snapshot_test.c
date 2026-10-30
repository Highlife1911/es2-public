/* //sim/oracle:snapshot_test: the ES2SNAP1 v2 writer on synthetic memory
 * (a 3 MiB image range and one heap region with two blocks), read back
 * field by field; the extra-region parser. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim/oracle/snapshot.h"

#define IMAGE_BASE 0x00400000u
#define HEAP_BASE 0x20000000u
#define HEAP_SIZE 0x40u

static uint8_t image[ES2_SNAPSHOT_IMAGE_SIZE];
static uint8_t heap[HEAP_SIZE];
static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static uint8_t *at(uint32_t va, uint32_t n) {
    if (va >= IMAGE_BASE && va - IMAGE_BASE + (uint64_t)n <= sizeof(image)) return image + (va - IMAGE_BASE);
    if (va >= HEAP_BASE && va - HEAP_BASE + (uint64_t)n <= sizeof(heap)) return heap + (va - HEAP_BASE);
    return NULL;
}
static int readable(void *u, uint32_t va, uint32_t n) { (void)u; return at(va, n) != NULL; }
static void read_bytes(void *u, uint32_t va, void *dst, uint32_t n) { (void)u; memcpy(dst, at(va, n), n); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int main(void) {
    /* The image: the frame counter, a pattern in the first fixed region. */
    put32(image + (0x004d24bcu + 0xbfu - IMAGE_BASE), 0x1234u);
    for (uint32_t i = 0; i < 16u; ++i) image[0x004a9572u - IMAGE_BASE + i] = (uint8_t)(0xa0u + i);
    /* The heap region: marker 1, a used block of 0x18 at +8, a free block of
     * 0x20 at +0x20, the end marker (CW3220's layout). */
    put32(heap, 1u);
    put32(heap + 4u, 0x18u | 1u);                /* header of the block at +8 */
    memset(heap + 8u, 0x5a, 0x10u);
    put32(heap + 8u + 0x18u - 8u, 0x18u | 1u);   /* its footer */
    put32(heap + 0x20u - 4u, 0x20u);             /* header of the free block at +0x20 */
    put32(heap + 0x20u + 0x20u - 8u, 0x20u);     /* its footer */
    put32(heap + HEAP_SIZE - 4u, 1u);
    es2_snapshot_region extra[4];
    unsigned extra_count = 0;
    CHECK(!es2_snapshot_parse_extra("0x49dd94:8, 0x49f8fc:5,0x49aef6:0x2", extra, 4, &extra_count));
    CHECK(extra_count == 3 && extra[0].va == 0x0049dd94u && extra[0].length == 8u && extra[2].length == 2u);
    unsigned bad_count;
    CHECK(es2_snapshot_parse_extra("0x49dd94", extra + 3, 1, &bad_count));
    CHECK(es2_snapshot_parse_extra("0x300000:4", extra + 3, 1, &bad_count));
    CHECK(es2_snapshot_parse_extra("0x49dd94:0", extra + 3, 1, &bad_count));
    CHECK(!es2_snapshot_parse_extra("   ", extra + 3, 1, &bad_count) && bad_count == 0);

    uint32_t base[1] = {HEAP_BASE}, size[1] = {HEAP_SIZE};
    es2_snapshot_map_entry map[1] = {{HEAP_BASE, 0x10000u, HEAP_BASE, 4u, 0x20000u}};
    es2_snapshot_module modules[1] = {{IMAGE_BASE, ES2_SNAPSHOT_IMAGE_SIZE, "DBSIM.EXE"}};
    es2_snapshot_source source = {NULL, readable, read_bytes};
    es2_snapshot_input in = {0x1234u, IMAGE_BASE, ES2_SNAPSHOT_IMAGE_SIZE, ES2_SNAPSHOT_FLAG_ARMED_EARLY | ES2_SNAPSHOT_FLAG_OWN_HEAP,
                             extra, extra_count, 0, 1, 1, base, size, 1u << 20, map, 1, modules, 1, 0};
    uint8_t *file = NULL;
    size_t length = 0;
    CHECK(!es2_snapshot_build(&source, &in, &file, &length));
    if (!file) { printf("FAIL no file\n"); return 1; }
    const uint8_t *p = file;
    CHECK(!memcmp(p, "ES2SNAP1", 8));
    CHECK(get32(p + 8) == 2u);
    uint32_t flags = get32(p + 12);
    CHECK(flags == (1u | 16u | 32u | 128u));
    CHECK(get32(p + 16) == 0x1234u && get32(p + 20) == IMAGE_BASE && get32(p + 24) == ES2_SNAPSHOT_IMAGE_SIZE);
    CHECK(get32(p + 28) == ES2_SNAPSHOT_FIXED_COUNT + 3u);
    p += 32;
    for (unsigned i = 0; i < ES2_SNAPSHOT_FIXED_COUNT + 3u; ++i) {
        uint32_t va = get32(p), n = get32(p + 4), ok = get32(p + 8);
        p += 12;
        CHECK(ok == 1u);
        if (i == 0) CHECK(va == 0x004a9572u && p[0] == 0xa0u && p[15] == 0xafu);
        if (i == ES2_SNAPSHOT_FIXED_COUNT) CHECK(va == 0x0049dd94u && n == 8u);
        p += n;
    }
    CHECK(get32(p) == 1u);                      /* one heap region */
    CHECK(get32(p + 4) == HEAP_BASE && get32(p + 8) == HEAP_SIZE && get32(p + 12) == 1u && get32(p + 16) == 2u);
    p += 20;
    CHECK(get32(p) == HEAP_BASE + 8u && get32(p + 4) == 0x18u && get32(p + 8) == 3u);
    p += 12;
    CHECK(p[0] == 0x5a && p[15] == 0x5a);
    p += 0x10u;
    CHECK(get32(p) == HEAP_BASE + 0x20u && get32(p + 4) == 0x20u && get32(p + 8) == 0u);
    p += 12;
    CHECK(get32(p) == 1u && get32(p + 4) == HEAP_BASE && get32(p + 20) == 0x20000u);
    p += 24;
    CHECK(get32(p) == 1u && get32(p + 4) == IMAGE_BASE && get32(p + 12) == 9u && !memcmp(p + 16, "DBSIM.EXE", 9));
    p += 25;
    CHECK((size_t)(p - file) == length);
    /* With ES2_SNAPSHOT_TEST_OUT the first file is also written there (for a
     * manual after_load_snapshot.py summary). */
    const char *dump = getenv("ES2_SNAPSHOT_TEST_OUT");
    if (dump) {
        FILE *f = fopen(dump, "wb");
        if (f) { fwrite(file, 1, length, f); fclose(f); }
    }
    free(file);

    /* A malformed region (no end marker) gives status 0 and flag 4; a
     * missing heap flag 2; max_bytes 0 leaves the used block without
     * contents and sets flag 8. */
    put32(heap + HEAP_SIZE - 4u, 7u);
    CHECK(!es2_snapshot_build(&source, &in, &file, &length));
    CHECK(get32(file + 12) & ES2_SNAPSHOT_FLAG_WALK_ERROR);
    free(file);
    put32(heap + HEAP_SIZE - 4u, 1u);
    in.max_bytes = 0;
    CHECK(!es2_snapshot_build(&source, &in, &file, &length));
    CHECK(get32(file + 12) & ES2_SNAPSHOT_FLAG_TRUNCATED);
    free(file);
    in.heap_available = 0;
    CHECK(!es2_snapshot_build(&source, &in, &file, &length));
    CHECK(get32(file + 12) == (2u | 16u | 32u | 128u));
    free(file);

    if (failures) return 1;
    printf("PASS snapshot_test\n");
    return 0;
}
