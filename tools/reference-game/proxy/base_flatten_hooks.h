#ifndef ES2_BASE_FLATTEN_HOOKS_H
#define ES2_BASE_FLATTEN_HOOKS_H
/* base_flatten group (docs/reverse-engineering/terrain.md, section
 * "base flattening and the base-mark raster"; mission_load.md
 * section 27): base_flatten 47129c (from the actor load 4253d8 at 4263d9,
 * or its hook actor_load through the bridge), its two recursive floods
 * flood_sum 470fe8 and flood_fill 471128 (from 47129c at 471325/47133f and
 * from themselves), the base-mark raster mark_raster 47136c (from
 * base_marks 405db0 at 405de9, or its hook through the bridge) and the cell
 * mark read cell_flag_get 470e00 (from 470fe8, 471128, 47129c and per frame
 * from the cell-object draw 46e5a4). All but cell_flag_get run during
 * the load only (live with the early arming, the default). Core
 * appended to core/src/terrain_load.c.
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX: cell_flag_get 470e00 and
 * cell_flag_set 470ddc from the floods, cell_flag_get, flood_sum,
 * flood_fill and the shading pass 46c2e0 from base_flatten,
 * cell_mark_set 470d74 from mark_raster; so the nested hooks keep
 * their modes. The floods' own recursion runs inside the core as a loop
 * with an explicit stack of one byte per level (the direction from the
 * parent), in the original's call order; it never reaches the game stack
 * (the original needs 44 resp. 52 bytes per level there). A path longer
 * than the 65,536 entries per flood (more cells than a 256 x 256 grid has;
 * the original's 1 MiB stack ends at 23,831 levels) raises
 * EXCEPTION_STACK_OVERFLOW as the original would fault; base_flatten's
 * IDIV with a zero count (or 80000000h / -1) faults here as at 471333.
 *
 * EDX:EAX exact; ECX is dead after every call site (terrain.md, section
 * "base flattening and the base-mark raster", "Callers' registers"), so it is not returned (the framework
 * returns the entry ECX). No original reads an uninitialized local; no
 * entry or exit shim. cell_flag_get accepts compare mode (a pure read, no
 * spans or regions: the pair is compared); the other four refuse it (game
 * calls through the bridge that write the marks and heights). No game
 * instruction is embedded. */
#include "es2/core/terrain_load.h"
enum { BASE_FLATTEN_PATH = 65536 };
static uint8_t base_flatten_sum_path[BASE_FLATTEN_PATH],base_flatten_fill_path[BASE_FLATTEN_PATH];
static void base_flatten_check(const es2_flood_stack *s) {
    if(s->overflow)es2_machine_stack_overflow();
}
static uint64_t __cdecl base_flatten_base_flatten(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_base_flatten);uint32_t divide[2]={0,0};int fault=0;
    uint64_t result=es2_base_flatten_registers(&math_native_memory,&g,a[0],r,divide,&fault,NULL);
    if(fault) {
        /* The original's IDIV DWORD [6B5474h] at 471333 faults here (#DE). */
        result=es2_machine_idiv(divide[0],divide[1]);
    }
    return result;
}
static uint64_t __cdecl base_flatten_flood_sum(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_flood_sum);
    es2_flood_stack s={base_flatten_sum_path,BASE_FLATTEN_PATH,0,0};
    const uint64_t result=es2_flood_sum_registers(&math_native_memory,&g,a[0],a[1],a[2],r,&s,NULL);
    base_flatten_check(&s);return result;
}
static uint64_t __cdecl base_flatten_flood_fill(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_flood_fill);
    es2_flood_stack s={base_flatten_fill_path,BASE_FLATTEN_PATH,0,0};
    const uint64_t result=es2_flood_fill_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r,&s,NULL);
    base_flatten_check(&s);return result;
}
static uint64_t __cdecl base_flatten_mark_raster(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_mark_raster);
    return es2_mark_raster_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r,NULL);
}
static uint64_t __cdecl base_flatten_cell_flag_get(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_cell_flag_get_registers(&math_native_memory,a[0],a[1],a[2],NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA. The floods' recursion is internal and
 * not counted: the entries of 470fe8 an all-original run counts are
 * hk_flood_sum_calls + 8 x hk_flood_sum_call_470ddc (the same for 471128). */
static callee_counter base_flatten_counters[]={
    {"call_470e00",ES2_COLLIDE_CALL,0x470e00,0},
    {"call_470fe8",ES2_COLLIDE_CALL,0x470fe8,0},
    {"call_471128",ES2_COLLIDE_CALL,0x471128,0},
    {"call_46c2e0",ES2_COLLIDE_CALL,0x46c2e0,0},
};
static callee_counter flood_sum_counters[]={
    {"call_470e00",ES2_COLLIDE_CALL,0x470e00,0},
    {"call_470ddc",ES2_COLLIDE_CALL,0x470ddc,0},
};
static callee_counter flood_fill_counters[]={
    {"call_470e00",ES2_COLLIDE_CALL,0x470e00,0},
    {"call_470ddc",ES2_COLLIDE_CALL,0x470ddc,0},
};
static callee_counter mark_raster_counters[]={
    {"call_470d74",ES2_COLLIDE_CALL,0x470d74,0},
};
#define BASE_FLATTEN_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *base_flatten_hook_counters(unsigned hook,unsigned *count) {
    BASE_FLATTEN_COUNTERS(base_flatten)
    BASE_FLATTEN_COUNTERS(flood_sum)
    BASE_FLATTEN_COUNTERS(flood_fill)
    BASE_FLATTEN_COUNTERS(mark_raster)
    *count=0;return NULL;
}
#undef BASE_FLATTEN_COUNTERS
/* 47129c: push ebp; mov ebp,esp; add esp,-8; push ebx; push esi; push edi;
 * mov esi,[ebp+8]. Relocated 6 bytes (1,2,3).
 * 470fe8 and 471128 (the same head): push ebp; mov ebp,esp; add esp,-0Ch;
 * mov eax,1. Relocated 6 bytes (1,2,3).
 * 47136c: push ebp; mov ebp,esp; add esp,-20h; mov ecx,8. Relocated 6
 * bytes (1,2,3).
 * 470e00: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov ecx,[eax+100h].
 * Relocated 6 bytes (1,2,3); the body is 32 bytes long, the patch's five
 * bytes stay inside it. */
static const BYTE base_flatten_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x57,0x8b,0x75,0x08};
static const BYTE flood_sum_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0xb8,0x01,0x00,0x00,0x00,0x53,0x56,0x57};
static const BYTE flood_fill_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0xb8,0x01,0x00,0x00,0x00,0x53,0x56,0x57};
static const BYTE mark_raster_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe0,0xb9,0x08,0x00,0x00,0x00,0x53,0x56,0x57};
static const BYTE cell_flag_get_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x88,0x00,0x01,0x00,0x00};
static const BYTE base_flatten_lens[]={1,2,3},flood_sum_lens[]={1,2,3},flood_fill_lens[]={1,2,3},
    mark_raster_lens[]={1,2,3},cell_flag_get_lens[]={1,2,3};
#define BASE_FLATTEN_EXT(name,refuse) \
    static const es2_hook_extension base_flatten_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,base_flatten_##name,refuse}; \
    static uint32_t __cdecl base_flatten_##name##32(const uint32_t *a,void *u) {return (uint32_t)base_flatten_##name(a,u,weapons_no_registers);}
BASE_FLATTEN_EXT(base_flatten,TRUE)
BASE_FLATTEN_EXT(flood_sum,TRUE)
BASE_FLATTEN_EXT(flood_fill,TRUE)
BASE_FLATTEN_EXT(mark_raster,TRUE)
BASE_FLATTEN_EXT(cell_flag_get,FALSE)
#undef BASE_FLATTEN_EXT
static es2_hook_descriptor base_flatten_hook_descriptors[]={
    {ES2_HOOK_ID(base_flatten),base_flatten_sig,12,6,base_flatten_lens,3,1,base_flatten_base_flatten32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flood_sum),flood_sum_sig,14,6,flood_sum_lens,3,3,base_flatten_flood_sum32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flood_fill),flood_fill_sig,14,6,flood_fill_lens,3,5,base_flatten_flood_fill32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mark_raster),mark_raster_sig,14,6,mark_raster_lens,3,5,base_flatten_mark_raster32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(cell_flag_get),cell_flag_get_sig,12,6,cell_flag_get_lens,3,3,base_flatten_cell_flag_get32,NULL,NULL,NULL,0},
};
#endif
