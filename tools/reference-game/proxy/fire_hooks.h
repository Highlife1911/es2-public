#ifndef ES2_FIRE_HOOKS_H
#define ES2_FIRE_HOOKS_H
/* fire group (docs/reverse-engineering/main_loop.md, section "fire
 * allocation 46b430, fire constructor 46b48c, fire release 46b62c"):
 * fire_alloc 46b430, fire_create 46b48c and fire_release 46b62c, the FIRE
 * pool helpers called by base_parts (403914, through its guest bridge) and
 * the region damage 40d434. Every game call goes through the weapons
 * guest bridge and callee observer (weapons_graph) with the original's
 * entry EAX/EDX/ECX, so the nested hooks pool_alloc, pool_prev,
 * object_init, sfx_play (stream B), fire_update and removal_append keep
 * their own modes; the memset thunk 49681a stays original. EDX:EAX exact;
 * ECX is dead at every caller (fire_alloc's original returns an
 * uninitialized local there after a successful allocation). No entry
 * register beyond those passed on and no uninitialized local is read, so
 * no entry shim. All three refuse compare mode (game calls, pool
 * allocation). No game instruction is embedded. */
#include "es2/core/fire.h"
static uint64_t __cdecl fire_fire_alloc(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_fire_alloc);uint32_t ecx;
    return es2_fire_alloc_registers(&math_native_memory,&g,r,&ecx);
}
static uint64_t __cdecl fire_fire_create(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_fire_create);uint32_t ecx;
    return es2_fire_create_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r,&ecx);
}
static uint64_t __cdecl fire_fire_release(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_fire_release);uint32_t ecx;
    return es2_fire_release_registers(&math_native_memory,&g,a[0],r,&ecx);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook
 * is non-original. Keys: the callee VA. */
static callee_counter fire_alloc_counters[]={
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_471c70",ES2_COLLIDE_CALL,0x471c70,0},
};
static callee_counter fire_create_counters[]={
    {"call_402188",ES2_COLLIDE_CALL,0x402188,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"call_462830",ES2_COLLIDE_CALL,0x462830,0},
    {"call_46b678",ES2_COLLIDE_CALL,0x46b678,0},
    {"call_478628",ES2_COLLIDE_CALL,0x478628,0},
};
static callee_counter fire_release_counters[]={
    {"call_471c70",ES2_COLLIDE_CALL,0x471c70,0},
    {"call_478628",ES2_COLLIDE_CALL,0x478628,0},
};
static callee_counter *fire_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_fire_alloc){*count=sizeof(fire_alloc_counters)/sizeof(fire_alloc_counters[0]);return fire_alloc_counters;}
    if(hook==ES2_HOOK_INDEX_fire_create){*count=sizeof(fire_create_counters)/sizeof(fire_create_counters[0]);return fire_create_counters;}
    if(hook==ES2_HOOK_INDEX_fire_release){*count=sizeof(fire_release_counters)/sizeof(fire_release_counters[0]);return fire_release_counters;}
    *count=0;return NULL;
}
/* 46b430: push ebp; mov ebp,esp; add esp,-8; push ebx; push esi; push edi; mov eax,[6b4f30h] */
static const BYTE fire_alloc_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x57,0xa1,0x30,0x4f,0x6b,0x00};
static const BYTE fire_alloc_lens[]={1,2,3};
/* 46b48c: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov edx,[6b4f28h] */
static const BYTE fire_create_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x15,0x28,0x4f,0x6b,0x00};
static const BYTE fire_create_lens[]={1,2,1,1};
/* 46b62c: push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi; mov edi,[ebp+8] */
static const BYTE fire_release_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x7d,0x08};
static const BYTE fire_release_lens[]={1,2,1,1};
#define FIRE_EXTENSION(name) \
static const es2_hook_extension fire_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0, \
    fire_##name,TRUE}; \
static uint32_t __cdecl fire_##name##32(const uint32_t *a,void *u) { \
    return (uint32_t)fire_##name(a,u,weapons_no_registers); }
FIRE_EXTENSION(fire_alloc)
FIRE_EXTENSION(fire_create)
FIRE_EXTENSION(fire_release)
#undef FIRE_EXTENSION
static es2_hook_descriptor fire_hook_descriptors[]={
    {ES2_HOOK_ID(fire_alloc),fire_alloc_sig,14,6,fire_alloc_lens,3,1,fire_fire_alloc32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(fire_create),fire_create_sig,12,5,fire_create_lens,4,5,fire_fire_create32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(fire_release),fire_release_sig,10,5,fire_release_lens,4,1,fire_fire_release32,NULL,NULL,NULL,0},
};
#endif
