#ifndef ES2_CORE_SIM_LEAVES_H
#define ES2_CORE_SIM_LEAVES_H
#include "es2/core/weapons.h"
/* The last simulation leaves (docs/reverse-engineering/vtables.md,
 * section "simulation leaves"). Addresses are guest addresses; the
 * `image` of the global-reading functions and g->image of the others is the
 * loaded image's base (0x400000 in the original). Every function returns
 * the original's EDX:EAX; `entry` holds the entry EAX/EDX/ECX, and when
 * `ecx` is not NULL the original's ECX is stored there (the adapters pass
 * NULL: ECX is dead at every caller). Game calls go through g->call with
 * the original's registers, so a callee that is itself a hook keeps its
 * own mode. */

/* part_shapes_bind 4030d0 (A, B, pool): two counted lists {pointer array
 * dword +0, word count +4}; each non-empty slot of A takes the next entry
 * of B (or, for a zero entry, a record of the pool at 18h bytes per step)
 * and copies the words +4 and +6 of the record it pointed to. */
uint64_t es2_part_shapes_bind_registers(const es2_guest_memory *, uint32_t a, uint32_t b, uint32_t pool,
    const uint32_t entry[3], uint32_t *ecx);
/* parts_intact 403668 (object): 1 when no part record with a cap above
 * zero is below its cap. */
uint64_t es2_parts_intact_registers(const es2_guest_memory *, uint32_t object, const uint32_t entry[3],
    uint32_t *ecx);
/* formation_slot 405b9c (_, row, col): the first call clears the
 * three-dword cell 4975d6; EAX = 4975d6 after copying the two dwords of
 * the table entry. */
uint64_t es2_formation_slot_registers(const es2_guest_memory *, uint32_t image, uint32_t row, uint32_t col,
    const uint32_t entry[3], uint32_t *ecx);
/* explosion_entry 407b20 (object): EAX = EDX = [4a9628] + 40 x sx8 byte
 * [object+41h]. */
uint64_t es2_explosion_entry_registers(const es2_guest_memory *, uint32_t image, uint32_t object,
    const uint32_t entry[3], uint32_t *ecx);
/* flat_draw 40991c (object): terrain_pose 4029d8, shape_actor_matrix
 * 401fe4, then slot +1Ch of the child [object+34h]. */
uint64_t es2_flat_draw_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t object,
    const uint32_t entry[3], uint32_t *ecx);
/* projectile_draw 40a120 (object): as flat_draw without terrain_pose, with
 * the global 4a5b04 cleared before and loaded from 4a5b08 after. */
uint64_t es2_projectile_draw_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t object,
    const uint32_t entry[3], uint32_t *ecx);
/* beam_done 40c2a0 (object): 1 when timer_word_step 467a48(object+5Ch)
 * returns a zero word. */
uint64_t es2_beam_done_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t object,
    const uint32_t entry[3], uint32_t *ecx);
/* weapon_by_mount 410670 (list {array +4, word count +8}, w): the first
 * entry whose mount definition byte [[c+18h]+17h] equals w, else 0. */
uint64_t es2_weapon_by_mount_registers(const es2_guest_memory *, uint32_t list, uint32_t w,
    const uint32_t entry[3], uint32_t *ecx);
/* ptr_list_add 411dd4 (list, p): stores p at index word [list+6], bumps the
 * count and writes the old index to word [p+4Bh]. */
uint64_t es2_ptr_list_add_registers(const es2_guest_memory *, uint32_t list, uint32_t p, const uint32_t entry[3],
    uint32_t *ecx);
/* box_square_init 413a90 (rect, w): words +6 = +8 = w, +4 = 200h, +2 = +0
 * = sx(w) >> 1. */
uint64_t es2_box_square_init_registers(const es2_guest_memory *, uint32_t rect, uint32_t w,
    const uint32_t entry[3], uint32_t *ecx);
/* twist_sound_latch 41a6d0 / pitch_sound_latch 41a994 (vehicle, w):
 * sfx_playing 462af0(21h) once |w| exceeds 0C0h and the servo word V+298h /
 * V+29Ah has moved; the flag byte and last word live in 49a1c9/49a1ca and
 * 49a1cc/49a1ce. */
uint64_t es2_twist_sound_latch_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t vehicle,
    uint32_t w, const uint32_t entry[3], uint32_t *ecx);
uint64_t es2_pitch_sound_latch_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t vehicle,
    uint32_t w, const uint32_t entry[3], uint32_t *ecx);
/* mech_type_index 41ff00 / flyer_type_index 422e80 (w): EAX = the byte at
 * 4a9d08 / 4a9dcc indexed by sx(w). */
uint64_t es2_mech_type_index_registers(const es2_guest_memory *, uint32_t image, uint32_t w,
    const uint32_t entry[3], uint32_t *ecx);
uint64_t es2_flyer_type_index_registers(const es2_guest_memory *, uint32_t image, uint32_t w,
    const uint32_t entry[3], uint32_t *ecx);
/* cockpit_notice 420a68 (id) / radio_notice 420a98 (actor, code, flag):
 * the event record {word code, dword 0 or the actor, 18 zero residue
 * bytes} is posted to the event port's slot 0 (g->scratch holds 24
 * bytes). */
uint64_t es2_cockpit_notice_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t id,
    const uint32_t entry[3], uint32_t *ecx);
uint64_t es2_radio_notice_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t actor,
    uint32_t code, uint32_t flag, const uint32_t entry[3], uint32_t *ecx);
/* notice_time 426aec: EAX = 7530h, doubled unless word 4a9e5e is 1. */
uint64_t es2_notice_time_registers(const es2_guest_memory *, uint32_t image, const uint32_t entry[3],
    uint32_t *ecx);
#endif
