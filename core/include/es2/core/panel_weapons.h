#ifndef ES2_CORE_PANEL_WEAPONS_H
#define ES2_CORE_PANEL_WEAPONS_H
#include "es2/core/weapons.h"
/* The panel weapon selection (docs/reverse-engineering/hud.md,
 * section "panel weapon selection as own code"): panel_weapon
 * 0x432a50 -> weapon_find 0x4106ac -> weapon_select 0x410708.
 *
 * W is a weapon system ([P+202h]): dword W+4 the slot list, word W+8 the
 * slot count, byte W+18h, signed byte W+1Dh the selected slot. Each
 * function returns the original's EDX:EAX and stores the original's ECX in
 * *ecx (live at callers of all three). Game code is called through g->call
 * (g->image = the loaded image base) with the original's entry
 * EAX/EDX/ECX, so a nested hook keeps its mode. */

/* weapon_select 410708 (W, slot): selects s16 slot unless it is already
 * selected; for a weapon with byte E+4Bh set and a negative definition
 * byte [[E+18h]+16h] the stored slot is moved by that byte. Writes byte
 * W+1Dh and the word 0x4d2528 (image + 0xd2528) <- 0. EAX = W, EDX = the
 * (adjusted) slot dword; ECX = s16 slot, E or the definition byte. No game
 * call, no entry register. */
uint64_t es2_weapon_select_registers(const es2_guest_memory *, uint32_t image, uint32_t weapons, uint32_t slot,
    uint32_t *ecx);
/* weapon_find 4106ac (W, elem): the first weapon whose virtual +58h
 * accepts elem; selects it through 410708 (g->call) when its byte E+4Ch is
 * set, then byte W+18h <- 1 and AX = 1. EAX = 0 otherwise. */
uint64_t es2_weapon_find_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t weapons,
    uint32_t element, const uint32_t entry[3], uint32_t *ecx);
/* panel_weapon 432a50 (H, elem): weapon_find([[H+203h]+202h], elem)
 * through g->call; AL = 1 when it returned AX != 0, else EAX = 0. */
uint64_t es2_panel_weapon_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t hud,
    uint32_t element, const uint32_t entry[3], uint32_t *ecx);
#endif
