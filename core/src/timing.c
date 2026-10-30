#include "es2/core/timing.h"

static int32_t signed_word(uint16_t value)
{
    return value < UINT16_C(0x8000) ? (int32_t)value : (int32_t)value - 65536;
}

static uint32_t read_value(const uint8_t *p, unsigned bytes)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= (uint32_t)p[i] << (i * 8);
    return value;
}

static void write_value(uint8_t *p, uint32_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

/* Original: DBSIM 0x467894; quotient range fits int32_t for all nonzero
 * signed-word divisors, including -1. C17 division truncates toward zero. */
uint32_t es2_time_scale(uint16_t reference, uint16_t elapsed)
{
    int32_t quotient = (signed_word(elapsed) * 256) / signed_word(reference);
    int32_t low = signed_word((uint16_t)quotient);
    uint32_t clamped = (uint32_t)(low < 64 ? 64 : low > 450 ? 450 : low);
    return ((uint32_t)quotient & UINT32_C(0xffff0000)) | clamped;
}

uint16_t es2_timer_word_step(void *timer, uint16_t scale)
{
    uint8_t *p = (uint8_t *)timer + 1;
    uint16_t value = (uint16_t)(read_value(p, 2) - (uint32_t)scale);
    if (value & UINT16_C(0x8000)) value = 0;
    write_value(p, value, 2);
    return value;
}

uint32_t es2_timer_dword_step(void *timer, uint16_t scale)
{
    uint8_t *p = (uint8_t *)timer + 1;
    uint32_t value = read_value(p, 4) - (uint32_t)signed_word(scale);
    if (value & UINT32_C(0x80000000)) value = 0;
    write_value(p, value, 4);
    return value;
}

void es2_timer_arm(void *timer, uint16_t duration)
{
    write_value((uint8_t *)timer + 1, (uint32_t)signed_word(duration) << 11, 4);
}
