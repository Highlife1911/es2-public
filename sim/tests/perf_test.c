/* The frame-time recorder (es2/sim/perf.h) on a fake clock and an in-memory
 * sink: the switch's paths, the header, frame and end lines with their
 * intervals and count differences, the window's wait, the hot list, the clock
 * conversion and the off state. No game data. */
#include <stdio.h>
#include <string.h>
#include "es2/sim/perf.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static uint64_t now_value;
static uint64_t fake_now(void *user) { (void)user; return now_value; }
static char sink[4096];
static unsigned sink_n;
static int fake_write(void *user, const char *text, unsigned n) {
    (void)user;
    if (sink_n + n >= sizeof(sink)) return 1;
    memcpy(sink + sink_n, text, n);
    sink_n += n;
    sink[sink_n] = 0;
    return 0;
}
/* The n-th line (0-based) of the sink into `out`. */
static const char *line_of(unsigned index, char *out, unsigned size) {
    const char *p = sink;
    for (unsigned i = 0; i < index && p; ++i) { p = strchr(p, '\n'); if (p) ++p; }
    if (!p || !*p) return NULL;
    const char *end = strchr(p, '\n');
    unsigned n = end ? (unsigned)(end - p) : (unsigned)strlen(p);
    if (n >= size) n = size - 1;
    memcpy(out, p, n);
    out[n] = 0;
    return out;
}

int main(void) {
    char path[64], line[512];
    /* The switch. */
    CHECK(!es2_sim_perf_path(NULL, "x\\tick.jsonl", path, sizeof(path)));
    CHECK(!es2_sim_perf_path("", "x\\tick.jsonl", path, sizeof(path)));
    CHECK(!es2_sim_perf_path("0", "x\\tick.jsonl", path, sizeof(path)));
    CHECK(es2_sim_perf_path("1", "C:\\out\\suite\\tick-log.jsonl", path, sizeof(path)) && !strcmp(path, "C:\\out\\suite\\perf.ndjson"));
    CHECK(es2_sim_perf_path("1", "out/suite\\a/tick.jsonl", path, sizeof(path)) && !strcmp(path, "out/suite\\a/perf.ndjson"));
    CHECK(es2_sim_perf_path("1", "tick.jsonl", path, sizeof(path)) && !strcmp(path, "perf.ndjson"));
    CHECK(es2_sim_perf_path("1", NULL, path, sizeof(path)) && !strcmp(path, "es2sim-perf.ndjson"));
    CHECK(es2_sim_perf_path("D:\\m\\w.ndjson", "tick.jsonl", path, sizeof(path)) && !strcmp(path, "D:\\m\\w.ndjson"));
    CHECK(!es2_sim_perf_path("D:\\a-path-much-longer-than-the-buffer-of-sixty-four-bytes\\perf.ndjson", NULL, path, sizeof(path)) && !path[0]);
    puts("PASS: ES2SIM_PERF unset/empty/0 off, 1 next to the tick log (or the current directory), else the file; too long refused");

    /* A run: set-up 5 ms, frame 0 (7 ms of work), a 6 ms sleep, frame 1. */
    es2_sim_perf p;
    now_value = 0;
    es2_sim_perf_init(&p, "window", fake_now, NULL, 1000000u, fake_write, NULL);
    CHECK(p.enabled);
    es2_sim_perf_frame(&p, 0, 1, 1);          /* before the first logic call: nothing */
    CHECK(sink_n == 0);
    now_value = 4000; es2_sim_perf_wait_begin(&p); now_value = 4500; es2_sim_perf_wait_end(&p);   /* a start-up sleep */
    now_value = 5000; es2_sim_perf_work(&p, 100, 150);
    now_value = 12000; es2_sim_perf_frame(&p, 0, 130, 200);
    now_value = 13000; es2_sim_perf_wait_begin(&p);
    es2_sim_perf_wait_begin(&p);              /* nested begin ignored */
    now_value = 19000; es2_sim_perf_wait_end(&p);
    es2_sim_perf_wait_end(&p);                /* unmatched end ignored */
    now_value = 20000; es2_sim_perf_work(&p, 140, 210);
    now_value = 45000; es2_sim_perf_frame(&p, 1, 230, 400);
    es2_sim_entry entries[4] = {{0}};
    entries[0].va = 0x401000u; entries[0].calls = 5; entries[0].traps = 4;
    entries[1].va = 0x402000u; entries[1].calls = 7; entries[1].traps = 0;   /* only C calls: not listed */
    entries[2].va = 0x403000u; entries[2].calls = 9; entries[2].traps = 9;
    entries[3].va = 0x404000u; entries[3].calls = 6; entries[3].traps = 4;
    es2_sim_dispatch d = {0};
    d.entries = entries; d.count = 4; d.capacity = 4;
    CHECK(es2_sim_perf_dispatch_calls(&d) == 27 && es2_sim_perf_dispatch_calls(NULL) == 0);
    now_value = 50000; es2_sim_perf_end(&p, "frame_limit", 230, 400, &d);
    CHECK(line_of(0, line, sizeof(line)) &&
          !strcmp(line, "{\"perf\":\"es2sim\",\"version\":1,\"mode\":\"window\",\"clock_hz\":1000000,\"setup_us\":5000,"
                        "\"setup_traps\":100,\"setup_calls\":150}"));
    CHECK(line_of(1, line, sizeof(line)) && !strcmp(line, "{\"f\":0,\"us\":7000,\"work\":7000,\"wait\":0,\"traps\":30,\"calls\":50}"));
    CHECK(line_of(2, line, sizeof(line)) && !strcmp(line, "{\"f\":1,\"us\":33000,\"work\":25000,\"wait\":6000,\"traps\":100,\"calls\":200}"));
    CHECK(line_of(3, line, sizeof(line)) &&
          !strcmp(line, "{\"end\":\"frame_limit\",\"frames\":2,\"us\":45000,\"traps\":130,\"calls\":250,"
                        "\"hot\":[[\"0x00403000\",9,9],[\"0x00401000\",4,5],[\"0x00404000\",4,6]]}"));
    CHECK(!line_of(4, line, sizeof(line)));
    puts("PASS: header with the set-up interval and counts, frames with wall/work/wait and count differences, end with the hot list");

    /* A frame without a logic call (a pass that ended in the pump) has work 0
     * and none of the frame before's wait; the clock conversion at 10 MHz;
     * an end before any frame. */
    sink_n = 0; sink[0] = 0;
    now_value = 0;
    es2_sim_perf_init(&p, NULL, fake_now, NULL, 10000000u, fake_write, NULL);
    now_value = 25; es2_sim_perf_work(&p, 0, 0);
    now_value = 50; es2_sim_perf_wait_begin(&p); now_value = 75; es2_sim_perf_wait_end(&p);
    now_value = 125; es2_sim_perf_frame(&p, 7, 3, 4);
    now_value = 30000025; es2_sim_perf_frame(&p, 8, 3, 4);
    CHECK(line_of(0, line, sizeof(line)) && strstr(line, "\"mode\":\"headless\"") && strstr(line, "\"setup_us\":2,"));
    CHECK(line_of(1, line, sizeof(line)) && !strcmp(line, "{\"f\":7,\"us\":10,\"work\":10,\"wait\":2,\"traps\":3,\"calls\":4}"));
    CHECK(line_of(2, line, sizeof(line)) && !strcmp(line, "{\"f\":8,\"us\":2999990,\"work\":0,\"wait\":0,\"traps\":0,\"calls\":0}"));
    sink_n = 0; sink[0] = 0;
    es2_sim_perf_init(&p, "headless", fake_now, NULL, 1000u, fake_write, NULL);
    es2_sim_perf_end(&p, "setup", 9, 9, NULL);
    CHECK(!strcmp(sink, "{\"end\":\"setup\",\"frames\":0,\"us\":0,\"traps\":0,\"calls\":0,\"hot\":[]}\n"));
    puts("PASS: work 0 without a logic call, the wait reset per frame, 10 MHz ticks to microseconds, an end before the first frame");

    /* Off: no clock or no sink, nothing written; a failed write stops the output. */
    sink_n = 0; sink[0] = 0;
    es2_sim_perf_init(&p, "headless", fake_now, NULL, 1000u, NULL, NULL);
    CHECK(!p.enabled);
    es2_sim_perf_work(&p, 1, 1); es2_sim_perf_frame(&p, 0, 2, 2); es2_sim_perf_end(&p, "x", 2, 2, &d);
    es2_sim_perf_init(&p, "headless", NULL, NULL, 1000u, fake_write, NULL);
    CHECK(!p.enabled);
    es2_sim_perf_work(&p, 1, 1);
    CHECK(sink_n == 0);
    sink_n = sizeof(sink) - 8;   /* full: the header does not fit */
    es2_sim_perf_init(&p, "headless", fake_now, NULL, 1000u, fake_write, NULL);
    es2_sim_perf_work(&p, 0, 0);
    CHECK(p.failed);
    unsigned before = sink_n;
    es2_sim_perf_frame(&p, 0, 1, 1);
    CHECK(sink_n == before);
    puts("PASS: off without a clock or a sink; a failed write ends the output");
    return 0;
}
