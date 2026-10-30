#ifndef ES2_MANAGER_SLOTS_HOOKS_H
#define ES2_MANAGER_SLOTS_HOOKS_H
/* manager_slots group (docs/reverse-engineering/main_loop.md,
 * section "the stage-5 flushes and the manager slot methods"): the
 * object base methods object_position 46b905 (slot +04) and shape_word
 * 46b910 (slot +10) of 22 class tables (core/src/fire.c), and the clip
 * methods of ANSequence (sequence_next 478760, sequence_prev 47877c,
 * sequence_reach_up 47878c, sequence_reach_down 4787b8: slots +20..+2C of
 * 4A684Ch) and ANCyclicSequence (cyclic_next 4787e4, cyclic_prev 478804,
 * cyclic_reach_up 478820, cyclic_reach_down 47882c: 4A6810h)
 * (core/src/animation.c). Each is the only entry of its slot in its
 * tables, so the receiver's vtable still decides which runs.
 *
 * Leaves: no game call, no memory write. EDX:EAX exact (478820/47882c keep
 * the entry EAX's high word, the others the entry EDX where the original
 * does); ECX is the framework's (the entry ECX): shape_word, sequence_next
 * and cyclic_next change ECX in the original, which every one-argument
 * caller of these slots writes before reading (main_loop.md). No shim. All
 * ten accept compare mode without a span (no writes). No game instruction
 * is embedded. */
#include "es2/core/fire.h"
#include "es2/core/animation.h"
static uint64_t __cdecl manager_slots_object_position(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;return es2_object_position_registers(a[0],r,&ecx);
}
static uint64_t __cdecl manager_slots_shape_word(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;return es2_shape_word_registers(&math_native_memory,a[0],r,&ecx);
}
#define MANAGER_SLOTS_CLIP(name,va) \
static uint64_t __cdecl manager_slots_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    (void)u;uint32_t ecx;const uint32_t args[3]={a[1],a[2],a[3]}; \
    return es2_clip_method_registers(&math_native_memory,va,a[0],args,r,&ecx); }
MANAGER_SLOTS_CLIP(sequence_next,0x478760u)
MANAGER_SLOTS_CLIP(sequence_prev,0x47877cu)
MANAGER_SLOTS_CLIP(sequence_reach_up,0x47878cu)
MANAGER_SLOTS_CLIP(sequence_reach_down,0x4787b8u)
MANAGER_SLOTS_CLIP(cyclic_next,0x4787e4u)
MANAGER_SLOTS_CLIP(cyclic_prev,0x478804u)
MANAGER_SLOTS_CLIP(cyclic_reach_up,0x478820u)
MANAGER_SLOTS_CLIP(cyclic_reach_down,0x47882cu)
#undef MANAGER_SLOTS_CLIP
/* No game calls: no callee counters. */
static callee_counter *manager_slots_hook_counters(unsigned hook,unsigned *count) {(void)hook;*count=0;return NULL;}
/* Verified entry bytes (reference DBSIM.EXE). */
/* 46b905: push ebp; mov ebp,esp; mov eax,[ebp+8]; add eax,26h; pop ebp; ret */
static const BYTE object_position_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x83,0xc0,0x26,0x5d,0xc3};
/* 46b910: push ebp; mov ebp,esp; mov eax,[ebp+8]; cmp dword [eax+34h],0; je */
static const BYTE shape_word_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x83,0x78,0x34,0x00,0x74,0x08};
/* 478760: push ebp; mov ebp,esp; mov eax,[ebp+0Ch]; mov edx,[ebp+8]; movsx ecx,ax */
static const BYTE sequence_next_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x0c,0x8b,0x55,0x08,0x0f,0xbf,0xc8};
/* 47877c: push ebp; mov ebp,esp; mov eax,[ebp+0Ch]; test ax,ax; je */
static const BYTE sequence_prev_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x0c,0x66,0x85,0xc0,0x74,0x03};
/* 47878c: push ebp; mov ebp,esp; mov edx,[ebp+10h]; mov eax,[ebp+0Ch]; cmp dx,ax; jle */
static const BYTE sequence_reach_up_sig[]={0x55,0x8b,0xec,0x8b,0x55,0x10,0x8b,0x45,0x0c,0x66,0x3b,0xd0,0x7e,0x06};
/* 4787b8: the same with jge */
static const BYTE sequence_reach_down_sig[]={0x55,0x8b,0xec,0x8b,0x55,0x10,0x8b,0x45,0x0c,0x66,0x3b,0xd0,0x7d,0x06};
/* 4787e4: push ebp; mov ebp,esp; mov edx,[ebp+0Ch]; mov eax,[ebp+8]; movsx ecx,dx */
static const BYTE cyclic_next_sig[]={0x55,0x8b,0xec,0x8b,0x55,0x0c,0x8b,0x45,0x08,0x0f,0xbf,0xca};
/* 478804: push ebp; mov ebp,esp; mov edx,[ebp+0Ch]; mov eax,[ebp+8]; test dx,dx; je */
static const BYTE cyclic_prev_sig[]={0x55,0x8b,0xec,0x8b,0x55,0x0c,0x8b,0x45,0x08,0x66,0x85,0xd2,0x74,0x05};
/* 478820, 47882c: push ebp; mov ebp,esp; mov ax,1; pop ebp; ret */
static const BYTE cyclic_reach_up_sig[]={0x55,0x8b,0xec,0x66,0xb8,0x01,0x00,0x5d,0xc3};
static const BYTE cyclic_reach_down_sig[]={0x55,0x8b,0xec,0x66,0xb8,0x01,0x00,0x5d,0xc3};
static const BYTE manager_slots_load_lens[]={1,2,3},manager_slots_mov_lens[]={1,2,4};
#define MANAGER_SLOTS_EXT(name) \
    static const es2_hook_extension manager_slots_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,manager_slots_##name,FALSE}; \
    static uint32_t __cdecl manager_slots_##name##32(const uint32_t *a,void *u) {return (uint32_t)manager_slots_##name(a,u,weapons_no_registers);}
MANAGER_SLOTS_EXT(object_position)
MANAGER_SLOTS_EXT(shape_word)
MANAGER_SLOTS_EXT(sequence_next)
MANAGER_SLOTS_EXT(sequence_prev)
MANAGER_SLOTS_EXT(sequence_reach_up)
MANAGER_SLOTS_EXT(sequence_reach_down)
MANAGER_SLOTS_EXT(cyclic_next)
MANAGER_SLOTS_EXT(cyclic_prev)
MANAGER_SLOTS_EXT(cyclic_reach_up)
MANAGER_SLOTS_EXT(cyclic_reach_down)
#undef MANAGER_SLOTS_EXT
#define MANAGER_SLOTS_DESCRIPTOR(name,relocate,lens,args) \
    {ES2_HOOK_ID(name),name##_sig,sizeof(name##_sig),relocate,lens,3,args,manager_slots_##name##32,NULL,NULL,NULL,0}
static es2_hook_descriptor manager_slots_hook_descriptors[]={
    MANAGER_SLOTS_DESCRIPTOR(object_position,6,manager_slots_load_lens,1),
    MANAGER_SLOTS_DESCRIPTOR(shape_word,6,manager_slots_load_lens,1),
    MANAGER_SLOTS_DESCRIPTOR(sequence_next,6,manager_slots_load_lens,2),
    MANAGER_SLOTS_DESCRIPTOR(sequence_prev,6,manager_slots_load_lens,2),
    MANAGER_SLOTS_DESCRIPTOR(sequence_reach_up,6,manager_slots_load_lens,4),
    MANAGER_SLOTS_DESCRIPTOR(sequence_reach_down,6,manager_slots_load_lens,4),
    MANAGER_SLOTS_DESCRIPTOR(cyclic_next,6,manager_slots_load_lens,2),
    MANAGER_SLOTS_DESCRIPTOR(cyclic_prev,6,manager_slots_load_lens,2),
    MANAGER_SLOTS_DESCRIPTOR(cyclic_reach_up,7,manager_slots_mov_lens,4),
    MANAGER_SLOTS_DESCRIPTOR(cyclic_reach_down,7,manager_slots_mov_lens,4),
};
#undef MANAGER_SLOTS_DESCRIPTOR
#endif
