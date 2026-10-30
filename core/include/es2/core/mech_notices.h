#ifndef ES2_CORE_MECH_NOTICES_H
#define ES2_CORE_MECH_NOTICES_H
#include "es2/core/weapons.h"
/* The MECH notice virtuals (docs/reverse-engineering/ai.md, section
 * "MECH notices"):
 *   mech_contact  412800 (A, B)    MECH virtual +48h, contact notice,
 *   mech_kill     415710 (K, V, w) MECH virtual +60h, kill credit,
 *   mech_attacked 41f7b8 (A, S, w) MECH virtual +50h, attacked-by notice,
 *   mech_alert    41f790 (A)       the "under attack" radio helper of +50h
 *                                  and of the sweep 426528.
 * Every game call (timer_word_step 467a48, the cockpit message post 420a98,
 * the ACTOR +50h default 411aec, actor_destination 41dbcc, target_search
 * 41e0e0, script_start 413e50, part_pick 41ce08, attack_target 41c0f4,
 * target_select 411fa0, attack_replan 41d5ec, order_resume 41eb34 and
 * mech_alert itself under mech_attacked) goes through g->call at the
 * original address with the EAX/EDX/ECX the original holds there, so nested
 * hooks keep their own modes; g->observe is told the original address.
 * g->image locates the globals (499358, 4a9b00.., 4a9b64, 4a9b84, 4a9e84)
 * and the pushed addresses; no scratch is used. entry[] is EAX/EDX/ECX at
 * entry. Each returns the original's EDX:EAX; ECX after return is outside
 * the contract (dead at every caller, ai.md). Word arguments are read as
 * their low 16 bits. */
uint64_t es2_mech_contact_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t other,const uint32_t entry[3]);
uint64_t es2_mech_kill_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t killer,uint32_t victim,uint32_t word,const uint32_t entry[3]);
uint64_t es2_mech_attacked_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t source,uint32_t word,const uint32_t entry[3]);
uint64_t es2_mech_alert_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,const uint32_t entry[3]);
#endif
