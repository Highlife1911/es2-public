#ifndef ES2_CORE_WEAPONS_H
#define ES2_CORE_WEAPONS_H
#include "es2/core/guest.h"
/* Original DBSIM entries; exact contracts: docs/reverse-engineering/weapons.md.
 * Addresses wrap modulo 2^32. No alignment or disjointness requirement for
 * mask_merge. Selectors accept every signed-byte index and borrow the base. */
uint64_t es2_projectile_def_registers(uint32_t base, uint8_t index);
uint64_t es2_bullet_def_registers(uint32_t base, uint8_t index);
uint64_t es2_mask_merge_registers(const es2_guest_memory *, uint32_t dst, uint32_t src);

/* Radius dispatch forwards the high ECX word returned by distance in a
 * narrow argument slot. A pair-only callback would silently lose it.
 * Callers supply EAX/EDX/ECX at entry and receive all three after the call.
 * x87 state must be left as produced by the guest. No host pointer narrowing. */
typedef struct es2_weapon_registers { uint32_t eax, edx, ecx; } es2_weapon_registers;
typedef void (*es2_weapon_call)(void *, uint32_t, const uint32_t *, unsigned, es2_weapon_registers *);
typedef void (*es2_weapon_observer)(void *, unsigned slot);
typedef struct es2_weapon_context {
    uint32_t list_slot, count_slot, distance, merge, timer, prepare;
    uint32_t projectile, bullet, position, angles;
    es2_weapon_call call;
    void *context;
    es2_weapon_observer observe;
    void *observer_context;
} es2_weapon_context;
uint64_t es2_radius_hit_registers(const es2_guest_memory *, const es2_weapon_context *,
    uint32_t point, uint32_t range, uint32_t damage, uint32_t source,
    uint32_t excluded, const uint32_t entry[3]);
uint64_t es2_weapon_step_registers(const es2_guest_memory *, const es2_weapon_context *,
    uint32_t weapon, uint32_t result, const uint32_t entry[3]);
uint64_t es2_weapon_fire_registers(const es2_guest_memory *, const es2_weapon_context *,
    uint32_t weapon, uint32_t owner, uint32_t free_word, const uint32_t entry[3]);
/* Weapon graph context. scratch is 128 accessible guest bytes, disjoint
 * from graph state. Both matrix padding bytes are explicit entry-stack residue.
 * All game code is called through call, including already-hooked leaves. */
typedef void (*es2_weapon_graph_observer)(void *, uint32_t function, unsigned slot);
typedef struct es2_weapon_graph {
    uint32_t image, scratch;
    uint32_t excluded; /* captured flight exclusion / emitter control residue */
    uint8_t padding, composed_padding;
    es2_weapon_call call;
    void *context;
    es2_weapon_graph_observer observe;
    void *observer_context;
} es2_weapon_graph;
uint64_t es2_weapon_prepare_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t weapon,uint32_t owner,const uint32_t entry[3]);
uint64_t es2_bullet_flight_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
uint64_t es2_projectile_flight_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
uint64_t es2_target_emit_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,const uint32_t entry[3]);
/* Impact needs 256 scratch bytes; the other graph calls need 128.
 * The runtime initializer callback uses a synthetic EBP at scratch+208. */
uint64_t es2_emit_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t weapon,uint32_t owner,uint32_t free_word,const uint32_t entry[3]);
uint64_t es2_alt_emit_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t weapon,uint32_t owner,uint32_t free_word,const uint32_t entry[3]);
uint64_t es2_impact_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t type,uint32_t matrix,uint32_t range,uint32_t owner,uint32_t scale,const uint32_t entry[3]);
/* Swept query: 256 scratch bytes, synthetic EBP at scratch+208 like impact.
 * EDX returns the exception-chain head the caller must restore to FS:0. */
uint64_t es2_sweep_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t packet,const uint32_t entry[3]);
/* Swept-query leaves. side_scale delegates its Q10 multiply through call;
 * cell is read-only and needs a finite traversal (see weapons.md). */
uint64_t es2_side_scale_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t destination,uint32_t side,const uint32_t entry[3]);
uint64_t es2_cell_registers(const es2_guest_memory *,uint32_t resource,uint32_t point,uint32_t radius_slot);
/* Terrain segment walk 46e980 (map, start, end, radius, out, mode) and its
 * sweep wrapper 428048 (out, segment). segment needs 256 scratch bytes, the
 * wrapper 64; the walk must stay finite (small grid, see weapons.md). */
uint64_t es2_segment_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t map,uint32_t start,
    uint32_t end,uint32_t radius,uint32_t out,uint32_t mode,const uint32_t entry[3]);
uint64_t es2_sweep_terrain_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t out,uint32_t segment,const uint32_t entry[3]);
/* Callees of the terrain walk (weapons.md). edge_test and contact need 64
 * bytes of frame scratch; face_test receives its caller's stack residue at
 * EBP-18h, used only for flags 3. */
uint64_t es2_slope_test_registers(const es2_guest_memory *,uint32_t vector,uint32_t normal,const uint32_t entry[3]);
uint64_t es2_normalize_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vector,uint32_t length,const uint32_t entry[3]);
uint64_t es2_diagonal_registers(const es2_guest_memory *,uint32_t map,uint32_t prev,uint32_t cur,uint32_t column,uint32_t row,uint32_t out);
uint64_t es2_edge_test_registers(const es2_guest_memory *,uint32_t frame,uint32_t map,uint32_t column,uint32_t row,uint32_t point,uint32_t direction);
uint64_t es2_face_test_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t map,uint32_t vector,
    uint32_t point,uint32_t residue,const uint32_t entry[3]);
uint64_t es2_face_cross_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t map,uint32_t column,
    uint32_t row,uint32_t vector,uint32_t direction,const uint32_t entry[3]);
uint64_t es2_contact_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t frame,uint32_t map,
    uint32_t prev,uint32_t cur,uint32_t column,uint32_t row,const uint32_t entry[3]);
uint64_t es2_terrain_line_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t out,uint32_t start,
    uint32_t end,uint32_t radius,const uint32_t entry[3]);
/* BEAM constructor 40b804 (object, type word, start, end, owner
 * dword). The graph's scratch is the original's 28h-byte frame (delta at
 * +0Ch, cur at +18h, point at +0); the array helper 49667c is called
 * through the graph like every other callee. EDX:EAX exact. */
#define ES2_BEAM_CREATE_SCRATCH 0x28u
uint64_t es2_beam_create_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t object,
    uint32_t type,uint32_t start,uint32_t end,uint32_t owner,const uint32_t entry[3]);
/* Bullet constructor (weapons.md, "bullet constructor"):
 * bullet_create 40b43c (type, position, angles, owner), its object
 * constructor bullet_init 40af6c (O, type) and the definition lookup
 * def_lookup 40ffc8 (category, type). Every game call goes through g->call
 * with the EAX/EDX/ECX the original holds there (pool_alloc, object_init,
 * bullet_def, rng_next, sfx_play_at and the group's own hooks keep their
 * modes; 49630c and the memset thunk 49681a stay original code). g->scratch
 * is bullet_create's 2Ch-byte frame from its lowest local, so f = g->scratch
 * + ES2_BULLET_CREATE_SCRATCH is the original's EBP; the core writes every
 * frame local the original writes. Unless frame is NULL, bullet_create also
 * stores, before every game call, the EBX/ESI/EDI the original holds there
 * and the address after its CALL (ebx/esi/edi = the caller's registers,
 * which the original keeps up to its first own use). Results: the full
 * EDX:EAX pair; bullet_create's EDX is the runtime record's saved FS:0 head
 * (f-24h), which the caller (the adapter) stores back into FS:0, and *ecx
 * receives the ECX the original returns (sfx_play_at's). Preconditions (the
 * original faults): pool_alloc returns an object, [O+4Eh] is a readable
 * definition and s16([D+2]) != 0 when word [D+0Ch] != 0. */
typedef struct es2_weapon_frame { uint32_t ebx, esi, edi, ret; } es2_weapon_frame;
#define ES2_BULLET_CREATE_SCRATCH 0x2cu
uint64_t es2_def_lookup_registers(const es2_guest_memory *,uint32_t image,uint32_t category,uint32_t type);
uint64_t es2_bullet_init_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t object,uint32_t type,
    const uint32_t entry[3]);
uint64_t es2_bullet_create_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_weapon_frame *frame,
    uint32_t ebx,uint32_t esi,uint32_t edi,uint32_t type,uint32_t position,uint32_t angles,uint32_t owner,
    const uint32_t entry[3],uint32_t *ecx);
/* Weapon and projectile helpers (weapons.md, "weapon and
 * projectile helpers"): proj_record 40ffb0 (the PROJ lookup record s16(i);
 * EDX = the entry EDX), damage_words 40501c (the default damage words
 * 4a95f2h/4a95f4h; EAX/EDX keep their entry high words), pitch_point
 * 49295c (dist2d_delta, then vector_angle), bullet_charge 40b5a0
 * (bullet_create, the charge word O+56h, the type-9 lock O+5Bh) and
 * rocket_init 40a948 (object_init, the vtables 4987a0 then 498448,
 * def_lookup(0, n)). entry = EAX/EDX/ECX at entry. Game calls go through
 * g->call with the original's EAX/EDX/ECX (dist2d_delta, vector_angle,
 * bullet_create, object_init, def_lookup keep their modes); *ecx (may be
 * NULL) receives the ECX the original returns. Results: the full EDX:EAX
 * pair. Precondition of bullet_charge: bullet_create returns an object. */
uint64_t es2_proj_record_registers(const es2_guest_memory *,uint32_t image,uint32_t index,const uint32_t entry[3]);
uint64_t es2_damage_words_registers(const es2_guest_memory *,uint32_t image,uint32_t x,uint32_t y,const uint32_t entry[3]);
uint64_t es2_pitch_point_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t p,uint32_t q,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_bullet_charge_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t type,uint32_t position,
    uint32_t angles,uint32_t owner,uint32_t charge,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_rocket_init_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t object,uint32_t type,
    const uint32_t entry[3],uint32_t *ecx);
/* (weapons.md, "missile steering 40a488 and ally selection 423974"):
 * missile_steer 40a488 (O, R) steers the player's type-3 missile O by the
 * control record R (multiply_q8 and movement_step through call); ally_nearest
 * 423974 (G, X) is the member of group G nearest to X (dist_gate through call),
 * a read-only query. */
uint64_t es2_missile_steer_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t object,uint32_t record,
    const uint32_t entry[3]);
uint64_t es2_ally_nearest_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t group,uint32_t actor,
    const uint32_t entry[3]);
#endif
