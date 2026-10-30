#include "es2/core/ai_behaviour.h"
#include "fixed.h"
/* AI behaviours around the torso servos (docs/reverse-engineering/ai.md,
 * section "stack residue under aim_fire"). r holds what the original's
 * EAX/EDX/ECX hold at the next game call; lo() is a 16-bit load or 16-bit
 * arithmetic that keeps the register's high word. Memory is read again wherever
 * the original reads it again after a call. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* A DBSIM VA (globals, tables, pushed script tables, the RNG state). */
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)

static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes its resolved target. */
static void ab_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}
static void ab_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
#define RNG_STATE 0x4d2599u

/* Original: DBSIM.EXE 0x40e5f8 */
uint64_t es2_weapon_range_registers(const es2_guest_memory *m,uint32_t W,uint32_t d) {
    const uint32_t c=R32(W+0x1cu);
    const int inside=(int32_t)d>(int32_t)R32(c+0x2cu)&&(int32_t)d<(int32_t)R32(c+0x30u);
    return pair(inside?1u:0u,d);
}

/* weapon_choose's EBX/ESI/EDI, the address after the next CALL and the
 * temporary the original has pushed below its outgoing ESP. */
static void ab_push(es2_ai_push_frame *f,const es2_weapon_graph *g,uint32_t ebx,uint32_t esi,uint32_t edi,uint32_t ret,
    uint32_t pushed,uint32_t temp) {
    if(f) { f->call.ebx=ebx;f->call.esi=esi;f->call.edi=edi;f->call.ret=VA(ret);f->pushed=pushed;f->temp=temp; }
}

uint64_t es2_weapon_choose_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    uint32_t angle,uint32_t d,uint32_t T,const uint32_t entry[3]) {
    return es2_weapon_choose_frame_registers(m,g,NULL,A,angle,d,T,entry);
}

/* Original: DBSIM.EXE 0x41f358. Frame words kept in locals: best (EBP-2),
 * t34 (EBP-4), chosen (EBP-8), count (EBP-0Ah), none (EBP-0Ch), bound
 * (EBP-0Eh), i (EBP-10h), rank (EBP-14h), cost (EBP-18h); with a frame
 * they are also stored where the original stores them (F = its EBP). At
 * its calls the original holds EDI = A; EBX = T, then the list entry W;
 * ESI = the word A+2AAh, then W.+60h's EAX, the rank, the first draw and
 * the product of both draws. */
uint64_t es2_weapon_choose_frame_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_push_frame *frame,
    uint32_t A,uint32_t angle,uint32_t d,uint32_t T,const uint32_t entry[3]) {
    uint32_t a[5],vt;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    const uint32_t F=g->scratch+ES2_WEAPON_CHOOSE_SCRATCH,edi=A;
#define WF16(o,v) do { if(frame)W16(F-(o),(v)); } while(0)
#define WF32(o,v) do { if(frame)W32(F-(o),(v)); } while(0)
    uint32_t ebx=T,esi=sx(R16(A+0x2aau));
    a[0]=esi;a[1]=0;a[2]=0x400u;a[3]=0x96u;a[4]=0xffffff9cu;
    ab_push(frame,g,ebx,esi,edi,0x41f382u,0,0);ab_call(g,0x47df48u,a,5,&r);
    uint32_t best=r.eax&0xffffu;WF16(2u,best);
    r.eax=lo(r.eax,angle+0x4000u);
    r.edx=(uint16_t)r.eax<0x8000u?1u:0u;
    vt=R32(T);r.ecx=vt;
    a[0]=T;a[1]=r.edx;ab_push(frame,g,ebx,esi,edi,0x41f3a2u,0,0);ab_virtual(g,R32(vt+0x34u),0x34,a,2,&r);
    const uint32_t t34=r.eax&0xffffu;WF16(4u,t34);
    uint32_t chosen=0,none=1;WF32(8u,0);
    r.edx=R32(A+0x202u);r.ecx=lo(r.ecx,R16(r.edx+8u));
    const uint32_t count=r.ecx&0xffffu;WF16(0xau,count);WF16(0xcu,1u);
    r.edx=R32(VA(0x4a9b84u));
    r.eax=R32(A+0x45u);
    r.ecx=r.eax==R32(r.edx+0x45u)?1u:0u;
    r.eax=lo(r.eax,0x23u);
    if(R16(T+0x1a8u))r.eax-=5u;
    const uint32_t bound=r.eax&0xffffu;WF16(0xeu,bound);
    uint32_t i=0;WF16(0x10u,0);
    r.edx=lo(r.edx,0);
    while(fx_s16(i)<fx_s16(count)) {
        r.ecx=sx(i);r.eax=R32(A+0x202u);r.edx=R32(r.eax+4u);
        const uint32_t W=R32(r.edx+4u*r.ecx);
        ebx=W;
        if(W) {
            vt=R32(W);r.eax=vt;a[0]=W;ab_push(frame,g,ebx,esi,edi,0x41f41eu,0,0);ab_virtual(g,R32(vt+0x5cu),0x5c,a,1,&r);r.ecx=W;
            if(!(uint16_t)r.eax) {
                vt=R32(W);r.edx=vt;a[0]=W;a[1]=0;ab_push(frame,g,ebx,esi,edi,0x41f430u,0,0);ab_virtual(g,R32(vt+0x60u),0x60,a,2,&r);
                esi=r.eax;
                const uint32_t s=r.eax&0xffffu;none=0;WF16(0xcu,0);
                r.eax=d;a[0]=W;a[1]=d;ab_push(frame,g,ebx,esi,edi,0x41f445u,0,0);ab_call(g,0x40e5f8u,a,2,&r);
                const uint32_t k=r.eax&0xffffu;
                if(!s&&k) {
                    r.edx=A+0x92u;
                    if(!R8(A+0x96u)&&!R16(A+0x26bu))W8(A+0x96u,1);
                }
                int pass=1;
                if(s!=5u&&s!=3u) {
                    r.ecx=sx(s);r.edx=R32(A+0x202u);
                    if(!R16(r.edx+2u*r.ecx+0xau))pass=0;
                }
                if(pass&&k) {
                    vt=R32(W);r.eax=vt;a[0]=W;ab_push(frame,g,ebx,esi,edi,0x41f4a2u,0,0);ab_virtual(g,R32(vt+0x2cu),0x2c,a,1,&r);r.ecx=W;
                    if((uint16_t)r.eax) {
                        const uint32_t D=R32(W+0x20u);
                        uint32_t e;
                        r.eax=D;
                        if(!t34)e=sx(R16(D+6u));
                        else {
                            e=sx(R16(D+4u));r.edx=sx(t34);
                            if((int32_t)e>(int32_t)r.edx)e=sx(R16(D+6u))+10000u;
                        }
                        esi=e;WF32(0x14u,e);
                        r.eax=e;a[0]=100;a[1]=e;ab_push(frame,g,ebx,esi,edi,0x41f4e0u,0,0);ab_call(g,0x47e0b0u,a,2,&r);
                        const uint32_t X=r.eax;
                        r.edx=R32(W+0x1cu);r.ecx=sx(R16(r.edx+0x34u));r.eax=r.ecx;WF32(0x18u,r.ecx);
                        a[0]=1000;a[1]=r.ecx;ab_push(frame,g,ebx,esi,edi,0x41f4fcu,1,X);ab_call(g,0x47e0b0u,a,2,&r);
                        r.edx=X-r.eax;
                        const uint32_t Z=r.edx;
                        r.ecx=lo(r.ecx,bound);
                        a[0]=VA(RNG_STATE);a[1]=r.ecx;ab_push(frame,g,ebx,esi,edi,0x41f512u,1,Z);ab_call(g,0x492f24u,a,2,&r);
                        const uint32_t r1=sx(r.eax);
                        esi=r1;
                        r.eax=lo(r.eax,bound);
                        a[0]=VA(RNG_STATE);a[1]=r.eax;ab_push(frame,g,ebx,esi,edi,0x41f527u,1,Z);ab_call(g,0x492f24u,a,2,&r);
                        r.edx=sx(r.eax);
                        esi=r1*r.edx;
                        r.eax=Z+esi;
                        r.edx=sx(best);
                        if((int32_t)r.eax>(int32_t)r.edx) { best=r.eax&0xffffu;chosen=W; }
                        WF16(2u,best);WF32(8u,chosen);   /* 41f53b/41f53f, unchanged values otherwise */
                    }
                }
            }
        }
        i=(i+1u)&0xffffu;WF16(0x10u,i);r.ecx=lo(r.ecx,i);
    }
    if(none) {
        r.eax=A+0x92u;
        if(!R8(A+0xa5u)) {
            W8(A+0xa5u,1);
            r.eax=R32(A+0x1b6u);
            if(r.eax) {
                const uint32_t f=r.eax;a[0]=f;ab_push(frame,g,ebx,esi,edi,0x41f581u,0,0);ab_call(g,0x423430u,a,1,&r);r.ecx=f;
            }
            if(fx_s16(R16(VA(0x4a9e63u)))<1000)W16(VA(0x4a9e63u),1000u);
        }
    }
#undef WF16
#undef WF32
    return pair(chosen,r.edx);
}

/* EBX/ESI/EDI of the original at the next call and the address after its
 * CALL instruction. */
static void ab_frame(es2_ai_call_frame *f,const es2_weapon_graph *g,uint32_t ebx,uint32_t esi,uint32_t edi,
    uint32_t ret) {
    if(f) { f->ebx=ebx;f->esi=esi;f->edi=edi;f->ret=VA(ret); }
}

/* Original: DBSIM.EXE 0x41f5a0. Frame: L (EBP-10h) at scratch, k (EBP-2)
 * at scratch+0Eh. At its calls the original holds EBX = A; ESI = P until
 * the weapon is known, then W (0 when none); EDI = P, then d, then the
 * spread word over d's high word, then V. */
uint64_t es2_aim_fire_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t A,uint32_t P,uint32_t w,uint32_t T,const uint32_t entry[3]) {
    uint32_t a[4],vt,W=0;
    const uint32_t L=g->scratch;
    es2_weapon_registers r={A+0x26u,entry[1],entry[2]};
    a[0]=A+0x26u;a[1]=P;ab_frame(frame,g,A,P,P,0x41f5bbu);ab_call(g,0x4928d0u,a,2,&r);
    const uint32_t d=r.eax;
    uint32_t edi=d;
    r.eax=P;r.edx=R32(P);W32(L,r.edx);r.ecx=R32(P+4u);W32(L+4u,r.ecx);r.eax=R32(P+8u);W32(L+8u,r.eax);
    int kept=0;
    r.eax=R32(A+0x2acu);
    if(r.eax) {
        const uint32_t C=r.eax;
        vt=R32(C);r.edx=vt;a[0]=C;ab_frame(frame,g,A,P,edi,0x41f5e3u);ab_virtual(g,R32(vt+0x2cu),0x2c,a,1,&r);r.ecx=C;
        if((uint16_t)r.eax) {
            r.ecx=R32(A+0x2acu);a[0]=r.ecx;a[1]=d;ab_frame(frame,g,A,P,edi,0x41f5f6u);ab_call(g,0x40e5f8u,a,2,&r);
            if((uint16_t)r.eax) { W=R32(A+0x2acu);kept=1; }
        }
    }
    if(!kept) {
        r.edx=A+0x92u;r.eax=r.edx;
        if(R8(A+0xb5u)) W8(A+0xb5u,0);
        else {
            r.edx=T;r.ecx=lo(r.ecx,w);
            a[0]=A;a[1]=r.ecx;a[2]=d;a[3]=T;ab_frame(frame,g,A,P,edi,0x41f62eu);ab_call(g,0x41f358u,a,4,&r);
            W=r.eax;r.eax=0;W32(A+0x2acu,0);
        }
    }
    if(!W) {
        r.ecx=L;a[0]=A;a[1]=L;ab_frame(frame,g,A,0,edi,0x41f785u);ab_call(g,0x41ef14u,a,2,&r);
        return pair(r.eax,r.edx);
    }
    r.edx=R32(W+0x20u);r.ecx=lo(r.ecx,R16(r.edx+0xau));
    const uint32_t k=r.ecx&0xffffu;
    W16(L+0xeu,k);
    r.eax=T;vt=R32(T);r.edx=vt;a[0]=T;ab_frame(frame,g,A,W,edi,0x41f657u);ab_virtual(g,R32(vt+0x38u),0x38,a,1,&r);
    r.ecx=r.eax;
    if(fx_s16(k)>0&&(uint16_t)r.ecx) {
        const int32_t x=fx_s16(fx_sar(d,2)),y=fx_s16(r.ecx)*4;
        const int32_t low=(int32_t)(uint32_t)(uint64_t)((int64_t)x*y);
        const uint32_t q=sx((uint32_t)(low/fx_s16(k)));
        r.ecx=T;r.eax=lo(q,R16(T+0x10u));r.edx=L;
        a[0]=L;a[1]=r.eax;a[2]=q;ab_frame(frame,g,A,W,edi,0x41f692u);ab_call(g,0x4929fcu,a,3,&r);
    }
    r.ecx=R32(A+0x45u);
    if(R8(r.ecx+0x12u)) {
        r.eax=sx(R16(VA(0x4a9e5cu)));
        const uint32_t z=lo(d,R16(VA(0x49a30cu)+2u*r.eax));
        edi=z;
        if((uint16_t)z) {
            a[0]=VA(RNG_STATE);a[1]=z;ab_frame(frame,g,A,W,edi,0x41f6bdu);ab_call(g,0x492f24u,a,2,&r);
            r.eax=sx(r.eax);W32(L,R32(L)+r.eax);
            ab_frame(frame,g,A,W,edi,0x41f6d1u);ab_call(g,0x492f24u,a,2,&r);
            r.edx=sx(r.eax);W32(L+4u,R32(L+4u)+r.edx);
            ab_frame(frame,g,A,W,edi,0x41f6e5u);ab_call(g,0x492f24u,a,2,&r);
            r.ecx=sx(r.eax);W32(L+8u,R32(L+8u)+r.ecx);
        }
    }
    r.eax=L;a[0]=A;a[1]=L;ab_frame(frame,g,A,W,edi,0x41f6f8u);ab_call(g,0x41ef14u,a,2,&r);
    const uint32_t V=r.eax;
    vt=R32(W);r.eax=vt;a[0]=W;a[1]=0;ab_frame(frame,g,A,W,V,0x41f705u);ab_virtual(g,R32(vt+0x60u),0x60,a,2,&r);
    if((uint16_t)r.eax!=5u)r.eax=1;
    else {
        r.eax=0;
        r.edx=lo(r.edx,R16(V)+0x3e8u);
        r.ecx=(uint16_t)r.edx<0x7d0u?1u:0u;
        if(!r.ecx)return pair(r.eax,r.edx);
        r.eax=(R16(V+4u)+0x3e8u)&0xffffu;
        r.edx=r.eax<0x7d0u?1u:0u;
        if(!r.edx)return pair(r.eax,r.edx);
    }
    vt=R32(W);r.ecx=vt;a[0]=W;a[1]=A;a[2]=0;ab_frame(frame,g,A,W,V,0x41f74fu);ab_virtual(g,R32(vt+0x28u),0x28,a,3,&r);
    r.eax=R32(W+0x1cu);r.eax=lo(r.eax,R16(r.eax+0x56u));
    r.edx=(uint16_t)r.eax==6u||(uint16_t)r.eax==0x16u?1u:0u;
    if(r.edx)W32(A+0x2acu,W);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41e984. Frame: the aim point Q (EBP-1Ch) at
 * scratch. */
uint64_t es2_aim_target_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    uint32_t w,uint32_t T) {
    uint32_t a[4],vt;
    const uint32_t Q=g->scratch,c=R16(R32(A+0x1f2u)+0x22u);
    es2_weapon_registers r={T+0x26u,T+0x26u,A+0x26u};
    a[0]=T+0x26u;a[1]=A+0x26u;ab_call(g,0x492934u,a,2,&r);
    r.eax=lo(r.eax,r.eax-R16(A+0x10u));
    r.edx=lo(r.edx,r.eax+c);
    r.ecx=(uint16_t)r.edx<(uint16_t)(2u*c)?1u:0u;
    if(!r.ecx) {
        a[0]=A;a[1]=0;ab_call(g,0x41e8d4u,a,2,&r);
        return pair(r.eax,r.edx);
    }
    r.eax=A+0x26u;a[0]=A+0x26u;a[1]=T+0x26u;ab_call(g,0x49288cu,a,2,&r);
    const uint32_t d=r.eax;
    r.edx=lo(r.edx,R16(A+0x2a2u));
    int part=0;
    if(fx_s16(r.edx)>-1) { r.eax=d+0xffffec78u;part=r.eax<0x3a98u; }
    if(part) {
        r.ecx=Q;vt=R32(T);a[0]=T;a[1]=r.edx;a[2]=Q;r.edx=vt;ab_virtual(g,R32(vt+0x58u),0x58,a,3,&r);
    } else {
        if(!R16(T+0x32u)) {
            r.eax=T+0x12u;r.edx=T+0xcu;a[0]=T+0xcu;a[1]=T+0x12u;ab_call(g,0x47ebb8u,a,2,&r);
            W16(T+0x32u,1);
        }
        vt=R32(T);r.eax=vt;a[0]=T;ab_virtual(g,R32(vt+0x24u),0x24,a,1,&r);r.ecx=T;
        r.eax+=0x14u;r.edx=Q;a[0]=r.eax;a[1]=T+0x12u;a[2]=Q;ab_call(g,0x48043cu,a,3,&r);
    }
    r.eax=Q;r.ecx=lo(r.ecx,w);
    a[0]=A;a[1]=Q;a[2]=r.ecx;a[3]=T;ab_call(g,0x41f5a0u,a,4,&r);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41e0e0 */
uint64_t es2_target_search_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    uint32_t P) {
    uint32_t a[3],best=0,cursor=0,score=0,rank=0;
    const uint32_t G=R32(A+0x45u),order=R32(G+4u*R32(G+0x6cu)+0x44u);
    const uint32_t keep=R32(order+0xeu)?1u:0u;
    const uint32_t range=keep?90000u:90000u+0xd9490u;
    W16(A+0x5bu,10000u);
    es2_weapon_registers r={keep,A+0x5au,range};
    for(;;) {
        r.eax=R32(VA(0x4a9b7au));a[0]=r.eax;a[1]=cursor;ab_call(g,0x471c70u,a,2,&r);
        const uint32_t E=cursor=r.eax;
        if(!E)break;
        a[0]=A;a[1]=E;a[2]=0;ab_call(g,0x411e80u,a,3,&r);
        if(!(uint16_t)r.eax)continue;
        r.eax=E+0x92u;
        if(R8(E+0xa4u))continue;
        if(R8(E+0xa5u))r.edx=1;
        else { r.edx=E+0x92u;r.eax=R8(E+0x99u)?1u:0u;r.edx=r.eax; }
        r.ecx=r.edx?0u:1u;
        const uint32_t cls=r.ecx;
        if(fx_s16(cls)<fx_s16(rank))continue;
        r.eax=P;r.edx=P;r.ecx=E+0x26u;a[0]=E+0x26u;a[1]=P;ab_call(g,0x4928d0u,a,2,&r);
        r.ecx=range-r.eax;
        if((int32_t)r.ecx<=0)continue;
        if(R16(E+0x1a2u)) {
            const int32_t n=fx_s16(R16(E+0x1a2u))+1,x=(int32_t)r.ecx;
            r.eax=(uint32_t)(x/n);r.edx=(uint32_t)(x%n);r.ecx=r.eax;
        }
        if((int32_t)r.ecx>(int32_t)score||fx_s16(cls)>fx_s16(rank)) { score=r.ecx;rank=cls;best=E; }
    }
    return pair(best,r.edx);
}

/* Original: DBSIM.EXE 0x41e224 */
uint64_t es2_hunt_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    uint32_t a[4],F=1,N=0,t;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    a[0]=A;ab_call(g,0x41dbccu,a,1,&r);
    const uint32_t D=r.eax,B=A+0x5au;
    if(R32(A+0x1a4u)) { /* drop the target */
        t=R32(A+0x1a4u);W16(t+0x1a2u,R16(t+0x1a2u)-1u);
        W32(A+0x1a4u,0);W8(A+0x9du,1);
    }
    r.eax=R32(A+0x45u);r.edx=R32(r.eax+0xcu);r.ecx=R32(r.edx);
    if(r.ecx==R32(VA(0x4a9b84u))&&!R16(A+0x23eu)) { r.eax=0;F=0; }
    int search=1;
    if(!F) { r.eax=A+0x92u;if(!R8(A+0xb6u))search=0; }
    if(search) {
        r.edx=B;a[0]=B;ab_call(g,0x467a48u,a,1,&r);
        r.ecx=(uint16_t)r.eax?0u:1u;
        if(r.ecx) {
            if(R16(A+0x23eu)==4u&&R32(A+0x248u))N=R32(A+0x248u);
            else { a[0]=A;a[1]=D;ab_call(g,0x41e0e0u,a,2,&r);N=r.eax; }
        }
    }
    if(N) {
        const uint32_t old=R32(A+0x1a4u);
        if(N!=old) {
            if(old) { r.ecx=R32(A+0x1a4u);W16(r.ecx+0x1a2u,R16(r.ecx+0x1a2u)-1u); }
            W32(A+0x1a4u,N);t=R32(A+0x1a4u);W16(t+0x1a2u,R16(t+0x1a2u)+1u);
            W8(A+0x9du,1);
        }
        r.eax=A+0x92u;
        if(R8(A+0xa5u))r.edx=1;
        else { r.eax=R8(A+0xa4u)||R8(A+0x99u)?1u:0u;r.edx=r.eax; }
        if(r.edx) { /* A cannot fight: the retreat script */
            r.ecx=A+0x4du;a[0]=A+0x4du;a[1]=VA(0x499800u);ab_call(g,0x413e50u,a,2,&r);
            W16(A+0x2a2u,0xffffu);
            return pair(0,r.edx);
        }
        r.eax=A+0x4du;a[0]=A+0x4du;a[1]=VA(0x499784u);ab_call(g,0x413e50u,a,2,&r);
        a[0]=A;ab_call(g,0x41ce08u,a,1,&r);
        r.edx=R32(A+0x45u);t=R32(R32(r.edx+0xcu))+0x92u;
        if(!R8(t+0x11u)||R8(A+0xb2u))W8(A+0x96u,1);
        return pair(0,r.edx);
    }
    if(F) {
        r.edx=A+0x26u;a[0]=A+0x26u;a[1]=D;ab_call(g,0x4928d0u,a,2,&r);
        const uint32_t dist=r.eax;
        a[0]=A;a[1]=0;ab_call(g,0x41e8d4u,a,2,&r);
        a[0]=A;ab_call(g,0x41c3c8u,a,1,&r);r.ecx=A;
        if((int32_t)dist>30000) {
            a[0]=A;a[1]=D;ab_call(g,0x41fac4u,a,2,&r);
            return pair(0,r.edx);
        }
        r.ecx=A+0x26u;r.eax=D;a[0]=D;a[1]=A+0x26u;ab_call(g,0x492934u,a,2,&r);
        r.eax=lo(r.eax,r.eax-R16(A+0x10u)+0x8000u);
        r.edx=lo(r.edx,0x100u);
        if((int32_t)dist>=10000)r.edx+=0xffffff00u;
        r.ecx=fx_sar(sx(r.eax),6);
        a[0]=A;a[1]=r.ecx;a[2]=r.edx;a[3]=0;ab_call(g,0x416a04u,a,4,&r);
        return pair(0,r.edx);
    }
    a[0]=A;ab_call(g,0x41fbb8u,a,1,&r);r.ecx=A;
    ab_call(g,0x41c3c8u,a,1,&r);r.ecx=A;
    a[1]=0;ab_call(g,0x41e8d4u,a,2,&r);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x41dc24. Frame at scratch (EBP = scratch+2Ch):
 * O = EBP-2Ch, E = EBP-20h, S = EBP-14h, H = EBP-8. */
uint64_t es2_line_blocked_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A) {
    uint32_t a[6],vt;
    const uint32_t O=g->scratch,E=O+0xcu,S=O+0x18u,H=O+0x24u;
    W32(S,R32(A+0x26u));W32(S+4u,R32(A+0x2au));W32(S+8u,R32(A+0x2eu));
    const uint32_t T=R32(A+0x1a4u);
    W32(E,R32(T+0x26u));W32(E+4u,R32(T+0x2au));W32(E+8u,R32(T+0x2eu));
    es2_weapon_registers r={R32(T+0x2eu),0,R32(T+0x2au)};
    vt=R32(A);r.edx=vt;a[0]=A;ab_virtual(g,R32(vt+0x24u),0x24,a,1,&r);
    const uint32_t R1=r.eax;
    r.eax=R32(A+0x1a4u);r.ecx=A;
    const uint32_t T2=r.eax;
    vt=R32(T2);r.edx=vt;a[0]=T2;ab_virtual(g,R32(vt+0x24u),0x24,a,1,&r);r.ecx=T2;
    r.ecx=R1?R32(R1+0x1cu):500u;W32(S+8u,R32(S+8u)+r.ecx);
    r.eax=r.eax?R32(r.eax+0x1cu):500u;W32(E+8u,R32(E+8u)+r.eax);
    r.eax=S;r.ecx=E;r.edx=R32(VA(0x4a0be0u));
    a[0]=r.edx;a[1]=S;a[2]=E;a[3]=0;a[4]=O;a[5]=1;ab_call(g,0x46e980u,a,6,&r);
    const uint32_t ground=r.eax&0xffu;
    r.eax=E;r.edx=S;r.ecx=H;
    a[0]=H;a[1]=S;a[2]=E;a[3]=0;a[4]=0xffffffffu;ab_call(g,0x404ca0u,a,5,&r);
    if((uint16_t)r.eax) {
        const uint32_t t=R32(A+0x1a4u);
        r.eax=t?t+0xcu:t;
        if(r.eax!=R32(H+4u))return pair(lo(r.eax,1),r.edx);
    }
    r.eax=H;r.edx=E;r.ecx=S;
    a[0]=H;a[1]=S;a[2]=E;a[3]=0;ab_call(g,0x4280f4u,a,4,&r);
    if((uint16_t)r.eax) {
        r.eax=lo(r.eax,1);
        if(!ground)r.eax+=1u;
        return pair(r.eax,r.edx);
    }
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x41dd64. Frame: Q (EBP-0Ch) at scratch; B =
 * A+5Ah. */
uint64_t es2_clear_shot_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A) {
    uint32_t a[4];
    const uint32_t Q=g->scratch,B=A+0x5au;
    W32(Q,R32(A+0x31eu));W32(Q+4u,R32(A+0x322u));W32(Q+8u,R32(A+0x326u));
    es2_weapon_registers r={R32(A+0x326u),Q,A+0x26u};
    a[0]=A+0x26u;a[1]=Q;ab_call(g,0x492934u,a,2,&r);
    uint32_t h=r.eax;
    if(!R16(B+4u)) { /* first run: the side, the timer and the state */
        r.eax=A+0x26u;r.edx=Q;a[0]=Q;a[1]=A+0x26u;ab_call(g,0x492934u,a,2,&r);
        r.eax=lo(r.eax,r.eax-R16(A+0x10u));
        r.ecx=fx_s16(r.eax)<0?1u:0u;
        W8(B+6u,r.ecx);W16(B+4u,1u);W32(B+9u,5000u);W8(B+7u,1);
    }
    r.eax=B+8u;a[0]=B+8u;ab_call(g,0x467aa8u,a,1,&r);r.ecx=B+8u;
    r.edx=r.eax?0u:1u;
    if(r.edx) {
        W32(B+9u,5000u);
        a[0]=A;ab_call(g,0x41dc24u,a,1,&r);r.ecx=A;
        W8(B+7u,r.eax);
        if(!R8(B+7u)) {
            r.eax=R32(B);r.edx=A+0x4du;a[0]=A+0x4du;a[1]=r.eax;ab_call(g,0x413e50u,a,2,&r);
            r.eax=A+0x92u;W8(A+0xadu,0);
        } else {
            r.edx=Q;r.ecx=A+0x26u;a[0]=A+0x26u;a[1]=Q;ab_call(g,0x4928d0u,a,2,&r);
            W32(B+0xdu,r.eax);
        }
    }
    if(R8(B+7u)==1u) {
        r.eax=R32(B+0xdu);
        r.edx=lo(r.edx,0xc000u);
        if(!R8(B+6u))r.edx+=0x8000u;
        h=lo(h,h+r.edx);
        r.ecx=Q;a[0]=Q;a[1]=h;a[2]=r.eax;ab_call(g,0x4929fcu,a,3,&r);
    }
    r.eax=A+0x26u;r.edx=Q;a[0]=Q;a[1]=A+0x26u;ab_call(g,0x492934u,a,2,&r);
    r.eax=fx_sar(sx(r.eax-R16(A+0x10u)),6);
    a[0]=A;a[1]=r.eax;a[2]=0x100u;a[3]=0;ab_call(g,0x416a04u,a,4,&r);
    a[0]=A;a[1]=0;ab_call(g,0x41e8d4u,a,2,&r);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x41d598 */
uint64_t es2_group_move_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    uint32_t a[2];
    es2_weapon_registers r={0,R32(R32(A+0x45u)+0xcu),entry[2]};
    r.eax=R32(r.edx);
    if(R16(A+0x23eu)) { r.edx=A+0x240u;a[0]=A;a[1]=A+0x240u;ab_call(g,0x41fac4u,a,2,&r); }
    else if(A==r.eax) { a[0]=A;ab_call(g,0x41fb60u,a,1,&r);r.ecx=A; }
    else { a[0]=A;ab_call(g,0x41fbb8u,a,1,&r);r.ecx=A; }
    a[0]=A;ab_call(g,0x41c3c8u,a,1,&r);r.ecx=A;
    a[1]=0;ab_call(g,0x41e8d4u,a,2,&r);
    return pair(r.eax,r.edx);
}
