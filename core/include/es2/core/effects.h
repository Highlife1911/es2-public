#ifndef ES2_CORE_EFFECTS_H
#define ES2_CORE_EFFECTS_H
#include "es2/core/guest.h"

typedef enum es2_effects_event { ES2_EFFECTS_CALL, ES2_EFFECTS_SLOT } es2_effects_event;
typedef void (*es2_effects_observer)(void *context, es2_effects_event event, uint32_t key);
/* Nonnull registers supply EAX/EDX/ECX at entry. In particular, feedback's
 * early return preserves EDX, so an ordinary C call is not sufficient. */
typedef uint64_t (*es2_effects_call)(void *context, uint32_t function,
                                    const uint32_t *args, unsigned count,
                                    const uint32_t registers[3]);

/* All addresses are guest addresses. The host owns a disjoint 12-byte point
 * scratch area for the transform callback; no host pointer is narrowed by
 * core. Callbacks may change guest state and return arbitrary EDX:EAX.
 * The optional observer runs immediately before a call, cannot change guest
 * state, and receives the function address or virtual slot offset. */
typedef struct es2_effects_context {
    uint32_t reference_position; /* address of pointer at 0x4d24ea; XYZ at +4 */
    uint32_t default_part;       /* record at 0x6c56a8 */
    uint32_t point;              /* host-owned guest scratch, 12 bytes */
    uint32_t euler, transform, distance, sound, feedback;
    es2_effects_call call;
    void *call_context;
    es2_effects_observer observe;
    void *observer_context;
} es2_effects_context;

/* Original: DBSIM.EXE 0x4195c8, cdecl actor pointer. Updates attached
 * transforms before distance-gated animation latches and sound/feedback.
 * All five direct callees and virtual +0x20 remain host calls, so their
 * independent hook modes apply. The actor's cached matrix is rebuilt only
 * when its valid word is zero. Writes are ordered, including the Z/X/Y
 * attachment position, heading, cache flag, arm and post-callback clear.
 * Null attachments and distant unflagged actors do not advance the latch
 * pointer; animation/category skips do. Category 2 on the special animation
 * redirects that pointer but never fires. Signed words/bytes, wrapped
 * subtraction, and scratch return bits follow the documented contract.
 * The no-track path returns incoming EDX:EAX unchanged; otherwise AX is the
 * exit counter, while its high word and EDX retain path-dependent values.
 * See docs/reverse-engineering/player_movement.md, "Ground effects 4195c8".
 * Valid guest pointers and enough storage for the signed count are required;
 * original invalid-pointer faults are outside this API. */
uint64_t es2_ground_effects_registers(const es2_guest_memory *memory, uint32_t actor,
                                     const es2_effects_context *context,
                                     uint32_t eax, uint32_t edx);
#endif
