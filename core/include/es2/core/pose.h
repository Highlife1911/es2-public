#ifndef ES2_CORE_POSE_H
#define ES2_CORE_POSE_H
#include <stdint.h>
#include "es2/core/guest.h"
void es2_pose_interpolate(const void *first,const void *second,uint32_t fraction,void *output);
/* Nonzero wrapped (x1-x0), and signed32-fitting IDIV quotient required. */
uint32_t es2_scalar_interpolate(uint32_t x,uint32_t x0,uint32_t x1,uint32_t y0,uint32_t y1);
uint32_t es2_track_count(const void *manager);
uint32_t es2_pose_interpolate_registers(const void *,const void *,uint32_t,void *);
uint64_t es2_scalar_interpolate_registers(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);

/* 0x478ef4: select the pose of track T for (clip, frame) and copy its
 * translation to T+0x22..0x27 and its angles to T+0x28..0x2d, then set the
 * word T+0x20 to 1; a clip whose word +0x08 is zero only clears T+0x20.
 * Reads and writes happen in the original order through guest memory, so
 * aliasing between T and the resource data behaves as in the original.
 * Result: EAX=T; EDX=clip (disabled clip) or 3*pose index. */
uint64_t es2_clip_pose_registers(const es2_guest_memory *memory,uint32_t track,uint32_t clip,uint32_t frame);
#endif
