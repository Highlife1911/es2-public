/* Portable code: no marker, so the scanner must report nothing. */
#include <stdint.h>
#include <string.h>

typedef struct plain_reader {
    const uint8_t *data;
    size_t size;
} plain_reader;

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int plain_get(const plain_reader *r, size_t at, uint32_t *out) {
    if (at > r->size || r->size - at < 4) return 0;
    *out = read_u32(r->data + at);
    return 1;
}

int plain_state(int state, int a, int b) {
    switch (state) {
    case 0: return a ? b : 0;
    default: return state > 3 ? a : b;
    }
}
