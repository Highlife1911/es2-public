#include "es2/core/mouse_events.h"
#include "fixed.h"
/* The mouse event queue (docs/reverse-engineering/input.md, section "mouse
 * event queue 452d40 / 453078"). r follows the original's EAX/EDX/ECX wherever
 * a value reaches a callee's entry or the RET; globals the original rereads
 * (the active screen, the captured index, the read list and its count) are
 * reread at the same places. */
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define R16(a) m->read16(m->context,(a))
#define R32(a) m->read32(m->context,(a))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(uint32_t)(v))
#define VA(va) (image+(uint32_t)(va)-0x400000u)
/* Globals (hypothetical names, input.md). */
#define WRITE_LIST VA(0x4d1e04u)  /* pointer to the list being filled */
#define READ_LIST VA(0x4d1e08u)   /* pointer to the list being dispatched */
#define SCREEN VA(0x49dbd8u)      /* the active screen */
#define CURSOR VA(0x4d1de3u)      /* cursor relative to the screen (x, y) */
#define LAST VA(0x4d1dccu)        /* last event position (x, y) */
#define BUTTONS VA(0x49db6cu)     /* the current event's button word */
#define CAPTURED VA(0x49dbdcu)    /* captured gadget index, -1 none */
#define DRAG VA(0x49dbdeu)        /* drag mode */
#define FOLLOW VA(0x49dbdfu)      /* cursor follow in playback */
#define DOUBLE VA(0x49dbe0u)      /* double click */
#define LEFT VA(0x4d1dd4u)        /* left button state, right at +1 */
#define RELEASED VA(0x49dbe4u)    /* last left release time, right at +4 */
#define DOUBLE_TIME VA(0x4d1decu)
#define PLAYBACK VA(0x4d24d6u)
enum { K_FOLLOW=0x486e7cu, K_REDRAW=0x452ab8u, K_HIT=0x4523b0u, K_MOVE=0x45297cu, K_PRESS=0x4527c8u, K_RELEASE=0x452898u };
enum { SLOT_ACTIVATE=0x8u, SLOT_CODE=0x10u, SLOT_DRAG=0x18u };

typedef struct me_state {
    const es2_guest_memory *m;
    const es2_weapon_graph *g;
    es2_weapon_registers r;
} me_state;

static uint32_t lo16(uint32_t reg,uint32_t v) { return (reg&0xffff0000u)|(uint16_t)v; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static void me_call(me_state *c,uint32_t va,uint32_t a0,uint32_t a1,unsigned n) {
    const uint32_t a[2]={a0,a1},image=c->g->image;
    if(c->g->observe)c->g->observe(c->g->observer_context,va,0);
    c->g->call(c->g->context,VA(va),a,n,&c->r);
}
/* call [vtable+slot]: target read here, after the registers are set. */
static void me_virtual(me_state *c,uint32_t vtable,unsigned slot,uint32_t a0,uint32_t a1,unsigned n) {
    const es2_guest_memory *m=c->m;const uint32_t a[2]={a0,a1},target=R32(vtable+slot);
    if(c->g->observe)c->g->observe(c->g->observer_context,target,slot);
    c->g->call(c->g->context,target,a,n,&c->r);
}
/* The gadget pointer of the screen for the captured index. */
static uint32_t me_gadget(const es2_guest_memory *m,uint32_t screen,uint32_t index) {
    return R32(screen+4u*index+0x256u);
}

/* 452ddf/452ed3/452f9c: P <- E, then P -= S+22Bh/+22Fh. Registers as the
 * three copies leave them differ, so each caller finishes them. */
static void me_cursor(const es2_guest_memory *m,uint32_t image,uint32_t E,uint32_t *x,uint32_t *y) {
    *x=R32(E);W32(CURSOR,*x);*y=R32(E+4u);W32(CURSOR+4u,*y);
}

/* 452d8b..453055: one event E. */
static void me_event(me_state *c,uint32_t E) {
    const es2_guest_memory *m=c->m;const uint32_t image=c->g->image;uint32_t x,y;
    c->r.eax=lo16(c->r.eax,R16(E+8u));W16(BUTTONS,c->r.eax);
    c->r.edx=R32(LAST);
    int same=0;
    if(c->r.edx==R32(E)) { c->r.ecx=R32(LAST+4u);same=c->r.ecx==R32(E+4u); }
    if(!same) {
        /* 452db0: a new position. */
        c->r.eax=E;c->r.edx=R32(E);W32(LAST,c->r.edx);c->r.ecx=R32(E+4u);W32(LAST+4u,c->r.ecx);
        if(R16(PLAYBACK)&&R8(FOLLOW)) { c->r.eax=E;me_call(c,K_FOLLOW,E,0,1);c->r.ecx=E; }
        c->r.eax=E;me_cursor(m,image,E,&x,&y);c->r.edx=x;c->r.ecx=y;
        c->r.eax=R32(SCREEN);c->r.edx=c->r.eax+0x22bu;
        c->r.ecx=R32(c->r.edx);W32(CURSOR,R32(CURSOR)-c->r.ecx);
        c->r.edx=R32(c->r.edx+4u);W32(CURSOR+4u,R32(CURSOR+4u)-c->r.edx);
        if(R8(DRAG)) {
            if(R16(CAPTURED)!=0xffffu) {
                /* 452e13: the drag point E - (S+0, S+4) in the frame at +8. */
                const uint32_t point=c->g->scratch+8u,S=c->r.eax;
                c->r.edx=E;c->r.ecx=R32(E);W32(point,c->r.ecx);c->r.edx=R32(E+4u);W32(point+4u,c->r.edx);
                c->r.edx=S;c->r.ecx=R32(S);W32(point,R32(point)-c->r.ecx);c->r.ecx=point;
                c->r.edx=R32(S+4u);W32(point+4u,R32(point+4u)-c->r.edx);
                c->r.edx=sx(R16(CAPTURED));
                const uint32_t G=me_gadget(m,S,c->r.edx);
                c->r.eax=G;c->r.ecx=R32(G+0x17u);
                me_virtual(c,c->r.ecx,SLOT_DRAG,G,point,2);
                c->r.eax=sx(R16(CAPTURED));c->r.edx=R32(SCREEN);
                c->r.ecx=me_gadget(m,c->r.edx,c->r.eax);
                c->r.eax=R32(SCREEN);
                me_call(c,K_REDRAW,c->r.eax,c->r.ecx,2);
            }
        } else {
            /* 452e6e: the screen hit test, then the move handler. */
            c->r.edx=R32(SCREEN);
            me_call(c,K_HIT,c->r.edx,E,2);
            if((uint8_t)c->r.eax) { c->r.ecx=R32(SCREEN);me_call(c,K_MOVE,c->r.ecx,E,2); }
        }
    }
    /* 452e8c: buttons. */
    W8(DOUBLE,0);c->r.eax=0;
    int flag=0;
    c->r.edx=R32(E+0xau);const uint32_t T=c->r.edx;
    if((R8(E+8u)&1u)&&!R8(LEFT)) { W8(LEFT,1);c->r.eax=1; }
    if((R8(E+8u)&2u)&&!R8(LEFT+1u))W8(LEFT+1u,1);
    if((uint8_t)c->r.eax) {
        c->r.eax=E;me_cursor(m,image,E,&x,&y);c->r.edx=x;c->r.ecx=y;
        c->r.edx=R32(SCREEN);c->r.eax=c->r.edx+0x22bu;
        c->r.ecx=R32(c->r.eax);W32(CURSOR,R32(CURSOR)-c->r.ecx);
        c->r.eax=R32(c->r.eax+4u);W32(CURSOR+4u,R32(CURSOR+4u)-c->r.eax);
        me_call(c,K_PRESS,c->r.edx,E,2);
    }
    for(unsigned b=0;b<2;++b) {
        /* 452efd / 452f47: a release of the left (b = 0) or right button. */
        c->r.eax=lo16(c->r.eax,R16(E+8u));
        if(c->r.eax&(1u<<b))continue;
        if(R8(LEFT+b)!=1)continue;
        flag=1;W8(LEFT+b,0);
        c->r.edx=T-R32(RELEASED+4u*b);
        if(c->r.edx<R32(DOUBLE_TIME))W8(DOUBLE,1);
        c->r.eax=lo16(c->r.eax,c->r.eax|(1u<<b));
        c->r.ecx=T;W32(RELEASED+4u*b,c->r.ecx);W16(BUTTONS,c->r.eax);
    }
    if(flag) {
        /* 452f9c: the release. */
        c->r.eax=E;me_cursor(m,image,E,&x,&y);c->r.edx=x;c->r.ecx=y;
        c->r.eax=R32(SCREEN)+0x22bu;
        c->r.edx=R32(c->r.eax);W32(CURSOR,R32(CURSOR)-c->r.edx);
        c->r.ecx=R32(c->r.eax+4u);W32(CURSOR+4u,R32(CURSOR+4u)-c->r.ecx);
        if(R8(DRAG)) {
            if(R16(CAPTURED)!=0xffffu) {
                c->r.eax=sx(R16(CAPTURED));c->r.edx=R32(SCREEN);c->r.ecx=me_gadget(m,c->r.edx,c->r.eax);
                W16(c->r.ecx+0x1bu,0);
                c->r.eax=sx(R16(CAPTURED));c->r.edx=R32(SCREEN);c->r.ecx=me_gadget(m,c->r.edx,c->r.eax);
                c->r.eax=R32(SCREEN);
                me_call(c,K_REDRAW,c->r.eax,c->r.ecx,2);
                W8(DRAG,0);
                c->r.edx=sx(R16(CAPTURED));c->r.ecx=R32(SCREEN);
                const uint32_t G=me_gadget(m,c->r.ecx,c->r.edx);
                c->r.eax=R32(G+0x17u);
                me_virtual(c,c->r.eax,SLOT_CODE,G,0,1);c->r.ecx=G;
                c->r.edx=sx(c->r.eax);
                c->r.ecx=R32(G+0x17u);
                me_virtual(c,c->r.ecx,SLOT_ACTIVATE,G,c->r.edx,2);
                W16(CAPTURED,0xffffu);
            }
        } else {
            c->r.eax=R32(SCREEN);
            me_call(c,K_RELEASE,c->r.eax,E,2);
        }
    }
    W16(BUTTONS,0);
}

/* Original: DBSIM.EXE 0x452d40 */
uint64_t es2_mouse_dispatch_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t *ecx,
    const uint32_t entry[3]) {
    const uint32_t image=g->image;
    me_state c={m,g,{entry[0],entry[1],entry[2]}};
    /* 452d51: swap the lists and empty the new write list. */
    c.r.eax=R32(WRITE_LIST);const uint32_t old=c.r.eax;
    c.r.edx=R32(READ_LIST);W32(WRITE_LIST,c.r.edx);
    c.r.ecx=old;W32(READ_LIST,c.r.ecx);
    c.r.eax=R32(WRITE_LIST);c.r.edx=0;W32(c.r.eax,0);
    c.r.ecx=R32(READ_LIST);uint32_t E=R32(c.r.ecx+4u);
    c.r.eax=0;
    for(uint32_t i=0;;++i,E+=0xeu) {
        c.r.edx=R32(READ_LIST);c.r.ecx=R32(c.r.edx);
        if(!(fx_s32(c.r.ecx)>fx_s32(i)))break;
        me_event(&c,E);
    }
    c.r.eax=R32(READ_LIST);
    *ecx=c.r.ecx;
    return (uint64_t)c.r.edx<<32|c.r.eax;
}

/* Original: DBSIM.EXE 0x453078 */
uint64_t es2_mouse_queue_registers(const es2_guest_memory *m,uint32_t image,uint32_t event,uint32_t value,
    uint32_t *ecx,const uint32_t entry[3]) {
    const uint32_t list=R32(WRITE_LIST);
    if(R32(list)==0x63u) { *ecx=entry[2];return (uint64_t)entry[1]<<32; }
    const uint32_t n=R32(list);
    W32(list,R32(list)+1u);
    const uint32_t slot=14u*n+R32(list+4u);
    uint32_t c=lo16(slot,R16(event+8u));W16(slot+8u,c);
    c=R32(event);W32(slot,c);
    W32(slot+4u,R32(event+4u));
    W32(slot+0xau,value);
    *ecx=c;
    return (uint64_t)value<<32|(slot&0xffffff00u)|1u;
}
