#include "es2/core/geometry.h"

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t magnitude(uint32_t value)
{
    return value & UINT32_C(0x80000000) ? 0u - value : value;
}

/* Signed order and arithmetic shift without implementation-defined signed
 * conversions or right shifts. Inputs/outputs retain all 32 original bits. */
static int less(uint32_t a, uint32_t b)
{
    return (a ^ UINT32_C(0x80000000)) < (b ^ UINT32_C(0x80000000));
}

static uint32_t sar(uint32_t value, unsigned count)
{
    return (value >> count) | ((value & UINT32_C(0x80000000)) ?
        (UINT32_MAX << (32u - count)) : 0u);
}

/* DBSIM 0x47de4c, also reached through 0x492a9a and 0x492a45. */
uint32_t es2_distance2(uint32_t dx, uint32_t dy)
{
    uint32_t a = magnitude(dx), b = magnitude(dy);
    return less(a, b) ? b + sar(a, 1) : a + sar(b, 1);
}

/* DBSIM 0x47de72, reached through 0x492aaf/0x492a7d/0x49288c.
 * Sort signed wrapped magnitudes; each fractional term truncates separately. */
uint32_t es2_distance3(uint32_t dx, uint32_t dy, uint32_t dz)
{
    uint32_t values[3] = {magnitude(dx), magnitude(dy), magnitude(dz)};
    for (unsigned i = 1; i < 3; ++i) {
        for (unsigned j = i; j && less(values[j - 1], values[j]); --j) {
            uint32_t temporary = values[j];
            values[j] = values[j - 1];
            values[j - 1] = temporary;
        }
    }
    return values[0] + sar(values[1], 2) + sar(values[1], 4) +
        sar(values[1], 5) + sar(values[2], 2);
}

uint32_t es2_distance2_vector(const void *vector)
{
    const uint8_t *p = vector;
    return es2_distance2(read32(p), read32(p + 4));
}

uint32_t es2_distance2_points(const void *a, const void *b)
{
    const uint8_t *p = a, *q = b;
    return es2_distance2(read32(p) - read32(q), read32(p + 4) - read32(q + 4));
}

uint32_t es2_distance3_points(const void *a, const void *b)
{
    const uint8_t *p = a, *q = b;
    return es2_distance3(read32(p) - read32(q), read32(p + 4) - read32(q + 4),
                         read32(p + 8) - read32(q + 8));
}
