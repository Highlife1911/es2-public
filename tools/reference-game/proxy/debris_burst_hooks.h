#ifndef ES2_DEBRIS_BURST_HOOKS_H
#define ES2_DEBRIS_BURST_HOOKS_H
/* Debris burst (docs/reverse-engineering/hit_tests.md, section
 * "debris burst"): debris_table 4089bc (pure leaf), debris_burst
 * 408530, debris_pick 4086bc, debris_piece 408588, debris_init 408940,
 * debris_spin 408b74 and debris_spin_any 408bb4. Every game callee goes
 * through the weapons guest bridge and callee observer (weapons_graph) with
 * the original's registers, so the nested hooks (rng_bounded, rng_next,
 * pool_alloc, object_init, euler_matrix, matrix_angles, debris_heading and
 * this group's own functions) keep their modes; memset 49681a, the
 * rotation compose 47f058 and the runtime frame setup 49630c run as
 * original code. debris_piece's 49630c gets the synthetic EBP (scratch +
 * ES2_DEBRIS_FRAME) through weapons_impact_guest_call, and the adapter
 * restores FS:0 to its entry head, as the original's epilogue does. ECX
 * after each hook is dead at every caller (hit_tests.md). Six refuse
 * compare mode (they call game code); debris_table accepts it: its
 * only store is the word behind its argument (reduced by the list count
 * when it is not below it), declared as a 2-byte span; EAX is 4a9650h or
 * [4a964ch], EDX the count; default comparison. No entry shim, no game instruction embedded. */
#include "es2/core/debris_burst.h"
static uint64_t __cdecl debris_burst_debris_table(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_debris_table_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0]);
}
static uint64_t __cdecl debris_burst_debris_burst(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_DEBRIS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_debris_burst);
    return es2_debris_burst_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static uint64_t __cdecl debris_burst_debris_pick(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_debris_pick);
    return es2_debris_pick_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static uint64_t __cdecl debris_burst_debris_piece(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_DEBRIS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_debris_piece);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_DEBRIS_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint32_t head=es2_machine_fs0();
    uint64_t result=es2_debris_piece_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
    es2_machine_set_fs0(head);return result;
}
static uint64_t __cdecl debris_burst_debris_init(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_debris_init);
    return es2_debris_init_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r);
}
static uint64_t __cdecl debris_burst_debris_spin(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_debris_spin);
    return es2_debris_spin_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl debris_burst_debris_spin_any(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_debris_spin_any);
    return es2_debris_spin_any_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA. debris_table has no game calls. */
static callee_counter debris_burst_counters[]={
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"call_4086bc",ES2_COLLIDE_CALL,0x4086bc,0},
};
static callee_counter debris_pick_counters[]={
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"call_408588",ES2_COLLIDE_CALL,0x408588,0},
};
static callee_counter debris_piece_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_408940",ES2_COLLIDE_CALL,0x408940,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47f058",ES2_COLLIDE_CALL,0x47f058,0},
    {"call_47f9a0",ES2_COLLIDE_CALL,0x47f9a0,0},
    {"call_408b74",ES2_COLLIDE_CALL,0x408b74,0},
    {"call_408bb4",ES2_COLLIDE_CALL,0x408bb4,0},
};
static callee_counter debris_init_counters[]={
    {"call_402188",ES2_COLLIDE_CALL,0x402188,0},
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
};
static callee_counter debris_spin_counters[]={
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"call_4089e4",ES2_COLLIDE_CALL,0x4089e4,0},
};
static callee_counter debris_spin_any_counters[]={
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_408b74",ES2_COLLIDE_CALL,0x408b74,0},
};
static callee_counter *debris_burst_hook_counters(unsigned hook,unsigned *count) {
#define DEBRIS_BURST_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    DEBRIS_BURST_COUNTERS(debris_burst)
    DEBRIS_BURST_COUNTERS(debris_pick)
    DEBRIS_BURST_COUNTERS(debris_piece)
    DEBRIS_BURST_COUNTERS(debris_init)
    DEBRIS_BURST_COUNTERS(debris_spin)
    DEBRIS_BURST_COUNTERS(debris_spin_any)
#undef DEBRIS_BURST_COUNTERS
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ebx; mov ecx,[ebp+8]; mov eax,4a9650h */
static const BYTE debris_table_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x4d,0x08,0xb8,0x50,0x96,0x4a,0x00};
/* push ebp; mov ebp,esp; add esp,-20h; push ebx; mov ebx,[ebp+8] */
static const BYTE debris_burst_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe0,0x53,0x8b,0x5d,0x08};
/* push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi; mov esi,[ebp+8] */
static const BYTE debris_pick_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x75,0x08};
/* push ebp; mov ebp,esp; add esp,-5Ch; push ebx; push esi; push edi */
static const BYTE debris_piece_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xa4,0x53,0x56,0x57};
/* push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; mov eax,[ebp+0Ch] */
static const BYTE debris_init_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x8b,0x45,0x0c};
/* push ebp; mov ebp,esp; push ebx; push esi; mov si,[4a9664h] */
static const BYTE debris_spin_sig[]={0x55,0x8b,0xec,0x53,0x56,0x66,0x8b,0x35,0x64,0x96,0x4a,0x00};
/* push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; push 4d2599h */
static const BYTE debris_spin_any_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x68,0x99,0x25,0x4d,0x00};
static const BYTE debris_burst_frame_lens[]={1,2,3},debris_burst_push_lens[]={1,2,1,1},debris_burst_load_lens[]={1,2,1,3};
/* debris_table in compare mode: the word it may reduce. */
static const es2_hook_span debris_table_spans[]={{0,0,2}};
#define DEBRIS_BURST_EXT(name,refuse) \
    static const es2_hook_extension debris_burst_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,debris_burst_##name,refuse}; \
    static uint32_t __cdecl debris_burst_##name##32(const uint32_t *a,void *u) {return (uint32_t)debris_burst_##name(a,u,weapons_no_registers);}
DEBRIS_BURST_EXT(debris_table,FALSE)
DEBRIS_BURST_EXT(debris_burst,TRUE)
DEBRIS_BURST_EXT(debris_pick,TRUE)
DEBRIS_BURST_EXT(debris_piece,TRUE)
DEBRIS_BURST_EXT(debris_init,TRUE)
DEBRIS_BURST_EXT(debris_spin,TRUE)
DEBRIS_BURST_EXT(debris_spin_any,TRUE)
#undef DEBRIS_BURST_EXT
static es2_hook_descriptor debris_burst_hook_descriptors[]={
    {ES2_HOOK_ID(debris_table),debris_table_sig,12,7,debris_burst_load_lens,4,1,debris_burst_debris_table32,NULL,NULL,debris_table_spans,1},
    {ES2_HOOK_ID(debris_burst),debris_burst_sig,10,6,debris_burst_frame_lens,3,4,debris_burst_debris_burst32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(debris_pick),debris_pick_sig,10,5,debris_burst_push_lens,4,4,debris_burst_debris_pick32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(debris_piece),debris_piece_sig,9,6,debris_burst_frame_lens,3,4,debris_burst_debris_piece32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(debris_init),debris_init_sig,10,7,debris_burst_load_lens,4,5,debris_burst_debris_init32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(debris_spin),debris_spin_sig,12,5,debris_burst_push_lens,4,3,debris_burst_debris_spin32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(debris_spin_any),debris_spin_any_sig,12,7,debris_burst_load_lens,4,2,debris_burst_debris_spin_any32,NULL,NULL,NULL,0},
};
#endif
