/* The own cw3220.dll (rtl/) against the original CW3220.DLL, in vitro.
 *
 * The own DLL (ES2_OWN_CW3220: its runfiles path, from //rtl:cw3220.dll) is
 * loaded into the harness; its DllMain runs as in any game process but
 * ES.EXE and VSHELL.EXE (own mode). CW3220.DLL is mapped as in rtl_cases.h
 * (an SHA-256 gate, relocations, every import a named trap, no
 * entry point). The DLL's descriptor (es2_own_runtime) gives its state; its
 * `reinitialize` reruns the attach-time initialisation after the harness
 * pointed the DLL's KERNEL32/USER32 import slots at the pass's emulated OS.
 * The test stack is the owner thread's stack, so the calls take the DLL's
 * private-stack adapters as DBSIM's calls do.
 *
 * Tables: __ctype with the tolower table below it, the DOS error map, the
 * scanner classes, the data exports, and the static I/O state against the
 * original's bytes as linked. Pure (rtl_cases.h's generator): the 20 memory,
 * string and formatting exports from the same state: EAX/ECX/EDX, arena,
 * argument slots and the stack below (all of it for the leaf functions, whose
 * pushed registers the DLL writes; for the others the DLL writes nothing
 * there); sprintf/vsprintf calls with a %p or %n conversion counted (outside
 * the formatter); the same cases from a foreign stack (the caller's-stack
 * path). Heap (rtl_heap_cases.h's replaced sequences): calloc/realloc/free
 * through DBSIM's thunks on the DLL's heap over the emulated OS and with the
 * original's head-block address: results, OS calls, modelled registers,
 * variables, window bytes, nothing below the return address. I/O
 * (rtl_io_cases.h's generator and emulated OS): sequences over the 21 file and
 * console exports on two worlds that swap through the shared memory (CW3220's
 * data, arena, OS state, heap window): the original's, started by CW3220's init
 * records (heap 4170fc, handles 409014, streams 407248, _tzset 41e268 over a
 * generated environment), and the DLL's, started by its reinitialize
 * (es2_io_startup, es2_io_tzset). After the start-up and after every step: EAX,
 * the OS calls, OS state, arena and window bytes and the I/O and heap variables
 * (FILE pointers, tokens and hook values translated between the two runtimes);
 * the console stubs by EAX (the DLL writes nothing). At the end the stream
 * hooks exit runs (407ca4, 407c7c). Lifecycle (rtl_start_cases.h's switched
 * comparison): __startup, _atexit, __exit, _abort and __ErrorExit of the DLL in
 * DBSIM's slots against the original: events, registers at every guest and
 * Win32 call, stack words, table bytes and the runtime variables (the DLL's
 * frame handler, window callback, abort text and dummy hook mapped to
 * CW3220's). Exceptions: __ExceptionHandler on every context entry of DBSIM's
 * frame tables (the callers of 49630c) for exceptions other than C++ throws;
 * also for a C++ throw in the search phase (entries without a catch) and for an
 * unwind with nothing constructed (entries whose clean-up calls nothing; the
 * CW3220 helpers that would call DBSIM code are guarded by int3). Startup
 * handler: the handler of __startup's frame record against 405160 with both
 * runtimes' UnhandledExceptionFilter slots at a stub: CPU and random codes,
 * unwind flags, the filter's answers, the _signal words at 0 or 405154;
 * disposition, one filter call with the given record and context, nothing
 * written. Foreign: _atexit from another thread against the original's, and
 * @_CatchCleanup$qv without a context block and with an empty list. Selection:
 * --rtl-own. */
#include "es2/rtl/own_runtime.h"

/* ------------------------------------------------------------------------
 * The DLL, its import slots and DBSIM's slots. */
static HMODULE ro_dll;
static const es2_own_runtime_info *ro_info;
static const char *ro_runfile(const char *path) {
    static char out[MAX_PATH];
    const char *manifest = getenv("RUNFILES_MANIFEST_FILE");
    FILE *stream = manifest && *manifest ? fopen(manifest, "rb") : NULL;
    if (!stream) return path;
    char line[4096], key[MAX_PATH + 8];
    snprintf(key, sizeof(key), "_main/%s ", path);
    size_t n = strlen(key);
    const char *found = path;
    while (fgets(line, sizeof(line), stream)) {
        if (strncmp(line, key, n)) continue;
        snprintf(out, sizeof(out), "%s", line + n);
        out[strcspn(out, "\r\n")] = 0;
        for (char *p = out; *p; ++p) if (*p == '/') *p = '\\';
        found = out;
        break;
    }
    fclose(stream);
    return found;
}
static uint32_t ro_export(const char *name) { return (uint32_t)(uintptr_t)GetProcAddress(ro_dll, name); }
static int ro_load_dll(void) {
    const char *name = getenv("ES2_OWN_CW3220");
    if (!name || !*name) return fail("ES2_OWN_CW3220 (the own cw3220.dll) is not set");
    const char *path = ro_runfile(name);
    ro_dll = LoadLibraryA(path);
    if (!ro_dll) { fprintf(stderr, "FAIL: %s: cannot load %s (error %lu)\n", current_function, path, GetLastError()); return 1; }
    es2_own_runtime_fn describe = (es2_own_runtime_fn)(void *)GetProcAddress(ro_dll, ES2_OWN_RUNTIME_EXPORT);
    if (!describe) return fail("es2_own_runtime export");
    ro_info = describe();
    if (ro_info->version != ES2_OWN_RUNTIME_VERSION || ro_info->mode != ES2_OWN_MODE_OWN) return fail("own runtime version or mode");
    printf("Loaded the own cw3220.dll at 0x%08x in own mode\n", (unsigned)ro_info->image_base);
    return 0;
}

/* The DLL's import slots by DLL and function name. */
typedef struct ro_patch { uint32_t *slot; uint32_t saved; } ro_patch;
static ro_patch ro_patches[64];
static unsigned ro_patch_count;
static uint32_t *ro_slot(const char *dll, const char *name) {
    const BYTE *image = (const BYTE *)ro_dll;
    uint32_t pe = get32(image + 0x3c), imports = get32(image + pe + 24 + 96 + 8);
    for (uint32_t d = imports; get32(image + d + 12); d += 20) {
        if (_stricmp((const char *)image + get32(image + d + 12), dll)) continue;
        uint32_t lookup = get32(image + d), iat = get32(image + d + 16);
        if (!lookup) lookup = iat;
        for (unsigned i = 0; get32(image + lookup + 4 * i); ++i) {
            uint32_t entry = get32(image + lookup + 4 * i);
            if (!(entry & 0x80000000u) && !strcmp((const char *)image + entry + 2, name)) return (uint32_t *)(uintptr_t)(image + iat + 4 * i);
        }
    }
    return NULL;
}
static int ro_point(const char *dll, const char *name, const void *target) {
    uint32_t *slot = ro_slot(dll, name);
    if (!slot) { fprintf(stderr, "FAIL: %s: the own DLL does not import %s!%s\n", current_function, dll, name); return 1; }
    if (ro_patch_count >= sizeof(ro_patches) / sizeof(ro_patches[0])) return fail("own import patch table");
    DWORD old;
    if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) return fail("own import slot protection");
    ro_patches[ro_patch_count].slot = slot;
    ro_patches[ro_patch_count++].saved = *slot;
    *slot = (uint32_t)(uintptr_t)target;
    return 0;
}
static void ro_unpoint(void) {
    while (ro_patch_count) { const ro_patch *p = &ro_patches[--ro_patch_count]; *p->slot = p->saved; }
}

/* DBSIM's 53 CW3220 slots at the original's exports (0) or the DLL's (1). */
static int ro_dbsim_slots(int own) {
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        uint32_t target = own ? ro_export(rtl_imports[i].symbol) : rtl_cw_export(rtl_imports[i].symbol);
        if (!target) { fprintf(stderr, "FAIL: %s: no %s export %s\n", current_function, own ? "own" : "CW3220", rtl_imports[i].symbol); return 1; }
        *(uint32_t *)(mapped + RTL_IAT_RVA + 4 * i) = target;
    }
    return 0;
}

#define RO_STACK_LO ((uint32_t)(uintptr_t)rtl_stack)
#define RO_STACK_HI ((uint32_t)(uintptr_t)(rtl_stack + RTL_STACK_DWORDS))
static int ro_reinit(uint32_t sentinel, int32_t platform, uint32_t environment, uint32_t code_base, uint32_t lo, uint32_t hi, uint32_t flags) {
    es2_own_test_setup s;
    memset(&s, 0, sizeof(s));
    s.sentinel = sentinel; s.platform = platform; s.environment = environment; s.code_base = code_base;
    s.stack_lo = lo; s.stack_hi = hi; s.flags = flags;
    if (ro_info->reinitialize(&s)) return fail("the own runtime refused reinitialize");
    return 0;
}
static uint32_t ro_word(uint32_t address) { return *(const volatile uint32_t *)(uintptr_t)address; }
static int ro_bytes(const char *what, uint32_t own, uint32_t original, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) {
        uint8_t a = *(const volatile uint8_t *)(uintptr_t)(own + i), b = *(const volatile uint8_t *)(uintptr_t)(original + i);
        if (a != b) { fprintf(stderr, "FAIL: %s: %s byte +0x%x own %02x original %02x\n", current_function, what, (unsigned)i, a, b); return 1; }
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * Translation between the two runtimes' addresses. */
#define RO_CW_STREAMS 0x1f830u
#define RO_CW_EXITBUF 0x7ca4u
#define RO_CW_EXITFOPEN 0x7c7cu
static uint32_t ro_cw_streams(void) { return (uint32_t)(uintptr_t)rtl_cw + RO_CW_STREAMS; }
static uint32_t ro_to_own(uint32_t v) {
    uint32_t o = v - ro_cw_streams();
    return o < ES2_IO_NFILE * 0x18u ? ro_info->io->streams + o : v;
}
static uint32_t ro_to_cw(uint32_t v) {
    uint32_t o = v - ro_info->io->streams;
    return o < ES2_IO_NFILE * 0x18u ? ro_cw_streams() + o : v;
}
static uint32_t ro_hook_to_cw(uint32_t v) {
    uint32_t base = (uint32_t)(uintptr_t)rtl_cw;
    if (v == ro_info->io->exit_buffers_value) return base + RO_CW_EXITBUF;
    if (v == ro_info->io->exit_files_value) return base + RO_CW_EXITFOPEN;
    if (v == ro_info->dummy_hook) return base + LS_DUMMY_HOOK;
    return v;
}
/* One FILE of each runtime (0x18 bytes: curp, buffer, level, bsize, istemp,
 * flags, token, fd, hold): pointers into the FILE array translated, the
 * token (the FILE's low address word) relative to the FILE. */
static uint32_t ro_token(uint32_t token, uint32_t fp) { return token ? (uint32_t)(uint16_t)(token - fp) : 0x10000u; }
static const char *ro_file_differs(const BYTE *own, uint32_t own_fp, const BYTE *cw, uint32_t cw_fp) {
    if (ro_to_cw(get32(own)) != get32(cw)) return "curp";
    if (ro_to_cw(get32(own + 4)) != get32(cw + 4)) return "buffer";
    if (get32(own + 8) != get32(cw + 8)) return "level";
    if (get32(own + 0xc) != get32(cw + 0xc)) return "bsize";
    if (get16(own + 0x10) != get16(cw + 0x10)) return "istemp";
    if (get16(own + 0x12) != get16(cw + 0x12)) return "flags";
    if (ro_token(get16(own + 0x14), own_fp) != ro_token(get16(cw + 0x14), cw_fp)) return "token";
    if (own[0x16] != cw[0x16]) return "fd";
    if (own[0x17] != cw[0x17]) return "hold";
    return NULL;
}

/* ------------------------------------------------------------------------
 * Tables and the static I/O state. */
static int ro_tables(void) {
    current_function = "rtl_own_tables";
    if (ro_reinit(0, 0, 0, 0, RO_STACK_LO, RO_STACK_HI, ES2_OWN_SETUP_NO_IO_START)) return 1;
    const es2_io_layout *io = ro_info->io;
    es2_io_layout cw;
    es2_io_layout_cw3220(&cw, (uint32_t)(uintptr_t)rtl_cw);
    uint32_t base = (uint32_t)(uintptr_t)rtl_cw;
    if (ro_export("__streams") != io->streams || ro_export("_errno") != io->errno_word || ro_export("__ctype") != ro_info->ctype_block + 0x100u)
        return fail("own data export addresses");
    if (rtl_cw_export("__streams") != cw.streams || rtl_cw_export("_errno") != cw.errno_word || rtl_cw_export("__ctype") != base + 0x218b4u)
        return fail("CW3220 data export addresses");
    if (ro_bytes("tolower and __ctype tables (4217b4, 0x201 bytes)", ro_info->ctype_block, base + 0x217b4u, 0x201u) ||
        ro_bytes("DOS error map (420358)", io->error_map, cw.error_map, ES2_IO_ERROR_MAP_SIZE) ||
        ro_bytes("scanner classes (420244)", io->scan_class, cw.scan_class, ES2_IO_SCAN_CLASSES)) return 1;
    for (unsigned b = 0; b < 256u; ++b)
        if (es2_io_ctype((uint8_t)b) != rtl_cw[0x218b5u + (int8_t)b]) return fail("es2_io_ctype against the signed read of 4218b4");
    /* The static state against CW3220's data as linked. */
    uint32_t checked = 0;
    static const struct { const char *name; uint32_t rva; size_t offset; } words[] = {
        {"_nfile", 0x1fce0u, offsetof(es2_io_layout, nfile)}, {"errno", 0x22248u, offsetof(es2_io_layout, errno_word)},
        {"_doserrno", 0x235d0u, offsetof(es2_io_layout, doserrno)}, {"_sys_nerr", 0x1fe8cu, offsetof(es2_io_layout, error_count)},
        {"umask", 0x20354u, offsetof(es2_io_layout, umask_word)}, {"find list", 0x235ccu, offsetof(es2_io_layout, find_list)},
        {"_timezone", 0x23070u, offsetof(es2_io_layout, time_zone)}, {"_daylight", 0x23074u, offsetof(es2_io_layout, summer_time)},
    };
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); ++i, ++checked) {
        uint32_t own = ro_word(*(const uint32_t *)((const BYTE *)io + words[i].offset)), original = get32(io_data_pristine + words[i].rva - IO_DATA_RVA);
        if (own != original) { fprintf(stderr, "FAIL: %s: %s own %08x original %08x\n", current_function, words[i].name, own, original); return 1; }
    }
    if (*(const volatile uint8_t *)(uintptr_t)io->getc_byte != io_data_pristine[0x235b0u - IO_DATA_RVA]) return fail("getc byte");
    /* _fmode: the module's word replaces it at __startup; until then both point to a default 0x4000. */
    if (ro_word(ro_word(io->fmode_pointer)) != ro_word(get32(io_data_pristine + 0x1fdb0u - IO_DATA_RVA))) return fail("the default _fmode word");
    for (uint32_t fd = 0; fd < ES2_IO_NFILE; ++fd, checked += 2) {
        if (ro_word(io->openfd + 4u * fd) != get32(io_data_pristine + 0x1fce4u - IO_DATA_RVA + 4u * fd)) return fail("handle flags as linked");
        if (ro_word(io->handles + 4u * fd) != get32(io_data_pristine + 0x234e8u - IO_DATA_RVA + 4u * fd)) return fail("handles as linked");
    }
    for (uint32_t i = 0; i < ES2_IO_NFILE; ++i, ++checked) {
        uint32_t own_fp = io->streams + 0x18u * i, cw_fp = ro_cw_streams() + 0x18u * i;
        const char *what = ro_file_differs((const BYTE *)(uintptr_t)own_fp, own_fp, io_data_pristine + RO_CW_STREAMS - IO_DATA_RVA + 0x18u * i, cw_fp);
        if (what) { fprintf(stderr, "FAIL: %s: FILE %u %s differs from CW3220's as linked\n", current_function, (unsigned)i, what); return 1; }
    }
    /* The stream hooks: CW3220's bare `ret` (4191a0) as linked, the DLL's own. */
    const uint32_t *hooks = (const uint32_t *)(uintptr_t)ro_info->exit_hooks;
    for (unsigned k = 0; k < 3; ++k, ++checked)
        if (hooks[k] != ro_info->dummy_hook || ro_hook_to_cw(hooks[k]) != get32(io_data_pristine + 0x222bcu - IO_DATA_RVA + 4u * k))
            return fail("stream hooks as linked");
    if (*(const uint8_t *)(uintptr_t)ro_info->dummy_hook != 0xc3u || rtl_cw[LS_DUMMY_HOOK] != 0xc3u) return fail("the dummy hooks are not a bare ret");
    printf("PASS: rtl_own tables: tolower/__ctype (0x201 bytes), DOS error map (0x%x), scanner classes (0x%x) equal to CW3220's; "
           "es2_io_ctype = the signed read; data exports __streams, _errno, __ctype; %u words of the static I/O state equal to "
           "CW3220's data as linked\n", ES2_IO_ERROR_MAP_SIZE, ES2_IO_SCAN_CLASSES, (unsigned)checked);
    return 0;
}

/* ------------------------------------------------------------------------
 * Pure: the 20 memory, string and formatting exports. */
static unsigned ro_pure_counts[ES2_RTL_FUNCTION_COUNT][2], ro_pure_empty[ES2_RTL_FUNCTION_COUNT], ro_pure_outside[ES2_RTL_FUNCTION_COUNT];
static unsigned ro_pure_empty_routine;
static int ro_pure_fail(const char *f, const char *what, const rtl_result *want, const rtl_result *got) {
    fprintf(stderr, "FAIL: rtl_own_pure %s case %u: %s; EAX/ECX/EDX original %08x %08x %08x own %08x %08x %08x\n", f, current_case, what,
            want->frame.out_eax, want->frame.out_ecx, want->frame.out_edx, got->frame.out_eax, got->frame.out_ecx, got->frame.out_edx);
    return 1;
}
static int ro_pure(void) {
    current_function = "rtl_own_pure";
    BYTE *arena = VirtualAlloc(NULL, RTL_ARENA + 4096u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD previous;
    if (!arena || !VirtualProtect(arena + RTL_ARENA, 4096, PAGE_NOACCESS, &previous)) return fail("arena");
    rtl_arena = arena; rtl_pre = malloc(RTL_ARENA); rtl_post = malloc(RTL_ARENA);
    if (!rtl_pre || !rtl_post) return fail("arena copies");
    for (unsigned i = 0; i < RTL_BELOW; ++i) rtl_below_pattern[i] = (BYTE)random32();
    unsigned cases = quick_tier ? 4000u : 40000u, foreign = quick_tier ? 400u : 4000u;
    static BYTE below_want[RTL_BELOW];
    for (int f = 0; f < ES2_RTL_FUNCTION_COUNT; ++f) {
        const char *name = es2_rtl_name((es2_rtl_function)f);
        uint32_t original = rtl_cw_export(name), own = ro_export(name);
        if (!original || !own) return fail("pure export missing");
        for (unsigned pass = 0; pass < 2u; ++pass) {
            /* Pass 1: a stack outside the owner's (the caller's-stack path). */
            if (ro_reinit(0, 0, 0, 0, pass ? 0x1000u : RO_STACK_LO, pass ? 0x2000u : RO_STACK_HI, ES2_OWN_SETUP_NO_IO_START)) return 1;
            rtl_reseed(300u + 2u * (unsigned)f + pass);
            unsigned count = pass ? foreign : cases;
            for (unsigned n = 0; n < count; ++n) {
                rtl_case c;
                rtl_result want, got;
                current_case = n; current_case_valid = 1;
                do rtl_generate((es2_rtl_function)f, n, &c); while (c.has_float);
                memcpy(rtl_pre, rtl_arena, RTL_ARENA);
                if (rtl_run_target(original, &c, &want)) return 1;
                memcpy(rtl_post, rtl_arena, RTL_ARENA);
                memcpy(below_want, rtl_below_post, RTL_BELOW);
                uint64_t declined = ro_info->counters->declined;
                if (rtl_run_target(own, &c, &got)) return 1;
                if (ro_info->counters->declined != declined) {
                    if (f != ES2_RTL_SPRINTF && f != ES2_RTL_VSPRINTF) return ro_pure_fail(name, "declined", &want, &got);
                    if (rtl_format_outside(c.args[1] - rtl_at(0))) {
                        /* A %p or %n conversion: outside the formatter; the DLL writes
                         * dest[0] = 0 (as the original does first) and returns 0. */
                        if (got.frame.out_eax != 0) return ro_pure_fail(name, "declined call's EAX", &want, &got);
                        if (rtl_arena[c.dest] != 0) return ro_pure_fail(name, "declined call: dest[0]", &want, &got);
                        rtl_arena[c.dest] = rtl_pre[c.dest];
                        if (memcmp(rtl_arena, rtl_pre, RTL_ARENA)) return ro_pure_fail(name, "declined call wrote memory", &want, &got);
                        ++ro_pure_outside[f];
                        continue;
                    }
                    if (want.frame.out_eax != 0) return ro_pure_fail(name, "declined a supported format", &want, &got);
                    /* An empty result: compared like any case, but EDX: CW3220's formatter
                     * leaves residue there (its output routine 407164, -1, ...), which
                     * no caller reads (cdecl); the DLL leaves 0. */
                    if (got.frame.out_edx != 0) return ro_pure_fail(name, "EDX of an empty result", &want, &got);
                    if (want.frame.out_edx == (uint32_t)(uintptr_t)rtl_cw + 0x7164u) ++ro_pure_empty_routine;
                    got.frame.out_edx = want.frame.out_edx;
                    ++ro_pure_empty[f];
                }
                if (got.frame.out_eax != want.frame.out_eax || got.frame.out_ecx != want.frame.out_ecx || got.frame.out_edx != want.frame.out_edx)
                    return ro_pure_fail(name, "registers", &want, &got);
                for (uint32_t i = 0; i < RTL_ARENA; ++i)
                    if (rtl_arena[i] != rtl_post[i]) {
                        fprintf(stderr, "FAIL: rtl_own_pure %s case %u: arena byte +0x%x original %02x own %02x (EAX %08x/%08x, declined %d, pass %u)\n",
                                name, n, (unsigned)i, rtl_post[i], rtl_arena[i], want.frame.out_eax, got.frame.out_eax,
                                ro_info->counters->declined != declined, pass);
                        if (f == ES2_RTL_SPRINTF || f == ES2_RTL_VSPRINTF) {
                            uint32_t format = c.args[1] - rtl_at(0);
                            fprintf(stderr, "  dest +0x%x format +0x%x \"%.120s\" args", c.args[0] - rtl_at(0), format, format < RTL_ARENA ? (const char *)rtl_pre + format : "?");
                            for (unsigned k = 2; k < RTL_ARGS; ++k) fprintf(stderr, " %08x", c.args[k]);
                            fputc('\n', stderr);
                        }
                        return 1;
                    }
                if (memcmp(got.args, want.args, sizeof(want.args))) return ro_pure_fail(name, "argument slots", &want, &got);
                if (!pass) {
                    /* Below the arguments: the return slot; the leaf functions' pushed
                     * registers (the DLL writes them); nothing else from the DLL. */
                    const BYTE *expected = rtl_residue(f) ? below_want : rtl_below_pattern;
                    if (memcmp(rtl_below_post, expected, RTL_BELOW - 4u) || memcmp(rtl_below_post + RTL_BELOW - 4u, below_want + RTL_BELOW - 4u, 4u))
                        return ro_pure_fail(name, "stack below the arguments", &want, &got);
                }
                ++ro_pure_counts[f][pass];
            }
            /* reinitialize cleared the counters: every call took the path of its pass. */
            const es2_own_counters *k = ro_info->counters;
            if (k->calls != (pass ? 0u : count) || k->foreign_calls != (pass ? count : 0u) || k->nested_calls) return fail("adapter path counters");
        }
    }
    current_case_valid = 0;
    unsigned total = 0, total_foreign = 0, empty = 0, outside = 0;
    for (int f = 0; f < ES2_RTL_FUNCTION_COUNT; ++f) {
        total += ro_pure_counts[f][0]; total_foreign += ro_pure_counts[f][1]; empty += ro_pure_empty[f]; outside += ro_pure_outside[f];
        if (!ro_pure_counts[f][0] || !ro_pure_counts[f][1]) { fprintf(stderr, "FAIL: %s: no case for %s\n", current_function, es2_rtl_name((es2_rtl_function)f)); return 1; }
    }
    printf("PASS: rtl_own pure: 20 exports, %u cases on the owner stack (private-stack adapters) and %u from a foreign stack equal to "
           "CW3220's exports: EAX/ECX/EDX, arena, argument slots; below the arguments the leaf functions' pushed registers and "
           "nothing else; %u empty sprintf/vsprintf results equal but EDX (CW3220's formatter residue, %u times its output routine "
           "407164; the DLL 0); %u calls with %%p/%%n left out (outside the formatter; the DLL returns 0 with dest[0] = 0)\n",
           total, total_foreign, empty, ro_pure_empty_routine, outside);
    return 0;
}

/* ------------------------------------------------------------------------
 * Heap: calloc/realloc/free through DBSIM's thunks. */
static unsigned ro_heap_ops, ro_heap_null, ro_heap_os, ro_heap_registers;
static int ro_heap_sequence(unsigned steps, uint32_t platform, unsigned policy, uint32_t seed) {
    uint32_t start = seed | 1u;
    if (ro_dbsim_slots(0)) return 1;
    random_state = start;
    heap_reset(platform, policy, seed);
    heap_live_count = 0;
    for (unsigned step = 0; step < steps; ++step) {
        heap_op op = heap_generate_import();
        heap_log_count = 0;
        if (heap_call(heap_target(&op, 1), op.a0, op.a1)) return 1;
        heap_seen s = {heap_frame.out_eax, heap_frame.out_ecx, heap_frame.out_edx, heap_log_count, heap_os_hash()};
        heap_seen_plain[step] = s;
        heap_after(&op, s.eax);
    }
    es2_heap plain;
    heap_real_state(&plain);
    uint64_t plain_window = heap_window_hash();
    /* The DLL: the same operations and OS decisions, CW3220's head block. */
    random_state = start;
    heap_reset(platform, policy, seed);
    heap_live_count = 0;
    uint32_t sentinel = heap_cw(RTL_HEAP_SENTINEL);
    if (ro_point("KERNEL32.dll", "VirtualAlloc", (const void *)heap_os_valloc) || ro_point("KERNEL32.dll", "VirtualFree", (const void *)heap_os_vfree) ||
        ro_reinit(sentinel, (int32_t)platform, 0, 0, RO_STACK_LO, RO_STACK_HI, ES2_OWN_SETUP_NO_IO_START) || ro_dbsim_slots(1)) return 1;
    heap_log_count = 0;
    uint64_t calls = ro_info->counters->calls;
    for (unsigned step = 0; step < steps; ++step) {
        heap_op op = heap_generate_import();
        heap_log_count = 0;
        if (heap_call(heap_target(&op, 1), op.a0, op.a1)) return 1;
        const heap_seen *s = &heap_seen_plain[step];
        uint32_t total = op.a0 * op.a1;
        BOOL refused = op.kind == HEAP_OP_CALLOC && (!total || (int32_t)es2_heap_block_size(total) < 16);
        BOOL registers = op.kind == HEAP_OP_FREE || (op.kind == HEAP_OP_CALLOC && (s->eax || refused));
        const char *what = heap_frame.out_eax != s->eax ? "EAX" :
                           heap_log_count != s->os_count || heap_os_hash() != s->os ? "OS calls" :
                           op.kind == HEAP_OP_CALLOC && heap_frame.out_ecx != s->ecx ? "ECX" :
                           registers && (heap_frame.out_ecx != s->ecx || heap_frame.out_edx != s->edx) ? "ECX/EDX" :
                           memcmp((const BYTE *)(uintptr_t)(RTL_ARGS_ADDRESS - RTL_BELOW), rtl_below_pattern, RTL_BELOW - 4u) ? "stack below the return address" : NULL;
        if (what) {
            fprintf(stderr, "FAIL: %s case %u: step %u %s(%08x, %08x): %s: EAX/ECX/EDX %08x %08x %08x, original %08x %08x %08x\n",
                    current_function, heap_sequence, step, heap_op_names[op.kind], op.a0, op.a1, what,
                    heap_frame.out_eax, heap_frame.out_ecx, heap_frame.out_edx, s->eax, s->ecx, s->edx);
            return 1;
        }
        ++ro_heap_ops; ro_heap_os += heap_log_count;
        if (registers) ++ro_heap_registers;
        if (op.kind != HEAP_OP_FREE && !s->eax) ++ro_heap_null;
        heap_after(&op, heap_frame.out_eax);
    }
    const es2_heap *m = ro_info->heap;
    if (m->rover != plain.rover || m->region_count != plain.region_count || m->committed_end != plain.committed_end ||
        m->reserved_end != plain.reserved_end || m->granularity != plain.granularity || m->platform != plain.platform ||
        memcmp(m->region_base, plain.region_base, sizeof(m->region_base)) || memcmp(m->region_size, plain.region_size, sizeof(m->region_size)))
        return heap_case_fail("the DLL's heap variables differ from the original's");
    if (heap_window_hash() != plain_window) return heap_case_fail("window bytes differ from the original run");
    es2_heap untouched;
    heap_real_state(&untouched);
    if (untouched.region_count || untouched.rover != sentinel || untouched.committed_end || untouched.reserved_end || untouched.granularity)
        return heap_case_fail("CW3220's heap variables changed in the DLL's run");
    if (ro_info->counters->calls - calls != steps || ro_info->counters->foreign_calls || ro_info->counters->nested_calls)
        return heap_case_fail("the DLL's call counters");
    if (es2_heap_walk(ro_info->heap, heap_real_read, NULL, NULL, NULL) < 0) return heap_case_fail("the DLL's heap walk");
    ro_unpoint();
    return ro_dbsim_slots(0);
}
static int ro_heap(void) {
    current_function = "rtl_own_heap";
    heap_window = VirtualAlloc(NULL, HEAP_WINDOW, MEM_RESERVE, PAGE_NOACCESS);
    if (!heap_window) return fail("heap window reservation");
    uint32_t saved_slots[2] = {heap_word(heap_cw(RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VA_ENTRY)), heap_word(heap_cw(RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VF_ENTRY))};
    *(uint32_t *)(rtl_cw + RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VA_ENTRY) = (uint32_t)(uintptr_t)heap_os_valloc;
    *(uint32_t *)(rtl_cw + RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VF_ENTRY) = (uint32_t)(uintptr_t)heap_os_vfree;
    /* heap_reset also restarts the heap model, which needs its shadow env. */
    memset(&heap_shadow, 0, sizeof(heap_shadow));
    es2_heap_words_init(&heap_shadow.words, heap_keys, heap_values, HEAP_WORD_BITS);
    heap_shadow.journal = heap_journal;
    heap_shadow.journal_capacity = 4096;
    es2_heap_shadow_env(&heap_env, &heap_shadow);
    for (unsigned i = 0; i < RTL_BELOW; ++i) rtl_below_pattern[i] = (BYTE)random32();
    unsigned sequences = quick_tier ? 24u : 240u;
    static const uint32_t platforms[3] = {0, 1, 2};
    for (unsigned k = 0; k < sequences; ++k, ++heap_sequence) {
        current_case = heap_sequence; current_case_valid = 1;
        if (ro_heap_sequence(HEAP_STEPS_MAX, platforms[k % 3u], k % 3u == 0 ? HEAP_POLICY_FOLLOW : k % 3u == 1 ? HEAP_POLICY_GAPS : HEAP_POLICY_MIXED,
                             random32())) return 1;
    }
    current_case_valid = 0;
    *(uint32_t *)(rtl_cw + RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VA_ENTRY) = saved_slots[0];
    *(uint32_t *)(rtl_cw + RTL_HEAP_K32_IAT + 4u * RTL_HEAP_VF_ENTRY) = saved_slots[1];
    VirtualFree(heap_window, 0, MEM_RELEASE);
    heap_window = NULL;
    printf("PASS: rtl_own heap: %u sequences, %u operations (%u NULL results, %u OS calls, ECX/EDX compared in %u): calloc/realloc/free "
           "through DBSIM's thunks on the DLL's heap give the original's results, OS calls, allocator variables, window bytes and "
           "modelled registers; nothing below the return address; CW3220's heap untouched\n",
           sequences, ro_heap_ops, ro_heap_null, ro_heap_os, ro_heap_registers);
    return 0;
}

/* ------------------------------------------------------------------------
 * I/O: two worlds over the shared memory. */
#define RO_ENV 0x8000u               /* the environment block (arena) */
#define RO_ENVIRON 0x8800u           /* CW3220's char ** environment array (arena) */
#define RO_ENVIRON_RVA 0x23cfcu      /* the environment pointer getenv (41924c) reads */
static io_snapshot *ro_world[2];     /* 0 the original's, 1 the DLL's */
static BYTE *ro_arena_pre;
static uint32_t ro_committed_max;
static vos_state ro_vos_start;
static int ro_save(unsigned w) {
    if (vos.vm_committed > ro_committed_max) ro_committed_max = vos.vm_committed;
    return io_save(ro_world[w]);
}
static void ro_load(unsigned w) {
    io_restore(ro_world[w]);
    if (ro_committed_max > ro_world[w]->heap_size) memset(vos_window + ro_world[w]->heap_size, 0, ro_committed_max - ro_world[w]->heap_size);
}

/* GetTimeZoneInformation of both runtimes' _tzset. */
static LONG ro_tz_bias, ro_tz_standard;
static DWORD ro_tz_result;
static unsigned ro_tz_calls[2];
static DWORD WINAPI ro_GetTimeZoneInformation(LPTIME_ZONE_INFORMATION zone) {
    memset(zone, 0, sizeof(*zone));
    zone->Bias = ro_tz_bias; zone->StandardBias = ro_tz_standard; zone->DaylightBias = -60;
    ++ro_tz_calls[vos_which];
    return ro_tz_result;
}
/* A generated environment: the block for the DLL, the pointer array for
 * CW3220's getenv; TZ valid, invalid or absent. */
static unsigned ro_tz_kinds[3];
static void ro_environment(void) {
    static const char *const values[] = {NULL, NULL, "EST5EDT", "PST8PDT", "CET-1CEST", "GMT0", "UTC+3", "ABC-12", "XY5", "abc", "ABC", "AB1",
                                         "EST5", "EST5E", "EST5EDTX", "est+05:30pdt", "123", "EST 5", "E1T5", "EST-", "EST+x", "EST5 EDT", "EST55"};
    static const char *const others[] = {"PATH=C:\\GAMES", "TZA=XYZ9", "T=1", "=C:=C:\\", "TEMP=C:\\TMP", "TZ"};
    char *block = (char *)(uintptr_t)io_a(RO_ENV), *p = block;
    uint32_t *array = (uint32_t *)(uintptr_t)io_a(RO_ENVIRON);
    unsigned n = 0;
    for (unsigned i = random32() % 4u; i; --i) { const char *e = others[random32() % 6u]; array[n++] = (uint32_t)(uintptr_t)p; strcpy(p, e); p += strlen(e) + 1; }
    const char *tz = values[random32() % (sizeof(values) / sizeof(values[0]))];
    if (tz) { array[n++] = (uint32_t)(uintptr_t)p; p += sprintf(p, "TZ=%s", tz) + 1; }
    for (unsigned i = random32() % 2u; i; --i) { const char *e = others[random32() % 6u]; array[n++] = (uint32_t)(uintptr_t)p; strcpy(p, e); p += strlen(e) + 1; }
    *p = 0;
    array[n] = 0;
    ro_tz_bias = (LONG)(random32() % 1500u) - 720;
    ro_tz_standard = random32() % 3u ? 0 : (LONG)(random32() % 121u) - 60;
    ro_tz_result = random32() % 3u;
    ++ro_tz_kinds[!tz ? 0 : (tz[0] >= 'A' && tz[1] >= 'A' && tz[2] >= 'A' && (tz[3] == '-' || tz[3] == '+' || (tz[3] >= '0' && tz[3] <= '9'))) ? 1 : 2];
}

/* The variables of both runtimes: CW3220's from its world's data, the DLL's
 * live (its tables are in the DLL, never swapped). */
static const char *ro_variables(const BYTE *data, const BYTE *own_data, uint32_t *where) {
    const es2_io_layout *io = ro_info->io;
#define RO_CW(rva) get32(data + (rva) - IO_DATA_RVA)
    *where = 0;
    if (RO_CW(0x1fce0u) != ro_word(io->nfile)) return "_nfile";
    for (uint32_t fd = 0; fd < ES2_IO_NFILE; ++fd) {
        *where = fd;
        if (RO_CW(0x1fce4u + 4u * fd) != ro_word(io->openfd + 4u * fd)) return "handle flags";
        if (RO_CW(0x234e8u + 4u * fd) != ro_word(io->handles + 4u * fd)) return "handles";
    }
    for (uint32_t i = 0; i < ES2_IO_NFILE; ++i) {
        *where = i;
        uint32_t own_fp = io->streams + 0x18u * i;
        const char *what = ro_file_differs((const BYTE *)(uintptr_t)own_fp, own_fp, data + RO_CW_STREAMS - IO_DATA_RVA + 0x18u * i, ro_cw_streams() + 0x18u * i);
        if (what) return what;
    }
    *where = 0;
    if (RO_CW(0x22248u) != ro_word(io->errno_word)) return "errno";
    if (RO_CW(0x235d0u) != ro_word(io->doserrno)) return "_doserrno";
    if (RO_CW(0x1fe8cu) != ro_word(io->error_count)) return "_sys_nerr";
    if (RO_CW(0x20354u) != ro_word(io->umask_word)) return "umask";
    if (data[0x235b0u - IO_DATA_RVA] != *(const volatile uint8_t *)(uintptr_t)io->getc_byte) return "getc byte";
    if (RO_CW(0x235ccu) != ro_word(io->find_list)) return "find list";
    if (RO_CW(0x23070u) != ro_word(io->time_zone)) return "_timezone";
    if (RO_CW(0x23074u) != ro_word(io->summer_time)) return "_daylight";
    if (RO_CW(0x1fdb0u) != ro_word(io->fmode_pointer)) return "_fmode pointer";
    const uint32_t *hooks = (const uint32_t *)(uintptr_t)ro_info->exit_hooks;
    for (unsigned k = 0; k < 3; ++k) { *where = k; if (RO_CW(0x222bcu + 4u * k) != ro_hook_to_cw(hooks[k])) return "stream hook"; }
    /* The heap: variables, and the head block both use. */
    const es2_heap *m = ro_info->heap;
    *where = 0;
    if (RO_CW(RTL_HEAP_ROVER) != m->rover) return "heap rover";
    if (RO_CW(RTL_HEAP_COUNT) != m->region_count) return "heap region count";
    if (RO_CW(RTL_HEAP_COMMITTED) != m->committed_end || RO_CW(RTL_HEAP_RESERVED) != m->reserved_end) return "heap ends";
    if (RO_CW(RTL_HEAP_GRANULARITY) != m->granularity || RO_CW(RTL_HEAP_PLATFORM) != m->platform) return "heap granularity or platform";
    for (uint32_t i = 0; i < m->region_count && i < ES2_HEAP_REGION_LIMIT; ++i) {
        *where = i;
        if (RO_CW(RTL_HEAP_TABLE + 8u * i) != m->region_base[i] || RO_CW(RTL_HEAP_TABLE + 8u * i + 4u) != m->region_size[i]) return "heap region table";
    }
    if (memcmp(data + RTL_HEAP_SENTINEL - 8u - IO_DATA_RVA, own_data + RTL_HEAP_SENTINEL - 8u - IO_DATA_RVA, 16)) return "heap head block";
#undef RO_CW
    return NULL;
}
static unsigned ro_io_sequence_number;
static int ro_io_compare(const char *when, const io_op *op) {
    const io_snapshot *o = ro_world[0], *w = ro_world[1];
    const char *what = NULL;
    uint32_t where = 0;
    if (memcmp(&o->os, &w->os, sizeof(o->os))) what = "emulated OS";
    else if (memcmp(o->arena, w->arena, IO_ARENA)) {
        what = "arena";
        while (o->arena[where] == w->arena[where]) ++where;
    } else if (o->heap_size != w->heap_size) { what = "heap window size"; where = w->heap_size; }
    else if (memcmp(o->heap, w->heap, o->heap_size)) {
        what = "heap window";
        while (o->heap[where] == w->heap[where]) ++where;
    } else what = ro_variables(o->data, w->data, &where);
    if (!what) return 0;
    fprintf(stderr, "FAIL: %s case %u: %s%s%s: %s differs (%u)", current_function, ro_io_sequence_number, when, op ? " " : "",
            op ? es2_io_name(op->f) : "", what, (unsigned)where);
    if (op) fprintf(stderr, " args %08x %08x %08x %08x", op->args[0], op->args[1], op->args[2], op->args[3]);
    fputc('\n', stderr);
    return 1;
}
/* The DLL's OS log with its FILE-array pointers as CW3220's. */
static void ro_translate_log(void) {
    for (unsigned i = 0; i < vos_log_count[1] && i < VOS_LOG; ++i)
        for (unsigned k = 0; k < 6; ++k) vos_log[1][i].a[k] = ro_to_cw(vos_log[1][i].a[k]);
}
static int ro_logs(const char *when, const io_op *op) {
    ro_translate_log();
    unsigned at;
    if (io_logs_equal(&at)) return 0;
    fprintf(stderr, "FAIL: %s case %u: %s %s: OS calls differ at %u (original %u calls, own %u)\n", current_function, ro_io_sequence_number, when,
            op ? es2_io_name(op->f) : "", at, vos_log_count[0], vos_log_count[1]);
    io_log_dump();
    return 1;
}

static uint32_t ro_io_own_export[ES2_IO_FUNCTION_COUNT];
static unsigned ro_io_counts[ES2_IO_FUNCTION_COUNT], ro_io_skipped, ro_io_stub_failed, ro_io_stub_declined, ro_io_steps;
/* One operation, generated in the original's world (loaded): the DLL's
 * call in its world, the original's in its own, then the comparison; the
 * original's world stays loaded. */
static int ro_io_step(const io_op *op) {
    /* rtl_io_cases.h's decline classes (a float or uninitialised-base fscanf
     * format, an unbuffered fread of 0 bytes: the export reads an uninitialised
     * local): calls DBSIM never makes; not run. */
    if ((op->f == ES2_IO_FSCANF && !io_format_modelled((const char *)(uintptr_t)op->args[1])) ||
        (op->f == ES2_IO_FREAD && op->args[3] - ro_cw_streams() < ES2_IO_NFILE * 0x18u && !get32(rtl_cw + (op->args[3] - (uint32_t)(uintptr_t)rtl_cw) + 0xcu) &&
         op->args[1] && op->args[1] * op->args[2] == 0)) { ++ro_io_skipped; return 0; }
    if (getenv("ES2_IO_TRACE"))
        fprintf(stderr, "case %u: %s %08x %08x %08x %08x\n", ro_io_sequence_number, es2_io_name(op->f), op->args[0], op->args[1], op->args[2], op->args[3]);
    int stub = es2_io_is_stub(op->f);
    memcpy(ro_arena_pre, io_arena, IO_ARENA);
    SYSTEMTIME clock = vos.clock;
    if (ro_save(0)) return 1;
    /* The DLL. */
    ro_load(1);
    memcpy(io_arena, ro_arena_pre, IO_ARENA);
    vos.clock = clock;
    io_op own = *op;
    for (unsigned k = 0; k < 16; ++k) own.args[k] = ro_to_own(own.args[k]);
    vos_which = 1; vos_log_count[1] = 0;
    uint64_t declined = ro_info->counters->declined;
    uint32_t got = 0, want = 0;
    if (io_call_export(ro_io_own_export[op->f], &own, &got)) return 1;
    vos_which = 0;
    if (vos_unexpected) return fail("emulated OS: unexpected call by the DLL");
    if (ro_save(1)) return 1;
    int own_declined = ro_info->counters->declined != declined;
    /* The original. */
    ro_load(0);
    vos_log_count[0] = 0;
    if (own_declined) {
        if (!stub) { fprintf(stderr, "FAIL: %s case %u: the DLL declined %s\n", current_function, ro_io_sequence_number, es2_io_name(op->f)); return 1; }
        /* printf with a %p format: outside the formatter (DBSIM's formats have none); the stub then reports 0. */
        if (got != 0 || vos_log_count[1]) return fail("declined console stub");
        ++ro_io_stub_declined;
        return ro_io_compare("declined stub", op);
    }
    if (io_call_export(io_export[op->f], op, &want)) return 1;
    if (vos_unexpected) return fail("emulated OS: unexpected call by CW3220");
    if (stub) {
        /* The console stubs: EAX (the original's write is not made by the DLL);
         * the original's world goes back to before the call. */
        if (want == 0xffffffffu) ++ro_io_stub_failed;
        else if (got != want) { fprintf(stderr, "FAIL: %s case %u: stub %s EAX original %08x own %08x\n", current_function, ro_io_sequence_number, es2_io_name(op->f), want, got); return 1; }
        ro_load(0);
        if (vos_log_count[1]) return fail("a console stub made OS calls");
        ++ro_io_counts[op->f];
        return ro_io_compare("after the stub", op);
    }
    if (ro_save(0)) return 1;
    io_after_step(op, want);
    if (ro_to_cw(got) != want) {
        fprintf(stderr, "FAIL: %s case %u: %s EAX original %08x own %08x args %08x %08x %08x %08x\n", current_function, ro_io_sequence_number,
                es2_io_name(op->f), want, ro_to_cw(got), op->args[0], op->args[1], op->args[2], op->args[3]);
        io_log_dump();
        return 1;
    }
    if (ro_logs("after", op) || ro_io_compare("after", op)) return 1;
    ++ro_io_counts[op->f];
    ++ro_io_steps;
    return 0;
}
static int ro_io_run(es2_io_function f, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, unsigned count) {
    io_op op;
    memset(&op, 0, sizeof(op));
    op.f = f; op.args[0] = a0; op.args[1] = a1; op.args[2] = a2; op.args[3] = a3; op.count = count;
    return ro_io_step(&op);
}

/* Start-up of both worlds. */
static unsigned ro_io_hooks_installed;
static int ro_io_start(uint32_t platform) {
    memcpy(rtl_cw + IO_DATA_RVA, io_data_pristine, IO_DATA_SIZE);
    VirtualFree(vos_window, VOS_WINDOW, MEM_DECOMMIT);
    ro_committed_max = 0;
    memset(io_arena, 0, IO_ARENA);
    vos_reset();
    ro_vos_start = vos;
    ro_environment();
    uint32_t s = random32() % 4u;
    io_fmode_value = s == 0 ? 0x8000u : s == 1 ? 0u : 0x4000u;
    put32(io_arena + IO_FMODE, io_fmode_value);
    memcpy(ro_arena_pre, io_arena, IO_ARENA);
    ro_tz_calls[0] = ro_tz_calls[1] = 0;
    /* The original: the init records in their order. */
    *(volatile uint32_t *)(uintptr_t)(rtl_cw + RTL_HEAP_PLATFORM) = platform;
    ((void (__cdecl *)(void))(uintptr_t)(rtl_cw + 0x170fcu))();
    put32(rtl_cw + 0x1fdb0u, io_a(IO_FMODE));
    put32(rtl_cw + RO_ENVIRON_RVA, io_a(RO_ENVIRON));
    vos_which = 0; vos_log_count[0] = 0;
    ((void (__cdecl *)(void))(uintptr_t)(rtl_cw + 0x9014u))();
    ((void (__cdecl *)(void))(uintptr_t)(rtl_cw + 0x7248u))();
    ((void (__cdecl *)(void))(uintptr_t)(rtl_cw + 0x1e268u))();
    if (vos_unexpected) return fail("emulated OS: unexpected call during CW3220's start-up");
    if (ro_save(0)) return 1;
    /* The DLL: its reinitialize on the same OS, arena and memory. */
    memcpy(rtl_cw + IO_DATA_RVA, io_data_pristine, IO_DATA_SIZE);
    memset(vos_window, 0, ro_committed_max);
    memcpy(io_arena, ro_arena_pre, IO_ARENA);
    vos = ro_vos_start;
    vos_which = 1; vos_log_count[1] = 0;
    if (ro_reinit((uint32_t)(uintptr_t)rtl_cw + RTL_HEAP_SENTINEL, (int32_t)platform, io_a(RO_ENV), 0, RO_STACK_LO, RO_STACK_HI, 0)) return 1;
    vos_which = 0;
    *(volatile uint32_t *)(uintptr_t)ro_info->io->fmode_pointer = io_a(IO_FMODE);
    if (vos_unexpected) return fail("emulated OS: unexpected call during the DLL's start-up");
    if (ro_save(1)) return 1;
    ro_load(0);
    if (ro_tz_calls[0] != ro_tz_calls[1]) return fail("GetTimeZoneInformation calls of the start-up");
    if (ro_logs("start-up", NULL) || ro_io_compare("start-up", NULL)) return 1;
    if (ro_word(ro_info->exit_hooks) != ro_info->dummy_hook) ++ro_io_hooks_installed;
    return 0;
}
/* The stream hooks exit runs (exitbuf, then exitfopen), on both worlds. */
static int ro_call0(uint32_t target) {
    rtl_vitro_frame fr;
    memset(&fr, 0, sizeof(fr));
    memcpy((BYTE *)(uintptr_t)(RTL_ARGS_ADDRESS - RTL_BELOW), rtl_below_pattern, RTL_BELOW);
    fr.esp = RTL_ARGS_ADDRESS; fr.target = target;
    fr.ebx = 0xb0b0b0b0u; fr.esi = 0x51515151u; fr.edi = 0xd1d1d1d1u; fr.ebp = 0xe0e0e0e0u;
    rtl_vitro_call(&fr);
    if (fr.out_ebx != 0xb0b0b0b0u || fr.out_esi != 0x51515151u || fr.out_edi != 0xd1d1d1d1u || fr.out_ebp != 0xe0e0e0e0u ||
        fr.out_esp != RTL_ARGS_ADDRESS || (fr.out_flags & 0x400u)) return fail("stream hook: callee-saved registers, ESP or DF");
    return 0;
}
static unsigned ro_io_exits;
static int ro_io_exit(void) {
    if (ro_save(0)) return 1;
    ro_load(1);
    /* The caller's memory and the clock as generated since the last call. */
    memcpy(io_arena, ro_world[0]->arena, IO_ARENA);
    vos.clock = ro_world[0]->os.clock;
    vos_which = 1; vos_log_count[1] = 0;
    const uint32_t *hooks = (const uint32_t *)(uintptr_t)ro_info->exit_hooks;
    uint32_t own0 = hooks[0], own1 = hooks[1];
    if (ro_call0(own0) || ro_call0(own1)) return 1;
    vos_which = 0;
    if (ro_save(1)) return 1;
    ro_load(0);
    vos_log_count[0] = 0;
    if (ro_call0(get32(rtl_cw + 0x222bcu)) || ro_call0(get32(rtl_cw + 0x222c0u))) return 1;
    if (vos_unexpected) return fail("emulated OS: unexpected call by a stream hook");
    if (ro_save(0)) return 1;
    if (ro_logs("exit hooks", NULL) || ro_io_compare("exit hooks", NULL)) return 1;
    ++ro_io_exits;
    return 0;
}
static int ro_io_sequence(unsigned steps) {
    io_focus_scan = 0;
    if (ro_io_start(random32() % 3u)) return 1;
    memset(io_ffblk_ready, 0, sizeof(io_ffblk_ready));
    unsigned kind = ro_io_sequence_number % 10u;
    if (ro_io_sequence_number % 2u == 0) {
        static const char *const modes[] = {"r", "rt", "rb", "r+", "r"};
        for (unsigned k = 0; k < 3u; ++k) {
            uint32_t name = io_name(1, k == 2 ? (random32() % 2u ? "c.dat" : io_file_names[13u + random32() % 4u]) : k ? "b.txt" : "a.txt");
            strcpy((char *)(uintptr_t)io_a(IO_MODES), modes[random32() % 5u]);
            if (ro_io_run(ES2_IO_FOPEN, name, io_a(IO_MODES), 0, 0, 2)) return 1;
        }
    }
    io_focus_scan = ro_io_sequence_number % 4u == 2u;
    if (kind == 3u)   /* every FILE in use */
        for (unsigned k = 0; k < 50u; ++k) {
            uint32_t name = io_name(1, k % 3u ? "<in>" : "a.txt");
            strcpy((char *)(uintptr_t)io_a(IO_MODES), "rb");
            if (ro_io_run(ES2_IO_FOPEN, name, io_a(IO_MODES), 0, 0, 2)) return 1;
        }
    if (kind == 7u)   /* every handle in use */
        for (unsigned k = 0; k < 56u; ++k)
            if (ro_io_run(ES2_IO_OPEN, io_name(0, k % 2u ? "a.tx" : "<in>"), k % 2u ? 0x8101u : 0x8000u, 0x180u, 0, 3)) return 1;
    if (kind == 9u)   /* terminal streams with output, then input */
        for (unsigned k = 0; k < 2u; ++k) {
            static const char *const modes[] = {"r+", "w+", "a+"};
            uint32_t name = io_name(1, "con");
            strcpy((char *)(uintptr_t)io_a(IO_MODES), modes[random32() % 3u]);
            if (ro_io_run(ES2_IO_FOPEN, name, io_a(IO_MODES), 0, 0, 2)) return 1;
        }
    for (unsigned s = 0; s < steps; ++s) {
        io_op op = io_generate();
        if (!io_op_allowed(&op)) continue;
        if (ro_io_step(&op)) return 1;
    }
    return ro_io_exit();
}
static int ro_io(void) {
    current_function = "rtl_own_io";
    vos_window = VirtualAlloc(NULL, VOS_WINDOW, MEM_RESERVE, PAGE_NOACCESS);
    io_arena = VirtualAlloc(NULL, IO_ARENA, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    io_data_pristine = io_data_pristine ? io_data_pristine : malloc(IO_DATA_SIZE);
    ro_world[0] = malloc(sizeof(io_snapshot)); ro_world[1] = malloc(sizeof(io_snapshot));
    ro_arena_pre = malloc(IO_ARENA);
    if (!vos_window || !io_arena || !io_data_pristine || !ro_world[0] || !ro_world[1] || !ro_arena_pre) return fail("memory");
    if (io_hook_os() || io_load_cd_files(getenv("ES2_DATA_ROOT"))) return 1;
    uint32_t *tz_slot = io_cw_slot("GetTimeZoneInformation");
    if (!tz_slot) return fail("CW3220's GetTimeZoneInformation slot");
    *tz_slot = (uint32_t)(uintptr_t)ro_GetTimeZoneInformation;
    for (int f = 0; f < ES2_IO_FUNCTION_COUNT; ++f) {
        io_export[f] = rtl_cw_export(es2_io_name((es2_io_function)f));
        ro_io_own_export[f] = ro_export(es2_io_name((es2_io_function)f));
        if (!io_export[f] || !ro_io_own_export[f]) return fail("I/O export missing");
    }
    es2_io_layout_cw3220(&io_layout, (uint32_t)(uintptr_t)rtl_cw);
    static const struct { const char *name; const void *function; } os[] = {
        {"CreateFileA", (const void *)vos_CreateFileA}, {"CloseHandle", (const void *)vos_CloseHandle}, {"ReadFile", (const void *)vos_ReadFile},
        {"WriteFile", (const void *)vos_WriteFile}, {"SetFilePointer", (const void *)vos_SetFilePointer}, {"GetFileSize", (const void *)vos_GetFileSize},
        {"GetFileType", (const void *)vos_GetFileType}, {"GetFileAttributesA", (const void *)vos_GetFileAttributesA},
        {"GetLastError", (const void *)vos_GetLastError}, {"FindFirstFileA", (const void *)vos_FindFirstFileA},
        {"FindNextFileA", (const void *)vos_FindNextFileA}, {"FindClose", (const void *)vos_FindClose},
        {"FileTimeToLocalFileTime", (const void *)vos_FileTimeToLocalFileTime}, {"FileTimeToDosDateTime", (const void *)vos_FileTimeToDosDateTime},
        {"GetLocalTime", (const void *)vos_GetLocalTime}, {"SetHandleCount", (const void *)vos_SetHandleCount},
        {"GetStartupInfoA", (const void *)vos_GetStartupInfoA}, {"GetStdHandle", (const void *)vos_GetStdHandle},
        {"VirtualAlloc", (const void *)vos_VirtualAlloc}, {"VirtualFree", (const void *)vos_VirtualFree},
        {"GetTimeZoneInformation", (const void *)ro_GetTimeZoneInformation},
    };
    for (unsigned i = 0; i < sizeof(os) / sizeof(os[0]); ++i) if (ro_point("KERNEL32.dll", os[i].name, os[i].function)) return 1;
    for (unsigned i = 0; i < RTL_BELOW; ++i) rtl_below_pattern[i] = (BYTE)random32();
    unsigned sequences = quick_tier ? 200u : 2000u, steps = 60u;
    for (ro_io_sequence_number = 0; ro_io_sequence_number < sequences; ++ro_io_sequence_number) {
        current_case = ro_io_sequence_number; current_case_valid = 1;
        if (ro_io_sequence(steps)) return 1;
    }
    current_case_valid = 0;
    ro_unpoint();
    printf("PASS: rtl_own io: %u sequences, %u start-ups (TZ absent %u, valid %u, invalid %u; exitbuf hook installed by the start-up in %u), "
           "%u file steps, %u console stub calls (%u whose original write failed, %u declined %%p formats), %u exit-hook runs, %u "
           "calls of H6-4's decline classes left out: EAX, OS calls, OS state, arena, heap window, I/O and heap variables equal to "
           "CW3220's after every step\n", sequences, sequences, ro_tz_kinds[0], ro_tz_kinds[1], ro_tz_kinds[2], ro_io_hooks_installed, ro_io_steps,
           ro_io_counts[ES2_IO_PRINTF] + ro_io_counts[ES2_IO_FPUTC] + ro_io_counts[ES2_IO_FPUTS], ro_io_stub_failed, ro_io_stub_declined, ro_io_exits,
           ro_io_skipped);
    printf("IO CASES:");
    for (int f = 0; f < ES2_IO_FUNCTION_COUNT; ++f) printf("%s %s %u", f ? "," : "", es2_io_name((es2_io_function)f) + 1, ro_io_counts[f]);
    printf("\n");
    for (int f = 0; f < ES2_IO_FUNCTION_COUNT; ++f)
        if (!ro_io_counts[f]) { fprintf(stderr, "FAIL: %s: no case for %s\n", current_function, es2_io_name((es2_io_function)f)); return 1; }
    return 0;
}

/* ------------------------------------------------------------------------
 * Lifecycle: rtl_start_cases.h's switched comparison with the DLL in DBSIM's
 * slots. */
static unsigned ro_life_paths[LS_P_COUNT];
static uint32_t ro_life_translate(uint32_t w) {
    if (w == ro_info->startup_handler) return ls_cw_base() + ES2_RTL_FRAME_HANDLER_RVA;
    if (w == ro_info->window_callback) return ls_cw_base() + ES2_RTL_ENUM_PROC_RVA;
    if (w == ro_info->abort_text) return ls_cw_base() + LS_ABORT_MESSAGE;
    if (w == ro_info->dummy_hook) return ls_cw_base() + LS_DUMMY_HOOK;
    return w;
}
static int ro_life_prepare(void) {
    if (ro_reinit(0, 0, 0, ls_cw_base(), LS_ADDR(ls_stack), LS_ADDR(ls_stack + LS_STACK_DWORDS), ES2_OWN_SETUP_NO_IO_START)) return 1;
    es2_rtl_runtime *m = ro_info->lifecycle;
    m->argc = ls_c.argc; m->argv = ls_c.argv; m->envp = ls_c.envp;
    m->atexit_count = ls_c.atexit_count;
    memcpy(m->atexit_table, ls_c.atexit_table, sizeof(m->atexit_table));
    uint32_t *hooks = (uint32_t *)(uintptr_t)ro_info->exit_hooks;
    for (unsigned i = 0; i < 3; ++i) hooks[i] = ls_c.hook_id[i] < 0 ? ro_info->dummy_hook : ls_void_address((unsigned)ls_c.hook_id[i]);
    m->sigabrt = ls_handler();
    m->sigabrt_extra = ls_c.sigabrt_extra;
    return 0;
}
static void ro_life_read(ls_vars *v, uint32_t anchor) {
    const es2_rtl_runtime *m = ro_info->lifecycle;
    const uint32_t *hooks = (const uint32_t *)(uintptr_t)ro_info->exit_hooks;
    v->gui = m->gui; v->matherr = m->matherr; v->matherrl = m->matherrl; v->fmode = m->fmode;
    v->module_count = m->module_count; v->module_table = m->module_table;
    v->frame = m->frame_record ? m->frame_record - anchor : 0;
    v->environment = m->environment; v->command = m->command_line; v->argc = m->argc; v->argv = m->argv; v->envp = m->envp;
    v->atexit_count = m->atexit_count;
    memcpy(v->atexit_table, m->atexit_table, sizeof(v->atexit_table));
    v->exitbuf = ro_life_translate(hooks[0]); v->exitfopen = ro_life_translate(hooks[1]); v->exitopen = ro_life_translate(hooks[2]);
    v->sigabrt = m->sigabrt; v->sigabrt_extra = m->sigabrt_extra;
}
static uint64_t ro_life_frames, ro_life_words, ro_life_events;
static int ro_life_case(unsigned n) {
    current_case = n; current_case_valid = 1;
    ls_switched_cases = 1;
    ls_generate(n);
    ls_switched_cases = 0;
    ls_vars vars[2];
    BYTE host_fpu[108];
    uint32_t pattern = 0x5a000000u | (random32() & 0x00ffffffu);
    ls_rich = 1;
    /* The original, through DBSIM's thunks to the mapped exports. */
    if (ro_dbsim_slots(0)) { ls_rich = 0; return 1; }
    ls_prepare_memory();
    ls_set_original();
    ls_begin_run(LS_ORIGINAL, pattern);
    ls_nested_exits = ls_nested_registrations = ls_full_tables = 0;
    memset(ls_kind_calls, 0, sizeof(ls_kind_calls));
    __asm__ volatile("fnsave %0" : "=m"(host_fpu));
    ls_fpu_garbage();
    ls_run(ls_exports[LS_X_STARTUP], ls_top(), LS_TABLE);
    __asm__ volatile("frstor %0" ::"m"(host_fpu));
    ls_startup_anchor = ls_top() - 8u;
    if (ls_unexpected) {
        ls_rich = 0;
        fprintf(stderr, "FAIL: %s case %u: the original called the unexpected import %s\n", current_function, n, rtl_cw_imports[ls_unexpected - 1u]);
        return 1;
    }
    ls_end_run(0);
    ls_read_original(&vars[0], ls_startup_anchor);
    /* The DLL: its exports in DBSIM's slots, DBSIM's entry jump to the
     * __startup thunk; CW3220's variables set as for the original. */
    ls_prepare_memory();
    ls_set_original();
    if (ro_life_prepare() || ro_dbsim_slots(1)) { ls_rich = 0; return 1; }
    es2_own_counters before = *ro_info->counters;
    ls_begin_run(LS_SWITCHED, pattern);
    ls_switched_return = ro_info->life_return;
    ls_switched_rva = ro_info->life_return_rva;
    ls_translate = ro_life_translate;
    __asm__ volatile("fnsave %0" : "=m"(host_fpu));
    ls_fpu_garbage();
    ls_run(ls_dbsim_thunk(RTL_STARTUP_INDEX), ls_top(), LS_TABLE);
    __asm__ volatile("frstor %0" ::"m"(host_fpu));
    ls_end_run(1);
    ls_switched_return = 0; ls_switched_rva = NULL;
    ro_life_read(&vars[1], ls_startup_anchor);
    ls_translate = NULL;
    const es2_own_counters *k = ro_info->counters;
    memset(ls_kind_calls, 0, sizeof(ls_kind_calls));
    ls_kind_calls[ES2_RTL_CALL_INIT_RECORD] = (unsigned)(k->init_records - before.init_records);
    ls_kind_calls[ls_c.main_kind == LS_MAIN_CONSOLE ? ES2_RTL_CALL_MAIN : ES2_RTL_CALL_WINMAIN] = (unsigned)(k->winmain - before.winmain);
    ls_kind_calls[ES2_RTL_CALL_ATEXIT] = (unsigned)(k->atexit_run - before.atexit_run);
    ls_kind_calls[ES2_RTL_CALL_EXIT_RECORD] = (unsigned)(k->exit_records - before.exit_records);
    ls_kind_calls[ES2_RTL_CALL_EXITBUF] = (unsigned)(k->stream_hooks - before.stream_hooks);
    ls_rich = 0;
    ls_mode = LS_ORIGINAL;
    if (ro_dbsim_slots(0)) return 1;
    if (ls_unexpected) { fprintf(stderr, "FAIL: %s case %u: the DLL's run called the unexpected import %s\n", current_function, n, rtl_cw_imports[ls_unexpected - 1u]); return 1; }
    if (ls_bad_return) { fprintf(stderr, "FAIL: %s case %u: %u calls without the DLL's return address\n", current_function, n, ls_bad_return); return 1; }
    /* CW3220's variables stay as ls_set_original left them. */
    if (*ls_var(LS_ATEXIT_COUNT) != ls_c.atexit_count || *ls_var(LS_ATEXIT_TABLE) != ls_c.atexit_table[0] || *ls_var(LS_FRAME) ||
        *ls_var(LS_GUI) || *ls_var(LS_MODULE_COUNT) || *ls_var(LS_EXITBUF) != ls_hook(0) || *ls_var(LS_SIGABRT) != ls_handler())
        return fail("the DLL's run wrote CW3220's variables");
    uint64_t frames = ls_switched_frames, words = ls_switched_words;
    if (ls_compare_events(n) || ls_compare_frames(n) || ls_compare_vars(n, vars)) return 1;
    ro_life_frames += ls_switched_frames - frames; ro_life_words += ls_switched_words - words;
    ro_life_events += ls_log_count[0];
    ls_record_paths(ro_life_paths, ls_log[0], ls_log_count[0]);
    return 0;
}
static int ro_life(void) {
    current_function = "rtl_own_life";
    for (unsigned i = 0; i < LS_X_COUNT; ++i) {
        ls_exports[i] = rtl_cw_export(ls_export_names[i].name);
        if (ls_exports[i] != ls_cw_base() + ls_export_names[i].rva) return fail("lifecycle export address");
    }
    /* CW3220's import slots: own stubs that end the run, the nine the
     * lifecycle calls with behaviour (as rtl_start_cases does). */
    BYTE *stubs = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!stubs) return fail("stub allocation");
    static uint32_t saved_slots[128];
    static uint32_t *slots[128];
    unsigned slot_count = 0;
    uint32_t pe = get32(rtl_cw + 0x3c), imports = get32(rtl_cw + pe + 24 + 96 + 8);
    for (uint32_t d = imports; get32(rtl_cw + d + 12); d += 20) {
        uint32_t lookup = get32(rtl_cw + d), iat = get32(rtl_cw + d + 16);
        if (!lookup) lookup = iat;
        for (unsigned i = 0; get32(rtl_cw + lookup + 4 * i); ++i) {
            if (slot_count >= 128) return fail("CW3220 import count");
            uint32_t *slot = (uint32_t *)(rtl_cw + iat + 4 * i);
            BYTE *s = stubs + 16 * slot_count;
            s[0] = 0x68; put32(s + 1, slot_count); s[5] = 0xe8;
            put32(s + 6, (uint32_t)(uintptr_t)ls_unexpected_import - (uint32_t)(uintptr_t)(s + 10)); s[10] = 0xcc;
            slots[slot_count] = slot; saved_slots[slot_count++] = *slot;
            *slot = (uint32_t)(uintptr_t)s;
        }
    }
    DWORD previous;
    if (!VirtualProtect(stubs, 4096, PAGE_EXECUTE_READ, &previous)) return fail("stub protection");
    static const struct { const char *dll, *name; const void *stub; } named[] = {
        {"KERNEL32.dll", "GetEnvironmentStrings", ls_api_environment}, {"KERNEL32.dll", "GetCommandLineA", ls_api_command},
        {"KERNEL32.dll", "GetModuleHandleA", ls_api_handle}, {"KERNEL32.dll", "GetModuleFileNameA", ls_api_file},
        {"KERNEL32.dll", "GetVersion", ls_api_version}, {"KERNEL32.dll", "GetCurrentThreadId", ls_api_thread},
        {"USER32.dll", "EnumThreadWindows", ls_api_enum}, {"USER32.dll", "MessageBoxA", ls_api_box},
        {"KERNEL32.dll", "ExitProcess", ls_api_exit},
    };
    for (unsigned i = 0; i < sizeof(named) / sizeof(named[0]); ++i) {
        uint32_t *slot = ls_cw_slot(named[i].dll, named[i].name);
        if (!slot) return fail("CW3220 import slot missing");
        *slot = (uint32_t)(uintptr_t)named[i].stub;
        if (ro_point(named[i].dll, named[i].name, named[i].stub)) return 1;
    }
    unsigned cases = quick_tier ? 1500u : 15000u;
    for (unsigned n = 0; n < cases; ++n)
        if (ro_life_case(0x200000u + n)) return 1;
    current_case_valid = 0;
    ro_unpoint();
    for (unsigned i = 0; i < slot_count; ++i) *slots[i] = saved_slots[i];
    printf("PASS: rtl_own life: %u cases, %llu events, %llu frames, %llu stack words: __startup, _atexit, __exit, _abort and __ErrorExit "
           "of the DLL in DBSIM's slots equal to CW3220's in events, registers at every guest and Win32 call, the stack words above "
           "them, table bytes and the runtime variables; CW3220's variables untouched\n", cases, (unsigned long long)ro_life_events,
           (unsigned long long)ro_life_frames, (unsigned long long)ro_life_words);
    static const unsigned required[] = {
        LS_P_INIT_TIE, LS_P_INIT_LOWER, LS_P_INIT_DONE, LS_P_INIT_ZERO, LS_P_EXIT_TIE, LS_P_EXIT_HIGHER, LS_P_GUI_STDCALL,
        LS_P_GUI_CDECL, LS_P_CONSOLE, LS_P_CMD_QUOTED, LS_P_CMD_PLAIN, LS_P_RETURN, LS_P_QUICK_EXIT, LS_P_ABORT_DEFAULT,
        LS_P_ABORT_IGNORE, LS_P_ABORT_HANDLER, LS_P_ABORT_EXTRA, LS_P_ERROR_EXIT, LS_P_ATEXIT_LIFO, LS_P_ATEXIT_FULL,
        LS_P_NESTED_REGISTRATION, LS_P_NESTED_EXIT, LS_P_HOOK_STUB, LS_P_HOOK_DUMMY, LS_P_NT, LS_P_9X_WINDOW,
        LS_P_9X_NO_WINDOW, LS_P_TITLE_BACKSLASH, LS_P_TITLE_COLON, LS_P_TITLE_PLAIN,
    };
    printf("LIFE PATHS:");
    for (unsigned i = 0; i < sizeof(required) / sizeof(required[0]); ++i) printf("%s %s %u", i ? "," : "", ls_path_names[required[i]], ro_life_paths[required[i]]);
    printf("\n");
    for (unsigned i = 0; i < sizeof(required) / sizeof(required[0]); ++i)
        if (!ro_life_paths[required[i]]) { fprintf(stderr, "FAIL: %s: path not reached: %s\n", current_function, ls_path_names[required[i]]); return 1; }
    return 0;
}

/* ------------------------------------------------------------------------
 * Exceptions: __ExceptionHandler over DBSIM's frame tables. The callers of
 * 49630c pass their table in EAX (B8 table, E8 call); the context index
 * lives at the frame record + 10h, the record at EBP + [table + 4]; the
 * values each function stores there name its entries (word prev, word kind). */
#define RO_FRAME_SETUP 0x9630cu
static unsigned ro_kinds[8], ro_handler_calls, ro_tables_seen;
static int ro_entry_chain(uint32_t table, uint32_t index) {
    for (unsigned steps = 0; index && steps < 256u; ++steps) {
        uint32_t kind = *(const uint16_t *)(uintptr_t)(table + index + 2u), prev = *(const uint16_t *)(uintptr_t)(table + index);
        if (kind > 5u) return -1;
        if (kind == 1u || kind == 2u) return (int)kind;   /* __except: the original would run its filter */
        index = prev;
    }
    return index ? -1 : 0;
}
/* a C++ throw (search phase) and an unwind. */
typedef int (__cdecl *ro_handler_fn)(void *, void *, void *, void *);
#define RO_THROW_CODE 0x0eefaceu
static unsigned ro_throw_entries, ro_throw_calls, ro_throw_caught;
static unsigned ro_unwind_entries, ro_unwind_calls, ro_unwind_cleanup, ro_unwind_block, ro_unwind_destroy;
static uint16_t ro_word16(uint32_t address) { return *(const volatile uint16_t *)(uintptr_t)address; }
/* 1 when the chain from `index` reaches a catch entry (kind 3). */
static int ro_chain_catches(uint32_t table, uint32_t index) {
    for (unsigned steps = 0; index && steps < 256u; ++steps) {
        if (ro_word16(table + index + 2u) == 3u) return 1;
        index = ro_word16(table + index);
    }
    return 0;
}
/* The original's unwind clean-up of a frame (40415d from the context index to
 * 0) with nothing constructed since the frame's entry: 0 when it calls
 * nothing; else 1 (a kind-0 block: 4052a3 calls it with EBP = the frame) or 2
 * (a kind-5 entry whose object table it walks: 40487b destroys when its
 * descriptor's count says so (flag 20h), for flags 3 (an allocation in
 * progress) or when objects were constructed; flag 100h returns the
 * descriptor's +24h, which the next entry's count adds). */
static int ro_unwind_cleans(uint32_t table, uint32_t index) {
    uint32_t returned = 0;
    for (unsigned steps = 0; index && steps < 256u; ++steps) {
        uint32_t kind = ro_word16(table + index + 2u);
        if (kind == 0u || kind == 4u) return 1;
        if (kind == 5u) {
            uint32_t count = ro_word(table + index + 4u), objects = ro_word(table + index + 8u), result = 0;
            if (objects) {
                uint32_t flags = ro_word(objects + 4u);
                if (!(flags & 0x100u) && ((flags & 0x20u) || (int32_t)(0u - (count + returned)) > 0 || (flags & 3u) == 3u)) return 2;
                if (flags & 0x100u) result = ro_word(ro_word(objects) + 0x24u);
            }
            returned = result;
        }
        index = ro_word16(table + index);
    }
    return 0;
}
/* While the throw and unwind cases run, the CW3220 helpers through which the
 * original would call DBSIM code (4052a3 clean-up block, 403a7c and 403ac9
 * destructor calls, 403e1c exception clean-up, 405036 thrown-object
 * destruction) and operator delete/delete[] (417d00, 417d10) start with int3:
 * a call the classification above did not expect stops the run. */
static const uint32_t ro_guard_rvas[] = {0x52a3u, 0x3a7cu, 0x3ac9u, 0x3e1cu, 0x5036u, 0x17d00u, 0x17d10u};
static BYTE ro_guard_saved[sizeof(ro_guard_rvas) / sizeof(ro_guard_rvas[0])];
static int ro_guards(int on) {
    for (unsigned i = 0; i < sizeof(ro_guard_rvas) / sizeof(ro_guard_rvas[0]); ++i) {
        BYTE *p = rtl_cw + ro_guard_rvas[i];
        DWORD previous, ignored;
        if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &previous)) return fail("guard protection");
        if (on) { ro_guard_saved[i] = *p; *p = 0xcc; } else *p = ro_guard_saved[i];
        VirtualProtect(p, 1, previous, &ignored);
        FlushInstructionCache(GetCurrentProcess(), p, 1);
    }
    return 0;
}
static int ro_throw_and_unwind(ro_handler_fn original, ro_handler_fn own, uint32_t table, uint32_t value) {
    /* The construction counter at this thread's StackBase-4 (where DBSIM's
     * c0w32 keeps it): the frame's entry value equal to it, so nothing was
     * constructed since. */
    uint32_t counter = *(const volatile uint32_t *)((const BYTE *)((NT_TIB *)NtCurrentTeb())->StackBase - 4);
    int catches = ro_chain_catches(table, value), cleans = ro_unwind_cleans(table, value);
    if (catches) ++ro_throw_caught; else ++ro_throw_entries;
    if (cleans) { ++ro_unwind_cleanup; if (cleans == 1) ++ro_unwind_block; else ++ro_unwind_destroy; } else ++ro_unwind_entries;
    for (unsigned c = 0; c < 5u; ++c) {
        int unwind = c >= 2u;
        if (unwind ? cleans != 0 : catches) continue;
        EXCEPTION_RECORD record, record_before;
        CONTEXT context, context_before;
        uint32_t frame[8], frame_before[8], frame_want[8];
        memset(&record, 0, sizeof(record));
        /* An unwind after a throw runs the thrown object's clean-up when the
         * table lists one (its first word): the throw code only without it. */
        record.ExceptionCode = !unwind || (c == 4u && !ro_word(table)) ? RO_THROW_CODE : 0xc0000005u;
        record.ExceptionFlags = unwind ? (c == 2u ? 2u : c == 3u ? 4u : 6u) : c;
        record.ExceptionAddress = (void *)(uintptr_t)(0x400000u + random32() % 0x2c0000u);
        record.NumberParameters = 3;
        for (unsigned i = 0; i < 3; ++i) record.ExceptionInformation[i] = random32();
        memset(&context, 0x5c, sizeof(context));
        frame[0] = 0xffffffffu; frame[1] = 0x4967f0u; frame[2] = table; frame[3] = 0x0012f000u;
        frame[4] = value | 0x5a5a0000u; frame[5] = random32(); frame[6] = random32(); frame[7] = counter;
        memcpy(&record_before, &record, sizeof(record)); memcpy(&context_before, &context, sizeof(context)); memcpy(frame_before, frame, sizeof(frame));
        memcpy(frame_want, frame, sizeof(frame));
        if (unwind) frame_want[4] &= 0xffff0000u;   /* 40415d steps the context word to 0 */
        int want = original(&record, frame, &context, NULL);
        if (memcmp(&record, &record_before, sizeof(record)) || memcmp(&context, &context_before, sizeof(context)) || memcmp(frame, frame_want, sizeof(frame))) {
            fprintf(stderr, "FAIL: %s: %s, table %08x entry %x: the original wrote more than %s (context word %08x)\n", current_function,
                    unwind ? "unwind" : "throw", table, value, unwind ? "the context word" : "nothing", frame[4]);
            return 1;
        }
        memcpy(frame, frame_before, sizeof(frame));
        int got = own(&record, frame, &context, NULL);
        if (memcmp(&record, &record_before, sizeof(record)) || memcmp(&context, &context_before, sizeof(context)) || memcmp(frame, frame_before, sizeof(frame)))
            return fail("the DLL's handler wrote the record, frame or context under a throw or an unwind");
        if (want != got || want != ExceptionContinueSearch) {
            fprintf(stderr, "FAIL: %s: %s, table %08x entry %x: original %d own %d\n", current_function, unwind ? "unwind" : "throw", table, value, want, got);
            return 1;
        }
        if (unwind) ++ro_unwind_calls; else ++ro_throw_calls;
    }
    return 0;
}
static int ro_exceptions(void) {
    current_function = "rtl_own_exceptions";
    typedef int (__cdecl *handler_fn)(void *, void *, void *, void *);
    handler_fn original = (handler_fn)(uintptr_t)rtl_cw_export("__ExceptionHandler"), own = (handler_fn)(uintptr_t)ro_export("__ExceptionHandler");
    if (!original || !own) return fail("__ExceptionHandler exports");
    if (ro_guards(1)) return 1;
    uint32_t pe = get32(mapped + 0x3c), sections = pe + 24 + get16(mapped + pe + 20);
    unsigned entries = 0;
    for (unsigned s = 0; s < get16(mapped + pe + 6); ++s) {
        const BYTE *h = mapped + sections + 40u * s;
        if (!(get32(h + 36) & 0x20000000u)) continue;
        uint32_t start = get32(h + 12), end = start + get32(h + 8), image_size = get32(mapped + pe + 24 + 56);
        /* The calls (E8 to 49630c) and the table load before each: mov eax,
         * imm32 (B8) up to 24 bytes earlier, the table inside the image with
         * a frame offset below EBP. */
        static uint32_t sites[512], site_tables[512];
        uint32_t site_count = 0;
        for (uint32_t p = start; p + 5u <= end; ++p) {
            if (mapped[p] != 0xe8 || p + 5u + get32(mapped + p + 1) != RO_FRAME_SETUP) continue;
            for (uint32_t back = 5u; back <= 24u && back <= p - start; ++back) {
                uint32_t q = p - back, table = get32(mapped + q + 1);
                if (mapped[q] != 0xb8 || table - 0x400000u >= image_size - 8u) continue;
                int32_t offset = (int32_t)ro_word(table + 4u);
                if (offset >= 0 || offset < -0x10000) continue;
                if (site_count >= 512u) return fail("frame-table call sites");
                sites[site_count] = p; site_tables[site_count++] = table;
                break;
            }
        }
        for (uint32_t i = 0; i < site_count; ++i) {
            uint32_t table = site_tables[i];
            int32_t disp = (int32_t)ro_word(table + 4u) + 0x10;
            uint32_t limit = i + 1u < site_count ? sites[i + 1] : sites[i] + 0x6000u;
            if (limit > end) limit = end;
            ++ro_tables_seen;
            for (uint32_t q = sites[i] + 5u; q + 6u <= limit; ++q) {
                uint32_t value;
                if (mapped[q] == 0x66 && mapped[q + 1] == 0xc7 && mapped[q + 2] == 0x45 && (int8_t)mapped[q + 3] == disp) value = get16(mapped + q + 4);
                else if (q + 9u <= limit && mapped[q] == 0x66 && mapped[q + 1] == 0xc7 && mapped[q + 2] == 0x85 && (int32_t)get32(mapped + q + 3) == disp) value = get16(mapped + q + 7);
                else continue;
                if (!value) continue;
                int chain = ro_entry_chain(table, value);
                if (chain < 0) continue;   /* a store of the same slot after the function's end */
                if (chain) { fprintf(stderr, "FAIL: %s: DBSIM table %08x entry %x reaches an __except entry (kind %d)\n", current_function, table, value, chain); return 1; }
                ++ro_kinds[*(const uint16_t *)(uintptr_t)(table + value + 2u)];
                ++entries;
                /* Exceptions other than a C++ throw, outside an unwind. */
                static const uint32_t codes[] = {0xc0000005u, 0xc0000094u, 0x80000003u, 0xc00000fdu, 0xc0000096u};
                for (unsigned c = 0; c < 6u; ++c) {
                    EXCEPTION_RECORD record, record_before;
                    CONTEXT context, context_before;
                    uint32_t frame[8], frame_before[8];
                    memset(&record, 0, sizeof(record));
                    record.ExceptionCode = c < 5u ? codes[c] : random32() | 0x80000000u;   /* never 0EEFFACEh */
                    record.ExceptionFlags = random32() % 2u;   /* 0, or EXCEPTION_NONCONTINUABLE */
                    record.ExceptionAddress = (void *)(uintptr_t)(0x400000u + random32() % 0x2c0000u);
                    memset(&context, 0x5c, sizeof(context));
                    frame[0] = 0xffffffffu; frame[1] = 0x4967f0u; frame[2] = table; frame[3] = 0x0012f000u;
                    frame[4] = value; frame[5] = 0; frame[6] = 0; frame[7] = 0x0012fe00u;
                    memcpy(&record_before, &record, sizeof(record)); memcpy(&context_before, &context, sizeof(context)); memcpy(frame_before, frame, sizeof(frame));
                    int want = original(&record, frame, &context, NULL);
                    if (memcmp(&record, &record_before, sizeof(record)) || memcmp(&context, &context_before, sizeof(context)) || memcmp(frame, frame_before, sizeof(frame)))
                        return fail("the original's handler wrote the record, frame or context");
                    int got = own(&record, frame, &context, NULL);
                    if (memcmp(&record, &record_before, sizeof(record)) || memcmp(&context, &context_before, sizeof(context)) || memcmp(frame, frame_before, sizeof(frame)))
                        return fail("the DLL's handler wrote the record, frame or context");
                    if (want != got || want != ExceptionContinueSearch) {
                        fprintf(stderr, "FAIL: %s: table %08x entry %x code %08x: original %d own %d\n", current_function, table, value, (unsigned)record.ExceptionCode, want, got);
                        return 1;
                    }
                    ++ro_handler_calls;
                }
                if (ro_throw_and_unwind(original, own, table, value)) { ro_guards(0); return 1; }
            }
        }
    }
    if (ro_guards(0)) return 1;
    if (!entries || !ro_kinds[5]) return fail("no DBSIM frame-table entries found");
    if (!ro_throw_calls || !ro_unwind_calls || !ro_unwind_cleanup) return fail("no throw or unwind case");
    printf("PASS: rtl_own exceptions: %u DBSIM frame tables, %u context-index stores (entry kind 0: %u, kind 3 (catch): %u, kind 4: %u, kind 5 "
           "(destructors): %u; no __except entry): __ExceptionHandler of the DLL and of CW3220 both continue the search for %u "
           "exceptions other than a C++ throw, outside an unwind, and write nothing\n", ro_tables_seen, entries, ro_kinds[0], ro_kinds[3],
           ro_kinds[4], ro_kinds[5], ro_handler_calls);
    printf("PASS: rtl_own throw (H6-10): a C++ throw (0EEFFACEh, search phase) through %u context entries without a catch: both "
           "handlers continue the search and write nothing (%u calls); %u entr%s DBSIM's only catch (terminate's catch-all), "
           "which the original takes (global unwind, jump to the catch block) and the DLL does not: not reproduced\n",
           ro_throw_entries, ro_throw_calls, ro_throw_caught, ro_throw_caught == 1 ? "y reaches" : "ies reach");
    printf("PASS: rtl_own unwind (H6-10): an unwind (flags 2, 4, 6) with nothing constructed since the frame's entry through %u "
           "context entries whose clean-up calls nothing (%u calls): both continue the search; the original only steps the "
           "frame's context word to 0 (40415d), the DLL writes nothing; %u entr%s would run DBSIM clean-up code in the original "
           "(%u through a kind-0 block, %u through destructor entries): the DLL runs no clean-up (not reproduced)\n",
           ro_unwind_entries, ro_unwind_calls, ro_unwind_cleanup, ro_unwind_cleanup == 1 ? "y" : "ies", ro_unwind_block, ro_unwind_destroy);
    return 0;
}

/* ------------------------------------------------------------------------
 * __startup's frame record handler: CW3220's 405160 against the DLL's
 * (es2_own_runtime's startup_handler). Both runtimes' UnhandledExceptionFilter
 * slots (CW3220's [0x424308], called through 41ed9b) at a stub that keeps
 * its pointers and answers a chosen value. CW3220's two _signal words
 * ([0x42341c], [0x423418]) at 0 or at the default 405154 (returns 1), the
 * states DBSIM can produce (it imports no _signal); __stkchk [0x41f818] is 0
 * (its C00000FD branch to __ErrorExit is dead). Codes other than 0EEFFACEh
 * (the C++ throw, which nothing in DBSIM raises). */
static unsigned ro_filter_calls;
static LONG ro_filter_answer;
static const EXCEPTION_POINTERS *ro_filter_seen;
static EXCEPTION_RECORD *ro_filter_record;
static CONTEXT *ro_filter_context;
static LONG WINAPI ro_filter_stub(EXCEPTION_POINTERS *pointers) {
    ++ro_filter_calls;
    ro_filter_seen = pointers;
    ro_filter_record = pointers->ExceptionRecord;
    ro_filter_context = pointers->ContextRecord;
    return ro_filter_answer;
}
static unsigned ro_startup_cases, ro_startup_signal[2], ro_startup_unwinds, ro_startup_answers[3];
static int ro_startup(void) {
    current_function = "rtl_own_startup";
    typedef int (__cdecl *fn_record_handler)(void *, void *, void *, void *);
    fn_record_handler original = (fn_record_handler)(uintptr_t)(rtl_cw + ES2_RTL_FRAME_HANDLER_RVA);
    fn_record_handler own = (fn_record_handler)(uintptr_t)ro_info->startup_handler;
    if (!own) return fail("the DLL's startup handler");
    if (get32(rtl_cw + 0x1f818u)) return fail("CW3220's __stkchk word is not 0");
    uint32_t *cw_slot = ls_cw_slot("KERNEL32.dll", "UnhandledExceptionFilter");
    if (!cw_slot || cw_slot != (uint32_t *)(uintptr_t)(rtl_cw + 0x24308u)) return fail("CW3220's UnhandledExceptionFilter slot");
    uint32_t cw_saved = *cw_slot;
    *cw_slot = (uint32_t)(uintptr_t)ro_filter_stub;
    if (ro_point("KERNEL32.dll", "UnhandledExceptionFilter", (const void *)ro_filter_stub)) { *cw_slot = cw_saved; return 1; }
    volatile uint32_t *signal_words[2] = {(volatile uint32_t *)(uintptr_t)(rtl_cw + 0x2341cu), (volatile uint32_t *)(uintptr_t)(rtl_cw + 0x23418u)};
    uint32_t signal_saved[2] = {*signal_words[0], *signal_words[1]};
    const uint32_t signal_default = (uint32_t)(uintptr_t)(rtl_cw + 0x5154u);
    static const uint32_t codes[] = {0xc0000005u, 0xc0000094u, 0xc0000095u, 0x80000003u, 0xc00000fdu, 0xc0000096u, 0xc000001du, 0xc0000027u, 0x80000004u};
    static const uint32_t flags[] = {0, 1, 2, 4, 6};
    static const LONG answers[] = {0, 1, -1};
    const unsigned codes_n = sizeof(codes) / sizeof(codes[0]), flags_n = sizeof(flags) / sizeof(flags[0]), answers_n = sizeof(answers) / sizeof(answers[0]);
    const unsigned matrix = codes_n * flags_n * answers_n * 4u, cases = matrix + (quick_tier ? 2000u : 20000u);
    int result = 0;
    for (unsigned n = 0; n < cases && !result; ++n) {
        current_case = n; current_case_valid = 1;
        unsigned m = n % matrix, signal = m & 3u;
        uint32_t code, flag;
        LONG answer;
        if (n < matrix) {
            m >>= 2;
            code = codes[m % codes_n]; m /= codes_n;
            flag = flags[m % flags_n]; m /= flags_n;
            answer = answers[m % answers_n];
        } else {
            signal = random32() & 3u;
            do code = random32(); while (code == 0x0eeffaceu);
            flag = random32() & 0x7fu;
            answer = (LONG)random32();
        }
        *signal_words[0] = signal & 1u ? signal_default : 0;
        *signal_words[1] = signal & 2u ? signal_default : 0;
        EXCEPTION_RECORD record, record_before;
        CONTEXT context, context_before;
        uint32_t frame[2] = {0xffffffffu, (uint32_t)(uintptr_t)original}, frame_before[2];
        memset(&record, 0, sizeof(record));
        record.ExceptionCode = code;
        record.ExceptionFlags = flag;
        record.ExceptionAddress = (void *)(uintptr_t)(0x400000u + random32() % 0x2c0000u);
        record.NumberParameters = random32() % 3u;
        for (unsigned i = 0; i < record.NumberParameters; ++i) record.ExceptionInformation[i] = random32();
        memset(&context, 0x5c, sizeof(context));
        memcpy(&record_before, &record, sizeof(record)); memcpy(&context_before, &context, sizeof(context)); memcpy(frame_before, frame, sizeof(frame));
        ro_filter_answer = answer;
        ro_filter_calls = 0; ro_filter_record = NULL; ro_filter_context = NULL;
        int want = original(&record, frame, &context, (void *)0x12345678);
        unsigned want_calls = ro_filter_calls;
        EXCEPTION_RECORD *want_record = ro_filter_record;
        CONTEXT *want_context = ro_filter_context;
        if (memcmp(&record, &record_before, sizeof(record)) || memcmp(&context, &context_before, sizeof(context)) || memcmp(frame, frame_before, sizeof(frame)))
            { result = fail("the original's handler wrote the record, frame or context"); break; }
        ro_filter_calls = 0; ro_filter_record = NULL; ro_filter_context = NULL;
        int got = own(&record, frame, &context, (void *)0x12345678);
        if (memcmp(&record, &record_before, sizeof(record)) || memcmp(&context, &context_before, sizeof(context)) || memcmp(frame, frame_before, sizeof(frame)))
            { result = fail("the DLL's handler wrote the record, frame or context"); break; }
        if (want != got || want != (int)answer || want_calls != 1u || ro_filter_calls != 1u || want_record != &record || ro_filter_record != &record ||
            want_context != &context || ro_filter_context != &context) {
            fprintf(stderr, "FAIL: %s case %u: code %08x flags %x signal %u answer %ld: original %d (%u filter calls), own %d (%u)\n", current_function, n,
                    (unsigned)code, (unsigned)flag, signal, (long)answer, want, want_calls, got, ro_filter_calls);
            result = 1;
            break;
        }
        ++ro_startup_cases;
        ro_startup_signal[signal == 3u] += signal == 0u || signal == 3u;
        ro_startup_unwinds += (flag & 6u) != 0;
        ro_startup_answers[answer == 0 ? 0 : answer == 1 ? 1 : 2]++;
    }
    current_case_valid = 0;
    *signal_words[0] = signal_saved[0]; *signal_words[1] = signal_saved[1];
    *cw_slot = cw_saved;
    ro_unpoint();
    if (result) return 1;
    printf("PASS: rtl_own startup handler: %u cases (%u during an unwind; _signal words both 0: %u, both 405154: %u; filter answers 0: %u, "
           "1: %u, other: %u): the DLL's handler of __startup's record and CW3220's 405160 return what UnhandledExceptionFilter "
           "answers, ask it once with the record and context given, also during an unwind, and write nothing\n", ro_startup_cases,
           ro_startup_unwinds, ro_startup_signal[0], ro_startup_signal[1], ro_startup_answers[0], ro_startup_answers[1], ro_startup_answers[2]);
    return 0;
}

/* ------------------------------------------------------------------------
 * _atexit from a foreign thread and @_CatchCleanup$qv. */
__attribute__((used)) static uint32_t ro_x_target, ro_x_argument, ro_x_out[3];
__asm__(
    /* cdecl void ro_x_call1(void): target(argument) with ECX/EDX preset;
     * EAX, ECX, EDX after it. */
    ".text\n_ro_x_call1:\n"
    "    pushl %ebx\n    pushl %esi\n    pushl %edi\n    pushl %ebp\n"
    "    movl $0x0c0c0c0c, %ecx\n    movl $0x0d0d0d0d, %edx\n"
    "    pushl _ro_x_argument\n    call *_ro_x_target\n    addl $4, %esp\n"
    "    movl %eax, _ro_x_out\n    movl %ecx, _ro_x_out+4\n    movl %edx, _ro_x_out+8\n"
    "    popl %ebp\n    popl %edi\n    popl %esi\n    popl %ebx\n    ret\n");
extern void __cdecl ro_x_call1(void) __asm__("_ro_x_call1");

static unsigned ro_atexit_cases, ro_atexit_full;
static int ro_atexit_result;
/* On the foreign thread: the same registration on both runtimes from the same
 * table state; registers, count and table compared. */
static DWORD WINAPI ro_atexit_thread(void *parameter) {
    unsigned cases = (unsigned)(uintptr_t)parameter;
    uint32_t original = rtl_cw_export("_atexit"), own = ro_export("_atexit");
    if (!original || !own) { ro_atexit_result = fail("_atexit exports"); return 0; }
    es2_rtl_runtime *m = ro_info->lifecycle;
    for (unsigned n = 0; n < cases; ++n) {
        current_case = n; current_case_valid = 1;
        uint32_t count = n % 7u == 0 ? ES2_RTL_ATEXIT_LIMIT : n % 7u == 1 ? ES2_RTL_ATEXIT_LIMIT - 1u : random32() % (ES2_RTL_ATEXIT_LIMIT + 1u);
        uint32_t table[ES2_RTL_ATEXIT_LIMIT], function = 0x00400000u + random32() % 0x2c0000u;
        for (unsigned i = 0; i < ES2_RTL_ATEXIT_LIMIT; ++i) table[i] = random32();
        *ls_var(LS_ATEXIT_COUNT) = count;
        memcpy((void *)(uintptr_t)ls_var(LS_ATEXIT_TABLE), table, sizeof(table));
        m->atexit_count = count;
        memcpy(m->atexit_table, table, sizeof(table));
        ro_x_argument = function;
        ro_x_target = original; ro_x_call1();
        uint32_t want[3] = {ro_x_out[0], ro_x_out[1], ro_x_out[2]};
        ro_x_target = own; ro_x_call1();
        if (memcmp(want, ro_x_out, sizeof(want))) {
            fprintf(stderr, "FAIL: %s case %u: count %u: original EAX/ECX/EDX %08x %08x %08x, own %08x %08x %08x\n", current_function, n,
                    (unsigned)count, want[0], want[1], want[2], ro_x_out[0], ro_x_out[1], ro_x_out[2]);
            ro_atexit_result = 1; return 0;
        }
        if (m->atexit_count != *ls_var(LS_ATEXIT_COUNT) || memcmp(m->atexit_table, (const void *)(uintptr_t)ls_var(LS_ATEXIT_TABLE), sizeof(table))) {
            ro_atexit_result = fail("the table after a foreign registration"); return 0;
        }
        ro_atexit_full += want[0] != 0;
        ++ro_atexit_cases;
    }
    current_case_valid = 0;
    return 0;
}

static int ro_catch_result;
static unsigned ro_catch_calls;
/* On the foreign thread: @_CatchCleanup$qv with the context word at
 * StackBase-8 absent (0) and naming a context block whose exception list is
 * empty (DBSIM's terminate after its catch-all with nothing thrown). The two
 * words below StackBase are restored afterwards. */
static DWORD WINAPI ro_catch_thread(void *parameter) {
    (void)parameter;
    void (__cdecl *cleanup)(void) = (void (__cdecl *)(void))(uintptr_t)ro_export("@_CatchCleanup$qv");
    if (!cleanup) { ro_catch_result = fail("@_CatchCleanup$qv export"); return 0; }
    volatile uint32_t *below = (volatile uint32_t *)((BYTE *)((NT_TIB *)NtCurrentTeb())->StackBase - 8);
    uint32_t saved[2] = {below[0], below[1]};
    uint32_t block[16], before[16];
    for (unsigned i = 0; i < 16; ++i) block[i] = i ? random32() : 0u;   /* +0: the exception list, empty */
    memcpy(before, block, sizeof(block));
    uint64_t declined = ro_info->counters->declined;
    below[0] = 0; below[1] = 0x77777777u;
    cleanup(); ++ro_catch_calls;
    if (below[0] != 0 || below[1] != 0x77777777u) ro_catch_result = fail("@_CatchCleanup$qv without a context block wrote below StackBase");
    below[0] = (uint32_t)(uintptr_t)block;
    cleanup(); ++ro_catch_calls;
    if (memcmp(block, before, sizeof(block)) || below[0] != (uint32_t)(uintptr_t)block) ro_catch_result = fail("@_CatchCleanup$qv with an empty list wrote");
    if (ro_info->counters->declined != declined) ro_catch_result = fail("@_CatchCleanup$qv declined without an exception");
    below[0] = saved[0]; below[1] = saved[1];
    return 0;
}

static int ro_foreign(void) {
    current_function = "rtl_own_foreign";
    /* The owner is the harness's private stack (as for the pure pass 0): the
     * thread below runs on its own stack, so every call is foreign. */
    if (ro_reinit(0, 0, 0, 0, RO_STACK_LO, RO_STACK_HI, ES2_OWN_SETUP_NO_IO_START)) return 1;
    uint32_t saved_count = *ls_var(LS_ATEXIT_COUNT), saved_table[ES2_RTL_ATEXIT_LIMIT];
    memcpy(saved_table, (const void *)(uintptr_t)ls_var(LS_ATEXIT_TABLE), sizeof(saved_table));
    unsigned cases = quick_tier ? 700u : 7000u;
    HANDLE thread = CreateThread(NULL, 0, ro_atexit_thread, (void *)(uintptr_t)cases, 0, NULL);
    if (!thread) return fail("thread");
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    *ls_var(LS_ATEXIT_COUNT) = saved_count;
    memcpy((void *)(uintptr_t)ls_var(LS_ATEXIT_TABLE), saved_table, sizeof(saved_table));
    if (ro_atexit_result) return 1;
    const es2_own_counters *k = ro_info->counters;
    if (k->calls || k->nested_calls || k->foreign_calls != cases || k->atexit_registered + k->atexit_refused != cases || k->atexit_refused != ro_atexit_full)
        return fail("_atexit's adapter path or registration counters");
    thread = CreateThread(NULL, 0, ro_catch_thread, NULL, 0, NULL);
    if (!thread) return fail("thread");
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    if (ro_catch_result) return 1;
    printf("PASS: rtl_own foreign (H6-10): %u _atexit registrations from a foreign thread (%u on a full table) equal to CW3220's in "
           "EAX/ECX/EDX, count and table, all counted foreign (caller's stack); @_CatchCleanup$qv on a foreign thread without a "
           "context block and with an empty exception list returns without a write or a decline (%u calls; the original reads "
           "through the empty list at 403ea6)\n", ro_atexit_cases, ro_atexit_full, ro_catch_calls);
    return 0;
}

/* ------------------------------------------------------------------------
 * Selection: --rtl-own. */
static int rtl_own_cases(const char *selection) {
    current_function = strcmp(selection, "--rtl-own") ? selection : "rtl_own";
    if (rtl_map_cw3220(getenv("ES2_DATA_ROOT")) || ro_load_dll()) return 1;
    io_data_pristine = malloc(IO_DATA_SIZE);
    if (!io_data_pristine) return fail("memory");
    memcpy(io_data_pristine, rtl_cw + IO_DATA_RVA, IO_DATA_SIZE);
    static uint32_t dbsim_saved[RTL_IMPORT_COUNT];
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) dbsim_saved[i] = *(uint32_t *)(mapped + RTL_IAT_RVA + 4 * i);
    if (ro_tables() || ro_pure() || ro_heap() || ro_io() || ro_life() || ro_exceptions() || ro_startup() || ro_foreign()) return 1;
    current_function = "rtl_own";
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) *(uint32_t *)(mapped + RTL_IAT_RVA + 4 * i) = dbsim_saved[i];
    if (GetModuleHandleA(ES2_OWN_ORIGINAL_FILE)) return fail("the original runtime was loaded");
    if (ro_info->counters->delegated) return fail("the DLL delegated a call");
    puts("PASS: rtl_own in-vitro run of the own cw3220.dll against the mapped CW3220.DLL; no entry point of it executed; no game process started");
    return 0;
}
