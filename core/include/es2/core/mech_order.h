#ifndef ES2_CORE_MECH_ORDER_H
#define ES2_CORE_MECH_ORDER_H
#include "es2/core/weapons.h"
/* The MECH order handler and the cockpit's wingman order
 * (docs/reverse-engineering/mech_order.md). Every game call goes through
 * g->call at the original address with the registers the original holds
 * there, so the hooked callees (target_select, dist_gate, dist2d_point and
 * mech_order itself through the vehicle virtual +28h) keep their own modes;
 * the AI helpers 411CB4, 41C0F4, 41EB34, 4238A0, 413E50, the list step
 * 471C70, the radio message 420A98 and the HUD message cancel 435AC8 stay
 * game code. g->image locates the globals and the pushed script addresses;
 * no scratch is used. Each returns the original's EDX:EAX; ECX is outside
 * the contract. */

/* mech_order 420AD4 (vehicle A, order record R, mode word): the vehicle
 * virtual +28h of MECHs. Dispatches the order code word [R] (0..11h, others
 * ignored), updates A's order state and posts the answer code through
 * 420A98 when the mode word is 0, or 1 and the order was taken. EAX = 1 if
 * the order was taken, else 0. */
uint64_t es2_mech_order_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t vehicle,uint32_t record,uint32_t mode,const uint32_t entry[3]);
/* wingman_order 431610 (cockpit H, order record R, slot word): sends R to
 * the wingman in slot [4D03C8h + 4*slot] through its virtual +28h, then
 * cancels HUD message 22h with 435AC8. EDX:EAX = 435AC8's. */
uint64_t es2_wingman_order_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t cockpit,uint32_t record,uint32_t slot,const uint32_t entry[3]);
#endif
