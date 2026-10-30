#ifndef ES2_CORE_DAMAGE_H
#define ES2_CORE_DAMAGE_H
#include "es2/core/guest.h"
#include "es2/core/weapons.h"

/* Original DBSIM entries 47de72, 492aaf, 492934 and 413c68. See the
 * damage path contract in docs/reverse-engineering/player_movement.md.
 * All integer arguments retain their original bits. No alignment required. */
uint64_t es2_distance3_registers(uint32_t x, uint32_t y, uint32_t z);
uint64_t es2_distance3_forward_registers(uint32_t x, uint32_t y, uint32_t z);
uint64_t es2_heading_point_registers(const void *p, const void *q, const void *corrections);
/* Writes exactly one of the two shield words. Nonzero low front word selects
 * the first. EDX is either the multiply high word or division remainder. */
uint64_t es2_shield_damage_registers(void *shields, uint32_t front, uint32_t damage);

typedef enum es2_damage_event { ES2_DAMAGE_CALL, ES2_DAMAGE_SLOT } es2_damage_event;
typedef void (*es2_damage_observer)(void *, es2_damage_event, uint32_t);
typedef struct es2_damage_context {
    uint32_t generator, heading, shield, random, distance;
    uint32_t point; /* disjoint host-owned 12-byte guest scratch */
    es2_guest_call call;
    void *call_context;
    es2_damage_observer observe;
    void *observer_context;
} es2_damage_context;

/* Own control flow of vehicle +70 (4187d0) and building +70 (404f20).
 * Every game callee, including arithmetic leaves, uses call; callbacks may
 * mutate the graph. Neither body directly writes guest state except scratch
 * written by V58. Virtual targets are read immediately before each call.
 * Valid pointers and nonfaulting signed falloff division are required, just
 * as in the original. The low product is sign-extended before division.
 * Return values are the full path-dependent EDX:EAX, not success flags. */
uint64_t es2_vehicle_hit_registers(const es2_guest_memory *, const es2_damage_context *,
    uint32_t actor, uint32_t damage, uint32_t point, uint32_t range, uint32_t source);
uint64_t es2_building_hit_registers(const es2_guest_memory *, const es2_damage_context *,
    uint32_t actor, uint32_t damage, uint32_t point, uint32_t range, uint32_t source, uint32_t entry_edx);

/* Building V74's indexed state and arbitrary lifecycle writes require guest
 * memory/calls. player_slot and state_table are guest addresses, not copies. */
typedef uint64_t (*es2_damage_register_call)(void *,uint32_t,const uint32_t *,unsigned,const uint32_t[3]);
typedef struct es2_building_part_context {
    uint32_t player_slot,state_table,divide,multiply,random,generator,notify,deactivate,unlink;
    es2_guest_call call;
    void *call_context;
    /* Unlink 423430 can preserve entry EDX:EAX. Supply the caller's bits. */
    es2_damage_register_call call_registers;
} es2_building_part_context;
uint64_t es2_building_part_registers(const es2_guest_memory *,const es2_building_part_context *,
    uint32_t actor,uint32_t part,uint32_t damage,uint32_t source,uint32_t entry_eax);
/* Caller domain: equipment count 0..59 with stable occupied slots between
 * snapshots, attachment configuration count 0..4, valid signed-indexed part
 * storage and nonzero signed ratio denominators. These are not parsers. */
uint64_t es2_vehicle_part_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t part,uint32_t damage,uint32_t source,const uint32_t entry[3]);

/* The damage data (damage.cpp) and the part tables of the mission
 * load. Contract in docs/reverse-engineering/mission_load.md, section
 * "damage data and part tables"; DMG grammar in
 * docs/formats/collision.md. Addresses are guest addresses; g->image is the
 * loaded image base. Every game call goes through g->call with the
 * original's entry EAX/EDX/ECX and is reported to g->observe first
 * (reference VA, slot 0; the stream's read method reports slot 18h), so a
 * callee that is itself a hook keeps its own mode. `entry` holds the entry
 * EAX/EDX/ECX. Each function returns the original's EDX:EAX and, when `ecx`
 * is not NULL, stores the original's ECX there (the adapters pass NULL: ECX
 * is dead at every call site). */
/* dmg_load 40d160 (name): the runtime record (descriptor 498840h) at the
 * synthetic EBP f = g->scratch + ES2_DMG_LOAD_FRAME (the bridge must pass f
 * as EBP for 49630c; g->scratch needs ES2_DMG_LOAD_SCRATCH zeroed bytes),
 * the stream at f-30h (472f14), the error report 479c78 when it did not
 * open, the slot [4a9828h] + 12 x counter word 498854h (incremented),
 * dmg_read 40d09c, stream_destroy 472f64(S, 2). EAX = the slot, EDX = the
 * record's saved head, which the caller writes to FS:0. */
#define ES2_DMG_LOAD_SCRATCH 0x50u
#define ES2_DMG_LOAD_FRAME 0x50u
uint64_t es2_dmg_load_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t name,
    const uint32_t entry[3],uint32_t *ecx);
/* dmg_read 40d09c (T, S): capacity count +0Ah, capacities +4 (new[]),
 * region count +8, regions +0 (new[] of 18 bytes each), dmg_region_read
 * 40cff8 per region; reads through S's slot +18h. */
uint64_t es2_dmg_read_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t table,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx);
/* dmg_region_read 40cff8 (R, S): the 8-byte prefix, the link list (new[]
 * of 4 x the word R+6) into R+8, R+0Ch = FFFFh, R+0Eh = 0. */
uint64_t es2_dmg_region_read_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t region,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx);
/* dmg_region_copy 40d050 (D, R): _memcpy 49680e of 12h bytes, a new[]
 * link list of 4 x the word D+6, _memcpy of R's links into it. */
uint64_t es2_dmg_region_copy_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t dest,
    uint32_t source,const uint32_t entry[3],uint32_t *ecx);
/* dmg_capacity_set 40d128 (T, I, V, n): word V[k] into [T+4] + 2 sx(I[k]). */
uint64_t es2_dmg_capacity_set_registers(const es2_guest_memory *,uint32_t table,uint32_t indices,
    uint32_t values,uint32_t count,const uint32_t entry[3],uint32_t *ecx);
/* dmg_region_set 40d240 (T, I, P, n): dmg_region_copy 40d050([T] + 18
 * sx(I[k]), P[k]). */
uint64_t es2_dmg_region_set_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t table,
    uint32_t indices,uint32_t sources,uint32_t count,const uint32_t entry[3],uint32_t *ecx);
/* dmg_part_store 40d284 (T, n, I, W, Q): region [T] + 18 sx(I[k]) gets
 * word +0Ch = W[k] and dword +0Eh = Q[k]. */
uint64_t es2_dmg_part_store_registers(const es2_guest_memory *,uint32_t table,uint32_t count,
    uint32_t indices,uint32_t words,uint32_t dwords,const uint32_t entry[3],uint32_t *ecx);
/* col_part_gather 40cafc (C, I, O, P): for each 16-byte entry of the COL
 * record C: I[k] = entry word +6, P[k] = entry + 8, O[k] = the word C+0. */
uint64_t es2_col_part_gather_registers(const es2_guest_memory *,uint32_t record,uint32_t ids,
    uint32_t owners,uint32_t points,const uint32_t entry[3],uint32_t *ecx);
/* col_part_collect 40cb84 (L, I, O, P): col_part_gather 40cafc for each
 * 10-byte record of the list, the outputs advanced by the gathered count. */
uint64_t es2_col_part_collect_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t list,
    uint32_t ids,uint32_t owners,uint32_t points,const uint32_t entry[3],uint32_t *ecx);
/* damage_state_ctor 40d2cc (D, r, c): three new[] word arrays (c, r, r
 * words; low words of r and c), _memset 49681a to 0, 0 and 1, D+18h = r,
 * D+1Ah = c. */
uint64_t es2_damage_state_ctor_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t state,
    uint32_t regions,uint32_t capacities,const uint32_t entry[3],uint32_t *ecx);
/* damage_state_bind 40d354 (D, A, M, C): the runtime record (descriptor
 * 498878h) at f = g->scratch + ES2_DAMAGE_BIND_FRAME (ES2_DAMAGE_BIND_SCRATCH
 * zeroed bytes), D+0Ch = M, D+10h = C, D+14h = A, and once the shared
 * block [498864h] (new 47749c(6), new[] 4774f0(28h)). EDX = the record's
 * saved head, which the caller writes to FS:0. */
#define ES2_DAMAGE_BIND_SCRATCH 0x28u
#define ES2_DAMAGE_BIND_FRAME 0x24u
uint64_t es2_damage_state_bind_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t state,
    uint32_t owner,uint32_t dmg,uint32_t col,const uint32_t entry[3],uint32_t *ecx);
#endif
