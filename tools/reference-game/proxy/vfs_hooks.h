#ifndef ES2_VFS_HOOKS_H
#define ES2_VFS_HOOKS_H
#include "guest_native.h"
/* Group vfs: DBSIM's VOL-aware read stream as own code
 * (docs/reverse-engineering/file_io.md, section "the VOL-aware read
 * stream as own code"): stream_open 4730e8, stream_close 47307c,
 * stream_read 4735b8, stream_seek 4733a4, stream_tell 473368, stream_size
 * 473334, vol_lookup 468064 and member_lookup 47cdf8 (core/src/vfs_stream.c
 * over formats/ es2_vol and es2_vfs_dbsim_member).
 *
 * Archives: an archive object A of the original's manager ([4a0d18]) is
 * identified by its path (the strcpy at A+8, at most 19 characters): the
 * same relative path is opened here (vfs_root + path; vfs_root is empty in
 * the game, so the working directory resolves it as for the game's own
 * open), mapped read-only, parsed with es2_vol_open and accepted only when
 * A's in-memory tables equal the file's (es2_vfs_stream_archive_matches).
 * Member bytes come from that mapping. An archive that is not identified
 * (no path, open/map/parse failure, different tables) is not modeled.
 *
 * Replaced mode: open calls close 47307c, the runtime _open (thunk 4967c6)
 * and vol_lookup 468064 through the guest bridge, vol_lookup calls
 * member_lookup 47cdf8, so nested hooks keep their modes; loose streams keep
 * the runtime's _read/_lseek/_filelength/_close, decoder kinds 7 and 9 the
 * original's decoder calls. A call the core does not model (ES2_VFS_PASS:
 * unidentified archive, buffered stream in an error state, out-of-file
 * range) runs the original through the trampoline and counts `bypassed`
 * (expected 0 in the suites).
 *
 * Compare mode: the replacement is pure (commit 0): the clone of S (or of
 * the record R) is compared, read compares its bytes with the original's
 * buffer instead of writing them (vfs_last_differences), and read, seek and
 * member_lookup see the buffered stream B's pre-state through a region
 * (ARG_INDIRECT: S+0Ah resp. A+4, 21h bytes) whose bytes are not compared
 * (the original's buffer window and ours differ by construction). A call
 * that would need a runtime or decoder call (a loose file, kinds 7/9) is
 * SKIP: the comparator accepts and the hook's `skipped` counts it. The
 * framework itself skips read/seek/member_lookup of a stream without B
 * (S+0Ah = 0, loose streams).
 *
 * Results (pair callbacks): EAX as the original (seek: AL only; bits 8-31
 * come from B's buffer state; close's comparator AL only, its bits 8-31 are
 * the stream's address), EDX = FS:0 at entry for stream_open (the frame
 * record's saved head), 0 for the others. Exit ECX/EDX are not reproduced
 * otherwise (no direct call site reads them before a write).
 *
 * vfs_record_hook (called by weapons_capture_entry) records each
 * installed hook's runtime: the
 * adapters read its mode and count skipped/bypassed. No shim, no game
 * instruction embedded. */
#include "es2/core/vfs_stream.h"
#include "es2/formats/vfs.h"
#include "machine_call.h"
#include "adapter_host.h"
#include "es2/platform/os.h"

enum { VFS_OPEN, VFS_CLOSE, VFS_READ, VFS_SEEK, VFS_TELL, VFS_SIZE, VFS_LOOKUP, VFS_MEMBER, VFS_HOOKS };
static const uint32_t vfs_rvas[VFS_HOOKS] = {0x730e8u, 0x7307cu, 0x735b8u, 0x733a4u, 0x73368u, 0x73334u, 0x68064u, 0x7cdf8u};
static es2_hook *vfs_runtime[VFS_HOOKS];
/* Prefix of the archive and loose paths: empty in the game (working
 * directory); the in-vitro harness sets the CD root with a backslash. */
static char vfs_root[ES2_OS_MAX_PATH];

typedef struct vfs_volume {
    uint32_t archive, buffered, records, dirs;
    int state;                                  /* 1 identified, -1 refused */
    es2_os_file_view view;
    es2_vol vol;
    es2_vfs_archive entry;
} vfs_volume;
#define VFS_VOLUMES 32u
static vfs_volume vfs_volumes[VFS_VOLUMES];
static unsigned vfs_volume_count;

static uint32_t vfs_r32(uint32_t a) { return math_native_read32(NULL, a); }
static void vfs_unmap(vfs_volume *v) {
    es2_os_unmap_file(&v->view);
    v->state = 0;
}
/* The fields the identification depends on are unchanged. */
static int vfs_stable(const vfs_volume *v) {
    return vfs_r32(v->archive + 4u) == v->buffered && vfs_r32(v->archive + 0x2cu) == v->records &&
           vfs_r32(v->archive + 0x24u) == v->dirs;
}
static uint32_t vfs_le(const uint8_t *p, unsigned n) {
    uint32_t x = 0;
    while (n--) x = x << 8 | p[n];
    return x;
}
/* A sparse view: the bytes es2_vol_open_tables reads (docs/formats/vol.md),
 * each range after the fields that locate it: the 12-byte header, the
 * directory and the member count and table size, the member table, and each
 * block's 9-byte prefix. Out-of-file fields are left to the parse. */
static void vfs_ensure_tables(const es2_os_file_view *view) {
    const uint8_t *b = view->bytes;
    const size_t size = view->size;
    es2_os_view_ensure(view, 0, 12);
    if (size < 12) return;
    const size_t directory = vfs_le(b + 10, 2);
    es2_os_view_ensure(view, 12, directory + 6);
    if (size - 12 < directory + 6) return;
    const size_t members = vfs_le(b + 12 + directory, 2), table = 18 + directory;
    es2_os_view_ensure(view, table, 18 * members);
    if (size - table < 18 * members) return;
    for (size_t i = 0; i < members; ++i) {
        const size_t block = vfs_le(b + table + 18 * i + 14, 4);
        if (block < size) es2_os_view_ensure(view, block, 9);
    }
}
static void vfs_load(vfs_volume *v, uint32_t a) {
    v->archive = a; v->buffered = vfs_r32(a + 4u); v->records = vfs_r32(a + 0x2cu); v->dirs = vfs_r32(a + 0x24u);
    v->state = -1;
    const char *name = es2_guest_span(a + 8u, 20u);
    size_t n = 0;
    while (n < 20u && name[n]) ++n;
    char path[ES2_OS_MAX_PATH];
    size_t root = strlen(vfs_root);
    if (!n || n == 20u || root + n >= sizeof(path)) return;
    memcpy(path, vfs_root, root); memcpy(path + root, name, n); path[root + n] = 0;
    if (!es2_os_map_file(path, &v->view)) return;
    /* A sparse view holds no payload yet, so its open skips the payloads'
     * suffix check; the site builder takes only VOLs that pass it. */
    const int sparse = es2_os_view_sparse(&v->view);
    if (sparse) vfs_ensure_tables(&v->view);
    const es2_span input = {v->view.bytes, v->view.size};
    if ((sparse ? es2_vol_open_tables(input, &v->vol, NULL) : es2_vol_open(input, &v->vol, NULL)) != ES2_OK ||
        !es2_vfs_stream_archive_matches(&math_native_memory, a, &v->vol)) return;
    v->entry = (es2_vfs_archive){a, v->buffered, &v->vol};
    v->state = 1;
}
static const vfs_volume *vfs_identify(uint32_t a) {
    for (unsigned i = 0; i < vfs_volume_count; ++i) {
        vfs_volume *v = &vfs_volumes[i];
        if (v->archive != a) continue;
        if (!vfs_stable(v)) { vfs_unmap(v); vfs_load(v, a); }
        return v->state == 1 ? v : NULL;
    }
    if (vfs_volume_count == VFS_VOLUMES) return NULL;
    vfs_volume *v = &vfs_volumes[vfs_volume_count++];
    memset(v, 0, sizeof(*v));
    vfs_load(v, a);
    return v->state == 1 ? v : NULL;
}
static const es2_vfs_archive *vfs_archive(void *context, uint32_t a, uint32_t b) {
    (void)context;
    if (a) { const vfs_volume *v = vfs_identify(a); return v ? &v->entry : NULL; }
    for (unsigned i = 0; i < vfs_volume_count; ++i) {
        const vfs_volume *v = &vfs_volumes[i];
        if (v->buffered == b && v->state == 1 && vfs_stable(v)) return &v->entry;
    }
    /* B of an archive not seen yet: the manager slot that holds it. */
    const uint32_t manager = vfs_r32(0x4a0d18u);
    if (!manager) return NULL;
    const unsigned count = *(const uint8_t *)es2_guest_span(manager + 0x78u, 1u);
    for (unsigned i = 0; i < count && i < 30u; ++i) {
        const uint32_t slot = vfs_r32(manager + 4u * i);
        if (slot && vfs_r32(slot + 4u) == b) { const vfs_volume *v = vfs_identify(slot); return v ? &v->entry : NULL; }
    }
    return NULL;
}
static void vfs_guest_call(void *context, uint32_t f, const uint32_t *a, unsigned n, uint32_t r[3]) {
    (void)context;
    es2_weapon_registers regs = {r[0], r[1], r[2]};
    weapons_machine_call(f, a, n, &regs);
    r[0] = regs.eax; r[1] = regs.edx; r[2] = regs.ecx;
}
/* Compare mode: would the runtime's _open(path, 8000h) open a loose file?
 * Its CreateFileA (CW3220 409ef3): GENERIC_READ, share read/write,
 * OPEN_EXISTING. */
static int vfs_loose(void *context, uint32_t path) {
    (void)context;
    if (!path) return 0;
    char full[ES2_OS_MAX_PATH];
    size_t root = strlen(vfs_root), n = 0;
    const char *p = es2_guest_string(path, (uint32_t)(root + 1u < sizeof(full) ? sizeof(full) - root : 1u));
    while (p[n] && root + n < sizeof(full) - 1u) ++n;
    if (p[n]) return 0;
    memcpy(full, vfs_root, root); memcpy(full + root, p, n); full[root + n] = 0;
    es2_os_handle f = es2_os_create_file(full, ES2_OS_GENERIC_READ, ES2_OS_FILE_SHARE_READ | ES2_OS_FILE_SHARE_WRITE, 0,
                                         ES2_OS_OPEN_EXISTING, ES2_OS_FILE_ATTRIBUTE_NORMAL);
    if (f == ES2_OS_INVALID_HANDLE) return 0;
    es2_os_close_handle(f);
    return 1;
}
static void vfs_observe(void *context, uint32_t function) { weapons_graph_observe(context, function, 0); }
static void vfs_ensure(void *context, const es2_vfs_archive *arc, size_t offset, size_t length) {
    (void)context;
    for (unsigned i = 0; i < vfs_volume_count; ++i)
        if (&vfs_volumes[i].entry == arc) { es2_os_view_ensure(&vfs_volumes[i].view, offset, length); return; }
}
/* The runtime of hook k while it is installed (entry patched). */
static es2_hook *vfs_installed(unsigned k, void *image) { return adapter_hook_installed(vfs_runtime[k], image, vfs_rvas[k]); }
static int vfs_last_skip;
static uint32_t vfs_last_differences;
static es2_vfs_host vfs_host(unsigned k, unsigned index, void *image) {
    es2_hook *h = vfs_installed(k, image);
    es2_vfs_host host = {&math_native_memory, (uint32_t)(uintptr_t)image, !adapter_hook_compares(h),
                         weapons_graph_context(index), vfs_archive, vfs_guest_call, vfs_loose, vfs_observe, vfs_ensure};
    vfs_last_skip = 0; vfs_last_differences = 0;
    return host;
}
static uint64_t vfs_finish(unsigned k, void *image, es2_vfs_outcome o, uint32_t eax, uint32_t edx,
                           const uint32_t *a, unsigned n, const uint32_t *r) {
    if (o == ES2_VFS_DONE) return (uint64_t)edx << 32 | eax;
    es2_hook *h = vfs_installed(k, image);
    if (o == ES2_VFS_SKIP) { adapter_hook_skipped(h); vfs_last_skip = 1; return 0; }
    /* PASS: the original, with the entry registers. */
    const uint32_t target = adapter_hook_pass(h, image, vfs_rvas[k]);
    return adapter_machine_pair(target, a, n, r[0], r[1], r[2]);
}
static uint32_t vfs_fs0(void) { return es2_machine_fs0(); }
static uint64_t __cdecl vfs_stream_open(const uint32_t *a, void *u, const uint32_t *r) {
    const uint32_t fs0 = vfs_fs0();
    es2_vfs_host h = vfs_host(VFS_OPEN, ES2_HOOK_INDEX_stream_open, u); es2_vfs_result o = {0, 0};
    es2_vfs_outcome k = es2_vfs_stream_open(&h, a[0], a[1], &o);
    return vfs_finish(VFS_OPEN, u, k, o.eax, fs0, a, 2, r);
}
static uint64_t __cdecl vfs_stream_close(const uint32_t *a, void *u, const uint32_t *r) {
    es2_vfs_host h = vfs_host(VFS_CLOSE, ES2_HOOK_INDEX_stream_close, u); es2_vfs_result o = {0, 0};
    es2_vfs_outcome k = es2_vfs_stream_close(&h, a[0], &o);
    return vfs_finish(VFS_CLOSE, u, k, o.eax, 0, a, 1, r);
}
static uint64_t __cdecl vfs_stream_read(const uint32_t *a, void *u, const uint32_t *r) {
    es2_vfs_host h = vfs_host(VFS_READ, ES2_HOOK_INDEX_stream_read, u); es2_vfs_result o = {0, 0};
    es2_vfs_outcome k = es2_vfs_stream_read(&h, a[0], a[1], a[2], r[0], &o);
    vfs_last_differences = o.differences;
    return vfs_finish(VFS_READ, u, k, o.eax, 0, a, 3, r);
}
static uint64_t __cdecl vfs_stream_seek(const uint32_t *a, void *u, const uint32_t *r) {
    es2_vfs_host h = vfs_host(VFS_SEEK, ES2_HOOK_INDEX_stream_seek, u); es2_vfs_result o = {0, 0};
    es2_vfs_outcome k = es2_vfs_stream_seek(&h, a[0], a[1], r[0], &o);
    return vfs_finish(VFS_SEEK, u, k, o.eax, 0, a, 2, r);
}
static uint64_t __cdecl vfs_stream_tell(const uint32_t *a, void *u, const uint32_t *r) {
    es2_vfs_host h = vfs_host(VFS_TELL, ES2_HOOK_INDEX_stream_tell, u); es2_vfs_result o = {0, 0};
    es2_vfs_outcome k = es2_vfs_stream_tell(&h, a[0], &o);
    return vfs_finish(VFS_TELL, u, k, o.eax, 0, a, 1, r);
}
static uint64_t __cdecl vfs_stream_size(const uint32_t *a, void *u, const uint32_t *r) {
    es2_vfs_host h = vfs_host(VFS_SIZE, ES2_HOOK_INDEX_stream_size, u); es2_vfs_result o = {0, 0};
    es2_vfs_outcome k = es2_vfs_stream_size(&h, a[0], &o);
    return vfs_finish(VFS_SIZE, u, k, o.eax, 0, a, 1, r);
}
static uint64_t __cdecl vfs_vol_lookup(const uint32_t *a, void *u, const uint32_t *r) {
    es2_vfs_host h = vfs_host(VFS_LOOKUP, ES2_HOOK_INDEX_vol_lookup, u); es2_vfs_result o = {0, 0};
    es2_vfs_outcome k = es2_vfs_stream_lookup(&h, a[0], a[1], a[2], &o);
    return vfs_finish(VFS_LOOKUP, u, k, o.eax, 0, a, 3, r);
}
static uint64_t __cdecl vfs_member_lookup(const uint32_t *a, void *u, const uint32_t *r) {
    es2_vfs_host h = vfs_host(VFS_MEMBER, ES2_HOOK_INDEX_member_lookup, u); es2_vfs_result o = {0, 0};
    es2_vfs_outcome k = es2_vfs_stream_member_lookup(&h, a[0], a[1], a[2], &o);
    return vfs_finish(VFS_MEMBER, u, k, o.eax, 0, a, 3, r);
}
/* Comparators: the clone of span 0 (S or R) and EAX; region views (B) are
 * not compared; a SKIP is accepted (counted in skipped). */
static BOOL vfs_same_span(const es2_hook_comparison *v) { return !memcmp(v->original[0], v->replacement[0], v->sizes[0]); }
static BOOL __cdecl vfs_equal(const es2_hook_comparison *v, void *u) {
    (void)u;
    return vfs_last_skip || (!vfs_last_differences && v->original_result == v->replacement_result && vfs_same_span(v));
}
static BOOL __cdecl vfs_equal_al(const es2_hook_comparison *v, void *u) {
    (void)u;
    return vfs_last_skip || ((uint8_t)v->original_result == (uint8_t)v->replacement_result && vfs_same_span(v));
}
static BOOL __cdecl vfs_equal_pair(const es2_hook_comparison *v, void *u) {
    return vfs_equal(v, u) && (vfs_last_skip || v->original_edx == v->replacement_edx);
}
/* Callee counters (hk_<hook>_call_<va>): the calls each replacement makes
 * through the guest bridge, and the modeled operations on the archive's
 * buffered stream (47c7d8 seek, 47c82c read) and the read's resync seek
 * (4733a4, done inline). */
static callee_counter vfs_open_counters[] = {
    {"call_47307c", ES2_COLLIDE_CALL, 0x47307c, 0}, {"call_4967c6", ES2_COLLIDE_CALL, 0x4967c6, 0},
    {"call_468064", ES2_COLLIDE_CALL, 0x468064, 0}, {"call_47749c", ES2_COLLIDE_CALL, 0x47749c, 0},
};
static callee_counter vfs_close_counters[] = {
    {"call_496874", ES2_COLLIDE_CALL, 0x496874, 0}, {"call_47b5cc", ES2_COLLIDE_CALL, 0x47b5cc, 0},
    {"call_47b9cc", ES2_COLLIDE_CALL, 0x47b9cc, 0},
};
static callee_counter vfs_read_counters[] = {
    {"call_496808", ES2_COLLIDE_CALL, 0x496808, 0}, {"call_4733a4", ES2_COLLIDE_CALL, 0x4733a4, 0},
    {"call_47c82c", ES2_COLLIDE_CALL, 0x47c82c, 0},
};
static callee_counter vfs_seek_counters[] = {
    {"call_496826", ES2_COLLIDE_CALL, 0x496826, 0}, {"call_47c7d8", ES2_COLLIDE_CALL, 0x47c7d8, 0},
};
static callee_counter vfs_tell_counters[] = {{"call_496826", ES2_COLLIDE_CALL, 0x496826, 0}};
static callee_counter vfs_size_counters[] = {{"call_496856", ES2_COLLIDE_CALL, 0x496856, 0}};
static callee_counter vfs_lookup_counters[] = {{"call_47cdf8", ES2_COLLIDE_CALL, 0x47cdf8, 0}};
static callee_counter vfs_member_counters[] = {
    {"call_47c7d8", ES2_COLLIDE_CALL, 0x47c7d8, 0}, {"call_47c82c", ES2_COLLIDE_CALL, 0x47c82c, 0},
};
#define VFS_COUNTERS(array) do { *count = sizeof(array) / sizeof(array[0]); return array; } while (0)
static callee_counter *vfs_hook_counters(unsigned hook, unsigned *count) {
    if (hook == ES2_HOOK_INDEX_stream_open) VFS_COUNTERS(vfs_open_counters);
    if (hook == ES2_HOOK_INDEX_stream_close) VFS_COUNTERS(vfs_close_counters);
    if (hook == ES2_HOOK_INDEX_stream_read) VFS_COUNTERS(vfs_read_counters);
    if (hook == ES2_HOOK_INDEX_stream_seek) VFS_COUNTERS(vfs_seek_counters);
    if (hook == ES2_HOOK_INDEX_stream_tell) VFS_COUNTERS(vfs_tell_counters);
    if (hook == ES2_HOOK_INDEX_stream_size) VFS_COUNTERS(vfs_size_counters);
    if (hook == ES2_HOOK_INDEX_vol_lookup) VFS_COUNTERS(vfs_lookup_counters);
    if (hook == ES2_HOOK_INDEX_member_lookup) VFS_COUNTERS(vfs_member_counters);
    *count = 0;
    return NULL;
}
#undef VFS_COUNTERS
/* Entry signatures (16 bytes) and the relocated instructions. */
/* 4730e8: push ebp; mov ebp,esp; add esp,-28h; push ebx; push esi; mov esi,[ebp+0Ch] */
static const BYTE vfs_open_sig[] = {0x55,0x8b,0xec,0x83,0xc4,0xd8,0x53,0x56,0x8b,0x75,0x0c,0x8b,0x5d,0x08,0xb8,0x94};
/* 47307c: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; cmp byte [ebx+26h],0 */
static const BYTE vfs_close_sig[] = {0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x80,0x7b,0x26,0x00,0x74,0x21,0x66,0x83,0x7b};
/* 4735b8: push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi */
static const BYTE vfs_read_sig[] = {0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x5d,0x08,0x8b,0x7d,0x10,0x8b,0x75,0x0c};
/* 4733a4: push ebp; mov ebp,esp; push ebx; push esi; mov esi,[ebp+0Ch] */
static const BYTE vfs_seek_sig[] = {0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x0c,0x8b,0x5d,0x08,0x83,0x7b,0x04,0x02,0x75};
/* 473368 / 473334: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov dx,[eax+8]; cmp dx,-1; je */
static const BYTE vfs_tell_sig[] = {0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x8b,0x50,0x08,0x66,0x83,0xfa,0xff,0x74,0x12};
static const BYTE vfs_size_sig[] = {0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x8b,0x50,0x08,0x66,0x83,0xfa,0xff,0x74,0x0c};
/* 468064: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov edi,[ebp+8] */
static const BYTE vfs_lookup_sig[] = {0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x7d,0x08,0x33,0xdb,0x8b,0xf7,0xeb,0x1f,0x8b};
/* 47cdf8: push ebp; mov ebp,esp; add esp,-14h; push ebx; push esi; push edi */
static const BYTE vfs_member_sig[] = {0x55,0x8b,0xec,0x83,0xc4,0xec,0x53,0x56,0x57,0x8b,0x75,0x08,0x68,0xab,0x5c,0x6c};
static const BYTE vfs_frame3_lens[] = {1,2,3};      /* push ebp; mov ebp,esp; 3-byte instruction */
static const BYTE vfs_push2_lens[] = {1,2,1,1};     /* push ebp; mov ebp,esp; two pushes */
static const BYTE vfs_close_lens[] = {1,2,1,3};
/* Spans: S (2Fh bytes) or the member record R (1Ch bytes); regions: B. */
static const es2_hook_span vfs_stream_span[] = {{0, 0, ES2_VFS_STREAM_SIZE}};
static const es2_hook_span vfs_record_span[] = {{2, 0, ES2_VFS_RECORD_SIZE}};
static const es2_hook_region vfs_stream_buffered[] = {{ES2_REGION_ARG_INDIRECT, 0, 0, ES2_VFS_S_ARCHIVE, 0, ES2_VFS_BUFFERED_SIZE, FALSE, 0, {0, 0}}};
static const es2_hook_region vfs_archive_buffered[] = {{ES2_REGION_ARG_INDIRECT, 0, 0, 4, 0, ES2_VFS_BUFFERED_SIZE, FALSE, 0, {0, 0}}};
/* Context callbacks require pair results (hook.c extension_valid). */
#define VFS_EXT(name, regions, nregions) \
    static const es2_hook_extension vfs_##name##_extension = {ES2_ABI_CDECL, ES2_RESULT_PAIR, NULL, NULL, regions, nregions, vfs_##name, FALSE}; \
    static uint32_t __cdecl vfs_##name##32(const uint32_t *a, void *u) { static const uint32_t none[3] = {0, 0, 0}; return (uint32_t)vfs_##name(a, u, none); }
VFS_EXT(stream_open, NULL, 0)
VFS_EXT(stream_close, NULL, 0)
VFS_EXT(stream_read, vfs_stream_buffered, 1)
VFS_EXT(stream_seek, vfs_stream_buffered, 1)
VFS_EXT(stream_tell, NULL, 0)
VFS_EXT(stream_size, NULL, 0)
VFS_EXT(vol_lookup, NULL, 0)
VFS_EXT(member_lookup, vfs_archive_buffered, 1)
#undef VFS_EXT
static es2_hook_descriptor vfs_hook_descriptors[] = {
    {ES2_HOOK_ID(stream_open), vfs_open_sig, 16, 6, vfs_frame3_lens, 3, 2, vfs_stream_open32, vfs_equal_pair, NULL, vfs_stream_span, 1},
    {ES2_HOOK_ID(stream_close), vfs_close_sig, 16, 7, vfs_close_lens, 4, 1, vfs_stream_close32, vfs_equal_al, NULL, vfs_stream_span, 1},
    {ES2_HOOK_ID(stream_read), vfs_read_sig, 16, 5, vfs_push2_lens, 4, 3, vfs_stream_read32, vfs_equal, NULL, vfs_stream_span, 1},
    {ES2_HOOK_ID(stream_seek), vfs_seek_sig, 16, 5, vfs_push2_lens, 4, 2, vfs_stream_seek32, vfs_equal_al, NULL, vfs_stream_span, 1},
    {ES2_HOOK_ID(stream_tell), vfs_tell_sig, 16, 6, vfs_frame3_lens, 3, 1, vfs_stream_tell32, vfs_equal, NULL, vfs_stream_span, 1},
    {ES2_HOOK_ID(stream_size), vfs_size_sig, 16, 6, vfs_frame3_lens, 3, 1, vfs_stream_size32, vfs_equal, NULL, vfs_stream_span, 1},
    {ES2_HOOK_ID(vol_lookup), vfs_lookup_sig, 16, 5, vfs_push2_lens, 4, 3, vfs_vol_lookup32, vfs_equal, NULL, vfs_record_span, 1},
    {ES2_HOOK_ID(member_lookup), vfs_member_sig, 16, 6, vfs_frame3_lens, 3, 3, vfs_member_lookup32, vfs_equal, NULL, vfs_record_span, 1},
};
#ifndef ES2_SIM_HOST
/* Called by weapons_capture_entry after the framework installed a hook:
 * records the runtime of a hook of this group. */
static void vfs_record_hook(es2_hook *h) {
    for (unsigned k = 0; k < VFS_HOOKS; ++k)
        if (h->descriptor->rva == vfs_rvas[k]) vfs_runtime[k] = h;
}
#endif
#endif
