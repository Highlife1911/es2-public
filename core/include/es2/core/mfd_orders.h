#ifndef ES2_CORE_MFD_ORDERS_H
#define ES2_CORE_MFD_ORDERS_H
#include "es2/core/weapons.h"
/* The MFD order path and the mission-load set-up of the order
 * group. Contract in docs/reverse-engineering/cockpit.md, section "MFD order
 * path". Addresses are guest addresses; g->image is the loaded image base
 * (0x400000 in the original), used for the globals 49b088 (the cockpit), 49b0f8
 * (the player's group), 49dbe0, 4a9b7a (the actor list), the wingman table
 * 4d03c8 and the order record 4d03d4. Every game call goes through g->call with
 * the original's EAX/EDX/ECX and is reported to g->observe first (direct calls
 * by their VA, the virtual slots by the key named per function), so a hooked
 * callee keeps its own mode. Each function returns the original's EDX:EAX;
 * `entry` holds the entry EAX/EDX/ECX.
 *
 * es2_mfd_frame: the original stack coordinate (an entry shim). Before every
 * game call the core stores the original's EBX/ESI/EDI and return address in
 * call, so the caller can run the callee at the original's outgoing ESP
 * `top`; a POP ECX after a one-slot call rereads [top-4]. NULL without the
 * shim (EBX/ESI/EDI are then the bridge's and the POP gives the slot value
 * passed). */
typedef struct es2_mfd_frame { es2_weapon_frame call; uint32_t top; } es2_mfd_frame;

/* mfd_button 446844 (G, button): slot 0 of the MFDGauge table 49cfa0, the
 * owner call of a pressed MFD button (438e3c). The button's index i is its
 * position in G+18h[0..[G+68h]); i 0..5 switch the MFD page (446e60(G,
 * i)), 6 does nothing, 8 cycles the radar range (446ff0), 7/9 the status view
 * (446fc4, MFD mode 0) or the cockpit target (446fb0), 10 gives the order of
 * the selected entry (447248) unless byte 49dbe0 is set, 11/12 (the radio
 * pair [G+44h]/[G+48h] themselves) clear/set byte +96h of the player
 * [[49b088]+203h] and switch the pair's state bytes +40h with their slot
 * +4 at [[B+17h]+4] (observer keys 44h/48h). caller_esi/caller_edi: the
 * caller's ESI and EDI, which reach the callees. */
uint64_t es2_mfd_button_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mfd_frame *frame,
    uint32_t caller_esi, uint32_t caller_edi, uint32_t gauge, uint32_t button, const uint32_t entry[3]);
/* mfd_click 4470c0 (G, W, P): the MFD entry click (from the select gadget's
 * slot +8 447658, P = the cursor point 4d1de3). In MFD mode 1 ([G+C1h] = 1)
 * the first of the six entry gadgets [[G+D1h]+14h+4i] whose rectangle (+4:
 * x0, y0, x1, y1, inclusive, signed) holds P: when byte 49dbe0 is set and i
 * is the selected entry [G+B1h], button_press(G, 0Ah, 1) and the order
 * 447248(G), byte G+E5h+[G+C1h] = 1; else the entry select 4470a4(G, i) and
 * byte G+E6h = 1. */
uint64_t es2_mfd_click_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mfd_frame *frame,
    uint32_t gauge, uint32_t gadget, uint32_t point, const uint32_t entry[3]);
/* mfd_order 447248 (G): the order code 43f9c0(menu [G+D1h]) into the word
 * of the order record 4d03d4, order_call 43166c([G+14h], 4d03d4), and after
 * a given order (nonzero EAX) the toggle 43f9f8(menu). The entry
 * EAX/EDX/ECX reach 43f9c0. */
uint64_t es2_mfd_order_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mfd_frame *frame,
    uint32_t gauge, const uint32_t entry[3]);
/* order_call 43166c (H, R): dword [R+2] = the player [H+203h], then the
 * group order 4231a4([49b0f8], R) with EAX = the player, EDX = the group and
 * the entry ECX; EBX/ESI/EDI are the caller's (caller[0..2]). */
uint64_t es2_order_call_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mfd_frame *frame,
    const uint32_t caller[3], uint32_t cockpit, uint32_t record, const uint32_t entry[3]);
/* group_order 4231a4 (Gp, R): gives the order of record R (code = word
 * [R], issuer [R+2]) to the members [[Gp+0Ch]+4i], i < s16 [Gp+10h]: per
 * pass the not yet ordered (flag byte), living (byte +99h clear) member
 * other than the issuer with the highest priority word w, then the
 * smallest distance (dist_gate 49288c(B+26h, issuer+26h)); its virtual
 * +28h(B, R, mode) with mode 0, 1 after a refusal, 2 after a taken order;
 * codes 0 and 2 stop at the first taker. w by code: 0/2/5 (byte
 * [[B+4Dh]+9] = 0), 1/8/0Fh that byte, 4 (byte B+96h = 0), 7 that byte;
 * codes 3, 6, 9..0Eh and above 0Fh never write w and read the frame's
 * residue word [f-1Ch]. f is the frame (the original's EBP with the entry
 * shim, else a scratch frame with Gp at f+8 and R at f+0Ch): every local
 * lives at its original address f-20h..f-1 and is read back from there, so
 * the flag bytes of members 10 and 11 are the result word's as in the
 * original; from 13 members on the flag bytes of members 12..15 are the
 * best-member dword, cleared at every pass (the original can then order
 * one of them forever), and from 17 on they reach the saved EBP and the
 * return address: outside the contract. memset 49681a(f-10h, 0, 10) first, with
 * the caller's EBX/ESI (caller[0..1]) and the entry EDX/ECX. EAX = 1 when
 * an order was taken. */
uint64_t es2_group_order_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mfd_frame *frame,
    uint32_t f, const uint32_t caller[2], uint32_t group, uint32_t record, const uint32_t entry[3]);
/* menu_code 43f9c0 (menu): EAX = [menu+32h], plus 3 when bit 1 of byte
 * [menu+2Ch+e] is set (e = the first read of [menu+32h]); EDX = e; *ecx =
 * EAX. */
uint64_t es2_menu_code_registers(const es2_guest_memory *, uint32_t menu, uint32_t *ecx);
/* menu_toggle 43f9f8 (menu): for e = [menu+32h] 4 or 5 flips bit 1 of byte
 * [menu+2Ch+e] (e reread) and sets byte menu+36h. EAX = menu, EDX = e - 6;
 * *ecx = e on the toggle path, else the entry ECX. */
uint64_t es2_menu_toggle_registers(const es2_guest_memory *, uint32_t menu, const uint32_t entry[3], uint32_t *ecx);
/* mfd_entry 4470a4 (G, i): [G+B1h] = i when it differs. EAX = G, EDX = i,
 * *ecx = the entry ECX. */
uint64_t es2_mfd_entry_registers(const es2_guest_memory *, uint32_t gauge, uint32_t index, const uint32_t entry[3],
    uint32_t *ecx);
/* mfd_target 446fb0 (G): cockpit_next 43349c([49b088]) with the entry
 * EDX/ECX and the caller's EBX/ESI/EDI (caller[0..2]); G is not read.
 * Returns 43349c's EDX:EAX. */
uint64_t es2_mfd_target_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mfd_frame *frame,
    const uint32_t caller[3], uint32_t gauge, const uint32_t entry[3]);
/* mfd_view_next 446fc4 (G): increments [G+318h]; when it then equals
 * [G+31Ch] it becomes 0 (*ecx = 0, else the entry ECX). EAX = G, EDX = the
 * incremented value. */
uint64_t es2_mfd_view_next_registers(const es2_guest_memory *, uint32_t gauge, const uint32_t entry[3], uint32_t *ecx);
/* mfd_range 446ff0 (G): i = [G+C5h] + 1, 0 instead of 3; 447014(G, i) with
 * EAX = G, the entry EDX/ECX and EBX = i, the caller's ESI/EDI
 * (caller[0..1]). EAX = i, EDX = 447014's. */
uint64_t es2_mfd_range_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mfd_frame *frame,
    const uint32_t caller[2], uint32_t gauge, const uint32_t entry[3]);
/* order_setup 431530 (H): at the mission load, walks the actor list [4a9b7a]
 * backwards (pool_prev 471c70) to the first actor whose byte +A3h is set;
 * its group [A+45h] becomes the order group 49b0f8, and its first members
 * other than the player [H+203h], up to three and up to the first null,
 * become the wingman table 4d03c8[0..n). Then per wingman the HDD panels
 * 44a7e8([H+1F9h], i, word [W+29Ch] over the ECX high word) and the MFD's
 * list 4472bc([H+1EDh], n). No address of a local reaches a callee, so the
 * four locals are host variables. Returns 4472bc's EDX:EAX. */
uint64_t es2_order_setup_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t cockpit,
    const uint32_t entry[3]);
#endif
