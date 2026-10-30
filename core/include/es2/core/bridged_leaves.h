#ifndef ES2_CORE_BRIDGED_LEAVES_H
#define ES2_CORE_BRIDGED_LEAVES_H
#include "es2/core/weapons.h"
/* Original leaves that own code reaches through the guest bridge
 * (docs/reverse-engineering/hud.md, section "the bridged leaves"). Each
 * function returns EDX:EAX exactly as the original
 * leaves them; entry[] = EAX/EDX/ECX at entry; *ecx receives the
 * original's ECX at return (callers' ECX is dead, the framework returns
 * its own). image = the loaded image's base (globals are addressed through
 * it); s16 = sign extension of a word; lo(R, w) = a 16-bit load into R
 * that keeps R's high word. */

/* weapon_target_word 0x40e484 (W, T), the only caller cockpit_sync
 * 0x41b130: word [W+7Dh] = 0 when T is not 0 and the word [T+1A8h] is 0,
 * else FFFFh. EAX = W, EDX = T, ECX entry. */
uint64_t es2_weapon_target_word_registers(const es2_guest_memory *,uint32_t weapon,uint32_t target,
    const uint32_t entry[3],uint32_t *ecx);

/* list_next 0x411e40 and list_prev 0x411e60 (L, X), the view-subject
 * steps of command_key 0x45feb8 over the actor list 0x4a9af8: L = {dword
 * table at +0, count word +6}, X's index word +4Bh. next: k = (s16 [X+4Bh]
 * + 1) rem s16 [L+6]; prev: k = (s16 [X+4Bh] + s16 [L+6] - 1) rem s16
 * [L+6] (CDQ/IDIV: truncation toward zero, the remainder takes the
 * dividend's sign). EAX = [[L] + 4k], EDX = k, ECX = L. A zero count
 * faults in the original (outside the contract). */
uint64_t es2_list_next_registers(const es2_guest_memory *,uint32_t list,uint32_t object,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_list_prev_registers(const es2_guest_memory *,uint32_t list,uint32_t object,const uint32_t entry[3],uint32_t *ecx);

/* r_key_toggle 0x41b468 (P), the HUD key R of hud_key 0x432bc8. F =
 * P+92h; when the byte [F+11h] (P+A3h) is set the byte [F+4] (P+96h) is
 * toggled (XOR 1). H = [0x49b088], E = [H+20Bh] (the EventPort). The
 * message record M = f-18h (f = the frame's EBP): dword [f-16h] = 0, then
 * sound row 0x462830(1Ah) and word [f-18h] = 2Ch when [F+4] is set, else
 * 0x462830(1Bh) and 2Dh; 0x435ac8(E, 0, 2Ch), 0x435ac8(E, 0, 2Dh); then
 * E's slot 0 (E, M). Bytes 6..17h of M are the frame's residue. Every game
 * call goes through g->call with the original's EAX/EDX/ECX; with a frame
 * (the entry shim) each call carries EBX = E, ESI = F, the caller's EDI
 * and its return address, and the POP ECX after the sound call rereads the
 * slot at frame->top - 4. EDX:EAX and *ecx are those slot 0 leaves. */
typedef struct es2_r_key_frame { es2_weapon_frame call; uint32_t top; } es2_r_key_frame;
uint64_t es2_r_key_toggle_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_r_key_frame *frame,
    uint32_t f,uint32_t caller_edi,uint32_t player,const uint32_t entry[3],uint32_t *ecx);

/* nav_marker_set 0x434974 (no argument; Alt+D of hud_key): H = [0x49b088],
 * S = [H+203h] + 26h (the player position); the three dwords S+8, S+0,
 * S+4 are copied in that order to H+25Eh+8, +0, +4; byte [H+26Ah] = 1
 * (active), byte [H+26Bh] = 0 (not armed). EAX = H+25Eh, EDX = H, ECX =
 * [S+4]. */
uint64_t es2_nav_marker_set_registers(const es2_guest_memory *,uint32_t image,const uint32_t entry[3],uint32_t *ecx);

/* throttle_handler 0x447e08 (G, gadget, value), slot 0 of the
 * ThrottleGauge table 0x49d1e0 (reached from slider_base 0x43904c): a
 * walk k = 0, 1, ... while k < [G+68h] (signed, reread each step) over the
 * dwords [G+18h+4k]; at the first k whose dword equals gadget: k = 0 sets
 * [G+B5h] = EAX = value (byte [G+C1h] set) or -value; k > 0 returns EAX =
 * k-1 without a write. Without a match EAX = the count reached. EDX =
 * G+18h+4k, ECX = G. */
uint64_t es2_throttle_handler_registers(const es2_guest_memory *,uint32_t gadget_object,uint32_t gadget,uint32_t value,
    const uint32_t entry[3],uint32_t *ecx);

/* view_pref_toggle 0x45fe38 (no argument; Tab/Enter of command_key): the
 * word 0x4d2518 = EAX = 1 when it was 0, else 0; EDX = [0x4d24ea] (the
 * camera), ECX = [EDX+32h]; when ECX equals [0x4d24e6] (the player) the
 * view countdown word 0x4d2520 = 2. */
uint64_t es2_view_pref_toggle_registers(const es2_guest_memory *,uint32_t image,const uint32_t entry[3],uint32_t *ecx);

/* The default constructors of the matrix type (32 bytes: a 14h-byte
 * rotation and a 12-byte vector), compiler-generated and empty: they write
 * nothing. rotation_base_ctor 0x49514f (R): EAX = R, EDX and ECX entry.
 * rotation_ctor 0x495194 (R): 0x49514f(R) with the entry registers, then
 * EAX = R, ECX = R (POP ECX), EDX as the callee leaves it. matrix_ctor
 * 0x495132 (M): 0x495194(M) with the entry registers, then 0x4953c4(M+14h)
 * with EAX = M+14h, ECX = M; EAX = M, ECX = M+14h, EDX as the callees
 * leave it. Game calls through g->call (image = g->image). */
uint64_t es2_rotation_base_ctor_registers(uint32_t object,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_rotation_ctor_registers(const es2_weapon_graph *,uint32_t object,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_matrix_ctor_registers(const es2_weapon_graph *,uint32_t object,const uint32_t entry[3],uint32_t *ecx);
#endif
