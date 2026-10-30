/* Lookup in the VSHELL span store and the measurement switch
 * (es2/shell/vshell_store.h). The runs themselves are generated
 * (shell/data/vshell_store_data.c). */
#include "es2/shell/vshell_store.h"
#include <stdio.h>
#include <stdlib.h>
#include "es2/platform/os.h"

const uint8_t es2_vshell_store_handle[1];

es2_status es2_vshell_store_span(uint32_t va, size_t size, es2_span *out, es2_error *e) {
    size_t lo = 0, hi = es2_vshell_run_count;
    if (!out || !size) return es2_fail(e, ES2_ERR_ARGUMENT, va);
    /* the last run whose VA is <= va */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (es2_vshell_runs[mid].va <= va) lo = mid + 1;
        else hi = mid;
    }
    if (lo) {
        const es2_vshell_run *r = &es2_vshell_runs[lo - 1];
        uint32_t at = va - r->va;
        if (at < r->size && r->size - at >= size) {
            *out = (es2_span){r->bytes + at, size};
            return ES2_OK;
        }
    }
    return es2_fail(e, ES2_ERR_NOT_FOUND, va);
}

/* The measurement switch: opened once per process on the first read. */
void es2_shell_span_log(const char *reader, uint32_t va, size_t size) {
    static int state;   /* 0 not yet asked, 1 logging, 2 off */
    static es2_os_stream *log;
    if (state == 2) return;
    if (!state) {
        const char *dir = es2_os_getenv("ES2_SHELL_SPAN_LOG");
        char path[1024];
        state = 2;
        if (!dir || !*dir) return;
        snprintf(path, sizeof path, "%s/spans-%lu.log", dir, (unsigned long)es2_os_process_id());
        if (!(log = es2_os_fopen(path, "a"))) return;
        state = 1;
    }
    es2_os_fprintf(log, "%s 0x%06lx %lu\n", reader ? reader : "span", (unsigned long)va, (unsigned long)size);
    es2_os_fflush(log);
}
