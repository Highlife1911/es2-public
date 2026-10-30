#ifndef ES2_MAP_PICK_HOOKS_H
#define ES2_MAP_PICK_HOOKS_H
/* map_pick group (docs/reverse-engineering/cockpit.md, section
 * "map pick 44d888"): map_pick 44d888, the HDD map's pick of the
 * actor under the cursor, reached from the map's input slot 44c988
 * through 44d6e0 (order target) and 44d82c (wingman selection). Its only
 * callee, the rectangle helper 44d288, is computed in core, so the hook
 * calls no game code (no graph, no counters). It reads the display shift
 * words 4d253a/4d253c and the actor list 4a9af8/4a9afe through the image
 * base in the descriptor's user pointer and writes only map+374h..+37Fh.
 * EDX:EAX are exact (EDX can leave 44c988 through 44d82c); ECX is dead at
 * both callers (each returns to 44c988, which pops ECX at once); no entry
 * register and no uninitialized local is read, so no entry shim. Compare
 * mode is refused (actor records may overlap the stored point). No game
 * instruction is embedded. */
#include "es2/core/map_pick.h"
static uint64_t __cdecl map_pick_map_pick(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_map_pick_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],a[1]);
}
static callee_counter *map_pick_hook_counters(unsigned hook,unsigned *count) {
    (void)hook;*count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-34h; push ebx; push esi; push edi;
 * mov ebx,[ebp+8] */
static const BYTE map_pick_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xcc,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE map_pick_frame_lens[]={1,2,3};
static const es2_hook_extension map_pick_map_pick_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,map_pick_map_pick,TRUE};
static uint32_t __cdecl map_pick_map_pick32(const uint32_t *a,void *u) {return (uint32_t)map_pick_map_pick(a,u,weapons_no_registers);}
static es2_hook_descriptor map_pick_hook_descriptors[]={
    {ES2_HOOK_ID(map_pick),map_pick_sig,12,6,map_pick_frame_lens,3,2,map_pick_map_pick32,NULL,NULL,NULL,0},
};
#endif
