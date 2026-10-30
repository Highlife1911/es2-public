#ifndef ES2_REMOVAL_HOOKS_H
#define ES2_REMOVAL_HOOKS_H
/* Deferred object removal: the flush 478644, pool_free 471bc8 and
 * the append 478628 (docs/reverse-engineering/main_loop.md, section
 * "object removal"). The flush's game calls go through the weapons
 * guest bridge and callee observer (weapons_graph) with the original's
 * registers: the runtime frame setup 49630c (synthetic EBP in a scratch,
 * ES2_REMOVAL_FRAME, through weapons_impact_guest_call), the runtime
 * 496406, the destructor slot +8 (original class code) and pool_free,
 * which keeps its own mode. The flush's adapter restores FS:0 to the
 * record's saved head (EDX), as the original's epilogue does. pool_free and
 * removal_append are leaves on guest memory. ECX after the three hooks is
 * dead at every caller (main_loop.md); EDX:EAX exact. Compare mode refused
 * (game calls; pool links and list items outside fixed spans). No entry
 * shim, no game instruction embedded. */
#include "es2/core/removal.h"
static uint64_t __cdecl removal_removal_flush(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_REMOVAL_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_removal_flush);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_REMOVAL_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;uint32_t ecx;
    uint64_t result=es2_removal_flush_registers(&math_native_memory,&g,a[0],a[1],r,&ecx);
    uint32_t head=(uint32_t)(result>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
static uint64_t __cdecl removal_pool_free(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;uint32_t ecx;return es2_pool_free_registers(&math_native_memory,a[0],a[1],&ecx);
}
static uint64_t __cdecl removal_removal_append(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;uint32_t ecx;return es2_removal_append_registers(&math_native_memory,a[0],a[1],&ecx);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook
 * is non-original. Keys: the callee VA, or the vtable offset for slots. */
static callee_counter removal_flush_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_496406",ES2_COLLIDE_CALL,0x496406,0},
    {"slot_08",ES2_COLLIDE_SLOT,0x08,0},
    {"call_471bc8",ES2_COLLIDE_CALL,0x471bc8,0},
};
static callee_counter *removal_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_removal_flush){*count=sizeof(removal_flush_counters)/sizeof(removal_flush_counters[0]);return removal_flush_counters;}
    *count=0;return NULL;
}
/* 478644: push ebp; mov ebp,esp; add esp,-24h; mov eax,4a1478h; push ebx; push esi; push edi */
static const BYTE removal_flush_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0xb8,0x78,0x14,0x4a,0x00,0x53,0x56,0x57};
static const BYTE removal_flush_lens[]={1,2,3};
/* 471bc8: push ebp; mov ebp,esp; push ebx; mov edx,[ebp+8]; dec word [edx+2] */
static const BYTE removal_free_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x55,0x08,0x66,0xff,0x4a,0x02};
static const BYTE removal_free_lens[]={1,2,1,3};
/* 478628: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov dx,[eax+4] */
static const BYTE removal_append_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x8b,0x50,0x04};
static const BYTE removal_append_lens[]={1,2,3};
#define REMOVAL_EXTENSION(name) \
static const es2_hook_extension removal_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0, \
    removal_##name,TRUE}; \
static uint32_t __cdecl removal_##name##32(const uint32_t *a,void *u) { \
    return (uint32_t)removal_##name(a,u,weapons_no_registers); }
REMOVAL_EXTENSION(removal_flush)
REMOVAL_EXTENSION(pool_free)
REMOVAL_EXTENSION(removal_append)
#undef REMOVAL_EXTENSION
static es2_hook_descriptor removal_hook_descriptors[]={
    {ES2_HOOK_ID(removal_flush),removal_flush_sig,14,6,removal_flush_lens,3,2,removal_removal_flush32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(pool_free),removal_free_sig,11,7,removal_free_lens,4,2,removal_pool_free32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(removal_append),removal_append_sig,10,6,removal_append_lens,3,2,removal_removal_append32,NULL,NULL,NULL,0},
};
#endif
