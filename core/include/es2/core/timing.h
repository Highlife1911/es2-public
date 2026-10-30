#ifndef ES2_CORE_TIMING_H
#define ES2_CORE_TIMING_H
#include <stdint.h>

/* Low words are interpreted as signed 16-bit x86 values. Scale returns the
 * complete EAX pattern (only its low word is clamped). Reference must have
 * a nonzero low word; original divide-fault/unwinding is outside this API. */
uint32_t es2_time_scale(uint16_t reference, uint16_t elapsed);

/* Timer buffers are byte addressed: untouched tag at +0, LE value at +1.
 * Accessible sizes: 3 bytes for word, 5 for dword/arm. No alignment required.
 * Steps subtract modulo their width, then clamp a set sign bit to zero.
 * Arm sign-extends the duration word before shifting modulo 32 bits. */
uint16_t es2_timer_word_step(void *timer, uint16_t scale);
uint32_t es2_timer_dword_step(void *timer, uint16_t scale);
void es2_timer_arm(void *timer, uint16_t duration);
#endif
