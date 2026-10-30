#ifndef ES2_CORE_RESULTS_H
#define ES2_CORE_RESULTS_H
#include "es2/core/weapons.h"
/* The team actor records of results.dat, 423d68 (team, stream),
 * called once by the result writer 42412c. Exact contract:
 * docs/reverse-engineering/mission_runtime.md (section "team result
 * records 423d68"). For every team member the 84-byte condition record of
 * condition_record 41b4ec is built in the original's frame layout at
 * g->scratch+80h-64h (128 scratch bytes), its 33 Q8 condition words are
 * written as ((100h - c) * 100) >> 8 through the stream's virtual +14h
 * (the transformed word also replaces the record word, as in the
 * original), then the 6 bytes at actor+2A4h. Every game call goes through
 * g->call with the original's entry registers, so the nested hooks keep
 * their modes. Result: EDX:EAX exact (EAX's low word is the team count
 * word); ECX is the team pointer and, like EAX/EDX, dead at the caller. */
uint64_t es2_result_conditions_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t team,uint32_t stream,const uint32_t entry[3]);
#endif
