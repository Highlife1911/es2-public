#ifndef ES2_CORE_HUD_HELPERS_H
#define ES2_CORE_HUD_HELPERS_H
#include "es2/core/guest.h"
/* Pure helpers of the cockpit/HUD region. Contract in
 * docs/reverse-engineering/hud.md (section "HUD-region helpers").
 * Addresses are guest addresses; `image` is the loaded image base
 * (0x400000 in the original), used for the cockpit pointer 49b088 and the
 * view size and scroll dwords 4d253e/4d2542 and 4d2556/4d255a.
 *
 * panel_visible 431410 (rect): 1 when the top-left or the bottom-right
 * corner of the rectangle (four dwords), moved by the scroll offset, lies
 * inside the cockpit canvas view (edges inclusive, signed), else 0; 0 when
 * the panel-off byte H+20Fh is set. Reads only. EDX:EAX as the original:
 * EDX is `entry_edx` when the panel is off, [4d2556] after a top-left hit,
 * else the moved y2 and, once y2 passed both tests and x2 is not left of
 * the view, the moved x2. ECX and the flags are dead at all 38 call sites
 * and are not reproduced. */
uint64_t es2_panel_visible_registers(const es2_guest_memory *, uint32_t image, uint32_t rect, uint32_t entry_edx);
#endif
