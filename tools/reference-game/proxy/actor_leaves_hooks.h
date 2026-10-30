#ifndef ES2_ACTOR_LEAVES_HOOKS_H
#define ES2_ACTOR_LEAVES_HOOKS_H
/* actor_leaves group (docs/reverse-engineering/vtables.md, section
 * "actor slot leaves"): the 33 hookable leaves of the actor tables
 * (ACTOR, BASE, RADAR_BASE, GUN_BASE, VEHICLE, LC_BASE, MECH, FLYER), one
 * hook per function body; the receiver's vtable still decides which body
 * runs. Core in core/src/actor_leaves.c.
 *
 * EDX:EAX exact. ECX is the framework's (the entry ECX); the originals that
 * change it (base_sight_radius, base_part_word, base_next_part,
 * mech_next_part, mech_condition, flyer_condition, flyer_target_scale)
 * have no caller that reads it before writing it (vtables.md, "Callers'
 * registers"). mech_condition and flyer_condition call part_condition
 * 40db2c, flyer_target_scale calls multiply_q10 47e0b0 through the weapons
 * guest bridge with the original's registers, so those nested hooks keep
 * their modes. No local is read before it is written: no shim.
 *
 * Compare: all 33 accept it. The readers have no state; actor_mark
 * declares the byte O+B1h (span of argument 0), the three part walks the
 * out dword (span of argument 2); actor_mark's EAX (O+92h) and
 * base_next_part's EDX (out) are compared relative to their span. The
 * callers' callees are pure (part_condition: IDIV over the part maxima,
 * faulting on a zero sum like the original in any non-original mode;
 * multiply_q10). No game instruction is embedded. */
#include "es2/core/actor_leaves.h"
#define ACTOR_LEAVES_CONST(name) \
static uint64_t __cdecl actor_leaves_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    (void)a;(void)u;uint32_t ecx;return es2_##name##_registers(r,&ecx); }
ACTOR_LEAVES_CONST(base_in_front)
ACTOR_LEAVES_CONST(base_decide)
ACTOR_LEAVES_CONST(actor_armour)
ACTOR_LEAVES_CONST(actor_target_scale)
ACTOR_LEAVES_CONST(actor_angle)
ACTOR_LEAVES_CONST(actor_order)
ACTOR_LEAVES_CONST(actor_keys)
ACTOR_LEAVES_CONST(actor_aim_node)
ACTOR_LEAVES_CONST(actor_radius)
ACTOR_LEAVES_CONST(actor_sight_radius)
ACTOR_LEAVES_CONST(actor_move)
ACTOR_LEAVES_CONST(actor_engage_roll)
ACTOR_LEAVES_CONST(actor_mount_word)
ACTOR_LEAVES_CONST(actor_part_word)
ACTOR_LEAVES_CONST(flyer_keys)
ACTOR_LEAVES_CONST(object_zero)
#undef ACTOR_LEAVES_CONST
#define ACTOR_LEAVES_READ1(name) \
static uint64_t __cdecl actor_leaves_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    (void)u;uint32_t ecx;return es2_##name##_registers(&math_native_memory,a[0],r,&ecx); }
ACTOR_LEAVES_READ1(gun_angle)
ACTOR_LEAVES_READ1(base_radius)
ACTOR_LEAVES_READ1(base_sight_radius)
ACTOR_LEAVES_READ1(mech_angle)
ACTOR_LEAVES_READ1(mech_radius)
ACTOR_LEAVES_READ1(mech_sight_radius)
ACTOR_LEAVES_READ1(actor_mark)
#undef ACTOR_LEAVES_READ1
#define ACTOR_LEAVES_READ2(name) \
static uint64_t __cdecl actor_leaves_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    (void)u;uint32_t ecx;return es2_##name##_registers(&math_native_memory,a[0],a[1],r,&ecx); }
ACTOR_LEAVES_READ2(base_part_word)
ACTOR_LEAVES_READ2(mech_part_word)
ACTOR_LEAVES_READ2(mech_mount_word)
ACTOR_LEAVES_READ2(actor_in_front)
#undef ACTOR_LEAVES_READ2
static uint64_t __cdecl actor_leaves_base_next_part(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;return es2_base_next_part_registers(&math_native_memory,a[0],a[1],a[2],r,&ecx);
}
static uint64_t __cdecl actor_leaves_mech_next_part(const uint32_t *a,void *u,const uint32_t *r) {
    uint32_t ecx;return es2_mech_next_part_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],a[1],a[2],r,&ecx);
}
static uint64_t __cdecl actor_leaves_actor_next_part(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;return es2_actor_next_part_registers(&math_native_memory,a[2],r,&ecx);
}
#define ACTOR_LEAVES_CALL(name) \
static uint64_t __cdecl actor_leaves_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name);uint32_t ecx; \
    return es2_##name##_registers(&math_native_memory,&g,a[0],r,&ecx); }
ACTOR_LEAVES_CALL(mech_condition)
ACTOR_LEAVES_CALL(flyer_condition)
ACTOR_LEAVES_CALL(flyer_target_scale)
#undef ACTOR_LEAVES_CALL
/* Callee counters (hk_<hook>_call_<va>), incremented only while the hook is
 * non-original; the other 30 hooks make no game calls. */
static callee_counter mech_condition_counters[]={{"call_40db2c",ES2_COLLIDE_CALL,0x40db2c,0}};
static callee_counter flyer_condition_counters[]={{"call_40db2c",ES2_COLLIDE_CALL,0x40db2c,0}};
static callee_counter flyer_target_scale_counters[]={{"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0}};
static callee_counter *actor_leaves_hook_counters(unsigned hook,unsigned *count) {
#define ACTOR_LEAVES_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    ACTOR_LEAVES_COUNTERS(mech_condition)
    ACTOR_LEAVES_COUNTERS(flyer_condition)
    ACTOR_LEAVES_COUNTERS(flyer_target_scale)
#undef ACTOR_LEAVES_COUNTERS
    *count=0;return NULL;
}
/* Compare: actor_mark's EAX is O+92h, 1Fh below the declared byte; the
 * private object of the replacement phase moves it with the clone. */
static BOOL __cdecl actor_leaves_mark_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    return v->original_edx==v->replacement_edx&&v->original[0][0]==v->replacement[0][0]&&
        v->original_result-(uint32_t)(uintptr_t)v->original[0]==v->replacement_result-(uint32_t)(uintptr_t)v->replacement[0];
}
/* base_next_part's EDX is the out pointer (the declared dword). */
static BOOL __cdecl actor_leaves_next_part_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    return v->original_result==v->replacement_result&&memcmp(v->original[0],v->replacement[0],4)==0&&
        v->original_edx==(uint32_t)(uintptr_t)v->original[0]&&v->replacement_edx==(uint32_t)(uintptr_t)v->replacement[0];
}
static const es2_hook_span actor_leaves_mark_span[]={{0,0xb1,1}};
static const es2_hook_span actor_leaves_out_span[]={{2,0,4}};
/* Verified entry bytes (reference DBSIM.EXE), through the RET where short. */
/* 403594: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov ax,[eax+20Fh]; pop ebp; ret */
static const BYTE gun_angle_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x8b,0x80,0x0f,0x02,0x00,0x00,0x5d,0xc3};
/* 4035a4: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov eax,[eax+1F2h]; mov ax,[eax+2Ah]; pop ebp; ret */
static const BYTE base_radius_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x80,0xf2,0x01,0x00,0x00,0x66,0x8b,0x40,0x2a,0x5d,0xc3};
/* 4035b8: push ebp; mov ebp,esp; push ebx; mov eax,[ebp+8]; mov edx,[eax+1F2h] */
static const BYTE base_sight_radius_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x08,0x8b,0x90,0xf2,0x01,0x00,0x00};
/* 40360c: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov eax,[eax+201h]; mov edx,[ebp+0Ch]; movsx ecx,word [eax+edx*2]; mov eax,ecx; pop ebp; ret */
static const BYTE base_part_word_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x80,0x01,0x02,0x00,0x00,0x8b,0x55,0x0c,0x0f,0xbf,0x0c,0x50,0x8b,0xc1,0x5d,0xc3};
/* 403624: push ebp; mov ebp,esp; push ebx; mov eax,[ebp+0Ch]; mov edx,[ebp+8]; test eax,eax */
static const BYTE base_next_part_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x0c,0x8b,0x55,0x08,0x85,0xc0};
/* 405308: push ebp; mov ebp,esp; mov ax,1; pop ebp; ret */
static const BYTE base_in_front_sig[]={0x55,0x8b,0xec,0x66,0xb8,0x01,0x00,0x5d,0xc3};
/* 406cfe: push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret */
static const BYTE base_decide_sig[]={0x55,0x8b,0xec,0x33,0xc0,0x5d,0xc3};
/* 411a4c: push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret */
static const BYTE actor_armour_sig[]={0x55,0x8b,0xec,0x33,0xc0,0x5d,0xc3};
/* 411a54: push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret */
static const BYTE actor_target_scale_sig[]={0x55,0x8b,0xec,0x33,0xc0,0x5d,0xc3};
/* 411a5c: push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret */
static const BYTE actor_angle_sig[]={0x55,0x8b,0xec,0x33,0xc0,0x5d,0xc3};
/* 411a64: push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret */
static const BYTE actor_order_sig[]={0x55,0x8b,0xec,0x33,0xc0,0x5d,0xc3};
/* 411a6c: push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret */
static const BYTE actor_keys_sig[]={0x55,0x8b,0xec,0x33,0xc0,0x5d,0xc3};
/* 411a9c: push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret */
static const BYTE actor_aim_node_sig[]={0x55,0x8b,0xec,0x33,0xc0,0x5d,0xc3};
/* 411aa4: push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret */
static const BYTE actor_radius_sig[]={0x55,0x8b,0xec,0x33,0xc0,0x5d,0xc3};
/* 411aac: push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret */
static const BYTE actor_sight_radius_sig[]={0x55,0x8b,0xec,0x33,0xc0,0x5d,0xc3};
/* 411ab4: push ebp; mov ebp,esp; mov ax,1; pop ebp; ret */
static const BYTE actor_move_sig[]={0x55,0x8b,0xec,0x66,0xb8,0x01,0x00,0x5d,0xc3};
/* 411ac0: push ebp; mov ebp,esp; mov ax,1; pop ebp; ret */
static const BYTE actor_engage_roll_sig[]={0x55,0x8b,0xec,0x66,0xb8,0x01,0x00,0x5d,0xc3};
/* 411acc: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov dx,[ebp+0Ch]; sub dx,[eax+10h]; add dx,3800h; cmp dx,7000h; setb al; and eax,1; pop ebp; ret */
static const BYTE actor_in_front_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x8b,0x55,0x0c,0x66,0x2b,0x50,0x10,0x66,0x81,0xc2,0x00,0x38,0x66,0x81,0xfa,0x00,0x70,0x0f,0x92,0xc0,0x83,0xe0,0x01,0x5d,0xc3};
/* 411b04: push ebp; mov ebp,esp; mov ax,1; pop ebp; ret */
static const BYTE actor_mount_word_sig[]={0x55,0x8b,0xec,0x66,0xb8,0x01,0x00,0x5d,0xc3};
/* 411b10: push ebp; mov ebp,esp; mov eax,1; pop ebp; ret */
static const BYTE actor_part_word_sig[]={0x55,0x8b,0xec,0xb8,0x01,0x00,0x00,0x00,0x5d,0xc3};
/* 411b1c: push ebp; mov ebp,esp; mov eax,[ebp+10h]; or edx,-1; mov [eax],edx; mov eax,edx; pop ebp; ret */
static const BYTE actor_next_part_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x10,0x83,0xca,0xff,0x89,0x10,0x8b,0xc2,0x5d,0xc3};
/* 415488: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov ax,[eax+298h]; pop ebp; ret */
static const BYTE mech_angle_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x8b,0x80,0x98,0x02,0x00,0x00,0x5d,0xc3};
/* 415504: push ebp; mov ebp,esp; mov eax,[ebp+8]; add eax,206h; push eax; call */
static const BYTE mech_condition_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x05,0x06,0x02,0x00,0x00,0x50,0xe8};
/* 415518: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov eax,[eax+1F2h]; mov ax,[eax+70h]; pop ebp; ret */
static const BYTE mech_radius_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x80,0xf2,0x01,0x00,0x00,0x66,0x8b,0x40,0x70,0x5d,0xc3};
/* 41552c: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov eax,[eax+1F2h]; mov ax,[eax+70h]; pop ebp; ret */
static const BYTE mech_sight_radius_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x80,0xf2,0x01,0x00,0x00,0x66,0x8b,0x40,0x70,0x5d,0xc3};
/* 415540: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov eax,[eax+20Eh]; mov edx,[ebp+0Ch]; movsx eax,word [eax+edx*2]; pop ebp; ret */
static const BYTE mech_part_word_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x80,0x0e,0x02,0x00,0x00,0x8b,0x55,0x0c,0x0f,0xbf,0x04,0x50,0x5d,0xc3};
/* 415558: push ebp; mov ebp,esp; push ebx; mov eax,[ebp+0Ch]; mov edx,[ebp+8]; mov ecx,[edx+20Eh] */
static const BYTE mech_next_part_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x0c,0x8b,0x55,0x08,0x8b,0x8a,0x0e,0x02,0x00,0x00};
/* 4155ac: push ebp; mov ebp,esp; mov eax,[ebp+8]; movsx edx,word [ebp+0Ch]; mov eax,[eax+202h]; mov ax,[eax+edx*2+0Ah]; pop ebp; ret */
static const BYTE mech_mount_word_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x0f,0xbf,0x55,0x0c,0x8b,0x80,0x02,0x02,0x00,0x00,0x66,0x8b,0x44,0x50,0x0a,0x5d,0xc3};
/* 421584: push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8]; movsx esi,word [ebx+233h] */
static const BYTE flyer_target_scale_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08,0x0f,0xbf,0xb3,0x33,0x02,0x00,0x00};
/* 4215c0: push ebp; mov ebp,esp; mov ax,[ebp+0Ch]; xor eax,eax; pop ebp; ret */
static const BYTE flyer_keys_sig[]={0x55,0x8b,0xec,0x66,0x8b,0x45,0x0c,0x33,0xc0,0x5d,0xc3};
/* 421e84: push ebp; mov ebp,esp; mov eax,[ebp+8]; add eax,200h; push eax; call */
static const BYTE flyer_condition_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x05,0x00,0x02,0x00,0x00,0x50,0xe8};
/* 42200c: push ebp; mov ebp,esp; mov eax,[ebp+8]; add eax,92h; mov byte [eax+1Fh],1; pop ebp; ret */
static const BYTE actor_mark_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x05,0x92,0x00,0x00,0x00,0xc6,0x40,0x1f,0x01,0x5d,0xc3};
/* 4786cb: push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret */
static const BYTE object_zero_sig[]={0x55,0x8b,0xec,0x33,0xc0,0x5d,0xc3};
/* Relocated prologues: push ebp; mov ebp,esp; then a load (3 bytes), XOR
 * EAX,EAX (2), MOV AX,imm16 or MOV AX,[EBP+0Ch] (4), MOV EAX,imm32 (5),
 * PUSH EBX + load (1+3) or PUSH EBX; PUSH ESI (1+1). */
static const BYTE actor_leaves_load_lens[]={1,2,3},actor_leaves_xor_lens[]={1,2,2},actor_leaves_word_lens[]={1,2,4},
    actor_leaves_imm_lens[]={1,2,5},actor_leaves_ebx_lens[]={1,2,1,3},actor_leaves_saved_lens[]={1,2,1,1};
#define ACTOR_LEAVES_EXT(name) \
    static const es2_hook_extension actor_leaves_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,actor_leaves_##name,FALSE}; \
    static uint32_t __cdecl actor_leaves_##name##32(const uint32_t *a,void *u) {return (uint32_t)actor_leaves_##name(a,u,weapons_no_registers);}
ACTOR_LEAVES_EXT(gun_angle)
ACTOR_LEAVES_EXT(base_radius)
ACTOR_LEAVES_EXT(base_sight_radius)
ACTOR_LEAVES_EXT(base_part_word)
ACTOR_LEAVES_EXT(base_next_part)
ACTOR_LEAVES_EXT(base_in_front)
ACTOR_LEAVES_EXT(base_decide)
ACTOR_LEAVES_EXT(actor_armour)
ACTOR_LEAVES_EXT(actor_target_scale)
ACTOR_LEAVES_EXT(actor_angle)
ACTOR_LEAVES_EXT(actor_order)
ACTOR_LEAVES_EXT(actor_keys)
ACTOR_LEAVES_EXT(actor_aim_node)
ACTOR_LEAVES_EXT(actor_radius)
ACTOR_LEAVES_EXT(actor_sight_radius)
ACTOR_LEAVES_EXT(actor_move)
ACTOR_LEAVES_EXT(actor_engage_roll)
ACTOR_LEAVES_EXT(actor_in_front)
ACTOR_LEAVES_EXT(actor_mount_word)
ACTOR_LEAVES_EXT(actor_part_word)
ACTOR_LEAVES_EXT(actor_next_part)
ACTOR_LEAVES_EXT(mech_angle)
ACTOR_LEAVES_EXT(mech_condition)
ACTOR_LEAVES_EXT(mech_radius)
ACTOR_LEAVES_EXT(mech_sight_radius)
ACTOR_LEAVES_EXT(mech_part_word)
ACTOR_LEAVES_EXT(mech_next_part)
ACTOR_LEAVES_EXT(mech_mount_word)
ACTOR_LEAVES_EXT(flyer_target_scale)
ACTOR_LEAVES_EXT(flyer_keys)
ACTOR_LEAVES_EXT(flyer_condition)
ACTOR_LEAVES_EXT(actor_mark)
ACTOR_LEAVES_EXT(object_zero)
#undef ACTOR_LEAVES_EXT
static es2_hook_descriptor actor_leaves_hook_descriptors[]={
    {ES2_HOOK_ID(gun_angle),gun_angle_sig,15,6,actor_leaves_load_lens,3,1,actor_leaves_gun_angle32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(base_radius),base_radius_sig,18,6,actor_leaves_load_lens,3,1,actor_leaves_base_radius32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(base_sight_radius),base_sight_radius_sig,13,7,actor_leaves_ebx_lens,4,1,actor_leaves_base_sight_radius32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(base_part_word),base_part_word_sig,23,6,actor_leaves_load_lens,3,2,actor_leaves_base_part_word32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(base_next_part),base_next_part_sig,12,7,actor_leaves_ebx_lens,4,3,actor_leaves_base_next_part32,actor_leaves_next_part_equal,NULL,actor_leaves_out_span,1},
    {ES2_HOOK_ID(base_in_front),base_in_front_sig,9,7,actor_leaves_word_lens,3,1,actor_leaves_base_in_front32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(base_decide),base_decide_sig,7,5,actor_leaves_xor_lens,3,1,actor_leaves_base_decide32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_armour),actor_armour_sig,7,5,actor_leaves_xor_lens,3,1,actor_leaves_actor_armour32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_target_scale),actor_target_scale_sig,7,5,actor_leaves_xor_lens,3,1,actor_leaves_actor_target_scale32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_angle),actor_angle_sig,7,5,actor_leaves_xor_lens,3,1,actor_leaves_actor_angle32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_order),actor_order_sig,7,5,actor_leaves_xor_lens,3,1,actor_leaves_actor_order32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_keys),actor_keys_sig,7,5,actor_leaves_xor_lens,3,1,actor_leaves_actor_keys32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_aim_node),actor_aim_node_sig,7,5,actor_leaves_xor_lens,3,1,actor_leaves_actor_aim_node32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_radius),actor_radius_sig,7,5,actor_leaves_xor_lens,3,1,actor_leaves_actor_radius32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_sight_radius),actor_sight_radius_sig,7,5,actor_leaves_xor_lens,3,1,actor_leaves_actor_sight_radius32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_move),actor_move_sig,9,7,actor_leaves_word_lens,3,1,actor_leaves_actor_move32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_engage_roll),actor_engage_roll_sig,9,7,actor_leaves_word_lens,3,1,actor_leaves_actor_engage_roll32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_in_front),actor_in_front_sig,32,6,actor_leaves_load_lens,3,2,actor_leaves_actor_in_front32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_mount_word),actor_mount_word_sig,9,7,actor_leaves_word_lens,3,1,actor_leaves_actor_mount_word32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_part_word),actor_part_word_sig,10,8,actor_leaves_imm_lens,3,1,actor_leaves_actor_part_word32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_next_part),actor_next_part_sig,15,6,actor_leaves_load_lens,3,3,actor_leaves_actor_next_part32,NULL,NULL,actor_leaves_out_span,1},
    {ES2_HOOK_ID(mech_angle),mech_angle_sig,15,6,actor_leaves_load_lens,3,1,actor_leaves_mech_angle32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_condition),mech_condition_sig,13,6,actor_leaves_load_lens,3,1,actor_leaves_mech_condition32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_radius),mech_radius_sig,18,6,actor_leaves_load_lens,3,1,actor_leaves_mech_radius32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_sight_radius),mech_sight_radius_sig,18,6,actor_leaves_load_lens,3,1,actor_leaves_mech_sight_radius32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_part_word),mech_part_word_sig,21,6,actor_leaves_load_lens,3,2,actor_leaves_mech_part_word32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_next_part),mech_next_part_sig,16,7,actor_leaves_ebx_lens,4,3,actor_leaves_mech_next_part32,NULL,NULL,actor_leaves_out_span,1},
    {ES2_HOOK_ID(mech_mount_word),mech_mount_word_sig,23,6,actor_leaves_load_lens,3,2,actor_leaves_mech_mount_word32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_target_scale),flyer_target_scale_sig,15,5,actor_leaves_saved_lens,4,1,actor_leaves_flyer_target_scale32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_keys),flyer_keys_sig,11,7,actor_leaves_word_lens,3,2,actor_leaves_flyer_keys32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_condition),flyer_condition_sig,13,6,actor_leaves_load_lens,3,1,actor_leaves_flyer_condition32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_mark),actor_mark_sig,17,6,actor_leaves_load_lens,3,1,actor_leaves_actor_mark32,actor_leaves_mark_equal,NULL,actor_leaves_mark_span,1},
    {ES2_HOOK_ID(object_zero),object_zero_sig,7,5,actor_leaves_xor_lens,3,1,actor_leaves_object_zero32,NULL,NULL,NULL,0},
};
#endif
