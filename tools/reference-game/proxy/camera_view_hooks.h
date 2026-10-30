#ifndef ES2_CAMERA_VIEW_HOOKS_H
#define ES2_CAMERA_VIEW_HOOKS_H
/* camera_view group (docs/reverse-engineering/camera.md, section
 * "The hooks (group camera_view)"): the camera cull
 * camera_attached 401078 (the binning 408240, 42883c and the cell objects
 * 428c60), the camera setters camera_orbit_set 40109c and camera_attach_set
 * 40111c (view_offsets), the outside camera outside_camera 401c74
 * (player_input, death_sequence, the F12 dialog's idle callback 45cfc8)
 * with its steps camera_turn_step 401c20 and camera_speed_step 401c4c, the
 * view request view_apply 45de30 (logic_frame, death_sequence), the view
 * subject setter view_subject_set 45df34 (view_apply, death_sequence,
 * command_key), the view offsets view_offsets 45edb0 (view_subject_set,
 * the mission start 45d1xx and mission_root) and the camera getter
 * camera_current 45edf8 (core/src/camera_view.c). They run in the logic
 * frame (the camera position, the view subject 4D2684h and the view
 * word 4D24F0h are read by simulation hooks), except camera_attached and
 * camera_current, whose callers are drawing code.
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX: multiply_q8 47e0a0 and
 * approach_word 467adc (the steps), the steps 401c20/401c4c (outside
 * camera), view_subject_set and the label step 45e208 (view_apply), the
 * runtime record 49630c (synthetic EBP f = scratch + 28h through
 * weapons_impact_guest_call; the adapter restores FS:0 to the record's
 * saved head, EDX, as the original's epilogue does), the graphics camera
 * constructor 48bd84 and view_offsets (view_subject_set), the subject's
 * virtual slot +30h and the two setters (view_offsets, f = scratch + 10h).
 * The nested hooks keep their modes; 45e208, 48bd84 and 49630c stay game
 * code.
 *
 * EDX:EAX exact. ECX is dead after every call site (camera.md, "The hooks (group
 * camera_view)", "Exit ECX at the call sites"): the framework returns the entry ECX; the
 * cores report the original's ECX for the in-vitro comparison
 * (camera_orbit_set leaves a frame address there, reported as 0).
 * camera_attached and camera_current leave ECX unchanged. No entry or exit
 * shim: no frame local is read before it is written (the slot +30h
 * implementations 40351c, 411a74 and 4155c4 write both 6-byte records).
 * All ten refuse compare mode (guest calls, outputs behind pointers and in
 * fixed globals, the runtime record; the two pure readers with the group).
 * No game instruction is embedded. */
#include "es2/core/camera_view.h"
static uint64_t __cdecl camera_view_camera_attached(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_camera_attached_registers(&math_native_memory,a[0],a[1],r,NULL);
}
static uint64_t __cdecl camera_view_camera_orbit_set(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_camera_orbit_set_registers(&math_native_memory,a[0],a[1],a[2],r,NULL);
}
static uint64_t __cdecl camera_view_camera_attach_set(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_camera_attach_set_registers(&math_native_memory,a[0],a[1],a[2],r,NULL);
}
static uint64_t __cdecl camera_view_camera_turn_step(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_camera_turn_step);
    return es2_camera_turn_step_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl camera_view_camera_speed_step(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_camera_speed_step);
    return es2_camera_speed_step_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl camera_view_outside_camera(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_outside_camera);
    return es2_outside_camera_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,NULL);
}
static uint64_t __cdecl camera_view_view_apply(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_view_apply);
    return es2_view_apply_registers(&math_native_memory,&g,r,NULL);
}
static uint64_t __cdecl camera_view_view_subject_set(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_VIEW_SUBJECT_FRAME]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_view_subject_set);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_VIEW_SUBJECT_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    const uint64_t result=es2_view_subject_set_registers(&math_native_memory,&g,a[0],r,NULL);
    const uint32_t head=(uint32_t)(result>>32);es2_machine_set_fs0(head);
    return result;
}
static uint64_t __cdecl camera_view_view_offsets(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_VIEW_OFFSETS_FRAME]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_view_offsets);
    return es2_view_offsets_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl camera_view_camera_current(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;return es2_camera_current_registers(&math_native_memory,(uint32_t)(uintptr_t)u,r,NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the virtual slot. */
static callee_counter camera_turn_step_counters[]={
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
    {"call_467adc",ES2_COLLIDE_CALL,0x467adc,0},
};
static callee_counter camera_speed_step_counters[]={
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
    {"call_467adc",ES2_COLLIDE_CALL,0x467adc,0},
};
static callee_counter outside_camera_counters[]={
    {"call_401c20",ES2_COLLIDE_CALL,0x401c20,0},
    {"call_401c4c",ES2_COLLIDE_CALL,0x401c4c,0},
};
static callee_counter view_apply_counters[]={
    {"call_45df34",ES2_COLLIDE_CALL,0x45df34,0},
    {"call_45e208",ES2_COLLIDE_CALL,0x45e208,0},
};
static callee_counter view_subject_set_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_48bd84",ES2_COLLIDE_CALL,0x48bd84,0},
    {"call_45edb0",ES2_COLLIDE_CALL,0x45edb0,0},
};
static callee_counter view_offsets_counters[]={
    {"slot_30",ES2_COLLIDE_SLOT,0x30,0},
    {"call_40111c",ES2_COLLIDE_CALL,0x40111c,0},
    {"call_40109c",ES2_COLLIDE_CALL,0x40109c,0},
};
static callee_counter *camera_view_hook_counters(unsigned hook,unsigned *count) {
#define CAMERA_VIEW_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
    CAMERA_VIEW_COUNTERS(camera_turn_step) CAMERA_VIEW_COUNTERS(camera_speed_step) CAMERA_VIEW_COUNTERS(outside_camera)
    CAMERA_VIEW_COUNTERS(view_apply) CAMERA_VIEW_COUNTERS(view_subject_set) CAMERA_VIEW_COUNTERS(view_offsets)
#undef CAMERA_VIEW_COUNTERS
    *count=0;return NULL;
}
/* Prologues (relocated bytes, instruction lengths):
 * 401078, 40111c: push ebp; mov ebp,esp; mov eax,[ebp+8], 6 bytes (1,2,3);
 * 40109c, 45df34, 45edb0: push ebp; mov ebp,esp; add esp,-N (83 c4 ib),
 * 6 bytes (1,2,3);
 * 401c20: push ebp; mov ebp,esp; push ebx; push 0C0h (68 imm32), 9 bytes
 * (1,2,1,5);
 * 401c4c: push ebp; mov ebp,esp; push ebx; push 50h (6a ib), 6 bytes
 * (1,2,1,2);
 * 401c74: push ebp; mov ebp,esp; push ebx; push esi, 5 bytes (1,2,1,1);
 * 45de30, 45edf8: mov eax,[moffs32] (a1), 5 bytes (5); 45edf8's RET stays in
 * place. */
static const BYTE camera_attached_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x83,0x78,0x36,0x02};
static const BYTE camera_orbit_set_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0x8b,0x45,0x08,0x83,0x78,0x4a,0x00};
static const BYTE camera_attach_set_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x83,0x78,0x4a,0x00};
static const BYTE camera_turn_step_sig[]={0x55,0x8b,0xec,0x53,0x68,0xc0,0x00,0x00,0x00,0x0f,0xbf,0x5d,0x0c};
static const BYTE camera_speed_step_sig[]={0x55,0x8b,0xec,0x53,0x6a,0x50,0x0f,0xbf,0x5d,0x0c};
static const BYTE outside_camera_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE view_apply_sig[]={0xa1,0x9c,0xf9,0x4c,0x00,0x53,0xbb,0xbc,0x24,0x4d,0x00};
static const BYTE view_subject_set_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0x53,0x56,0xb8,0xfd,0xf1,0x49,0x00};
static const BYTE view_offsets_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0x53,0x8b,0x5d,0x08};
static const BYTE camera_current_sig[]={0xa1,0xea,0x24,0x4d,0x00,0xc3};
static const BYTE camera_view_lens[]={1,2,3},camera_turn_step_lens[]={1,2,1,5},camera_speed_step_lens[]={1,2,1,2},
    outside_camera_lens[]={1,2,1,1},camera_view_load_lens[]={5};
#define CAMERA_VIEW_EXTENSION(name) \
static const es2_hook_extension camera_view_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0, \
    camera_view_##name,TRUE}; \
static uint32_t __cdecl camera_view_##name##32(const uint32_t *a,void *u) { \
    return (uint32_t)camera_view_##name(a,u,weapons_no_registers); }
CAMERA_VIEW_EXTENSION(camera_attached)
CAMERA_VIEW_EXTENSION(camera_orbit_set)
CAMERA_VIEW_EXTENSION(camera_attach_set)
CAMERA_VIEW_EXTENSION(camera_turn_step)
CAMERA_VIEW_EXTENSION(camera_speed_step)
CAMERA_VIEW_EXTENSION(outside_camera)
CAMERA_VIEW_EXTENSION(view_apply)
CAMERA_VIEW_EXTENSION(view_subject_set)
CAMERA_VIEW_EXTENSION(view_offsets)
CAMERA_VIEW_EXTENSION(camera_current)
#undef CAMERA_VIEW_EXTENSION
static es2_hook_descriptor camera_view_hook_descriptors[]={
    {ES2_HOOK_ID(camera_attached),camera_attached_sig,11,6,camera_view_lens,3,2,camera_view_camera_attached32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(camera_orbit_set),camera_orbit_set_sig,13,6,camera_view_lens,3,3,camera_view_camera_orbit_set32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(camera_attach_set),camera_attach_set_sig,10,6,camera_view_lens,3,3,camera_view_camera_attach_set32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(camera_turn_step),camera_turn_step_sig,13,9,camera_turn_step_lens,4,2,camera_view_camera_turn_step32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(camera_speed_step),camera_speed_step_sig,10,6,camera_speed_step_lens,4,2,camera_view_camera_speed_step32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(outside_camera),outside_camera_sig,9,5,outside_camera_lens,4,4,camera_view_outside_camera32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(view_apply),view_apply_sig,11,5,camera_view_load_lens,1,0,camera_view_view_apply32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(view_subject_set),view_subject_set_sig,13,6,camera_view_lens,3,1,camera_view_view_subject_set32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(view_offsets),view_offsets_sig,10,6,camera_view_lens,3,1,camera_view_view_offsets32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(camera_current),camera_current_sig,6,5,camera_view_load_lens,1,0,camera_view_camera_current32,NULL,NULL,NULL,0},
};
#endif
