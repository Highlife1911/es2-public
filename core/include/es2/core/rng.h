#ifndef ES2_CORE_RNG_H
#define ES2_CORE_RNG_H

#include <stdint.h>

enum {
    ES2_RNG_WORD_COUNT = 56,
    ES2_RNG_SEED_SIZE = 0x70,
    ES2_RNG_STATE_SIZE = 0x72
};

/* The state is caller-owned writable byte storage, not a C struct. It contains
 * 56 little-endian words, then indices i/j at offsets 0x70/0x71. Neither state
 * nor seed needs word alignment. All pointers must reference the full spans.
 * Seed bytes must not overlap the two index bytes: initialization writes those
 * indices before copying the seed, following the documented original order. */
void es2_rng_init(void *state, const uint8_t seed[ES2_RNG_SEED_SIZE]);

/* Precondition: both stored indices are in 0..55. Updates the destination word
 * with a sum modulo 65536, then advances both indices modulo 56. The return is
 * zero-extended to 32 bits, as documented for the original valid-state path. */
uint32_t es2_rng_next(void *state);

/* Precondition: 1 <= bound <= 32767. Exactly one draw, masked to 15 bits, then
 * signed remainder. Deliberately no zero/negative-bound check or rejection
 * sampling. Passing zero has undefined C behavior; no original fault emulation
 * is promised outside the precondition. */
int32_t es2_rng_bounded(void *state, int16_t bound);

#endif
