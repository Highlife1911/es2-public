#ifndef ES2_TYPE_LOADS_HOOKS_H
#define ES2_TYPE_LOADS_HOOKS_H
/* type_loads group (docs/reverse-engineering/mission_load.md,
 * section "MECH and FLYER type loads"): the type loads of the world
 * load's pass 1 (0x424308), mech_types 42049c (the MECH type table, from
 * 4252dd), mech_type_load 4201a8 (one MECH type), lod_remap 420090 (the
 * part remap rows of the LOD shapes), node_remap 41ff88 (the recursive
 * shape-node walk), remap_word 41ff68, mech_type_select 41ff10 (from
 * 4250b9), gun_list_load 40fee8, gun_classes 40fc50, node_list 40304c, the
 * running slot bases gun_slot_base 40feb4, col_slot_base 40cbd4 and
 * dmg_slot_base 40d200, flyer_types 422fb4 (from 4252e2), flyer_type_load
 * 422ed0 and flyer_type_select 422e90 (from 4250f7). All load-time code:
 * live only with the early arming (the default).
 * Core in core/src/type_loads.c.
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX: the runtime record 49630c
 * (mech_types, mech_type_load and gun_list_load run with the synthetic EBP
 * of a zeroed scratch through weapons_impact_guest_call; the adapter writes
 * the record's saved head, EAX or EDX, back to FS:0 as the epilogue does),
 * new[] 4774f0, 49667c with its element constructors, memset 49681a, the
 * file layer (492d00, 492bec, 492f54, 472f14, stream_read, stream_destroy,
 * 479c78), the shape loads 474cd8/474c38, the H3 displays 4378d8/4379cc,
 * 40fea0, the debris set 40874c, multiply_q10, dmg_load, collision_load,
 * the shape virtuals +20h/+24h, the node virtual +0 and the fifteen among
 * themselves, so nested hooks keep their modes. lod_remap's rows live in a
 * zeroed scratch frame. The type-index leaves 41ff00/422e80 and the Q16
 * helpers 47e068/47e08d are computed in core.
 *
 * EDX:EAX exact; ECX is dead after every call site (mission_load.md), so it
 * is not returned (the framework returns the entry ECX). No entry or exit
 * shim. gun_list_load's original pushes the type word with its caller's EBX
 * high word for 40fea0 (MOV BX); the adapter reconstructs EBX as the only
 * caller 4201a8 has it (the type record's shape list +0DEh); 40fea0 reads
 * the word only. The three slot bases accept compare (one image word each,
 * a region) and so does remap_word (span: the word of argument 0, EAX
 * compared relative to it; the table byte is only read); the other eleven
 * refuse it (allocation, files, the runtime record, game calls that write
 * the type tables and shapes). No game instruction is embedded. */
#include "es2/core/type_loads.h"
static uint64_t type_loads_head(uint64_t result,uint32_t head) {
    es2_machine_set_fs0(head);return result;
}
static uint64_t __cdecl type_loads_mech_types(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_MECH_TYPES_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_mech_types);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_MECH_TYPES_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;(void)a;
    const uint64_t result=es2_mech_types_registers(&math_native_memory,&g,r,NULL);
    return type_loads_head(result,(uint32_t)result);
}
static uint64_t __cdecl type_loads_mech_type_load(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_MECH_TYPE_LOAD_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_mech_type_load);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_MECH_TYPE_LOAD_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    const uint64_t result=es2_mech_type_load_registers(&math_native_memory,&g,a[0],r,NULL);
    return type_loads_head(result,(uint32_t)result);
}
/* 4201a8 at 420391: EBX = the shape list rec+0DEh of the type record whose
 * index is the pushed word minus the base word 4A9D70h. */
static uint32_t type_loads_caller_ebx(void *u,uint32_t type) {
    const uint32_t b=(uint32_t)(uintptr_t)u;
    const uint16_t index=(uint16_t)(type-math_native_read16(NULL,b+0xa9d70u));
    return math_native_read32(NULL,b+0xa9d64u)+0x21eu*(uint32_t)(int32_t)(int16_t)index+0xdeu;
}
static uint64_t __cdecl type_loads_gun_list_load(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_GUN_LIST_LOAD_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_gun_list_load);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_GUN_LIST_LOAD_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    const uint64_t result=es2_gun_list_load_registers(&math_native_memory,&g,a[0],a[1],type_loads_caller_ebx(u,a[1]),r,NULL);
    return type_loads_head(result,(uint32_t)(result>>32));
}
static uint64_t __cdecl type_loads_lod_remap(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_LOD_REMAP_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_lod_remap);
    return es2_lod_remap_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl type_loads_node_remap(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_node_remap);
    return es2_node_remap_registers(&math_native_memory,&g,a[0],a[1],a[2],r,NULL);
}
static uint64_t __cdecl type_loads_node_list(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_node_list);
    return es2_node_list_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,NULL);
}
#define TYPE_LOADS_GRAPH0(name) \
static uint64_t __cdecl type_loads_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name);(void)a; \
    return es2_##name##_registers(&math_native_memory,&g,r,NULL); }
#define TYPE_LOADS_GRAPH1(name) \
static uint64_t __cdecl type_loads_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name); \
    return es2_##name##_registers(&math_native_memory,&g,a[0],r,NULL); }
TYPE_LOADS_GRAPH0(flyer_types)
TYPE_LOADS_GRAPH1(flyer_type_load)
TYPE_LOADS_GRAPH1(mech_type_select)
TYPE_LOADS_GRAPH1(flyer_type_select)
#undef TYPE_LOADS_GRAPH0
#undef TYPE_LOADS_GRAPH1
static uint64_t __cdecl type_loads_remap_word(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_remap_word_registers(&math_native_memory,a[0],a[1],r,NULL);
}
static uint64_t __cdecl type_loads_gun_classes(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_gun_classes_registers(&math_native_memory,a[0],a[1],r,NULL);
}
#define TYPE_LOADS_SLOT(name) \
static uint64_t __cdecl type_loads_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    return es2_##name##_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],r,NULL); }
TYPE_LOADS_SLOT(gun_slot_base)
TYPE_LOADS_SLOT(col_slot_base)
TYPE_LOADS_SLOT(dmg_slot_base)
#undef TYPE_LOADS_SLOT
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the virtual slot (the node class
 * slot +0 is keyed 100h, since key 0 means a direct call). */
static callee_counter mech_types_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_492d00",ES2_COLLIDE_CALL,0x492d00,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_4378d8",ES2_COLLIDE_CALL,0x4378d8,0},
    {"call_4201a8",ES2_COLLIDE_CALL,0x4201a8,0},
};
static callee_counter mech_type_load_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_492bec",ES2_COLLIDE_CALL,0x492bec,0},
    {"call_492f54",ES2_COLLIDE_CALL,0x492f54,0},
    {"call_474cd8",ES2_COLLIDE_CALL,0x474cd8,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_420090",ES2_COLLIDE_CALL,0x420090,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_40fee8",ES2_COLLIDE_CALL,0x40fee8,0},
    {"call_40fc50",ES2_COLLIDE_CALL,0x40fc50,0},
    {"call_40304c",ES2_COLLIDE_CALL,0x40304c,0},
    {"call_4379cc",ES2_COLLIDE_CALL,0x4379cc,0},
    {"call_40874c",ES2_COLLIDE_CALL,0x40874c,0},
};
static callee_counter lod_remap_counters[]={
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
    {"call_41ff88",ES2_COLLIDE_CALL,0x41ff88,0},
};
static callee_counter node_remap_counters[]={
    {"slot_0",ES2_COLLIDE_SLOT,0x100,0},
    {"call_41ff68",ES2_COLLIDE_CALL,0x41ff68,0},
    {"call_41ff88",ES2_COLLIDE_CALL,0x41ff88,0},
};
static callee_counter mech_type_select_counters[]={
    {"call_40feb4",ES2_COLLIDE_CALL,0x40feb4,0},
    {"call_40cbd4",ES2_COLLIDE_CALL,0x40cbd4,0},
    {"call_40d200",ES2_COLLIDE_CALL,0x40d200,0},
};
static callee_counter gun_list_load_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_472f14",ES2_COLLIDE_CALL,0x472f14,0},
    {"call_40fea0",ES2_COLLIDE_CALL,0x40fea0,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"call_4735b8",ES2_COLLIDE_CALL,0x4735b8,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_472f64",ES2_COLLIDE_CALL,0x472f64,0},
};
static callee_counter node_list_counters[]={
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"slot_24",ES2_COLLIDE_SLOT,0x24,0},
};
static callee_counter flyer_types_counters[]={
    {"call_492d00",ES2_COLLIDE_CALL,0x492d00,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_422ed0",ES2_COLLIDE_CALL,0x422ed0,0},
};
static callee_counter flyer_type_load_counters[]={
    {"call_492bec",ES2_COLLIDE_CALL,0x492bec,0},
    {"call_492f54",ES2_COLLIDE_CALL,0x492f54,0},
    {"call_474c38",ES2_COLLIDE_CALL,0x474c38,0},
    {"call_40d160",ES2_COLLIDE_CALL,0x40d160,0},
    {"call_40cd88",ES2_COLLIDE_CALL,0x40cd88,0},
};
static callee_counter flyer_type_select_counters[]={
    {"call_40d200",ES2_COLLIDE_CALL,0x40d200,0},
    {"call_40cbd4",ES2_COLLIDE_CALL,0x40cbd4,0},
};
#define TYPE_LOADS_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *type_loads_hook_counters(unsigned hook,unsigned *count) {
    TYPE_LOADS_COUNTERS(mech_types)
    TYPE_LOADS_COUNTERS(mech_type_load)
    TYPE_LOADS_COUNTERS(lod_remap)
    TYPE_LOADS_COUNTERS(node_remap)
    TYPE_LOADS_COUNTERS(mech_type_select)
    TYPE_LOADS_COUNTERS(gun_list_load)
    TYPE_LOADS_COUNTERS(node_list)
    TYPE_LOADS_COUNTERS(flyer_types)
    TYPE_LOADS_COUNTERS(flyer_type_load)
    TYPE_LOADS_COUNTERS(flyer_type_select)
    *count=0;return NULL;
}
#undef TYPE_LOADS_COUNTERS
/* Verified entry bytes (reference DBSIM.EXE).
 * 42049c: push ebp; mov ebp,esp; add esp,-24h; mov eax,49A3E0h; push ebx.
 * 4201a8: push ebp; mov ebp,esp; add esp,-60h; mov eax,49A3B0h; push ebx.
 * 40fee8: push ebp; mov ebp,esp; add esp,-50h; mov eax,498FB1h; push ebx.
 * 41ff88: push ebp; mov ebp,esp; add esp,-0Ch; push ebx; push esi; push edi.
 * 40304c: push ebp; mov ebp,esp; add esp,-8; push ebx; push esi; push edi.
 * 41ff68: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov dx,[eax]; cmp dx,-1.
 * Relocated 6 bytes (1,2,3); in the framed ones the descriptor load and the
 * call of 49630c that follow stay in place.
 * 420090: push ebp; mov ebp,esp; add esp,-1A0h; push ebx; push esi; push
 * edi. Relocated 9 bytes (1,2,6).
 * 41ff10, 422e90: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; push
 * ebx. Relocated 7 bytes (1,2,1,3).
 * 40fc50: push ebp; mov ebp,esp; push ebx; push esi; mov ecx,[ebp+0Ch].
 * 422ed0: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov
 * ecx,[4A9DF0h]. Relocated 5 bytes (1,2,1,1).
 * 40feb4, 40cbd4, 40d200: push ebp; mov ebp,esp; mov ax,[word]; mov
 * dx,[ebp+8]. Relocated 9 bytes (1,2,6: 66 A1 moffs32).
 * 422fb4: push ebx; push esi; cmp word [4A9DCAh],0; je. Relocated 10 bytes
 * (1,1,8: 66 83 /7 with the absolute address). */
static const BYTE mech_types_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0xb8,0xe0,0xa3,0x49,0x00,0x53};
static const BYTE mech_type_load_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xa0,0xb8,0xb0,0xa3,0x49,0x00,0x53};
static const BYTE gun_list_load_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb0,0xb8,0xb1,0x8f,0x49,0x00,0x53};
static const BYTE lod_remap_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x60,0xfe,0xff,0xff,0x53,0x56,0x57};
static const BYTE node_remap_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57,0x8b,0x75,0x0c};
static const BYTE remap_word_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x8b,0x10};
static const BYTE mech_type_select_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x53};
static const BYTE gun_classes_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x4d,0x0c};
static const BYTE node_list_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x57,0x66,0x8b,0x45,0x10};
static const BYTE gun_slot_base_sig[]={0x55,0x8b,0xec,0x66,0xa1,0xe8,0x98,0x4a,0x00,0x66,0x8b,0x55,0x08};
static const BYTE col_slot_base_sig[]={0x55,0x8b,0xec,0x66,0xa1,0xdc,0x87,0x49,0x00,0x66,0x8b,0x55,0x08};
static const BYTE dmg_slot_base_sig[]={0x55,0x8b,0xec,0x66,0xa1,0x56,0x88,0x49,0x00,0x66,0x8b,0x55,0x08};
static const BYTE flyer_types_sig[]={0x53,0x56,0x66,0x83,0x3d,0xca,0x9d,0x4a,0x00,0x00,0x74,0x55};
static const BYTE flyer_type_load_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x0d,0xf0,0x9d,0x4a,0x00};
static const BYTE flyer_type_select_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x53};
static const BYTE type_loads_frame_lens[]={1,2,3},type_loads_large_lens[]={1,2,6},type_loads_ebx_lens[]={1,2,1,3},
    type_loads_saved_lens[]={1,2,1,1},type_loads_cmp_lens[]={1,1,8};
/* Compare: remap_word's EAX is P, the declared word itself. */
static BOOL __cdecl type_loads_remap_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    return v->original_edx==v->replacement_edx&&!memcmp(v->original[0],v->replacement[0],2)&&
        v->original_result-(uint32_t)(uintptr_t)v->original[0]==v->replacement_result-(uint32_t)(uintptr_t)v->replacement[0];
}
static const es2_hook_span type_loads_remap_span[]={{0,0,2}};
static const es2_hook_region type_loads_gun_slot_region[]={{ES2_REGION_RVA,0,0xa98e8,0,0,2,FALSE,0,{0,0}}};
static const es2_hook_region type_loads_col_slot_region[]={{ES2_REGION_RVA,0,0x987dc,0,0,2,FALSE,0,{0,0}}};
static const es2_hook_region type_loads_dmg_slot_region[]={{ES2_REGION_RVA,0,0x98856,0,0,2,FALSE,0,{0,0}}};
#define TYPE_LOADS_EXT(name,regions,count,unsupported) \
    static const es2_hook_extension type_loads_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,regions,count,type_loads_##name,unsupported}; \
    static uint32_t __cdecl type_loads_##name##32(const uint32_t *a,void *u) {return (uint32_t)type_loads_##name(a,u,weapons_no_registers);}
TYPE_LOADS_EXT(mech_types,NULL,0,TRUE)
TYPE_LOADS_EXT(mech_type_load,NULL,0,TRUE)
TYPE_LOADS_EXT(lod_remap,NULL,0,TRUE)
TYPE_LOADS_EXT(node_remap,NULL,0,TRUE)
TYPE_LOADS_EXT(remap_word,NULL,0,FALSE)
TYPE_LOADS_EXT(mech_type_select,NULL,0,TRUE)
TYPE_LOADS_EXT(gun_list_load,NULL,0,TRUE)
TYPE_LOADS_EXT(gun_classes,NULL,0,TRUE)
TYPE_LOADS_EXT(node_list,NULL,0,TRUE)
TYPE_LOADS_EXT(gun_slot_base,type_loads_gun_slot_region,1,FALSE)
TYPE_LOADS_EXT(col_slot_base,type_loads_col_slot_region,1,FALSE)
TYPE_LOADS_EXT(dmg_slot_base,type_loads_dmg_slot_region,1,FALSE)
TYPE_LOADS_EXT(flyer_types,NULL,0,TRUE)
TYPE_LOADS_EXT(flyer_type_load,NULL,0,TRUE)
TYPE_LOADS_EXT(flyer_type_select,NULL,0,TRUE)
#undef TYPE_LOADS_EXT
static es2_hook_descriptor type_loads_hook_descriptors[]={
    {ES2_HOOK_ID(mech_types),mech_types_sig,12,6,type_loads_frame_lens,3,0,type_loads_mech_types32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_type_load),mech_type_load_sig,12,6,type_loads_frame_lens,3,1,type_loads_mech_type_load32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(lod_remap),lod_remap_sig,12,9,type_loads_large_lens,3,2,type_loads_lod_remap32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(node_remap),node_remap_sig,12,6,type_loads_frame_lens,3,3,type_loads_node_remap32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(remap_word),remap_word_sig,9,6,type_loads_frame_lens,3,2,type_loads_remap_word32,
        type_loads_remap_equal,NULL,type_loads_remap_span,1},
    {ES2_HOOK_ID(mech_type_select),mech_type_select_sig,8,7,type_loads_ebx_lens,4,1,type_loads_mech_type_select32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(gun_list_load),gun_list_load_sig,12,6,type_loads_frame_lens,3,2,type_loads_gun_list_load32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(gun_classes),gun_classes_sig,8,5,type_loads_saved_lens,4,2,type_loads_gun_classes32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(node_list),node_list_sig,13,6,type_loads_frame_lens,3,4,type_loads_node_list32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(gun_slot_base),gun_slot_base_sig,13,9,type_loads_large_lens,3,1,type_loads_gun_slot_base32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(col_slot_base),col_slot_base_sig,13,9,type_loads_large_lens,3,1,type_loads_col_slot_base32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dmg_slot_base),dmg_slot_base_sig,13,9,type_loads_large_lens,3,1,type_loads_dmg_slot_base32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_types),flyer_types_sig,12,10,type_loads_cmp_lens,3,0,type_loads_flyer_types32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_type_load),flyer_type_load_sig,12,5,type_loads_saved_lens,4,1,type_loads_flyer_type_load32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_type_select),flyer_type_select_sig,8,7,type_loads_ebx_lens,4,1,type_loads_flyer_type_select32,NULL,NULL,NULL,0},
};
#endif
