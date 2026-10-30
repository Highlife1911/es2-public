/* The guest image (own_process.md section 2, "es2sim without DBSIM.EXE"): DBSIM's layout, import
 * slots and resources as own tables (sim/data/dbsim_image_tables.c), placed by
 * the host at the original image base with the original VAs (identity mapping).
 * Relocations are not applied. es2sim reads no DBSIM.EXE: the file
 * path (hash gate, PE parse, placement, the file's import list and resource
 * lookup) is es2/sim/image_file.h, testonly, for the tests that compare the own
 * tables with the file. */
#ifndef ES2_SIM_IMAGE_H
#define ES2_SIM_IMAGE_H
#include <stddef.h>
#include <stdint.h>

#define ES2_SIM_MAX_SECTIONS 16u
#define ES2_SIM_MAX_IMPORTS 512u
#define ES2_SIM_IMPORT_NAME 96u

/* The reference DBSIM.EXE's image (docs/reference_version.md). */
#define ES2_SIM_DBSIM_BASE 0x00400000u
#define ES2_SIM_DBSIM_IMAGE_SIZE 0x002d8000u

/* Section flags (IMAGE_SCN_*). */
#define ES2_SIM_SCN_EXECUTE 0x20000000u
#define ES2_SIM_SCN_WRITE 0x80000000u

typedef struct es2_sim_section {
    char name[9];
    uint32_t rva, virtual_size, raw_offset, raw_size, flags;
} es2_sim_section;

typedef struct es2_sim_image_info {
    uint32_t image_base, image_size, headers_size, entry_rva;
    unsigned section_count;
    es2_sim_section sections[ES2_SIM_MAX_SECTIONS];
    uint32_t import_rva, import_size, reloc_rva, reloc_size, resource_rva, resource_size;
    uint32_t code_lo, code_hi;       /* VA range [lo, hi) of the executable sections (one contiguous run required) */
} es2_sim_image_info;

typedef struct es2_sim_import {
    uint32_t slot_va;                /* the IAT slot */
    char name[ES2_SIM_IMPORT_NAME];  /* "dll!symbol" or "dll!#ordinal" */
} es2_sim_import;

/* Resource types (RT_*). */
#define ES2_SIM_RT_ICON 3u
#define ES2_SIM_RT_GROUP_ICON 14u

/* The layout, the imports and the resources as own tables (own_process.md,
 * "headers, imports and the icon as own tables"):
 * sim/data/dbsim_image_tables.c, generated from the reference DBSIM.EXE by
 * tools/reference-game/scripts/dbsim_image_tables.py and compared with the file
 * by //sim:image_tables_real_test. The host takes them from here only. */

/* One import slot: DLL name and the VA of that string, symbol name, hint
 * and VA of the hint/name entry (symbol NULL: by `ordinal`), IAT slot VA. */
typedef struct es2_sim_own_import {
    const char *dll;
    uint32_t dll_name_va;
    const char *symbol;
    uint16_t hint;
    uint32_t name_va;
    uint32_t ordinal;
    uint32_t slot_va;
} es2_sim_own_import;

/* One resource leaf: type (RT_*), name as stored (upper case; NULL: by
 * `id`), language, VA and size of the data, the bytes. */
typedef struct es2_sim_own_resource {
    uint32_t type;
    const char *name;
    uint32_t id, language, va, size;
    const uint8_t *bytes;
} es2_sim_own_resource;

extern const es2_sim_image_info es2_sim_dbsim_layout;
extern const es2_sim_own_import es2_sim_dbsim_imports[];
extern const unsigned es2_sim_dbsim_import_count;
extern const es2_sim_own_resource es2_sim_dbsim_resources[];
extern const unsigned es2_sim_dbsim_resource_count;

/* What es2_sim_image_parse (image_file.h) gives on the reference file, from
 * the own table. */
void es2_sim_image_info_own(es2_sim_image_info *info);

/* What es2_sim_image_imports gives on the placed reference image ("dll!symbol"
 * or "dll!#ordinal", the slot VA, in the file's order), from the own
 * table. 0, or 1 and *error. */
int es2_sim_imports_own(es2_sim_import *imports, unsigned capacity, unsigned *count, const char **error);

/* es2_sim_image_resource's lookup over the own resources: type by number,
 * name without regard to case or, with `name` NULL, `id`; of the languages
 * the neutral one, else the first. 0 with *bytes and *size, or 1 and *error
 * ("resource not found"). */
int es2_sim_resource_own(uint32_t type, const char *name, uint32_t id, const uint8_t **bytes, uint32_t *size,
                         const char **error);

#endif
