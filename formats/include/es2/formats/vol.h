#ifndef ES2_FORMATS_VOL_H
#define ES2_FORMATS_VOL_H
#include "es2/formats/common.h"

typedef struct es2_vol_directory { es2_span name; } es2_vol_directory;
typedef struct es2_vol_member {
    es2_span name;      /* No NUL included. Original spelling is preserved. */
    es2_span directory; /* Includes final backslash, excludes NUL. */
    uint8_t directory_index;
    uint8_t kind;
    uint32_t metadata;  /* Unknown block field, preserved without interpretation. */
    size_t block_offset, payload_offset, length;
    es2_span payload;
} es2_vol_member;
typedef struct es2_vol {
    es2_span input, directory_bytes, member_bytes;
    size_t directory_count, member_count, table_offset, blocks_offset;
    uint8_t flag0, flag1, priority;
    uint16_t reserved;
} es2_vol;

/* Fully validate VOLN. Unsorted member records are allowed. Kind 2 only.
 * Blocks must be nonempty, nonoverlapping, tightly cover the body and have
 * the documented repeated-byte suffix. No observed flag values are imposed.
 * Success assigns out; failure leaves out unchanged. All storage is borrowed. */
es2_status es2_vol_open(es2_span input, es2_vol *out, es2_error *error);
es2_status es2_vol_directory_at(const es2_vol *vol, size_t index,
                               es2_span *out, es2_error *error);
es2_status es2_vol_member_at(const es2_vol *vol, size_t index,
                            es2_vol_member *out, es2_error *error);
/* Optional materialized tables in caller-provided arrays, sized from open(). */
es2_status es2_vol_read_tables(const es2_vol *vol,
    es2_vol_directory *directories, size_t directory_capacity,
    es2_vol_member *members, size_t member_capacity, es2_error *error);
/* ASCII case-insensitive full path; '/' and '\\' are equivalent by API policy.
 * This policy does not claim original VFS search/patch precedence behavior. */
es2_status es2_vol_find(const es2_vol *vol, const char *path,
                       es2_vol_member *out, es2_error *error);
es2_status es2_vol_payload(const es2_vol *vol, size_t index,
                          es2_span *out, es2_error *error);
#endif
