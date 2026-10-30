#ifndef ES2_CORE_MFD_KEYS_H
#define ES2_CORE_MFD_KEYS_H
#include "es2/core/mfd_orders.h"
/* The MFD's key handlers. Contract in
 * docs/reverse-engineering/cockpit.md, section "MFD keys". G is the
 * MFDGauge (the cockpit's [H+1EDh]), its menu [G+D1h]. Addresses are guest
 * addresses; g->image is the loaded image base (0x400000 in the original),
 * used for the enable table 49cbd8 (13 bytes per MFD mode). Every game call
 * goes through g->call with the original's EAX/EDX/ECX and is reported to
 * g->observe first under its VA, so a hooked callee keeps its own mode.
 * Each function returns the original's EDX:EAX; `entry` holds the entry
 * EAX/EDX/ECX. frame (es2_mfd_frame, mfd_orders.h): the original stack
 * coordinate of an entry shim (EBX/ESI/EDI and return address of each
 * call, POP ECX rereads [top-4]); NULL without the shim. caller_esi /
 * caller_edi: the caller's ESI and EDI, which reach the callees where the
 * function does not change them. */

/* mfd_key 4469e8 (G, key): slot +0Ch of the MFDGauge table 49cfa0 (the key
 * method hud_key offers a key to). AL = 0 when panel_visible 431410(G+EBh)
 * gives AL = 0. Key word 20h, 213h, 214h, 2Dh: when the enable byte
 * 49cbdf + 13*[G+C1h] + (0, 1, 2, 3) is set, button_press 438d9c(G, 7, 8,
 * 9 or 10, 1); 213h with the byte clear: mfd_range 446ff0(G); 214h, 20h and
 * 2Dh with the byte clear: nothing. In MFD mode 1 ([G+C1h] = 1): 34h the
 * next entry 44703c(G), 33h the previous 447070(G), each followed by byte
 * G+E5h+[G+C1h] = 1; the key words 12h..2Eh through the byte table 446b78
 * and the jump table 446b95: 12h and 2Eh entry 4, 18h 3, 1Eh 0, 21h 5,
 * 22h 1, 23h 2 (mfd_key_select 447158(G, [G+FBh], n)). A handled key gives
 * AL = 1 over the upper bytes of the current EAX; any other key AL = 0. */
uint64_t es2_mfd_key_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mfd_frame *frame,
    uint32_t caller_edi, uint32_t gauge, uint32_t key, const uint32_t entry[3]);
/* mfd_alt_key 446c38 (G, code): hud_key's Alt order keys. ESI = the menu
 * [G+D1h] (read first, on every path). code 212h..22Eh through the byte
 * table 446c65 and the jump table 446c82: 21Eh entry 0, 222h 1, 223h 2,
 * 218h 3, 221h 5: menu_select 43f9e0(menu, n), mfd_order 447248(G),
 * mfd_key_select 447158(G, [G+FBh], n); 22Eh and 212h: menu_select(menu,
 * 4), then menu_code 43f9c0(menu), and only if it gives 4 (22Eh) or 7
 * (212h) mfd_order(G) and mfd_key_select(G, [G+FBh], 4). These give AL = 1
 * over the upper bytes of the current EAX; every other code EAX = 0 with
 * the entry EDX. */
uint64_t es2_mfd_alt_key_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mfd_frame *frame,
    uint32_t caller_edi, uint32_t gauge, uint32_t code, const uint32_t entry[3]);
/* mfd_key_select 447158 (G, o, n): when o is [G+FBh] and [G+C1h] is 1,
 * mfd_entry 4470a4(G, n) (EDX = n at the call) and byte G+E6h = 1. EDX:EAX
 * as left: o and the entry EDX on the other paths. */
uint64_t es2_mfd_key_select_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mfd_frame *frame,
    uint32_t caller_esi, uint32_t caller_edi, uint32_t gauge, uint32_t o, uint32_t n, const uint32_t entry[3]);
/* menu_select 43f9e0 (menu, k): word [menu+37h] = word [menu+32h] (the old
 * entry), dword [menu+32h] = k. EAX = menu, DX = the old word over the
 * entry EDX's upper half, *ecx = k. */
uint64_t es2_menu_select_registers(const es2_guest_memory *, uint32_t menu, uint32_t k, const uint32_t entry[3],
    uint32_t *ecx);
/* mfd_entry_next 44703c (G) / mfd_entry_prev 447070 (G): from i = [G+B1h]
 * the next (i+1, exactly 6 wraps to 0) or previous (i-1, negative wraps to
 * 5) index whose byte [[G+D1h]+2Ch+i] has bit 0 clear (the menu pointer
 * read once, the flag byte at every step; with all six bits set the
 * original loops for ever, and so does this), then mfd_entry 4470a4(G, i)
 * with EAX = G, EDX = the menu. EAX = i, EDX = 4470a4's. */
uint64_t es2_mfd_entry_next_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mfd_frame *frame,
    uint32_t caller_esi, uint32_t caller_edi, uint32_t gauge, const uint32_t entry[3]);
uint64_t es2_mfd_entry_prev_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mfd_frame *frame,
    uint32_t caller_esi, uint32_t caller_edi, uint32_t gauge, const uint32_t entry[3]);
#endif
