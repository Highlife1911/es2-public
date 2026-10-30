#ifndef ES2_CORE_RENDER_ROOT_H
#define ES2_CORE_RENDER_ROOT_H
#include "es2/core/weapons.h"
/* The render root's simulation part (docs/reverse-engineering/
 * main_loop.md, section "the render root as own code").
 *
 * es2_render_root_registers, 0x45fca8 (no argument; EBX = the view block
 * 0x4d24bc for every call): the stage dispatcher 0x401d94(5) (the removal
 * flushes); the detail settings 0x45d490/0x45d50c when bytes 0x4d1f41/
 * 0x4d1f42 differ from their words; the jolt point 0x42d870 copied into
 * the frame and added to the canvas origin [[0x4d24ce]+220h/+224h]; the
 * camera setup 0x48c2e4, the terrain draw 0x46cb9c, the panel frame
 * 0x4327ac, the object binning 0x42841c; when 0x42db18 returns AL != 0 the
 * camera setup again, 0x40707c([0x4a9608], cam+4), the terrain draw, the
 * view transform 0x48c444(0), the sky/horizon 0x42e700 and the eye cell
 * objects 0x42883c(cam, byte 0x4d24c5); the cockpit sync 0x41b130(player
 * [0x4d24e6]); the HUD draw ([H+1F5h] virtual +8), the port frames
 * (virtual +10h of 0x433158()'s object and of [H+20Bh]); the jolt
 * subtracted again (the frame's copy, the context re-read); the cursor
 * point 0x4d22c8/0x4d22cc copied into the frame and 0x434520(H, &copy).
 * Returns 434520's EDX:EAX, *ecx its ECX.
 *
 * Every game call goes through g->call (observed first: direct calls by
 * their original VA with slot 0, virtual calls by the resolved target and
 * the slot) with the EAX/EDX/ECX the original holds there, so a hooked
 * callee keeps its own mode. g->image is the loaded image base (globals and
 * callees are relocated by it). g->scratch is the frame: the original's 16
 * bytes of locals at its outgoing ESP (jolt x/y at +0/+4, the cursor copy
 * at +8/+0Ch, whose address reaches 434520); every local is written at the
 * original's point and read where the original reads it.
 *
 * frame (required): before every game call the core stores there the EBX
 * (the relocated 0x4d24bc), ESI and EDI (the caller's, caller_esi/
 * caller_edi) and the return address of its CALL (an image VA relative to
 * g->image). With frame->top nonzero (the original's outgoing ESP, the
 * entry ESP - 14h; then also g->scratch) ECX after a one-slot call is
 * re-read from the argument slot top-4 (the original's POP ECX); otherwise
 * it is the pushed argument. */
#define ES2_RENDER_ROOT_FRAME 0x10u
typedef struct es2_render_root { es2_weapon_frame call; uint32_t top; } es2_render_root;
uint64_t es2_render_root_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_render_root *frame,
    uint32_t caller_esi, uint32_t caller_edi, const uint32_t entry[3], uint32_t *ecx);
/* 0x401d94(stage): the stage-callback table at 0x4a804c (6-byte entries:
 * function pointer, stage word) up to the signed count word 0x497074,
 * re-read before every entry; every entry whose word equals the low word
 * of the stage is called without arguments (the pointer read at the call)
 * through g->call, observed by its original VA (the pointer minus g->image
 * plus 0x400000) with slot 0, with EBX = the entry, ESI = its index, EDI =
 * the stage (frame, required) and the return address 0x401dae; EAX/EDX/ECX
 * flow from one callback to the next. Returns the last callback's EDX:EAX
 * (the entry pair without a call); ECX is dead at all seven callers. */
uint64_t es2_stage_dispatch_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_weapon_frame *frame,
    uint32_t stage, const uint32_t entry[3]);
/* 0x40707c(D, S): D+8 <- S+8, D <- S, D+4 <- S+4 (each dword read just
 * before its store, so overlapping vectors copy as the original does).
 * Returns EAX = D, EDX = [S+4] as read; *ecx = [S] as read. */
uint64_t es2_vector_copy_registers(const es2_guest_memory *, uint32_t dst, uint32_t src, uint32_t *ecx);
/* The twelve removal flushes, index 0..11 in this order:
 * 0x405ea4, 0x4077e8, 0x407b3c, 0x4083e0, 0x408f84, 0x408f9c, 0x409904,
 * 0x409a1c, 0x40a108, 0x41544c, 0x4215a8, 0x46b188 (the eleven stage-5
 * callbacks of 0x4a804c and 0x408f84). Each reads dword [P] then [P+4]
 * (P = es2_stage_flush_pool(index), relocated by g->image) and calls
 * removal_flush 0x478644(list, pool) through g->call with EAX = pool, EDX =
 * list, ECX = entry ECX (observed as 0x478644); 0x408f9c then calls
 * 0x408f84() with that call's registers, 0x46b188 then stores dword
 * 0x6b4f3c <- 7FFFFFFFh. Returns the last call's EDX:EAX, *ecx its ECX. An
 * index out of range returns the entry registers. */
#define ES2_STAGE_FLUSHES 12u
uint32_t es2_stage_flush_va(unsigned index);
uint32_t es2_stage_flush_pool(unsigned index);
uint64_t es2_stage_flush_registers(const es2_guest_memory *, const es2_weapon_graph *, unsigned index,
    const uint32_t entry[3], uint32_t *ecx);
#endif
