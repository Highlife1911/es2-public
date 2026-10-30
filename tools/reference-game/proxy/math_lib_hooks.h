#ifndef ES2_MATH_LIB_HOOKS_H
#define ES2_MATH_LIB_HOOKS_H
/* math_lib group (docs/reverse-engineering/player_movement.md,
 * section "graphics-library candidates and the texture edge setup
 * 48aaac"): tex_edge 48aaac, the edge setup of the textured polygon
 * fill 48a924 (graphics library; a pure helper of the draw pass, no
 * drawing side effect). The Q16 divide 47e068 is computed in core, so the
 * hook calls no game code (no graph, no counters). It reads the globals
 * 6c5f80..6c5f8c through the image base in the descriptor's user pointer.
 * Compare mode is refused (the edge record may overlap the point arrays).
 * The entry EDX is returned when the start vertex is already the end
 * vertex; no other entry register or uninitialized local is read, so no
 * entry shim. ECX at return is dead at all three call sites (walk in the
 * docs). No game instruction is embedded. */
#include "es2/core/math_lib.h"
static uint64_t __cdecl math_lib_tex_edge(const uint32_t *a,void *u,const uint32_t *r) {
    return es2_tex_edge_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],a[1],r[1]);
}
static callee_counter *math_lib_hook_counters(unsigned hook,unsigned *count) {
    (void)hook;*count=0;return NULL;
}
static const BYTE math_lib_tex_edge_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x7d,0x0c,0x8b,0x5d,0x08};
static const BYTE math_lib_push_lens[]={1,2,1,1};
static const es2_hook_extension math_lib_tex_edge_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,math_lib_tex_edge,TRUE};
static uint32_t __cdecl math_lib_tex_edge32(const uint32_t *a,void *u) {return (uint32_t)math_lib_tex_edge(a,u,weapons_no_registers);}
static es2_hook_descriptor math_lib_hook_descriptors[]={
    {ES2_HOOK_ID(tex_edge),math_lib_tex_edge_sig,13,5,math_lib_push_lens,4,2,math_lib_tex_edge32,NULL,NULL,NULL,0},
};
#endif
