#include "es2/core/flyer_states.h"
#include "fixed.h"
/* FLYER AI state methods (docs/reverse-engineering/flyer.md, section "the FLYER
 * AI state methods"). r holds what the original's EAX/EDX/ECX hold at the next
 * game call; lo() is a 16-bit load or 16-bit arithmetic that keeps the
 * register's high word. Memory is read again wherever the original reads it
 * again after a call. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* A DBSIM VA (direct callees, state records) inside the image at g->image. */
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)

static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static int less(uint32_t a,uint32_t b) { return fx_s32(a)<fx_s32(b); }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes its resolved target. */
static void fs_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}
static void fs_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* A one-slot call followed by POP ECX. */
static void fs_call1(const es2_weapon_graph *g,uint32_t va,uint32_t arg,es2_weapon_registers *r) {
    const uint32_t a[1]={arg};fs_call(g,va,a,1,r);r->ecx=arg;
}

/* Original: DBSIM.EXE 0x422ca8 */
uint64_t es2_flyer_attacking_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.eax=A;r.edx=R32(A+0x1a4u);
    const uint32_t q=r.edx+0x92u;r.ecx=q;
    if(R8(q+0x12u))r.edx=1;
    else r.edx=R8(q+7u)?1u:0u;
    if((uint16_t)r.edx)return pair(lo(r.eax,1),r.edx);
    r.ecx=R32(A+0x45u);r.edx=R32(r.ecx+0xcu);
    fs_call1(g,A==R32(r.edx)?0x4226a0u:0x422598u,A,&r);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x422598. Frame at g->scratch: P = +0 (EBP-14h),
 * the zero dword +0Ch (EBP-8), the distance +10h (EBP-4). */
uint64_t es2_flyer_follow_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    const uint32_t P=g->scratch,zero=P+0xcu,dist=P+0x10u;
    uint32_t a[5];
    r.eax=R32(A+0x45u);r.edx=R32(r.eax+0xcu);
    const uint32_t L=R32(r.edx);
    r.eax=L+0x26u;
    r.edx=R32(r.eax);W32(P,r.edx);
    r.ecx=R32(r.eax+4u);W32(P+4u,r.ecx);
    r.eax=R32(r.eax+8u);W32(P+8u,r.eax);
    r.edx=lo(r.edx,R16(L+0x10u));r.ecx=P;
    a[0]=P;a[1]=r.edx;a[2]=0x2000;fs_call(g,0x4929fcu,a,3,&r);
    r.eax=P;const uint32_t vt=R32(A);r.edx=vt;
    a[0]=A;a[1]=P;fs_virtual(g,R32(vt+0x78u),0x78,a,2,&r);
    uint32_t edi=A+0x26u;r.eax=P;
    a[0]=P;a[1]=edi;fs_call(g,0x49288cu,a,2,&r);
    W32(dist,r.eax);
    r.edx=P;a[0]=P;a[1]=edi;fs_call(g,0x492934u,a,2,&r);
    r.eax=lo(r.eax,r.eax-R16(A+0x10u));
    edi=r.eax;
    r.eax=lo(r.eax,R16(A+0x10u));
    r.eax=lo(r.eax,r.eax-R16(L+0x10u));
    r.eax=lo(r.eax,r.eax+0x4000u);
    r.edx=(uint16_t)r.eax<0x8000u?1u:0u;
    if(r.edx) {
        r.ecx=lo(edi,edi+0x4000u);
        r.eax=(uint16_t)r.ecx<0x8000u?1u:0u;
        if(!r.eax&&less(R32(dist),10000)) {
            r.edx=lo(r.edx,0x8000u);r.edx=lo(r.edx,r.edx-edi);edi=r.edx;
        }
    }
    r.ecx=0;W32(zero,r.ecx);
    r.eax=R32(zero)+0x7530u;
    a[0]=A;a[1]=r.eax;fs_call(g,0x422108u,a,2,&r);
    a[0]=A;a[1]=edi;a[2]=r.eax;a[3]=L;fs_call(g,0x4222fcu,a,4,&r);
    if(less(15000,R32(dist))) { W16(A+0x21cu,0x100);return pair(r.eax,r.edx); }
    r.edx=lo(r.edx,R16(dist));
    a[0]=A;a[1]=L;a[2]=r.edx;a[3]=edi;a[4]=0x2000;fs_call(g,0x422260u,a,5,&r);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x422a50 */
uint64_t es2_flyer_formation_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.eax=R32(A+0x45u);r.edx=R32(r.eax+0xcu);r.eax=R32(r.edx);
    fs_call1(g,A==r.eax?0x4224c4u:0x422598u,A,&r);
    r.eax=A+0x92u;W8(r.eax+4u,0);
    return pair(r.eax,r.edx);
}

/* 0x422a80 and 0x422b34 up to the new target N: flyer_formation, then for
 * the group leader the timer B = A+5Ah; on expiry word B+1 = 10000 and
 * target_select(A, kind, 0). Returns 0 when the method ends there. */
static int fs_pick(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,uint32_t kind,es2_weapon_registers *r) {
    const uint32_t B=A+0x5au;
    fs_call1(g,0x422a50u,A,r);
    r->eax=R32(A+0x45u);r->edx=R32(r->eax+0xcu);
    r->ecx=A==R32(r->edx)?1u:0u;
    if(!r->ecx)return 0;
    fs_call1(g,0x467a48u,B,r);
    r->eax=(uint16_t)r->eax?0u:1u;
    if(!r->eax)return 0;
    W16(B+1u,10000);
    const uint32_t a[3]={A,kind,0};fs_call(g,0x411fa0u,a,3,r);
    return 1;
}

/* Original: DBSIM.EXE 0x422a80 */
uint64_t es2_flyer_search_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    if(!fs_pick(m,g,A,0x11,&r))return pair(0,r.edx);
    const uint32_t N=r.eax;
    if(!N)return pair(0,r.edx);
    r.edx=R32(A+0x45u);
    const uint32_t a[2]={r.edx,N};fs_call(g,0x423918u,a,2,&r);
    if(!(uint16_t)r.eax)return pair(0,r.edx);
    r.eax=N;
    const uint32_t T=R32(A+0x1a4u);
    if(r.eax!=T) {
        if(T) { r.ecx=R32(A+0x1a4u);W16(r.ecx+0x1a2u,R16(r.ecx+0x1a2u)-1u); }
        W32(A+0x1a4u,r.eax);
        if(r.eax) { r.eax=R32(A+0x1a4u);W16(r.eax+0x1a2u,R16(r.eax+0x1a2u)+1u); }
        r.edx=A+0x92u;W8(r.edx+0xbu,1);
    }
    fs_call1(g,0x422bf0u,A,&r);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x422b34 */
uint64_t es2_flyer_patrol_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    if(!fs_pick(m,g,A,0x10,&r))return pair(0,r.edx);
    const uint32_t N=r.eax;
    if(N!=R32(A+0x1a4u)) {
        if(R32(A+0x1a4u)) { r.edx=R32(A+0x1a4u);W16(r.edx+0x1a2u,R16(r.edx+0x1a2u)-1u); }
        W32(A+0x1a4u,N);
        if(N) { r.eax=R32(A+0x1a4u);W16(r.eax+0x1a2u,R16(r.eax+0x1a2u)+1u); }
        W8(A+0x92u+0xbu,1);
    }
    if(!R32(A+0x1a4u))return pair(0,r.edx);
    fs_call1(g,0x422bf0u,A,&r);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x422bdc */
uint64_t es2_flyer_scout_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    (void)m;es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.eax=A;fs_call1(g,0x422a50u,A,&r);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x422d00. The jump table 0x422d33 (order types
 * 0..5): 0 search and destroy 499dach, 3 patrolling 499d70h, 4 sleeping
 * 499de8h and 5 scouting 499e24h (both also set byte A+0A5h); 1, 2 and
 * every other type (unsigned compare, so negative words too, and 0Bh when
 * there is no current order) keep the entry ECX as the record. */
uint64_t es2_flyer_decision_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.edx=A;r.eax=R32(A+0x45u);
    uint32_t index=R32(r.eax+0x6cu);
    if(R32(r.eax+4u*index+0x44u)) {
        index=R32(r.eax+0x6cu);r.eax=R32(r.eax+4u*index+0x44u);r.eax=lo(r.eax,R16(r.eax));
    } else r.eax=lo(r.eax,0xb);
    r.eax=sx(r.eax);
    switch(r.eax) {
    case 0:r.ecx=VA(0x499dacu);break;
    case 3:r.ecx=VA(0x499d70u);break;
    case 4:r.ecx=VA(0x499de8u);r.eax=r.edx+0x92u;W8(r.eax+0x13u,1);break;
    case 5:r.ecx=VA(0x499e24u);r.eax=r.edx+0x92u;W8(r.eax+0x13u,1);break;
    default:break;
    }
    r.eax=r.ecx;r.edx+=0x4du;
    const uint32_t a[2]={r.edx,r.eax};fs_call(g,0x413e50u,a,2,&r);
    return pair(lo(r.eax,1),r.edx);
}

/* Original: DBSIM.EXE 0x422bf0 */
uint64_t es2_flyer_engage_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.eax=A+0x4du;
    {const uint32_t a[2]={A+0x4du,VA(0x499d34u)};fs_call(g,0x413e50u,a,2,&r);}
    r.edx=R32(A+0x45u);r.ecx=R32(r.edx+0xcu);
    r.eax=A==R32(r.ecx)?1u:0u;
    if(!r.eax)return pair(r.eax,r.edx);
    for(uint32_t i=0;;++i) {
        r.edx=R32(A+0x45u);
        if(fx_s16(i)>=fx_s16(R16(r.edx+0x10u))) { r.eax=0;break; }
        r.eax=sx(i);r.edx=R32(r.edx+0xcu);r.eax=R32(r.edx+4u*r.eax);
        if(!r.eax)break;
        const uint32_t M=r.eax;
        if(A==M)continue;
        r.edx=M+0x92u;
        if(R8(r.edx+7u))continue;
        r.edx=R32(A+0x1a4u);
        if(r.edx!=R32(M+0x1a4u)) {
            if(R32(M+0x1a4u)) { r.ecx=R32(M+0x1a4u);W16(r.ecx+0x1a2u,R16(r.ecx+0x1a2u)-1u); }
            W32(M+0x1a4u,r.edx);
            if(r.edx) { r.ecx=R32(M+0x1a4u);W16(r.ecx+0x1a2u,R16(r.ecx+0x1a2u)+1u); }
            r.edx=M+0x92u;r.ecx=r.edx;W8(r.ecx+0xbu,1);
        }
        fs_call1(g,0x422bf0u,M,&r);
    }
    return pair(r.eax,r.edx);
}

/* FLYER AI helpers (flyer.md, section "the FLYER AI helpers `0x4224c4`,
 * `0x422108`, `0x422098`, `0x422260`"). The pure 0x47e08d and 0x47e0c0 are
 * computed with their register effects (as in flyer.c); every other callee
 * goes through g->call. */
/* 0x47e08d(x, y): EDX:EAX = x*y; EAX = bits 16..47, EDX = bits 32..63; ECX kept. */
static void fs_q16(es2_weapon_registers *r,uint32_t x,uint32_t y) {
    const uint64_t p=(uint64_t)(fx_s32(x)*fx_s32(y));r->eax=(uint32_t)(p>>16);r->edx=(uint32_t)(p>>32);
}
/* 0x47e0c0(x, w): EDX:EAX = x*s16(w); EAX = bits 14..45, EDX = bits 32..63; ECX = s16(w). */
static void fs_q14(es2_weapon_registers *r,uint32_t x,uint32_t w) {
    const uint64_t p=(uint64_t)(fx_s32(x)*fx_s16(w));r->eax=(uint32_t)(p>>14);r->edx=(uint32_t)(p>>32);r->ecx=sx(w);
}

/* Original: DBSIM.EXE 0x4224c4. R = [A+45h]+4 is reread where the original
 * rereads it; only DI (the "next waypoint found" flag) is tested. */
uint64_t es2_flyer_route_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[4];
    int next=1;
    r.eax=R32(A+0x45u)+4u;r.edx=r.eax;
    r.ecx=lo(r.ecx,R16(r.edx))+1u; /* 4224e3: INC ECX after a 16-bit load */
    a[0]=r.eax;a[1]=r.ecx;fs_call(g,0x423b0cu,a,2,&r);
    uint32_t P=r.eax;
    if(!P) {
        next=0;
        r.eax=R32(A+0x45u)+4u;r.edx=r.eax;
        r.ecx=lo(r.ecx,R16(r.edx));
        a[0]=r.eax;a[1]=r.ecx;fs_call(g,0x423b0cu,a,2,&r);
        P=r.eax;
    }
    if(next) {
        r.eax=A+0x26u;
        a[0]=P;a[1]=A+0x26u;fs_call(g,0x4928d0u,a,2,&r);
        if(less(r.eax,15000)) { r.edx=R32(A+0x45u)+4u;fs_call1(g,0x42313cu,r.edx,&r); }
    }
    a[0]=A;a[1]=0x7530u;fs_call(g,0x422108u,a,2,&r);
    const uint32_t pitch=r.eax;
    r.eax=A+0x26u;r.edx=P;
    a[0]=P;a[1]=A+0x26u;fs_call(g,0x492934u,a,2,&r);
    r.eax=lo(r.eax,r.eax-R16(A+0x10u));r.ecx=sx(r.eax);
    fs_q16(&r,r.ecx,0x6d60u);
    a[0]=A;a[1]=r.eax;a[2]=pitch;a[3]=0;fs_call(g,0x4222fcu,a,4,&r);
    W16(A+0x21cu,0xb4u);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x422108 */
uint64_t es2_flyer_altitude_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,uint32_t height,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[2];
    r.eax=A+0x92u;
    if(R8(r.eax+0x1cu)) {
        a[0]=sx(R16(A+0x23cu));a[1]=0xfa0u;fs_call(g,0x47e0a0u,a,2,&r);
        r.eax=lo(r.eax,r.eax+R16(A+0xcu));
    } else {
        r.eax=height-R32(A+0x2eu);
        a[0]=0x2710u;a[1]=r.eax;fs_call(g,0x49290cu,a,2,&r);
    }
    a[0]=A;a[1]=r.eax;fs_call(g,0x422098u,a,2,&r);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x422098. The roll word is read once (EBP-2); the
 * original's ECX after return, (roll << 16) | cos (the EBP-4 dword), is
 * dead at its callers. */
uint64_t es2_flyer_pitch_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,uint32_t goal,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    fs_q16(&r,sx(goal)-sx(R16(A+0xcu)),0x5dcu);
    const uint32_t e=r.eax,roll=R16(A+0xeu);
    r.eax=lo(r.eax,roll);r.edx=lo(r.edx,roll);
    fs_call1(g,0x47dff0u,r.edx,&r);
    const uint32_t c=(uint16_t)r.eax;
    fs_q14(&r,sx(c),e);
    const uint32_t v=sx(r.eax);
    fs_q16(&r,sx(R16(A+0x25bu)),0x1f40u);
    r.eax=v-r.eax;
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x422260. Only the low words of d, h and spacing are
 * read; the entry EDX's high word rides in the heading_cos slot. */
uint64_t es2_flyer_throttle_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,uint32_t L,
    uint32_t d,uint32_t h,uint32_t spacing,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    const uint32_t angle=(uint16_t)(h+R16(A+0x10u)-R16(L+0x10u));
    r.eax=sx(d);r.edx=lo(r.edx,angle);
    fs_call1(g,0x47dff0u,r.edx,&r);
    const uint32_t c=(uint16_t)r.eax;
    fs_q14(&r,sx(d),c);
    r.eax=sx(r.eax);r.eax=lo(r.eax,r.eax-spacing);r.ecx=sx(r.eax);
    r.edx=lo(r.edx,R16(L+0x233u));r.edx=lo(r.edx,r.edx-R16(A+0x233u));
    r.eax=lo(r.eax,R16(A+0x21cu));
    r.edx<<=3;r.ecx=fx_sar(r.ecx,1);r.ecx=lo(r.ecx,r.ecx+r.edx);
    r.edx=r.ecx;r.eax=lo(r.eax,r.eax+r.edx);
    if(fx_s16(r.eax)>=0x100)r.edx=lo(r.edx,0x100u);
    else if(fx_s16(r.eax)<=0x8c)r.edx=lo(r.edx,0x8cu);
    else r.edx=r.eax;
    W16(A+0x21cu,r.edx);
    return pair(r.eax,r.edx);
}
