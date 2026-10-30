/* The x86 framework's x87 result (hook/hook.h): a replacement that returns
 * in ST0, which the framework's assembly entry loads from the long double.
 * The sim dispatch has no x87 state and refuses the kind
 * (docs/web_build.md section 4.7). Included by descriptor.h on i386. */
#ifndef ES2_HOOK_DESCRIPTOR_X86_H
#define ES2_HOOK_DESCRIPTOR_X86_H
#define ES2_RESULT_ST0 ((es2_hook_result_kind)2)
typedef long double (__attribute__((cdecl)) *es2_hook_replacement80)(const uint32_t *, void *);
#endif
