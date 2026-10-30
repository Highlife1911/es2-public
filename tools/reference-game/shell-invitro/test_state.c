#include "state.h"
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x)) {                                                                \
      fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x);          \
      return 1;                                                                \
    }                                                                          \
  } while (0)
static unsigned char data[8192];
static size_t cursor;
static void word(unsigned n) {
  data[cursor++] = (unsigned char)n;
  data[cursor++] = (unsigned char)(n >> 8);
}
static void zeros(size_t n) {
  memset(data + cursor, 0, n);
  cursor += n;
}
static void pilot(void) {
  word(0);
  word(0);
  word(2);
  data[cursor++] = 'X';
  data[cursor++] = 0;
  word(0);
  data[cursor++] = 1;
  word(0);
  word(65535);
  zeros(18);
}
int main(void) {
  for (unsigned i = 0; i < 33; i++) {
    data[cursor++] = 0;
    word(0);
  }
  word(5);
  for (unsigned i = 0; i < 5; i++) {
    word(i);
    word(0);
  }
  size_t selection = cursor;
  word(1);
  word(0);
  zeros(148);
  for (unsigned i = 0; i < 36; i++)
    pilot();
  zeros(12);
  word(1);
  word(1);
  pilot();
  word(1);
  size_t loadout = cursor;
  word(0);
  word(0);
  word(0);
  zeros(70);
  word(3);
  word(1);
  size_t item = cursor;
  word(2);
  word(3);
  zeros(8);
  zeros(22);
  size_t variables = cursor;
  zeros(2022);
  size_t size = cursor;
  shell_save saved = {0};
  es2_error error;
  CHECK(shell_save_check((es2_span){data, size}, &saved, &error) == ES2_OK);
  CHECK(saved.selection_offset == selection &&
        saved.variable_offset == variables);
  CHECK(saved.group == 1 && saved.mission == 0 && saved.consumed == size &&
        !saved.trailer_size);
  for (size_t i = 0; i < size; i++) {
    shell_save unchanged = saved;
    CHECK(shell_save_check((es2_span){data, i}, &unchanged, &error) != ES2_OK);
    CHECK(!memcmp(&unchanged, &saved, sizeof(saved)));
  }
  data[selection] = 6;
  CHECK(shell_save_check((es2_span){data, size}, &saved, &error) != ES2_OK);
  data[selection] = 1;
  data[item] = 3;
  CHECK(shell_save_check((es2_span){data, size}, &saved, &error) != ES2_OK);
  data[item] = 2;
  data[item + 2] = 33;
  CHECK(shell_save_check((es2_span){data, size}, &saved, &error) != ES2_OK);
  data[item + 2] = 3;
  data[loadout] = 8;
  CHECK(shell_save_check((es2_span){data, size}, &saved, &error) != ES2_OK);
  data[loadout] = 0;
  CHECK(shell_save_check((es2_span){data, size + 10}, &saved, &error) ==
            ES2_OK &&
        saved.trailer_size == 10);
  printf("PASS: %u synthetic SAV checks, including every truncation\n", checks);
  return 0;
}
