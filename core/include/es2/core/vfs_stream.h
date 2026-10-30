#ifndef ES2_CORE_VFS_STREAM_H
#define ES2_CORE_VFS_STREAM_H
#include <stdint.h>
#include "es2/core/guest.h"
#include "es2/formats/vol.h"
/* DBSIM's VOL-aware read stream as own code
 * (docs/reverse-engineering/file_io.md, section "the VOL-aware read
 * stream as own code"). Eight functions of the stream library:
 *
 *   close 47307c (S)            open 4730e8 (S, path)
 *   read 4735b8 (S, size, buf)  seek 4733a4 (S, pos)
 *   tell 473368 (S)             size 473334 (S)
 *   lookup 468064 (M, path, R)  member_lookup 47cdf8 (A, path, R)
 *
 * S is the 0x2f-byte stream object (vtable 4a0e10), M the archive manager
 * ([4a0d18]), A an archive object of a manager slot, R the 0x1c-byte
 * member record inside S (S+0Ah). Member bytes come from the archive file
 * parsed by formats/ (es2_vol, es2_vfs_dbsim_member), never from the
 * archive's own buffered stream B = [A+4]; the loose-file path keeps the
 * runtime's file calls (_open/_read/_lseek/_filelength/_close), and the
 * decompressor kinds 7 and 9 (none in the CD corpus) keep the original's
 * decoder calls. */

enum {
    ES2_VFS_S_STATUS = 0x04, ES2_VFS_S_HANDLE = 0x08, ES2_VFS_S_ARCHIVE = 0x0a, ES2_VFS_S_SOURCE = 0x0e,
    ES2_VFS_S_START = 0x12, ES2_VFS_S_POS = 0x16, ES2_VFS_S_APOS = 0x1a, ES2_VFS_S_KIND = 0x1e,
    ES2_VFS_S_LENGTH = 0x22, ES2_VFS_S_OWNED = 0x26, ES2_VFS_S_DECODER7 = 0x27, ES2_VFS_S_DECODER9 = 0x2b,
    ES2_VFS_STREAM_SIZE = 0x2f,
    /* The archive's buffered stream B (vtable 4a1ad8): status, file base of
     * the buffer, valid bytes, cursor. tell = base + cursor (status 6: 0). */
    ES2_VFS_B_STATUS = 0x04, ES2_VFS_B_BASE = 0x10, ES2_VFS_B_VALID = 0x19, ES2_VFS_B_CURSOR = 0x1d,
    ES2_VFS_BUFFERED_SIZE = 0x21,
    ES2_VFS_RECORD_SIZE = 0x1c,
    ES2_VFS_ARCHIVE_SIZE = 0x30
};

/* An archive the host identified: the original's archive object, its
 * buffered stream [A+4] and the parsed file (vol->input = the whole file).
 * es2_vfs_archive_matches must have accepted the pair. */
typedef struct es2_vfs_archive {
    uint32_t archive, buffered;
    const es2_vol *vol;
} es2_vfs_archive;

typedef struct es2_vfs_host {
    const es2_guest_memory *m;
    uint32_t image;            /* image base (0x400000 in the game) */
    /* 1: replaced mode (writes, runtime and nested-hook calls); 0: the
     * replacement phase of compare mode (pure: the clone of S/R only). */
    int commit;
    void *context;
    /* The identified archive for an archive object (buffered = 0) or for a
     * buffered stream (archive = 0); NULL when the host has none. */
    const es2_vfs_archive *(*archive)(void *context, uint32_t archive, uint32_t buffered);
    /* commit only: cdecl guest call; registers = entry EAX/EDX/ECX in,
     * exit EAX/EDX/ECX out. */
    void (*call)(void *context, uint32_t function, const uint32_t *args, unsigned count, uint32_t registers[3]);
    /* compare only: nonzero when the runtime's _open(path, 8000h) would
     * open the loose file. */
    int (*loose)(void *context, uint32_t path);
    /* optional: each original call the function makes (the VA), guest or
     * modeled, in order. */
    void (*observe)(void *context, uint32_t function);
} es2_vfs_host;

typedef enum es2_vfs_outcome {
    ES2_VFS_DONE = 0,  /* result in eax, all effects done */
    ES2_VFS_PASS = 1,  /* commit: not modeled, nothing written; run the original */
    ES2_VFS_SKIP = 2   /* compare: needs a runtime/decoder call; nothing compared */
} es2_vfs_outcome;

typedef struct es2_vfs_result {
    uint32_t eax;
    uint32_t differences;      /* compare-mode read: bytes unequal to the guest buffer */
} es2_vfs_result;

/* The archive object A matches the parsed file: directory and member
 * counts, priority, every member record (18 bytes) and every directory
 * string equal to the file's tables, [A+0x24]/[A+0x2c] non-null. */
int es2_vfs_stream_archive_matches(const es2_guest_memory *m, uint32_t archive, const es2_vol *vol);

es2_vfs_outcome es2_vfs_stream_close(const es2_vfs_host *h, uint32_t stream, es2_vfs_result *out);
es2_vfs_outcome es2_vfs_stream_open(const es2_vfs_host *h, uint32_t stream, uint32_t path, es2_vfs_result *out);
es2_vfs_outcome es2_vfs_stream_read(const es2_vfs_host *h, uint32_t stream, uint32_t size, uint32_t buffer,
    uint32_t entry_eax, es2_vfs_result *out);
es2_vfs_outcome es2_vfs_stream_seek(const es2_vfs_host *h, uint32_t stream, uint32_t position, uint32_t entry_eax,
    es2_vfs_result *out);
es2_vfs_outcome es2_vfs_stream_tell(const es2_vfs_host *h, uint32_t stream, es2_vfs_result *out);
es2_vfs_outcome es2_vfs_stream_size(const es2_vfs_host *h, uint32_t stream, es2_vfs_result *out);
es2_vfs_outcome es2_vfs_stream_lookup(const es2_vfs_host *h, uint32_t manager, uint32_t path, uint32_t record,
    es2_vfs_result *out);
es2_vfs_outcome es2_vfs_stream_member_lookup(const es2_vfs_host *h, uint32_t archive, uint32_t path, uint32_t record,
    es2_vfs_result *out);
/* (docs/reverse-engineering/asset_loading.md): the read 4735b8 of a
 * member stream without a runtime call. The bytes go to `buffer` when it is
 * nonzero (in both modes: a caller in compare mode passes its own clone or
 * scratch), *from and *count are their source in the archive file (count 0
 * when nothing is delivered); S is updated as by the read, B gets its empty
 * window when commit or `move` is set (move: the caller's compare declares
 * B as a region). EAX as the read. A stream with a handle, an unidentified
 * archive or an unmodeled buffered-stream state: PASS/SKIP, nothing
 * written. */
es2_vfs_outcome es2_vfs_stream_fetch(const es2_vfs_host *h, uint32_t stream, uint32_t size, uint32_t buffer,
    uint32_t entry_eax, int move, const uint8_t **from, uint32_t *count, es2_vfs_result *out);
#endif
