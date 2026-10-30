#ifndef ES2_CORE_DESTRUCTORS_H
#define ES2_CORE_DESTRUCTORS_H
#include "es2/core/weapons.h"
/* The object destructors of the stage-5 pools and the shape-instance
 * destructor pair (docs/reverse-engineering/main_loop.md, section "object
 * destructors").
 *
 * object_dtor(kind, O, flag): the destructor of one class (ES2_DTOR_*),
 * cdecl (O, flag). Every game call goes through g->call with the EAX/EDX/
 * ECX the original holds there: the runtime frame setup 0x49630c
 * (synthetic EBP f = g->scratch + ES2_DESTRUCTORS_FRAME; record f-24h,
 * context word f-14h, shape slot f-4), removal_append 0x478628 (EXPLOSION),
 * sfx_stop 0x462ac4 (FIRE), the arena free 0x47758c (BEAM), the runtime
 * RTTI count 0x496406, the shape instance's virtual +18h (target read from
 * its vtable, observed as slot 18h) and, for flag bit 0, the heap free
 * 0x477548. Result EDX:EAX exactly; *ecx = the original's ECX; *head = the
 * saved FS:0 head the epilogue stores, which the caller writes back to
 * FS:0. g->scratch must be ES2_DESTRUCTORS_SCRATCH accessible guest bytes.
 *
 * shape_dtor 0x490bdc(S, flag) and shape_base_dtor 0x48be20(S, flag): no
 * frame; game calls 0x47758c, shape_base_dtor 0x48be20 and 0x477548
 * (flag bit 0) through g->call. Result EDX:EAX exactly; *ecx = the
 * original's ECX. g->scratch is not used. */
#define ES2_DESTRUCTORS_SCRATCH 64u
#define ES2_DESTRUCTORS_FRAME 48u
enum {
    ES2_DTOR_EXPLOSION, /* 0x407e48 */
    ES2_DTOR_FIRE,      /* 0x46b574 */
    ES2_DTOR_BEAM,      /* 0x40ba94 */
    ES2_DTOR_DEBRIS,    /* 0x40f7e6 */
    ES2_DTOR_FLAT,      /* 0x41bd46 */
    ES2_DTOR_METEOR,    /* 0x424034 */
    ES2_DTOR_BULLET,    /* 0x40b64d */
    ES2_DTOR_ROCKET,    /* 0x40ab8d */
    ES2_DTOR_KINDS
};
uint64_t es2_object_dtor_registers(const es2_guest_memory *,const es2_weapon_graph *,unsigned kind,
    uint32_t object,uint32_t flag,const uint32_t entry[3],uint32_t *ecx,uint32_t *head);
uint64_t es2_shape_dtor_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t shape,uint32_t flag,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_shape_base_dtor_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t shape,uint32_t flag,const uint32_t entry[3],uint32_t *ecx);
#endif
