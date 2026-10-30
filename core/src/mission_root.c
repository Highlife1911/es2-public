#include "es2/core/mission_root.h"
#include "fixed.h"
#include <stddef.h>
/* The mission-load root 0x461608 (docs/reverse-engineering/ mission_load.md,
 * section "the mission-load root"). Every guest read and write happens where
 * the original does it (the callees may change the view block, the pools, the
 * frame and the tables the loops reread), and r holds what the original's
 * EAX/EDX/ECX hold at the next game call, including the halves that 8- and
 * 16-bit loads keep. Addresses in the comments are the original's instructions.
 * */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define VA(x) (g->image+(uint32_t)(x)-0x400000u)

/* The view block 0x4d24bc (EDI throughout). */
enum { V_COCKPIT=0x4u, V_SOUND=0xcu, V_CONTEXT=0x12u, V_DETAIL=0x16u, V_DETAIL_DONE=0x18u, V_TAPE=0x1au,
    V_PLAYER=0x2au, V_CAMERA=0x2eu, V_DETAIL3=0x3au, V_GFX=0x4eu, V_INTRO=0x54u, V_SUBJECT=0x60u, V_LOADING=0x6eu,
    V_LEAVE=0x76u, V_MODE=0x7bu, V_LAYOUT=0x7cu, V_WIDTH=0x82u, V_PRESENT=0xaau, V_DETAIL2=0xb1u,
    V_DETAIL2_DONE=0xb3u, V_FLYER=0xb5u, V_RUNS=0xb7u, V_RATE=0xbbu };

typedef struct mr_state {
    const es2_guest_memory *m;
    const es2_weapon_graph *g;
    es2_mission_root *frame;
    uint32_t ebx, esi, edi;
    es2_weapon_registers r;
} mr_state;

static uint32_t lo16(uint32_t reg, uint32_t word) { return (reg & 0xffff0000u) | (uint16_t)word; }
static uint32_t lo8(uint32_t reg, uint32_t byte) { return (reg & 0xffffff00u) | (uint8_t)byte; }
static uint32_t sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }

/* The registers and return address of the CALL whose next instruction is
 * `ret` (original VA). */
static void mr_at(mr_state *s, uint32_t ret) {
    const es2_weapon_graph *g = s->g;
    s->frame->call.ebx = s->ebx; s->frame->call.esi = s->esi; s->frame->call.edi = s->edi;
    s->frame->call.ret = VA(ret);
}
static void mr_call(mr_state *s, uint32_t va, uint32_t ret, const uint32_t *a, unsigned n) {
    const es2_weapon_graph *g = s->g;
    mr_at(s, ret);
    if(g->observe) g->observe(g->observer_context, va, 0);
    g->call(g->context, VA(va), a, n, &s->r);
}
/* POP ECX after a one-slot call: the slot as the callee left it at the
 * original coordinate, else the pushed argument. */
static void mr_pop(mr_state *s, uint32_t arg) {
    const es2_guest_memory *m = s->m;
    s->r.ecx = s->frame->top ? R32(s->frame->top - 4u) : arg;
}
static void mr_one(mr_state *s, uint32_t va, uint32_t ret, uint32_t arg) {
    const uint32_t a[1] = {arg};
    mr_call(s, va, ret, a, 1);
    mr_pop(s, arg);
}
static void mr_two(mr_state *s, uint32_t va, uint32_t ret, uint32_t first, uint32_t second) {
    const uint32_t a[2] = {first, second};
    mr_call(s, va, ret, a, 2);
}
/* A drain: while pool_prev(pool, 0) finds an object, removal_append(list,
 * object) and the flushes. The pool and list globals are reread every
 * time. */
static void mr_drain(mr_state *s, uint32_t pool, uint32_t list, uint32_t prev_ret, uint32_t append_ret,
    const uint32_t *flushes, unsigned count) {
    const es2_guest_memory *m = s->m;
    const es2_weapon_graph *g = s->g;
    es2_weapon_registers *r = &s->r;
    for(;;) {
        r->ecx = R32(VA(pool));
        mr_two(s, 0x471c70u, prev_ret, r->ecx, 0u);            /* push 0; push ecx; call; add esp,8 */
        s->ebx = r->eax;
        if(!r->eax) break;
        r->edx = R32(VA(list));                                /* mov eax,ebx; push eax; mov edx,[list] */
        mr_two(s, 0x478628u, append_ret, r->edx, r->eax);
        for(unsigned i = 0; i < count; ++i) mr_call(s, flushes[i], flushes[count + i], NULL, 0);
    }
}

/* Original: DBSIM.EXE 0x461608 */
uint64_t es2_mission_root_registers(const es2_guest_memory *m, const es2_weapon_graph *g, es2_mission_root *frame,
    uint32_t caller_ebx, const uint32_t entry[3], uint32_t *ecx) {
    const uint32_t f = g->scratch + ES2_MISSION_ROOT_LOCALS, B = VA(0x4d24bcu);
    mr_state s = {m, g, frame, caller_ebx, f - 8u, B, {entry[0], entry[1], entry[2]}};
    es2_weapon_registers *r = &s.r;
    /* 461611: the runtime record at f-30h (descriptor 49F7C9h). */
    r->eax = VA(0x49f7c9u);
    mr_call(&s, 0x49630cu, 0x461626u, NULL, 0);
    /* 461626: the abort-notify callback 461304, the input set-up. */
    mr_one(&s, 0x479cfcu, 0x461630u, VA(0x461304u));
    mr_call(&s, 0x45a5dcu, 0x461636u, NULL, 0);
    if(R8(VA(0x4d267du))) mr_call(&s, 0x4955c4u, 0x461644u, NULL, 0);
    /* 461644: GetWindowRect(hwnd, f-108h), GetClientRect(hwnd, f-118h)
     * (stdcall thunks); the border sizes. */
    r->edx = f - 0x108u;
    r->ecx = R32(VA(0x4d3610u));
    mr_two(&s, 0x49694cu, 0x461657u, r->ecx, r->edx);
    r->eax = R32(f - 0x100u);
    r->ecx = f - 0x118u;
    r->eax -= R32(f - 0x108u);
    W32(f - 0x34u, r->eax);
    r->eax = R32(VA(0x4d3610u));
    r->edx = R32(f - 0xfcu);
    r->edx -= R32(f - 0x104u);
    W32(f - 0x38u, r->edx);
    mr_two(&s, 0x496970u, 0x461687u, r->eax, r->ecx);
    r->edx = R32(f - 0x34u) - R32(f - 0x110u);
    W32(f - 0x3cu, r->edx);
    r->ecx = R32(f - 0x38u) - R32(f - 0x10cu);
    W32(f - 0x40u, r->ecx);
    /* 46169f: graphics context, pump, frame clear, the launch peek. */
    mr_call(&s, 0x461294u, 0x4616a4u, NULL, 0);
    mr_call(&s, 0x464c78u, 0x4616a9u, NULL, 0);
    mr_call(&s, 0x46548cu, 0x4616aeu, NULL, 0);
    r->eax = f - 0x42u;
    r->edx = f - 0x44u;
    mr_two(&s, 0x4614c0u, 0x4616bbu, r->edx, r->eax);
    /* 4616be: the loading-screen name: the 4 bytes at 49F89Ah ("ld_"),
     * then "flyr" (49F89Eh) when the word f-42h is set, else "herc"
     * (49F8A4h), character 7 = '1' + the world byte. The MOVSD's PUSH
     * ESI/PUSH EDI lie in the next call's argument slots. */
    W32(f - 0x128u, R32(VA(0x49f89au)));
    r->eax = f - 0x128u;
    r->edx = R16(f - 0x42u) ? VA(0x49f89eu) : VA(0x49f8a4u);
    mr_two(&s, 0x4967eau, 0x4616f0u, r->eax, r->edx);
    r->ecx = lo8(r->ecx, R8(f - 0x44u) + 0x31u);
    W8(f - 0x121u, r->ecx);
    r->eax = R32(VA(0x4a09fcu));
    r->edx = f - 0x128u;
    mr_two(&s, 0x492becu, 0x461711u, r->edx, r->eax);
    /* 461714: the palette and its selection, pump, the bitmap. */
    mr_one(&s, 0x465f7cu, 0x46171au, r->eax);
    W32(f - 0x48u, r->eax);
    r->ecx = R32(f - 0x48u);
    mr_one(&s, 0x481fe0u, 0x461727u, r->ecx);
    mr_call(&s, 0x464c78u, 0x46172du, NULL, 0);
    r->eax = f - 0x128u;
    mr_one(&s, 0x461450u, 0x461739u, r->eax);
    /* 46173a: the graphics-library object (context 14h, 6 bytes). */
    mr_one(&s, 0x47749cu, 0x461741u, 6u);
    W32(f - 4u, r->eax);
    if(r->eax) {
        W16(f - 0x20u, 0x14u);
        r->edx = R32(f - 4u);
        mr_two(&s, 0x480af0u, 0x46175du, r->edx, 0x26d3u);
        r->ecx = R32(VA(0x6cbee4u));
        W32(r->ecx, R32(r->ecx) - 1u);
        W16(f - 0x20u, 8u);
    }
    r->eax = R32(f - 4u);
    W32(B + V_GFX, r->eax);
    /* 461779: the sound init. Slot 0 is the sound byte (the dword f-4Ch);
     * slots 1..5 the words f-50h .. f-60h that nothing writes (stack
     * residue at the original coordinate), with the high words the
     * registers carry; 462410 reads only slot 0. */
    r->edx = R8(B + V_SOUND);
    W32(f - 0x4cu, r->edx);
    {
        uint32_t a[6];
        r->ecx = lo16(r->ecx, R16(f - 0x60u)); a[5] = r->ecx;
        r->eax = lo16(r->eax, R16(f - 0x5cu)); a[4] = r->eax;
        r->edx = lo16(r->edx, R16(f - 0x58u)); a[3] = r->edx;
        r->ecx = lo16(r->ecx, R16(f - 0x54u)); a[2] = r->ecx;
        r->eax = lo16(r->eax, R16(f - 0x50u)); a[1] = r->eax;
        r->edx = lo16(r->edx, R16(f - 0x4cu)); a[0] = r->edx;
        mr_call(&s, 0x462410u, 0x4617a4u, a, 6);
    }
    mr_call(&s, 0x463370u, 0x4617acu, NULL, 0);
    mr_call(&s, 0x48dd08u, 0x4617b1u, NULL, 0);
    mr_call(&s, 0x4913dcu, 0x4617b6u, NULL, 0);
    /* 4617b6: the stage-2 callbacks, lights, limiter reference, pass 1. */
    mr_one(&s, 0x401d94u, 0x4617bdu, 2u);
    mr_call(&s, 0x46134cu, 0x4617c3u, NULL, 0);
    mr_call(&s, 0x467840u, 0x4617c8u, NULL, 0);
    mr_call(&s, 0x424308u, 0x4617cdu, NULL, 0);
    /* 4617cd: view+0BBh = 25 for a flyer mission, else 13. */
    r->ecx = R8(B + V_FLYER) ? 0x19u : 0xdu;
    W32(B + V_RATE, r->ecx);
    /* 4617e4: the terrain objects: count word f-68h (decremented before
     * the test), cursor f-6Ch; a null entry ends the loop. */
    r->eax = lo16(r->eax, R16(VA(0x4aaa7cu)));
    W16(f - 0x68u, r->eax);
    r->edx = R32(VA(0x4aaa78u));
    W32(f - 0x6cu, r->edx);
    for(;;) {
        r->ecx = lo16(r->ecx, R16(f - 0x68u));
        W16(f - 0x68u, R16(f - 0x68u) - 1u);
        if((uint16_t)r->ecx) {
            r->eax = R32(f - 0x6cu);
            W32(f - 0x6cu, R32(f - 0x6cu) + 4u);
            r->eax = R32(r->eax);
        } else r->eax = 0;
        if(!r->eax) break;
        mr_one(&s, 0x4282d8u, 0x4617ffu, r->eax);
    }
    /* 46181f: mouse queue, input byte, pass 2, the bitmap set-up, pump. */
    mr_call(&s, 0x452ae4u, 0x461824u, NULL, 0);
    W8(VA(0x4d1dd6u), 0);
    mr_call(&s, 0x4253d8u, 0x461830u, NULL, 0);
    mr_call(&s, 0x44f6f4u, 0x461835u, NULL, 0);
    mr_call(&s, 0x464c78u, 0x46183au, NULL, 0);
    /* 46183a: the drains (explosions, then debris). */
    {
        static const uint32_t first[6] = {0x407b3cu, 0x4077e8u, 0x409904u, 0x461853u, 0x461858u, 0x46185du};
        static const uint32_t second[2] = {0x4083e0u, 0x46188du};
        mr_drain(&s, 0x4a961eu, 0x4a9622u, 0x46186bu, 0x46184bu, first, 3);
        mr_drain(&s, 0x4a9642u, 0x4a9646u, 0x46189bu, 0x461885u, second, 1);
    }
    /* 4618a4: word 4CF880h, the player cleared; the height fix and the
     * player pick over the MECH pool (iterator f-70h, copied to f-74h). */
    r->edx = lo16(r->edx, R16(VA(0x49abbau)));
    r->ecx = 0;
    W16(VA(0x4cf880u), r->edx);
    W32(B + V_PLAYER, r->ecx);
    r->eax = 0;
    W32(f - 0x70u, r->eax);
    for(;;) {
        r->ecx = R32(f - 0x70u);
        W32(f - 0x74u, r->ecx);
        r->eax = R32(f - 0x74u);
        r->edx = R32(VA(0x4a9b7au));
        mr_two(&s, 0x471c70u, 0x46195du, r->edx, r->eax);
        W32(f - 0x70u, r->eax);
        s.ebx = r->eax;
        if(!r->eax) break;
        /* 4618c1: the position into f-134h, the ground height at it. */
        r->eax = s.ebx + 0x26u;
        r->edx = R32(r->eax);
        W32(f - 0x134u, r->edx);
        r->edx = f - 0x134u;
        r->ecx = R32(r->eax + 4u);
        W32(f - 0x130u, r->ecx);
        r->ecx = R32(VA(0x4a0be0u));
        r->eax = R32(r->eax + 8u);
        W32(f - 0x12cu, r->eax);
        mr_two(&s, 0x46e180u, 0x4618f1u, r->ecx, r->edx);
        W32(f - 0x12cu, r->eax);
        r->eax = R32(s.ebx + 0x1f2u);
        r->edx = sx16(R16(r->eax + 0x16u));
        W32(f - 0x12cu, R32(f - 0x12cu) + r->edx);
        if(R16(r->eax + 0x50u)) W32(f - 0x12cu, R32(f - 0x12cu) + 0x1388u);
        r->eax = s.ebx + 0x26u;
        r->edx = R32(f - 0x12cu);
        W32(r->eax + 8u, r->edx);
        r->ecx = R32(f - 0x134u);
        W32(r->eax, r->ecx);
        r->edx = R32(f - 0x130u);
        W32(r->eax + 4u, r->edx);
        r->eax = s.ebx + 0x92u;
        if(R8(r->eax + 0x11u)) W32(B + V_PLAYER, s.ebx);
    }
    /* 46196d: the player 4D2684h, the terrain object's callback +125h. */
    r->edx = R32(B + V_PLAYER);
    W32(VA(0x4d2684u), r->edx);
    r->ecx = R32(VA(0x4a0be0u));
    W32(f - 0x78u, r->ecx);
    r->eax = R32(f - 0x78u);
    W32(r->eax + 0x125u, VA(0x428c60u));
    /* 46198c: one camera-like object per member of the player's group
     * (count word f-7Ah, counter f-80h) into 4D2688h[], the object slot
     * [ESI] = f-8 (context 2Ch while it is built). */
    s.ebx = VA(0x4d2688u);
    r->eax = R32(B + V_PLAYER);
    r->edx = R32(r->eax + 0x45u);
    r->ecx = lo16(r->ecx, R16(r->edx + 0x10u));
    W16(f - 0x7au, r->ecx);
    r->eax = 0;
    W32(f - 0x80u, r->eax);
    for(;;) {
        r->eax = sx16(R16(f - 0x7au));
        if(!((int32_t)r->eax > (int32_t)R32(f - 0x80u))) break;
        mr_one(&s, 0x47749cu, 0x4619b0u, 0x4eu);
        W32(s.esi, r->eax);
        if(r->eax) {
            W16(f - 0x20u, 0x2cu);
            r->edx = R32(s.esi);
            mr_one(&s, 0x48bd84u, 0x4619c9u, r->edx);
            r->ecx = R32(s.esi); W32(r->ecx, VA(0x49f8e8u));
            r->eax = R32(s.esi) + 0x26u; W32(f - 0x84u, r->eax);
            r->edx = R32(s.esi) + 0x2cu; W32(f - 0x88u, r->edx);
            r->ecx = R32(s.esi) + 0x38u; W32(f - 0x8cu, r->ecx);
            r->eax = R32(s.esi) + 0x3eu; W32(f - 0x90u, r->eax);
            r->edx = R32(s.esi) + 0x44u; W32(f - 0x94u, r->edx);
            r->ecx = R32(s.esi); r->eax = 0; W32(r->ecx + 0x32u, r->eax);
            r->edx = R32(s.esi); r->ecx = 0; W32(r->edx + 0x4au, r->ecx);
            r->eax = R32(s.esi); W16(r->eax + 0x36u, 0);
            r->edx = R32(s.esi); r->ecx = 0; W16(r->edx + 0x48u, r->ecx);
            r->eax = R32(s.esi); W16(r->eax + 0x46u, r->ecx);
            r->eax = R32(s.esi); W16(r->eax + 0x44u, r->ecx);
            r->edx = R32(VA(0x6cbee4u)); W32(r->edx, R32(r->edx) + 1u);
            r->eax = R32(VA(0x6cbee4u)); W32(r->eax, R32(r->eax) - 3u);
            W16(f - 0x20u, 0x20u);
        }
        r->edx = R32(s.esi);
        W32(s.ebx, r->edx);
        W32(f - 0x80u, R32(f - 0x80u) + 1u);
        s.ebx += 4u;
    }
    /* 461a64: the view subject, pump, frame clear, pump. */
    r->ecx = R32(B + V_PLAYER);
    W32(B + V_SUBJECT, r->ecx);
    mr_call(&s, 0x464c78u, 0x461a6fu, NULL, 0);
    mr_call(&s, 0x46548cu, 0x461a74u, NULL, 0);
    mr_call(&s, 0x464c78u, 0x461a79u, NULL, 0);
    /* 461a79: below 640 pixels the cursor sprite lo_curs (49F8AAh). */
    if((int32_t)R32(B + V_WIDTH) < 0x280) {
        r->eax = R32(VA(0x4a0a10u));
        mr_two(&s, 0x492becu, 0x461a95u, VA(0x49f8aau), r->eax);
        W32(f - 0x98u, r->eax);
        r->edx = R32(f - 0x98u);
        mr_one(&s, 0x45cdf4u, 0x461aaau, r->edx);
        W32(f - 0x9cu, r->eax);
        r->ecx = R32(f - 0x9cu);
        mr_one(&s, 0x465618u, 0x461abdu, r->ecx);
    }
    /* 461abe: a value into f-0A0h that nothing reads (and f-0A4h = 0). */
    r->eax = 0;
    W32(f - 0xa4u, r->eax);
    r->edx = sx8(R8(B + V_MODE)) - 1u;
    if(!r->edx) {
        r->ecx = R8(B + 0xbu) ? 0u : 1u;
        W32(f - 0xa0u, r->ecx);
    } else if(R16(B + V_PRESENT)) {
        r->eax = 0;
        W32(f - 0xa0u, r->eax);
    } else {
        r->edx = R8(B + 0xbu) ? 0u : 2u;
        W32(f - 0xa0u, r->edx);
    }
    /* 461b03 (a CMP without a consumer): the HUD set-up; the modal
     * callbacks. */
    mr_call(&s, 0x4544c0u, 0x461b10u, NULL, 0);
    W32(VA(0x49dd94u), VA(0x461250u));
    W32(VA(0x49dd98u), VA(0x461274u));
    /* 461b24: the main camera (context 44h): 9 above 1200 of dword 4D3040h,
     * else 8; the vectors (0, -6000, 0) at f-140h and three zero words at
     * f-0B8h. */
    r->ecx = (int32_t)R32(VA(0x4d3040u)) > 0x4b0 ? 9u : 8u;
    W32(f - 0xb0u, r->ecx);
    mr_one(&s, 0x47749cu, 0x461b43u, 0x4eu);
    W32(f - 0xcu, r->eax);
    if(r->eax) {
        W16(f - 0x20u, 0x44u);
        W16(f - 0xb8u, 0); W16(f - 0xb6u, 0); W16(f - 0xb4u, 0);
        r->edx = 0; r->ecx = 0;
        W32(f - 0x140u, r->edx);
        W32(f - 0x13cu, 0xffffe890u);
        W32(f - 0x138u, r->ecx);
        r->ecx = f - 0xb8u;
        r->eax = R32(f - 0xb0u);
        W32(f - 0xbcu, r->eax);
        r->eax = f - 0x140u;
        r->edx = R32(B + V_CONTEXT);
        W32(f - 0xc0u, r->edx);
        {
            const uint32_t angles = r->ecx, position = r->eax;
            r->edx = R32(f - 0xbcu);
            r->ecx = R32(f - 0xc0u);
            r->eax = R32(f - 0xcu);
            const uint32_t a[6] = {r->eax, r->ecx, r->edx, 0x100u, position, angles};
            mr_call(&s, 0x48bda4u, 0x461bc9u, a, 6);
        }
        r->edx = R32(f - 0xcu); W32(r->edx, VA(0x49f8e8u));
        r->ecx = R32(f - 0xcu) + 0x26u; W32(f - 0xc4u, r->ecx);
        r->eax = R32(f - 0xcu) + 0x2cu; W32(f - 0xc8u, r->eax);
        r->edx = R32(f - 0xcu) + 0x38u; W32(f - 0xccu, r->edx);
        r->ecx = R32(f - 0xcu) + 0x3eu; W32(f - 0xd0u, r->ecx);
        r->eax = R32(f - 0xcu) + 0x44u; W32(f - 0xd4u, r->eax);
        r->edx = R32(f - 0xcu); r->ecx = 0; W32(r->edx + 0x32u, r->ecx);
        r->eax = R32(f - 0xcu); r->edx = 0; W32(r->eax + 0x4au, r->edx);
        r->ecx = R32(f - 0xcu); W16(r->ecx + 0x36u, 0);
        r->eax = R32(f - 0xcu); r->edx = 0; W16(r->eax + 0x48u, r->edx);
        r->eax = R32(f - 0xcu); W16(r->eax + 0x46u, r->edx);
        r->eax = R32(f - 0xcu); W16(r->eax + 0x44u, r->edx);
        r->ecx = R32(VA(0x6cbee4u)); W32(r->ecx, R32(r->ecx) + 1u);
        r->eax = R32(VA(0x6cbee4u)); W32(r->eax, R32(r->eax) - 3u);
        W16(f - 0x20u, 0x38u);
    }
    r->edx = R32(f - 0xcu);
    W32(B + V_CAMERA, r->edx);
    /* 461c62: the view object of the player; camera word +36h = 2 when
     * +4Ah is 0; the loading word. */
    r->eax = R32(VA(0x4d2684u));
    mr_one(&s, 0x45edb0u, 0x461c6du, r->eax);
    r->eax = R32(B + V_CAMERA);
    if(!R32(r->eax + 0x4au)) W16(r->eax + 0x36u, 2u);
    W16(B + V_LOADING, 1u);
    /* 461c83: the cockpit (context 5Ch while it is built, then 50h). */
    mr_one(&s, 0x47749cu, 0x461c8au, 0x37u);
    W32(f - 0x10u, r->eax);
    if(r->eax) {
        W16(f - 0x20u, 0x5cu);
        r->edx = R32(B + V_PLAYER);
        r->ecx = R32(f - 0x10u);
        mr_two(&s, 0x429660u, 0x461ca5u, r->ecx, r->edx);
        W16(f - 0x20u, 0x50u);
    }
    r->eax = R32(f - 0x10u);
    W32(B + V_COCKPIT, r->eax);
    /* 461cb9: the order group set-up; the per-MECH display objects
     * (iterator f-0D8h, in ESI at the walk's call). */
    r->edx = R32(VA(0x49b088u));
    mr_one(&s, 0x431530u, 0x461cc5u, r->edx);
    r->ecx = 0;
    W32(f - 0xd8u, r->ecx);
    for(;;) {
        s.esi = R32(f - 0xd8u);
        r->eax = R32(VA(0x4a9b7au));
        mr_two(&s, 0x471c70u, 0x461cefu, r->eax, s.esi);
        W32(f - 0xd8u, r->eax);
        s.ebx = r->eax;
        if(!r->eax) break;
        mr_one(&s, 0x437c8cu, 0x461cd6u, s.ebx);
        W32(s.ebx + 0x1feu, r->eax);
    }
    /* 461cfe: cockpit sync, the first camera step, the layout switch
     * (D, 1) unless byte view+7Ch is 2, then (D, 0). */
    r->edx = R32(B + V_PLAYER);
    mr_one(&s, 0x41b130u, 0x461d07u, r->edx);
    r->ecx = R32(B + V_CAMERA);
    mr_one(&s, 0x4011a0u, 0x461d11u, r->ecx);
    r->eax = sx8(R8(B + V_LAYOUT));
    if(r->eax != 2u) {
        r->edx = R32(B + V_COCKPIT);
        mr_two(&s, 0x42a1f0u, 0x461d26u, r->edx, 1u);
    }
    r->ecx = R32(B + V_COCKPIT);
    mr_two(&s, 0x42a1f0u, 0x461d34u, r->ecx, 0u);
    /* 461d34: the building search: the first building of a category-0
     * group within 40000 of the player ends it; its result, f-0DAh and
     * f-0E0h are never used. */
    r->eax = lo16(r->eax, R16(VA(0x4a9e54u)));
    W16(f - 0xdau, r->eax);
    r->edx = 0;
    W32(f - 0xe0u, r->edx);
    for(;;) {
        s.esi = R32(f - 0xe0u);
        r->eax = R32(VA(0x4a95a0u));
        mr_two(&s, 0x471c70u, 0x461d83u, r->eax, s.esi);
        W32(f - 0xe0u, r->eax);
        s.ebx = r->eax;
        if(!r->eax) break;
        r->ecx = R32(s.ebx + 0x45u);
        if(R8(r->ecx + 0x12u)) continue;
        s.esi = R32(B + V_PLAYER) + 0x26u;
        s.ebx += 0x26u;
        mr_two(&s, 0x49288cu, 0x461d67u, s.ebx, s.esi);
        if((int32_t)r->eax < 0x9c40) break;
    }
    /* 461d92: with a sound context and header word 4 = 0 the CD track
     * (the -R counter mod 5) + 2, looping. */
    if(R32(VA(0x49f8ecu)) && !R16(VA(0x4aa728u))) {
        const int32_t runs = (int32_t)R32(B + V_RUNS);
        r->ecx = 5u;
        r->eax = (uint32_t)(runs / 5);                         /* cdq; idiv ecx */
        r->edx = (uint32_t)(runs % 5) + 2u;
        W32(VA(0x49f8fcu), r->edx);
        W8(VA(0x49f900u), 1u);
        mr_call(&s, 0x46313cu, 0x461dc8u, NULL, 0);
    }
    /* 461dd2: the first render, or the intro. */
    if(!R16(B + V_INTRO)) {
        W16(VA(0x49aef6u), 8u);
        r->eax = R32(B + V_CAMERA);
        mr_one(&s, 0x42eb40u, 0x461debu, r->eax);
        mr_call(&s, 0x43048cu, 0x461df1u, NULL, 0);
        mr_call(&s, 0x45fca8u, 0x461df6u, NULL, 0);
    } else mr_call(&s, 0x45d548u, 0x461dfdu, NULL, 0);
    W16(B + V_LOADING, 0);
    mr_call(&s, 0x464c78u, 0x461e08u, NULL, 0);
    mr_call(&s, 0x43048cu, 0x461e0du, NULL, 0);
    mr_call(&s, 0x464c78u, 0x461e12u, NULL, 0);
    /* 461e12: present, or the text rectangle (0..320, 0..400) ordered by
     * the two comparisons of constants (both always fall through). */
    if(R16(B + V_PRESENT)) mr_call(&s, 0x464a14u, 0x461e21u, NULL, 0);
    else {
        W32(f - 0xecu, 0x140u);
        r->edx = 0x190u;
        W32(f - 0xe8u, r->edx);
        r->ecx = 0;
        W32(f - 0xf4u, r->ecx);
        r->eax = 0;
        W32(f - 0xf0u, r->eax);
        r->ecx = R32(f - 0xecu);
        if((int32_t)r->ecx > (int32_t)R32(f - 0xf4u)) {
            r->ecx = R32(f - 0xf4u); W32(f - 0x150u, r->ecx);
            r->ecx = R32(f - 0xecu); W32(f - 0x148u, r->ecx);
        } else {
            r->ecx = R32(f - 0xecu); W32(f - 0x150u, r->ecx);
            r->ecx = R32(f - 0xf4u); W32(f - 0x148u, r->ecx);
        }
        if((int32_t)r->edx > (int32_t)r->eax) { W32(f - 0x14cu, r->eax); W32(f - 0x144u, r->edx); }
        else { W32(f - 0x14cu, r->edx); W32(f - 0x144u, r->eax); }
        r->eax = f - 0x150u;
        const uint32_t a[3] = {1u, r->eax, 0u};
        mr_call(&s, 0x43122cu, 0x461eb9u, a, 3);
    }
    /* 461ebc: the start sequence, byte 49B105h, the HUD step, the
     * graphics slot [4A5824h]. */
    mr_call(&s, 0x4328ccu, 0x461ec1u, NULL, 0);
    W8(VA(0x49b105u), 1u);
    mr_call(&s, 0x433f7cu, 0x461ecdu, NULL, 0);
    {
        const uint32_t target = R32(VA(0x4a5824u));
        mr_at(&s, 0x461ed3u);
        if(g->observe) g->observe(g->observer_context, target, 0x4a5824u);
        g->call(g->context, target, NULL, 0, r);
    }
    /* 461ed3: the detail levels from the prefs bytes. */
    r->edx = R8(VA(0x4d1f41u));
    W16(B + V_DETAIL_DONE, r->edx);
    W16(B + V_DETAIL, r->edx);
    r->eax = lo16(r->eax, R16(B + V_DETAIL));
    mr_one(&s, 0x45d490u, 0x461eedu, r->eax);
    r->ecx = R8(VA(0x4d1f42u));
    W16(B + V_DETAIL2_DONE, r->ecx);
    r->eax = lo16(r->eax, R16(B + V_DETAIL2_DONE));
    W16(B + V_DETAIL2, r->eax);
    r->edx = lo16(r->edx, R16(B + V_DETAIL2));
    mr_one(&s, 0x45d50cu, 0x461f18u, r->edx);
    r->ecx = R8(VA(0x4d1f40u));
    W16(B + V_DETAIL3, r->ecx);
    /* 461f25: the drop sequence, the input byte unless tape playback, the
     * display set-up, pump, the key ring flush; the leave request. */
    if(R16(B + V_INTRO)) mr_call(&s, 0x45d85cu, 0x461f31u, NULL, 0);
    if(!R16(B + V_TAPE)) W8(VA(0x4d1dd6u), 1u);
    mr_call(&s, 0x45b758u, 0x461f44u, NULL, 0);
    mr_call(&s, 0x464c78u, 0x461f49u, NULL, 0);
    r->eax = R32(VA(0x6c5eb8u));
    W32(f - 0xf8u, r->eax);
    r->edx = R32(f - 0xf8u);
    mr_one(&s, 0x477abcu, 0x461f60u, r->edx);
    W16(B + V_LEAVE, 0);
    /* 461f67: ECX = the record's saved head (the caller writes FS:0). */
    r->ecx = R32(f - 0x30u);
    if(ecx) *ecx = r->ecx;
    return (uint64_t)r->edx << 32 | r->eax;
}
