/* Stand-in for ES.EXE, VSHELL.EXE and other programs that import from
 * cw3220.dll (//rtl:cw3220_delegate_test copies it under those names next to the
 * own DLL and a fake original). It imports four names statically and
 * prints where each one leads. */
#include <windows.h>
#include <stdio.h>

__declspec(dllimport) unsigned __cdecl _strlen(const char *s);
__declspec(dllimport) int __cdecl _memcmp(const void *a, const void *b, unsigned n);
__declspec(dllimport) int __cdecl _strcmp(const char *a, const char *b);
extern __declspec(dllimport) unsigned char __streams[];

static int original_loaded(void) { return GetModuleHandleA("CW3220-ORIGINAL.DLL") != NULL; }

int main(void)
{
    int before = original_loaded();
    unsigned length = _strlen("abc");
    int compared = _memcmp("ab", "ab", 2);
    FARPROC own = GetProcAddress(GetModuleHandleA("cw3220.dll"), "__streams");
    HMODULE original = GetModuleHandleA("CW3220-ORIGINAL.DLL");
    FARPROC theirs = original ? GetProcAddress(original, "__streams") : NULL;
    const char *streams = (FARPROC)(void *)__streams == own ? "own" : theirs && (FARPROC)(void *)__streams == theirs ? "original" : "other";
    int middle = original_loaded();
    int ordered = _strcmp("a", "b");
    printf("before=%d strlen=%u memcmp=%d streams=%s middle=%d strcmp=%d after=%d\n", before, length, compared, streams, middle, ordered,
           original_loaded());
    return 0;
}
