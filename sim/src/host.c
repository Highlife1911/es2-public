/* The own process's host: mapping, set-up, exit (es2/sim/host.h). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/host.h"
#include "es2/sim/image_access.h"
#include "es2/sim/image_data.h"
#include "es2/sim/loop.h"
#include "sim/oracle/tick_record.h"

static int host_fail(es2_sim *sim, int code, const char *error) { sim->error = error; return code; }

/* The VAs whose machine calls from the seamed bridges reach the dispatch
 * through the bridge seam instead of the trap (es2/sim/trap.h; own_process.md,
 * "the bridge seam for the hot VAs": the first ten; "the remaining bridges and
 * 54 more VAs": the next 54 by traps in acceptance-60s), sorted ascending; at
 * most ES2_SIM_BRIDGE_MAX. */
static const uint32_t host_bridged_vas[] = {
    0x0040250cu,   /* animation_step */
    0x00402604u,   /* actor_animate */
    0x004029d8u,   /* terrain_pose */
    0x00403548u,   /* base_aim_node */
    0x004035a4u,   /* base_radius */
    0x004035b8u,   /* base_sight_radius */
    0x00403914u,   /* base_parts */
    0x00403ca8u,   /* base_update */
    0x00404ae4u,   /* obstacle_test */
    0x00405038u,   /* base_hit_test */
    0x0040e62cu,   /* weapon_ammo */
    0x0040ede8u,   /* weapon_ready */
    0x0040ef94u,   /* weapon_step */
    0x0040f881u,   /* mask_merge */
    0x00411aacu,   /* actor_sight_radius */
    0x00411cecu,   /* actor_step (entry shim) */
    0x00412608u,   /* sight_check */
    0x004128f8u,   /* contact_scan */
    0x0041552cu,   /* mech_sight_radius */
    0x00417b98u,   /* mech_aim_node */
    0x004233a4u,   /* region_test */
    0x004234b8u,   /* trigger_regions */
    0x004236c4u,   /* group_activate */
    0x00423a74u,   /* order_update */
    0x00427238u,   /* cell */
    0x00427c68u,   /* collision_walk */
    0x00427da8u,   /* object_query */
    0x004280f4u,   /* terrain_line */
    0x00467924u,   /* movement_step */
    0x00467a48u,   /* timer_word_step */
    0x00467aa8u,   /* timer_dword_step */
    0x00467adcu,   /* approach_word */
    0x0046a5bcu,   /* convoy_leader */
    0x0046b910u,   /* shape_word */
    0x0046e180u,   /* terrain_height */
    0x0046e980u,   /* segment */
    0x0046ff4cu,   /* slope_test */
    0x00470468u,   /* edge_test */
    0x00470d74u,   /* cell_mark_set */
    0x00470e00u,   /* cell_flag_get */
    0x00471c18u,   /* pool_next */
    0x00471c70u,   /* pool_prev */
    0x00476850u,   /* part_find (service) */
    0x00478644u,   /* removal_flush */
    0x00478d38u,   /* track_advance */
    0x0047d32cu,   /* angle_ratio */
    0x0047de72u,   /* dist3d_delta */
    0x0047e0a0u,   /* multiply_q8 */
    0x0047e0b0u,   /* multiply_q10 */
    0x0047ebb8u,   /* euler_matrix */
    0x0047ffe8u,   /* rotate_2d */
    0x004801d4u,   /* matrix_point (entry shim) */
    0x00480304u,   /* rotate_point */
    0x0048043cu,   /* transform_point */
    0x0048e3e0u,   /* class_new_48e3e0 (service) */
    0x0049288cu,   /* dist_gate */
    0x004928d0u,   /* dist2d_point */
    0x0049290cu,   /* vector_angle */
    0x00492934u,   /* heading_point */
    0x004929fcu,   /* point_offset */
    0x00492ee0u,   /* rng_next */
    0x00492f24u,   /* rng_bounded */
    0x0049630cu,   /* RTL frame record (service, reads EBP) */
    0x0049681au,   /* rt_memset (service, CODE jump thunk of cw3220.dll!_memset) */
};

static uint32_t page_up(uint32_t x) { return (x + 0xfffu) & ~0xfffu; }

static int protect(uint8_t *at, uint32_t size, DWORD protection) {
    DWORD previous;
    return size ? VirtualProtect(at, size, protection, &previous) != 0 : 1;
}

unsigned long es2_sim_code_page_protection(uint32_t va) {
    uint32_t page = va & ~0xfffu;
    for (unsigned i = 0; i < es2_sim_dbsim_data_run_count; ++i) {
        const es2_sim_data_run *r = &es2_sim_dbsim_data_runs[i];
        if (r->kind != ES2_SIM_DATA_CODE_TABLE) continue;
        if ((uint64_t)r->va + r->size > page && r->va < (uint64_t)page + 0x1000u) return PAGE_READONLY;
    }
    return PAGE_NOACCESS;
}

/* The guest image from the own tables (es2/sim/host.h). No file is opened: the
 * layout and the import slots are the own tables, the bytes the own runs
 * written into the zeroed committed range. */
int es2_sim_map(es2_sim *sim, void *reserved) {
    const char *error = NULL;
    es2_sim_image_info_own(&sim->info);
    if (sim->info.image_base != ES2_SIM_DBSIM_BASE || sim->info.image_size != ES2_SIM_DBSIM_IMAGE_SIZE ||
        sim->info.code_lo < sim->info.image_base || sim->info.code_hi <= sim->info.code_lo ||
        sim->info.code_hi - sim->info.image_base > sim->info.image_size || (sim->info.code_lo & 0xfffu) ||
        (sim->info.code_hi & 0xfffu))
        return host_fail(sim, ES2_SIM_EXIT_IMAGE, "own layout: image base, size or CODE range");
    if (reserved != (void *)(uintptr_t)ES2_SIM_DBSIM_BASE ||
        VirtualAlloc(reserved, sim->info.image_size, MEM_COMMIT, PAGE_READWRITE) != reserved)
        return host_fail(sim, ES2_SIM_EXIT_CLAIM, "commit the claimed image range");
    sim->image = reserved;
    /* Committed memory is zero: the runs give the initialised DATA and the
     * CODE bytes own code reads; everything else stays zero (BSS too). */
    if (es2_sim_dbsim_data_write(sim->image, sim->info.image_base, sim->info.image_size, &error))
        return host_fail(sim, ES2_SIM_EXIT_IMAGE, error);
    if (es2_sim_imports_own(sim->imports, ES2_SIM_MAX_IMPORTS, &sim->import_count, &error))
        return host_fail(sim, ES2_SIM_EXIT_IMAGE, error);
    /* One trap address per import slot in a page range that is never
     * executable; the trap keys a call there by the slot's VA. */
    uint32_t trap_bytes = page_up(4u * (sim->import_count ? sim->import_count : 1u));
    sim->import_traps = VirtualAlloc(NULL, trap_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!sim->import_traps) return host_fail(sim, ES2_SIM_EXIT_SETUP, "import trap pages");
    memset(sim->import_traps, 0xcc, trap_bytes);
    if (!protect(sim->import_traps, trap_bytes, PAGE_READONLY)) return host_fail(sim, ES2_SIM_EXIT_SETUP, "import trap protection");
    for (unsigned i = 0; i < sim->import_count; ++i) {
        uint32_t slot = sim->imports[i].slot_va;
        if (slot < sim->info.image_base || slot - sim->info.image_base > sim->info.image_size - 4u)
            return host_fail(sim, ES2_SIM_EXIT_IMAGE, "own import slot outside the image");
        uint32_t trap = (uint32_t)(uintptr_t)(sim->import_traps + 4u * i);
        memcpy(sim->image + (slot - sim->info.image_base), &trap, 4);
        sim->import_slots[i] = slot;
        sim->import_names[i] = sim->imports[i].name;
    }
    /* Protections: headers and read-only sections read-only, writable
     * sections read-write, CODE never executable (no original instruction
     * runs). CODE holds only the own CODE runs; its pages are
     * read-only where a CODE_TABLE run lies (the index-byte tables and the
     * dword 0x4967ae) and no-access elsewhere, so a stray read faults
     * instead of returning zero. The RTTI counts in CODE are not read
     * through the guest: the own models of 0x496406 take them from the runs
     * (es2_sim_dbsim_rtti_count). */
    if (!protect(sim->image, page_up(sim->info.headers_size), PAGE_READONLY))
        return host_fail(sim, ES2_SIM_EXIT_SETUP, "header protection");
    for (unsigned i = 0; i < sim->info.section_count; ++i) {
        const es2_sim_section *s = &sim->info.sections[i];
        uint32_t span = page_up(s->virtual_size > s->raw_size ? s->virtual_size : s->raw_size);
        if (s->rva + span > sim->info.image_size) span = sim->info.image_size - s->rva;
        if (s->flags & ES2_SIM_SCN_EXECUTE) {
            for (uint32_t page = sim->info.image_base + s->rva; page < sim->info.image_base + s->rva + span; page += 0x1000u)
                if (!protect(sim->image + (page - sim->info.image_base), 0x1000u, (DWORD)es2_sim_code_page_protection(page)))
                    return host_fail(sim, ES2_SIM_EXIT_SETUP, "CODE protection");
            continue;
        }
        DWORD p = (s->flags & ES2_SIM_SCN_WRITE) ? PAGE_READWRITE : PAGE_READONLY;
        if (!protect(sim->image + s->rva, span, p)) return host_fail(sim, ES2_SIM_EXIT_SETUP, "section protection");
    }
    /* The image-access measurement, armed only with --image-access
     * (es2/sim/image_access.h); without it this does nothing. */
    if (es2_sim_image_access_arm(&sim->info, es2_sim_image_table_sites, ES2_SIM_IMAGE_TABLE_SITES, &error))
        return host_fail(sim, ES2_SIM_EXIT_SETUP, error);
    return 0;
}

/* ---- log */
static void log_write(es2_sim *sim, const char *text, int length) {
    if (!sim->log || length <= 0) return;
    DWORD written;
    WriteFile((HANDLE)sim->log, text, (DWORD)length, &written, NULL);
}

static void host_refuse(const es2_sim_refusal *r, void *user) {
    es2_sim *sim = user;
    char line[512];
    char name[160];
    if (r->name) snprintf(name, sizeof(name), "\"%s\"", r->name); else strcpy(name, "null");
    int n = snprintf(line, sizeof(line),
        "{\"event\":\"refused\",\"va\":\"0x%08lx\",\"name\":%s,\"reason\":\"%s\",\"return\":\"0x%08lx\",\"frame\":\"0x%08lx\"",
        (unsigned long)r->va, name, r->reason ? r->reason : "", (unsigned long)r->return_address, (unsigned long)r->frame);
    /* A CODE VA names its page of the function index (docs/functions/). */
    if (r->va >= sim->info.code_lo && r->va < sim->info.code_hi)
        n += snprintf(line + n, sizeof(line) - (size_t)n, ",\"index\":\"docs/functions/dbsim-%06lx.md\"",
                      (unsigned long)(r->va & 0xffff0000u));
    n += snprintf(line + n, sizeof(line) - (size_t)n, "}\r\n");
    fputs(line, stderr);
    fflush(stderr);
    log_write(sim, line, n);
    /* A refused trapped call ends the process in the trap handler
     * (dispatch_trap.c); the exit record is written here, before it. A refusal
     * of a C call returns to its caller, which finishes the log. */
    if (r->return_address) es2_sim_finish(sim, "refused", ES2_SIM_EXIT_REFUSED);
}

/* [trace] watch_count and watch1..N, with the proxy's rules (tick_log.h): at
 * most ES2_MEM_WATCH_MAX, each entry present, shorter than 255 characters and
 * printable ASCII, and accepted by the parser. */
static int read_watches(es2_sim *sim, const wchar_t *config) {
    es2_mem_watch_clear(&sim->watches);
    if (!config || !*config) return 0;
    UINT watches = GetPrivateProfileIntW(L"trace", L"watch_count", 0, config);
    if (watches > ES2_MEM_WATCH_MAX) return 1;
    for (UINT i = 1; i <= watches; ++i) {
        wchar_t key[16], wide[256];
        char spec[256];
        swprintf(key, 16, L"watch%u", i);
        DWORD got = GetPrivateProfileStringW(L"trace", key, L"", wide, 256, config);
        if (!got || got >= 255) return 1;
        for (DWORD c = 0; c <= got; ++c) {
            if (wide[c] > 0x7e) return 1;
            spec[c] = (char)wide[c];
        }
        if (es2_mem_watch_add(&sim->watches, spec)) return 1;
    }
    return 0;
}

/* The guest's environment block (es2_sim_guest_environment): es2sim's own,
 * with the launcher's __COMPAT_LAYER put back. Free with free(). */
static char *guest_environment(void) {
    char *environment = GetEnvironmentStringsA();
    char *guest = es2_sim_guest_environment(environment);
    if (environment) FreeEnvironmentStringsA(environment);
    return guest;
}

/* [trace] heap_trace=<path> (own_process.md, "the heap trace"), the
 * proxy's heap trace (rtl_heap_shadow.h) for es2sim, so that one reader
 * takes both: the attach line with the heap's variables and its blocks
 * before DBSIM's entry and the sizes of es2sim's environment strings, one
 * line per operation with the frame counter, the end line at the exit
 * record. An operation names the function as the heap saw it: DBSIM's
 * imports "import" (calloc, free, realloc; new and delete reach the heap
 * through 0x47747c's calloc and _free), CW3220's own blocks (the file
 * service) "internal". A line that cannot be written is counted as lost. */
#define HEAP_TRACE_BUFFER 65536u
static char heap_trace_buffer[HEAP_TRACE_BUFFER];
static uint32_t heap_trace_used, heap_trace_regions;
static HANDLE heap_trace_file = INVALID_HANDLE_VALUE;
static uint64_t heap_trace_ops, heap_trace_lost;
static int heap_trace_flush(void) {
    if (heap_trace_file == INVALID_HANDLE_VALUE) return 1;
    DWORD written = 0;
    int bad = heap_trace_used && (!WriteFile(heap_trace_file, heap_trace_buffer, heap_trace_used, &written, NULL) ||
                                  written != heap_trace_used);
    heap_trace_used = 0;
    if (bad) { CloseHandle(heap_trace_file); heap_trace_file = INVALID_HANDLE_VALUE; }
    return bad;
}
static int heap_trace_line(const char *format, ...) __attribute__((format(printf, 1, 2)));
static int heap_trace_line(const char *format, ...) {
    if (heap_trace_file == INVALID_HANDLE_VALUE) return 1;
    if (HEAP_TRACE_BUFFER - heap_trace_used < 1024u && heap_trace_flush()) return 1;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(heap_trace_buffer + heap_trace_used, HEAP_TRACE_BUFFER - heap_trace_used, format, args);
    va_end(args);
    if (n <= 0 || (uint32_t)n >= HEAP_TRACE_BUFFER - heap_trace_used) return 1;
    heap_trace_used += (uint32_t)n;
    return 0;
}
static uint32_t heap_trace_read(void *context, uint32_t address) {
    const es2_guest_memory *m = context;
    return m->read32(m->context, address);
}
static uint32_t heap_trace_frame(const es2_sim *sim) { return sim->memory.read32(sim->memory.context, ES2_SIM_VA_FRAME_COUNTER); }
/* The regions, after a change of their number (the proxy names the OS
 * calls in reserve and release fields; es2sim's heap only counts them). */
static int heap_trace_region_list(const es2_heap *h) {
    int bad = heap_trace_line("\"regions\":[");
    for (uint32_t i = 0; !bad && i < h->region_count && i < ES2_HEAP_REGION_LIMIT; ++i)
        bad = heap_trace_line("%s[\"%08lx\",%lu]", i ? "," : "", (unsigned long)h->region_base[i], (unsigned long)h->region_size[i]);
    return bad || heap_trace_line("]");
}
static void heap_trace_op(void *user, es2_sim_heap_function function, int internal, uint32_t caller, uint32_t a0, uint32_t a1,
                          uint32_t result) {
    static const char *const names[] = {"malloc", "calloc", "free", "realloc"};
    const es2_sim *sim = user;
    const es2_heap *h = &sim->heap.heap;
    uint64_t index = heap_trace_ops++;
    int bad = heap_trace_line("{\"op\":%llu,\"frame\":%lu,\"fn\":\"%s\",\"from\":\"%s\",\"caller\":\"%08lx\",\"a0\":\"%08lx\","
                              "\"a1\":\"%08lx\",\"result\":\"%08lx\",\"rover\":\"%08lx\"",
                              (unsigned long long)index, (unsigned long)heap_trace_frame(sim), names[function],
                              internal ? "internal" : "import", (unsigned long)caller, (unsigned long)a0, (unsigned long)a1,
                              (unsigned long)result, (unsigned long)h->rover);
    if (!bad && h->region_count != heap_trace_regions) {
        heap_trace_regions = h->region_count;
        bad = heap_trace_line(",") || heap_trace_region_list(h);
    }
    if (bad || heap_trace_line("}\n")) ++heap_trace_lost;
}
static int heap_trace_open(es2_sim *sim, const wchar_t *config) {
    wchar_t path[MAX_PATH];
    if (!config || !*config) return 0;
    DWORD n = GetPrivateProfileStringW(L"trace", L"heap_trace", L"", path, MAX_PATH, config);
    if (!n) return 0;
    if (n >= MAX_PATH - 1) return 1;
    heap_trace_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (heap_trace_file == INVALID_HANDLE_VALUE) return 1;
    heap_trace_used = 0;
    heap_trace_ops = heap_trace_lost = 0;
    const es2_heap *h = &sim->heap.heap;
    heap_trace_regions = h->region_count;
    int32_t blocks = es2_heap_walk(h, heap_trace_read, &sim->memory, NULL, NULL);
    int bad = heap_trace_line("{\"kind\":\"attach\",\"schema\":1,\"frame\":%lu,\"sentinel\":\"%08lx\",\"rover\":\"%08lx\","
                              "\"committed\":\"%08lx\",\"reserved\":\"%08lx\",\"granularity\":%lu,\"platform\":%lu,\"blocks\":%ld,",
                              (unsigned long)heap_trace_frame(sim), (unsigned long)h->sentinel, (unsigned long)h->rover,
                              (unsigned long)h->committed_end, (unsigned long)h->reserved_end, (unsigned long)h->granularity,
                              (unsigned long)h->platform, (long)blocks) ||
              heap_trace_region_list(h) || heap_trace_line(",\"env_lengths\":[");
    /* The guest's environment: the sizes only (no content). */
    char *environment = guest_environment();
    unsigned strings = 0;
    for (const char *e = environment; !bad && e && *e; e += strlen(e) + 1u, ++strings)
        bad = heap_trace_line("%s%lu", strings ? "," : "", (unsigned long)strlen(e));
    free(environment);
    bad = bad || heap_trace_line("]}\n");
    for (uint32_t i = 0; !bad && i < h->region_count && i < ES2_HEAP_REGION_LIMIT; ++i) {
        uint32_t end = h->region_base[i] + h->region_size[i];
        for (uint32_t p = h->region_base[i] + 8u; !bad && p < end;) {
            uint32_t header = heap_trace_read(&sim->memory, p - 4u);
            if ((header & ~1u) < 16u) break;
            bad = heap_trace_line("{\"kind\":\"block\",\"p\":\"%08lx\",\"size\":%lu,\"used\":%u}\n", (unsigned long)p,
                                  (unsigned long)(header & ~1u), (unsigned)(header & 1u));
            p += header & ~1u;
        }
    }
    if (bad || heap_trace_flush()) {
        if (heap_trace_file != INVALID_HANDLE_VALUE) CloseHandle(heap_trace_file);
        heap_trace_file = INVALID_HANDLE_VALUE;
        return 1;
    }
    sim->heap.observe = heap_trace_op;
    sim->heap.observe_user = sim;
    return 0;
}
static void heap_trace_close(es2_sim *sim) {
    if (heap_trace_file == INVALID_HANDLE_VALUE) return;
    sim->heap.observe = NULL;
    if (heap_trace_line("{\"kind\":\"end\",\"ops\":%llu,\"lost\":%llu}\n", (unsigned long long)heap_trace_ops,
                        (unsigned long long)heap_trace_lost) || heap_trace_flush())
        fputs("es2sim: the heap trace could not be completed\n", stderr);
    if (heap_trace_file != INVALID_HANDLE_VALUE) CloseHandle(heap_trace_file);
    heap_trace_file = INVALID_HANDLE_VALUE;
}

int es2_sim_setup(es2_sim *sim, const wchar_t *config, const wchar_t *log) {
    es2_sim_identity_memory(&sim->memory);
    if (read_watches(sim, config)) {
        es2_mem_watch_clear(&sim->watches);
        return host_fail(sim, ES2_SIM_EXIT_SETUP, "[trace] watch refused");
    }
    /* Virtual time: [time] of the proxy's INI when given, else the recorded
     * suites' parameters. */
    UINT step = ES2_SIM_VT_STEP, start = ES2_VT_DEFAULT_START, stall = ES2_VT_DEFAULT_STALL_LIMIT;
    int intra = ES2_SIM_VT_INTRA;
    if (config && *config) {
        step = GetPrivateProfileIntW(L"time", L"virtual_time_step_ms", (INT)step, config);
        start = GetPrivateProfileIntW(L"time", L"virtual_time_start_ms", (INT)start, config);
        stall = GetPrivateProfileIntW(L"time", L"virtual_time_stall_limit", (INT)stall, config);
        intra = GetPrivateProfileIntW(L"time", L"virtual_time_intra_frame", intra, config) != 0;
    }
    memset(&sim->vt, 0, sizeof(sim->vt));
    es2_vt_enable(&sim->vt, 1, step, start, stall, intra);
    if (!sim->vt.enabled) return host_fail(sim, ES2_SIM_EXIT_SETUP, "virtual time step below 40 ms");
    /* The heap: own reservations, preferred base first. */
    es2_sim_heap_os os;
    es2_sim_heap_os_win32(&os);
    memset(sim->heap_head, 0, sizeof(sim->heap_head));
    es2_sim_heap_init(&sim->heap, &sim->memory, &os, (uint32_t)(uintptr_t)&sim->heap_head[2], es2_sim_heap_platform(),
                      ES2_SIM_HEAP_PREFERRED_BASE, ES2_SIM_HEAP_PREFERRED_SIZE);
    sim->services.memory = &sim->memory;
    sim->services.heap = &sim->heap;
    sim->services.vt = &sim->vt;
    sim->services.fs0 = NULL;
    /* The dispatch: services, then the registry's adapters; sealed sorted. */
    es2_sim_dispatch_init(&sim->dispatch, sim->entries, ES2_SIM_MAX_ENTRIES, host_refuse, sim);
    sim->dispatch.import_slots = sim->import_slots;
    sim->dispatch.import_names = sim->import_names;
    sim->dispatch.import_count = sim->import_count;
    if (es2_sim_register_services(&sim->dispatch, &sim->services) ||
        es2_sim_register_adapters(&sim->dispatch, sim->info.image_base))
        return host_fail(sim, ES2_SIM_EXIT_SETUP, "dispatch table full");
    es2_sim_adapters_set_user(sim->image);
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&sim->dispatch, &error, &bad)) {
        static char text[128];
        snprintf(text, sizeof(text), "dispatch: %s at 0x%08lx", error, (unsigned long)bad);
        return host_fail(sim, ES2_SIM_EXIT_SETUP, text);
    }
    if (es2_sim_enable_dep(&sim->dep_flags, &sim->dep_permanent)) return host_fail(sim, ES2_SIM_EXIT_SETUP, "DEP");
    es2_sim_trap_range ranges[2] = {
        {sim->info.code_lo, sim->info.code_hi, NULL, 0},
        {(uint32_t)(uintptr_t)sim->import_traps, (uint32_t)(uintptr_t)sim->import_traps + 4u * sim->import_count,
         sim->import_slots, 4u},
    };
    if (es2_sim_trap_install(&sim->dispatch, ranges, sim->import_count ? 2u : 1u))
        return host_fail(sim, ES2_SIM_EXIT_SETUP, "trap");
    if (es2_sim_bridge_set(host_bridged_vas, (unsigned)(sizeof(host_bridged_vas) / sizeof(host_bridged_vas[0]))))
        return host_fail(sim, ES2_SIM_EXIT_SETUP, "bridge");
    if (log && *log) {
        HANDLE h = CreateFileW(log, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return host_fail(sim, ES2_SIM_EXIT_SETUP, "tick log (CREATE_NEW)");
        sim->log = h;
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        es2_tick_start s = {2u, "own_process_headless", (unsigned long)GetCurrentProcessId(),
                            (unsigned long)GetCurrentThreadId(), sim->info.image_base, ES2_SIM_VA_FRAME_BOUNDARY,
                            (long long)frequency.QuadPart, 1000000ul, 0, sim->watches.count};
        char line[512];
        log_write(sim, line, es2_tick_record_start(line, sizeof(line), &s));
    }
    return 0;
}

int es2_sim_runtime_attach(es2_sim *sim, es2_sim_files *files, uint32_t region, const wchar_t *config) {
    char *environment = guest_environment();
    es2_sim_heap_copy_environment(&sim->heap, environment);
    free(environment);
    if (es2_sim_files_init(files, &sim->memory, region, &sim->heap))
        return host_fail(sim, ES2_SIM_EXIT_SETUP, "file services (runtime region)");
    es2_sim_heap_static_objects(&sim->heap);
    if (heap_trace_open(sim, config)) return host_fail(sim, ES2_SIM_EXIT_SETUP, "[trace] heap_trace (CREATE_NEW)");
    return 0;
}

/* A guest range is readable when every page is committed and accessible
 * (the proxy's tick_log_readable). */
static int readable(uint32_t address, uint32_t size) {
    uintptr_t cursor = address, end = (uintptr_t)address + size;
    if (!address || end < cursor) return 0;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery((void *)cursor, &region, sizeof(region)) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return 0;
        uintptr_t next = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (next <= cursor) return 0;
        cursor = next;
    }
    return 1;
}

/* The watches' access. The guest is mapped at its own addresses (identity
 * memory); a range is copied only when all of it is readable now, and the live
 * bytes are read at the address the probe accepted. */
static int watch_copy(void *user, void *out, uint32_t address, uint32_t size) {
    es2_sim *sim = user;
    if (!readable(address, size)) return 0;
    memcpy(out, sim->memory.host(sim->memory.context, address), size);
    return 1;
}
static const uint8_t *watch_view(void *user, uint32_t address, uint32_t size) {
    es2_sim *sim = user;
    (void)size;
    return sim->memory.host(sim->memory.context, address);
}
static void watch_emit(void *user, const char *text, int length) { log_write(user, text, length); }

void es2_sim_tick(es2_sim *sim, const es2_journal *journal) {
    if (!sim->log) return;
    static char line[4096];
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    const uint8_t *context = (const uint8_t *)(uintptr_t)ES2_SIM_VA_TICK_CONTEXT;
    const uint8_t *clock = (const uint8_t *)(uintptr_t)ES2_SIM_VA_TICK_CLOCK;
    ++sim->ticks;
    int n = es2_tick_record_head(line, sizeof(line), 0, sim->ticks, (long long)qpc.QuadPart, ES2_SIM_VA_TICK_CONTEXT,
                                 context, clock);
    n += es2_tick_record_rng(line + n, sizeof(line) - (size_t)n, 0, (const uint8_t *)(uintptr_t)0x004d2599u, 0, 0, NULL);
    n += es2_tick_record_rng(line + n, sizeof(line) - (size_t)n, 1, (const uint8_t *)(uintptr_t)0x004d260bu, 0, 0, NULL);
    uint32_t actor = sim->memory.read32(sim->memory.context, ES2_SIM_VA_TICK_CONTEXT + 0x2au);
    n += es2_tick_record_actor(line + n, sizeof(line) - (size_t)n, actor,
                               readable(actor, ES2_TICK_ACTOR_SIZE) ? (const uint8_t *)(uintptr_t)actor : NULL, 0);
    if (sim->vt.enabled)
        n += es2_tick_record_vt(line + n, sizeof(line) - (size_t)n, sim->vt.intra, (unsigned long)sim->vt.limiter_polls,
                                (unsigned long)sim->vt.stall_advances, (unsigned long)sim->vt.late_polls,
                                (unsigned long)sim->vt.intra_advances);
    if (journal)
        n += es2_tick_record_input(line + n, sizeof(line) - (size_t)n, 1, (unsigned long)journal->recorded,
                                   (unsigned long)journal->injected, (unsigned long)journal->dropped,
                                   (unsigned long)journal->fallbacks, (unsigned long)journal->focus_dropped);
    n += snprintf(line + n, sizeof(line) - (size_t)n, "}\r\n");
    log_write(sim, line, n);
    if (sim->watches.count) {
        static char watch_line[4u * ES2_MEM_WATCH_MAX_BYTES + 1024u];   /* the proxy's TICK_LOG_WATCH_LINE_MAX */
        es2_mem_watch_access access = {sim, watch_copy, watch_view, watch_emit, NULL, NULL};
        es2_mem_watch_check(&sim->watches, sim->ticks,
                            (unsigned long)sim->memory.read32(sim->memory.context, ES2_SIM_VA_TICK_CONTEXT + 0xbfu),
                            &access, watch_line, sizeof(watch_line));
    }
}

uint32_t es2_sim_exit_code(const es2_sim *sim) { return sim->memory.read32(sim->memory.context, ES2_SIM_VA_EXIT_CODE); }

void es2_sim_finish(es2_sim *sim, const char *reason, int exit_code) {
    heap_trace_close(sim);
    if (!sim->log) return;
    unsigned hooks = 0, services = 0;
    for (unsigned i = 0; i < sim->dispatch.count; ++i) {
        if (sim->entries[i].kind == ES2_SIM_HOOK) ++hooks;
        else if (sim->entries[i].kind == ES2_SIM_SERVICE) ++services;
    }
    char line[1024];
    int n = snprintf(line, sizeof(line),
        "{\"event\":\"exit\",\"reason\":\"%s\",\"exit_code\":%lu,\"frames\":%lu,\"hooks\":%u,\"services\":%u,"
        "\"imports\":%u,\"trapped_calls\":%llu,\"refusals\":%llu,\"heap_regions\":%lu,\"heap_os_calls\":%llu,"
        "\"vt_limiter_polls\":%lu,\"vt_stall_advances\":%lu,\"dep_flags\":%lu,\"calls\":[",
        reason, (unsigned long)(uint32_t)exit_code, sim->ticks, hooks, services, sim->import_count, (unsigned long long)es2_sim_trap_calls(),
        (unsigned long long)sim->dispatch.refusals, (unsigned long)sim->heap.heap.region_count,
        (unsigned long long)sim->heap.os_calls, (unsigned long)sim->vt.limiter_polls,
        (unsigned long)sim->vt.stall_advances, sim->dep_flags);
    log_write(sim, line, n);
    int first = 1;
    for (unsigned i = 0; i < sim->dispatch.count; ++i) {
        if (!sim->entries[i].calls) continue;
        n = snprintf(line, sizeof(line), "%s[\"0x%08lx\",%llu]", first ? "" : ",", (unsigned long)sim->entries[i].va,
                     (unsigned long long)sim->entries[i].calls);
        log_write(sim, line, n);
        first = 0;
    }
    log_write(sim, "]}\r\n", 4);
    CloseHandle((HANDLE)sim->log);
    sim->log = NULL;
}
