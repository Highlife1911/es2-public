/* The frame-time recorder; see es2/sim/perf.h. */
#include <stdio.h>
#include <string.h>
#include "es2/platform/os.h"
#include "es2/sim/perf.h"

int es2_sim_perf_path(const char *value, const char *tick_log, char *out, unsigned capacity) {
    if (!value || !value[0] || !strcmp(value, "0") || !capacity) return 0;
    int n;
    if (!strcmp(value, "1")) {
        if (tick_log && tick_log[0]) {
            const char *slash = strrchr(tick_log, '\\'), *other = strrchr(tick_log, '/');
            if (other > slash) slash = other;
            unsigned dir = slash ? (unsigned)(slash - tick_log) + 1u : 0u;
            n = snprintf(out, capacity, "%.*sperf.ndjson", (int)dir, tick_log);
        } else {
            n = snprintf(out, capacity, "es2sim-perf.ndjson");
        }
    } else {
        n = snprintf(out, capacity, "%s", value);
    }
    if (n < 0 || (unsigned)n >= capacity) { out[0] = 0; return 0; }
    return 1;
}

static uint64_t micro(const es2_sim_perf *p, uint64_t ticks) {
    return p->hz ? ticks / p->hz * 1000000u + ticks % p->hz * 1000000u / p->hz : 0u;
}

static void emit(es2_sim_perf *p, const char *text, int n) {
    if (p->failed || n <= 0) return;
    if (p->write(p->write_user, text, (unsigned)n)) p->failed = 1;
}

void es2_sim_perf_init(es2_sim_perf *p, const char *mode, uint64_t (*now)(void *), void *now_user, uint64_t hz,
                       int (*write)(void *, const char *, unsigned), void *write_user) {
    memset(p, 0, sizeof(*p));
    p->enabled = now && write && hz;
    p->now = now; p->now_user = now_user; p->hz = hz;
    p->write = write; p->write_user = write_user;
    p->mode = mode ? mode : "headless";
    if (p->enabled) p->start = now(now_user);
}

void es2_sim_perf_work(es2_sim_perf *p, uint64_t traps, uint64_t calls) {
    if (!p->enabled) return;
    uint64_t t = p->now(p->now_user);
    if (!p->header_done) {
        char line[256];
        int n = snprintf(line, sizeof(line), "{\"perf\":\"es2sim\",\"version\":1,\"mode\":\"%s\",\"clock_hz\":%llu,\"setup_us\":%llu,"
                         "\"setup_traps\":%llu,\"setup_calls\":%llu}\n", p->mode, (unsigned long long)p->hz,
                         (unsigned long long)micro(p, t - p->start), (unsigned long long)traps, (unsigned long long)calls);
        emit(p, line, n);
        p->header_done = 1;
        p->first = p->last = t;
        p->wait = 0;   /* sleeps of the start-up are part of the set-up */
        p->traps = p->traps0 = traps;
        p->calls = p->calls0 = calls;
    }
    p->work_begin = t;
    p->in_work = 1;
}

void es2_sim_perf_wait_begin(es2_sim_perf *p) {
    if (!p->enabled || p->in_wait) return;
    p->wait_begin = p->now(p->now_user);
    p->in_wait = 1;
}

void es2_sim_perf_wait_end(es2_sim_perf *p) {
    if (!p->enabled || !p->in_wait) return;
    p->wait += p->now(p->now_user) - p->wait_begin;
    p->in_wait = 0;
}

void es2_sim_perf_frame(es2_sim_perf *p, uint32_t frame, uint64_t traps, uint64_t calls) {
    if (!p->enabled || !p->header_done) return;
    uint64_t t = p->now(p->now_user);
    char line[256];
    int n = snprintf(line, sizeof(line), "{\"f\":%lu,\"us\":%llu,\"work\":%llu,\"wait\":%llu,\"traps\":%llu,\"calls\":%llu}\n",
                     (unsigned long)frame, (unsigned long long)micro(p, t - p->last),
                     (unsigned long long)(p->in_work ? micro(p, t - p->work_begin) : 0u), (unsigned long long)micro(p, p->wait),
                     (unsigned long long)(traps - p->traps), (unsigned long long)(calls - p->calls));
    emit(p, line, n);
    p->last = t;
    p->wait = 0;
    p->in_work = 0;
    p->traps = traps;
    p->calls = calls;
    ++p->frames;
}

uint64_t es2_sim_perf_dispatch_calls(const es2_sim_dispatch *d) {
    uint64_t sum = 0;
    if (d) for (unsigned i = 0; i < d->count; ++i) sum += d->entries[i].calls;
    return sum;
}

void es2_sim_perf_end(es2_sim_perf *p, const char *reason, uint64_t traps, uint64_t calls, const es2_sim_dispatch *d) {
    if (!p->enabled) return;
    uint64_t t = p->now(p->now_user);
    char line[512];
    /* Totals over the frames (from the first frame's start). Before the
     * first frame the counters' start values are those at the end. */
    uint64_t t0 = p->header_done ? p->first : t;
    uint64_t base_traps = p->header_done ? p->traps0 : traps, base_calls = p->header_done ? p->calls0 : calls;
    int n = snprintf(line, sizeof(line), "{\"end\":\"%s\",\"frames\":%llu,\"us\":%llu,\"traps\":%llu,\"calls\":%llu,\"hot\":[",
                     reason ? reason : "", (unsigned long long)p->frames, (unsigned long long)micro(p, t - t0),
                     (unsigned long long)(traps - base_traps), (unsigned long long)(calls - base_calls));
    emit(p, line, n);
    /* The most trapped entries over the whole run (the candidates of a
     * compile-time bridge), most traps first (ties: lower VA). */
    unsigned taken[ES2_SIM_PERF_HOT], count = 0;
    while (d && count < ES2_SIM_PERF_HOT) {
        unsigned best = d->count;
        for (unsigned i = 0; i < d->count; ++i) {
            if (!d->entries[i].traps) continue;
            int used = 0;
            for (unsigned k = 0; k < count; ++k) used |= taken[k] == i;
            if (used) continue;
            if (best == d->count || d->entries[i].traps > d->entries[best].traps) best = i;
        }
        if (best == d->count) break;
        n = snprintf(line, sizeof(line), "%s[\"0x%08lx\",%llu,%llu]", count ? "," : "", (unsigned long)d->entries[best].va,
                     (unsigned long long)d->entries[best].traps, (unsigned long long)d->entries[best].calls);
        emit(p, line, n);
        taken[count++] = best;
    }
    emit(p, "]}\n", 3);
}

/* ---- the host binding (es2/platform/os.h) */
static uint64_t qpc_now(void *user) {
    (void)user;
    return es2_os_counter();
}
static int file_write(void *user, const char *text, unsigned n) {
    uint32_t written = 0;
    return !es2_os_write_file((es2_os_handle)user, text, n, &written) || written != n;
}

int es2_sim_perf_open(es2_sim_perf *p, const char *mode, const char *path) {
    memset(p, 0, sizeof(*p));
    uint64_t hz = path ? es2_os_counter_frequency() : 0;
    if (!hz || hz > INT64_MAX) return 1;
    es2_os_handle f = es2_os_create_file(path, ES2_OS_GENERIC_WRITE, ES2_OS_FILE_SHARE_READ, 0, ES2_OS_CREATE_ALWAYS,
                                         ES2_OS_FILE_ATTRIBUTE_NORMAL);
    if (f == ES2_OS_INVALID_HANDLE) return 1;
    es2_sim_perf_init(p, mode, qpc_now, NULL, hz, file_write, (void *)f);
    return 0;
}

void es2_sim_perf_close(es2_sim_perf *p) {
    if (p->enabled && p->write == file_write) es2_os_close_handle((es2_os_handle)p->write_user);
    p->enabled = 0;
}
