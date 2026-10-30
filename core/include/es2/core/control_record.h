#ifndef ES2_CORE_CONTROL_RECORD_H
#define ES2_CORE_CONTROL_RECORD_H
#include "es2/core/weapons.h"
/* The per-frame control record 45a810 (row 2 of the logic frame
 * 45f570, also called by the modal screen loops). Exact contract:
 * docs/reverse-engineering/input.md, section "control record
 * 45a810". The core builds the 48-byte record 4d22c6 from the device
 * block that the poll 45baa8 fills (keyboard word, cursor, mouse buttons,
 * joystick axes, buttons and hat), from the tape entry in playback and
 * writes the tape entry in recording mode. Every platform and game call
 * stays original and goes through g->call with the entry EAX/EDX/ECX the
 * original holds at that call: the message pump 464c78, memset 49681a,
 * the device poll 45baa8, the key read 45bbc4, the joystick query 45c524
 * and 477904, the keyboard-axis helper 45a4cc, the command handler 45feb8
 * for queued keys, the event list swap 452d40, the playback key check
 * 45fe6c, the event append 453078, the address copy 486ece, the file name
 * 492b90, the error report 479c78 and the runtime's fopen/fread/fwrite/
 * fclose thunks 496802/49684a/496832/496838.
 *
 * g->image locates the globals; g->scratch is the original's 0C8h-byte
 * frame (offsets as from its ESP after the prologue): the key word +0Ah,
 * the tape buttons +4..+0Bh, the tape counts +0Ch, the tape entry +58h,
 * the event entry +68h and the recorded entry +0B8h are passed to callees
 * by address and live there. EAX = 4d22c6 (relocated by g->image); EDX
 * and, through *ecx, ECX are the original's at its RET. */
#define ES2_CONTROL_RECORD_SCRATCH 0xc8u
uint64_t es2_control_record_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t *ecx,
    const uint32_t entry[3]);
/* The flight-key callback 45a324 (input.md, section "flight-key callback
 * 45a324"), the hot-key callback of the list 49eb0b
 * that the keyboard handler 477bec calls with the whole key code (scan
 * code, 80h key-up, 200h/400h/800h Alt/Ctrl/Shift). `image` is the loaded
 * image base. With the demo word 4d2530 set it sets the quit word 4d2532
 * and returns 2; with the capture flag 49eaa5 clear it returns 2; Num-
 * and Num+ pass (2) while byte 4d2571 is 0; otherwise the 7-bit code is
 * looked up in the 30-byte list (indices above 15 fold down by 15, 15 and
 * above pass) and the key-state arrays 4d2394 (down), 4d23a3 (released
 * before seen), 4d23b2 (seen) and the Alt word 4d23d6 are updated.
 * Returns EDX:EAX: EAX = 1 (consumed) or 2, EDX = the code, or the code
 * with its low word reduced to bits 0..6 once the down flag is taken. ECX
 * is dead at the callers and not reproduced. */
uint64_t es2_flight_key_registers(const es2_guest_memory *,uint32_t image,uint32_t code);
/* The command-key queue callback 45a498 (input.md, section "command-key
 * queue callback 45a498"), the hot-key callback of the list
 * 49eb35 that the keyboard handler 477bec calls with the whole key code.
 * With the capture byte 49eaa4 clear it returns 2 and EDX unchanged;
 * otherwise it stores the low word of the code at 4d20c4 + 2 * [49eaa0]
 * (no bound check: the dword index is used as the original does, the
 * address wraps at 32 bits) and increments [49eaa0] by rereading it.
 * Precondition: that address is writable guest memory. `edx` is the
 * caller's EDX at the call (MOV DX keeps its high word). Returns EDX:EAX:
 * EAX = 1 (consumed) or 2, EDX = the high word of the entry EDX over the
 * code's low word (the entry EDX when the capture byte is clear). ECX is
 * untouched by the original and not reproduced. */
uint64_t es2_command_queue_registers(const es2_guest_memory *,uint32_t image,uint32_t code,uint32_t edx);
#endif
