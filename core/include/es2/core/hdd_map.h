#ifndef ES2_CORE_HDD_MAP_H
#define ES2_CORE_HDD_MAP_H
#include "es2/core/weapons.h"
/* The HDD map frame 0x44c988(M), slot 1 of the HDDMap table
 * 0x49d9d8, called once per redraw of the map page by 0x449f64 under the
 * HDD draw 0x449bf8 (docs/reverse-engineering/cockpit.md, section "HDD map
 * frame").
 *
 * In order: a pending gadget event (byte M+0x14d) -> map_areas 0x44d450(M);
 * the clock 0x467828; on bit 4 of the clock value the blink counter word
 * 0x49d6b8 counts the rising edges modulo 4 (latch byte 0x49d7e6); clock
 * value above the dword M+0x149 (unsigned) -> M+0x149 <- value + 30 and the
 * tick 0x44d370(M); the locals [f-0xc] <- 0, [f-4] <- [0x6c5f70] (the
 * graphics context X); a pending click (byte M+0x11c) -> map_click
 * 0x44d6e0(M) when byte M+0x103 is set and the dword M+0x105 is not -1,
 * else the wingman pick 0x44d82c(M); the selected wingman's order record
 * R = M + 0x85 + 0x2a*[M+0x105] with dword R+0x22 = 1: byte R+0x20 set and
 * the actor [R+0x18] dead (byte +0x99), or byte R+0x21 set and the actor
 * [R+0x1c] dead -> both bytes cleared. Then only when the dword M+0xc is 1
 * or 2: the wingman-button blink (byte 0x49d6ac: 0x44f644 on the selected
 * wingman's button [[M+0x31]+4e] and its byte +0x1f <- 0); the mode-button
 * panel (byte M+0x139: 0x44de14(M) and 0x43122c(1, &rect f-0x20, 0) with
 * the rectangle from button 0's top left to button 7's bottom right) or,
 * when the previous mode M+0x81 differs from the mode M+0x5d, X+0x22c <-
 * f-0xc and for each of the two modes k != -1 the button colours (bytes
 * M+0x131+k-10 choose 0x49b0b0/0x49b0b4 or 0x49b0ac), the label 0x438aac
 * (button, [0x4d12a8+4k], byte [[0x4d12f0+4k]]) and 0x44dd74(M, k-10, 0,
 * flag), then M+0x81 <- M+0x5d; the label redraw 0x44dd14(M) (byte
 * M+0x144); and, with the map redraw byte 0x49d6ae, X+0x22c <- f-0xc,
 * [f-8] <- s16 [0x4d3ba2], 0x486704(M+0x71), the map draw 0x44e334(M),
 * M+0xc <- 0, byte 0x49d6ae <- 0.
 *
 * Every game call goes through g->call (observed first by its original VA,
 * slot 0) with the EAX/EDX/ECX the original holds there, so a hooked
 * callee keeps its own mode; g->image relocates the globals and callees.
 * Returns the original's EDX:EAX; ECX is popped by the only caller
 * (0x449fbb) and not reproduced.
 *
 * f is the frame pointer whose locals the core uses at the original's
 * offsets (f-0x20..f-1): the original's EBP under the entry shim, else a
 * scratch frame of at least 0x28 bytes below f. The address f-0xc is what
 * reaches the graphics context. frame (NULL for a plain bridge): before
 * every game call the core stores there the EBX (M), ESI and EDI that
 * 0x44c988 holds at that call and the return address of its CALL (an
 * image VA relative to g->image); caller_esi/caller_edi are the caller's
 * (ESI changes to k-10 for the button calls). With frame->top nonzero
 * (the original's outgoing ESP), ECX after a one-slot call is re-read from
 * the argument slot top-4 (the original's POP ECX); otherwise it is the
 * pushed argument. */
typedef struct es2_map_frame { es2_weapon_frame call; uint32_t top; } es2_map_frame;
uint64_t es2_map_frame_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_map_frame *frame,
    uint32_t f, uint32_t caller_esi, uint32_t caller_edi, uint32_t map, const uint32_t entry[3]);
#endif
