#ifndef ES2_FORMATION_HOOKS_H
#define ES2_FORMATION_HOOKS_H
/* formation group (docs/reverse-engineering/ai.md, section
 * "Formation slot virtual +78h"): the actor virtual +78h,
 * formation_base 405c04 (BASE classes), formation_mech 417898 (MECH) and
 * formation_flyer 421e98 (FLYER). Core in core/src/formation.c. The slot
 * helpers 405b9c/4205cc/423044 and the placement 411d64 are computed in
 * core and counted as call_<address> without a game call; euler_matrix
 * 47ebb8 and rotate_2d 47ffe8 go through the weapons guest bridge and
 * callee observer (weapons_graph) with the original's registers, so they
 * keep their own modes. The slot point Q and the rotated offset R live in
 * the adapter's scratch; the static buffers 4975d6/49a430 are the game's.
 * The entry registers come from the register context (returned unchanged
 * on the slot-0 paths). No local is read before it is written, so no entry
 * shim. All three accept compare mode: a 12-byte span on P, an
 * RVA region for the class's static buffer with its first-call byte
 * (BASE 4975d6..4975e2, MECH 49a430..49a436; FLYER has none) and the
 * default comparison (EAX/EDX are values or the entry registers). The
 * leader's lazy matrix cache (word L+32h, L+12h..L+24h through
 * euler_matrix; L three pointers away) is filled by the original phase, so
 * the replacement phase finds it set and does not write it; rotate_2d
 * writes only the adapter's scratch (compare_mode.md, "spans and
 * comparators"). No game
 * instruction is embedded. */
#include "es2/core/formation.h"
#define FORMATION_ADAPTER(name) \
static uint64_t __cdecl formation_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    BYTE scratch[ES2_FORMATION_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##name); \
    return es2_##name##_registers(&math_native_memory,&g,a[0],a[1],r); }
FORMATION_ADAPTER(formation_base)
FORMATION_ADAPTER(formation_mech)
FORMATION_ADAPTER(formation_flyer)
#undef FORMATION_ADAPTER
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. */
static callee_counter formation_base_counters[]={
    {"call_405b9c",ES2_COLLIDE_CALL,0x405b9c,0},
    {"call_411d64",ES2_COLLIDE_CALL,0x411d64,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47ffe8",ES2_COLLIDE_CALL,0x47ffe8,0},
};
static callee_counter formation_mech_counters[]={
    {"call_4205cc",ES2_COLLIDE_CALL,0x4205cc,0},
    {"call_411d64",ES2_COLLIDE_CALL,0x411d64,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47ffe8",ES2_COLLIDE_CALL,0x47ffe8,0},
};
static callee_counter formation_flyer_counters[]={
    {"call_423044",ES2_COLLIDE_CALL,0x423044,0},
    {"call_411d64",ES2_COLLIDE_CALL,0x411d64,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47ffe8",ES2_COLLIDE_CALL,0x47ffe8,0},
};
static callee_counter *formation_hook_counters(unsigned hook,unsigned *count) {
#define FORMATION_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    FORMATION_COUNTERS(formation_base)
    FORMATION_COUNTERS(formation_mech)
    FORMATION_COUNTERS(formation_flyer)
#undef FORMATION_COUNTERS
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8]; mov si,[ebx+49h] */
static const BYTE formation_base_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08,0x66,0x8b,0x73,0x49};
/* push ebp; mov ebp,esp; add esp,-0Ch; push ebx; mov ebx,[ebp+8]; mov ax,[ebx+49h]
 * (417898 and 421e98 begin with the same bytes) */
static const BYTE formation_word_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x8b,0x5d,0x08,0x66,0x8b,0x43,0x49};
static const BYTE formation_saved_lens[]={1,2,1,1},formation_frame_lens[]={1,2,3};
/* Compare: the static buffers in place, P as a span. */
static const es2_hook_region formation_base_regions[]={{ES2_REGION_RVA,0,0x975d6,0,0,13,FALSE,0,{0,0}}},
    formation_mech_regions[]={{ES2_REGION_RVA,0,0x9a430,0,0,7,FALSE,0,{0,0}}};
#define FORMATION_EXT(name,regions,count) \
    static const es2_hook_extension formation_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,regions,count,formation_##name,FALSE}; \
    static uint32_t __cdecl formation_##name##32(const uint32_t *a,void *u) {return (uint32_t)formation_##name(a,u,weapons_no_registers);}
FORMATION_EXT(formation_base,formation_base_regions,1)
FORMATION_EXT(formation_mech,formation_mech_regions,1)
FORMATION_EXT(formation_flyer,NULL,0)
#undef FORMATION_EXT
static const es2_hook_span formation_point_spans[]={{1,0,12}};
static es2_hook_descriptor formation_hook_descriptors[]={
    {ES2_HOOK_ID(formation_base),formation_base_sig,12,5,formation_saved_lens,4,2,formation_formation_base32,NULL,NULL,formation_point_spans,1},
    {ES2_HOOK_ID(formation_mech),formation_word_sig,14,6,formation_frame_lens,3,2,formation_formation_mech32,NULL,NULL,formation_point_spans,1},
    {ES2_HOOK_ID(formation_flyer),formation_word_sig,14,6,formation_frame_lens,3,2,formation_formation_flyer32,NULL,NULL,formation_point_spans,1},
};
#endif
