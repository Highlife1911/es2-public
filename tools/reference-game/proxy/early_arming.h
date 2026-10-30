/* Arming at the mission load and the after-load snapshot.
 * Design and evidence: docs/reverse-engineering/mission_load.md, section 13
 * "arming before the load and the after-load snapshot".
 *
 * Without either key below nothing at all is changed (no allocation, no byte
 * of the image touched). DBSIM.EXE only.
 *
 * [arming] early=1 replaces, in memory, the rel32 of the single call of the
 * mission load in the game main (`call 0x461608` at 0x45f42b) so that the
 * call enters ea_entry_load instead. The proxy then arms (function hooks,
 * tick log, live coverage, entry probes, RNG hooks: the same
 * initialize_forwarding that the first MCI call runs otherwise) before the
 * first instruction of 0x461608 instead of inside it at 0x461dc3.
 *
 * [snapshot] after_load=1 replaces the rel32 of the main loop's call of the
 * logic frame (`call 0x45f570` at 0x45f487) and, at its first execution,
 * writes the after-load snapshot (fixed regions plus every block of the
 * runtime's heap, mission_load.md section 9) to [snapshot] path. The heap is
 * CW3220.DLL's (the reference file, its region table) or the
 * own cw3220.dll's (rtl/; its region table through the es2_own_runtime
 * descriptor, flag 128).
 *
 * Both entries use the technique of rtl_imports.h (rtl_startup_entry): the
 * entry stub saves ESP, switches to a private stack, saves the flags, every
 * general register, the TEB stack bounds (set to the private stack while the
 * step runs) and the x87/SSE state, restores the call's original rel32, runs
 * the step, restores everything and jumps to the original target with ESP
 * and all registers as the call left them. The only stack write is the
 * call's own return address, which is the original one (site + 5); nothing
 * below ESP is touched, so no stack residue reaches the load or the frame.
 * A site is taken once: its bytes are original again before the step runs.
 *
 * DllMain-safe part: the two INI keys are read in DllMain (kernel32 only, no
 * DLL is loaded; the same exception as vol_probes.h's [vol_probes]), the
 * image and
 * both call sites are checked and patched, and the private stack is
 * allocated only when a key is set. Everything else runs in the steps, on
 * the main thread, outside the loader lock.
 */
#ifndef ES2_EARLY_ARMING_H
#define ES2_EARLY_ARMING_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "hook/hook.h"
#include "es2/rtl/own_runtime.h"

#define EA_LINK_BASE 0x00400000u
#define EA_IMAGE_SIZE 0x002d8000u
#define EA_LOAD_CALL_RVA 0x0005f42bu     /* call 0x461608 (mission load) in the game main 0x45f160 */
#define EA_LOAD_TARGET_RVA 0x00061608u
#define EA_FRAME_CALL_RVA 0x0005f487u    /* call 0x45f570 (logic frame) in the main loop */
#define EA_FRAME_TARGET_RVA 0x0005f570u
#define EA_PRIVATE_STACK (1024u * 1024u)
/* CW3220 heap variables (RVAs in the DLL; rtl_imports.md "The CW3220 heap"). */
#define EA_CW_IMAGE_SIZE 0x0002e000u
#define EA_CW_REGION_COUNT 0x00023938u
#define EA_CW_REGION_TABLE 0x00023738u
#define EA_CW_REGION_LIMIT 64u
/* The game allocator's arena (rtl_imports.md 4.3): header, threshold word. */
#define EA_ARENA_HEADER 0x006bb2f8u
#define EA_ARENA_THRESHOLD 0x006bb304u
#define EA_ARENA_BASE 0x00604f04u
#define EA_ARENA_SIZE 0x000b0000u

enum { EA_SITE_LOAD, EA_SITE_FRAME, EA_SITES };
enum { EA_OFF = 0, EA_PATCHED = 1, EA_RAN = 2, EA_REFUSED = -1, EA_RESTORE_FAILED = -2 };
typedef void (*ea_step)(void);
typedef struct ea_site {
    BYTE *site;          /* the `call rel32` instruction */
    uint32_t target;     /* its original target */
    uint32_t original;   /* its original rel32 */
    ea_step step;
    volatile LONG state;
} ea_site;
static ea_site ea_sites[EA_SITES];
static LONG ea_step_runs[EA_SITES];
static BYTE *ea_image;
static uintptr_t ea_image_base;   /* where the image is (0x400000 in DBSIM) */

/* Read by the assembly below. */
__attribute__((used)) static uint32_t ea_entry_esp, ea_entry_target, ea_entry_index;
__attribute__((used)) static uint32_t ea_private_base, ea_private_top;
__attribute__((used)) static BYTE ea_fpu_state[512] __attribute__((aligned(16)));

__asm__(
    ".text\n.p2align 4\n"
    "_ea_entry_load:\n"
    "    movl %esp, _ea_entry_esp\n"
    "    movl $0, _ea_entry_index\n"
    "    jmp _ea_entry_common\n"
    ".p2align 4\n"
    "_ea_entry_frame:\n"
    "    movl %esp, _ea_entry_esp\n"
    "    movl $1, _ea_entry_index\n"
    "    jmp _ea_entry_common\n"
    ".p2align 4\n"
    "_ea_entry_common:\n"
    "    movl _ea_private_top, %esp\n"
    "    pushfl\n"
    "    pushal\n"
    "    pushl %fs:4\n"
    "    pushl %fs:8\n"
    "    movl _ea_private_top, %eax\n"
    "    movl %eax, %fs:4\n"
    "    movl _ea_private_base, %eax\n"
    "    movl %eax, %fs:8\n"
    "    fxsave _ea_fpu_state\n"
    "    cld\n"
    "    call _ea_run_step\n"
    "    fxrstor _ea_fpu_state\n"
    "    popl %fs:8\n"
    "    popl %fs:4\n"
    "    popal\n"
    "    popfl\n"
    "    movl _ea_entry_esp, %esp\n"
    "    jmp *_ea_entry_target\n");
extern void ea_entry_load(void) __asm__("_ea_entry_load");
extern void ea_entry_frame(void) __asm__("_ea_entry_frame");

static uint32_t ea_u32(const BYTE *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static BOOL ea_write_rel32(BYTE *site, uint32_t from, uint32_t to) {
    DWORD protection, ignored;
    if (ea_u32(site + 1) != from) return FALSE;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &protection)) return FALSE;
    memcpy(site + 1, &to, 4);
    BOOL ok = FlushInstructionCache(GetCurrentProcess(), site, 5);
    if (!VirtualProtect(site, 5, protection, &ignored)) ok = FALSE;
    return ok;
}

/* Runs on the private stack. Restores the site first, so the step sees (and
 * any verifier it runs checks) the original bytes. */
void __cdecl ea_run_step(void) __asm__("_ea_run_step") __attribute__((used));
void __cdecl ea_run_step(void) {
    DWORD error = GetLastError();
    ea_site *s = &ea_sites[ea_entry_index < EA_SITES ? ea_entry_index : 0];
    ea_entry_target = s->target;
    uint32_t ours = (uint32_t)((uintptr_t)(ea_entry_index == EA_SITE_LOAD ? ea_entry_load : ea_entry_frame) - ((uintptr_t)s->site + 5));
    if (s->state == EA_PATCHED) {
        if (ea_write_rel32(s->site, ours, s->original)) {
            InterlockedExchange(&s->state, EA_RAN);
            InterlockedIncrement(&ea_step_runs[ea_entry_index]);
            if (s->step) s->step();
        } else InterlockedExchange(&s->state, EA_RESTORE_FAILED);
    }
    SetLastError(error);
}

static BOOL ea_private_stack_allocate(void) {
    if (ea_private_base) return TRUE;
    BYTE *stack = VirtualAlloc(NULL, EA_PRIVATE_STACK, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!stack) return FALSE;
    ea_private_base = (uint32_t)(uintptr_t)stack;
    ea_private_top = ea_private_base + EA_PRIVATE_STACK;
    return TRUE;
}

/* The site must be `call rel32` to the expected target. */
static BOOL ea_site_valid(const BYTE *image, uint32_t site_rva, uint32_t target_rva) {
    const BYTE *site = image + site_rva;
    return site[0] == 0xe8 && (uint32_t)((uintptr_t)site + 5 + ea_u32(site + 1)) == (uint32_t)((uintptr_t)image + target_rva);
}

/* Memory only (DllMain-safe). `image` must be a PE image based at
 * expected_base (es2_hook_image_valid). Either both requested sites are
 * patched or nothing is changed. */
static BOOL ea_install_image(BYTE *image, uintptr_t expected_base, BOOL early, BOOL after_load, ea_step arm, ea_step snapshot) {
    if (!early && !after_load) return TRUE;
    for (unsigned i = 0; i < EA_SITES; ++i) if (ea_sites[i].state != EA_OFF) return FALSE;
    BOOL ok = es2_hook_image_valid(image, expected_base, EA_IMAGE_SIZE) &&
        (!early || ea_site_valid(image, EA_LOAD_CALL_RVA, EA_LOAD_TARGET_RVA)) &&
        (!after_load || ea_site_valid(image, EA_FRAME_CALL_RVA, EA_FRAME_TARGET_RVA)) &&
        ea_private_stack_allocate();
    if (!ok) {
        if (early) ea_sites[EA_SITE_LOAD].state = EA_REFUSED;
        if (after_load) ea_sites[EA_SITE_FRAME].state = EA_REFUSED;
        return FALSE;
    }
    ea_image = image; ea_image_base = expected_base;
    const uint32_t rvas[EA_SITES][2] = {{EA_LOAD_CALL_RVA, EA_LOAD_TARGET_RVA}, {EA_FRAME_CALL_RVA, EA_FRAME_TARGET_RVA}};
    void (*const stubs[EA_SITES])(void) = {ea_entry_load, ea_entry_frame};
    const BOOL wanted[EA_SITES] = {early, after_load};
    const ea_step steps[EA_SITES] = {arm, snapshot};
    for (unsigned i = 0; i < EA_SITES; ++i) {
        if (!wanted[i]) continue;
        ea_site *s = &ea_sites[i];
        s->site = image + rvas[i][0];
        s->original = ea_u32(s->site + 1);
        s->target = (uint32_t)((uintptr_t)image + rvas[i][1]);
        s->step = steps[i];
        s->state = EA_PATCHED;   /* published before the bytes */
        MemoryBarrier();
        if (!ea_write_rel32(s->site, s->original, (uint32_t)((uintptr_t)stubs[i] - ((uintptr_t)s->site + 5)))) {
            s->state = EA_REFUSED;
            for (unsigned j = 0; j < i; ++j)
                if (ea_sites[j].state == EA_PATCHED &&
                    ea_write_rel32(ea_sites[j].site, (uint32_t)((uintptr_t)stubs[j] - ((uintptr_t)ea_sites[j].site + 5)), ea_sites[j].original))
                    ea_sites[j].state = EA_REFUSED;
            return FALSE;
        }
    }
    return TRUE;
}

/* ------------------------------------------------------------------------
 * The after-load snapshot (mission_load.md section 9). File format
 * ("ES2SNAP1", all words little-endian uint32):
 *   header: magic[8], version 2, flags, frame, image_base, image_size,
 *           fixed_count
 *   fixed_count x {va, length, readable} + length bytes when readable
 *   heap: region_count, then per region {base, size, status, block_count}
 *         and block_count x {address p, size s, flags} + s-8 bytes when
 *         flag 2 is set (bit 0: in use; bit 1: contents follow); the regions
 *         of CW3220.DLL's heap or of the own cw3220.dll's (flag 128): both
 *         keep the same block layout (rtl_imports.md "the heap, shadow
 *         stage", "the heap, replaced")
 *   memory map (version 2, for the noise model): map_count x {base, size,
 *         allocation_base, protect, type} for every committed region of
 *         the address space, then module_count x {base, size_of_image,
 *         name_length} + the module's file name (UTF-8, no directory) for
 *         every allocation base of an image region
 * flags: 1 heap walked, 2 heap unavailable, 4 heap walk error, 8 truncated
 *        (max_bytes reached: later blocks without contents), 16 complete,
 *        32 armed early (the mission load ran after arming), 64 memory map
 *        truncated (more than EA_MAP_MAX regions or EA_MODULE_MAX modules),
 *        128 the heap is the own runtime's (without it, CW3220's).
 * va is the reference address (0x400000-based); heap addresses are the
 * real ones. Only blocks in use carry contents (free blocks hold stale
 * bytes). The comparison: tools/reference-game/scripts/after_load_snapshot.py
 * (it also reads version 1, which ends after the heap). */
typedef struct ea_region { uint32_t va, length; } ea_region;
/* Fixed regions of mission_load.md sections 5 and 9 (reference VAs). */
static const ea_region ea_fixed_regions[] = {
    {0x004a9572u, 0x004a9dfcu - 0x004a9572u},   /* stage-2 type globals, actor pools and lists, player 0x4a9b84 */
    {0x004a9e00u, 0x004aa728u - 0x004a9e00u},   /* header and mission tables, variables 0x4a9e70 (2000), strings */
    {0x004aa728u, 0x004aaa7cu - 0x004aa728u},   /* status word, selection and remap, MEC, terrain object table */
    {0x004aab00u, 0x000249f0u},                 /* the table 0x428230 clears (stage 2, record 27) */
    {0x004cf878u, 0x14u},                       /* 0x428230's pointers 0x4cf878..0x4cf888 */
    {0x006b4f2cu, 0x10u},                       /* fires */
    {0x004d24bcu, 0x004d27b8u - 0x004d24bcu},   /* globals object: player, cameras, both RNG streams */
    {0x0049a871u, 0x0049a884u - 0x0049a871u},   /* pass 2's default position and angle statics */
    {0x004d03c8u, 4u}, {0x0049b0f8u, 4u}, {0x0049b034u, 4u},
    {0x004a0be0u, 4u},                          /* terrain object pointer */
    {EA_ARENA_HEADER, 0x18u},                   /* arena header, threshold, CW3220 byte sum 0x6bb30c */
};
#define EA_FIXED_COUNT (sizeof(ea_fixed_regions) / sizeof(ea_fixed_regions[0]))
#define EA_EXTRA_MAX 16u

typedef struct ea_writer { HANDLE file; BYTE buffer[65536]; DWORD used; BOOL failed; uint64_t total; } ea_writer;
static ea_writer ea_out;
static void ea_flush(ea_writer *w) {
    DWORD offset = 0, written = 0;
    while (!w->failed && offset < w->used) {
        if (!WriteFile(w->file, w->buffer + offset, w->used - offset, &written, NULL) || !written) w->failed = TRUE;
        offset += written;
    }
    w->used = 0;
}
static void ea_put(ea_writer *w, const void *data, DWORD size) {
    const BYTE *p = data;
    w->total += size;
    while (size && !w->failed) {
        if (w->used == sizeof(w->buffer)) ea_flush(w);
        DWORD take = (DWORD)sizeof(w->buffer) - w->used;
        if (take > size) take = size;
        memcpy(w->buffer + w->used, p, take);
        w->used += take; p += take; size -= take;
    }
}
static void ea_put32(ea_writer *w, uint32_t v) { ea_put(w, &v, 4); }
/* Overwrites one word already written (flushes first). */
static void ea_patch32(ea_writer *w, uint64_t at, uint32_t v) {
    ea_flush(w);
    LARGE_INTEGER position, end; position.QuadPart = (LONGLONG)at; end.QuadPart = (LONGLONG)w->total;
    DWORD written = 0;
    if (w->failed || !SetFilePointerEx(w->file, position, NULL, FILE_BEGIN) || !WriteFile(w->file, &v, 4, &written, NULL) ||
        written != 4 || !SetFilePointerEx(w->file, end, NULL, FILE_BEGIN)) w->failed = TRUE;
}

static BOOL ea_readable(const void *pointer, SIZE_T size) {
    uintptr_t cursor = (uintptr_t)pointer;
    if (!cursor || size > UINTPTR_MAX - cursor) return FALSE;
    uintptr_t end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery((void *)cursor, &region, sizeof(region)) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return FALSE;
        uintptr_t next = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (next <= cursor) return FALSE;
        cursor = next;
    }
    return TRUE;
}

static void ea_put_region(ea_writer *w, uint32_t va, uint32_t length) {
    const BYTE *p = ea_image + (va - EA_LINK_BASE);
    BOOL ok = va >= EA_LINK_BASE && va - EA_LINK_BASE <= EA_IMAGE_SIZE && length <= EA_IMAGE_SIZE - (va - EA_LINK_BASE) &&
        ea_readable(p, length);
    ea_put32(w, va); ea_put32(w, length); ea_put32(w, ok ? 1u : 0u);
    if (ok) ea_put(w, p, length);
}

/* One region of the CW3220 heap: a marker word 1 at base, the first block at
 * base+8, header at p-4 and footer at p+s-8 (s | 1 in use), the end marker
 * at base+size-4. Returns the block count, or -1 for a malformed region. */
static int32_t ea_heap_count(uint32_t base, uint32_t size) {
    uint32_t end = base + size;
    if (end < base || size < 24u || !ea_readable((const void *)(uintptr_t)base, size) || ea_u32((const BYTE *)(uintptr_t)base) != 1u) return -1;
    int32_t blocks = 0;
    uint32_t p = base + 8u;
    while (p < end) {
        uint32_t header = ea_u32((const BYTE *)(uintptr_t)(p - 4u)), s = header & ~1u;
        if (s < 16u || (s & 3u) || s > end - p || ea_u32((const BYTE *)(uintptr_t)(p + s - 8u)) != header) return -1;
        p += s; ++blocks;
    }
    return p == end && ea_u32((const BYTE *)(uintptr_t)(end - 4u)) == 1u ? blocks : -1;
}

/* The memory map of version 2 (the noise model: pointers into system
 * heaps, VirtualAlloc'd buffers, mapped views and modules vary between runs;
 * the comparison classifies them with this map). One VirtualQuery walk over
 * the user address space writes every committed region; the count is
 * patched afterwards. Then one record per image allocation base: the size
 * of image from its PE header (0 when unreadable) and the module's file name
 * without its directory (empty when it is no loaded module). Returns FALSE
 * when a limit cut the map short. */
#define EA_MAP_MAX 65536u
#define EA_MODULE_MAX 512u
static uint32_t ea_module_bases[EA_MODULE_MAX];
static BOOL ea_put_memory_map(ea_writer *w) {
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    uintptr_t cursor = (uintptr_t)system.lpMinimumApplicationAddress, top = (uintptr_t)system.lpMaximumApplicationAddress;
    uint64_t count_at = w->total;
    uint32_t count = 0, modules = 0;
    BOOL complete = TRUE;
    ea_put32(w, 0u);
    while (cursor < top) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery((void *)cursor, &region, sizeof(region))) { complete = FALSE; break; }
        uintptr_t next = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (next <= cursor) break;
        if (region.State == MEM_COMMIT) {
            if (count == EA_MAP_MAX) complete = FALSE;
            else {
                ea_put32(w, (uint32_t)(uintptr_t)region.BaseAddress); ea_put32(w, (uint32_t)region.RegionSize);
                ea_put32(w, (uint32_t)(uintptr_t)region.AllocationBase); ea_put32(w, region.Protect); ea_put32(w, region.Type);
                ++count;
            }
            if (region.Type == MEM_IMAGE) {
                uint32_t base = (uint32_t)(uintptr_t)region.AllocationBase, k = 0;
                while (k < modules && ea_module_bases[k] != base) ++k;
                if (k == modules) { if (modules < EA_MODULE_MAX) ea_module_bases[modules++] = base; else complete = FALSE; }
            }
        }
        cursor = next;
    }
    ea_patch32(w, count_at, count);
    ea_put32(w, modules);
    for (uint32_t k = 0; k < modules; ++k) {
        const BYTE *b = (const BYTE *)(uintptr_t)ea_module_bases[k];
        uint32_t size = 0;
        if (ea_readable(b, 0x40)) {
            uint32_t pe = ea_u32(b + 0x3c);
            if (pe < 0x1000u - 0x58u && ea_readable(b + pe, 0x58) && ea_u32(b + pe) == 0x00004550u) size = ea_u32(b + pe + 24 + 56);
        }
        WCHAR path[MAX_PATH];
        char name[3 * MAX_PATH];
        DWORD n = GetModuleFileNameW((HMODULE)(uintptr_t)ea_module_bases[k], path, MAX_PATH);
        int length = 0;
        if (n && n < MAX_PATH) {
            const WCHAR *slash = wcsrchr(path, L'\\');
            length = WideCharToMultiByte(CP_UTF8, 0, slash ? slash + 1 : path, -1, name, (int)sizeof(name), NULL, NULL);
            length = length > 0 ? length - 1 : 0;
        }
        ea_put32(w, ea_module_bases[k]); ea_put32(w, size); ea_put32(w, (uint32_t)length);
        if (length) ea_put(w, name, (DWORD)length);
    }
    return complete;
}

/* The heap the snapshot walks: the region table of CW3220.DLL's heap (its
 * variables 0x423938/0x423738, read from the module) or of the own
 * cw3220.dll's (es2_heap through es2_own_runtime). count beyond
 * EA_CW_REGION_LIMIT: no region is taken (flag 4). */
enum { EA_HEAP_NONE = 0, EA_HEAP_CW3220 = 1, EA_HEAP_OWN = 2 };
typedef struct ea_heap {
    uint32_t kind, count;
    uint32_t base[EA_CW_REGION_LIMIT], size[EA_CW_REGION_LIMIT];
} ea_heap;
static void ea_heap_from_cw(const BYTE *cw, ea_heap *heap) {
    memset(heap, 0, sizeof(*heap));
    heap->kind = EA_HEAP_CW3220;
    heap->count = ea_u32(cw + EA_CW_REGION_COUNT);
    for (uint32_t r = 0; heap->count <= EA_CW_REGION_LIMIT && r < heap->count; ++r) {
        heap->base[r] = ea_u32(cw + EA_CW_REGION_TABLE + 8u * r);
        heap->size[r] = ea_u32(cw + EA_CW_REGION_TABLE + 8u * r + 4u);
    }
}
/* The own runtime's descriptor of a module (its es2_own_runtime export,
 * the version this proxy knows), else NULL. */
static const es2_own_runtime_info *ea_own_runtime(HMODULE module) {
    es2_own_runtime_fn describe = module ? (es2_own_runtime_fn)(void *)GetProcAddress(module, ES2_OWN_RUNTIME_EXPORT) : NULL;
    const es2_own_runtime_info *info = describe ? describe() : NULL;
    return info && info->version == ES2_OWN_RUNTIME_VERSION && info->heap ? info : NULL;
}
static BOOL ea_heap_from_own(const es2_own_runtime_info *info, ea_heap *heap) {
    memset(heap, 0, sizeof(*heap));
    if (!info || !info->heap || !ea_readable(info->heap, sizeof(*info->heap))) return FALSE;
    const es2_heap *own = info->heap;
    heap->kind = EA_HEAP_OWN;
    heap->count = own->region_count;
    for (uint32_t r = 0; heap->count <= EA_CW_REGION_LIMIT && r < heap->count; ++r) {
        heap->base[r] = own->region_base[r];
        heap->size[r] = own->region_size[r];
    }
    return TRUE;
}

/* Writes the snapshot. heap: the runtime's region table (filled by the
 * caller) or NULL. extra: further fixed regions. Returns TRUE when complete. */
static BOOL ea_snapshot_write(const WCHAR *path, const ea_heap *heap, const ea_region *extra, unsigned extra_count,
                              uint64_t max_bytes, BOOL armed_early) {
    ea_writer *w = &ea_out;
    w->used = 0; w->failed = FALSE; w->total = 0;
    w->file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (w->file == INVALID_HANDLE_VALUE) return FALSE;
    const BYTE *globals = ea_image + (0x004d24bcu - EA_LINK_BASE);
    uint32_t flags = armed_early ? 32u : 0u;
    uint32_t arena = ea_u32(ea_image + (EA_ARENA_THRESHOLD - EA_LINK_BASE));
    unsigned fixed = (unsigned)EA_FIXED_COUNT + extra_count + (arena ? 1u : 0u);
    ea_put(w, "ES2SNAP1", 8);
    ea_put32(w, 2u); ea_put32(w, 0u);
    ea_put32(w, ea_u32(globals + 0xbf));
    ea_put32(w, (uint32_t)ea_image_base); ea_put32(w, EA_IMAGE_SIZE);
    ea_put32(w, fixed);
    for (unsigned i = 0; i < EA_FIXED_COUNT; ++i) ea_put_region(w, ea_fixed_regions[i].va, ea_fixed_regions[i].length);
    for (unsigned i = 0; i < extra_count; ++i) ea_put_region(w, extra[i].va, extra[i].length);
    if (arena) ea_put_region(w, EA_ARENA_BASE, EA_ARENA_SIZE);   /* only with the -l arena (new below the threshold) */
    BOOL walked = heap && heap->kind != EA_HEAP_NONE;
    uint32_t count = walked ? heap->count : 0u;
    if (walked && heap->kind == EA_HEAP_OWN) flags |= 128u;
    if (!walked) { flags |= 2u; ea_put32(w, 0u); }
    else if (count > EA_CW_REGION_LIMIT) { flags |= 4u; ea_put32(w, 0u); }
    else {
        flags |= 1u;
        ea_put32(w, count);
        uint64_t dumped = 0;
        for (uint32_t r = 0; r < count; ++r) {
            uint32_t base = heap->base[r], size = heap->size[r];
            int32_t blocks = ea_heap_count(base, size);
            ea_put32(w, base); ea_put32(w, size); ea_put32(w, blocks >= 0 ? 1u : 0u); ea_put32(w, blocks >= 0 ? (uint32_t)blocks : 0u);
            if (blocks < 0) { flags |= 4u; continue; }
            for (uint32_t p = base + 8u; p < base + size;) {
                uint32_t header = ea_u32((const BYTE *)(uintptr_t)(p - 4u)), s = header & ~1u, block_flags = header & 1u;
                if ((header & 1u) && dumped + (s - 8u) <= max_bytes) { block_flags |= 2u; dumped += s - 8u; }
                else if (header & 1u) flags |= 8u;
                ea_put32(w, p); ea_put32(w, s); ea_put32(w, block_flags);
                if (block_flags & 2u) ea_put(w, (const void *)(uintptr_t)p, s - 8u);
                p += s;
            }
        }
    }
    if (!ea_put_memory_map(w)) flags |= 64u;
    ea_flush(w);
    flags |= 16u;
    LARGE_INTEGER at; at.QuadPart = 12;
    DWORD written = 0;
    BOOL ok = !w->failed && SetFilePointerEx(w->file, at, NULL, FILE_BEGIN) &&
        WriteFile(w->file, &flags, 4, &written, NULL) && written == 4 && FlushFileBuffers(w->file);
    if (!CloseHandle(w->file)) ok = FALSE;
    w->file = INVALID_HANDLE_VALUE;
    return ok;
}

/* "0xVA:LEN,0xVA:LEN" (hexadecimal VA, decimal or 0x LEN), at most 16. */
static BOOL ea_parse_extra(const WCHAR *text, ea_region *out, unsigned *count) {
    *count = 0;
    const WCHAR *p = text;
    while (*p == L' ') ++p;
    if (!*p) return TRUE;
    for (;;) {
        if (*count == EA_EXTRA_MAX) return FALSE;
        WCHAR *end;
        unsigned long va = wcstoul(p, &end, 16);
        if (end == p || *end != L':') return FALSE;
        p = end + 1;
        unsigned long length = wcstoul(p, &end, 0);
        if (end == p || !length || va < EA_LINK_BASE || va - EA_LINK_BASE >= EA_IMAGE_SIZE ||
            length > EA_IMAGE_SIZE - (va - EA_LINK_BASE)) return FALSE;
        out[(*count)++] = (ea_region){(uint32_t)va, (uint32_t)length};
        p = end;
        while (*p == L' ') ++p;
        if (!*p) return TRUE;
        if (*p++ != L',') return FALSE;
        while (*p == L' ') ++p;
    }
}

/* The snapshot step's configuration ([snapshot] path, extra, max_bytes in
 * MiB, default 256) from `ini`; then the write. */
static BOOL ea_snapshot_from_config(const WCHAR *ini, const ea_heap *heap, BOOL armed_early) {
    WCHAR path[MAX_PATH], extra_text[512];
    DWORD n = GetPrivateProfileStringW(L"snapshot", L"path", L"", path, MAX_PATH, ini);
    if (!n || n >= MAX_PATH - 1) return FALSE;
    n = GetPrivateProfileStringW(L"snapshot", L"extra", L"", extra_text, 512, ini);
    if (n >= 511) return FALSE;
    ea_region extra[EA_EXTRA_MAX]; unsigned extra_count = 0;
    if (!ea_parse_extra(extra_text, extra, &extra_count)) return FALSE;
    UINT mib = GetPrivateProfileIntW(L"snapshot", L"max_bytes_mib", 256, ini);
    return ea_snapshot_write(path, heap, extra, extra_count, (uint64_t)mib << 20, armed_early);
}

#ifndef ES2_EA_NO_PROCESS_ENTRY
/* ------------------------------------------------------------------------
 * Proxy part (media_proxy.c): steps and the DllMain entry. */
static WCHAR ea_config_path[MAX_PATH];
static BOOL ea_armed_early;

/* Step at 0x45f42b: arm now, unless something armed already (a DirectDraw
 * or MCI call before the load; then the load was armed anyway). */
static void ea_arm_step(void) {
    BOOL pending = FALSE;
    BOOL done = InitOnceBeginInitialize(&forwarding_once, INIT_ONCE_CHECK_ONLY, &pending, NULL) && !pending;
    BOOL armed = InitOnceExecuteOnce(&forwarding_once, initialize_forwarding, NULL, NULL);
    ea_armed_early = armed && !done;
    log_event(done ? "early_arming_already_armed" : armed ? "early_arming_armed" : "early_arming_forwarding_failed");
}

/* The loaded CW3220.DLL, only when it is the reference file (the check of
 * rtl_heap_shadow.h). */
static const BYTE *ea_cw_module(void) {
    HMODULE cw = GetModuleHandleW(L"cw3220.DLL");
    WCHAR path[MAX_PATH];
    DWORD n = cw ? GetModuleFileNameW(cw, path, MAX_PATH) : 0;
    if (!n || n >= MAX_PATH || !rtl_file_matches(path, RTL_HEAP_CW_FILE_SIZE, RTL_HEAP_CW_SHA256)) return NULL;
    const BYTE *b = (const BYTE *)cw;
    uint32_t pe = ea_u32(b + 0x3c);
    return pe < 4096 - 248 && ea_u32(b + pe + 24 + 56) == EA_CW_IMAGE_SIZE ? b : NULL;
}

/* The heap of the runtime behind DBSIM's cw3220 imports: the own cw3220.dll
 * (it exports es2_own_runtime) or the reference CW3220.DLL; NULL for
 * anything else (flag 2). */
static const ea_heap *ea_process_heap(ea_heap *heap) {
    const es2_own_runtime_info *own = ea_own_runtime(GetModuleHandleW(L"cw3220.DLL"));
    if (own) return ea_heap_from_own(own, heap) ? heap : NULL;
    const BYTE *cw = ea_cw_module();
    if (!cw) return NULL;
    ea_heap_from_cw(cw, heap);
    return heap;
}

/* Step at 0x45f487 (first logic frame). */
static ea_heap ea_step_heap;
static void ea_snapshot_step(void) {
    BOOL ok = ea_snapshot_from_config(ea_config_path, ea_process_heap(&ea_step_heap), ea_armed_early);
    log_event(ok ? "after_load_snapshot_written" : "after_load_snapshot_failed");
}

/* DllMain: DBSIM.EXE only; reads [arming] early and [snapshot] after_load
 * from es2-oracle.ini next to the proxy. Without them nothing is done. */
static void install_early_arming(void) {
    WCHAR exe[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, exe, MAX_PATH);
    WCHAR *name = length && length < MAX_PATH ? wcsrchr(exe, L'\\') : NULL;
    if (!name || _wcsicmp(name + 1, L"DBSIM.EXE")) return;
    DWORD n = GetModuleFileNameW(self_module, ea_config_path, MAX_PATH);
    WCHAR *slash = n && n < MAX_PATH ? wcsrchr(ea_config_path, L'\\') : NULL;
    if (!slash || slash - ea_config_path + 1 + wcslen(L"es2-oracle.ini") >= MAX_PATH) { ea_config_path[0] = 0; return; }
    wcscpy(slash + 1, L"es2-oracle.ini");
    BOOL early = GetPrivateProfileIntW(L"arming", L"early", 0, ea_config_path) == 1;
    BOOL after_load = GetPrivateProfileIntW(L"snapshot", L"after_load", 0, ea_config_path) == 1;
    ea_install_image((BYTE *)GetModuleHandleW(NULL), EA_LINK_BASE, early, after_load, ea_arm_step, ea_snapshot_step);
}
#endif
#endif
