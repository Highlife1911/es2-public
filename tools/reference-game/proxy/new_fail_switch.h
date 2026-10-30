/* The injection switch for DBSIM's `new` (0x47749c) and the report of
 * the protection of DBSIM's CODE pages. Contract, evidence and use:
 * docs/reference-game/rtl_imports.md, section "the CODE pages' protection
 * and the `new` failure switch".
 *
 * Purpose. DBSIM's callback registration 0x479cfc (called by six sites, the
 * "assert notifier" of rtl_imports.md "error and exception paths of the own
 * runtime") asks `new` for 8 bytes at
 * 0x479d02. A NULL answer sends it to _fputs(string, FILE 0x4968b6) and then
 * to _abort; the FILE lies in the read-only CODE section, so the original
 * runtime faults in its flag update (0x406287). Memory is never exhausted
 * in a normal run, so this path is reached only by forcing the NULL. The
 * switch forces it for one request; the page report says whether the
 * proxy's patching left the FILE's page (or any CODE page) writable, which
 * would turn that fault into a write.
 *
 * Off unless the environment has ES2_NEW_FAIL=<caller>:<n> (case-insensitive):
 *   <caller>  the call site of 0x479cfc whose request counts, as a hexadecimal
 *             address (46162b, 4779e5, 48063f, 480b23, 480f70, 480fbc; an
 *             optional 0x), or `any` for the requests of all six sites
 *   <n>       decimal 0..1000000: the n-th counted request is answered with
 *             NULL, once. 0 only counts and records (no injection)
 * ES2_NEW_FAIL_LOG=<path> names the NDJSON file (appended) that receives the
 * records below; without it nothing is written (the counters stay in memory).
 * Every `{suite}` in the path is replaced by the suite of the clone
 * (the stem of [trace] tick_log in the INI beside the proxy, "unknown"
 * without), so one variable gives each suite of a parallel run its own file.
 *
 * What it changes. Only the rel32 of the single `call 0x47749c` at 0x479d02,
 * which enters nf_entry instead. nf_entry reads the size argument and the
 * return address of the registration's caller from the stack (the frame of
 * 0x479cfc is `push ebp; mov ebp,esp; push ebx; push 8; call`), asks
 * nf_decide, and either returns EAX 0 (stack and registers as the real
 * `new` leaves them: cdecl, the caller pops its argument) or jumps to the
 * real `new` with the stack untouched. Every other caller of `new` is
 * untouched. DBSIM.EXE only. DllMain-safe: kernel32 only, memory only. The
 * image, `new`'s prologue, the registration's first instructions and the six
 * call sites are compared with the reference bytes first, otherwise nothing
 * is changed (nf_state says why).
 *
 * Records (one JSON object per line, CRLF), only with the log path:
 *   new_fail_installed   caller, n, image
 *   new_fail_request     one per request from a counted site (max 256):
 *                        site, ordinal (among the counted requests), ordinal_site,
 *                        inject
 *   new_fail_pages       the CODE pages' protection (nf_scan_code), at "armed"
 *                        (after the proxy's hooks, nf_report_armed) and at
 *                        "inject" (just before the NULL is returned)
 *   new_fail_exception   after the injection, the first severe exceptions
 *                        (max 8): code, address, module and offset, access
 *                        type and address, the page's protection, ESP and
 *                        whether it lies on the thread's own stack, and
 *                        the SEH chain at that moment (seh, seh_count, seh_end).
 *                        The handler only records; the exception goes on to
 *                        the process's own handlers.
 *   new_fail_exit_probe  once, at the injection: the exit-path probe's
 *                        state (armed, no_module, no_import, slot_mismatch,
 *                        write_refused, bad_image), CW3220.DLL's base, size and
 *                        timestamp, the import slot and its content
 *   new_fail_exit_filter when the patched import slot of kernel32's
 *                        UnhandledExceptionFilter is called (max 4): the
 *                        return address (caller module and offset: __startup's
 *                        record in the original runtime, own_startup_handler in
 *                        the own), the exception record the caller passes, the
 *                        stack the call runs on, the SEH chain
 *   new_fail_exit_filter_result  the real filter's answer, if it returns
 * The exit-path probe patches nothing until the injection happens: it
 * replaces one import slot of CW3220.DLL (only when it holds kernel32's own
 * export, after the walk of the import table) by a thunk that records and
 * forwards the call unchanged.
 *   new_fail_summary     counters at process detach (not reached when a
 *                        fault ends the process)
 */
#ifndef ES2_NEW_FAIL_SWITCH_H
#define ES2_NEW_FAIL_SWITCH_H
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "hook/hook.h"

#define NF_LINK_BASE 0x00400000u
#define NF_IMAGE_SIZE 0x002d8000u
#define NF_NEW_RVA 0x0007749cu          /* new: push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8]; cmp ebx,[0x6bb304] */
#define NF_REG_RVA 0x00079cfcu          /* the registration: push ebp; mov ebp,esp; push ebx; push 8; call new */
#define NF_CALL_RVA 0x00079d02u         /* call 0x47749c */
#define NF_FILE_RVA 0x000968b6u         /* the FILE _fputs gets: __streams' thunk + 0x30, inside CODE */
_Static_assert(NF_CALL_RVA == NF_REG_RVA + 6u, "the call follows `push ebp; mov ebp,esp; push ebx; push 8`");
#define NF_SITE_COUNT 6u
#define NF_LOG_MAX 256u
#define NF_EXCEPTION_MAX 8u
#define NF_PROTECT_KINDS 8u
#define NF_WRITABLE_LISTED 16u

static const uint32_t nf_site_rvas[NF_SITE_COUNT] = {
    0x0006162bu, 0x000779e5u, 0x0008063fu, 0x00080b23u, 0x00080f70u, 0x00080fbcu   /* call 0x479cfc, each preceded by `push callback` and followed by `pop ecx` */
};

enum { NF_OFF = 0, NF_INSTALLED = 1, NF_REFUSED_IMAGE = -1, NF_REFUSED_SITE = -2, NF_REFUSED_WRITE = -3, NF_REFUSED_CONFIG = -4 };
static volatile LONG nf_state;
static BYTE *nf_image;
static uint32_t nf_filter_rva;                  /* 0: any of the six sites */
static uint32_t nf_nth;                         /* 0: count only */
static WCHAR nf_log_path[MAX_PATH];
static BYTE *nf_pristine;                       /* the file's image in memory layout, loaded on first use */
static BOOL nf_pristine_tried;
static BYTE *nf_site;
static uint32_t nf_original_rel;
__attribute__((used)) static volatile uint32_t nf_real_target;   /* nf_entry jumps here */
static volatile LONG nf_requests;               /* every request that reached nf_entry */
static volatile LONG nf_other;                  /* of them: not 8 bytes or not from the six sites */
static volatile LONG nf_counted;                /* requests from the counted sites */
static volatile LONG nf_by_site[NF_SITE_COUNT];
static volatile LONG nf_injected;
static volatile LONG nf_logged;
static volatile LONG nf_exceptions;
static PVOID nf_veh_handle;
/* The observation of the exit path (see "the exit path" below). */
enum { NF_PROBE_OFF = 0, NF_PROBE_ARMED = 1, NF_PROBE_NO_MODULE = -1, NF_PROBE_NO_IMPORT = -2, NF_PROBE_MISMATCH = -3, NF_PROBE_WRITE = -4, NF_PROBE_BAD_IMAGE = -5 };
static volatile LONG nf_probe_state;
static volatile LONG nf_filter_calls, nf_filter_returns;
static uint32_t *nf_probe_slot;                 /* the patched import slot */
static uint32_t nf_probe_real;                  /* what the slot held: kernel32's UnhandledExceptionFilter */

static uint32_t nf_u32(const BYTE *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint32_t nf_u16(const BYTE *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

/* ---- the log ------------------------------------------------------------ */
static void nf_write(const char *line) {
    if (!nf_log_path[0]) return;
    HANDLE file = CreateFileW(nf_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, line, (DWORD)strlen(line), &written, NULL);
    CloseHandle(file);
}
static void nf_event(const char *event, const char *fields) {
    char line[4096];
    int n = snprintf(line, sizeof(line), "{\"event\":\"%s\",\"pid\":%lu%s%s}\r\n", event, (unsigned long)GetCurrentProcessId(),
                     fields && *fields ? "," : "", fields ? fields : "");
    if (n > 0 && n < (int)sizeof(line)) nf_write(line);
}

/* ---- the protection of the CODE pages ----------------------------------- */
typedef struct nf_scan {
    uint32_t sections, pages, patched, unpatched, unknown;
    uint32_t writable_patched, writable_unpatched;
    unsigned kinds;
    DWORD protect[NF_PROTECT_KINDS];
    uint32_t kind_patched[NF_PROTECT_KINDS], kind_unpatched[NF_PROTECT_KINDS], kind_unknown[NF_PROTECT_KINDS];
    unsigned writable_count;
    uint32_t writable_rva[NF_WRITABLE_LISTED];
    uint32_t query_failed;
} nf_scan;

#define NF_WRITABLE_MASK (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)

/* VirtualQuery of the page at `address`: its protection (0 when the query
 * fails or the page is not committed). */
static DWORD nf_page_protect(const void *address) {
    MEMORY_BASIC_INFORMATION region;
    if (!VirtualQuery(address, &region, sizeof(region)) || region.State != MEM_COMMIT) return 0;
    return region.Protect;
}

/* The pages of every code section of `image` (section headers read from the
 * mapped image: bounded by the header page). With `pristine`, the file's
 * image in memory layout, a page counts as patched when its bytes differ
 * from it within the section's file-backed size; without, the patched state
 * is unknown. Returns the number of code sections. */
static unsigned nf_scan_code(const BYTE *image, const BYTE *pristine, nf_scan *out) {
    memset(out, 0, sizeof(*out));
    uint32_t pe = nf_u32(image + 0x3c);
    if (pe < 64 || pe > 4096 - 248) return 0;
    const BYTE *nt = image + pe;
    unsigned sections = nf_u16(nt + 6), optional = nf_u16(nt + 20);
    if (!sections || sections > 96 || pe + 24 + optional + 40u * sections > 4096) return 0;
    for (unsigned s = 0; s < sections; ++s) {
        const BYTE *h = nt + 24 + optional + 40 * s;
        uint32_t vsize = nf_u32(h + 8), rva = nf_u32(h + 12), raw = nf_u32(h + 16), flags = nf_u32(h + 36);
        if (!(flags & IMAGE_SCN_CNT_CODE) || !(flags & IMAGE_SCN_MEM_EXECUTE) || !vsize) continue;
        ++out->sections;
        uint32_t compare = raw < vsize ? raw : vsize;           /* bytes the file backs */
        for (uint32_t page = rva & ~0xfffu; page < rva + vsize; page += 0x1000u) {
            ++out->pages;
            DWORD protect = nf_page_protect(image + page);
            if (!protect) ++out->query_failed;
            unsigned k = 0;
            while (k < out->kinds && out->protect[k] != protect) ++k;
            if (k == out->kinds) { if (k == NF_PROTECT_KINDS) k = NF_PROTECT_KINDS - 1; else { out->protect[k] = protect; ++out->kinds; } }
            int patched = -1;
            if (pristine && protect) {
                uint32_t from = page > rva ? page : rva, to = page + 0x1000u < rva + compare ? page + 0x1000u : rva + compare;
                patched = to > from && memcmp(image + from, pristine + from, to - from) != 0;
            }
            if (patched < 0) { ++out->unknown; ++out->kind_unknown[k]; }
            else if (patched) { ++out->patched; ++out->kind_patched[k]; }
            else { ++out->unpatched; ++out->kind_unpatched[k]; }
            if (protect & NF_WRITABLE_MASK) {
                if (patched > 0) ++out->writable_patched; else ++out->writable_unpatched;
                if (out->writable_count < NF_WRITABLE_LISTED) out->writable_rva[out->writable_count++] = page;
            }
        }
    }
    return out->sections;
}

/* The file's image in memory layout (headers and the raw data of every
 * section at its RVA, the rest zero), or NULL: bounds-checked, the image
 * size must be `image_size`. */
static BYTE *nf_map_file(const BYTE *f, size_t n, uint32_t image_size) {
    if (n < 0x400 || nf_u16(f) != 0x5a4du) return NULL;
    uint32_t pe = nf_u32(f + 0x3c);
    if (pe < 64 || pe > n - 248 || nf_u32(f + pe) != 0x00004550u) return NULL;
    const BYTE *nt = f + pe;
    unsigned sections = nf_u16(nt + 6), optional = nf_u16(nt + 20);
    uint32_t headers = nf_u32(nt + 24 + 60);
    if (!sections || sections > 96 || optional < 96 || nf_u32(nt + 24 + 56) != image_size ||
        pe + 24 + (size_t)optional + 40u * sections > n || headers > image_size || headers > n) return NULL;
    BYTE *mem = calloc(1, image_size);
    if (!mem) return NULL;
    memcpy(mem, f, headers);
    for (unsigned s = 0; s < sections; ++s) {
        const BYTE *h = nt + 24 + optional + 40 * s;
        uint32_t rva = nf_u32(h + 12), raw = nf_u32(h + 16), offset = nf_u32(h + 20);
        if (!raw) continue;
        if (rva > image_size || raw > image_size - rva || offset > n || raw > n - offset) { free(mem); return NULL; }
        memcpy(mem + rva, f + offset, raw);
    }
    return mem;
}

/* The running executable's file as a memory-layout image; once. */
static const BYTE *nf_load_pristine(void) {
    if (nf_pristine_tried) return nf_pristine;
    nf_pristine_tried = TRUE;
    WCHAR path[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return NULL;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER size;
    BYTE *bytes = NULL;
    DWORD read = 0;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 16 * 1024 * 1024 && (bytes = malloc((size_t)size.QuadPart)) &&
        ReadFile(file, bytes, (DWORD)size.QuadPart, &read, NULL) && read == (DWORD)size.QuadPart) {
        nf_pristine = nf_map_file(bytes, read, NF_IMAGE_SIZE);
    }
    free(bytes);
    CloseHandle(file);
    return nf_pristine;
}

static int nf_runs_json(const nf_scan *s, char *out, size_t size) {
    int used = 0;
    for (unsigned k = 0; k < s->kinds && used >= 0 && (size_t)used < size; ++k)
        used += snprintf(out + used, size - (size_t)used, "%s{\"protect\":\"0x%lx\",\"patched\":%lu,\"unpatched\":%lu,\"unknown\":%lu}", k ? "," : "",
                         (unsigned long)s->protect[k], (unsigned long)s->kind_patched[k], (unsigned long)s->kind_unpatched[k], (unsigned long)s->kind_unknown[k]);
    return used;
}

/* One new_fail_pages record. `when` is "armed" or "inject". The FILE's page
 * and the page of the call site 0x479d02 are named separately. */
static void nf_report_pages(const char *when) {
    if (!nf_image) return;
    nf_scan scan;
    const BYTE *pristine = nf_load_pristine();
    nf_scan_code(nf_image, pristine, &scan);
    char runs[512], listed[256], fields[1536];
    nf_runs_json(&scan, runs, sizeof(runs));
    int used = 0;
    listed[0] = 0;
    for (unsigned i = 0; i < scan.writable_count && (size_t)used + 16 < sizeof(listed); ++i)
        used += snprintf(listed + used, sizeof(listed) - (size_t)used, "%s\"0x%lx\"", i ? "," : "", (unsigned long)scan.writable_rva[i]);
    const BYTE *file_page = nf_image + (NF_FILE_RVA & ~0xfffu), *call_page = nf_image + (NF_CALL_RVA & ~0xfffu);
    int file_patched = pristine ? memcmp(file_page, pristine + (NF_FILE_RVA & ~0xfffu), 0x1000) != 0 : -1;
    int call_patched = pristine ? memcmp(call_page, pristine + (NF_CALL_RVA & ~0xfffu), 0x1000) != 0 : -1;
    snprintf(fields, sizeof(fields),
             "\"when\":\"%s\",\"image\":\"0x%lx\",\"pristine\":%s,\"code_sections\":%lu,\"pages\":%lu,\"patched\":%lu,\"unpatched\":%lu,\"unknown\":%lu,"
             "\"writable_patched\":%lu,\"writable_unpatched\":%lu,\"query_failed\":%lu,\"by_protect\":[%s],\"writable_pages\":[%s],"
             "\"file_page\":{\"rva\":\"0x%lx\",\"protect\":\"0x%lx\",\"patched\":%d},\"call_page\":{\"rva\":\"0x%lx\",\"protect\":\"0x%lx\",\"patched\":%d}",
             when, (unsigned long)(uintptr_t)nf_image, pristine ? "true" : "false", (unsigned long)scan.sections, (unsigned long)scan.pages,
             (unsigned long)scan.patched, (unsigned long)scan.unpatched, (unsigned long)scan.unknown, (unsigned long)scan.writable_patched,
             (unsigned long)scan.writable_unpatched, (unsigned long)scan.query_failed, runs, listed,
             (unsigned long)(NF_FILE_RVA & ~0xfffu), (unsigned long)nf_page_protect(file_page), file_patched,
             (unsigned long)(NF_CALL_RVA & ~0xfffu), (unsigned long)nf_page_protect(call_page), call_patched);
    nf_event("new_fail_pages", fields);
}

/* Called by the proxy once its own arming is complete (initialize_forwarding). */
static void nf_report_armed(void) {
    if (nf_state == NF_INSTALLED) nf_report_pages("armed");
}

/* ---- the record of what happens after the injection --------------------- */
/* The module that holds `address` (file name, sanitised for JSON; empty when
 * unknown) and its base (0 when unknown). */
static void nf_describe(uintptr_t address, char module[64], uintptr_t *base_out) {
    MEMORY_BASIC_INFORMATION where;
    uintptr_t base = 0;
    module[0] = 0;
    if (VirtualQuery((void *)address, &where, sizeof(where)) && where.State == MEM_COMMIT) {
        base = (uintptr_t)where.AllocationBase;
        WCHAR path[MAX_PATH];
        DWORD n = GetModuleFileNameW((HMODULE)base, path, MAX_PATH);
        const WCHAR *name = n && n < MAX_PATH ? wcsrchr(path, L'\\') : NULL;
        name = name ? name + 1 : path;
        unsigned i = 0;
        for (; n && n < MAX_PATH && i + 1 < 64 && name[i]; ++i)
            module[i] = name[i] < 0x7f && name[i] >= 0x20 && name[i] != '"' && name[i] != '\\' ? (char)name[i] : '?';
        module[i] = 0;
    }
    *base_out = base;
}

/* `size` bytes at `address` are committed, readable memory of one region. */
static BOOL nf_readable(uintptr_t address, size_t size) {
    MEMORY_BASIC_INFORMATION region;
    if (!address || !VirtualQuery((void *)address, &region, sizeof(region)) || region.State != MEM_COMMIT) return FALSE;
    if (region.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return FALSE;
    return address + size <= (uintptr_t)region.BaseAddress + region.RegionSize;
}

static BOOL nf_on_thread_stack(uintptr_t address) {
    const NT_TIB *tib = (const NT_TIB *)NtCurrentTeb();
    return address >= (uintptr_t)tib->StackLimit && address < (uintptr_t)tib->StackBase;
}

#define NF_SEH_MAX 8u
/* The chain of exception registration records that starts at `head` (the
 * thread's fs:[0]), as JSON array items: frame, handler, the handler's module
 * and offset, whether the frame lies on the thread's own stack. Every record
 * is checked for readable memory, 4-byte alignment and a higher address than
 * its predecessor; `*end` says why the walk stopped: "terminated" (the -1
 * link), "bad" (unreadable or misaligned), "backwards", or "truncated" (more
 * than NF_SEH_MAX). Memory only. Returns the number of items written. */
static unsigned nf_seh_walk(uintptr_t head, char *out, size_t size, const char **end) {
    unsigned count = 0;
    size_t used = 0;
    uintptr_t frame = head, previous = 0;
    out[0] = 0;
    *end = "terminated";
    while (frame != 0xffffffffu) {
        if (count == NF_SEH_MAX) { *end = "truncated"; break; }
        if ((frame & 3) || !nf_readable(frame, 8)) { *end = "bad"; break; }
        if (frame <= previous) { *end = "backwards"; break; }
        uint32_t next, handler;
        memcpy(&next, (void *)frame, 4);
        memcpy(&handler, (void *)(frame + 4), 4);
        char module[64];
        uintptr_t base;
        nf_describe(handler, module, &base);
        int n = snprintf(out + used, size - used, "%s{\"frame\":\"0x%lx\",\"handler\":\"0x%lx\",\"module\":\"%s\",\"offset\":\"0x%lx\",\"on_thread_stack\":%s}",
                         count ? "," : "", (unsigned long)frame, (unsigned long)handler, module, (unsigned long)(base ? handler - base : 0),
                         nf_on_thread_stack(frame) ? "true" : "false");
        if (n < 0 || (size_t)n >= size - used) { out[used] = 0; *end = "truncated"; break; }
        used += (size_t)n;
        ++count;
        previous = frame;
        frame = next;
    }
    return count;
}

static LONG CALLBACK nf_veh(EXCEPTION_POINTERS *x) {
    DWORD code = x->ExceptionRecord->ExceptionCode;
    if (!nf_injected || (code & 0xc0000000u) != 0xc0000000u || nf_exceptions >= (LONG)NF_EXCEPTION_MAX) return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement(&nf_exceptions);
    uintptr_t address = (uintptr_t)x->ExceptionRecord->ExceptionAddress;
    char module[64], fields[2304], chain[1536];
    uintptr_t base;
    nf_describe(address, module, &base);
    uintptr_t access = 0, target = 0;
    DWORD target_protect = 0;
    if (code == EXCEPTION_ACCESS_VIOLATION && x->ExceptionRecord->NumberParameters >= 2) {
        access = x->ExceptionRecord->ExceptionInformation[0];
        target = x->ExceptionRecord->ExceptionInformation[1];
        target_protect = nf_page_protect((void *)target);
    }
    const NT_TIB *tib = (const NT_TIB *)NtCurrentTeb();
    uintptr_t esp = x->ContextRecord->Esp;
    const char *end;
    unsigned seh = nf_seh_walk((uintptr_t)tib->ExceptionList, chain, sizeof(chain), &end);
    snprintf(fields, sizeof(fields),
             "\"code\":\"0x%08lx\",\"address\":\"0x%lx\",\"module\":\"%s\",\"module_base\":\"0x%lx\",\"offset\":\"0x%lx\","
             "\"access\":%lu,\"target\":\"0x%lx\",\"target_protect\":\"0x%lx\",\"esp\":\"0x%lx\",\"esp_on_thread_stack\":%s,"
             "\"seh_count\":%u,\"seh_end\":\"%s\",\"seh\":[%s]",
             (unsigned long)code, (unsigned long)address, module, (unsigned long)base, (unsigned long)(base ? address - base : 0),
             (unsigned long)access, (unsigned long)target, (unsigned long)target_protect, (unsigned long)esp,
             nf_on_thread_stack(esp) ? "true" : "false", seh, end, chain);
    nf_event("new_fail_exception", fields);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---- the exit path ------------------------------------------------------ */
/* `size` bytes at `rva` inside an image of `size` bytes. */
static BOOL nf_in_image(uint32_t image_size, uint32_t rva, uint32_t bytes) { return rva <= image_size && bytes <= image_size - rva; }

/* The NUL-terminated string at `rva` (at most `max` bytes), or NULL. */
static const char *nf_string_at(const BYTE *image, uint32_t image_size, uint32_t rva, uint32_t max) {
    if (!nf_in_image(image_size, rva, 1)) return NULL;
    for (uint32_t i = 0; i < max && rva + i < image_size; ++i) if (!image[rva + i]) return (const char *)image + rva;
    return NULL;
}

/* The import table of a PE32 image in memory layout (`image_size` bytes):
 * the RVA of the first-thunk slot that imports `function` by name from
 * `dll` (case-insensitive). Every RVA is bounds-checked; names are read from
 * the original-first-thunk table (the first thunk when that is zero, which
 * only finds anything while the loader has not yet filled it). */
static BOOL nf_find_import(const BYTE *image, uint32_t image_size, const char *dll, const char *function, uint32_t *slot_rva) {
    if (image_size < 0x1000) return FALSE;
    uint32_t pe = nf_u32(image + 0x3c);
    if (pe < 64 || pe > 4096 - 248 || nf_u32(image + pe) != 0x00004550u) return FALSE;
    const BYTE *nt = image + pe;
    unsigned optional = nf_u16(nt + 20);
    if (optional < 112 || pe + 24 + optional > 4096 || nf_u16(nt + 24) != 0x010bu || nf_u32(nt + 24 + 92) < 2) return FALSE;
    uint32_t table = nf_u32(nt + 24 + 96 + 8), table_size = nf_u32(nt + 24 + 96 + 12);
    if (!table || table_size < 20) return FALSE;
    for (uint32_t d = 0; d < 256; ++d) {
        if (!nf_in_image(image_size, table + 20u * d, 20)) return FALSE;
        const BYTE *descriptor = image + table + 20u * d;
        uint32_t names = nf_u32(descriptor), name = nf_u32(descriptor + 12), first = nf_u32(descriptor + 16);
        if (!name && !first) return FALSE;
        const char *module = nf_string_at(image, image_size, name, 260);
        if (!module || _stricmp(module, dll)) continue;
        if (!names) names = first;
        for (uint32_t t = 0; t < 8192; ++t) {
            if (!nf_in_image(image_size, names + 4u * t, 4) || !nf_in_image(image_size, first + 4u * t, 4)) break;
            uint32_t entry = nf_u32(image + names + 4u * t);
            if (!entry) break;
            if (entry & 0x80000000u) continue;                 /* by ordinal */
            const char *imported = nf_string_at(image, image_size, entry + 2, 260);
            if (imported && !strcmp(imported, function)) { *slot_rva = first + 4u * t; return TRUE; }
        }
    }
    return FALSE;
}

/* The patched import and what it points at; the stdcall thunk the slot gets. */
static LONG WINAPI nf_exit_probe(EXCEPTION_POINTERS *x);

/* Patches the slot that imports kernel32's UnhandledExceptionFilter in the
 * image at `module` (`image_size` bytes; `copy` is a readable copy of it in
 * memory layout, `module` itself when that is plain memory) with
 * nf_exit_probe, when the slot holds exactly `real` (the signature:
 * kernel32's own export). Returns NF_PROBE_ARMED or why nothing was
 * changed. Memory only. */
static LONG nf_arm_exit_probe_in(BYTE *module, const BYTE *copy, uint32_t image_size, uintptr_t real, uint32_t *slot_rva_out) {
    uint32_t rva = 0;
    if (!real || !module || !nf_find_import(copy, image_size, "kernel32.dll", "UnhandledExceptionFilter", &rva)) return NF_PROBE_NO_IMPORT;
    if (slot_rva_out) *slot_rva_out = rva;
    uint32_t *slot = (uint32_t *)(module + rva);
    if (!nf_readable((uintptr_t)slot, 4) || *slot != (uint32_t)real) return NF_PROBE_MISMATCH;
    DWORD protection, ignored;
    if (!VirtualProtect(slot, 4, PAGE_READWRITE, &protection)) return NF_PROBE_WRITE;
    nf_probe_real = *slot;
    nf_probe_slot = slot;
    *slot = (uint32_t)(uintptr_t)nf_exit_probe;
    VirtualProtect(slot, 4, protection, &ignored);
    nf_probe_state = NF_PROBE_ARMED;
    return NF_PROBE_ARMED;
}

/* Puts the original target back. Tests only: the process never disarms (at detach CW3220.DLL may be gone). */
__attribute__((unused)) static void nf_disarm_exit_probe(void) {
    if (nf_probe_state != NF_PROBE_ARMED || !nf_probe_slot) return;
    DWORD protection, ignored;
    if (VirtualProtect(nf_probe_slot, 4, PAGE_READWRITE, &protection)) {
        *nf_probe_slot = nf_probe_real;
        VirtualProtect(nf_probe_slot, 4, protection, &ignored);
    }
    nf_probe_state = NF_PROBE_OFF;
    nf_probe_slot = NULL;
}

static const char *nf_probe_text(LONG state) {
    switch (state) {
    case NF_PROBE_ARMED: return "armed";
    case NF_PROBE_NO_MODULE: return "no_module";
    case NF_PROBE_NO_IMPORT: return "no_import";
    case NF_PROBE_MISMATCH: return "slot_mismatch";
    case NF_PROBE_WRITE: return "write_refused";
    case NF_PROBE_BAD_IMAGE: return "bad_image";
    default: return "off";
    }
}

/* Arms the probe in `module` (any loaded module) and writes the
 * new_fail_exit_probe record. `real` is kernel32's UnhandledExceptionFilter. */
static LONG nf_arm_exit_probe_module(HMODULE module, uintptr_t real) {
    if (nf_probe_state != NF_PROBE_OFF) return nf_probe_state;
    uint32_t rva = 0, size = 0, stamp = 0, held = 0;
    char name[64] = "";
    uintptr_t base = (uintptr_t)module;
    LONG state;
    if (!module) state = NF_PROBE_NO_MODULE;
    else if (!nf_readable(base, 4096) || nf_u16((const BYTE *)module) != 0x5a4du) state = NF_PROBE_BAD_IMAGE;
    else {
        uint32_t pe = nf_u32((const BYTE *)module + 0x3c);
        if (pe < 64 || pe > 4096 - 248 || nf_u32((const BYTE *)module + pe) != 0x00004550u) state = NF_PROBE_BAD_IMAGE;
        else {
            size = nf_u32((const BYTE *)module + pe + 24 + 56);
            stamp = nf_u32((const BYTE *)module + pe + 8);
            BYTE *copy = size >= 0x1000 && size <= 0x4000000u ? calloc(1, size) : NULL;
            if (!copy) state = NF_PROBE_BAD_IMAGE;
            else {
                /* The sections are separate regions with their own protection: copy page by page, an
                 * unreadable page stays zero (ReadProcessMemory reports it instead of faulting). */
                for (uint32_t at = 0; at < size; at += 0x1000u) {
                    SIZE_T got;
                    ReadProcessMemory(GetCurrentProcess(), (const BYTE *)module + at, copy + at, size - at < 0x1000u ? size - at : 0x1000u, &got);
                }
                state = nf_arm_exit_probe_in((BYTE *)module, copy, size, real, &rva);
                if (rva) held = nf_u32(copy + rva);
                free(copy);
            }
        }
    }
    nf_probe_state = state;
    uintptr_t ignored;
    if (module) nf_describe(base, name, &ignored);
    char fields[512];
    snprintf(fields, sizeof(fields),
             "\"state\":\"%s\",\"module\":\"%s\",\"module_base\":\"0x%lx\",\"size_of_image\":\"0x%lx\",\"timestamp\":\"0x%lx\","
             "\"slot_rva\":\"0x%lx\",\"slot_was\":\"0x%lx\",\"kernel32_export\":\"0x%lx\"",
             nf_probe_text(state), name, (unsigned long)base, (unsigned long)size, (unsigned long)stamp, (unsigned long)rva,
             (unsigned long)held, (unsigned long)real);
    nf_event("new_fail_exit_probe", fields);
    return state;
}

/* At the injection: CW3220.DLL in this process (the original or the own
 * runtime, both deployed under that name). */
static LONG nf_arm_exit_probe(void) {
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC real = kernel32 ? GetProcAddress(kernel32, "UnhandledExceptionFilter") : NULL;
    return nf_arm_exit_probe_module(GetModuleHandleW(L"CW3220.DLL"), (uintptr_t)real);
}

#define NF_FILTER_MAX 4u
/* The import slot's new target. It records the call (the return address, so
 * the caller's module and offset; the exception record the caller passes;
 * the stack it runs on; the SEH chain at this moment), forwards to the real
 * filter with the same argument and records the answer. stdcall, one
 * argument: the real filter pops it. */
__attribute__((used, noinline)) static LONG WINAPI nf_exit_probe(EXCEPTION_POINTERS *x) {
    LONG call = InterlockedIncrement(&nf_filter_calls);
    LONG (WINAPI *real)(EXCEPTION_POINTERS *) = (LONG (WINAPI *)(EXCEPTION_POINTERS *))(uintptr_t)nf_probe_real;
    if ((unsigned)call <= NF_FILTER_MAX) {
        volatile char marker = 0;
        uintptr_t caller = (uintptr_t)__builtin_return_address(0), sp = (uintptr_t)&marker;
        char caller_module[64], fault_module[64], chain[1536], fields[2816];
        uintptr_t caller_base, fault_base;
        nf_describe(caller, caller_module, &caller_base);
        uintptr_t code = 0, address = 0, record = 0, context = 0, ctx_esp = 0;
        if (nf_readable((uintptr_t)x, 8)) {
            record = (uintptr_t)x->ExceptionRecord;
            context = (uintptr_t)x->ContextRecord;
            if (nf_readable(record, 16)) { code = x->ExceptionRecord->ExceptionCode; address = (uintptr_t)x->ExceptionRecord->ExceptionAddress; }
            if (nf_readable(context, sizeof(CONTEXT))) ctx_esp = x->ContextRecord->Esp;
        }
        nf_describe(address, fault_module, &fault_base);
        MEMORY_BASIC_INFORMATION region;
        uintptr_t region_base = 0, region_size = 0;
        if (VirtualQuery((void *)sp, &region, sizeof(region))) { region_base = (uintptr_t)region.AllocationBase; region_size = (uintptr_t)region.RegionSize; }
        const NT_TIB *tib = (const NT_TIB *)NtCurrentTeb();
        const char *end;
        unsigned seh = nf_seh_walk((uintptr_t)tib->ExceptionList, chain, sizeof(chain), &end);
        snprintf(fields, sizeof(fields),
                 "\"call\":%ld,\"return\":\"0x%lx\",\"caller_module\":\"%s\",\"caller_offset\":\"0x%lx\","
                 "\"pointers\":\"0x%lx\",\"code\":\"0x%08lx\",\"address\":\"0x%lx\",\"fault_module\":\"%s\",\"fault_offset\":\"0x%lx\","
                 "\"context_esp\":\"0x%lx\",\"context_esp_on_thread_stack\":%s,\"sp\":\"0x%lx\",\"sp_on_thread_stack\":%s,"
                 "\"stack_limit\":\"0x%lx\",\"stack_base\":\"0x%lx\",\"sp_region\":\"0x%lx\",\"sp_region_size\":\"0x%lx\","
                 "\"seh_count\":%u,\"seh_end\":\"%s\",\"seh\":[%s]",
                 (long)call, (unsigned long)caller, caller_module, (unsigned long)(caller_base ? caller - caller_base : 0),
                 (unsigned long)(uintptr_t)x, (unsigned long)code, (unsigned long)address, fault_module,
                 (unsigned long)(fault_base ? address - fault_base : 0), (unsigned long)ctx_esp, nf_on_thread_stack(ctx_esp) ? "true" : "false",
                 (unsigned long)sp, nf_on_thread_stack(sp) ? "true" : "false", (unsigned long)(uintptr_t)tib->StackLimit,
                 (unsigned long)(uintptr_t)tib->StackBase, (unsigned long)region_base, (unsigned long)region_size, seh, end, chain);
        nf_event("new_fail_exit_filter", fields);
    }
    LONG result = real(x);
    InterlockedIncrement(&nf_filter_returns);
    if ((unsigned)call <= NF_FILTER_MAX) {
        char fields[64];
        snprintf(fields, sizeof(fields), "\"call\":%ld,\"result\":%ld", (long)call, (long)result);
        nf_event("new_fail_exit_filter_result", fields);
    }
    return result;
}

/* ---- the request -------------------------------------------------------- */
static int nf_site_index(uint32_t caller_return) {
    for (unsigned i = 0; i < NF_SITE_COUNT; ++i)
        if (caller_return == (uint32_t)(uintptr_t)nf_image + nf_site_rvas[i] + 5u) return (int)i;
    return -1;
}

/* cdecl: 1 answers NULL, 0 lets the real `new` run. `size` is the request,
 * `caller_return` the return address of the registration's caller. */
__attribute__((used, noinline)) static uint32_t nf_decide(uint32_t size, uint32_t caller_return) {
    InterlockedIncrement(&nf_requests);
    int site = size == 8 ? nf_site_index(caller_return) : -1;
    if (site < 0) { InterlockedIncrement(&nf_other); return 0; }
    InterlockedIncrement(&nf_by_site[site]);
    if (nf_filter_rva && nf_filter_rva != nf_site_rvas[site]) return 0;
    LONG ordinal = InterlockedIncrement(&nf_counted);
    LONG ordinal_site = nf_by_site[site];
    BOOL inject = nf_nth && (uint32_t)ordinal == nf_nth && !InterlockedCompareExchange(&nf_injected, 1, 0);
    if (nf_logged < (LONG)NF_LOG_MAX) {
        InterlockedIncrement(&nf_logged);
        char fields[160];
        snprintf(fields, sizeof(fields), "\"site\":\"0x%lx\",\"ordinal\":%ld,\"ordinal_site\":%ld,\"inject\":%d",
                 (unsigned long)(NF_LINK_BASE + nf_site_rvas[site]), (long)ordinal, (long)ordinal_site, inject ? 1 : 0);
        nf_event("new_fail_request", fields);
    }
    if (inject) {
        nf_report_pages("inject");
        nf_veh_handle = AddVectoredExceptionHandler(0, nf_veh);   /* last: only records */
        nf_arm_exit_probe();                                      /* only now, never when nothing is injected */
    }
    return inject ? 1u : 0u;
}

/* The call's new target. Stack at entry: [esp] the return into 0x479d07,
 * [esp+4] the size, then the registration's saved EBX and EBP and, at
 * [esp+0x10], the return address into the registration's caller. */
__attribute__((naked)) static void nf_entry(void) {
    __asm__ volatile(
        "movl 0x10(%esp), %eax\n\t"
        "pushl %eax\n\t"
        "pushl 8(%esp)\n\t"
        "call _nf_decide\n\t"
        "addl $8, %esp\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "jmp *_nf_real_target\n"
        "1:\n\t"
        "xorl %eax, %eax\n\t"
        "ret\n\t");
}

/* ---- installation ------------------------------------------------------- */
/* The reference bytes. Operands that hold addresses are compared with
 * `base` + RVA (the address the image is mapped at). */
static BOOL nf_site_valid(const BYTE *image, uint32_t base) {
    const BYTE *n = image + NF_NEW_RVA, *r = image + NF_REG_RVA;
    static const BYTE new_head[10] = {0x55, 0x8b, 0xec, 0x53, 0x56, 0x8b, 0x5d, 0x08, 0x3b, 0x1d};
    static const BYTE reg_head[6] = {0x55, 0x8b, 0xec, 0x53, 0x6a, 0x08};
    static const BYTE reg_tail[7] = {0x59, 0x8b, 0xd8, 0x85, 0xc0, 0x75, 0x17};
    if (memcmp(n, new_head, sizeof(new_head)) || memcmp(r, reg_head, sizeof(reg_head)) || r[6] != 0xe8 ||
        (uint32_t)(NF_REG_RVA + 11u + nf_u32(r + 7)) != NF_NEW_RVA || memcmp(r + 11, reg_tail, sizeof(reg_tail)) ||
        r[18] != 0x68 || nf_u32(r + 19) != base + NF_FILE_RVA || r[23] != 0x68) return FALSE;
    for (unsigned i = 0; i < NF_SITE_COUNT; ++i) {
        const BYTE *c = image + nf_site_rvas[i];
        if (c[0] != 0xe8 || (uint32_t)(nf_site_rvas[i] + 5u + nf_u32(c + 1)) != NF_REG_RVA || c[5] != 0x59 || c[-5] != 0x68) return FALSE;
    }
    return TRUE;
}

/* Memory only. `image` must be a PE image based at expected_base. caller_va
 * is 0 for any of the six sites, else the link-base address of one;
 * log may be NULL. FALSE for a refusal (nf_state says why). */
static BOOL nf_install_image(BYTE *image, uintptr_t expected_base, uint32_t caller_va, uint32_t nth, const WCHAR *log) {
    if (nf_state != NF_OFF) return FALSE;
    uint32_t filter = 0;
    if (caller_va) {
        for (unsigned i = 0; i < NF_SITE_COUNT; ++i) if (caller_va == NF_LINK_BASE + nf_site_rvas[i]) filter = nf_site_rvas[i];
        if (!filter) { nf_state = NF_REFUSED_CONFIG; return FALSE; }
    }
    if (nth > 1000000u) { nf_state = NF_REFUSED_CONFIG; return FALSE; }
    if (!es2_hook_image_valid(image, expected_base, NF_IMAGE_SIZE)) { nf_state = NF_REFUSED_IMAGE; return FALSE; }
    if (!nf_site_valid(image, (uint32_t)(uintptr_t)image)) { nf_state = NF_REFUSED_SITE; return FALSE; }
    BYTE *site = image + NF_CALL_RVA;
    uint32_t rel = (uint32_t)((uintptr_t)nf_entry - ((uintptr_t)site + 5));
    DWORD protection, ignored;
    if (!VirtualProtect(site + 1, 4, PAGE_EXECUTE_READWRITE, &protection)) { nf_state = NF_REFUSED_WRITE; return FALSE; }
    nf_image = image;
    nf_site = site;
    nf_filter_rva = filter;
    nf_nth = nth;
    nf_log_path[0] = 0;
    if (log && wcslen(log) < MAX_PATH) wcscpy(nf_log_path, log);
    nf_real_target = (uint32_t)(uintptr_t)(image + NF_NEW_RVA);
    nf_original_rel = nf_u32(site + 1);
    memcpy(site + 1, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    VirtualProtect(site + 1, 4, protection, &ignored);
    nf_state = NF_INSTALLED;
    char fields[160];
    snprintf(fields, sizeof(fields), "\"caller\":\"%s\",\"n\":%lu,\"image\":\"0x%lx\"", filter ? "site" : "any", (unsigned long)nth, (unsigned long)(uintptr_t)image);
    if (filter) snprintf(fields, sizeof(fields), "\"caller\":\"0x%lx\",\"n\":%lu,\"image\":\"0x%lx\"", (unsigned long)(NF_LINK_BASE + filter), (unsigned long)nth, (unsigned long)(uintptr_t)image);
    nf_event("new_fail_installed", fields);
    return TRUE;
}

/* "<caller>:<n>" with caller = hexadecimal call-site address or `any`
 * (case-insensitive), n = decimal. FALSE when the text is malformed. */
static BOOL nf_parse(const WCHAR *text, uint32_t *caller_va, uint32_t *nth) {
    const WCHAR *colon = wcschr(text, L':');
    if (!colon || colon == text || !colon[1]) return FALSE;
    size_t head = (size_t)(colon - text);
    if (head == 3 && !_wcsnicmp(text, L"any", 3)) *caller_va = 0;
    else {
        const WCHAR *p = text;
        if (head > 2 && p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) p += 2;
        uint32_t v = 0;
        if (p == colon || colon - p > 8) return FALSE;
        for (; p < colon; ++p) {
            uint32_t d = *p >= L'0' && *p <= L'9' ? (uint32_t)(*p - L'0') : *p >= L'a' && *p <= L'f' ? (uint32_t)(*p - L'a' + 10) :
                *p >= L'A' && *p <= L'F' ? (uint32_t)(*p - L'A' + 10) : 16u;
            if (d > 15) return FALSE;
            v = v << 4 | d;
        }
        if (!v) return FALSE;
        *caller_va = v;
    }
    uint32_t n = 0;
    for (const WCHAR *p = colon + 1; *p; ++p) {
        if (*p < L'0' || *p > L'9' || n > 1000000u) return FALSE;
        n = n * 10 + (uint32_t)(*p - L'0');
    }
    if (n > 1000000u) return FALSE;
    *nth = n;
    return TRUE;
}

/* ES2_NEW_FAIL present: TRUE with the parsed settings (FALSE and `*malformed`
 * set for a bad value, FALSE for an unset variable). */
static BOOL nf_requested(uint32_t *caller_va, uint32_t *nth, BOOL *malformed) {
    WCHAR value[64];
    *malformed = FALSE;
    DWORD n = GetEnvironmentVariableW(L"ES2_NEW_FAIL", value, 64);
    if (!n) return FALSE;
    if (n >= 64 || !nf_parse(value, caller_va, nth)) { *malformed = TRUE; return FALSE; }
    return TRUE;
}

static void nf_shutdown(void) {
    if (nf_state != NF_INSTALLED) return;
    char fields[448];
    snprintf(fields, sizeof(fields), "\"requests\":%ld,\"other\":%ld,\"counted\":%ld,\"by_site\":[%ld,%ld,%ld,%ld,%ld,%ld],\"injected\":%ld,\"exceptions\":%ld,"
             "\"exit_probe\":\"%s\",\"exit_filter_calls\":%ld,\"exit_filter_returns\":%ld",
             (long)nf_requests, (long)nf_other, (long)nf_counted, (long)nf_by_site[0], (long)nf_by_site[1], (long)nf_by_site[2],
             (long)nf_by_site[3], (long)nf_by_site[4], (long)nf_by_site[5], (long)nf_injected, (long)nf_exceptions,
             nf_probe_text(nf_probe_state), (long)nf_filter_calls, (long)nf_filter_returns);
    nf_event("new_fail_summary", fields);
}

/* ---- the log path's {suite} ---------------------------------------------- */
/* The suite the clone is configured for: the stem of [trace] tick_log in the
 * INI (the runner writes `<...>\<suite>\run\<suite>.jsonl`), reduced to
 * letters, digits, `-` and `_` (anything else becomes `_`, at most 64
 * characters); "unknown" when the INI or the key is missing. */
static void nf_suite_name(const WCHAR *ini, WCHAR *out, size_t size) {
    WCHAR path[MAX_PATH];
    path[0] = 0;
    if (ini) GetPrivateProfileStringW(L"trace", L"tick_log", L"", path, MAX_PATH, ini);
    const WCHAR *name = path, *slash = wcsrchr(path, L'\\'), *forward = wcsrchr(path, L'/');
    if (forward && (!slash || forward > slash)) slash = forward;
    if (slash) name = slash + 1;
    const WCHAR *dot = wcsrchr(name, L'.');
    size_t length = dot ? (size_t)(dot - name) : wcslen(name), n = 0;
    for (size_t i = 0; i < length && n < 64 && n + 1 < size; ++i)
        out[n++] = (name[i] >= L'0' && name[i] <= L'9') || (name[i] >= L'a' && name[i] <= L'z') || (name[i] >= L'A' && name[i] <= L'Z') ||
                   name[i] == L'-' || name[i] == L'_' ? name[i] : L'_';
    out[n] = 0;
    if (!n) { wcsncpy(out, L"unknown", size - 1); out[size - 1] = 0; }
}

/* `text` with every `{suite}` replaced by `suite`; FALSE when it does not fit. */
static BOOL nf_expand_suite(const WCHAR *text, const WCHAR *suite, WCHAR *out, size_t size) {
    size_t n = 0, suite_length = wcslen(suite);
    for (const WCHAR *p = text; *p;) {
        if (!wcsncmp(p, L"{suite}", 7)) {
            if (n + suite_length >= size) return FALSE;
            memcpy(out + n, suite, suite_length * sizeof(WCHAR));
            n += suite_length;
            p += 7;
        } else {
            if (n + 1 >= size) return FALSE;
            out[n++] = *p++;
        }
    }
    out[n] = 0;
    return TRUE;
}

#ifndef ES2_NF_NO_PROCESS_ENTRY
/* The INI beside the proxy DLL (media_proxy.c reads the same file). */
static BOOL nf_proxy_ini(WCHAR *ini) {
    HMODULE self;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(void *)nf_proxy_ini, &self)) return FALSE;
    DWORD n = GetModuleFileNameW(self, ini, MAX_PATH);
    WCHAR *slash = n && n < MAX_PATH ? wcsrchr(ini, L'\\') : NULL;
    if (!slash || (size_t)(slash - ini) + 1 + wcslen(L"es2-oracle.ini") >= MAX_PATH) return FALSE;
    wcscpy(slash + 1, L"es2-oracle.ini");
    return TRUE;
}

/* DllMain (media_proxy.c): DBSIM.EXE only. */
static void install_new_fail_switch(void) {
    uint32_t caller, nth;
    BOOL malformed;
    if (!nf_requested(&caller, &nth, &malformed)) { if (malformed) nf_state = NF_REFUSED_CONFIG; return; }
    WCHAR exe[MAX_PATH], log[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, exe, MAX_PATH);
    WCHAR *name = length && length < MAX_PATH ? wcsrchr(exe, L'\\') : NULL;
    if (!name || _wcsicmp(name + 1, L"DBSIM.EXE")) return;
    DWORD l = GetEnvironmentVariableW(L"ES2_NEW_FAIL_LOG", log, MAX_PATH);
    if (l && l < MAX_PATH && wcsstr(log, L"{suite}")) {          /* one file per suite when several clones share the variable */
        WCHAR ini[MAX_PATH], suite[65], expanded[MAX_PATH];
        suite[0] = 0;
        nf_suite_name(nf_proxy_ini(ini) ? ini : NULL, suite, 65);
        if (nf_expand_suite(log, suite, expanded, MAX_PATH)) wcscpy(log, expanded);
        else l = 0;
    }
    nf_install_image((BYTE *)GetModuleHandleW(NULL), NF_LINK_BASE, caller, nth, l && l < MAX_PATH ? log : NULL);
}
#endif
#endif
