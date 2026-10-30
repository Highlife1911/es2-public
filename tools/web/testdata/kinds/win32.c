/* Win32 calls and includes. */
#include <windows.h>                                               /* EXPECT: win32-include */
#include <stdint.h>
unsigned ticks(void) { return GetTickCount(); }                    /* EXPECT: win32-call */
void *file(const char *n) { return CreateFileA(n, 0, 0, 0, 3, 0, 0); } /* EXPECT: win32-call */
void *trap(void *h) { return AddVectoredExceptionHandler(1, h); }  /* EXPECT: win32-call */
void *window(void) { return CreateWindowExA(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0); } /* EXPECT: win32-call */
void swap(volatile long *p) { InterlockedExchange(p, 1); }         /* EXPECT: win32-call */
unsigned GetTickCountCache(void) { return cached.GetTickCount(); }
unsigned via_pointer(struct api *a) { return a->GetTickCount(); }
const char *text = "GetTickCount()";
/* GetTickCount() in a comment */
int GetTickCountValue;
