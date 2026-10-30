#ifndef ES2_CORE_MISSION_H
#define ES2_CORE_MISSION_H
#include "es2/core/weapons.h"
/* Runtime mission logic; exact contracts in
 * docs/reverse-engineering/mission_runtime.md. Every game call, including
 * the hooked functions of this group, goes through g->call with the entry
 * registers the original sets up, so nested hooks keep their own modes.
 * g->image locates the mission globals. */

/* Trigger dispatch and region predicates.
 *
 * region_test 4233a4 (region, position): AX = 1 when the position lies
 * strictly inside the rectangle (type 0) or below the distance threshold.
 * trigger_regions 4234b8 (trigger, position): AX = 1 when latched or when a
 * region contains the position (then trigger_fire runs).
 * trigger_group 423508 (group, trigger): trigger_regions per actor; an empty
 * group returns AX = entry ECX >> 16 (uninitialized local).
 * trigger_fire 423430 (trigger): latch, variable actions, one message per
 * used slot. residue is the guest address of the 16 record bytes the
 * original never initializes (entry ESP-16h..-7), 0 for zeros; g->scratch
 * needs 32 accessible bytes for the event record.
 * trigger_scan 426b70 (player): the per-frame trigger pass.
 * All return EDX:EAX exactly; ECX is outside the contract. */
uint64_t es2_region_test_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t region,uint32_t position,const uint32_t entry[3]);
uint64_t es2_trigger_regions_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t trigger,uint32_t position,const uint32_t entry[3]);
uint64_t es2_trigger_group_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t group,uint32_t trigger,const uint32_t entry[3]);
uint64_t es2_trigger_fire_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t trigger,uint32_t residue,const uint32_t entry[3]);
uint64_t es2_trigger_scan_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t player,const uint32_t entry[3]);

/* The player's route callback, mission timers and the status check
 * (mission_runtime.md, section "Player route callback, mission timers and
 * status check").
 *
 * route_closing 423170 (holder) and route_advance 42313c (holder): leaves
 * on the route holder group+4.
 * player_route 41c194 (actor): route progress and the mission-mode checks;
 * EAX = 0.
 * timer_scan 426b48 (): mission_timer 4230a4 for every mission timer.
 * mission_timer 4230a4 (timer): gate, step, fire ten triggers, re-arm.
 * status_check 4135e8 (player, unused, force): the second slot is never
 * read. residue is the guest address of the 16 event record bytes the
 * original never initializes (entry ESP-16h..-7), 0 for zeros; g->scratch
 * needs 32 accessible bytes for the record. 413280 reads the caller's ESI,
 * which must be force: the caller's g->call provides that.
 * All return EDX:EAX exactly; ECX is outside the contract. */
uint64_t es2_route_closing_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t holder,const uint32_t entry[3]);
uint64_t es2_route_advance_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t holder,const uint32_t entry[3]);
uint64_t es2_player_route_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,const uint32_t entry[3]);
uint64_t es2_timer_scan_registers(const es2_guest_memory *,const es2_weapon_graph *,
    const uint32_t entry[3]);
uint64_t es2_mission_timer_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t timer,const uint32_t entry[3]);
uint64_t es2_status_check_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t player,uint32_t force,uint32_t residue,const uint32_t entry[3]);
#endif
