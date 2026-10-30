/* The own DATA and CODE runs (sim/data/dbsim_data.c) against the reference
 * DBSIM.EXE (ES2_DATA_ROOT; SKIP without):
 * - every run equal to the placed file byte for byte;
 * - the runs written by es2_sim_dbsim_data_write into a zeroed image equal
 *   to the placed file over the whole initialised DATA and every CODE run
 *   (the spans), and zero everywhere else (BSS stays zero);
 * - every pointer run's slot a HIGHLOW relocation site and every site in the
 *   initialised DATA inside a pointer or dword run;
 * - strings without an inner NUL;
 * - the six index-byte tables and the dword 0x4967ae as CODE runs, and the 14
 *   type-descriptor counts the image-access measurement found as RTTI runs;
 * - every byte of the image-access map (tests/image_data_map.h) inside a span. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/image_file.h"
#include "es2/sim/image_data.h"
#include "image_data_map.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *data = NULL;
    if (!fseek(f, 0, SEEK_END)) {
        long n = ftell(f);
        if (n > 0 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n)) && fread(data, 1, (size_t)n, f) != (size_t)n) {
            free(data);
            data = NULL;
        }
        *size = n > 0 ? (size_t)n : 0;
    }
    fclose(f);
    return data;
}

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

static const es2_sim_data_run *run_at(uint32_t va) {
    for (unsigned i = 0; i < es2_sim_dbsim_data_run_count; ++i)
        if (es2_sim_dbsim_data_runs[i].va == va) return &es2_sim_dbsim_data_runs[i];
    return NULL;
}

static int in_span(uint32_t lo, uint32_t hi) {
    for (unsigned k = 0; k < es2_sim_dbsim_data_span_count; ++k) {
        const es2_sim_data_span *s = &es2_sim_dbsim_data_spans[k];
        if (lo >= s->va && (uint64_t)hi <= (uint64_t)s->va + s->size) return 1;
    }
    return 0;
}

int main(void) {
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) { puts("SKIP: ES2_DATA_ROOT is not set; no original data read"); return 0; }
    char path[1024];
    uint8_t *file = NULL;
    size_t size = 0;
    static const char *const rel[] = {"\\VER95\\DBSIM.EXE", "\\extracted\\cd\\VER95\\DBSIM.EXE"};
    for (unsigned i = 0; i < 2 && !file; ++i)
        if (snprintf(path, sizeof(path), "%s%s", root, rel[i]) < (int)sizeof(path)) file = read_file(path, &size);
    if (!file) { puts("SKIP: no DBSIM.EXE below ES2_DATA_ROOT; no original data read"); return 0; }
    const char *error = NULL;
    CHECK(es2_sim_image_gate(file, size, ES2_SIM_DBSIM_FILE_SIZE, ES2_SIM_DBSIM_SHA256, &error) == 0);
    es2_sim_image_info info;
    CHECK(es2_sim_image_parse(file, size, &info, &error) == 0);
    const uint32_t base = info.image_base, image_size = info.image_size;
    uint8_t *mapped = calloc(image_size, 1), *own = calloc(image_size, 1);
    CHECK(mapped && own);
    CHECK(es2_sim_image_place(file, size, &info, mapped, &error) == 0);

    /* The initialised DATA span is the DATA section's raw part. */
    const es2_sim_section *data = NULL;
    for (unsigned i = 0; i < info.section_count; ++i)
        if (!strcmp(info.sections[i].name, "DATA")) data = &info.sections[i];
    CHECK(data && es2_sim_dbsim_data_spans[0].va == base + data->rva && es2_sim_dbsim_data_spans[0].size == data->raw_size);
    const uint32_t init_lo = base + data->rva, init_hi = init_lo + data->raw_size;

    /* Written into zeroed memory. */
    CHECK(es2_sim_dbsim_data_write(own, base, image_size, &error) == 0);

    /* Every run equal to the file. */
    unsigned counts[7] = {0};
    for (unsigned i = 0; i < es2_sim_dbsim_data_run_count; ++i) {
        const es2_sim_data_run *r = &es2_sim_dbsim_data_runs[i];
        CHECK(in_span(r->va, r->va + r->size));
        if (memcmp(own + (r->va - base), mapped + (r->va - base), r->size)) {
            fprintf(stderr, "run 0x%08x (%u bytes) differs from the file\n", (unsigned)r->va, (unsigned)r->size);
            return 1;
        }
        if (r->kind == ES2_SIM_DATA_STRING) CHECK(strlen((const char *)r->data) + 1u == r->size);
        ++counts[r->kind];
    }
    /* The spans equal, everything else zero. */
    uint64_t span_bytes = 0;
    uint8_t *covered = calloc(image_size, 1);
    CHECK(covered);
    for (unsigned k = 0; k < es2_sim_dbsim_data_span_count; ++k) {
        const es2_sim_data_span *s = &es2_sim_dbsim_data_spans[k];
        CHECK(s->va >= base && (uint64_t)s->va + s->size <= (uint64_t)base + image_size);
        CHECK(memcmp(own + (s->va - base), mapped + (s->va - base), s->size) == 0);
        memset(covered + (s->va - base), 1, s->size);
        span_bytes += s->size;
    }
    uint64_t nonzero_outside = 0;
    for (uint32_t off = 0; off < image_size; ++off)
        if (own[off] && !covered[off]) ++nonzero_outside;
    CHECK(nonzero_outside == 0);
    free(covered);

    /* Relocation sites in the initialised DATA <-> pointer and dword runs. */
    unsigned sites = 0, slots = 0;
    for (uint32_t at = 0; at + 8u <= info.reloc_size;) {
        const uint8_t *b = mapped + info.reloc_rva + at;
        uint32_t page = rd32(b), block = rd32(b + 4);
        if (!block) break;
        CHECK(block >= 8u && at + block <= info.reloc_size);
        for (uint32_t k = 0; k < (block - 8u) / 2u; ++k) {
            uint32_t e = rd16(b + 8u + 2u * k);
            if (e >> 12 != 3u) continue;
            uint32_t va = base + page + (e & 0xfffu);
            if (va < init_lo || va >= init_hi) continue;
            ++sites;
            int found = 0;
            for (unsigned i = 0; i < es2_sim_dbsim_data_run_count && !found; ++i) {
                const es2_sim_data_run *r = &es2_sim_dbsim_data_runs[i];
                found = (r->kind == ES2_SIM_DATA_POINTERS || r->kind == ES2_SIM_DATA_DWORDS) && va >= r->va &&
                        va < r->va + r->size && (va - r->va) % 4u == 0;
            }
            if (!found) { fprintf(stderr, "relocation site 0x%08x in no pointer run\n", (unsigned)va); return 1; }
        }
        at += block;
    }
    for (unsigned i = 0; i < es2_sim_dbsim_data_run_count; ++i) {
        const es2_sim_data_run *r = &es2_sim_dbsim_data_runs[i];
        if (r->kind == ES2_SIM_DATA_POINTERS || r->kind == ES2_SIM_DATA_DWORDS) slots += r->size / 4u;
    }
    CHECK(sites == slots);

    /* The CODE runs the row names, and the measured descriptor counts. */
    static const uint32_t tables[] = {0x4100fdu, 0x405366u, 0x426492u, 0x436be2u, 0x44bb9fu, 0x44e1f4u, 0x4967aeu};
    for (unsigned i = 0; i < sizeof(tables) / sizeof(tables[0]); ++i) {
        const es2_sim_data_run *r = run_at(tables[i]);
        CHECK(r && r->kind == ES2_SIM_DATA_CODE_TABLE);
    }
    CHECK(run_at(0x4967aeu)->size == 4u);
    /* own_process.md, "what es2sim really reads from the mapped image": the operand VAs (0x427000 was the faulting byte of 0x426ffd). */
    static const uint32_t measured[] = {0x408310u, 0x40ab5du, 0x40b61du, 0x40c3f8u, 0x40f7b6u, 0x41bd16u, 0x424004u,
                                        0x426ffdu, 0x45c573u, 0x46bb91u, 0x47cf71u, 0x4823a6u, 0x4836e0u, 0x490cfcu};
    for (unsigned i = 0; i < sizeof(measured) / sizeof(measured[0]); ++i) {
        const es2_sim_data_run *r = run_at(measured[i]);
        CHECK(r && r->kind == ES2_SIM_DATA_RTTI_COUNT && r->size == 4u);
    }
    for (unsigned i = 0; i < sizeof(image_data_code_reads) / sizeof(image_data_code_reads[0]); ++i)
        CHECK(in_span(image_data_code_reads[i], image_data_code_reads[i] + 1u));

    /* The image-access map. */
    unsigned map_bytes = 0;
    for (unsigned i = 0; i < sizeof(image_data_map) / sizeof(image_data_map[0]); ++i) {
        if (!in_span(image_data_map[i][0], image_data_map[i][1])) {
            fprintf(stderr, "map range 0x%08x..0x%08x not covered\n", (unsigned)image_data_map[i][0],
                    (unsigned)image_data_map[i][1]);
            return 1;
        }
        map_bytes += image_data_map[i][1] - image_data_map[i][0];
    }
    printf("PASS image_data_real_test: %u runs (bytes %u, strings %u, pointers %u, dwords %u, code tables %u, "
           "rtti counts %u), %u spans with %llu bytes, %u relocation slots, %u map ranges (%u bytes)\n",
           es2_sim_dbsim_data_run_count, counts[ES2_SIM_DATA_BYTES], counts[ES2_SIM_DATA_STRING],
           counts[ES2_SIM_DATA_POINTERS], counts[ES2_SIM_DATA_DWORDS], counts[ES2_SIM_DATA_CODE_TABLE],
           counts[ES2_SIM_DATA_RTTI_COUNT], es2_sim_dbsim_data_span_count, (unsigned long long)span_bytes, slots,
           (unsigned)(sizeof(image_data_map) / sizeof(image_data_map[0])), map_bytes);
    free(mapped);
    free(own);
    free(file);
    return 0;
}
