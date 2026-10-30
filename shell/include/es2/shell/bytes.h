/* Little-endian byte access shared by the shell's readers and writers of
 * SAV, MEC, DAT and executable-table bytes. Explicit, never a cast of a
 * struct over the buffer. */
#ifndef ES2_SHELL_BYTES_H
#define ES2_SHELL_BYTES_H

#include <stdint.h>

static inline uint16_t es2_le16(const uint8_t *p) { return (uint16_t)(p[0] | (unsigned)p[1] << 8); }
static inline int16_t es2_le16s(const uint8_t *p) {
    unsigned x = es2_le16(p);
    return (int16_t)(x < 32768u ? (int)x : (int)x - 65536);
}
static inline uint32_t es2_le32(const uint8_t *p) { return (uint32_t)es2_le16(p) | (uint32_t)es2_le16(p + 2) << 16; }
static inline void es2_put16(uint8_t *p, uint32_t x) { p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); }
static inline void es2_put32(uint8_t *p, uint32_t x) { es2_put16(p, x); es2_put16(p + 2, x >> 16); }

#endif
