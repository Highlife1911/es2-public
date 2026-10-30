#include "es2/core/convoy.h"
#include "fixed.h"
/* VEHICLE update chain (docs/reverse-engineering/convoy.md). r holds what the
 * original's EAX/EDX/ECX hold at the next game call; lo() is a 16-bit load that
 * keeps the register's high word. Frame locals passed by pointer live in guest
 * scratch below the synthetic EBP f = scratch + 40h. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes its resolved target. */
static void cv_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,g->image+va-0x400000u,a,n,r);
}
static void cv_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* 0x47df04 computed here (its only callers are 46aa60, 427c68 and
 * 41fd37, all own code): ENTER 0,0; MOV CX,[M+2]; XCHG [M+4],CX; MOV
 * [M+2],CX exchanges the words M+2 and M+4; EAX = M (the argument), CX =
 * the old [M+4] over ECX's high word, EDX unchanged. The call is only
 * observed, so call_47df04 still counts it. */
static void cv_swap(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t M,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,0x47df04u,0);
    const uint32_t w1=R16(M+2u),w2=R16(M+4u);
    W16(M+4u,w1);W16(M+2u,w2);
    r->eax=M;r->ecx=lo(r->ecx,w2);
}
/* The signed word clamp of 46a8dc and its two copies in 46aa60: x >= hi
 * gives hi over keep's high word, x <= lo gives lo over keep's high word,
 * otherwise the whole dword x. */
static uint32_t clamp(uint32_t x,int32_t low,int32_t high,uint32_t keep) {
    if(fx_s16(x)>=high)return lo(keep,(uint32_t)high);
    if(fx_s16(x)<=low)return lo(keep,(uint32_t)low);
    return x;
}

/* Original: DBSIM.EXE 0x46a89c */
uint64_t es2_convoy_steer_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,uint32_t steer,uint32_t speed,const uint32_t entry[3]) {
    uint32_t a[3];
    es2_weapon_registers r={o+0x222u,entry[1],entry[2]};
    a[0]=o+0x222u;cv_call(g,0x467a48u,a,1,&r);
    if((uint16_t)r.eax)speed=lo(speed,R8(o+0x227u)?0xff38u:0xc8u);
    uint32_t s=clamp(steer,-0x100,0x100,r.eax);
    r.eax=s;r.edx=o+0x220u;r.ecx=o+0x222u;
    a[0]=o+0x220u;a[1]=speed;a[2]=0x1eu;cv_call(g,0x467adc,a,3,&r);
    uint32_t w0=R16(o+0xcu),w1=R16(o+0xeu),w2=R16(o+0x10u);
    r.edx=lo(r.edx,w0);r.ecx=lo(r.ecx,w1);r.eax=lo(o+0xcu,w2);
    a[0]=0xc8u;a[1]=sx(s);cv_call(g,0x47e0a0u,a,2,&r);
    uint32_t yaw=(w2+r.eax)&0xffffu;
    W32(o+0xcu,w0|w1<<16);W16(o+0x10u,yaw);W16(o+0x32u,0);
    return pair(o+0xcu,w1<<16|yaw);
}

/* Original: DBSIM.EXE 0x46a958. Frame: the target point L at f-0Ch
 * (x, y; f-4 is never written). */
uint64_t es2_convoy_seek_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,uint32_t p,uint32_t heading,uint32_t speed,const uint32_t entry[3]) {
    uint32_t a[4],l=g->scratch+0x40u-0xcu;
    es2_weapon_registers r={o+0x26u,entry[1],entry[2]};
    a[0]=o+0x26u;a[1]=p;cv_call(g,0x4928d0u,a,2,&r);
    uint32_t d=r.eax,x=R32(p);W32(l,x);
    W32(l+4u,R32(p+4u));
    r.eax=0x2328u-d;r.edx=lo(x,heading);r.ecx=l;
    a[0]=l;a[1]=r.edx;a[2]=r.eax;cv_call(g,0x4929fcu,a,3,&r);
    r.eax=o+0x26u;r.edx=l;
    a[0]=l;a[1]=o+0x26u;cv_call(g,0x492934u,a,2,&r);
    uint32_t diff=lo(r.eax,r.eax-R16(o+0x10u));
    r.edx=fx_sar(sx(diff),4);r.eax=lo(diff,speed?speed:0xaau);
    a[0]=o;a[1]=r.edx;a[2]=r.eax;a[3]=0;cv_call(g,0x46a89cu,a,4,&r);
    return pair((int32_t)d<10000?1u:0u,r.edx);
}

/* Original: DBSIM.EXE 0x46a9e8 */
uint64_t es2_convoy_lead_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    uint32_t a[4],h=R32(o+0x45u)+4u;
    es2_weapon_registers r={lo(entry[0],R16(h))+1u,entry[1],entry[2]};
    a[0]=h;a[1]=r.eax;cv_call(g,0x423b0cu,a,2,&r);
    uint32_t first=r.eax;
    if(!first) {
        a[0]=o;a[1]=0;a[2]=0;a[3]=0;cv_call(g,0x46a89cu,a,4,&r);
        return pair(r.eax,r.edx);
    }
    h=R32(o+0x45u)+4u;r.eax=lo(r.eax,R16(h));
    a[0]=h;a[1]=r.eax;cv_call(g,0x423b0cu,a,2,&r);
    a[0]=first;a[1]=r.eax;cv_call(g,0x492934u,a,2,&r);
    a[0]=o;a[1]=first;a[2]=r.eax;a[3]=0;cv_call(g,0x46a958u,a,4,&r);
    if(!(uint16_t)r.eax)return pair(r.eax,r.edx);
    h=R32(o+0x45u)+4u;r.edx=h;
    a[0]=h;cv_call(g,0x42313cu,a,1,&r);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x46aa60. Frame: D f-34h, R f-28h, V f-1Ch, M f-8. */
uint64_t es2_convoy_follow_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,uint32_t follow_edi,const uint32_t entry[3]) {
    uint32_t a[4],f=g->scratch+0x40u,dv=f-0x34u,rv=f-0x28u,v=f-0x1cu,mv=f-8u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    a[0]=o;cv_call(g,0x46a5bcu,a,1,&r);
    uint32_t leader=r.eax;
    W32(v,R32(leader+0x26u));W32(v+4u,R32(leader+0x2au));
    uint32_t vz=R32(leader+0x2eu);W32(v+8u,vz);
    uint32_t table=R32(o);
    r.eax=vz;r.edx=v;r.ecx=table;
    a[0]=o;a[1]=v;cv_virtual(g,R32(table+0x78u),0x78,a,2,&r);
    uint32_t edi=lo(follow_edi,R16(leader+0x10u));
    uint32_t eax=lo(edi,edi-R16(o+0x10u)),edx;
    if((uint16_t)eax==0x8000u)edx=lo(r.edx,0x7fffu);
    else if((uint16_t)eax&0x8000u)edx=lo(eax,0u-eax);
    else edx=eax;
    if(fx_s16(edx)<0x4000) {
        uint32_t bx=R32(o+0x26u),c=leader+0xcu,by=R32(o+0x2au),bz=R32(o+0x2eu);
        r.eax=c;r.edx=by;r.ecx=c;
        if(!R16(c+0x26u)) {
            r.edx=c;r.ecx=c+6u;
            a[0]=c;a[1]=c+6u;cv_call(g,0x47ebb8u,a,2,&r);
            r.edx=c;W16(c+0x26u,1);
        }
        /* The first four matrix words (2 x 2 rotation) into M; the loads
         * through DX/CX/AX are overwritten before the next call. */
        uint32_t w=c+6u;
        for(unsigned i=0;i<4;++i)W16(mv+2u*i,R16(w+2u*i));
        W32(dv+8u,bz-R32(v+8u));W32(dv,bx-R32(v));
        r.eax=R32(v);r.edx=R32(v+4u);W32(dv+4u,by-r.edx);r.ecx=mv;
        cv_swap(m,g,mv,&r);
        r.ecx=mv;
        a[0]=dv;a[1]=r.eax;a[2]=rv;r.eax=dv;r.edx=rv;cv_call(g,0x47ffe8u,a,3,&r);
        uint32_t s=sx(R16(leader+0x220u))-fx_sar(R32(rv+4u),5);
        r.edx=clamp(s,-0x96,0x100,r.edx);s=sx(r.edx);
        r.eax=s;r.ecx=v;
        a[0]=v;a[1]=edi;a[2]=0x4e20u;cv_call(g,0x4929fcu,a,3,&r);
        r.eax=v;
        a[0]=o;a[1]=v;a[2]=edi;a[3]=s;cv_call(g,0x46a958u,a,4,&r);
        return pair(r.eax,r.edx);
    }
    r.eax=eax;r.edx=v;
    a[0]=o+0x26u;a[1]=v;cv_call(g,0x4928d0u,a,2,&r);
    uint32_t far=fx_sar(r.eax,5);
    r.eax=v;
    a[0]=v;a[1]=o+0x26u;cv_call(g,0x492934u,a,2,&r);
    uint32_t steer=fx_sar(sx(r.eax-R16(o+0x10u)),4);
    uint32_t speed=sx(clamp(far,-0x96,0x100,steer));
    r.eax=steer;r.edx=speed;
    a[0]=o;a[1]=steer;a[2]=speed;a[3]=0;cv_call(g,0x46a89cu,a,4,&r);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x46a810. Frame: velocity f-0Ch, result f-18h. */
uint64_t es2_convoy_drive_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    uint32_t a[3],f=g->scratch+0x40u,vec=f-0xcu,out=f-0x18u,c=o+0xcu;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    a[0]=o;cv_call(g,0x46a5bcu,a,1,&r);
    r.ecx=o;cv_call(g,o==r.eax?0x46a9e8u:0x46aa60u,a,1,&r);
    W32(vec,0);W32(vec+4u,sx(R16(o+0x220u)));W32(vec+8u,0);
    if(!R16(c+0x26u)) {
        r.eax=c+6u;r.edx=0;r.ecx=0;
        a[0]=c;a[1]=c+6u;cv_call(g,0x47ebb8u,a,2,&r);
        W16(c+0x26u,1);
    }
    r.eax=c+6u;r.edx=out;r.ecx=vec;
    a[0]=vec;a[1]=c+6u;a[2]=out;cv_call(g,0x48043cu,a,3,&r);
    W32(o+0x2eu,R32(out+8u));W32(o+0x26u,R32(out));
    uint32_t y=R32(out+4u);W32(o+0x2au,y);
    return pair(o+0x26u,y);
}

/* Original: DBSIM.EXE 0x46a6d4. Frame: the angle pair P at f-8 (words at
 * f-8 and f-4; f-6 and f-2 are never written). */
uint64_t es2_convoy_think_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    uint32_t a[2],group=R32(o+0x45u),member=0,eax=group;
    if(fx_s16(R16(group+0x10u))>0) { eax=R32(group+0xcu);member=R32(eax); }
    es2_weapon_registers r={lo(eax,R16(member+0x1a8u)),member,entry[2]};
    a[0]=o;
    if((uint16_t)r.eax!=3u) { cv_call(g,0x403ca8u,a,1,&r);return pair(r.eax,r.edx); }
    uint32_t config=R32(o+0x1f2u);r.ecx=config;
    if(R16(config+0x2eu)) {
        cv_call(g,0x404100u,a,1,&r);
        uint32_t p=g->scratch+0x40u-8u;
        r.eax=lo(r.eax,0u-R16(o+0x20du));W16(p,r.eax);
        r.edx=lo(r.edx,R16(o+0x20fu));W16(p+4u,r.edx);
        if(!R32(o+0x1a4u)) { r.ecx=p;a[1]=p;cv_call(g,0x403d5cu,a,2,&r); }
    } else cv_call(g,0x403ca8u,a,1,&r);
    if(R8(o+0x99u))return pair(o+0x92u,r.edx);
    uint32_t x=R32(o+0x26u),y=R32(o+0x2au),z=R32(o+0x2eu);
    uint32_t w0=R16(o+0xcu),w1=R16(o+0xeu),w2=R16(o+0x10u);
    r.edx=lo(x,w0);r.ecx=lo(y,w1);r.eax=lo(o+0xcu,w2);
    cv_call(g,0x46a810u,a,1,&r);
    r.ecx=o;cv_call(g,0x4029d8u,a,1,&r);
    r.eax=R32(o+0x2eu);W32(o+0x2eu,r.eax);r.ecx=o;
    cv_call(g,0x46a614u,a,1,&r);
    if(!(uint16_t)r.eax)return pair(r.eax,r.edx);
    W8(o+0x227u,fx_s16(R16(o+0x220u))>0);W16(o+0x223u,3000u);
    W32(o+0x2eu,z);W32(o+0x26u,x);W32(o+0x2au,y);
    W32(o+0xcu,w0|w1<<16);W16(o+0x10u,w2);W16(o+0x32u,0);W16(o+0x220u,0);
    return pair(o+0xcu,y);
}

/* Original: DBSIM.EXE 0x46a5bc (convoy.md, section "46a5bc
 * convoy_leader(B)"). The group, its list and its count are read again for every index. */
uint64_t es2_convoy_leader_registers(const es2_guest_memory *m,uint32_t o) {
    uint32_t i=0,edx;
    do {
        edx=R32(o+0x45u);
        if(fx_s16(R16(edx+0x10u))>fx_s16(i))edx=R32(R32(edx+0xcu)+4u*i);
        else edx=0;
        if(!R8(edx+0xa4u)&&!R8(edx+0x99u))return pair(edx,edx);
    } while(++i<4u);
    return pair(0,edx);
}
