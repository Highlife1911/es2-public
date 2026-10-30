#include "hook/hook.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <float.h>

_Static_assert(sizeof(void *) == 4, "The detour bridge requires Windows x86");

#define CODE_BYTES 128u
#define BRIDGE_OFFSET 64u

_Static_assert(offsetof(es2_hook_call, result) == 20, "machine call layout");
_Static_assert(offsetof(es2_hook_call, entry_registers) == 24, "machine entry register layout");
_Static_assert(offsetof(es2_hook_result, fpu) == 8, "machine result layout");
_Static_assert(LDBL_MANT_DIG == 64, "extended callbacks require x87 long double");
extern void es2_hook_entry_extended(void);

/* The simulation thread is the only supported caller. An original phase
 * bypasses every nested configured hook, even one in REPLACED mode. */
static unsigned original_phase_depth;
static unsigned dispatch_depth;

static uint16_t read16(const BYTE *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read32(const BYTE *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write32(BYTE *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (BYTE)(value >> (i * 8));
}

static BOOL readable(const void *pointer, size_t size) {
    uintptr_t cursor = (uintptr_t)pointer;
    if (!cursor || size > UINTPTR_MAX - cursor) return FALSE;
    uintptr_t end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery((const void *)cursor, &region, sizeof(region)) ||
            region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return FALSE;
        DWORD access = region.Protect & 0xffu;
        if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
            access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY)
            return FALSE;
        uintptr_t next = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (next <= cursor) return FALSE;
        cursor = next;
    }
    return TRUE;
}

static BOOL name_valid(const char *name) {
    if (!name || !*name) return FALSE;
    for (size_t i = 0; i <= ES2_HOOK_MAX_NAME; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!c) return TRUE;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) return FALSE;
    }
    return FALSE;
}

BOOL es2_hook_image_valid(const BYTE *image, uintptr_t expected_base, size_t expected_size) {
    if ((uintptr_t)image != expected_base || expected_size < 4096 ||
        expected_size > UINT32_MAX || expected_size > UINTPTR_MAX - expected_base ||
        !readable(image, 4096) || read16(image) != 0x5a4du) return FALSE;
    uint32_t pe = read32(image + 0x3c);
    if (pe < 64 || pe > 4096 - 248) return FALSE;
    const BYTE *nt = image + pe;
    return read32(nt) == 0x00004550u && read16(nt + 4) == 0x014cu &&
        read16(nt + 24) == 0x010bu && read32(nt + 24 + 28) == expected_base &&
        read32(nt + 24 + 56) == expected_size;
}

/* Decode only an intentionally small set of position-independent x86
 * instructions. This reads ModRM/SIB/displacements and immediate widths;
 * bytes resembling opcodes inside an operand are never treated as opcodes. */
static size_t modrm_size(const BYTE *p, size_t available) {
    if (!available) return 0;
    unsigned mod = p[0] >> 6, rm = p[0] & 7u;
    size_t size = 1;
    if (mod == 3) return size;
    if (rm == 4) {
        if (available < 2) return 0;
        ++size;
        if (mod == 0 && (p[1] & 7u) == 5) size += 4;
    } else if (mod == 0 && rm == 5) {
        size += 4;
    }
    if (mod == 1) ++size;
    if (mod == 2) size += 4;
    return size <= available ? size : 0;
}

static size_t instruction_size(const BYTE *p, size_t available) {
    if (!available) return 0;
    BYTE op = p[0];
    if ((op >= 0x50 && op <= 0x5f) || op == 0x90) return 1;
    if ((op >= 0xb8 && op <= 0xbf) || op == 0x68) return available >= 5 ? 5 : 0;
    if (op == 0x6a) return available >= 2 ? 2 : 0;
    if (op == 0xc8) return available >= 4 ? 4 : 0; /* ENTER imm16, imm8 */
    /* Operand-size prefix: only 16-bit MOV r/m without immediate, whose
     * ModRM length the prefix does not change. */
    if (op == 0x66) {
        if (available < 2 || (p[1] != 0x89 && p[1] != 0x8b)) return 0;
        size_t inner = instruction_size(p + 1, available - 1);
        return inner ? inner + 1 : 0;
    }
    size_t prefix = 1, immediate = 0;
    if (op == 0x0f) {
        if (available < 2 || (p[1] != 0xb6 && p[1] != 0xb7 && p[1] != 0xbe && p[1] != 0xbf)) return 0;
        prefix = 2;
    } else if (op == 0x81 || op == 0x83 || op == 0xc6 || op == 0xc7) {
        if (available < 2) return 0;
        unsigned group = (p[1] >> 3) & 7u;
        if ((op == 0xc6 || op == 0xc7) ? group != 0 : (group != 0 && group != 5 && group != 7)) return 0;
        immediate = (op == 0x81 || op == 0xc7) ? 4 : 1;
    } else if (op != 0x88 && op != 0x89 && op != 0x8a && op != 0x8b && op != 0x8d &&
               op != 0x01 && op != 0x03 && op != 0x29 && op != 0x2b &&
               op != 0x31 && op != 0x33 && op != 0x85) {
        return 0;
    }
    if (prefix >= available) return 0;
    if (op == 0x8d && (p[1] >> 6) == 3) return 0;
    size_t operand = modrm_size(p + prefix, available - prefix);
    if (!operand || immediate > available - prefix - operand) return 0;
    return prefix + operand + immediate;
}

BOOL es2_hook_prologue_valid(const BYTE *bytes, size_t size, const BYTE *lengths, size_t count) {
    if (!bytes || !lengths || size < 5 || size > ES2_HOOK_MAX_PROLOGUE || !count || count > size) return FALSE;
    size_t position = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!lengths[i] || lengths[i] > size - position) return FALSE;
        size_t decoded = instruction_size(bytes + position, size - position);
        if (!decoded || decoded != lengths[i]) return FALSE;
        position += decoded;
    }
    return position == size;
}

static BOOL descriptor_valid(const es2_hook_descriptor *d) {
    if (!d || !name_valid(d->name) || !d->expected || !d->replacement ||
        d->arg_count > ES2_HOOK_MAX_ARGS || d->span_count > ES2_HOOK_MAX_SPANS ||
        (d->span_count && !d->spans) || d->expected_size < d->relocate_size ||
        d->expected_size > ES2_HOOK_MAX_SIGNATURE) return FALSE;
    size_t total = 0;
    for (unsigned i = 0; i < d->span_count; ++i) {
        const es2_hook_span *s = d->spans + i;
        if (s->arg_index >= d->arg_count || !s->size || s->offset > ES2_HOOK_MAX_STATE_BYTES ||
            s->size > ES2_HOOK_MAX_STATE_BYTES - s->offset) return FALSE;
        size_t extent = s->offset + s->size;
        if (extent > ES2_HOOK_MAX_STATE_BYTES - total) return FALSE;
        total += extent;
        for (unsigned j = 0; j < i; ++j) if (d->spans[j].arg_index == s->arg_index) return FALSE;
    }
    return TRUE;
}

static uint32_t call_original(es2_hook *hook, const uint32_t args[ES2_HOOK_MAX_ARGS]) {
    typedef uint32_t (__cdecl *f0)(void);
    typedef uint32_t (__cdecl *f1)(uint32_t);
    typedef uint32_t (__cdecl *f2)(uint32_t, uint32_t);
    typedef uint32_t (__cdecl *f3)(uint32_t, uint32_t, uint32_t);
    typedef uint32_t (__cdecl *f4)(uint32_t, uint32_t, uint32_t, uint32_t);
    typedef uint32_t (__cdecl *f5)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    typedef uint32_t (__cdecl *f6)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    switch (hook->descriptor->arg_count) {
    case 0: return ((f0)hook->trampoline)();
    case 1: return ((f1)hook->trampoline)(args[0]);
    case 2: return ((f2)hook->trampoline)(args[0], args[1]);
    case 3: return ((f3)hook->trampoline)(args[0], args[1], args[2]);
    case 4: return ((f4)hook->trampoline)(args[0], args[1], args[2], args[3]);
    case 5: return ((f5)hook->trampoline)(args[0], args[1], args[2], args[3], args[4]);
    default: return ((f6)hook->trampoline)(args[0], args[1], args[2], args[3], args[4], args[5]);
    }
}

static BOOL snapshot(const es2_hook_descriptor *d, const uint32_t args[ES2_HOOK_MAX_ARGS],
                     uint32_t private_args[ES2_HOOK_MAX_ARGS], BYTE *storage, es2_hook_comparison *views) {
    uintptr_t starts[ES2_HOOK_MAX_SPANS] = {0}, ends[ES2_HOOK_MAX_SPANS] = {0};
    size_t cursor = 0;
    memcpy(private_args, args, sizeof(uint32_t) * ES2_HOOK_MAX_ARGS);
    memset(views, 0, sizeof(*views));
    views->span_count = d->span_count;
    for (unsigned i = 0; i < d->span_count; ++i) {
        const es2_hook_span *s = d->spans + i;
        size_t extent = s->offset + s->size;
        uintptr_t base = args[s->arg_index];
        if (!readable((const void *)base, extent)) return FALSE;
        starts[i] = base;
        ends[i] = base + extent;
        for (unsigned j = 0; j < i; ++j)
            if (starts[i] < ends[j] && starts[j] < ends[i]) return FALSE;
        /* Preserve each argument's alignment modulo 16. The backing buffer
         * reserves up to 15 padding bytes per span beyond its byte limit. */
        cursor += (base - (uintptr_t)(storage + cursor)) & 15u;
        private_args[s->arg_index] = (uint32_t)(uintptr_t)(storage + cursor);
        memcpy(storage + cursor, (const void *)base, extent);
        views->original[i] = (const BYTE *)(base + s->offset);
        views->replacement[i] = storage + cursor + s->offset;
        views->sizes[i] = s->size;
        cursor += extent;
    }
    /* A second argument aliasing a span would otherwise still expose real
     * state to the replacement. Reject it unless it has its own disjoint span. */
    for (unsigned a = 0; a < d->arg_count; ++a) {
        BOOL mapped = FALSE;
        for (unsigned i = 0; i < d->span_count; ++i) if (d->spans[i].arg_index == a) mapped = TRUE;
        if (!mapped) for (unsigned i = 0; i < d->span_count; ++i)
            if ((uintptr_t)args[a] >= starts[i] && (uintptr_t)args[a] < ends[i]) return FALSE;
    }
    return TRUE;
}

static BOOL compare_bytes(const es2_hook_comparison *v, unsigned *span, size_t *offset) {
    *span = UINT32_MAX;
    *offset = SIZE_MAX;
    for (unsigned i = 0; i < v->span_count; ++i) {
        for (size_t j = 0; j < v->sizes[i]; ++j) {
            if (v->original[i][j] != v->replacement[i][j]) {
                *span = i;
                *offset = j;
                return FALSE;
            }
        }
    }
    return TRUE;
}

static BOOL views_readable(const es2_hook_comparison *v) {
    for (unsigned i = 0; i < v->span_count; ++i)
        if (!readable(v->original[i], v->sizes[i])) return FALSE;
    return TRUE;
}

static void log_mismatch(es2_hook *hook, uint64_t call, const uint32_t *args,
                         const es2_hook_comparison *v, unsigned span, size_t offset) {
    es2_hook_log *log = hook->options.log;
    if (!log || log->file == INVALID_HANDLE_VALUE || !log->file || log->attempted >= log->limit) return;
    ++log->attempted;
    char line[1024], frame[32] = "null", span_text[24] = "null", offset_text[24] = "null";
    uint32_t frame_value;
    if (log->frame && log->frame(log->user, &frame_value)) snprintf(frame, sizeof(frame), "%lu", (unsigned long)frame_value);
    if (span != UINT32_MAX) {
        snprintf(span_text, sizeof(span_text), "%u", span);
        snprintf(offset_text, sizeof(offset_text), "%lu", (unsigned long)offset);
    }
    int n = snprintf(line, sizeof(line),
        "{\"event\":\"hook_mismatch\",\"hook\":\"%s\",\"call\":%llu,\"frame\":%s,\"args\":[",
        hook->descriptor->name, (unsigned long long)call, frame);
    for (unsigned i = 0; i < hook->descriptor->arg_count; ++i)
        n += snprintf(line + n, sizeof(line) - (size_t)n, "%s%lu", i ? "," : "", (unsigned long)args[i]);
    n += snprintf(line + n, sizeof(line) - (size_t)n,
        "],\"original\":%lu,\"replacement\":%lu,\"first_diff_span\":%s,\"first_diff_offset\":%s}\r\n",
        (unsigned long)v->original_result, (unsigned long)v->replacement_result, span_text, offset_text);
    DWORD written = 0;
    if (n <= 0 || n >= (int)sizeof(line) || !WriteFile(log->file, line, (DWORD)n, &written, NULL) || written != (DWORD)n)
        ++log->failed_writes;
    else ++log->written;
}

static uint32_t __attribute__((force_align_arg_pointer)) __cdecl dispatch(es2_hook *hook, const uint32_t *entry_stack) {
    uint32_t args[ES2_HOOK_MAX_ARGS] = {0};
    for (unsigned i = 0; i < hook->descriptor->arg_count; ++i) args[i] = entry_stack[i + 1];
    if (GetCurrentThreadId() != hook->owner_thread) return call_original(hook, args);
    ++hook->in_flight;
    uint64_t call = ++hook->calls;
    uint32_t result;
    if (hook->installed != 1 || original_phase_depth) {
        ++hook->bypassed;
        result = call_original(hook, args);
    } else if (dispatch_depth >= ES2_HOOK_MAX_DEPTH) {
        ++hook->skipped;
        ++original_phase_depth;
        result = call_original(hook, args);
        --original_phase_depth;
    } else {
        ++dispatch_depth;
        if (hook->options.mode == ES2_HOOK_REPLACED) {
            result = hook->descriptor->replacement(args, hook->descriptor->user);
        } else {
            _Alignas(16) BYTE state[ES2_HOOK_MAX_STATE_BYTES + ES2_HOOK_MAX_SPANS * 15u];
            uint32_t private_args[ES2_HOOK_MAX_ARGS];
            es2_hook_comparison values;
            BOOL have_snapshot = snapshot(hook->descriptor, args, private_args, state, &values);
            ++original_phase_depth;
            result = call_original(hook, args);
            --original_phase_depth;
            if (!have_snapshot || !views_readable(&values)) {
                ++hook->skipped;
            } else {
                values.original_result = result;
                values.replacement_result = hook->descriptor->replacement(private_args, hook->descriptor->user);
                unsigned span;
                size_t offset;
                BOOL same_bytes = compare_bytes(&values, &span, &offset);
                BOOL same = hook->descriptor->compare ? hook->descriptor->compare(&values, hook->descriptor->user) :
                    (same_bytes && values.original_result == values.replacement_result);
                if (!same) {
                    ++hook->mismatches;
                    log_mismatch(hook, call, args, &values, span, offset);
                }
            }
        }
        --dispatch_depth;
    }
    --hook->in_flight;
    return result;
}

static BOOL patch_protect(es2_hook *hook, DWORD protection, DWORD *previous) {
    return hook->options.patch_api.protect ?
        hook->options.patch_api.protect(hook->site, hook->descriptor->relocate_size, protection, previous) :
        VirtualProtect(hook->site, hook->descriptor->relocate_size, protection, previous);
}

static BOOL patch_flush(es2_hook *hook) {
    return hook->options.patch_api.flush ?
        hook->options.patch_api.flush(GetCurrentProcess(), hook->site, hook->descriptor->relocate_size) :
        FlushInstructionCache(GetCurrentProcess(), hook->site, hook->descriptor->relocate_size);
}

static void release_code(es2_hook *hook) {
    if (hook->code) VirtualFree(hook->code, 0, MEM_RELEASE);
    hook->code = NULL;
    hook->trampoline = NULL;
}

static BOOL prepare_code(es2_hook *hook) {
    size_t n = hook->descriptor->relocate_size;
    hook->code = VirtualAlloc(NULL, CODE_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!hook->code) { hook->error = ES2_HOOK_ERROR_MEMORY; return FALSE; }
    hook->trampoline = hook->code;
    memcpy(hook->code, hook->original, n);
    hook->code[n] = 0xe9;
    write32(hook->code + n + 1, (uint32_t)(uintptr_t)(hook->site + n) - (uint32_t)(uintptr_t)(hook->code + n + 5));
    BYTE *b = hook->code + BRIDGE_OFFSET;
    /* mov eax,esp; push eax; push context; mov eax,dispatcher; call eax;
     * add esp,8; ret. The dispatcher preserves cdecl callee-saved registers. */
    if (hook->extension) {
        /* push context; call preserving bridge; discard context; ret N. */
        b[0] = 0x68; write32(b + 1, (uint32_t)(uintptr_t)hook);
        b[5] = 0xe8; write32(b + 6, (uint32_t)(uintptr_t)es2_hook_entry_extended - (uint32_t)(uintptr_t)(b + 10));
        b[10] = 0x83; b[11] = 0xc4; b[12] = 4;
        unsigned slots = hook->descriptor->arg_count;
        if (hook->extension->abi == ES2_ABI_BORLAND) slots = slots > 3 ? slots - 3 : 0;
        if (hook->extension->abi == ES2_ABI_CDECL) slots = 0;
        b[13] = 0xc2; b[14] = (BYTE)(slots * 4); b[15] = 0;
    } else {
    b[0] = 0x89; b[1] = 0xe0; b[2] = 0x50; b[3] = 0x68;
    write32(b + 4, (uint32_t)(uintptr_t)hook);
    b[8] = 0xb8; write32(b + 9, (uint32_t)(uintptr_t)dispatch);
    b[13] = 0xff; b[14] = 0xd0; b[15] = 0x83; b[16] = 0xc4; b[17] = 0x08; b[18] = 0xc3;
    }
    memset(hook->patch, 0x90, n);
    hook->patch[0] = 0xe9;
    write32(hook->patch + 1, (uint32_t)(uintptr_t)b - (uint32_t)(uintptr_t)(hook->site + 5));
    DWORD previous;
    if (!VirtualProtect(hook->code, CODE_BYTES, PAGE_EXECUTE_READ, &previous)) {
        hook->error = ES2_HOOK_ERROR_PROTECT;
        release_code(hook);
        return FALSE;
    }
    if (!FlushInstructionCache(GetCurrentProcess(), hook->code, CODE_BYTES)) {
        hook->error = ES2_HOOK_ERROR_FLUSH;
        release_code(hook);
        return FALSE;
    }
    return TRUE;
}

static BOOL extension_valid(const es2_hook_descriptor *, const es2_hook_options *, const es2_hook_extension *);

BOOL es2_hook_install_ex(es2_hook *hook, const es2_hook_descriptor *d, const es2_hook_options *options,
                         const es2_hook_extension *extension) {
    if (!hook) return FALSE;
    if (hook->installed || hook->code || hook->in_flight) { hook->error = ES2_HOOK_ERROR_BUSY; return FALSE; }
    if (!d || !options || !name_valid(d->name) || options->mode < ES2_HOOK_ORIGINAL || options->mode > ES2_HOOK_COMPARE) {
        hook->error = ES2_HOOK_ERROR_ARGUMENT; return FALSE;
    }
    memset(hook, 0, sizeof(*hook));
    hook->descriptor = d;
    hook->options = *options;
    hook->extension = extension;
    hook->owner_thread = GetCurrentThreadId();
    if (options->mode == ES2_HOOK_ORIGINAL) return TRUE;
    if (!descriptor_valid(d)) { hook->error = ES2_HOOK_ERROR_ARGUMENT; return FALSE; }
    if (extension && !extension_valid(d, options, extension)) { hook->error = ES2_HOOK_ERROR_ARGUMENT; return FALSE; }
    if (!es2_hook_prologue_valid(d->expected, d->relocate_size, d->instruction_lengths, d->instruction_count)) {
        hook->error = ES2_HOOK_ERROR_INSTRUCTION; return FALSE;
    }
    if (!es2_hook_image_valid(options->image, options->expected_base, options->expected_image_size) ||
        d->rva > options->expected_image_size || d->expected_size > options->expected_image_size - d->rva ||
        !readable(options->image + d->rva, d->expected_size)) {
        hook->error = ES2_HOOK_ERROR_IMAGE; return FALSE;
    }
    hook->site = options->image + d->rva;
    if (memcmp(hook->site, d->expected, d->expected_size)) { hook->error = ES2_HOOK_ERROR_SIGNATURE; return FALSE; }
    /* One protection value must cover the whole patch. Refuse a region crossing
     * rather than accidentally changing another page's original protection. */
    MEMORY_BASIC_INFORMATION region;
    if (!VirtualQuery(hook->site, &region, sizeof(region)) || d->relocate_size >
        (uintptr_t)region.BaseAddress + region.RegionSize - (uintptr_t)hook->site) {
        hook->error = ES2_HOOK_ERROR_PROTECT; return FALSE;
    }
    memcpy(hook->original, hook->site, d->relocate_size);
    if (!prepare_code(hook)) return FALSE;
    if (!patch_protect(hook, PAGE_EXECUTE_READWRITE, &hook->original_protection)) {
        hook->error = ES2_HOOK_ERROR_PROTECT;
        release_code(hook);
        return FALSE;
    }
    InterlockedExchange(&hook->installed, 1);
    memcpy(hook->site, hook->patch, d->relocate_size);
    DWORD ignored;
    BOOL flushed = patch_flush(hook);
    BOOL protected = flushed && patch_protect(hook, hook->original_protection, &ignored);
    if (flushed && protected) return TRUE;
    hook->error = flushed ? ES2_HOOK_ERROR_PROTECT : ES2_HOOK_ERROR_FLUSH;
    /* Failed restoration leaves the site writable by the callback contract.
     * The live context remains published until original bytes AND cache are
     * restored. On persistent failure retain the trampoline for stale jumps. */
    InterlockedExchange(&hook->installed, 2);
    memcpy(hook->site, hook->original, d->relocate_size);
    BOOL restored_cache = patch_flush(hook);
    BOOL restored_protection = patch_protect(hook, hook->original_protection, &ignored);
    if (!restored_cache || !restored_protection) {
        hook->error = ES2_HOOK_ERROR_ROLLBACK;
        return FALSE;
    }
    InterlockedExchange(&hook->installed, 0);
    release_code(hook);
    return FALSE;
}

BOOL es2_hook_install(es2_hook *hook, const es2_hook_descriptor *d, const es2_hook_options *options) {
    return es2_hook_install_ex(hook, d, options, NULL);
}

static BOOL extension_valid(const es2_hook_descriptor *d, const es2_hook_options *o, const es2_hook_extension *e) {
    if ((unsigned)e->abi > ES2_ABI_BORLAND || (unsigned)e->result_kind > ES2_RESULT_ST0 ||
        e->region_count > ES2_HOOK_MAX_SPANS - d->span_count ||
        (e->region_count && !e->regions) ||
        (e->result_kind == ES2_RESULT_PAIR && !e->replacement64 && !e->replacement_context) ||
        (e->replacement_context && e->result_kind != ES2_RESULT_PAIR) ||
        (e->result_kind == ES2_RESULT_ST0 && !e->replacement80) ||
        (e->compare_unsupported && o->mode == ES2_HOOK_COMPARE)) return FALSE;
    size_t total = 0;
    for (unsigned i = 0; i < d->span_count; ++i) total += d->spans[i].offset + d->spans[i].size;
    for (unsigned i = 0; i < e->region_count; ++i) {
        const es2_hook_region *r = e->regions + i;
        if ((unsigned)r->source > ES2_REGION_RVA_INDIRECT || !r->size ||
            r->pointer_hops > ES2_HOOK_MAX_POINTER_HOPS ||
            (r->source == ES2_REGION_RVA && r->pointer_hops) ||
            (r->pointer_hops > 1 && !r->read_only) ||
            r->size > ES2_HOOK_MAX_STATE_BYTES - total ||
            r->offset > ES2_HOOK_MAX_STATE_BYTES || r->pointer_offset > ES2_HOOK_MAX_STATE_BYTES ||
            (r->source == ES2_REGION_ARG_INDIRECT && r->arg_index >= d->arg_count)) return FALSE;
        for (unsigned hop = 1; hop < r->pointer_hops; ++hop)
            if (r->next_pointer_offsets[hop - 1] > ES2_HOOK_MAX_STATE_BYTES) return FALSE;
        total += r->size;
        if (r->source != ES2_REGION_ARG_INDIRECT) {
            size_t extent = r->source == ES2_REGION_RVA ? r->offset + r->size : r->pointer_offset + 4;
            if (r->rva > o->expected_image_size || extent > o->expected_image_size - r->rva) return FALSE;
        }
    }
    return TRUE;
}

typedef struct extended_frame {
    BYTE fpu[108];
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax, flags, stub_return;
    es2_hook *hook;
    uint32_t caller_return;
} extended_frame;
_Static_assert(sizeof(extended_frame) == 156 && offsetof(extended_frame, hook) == 148, "entry layout");

static BOOL writable(const void *p, size_t size) {
    uintptr_t cursor = (uintptr_t)p, end = cursor + size;
    if (!cursor || end < cursor) return FALSE;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery((const void *)cursor, &region, sizeof(region)) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return FALSE;
        DWORD access = region.Protect & 0xff;
        if (access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
            access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) return FALSE;
        uintptr_t next = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (next <= cursor) return FALSE;
        cursor = next;
    }
    return TRUE;
}

typedef struct region_snapshot {
    BYTE *address;
    const BYTE *pointer_slots[ES2_HOOK_MAX_POINTER_HOPS];
    uint32_t pointer_values[ES2_HOOK_MAX_POINTER_HOPS];
    unsigned pointer_count;
    size_t buffer_offset;
} region_snapshot;

static BOOL resolve_regions(es2_hook *h, const uint32_t *args, region_snapshot *regions, BYTE *before) {
    size_t cursor = 0;
    const es2_hook_extension *e = h->extension;
    for (unsigned i = 0; i < e->region_count; ++i) {
        const es2_hook_region *r = &e->regions[i];
        uintptr_t base = r->source == ES2_REGION_ARG_INDIRECT ? args[r->arg_index] :
            (uintptr_t)h->options.image + r->rva;
        regions[i].pointer_count = 0;
        if (r->source != ES2_REGION_RVA) {
            unsigned hops = r->pointer_hops ? r->pointer_hops : 1;
            for (unsigned hop = 0; hop < hops; ++hop) {
                size_t offset = hop ? r->next_pointer_offsets[hop - 1] : r->pointer_offset;
                if (!base || offset > UINTPTR_MAX - base || !readable((void *)(base + offset), 4)) return FALSE;
                regions[i].pointer_slots[hop] = (const BYTE *)(base + offset);
                base = regions[i].pointer_values[hop] = read32(regions[i].pointer_slots[hop]);
                if (!base) return FALSE;
                ++regions[i].pointer_count;
            }
        }
        if (r->offset > UINTPTR_MAX - base || r->size > UINTPTR_MAX - base - r->offset) return FALSE;
        BYTE *address = (BYTE *)(base + r->offset);
        /* In-place isolation needs writable memory, even for verification-only
         * regions. A read-only page is allowed only if neither phase changes it;
         * such pages are never written by the framework. */
        if (!readable(address, r->size) || (!r->read_only && !writable(address, r->size))) return FALSE;
        for (unsigned j = 0; j < i; ++j)
            if ((uintptr_t)address < (uintptr_t)regions[j].address + e->regions[j].size &&
                (uintptr_t)regions[j].address < (uintptr_t)address + r->size) return FALSE;
        for (unsigned j = 0; j < h->descriptor->span_count; ++j) {
            const es2_hook_span *s = &h->descriptor->spans[j];
            uintptr_t start = args[s->arg_index], end = start + s->offset + s->size;
            if ((uintptr_t)address < end && start < (uintptr_t)address + r->size) return FALSE;
        }
        regions[i].address = address;
        regions[i].buffer_offset = cursor;
        memcpy(before + cursor, address, r->size);
        cursor += r->size;
    }
    return TRUE;
}

static BOOL fpu_equal(const BYTE *a, const BYTE *b) {
    /* FIP/FDP identify different implementation instructions/data, so are not
     * value state. Reserved environment bytes and empty register slots likewise
     * do not participate. CW, full SW, full tags and every occupied 80-bit
     * value DO participate, including signed zero, NaN payload and precision. */
    if (read16(a) != read16(b) || read16(a + 4) != read16(b + 4) || read16(a + 8) != read16(b + 8)) return FALSE;
    unsigned top = (read16(a + 4) >> 11) & 7, tags = read16(a + 8);
    for (unsigned i = 0; i < 8; ++i)
        if (((tags >> (2 * ((top + i) & 7))) & 3) != 3 && memcmp(a + 28 + i * 10, b + 28 + i * 10, 10)) return FALSE;
    return TRUE;
}

static void extended_call(es2_hook *h, const uint32_t *args, const BYTE *before, BOOL original, es2_hook_result *result, const uint32_t *registers) {
    es2_hook_call call = {0};
    uint32_t callback_args[ES2_HOOK_MAX_ARGS] = {(uint32_t)(uintptr_t)args, (uint32_t)(uintptr_t)h->descriptor->user, (uint32_t)(uintptr_t)registers};
    call.fpu_before = before; call.result = result;
    if (original) {
        call.target = h->trampoline; call.abi = h->extension->abi;
        call.arg_count = h->descriptor->arg_count; call.args = args;
        call.entry_registers = registers;
    } else {
        call.target = h->extension->replacement_context ? (void *)h->extension->replacement_context :
            h->extension->result_kind == ES2_RESULT_PAIR ? (void *)h->extension->replacement64 :
            h->extension->result_kind == ES2_RESULT_ST0 ? (void *)h->extension->replacement80 : (void *)h->descriptor->replacement;
        call.abi = ES2_ABI_CDECL; call.arg_count = h->extension->replacement_context ? 3 : 2; call.args = callback_args;
    }
    es2_hook_machine_call(&call);
}

void __attribute__((force_align_arg_pointer)) __cdecl es2_hook_dispatch_extended(extended_frame *frame) {
    es2_hook *h = frame->hook;
    const es2_hook_descriptor *d = h->descriptor;
    const es2_hook_extension *e = h->extension;
    uint32_t args[ES2_HOOK_MAX_ARGS] = {0};
    const uint32_t *stack_args = (const uint32_t *)(frame + 1);
    unsigned reg_count = e->abi == ES2_ABI_BORLAND ? (d->arg_count < 3 ? d->arg_count : 3) : 0;
    const uint32_t regs[] = {frame->eax, frame->edx, frame->ecx};
    for (unsigned i = 0; i < d->arg_count; ++i)
        args[i] = i < reg_count ? regs[i] : stack_args[e->abi == ES2_ABI_BORLAND ? d->arg_count - 1 - i : i];
    es2_hook_result result;
    if (GetCurrentThreadId() != h->owner_thread) {
        extended_call(h, args, frame->fpu, TRUE, &result, regs);
    } else {
        ++h->in_flight;
        uint64_t call_number = ++h->calls;
        if (h->installed != 1 || original_phase_depth || dispatch_depth >= ES2_HOOK_MAX_DEPTH) {
            if (h->installed != 1 || original_phase_depth) ++h->bypassed; else ++h->skipped;
            ++original_phase_depth;
            extended_call(h, args, frame->fpu, TRUE, &result, regs);
            --original_phase_depth;
        } else {
            ++dispatch_depth;
            if (h->options.mode == ES2_HOOK_REPLACED) extended_call(h, args, frame->fpu, FALSE, &result, regs);
            else {
                BYTE state[ES2_HOOK_MAX_STATE_BYTES + ES2_HOOK_MAX_SPANS * 15u];
                BYTE before[ES2_HOOK_MAX_STATE_BYTES], after[ES2_HOOK_MAX_STATE_BYTES], candidate[ES2_HOOK_MAX_STATE_BYTES];
                region_snapshot regions[ES2_HOOK_MAX_SPANS] = {0};
                uint32_t private_args[ES2_HOOK_MAX_ARGS];
                es2_hook_comparison views;
                BOOL ready = snapshot(d, args, private_args, state, &views) && resolve_regions(h, args, regions, before);
                ++original_phase_depth;
                extended_call(h, args, frame->fpu, TRUE, &result, regs);
                --original_phase_depth;
                ready = ready && views_readable(&views);
                for (unsigned i = 0; ready && i < e->region_count; ++i) {
                    const es2_hook_region *r = &e->regions[i];
                    const region_snapshot *s = &regions[i];
                    ready = readable(s->address, r->size);
                    for (unsigned hop = 0; ready && hop < s->pointer_count; ++hop)
                        ready = readable(s->pointer_slots[hop], 4) && read32(s->pointer_slots[hop]) == s->pointer_values[hop];
                    if (ready) {
                        memcpy(after + s->buffer_offset, s->address, r->size);
                        if (memcmp(before + s->buffer_offset, s->address, r->size) && !writable(s->address, r->size)) ready = FALSE;
                    }
                }
                if (!ready) ++h->skipped;
                else {
                    for (unsigned i = 0; i < e->region_count; ++i) {
                        const es2_hook_region *r = &e->regions[i]; const region_snapshot *s = &regions[i];
                        if (memcmp(s->address, before + s->buffer_offset, r->size)) memcpy(s->address, before + s->buffer_offset, r->size);
                    }
                    es2_hook_result replacement;
                    extended_call(h, private_args, frame->fpu, FALSE, &replacement, regs);
                    BOOL unchanged = TRUE;
                    for (unsigned i = 0; i < e->region_count; ++i) {
                        const es2_hook_region *r = &e->regions[i]; const region_snapshot *s = &regions[i];
                        for (unsigned hop = 0; hop < s->pointer_count; ++hop) {
                            const BYTE *slot = s->pointer_slots[hop];
                            if (!readable(slot, 4)) unchanged = FALSE;
                            else if (read32(slot) != s->pointer_values[hop]) {
                                unchanged = FALSE;
                                if (writable(slot, 4)) write32((BYTE *)slot, s->pointer_values[hop]);
                            }
                        }
                        memcpy(candidate + s->buffer_offset, s->address, r->size);
                        if (r->read_only && (memcmp(before + s->buffer_offset, after + s->buffer_offset, r->size) ||
                            memcmp(before + s->buffer_offset, candidate + s->buffer_offset, r->size))) unchanged = FALSE;
                        if (memcmp(s->address, after + s->buffer_offset, r->size)) memcpy(s->address, after + s->buffer_offset, r->size);
                        unsigned j = d->span_count + i;
                        views.original[j] = after + s->buffer_offset; views.replacement[j] = candidate + s->buffer_offset; views.sizes[j] = r->size;
                    }
                    views.span_count = d->span_count + e->region_count;
                    views.original_result = result.eax; views.replacement_result = replacement.eax;
                    views.original_edx = result.edx; views.replacement_edx = replacement.edx;
                    unsigned span; size_t offset;
                    BOOL bytes_equal = compare_bytes(&views, &span, &offset);
                    BOOL returns_equal = e->result_kind == ES2_RESULT_ST0 ?
                        !memcmp(result.fpu + 28, replacement.fpu + 28, 10) : result.eax == replacement.eax;
                    if (e->result_kind == ES2_RESULT_PAIR) returns_equal = returns_equal && result.edx == replacement.edx;
                    BOOL same = d->compare ? d->compare(&views, d->user) : (bytes_equal && returns_equal);
                    if (!same || (e->result_kind == ES2_RESULT_ST0 && !returns_equal) ||
                        !unchanged || !fpu_equal(result.fpu, replacement.fpu)) {
                        ++h->mismatches;
                        log_mismatch(h, call_number, args, &views, span, offset);
                    }
                }
            }
            --dispatch_depth;
        }
        --h->in_flight;
    }
    frame->eax = result.eax; frame->edx = result.edx;
    memcpy(frame->fpu, result.fpu, sizeof(frame->fpu));
}

BOOL es2_hook_uninstall(es2_hook *hook) {
    if (!hook) return FALSE;
    if (!hook->installed && !hook->code) { hook->error = ES2_HOOK_ERROR_NONE; return TRUE; }
    if (GetCurrentThreadId() != hook->owner_thread) { hook->error = ES2_HOOK_ERROR_THREAD; return FALSE; }
    if (hook->in_flight) { hook->error = ES2_HOOK_ERROR_BUSY; return FALSE; }
    size_t n = hook->descriptor->relocate_size;
    if (!readable(hook->site, n) || (memcmp(hook->site, hook->patch, n) && memcmp(hook->site, hook->original, n))) {
        hook->error = ES2_HOOK_ERROR_SIGNATURE; return FALSE;
    }
    InterlockedExchange(&hook->installed, 2);
    DWORD ignored;
    if (!patch_protect(hook, PAGE_EXECUTE_READWRITE, &ignored)) { hook->error = ES2_HOOK_ERROR_ROLLBACK; return FALSE; }
    memcpy(hook->site, hook->original, n);
    BOOL flushed = patch_flush(hook);
    BOOL protected = patch_protect(hook, hook->original_protection, &ignored);
    if (!flushed || !protected) { hook->error = ES2_HOOK_ERROR_ROLLBACK; return FALSE; }
    InterlockedExchange(&hook->installed, 0);
    release_code(hook);
    hook->error = ES2_HOOK_ERROR_NONE;
    return TRUE;
}

BOOL es2_hook_read_mode(const WCHAR *path, const char *name, es2_hook_mode *mode) {
    if (!mode) return FALSE;
    *mode = ES2_HOOK_ORIGINAL;
    if (!path || !*path || !name_valid(name)) return FALSE;
    WCHAR key[ES2_HOOK_MAX_NAME + 1], value[16];
    size_t n = strlen(name);
    for (size_t i = 0; i <= n; ++i) key[i] = (WCHAR)(unsigned char)name[i];
    DWORD got = GetPrivateProfileStringW(L"hooks", key, L"original", value, 16, path);
    if (!got || got >= 15) return FALSE;
    if (!_wcsicmp(value, L"original")) return TRUE;
    if (!_wcsicmp(value, L"replaced")) { *mode = ES2_HOOK_REPLACED; return TRUE; }
    if (!_wcsicmp(value, L"compare")) { *mode = ES2_HOOK_COMPARE; return TRUE; }
    return FALSE;
}

BOOL es2_hook_log_open(es2_hook_log *log, const WCHAR *path, uint32_t limit, es2_hook_frame frame, void *user) {
    if (!log || !path || !*path) return FALSE;
    if (log->file && log->file != INVALID_HANDLE_VALUE) return FALSE;
    memset(log, 0, sizeof(*log));
    log->file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log->file == INVALID_HANDLE_VALUE) return FALSE;
    log->limit = limit ? limit : 32u;
    log->frame = frame;
    log->user = user;
    return TRUE;
}

void es2_hook_log_close(es2_hook_log *log) {
    if (!log) return;
    if (log->file && log->file != INVALID_HANDLE_VALUE) CloseHandle(log->file);
    log->file = INVALID_HANDLE_VALUE;
}
