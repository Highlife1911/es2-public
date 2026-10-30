#ifndef ES2_CORE_THROTTLE_H
#define ES2_CORE_THROTTLE_H
#include "es2/core/weapons.h"
/* The throttle slider's event slot (4483a0), the base gadget event it
 * starts with (43904c) and the shield balance step (413af8). Exact
 * contracts: docs/reverse-engineering/cockpit.md, section "throttle
 * slider event and shield step". Game calls go through g->call with the
 * registers the original sets up, so nested hooks keep their own modes;
 * g->image locates the globals. No scratch memory is needed. Every function
 * returns EDX:EAX exactly; ECX is outside the contract (see the section). */

/* slider_base 43904c (G, event): three virtual calls on the gadget G, in the
 * original's order: the virtual 0 of the object at G+42h with (that object,
 * G, event) and then the virtual 4 of the class table at G+17h with (G), and
 * the virtual 8 of the table at G+3Eh (the dword at G+3Eh points to it) with
 * (G+3Eh). Returns EDX:EAX of the last. Every target is read when it is
 * called. G+42h, G+17h and G+3Eh are reread after the preceding virtual. */
uint64_t es2_slider_base_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t gadget,uint32_t event,const uint32_t entry[3]);
/* slider_event 4483a0 (G, event): 43904c(G, event) through the guest bridge
 * (the nested hook keeps its mode), then, when bit 0 of the byte 49db6c is
 * set, the joystick summary 477904 (guest call; EAX = pointer P) and, when
 * byte P+4 is nonzero and byte 4d1f46 equals 1, the lever word 49a06e = 1
 * for a signed event <= 0 and 0FFFFh otherwise. EAX keeps the high word of P
 * (-1 borrowed from it when the lever is -1). */
uint64_t es2_slider_event_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t gadget,uint32_t event,const uint32_t entry[3]);
/* shield_step 413af8 (S, up): the balance word S+4 += 66h (word up <> 0) or
 * -66h, then clamped to 0..400h as signed words; no game call. EAX = S;
 * EDX = (entry EDX high word, minus one for a down step) : the unclamped
 * sum; ECX is outside the contract. */
uint64_t es2_shield_step_registers(const es2_guest_memory *,uint32_t s,uint32_t up,const uint32_t entry[3]);
#endif
