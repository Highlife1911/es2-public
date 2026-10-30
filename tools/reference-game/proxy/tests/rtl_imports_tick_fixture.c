/* The CW3220 import shim's tick and exit_counters fields (proxy/
 * rtl_imports.h through proxy/tick_log.h). Synthetic image, own counters and
 * an in-memory sink, as in exit_counters_fixture.c; no game code runs. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static WCHAR config_path[MAX_PATH];
static void log_event(const char *event) { (void)event; }
#include "../rng_hooks.h"
#define ES2_RTL_NO_PROCESS_ENTRY
#include "../rtl_imports.h"
static void rtl_arm(void) {}

static char sink[8 * 1024 * 1024];
static DWORD sink_size;
static BOOL WINAPI fake_qpc(LARGE_INTEGER *value) { value->QuadPart = 987654321; return TRUE; }
static BOOL WINAPI fake_write(HANDLE file, const void *data, DWORD size, DWORD *written, OVERLAPPED *overlap) {
    (void)file; (void)overlap;
    if (size >= sizeof(sink) - sink_size) { *written = 0; return FALSE; }
    memcpy(sink + sink_size, data, size); sink_size += size; sink[sink_size] = 0;
    *written = size; return TRUE;
}
#define QueryPerformanceCounter fake_qpc
#define WriteFile fake_write
#include "../tick_log.h"
#undef WriteFile
#undef QueryPerformanceCounter
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static BYTE *image, *context;
static char fields[TICK_LOG_LINE_MAX];
static unsigned count(const char *text, const char *needle) {
    unsigned n = 0;
    for (const char *p = text; (p = strstr(p, needle)); p += strlen(needle)) ++n;
    return n;
}
static const char *last_line(void) {
    const char *p = sink + sink_size - 2;
    while (p > sink && p[-1] != '\n') --p;
    return p;
}
static void frame(DWORD value) { memcpy(context + 0xbf, &value, 4); }
static int setup(BOOL hooks, LONG rtl) {
    image = VirtualAlloc(NULL, TICK_LOG_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(image);
    context = image + TICK_LOG_CTX_RVA;
    tick_log_image = image; tick_log_state = 1; tick_log_file = (HANDLE)(uintptr_t)1;
    tick_log_limit = 1000000; tick_log_sequence = 0;
    rng_hooks_configured = hooks;
    for (unsigned i = 0; i < FUNCTION_HOOK_COUNT; ++i) {
        rng_hooks[i].options.mode = ES2_HOOK_REPLACED;
        rng_hooks[i].calls = 1000 + i; rng_hooks[i].bypassed = 0;
    }
    rtl_state = rtl;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        rtl_installed[i] = rtl_imports[i].shim;
        rtl_counter c = {10u + i, 0, 1, 2, 3, 4u + i};
        c.calls = c.hook + c.sim + c.dbsim + c.other;
        rtl_counters[i] = c;
    }
    rtl_foreign_calls = 5;
    return 0;
}
static unsigned shimmed(void) { unsigned n = 0; for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) n += rtl_imports[i].shim; return n; }

/* Only the import shim: ticks carry the calls, one full exit record. */
static int fields_case(void) {
    CHECK(!setup(FALSE, RTL_INSTALLED));
    for (DWORD f = 0; f < 2; ++f) { frame(100 + f); es2_tick_record(context); }
    CHECK(count(sink, "\"event\":\"tick\"") == 2 && !strstr(sink, "\"hk_") && !strstr(sink, "exit_counters"));
    CHECK(count(sink, "_calls\":") == 2 * (shimmed() + 1) && !strstr(sink, "_hook\":") && !strstr(sink, "rtl_atexit"));
    int n = rtl_tick_fields(fields, sizeof(fields), FALSE);
    CHECK(n > 0 && count(sink, fields) == 2);
    CHECK(strstr(sink, ",\"rtl_memset_calls\":43,") && strstr(sink, ",\"rtl_foreign_calls\":5}\r\n"));
    DWORD ticks = sink_size;
    tick_log_shutdown();
    const char *exit_line = sink + ticks;
    CHECK(last_line() == exit_line && count(sink, "exit_counters") == 1 && !strstr(exit_line, "counters_overflow"));
    const char *head = "{\"event\":\"exit_counters\",\"reason\":\"shutdown\",\"last_sequence\":2,\"frame_counter\":101,\"rtl_";
    CHECK(!strncmp(exit_line, head, strlen(head)));
    CHECK(count(exit_line, "\"rtl_") == 6 * shimmed() + 1 && !strstr(exit_line, "\"hk_"));
    CHECK(strstr(exit_line, ",\"rtl_memset_calls\":43,\"rtl_memset_mismatches\":0,\"rtl_memset_hook\":1,"
                            "\"rtl_memset_sim\":2,\"rtl_memset_dbsim\":3,\"rtl_memset_other\":37,"));
    CHECK(strstr(exit_line, ",\"rtl_CatchCleanup_calls\":") && !strstr(exit_line, "rtl_startup") && !strstr(exit_line, "rtl_errno"));
    printf("PASS fields: %u imports, ticks with calls only, one exit record with 6 counters each (%lu bytes)\n",
           shimmed(), (unsigned long)strlen(exit_line));
    return 0;
}

/* Shim off: the log is exactly as before (no rtl field, no exit record
 * without [hooks]; with [hooks] the record is the hook fields alone). */
static int off_case(void) {
    CHECK(!setup(FALSE, RTL_OFF));
    for (DWORD f = 0; f < 2; ++f) { frame(f); es2_tick_record(context); }
    DWORD ticks = sink_size;
    tick_log_shutdown();
    CHECK(sink_size == ticks && !strstr(sink, "rtl_") && !strstr(sink, "exit_counters"));
    sink_size = 0; sink[0] = 0;
    CHECK(!setup(TRUE, RTL_OFF));
    tick_log_exit_written = FALSE;
    frame(7); es2_tick_record(context);
    ticks = sink_size;
    tick_log_shutdown();
    const char *exit_line = sink + ticks;
    int n = rng_hooks_tick_fields(fields, sizeof(fields));
    CHECK(n > 0 && !strstr(sink, "rtl_"));
    const char *head = "{\"event\":\"exit_counters\",\"reason\":\"shutdown\",\"last_sequence\":1,\"frame_counter\":7";
    CHECK(!strncmp(exit_line, head, strlen(head)) && !strncmp(exit_line + strlen(head), fields, (size_t)n) &&
          !strcmp(exit_line + strlen(head) + n, "}\r\n"));
    puts("PASS off: without the shim no rtl field; the exit record is unchanged (none without [hooks])");
    return 0;
}

static int refused_case(void) {
    CHECK(!setup(FALSE, RTL_REFUSED));
    rtl_refuse("slot", "memcpy");
    frame(3); es2_tick_record(context);
    CHECK(strstr(sink, ",\"rtl_refused\":\"slot_memcpy\"}\r\n") && !strstr(sink, "_calls"));
    DWORD ticks = sink_size;
    tick_log_shutdown();
    CHECK(!strcmp(sink + ticks, "{\"event\":\"exit_counters\",\"reason\":\"shutdown\",\"last_sequence\":1,\"frame_counter\":3,"
                               "\"rtl_refused\":\"slot_memcpy\"}\r\n"));
    /* Still pending at a tick: the entry never went through the slot. */
    sink_size = 0; sink[0] = 0;
    CHECK(!setup(FALSE, RTL_PENDING));
    tick_log_exit_written = FALSE;
    frame(4); es2_tick_record(context);
    CHECK(strstr(sink, ",\"rtl_refused\":\"not_armed\"}\r\n"));
    puts("PASS refused: a refused (or never armed) request is reported in every tick and in the exit record");
    return 0;
}

/* Every hook replaced and every counter at UINT64_MAX, plus all 47 imports. */
static int bound_case(void) {
    CHECK(!setup(TRUE, RTL_INSTALLED));
    for (unsigned i = 0; i < FUNCTION_HOOK_COUNT; ++i) {
        rng_hooks[i].calls = rng_hooks[i].bypassed = UINT64_MAX;
        rng_hooks[i].mismatches = rng_hooks[i].skipped = 0;
        unsigned n; callee_counter *c = weapons_graph_counters(i, &n);
        for (unsigned j = 0; j < n; ++j) c[j].count = UINT64_MAX;
    }
    for (unsigned i = 0; i < GROUND_UPDATE_COUNTERS; ++i) ground_update_counters[i].count = UINT64_MAX;
    for (unsigned i = 0; i < GROUND_COLLIDE_COUNTERS; ++i) ground_collide_counters[i].count = UINT64_MAX;
    for (unsigned i = 0; i < GROUND_EFFECTS_COUNTERS; ++i) ground_effects_counters[i].count = UINT64_MAX;
    for (unsigned i = 0; i < VEHICLE_HIT_COUNTERS; ++i) vehicle_hit_counters[i].count = UINT64_MAX;
    for (unsigned i = 0; i < BUILDING_HIT_COUNTERS; ++i) building_hit_counters[i].count = UINT64_MAX;
    for (unsigned i = 0; i < RADIUS_HIT_COUNTERS; ++i) radius_hit_counters[i].count = UINT64_MAX;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        rtl_counter c = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
        rtl_counters[i] = c;
    }
    rtl_foreign_calls = UINT64_MAX;
    /* The 20 own imports in compare mode with their skipped counters
     * and the longest first-mismatch detail. */
    unsigned own = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i)
        if (rtl_own_function(i) >= 0) { rtl_modes[i] = ES2_HOOK_COMPARE; rtl_skipped[i] = UINT64_MAX; ++own; }
    memset(rtl_mismatch_detail, 'm', sizeof(rtl_mismatch_detail) - 1);
    rtl_mismatch_detail[sizeof(rtl_mismatch_detail) - 1] = 0;
    /* The three heap imports in shadow with every heap counter at its
     * maximum and the longest first heap mismatch. */
    rtl_shadowed[RTL_I_calloc] = rtl_shadowed[RTL_I_realloc] = rtl_shadowed[RTL_I_free] = 1;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) rtl_heap_import_mismatches[i] = UINT64_MAX;
    rtl_heap_active = TRUE;
    rtl_heap_internal_ops = rtl_heap_external_ops = rtl_heap_detour_mismatches = rtl_heap_foreign_ops = UINT64_MAX;
    rtl_heap_os_calls = rtl_heap_os_unobserved = rtl_heap_skipped = rtl_heap_resyncs = UINT64_MAX;
    rtl_heap_attach_blocks = INT32_MIN;
    rtl_heap_model.region_count = UINT32_MAX;
    memset(rtl_heap_mismatch_detail, 'h', sizeof(rtl_heap_mismatch_detail) - 1);
    rtl_heap_mismatch_detail[sizeof(rtl_heap_mismatch_detail) - 1] = 0;
    /* The 18 file imports in shadow, the 3 console stubs replaced,
     * every I/O counter at its maximum and the longest first I/O mismatch. */
    unsigned io_shadow = 0, io_stubs = 0;
    for (unsigned i = 0; i < RTL_IMPORT_COUNT; ++i) {
        int f = rtl_io_function_of(i);
        if (f < 0) continue;
        if (es2_io_is_stub((es2_io_function)f)) { rtl_modes[i] = ES2_HOOK_REPLACED; rtl_skipped[i] = UINT64_MAX; ++io_stubs; continue; }
        rtl_io_shadowed[i] = 1; rtl_io_mismatches[i] = rtl_io_shadow_skipped[i] = UINT64_MAX; ++io_shadow;
    }
    rtl_io_active = TRUE;
    rtl_io_os_calls = UINT64_MAX;
    memset(rtl_io_mismatch_detail, 'i', sizeof(rtl_io_mismatch_detail) - 1);
    rtl_io_mismatch_detail[sizeof(rtl_io_mismatch_detail) - 1] = 0;
    /* The own heap's fields at their longest (a real run has either
     * the shadow or the own heap; both here give an upper bound). */
    rtl_heap_own_active = TRUE;
    rtl_heap_foreign_frees = rtl_heap_own_os_calls = rtl_heap_own_os_failed = UINT64_MAX;
    rtl_heap_own_blocks = INT32_MIN;
    rtl_heap_own.region_count = UINT32_MAX;
    /* The lifecycle group with its four slot imports installed in mode
     * replaced (atexit and exit added to the shimmed ones) and every
     * lifecycle counter at its longest. */
    static const unsigned life[4] = {RTL_I_atexit, RTL_I_exit, RTL_I_abort, RTL_I_ErrorExit};
    for (unsigned k = 0; k < 4; ++k) {
        rtl_installed[life[k]] = 1; rtl_modes[life[k]] = ES2_HOOK_REPLACED; rtl_skipped[life[k]] = UINT64_MAX;
    }
    rtl_life_active = TRUE;
    rtl_life_init_records = rtl_life_winmain = rtl_life_registered = rtl_life_refused = rtl_life_atexit_run = UINT64_MAX;
    rtl_life_exit_records = rtl_life_hooks = rtl_life_signals = UINT64_MAX;
    rtl_life_status = INT64_MIN;
    rtl_life_rt.atexit_count = UINT32_MAX;
    /* The own runtime's fields at their longest (a real run with the
     * lifecycle group has CW3220.DLL; here both give an upper bound). */
    static es2_own_counters own_counters;
    static es2_own_runtime_info own_info;
    own_counters.calls = own_counters.foreign_calls = own_counters.nested_calls = UINT64_MAX;
    own_counters.declined = own_counters.delegated = UINT64_MAX;
    own_counters.exit_status = INT64_MIN;
    own_info.version = ES2_OWN_RUNTIME_VERSION;
    own_info.counters = &own_counters;
    rtl_own_runtime = &own_info;
    rtl_runtime_known = TRUE;
    rtl_life_cw = VirtualAlloc(NULL, RTL_HEAP_CW_IMAGE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(rtl_life_cw);
    memset(rtl_life_cw + RTL_LIFE_ATEXIT_COUNT, 0xff, 4);
    frame(UINT32_MAX);
    es2_tick_record(context);
    tick_log_sequence = UINT32_MAX;
    size_t tick_size = strlen(sink);
    /* Each tick: the calls, rtl_foreign_calls, the shadow totals,
     * the foreign frees, atexit's and exit's calls and the
     * lifecycle's two. */
    if (count(sink, "\"rtl_") != shimmed() + 2 + 4 + 2) fprintf(stderr, "tick %lu bytes, %u rtl fields\n", (unsigned long)tick_size, count(sink, "\"rtl_"));
    CHECK(count(sink, "\"rtl_") == shimmed() + 2 + 4 + 2 && strstr(sink, ",\"rtl_heap_shadow_mismatches\":18446744073709551615,") &&
          strstr(sink, ",\"rtl_heap_foreign_frees\":18446744073709551615,") &&
          strstr(sink, ",\"rtl_io_shadow_mismatches\":18446744073709551615,") &&
          strstr(sink, ",\"rtl_life_init_records\":18446744073709551615,\"rtl_life_winmain\":18446744073709551615}"));
    DWORD ticks = sink_size;
    tick_log_shutdown();
    const char *exit_line = sink + ticks;
    CHECK(count(exit_line, "exit_counters") == 1 && !strstr(exit_line, "counters_overflow"));
    /* 6 per import, 20 skipped, foreign calls, first mismatch; 3 shadow
     * mismatches, their total, the 10 heap counters and the first heap
     * mismatch. */
    /* 2 per shadowed file import, the stubs' skipped, the I/O total,
     * the recorded calls and the first I/O mismatch; the foreign
     * frees and the own heap's 4 fields; 6 for atexit and exit each,
     * the group's 4 skipped and the lifecycle's 11; the runtime's 8. */
    CHECK(own == 20 && io_shadow == 18 && io_stubs == 3 &&
          count(exit_line, "\"rtl_") == 6 * shimmed() + own + 2 + 3 + 1 + 11 + 2 * io_shadow + io_stubs + 1 + 2 + 1 + 4 + 6 * 2 + 4 + 11 + 8 &&
          count(exit_line, "\"hk_") > 2 * FUNCTION_HOOK_COUNT - 1);
    CHECK(strstr(exit_line, ",\"rtl_own_delegated\":18446744073709551615,\"rtl_own_exit_status\":-9223372036854775808"));
    CHECK(strstr(exit_line, ",\"rtl_atexit_skipped\":18446744073709551615,") &&
          strstr(exit_line, ",\"rtl_life_exit_status\":-9223372036854775808,\"rtl_life_cw_atexit\":4294967295,\"rtl_runtime\":\"own\","));
    CHECK(strstr(exit_line, ",\"rtl_read_shadow_mismatches\":18446744073709551615,\"rtl_read_shadow_skipped\":18446744073709551615,") &&
          strstr(exit_line, ",\"rtl_printf_skipped\":18446744073709551615,") && strstr(exit_line, ",\"rtl_io_first_mismatch\":\"iii"));
    CHECK(strstr(exit_line, ",\"rtl_vsprintf_skipped\":18446744073709551615,") && strstr(exit_line, ",\"rtl_first_mismatch\":\"mmm"));
    CHECK(strstr(exit_line, ",\"rtl_calloc_shadow_mismatches\":18446744073709551615,") && strstr(exit_line, ",\"rtl_heap_first_mismatch\":\"hhh") &&
          strstr(exit_line, ",\"rtl_heap_attach_blocks\":-2147483648,\"rtl_heap_regions\":4294967295,"));
    CHECK(strstr(exit_line, ",\"rtl_heap_own_regions\":4294967295,\"rtl_heap_own_blocks\":-2147483648,"
                            "\"rtl_heap_own_os_calls\":18446744073709551615,\"rtl_heap_own_os_failed\":18446744073709551615"));
    CHECK(strlen(exit_line) < TICK_LOG_LINE_MAX && tick_size < TICK_LOG_LINE_MAX);
    printf("PASS bound: maximum tick %lu bytes, maximum exit record %lu bytes < %u (all hooks + %u imports, %u in compare, 3 heap and %u I/O in shadow, %u stubs, own heap, lifecycle group)\n",
           (unsigned long)tick_size, (unsigned long)strlen(exit_line), TICK_LOG_LINE_MAX, shimmed() + 2, own, io_shadow, io_stubs);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "fields")) return fields_case();
    if (!strcmp(argv[1], "off")) return off_case();
    if (!strcmp(argv[1], "refused")) return refused_case();
    if (!strcmp(argv[1], "bound")) return bound_case();
    return 2;
}
