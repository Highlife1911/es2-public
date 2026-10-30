#ifndef ES2_FIT_LEAVES_HOOKS_H
#define ES2_FIT_LEAVES_HOOKS_H
/* fit_leaves group (docs/reverse-engineering/mission_load.md,
 * section "the remaining weapon_fit callees"): the remaining
 * callees of the weapon fit weapon_fit 4175dc (core/src/fit_leaves.c):
 * weapon_shape_word 40e61c, mount_entry 40fea0, mount_table 420634,
 * shield_split 413ac8, energy_rate 417d08, equip_slots 40fb2c and the
 * recursive node_state_fill 417530. mount_entry is also called by
 * gun_list_load (40ff15) and mount_table also by the display-object
 * code at 437ce7; the others only by weapon_fit (and node_state_fill by
 * itself). All run during the mission load, so they are live only with the
 * early arming (the default).
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX: mount_entry 40fea0
 * (mount_table), multiply_q10 47e0b0 (shield_split, energy_rate),
 * part_ratio 40dbc0 (energy_rate), the memset thunk 49681a (equip_slots)
 * and the shape node's virtual slot 0 (node_state_fill, called without an
 * observer key), so the nested hooks keep their modes. node_state_fill
 * recurses inside the core in the original's order; its recursive calls do
 * not pass the detour, so in replaced mode hk_node_state_fill_calls counts
 * the top-level calls only (the original mode counts every level).
 *
 * EDX:EAX exact. ECX is dead after every call site (mission_load.md, "the
 * remaining weapon_fit callees", "Registers, stack and shims"): the framework returns the entry ECX; the cores report the
 * original's ECX for the in-vitro comparison (equip_slots and node_state_fill
 * leave stack residue in its high word or all of it; those are not
 * compared). No entry or exit shim: no read of an uninitialized local or of
 * stack residue. All seven refuse compare mode (guest calls, the recursion,
 * the load-time state); the load is accepted by the after-load snapshot.
 * No game instruction is embedded. */
#include "es2/core/fit_leaves.h"
static uint64_t __cdecl fit_leaves_weapon_shape_word(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_weapon_shape_word_registers(&math_native_memory,a[0],r,NULL);
}
static uint64_t __cdecl fit_leaves_mount_entry(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_mount_entry);
    return es2_mount_entry_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl fit_leaves_mount_table(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_mount_table);
    return es2_mount_table_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl fit_leaves_shield_split(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_shield_split);
    return es2_shield_split_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl fit_leaves_energy_rate(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_energy_rate);
    return es2_energy_rate_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl fit_leaves_equip_slots(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_equip_slots);
    return es2_equip_slots_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl fit_leaves_node_state_fill(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_node_state_fill);
    return es2_node_state_fill_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA. */
static callee_counter mount_table_counters[]={
    {"call_40fea0",ES2_COLLIDE_CALL,0x40fea0,0},
};
static callee_counter shield_split_counters[]={
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
};
static callee_counter energy_rate_counters[]={
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_40dbc0",ES2_COLLIDE_CALL,0x40dbc0,0},
};
static callee_counter equip_slots_counters[]={
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
};
static callee_counter *fit_leaves_hook_counters(unsigned hook,unsigned *count) {
#define FIT_LEAVES_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
    FIT_LEAVES_COUNTERS(mount_table) FIT_LEAVES_COUNTERS(shield_split) FIT_LEAVES_COUNTERS(energy_rate)
    FIT_LEAVES_COUNTERS(equip_slots)
#undef FIT_LEAVES_COUNTERS
    *count=0;return NULL;
}
/* Prologues (relocated bytes, instruction lengths):
 * 40e61c, 420634: push ebp; mov ebp,esp; mov eax,[ebp+8], 6 bytes (1,2,3);
 * 40fea0: push ebp; mov ebp,esp; movsx eax,word [ebp+8], 7 bytes (1,2,4);
 * 413ac8: push ebp; mov ebp,esp; push ebx; push esi, 5 bytes (1,2,1,1);
 * 417d08, 417530, 40fb2c: push ebp; mov ebp,esp; add esp,-N (83 c4 ib),
 * 6 bytes (1,2,3); the pushes and loads that follow stay in place. */
static const BYTE weapon_shape_word_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x40,0x18};
static const BYTE mount_entry_sig[]={0x55,0x8b,0xec,0x0f,0xbf,0x45,0x08,0x03,0xc0};
static const BYTE mount_table_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x90,0xf2,0x01,0x00,0x00};
static const BYTE shield_split_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE energy_rate_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE equip_slots_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x8b,0x5d,0x0c};
static const BYTE node_state_fill_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x57,0x8b,0x5d,0x0c};
static const BYTE fit_leaves_lens[]={1,2,3},mount_entry_lens[]={1,2,4},shield_split_lens[]={1,2,1,1};
#define FIT_LEAVES_EXTENSION(name) \
static const es2_hook_extension fit_leaves_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0, \
    fit_leaves_##name,TRUE}; \
static uint32_t __cdecl fit_leaves_##name##32(const uint32_t *a,void *u) { \
    return (uint32_t)fit_leaves_##name(a,u,weapons_no_registers); }
FIT_LEAVES_EXTENSION(weapon_shape_word)
FIT_LEAVES_EXTENSION(mount_entry)
FIT_LEAVES_EXTENSION(mount_table)
FIT_LEAVES_EXTENSION(shield_split)
FIT_LEAVES_EXTENSION(energy_rate)
FIT_LEAVES_EXTENSION(equip_slots)
FIT_LEAVES_EXTENSION(node_state_fill)
#undef FIT_LEAVES_EXTENSION
static es2_hook_descriptor fit_leaves_hook_descriptors[]={
    {ES2_HOOK_ID(weapon_shape_word),weapon_shape_word_sig,9,6,fit_leaves_lens,3,1,fit_leaves_weapon_shape_word32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mount_entry),mount_entry_sig,9,7,mount_entry_lens,3,1,fit_leaves_mount_entry32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mount_table),mount_table_sig,12,6,fit_leaves_lens,3,1,fit_leaves_mount_table32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(shield_split),shield_split_sig,9,5,shield_split_lens,4,1,fit_leaves_shield_split32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(energy_rate),energy_rate_sig,12,6,fit_leaves_lens,3,1,fit_leaves_energy_rate32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(equip_slots),equip_slots_sig,10,6,fit_leaves_lens,3,2,fit_leaves_equip_slots32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(node_state_fill),node_state_fill_sig,12,6,fit_leaves_lens,3,2,fit_leaves_node_state_fill32,NULL,NULL,NULL,0},
};
#endif
