/* Own-code fixture for proxy/sha256.h, the proxy's one SHA-256. The
 * standard vectors of FIPS 180-2 (empty string, "abc", the 448-bit and the
 * 896-bit message, one million 'a'), every message length around the block
 * and padding boundaries, many chunkings, and pseudo-random data of many
 * lengths against the CryptoAPI provider. The call sites' entry points are
 * checked in their own fixtures (rtl_imports_sha256_test: rtl_file_matches;
 * presenter_sha_test: pr_hash_window, pr_rgb_hash). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../sha256.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static void one_shot(const uint8_t *data, size_t n, char out[65]) {
    sha256_ctx s; sha256_init(&s); sha256_update(&s, data, n); sha256_hex(&s, out);
}
static void chunked(const uint8_t *data, size_t n, size_t step, char out[65]) {
    sha256_ctx s; sha256_init(&s);
    while (n) { size_t take = step < n ? step : n; sha256_update(&s, data, take); data += take; n -= take; }
    sha256_hex(&s, out);
}
static int crypto(const uint8_t *data, size_t n, char out[65]) {
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0; BYTE digest[32]; DWORD length = 32;
    CHECK(CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT));
    CHECK(CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) && CryptHashData(hash, data, (DWORD)n, 0) &&
          CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0) && length == 32);
    CryptDestroyHash(hash); CryptReleaseContext(provider, 0);
    for (unsigned i = 0; i < 32; ++i) snprintf(out + 2 * i, 3, "%02x", digest[i]);
    return 0;
}

int main(void) {
    char hex[65], want[65];
    static const struct { const char *text; const char *digest; } vectors[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
         "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
    };
    for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        const uint8_t *text = (const uint8_t *)vectors[i].text; size_t n = strlen(vectors[i].text);
        one_shot(text, n, hex);
        CHECK(!strcmp(hex, vectors[i].digest));
        for (size_t step = 1; step <= 70; ++step) { chunked(text, n, step, hex); CHECK(!strcmp(hex, vectors[i].digest)); }
    }
    /* One million 'a' (FIPS 180-2 appendix B.3), in one call and in 1000-byte pieces. */
    static uint8_t million[1000000];
    memset(million, 'a', sizeof(million));
    one_shot(million, sizeof(million), hex);
    CHECK(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    chunked(million, sizeof(million), 1000, hex);
    CHECK(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    chunked(million, sizeof(million), 63, hex);
    CHECK(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));

    /* Pseudo-random data: every length 0..300 (all padding cases around 55/56/63/64/119/120) and larger
     * lengths, each in one call and with chunk sizes 1, 7, 64, 65, against CryptoAPI. */
    static uint8_t data[70000];
    uint32_t x = 12345;
    for (size_t i = 0; i < sizeof(data); ++i) { x = x * 1103515245u + 12345u; data[i] = (uint8_t)(x >> 16); }
    unsigned checked = 0;
    for (size_t n = 0; n <= sizeof(data); n = n < 300 ? n + 1 : n * 2 + 7) {
        if (crypto(data, n, want)) return 1;
        one_shot(data, n, hex); CHECK(!strcmp(hex, want));
        static const size_t steps[] = {1, 7, 64, 65};
        for (unsigned k = 0; k < 4; ++k) { chunked(data, n, steps[k], hex); CHECK(!strcmp(hex, want)); }
        ++checked;
    }
    printf("PASS sha256.h: 4 FIPS vectors with 70 chunkings each, one million 'a' three ways, %u lengths against CryptoAPI\n", checked);
    return 0;
}
