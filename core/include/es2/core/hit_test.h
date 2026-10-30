#ifndef ES2_CORE_HIT_TEST_H
#define ES2_CORE_HIT_TEST_H
#include "es2/core/weapons.h"
/* The actor hit tests of vtable slot +20h
 * (docs/reverse-engineering/hit_tests.md, sections "Dispatch site
 * and packet" and "hit tests").
 *
 *   base_hit_test  0x405038  BASE, RADAR_BASE, GUN_BASE, VEHICLE, LC_BASE:
 *                            object query or COL traversal; hit distance + 1
 *   mech_hit_test  0x418ba8  MECH: shield model first, then COL; the shield
 *                            penetration depth or the COL hit word (no + 1);
 *                            a COL hit goes to the part hit 0x4188c8
 *   flyer_hit_test 0x421c8c  FLYER: view subject exempt; COL hit distance
 *                            + 1, explosion with owner 0, debris
 *
 * The only caller is the sweep's virtual call at 0x4266c9 (stack: actor,
 * packet P). P+0 the segment S (S+0 the forward pose, S+4 the length, S+8
 * the radius word, S+0Ah the inverse pose), P+4 and P+6 damage words, P+0Ah
 * the effect table, P+0Eh the owner, P+12h the weapon kind.
 *
 * Every game call goes through g->call with the EAX/EDX/ECX the original
 * holds there: the runtime frame setup 0x49630c (synthetic EBP f =
 * g->scratch + ES2_HIT_TEST_FRAME; record at f-24h, context word f-14h),
 * the virtual slots +10h and +74h of the actor (target read from its
 * vtable), the hooked leaves (object_query, dist_gate, euler_matrix,
 * matrix_compose, col_query, transform_point, shield_geometry, pool_alloc,
 * rng_next, explosion_create, debris_table, debris_burst), the message post
 * 0x420a68 and the MECH part hit 0x4188c8. g->image is the image base.
 * Frame locals that reach a game call by address live below f, so each
 * function needs ES2_HIT_TEST_SCRATCH guest bytes of scratch.
 *
 * Results: EDX:EAX exactly as the originals leave them (EDX is the record's
 * saved FS:0 head on most exits). The original's epilogue restores FS:0
 * from that head; the caller does it after these functions return. ECX is
 * dead at the sweep (hit_tests.md, "Dispatch site and packet"). */
#define ES2_HIT_TEST_SCRATCH 256u
#define ES2_HIT_TEST_FRAME 208u
uint64_t es2_base_hit_test_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t packet,const uint32_t entry[3]);
uint64_t es2_mech_hit_test_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t packet,const uint32_t entry[3]);
uint64_t es2_flyer_hit_test_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t packet,const uint32_t entry[3]);
#endif
