#ifndef ES2_VOL_PROBES_H
#define ES2_VOL_PROBES_H
/* Ordered probes on DBSIM's VOL registration and lookup functions,
 * armed at DLL_PROCESS_ATTACH so that the startup registration (0x45f2a8 ->
 * 0x473260) runs with them in place. The other proxy instrumentation arms at
 * the first DirectDraw/MCI call, after that registration. Design, record
 * format and live request: docs/formats/vol.md, "Registration probes from
 * DllMain".
 *
 * Off unless the INI named by ES2_LIVE_COVERAGE_CONFIG has [vol_probes]
 * enabled=1. Without the variable the attach path stops after one
 * GetEnvironmentVariableW; every later entry point returns at its first
 * statement while vp_state is VP_OFF.
 *
 * Loader-lock rules for the attach path: only kernel32 memory/file calls
 * (GetModuleFileNameW, GetEnvironmentVariableW, GetPrivateProfileStringW,
 * CreateFileW, VirtualQuery, VirtualProtect, FlushInstructionCache,
 * AddVectoredExceptionHandler). No LoadLibrary, no thread creation, no
 * waiting, no user32/advapi32. The module file hash needs CryptoAPI and is
 * therefore checked at the first DirectDraw/MCI call; before that the image
 * is identified by its PE header, an anchor and per-site byte signatures.
 *
 * Supported probe kind, as in entry_probe_engine.h: a one-byte PUSH r32
 * (not ESP) replaced by INT3. The handler records, then emulates the PUSH in
 * the saved context. It never removes the INT3 temporarily. */
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "hook/hook.h"

#define VP_MAX_SITES 16u
#define VP_MAX_EVENTS 8192u
#define VP_MAX_SNAPSHOTS 1024u
#define VP_MAX_ARCHIVES 64u
#define VP_TEXT 96u
#define VP_SLOTS 30u
#define VP_MANAGER_BYTES 0x79u  /* 30 slot pointers, slot count byte at +0x78 */
#define VP_ARCHIVE_BYTES 0x24u  /* name +0x08 (20 bytes), priority +0x1c, directories +0x1d, mask +0x20 */
#define VP_NONE 0xffu
#define VP_MODULE_SHA256 "8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad"

enum { VP_OFF, VP_ARMED, VP_REFUSED, VP_DONE };
enum { VP_SNAP_NONE, VP_SNAP_ARG0, VP_SNAP_GLOBAL };
enum { VP_TEXT_NONE, VP_TEXT_OK, VP_TEXT_NULL, VP_TEXT_UNREADABLE, VP_TEXT_TRUNCATED };
static const char *const vp_text_states[] = {"none", "ok", "null", "unreadable", "truncated"};

/* One probed function entry. args = number of cdecl stack arguments read at
 * [esp+4..]; text = argument indices read as C strings; snapshot = manager
 * whose slot list is copied (argument 0 or the global manager); archive_arg =
 * argument holding an archive object; lookup = site belongs to the optional
 * lookup set ([vol_probes] lookups=0 leaves it out). */
typedef struct vp_spec {
    uint32_t rva;
    const char *role;
    BYTE args, text[2], snapshot, archive_arg, lookup, length;
    BYTE signature[12];
} vp_spec;
typedef struct vp_target {
    uintptr_t base;
    size_t size;
    const vp_spec *specs;
    unsigned count;
    uint32_t manager_rva, anchor_rva;
    const BYTE *anchor;
    size_t anchor_length;
} vp_target;
typedef struct vp_range { uintptr_t start; size_t size; } vp_range;

/* Reference DBSIM.EXE (docs/reference_version.md). Signatures are the first
 * 12 bytes at each entry, read from the hash-verified file and checked
 * against the llvm-objdump listing; argument layouts from the same listing
 * (docs/formats/vol.md, "Registration probes from DllMain"). */
static const vp_spec vp_reference_specs[] = {
    {0x00073260u, "register", 4, {1, 2}, VP_SNAP_GLOBAL, VP_NONE, 0, 12,
     {0x55, 0x8b, 0xec, 0x83, 0xc4, 0xdc, 0xb8, 0xdc, 0x0d, 0x4a, 0x00, 0x53}},
    {0x00067b9cu, "manager_create", 5, {2, 3}, VP_SNAP_NONE, VP_NONE, 0, 12,
     {0x55, 0x8b, 0xec, 0x53, 0x8b, 0x5d, 0x08, 0x33, 0xd2, 0x8b, 0xc3, 0x33}},
    {0x00067cd8u, "manager_update", 5, {2, 3}, VP_SNAP_ARG0, VP_NONE, 0, 12,
     {0x55, 0x8b, 0xec, 0x81, 0xc4, 0xa8, 0xfe, 0xff, 0xff, 0x53, 0x56, 0x57}},
    {0x00067f40u, "slot_remove", 2, {VP_NONE, VP_NONE}, VP_SNAP_ARG0, VP_NONE, 0, 12,
     {0x55, 0x8b, 0xec, 0x53, 0xa1, 0xe4, 0xbe, 0x6c, 0x00, 0x56, 0x8b, 0x75}},
    {0x00067f94u, "insert", 3, {1, VP_NONE}, VP_SNAP_ARG0, VP_NONE, 0, 12,
     {0x55, 0x8b, 0xec, 0x53, 0x56, 0x57, 0xa1, 0x8c, 0x08, 0x4a, 0x00, 0x8b}},
    {0x00067fecu, "add_by_name", 2, {1, VP_NONE}, VP_SNAP_ARG0, VP_NONE, 0, 12,
     {0x55, 0x8b, 0xec, 0x53, 0x56, 0x57, 0x83, 0x7d, 0x0c, 0x00, 0x8b, 0x75}},
    {0x00067be8u, "manager_destroy", 2, {VP_NONE, VP_NONE}, VP_SNAP_ARG0, VP_NONE, 0, 12,
     {0x55, 0x8b, 0xec, 0x53, 0xa1, 0xe4, 0xbe, 0x6c, 0x00, 0x56, 0x8b, 0x75}},
    {0x0007cab0u, "archive_open", 3, {0, VP_NONE}, VP_SNAP_NONE, VP_NONE, 0, 12,
     {0x55, 0x8b, 0xec, 0x83, 0xc4, 0xb8, 0xb8, 0xac, 0x1b, 0x4a, 0x00, 0x53}},
    {0x000730e8u, "stream_open", 2, {1, VP_NONE}, VP_SNAP_NONE, VP_NONE, 1, 12,
     {0x55, 0x8b, 0xec, 0x83, 0xc4, 0xd8, 0x53, 0x56, 0x8b, 0x75, 0x0c, 0x8b}},
    {0x00068064u, "lookup", 3, {1, VP_NONE}, VP_SNAP_NONE, VP_NONE, 1, 12,
     {0x55, 0x8b, 0xec, 0x53, 0x56, 0x57, 0x8b, 0x7d, 0x08, 0x33, 0xdb, 0x8b}},
    {0x0007cdf8u, "member_lookup", 3, {1, VP_NONE}, VP_SNAP_NONE, 0, 1, 12,
     {0x55, 0x8b, 0xec, 0x83, 0xc4, 0xec, 0x53, 0x56, 0x57, 0x8b, 0x75, 0x08}},
};
/* 0x47327c: cmp dword [0x4a0d18], 0 -- the registration entry's test of the
 * global manager pointer whose slots are snapshotted. */
static const BYTE vp_reference_anchor[] = {0x83, 0x3d, 0x18, 0x0d, 0x4a, 0x00, 0x00};
static const vp_target vp_reference_target = {
    0x00400000u, 0x002d8000u, vp_reference_specs,
    (unsigned)(sizeof(vp_reference_specs) / sizeof(vp_reference_specs[0])),
    0x000a0d18u, 0x0007327cu, vp_reference_anchor, sizeof(vp_reference_anchor)};

typedef struct vp_site {
    uintptr_t address;
    const vp_spec *spec;
    volatile LONG64 count;
    volatile LONG installed, restored;
    DWORD protection;
    BYTE saved;
} vp_site;
typedef struct vp_event {
    volatile LONG ready;
    LONG frame, snapshot;
    DWORD thread, args[5];
    BYTE site, text_state[2];
    char text[2][VP_TEXT];
} vp_event;
typedef struct vp_snapshot {
    DWORD manager;
    LONG event;
    BYTE state, count;  /* state: 0 copied, 1 null manager, 2 unreadable */
    DWORD slots[VP_SLOTS];
} vp_snapshot;
typedef struct vp_archive {
    DWORD ptr, mask;
    LONG first_event;
    BYTE readable, priority, dirs;
    char name[21];
} vp_archive;

static vp_site vp_sites[VP_MAX_SITES];
static unsigned vp_count;
static vp_event vp_events[VP_MAX_EVENTS];
static vp_snapshot vp_snapshots[VP_MAX_SNAPSHOTS];
static vp_archive vp_archives[VP_MAX_ARCHIVES];
static volatile LONG vp_event_next, vp_events_dropped, vp_snapshot_next, vp_snapshots_dropped;
static volatile LONG vp_archive_count, vp_archive_lock, vp_archives_dropped;
static volatile LONG vp_active, vp_in_handler, vp_first_thread, vp_other_thread, vp_error, vp_frame = -1;
static PVOID vp_handler;
static uintptr_t vp_manager_global;
static int vp_state = VP_OFF, vp_module_verified = -1;
static BOOL vp_lookups = TRUE, vp_report_reserved, vp_forwarded, vp_complete, vp_has_tick;
static const char *vp_refusal, *vp_reason = "armed";
static WCHAR vp_report_path[MAX_PATH], vp_temp_path[MAX_PATH];
static DWORD vp_frame_limit, vp_period = 32, vp_attach_thread, vp_tick_thread, vp_last_frame;
static LONG vp_forwarding_event = -1, vp_published_events = -1;

static uint32_t vp_u32(const BYTE *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static BOOL vp_readable(uintptr_t p, size_t length) {
    if (!p || length > UINTPTR_MAX - p) return FALSE;
    uintptr_t end = p + length;
    while (p < end) {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery((void *)p, &m, sizeof(m)) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return FALSE;
        uintptr_t next = (uintptr_t)m.BaseAddress + m.RegionSize;
        if (next <= p) return FALSE;
        p = next;
    }
    return TRUE;
}
/* Checked copy; game pointers are never dereferenced directly. */
static BOOL vp_copy(uintptr_t address, void *out, size_t length) {
    SIZE_T got = 0;
    return address && length <= UINTPTR_MAX - address &&
        ReadProcessMemory(GetCurrentProcess(), (const void *)address, out, length, &got) && got == length;
}
static BYTE vp_copy_text(uintptr_t address, char *out) {
    memset(out, 0, VP_TEXT);
    if (!address) return VP_TEXT_NULL;
    size_t have = 0;
    while (have < VP_TEXT - 1) {
        size_t chunk = 4096u - ((address + have) & 4095u);
        if (chunk > VP_TEXT - 1 - have) chunk = VP_TEXT - 1 - have;
        if (!vp_copy(address + have, out + have, chunk)) {
            memset(out + have, 0, VP_TEXT - have);
            return VP_TEXT_UNREADABLE;
        }
        size_t end = strnlen(out + have, chunk);
        if (end < chunk) {
            memset(out + have + end, 0, VP_TEXT - have - end);
            return VP_TEXT_OK;
        }
        have += chunk;
    }
    out[VP_TEXT - 1] = 0;
    return VP_TEXT_TRUNCATED;
}
static DWORD vp_register(const CONTEXT *c, unsigned r) {
    switch (r) {
    case 0: return c->Eax; case 1: return c->Ecx; case 2: return c->Edx; case 3: return c->Ebx;
    case 4: return c->Esp; case 5: return c->Ebp; case 6: return c->Esi; default: return c->Edi;
    }
}
static void vp_note_archive(DWORD pointer, LONG event) {
    if (!pointer) return;
    vp_archive a;
    BYTE raw[VP_ARCHIVE_BYTES];
    memset(&a, 0, sizeof(a));
    a.ptr = pointer;
    a.first_event = event;
    if (vp_copy(pointer, raw, sizeof(raw))) {
        a.readable = 1;
        memcpy(a.name, raw + 8, 20);
        size_t n = strnlen(a.name, 20);
        memset(a.name + n, 0, sizeof(a.name) - n);
        a.priority = raw[0x1c];
        a.dirs = raw[0x1d];
        a.mask = vp_u32(raw + 0x20);
    }
    while (InterlockedCompareExchange(&vp_archive_lock, 1, 0)) YieldProcessor();
    BOOL known = FALSE;
    for (LONG i = 0; i < vp_archive_count && !known; ++i) {
        const vp_archive *k = &vp_archives[i];
        known = k->ptr == a.ptr && k->readable == a.readable && k->priority == a.priority &&
            k->dirs == a.dirs && k->mask == a.mask && !memcmp(k->name, a.name, sizeof(a.name));
    }
    if (!known) {
        if (vp_archive_count < (LONG)VP_MAX_ARCHIVES) vp_archives[vp_archive_count++] = a;
        else InterlockedIncrement(&vp_archives_dropped);
    }
    InterlockedExchange(&vp_archive_lock, 0);
}
static void vp_fill_snapshot(vp_snapshot *s, DWORD manager, LONG event) {
    BYTE raw[VP_MANAGER_BYTES];
    memset(s, 0, sizeof(*s));
    s->manager = manager;
    s->event = event;
    if (!manager) { s->state = 1; return; }
    if (!vp_copy(manager, raw, sizeof(raw))) { s->state = 2; return; }
    s->count = raw[0x78];
    unsigned n = s->count < VP_SLOTS ? s->count : VP_SLOTS;
    for (unsigned i = 0; i < n; ++i) {
        s->slots[i] = vp_u32(raw + 4 * i);
        vp_note_archive(s->slots[i], event);
    }
}
static DWORD vp_global_manager(void) {
    DWORD manager = 0;
    return vp_copy(vp_manager_global, &manager, 4) ? manager : 0;
}
static void vp_record(unsigned index, const CONTEXT *c) {
    const vp_spec *spec = vp_sites[index].spec;
    LONG n = InterlockedIncrement(&vp_event_next) - 1;
    if (n >= (LONG)VP_MAX_EVENTS) { InterlockedIncrement(&vp_events_dropped); return; }
    vp_event *e = &vp_events[n];
    e->site = (BYTE)index;
    e->thread = GetCurrentThreadId();
    e->frame = InterlockedCompareExchange(&vp_frame, 0, 0);
    e->snapshot = -1;
    /* The INT3 replaces the entry PUSH: [esp] is the return address. */
    for (unsigned a = 0; a < spec->args; ++a)
        if (!vp_copy((uintptr_t)c->Esp + 4u + 4u * a, &e->args[a], 4)) e->args[a] = 0;
    for (unsigned t = 0; t < 2; ++t)
        e->text_state[t] = spec->text[t] == VP_NONE ? VP_TEXT_NONE : vp_copy_text(e->args[spec->text[t]], e->text[t]);
    if (spec->snapshot != VP_SNAP_NONE) {
        LONG k = InterlockedIncrement(&vp_snapshot_next) - 1;
        if (k < (LONG)VP_MAX_SNAPSHOTS) {
            vp_fill_snapshot(&vp_snapshots[k], spec->snapshot == VP_SNAP_ARG0 ? e->args[0] : vp_global_manager(), n);
            e->snapshot = k;
        } else InterlockedIncrement(&vp_snapshots_dropped);
    }
    if (spec->archive_arg != VP_NONE) vp_note_archive(e->args[spec->archive_arg], n);
    InterlockedExchange(&e->ready, 1);
}
static LONG CALLBACK vp_exception(EXCEPTION_POINTERS *x) {
    if (!vp_active || x->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t pc = (uintptr_t)x->ExceptionRecord->ExceptionAddress;
    for (unsigned i = 0; i < vp_count; ++i) {
        vp_site *s = &vp_sites[i];
        if (pc != s->address || !s->installed || (*(BYTE *)pc != 0xcc && !(s->restored && *(BYTE *)pc == s->saved))) continue;
        DWORD error = GetLastError();
        InterlockedIncrement(&vp_in_handler);
        LONG tid = (LONG)GetCurrentThreadId(), first = InterlockedCompareExchange(&vp_first_thread, tid, 0);
        if (first && first != tid) InterlockedCompareExchange(&vp_other_thread, tid, 0);
        InterlockedIncrement64(&s->count);
        vp_record(i, x->ContextRecord);
        DWORD value = vp_register(x->ContextRecord, s->saved - 0x50u);
        x->ContextRecord->Esp -= 4;
        *(DWORD *)(uintptr_t)x->ContextRecord->Esp = value;
        x->ContextRecord->Eip = (DWORD)(pc + 1);
        InterlockedDecrement(&vp_in_handler);
        SetLastError(error);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
/* Restores only an INT3 this module wrote; another instrumenter's byte is
 * never overwritten. */
static BOOL vp_restore(vp_site *s) {
    if (!s->installed || s->restored) return TRUE;
    BYTE *p = (BYTE *)s->address;
    if (*p != 0xcc) { InterlockedCompareExchange(&vp_error, 2, 0); return FALSE; }
    DWORD old, ignored;
    if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old)) { InterlockedCompareExchange(&vp_error, 3, 0); return FALSE; }
    *p = s->saved;
    BOOL flushed = FlushInstructionCache(GetCurrentProcess(), p, 1);
    BOOL protected = VirtualProtect(p, 1, s->protection, &ignored);
    if (!flushed || !protected) { InterlockedCompareExchange(&vp_error, 4, 0); return FALSE; }
    InterlockedExchange(&s->restored, 1);
    return TRUE;
}
/* Requires quiescence, as entry probes: no thread inside a probed entry. */
static BOOL vp_disarm(void) {
    if (vp_in_handler) { InterlockedCompareExchange(&vp_error, 5, 0); return FALSE; }
    BOOL ok = TRUE;
    for (unsigned i = 0; i < vp_count; ++i) if (!vp_restore(&vp_sites[i])) ok = FALSE;
    if (!ok) return FALSE;
    if (vp_handler && !RemoveVectoredExceptionHandler(vp_handler)) { InterlockedCompareExchange(&vp_error, 8, 0); return FALSE; }
    vp_handler = NULL;
    vp_active = 0;
    return TRUE;
}
static BOOL vp_excluded(uintptr_t address, const vp_range *excluded, unsigned nexcluded) {
    for (unsigned j = 0; j < nexcluded; ++j)
        if (address >= excluded[j].start && address - excluded[j].start < excluded[j].size) return TRUE;
    return FALSE;
}
/* A lookup-set entry inside an excluded range is left out (the
 * three lookup entries are registered hooks of group vfs, whose counters
 * count the same entries); any other excluded entry refuses the set. */
static BOOL vp_skipped(BYTE *image, const vp_spec *s, BOOL lookups, const vp_range *excluded, unsigned nexcluded) {
    return s->lookup && (!lookups || vp_excluded((uintptr_t)image + s->rva, excluded, nexcluded));
}
/* Validates everything before the first byte changes; a refusal patches
 * nothing. */
static BOOL vp_arm(BYTE *image, const vp_target *t, BOOL lookups, const vp_range *excluded, unsigned nexcluded, const char **why) {
    *why = "busy";
    if (vp_active || vp_handler || !t->count) return FALSE;
    *why = "image";
    if ((uintptr_t)image != t->base || !es2_hook_image_valid(image, t->base, t->size) || t->manager_rva > t->size - 4 ||
        t->anchor_length > t->size || t->anchor_rva > t->size - t->anchor_length ||
        !vp_readable((uintptr_t)image + t->anchor_rva, t->anchor_length) || memcmp(image + t->anchor_rva, t->anchor, t->anchor_length))
        return FALSE;
    *why = "sites";
    unsigned n = 0;
    for (unsigned i = 0; i < t->count; ++i) {
        const vp_spec *s = &t->specs[i];
        if (vp_skipped(image, s, lookups, excluded, nexcluded)) continue;
        BYTE op = s->signature[0];
        if (n == VP_MAX_SITES || !s->length || s->length > sizeof(s->signature) || s->args > 5 ||
            s->rva >= t->size || s->length > t->size - s->rva || op < 0x50 || op > 0x57 || op == 0x54 ||
            (s->archive_arg != VP_NONE && s->archive_arg >= s->args) || s->snapshot > VP_SNAP_GLOBAL ||
            (s->snapshot == VP_SNAP_ARG0 && !s->args)) return FALSE;
        for (unsigned k = 0; k < 2; ++k) if (s->text[k] != VP_NONE && s->text[k] >= s->args) return FALSE;
        uintptr_t address = (uintptr_t)image + s->rva;
        if (!vp_readable(address, s->length) || memcmp((const BYTE *)address, s->signature, s->length)) return FALSE;
        for (unsigned j = 0; j < n; ++j) if (vp_sites[j].address == address) return FALSE;
        if (vp_excluded(address, excluded, nexcluded)) return FALSE;
        n++;
    }
    if (!n) return FALSE;
    vp_count = 0;
    for (unsigned i = 0; i < t->count; ++i) {
        const vp_spec *s = &t->specs[i];
        if (vp_skipped(image, s, lookups, excluded, nexcluded)) continue;
        vp_site *site = &vp_sites[vp_count++];
        memset(site, 0, sizeof(*site));
        site->address = (uintptr_t)image + s->rva;
        site->spec = s;
        site->saved = s->signature[0];
    }
    vp_event_next = vp_events_dropped = vp_snapshot_next = vp_snapshots_dropped = 0;
    vp_archive_count = vp_archives_dropped = 0;
    vp_error = vp_first_thread = vp_other_thread = 0;
    vp_manager_global = (uintptr_t)image + t->manager_rva;
    *why = "install";
    vp_handler = AddVectoredExceptionHandler(1, vp_exception);
    if (!vp_handler) return FALSE;
    vp_active = 1;
    for (unsigned i = 0; i < vp_count; ++i) {
        vp_site *s = &vp_sites[i];
        DWORD old, ignored;
        if (!VirtualProtect((void *)s->address, 1, PAGE_EXECUTE_READWRITE, &old)) { vp_disarm(); return FALSE; }
        s->protection = old;
        s->installed = 1;
        *(BYTE *)s->address = 0xcc;
        BOOL flushed = FlushInstructionCache(GetCurrentProcess(), (void *)s->address, 1);
        BOOL protected = VirtualProtect((void *)s->address, 1, old, &ignored);
        if (!flushed || !protected) { vp_disarm(); return FALSE; }
    }
    return TRUE;
}

/* ---- report: schema 1, atomically published like probes.json ---------- */
typedef struct vp_writer { HANDLE file; BOOL ok; } vp_writer;
static void vp_put(vp_writer *w, const char *format, ...) {
    if (!w->ok) return;
    char text[1024];
    va_list a;
    va_start(a, format);
    int n = vsnprintf(text, sizeof(text), format, a);
    va_end(a);
    DWORD written = 0;
    if (n < 0 || n >= (int)sizeof(text) || !WriteFile(w->file, text, (DWORD)n, &written, NULL) || written != (DWORD)n) w->ok = FALSE;
}
static void vp_put_string(vp_writer *w, const char *s, size_t max) {
    char text[6 * VP_TEXT + 4];
    size_t n = 0;
    text[n++] = '"';
    for (size_t i = 0; i < max && i < VP_TEXT && s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') { text[n++] = '\\'; text[n++] = (char)c; }
        else if (c < 0x20 || c > 0x7e) n += (size_t)snprintf(text + n, sizeof(text) - n, "\\u%04x", c);
        else text[n++] = (char)c;
    }
    text[n++] = '"';
    text[n] = 0;
    vp_put(w, "%s", text);
}
static void vp_put_snapshot(vp_writer *w, const vp_snapshot *s) {
    static const char *const states[] = {"copied", "null_manager", "unreadable"};
    vp_put(w, "{\"event\":%ld,\"manager\":\"0x%08lx\",\"state\":\"%s\",\"count\":%u,\"slots\":[", (long)s->event,
           (unsigned long)s->manager, states[s->state <= 2 ? s->state : 2], (unsigned)s->count);
    unsigned n = s->state ? 0 : (s->count < VP_SLOTS ? s->count : VP_SLOTS);
    for (unsigned i = 0; i < n; ++i) vp_put(w, "%s\"0x%08lx\"", i ? "," : "", (unsigned long)s->slots[i]);
    vp_put(w, "]}");
}
static BOOL vp_publish(const char *reason) {
    if (!vp_report_reserved) return FALSE;
    vp_reason = reason;
    vp_snapshot final;
    BOOL armed = vp_count != 0 && vp_state != VP_REFUSED;
    if (armed) vp_fill_snapshot(&final, vp_global_manager(), -1);
    vp_writer w = {CreateFileW(vp_temp_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL), TRUE};
    if (w.file == INVALID_HANDLE_VALUE) { InterlockedCompareExchange(&vp_error, 7, 0); return FALSE; }
    LONG total = vp_event_next, shown = total < (LONG)VP_MAX_EVENTS ? total : (LONG)VP_MAX_EVENTS;
    LONG snapshots = vp_snapshot_next < (LONG)VP_MAX_SNAPSHOTS ? vp_snapshot_next : (LONG)VP_MAX_SNAPSHOTS;
    vp_put(&w, "{\"schema\":1,\"kind\":\"vol_probes\",\"module_sha256\":\"%s\",\"module_verified\":%s,\"pid\":%lu,"
           "\"armed\":%s,\"armed_at\":\"process_attach\",\"refusal\":",
           VP_MODULE_SHA256, vp_module_verified < 0 ? "null" : vp_module_verified ? "true" : "false",
           (unsigned long)GetCurrentProcessId(), armed ? "true" : "false");
    if (vp_refusal) vp_put_string(&w, vp_refusal, 64); else vp_put(&w, "null");
    vp_put(&w, ",\"attach_thread\":%lu,\"first_thread\":%lu,\"other_thread\":%lu,\"tick_thread\":%lu,\"error\":%ld,"
           "\"complete\":%s,\"reason\":\"%s\",\"has_tick\":%s,\"last_frame\":%lu,\"frame_limit\":%lu,\"lookups\":%s,",
           (unsigned long)vp_attach_thread, (unsigned long)vp_first_thread, (unsigned long)vp_other_thread,
           (unsigned long)vp_tick_thread, (long)vp_error, vp_complete ? "true" : "false", reason,
           vp_has_tick ? "true" : "false", (unsigned long)vp_last_frame, (unsigned long)vp_frame_limit, vp_lookups ? "true" : "false");
    if (vp_forwarding_event < 0) vp_put(&w, "\"forwarding_event\":null,");
    else vp_put(&w, "\"forwarding_event\":%ld,", (long)vp_forwarding_event);
    vp_put(&w, "\"events_total\":%ld,\"events_dropped\":%ld,\"snapshots_dropped\":%ld,\"archives_dropped\":%ld,\"sites\":[",
           (long)total, (long)vp_events_dropped, (long)vp_snapshots_dropped, (long)vp_archives_dropped);
    for (unsigned i = 0; i < vp_count; ++i)
        vp_put(&w, "%s{\"va\":\"0x%08lx\",\"role\":\"%s\",\"count\":%llu,\"restored\":%s}", i ? "," : "",
               (unsigned long)vp_sites[i].address, vp_sites[i].spec->role,
               (unsigned long long)InterlockedCompareExchange64(&vp_sites[i].count, 0, 0), vp_sites[i].restored ? "true" : "false");
    vp_put(&w, "],\"events\":[");
    for (LONG i = 0; i < shown; ++i) {
        const vp_event *e = &vp_events[i];
        if (!e->ready) { vp_put(&w, "%s{\"seq\":%ld,\"pending\":true}", i ? "," : "", (long)i); continue; }
        const vp_site *s = &vp_sites[e->site];
        vp_put(&w, "%s{\"seq\":%ld,\"va\":\"0x%08lx\",\"role\":\"%s\",\"thread\":%lu,\"frame\":%ld,\"args\":[", i ? "," : "",
               (long)i, (unsigned long)s->address, s->spec->role, (unsigned long)e->thread, (long)e->frame);
        for (unsigned a = 0; a < s->spec->args; ++a) vp_put(&w, "%s\"0x%08lx\"", a ? "," : "", (unsigned long)e->args[a]);
        vp_put(&w, "],\"text\":[");
        for (unsigned t = 0; t < 2; ++t) {
            if (t) vp_put(&w, ",");
            if (e->text_state[t] == VP_TEXT_OK || e->text_state[t] == VP_TEXT_TRUNCATED || e->text_state[t] == VP_TEXT_UNREADABLE)
                vp_put_string(&w, e->text[t], VP_TEXT);
            else vp_put(&w, "null");
        }
        vp_put(&w, "],\"text_state\":[\"%s\",\"%s\"],\"snapshot\":", vp_text_states[e->text_state[0] % 5], vp_text_states[e->text_state[1] % 5]);
        if (e->snapshot < 0) vp_put(&w, "null}"); else vp_put(&w, "%ld}", (long)e->snapshot);
    }
    vp_put(&w, "],\"snapshots\":[");
    for (LONG i = 0; i < snapshots; ++i) { if (i) vp_put(&w, ","); vp_put_snapshot(&w, &vp_snapshots[i]); }
    vp_put(&w, "],\"final\":");
    if (armed) vp_put_snapshot(&w, &final); else vp_put(&w, "null");
    vp_put(&w, ",\"archives\":[");
    while (InterlockedCompareExchange(&vp_archive_lock, 1, 0)) YieldProcessor();
    for (LONG i = 0; i < vp_archive_count; ++i) {
        const vp_archive *a = &vp_archives[i];
        vp_put(&w, "%s{\"ptr\":\"0x%08lx\",\"readable\":%s,\"name\":", i ? "," : "", (unsigned long)a->ptr, a->readable ? "true" : "false");
        vp_put_string(&w, a->name, 20);
        vp_put(&w, ",\"priority\":%u,\"dirs\":%u,\"mask\":\"0x%08lx\",\"first_event\":%ld}",
               (unsigned)a->priority, (unsigned)a->dirs, (unsigned long)a->mask, (long)a->first_event);
    }
    InterlockedExchange(&vp_archive_lock, 0);
    vp_put(&w, "]}\n");
    BOOL ok = w.ok && FlushFileBuffers(w.file);
    if (!CloseHandle(w.file)) ok = FALSE;
    if (ok) ok = MoveFileExW(vp_temp_path, vp_report_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) { DeleteFileW(vp_temp_path); InterlockedCompareExchange(&vp_error, 7, 0); return FALSE; }
    vp_published_events = total;
    return TRUE;
}

/* ---- configuration and life cycle -------------------------------------- */
static BOOL vp_get(const WCHAR *ini, const WCHAR *key, const WCHAR *fallback, WCHAR *out, DWORD size) {
    return GetPrivateProfileStringW(L"vol_probes", key, fallback, out, size, ini) < size - 1;
}
static BOOL vp_number(const WCHAR *ini, const WCHAR *key, const WCHAR *fallback, DWORD *out) {
    WCHAR text[24];
    if (!vp_get(ini, key, fallback, text, 24) || !*text) return FALSE;
    uint32_t n = 0;
    for (unsigned i = 0; text[i]; ++i) {
        unsigned d = (unsigned)(text[i] - L'0');
        if (d > 9 || n > (UINT32_MAX - d) / 10) return FALSE;
        n = n * 10 + d;
    }
    *out = n;
    return TRUE;
}
/* 0 off (key absent or enabled=0), 1 on, -1 malformed. */
static int vol_probes_configuration(const WCHAR *ini) {
    WCHAR enabled[8], lookups[8];
    if (!vp_get(ini, L"enabled", L"0", enabled, 8)) return -1;
    if (!wcscmp(enabled, L"0")) return 0;
    if (wcscmp(enabled, L"1") || !vp_get(ini, L"lookups", L"1", lookups, 8) ||
        (wcscmp(lookups, L"0") && wcscmp(lookups, L"1")) ||
        !vp_number(ini, L"frame_limit", L"0", &vp_frame_limit) ||
        !vp_number(ini, L"report_every_ticks", L"32", &vp_period) ||
        !vp_get(ini, L"report", L"", vp_report_path, MAX_PATH) || !*vp_report_path ||
        wcslen(vp_report_path) > MAX_PATH - 32) return -1;
    vp_lookups = lookups[0] == L'1';
    return 1;
}
static BOOL vp_is_dbsim(const WCHAR *exe) {
    const WCHAR *name = exe ? wcsrchr(exe, L'\\') : NULL;
    return name && !_wcsicmp(name + 1, L"DBSIM.EXE");
}
static void vp_refuse(const char *why) { vp_state = VP_REFUSED; vp_refusal = why; }
/* DllMain (DLL_PROCESS_ATTACH) part. Runs once. */
static void vol_probes_attach(BYTE *image, const WCHAR *exe, const vp_target *target, const vp_range *excluded, unsigned nexcluded) {
    if (vp_state != VP_OFF || !vp_is_dbsim(exe)) return;
    WCHAR ini[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ES2_LIVE_COVERAGE_CONFIG", ini, MAX_PATH);
    if (!n) return;
    if (n >= MAX_PATH) { vp_refuse("config_path"); return; }
    int configured = vol_probes_configuration(ini);
    if (!configured) return;
    if (configured < 0) { vp_refuse("config"); return; }
    swprintf(vp_temp_path, MAX_PATH, L"%ls.tmp.%lu", vp_report_path, (unsigned long)GetCurrentProcessId());
    /* A fresh report only: an earlier file can never be mistaken for this run. */
    HANDLE reserve = CreateFileW(vp_report_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (reserve == INVALID_HANDLE_VALUE) { vp_refuse("report_exists"); return; }
    CloseHandle(reserve);
    vp_report_reserved = TRUE;
    vp_attach_thread = GetCurrentThreadId();
    const char *why = "sites";
    if (!vp_arm(image, target, vp_lookups, excluded, nexcluded, &why)) { vp_count = 0; vp_refuse(why); return; }
    vp_state = VP_ARMED;
}
static void vp_finish(const char *reason) {
    if (vp_state != VP_ARMED) return;
    BOOL owned = !vp_other_thread && (!vp_first_thread || (DWORD)vp_first_thread == GetCurrentThreadId());
    BOOL restored = FALSE;
    /* A second executing thread invalidates quiescence: keep the handler and
     * INT3s rather than removing them underneath it. */
    if (!owned) InterlockedCompareExchange(&vp_error, 6, 0);
    else restored = vp_disarm();
    vp_complete = restored && !vp_error;
    vp_state = VP_DONE;
    vp_publish(reason);
}
/* First DirectDraw/MCI call (initialize_forwarding): outside the loader lock.
 * verified = module file SHA-256 equals the reference. */
static void vol_probes_forwarding_verified(BOOL verified) {
    if (vp_state == VP_OFF || vp_forwarded) return;
    vp_forwarded = TRUE;
    if (vp_state != VP_ARMED) { if (vp_state == VP_REFUSED && vp_report_reserved) vp_publish("refused"); return; }
    vp_forwarding_event = vp_event_next;
    vp_module_verified = verified ? 1 : 0;
    if (!verified) { InterlockedCompareExchange(&vp_error, 10, 0); vp_finish("module_hash_mismatch"); return; }
    vp_publish("forwarding");
}
/* Tick boundary (tick_log.h), after the tick record has been written. */
static void vol_probes_tick(DWORD frame) {
    if (vp_state != VP_ARMED) return;
    __attribute__((aligned(16))) BYTE fpu[512];
    __asm__ volatile("fxsave %0" : "=m"(fpu));
    DWORD error = GetLastError();
    InterlockedExchange(&vp_frame, (LONG)frame);
    vp_has_tick = TRUE;
    vp_last_frame = frame;
    vp_tick_thread = GetCurrentThreadId();
    if (vp_frame_limit && frame >= vp_frame_limit - 1) vp_finish("frame_limit");
    else if (vp_period && frame % vp_period == 0 && vp_event_next != vp_published_events) vp_publish("tick");
    SetLastError(error);
    __asm__ volatile("fxrstor %0" : : "m"(fpu));
}
/* Process detach (or a test's end). */
static void vol_probes_shutdown(const char *reason) {
    if (vp_state == VP_OFF) return;
    if (vp_state == VP_REFUSED) { if (vp_report_reserved && !vp_forwarded) { vp_forwarded = TRUE; vp_publish("refused"); } return; }
    if (vp_state != VP_ARMED) return;
    __attribute__((aligned(16))) BYTE fpu[512];
    __asm__ volatile("fxsave %0" : "=m"(fpu));
    DWORD error = GetLastError();
    vp_finish(reason);
    SetLastError(error);
    __asm__ volatile("fxrstor %0" : : "m"(fpu));
}

#if defined(ES2_RNG_HOOKS_H) && defined(ES2_LIVE_COVERAGE_H)
/* Proxy integration (media_proxy.c). Registered hook relocation ranges and
 * all coverage branch sites are excluded regardless of their later modes. */
static void vol_probes_process_attach(void) {
    WCHAR exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH || !vp_is_dbsim(exe) || !GetEnvironmentVariableW(L"ES2_LIVE_COVERAGE_CONFIG", NULL, 0)) return;
    static vp_range ranges[FUNCTION_HOOK_COUNT + LIVE_SITE_COUNT];
    BYTE *image = (BYTE *)GetModuleHandleW(NULL);
    unsigned k = 0;
    for (unsigned i = 0; i < FUNCTION_HOOK_COUNT; ++i) {
        const es2_hook_descriptor *d = function_hook_descriptor(i);
        ranges[k++] = (vp_range){(uintptr_t)image + d->rva, d->relocate_size};
    }
    for (unsigned i = 0; i < LIVE_SITE_COUNT; ++i) ranges[k++] = (vp_range){live_all_sites[i].address, live_all_sites[i].length};
    vol_probes_attach(image, exe, &vp_reference_target, ranges, k);
}
static void vol_probes_forwarding(void) {
    if (vp_state == VP_OFF || vp_forwarded) return;
    if (vp_state != VP_ARMED) {
        if (vp_state == VP_REFUSED) log_event("vol_probes_refused");
        vol_probes_forwarding_verified(FALSE);
        return;
    }
    WCHAR exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    BOOL verified = n && n < MAX_PATH && rng_hook_reference_file(exe);
    vol_probes_forwarding_verified(verified);
    log_event(verified ? "vol_probes_forwarding" : "vol_probes_hash_refused");
}
#endif
#endif
