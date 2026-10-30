#ifndef ES2_CORE_ASSET_LOAD_H
#define ES2_CORE_ASSET_LOAD_H
#include <stdint.h>
#include "es2/core/vfs_stream.h"
/* DBSIM's persistent-object loader and the bitmap classes as own code
 * (docs/reverse-engineering/asset_loading.md):
 *
 *   dts_load 47a144 (S, arg)            the generic loader: header 47a6b4,
 *                                       registry lookup 47a4a0, factory of
 *                                       the registry entry, drain 47a83c
 *   bitmap_new 481cd4 (S, arg)          GLBitmap factory (0x1a bytes)
 *   bitmap_read 481d8c (B, S, arg)      GLBitmap read (vtable 4a306c +0Ch)
 *   bitmap_array_new 482d88 (S, arg)    GLBitmapArray factory (0x0c bytes)
 *   bitmap_array_read 482e7c (A, S, arg) GLBitmapArray read (4a32ec +0Ch)
 *   stream_destroy 472f64 (S, flag)     destructor of the VOL-aware stream
 *
 * Stream operations on a member stream of an identified archive (vtable
 * 4a0e10, no handle) are served from the archive file through the VOL-aware
 * stream model (es2_vfs_stream_tell, es2_vfs_stream_fetch); any other
 * stream (or a state that model does not cover) gets the original's virtual
 * call (+4 tell, +18h read) through the guest bridge in replaced mode and SKIP
 * in compare mode. The factories' classes are identified by their registry
 * entry; other classes keep the original factory (a guest call).
 *
 * The memory stream class (vtable 4a1788, constructor 47b36c(S,
 * size, buffer)) is served as own code in replaced mode: tell 47b400 and
 * read 47b4b4 over its fields (+4 status, +8 start, +0Ch end, +10h cursor).
 * DBSIM's weapon shape cache (40f998 reads the roots of mechwpns.dts into
 * heap blocks, 40fab0 loads one per weapon mount through 474c10) is its only
 * user that reaches dts_load. Compare mode skips memory streams. */

/* Observer keys (h->vfs.observe) of dts_load's PASS outcomes: exactly one
 * per PASS in replaced mode, none in compare mode (the adapter exports them
 * as hk_dts_load_pass_<reason>). */
#define ES2_ASSET_PASS_STREAM 1u    /* a stream class neither VOL-aware nor memory */
#define ES2_ASSET_PASS_LOOSE 2u     /* a VOL-aware stream with a file handle (loose file) */
#define ES2_ASSET_PASS_UNSERVED 3u  /* a stream state the models do not serve (VOL member read, memory cursor past the end) */
#define ES2_ASSET_PASS_TAG 4u       /* a header tag the registry lacks, the stream still readable */
#define ES2_ASSET_PASS_FALLBACK 5u  /* a header tag the registry lacks, the fallback registry [4a1694] in use */

enum {
    /* The memory stream: status, start, end, cursor. */
    ES2_ASSET_MEM_STATUS = 0x04, ES2_ASSET_MEM_START = 0x08, ES2_ASSET_MEM_END = 0x0c, ES2_ASSET_MEM_CURSOR = 0x10,
    ES2_ASSET_MEM_SIZE = 0x14,
    ES2_ASSET_BITMAP_SIZE = 0x1a, ES2_ASSET_ARRAY_SIZE = 0x0c, ES2_ASSET_FRAME_SIZE = 0x1c,
    /* Guest scratch a host provides per call: the loader frames of the
     * nested compare models (0x20 each, ES2_ASSET_DEPTH of them), a copy of
     * the stream for the header peek, the one-byte locals and a scratch
     * object for the factories' compare models. */
    ES2_ASSET_DEPTH = 4,
    ES2_ASSET_PEEK = 0x80, ES2_ASSET_LOCAL = 0xb0, ES2_ASSET_OBJECT = 0xc0,
    ES2_ASSET_SCRATCH = 0x140,
    ES2_ASSET_EXPECT_MAX = 1024
};

/* Compare mode: what the original's load must have built, in load order
 * (pre-order: an array is followed by its children). */
/* NONE: a load that built nothing (the pointer must be 0). */
typedef enum es2_asset_kind { ES2_ASSET_BITMAP = 1, ES2_ASSET_ARRAY = 2, ES2_ASSET_NONE = 3 } es2_asset_kind;
typedef struct es2_asset_object {
    es2_asset_kind kind;
    uint32_t end;                      /* index after this object's subtree */
    /* bitmap: the values the read stores */
    uint16_t height, width, compression, colours;
    uint8_t depth, flags;              /* flags: the low five bits of +9 */
    uint32_t pixel_size;
    const uint8_t *pixels, *colour_bytes;  /* source bytes in the archive file */
    /* array */
    uint32_t count;
} es2_asset_object;
typedef struct es2_asset_expect {
    es2_asset_object objects[ES2_ASSET_EXPECT_MAX];
    unsigned count;
} es2_asset_expect;

typedef struct es2_asset_host {
    /* memory, image, commit, context, archive, call (guest calls), loose,
     * observe: as for the VOL stream functions (es2_vfs_host). */
    es2_vfs_host vfs;
    uint32_t scratch;                  /* ES2_ASSET_SCRATCH guest bytes */
    es2_asset_expect *expect;          /* compare mode: filled, else NULL */
} es2_asset_host;

typedef struct es2_asset_result { uint32_t eax, edx; } es2_asset_result;

/* Original: DBSIM.EXE 0x47a144. PASS (replaced) / SKIP (compare) before any
 * write: a stream class other than the VOL-aware and the memory stream, a
 * VOL-aware stream with a handle, a state the models do not serve, and a
 * header whose tag the registry lacks while the stream stays readable (the
 * original's drain then reads an uninitialized frame dword) or while the
 * fallback registry [4a1694] is in use; replaced mode reports the reason to
 * the observer (ES2_ASSET_PASS_*). Compare: a memory stream is SKIP; the
 * factory must be bitmap_new or bitmap_array_new (else SKIP); the expected
 * objects go to h->expect. */
es2_vfs_outcome es2_asset_dts_load(const es2_asset_host *h, uint32_t stream, uint32_t arg, es2_asset_result *out);
/* Original: DBSIM.EXE 0x481cd4 / 0x482d88. fs0 = FS:[0] at entry (the
 * exception record's saved head, EDX at exit). */
es2_vfs_outcome es2_asset_bitmap_new(const es2_asset_host *h, uint32_t stream, uint32_t arg, uint32_t fs0,
                                     es2_asset_result *out);
es2_vfs_outcome es2_asset_bitmap_array_new(const es2_asset_host *h, uint32_t stream, uint32_t arg, uint32_t fs0,
                                           es2_asset_result *out);
/* Original: DBSIM.EXE 0x481d8c. entry_ecx: its top byte is the local the
 * original pushes (push ecx) and keeps when a byte read delivers nothing.
 * Compare: the fields go to `bitmap` (the clone); the pointers +0Eh/+16h
 * stay 0 and h->expect->objects[0] holds the bytes they must point to. */
es2_vfs_outcome es2_asset_bitmap_read(const es2_asset_host *h, uint32_t bitmap, uint32_t stream, uint32_t entry_ecx,
                                      es2_asset_result *out);
/* Original: DBSIM.EXE 0x482e7c. Compare: the count goes to the clone, +8
 * stays as it was and h->expect describes the children. */
es2_vfs_outcome es2_asset_bitmap_array_read(const es2_asset_host *h, uint32_t array, uint32_t stream,
                                            es2_asset_result *out);
/* Original: DBSIM.EXE 0x472f64. Compare: SKIP with a decoder object, the
 * delete flag or an owned handle; the construction counter is written
 * (the caller declares it as a region). */
es2_vfs_outcome es2_asset_stream_destroy(const es2_asset_host *h, uint32_t stream, uint32_t flag, uint32_t fs0,
                                         es2_asset_result *out);
#endif
