#ifndef ES2_LC_BASE_HOOKS_H
#define ES2_LC_BASE_HOOKS_H
#include "guest_native.h"
/* LC_BASE update (docs/reverse-engineering/vtables.md, section
 * "lc_base_update: contract"): lc_base_update 4045c8 (slot +18h of
 * LC_BASE, row 10 of the logic frame), projectile_create 40a9c4 (also
 * called by turret, weapon_fire and target_emit) and weapon_record 40fe84
 * (also called by the weapon factory 40fff8). Every game callee goes
 * through the weapons guest bridge and callee observer (weapons_graph)
 * with the original's registers, so the nested hooks (base_update,
 * base_parts, timer_word_step, target_select, dist_gate, aim_angles,
 * rng_bounded, sincos_pair, rotate_2d, euler_matrix, impact, pool_alloc,
 * projectile_def, the actor virtuals and this group's own functions) keep
 * their modes and the constructor 40a948, the runtime calls 49630c/49681a
 * and the sound 4628e0 run as original code. lc_base_update and
 * projectile_create refuse compare mode; weapon_record (index * 58h +
 * [4a98ech], EDX the sign-extended index, no store) accepts it with the
 * default comparison. No game instruction is
 * embedded (the shims only load and store registers and move ESP).
 *
 * lc_base_update and projectile_create have entry shims like the MECH
 * combat steps (mech_combat_hooks.h): the shim keeps the original's EBP, stores
 * the caller's EBX/ESI/EDI in the original's save slots, reserves a
 * guard-probed gap (40 pages = 160 KiB for lc_base_update, holding
 * projectile_create's 16 pages, impact's and target_select's subtrees and
 * their host frames; 16 pages = 64 KiB for projectile_create) and leaves
 * {EBP, outgoing ESP} for the adapter, whose frame is then the original one
 * and whose game calls run through ai_behaviour_stack_call at the original
 * stack coordinate with the EBX/ESI/EDI the core names and the original's
 * EBP and return addresses. So impact finds at its entry ESP-88h (the
 * receiver that sweep excludes) the bytes the original would leave there,
 * projectile_create's runtime record lies at the original f-24h (49630c
 * runs with the original EBP) and the frames hold the original bytes for
 * later readers of stale stack (turret's entry shim). The pages are counted
 * in EBX (reloaded from its save slot before the dispatch), so the entry
 * EAX/EDX/ECX reach the adapter unchanged. Without the shim (direct and
 * legacy calls) the plain bridge and the adapter's own frame are used; for
 * projectile_create then a synthetic EBP in the scratch for 49630c
 * (weapons_impact_guest_call). projectile_create's adapter stores FS:0
 * from the returned EDX (the record's saved head), as the original's
 * epilogue does. */
#include "es2/core/lc_base.h"
static uint64_t __cdecl lc_base_weapon_record(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_weapon_record_registers(math_native_read32(NULL,(uint32_t)(uintptr_t)u+0xa98ecu),a[0]);
}
__attribute__((used)) static ai_behaviour_stack *lc_base_update_pending,*lc_base_projectile_pending;
static uint64_t __cdecl lc_base_lc_base_update(const uint32_t *a,void *u,const uint32_t *r) {
    const ai_behaviour_stack *s=ai_behaviour2_take(&lc_base_update_pending,a,1);
    BYTE scratch[ES2_LC_BASE_UPDATE_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_lc_base_update);
    ai_behaviour_guest c={s,{0,0,0,0}};uint32_t esi=0,edi=0;
    if(s) { g.scratch=s->ebp-ES2_LC_BASE_UPDATE_SCRATCH;g.call=ai_behaviour_stack_guest_call;g.context=&c;
        esi=math_native_read32(NULL,s->ebp-0xc8u);edi=math_native_read32(NULL,s->ebp-0xccu); }
    return es2_lc_base_update_registers(&math_native_memory,&g,s?&c.frame:NULL,esi,edi,a[0],r);
}
static uint64_t __cdecl lc_base_projectile_create(const uint32_t *a,void *u,const uint32_t *r) {
    const ai_behaviour_stack *s=ai_behaviour2_take(&lc_base_projectile_pending,a,5);
    BYTE scratch[ES2_PROJECTILE_CREATE_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_projectile_create);
    ai_behaviour_guest c={s,{0,0,0,0}};uint32_t ebx=0,edi=0;
    weapons_impact_context x={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_PROJECTILE_CREATE_SCRATCH)};
    if(s) { g.scratch=s->ebp-ES2_PROJECTILE_CREATE_SCRATCH;g.call=ai_behaviour_stack_guest_call;g.context=&c;
        ebx=math_native_read32(NULL,s->ebp-0x2cu);edi=math_native_read32(NULL,s->ebp-0x34u); }
    else { g.call=weapons_impact_guest_call;g.context=&x; }
    uint64_t result=es2_projectile_create_registers(&math_native_memory,&g,s?&c.frame:NULL,ebx,edi,a[0],a[1],a[2],a[3],a[4],r);
    uint32_t head=(uint32_t)(result>>32);es2_machine_set_fs0(head);
    return result;
}
/* The entry shims: save slots EBP+save, -4, -8 (ebx, esi, edi; the
 * outgoing ESP is the EDI slot), the guard-probed pages and the argument
 * slots copied for the framework's dispatch. */
static int lc_base_entry(es2_machine *m,uint32_t bridge,ai_behaviour_stack **pending,int32_t save,uint32_t pages,unsigned count) {
    uint32_t ebp;const uint32_t context=shim_saved_gap(m,bridge,&ebp,save,pages);
    *pending=es2_guest_span(context,sizeof(ai_behaviour_stack));
    const int ran=shim_gap_call(m,bridge,ebp,context,count);
    *pending=NULL;return ran;
}
static int lc_base_update_entry(es2_machine *m,uint32_t bridge) {return lc_base_entry(m,bridge,&lc_base_update_pending,-0xc4,40u,1);}
static int lc_base_projectile_entry(es2_machine *m,uint32_t bridge) {return lc_base_entry(m,bridge,&lc_base_projectile_pending,-0x2c,16u,5);}
ES2_SHIM(lc_base_update_shim,0x45c8u,lc_base_update_entry,0);
ES2_SHIM(lc_base_projectile_shim,0xa9c4u,lc_base_projectile_entry,0);
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the vtable offset for slots;
 * weapon_record has no game calls. */
static callee_counter lc_base_update_counters[]={
    {"call_403ca8",ES2_COLLIDE_CALL,0x403ca8,0},
    {"call_403914",ES2_COLLIDE_CALL,0x403914,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_411fa0",ES2_COLLIDE_CALL,0x411fa0,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"slot_30",ES2_COLLIDE_SLOT,0x30,0},
    {"call_492990",ES2_COLLIDE_CALL,0x492990,0},
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"call_47eb30",ES2_COLLIDE_CALL,0x47eb30,0},
    {"call_47ffe8",ES2_COLLIDE_CALL,0x47ffe8,0},
    {"call_40a9c4",ES2_COLLIDE_CALL,0x40a9c4,0},
    {"call_40fe84",ES2_COLLIDE_CALL,0x40fe84,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_40bf74",ES2_COLLIDE_CALL,0x40bf74,0},
};
static callee_counter projectile_create_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_40a948",ES2_COLLIDE_CALL,0x40a948,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"slot_6c",ES2_COLLIDE_SLOT,0x6c,0},
    {"slot_54",ES2_COLLIDE_SLOT,0x54,0},
    {"call_40a234",ES2_COLLIDE_CALL,0x40a234,0},
    {"call_4628e0",ES2_COLLIDE_CALL,0x4628e0,0},
};
static callee_counter *lc_base_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_lc_base_update) {
        *count=sizeof(lc_base_update_counters)/sizeof(lc_base_update_counters[0]);return lc_base_update_counters; }
    if(hook==ES2_HOOK_INDEX_projectile_create) {
        *count=sizeof(projectile_create_counters)/sizeof(projectile_create_counters[0]);return projectile_create_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-0C0h (4045c8) / add esp,-28h; mov
 * eax,498370h (40a9c4) / movsx eax,word [ebp+8] (40fe84). */
static const BYTE lc_base_update_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x40,0xff,0xff,0xff};
static const BYTE lc_base_projectile_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0xb8,0x70,0x83,0x49,0x00};
static const BYTE lc_base_record_sig[]={0x55,0x8b,0xec,0x0f,0xbf,0x45,0x08};
static const BYTE lc_base_update_lens[]={1,2,6},lc_base_frame_lens[]={1,2,3},lc_base_record_lens[]={1,2,4};
#define LC_BASE_EXT(name,refuse) \
    static const es2_hook_extension lc_base_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,lc_base_##name,refuse}; \
    static uint32_t __cdecl lc_base_##name##32(const uint32_t *a,void *u) {return (uint32_t)lc_base_##name(a,u,weapons_no_registers);}
LC_BASE_EXT(lc_base_update,TRUE)
LC_BASE_EXT(projectile_create,TRUE)
LC_BASE_EXT(weapon_record,FALSE)
#undef LC_BASE_EXT
static es2_hook_descriptor lc_base_hook_descriptors[]={
    {ES2_HOOK_ID(lc_base_update),lc_base_update_sig,9,9,lc_base_update_lens,3,1,lc_base_lc_base_update32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(projectile_create),lc_base_projectile_sig,11,6,lc_base_frame_lens,3,5,lc_base_projectile_create32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(weapon_record),lc_base_record_sig,7,7,lc_base_record_lens,3,1,lc_base_weapon_record32,NULL,NULL,NULL,0},
};
#endif
