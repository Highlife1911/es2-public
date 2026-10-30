#ifndef ES2_CORE_COLLISION_H
#define ES2_CORE_COLLISION_H
#include "es2/core/guest.h"
#include <stdint.h>

/* Calls the cdecl function at guest address `function` with `count` dword
 * arguments and returns its EDX:EAX. Arguments may be guest addresses of the
 * caller's own buffers: register-level functions run where guest and host
 * addresses coincide (the original process, the 32-bit harness). */
/* es2_guest_call is declared in guest.h. */

/* Optional diagnostics, independent of the host/proxy. CALL reports the
 * guest function address; SLOT reports the vtable offset (not its resolved
 * function, which can differ by class). The three path events have key 0:
 * contact entry, slope drift actually accumulated, and slide damage >250.
 * Called immediately before the call/path; must not mutate guest state.
 * A null observer has no effect. Results and call arguments are unchanged. */
typedef enum es2_collide_event {
    ES2_COLLIDE_CALL, ES2_COLLIDE_SLOT, ES2_COLLIDE_CONTACT,
    ES2_COLLIDE_SLOPE_DRIFT, ES2_COLLIDE_SLIDE_DAMAGE
} es2_collide_event;
typedef void (*es2_collide_observer)(void *context, es2_collide_event event, uint32_t key);

/* Guest addresses used by 0x418f74 (names are working hypotheses). */
typedef struct es2_collide_globals {
    uint32_t terrain;        /* 0x4a0be0: terrain grid pointer */
    uint32_t objects;        /* 0x4a9af8: pointer to the scene object list */
    uint32_t object_count;   /* 0x4a9afe: signed word, length of that list */
    uint32_t drift;          /* 0x4a9cd0: slope drift X, Y */
    uint32_t drift_enabled;  /* 0x4a9ce4 */
    uint32_t last_position;  /* 0x4a9cd8: X, Y, Z of the last grounded update */
    uint32_t damage_level;   /* 0x4a9e5c: signed word index into damage_table */
    uint32_t damage_table;   /* 0x49a058: signed words */
    uint32_t default_part;   /* 0x6c56a8: record used when a part lookup fails */
    uint32_t generator;      /* 0x4d2599: RNG state */
    uint32_t rng_bounded;    /* 0x492f24 */
    uint32_t buildings;      /* 0x404ae4: static obstacle test */
    uint32_t terrain_face;   /* 0x46e498: face record under a point */
    uint32_t effect;         /* 0x434010 */
    uint32_t sound;          /* 0x462830 */
    const void *quarter;     /* host pointer to the quarter sine table (0x4a25c4) */
    es2_collide_observer observe;
    void *observer_context;
} es2_collide_globals;

/* 0x418f74 collision test for ground vehicle A (cdecl, one argument), see
 * docs/reverse-engineering/player_movement.md. Returns EDX:EAX; AX is the
 * collision flag. The arithmetic helpers (0x49288c, 0x47ebb8, 0x47df19,
 * 0x480304, 0x47e0b0, 0x47de4c) run in core; virtual methods and the game
 * functions named in `g` go through `call`. */
uint64_t es2_ground_collide_registers(const es2_guest_memory *m, uint32_t actor,
                                      const es2_collide_globals *g, es2_guest_call call, void *context);

/* Sweep receiver object query (docs/reverse-engineering/weapons.md,
 * "Sweep receiver object query 427da8, grid sweep 4273c8, set_length
 * 4927f0"). Game calls go through g->call (hooked callees are entered as
 * hooks) with the EAX/EDX/ECX the original holds at each call; g->image is
 * the image base. All three return EDX:EAX; ECX at return is dead after
 * every caller and is not reproduced. Frame addresses in the returned
 * high halves are those of the replacement scratch.
 *
 * object_query 427da8 (out, segment record, object list, count word): needs
 * 0xa0 bytes of scratch. grid_sweep 4273c8 (shape, out, two points, radius
 * word): needs 0x50 bytes. set_length 4927f0 (vector, length word): no
 * scratch; only the CW3220 memset (zero vector) goes through g->call.
 * Division faults of the original (grid step 0, INT_MIN / -1) are outside
 * the contract. */
#include "es2/core/weapons.h"
uint64_t es2_object_query_registers(const es2_guest_memory *m, const es2_weapon_graph *g, uint32_t out,
                                    uint32_t segment, uint32_t list, uint32_t count, const uint32_t entry[3]);
uint64_t es2_grid_sweep_registers(const es2_guest_memory *m, const es2_weapon_graph *g, uint32_t shape,
                                  uint32_t out, uint32_t segment, uint32_t radius, const uint32_t entry[3]);
uint64_t es2_set_length_registers(const es2_guest_memory *m, const es2_weapon_graph *g, uint32_t vector,
                                  uint32_t length);
#endif
