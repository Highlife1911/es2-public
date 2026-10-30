#ifndef ES2_CORE_MISSION_ROOT_H
#define ES2_CORE_MISSION_ROOT_H
#include "es2/core/weapons.h"
/* The mission-load root 0x461608 (DBSIM.EXE), called once per
 * process by the game main 0x45f160 at 0x45f42b. Contract:
 * docs/reverse-engineering/mission_load.md, section "the
 * mission-load root".
 *
 * es2_mission_root_registers (no argument; EDI = the view block 0x4d24bc for
 * every call): the runtime record 0x49630c, the abort-notify callback
 * 0x479cfc(0x461304), the input set-up 0x45a5dc, 0x4955c4 when byte 0x4d267d is
 * set, the window and client rectangles (GetWindowRect and GetClientRect
 * through the stdcall thunks 0x49694c/0x496970 into the frame, the border sizes
 * at f-34h .. f-40h), the graphics context 0x461294, the pump 0x464c78 and the
 * frame clear 0x46548c, the launch peek 0x4614c0(f-44h, f-42h), the loading
 * screen (its name "ld_" + "herc"/"flyr" + '1' + world at f-128h: 0x4967ea,
 * 0x492bec, the palette 0x465f7c and 0x481fe0, 0x461450), the graphics-library
 * object (new(6), 0x480af0) at view+4Eh, the sound init 0x462410 (six slots:
 * the sound byte view+0Ch and five words of the frame the original never
 * writes), 0x463370, 0x48dd08, 0x4913dc, stage_dispatch(2), the scene lights
 * 0x46134c, the limiter reference 0x467840, the world load 0x424308, view+0BBh
 * = 25 or 13, the terrain object binning 0x4282d8 over the table [0x4aaa78]
 * (count word 0x4aaa7c, a null entry ends it), the mouse queue 0x452ae4, byte
 * 0x4d1dd6 <- 0, the actor load 0x4253d8, 0x44f6f4, the two drains (explosions
 * 0x4a961e/0x4a9622 with 0x407b3c, 0x4077e8, 0x409904; debris 0x4a9642/0x4a9646
 * with 0x4083e0; pool_prev 0x471c70 and removal_append 0x478628), word 0x4cf880
 * <- word 0x49abba, the height fix (terrain_height 0x46e180 plus the type
 * offsets) and the player pick over the MECH pool [0x4a9b7a], 0x4d2684 and the
 * terrain object's callback +125h, one camera-like object per member of the
 * player's group (new(4Eh), 0x48bd84, vtable 0x49f8e8) into 0x4d2688[],
 * view+60h, the cursor sprite (below 640 pixels: 0x492bec, 0x45cdf4, 0x465618),
 * the dead computation f-0A0h/f-0A4h, the HUD set-up 0x4544c0, the modal
 * callbacks 0x49dd94/0x49dd98, the main camera (new(4Eh), 0x48bda4 with the
 * frame vectors f-140h and f-0B8h) at view+2Eh, the view object 0x45edb0,
 * view+6Eh, the cockpit (new(37h), 0x429660) at view+4, 0x431530, the per-MECH
 * display objects 0x437c8c at A+1FEh, cockpit_sync 0x41b130, camera_update
 * 0x4011a0, the layout switch 0x42a1f0 (once or twice), the dead building
 * search (dist_gate 0x49288c), the CD track 0x46313c (sound context and header
 * word 4 = 0), the first render (0x42eb40, 0x43048c, render_root 0x45fca8) or
 * the intro 0x45d548, the pumps and palette flush, the present 0x464a14 or the
 * text rectangle 0x43122c(1, f-150h, 0), 0x4328cc, byte 0x49b105, 0x433f7c, the
 * graphics slot [0x4a5824], the detail levels 0x45d490/0x45d50c, view+3Ah, the
 * drop sequence 0x45d85c, byte 0x4d1dd6, 0x45b758, the key ring flush 0x477abc
 * of [0x6c5eb8], view+76h <- 0.
 *
 * Every game call goes through g->call (observed first: direct calls by
 * their original VA with slot 0, the graphics slot call by the slot key
 * 0x4a5824) with the EAX/EDX/ECX the original holds there, so a hooked
 * callee keeps its own mode. g->image is the loaded image base (globals,
 * callees and image constants are relocated by it). The frame: f =
 * g->scratch + ES2_MISSION_ROOT_LOCALS is the original's EBP; every local
 * lies at the original's offset below f and is written where the original
 * writes it (dead stores included) and read where it reads it; the runtime
 * record lies at f-30h (the bridge passes the EBP for 0x49630c).
 *
 * frame (required): before every game call the core stores there the EBX,
 * ESI and EDI the original holds at that CALL (EBX starts as caller_ebx,
 * ESI as f-8, EDI = the relocated 0x4d24bc) and its return address (an
 * image VA relative to g->image). With frame->top nonzero (the original's
 * outgoing ESP, f-15Ch) ECX after a one-slot call is re-read from the
 * argument slot top-4 (the original's POP ECX); otherwise it is the pushed
 * argument.
 *
 * Returns the EDX:EAX of the last call (0x477abc); *ecx = the record's
 * saved FS:0 head [f-30h], which the caller writes back to FS:0 as the
 * original's epilogue does (MOV ECX,[EBP-30h]; MOV FS:[0],ECX). */
#define ES2_MISSION_ROOT_LOCALS 0x150u
#define ES2_MISSION_ROOT_SCRATCH 0x160u
#define ES2_MISSION_ROOT_OUTGOING 0x15cu
typedef struct es2_mission_root { es2_weapon_frame call; uint32_t top; } es2_mission_root;
uint64_t es2_mission_root_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_mission_root *frame,
    uint32_t caller_ebx, const uint32_t entry[3], uint32_t *ecx);
#endif
