#ifndef ES2_CORE_COMMANDS_H
#define ES2_CORE_COMMANDS_H
#include "es2/core/weapons.h"
/* The command handler 0x45feb8 and its playback key check 0x45fe6c
 * (docs/reverse-engineering/input.md, section "command handler as own
 * code"). Both are cdecl with one stack argument (the key code dword) and
 * return the original's EDX:EAX; ECX is dead at every caller (each pops it),
 * so the framework's entry ECX is returned.
 *
 * Game code is reached through g->call with the original's entry EAX/EDX/
 * ECX, so a nested hook keeps its mode (hud_key 432bc8, playback_key 45fe6c,
 * dist_gate 49288c, status_eval 413180, cockpit_nearest 4333c8 and, through
 * the vtables, vehicle_keys 4157c8 (+0x2c) and vehicle_part 417de4 (+0x74)).
 * Before every call except 0x49630c the core stores in *saved the EBX, ESI
 * and EDI the original holds there (the key method +0x2c returns the high
 * word of its caller's ESI, 45fe6c the high word of its caller's EBX); the
 * adapter's bridge loads them. The routing, the cheat cases (freeze, step,
 * view cycling, damage slot, Alt+Ctrl+D/N damage through +0x74), the align/
 * centering keys and the alert/pause/quit dialog bookkeeping are own code;
 * the view requests (0x42a3f4, 0x45df34, 0x45fe38, 0x401148), the dialogs
 * (0x47749c, 0x455950, 0x4561dc, 0x4543b0, the dialog virtual +8, 0x45cff0),
 * WinHelp (0x45f070, 0x4669c4), the HUD lamp (0x429828, 0x4610ad, 0x441fa4,
 * 0x442120), the list walk (0x411e40, 0x411e60), the joystick summary
 * 0x477904, the display switch 0x4667c8 and the debug trap 0x467ad8 stay
 * guest calls. */
typedef struct es2_commands_saved { uint32_t ebx, esi, edi; } es2_commands_saved;

/* 0x45feb8 command_key(code): returns 0 while word 0x4d2564 is 0. Otherwise
 * hud_key([0x49b088], code) (AL != 0 -> AL), 0x45fe6c(code) (AX != 0 -> its
 * EAX), then the case table over code & 0xffff; EAX = EDI + EBX (EBX = 1 for
 * a handled case or the key method's EAX, EDI = 0 or the dialog result).
 * The exception frame 0x49630c is set up through g->call with the synthetic
 * EBP f = g->scratch + ES2_COMMANDS_FRAME (record at f-0x38, try-state word
 * at f-0x28); *fs_head receives the saved FS:0 head that the adapter writes
 * back to FS:0 as the original's epilogue does; EDX = that head.
 * g->scratch must be ES2_COMMANDS_SCRATCH accessible guest bytes. */
#define ES2_COMMANDS_SCRATCH 0x64u
#define ES2_COMMANDS_FRAME 0x60u
uint64_t es2_command_key_registers(const es2_guest_memory *, const es2_weapon_graph *,
    es2_commands_saved *saved, uint32_t code, const uint32_t entry[3], uint32_t *fs_head);
/* The same at the original stack coordinate. Before every game call
 * (0x49630c included) it stores in *frame the EBX, ESI and EDI that 0x45feb8
 * holds there and the return address of its CALL (an image VA relative to
 * g->image; no call of 0x45feb8 has another call's slots above it).
 * caller_ebx/caller_edi are the caller's, live up to 0x49630c and 0x432bc8.
 * A bridge that runs the calls at the original's outgoing ESP (EBP-68h;
 * g->scratch = EBP-ES2_COMMANDS_FRAME, so f = EBP and 0x49630c links the
 * record at the original EBP-38h) loads them, so the HUD key handler and
 * everything below it find the original's stack bytes
 * (docs/reverse-engineering/input.md, "stack residue under the command
 * handler"). */
uint64_t es2_command_key_frame_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_weapon_frame *frame,
    uint32_t caller_ebx, uint32_t caller_edi, uint32_t code, const uint32_t entry[3], uint32_t *fs_head);

/* 0x45fe6c playback_key(code): Alt+Enter (0x21c) calls the display switch
 * 0x4667c8 unless both the playback word 0x4d24d6 and the option word
 * 0x4d252c are set; Ctrl+B (0x430) calls the debug trap 0x467ad8 when
 * 0x4d252c is set. EAX = (caller's EBX & 0xffff0000) | 1 for these two
 * codes, else 0; EDX = the entry EDX or the callee's. ebx is the caller's
 * EBX (only its high word is read); entry[0]'s high word reaches the
 * callees in EAX (its low word is overwritten first). */
uint64_t es2_playback_key_registers(const es2_guest_memory *, const es2_weapon_graph *,
    uint32_t code, uint32_t ebx, const uint32_t entry[3]);
#endif
