#ifndef ES2_CORE_DEATH_H
#define ES2_CORE_DEATH_H
#include "es2/core/weapons.h"
/* The player's death sequence 45fa84, step 18 of the logic frame
 * 45f570. Exact contract: docs/reverse-engineering/main_loop.md (section
 * "death sequence 45fa84"). The view block 4d24bc, the start flag
 * 4d2a68, the countdown object 4d2a58 and the six class tables at
 * 49f4f4..49f513 are reached through g->image. Every game call goes through
 * g->call with the entry registers the original holds there (display
 * request 42a3f4, view subject 45df34, display start 42a4c4, view update
 * 45de30, outside camera 401c74, timer_dword_step 467aa8), so nested hooks
 * keep their modes. g->scratch is unused. EAX is exact (1 when the
 * countdown has run out, else 0); EDX and ECX are dead at the only caller
 * and are not part of the contract. */
uint64_t es2_death_sequence_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3]);
#endif
