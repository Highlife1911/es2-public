#ifndef ES2_RESULTS_WRITER_HOOKS_H
#define ES2_RESULTS_WRITER_HOOKS_H
/* Results writer 42412c and its helpers score_sum 423e88,
 * vehicle_value 418e60 and list_append 426ac8
 * (docs/reverse-engineering/mission_runtime.md, section "results
 * writer 42412c and its helpers"). Every game call goes through the
 * weapons guest bridge and callee observer (weapons_graph) with the
 * original's registers, so the nested hooks (objective_eval, score_sum,
 * multiply_q10, result_conditions, pool_prev, vehicle_value, part_ratio,
 * list_append, damage_score) keep their own modes. The writer's frame
 * lives in scratch at the original's EBP offsets: the runtime frame setup
 * 49630c gets the synthetic EBP (ES2_RESULTS_WRITER_FRAME) through
 * weapons_impact_guest_call, the stream objects are built there by the
 * original 47b064, and the adapter restores FS:0 to the record's saved
 * head (the returned EDX) as the original's epilogue does. The stream
 * class (write virtual +14h, 47b2b8, the closes 47b09c), the path builder
 * 45ee00 and the fatal report 479c78 stay original (file I/O).
 * vehicle_value keeps its two frame dwords in 8 scratch bytes. EDX:EAX
 * exact; ECX is dead at every caller. All four refuse compare mode (file
 * writes, game calls, runtime-indexed stores). No entry shim, no game
 * instruction embedded. */
#include "es2/core/results_writer.h"
static uint64_t __cdecl results_writer_results_writer(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_RESULTS_WRITER_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_results_writer);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_RESULTS_WRITER_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t result=es2_results_writer_registers(&math_native_memory,&g,a[0],r,NULL);
    uint32_t head=(uint32_t)(result>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
static uint64_t __cdecl results_writer_score_sum(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_score_sum);
    return es2_score_sum_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl results_writer_vehicle_value(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_VEHICLE_VALUE_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_vehicle_value);
    return es2_vehicle_value_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl results_writer_list_append(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_list_append_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook
 * is non-original. Keys: the callee VA; slot_14 = the stream's write
 * virtual (3 + 2 x pairs per call). list_append has no game calls. */
static callee_counter results_writer_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_413280",ES2_COLLIDE_CALL,0x413280,0},
    {"call_45ee00",ES2_COLLIDE_CALL,0x45ee00,0},
    {"call_47b064",ES2_COLLIDE_CALL,0x47b064,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"slot_14",ES2_COLLIDE_SLOT,0x14,0},
    {"call_423e88",ES2_COLLIDE_CALL,0x423e88,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_423d68",ES2_COLLIDE_CALL,0x423d68,0},
    {"call_47b09c",ES2_COLLIDE_CALL,0x47b09c,0},
    {"call_47b2b8",ES2_COLLIDE_CALL,0x47b2b8,0},
};
static callee_counter score_sum_counters[]={
    {"call_471c70",ES2_COLLIDE_CALL,0x471c70,0},
    {"call_418e60",ES2_COLLIDE_CALL,0x418e60,0},
};
static callee_counter vehicle_value_counters[]={
    {"call_40dbc0",ES2_COLLIDE_CALL,0x40dbc0,0},
    {"call_426ac8",ES2_COLLIDE_CALL,0x426ac8,0},
    {"call_41537c",ES2_COLLIDE_CALL,0x41537c,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
};
static callee_counter *results_writer_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_results_writer){*count=sizeof(results_writer_counters)/sizeof(results_writer_counters[0]);return results_writer_counters;}
    if(hook==ES2_HOOK_INDEX_score_sum){*count=sizeof(score_sum_counters)/sizeof(score_sum_counters[0]);return score_sum_counters;}
    if(hook==ES2_HOOK_INDEX_vehicle_value){*count=sizeof(vehicle_value_counters)/sizeof(vehicle_value_counters[0]);return vehicle_value_counters;}
    *count=0;return NULL;
}
/* 42412c: push ebp; mov ebp,esp; add esp,-4Ch; mov eax,49a76ch; push ebx; push esi; push edi */
static const BYTE results_writer_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb4,0xb8,0x6c,0xa7,0x49,0x00,0x53,0x56,0x57};
/* 423e88: push ebp; mov ebp,esp; add esp,-8; xor eax,eax; xor edx,edx; push ebx; push esi; push edi */
static const BYTE score_sum_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x33,0xc0,0x33,0xd2,0x53,0x56,0x57};
/* 418e60: push ebp; mov ebp,esp; add esp,-8; push ebx; push esi; push edi; mov ebx,[ebp+8] */
static const BYTE vehicle_value_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE results_writer_frame_lens[]={1,2,3};
/* 426ac8: push ebp; mov ebp,esp; movsx eax,word [4a9e6ah]; mov edx,[4a9e6ch] (absolute) */
static const BYTE list_append_sig[]={0x55,0x8b,0xec,0x0f,0xbf,0x05,0x6a,0x9e,0x4a,0x00,0x8b,0x15,0x6c,0x9e,0x4a,0x00};
static const BYTE list_append_lens[]={1,2,7};
#define RESULTS_WRITER_EXT(name) \
    static const es2_hook_extension results_writer_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,results_writer_##name,TRUE}; \
    static uint32_t __cdecl results_writer_##name##32(const uint32_t *a,void *u) {return (uint32_t)results_writer_##name(a,u,weapons_no_registers);}
RESULTS_WRITER_EXT(results_writer)
RESULTS_WRITER_EXT(score_sum)
RESULTS_WRITER_EXT(vehicle_value)
RESULTS_WRITER_EXT(list_append)
#undef RESULTS_WRITER_EXT
static es2_hook_descriptor results_writer_hook_descriptors[]={
    {ES2_HOOK_ID(results_writer),results_writer_sig,14,6,results_writer_frame_lens,3,1,results_writer_results_writer32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(score_sum),score_sum_sig,13,6,results_writer_frame_lens,3,1,results_writer_score_sum32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(vehicle_value),vehicle_value_sig,12,6,results_writer_frame_lens,3,1,results_writer_vehicle_value32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(list_append),list_append_sig,16,10,list_append_lens,3,1,results_writer_list_append32,NULL,NULL,NULL,0},
};
#endif
