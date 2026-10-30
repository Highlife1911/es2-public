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
__attribute__((used)) static void *weapons_building_entry_bridge;
__attribute__((naked)) static void weapons_building_entry(void) {
    __asm__ volatile("movb -101(%esp),%cl\n\tjmp *_weapons_building_entry_bridge\n\t");
}
#endif
