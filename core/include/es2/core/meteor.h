#ifndef ES2_CORE_METEOR_H
#define ES2_CORE_METEOR_H
#include "es2/core/weapons.h"
/* The meteor constructor and the shape-instance constructor
 * (docs/reverse-engineering/main_loop.md, section "meteor
 * constructor 409b44 and shape instance 490b64").
 *
 * shape_instance 0x490b64(O, S): three vtable stores, two increments of
 * the runtime construction count [[0x6cbee4]], O+4 = S, O+8 = a copy of
 * S's 2*n bytes from the arena allocation 0x47a288 (n = word S+24h, the
 * source dword S+1Ch, the memcpy thunk 0x49680e) or 0 for n = 0. Both
 * game calls go through g->call with the original's EAX/EDX/ECX. Result
 * EDX:EAX exactly (O, the counter pointer); *ecx = the original's ECX
 * (the entry ECX for n = 0, else the memcpy thunk's).
 *
 * meteor_create 0x409b44(O, P, G): every game call goes through g->call
 * with the original's EAX/EDX/ECX: the runtime frame setup 0x49630c
 * (synthetic EBP f = g->scratch + ES2_METEOR_FRAME; record f-24h, context
 * word f-14h, heap slot f-4, the locals f-48h..f-2Ah), object_init
 * 0x402188, rng_next 0x492ee0 and rng_bounded 0x492f24 (stream A),
 * sincos_pair 0x47eb30, rotate_2d 0x47ffe8, the rotation 0x47ff8c, the
 * heap 0x47749c and shape_instance 0x490b64. Result EDX:EAX exactly (O,
 * the counter pointer); *ecx = the record's saved FS:0 head (the
 * original's ECX), which the caller writes back to FS:0 as the epilogue
 * does. g->scratch must be ES2_METEOR_SCRATCH accessible guest bytes. */
#define ES2_METEOR_SCRATCH 0x60u
#define ES2_METEOR_FRAME 0x58u
uint64_t es2_shape_instance_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t shape,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_meteor_create_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t point,uint32_t group,const uint32_t entry[3],uint32_t *ecx);
#endif
