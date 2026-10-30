#ifndef ES2_DESTRUCTORS_HOOKS_H
#define ES2_DESTRUCTORS_HOOKS_H
/* Object destructors (docs/reverse-engineering/main_loop.md, section
 * "object destructors"): slot +8 of EXPLOSION 407e48, FIRE 46b574,
 * BEAM 40ba94, DEBRIS 40f7e6, FlatObj 41bd46, METEOR 424034, BULLET 40b64d
 * and ROCKET 40ab8d, the shape-instance destructor 490bdc and its base
 * 48be20. Every game call goes through the weapons guest bridge and callee
 * observer (weapons_graph) with the original's registers: the runtime frame
 * setup 49630c (synthetic EBP in a scratch, ES2_DESTRUCTORS_FRAME, through
 * weapons_impact_guest_call), the runtime 496406, the heap free 477548, the
 * arena free 47758c, the shape instance's virtual +18h and the hooks
 * removal_append, sfx_stop and shape_base_dtor, which keep their own modes.
 * The eight object destructors' adapters restore FS:0 to the record's saved
 * head, as the epilogues do. EDX:EAX exact; ECX is dead at the callers
 * (removal_flush, the TS_OBJ tails). All ten refuse compare mode (game
 * calls, frees, the FS:0 record). No entry shim, no game instruction
 * embedded. */
#include "es2/core/destructors.h"
static uint64_t destructors_object(const uint32_t *a,void *u,const uint32_t *r,unsigned kind,unsigned hook) {
    BYTE scratch[ES2_DESTRUCTORS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,hook);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_DESTRUCTORS_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;uint32_t ecx,head;
    uint64_t result=es2_object_dtor_registers(&math_native_memory,&g,kind,a[0],a[1],r,&ecx,&head);
    es2_machine_set_fs0(head);return result;
}
#define DESTRUCTORS_OBJECT(name,kind) \
static uint64_t __cdecl destructors_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    return destructors_object(a,u,r,kind,ES2_HOOK_INDEX_##name); }
DESTRUCTORS_OBJECT(explosion_dtor,ES2_DTOR_EXPLOSION)
DESTRUCTORS_OBJECT(fire_dtor,ES2_DTOR_FIRE)
DESTRUCTORS_OBJECT(beam_dtor,ES2_DTOR_BEAM)
DESTRUCTORS_OBJECT(debris_dtor,ES2_DTOR_DEBRIS)
DESTRUCTORS_OBJECT(flat_dtor,ES2_DTOR_FLAT)
DESTRUCTORS_OBJECT(meteor_dtor,ES2_DTOR_METEOR)
DESTRUCTORS_OBJECT(bullet_dtor,ES2_DTOR_BULLET)
DESTRUCTORS_OBJECT(rocket_dtor,ES2_DTOR_ROCKET)
#undef DESTRUCTORS_OBJECT
static uint64_t __cdecl destructors_shape_dtor(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_shape_dtor);uint32_t ecx;
    return es2_shape_dtor_registers(&math_native_memory,&g,a[0],a[1],r,&ecx);
}
static uint64_t __cdecl destructors_shape_base_dtor(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_shape_base_dtor);uint32_t ecx;
    return es2_shape_base_dtor_registers(&math_native_memory,&g,a[0],a[1],r,&ecx);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook
 * is non-original. Keys: the callee VA, or the vtable offset for the slot. */
#define DESTRUCTORS_TAIL_COUNTERS \
    {"call_496406",ES2_COLLIDE_CALL,0x496406,0}, \
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0}, \
    {"call_477548",ES2_COLLIDE_CALL,0x477548,0},
static callee_counter explosion_dtor_counters[]={{"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_478628",ES2_COLLIDE_CALL,0x478628,0},DESTRUCTORS_TAIL_COUNTERS};
static callee_counter fire_dtor_counters[]={{"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_462ac4",ES2_COLLIDE_CALL,0x462ac4,0},DESTRUCTORS_TAIL_COUNTERS};
static callee_counter beam_dtor_counters[]={{"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_47758c",ES2_COLLIDE_CALL,0x47758c,0},DESTRUCTORS_TAIL_COUNTERS};
static callee_counter debris_dtor_counters[]={{"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},DESTRUCTORS_TAIL_COUNTERS};
static callee_counter flat_dtor_counters[]={{"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},DESTRUCTORS_TAIL_COUNTERS};
static callee_counter meteor_dtor_counters[]={{"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},DESTRUCTORS_TAIL_COUNTERS};
static callee_counter bullet_dtor_counters[]={{"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},DESTRUCTORS_TAIL_COUNTERS};
static callee_counter rocket_dtor_counters[]={{"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},DESTRUCTORS_TAIL_COUNTERS};
#undef DESTRUCTORS_TAIL_COUNTERS
static callee_counter shape_dtor_counters[]={
    {"call_47758c",ES2_COLLIDE_CALL,0x47758c,0},
    {"call_48be20",ES2_COLLIDE_CALL,0x48be20,0},
    {"call_477548",ES2_COLLIDE_CALL,0x477548,0},
};
static callee_counter shape_base_dtor_counters[]={
    {"call_477548",ES2_COLLIDE_CALL,0x477548,0},
};
static callee_counter *destructors_hook_counters(unsigned hook,unsigned *count) {
#define DESTRUCTORS_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
    DESTRUCTORS_COUNTERS(explosion_dtor) DESTRUCTORS_COUNTERS(fire_dtor) DESTRUCTORS_COUNTERS(beam_dtor)
    DESTRUCTORS_COUNTERS(debris_dtor) DESTRUCTORS_COUNTERS(flat_dtor) DESTRUCTORS_COUNTERS(meteor_dtor)
    DESTRUCTORS_COUNTERS(bullet_dtor) DESTRUCTORS_COUNTERS(rocket_dtor)
    DESTRUCTORS_COUNTERS(shape_dtor) DESTRUCTORS_COUNTERS(shape_base_dtor)
#undef DESTRUCTORS_COUNTERS
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-24h; mov eax,<descriptor>; push ebx;
 * [push esi;] mov ebx,[ebp+8] */
static const BYTE destructors_explosion_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0xb8,0x28,0x7c,0x49,0x00,0x53,0x56,0x8b,0x5d,0x08};
static const BYTE destructors_fire_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0xb8,0xf1,0x0a,0x4a,0x00,0x53,0x56,0x8b,0x5d,0x08};
static const BYTE destructors_beam_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0xb8,0x7c,0x86,0x49,0x00,0x53,0x56,0x8b,0x5d,0x08};
static const BYTE destructors_debris_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0xb8,0x18,0x8a,0x49,0x00,0x53,0x8b,0x5d,0x08};
static const BYTE destructors_flat_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0xb8,0x06,0xa2,0x49,0x00,0x53,0x8b,0x5d,0x08};
static const BYTE destructors_meteor_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0xb8,0xec,0xa6,0x49,0x00,0x53,0x8b,0x5d,0x08};
/* push ebp; mov ebp,esp; add esp,-24h; push ebx; push esi; mov ebx,[ebp+8];
 * mov esi,6cbee4h; mov eax,<descriptor> */
static const BYTE destructors_bullet_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0x53,0x56,0x8b,0x5d,0x08,0xbe,0xe4,0xbe,0x6c,0x00,
    0xb8,0x79,0x85,0x49,0x00};
static const BYTE destructors_rocket_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0x53,0x56,0x8b,0x5d,0x08,0xbe,0xe4,0xbe,0x6c,0x00,
    0xb8,0xa0,0x83,0x49,0x00};
static const BYTE destructors_frame_lens[]={1,2,3};
/* 490bdc: push ebp; mov ebp,esp; mov eax,[6cbee4h]; push ebx; mov ebx,[ebp+8] */
static const BYTE destructors_shape_sig[]={0x55,0x8b,0xec,0xa1,0xe4,0xbe,0x6c,0x00,0x53,0x8b,0x5d,0x08};
static const BYTE destructors_shape_lens[]={1,2,5};
/* 48be20: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov edx,[6cbee4h] */
static const BYTE destructors_base_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x15,0xe4,0xbe,0x6c,0x00};
#define DESTRUCTORS_EXTENSION(name) \
static const es2_hook_extension destructors_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0, \
    destructors_##name,TRUE}; \
static uint32_t __cdecl destructors_##name##32(const uint32_t *a,void *u) { \
    return (uint32_t)destructors_##name(a,u,weapons_no_registers); }
DESTRUCTORS_EXTENSION(explosion_dtor)
DESTRUCTORS_EXTENSION(fire_dtor)
DESTRUCTORS_EXTENSION(beam_dtor)
DESTRUCTORS_EXTENSION(debris_dtor)
DESTRUCTORS_EXTENSION(flat_dtor)
DESTRUCTORS_EXTENSION(meteor_dtor)
DESTRUCTORS_EXTENSION(bullet_dtor)
DESTRUCTORS_EXTENSION(rocket_dtor)
DESTRUCTORS_EXTENSION(shape_dtor)
DESTRUCTORS_EXTENSION(shape_base_dtor)
#undef DESTRUCTORS_EXTENSION
static es2_hook_descriptor destructors_hook_descriptors[]={
    {ES2_HOOK_ID(explosion_dtor),destructors_explosion_sig,16,6,destructors_frame_lens,3,2,destructors_explosion_dtor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(fire_dtor),destructors_fire_sig,16,6,destructors_frame_lens,3,2,destructors_fire_dtor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(beam_dtor),destructors_beam_sig,16,6,destructors_frame_lens,3,2,destructors_beam_dtor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(debris_dtor),destructors_debris_sig,15,6,destructors_frame_lens,3,2,destructors_debris_dtor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flat_dtor),destructors_flat_sig,15,6,destructors_frame_lens,3,2,destructors_flat_dtor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(meteor_dtor),destructors_meteor_sig,15,6,destructors_frame_lens,3,2,destructors_meteor_dtor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(bullet_dtor),destructors_bullet_sig,21,6,destructors_frame_lens,3,2,destructors_bullet_dtor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(rocket_dtor),destructors_rocket_sig,21,6,destructors_frame_lens,3,2,destructors_rocket_dtor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(shape_dtor),destructors_shape_sig,12,8,destructors_shape_lens,3,2,destructors_shape_dtor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(shape_base_dtor),destructors_base_sig,12,6,destructors_frame_lens,3,2,destructors_shape_base_dtor32,NULL,NULL,NULL,0},
};
#endif
