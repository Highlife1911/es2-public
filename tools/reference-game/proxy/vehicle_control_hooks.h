#ifndef ES2_VEHICLE_CONTROL_HOOKS_H
#define ES2_VEHICLE_CONTROL_HOOKS_H
/* Vehicle control group (docs/reverse-engineering/ai.md): part_ratio
 * 40dbc0, slope_speed 41693c, random_damage 417a04, vehicle_control 416a04.
 * Included after weapons_hooks.h: game callees go through its guest bridge
 * and callee observer (weapons_graph). part_ratio is a pure reader and
 * allows compare mode; random_damage and vehicle_control call game code
 * and refuse it. slope_speed accepts it: its callees
 * terrain_face and euler_matrix are pure apart from the matrix they are
 * given, and it writes A+12h..A+24h (through euler_matrix), the flag word
 * A+32h and the speed word, so spans A+12h..A+33h (the prefix holds the
 * angles A+0Ch and the position A+26h it hands on) and speed+0..1, and a
 * comparator: bytes, EDX, and EAX unless both EAX values are addresses in
 * the current thread's stack (the face-0 path returns the frame address
 * EBP-0Ch; the other paths return small values). None
 * reads an uninitialized local, so no entry shim. No game instruction is
 * embedded. */
#include "es2/core/vehicle_control.h"
#include "adapter_host.h"
static uint64_t __cdecl vehicle_part_ratio(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_part_ratio_registers(&math_native_memory,a[0],a[1]);
}
static uint64_t __cdecl vehicle_slope_speed(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[32]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_slope_speed);
    return es2_slope_speed_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl vehicle_random_damage(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_random_damage);
    return es2_random_damage_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl vehicle_vehicle_control(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[32]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_vehicle_control);
    return es2_vehicle_control_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static const BYTE vehicle_part_ratio_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0};
static const BYTE vehicle_slope_speed_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0};
static const BYTE vehicle_random_damage_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4};
static const BYTE vehicle_vehicle_control_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x5c,0xff,0xff,0xff};
static const BYTE vehicle_short_frame_lens[]={1,2,3};
static const BYTE vehicle_long_frame_lens[]={1,2,6};
static const es2_hook_extension vehicle_part_ratio_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,vehicle_part_ratio,FALSE};
static const es2_hook_extension vehicle_slope_speed_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,vehicle_slope_speed,FALSE};
#define VEHICLE_EXT(name) static const es2_hook_extension vehicle_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,vehicle_##name,TRUE};
VEHICLE_EXT(random_damage)
VEHICLE_EXT(vehicle_control)
#undef VEHICLE_EXT
#define VEHICLE_LEGACY(name) static uint32_t __cdecl vehicle_##name##32(const uint32_t *a,void *u) {return (uint32_t)vehicle_##name(a,u,weapons_no_registers);}
VEHICLE_LEGACY(part_ratio)
VEHICLE_LEGACY(slope_speed)
VEHICLE_LEGACY(random_damage)
VEHICLE_LEGACY(vehicle_control)
#undef VEHICLE_LEGACY
/* Compare: slope_speed's spans A+12h..A+33h and the speed word. */
static const es2_hook_span vehicle_slope_speed_spans[]={{0,0x12,0x22},{1,0,2}};
static BOOL __cdecl vehicle_slope_speed_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    for(unsigned i=0;i<v->span_count;++i)if(memcmp(v->original[i],v->replacement[i],v->sizes[i]))return FALSE;
    return v->original_edx==v->replacement_edx&&(v->original_result==v->replacement_result||
        (adapter_stack_address(v->original_result)&&adapter_stack_address(v->replacement_result)));
}
static es2_hook_descriptor vehicle_hook_descriptors[]={
    {ES2_HOOK_ID(part_ratio),vehicle_part_ratio_sig,6,6,vehicle_short_frame_lens,3,2,vehicle_part_ratio32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(slope_speed),vehicle_slope_speed_sig,6,6,vehicle_short_frame_lens,3,2,vehicle_slope_speed32,vehicle_slope_speed_equal,NULL,vehicle_slope_speed_spans,2},
    {ES2_HOOK_ID(random_damage),vehicle_random_damage_sig,6,6,vehicle_short_frame_lens,3,3,vehicle_random_damage32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(vehicle_control),vehicle_vehicle_control_sig,9,9,vehicle_long_frame_lens,3,4,vehicle_vehicle_control32,NULL,NULL,NULL,0},
};
#endif
