#ifndef ES2_LINE_PLANE_HOOKS_H
#define ES2_LINE_PLANE_HOOKS_H
/* Line/plane intersection: line_plane 47e610, the contact helper of
 * the terrain walk (docs/reverse-engineering/weapons.md, "47e610
 * line_plane"). A pure leaf: its helpers 4953c4, 47eaa1/47df84 and
 * 47eabe/47de2d are computed in core, so there is no game call, no graph
 * and no callee counter. The entry registers are not read. A hit returns
 * the original's frame address EBP-28h in EDX; the adapter supplies its own
 * scratch frame as the stand-in (EDX is dead at both callers). It accepts
 * compare mode with a 12-byte span on out (written only on a
 * hit) and a comparator: a miss compares EAX = 0 and EDX; a hit compares
 * EAX = (out & FFFFFF00h) | 1 with out remapped to the clone and requires
 * both EDX values to be addresses in the current thread's stack (the two
 * frames). No game instruction is embedded. */
#include "es2/core/terrain.h"
static uint64_t __cdecl line_plane_hook(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[64]={0};(void)u;(void)r;
    return es2_line_plane_registers(&math_native_memory,(uint32_t)(uintptr_t)(frame+0x40),a[0],a[1],a[2]);
}
static const BYTE line_plane_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xc4,0x53,0x56,0x57};
static const BYTE line_plane_frame_lens[]={1,2,3};
static const es2_hook_extension line_plane_line_plane_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,line_plane_hook,FALSE};
static uint32_t __cdecl line_plane_hook32(const uint32_t *a,void *u) {return (uint32_t)line_plane_hook(a,u,NULL);}
/* Compare. An address in the committed stack of the current thread
 * (TIB StackLimit..StackBase): the original's EBP-28h and the adapter's
 * scratch frame both lie there. */
static int line_plane_stack_address(uint32_t value) {
    const NT_TIB *t=(const NT_TIB *)NtCurrentTeb();
    return value>=(uint32_t)(uintptr_t)t->StackLimit&&value<(uint32_t)(uintptr_t)t->StackBase;
}
static const es2_hook_span line_plane_spans[]={{2,0,12}};
static BOOL __cdecl line_plane_equal(const es2_hook_comparison *v,void *u) {
    (void)u;const uint32_t o=(uint32_t)(uintptr_t)v->original[0]&0xffffff00u,r=(uint32_t)(uintptr_t)v->replacement[0]&0xffffff00u;
    if(memcmp(v->original[0],v->replacement[0],12))return FALSE;
    if(!v->original_result)return !v->replacement_result&&v->original_edx==v->replacement_edx;
    return v->original_result==(o|1u)&&v->replacement_result==(r|1u)&&
        line_plane_stack_address(v->original_edx)&&line_plane_stack_address(v->replacement_edx);
}
static es2_hook_descriptor line_plane_hook_descriptors[]={
    {ES2_HOOK_ID(line_plane),line_plane_sig,9,6,line_plane_frame_lens,3,3,line_plane_hook32,line_plane_equal,NULL,line_plane_spans,1},
};
#endif
