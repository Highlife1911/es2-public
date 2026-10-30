/* The writer of the own DATA and CODE runs (es2/sim/image_data.h;
 * own_process.md, "the initialised DATA and the CODE bytes as own runs"). The
 * runs come from sim/data/dbsim_data.c. */
#include "es2/sim/image_data.h"

static int dword_kind(es2_sim_data_kind kind) {
    return kind == ES2_SIM_DATA_POINTERS || kind == ES2_SIM_DATA_DWORDS || kind == ES2_SIM_DATA_RTTI_COUNT;
}

int es2_sim_data_check(const es2_sim_data_run *runs, unsigned count, uint32_t guest_va, uint32_t guest_size,
                       const char **error) {
    const char *e = NULL;
    uint64_t end = (uint64_t)guest_va + guest_size, prev_end = 0;
    if (!runs && count) e = "no run table";
    for (unsigned i = 0; !e && i < count; ++i) {
        const es2_sim_data_run *r = &runs[i];
        uint64_t lo = r->va, hi = lo + r->size;
        if (!r->data) e = "a run without data";
        else if (r->kind < ES2_SIM_DATA_BYTES || r->kind > ES2_SIM_DATA_RTTI_COUNT) e = "a run of unknown kind";
        else if (!r->size) e = "an empty run";
        else if (dword_kind(r->kind) && r->size % 4u) e = "a dword run whose size is not a multiple of 4";
        else if (r->kind == ES2_SIM_DATA_STRING && ((const char *)r->data)[r->size - 1u]) e = "a string without its NUL";
        else if (lo < guest_va || hi > end) e = "a run outside the guest range";
        else if (i && lo < prev_end) e = "runs out of order or overlapping";
        prev_end = hi;
    }
    if (e && error) *error = e;
    return e != NULL;
}

int es2_sim_data_write(const es2_sim_data_run *runs, unsigned count, uint8_t *guest, uint32_t guest_va,
                       uint32_t guest_size, const char **error) {
    if (!guest) {
        if (error) *error = "no guest memory";
        return 1;
    }
    if (es2_sim_data_check(runs, count, guest_va, guest_size, error)) return 1;
    for (unsigned i = 0; i < count; ++i) {
        const es2_sim_data_run *r = &runs[i];
        uint8_t *to = guest + (r->va - guest_va);
        if (dword_kind(r->kind)) {
            const uint32_t *v = r->data;
            for (uint32_t k = 0; k < r->size / 4u; ++k) {
                to[4u * k] = (uint8_t)v[k];
                to[4u * k + 1u] = (uint8_t)(v[k] >> 8);
                to[4u * k + 2u] = (uint8_t)(v[k] >> 16);
                to[4u * k + 3u] = (uint8_t)(v[k] >> 24);
            }
        } else {
            const uint8_t *b = r->data;
            for (uint32_t k = 0; k < r->size; ++k) to[k] = b[k];
        }
    }
    return 0;
}

int es2_sim_dbsim_data_write(uint8_t *guest, uint32_t guest_va, uint32_t guest_size, const char **error) {
    return es2_sim_data_write(es2_sim_dbsim_data_runs, es2_sim_dbsim_data_run_count, guest, guest_va, guest_size,
                              error);
}

int es2_sim_data_rtti_count(const es2_sim_data_run *runs, unsigned count, uint32_t va, uint32_t *value) {
    unsigned lo = 0, hi = count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2u;
        if (runs[mid].va < va) lo = mid + 1u;
        else hi = mid;
    }
    if (lo == count || runs[lo].va != va || runs[lo].kind != ES2_SIM_DATA_RTTI_COUNT || runs[lo].size != 4u) return 1;
    *value = ((const uint32_t *)runs[lo].data)[0];
    return 0;
}

int es2_sim_dbsim_rtti_count(uint32_t va, uint32_t *value) {
    return es2_sim_data_rtti_count(es2_sim_dbsim_data_runs, es2_sim_dbsim_data_run_count, va, value);
}
