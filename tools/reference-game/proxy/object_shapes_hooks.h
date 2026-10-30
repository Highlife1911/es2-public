#ifndef ES2_OBJECT_SHAPES_HOOKS_H
#define ES2_OBJECT_SHAPES_HOOKS_H
/* object_shapes group (docs/reverse-engineering/mission_load.md,
 * section "object shape set-up"): the object shape set-up of the
 * mission load, reached from the constructors mech_ctor,
 * flyer_ctor and building_factory (mission_ctors): ts_obj_ctor 402278 (the TS_OBJ base
 * constructor with the animated shape instance), the DET_ANIM_OBJ
 * constructors det_anim_shape 40332c, det_anim_static 403368 (over
 * object_init) and det_anim_record 4033a4, anim_instance_ctor 478838 (the
 * 42h-byte shape instance), object_track 402374 (an object's animation
 * thread; also called by the weapon set-up 40df30), thread_add 478a3c,
 * thread_ctor 478e48 and shape_word_block 402fc0 (called by 40df30,
 * 42067c and 4031d4, which nothing calls). They run during the load (and
 * 402fc0 with the weapon constructors), so they are live only with the
 * early arming (the default).
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX, so the group's own hooks
 * call each other through the bridge and keep their modes, and so do
 * shape_instance, node_matrices, object_init, track_clip, track_rate and
 * track_sort; the runtime (49630c, new 47749c, new[] 4774f0, 49667c with
 * the element constructors 4922e6 and matrix_ctor), the arena allocation
 * 47a288 and the CW3220 memset/memcpy thunks stay guest calls. ts_obj_ctor
 * and object_track set up the runtime record through 49630c with the
 * synthetic EBP (the frame in a zeroed scratch, the locals at the
 * original's offsets) through weapons_impact_guest_call, and their adapters
 * write the record's saved head back to FS:0 as the originals' epilogues do
 * (ts_obj_ctor: ECX, object_track: EDX).
 *
 * EDX:EAX exact; ECX is dead after every call site (mission_load.md), so it
 * is not returned (the framework returns the entry ECX). No function reads
 * a caller register other than EAX/EDX/ECX or an uninitialized local: no
 * entry or exit shim. All nine refuse compare mode (allocation,
 * constructors, the construction count, the runtime record, game calls);
 * the load is accepted by the after-load snapshot. No game instruction is
 * embedded. */
#include "es2/core/object_shapes.h"
#define OBJECT_SHAPES_FRAMED(name,index) \
    BYTE scratch[ES2_OBJECT_SHAPES_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##index); \
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_OBJECT_SHAPES_FRAME)}; \
    g.call=weapons_impact_guest_call;g.context=&c
static uint64_t __cdecl object_shapes_ts_obj_ctor(const uint32_t *a,void *u,const uint32_t *r) {
    OBJECT_SHAPES_FRAMED(ts_obj_ctor,ts_obj_ctor);
    uint32_t head=0;
    const uint64_t result=es2_ts_obj_ctor_registers(&math_native_memory,&g,a[0],a[1],r,&head);
    __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
static uint64_t __cdecl object_shapes_object_track(const uint32_t *a,void *u,const uint32_t *r) {
    OBJECT_SHAPES_FRAMED(object_track,object_track);
    const uint64_t result=es2_object_track_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
    const uint32_t head=(uint32_t)(result>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
#undef OBJECT_SHAPES_FRAMED
#define OBJECT_SHAPES_PLAIN(name) \
    static uint64_t __cdecl object_shapes_##name(const uint32_t *a,void *u,const uint32_t *r) { \
        es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name); \
        return es2_##name##_registers(&math_native_memory,&g,a[0],a[1],r,NULL); }
OBJECT_SHAPES_PLAIN(det_anim_shape)
OBJECT_SHAPES_PLAIN(det_anim_static)
OBJECT_SHAPES_PLAIN(det_anim_record)
OBJECT_SHAPES_PLAIN(anim_instance_ctor)
OBJECT_SHAPES_PLAIN(thread_add)
OBJECT_SHAPES_PLAIN(thread_ctor)
OBJECT_SHAPES_PLAIN(shape_word_block)
#undef OBJECT_SHAPES_PLAIN
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA. */
static callee_counter ts_obj_ctor_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_478838",ES2_COLLIDE_CALL,0x478838,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
};
static callee_counter det_anim_shape_counters[]={
    {"call_402278",ES2_COLLIDE_CALL,0x402278,0},
};
static callee_counter det_anim_static_counters[]={
    {"call_402188",ES2_COLLIDE_CALL,0x402188,0},
};
static callee_counter det_anim_record_counters[]={
    {"call_402278",ES2_COLLIDE_CALL,0x402278,0},
};
static callee_counter anim_instance_ctor_counters[]={
    {"call_490b64",ES2_COLLIDE_CALL,0x490b64,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_478c64",ES2_COLLIDE_CALL,0x478c64,0},
};
static callee_counter object_track_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_478e48",ES2_COLLIDE_CALL,0x478e48,0},
    {"call_4792ac",ES2_COLLIDE_CALL,0x4792ac,0},
    {"call_4796d8",ES2_COLLIDE_CALL,0x4796d8,0},
    {"call_478a3c",ES2_COLLIDE_CALL,0x478a3c,0},
};
static callee_counter thread_add_counters[]={
    {"call_4789d0",ES2_COLLIDE_CALL,0x4789d0,0},
};
static callee_counter thread_ctor_counters[]={
    {"call_4792ac",ES2_COLLIDE_CALL,0x4792ac,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
};
static callee_counter shape_word_block_counters[]={
    {"call_47a288",ES2_COLLIDE_CALL,0x47a288,0},
    {"call_49680e",ES2_COLLIDE_CALL,0x49680e,0},
};
#define OBJECT_SHAPES_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *object_shapes_hook_counters(unsigned hook,unsigned *count) {
    OBJECT_SHAPES_COUNTERS(ts_obj_ctor)
    OBJECT_SHAPES_COUNTERS(det_anim_shape)
    OBJECT_SHAPES_COUNTERS(det_anim_static)
    OBJECT_SHAPES_COUNTERS(det_anim_record)
    OBJECT_SHAPES_COUNTERS(anim_instance_ctor)
    OBJECT_SHAPES_COUNTERS(object_track)
    OBJECT_SHAPES_COUNTERS(thread_add)
    OBJECT_SHAPES_COUNTERS(thread_ctor)
    OBJECT_SHAPES_COUNTERS(shape_word_block)
    *count=0;return NULL;
}
#undef OBJECT_SHAPES_COUNTERS
/* 402278: push ebp; mov ebp,esp; add esp,-38h; mov eax,497145h; push ebx.
 * Relocated 6 bytes (1,2,3); the descriptor load and the call of 49630c
 * that follow stay in place.
 * 40332c, 403368: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; mov
 * eax,[ebp+0Ch]; push eax; push ebx. Relocated 7 bytes (1,2,1,3).
 * 4033a4: push ebp; mov ebp,esp; push ebx; push esi; mov esi,[ebp+0Ch];
 * mov ebx,[ebp+8]; mov eax,[esi]. Relocated 5 bytes (1,2,1,1).
 * 478838: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov
 * ebx,[ebp+8]; mov eax,[ebp+0Ch]. Relocated 5 bytes (1,2,1,1).
 * 402374: push ebp; mov ebp,esp; add esp,-24h; mov eax,497175h; push ebx.
 * Relocated 6 bytes (1,2,3).
 * 478a3c: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+0Ch]; mov
 * ecx,[ebp+8]. Relocated 7 bytes (1,2,1,3).
 * 478e48: push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8]; mov
 * edx,[ebp+0Ch]. Relocated 5 bytes (1,2,1,1).
 * 402fc0: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov
 * ebx,[ebp+8]; mov eax,[ebp+0Ch]. Relocated 5 bytes (1,2,1,1). */
static const BYTE ts_obj_ctor_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xc8,0xb8,0x45,0x71,0x49,0x00,0x53};
static const BYTE det_anim_shape_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x8b,0x45,0x0c,0x50,0x53};
static const BYTE det_anim_static_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x8b,0x45,0x0c,0x50,0x53};
static const BYTE det_anim_record_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x0c,0x8b,0x5d,0x08,0x8b,0x06};
static const BYTE anim_instance_ctor_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08,0x8b,0x45,0x0c};
static const BYTE object_track_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0xb8,0x75,0x71,0x49,0x00,0x53};
static const BYTE thread_add_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x0c,0x8b,0x4d,0x08};
static const BYTE thread_ctor_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08,0x8b,0x55,0x0c};
static const BYTE shape_word_block_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08,0x8b,0x45,0x0c};
static const BYTE object_shapes_frame_lens[]={1,2,3},object_shapes_load_lens[]={1,2,1,3},object_shapes_push_lens[]={1,2,1,1};
#define OBJECT_SHAPES_EXT(name) \
    static const es2_hook_extension object_shapes_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,object_shapes_##name,TRUE}; \
    static uint32_t __cdecl object_shapes_##name##32(const uint32_t *a,void *u) {return (uint32_t)object_shapes_##name(a,u,weapons_no_registers);}
OBJECT_SHAPES_EXT(ts_obj_ctor)
OBJECT_SHAPES_EXT(det_anim_shape)
OBJECT_SHAPES_EXT(det_anim_static)
OBJECT_SHAPES_EXT(det_anim_record)
OBJECT_SHAPES_EXT(anim_instance_ctor)
OBJECT_SHAPES_EXT(object_track)
OBJECT_SHAPES_EXT(thread_add)
OBJECT_SHAPES_EXT(thread_ctor)
OBJECT_SHAPES_EXT(shape_word_block)
#undef OBJECT_SHAPES_EXT
static es2_hook_descriptor object_shapes_hook_descriptors[]={
    {ES2_HOOK_ID(ts_obj_ctor),ts_obj_ctor_sig,12,6,object_shapes_frame_lens,3,2,object_shapes_ts_obj_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(det_anim_shape),det_anim_shape_sig,12,7,object_shapes_load_lens,4,2,object_shapes_det_anim_shape32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(det_anim_static),det_anim_static_sig,12,7,object_shapes_load_lens,4,2,object_shapes_det_anim_static32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(det_anim_record),det_anim_record_sig,13,5,object_shapes_push_lens,4,2,object_shapes_det_anim_record32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(anim_instance_ctor),anim_instance_ctor_sig,12,5,object_shapes_push_lens,4,2,object_shapes_anim_instance_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(object_track),object_track_sig,12,6,object_shapes_frame_lens,3,2,object_shapes_object_track32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(thread_add),thread_add_sig,10,7,object_shapes_load_lens,4,2,object_shapes_thread_add32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(thread_ctor),thread_ctor_sig,11,5,object_shapes_push_lens,4,2,object_shapes_thread_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(shape_word_block),shape_word_block_sig,12,5,object_shapes_push_lens,4,2,object_shapes_shape_word_block32,NULL,NULL,NULL,0},
};
#endif
