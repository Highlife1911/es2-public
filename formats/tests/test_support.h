#ifndef ES2_FORMATS_TEST_SUPPORT_H
#define ES2_FORMATS_TEST_SUPPORT_H
#include <stddef.h>
#include <stdint.h>
int test_check(int passed, const char *expression, const char *file, int line);
#define CHECK(expression) ((void)test_check((expression) ? 1 : 0, #expression, __FILE__, __LINE__))
/* Synthetic fixture writer. Never use input bytes from original assets. */
static inline void test_put16(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void test_put32(uint8_t *p, uint32_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
#endif
