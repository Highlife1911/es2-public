#include "es2/core/rng.h"

#include <string.h>

static uint32_t read_word(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8);
}

void es2_rng_init(void *state, const uint8_t seed[ES2_RNG_SEED_SIZE])
{
    uint8_t *bytes = state;
    bytes[0x70] = 24;
    bytes[0x71] = 55;
    memmove(bytes, seed, ES2_RNG_SEED_SIZE);
}

uint32_t es2_rng_next(void *state)
{
    uint8_t *bytes = state;
    unsigned int source = bytes[0x70];
    unsigned int destination = bytes[0x71];
    uint32_t value = (read_word(bytes + 2 * source) +
                      read_word(bytes + 2 * destination)) & UINT32_C(0xffff);
    bytes[2 * destination] = (uint8_t)value;
    bytes[2 * destination + 1] = (uint8_t)(value >> 8);
    bytes[0x70] = (uint8_t)(source == 55 ? 0 : source + 1);
    bytes[0x71] = (uint8_t)(destination == 55 ? 0 : destination + 1);
    return value;
}

int32_t es2_rng_bounded(void *state, int16_t bound)
{
    int32_t masked = (int32_t)(es2_rng_next(state) & UINT32_C(0x7fff));
    return masked % (int32_t)bound;
}
