#ifndef ES2_CORE_KEY_AXES_H
#define ES2_CORE_KEY_AXES_H
#include "es2/core/guest.h"
/* The keyboard axes of the control record. Contract in
 * docs/reverse-engineering/input.md (section "keyboard axes
 * 45a4cc"). Addresses are guest addresses; `image` is the loaded image
 * base (0x400000 in the original), used for the key-state arrays
 * 4d2394 (down), 4d23a3 (released before seen), 4d23b2 (seen), the Alt
 * word 4d23d6 and the tables 49eb3d (exclusive flags) and 49eb45 (signed
 * byte pairs).
 *
 * key_axes 45a4cc (block, flag): clears the axis dwords block+0..+0Fh and
 * the bytes +10h/+11h, adds the pairs of the held pad keys 0..7 (an
 * exclusive key restarts the sum and ends the pad scan) to +0/+4 and of
 * the first held key 8..13 to +8/+0Ch (shifted left by 7), marks each
 * visited held key as seen, sets +10h/+11h from the bytes 4d23a2/4d23a3,
 * clears +12h/+13h and drops the keys that are down and were released
 * before they were seen. `flag` has no effect (its only test is dead).
 * Reads and writes follow the original's order, so an overlapping block
 * behaves the same. Returns EDX:EAX = image VA 4d23a3 : 0Fh; ECX
 * (4d23b2) and the flags are dead at the only caller and not
 * reproduced. */
uint64_t es2_key_axes_registers(const es2_guest_memory *, uint32_t image, uint32_t block, uint32_t flag);
#endif
