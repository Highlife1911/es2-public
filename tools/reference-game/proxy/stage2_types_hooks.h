#ifndef ES2_STAGE2_TYPES_HOOKS_H
#define ES2_STAGE2_TYPES_HOOKS_H
/* stage2_types group (docs/reverse-engineering/mission_load.md,
 * section "stage-2 callbacks with type data"): the stage-2
 * callbacks of stage_dispatch(2) that load type data, weapon_types 40fc8c,
 * rocket_types 40a818, bullet_types 40ade0, beam_types 40b6e0, mech_forms
 * 41fdb0 and flyer_forms 422d90, and the weapon-list entry weapon_entry
 * 40f8bc (core/src/stage2_types.c). The callbacks are registered by
 * 401d64(fn, 2) and called once per process through `call [ebx]` at 401dac
 * from 401d94(2) at 4617b8 inside the mission load (as
 * mission_pools); weapon_entry is called only by weapon_types (40fdc8). All
 * are live only with the early arming (the default).
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX: the runtime record 49630c
 * gets the synthetic EBP (ES2_STAGE2_TYPES_FRAME in a zeroed scratch, the
 * stream object at f-30h and beam_types' count word at f-52h at the
 * original's offsets) through weapons_impact_guest_call, and the adapter
 * restores FS:0 to the record's saved head as the original's epilogue does
 * (from EAX for weapon_types, rocket_types and bullet_types, from EDX for
 * beam_types, mech_forms and flyer_forms). The callees stay game code with
 * their own modes: the runtime (49630c, new[] 4774f0, 49667c, _memset
 * 49681a), the path builder 492bec, the stream (472f14, the hooks
 * stream_read 4735b8, stream_close 47307c, stream_open 4730e8 and
 * stream_destroy 472f64, the read method at slot +18h), the error report
 * 479c78, the resources (474cd8, 45cdf4, 46a03c, 48349c), weapon_entry
 * (under weapon_types) and the hooks dmg_region_read 40cff8 and
 * collision_volume 40cc14 (under weapon_entry).
 *
 * EDX:EAX exact; ECX is dead after the dispatcher's indirect call (401dac
 * reads no register; the next callback of each reads none before writing
 * it) and after 40fdc8 (40fdcd..40fde0 write it), so it is not returned
 * (the framework returns the entry ECX). No entry or exit shim: no read of
 * an uninitialized local or of stack residue other than beam_types' count
 * word, which the read call it is passed to fills. All seven refuse compare
 * mode (file I/O, allocation, resource loading, the runtime record); the
 * load is accepted by the after-load snapshot. No game instruction is
 * embedded. */
#include "es2/core/stage2_types.h"
#define STAGE2_TYPES_ADAPTER(name,shift) \
static uint64_t __cdecl stage2_types_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    (void)a;BYTE scratch[ES2_STAGE2_TYPES_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##name); \
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_STAGE2_TYPES_FRAME)}; \
    g.call=weapons_impact_guest_call;g.context=&c; \
    uint64_t result=es2_##name##_registers(&math_native_memory,&g,r,NULL); \
    uint32_t head=(uint32_t)(result>>(shift));__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result; }
STAGE2_TYPES_ADAPTER(weapon_types,0)
STAGE2_TYPES_ADAPTER(rocket_types,0)
STAGE2_TYPES_ADAPTER(bullet_types,0)
STAGE2_TYPES_ADAPTER(beam_types,32)
STAGE2_TYPES_ADAPTER(mech_forms,32)
STAGE2_TYPES_ADAPTER(flyer_forms,32)
#undef STAGE2_TYPES_ADAPTER
static uint64_t __cdecl stage2_types_weapon_entry(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_weapon_entry);
    return es2_weapon_entry_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the virtual slot. */
static callee_counter weapon_types_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_492bec",ES2_COLLIDE_CALL,0x492bec,0},
    {"call_45cdf4",ES2_COLLIDE_CALL,0x45cdf4,0},
    {"call_46a03c",ES2_COLLIDE_CALL,0x46a03c,0},
    {"call_48349c",ES2_COLLIDE_CALL,0x48349c,0},
    {"call_474cd8",ES2_COLLIDE_CALL,0x474cd8,0},
    {"call_472f14",ES2_COLLIDE_CALL,0x472f14,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"call_4735b8",ES2_COLLIDE_CALL,0x4735b8,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_40f8bc",ES2_COLLIDE_CALL,0x40f8bc,0},
    {"call_47307c",ES2_COLLIDE_CALL,0x47307c,0},
    {"call_4730e8",ES2_COLLIDE_CALL,0x4730e8,0},
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_472f64",ES2_COLLIDE_CALL,0x472f64,0},
};
static callee_counter weapon_entry_counters[]={
    {"call_40cff8",ES2_COLLIDE_CALL,0x40cff8,0},
    {"call_40cc14",ES2_COLLIDE_CALL,0x40cc14,0},
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
};
static callee_counter rocket_types_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_492bec",ES2_COLLIDE_CALL,0x492bec,0},
    {"call_472f14",ES2_COLLIDE_CALL,0x472f14,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"call_4735b8",ES2_COLLIDE_CALL,0x4735b8,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_472f64",ES2_COLLIDE_CALL,0x472f64,0},
    {"call_474cd8",ES2_COLLIDE_CALL,0x474cd8,0},
};
static callee_counter bullet_types_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_492bec",ES2_COLLIDE_CALL,0x492bec,0},
    {"call_472f14",ES2_COLLIDE_CALL,0x472f14,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"call_4735b8",ES2_COLLIDE_CALL,0x4735b8,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_472f64",ES2_COLLIDE_CALL,0x472f64,0},
    {"call_474cd8",ES2_COLLIDE_CALL,0x474cd8,0},
    {"call_45cdf4",ES2_COLLIDE_CALL,0x45cdf4,0},
    {"call_46a03c",ES2_COLLIDE_CALL,0x46a03c,0},
};
static callee_counter beam_types_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_492bec",ES2_COLLIDE_CALL,0x492bec,0},
    {"call_472f14",ES2_COLLIDE_CALL,0x472f14,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_4735b8",ES2_COLLIDE_CALL,0x4735b8,0},
    {"call_472f64",ES2_COLLIDE_CALL,0x472f64,0},
    {"call_45cdf4",ES2_COLLIDE_CALL,0x45cdf4,0},
    {"call_46a03c",ES2_COLLIDE_CALL,0x46a03c,0},
    {"call_48349c",ES2_COLLIDE_CALL,0x48349c,0},
};
static callee_counter mech_forms_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"call_492bec",ES2_COLLIDE_CALL,0x492bec,0},
    {"call_472f14",ES2_COLLIDE_CALL,0x472f14,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"call_4735b8",ES2_COLLIDE_CALL,0x4735b8,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_45cdf4",ES2_COLLIDE_CALL,0x45cdf4,0},
    {"call_46a03c",ES2_COLLIDE_CALL,0x46a03c,0},
    {"call_48349c",ES2_COLLIDE_CALL,0x48349c,0},
    {"call_472f64",ES2_COLLIDE_CALL,0x472f64,0},
};
static callee_counter flyer_forms_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"call_492bec",ES2_COLLIDE_CALL,0x492bec,0},
    {"call_472f14",ES2_COLLIDE_CALL,0x472f14,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"call_4735b8",ES2_COLLIDE_CALL,0x4735b8,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_472f64",ES2_COLLIDE_CALL,0x472f64,0},
};
#define STAGE2_TYPES_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *stage2_types_hook_counters(unsigned hook,unsigned *count) {
    STAGE2_TYPES_COUNTERS(weapon_types)
    STAGE2_TYPES_COUNTERS(weapon_entry)
    STAGE2_TYPES_COUNTERS(rocket_types)
    STAGE2_TYPES_COUNTERS(bullet_types)
    STAGE2_TYPES_COUNTERS(beam_types)
    STAGE2_TYPES_COUNTERS(mech_forms)
    STAGE2_TYPES_COUNTERS(flyer_forms)
    *count=0;return NULL;
}
#undef STAGE2_TYPES_COUNTERS
/* 40fc8c: push ebp; mov ebp,esp; add esp,-50h; push ebx; push esi; push
 * edi; mov esi,4a9ae8h. 40a818, 40ade0, 41fdb0, 422d90: push ebp; mov
 * ebp,esp; add esp,-50h; mov eax,<descriptor>; push ebx. 40b6e0: the same
 * with add esp,-54h. Relocated 6 bytes each (1,2,3); what follows stays in
 * place. 40f8bc: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov
 * esi,[ebp+0Ch]; relocated 5 bytes (1,2,1,1). */
static const BYTE weapon_types_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb0,0x53,0x56,0x57,0xbe,0xe8,0x9a,0x4a,0x00};
static const BYTE rocket_types_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb0,0xb8,0x4c,0x83,0x49,0x00,0x53};
static const BYTE bullet_types_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb0,0xb8,0x25,0x85,0x49,0x00,0x53};
static const BYTE beam_types_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xac,0xb8,0x58,0x86,0x49,0x00,0x53};
static const BYTE mech_forms_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb0,0xb8,0x8c,0xa3,0x49,0x00,0x53};
static const BYTE flyer_forms_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb0,0xb8,0x78,0xa6,0x49,0x00,0x53};
static const BYTE weapon_entry_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x75,0x0c};
static const BYTE stage2_types_frame_lens[]={1,2,3},stage2_types_push_lens[]={1,2,1,1};
#define STAGE2_TYPES_EXT(name) \
    static const es2_hook_extension stage2_types_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,stage2_types_##name,TRUE}; \
    static uint32_t __cdecl stage2_types_##name##32(const uint32_t *a,void *u) {return (uint32_t)stage2_types_##name(a,u,weapons_no_registers);}
STAGE2_TYPES_EXT(weapon_types)
STAGE2_TYPES_EXT(weapon_entry)
STAGE2_TYPES_EXT(rocket_types)
STAGE2_TYPES_EXT(bullet_types)
STAGE2_TYPES_EXT(beam_types)
STAGE2_TYPES_EXT(mech_forms)
STAGE2_TYPES_EXT(flyer_forms)
#undef STAGE2_TYPES_EXT
static es2_hook_descriptor stage2_types_hook_descriptors[]={
    {ES2_HOOK_ID(weapon_types),weapon_types_sig,14,6,stage2_types_frame_lens,3,0,stage2_types_weapon_types32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(weapon_entry),weapon_entry_sig,9,5,stage2_types_push_lens,4,2,stage2_types_weapon_entry32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(rocket_types),rocket_types_sig,12,6,stage2_types_frame_lens,3,0,stage2_types_rocket_types32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(bullet_types),bullet_types_sig,12,6,stage2_types_frame_lens,3,0,stage2_types_bullet_types32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(beam_types),beam_types_sig,12,6,stage2_types_frame_lens,3,0,stage2_types_beam_types32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_forms),mech_forms_sig,12,6,stage2_types_frame_lens,3,0,stage2_types_mech_forms32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_forms),flyer_forms_sig,12,6,stage2_types_frame_lens,3,0,stage2_types_flyer_forms32,NULL,NULL,NULL,0},
};
#endif
