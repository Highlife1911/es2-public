#ifndef ES2_CORE_HUD_KEYS_H
#define ES2_CORE_HUD_KEYS_H
#include "es2/core/weapons.h"
/* The HUD key handler and the two button helpers below it
 * (docs/reverse-engineering/hud.md, section "HUD key handler as own
 * code"): the key dispatcher 0x432bc8, the button press 0x438d9c and the
 * button-release scheduler 0x4530bc.
 *
 * All three are cdecl and return the original's EDX:EAX. ECX is dead at
 * every caller (45feb8 and player_input read only AL of 0x432bc8; the many
 * callers of 0x438d9c/0x4530bc overwrite or ignore the result), so there is
 * no exit shim; the framework returns the entry ECX. Game code is reached
 * through g->call with the original's entry EAX/EDX/ECX, so a nested hook
 * keeps its mode. The whole function body is pure routing: it changes no
 * compared simulation state of its own; the compared effect is the weapon
 * selection (0x432a50, hook) and target cycling (0x43349c/0x4332dc, hooks)
 * it dispatches to, plus the virtual clock the button helpers advance. The
 * display requests (0x42a3f4), the F11 dialog (0x47749c/0x457538/0x4543b0),
 * the MFD forward (0x446c38), the R toggle (0x41b468), the Tab lock
 * (0x40e4ac), the nav marker (0x434974) and the eleven panel gadget virtual
 * slots stay H3 guest calls. */

/* 0x4530bc button_release(P, obj): append obj to the panel's pending-release
 * list at P+0x356 (8-byte entries), bump the count word P+0x396, and store
 * the release time clock()+0xa (the clock 0x467828 is a guest call). EAX =
 * clock()+0xa; EDX and ECX are the clock call's (dead at every caller). */
uint64_t es2_button_release_registers(const es2_guest_memory *, const es2_weapon_graph *,
    uint32_t panel, uint32_t object, const uint32_t entry[3]);
/* 0x438d9c button_press(G, index, flag): B = [G + 4*index + 0x18]; unless its
 * state word [B+0x1b] is 2, call the button virtual +8(B, flag) then
 * 0x4530bc([[0x49b088]+0], B). EAX = the virtual's/0x4530bc's EAX on the
 * active path, else the sign-extended state word. */
uint64_t es2_button_press_registers(const es2_guest_memory *, const es2_weapon_graph *,
    uint32_t group, uint32_t index, uint32_t flag, const uint32_t entry[3]);
/* 0x432bc8 hud_key(H, code): returns 0 when byte H+0x20f is set. Otherwise a
 * compare chain over the low word of code dispatches the weapon keys, the
 * target keys, the function keys, F11 and a default that offers the code to
 * the MFD (0x446c38) and the panel gadgets. The display-object getter
 * 0x429820 is a guest call. The exception frame is set up by
 * 0x49630c through g->call (synthetic EBP f = g->scratch + ES2_HUD_KEYS_FRAME);
 * *fs_head receives the saved FS:0 head the adapter writes back to FS:0 as
 * the original's epilogue does. EAX = AL (1 handled, 0 not); EDX = that head.
 * g->scratch must be ES2_HUD_KEYS_SCRATCH accessible guest bytes. */
#define ES2_HUD_KEYS_SCRATCH 0x40u
#define ES2_HUD_KEYS_FRAME 0x30u
uint64_t es2_hud_key_registers(const es2_guest_memory *, const es2_weapon_graph *,
    uint32_t hud, uint32_t code, const uint32_t entry[3], uint32_t *fs_head);
/* The same at the original stack coordinate. Before every game call
 * it stores in *frame the EBX, ESI and EDI that 0x432bc8 holds there, the
 * return address of its CALL (an image VA relative to g->image) and, for
 * the display-object getter 0x429820 that the original calls with the
 * request of the following 0x42a3f4 already pushed, that slot (pushed = 1,
 * temp = the request; else 0). caller_esi is the caller's ESI (live up to
 * 0x429820's first call). A bridge that runs the calls at the original's
 * outgoing ESP (EBP-3Ch; g->scratch = EBP-ES2_HUD_KEYS_FRAME, so f = EBP and
 * 49630c links the record at the original EBP-28h) loads them, so the MFD
 * forward 0x446c38 and everything below it find the original's stack bytes
 * (docs/reverse-engineering/input.md, "stack residue under the command
 * handler"). */
typedef struct es2_hud_key_frame { es2_weapon_frame call; uint32_t pushed, temp; } es2_hud_key_frame;
uint64_t es2_hud_key_frame_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_hud_key_frame *frame,
    uint32_t caller_esi, uint32_t hud, uint32_t code, const uint32_t entry[3], uint32_t *fs_head);
#endif
