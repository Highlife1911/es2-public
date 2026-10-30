/* A stand-in for the original CW3220.DLL in //rtl:cw3220_delegate_test: it
 * exports the same 65 names as the own DLL, with recognisable results, so
 * the test sees which DLL a stand-in program's calls reach. */
#include <stdint.h>

unsigned __cdecl fake_strlen(const char *s)
{
    unsigned n = 0;
    while (s[n]) ++n;
    return 1000u + n;
}
int __cdecl fake_memcmp(const void *a, const void *b, unsigned n) { (void)a; (void)b; (void)n; return 3000; }
int __cdecl fake_strcmp(const char *a, const char *b) { (void)a; (void)b; return 2000; }
int __cdecl fake_other(void) { return 0x77; }
int32_t fake_errno;
uint8_t fake_streams[50 * 0x18];
uint8_t fake_ctype[257];
