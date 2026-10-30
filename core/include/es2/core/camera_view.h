#ifndef ES2_CORE_CAMERA_VIEW_H
#define ES2_CORE_CAMERA_VIEW_H
#include "es2/core/weapons.h"
/* The outside camera and the view subject (docs/reverse-engineering/
 * camera.md, section "The hooks (group camera_view)").
 * The camera object C (vtable 49F8E8h, 4Eh bytes) is the one camera_update
 * 4011a0 integrates: orbit distance word +22h, step word +24h, orbit angles
 * +26h, angle deltas +2Ch/+2Eh/+30h, target +32h, mode word +36h (0 free,
 * 1 orbit, 2 attached, 3 replay), orbit offset +38h, attachment offset +3Eh,
 * lock dword +4Ah. The view block B = 4D24BCh: player +2Ah (4D24E6h), camera
 * +2Eh (4D24EAh), view word +32h, outside word +34h (4D24F0h), outside
 * default +5Ch (4D2518h), requested view +5Eh, subject +60h, countdown +64h,
 * flag +78h. Addresses are guest addresses; image is the loaded image base
 * (0x400000 in the original). Every game call goes through g->call with the
 * original's entry EAX/EDX/ECX, so a callee that is itself a hook keeps its
 * own mode; `entry` holds the entry EAX/EDX/ECX. Each function returns the
 * original's EDX:EAX and, when `ecx` is not NULL, stores the original's ECX
 * there (0 where the original leaves a frame address of its own). */

/* camera_attached 401078 (C, O): EAX = EDX = 1 when mode word C+36h is 2 and
 * target C+32h is O, else 0. ECX unchanged. */
uint64_t es2_camera_attached_registers(const es2_guest_memory *, uint32_t camera, uint32_t object,
    const uint32_t entry[3], uint32_t *ecx);
/* camera_orbit_set 40109c (C, S, b): unless dword C+4Ah, C+38h..3Dh = the
 * three words at b, target = S, orbit angles C+26h..2Bh, distance C+22h,
 * deltas C+2Ch..31h and step C+24h = 0, mode = 1 (in the original's write
 * order). EAX = C; EDX = 0 (entry EDX when locked); ECX = a frame address
 * of the original (reported as 0; entry ECX when locked). */
uint64_t es2_camera_orbit_set_registers(const es2_guest_memory *, uint32_t camera, uint32_t subject,
    uint32_t offset, const uint32_t entry[3], uint32_t *ecx);
/* camera_attach_set 40111c (C, S, a): unless dword C+4Ah, C+3Eh..43h = the
 * three words at a, target = S, mode = 2. EAX = C; EDX = S and ECX = the
 * dword at a with its low word replaced by word a+4 (entry EDX/ECX when
 * locked). */
uint64_t es2_camera_attach_set_registers(const es2_guest_memory *, uint32_t camera, uint32_t subject,
    uint32_t offset, const uint32_t entry[3], uint32_t *ecx);
/* camera_turn_step 401c20 (p, w): approach_word(p, multiply_q8(800h,
 * sx w), 0C0h). camera_speed_step 401c4c (p, w): approach_word(p,
 * multiply_q8(400h, sx w), 50h). Registers of the last call. */
uint64_t es2_camera_turn_step_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t word,
    uint32_t value, const uint32_t entry[3], uint32_t *ecx);
uint64_t es2_camera_speed_step_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t word,
    uint32_t value, const uint32_t entry[3], uint32_t *ecx);
/* outside_camera 401c74 (C, a, b, c): unless dword C+4Ah, by sx(mode word
 * C+36h): 0: camera_turn_step(C+30h, -a), camera_speed_step(C+24h, c ? -b
 * : 0), camera_turn_step(C+2Ch, c ? 0 : b); 1: the same with +a and +b;
 * other modes nothing (EAX = sx(mode) - 4). The 16-bit argument words go
 * into dwords whose high word is FFFFh (first call of mode 0) or 0. */
uint64_t es2_outside_camera_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t camera,
    uint32_t a, uint32_t b, uint32_t c, const uint32_t entry[3], uint32_t *ecx);
/* view_apply 45de30 (): while the transition byte [4CF99Ch]+1Ch is set,
 * applies a requested view word B+5Eh that differs from B+32h (1 free, 2
 * attached to [B+60h] or the player, 3 orbit around [B+60h], 4 replay of
 * the player); otherwise refreshes B+34h from B+5Ch in view 3 with the
 * player as subject. Then, with a nonzero countdown B+64h, 45e208 once and
 * the countdown minus one. */
uint64_t es2_view_apply_registers(const es2_guest_memory *, const es2_weapon_graph *, const uint32_t entry[3],
    uint32_t *ecx);
/* view_subject_set 45df34 (S): the runtime record (49630c, record at f-20h)
 * and once the static camera 4D2754h (48bd84, guard byte 49F1ECh); the
 * group camera of S, a free group camera (copied from the current camera,
 * then view_offsets(S)) or the static camera (copied, then view_offsets(S))
 * becomes the camera 4D24EAh; subject 4D2684h = S, word 4D24F0h = (S =
 * player ? word 4D2518h : 1). EDX = the record's saved FS:0 head (the
 * adapter restores FS:0 to it), ECX = S. g->scratch is the original's
 * frame from its lowest local: f = g->scratch + ES2_VIEW_SUBJECT_FRAME is
 * the original's EBP (locals f-28h counter, f-22h count, try word f-10h). */
#define ES2_VIEW_SUBJECT_FRAME 0x28u
uint64_t es2_view_subject_set_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t subject,
    const uint32_t entry[3], uint32_t *ecx);
/* view_offsets 45edb0 (S): S's virtual slot +30h fills the attachment
 * offset f-8 and the orbit offset f-10h, then camera_attach_set([4D24EAh],
 * S, f-8) and camera_orbit_set([4D24EAh], S, f-10h). Registers of the last
 * call. f = g->scratch + ES2_VIEW_OFFSETS_FRAME is the original's EBP. */
#define ES2_VIEW_OFFSETS_FRAME 0x10u
uint64_t es2_view_offsets_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t subject,
    const uint32_t entry[3], uint32_t *ecx);
/* camera_current 45edf8 (): EAX = [4D24EAh]; EDX and ECX unchanged. */
uint64_t es2_camera_current_registers(const es2_guest_memory *, uint32_t image, const uint32_t entry[3],
    uint32_t *ecx);
#endif
