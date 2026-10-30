#ifndef ES2_CORE_CAMERA_H
#define ES2_CORE_CAMERA_H
#include "es2/core/weapons.h"
/* 0x4011a0: per-frame camera update (docs/reverse-engineering/camera.md).
 * Records the followed object's position and angles in two 50-entry rings,
 * then places the camera C by its mode word C+0x36: 0 free flight, 1 orbit
 * around the target C+0x32, 2 attached to the target (virtual +0x24 picks an
 * optional mount matrix), 3 delayed replay from the rings; other modes only
 * record. g->scratch holds 0x1c0 bytes of frame (EBP-0x1c0..EBP) at the
 * original offsets, because callees receive pointers into it. Every game
 * call, including hooked ones, goes through g->call with the original entry
 * EAX/EDX/ECX; the Q16 helpers 47e068/47e08d are computed here. The ring
 * index [0x4a8048] must be 0..49 and the step ring must let mode 3's
 * backward walk end (see camera.md). Returns EDX:EAX as the original. */
uint64_t es2_camera_update_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t camera);
#endif
