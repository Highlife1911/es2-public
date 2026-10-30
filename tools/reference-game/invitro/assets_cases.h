/* Group assets (docs/reverse-engineering/asset_loading.md): dts_load 47a144,
 * bitmap_new 481cd4, bitmap_read 481d8c, bitmap_array_new 482d88,
 * bitmap_array_read 482e7c, stream_destroy 472f64.
 *
 * Built on the vfs harness (vfs_cases.h): the real CD archives under
 * ES2_DATA_ROOT behind DBSIM's file imports, the archive manager built by
 * the original insert over the nine simulation archives, the bump heap
 * behind _calloc (new 4774f0 -> 47747c), the decoder stubs. Added here: the
 * four class registries (the original register 47a0b8 over the GL, TS, AN
 * and grid tables), _free as a logging stub, a fake decoder object that
 * survives 496406 and its virtual destructor (a logging stub), an arena
 * record for [6c5830] over a test arena.
 *
 * Inputs: every object of a registered class at the top level of a member
 * (shapes, palettes, fonts, cursors, grids, bitmap arrays) and every
 * bitmap inside an array (members up to 1 MiB, shapes up to 64 KiB), plus
 * positions inside pixel data whose 13 header bytes read as a small pixel
 * size and a colour count of 1..64 (the CD bitmaps have none). A stream is
 * opened with the original open 4730e8 and moved with the original seek
 * 4733a4; variations: another stream moving the archive's buffered stream
 * (resync), a shorter member length (reads past the end; never for shapes
 * and the other classes whose readers dereference their children), the
 * status preset, [6c5830] over a test arena with little room (arena and
 * new mixed). Header reads that would leave the original's drain reading
 * its unset frame dword (a tag the registry lacks with the stream still
 * readable) are not generated: they depend on stack garbage. One in
 * four of these streams is a memory stream (vtable 4a1788) over the
 * member's bytes instead, and dts_load also loads the roots of MECHWPNS.DTS
 * from memory as the weapon shape cache does (40f998/40fab0).
 *
 * Compared per case: EAX (factories and stream_destroy: EDX too), the test
 * arena (streams, objects, the arena record and its memory), the heap bytes
 * allocated in the case and the bump, the vfs state (buffered streams'
 * tells and status, file streams, file system handles, the construction
 * counter, the allocator counter 6bb30c, the decoder stub log), [6c5964],
 * [6c5830] and the logs of _free and the decoder destructor.
 *
 * Passes (limit 0): direct cases with the coverage window; real-callee
 * (dts_load and bitmap_array_new with the six hooks and stream_read,
 * stream_tell, stream_close in all 512 original/replaced combinations against
 * the all-original run); installed 31/a7 poison cases; compare cases (the hook
 * alone in compare mode: 0 mismatches; skewed: a byte of the stream's clone
 * flipped, one mismatch per compared call); for dts_load the pass reasons
 * (asc_reasons: bypassed = hk_dts_load_pass_<reason>) and the weapon shape
 * cache (asc_cache_pass: every MECHWPNS root, bypassed 0). limit > 0: main.c's
 * installed pass. */
#include "es2/formats/dbm.h"
#include "es2/formats/dts.h"
enum { ASC_S0 = 0x000, ASC_S1 = 0x040, ASC_HS = 0x80, ASC_OBJ = 0x100, ASC_ARR = 0x140, ASC_AREC = 0x180, ASC_DEC = 0x200,
       ASC_DVT = 0x240, ASC_DTI = 0x260, ASC_PATH = 0x300, ASC_AMEM = 0x1000, ASC_ARENA = 0x4000 };
static BYTE asc_arena[ASC_ARENA];
#define ASC_ADDR(o) ((uint32_t)(uintptr_t)(asc_arena + (o)))
static BYTE asc_buffer[0x1000];

/* ------------------------------------------------------------------ */
/* Stubs: _free and the fake decoder's virtual destructor. */
typedef struct asc_note_t { uint32_t id, a, b; } asc_note_t;
static asc_note_t asc_notes[16];
static unsigned asc_note_count;
static void asc_note(uint32_t id, uint32_t a, uint32_t b) {
    if (asc_note_count < 16) { asc_notes[asc_note_count].id = id; asc_notes[asc_note_count].a = a; asc_notes[asc_note_count].b = b; }
    ++asc_note_count;
}
static uint64_t __cdecl asc_free(uint32_t p) { asc_note(1, p, 0); return 0x0000f2ee0000f1eeull; }
static uint64_t __cdecl asc_decoder_dtor(uint32_t d, uint32_t flag) { asc_note(2, d, flag); return 0x0000d2ee0000d1eeull; }
static uint32_t asc_free_slot, asc_free_saved;

/* ------------------------------------------------------------------ */
/* The corpus. */
/* cached: MECHWPNS.DTS, kept only for the memory-stream roots. */
typedef struct asc_member { char path[64]; const BYTE *data; uint32_t length, tag; unsigned archive; int cached; } asc_member;
typedef struct asc_object { unsigned member; uint32_t offset, tag; } asc_object;
static asc_member *asc_members;
static unsigned asc_member_count;
static asc_object *asc_objects;          /* registered objects: roots and array children */
static unsigned asc_object_count;
static unsigned *asc_bitmaps, asc_bitmap_count;     /* indexes of bitmap objects (array children) */
static unsigned *asc_arrays, asc_array_count;       /* indexes of array roots */
static unsigned *asc_simple, asc_simple_count;      /* roots of the GL classes (truncation allowed) */
typedef struct asc_odd { unsigned member; uint32_t offset; } asc_odd;
static asc_odd asc_odds[256];
static unsigned asc_odd_count;
static unsigned *asc_others, asc_other_count;       /* members of unregistered tags (header past the end only) */
/* The roots of MECHWPNS.DTS as the weapon shape cache keeps them (40f998: one
 * heap block per root, (length + 9) AND -2 bytes; 40fab0 loads one through a
 * memory stream over its block). */
typedef struct asc_root { unsigned member; uint32_t offset, size; } asc_root;
static asc_root asc_mroots[64];
static unsigned asc_mroot_count;
enum { ASC_TAG_ARRAY = 0x00280001u, ASC_TAG_BITMAP = 0x0028000eu };
static int asc_registered(uint32_t tag) {
    const uint32_t n = get32(mapped + 0xa1690);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t entries = get32(mapped + 0x2c5834 + 8 * i), count = get32(mapped + 0x2c5838 + 8 * i);
        for (uint32_t k = 0; k < count; ++k) if (get32((const BYTE *)(uintptr_t)(entries + 12 * k)) == tag) return 1;
    }
    return 0;
}
static int asc_gl(uint32_t tag) { return (tag >> 16) == 0x28u; }
static void asc_add_object(unsigned m, uint32_t offset, uint32_t tag) {
    asc_object *o = &asc_objects[asc_object_count++];
    o->member = m; o->offset = offset; o->tag = tag;
}
static void asc_scan_member(unsigned m) {
    const asc_member *x = &asc_members[m];
    uint32_t at = 0;
    while (at + 8 <= x->length && asc_object_count < 16000) {
        const uint32_t tag = get32(x->data + at), length = get32(x->data + at + 4);
        if (!asc_registered(tag) || length > x->length - at - 8) break;
        const unsigned index = asc_object_count;
        asc_add_object(m, at, tag);
        if (tag == ASC_TAG_ARRAY && length >= 4) {
            asc_arrays[asc_array_count++] = index;
            uint32_t c = at + 12;
            const uint32_t end = at + 8 + length, count = get32(x->data + at + 8);
            for (uint32_t i = 0; i < count && c + 8 <= end && asc_object_count < 16000; ++i) {
                const uint32_t ctag = get32(x->data + c), clength = get32(x->data + c + 4);
                if (ctag != ASC_TAG_BITMAP || clength > end - c - 8) break;
                asc_bitmaps[asc_bitmap_count++] = asc_object_count;
                asc_add_object(m, c, ctag);
                /* Odd positions inside the pixel data (pixel size <= 8000h,
                 * colour count 1..64) for the colour path. */
                if (asc_odd_count < 256 && clength > 64) {
                    for (uint32_t p = c + 8 + 13; p + 13 <= c + 8 + clength && asc_odd_count < 256; p += 7) {
                        const uint32_t size = get32(x->data + p + 7);
                        const uint16_t colours = get16(x->data + p + 11);
                        if (size <= 0x8000u && colours >= 1 && colours <= 64 && !(random32() & 7)) {
                            asc_odds[asc_odd_count].member = m; asc_odds[asc_odd_count].offset = p; ++asc_odd_count;
                            break;
                        }
                    }
                }
                c += 8 + clength + (clength & 1u);
            }
        }
        if (asc_gl(tag)) asc_simple[asc_simple_count++] = index;
        at += 8 + length + (length & 1u);
    }
}
static int asc_corpus(void) {
    asc_members = calloc(4096, sizeof(*asc_members));
    asc_objects = calloc(16000, sizeof(*asc_objects));
    asc_bitmaps = calloc(16000, sizeof(unsigned)); asc_arrays = calloc(4096, sizeof(unsigned));
    asc_simple = calloc(16000, sizeof(unsigned)); asc_others = calloc(4096, sizeof(unsigned));
    if (!asc_members || !asc_objects || !asc_bitmaps || !asc_arrays || !asc_simple || !asc_others) return fail("assets corpus memory");
    for (unsigned i = 0; i < VFC_SIM; ++i) {
        char key[128];
        snprintf(key, sizeof(key), "%s", (const char *)(uintptr_t)(vfc_archive[i] + 8));
        if (!vff_key(key, key)) return fail("assets archive name");
        const vff_file *f = NULL;
        for (unsigned k = 0; k < vff_file_count; ++k) if (!strcmp(vff_files[k].key, key)) f = &vff_files[k];
        es2_vol vol;
        if (!f || es2_vol_open((es2_span){f->data, f->size}, &vol, NULL) != ES2_OK) return fail("assets archive parse");
        for (size_t m = 0; m < vol.member_count && asc_member_count < 4096; ++m) {
            es2_vol_member e;
            if (es2_vol_member_at(&vol, m, &e, NULL) != ES2_OK) return fail("assets member");
            if (e.length < 4 || e.length > 0x100000u) continue;
            const uint32_t tag = get32(e.payload.data);
            const int reg = asc_registered(tag);
            if (e.name.size == 12 && !_strnicmp((const char *)e.name.data, "MECHWPNS.DTS", 12) && !asc_mroot_count) {
                asc_member *x = &asc_members[asc_member_count];
                snprintf(x->path, sizeof(x->path), "%.*s%.*s", (int)e.directory.size, (const char *)e.directory.data,
                         (int)e.name.size, (const char *)e.name.data);
                x->data = e.payload.data; x->length = (uint32_t)e.length; x->tag = tag; x->archive = i; x->cached = 1;
                for (uint32_t at = 0; at + 8 <= x->length && asc_mroot_count < 64;) {
                    const uint32_t size = (get32(x->data + at + 4) + 9u) & ~1u;
                    if (get32(x->data + at) != 0x00140008u || size > x->length - at) break;
                    asc_mroots[asc_mroot_count].member = asc_member_count;
                    asc_mroots[asc_mroot_count].offset = at; asc_mroots[asc_mroot_count++].size = size;
                    at += size;
                }
                ++asc_member_count;
                continue;
            }
            /* Shapes and grids: small members only (their object graphs
             * fill the bump heap fast). */
            if (reg && !asc_gl(tag) && e.length > 0x10000u) continue;
            if (!reg && e.length > 0x10000u) continue;
            asc_member *x = &asc_members[asc_member_count];
            snprintf(x->path, sizeof(x->path), "%.*s%.*s", (int)e.directory.size, (const char *)e.directory.data,
                     (int)e.name.size, (const char *)e.name.data);
            x->data = e.payload.data; x->length = (uint32_t)e.length; x->tag = tag; x->archive = i;
            if (!reg) { if (e.length >= 8) asc_others[asc_other_count++] = asc_member_count; ++asc_member_count; continue; }
            asc_scan_member(asc_member_count++);
        }
    }
    if (asc_array_count < 100 || asc_bitmap_count < 1000 || asc_odd_count < 8 || asc_simple_count < 100 || asc_object_count < 1500 ||
        asc_mroot_count < 50)
        return fail("assets corpus too small");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Setup, patches, state. */
static int asc_ready;
static vfc_state asc_base, asc_pre, asc_orig, asc_repl;
static uint32_t asc_saved_registries, asc_saved_table[8], asc_saved_fallback, asc_saved_context, asc_saved_arena;
static uint32_t asc_registry_count;
static uint32_t asc_entry(uint32_t va, unsigned n, uint32_t a0, uint32_t a1, uint32_t a2) {
    const uint32_t a[3] = {a0, a1, a2};
    return (uint32_t)vfc_entry(va, n, a, vfc_zero);
}
static int asc_patch(int on) {
    if (vfc_patch(on)) return 1;
    if (on) {
        asc_free_saved = get32(mapped + asc_free_slot - 0x400000u);
        put32(mapped + asc_free_slot - 0x400000u, (uint32_t)(uintptr_t)asc_free);
        asc_saved_registries = get32(mapped + 0xa1690); asc_saved_fallback = get32(mapped + 0xa1694);
        for (unsigned i = 0; i < 8; ++i) asc_saved_table[i] = get32(mapped + 0x2c5834 + 4 * i);
        asc_saved_context = get32(mapped + 0x2c5964); asc_saved_arena = get32(mapped + 0x2c5830);
        put32(mapped + 0xa1690, 0);
        /* The registrations of the static initializers: GL 4a2eb8 (9), TS
         * 4a63b0 ([4a6488]), AN 4a687c ([4a68ac]), grid 49aa7c (1). */
        current_function = "assets setup";
        asc_entry(0x47a0b8, 2, 0x4a2eb8, get32(mapped + 0xa2f24), 0);
        asc_entry(0x47a0b8, 2, 0x4a63b0, get32(mapped + 0xa6488), 0);
        asc_entry(0x47a0b8, 2, 0x4a687c, get32(mapped + 0xa68ac), 0);
        asc_entry(0x47a0b8, 2, 0x49aa7c, 1, 0);
        asc_registry_count = get32(mapped + 0xa1690);
        put32(mapped + 0xa1694, 0);
    } else {
        put32(mapped + asc_free_slot - 0x400000u, asc_free_saved);
        put32(mapped + 0xa1690, asc_saved_registries); put32(mapped + 0xa1694, asc_saved_fallback);
        for (unsigned i = 0; i < 8; ++i) put32(mapped + 0x2c5834 + 4 * i, asc_saved_table[i]);
        put32(mapped + 0x2c5964, asc_saved_context); put32(mapped + 0x2c5830, asc_saved_arena);
    }
    return 0;
}
static int asc_setup(void) {
    if (!vfc_ready && vfc_setup()) return 1;
    const BYTE *t = mapped + 0x4967a8 - 0x400000u;
    if (t[0] != 0xff || t[1] != 0x25) return fail("assets _free thunk");
    asc_free_slot = get32(t + 2);
    if (strcmp(vff_slot_name(asc_free_slot), "_free")) return fail("assets _free slot");
    if (asc_patch(1)) return 1;
    if (asc_registry_count != 4) return fail("assets registries");
    if (asc_corpus()) return 1;
    asc_patch(0);
    printf("ASSETS setup: %u members (%u unregistered), %u objects: %u arrays, %u bitmaps, %u GL roots, %u odd positions, "
           "%u MECHWPNS roots for memory streams\n",
           asc_member_count, asc_other_count, asc_object_count, asc_array_count, asc_bitmap_count, asc_simple_count, asc_odd_count,
           asc_mroot_count);
    asc_ready = 1;
    return 0;
}
/* The case state beyond the vfs state. */
typedef struct asc_state {
    BYTE arena[ASC_ARENA];
    uint32_t context, current_arena, heap_base, heap_size;
    asc_note_t notes[16];
    unsigned note_count;
} asc_state;
static asc_state asc_xbase, asc_xpre, asc_xorig, asc_xrepl;
#define ASC_HEAP_COPY (4u << 20)
static BYTE *asc_heap_copy;
static uint32_t asc_heap_base;
static void asc_save(vfc_state *v, asc_state *s, int heap) {
    vfc_save(v);
    memcpy(s->arena, asc_arena, ASC_ARENA);
    s->context = get32(mapped + 0x2c5964); s->current_arena = get32(mapped + 0x2c5830);
    memcpy(s->notes, asc_notes, sizeof(asc_notes)); s->note_count = asc_note_count;
    s->heap_base = asc_heap_base; s->heap_size = vff.bump - asc_heap_base;
    if (heap && s->heap_size <= ASC_HEAP_COPY) memcpy(asc_heap_copy, vff_heap + asc_heap_base, s->heap_size);
}
static void asc_load(const vfc_state *v, const asc_state *s) {
    vfc_load(v);
    memcpy(asc_arena, s->arena, ASC_ARENA);
    put32(mapped + 0x2c5964, s->context); put32(mapped + 0x2c5830, s->current_arena);
    memcpy(asc_notes, s->notes, sizeof(asc_notes)); asc_note_count = s->note_count;
}
/* 0 when equal (the original's heap bytes are in asc_heap_copy). */
static int asc_differs(const vfc_state *va, const asc_state *a, const vfc_state *vb, const asc_state *b, unsigned n) {
    for (unsigned i = 0; i < ASC_ARENA; ++i)
        if (a->arena[i] != b->arena[i]) { fprintf(stderr, "  test arena +%x %02x/%02x\n", i, a->arena[i], b->arena[i]); return 1; }
    if (a->context != b->context || a->current_arena != b->current_arena) {
        fprintf(stderr, "  globals context %08x/%08x arena %08x/%08x\n", a->context, b->context, a->current_arena, b->current_arena); return 1;
    }
    if (a->note_count != b->note_count || memcmp(a->notes, b->notes, sizeof(a->notes))) { fprintf(stderr, "  stub log %u/%u\n", a->note_count, b->note_count); return 1; }
    if (a->heap_size != b->heap_size) { fprintf(stderr, "  heap size %x/%x\n", a->heap_size, b->heap_size); return 1; }
    if (b->heap_size <= ASC_HEAP_COPY)
        for (uint32_t i = 0; i < b->heap_size; ++i)
            if (asc_heap_copy[i] != vff_heap[asc_heap_base + i]) {
                fprintf(stderr, "  heap +%x (block base +%x) %02x/%02x\n", i, asc_heap_base, asc_heap_copy[i], vff_heap[asc_heap_base + i]); return 1;
            }
    return vfc_differs(va, vb, n);
}

/* ------------------------------------------------------------------ */
/* Case generation. */
static unsigned asc_pick(unsigned n) { return random32() % n; }
static void asc_fresh_stream(uint32_t s) {
    BYTE *p = (BYTE *)(uintptr_t)s;
    memset(p, 0, 0x30);
    put32(p, 0x4a0e10); put32(p + 4, 5); put16(p + 8, 0xffff);
}
static void asc_open(uint32_t s, const char *path) {
    asc_fresh_stream(s);
    memset(asc_arena + ASC_PATH, 0, 0x100);
    snprintf((char *)asc_arena + ASC_PATH, 0x100, "%s", path);
    asc_entry(0x4730e8, 2, s, ASC_ADDR(ASC_PATH), 0);
}
static void asc_seek(uint32_t s, uint32_t pos) { asc_entry(0x4733a4, 2, s, pos, 0); }
/* Opens S on member m at offset, with the variations. truncate: a shorter
 * member length is allowed. */
static void asc_position(uint32_t s, unsigned m, uint32_t offset, int truncate) {
    asc_open(s, asc_members[m].path);
    if (offset) asc_seek(s, offset);
    BYTE *p = (BYTE *)(uintptr_t)s;
    if (!asc_pick(4)) {                                   /* another stream moves B */
        unsigned o = asc_pick(asc_member_count);
        for (unsigned tries = 0; tries < 8 && asc_members[o].archive != asc_members[m].archive; ++tries) o = asc_pick(asc_member_count);
        asc_open(ASC_ADDR(ASC_S1), asc_members[o].path);
        asc_entry(0x4735b8, 3, ASC_ADDR(ASC_S1), 1 + asc_pick(64), (uint32_t)(uintptr_t)asc_buffer);
    }
    if (truncate && !asc_pick(5)) {
        const uint32_t length = get32(p + 0x22);
        if (length > offset) put32(p + 0x22, offset + asc_pick(length - offset < 64 ? length - offset : 64));
    }
}
/* S as a memory stream (vtable 4a1788; the fields 47b36c(S, size, buffer)
 * leaves: status 0, start, end = start + size, cursor = start) over `size`
 * bytes of member m from `from`, the cursor moved to `at`. */
static unsigned asc_memory_cases;
static void asc_memory(uint32_t s, unsigned m, uint32_t from, uint32_t size, uint32_t at) {
    BYTE *p = (BYTE *)(uintptr_t)s;
    const uint32_t start = (uint32_t)(uintptr_t)(asc_members[m].data + from);
    memset(p, 0, 0x30);
    put32(p, 0x4a1788); put32(p + 8, start); put32(p + 0xc, start + size); put32(p + 0x10, start + at);
    ++asc_memory_cases;
}
/* asc_position, or (one in four) a memory stream over the whole member with
 * the cursor at offset; truncate: its end cut short as a shorter member. */
static void asc_place(uint32_t s, unsigned m, uint32_t offset, int truncate) {
    if (asc_pick(4)) { asc_position(s, m, offset, truncate); return; }
    uint32_t end = asc_members[m].length;
    if (truncate && !asc_pick(5) && end > offset) end = offset + asc_pick(end - offset < 64 ? end - offset : 64);
    asc_memory(s, m, 0, end, offset);
}
static void asc_arena_choice(void) {
    memset(asc_arena + ASC_AMEM, 0, ASC_ARENA - ASC_AMEM);
    if (asc_pick(3)) { put32(mapped + 0x2c5830, 0); return; }
    BYTE *r = asc_arena + ASC_AREC;
    put32(r, ASC_ADDR(ASC_AMEM)); put32(r + 4, asc_pick(0x3000)); put32(r + 8, asc_pick(0x100));
    put32(mapped + 0x2c5830, ASC_ADDR(ASC_AREC));
}
/* The fake decoder: 496406 reads [[d]-[[d]-4]-0Ch]+20h; its slot 0 is the
 * destructor stub. */
static void asc_decoder(void) {
    put32(asc_arena + ASC_DEC, ASC_ADDR(ASC_DVT));
    put32(asc_arena + ASC_DVT - 4, 0); put32(asc_arena + ASC_DVT - 0xc, ASC_ADDR(ASC_DTI));
    put32(asc_arena + ASC_DTI + 0x20, asc_pick(5));
    put32(asc_arena + ASC_DVT, (uint32_t)(uintptr_t)asc_decoder_dtor);
}
static uint32_t asc_heap_stream;                    /* a stream as a heap block (delete flag) */
static void asc_build(unsigned k, uint32_t a[3], uint32_t r[3]) {
    vfc_buffer_extent = 64;
    asc_load(&asc_base, &asc_xbase);
    vff.fail_read = vff.fail_lseek = vff.fail_close = vff.fail_calloc = 0;
    put32(mapped + 0xa0d18, vfc_manager[0]);
    memset(asc_arena, 0, ASC_AMEM);
    put32(mapped + 0x2c5964, 0x5a5a0000u | asc_pick(0x10000));
    for (unsigned i = 0; i < 3; ++i) r[i] = random32();
    asc_arena_choice();
    asc_heap_stream = 0;
    const uint32_t s = ASC_ADDR(ASC_S0);
    a[0] = s; a[1] = random32(); a[2] = random32();
    if (k == AS_LOAD) {
        const unsigned c = asc_pick(32);
        if (c < 2 && asc_other_count) {                  /* a header across the member end */
            const asc_member *x = &asc_members[asc_others[asc_pick(asc_other_count)]];
            asc_place(s, (unsigned)(x - asc_members), x->length - asc_pick(8), 0);
        } else if (c < 5) {                              /* a root of the weapon shape cache (40fab0) */
            const asc_root *o = &asc_mroots[asc_pick(asc_mroot_count)];
            asc_memory(s, o->member, o->offset, o->size, 0);
        } else {
            const asc_object *o = &asc_objects[c < 12 ? asc_bitmaps[asc_pick(asc_bitmap_count)] : c < 20 ? asc_arrays[asc_pick(asc_array_count)] :
                                               c < 24 ? asc_simple[asc_pick(asc_simple_count)] : asc_pick(asc_object_count)];
            asc_place(s, o->member, o->offset, o->tag == ASC_TAG_ARRAY || o->tag == ASC_TAG_BITMAP);
        }
        if (!asc_pick(24)) put32(asc_arena + ASC_S0 + 4, asc_pick(2) ? 2 : 1);
    } else if (k == AS_BITMAP_NEW || k == AS_BITMAP_READ) {
        if (!asc_pick(8)) { const asc_odd *o = &asc_odds[asc_pick(asc_odd_count)]; asc_place(s, o->member, o->offset, 1); }
        else { const asc_object *o = &asc_objects[asc_bitmaps[asc_pick(asc_bitmap_count)]]; asc_place(s, o->member, o->offset + 8, 1); }
        if (!asc_pick(24)) put32(asc_arena + ASC_S0 + 4, 2);
        if (k == AS_BITMAP_READ) {
            BYTE *b = asc_arena + ASC_OBJ;
            random_bytes(b, 0x20);
            put32(b, 0x4a306c); put32(b + 0xa, asc_pick(0x400)); put16(b + 0x14, (uint16_t)asc_pick(3));
            a[0] = ASC_ADDR(ASC_OBJ); a[1] = s;
        }
    } else if (k == AS_ARRAY_NEW || k == AS_ARRAY_READ) {
        const asc_object *o = &asc_objects[asc_arrays[asc_pick(asc_array_count)]];
        asc_place(s, o->member, o->offset + 8, 1);
        if (!asc_pick(24)) put32(asc_arena + ASC_S0 + 4, 2);
        if (k == AS_ARRAY_READ) {
            BYTE *b = asc_arena + ASC_ARR;
            put32(b, 0x4a32ec); put32(b + 4, asc_pick(40)); put32(b + 8, random32());
            a[0] = ASC_ADDR(ASC_ARR); a[1] = s;
        }
    } else {                                             /* stream_destroy */
        uint32_t t = s;
        const unsigned c = asc_pick(16);
        if (c == 0) {
            a[0] = 0;
        } else {
            if (c < 4) {                                 /* a stream as a heap block (size before it), deleted */
                t = ASC_ADDR(ASC_HS); asc_heap_stream = t;
                put32(asc_arena + ASC_HS - 4, 0x30);
            }
            const unsigned kind = asc_pick(8);
            if (kind < 5) { const asc_member *x = &asc_members[asc_pick(asc_member_count)]; asc_open(t, x->path);
                            if (asc_pick(2)) asc_entry(0x4735b8, 3, t, 1 + asc_pick(32), (uint32_t)(uintptr_t)asc_buffer); }
            else if (kind < 7) asc_open(t, vfc_loose[asc_pick(sizeof(vfc_loose) / sizeof(vfc_loose[0]))]);
            else asc_fresh_stream(t);
            BYTE *p = (BYTE *)(uintptr_t)t;
            if (!asc_pick(4)) {
                asc_decoder();
                if (asc_pick(2)) put32(p + 0x27, ASC_ADDR(ASC_DEC));
                if (asc_pick(2)) put32(p + 0x2b, ASC_ADDR(ASC_DEC));
            }
            if (!asc_pick(8)) put32(p + 4, asc_pick(6));
            a[0] = t;
        }
        a[1] = (asc_heap_stream ? 1u : 0u) | (random32() & 0xfeu) | (asc_pick(2) ? 2u : 0u);
        if (!asc_heap_stream && !asc_pick(2)) a[1] = 2;
        if (asc_heap_stream) a[1] |= 1u;
    }
}

/* ------------------------------------------------------------------ */
static unsigned asc_index(unsigned k) {
    static const unsigned index[AS_HOOKS] = {ES2_HOOK_INDEX_dts_load, ES2_HOOK_INDEX_bitmap_new, ES2_HOOK_INDEX_bitmap_read,
        ES2_HOOK_INDEX_bitmap_array_new, ES2_HOOK_INDEX_bitmap_array_read, ES2_HOOK_INDEX_stream_destroy};
    return index[k];
}
static unsigned asc_group(unsigned index) { for (unsigned k = 0; k < AS_HOOKS; ++k) if (asc_index(k) == index) return k; return AS_HOOKS; }
static uint64_t asc_mask(unsigned k, uint64_t v) {
    return k == AS_BITMAP_NEW || k == AS_ARRAY_NEW || k == AS_DESTROY ? v : v & 0xffffffffu;
}
static uint64_t asc_nonzero;
static int asc_report(unsigned k, unsigned n, const char *what, const uint32_t *a, uint64_t want, uint64_t got) {
    fprintf(stderr, "FAIL: %s case %u (%s) result %016llx/%016llx args %08x %08x %08x\n", function_hook_descriptor(asc_index(k))->name, n,
            what, (unsigned long long)want, (unsigned long long)got, a[0], a[1], a[2]);
    for (unsigned j = 0; j < 3; ++j) {
        const asc_state *x = j == 0 ? &asc_xpre : j == 1 ? &asc_xorig : &asc_xrepl;
        fprintf(stderr, "  S0 %s:", j == 0 ? "pre " : j == 1 ? "orig" : "repl");
        for (unsigned i = 0; i < 0x30; ++i) fprintf(stderr, " %02x", x->arena[ASC_S0 + i]);
        fprintf(stderr, "\n");
    }
    return 1;
}
static int asc_case(unsigned k, unsigned n, void *original, void *replaced, const char *what) {
    const unsigned index = asc_index(k);
    const es2_hook_descriptor *d = function_hook_descriptor(index);
    uint32_t a[3], r[3];
    asc_build(k, a, r);
    vff_log_count = 0; asc_note_count = 0;
    asc_heap_base = vff.bump;
    asc_save(&asc_pre, &asc_xpre, 0);
    const uint64_t want = compare_probe_call(original, index, a, r);
    asc_save(&asc_orig, &asc_xorig, 1);
    asc_load(&asc_pre, &asc_xpre);
    const uint64_t got = replaced ? compare_probe_call(replaced, index, a, r)
                                  : function_hook_extension(index)->replacement_context(a, d->user, r);
    asc_save(&asc_repl, &asc_xrepl, 0);
    if (asc_mask(k, want) != asc_mask(k, got) || asc_differs(&asc_orig, &asc_xorig, &asc_repl, &asc_xrepl, n))
        return asc_report(k, n, what, a, want, got);
    asc_nonzero += (uint32_t)want != 0;
    return 0;
}

/* Real-callee pass: the six hooks and stream_read, stream_tell and
 * stream_close in all 512 original/replaced combinations, through the
 * (possibly hooked) entry of dts_load or bitmap_array_new, against the
 * all-original run. */
static int asc_real(unsigned k) {
    const unsigned index = asc_index(k);
    static const unsigned others[3] = {ES2_HOOK_INDEX_stream_read, ES2_HOOK_INDEX_stream_tell, ES2_HOOK_INDEX_stream_close};
    unsigned total = 0, entries = 0;
    for (unsigned mask = 0; mask < 512; ++mask) for (unsigned n = 0; n < 2; ++n) {
        uint32_t a[3], r[3];
        current_case = mask * 2u + n; current_case_valid = 1;
        asc_build(k, a, r);
        vff_log_count = 0; asc_note_count = 0;
        asc_heap_base = vff.bump;
        asc_save(&asc_pre, &asc_xpre, 0);
        const uint64_t want = compare_probe_call(mapped + assets_rvas[k], index, a, r);
        asc_save(&asc_orig, &asc_xorig, 1);
        asc_load(&asc_pre, &asc_xpre);
        es2_hook hooks[AS_HOOKS + 3]; memset(hooks, 0, sizeof(hooks));
        es2_hook_options o = {0}; o.image = mapped; o.expected_base = 0x400000; o.expected_image_size = 0x2d8000;
        for (unsigned j = 0; j < AS_HOOKS + 3; ++j) {
            const unsigned x = j < AS_HOOKS ? asc_index(j) : others[j - AS_HOOKS];
            o.mode = mask >> j & 1 ? ES2_HOOK_REPLACED : ES2_HOOK_ORIGINAL;
            if (!weapons_install_hook(hooks + j, function_hook_descriptor(x), &o, function_hook_extension(x))) return fail("assets real install");
        }
        const uint64_t got = compare_probe_call(mapped + assets_rvas[k], index, a, r);
        for (unsigned j = 0; j < AS_HOOKS + 3; ++j) {
            entries += (unsigned)hooks[j].calls;
            if (!es2_hook_uninstall(hooks + j)) return fail("assets real uninstall");
        }
        asc_save(&asc_repl, &asc_xrepl, 0);
        if (asc_mask(k, want) != asc_mask(k, got) || asc_differs(&asc_orig, &asc_xorig, &asc_repl, &asc_xrepl, n)) {
            fprintf(stderr, "  (real-callee, mask %03x)\n", mask);
            return asc_report(k, n, "real-callee", a, want, got);
        }
        ++total;
    }
    current_case_valid = 0;
    printf("PASS: %s %u real-callee cases (6 hooks and stream_read/stream_tell/stream_close in all 512 original/replaced "
           "combinations, %u hook entries)\n", function_hook_descriptor(index)->name, total, entries);
    return 0;
}

/* Installed replaced hook against the original, both from a 31/a7 stack,
 * fast and preserving dispatch. */
static int asc_poison(unsigned k) {
    const unsigned index = asc_index(k);
    const es2_hook_descriptor *d = function_hook_descriptor(index);
    unsigned total = 0;
    for (unsigned fast = 0; fast < 2; ++fast) for (unsigned n = 0; n < 128; ++n) {
        uint32_t a[3], r[3], want[2], got[2], pattern = n & 1 ? 0xa7a7a7a7u : 0x31313131u;
        current_case = n; current_case_valid = 1;
        asc_build(k, a, r);
        vff_log_count = 0; asc_note_count = 0;
        asc_heap_base = vff.bump;
        asc_save(&asc_pre, &asc_xpre, 0);
        vfc_poison_call(mapped + assets_rvas[k], a, pattern, want);
        asc_save(&asc_orig, &asc_xorig, 1);
        asc_load(&asc_pre, &asc_xpre);
        es2_hook h = {0}; es2_hook_options o = {0};
        o.image = mapped; o.expected_base = 0x400000; o.expected_image_size = 0x2d8000; o.mode = ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);
        if (!weapons_install_hook(&h, d, &o, function_hook_extension(index))) return fail("assets poison install");
        vfc_poison_call(mapped + assets_rvas[k], a, pattern, got);
        const uint64_t calls = h.calls;
        if (!es2_hook_uninstall(&h)) return fail("assets poison uninstall");
        es2_hook_set_fast_path(TRUE);
        asc_save(&asc_repl, &asc_xrepl, 0);
        const uint64_t w = (uint64_t)want[1] << 32 | want[0], g = (uint64_t)got[1] << 32 | got[0];
        /* dts_load re-enters itself through an array's children. */
        if (asc_mask(k, w) != asc_mask(k, g) || !calls || (k != AS_LOAD && calls != 1) ||
            asc_differs(&asc_orig, &asc_xorig, &asc_repl, &asc_xrepl, n)) {
            fprintf(stderr, "  (poison, %s, calls %llu)\n", fast ? "fast" : "preserving", (unsigned long long)calls);
            return asc_report(k, n, "poison", a, w, g);
        }
        ++total;
    }
    current_case_valid = 0;
    printf("PASS: %s %u installed 31/a7 poison cases, fast/preserving dispatch\n", d->name, total);
    return 0;
}

/* Compare mode, the hook alone: the original's result and state kept, one
 * call per case; exact: 0 mismatches; skewed (a byte of the stream's clone
 * flipped after the replacement): one mismatch per compared call. */
static es2_hook_replacement_context asc_exact;
static unsigned asc_skew_arg;
static uint64_t __cdecl asc_skew(const uint32_t *a, void *u, const uint32_t *r) {
    uint64_t result = asc_exact(a, u, r);
    if (!assets_last_skip && a[asc_skew_arg]) ((BYTE *)(uintptr_t)a[asc_skew_arg])[0x16] ^= 0x40u;
    return result;
}
static int asc_compare(unsigned k) {
    const unsigned index = asc_index(k);
    const es2_hook_descriptor *d = function_hook_descriptor(index);
    const es2_hook_extension *x = function_hook_extension(index);
    es2_hook_extension skewed = *x;
    asc_exact = x->replacement_context; skewed.replacement_context = asc_skew;
    asc_skew_arg = k == AS_BITMAP_READ || k == AS_ARRAY_READ ? 1u : 0u;
    uint64_t skipped[2] = {0, 0}, mismatches[2] = {0, 0}, compared[2] = {0, 0};
    for (unsigned skew = 0; skew < 2; ++skew) for (unsigned n = 0; n < 1024; ++n) {
        uint32_t a[3], r[3];
        current_case = n; current_case_valid = 1;
        asc_build(k, a, r);
        vff_log_count = 0; asc_note_count = 0;
        asc_heap_base = vff.bump;
        asc_save(&asc_pre, &asc_xpre, 0);
        const uint64_t want = compare_probe_call(mapped + assets_rvas[k], index, a, r);
        asc_save(&asc_orig, &asc_xorig, 1);
        asc_load(&asc_pre, &asc_xpre);
        es2_hook h = {0}; es2_hook_options o = {0};
        o.image = mapped; o.expected_base = 0x400000; o.expected_image_size = 0x2d8000; o.mode = ES2_HOOK_COMPARE;
        if (!weapons_install_hook(&h, d, &o, skew ? &skewed : x)) return fail("assets compare install");
        const uint64_t got = compare_probe_call(mapped + assets_rvas[k], index, a, r);
        if (!es2_hook_uninstall(&h)) return fail("assets compare uninstall");
        asc_save(&asc_repl, &asc_xrepl, 0);
        /* dts_load: the children of an array enter it again in the
         * original phase (bypassed). */
        const int bad = got != want || !h.calls || h.bypassed != h.calls - 1 || (k != AS_LOAD && h.calls != 1) ||
                        h.mismatches + h.skipped > 1 ||
                        (skew ? h.mismatches + h.skipped != 1 : h.mismatches != 0) ||
                        asc_differs(&asc_orig, &asc_xorig, &asc_repl, &asc_xrepl, n);
        if (bad) {
            fprintf(stderr, "  (compare, %s) calls %llu mismatches %llu skipped %llu bypassed %llu\n", skew ? "skewed" : "exact",
                    (unsigned long long)h.calls, (unsigned long long)h.mismatches, (unsigned long long)h.skipped, (unsigned long long)h.bypassed);
            return asc_report(k, n, "compare", a, want, got);
        }
        skipped[skew] += h.skipped; mismatches[skew] += h.mismatches; compared[skew] += h.calls - h.skipped;
    }
    current_case_valid = 0;
    if (!compared[0]) return fail("assets compare: no compared call");
    printf("PASS: %s compare mode: 1024 exact cases (%llu compared, 0 mismatches, %llu skipped), 1024 skewed cases (%llu mismatches, "
           "%llu skipped), original result and state kept\n", d->name, (unsigned long long)compared[0], (unsigned long long)skipped[0],
           (unsigned long long)mismatches[1], (unsigned long long)skipped[1]);
    return 0;
}

/* formats/ against the original loader, over the whole corpus (no hook
 * installed): every bitmap array loaded by the original 47a144 equals
 * es2_dba_parse_prefix / es2_dba_image_at (count, every child's fields,
 * pixel and colour bytes); every shape root leaves the stream exactly at
 * the end of its encoded object in es2_dts_walk (header, body, pad). */
typedef struct asc_roots { uint32_t end[64]; unsigned count; size_t base; } asc_roots;
static es2_status asc_root_visit(const es2_dts_object *o, void *user) {
    asc_roots *r = user;
    if (!o->depth && r->count < 64) r->end[r->count++] = (uint32_t)(o->offset + o->encoded.size);
    return ES2_OK;
}
static int asc_formats_pass(void) {
    const uint32_t s = ASC_ADDR(ASC_S0);
    unsigned arrays = 0, images = 0, shapes = 0, roots = 0;
    for (unsigned m = 0; m < asc_member_count; ++m) {
        const asc_member *x = &asc_members[m];
        if (x->tag == ASC_TAG_ARRAY) {
            es2_dba dba;
            if (es2_dba_parse_prefix((es2_span){x->data, x->length}, &dba, NULL) != ES2_OK) {
                fprintf(stderr, "FAIL: formats/dbm refuses %s\n", x->path); return 1;
            }
            asc_load(&asc_base, &asc_xbase); put32(mapped + 0xa0d18, vfc_manager[0]); put32(mapped + 0x2c5830, 0);
            asc_open(s, x->path);
            const uint32_t a = asc_entry(0x47a144, 2, s, 0, 0);
            if (!a || get32((BYTE *)(uintptr_t)a) != 0x4a32ec || get32((BYTE *)(uintptr_t)a + 4) != dba.count) {
                fprintf(stderr, "FAIL: dts_load formats/dbm %s: array %08x count %u\n", x->path, a, dba.count); return 1;
            }
            const BYTE *list = (const BYTE *)(uintptr_t)get32((BYTE *)(uintptr_t)a + 8);
            for (uint32_t i = 0; i < dba.count; ++i) {
                es2_dbm image;
                const BYTE *b = (const BYTE *)(uintptr_t)get32(list + 4 * i);
                if (es2_dba_image_at(&dba, i, &image, NULL) != ES2_OK || !b || get32(b) != 0x4a306c ||
                    get16(b + 4) != image.height || get16(b + 6) != image.width || b[8] != image.bits_per_pixel ||
                    (b[9] & 0x1f) != (image.flags & 0x1f) || get16(b + 0x12) != image.compression ||
                    get32(b + 0xa) != image.pixels.size || get16(b + 0x14) != image.colour_count ||
                    (image.pixels.size && memcmp((const void *)(uintptr_t)get32(b + 0xe), image.pixels.data, image.pixels.size)) ||
                    (image.colours.size && memcmp((const void *)(uintptr_t)get32(b + 0x16), image.colours.data, image.colours.size))) {
                    fprintf(stderr, "FAIL: dts_load formats/dbm %s: image %u differs\n", x->path, i); return 1;
                }
                ++images;
            }
            ++arrays;
        } else if ((x->tag == 0x00140008u || x->tag == 0x001e0003u) && !x->cached) {
            asc_roots r = {{0}, 0, 0};
            es2_dts_stats stats;
            if (es2_dts_walk((es2_span){x->data, x->length}, ES2_DTS_DEPTH_LIMIT, asc_root_visit, &r, &stats, NULL) != ES2_OK || !r.count) {
                fprintf(stderr, "FAIL: formats/dts refuses %s\n", x->path); return 1;
            }
            asc_load(&asc_base, &asc_xbase); put32(mapped + 0xa0d18, vfc_manager[0]); put32(mapped + 0x2c5830, 0);
            asc_open(s, x->path);
            for (unsigned i = 0; i < r.count; ++i) {
                if (!asc_entry(0x47a144, 2, s, 0, 0) || get32(asc_arena + ASC_S0 + 0x16) != r.end[i]) {
                    fprintf(stderr, "FAIL: dts_load formats/dts %s: root %u ends at %x, walk %x\n", x->path, i,
                            get32(asc_arena + ASC_S0 + 0x16), r.end[i]); return 1;
                }
                ++roots;
            }
            ++shapes;
        }
    }
    printf("PASS: dts_load formats cross-check: %u bitmap arrays (%u images) equal to formats/dbm, %u shape members (%u roots) "
           "consumed as formats/dts frames them, original loader\n", arrays, images, shapes, roots);
    return 0;
}

/* dts_load's PASS reasons through the installed replaced hook, 64 cases per
 * reason: hk_dts_load_bypassed equals the reason's pass counter (nested loads
 * of the original included) and the other four stay 0. The stream class (S's
 * vtable a copy of 4a0e10's in the test arena) and the loose stream (a file of
 * the file system, status preset: nothing read) are deterministic and compared
 * with the original run; the unserved state (the archive's buffered stream B in
 * an error state), the unregistered tag and the fallback registry ([4a1694] 1)
 * reach the original's drain over its unset frame dword (stack garbage), so
 * only the counters are checked. */
enum { ASC_FVT = 0x2a0 };                                /* the vtable copy (40h bytes) */
static int asc_reasons(void) {
    static const char *const names[ASSETS_PASS_COUNT] = {"stream", "loose", "unserved", "tag", "fallback"};
    const unsigned index = ES2_HOOK_INDEX_dts_load;
    const es2_hook_descriptor *d = function_hook_descriptor(index);
    uint64_t bypassed[ASSETS_PASS_COUNT] = {0};
    unsigned compared = 0;
    for (unsigned reason = 0; reason < ASSETS_PASS_COUNT; ++reason) for (unsigned n = 0; n < 64; ++n) {
        uint32_t a[3], r[3];
        current_case = n; current_case_valid = 1;
        asc_build(AS_LOAD, a, r);
        const uint32_t s = ASC_ADDR(ASC_S0);
        BYTE *p = asc_arena + ASC_S0;
        int exact = 0;
        if (reason == 0 || reason == 2) {
            const asc_object *o = &asc_objects[asc_pick(asc_object_count)];
            asc_position(s, o->member, o->offset, 0);
            if (reason == 0) { memcpy(asc_arena + ASC_FVT, mapped + 0xa0e10, 0x40); put32(p, ASC_ADDR(ASC_FVT)); exact = 1; }
            else put32((BYTE *)(uintptr_t)get32(p + 0x0a) + 4, 1);
        } else if (reason == 1) {
            for (unsigned tries = 0; tries < 16 && (tries == 0 || get16(p + 8) == 0xffff); ++tries)
                asc_open(s, vfc_loose[asc_pick(sizeof(vfc_loose) / sizeof(vfc_loose[0]))]);
            if (get16(p + 8) == 0xffff) return fail("assets reasons: no loose file opened");
            put32(p + 4, 1 + asc_pick(2));
            exact = 1;
        } else {
            asc_position(s, asc_others[asc_pick(asc_other_count)], 0, 0);
            if (reason == 4) put32(mapped + 0xa1694, 1);
        }
        a[0] = s;
        vff_log_count = 0; asc_note_count = 0;
        asc_heap_base = vff.bump;
        asc_save(&asc_pre, &asc_xpre, 0);
        uint64_t want = 0;
        if (exact) {
            want = compare_probe_call(mapped + assets_rvas[AS_LOAD], index, a, r);
            asc_save(&asc_orig, &asc_xorig, 1);
            asc_load(&asc_pre, &asc_xpre);
        }
        for (unsigned i = 0; i < ASSETS_PASS_COUNT; ++i) assets_load_counters[ASSETS_PASS_FIRST + i].count = 0;
        es2_hook h = {0}; es2_hook_options o = {0};
        o.image = mapped; o.expected_base = 0x400000; o.expected_image_size = 0x2d8000; o.mode = ES2_HOOK_REPLACED;
        if (!weapons_install_hook(&h, d, &o, function_hook_extension(index))) return fail("assets reasons install");
        const uint64_t got = compare_probe_call(mapped + assets_rvas[AS_LOAD], index, a, r);
        if (!es2_hook_uninstall(&h)) return fail("assets reasons uninstall");
        put32(mapped + 0xa1694, 0);
        int bad = !h.bypassed || assets_load_counters[ASSETS_PASS_FIRST + reason].count != h.bypassed;
        for (unsigned i = 0; i < ASSETS_PASS_COUNT; ++i)
            if (i != reason && assets_load_counters[ASSETS_PASS_FIRST + i].count) bad = 1;
        if (exact) {
            asc_save(&asc_repl, &asc_xrepl, 0);
            if ((uint32_t)want != (uint32_t)got || asc_differs(&asc_orig, &asc_xorig, &asc_repl, &asc_xrepl, n)) bad = 1;
            else ++compared;
        }
        if (bad) {
            fprintf(stderr, "  (reasons, %s) calls %llu bypassed %llu pass", names[reason], (unsigned long long)h.calls,
                    (unsigned long long)h.bypassed);
            for (unsigned i = 0; i < ASSETS_PASS_COUNT; ++i)
                fprintf(stderr, " %s=%llu", names[i], (unsigned long long)assets_load_counters[ASSETS_PASS_FIRST + i].count);
            fprintf(stderr, "\n");
            return asc_report(AS_LOAD, n, "reasons", a, want, got);
        }
        bypassed[reason] += h.bypassed;
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < ASSETS_PASS_COUNT; ++i) assets_load_counters[ASSETS_PASS_FIRST + i].count = 0;
    printf("PASS: dts_load pass reasons, 64 installed cases each: bypassed = hk_dts_load_pass_<reason> (stream %llu, loose %llu, "
           "unserved %llu, tag %llu, fallback %llu), the others 0; %u stream/loose cases equal to the original\n",
           (unsigned long long)bypassed[0], (unsigned long long)bypassed[1], (unsigned long long)bypassed[2],
           (unsigned long long)bypassed[3], (unsigned long long)bypassed[4], compared);
    return 0;
}

/* Every root of the weapon shape cache as 40fab0 loads it (a memory stream over
 * its block, 474c10: dts_load(S, 0)) through the installed replaced hook
 * against the original: equal, no PASS (bypassed 0), and the memory stream's
 * tells and reads per load. */
static int asc_cache_pass(void) {
    const unsigned index = ES2_HOOK_INDEX_dts_load;
    const es2_hook_descriptor *d = function_hook_descriptor(index);
    uint64_t calls = 0, tells = 0, reads = 0, low = UINT64_MAX, high = 0;
    for (unsigned n = 0; n < asc_mroot_count; ++n) {
        uint32_t a[3], r[3];
        current_case = n; current_case_valid = 1;
        asc_build(AS_LOAD, a, r);
        put32(mapped + 0x2c5830, 0);
        const asc_root *o = &asc_mroots[n];
        asc_memory(ASC_ADDR(ASC_S0), o->member, o->offset, o->size, 0);
        a[0] = ASC_ADDR(ASC_S0); a[1] = 0;
        vff_log_count = 0; asc_note_count = 0;
        asc_heap_base = vff.bump;
        asc_save(&asc_pre, &asc_xpre, 0);
        const uint64_t want = compare_probe_call(mapped + assets_rvas[AS_LOAD], index, a, r);
        asc_save(&asc_orig, &asc_xorig, 1);
        asc_load(&asc_pre, &asc_xpre);
        const uint64_t t0 = assets_load_counters[4].count, r0 = assets_load_counters[5].count;
        es2_hook h = {0}; es2_hook_options opt = {0};
        opt.image = mapped; opt.expected_base = 0x400000; opt.expected_image_size = 0x2d8000; opt.mode = ES2_HOOK_REPLACED;
        if (!weapons_install_hook(&h, d, &opt, function_hook_extension(index))) return fail("assets cache install");
        const uint64_t got = compare_probe_call(mapped + assets_rvas[AS_LOAD], index, a, r);
        if (!es2_hook_uninstall(&h)) return fail("assets cache uninstall");
        asc_save(&asc_repl, &asc_xrepl, 0);
        if ((uint32_t)want != (uint32_t)got || !(uint32_t)got || h.bypassed || h.skipped ||
            asc_differs(&asc_orig, &asc_xorig, &asc_repl, &asc_xrepl, n)) {
            fprintf(stderr, "  (cache root %u at %x, %u bytes) calls %llu bypassed %llu\n", n, o->offset, o->size,
                    (unsigned long long)h.calls, (unsigned long long)h.bypassed);
            return asc_report(AS_LOAD, n, "cache", a, want, got);
        }
        calls += h.calls; tells += assets_load_counters[4].count - t0; reads += assets_load_counters[5].count - r0;
        if (h.calls < low) low = h.calls;
        if (h.calls > high) high = h.calls;
    }
    current_case_valid = 0;
    printf("PASS: dts_load weapon shape cache: %u MECHWPNS roots from memory streams equal to the original, bypassed 0; "
           "%llu loads (%llu..%llu per root), %llu memory tells (47b400), %llu memory reads (47b4b4)\n", asc_mroot_count,
           (unsigned long long)calls, (unsigned long long)low, (unsigned long long)high, (unsigned long long)tells,
           (unsigned long long)reads);
    return 0;
}

static int assets_cases(unsigned index, unsigned limit) {
    if (!asc_ready && asc_setup()) return 1;
    current_function = function_hook_descriptor(index)->name;
    const unsigned k = asc_group(index);
    if (k == AS_HOOKS) return fail("assets index");
    if (!asc_heap_copy && !(asc_heap_copy = malloc(ASC_HEAP_COPY))) return fail("assets heap copy");
    if (asc_patch(1)) return 1;
    static int based;
    if (!based) { memset(asc_arena, 0, ASC_ARENA); asc_heap_base = vff.bump; asc_save(&asc_base, &asc_xbase, 0); based = 1; }
    asc_load(&asc_base, &asc_xbase);
    int status = 0;
    /* Each case loads a whole object (up to 1 MiB of member data): the quick
     * tier runs 8,000 direct cases (all inside the coverage window), the
     * full tier 40,000. */
    const unsigned cases = limit ? limit : quick_tier ? 8000u : 40000u;
    if (!limit) coverage_enable(1);
    asc_memory_cases = 0;
    for (unsigned n = 0; n < cases && !status; ++n) {
        current_case = n; current_case_valid = 1;
        if (!limit && n == 16384) coverage_enable(0);
        status = limit ? asc_case(k, n, rng_hooks[index].trampoline, mapped + assets_rvas[k], "installed")
                       : asc_case(k, n, mapped + assets_rvas[k], NULL, "direct");
    }
    if (!limit) coverage_enable(0);
    current_case_valid = 0;
    if (!status) printf("PASS: %-20s %u %s cases (%llu nonzero EAX); EAX/EDX, test arena, heap bytes, buffered streams, file system, counters, logs\n",
                        function_hook_descriptor(index)->name, cases, limit ? "installed" : "direct", (unsigned long long)asc_nonzero);
    if (!status && k != AS_DESTROY)
        printf("ASSETS %s: %u of the %u cases on a memory stream (4a1788)\n", function_hook_descriptor(index)->name, asc_memory_cases, cases);
    asc_nonzero = 0;
    if (!limit && !status && k == AS_LOAD) status = asc_formats_pass();
    if (!limit && !status && (k == AS_LOAD || k == AS_ARRAY_NEW)) status = asc_real(k);
    if (!limit && !status) status = asc_poison(k);
    if (!limit && !status) status = asc_compare(k);
    if (!limit && !status && k == AS_LOAD) status = asc_reasons();
    if (!limit && !status && k == AS_LOAD) status = asc_cache_pass();
    asc_load(&asc_base, &asc_xbase);
    if (asc_patch(0)) return 1;
    return status;
}
