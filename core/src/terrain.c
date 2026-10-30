#include "es2/core/terrain.h"
#include "fixed.h"

static uint64_t pair(uint32_t edx, uint32_t eax) { return ((uint64_t)edx << 32) | eax; }
static int32_t s32(uint32_t x) { return (int32_t)fx_s32(x); }

/* Corner cell pointer, 0 when the index (row << columns) + column leaves the
 * array of 1 << (columns + rows) cells (signed comparisons, as compiled). */
static uint32_t cell(uint32_t base, uint32_t column, uint32_t row, uint32_t columns, uint32_t rows) {
    uint32_t index = (row << (columns & 31u)) + column;
    uint32_t limit = 1u << ((columns + rows) & 31u);
    if (s32(index) < 0 || s32(index) >= s32(limit)) return 0;
    return (index << 4) + base;
}

uint64_t es2_terrain_height_registers(const es2_guest_memory *m, uint32_t g, uint32_t p) {
    void *c = m->context;
    uint32_t shift = m->read32(c, g + 0x108u) & 31u;
    uint32_t px = m->read32(c, p), py = m->read32(c, p + 4u);
    uint32_t x = fx_sar(px, shift), y = fx_sar(py, shift);
    uint32_t columns = m->read32(c, g + 0x100u), rows = m->read32(c, g + 0x104u);
    if (s32(x) < 0 || s32(x) >= s32(1u << (columns & 31u)) ||
        s32(y) < 0 || s32(y) >= s32(1u << (rows & 31u)))
        return pair(x, 0);
    uint32_t size = 1u << shift;
    uint32_t fx = px - (x << shift), fy = py - (y << shift);
    uint32_t base = m->read32(c, g + 0xecu);
    uint32_t c00 = cell(base, x, y, columns, rows);
    uint32_t c01 = cell(base, x, y + 1u, columns, rows);
    uint32_t c11 = cell(base, x + 1u, y + 1u, columns, rows);
    uint32_t c10 = cell(base, x + 1u, y, columns, rows);
    if (!c00 || !c01 || !c11 || !c10) return pair(c10, 0);
    uint32_t scale = m->read32(c, g + 0x118u), offset = m->read32(c, g + 0x110u);
    uint32_t h01 = (uint32_t)(m->read16(c, c01) & 0xffu) * scale + offset;
    uint32_t h00 = (uint32_t)(m->read16(c, c00) & 0xffu) * scale + offset;
    uint32_t h11 = (uint32_t)(m->read16(c, c11) & 0xffu) * scale + offset;
    uint32_t h10 = (uint32_t)(m->read16(c, c10) & 0xffu) * scale + offset;
    unsigned flags = (m->read16(c, c00 + 0x0eu) >> 8) & 3u;
    if (flags == 2) {
        if (s32(fx) <= s32(fy)) {
            uint32_t edx = fx_sar((h00 - h01) * (size - fy), shift);
            return pair(edx, h01 + fx_sar((h11 - h01) * fx, shift) + edx);
        }
        uint32_t r = h10 + fx_sar((h00 - h10) * (size - fx), shift) + fx_sar((h11 - h10) * fy, shift);
        return pair(r, r);
    }
    if (!flags && s32(size - fx) < s32(fy)) {
        uint32_t r = h11 + fx_sar((h01 - h11) * (size - fx), shift) + fx_sar((h10 - h11) * (size - fy), shift);
        return pair(r, r);
    }
    int64_t product = (int64_t)s32(h01 - h00) * s32(fy);
    uint32_t r = h00 + fx_sar((h10 - h00) * fx, shift) + fx_sar((uint32_t)product, shift);
    return pair((uint32_t)((uint64_t)product >> 32), r);
}

/* Original: DBSIM.EXE 0x46e498 */
uint64_t es2_terrain_face_registers(const es2_guest_memory *m, uint32_t g, uint32_t p) {
    void *c = m->context;
    uint32_t shift = m->read32(c, g + 0x108u) & 31u;
    uint32_t px = m->read32(c, p), py = m->read32(c, p + 4u);
    uint32_t x = fx_sar(px, shift), y = fx_sar(py, shift);
    uint32_t columns = m->read32(c, g + 0x100u), rows = m->read32(c, g + 0x104u);
    if (s32(x) < 0 || s32(x) >= s32(1u << (columns & 31u)) ||
        s32(y) < 0 || s32(y) >= s32(1u << (rows & 31u)))
        return pair(x, 0);
    uint32_t size = 1u << shift;
    uint32_t fx = px - (x << shift), fy = py - (y << shift);
    uint32_t index = (y << (columns & 31u)) + x;
    uint32_t limit = 1u << ((columns + rows) & 31u);
    uint32_t face = s32(index) < 0 || s32(index) >= s32(limit) ? 0 : (index << 4) + m->read32(c, g + 0xecu);
    unsigned flags = (m->read16(c, face + 0x0eu) >> 8) & 3u;
    if (flags == 2) return pair(fx, s32(fx) > s32(fy) ? face + 7u : face + 1u);
    uint32_t rest = size - fx;
    return pair(rest, s32(rest) < s32(fy) ? face + 7u : face + 1u);
}

/* Original: DBSIM.EXE 0x47e610 (with its pure helpers 0x4953c4, 0x47eaa1 ->
 * 0x47df84 and 0x47eabe -> 0x47de2d computed inline). Segment S..E against
 * the plane n.p + c = 0: den = n.(E - S), num = -(n.S + c), both modulo
 * 2^32; a hit needs den != 0 and num in [0, den] (or [den, 0]). Each axis is
 * S + trunc(D * num / den) from the exact 64-bit product, and out is written
 * word by word between the rereads of S, as compiled. `frame` stands for the
 * original EBP; a hit returns frame - 28h in EDX (a dead frame address). */
uint64_t es2_line_plane_registers(const es2_guest_memory *m, uint32_t frame, uint32_t plane, uint32_t segment,
                                  uint32_t out) {
    void *c = m->context;
    uint32_t d[3];
    for (unsigned k = 0; k < 3; ++k) d[k] = m->read32(c, segment + 12u + 4u * k) - m->read32(c, segment + 4u * k);
    uint32_t nx = m->read32(c, plane), ny = m->read32(c, plane + 4u), nz = m->read32(c, plane + 8u);
    uint32_t den = nx * d[0] + ny * d[1] + nz * d[2];
    if (!den) return pair(ny * d[1], 0);
    uint32_t sy = ny * m->read32(c, segment + 4u);
    uint32_t num = 0u - (nx * m->read32(c, segment) + sy + nz * m->read32(c, segment + 8u) + m->read32(c, plane + 12u));
    if (s32(den) > 0 ? s32(num) < 0 || s32(den) < s32(num) : s32(num) > 0 || s32(den) > s32(num))
        return pair(sy, 0);
    for (unsigned k = 0; k < 3; ++k) {
        int64_t q = (int64_t)s32(d[k]) * s32(num) / s32(den);
        m->write32(c, out + 4u * k, (uint32_t)q + m->read32(c, segment + 4u * k));
    }
    return pair(frame - 0x28u, (out & 0xffffff00u) | 1u);
}
