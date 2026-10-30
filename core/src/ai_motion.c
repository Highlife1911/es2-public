#include "es2/core/ai_motion.h"
#include "fixed.h"

/* AI flyer move step 4218c4 and formation steering 41fbb8.
 * (docs/reverse-engineering/flyer.md, ai.md). r follows EAX/EDX/ECX wherever a
 * game call or the result can see them; lo() is a 16-bit write that keeps the
 * register's high word. Memory is read again wherever the original reads it
 * again (callees may write it). */
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
static int32_t s32(uint32_t x) { return (int32_t)x; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
/* A direct call at its original address, or the virtual at `slot` of an
 * object whose table the register already holds (as in the original). */
static void am_call(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
#define CALL(va,n) am_call(g,g->image+(va)-0x400000u,0,a,(n),&r)
#define VCALL(table,slot,n) am_call(g,R32((table)+(slot)),(slot),a,(n),&r)

/* One terrain probe after V is written: transform_point(V, O+6, Q), then
 * terrain_height(grid, Q). Returns the height; EAX/EDX/ECX as the original. */
static uint32_t fm_probe(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_registers *rp,uint32_t mat,
                         uint32_t V,uint32_t Q,int first) {
    es2_weapon_registers r=*rp;uint32_t a[3];
    a[0]=V;a[1]=mat;a[2]=Q;CALL(0x48043c,3);
    const uint32_t grid=R32(g->image+0xa0be0u);
    if(first) { r.ecx=Q;r.eax=grid; } else { r.eax=Q;r.edx=grid; }
    a[0]=grid;a[1]=Q;CALL(0x46e180,2);
    *rp=r;return r.eax;
}

/* Original: DBSIM.EXE 0x4218c4. */
uint64_t es2_flyer_move_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t flyer,const uint32_t entry[3]) {
    const uint32_t X=flyer,O=X+0xcu,mat=X+0x12u,P=X+0x26u,S=X+0x243u,V=g->scratch+0x80u-0x2cu,Q=g->scratch+0x80u-0x38u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3],h,d;
    r.eax=R32(X+0x1fau);
    if(!R16(O+0x26u)) { r.eax=mat;a[0]=O;a[1]=mat;CALL(0x47ebb8,2);W16(O+0x26u,1); }
    /* Position += world velocity (z, x, y). */
    r.eax=P;r.ecx=R32(S+0x14u);W32(P+8u,R32(P+8u)+r.ecx);
    r.ecx=R32(S+0xcu);W32(P,R32(P)+r.ecx);
    r.edx=R32(S+0x10u);W32(P+4u,R32(P+4u)+r.edx);
    /* Probe 1: right, behind, below; rolls one way. */
    W32(V,1000u);W32(V+4u,0u-420u);W32(V+8u,0u-300u);
    r.edx=Q;r.eax=V;h=fm_probe(m,g,&r,mat,V,Q,1);
    if(s32(h)>s32(R32(Q+8u))) {
        d=h-R32(Q+8u);r.eax=d;a[0]=0xfa0u;a[1]=d;CALL(0x47e0b0,2);
        W16(S+0x1au,R16(S+0x1au)-r.eax);
        r.edx=lo(r.edx,R16(S+0x1au)+R16(X+0xeu));r.eax=O;W16(X+0xeu,r.edx);W16(O+0x26u,0);
    }
    /* Probe 2: mirrored to the left; rolls the other way. */
    W32(V,0u-R32(V));
    r.eax=mat;r.edx=Q;r.ecx=V;h=fm_probe(m,g,&r,mat,V,Q,0);
    if(s32(h)>s32(R32(Q+8u))) {
        d=h-R32(Q+8u);r.eax=d;a[0]=0xfa0u;a[1]=d;CALL(0x47e0b0,2);
        W16(S+0x1au,R16(S+0x1au)+r.eax);
        r.edx=lo(r.edx,R16(S+0x1au)+R16(X+0xeu));r.eax=O;W16(X+0xeu,r.edx);W16(O+0x26u,0);
    }
    /* Probes 3 (ahead) and 4 (far ahead, below): pitch up. */
    static const uint32_t ahead[2][3]={{0u,1000u,0u},{0u,15000u,0u-1500u}},gain[2]={0x7d0u,0x14u};
    for(unsigned k=0;k<2;++k) {
        W32(V,ahead[k][0]);W32(V+4u,ahead[k][1]);W32(V+8u,ahead[k][2]);
        r.eax=mat;r.edx=Q;r.ecx=V;h=fm_probe(m,g,&r,mat,V,Q,0);
        if(s32(h)>s32(R32(Q+8u))) {
            if(fx_s16(R16(S+0x18u))<0)W16(S+0x18u,0);
            d=h-R32(Q+8u);r.eax=d;a[0]=gain[k];a[1]=d;CALL(0x47e0b0,2);
            W16(S+0x18u,R16(S+0x18u)+r.eax);
            r.edx=lo(r.edx,R16(S+0x18u)+R16(X+0xcu));r.eax=O;W16(X+0xcu,r.edx);W16(O+0x26u,0);
        }
    }
    /* Ground clearance: at least 500 above the terrain under P. */
    r.eax=P;r.edx=R32(P+8u);W32(Q+8u,r.edx);r.edx=Q;r.ecx=R32(P);W32(Q,r.ecx);
    r.ecx=R32(g->image+0xa0be0u);r.eax=R32(P+4u);W32(Q+4u,r.eax);
    a[0]=r.ecx;a[1]=Q;CALL(0x46e180,2);
    h=r.eax+500u;
    if(s32(h)>s32(R32(Q+8u))) {
        W32(Q+8u,h);r.eax=P;r.edx=R32(Q+8u);W32(P+8u,r.edx);r.ecx=R32(Q);W32(P,r.ecx);r.edx=R32(Q+4u);W32(P+4u,r.edx);
    }
    /* Fly-by sound row 31h once per approach to the camera while moving. */
    if(s32(R32(S+4u))>0) {
        uint32_t dx=R32(P),dy=R32(P+4u),dz=R32(P+8u);
        const uint32_t C=R32(g->image+0xd24eau)+4u;
        dz-=R32(C+8u);dx-=R32(C);r.eax=R32(C+4u);dy-=r.eax;
        r.edx=dx;r.ecx=dx;a[0]=dx;a[1]=dy;a[2]=dz;CALL(0x47de72,3);
        if(s32(r.eax)>=30000)W16(X+6u,0);
        else if(!R16(X+6u)) { a[0]=0x31u;CALL(0x462830,1);r.ecx=0x31u;W16(X+6u,1); }
    }
    return pair(lo(r.eax,1u),r.edx);
}

/* 47ffe8: two dwords times a 2 x 2 word matrix (words 0,2 give x', 1,3 give
 * y'), 64-bit products plus 2000h, bits 14..45 kept. */
static void am_rotate2(const es2_guest_memory *m,uint32_t vector,uint32_t matrix,uint32_t out) {
    for(unsigned i=0;i<2;++i) {
        const uint64_t sum=(uint64_t)(fx_s32(R32(vector))*fx_s16(R16(matrix+2u*i)))+
            (uint64_t)(fx_s32(R32(vector+4u))*fx_s16(R16(matrix+4u+2u*i)))+0x2000u;
        W32(out+4u*i,(uint32_t)(sum>>14));
    }
}

/* Original: DBSIM.EXE 0x41fbb8. T, R, Q, M are the frame locals at
 * EBP-1Ch, -28h, -34h and -8. */
uint64_t es2_formation_steer_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t vehicle,const uint32_t entry[3]) {
    const uint32_t A=vehicle,F=g->scratch+0x80u,T=F-0x1cu,R=F-0x28u,Q=F-0x34u,M=F-8u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[4];
    const uint32_t L=R32(R32(R32(A+0x45u)+0xcu));
    r.edx=R32(L+0x26u);W32(T,r.edx);r.ecx=R32(L+0x2au);W32(T+4u,r.ecx);r.eax=R32(L+0x2eu);W32(T+8u,r.eax);
    r.edx=T;r.ecx=R32(A);a[0]=A;a[1]=T;VCALL(r.ecx,0x78u,2);
    r.eax=T;r.edx=A+0x26u;a[0]=A+0x26u;a[1]=T;CALL(0x4928d0,2);
    const uint32_t d=r.eax;
    r.eax=A+0x26u;r.edx=T;a[0]=T;a[1]=A+0x26u;CALL(0x492934,2);
    r.eax=lo(r.eax,r.eax-R16(A+0x10u));
    a[2]=0;a[3]=0;
    if(s32(d)<2000) {
        r.eax=lo(r.eax,R16(A+0x10u)-R16(L+0x10u));
        r.edx=fx_sar(0u-(uint32_t)fx_s16(r.eax),6);
        a[0]=A;a[1]=r.edx;CALL(0x416a04,4);
        return pair(r.eax,r.edx);
    }
    int near_path=0;
    if(!(s32(d)>25000)) {
        r.ecx=lo(r.ecx,R16(L+0x28eu)+0x19u);
        r.edx=(uint16_t)r.ecx<0x32u;near_path=!r.edx;
    }
    if(!near_path) {
        r.edx=lo(r.edx,0x100u);
        if(s32(d)<25000)r.edx=fx_sar(d,7);
        r.eax=fx_sar((uint32_t)fx_s16(r.eax),6);
        a[0]=A;a[1]=r.eax;a[2]=r.edx;CALL(0x416a04,4);
        return pair(r.eax,r.edx);
    }
    const uint32_t si=lo(d,R16(A+0x10u)-R16(L+0x10u));
    if((uint16_t)si==0x8000u)r.eax=lo(r.eax,0x7fffu);
    else if(fx_s16(si)<0)r.eax=lo(si,0u-si);
    else r.eax=si;
    if(fx_s16(r.eax)>=0x2000) {
        r.eax=lo(r.eax,0xff00u);
        if(!(fx_s16(si)>0))r.eax+=0x200u;
        a[0]=A;a[1]=r.eax;a[2]=0xffffff00u;CALL(0x416a04,4);
        return pair(r.eax,r.edx);
    }
    r.edx=R32(A+0x26u);W32(Q,r.edx);r.edx=L+0xcu;r.ecx=R32(A+0x2au);W32(Q+4u,r.ecx);r.eax=R32(A+0x2eu);W32(Q+8u,r.eax);
    r.ecx=L+0xcu;
    if(!R16(L+0x32u)) { r.eax=L+0xcu;r.edx=L+0x12u;r.ecx=L+0xcu;a[0]=L+0xcu;a[1]=L+0x12u;CALL(0x47ebb8,2);W16(L+0x32u,1); }
    for(unsigned i=0;i<4;++i)W16(M+2u*i,R16(L+0x12u+2u*i));
    W32(Q+8u,R32(Q+8u)-R32(T+8u));W32(Q,R32(Q)-R32(T));W32(Q+4u,R32(Q+4u)-R32(T+4u));
    { const uint16_t w1=R16(M+2u),w2=R16(M+4u);W16(M+4u,w1);W16(M+2u,w2); } /* 47df04 */
    am_rotate2(m,Q,M,R);                                                     /* 47ffe8 */
    r.eax=fx_sar((uint32_t)fx_s16(si),6);r.edx=fx_sar(0u-R32(R),5);r.eax=lo(r.eax,r.eax+r.edx);
    r.edx=lo(r.edx,R16(L+0x28eu));r.ecx=fx_sar(0u-R32(R+4u),5);r.edx=lo(r.edx,r.edx+r.ecx);r.eax=lo(r.eax,0u-r.eax);
    a[0]=A;a[1]=r.eax;a[2]=r.edx;CALL(0x416a04,4);
    return pair(r.eax,r.edx);
}
