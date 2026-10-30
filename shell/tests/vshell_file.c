/* The PE section walk over a VSHELL.EXE file image, for tests only
 * (vshell_file.h). */
#include "vshell_file.h"
#include "es2/shell/bytes.h"
#include <string.h>

es2_status es2_test_vshell_file_span(es2_span image, uint32_t va, size_t size, es2_span *out, es2_error *e) {
    size_t pe, count, optional, i;
    uint32_t base;
    if (!out || !image.data) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    if (image.size < 0x40) return es2_fail(e, ES2_ERR_TRUNCATED, 0);
    pe = es2_le32(image.data + 0x3c);
    if (pe > image.size || image.size - pe < 24 || memcmp(image.data + pe, "PE\0\0", 4)) return es2_fail(e, ES2_ERR_MAGIC, pe);
    count = es2_le16(image.data + pe + 6);
    optional = es2_le16(image.data + pe + 20);
    if (optional < 32 || image.size - pe - 24 < optional || (image.size - pe - 24 - optional) / 40 < count)
        return es2_fail(e, ES2_ERR_TRUNCATED, pe);
    base = es2_le32(image.data + pe + 24 + 28);
    for (i = 0; i < count; i++) {
        const uint8_t *h = image.data + pe + 24 + optional + 40 * i;
        uint32_t address = es2_le32(h + 12), raw_size = es2_le32(h + 16), raw = es2_le32(h + 20), rva;
        if (va < base) break;
        rva = va - base;
        if (rva < address || rva - address > raw_size || raw_size - (rva - address) < size) continue;
        if (raw > image.size || image.size - raw < rva - address || image.size - raw - (rva - address) < size)
            return es2_fail(e, ES2_ERR_TRUNCATED, raw);
        *out = (es2_span){image.data + raw + (rva - address), size};
        return ES2_OK;
    }
    return es2_fail(e, ES2_ERR_NOT_FOUND, va);
}

void es2_test_vshell_file_install(void) { es2_shell_set_file_reader(es2_test_vshell_file_span); }
