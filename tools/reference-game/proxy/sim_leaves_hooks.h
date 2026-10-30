#ifndef ES2_SIM_LEAVES_HOOKS_H
#define ES2_SIM_LEAVES_HOOKS_H
/* sim_leaves group (docs/reverse-engineering/vtables.md, section
 * "simulation leaves"): the last simulation leaves, 17 hooks, one per
 * function body. Core in core/src/sim_leaves.c.
 *
 *   part_shapes_bind 4030d0  parts_intact 403668  formation_slot 405b9c
 *   explosion_entry 407b20   flat_draw 40991c     projectile_draw 40a120
 *   beam_done 40c2a0         weapon_by_mount 410670  ptr_list_add 411dd4
 *   box_square_init 413a90   twist_sound_latch 41a6d0
 *   pitch_sound_latch 41a994 mech_type_index 41ff00  flyer_type_index 422e80
 *   cockpit_notice 420a68    radio_notice 420a98     notice_time 426aec
 *
 * flat_draw, projectile_draw and beam_done are table slots (the vtables of
 * the flat object 49a262, the projectile classes and the beam class), so
 * the receiver still decides which body runs; the rest are direct calls.
 * EDX:EAX exact. ECX is the framework's (the entry ECX): every original
 * that changes it has callers that write ECX before reading it
 * (vtables.md, "registers (ECX liveness)"; the three slots follow the BCC32 rule
 * that ECX never survives a call).
 *
 * Game calls go through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's registers, so nested hooks keep
 * their modes: terrain_pose 4029d8 and shape_actor_matrix 401fe4
 * (flat_draw, projectile_draw), timer_word_step 467a48 (beam_done),
 * sfx_playing 462af0 (the latches), port_a 433158 (radio_notice) and the
 * child's virtual slot +1Ch (key slot_1c) or the event port's slot 0
 * (keys: the call sites 420a8d and 420acb). The two notices build the
 * original's 24-byte frame record in 128 bytes of adapter scratch; its 18
 * trailing residue bytes are zero (presentation state, as in
 * core/src/mission.c). No entry or exit shim: no read of an uninitialised
 * local or of stack residue.
 *
 * Compare: the six pure readers parts_intact, explosion_entry,
 * weapon_by_mount, mech_type_index, flyer_type_index and notice_time accept
 * it; the other eleven write state without a declared span or make game
 * calls and refuse it. No game instruction is embedded. */
#include "es2/core/sim_leaves.h"
#define SIM_LEAVES_IMAGE(u) ((uint32_t)(uintptr_t)(u))
static uint64_t __cdecl sim_leaves_part_shapes_bind(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_part_shapes_bind_registers(&math_native_memory,a[0],a[1],a[2],r,NULL);
}
static uint64_t __cdecl sim_leaves_parts_intact(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_parts_intact_registers(&math_native_memory,a[0],r,NULL);
}
static uint64_t __cdecl sim_leaves_formation_slot(const uint32_t *a,void *u,const uint32_t *r) {
    return es2_formation_slot_registers(&math_native_memory,SIM_LEAVES_IMAGE(u),a[1],a[2],r,NULL);
}
static uint64_t __cdecl sim_leaves_explosion_entry(const uint32_t *a,void *u,const uint32_t *r) {
    return es2_explosion_entry_registers(&math_native_memory,SIM_LEAVES_IMAGE(u),a[0],r,NULL);
}
static uint64_t __cdecl sim_leaves_flat_draw(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_flat_draw);
    return es2_flat_draw_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl sim_leaves_projectile_draw(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_projectile_draw);
    return es2_projectile_draw_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl sim_leaves_beam_done(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_beam_done);
    return es2_beam_done_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl sim_leaves_weapon_by_mount(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_weapon_by_mount_registers(&math_native_memory,a[0],a[1],r,NULL);
}
static uint64_t __cdecl sim_leaves_ptr_list_add(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_ptr_list_add_registers(&math_native_memory,a[0],a[1],r,NULL);
}
static uint64_t __cdecl sim_leaves_box_square_init(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_box_square_init_registers(&math_native_memory,a[0],a[1],r,NULL);
}
static uint64_t __cdecl sim_leaves_twist_sound_latch(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_twist_sound_latch);
    return es2_twist_sound_latch_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl sim_leaves_pitch_sound_latch(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_pitch_sound_latch);
    return es2_pitch_sound_latch_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl sim_leaves_mech_type_index(const uint32_t *a,void *u,const uint32_t *r) {
    return es2_mech_type_index_registers(&math_native_memory,SIM_LEAVES_IMAGE(u),a[0],r,NULL);
}
static uint64_t __cdecl sim_leaves_flyer_type_index(const uint32_t *a,void *u,const uint32_t *r) {
    return es2_flyer_type_index_registers(&math_native_memory,SIM_LEAVES_IMAGE(u),a[0],r,NULL);
}
static uint64_t __cdecl sim_leaves_cockpit_notice(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_cockpit_notice);
    return es2_cockpit_notice_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl sim_leaves_radio_notice(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_radio_notice);
    return es2_radio_notice_registers(&math_native_memory,&g,a[0],a[1],a[2],r,NULL);
}
static uint64_t __cdecl sim_leaves_notice_time(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;return es2_notice_time_registers(&math_native_memory,SIM_LEAVES_IMAGE(u),r,NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the vtable offset (slot_1c), or the
 * call site of the event port's slot 0. */
static callee_counter flat_draw_counters[]={
    {"call_4029d8",ES2_COLLIDE_CALL,0x4029d8,0},
    {"call_401fe4",ES2_COLLIDE_CALL,0x401fe4,0},
    {"slot_1c",ES2_COLLIDE_SLOT,0x1c,0},
};
static callee_counter projectile_draw_counters[]={
    {"call_401fe4",ES2_COLLIDE_CALL,0x401fe4,0},
    {"slot_1c",ES2_COLLIDE_SLOT,0x1c,0},
};
static callee_counter beam_done_counters[]={
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
};
static callee_counter twist_sound_latch_counters[]={
    {"call_462af0",ES2_COLLIDE_CALL,0x462af0,0},
};
static callee_counter pitch_sound_latch_counters[]={
    {"call_462af0",ES2_COLLIDE_CALL,0x462af0,0},
};
static callee_counter cockpit_notice_counters[]={
    {"call_420a8d",ES2_COLLIDE_CALL,0x420a8d,0},
};
static callee_counter radio_notice_counters[]={
    {"call_433158",ES2_COLLIDE_CALL,0x433158,0},
    {"call_420acb",ES2_COLLIDE_CALL,0x420acb,0},
};
static callee_counter *sim_leaves_hook_counters(unsigned hook,unsigned *count) {
#define SIM_LEAVES_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
    SIM_LEAVES_COUNTERS(flat_draw) SIM_LEAVES_COUNTERS(projectile_draw) SIM_LEAVES_COUNTERS(beam_done)
    SIM_LEAVES_COUNTERS(twist_sound_latch) SIM_LEAVES_COUNTERS(pitch_sound_latch)
    SIM_LEAVES_COUNTERS(cockpit_notice) SIM_LEAVES_COUNTERS(radio_notice)
#undef SIM_LEAVES_COUNTERS
    *count=0;return NULL;
}
/* Verified entry bytes (reference DBSIM.EXE). The relocated prologue is
 * `push ebp; mov ebp,esp` plus the instructions listed with the lengths. */
/* 4030d0: add esp,-10h (1,2,3); then push ebx; mov ecx,[ebp+10h] */
static const BYTE part_shapes_bind_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0x53,0x8b,0x4d,0x10};
/* 403668: push ebx; push esi (1,2,1,1); then mov ecx,[ebp+8]; xor edx,edx */
static const BYTE parts_intact_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x4d,0x08,0x33,0xd2};
/* 405b9c: cmp byte [4975e2h],0 (1,2,7) */
static const BYTE formation_slot_sig[]={0x55,0x8b,0xec,0x80,0x3d,0xe2,0x75,0x49,0x00,0x00};
/* 407b20: mov eax,[ebp+8] (1,2,3); then movsx eax,byte [eax+41h] */
static const BYTE explosion_entry_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x0f,0xbe,0x40,0x41};
/* 40991c: push ebx; mov ebx,[ebp+8] (1,2,1,3); then push ebx; call 4029d8 */
static const BYTE flat_draw_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x53,0xe8,0xaf,0x90,0xff,0xff};
/* 40a120: push ebx; xor eax,eax (1,2,1,2); then mov ebx,[ebp+8]; mov [4a5b04h],eax */
static const BYTE projectile_draw_sig[]={0x55,0x8b,0xec,0x53,0x33,0xc0,0x8b,0x5d,0x08,0xa3,0x04,0x5b,0x4a,0x00};
/* 40c2a0: push ebx; mov ebx,[ebp+8] (1,2,1,3); then add ebx,5Ch */
static const BYTE beam_done_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x83,0xc3,0x5c};
/* 410670: push ebx; push esi; push edi (1,2,1,1,1); then xor esi,esi; mov ebx,[ebp+8] */
static const BYTE weapon_by_mount_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x33,0xf6,0x8b,0x5d,0x08};
/* 411dd4: push ebx; mov edx,[ebp+0Ch] (1,2,1,3); then mov eax,[ebp+8] */
static const BYTE ptr_list_add_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x55,0x0c,0x8b,0x45,0x08};
/* 413a90: mov eax,[ebp+8] (1,2,3); then mov dx,[ebp+0Ch] */
static const BYTE box_square_init_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x8b,0x55,0x0c};
/* 41a6d0, 41a994: push ebx; mov eax,[ebp+0Ch] (1,2,1,3); then mov ebx,[ebp+8] */
static const BYTE sound_latch_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x0c,0x8b,0x5d,0x08};
/* 41ff00, 422e80: movsx eax,word [ebp+8] (1,2,4); then movzx eax,byte [eax+4a9d08h / 4a9dcch] */
static const BYTE mech_type_index_sig[]={0x55,0x8b,0xec,0x0f,0xbf,0x45,0x08,0x0f,0xb6,0x80,0x08,0x9d,0x4a,0x00};
static const BYTE flyer_type_index_sig[]={0x55,0x8b,0xec,0x0f,0xbf,0x45,0x08,0x0f,0xb6,0x80,0xcc,0x9d,0x4a,0x00};
/* 420a68: add esp,-18h (1,2,3); then xor eax,eax; mov [ebp-16h],eax */
static const BYTE cockpit_notice_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x33,0xc0,0x89,0x45,0xea};
/* 420a98: add esp,-18h (1,2,3); then mov eax,[ebp+8]; lea edx,[eax+92h] */
static const BYTE radio_notice_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x8b,0x45,0x08,0x8d,0x90,0x92,0x00,0x00,0x00};
/* 426aec: mov eax,7530h (5); then cmp word [4a9e5eh],1 */
static const BYTE notice_time_sig[]={0xb8,0x30,0x75,0x00,0x00,0x66,0x83,0x3d,0x5e,0x9e,0x4a,0x00,0x01};
static const BYTE sim_leaves_frame_lens[]={1,2,3},sim_leaves_saved_lens[]={1,2,1,1},sim_leaves_cmp_lens[]={1,2,7},
    sim_leaves_ebx_lens[]={1,2,1,3},sim_leaves_xor_lens[]={1,2,1,2},sim_leaves_three_lens[]={1,2,1,1,1},
    sim_leaves_word_lens[]={1,2,4},sim_leaves_imm_lens[]={5};
#define SIM_LEAVES_EXTENSION(name,unsupported) \
static const es2_hook_extension sim_leaves_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0, \
    sim_leaves_##name,unsupported}; \
static uint32_t __cdecl sim_leaves_##name##32(const uint32_t *a,void *u) { \
    return (uint32_t)sim_leaves_##name(a,u,weapons_no_registers); }
SIM_LEAVES_EXTENSION(part_shapes_bind,TRUE)
SIM_LEAVES_EXTENSION(parts_intact,FALSE)
SIM_LEAVES_EXTENSION(formation_slot,TRUE)
SIM_LEAVES_EXTENSION(explosion_entry,FALSE)
SIM_LEAVES_EXTENSION(flat_draw,TRUE)
SIM_LEAVES_EXTENSION(projectile_draw,TRUE)
SIM_LEAVES_EXTENSION(beam_done,TRUE)
SIM_LEAVES_EXTENSION(weapon_by_mount,FALSE)
SIM_LEAVES_EXTENSION(ptr_list_add,TRUE)
SIM_LEAVES_EXTENSION(box_square_init,TRUE)
SIM_LEAVES_EXTENSION(twist_sound_latch,TRUE)
SIM_LEAVES_EXTENSION(pitch_sound_latch,TRUE)
SIM_LEAVES_EXTENSION(mech_type_index,FALSE)
SIM_LEAVES_EXTENSION(flyer_type_index,FALSE)
SIM_LEAVES_EXTENSION(cockpit_notice,TRUE)
SIM_LEAVES_EXTENSION(radio_notice,TRUE)
SIM_LEAVES_EXTENSION(notice_time,FALSE)
#undef SIM_LEAVES_EXTENSION
static es2_hook_descriptor sim_leaves_hook_descriptors[]={
    {ES2_HOOK_ID(part_shapes_bind),part_shapes_bind_sig,10,6,sim_leaves_frame_lens,3,3,sim_leaves_part_shapes_bind32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(parts_intact),parts_intact_sig,10,5,sim_leaves_saved_lens,4,1,sim_leaves_parts_intact32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(formation_slot),formation_slot_sig,10,10,sim_leaves_cmp_lens,3,3,sim_leaves_formation_slot32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(explosion_entry),explosion_entry_sig,10,6,sim_leaves_frame_lens,3,1,sim_leaves_explosion_entry32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flat_draw),flat_draw_sig,13,7,sim_leaves_ebx_lens,4,1,sim_leaves_flat_draw32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(projectile_draw),projectile_draw_sig,14,6,sim_leaves_xor_lens,4,1,sim_leaves_projectile_draw32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(beam_done),beam_done_sig,10,7,sim_leaves_ebx_lens,4,1,sim_leaves_beam_done32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(weapon_by_mount),weapon_by_mount_sig,11,6,sim_leaves_three_lens,5,2,sim_leaves_weapon_by_mount32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(ptr_list_add),ptr_list_add_sig,10,7,sim_leaves_ebx_lens,4,2,sim_leaves_ptr_list_add32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(box_square_init),box_square_init_sig,10,6,sim_leaves_frame_lens,3,2,sim_leaves_box_square_init32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(twist_sound_latch),sound_latch_sig,10,7,sim_leaves_ebx_lens,4,2,sim_leaves_twist_sound_latch32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(pitch_sound_latch),sound_latch_sig,10,7,sim_leaves_ebx_lens,4,2,sim_leaves_pitch_sound_latch32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_type_index),mech_type_index_sig,14,7,sim_leaves_word_lens,3,1,sim_leaves_mech_type_index32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_type_index),flyer_type_index_sig,14,7,sim_leaves_word_lens,3,1,sim_leaves_flyer_type_index32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(cockpit_notice),cockpit_notice_sig,11,6,sim_leaves_frame_lens,3,1,sim_leaves_cockpit_notice32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(radio_notice),radio_notice_sig,15,6,sim_leaves_frame_lens,3,3,sim_leaves_radio_notice32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(notice_time),notice_time_sig,13,5,sim_leaves_imm_lens,1,0,sim_leaves_notice_time32,NULL,NULL,NULL,0},
};
#endif
