#ifndef ES2_MOUNT_HOOKS_H
#define ES2_MOUNT_HOOKS_H
/* mount group (docs/reverse-engineering/player_movement.md, "trig leaves
 * 47eb30 and 47ffe8", and weapons.md, "weapon mount helper 40f540 and
 * 40f904"): the pure trig leaves sincos_pair 47eb30 and
 * rotate_2d 47ffe8, and the weapon mount helper mount_offset 40f540 with
 * mount_base 40f904. The leaves call no game code (no graph, no counters);
 * mount_offset calls 40f904 through the weapons guest bridge and callee
 * observer, so mount_base stays nested in its own mode. mount_offset and
 * mount_base refuse compare mode (static vector and guard byte, a game
 * call). The two leaves accept it: sincos_pair with an 8-byte
 * span on its matrix argument (default comparison: EAX = -s and EDX are
 * values), rotate_2d with an 8-byte span on out and a comparator that
 * remaps EAX = out to the clone (an out that v or m starts inside is
 * skipped by the framework; no caller overlaps, compare_mode.md, "spans
 * and comparators"). No entry register is read and no entry shim is
 * needed; ECX at return is dead at every caller (walks in the docs). No
 * game instruction is embedded. */
#include "es2/core/mount.h"
static uint64_t __cdecl mount_sincos_pair(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_sincos_pair_registers(&math_native_memory,a[0],a[1],(const BYTE *)u+0xa25c4);
}
static uint64_t __cdecl mount_rotate_2d(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_rotate_2d_registers(&math_native_memory,a[0],a[1],a[2]);
}
static uint64_t __cdecl mount_mount_base(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_mount_base_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],a[1]);
}
static uint64_t __cdecl mount_mount_offset(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_mount_offset);
    return es2_mount_offset_registers(&math_native_memory,&g,a[0],r);
}
static callee_counter mount_offset_counters[]={
    {"call_40f904",ES2_COLLIDE_CALL,0x40f904,0},
};
static callee_counter *mount_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_mount_offset) { *count=sizeof(mount_offset_counters)/sizeof(mount_offset_counters[0]);return mount_offset_counters; }
    *count=0;return NULL;
}
static const BYTE mount_sincos_sig[]={0xc8,0x00,0x00,0x00,0x53,0x66,0x8b,0x4d,0x08};
static const BYTE mount_rotate_sig[]={0xc8,0x00,0x00,0x00,0x56,0x57,0x53,0x8b,0x5d,0x08};
static const BYTE mount_offset_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08};
static const BYTE mount_base_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08};
static const BYTE mount_enter_lens[]={4,1},mount_push_lens[]={1,2,1,3},mount_frame_lens[]={1,2,3};
#define MOUNT_EXT_FLAG(name,unsupported) \
    static const es2_hook_extension mount_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,mount_##name,unsupported}; \
    static uint32_t __cdecl mount_##name##32(const uint32_t *a,void *u) {return (uint32_t)mount_##name(a,u,weapons_no_registers);}
#define MOUNT_EXT(name) MOUNT_EXT_FLAG(name,TRUE)
MOUNT_EXT_FLAG(sincos_pair,FALSE)
MOUNT_EXT_FLAG(rotate_2d,FALSE)
MOUNT_EXT(mount_offset)
MOUNT_EXT(mount_base)
#undef MOUNT_EXT
#undef MOUNT_EXT_FLAG
/* Compare: sincos_pair writes m+0..m+7, rotate_2d out+0..out+7.
 * rotate_2d's EAX is out (the clone in the replacement phase), its EDX the
 * high dword of the second row sum. */
static const es2_hook_span mount_sincos_pair_spans[]={{1,0,8}},mount_rotate_2d_spans[]={{2,0,8}};
static BOOL __cdecl mount_rotate_2d_equal(const es2_hook_comparison *v,void *u) {
    (void)u;const uint32_t o=(uint32_t)(uintptr_t)v->original[0],r=(uint32_t)(uintptr_t)v->replacement[0];
    return !memcmp(v->original[0],v->replacement[0],8)&&v->original_result-o==v->replacement_result-r&&
        v->original_edx==v->replacement_edx;
}
static es2_hook_descriptor mount_hook_descriptors[]={
    {ES2_HOOK_ID(sincos_pair),mount_sincos_sig,9,5,mount_enter_lens,2,2,mount_sincos_pair32,NULL,NULL,mount_sincos_pair_spans,1},
    {ES2_HOOK_ID(rotate_2d),mount_rotate_sig,10,5,mount_enter_lens,2,3,mount_rotate_2d32,mount_rotate_2d_equal,NULL,mount_rotate_2d_spans,1},
    {ES2_HOOK_ID(mount_offset),mount_offset_sig,7,7,mount_push_lens,4,1,mount_mount_offset32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mount_base),mount_base_sig,6,6,mount_frame_lens,3,2,mount_mount_base32,NULL,NULL,NULL,0},
};
#endif
