#ifndef ES2_WEAPON_SYNCS_HOOKS_H
#define ES2_WEAPON_SYNCS_HOOKS_H
/* weapon_syncs group (docs/reverse-engineering/weapon_classes.md,
 * section "the weapon gauge syncs, the LAEW emission and the empty
 * bodies"): the six
 * slot +50h bodies that weapon_frame 0x410cb6 calls once per weapon and
 * rendered frame (equip_sync 40f144 for EQUIP/TARG/SHIELD/ENERGY, ecm_sync
 * 40f184, turbo_sync 40f1f0, beam_sync 40f288 for BEAM/ELF, gun_sync 40f330,
 * laew_sync 40f3b8) and the slot +28h body of LAEW (laew_fire 40ea04), in
 * core/src/weapon_classes.c. The syncs move the weapon's state to and from
 * its gauge widget; the widget's slots (+18h read, +14h and +10h write) and
 * the gauge record getters stay original and are called through the weapons
 * guest bridge with the original's EAX/EDX/ECX (the widget slots at the
 * addresses read from the widget's table, so with no callee counter). The
 * original's local record is the adapter's 128-byte scratch.
 *
 * The six empty bodies of the slots +28h, +38h, +3Ch, +40h, +44h and +4Ch
 * (weapon_none_28 40e95c, weapon_none_38 4111ce, weapon_none_3c 4111d3,
 * weapon_none_40 4111d8, weapon_none_44 4111dd, weapon_none_4c 40f89f:
 * push ebp; mov ebp,esp; pop ebp; ret) are hookable because the framework
 * accepts a final RET; they change no register and no memory (the
 * core is es2_actor_empty_registers) and accept compare mode.
 *
 * The seven bodies above refuse compare mode (game calls). EDX:EAX exact; ECX is the
 * framework's (the weapon_frame loop reloads EAX/EDX/ECX after the call).
 * laew_fire needs the entry shim of weapon_fire: weapon preparation reads two
 * uninitialised matrix bytes of its caller's stack, 65 and 97 bytes below
 * the entry ESP of the original body (laew_fire and weapon_fire have the
 * same frame); the shim hands them over in CL/CH, and the adapter replays
 * them to preparation through weapons_prepare_stack_call. It is chained from
 * mfd_orders_capture_entry. */
#include "es2/core/weapon_classes.h"
#include "es2/core/actor_leaves.h"
#define WEAPON_SYNCS_ADAPTER(name,expr) \
static uint64_t __cdecl weapon_syncs_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##name);uint32_t ecx;(void)a; \
    return expr; }
WEAPON_SYNCS_ADAPTER(equip_sync,es2_equip_sync_registers(&math_native_memory,&g,a[0],r,&ecx))
WEAPON_SYNCS_ADAPTER(ecm_sync,es2_ecm_sync_registers(&math_native_memory,&g,a[0],r,&ecx))
WEAPON_SYNCS_ADAPTER(turbo_sync,es2_turbo_sync_registers(&math_native_memory,&g,a[0],r,&ecx))
WEAPON_SYNCS_ADAPTER(beam_sync,es2_beam_sync_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,&ecx))
WEAPON_SYNCS_ADAPTER(gun_sync,es2_gun_sync_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,&ecx))
WEAPON_SYNCS_ADAPTER(laew_sync,es2_laew_sync_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,&ecx))
#undef WEAPON_SYNCS_ADAPTER
#define WEAPON_SYNCS_EMPTY(name) static uint64_t __cdecl weapon_syncs_##name(const uint32_t *a,void *u,const uint32_t *r) {     (void)a;(void)u;uint32_t ecx;return es2_actor_empty_registers(r,&ecx); }
WEAPON_SYNCS_EMPTY(weapon_none_28)
WEAPON_SYNCS_EMPTY(weapon_none_38)
WEAPON_SYNCS_EMPTY(weapon_none_3c)
WEAPON_SYNCS_EMPTY(weapon_none_40)
WEAPON_SYNCS_EMPTY(weapon_none_44)
WEAPON_SYNCS_EMPTY(weapon_none_4c)
#undef WEAPON_SYNCS_EMPTY
/* laew_fire: preparation is called with the residue bytes the entry shim read. */
typedef struct weapon_syncs_fire_stack { uint32_t prepare,residue; } weapon_syncs_fire_stack;
static void weapon_syncs_fire_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const weapon_syncs_fire_stack *stack=context;
    if(f==stack->prepare && n==3)weapons_prepare_stack_call(f,a,r,stack->residue);
    else weapons_machine_call(f,a,n,r);
}
static uint64_t __cdecl weapon_syncs_laew_fire(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_laew_fire);
    weapon_syncs_fire_stack stack={(uint32_t)(uintptr_t)u+0xe788u,r[2]};uint32_t ecx;
    g.call=weapon_syncs_fire_call;g.context=&stack;
    return es2_laew_fire_registers(&math_native_memory,&g,a[0],a[1],a[2],r,&ecx);
}
/* Callee counters (hk_<hook>_call_<va>), incremented only while the hook is
 * non-original. The widget slots have no key. */
static callee_counter ecm_sync_counters[]={{"call_420a68",ES2_COLLIDE_CALL,0x420a68,0}};
static callee_counter turbo_sync_counters[]={{"call_462830",ES2_COLLIDE_CALL,0x462830,0}};
static callee_counter beam_sync_counters[]={{"call_440e9c",ES2_COLLIDE_CALL,0x440e9c,0}};
static callee_counter gun_sync_counters[]={
    {"call_4414cc",ES2_COLLIDE_CALL,0x4414cc,0},
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
};
static callee_counter laew_sync_counters[]={
    {"call_4414cc",ES2_COLLIDE_CALL,0x4414cc,0},
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
};
static callee_counter laew_fire_counters[]={
    {"call_40e788",ES2_COLLIDE_CALL,0x40e788,0},
    {"call_40acac",ES2_COLLIDE_CALL,0x40acac,0},
};
#define WEAPON_SYNCS_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
    *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
static callee_counter *weapon_syncs_hook_counters(unsigned hook,unsigned *count) {
    WEAPON_SYNCS_COUNTERS(ecm_sync) WEAPON_SYNCS_COUNTERS(turbo_sync) WEAPON_SYNCS_COUNTERS(beam_sync)
    WEAPON_SYNCS_COUNTERS(gun_sync) WEAPON_SYNCS_COUNTERS(laew_sync) WEAPON_SYNCS_COUNTERS(laew_fire)
    *count=0;return NULL;
}
#undef WEAPON_SYNCS_COUNTERS
/* Verified entry bytes (reference DBSIM.EXE): 40f144, 40f184, 40f1f0:
 * push ebp; mov ebp,esp; add esp,-8; push ebx; mov ebx,[ebp+8]; mov eax,[ebx+79h] */
static const BYTE weapon_syncs_widget_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x8b,0x5d,0x08,0x8b,0x43,0x79};
/* 40f288, 40f330: push ebp; mov ebp,esp; add esp,-18h; push ebx; push esi; push edi;
 * mov ebx,[ebp+8]; mov eax,[ebx+77h] */
static const BYTE weapon_syncs_record_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x53,0x56,0x57,0x8b,0x5d,0x08,0x8b,0x43,0x77};
/* 40f3b8: push ebp; mov ebp,esp; mov dx,[ebp+14h]; mov eax,[ebp+8] */
static const BYTE weapon_syncs_laew_sync_sig[]={0x55,0x8b,0xec,0x66,0x8b,0x55,0x14,0x8b,0x45,0x08};
/* 40ea04: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov edi,[ebp+10h];
 * mov esi,[ebp+0Ch] */
static const BYTE weapon_syncs_laew_fire_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x7d,0x10,0x8b,0x75,0x0c};
/* The six empty bodies: push ebp; mov ebp,esp; pop ebp; ret */
static const BYTE weapon_syncs_empty_sig[]={0x55,0x8b,0xec,0x5d,0xc3};
static const BYTE weapon_syncs_empty_lens[]={1,2,1,1};
static const BYTE weapon_syncs_frame_lens[]={1,2,3},weapon_syncs_mov16_lens[]={1,2,4},weapon_syncs_saved_lens[]={1,2,1,1};
#define WEAPON_SYNCS_EXT(name) \
    static const es2_hook_extension weapon_syncs_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,weapon_syncs_##name,TRUE}; \
    static uint32_t __cdecl weapon_syncs_##name##32(const uint32_t *a,void *u) {return (uint32_t)weapon_syncs_##name(a,u,weapons_no_registers);}
WEAPON_SYNCS_EXT(equip_sync)
WEAPON_SYNCS_EXT(ecm_sync)
WEAPON_SYNCS_EXT(turbo_sync)
WEAPON_SYNCS_EXT(beam_sync)
WEAPON_SYNCS_EXT(gun_sync)
WEAPON_SYNCS_EXT(laew_sync)
WEAPON_SYNCS_EXT(laew_fire)
#undef WEAPON_SYNCS_EXT
#define WEAPON_SYNCS_EMPTY_EXT(name)     static const es2_hook_extension weapon_syncs_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,weapon_syncs_##name,FALSE};     static uint32_t __cdecl weapon_syncs_##name##32(const uint32_t *a,void *u) {return (uint32_t)weapon_syncs_##name(a,u,weapons_no_registers);}
WEAPON_SYNCS_EMPTY_EXT(weapon_none_28)
WEAPON_SYNCS_EMPTY_EXT(weapon_none_38)
WEAPON_SYNCS_EMPTY_EXT(weapon_none_3c)
WEAPON_SYNCS_EMPTY_EXT(weapon_none_40)
WEAPON_SYNCS_EMPTY_EXT(weapon_none_44)
WEAPON_SYNCS_EMPTY_EXT(weapon_none_4c)
#undef WEAPON_SYNCS_EMPTY_EXT
#define WEAPON_SYNCS_DESCRIPTOR(name,sig,relocate,lens,args) \
    {ES2_HOOK_ID(name),weapon_syncs_##sig##_sig,sizeof(weapon_syncs_##sig##_sig),relocate,weapon_syncs_##lens##_lens, \
     sizeof(weapon_syncs_##lens##_lens),args,weapon_syncs_##name##32,NULL,NULL,NULL,0}
static es2_hook_descriptor weapon_syncs_hook_descriptors[]={
    WEAPON_SYNCS_DESCRIPTOR(equip_sync,widget,6,frame,1),
    WEAPON_SYNCS_DESCRIPTOR(ecm_sync,widget,6,frame,1),
    WEAPON_SYNCS_DESCRIPTOR(turbo_sync,widget,6,frame,1),
    WEAPON_SYNCS_DESCRIPTOR(beam_sync,record,6,frame,4),
    WEAPON_SYNCS_DESCRIPTOR(gun_sync,record,6,frame,4),
    WEAPON_SYNCS_DESCRIPTOR(laew_sync,laew_sync,7,mov16,4),
    WEAPON_SYNCS_DESCRIPTOR(laew_fire,laew_fire,5,saved,3),
    WEAPON_SYNCS_DESCRIPTOR(weapon_none_28,empty,5,empty,1),
    WEAPON_SYNCS_DESCRIPTOR(weapon_none_38,empty,5,empty,1),
    WEAPON_SYNCS_DESCRIPTOR(weapon_none_3c,empty,5,empty,1),
    WEAPON_SYNCS_DESCRIPTOR(weapon_none_40,empty,5,empty,1),
    WEAPON_SYNCS_DESCRIPTOR(weapon_none_44,empty,5,empty,1),
    WEAPON_SYNCS_DESCRIPTOR(weapon_none_4c,empty,5,empty,1),
};
#undef WEAPON_SYNCS_DESCRIPTOR
/* The entry shim of laew_fire, the same bytes as weapons_fire_entry: the two
 * residue bytes of the original frame in CL and CH. */
__attribute__((used)) static void *weapon_syncs_laew_fire_entry_bridge;
__attribute__((naked)) static void weapon_syncs_laew_fire_entry(void) {
    __asm__ volatile("movb -65(%esp),%cl\n\tmovb -97(%esp),%ch\n\tjmp *_weapon_syncs_laew_fire_entry_bridge\n\t");
}
/* Called by mfd_orders_capture_entry after the framework installed its
 * jump: redirect the jump of laew_fire to its shim and keep h->patch
 * consistent, so uninstall still validates the exact patch. The chain
 * continues in mission_root_capture_entry
 * (mission_root_hooks.h). */
static BOOL mission_root_capture_entry(es2_hook *h);
static BOOL weapon_syncs_capture_entry(es2_hook *h) {
    if(!h->installed)return TRUE;
    if(h->descriptor->rva!=0xea04u)return mission_root_capture_entry(h);
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    weapon_syncs_laew_fire_entry_bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),
        (uint32_t)(uintptr_t)weapon_syncs_laew_fire_entry-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
#endif
