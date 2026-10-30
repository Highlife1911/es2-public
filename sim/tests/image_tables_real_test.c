/* The own image tables (sim/data/dbsim_image_tables.c) against the reference
 * DBSIM.EXE (ES2_DATA_ROOT; SKIP without): the layout equal to
 * es2_sim_image_parse's, the imports equal to es2_sim_image_imports' (and every
 * DLL name, hint and symbol at its VA, every IAT slot naming its hint/name
 * entry), every resource's bytes equal to es2_sim_image_resource's, and the
 * class icon's HICONs from the own bytes equal, bit for bit, to those from the
 * placed image at 32, 16 and the system's two sizes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/image_file.h"
#include "es2/sim/platform_sdl.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *data = NULL;
    if (!fseek(f, 0, SEEK_END)) {
        long n = ftell(f);
        if (n > 0 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n)) && fread(data, 1, (size_t)n, f) != (size_t)n) {
            free(data);
            data = NULL;
        }
        *size = n > 0 ? (size_t)n : 0;
    }
    fclose(f);
    return data;
}

/* The bits of one bitmap of an icon (GetBitmapBits); 0 when equal. */
static int same_bitmap(HBITMAP a, HBITMAP b) {
    if (!a || !b) return a != b;
    BITMAP x, y;
    if (!GetObjectA(a, sizeof(x), &x) || !GetObjectA(b, sizeof(y), &y)) return 1;
    if (x.bmWidth != y.bmWidth || x.bmHeight != y.bmHeight || x.bmBitsPixel != y.bmBitsPixel || x.bmPlanes != y.bmPlanes)
        return 1;
    LONG n = x.bmWidthBytes * x.bmHeight * x.bmPlanes;
    uint8_t *p = malloc((size_t)n), *q = malloc((size_t)n);
    int differ = !p || !q || GetBitmapBits(a, n, p) != n || GetBitmapBits(b, n, q) != n || memcmp(p, q, (size_t)n);
    free(p);
    free(q);
    return differ;
}

static int same_icon(void *a, void *b, int cx) {
    ICONINFO x, y;
    CHECK(a && b && GetIconInfo((HICON)a, &x) && GetIconInfo((HICON)b, &y));
    BITMAP bm;
    int ok = x.fIcon == y.fIcon && !same_bitmap(x.hbmColor, y.hbmColor) && !same_bitmap(x.hbmMask, y.hbmMask) &&
             GetObjectA(x.hbmColor ? x.hbmColor : x.hbmMask, sizeof(bm), &bm) && bm.bmWidth == cx;
    if (x.hbmColor) DeleteObject(x.hbmColor);
    if (y.hbmColor) DeleteObject(y.hbmColor);
    DeleteObject(x.hbmMask);
    DeleteObject(y.hbmMask);
    DestroyIcon((HICON)a);
    DestroyIcon((HICON)b);
    return !ok;
}

int main(void) {
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) { puts("SKIP: ES2_DATA_ROOT is not set; no original data read"); return 0; }
    char path[MAX_PATH];
    uint8_t *file = NULL;
    size_t size = 0;
    static const char *const rel[] = {"\\VER95\\DBSIM.EXE", "\\extracted\\cd\\VER95\\DBSIM.EXE"};
    for (unsigned i = 0; i < 2 && !file; ++i)
        if (snprintf(path, sizeof(path), "%s%s", root, rel[i]) < (int)sizeof(path)) file = read_file(path, &size);
    if (!file) { puts("SKIP: no DBSIM.EXE below ES2_DATA_ROOT; no original data read"); return 0; }
    const char *error = NULL;
    CHECK(es2_sim_image_gate(file, size, ES2_SIM_DBSIM_FILE_SIZE, ES2_SIM_DBSIM_SHA256, &error) == 0);

    /* The layout. */
    es2_sim_image_info parsed, own;
    CHECK(es2_sim_image_parse(file, size, &parsed, &error) == 0);
    memset(&own, 0, sizeof(own));
    es2_sim_image_info_own(&own);
    CHECK(own.image_base == parsed.image_base && own.image_size == parsed.image_size);
    CHECK(own.headers_size == parsed.headers_size && own.entry_rva == parsed.entry_rva);
    CHECK(own.section_count == parsed.section_count);
    for (unsigned i = 0; i < parsed.section_count; ++i) {
        const es2_sim_section *a = &own.sections[i], *b = &parsed.sections[i];
        CHECK(!memcmp(a->name, b->name, sizeof(a->name)));
        CHECK(a->rva == b->rva && a->virtual_size == b->virtual_size && a->raw_offset == b->raw_offset &&
              a->raw_size == b->raw_size && a->flags == b->flags);
    }
    CHECK(own.import_rva == parsed.import_rva && own.import_size == parsed.import_size);
    CHECK(own.reloc_rva == parsed.reloc_rva && own.reloc_size == parsed.reloc_size);
    CHECK(own.resource_rva == parsed.resource_rva && own.resource_size == parsed.resource_size);
    CHECK(own.code_lo == parsed.code_lo && own.code_hi == parsed.code_hi);

    /* The imports. */
    uint8_t *image = calloc(1, parsed.image_size);
    CHECK(image && es2_sim_image_place(file, size, &parsed, image, &error) == 0);
    static es2_sim_import from_file[ES2_SIM_MAX_IMPORTS], from_own[ES2_SIM_MAX_IMPORTS];
    unsigned n_file = 0, n_own = 0;
    CHECK(es2_sim_image_imports(image, &parsed, from_file, ES2_SIM_MAX_IMPORTS, &n_file, &error) == 0);
    CHECK(es2_sim_imports_own(from_own, ES2_SIM_MAX_IMPORTS, &n_own, &error) == 0);
    CHECK(n_own == n_file && n_own == es2_sim_dbsim_import_count);
    const uint32_t base = parsed.image_base;
    for (unsigned i = 0; i < n_own; ++i) {
        CHECK(from_own[i].slot_va == from_file[i].slot_va && !strcmp(from_own[i].name, from_file[i].name));
        const es2_sim_own_import *s = &es2_sim_dbsim_imports[i];
        CHECK(s->slot_va == from_file[i].slot_va);
        CHECK(s->dll_name_va - base < parsed.image_size && !strcmp((const char *)image + (s->dll_name_va - base), s->dll));
        uint32_t entry = rd32(image + (s->slot_va - base));
        if (s->symbol) {
            CHECK(entry == s->name_va - base && s->name_va - base + 2u < parsed.image_size);
            const uint8_t *hint = image + (s->name_va - base);
            CHECK(((uint32_t)hint[0] | (uint32_t)hint[1] << 8) == s->hint && !strcmp((const char *)hint + 2, s->symbol));
        } else {
            CHECK(entry == (0x80000000u | s->ordinal));
        }
    }

    /* The resources. */
    for (unsigned i = 0; i < es2_sim_dbsim_resource_count; ++i) {
        const es2_sim_own_resource *r = &es2_sim_dbsim_resources[i];
        uint32_t rva = 0, rsize = 0;
        const uint8_t *bytes = NULL;
        uint32_t own_size = 0;
        CHECK(es2_sim_image_resource(image, &parsed, r->type, r->name, r->id, &rva, &rsize, &error) == 0);
        CHECK(r->va == base + rva && r->size == rsize && !memcmp(r->bytes, image + rva, rsize));
        CHECK(es2_sim_resource_own(r->type, r->name, r->id, &bytes, &own_size, &error) == 0 && bytes == r->bytes);
    }
    CHECK(es2_sim_dbsim_resource_count == 2u);

    /* The class icon: the name the host passes is the one at 0x4a084d
     * (LoadIconA's argument at 0x465111); the same HICONs from both sources. */
    const uint32_t name_rva = 0x004a084du - base;
    CHECK(memchr(image + name_rva, 0, 16) && !_stricmp((const char *)image + name_rva, "es2icon"));
    const char *name = (const char *)image + name_rva;
    const int sizes[4] = {32, 16, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CXSMICON)};
    for (unsigned i = 0; i < 4; ++i) {
        void *a = es2_sim_window_load_icon(image, &parsed, name, sizes[i], sizes[i], &error);
        void *b = es2_sim_window_load_icon_own(name, sizes[i], sizes[i], &error);
        CHECK(same_icon(a, b, sizes[i]) == 0);
    }
    free(image);
    free(file);
    printf("PASS: image_tables_real: layout (%u sections), %u imports with their names at their VAs, %u resources, "
           "the class icon's HICONs equal at 32, 16, %d and %d px\n",
           own.section_count, n_own, es2_sim_dbsim_resource_count, sizes[2], sizes[3]);
    return 0;
}
