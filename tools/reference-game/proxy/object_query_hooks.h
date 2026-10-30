#ifndef ES2_OBJECT_QUERY_HOOKS_H
#define ES2_OBJECT_QUERY_HOOKS_H
/* Sweep receiver object query: object_query 427da8, grid_sweep 4273c8,
 * set_length 4927f0 (docs/reverse-engineering/weapons.md). Included after
 * weapons_hooks.h: the adapters reuse its guest-call bridge, graph context
 * and callee-counter observer. Every game callee (the matrix and distance
 * hooks, the cell query 427238, the two nested new hooks and the CW3220
 * memset) is entered through the bridge with the original entry registers,
 * so nested hooks keep their own modes. All three accept compare
 * mode (compare_mode.md, "spans and comparators"): object_query with spans on out
 * (8 bytes) and on the segment record (P+0..P+29h, written: the length
 * P+4), an RVA region for [4aaad0] and a comparator on AX and the bytes
 * (both callers test AX only; the high halves can be frame residue); the
 * objects' lazy matrix cache (word B+32h, B+12h..B+24h through euler_matrix)
 * is filled by the original phase, so the replacement phase finds it set
 * and writes no object. grid_sweep with a span on out+4..out+0Fh and a
 * comparator that remaps the high word of EAX = ((out+4) & FFFF0000h) | 1;
 * set_length with a span on V and a comparator remapping EAX = V (its
 * memset acts on the clone). Their game callees are pure or write only
 * the replacement's own scratch. No entry shim: no entry
 * register or uninitialized local is read. ECX at return is dead after every
 * caller. No game instruction is embedded. */
#include "es2/core/collision.h"
static uint64_t __cdecl object_query_object_query(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[0xa0]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_object_query);
    return es2_object_query_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static uint64_t __cdecl object_query_grid_sweep(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[0x50]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_grid_sweep);
    return es2_grid_sweep_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static uint64_t __cdecl object_query_set_length(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_set_length);(void)r;
    return es2_set_length_registers(&math_native_memory,&g,a[0],a[1]);
}
static const BYTE object_query_object_query_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x64,0xff,0xff,0xff,0x53,0x56,0x57};
static const BYTE object_query_grid_sweep_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb4,0x53,0x56,0x57};
static const BYTE object_query_set_length_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE object_query_long_lens[]={1,2,6},object_query_frame_lens[]={1,2,3},object_query_push_lens[]={1,2,1,1};
/* Compare declarations. object_query: [4aaad0] in place. */
static const es2_hook_region object_query_object_query_regions[]={{ES2_REGION_RVA,0,0xaaad0,0,0,4,FALSE,0,{0,0}}};
static const es2_hook_extension object_query_object_query_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,
    object_query_object_query_regions,1,object_query_object_query,FALSE};
#define OBJECT_QUERY_EXT(name) static const es2_hook_extension object_query_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,object_query_##name,FALSE};
OBJECT_QUERY_EXT(grid_sweep) OBJECT_QUERY_EXT(set_length)
#undef OBJECT_QUERY_EXT
static const es2_hook_span object_query_query_spans[]={{0,0,8},{1,0,0x2a}},object_query_sweep_spans[]={{1,4,12}},
    object_query_length_spans[]={{0,0,12}};
static int object_query_bytes_equal(const es2_hook_comparison *v) {
    for(unsigned i=0;i<v->span_count;++i)if(memcmp(v->original[i],v->replacement[i],v->sizes[i]))return 0;
    return 1;
}
static BOOL __cdecl object_query_query_equal(const es2_hook_comparison *v,void *u) {
    (void)u;return object_query_bytes_equal(v)&&(uint16_t)v->original_result==(uint16_t)v->replacement_result;
}
/* EAX on a hit is ((out+4) & FFFF0000h) | 1: its high word is remapped. */
static BOOL __cdecl object_query_sweep_equal(const es2_hook_comparison *v,void *u) {
    (void)u;const uint32_t o=(uint32_t)(uintptr_t)v->original[0]>>16,r=(uint32_t)(uintptr_t)v->replacement[0]>>16;
    const uint32_t x=v->original_result>>16,y=v->replacement_result>>16;
    return object_query_bytes_equal(v)&&(uint16_t)v->original_result==(uint16_t)v->replacement_result&&
        (x==y||(x==o&&y==r))&&v->original_edx==v->replacement_edx;
}
static BOOL __cdecl object_query_length_equal(const es2_hook_comparison *v,void *u) {
    (void)u;const uint32_t o=(uint32_t)(uintptr_t)v->original[0],r=(uint32_t)(uintptr_t)v->replacement[0];
    return object_query_bytes_equal(v)&&v->original_result-o==v->replacement_result-r&&v->original_edx==v->replacement_edx;
}
#define OBJECT_QUERY_LEGACY(name) static uint32_t __cdecl object_query_##name##32(const uint32_t *a,void *u) {return (uint32_t)object_query_##name(a,u,weapons_no_registers);}
OBJECT_QUERY_LEGACY(object_query) OBJECT_QUERY_LEGACY(grid_sweep) OBJECT_QUERY_LEGACY(set_length)
#undef OBJECT_QUERY_LEGACY
static es2_hook_descriptor object_query_hook_descriptors[]={
    {ES2_HOOK_ID(object_query),object_query_object_query_sig,12,9,object_query_long_lens,3,4,object_query_object_query32,object_query_query_equal,NULL,object_query_query_spans,2},
    {ES2_HOOK_ID(grid_sweep),object_query_grid_sweep_sig,9,6,object_query_frame_lens,3,4,object_query_grid_sweep32,object_query_sweep_equal,NULL,object_query_sweep_spans,1},
    {ES2_HOOK_ID(set_length),object_query_set_length_sig,9,5,object_query_push_lens,4,2,object_query_set_length32,object_query_length_equal,NULL,object_query_length_spans,1},
};
#endif
