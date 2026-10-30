#include "es2/core/rng.h"

#include <stdio.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(void)
{
    uint8_t seed[ES2_RNG_SEED_SIZE], state[ES2_RNG_STATE_SIZE];
    unsigned int draw;
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1) return 2;
#endif
    if (fread(seed, 1, sizeof(seed), stdin) != sizeof(seed) || fgetc(stdin) != EOF) {
        fputs("Expected exactly 112 verified seed bytes on stdin\n", stderr);
        return 2;
    }
    es2_rng_init(state, seed);
    for (draw = 0; draw < 8; ++draw) {
        printf("%04x%s", (unsigned int)es2_rng_next(state), draw == 7 ? "\n" : " ");
    }
    return 0;
}
