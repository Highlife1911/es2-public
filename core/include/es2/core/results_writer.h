#ifndef ES2_CORE_RESULTS_WRITER_H
#define ES2_CORE_RESULTS_WRITER_H
#include "es2/core/weapons.h"
/* The mission-end results writer and its helpers. Exact contract:
 * docs/reverse-engineering/mission_runtime.md, section "results
 * writer 42412c and its helpers". Every game call goes through g->call
 * with the original's EAX/EDX/ECX (observer key: the callee VA, or slot
 * 14h for the stream's write virtual), so nested hooks keep their modes.
 *
 * list_append 0x426ac8(item): appends the dword at item to the list
 * [4a9e6c] at index [4a9e6a] (no bound check) and increments the count
 * word. Result EDX:EAX = (list, sx16(count)); *ecx = the dword.
 *
 * vehicle_value 0x418e60(V): part_ratio, list_append, damage_score and
 * multiply_q10 through g->call; the frame dwords f-8/f-4 live in
 * g->scratch (ES2_VEHICLE_VALUE_SCRATCH bytes, f = scratch+8), and the
 * pair handed to list_append is f-4. Result EDX:EAX exact; *ecx = the
 * dword at f-4 (the original's ECX whenever an item was appended, else
 * the original reads stack residue).
 *
 * score_sum 0x423e88(team): pool_prev over the MECH list [4a9b7a] and
 * vehicle_value per counted vehicle through g->call. Result EDX:EAX exact
 * (sum, the last pool_prev's EDX); *ecx = 0 (the final cursor).
 *
 * results_writer 0x42412c(P): the runtime frame setup 0x49630c with the
 * synthetic EBP f = g->scratch + ES2_RESULTS_WRITER_FRAME (record f-38h,
 * context word f-28h, streams R = f-0Ch and V = f-18h, locals down to
 * f-4Ah), then the results.dat and mission.var writes through g->call.
 * Result EAX = the last close's EAX, EDX = the record's saved FS:0 head
 * (the caller writes it back to FS:0 as the epilogue does); *ecx = the
 * last close's ECX. g->scratch must be ES2_RESULTS_WRITER_SCRATCH
 * accessible guest bytes. */
#define ES2_VEHICLE_VALUE_SCRATCH 8u
#define ES2_RESULTS_WRITER_SCRATCH 0x60u
#define ES2_RESULTS_WRITER_FRAME 0x58u
uint64_t es2_list_append_registers(const es2_guest_memory *,uint32_t image,uint32_t item,uint32_t *ecx);
uint64_t es2_vehicle_value_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t vehicle,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_score_sum_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t team,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_results_writer_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t player,const uint32_t entry[3],uint32_t *ecx);
#endif
