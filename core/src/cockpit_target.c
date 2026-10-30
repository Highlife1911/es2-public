#include "es2/core/cockpit_target.h"
#include "fixed.h"
/* Cockpit target selection 433174, 433250, 43349C, 4333C8 and 4332DC
 * (docs/reverse-engineering/hud.md, "cockpit target cycling as own code"). The
 * register struct r follows EAX/EDX/ECX wherever a game call or the result sees
 * them. Memory is read again wherever the original reads it again, because the
 * bridged callees may change it. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
/* A DBSIM VA (globals, direct callees) inside the image at g->image. */
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)

static uint64_t ct_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit register write: the high word stays. */
static uint32_t ct_lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t ct_sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t ct_sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
static int ct_less(uint32_t a,uint32_t b) { return fx_s32(a)<fx_s32(b); }
/* A direct call names the original VA; a virtual call is observed by slot. */
static void ct_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,VA(va),0);
    g->call(g->context,VA(va),a,n,r);
}
static void ct_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* The view subject [4D2684h] while dword 49EF34h is set, else the player. */
static uint32_t ct_subject(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t cockpit) {
    return R32(VA(0x49ef34u))?R32(VA(0x4d2684u)):R32(cockpit+0x203u);
}
/* The HUD record update shared by 43349C, 4333C8 and 4332DC: 43DBB0 (the
 * record at HUD+B1h, computed here) gets the target, read once into
 * *target, at +6 and flag +24h; the caller then runs 43D9B4(HUD, record)
 * as game code with the pointer read again. */
static uint32_t ct_record(const es2_guest_memory *m,uint32_t cockpit,uint32_t *target) {
    const uint32_t record=R32(cockpit+0x1f5u)+0xb1u;
    *target=R32(cockpit+0x210u);W32(record+6u,*target);W8(record+0x24u,1);
    return record;
}

/* Original: DBSIM.EXE 0x433174 */
uint64_t es2_cockpit_valid_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t cockpit,uint32_t target,const uint32_t entry[3]) {
    const uint32_t H=cockpit,T=target,S=ct_subject(m,g,H);
    es2_weapon_registers r={S+0x26u,entry[1],entry[2]};
    uint32_t a[2]={S+0x26u,T+0x26u};ct_call(g,0x49288cu,a,2,&r);
    const uint32_t d=r.eax,near_limit=R32(VA(0x4d1c78u));
    /* 426AEC: 30000, doubled unless word 4A9E5Eh is 1. */
    const uint32_t far_limit=R16(VA(0x4a9e5eu))==1u?30000u:60000u;
    if(R8(T+0xa4u)||R8(T+0x99u))return ct_pair(0,T+0x92u);
    const uint32_t side=ct_sx8(R8(R32(T+0x45u)+0x12u)),own=ct_sx8(R8(R32(S+0x45u)+0x12u));
    if((uint16_t)side==(uint16_t)own)return ct_pair(0,own);
    if(R8(T+0x95u)&&ct_less(d,near_limit))return ct_pair(1,T+0x92u);
    const uint32_t index=ct_sx16(R16(T+0x4bu));
    if(!R8(S+0xc2u+index))return ct_pair(0,index);
    return ct_pair(ct_less(d,far_limit)?1u:0u,index);
}

/* Original: DBSIM.EXE 0x433250 */
uint64_t es2_cockpit_arc_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t cockpit,uint32_t target,const uint32_t entry[3]) {
    const uint32_t H=cockpit,T=target;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2],x;
    if(R32(VA(0x49ef34u))) {
        const uint32_t V=R32(VA(0x4d2684u));
        r.eax=V+0x26u;a[0]=T+0x26u;a[1]=V+0x26u;ct_call(g,0x492934u,a,2,&r);
        x=ct_lo(r.eax,r.eax-R16(R32(VA(0x4d2684u))+0x10u));
    } else {
        const uint32_t P=R32(H+0x203u);
        r.eax=P+0x26u;r.edx=T+0x26u;a[0]=T+0x26u;a[1]=P+0x26u;ct_call(g,0x492934u,a,2,&r);
        const uint32_t saved=ct_lo(r.eax,r.eax-R16(P+0x10u));
        /* The player's twist, virtual +3Ch, on the pointer read again. */
        const uint32_t player=R32(H+0x203u);
        r.ecx=player;r.eax=R32(player);a[0]=player;ct_virtual(g,R32(r.eax+0x3cu),0x3cu,a,1,&r);
        x=ct_lo(r.eax,r.eax+saved);
    }
    const int32_t bearing=fx_s16(x);
    const uint32_t inside=bearing>-9000&&bearing<9000;
    return ct_pair(inside,inside);
}

/* Inserts T at distance d into bucket e (count, actors, distances): the
 * first stored distance above d gives the place, at most four are kept. */
static void ct_insert(const es2_guest_memory *m,uint32_t e,uint32_t T,uint32_t d) {
    for(uint32_t c=0;c<4u;++c) {
        if(!ct_less(d,R32(R32(e+8u)+4u*c)))continue;
        uint32_t last=R32(e)-1u;
        if(fx_s32(last)>2)last=2;
        for(uint32_t j=last;fx_s32(c)<=fx_s32(j);--j) {
            uint32_t list=R32(e+4u),v=R32(list+4u*j);W32(list+4u*j+4u,v);
            list=R32(e+8u);v=R32(list+4u*j);W32(list+4u*j+4u,v);
        }
        uint32_t list=R32(e+4u);W32(list+4u*c,T);
        list=R32(e+8u);W32(list+4u*c,d);
        W32(e,last+2u);
        return;
    }
}

/* Original: DBSIM.EXE 0x43349c */
uint64_t es2_cockpit_next_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t cockpit,const uint32_t entry[3]) {
    const uint32_t H=cockpit;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    const uint32_t old_head=R32(R32(VA(0x4d040cu))),old_target=R32(H+0x210u);
    uint32_t cursor=R32(VA(0x4a9af8u));
    /* Four buckets: count 0, distances 7FFFFFFh; then the list. */
    uint32_t bucket=R32(VA(0x4d0410u));
    for(unsigned k=0;k<4u;++k,bucket+=12u) {
        W32(bucket,0);
        for(uint32_t j=0;j<4u;++j)W32(R32(bucket+8u)+4u*j,0x7ffffffu);
    }
    for(uint32_t j=0;j<4u;++j)W32(R32(VA(0x4d040cu))+4u*j,0);
    for(uint16_t i=0;;) {
        const uint16_t old=i++;uint32_t T=0,slot=0;
        if(fx_s16(old)<fx_s16(R16(VA(0x4a9afeu)))) { slot=cursor;cursor+=4u;T=R32(slot); }
        if(!T)break;
        r.eax=T;r.edx=H;r.ecx=slot;a[0]=H;a[1]=T;ct_call(g,0x433174u,a,2,&r);
        if(!(uint8_t)r.eax)continue;
        r.ecx=T;r.eax=H;a[0]=H;a[1]=T;ct_call(g,0x433250u,a,2,&r);
        if(!(uint8_t)r.eax)continue;
        const uint32_t P=R32(H+0x203u);
        r.eax=H;r.edx=P+0x26u;a[0]=T+0x26u;a[1]=P+0x26u;ct_call(g,0x492934u,a,2,&r);
        const uint32_t x=ct_lo(r.eax,r.eax-R16(P+0x10u));
        const uint32_t player=R32(H+0x203u);
        r.ecx=H;r.eax=player;r.edx=R32(player);a[0]=player;ct_virtual(g,R32(r.edx+0x3cu),0x3cu,a,1,&r);
        const uint32_t b=ct_lo(x,x+r.eax);
        r.ecx=b;r.eax=H;r.edx=R32(H+0x203u)+0x26u;a[0]=r.edx;a[1]=T+0x26u;ct_call(g,0x49288cu,a,2,&r);
        const uint32_t d=r.eax;
        /* |b| as a word; 8000h becomes 7FFFh in dist_gate's ECX. */
        uint32_t bearing;
        if((uint16_t)b==0x8000u)bearing=ct_lo(r.ecx,0x7fffu);
        else if(fx_s16(b)<0)bearing=ct_lo(b,0u-b);
        else bearing=b;
        r.ecx=bearing;r.eax=T;r.edx=R32(T);a[0]=T;ct_virtual(g,R32(r.edx+0x10u),0x10u,a,1,&r);
        r.ecx=T;a[0]=d;a[1]=ct_sx16(r.eax);ct_call(g,0x47d32cu,a,2,&r);
        const uint32_t ratio=(uint16_t)r.eax;
        r.eax=ratio;r.edx=ratio;a[0]=0;a[1]=ratio;ct_call(g,0x47e0b0u,a,2,&r);
        int32_t rest=fx_s16(bearing-r.eax);
        if(rest<0)rest=0;
        uint32_t k=(uint32_t)(rest>>10);
        if(k>3u)k=3u;
        ct_insert(m,12u*k+R32(VA(0x4d0410u)),T,d);
    }
    /* The new list: buckets in order, at most four actors, zero-filled. */
    uint32_t n=0;bucket=R32(VA(0x4d0410u));
    for(unsigned k=0;k<4u&&n<4u;++k,bucket+=12u)
        for(uint32_t j=0;ct_less(j,R32(bucket));++j) {
            const uint32_t list=R32(VA(0x4d040cu)),actor=R32(R32(bucket+4u)+4u*j);
            W32(list+4u*n,actor);
            if(++n==4u)break;
        }
    for(;n<4u;++n)W32(R32(VA(0x4d040cu))+4u*n,0);
    /* The choice: the list head, or the entry after the current target when
     * the head is the previous head or target. */
    uint32_t choice=R32(R32(VA(0x4d040cu)));
    if((choice==old_head||choice==old_target)&&R32(H+0x210u)) {
        uint32_t p=R32(VA(0x4d040cu));
        for(unsigned i=0;i<3u;++i,p+=4u)
            if(R32(H+0x210u)==R32(p)&&R32(p+4u)) { choice=R32(p+4u);break; }
    }
    if(choice==old_target)return ct_pair(R32(H+0x210u),choice);
    W32(H+0x210u,choice);
    uint32_t chosen;const uint32_t record=ct_record(m,H,&chosen),hud=R32(H+0x1f5u);
    r.eax=H;r.edx=hud;r.ecx=chosen;a[0]=hud;a[1]=record;ct_call(g,0x43d9b4u,a,2,&r);
    return ct_pair(R32(H+0x210u),r.edx);
}

/* Original: DBSIM.EXE 0x4333c8 */
uint64_t es2_cockpit_nearest_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t cockpit,const uint32_t entry[3]) {
    const uint32_t H=cockpit;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    uint32_t cursor=R32(VA(0x4a9af8u)),best=0,nearest=0x7fffffffu;
    r.eax=cursor;
    for(uint16_t i=0;;) {
        const uint16_t old=i++;uint32_t T=0;
        r.eax=ct_lo(r.eax,old);
        if(fx_s16(old)<fx_s16(R16(VA(0x4a9afeu)))) { r.edx=cursor;cursor+=4u;T=R32(r.edx); }
        if(!T)break;
        const uint32_t type=R16(T+0x1a8u);r.eax=ct_lo(r.eax,type);
        if(type!=0u&&type!=2u)continue;
        a[0]=H;a[1]=T;ct_call(g,0x433174u,a,2,&r);
        if(!(uint8_t)r.eax)continue;
        r.edx=T+0x26u;r.ecx=R32(H+0x203u)+0x26u;a[0]=r.ecx;a[1]=r.edx;ct_call(g,0x49288cu,a,2,&r);
        if(ct_less(r.eax,nearest)) { nearest=r.eax;best=T; }
    }
    if(best&&best!=R32(H+0x210u)) {
        W32(H+0x210u,best);
        uint32_t chosen;const uint32_t record=ct_record(m,H,&chosen),hud=R32(H+0x1f5u);
        r.eax=record;r.edx=chosen;r.ecx=hud;a[0]=hud;a[1]=record;ct_call(g,0x43d9b4u,a,2,&r);
    }
    return ct_pair(best,r.edx);
}

/* Original: DBSIM.EXE 0x4332dc */
uint64_t es2_cockpit_select_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t cockpit,uint32_t target,const uint32_t entry[3]) {
    const uint32_t H=cockpit,T=target;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    if(!T) { r.eax=0;W32(H+0x210u,0); }
    else {
        if(!R32(H+0x210u)) { a[0]=H;ct_call(g,0x43349cu,a,1,&r);r.ecx=H; }
        const uint32_t start=R32(H+0x210u);r.edx=start;
        for(;;) {
            const uint32_t current=R32(H+0x210u);
            if(T==current) {
                a[0]=H;a[1]=current;ct_call(g,0x433174u,a,2,&r);
                if((uint8_t)r.eax) {
                    a[0]=H;a[1]=T;ct_call(g,0x433250u,a,2,&r);
                    if((uint8_t)r.eax)break;
                }
            }
            /* One step of the cycle cursor: pointer H+214h, index word H+218h. */
            const uint32_t walk=H+0x214u,old=R16(walk+4u);
            r.eax=walk;r.edx=ct_lo(r.edx,old);W16(walk+4u,old+1u);
            uint32_t next=0;
            if(fx_s16(old)<fx_s16(R16(VA(0x4a9afeu)))) { r.ecx=R32(walk);W32(walk,r.ecx+4u);next=R32(r.ecx); }
            r.eax=next;W32(H+0x210u,next);
            if(!R32(H+0x210u)) { r.eax=walk;W16(walk+4u,0);r.edx=R32(VA(0x4a9af8u));W32(walk,r.edx); }
            r.ecx=R32(H+0x210u);
            if(r.ecx==start)break;
        }
    }
    if(T!=R32(H+0x210u))return ct_pair(0,r.edx);
    uint32_t chosen;const uint32_t record=ct_record(m,H,&chosen),hud=R32(H+0x1f5u);
    r.eax=record;r.edx=chosen;r.ecx=hud;a[0]=hud;a[1]=record;ct_call(g,0x43d9b4u,a,2,&r);
    return ct_pair((r.eax&0xffffff00u)|1u,r.edx);
}
