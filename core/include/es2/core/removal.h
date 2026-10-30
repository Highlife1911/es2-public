#ifndef ES2_CORE_REMOVAL_H
#define ES2_CORE_REMOVAL_H
#include "es2/core/weapons.h"
/* Deferred object removal of stage 5 (docs/reverse-engineering/
 * main_loop.md, section "object removal").
 *
 * A removal list L is { dword item array at +0, signed word count at +4 }.
 * A pool P is { word free count +0, word used count +2, dword free head
 * +0Ah, dword used head +0Eh }; an object O has its link node N = O-8
 * (next +0, previous +4).
 *
 * removal_flush 0x478644(L, P): every game call goes through g->call with
 * the EAX/EDX/ECX the original holds there: the runtime frame setup
 * 0x49630c (synthetic EBP f = g->scratch + ES2_REMOVAL_FRAME, record at
 * f-24h, context word f-14h), per nonnull entry the runtime 0x496406(O, 0)
 * and the destructor [O]+8 (O, 2), and per entry pool_free 0x471bc8(P, O).
 * Results: EDX:EAX exactly; *ecx = the original's ECX (the last
 * pool_free's ECX, or the entry ECX when the loop did not run). EDX is the
 * record's saved FS:0 head: the caller restores FS:0 to it, as the
 * epilogue does.
 *
 * pool_free 0x471bc8(P, O) and removal_append 0x478628(L, v) are leaves:
 * EDX:EAX exactly, *ecx = the original's ECX; entry registers not read. */
#define ES2_REMOVAL_SCRATCH 64u
#define ES2_REMOVAL_FRAME 48u
uint64_t es2_removal_flush_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t list,uint32_t pool,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_pool_free_registers(const es2_guest_memory *,uint32_t pool,uint32_t object,uint32_t *ecx);
uint64_t es2_removal_append_registers(const es2_guest_memory *,uint32_t list,uint32_t value,uint32_t *ecx);
#endif
