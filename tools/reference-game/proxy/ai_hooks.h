#ifndef ES2_AI_HOOKS_H
#define ES2_AI_HOOKS_H
/* Vehicle AI group (docs/reverse-engineering/ai.md). Included after
 * weapons_hooks.h: the adapters reuse its guest-call bridge, graph context
 * and callee-counter observer. Both replacements call game code, so compare
 * mode is refused. Neither function reads uninitialized stack that reaches
 * an observable result, so no entry shim is installed. */
#include "es2/core/ai.h"
static uint64_t __cdecl ai_object_line(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_object_line);
    return es2_object_line_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r);
}
static uint64_t __cdecl ai_obstacle_steer(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[256]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_obstacle_steer);
    return es2_obstacle_steer_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static const BYTE ai_object_line_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xa0};
static const BYTE ai_obstacle_steer_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x18,0xff,0xff,0xff};
static const BYTE ai_short_frame_lens[]={1,2,3};
static const BYTE ai_long_frame_lens[]={1,2,6};
static const es2_hook_extension ai_object_line_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,ai_object_line,TRUE};
static const es2_hook_extension ai_obstacle_steer_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,ai_obstacle_steer,TRUE};
static uint32_t __cdecl ai_object_line32(const uint32_t *a,void *u) { return (uint32_t)ai_object_line(a,u,weapons_no_registers); }
static uint32_t __cdecl ai_obstacle_steer32(const uint32_t *a,void *u) { return (uint32_t)ai_obstacle_steer(a,u,weapons_no_registers); }
static es2_hook_descriptor ai_hook_descriptors[]={
    {ES2_HOOK_ID(object_line),ai_object_line_sig,6,6,ai_short_frame_lens,3,5,ai_object_line32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(obstacle_steer),ai_obstacle_steer_sig,9,9,ai_long_frame_lens,3,4,ai_obstacle_steer32,NULL,NULL,NULL,0},
};
#endif
