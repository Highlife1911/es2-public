#ifndef ES2_PANEL_WEAPONS_HOOKS_H
#define ES2_PANEL_WEAPONS_HOOKS_H
/* panel_weapons group (docs/reverse-engineering/hud.md, section
 * "panel weapon selection as own code"): weapon_select 410708,
 * weapon_find 4106ac and panel_weapon 432a50. Game calls go through the
 * weapons guest bridge and callee observer (weapons_graph) with the
 * original's entry EAX/EDX/ECX: panel_weapon calls 4106ac, weapon_find the
 * weapon virtual +58h and 410708, so the nested hooks keep their modes.
 * All three return EDX:EAX and ECX exactly: ECX is live after 410708 (into
 * 410970 at 410c6d) and escapes through 432a50's callers (the MFD gadget
 * methods 440f18/4414dc, the HUD key handler 432bc8). The framework returns
 * the entry ECX, so each hook has an exit shim: it calls the framework's
 * dispatch with the two arguments copied and, if the replacement ran,
 * loads the ECX the core computed (the sfx pattern); EAX and EDX pass
 * through. No entry register other than EDX/ECX
 * passed on to the callees is read, no uninitialized local. Refuses compare
 * mode. No game instruction is embedded; the shim only marshals
 * registers. */
#include "es2/core/panel_weapons.h"
/* Set by the adapters, consumed by the shims; a nested hook sets and
 * consumes its own pair first. */
#define PANEL_WEAPONS_EXIT(name) __attribute__((used)) static uint32_t panel_weapons_##name##_ecx,panel_weapons_##name##_ecx_valid;
PANEL_WEAPONS_EXIT(select) PANEL_WEAPONS_EXIT(find) PANEL_WEAPONS_EXIT(panel)
#undef PANEL_WEAPONS_EXIT
static uint64_t __cdecl panel_weapons_weapon_select(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;uint32_t ecx;
    uint64_t v=es2_weapon_select_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],a[1],&ecx);
    panel_weapons_select_ecx=ecx;panel_weapons_select_ecx_valid=1;return v;
}
static uint64_t __cdecl panel_weapons_weapon_find(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_weapon_find);uint32_t ecx;
    uint64_t v=es2_weapon_find_registers(&math_native_memory,&g,a[0],a[1],r,&ecx);
    panel_weapons_find_ecx=ecx;panel_weapons_find_ecx_valid=1;return v;
}
static uint64_t __cdecl panel_weapons_panel_weapon(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_panel_weapon);uint32_t ecx;
    uint64_t v=es2_panel_weapon_registers(&math_native_memory,&g,a[0],a[1],r,&ecx);
    panel_weapons_panel_ecx=ecx;panel_weapons_panel_ecx_valid=1;return v;
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. */
static callee_counter weapon_find_counters[]={
    {"slot_58",ES2_COLLIDE_SLOT,0x58,0},
    {"call_410708",ES2_COLLIDE_CALL,0x410708,0},
};
static callee_counter panel_weapon_counters[]={
    {"call_4106ac",ES2_COLLIDE_CALL,0x4106ac,0},
};
static callee_counter *panel_weapons_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_weapon_find) { *count=sizeof(weapon_find_counters)/sizeof(weapon_find_counters[0]);return weapon_find_counters; }
    if(hook==ES2_HOOK_INDEX_panel_weapon) { *count=sizeof(panel_weapon_counters)/sizeof(panel_weapon_counters[0]);return panel_weapon_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ebx; push esi; then mov edx,[ebp+0Ch] /
 * push edi; mov esi,[ebp+8] / mov ebx,[ebp+8]. */
static const BYTE panel_weapons_select_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x55,0x0c};
static const BYTE panel_weapons_find_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x75,0x08};
static const BYTE panel_weapons_panel_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08};
static const BYTE panel_weapons_lens[]={1,2,1,1};
#define PANEL_WEAPONS_EXT(name) \
    static const es2_hook_extension panel_weapons_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,panel_weapons_##name,TRUE}; \
    static uint32_t __cdecl panel_weapons_##name##32(const uint32_t *a,void *u) {return (uint32_t)panel_weapons_##name(a,u,weapons_no_registers);}
PANEL_WEAPONS_EXT(weapon_select)
PANEL_WEAPONS_EXT(weapon_find)
PANEL_WEAPONS_EXT(panel_weapon)
#undef PANEL_WEAPONS_EXT
static es2_hook_descriptor panel_weapons_hook_descriptors[]={
    {ES2_HOOK_ID(weapon_select),panel_weapons_select_sig,8,5,panel_weapons_lens,4,2,panel_weapons_weapon_select32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(weapon_find),panel_weapons_find_sig,9,5,panel_weapons_lens,4,2,panel_weapons_weapon_find32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(panel_weapon),panel_weapons_panel_sig,8,5,panel_weapons_lens,4,2,panel_weapons_panel_weapon32,NULL,NULL,NULL,0},
};
/* The exit shims (shim_exit_call) with the two arguments copied. */
#define PANEL_WEAPONS_SHIM(name,rva) \
    static int panel_weapons_##name##_entry(es2_machine *m,uint32_t bridge) { \
        return shim_exit_call(m,bridge,2,&panel_weapons_##name##_ecx,&panel_weapons_##name##_ecx_valid); } \
    ES2_SHIM(panel_weapons_##name##_shim,rva,panel_weapons_##name##_entry,0);
PANEL_WEAPONS_SHIM(select,0x10708u) PANEL_WEAPONS_SHIM(find,0x106acu) PANEL_WEAPONS_SHIM(panel,0x32a50u)
#undef PANEL_WEAPONS_SHIM
#endif
