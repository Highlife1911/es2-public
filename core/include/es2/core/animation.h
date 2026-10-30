#ifndef ES2_CORE_ANIMATION_H
#define ES2_CORE_ANIMATION_H
#include <stdint.h>
#include "es2/core/guest.h"

/* Animation manager M: animation matrix pointer at M+0x16, first track
 * pointer at M+0x1a (zero when the manager has no track). Actor A: angles
 * A+0x0c..0x11, cached rotation matrix A+0x12..0x31 whose translation
 * A+0x26..0x31 is the world position, cache flag word A+0x32. Objects are
 * addressed by their 32-bit guest addresses; results are EDX:EAX. */

typedef struct es2_math_tables {
    const void *quarter;      /* heading quarter table (Euler) */
    const void *large;        /* inverse-sine tables and angle corrections */
    const void *small;
    const void *corrections;
} es2_math_tables;

/* 0x478b7c: if M has a first track, run the scaled inverse track transform
 * against `input` (track_scaled_inverse, see track.h), then copy `input`
 * into M's animation matrix (bytes 0..19, then translation Z, X, Y).
 * Returns EAX=0 with the incoming EDX when there is no track, otherwise
 * EAX = matrix+0x14, EDX = matrix. */
uint64_t es2_manager_reset_registers(const es2_guest_memory *memory, uint32_t manager,
                                     const void *input, const void *quarter_table,
                                     uint8_t padding, uint32_t edx);

/* Advances all tracks of M by `step` (0x478d38). Supplied by the host, which
 * normally calls the game's function at 0x478d38 (hooked or not), so the
 * two replacements stay independently selectable. */
typedef void (*es2_track_advance)(void *context, uint32_t manager, uint32_t step);

/* Clip objects (ANAnimList resource R = [T+0], clip pointer array [R+6]) are
 * ANSequence or ANCyclicSequence instances. Their virtual methods are
 * reproduced for the two known vtables (frame count = word at clip+0x0a):
 *   0x20 next(frame), 0x24 previous(frame): ANSequence stops at the ends,
 *        ANCyclicSequence wraps;
 *   0x28 reached(frame, target, time) and 0x2c (the same for backward
 *        play): ANSequence compares frames, ANCyclicSequence always 1.
 * Any other vtable goes to `other`, which calls the original method with
 * one (0x20/0x24) or three (0x28/0x2c) arguments and returns EDX:EAX. */
typedef struct es2_clip_classes {
    uint32_t sequence;   /* ANSequence vtable */
    uint32_t cyclic;     /* ANCyclicSequence vtable */
    uint64_t (*other)(void *context, uint32_t clip, uint32_t slot, const uint32_t args[3]);
    void *context;
} es2_clip_classes;

/* One clip virtual call with full 32-bit argument slots. Some methods leave
 * EAX or EDX as they were at the call, so the caller supplies those values.
 * Returns EDX:EAX after the method. */
uint64_t es2_clip_call(const es2_guest_memory *memory, const es2_clip_classes *classes,
                       uint32_t clip, uint32_t slot, const uint32_t args[3],
                       uint32_t eax, uint32_t edx);

/* 0x478d38 track advance for manager M (fields: shape [M+4] with node
 * hierarchy [[M+4]+0x2a], node count word M+0x0c, node flag bytes [M+0x0e],
 * 12-byte node poses [M+0x12], 32-byte node matrices [M+0x16], up to ten
 * track pointers M+0x1a..0x41 ending at the first zero):
 *  1. every track from last to first runs its clip state machine (0x479720)
 *     for `step` (low word; scaled by the track rate T+0x4e / 256),
 *  2. tracks are sorted by their clip's priority word (0x4789d0),
 *  3. the first track's scaled transform goes to node matrix 0 (0x4790b4),
 *  4. every track writes its interpolated poses and sets node flag 1 (0x479ab0),
 *  5. the hierarchy rebuilds the matrices of changed nodes and clears the
 *     flags (0x478c64; flag 2 marks a rebuilt node for its children).
 * Divisions by a zero frame duration (T+0x1e) fault in the original and are
 * a precondition here. `padding` follows the padding contract of track.h.
 * Returns EAX = flag array, EDX = sign-extended node count (memset result). */
uint64_t es2_track_advance_registers(const es2_guest_memory *memory, uint32_t manager,
                                     uint32_t step, const es2_clip_classes *classes,
                                     const void *quarter_table, uint8_t padding);

/* The parts of 0x478d38 as standalone functions with their exact EDX:EAX
 * (track_advance calls them directly). Callees run in core: clip_pose,
 * track_forward/inverse/scaled, pose_parent, pose_matrix; frame steps
 * through `classes` with the original's full 32-bit slots. Address-valued
 * results of the track.h wrappers are host addresses (identity in the
 * proxy and in the in-vitro harness). */

/* 0x479720: advance track T by the low word of `step` times the rate
 * T+0x4e / 256 (step 1 above). `step` is the full argument slot and `edx`
 * EDX at entry; both are the result when the track is stopped (state bit 0
 * with stop flag bit 0). A zero frame duration ends a forward step; going
 * backward through one never ends (the original loops forever), which is
 * outside the contract. */
uint64_t es2_track_step_registers(const es2_guest_memory *memory, const es2_clip_classes *classes,
                                  uint32_t track, uint32_t step, uint32_t edx,
                                  const void *quarter_table, uint8_t padding);

/* 0x4789d0: stable bubble sort of M's track pointers by the signed priority
 * word clip+6 of each track's current clip (step 2), repeated until a pass
 * swaps nothing. With fewer than two tracks EAX = count and EDX =
 * M+0x1a+4*count (0x4789b0's registers); otherwise the registers of 0x478e8c
 * for the last track: EAX = high word of its clip pointer : priority word,
 * EDX = sign-extended clip index. */
uint64_t es2_track_sort_registers(const es2_guest_memory *memory, uint32_t manager);

/* 0x478aac: steps 1-3. Every track from last to first runs 0x479720 with
 * EDX = T and a step slot that keeps the high word of the previous EAX,
 * then the sort; with a first track the result is 0x4790b4's, otherwise
 * EAX = 0 and EDX = M+0x1a. */
uint64_t es2_track_list_registers(const es2_guest_memory *memory, uint32_t manager, uint32_t step,
                                  const es2_clip_classes *classes, const void *quarter_table,
                                  uint8_t padding);

/* 0x478c64: step 5, also called by the manager constructor 0x478838. A
 * negative node count word M+0x0c is outside the contract (the runtime
 * memset would clear about 4 GiB). `padding` is the stack residue the child
 * calls see before the first root call; after it they see the high byte of
 * that root's matrix address. Returns EAX = flags, EDX = count. */
uint64_t es2_node_matrices_registers(const es2_guest_memory *memory, uint32_t manager,
                                     const void *quarter_table, uint8_t padding);

/* Low word of next (slot 0x20, `forward`) or previous (0x24) frame. */
uint16_t es2_clip_step(const es2_guest_memory *memory, const es2_clip_classes *classes,
                       uint32_t clip, int forward, uint16_t frame);

/* 0x4792ac: initialize track T to the low words of clip/frame/phase.
 * Call the selected clip's virtual +0x20 (clip, frame), retaining entry
 * EAX's upper word in that frame slot; then reread the track/resource
 * graph for the duration and call clip_pose(T, s16 clip, s16 frame).
 * Reset the stop/transition words afterwards, preserving clip_pose's
 * complete EDX:EAX. Both callees use the host callback so their hook modes
 * remain independent. The host supplies the address of 0x478ef4.
 * All indexed objects must exist; the original has no bounds checks. */
uint64_t es2_track_clip_registers(const es2_guest_memory *memory, uint32_t track,
                                  uint32_t clip, uint32_t frame, uint32_t phase,
                                  uint32_t eax, uint32_t clip_pose,
                                  es2_guest_call call, void *context);

/* 0x47967c: set the stop target of track T (clip a, frame b, flags c; low
 * words), clear the stopped bit, then either mark the track stopped (target
 * clip 0xffff, or already at the target with time 0) or search the route
 * to the target (0x4793d4): from the next frame (rate >= 0) or the current
 * frame (rate < 0), walk the clip frame by frame through each frame's
 * transitions in [R+0x1e] (8-byte records: duration, target clip, target
 * frame, pose) until one leads to the target clip and, unless the target
 * frame is 0xffff, the clip's reached method (0x28/0x2c) accepts it; that
 * transition is stored as T+0x0c..0x14. Without one the trigger is 0xffff.
 * `edx` is EDX at entry (its high word survives on the early exits). */
uint64_t es2_track_target_registers(const es2_guest_memory *memory, const es2_clip_classes *classes,
                                    uint32_t track, uint32_t clip, uint32_t frame, uint32_t flags,
                                    uint32_t edx);

/* 0x4796d8: store rate T+0x4e; when its sign changes, re-run 0x47967c with
 * the current target. Returns EAX = T, EDX = the rate argument, or the
 * result of 0x47967c. */
uint64_t es2_track_rate_registers(const es2_guest_memory *memory, const es2_clip_classes *classes,
                                  uint32_t track, uint32_t rate);

/* Host access to 0x40250c (animation_step) for the actor-level functions;
 * normally the game's function, hooked or not. Returns EDX:EAX. */
typedef uint64_t (*es2_actor_step)(void *context, uint32_t actor, uint32_t manager, uint32_t step);
/* Host access to 0x4796d8 and 0x402604 for 0x418f40. */
typedef uint64_t (*es2_actor_rate)(void *context, uint32_t track, uint32_t rate);
typedef uint64_t (*es2_actor_animate)(void *context, uint32_t actor, uint32_t step);

/* 0x402604: if byte A+0x39 is set, animation_step(A, [A+0x34], step);
 * otherwise EAX = A, EDX = A+0x38. */
uint64_t es2_actor_animate_registers(const es2_guest_memory *memory, uint32_t actor, uint32_t step,
                                     es2_actor_step animation_step, void *context);

/* Snapshot block (guest address `block`, 0x4a8cd0 in DBSIM): actor matrix
 * +0x000 (32 bytes), ten 82-byte track states +0x020, node flags +0x354,
 * node poses +0x372 (12 bytes each), node matrices +0x4da (32 bytes each),
 * for the manager M = [A+0x34] with n = s16 M+0x0c nodes (no bound check).
 * 0x402628 saves actor A into the block, 0x4027fc restores it. Both first
 * build the cached actor matrix (Euler from A+0x0c into A+0x12, A+0x32 = 1)
 * if A+0x32 is zero; empty track slots are skipped. The node arrays are
 * copied with the runtime's memcpy (dwords forward, then bytes), whose
 * result is returned: EAX = matrix destination, EDX = 32 * n. */
uint64_t es2_actor_snapshot_registers(const es2_guest_memory *memory, uint32_t actor, uint32_t block,
                                      const void *quarter_table);
uint64_t es2_actor_rollback_registers(const es2_guest_memory *memory, uint32_t actor, uint32_t block,
                                      const void *quarter_table);

/* Callees of the ground vehicle update 0x41a360, normally the game's
 * functions (hooked or not). Each returns EDX:EAX. `height` receives the
 * point by value; the host passes it to 0x46e180 through its own memory. */
typedef struct es2_ground_calls {
    uint64_t (*snapshot)(void *context, uint32_t actor);                      /* 0x402628 */
    uint64_t (*animate)(void *context, uint32_t actor);                       /* 0x418f40 */
    uint64_t (*height)(void *context, uint32_t terrain, uint32_t x, uint32_t y, uint32_t z); /* 0x46e180 */
    uint64_t (*collide)(void *context, uint32_t actor);                       /* 0x418f74 */
    uint64_t (*rollback)(void *context, uint32_t actor);                      /* 0x4027fc */
    uint64_t (*random)(void *context, uint32_t generator);                    /* 0x492ee0 */
    uint64_t (*sound)(void *context, uint32_t id);                            /* 0x462830 */
    uint64_t (*clip)(void *context, uint32_t track, uint32_t clip, uint32_t frame, uint32_t phase); /* 0x4792ac */
    /* Entry registers matter when effects finds no track. */
    uint64_t (*effects)(void *context, uint32_t actor, uint32_t eax, uint32_t edx); /* 0x4195c8 */
    void *context;
} es2_ground_calls;

/* Guest addresses of the globals 0x41a360 uses. */
typedef struct es2_ground_globals {
    uint32_t terrain;        /* 0x4a0be0: terrain grid pointer */
    uint32_t drift;          /* 0x4a9cd0: X and Y added to the probe point ... */
    uint32_t drift_enabled;  /* 0x4a9ce4: ... when this dword is nonzero */
    uint32_t last_position;  /* 0x4a9cd8: X, Y, Z written while byte A+0xa3 is set */
    uint32_t generator;      /* 0x4d2599: RNG for the collision recoil */
} es2_ground_globals;

/* 0x41a360 ground vehicle update (actor callback): snapshot, ground
 * animation, place the actor on the terrain (height + s16 [[A+0x1f2]+0x16]),
 * then on collision roll back and either set a random recoil (on the ground,
 * byte A+0xa3 clear) or play sound 0x29, and either switch the track to the
 * vehicle's clip (track in transition) or clamp and reverse the speed and
 * animate once more (a second collision rolls back and stops). Finally the
 * effects call. Returns AX = 1 with the high word of the effects' EAX and
 * the effects' EDX. */
uint64_t es2_ground_update_registers(const es2_guest_memory *memory, uint32_t actor,
                                     const es2_ground_globals *globals, const es2_ground_calls *calls);

/* 0x418f40 (ground vehicle): set the rate of track [A+0x22c] to word
 * A+0x2a0 (0x4796d8), take movement_step(100) with time scale `scale`
 * (word 0x4d3b64), then actor_animate(A, that step). `eax` is EAX at
 * entry (the high word of the pushed rate). Returns actor_animate's EDX:EAX. */
uint64_t es2_ground_animate_registers(const es2_guest_memory *memory, uint32_t actor, uint16_t scale,
                                      uint32_t eax, es2_actor_rate rate, es2_actor_animate animate,
                                      void *context);

/* 0x40250c: reset M's animation matrix to identity through manager_reset,
 * advance the tracks, then apply the accumulated animation transform to the
 * actor: position = actor rotation * animation translation + position
 * (written Z, X, Y), angles += angles of the animation matrix, cache flag
 * cleared, animation matrix reset to identity. Returns EAX = animation
 * matrix, EDX = 0x20 (the runtime memset's count). */
uint64_t es2_animation_step_registers(const es2_guest_memory *memory, uint32_t actor,
                                      uint32_t manager, uint32_t step,
                                      const es2_math_tables *tables,
                                      es2_track_advance advance, void *context,
                                      uint8_t padding);
/* Parts of track_target and track_advance as standalone functions
 * (contracts: player_movement.md, "Track and pose remainder").
 * 0x4793d4: the route search of track_target, returning its EDX:EAX. The
 * time slot of the first reached call carries the caller's EDI high word in
 * the original; here it is 0 (only an unknown clip class can observe it). */
uint64_t es2_track_route_registers(const es2_guest_memory *memory, const es2_clip_classes *classes,
                                   uint32_t track);
/* 0x479ab0: interpolated node poses of track T into `poses` (12 bytes per
 * node) with flag byte 1 at `flags`; a zero frame duration T+0x1e faults in
 * the original (precondition). EAX = 0, EDX = map for no bones; otherwise
 * from the last bone: EAX = s16 node, EDX = flags, or for a skipped node 0
 * the two frame words over their addresses' high words. */
uint64_t es2_track_poses_registers(const es2_guest_memory *memory, uint32_t track, uint32_t poses,
                                   uint32_t flags);
/* 0x478b00: es2_track_poses_registers for every track of M from last to
 * first; without tracks EAX = 0, EDX = M+0x1a (0x4789b0's registers). */
uint64_t es2_pose_list_registers(const es2_guest_memory *memory, uint32_t manager);
/* One clip method by its address (0x478760/0x47877c/0x47878c/
 * 0x4787b8 ANSequence +20h..+2Ch, 0x4787e4/0x478804/0x478820/0x47882c
 * ANCyclicSequence), whatever the clip's vtable; args = the slots after the
 * clip, entry = EAX/EDX/ECX at entry. Returns EDX:EAX, *ecx = ECX
 * (main_loop.md, section "the stage-5 flushes and the manager slot
 * methods"); another address returns the entry
 * registers. */
uint64_t es2_clip_method_registers(const es2_guest_memory *memory, uint32_t method, uint32_t clip,
                                   const uint32_t args[3], const uint32_t entry[3], uint32_t *ecx);
#endif
