#ifndef ES2_SHELL_MEMORY_H
#define ES2_SHELL_MEMORY_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct buffer {
  unsigned char *data;
  size_t size, capacity;
} buffer;

/* Memory-only stream primitives. Truncation is size=0; a later sparse write
 * must clear its gap even when the allocation still contains old content. */
static int shell_buffer_write(buffer *b, size_t position, const void *input,
                              size_t n) {
  const size_t limit = 16u * 1024u * 1024u;
  if (!b || n > limit || position > limit - n)
    return -1;
  if (!n)
    return 0;
  size_t end = position + n;
  if (end > b->capacity) {
    size_t capacity = end + 4096;
    unsigned char *data = realloc(b->data, capacity);
    if (!data)
      return -1;
    b->data = data;
    b->capacity = capacity;
  }
  if (position > b->size)
    memset(b->data + b->size, 0, position - b->size);
  if (n)
    memcpy(b->data + position, input, n);
  if (end > b->size)
    b->size = end;
  return (int)n;
}
static size_t shell_buffer_read(const unsigned char *data, size_t size,
                                size_t position, void *output, size_t n) {
  if (position >= size)
    return 0;
  if (n > size - position)
    n = size - position;
  if (n)
    memcpy(output, data + position, n);
  return n;
}
#endif
