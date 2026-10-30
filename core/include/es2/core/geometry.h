#ifndef ES2_CORE_GEOMETRY_H
#define ES2_CORE_GEOMETRY_H
#include <stdint.h>

/* Coordinates and results are x86 bit patterns, not unsigned magnitudes.
 * All subtraction/addition wraps at 32 bits, including abs(INT32_MIN).
 * Point buffers contain 2 or 3 accessible little-endian dwords; unaligned
 * and identical/overlapping read-only buffers are supported. */
uint32_t es2_distance2(uint32_t dx, uint32_t dy);
uint32_t es2_distance3(uint32_t dx, uint32_t dy, uint32_t dz);
uint32_t es2_distance2_vector(const void *vector);
uint32_t es2_distance2_points(const void *a, const void *b);
uint32_t es2_distance3_points(const void *a, const void *b);
#endif
