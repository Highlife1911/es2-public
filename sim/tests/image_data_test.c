/* The run writer (src/image_data.c) on synthetic runs: the refusals (overlap,
 * order, bounds, empty, dword size, string NUL, kind, data), the write order
 * and little-endian dwords, nothing written on a refusal, bytes outside the
 * runs left alone; and the compiled-in DBSIM table's shape (sorted, inside the
 * image, spans). No game data. */
#include <stdio.h>
#include <string.h>
#include "es2/sim/image_data.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static const uint8_t bytes_a[3] = {0x11, 0x22, 0x33};
static const char text_b[4] = "abc";
static const uint32_t dwords_c[2] = {0x00401000u, 0xdeadbeefu};
static const uint8_t table_d[2] = {0x05, 0x00};
static const uint32_t count_e[1] = {7u};
static const char no_nul[3] = {'x', 'y', 'z'};

static int refused(const es2_sim_data_run *runs, unsigned n, const char *expect) {
    uint8_t guest[64];
    memset(guest, 0xcc, sizeof(guest));
    const char *error = NULL;
    if (es2_sim_data_write(runs, n, guest, 0x1000u, sizeof(guest), &error) != 1) return 0;
    if (!error || !strstr(error, expect)) {
        fprintf(stderr, "error %s, expected %s\n", error ? error : "(none)", expect);
        return 0;
    }
    for (unsigned i = 0; i < sizeof(guest); ++i)
        if (guest[i] != 0xcc) return 0;   /* nothing written */
    return 1;
}

int main(void) {
    const es2_sim_data_run good[] = {
        {0x1000u, 3u, ES2_SIM_DATA_BYTES, bytes_a},
        {0x1004u, 4u, ES2_SIM_DATA_STRING, text_b},
        {0x1008u, 8u, ES2_SIM_DATA_POINTERS, dwords_c},
        {0x1010u, 2u, ES2_SIM_DATA_CODE_TABLE, table_d},
        {0x1012u, 4u, ES2_SIM_DATA_RTTI_COUNT, count_e},
        {0x103cu, 4u, ES2_SIM_DATA_DWORDS, count_e},
    };
    uint8_t guest[64];
    memset(guest, 0xcc, sizeof(guest));
    const char *error = NULL;
    CHECK(es2_sim_data_write(good, 6, guest, 0x1000u, sizeof(guest), &error) == 0);
    static const uint8_t expect[0x16] = {0x11, 0x22, 0x33, 0xcc, 'a', 'b', 'c', 0, 0x00, 0x10, 0x40, 0x00,
                                         0xef, 0xbe, 0xad, 0xde, 0x05, 0x00, 7, 0, 0, 0};
    CHECK(memcmp(guest, expect, sizeof(expect)) == 0);
    for (unsigned i = 0x16; i < 0x3c; ++i) CHECK(guest[i] == 0xcc);   /* outside the runs: untouched */
    CHECK(guest[0x3c] == 7 && guest[0x3f] == 0);
    CHECK(es2_sim_data_write(good, 0, guest, 0x1000u, sizeof(guest), &error) == 0);   /* empty table */

    /* Refusals, nothing written. */
    const es2_sim_data_run overlap[] = {{0x1000u, 4u, ES2_SIM_DATA_BYTES, bytes_a},
                                        {0x1003u, 2u, ES2_SIM_DATA_CODE_TABLE, table_d}};
    CHECK(refused(overlap, 2, "overlapping"));
    const es2_sim_data_run order[] = {{0x1008u, 2u, ES2_SIM_DATA_CODE_TABLE, table_d},
                                      {0x1000u, 3u, ES2_SIM_DATA_BYTES, bytes_a}};
    CHECK(refused(order, 2, "out of order"));
    const es2_sim_data_run below[] = {{0x0fffu, 2u, ES2_SIM_DATA_CODE_TABLE, table_d}};
    CHECK(refused(below, 1, "outside"));
    const es2_sim_data_run beyond[] = {{0x103fu, 2u, ES2_SIM_DATA_CODE_TABLE, table_d}};
    CHECK(refused(beyond, 1, "outside"));
    const es2_sim_data_run wrap[] = {{0xffffffffu, 2u, ES2_SIM_DATA_CODE_TABLE, table_d}};
    CHECK(refused(wrap, 1, "outside"));
    const es2_sim_data_run empty[] = {{0x1000u, 0u, ES2_SIM_DATA_BYTES, bytes_a}};
    CHECK(refused(empty, 1, "empty"));
    const es2_sim_data_run odd[] = {{0x1000u, 6u, ES2_SIM_DATA_POINTERS, dwords_c}};
    CHECK(refused(odd, 1, "multiple of 4"));
    const es2_sim_data_run nul[] = {{0x1000u, 3u, ES2_SIM_DATA_STRING, no_nul}};
    CHECK(refused(nul, 1, "NUL"));
    const es2_sim_data_run kind[] = {{0x1000u, 3u, (es2_sim_data_kind)9, bytes_a}};
    CHECK(refused(kind, 1, "kind"));
    const es2_sim_data_run nodata[] = {{0x1000u, 3u, ES2_SIM_DATA_BYTES, NULL}};
    CHECK(refused(nodata, 1, "without data"));
    /* A late refusal (the last run) still writes nothing. */
    const es2_sim_data_run late[] = {{0x1000u, 3u, ES2_SIM_DATA_BYTES, bytes_a},
                                     {0x1004u, 4u, ES2_SIM_DATA_STRING, text_b},
                                     {0x1006u, 2u, ES2_SIM_DATA_CODE_TABLE, table_d}};
    CHECK(refused(late, 3, "overlapping"));
    CHECK(es2_sim_data_write(good, 6, NULL, 0x1000u, 64u, &error) == 1);

    /* The DBSIM table: sorted and inside the image; the spans hold every run. */
    CHECK(es2_sim_dbsim_data_run_count > 0u && es2_sim_dbsim_data_span_count > 0u);
    CHECK(es2_sim_data_check(es2_sim_dbsim_data_runs, es2_sim_dbsim_data_run_count, 0x00400000u, 0x002d8000u,
                             &error) == 0);
    CHECK(es2_sim_data_check(es2_sim_dbsim_data_runs, es2_sim_dbsim_data_run_count, 0x00497000u, 0x00010c00u,
                             &error) == 1);   /* the CODE runs lie below DATA */
    CHECK(es2_sim_dbsim_data_spans[0].va == 0x00497000u && es2_sim_dbsim_data_spans[0].size == 0x00010c00u);
    for (unsigned i = 0; i < es2_sim_dbsim_data_run_count; ++i) {
        const es2_sim_data_run *r = &es2_sim_dbsim_data_runs[i];
        int inside = 0;
        for (unsigned k = 0; k < es2_sim_dbsim_data_span_count && !inside; ++k) {
            const es2_sim_data_span *s = &es2_sim_dbsim_data_spans[k];
            inside = r->va >= s->va && (uint64_t)r->va + r->size <= (uint64_t)s->va + s->size;
        }
        CHECK(inside);
        CHECK((r->kind == ES2_SIM_DATA_CODE_TABLE || r->kind == ES2_SIM_DATA_RTTI_COUNT) == (r->va < 0x00497000u));
    }
    puts("PASS image_data_test");
    return 0;
}
