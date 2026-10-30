#ifndef ES2_CORE_TRACK_H
#define ES2_CORE_TRACK_H
#include <stdint.h>

/* Matrix wrappers around track objects T (pose words T+0x22..0x2d, enable
 * word T+0x20, scale words T+0x1c/0x1e, matrix T+0x2e..0x4d) and around pose
 * records. Each builds a local 32-byte matrix (see matrix.h) on the stack.
 * The original never initializes byte 19 (padding) of those locals, so on
 * some paths the output matrix's byte 19 receives stack residue. `padding`
 * is the value such an uninitialized local byte holds; every other output
 * byte is deterministic. Results are EDX:EAX as seen by a 32-bit caller;
 * `eax`/`edx` are the incoming register values that disabled paths keep.
 * `table` is the heading quarter table used by the Euler step. */
uint64_t es2_pose_parent_registers(const void *pose, const void *parent, void *out,
                                   const void *table, uint8_t padding);           /* 0x478bc4 */
uint64_t es2_track_forward_registers(void *track, const void *table, uint8_t padding,
                                     uint32_t eax, uint32_t edx);                  /* 0x478f6c */
uint64_t es2_track_inverse_registers(void *track, const void *table, uint8_t padding,
                                     uint32_t eax, uint32_t edx);                  /* 0x478ff4 */
/* The scaled variants divide by s16(T+0x1e) when enabled; zero faults in the
 * original (#DE) and is a precondition here. */
uint64_t es2_track_scaled_registers(void *track, void *out, const void *table,
                                    uint8_t padding);                              /* 0x4790b4 */
uint64_t es2_track_scaled_inverse_registers(void *track, const void *input, const void *table,
                                            uint8_t padding);                      /* 0x479194 */
#endif
