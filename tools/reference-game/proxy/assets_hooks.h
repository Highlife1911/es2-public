#ifndef ES2_ASSETS_HOOKS_H
#define ES2_ASSETS_HOOKS_H
#include "guest_native.h"
/* Group assets: DBSIM's persistent-object loader and the bitmap
 * classes as own code (docs/reverse-engineering/asset_loading.md):
 * dts_load 47a144, bitmap_new 481cd4, bitmap_read 481d8c, bitmap_array_new
 * 482d88, bitmap_array_read 482e7c and stream_destroy 472f64
 * (core/src/asset_load.c over the stream model core/src/vfs_stream.c
 * and the archives the vfs adapter identifies, vfs_hooks.h).
 *
 * Replaced mode: member streams of an identified archive are read from the
 * archive file (es2_vfs_stream_fetch); every other stream operation, the
 * factories of the registry entries, the class reads through the vtable,
 * the nested loads of an array, new 4774f0 (when the arena [6c5830] has no
 * room), the stream close 47307c, the decoders' calls and the delete
 * 477548 are guest calls (vfs_guest_call), so nested hooks keep their
 * modes. Memory streams (vtable 4a1788, the weapon shape cache) are
 * read and told as own code. dts_load passes a stream class it does not
 * model, a VOL-aware stream with a handle, a state the models do not serve,
 * and a header whose tag the registry lacks while the stream stays readable
 * (the original's drain then reads an unset frame dword) or the fallback
 * registry [4a1694] is in use, to the original through the trampoline
 * (`bypassed`, and one hk_dts_load_pass_<reason>; expected 0 in the
 * suites). Each callback has its own guest
 * scratch on its stack (the loader frame, the one-byte locals); the
 * original's frame layout below the caller's ESP is not reproduced.
 *
 * Compare mode: the replacement is pure. dts_load, the two factories and
 * bitmap_array_read describe the objects the original must have built
 * (es2_asset_expect: every field, the pixel and colour bytes from the
 * archive file, the arrays' children in order) and the comparators walk
 * the original's objects from its result (or from the array's pointer) and
 * compare them byte for byte; allocation addresses are not compared.
 * bitmap_read writes its fields into the clone of the object (the pointers
 * +0Eh/+16h excluded) and the original's pixel and colour bytes are
 * compared with the file's. The stream S is a span, its buffered stream B a
 * region (ARG_INDIRECT S+0Ah, 21h bytes, not compared: the windows differ
 * by design), the construction counter [[6cbee4]] a region where the hook
 * changes it. A class the core does not model (shapes, palettes, fonts), a
 * stream it does not serve, a short read in a factory's object, a decoder
 * object, the delete flag or an owned handle is SKIP (`skipped`).
 *
 * Results (pair callbacks): EAX as the original; EDX = FS:0 at entry for
 * the two factories (the exception record's saved head), the base vtable
 * 4a16e8 (or the delete's EDX) for stream_destroy, 0 for the others (no
 * direct call site reads EDX or ECX after these calls, asset_loading.md).
 *
 * assets_record_hook (called by weapons_capture_entry) records each
 * installed hook's runtime: the
 * adapters read its mode and count skipped/bypassed. No shim, no game
 * instruction embedded. */
#include "es2/core/asset_load.h"
#include "machine_call.h"
#include "adapter_host.h"

enum { AS_LOAD, AS_BITMAP_NEW, AS_BITMAP_READ, AS_ARRAY_NEW, AS_ARRAY_READ, AS_DESTROY, AS_HOOKS };
static const uint32_t assets_rvas[AS_HOOKS] = {0x7a144u, 0x81cd4u, 0x81d8cu, 0x82d88u, 0x82e7cu, 0x72f64u};
static es2_hook *assets_runtime[AS_HOOKS];
static es2_hook *assets_installed(unsigned k, void *image) { return adapter_hook_installed(assets_runtime[k], image, assets_rvas[k]); }
static es2_asset_expect assets_expect;
static int assets_last_skip;
static void assets_observe(void *context, uint32_t function) { weapons_graph_observe(context, function, 0); }
static es2_asset_host assets_host(unsigned k, unsigned index, void *image, BYTE *scratch) {
    es2_hook *h = assets_installed(k, image);
    const int commit = !adapter_hook_compares(h);
    memset(scratch, 0, ES2_ASSET_SCRATCH);
    es2_asset_host host = {{&math_native_memory, (uint32_t)(uintptr_t)image, commit, weapons_graph_context(index), vfs_archive,
                            vfs_guest_call, vfs_loose, assets_observe, vfs_ensure},
                           (uint32_t)(uintptr_t)scratch, commit ? NULL : &assets_expect};
    assets_last_skip = 0;
    return host;
}
static uint64_t assets_finish(unsigned k, void *image, es2_vfs_outcome o, uint32_t eax, uint32_t edx,
                              const uint32_t *a, unsigned n, const uint32_t *r) {
    if (o == ES2_VFS_DONE) return (uint64_t)edx << 32 | eax;
    es2_hook *h = assets_installed(k, image);
    if (o == ES2_VFS_SKIP) { adapter_hook_skipped(h); assets_last_skip = 1; return 0; }
    /* PASS: the original, with the entry registers. */
    const uint32_t target = adapter_hook_pass(h, image, assets_rvas[k]);
    return adapter_machine_pair(target, a, n, r[0], r[1], r[2]);
}
static uint64_t __cdecl assets_dts_load(const uint32_t *a, void *u, const uint32_t *r) {
    BYTE scratch[ES2_ASSET_SCRATCH];
    es2_asset_host h = assets_host(AS_LOAD, ES2_HOOK_INDEX_dts_load, u, scratch); es2_asset_result o = {0, 0};
    es2_vfs_outcome k = es2_asset_dts_load(&h, a[0], a[1], &o);
    return assets_finish(AS_LOAD, u, k, o.eax, o.edx, a, 2, r);
}
static uint64_t __cdecl assets_bitmap_new(const uint32_t *a, void *u, const uint32_t *r) {
    const uint32_t fs0 = vfs_fs0();
    BYTE scratch[ES2_ASSET_SCRATCH];
    es2_asset_host h = assets_host(AS_BITMAP_NEW, ES2_HOOK_INDEX_bitmap_new, u, scratch); es2_asset_result o = {0, 0};
    es2_vfs_outcome k = es2_asset_bitmap_new(&h, a[0], a[1], fs0, &o);
    return assets_finish(AS_BITMAP_NEW, u, k, o.eax, o.edx, a, 2, r);
}
static uint64_t __cdecl assets_bitmap_read(const uint32_t *a, void *u, const uint32_t *r) {
    BYTE scratch[ES2_ASSET_SCRATCH];
    es2_asset_host h = assets_host(AS_BITMAP_READ, ES2_HOOK_INDEX_bitmap_read, u, scratch); es2_asset_result o = {0, 0};
    es2_vfs_outcome k = es2_asset_bitmap_read(&h, a[0], a[1], r[2], &o);
    return assets_finish(AS_BITMAP_READ, u, k, o.eax, o.edx, a, 3, r);
}
static uint64_t __cdecl assets_bitmap_array_new(const uint32_t *a, void *u, const uint32_t *r) {
    const uint32_t fs0 = vfs_fs0();
    BYTE scratch[ES2_ASSET_SCRATCH];
    es2_asset_host h = assets_host(AS_ARRAY_NEW, ES2_HOOK_INDEX_bitmap_array_new, u, scratch); es2_asset_result o = {0, 0};
    es2_vfs_outcome k = es2_asset_bitmap_array_new(&h, a[0], a[1], fs0, &o);
    return assets_finish(AS_ARRAY_NEW, u, k, o.eax, o.edx, a, 2, r);
}
static uint64_t __cdecl assets_bitmap_array_read(const uint32_t *a, void *u, const uint32_t *r) {
    BYTE scratch[ES2_ASSET_SCRATCH];
    es2_asset_host h = assets_host(AS_ARRAY_READ, ES2_HOOK_INDEX_bitmap_array_read, u, scratch); es2_asset_result o = {0, 0};
    es2_vfs_outcome k = es2_asset_bitmap_array_read(&h, a[0], a[1], &o);
    return assets_finish(AS_ARRAY_READ, u, k, o.eax, o.edx, a, 3, r);
}
static uint64_t __cdecl assets_stream_destroy(const uint32_t *a, void *u, const uint32_t *r) {
    const uint32_t fs0 = vfs_fs0();
    BYTE scratch[ES2_ASSET_SCRATCH];
    es2_asset_host h = assets_host(AS_DESTROY, ES2_HOOK_INDEX_stream_destroy, u, scratch); es2_asset_result o = {0, 0};
    es2_vfs_outcome k = es2_asset_stream_destroy(&h, a[0], a[1], fs0, &o);
    return assets_finish(AS_DESTROY, u, k, o.eax, o.edx, a, 2, r);
}

/* ------------------------------------------------------------------ */
/* Comparators: the original's objects against the replacement's
 * description (assets_expect). */
static int assets_readable(uint32_t p, uint32_t n) {
    const uintptr_t end = (uintptr_t)p + n;
    if (!p || !n || end < (uintptr_t)p || !es2_guest_probe(p, n)) return 0;
    return adapter_committed(p, n);
}
static uint32_t assets_get32(const BYTE *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t assets_r32(uint32_t a) { return math_native_read32(NULL, a); }
static uint16_t assets_r16(uint32_t a) { const BYTE *p = es2_guest_span(a, 2u); return (uint16_t)(p[0] | (uint16_t)(p[1] << 8)); }
static int assets_data(uint32_t p, const uint8_t *want, uint32_t n) {
    return n ? want && assets_readable(p, n) && !memcmp(es2_guest_span(p, n), want, n) : p == 0;
}
/* The fields and data of the bitmap at o (not its vtable). */
static int assets_bitmap_matches(uint32_t o, const es2_asset_object *e) {
    const BYTE *b = es2_guest_span(o, 10u);
    if (assets_r16(o + 4u) != e->height || assets_r16(o + 6u) != e->width || b[8] != e->depth || (b[9] & 0x1fu) != e->flags ||
        assets_r32(o + 0x0au) != e->pixel_size || assets_r16(o + 0x12u) != e->compression || assets_r16(o + 0x14u) != e->colours)
        return 0;
    return assets_data(assets_r32(o + 0x0eu), e->pixels, e->pixel_size) &&
           assets_data(assets_r32(o + 0x16u), e->colour_bytes, (uint32_t)(int16_t)e->colours << 2);
}
static int assets_verify(uint32_t image, uint32_t o, unsigned i, unsigned *next);
static int assets_children(uint32_t image, uint32_t list, unsigned i, unsigned *next) {
    const es2_asset_object *e = &assets_expect.objects[i];
    unsigned k = i + 1u;
    if (e->count && !assets_readable(list, 4u * e->count)) return 0;
    for (uint32_t c = 0; c < e->count; ++c) {
        unsigned n;
        if (!assets_verify(image, assets_r32(list + 4u * c), k, &n)) return 0;
        k = n;
    }
    *next = k;
    return k == e->end;
}
static int assets_verify(uint32_t image, uint32_t o, unsigned i, unsigned *next) {
    if (i >= assets_expect.count) return 0;
    const es2_asset_object *e = &assets_expect.objects[i];
    if (e->kind == ES2_ASSET_NONE) { *next = i + 1u; return o == 0; }
    if (e->kind == ES2_ASSET_BITMAP) {
        *next = i + 1u;
        return assets_readable(o, ES2_ASSET_BITMAP_SIZE) && assets_r32(o) == image + 0xa306cu && assets_bitmap_matches(o, e);
    }
    return assets_readable(o, ES2_ASSET_ARRAY_SIZE) && assets_r32(o) == image + 0xa32ecu && assets_r32(o + 4u) == e->count &&
           assets_children(image, assets_r32(o + 8u), i, next);
}
static int assets_span_equal(const es2_hook_comparison *v, unsigned i) {
    return !memcmp(v->original[i], v->replacement[i], v->sizes[i]);
}
/* dts_load (span S, regions B and the counter): the object graph from EAX. */
static BOOL __cdecl assets_equal_load(const es2_hook_comparison *v, void *u) {
    if (assets_last_skip) return TRUE;
    if (!assets_span_equal(v, 0) || !assets_span_equal(v, 2)) return FALSE;
    unsigned next;
    return assets_verify((uint32_t)(uintptr_t)u, v->original_result, 0, &next) && next == assets_expect.count;
}
/* The factories: as dts_load, and EDX (FS:0 at entry). */
static BOOL __cdecl assets_equal_new(const es2_hook_comparison *v, void *u) {
    return assets_equal_load(v, u) && (assets_last_skip || v->original_edx == v->replacement_edx);
}
/* bitmap_read (spans B and S, region B of S): the clone without its two
 * pointers, the original's pixel and colour bytes, EAX. */
static BOOL __cdecl assets_equal_read(const es2_hook_comparison *v, void *u) {
    (void)u;
    if (assets_last_skip) return TRUE;
    const BYTE *x = v->original[0], *y = v->replacement[0];
    if (memcmp(x, y, 0x0e) || memcmp(x + 0x12, y + 0x12, 4) || !assets_span_equal(v, 1) ||
        v->original_result != v->replacement_result || assets_expect.count != 1)
        return FALSE;
    const es2_asset_object *e = &assets_expect.objects[0];
    return assets_data(assets_get32(x + 0x0e), e->pixels, e->pixel_size) &&
           assets_data(assets_get32(x + 0x16), e->colour_bytes, (uint32_t)(int16_t)e->colours << 2);
}
/* bitmap_array_read (spans A and S, regions B and the counter): A without
 * its pointer, the children from the original's pointer array. */
static BOOL __cdecl assets_equal_array(const es2_hook_comparison *v, void *u) {
    if (assets_last_skip) return TRUE;
    const BYTE *x = v->original[0], *y = v->replacement[0];
    if (memcmp(x, y, 8) || !assets_span_equal(v, 1) || !assets_span_equal(v, 3) || !assets_expect.count ||
        assets_expect.objects[0].kind != ES2_ASSET_ARRAY || assets_get32(x + 4) != assets_expect.objects[0].count)
        return FALSE;
    unsigned next;
    return assets_children((uint32_t)(uintptr_t)u, assets_get32(x + 8), 0, &next) && next == assets_expect.count;
}
/* stream_destroy (span S, region counter): EAX, EDX and the bytes. */
static BOOL __cdecl assets_equal_destroy(const es2_hook_comparison *v, void *u) {
    (void)u;
    return assets_last_skip || (v->original_result == v->replacement_result && v->original_edx == v->replacement_edx &&
                                assets_span_equal(v, 0) && assets_span_equal(v, 1));
}

/* Callee counters (hk_<hook>_call_<va>): the guest calls and the modeled
 * stream operations (473368 tell, 4735b8 read; also when served from the
 * archive file; 47b400 tell, 47b4b4 read of a memory stream, also
 * when served as own code). dts_load's PASS reasons
 * (hk_dts_load_pass_<reason>, observer keys ES2_ASSET_PASS_*): one per
 * PASS in replaced mode, so hk_dts_load_bypassed minus their sum counts
 * the calls the framework bypassed (inside another hook's original phase). */
static callee_counter assets_load_counters[] = {
    {"call_473368", ES2_COLLIDE_CALL, 0x473368, 0}, {"call_4735b8", ES2_COLLIDE_CALL, 0x4735b8, 0},
    {"call_481cd4", ES2_COLLIDE_CALL, 0x481cd4, 0}, {"call_482d88", ES2_COLLIDE_CALL, 0x482d88, 0},
    {"call_47b400", ES2_COLLIDE_CALL, 0x47b400, 0}, {"call_47b4b4", ES2_COLLIDE_CALL, 0x47b4b4, 0},
    {"pass_stream", ES2_COLLIDE_CALL, ES2_ASSET_PASS_STREAM, 0}, {"pass_loose", ES2_COLLIDE_CALL, ES2_ASSET_PASS_LOOSE, 0},
    {"pass_unserved", ES2_COLLIDE_CALL, ES2_ASSET_PASS_UNSERVED, 0}, {"pass_tag", ES2_COLLIDE_CALL, ES2_ASSET_PASS_TAG, 0},
    {"pass_fallback", ES2_COLLIDE_CALL, ES2_ASSET_PASS_FALLBACK, 0},
};
enum { ASSETS_PASS_FIRST = 6, ASSETS_PASS_COUNT = 5 };   /* assets_load_counters[6..10] */
static callee_counter assets_bitmap_new_counters[] = {
    {"call_47a2c8", ES2_COLLIDE_CALL, 0x47a2c8, 0}, {"call_4774f0", ES2_COLLIDE_CALL, 0x4774f0, 0},
    {"call_481d8c", ES2_COLLIDE_CALL, 0x481d8c, 0},
};
static callee_counter assets_bitmap_read_counters[] = {
    {"call_4735b8", ES2_COLLIDE_CALL, 0x4735b8, 0}, {"call_47a288", ES2_COLLIDE_CALL, 0x47a288, 0},
    {"call_4774f0", ES2_COLLIDE_CALL, 0x4774f0, 0}, {"call_47b4b4", ES2_COLLIDE_CALL, 0x47b4b4, 0},
};
static callee_counter assets_array_new_counters[] = {
    {"call_47a2c8", ES2_COLLIDE_CALL, 0x47a2c8, 0}, {"call_4774f0", ES2_COLLIDE_CALL, 0x4774f0, 0},
    {"call_482e7c", ES2_COLLIDE_CALL, 0x482e7c, 0},
};
static callee_counter assets_array_read_counters[] = {
    {"call_4735b8", ES2_COLLIDE_CALL, 0x4735b8, 0}, {"call_47a288", ES2_COLLIDE_CALL, 0x47a288, 0},
    {"call_4774f0", ES2_COLLIDE_CALL, 0x4774f0, 0}, {"call_47a144", ES2_COLLIDE_CALL, 0x47a144, 0},
    {"call_47b4b4", ES2_COLLIDE_CALL, 0x47b4b4, 0},
};
static callee_counter assets_destroy_counters[] = {
    {"call_47307c", ES2_COLLIDE_CALL, 0x47307c, 0}, {"call_496406", ES2_COLLIDE_CALL, 0x496406, 0},
    {"call_477548", ES2_COLLIDE_CALL, 0x477548, 0},
};
#define ASSETS_COUNTERS(array) do { *count = sizeof(array) / sizeof(array[0]); return array; } while (0)
static callee_counter *assets_hook_counters(unsigned hook, unsigned *count) {
    if (hook == ES2_HOOK_INDEX_dts_load) ASSETS_COUNTERS(assets_load_counters);
    if (hook == ES2_HOOK_INDEX_bitmap_new) ASSETS_COUNTERS(assets_bitmap_new_counters);
    if (hook == ES2_HOOK_INDEX_bitmap_read) ASSETS_COUNTERS(assets_bitmap_read_counters);
    if (hook == ES2_HOOK_INDEX_bitmap_array_new) ASSETS_COUNTERS(assets_array_new_counters);
    if (hook == ES2_HOOK_INDEX_bitmap_array_read) ASSETS_COUNTERS(assets_array_read_counters);
    if (hook == ES2_HOOK_INDEX_stream_destroy) ASSETS_COUNTERS(assets_destroy_counters);
    *count = 0;
    return NULL;
}
#undef ASSETS_COUNTERS

/* Entry signatures and the relocated instructions. */
/* 47a144: push ebp; mov ebp,esp; add esp,-1Ch; push ebx; push esi; push edi; mov ebx,[ebp+8]; xor esi,esi */
static const BYTE assets_load_sig[] = {0x55,0x8b,0xec,0x83,0xc4,0xe4,0x53,0x56,0x57,0x8b,0x5d,0x08,0x33,0xf6,0x8b,0x3d};
/* 481cd4 / 482d88: push ebp; mov ebp,esp; add esp,-24h; mov eax,<descriptor>; push ebx */
static const BYTE assets_bitmap_new_sig[] = {0x55,0x8b,0xec,0x83,0xc4,0xdc,0xb8,0x9c,0x30,0x4a,0x00,0x53};
static const BYTE assets_array_new_sig[] = {0x55,0x8b,0xec,0x83,0xc4,0xdc,0xb8,0xd8,0x31,0x4a,0x00,0x53};
/* 481d8c: push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi; mov esi,[ebp+0Ch]; mov edi,[ebp+8] */
static const BYTE assets_bitmap_read_sig[] = {0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x75,0x0c,0x8b,0x7d,0x08,0x8d,0x47,0x04};
/* 482e7c: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov edi,[ebp+0Ch]; mov esi,[ebp+8] */
static const BYTE assets_array_read_sig[] = {0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x7d,0x0c,0x8b,0x75,0x08,0x8d,0x46,0x04};
/* 472f64: push ebp; mov ebp,esp; add esp,-28h; mov eax,4a0d3c; push ebx; mov ebx,[ebp+8] */
static const BYTE assets_destroy_sig[] = {0x55,0x8b,0xec,0x83,0xc4,0xd8,0xb8,0x3c,0x0d,0x4a,0x00,0x53,0x8b,0x5d,0x08};
static const BYTE assets_frame3_lens[] = {1,2,3};    /* push ebp; mov ebp,esp; add esp,imm8 */
static const BYTE assets_push2_lens[] = {1,2,1,1};   /* push ebp; mov ebp,esp; two pushes */
/* Spans and regions: the stream S (2Fh bytes), the object (1Ah / 0Ch);
 * B through S+0Ah (21h bytes); the construction counter [[6cbee4]]. */
static const es2_hook_span assets_s0_span[] = {{0, 0, ES2_VFS_STREAM_SIZE}};
static const es2_hook_span assets_read_spans[] = {{0, 0, ES2_ASSET_BITMAP_SIZE}, {1, 0, ES2_VFS_STREAM_SIZE}};
static const es2_hook_span assets_array_spans[] = {{0, 0, ES2_ASSET_ARRAY_SIZE}, {1, 0, ES2_VFS_STREAM_SIZE}};
static const es2_hook_region assets_s0_regions[] = {
    {ES2_REGION_ARG_INDIRECT, 0, 0, ES2_VFS_S_ARCHIVE, 0, ES2_VFS_BUFFERED_SIZE, FALSE, 0, {0, 0}},
    {ES2_REGION_RVA_INDIRECT, 0, 0x2cbee4u, 0, 0, 4, FALSE, 0, {0, 0}},
};
static const es2_hook_region assets_s1_regions[] = {
    {ES2_REGION_ARG_INDIRECT, 1, 0, ES2_VFS_S_ARCHIVE, 0, ES2_VFS_BUFFERED_SIZE, FALSE, 0, {0, 0}},
    {ES2_REGION_RVA_INDIRECT, 0, 0x2cbee4u, 0, 0, 4, FALSE, 0, {0, 0}},
};
static const es2_hook_region assets_counter_region[] = {{ES2_REGION_RVA_INDIRECT, 0, 0x2cbee4u, 0, 0, 4, FALSE, 0, {0, 0}}};
/* Context callbacks require pair results (hook.c extension_valid). */
#define ASSETS_EXT(name, regions, nregions) \
    static const es2_hook_extension assets_##name##_extension = {ES2_ABI_CDECL, ES2_RESULT_PAIR, NULL, NULL, regions, nregions, assets_##name, FALSE}; \
    static uint32_t __cdecl assets_##name##32(const uint32_t *a, void *u) { static const uint32_t none[3] = {0, 0, 0}; return (uint32_t)assets_##name(a, u, none); }
ASSETS_EXT(dts_load, assets_s0_regions, 2)
ASSETS_EXT(bitmap_new, assets_s0_regions, 2)
ASSETS_EXT(bitmap_read, assets_s1_regions, 1)
ASSETS_EXT(bitmap_array_new, assets_s0_regions, 2)
ASSETS_EXT(bitmap_array_read, assets_s1_regions, 2)
ASSETS_EXT(stream_destroy, assets_counter_region, 1)
#undef ASSETS_EXT
static es2_hook_descriptor assets_hook_descriptors[] = {
    {ES2_HOOK_ID(dts_load), assets_load_sig, 16, 6, assets_frame3_lens, 3, 2, assets_dts_load32, assets_equal_load, NULL, assets_s0_span, 1},
    {ES2_HOOK_ID(bitmap_new), assets_bitmap_new_sig, 12, 6, assets_frame3_lens, 3, 2, assets_bitmap_new32, assets_equal_new, NULL, assets_s0_span, 1},
    {ES2_HOOK_ID(bitmap_read), assets_bitmap_read_sig, 16, 5, assets_push2_lens, 4, 3, assets_bitmap_read32, assets_equal_read, NULL, assets_read_spans, 2},
    {ES2_HOOK_ID(bitmap_array_new), assets_array_new_sig, 12, 6, assets_frame3_lens, 3, 2, assets_bitmap_array_new32, assets_equal_new, NULL, assets_s0_span, 1},
    {ES2_HOOK_ID(bitmap_array_read), assets_array_read_sig, 15, 5, assets_push2_lens, 4, 3, assets_bitmap_array_read32, assets_equal_array, NULL, assets_array_spans, 2},
    {ES2_HOOK_ID(stream_destroy), assets_destroy_sig, 15, 6, assets_frame3_lens, 3, 2, assets_stream_destroy32, assets_equal_destroy, NULL, assets_s0_span, 1},
};
#ifndef ES2_SIM_HOST
/* Called by weapons_capture_entry after the framework installed a hook:
 * records the runtime of a hook of this group. */
static void assets_record_hook(es2_hook *h) {
    for (unsigned k = 0; k < AS_HOOKS; ++k)
        if (h->descriptor->rva == assets_rvas[k]) assets_runtime[k] = h;
}
#endif
#endif
