/* The proxy's one SHA-256 (FIPS 180-4), header-only, no Windows call and
 * no dependency beyond the C library. Used by rtl_imports.h (the reference
 * check of DBSIM.EXE and CW3220.DLL at the entry point, where loading the
 * CryptoAPI provider would add DLL loads before the game's static
 * initializers) and by presenter_frame.h (frame and colour-table hashes of the
 * capture). Checked against the FIPS vectors and CryptoAPI in
 * tests/sha256_fixture.c. Interface: sha256_init, sha256_update (any chunking),
 * sha256_hex (finishes the context and writes 64 lowercase hex digits + NUL). */
#ifndef ES2_SHA256_H
#define ES2_SHA256_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct sha256_ctx { uint32_t h[8]; uint64_t length; uint8_t block[64]; unsigned used; } sha256_ctx;
static const uint32_t sha256_k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
};
static uint32_t sha256_ror(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }
static void sha256_block(sha256_ctx *s, const uint8_t *p) {
    uint32_t w[64], a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (unsigned i = 0; i < 16; ++i)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (unsigned i = 16; i < 64; ++i)
        w[i] = w[i - 16] + (sha256_ror(w[i - 15], 7) ^ sha256_ror(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] +
               (sha256_ror(w[i - 2], 17) ^ sha256_ror(w[i - 2], 19) ^ (w[i - 2] >> 10));
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t t1 = h + (sha256_ror(e, 6) ^ sha256_ror(e, 11) ^ sha256_ror(e, 25)) + ((e & f) ^ (~e & g)) + sha256_k[i] + w[i];
        uint32_t t2 = (sha256_ror(a, 2) ^ sha256_ror(a, 13) ^ sha256_ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha256_init(sha256_ctx *s) {
    static const uint32_t h0[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    memcpy(s->h, h0, sizeof(h0)); s->length = 0; s->used = 0;
}
static void sha256_update(sha256_ctx *s, const uint8_t *p, size_t n) {
    s->length += n;
    if (s->used) {
        size_t take = 64u - s->used < n ? 64u - s->used : n;
        memcpy(s->block + s->used, p, take); s->used += (unsigned)take; p += take; n -= take;
        if (s->used < 64) return;
        sha256_block(s, s->block); s->used = 0;
    }
    for (; n >= 64; p += 64, n -= 64) sha256_block(s, p);
    memcpy(s->block, p, n); s->used = (unsigned)n;
}
static void sha256_hex(sha256_ctx *s, char out[65]) {
    static const char digits[] = "0123456789abcdef";
    uint64_t bits = s->length * 8u;
    uint8_t pad[72] = {0x80};
    size_t padding = (s->used < 56 ? 56u : 120u) - s->used;
    for (unsigned i = 0; i < 8; ++i) pad[padding + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(s, pad, padding + 8);
    for (unsigned i = 0; i < 32; ++i) {
        uint8_t v = (uint8_t)(s->h[i / 4] >> (24 - 8 * (i % 4)));
        out[2 * i] = digits[v >> 4]; out[2 * i + 1] = digits[v & 15];
    }
    out[64] = 0;
}
#endif
