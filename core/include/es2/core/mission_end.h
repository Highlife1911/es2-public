#ifndef ES2_CORE_MISSION_END_H
#define ES2_CORE_MISSION_END_H
#include "es2/core/weapons.h"
/* The mission end 461ff0 of the game main (DBSIM.EXE), called once
 * by the main routine 45f160 at 45f54b after the loop tail. Contract:
 * docs/reverse-engineering/launcher.md, section "the mission end `0x461ff0` as
 * own code". g->image is the loaded image base (0x400000 in the original);
 * every global is addressed through it. Both game calls go through g->call with
 * the original's EAX/EDX/ECX and are reported to g->observe first (key = the
 * callee's VA), so a hooked callee keeps its own mode:
 *   - _fclose 0x496838 (the runtime's import thunk; the dump and the tape
 *     stream), one argument;
 *   - results_writer 0x42412c (a hook), one argument: the dword at 4d24e6.
 *
 * Steps: byte 4d1dd6 <- 0; when the tape stream dword 4d24e2 is nonzero:
 * fclose the dump stream dword 4d24de when nonzero (then that dword <- 0),
 * fclose the tape stream (then that dword <- 0); results_writer([4d24e6]);
 * then the exit code dword 4d27b8, first match: demo word 4d2530 nonzero
 * -> 6; quit word 4d24fe nonzero -> 0; byte [[4d24e6]+99h] nonzero and
 * word 4a9e52 nonzero -> 4; else 3. The two one-byte exits 0x426514 and
 * 0x425344 that the original calls last are bare RET: the core does not
 * call them.
 *
 * Returns the original's EDX:EAX at its RET and stores its ECX in *ecx;
 * `entry` holds the entry EAX/EDX/ECX (the callers do not read them). */
uint64_t es2_mission_end_registers(const es2_guest_memory *,const es2_weapon_graph *,
    const uint32_t entry[3],uint32_t *ecx);
#endif
