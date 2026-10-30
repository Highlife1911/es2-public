#ifndef ES2_WEAPON_METHODS_HOOKS_H
#define ES2_WEAPON_METHODS_HOOKS_H
/* weapon_methods group (docs/reverse-engineering/weapon_classes.md,
 * section "the weapon-class methods"): the bodies of the weapon
 * vtables' slots +2Ch ... +60h (core/src/weapon_classes.c). Each is reached
 * only through the vtables (gun_ammo, elf_step and the H3 body 40f3b8 make
 * the three direct calls), so the receiver's table decides which hook runs.
 *
 * EDX:EAX exact, except beam_ready's EDX high word on the path where the
 * original copies the caller's EBX (the adapter cannot see EBX and passes
 * 0; the callers use DX only). ECX is the framework's (the entry ECX); the
 * originals' ECX is dead at the weapon dispatch sites or reaches only the
 * high word of a later word slot (weapon_classes.md, "Registers the hooks
 * do not return"). No entry shim. Game calls (weapon_step 40ef94,
 * timer_word_step 467a48, beam_step 40f00c, the button latch 45b734) go
 * through the weapons guest bridge with the original's EAX/EDX/ECX, so
 * nested hooks keep their modes; gun_ammo computes its call of 40e62c in
 * core. The 15 hooks without calls or writes accept compare mode (no span);
 * so do beam_ready (comparator on EAX and DX) and the nine
 * writers (spans over W or the word behind p, elf_trigger's frame word
 * [[W+14h]] as a one-hop region; registers that hold W, W+3Bh, W+43h or p
 * compared relative to the span); the four callers refuse it (game calls).
 * No game instruction is embedded. */
#include "es2/core/weapon_classes.h"
#define WEAPON_METHODS_LEAF(name,expr) \
static uint64_t __cdecl weapon_methods_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    (void)a;(void)u;uint32_t ecx;return expr; }
WEAPON_METHODS_LEAF(equip_ready,es2_weapon_zero_registers(r,&ecx))
WEAPON_METHODS_LEAF(equip_trigger,es2_weapon_zero_registers(r,&ecx))
WEAPON_METHODS_LEAF(weapon_priority,es2_weapon_zero_registers(r,&ecx))
WEAPON_METHODS_LEAF(weapon_gauge_match,es2_weapon_zero_registers(r,&ecx))
WEAPON_METHODS_LEAF(weapon_spent,es2_weapon_one_registers(r,&ecx))
WEAPON_METHODS_LEAF(weapon_stock,es2_weapon_one_registers(r,&ecx))
WEAPON_METHODS_LEAF(weapon_trigger,es2_weapon_trigger_registers(&math_native_memory,a[0],a[1],r,&ecx))
WEAPON_METHODS_LEAF(beam_gauge_match,es2_gauge_match_registers(&math_native_memory,a[0],a[1],r,&ecx))
WEAPON_METHODS_LEAF(gun_gauge_match,es2_gauge_match_registers(&math_native_memory,a[0],a[1],r,&ecx))
WEAPON_METHODS_LEAF(beam_spent,es2_beam_spent_registers(&math_native_memory,a[0],r,&ecx))
WEAPON_METHODS_LEAF(gun_spent,es2_gun_spent_registers(&math_native_memory,a[0],r,&ecx))
WEAPON_METHODS_LEAF(gun_stock,es2_gun_stock_registers(&math_native_memory,a[0],r,&ecx))
WEAPON_METHODS_LEAF(beam_priority,es2_beam_priority_registers(&math_native_memory,a[0],r,&ecx))
WEAPON_METHODS_LEAF(gun_clear,es2_gun_clear_registers(&math_native_memory,a[0],r,&ecx))
WEAPON_METHODS_LEAF(beam_fill,es2_beam_fill_registers(&math_native_memory,a[0],r,&ecx))
WEAPON_METHODS_LEAF(beam_preset,es2_beam_preset_registers(&math_native_memory,a[0],r,&ecx))
WEAPON_METHODS_LEAF(beam_drain,es2_beam_drain_registers(&math_native_memory,a[0],r,&ecx))
WEAPON_METHODS_LEAF(beam_clear,es2_beam_clear_registers(&math_native_memory,a[0],r,&ecx))
WEAPON_METHODS_LEAF(weapon_ammo,es2_weapon_ammo_registers(&math_native_memory,a[0],a[1],r,&ecx))
WEAPON_METHODS_LEAF(gun_ammo,es2_gun_ammo_registers(&math_native_memory,a[0],a[1],r,&ecx))
WEAPON_METHODS_LEAF(gun_ready,es2_gun_ready_registers(&math_native_memory,a[0],r,&ecx))
/* The caller's EBX high word is not visible here: 0 (weapon_classes.md). */
WEAPON_METHODS_LEAF(beam_ready,es2_beam_ready_registers(&math_native_memory,a[0],r,0,&ecx))
WEAPON_METHODS_LEAF(elf_ready,es2_elf_ready_registers(&math_native_memory,a[0],r,&ecx))
WEAPON_METHODS_LEAF(elf_trigger,es2_elf_trigger_registers(&math_native_memory,a[0],a[1],r,&ecx))
WEAPON_METHODS_LEAF(beam_adjust,es2_beam_adjust_registers(&math_native_memory,a[0],a[1],r,&ecx))
#undef WEAPON_METHODS_LEAF
#define WEAPON_METHODS_GRAPH(name,expr) \
static uint64_t __cdecl weapon_methods_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name);uint32_t ecx;return expr; }
WEAPON_METHODS_GRAPH(laew_trigger,es2_laew_trigger_registers(&math_native_memory,&g,a[0],a[1],r,&ecx))
WEAPON_METHODS_GRAPH(beam_step,es2_beam_step_registers(&math_native_memory,&g,a[0],a[1],a[2],r,&ecx))
WEAPON_METHODS_GRAPH(turbo_step,es2_turbo_step_registers(&math_native_memory,&g,a[0],a[1],a[2],r,&ecx))
WEAPON_METHODS_GRAPH(elf_step,es2_elf_step_registers(&math_native_memory,&g,a[0],a[1],a[2],r,&ecx))
#undef WEAPON_METHODS_GRAPH
/* Callee counters (hk_<hook>_call_<va>), incremented only while the hook
 * is non-original. */
static callee_counter laew_trigger_counters[]={{"call_45b734",ES2_COLLIDE_CALL,0x45b734,0}};
static callee_counter beam_step_counters[]={{"call_40ef94",ES2_COLLIDE_CALL,0x40ef94,0}};
static callee_counter turbo_step_counters[]={{"call_40ef94",ES2_COLLIDE_CALL,0x40ef94,0}};
static callee_counter elf_step_counters[]={
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_40f00c",ES2_COLLIDE_CALL,0x40f00c,0},
};
#define WEAPON_METHODS_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
    *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
static callee_counter *weapon_methods_hook_counters(unsigned hook,unsigned *count) {
    WEAPON_METHODS_COUNTERS(laew_trigger) WEAPON_METHODS_COUNTERS(beam_step)
    WEAPON_METHODS_COUNTERS(turbo_step) WEAPON_METHODS_COUNTERS(elf_step)
    *count=0;return NULL;
}
#undef WEAPON_METHODS_COUNTERS
/* Verified entry bytes (reference DBSIM.EXE). */
/* 40ecd4, 40e678, 4111e2, 40df00: push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret */
static const BYTE weapon_methods_zero_sig[]={0x55,0x8b,0xec,0x33,0xc0,0x5d,0xc3};
/* 40f8a4, 4111e9: push ebp; mov ebp,esp; mov ax,1; pop ebp; ret */
static const BYTE weapon_methods_one_sig[]={0x55,0x8b,0xec,0x66,0xb8,0x01,0x00,0x5d,0xc3};
/* 40ecdc: push ebp; mov ebp,esp; push ebx; mov eax,[ebp+8] */
static const BYTE weapon_methods_beam_ready_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x08};
/* 40ed6c, 40eda0: mov eax,[ebp+8]; lea edx,[eax+43h] */
static const BYTE weapon_methods_gun_ready_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8d,0x50,0x43,0x80,0x7a,0x06};
static const BYTE weapon_methods_elf_ready_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8d,0x50,0x43,0x80,0x7a,0x06};
/* 40f8ad: mov eax,[ebp+0Ch]; movzx eax,byte [eax+0Dh]; pop ebp; ret */
static const BYTE weapon_methods_weapon_trigger_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x0c,0x0f,0xb6,0x40,0x0d,0x5d,0xc3};
/* 40e680: push esi; push edi; mov edi,[ebp+0Ch]; mov eax,[ebp+8] */
static const BYTE weapon_methods_elf_trigger_sig[]={0x55,0x8b,0xec,0x56,0x57,0x8b,0x7d,0x0c,0x8b,0x45,0x08,0x8b};
/* 40e71c: push ebx; push esi; push edi; mov edx,[ebp+0Ch]; mov eax,[ebp+8] */
static const BYTE weapon_methods_laew_trigger_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x55,0x0c,0x8b,0x45,0x08};
/* 40f00c: push ebx; push esi; push edi; xor eax,eax; mov ebx,[ebp+8] */
static const BYTE weapon_methods_beam_step_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x33,0xc0,0x8b,0x5d,0x08,0x8b};
/* 40f3d8: push ebx; push esi; push edi; mov esi,[ebp+0Ch]; mov ebx,[ebp+8] */
static const BYTE weapon_methods_elf_step_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x75,0x0c,0x8b,0x5d,0x08};
/* 40f0d0: push ebx; push esi; push edi; mov edi,[ebp+0Ch]; mov ebx,[ebp+8] */
static const BYTE weapon_methods_turbo_step_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x7d,0x0c,0x8b,0x5d,0x08};
/* 40f48c: mov dx,50h; cmp word [ebp+0Ch],0 */
static const BYTE weapon_methods_beam_adjust_sig[]={0x55,0x8b,0xec,0x66,0xba,0x50,0x00,0x66,0x83,0x7d,0x0c,0x00};
/* 40f4d8: mov eax,[ebp+8]; lea edx,[eax+43h]; cmp byte [edx+6],... */
static const BYTE weapon_methods_beam_preset_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8d,0x50,0x43,0x80,0x7a,0x06};
/* 40f4f0: mov eax,[ebp+8]; lea edx,[eax+43h]; mov byte [edx],1 */
static const BYTE weapon_methods_beam_fill_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8d,0x50,0x43,0xc6,0x02,0x01};
/* 4116ad, 4115f8: mov eax,[ebp+8]; mov word [eax+7Bh],0 */
static const BYTE weapon_methods_beam_drain_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0xc7,0x40,0x7b,0x00,0x00};
static const BYTE weapon_methods_gun_clear_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0xc7,0x40,0x7b,0x00,0x00};
/* 40f504: mov eax,[ebp+8]; lea edx,[eax+43h]; cmp byte [edx],0 */
static const BYTE weapon_methods_beam_priority_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8d,0x50,0x43,0x80,0x3a,0x00};
/* 4116cf: mov eax,[ebp+8]; xor edx,edx; mov [eax+7Dh],dx */
static const BYTE weapon_methods_beam_clear_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x33,0xd2,0x66,0x89,0x50,0x7d};
/* 40f520: mov eax,[ebp+8]; movsx edx,word [eax+7Bh]; mov eax,... */
static const BYTE weapon_methods_gun_stock_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x0f,0xbf,0x50,0x7b,0x8b,0x40};
/* 40df08, 40df1c: mov eax,[ebp+8]; mov edx,[ebp+0Ch]; cmp edx,[eax+77h] */
static const BYTE weapon_methods_gauge_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x55,0x0c,0x3b,0x50,0x77};
/* 40ed34: mov eax,[ebp+8]; lea edx,[eax+43h]; mov al,[edx+6] */
static const BYTE weapon_methods_beam_spent_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8d,0x50,0x43,0x8a,0x42,0x06};
/* 40ed48: mov eax,[ebp+8]; cmp word [eax+7Bh],0; je */
static const BYTE weapon_methods_gun_spent_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x83,0x78,0x7b,0x00,0x74};
/* 40e62c: mov eax,[ebp+0Ch]; test eax,eax; je +5; mov word ... */
static const BYTE weapon_methods_weapon_ammo_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x0c,0x85,0xc0,0x74,0x05,0x66,0xc7};
/* 40e644: mov edx,[ebp+0Ch]; mov eax,[ebp+8]; mov ecx,[eax+20h] */
static const BYTE weapon_methods_gun_ammo_sig[]={0x55,0x8b,0xec,0x8b,0x55,0x0c,0x8b,0x45,0x08,0x8b,0x48,0x20};
static const BYTE weapon_methods_xor_lens[]={1,2,2},weapon_methods_mov16_lens[]={1,2,4},
    weapon_methods_load_lens[]={1,2,3},weapon_methods_saved_lens[]={1,2,1,1},weapon_methods_ebx_lens[]={1,2,1,3};
/* Compare declarations (weapon_classes.md, "Compare mode"). Every
 * declared byte is compared; EAX/EDX that hold an address in the clone are
 * compared relative to the span. */
static BOOL weapon_methods_bytes_equal(const es2_hook_comparison *v) {
    for(unsigned i=0;i<v->span_count;++i)if(memcmp(v->original[i],v->replacement[i],v->sizes[i]))return FALSE;
    return TRUE;
}
static uint32_t weapon_methods_offset(uint32_t value,const BYTE *base) {return value-(uint32_t)(uintptr_t)base;}
static BOOL weapon_methods_eax_relative(const es2_hook_comparison *v) {
    return weapon_methods_offset(v->original_result,v->original[0])==weapon_methods_offset(v->replacement_result,v->replacement[0]);
}
/* gun_clear (W), beam_adjust (W+3Bh), beam_clear (W+3Bh): EAX relative,
 * EDX a value. */
static BOOL __cdecl weapon_methods_eax_span_equal(const es2_hook_comparison *v,void *u) {
    (void)u;return weapon_methods_bytes_equal(v)&&weapon_methods_eax_relative(v)&&v->original_edx==v->replacement_edx;
}
/* beam_fill, beam_preset (EAX W), beam_drain (EAX W+3Bh): EDX = W+43h. */
static BOOL __cdecl weapon_methods_pair_span_equal(const es2_hook_comparison *v,void *u) {
    (void)u;return weapon_methods_bytes_equal(v)&&weapon_methods_eax_relative(v)&&
        weapon_methods_offset(v->original_edx,v->original[0])==weapon_methods_offset(v->replacement_edx,v->replacement[0]);
}
/* weapon_ammo/gun_ammo: AX equal; the high word of EAX equal, or in each
 * phase p's high word (lo(p, 5)). */
static BOOL weapon_methods_ammo_eax(const es2_hook_comparison *v) {
    const uint32_t o=v->original_result,r=v->replacement_result;
    return (uint16_t)o==(uint16_t)r&&((o>>16)==(r>>16)||
        ((o>>16)==(uint32_t)(uintptr_t)v->original[0]>>16&&(r>>16)==(uint32_t)(uintptr_t)v->replacement[0]>>16));
}
static BOOL __cdecl weapon_methods_ammo_equal(const es2_hook_comparison *v,void *u) {
    (void)u;return weapon_methods_bytes_equal(v)&&weapon_methods_ammo_eax(v)&&v->original_edx==v->replacement_edx;
}
static BOOL __cdecl weapon_methods_gun_ammo_equal(const es2_hook_comparison *v,void *u) {
    (void)u;return weapon_methods_bytes_equal(v)&&weapon_methods_ammo_eax(v)&&
        weapon_methods_offset(v->original_edx,v->original[0])==weapon_methods_offset(v->replacement_edx,v->replacement[0]);
}
/* beam_ready: EAX and DX (the EDX high word is the caller's EBX's on one
 * path; the callers use DX only). */
static BOOL __cdecl weapon_methods_ready_equal(const es2_hook_comparison *v,void *u) {
    (void)u;return v->original_result==v->replacement_result&&(uint16_t)v->original_edx==(uint16_t)v->replacement_edx;
}
static const es2_hook_span weapon_methods_w7b_span[]={{0,0x7b,2}},weapon_methods_flags_span[]={{0,0x43,0x3a}},
    weapon_methods_w34_span[]={{0,0x34,0x49}},weapon_methods_w34c_span[]={{0,0x34,0x4b}},weapon_methods_p_span[]={{1,0,2}},
    weapon_methods_elf_span[]={{0,0x47,0x3f}};
/* elf_trigger: the frame word [[W+14h]] (written only when [W+10h] != 0;
 * a call with [W+14h] = 0 is skipped by the framework). */
static const es2_hook_region weapon_methods_elf_regions[]={{ES2_REGION_ARG_INDIRECT,0,0,0x14,0,2,FALSE,0,{0,0}}};
#define WEAPON_METHODS_EXT(name,unsupported) WEAPON_METHODS_EXT_REGIONS(name,NULL,0,unsupported)
#define WEAPON_METHODS_EXT_REGIONS(name,regions,count,unsupported) \
    static const es2_hook_extension weapon_methods_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,regions,count,weapon_methods_##name,unsupported}; \
    static uint32_t __cdecl weapon_methods_##name##32(const uint32_t *a,void *u) {return (uint32_t)weapon_methods_##name(a,u,weapons_no_registers);}
WEAPON_METHODS_EXT(equip_ready,FALSE)
WEAPON_METHODS_EXT(beam_ready,FALSE)
WEAPON_METHODS_EXT(gun_ready,FALSE)
WEAPON_METHODS_EXT(elf_ready,FALSE)
WEAPON_METHODS_EXT(weapon_trigger,FALSE)
WEAPON_METHODS_EXT_REGIONS(elf_trigger,weapon_methods_elf_regions,1,FALSE)
WEAPON_METHODS_EXT(laew_trigger,TRUE)
WEAPON_METHODS_EXT(equip_trigger,FALSE)
WEAPON_METHODS_EXT(beam_step,TRUE)
WEAPON_METHODS_EXT(elf_step,TRUE)
WEAPON_METHODS_EXT(turbo_step,TRUE)
WEAPON_METHODS_EXT(beam_adjust,FALSE)
WEAPON_METHODS_EXT(beam_preset,FALSE)
WEAPON_METHODS_EXT(beam_fill,FALSE)
WEAPON_METHODS_EXT(beam_drain,FALSE)
WEAPON_METHODS_EXT(weapon_priority,FALSE)
WEAPON_METHODS_EXT(beam_priority,FALSE)
WEAPON_METHODS_EXT(gun_clear,FALSE)
WEAPON_METHODS_EXT(beam_clear,FALSE)
WEAPON_METHODS_EXT(weapon_stock,FALSE)
WEAPON_METHODS_EXT(gun_stock,FALSE)
WEAPON_METHODS_EXT(weapon_gauge_match,FALSE)
WEAPON_METHODS_EXT(beam_gauge_match,FALSE)
WEAPON_METHODS_EXT(gun_gauge_match,FALSE)
WEAPON_METHODS_EXT(weapon_spent,FALSE)
WEAPON_METHODS_EXT(beam_spent,FALSE)
WEAPON_METHODS_EXT(gun_spent,FALSE)
WEAPON_METHODS_EXT(weapon_ammo,FALSE)
WEAPON_METHODS_EXT(gun_ammo,FALSE)
#undef WEAPON_METHODS_EXT
#undef WEAPON_METHODS_EXT_REGIONS
#define WEAPON_METHODS_DESCRIPTOR(name,sig,relocate,lens,args) \
    {ES2_HOOK_ID(name),weapon_methods_##sig##_sig,sizeof(weapon_methods_##sig##_sig),relocate,weapon_methods_##lens##_lens, \
     sizeof(weapon_methods_##lens##_lens),args,weapon_methods_##name##32,NULL,NULL,NULL,0}
#define WEAPON_METHODS_COMPARED(name,sig,relocate,lens,args,compare,spans,count) \
    {ES2_HOOK_ID(name),weapon_methods_##sig##_sig,sizeof(weapon_methods_##sig##_sig),relocate,weapon_methods_##lens##_lens, \
     sizeof(weapon_methods_##lens##_lens),args,weapon_methods_##name##32,compare,NULL,spans,count}
static es2_hook_descriptor weapon_methods_hook_descriptors[]={
    WEAPON_METHODS_DESCRIPTOR(equip_ready,zero,5,xor,1),
    WEAPON_METHODS_COMPARED(beam_ready,beam_ready,7,ebx,1,weapon_methods_ready_equal,NULL,0),
    WEAPON_METHODS_DESCRIPTOR(gun_ready,gun_ready,6,load,1),
    WEAPON_METHODS_DESCRIPTOR(elf_ready,elf_ready,6,load,1),
    WEAPON_METHODS_DESCRIPTOR(weapon_trigger,weapon_trigger,6,load,2),
    WEAPON_METHODS_COMPARED(elf_trigger,elf_trigger,5,saved,2,NULL,weapon_methods_elf_span,1),
    WEAPON_METHODS_DESCRIPTOR(laew_trigger,laew_trigger,5,saved,2),
    WEAPON_METHODS_DESCRIPTOR(equip_trigger,zero,5,xor,1),
    WEAPON_METHODS_DESCRIPTOR(beam_step,beam_step,5,saved,3),
    WEAPON_METHODS_DESCRIPTOR(elf_step,elf_step,5,saved,3),
    WEAPON_METHODS_DESCRIPTOR(turbo_step,turbo_step,5,saved,3),
    WEAPON_METHODS_COMPARED(beam_adjust,beam_adjust,7,mov16,2,weapon_methods_eax_span_equal,weapon_methods_w34_span,1),
    WEAPON_METHODS_COMPARED(beam_preset,beam_preset,6,load,1,weapon_methods_pair_span_equal,weapon_methods_flags_span,1),
    WEAPON_METHODS_COMPARED(beam_fill,beam_fill,6,load,1,weapon_methods_pair_span_equal,weapon_methods_flags_span,1),
    WEAPON_METHODS_COMPARED(beam_drain,beam_drain,6,load,1,weapon_methods_pair_span_equal,weapon_methods_w34_span,1),
    WEAPON_METHODS_DESCRIPTOR(weapon_priority,zero,5,xor,1),
    WEAPON_METHODS_DESCRIPTOR(beam_priority,beam_priority,6,load,1),
    WEAPON_METHODS_COMPARED(gun_clear,gun_clear,6,load,1,weapon_methods_eax_span_equal,weapon_methods_w7b_span,1),
    WEAPON_METHODS_COMPARED(beam_clear,beam_clear,6,load,1,weapon_methods_eax_span_equal,weapon_methods_w34c_span,1),
    WEAPON_METHODS_DESCRIPTOR(weapon_stock,one,7,mov16,1),
    WEAPON_METHODS_DESCRIPTOR(gun_stock,gun_stock,6,load,1),
    WEAPON_METHODS_DESCRIPTOR(weapon_gauge_match,zero,5,xor,1),
    WEAPON_METHODS_DESCRIPTOR(beam_gauge_match,gauge,6,load,2),
    WEAPON_METHODS_DESCRIPTOR(gun_gauge_match,gauge,6,load,2),
    WEAPON_METHODS_DESCRIPTOR(weapon_spent,one,7,mov16,1),
    WEAPON_METHODS_DESCRIPTOR(beam_spent,beam_spent,6,load,1),
    WEAPON_METHODS_DESCRIPTOR(gun_spent,gun_spent,6,load,1),
    WEAPON_METHODS_COMPARED(weapon_ammo,weapon_ammo,6,load,2,weapon_methods_ammo_equal,weapon_methods_p_span,1),
    WEAPON_METHODS_COMPARED(gun_ammo,gun_ammo,6,load,2,weapon_methods_gun_ammo_equal,weapon_methods_p_span,1),
};
#undef WEAPON_METHODS_DESCRIPTOR
#undef WEAPON_METHODS_COMPARED
#endif
