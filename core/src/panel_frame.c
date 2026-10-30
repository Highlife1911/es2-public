#include "es2/core/panel_frame.h"
/* The cockpit panel frame (docs/reverse-engineering/hud.md, section "cockpit
 * panel frame as own code"). Every guest read is done where the original does
 * it (the callees may change the cockpit object), and r holds what the
 * original's EAX/EDX/ECX hold at the next game call. */
#define R32(a) m->read32(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define VA(x) (g->image+(uint32_t)(x)-0x400000u)

typedef struct pf_state {
    const es2_guest_memory *m;
    const es2_weapon_graph *g;
    es2_panel_frame *frame;
    uint32_t ebx, esi, edi;
    es2_weapon_registers r;
} pf_state;

/* The registers and return address of the CALL at `ret` (original VA). */
static void pf_at(pf_state *s, uint32_t ret) {
    if(!s->frame) return;
    const es2_weapon_graph *g = s->g;
    s->frame->call.ebx = s->ebx; s->frame->call.esi = s->esi; s->frame->call.edi = s->edi;
    s->frame->call.ret = VA(ret);
}
/* POP ECX after a one-slot call: the slot as the callee left it at the
 * original coordinate, else the pushed argument. */
static void pf_pop(pf_state *s, uint32_t arg) {
    const es2_guest_memory *m = s->m;
    s->r.ecx = s->frame && s->frame->top ? R32(s->frame->top - 4u) : arg;
}
static void pf_call(pf_state *s, uint32_t va, uint32_t ret, const uint32_t *a, unsigned n) {
    const es2_weapon_graph *g = s->g;
    pf_at(s, ret);
    if(g->observe) g->observe(g->observer_context, va, 0);
    g->call(g->context, VA(va), a, n, &s->r);
}
/* The one-slot virtual `slot` of object o: EAX/ECX/EDX as the original
 * loads them (obj_reg receives o, vt_reg its vtable), then POP ECX. */
static void pf_virtual(pf_state *s, uint32_t o, unsigned slot, uint32_t ret, uint32_t *obj_reg, uint32_t *vt_reg) {
    const es2_guest_memory *m = s->m;
    const es2_weapon_graph *g = s->g;
    *obj_reg = o;
    const uint32_t vt = R32(o);
    *vt_reg = vt;
    const uint32_t target = R32(vt + slot);
    const uint32_t a[1] = {o};
    pf_at(s, ret);
    if(g->observe) g->observe(g->observer_context, target, slot);
    g->call(g->context, target, a, 1, &s->r);
    pf_pop(s, o);
}
static void pf_one(pf_state *s, uint32_t va, uint32_t ret, uint32_t arg) {
    const uint32_t a[1] = {arg};
    pf_call(s, va, ret, a, 1);
    pf_pop(s, arg);
}

/* Original: DBSIM.EXE 0x4327ac */
uint64_t es2_panel_frame_registers(const es2_guest_memory *m, const es2_weapon_graph *g, es2_panel_frame *frame,
    uint32_t caller_esi, uint32_t caller_edi, uint32_t hud, const uint32_t entry[3]) {
    const uint32_t H = hud;
    pf_state s = {m, g, frame, H, caller_esi, caller_edi, {entry[0], entry[1], entry[2]}};
    es2_weapon_registers *r = &s.r;
    /* 4327b2: the view code's panel-less rendering. */
    if(R8(VA(0x49b104u))) return (uint64_t)entry[1] << 32 | entry[0];
    /* 4327c2: the scroll zones once. */
    if(!R8(H + 0x23eu)) {
        pf_one(&s, 0x433770u, 0x4327d1u, H);
        W8(H + 0x23eu, 1u);
    }
    /* 4327d9: the startup sequence until it reports done. */
    if(!R8(H + 0x245u)) pf_one(&s, 0x432924u, 0x4327e8u, H);
    pf_one(&s, 0x43408cu, 0x4327efu, H);
    pf_one(&s, 0x434194u, 0x4327f6u, H);
    /* 4327f6: panel off: only the sub-object virtual +8 of [H+1F9h]. */
    if(R8(H + 0x20fu)) {
        pf_virtual(&s, R32(H + 0x1f9u), 8u, 0x43280cu, &r->eax, &r->edx);
        return (uint64_t)r->edx << 32 | r->eax;
    }
    /* 432812: the port's virtual +14h, the context push. */
    pf_virtual(&s, R32(H + 0x207u), 0x14u, 0x43281eu, &r->ecx, &r->eax);
    pf_call(&s, 0x4311e0u, 0x432824u, NULL, 0);
    /* 432824: the ten panel elements, null skipped. */
    s.edi = 0;
    for(uint32_t i = 0; i < 10u; ++i) {
        s.esi = H + 0x70u + 4u * i;
        s.edi = i;
        r->eax = R32(s.esi);
        if(r->eax) pf_virtual(&s, r->eax, 8u, 0x432835u, &r->eax, &r->edx);
    }
    s.esi = H + 0x98u;
    s.edi = 10u;
    /* 43283f: the six sub-objects, each read just before its call. */
    pf_virtual(&s, R32(H + 0x1e1u), 8u, 0x43284bu, &r->ecx, &r->eax);
    pf_virtual(&s, R32(H + 0x1e5u), 8u, 0x432858u, &r->edx, &r->ecx);
    pf_virtual(&s, R32(H + 0x1e9u), 8u, 0x432865u, &r->eax, &r->edx);
    pf_virtual(&s, R32(H + 0x1f1u), 8u, 0x432872u, &r->ecx, &r->eax);
    pf_virtual(&s, R32(H + 0x1edu), 8u, 0x43287fu, &r->edx, &r->ecx);
    pf_virtual(&s, R32(H + 0x1f9u), 8u, 0x43288cu, &r->eax, &r->edx);
    pf_call(&s, 0x431210u, 0x432892u, NULL, 0);
    pf_one(&s, 0x433c54u, 0x432898u, H);
    /* 432899: the target check; the target is read once. */
    const uint32_t T = R32(H + 0x210u);
    r->eax = T;
    if(T) {
        const uint32_t a[2] = {H, T};
        pf_call(&s, 0x433174u, 0x4328aau, a, 2);
        if(!(uint8_t)r->eax) {
            r->edx = 0;
            W32(H + 0x210u, 0u);
        }
    }
    /* 4328b9: button releases of the panel object [H], the nav marker. */
    r->ecx = R32(H);
    pf_one(&s, 0x4530fcu, 0x4328c1u, r->ecx);
    pf_call(&s, 0x4349acu, 0x4328c7u, NULL, 0);
    return (uint64_t)r->edx << 32 | r->eax;
}
