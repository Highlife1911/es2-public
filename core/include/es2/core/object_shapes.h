#ifndef ES2_CORE_OBJECT_SHAPES_H
#define ES2_CORE_OBJECT_SHAPES_H
#include "es2/core/weapons.h"
/* The object shape set-up of the mission load: the TS_OBJ base
 * constructor, the three DET_ANIM_OBJ constructors, the animated shape
 * instance, an object's animation thread with the thread constructor and
 * the slot insert, and the shape word block. Contract in
 * docs/reverse-engineering/mission_load.md, section "object shape
 * set-up". Addresses are guest addresses; g->image is the loaded image
 * base (0x400000 in the original). Every game call goes through g->call
 * with the original's entry EAX/EDX/ECX and is reported to g->observe
 * first (reference VA, slot 0), so a callee that is itself a hook (the
 * other functions of this group, shape_instance, node_matrices,
 * object_init, track_clip, track_rate, track_sort) keeps its own mode.
 * `entry` holds the entry EAX/EDX/ECX. Each function returns the
 * original's EDX:EAX and, when `ecx` is not NULL, stores the original's
 * ECX there (ECX is dead at every caller, mission_load.md). "count +n"
 * is [[6CBEE4h]] += n, the pointer read each time. */

/* The two framed functions (ts_obj_ctor, object_track) set up the runtime
 * record through 49630c with the synthetic EBP f = g->scratch +
 * ES2_OBJECT_SHAPES_FRAME (the bridge passes it as EBP); the record lies at
 * f-24h (context word f-14h), the allocation slot at f-4. */
#define ES2_OBJECT_SHAPES_SCRATCH 0x48u
#define ES2_OBJECT_SHAPES_FRAME 0x40u

/* ts_obj_ctor 402278 (O, shape): the vtables DRAWABLE 4A14A4h, MAT_OBJ
 * 497289h, TS_OBJ 4A0B80h with count +1 after the first two, the zeroed
 * pose (O+0Ch dword, O+10h word, O+26h/2Ah/2Eh dwords, O+32h word), the
 * animated shape instance at O+34h (new 47749c(42h), then
 * anim_instance_ctor 478838, count -3; 0 without a shape or allocation),
 * memset(O+38h, 0, 8), byte O+39h = 1, count +1. EAX = O, EDX = the counter
 * pointer, ECX = the record's saved FS:0 head, which the caller writes back
 * to FS:0 as the original's epilogue does. */
uint64_t es2_ts_obj_ctor_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t object,
    uint32_t shape, const uint32_t entry[3], uint32_t *ecx);
/* det_anim_shape 40332c (O, shape): ts_obj_ctor(O, shape), then ANIM_OBJ
 * 4973ACh, count +1, DET_ANIM_OBJ 4973CCh, dword O+41h = 0, count +1.
 * det_anim_static 403368 (O, shape): the same over object_init 402188.
 * EAX = O, EDX = the counter pointer, ECX = 0. */
uint64_t es2_det_anim_shape_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t object,
    uint32_t shape, const uint32_t entry[3], uint32_t *ecx);
uint64_t es2_det_anim_static_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t object,
    uint32_t shape, const uint32_t entry[3], uint32_t *ecx);
/* det_anim_record 4033a4 (O, record): ts_obj_ctor(O, [[record]]), the two
 * vtables and counts as above, dword O+41h = record. EAX = O, EDX = ECX =
 * the counter pointer (read twice). */
uint64_t es2_det_anim_record_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t object,
    uint32_t record, const uint32_t entry[3], uint32_t *ecx);
/* anim_instance_ctor 478838 (I, shape): shape_instance 490b64(I, shape),
 * vtable 4A14C4h, the global rotation 6C56A8h (memset 20h, the three
 * diagonal words 4000h), the ten thread slots I+1Ah cleared, the node count
 * word I+0Ch = word [[I+4]+2Ah]+16h, the flag bytes new[] 4774f0 at I+0Eh,
 * the 12-byte pose array (49667c, element 4922e6) at I+12h, the 32-byte
 * matrix array (49667c, element matrix_ctor 495132) at I+16h, per node k the
 * pose copied from the shape's pose table and flag byte k = 1, then
 * node_matrices 478c64(I), memset of the first matrix with its three
 * diagonal words 4000h, count +1. EAX = I, EDX = the counter pointer, ECX =
 * the last memset's. */
uint64_t es2_anim_instance_ctor_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t instance,
    uint32_t shape, const uint32_t entry[3], uint32_t *ecx);
/* object_track 402374 (O, word): a thread T = new 47749c(52h), when not
 * null thread_ctor 478e48(T, [[[O+34h]+4]+2Ah]); track_clip 4792ac(T,
 * word, 0, 0), track_rate 4796d8(T, 0), thread_add 478a3c([O+34h], T).
 * EAX = T, EDX = the record's saved FS:0 head (the caller writes it back to
 * FS:0), ECX = thread_add's. */
uint64_t es2_object_track_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t object,
    uint32_t word, const uint32_t entry[3], uint32_t *ecx);
/* thread_add 478a3c (I, T): T into the first null of the ten slots
 * I+1Ah, then track_sort 4789d0(I), EAX = 1, EDX = track_sort's; no free
 * slot: EAX = 0, EDX = 10. ECX = I. */
uint64_t es2_thread_add_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t instance,
    uint32_t thread, const uint32_t entry[3], uint32_t *ecx);
/* thread_ctor 478e48 (T, M): T+0 = M, track_clip 4792ac(T, 0, 0, 0),
 * memset(T+2Eh, 0, 20h), words T+3Eh, T+34h, T+2Eh = 4000h. EAX = T, EDX =
 * memset's, ECX = memset's with the low word 4000h. */
uint64_t es2_thread_ctor_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t thread,
    uint32_t manager, const uint32_t entry[3], uint32_t *ecx);
/* shape_word_block 402fc0 (R, S): R+0 = S; for S not null R+4 = a copy of
 * the shape's word array ([S+1Ch], word S+24h words) in an arena block
 * (47a288 on the heap [6C5830h], memcpy 49680e), or 0 for an empty array;
 * count +1. EAX = R, EDX = the counter pointer, ECX = memcpy's or the
 * entry ECX. */
uint64_t es2_shape_word_block_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t block,
    uint32_t shape, const uint32_t entry[3], uint32_t *ecx);
#endif
