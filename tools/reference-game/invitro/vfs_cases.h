/* Group vfs (docs/reverse-engineering/file_io.md, "the VOL-aware read stream as
 * own code"): stream_open 4730e8, stream_close 47307c, stream_read 4735b8,
 * stream_seek 4733a4, stream_tell 473368, stream_size 473334, vol_lookup
 * 468064, member_lookup 47cdf8.
 *
 * Inputs are the real files under ES2_DATA_ROOT. DBSIM's file imports (_open,
 * _read, _lseek, _filelength, _close) point to an own test file system over the
 * CD tree (whole files read once, handles and positions in a state struct that
 * is saved and restored with each case), _calloc to a deterministic bump heap,
 * the string imports the original lookup uses (_stricmp, _strrchr, _strchr,
 * _strncpy, _strcat, _strncmp) to the mapped CW3220.DLL (rtl_cases.h's private
 * mapping). The archive manager is built by the original insert 467f94
 * (archive_open 47cab0, the read and buffered streams) over the nine simulation
 * archives of the CD in directory order; a second manager holds a synthetic
 * archive (kinds 2, 4, 7, 9; not on disk, so the adapter cannot identify it)
 * before SIMPATCH. The decoder functions
 * 47b50c/47b594/47b5cc/47b840/47b974/47b9cc are logging stubs in the private
 * image; a fake decoder object has stub virtuals +8/+18h. The adapter's
 * vfs_root is the CD root, so it maps the same VOL files.
 *
 * A case starts from the base state, opens and moves streams with the
 * original functions (installed pass: through the installed hooks), moves
 * the buffered streams of the archives, sets fields and fault injections,
 * then runs the original (unhooked, or the trampoline in the installed
 * pass) and the replacement (the context callback, or the installed hook)
 * from the same state. Compared: EAX (stream_seek: AL), EDX of stream_open,
 * the stream objects, records and paths (arena), the read buffer with
 * guards, each buffered stream B's tell and status and fixed fields, each
 * archive file stream's vtable/handle/owner, the open handles of the test
 * file system with the positions of non-archive handles, the bump heap,
 * the construction counter, the allocator counter 6bb30c and the decoder
 * stub log. Not compared: B's buffer window (base/valid/cursor beyond the
 * tell, buffer bytes), the archive file streams' status and file positions
 * (internal to B, reset by B's next refill) and the static split buffers
 * 6c5aa8..6c5eb3 (scratch, see file_io.md).
 *
 * Passes (limit 0): direct cases with the coverage window; a real-callee
 * pass (stream_open and stream_read: the eight hooks installed in all 256
 * original/replaced combinations against the all-original run, each
 * followed by an original read through the same archive); installed
 * 31/a7 poison cases; compare cases (the hook alone in compare mode:
 * original result and state kept, 0 mismatches, skips counted; then a
 * skewed replacement: EAX bit 0 flipped, for stream_read a byte of the
 * original's buffer flipped before the comparison, every compared call one
 * mismatch). limit > 0: main.c's installed pass. */
#include <ctype.h>
static int rtl_map_cw3220(const char *root);
static uint32_t rtl_cw_export(const char *name);
static BYTE *rtl_cw;

/* ------------------------------------------------------------------ */
/* Test file system behind DBSIM's file imports. */
#define VFF_FILES 64u
#define VFF_HANDLES 24u
#define VFF_FIRST 5
#define VFF_HEAP (4u << 20)
typedef struct vff_file { char key[128]; BYTE *data; uint32_t size; } vff_file;
typedef struct vff_handle { int file; uint32_t pos; } vff_handle;
typedef struct vff_state {
    vff_handle handle[VFF_HANDLES];
    uint32_t bump;
    unsigned fail_read, fail_lseek, fail_close, fail_calloc;
} vff_state;
static vff_file vff_files[VFF_FILES];
static unsigned vff_file_count;
static vff_state vff;
static BYTE *vff_heap;
static char vff_cd[MAX_PATH];
static int vff_key(const char *path, char key[128]) {
    size_t n = 0;
    for (; path[n]; ++n) {
        if (n >= 127) return 0;
        char c = path[n];
        key[n] = c == '/' ? '\\' : (char)toupper((unsigned char)c);
    }
    key[n] = 0;
    return n != 0;
}
static int vff_add(const char *key, BYTE *data, uint32_t size) {
    if (vff_file_count == VFF_FILES) return -1;
    vff_file *f = &vff_files[vff_file_count];
    snprintf(f->key, sizeof(f->key), "%s", key);
    f->data = data; f->size = size;
    return (int)vff_file_count++;
}
/* A file of the CD tree (read once), or -1 when it does not open. */
static int vff_find(const char *path) {
    char key[128], full[MAX_PATH];
    if (!vff_key(path, key)) return -1;
    for (unsigned i = 0; i < vff_file_count; ++i) if (!strcmp(vff_files[i].key, key)) return (int)i;
    if (snprintf(full, sizeof(full), "%s\\%s", vff_cd, path) >= (int)sizeof(full)) return -1;
    HANDLE f = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    LARGE_INTEGER size; DWORD got = 0; BYTE *data = NULL;
    int ok = GetFileSizeEx(f, &size) && size.QuadPart >= 0 && size.QuadPart < 0x4000000 &&
             (data = malloc((size_t)size.QuadPart + 1u)) && ReadFile(f, data, (DWORD)size.QuadPart, &got, NULL) &&
             got == (DWORD)size.QuadPart;
    CloseHandle(f);
    if (!ok) { free(data); return -1; }
    return vff_add(key, data, (uint32_t)size.QuadPart);
}
/* The archives' own file handles (open at the end of the setup) never fail:
 * an I/O error inside an archive is not part of the modeled contract. */
static uint32_t vff_archive_mask;
static int vff_archive(int h) { return h >= VFF_FIRST && h < VFF_FIRST + 32 && (vff_archive_mask >> (h - VFF_FIRST) & 1u); }
static vff_handle *vff_handle_of(int h) {
    if (h < VFF_FIRST || h >= VFF_FIRST + (int)VFF_HANDLES || vff.handle[h - VFF_FIRST].file < 0) return NULL;
    return &vff.handle[h - VFF_FIRST];
}
static int __cdecl vff_open(const char *path, int flags) {
    if (flags != 0x8000) { fprintf(stderr, "FAIL: %s: _open flags %x\n", current_function, (unsigned)flags); longjmp(trap_return, 1); }
    const int f = path ? vff_find(path) : -1;
    if (f < 0) return -1;
    for (unsigned i = 0; i < VFF_HANDLES; ++i)
        if (vff.handle[i].file < 0) { vff.handle[i].file = f; vff.handle[i].pos = 0; return VFF_FIRST + (int)i; }
    return -1;
}
static int __cdecl vff_read(int h, void *buffer, unsigned n) {
    vff_handle *x = vff_handle_of(h);
    if (!x || (vff.fail_read && !vff_archive(h))) { vff.fail_read = 0; return -1; }
    const vff_file *f = &vff_files[x->file];
    const uint32_t k = x->pos >= f->size ? 0 : (n < f->size - x->pos ? n : f->size - x->pos);
    memcpy(buffer, f->data + x->pos, k);
    x->pos += k;
    return (int)k;
}
static long __cdecl vff_lseek(int h, long offset, int whence) {
    vff_handle *x = vff_handle_of(h);
    if (!x || (vff.fail_lseek && !vff_archive(h)) || whence < 0 || whence > 2) { vff.fail_lseek = 0; return -1; }
    const int64_t base = whence == 0 ? 0 : whence == 1 ? x->pos : vff_files[x->file].size, at = base + offset;
    if (at < 0 || at > 0x7fffffff) return -1;
    x->pos = (uint32_t)at;
    return (long)at;
}
static long __cdecl vff_filelength(int h) { vff_handle *x = vff_handle_of(h); return x ? (long)vff_files[x->file].size : -1; }
static int __cdecl vff_close(int h) {
    vff_handle *x = vff_handle_of(h);
    if (!x || (vff.fail_close && !vff_archive(h))) { vff.fail_close = 0; return -1; }
    x->file = -1;
    return 0;
}
/* calloc(n, size) of 47747c: zeroed block, its size in the dword before it. */
static void *__cdecl vff_calloc(unsigned n, unsigned size) {
    const uint32_t total = n * size;
    if (vff.fail_calloc) { vff.fail_calloc = 0; return NULL; }
    if (total > VFF_HEAP / 2 || vff.bump > VFF_HEAP - total - 16u) { fprintf(stderr, "FAIL: vfs bump heap exhausted\n"); longjmp(trap_return, 1); }
    BYTE *p = vff_heap + vff.bump + 8;
    vff.bump += (total + 15u) & ~7u;
    memset(p, 0, total); put32(p - 4, total);
    return p;
}

/* ------------------------------------------------------------------ */
/* Decoder stubs (patched into the private image) and the fake decoder. */
typedef struct vff_log { uint32_t id, a[3]; } vff_log;
static vff_log vff_logs[16];
static unsigned vff_log_count;
static uint32_t vff_plan;
static void vff_note(uint32_t id, uint32_t a, uint32_t b, uint32_t c) {
    if (vff_log_count < 16) { vff_log *l = &vff_logs[vff_log_count]; l->id = id; l->a[0] = a; l->a[1] = b; l->a[2] = c; }
    ++vff_log_count;
}
static uint32_t vff_decoder_vtable;  /* the fake decoder vtable (arena), set by the setup */
static uint32_t __cdecl vff_ctor7(uint32_t p) { vff_note(1, p, 0, 0); if (p) put32((BYTE *)(uintptr_t)p, vff_decoder_vtable); return p; }
static uint32_t __cdecl vff_init7(uint32_t d, uint32_t a) { vff_note(2, d, a, 0); return vff_plan; }
static uint32_t __cdecl vff_reset7(uint32_t d) { vff_note(3, d, 0, 0); return vff_plan ^ 1u; }
static uint32_t __cdecl vff_ctor9(uint32_t p) { vff_note(4, p, 0, 0); if (p) put32((BYTE *)(uintptr_t)p, vff_decoder_vtable); return p; }
static uint32_t __cdecl vff_init9(uint32_t d, uint32_t a) { vff_note(5, d, a, 0); return vff_plan ^ 2u; }
static uint32_t __cdecl vff_reset9(uint32_t d) { vff_note(6, d, 0, 0); return vff_plan ^ 3u; }
static uint32_t __cdecl vff_dec_seek(uint32_t d, uint32_t pos) { vff_note(7, d, pos, 0); return vff_plan ^ 4u; }
static uint32_t __cdecl vff_dec_read(uint32_t d, uint32_t n, uint32_t b) {
    vff_note(8, d, n, b);
    if (n <= 0x10000u) memset((void *)(uintptr_t)b, 0x6b, n);
    return vff_plan ^ 5u;
}
static const struct { uint32_t rva; void *stub; } vff_decoders[] = {
    {0x7b50c, vff_ctor7}, {0x7b594, vff_init7}, {0x7b5cc, vff_reset7},
    {0x7b840, vff_ctor9}, {0x7b974, vff_init9}, {0x7b9cc, vff_reset9},
};

/* ------------------------------------------------------------------ */
/* Import slots of the private DBSIM (through each thunk's jmp [slot]). */
typedef struct vff_import { uint32_t thunk; const char *name; void *own; uint32_t slot, saved; } vff_import;
static vff_import vff_imports[] = {
    {0x4967c6, "_open", (void *)vff_open, 0, 0}, {0x496808, "_read", (void *)vff_read, 0, 0},
    {0x496826, "_lseek", (void *)vff_lseek, 0, 0}, {0x496856, "_filelength", (void *)vff_filelength, 0, 0},
    {0x496874, "_close", (void *)vff_close, 0, 0}, {0x49686e, "_calloc", (void *)vff_calloc, 0, 0},
    {0x496868, "_stricmp", NULL, 0, 0}, {0x4967cc, "_strrchr", NULL, 0, 0}, {0x4967a2, "_strchr", NULL, 0, 0},
    {0x496760, "_strncpy", NULL, 0, 0}, {0x4967ea, "_strcat", NULL, 0, 0}, {0x496778, "_strncmp", NULL, 0, 0},
};
#define VFF_IMPORTS (sizeof(vff_imports) / sizeof(vff_imports[0]))
/* The import name bound to an IAT slot (DBSIM's import directory). */
static const char *vff_slot_name(uint32_t slot) {
    const uint32_t pe = get32(mapped + 0x3c);
    for (const BYTE *d = mapped + get32(mapped + pe + 24 + 104); get32(d + 12); d += 20) {
        const uint32_t lookup = get32(d) ? get32(d) : get32(d + 16), iat = get32(d + 16);
        for (unsigned i = 0; get32(mapped + lookup + 4 * i); ++i)
            if (0x400000u + iat + 4 * i == slot) {
                const uint32_t e = get32(mapped + lookup + 4 * i);
                return e & 0x80000000u ? "" : (const char *)mapped + e + 2;
            }
    }
    return "";
}

/* ------------------------------------------------------------------ */
/* The archives, the managers and the arena. */
static const char *const vfc_sim[] = {"SIMALERT", "SIMLANG", "SIMPATCH", "SIMSOUND", "SIMVOICE", "SIMVOICF",
                                     "SIMVOICG", "SIMVOL0", "ZONES"};
#define VFC_SIM 9u
#define VFC_ARCHIVES (VFC_SIM + 2u)                   /* + the synthetic one and SIMPATCH again in M2 */
static uint32_t vfc_manager[2];                      /* M1: CD archives; M2: synthetic, SIMPATCH */
static uint32_t vfc_archive[VFC_ARCHIVES];            /* archive objects of M1's and M2's slots */
static unsigned vfc_archive_count;
typedef struct vfc_member { char path[64]; uint32_t length; unsigned archive; } vfc_member;
static vfc_member *vfc_members;
static unsigned vfc_member_count;
static const char *const vfc_loose[] = {"DATA\\PREFS.CFG", "data\\keyjoy.cfg", "ERROR.STR", "version.txt", "DEMO.STR",
                                       "DATA\\MAT0.DAT", "vol\\SIMPATCH.VOL", "VOL\\simlang.vol"};
static const char *const vfc_missing[] = {"dts\\nothere.dts", "zz\\a.b", "", "VOL", "DATA", "dts\\", "\\dts\\x",
                                         "c:dts\\x.dts", "pdg\\spider", "bnd\\results.bnd.", "pdg/spider.pdg"};
enum { VFC_S0 = 0x000, VFC_S1 = 0x040, VFC_R = 0x080, VFC_PATH = 0x100, VFC_D = 0x300, VFC_DVT = 0x320, VFC_ARENA = 0x400 };
static BYTE vfc_arena[VFC_ARENA];
#define VFC_ADDR(o) ((uint32_t)(uintptr_t)(vfc_arena + (o)))
#define VFC_BUF 0x10000u
static BYTE vfc_buffer[VFC_BUF + 32u];
#define VFC_BUFFER ((uint32_t)(uintptr_t)(vfc_buffer + 16u))
static uint32_t vfc_counter, vfc_saved_counter_ptr, vfc_saved_manager, vfc_saved_bufsize, vfc_saved_alloc;
static BYTE vfc_decoder_saved[6][5];
static int vfc_ready;
/* The synthetic archive: dts\ with K2 (kind 2), K4, K7, K9, each 24 bytes. */
static BYTE vfc_synthetic[512];
static uint32_t vfc_synthetic_size;
static void vfc_build_synthetic(void) {
    BYTE *v = vfc_synthetic;
    memset(v, 0, sizeof(vfc_synthetic));
    memcpy(v, "VOLN", 4); put32(v + 4, 1); v[8] = 10; v[9] = 1; put16(v + 10, 5); memcpy(v + 12, "dts\\", 5);
    static const char *const names[4] = {"K2.DTS", "K4.DTS", "K7.DTS", "K9.DTS"};
    static const BYTE kinds[4] = {2, 4, 7, 9};
    put16(v + 17, 4); put32(v + 19, 72);
    uint32_t block = 23 + 72;
    for (unsigned i = 0; i < 4; ++i) {
        BYTE *slot = v + 23 + 18 * i;
        memcpy(slot, names[i], strlen(names[i])); slot[13] = 0; put32(slot + 14, block);
        v[block] = kinds[i]; put32(v + block + 1, 24); put32(v + block + 5, 0x1000u * i);
        for (unsigned k = 0; k < 24; ++k) v[block + 9 + k] = (BYTE)(0x30 + 8 * i + k);
        v[block + 33] = v[block + 32];
        block += 34;
    }
    vfc_synthetic_size = block;
}
static uint64_t vfc_entry(uint32_t va, unsigned count, const uint32_t *a, const uint32_t *r) {
    es2_hook_result res;
    es2_hook_call call = {mapped + (va - 0x400000u), ES2_ABI_CDECL, count, a, initial_fpu, &res, r};
    es2_hook_machine_call(&call);
    return (uint64_t)res.edx << 32 | res.eax;
}
static const uint32_t vfc_zero[3] = {0, 0, 0};
static int vfc_patch(int on) {
    for (unsigned i = 0; i < VFF_IMPORTS; ++i) {
        vff_import *m = &vff_imports[i];
        if (on) {
            m->saved = get32(mapped + m->slot - 0x400000u);
            put32(mapped + m->slot - 0x400000u, m->own ? (uint32_t)(uintptr_t)m->own : rtl_cw_export(m->name));
        } else put32(mapped + m->slot - 0x400000u, m->saved);
    }
    for (unsigned i = 0; i < 6; ++i)
        if (actor_patch(vff_decoders[i].rva, vff_decoders[i].stub, vfc_decoder_saved[i], on ? TRUE : FALSE)) return 1;
    if (on) {
        vfc_saved_counter_ptr = get32(mapped + 0x2cbee4); vfc_saved_manager = get32(mapped + 0xa0d18);
        vfc_saved_bufsize = get32(mapped + 0xa088c); vfc_saved_alloc = get32(mapped + 0x2bb30c);
        put32(mapped + 0x2cbee4, (uint32_t)(uintptr_t)&vfc_counter); put32(mapped + 0xa088c, 0x400);
    } else {
        put32(mapped + 0x2cbee4, vfc_saved_counter_ptr); put32(mapped + 0xa0d18, vfc_saved_manager);
        put32(mapped + 0xa088c, vfc_saved_bufsize); put32(mapped + 0x2bb30c, vfc_saved_alloc);
    }
    return 0;
}
/* One-time setup: the CD root, CW3220, the file system, the managers. */
static int vfc_setup(void) {
    const char *root = getenv("ES2_DATA_ROOT");
    char probe[MAX_PATH];
    snprintf(probe, sizeof(probe), "%s/VER95/DBSIM.EXE", root);
    snprintf(vff_cd, sizeof(vff_cd), GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES ? "%s" : "%s\\extracted\\cd", root);
    if (!rtl_cw && rtl_map_cw3220(root)) return 1;
    vff_heap = VirtualAlloc(NULL, VFF_HEAP, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!vff_heap) return fail("vfs heap");
    for (unsigned i = 0; i < VFF_HANDLES; ++i) vff.handle[i].file = -1;
    for (unsigned i = 0; i < VFF_IMPORTS; ++i) {
        vff_import *m = &vff_imports[i];
        const BYTE *t = mapped + m->thunk - 0x400000u;
        if (t[0] != 0xff || t[1] != 0x25) return fail("vfs import thunk");
        m->slot = get32(t + 2);
        if (strcmp(vff_slot_name(m->slot), m->name)) return fail("vfs import slot name");
        if (!m->own && !rtl_cw_export(m->name)) return fail("vfs CW3220 export");
    }
    vfc_build_synthetic();
    vff_decoder_vtable = VFC_ADDR(VFC_DVT);
    if (vff_add("VOL\\ZZSYNTH.VOL", vfc_synthetic, vfc_synthetic_size) < 0) return fail("vfs synthetic file");
    snprintf(vfs_root, sizeof(vfs_root), "%s\\", vff_cd);
    if (vfc_patch(1)) return 1;
    /* The managers: zeroed 79h bytes, then the original insert per path. */
    for (unsigned m = 0; m < 2; ++m) {
        BYTE *manager = vff_calloc(1, 0x79);
        vfc_manager[m] = (uint32_t)(uintptr_t)manager;
        const unsigned count = m ? 2u : VFC_SIM;
        for (unsigned i = 0; i < count; ++i) {
            char path[64];
            snprintf(path, sizeof(path), "vol\\%s.VOL", m ? (i ? "SIMPATCH" : "ZZSYNTH") : vfc_sim[i]);
            const uint32_t a[3] = {vfc_manager[m], (uint32_t)(uintptr_t)path, 1};
            current_function = "vfs setup";
            vfc_entry(0x467f94, 3, a, vfc_zero);
        }
        if (manager[0x78] != count) return fail("vfs manager build");
        for (unsigned i = 0; i < count; ++i) vfc_archive[vfc_archive_count++] = get32(manager + 4 * i);
    }
    /* Member paths of the CD archives (directory string + name). */
    vfc_members = calloc(4096, sizeof(*vfc_members));
    for (unsigned i = 0; i < VFC_SIM && vfc_members; ++i) {
        const uint32_t archive = vfc_archive[i];
        const vff_file *f = NULL;
        char key[128];
        snprintf(key, sizeof(key), "%s", (const char *)(uintptr_t)(archive + 8));
        if (!vff_key(key, key)) return fail("vfs archive name");
        for (unsigned k = 0; k < vff_file_count; ++k) if (!strcmp(vff_files[k].key, key)) f = &vff_files[k];
        es2_vol vol;
        if (!f || es2_vol_open((es2_span){f->data, f->size}, &vol, NULL) != ES2_OK) return fail("vfs archive parse");
        for (size_t m = 0; m < vol.member_count && vfc_member_count < 4096; ++m) {
            es2_vol_member e;
            if (es2_vol_member_at(&vol, m, &e, NULL) != ES2_OK) return fail("vfs member");
            vfc_member *v = &vfc_members[vfc_member_count++];
            snprintf(v->path, sizeof(v->path), "%.*s%.*s", (int)e.directory.size, (const char *)e.directory.data,
                     (int)e.name.size, (const char *)e.name.data);
            v->length = (uint32_t)e.length; v->archive = i;
        }
    }
    if (!vfc_members || vfc_member_count < 2000) return fail("vfs member list");
    for (unsigned i = 0; i < VFF_HANDLES; ++i) if (vff.handle[i].file >= 0) vff_archive_mask |= 1u << i;
    vfc_patch(0);
    printf("VFS setup: CD %s, %u files, managers %u+2 archives (synthetic not identifiable), %u member paths, heap %u bytes\n",
           vff_cd, vff_file_count, VFC_SIM, vfc_member_count, vff.bump);
    vfc_ready = 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* State: snapshot, restore and compare. */
typedef struct vfc_state {
    BYTE arena[VFC_ARENA];
    BYTE buffer[VFC_BUF + 32u];
    BYTE buffered[VFC_ARCHIVES][ES2_VFS_BUFFERED_SIZE];
    BYTE window[VFC_ARCHIVES][0x400];
    BYTE file_stream[VFC_ARCHIVES][0x0b];
    vff_state fs;
    uint32_t manager, counter, alloc;
    BYTE split[0x40c];
    vff_log logs[16];
    unsigned log_count;
} vfc_state;
static vfc_state vfc_base, vfc_pre, vfc_orig, vfc_repl;
static uint32_t vfc_buffer_extent = 64;              /* compared and saved buffer bytes this case */
static void vfc_save(vfc_state *s) {
    memcpy(s->arena, vfc_arena, VFC_ARENA);
    memcpy(s->buffer, vfc_buffer, vfc_buffer_extent + 32u);
    for (unsigned i = 0; i < vfc_archive_count; ++i) {
        const uint32_t b = get32((BYTE *)(uintptr_t)vfc_archive[i] + 4), u = get32((BYTE *)(uintptr_t)b + 8);
        memcpy(s->buffered[i], (void *)(uintptr_t)b, ES2_VFS_BUFFERED_SIZE);
        memcpy(s->window[i], (void *)(uintptr_t)get32((BYTE *)(uintptr_t)b + 0x14), 0x400);
        memcpy(s->file_stream[i], (void *)(uintptr_t)u, 0x0b);
    }
    s->fs = vff;
    s->manager = get32(mapped + 0xa0d18); s->counter = vfc_counter; s->alloc = get32(mapped + 0x2bb30c);
    memcpy(s->split, mapped + 0x2c5aa8, sizeof(s->split));
    memcpy(s->logs, vff_logs, sizeof(vff_logs)); s->log_count = vff_log_count;
}
static void vfc_load(const vfc_state *s) {
    memcpy(vfc_arena, s->arena, VFC_ARENA);
    memcpy(vfc_buffer, s->buffer, vfc_buffer_extent + 32u);
    for (unsigned i = 0; i < vfc_archive_count; ++i) {
        const uint32_t b = get32((BYTE *)(uintptr_t)vfc_archive[i] + 4);
        memcpy((void *)(uintptr_t)b, s->buffered[i], ES2_VFS_BUFFERED_SIZE);
        memcpy((void *)(uintptr_t)get32(s->buffered[i] + 0x14), s->window[i], 0x400);
        memcpy((void *)(uintptr_t)get32(s->buffered[i] + 8), s->file_stream[i], 0x0b);
    }
    vff = s->fs;
    put32(mapped + 0xa0d18, s->manager); vfc_counter = s->counter; put32(mapped + 0x2bb30c, s->alloc);
    memcpy(mapped + 0x2c5aa8, s->split, sizeof(s->split));
    memcpy(vff_logs, s->logs, sizeof(vff_logs)); vff_log_count = s->log_count;
}
static int vfc_archive_handle(const vfc_state *s, int h) {
    for (unsigned i = 0; i < vfc_archive_count; ++i) if ((int)(int16_t)get16(s->file_stream[i] + 8) == h) return 1;
    return 0;
}
/* 0 when equal; otherwise prints the first difference. */
static int vfc_differs(const vfc_state *a, const vfc_state *b, unsigned n) {
    for (unsigned i = 0; i < VFC_ARENA; ++i)
        if (a->arena[i] != b->arena[i]) { fprintf(stderr, "  arena +%x %02x/%02x\n", i, a->arena[i], b->arena[i]); return 1; }
    for (unsigned i = 0; i < vfc_buffer_extent + 32u; ++i)
        if (a->buffer[i] != b->buffer[i]) { fprintf(stderr, "  buffer +%x %02x/%02x\n", i, a->buffer[i], b->buffer[i]); return 1; }
    for (unsigned i = 0; i < vfc_archive_count; ++i) {
        const BYTE *x = a->buffered[i], *y = b->buffered[i];
        const uint32_t tx = get32(x + 4) == 6 ? 0 : get32(x + 0x10) + get32(x + 0x1d), ty = get32(y + 4) == 6 ? 0 : get32(y + 0x10) + get32(y + 0x1d);
        if (tx != ty || get32(x + 4) != get32(y + 4) || memcmp(x, y, 4) || memcmp(x + 8, y + 8, 8) || memcmp(x + 0x14, y + 0x14, 5)) {
            fprintf(stderr, "  archive %u buffered tell %x/%x status %u/%u\n", i, tx, ty, get32(x + 4), get32(y + 4)); return 1;
        }
        if (memcmp(a->file_stream[i], b->file_stream[i], 4) || memcmp(a->file_stream[i] + 8, b->file_stream[i] + 8, 3)) {
            fprintf(stderr, "  archive %u file stream\n", i); return 1;
        }
    }
    for (unsigned h = 0; h < VFF_HANDLES; ++h) {
        const vff_handle *x = &a->fs.handle[h], *y = &b->fs.handle[h];
        if (x->file != y->file || (x->file >= 0 && !vfc_archive_handle(a, VFF_FIRST + (int)h) && x->pos != y->pos)) {
            fprintf(stderr, "  handle %u file %d/%d pos %u/%u\n", VFF_FIRST + h, x->file, y->file, x->pos, y->pos); return 1;
        }
    }
    if (a->fs.bump != b->fs.bump || a->fs.fail_read != b->fs.fail_read || a->fs.fail_lseek != b->fs.fail_lseek ||
        a->fs.fail_close != b->fs.fail_close || a->fs.fail_calloc != b->fs.fail_calloc) {
        fprintf(stderr, "  file system bump %u/%u faults\n", a->fs.bump, b->fs.bump); return 1;
    }
    if (a->manager != b->manager || a->counter != b->counter || a->alloc != b->alloc) {
        fprintf(stderr, "  globals manager %x/%x counter %u/%u alloc %u/%u\n", a->manager, b->manager, a->counter, b->counter, a->alloc, b->alloc);
        return 1;
    }
    if (a->log_count != b->log_count || memcmp(a->logs, b->logs, sizeof(a->logs))) {
        fprintf(stderr, "  decoder log %u/%u\n", a->log_count, b->log_count); return 1;
    }
    (void)n;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Case generation. */
static unsigned vfc_pick(unsigned n) { return random32() % n; }
static uint32_t vfc_s(unsigned o) { return VFC_ADDR(o); }
static void vfc_fresh_stream(unsigned o) {
    BYTE *s = vfc_arena + o;
    memset(s, 0, 0x30);
    put32(s, 0x4a0e10); put32(s + 4, 5); put16(s + 8, 0xffff);
    s[0x26] = (BYTE)(vfc_pick(4) ? 0 : vfc_pick(3) ? 1 : random32());
}
/* A member path variant: exact, folded case, or a near miss. */
static unsigned vfc_last_archive;                  /* archive of the last member path drawn */
static void vfc_member_path(char *out, size_t size, unsigned *length, unsigned *archive) {
    const vfc_member *m = &vfc_members[vfc_pick(vfc_member_count)];
    snprintf(out, size, "%s", m->path);
    if (length) *length = m->length;
    if (archive) *archive = m->archive;
    vfc_last_archive = m->archive;
    switch (vfc_pick(16)) {
    case 0: for (char *p = out; *p; ++p) *p = (char)tolower((unsigned char)*p); break;
    case 1: for (char *p = out; *p; ++p) if (vfc_pick(2)) *p = (char)(isupper((unsigned char)*p) ? tolower((unsigned char)*p) : toupper((unsigned char)*p)); break;
    case 2: { char *p = strchr(out, '\\'); if (p) *p = '/'; break; }
    case 3: if (strlen(out) > 1) out[strlen(out) - 1] = 0; break;
    case 4: { size_t n = strlen(out); if (n + 1 < size) { out[n] = 'X'; out[n + 1] = 0; } break; }
    case 5: { char t[64]; snprintf(t, sizeof(t), "c:%s", out); snprintf(out, size, "%s", t); break; }
    case 6: { char *p = strchr(out, '\\'); if (p) memmove(out, p + 1, strlen(p)); break; }
    default: break;
    }
}
static void vfc_path(char *out, size_t size, unsigned *length) {
    const unsigned k = vfc_pick(32);
    if (length) *length = 0;
    if (k < 22) vfc_member_path(out, size, length, NULL);
    else if (k < 26) snprintf(out, size, "%s", vfc_loose[vfc_pick(sizeof(vfc_loose) / sizeof(vfc_loose[0]))]);
    else if (k < 30) snprintf(out, size, "%s", vfc_missing[vfc_pick(sizeof(vfc_missing) / sizeof(vfc_missing[0]))]);
    else if (k < 31) { unsigned n = 250 + vfc_pick(20); memset(out, 'a', n); memcpy(out, "dts\\", 4); out[n] = 0; }
    else snprintf(out, size, "dts\\K%c.DTS", "2479"[vfc_pick(4)]);
}
static uint32_t vfc_op(uint32_t va, unsigned n, uint32_t a0, uint32_t a1, uint32_t a2) {
    const uint32_t a[3] = {a0, a1, a2};
    return (uint32_t)vfc_entry(va, n, a, vfc_zero);
}
static void vfc_set_path(const char *p) { memset(vfc_arena + VFC_PATH, 0, 0x200); memcpy(vfc_arena + VFC_PATH, p, strlen(p) < 0x1ff ? strlen(p) : 0x1ff); }
/* Opens stream o on a path of the current manager and moves it. */
static unsigned vfc_open_stream(unsigned o, int loose) {
    char p[300]; unsigned length = 0;
    if (loose) snprintf(p, sizeof(p), "%s", vfc_loose[vfc_pick(sizeof(vfc_loose) / sizeof(vfc_loose[0]))]);
    else vfc_path(p, sizeof(p), &length);
    vfc_fresh_stream(o);
    vfc_set_path(p);
    vfc_op(0x4730e8, 2, vfc_s(o), VFC_ADDR(VFC_PATH), 0);
    return length;
}
static void vfc_move_buffered(void) {
    const uint32_t b = get32((BYTE *)(uintptr_t)vfc_archive[vfc_pick(vfc_archive_count)] + 4);
    switch (vfc_pick(4)) {
    case 0: { const uint32_t a[2] = {b, random32() % 0x8000u}; vfc_entry(get32((BYTE *)(uintptr_t)get32((BYTE *)(uintptr_t)b) + 8), 2, a, vfc_zero); break; }
    case 1: { const uint32_t a[3] = {b, 1 + vfc_pick(40), VFC_BUFFER}; vfc_entry(get32((BYTE *)(uintptr_t)get32((BYTE *)(uintptr_t)b) + 0x18), 3, a, vfc_zero); break; }
    default: { char p[64]; vfc_member_path(p, sizeof(p), NULL, NULL); vfc_set_path(p);
        vfc_op(0x47cdf8, 3, vfc_archive[vfc_pick(VFC_SIM)], VFC_ADDR(VFC_PATH), VFC_ADDR(VFC_R)); break; }
    }
}
/* A member stream with status 0 and no source (kind 4 of the synthetic
 * archive, or S+0Eh cleared) makes the original read call through a null
 * object: such reads are not generated (the original faults). */
static int vfc_readable(unsigned o) {
    const BYTE *s = vfc_arena + o;
    return get32(s + 4) || get16(s + 8) != 0xffff || get32(s + 0xe);
}
static void vfc_read_op(unsigned o, uint32_t size) { if (vfc_readable(o)) vfc_op(0x4735b8, 3, vfc_s(o), size, VFC_BUFFER); }
static void vfc_random_ops(unsigned o, unsigned length) {
    for (unsigned k = vfc_pick(4); k; --k) {
        switch (vfc_pick(5)) {
        case 0: case 1: vfc_read_op(o, 1 + vfc_pick(length + 3 < 0x400 ? length + 3 : 0x400)); break;
        case 2: vfc_op(0x4733a4, 2, vfc_s(o), vfc_pick(length + 4), 0); break;
        case 3: vfc_move_buffered(); break;
        default: if (!vfc_pick(4)) { vfc_open_stream(VFC_S1, 0); vfc_read_op(VFC_S1, 1 + vfc_pick(64)); } break;
        }
    }
}
/* Field and status variations the generator applies after the pre-ops. */
/* lookup: the case will look up a member; a buffered stream in an error
 * state (3, 6) then gives the original stack garbage (B fails the seek and
 * the 9-byte read), so only status 2 is set. */
static void vfc_tweak(unsigned o, int lookup) {
    BYTE *s = vfc_arena + o;
    switch (vfc_pick(24)) {
    case 0: put32(s + 4, vfc_pick(2) ? 2 : vfc_pick(2) ? 1 : 3); break;
    case 1: put32(s + 0x16, get32(s + 0x22) + vfc_pick(3)); break;
    case 2: put32(s + 0x1a, get32(s + 0x1a) + 1); break;
    case 3: put32(s + 0xe, 0); break;
    case 4: s[0x26] = (BYTE)vfc_pick(2); break;
    case 5: case 6: {                                   /* decoder kinds through the fake decoder */
        if (!get32(s + 0xa)) break;                     /* (the original seeks S+0Ah first) */
        put32(s + 0x1e, vfc_pick(3) ? (vfc_pick(2) ? 7 : 9) : 4u + vfc_pick(3));
        put32(s + 0xe, VFC_ADDR(VFC_D));
        if (vfc_pick(2)) put32(s + 0x27, VFC_ADDR(VFC_D));
        if (vfc_pick(2)) put32(s + 0x2b, VFC_ADDR(VFC_D));
        break;
    }
    case 7: { const uint32_t b = get32(s + 0xa); if (b) put32((BYTE *)(uintptr_t)b + 4, lookup || vfc_pick(2) ? 2 : vfc_pick(2) ? 6 : 3); break; }
    case 8: { const uint32_t b = get32(s + 0xa); if (b) put32((BYTE *)(uintptr_t)b + 0x1d, get32((BYTE *)(uintptr_t)b + 0x1d) + 1 + vfc_pick(4)); break; }
    default: break;
    }
}
/* Builds hook k's case: arguments a, entry registers r; the buffer extent. */
static void vfc_build(unsigned k, uint32_t a[3], uint32_t r[3]) {
    vfc_buffer_extent = 64;
    vfc_load(&vfc_base);
    for (unsigned i = 0; i < 3; ++i) r[i] = random32();
    vff_plan = random32();
    put32(mapped + 0xa0d18, vfc_manager[vfc_pick(10) ? 0 : 1]);
    memset(vfc_arena, 0, VFC_ARENA);
    put32(vfc_arena + VFC_D, VFC_ADDR(VFC_DVT));
    put32(vfc_arena + VFC_DVT + 8, (uint32_t)(uintptr_t)vff_dec_seek); put32(vfc_arena + VFC_DVT + 0x18, (uint32_t)(uintptr_t)vff_dec_read);
    for (unsigned i = 0; i < 4; ++i) vfc_arena[VFC_R + 0x1c + i] = (BYTE)(0xa0 + i);
    char p[300]; unsigned length = 0;
    vfc_fresh_stream(VFC_S0); vfc_fresh_stream(VFC_S1);
    if (k == VFS_OPEN || k == VFS_LOOKUP || k == VFS_MEMBER) {
        if (!vfc_pick(3)) { length = vfc_open_stream(VFC_S0, !vfc_pick(4)); vfc_random_ops(VFC_S0, length); }
        if (!vfc_pick(4)) vfc_move_buffered();
        vfc_path(p, sizeof(p), NULL);
        /* The synthetic manager: its decoder kinds, the stream often already
         * holding a decoder object of an earlier open (S+27h / S+2Bh set). */
        if (k == VFS_OPEN && get32(mapped + 0xa0d18) == vfc_manager[1] && vfc_pick(4)) {
            if (vfc_pick(2)) { char q[16]; snprintf(q, sizeof(q), "dts\\K%c.DTS", vfc_pick(2) ? '7' : '9'); vfc_set_path(q);
                vfc_op(0x4730e8, 2, vfc_s(VFC_S0), VFC_ADDR(VFC_PATH), 0); }
            snprintf(p, sizeof(p), "dts\\K%c.DTS", "2479"[vfc_pick(4)]);
        }
        vfc_set_path(p);
        if (k == VFS_OPEN && !vfc_pick(30)) put32(mapped + 0xa0d18, 0);
        if (k == VFS_OPEN && !vfc_pick(3)) vfc_tweak(VFC_S0, 1);
        a[0] = k == VFS_OPEN ? vfc_s(VFC_S0) : k == VFS_LOOKUP ? get32(mapped + 0xa0d18) :
               vfc_pick(2) ? vfc_archive[vfc_last_archive] : vfc_archive[vfc_pick(vfc_archive_count)];
        a[1] = !vfc_pick(64) ? 0 : VFC_ADDR(VFC_PATH);
        a[2] = VFC_ADDR(VFC_R);
        if (k == VFS_LOOKUP && !vfc_pick(16)) { BYTE *m = vff_calloc(1, 0x79); m[0x78] = (BYTE)vfc_pick(2); put32(m, vfc_archive[0]); a[0] = (uint32_t)(uintptr_t)m; }
        if (k == VFS_MEMBER) for (unsigned i = 0; i < 0x1c; ++i) vfc_arena[VFC_R + i] = (BYTE)random32();
    } else {
        if (vfc_pick(16)) { length = vfc_open_stream(VFC_S0, !vfc_pick(6)); vfc_random_ops(VFC_S0, length ? length : 64); }
        if (!vfc_pick(3)) vfc_move_buffered();
        if (!vfc_pick(3)) vfc_tweak(VFC_S0, 0);
        a[0] = vfc_s(VFC_S0);
        if (k == VFS_READ) {
            if (!vfc_readable(VFC_S0)) put32(vfc_arena + VFC_S0 + 4, 5);
            /* A member position past length+1 makes the original read length-pos+1
             * (wrapped) bytes: not generated. */
            if (get16(vfc_arena + VFC_S0 + 8) == 0xffff && get32(vfc_arena + VFC_S0 + 0x16) > get32(vfc_arena + VFC_S0 + 0x22))
                put32(vfc_arena + VFC_S0 + 0x16, get32(vfc_arena + VFC_S0 + 0x22));
            const uint32_t pos = get32(vfc_arena + VFC_S0 + 0x16), len = get32(vfc_arena + VFC_S0 + 0x22);
            const uint32_t left = len >= pos ? len - pos : 0;
            uint32_t size;
            switch (vfc_pick(8)) {
            case 0: size = 0; break;
            case 1: size = left + vfc_pick(4); break;
            case 2: size = left; break;
            case 3: size = 1 + vfc_pick(0x2000); break;
            default: size = 1 + vfc_pick(64); break;
            }
            if (size > VFC_BUF - 16u) size = 1 + vfc_pick(64);
            /* Past-end counts read length-pos+1 bytes; keep them in the buffer. */
            if (size + pos > len && left + 1u > VFC_BUF - 16u) size = 1;
            a[1] = size; a[2] = VFC_BUFFER;
            vfc_buffer_extent = size + 8u > VFC_BUF ? VFC_BUF : size + 8u;
            if (left + 2u > vfc_buffer_extent && left + 2u <= VFC_BUF) vfc_buffer_extent = left + 2u;
        } else if (k == VFS_SEEK) {
            const uint32_t len = get32(vfc_arena + VFC_S0 + 0x22);
            a[1] = vfc_pick(16) ? vfc_pick(len + 4) : random32();
        }
    }
    if (!vfc_pick(12)) vff.fail_read = 1;
    if (!vfc_pick(12)) vff.fail_lseek = 1;
    if (!vfc_pick(12)) vff.fail_close = 1;
    if (!vfc_pick(10)) vff.fail_calloc = 1;
}

/* ------------------------------------------------------------------ */
/* Runs one case: the original against the replacement. */
static unsigned vfc_index(unsigned k) {
    static const unsigned index[VFS_HOOKS] = {ES2_HOOK_INDEX_stream_open, ES2_HOOK_INDEX_stream_close, ES2_HOOK_INDEX_stream_read,
        ES2_HOOK_INDEX_stream_seek, ES2_HOOK_INDEX_stream_tell, ES2_HOOK_INDEX_stream_size, ES2_HOOK_INDEX_vol_lookup,
        ES2_HOOK_INDEX_member_lookup};
    return index[k];
}
static unsigned vfc_group(unsigned index) { for (unsigned k = 0; k < VFS_HOOKS; ++k) if (vfc_index(k) == index) return k; return VFS_HOOKS; }
static uint64_t vfc_mask(unsigned k, uint64_t v) { return k == VFS_SEEK ? v & 0xffu : k == VFS_OPEN ? v : v & 0xffffffffu; }
static uint64_t vfc_paths[2];                         /* original results with AL 0 / AL 1 */
static int vfc_case(unsigned k, unsigned n, void *original, void *replaced, const char *what) {
    const unsigned index = vfc_index(k);
    const es2_hook_descriptor *d = function_hook_descriptor(index);
    uint32_t a[3] = {0, 0, 0}, r[3];
    vfc_build(k, a, r);
    vff_log_count = 0;
    vfc_save(&vfc_pre);
    const uint64_t want = compare_probe_call(original, index, a, r);
    vfc_save(&vfc_orig);
    vfc_load(&vfc_pre);
    const uint64_t got = replaced ? compare_probe_call(replaced, index, a, r)
                                  : function_hook_extension(index)->replacement_context(a, d->user, r);
    vfc_save(&vfc_repl);
    if (vfc_mask(k, want) != vfc_mask(k, got) || vfc_differs(&vfc_orig, &vfc_repl, n)) {
        fprintf(stderr, "FAIL: %s case %u (%s) result %016llx/%016llx args %08x %08x %08x path \"%.40s\"\n", d->name, n, what,
                (unsigned long long)want, (unsigned long long)got, a[0], a[1], a[2], (const char *)vfc_arena + VFC_PATH);
        if (vfc_mask(k, want) == vfc_mask(k, got)) vfc_differs(&vfc_orig, &vfc_repl, n);
        for (unsigned i = 0; i < vfc_archive_count; ++i) {
            const BYTE *b = vfc_pre.buffered[i];
            const int16_t h = (int16_t)get16(vfc_pre.file_stream[i] + 8);
            const vff_file *f = h >= VFF_FIRST ? &vff_files[vfc_pre.fs.handle[h - VFF_FIRST].file] : NULL;
            const uint32_t base = get32(b + 0x10), valid = get32(b + 0x19);
            if (f && base + valid <= f->size && memcmp(vfc_pre.window[i], f->data + base, valid))
                fprintf(stderr, "  archive %u window inconsistent with file at %x+%x\n", i, base, valid);
            fprintf(stderr, "  archive %u B %08x base %x valid %x cursor %x st %u -> base %x valid %x cursor %x st %u\n", i,
                    get32((BYTE *)(uintptr_t)vfc_archive[i] + 4), base, valid, get32(b + 0x1d), get32(b + 4),
                    get32(vfc_orig.buffered[i] + 0x10), get32(vfc_orig.buffered[i] + 0x19), get32(vfc_orig.buffered[i] + 0x1d),
                    get32(vfc_orig.buffered[i] + 4));
            fprintf(stderr, "  archive %u handle %d file %s pos %x/%x U status %u/%u\n", i, h, f ? f->key : "-",
                    h >= VFF_FIRST ? vfc_pre.fs.handle[h - VFF_FIRST].pos : 0, h >= VFF_FIRST ? vfc_orig.fs.handle[h - VFF_FIRST].pos : 0,
                    get32(vfc_pre.file_stream[i] + 4), get32(vfc_orig.file_stream[i] + 4));
        }
        const vfc_state *st[3] = {&vfc_pre, &vfc_orig, &vfc_repl};
        for (unsigned j = 0; j < 3; ++j) {
            fprintf(stderr, "  S0 %s:", j == 0 ? "pre " : j == 1 ? "orig" : "repl");
            for (unsigned i = 0; i < 0x30; ++i) fprintf(stderr, " %02x", st[j]->arena[VFC_S0 + i]);
            fprintf(stderr, "\n  R  %s:", j == 0 ? "pre " : j == 1 ? "orig" : "repl");
            for (unsigned i = 0; i < 0x20; ++i) fprintf(stderr, " %02x", st[j]->arena[VFC_R + i]);
            fprintf(stderr, " log %u manager %08x\n", st[j]->log_count, st[j]->manager);
        }
        for (unsigned i = 0; i < vfc_archive_count; ++i) {
            const BYTE *b = vfc_pre.buffered[i], *o = vfc_orig.buffered[i];
            fprintf(stderr, "  archive %u %08x %.16s B pre base %x valid %x cursor %x status %u; orig base %x valid %x cursor %x status %u\n",
                    i, vfc_archive[i], (const char *)(uintptr_t)(vfc_archive[i] + 8), get32(b + 0x10), get32(b + 0x19), get32(b + 0x1d),
                    get32(b + 4), get32(o + 0x10), get32(o + 0x19), get32(o + 0x1d), get32(o + 4));
        }
        return 1;
    }
    vfc_paths[(uint8_t)want ? 1 : 0]++;
    return 0;
}

/* Real-callee pass: the eight hooks in all 256 original/replaced
 * combinations; stream_open or stream_read through the (possibly hooked)
 * entry against the all-original run. Then, with no hook installed, the
 * same follow-up from both end states: an original open of another member
 * and an original read on it or on S0 (the buffered streams' empty window
 * left by the replacements must serve the original). */
static int vfc_real(unsigned k) {
    const unsigned index = vfc_index(k);
    unsigned total = 0, replaced_entries = 0, follow_ups = 0;
    for (unsigned mask = 0; mask < 256; ++mask) for (unsigned n = 0; n < 12; ++n) {
        uint32_t a[3] = {0, 0, 0}, r[3];
        current_case = mask * 12u + n; current_case_valid = 1;
        vfc_build(k, a, r);
        char p[64]; vfc_member_path(p, sizeof(p), NULL, NULL);
        const unsigned on_s1 = vfc_pick(2), size = 1 + vfc_pick(48);
        vff_log_count = 0;
        vfc_save(&vfc_pre);
        const uint64_t want = compare_probe_call(mapped + vfs_rvas[k], index, a, r);
        vfc_save(&vfc_orig);
        static vfc_state follow_orig, follow_repl;
        vfc_fresh_stream(VFC_S1); vfc_set_path(p);
        BYTE fresh[0x30]; memcpy(fresh, vfc_arena + VFC_S1, sizeof(fresh));
        vfc_op(0x4730e8, 2, vfc_s(VFC_S1), VFC_ADDR(VFC_PATH), 0);
        vfc_read_op(on_s1 ? VFC_S1 : VFC_S0, size);
        vfc_save(&follow_orig);
        vfc_load(&vfc_pre);
        es2_hook hooks[VFS_HOOKS]; memset(hooks, 0, sizeof(hooks));
        es2_hook_options o = {0}; o.image = mapped; o.expected_base = 0x400000; o.expected_image_size = 0x2d8000;
        for (unsigned j = 0; j < VFS_HOOKS; ++j) {
            o.mode = mask >> j & 1 ? ES2_HOOK_REPLACED : ES2_HOOK_ORIGINAL;
            if (!weapons_install_hook(hooks + j, function_hook_descriptor(vfc_index(j)), &o, function_hook_extension(vfc_index(j))))
                return fail("vfs real install");
        }
        const uint64_t got = compare_probe_call(mapped + vfs_rvas[k], index, a, r);
        for (unsigned j = 0; j < VFS_HOOKS; ++j) {
            replaced_entries += (unsigned)hooks[j].calls;
            if (!es2_hook_uninstall(hooks + j)) return fail("vfs real uninstall");
        }
        vfc_save(&vfc_repl);
        int bad = vfc_mask(k, want) != vfc_mask(k, got) || vfc_differs(&vfc_orig, &vfc_repl, n);
        /* A buffered stream left in an error state (3, 6) gives the original
         * lookup stack garbage: no follow-up then. */
        int clean = 1;
        for (unsigned i = 0; i < vfc_archive_count; ++i) clean &= get32(vfc_orig.buffered[i] + 4) == 0 || get32(vfc_orig.buffered[i] + 4) == 2;
        if (!bad && clean) {
            ++follow_ups;
            memcpy(vfc_arena + VFC_S1, fresh, sizeof(fresh)); vfc_set_path(p);
            vfc_op(0x4730e8, 2, vfc_s(VFC_S1), VFC_ADDR(VFC_PATH), 0);
            vfc_read_op(on_s1 ? VFC_S1 : VFC_S0, size);
            vfc_save(&follow_repl);
            bad = vfc_differs(&follow_orig, &follow_repl, n);
            if (bad) {
                fprintf(stderr, "  (follow-up open of \"%s\" and read)\n", p);
                for (unsigned i = 0; i < vfc_archive_count; ++i) {
                    const BYTE *x = vfc_orig.buffered[i], *y = vfc_repl.buffered[i], *u = follow_orig.buffered[i], *v = follow_repl.buffered[i];
                    fprintf(stderr, "  archive %u B base/valid/cursor/status %x/%x/%x/%u vs %x/%x/%x/%u -> %x/%x/%x/%u vs %x/%x/%x/%u U %u/%u\n", i,
                            get32(x + 0x10), get32(x + 0x19), get32(x + 0x1d), get32(x + 4), get32(y + 0x10), get32(y + 0x19), get32(y + 0x1d), get32(y + 4),
                            get32(u + 0x10), get32(u + 0x19), get32(u + 0x1d), get32(u + 4), get32(v + 0x10), get32(v + 0x19), get32(v + 0x1d), get32(v + 4),
                            get32(vfc_orig.file_stream[i] + 4), get32(vfc_repl.file_stream[i] + 4));
                }
            }
        }
        if (bad) {
            fprintf(stderr, "FAIL: %s case %u (real-callee, mask %02x) result %016llx/%016llx\n", function_hook_descriptor(index)->name,
                    n, mask, (unsigned long long)want, (unsigned long long)got);
            return 1;
        }
        ++total;
    }
    current_case_valid = 0;
    if (follow_ups < total / 2) return fail("vfs real pass: too few follow-ups");
    printf("PASS: %s %u real-callee cases (8 hooks in all 256 original/replaced combinations, %u hook entries), %u followed by "
           "an original open and read from both end states\n", function_hook_descriptor(index)->name, total, replaced_entries, follow_ups);
    return 0;
}

/* The case from a stack filled with a pattern: EAX = EDX = ECX = pattern. */
__attribute__((naked)) static void vfc_poison_call(void *target, const uint32_t *a, uint32_t pattern, uint32_t out[2]) {
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Installed replaced hook against the original, both from a 31/a7 stack,
 * fast and preserving dispatch. */
static int vfc_poison(unsigned k) {
    const unsigned index = vfc_index(k);
    const es2_hook_descriptor *d = function_hook_descriptor(index);
    unsigned total = 0;
    for (unsigned fast = 0; fast < 2; ++fast) for (unsigned n = 0; n < 128; ++n) {
        uint32_t a[3] = {0, 0, 0}, r[3], want[2], got[2], pattern = n & 1 ? 0xa7a7a7a7u : 0x31313131u;
        current_case = n; current_case_valid = 1;
        vfc_build(k, a, r);
        vff_log_count = 0;
        vfc_save(&vfc_pre);
        vfc_poison_call(mapped + vfs_rvas[k], a, pattern, want);
        vfc_save(&vfc_orig);
        vfc_load(&vfc_pre);
        es2_hook h = {0}; es2_hook_options o = {0};
        o.image = mapped; o.expected_base = 0x400000; o.expected_image_size = 0x2d8000; o.mode = ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);
        if (!weapons_install_hook(&h, d, &o, function_hook_extension(index))) return fail("vfs poison install");
        vfc_poison_call(mapped + vfs_rvas[k], a, pattern, got);
        const uint64_t calls = h.calls;
        if (!es2_hook_uninstall(&h)) return fail("vfs poison uninstall");
        es2_hook_set_fast_path(TRUE);
        vfc_save(&vfc_repl);
        const uint64_t w = (uint64_t)want[1] << 32 | want[0], g = (uint64_t)got[1] << 32 | got[0];
        if (vfc_mask(k, w) != vfc_mask(k, g) || calls != 1 || vfc_differs(&vfc_orig, &vfc_repl, n)) {
            fprintf(stderr, "FAIL: %s case %u (poison, %s) result %016llx/%016llx calls %llu\n", d->name, n, fast ? "fast" : "preserving",
                    (unsigned long long)w, (unsigned long long)g, (unsigned long long)calls);
            return 1;
        }
        ++total;
    }
    current_case_valid = 0;
    printf("PASS: %s %u installed 31/a7 poison cases, fast/preserving dispatch\n", d->name, total);
    return 0;
}

/* Compare mode, the hook alone: the original's result and state are kept,
 * one call per case, no bypass; exact replacement 0 mismatches; skewed
 * replacement one mismatch per compared call. Skew: EAX bit 0, or for
 * stream_read a byte of the original's buffer flipped while the
 * replacement compares (restored afterwards). */
static es2_hook_replacement_context vfc_exact;
static unsigned vfc_skew_bytes, vfc_skew_byte_hits;
static uint64_t __cdecl vfc_skew(const uint32_t *a, void *u, const uint32_t *r) {
    BYTE *flip = vfc_skew_bytes && a[1] && a[2] ? (BYTE *)(uintptr_t)a[2] : NULL;
    if (flip) *flip ^= 0x40u;
    uint64_t result = vfc_exact(a, u, r);
    if (flip) *flip ^= 0x40u;
    if (flip && vfs_last_differences) ++vfc_skew_byte_hits;
    else if (!vfs_last_skip) result ^= 1u;
    return result;
}
static int vfc_compare(unsigned k) {
    const unsigned index = vfc_index(k);
    const es2_hook_descriptor *d = function_hook_descriptor(index);
    const es2_hook_extension *x = function_hook_extension(index);
    es2_hook_extension skewed = *x;
    vfc_exact = x->replacement_context; skewed.replacement_context = vfc_skew;
    uint64_t skipped[2] = {0, 0}, mismatches[2] = {0, 0}, compared[2] = {0, 0};
    vfc_skew_byte_hits = 0;
    for (unsigned skew = 0; skew < 2; ++skew) for (unsigned n = 0; n < 1024; ++n) {
        uint32_t a[3] = {0, 0, 0}, r[3];
        current_case = n; current_case_valid = 1;
        vfc_build(k, a, r);
        vfc_skew_bytes = k == VFS_READ && (n & 1);
        vff_log_count = 0;
        vfc_save(&vfc_pre);
        const uint64_t want = compare_probe_call(mapped + vfs_rvas[k], index, a, r);
        vfc_save(&vfc_orig);
        vfc_load(&vfc_pre);
        es2_hook h = {0}; es2_hook_options o = {0};
        o.image = mapped; o.expected_base = 0x400000; o.expected_image_size = 0x2d8000; o.mode = ES2_HOOK_COMPARE;
        if (!weapons_install_hook(&h, d, &o, skew ? &skewed : x)) return fail("vfs compare install");
        const uint64_t got = compare_probe_call(mapped + vfs_rvas[k], index, a, r);
        if (!es2_hook_uninstall(&h)) return fail("vfs compare uninstall");
        vfc_save(&vfc_repl);
        const int bad = got != want || h.calls != 1 || h.bypassed || h.mismatches + h.skipped > 1 ||
                        (skew ? h.mismatches + h.skipped != 1 : h.mismatches != 0) || vfc_differs(&vfc_orig, &vfc_repl, n);
        if (bad) {
            fprintf(stderr, "FAIL: %s case %u (compare, %s) result %016llx/%016llx calls %llu mismatches %llu skipped %llu bypassed %llu\n",
                    d->name, n, skew ? "skewed" : "exact", (unsigned long long)want, (unsigned long long)got, (unsigned long long)h.calls,
                    (unsigned long long)h.mismatches, (unsigned long long)h.skipped, (unsigned long long)h.bypassed);
            return 1;
        }
        skipped[skew] += h.skipped; mismatches[skew] += h.mismatches; compared[skew] += h.calls - h.skipped;
    }
    current_case_valid = 0;
    if (!compared[0]) return fail("vfs compare: no compared call");
    printf("PASS: %s compare mode: 1024 exact cases (%llu compared, 0 mismatches, %llu skipped), 1024 skewed cases (%llu mismatches, "
           "%llu skipped; %u by a buffer byte), original result and state kept\n", d->name, (unsigned long long)compared[0],
           (unsigned long long)skipped[0], (unsigned long long)mismatches[1], (unsigned long long)skipped[1], vfc_skew_byte_hits);
    return 0;
}

static int vfs_cases(unsigned index, unsigned limit) {
    if (!vfc_ready && vfc_setup()) return 1;
    current_function = function_hook_descriptor(index)->name;
    const unsigned k = vfc_group(index);
    if (k == VFS_HOOKS) return fail("vfs index");
    if (vfc_patch(1)) return 1;
    static int based;
    if (!based) { vfc_save(&vfc_base); based = 1; }
    vfc_load(&vfc_base);
    int status = 0;
    const unsigned cases = case_budget(limit, 200000);
    if (!limit) coverage_enable(1);
    for (unsigned n = 0; n < cases && !status; ++n) {
        current_case = n; current_case_valid = 1;
        if (!limit && n == 16384) coverage_enable(0);
        status = limit ? vfc_case(k, n, rng_hooks[index].trampoline, mapped + vfs_rvas[k], "installed")
                       : vfc_case(k, n, mapped + vfs_rvas[k], NULL, "direct");
    }
    if (!limit) coverage_enable(0);
    current_case_valid = 0;
    if (!status) printf("PASS: %-20s %u %s cases (%llu AL=1); EAX/EDX, streams, records, buffer, buffered tells, file system, counters, decoder log\n",
                        function_hook_descriptor(index)->name, cases, limit ? "installed" : "direct", (unsigned long long)vfc_paths[1]);
    vfc_paths[0] = vfc_paths[1] = 0;
    if (!limit && !status && (k == VFS_OPEN || k == VFS_READ)) status = vfc_real(k);
    if (!limit && !status) status = vfc_poison(k);
    if (!limit && !status) status = vfc_compare(k);
    vfc_load(&vfc_base);
    if (vfc_patch(0)) return 1;
    return status;
}
