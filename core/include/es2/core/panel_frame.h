#ifndef ES2_CORE_PANEL_FRAME_H
#define ES2_CORE_PANEL_FRAME_H
#include "es2/core/weapons.h"
/* The cockpit panel frame 0x4327ac(H) (docs/reverse-engineering/
 * hud.md, section "cockpit panel frame as own code").
 *
 * Returns at once on byte 0x49b104. Otherwise: the scroll-zone setup
 * 0x433770 once (H+0x23e <- 1), the startup sequence 0x432924 until
 * H+0x245, the jolt 0x43408c and bump 0x434194; with the panel-off byte
 * H+0x20f only the virtual +8 of [H+0x1f9]; else the port virtual +0x14
 * of [H+0x207], the context push 0x4311e0, the virtual +8 of the ten
 * elements H+0x70[i] (null skipped) and of [H+0x1e1], [H+0x1e5],
 * [H+0x1e9], [H+0x1f1], [H+0x1ed], [H+0x1f9], the context pop 0x431210,
 * the cursor 0x433c54, the target check cockpit_valid 0x433174(H, T) for
 * T = [H+0x210] != 0 (AL = 0 -> H+0x210 <- 0), the button releases
 * 0x4530fc([H]) and the nav marker 0x4349ac. Returns the original's
 * EDX:EAX; ECX is dead at the only caller and not reproduced.
 *
 * Every game call goes through g->call (observed first: direct calls by
 * their original VA with slot 0, virtual calls by the resolved target and
 * the slot) with the EAX/EDX/ECX the original holds there, so a hooked
 * callee keeps its own mode. g->image is the loaded image base (globals and
 * direct callees are relocated by it).
 *
 * frame (NULL for a plain bridge): before every game call the core stores
 * there the EBX, ESI and EDI that 0x4327ac holds at that call and the
 * return address of its CALL (an image VA relative to g->image), so a
 * bridge that runs the call at the original stack coordinate can load
 * them. caller_esi/caller_edi are the caller's ESI/EDI (held up to
 * 0x4311e0). With frame->top nonzero (the original's outgoing ESP), ECX
 * after a one-slot call is re-read from the argument slot top-4 (the
 * original's POP ECX); otherwise it is the pushed argument. */
typedef struct es2_panel_frame { es2_weapon_frame call; uint32_t top; } es2_panel_frame;
uint64_t es2_panel_frame_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_panel_frame *frame,
    uint32_t caller_esi, uint32_t caller_edi, uint32_t hud, const uint32_t entry[3]);
#endif
