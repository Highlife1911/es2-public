#include "es2/core/meteor.h"
#include "fixed.h"
/* Meteor constructor and shape instance (docs/reverse-engineering/
 * main_loop.md, section "meteor constructor 409b44 and shape instance 490b64").
 * r holds what the original's EAX/EDX/ECX hold at the next game call. The
 * constructor's frame lives in the scratch at the original EBP offsets (f = the
 * synthetic EBP). */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(g,x) ((g)->image+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static void put_byte(const es2_guest_memory *m,uint32_t a,uint8_t v) { *(uint8_t *)m->host(m->context,a)=v; }
/* Calls name the original VA (observer key); the bridge gets the address
 * in this image. */
static void mt_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}
/* inc/sub dword [[0x6cbee4]]: the pointer is read first. */
static uint32_t mt_count(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t delta) {
    const uint32_t counter=R32(VA(g,0x6cbee4u));
    W32(counter,R32(counter)+delta);
    return counter;
}

/* Original: DBSIM.EXE 0x490b64 */
uint64_t es2_shape_instance_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,uint32_t s,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={0,entry[1],entry[2]};
    W32(o,VA(g,0x4a69f0u));
    W32(o,VA(g,0x4a6a14u));
    (void)mt_count(m,g,1);
    W32(o,VA(g,0x4a62edu));
    W32(o+4u,s);
    const uint32_t n=R16(s+0x24u);
    if(n) { /* 490b93 */
        uint32_t a[3]={2u*n,R32(VA(g,0x6c5830u))};
        r.eax=a[1];r.edx=a[0];
        mt_call(g,0x47a288u,a,2,&r);
        const uint32_t block=r.eax;
        W32(o+8u,block);
        r.eax=R32(o+4u);                  /* 490bae: S reread */
        r.edx=2u*R16(r.eax+0x24u);        /* the count reread */
        r.ecx=R32(r.eax+0x1cu);
        a[0]=block;a[1]=r.ecx;a[2]=r.edx;
        mt_call(g,0x49680eu,a,3,&r);
    } else W32(o+8u,0);
    const uint32_t counter=mt_count(m,g,1);
    *ecx=r.ecx;
    return pair(o,counter);
}

/* Original: DBSIM.EXE 0x409b44. Frame: record f-24h, context word f-14h,
 * heap slot f-4, M = f-2Ch, V = f-34h, R = f-48h (z at f-40h), D = f-3Ch. */
uint64_t es2_meteor_create_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,uint32_t p,uint32_t group,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_METEOR_FRAME,M=f-0x2cu,V=f-0x34u,R=f-0x48u,D=f-0x3cu;
    const uint32_t state=VA(g,0x4d2599u);
    uint32_t a[3];
    es2_weapon_registers r={VA(g,0x498170u),entry[1],entry[2]};
    mt_call(g,0x49630cu,NULL,0,&r);
    r.edx=R32(VA(g,0x4a96b4u));r.ecx=R32(r.edx);
    a[0]=o;a[1]=r.ecx;mt_call(g,0x402188u,a,2,&r);
    W32(o,VA(g,0x4982b8u));
    r.eax=o+0x45u;a[0]=state;mt_call(g,0x492ee0u,a,1,&r);
    r.ecx=state;
    const uint32_t draw=r.eax;
    r.edx=M;a[0]=draw;a[1]=M;mt_call(g,0x47eb30u,a,2,&r);
    a[0]=state;a[1]=0x61a8u;mt_call(g,0x492f24u,a,2,&r);
    const int32_t s=0x11170+fx_s16(r.eax);
    W32(o+0x55u,group);
    W32(V,0);W32(V+4u,(uint32_t)-s);
    r.eax=M;r.edx=V;r.ecx=R;a[0]=V;a[1]=M;a[2]=R;mt_call(g,0x47ffe8u,a,3,&r);
    const int32_t q=s/2000; /* s in 37232..102767: q in 18..51 */
    const uint32_t z=50u*fx_sar((uint32_t)(q*q),1);
    W32(f-0x40u,z);
    W16(D,0);W16(D+2u,0x7d0u);W16(D+4u,0);
    r.eax=o+0x45u;r.edx=M;r.ecx=D;a[0]=D;a[1]=M;a[2]=o+0x45u;mt_call(g,0x47ff8cu,a,3,&r);
    W16(o+0x49u,(uint32_t)(fx_s32(0u-R32(f-0x40u))/q)); /* 409c1a: neg; cdq; idiv esi */
    W16(o+0x10u,draw);
    W16(o+0x32u,0);
    W32(o+0x4du,0);
    W32(o+0x51u,0);
    W32(f-0x40u,R32(f-0x40u)+R32(p+8u));
    W32(R,R32(R)+R32(p));
    W32(R+4u,R32(R+4u)+R32(p+4u));
    W32(o+0x2eu,R32(f-0x40u));
    r.ecx=R32(R);W32(o+0x26u,r.ecx);
    r.edx=R32(R+4u);W32(o+0x2au,r.edx);
    put_byte(m,o+0x4bu,0);
    put_byte(m,o+0x4cu,0);
    r.eax=o+0x26u;a[0]=0xcu;mt_call(g,0x47749cu,a,1,&r);
    W32(f-4u,r.eax);
    if(r.eax) { /* 409c7a */
        W16(f-0x14u,0x14u);
        r.ecx=R32(VA(g,0x4a96b4u));
        r.eax=R32(r.ecx+4u);
        r.edx=R32(f-4u);
        a[0]=r.edx;a[1]=r.eax;mt_call(g,0x490b64u,a,2,&r);
        (void)mt_count(m,g,(uint32_t)-2);
        W16(f-0x14u,8u);
    }
    W32(o+0x41u,R32(f-4u));
    const uint32_t counter=mt_count(m,g,1);
    *ecx=R32(f-0x24u);
    return pair(o,counter);
}
