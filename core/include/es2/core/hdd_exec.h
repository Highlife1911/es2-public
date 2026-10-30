#ifndef ES2_CORE_HDD_EXEC_H
#define ES2_CORE_HDD_EXEC_H
#include "es2/core/mfd_orders.h"
/* The HDD button executor. Contract in
 * docs/reverse-engineering/cockpit.md, section "HDD button
 * executor". H is the HDD gauge (449bf8's argument); f is the address of
 * the original's EBP: the locals live at f-20h..f-1 (f-8 is the record
 * whose address goes into the graphics context [[6c5f70h]+22Ch]), as in the
 * original when f is the original's frame (the entry shim), or in a scratch
 * frame without it. Addresses are guest addresses; g->image is the loaded
 * image base (0x400000 in the original), used for the context pointer
 * 6c5f70, the lamp sounds 4d3b96/4d3b9a, the wingman table 4d03c8 and the
 * order record 4d03d4. Every game call goes through g->call with the
 * original's EAX/EDX/ECX and is reported to g->observe first under its VA,
 * so a hooked callee keeps its own mode. frame (es2_mfd_frame,
 * mfd_orders.h): the original stack coordinate (EBX/ESI/EDI and return
 * address of each call, POP ECX rereads [top-4]); NULL without the shim.
 * caller_esi / caller_edi: the caller's ESI and EDI, which reach the
 * callees where the function has not changed them. `entry` holds the entry
 * EAX/EDX/ECX. */

/* hdd_execute 44a1a0 (H): the pending HDD command c = s8 [H+528h]. c = -1:
 * nothing (EAX = 0). With byte [H+76h] set and c > 1: nothing (EAX = c+1,
 * EDX = c-1). Otherwise [f-8] = 0, [f-10h] = 4 and, through the jump table
 * 44a1f7 on c (0..14, page v = [H+109h], M = [H+101h], W = [H+105h]):
 * 0/1 the page button 44a60c(H, c); 2/3 (v = 0) 44d0c0/44d124(M) or (v = 1)
 * 450bf4/450c18(W), then byte H+10Fh (v = 0) or H+10Eh (v = 1) = 1 and
 * byte H+B0h = 2; 4/5 (v = 0) 44cff8/44d05c(M), H+10Fh and H+B0h, or (v =
 * 1) 44ba08/44b9b0(H) into [H+524h] and the lamp block: the lamp state
 * (wingman [H+524h] with byte +A3h set) against byte [H+51Fh], which is
 * toggled, and a toggle's sound word s16 4d3b96 (off) / 4d3b9a (on) in
 * [f-4]; a nonzero [f-4] stores f-8 in [[6c5f70h]+22Ch], clips the rect
 * H+F1h (486704) and draws the copy f-20h (x1 + 1) with 43122c(1, f-20h,
 * 0); then byte H+110h = 1, H+B0h = 2; 6/7 (v = 0) 44cf90/44cfc4(M), H+10Fh
 * and H+B0h; 9 (v = 1) 450bf4(W), H+10Eh and H+B0h; 10..12 (v = 1 after
 * 44a60c(H, 0), or v = 0) the wingman i = c-10 of the table 4d03c8 when i
 * is not [H+517h], the entry is set and its byte +99h clear:
 * 44a748(H, i), then 433e3c(2) when byte [M+103h] is set; 13 (v = 0) with
 * byte [M+104h] clear 44dc6c(M, 0, 1), set 44db9c(M) and, when [H+517h] is
 * not -1, the order word [M+5Dh] into 4d03d4, 44db4c(M, 4d03d4),
 * 431610([H+14h], 4d03d4, ECX with CX = [H+517h]), 433e3c(0), 44dc10(M),
 * 44a748(H, -1); 14 44dc10(M), 433e3c(0), 44a748(H, -1); 8 and the other
 * pages nothing. Every such path ends with byte [H+528h] = -1. EDX:EAX as
 * the original leaves them. */
uint64_t es2_hdd_execute_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mfd_frame *frame,
    uint32_t f, uint32_t caller_esi, uint32_t caller_edi, uint32_t hdd, const uint32_t entry[3]);
#endif
