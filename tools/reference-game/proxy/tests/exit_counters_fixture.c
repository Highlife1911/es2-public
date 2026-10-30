/* The exit-time hook-counter record of proxy/tick_log.h. Synthetic
 * image, own counters and an in-memory sink; no original code or game runs. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static WCHAR config_path[MAX_PATH];
static void log_event(const char *event) { (void)event; }
#include "../rng_hooks.h"

static char ec_sink[4 * 1024 * 1024];
static DWORD ec_sink_size;
static BOOL WINAPI ec_qpc(LARGE_INTEGER *value) { value->QuadPart = 987654321; return TRUE; }
static BOOL WINAPI ec_write(HANDLE file, const void *data, DWORD size, DWORD *written, OVERLAPPED *overlap) {
    (void)file; (void)overlap;
    if (size >= sizeof(ec_sink) - ec_sink_size) { *written = 0; return FALSE; }
    memcpy(ec_sink + ec_sink_size, data, size); ec_sink_size += size; ec_sink[ec_sink_size] = 0;
    *written = size; return TRUE;
}
#define QueryPerformanceCounter ec_qpc
#define WriteFile ec_write
#include "../tick_log.h"
#undef WriteFile
#undef QueryPerformanceCounter
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static BYTE *ec_image, *ec_context;
static char ec_fields[TICK_LOG_LINE_MAX];

static unsigned ec_count(const char *text, const char *needle) {
    unsigned n = 0;
    for (const char *p = text; (p = strstr(p, needle)); p += strlen(needle)) ++n;
    return n;
}
/* Start of the last CRLF-terminated line of the sink. */
static const char *ec_last_line(void) {
    if (ec_sink_size < 2) return ec_sink;
    const char *p = ec_sink + ec_sink_size - 2;
    while (p > ec_sink && p[-1] != '\n') --p;
    return p;
}
static void ec_frame(DWORD frame) { memcpy(ec_context + 0xbf, &frame, 4); }
static int ec_setup(BOOL hooks) {
    ec_image = VirtualAlloc(NULL, TICK_LOG_IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(ec_image);
    ec_context = ec_image + TICK_LOG_CTX_RVA;
    tick_log_image = ec_image; tick_log_state = 1; tick_log_file = (HANDLE)(uintptr_t)1;
    tick_log_limit = 1000000; tick_log_sequence = 0;
    rng_hooks_configured = hooks;
    for (unsigned i = 0; i < FUNCTION_HOOK_COUNT; ++i) {
        rng_hooks[i].options.mode = ES2_HOOK_REPLACED;
        rng_hooks[i].calls = 1000 + i; rng_hooks[i].bypassed = i & 1;
    }
    return 0;
}
static unsigned ec_graph_fields(void) {
    unsigned fields = 0;
    for (unsigned hook = 0; hook < FUNCTION_HOOK_COUNT; ++hook) { unsigned count; weapons_graph_counters(hook, &count); fields += count; }
    return fields;
}
/* Every hk_* field a tick carries with all hooks replaced (the bound case of
 * path_counters_fixture.c). */
static unsigned ec_expected_fields(void) {
    return 2 * FUNCTION_HOOK_COUNT + ec_graph_fields() + GROUND_UPDATE_COUNTERS + GROUND_COLLIDE_COUNTERS +
           GROUND_EFFECTS_COUNTERS + VEHICLE_HIT_COUNTERS + BUILDING_HIT_COUNTERS + RADIUS_HIT_COUNTERS;
}
/* Hooks that run after the last tick: the result writer's damage_score and
 * result_conditions (both callee counters). */
static int ec_after_last_tick(void) {
    unsigned count; callee_counter *c = weapons_graph_counters(ES2_HOOK_INDEX_result_conditions, &count);
    CHECK(count == 2);
    rng_hooks[ES2_HOOK_INDEX_damage_score].calls = 7777;
    rng_hooks[ES2_HOOK_INDEX_result_conditions].calls = 1;
    c[0].count = 5; c[1].count = 1;
    return 0;
}
/* The exit line is the header plus exactly the fields a tick would carry now. */
static int ec_check_exit_line(const char *line, const char *reason, unsigned long sequence, unsigned long frame) {
    char head[160];
    snprintf(head, sizeof(head), "{\"event\":\"exit_counters\",\"reason\":\"%s\",\"last_sequence\":%lu,\"frame_counter\":%lu,",
             reason, sequence, frame);
    CHECK(!strncmp(line, head, strlen(head)));
    int n = rng_hooks_tick_fields(ec_fields, sizeof(ec_fields));
    CHECK(n > 0);
    const char *fields = line + strlen(head) - 1;
    CHECK(!strncmp(fields, ec_fields, (size_t)n) && !strcmp(fields + n, "}\r\n"));
    CHECK(ec_count(line, "\"hk_") == ec_expected_fields());
    CHECK(strlen(line) < TICK_LOG_LINE_MAX && !strstr(line, "counters_overflow"));
    return 0;
}

static int ec_shutdown_case(void) {
    CHECK(!ec_setup(TRUE));
    for (DWORD f = 0; f < 3; ++f) { ec_frame(1000 + f); es2_tick_record(ec_context); }
    CHECK(ec_count(ec_sink, "\"event\":\"tick\"") == 3 && !strstr(ec_sink, "exit_counters"));
    DWORD ticks_size = ec_sink_size;
    char *ticks = malloc(ticks_size + 1);
    CHECK(ticks); memcpy(ticks, ec_sink, ticks_size + 1);
    CHECK(strstr(ticks, "\"hk_damage_score_calls\":") && !strstr(ticks, "\"hk_damage_score_calls\":7777"));
    CHECK(!ec_after_last_tick());
    ec_frame(1003);
    tick_log_shutdown();
    /* Ticks are untouched; exactly one record follows, as the final line. */
    CHECK(ec_sink_size > ticks_size && !memcmp(ec_sink, ticks, ticks_size));
    CHECK(ec_count(ec_sink, "\"event\":\"exit_counters\"") == 1 && ec_count(ec_sink + ticks_size, "\r\n") == 1);
    CHECK(ec_last_line() == ec_sink + ticks_size);
    CHECK(!ec_check_exit_line(ec_sink + ticks_size, "shutdown", 3, 1003));
    CHECK(strstr(ec_sink + ticks_size, "\"hk_damage_score_calls\":7777,"));
    CHECK(strstr(ec_sink + ticks_size, "\"hk_result_conditions_calls\":1,"));
    CHECK(strstr(ec_sink + ticks_size, "\"hk_result_conditions_call_41b4ec\":5,"));
    /* A second exit callback (atexit, then _exit) or a reopened state writes nothing. */
    DWORD final_size = ec_sink_size;
    tick_log_shutdown();
    tick_log_file = (HANDLE)(uintptr_t)1; tick_log_exit_record("shutdown"); tick_log_flush();
    CHECK(ec_sink_size == final_size);
    free(ticks);
    printf("PASS shutdown: 3 ticks unchanged, one final exit_counters record, %u hk fields, %lu bytes\n",
           ec_expected_fields(), (unsigned long)strlen(ec_sink + ticks_size));
    return 0;
}

static int ec_runner_case(void) {
    CHECK(!ec_setup(TRUE));
    CHECK(tick_io_start(0, 1000));
    for (DWORD f = 0; f < 2; ++f) { ec_frame(2000 + f); es2_tick_record(ec_context); }
    CHECK(!ec_after_last_tick());
    ec_frame(2002);
    /* Request-TickFlush (OracleSpeed.psm1): request, then wait for the ack. */
    CHECK(SetEvent(tick_io_request) && WaitForSingleObject(tick_io_ack, 5000) == WAIT_OBJECT_0);
    tick_io_enter();
    CHECK(!tick_log_used && ec_count(ec_sink, "\"event\":\"exit_counters\"") == 1);
    const char *exit_line = ec_last_line();
    CHECK(!ec_check_exit_line(exit_line, "runner_flush", 2, 2002));
    DWORD exit_end = ec_sink_size;
    tick_io_leave();
    /* The game runs on until the stop: later ticks are still logged; the
     * clean exit afterwards adds no second record. */
    ec_frame(2002); es2_tick_record(ec_context);
    tick_log_shutdown();
    CHECK(ec_count(ec_sink, "\"event\":\"exit_counters\"") == 1 && ec_count(ec_sink, "\"event\":\"tick\"") == 3);
    CHECK(strstr(ec_sink + exit_end, "\"sequence\":3,"));
    CHECK(WaitForSingleObject(tick_io_worker, 3000) == WAIT_OBJECT_0);
    puts("PASS runner: flush request writes the record before the acknowledgement, once");
    return 0;
}

static int ec_no_hooks_case(void) {
    CHECK(!ec_setup(FALSE));
    for (DWORD f = 0; f < 2; ++f) { ec_frame(f); es2_tick_record(ec_context); }
    DWORD ticks_size = ec_sink_size;
    tick_log_shutdown();
    CHECK(ec_sink_size == ticks_size && !strstr(ec_sink, "\"hk_") && !strstr(ec_sink, "exit_counters"));
    puts("PASS no_hooks: without [hooks] the log is unchanged (no exit record)");
    return 0;
}

static int ec_limit_case(void) {
    CHECK(!ec_setup(TRUE));
    tick_log_limit = 2;
    for (DWORD f = 0; f < 3; ++f) { ec_frame(f); es2_tick_record(ec_context); }
    CHECK(tick_log_state == 3 && tick_log_file == INVALID_HANDLE_VALUE);
    DWORD size = ec_sink_size;
    tick_log_shutdown();
    CHECK(ec_sink_size == size && !strstr(ec_sink, "exit_counters"));
    CHECK(!strcmp(ec_last_line(), "{\"event\":\"limit_reached\"}\r\n"));
    puts("PASS limit: a closed log (limit_reached) gets no exit record");
    return 0;
}

static int ec_bound_case(void) {
    CHECK(!ec_setup(TRUE));
    for (unsigned i = 0; i < FUNCTION_HOOK_COUNT; ++i) {
        rng_hooks[i].calls = rng_hooks[i].bypassed = UINT64_MAX;
        rng_hooks[i].mismatches = rng_hooks[i].skipped = 0;
        unsigned count; callee_counter *c = weapons_graph_counters(i, &count);
        for (unsigned j = 0; j < count; ++j) c[j].count = UINT64_MAX;
    }
    for (unsigned i = 0; i < GROUND_UPDATE_COUNTERS; ++i) ground_update_counters[i].count = UINT64_MAX;
    for (unsigned i = 0; i < GROUND_COLLIDE_COUNTERS; ++i) ground_collide_counters[i].count = UINT64_MAX;
    for (unsigned i = 0; i < GROUND_EFFECTS_COUNTERS; ++i) ground_effects_counters[i].count = UINT64_MAX;
    for (unsigned i = 0; i < VEHICLE_HIT_COUNTERS; ++i) vehicle_hit_counters[i].count = UINT64_MAX;
    for (unsigned i = 0; i < BUILDING_HIT_COUNTERS; ++i) building_hit_counters[i].count = UINT64_MAX;
    for (unsigned i = 0; i < RADIUS_HIT_COUNTERS; ++i) radius_hit_counters[i].count = UINT64_MAX;
    tick_log_sequence = UINT32_MAX; ec_frame(UINT32_MAX);
    tick_log_shutdown();
    CHECK(ec_count(ec_sink, "\"event\":\"exit_counters\"") == 1);
    CHECK(!ec_check_exit_line(ec_sink, "shutdown", UINT32_MAX, UINT32_MAX));
    CHECK(ec_count(ec_sink, ":18446744073709551615") == ec_expected_fields());
    printf("PASS bound: maximum exit record %lu bytes + NUL < %u, %u hk fields\n",
           (unsigned long)ec_sink_size, TICK_LOG_LINE_MAX, ec_expected_fields());
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "shutdown")) return ec_shutdown_case();
    if (!strcmp(argv[1], "runner")) return ec_runner_case();
    if (!strcmp(argv[1], "no_hooks")) return ec_no_hooks_case();
    if (!strcmp(argv[1], "limit")) return ec_limit_case();
    if (!strcmp(argv[1], "bound")) return ec_bound_case();
    return 2;
}
