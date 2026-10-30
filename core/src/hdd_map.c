#include "es2/core/hdd_map.h"
#include "fixed.h"
#include <stddef.h>
/* The HDD map frame (docs/reverse-engineering/cockpit.md, section "HDD map
 * frame"). Every guest read and write is done in the original's order and
 * repeated where the original repeats it (the callees may change M, the records
 * and the globals); r holds what the original's EAX/EDX/ECX hold at the next
 * game call. The locals live at the original's offsets below f. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define VA(x) (g->image+(uint32_t)(x)-0x400000u)

typedef struct hm_state {
    const es2_guest_memory *m;
    const es2_weapon_graph *g;
    es2_map_frame *frame;
    uint32_t ebx, esi, edi;
    es2_weapon_registers r;
} hm_state;

static uint64_t hm_pair(uint32_t eax, uint32_t edx) { return (uint64_t)edx << 32 | eax; }
/* One direct game call returning to `ret` (original VA). */
static void hm_call(hm_state *s, uint32_t va, uint32_t ret, const uint32_t *a, unsigned n) {
    const es2_weapon_graph *g = s->g;
    if(s->frame) {
        s->frame->call.ebx = s->ebx; s->frame->call.esi = s->esi; s->frame->call.edi = s->edi;
        s->frame->call.ret = VA(ret);
    }
    if(g->observe) g->observe(g->observer_context, va, 0);
    g->call(g->context, VA(va), a, n, &s->r);
}
/* POP ECX after a one-slot call: the slot as the callee left it at the
 * original coordinate, else the pushed argument. */
static void hm_pop(hm_state *s, uint32_t arg) {
    const es2_guest_memory *m = s->m;
    s->r.ecx = s->frame && s->frame->top ? R32(s->frame->top - 4u) : arg;
}
static void hm_one(hm_state *s, uint32_t va, uint32_t ret, uint32_t arg) {
    const uint32_t a[1] = {arg};
    hm_call(s, va, ret, a, 1);
    hm_pop(s, arg);
}
/* One mode button k (44cb3f..44cb9a for the previous mode, 44cba5..44cc07
 * for the current one): ESI = k-10, the button B = [M+0x3d+4(k-10)] takes
 * the colour pair by the enable byte M+0x131+(k-10), the label 0x438aac(B,
 * [0x4d12a8+4k'], byte [[0x4d12f0+4k']]) with k' = the mode field reread,
 * then 0x44dd74(M, k-10, 0, flag). `current` selects the second variant:
 * colours 0x49b0b8/0x49b0b4 instead of 0x49b0b0/0x49b0b4, the flag byte
 * [f-0xd] (0 enabled, 1 disabled) in AL of the last argument instead of 1. */
static void hm_button(hm_state *s, uint32_t M, uint32_t f, uint32_t field, int current, uint32_t ret_label,
    uint32_t ret_draw) {
    const es2_guest_memory *m = s->m;
    const es2_weapon_graph *g = s->g;
    es2_weapon_registers *r = &s->r;
    s->esi = r->eax - 0xau;                                  /* mov esi,eax; add esi,-0ah */
    const uint8_t on = R8(M + s->esi + 0x131u);              /* cmp byte [ebx+esi+131h],0 */
    const uint32_t B = R32(M + 4u * s->esi + 0x3du);         /* mov eax,[ebx+4*esi+3dh] */
    r->eax = B;
    if(on) {
        r->edx = R32(VA(current ? 0x49b0b8u : 0x49b0b0u));
        r->ecx = R32(VA(0x49b0b4u));
        if(current) W8(f - 0xdu, 0u);
    } else {
        r->ecx = R32(VA(0x49b0acu));
        if(current) W8(f - 0xdu, 1u);
        r->edx = r->ecx;
    }
    W32(B, r->edx);                                          /* mov [eax],edx */
    W32(B + 0x21u, r->ecx);                                  /* mov [eax+21h],ecx */
    const uint32_t k = R32(M + field);                       /* mov edx,[ebx+81h/5dh] */
    r->edx = k;
    r->ecx = R32(VA(0x4d12f0u) + 4u * k);                    /* mov ecx,[4*edx+4d12f0h] */
    r->ecx = R8(r->ecx);                                     /* movzx ecx,byte [ecx] */
    const uint32_t c = r->ecx;
    r->edx = R32(VA(0x4d12a8u) + 4u * k);                    /* mov edx,[4*edx+4d12a8h] */
    const uint32_t label[3] = {B, r->edx, c};
    hm_call(s, 0x438aacu, ret_label, label, 3);              /* add esp,0ch: no POP */
    uint32_t flag = 1u;
    if(current) {
        r->eax = (r->eax & 0xffffff00u) | R8(f - 0xdu);      /* mov al,[ebp-0dh]; push eax */
        flag = r->eax;
    }
    const uint32_t draw[4] = {M, s->esi, 0u, flag};
    hm_call(s, 0x44dd74u, ret_draw, draw, 4);                /* add esp,10h */
}

/* Original: DBSIM.EXE 0x44c988 */
uint64_t es2_map_frame_registers(const es2_guest_memory *m, const es2_weapon_graph *g, es2_map_frame *frame,
    uint32_t f, uint32_t caller_esi, uint32_t caller_edi, uint32_t map, const uint32_t entry[3]) {
    const uint32_t M = map;
    hm_state s = {m, g, frame, M, caller_esi, caller_edi, {entry[0], entry[1], entry[2]}};
    es2_weapon_registers *r = &s.r;
    /* 44c993: a pending gadget event. */
    if(R8(M + 0x14du)) hm_one(&s, 0x44d450u, 0x44c9a2u, M);
    /* 44c9a3: the clock; bit 4 drives the blink counter. */
    hm_call(&s, 0x467828u, 0x44c9a8u, NULL, 0);
    if(r->eax & 0x10u) {
        if(!R8(VA(0x49d7e6u))) {
            W16(VA(0x49d6b8u), R16(VA(0x49d6b8u)) + 1u);                 /* 44c9b5 inc word */
            uint32_t e = (uint32_t)fx_s16(R16(VA(0x49d6b8u))) & 0x80000003u;   /* 44c9bc movsx, and */
            if(e & 0x80000000u) e = ((e - 1u) | 0xfffffffcu) + 1u;          /* 44c9c9 jns; dec, or -4, inc */
            r->edx = e;
            W16(VA(0x49d6b8u), e);                                          /* 44c9d0 */
            W8(VA(0x49d7e6u), 1u);                                          /* 44c9d7 */
        }
    } else {
        W8(VA(0x49d7e6u), 0u);                                              /* 44c9e0 */
    }
    /* 44c9e7: the 30-tick timer (unsigned compare). */
    if(r->eax > R32(M + 0x149u)) {
        r->eax += 0x1eu;
        W32(M + 0x149u, r->eax);
        hm_one(&s, 0x44d370u, 0x44c9feu, M);
    }
    /* 44c9ff: the locals; X is the graphics context read once. */
    r->ecx = 0;
    const uint32_t X = R32(VA(0x6c5f70u));
    r->eax = X;
    W32(f - 0xcu, 0u);
    W32(f - 4u, X);
    /* 44ca0c: a pending click: an order target or point, else a wingman. */
    if(R8(M + 0x11cu)) {
        if(R8(M + 0x103u) && R32(M + 0x105u) != 0xffffffffu) hm_one(&s, 0x44d6e0u, 0x44ca2du, M);
        else hm_one(&s, 0x44d82cu, 0x44ca36u, M);
    }
    /* 44ca37: the selected wingman's order record; a dead target clears it. */
    r->eax = R32(M + 0x105u);
    if(r->eax != 0xffffffffu) {
        const uint32_t R = 42u * r->eax + M + 0x85u;   /* lea edx,[eax+4*eax]; lea edx,[eax+4*edx]; add edx,edx */
        r->edx = R;
        r->eax = R;
        if(R32(R + 0x22u) == 1u) {
            int clear = 0;
            if(R8(R + 0x20u)) {
                r->edx = R32(R + 0x18u) + 0x92u;
                clear = R8(r->edx + 7u) != 0;
            }
            if(!clear && R8(R + 0x21u)) {
                r->edx = R32(R + 0x1cu) + 0x92u;
                clear = R8(r->edx + 7u) != 0;
            }
            if(clear) {
                W8(R + 0x20u, 0u);
                W8(R + 0x21u, 0u);
            }
        }
    }
    /* 44ca8c: the rest only while M+0xc is 1 or 2 (dec eax; sub eax,2; jb). */
    const uint32_t state = R32(M + 0xcu);
    r->eax = state - 3u;
    if(state - 1u >= 2u) return hm_pair(r->eax, r->edx);
    /* 44ca9a: the wingman-button blink. */
    if(R8(VA(0x49d6acu))) {
        r->eax = R32(M + 0x105u);
        if(r->eax != 0xffffffffu) {
            r->edx = R32(M + 0x31u);
            r->ecx = R32(r->edx + 4u * r->eax);
            const uint32_t button = r->ecx;
            const uint32_t a[1] = {button};
            hm_call(&s, 0x44f644u, 0x44cabau, a, 1);
            r->eax = R32(M + 0x31u);
            r->edx = R32(M + 0x105u);
            hm_pop(&s, button);
            r->ecx = R32(r->eax + 4u * r->edx);
            W8(r->ecx + 0x1fu, 0u);
        }
        W8(VA(0x49d6acu), 0u);
    }
    if(R8(M + 0x139u)) {
        /* 44cadb: the mode-button panel and its rectangle. */
        hm_one(&s, 0x44de14u, 0x44cae1u, M);
        r->eax = R32(M + 0x3du) + 4u;
        r->edx = R32(r->eax); W32(f - 0x20u, r->edx);
        r->ecx = R32(r->eax + 4u); W32(f - 0x1cu, r->ecx);
        r->eax = R32(M + 0x59u) + 0xcu;
        r->edx = R32(r->eax); W32(f - 0x18u, r->edx);
        r->ecx = R32(r->eax + 4u);
        r->eax = f - 0x20u;
        W32(f - 0x14u, r->ecx);
        const uint32_t a[3] = {1u, f - 0x20u, 0u};
        hm_call(&s, 0x43122cu, 0x44cb11u, a, 3);
    } else {
        /* 44cb19: a mode change redraws both buttons. */
        r->edx = R32(M + 0x81u);
        if(r->edx != R32(M + 0x5du)) {
            r->ecx = f - 0xcu;
            r->eax = R32(f - 4u);
            W32(r->eax + 0x22cu, r->ecx);                       /* 44cb2e */
            r->eax = R32(M + 0x81u);
            if(r->eax != 0xffffffffu) hm_button(&s, M, f, 0x81u, 0, 0x44cb8cu, 0x44cb9au);
            r->eax = R32(M + 0x5du);                            /* 44cb9d */
            if(r->eax != 0xffffffffu) hm_button(&s, M, f, 0x5du, 1, 0x44cbf7u, 0x44cc07u);
            r->eax = R32(M + 0x5du);                            /* 44cc0a */
            W32(M + 0x81u, r->eax);
        }
    }
    /* 44cc13: the label redraw. */
    if(R8(M + 0x144u)) hm_one(&s, 0x44dd14u, 0x44cc22u, M);
    /* 44cc23: the map redraw. */
    if(R8(VA(0x49d6aeu))) {
        r->edx = f - 0xcu;
        r->ecx = R32(f - 4u);
        W32(r->ecx + 0x22cu, r->edx);                           /* 44cc32 */
        r->eax = (uint32_t)fx_s16(R16(VA(0x4d3ba2u)));
        W32(f - 8u, r->eax);
        r->eax = M + 0x71u;
        hm_one(&s, 0x486704u, 0x44cc4bu, M + 0x71u);
        hm_one(&s, 0x44e334u, 0x44cc52u, M);
        r->edx = 0;
        W32(M + 0xcu, 0u);
        W8(VA(0x49d6aeu), 0u);
    }
    return hm_pair(r->eax, r->edx);
}
