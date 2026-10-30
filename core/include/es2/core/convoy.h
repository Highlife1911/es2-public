#ifndef ES2_CORE_CONVOY_H
#define ES2_CORE_CONVOY_H
#include "es2/core/weapons.h"
/* Update chain of the building-pool class VEHICLE (vtable 0x497818, slot
 * +18h): convoy_think 0x46a6d4, convoy_drive 0x46a810,
 * convoy_lead 0x46a9e8, convoy_follow 0x46aa60, convoy_seek 0x46a958 and
 * convoy_steer 0x46a89c. Contracts: docs/reverse-engineering/convoy.md.
 *
 * Every game callee (the group-leader search 0x46a5bc, the base update
 * 0x403ca8, turret, turret_aim, terrain_pose, the collision test 0x46a614,
 * route_point, route_advance, dist2d_point, heading_point, the point
 * projection 0x4929fc, euler_matrix, transform_point, the 2 x 2 swap
 * 0x47df04, rotate_2d, timer_word_step, approach_word, multiply_q8, the
 * virtual +78h and the group's own nested functions) is called through
 * g->call with the original's entry EAX/EDX/ECX. g->image is the image
 * base; g->scratch holds ES2_CONVOY_SCRATCH guest bytes for the frame
 * locals that are passed by pointer (synthetic EBP = scratch + 40h).
 *
 * follow_edi is convoy_follow's entry EDI (callee-saved, never set by
 * 0x46aa60; its high word reaches three callee slots that read only the
 * low word). The game's only call chain passes object + 26h.
 * Preconditions (the original faults): convoy_think's group has a member;
 * convoy_follow's leader search finds a member. */
#define ES2_CONVOY_SCRATCH 64u
uint64_t es2_convoy_steer_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t steer,uint32_t speed,const uint32_t entry[3]);
uint64_t es2_convoy_seek_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t point,uint32_t heading,uint32_t speed,const uint32_t entry[3]);
uint64_t es2_convoy_lead_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
uint64_t es2_convoy_follow_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t follow_edi,const uint32_t entry[3]);
uint64_t es2_convoy_drive_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
uint64_t es2_convoy_think_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
/* convoy_leader 46a5bc (B): the first of the group's members 0..3
 * (group [B+45h], list dword +0Ch, count word +10h, both reread per pass)
 * whose bytes +0A4h and +99h are both 0; EAX = that member, else 0 after
 * four. EDX = the last member read. A member index at or above the signed
 * count gives member 0, whose bytes are read at 0A4h (the original
 * faults; the core reads the same address). Pure: no call, no write.
 * Contract: docs/reverse-engineering/convoy.md, section "46a5bc
 * convoy_leader(B)". */
uint64_t es2_convoy_leader_registers(const es2_guest_memory *,uint32_t object);
#endif
