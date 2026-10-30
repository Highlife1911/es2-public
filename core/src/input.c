#include "es2/core/input.h"
#include "fixed.h"

/* Children of the input dispatcher 460870: fire_selected 410dbc, fire_line
 * 415608, aim_point 41b728 and leg_input 4160dc.
 * (docs/reverse-engineering/input.md). The register struct r follows
 * EAX/EDX/ECX through every path so that each game call sees the original's
 * entry registers; lo() is a 16-bit load or operation that keeps the register's
 * high word. */
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
static int32_t i32(uint32_t x) { return (int32_t)fx_s32(x); }
static int32_t s16(uint32_t x) { return fx_s16(x); }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static void in_call(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
                    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* A direct call names the original VA; a virtual call passes the target it
 * read from the receiver's vtable together with the slot offset. */
#define CALL(va,n) in_call(g,g->image+(va)-0x400000u,0,a,n,&r)
#define VCALL(target,slot,n) in_call(g,(target),(slot),a,n,&r)
#define GLOBAL(va) (g->image+(va)-0x400000u)

/* Original: DBSIM.EXE 0x410dbc. S = the selected weapon list[s8 W+1Dh],
 * L = its linked partner when byte S+4Bh is set. The fire flag F (word
 * 4a9e52 = 0 and word 4a9e58 = 1) is passed in the low word of the third
 * +28h slot over the high word of the preceding call's EAX. ECX at the
 * return equals the result (the epilogue pops the result local into ECX). */
uint64_t es2_fire_selected_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,uint32_t R,
    uint32_t P,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    const uint32_t b=R8(W+0x1du);
    r.eax=b;r.edx=sx8(b)+1u;
    if(i32(r.edx)<=0)return pair(0,r.edx);
    r.ecx=sx8(b);r.eax=r.ecx;r.edx=R32(W+4u);
    const uint32_t S=R32(r.edx+4u*r.eax);
    uint32_t L=0;
    r.eax=R16(GLOBAL(0x4a9e52u))==0;
    r.edx=(uint16_t)r.eax&&R16(GLOBAL(0x4a9e58u))==1;
    const uint16_t flag=(uint16_t)r.edx;
    r.eax=S+0x43u;
    if(R8(S+0x4bu)) {
        r.edx=R32(S+0x18u);r.eax=sx8(R8(r.edx+0x16u));r.edx=sx8(R8(W+0x1du));
        r.eax=lo(r.eax,r.eax+r.edx);r.ecx=sx(r.eax);r.eax=R32(W+4u);L=R32(r.eax+4u*r.ecx);
    }
    a[0]=S;r.eax=R32(S);VCALL(R32(r.eax+0x2cu),0x2c,1);r.ecx=S;
    if(!(uint16_t)r.eax)return pair(0,r.edx);
    if(L) {
        a[0]=L;r.edx=R32(L);VCALL(R32(r.edx+0x2cu),0x2c,1);r.ecx=L;
        if(!(uint16_t)r.eax)return pair(0,r.edx);
    }
    r.ecx=R;a[0]=S;a[1]=R;r.eax=R32(S);VCALL(R32(r.eax+0x30u),0x30,2);
    if(!(uint16_t)r.eax)return pair(0,r.edx);
    if(L) {
        r.edx=R;a[0]=L;a[1]=R;r.ecx=R32(L);VCALL(R32(r.ecx+0x30u),0x30,2);
        if(!(uint16_t)r.eax)return pair(0,r.edx);
    }
    r.eax=lo(r.eax,flag);r.edx=P;a[0]=S;a[1]=P;a[2]=r.eax;r.ecx=R32(S);VCALL(R32(r.ecx+0x28u),0x28,3);
    a[0]=S;r.eax=R32(S);VCALL(R32(r.eax+0x2cu),0x2c,1);r.ecx=S;
    if(!(uint16_t)r.eax) { r.eax=W+0x14u;W8(W+0x18u,0); }
    a[0]=S;a[1]=0;r.edx=R32(S);VCALL(R32(r.edx+0x60u),0x60,2);
    if((uint16_t)r.eax==3) { r.ecx=lo(r.ecx,1);W16(GLOBAL(0x4d2528u),1);W16(GLOBAL(0x4d2526u),1); }
    if(!L)return pair(1,r.edx);
    r.eax=lo(r.eax,flag);a[0]=L;a[1]=P;a[2]=r.eax;r.edx=P;r.eax=R32(L);VCALL(R32(r.eax+0x28u),0x28,3);
    a[0]=S;r.edx=R32(S);VCALL(R32(r.edx+0x2cu),0x2c,1);r.ecx=S;
    if(!(uint16_t)r.eax) { r.eax=W+0x14u;W8(W+0x18u,0); }
    return pair(1,r.edx);
}

/* 47eb30: the heading cosine of a word angle as the original rounds it,
 * (x + 8) >> 4 by SAR/ADC, folded by the quarter sine table at 4a25c4. */
static uint32_t in_cosine(const es2_guest_memory *m,const es2_weapon_graph *g,uint16_t angle) {
    int32_t index=(int32_t)fx_sar(sx(angle)+8u,4);
    if(index<0)index=-index;
    if(index>=0x400)return 0u-R16(GLOBAL(0x4a25c4u)+2u*(uint32_t)(0x800-index));
    return R16(GLOBAL(0x4a25c4u)+2u*(uint32_t)index);
}
/* One output dword of 47ffe8: x * word a + y * word b + 2000h, bits 14..45;
 * *high receives the sum's high dword (the EDX it leaves). */
static uint32_t in_rotate(uint32_t x,uint32_t y,uint32_t wa,uint32_t wb,uint32_t *high) {
    const uint64_t sum=(uint64_t)(fx_s32(x)*fx_s16(wa))+(uint64_t)(fx_s32(y)*fx_s16(wb))+0x2000u;
    *high=(uint32_t)(sum>>32);return (uint32_t)(sum>>14);
}

/* Original: DBSIM.EXE 0x415608. The local vector (0, 1000h), the rotation
 * words and the running point live in C variables: no game function
 * receives their frame addresses (47eb30/47ffe8 are computed here). */
uint64_t es2_fire_line_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t P,uint32_t R,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    const uint32_t count=GLOBAL(0x4a9cc8u),line=GLOBAL(0x4a9b88u);
    r.eax=R;r.edx=R32(P+0x202u);a[0]=r.edx;a[1]=R;a[2]=P;CALL(0x410dbc,3);
    if(!r.eax) { W32(count,0);return pair(0,r.edx); }
    if(R32(P+0x1a4u)) {
        const uint32_t T=R32(P+0x1a4u);
        r.eax=P+0x26u;a[0]=r.eax;a[1]=T+0x26u;CALL(0x49288c,2);
        W32(count,fx_sar(r.eax,12));
        if(i32(R32(count))>40)W32(count,40);
        if(i32(R32(count))<1)W32(count,1);
    } else W32(count,20);
    uint32_t x=R32(P+0x26u),y=R32(P+0x2au);
    r.edx=x;r.ecx=y;a[0]=P;r.eax=R32(P);VCALL(R32(r.eax+0x3cu),0x3c,1);
    const uint16_t angle=(uint16_t)(R16(P+0x10u)-r.eax);
    /* 47eb30(angle, matrix): words cos, sin, -sin, cos. */
    const uint32_t c=in_cosine(m,g,angle),s=in_cosine(m,g,(uint16_t)(angle-0x4000u));
    const uint16_t m0=(uint16_t)c,m1=(uint16_t)s,m2=(uint16_t)(0u-s),m3=(uint16_t)c;
    /* 47ffe8((0, 1000h), matrix, step). */
    uint32_t high;
    const uint32_t dx=in_rotate(0,0x1000u,m0,m2,&high),dy=in_rotate(0,0x1000u,m1,m3,&high);
    r.edx=high;
    uint32_t i=0;
    for(;i32(i)<i32(R32(count));++i) {
        x+=dx;y+=dy;r.edx=x;W32(line+8u*i,x);r.ecx=y;W32(line+8u*i+4u,y);
    }
    return pair(i,r.edx);
}

/* Original: DBSIM.EXE 0x41b728. T = [P+1A4h] is reread at each use as in
 * the original. The weapon path (dword P+30Bh set and the target closer
 * than 30,000) returns 40e4dc's registers and leaves *p3 alone. */
uint64_t es2_aim_point_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t P,uint32_t out,
    uint32_t p3,uint32_t p4,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[6];
    if(R32(P+0x30bu)) {
        const uint32_t T=R32(P+0x1a4u);
        r.eax=P+0x26u;a[0]=r.eax;a[1]=T+0x26u;CALL(0x49288c,2);
        if(i32(r.eax)<30000) {
            r.eax=R32(P+0x20eu);r.edx=R32(P+0x1a4u);r.ecx=R32(P+0x30bu);
            a[0]=r.ecx;a[1]=r.edx;a[2]=out;a[3]=r.eax;a[4]=p3;a[5]=p4;CALL(0x40e4dc,6);
            return pair(r.eax,r.edx);
        }
    }
    const uint32_t T=R32(P+0x1a4u);
    r.eax=T;r.edx=R32(T);a[0]=T;VCALL(R32(r.edx+0x24u),0x24,1);r.ecx=T;
    const uint32_t node=r.eax;
    if(node) {
        const uint32_t B=R32(P+0x1a4u)+0xcu;r.eax=B;
        if(!R16(B+0x26u)) {
            r.edx=B+6u;r.ecx=B;a[0]=B;a[1]=B+6u;CALL(0x47ebb8,2);
            W16(B+0x26u,1);
        }
        r.eax=B+6u;r.edx=node+0x14u;a[0]=r.edx;a[1]=r.eax;a[2]=out;CALL(0x48043c,3);
    } else {
        const uint32_t p=R32(P+0x1a4u)+0x26u;
        r.edx=R32(p+8u);W32(out+8u,r.edx);r.ecx=R32(p);W32(out,r.ecx);r.eax=R32(p+4u);W32(out+4u,r.eax);
    }
    if(p3) { r.edx=p3;W16(p3,0); }
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4160dc. turn (EDI) and throttle (ESI) keep their
 * high words; the NEGs and the set point load touch only the low words. */
uint64_t es2_leg_input_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t P,uint32_t turn,
    uint32_t throttle,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[4];
    const uint32_t lever=GLOBAL(0x49a06eu),set=P+0x290u;
    uint32_t di=turn,si=throttle;
    if(!R16(GLOBAL(0x4d24f0u))&&R16(lever)) {
        /* Lever: the set point follows |throttle - 256| * 2 directly. */
        r.ecx=sx(si)+0xffffff00u;r.edx=i32(r.ecx)<0?0xffffffffu:0;
        r.eax=((r.ecx^r.edx)-r.edx)*2u;
        if(i32(r.eax)<100)r.eax=0;
        else if(s16(R16(lever))<0)r.eax=0u-r.eax;
        W16(set,r.eax);r.eax=P+0x92u;si=0;W8(P+0x93u,1);
    }
    if(s16(R16(lever))<0)si=lo(si,0u-si);
    if((uint16_t)si==0x8000u)r.eax=lo(r.eax,0x7fff);
    else if(s16(si)<0)r.eax=lo(si,0u-si);
    else r.eax=si;
    if(s16(r.eax)>60) { if(s16(si)<=0)di=lo(di,0u-di); }
    else if(s16(R16(set))>=0)di=lo(di,0u-di);
    r.edx=0u-sx(si);r.ecx=r.edx;a[0]=0x91;a[1]=r.ecx;CALL(0x47e0a0,2);
    if((uint16_t)r.eax) {
        r.edx=P+0x92u;W8(P+0x93u,1);
        r.eax=lo(r.eax,r.eax+R16(set));si=lo(si,R16(set));
        int clamp=1;
        if((uint16_t)si) {
            r.edx=s16(r.eax)<0;r.ecx=s16(si)<0;
            if(r.edx!=r.ecx) { W16(set,0);clamp=0; }
        }
        if(clamp) {
            r.edx=lo(r.edx,0x400);
            if(s16(R16(lever))<0)r.edx+=0xfffffc00u;
            r.ecx=lo(r.ecx,0xfc00);
            if(s16(R16(lever))>0)r.ecx+=0x400u;
            if(s16(r.edx)>s16(r.eax))r.edx=s16(r.ecx)<s16(r.eax)?r.eax:r.ecx;
            W16(set,r.edx);
        }
    }
    r.eax=R32(P+0x1f2u);
    si=s16(R16(set))<0?lo(si,0u-R16(r.eax+4u)):lo(si,R16(r.eax+6u));
    r.eax=sx(si);r.edx=sx(R16(set));r.ecx=r.edx;a[0]=r.eax;a[1]=r.ecx;CALL(0x47e0b0,2);
    a[0]=P;a[1]=di;a[2]=r.eax;a[3]=0;CALL(0x416a04,4);
    return pair(r.eax,r.edx);
}

/* (docs/reverse-engineering/input.md, section "the targeting
 * computer 40e4dc and 40e4ac"). W = the TARG equipment object [P+30Bh],
 * T = the target. */

/* Original: DBSIM.EXE 0x40e4ac. With a target, the part index word W+7Dh
 * becomes the target's next living part after it (virtual +80h with the
 * sign-extended index and W+86h). */
uint64_t es2_targ_next_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,uint32_t T,
    const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    if(T) {
        r.eax=W+0x86u;r.edx=sx(R16(W+0x7du));r.ecx=R32(T);
        a[0]=T;a[1]=r.edx;a[2]=r.eax;VCALL(R32(r.ecx+0x80u),0x80,3);
        W16(W+0x7du,r.eax);
    }
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40e4dc (W, T, out, slot 4 never read, p5, p6).
 * A locked part (W+7Dh >= 0) on a working computer (W+7Fh < 155): the
 * part position (virtual +58h) after a liveness check (+84h) that moves
 * the lock on (40e4ac) when the part is gone; *p6 = the part word, a
 * damaged computer (W+7Fh > 104) loses the lock when its timer W+81h runs
 * out (W+82h = 5000, W+7Dh = -1), *p5 = 1. Otherwise the aim point of
 * aim_point's fallback (node through the pose or the position) and
 * *p5 = 0. ECX at the return is the pushed entry ECX (the node path: out),
 * restored by the framework; dead at the only caller (41b779, whose
 * epilogue pops its own local into ECX). */
uint64_t es2_targ_aim_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,uint32_t T,
    uint32_t out,uint32_t p5,uint32_t p6,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[3];
    if(s16(R16(W+0x7du))>-1&&s16(R16(W+0x7fu))<0x9b) {
        r.eax=R32(W+0x86u);r.edx=R32(T);a[0]=T;a[1]=r.eax;VCALL(R32(r.edx+0x84u),0x84,2);
        if(!r.eax) { a[0]=W;a[1]=T;CALL(0x40e4ac,2); }
        r.ecx=lo(r.ecx,R16(W+0x86u));r.eax=R32(T);a[0]=T;a[1]=r.ecx;a[2]=out;VCALL(R32(r.eax+0x58u),0x58,3);
        if(p6) { r.edx=p6;r.ecx=lo(r.ecx,R16(W+0x86u));W16(p6,r.ecx); }
        if(s16(R16(W+0x7fu))>0x68) {
            r.eax=W+0x81u;a[0]=r.eax;CALL(0x467aa8,1);r.ecx=a[0];
            r.edx=r.eax?0u:1u;
            if(r.edx) { W32(W+0x82u,5000);W16(W+0x7du,0xffffu); }
        }
        if(p5) { r.ecx=p5;W16(p5,1); }
        return pair(r.eax,r.edx);
    }
    r.eax=R32(T);a[0]=T;VCALL(R32(r.eax+0x24u),0x24,1);r.ecx=T;
    const uint32_t node=r.eax;
    if(node) {
        const uint32_t B=T+0xcu;
        if(!R16(B+0x26u)) { r.eax=B+6u;a[0]=B;a[1]=B+6u;CALL(0x47ebb8,2);W16(B+0x26u,1); }
        r.eax=B+6u;r.edx=node+0x14u;r.ecx=out;a[0]=r.edx;a[1]=r.eax;a[2]=out;CALL(0x48043c,3);
    } else {
        const uint32_t p=T+0x26u;
        r.edx=R32(p+8u);W32(out+8u,r.edx);r.ecx=R32(p);W32(out,r.ecx);r.eax=R32(p+4u);W32(out+4u,r.eax);
    }
    if(p5) { r.edx=p5;W16(p5,0); }
    return pair(r.eax,r.edx);
}
