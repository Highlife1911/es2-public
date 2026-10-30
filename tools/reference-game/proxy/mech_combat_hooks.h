#ifndef ES2_MECH_COMBAT_HOOKS_H
#define ES2_MECH_COMBAT_HOOKS_H
#include "guest_native.h"
/* MECH combat states (docs/reverse-engineering/ai.md, section "MECH combat
 * states"):
 * the step members of the states "attacking" (attack_step 41c594),
 * "attacking base" (base_attack 41c86c), "attacking flyer" (flyer_attack
 * 41c9cc), "facing off" (face_off 41d41c), "flanking" (flank_attack 41d4e4)
 * and "driving off" (drive_off 41def0), and their helpers engage_end 41c478
 * and skirt_check 41de9c. Every game callee (the nested hooks of other
 * groups and this group's own helpers) goes through the weapons guest
 * bridge and callee observer (weapons_graph) with the original's
 * registers, so seven refuse compare mode. engage_end, a leaf without game
 * calls, accepts it: it writes only the state word A+23Eh, declared
 * as the span A+23Eh..A+24Bh (the clone also holds A+1A4h and A+248h, which
 * it compares); EAX = 1, EDX the target or the entry EDX, default
 * comparison. No game instruction is embedded (the shims only load and
 * store registers and move ESP).
 *
 * The six steps lie on the fire path: five call aim_select (41ea7c), whose
 * aim_fire reaches the weapon bodies that read uninitialized stack (ai.md,
 * "stack residue under aim_fire"), and drive_off calls face_off. Each has an entry shim like
 * retreat_fire's (ai_behaviour2_hooks.h): it keeps the original's EBP,
 * stores the caller's EBX/ESI/EDI in the original's save slots, reserves a
 * guard-probed gap (72 pages for the five aim_select callers: aim_select's
 * 68 plus its host frames, as retreat_fire; 74 for drive_off: face_off's 72
 * plus its host frames, which stay below 2 pages; the trampoline shims of
 * actor_step_hooks.h reserve 80 pages around all of them) and leaves {EBP, outgoing ESP} for the adapter, whose frame is
 * then the original one and whose game calls run through
 * ai_behaviour_stack_call (ai_behaviour_hooks.h) at the original stack
 * coordinate with the EBX/ESI/EDI the core names and the original's EBP and
 * return addresses. Unlike the ai_behaviour2 shims these count the pages in EBX
 * (restored from its save slot before the dispatch), so the entry EAX that
 * the originals hand to skirt_check reaches the adapter unchanged. The
 * caller's ESI and EDI, which the originals keep up to their first own
 * use, are read back from their save slots. Without the shim (direct and
 * legacy calls) the plain bridge and the adapter's own frame are used.
 * engage_end and skirt_check have no fire path and use the plain bridge. */
#include "es2/core/mech_combat.h"
static uint64_t __cdecl mech_combat_engage_end(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_engage_end_registers(&math_native_memory,a[0],r);
}
static uint64_t __cdecl mech_combat_skirt_check(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_skirt_check);
    return es2_skirt_check_registers(&math_native_memory,&g,a[0],r);
}
/* One step: its pending context and bridge, the adapter and the entry shim.
 * frame_size = the original's frame (add esp,-frame_size), so the saves are
 * at EBP-frame_size-4/-8/-0Ch (ebx, esi, edi) and the outgoing ESP is the
 * EDI slot; size = the
 * core's scratch (ES2_*_SCRATCH, the frame from its lowest local). */
#define MECH_COMBAT_STEP(name,size,frame_size,ebx,esi,edi,pages) \
__attribute__((used)) static ai_behaviour_stack *mech_combat_##name##_pending; \
static uint64_t __cdecl mech_combat_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    const ai_behaviour_stack *s=ai_behaviour2_take(&mech_combat_##name##_pending,a,1); \
    BYTE scratch[size]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##name); \
    ai_behaviour_guest c={s,{0,0,0,0}};uint32_t caller_esi=0,caller_edi=0; \
    if(s) { g.scratch=s->ebp-(uint32_t)(frame_size);g.call=ai_behaviour_stack_guest_call;g.context=&c; \
        caller_esi=math_native_read32(NULL,s->ebp+(uint32_t)(esi));caller_edi=math_native_read32(NULL,s->ebp+(uint32_t)(edi)); } \
    return es2_##name##_registers(&math_native_memory,&g,s?&c.frame:NULL,caller_esi,caller_edi,a[0],r); \
} \
static int mech_combat_##name##_entry(es2_machine *m,uint32_t bridge) { \
    uint32_t ebp;const uint32_t context=shim_saved_gap(m,bridge,&ebp,ebx,pages); \
    mech_combat_##name##_pending=es2_guest_span(context,sizeof(ai_behaviour_stack)); \
    const int ran=shim_gap_call(m,bridge,ebp,context,1); \
    mech_combat_##name##_pending=NULL;return ran; \
}
/* 41c594: frame 30h (P, L, S), saves EBP-34h/-38h/-3Ch. */
MECH_COMBAT_STEP(attack_step,ES2_ATTACK_STEP_SCRATCH,0x30,-0x34,-0x38,-0x3c,72)
/* 41c86c, 41c9cc, 41d41c, 41d4e4: frame 18h (S), saves EBP-1Ch/-20h/-24h. */
MECH_COMBAT_STEP(base_attack,ES2_COMBAT_STEP_SCRATCH,0x18,-0x1c,-0x20,-0x24,72)
MECH_COMBAT_STEP(flyer_attack,ES2_COMBAT_STEP_SCRATCH,0x18,-0x1c,-0x20,-0x24,72)
MECH_COMBAT_STEP(face_off,ES2_COMBAT_STEP_SCRATCH,0x18,-0x1c,-0x20,-0x24,72)
MECH_COMBAT_STEP(flank_attack,ES2_COMBAT_STEP_SCRATCH,0x18,-0x1c,-0x20,-0x24,72)
/* 41def0: frame 24h (unused by the live code), saves EBP-28h/-2Ch/-30h;
 * face_off's 72-page gap and host frames nest inside its 74 pages. */
MECH_COMBAT_STEP(drive_off,4,0x24,-0x28,-0x2c,-0x30,74)
#undef MECH_COMBAT_STEP
ES2_SHIM(mech_combat_attack_step_shim,0x1c594u,mech_combat_attack_step_entry,0);
ES2_SHIM(mech_combat_base_attack_shim,0x1c86cu,mech_combat_base_attack_entry,0);
ES2_SHIM(mech_combat_flyer_attack_shim,0x1c9ccu,mech_combat_flyer_attack_entry,0);
ES2_SHIM(mech_combat_face_off_shim,0x1d41cu,mech_combat_face_off_entry,0);
ES2_SHIM(mech_combat_flank_attack_shim,0x1d4e4u,mech_combat_flank_attack_entry,0);
ES2_SHIM(mech_combat_drive_off_shim,0x1def0u,mech_combat_drive_off_entry,0);
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA; engage_end has no game calls. */
static callee_counter skirt_check_counters[]={
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
};
static callee_counter attack_step_counters[]={
    {"call_41de9c",ES2_COLLIDE_CALL,0x41de9c,0},
    {"call_41e758",ES2_COLLIDE_CALL,0x41e758,0},
    {"call_4929fc",ES2_COLLIDE_CALL,0x4929fc,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_41e828",ES2_COLLIDE_CALL,0x41e828,0},
    {"call_41ea7c",ES2_COLLIDE_CALL,0x41ea7c,0},
    {"call_41c4a8",ES2_COLLIDE_CALL,0x41c4a8,0},
    {"call_41c478",ES2_COLLIDE_CALL,0x41c478,0},
};
static callee_counter base_attack_counters[]={
    {"call_41de9c",ES2_COLLIDE_CALL,0x41de9c,0},
    {"call_41e758",ES2_COLLIDE_CALL,0x41e758,0},
    {"call_41c72c",ES2_COLLIDE_CALL,0x41c72c,0},
    {"call_41e828",ES2_COLLIDE_CALL,0x41e828,0},
    {"call_41ea7c",ES2_COLLIDE_CALL,0x41ea7c,0},
    {"call_423918",ES2_COLLIDE_CALL,0x423918,0},
    {"call_41c478",ES2_COLLIDE_CALL,0x41c478,0},
};
static callee_counter flyer_attack_counters[]={
    {"call_41e758",ES2_COLLIDE_CALL,0x41e758,0},
    {"call_41e828",ES2_COLLIDE_CALL,0x41e828,0},
    {"call_41ea7c",ES2_COLLIDE_CALL,0x41ea7c,0},
    {"call_41c478",ES2_COLLIDE_CALL,0x41c478,0},
};
static callee_counter face_off_counters[]={
    {"call_41de9c",ES2_COLLIDE_CALL,0x41de9c,0},
    {"call_41e758",ES2_COLLIDE_CALL,0x41e758,0},
    {"call_41e828",ES2_COLLIDE_CALL,0x41e828,0},
    {"call_41ea7c",ES2_COLLIDE_CALL,0x41ea7c,0},
    {"call_41c4a8",ES2_COLLIDE_CALL,0x41c4a8,0},
    {"call_41c478",ES2_COLLIDE_CALL,0x41c478,0},
};
static callee_counter flank_attack_counters[]={
    {"call_41de9c",ES2_COLLIDE_CALL,0x41de9c,0},
    {"call_41e758",ES2_COLLIDE_CALL,0x41e758,0},
    {"call_41c72c",ES2_COLLIDE_CALL,0x41c72c,0},
    {"call_41e828",ES2_COLLIDE_CALL,0x41e828,0},
    {"call_41ea7c",ES2_COLLIDE_CALL,0x41ea7c,0},
    {"call_41c4a8",ES2_COLLIDE_CALL,0x41c4a8,0},
    {"call_41c478",ES2_COLLIDE_CALL,0x41c478,0},
};
static callee_counter drive_off_counters[]={
    {"call_41de9c",ES2_COLLIDE_CALL,0x41de9c,0},
    {"call_41d41c",ES2_COLLIDE_CALL,0x41d41c,0},
};
static callee_counter *mech_combat_hook_counters(unsigned hook,unsigned *count) {
#define MECH_COMBAT_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    MECH_COMBAT_COUNTERS(skirt_check)
    MECH_COMBAT_COUNTERS(attack_step)
    MECH_COMBAT_COUNTERS(base_attack)
    MECH_COMBAT_COUNTERS(flyer_attack)
    MECH_COMBAT_COUNTERS(face_off)
    MECH_COMBAT_COUNTERS(flank_attack)
    MECH_COMBAT_COUNTERS(drive_off)
#undef MECH_COMBAT_COUNTERS
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; then mov eax,[ebp+8] (41c478), push ebx/esi/edi
 * (41de9c) or add esp,-n and push ebx/esi/edi (the steps). */
static const BYTE mech_combat_engage_end_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x83,0xb8};
static const BYTE mech_combat_skirt_check_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE mech_combat_attack_step_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd0,0x53,0x56,0x57};
static const BYTE mech_combat_step18_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x53,0x56,0x57};
static const BYTE mech_combat_drive_off_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0x53,0x56,0x57};
static const BYTE mech_combat_frame_lens[]={1,2,3},mech_combat_push_lens[]={1,2,1,1};
/* engage_end in compare mode: the state word A+23Eh (with A+248h read). */
static const es2_hook_span mech_combat_engage_end_spans[]={{0,0x23e,0x0e}};
#define MECH_COMBAT_EXT(name,refuse) \
    static const es2_hook_extension mech_combat_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,mech_combat_##name,refuse}; \
    static uint32_t __cdecl mech_combat_##name##32(const uint32_t *a,void *u) {return (uint32_t)mech_combat_##name(a,u,weapons_no_registers);}
MECH_COMBAT_EXT(engage_end,FALSE)
MECH_COMBAT_EXT(skirt_check,TRUE)
MECH_COMBAT_EXT(attack_step,TRUE)
MECH_COMBAT_EXT(base_attack,TRUE)
MECH_COMBAT_EXT(flyer_attack,TRUE)
MECH_COMBAT_EXT(face_off,TRUE)
MECH_COMBAT_EXT(flank_attack,TRUE)
MECH_COMBAT_EXT(drive_off,TRUE)
#undef MECH_COMBAT_EXT
static es2_hook_descriptor mech_combat_hook_descriptors[]={
    {ES2_HOOK_ID(engage_end),mech_combat_engage_end_sig,9,6,mech_combat_frame_lens,3,1,mech_combat_engage_end32,NULL,NULL,mech_combat_engage_end_spans,1},
    {ES2_HOOK_ID(skirt_check),mech_combat_skirt_check_sig,9,5,mech_combat_push_lens,4,1,mech_combat_skirt_check32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(attack_step),mech_combat_attack_step_sig,9,6,mech_combat_frame_lens,3,1,mech_combat_attack_step32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(base_attack),mech_combat_step18_sig,9,6,mech_combat_frame_lens,3,1,mech_combat_base_attack32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_attack),mech_combat_step18_sig,9,6,mech_combat_frame_lens,3,1,mech_combat_flyer_attack32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(face_off),mech_combat_step18_sig,9,6,mech_combat_frame_lens,3,1,mech_combat_face_off32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flank_attack),mech_combat_step18_sig,9,6,mech_combat_frame_lens,3,1,mech_combat_flank_attack32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(drive_off),mech_combat_drive_off_sig,9,6,mech_combat_frame_lens,3,1,mech_combat_drive_off32,NULL,NULL,NULL,0},
};
#endif
