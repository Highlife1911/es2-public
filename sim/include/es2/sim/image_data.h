/* DBSIM's initialised DATA and the CODE bytes es2sim reads, as own runs
 * (own_process.md, "the initialised DATA and the CODE bytes as own runs").
 *
 * sim/data/dbsim_data.c is generated from the reference DBSIM.EXE by
 * tools/reference-game/scripts/dbsim_data.py and compared with the file by
 * //sim:image_data_real_test. Every run carries its guest VA; written into
 * zeroed memory at the original addresses, the runs give the original's
 * initialised DATA (0x497000..0x4a7c00) and the CODE ranges own code reads (the
 * index-byte tables, the dword 0x4967ae, the type descriptors' counts at
 * +0x20). The host builds the guest image from them
 * (own_process.md, "es2sim without DBSIM.EXE"). */
#ifndef ES2_SIM_IMAGE_DATA_H
#define ES2_SIM_IMAGE_DATA_H
#include <stddef.h>
#include <stdint.h>

/* What a run's `data` holds. Dword kinds: size / 4 values of uint32_t,
 * written little-endian. String: size chars, the last the NUL. Bytes and
 * CODE table: size uint8_t. */
typedef enum es2_sim_data_kind {
    ES2_SIM_DATA_BYTES = 1,        /* other bytes of the initialised DATA */
    ES2_SIM_DATA_STRING = 2,       /* a NUL-terminated string literal */
    ES2_SIM_DATA_POINTERS = 3,     /* relocated slots: VA constants (code, data, bss) */
    ES2_SIM_DATA_DWORDS = 4,       /* relocated slots whose target is unexplained, as plain dwords */
    ES2_SIM_DATA_CODE_TABLE = 5,   /* bytes inside CODE that own code reads */
    ES2_SIM_DATA_RTTI_COUNT = 6    /* the dword +0x20 of a type descriptor in CODE */
} es2_sim_data_kind;

typedef struct es2_sim_data_run {
    uint32_t va;
    uint32_t size;
    es2_sim_data_kind kind;
    const void *data;
} es2_sim_data_run;

/* A guest range the runs reproduce in full when written into zeroed memory. */
typedef struct es2_sim_data_span {
    uint32_t va;
    uint32_t size;
} es2_sim_data_span;

extern const es2_sim_data_run es2_sim_dbsim_data_runs[];
extern const unsigned es2_sim_dbsim_data_run_count;
extern const es2_sim_data_span es2_sim_dbsim_data_spans[];
extern const unsigned es2_sim_dbsim_data_span_count;

/* Checks `runs`: each with data, a known kind, a size above 0 (a multiple of
 * 4 for the dword kinds, a NUL last for strings), inside the guest range
 * [guest_va, guest_va + guest_size), in ascending VA order and without
 * overlap. 0, or 1 and *error. */
int es2_sim_data_check(const es2_sim_data_run *runs, unsigned count, uint32_t guest_va, uint32_t guest_size,
                       const char **error);

/* Writes `runs` in table order into `guest` (the host buffer of the guest
 * range [guest_va, guest_va + guest_size)), after es2_sim_data_check
 * passed; on a refusal nothing is written. Bytes outside the runs are left
 * as they are (the caller passes zeroed memory). 0, or 1 and *error. */
int es2_sim_data_write(const es2_sim_data_run *runs, unsigned count, uint8_t *guest, uint32_t guest_va,
                       uint32_t guest_size, const char **error);

/* es2_sim_data_write with DBSIM's runs. */
int es2_sim_dbsim_data_write(uint8_t *guest, uint32_t guest_va, uint32_t guest_size, const char **error);

/* The dword of the RTTI_COUNT run at `va` in `runs` (binary search; the runs
 * are in ascending VA order). 0 with *value, or 1 when no four-byte RTTI_COUNT
 * run starts at `va`. */
int es2_sim_data_rtti_count(const es2_sim_data_run *runs, unsigned count, uint32_t va, uint32_t *value);

/* es2_sim_data_rtti_count over DBSIM's runs: the count the own models of
 * 0x496406 add (the type descriptor's dword +0x20 in CODE, which is
 * no-access in the guest). */
int es2_sim_dbsim_rtti_count(uint32_t va, uint32_t *value);

#endif
