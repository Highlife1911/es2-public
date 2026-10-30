/* The part codec of vfs.wasm (docs/web_build.md section 8.2): one zstd frame decoded into a
 * buffer of the part's length, then its SHA-256 compared with the manifest's. web/vfs.mjs
 * keeps the manifest, the overlay and the sparse files; this file only decodes and checks. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "es2/formats/sha256.h"
#include "zstd.h"

enum {
    ES2V_OK = 0,
    ES2V_NOT_A_FRAME = -1,
    ES2V_WRONG_SIZE = -2,
    ES2V_CORRUPT = -3,
    ES2V_WRONG_HASH = -4,
};

/* Decodes exactly one frame of `dst_size` bytes and checks it against the 64 hex digits of
 * `sha256_hex`. On any failure the destination is cleared, so no unchecked byte survives. */
int es2v_part_decode(const void *src, size_t src_size, void *dst, size_t dst_size, const char *sha256_hex)
{
    unsigned long long declared = ZSTD_getFrameContentSize(src, src_size);
    if (declared == ZSTD_CONTENTSIZE_ERROR || declared == ZSTD_CONTENTSIZE_UNKNOWN) return ES2V_NOT_A_FRAME;
    if (declared != dst_size) return ES2V_WRONG_SIZE;
    size_t frame = ZSTD_findFrameCompressedSize(src, src_size);
    int status = ES2V_OK;
    if (ZSTD_isError(frame) || frame != src_size) {
        status = ES2V_CORRUPT;
    } else {
        size_t got = ZSTD_decompress(dst, dst_size, src, src_size);
        if (ZSTD_isError(got) || got != dst_size) status = ES2V_CORRUPT;
    }
    if (status == ES2V_OK) {
        char hex[65];
        es2f_sha256_of(dst, dst_size, hex);
        if (memcmp(hex, sha256_hex, 64) != 0) status = ES2V_WRONG_HASH;
    }
    if (status != ES2V_OK && dst_size) memset(dst, 0, dst_size);
    return status;
}

/* SHA-256 of a buffer as 64 lowercase hex digits and a NUL, for overlay entries. */
void es2v_sha256(const void *data, size_t size, char hex[65])
{
    es2f_sha256_of(data, size, hex);
}
