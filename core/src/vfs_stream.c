#include "es2/core/vfs_stream.h"
#include "es2/formats/vfs.h"
#include "fixed.h"
#include <string.h>

/* The VOL-aware read stream of DBSIM (docs/reverse-engineering/ file_io.md,
 * section "the VOL-aware read stream as own code"). Every field is read again
 * where the original reads it. The functions decide ES2_VFS_PASS / ES2_VFS_SKIP
 * before their first write. */
#define R32(a) h->m->read32(h->m->context,(a))
#define R16(a) h->m->read16(h->m->context,(a))
#define W32(a,v) h->m->write32(h->m->context,(a),(v))
#define W16(a,v) h->m->write16(h->m->context,(a),(uint16_t)(v))
#define R8(a) (*(const uint8_t *)h->m->host(h->m->context,(a)))
#define W8(a,v) (*(uint8_t *)h->m->host(h->m->context,(a))=(uint8_t)(v))
#define VA(va) (h->image+(uint32_t)(va)-0x400000u)

enum {
    VA_RTL_OPEN = 0x4967c6, VA_RTL_READ = 0x496808, VA_RTL_LSEEK = 0x496826, VA_RTL_FILELENGTH = 0x496856,
    VA_RTL_CLOSE = 0x496874, VA_CLOSE = 0x47307c, VA_LOOKUP = 0x468064, VA_MEMBER = 0x47cdf8,
    VA_SEEK = 0x4733a4, VA_NEW = 0x47749c, VA_DECODER7_CTOR = 0x47b50c, VA_DECODER7_INIT = 0x47b594,
    VA_DECODER7_RESET = 0x47b5cc, VA_DECODER9_CTOR = 0x47b840, VA_DECODER9_INIT = 0x47b974,
    VA_DECODER9_RESET = 0x47b9cc, VA_MANAGER = 0x4a0d18, VA_COUNTER = 0x6cbee4, VA_STREAM_VTABLE = 0x4a0e10,
    /* the buffered stream's own calls, reported for the modeled paths */
    VA_BUFFERED_SEEK = 0x47c7d8, VA_BUFFERED_READ = 0x47c82c, VA_BUFFERED_TELL = 0x47c7bc
};

static uint32_t sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
static void observe(const es2_vfs_host *h, uint32_t va) { if (h->observe) h->observe(h->context, va); }
/* A guest call with zero entry registers; returns the exit EAX. */
static uint32_t call(const es2_vfs_host *h, uint32_t va, const uint32_t *args, unsigned count) {
    uint32_t r[3] = {0, 0, 0};
    observe(h, va);
    h->call(h->context, VA(va), args, count, r);
    return r[0];
}
static es2_vfs_outcome unmodeled(const es2_vfs_host *h) { return h->commit ? ES2_VFS_PASS : ES2_VFS_SKIP; }
static uint32_t al1(uint32_t eax) { return (eax & 0xffffff00u) | 1u; }
/* The buffered stream after one of its seeks or reads that ends at `at`:
 * an empty window at `at` (base, valid 0, cursor 0, status 0). Its tell is
 * `at` and its next seek or read refills from the file, so the original's
 * later calls on it behave as after the original's own operation. */
static void buffered_at(const es2_vfs_host *h, uint32_t b, uint32_t at) {
    if (!h->commit) return;
    W32(b + ES2_VFS_B_STATUS, 0);
    W32(b + ES2_VFS_B_BASE, at);
    W32(b + ES2_VFS_B_VALID, 0);
    W32(b + ES2_VFS_B_CURSOR, 0);
}

int es2_vfs_stream_archive_matches(const es2_guest_memory *m, uint32_t a, const es2_vol *vol) {
    if (!m || !a || !vol) return 0;
    const uint8_t *object = m->host(m->context, a);
    if (!object || object[0x1c] != vol->priority || object[0x1d] != vol->directory_count ||
        fx_r16(object + 0x1e) != vol->member_count || !fx_r32(object + 4)) return 0;
    uint32_t records = fx_r32(object + 0x2c), dirs = fx_r32(object + 0x24);
    if (vol->member_count) {
        const uint8_t *r = records ? m->host(m->context, records) : NULL;
        if (!r || memcmp(r, vol->member_bytes.data, 18u * vol->member_count)) return 0;
    }
    size_t at = 0;
    for (size_t i = 0; i < vol->directory_count; ++i) {
        const uint8_t *s = dirs ? m->host(m->context, m->read32(m->context, dirs + 4u * (uint32_t)i)) : NULL;
        if (!s) return 0;
        size_t k = 0;
        do {
            if (at + k >= vol->directory_bytes.size || s[k] != vol->directory_bytes.data[at + k]) return 0;
        } while (s[k++] != 0);
        at += k;
    }
    return 1;
}

/* Original: DBSIM.EXE 0x47307c */
es2_vfs_outcome es2_vfs_stream_close(const es2_vfs_host *h, uint32_t s, es2_vfs_result *out) {
    const int handle = R8(s + ES2_VFS_S_OWNED) && R16(s + ES2_VFS_S_HANDLE) != 0xffffu;
    if (!h->commit && (handle || R32(s + ES2_VFS_S_DECODER7) || R32(s + ES2_VFS_S_DECODER9))) return ES2_VFS_SKIP;
    if (handle) {
        const uint32_t a[1] = {sx16(R16(s + ES2_VFS_S_HANDLE))};
        if (call(h, VA_RTL_CLOSE, a, 1) == UINT32_MAX) {     /* 47309e */
            W32(s + ES2_VFS_S_STATUS, 3);
            out->eax = 0;
            return ES2_VFS_DONE;
        }
    }
    uint32_t d = R32(s + ES2_VFS_S_DECODER7);
    if (d) { const uint32_t a[1] = {d}; call(h, VA_DECODER7_RESET, a, 1); }
    d = R32(s + ES2_VFS_S_DECODER9);
    if (d) { const uint32_t a[1] = {d}; call(h, VA_DECODER9_RESET, a, 1); }
    W32(s + ES2_VFS_S_ARCHIVE, 0);
    W32(s + ES2_VFS_S_SOURCE, 0);
    W32(s + ES2_VFS_S_POS, 0);
    W16(s + ES2_VFS_S_HANDLE, 0xffffu);
    W32(s + ES2_VFS_S_STATUS, 5);
    out->eax = al1(s + ES2_VFS_S_ARCHIVE);                 /* lea eax,[ebx+0Ah]; mov al,1 */
    return ES2_VFS_DONE;
}

/* Kind 7 or 9: the decoder object at S+27h / S+2Bh, allocated once. */
static uint32_t open_decoder(const es2_vfs_host *h, uint32_t s, unsigned slot, uint32_t size, uint32_t ctor,
                             uint32_t init) {
    if (!R32(s + slot)) {
        const uint32_t n[1] = {size};
        const uint32_t p = call(h, VA_NEW, n, 1);
        if (p) {
            const uint32_t a[1] = {p};
            call(h, ctor, a, 1);
            const uint32_t c = R32(VA(VA_COUNTER));
            W32(c, R32(c) - 2u);
        }
        W32(s + slot, p);
    }
    W32(s + ES2_VFS_S_SOURCE, R32(s + slot));
    const uint32_t a[2] = {R32(s + slot), R32(s + ES2_VFS_S_ARCHIVE)};
    return call(h, init, a, 2);
}

/* Original: DBSIM.EXE 0x4730e8 */
es2_vfs_outcome es2_vfs_stream_open(const es2_vfs_host *h, uint32_t s, uint32_t path, es2_vfs_result *out) {
    es2_vfs_result inner = {0, 0};
    uint32_t eax;
    if (h->commit) {
        const uint32_t a[1] = {s};
        call(h, VA_CLOSE, a, 1);
    } else {
        /* Compare: the loose open is decided first, so that nothing below
         * depends on a runtime call. */
        if (h->loose(h->context, path)) return ES2_VFS_SKIP;
        es2_vfs_outcome o = es2_vfs_stream_close(h, s, &inner);
        if (o != ES2_VFS_DONE) return o;
    }
    W32(s + ES2_VFS_S_ARCHIVE, 0);
    W32(s + ES2_VFS_S_SOURCE, 0);
    W32(s + ES2_VFS_S_POS, 0);
    if (h->commit) {
        const uint32_t a[2] = {path, 0x8000u};
        eax = call(h, VA_RTL_OPEN, a, 2);
    } else {
        eax = UINT32_MAX;
    }
    W16(s + ES2_VFS_S_HANDLE, eax);
    if ((uint16_t)eax == 0xffffu) {                         /* 473128: loose open failed */
        const uint32_t manager = R32(VA(VA_MANAGER));
        if (!manager) goto failed;
        if (h->commit) {
            const uint32_t a[3] = {manager, path, s + ES2_VFS_S_ARCHIVE};
            eax = call(h, VA_LOOKUP, a, 3);
        } else {
            es2_vfs_outcome o = es2_vfs_stream_lookup(h, manager, path, s + ES2_VFS_S_ARCHIVE, &inner);
            if (o != ES2_VFS_DONE) return o;
            eax = inner.eax;
        }
        if (!(uint8_t)eax) goto failed;
        const uint32_t kind = R32(s + ES2_VFS_S_KIND);
        if (kind == 2) {
            W32(s + ES2_VFS_S_SOURCE, R32(s + ES2_VFS_S_ARCHIVE));
            eax = 0;
        } else if (kind == 7 || kind == 9) {
            if (!h->commit) return ES2_VFS_SKIP;
            eax = kind == 7 ? open_decoder(h, s, ES2_VFS_S_DECODER7, 0x92u, VA_DECODER7_CTOR, VA_DECODER7_INIT)
                            : open_decoder(h, s, ES2_VFS_S_DECODER9, 0x28u, VA_DECODER9_CTOR, VA_DECODER9_INIT);
        } else {
            eax = kind - 9u;                                /* the switch's SUBs leave kind-9 */
        }
    }
    W32(s + ES2_VFS_S_STATUS, 0);                           /* 473245 */
    W8(s + ES2_VFS_S_OWNED, 1);
    out->eax = al1(eax);
    return ES2_VFS_DONE;
failed:                                                     /* 47314d */
    W32(s + ES2_VFS_S_ARCHIVE, 0);
    W32(s + ES2_VFS_S_SOURCE, 0);
    W32(s + ES2_VFS_S_POS, 0);
    W32(s + ES2_VFS_S_STATUS, 1);
    out->eax = 0;
    return ES2_VFS_DONE;
}

/* Original: DBSIM.EXE 0x468064 */
es2_vfs_outcome es2_vfs_stream_lookup(const es2_vfs_host *h, uint32_t manager, uint32_t path, uint32_t record,
                               es2_vfs_result *out) {
    /* EBX counts, compared as a byte with the count at M+78h. */
    for (uint32_t i = 0; (uint8_t)i < R8(manager + 0x78u); ++i) {
        const uint32_t archive = R32(manager + 4u * i);
        uint32_t eax;
        if (h->commit) {
            const uint32_t a[3] = {archive, path, record};
            eax = call(h, VA_MEMBER, a, 3);
        } else {
            es2_vfs_result inner = {0, 0};
            es2_vfs_outcome o = es2_vfs_stream_member_lookup(h, archive, path, record, &inner);
            if (o != ES2_VFS_DONE) return o;
            eax = inner.eax;
        }
        if ((uint8_t)eax) { out->eax = al1(eax); return ES2_VFS_DONE; }
    }
    out->eax = 0;
    return ES2_VFS_DONE;
}

/* Original: DBSIM.EXE 0x47cdf8 (split 0x47d198 and rebuild 0x47d2c4 through
 * es2_vfs_dbsim_member; the original's static split buffers 6c5aa8..6c5eb3
 * are scratch that only these two functions and the registration read
 * after writing, and are not written here). */
es2_vfs_outcome es2_vfs_stream_member_lookup(const es2_vfs_host *h, uint32_t a, uint32_t path, uint32_t record,
                                      es2_vfs_result *out) {
    const es2_vfs_archive *arc = a ? h->archive(h->context, a, 0) : NULL;
    if (!arc) return unmodeled(h);
    /* A null path splits into empty parts (47d1d1): directory index 0xff
     * and an empty name, which no record of a parsed archive carries. */
    if (!path) { out->eax = 0; return ES2_VFS_DONE; }
    char text[ES2_VFS_DBSIM_PATH_MAX];
    const uint8_t *p = h->m->host(h->m->context, path);
    size_t length = 0;
    while (length < sizeof(text) && p[length]) { text[length] = (char)p[length]; ++length; }
    if (length >= sizeof(text)) return unmodeled(h);
    text[length] = 0;
    size_t index = 0;
    es2_status status = es2_vfs_dbsim_member(arc->vol, text, &index, NULL);
    if (status == ES2_ERR_NOT_FOUND) { out->eax = 0; return ES2_VFS_DONE; }
    if (status != ES2_OK) return unmodeled(h);
    const uint32_t b = R32(a + 4u);
    const uint32_t bstatus = R32(b + ES2_VFS_B_STATUS);
    const uint8_t *slot = arc->vol->member_bytes.data + 18u * index;
    const uint32_t block = fx_r32(slot + 14);
    const size_t size = arc->vol->input.size;
    if ((bstatus != 0 && bstatus != 2) || block >= size || size - block < 9u) return unmodeled(h);
    const uint8_t *prefix = arc->vol->input.data + block;
    if (h->ensure) {
        /* The whole member before any of its bytes is read: prefix, payload
         * and the suffix byte the read at the end delivers. */
        h->ensure(h->context, arc, block, 9u);
        const uint32_t length = fx_r32(prefix + 1);
        const size_t rest = size - block - 9u;
        if (rest) h->ensure(h->context, arc, block + 9u, length < rest ? (size_t)length + 1u : rest);
    }
    W32(record + 8u, block + 9u);                           /* 47cee8: B+0Dh-4 */
    W32(record + 0x10u, block + 9u);
    observe(h, VA_BUFFERED_SEEK);                           /* B.seek(block), B.read(9) */
    observe(h, VA_BUFFERED_READ);
    buffered_at(h, b, block + 9u);
    W32(record + 0x14u, prefix[0]);
    W32(record + 0x18u, fx_r32(prefix + 1));
    W32(record, b);
    out->eax = al1(fx_r32(prefix + 1));                     /* EAX = the length dword, AL = 1 */
    return ES2_VFS_DONE;
}

/* Original: DBSIM.EXE 0x473368 */
es2_vfs_outcome es2_vfs_stream_tell(const es2_vfs_host *h, uint32_t s, es2_vfs_result *out) {
    const uint32_t handle = R16(s + ES2_VFS_S_HANDLE);
    if (handle != 0xffffu) {
        if (!h->commit) return ES2_VFS_SKIP;
        const uint32_t a[3] = {sx16(handle), 0, 1};
        out->eax = call(h, VA_RTL_LSEEK, a, 3);
        return ES2_VFS_DONE;
    }
    out->eax = R32(s + ES2_VFS_S_SOURCE) ? R32(s + ES2_VFS_S_POS) : 0;
    return ES2_VFS_DONE;
}

/* Original: DBSIM.EXE 0x473334 */
es2_vfs_outcome es2_vfs_stream_size(const es2_vfs_host *h, uint32_t s, es2_vfs_result *out) {
    const uint32_t handle = R16(s + ES2_VFS_S_HANDLE);
    if (handle != 0xffffu) {
        if (!h->commit) return ES2_VFS_SKIP;
        const uint32_t a[1] = {sx16(handle)};
        out->eax = call(h, VA_RTL_FILELENGTH, a, 1);
        return ES2_VFS_DONE;
    }
    out->eax = R32(s + ES2_VFS_S_SOURCE) ? R32(s + ES2_VFS_S_LENGTH) : 0;
    return ES2_VFS_DONE;
}

/* The buffered stream B of a member stream when the host identified its
 * archive and B can seek: status 0 or 2 (a seek resets 2). */
static const es2_vfs_archive *member_archive(const es2_vfs_host *h, uint32_t s, uint32_t *b) {
    *b = R32(s + ES2_VFS_S_ARCHIVE);
    if (!*b) return NULL;
    const es2_vfs_archive *arc = h->archive(h->context, 0, *b);
    const uint32_t bstatus = arc ? R32(*b + ES2_VFS_B_STATUS) : 1u;
    return bstatus == 0 || bstatus == 2 ? arc : NULL;
}

/* Original: DBSIM.EXE 0x4733a4 */
es2_vfs_outcome es2_vfs_stream_seek(const es2_vfs_host *h, uint32_t s, uint32_t position, uint32_t entry_eax,
                             es2_vfs_result *out) {
    uint32_t status = R32(s + ES2_VFS_S_STATUS), eax = entry_eax;
    const int reset = status == 2;
    if (reset) { status = 0; eax = 0; }                     /* 4733b5: xor eax,eax */
    if (status) { out->eax = 0; return ES2_VFS_DONE; }
    const uint32_t handle = R16(s + ES2_VFS_S_HANDLE);
    eax = (eax & 0xffff0000u) | handle;                     /* mov ax,[ebx+8] */
    if (handle != 0xffffu) {
        if (!h->commit) return ES2_VFS_SKIP;
        if (reset) W32(s + ES2_VFS_S_STATUS, 0);
        const uint32_t a[3] = {sx16(handle), position, 0};
        const uint32_t r = call(h, VA_RTL_LSEEK, a, 3);
        if (r == UINT32_MAX) { W32(s + ES2_VFS_S_STATUS, 1); out->eax = 0; }   /* 473580: always 1 */
        else out->eax = al1(r + 1u);
        return ES2_VFS_DONE;
    }
    if (!R32(s + ES2_VFS_S_SOURCE)) {
        if (reset) W32(s + ES2_VFS_S_STATUS, 0);
        out->eax = al1(eax);
        return ES2_VFS_DONE;
    }
    const uint32_t kind = R32(s + ES2_VFS_S_KIND);
    if (kind == 7 || kind == 9) return unmodeled(h);
    if (kind != 2) {
        if (reset) W32(s + ES2_VFS_S_STATUS, 0);
        W32(s + ES2_VFS_S_POS, position);
        out->eax = al1(eax);
        return ES2_VFS_DONE;
    }
    uint32_t b;
    const es2_vfs_archive *arc = member_archive(h, s, &b);
    const uint32_t length = R32(s + ES2_VFS_S_LENGTH);
    const uint32_t apos = position < length ? R32(s + ES2_VFS_S_START) + position : length;
    if (!arc || apos >= arc->vol->input.size) return unmodeled(h);
    if (reset) W32(s + ES2_VFS_S_STATUS, 0);
    W32(s + ES2_VFS_S_POS, position);
    W32(s + ES2_VFS_S_APOS, apos);
    if (position >= length) W32(s + ES2_VFS_S_STATUS, 2);
    observe(h, VA_BUFFERED_SEEK);
    buffered_at(h, b, apos);
    /* AL = the buffered seek's success (the target lies in the file); its
     * EAX bits 8-31 depend on B's buffer and are not reproduced: the value
     * here is the in-window path's (B with AL = 1). */
    out->eax = al1(b);
    return ES2_VFS_DONE;
}

/* The member path of the read 4735b8 (status 0, size nonzero, no handle).
 * copy 1 writes the bytes to the buffer, 0 counts the bytes that differ
 * from it (compare mode of the read hook), -1 leaves it alone. move: the
 * buffered stream gets its empty window at the new position (commit, or a
 * caller whose compare declares B as a region). *from and *count: the source
 * bytes in the archive file. */
static es2_vfs_outcome member_read(const es2_vfs_host *h, uint32_t s, uint32_t size, uint32_t buffer, int copy,
                                   int move, const uint8_t **from_out, uint32_t *count_out, es2_vfs_result *out) {
    uint32_t b;
    const es2_vfs_archive *arc = member_archive(h, s, &b);
    if (!arc || R32(s + ES2_VFS_S_SOURCE) != b) return unmodeled(h);
    const uint32_t tell = R32(b + ES2_VFS_B_BASE) + R32(b + ES2_VFS_B_CURSOR);
    const uint32_t pos = R32(s + ES2_VFS_S_POS), length = R32(s + ES2_VFS_S_LENGTH);
    const int resync = tell != R32(s + ES2_VFS_S_APOS);
    uint32_t at = tell;
    if (resync) {
        /* 47362b: the stream's own seek (virtual +8) to S+16h, done here as
         * the modeled kind-2 seek; it needs the standard vtable. */
        if (R32(s) != VA(VA_STREAM_VTABLE) || R32(s + ES2_VFS_S_KIND) != 2) return unmodeled(h);
        at = pos < length ? R32(s + ES2_VFS_S_START) + pos : length;
    } else if (R32(b + ES2_VFS_B_STATUS)) {
        return unmodeled(h);                                /* B's read would deliver nothing */
    }
    const int normal = size + pos <= length;               /* 47363d: unsigned, 32-bit sum */
    const uint32_t count = normal ? size : length - pos + 1u;
    const size_t file = arc->vol->input.size;
    if (at > file || count > file - at) return unmodeled(h);
    if (resync) {
        observe(h, VA_SEEK);
        W32(s + ES2_VFS_S_POS, pos);
        W32(s + ES2_VFS_S_APOS, at);
        if (pos >= length) W32(s + ES2_VFS_S_STATUS, 2);
    }
    if (!normal) W32(s + ES2_VFS_S_STATUS, 2);
    observe(h, VA_BUFFERED_READ);
    const uint8_t *from = arc->vol->input.data + at;
    if (copy >= 0) {
        uint8_t *to = h->m->host(h->m->context, buffer);
        if (copy) memcpy(to, from, count);
        else for (uint32_t i = 0; i < count; ++i) out->differences += to[i] != from[i];
    }
    if (from_out) *from_out = from;
    if (count_out) *count_out = count;
    W32(s + ES2_VFS_S_POS, normal ? pos + size : length + 1u);
    observe(h, VA_BUFFERED_TELL);
    W32(s + ES2_VFS_S_APOS, at + count);
    if (move) {
        W32(b + ES2_VFS_B_STATUS, 0);
        W32(b + ES2_VFS_B_BASE, at + count);
        W32(b + ES2_VFS_B_VALID, 0);
        W32(b + ES2_VFS_B_CURSOR, 0);
    }
    out->eax = normal ? al1(at + count) : 0;
    return ES2_VFS_DONE;
}

/* Original: DBSIM.EXE 0x4735b8 */
es2_vfs_outcome es2_vfs_stream_read(const es2_vfs_host *h, uint32_t s, uint32_t size, uint32_t buffer,
                             uint32_t entry_eax, es2_vfs_result *out) {
    out->differences = 0;
    if (R32(s + ES2_VFS_S_STATUS)) { out->eax = 0; return ES2_VFS_DONE; }
    if (!size) { out->eax = al1(entry_eax); return ES2_VFS_DONE; }
    const uint32_t handle = R16(s + ES2_VFS_S_HANDLE);
    if (handle != 0xffffu) {
        if (!h->commit) return ES2_VFS_SKIP;
        const uint32_t a[3] = {sx16(handle), buffer, size};
        const uint32_t n = call(h, VA_RTL_READ, a, 3);
        if (n == UINT32_MAX) { W32(s + ES2_VFS_S_STATUS, 1); out->eax = 0; }
        else if (size != n) { W32(s + ES2_VFS_S_STATUS, 2); out->eax = 0; }
        else out->eax = al1(n + 1u);
        return ES2_VFS_DONE;
    }
    return member_read(h, s, size, buffer, h->commit ? 1 : 0, h->commit, NULL, NULL, out);
}

/* A read of a member stream as es2_vfs_stream_read does it, without
 * a runtime call: the bytes go to `buffer` (when nonzero) in both modes,
 * *from and *count give their source in the archive file (count 0 when the
 * read delivers nothing), and B moves when commit or `move` is set. A
 * stream with a handle is not modeled (PASS/SKIP, nothing written). */
es2_vfs_outcome es2_vfs_stream_fetch(const es2_vfs_host *h, uint32_t s, uint32_t size, uint32_t buffer,
                                     uint32_t entry_eax, int move, const uint8_t **from, uint32_t *count,
                                     es2_vfs_result *out) {
    out->differences = 0;
    *from = NULL; *count = 0;
    if (R32(s + ES2_VFS_S_STATUS)) { out->eax = 0; return ES2_VFS_DONE; }
    if (!size) { out->eax = al1(entry_eax); return ES2_VFS_DONE; }
    if (R16(s + ES2_VFS_S_HANDLE) != 0xffffu) return unmodeled(h);
    return member_read(h, s, size, buffer, buffer ? 1 : -1, h->commit || move, from, count, out);
}
