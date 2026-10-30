#ifndef ES2_FORMATS_SHA256_H
#define ES2_FORMATS_SHA256_H
/* SHA-256 (FIPS 180-4), one shared implementation (no platform call); the
 * shell's gui library and its tests use it too. */
#include <stddef.h>
#include <stdint.h>

typedef struct es2f_sha256 { uint32_t state[8]; uint64_t bytes; uint8_t buffer[64]; size_t fill; } es2f_sha256;
void es2f_sha256_init(es2f_sha256 *s);
void es2f_sha256_update(es2f_sha256 *s, const void *data, size_t size);
/* Finishes the context; hex gets 64 lowercase digits and a NUL. */
void es2f_sha256_hex(es2f_sha256 *s, char hex[65]);
void es2f_sha256_of(const void *data, size_t size, char hex[65]);

#endif
