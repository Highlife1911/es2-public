#ifndef ES2_KEY_AXES_HOOKS_H
#define ES2_KEY_AXES_HOOKS_H
/* key_axes group (docs/reverse-engineering/input.md, section "keyboard
 * axes 45a4cc"): key_axes 45a4cc, called once per control record
 * by control_record 45a810 (0x45ad3c) as 45a4cc(4d23c1, flag). It fills
 * the 20-byte keyboard-axis block from the flight-key arrays 4d2394/
 * 4d23a3/4d23b2 and the tables 49eb3d/49eb45, which it reads through the
 * image base in the descriptor's user pointer, and updates the seen and
 * down arrays. It calls no game code and no Win32 function (no graph, no
 * counters, nothing for the input journal). EDX:EAX are the constants
 * 4d23a3:0Fh (relocated with the image base); ECX and the flags are
 * overwritten at 45ad41/45ad44 before any read and are not reproduced.
 * No uninitialized local is read, so no entry shim. It accepts
 * compare mode: a 20-byte span on the block and one RVA region over the
 * key arrays 4d2394..4d23c0 (down, released, seen; the released array is
 * only read), restored in place for the replacement phase; the default
 * comparison (EAX and EDX are constants). No game instruction is
 * embedded. */
#include "es2/core/key_axes.h"
static uint64_t __cdecl key_axes_key_axes(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_key_axes_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],a[1]);
}
static callee_counter *key_axes_hook_counters(unsigned hook,unsigned *count) {
    (void)hook;*count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ebx; push esi; push edi; mov esi,[ebp+0Ch];
 * mov edx,[ebp+8] */
static const BYTE key_axes_key_axes_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x75,0x0c,0x8b,0x55,0x08};
static const BYTE key_axes_frame_lens[]={1,2,1,1,1};
/* Compare: the key arrays D, U, S (4d2394, 45 bytes) in place. */
static const es2_hook_region key_axes_key_axes_regions[]={{ES2_REGION_RVA,0,0xd2394,0,0,0x2d,FALSE,0,{0,0}}};
static const es2_hook_extension key_axes_key_axes_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,
    key_axes_key_axes_regions,1,key_axes_key_axes,FALSE};
static uint32_t __cdecl key_axes_key_axes32(const uint32_t *a,void *u) {return (uint32_t)key_axes_key_axes(a,u,weapons_no_registers);}
static const es2_hook_span key_axes_key_axes_spans[]={{0,0,0x14}};
static es2_hook_descriptor key_axes_hook_descriptors[]={
    {ES2_HOOK_ID(key_axes),key_axes_key_axes_sig,12,6,key_axes_frame_lens,5,2,key_axes_key_axes32,NULL,NULL,key_axes_key_axes_spans,1},
};
#endif
