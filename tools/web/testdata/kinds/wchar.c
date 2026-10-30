/* wide characters */
#include <stdint.h>
wchar_t buffer[8];                                                 /* EXPECT: wchar_t */
const wchar_t *name = L"x";                                        /* EXPECT: wchar_t */
const WCHAR *alias = 0;                                            /* EXPECT: wchar_t */
uint16_t units[8];
int wchar_count;
const char *s = "wchar_t";
const char *t = "L";
const char *u = "abcL";
/* wchar_t in a comment, L"x" too */
#define LOG(x) x
int logged = LOG("x");
