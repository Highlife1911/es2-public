#ifndef ES2_CORE_MOVEMENT_H
#define ES2_CORE_MOVEMENT_H
#include <stdint.h>

/* All storage is little-endian, possibly unaligned. The trig table is 1025
 * uint16 values supplied by the caller; no original data is compiled in. */
uint32_t es2_heading_cos(uint16_t angle, const void *quarter_table);
uint32_t es2_heading_sin(uint16_t angle, const void *quarter_table);
uint32_t es2_multiply_q8(uint32_t a, uint32_t b);
uint32_t es2_movement_step(uint16_t value, uint16_t scale);
/* Actor >=0x2c1 bytes, config >=0x20a. A zero selected divisor or an IDIV
 * quotient outside signed32 raises the original's fault (es2_divide_raise). */
uint32_t es2_hud_speed(const void *actor, const void *config);
/* Matrix is 32 bytes: nine signed Q14 coefficients, signed mode byte +18,
 * translations +20/+24/+28. Output is 12 bytes, written X then Y then Z.
 * Inputs are re-read between writes, including when input/output overlap. */
void es2_transform_point(const void *input, const void *matrix, void *output);
/* Eight-byte XY records. X is written before Y; overlap is permitted. */
void es2_vector_subtract(void *destination, const void *source);
#endif
