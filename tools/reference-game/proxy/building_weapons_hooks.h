#ifndef ES2_BUILDING_WEAPONS_HOOKS_H
#define ES2_BUILDING_WEAPONS_HOOKS_H
#include "es2/core/building_weapons.h"
static uint64_t __cdecl weapons_turret(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_turret);
    g.padding=(uint8_t)r[2];return es2_turret_registers(&math_native_memory,&g,a[0],r);
}
static uint32_t __cdecl weapons_turret32(const uint32_t *a,void *u) {
    const uint32_t r[3]={0};return (uint32_t)weapons_turret(a,u,r);
}
static const es2_hook_extension weapons_turret_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,weapons_turret,TRUE};
static const BYTE weapons_building_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x80};
static const BYTE weapons_building_lens[]={1,2,3};
static const es2_hook_span weapons_building_span[]={{0,0,0x220}};
/* 404100's shim: CL from the caller's residue at entry ESP-101. */
static int weapons_building_entry(es2_machine *m,uint32_t bridge) {
    (void)bridge;m->ecx=shim_low8(m->ecx,shim_r8(m->esp-101u));return 0;
}
ES2_SHIM(weapons_building_shim,0x4100u,weapons_building_entry,0);
#endif
