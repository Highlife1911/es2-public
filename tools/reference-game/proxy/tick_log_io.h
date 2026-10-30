/* Optional batching and a runner/worker flush handshake. No game memory is
 * touched by the worker. The tick and all its watches share one lock. */
#ifndef ES2_TICK_LOG_IO_H
#define ES2_TICK_LOG_IO_H
static BOOL tick_io_buffered, tick_io_ready, tick_io_failed;
static DWORD tick_io_target, tick_io_interval = 100;
static CRITICAL_SECTION tick_io_lock;
static HANDLE tick_io_request, tick_io_ack, tick_io_stop, tick_io_worker;
static void tick_log_flush(void);
static void tick_log_exit_record(const char *reason);
static void tick_io_enter(void) { if (tick_io_ready) EnterCriticalSection(&tick_io_lock); }
static void tick_io_leave(void) { if (tick_io_ready) LeaveCriticalSection(&tick_io_lock); }
static DWORD WINAPI tick_io_run(void *unused) {
    (void)unused;
    HANDLE events[2] = {tick_io_stop, tick_io_request};
    for (;;) {
        DWORD wait = WaitForMultipleObjects(2, events, FALSE, tick_io_interval);
        if (wait == WAIT_OBJECT_0) break;
        tick_io_enter();
        /* A runner request precedes a forced stop (no exit callbacks run):
         * the exit-counter record is written first, at most once. */
        if (wait == WAIT_OBJECT_0+1) tick_log_exit_record("runner_flush");
        tick_log_flush(); tick_io_leave();
        if (wait == WAIT_OBJECT_0+1) {
#ifdef ES2_ORACLE_PERF_H
            perf_report("runner_flush", FALSE);
#endif
            if (!tick_io_failed) SetEvent(tick_io_ack);
        }
    }
    return 0;
}
static BOOL tick_io_start(DWORD target, DWORD interval) {
    WCHAR name[80];
    if (!interval || interval > 1000 || tick_io_ready) return FALSE;
    InitializeCriticalSection(&tick_io_lock); tick_io_ready = TRUE;
    tick_io_target = target; tick_io_interval = interval;
    swprintf(name, 80, L"Local\\es2-tick-%lu-flush", (unsigned long)GetCurrentProcessId());
    tick_io_request = CreateEventW(NULL, FALSE, FALSE, name);
    swprintf(name, 80, L"Local\\es2-tick-%lu-ack", (unsigned long)GetCurrentProcessId());
    tick_io_ack = CreateEventW(NULL, FALSE, FALSE, name);
    tick_io_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!tick_io_request || !tick_io_ack || !tick_io_stop) return FALSE;
    tick_io_worker = CreateThread(NULL, 0, tick_io_run, NULL, 0, NULL);
    if (!tick_io_worker) return FALSE;
    tick_io_buffered = TRUE;
    return TRUE;
}
/* Never wait for a worker in DllMain. The owner performs exit flushing under
 * the lock; Windows reclaims process-lifetime synchronization handles. */
static void tick_io_finish(void) { if (tick_io_stop) SetEvent(tick_io_stop); }
#endif
