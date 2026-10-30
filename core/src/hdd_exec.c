#include "es2/core/hdd_exec.h"
/* The HDD button executor (docs/reverse-engineering/cockpit.md, section "HDD
 * button executor"). Every guest read and write is done in the original's
 * order, the locals included (they live at f); the registers EAX/EDX/ECX are
 * carried through as the original leaves them, so every game call sees the
 * original's entry registers and the function returns the original's EDX:EAX.
 * */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define VA(x) (g->image+(uint32_t)(x)-0x400000u)
static uint64_t he_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo16(uint32_t r,uint32_t v) { return (r&0xffff0000u)|(v&0xffffu); }
static uint32_t lo8(uint32_t r,uint32_t v) { return (r&0xffffff00u)|(v&0xffu); }
static uint32_t sx8(uint8_t v) { return (uint32_t)(int32_t)(int8_t)v; }
static uint32_t sx16(uint16_t v) { return (uint32_t)(int32_t)(int16_t)v; }

/* The function's state: the original's EBX/ESI/EDI and EAX/EDX/ECX. */
typedef struct he_state {
    const es2_guest_memory *m;
    const es2_weapon_graph *g;
    es2_mfd_frame *frame;
    uint32_t ebx, esi, edi;
    es2_weapon_registers r;
} he_state;
/* One direct game call to `target` returning to `ret` (original VAs). */
static void he_call(he_state *s,uint32_t target,uint32_t ret,const uint32_t *a,unsigned n) {
    const es2_weapon_graph *g=s->g;
    if(s->frame) {
        s->frame->call.ebx=s->ebx;s->frame->call.esi=s->esi;s->frame->call.edi=s->edi;
        s->frame->call.ret=VA(ret);
    }
    if(g->observe)g->observe(g->observer_context,target,0);
    g->call(g->context,VA(target),a,n,&s->r);
}
/* A one-slot call followed by POP ECX: the slot as the callee left it. */
static void he_call1(he_state *s,uint32_t target,uint32_t ret,uint32_t a0) {
    const es2_guest_memory *m=s->m;
    he_call(s,target,ret,&a0,1);
    s->r.ecx=s->frame&&s->frame->top?R32(s->frame->top-4u):a0;
}
/* The page marks after a case's call: byte H+o = 1, byte H+B0h = 2. */
static void he_mark(he_state *s,uint32_t o) {
    const es2_guest_memory *m=s->m;
    W8(s->ebx+o,1u);
    W8(s->ebx+0xb0u,2u);
}

/* The lamp block 44a381 after 44ba08/44b9b0. */
static void he_lamp(he_state *s,uint32_t f) {
    const es2_guest_memory *m=s->m;const es2_weapon_graph *g=s->g;
    const uint32_t h=s->ebx;
    s->r.eax=0;
    W32(f-4u,0);                                            /* 44a383 */
    s->edi=R32(h+0x524u);                                   /* 44a388 */
    if(s->edi) {
        s->r.edx=s->edi+0x92u;                              /* 44a392 */
        if(R8(s->r.edx+0x11u))s->r.eax=lo8(s->r.eax,1u);    /* 44a39a; 44a3a0 mov al,1 */
    }
    const uint8_t al=(uint8_t)s->r.eax;
    if(R8(h+0x51fu)&&!al) {                                 /* 44a3a2; 44a3ab */
        W8(h+0x51fu,0);                                     /* 44a3af */
        s->r.ecx=sx16(R16(VA(0x4d3b96u)));                  /* 44a3b6 */
        W32(f-4u,s->r.ecx);
    } else if(!R8(h+0x51fu)&&al) {                          /* 44a3c2 reread; 44a3cb */
        W8(h+0x51fu,1u);                                    /* 44a3cf */
        s->r.eax=sx16(R16(VA(0x4d3b9au)));                  /* 44a3d6 */
        W32(f-4u,s->r.eax);
    }
    if(R32(f-4u)) {                                         /* 44a3e0 */
        s->r.edx=f-8u;                                      /* 44a3e6 lea edx,[ebp-8] */
        W32(s->esi+0x22cu,s->r.edx);                        /* 44a3e9 the escaping frame pointer */
        s->esi=h+0xf1u;                                     /* 44a3ef */
        s->r.eax=s->esi;
        he_call1(s,0x486704u,0x44a3fdu,s->esi);             /* 44a3f8 clip(H+F1h) */
        s->r.eax=s->esi;                                    /* 44a3fe */
        s->r.edx=R32(s->r.eax);W32(f-0x20u,s->r.edx);       /* 44a400 */
        s->r.ecx=R32(s->r.eax+4u);                          /* 44a405 */
        s->r.eax+=8u;
        W32(f-0x1cu,s->r.ecx);                              /* 44a40b */
        s->r.edx=R32(s->r.eax);W32(f-0x18u,s->r.edx);       /* 44a40e */
        s->r.ecx=R32(s->r.eax+4u);                          /* 44a413 */
        s->r.eax=f-0x20u;
        W32(f-0x14u,s->r.ecx);                              /* 44a419 */
        W32(f-0x18u,R32(f-0x18u)+1u);                       /* 44a41c inc */
        const uint32_t a[3]={1u,f-0x20u,0};
        he_call(s,0x43122cu,0x44a429u,a,3);                 /* 44a424 draw(1, f-20h, 0) */
    }
    he_mark(s,0x110u);                                      /* 44a42c */
}

/* Cases 10..12 from 44a4f0. */
static void he_wingman(he_state *s) {
    const es2_guest_memory *m=s->m;const es2_weapon_graph *g=s->g;
    const uint32_t h=s->ebx;
    s->r.eax=sx8(R8(h+0x528u))-10u;                         /* 44a4f0; 44a4f7 */
    s->r.edx=R32(VA(0x4d03c8u)+4u*s->r.eax);                /* 44a4fa */
    if(s->r.eax==R32(h+0x517u))return;                      /* 44a501 */
    if(!s->r.edx)return;                                    /* 44a50d */
    s->esi=s->r.edx+0x92u;                                  /* 44a515 */
    if(R8(s->esi+7u))return;                                /* 44a51b */
    const uint32_t a[2]={h,s->r.eax};
    he_call(s,0x44a748u,0x44a52cu,a,2);                     /* 44a527 select(H, i) */
    s->r.eax=R32(h+0x101u);                                 /* 44a52f */
    if(!R8(s->r.eax+0x103u))return;                         /* 44a535 */
    he_call1(s,0x433e3cu,0x44a549u,2u);                     /* 44a544 */
}

/* Case 13 from 44a554 (byte [H+109h] checked by the caller). */
static void he_order(he_state *s) {
    const es2_guest_memory *m=s->m;const es2_weapon_graph *g=s->g;
    const uint32_t h=s->ebx;
    s->esi=R32(h+0x101u);                                   /* 44a561 */
    if(!R8(s->esi+0x104u)) {                                /* 44a567 */
        const uint32_t a[3]={s->esi,0,1u};
        he_call(s,0x44dc6cu,0x44a5d7u,a,3);                 /* 44a5d2 */
        return;
    }
    {
        const uint32_t a0=s->esi;
        he_call(s,0x44db9cu,0x44a576u,&a0,1);               /* 44a571 */
        const int none=R32(h+0x517u)==0xffffffffu;          /* 44a576 cmp before the pop */
        s->r.ecx=s->frame&&s->frame->top?R32(s->frame->top-4u):a0;  /* 44a57d */
        if(none)return;
    }
    s->edi=VA(0x4d03d4u);                                   /* 44a580 */
    s->r.eax=lo16(s->r.eax,R16(s->esi+0x5du));              /* 44a585 */
    W16(s->edi,s->r.eax);                                   /* 44a589 */
    s->r.edx=R32(h+0x101u);                                 /* 44a58d */
    {
        const uint32_t a[2]={s->r.edx,s->edi};
        he_call(s,0x44db4cu,0x44a599u,a,2);                 /* 44a594 */
    }
    s->r.ecx=lo16(s->r.ecx,R16(h+0x517u));                  /* 44a59c mov cx */
    s->r.eax=R32(h+0x14u);                                  /* 44a5a5 */
    {
        const uint32_t a[3]={s->r.eax,s->edi,s->r.ecx};
        he_call(s,0x431610u,0x44a5aeu,a,3);                 /* 44a5a9 order(cockpit, record, ECX) */
    }
    he_call1(s,0x433e3cu,0x44a5b8u,0);                      /* 44a5b3 */
    he_call1(s,0x44dc10u,0x44a5bfu,s->esi);                 /* 44a5ba */
    const uint32_t a[2]={h,0xffffffffu};
    he_call(s,0x44a748u,0x44a5c8u,a,2);                     /* 44a5c3 */
}

/* Original: DBSIM.EXE 0x44a1a0 */
uint64_t es2_hdd_execute_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_mfd_frame *frame,
    uint32_t f,uint32_t caller_esi,uint32_t caller_edi,uint32_t hdd,const uint32_t entry[3]) {
    he_state s={m,g,frame,hdd,caller_esi,caller_edi,{entry[0],entry[1],entry[2]}};
    const uint32_t h=hdd;
    s.r.eax=sx8(R8(h+0x528u))+1u;                           /* 44a1ac; 44a1b3 inc */
    if(!s.r.eax)return he_pair(s.r.eax,s.r.edx);            /* 44a1b4 */
    if(R8(h+0x76u)) {                                       /* 44a1ba */
        s.r.edx=sx8(R8(h+0x528u))-1u;                       /* 44a1c0; 44a1c7 dec */
        if((int32_t)s.r.edx>0)return he_pair(s.r.eax,s.r.edx);  /* 44a1c8 jg */
    }
    s.esi=R32(VA(0x6c5f70u));                               /* 44a1ce the graphics context */
    s.r.eax=0;
    W32(f-8u,0);                                            /* 44a1d6 */
    W32(f-0x10u,4u);                                        /* 44a1d9 */
    s.r.edx=sx8(R8(h+0x528u));                              /* 44a1e0 */
    if(s.r.edx<=14u) {                                      /* 44a1e7 cmp 0Eh; ja */
        const uint32_t c=s.r.edx;
        uint32_t v;
        switch(c) {                                         /* 44a1f0 jmp [4*edx+44a1f7] */
        case 0: case 1: {                                   /* 44a233 / 44a243 */
            const uint32_t a[2]={h,c};
            he_call(&s,0x44a60cu,c?0x44a24bu:0x44a23bu,a,2);
            break;
        }
        case 2: case 3:                                     /* 44a253 / 44a2aa */
            v=R32(h+0x109u);
            s.r.ecx=v-1u;
            if(v==0) {
                s.r.eax=R32(h+0x101u);                      /* 44a265 / 44a2bc */
                he_call1(&s,c==2?0x44d0c0u:0x44d124u,c==2?0x44a271u:0x44a2c8u,s.r.eax);
                he_mark(&s,0x10fu);
            } else if(v==1) {
                s.r.edx=R32(h+0x105u);                      /* 44a285 / 44a2dc */
                he_call1(&s,c==2?0x450bf4u:0x450c18u,c==2?0x44a291u:0x44a2e8u,s.r.edx);
                he_mark(&s,0x10eu);
            }
            break;
        case 4:                                             /* 44a301 */
            v=R32(h+0x109u);
            s.r.ecx=v-1u;
            if(v==0) {
                s.r.eax=R32(h+0x101u);                      /* 44a313 */
                he_call1(&s,0x44cff8u,0x44a31fu,s.r.eax);
                he_mark(&s,0x10fu);
            } else if(v==1) {
                he_call1(&s,0x44ba08u,0x44a339u,h);         /* 44a333 */
                W32(h+0x524u,s.r.eax);                      /* 44a33a */
                he_lamp(&s,f);
            }
            break;
        case 5:                                             /* 44a342 */
            v=R32(h+0x109u);
            s.r.edx=v-1u;
            if(v==0) {
                s.r.ecx=R32(h+0x101u);                      /* 44a354 */
                he_call1(&s,0x44d05cu,0x44a360u,s.r.ecx);
                he_mark(&s,0x10fu);
            } else if(v==1) {
                he_call1(&s,0x44b9b0u,0x44a37au,h);         /* 44a374 */
                W32(h+0x524u,s.r.eax);                      /* 44a37b */
                he_lamp(&s,f);
            }
            break;
        case 6:                                             /* 44a444 */
            v=R32(h+0x109u);
            s.r.edx=v-1u;
            if(v==0) {
                s.r.ecx=R32(h+0x101u);                      /* 44a453 */
                he_call1(&s,0x44cf90u,0x44a45fu,s.r.ecx);
                he_mark(&s,0x10fu);
            }
            break;
        case 7:                                             /* 44a473 */
            v=R32(h+0x109u);
            s.r.eax=v-1u;
            if(v==0) {
                s.r.edx=R32(h+0x101u);                      /* 44a482 */
                he_call1(&s,0x44cfc4u,0x44a48eu,s.r.edx);
                he_mark(&s,0x10fu);
            }
            break;
        case 9:                                             /* 44a4a2 */
            v=R32(h+0x109u);
            s.r.ecx=v-1u;                                   /* dec ecx; jne */
            if(v==1) {
                s.r.eax=R32(h+0x105u);                      /* 44a4af */
                he_call1(&s,0x450bf4u,0x44a4bbu,s.r.eax);
                he_mark(&s,0x10eu);
            }
            break;
        case 10: case 11: case 12:                          /* 44a4d4 */
            v=R32(h+0x109u);
            s.r.edx=v-1u;
            if(v==1) {
                const uint32_t a[2]={h,0};
                he_call(&s,0x44a60cu,0x44a4edu,a,2);        /* 44a4e8 */
                he_wingman(&s);
            } else if(v==0) {
                he_wingman(&s);
            }
            break;
        case 13:                                            /* 44a554 */
            if(!R32(h+0x109u))he_order(&s);
            break;
        case 14: {                                          /* 44a5dc */
            s.r.edx=R32(h+0x101u);
            he_call1(&s,0x44dc10u,0x44a5e8u,s.r.edx);
            he_call1(&s,0x433e3cu,0x44a5f0u,0);
            const uint32_t a[2]={h,0xffffffffu};
            he_call(&s,0x44a748u,0x44a5f9u,a,2);
            break;
        }
        default:                                            /* 8: the end */
            break;
        }
    }
    W8(h+0x528u,0xffu);                                     /* 44a5fc */
    return he_pair(s.r.eax,s.r.edx);
}
