#ifndef ES2_WORLD_HOOKS_H
#define ES2_WORLD_HOOKS_H
/* Terrain draw pass helpers: poly_clip 472bcc, poly_spans 472b14,
 * view_rect 46d324, cell_uv 46bdf8 (docs/reverse-engineering/terrain.md).
 * Included after weapons_hooks.h: poly_spans calls the original scan
 * converter 4930c1 through the weapons guest bridge with its entry
 * registers and counts it (weapons_graph); the other three are leaves.
 * All four refuse compare mode (outputs behind pointers read from the
 * arguments; poly_spans translates its polygon in place). No entry shims:
 * no entry register or uninitialized local is read, and ECX at return is
 * dead after every caller. No game instruction is embedded. */
#include "es2/core/world.h"
static uint64_t __cdecl world_poly_clip(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_poly_clip_registers(&math_native_memory,a[0],a[1],a[2],a[3],a[4]);
}
static uint64_t __cdecl world_poly_spans(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_poly_spans);(void)r;
    return es2_poly_spans_registers(&math_native_memory,&g,a[0],a[1],a[2]);
}
static uint64_t __cdecl world_view_rect(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_view_rect_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],a[1]);
}
static uint64_t __cdecl world_cell_uv(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_cell_uv_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],a[1],a[2],a[3]);
}
static const BYTE world_poly_clip_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb0,0x33,0xd2,0x53,0x56,0x57};
static const BYTE world_poly_spans_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x57,0x8b,0x7d,0x08};
static const BYTE world_view_rect_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x53,0x56,0x57,0x8b,0x45,0x0c};
static const BYTE world_cell_uv_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x8b,0x45,0x14};
static const BYTE world_frame_lens[]={1,2,3};
#define WORLD_EXT(name) static const es2_hook_extension world_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,world_##name,TRUE};
WORLD_EXT(poly_clip) WORLD_EXT(poly_spans) WORLD_EXT(view_rect) WORLD_EXT(cell_uv)
#undef WORLD_EXT
#define WORLD_LEGACY(name) static uint32_t __cdecl world_##name##32(const uint32_t *a,void *u) {return (uint32_t)world_##name(a,u,weapons_no_registers);}
WORLD_LEGACY(poly_clip) WORLD_LEGACY(poly_spans) WORLD_LEGACY(view_rect) WORLD_LEGACY(cell_uv)
#undef WORLD_LEGACY
static es2_hook_descriptor world_hook_descriptors[]={
    {ES2_HOOK_ID(poly_clip),world_poly_clip_sig,11,6,world_frame_lens,3,5,world_poly_clip32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(poly_spans),world_poly_spans_sig,12,6,world_frame_lens,3,3,world_poly_spans32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(view_rect),world_view_rect_sig,12,6,world_frame_lens,3,2,world_view_rect32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(cell_uv),world_cell_uv_sig,11,6,world_frame_lens,3,4,world_cell_uv32,NULL,NULL,NULL,0},
};
#endif
