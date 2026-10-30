#include "es2/core/ai_helpers.h"
#include "es2/core/movement.h"
#include "fixed.h"
/* Route and behaviour helpers (docs/reverse-engineering/mission_runtime.md,
 * section "Route and behaviour helpers"). r holds what the original's
 * EAX/EDX/ECX hold at the next game call; lo() is a 16-bit load that keeps the
 * register's high word. Memory is read again wherever the original reads it
 * again. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* A DBSIM VA (globals, image tables, the pushed script table). */
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)

static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t sx8(uint32_t x) { x&=0xffu; return x<0x80u?x:x|0xffffff00u; }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes its resolved target. */
static void ah_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}
static void ah_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* One step of 4929fc: d times the cosine word of angle, >> 14, low word
 * sign-extended (492b10/492b31 over 492aed and the Q14 multiply 47e0c0). */
static int32_t po_product(uint32_t distance,uint32_t angle,const void *quarter) {
    return fx_s16(distance)*fx_s16(es2_heading_cos((uint16_t)angle,quarter));
}
/* Original: DBSIM.EXE 0x4929fc */
uint64_t es2_point_offset_registers(const es2_guest_memory *m,uint32_t p,uint32_t angle,uint32_t distance,
    const void *quarter) {
    const uint32_t a=(angle+0x4000u)&0xffffu;
    int32_t product=po_product(distance,a,quarter);
    W32(p,R32(p)+sx(fx_sar((uint32_t)product,14)));
    product=po_product(distance,a+0xc000u,quarter);
    W32(p+4u,R32(p+4u)+sx(fx_sar((uint32_t)product,14)));
    return pair(p,product<0?0xffffffffu:0u);
}

/* Original: DBSIM.EXE 0x41cb44 */
uint64_t es2_threat_sum_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t x,
    const uint32_t entry[3]) {
    uint32_t a[2],sum=0,e=0;
    es2_weapon_registers r={0,0,entry[2]};
    for(;;) {
        r.eax=R32(VA(0x4a9b7au));
        a[0]=r.eax;a[1]=e;ah_call(g,0x471c70u,a,2,&r);
        e=r.eax;
        if(!e)break;
        if(R32(e+0x1a4u)==x) { r.ecx=sx(R16(e+0x29eu));sum+=r.ecx; }
    }
    return pair(sum,r.edx);
}

/* Original: DBSIM.EXE 0x41c4a8 */
uint64_t es2_engage_done_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    uint32_t a[2],v;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    if(R16(A+0x23eu)==4u&&R32(A+0x248u)) {
        r.eax=R32(A+0x1a4u);a[0]=r.eax;ah_call(g,0x411cb4u,a,1,&r);
        return pair(fx_s16(r.eax)>fx_s16(R16(A+0x250u))?1u:0u,r.edx);
    }
    r.edx=R32(A+0x1a4u);r.ecx=R32(A+0x45u);
    a[0]=r.ecx;a[1]=r.edx;ah_call(g,0x423918u,a,2,&r);
    if((uint16_t)r.eax) {
        if(!R8(R32(A+0x45u)+0x12u)) {
            const uint32_t T=R32(A+0x1a4u);
            v=R8(T+0xa4u)||R8(T+0x99u)?1u:0u;
            return pair(v,v);
        }
        const uint32_t b=R8(R32(A+0x1a4u)+0x99u);
        return pair(sx8(b),(r.edx&0xffffff00u)|b);
    }
    const uint32_t T=R32(A+0x1a4u);
    v=R8(T+0xa5u)||R8(T+0xa4u)||R8(T+0x99u)?1u:0u;
    return pair(v,v);
}

/* Original: DBSIM.EXE 0x41c72c. Frame: the point L at f-0Ch with the
 * synthetic EBP f = scratch + 0Ch. B = A+5Ah is the behaviour block. */
uint64_t es2_flank_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    uint32_t S,const uint32_t entry[3]) {
    uint32_t a[3];
    const uint32_t B=A+0x5au,l=g->scratch;
    es2_weapon_registers r={B+5u,entry[1],entry[2]};
    a[0]=B+5u;ah_call(g,0x467a48u,a,1,&r);
    if((uint16_t)r.eax) { /* the first timer still runs */
        W32(S+0x10u,0);W16(S+0xeu,0xffffu);W16(S+2u,0);W8(B+0xcu,0);W16(B+9u,0xfa0u);
        return pair(B+0xbu,0);
    }
    r.edx=B+8u;r.ecx=B+5u;
    a[0]=B+8u;ah_call(g,0x467a48u,a,1,&r);
    if(!(uint16_t)r.eax&&(int32_t)R32(A+0x288u)>0x64)W16(B+6u,0x3e8u);
    const int32_t limit=R8(B+0xcu)?0x4000:0x6000;
    const uint32_t distance=sx(R16(S+8u));
    if(limit<=(int32_t)distance) { /* close: virtual +3Ch decides stop or reverse */
        const uint32_t table=R32(A);
        r.eax=B+0xbu;r.edx=table;r.ecx=distance;
        a[0]=A;ah_virtual(g,R32(table+0x3cu),0x3c,a,1,&r);
        W8(B+0xcu,1);
        if((uint16_t)(r.eax+0x7d0u)<0xfa0u) { W16(S+0xeu,0);W16(B+3u,0); }
        else W16(S+0xeu,0xffffu);
        return pair(0,B+0xbu);
    }
    const uint32_t P=R32(A+0x1a4u)+0x26u;
    W32(l,R32(P));
    uint32_t c=R32(P+4u);W32(l+4u,c);c=lo(c,0xe890u);
    const uint32_t z=R32(P+8u);W32(l+8u,z);
    uint32_t angle=lo(z,R16(S));
    W32(S+0x10u,0);W16(S+0xeu,1);
    angle=lo(angle,angle+0x8000u);
    if(fx_s16(R16(S+2u))>=0)c+=0x2ee0u;
    angle=lo(angle,angle+c);
    r.eax=l;r.edx=0;r.ecx=c;
    a[0]=l;a[1]=angle;a[2]=0x3a98u;ah_call(g,0x4929fcu,a,3,&r);
    r.edx=A+0x26u;r.ecx=l;
    a[0]=l;a[1]=A+0x26u;ah_call(g,0x492934u,a,2,&r);
    W16(S+2u,r.eax-R16(A+0x10u));
    W8(B+0xcu,0);
    return pair(B+0xbu,r.edx);
}

/* 413E50 (A+4Dh, the script table at 499800) with the path's EDX/ECX. */
static void ah_retreat_script(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,es2_weapon_registers *r) {
    uint32_t a[2]={A+0x4du,VA(0x499800u)};
    (void)m;r->eax=A+0x4du;ah_call(g,0x413e50u,a,2,r);
}
/* Original: DBSIM.EXE 0x41cb94. Frame f = scratch + 48h: the six ratio
 * words at f-48h stay in host memory; only the address f-3Ch reaches a
 * call (as ECX). */
uint64_t es2_retreat_check_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    uint32_t a[1];
    es2_weapon_registers r={A+0x92u,entry[1],entry[2]};
    int out=1;
    if(R8(A+0xa5u)) { r.edx=1; }
    else if(R8(A+0xa4u)) { r.eax=1;r.edx=1; }
    else { r.ecx=A+0x92u;out=R8(A+0x99u)!=0;r.eax=r.edx=out?1u:0u; }
    if(out) { /* A cannot fight: resume the order or start the script */
        r.ecx=A;r.edx=R16(R32(A+0x1a4u)+0x1a8u);r.edx=r.edx==1u||r.edx==3u?1u:0u;r.eax=r.edx;
        if(r.eax) {
            r.eax=R32(A+0x1a4u);r.edx=R32(r.eax+0x1f2u);
            if(!R16(r.edx+0x2eu)) {
                a[0]=A;ah_call(g,0x41eb34u,a,1,&r);
                return pair(lo(r.eax,1),r.edx);
            }
        }
        ah_retreat_script(m,g,A,&r);
        return pair(lo(r.eax,1),r.edx);
    }
    const uint32_t table=R32(A);
    r.eax=0;r.edx=A;r.ecx=table;
    a[0]=A;ah_virtual(g,R32(table+0x40u),0x40,a,1,&r);
    uint32_t score=r.eax&0xffffu;
    uint16_t ratio[6];
    for(uint32_t i=0;i<6u;++i) {
        const uint32_t k=sx(R16(VA(0x49a328u)+2u*i));
        const uint32_t maxima=R32(R32(A+0x212u)+4u);
        const int32_t c=fx_s16(R16(R32(A+0x206u)+2u*k));
        if(c<0)ratio[i]=0x100u;
        else ratio[i]=(uint16_t)(uint32_t)((int64_t)c*256/fx_s16(R16(maxima+2u*k)));
    }
    uint32_t edx=0;
    for(uint32_t j=0;j<3u;++j) {
        const uint32_t weight=VA(0x49a334u)+2u*j;
        edx=0;
        for(unsigned i=0;i<6u;++i)
            if(fx_s16(R16(VA(0x49a33au)+2u*j))<fx_s16(ratio[i])) { score=(score+R16(weight))&0xffffu;edx=lo(weight,1); }
        if(!(uint16_t)edx)break;
    }
    const int32_t s=fx_s16(score);
    uint32_t flag=0;
    r.eax=6;r.edx=edx;r.ecx=g->scratch+0xcu;
    if(s>0x46) {
        r.eax=A;W16(A+0x2aau,0x3e8u);r.ecx=A;
        if(R16(A+0x1a2u))flag=1;
    } else if(s>0x32) {
        r.eax=A;W16(A+0x2aau,0x258u);r.edx=A;r.eax=lo(A,R16(A+0x1a2u));
        if(fx_s16(r.eax)>2)flag=1;
        else if((uint16_t)r.eax) {
            a[0]=A;ah_call(g,0x41cb44u,a,1,&r);
            r.ecx=A;r.edx=sx(R16(A+0x29eu));
            if((int32_t)r.eax>(int32_t)r.edx)flag=1;
        }
    } else if(s>0x1e) {
        r.eax=A;W16(A+0x2aau,0x12cu);r.ecx=A;
        if(fx_s16(R16(A+0x1a2u))>2) {
            a[0]=A;ah_call(g,0x41cb44u,a,1,&r);
            const int32_t n=fx_s16(R16(A+0x1a2u)),q=(int32_t)((int64_t)(int32_t)r.eax/n);
            r.eax=(uint32_t)q;r.ecx=A;r.edx=sx(R16(A+0x29eu));
            if(q>(int32_t)r.edx)flag=1;
        }
    }
    if(flag)ah_retreat_script(m,g,A,&r);
    return pair(lo(r.eax,flag),r.edx);
}

/* Original: DBSIM.EXE 0x41ce08 */
uint64_t es2_part_pick_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    uint32_t a[2],end;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    const uint32_t T=R32(A+0x1a4u),W=R32(T+0x20eu),state=VA(0x4d2599u);
    uint32_t best=0xffffu,top=0;
    a[0]=state;ah_call(g,0x492ee0u,a,1,&r);
    r.eax=lo(r.eax,r.eax&0x7fu);
    const uint32_t D=R32(A+0x30bu);
    r.edx=D&&fx_s16(R16(D+0x7fu))<0xaa?1u:0u;
    r.ecx=T+0x92u;
    if(R8(T+0xb4u)) { end=1;r.eax=0; }
    else if(fx_s16(r.eax)<0x28||r.edx) { r.eax=lo(r.eax,7);end=0x13; }
    else if(fx_s16(r.eax)>=0x50) { end=7;r.eax=0; }
    else {
        r.eax=lo(r.eax,0x13);r.edx=R32(A+0x202u);
        r.ecx=lo(r.ecx,R16(r.edx+8u)+0x13u);end=r.ecx&0xffffu;
    }
    uint32_t i=r.eax,p;
    r.eax=sx(i);r.edx=W+2u*r.eax;p=r.edx;
    if(fx_s16(i)<fx_s16(end)) {
        do {
            if(R16(p)) {
                r.ecx=A+0x206u;
                a[0]=A+0x206u;a[1]=i;ah_call(g,0x40dbc0u,a,2,&r);
                const uint32_t ratio=r.eax;
                a[0]=state;ah_call(g,0x492ee0u,a,1,&r);
                r.ecx=state;r.eax=lo(r.eax,(r.eax&0x3fu)+ratio);r.edx=ratio;
                if(fx_s16(r.eax)>fx_s16(top)) { top=r.eax&0xffffu;best=i&0xffffu; }
            }
            ++i;p+=2u;
        } while(fx_s16(i)<fx_s16(end));
    }
    W16(A+0x2a2u,best);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41e828 */
uint64_t es2_drive_apply_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    uint32_t S) {
    uint32_t a[4];
    if((int32_t)R32(S+0xau)<(int32_t)R32(S+0x10u))
        W16(S+0xeu,fx_s16(R16(S+4u))>=0x4000?1u:0xffffu);
    else if((int32_t)R32(S+0xau)>(int32_t)R32(S+0x14u))
        W16(S+0xeu,fx_s16(R16(S+4u))>0x4000?0xffffu:1u);
    const uint32_t delta=R16(S+2u);
    W16(S+4u,delta==0x8000u?0x7fffu:(delta&0x8000u)?0u-delta:delta);
    const uint32_t steer=fx_sar(sx(R16(S+2u)),6);
    uint32_t speed=0;
    if(R16(S+0xeu)) {
        W16(A+0x5au,R16(S+0xeu));
        speed=lo(A+0x5au,R16(S+0xeu));
        if((uint16_t)speed) {
            const int forward=!(speed&0x8000u);
            speed=lo(speed,0xff00u);
            if(forward)speed+=0x200u;
        }
    }
    es2_weapon_registers r={S,steer,A};
    a[0]=A;a[1]=steer;a[2]=speed;a[3]=0;ah_call(g,0x416a04u,a,4,&r);
    return pair(r.eax,r.edx);
}
