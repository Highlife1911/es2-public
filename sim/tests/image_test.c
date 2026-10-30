/* The guest image reader (es2/sim/image.h) on synthetic PE files: parse,
 * place, import list, hash gate and every refusal. No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/image_file.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define FILE_SIZE 0x1000u
static uint8_t file[FILE_SIZE];
static void w16(uint32_t at, uint16_t v) { file[at] = (uint8_t)v; file[at + 1] = (uint8_t)(v >> 8); }
static void w32(uint32_t at, uint32_t v) { for (unsigned i = 0; i < 4; ++i) file[at + i] = (uint8_t)(v >> (8 * i)); }
static void section(unsigned i, const char *name, uint32_t vsize, uint32_t rva, uint32_t raw, uint32_t offset, uint32_t flags) {
    uint32_t s = 0x80u + 24u + 0xe0u + 40u * i;
    memcpy(file + s, name, strlen(name));
    w32(s + 8, vsize); w32(s + 12, rva); w32(s + 16, raw); w32(s + 20, offset); w32(s + 36, flags);
}

/* Image base 0x10000, size 0x5000: headers 0x400; CODE rva 0x1000 (raw 0x200
 * at 0x400), DATA rva 0x2000 (vsize 0x1000, raw 0x200 at 0x600), .idata rva
 * 0x3000 (raw 0x400 at 0x800) with two DLLs, .rsrc rva 0x4000 (raw 0 bytes). */
static void build(void) {
    memset(file, 0, sizeof(file));
    file[0] = 'M'; file[1] = 'Z';
    w32(0x3c, 0x80);
    memcpy(file + 0x80, "PE\0\0", 4);
    w16(0x84, 0x14c); w16(0x86, 4); w16(0x84 + 16, 0xe0);
    uint32_t o = 0x98;
    w16(o, 0x10b);
    w32(o + 16, 0x1000);              /* entry */
    w32(o + 28, 0x10000);             /* image base */
    w32(o + 32, 0x1000);              /* section alignment */
    w32(o + 56, 0x5000);              /* image size */
    w32(o + 60, 0x400);               /* headers */
    w32(o + 92, 16);                  /* directories */
    w32(o + 104, 0x3000); w32(o + 108, 0x3c);   /* imports */
    w32(o + 136, 0x2100); w32(o + 140, 0x8);    /* a relocation directory (reported only) */
    section(0, "CODE", 0x200, 0x1000, 0x200, 0x400, 0x60000020u);
    section(1, "DATA", 0x1000, 0x2000, 0x200, 0x600, 0xc0000040u);
    section(2, ".idata", 0x400, 0x3000, 0x400, 0x800, 0xc0000040u);
    section(3, ".rsrc", 0x100, 0x4000, 0, 0, 0x40000040u);
    for (unsigned i = 0; i < 0x200; ++i) file[0x400 + i] = (uint8_t)(0x90 + (i & 3));
    file[0x600] = 0x5a;
    /* .idata at file 0x800 = rva 0x3000: descriptors (3 x 20), lookup and IAT, names. */
    uint32_t idata = 0x800;
    /* KERNEL32.dll: lookup 0x3100, name 0x3200, IAT 0x3140 */
    w32(idata + 0, 0x3100); w32(idata + 12, 0x3200); w32(idata + 16, 0x3140);
    /* cw3220.DLL: no lookup (IAT only), name 0x3210, IAT 0x3150 */
    w32(idata + 20, 0); w32(idata + 32, 0x3210); w32(idata + 36, 0x3150);
    w32(0x800 + 0x100, 0x3220); w32(0x800 + 0x104, 0x80000005u);      /* GetTickCount, ordinal 5 */
    w32(0x800 + 0x140, 0x3220); w32(0x800 + 0x144, 0x80000005u);
    w32(0x800 + 0x150, 0x3230);                                         /* _memset */
    memcpy(file + 0x800 + 0x200, "KERNEL32.dll", 13);
    memcpy(file + 0x800 + 0x210, "cw3220.DLL", 11);
    memcpy(file + 0x800 + 0x222, "GetTickCount", 13);
    memcpy(file + 0x800 + 0x232, "_memset", 8);
}

static int refused(const char *expected) {
    es2_sim_image_info info;
    const char *error = NULL;
    if (!es2_sim_image_parse(file, FILE_SIZE, &info, &error)) { fprintf(stderr, "FAIL: accepted, wanted %s\n", expected); return 1; }
    if (strcmp(error, expected)) { fprintf(stderr, "FAIL: %s instead of %s\n", error, expected); return 1; }
    return 0;
}

/* A synthetic resource tree at RVA 0x100 of a 0x400-byte image: types 3
 * (numbers 1 and 2) and 14 (the name "ES2ICON"); number 2 has two
 * languages, the neutral one second. */
static uint8_t res_image[0x400];
static void rw16(uint32_t at, uint16_t v) { res_image[0x100 + at] = (uint8_t)v; res_image[0x101 + at] = (uint8_t)(v >> 8); }
static void rw32(uint32_t at, uint32_t v) { for (unsigned i = 0; i < 4; ++i) res_image[0x100 + at + i] = (uint8_t)(v >> (8 * i)); }
static void rdir(uint32_t at, uint16_t named, uint16_t numbered) { rw16(at + 12, named); rw16(at + 14, numbered); }
static void rentry(uint32_t dir, unsigned i, uint32_t key, uint32_t target) { rw32(dir + 16 + 8 * i, key); rw32(dir + 20 + 8 * i, target); }
static void rdata(uint32_t at, uint32_t rva, uint32_t size) { rw32(at, rva); rw32(at + 4, size); }
static int resources(void) {
    es2_sim_image_info info;
    memset(&info, 0, sizeof(info));
    memset(res_image, 0, sizeof(res_image));
    info.image_size = 0x400; info.resource_rva = 0x100; info.resource_size = 0x200;
    rdir(0x00, 0, 2); rentry(0x00, 0, 3, 0x80000030u); rentry(0x00, 1, 14, 0x80000060u);
    rdir(0x30, 0, 2); rentry(0x30, 0, 1, 0x80000090u); rentry(0x30, 1, 2, 0x800000c0u);
    rdir(0x60, 1, 0); rentry(0x60, 0, 0x80000110u, 0x800000f0u);
    rdir(0x90, 0, 1); rentry(0x90, 0, 0x409, 0x150);
    rdir(0xc0, 0, 2); rentry(0xc0, 0, 0x407, 0x160); rentry(0xc0, 1, 0, 0x170);
    rdir(0xf0, 0, 1); rentry(0xf0, 0, 0, 0x180);
    rw16(0x110, 7);
    for (unsigned i = 0; i < 7; ++i) rw16(0x112 + 2 * i, (uint16_t)"ES2ICON"[i]);
    rdata(0x150, 0x300, 0x28); rdata(0x160, 0x330, 0x10); rdata(0x170, 0x340, 0x18); rdata(0x180, 0x360, 0x14);
    uint32_t rva = 0, size = 0;
    const char *error = NULL;
    CHECK(!es2_sim_image_resource(res_image, &info, ES2_SIM_RT_GROUP_ICON, "es2icon", 0, &rva, &size, &error));
    CHECK(rva == 0x360 && size == 0x14);
    CHECK(!es2_sim_image_resource(res_image, &info, ES2_SIM_RT_GROUP_ICON, "ES2ICON", 0, &rva, &size, &error) && rva == 0x360);
    CHECK(!es2_sim_image_resource(res_image, &info, ES2_SIM_RT_ICON, NULL, 1, &rva, &size, &error));
    CHECK(rva == 0x300 && size == 0x28);                         /* the only language */
    CHECK(!es2_sim_image_resource(res_image, &info, ES2_SIM_RT_ICON, NULL, 2, &rva, &size, &error));
    CHECK(rva == 0x340 && size == 0x18);                         /* the neutral language before the first */
    CHECK(es2_sim_image_resource(res_image, &info, ES2_SIM_RT_GROUP_ICON, "es2ico", 0, &rva, &size, &error) &&
          !strcmp(error, "resource not found"));
    CHECK(es2_sim_image_resource(res_image, &info, ES2_SIM_RT_GROUP_ICON, "es2icox", 0, &rva, &size, &error) &&
          !strcmp(error, "resource not found"));
    CHECK(es2_sim_image_resource(res_image, &info, ES2_SIM_RT_GROUP_ICON, NULL, 1, &rva, &size, &error) &&
          !strcmp(error, "resource not found"));                 /* a named entry is no number */
    CHECK(es2_sim_image_resource(res_image, &info, ES2_SIM_RT_ICON, NULL, 5, &rva, &size, &error) &&
          !strcmp(error, "resource not found"));
    CHECK(es2_sim_image_resource(res_image, &info, 6, NULL, 1, &rva, &size, &error) && !strcmp(error, "resource not found"));
    /* Refusals: data outside the image, entries past the directory, a name
     * past it, a leaf where a directory belongs, no directory. */
    rdata(0x180, 0x3f0, 0x20);
    CHECK(es2_sim_image_resource(res_image, &info, ES2_SIM_RT_GROUP_ICON, "es2icon", 0, &rva, &size, &error) &&
          !strcmp(error, "resource data bounds"));
    rdir(0x30, 0, 0x40);
    CHECK(es2_sim_image_resource(res_image, &info, ES2_SIM_RT_ICON, NULL, 1, &rva, &size, &error) &&
          !strcmp(error, "resource entries bounds"));
    rw16(0x110, 0x100);
    CHECK(es2_sim_image_resource(res_image, &info, ES2_SIM_RT_GROUP_ICON, "es2icon", 0, &rva, &size, &error) &&
          !strcmp(error, "resource name bounds"));
    rentry(0x00, 1, 14, 0x60);
    CHECK(es2_sim_image_resource(res_image, &info, ES2_SIM_RT_GROUP_ICON, "es2icon", 0, &rva, &size, &error) &&
          !strcmp(error, "resource type is no directory"));
    info.resource_size = 0;
    CHECK(es2_sim_image_resource(res_image, &info, ES2_SIM_RT_ICON, NULL, 1, &rva, &size, &error) &&
          !strcmp(error, "no resource directory"));
    return 0;
}

/* The own tables' shape and lookups (no game data; the values are compared with
 * the file by //sim:image_tables_real_test). */
static int own_tables(void) {
    es2_sim_image_info info;
    memset(&info, 0xcc, sizeof(info));
    es2_sim_image_info_own(&info);
    CHECK(info.image_base == ES2_SIM_DBSIM_BASE && info.image_size == ES2_SIM_DBSIM_IMAGE_SIZE);
    CHECK(info.section_count && info.section_count <= ES2_SIM_MAX_SECTIONS);
    CHECK(info.code_lo == info.image_base + info.sections[0].rva && info.code_lo < info.code_hi);
    es2_sim_import imports[ES2_SIM_MAX_IMPORTS];
    unsigned count = 0;
    const char *error = NULL;
    CHECK(es2_sim_imports_own(imports, ES2_SIM_MAX_IMPORTS, &count, &error) == 0 && count == es2_sim_dbsim_import_count);
    for (unsigned i = 0; i < count; ++i) {
        const es2_sim_own_import *s = &es2_sim_dbsim_imports[i];
        char name[ES2_SIM_IMPORT_NAME];
        snprintf(name, sizeof(name), "%s!%s", s->dll, s->symbol);
        CHECK(s->symbol && imports[i].slot_va == s->slot_va && !strcmp(imports[i].name, name));
        CHECK(info.import_rva + info.image_base <= s->slot_va && s->slot_va < info.import_rva + info.image_base + 0x1000u);
        CHECK(!i || s->slot_va > es2_sim_dbsim_imports[i - 1].slot_va);
    }
    CHECK(es2_sim_imports_own(imports, count - 1, &count, &error) && !strcmp(error, "too many imports"));
    const uint8_t *bytes = NULL;
    uint32_t size = 0;
    CHECK(es2_sim_resource_own(ES2_SIM_RT_GROUP_ICON, "es2icon", 0, &bytes, &size, &error) == 0 && size == 20u);
    CHECK(bytes[2] == 1 && bytes[4] == 1);                      /* an icon group with one entry */
    uint32_t id = (uint32_t)bytes[18] | (uint32_t)bytes[19] << 8, image_size = (uint32_t)bytes[14] | (uint32_t)bytes[15] << 8;
    CHECK(es2_sim_resource_own(ES2_SIM_RT_ICON, NULL, id, &bytes, &size, &error) == 0 && size == image_size && size == 744u);
    CHECK(es2_sim_resource_own(ES2_SIM_RT_GROUP_ICON, "ES2ICON", 0, &bytes, &size, &error) == 0);
    CHECK(es2_sim_resource_own(ES2_SIM_RT_GROUP_ICON, "es2ico", 0, &bytes, &size, &error) && !strcmp(error, "resource not found"));
    CHECK(es2_sim_resource_own(ES2_SIM_RT_ICON, NULL, id + 1u, &bytes, &size, &error) && !strcmp(error, "resource not found"));
    CHECK(es2_sim_resource_own(ES2_SIM_RT_ICON, "es2icon", 0, &bytes, &size, &error) && !strcmp(error, "resource not found"));
    return 0;
}

int main(void) {
    /* SHA-256 known answer (FIPS 180-4 "abc"). */
    char hex[65];
    es2_sim_sha256_hex((const uint8_t *)"abc", 3, hex);
    CHECK(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    build();
    es2_sim_image_info info;
    const char *error = NULL;
    CHECK(!es2_sim_image_parse(file, FILE_SIZE, &info, &error));
    CHECK(info.image_base == 0x10000 && info.image_size == 0x5000 && info.headers_size == 0x400 && info.section_count == 4);
    CHECK(info.code_lo == 0x11000 && info.code_hi == 0x11200 && info.reloc_rva == 0x2100 && info.import_rva == 0x3000);
    CHECK(!strcmp(info.sections[2].name, ".idata") && info.sections[1].virtual_size == 0x1000);

    /* The gate: the right size and hash pass, anything else is refused. */
    es2_sim_sha256_hex(file, FILE_SIZE, hex);
    CHECK(!es2_sim_image_gate(file, FILE_SIZE, FILE_SIZE, hex, &error));
    CHECK(es2_sim_image_gate(file, FILE_SIZE, FILE_SIZE + 1, hex, &error) && !strcmp(error, "reference size"));
    hex[0] = hex[0] == '0' ? '1' : '0';
    CHECK(es2_sim_image_gate(file, FILE_SIZE, FILE_SIZE, hex, &error) && !strcmp(error, "SHA-256 gate"));
    CHECK(es2_sim_image_gate(file, FILE_SIZE, FILE_SIZE, ES2_SIM_DBSIM_SHA256, &error));

    /* Placement: raw bytes at their RVA, the rest zero, original VAs kept
     * (no relocation applied). */
    uint8_t *image = calloc(1, info.image_size);
    CHECK(image && !es2_sim_image_place(file, FILE_SIZE, &info, image, &error));
    CHECK(image[0] == 'M' && image[0x1000] == 0x90 && image[0x11ff] == 0x93 && image[0x1200] == 0);
    CHECK(image[0x2000] == 0x5a && image[0x2200] == 0 && image[0x4000] == 0);
    es2_sim_import imports[8];
    unsigned count = 0;
    CHECK(!es2_sim_image_imports(image, &info, imports, 8, &count, &error));
    CHECK(count == 3);
    CHECK(imports[0].slot_va == 0x13140 && !strcmp(imports[0].name, "KERNEL32.dll!GetTickCount"));
    CHECK(imports[1].slot_va == 0x13144 && !strcmp(imports[1].name, "KERNEL32.dll!#5"));
    CHECK(imports[2].slot_va == 0x13150 && !strcmp(imports[2].name, "cw3220.DLL!_memset"));
    CHECK(es2_sim_image_imports(image, &info, imports, 2, &count, &error) && !strcmp(error, "too many imports"));
    /* An unterminated import name. */
    memset(image + 0x3232, 'x', 0x5000 - 0x3232);
    CHECK(es2_sim_image_imports(image, &info, imports, 8, &count, &error) && !strcmp(error, "import symbol name"));
    free(image);

    /* Refusals. */
    build(); file[0] = 'X';                         if (refused("no MZ header")) return 1;
    build(); w32(0x3c, FILE_SIZE - 8);              if (refused("no PE header")) return 1;
    build(); w16(0x84, 0x8664);                     if (refused("not i386")) return 1;
    build(); w16(0x98, 0x20b);                      if (refused("not PE32")) return 1;
    build(); w32(0x98 + 56, 0x5800);                if (refused("section alignment")) return 1;
    build(); w32(0x98 + 60, 0x2000);                if (refused("headers bounds")) return 1;
    build(); w16(0x86, 17);                         if (refused("section count")) return 1;
    build(); section(1, "DATA", 0x1000, 0x2000, 0x200, 0xf00, 0xc0000040u); if (refused("section raw bounds")) return 1;
    build(); section(1, "DATA", 0x4000, 0x2000, 0x200, 0x600, 0xc0000040u); if (refused("section bounds")) return 1;
    build(); section(3, ".rsrc", 0x100, 0x2800, 0, 0, 0x40000040u);       if (refused("overlapping sections")) return 1;
    build(); section(3, ".rsrc", 0x100, 0x4000, 0, 0, 0x60000020u);       if (refused("executable sections not contiguous")) return 1;
    build(); w32(0x98 + 104, 0x4f00); w32(0x98 + 108, 0x200);           if (refused("import directory")) return 1;
    build(); w32(0x98 + 136, 0x4f00); w32(0x98 + 140, 0x200);           if (refused("relocation directory")) return 1;
    build(); w32(0x98 + 112, 0x4f00); w32(0x98 + 116, 0x200);           if (refused("resource directory")) return 1;
    CHECK(es2_sim_image_parse(file, 0x30, &info, &error) && !strcmp(error, "no MZ header"));
    if (resources()) return 1;
    if (own_tables()) return 1;
    puts("PASS: image: SHA-256, parse, gate, place (original VAs), imports, 15 refusals, resources; the own tables");
    return 0;
}
