#include "es2/core/render_root.h"
/* The render root, the stage dispatcher and the vector copy
 * (docs/reverse-engineering/main_loop.md, section "the render root as own
 * code"). Every guest read and write happens where the original does it (the
 * callees may change the view block, the cockpit, the camera, the stage table
 * and the frame), and r holds what the original's EAX/EDX/ECX hold at the next
 * game call, including the halves that 16-bit loads keep. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(x) (g->image+(uint32_t)(x)-0x400000u)
#define LO16(reg,v) ((reg)=((reg)&0xffff0000u)|(uint16_t)(v))

/* The view block 0x4d24bc (EBX of 45fca8): detail words, the graphics
 * context, the player, the camera, the second detail pair, the debug
 * byte. */
enum { V_DETAIL=0x16u, V_DETAIL_DONE=0x18u, V_CONTEXT=0x12u, V_PLAYER=0x2au, V_CAMERA=0x2eu,
    V_DETAIL2=0xb1u, V_DETAIL2_DONE=0xb3u, V_DEBUG=0x9u };
/* The cockpit [49B088h]: the HUD and the EventPort. */
enum { H_HUD=0x1f5u, H_EVENTS=0x20bu };

typedef struct rr_state {
    const es2_guest_memory *m;
    const es2_weapon_graph *g;
    es2_render_root *frame;
    uint32_t ebx, esi, edi;
    es2_weapon_registers r;
} rr_state;

/* The registers and return address of the CALL at `ret` (original VA). */
static void rr_at(rr_state *s, uint32_t ret) {
    const es2_weapon_graph *g = s->g;
    s->frame->call.ebx = s->ebx; s->frame->call.esi = s->esi; s->frame->call.edi = s->edi;
    s->frame->call.ret = VA(ret);
}
static void rr_call(rr_state *s, uint32_t va, uint32_t ret, const uint32_t *a, unsigned n) {
    const es2_weapon_graph *g = s->g;
    rr_at(s, ret);
    if(g->observe) g->observe(g->observer_context, va, 0);
    g->call(g->context, VA(va), a, n, &s->r);
}
/* POP ECX after a one-slot call: the slot as the callee left it at the
 * original coordinate, else the pushed argument. */
static void rr_pop(rr_state *s, uint32_t arg) {
    const es2_guest_memory *m = s->m;
    s->r.ecx = s->frame->top ? R32(s->frame->top - 4u) : arg;
}
static void rr_one(rr_state *s, uint32_t va, uint32_t ret, uint32_t arg) {
    const uint32_t a[1] = {arg};
    rr_call(s, va, ret, a, 1);
    rr_pop(s, arg);
}
/* The one-slot virtual `slot` of o: the vtable and target are read at the
 * call, the registers are loaded by the caller; then POP ECX. */
static void rr_virtual(rr_state *s, uint32_t o, uint32_t vt, unsigned slot, uint32_t ret) {
    const es2_guest_memory *m = s->m;
    const es2_weapon_graph *g = s->g;
    const uint32_t target = R32(vt + slot);
    const uint32_t a[1] = {o};
    rr_at(s, ret);
    if(g->observe) g->observe(g->observer_context, target, slot);
    g->call(g->context, target, a, 1, &s->r);
}

/* Original: DBSIM.EXE 0x45fca8 */
uint64_t es2_render_root_registers(const es2_guest_memory *m, const es2_weapon_graph *g, es2_render_root *frame,
    uint32_t caller_esi, uint32_t caller_edi, const uint32_t entry[3], uint32_t *ecx) {
    const uint32_t B = VA(0x4d24bcu), F = g->scratch;
    rr_state s = {m, g, frame, B, caller_esi, caller_edi, {entry[0], entry[1], entry[2]}};
    es2_weapon_registers *r = &s.r;
    /* 45fcb1: the stage-5 callbacks (removal flushes). */
    rr_one(&s, 0x401d94u, 0x45fcb8u, 5u);
    /* 45fcb9: detail setting 1 when its byte changed. */
    r->eax = R8(VA(0x4d1f41u));
    W16(B + V_DETAIL, r->eax);
    if((uint16_t)r->eax != R16(B + V_DETAIL_DONE)) {
        rr_one(&s, 0x45d490u, 0x45fcd0u, r->eax);
        LO16(r->edx, R16(B + V_DETAIL));
        W16(B + V_DETAIL_DONE, r->edx);
    }
    /* 45fcd9: detail setting 2. */
    r->eax = R8(VA(0x4d1f42u));
    W16(B + V_DETAIL2, r->eax);
    if((uint16_t)r->eax != R16(B + V_DETAIL2_DONE)) {
        rr_one(&s, 0x45d50cu, 0x45fcf6u, r->eax);
        LO16(r->edx, R16(B + V_DETAIL2));
        W16(B + V_DETAIL2_DONE, r->edx);
    }
    /* 45fd05: the jolt point into the frame, added to the canvas origin. */
    rr_call(&s, 0x42d870u, 0x45fd0au, NULL, 0);
    const uint32_t P = r->eax;
    r->ecx = R32(P); W32(F, r->ecx);
    r->eax = R32(P + 4u); W32(F + 4u, r->eax);
    r->eax = R32(B + V_CONTEXT) + 0x220u;
    r->edx = R32(F); W32(r->eax, R32(r->eax) + r->edx);
    r->ecx = R32(F + 4u); W32(r->eax + 4u, R32(r->eax + 4u) + r->ecx);
    /* 45fd2a: camera setup, terrain draw, panel frame, object binning. */
    r->eax = R32(B + V_CAMERA);
    rr_one(&s, 0x48c2e4u, 0x45fd33u, r->eax);
    r->edx = R32(B + V_CAMERA);
    r->ecx = R32(VA(0x4a0be0u));
    { const uint32_t a[3] = {r->ecx, r->edx, 0}; rr_call(&s, 0x46cb9cu, 0x45fd46u, a, 3); }
    r->eax = R32(VA(0x49b088u));
    rr_one(&s, 0x4327acu, 0x45fd54u, r->eax);
    rr_call(&s, 0x42841cu, 0x45fd5au, NULL, 0);
    /* 45fd5a: the 3D view of this layout. */
    rr_call(&s, 0x42db18u, 0x45fd5fu, NULL, 0);
    if((uint8_t)r->eax) {
        r->edx = R32(B + V_CAMERA);
        rr_one(&s, 0x48c2e4u, 0x45fd6cu, r->edx);
        r->eax = R32(B + V_CAMERA) + 4u;
        r->edx = R32(VA(0x4a9608u));
        { const uint32_t a[2] = {r->edx, r->eax}; rr_call(&s, 0x40707cu, 0x45fd80u, a, 2); }
        r->ecx = R32(B + V_CAMERA);
        r->eax = R32(VA(0x4a0be0u));
        { const uint32_t a[3] = {r->eax, r->ecx, 0}; rr_call(&s, 0x46cb9cu, 0x45fd94u, a, 3); }
        rr_one(&s, 0x48c444u, 0x45fd9eu, 0u);
        r->edx = R32(B + V_CAMERA);
        rr_one(&s, 0x42e700u, 0x45fda8u, r->edx);
        r->ecx = R8(B + V_DEBUG);
        r->eax = R32(B + V_CAMERA);
        { const uint32_t a[2] = {r->eax, r->ecx}; rr_call(&s, 0x42883cu, 0x45fdb8u, a, 2); }
    }
    /* 45fdbb: the cockpit sync for the player. */
    r->edx = R32(B + V_PLAYER);
    rr_one(&s, 0x41b130u, 0x45fdc4u, r->edx);
    /* 45fdc5: HUD draw [H+1F5h] +8 (EAX object, EDX vtable, ECX H). */
    r->ecx = R32(VA(0x49b088u));
    r->eax = R32(r->ecx + H_HUD);
    { const uint32_t o = r->eax; r->edx = R32(o); rr_virtual(&s, o, r->edx, 8u, 0x45fdd7u); rr_pop(&s, o); }
    /* 45fdd8: the message port's frame (EAX object, ECX vtable). */
    rr_call(&s, 0x433158u, 0x45fdddu, NULL, 0);
    { const uint32_t o = r->eax; r->ecx = R32(o); rr_virtual(&s, o, r->ecx, 0x10u, 0x45fde3u); rr_pop(&s, o); }
    /* 45fde4: the EventPort's frame (EAX H, EDX object, ECX vtable). */
    r->eax = R32(VA(0x49b088u));
    { const uint32_t o = R32(r->eax + H_EVENTS); r->edx = o; r->ecx = R32(o); rr_virtual(&s, o, r->ecx, 0x10u, 0x45fdf5u);
      /* 45fdf5: the context is read before the POP ECX. */
      r->eax = R32(B + V_CONTEXT); rr_pop(&s, o); }
    /* 45fdf9: the jolt subtracted from the (re-read) canvas origin. */
    r->eax += 0x220u;
    r->edx = R32(F); W32(r->eax, R32(r->eax) - r->edx);
    r->ecx = R32(F + 4u); W32(r->eax + 4u, R32(r->eax + 4u) - r->ecx);
    /* 45fe0a: the cursor point into the frame; widget hover and draw. */
    r->ecx = F + 8u;
    r->eax = R32(VA(0x4d22c8u));
    r->edx = R32(VA(0x4d22ccu));
    W32(F + 8u, r->eax); W32(F + 0xcu, r->edx);
    r->eax = R32(VA(0x49b088u));
    { const uint32_t a[2] = {r->eax, r->ecx}; rr_call(&s, 0x434520u, 0x45fe2du, a, 2); }
    *ecx = r->ecx;
    return (uint64_t)r->edx << 32 | r->eax;
}

/* Original: DBSIM.EXE 0x401d94 */
uint64_t es2_stage_dispatch_registers(const es2_guest_memory *m, const es2_weapon_graph *g, es2_weapon_frame *frame,
    uint32_t stage, const uint32_t entry[3]) {
    es2_weapon_registers r = {entry[0], entry[1], entry[2]};
    uint32_t ebx = VA(0x4a804cu), esi = 0;
    /* 401db2: CMP SI,[497074h]; JL (signed words, the count re-read). */
    while((int16_t)(uint16_t)esi < (int16_t)R16(VA(0x497074u))) {
        if((uint16_t)stage == R16(ebx + 4u)) {
            const uint32_t target = R32(ebx);
            frame->ebx = ebx; frame->esi = esi; frame->edi = stage; frame->ret = VA(0x401daeu);
            if(g->observe) g->observe(g->observer_context, target - g->image + 0x400000u, 0);
            g->call(g->context, target, NULL, 0, &r);
        }
        ++esi;
        ebx += 6u;
    }
    return (uint64_t)r.edx << 32 | r.eax;
}

/* Original: DBSIM.EXE 0x40707c */
uint64_t es2_vector_copy_registers(const es2_guest_memory *m, uint32_t dst, uint32_t src, uint32_t *ecx) {
    uint32_t c = R32(src + 8u);
    W32(dst + 8u, c);
    c = R32(src);
    W32(dst, c);
    const uint32_t d = R32(src + 4u);
    W32(dst + 4u, d);
    *ecx = c;
    return (uint64_t)d << 32 | dst;
}

/* The removal flushes (main_loop.md, section "the stage-5
 * flushes and the manager slot methods"). Each reads the pool P, then the
 * list P+4, and calls removal_flush(list, pool) with EAX = pool, EDX =
 * list; 408f9c then calls 408f84, 46b188 then resets 6b4f3c. */
static const struct { uint32_t va, pool; } rr_flushes[ES2_STAGE_FLUSHES] = {
    {0x405ea4u,0x4a95a0u},{0x4077e8u,0x4a95feu},{0x407b3cu,0x4a961eu},{0x4083e0u,0x4a9642u},
    {0x408f84u,0x4a966eu},{0x408f9cu,0x4a9684u},{0x409904u,0x4a968du},{0x409a1cu,0x4a96aau},
    {0x40a108u,0x4a96c2u},{0x41544cu,0x4a9b7au},{0x4215a8u,0x4a9db9u},{0x46b188u,0x6b4f30u}};

uint32_t es2_stage_flush_pool(unsigned index) { return index < ES2_STAGE_FLUSHES ? rr_flushes[index].pool : 0; }
uint32_t es2_stage_flush_va(unsigned index) { return index < ES2_STAGE_FLUSHES ? rr_flushes[index].va : 0; }

/* Original: DBSIM.EXE 0x405ea4, 0x4077e8, 0x407b3c, 0x4083e0, 0x408f84,
 * 0x408f9c, 0x409904, 0x409a1c, 0x40a108, 0x41544c, 0x4215a8, 0x46b188 */
uint64_t es2_stage_flush_registers(const es2_guest_memory *m, const es2_weapon_graph *g, unsigned index,
    const uint32_t entry[3], uint32_t *ecx) {
    es2_weapon_registers r = {entry[0], entry[1], entry[2]};
    if(index >= ES2_STAGE_FLUSHES) { *ecx = r.ecx; return (uint64_t)r.edx << 32 | r.eax; }
    const uint32_t pool = rr_flushes[index].pool, va = rr_flushes[index].va;
    r.eax = R32(VA(pool));
    r.edx = R32(VA(pool + 4u));
    { const uint32_t a[2] = {r.edx, r.eax};
      if(g->observe) g->observe(g->observer_context, 0x478644u, 0);
      g->call(g->context, VA(0x478644u), a, 2, &r); }
    if(va == 0x408f9cu) {
        if(g->observe) g->observe(g->observer_context, 0x408f84u, 0);
        g->call(g->context, VA(0x408f84u), NULL, 0, &r);
    } else if(va == 0x46b188u) W32(VA(0x6b4f3cu), 0x7fffffffu);
    *ecx = r.ecx;
    return (uint64_t)r.edx << 32 | r.eax;
}
