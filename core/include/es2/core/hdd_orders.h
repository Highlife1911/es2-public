#ifndef ES2_CORE_HDD_ORDERS_H
#define ES2_CORE_HDD_ORDERS_H
#include "es2/core/weapons.h"
/* The HDD map order controls of the HDDMap object M. Contract in
 * docs/reverse-engineering/cockpit.md, section "HDD map order
 * controls". Addresses are guest addresses; g->image is the loaded image
 * base (0x400000 in the original), used for the globals 49d6b0 (the map
 * gadget holder), 49b088 (the graphics context) and the text table
 * 4d1638. Every game call goes through g->call with the original's entry
 * EAX/EDX/ECX and is reported to g->observe first; the in-group callees
 * map_mode 44d9f4 and wingman_select 44da98 are called the same way, so a
 * hooked callee keeps its own mode. `entry` holds the entry EAX/EDX/ECX.
 * Each function returns the original's EDX:EAX; ECX is dead at every
 * caller and not reproduced. */

/* map_areas 44d450 (M): handles a pending gadget event (M+14Eh, point
 * M+152h): a hit on one of the eight mode buttons sets that map mode
 * (44d9f4), sends (438d9c with button 0Dh) or shows the "no wingman"
 * label (44dc6c), and may warp the mouse to the map centre (486e7c with an
 * 8-byte local at g->scratch); an event on the map area stores the click
 * point for map_click. Always clears M+14Dh. */
uint64_t es2_map_areas_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t map,
    const uint32_t entry[3]);
/* map_mode 44d9f4 (M, mode): cursor shape 433e3c, label 44dc6c or 44dd14,
 * the pick flags M+103h/M+104h/M+380h, and on a change M+81h/M+5Dh and the
 * map gadget's redraw bytes. Returns EAX = the old mode. */
uint64_t es2_map_mode_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t map,
    uint32_t mode, const uint32_t entry[3]);
/* wingman_select 44da98 (M, index): deselects the old wingman (44ede0,
 * map mode -1 and the record's pending byte when a mode is set), stores
 * M+105h, selects the new one (44ede0, 44ee00) or disables the buttons
 * (44ee24), requests the gadget redraw. Returns EAX = the gadget, EDX =
 * [49d6b0]. */
uint64_t es2_wingman_select_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t map,
    uint32_t index, const uint32_t entry[3]);
/* order_cancel 44dc10 (M): map mode -1, the selected record's pending
 * byte, wingman_select(M, -1), then clears the order flags. Returns
 * wingman_select's EDX:EAX. */
uint64_t es2_order_cancel_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t map,
    const uint32_t entry[3]);

/* hdd_key 449ff4 (H, code): the HDD gauge's key method (vtable
 * 49d64c slot +0Ch, offered by hud_key in display mode 1). By the low word
 * of code: the arrows 48h/4Bh/4Dh/50h and keypad -/+ 4Ah/4Eh, the keys
 * -/= 0Ch/0Dh and 1/2/3 (02h..04h) press HDD buttons 2..7 and 0Ah..0Ch
 * (button_press 438d9c); W/I/S (11h/17h/1Fh) show page 1 (44a60c) and set
 * its view 2/1/0 (450b88); every other code goes to the active page's key
 * slot +10h(page, code), page = [H+101h+4*[H+109h]], whose EDX:EAX is
 * returned. Handled keys return AL = 1 over the last callee's EAX. */
uint64_t es2_hdd_key_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t hdd,
    uint32_t code, const uint32_t entry[3]);
#endif
