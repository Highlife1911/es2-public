#ifndef ES2_CORE_FLYER_PART_H
#define ES2_CORE_FLYER_PART_H
#include "es2/core/weapons.h"
/* The FLYER part damage 0x421bb4(F, part, damage, source), slot
 * +74h of the FLYER vtable 0x49a5e0 (docs/reverse-engineering/hit_tests.md,
 * section "FLYER part damage"). Callers through
 * the slot: the FLYER hit test 0x421c8c, the ACTOR radial-hit default
 * 0x411b3c (slot +70h of FLYER) and the cheat dispatcher 0x45feb8.
 *
 * [0x4a9660] <- F+24Fh (the velocity debris_heading adds to new debris)
 * for the duration of the call; the region damage 0x40da38(F+200h, part,
 * damage). When it reports a destroyed region (AX != 0) and the part's low
 * word is 0, the FLYER dies: with a source, the message 0x420a68(2Eh) when
 * the source is the player [0x4d24e6] and the player's target +1A4h is F,
 * and the source's slot +60h(source, F, 0); then bytes F+99h <- 1, F+96h
 * <- 0, F+38h <- 1, dword F+2Eh <- -100000, death_actions(F),
 * trigger_fire([F+1B6h]) when nonzero, script_start(F+4Dh, 0x499e60) and,
 * when word F+6 != 0, the sound 0x462ac4(31h) and word F+6 <- 0. Always
 * [0x4a9660] <- 0 at the end.
 *
 * Every game call goes through g->call with the EAX/EDX/ECX the original
 * holds there (0x40da38, 0x420a68, the source's slot +60h, death_actions,
 * trigger_fire, script_start, 0x462ac4; nested hooks keep their modes).
 * The damage argument is a dword slot of which only the low word is read;
 * the original pushes it as EDX = lo(entry EDX, damage), so 0x40da38 gets
 * the entry EDX's high word: entry[1] is needed, entry[0]/entry[2] are
 * not read. g->image is the image base; g->scratch is not used.
 *
 * Result: EDX:EAX exactly as the original leaves them. The original
 * returns ECX = 0 on every path (XOR ECX,ECX at 0x421c7e); the adapter's
 * entry shim provides it (the framework restores the entry ECX). */
uint64_t es2_flyer_part_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t flyer,uint32_t part,uint32_t damage,uint32_t source,const uint32_t entry[3]);
#endif
