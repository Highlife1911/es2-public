#ifndef ES2_HUD_HELPERS_HOOKS_H
#define ES2_HUD_HELPERS_HOOKS_H
/* hud_helpers group (docs/reverse-engineering/hud.md, section
 * "HUD-region helpers"): panel_visible 431410, the test whether a
 * rectangle's top-left or bottom-right corner lies in the cockpit canvas
 * view, called by 34 draw functions (38 sites). It reads the cockpit
 * pointer 49b088 and the dwords 4d253e/4d2542/4d2556/4d255a through the
 * image base in the descriptor's user pointer and writes nothing. It calls
 * no game code (no graph, no counters). EDX:EAX are exact; the panel-off
 * path keeps the entry EDX, taken from the register context. ECX and the
 * flags are written before any read at all 38 call sites (listing scan,
 * hud.md), so they are not reproduced. No uninitialized local is read, so
 * no entry shim. Compare mode is refused like the other context hooks. No
 * game instruction is embedded. */
#include "es2/core/hud_helpers.h"
static uint64_t __cdecl hud_helpers_panel_visible(const uint32_t *a,void *u,const uint32_t *r) {
    return es2_panel_visible_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],r[1]);
}
static callee_counter *hud_helpers_hook_counters(unsigned hook,unsigned *count) {
    (void)hook;*count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-20h; mov eax,[49b088h] */
static const BYTE hud_helpers_panel_visible_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe0,0xa1,0x88,0xb0,0x49,0x00};
static const BYTE hud_helpers_frame_lens[]={1,2,3};
static const es2_hook_extension hud_helpers_panel_visible_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,hud_helpers_panel_visible,TRUE};
static uint32_t __cdecl hud_helpers_panel_visible32(const uint32_t *a,void *u) {return (uint32_t)hud_helpers_panel_visible(a,u,weapons_no_registers);}
static es2_hook_descriptor hud_helpers_hook_descriptors[]={
    {ES2_HOOK_ID(panel_visible),hud_helpers_panel_visible_sig,11,6,hud_helpers_frame_lens,3,1,hud_helpers_panel_visible32,NULL,NULL,NULL,0},
};
#endif
