#include "es2/core/asset_load.h"
#include "fixed.h"
#include <string.h>

/* The persistent-object loader and the bitmap classes of DBSIM
 * (docs/reverse-engineering/asset_loading.md). Fields are read again where the
 * original reads them. In replaced mode (commit) the functions make the
 * original's writes and calls in its order; in compare mode (commit 0) they
 * write only the caller's clone, the guest scratch and the stream's buffered
 * stream B (a declared region), and describe the objects the original must have
 * built in h->expect. */
#define M_ (h->vfs.m)
#define R32(a) M_->read32(M_->context,(a))
#define R16(a) M_->read16(M_->context,(a))
#define W32(a,v) M_->write32(M_->context,(a),(v))
#define W16(a,v) M_->write16(M_->context,(a),(uint16_t)(v))
#define R8(a) (*(const uint8_t *)M_->host(M_->context,(a)))
#define W8(a,v) (*(uint8_t *)M_->host(M_->context,(a))=(uint8_t)(v))
#define VA(va) (h->vfs.image+(uint32_t)(va)-0x400000u)
#define TRY(x) do { es2_vfs_outcome t_ = (x); if (t_ != ES2_VFS_DONE) return t_; } while (0)

enum {
    VA_LOAD = 0x47a144, VA_HEADER = 0x47a6b4, VA_LOOKUP = 0x47a4a0, VA_DRAIN = 0x47a83c, VA_STATUS = 0x4930ab,
    VA_CONTEXT = 0x6c5964, VA_REGISTRIES = 0x4a1690, VA_REGISTRY = 0x6c5834, VA_FALLBACK = 0x4a1694,
    VA_ARENA = 0x6c5830, VA_COUNTER = 0x6cbee4, VA_NEW = 0x4774f0, VA_ALLOC = 0x47a288, VA_ALLOC_NEW = 0x47a2c8,
    VA_STREAM_VT = 0x4a0e10, VA_TELL = 0x473368, VA_READ = 0x4735b8, VA_CLOSE = 0x47307c,
    VA_BASE_VT = 0x4a16e8, VA_BASE_SLOT = 0x47a904, VA_BASE_DTOR = 0x47a8a4, VA_DECODER_SIZE = 0x496406,
    VA_DELETE = 0x477548,
    VA_BITMAP_NEW = 0x481cd4, VA_BITMAP_CTOR = 0x48156c, VA_BITMAP_READ = 0x481d8c, VA_BITMAP_VT = 0x4a306c,
    VA_ARRAY_NEW = 0x482d88, VA_ARRAY_CTOR = 0x482f38, VA_ARRAY_READ = 0x482e7c, VA_ARRAY_VT = 0x4a32ec,
    VA_VT_ROOT = 0x4a69f0, VA_VT_BMP1 = 0x4a3368, VA_VT_BMP2 = 0x4a3040,
    /* The memory stream class (constructor 47b36c) */
    VA_MEM_VT = 0x4a1788, VA_MEM_TELL = 0x47b400, VA_MEM_READ = 0x47b4b4
};

static void observe(const es2_asset_host *h, uint32_t va) { if (h->vfs.observe) h->vfs.observe(h->vfs.context, va); }
static es2_vfs_outcome unmodeled(const es2_asset_host *h) { return h->vfs.commit ? ES2_VFS_PASS : ES2_VFS_SKIP; }
/* dts_load's PASS with its reason (ES2_ASSET_PASS_*; replaced mode only). */
static es2_vfs_outcome pass(const es2_asset_host *h, uint32_t reason) {
    if (h->vfs.commit) observe(h, reason);
    return unmodeled(h);
}
/* A guest call (commit only): `observed` is the VA reported to the
 * observer, `target` the address called; registers in r (EAX/EDX/ECX in,
 * out). Returns the exit EAX. */
static uint32_t call(const es2_asset_host *h, uint32_t observed, uint32_t target, const uint32_t *args, unsigned count,
                     uint32_t r[3]) {
    observe(h, observed);
    h->vfs.call(h->vfs.context, target, args, count, r);
    return r[0];
}
static uint32_t call_va(const es2_asset_host *h, uint32_t va, const uint32_t *args, unsigned count, uint32_t r[3]) {
    return call(h, va, VA(va), args, count, r);
}
static void counter_add(const es2_asset_host *h, uint32_t delta) {
    const uint32_t c = R32(VA(VA_COUNTER));
    W32(c, R32(c) + delta);
}
static int vol_stream(const es2_asset_host *h, uint32_t s) { return R32(s) == VA(VA_STREAM_VT); }
static int mem_stream(const es2_asset_host *h, uint32_t s) { return R32(s) == VA(VA_MEM_VT); }

/* 47b4b4, the memory stream's read (S, size, buffer), replaced mode: status
 * set (47b4c7): EAX 0, nothing read. Else avail = end - cursor; avail >= size
 * (signed, 47b4d7): memcpy(buffer, cursor, size), cursor += size, EAX = the
 * memcpy's (the buffer) with AL 1; else memcpy of avail bytes, cursor = end
 * and 47b3a0(S, 0): status 2 (the cursor is at the end, 47b3ac/47b3b2), EAX
 * 0. A negative count (the original's memcpy would run wild) or overlapping
 * ranges are not modeled: 0, nothing written (the caller makes the virtual
 * call). *count: the bytes copied. */
static int mem_read(const es2_asset_host *h, uint32_t s, uint32_t size, uint32_t buffer, uint32_t *count, uint32_t *eax) {
    *count = 0;
    if (R32(s + ES2_ASSET_MEM_STATUS)) { *eax = 0; return 1; }
    const uint32_t cursor = R32(s + ES2_ASSET_MEM_CURSOR), avail = R32(s + ES2_ASSET_MEM_END) - cursor;
    const int whole = (int32_t)avail >= (int32_t)size;
    const uint32_t n = whole ? size : avail;
    if ((int32_t)n < 0) return 0;
    if (n && (uint64_t)buffer < (uint64_t)cursor + n && (uint64_t)cursor < (uint64_t)buffer + n) return 0;
    if (n) memcpy(M_->host(M_->context, buffer), M_->host(M_->context, cursor), n);
    W32(s + ES2_ASSET_MEM_CURSOR, cursor + n);
    if (whole) {
        *eax = (buffer & 0xffffff00u) | 1u;
    } else {
        W32(s + ES2_ASSET_MEM_STATUS, 2);
        *eax = 0;
    }
    *count = n;
    return 1;
}

/* The stream's virtual +4 (tell). A member stream of the VOL-aware class
 * (no handle) answers from its fields as 473368 does, a memory stream as
 * 47b400 (cursor - start; replaced mode); anything else is the virtual call
 * with the call site's EAX/EDX. */
static es2_vfs_outcome s_tell(const es2_asset_host *h, uint32_t s, uint32_t eax_in, uint32_t edx_in, uint32_t *value) {
    if (vol_stream(h, s) && R16(s + ES2_VFS_S_HANDLE) == 0xffffu) {
        observe(h, VA_TELL);
        *value = R32(s + ES2_VFS_S_SOURCE) ? R32(s + ES2_VFS_S_POS) : 0;
        return ES2_VFS_DONE;
    }
    if (!h->vfs.commit) return ES2_VFS_SKIP;
    if (mem_stream(h, s)) {
        observe(h, VA_MEM_TELL);
        *value = R32(s + ES2_ASSET_MEM_CURSOR) - R32(s + ES2_ASSET_MEM_START);
        return ES2_VFS_DONE;
    }
    const uint32_t a[1] = {s};
    uint32_t r[3] = {eax_in, edx_in, 0};
    *value = call(h, VA_TELL, R32(R32(s) + 4u), a, 1, r);
    return ES2_VFS_DONE;
}
/* The stream's virtual +18h (read size bytes to buffer). The VOL-aware
 * class goes through the VOL member read (es2_vfs_stream_fetch: bytes from
 * the archive file), the memory class (replaced mode) through mem_read;
 * when neither models the state the virtual call runs (commit) or the call
 * is SKIP. *count: the bytes delivered (the
 * virtual call's count is not known: size). buffer 0 (compare only): no
 * copy, *from gives the source. eax_in/edx_in: the call site's EAX/EDX
 * (the read uses EAX for a zero size). */
static es2_vfs_outcome s_read(const es2_asset_host *h, uint32_t s, uint32_t size, uint32_t buffer, uint32_t eax_in,
                              uint32_t edx_in, const uint8_t **from, uint32_t *count, uint32_t *eax) {
    if (from) *from = NULL;
    if (vol_stream(h, s)) {
        es2_vfs_result r = {0, 0};
        const uint8_t *f; uint32_t c;
        observe(h, VA_READ);
        es2_vfs_outcome o = es2_vfs_stream_fetch(&h->vfs, s, size, buffer, eax_in, 1, &f, &c, &r);
        if (o == ES2_VFS_DONE) {
            *eax = r.eax;
            if (from) *from = f;
            if (count) *count = c;
            return ES2_VFS_DONE;
        }
        if (!h->vfs.commit) return ES2_VFS_SKIP;
    } else if (!h->vfs.commit) {
        return ES2_VFS_SKIP;
    } else if (mem_stream(h, s)) {
        uint32_t c;
        observe(h, VA_MEM_READ);
        if (mem_read(h, s, size, buffer, &c, eax)) {
            if (count) *count = c;
            return ES2_VFS_DONE;
        }
    } else {
        observe(h, VA_READ);
    }
    const uint32_t a[3] = {s, size, buffer};
    uint32_t r[3] = {eax_in, edx_in, 0};
    h->vfs.call(h->vfs.context, R32(R32(s) + 0x18u), a, 3, r);
    *eax = r[0];
    if (count) *count = size;
    return ES2_VFS_DONE;
}
/* 47a288 / 47a2c8 (the same code at two addresses): `size` bytes from the
 * current arena [6c5830] when its end (32-bit sum) stays at or below the
 * limit, else the runtime's new 4774f0 (commit only). */
static uint32_t alloc(const es2_asset_host *h, uint32_t va, uint32_t size) {
    observe(h, va);
    const uint32_t a = R32(VA(VA_ARENA));
    if (a) {
        const uint32_t used = R32(a + 8u), end = used + size;
        if (end <= R32(a + 4u)) { W32(a + 8u, end); return R32(a) + used; }
    }
    const uint32_t args[1] = {size};
    uint32_t r[3] = {0, 0, 0};
    return call_va(h, VA_NEW, args, 1, r);
}
/* 47a4a0: the registry entry (tag, factory, name) of a tag, or 0. The
 * table at 6c5834 holds [4a1690] (entries, count) pairs. */
static uint32_t lookup(const es2_asset_host *h, uint32_t tag) {
    for (uint32_t i = 0; i < R32(VA(VA_REGISTRIES)); ++i) {
        const uint32_t pair = VA(VA_REGISTRY) + 8u * i, entries = R32(pair);
        for (uint32_t k = 0; k < R32(pair + 4u); ++k)
            if (R32(entries + 12u * k) == tag) return entries + 12u * k;
    }
    return 0;
}
static int expect_push(const es2_asset_host *h, unsigned *index) {
    if (!h->expect || h->expect->count >= ES2_ASSET_EXPECT_MAX) return 0;
    *index = h->expect->count++;
    memset(&h->expect->objects[*index], 0, sizeof(h->expect->objects[*index]));
    return 1;
}

static es2_vfs_outcome load(const es2_asset_host *h, uint32_t s, uint32_t arg, unsigned depth, uint32_t *result);

/* 481d8c: the GLBitmap fields from the stream. exact: the object's
 * pre-state is real (else a read that delivers less than it asks is SKIP in
 * compare mode). Compare: the pixel and colour reads are not copied; their
 * sources go to *pixels / *colours. */
static es2_vfs_outcome bitmap_read(const es2_asset_host *h, uint32_t b, uint32_t s, uint32_t entry_ecx, unsigned depth,
                                   int exact, const uint8_t **pixels, const uint8_t **colours, uint32_t *eax) {
    const int commit = h->vfs.commit;
    const uint32_t local = h->scratch + ES2_ASSET_LOCAL + depth;
    uint32_t count, n;
    W8(local, entry_ecx >> 24);                             /* push ecx: [ebp-1] */
    static const struct { uint32_t offset, size; } fields[3] = {{4, 2}, {6, 2}, {8, 1}};
    for (unsigned i = 0; i < 3; ++i) {                      /* 492430, 492430, 490874 */
        TRY(s_read(h, s, fields[i].size, b + fields[i].offset, R32(s), b + fields[i].offset, NULL, &count, eax));
        if (!commit && !exact && count != fields[i].size) return ES2_VFS_SKIP;
    }
    TRY(s_read(h, s, 1, local, R32(s), local, NULL, &count, eax));        /* 481f3d */
    if (!commit && !exact && count != 1) return ES2_VFS_SKIP;
    const uint8_t v = R8(local);
    W8(b + 9u, (v & 0x0fu) | (R8(b + 9u) & 0xf0u));
    W8(b + 9u, (R8(b + 9u) & 0xefu) | (v & 0x10u));
    TRY(s_read(h, s, 1, local, R32(s), local, NULL, &count, eax));
    if (!commit && !exact && count != 1) return ES2_VFS_SKIP;
    W16(b + 0x12u, R8(local));
    W32(b + 0x0eu, 0);
    W32(b + 0x16u, 0);
    TRY(s_read(h, s, 4, b + 0x0au, R32(s), b + 0x0au, NULL, &count, eax));    /* 482f21 */
    if (!commit && !exact && count != 4) return ES2_VFS_SKIP;
    TRY(s_read(h, s, 2, b + 0x14u, R32(s), b + 0x14u, NULL, &count, eax));    /* 492430 */
    if (!commit && !exact && count != 2) return ES2_VFS_SKIP;
    if ((n = R32(b + 0x0au)) != 0) {
        if (commit) {
            const uint32_t p = alloc(h, VA_ALLOC, n);
            W32(b + 0x0eu, p);
            TRY(s_read(h, s, R32(b + 0x0au), R32(b + 0x0eu), R32(s), R32(b + 0x0eu), NULL, NULL, eax));   /* 482bd7 */
        } else {
            TRY(s_read(h, s, n, 0, R32(s), 0, pixels, &count, eax));
            if (count != n) return ES2_VFS_SKIP;
        }
    }
    if (R16(b + 0x14u)) {
        n = (uint32_t)fx_s16(R16(b + 0x14u)) << 2;
        if (commit) {
            const uint32_t p = alloc(h, VA_ALLOC, n);
            W32(b + 0x16u, p);
            const uint32_t size = (uint32_t)fx_s16(R16(b + 0x14u)) << 2;        /* 481e80: movsx, shl; EAX = size */
            TRY(s_read(h, s, size, R32(b + 0x16u), size, R32(s), NULL, NULL, eax));
        } else {
            TRY(s_read(h, s, n, 0, n, R32(s), colours, &count, eax));
            if (count != n) return ES2_VFS_SKIP;
        }
    }
    return ES2_VFS_DONE;
}
static void expect_bitmap(const es2_asset_host *h, unsigned i, uint32_t b, const uint8_t *pixels, const uint8_t *colours) {
    es2_asset_object *o = &h->expect->objects[i];
    o->kind = ES2_ASSET_BITMAP;
    o->end = i + 1u;
    o->height = R16(b + 4u); o->width = R16(b + 6u); o->depth = R8(b + 8u); o->flags = R8(b + 9u) & 0x1fu;
    o->compression = R16(b + 0x12u); o->pixel_size = R32(b + 0x0au); o->colours = R16(b + 0x14u);
    o->pixels = pixels; o->colour_bytes = colours;
}

/* 482e7c: count, the pointer array, one load 47a144(S, 0) per element.
 * Compare: the children's loads are modeled one level deeper. */
static es2_vfs_outcome array_read(const es2_asset_host *h, uint32_t a, uint32_t s, unsigned depth, int exact,
                                  uint32_t *eax) {
    uint32_t count;
    TRY(s_read(h, s, 4, a + 4u, R32(s), a + 4u, NULL, &count, eax));       /* 482f21 */
    if (h->vfs.commit) {
        const uint32_t p = alloc(h, VA_ALLOC, R32(a + 4u) << 2);
        W32(a + 8u, p);
        *eax = p;
        for (uint32_t i = 0; (int32_t)i < (int32_t)R32(a + 4u); ++i) {
            const uint32_t args[2] = {s, 0};
            uint32_t r[3] = {0, 0, 0};
            *eax = call_va(h, VA_LOAD, args, 2, r);
            W32(R32(a + 8u) + 4u * i, *eax);
        }
        return ES2_VFS_DONE;
    }
    if (!exact && count != 4) return ES2_VFS_SKIP;
    const uint32_t n = R32(a + 4u);
    unsigned index;
    if (n > ES2_ASSET_EXPECT_MAX || !expect_push(h, &index)) return ES2_VFS_SKIP;
    h->expect->objects[index].kind = ES2_ASSET_ARRAY;
    h->expect->objects[index].count = n;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t child;
        TRY(load(h, s, 0, depth + 1u, &child));
    }
    h->expect->objects[index].end = h->expect->count;
    *eax = 0;
    return ES2_VFS_DONE;
}

/* Factories: the object from the arena (47a2c8), the constructor's
 * stores, the counter, the class read through the vtable. Compare: the
 * object is the scratch object of this depth. */
static es2_vfs_outcome bitmap_new(const es2_asset_host *h, uint32_t s, uint32_t arg, unsigned depth, uint32_t *object) {
    if (h->vfs.commit) {
        const uint32_t b = alloc(h, VA_ALLOC_NEW, ES2_ASSET_BITMAP_SIZE);
        if (b) {
            observe(h, VA_BITMAP_CTOR);                     /* 48156c */
            W32(b, VA(VA_VT_ROOT));
            W32(b, VA(VA_VT_BMP1));
            counter_add(h, 1);
            W32(b, VA(VA_VT_BMP2));
            counter_add(h, 1);
            W32(b, VA(VA_BITMAP_VT));
            W32(b + 0x0au, 0);
            W16(b + 0x14u, 0);
            W32(b + 0x16u, 0);
            W32(b + 0x0eu, 0);
            counter_add(h, 1);
            counter_add(h, (uint32_t)-3);                   /* 481d13 */
        }
        /* A null object faults at 481d2d (mov ecx,[ebx]) as here. */
        const uint32_t vt = R32(b);
        const uint32_t args[3] = {b, s, arg};
        uint32_t r[3] = {arg, s, vt};
        call(h, VA_BITMAP_READ, R32(vt + 0x0cu), args, 3, r);
        *object = b;
        return ES2_VFS_DONE;
    }
    if (R32(VA(VA_BITMAP_VT) + 0x0cu) != VA(VA_BITMAP_READ)) return ES2_VFS_SKIP;
    const uint32_t b = h->scratch + ES2_ASSET_OBJECT + 0x20u * depth;
    for (unsigned i = 0; i < 0x20u; i += 4) W32(b + i, 0);
    W32(b, VA(VA_BITMAP_VT));
    const uint8_t *pixels = NULL, *colours = NULL;
    uint32_t eax;
    unsigned index;
    if (!expect_push(h, &index)) return ES2_VFS_SKIP;
    TRY(bitmap_read(h, b, s, VA(VA_BITMAP_VT), depth, 0, &pixels, &colours, &eax));
    expect_bitmap(h, index, b, pixels, colours);
    *object = 0;
    return ES2_VFS_DONE;
}
static es2_vfs_outcome array_new(const es2_asset_host *h, uint32_t s, uint32_t arg, unsigned depth, uint32_t *object) {
    if (h->vfs.commit) {
        const uint32_t a = alloc(h, VA_ALLOC_NEW, ES2_ASSET_ARRAY_SIZE);
        if (a) {
            observe(h, VA_ARRAY_CTOR);                      /* 482f38, 482f80(a, 0, 0) */
            W32(a, VA(VA_VT_ROOT));
            W32(a, VA(VA_ARRAY_VT));
            W32(a + 4u, 0);
            W32(a + 8u, 0);
            counter_add(h, 1);
            counter_add(h, (uint32_t)-1);                   /* 482dc7 */
        }
        const uint32_t vt = R32(a);                         /* a null object faults at 482de0 */
        const uint32_t args[3] = {a, s, arg};
        uint32_t r[3] = {arg, s, vt};
        call(h, VA_ARRAY_READ, R32(vt + 0x0cu), args, 3, r);
        *object = a;
        return ES2_VFS_DONE;
    }
    if (R32(VA(VA_ARRAY_VT) + 0x0cu) != VA(VA_ARRAY_READ)) return ES2_VFS_SKIP;
    const uint32_t a = h->scratch + ES2_ASSET_OBJECT + 0x20u * depth;
    W32(a, VA(VA_ARRAY_VT)); W32(a + 4u, 0); W32(a + 8u, 0);
    uint32_t eax;
    TRY(array_read(h, a, s, depth, 0, &eax));
    *object = 0;
    return ES2_VFS_DONE;
}

/* 47a144 with the header 47a6b4 and the drain 47a83c. */
static es2_vfs_outcome load(const es2_asset_host *h, uint32_t s, uint32_t arg, unsigned depth, uint32_t *result) {
    const int commit = h->vfs.commit;
    if (depth >= ES2_ASSET_DEPTH) return unmodeled(h);     /* compare only: commit loads at depth 0 */
    const int memory = mem_stream(h, s);
    if (!memory && !vol_stream(h, s)) return pass(h, ES2_ASSET_PASS_STREAM);
    if (memory && !commit) return ES2_VFS_SKIP;
    if (!memory && R16(s + ES2_VFS_S_HANDLE) != 0xffffu) return pass(h, ES2_ASSET_PASS_LOOSE);
    const uint32_t frame = h->scratch + 0x20u * depth, local = h->scratch + ES2_ASSET_LOCAL + depth;
    /* The peek: the header read without touching S (and B). A tag the
     * registry lacks leaves the frame's end dword unset; the drain reads it
     * unless the stream's status is nonzero after the header. */
    {
        uint32_t status;
        W32(frame + 0x10u, UINT32_MAX); W32(frame + 0x14u, 0);
        if (memory) {                                       /* 47b4b4 on the fields */
            status = R32(s + ES2_ASSET_MEM_STATUS);
            if (!status) {
                const uint32_t cursor = R32(s + ES2_ASSET_MEM_CURSOR), avail = R32(s + ES2_ASSET_MEM_END) - cursor;
                if ((int32_t)avail < 0) return pass(h, ES2_ASSET_PASS_UNSERVED);
                const uint32_t n = (int32_t)avail >= 8 ? 8u : avail;
                for (uint32_t i = 0; i < n; ++i) W8(frame + 0x10u + i, R8(cursor + i));
                if (n < 8) status = 2;
            }
        } else {                                            /* the VOL member read on a copy of S */
            const uint32_t copy = h->scratch + ES2_ASSET_PEEK;
            memcpy(M_->host(M_->context, copy), M_->host(M_->context, s), ES2_VFS_STREAM_SIZE);
            es2_vfs_host quiet = h->vfs;
            quiet.commit = 0; quiet.observe = NULL;
            es2_vfs_result r = {0, 0};
            const uint8_t *f; uint32_t c;
            if (es2_vfs_stream_fetch(&quiet, copy, 8, frame + 0x10u, R32(copy), 0, &f, &c, &r) != ES2_VFS_DONE)
                return pass(h, ES2_ASSET_PASS_UNSERVED);
            status = R32(copy + ES2_VFS_S_STATUS);
        }
        if (!lookup(h, R32(frame + 0x10u))) {
            if (R32(VA(VA_FALLBACK))) return pass(h, ES2_ASSET_PASS_FALLBACK);
            if (!status) return pass(h, ES2_ASSET_PASS_TAG);
        }
    }
    const uint32_t saved = R32(VA(VA_CONTEXT));
    if (commit) W32(VA(VA_CONTEXT), frame);                /* 47a15b */
    observe(h, VA_HEADER);
    uint32_t t, eax;
    TRY(s_tell(h, s, R32(s), frame + 0x10u, &t));
    W32(frame + 4u, t);
    W32(frame + 0x10u, UINT32_MAX);
    W32(frame + 0x14u, 0);
    TRY(s_read(h, s, 8, frame + 0x10u, R32(s), frame, NULL, NULL, &eax));
    const uint32_t tag = R32(frame + 0x10u);
    observe(h, VA_LOOKUP);
    uint32_t entry = lookup(h, tag);
    if (entry) {
        W32(frame, tag);
        TRY(s_tell(h, s, entry, R32(s), &t));
        const uint32_t length = R32(frame + 0x14u);
        W32(frame + 8u, (t + length + 1u) & ~1u);           /* 47a70d..47a712 */
        W8(frame + 0x0cu, (length & 1u) == 0);
    }
    uint32_t object = 0;
    int built = 0;
    if (tag != UINT32_MAX) {                                /* 47a171 */
        observe(h, VA_LOOKUP);
        const uint32_t e = lookup(h, tag);
        W32(frame + 0x18u, e);
        if (e) {                                            /* 47a182 */
            const uint32_t factory = R32(e + 4u);
            if (commit) {
                const uint32_t args[2] = {s, arg};
                uint32_t r[3] = {e, arg, e};
                object = call(h, factory - h->vfs.image + 0x400000u, factory, args, 2, r);
            } else if (factory == VA(VA_BITMAP_NEW)) {
                TRY(bitmap_new(h, s, arg, depth, &object));
            } else if (factory == VA(VA_ARRAY_NEW)) {
                TRY(array_new(h, s, arg, depth, &object));
            } else {
                return ES2_VFS_SKIP;
            }
            built = 1;
        }
    }
    if (!commit && !built) {                                /* nothing built: the pointer is 0 */
        unsigned index;
        if (!expect_push(h, &index)) return ES2_VFS_SKIP;
        h->expect->objects[index].kind = ES2_ASSET_NONE;
        h->expect->objects[index].end = index + 1u;
    }
    observe(h, VA_DRAIN);
    for (;;) {
        if (R32(s + ES2_VFS_S_STATUS)) break;              /* 4930ab */
        TRY(s_tell(h, s, 0, R32(s), &t));
        const uint32_t context = commit ? R32(VA(VA_CONTEXT)) : frame;
        if ((int32_t)t >= (int32_t)R32(context + 8u)) break; /* 47a86e */
        TRY(s_read(h, s, 1, local, R32(s), local, NULL, NULL, &eax));     /* 481f3d */
    }
    if (commit) W32(VA(VA_CONTEXT), saved);                 /* 47a19b */
    *result = object;
    return ES2_VFS_DONE;
}

/* Original: DBSIM.EXE 0x47a144 */
es2_vfs_outcome es2_asset_dts_load(const es2_asset_host *h, uint32_t stream, uint32_t arg, es2_asset_result *out) {
    if (h->expect) h->expect->count = 0;
    uint32_t object = 0;
    es2_vfs_outcome o = load(h, stream, arg, 0, &object);
    out->eax = object;
    out->edx = 0;
    return o;
}

/* Original: DBSIM.EXE 0x481cd4 */
es2_vfs_outcome es2_asset_bitmap_new(const es2_asset_host *h, uint32_t stream, uint32_t arg, uint32_t fs0,
                                     es2_asset_result *out) {
    if (h->expect) h->expect->count = 0;
    uint32_t object = 0;
    es2_vfs_outcome o = bitmap_new(h, stream, arg, 0, &object);
    out->eax = object;
    out->edx = fs0;
    return o;
}

/* Original: DBSIM.EXE 0x482d88 */
es2_vfs_outcome es2_asset_bitmap_array_new(const es2_asset_host *h, uint32_t stream, uint32_t arg, uint32_t fs0,
                                           es2_asset_result *out) {
    if (h->expect) h->expect->count = 0;
    uint32_t object = 0;
    es2_vfs_outcome o = array_new(h, stream, arg, 0, &object);
    out->eax = object;
    out->edx = fs0;
    return o;
}

/* Original: DBSIM.EXE 0x481d8c */
es2_vfs_outcome es2_asset_bitmap_read(const es2_asset_host *h, uint32_t bitmap, uint32_t stream, uint32_t entry_ecx,
                                      es2_asset_result *out) {
    out->eax = 0; out->edx = 0;
    if (h->vfs.commit) return bitmap_read(h, bitmap, stream, entry_ecx, 0, 1, NULL, NULL, &out->eax);
    unsigned index;
    h->expect->count = 0;
    if (!expect_push(h, &index)) return ES2_VFS_SKIP;
    const uint8_t *pixels = NULL, *colours = NULL;
    TRY(bitmap_read(h, bitmap, stream, entry_ecx, 0, 1, &pixels, &colours, &out->eax));
    expect_bitmap(h, index, bitmap, pixels, colours);
    return ES2_VFS_DONE;
}

/* Original: DBSIM.EXE 0x482e7c */
es2_vfs_outcome es2_asset_bitmap_array_read(const es2_asset_host *h, uint32_t array, uint32_t stream,
                                            es2_asset_result *out) {
    out->eax = 0; out->edx = 0;
    if (h->expect) h->expect->count = 0;
    return array_read(h, array, stream, 0, 1, &out->eax);
}

/* Original: DBSIM.EXE 0x472f64 (with the base destructor 47a8a4(S, 0)) */
es2_vfs_outcome es2_asset_stream_destroy(const es2_asset_host *h, uint32_t s, uint32_t flag, uint32_t fs0,
                                         es2_asset_result *out) {
    const int commit = h->vfs.commit;
    if (!s) {                                               /* 472f82 */
        out->edx = R32(VA(VA_COUNTER));
        counter_add(h, (uint32_t)-1);
        out->eax = fs0;                                     /* 49630c leaves FS:[0] in EAX */
        return ES2_VFS_DONE;
    }
    if (!commit && (R32(s + ES2_VFS_S_DECODER7) || R32(s + ES2_VFS_S_DECODER9) || (flag & 1u) ||
                    R32(VA(VA_BASE_VT) + 0x10u) != VA(VA_BASE_SLOT) ||
                    (R8(s + ES2_VFS_S_OWNED) && R16(s + ES2_VFS_S_HANDLE) != 0xffffu)))
        return ES2_VFS_SKIP;
    counter_add(h, (uint32_t)-1);                           /* 472f7e */
    W32(s, VA(VA_STREAM_VT));
    for (unsigned k = 0; k < 2; ++k) {                      /* 472f8e, 472fcd: the decoder objects */
        const uint32_t d = R32(s + (k ? ES2_VFS_S_DECODER9 : ES2_VFS_S_DECODER7));
        if (!d) continue;
        const uint32_t a[2] = {d, 0};
        uint32_t r[3] = {d, d, 0};
        const uint32_t n = call_va(h, VA_DECODER_SIZE, a, 2, r);
        counter_add(h, n);
        const uint32_t vt = R32(d);
        const uint32_t b[2] = {d, 3};
        uint32_t q[3] = {d, vt, R32(VA(VA_COUNTER))};
        call(h, R32(vt) - h->vfs.image + 0x400000u, R32(vt), b, 2, q);    /* the decoder's virtual destructor */
    }
    if (commit) {
        const uint32_t a[1] = {s};
        uint32_t r[3] = {0, 0, 0};
        call_va(h, VA_CLOSE, a, 1, r);
    } else {
        es2_vfs_result r = {0, 0};
        observe(h, VA_CLOSE);
        TRY(es2_vfs_stream_close(&h->vfs, s, &r));
    }
    observe(h, VA_BASE_DTOR);
    const uint32_t c = R32(VA(VA_COUNTER));                 /* 47a8a7 */
    W32(c, R32(c) - 1u);
    W32(s, VA(VA_BASE_VT));
    const uint32_t slot = R32(VA(VA_BASE_VT) + 0x10u);
    if (slot == VA(VA_BASE_SLOT)) {
        out->eax = (c & 0xffffff00u) | 1u;                  /* 47a907: mov al,1 */
    } else {
        const uint32_t a[1] = {s};
        uint32_t r[3] = {c, VA(VA_BASE_VT), s};
        out->eax = call(h, slot - h->vfs.image + 0x400000u, slot, a, 1, r);
    }
    out->edx = VA(VA_BASE_VT);
    if (flag & 1u) {                                        /* 47301e */
        const uint32_t a[1] = {s};
        uint32_t r[3] = {out->eax, out->edx, s};
        out->eax = call_va(h, VA_DELETE, a, 1, r);
        out->edx = r[1];
    }
    return ES2_VFS_DONE;
}
