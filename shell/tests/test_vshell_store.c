/* The VSHELL span store's lookup (es2/shell/vshell_store.h) on the
 * committed runs, without the original file: every run answers inside, a
 * read past a run's end, one that straddles two runs, one before the first
 * and one after the last run are refused (ES2_ERR_NOT_FOUND, never zero
 * bytes); es2_shell_exe_span(ES2_VSHELL_STORE, ...) answers the same; the
 * measurement switch writes "<reader> 0x<va> <size>" lines only when
 * ES2_SHELL_SPAN_LOG names a directory. */
#include "es2/shell/practice.h"
#include "es2/shell/vshell_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#endif

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

int main(void) {
    es2_span s;
    es2_error e;
    /* the measurement switch (first, before any read decides it): a directory from
     * the environment, one line per successful read */
#ifdef _WIN32
    const char *dir = getenv("TEST_TMPDIR");
    if (dir && *dir) {
        char setting[1100], path[1100], line[128];
        snprintf(setting, sizeof setting, "ES2_SHELL_SPAN_LOG=%s", dir);
        CHECK(_putenv(setting) == 0);
        CHECK(es2_shell_exe_span(ES2_VSHELL_STORE, 0x46f72c, 16, &s, &e) == ES2_OK);
        es2_shell_span_log("literal", 0x479c31, 2);
        snprintf(path, sizeof path, "%s/spans-%lu.log", dir, (unsigned long)_getpid());
        FILE *f = fopen(path, "r");
        CHECK(f != NULL);
        if (f) {
            CHECK(fgets(line, sizeof line, f) && !strcmp(line, "store 0x46f72c 16\n"));
            CHECK(fgets(line, sizeof line, f) && !strcmp(line, "literal 0x479c31 2\n"));
            fclose(f);
        }
    }
#endif
    CHECK(es2_vshell_run_count > 0);
    for (size_t i = 0; i < es2_vshell_run_count; i++) {
        const es2_vshell_run *r = &es2_vshell_runs[i];
        if (i) CHECK(es2_vshell_runs[i - 1].va + es2_vshell_runs[i - 1].size < r->va);
        CHECK(es2_vshell_store_span(r->va, r->size, &s, &e) == ES2_OK && s.data == r->bytes && s.size == r->size);
        CHECK(es2_vshell_store_span(r->va + r->size - 1, 1, &s, &e) == ES2_OK && s.data == r->bytes + r->size - 1);
        CHECK(es2_shell_exe_span(ES2_VSHELL_STORE, r->va, r->size, &s, &e) == ES2_OK && s.data == r->bytes);
        /* one byte too many: refused, with the VA as offset */
        CHECK(es2_vshell_store_span(r->va, (size_t)r->size + 1, &s, &e) == ES2_ERR_NOT_FOUND && e.offset == r->va);
        CHECK(es2_shell_exe_span(ES2_VSHELL_STORE, r->va + 1, r->size, &s, &e) == ES2_ERR_NOT_FOUND);
        /* the byte after the run (never adjacent to the next run) */
        CHECK(es2_vshell_store_span(r->va + r->size, 1, &s, &e) == ES2_ERR_NOT_FOUND);
        CHECK(es2_vshell_store_span(r->va - 1, 1, &s, &e) == ES2_ERR_NOT_FOUND);
    }
    const es2_vshell_run *last = &es2_vshell_runs[es2_vshell_run_count - 1];
    CHECK(es2_vshell_store_span(0x400000, 1, &s, &e) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vshell_store_span(0, 1, &s, &e) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vshell_store_span(last->va + last->size + 16, 1, &s, &e) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vshell_store_span(0xffffffffu, 1, &s, &e) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vshell_store_span(es2_vshell_runs[0].va, 0, &s, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_vshell_store_span(es2_vshell_runs[0].va, 1, NULL, &e) == ES2_ERR_ARGUMENT);
    /* the spans the readers ask for (engine, practice, gui) are in the store */
    static const uint32_t spans[][2] = {
        {0x47f850, 112}, {0x46f72c, 16}, {0x46f868, 36}, {0x46f9e4, 48}, {0x46fec4, 12}, {0x46feb8, 12},
        {0x46fb80, 66}, {0x46fbc2, 66}, {0x46fa20, 58}, {0x46f9a8, 60}, {0x476a4c, 54}, {0x479c56, 24},
        {0x479f70, 8}, {0x47396b, 8}, {0x47b480, 0x802}, {0x47aaf4, 0x201}, {0x46ffc0, 18}, {0x46e5a9, 0xed4},
        {0x47665c, 10}, {0x476666, 12}, {0x46f87a, 18}, {0x47083c, 36}, {0x47006c, 36}};
    for (size_t i = 0; i < sizeof spans / sizeof spans[0]; i++)
        CHECK(es2_shell_exe_span(ES2_VSHELL_STORE, spans[i][0], spans[i][1], &s, &e) == ES2_OK);
    uint8_t practice[ES2_SHELL_PRACTICE_TABLES_SIZE];
    CHECK(es2_shell_practice_tables(ES2_VSHELL_STORE, practice, &e) == ES2_OK);
    /* the icon group names its images */
    CHECK(es2_vshell_icon_image_count > 0 && es2_vshell_icon_group_size == 6 + 14 * es2_vshell_icon_image_count);
    CHECK(es2_vshell_icon_group[2] == 1 && es2_vshell_icon_group[4] == es2_vshell_icon_image_count);
    for (size_t i = 0; i < es2_vshell_icon_image_count; i++)
        CHECK(es2_vshell_icon_images[i].size > 40 && es2_vshell_icon_images[i].bytes[0] == 40);  /* BITMAPINFOHEADER */

    printf("%s: %zu runs; %u checks, %u failures\n", failures ? "FAIL" : "PASS", es2_vshell_run_count, checks,
           failures);
    return failures ? 1 : 0;
}
