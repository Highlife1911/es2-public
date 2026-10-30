#ifndef ES2_STAGE_FLUSH_HOOKS_H
#define ES2_STAGE_FLUSH_HOOKS_H
/* stage_flush group (docs/reverse-engineering/main_loop.md, section
 * "the stage-5 flushes and the manager slot methods"): the eleven
 * stage-5 callbacks of the table 4A804Ch and 408f84, which one of them
 * calls: flush_buildings 405ea4, flush_lights 4077e8, flush_explosions
 * 407b3c, flush_debris 4083e0, flush_smoke_balls 408f84, flush_smoke 408f9c,
 * flush_attached 409904, flush_meteors 409a1c, flush_projectiles 40a108,
 * flush_mechs 41544c, flush_flyers 4215a8, flush_fires 46b188 (core in
 * core/src/render_root.c).
 *
 * The table keeps the original VAs, so stage_dispatch (original or
 * replaced) reaches these hooks through their patched prologues, and so do
 * the direct callers (the mission-end drains, vehicle_part). Every game
 * call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX: removal_flush 478644 (a
 * hook of group removal) and, under flush_smoke, 408f84 (flush_smoke_balls)
 * keep their own modes. EDX:EAX exact; ECX is the framework's (the entry
 * ECX), dead at every caller (main_loop.md). No entry register but ECX is
 * read and no stack below the entry ESP, so no shim. All refuse compare
 * mode (game calls). No game instruction is embedded. */
#include "es2/core/render_root.h"
#define STAGE_FLUSH_ADAPTER(name,k) \
static uint64_t __cdecl stage_flush_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    (void)a;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name);uint32_t ecx; \
    return es2_stage_flush_registers(&math_native_memory,&g,k,r,&ecx); }
STAGE_FLUSH_ADAPTER(flush_buildings,0)
STAGE_FLUSH_ADAPTER(flush_lights,1)
STAGE_FLUSH_ADAPTER(flush_explosions,2)
STAGE_FLUSH_ADAPTER(flush_debris,3)
STAGE_FLUSH_ADAPTER(flush_smoke_balls,4)
STAGE_FLUSH_ADAPTER(flush_smoke,5)
STAGE_FLUSH_ADAPTER(flush_attached,6)
STAGE_FLUSH_ADAPTER(flush_meteors,7)
STAGE_FLUSH_ADAPTER(flush_projectiles,8)
STAGE_FLUSH_ADAPTER(flush_mechs,9)
STAGE_FLUSH_ADAPTER(flush_flyers,10)
STAGE_FLUSH_ADAPTER(flush_fires,11)
#undef STAGE_FLUSH_ADAPTER
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA. */
#define STAGE_FLUSH_COUNTER(name) static callee_counter name##_counters[]={{"call_478644",ES2_COLLIDE_CALL,0x478644,0}};
STAGE_FLUSH_COUNTER(flush_buildings)
STAGE_FLUSH_COUNTER(flush_lights)
STAGE_FLUSH_COUNTER(flush_explosions)
STAGE_FLUSH_COUNTER(flush_debris)
STAGE_FLUSH_COUNTER(flush_smoke_balls)
STAGE_FLUSH_COUNTER(flush_attached)
STAGE_FLUSH_COUNTER(flush_meteors)
STAGE_FLUSH_COUNTER(flush_projectiles)
STAGE_FLUSH_COUNTER(flush_mechs)
STAGE_FLUSH_COUNTER(flush_flyers)
STAGE_FLUSH_COUNTER(flush_fires)
#undef STAGE_FLUSH_COUNTER
static callee_counter flush_smoke_counters[]={
    {"call_478644",ES2_COLLIDE_CALL,0x478644,0},
    {"call_408f84",ES2_COLLIDE_CALL,0x408f84,0},
};
static callee_counter *stage_flush_hook_counters(unsigned hook,unsigned *count) {
#define STAGE_FLUSH_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
    STAGE_FLUSH_COUNTERS(flush_buildings)
    STAGE_FLUSH_COUNTERS(flush_lights)
    STAGE_FLUSH_COUNTERS(flush_explosions)
    STAGE_FLUSH_COUNTERS(flush_debris)
    STAGE_FLUSH_COUNTERS(flush_smoke_balls)
    STAGE_FLUSH_COUNTERS(flush_smoke)
    STAGE_FLUSH_COUNTERS(flush_attached)
    STAGE_FLUSH_COUNTERS(flush_meteors)
    STAGE_FLUSH_COUNTERS(flush_projectiles)
    STAGE_FLUSH_COUNTERS(flush_mechs)
    STAGE_FLUSH_COUNTERS(flush_flyers)
    STAGE_FLUSH_COUNTERS(flush_fires)
#undef STAGE_FLUSH_COUNTERS
    *count=0;return NULL;
}
/* Every flush starts MOV EAX,[P]; PUSH EAX; MOV EDX,[P+4]; PUSH EDX.
 * Relocated: the five-byte MOV EAX,[moffs32] (absolute operand). */
static const BYTE flush_buildings_sig[]={0xa1,0xa0,0x95,0x4a,0x00,0x50,0x8b,0x15,0xa4,0x95,0x4a,0x00,0x52};
static const BYTE flush_lights_sig[]={0xa1,0xfe,0x95,0x4a,0x00,0x50,0x8b,0x15,0x02,0x96,0x4a,0x00,0x52};
static const BYTE flush_explosions_sig[]={0xa1,0x1e,0x96,0x4a,0x00,0x50,0x8b,0x15,0x22,0x96,0x4a,0x00,0x52};
static const BYTE flush_debris_sig[]={0xa1,0x42,0x96,0x4a,0x00,0x50,0x8b,0x15,0x46,0x96,0x4a,0x00,0x52};
static const BYTE flush_smoke_balls_sig[]={0xa1,0x6e,0x96,0x4a,0x00,0x50,0x8b,0x15,0x72,0x96,0x4a,0x00,0x52};
static const BYTE flush_smoke_sig[]={0xa1,0x84,0x96,0x4a,0x00,0x50,0x8b,0x15,0x88,0x96,0x4a,0x00,0x52};
static const BYTE flush_attached_sig[]={0xa1,0x8d,0x96,0x4a,0x00,0x50,0x8b,0x15,0x91,0x96,0x4a,0x00,0x52};
static const BYTE flush_meteors_sig[]={0xa1,0xaa,0x96,0x4a,0x00,0x50,0x8b,0x15,0xae,0x96,0x4a,0x00,0x52};
static const BYTE flush_projectiles_sig[]={0xa1,0xc2,0x96,0x4a,0x00,0x50,0x8b,0x15,0xc6,0x96,0x4a,0x00,0x52};
static const BYTE flush_mechs_sig[]={0xa1,0x7a,0x9b,0x4a,0x00,0x50,0x8b,0x15,0x7e,0x9b,0x4a,0x00,0x52};
static const BYTE flush_flyers_sig[]={0xa1,0xb9,0x9d,0x4a,0x00,0x50,0x8b,0x15,0xbd,0x9d,0x4a,0x00,0x52};
static const BYTE flush_fires_sig[]={0xa1,0x30,0x4f,0x6b,0x00,0x50,0x8b,0x15,0x34,0x4f,0x6b,0x00,0x52};
static const BYTE stage_flush_lens[]={5};
#define STAGE_FLUSH_EXT(name) \
    static const es2_hook_extension stage_flush_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,stage_flush_##name,TRUE}; \
    static uint32_t __cdecl stage_flush_##name##32(const uint32_t *a,void *u) {return (uint32_t)stage_flush_##name(a,u,weapons_no_registers);}
STAGE_FLUSH_EXT(flush_buildings)
STAGE_FLUSH_EXT(flush_lights)
STAGE_FLUSH_EXT(flush_explosions)
STAGE_FLUSH_EXT(flush_debris)
STAGE_FLUSH_EXT(flush_smoke_balls)
STAGE_FLUSH_EXT(flush_smoke)
STAGE_FLUSH_EXT(flush_attached)
STAGE_FLUSH_EXT(flush_meteors)
STAGE_FLUSH_EXT(flush_projectiles)
STAGE_FLUSH_EXT(flush_mechs)
STAGE_FLUSH_EXT(flush_flyers)
STAGE_FLUSH_EXT(flush_fires)
#undef STAGE_FLUSH_EXT
#define STAGE_FLUSH_DESCRIPTOR(name) \
    {ES2_HOOK_ID(name),name##_sig,sizeof(name##_sig),5,stage_flush_lens,1,0,stage_flush_##name##32,NULL,NULL,NULL,0}
static es2_hook_descriptor stage_flush_hook_descriptors[]={
    STAGE_FLUSH_DESCRIPTOR(flush_buildings),
    STAGE_FLUSH_DESCRIPTOR(flush_lights),
    STAGE_FLUSH_DESCRIPTOR(flush_explosions),
    STAGE_FLUSH_DESCRIPTOR(flush_debris),
    STAGE_FLUSH_DESCRIPTOR(flush_smoke_balls),
    STAGE_FLUSH_DESCRIPTOR(flush_smoke),
    STAGE_FLUSH_DESCRIPTOR(flush_attached),
    STAGE_FLUSH_DESCRIPTOR(flush_meteors),
    STAGE_FLUSH_DESCRIPTOR(flush_projectiles),
    STAGE_FLUSH_DESCRIPTOR(flush_mechs),
    STAGE_FLUSH_DESCRIPTOR(flush_flyers),
    STAGE_FLUSH_DESCRIPTOR(flush_fires),
};
#undef STAGE_FLUSH_DESCRIPTOR
#endif
