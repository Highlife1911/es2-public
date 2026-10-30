/* SHA-256 (FIPS 180-4): the shared small implementation. */
#include "es2/formats/sha256.h"

#include <string.h>

static const uint32_t round_constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static uint32_t rotr(uint32_t v, unsigned n) { return v >> n | v << (32u - n); }

static void compress(es2f_sha256 *s, const uint8_t block[64]) {
    uint32_t w[64], v[8];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)block[4 * i] << 24 | (uint32_t)block[4 * i + 1] << 16 | (uint32_t)block[4 * i + 2] << 8 |
               block[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ w[i - 15] >> 3;
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ w[i - 2] >> 10;
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, s->state, sizeof v);
    for (int i = 0; i < 64; i++) {
        uint32_t big1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
        uint32_t choose = (v[4] & v[5]) ^ (~v[4] & v[6]);
        uint32_t t1 = v[7] + big1 + choose + round_constants[i] + w[i];
        uint32_t big0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
        uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        memmove(v + 1, v, 7 * sizeof v[0]);
        v[4] += t1;
        v[0] = t1 + big0 + majority;
    }
    for (int i = 0; i < 8; i++) s->state[i] += v[i];
}

void es2f_sha256_init(es2f_sha256 *s) {
    static const uint32_t initial[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    memcpy(s->state, initial, sizeof initial);
    s->bytes = 0;
    s->fill = 0;
}

void es2f_sha256_update(es2f_sha256 *s, const void *data, size_t size) {
    const uint8_t *p = data;
    s->bytes += size;
    while (size) {
        size_t take = 64 - s->fill < size ? 64 - s->fill : size;
        memcpy(s->buffer + s->fill, p, take);
        s->fill += take;
        p += take;
        size -= take;
        if (s->fill == 64) {
            compress(s, s->buffer);
            s->fill = 0;
        }
    }
}

void es2f_sha256_hex(es2f_sha256 *s, char hex[65]) {
    uint64_t bits = s->bytes * 8;
    uint8_t pad = 0x80, zero = 0, length[8];
    es2f_sha256_update(s, &pad, 1);
    while (s->fill != 56) es2f_sha256_update(s, &zero, 1);
    for (int i = 0; i < 8; i++) length[i] = (uint8_t)(bits >> (56 - 8 * i));
    es2f_sha256_update(s, length, 8);
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        uint8_t b = (uint8_t)(s->state[i / 4] >> (24 - 8 * (i % 4)));
        hex[2 * i] = digits[b >> 4];
        hex[2 * i + 1] = digits[b & 15];
    }
    hex[64] = 0;
}

void es2f_sha256_of(const void *data, size_t size, char hex[65]) {
    es2f_sha256 s;
    es2f_sha256_init(&s);
    es2f_sha256_update(&s, data, size);
    es2f_sha256_hex(&s, hex);
}
