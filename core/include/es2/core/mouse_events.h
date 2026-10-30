#ifndef ES2_CORE_MOUSE_EVENTS_H
#define ES2_CORE_MOUSE_EVENTS_H
#include "es2/core/weapons.h"
/* The mouse event queue of the modal GUI screens
 * (docs/reverse-engineering/input.md, section "mouse event queue
 * 452d40 / 453078"). Events are 14 bytes: x dword, y dword, button word,
 * time dword. The write list [4d1e04] and the read list [4d1e08] point at
 * headers (count dword, entries pointer).
 *
 * es2_mouse_dispatch_registers (452d40, no arguments, called by the
 * control record 45a810): swaps the lists, empties the new write list and
 * dispatches every event of the read list to the active screen [49dbd8]:
 * cursor position, presses, moves, releases, double clicks and dragging.
 * Every game call (the cursor follow 486e7c, the screen hit test 4523b0,
 * the move 45297c, press 4527c8 and release 452898 handlers, the redraw
 * 452ab8 and the gadget virtuals +8, +10h, +18h) goes through g->call with
 * the EAX/EDX/ECX the original holds there; direct calls are observed
 * under their original VA, virtual calls under their slot. g->image
 * locates the globals; g->scratch holds the original's 18h-byte frame
 * (the drag point at +8 is passed by address). Returns EDX:EAX; *ecx is
 * the original's ECX at RET (the read list's count).
 *
 * es2_mouse_queue_registers (453078, cdecl (event, value)): appends one
 * event to the write list unless its count is exactly 99. Returns
 * EDX:EAX; *ecx is the original's ECX at RET. image locates 4d1e04. */
#define ES2_MOUSE_DISPATCH_SCRATCH 0x18u
uint64_t es2_mouse_dispatch_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t *ecx,
    const uint32_t entry[3]);
uint64_t es2_mouse_queue_registers(const es2_guest_memory *,uint32_t image,uint32_t event,uint32_t value,
    uint32_t *ecx,const uint32_t entry[3]);
#endif
