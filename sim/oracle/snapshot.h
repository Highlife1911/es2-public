/* The after-load snapshot ES2SNAP1 version 2 (mission_load.md sections 9
 * and 13, "The after-load snapshot"; the proxy's writer is
 * tools/reference-game/proxy/early_arming.h, the reader and comparison
 * tools/reference-game/scripts/after_load_snapshot.py). Pure C17, no Win32:
 * the caller hands the guest bytes, the heap's region table and the memory
 * map; the writer lays out the same file the proxy writes (all words
 * little-endian uint32):
 *
 *   header  "ES2SNAP1", version 2, flags, frame, image base, image size,
 *           fixed count
 *   fixed   per region {va, length, readable} + length bytes when readable
 *   heap    region count, per region {base, size, status, block count} and
 *           per block {address, size, flags} + size - 8 bytes when flag 2
 *           (CW3220's block layout: marker word 1 at base, the first block
 *           at base + 8, header s|1 at p - 4, footer at p + s - 8, the end
 *           marker at base + size - 4); 0 regions when no heap is walked
 *   map     entry count, {base, size, allocation base, protect, type} per
 *           committed region, module count, {base, size of image, name
 *           length} + the name per module
 *
 * Flags: 1 heap walked, 2 heap unavailable, 4 heap walk error, 8 truncated
 * (max_bytes reached), 16 complete, 32 armed early, 64 memory map truncated,
 * 128 own runtime heap. Used by the own process (es2sim); the proxy keeps
 * its own copy until a live regression switches it. */
#ifndef ES2_SIM_ORACLE_SNAPSHOT_H
#define ES2_SIM_ORACLE_SNAPSHOT_H
#include <stddef.h>
#include <stdint.h>

#define ES2_SNAPSHOT_VERSION 2u
#define ES2_SNAPSHOT_IMAGE_SIZE 0x002d8000u
#define ES2_SNAPSHOT_REGION_LIMIT 64u
#define ES2_SNAPSHOT_FLAG_WALKED 1u
#define ES2_SNAPSHOT_FLAG_UNAVAILABLE 2u
#define ES2_SNAPSHOT_FLAG_WALK_ERROR 4u
#define ES2_SNAPSHOT_FLAG_TRUNCATED 8u
#define ES2_SNAPSHOT_FLAG_COMPLETE 16u
#define ES2_SNAPSHOT_FLAG_ARMED_EARLY 32u
#define ES2_SNAPSHOT_FLAG_MAP_TRUNCATED 64u
#define ES2_SNAPSHOT_FLAG_OWN_HEAP 128u

typedef struct es2_snapshot_region { uint32_t va, length; } es2_snapshot_region;
/* The fixed regions of mission_load.md sections 5 and 9 (reference VAs), in
 * the proxy's order; ES2_SNAPSHOT_FIXED_COUNT of them. */
extern const es2_snapshot_region es2_snapshot_fixed_regions[];
#define ES2_SNAPSHOT_FIXED_COUNT 13u
/* The -l arena (written after the extras when the threshold word
 * [0x6bb304] is nonzero). */
#define ES2_SNAPSHOT_ARENA_BASE 0x00604f04u
#define ES2_SNAPSHOT_ARENA_SIZE 0x000b0000u
#define ES2_SNAPSHOT_ARENA_THRESHOLD 0x006bb304u

typedef struct es2_snapshot_map_entry { uint32_t base, size, allocation_base, protect, type; } es2_snapshot_map_entry;
typedef struct es2_snapshot_module { uint32_t base, size; const char *name; } es2_snapshot_module;

typedef struct es2_snapshot_source {
    void *user;
    /* 1 when [va, va + n) is committed and readable. */
    int (*readable)(void *user, uint32_t va, uint32_t n);
    /* The n bytes at va (only called after readable() said yes). */
    void (*read)(void *user, uint32_t va, void *dst, uint32_t n);
} es2_snapshot_source;

typedef struct es2_snapshot_input {
    uint32_t frame;                    /* the dword at 0x4d24bc + 0xbf */
    uint32_t image_base, image_size;
    uint32_t flags;                    /* 32 and 128 as they apply; the writer adds the rest */
    const es2_snapshot_region *extra;
    unsigned extra_count;
    int arena;                         /* write the -l arena region after the extras */
    int heap_available;                /* 0: flag 2, no region */
    uint32_t heap_count;
    const uint32_t *heap_base, *heap_size;
    uint64_t max_bytes;                /* block contents beyond it: flag 8 */
    const es2_snapshot_map_entry *map;
    unsigned map_count;
    const es2_snapshot_module *modules;
    unsigned module_count;
    int map_truncated;
} es2_snapshot_input;

/* The file in a buffer the caller frees (free()). 0, or 1 when out of memory. */
int es2_snapshot_build(const es2_snapshot_source *source, const es2_snapshot_input *input, uint8_t **out, size_t *length);

/* "0xVA:LEN,0xVA:LEN" (hexadecimal VA, decimal or 0x LEN) as the proxy's
 * [snapshot] extra, at most `capacity` regions inside the image. 0, or 1
 * for a malformed list. */
int es2_snapshot_parse_extra(const char *text, es2_snapshot_region *out, unsigned capacity, unsigned *count);

#endif
