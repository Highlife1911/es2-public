/* The single file a wasm link produces: the magic, then per file a little-endian u32 name length, the name, a
   little-endian u32 size and the bytes. Written by emcc_wrapper.c, read by wasm_unpack.c. */
#ifndef ES2_WASM_BUNDLE_H
#define ES2_WASM_BUNDLE_H
#include <stdint.h>
#include <stdio.h>

#define WASM_BUNDLE_EXTENSION ".wasm"
#define WASM_BUNDLE_MAGIC "ES2WASM1\n"
#define WASM_BUNDLE_MAGIC_LENGTH 9

static inline int wasm_bundle_u32(FILE *out, uint32_t value) {
    unsigned char b[4];
    b[0] = (unsigned char)value;
    b[1] = (unsigned char)(value >> 8);
    b[2] = (unsigned char)(value >> 16);
    b[3] = (unsigned char)(value >> 24);
    return fwrite(b, 1, 4, out) == 4;
}

static inline int wasm_bundle_begin(FILE *out) { return fwrite(WASM_BUNDLE_MAGIC, 1, WASM_BUNDLE_MAGIC_LENGTH, out) == WASM_BUNDLE_MAGIC_LENGTH; }

static inline int wasm_bundle_add(FILE *out, const char *name, const void *data, size_t size) {
    size_t length = 0;
    while (name[length]) length++;
    if (size > 0xffffffffu) return 0;
    return wasm_bundle_u32(out, (uint32_t)length) && fwrite(name, 1, length, out) == length &&
           wasm_bundle_u32(out, (uint32_t)size) && fwrite(data, 1, size, out) == size;
}

/* 1 and the value, or 0 at the end of the file, -1 on a short read. */
static inline int wasm_bundle_read_u32(FILE *in, uint32_t *value) {
    unsigned char b[4];
    size_t got = fread(b, 1, 4, in);
    if (got == 0) return 0;
    if (got != 4) return -1;
    *value = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 1;
}

#endif
