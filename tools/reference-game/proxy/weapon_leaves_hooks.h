#ifndef ES2_WEAPON_LEAVES_HOOKS_H
#define ES2_WEAPON_LEAVES_HOOKS_H
/* weapon_leaves group (docs/reverse-engineering/weapons.md, section
 * "weapon and projectile helpers"): proj_record 40ffb0 (the PROJ
 * lookup record by index; turret, target_emit, the weapon factory),
 * pitch_point 49295c (target_emit's pitch toward a point), damage_words
 * 40501c (bullet_flight's default damage words), bullet_charge 40b5a0
 * (emit's low-charge wrapper around bullet_create) and rocket_init 40a948
 * (projectile_create's category-0 object part). The game callees
 * (dist2d_delta and vector_angle under pitch_point, bullet_create under
 * bullet_charge, object_init and def_lookup under rocket_init; all of
 * them hooks) go through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX, so they keep their
 * modes (bullet_create keeps its entry shim). No entry shim: none of the
 * five reads an uninitialized local or stack residue. The framework
 * restores the entry ECX; the original's ECX is the same for proj_record
 * and damage_words and dead at every caller for the other three
 * (weapons.md). proj_record, pitch_point and damage_words accept compare
 * mode (pure reader; pure callees; a 4-byte region over the damage words
 * 4a95f2h..4a95f5h), with the default comparison of EAX and EDX;
 * bullet_charge and rocket_init refuse it (allocation, constructor). No
 * game instruction is embedded. */
static uint64_t __cdecl weapon_leaves_proj_record(const uint32_t *a,void *u,const uint32_t *r) {
    return es2_proj_record_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],r);
}
static uint64_t __cdecl weapon_leaves_pitch_point(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_pitch_point);
    return es2_pitch_point_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl weapon_leaves_damage_words(const uint32_t *a,void *u,const uint32_t *r) {
    return es2_damage_words_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],a[1],r);
}
static uint64_t __cdecl weapon_leaves_bullet_charge(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_bullet_charge);
    return es2_bullet_charge_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r,NULL);
}
static uint64_t __cdecl weapon_leaves_rocket_init(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_rocket_init);
    return es2_rocket_init_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. proj_record and damage_words have no game calls. */
static callee_counter pitch_point_counters[]={
    {"call_492a9a",ES2_COLLIDE_CALL,0x492a9a,0},
    {"call_49290c",ES2_COLLIDE_CALL,0x49290c,0},
};
static callee_counter bullet_charge_counters[]={
    {"call_40b43c",ES2_COLLIDE_CALL,0x40b43c,0},
};
static callee_counter rocket_init_counters[]={
    {"call_402188",ES2_COLLIDE_CALL,0x402188,0},
    {"call_40ffc8",ES2_COLLIDE_CALL,0x40ffc8,0},
};
static callee_counter *weapon_leaves_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_pitch_point) { *count=2;return pitch_point_counters; }
    if(hook==ES2_HOOK_INDEX_bullet_charge) { *count=1;return bullet_charge_counters; }
    if(hook==ES2_HOOK_INDEX_rocket_init) { *count=2;return rocket_init_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; movsx eax,word [ebp+8]; shl eax,2 (40ffb0) /
 * push ebp; mov ebp,esp; push ebx; push esi; mov esi,[ebp+0Ch];
 * mov ebx,[ebp+8] (49295c) / push ebp; mov ebp,esp; mov ax,[ebp+8];
 * mov [4a95f2h],ax (40501c) / push ebp; mov ebp,esp; push ebx; push esi;
 * mov esi,[ebp+14h]; mov ebx,[ebp+8] (40b5a0) / push ebp; mov ebp,esp;
 * push ebx; push esi; push edi; mov ecx,[4a96d0h] (40a948). */
static const BYTE weapon_leaves_proj_record_sig[]={0x55,0x8b,0xec,0x0f,0xbf,0x45,0x08,0xc1,0xe0,0x02};
static const BYTE weapon_leaves_pitch_point_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x0c,0x8b,0x5d,0x08};
static const BYTE weapon_leaves_damage_words_sig[]={0x55,0x8b,0xec,0x66,0x8b,0x45,0x08,0x66,0xa3,0xf2,0x95,0x4a,0x00};
static const BYTE weapon_leaves_bullet_charge_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x14,0x8b,0x5d,0x08};
static const BYTE weapon_leaves_rocket_init_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x0d,0xd0,0x96,0x4a,0x00};
static const BYTE weapon_leaves_load_lens[]={1,2,4},weapon_leaves_push_lens[]={1,2,1,1};
/* damage_words in compare mode: the two words in place. */
static const es2_hook_region weapon_leaves_damage_words_regions[]={{ES2_REGION_RVA,0,0xa95f2,0,0,4,FALSE,0,{0,0}}};
#define WEAPON_LEAVES_EXT(name,regions,count,refuse) \
    static const es2_hook_extension weapon_leaves_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,regions,count,weapon_leaves_##name,refuse}; \
    static uint32_t __cdecl weapon_leaves_##name##32(const uint32_t *a,void *u) {return (uint32_t)weapon_leaves_##name(a,u,weapons_no_registers);}
WEAPON_LEAVES_EXT(proj_record,NULL,0,FALSE)
WEAPON_LEAVES_EXT(pitch_point,NULL,0,FALSE)
WEAPON_LEAVES_EXT(damage_words,weapon_leaves_damage_words_regions,1,FALSE)
WEAPON_LEAVES_EXT(bullet_charge,NULL,0,TRUE)
WEAPON_LEAVES_EXT(rocket_init,NULL,0,TRUE)
#undef WEAPON_LEAVES_EXT
static es2_hook_descriptor weapon_leaves_hook_descriptors[]={
    {ES2_HOOK_ID(proj_record),weapon_leaves_proj_record_sig,10,7,weapon_leaves_load_lens,3,1,weapon_leaves_proj_record32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(pitch_point),weapon_leaves_pitch_point_sig,11,5,weapon_leaves_push_lens,4,2,weapon_leaves_pitch_point32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(damage_words),weapon_leaves_damage_words_sig,13,7,weapon_leaves_load_lens,3,2,weapon_leaves_damage_words32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(bullet_charge),weapon_leaves_bullet_charge_sig,11,5,weapon_leaves_push_lens,4,5,weapon_leaves_bullet_charge32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(rocket_init),weapon_leaves_rocket_init_sig,12,5,weapon_leaves_push_lens,4,2,weapon_leaves_rocket_init32,NULL,NULL,NULL,0},
};
#endif
