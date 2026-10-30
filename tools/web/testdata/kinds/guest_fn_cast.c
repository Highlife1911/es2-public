/* Guest VAs called as functions. */
#include <stdint.h>
typedef int (*guest_fn)(int);
typedef int (__cdecl *guest_cdecl_fn)(int, int);
typedef int (*callback_fn)(int);
int local(int x);
int call_va(uint32_t va) { return ((guest_fn)(uintptr_t)va)(1); }              /* EXPECT: guest-fn-cast */
int call_inline(uint32_t va) { return ((int (*)(int))va)(2); }                  /* EXPECT: guest-fn-cast */
int call_cdecl(uint32_t va) { return ((guest_cdecl_fn)va)(1, 2); }              /* EXPECT: guest-fn-cast */
int call_macro(void) { return ((guest_fn)MATH_GAME_TARGET(0x401000))(3); }      /* EXPECT: guest-fn-cast */
int call_local(void) { return ((callback_fn)&local)(4); }
callback_fn keep = (callback_fn)local;
int call_member(guest_fn *table) { return table[0](5); }
