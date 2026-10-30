/* The reference DBSIM.EXE as a file: hash gate, PE parse, placement, import
 * list, resource lookup (es2/sim/image_file.h). Own code; the field offsets
 * are those of the PE/COFF format (Microsoft's specification).
 *
 * es2sim does not read the file (own_process.md, "es2sim without DBSIM.EXE");
 * this library is testonly and serves the tests that compare the own tables
 * with the file (image_test, table_test, init_test, image_tables_real_test,
 * image_data_real_test, platform_sdl_test). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "es2/sim/image_file.h"
#include "tools/reference-game/proxy/sha256.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
/* [offset, offset + count) lies inside [0, size). */
static int inside(uint64_t offset, uint64_t count, uint64_t size) { return offset <= size && count <= size - offset; }
static int fail(const char **error, const char *text) { if (error) *error = text; return 1; }

void es2_sim_sha256_hex(const uint8_t *bytes, size_t size, char out[65]) {
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, bytes, size);
    sha256_hex(&ctx, out);
}

int es2_sim_image_gate(const uint8_t *file, size_t size, size_t expected_size, const char *expected_sha256,
                       const char **error) {
    if (size != expected_size) return fail(error, "reference size");
    char hex[65];
    es2_sim_sha256_hex(file, size, hex);
    if (strcmp(hex, expected_sha256)) return fail(error, "SHA-256 gate");
    return 0;
}

int es2_sim_image_parse(const uint8_t *file, size_t size, es2_sim_image_info *info, const char **error) {
    memset(info, 0, sizeof(*info));
    if (size < 0x40 || file[0] != 'M' || file[1] != 'Z') return fail(error, "no MZ header");
    uint32_t pe = rd32(file + 0x3c);
    if (!inside(pe, 24, size) || memcmp(file + pe, "PE\0\0", 4)) return fail(error, "no PE header");
    const uint8_t *coff = file + pe + 4;
    if (rd16(coff) != 0x14c) return fail(error, "not i386");
    unsigned sections = rd16(coff + 2), optional_size = rd16(coff + 16);
    uint32_t opt = pe + 24;
    if (optional_size < 96 + 16 * 8 || !inside(opt, optional_size, size)) return fail(error, "optional header");
    const uint8_t *o = file + opt;
    if (rd16(o) != 0x10b) return fail(error, "not PE32");
    info->entry_rva = rd32(o + 16);
    info->image_base = rd32(o + 28);
    uint32_t section_alignment = rd32(o + 32);
    info->image_size = rd32(o + 56);
    info->headers_size = rd32(o + 60);
    unsigned directories = rd32(o + 92);
    if (directories < 6 || 96u + 8u * directories > optional_size) return fail(error, "data directories");
    info->import_rva = rd32(o + 104); info->import_size = rd32(o + 108);
    info->resource_rva = rd32(o + 112); info->resource_size = rd32(o + 116);
    info->reloc_rva = rd32(o + 136); info->reloc_size = rd32(o + 140);
    if (!info->image_size || (uint64_t)info->image_base + info->image_size > 0x100000000ull)
        return fail(error, "image size");
    if (!section_alignment || info->image_size % section_alignment) return fail(error, "section alignment");
    if (!inside(0, info->headers_size, size) || !inside(0, info->headers_size, info->image_size))
        return fail(error, "headers bounds");
    if (!sections || sections > ES2_SIM_MAX_SECTIONS) return fail(error, "section count");
    uint32_t table = opt + optional_size;
    if (!inside(table, 40u * sections, size) || !inside(table, 40u * sections, info->headers_size))
        return fail(error, "section table");
    info->section_count = sections;
    for (unsigned i = 0; i < sections; ++i) {
        const uint8_t *s = file + table + 40u * i;
        es2_sim_section *x = &info->sections[i];
        memcpy(x->name, s, 8); x->name[8] = 0;
        x->virtual_size = rd32(s + 8); x->rva = rd32(s + 12);
        x->raw_size = rd32(s + 16); x->raw_offset = rd32(s + 20); x->flags = rd32(s + 36);
        uint32_t span = x->virtual_size > x->raw_size ? x->virtual_size : x->raw_size;
        if (x->rva < info->headers_size || !inside(x->rva, span, info->image_size)) return fail(error, "section bounds");
        if (x->raw_size && !inside(x->raw_offset, x->raw_size, size)) return fail(error, "section raw bounds");
        for (unsigned j = 0; j < i; ++j) {
            const es2_sim_section *y = &info->sections[j];
            uint32_t yspan = y->virtual_size > y->raw_size ? y->virtual_size : y->raw_size;
            if (x->rva < y->rva + yspan && y->rva < x->rva + span) return fail(error, "overlapping sections");
        }
        if (x->flags & ES2_SIM_SCN_EXECUTE) {
            uint32_t lo = info->image_base + x->rva, hi = lo + span;
            if (!info->code_hi) { info->code_lo = lo; info->code_hi = hi; }
            else if (lo == info->code_hi) info->code_hi = hi;
            else if (hi == info->code_lo) info->code_lo = lo;
            else return fail(error, "executable sections not contiguous");
        }
    }
    if (info->import_size && !inside(info->import_rva, info->import_size, info->image_size)) return fail(error, "import directory");
    if (info->resource_size && !inside(info->resource_rva, info->resource_size, info->image_size)) return fail(error, "resource directory");
    if (info->reloc_size && !inside(info->reloc_rva, info->reloc_size, info->image_size)) return fail(error, "relocation directory");
    return 0;
}

int es2_sim_image_place(const uint8_t *file, size_t size, const es2_sim_image_info *info, uint8_t *image,
                        const char **error) {
    if (!inside(0, info->headers_size, size)) return fail(error, "headers bounds");
    memcpy(image, file, info->headers_size);
    for (unsigned i = 0; i < info->section_count; ++i) {
        const es2_sim_section *s = &info->sections[i];
        uint32_t count = s->raw_size;
        /* Raw data beyond the virtual size is file padding: not part of the image. */
        if (s->virtual_size && count > s->virtual_size) count = s->virtual_size;
        if (!count) continue;
        if (!inside(s->raw_offset, count, size) || !inside(s->rva, count, info->image_size)) return fail(error, "section bounds");
        memcpy(image + s->rva, file + s->raw_offset, count);
    }
    return 0;
}

/* NUL-terminated string at rva inside the image; NULL when unterminated. */
static const char *image_string(const uint8_t *image, uint32_t image_size, uint32_t rva) {
    if (rva >= image_size) return NULL;
    const void *end = memchr(image + rva, 0, image_size - rva);
    return end ? (const char *)image + rva : NULL;
}

int es2_sim_image_imports(const uint8_t *image, const es2_sim_image_info *info, es2_sim_import *imports,
                          unsigned capacity, unsigned *count, const char **error) {
    *count = 0;
    if (!info->import_size) return 0;
    for (uint32_t d = info->import_rva;; d += 20) {
        if (!inside(d, 20, info->image_size)) return fail(error, "import descriptor bounds");
        uint32_t lookup = rd32(image + d), name = rd32(image + d + 12), iat = rd32(image + d + 16);
        if (!name) break;                                   /* the zero descriptor ends the table */
        const char *dll = image_string(image, info->image_size, name);
        if (!dll) return fail(error, "import DLL name");
        if (!lookup) lookup = iat;
        for (uint32_t slot = 0;; ++slot) {
            uint32_t entry_at = lookup + 4u * slot, slot_at = iat + 4u * slot;
            if (!inside(entry_at, 4, info->image_size) || !inside(slot_at, 4, info->image_size)) return fail(error, "import slot bounds");
            uint32_t entry = rd32(image + entry_at);
            if (!entry) break;
            if (*count >= capacity) return fail(error, "too many imports");
            es2_sim_import *x = &imports[(*count)++];
            x->slot_va = info->image_base + slot_at;
            size_t dll_length = strlen(dll);
            if (dll_length > ES2_SIM_IMPORT_NAME - 16) return fail(error, "import DLL name length");
            memcpy(x->name, dll, dll_length);
            x->name[dll_length] = '!';
            char *rest = x->name + dll_length + 1;
            size_t room = ES2_SIM_IMPORT_NAME - dll_length - 1;
            if (entry & 0x80000000u) {
                unsigned ordinal = entry & 0xffffu, digits = 0;
                char tmp[8];
                do { tmp[digits++] = (char)('0' + ordinal % 10); ordinal /= 10; } while (ordinal);
                rest[0] = '#';
                for (unsigned k = 0; k < digits; ++k) rest[1 + k] = tmp[digits - 1 - k];
                rest[1 + digits] = 0;
            } else {
                const char *symbol = image_string(image, info->image_size, entry + 2u);
                if (!symbol) return fail(error, "import symbol name");
                size_t length = strlen(symbol);
                if (length >= room) return fail(error, "import symbol length");
                memcpy(rest, symbol, length + 1);
            }
        }
    }
    return 0;
}

/* One level of the resource tree: the directory at `at` (an offset into the
 * resource directory, 16 bytes, then the named entries, then the numbered
 * ones, 8 bytes each) and the entry for `name` or `id`; with `any` the
 * entry for `id` or else the first. *target is the entry's second word. */
static int resource_entry(const uint8_t *r, uint32_t rsize, uint32_t at, const char *name, uint32_t id, int any,
                          uint32_t *target, const char **error) {
    if (!inside(at, 16, rsize)) return fail(error, "resource directory bounds");
    unsigned named = rd16(r + at + 12), numbered = rd16(r + at + 14), n = named + numbered;
    if (!inside(at + 16u, 8ull * n, rsize)) return fail(error, "resource entries bounds");
    for (unsigned i = 0; i < n; ++i) {
        uint32_t key = rd32(r + at + 16u + 8u * i);
        if (name) {
            if (!(key & 0x80000000u)) continue;
            uint32_t s = key & 0x7fffffffu;
            if (!inside(s, 2, rsize)) return fail(error, "resource name bounds");
            unsigned length = rd16(r + s);
            if (!inside(s + 2u, 2ull * length, rsize)) return fail(error, "resource name bounds");
            if (length != strlen(name)) continue;
            unsigned k = 0;
            for (; k < length; ++k) {
                unsigned c = rd16(r + s + 2u + 2u * k), d = (unsigned char)name[k];
                if (c >= 'a' && c <= 'z') c -= 0x20u;
                if (d >= 'a' && d <= 'z') d -= 0x20u;
                if (c != d) break;
            }
            if (k < length) continue;
        } else if (key != id) {
            continue;
        }
        *target = rd32(r + at + 16u + 8u * i + 4u);
        return 0;
    }
    if (any && n) { *target = rd32(r + at + 16u + 4u); return 0; }
    return fail(error, "resource not found");
}

int es2_sim_image_resource(const uint8_t *image, const es2_sim_image_info *info, uint32_t type, const char *name,
                           uint32_t id, uint32_t *rva, uint32_t *size, const char **error) {
    if (!info->resource_size || !inside(info->resource_rva, info->resource_size, info->image_size))
        return fail(error, "no resource directory");
    const uint8_t *r = image + info->resource_rva;
    uint32_t rsize = info->resource_size, at = 0;
    if (resource_entry(r, rsize, 0, NULL, type, 0, &at, error)) return 1;
    if (!(at & 0x80000000u)) return fail(error, "resource type is no directory");
    if (resource_entry(r, rsize, at & 0x7fffffffu, name, id, 0, &at, error)) return 1;
    if (!(at & 0x80000000u)) return fail(error, "resource name is no directory");
    if (resource_entry(r, rsize, at & 0x7fffffffu, NULL, 0, 1, &at, error)) return 1;
    if (at & 0x80000000u) return fail(error, "resource language is a directory");
    if (!inside(at, 16, rsize)) return fail(error, "resource data entry bounds");
    *rva = rd32(r + at);
    *size = rd32(r + at + 4u);
    if (!inside(*rva, *size, info->image_size)) return fail(error, "resource data bounds");
    return 0;
}

void *es2_sim_window_load_icon(const uint8_t *image, const es2_sim_image_info *info, const char *name, int cx, int cy,
                               const char **error) {
    uint32_t rva = 0, size = 0;
    if (es2_sim_image_resource(image, info, ES2_SIM_RT_GROUP_ICON, name, 0, &rva, &size, error)) return NULL;
    if (size < 6u) { *error = "group icon too short"; return NULL; }
    int id = LookupIconIdFromDirectoryEx((PBYTE)(image + rva), TRUE, cx, cy, LR_DEFAULTCOLOR);
    if (id <= 0) { *error = "no icon in the group"; return NULL; }
    if (es2_sim_image_resource(image, info, ES2_SIM_RT_ICON, NULL, (uint32_t)id, &rva, &size, error)) return NULL;
    HICON icon = CreateIconFromResourceEx((PBYTE)(image + rva), size, TRUE, 0x00030000, cx, cy, LR_DEFAULTCOLOR);
    if (!icon) { *error = "CreateIconFromResourceEx refused the icon"; return NULL; }
    return icon;
}
