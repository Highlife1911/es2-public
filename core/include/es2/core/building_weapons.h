#ifndef ES2_CORE_BUILDING_WEAPONS_H
#define ES2_CORE_BUILDING_WEAPONS_H
#include "es2/core/weapons.h"
/* Original 404100. scratch: 128 disjoint guest bytes; padding is the
 * composed matrix's entry-stack byte19. All game code remains delegated.
 * The animation divisor must be nonzero, as required by original IDIV. */
uint64_t es2_turret_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t building,const uint32_t entry[3]);
#endif
