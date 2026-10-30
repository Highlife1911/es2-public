#ifndef ES2_METEOR_HOOKS_H
#define ES2_METEOR_HOOKS_H
/* Meteor constructor 409b44 and shape instance 490b64
 * (docs/reverse-engineering/main_loop.md, section "meteor
 * constructor 409b44 and shape instance 490b64"). Every game call goes
 * through the weapons guest bridge and callee observer (weapons_graph)
 * with the original's registers, so the nested hooks (object_init,
 * rng_next, rng_bounded, sincos_pair, rotate_2d, shape_instance) keep
 * their own modes; the runtime frame setup 49630c gets the synthetic EBP
 * (ES2_METEOR_FRAME in a scratch) through weapons_impact_guest_call, and
 * the constructor's adapter restores FS:0 to the record's saved head (the
 * core's ECX), as the original's epilogue does. The heap 47749c, the
 * rotation 47ff8c, the arena allocation 47a288 and the memcpy thunk
 * 49680e stay original. EDX:EAX exact; ECX is dead at every caller. Both
 * refuse compare mode (game calls, allocations). No entry shim, no game
 * instruction embedded. */
#include "es2/core/meteor.h"
static uint64_t __cdecl meteor_meteor_create(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_METEOR_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_meteor_create);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_METEOR_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;uint32_t head;
    uint64_t result=es2_meteor_create_registers(&math_native_memory,&g,a[0],a[1],a[2],r,&head);
    es2_machine_set_fs0(head);return result;
}
static uint64_t __cdecl meteor_shape_instance(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_shape_instance);uint32_t ecx;
    return es2_shape_instance_registers(&math_native_memory,&g,a[0],a[1],r,&ecx);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook
 * is non-original. Keys: the callee VA. */
static callee_counter meteor_create_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_402188",ES2_COLLIDE_CALL,0x402188,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_47eb30",ES2_COLLIDE_CALL,0x47eb30,0},
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"call_47ffe8",ES2_COLLIDE_CALL,0x47ffe8,0},
    {"call_47ff8c",ES2_COLLIDE_CALL,0x47ff8c,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_490b64",ES2_COLLIDE_CALL,0x490b64,0},
};
static callee_counter meteor_shape_counters[]={
    {"call_47a288",ES2_COLLIDE_CALL,0x47a288,0},
    {"call_49680e",ES2_COLLIDE_CALL,0x49680e,0},
};
static callee_counter *meteor_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_meteor_create){*count=sizeof(meteor_create_counters)/sizeof(meteor_create_counters[0]);return meteor_create_counters;}
    if(hook==ES2_HOOK_INDEX_shape_instance){*count=sizeof(meteor_shape_counters)/sizeof(meteor_shape_counters[0]);return meteor_shape_counters;}
    *count=0;return NULL;
}
/* 409b44: push ebp; mov ebp,esp; add esp,-48h; mov eax,498170h; push ebx; push esi; push edi */
static const BYTE meteor_create_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb8,0xb8,0x70,0x81,0x49,0x00,0x53,0x56,0x57};
static const BYTE meteor_create_lens[]={1,2,3};
/* 490b64: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov ebx,[ebp+8] */
static const BYTE meteor_shape_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE meteor_shape_lens[]={1,2,1,1};
#define METEOR_EXTENSION(name) \
static const es2_hook_extension meteor_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0, \
    meteor_##name,TRUE}; \
static uint32_t __cdecl meteor_##name##32(const uint32_t *a,void *u) { \
    return (uint32_t)meteor_##name(a,u,weapons_no_registers); }
METEOR_EXTENSION(meteor_create)
METEOR_EXTENSION(shape_instance)
#undef METEOR_EXTENSION
static es2_hook_descriptor meteor_hook_descriptors[]={
    {ES2_HOOK_ID(meteor_create),meteor_create_sig,14,6,meteor_create_lens,3,3,meteor_meteor_create32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(shape_instance),meteor_shape_sig,9,5,meteor_shape_lens,4,2,meteor_shape_instance32,NULL,NULL,NULL,0},
};
#endif
