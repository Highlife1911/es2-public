#ifndef ES2_ACTOR_VIRTUALS_HOOKS_H
#define ES2_ACTOR_VIRTUALS_HOOKS_H
/* actor_virtuals group (docs/reverse-engineering/vtables.md, section
 * "actor virtuals"): eleven small actor virtuals with simulation
 * reads, one hook per function: base_aim_node 403548, base_offsets 40351c,
 * base_condition 4052b4, base_part_position 406808 (BASE +24h/+30h/+40h/
 * +58h), actor_attacked 411aec (ACTOR +50h), mech_aim_node 417b98,
 * mech_offsets 4155c4, mech_armour 4154d0, mech_target_scale 415498,
 * mech_engage_roll 41cabc and mech_link_position 41dd2c (MECH +24h/+30h/
 * +34h/+38h/+4Ch/+64h). Core in core/src/actor_virtuals.c. Every game
 * callee goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's registers, so the nested hooks
 * (contact_share, euler_matrix, multiply_q10, rng_next) keep their modes and
 * memset 49681a, 411a74, 4801d4 and the shape's slot +20h run as original
 * code. base_condition's IDIV fault (summed maxima 0, or 80000000h / -1) is
 * raised by an IDIV here with the original's operands. No local is read
 * before it is written and ECX/flags at return are dead at every listed
 * call site, so no entry shim. Eight refuse compare mode; the readers base_condition, mech_armour and mech_target_scale (only callee
 * multiply_q10, pure) accept it (base_condition's faulting inputs leave
 * through the detour in any non-original mode, outside the framework
 * contract). No game instruction is embedded. */
#include "es2/core/actor_virtuals.h"
#define ACTOR_VIRTUALS_GRAPH(name) es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name)
static uint64_t __cdecl actor_virtuals_base_aim_node(const uint32_t *a,void *u,const uint32_t *r) {
    ACTOR_VIRTUALS_GRAPH(base_aim_node);return es2_base_aim_node_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl actor_virtuals_base_offsets(const uint32_t *a,void *u,const uint32_t *r) {
    ACTOR_VIRTUALS_GRAPH(base_offsets);return es2_base_offsets_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl actor_virtuals_base_condition(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;uint32_t operands[2];int fault=0;
    uint64_t result=es2_base_condition_registers(&math_native_memory,a[0],operands,&fault);
    if(fault) {
        /* The original's IDIV DWORD [EBP-4] at 4052fe faults here (#DE). */
        uint32_t eax=operands[0],edx;
        __asm__ volatile("cltd\n\tidivl %2":"+a"(eax),"=&d"(edx):"r"(operands[1]):"cc");
        result=(uint64_t)edx<<32|eax;
    }
    return result;
}
static uint64_t __cdecl actor_virtuals_actor_attacked(const uint32_t *a,void *u,const uint32_t *r) {
    ACTOR_VIRTUALS_GRAPH(actor_attacked);return es2_actor_attacked_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl actor_virtuals_base_part_position(const uint32_t *a,void *u,const uint32_t *r) {
    ACTOR_VIRTUALS_GRAPH(base_part_position);return es2_base_part_position_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl actor_virtuals_mech_aim_node(const uint32_t *a,void *u,const uint32_t *r) {
    ACTOR_VIRTUALS_GRAPH(mech_aim_node);return es2_mech_aim_node_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl actor_virtuals_mech_offsets(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_mech_offsets_registers(&math_native_memory,a[0],a[1],a[2]);
}
static uint64_t __cdecl actor_virtuals_mech_armour(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_mech_armour_registers(&math_native_memory,a[0],a[1]);
}
static uint64_t __cdecl actor_virtuals_mech_target_scale(const uint32_t *a,void *u,const uint32_t *r) {
    ACTOR_VIRTUALS_GRAPH(mech_target_scale);return es2_mech_target_scale_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl actor_virtuals_mech_engage_roll(const uint32_t *a,void *u,const uint32_t *r) {
    ACTOR_VIRTUALS_GRAPH(mech_engage_roll);return es2_mech_engage_roll_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl actor_virtuals_mech_link_position(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_mech_link_position_registers(&math_native_memory,a[0],r);
}
#undef ACTOR_VIRTUALS_GRAPH
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original; base_condition, mech_offsets, mech_armour and
 * mech_link_position make no game calls. */
static callee_counter base_aim_node_counters[]={{"call_49681a",ES2_COLLIDE_CALL,0x49681a,0}};
static callee_counter base_offsets_counters[]={{"call_411a74",ES2_COLLIDE_CALL,0x411a74,0}};
static callee_counter actor_attacked_counters[]={{"call_412704",ES2_COLLIDE_CALL,0x412704,0}};
static callee_counter base_part_position_counters[]={
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
};
static callee_counter mech_aim_node_counters[]={{"slot_20",ES2_COLLIDE_SLOT,0x20,0}};
static callee_counter mech_target_scale_counters[]={{"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0}};
static callee_counter mech_engage_roll_counters[]={{"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0}};
static callee_counter *actor_virtuals_hook_counters(unsigned hook,unsigned *count) {
#define ACTOR_VIRTUALS_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    ACTOR_VIRTUALS_COUNTERS(base_aim_node)
    ACTOR_VIRTUALS_COUNTERS(base_offsets)
    ACTOR_VIRTUALS_COUNTERS(actor_attacked)
    ACTOR_VIRTUALS_COUNTERS(base_part_position)
    ACTOR_VIRTUALS_COUNTERS(mech_aim_node)
    ACTOR_VIRTUALS_COUNTERS(mech_target_scale)
    ACTOR_VIRTUALS_COUNTERS(mech_engage_roll)
#undef ACTOR_VIRTUALS_COUNTERS
    *count=0;return NULL;
}
/* Verified entry bytes (reference DBSIM.EXE). */
/* push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8]; mov esi,4a95cch */
static const BYTE actor_virtuals_base_aim_node_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08,0xbe,0xcc,0x95,0x4a,0x00};
/* push ebp; mov ebp,esp; push ebx; push esi; mov esi,[ebp+10h]; mov ebx,[ebp+8] */
static const BYTE actor_virtuals_base_offsets_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x10,0x8b,0x5d,0x08};
/* push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi; mov ebx,[ebp+8]; xor ecx,ecx */
static const BYTE actor_virtuals_base_condition_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x5d,0x08,0x33,0xc9};
/* push ebp; mov ebp,esp; mov eax,[ebp+8]; mov edx,[ebp+0Ch]; push edx; push eax; call */
static const BYTE actor_virtuals_actor_attacked_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x55,0x0c,0x52,0x50,0xe8};
/* push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi; mov esi,[ebp+8]; mov edi,[ebp+10h] */
static const BYTE actor_virtuals_base_part_position_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x75,0x08,0x8b,0x7d,0x10};
/* push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi; mov esi,[ebp+8]; mov eax,[esi+1F2h] */
static const BYTE actor_virtuals_mech_aim_node_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x75,0x08,0x8b,0x86,0xf2,0x01,0x00,0x00};
/* push ebp; mov ebp,esp; push ebx; mov eax,[ebp+8]; mov ecx,[ebp+10h] */
static const BYTE actor_virtuals_mech_offsets_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x08,0x8b,0x4d,0x10};
/* push ebp; mov ebp,esp; mov edx,[ebp+8]; lea eax,[edx+222h] */
static const BYTE actor_virtuals_mech_armour_sig[]={0x55,0x8b,0xec,0x8b,0x55,0x08,0x8d,0x82,0x22,0x02,0x00,0x00};
/* push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8]; mov eax,[ebx+1F2h] */
static const BYTE actor_virtuals_mech_target_scale_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08,0x8b,0x83,0xf2,0x01,0x00,0x00};
/* push ebp; mov ebp,esp; push ebx; push esi; mov eax,[ebp+0Ch]; mov ebx,[ebp+8] */
static const BYTE actor_virtuals_mech_engage_roll_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x45,0x0c,0x8b,0x5d,0x08};
/* push ebp; mov ebp,esp; push ebx; mov eax,[ebp+8]; mov edx,[eax+1A4h] */
static const BYTE actor_virtuals_mech_link_position_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x08,0x8b,0x90,0xa4,0x01,0x00,0x00};
static const BYTE actor_virtuals_saved_lens[]={1,2,1,1},actor_virtuals_load_lens[]={1,2,3},actor_virtuals_ebx_load_lens[]={1,2,1,3};
#define ACTOR_VIRTUALS_EXT_FLAG(name,unsupported) \
    static const es2_hook_extension actor_virtuals_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,actor_virtuals_##name,unsupported}; \
    static uint32_t __cdecl actor_virtuals_##name##32(const uint32_t *a,void *u) {return (uint32_t)actor_virtuals_##name(a,u,weapons_no_registers);}
#define ACTOR_VIRTUALS_EXT(name) ACTOR_VIRTUALS_EXT_FLAG(name,TRUE)
ACTOR_VIRTUALS_EXT(base_aim_node)
ACTOR_VIRTUALS_EXT(base_offsets)
ACTOR_VIRTUALS_EXT_FLAG(base_condition,FALSE)
ACTOR_VIRTUALS_EXT(actor_attacked)
ACTOR_VIRTUALS_EXT(base_part_position)
ACTOR_VIRTUALS_EXT(mech_aim_node)
ACTOR_VIRTUALS_EXT(mech_offsets)
ACTOR_VIRTUALS_EXT_FLAG(mech_armour,FALSE)
ACTOR_VIRTUALS_EXT_FLAG(mech_target_scale,FALSE)
ACTOR_VIRTUALS_EXT(mech_engage_roll)
ACTOR_VIRTUALS_EXT(mech_link_position)
#undef ACTOR_VIRTUALS_EXT
#undef ACTOR_VIRTUALS_EXT_FLAG
#define ACTOR_VIRTUALS_DESCRIPTOR(name,relocate,lens,count,args) \
    {ES2_HOOK_ID(name),actor_virtuals_##name##_sig,sizeof(actor_virtuals_##name##_sig),relocate,lens,count,args, \
     actor_virtuals_##name##32,NULL,NULL,NULL,0}
static es2_hook_descriptor actor_virtuals_hook_descriptors[]={
    ACTOR_VIRTUALS_DESCRIPTOR(base_aim_node,5,actor_virtuals_saved_lens,4,1),
    ACTOR_VIRTUALS_DESCRIPTOR(base_offsets,5,actor_virtuals_saved_lens,4,3),
    ACTOR_VIRTUALS_DESCRIPTOR(base_condition,5,actor_virtuals_saved_lens,4,1),
    ACTOR_VIRTUALS_DESCRIPTOR(actor_attacked,6,actor_virtuals_load_lens,3,2),
    ACTOR_VIRTUALS_DESCRIPTOR(base_part_position,5,actor_virtuals_saved_lens,4,3),
    ACTOR_VIRTUALS_DESCRIPTOR(mech_aim_node,5,actor_virtuals_saved_lens,4,1),
    ACTOR_VIRTUALS_DESCRIPTOR(mech_offsets,7,actor_virtuals_ebx_load_lens,4,3),
    ACTOR_VIRTUALS_DESCRIPTOR(mech_armour,6,actor_virtuals_load_lens,3,2),
    ACTOR_VIRTUALS_DESCRIPTOR(mech_target_scale,5,actor_virtuals_saved_lens,4,1),
    ACTOR_VIRTUALS_DESCRIPTOR(mech_engage_roll,5,actor_virtuals_saved_lens,4,2),
    ACTOR_VIRTUALS_DESCRIPTOR(mech_link_position,7,actor_virtuals_ebx_load_lens,4,1),
};
#undef ACTOR_VIRTUALS_DESCRIPTOR
#endif
