#include "memory.h"
#include <assert.h>
#include <stdint.h>
int main(void) {
  buffer b = {0};
  unsigned char out[12];
  assert(shell_buffer_write(&b, 0, "ABCDEFGHIJKL", 12) == 12);
  b.size = 0;
  assert(shell_buffer_write(&b, 4, "Z", 1) == 1);
  assert(b.size == 5 && !memcmp(b.data, "\0\0\0\0Z", 5));
  assert(shell_buffer_read(b.data, b.size, 2, out, 12) == 3);
  assert(!memcmp(out, "\0\0Z", 3));
  assert(shell_buffer_read(b.data, b.size, 20, out, 12) == 0);
  assert(shell_buffer_write(&b, SIZE_MAX, "Z", 1) == -1);
  assert(shell_buffer_write(&b, 0, "Z", SIZE_MAX) == -1);
  assert(shell_buffer_write(NULL, 0, "Z", 1) == -1);
  assert(b.size == 5);
  assert(shell_buffer_write(&b, 20, NULL, 0) == 0 && b.size == 5);
  free(b.data);
  return 0;
}
