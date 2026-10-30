#ifndef ES2_FORMATS_COMMON_H
#define ES2_FORMATS_COMMON_H

#include <stddef.h>
#include <stdint.h>

/* All views borrow caller-owned, immutable input. No library allocation or I/O. */
typedef struct es2_span { const uint8_t *data; size_t size; } es2_span;
typedef enum es2_status {
    ES2_OK = 0, ES2_ERR_ARGUMENT, ES2_ERR_TRUNCATED, ES2_ERR_MAGIC,
    ES2_ERR_VERSION, ES2_ERR_COUNT, ES2_ERR_OFFSET, ES2_ERR_LENGTH,
    ES2_ERR_OVERLAP, ES2_ERR_STRING, ES2_ERR_UNSUPPORTED, ES2_ERR_CAPACITY,
    ES2_ERR_DEPTH, ES2_ERR_CALLBACK, ES2_ERR_NOT_FOUND
} es2_status;
typedef struct es2_error {
    es2_status code;
    size_t offset;
    const char *message; /* Static string; never freed by the caller. */
} es2_error;
typedef struct es2_reader {
    es2_span span;
    size_t offset;
    es2_status status;
    es2_error *error;
} es2_reader;

const char *es2_status_string(es2_status status);
void es2_error_clear(es2_error *error);
es2_status es2_fail(es2_error *error, es2_status status, size_t offset);
es2_status es2_subspan(es2_span input, size_t offset, size_t length,
                       es2_span *out, es2_error *error);
/* Reader errors are sticky. Failed reads leave cursor and output unchanged. */
es2_status es2_reader_init(es2_reader *reader, es2_span input, es2_error *error);
size_t es2_remaining(const es2_reader *reader);
es2_status es2_read_span(es2_reader *reader, size_t length, es2_span *out);
es2_status es2_skip(es2_reader *reader, size_t length);
es2_status es2_read_u8(es2_reader *reader, uint8_t *out);
es2_status es2_read_u16(es2_reader *reader, uint16_t *out);
es2_status es2_read_u32(es2_reader *reader, uint32_t *out);
es2_status es2_read_i16(es2_reader *reader, int16_t *out);
es2_status es2_read_i32(es2_reader *reader, int32_t *out);
es2_status es2_reader_end(es2_reader *reader);

#endif
