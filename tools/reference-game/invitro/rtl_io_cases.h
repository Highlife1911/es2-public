/* The CW3220 file and console imports against core/src/rtl_io.c, in vitro.
 *
 * CW3220.DLL is mapped as in rtl_cases.h (an SHA-256 gate, relocations,
 * every import a named trap, no entry point). The KERNEL32 slots the I/O code
 * uses then point to an emulated OS with an in-memory file system (files with
 * attributes, a character device, a read-only file, a file whose reads fail,
 * a small "disk full" file, a sink; handles with access rights and positions;
 * a find iterator with '*'/'?' patterns; a scripted clock) that logs every
 * call with its arguments in normalised form (stack structures by content).
 * VirtualAlloc/VirtualFree serve the heap from a reserved window. CW3220's
 * own initialisation of the heap (4170fc), of the handle table (409014: with
 * SetHandleCount, GetStartupInfoA, GetStdHandle, GetFileType) and of the
 * standard streams (407248) runs at the start of every sequence; the module's
 * _fmode pointer is set to a word of the test (0x4000 as in DBSIM, 0x8000 or
 * 0), the time zone to EST or UTC with or without summer time.
 *
 * Direct cases: sequences of generated operations over the 21 exports (handle
 * family with every flag combination, FILE family with every mode, fscanf
 * with a format grammar, find patterns and attribute masks, time over
 * 1965-2040 and the summer-time boundaries, the console stubs). Each
 * operation runs on the export from a saved state, then on the model from
 * the same state: EAX, the logged OS calls, CW3220's DATA section (handle
 * table, FILE array, errno, find list, exit hooks), the heap window, the test
 * arena and the emulated OS must be equal. For the console stubs only EAX
 * is compared and the stub must change nothing. The sequence continues from
 * the export's state.
 *
 * Installed cases: the proxy's shim on the mapped DBSIM (CW3220 slots at the
 * mapped exports): the 18 file imports in mode shadow, then all 21 in mode
 * replaced; each sequence runs once without the shim and once with it through
 * DBSIM's thunks from the same start: EAX after every step and the final
 * state equal, 0 shadow mismatches. A planted model error (a wrong errno
 * address in the shim's layout) must be caught once.
 * Selection: --rtl-io or rtl_io_<name> (mutation runs). */

/* ------------------------------------------------------------------------
 * Emulated OS. */
#define VOS_FILES 24
#define VOS_DATA 2048u
#define VOS_HANDLES 60
#define VOS_HANDLE_BASE 0x1000u
#define VOS_FIND_BASE 0x5000u
#define VOS_LOG 512
enum { VOS_DISK, VOS_DEVICE, VOS_FAULTY, VOS_SINK, VOS_FULL, VOS_PIPE };
typedef struct vos_file { char name[24]; uint32_t attributes, size, kind, time[2]; uint8_t data[VOS_DATA]; uint8_t used; } vos_file;
typedef struct vos_handle { uint32_t open, file, position, access, find, cursor; char pattern[24]; } vos_handle;
typedef struct vos_state {
    vos_file files[VOS_FILES];
    vos_handle handles[VOS_HANDLES];
    uint32_t last_error, set_handle_count;
    SYSTEMTIME clock;
    uint32_t vm_cursor, vm_committed, vm_limit;   /* vm_limit: commits beyond it fail (0: none) */
} vos_state;
static vos_state vos;
typedef struct vos_entry { uint32_t fn, a[6], result, count, nbytes; uint8_t bytes[48]; } vos_entry;
static vos_entry vos_log[2][VOS_LOG];
static unsigned vos_log_count[2], vos_which, vos_unexpected;
static BYTE *vos_window;
#define VOS_WINDOW (16u << 20)

static vos_entry *vos_entry_new(uint32_t fn) {
    unsigned *n = &vos_log_count[vos_which];
    static vos_entry spare;
    vos_entry *e = *n < VOS_LOG ? &vos_log[vos_which][*n] : &spare;
    ++*n;
    memset(e, 0, sizeof(*e));
    e->fn = fn;
    return e;
}
static int vos_find_file(const char *name) {
    for (int i = 0; i < VOS_FILES; ++i) if (vos.files[i].used && !strcmp(vos.files[i].name, name)) return i;
    return -1;
}
static int vos_handle_of(HANDLE h, int find) {
    uint32_t v = (uint32_t)(uintptr_t)h, base = find ? VOS_FIND_BASE : VOS_HANDLE_BASE;
    if (v < base || (v - base) % 0x10u || (v - base) / 0x10u >= VOS_HANDLES) return -1;
    int k = (int)((v - base) / 0x10u);
    return vos.handles[k].open && (uint32_t)vos.handles[k].find == (uint32_t)find ? k : -1;
}
static int vos_new_handle(void) { for (int k = 0; k < VOS_HANDLES; ++k) if (!vos.handles[k].open) return k; return -1; }
static int vos_match(const char *pattern, const char *name) {
    if (!*pattern) return !*name;
    if (*pattern == '*') return vos_match(pattern + 1, name) || (*name && vos_match(pattern, name + 1));
    if (*name && (*pattern == '?' || *pattern == *name)) return vos_match(pattern + 1, name + 1);
    return 0;
}
static uint32_t vos_capacity(const vos_file *f) { return f->kind == VOS_FULL ? 24u : VOS_DATA; }

static HANDLE WINAPI vos_CreateFileA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD attributes, HANDLE t) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_CREATE_FILE);
    e->a[0] = (uint32_t)(uintptr_t)path; e->a[1] = access; e->a[2] = share;
    e->a[3] = sa ? (sa->nLength == 12 && !sa->lpSecurityDescriptor ? (uint32_t)sa->bInheritHandle : 0xeeeeeeeeu) : 0xffffffffu;
    e->a[4] = disposition; e->a[5] = attributes | (t ? 0x80000000u : 0u);
    HANDLE result = INVALID_HANDLE_VALUE;
    int f = vos_find_file(path), k = vos_new_handle();
    if (!strcmp(path, "busy")) vos.last_error = 0x6e;
    else if (!strcmp(path, "huge")) vos.last_error = 998;   /* above the DOS error map */
    else if (strlen(path) >= 24 || !*path) vos.last_error = 123;
    else if (k < 0) vos.last_error = 4;
    else if (disposition == CREATE_NEW && f >= 0) vos.last_error = 80;
    else if ((disposition == OPEN_EXISTING || disposition == TRUNCATE_EXISTING) && f < 0) vos.last_error = 2;
    else if (f >= 0 && (vos.files[f].attributes & 1u) && (access & GENERIC_WRITE)) vos.last_error = 5;
    else if (disposition < 1 || disposition > 5) vos.last_error = 87;
    else {
        if (f < 0) {
            for (f = 0; f < VOS_FILES && vos.files[f].used; ++f) {}
            if (f == VOS_FILES) { vos.last_error = 112; goto done; }
            memset(&vos.files[f], 0, sizeof(vos.files[f]));
            vos.files[f].used = 1; strcpy(vos.files[f].name, path);
            vos.files[f].attributes = (attributes & 1u) ? 1u : 0x20u;
            vos.files[f].time[0] = 0x1234567u; vos.files[f].time[1] = 0x01b0e000u;
        }
        if ((disposition == CREATE_ALWAYS || disposition == TRUNCATE_EXISTING) && vos.files[f].kind != VOS_SINK) vos.files[f].size = 0;
        vos_handle *h = &vos.handles[k];
        memset(h, 0, sizeof(*h));
        h->open = 1; h->file = (uint32_t)f; h->access = access;
        result = (HANDLE)(uintptr_t)(VOS_HANDLE_BASE + 0x10u * (uint32_t)k);
    }
done:
    e->result = (uint32_t)(uintptr_t)result;
    return result;
}
static BOOL WINAPI vos_CloseHandle(HANDLE h) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_CLOSE_HANDLE);
    e->a[0] = (uint32_t)(uintptr_t)h;
    int k = vos_handle_of(h, 0);
    if (k < 0) { vos.last_error = 6; return FALSE; }
    if (!strcmp(vos.files[vos.handles[k].file].name, "noclose")) { vos.last_error = 5; return FALSE; }
    vos.handles[k].open = 0;
    e->result = 1;
    return TRUE;
}
static BOOL WINAPI vos_ReadFile(HANDLE h, LPVOID buffer, DWORD n, LPDWORD got, LPOVERLAPPED o) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_READ_FILE);
    e->a[0] = (uint32_t)(uintptr_t)h; e->a[1] = (uint32_t)(uintptr_t)buffer; e->a[2] = n; e->a[3] = (uint32_t)(uintptr_t)o;
    if (got) *got = 0;
    int k = vos_handle_of(h, 0);
    if (k < 0) { vos.last_error = 6; return FALSE; }
    vos_handle *hd = &vos.handles[k];
    vos_file *f = &vos.files[hd->file];
    if (!(hd->access & GENERIC_READ)) { vos.last_error = 5; return FALSE; }
    if (f->kind == VOS_FAULTY && hd->position >= 3u) { vos.last_error = 23; return FALSE; }
    uint32_t count = 0;
    while (count < n && hd->position + count < f->size) { ((BYTE *)buffer)[count] = f->data[hd->position + count]; ++count; }
    hd->position += count;
    if (got) *got = count;
    e->count = count; e->result = 1;
    return TRUE;
}
static BOOL WINAPI vos_WriteFile(HANDLE h, LPCVOID buffer, DWORD n, LPDWORD written, LPOVERLAPPED o) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_WRITE_FILE);
    e->a[0] = (uint32_t)(uintptr_t)h; e->a[2] = n; e->a[3] = (uint32_t)(uintptr_t)o;
    e->nbytes = n;
    for (uint32_t i = 0; i < n && i < sizeof(e->bytes); ++i) e->bytes[i] = ((const BYTE *)buffer)[i];
    if (written) *written = 0;
    int k = vos_handle_of(h, 0);
    if (k < 0) { vos.last_error = 6; return FALSE; }
    vos_handle *hd = &vos.handles[k];
    vos_file *f = &vos.files[hd->file];
    if (!(hd->access & GENERIC_WRITE)) { vos.last_error = 5; return FALSE; }
    uint32_t count = n;
    if (f->kind != VOS_SINK) {
        uint32_t room = hd->position < vos_capacity(f) ? vos_capacity(f) - hd->position : 0u;
        if (count > room) count = room;
        for (uint32_t i = 0; i < count; ++i) f->data[hd->position + i] = ((const BYTE *)buffer)[i];
        if (count && hd->position + count > f->size) {
            for (uint32_t i = f->size; i < hd->position; ++i) f->data[i] = 0;
            f->size = hd->position + count;
        }
        hd->position += count;
    }
    if (written) *written = count;
    e->count = count; e->result = 1;
    return TRUE;
}
static DWORD WINAPI vos_SetFilePointer(HANDLE h, LONG distance, PLONG high, DWORD method) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_SET_FILE_POINTER);
    e->a[0] = (uint32_t)(uintptr_t)h; e->a[1] = (uint32_t)distance; e->a[2] = (uint32_t)(uintptr_t)high; e->a[3] = method;
    e->result = 0xffffffffu;
    int k = vos_handle_of(h, 0);
    if (k < 0) { vos.last_error = 6; return 0xffffffffu; }
    vos_handle *hd = &vos.handles[k];
    uint32_t kind = vos.files[hd->file].kind;
    if ((kind == VOS_PIPE && method == FILE_BEGIN) || (kind == VOS_DEVICE && method == FILE_END)) { vos.last_error = 132; return 0xffffffffu; }
    int64_t base = method == FILE_BEGIN ? 0 : method == FILE_CURRENT ? hd->position : method == FILE_END ? vos.files[hd->file].size : -1;
    if (base < 0) { vos.last_error = 87; return 0xffffffffu; }
    int64_t to = base + distance;
    if (to < 0 || to > 0x7fffffff) { vos.last_error = 131; return 0xffffffffu; }
    hd->position = (uint32_t)to;
    e->result = (uint32_t)to;
    return (DWORD)to;
}
static DWORD WINAPI vos_GetFileSize(HANDLE h, LPDWORD high) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_GET_FILE_SIZE);
    e->a[0] = (uint32_t)(uintptr_t)h; e->a[1] = (uint32_t)(uintptr_t)high;
    int k = vos_handle_of(h, 0);
    if (k < 0) { vos.last_error = 6; e->result = 0xffffffffu; return 0xffffffffu; }
    e->result = vos.files[vos.handles[k].file].size;
    return e->result;
}
static DWORD WINAPI vos_GetFileType(HANDLE h) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_GET_FILE_TYPE);
    e->a[0] = (uint32_t)(uintptr_t)h;
    int k = vos_handle_of(h, 0);
    if (k < 0) { vos.last_error = 6; return 0; }
    e->result = vos.files[vos.handles[k].file].kind == VOS_DEVICE ? 2u : 1u;
    return e->result;
}
static DWORD WINAPI vos_GetFileAttributesA(LPCSTR path) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_GET_FILE_ATTRIBUTES);
    e->a[0] = (uint32_t)(uintptr_t)path;
    int f = vos_find_file(path);
    if (f < 0) { vos.last_error = 2; e->result = 0xffffffffu; return 0xffffffffu; }
    e->result = vos.files[f].attributes;
    return e->result;
}
static DWORD WINAPI vos_GetLastError(void) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_GET_LAST_ERROR);
    e->result = vos.last_error;
    return vos.last_error;
}
static void vos_fill_find(WIN32_FIND_DATAA *d, const vos_file *f) {
    memset(d, 0, sizeof(*d));
    d->dwFileAttributes = f->attributes;
    d->ftLastWriteTime.dwLowDateTime = f->time[0]; d->ftLastWriteTime.dwHighDateTime = f->time[1];
    d->nFileSizeLow = f->size;
    strcpy(d->cFileName, f->name);
}
static BOOL vos_find_step(vos_handle *h, WIN32_FIND_DATAA *d) {
    for (; h->cursor < VOS_FILES; ++h->cursor) {
        const vos_file *f = &vos.files[h->cursor];
        if (f->used && f->name[0] != '<' && vos_match(h->pattern, f->name)) { vos_fill_find(d, f); ++h->cursor; return TRUE; }
    }
    return FALSE;
}
static HANDLE WINAPI vos_FindFirstFileA(LPCSTR pattern, WIN32_FIND_DATAA *d) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_FIND_FIRST);
    e->a[0] = (uint32_t)(uintptr_t)pattern;
    memset(d, 0x5a, sizeof(*d));
    int k = vos_new_handle();
    const char *p = strrchr(pattern, '\\');
    p = p ? p + 1 : pattern;
    if (k < 0 || strlen(p) >= 24) { vos.last_error = 4; e->result = 0xffffffffu; return INVALID_HANDLE_VALUE; }
    vos_handle *h = &vos.handles[k];
    memset(h, 0, sizeof(*h));
    h->open = 1; h->find = 1; strcpy(h->pattern, p);
    if (!vos_find_step(h, d)) { h->open = 0; vos.last_error = 2; e->result = 0xffffffffu; return INVALID_HANDLE_VALUE; }
    e->result = VOS_FIND_BASE + 0x10u * (uint32_t)k;
    for (unsigned i = 0; i < 16; ++i) e->bytes[i] = ((const BYTE *)d)[0x2c + i];
    return (HANDLE)(uintptr_t)e->result;
}
static BOOL WINAPI vos_FindNextFileA(HANDLE handle, WIN32_FIND_DATAA *d) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_FIND_NEXT);
    e->a[0] = (uint32_t)(uintptr_t)handle;
    int k = vos_handle_of(handle, 1);
    if (k < 0) { vos.last_error = 6; return FALSE; }
    if (!vos_find_step(&vos.handles[k], d)) { vos.last_error = 18; return FALSE; }
    e->result = 1;
    for (unsigned i = 0; i < 16; ++i) e->bytes[i] = ((const BYTE *)d)[0x2c + i];
    return TRUE;
}
static BOOL WINAPI vos_FindClose(HANDLE handle) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_FIND_CLOSE);
    e->a[0] = (uint32_t)(uintptr_t)handle;
    int k = vos_handle_of(handle, 1);
    if (k < 0) { vos.last_error = 6; return FALSE; }
    vos.handles[k].open = 0;
    e->result = 1;
    return TRUE;
}
static BOOL WINAPI vos_FileTimeToLocalFileTime(const FILETIME *in, LPFILETIME out) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_FILE_TIME_TO_LOCAL);
    e->a[0] = in->dwLowDateTime; e->a[1] = in->dwHighDateTime;
    uint64_t v = ((uint64_t)in->dwHighDateTime << 32 | in->dwLowDateTime) - 180000000000ull;
    out->dwLowDateTime = (DWORD)v; out->dwHighDateTime = (DWORD)(v >> 32);
    e->result = 1;
    return TRUE;
}
static BOOL WINAPI vos_FileTimeToDosDateTime(const FILETIME *ft, LPWORD date, LPWORD time) {
    vos_entry *e = vos_entry_new(ES2_IO_OS_FILE_TIME_TO_DOS);
    e->a[0] = ft->dwLowDateTime; e->a[1] = ft->dwHighDateTime; e->a[2] = (uint32_t)(uintptr_t)date; e->a[3] = (uint32_t)(uintptr_t)time;
    e->result = (uint32_t)FileTimeToDosDateTime(ft, date, time);
    return (BOOL)e->result;
}
static void WINAPI vos_GetLocalTime(LPSYSTEMTIME st) { vos_entry_new(ES2_IO_OS_GET_LOCAL_TIME); *st = vos.clock; }
static UINT WINAPI vos_SetHandleCount(UINT n) { return n < vos.set_handle_count ? n : vos.set_handle_count; }
static void WINAPI vos_GetStartupInfoA(LPSTARTUPINFOA si) { memset(si, 0, sizeof(*si)); si->cb = sizeof(*si); }
static HANDLE WINAPI vos_GetStdHandle(DWORD which) {
    return (HANDLE)(uintptr_t)(which == STD_INPUT_HANDLE ? VOS_HANDLE_BASE : which == STD_OUTPUT_HANDLE ? VOS_HANDLE_BASE + 0x10u :
                               which == STD_ERROR_HANDLE ? VOS_HANDLE_BASE + 0x20u : 0xffffffffu);
}
static LPVOID WINAPI vos_VirtualAlloc(LPVOID address, SIZE_T size, DWORD type, DWORD protect) {
    uint32_t base = (uint32_t)(uintptr_t)vos_window;
    if (type == MEM_RESERVE && !address && protect == PAGE_NOACCESS) {
        if (vos.vm_cursor + size > VOS_WINDOW) return NULL;
        uint32_t result = base + vos.vm_cursor;
        vos.vm_cursor += (uint32_t)size;
        return (LPVOID)(uintptr_t)result;
    }
    if (type == MEM_COMMIT && (uint32_t)(uintptr_t)address - base < VOS_WINDOW) {
        if (vos.vm_limit && (uint32_t)(uintptr_t)address - base + (uint32_t)size > vos.vm_limit) return NULL;
        LPVOID r = VirtualAlloc(address, size, MEM_COMMIT, protect);
        uint32_t end = (uint32_t)(uintptr_t)address - base + (uint32_t)size;
        if (r && end > vos.vm_committed) vos.vm_committed = end;
        return r;
    }
    ++vos_unexpected;
    return NULL;
}
static BOOL WINAPI vos_VirtualFree(LPVOID address, SIZE_T size, DWORD type) {
    if (type == MEM_DECOMMIT && (uint32_t)(uintptr_t)address - (uint32_t)(uintptr_t)vos_window < VOS_WINDOW) return TRUE;   /* kept committed */
    (void)size;
    ++vos_unexpected;
    return FALSE;
}

/* CW3220's KERNEL32 slots by name. */
static uint32_t *io_cw_slot(const char *name) {
    for (uint32_t e = 0; e < 128u; ++e) {
        uint32_t entry = get32(rtl_cw + RTL_HEAP_K32_LOOKUP + 4u * e);
        if (!entry) break;
        if (!(entry & 0x80000000u) && !strcmp((const char *)rtl_cw + entry + 2, name)) return (uint32_t *)(rtl_cw + RTL_HEAP_K32_IAT + 4u * e);
    }
    return NULL;
}
static int io_hook_os(void) {
    static const struct { const char *name; void *function; } os[] = {
        {"CreateFileA", (void *)vos_CreateFileA}, {"CloseHandle", (void *)vos_CloseHandle}, {"ReadFile", (void *)vos_ReadFile},
        {"WriteFile", (void *)vos_WriteFile}, {"SetFilePointer", (void *)vos_SetFilePointer}, {"GetFileSize", (void *)vos_GetFileSize},
        {"GetFileType", (void *)vos_GetFileType}, {"GetFileAttributesA", (void *)vos_GetFileAttributesA},
        {"GetLastError", (void *)vos_GetLastError}, {"FindFirstFileA", (void *)vos_FindFirstFileA}, {"FindNextFileA", (void *)vos_FindNextFileA},
        {"FindClose", (void *)vos_FindClose}, {"FileTimeToLocalFileTime", (void *)vos_FileTimeToLocalFileTime},
        {"FileTimeToDosDateTime", (void *)vos_FileTimeToDosDateTime}, {"GetLocalTime", (void *)vos_GetLocalTime},
        {"SetHandleCount", (void *)vos_SetHandleCount}, {"GetStartupInfoA", (void *)vos_GetStartupInfoA}, {"GetStdHandle", (void *)vos_GetStdHandle},
        {"VirtualAlloc", (void *)vos_VirtualAlloc}, {"VirtualFree", (void *)vos_VirtualFree},
    };
    for (unsigned i = 0; i < sizeof(os) / sizeof(os[0]); ++i) {
        uint32_t *slot = io_cw_slot(os[i].name);
        if (!slot) return fail("CW3220 KERNEL32 import missing");
        *slot = (uint32_t)(uintptr_t)os[i].function;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * The model's direct env: memory as is, the same emulated OS, CW3220's
 * allocator. */
static uint32_t io_malloc_export, io_free_export;
static es2_io_layout io_layout;
static uint8_t io_read8(void *context, uint32_t a) { (void)context; return *(const volatile BYTE *)(uintptr_t)a; }
static void io_write8(void *context, uint32_t a, uint8_t v) { (void)context; *(volatile BYTE *)(uintptr_t)a = v; }
static void io_os(void *context, es2_io_os *call) {
    (void)context;
    uint32_t *a = call->arg;
    switch (call->function) {
    case ES2_IO_OS_CREATE_FILE: {
        SECURITY_ATTRIBUTES sa = {12, NULL, (BOOL)a[3]};
        call->result = (uint32_t)(uintptr_t)vos_CreateFileA((LPCSTR)(uintptr_t)a[0], a[1], a[2], &sa, a[4], a[5], NULL);
        break;
    }
    case ES2_IO_OS_CLOSE_HANDLE: call->result = (uint32_t)vos_CloseHandle((HANDLE)(uintptr_t)a[0]); break;
    case ES2_IO_OS_READ_FILE: {
        DWORD got = 0;
        call->result = (uint32_t)vos_ReadFile((HANDLE)(uintptr_t)a[0], (LPVOID)(uintptr_t)a[1], a[2], &got, NULL);
        call->count = got;
        break;
    }
    case ES2_IO_OS_WRITE_FILE: {
        DWORD written = 0;
        call->result = (uint32_t)vos_WriteFile((HANDLE)(uintptr_t)a[0], a[1] ? (LPCVOID)(uintptr_t)a[1] : (LPCVOID)call->data, a[2], &written, NULL);
        call->count = written;
        break;
    }
    case ES2_IO_OS_SET_FILE_POINTER: call->result = vos_SetFilePointer((HANDLE)(uintptr_t)a[0], (LONG)a[1], NULL, a[2]); break;
    case ES2_IO_OS_GET_FILE_SIZE: call->result = vos_GetFileSize((HANDLE)(uintptr_t)a[0], NULL); break;
    case ES2_IO_OS_GET_FILE_TYPE: call->result = vos_GetFileType((HANDLE)(uintptr_t)a[0]); break;
    case ES2_IO_OS_GET_FILE_ATTRIBUTES: call->result = vos_GetFileAttributesA((LPCSTR)(uintptr_t)a[0]); break;
    case ES2_IO_OS_GET_LAST_ERROR: call->result = vos_GetLastError(); break;
    case ES2_IO_OS_FIND_FIRST: call->result = (uint32_t)(uintptr_t)vos_FindFirstFileA((LPCSTR)(uintptr_t)a[0], (WIN32_FIND_DATAA *)call->out); break;
    case ES2_IO_OS_FIND_NEXT: call->result = (uint32_t)vos_FindNextFileA((HANDLE)(uintptr_t)a[0], (WIN32_FIND_DATAA *)call->out); break;
    case ES2_IO_OS_FIND_CLOSE: call->result = (uint32_t)vos_FindClose((HANDLE)(uintptr_t)a[0]); break;
    case ES2_IO_OS_FILE_TIME_TO_LOCAL: {
        FILETIME in = {a[0], a[1]};
        call->result = (uint32_t)vos_FileTimeToLocalFileTime(&in, (LPFILETIME)call->out);
        break;
    }
    case ES2_IO_OS_FILE_TIME_TO_DOS: {
        FILETIME in = {a[0], a[1]};
        call->result = (uint32_t)vos_FileTimeToDosDateTime(&in, (LPWORD)(uintptr_t)a[2], (LPWORD)(uintptr_t)a[3]);
        break;
    }
    case ES2_IO_OS_GET_LOCAL_TIME: vos_GetLocalTime((LPSYSTEMTIME)call->out); break;
    default: ++vos_unexpected; break;
    }
}
static uint32_t io_malloc(void *context, uint32_t n, uint32_t hint) {
    (void)context; (void)hint;
    return ((uint32_t (__cdecl *)(uint32_t))(uintptr_t)io_malloc_export)(n);
}
static void io_free(void *context, uint32_t p) { (void)context; ((void (__cdecl *)(uint32_t))(uintptr_t)io_free_export)(p); }
static const es2_io_env io_env = {NULL, io_read8, io_write8, io_os, io_malloc, io_free};

/* ------------------------------------------------------------------------
 * State: CW3220's DATA section, the heap window, the test arena, the OS. */
#define IO_DATA_RVA 0x1f000u
#define IO_DATA_SIZE 0x5000u
#define IO_ARENA 0x10000u
static BYTE *io_arena, *io_data_pristine;
typedef struct io_snapshot {
    BYTE data[IO_DATA_SIZE];
    BYTE arena[IO_ARENA];
    vos_state os;
    uint32_t heap_size;
    BYTE heap[0x40000];
} io_snapshot;
static io_snapshot *io_saved, *io_after_original, *io_after_model;
static int io_save(io_snapshot *s) {
    memcpy(s->data, rtl_cw + IO_DATA_RVA, IO_DATA_SIZE);
    memcpy(s->arena, io_arena, IO_ARENA);
    s->os = vos;
    s->heap_size = vos.vm_committed;
    if (s->heap_size > sizeof(s->heap)) return fail("heap window over the snapshot size");
    memcpy(s->heap, vos_window, s->heap_size);
    return 0;
}
static void io_restore(const io_snapshot *s) {
    memcpy(rtl_cw + IO_DATA_RVA, s->data, IO_DATA_SIZE);
    memcpy(io_arena, s->arena, IO_ARENA);
    vos = s->os;
    memcpy(vos_window, s->heap, s->heap_size);
}
static const char *io_snapshot_difference(const io_snapshot *a, const io_snapshot *b, uint32_t *where) {
    for (uint32_t i = 0; i < IO_DATA_SIZE; ++i) if (a->data[i] != b->data[i]) { *where = (uint32_t)(uintptr_t)rtl_cw + IO_DATA_RVA + i; return "CW3220 DATA"; }
    for (uint32_t i = 0; i < IO_ARENA; ++i) if (a->arena[i] != b->arena[i]) { *where = (uint32_t)(uintptr_t)io_arena + i; return "arena"; }
    if (memcmp(&a->os, &b->os, sizeof(a->os))) { *where = 0; return "emulated OS"; }
    if (a->heap_size != b->heap_size) { *where = a->heap_size; return "heap size"; }
    for (uint32_t i = 0; i < a->heap_size; ++i) if (a->heap[i] != b->heap[i]) { *where = (uint32_t)(uintptr_t)vos_window + i; return "heap"; }
    return NULL;
}

/* Arena layout (offsets). */
#define IO_NAMES 0x0000u       /* 16 names x 32 bytes */
#define IO_MODES 0x0200u       /* 8 modes x 8 bytes */
#define IO_FORMAT 0x0300u      /* 0x100 bytes */
#define IO_FMODE 0x0400u
#define IO_FFBLK 0x0500u       /* 3 x 0x120 */
#define IO_OUT 0x1000u         /* 12 x 64 bytes */
#define IO_TIME 0x1400u
#define IO_BUFFER 0x2000u      /* 0x4000 bytes */
#define IO_BUFFER_SIZE 0x4000u
static uint32_t io_a(uint32_t offset) { return (uint32_t)(uintptr_t)io_arena + offset; }

/* ------------------------------------------------------------------------
 * Sequence set-up. */
static const char *const io_file_names[] = {"a.txt", "b.txt", "c.dat", "d.vol", "e.vol", "ro", "con", "bad", "full", "noclose", "dir", "hid", "lab",
                                            "keyjoy.cfg", "sound.cfg", "prefs.cfg", "mat0.dat", "pipe", "crs", "ws", "cz"};
/* Copies (in memory, read once) of four small files of the CD's DATA directory. */
static const char *const io_cd_names[4] = {"KEYJOY.CFG", "SOUND.CFG", "PREFS.CFG", "MAT0.DAT"};
static BYTE io_cd_data[4][VOS_DATA];
static uint32_t io_cd_size[4];
static int io_load_cd_files(const char *root) {
    for (unsigned i = 0; i < 4; ++i) {
        char path[MAX_PATH];
        FILE *f = NULL;
        for (unsigned k = 0; k < 2 && !f; ++k) {
            snprintf(path, sizeof(path), k ? "%s/extracted/cd/DATA/%s" : "%s/DATA/%s", root, io_cd_names[i]);
            f = fopen(path, "rb");
        }
        if (!f) return fail("CD DATA file missing");
        io_cd_size[i] = (uint32_t)fread(io_cd_data[i], 1, VOS_DATA, f);
        fclose(f);
    }
    return 0;
}
static const char io_text_alphabet[] = "  \t\r\n\x1a\xff-+0123456789abcfxXzAZ:,%[]^";
static uint32_t io_fmode_value, io_sequence;
static void io_text(BYTE *p, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t r = random32() % 100u;
        p[i] = r < 70 ? (BYTE)io_text_alphabet[random32() % (sizeof(io_text_alphabet) - 1u)] : (BYTE)random32();
        if (r >= 70 && r < 97) p[i] = (BYTE)("0123456789 -\n\r"[random32() % 14u]);
        if ((p[i] == 0x1a || p[i] == 0xff) && random32() % 8u) p[i] = ' ';   /* Ctrl-Z and 0xff end text input: rare */
    }
}
/* Scanner-friendly text: numbers in every base and sign, words, separators. */
static uint32_t io_tokens(BYTE *p, uint32_t capacity) {
    static const char *const tokens[] = {"12", "-45", "+7", "0x1F", "0X2a", "017", "0", "08", "2147483647", "4294967296", "-2147483649",
                                         "abc", "cab", "zz", "a-b", "x", ":", ",", "%", "]", "^", "\r\n", "\n", " ", "  ", "\t", "9z"};
    uint32_t n = 0;
    while (n + 16u < capacity) {
        const char *t = tokens[random32() % (sizeof(tokens) / sizeof(tokens[0]))];
        size_t k = strlen(t);
        memcpy(p + n, t, k);
        n += (uint32_t)k;
        if (random32() % 3u) p[n++] = ' ';
        if (random32() % 200u == 0) break;
    }
    return n;
}
static void vos_reset(void) {
    memset(&vos, 0, sizeof(vos));
    vos.set_handle_count = random32() % 4u == 0 ? 20u + random32() % 30u : 50u;
    const char *std_names[3] = {"<in>", "<out>", "<err>"};
    uint32_t std_kinds[3] = {VOS_DEVICE, random32() % 4u ? VOS_SINK : VOS_DEVICE, random32() % 3u ? VOS_DEVICE : VOS_DISK};
    for (unsigned i = 0; i < 3; ++i) {
        vos_file *f = &vos.files[i];
        f->used = 1; strcpy(f->name, std_names[i]); f->kind = std_kinds[i]; f->attributes = 0x20;
        vos.handles[i].open = 1; vos.handles[i].file = i; vos.handles[i].access = GENERIC_READ | GENERIC_WRITE;
    }
    for (unsigned i = 0; i < sizeof(io_file_names) / sizeof(io_file_names[0]); ++i) {
        if (random32() % 6u == 0) continue;
        vos_file *f = &vos.files[3 + i];
        f->used = 1; strcpy(f->name, io_file_names[i]);
        f->attributes = !strcmp(f->name, "ro") ? 0x21u : !strcmp(f->name, "dir") ? 0x10u : !strcmp(f->name, "hid") ? 0x22u :
                        !strcmp(f->name, "lab") ? 0x08u : 0x20u;
        f->kind = !strcmp(f->name, "con") ? VOS_DEVICE : !strcmp(f->name, "bad") ? VOS_FAULTY : !strcmp(f->name, "full") ? VOS_FULL : VOS_DISK;
        f->size = f->kind == VOS_FULL ? random32() % 20u : random32() % 4u == 0 ? random32() % 8u : random32() % 1500u;
        io_text(f->data, f->size);
        if (!strcmp(f->name, "a.txt") || !strcmp(f->name, "b.txt")) f->size = io_tokens(f->data, 600u + random32() % 800u);
        if (i >= 13u && i < 17u) { f->size = io_cd_size[i - 13u]; memcpy(f->data, io_cd_data[i - 13u], f->size); }
        if (!strcmp(f->name, "pipe")) f->kind = VOS_PIPE;
        if (!strcmp(f->name, "cz")) { f->size = 4; memcpy(f->data, "\x1a" "abc", 4); }   /* Ctrl-Z first */
        if (!strcmp(f->name, "ws")) { f->size = 9; memcpy(f->data, "12 \t\n\xe9 \t ", 9); }   /* a high byte, then white space to the end */
        if (!strcmp(f->name, "crs") || !strcmp(f->name, "bad")) {
            /* Carriage returns only (a text read that keeps nothing), or CRs where the faulty reads begin. */
            f->size = 24u + random32() % 40u;
            for (uint32_t k = 0; k < f->size; ++k) f->data[k] = random32() % 6u || k < 6u ? '\r' : (BYTE)('a' + random32() % 3u);
        }
        f->time[0] = random32(); f->time[1] = 0x01b00000u + random32() % 0x30000u;
    }
}
static int io_init_runtime(void) {
    memcpy(rtl_cw + IO_DATA_RVA, io_data_pristine, IO_DATA_SIZE);
    VirtualFree(vos_window, VOS_WINDOW, MEM_DECOMMIT);
    memset(io_arena, 0, IO_ARENA);
    vos_reset();
    *(volatile uint32_t *)(uintptr_t)(rtl_cw + RTL_HEAP_PLATFORM) = 0;
    ((void (__cdecl *)(void))(uintptr_t)(rtl_cw + 0x170fcu))();      /* heap */
    uint32_t s = random32() % 4u;
    io_fmode_value = s == 0 ? 0x8000u : s == 1 ? 0u : 0x4000u;
    put32(io_arena + IO_FMODE, io_fmode_value);
    put32(rtl_cw + 0x1fdb0u, io_a(IO_FMODE));                         /* the module's _fmode */
    uint32_t tz = random32() % 3u;
    put32(rtl_cw + 0x23070u, tz == 0 ? 0u : 18000u);
    put32(rtl_cw + 0x23074u, tz == 2 ? 0u : 1u);
    vos_which = 0; vos_log_count[0] = 0;
    ((void (__cdecl *)(void))(uintptr_t)(rtl_cw + 0x9014u))();       /* handles */
    ((void (__cdecl *)(void))(uintptr_t)(rtl_cw + 0x7248u))();       /* standard streams */
    vos_log_count[0] = 0;
    return vos_unexpected ? fail("emulated OS: unexpected call during start-up") : 0;
}

/* ------------------------------------------------------------------------
 * Operations. */
typedef struct io_op {
    es2_io_function f;
    uint32_t args[16];
    unsigned count;
} io_op;
static const char *io_names_text(void) {
    static const char *const extra[] = {"missing", "busy", "x.txt", "new", "", "a.tx", "<out>", "<in>", "huge"};
    uint32_t r = random32() % 10u;
    return r < 7 ? io_file_names[random32() % (sizeof(io_file_names) / sizeof(io_file_names[0]))] : extra[random32() % 9u];
}
static uint32_t io_name(unsigned slot, const char *text) {
    uint32_t a = io_a(IO_NAMES + 32u * (slot % 16u));
    strcpy((char *)(uintptr_t)a, text);
    return a;
}
static uint32_t io_count(void) {
    static const uint32_t edges[] = {0, 1, 2, 3, 4, 7, 8, 63, 64, 127, 128, 129, 511, 512, 513, 1024, 1500, 2047, 2048, 0xffffffffu};
    uint32_t r = random32() % 10u;
    if (r < 5) return edges[random32() % (sizeof(edges) / sizeof(edges[0]))];
    if (r < 9) return random32() % 600u;
    return random32() % IO_BUFFER_SIZE;
}
static uint32_t io_fd(void) {
    uint32_t r = random32() % 20u;
    if (r == 0) return 0xffffffffu;
    if (r == 1) return 40u + random32() % 30u;
    if (r < 14) {
        /* Mostly a handle in use. */
        uint32_t open[50], n = 0;
        for (uint32_t fd = 0; fd < 50u; ++fd) if (get32(rtl_cw + 0x1fce4u + 4u * fd)) open[n++] = fd;
        if (n) return open[random32() % n];
    }
    return random32() % 12u;
}
/* A FILE pointer: an entry of the table (most picks one in use); with
 * `bogus`, sometimes a pointer whose token does not match (only for the
 * functions that check it: fclose, fseek, rewind). */
static uint32_t io_fp_pick(int bogus) {
    uint32_t streams = (uint32_t)(uintptr_t)rtl_cw + 0x1f830u, r = random32() % 30u;
    if (bogus && r == 0) return streams + 0x18u * (3u + random32() % 6u) + 2u;   /* token mismatch */
    for (unsigned tries = 0; tries < 8; ++tries) {
        uint32_t k = r < 2 ? random32() % 3u : 3u + random32() % 8u;
        uint32_t fp = streams + 0x18u * k;
        if ((int8_t)rtl_cw[0x1f830u + 0x18u * k + 0x16u] >= 0 || tries == 7) return fp;
    }
    return streams + 0x18u * 3u;
}
static uint32_t io_fp(void) { return io_fp_pick(0); }
/* Mostly an open stream that can be read (for fread and fscanf). */
static uint32_t io_fp_readable(void) {
    if (random32() % 10u < 7u) {
        uint32_t found[50], n = 0;
        for (uint32_t k = 3; k < 50u; ++k) {
            const BYTE *f = rtl_cw + 0x1f830u + 0x18u * k;
            if ((int8_t)f[0x16] >= 0 && (get16(f + 0x12) & 0x21u) == 1u && (int32_t)get32(f + 8) >= 0) found[n++] = k;
        }
        if (n) return (uint32_t)(uintptr_t)rtl_cw + 0x1f830u + 0x18u * found[random32() % n];
    }
    return io_fp();
}
static int io_fp_open(uint32_t fp) {
    uint32_t streams = (uint32_t)(uintptr_t)rtl_cw + 0x1f830u;
    return fp - streams < 50u * 0x18u && (fp - streams) % 0x18u == 0 && (int8_t)rtl_cw[fp - (uint32_t)(uintptr_t)rtl_cw + 0x16u] >= 0;
}
static const char *const io_modes[] = {"r", "w", "a", "r+", "w+", "a+", "rt", "wt", "at", "rb", "wb", "ab", "r+b", "rb+", "w+t", "wt+",
                                       "a+b", "r+t", "x", "", "rx", "b", "+", "rbx", "w+b+", "at+", "r++", "a++", "w+x"};
static unsigned io_format_conversions;
/* A fscanf format of up to 8 conversions; outputs in the arena. */
static void io_format(io_op *op) {
    static const char *const pieces[] = {
        " ", " ", "%d", "%5d", "%*d", "%ld", "%hd", "%D", "%i", "%3i", "%o", "%x", "%X", "%u", "%s", "%3s", "%*s", "%c", "%3c",
        "%*c", "%[a-c]", "%[^ \n]", "%[0-9-]", "%[]a]", "%[^]x]", "%[c-a]", "%[a-]", "%n", "%p", "%hn", "%%", ":", ",", "x", "-",
        "\n", "\t", "%f", "%5%", "%", "%Lg", "%Ns", "%Fd", "%lX", "%2[0-9]", "%0d", "%10s", "\xff", "%\xe9", "%1i", "%1x", "%2i",
    };
    char *format = (char *)(uintptr_t)io_a(IO_FORMAT);
    format[0] = 0;
    unsigned conversions = 0, n = 1 + random32() % 8u;
    for (unsigned i = 0; i < n && strlen(format) < 200; ++i) {
        const char *p = pieces[random32() % (sizeof(pieces) / sizeof(pieces[0]))];
        if (!strcmp(p, "%") && i + 1 < n) continue;   /* only at the end */
        strcat(format, p);
        if (p[0] == '%' && p[1] != '%' && p[1] != '*' && strcmp(p, "%5%") && strcmp(p, "%")) ++conversions;
    }
    io_format_conversions = conversions;
    op->args[1] = io_a(IO_FORMAT);
    for (unsigned i = 0; i < 12; ++i) op->args[2 + i] = io_a(IO_OUT + 64u * i);
    op->count = 14;
}
/* A format the model takes (no float conversion, no number read with an
 * uninitialised base: "%5%" or a '%' at the end). */
static int io_format_modelled(const char *format) {
    return es2_io_scan_supported(&io_env, &io_layout, (uint32_t)(uintptr_t)format);
}
static void io_clock(void) {
    SYSTEMTIME *t = &vos.clock;
    memset(t, 0, sizeof(*t));
    uint32_t r = random32() % 10u;
    t->wYear = (WORD)(1965 + random32() % 76u);
    t->wMonth = (WORD)(1 + random32() % 12u);
    t->wDay = (WORD)(1 + random32() % 31u);
    t->wHour = (WORD)(random32() % 24u); t->wMinute = (WORD)(random32() % 60u); t->wSecond = (WORD)(random32() % 60u);
    if (r < 4) {
        /* Around the summer-time boundaries: April and October, days 1-31, hours 0-3. */
        t->wYear = (WORD)(1970 + random32() % 40u);
        t->wMonth = random32() % 2u ? 4 : 10;
        t->wHour = (WORD)(random32() % 4u);
    }
    if (r == 4) {
        t->wSecond = (WORD)(59 + random32() % 3u); t->wMinute = (WORD)(59 + random32() % 2u);
        if (random32() % 2u) { t->wMonth = 12; t->wDay = 31; t->wHour = 23; }   /* into the next year */
    }
    if (r == 5) { t->wYear = (WORD)(2037 + random32() % 3u); t->wMonth = 12; t->wDay = 31; }
    if (r == 6) { t->wYear = (WORD)(1972 + 4u * (random32() % 16u) + (random32() % 3u == 0)); t->wMonth = 2; t->wDay = (WORD)(27 + random32() % 5u); }
}
static int io_focus_scan;   /* sequences that exercise the scanner on readable streams */
static io_op io_generate(void) {
    io_op op;
    memset(&op, 0, sizeof(op));
    uint32_t r = random32() % 100u;
    if (io_focus_scan) {
        uint32_t fp = io_fp_readable();
        if (r < 75u) { op.f = ES2_IO_FSCANF; op.args[0] = fp; io_format(&op); return op; }
        op.f = r < 90u ? ES2_IO_REWIND : ES2_IO_FSEEK;
        op.args[0] = fp; op.args[1] = random32() % 300u; op.args[2] = 0; op.count = op.f == ES2_IO_FSEEK ? 3u : 1u;
        return op;
    }
    es2_io_function f = r < 9 ? ES2_IO_OPEN : r < 13 ? ES2_IO_CLOSE : r < 19 ? ES2_IO_READ : r < 24 ? ES2_IO_WRITE : r < 28 ? ES2_IO_LSEEK :
                        r < 30 ? ES2_IO_FILELENGTH : r < 40 ? ES2_IO_FOPEN : r < 45 ? ES2_IO_FCLOSE : r < 53 ? ES2_IO_FREAD :
                        r < 58 ? ES2_IO_FWRITE : r < 62 ? ES2_IO_FSEEK : r < 65 ? ES2_IO_FTELL : r < 67 ? ES2_IO_REWIND :
                        r < 78 ? ES2_IO_FSCANF : r < 80 ? ES2_IO_FLUSHALL : r < 85 ? ES2_IO_FINDFIRST : r < 89 ? ES2_IO_FINDNEXT :
                        r < 93 ? ES2_IO_TIME : r < 96 ? ES2_IO_PRINTF : r < 98 ? ES2_IO_FPUTC : ES2_IO_FPUTS;
    op.f = f;
    switch (f) {
    case ES2_IO_OPEN: {
        static const uint32_t access[] = {0, 1, 2, 3};
        uint32_t flags = access[random32() % 10u < 9 ? random32() % 3u : 3u] | (random32() % 8u) << 8 | (random32() % 3u == 0 ? 0x800u : 0u) |
                         (random32() % 5u == 0 ? 0x80u : 0u) | (random32() % 8u) << 4;
        uint32_t m = random32() % 4u;
        flags |= m == 0 ? 0u : m == 1 ? 0x4000u : m == 2 ? 0x8000u : 0xc000u;
        static const uint32_t modes[] = {0, 0x80, 0x100, 0x180, 0xffffffffu};
        op.args[0] = io_name(0, io_names_text()); op.args[1] = flags; op.args[2] = random32() % 6u ? modes[random32() % 5u] : random32();
        op.count = 3;
        break;
    }
    case ES2_IO_CLOSE: case ES2_IO_FILELENGTH: op.args[0] = io_fd(); op.count = 1; break;
    case ES2_IO_READ: case ES2_IO_WRITE: {
        uint32_t n = io_count(), offset = n < IO_BUFFER_SIZE ? random32() % (IO_BUFFER_SIZE - n + 1u) : 0u;
        if (n != 0xffffffffu && n > IO_BUFFER_SIZE) n = IO_BUFFER_SIZE;
        if (f == ES2_IO_WRITE) { if (n == 0xffffffffu) n = 0; io_text(io_arena + IO_BUFFER + offset, n); }
        op.args[0] = io_fd(); op.args[1] = io_a(IO_BUFFER + (n == 0xffffffffu ? 0u : offset)); op.args[2] = n; op.count = 3;
        if (n == 0xffffffffu) op.args[2] = 0xffffffffu;
        break;
    }
    case ES2_IO_LSEEK: {
        static const uint32_t offsets[] = {0, 1, 0xffffffffu, 3000, 0x80000000u, 100, 0xfffffff0u};
        op.args[0] = io_fd(); op.args[1] = random32() % 2u ? offsets[random32() % 7u] : random32() % 2000u;
        op.args[2] = random32() % 10u ? random32() % 3u : random32() % 2u ? 3u : 0xffffffffu;
        op.count = 3;
        break;
    }
    case ES2_IO_FOPEN:
        op.args[0] = io_name(1, io_names_text());
        op.args[1] = io_a(IO_MODES);
        strcpy((char *)(uintptr_t)op.args[1], io_modes[random32() % (sizeof(io_modes) / sizeof(io_modes[0]))]);
        op.count = 2;
        break;
    case ES2_IO_FCLOSE: case ES2_IO_FTELL: case ES2_IO_REWIND:
        op.args[0] = random32() % 40u ? io_fp_pick(f != ES2_IO_FTELL) : 0u; op.count = 1;
        if (f != ES2_IO_FCLOSE && !op.args[0]) op.args[0] = io_fp();   /* NULL only for fclose */
        break;
    case ES2_IO_FREAD: case ES2_IO_FWRITE: {
        static const uint32_t sizes[] = {0, 1, 1, 1, 2, 3, 7, 16, 100};
        uint32_t size = sizes[random32() % 9u], count = random32() % 5u == 0 ? 0u : random32() % 3u ? random32() % 40u : random32() % 700u;
        if (f == ES2_IO_FWRITE && random32() % 3u == 0) { size = 1; count = 200u + random32() % 300u; }
        if (size && size * count > IO_BUFFER_SIZE) count = IO_BUFFER_SIZE / size;
        if (f == ES2_IO_FWRITE) io_text(io_arena + IO_BUFFER, size * count);
        op.args[0] = io_a(IO_BUFFER); op.args[1] = size; op.args[2] = count; op.args[3] = f == ES2_IO_FREAD ? io_fp_readable() : io_fp(); op.count = 4;
        break;
    }
    case ES2_IO_FSEEK: {
        static const uint32_t offsets[] = {0, 1, 2, 0xffffffffu, 600, 0xfffffe00u};
        op.args[0] = io_fp_pick(1); op.args[1] = random32() % 2u ? offsets[random32() % 6u] : random32() % 1600u;
        op.args[2] = random32() % 10u ? random32() % 3u : 7u;
        op.count = 3;
        break;
    }
    case ES2_IO_FSCANF: op.args[0] = io_fp_readable(); io_format(&op); break;
    case ES2_IO_FLUSHALL: op.count = 0; break;
    case ES2_IO_FINDFIRST: {
        static const char *const patterns[] = {"*", "*.vol", "vol\\*.vol", "?.txt", "*.*", "missing*", "d*", "c?n", "*l", "<*", "la*", "h*"};
        op.args[0] = io_name(2, patterns[random32() % 12u]);
        op.args[1] = io_a(IO_FFBLK + 0x120u * (random32() % 3u));
        static const uint32_t masks[] = {0, 0, 0x10, 0x16, 0x08, 0x02, 0x04, 0x37};
        op.args[2] = masks[random32() % 8u];
        op.count = 3;
        break;
    }
    case ES2_IO_FINDNEXT: op.args[0] = io_a(IO_FFBLK + 0x120u * (random32() % 3u)); op.count = 1; break;
    case ES2_IO_TIME: io_clock(); op.args[0] = random32() % 3u ? io_a(IO_TIME) : 0u; op.count = 1; break;
    case ES2_IO_PRINTF: {
        static const char *const formats[] = {"x=%d\n", "%s:%5d", "%08x %c", "", "%ld %u", "abc", "%%%s", "%-4d|", "%p"};
        char *format = (char *)(uintptr_t)io_a(IO_FORMAT);
        strcpy(format, formats[random32() % 9u]);
        op.args[0] = io_a(IO_FORMAT); op.args[1] = random32(); op.args[2] = random32() % 2u ? io_a(IO_NAMES) : random32(); op.args[3] = random32();
        if (strstr(format, "%s")) op.args[strstr(format, "%s") == format + 2 || !strncmp(format, "%s", 2) ? 1 : 2] = io_a(IO_NAMES);
        op.count = 4;
        break;
    }
    case ES2_IO_FPUTC: op.args[0] = random32(); op.args[1] = (uint32_t)(uintptr_t)rtl_cw + 0x1f848u + (random32() % 3u == 0 ? 0x18u : 0u); op.count = 2; break;
    case ES2_IO_FPUTS:
        op.args[0] = random32() % 5u ? io_name(3, io_names_text()) : 0u;
        op.args[1] = (uint32_t)(uintptr_t)rtl_cw + 0x1f848u + (random32() % 2u ? 0x18u : 0u);
        op.count = 2;
        break;
    default: break;
    }
    return op;
}
/* findnext on a block whose node was never set up would dereference junk in
 * the original as well: only blocks that a findfirst set up. */
static BYTE io_ffblk_ready[3];
static int32_t io_fp_level(uint32_t fp) {
    uint32_t streams = (uint32_t)(uintptr_t)rtl_cw + 0x1f830u;
    return fp - streams < 50u * 0x18u ? (int32_t)get32(rtl_cw + 0x1f830u + (fp - streams) + 8u) : 0;
}
static int io_op_allowed(const io_op *op) {
    if (op->f == ES2_IO_FINDNEXT) return io_ffblk_ready[(op->args[0] - io_a(IO_FFBLK)) / 0x120u];
    /* As C requires (and DBSIM does): no input right after output on a
     * stream, no output over unread input, without a seek or flush between
     * them; the original corrupts its FILE otherwise (a read moves curp past
     * the buffer, the next flush writes from there). */
    if (op->f == ES2_IO_FREAD) return io_fp_level(op->args[3]) >= 0;
    if (op->f == ES2_IO_FWRITE) {
        /* After input (flag 0x80, cleared by a seek) curp is inside the buffer
         * even at level 0: the copy would run past its end. */
        uint32_t streams = (uint32_t)(uintptr_t)rtl_cw + 0x1f830u, fp = op->args[3];
        return io_fp_level(fp) <= 0 && !(fp - streams < 50u * 0x18u && (get16(rtl_cw + 0x1f830u + (fp - streams) + 0x12u) & 0x80u));
    }
    /* __fputc is the putc macro's overflow call: the macro has already
     * incremented level to 0 (__fputc decrements it again). Called with any
     * other level the export moves curp without counting and finally writes
     * past the buffer. */
    if (op->f == ES2_IO_FPUTC) return io_fp_level(op->args[1]) == 0;
    return 1;
}

/* ------------------------------------------------------------------------
 * Running an operation. */
static rtl_vitro_frame io_frame;
static uint32_t io_export[ES2_IO_FUNCTION_COUNT];
static int io_call_export(uint32_t target, const io_op *op, uint32_t *eax) {
    rtl_vitro_frame *fr = &io_frame;
    memset(fr, 0, sizeof(*fr));
    uint32_t *args = (uint32_t *)(uintptr_t)RTL_ARGS_ADDRESS;
    memcpy(args, op->args, sizeof(op->args));
    memcpy((BYTE *)(uintptr_t)(RTL_ARGS_ADDRESS - RTL_BELOW), rtl_below_pattern, RTL_BELOW);
    fr->esp = RTL_ARGS_ADDRESS; fr->target = target;
    fr->eax = 0x0eaeaeaeu; fr->ecx = 0x0c0c0c0cu; fr->edx = 0x0d0d0d0du;
    fr->ebx = 0xb0b0b0b0u; fr->esi = 0x51515151u; fr->edi = 0xd1d1d1d1u; fr->ebp = 0xe0e0e0e0u;
    rtl_vitro_call(fr);
    if (fr->out_ebx != 0xb0b0b0b0u || fr->out_esi != 0x51515151u || fr->out_edi != 0xd1d1d1d1u || fr->out_ebp != 0xe0e0e0e0u ||
        fr->out_esp != RTL_ARGS_ADDRESS || (fr->out_flags & 0x400u)) return fail("callee-saved registers, ESP or DF");
    /* The argument slots are not compared: CW3220 reuses them as locals (_open
     * ORs the default mode into its flags slot); the caller never reads them. */
    *eax = fr->out_eax;
    return 0;
}
static int io_call_model(const io_op *op, uint32_t *eax, int *status) {
    uint32_t *args = (uint32_t *)(uintptr_t)RTL_ARGS_ADDRESS;
    memcpy(args, op->args, sizeof(op->args));
    *status = es2_io_call(op->f, &io_env, &io_layout, RTL_ARGS_ADDRESS, eax);
    return 0;
}

/* Paths observed (from the inputs and the export's results). */
enum {
    P_OPEN_OK, P_OPEN_MISSING, P_OPEN_BUSY, P_OPEN_EXISTS, P_OPEN_ACCESS3, P_OPEN_CREATE, P_OPEN_DEVICE, P_OPEN_MANY,
    P_CLOSE_OK, P_CLOSE_BAD, P_READ_BINARY, P_READ_TEXT, P_READ_CTRLZ, P_READ_EOF, P_READ_ERROR, P_READ_ZERO,
    P_WRITE_BINARY, P_WRITE_TEXT, P_WRITE_APPEND, P_WRITE_SHORT, P_WRITE_ERROR, P_LSEEK_OK, P_LSEEK_BAD_WHENCE, P_LSEEK_NEGATIVE,
    P_LSEEK_PAST_END, P_FILELENGTH, P_FOPEN_OK, P_FOPEN_BAD_MODE, P_FOPEN_FAIL, P_FOPEN_DEVICE, P_FCLOSE_OK, P_FCLOSE_TOKEN,
    P_FCLOSE_NULL, P_FREAD_OK, P_FREAD_SHORT, P_FREAD_SIZE0, P_FREAD_BULK, P_FWRITE_OK, P_FWRITE_BULK, P_FWRITE_SIZE0,
    P_FWRITE_LINE, P_FSEEK_OK, P_FSEEK_FAIL, P_FTELL_OUT, P_FTELL_IN, P_REWIND, P_FSCANF_COUNT, P_FSCANF_EOF, P_FSCANF_ZERO,
    P_FSCANF_DECLINED, P_FLUSHALL_SOME, P_FIND_OK, P_FIND_NONE, P_FIND_REUSE, P_FINDNEXT_OK, P_FINDNEXT_END, P_TIME_OK,
    P_TIME_BEFORE, P_TIME_SUMMER, P_PRINTF, P_PRINTF_DECLINED, P_FPUTC, P_FPUTS, P_COUNT
};
static const char *const io_path_names[P_COUNT] = {
    "open ok", "open missing", "open 0x6e", "open exists", "open access 3", "open create", "open device", "open no handle",
    "close ok", "close bad", "read binary", "read text", "read ctrl-z", "read end", "read error", "read 0/-1",
    "write binary", "write text", "write append", "write short", "write error", "lseek ok", "lseek bad whence", "lseek negative",
    "lseek past end", "filelength", "fopen ok", "fopen bad mode", "fopen failed", "fopen device", "fclose ok", "fclose token",
    "fclose NULL", "fread full", "fread short", "fread size 0", "fread bulk", "fwrite ok", "fwrite bulk", "fwrite size 0",
    "fwrite line-buffered", "fseek ok", "fseek fail", "ftell output", "ftell input", "rewind", "fscanf count", "fscanf EOF",
    "fscanf 0", "fscanf declined", "flushall >0", "findfirst ok", "findfirst none", "findfirst reuse", "findnext ok", "findnext end",
    "time ok", "time before 1970", "time summer", "printf", "printf declined", "fputc", "fputs",
};
static unsigned io_paths[P_COUNT];
static uint32_t io_fd_flags(uint32_t fd) { return fd < 50u ? get32(rtl_cw + 0x1fce4u + 4u * fd) : 0u; }
static void io_classify(const io_op *op, uint32_t eax, const io_snapshot *before) {
    const uint32_t *a = op->args;
    uint32_t streams = (uint32_t)(uintptr_t)rtl_cw + 0x1f830u;
    switch (op->f) {
    case ES2_IO_OPEN: {
        const char *name = (const char *)(uintptr_t)a[0];
        if ((a[1] & 3u) == 3u) ++io_paths[P_OPEN_ACCESS3];
        else if (eax != 0xffffffffu) { ++io_paths[P_OPEN_OK]; if (a[1] & 0x100u) ++io_paths[P_OPEN_CREATE]; if (io_fd_flags(eax) & 0x2000u) ++io_paths[P_OPEN_DEVICE]; }
        else if (!strcmp(name, "busy")) ++io_paths[P_OPEN_BUSY];
        else if (get32(rtl_cw + 0x235d0u) == 80u) ++io_paths[P_OPEN_EXISTS];
        else if (get32(rtl_cw + 0x235d0u) == 4u) ++io_paths[P_OPEN_MANY];
        else ++io_paths[P_OPEN_MISSING];
        break;
    }
    case ES2_IO_CLOSE: ++io_paths[eax ? P_CLOSE_BAD : P_CLOSE_OK]; break;
    case ES2_IO_READ: {
        uint32_t flags = a[0] < 50u ? get32(before->data + 0x0ce4u + 4u * a[0]) : 0u;
        if (a[2] + 1u < 2u) ++io_paths[P_READ_ZERO];
        else if (eax == 0xffffffffu) ++io_paths[P_READ_ERROR];
        else if (eax == 0) ++io_paths[P_READ_EOF];
        else if (flags & 0x4000u) { ++io_paths[P_READ_TEXT]; if (io_fd_flags(a[0]) & 0x200u) ++io_paths[P_READ_CTRLZ]; }
        else ++io_paths[P_READ_BINARY];
        break;
    }
    case ES2_IO_WRITE: {
        uint32_t flags = a[0] < 50u ? get32(before->data + 0x0ce4u + 4u * a[0]) : 0u;
        if (eax == 0xffffffffu) ++io_paths[P_WRITE_ERROR];
        else if (eax < a[2]) ++io_paths[P_WRITE_SHORT];
        else if (a[2] && (flags & 0x4000u)) ++io_paths[P_WRITE_TEXT];
        else if (a[2]) ++io_paths[P_WRITE_BINARY];
        if (a[2] && (flags & 0x800u) && eax != 0xffffffffu) ++io_paths[P_WRITE_APPEND];
        break;
    }
    case ES2_IO_LSEEK:
        if (a[2] > 2u && a[0] < 50u) ++io_paths[P_LSEEK_BAD_WHENCE];
        else if (eax == 0xffffffffu && get32(rtl_cw + 0x235d0u) == 131u) ++io_paths[P_LSEEK_NEGATIVE];
        else if (eax != 0xffffffffu) { ++io_paths[P_LSEEK_OK]; if (eax > 1600u) ++io_paths[P_LSEEK_PAST_END]; }
        break;
    case ES2_IO_FILELENGTH: if (eax != 0xffffffffu) ++io_paths[P_FILELENGTH]; break;
    case ES2_IO_FOPEN: {
        const char *mode = (const char *)(uintptr_t)a[1];
        if (eax) { ++io_paths[P_FOPEN_OK]; if (get16(rtl_cw + (eax - (uint32_t)(uintptr_t)rtl_cw) + 0x12u) & 0x200u) ++io_paths[P_FOPEN_DEVICE]; }
        else if (!*mode || !strchr("rwa", mode[0]) || (mode[1] && !strchr("tb+", mode[1]))) ++io_paths[P_FOPEN_BAD_MODE];
        else ++io_paths[P_FOPEN_FAIL];
        break;
    }
    case ES2_IO_FCLOSE:
        if (!a[0]) ++io_paths[P_FCLOSE_NULL];
        else if ((a[0] - streams) % 0x18u) ++io_paths[P_FCLOSE_TOKEN];
        else if (!eax) ++io_paths[P_FCLOSE_OK];
        break;
    case ES2_IO_FREAD:
        if (!a[1]) ++io_paths[P_FREAD_SIZE0];
        else if (eax == a[2] && a[2]) ++io_paths[P_FREAD_OK];
        else if (eax < a[2]) ++io_paths[P_FREAD_SHORT];
        if (a[1] && a[1] * a[2] >= 512u && io_fp_open(a[3]) && get32(before->data + (a[3] - streams) + 0x830u + 0xcu)) ++io_paths[P_FREAD_BULK];
        break;
    case ES2_IO_FWRITE: {
        uint32_t flags = get16(before->data + (a[3] - streams) + 0x830u + 0x12u);
        if (!a[1]) ++io_paths[P_FWRITE_SIZE0];
        else if (eax == a[2] && a[2]) { ++io_paths[P_FWRITE_OK]; if (a[1] * a[2] > 512u) ++io_paths[P_FWRITE_BULK]; if (flags & 8u) ++io_paths[P_FWRITE_LINE]; }
        break;
    }
    case ES2_IO_FSEEK: ++io_paths[eax ? P_FSEEK_FAIL : P_FSEEK_OK]; break;
    case ES2_IO_FTELL: if (eax != 0xffffffffu) ++io_paths[(int32_t)get32(before->data + (a[0] - streams) + 0x830u + 8u) < 0 ? P_FTELL_OUT : P_FTELL_IN]; break;
    case ES2_IO_REWIND: ++io_paths[P_REWIND]; break;
    case ES2_IO_FSCANF:
        ++io_paths[eax == 0xffffffffu ? P_FSCANF_EOF : eax ? P_FSCANF_COUNT : P_FSCANF_ZERO];
        if (getenv("ES2_IO_SCAN_TRACE")) {
            const BYTE *f = before->data + (a[0] - streams) + 0x830u;
            fprintf(stderr, "fscanf %08x fd %d flags %04x level %d bsize %u format \"%s\" -> %08x\n", a[0], (int)(int8_t)f[0x16],
                    (unsigned)get16(f + 0x12), (int)get32(f + 8), (unsigned)get32(f + 0xc), (const char *)(uintptr_t)a[1], eax);
        }
        break;
    case ES2_IO_FLUSHALL: if (eax) ++io_paths[P_FLUSHALL_SOME]; break;
    case ES2_IO_FINDFIRST: {
        uint32_t before_node = get32(before->arena + (a[1] - io_a(0)));
        ++io_paths[eax ? P_FIND_NONE : P_FIND_OK];
        if (before_node && get32(io_arena + (a[1] - io_a(0))) == before_node) ++io_paths[P_FIND_REUSE];
        break;
    }
    case ES2_IO_FINDNEXT: ++io_paths[eax ? P_FINDNEXT_END : P_FINDNEXT_OK]; break;
    case ES2_IO_TIME: {
        const SYSTEMTIME *t = &vos.clock;
        if (eax == 0xffffffffu && t->wYear < 1970) ++io_paths[P_TIME_BEFORE];
        else if (eax != 0xffffffffu) { ++io_paths[P_TIME_OK]; if (get32(rtl_cw + 0x23074u) && (t->wMonth == 4 || t->wMonth == 10)) ++io_paths[P_TIME_SUMMER]; }
        break;
    }
    case ES2_IO_PRINTF: ++io_paths[P_PRINTF]; break;
    case ES2_IO_FPUTC: ++io_paths[P_FPUTC]; break;
    case ES2_IO_FPUTS: ++io_paths[P_FPUTS]; break;
    default: break;
    }
}

static int io_logs_equal(unsigned *where) {
    if (vos_log_count[0] != vos_log_count[1]) { *where = vos_log_count[0] < vos_log_count[1] ? vos_log_count[0] : vos_log_count[1]; return 0; }
    for (unsigned i = 0; i < vos_log_count[0] && i < VOS_LOG; ++i) {
        const vos_entry *x = &vos_log[0][i], *y = &vos_log[1][i];
        if (x->fn != y->fn || memcmp(x->a, y->a, sizeof(x->a)) || x->result != y->result || x->count != y->count || x->nbytes != y->nbytes ||
            memcmp(x->bytes, y->bytes, sizeof(x->bytes))) { *where = i; return 0; }
    }
    return 1;
}
static void io_log_dump(void) {
    for (unsigned w = 0; w < 2; ++w)
        for (unsigned i = 0; i < vos_log_count[w] && i < 40u; ++i) {
            const vos_entry *e = &vos_log[w][i];
            fprintf(stderr, "  %s %2u: fn %u args %08x %08x %08x %08x %08x %08x result %08x count %u bytes %u\n", w ? "model" : "orig ", i, e->fn,
                    e->a[0], e->a[1], e->a[2], e->a[3], e->a[4], e->a[5], e->result, e->count, e->nbytes);
        }
}

typedef struct io_tally { unsigned direct, declined, installed_shadow, installed_replaced; } io_tally;
static io_tally io_tallies[ES2_IO_FUNCTION_COUNT];
static unsigned io_stub_divergent;

/* One direct case: export, then the model from the same state. */
static uint32_t io_last_eax;   /* the export's EAX of the last direct case */
static int io_direct(const io_op *op) {
    if (getenv("ES2_IO_TRACE"))
        fprintf(stderr, "case %u: %s %08x %08x %08x %08x\n", current_case, es2_io_name(op->f), op->args[0], op->args[1], op->args[2], op->args[3]);
    if (io_save(io_saved)) return 1;
    vos_which = 0; vos_log_count[0] = 0;
    uint32_t want = 0, got = 0;
    int status = 0;
    if (op->f == ES2_IO_FREAD && !get32(io_saved->data + (op->args[3] - (uint32_t)(uintptr_t)rtl_cw - IO_DATA_RVA) + 0xcu) &&
        op->args[1] && op->args[1] * op->args[2] == 0) {
        /* The export reads an uninitialised local here: only the model's decline is checked. */
        io_call_model(op, &got, &status);
        if (status != ES2_IO_UNSUPPORTED) return fail("unbuffered fread of 0 bytes not declined");
        if (io_save(io_after_model)) return 1;
        uint32_t where;
        if (io_snapshot_difference(io_saved, io_after_model, &where)) return fail("declined call changed state");
        ++io_tallies[op->f].declined;
        return 0;
    }
    if (op->f == ES2_IO_PRINTF || (op->f == ES2_IO_FSCANF && !io_format_modelled((const char *)(uintptr_t)op->args[1]))) {
        /* A float conversion: the export would call CW3220's float scanner. */
        io_call_model(op, &got, &status);
        if (status == ES2_IO_UNSUPPORTED) {
            if (io_save(io_after_model)) return 1;
            uint32_t where;
            if (io_snapshot_difference(io_saved, io_after_model, &where)) return fail("declined fscanf changed state");
            ++io_tallies[op->f].declined; ++io_paths[op->f == ES2_IO_PRINTF ? P_PRINTF_DECLINED : P_FSCANF_DECLINED];
            return 0;
        }
        io_restore(io_saved);
        vos_log_count[0] = 0;
    }
    if (io_call_export(io_export[op->f], op, &want)) return 1;
    io_last_eax = want;
    if (io_save(io_after_original)) return 1;
    if (vos_unexpected) return fail("emulated OS: unexpected call");
    io_classify(op, want, io_saved);
    io_restore(io_saved);
    vos_which = 1; vos_log_count[1] = 0;
    io_call_model(op, &got, &status);
    vos_which = 0;
    if (io_save(io_after_model)) return 1;
    if (status == ES2_IO_UNSUPPORTED) {
        uint32_t where;
        if (io_snapshot_difference(io_saved, io_after_model, &where) || vos_log_count[1]) return fail("declined call changed state");
        int expected = (op->f == ES2_IO_FSCANF) || (op->f == ES2_IO_PRINTF) || (op->f == ES2_IO_TIME && (!vos.clock.wMonth || !vos.clock.wDay));
        if (!expected) { fprintf(stderr, "FAIL: %s case %u: %s declined\n", current_function, current_case, es2_io_name(op->f)); return 1; }
        if (op->f == ES2_IO_FSCANF) ++io_paths[P_FSCANF_DECLINED];
        if (op->f == ES2_IO_PRINTF) ++io_paths[P_PRINTF_DECLINED];
        ++io_tallies[op->f].declined;
        io_restore(io_after_original);
        return 0;
    }
    if (got != want && !(es2_io_is_stub(op->f) && want == 0xffffffffu)) {
        fprintf(stderr, "FAIL: %s case %u: %s EAX original %08x model %08x args %08x %08x %08x %08x\n", current_function, current_case,
                es2_io_name(op->f), want, got, op->args[0], op->args[1], op->args[2], op->args[3]);
        io_log_dump();
        return 1;
    }
    if (es2_io_is_stub(op->f)) {
        uint32_t where;
        if (want == 0xffffffffu) {
            /* The export's write failed (a closed stdout, a broken handle):
             * the stub reports success by design. */
            ++io_stub_divergent;
            io_restore(io_after_original);
            return 0;
        }
        const char *what = io_snapshot_difference(io_saved, io_after_model, &where);
        if (what || vos_log_count[1]) { fprintf(stderr, "FAIL: %s case %u: stub %s changed %s\n", current_function, current_case, es2_io_name(op->f), what ? what : "the OS log"); return 1; }
        ++io_tallies[op->f].direct;
        io_restore(io_after_original);
        return 0;
    }
    unsigned at;
    if (!io_logs_equal(&at)) {
        fprintf(stderr, "FAIL: %s case %u: %s OS calls differ at %u (original %u calls, model %u)\n", current_function, current_case,
                es2_io_name(op->f), at, vos_log_count[0], vos_log_count[1]);
        io_log_dump();
        return 1;
    }
    uint32_t where;
    const char *what = io_snapshot_difference(io_after_original, io_after_model, &where);
    if (what) {
        fprintf(stderr, "FAIL: %s case %u: %s %s differs at %08x (args %08x %08x %08x %08x)\n", current_function, current_case,
                es2_io_name(op->f), what, (unsigned)where, op->args[0], op->args[1], op->args[2], op->args[3]);
        return 1;
    }
    ++io_tallies[op->f].direct;
    return 0;
}
static void io_after_step(const io_op *op, uint32_t eax) {
    if (op->f == ES2_IO_FINDFIRST) io_ffblk_ready[(op->args[1] - io_a(IO_FFBLK)) / 0x120u] = get32(io_arena + (op->args[1] - io_a(0))) != 0;
    (void)eax;
}
static int io_run(es2_io_function f, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    io_op op;
    memset(&op, 0, sizeof(op));
    op.f = f; op.args[0] = a0; op.args[1] = a1; op.args[2] = a2; op.args[3] = a3; op.count = 4;
    return io_direct(&op);
}
static uint32_t io_last_fopen(const char *name, const char *mode) {
    uint32_t path = io_name(1, name), m = io_a(IO_MODES);
    strcpy((char *)(uintptr_t)m, mode);
    uint32_t before = vos_log_count[0];
    (void)before;
    if (io_run(ES2_IO_FOPEN, path, m, 0, 0)) return 1u;
    /* The FILE it took: the first entry with that file's handle just opened. */
    for (uint32_t k = 3; k < 50u; ++k) {
        const BYTE *f = rtl_cw + 0x1f830u + 0x18u * k;
        if ((int8_t)f[0x16] >= 0 && get16(f + 0x12) && get32(f + 8) == 0 && get32(f) == get32(f + 4) && get32(f + 0xc) &&
            !(get16(f + 0x12) & 0x80u) && !(get16(f + 0x12) & 0x100u)) return (uint32_t)(uintptr_t)f;
    }
    return 0;
}
/* Paths that random sequences reach too rarely for the quick tier. */
static int io_scenarios(void) {
    uint32_t fp, buffer = io_a(IO_BUFFER);
    /* fread: a bulk read of exactly the rest; a single byte from a fresh stream. */
    if ((fp = io_last_fopen("a.txt", "rb")) == 1u) return 1;
    if (fp && (io_run(ES2_IO_FREAD, buffer, 1, 512, fp) || io_run(ES2_IO_FSEEK, fp, 0, 0, 0) || io_run(ES2_IO_FREAD, buffer, 1, 1, fp))) return 1;
    /* ftell on an appending pipe with pending output (the seek back fails),
     * and on an appending terminal (the seek to the end fails). */
    static const char *const appended[2] = {"pipe", "con"};
    for (unsigned k = 0; k < 2u; ++k) {
        if ((fp = io_last_fopen(appended[k], k ? "a+" : "a")) == 1u) return 1;
        io_text(io_arena + IO_BUFFER, 12);
        if (fp && (io_run(ES2_IO_FWRITE, buffer, 1, 12, fp) || io_run(ES2_IO_FTELL, fp, 0, 0, 0))) return 1;
    }
    /* A text read of carriage returns only: the round keeps nothing and reads again. */
    /* And a text read that meets Ctrl-Z as the first byte. */
    static const char *const texts[2] = {"crs", "cz"};
    for (unsigned t = 0; t < 2u; ++t) {
        io_op op;
        memset(&op, 0, sizeof(op));
        op.f = ES2_IO_OPEN; op.args[0] = io_name(0, texts[t]); op.args[1] = 0x4000u; op.count = 3;
        if (io_direct(&op)) return 1;
        uint32_t fd = io_last_eax;
        if (fd < 50u && (io_run(ES2_IO_READ, fd, buffer, 2, 0) || io_run(ES2_IO_READ, fd, buffer, 7, 0))) return 1;
    }
    /* fscanf: the input ends inside white space the format skips. */
    if ((fp = io_last_fopen("ws", "r")) == 1u) return 1;
    if (fp) {
        strcpy((char *)(uintptr_t)io_a(IO_FORMAT), "%d %*c ");
        if (io_run(ES2_IO_FSCANF, fp, io_a(IO_FORMAT), io_a(IO_OUT), 0)) return 1;
    }
    /* A free stream with a stale level: __fputc on a closed stdout leaves
     * level -1; the next fopen takes that entry and setvbuf seeks first. */
    uint32_t stdout_file = (uint32_t)(uintptr_t)rtl_cw + 0x1f848u;
    if (io_run(ES2_IO_FCLOSE, stdout_file, 0, 0, 0)) return 1;
    if (io_fp_level(stdout_file) == 0 && io_run(ES2_IO_FPUTC, 'x', stdout_file, 0, 0)) return 1;
    if ((fp = io_last_fopen("b.txt", "r")) == 1u) return 1;
    /* A line-buffered terminal: the first character after a seek takes fputc's
     * slow path; a newline and a carriage return there flush. */
    if ((fp = io_last_fopen("con", "w")) == 1u) return 1;
    static const char line_ends[3] = {'\n', '\r', 'x'};
    for (unsigned k = 0; fp && k < 3u; ++k) {
        io_arena[IO_BUFFER] = (BYTE)line_ends[k];
        if (io_run(ES2_IO_FWRITE, buffer, 1, 1, fp) || io_run(ES2_IO_FSEEK, fp, 0, 1, 0)) return 1;
    }
    return 0;
}
static int io_direct_sequence(unsigned steps) {
    io_focus_scan = 0;
    if (io_init_runtime()) return 1;
    memset(io_ffblk_ready, 0, sizeof(io_ffblk_ready));
    if (io_sequence % 2u == 0) {
        /* Readable streams for fread and fscanf. */
        static const char *const modes[] = {"r", "rt", "rb", "r+", "r"};
        for (unsigned k = 0; k < 3u; ++k) {
            io_op op;
            memset(&op, 0, sizeof(op));
            op.f = ES2_IO_FOPEN;
            op.args[0] = io_name(1, k == 2 ? (random32() % 2u ? "c.dat" : io_file_names[13u + random32() % 4u]) : k ? "b.txt" : "a.txt");
            op.args[1] = io_a(IO_MODES);
            strcpy((char *)(uintptr_t)op.args[1], modes[random32() % 5u]);
            op.count = 2;
            if (io_direct(&op)) return 1;
        }
    }
    io_focus_scan = io_sequence % 4u == 2u;
    if (io_sequence % 10u == 1u && io_scenarios()) return 1;
    if (io_sequence % 10u == 3u) {
        /* Every FILE in use: fopen until there is none left. */
        for (unsigned k = 0; k < 50u; ++k) {
            io_op op;
            memset(&op, 0, sizeof(op));
            op.f = ES2_IO_FOPEN;
            op.args[0] = io_name(1, k % 3u ? "<in>" : "a.txt");
            op.args[1] = io_a(IO_MODES);
            strcpy((char *)(uintptr_t)op.args[1], "rb");
            op.count = 2;
            if (io_direct(&op)) return 1;
        }
    }
    if (io_sequence % 10u == 5u) {
        /* Memory pressure: CW3220's heap taken until malloc returns NULL. */
        static const uint32_t sizes[] = {0x1000u, 0x200u, 0x40u, 0x10u, 1u};
        vos.vm_cursor = VOS_WINDOW;   /* no further reservation */
        vos.vm_limit = vos.vm_committed;   /* and no further commit */
        for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); ++k)
            while (((uint32_t (__cdecl *)(uint32_t))(uintptr_t)io_malloc_export)(sizes[k])) {}
    }
    if (io_sequence % 10u == 9u) {
        /* A terminal stream with output, then a terminal read (flush of terminals). */
        static const char *const modes[] = {"r+", "w+", "a+"};
        for (unsigned k = 0; k < 2u; ++k) {
            io_op op;
            memset(&op, 0, sizeof(op));
            op.f = ES2_IO_FOPEN;
            op.args[0] = io_name(1, "con");
            op.args[1] = io_a(IO_MODES);
            strcpy((char *)(uintptr_t)op.args[1], modes[random32() % 3u]);
            op.count = 2;
            if (io_direct(&op)) return 1;
        }
    }
    if (io_sequence % 10u == 7u) {
        /* Every handle in use: opens until the table (or the OS) has none left. */
        for (unsigned k = 0; k < 56u; ++k) {
            io_op op;
            memset(&op, 0, sizeof(op));
            op.f = ES2_IO_OPEN;
            op.args[0] = io_name(0, k % 2u ? "a.tx" : "<in>"); op.args[1] = k % 2u ? 0x8101u : 0x8000u; op.args[2] = 0x180u; op.count = 3;
            if (io_direct(&op)) return 1;
        }
    }
    for (unsigned s = 0; s < steps; ++s) {
        io_op op = io_generate();
        if (!io_op_allowed(&op)) continue;
        if (io_direct(&op)) return 1;
        io_after_step(&op, 0);
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * Installed: the shim in shadow or replaced mode against a plain run. */
static uint32_t io_import_of(es2_io_function f) {
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) if (!strcmp(rtl_imports[i].symbol, es2_io_name(f))) return i;
    return RTL_IMPORT_COUNT;
}
static int io_install(int shadow) {
    rtl_options o;
    memset(&o, 0, sizeof(o));
    o.image = mapped; o.expected_base = 0x00400000u; o.image_size = RTL_IMAGE_SIZE;
    o.resolve = rtl_cw_resolve;
    o.stack_lo = (uintptr_t)rtl_stack; o.stack_hi = (uintptr_t)(rtl_stack + RTL_STACK_DWORDS);
    o.cw_image = rtl_cw;
    if (!rtl_module_range((const void *)io_install, &o.hook_lo, &o.hook_hi)) return fail("harness module range");
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        o.selected[i] = rtl_imports[i].shim;
        int f = rtl_io_function_of(i);
        if (f < 0) continue;
        if (shadow) o.shadow[i] = !es2_io_is_stub((es2_io_function)f);
        else o.modes[i] = ES2_HOOK_REPLACED;
    }
    if (!rtl_install(&o)) { fprintf(stderr, "FAIL: %s: rtl shim refused: %s\n", current_function, rtl_reason); return 1; }
    return 0;
}
#define IO_INSTALLED_STEPS 120u
static uint32_t io_plain_eax[IO_INSTALLED_STEPS];
static es2_io_function io_plain_f[IO_INSTALLED_STEPS];
static io_snapshot *io_plain_end;
/* The next operation of an installed sequence (the same random draws in
 * both runs); at the planted step a close of an invalid handle (it always
 * sets errno). */
static io_op io_installed_op(unsigned step, int shadow, int plant) {
    for (;;) {
        io_op op = io_generate();
        if (plant && step == IO_INSTALLED_STEPS / 2u) { memset(&op, 0, sizeof(op)); op.f = ES2_IO_CLOSE; op.args[0] = 60; op.count = 1; return op; }
        if (!io_op_allowed(&op)) continue;
        if (op.f == ES2_IO_FREAD && op.args[1] && op.args[1] * op.args[2] == 0) continue;   /* see io_direct */
        if (op.f == ES2_IO_FSCANF && !io_format_modelled((const char *)(uintptr_t)op.args[1])) continue;
        if (op.f == ES2_IO_PRINTF) continue;   /* the export of a declined format is not run (float) */
        if (!shadow && es2_io_is_stub(op.f)) continue;   /* the stubs write nothing by design */
        if (shadow && es2_io_is_stub(op.f)) continue;    /* not shadowed: the export runs unchanged */
        return op;
    }
}
static int io_installed_sequence(int shadow, int plant) {
    uint32_t start = random32() | 1u;
    /* Plain run. */
    random_state = start;
    if (io_init_runtime()) return 1;
    memset(io_ffblk_ready, 0, sizeof(io_ffblk_ready));
    for (unsigned s = 0; s < IO_INSTALLED_STEPS; ++s) {
        io_op op = io_installed_op(s, shadow, plant);
        vos_log_count[0] = 0;
        if (io_call_export(io_export[op.f], &op, &io_plain_eax[s])) return 1;
        io_plain_f[s] = op.f;
        io_after_step(&op, 0);
    }
    if (io_save(io_plain_end)) return 1;
    /* The same steps through the shim. */
    random_state = start;
    if (io_init_runtime()) return 1;
    memset(io_ffblk_ready, 0, sizeof(io_ffblk_ready));
    if (io_install(shadow)) return 1;
    for (unsigned s = 0; s < IO_INSTALLED_STEPS; ++s) {
        io_op op = io_installed_op(s, shadow, plant);
        if (op.f != io_plain_f[s]) return fail("installed: the step sequence diverged");
        uint32_t eax = 0, saved_errno = rtl_io_layout.errno_word;
        if (plant && s == IO_INSTALLED_STEPS / 2u) rtl_io_layout.errno_word += 0x40u;
        vos_log_count[0] = 0;
        int failed = io_call_export((uint32_t)(uintptr_t)mapped + RTL_THUNK_RVA + 6u * io_import_of(op.f), &op, &eax);
        rtl_io_layout.errno_word = saved_errno;
        if (failed) return 1;
        if (eax != io_plain_eax[s]) {
            fprintf(stderr, "FAIL: %s case %u: installed %s step %u %s: EAX %08x, without the shim %08x (args %08x %08x %08x %08x)\n", current_function,
                    io_sequence, shadow ? "shadow" : "replaced", s, es2_io_name(op.f), eax, io_plain_eax[s], op.args[0], op.args[1], op.args[2], op.args[3]);
            return 1;
        }
        io_after_step(&op, 0);
        if (shadow) ++io_tallies[op.f].installed_shadow; else ++io_tallies[op.f].installed_replaced;
    }
    if (io_save(io_after_model)) return 1;
    uint32_t where;
    const char *what = io_snapshot_difference(io_plain_end, io_after_model, &where);
    if (what) {
        fprintf(stderr, "FAIL: %s case %u: installed %s: %s differs at %08x from the run without the shim\n", current_function, io_sequence,
                shadow ? "shadow" : "replaced", what, (unsigned)where);
        return 1;
    }
    uint64_t mismatches = rtl_io_mismatch_total(), skipped = 0, declined = 0, calls = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        skipped += rtl_io_shadow_skipped[i];
        if (rtl_io_of[i] >= 0) { declined += rtl_skipped[i]; calls += rtl_counters[i].calls; }
    }
    if (calls != IO_INSTALLED_STEPS) return fail("installed: thunk counters");
    if (plant) {
        if (mismatches != 1 || (!strstr(rtl_io_mismatch_detail, "close:state@") && !strstr(rtl_io_mismatch_detail, "close:memory@"))) {
            fprintf(stderr, "FAIL: %s case %u: planted error: mismatches %llu detail %s\n", current_function, io_sequence,
                    (unsigned long long)mismatches, rtl_io_mismatch_detail);
            return 1;
        }
        printf("PASS: rtl_io planted model error caught once (%s)\n", rtl_io_mismatch_detail);
    } else if (mismatches || skipped || declined || rtl_foreign_calls) {
        for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i)
            if (rtl_io_shadow_skipped[i] || (rtl_io_of[i] >= 0 && rtl_skipped[i]))
                fprintf(stderr, "  %s: shadow skipped %llu (last: %s), declined %llu\n", rtl_imports[i].key,
                        (unsigned long long)rtl_io_shadow_skipped[i], rtl_io_last_skip, (unsigned long long)rtl_skipped[i]);
        fprintf(stderr, "FAIL: %s case %u: installed %s: mismatches %llu shadow skipped %llu declined %llu foreign %llu first %s\n",
                current_function, io_sequence, shadow ? "shadow" : "replaced", (unsigned long long)mismatches, (unsigned long long)skipped,
                (unsigned long long)declined, (unsigned long long)rtl_foreign_calls, rtl_io_mismatch_detail);
        return 1;
    }
    if (shadow) {
        static char tick[16384];
        if (rtl_tick_fields(tick, sizeof(tick), FALSE) <= 0 || !strstr(tick, plant ? ",\"rtl_io_shadow_mismatches\":1" : ",\"rtl_io_shadow_mismatches\":0"))
            return fail("io tick field");
        static char exit_record[65536];
        if (rtl_tick_fields(exit_record, sizeof(exit_record), TRUE) <= 0 || !strstr(exit_record, ",\"rtl_read_shadow_skipped\":0") ||
            !strstr(exit_record, ",\"rtl_io_os_calls\":")) return fail("io exit fields");
    }
    if (!rtl_uninstall()) return fail("rtl shim uninstall");
    uint32_t *recorder_slot = io_cw_slot("ReadFile");
    if (!recorder_slot || *recorder_slot != (uint32_t)(uintptr_t)vos_ReadFile) return fail("CW3220 OS slots not restored");
    return 0;
}

/* The console stubs through DBSIM's thunks in mode replaced: EAX as the
 * model's, nothing written (no FILE state, no OS call); a declined printf
 * format reaches the export (counted as skipped). */
static int io_installed_stubs(unsigned count) {
    if (io_init_runtime()) return 1;
    if (io_install(0)) return 1;
    unsigned declined_expected = 0;
    for (unsigned n = 0; n < count; ++n) {
        io_op op;
        memset(&op, 0, sizeof(op));
        uint32_t r = random32() % 3u;
        op.f = r == 0 ? ES2_IO_PRINTF : r == 1 ? ES2_IO_FPUTC : ES2_IO_FPUTS;
        if (op.f == ES2_IO_PRINTF) {
            static const char *const formats[] = {"x=%d\n", "%s:%5d", "%08x %c", "", "%ld %u", "abc", "%%%s", "%-4d|"};
            const char *format = formats[random32() % 8u];
            strcpy((char *)(uintptr_t)io_a(IO_FORMAT), format);
            op.args[0] = io_a(IO_FORMAT); op.args[1] = strstr(format, "%s") ? io_a(IO_NAMES) : random32(); op.args[2] = random32(); op.count = 3;
            declined_expected += !*format;
        } else if (op.f == ES2_IO_FPUTC) {
            op.args[0] = random32(); op.args[1] = (uint32_t)(uintptr_t)rtl_cw + 0x1f848u; op.count = 2;
        } else {
            op.args[0] = random32() % 4u ? io_name(3, io_names_text()) : 0u; op.args[1] = (uint32_t)(uintptr_t)rtl_cw + 0x1f860u; op.count = 2;
        }
        uint32_t want = 0, got = 0;
        int status = 0;
        io_call_model(&op, &want, &status);
        if (io_save(io_saved)) return 1;
        vos_log_count[0] = 0;
        if (io_call_export((uint32_t)(uintptr_t)mapped + RTL_THUNK_RVA + 6u * io_import_of(op.f), &op, &got)) return 1;
        if (io_save(io_after_model)) return 1;
        uint32_t where;
        if (status == ES2_IO_DONE && (got != want || io_snapshot_difference(io_saved, io_after_model, &where) || vos_log_count[0])) {
            fprintf(stderr, "FAIL: %s: installed stub %s: EAX %08x, model %08x, or state written\n", current_function, es2_io_name(op.f), got, want);
            return 1;
        }
        ++io_tallies[op.f].installed_replaced;
    }
    uint64_t declined = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) if (rtl_io_of[i] >= 0) declined += rtl_skipped[i];
    if (declined != declined_expected) return fail("installed stubs: declined count");
    if (!rtl_uninstall()) return fail("rtl shim uninstall");
    printf("PASS: rtl_io installed console stubs: %u calls through DBSIM's thunks (%u declined printf formats reached the export); "
           "EAX equal to the model, nothing written\n", count, declined_expected);
    return 0;
}

/* ------------------------------------------------------------------------
 * Branch coverage of the original: an INT3 on every conditional branch of the
 * CW3220 code behind the set (rtl_io_branches.h) during the direct cases; the
 * handler decides the branch from EFLAGS as the CPU would and continues at
 * the target or behind the branch. A site is disarmed once it has both
 * outcomes. Sites with one outcome must be listed below with the reason. */
typedef struct io_branch { uint32_t owner, rva, length; enum branch_condition condition; uint32_t target; uint64_t counts[2]; BYTE saved, armed; } io_branch;
static io_branch io_branches[] = {
#define IO_BRANCH(owner, rva, length, condition, target) {owner, rva, length, condition, target, {0, 0}, 0, 0},
#include "rtl_io_branches.h"
#undef IO_BRANCH
};
#define IO_BRANCH_COUNT (sizeof(io_branches) / sizeof(io_branches[0]))
static int io_coverage_on;
static void io_branch_patch(io_branch *b, int arm) {
    BYTE *p = rtl_cw + b->rva;
    DWORD old, ignored;
    if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old)) ExitProcess(140);
    if (arm) { b->saved = *p; *p = 0xcc; } else *p = b->saved;
    if (!VirtualProtect(p, 1, old, &ignored)) ExitProcess(141);
    b->armed = (BYTE)arm;
}
static LONG CALLBACK io_branch_fault(EXCEPTION_POINTERS *fault) {
    if (!io_coverage_on || fault->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    uint32_t pc = (uint32_t)(uintptr_t)fault->ExceptionRecord->ExceptionAddress, base = (uint32_t)(uintptr_t)rtl_cw;
    for (unsigned i = 0; i < IO_BRANCH_COUNT; ++i) {
        io_branch *b = &io_branches[i];
        if (base + b->rva != pc || !b->armed) continue;
        unsigned taken = branch_condition_taken(b->condition, fault->ContextRecord->EFlags);
        ++b->counts[taken];
        fault->ContextRecord->Eip = taken ? base + b->target : pc + b->length;
        if (b->counts[!taken]) { io_branch_patch(b, 0); FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)pc, 1); }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static void io_coverage(int on) {
    for (unsigned i = 0; i < IO_BRANCH_COUNT; ++i) {
        io_branch *b = &io_branches[i];
        if (on && !b->armed && (!b->counts[0] || !b->counts[1])) io_branch_patch(b, 1);
        if (!on && b->armed) io_branch_patch(b, 0);
    }
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    io_coverage_on = on;
}
/* Sites whose second outcome the cases cannot reach, with the reason
 * (DECLINED: only on a path the model leaves to the original; PROVED: no
 * input of the export can take it). */
typedef struct io_branch_note { uint32_t rva; unsigned edge; const char *reason; } io_branch_note;
static const io_branch_note io_branch_notes[] = {
#include "rtl_io_branch_notes.h"
    {0, 0, NULL}
};
static int io_coverage_report(void) {
    unsigned both = 0, noted = 0;
    for (unsigned i = 0; i < IO_BRANCH_COUNT; ++i) {
        const io_branch *b = &io_branches[i];
        const io_branch_note *note = NULL;
        for (unsigned k = 0; io_branch_notes[k].reason; ++k) if (io_branch_notes[k].rva == b->rva) note = &io_branch_notes[k];
        if (b->counts[0] && b->counts[1]) {
            if (note) { fprintf(stderr, "FAIL: %s: noted site %05x reached both ways (%s)\n", current_function, b->rva, note->reason); return 1; }
            ++both;
            continue;
        }
        if (note && ((note->edge == 2 && !b->counts[0] && !b->counts[1]) || (note->edge < 2 && b->counts[note->edge] && !b->counts[!note->edge]))) {
            ++noted;
            printf("NOTED: %05x (in %05x) %s: %s\n", b->rva, b->owner, note->edge == 2 ? "never runs" : note->edge ? "always taken" : "never taken", note->reason);
            continue;
        }
        fprintf(stderr, "FAIL: %s: branch %05x (in %05x) fall %llu taken %llu\n", current_function, b->rva, b->owner,
                (unsigned long long)b->counts[0], (unsigned long long)b->counts[1]);
        return 1;
    }
    printf("PASS: rtl_io %u conditional branches of the original: %u both ways, %u with one edge noted\n", (unsigned)IO_BRANCH_COUNT, both, noted);
    return 0;
}

/* ------------------------------------------------------------------------
 * Selection: --rtl-io (all) or rtl_io_<name> (a mutation run: the same cases). */
static int rtl_io_cases(const char *selection) {
    current_function = strcmp(selection, "--rtl-io") ? selection : "rtl_io";
    if (rtl_map_cw3220(getenv("ES2_DATA_ROOT"))) return 1;
    vos_window = VirtualAlloc(NULL, VOS_WINDOW, MEM_RESERVE, PAGE_NOACCESS);
    io_arena = VirtualAlloc(NULL, IO_ARENA, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    io_data_pristine = malloc(IO_DATA_SIZE);
    io_saved = malloc(sizeof(io_snapshot)); io_after_original = malloc(sizeof(io_snapshot)); io_after_model = malloc(sizeof(io_snapshot));
    io_plain_end = malloc(sizeof(io_snapshot));
    if (!vos_window || !io_arena || !io_data_pristine || !io_saved || !io_after_original || !io_after_model || !io_plain_end) return fail("memory");
    if (io_hook_os() || io_load_cd_files(getenv("ES2_DATA_ROOT"))) return 1;
    memcpy(io_data_pristine, rtl_cw + IO_DATA_RVA, IO_DATA_SIZE);
    for (int f = 0; f < ES2_IO_FUNCTION_COUNT; ++f) {
        io_export[f] = rtl_cw_export(es2_io_name((es2_io_function)f));
        if (!io_export[f] || io_import_of((es2_io_function)f) == RTL_IMPORT_COUNT) return fail("export or import missing");
    }
    io_malloc_export = rtl_cw_export("_malloc"); io_free_export = rtl_cw_export("_free");
    if (!io_malloc_export || !io_free_export) return fail("allocator exports");
    es2_io_layout_cw3220(&io_layout, (uint32_t)(uintptr_t)rtl_cw);
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        uint32_t *slot = (uint32_t *)(mapped + RTL_IAT_RVA + 4 * i);
        *slot = rtl_cw_export(rtl_imports[i].symbol);
        if (!*slot) return fail("CW3220 export of an import missing");
    }
    for (unsigned i = 0; i < RTL_BELOW; ++i) rtl_below_pattern[i] = (BYTE)random32();
    unsigned sequences = quick_tier ? 800u : 8000u, steps = 60u, installed = quick_tier ? 8u : 80u;
    PVOID handler = AddVectoredExceptionHandler(1, io_branch_fault);
    if (!handler) return fail("coverage handler");
    io_coverage(1);
    for (io_sequence = 0; io_sequence < sequences; ++io_sequence) {
        current_case = io_sequence; current_case_valid = 1;
        if (io_direct_sequence(steps)) return 1;
    }
    io_coverage(0);
    RemoveVectoredExceptionHandler(handler);
    unsigned total = 0, declined = 0;
    for (int f = 0; f < ES2_IO_FUNCTION_COUNT; ++f) { total += io_tallies[f].direct; declined += io_tallies[f].declined; }
    printf("PASS: rtl_io %u direct sequences, %u operations (%u declined by the model: float or uninitialised-base fscanf formats, "
           "unbuffered 0-byte freads, printf formats outside the modelled subset; %u stub calls whose export failed): EAX, OS calls, "
           "CW3220 data, heap, arena and OS state equal\n", io_sequence, total + declined, declined, io_stub_divergent);
    printf("CASES:");
    for (int f = 0; f < ES2_IO_FUNCTION_COUNT; ++f) printf("%s %s %u/%u", f ? "," : "", es2_io_name((es2_io_function)f) + 1, io_tallies[f].direct, io_tallies[f].declined);
    printf("\nPATHS:");
    for (unsigned i = 0; i < P_COUNT; ++i) printf("%s %s %u", i ? "," : "", io_path_names[i], io_paths[i]);
    printf("\n");
    for (unsigned i = 0; i < P_COUNT; ++i)
        if (!io_paths[i]) { fprintf(stderr, "FAIL: %s: path not reached: %s\n", current_function, io_path_names[i]); return 1; }
    if (getenv("ES2_IO_BRANCHES"))
        for (unsigned i = 0; i < IO_BRANCH_COUNT; ++i)
            if (!io_branches[i].counts[0] || !io_branches[i].counts[1])
                printf("ONE-WAY %05x %05x %llu %llu\n", io_branches[i].owner, io_branches[i].rva, (unsigned long long)io_branches[i].counts[0],
                       (unsigned long long)io_branches[i].counts[1]);
    if (io_coverage_report()) return 1;
    for (unsigned k = 0; k < installed; ++k, ++io_sequence) {
        current_case = io_sequence;
        if (io_installed_sequence(1, 0) || io_installed_sequence(0, 0)) return 1;
    }
    ++io_sequence;
    if (io_installed_sequence(1, 1)) return 1;
    if (io_installed_stubs(quick_tier ? 300u : 3000u)) return 1;
    current_case_valid = 0;
    unsigned shadowed = 0, replaced = 0, stubs = 0;
    for (int f = 0; f < ES2_IO_FUNCTION_COUNT; ++f) {
        shadowed += io_tallies[f].installed_shadow;
        if (es2_io_is_stub((es2_io_function)f)) stubs += io_tallies[f].installed_replaced; else replaced += io_tallies[f].installed_replaced;
    }
    printf("PASS: rtl_io installed %u sequences of %u calls: %u calls in shadow (0 mismatches, 0 skipped), %u file calls in replaced: "
           "EAX after every step and the final state equal to the run without the shim; %u console stub calls in replaced\n",
           2u * installed + 1u, IO_INSTALLED_STEPS, shadowed, replaced, stubs);
    printf("INSTALLED:");
    for (int f = 0; f < ES2_IO_FUNCTION_COUNT; ++f)
        printf("%s %s %u/%u", f ? "," : "", es2_io_name((es2_io_function)f) + 1, io_tallies[f].installed_shadow, io_tallies[f].installed_replaced);
    printf("\n");
    for (int f = 0; f < ES2_IO_FUNCTION_COUNT; ++f)
        if (!io_tallies[f].installed_replaced || (!es2_io_is_stub((es2_io_function)f) && !io_tallies[f].installed_shadow)) {
            fprintf(stderr, "FAIL: %s: no installed case for %s\n", current_function, es2_io_name((es2_io_function)f));
            return 1;
        }
    puts("PASS: rtl_io in-vitro run against the mapped CW3220.DLL over an emulated OS; no game process started");
    return 0;
}
