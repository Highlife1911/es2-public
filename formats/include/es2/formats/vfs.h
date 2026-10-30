#ifndef ES2_FORMATS_VFS_H
#define ES2_FORMATS_VFS_H
#include "es2/formats/vol.h"

/* A path is the concatenation of two borrowed spans, without NUL bytes.
 * Archives retain directory/name separately; loose files use an empty prefix. */
typedef struct es2_vfs_path { es2_span prefix, name; } es2_vfs_path;
typedef struct es2_vfs_file { const char *path; es2_span data; } es2_vfs_file;
typedef enum es2_vfs_source_kind { ES2_VFS_VOL, ES2_VFS_LOOSE } es2_vfs_source_kind;
typedef struct es2_vfs_source {
    es2_vfs_source_kind kind;
    union {
        const es2_vol *vol;
        struct { const es2_vfs_file *files; size_t count; } loose;
    } data;
} es2_vfs_source;
typedef enum es2_vfs_policy {
    ES2_VFS_CALLER_ORDER, ES2_VFS_DBSIM_ORDER
} es2_vfs_policy;
typedef struct es2_vfs {
    const es2_vfs_source *sources;
    size_t source_count, file_count;
    es2_vfs_policy policy;
    const size_t *source_order; /* Borrowed original source indices, or NULL. */
    size_t order_count;        /* Used only by ES2_VFS_DBSIM_ORDER. */
} es2_vfs;
typedef struct es2_vfs_copy {
    es2_vfs_path path;
    es2_span payload;
    size_t source_index, file_index;
    size_t next_copy; /* SIZE_MAX ends the same-path chain. */
} es2_vfs_copy;
typedef struct es2_vfs_entry {
    es2_vfs_path path; /* Spelling of the first (winning) copy. */
    size_t first_copy, last_copy, copy_count;
    int all_identical; /* Full payload-byte comparison, including lengths. */
} es2_vfs_entry;
typedef struct es2_vfs_stats {
    size_t unique_count, copy_count, duplicate_count, identical_duplicate_count;
} es2_vfs_stats;

/* Sources are ordered by caller policy: the first source containing a full
 * path wins. This is the default API policy, NOT confirmed game behavior.
 * No archive priority/flags are interpreted and loose files have no inherent
 * priority. Reorder the source array to implement a different precedence.
 *
 * Paths are printable ASCII relative paths with at least one separator and
 * nonempty components. Leading/trailing/repeated separators, ':' and '.'/'..'
 * components are rejected, not normalized. Only ASCII case and '/' vs '\\'
 * are folded. Duplicate folded paths within one loose source are rejected.
 * These input rules are API policy, not claims about the original loader.
 *
 * VOL descriptors must come from successful es2_vol_open calls. The source
 * array, loose paths/lists, archive descriptors and all bytes remain alive and
 * unchanged while the VFS is used. No allocation or I/O occurs. Empty source
 * lists and zero-byte loose files are allowed. Failure leaves out unchanged. */
es2_status es2_vfs_open(const es2_vfs_source *sources, size_t source_count,
                        es2_vfs *out, es2_error *error);
/* Original DBSIM registration/lookup order; see docs/formats/vol.md.
 * Input sources are in explicit enumeration/registration order. The caller
 * expands the two root/name patterns, suppresses duplicate archive filenames,
 * and omits missing/unopenable archives and loose files. No directory sorting
 * or file-system access occurs here. All descriptors must be valid even when
 * their archive mask is filtered out. archive_mask is normally 1 (DBSIM) or
 * 0x100 (VSHELL, whose insertion policy is identical).
 *
 * All loose sources precede archives, preserving their caller order. Archives
 * are selected by nonzero intersection with the LE mask at header bytes 4..7.
 * For each registration, a higher priority displaces a slot and carries the
 * displaced archive forward; equals are skipped. This is NOT stable sorting:
 * A5,B5,C10 becomes C10,B5,A5. At most 30 accepted archives are supported.
 *
 * source_order has capacity >= source_count and must not overlap inputs/out.
 * It remains alive and unchanged with all other borrowed storage. It may be
 * partially written on failure; out remains unchanged. Returned source_index
 * always refers to the ORIGINAL sources array, including after filtering.
 *
 * ASCII case is folded. Original archive lookup does not fold '/' to '\\':
 * slash queries may resolve loose entries but never archive members. Existing
 * relative-path validation and Windows-style loose path aliases are retained.
 * Enumeration uses canonical folded paths; its first_copy is the
 * winner for a canonical backslash query. This models a fresh manager only,
 * not refresh/removal, I/O errors, malformed archives or OS path traversal. */
es2_status es2_vfs_open_dbsim_order(const es2_vfs_source *sources,
    size_t source_count, uint32_t archive_mask,
    size_t *source_order, size_t order_capacity,
    es2_vfs *out, es2_error *error);
es2_status es2_vfs_find(const es2_vfs *vfs, const char *path,
                        es2_vfs_copy *out, es2_error *error);

/* Unique entries follow first-occurrence order; copies follow source order,
 * then archive member/list order. first_copy is the lookup winner. Allocate
 * both arrays to vfs.file_count for a sufficient upper bound. Smaller entry
 * capacity is allowed when it holds all unique paths. Copy capacity must hold
 * every file. Arrays may be partly filled on failure; stats is assigned only
 * on success. Arrays must not overlap each other or any borrowed input. */
es2_status es2_vfs_enumerate(const es2_vfs *vfs,
    es2_vfs_entry *entries, size_t entry_capacity,
    es2_vfs_copy *copies, size_t copy_capacity,
    es2_vfs_stats *stats, es2_error *error);

/* Enumerate indices of entries having multiple copies, in entry order.
 * entries must come from a successful enumeration. required is assigned the
 * exact duplicate count even on ES2_ERR_CAPACITY; NULL/zero output can query
 * that count. No indices are written on a capacity failure. */
es2_status es2_vfs_duplicate_indices(const es2_vfs_entry *entries,
    size_t entry_count, size_t *indices, size_t capacity,
    size_t *required, es2_error *error);

/* DBSIM's per-archive member lookup (0x47cdf8, split 0x47d198;
 * docs/reverse-engineering/file_io.md, section "the VOL-aware read stream").
 * path is a NUL-terminated byte string of fewer than ES2_VFS_DBSIM_PATH_MAX
 * bytes (longer paths overflow the original's static split buffers and are refused
 * with ES2_ERR_LENGTH). The directory part is the drive (the first two bytes
 * when byte 1 is ':') followed by everything up to and including the last
 * backslash after the drive; the file part is the rest. Only the backslash separates:
 * '/' is an ordinary byte. An empty directory part selects directory index
 * 0xff without a directory search; otherwise the first directory string of
 * the table that equals it is used, and none is ES2_ERR_NOT_FOUND. Then the
 * first member record in table order with that directory index whose name
 * (the slot up to its NUL) equals the file part wins. Equality is Borland
 * stricmp's: equal length and equal bytes after mapping 'a'..'z' to
 * 'A'..'Z' (no other byte is folded). On success *index is the member's
 * record index. No allocation or I/O; the VOL must come from es2_vol_open. */
#define ES2_VFS_DBSIM_PATH_MAX 256u
es2_status es2_vfs_dbsim_member(const es2_vol *vol, const char *path,
                                size_t *index, es2_error *error);
#endif
