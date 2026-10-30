#include "es2/core/rng.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static uint64_t checks;
static unsigned int failures;

static void check(int passed, const char *expression, int line)
{
    ++checks;
    if (!passed) {
        if (failures < 20) fprintf(stderr, "rng:%d: %s\n", line, expression);
        ++failures;
    }
}

#define CHECK(expression) check((expression) ? 1 : 0, #expression, __LINE__)

static void put_word(uint8_t *bytes, size_t index, uint32_t value)
{
    bytes[2 * index] = (uint8_t)value;
    bytes[2 * index + 1] = (uint8_t)(value >> 8);
}

static uint32_t word(const uint8_t *bytes, size_t index)
{
    return (uint32_t)bytes[2 * index] + 256u * bytes[2 * index + 1];
}

static void make_seed(uint8_t seed[ES2_RNG_SEED_SIZE])
{
    size_t index;
    for (index = 0; index < ES2_RNG_WORD_COUNT; ++index) {
        /* Entirely synthetic; deliberately crosses the sign and carry bits. */
        put_word(seed, index, (uint32_t)(index * 1297u + 0xf123u));
    }
}

static void test_initialization(void)
{
    _Alignas(uint32_t) uint8_t storage[ES2_RNG_STATE_SIZE + 2];
    _Alignas(uint32_t) uint8_t source[ES2_RNG_SEED_SIZE + 2];
    uint8_t saved[ES2_RNG_SEED_SIZE];
    uint8_t *state = storage + 1;
    uint8_t *seed = source + 1;
    memset(storage, 0xa6, sizeof(storage));
    memset(source, 0xc9, sizeof(source));
    make_seed(seed);
    memcpy(saved, seed, sizeof(saved));
    es2_rng_init(state, seed);
    CHECK(ES2_RNG_STATE_SIZE == 114);
    CHECK(ES2_RNG_SEED_SIZE == 112);
    CHECK(((uintptr_t)state & 1u) != 0);
    CHECK(((uintptr_t)seed & 1u) != 0);
    CHECK(memcmp(state, saved, sizeof(saved)) == 0);
    CHECK(state[0x70] == 24);
    CHECK(state[0x71] == 55);
    CHECK(storage[0] == 0xa6 && storage[sizeof(storage) - 1] == 0xa6);
    CHECK(memcmp(seed, saved, sizeof(saved)) == 0);
    CHECK(source[0] == 0xc9 && source[sizeof(source) - 1] == 0xc9);
    (void)es2_rng_next(state);
    CHECK(storage[0] == 0xa6 && storage[sizeof(storage) - 1] == 0xa6);
    CHECK(memcmp(seed, saved, sizeof(saved)) == 0);
    /* A caller may reset indices while using the state's existing word span. */
    memcpy(saved, state, sizeof(saved));
    es2_rng_init(state, state);
    CHECK(memcmp(state, saved, sizeof(saved)) == 0);
    CHECK(state[0x70] == 24 && state[0x71] == 55);
}

static void test_short_known_sequence(void)
{
    static const uint32_t expected[] = {79, 25, 27, 29, 31, 33, 35, 37};
    uint8_t seed[ES2_RNG_SEED_SIZE];
    uint8_t state[ES2_RNG_STATE_SIZE];
    size_t index;
    for (index = 0; index < ES2_RNG_WORD_COUNT; ++index) put_word(seed, index, (uint32_t)index);
    es2_rng_init(state, seed);
    for (index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        CHECK(es2_rng_next(state) == expected[index]);
    }
    CHECK(state[0x70] == 32 && state[0x71] == 7);
}

static void test_all_index_pairs(void)
{
    uint8_t seed[ES2_RNG_SEED_SIZE];
    _Alignas(uint32_t) uint8_t guarded[ES2_RNG_STATE_SIZE + 2];
    uint8_t *state = guarded + 1;
    size_t source, destination, index;
    make_seed(seed);
    for (source = 0; source < ES2_RNG_WORD_COUNT; ++source) {
        for (destination = 0; destination < ES2_RNG_WORD_COUNT; ++destination) {
            uint32_t sum = (word(seed, source) + word(seed, destination)) % 65536u;
            memset(guarded, 0xbd, sizeof(guarded));
            es2_rng_init(state, seed);
            state[0x70] = (uint8_t)source;
            state[0x71] = (uint8_t)destination;
            CHECK(es2_rng_next(state) == sum);
            for (index = 0; index < ES2_RNG_WORD_COUNT; ++index) {
                CHECK(word(state, index) == (index == destination ? sum : word(seed, index)));
            }
            CHECK(state[0x70] == (source + 1) % ES2_RNG_WORD_COUNT);
            CHECK(state[0x71] == (destination + 1) % ES2_RNG_WORD_COUNT);
            CHECK(guarded[0] == 0xbd && guarded[sizeof(guarded) - 1] == 0xbd);
        }
    }
}

static void test_overflow_and_unsigned_return(void)
{
    uint8_t seed[ES2_RNG_SEED_SIZE] = {0};
    uint8_t state[ES2_RNG_STATE_SIZE];
    put_word(seed, 24, 0xffff);
    put_word(seed, 55, 0xffff);
    es2_rng_init(state, seed);
    CHECK(es2_rng_next(state) == UINT32_C(65534));
    CHECK(state[110] == 0xfe && state[111] == 0xff);
    CHECK(state[0x70] == 25 && state[0x71] == 0);
    state[0x70] = 55;
    state[0x71] = 55;
    put_word(state, 55, 0x8000);
    CHECK(es2_rng_next(state) == 0);
    CHECK(state[0x70] == 0 && state[0x71] == 0);
    put_word(state, 0, 0xffff);
    CHECK(es2_rng_next(state) == 0xfffe);
    CHECK(state[0x70] == 1 && state[0x71] == 1);
}

static void test_lag_recurrence_and_independent_streams(void)
{
    enum { DRAWS = 4096 };
    uint32_t history[ES2_RNG_WORD_COUNT + DRAWS];
    uint8_t seed[ES2_RNG_SEED_SIZE];
    uint8_t state[ES2_RNG_STATE_SIZE], twin[ES2_RNG_STATE_SIZE];
    uint8_t snapshot[ES2_RNG_STATE_SIZE];
    size_t index;
    make_seed(seed);
    /* An independent chronological-history model: x[t] = x[t-31]+x[t-56].
     * It does not implement or advance the byte-state's two indices. */
    for (index = 0; index < ES2_RNG_WORD_COUNT; ++index) {
        history[index] = word(seed, (55 + index) % ES2_RNG_WORD_COUNT);
    }
    for (index = ES2_RNG_WORD_COUNT; index < ES2_RNG_WORD_COUNT + DRAWS; ++index) {
        history[index] = (history[index - 31] + history[index - 56]) % 65536u;
    }
    es2_rng_init(state, seed);
    es2_rng_init(twin, seed);
    CHECK(memcmp(state, twin, sizeof(state)) == 0);
    for (index = 0; index < DRAWS; ++index) {
        memcpy(snapshot, twin, sizeof(snapshot));
        CHECK(es2_rng_next(state) == history[ES2_RNG_WORD_COUNT + index]);
        CHECK(memcmp(twin, snapshot, sizeof(twin)) == 0);
        CHECK(es2_rng_next(twin) == history[ES2_RNG_WORD_COUNT + index]);
        CHECK(memcmp(state, twin, sizeof(state)) == 0);
    }
    CHECK(state[0x70] == (24 + DRAWS) % 56);
    CHECK(state[0x71] == (55 + DRAWS) % 56);
    for (index = DRAWS - 56; index < DRAWS; ++index) {
        CHECK(word(state, (55 + index) % 56) == history[56 + index]);
    }
}

static void test_bounded(void)
{
    static const uint32_t raw_values[] = {0, 1, 0x7fff, 0x8000, 0x8001, 0xffff};
    uint8_t seed[ES2_RNG_SEED_SIZE] = {0};
    uint8_t state[ES2_RNG_STATE_SIZE], expected_state[ES2_RNG_STATE_SIZE];
    int32_t bound;
    size_t index;
    put_word(seed, 24, 0xffff);
    put_word(seed, 55, 0xffff);
    es2_rng_init(expected_state, seed);
    CHECK(es2_rng_next(expected_state) == 65534);
    for (bound = 1; bound <= 32767; ++bound) {
        es2_rng_init(state, seed);
        CHECK(es2_rng_bounded(state, (int16_t)bound) == 32766 % bound);
        CHECK(memcmp(state, expected_state, sizeof(state)) == 0);
    }
    for (index = 0; index < sizeof(raw_values) / sizeof(raw_values[0]); ++index) {
        put_word(seed, 24, raw_values[index]);
        put_word(seed, 55, 0);
        es2_rng_init(state, seed);
        CHECK(es2_rng_bounded(state, 30000) == (int32_t)(raw_values[index] & 32767u) % 30000);
    }
    /* Do not call the deliberately unchecked API with zero or negative bounds. */
}

static void test_restore(void)
{
    uint8_t seed[ES2_RNG_SEED_SIZE];
    uint8_t state[ES2_RNG_STATE_SIZE];
    uint8_t restored[ES2_RNG_STATE_SIZE], snapshot[ES2_RNG_STATE_SIZE];
    uint32_t next_values[128];
    size_t index;
    make_seed(seed);
    es2_rng_init(state, seed);
    for (index = 0; index < 113; ++index) (void)es2_rng_next(state);
    memcpy(snapshot, state, sizeof(snapshot));
    for (index = 0; index < sizeof(next_values) / sizeof(next_values[0]); ++index) {
        next_values[index] = es2_rng_next(state);
    }
    memcpy(restored, snapshot, sizeof(restored));
    for (index = 0; index < sizeof(next_values) / sizeof(next_values[0]); ++index) {
        CHECK(es2_rng_next(restored) == next_values[index]);
    }
    CHECK(memcmp(restored, state, sizeof(state)) == 0);
}

int main(void)
{
    test_initialization();
    test_short_known_sequence();
    test_all_index_pairs();
    test_overflow_and_unsigned_return();
    test_lag_recurrence_and_independent_streams();
    test_bounded();
    test_restore();
    printf("rng synthetic: %" PRIu64 " checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
