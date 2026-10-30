#ifndef ES2_CORE_TORSO_H
#define ES2_CORE_TORSO_H
#include "es2/core/weapons.h"
/* Torso servos and weapon keys; contracts in
 * docs/reverse-engineering/input.md, section "torso servos and weapon keys".
 * Every game call, hooked or not, goes through the graph's guest bridge with
 * the EAX/EDX/ECX the original holds at that call, so nested hooks keep their
 * own modes. g->image is the loaded image base.
 *
 * torso_twist 41a550 (P, rate, target, snap) and torso_pitch 41a808
 * (P, rate, d, target, snap): scale the rate input, approach it, integrate
 * the twist word P+298h / pitch word P+29Ah, clamp, optionally stop at the
 * target, seek the torso track; the pitch servo then calls 41a74c(P, d).
 * The sound latches 41a6d0 / 41a994 stay original. EDX:EAX = P+92h : EDX
 * or the latch's pair.
 *
 * weapon_converge 41a74c (P, d): the convergence angles of every weapon
 * (words S+24h..S+29h) via 40f540 and aim_angles 492990. Needs 28h bytes of
 * frame scratch in guest memory at g->scratch (synthetic EBP at +28h).
 *
 * weapon_keys 410f78 (W, code, esi): the weapon-system keys. esi is the
 * caller's ESI (entry shim); its high word is the result's for a handled
 * key. The entry EAX/EDX are dead; entry[2] is the caller's ECX. */
enum { ES2_WEAPON_CONVERGE_SCRATCH = 0x28 };
uint64_t es2_torso_twist_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,uint32_t rate,
    uint32_t target,uint32_t snap,const uint32_t entry[3]);
uint64_t es2_torso_pitch_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,uint32_t rate,
    uint32_t distance,uint32_t target,uint32_t snap,const uint32_t entry[3]);
uint64_t es2_weapon_converge_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,
    uint32_t distance,const uint32_t entry[3]);
uint64_t es2_weapon_keys_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapons,uint32_t code,
    uint32_t esi,const uint32_t entry[3]);
#endif
