#ifndef ES2_EFFECT_CREATE_HOOKS_H
#define ES2_EFFECT_CREATE_HOOKS_H
/* Effect creation: explosion_create 407f1c, object_init 402188,
 * light_create 407604, pool_alloc 471b30 (docs/reverse-engineering/weapons.md,
 * section "effect creation"). Included after weapons_hooks.h: every game callee goes
 * through its guest bridge and callee observer (weapons_graph); the runtime
 * frame setup 49630c gets the synthetic EBP through weapons_impact_guest_call,
 * and the adapter restores FS:0 from the record's saved head, as the
 * original's epilogue does. All four refuse compare mode (pool links,
 * runtime count and constructor graphs are outside fixed spans). No game
 * instruction is embedded. */
#include "es2/core/effect_create.h"
static uint64_t __cdecl effect_pool_alloc(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_pool_alloc_registers(&math_native_memory,a[0],r);
}
static uint64_t __cdecl effect_light_create(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_light_create);(void)r;
    return es2_light_create_registers(&math_native_memory,&g,a[0],a[1],a[2]);
}
/* The runtime frame setup 0x49630c links its record into FS:0 and the
 * original's epilogue restores the entry head. The record lives in this
 * scratch frame, which does not survive the adapter, so FS:0 is restored to
 * the head it had at entry (exactly what the original leaves). */
static uint64_t __cdecl effect_object_init(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[256]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_object_init);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+208)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint32_t head;__asm__ volatile("movl %%fs:0,%0":"=r"(head));
    uint64_t result=es2_object_init_registers(&math_native_memory,&g,a[0],a[1],r);
    __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
static uint64_t __cdecl effect_explosion_create(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[256]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_explosion_create);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+208)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint32_t head;__asm__ volatile("movl %%fs:0,%0":"=r"(head));
    uint64_t result=es2_explosion_create_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r);
    __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
static const BYTE effect_explosion_create_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0x53,0x56,0x57};
static const BYTE effect_object_init_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xc8,0xb8,0x15,0x71,0x49,0x00};
static const BYTE effect_light_create_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08};
static const BYTE effect_pool_alloc_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x8b,0x45,0x08};
static const BYTE effect_frame_lens[]={1,2,3};
static const BYTE effect_push_lens[]={1,2,1,1};
#define EFFECT_EXT(name) static const es2_hook_extension effect_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,effect_##name,TRUE};
EFFECT_EXT(explosion_create) EFFECT_EXT(object_init) EFFECT_EXT(light_create) EFFECT_EXT(pool_alloc)
#undef EFFECT_EXT
#define EFFECT_LEGACY(name) static uint32_t __cdecl effect_##name##32(const uint32_t *a,void *u) {return (uint32_t)effect_##name(a,u,weapons_no_registers);}
EFFECT_LEGACY(explosion_create) EFFECT_LEGACY(object_init) EFFECT_LEGACY(light_create) EFFECT_LEGACY(pool_alloc)
#undef EFFECT_LEGACY
static es2_hook_descriptor effect_hook_descriptors[]={
    {ES2_HOOK_ID(explosion_create),effect_explosion_create_sig,9,6,effect_frame_lens,3,5,effect_explosion_create32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(object_init),effect_object_init_sig,11,6,effect_frame_lens,3,2,effect_object_init32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(light_create),effect_light_create_sig,8,5,effect_push_lens,4,3,effect_light_create32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(pool_alloc),effect_pool_alloc_sig,9,5,effect_push_lens,4,1,effect_pool_alloc32,NULL,NULL,NULL,0},
};
#endif
