/* SHA-256 (sha256.h): the FIPS 180-4 example messages and the padding
 * boundaries, synthetic input only. */
#include "es2/formats/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static int hash_is(const void *data, size_t size, const char *expected) {
    char hex[65];
    es2f_sha256_of(data, size, hex);
    return !strcmp(hex, expected);
}

int main(void) {
    static const char two_block[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const char long_msg[] = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    char hex[65];
    unsigned char *big;
    size_t n;
    CHECK(hash_is("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(hash_is("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(hash_is(two_block, strlen(two_block), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK(hash_is(long_msg, strlen(long_msg), "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));
    big = malloc(1000000);
    CHECK(big != NULL);
    if (big) {
        memset(big, 'a', 1000000);
        CHECK(hash_is(big, 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
        /* incremental updates at every split of every padding-relevant length equal the one-shot value */
        for (n = 0; n <= 200; n++) {
            char whole[65];
            size_t cut;
            es2f_sha256_of(big, n, whole);
            for (cut = 0; cut <= n; cut += (cut < 3 || n - cut < 3) ? 1 : 29) {
                es2f_sha256 s;
                es2f_sha256_init(&s);
                es2f_sha256_update(&s, big, cut);
                es2f_sha256_update(&s, big + cut, n - cut);
                es2f_sha256_hex(&s, hex);
                if (strcmp(hex, whole)) { CHECK(!"split update differs"); break; }
            }
        }
        /* lengths 55, 56, 63, 64 around the one-or-two-block padding choice differ from each other */
        {
            char h55[65], h56[65], h63[65], h64[65];
            es2f_sha256_of(big, 55, h55); es2f_sha256_of(big, 56, h56);
            es2f_sha256_of(big, 63, h63); es2f_sha256_of(big, 64, h64);
            CHECK(strcmp(h55, h56) && strcmp(h56, h63) && strcmp(h63, h64) && strcmp(h55, h64));
            CHECK(!strcmp(h56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"));
            CHECK(!strcmp(h64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));
        }
        free(big);
    }
    printf("sha256: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
