/* The VSHELL span store (shell/data/vshell_store_data.c) against the
 * reference VER95/VSHELL.EXE (SHA-256 1534aa8f...a16e) under ES2_DATA_ROOT:
 *   1. every run equals the file at its VA (the section walk over the file,
 *      the tests' own vshell_file.c installed as es2_shell_exe_span's file
 *      reader), and the runs are sorted,
 *      disjoint and not adjacent;
 *   2. the icon: the group and every image equal the file at their VAs and
 *      equal what Windows' resource loader finds for RT_GROUP_ICON "ES2ICON"
 *      and its RT_ICON ids in the file;
 *   3. coverage: every read of the span store's measurement (argv[1],
 *      shell/data/vshell_reads.txt), every layout literal (ES2_UI_REF_EXE_STRING
 *      with its NUL) and the readers themselves (engine tables, practice
 *      tables) give the same bytes from the store as from the file.
 * Nothing of the file is printed except counts. Skipped without ES2_DATA_ROOT. */
#include "es2/shell/engine.h"
#include "es2/shell/gui/layout.h"
#include "es2/shell/practice.h"
#include "es2/shell/vshell_store.h"

#include "es2/formats/sha256.h"
#include "vshell_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

static es2_span file;

/* The bytes [va, va + size) from the store and from the file are equal. */
static int same(uint32_t va, size_t size) {
    es2_span a, b;
    return es2_shell_exe_span(ES2_VSHELL_STORE, va, size, &a, NULL) == ES2_OK &&
           es2_shell_exe_span(file, va, size, &b, NULL) == ES2_OK && !memcmp(a.data, b.data, size);
}

static unsigned literal_count;
static void literal(const es2_ui_layout_ref *r) {
    if (r->kind != ES2_UI_REF_EXE_STRING) return;
    literal_count++;
    CHECK(same((uint32_t)r->a, (size_t)r->b + 1));
}

/* A data file: the path itself, else its line "_main/<rootpath> <path>" in
 * the runfiles manifest (Windows has no runfiles tree). */
static FILE *open_runfile(const char *rootpath) {
    FILE *f = fopen(rootpath, "r"), *manifest;
    char line[2048], key[600];
    if (f) return f;
    manifest = getenv("RUNFILES_MANIFEST_FILE") ? fopen(getenv("RUNFILES_MANIFEST_FILE"), "r") : NULL;
    snprintf(key, sizeof key, "_main/%s ", rootpath);
    while (manifest && !f && fgets(line, sizeof line, manifest)) {
        if (strncmp(line, key, strlen(key))) continue;
        char *path = line + strlen(key);
        path[strcspn(path, "\r\n")] = 0;
        f = fopen(path, "r");
    }
    if (manifest) fclose(manifest);
    return f;
}

#ifdef _WIN32
/* The resource's bytes as Windows finds them in the file mapped as data. */
static int resource_equal(HMODULE m, LPCSTR name, LPCSTR type, const uint8_t *bytes, size_t size) {
    HRSRC r = FindResourceA(m, name, type);
    HGLOBAL g = r ? LoadResource(m, r) : NULL;
    const void *p = g ? LockResource(g) : NULL;
    return p && SizeofResource(m, r) == size && !memcmp(p, bytes, size);
}
#endif

int main(int argc, char **argv) {
    const char *root = getenv("ES2_DATA_ROOT");
    char path[1024], hex[65];
    if (!root || !*root) {
        printf("SKIP: ES2_DATA_ROOT not set\n");
        return 0;
    }
    es2_test_vshell_file_install();   /* the file through the tests' own section walk */
    snprintf(path, sizeof path, "%s/VER95/VSHELL.EXE", root);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("SKIP: %s not found\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *image = size > 0 ? malloc((size_t)size) : NULL;
    size_t got = image ? fread(image, 1, (size_t)size, f) : 0;
    fclose(f);
    file = (es2_span){image, got};
    es2f_sha256_of(image, got, hex);
    if (strcmp(hex, "1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e")) {
        printf("FAIL: VSHELL.EXE is not the reference file\n");
        return 1;
    }
    CHECK(strstr(es2_vshell_store_source, hex) != NULL);

    /* 1. the runs */
    size_t bytes = 0;
    CHECK(es2_vshell_run_count > 0);
    for (size_t i = 0; i < es2_vshell_run_count; i++) {
        const es2_vshell_run *r = &es2_vshell_runs[i];
        es2_span s;
        CHECK(r->size > 0 && r->bytes && r->readers && *r->readers);
        CHECK(es2_shell_exe_span(file, r->va, r->size, &s, NULL) == ES2_OK && !memcmp(s.data, r->bytes, r->size));
        if (i) CHECK(es2_vshell_runs[i - 1].va + es2_vshell_runs[i - 1].size < r->va);
        bytes += r->size;
    }

    /* 2. the icon */
    es2_span s;
    CHECK(es2_shell_exe_span(file, es2_vshell_icon_group_va, es2_vshell_icon_group_size, &s, NULL) == ES2_OK &&
          !memcmp(s.data, es2_vshell_icon_group, es2_vshell_icon_group_size));
    CHECK(es2_vshell_icon_group_size == 6 + 14 * es2_vshell_icon_image_count);
    CHECK(es2_vshell_icon_group[4] == es2_vshell_icon_image_count && !es2_vshell_icon_group[5]);
    for (size_t i = 0; i < es2_vshell_icon_image_count; i++) {
        const es2_vshell_icon_image *m = &es2_vshell_icon_images[i];
        const uint8_t *entry = es2_vshell_icon_group + 6 + 14 * i;
        CHECK(es2_shell_exe_span(file, m->va, m->size, &s, NULL) == ES2_OK && !memcmp(s.data, m->bytes, m->size));
        CHECK((entry[12] | entry[13] << 8) == m->id);
        CHECK(((uint32_t)entry[8] | (uint32_t)entry[9] << 8 | (uint32_t)entry[10] << 16 | (uint32_t)entry[11] << 24) ==
              m->size);
    }
#ifdef _WIN32
    HMODULE module = LoadLibraryExA(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    CHECK(module != NULL);
    if (module) {
        CHECK(resource_equal(module, "ES2ICON", MAKEINTRESOURCEA(14), es2_vshell_icon_group, es2_vshell_icon_group_size));
        for (size_t i = 0; i < es2_vshell_icon_image_count; i++)
            CHECK(resource_equal(module, MAKEINTRESOURCEA(es2_vshell_icon_images[i].id), MAKEINTRESOURCEA(3),
                                 es2_vshell_icon_images[i].bytes, es2_vshell_icon_images[i].size));
        FreeLibrary(module);
    }
#endif

    /* 3. coverage: the measured reads */
    unsigned reads = 0;
    if (argc > 1) {
        FILE *list = open_runfile(argv[1]);
        char line[256];
        CHECK(list != NULL);
        while (list && fgets(line, sizeof line, list)) {
            unsigned long va, n;
            char reader[64];
            if (line[0] == '#' || sscanf(line, "%lx %lu %63s", &va, &n, reader) != 3) continue;
            reads++;
            CHECK(same((uint32_t)va, (size_t)n));
        }
        if (list) fclose(list);
        CHECK(reads > 0);
    }
    /* the layout literals */
    for (int i = 0; i < ES2_UI_LAYOUT_CONTROL_COUNT; i++) {
        const es2_ui_layout_control *c = es2_ui_layout_control_of(i);
        literal(&c->text);
        for (int k = 0; k < 5; k++) literal(&c->ref[k]);
    }
    for (int i = 0; i < ES2_UI_LAYOUT_FIELD_COUNT; i++) literal(&es2_ui_layout_field_of(i)->value);
    for (int i = 0; i < ES2_UI_LAYOUT_METHOD_COUNT; i++)
        for (int k = 0; k < 5; k++) literal(&es2_ui_layout_method_of(i)->ref[k]);
    CHECK(literal_count > 0);
    /* the readers over the store equal the readers over the file */
    static es2_shell_engine_tables from_store, from_file;
    uint8_t practice_store[ES2_SHELL_PRACTICE_TABLES_SIZE], practice_file[ES2_SHELL_PRACTICE_TABLES_SIZE];
    memset(&from_store, 0, sizeof from_store);
    memset(&from_file, 0, sizeof from_file);
    CHECK(es2_shell_engine_tables_read(ES2_VSHELL_STORE, &from_store, NULL) == ES2_OK);
    CHECK(es2_shell_engine_tables_read(file, &from_file, NULL) == ES2_OK);
    CHECK(!memcmp(from_store.prepare, from_file.prepare, sizeof from_file.prepare));
    CHECK(!memcmp(from_store.results, from_file.results, sizeof from_file.results));
    CHECK(!memcmp(from_store.rows, from_file.rows, sizeof from_file.rows));
    CHECK(!memcmp(from_store.arm, from_file.arm, sizeof from_file.arm));
    CHECK(es2_shell_practice_tables(ES2_VSHELL_STORE, practice_store, NULL) == ES2_OK);
    CHECK(es2_shell_practice_tables(file, practice_file, NULL) == ES2_OK);
    CHECK(!memcmp(practice_store, practice_file, sizeof practice_file));

    printf("%s: %zu runs (%zu bytes), %zu icon image(s), %u measured reads, %u literal uses; %u checks, %u failures\n",
           failures ? "FAIL" : "PASS", es2_vshell_run_count, bytes, es2_vshell_icon_image_count, reads, literal_count,
           checks, failures);
    free(image);
    return failures ? 1 : 0;
}
