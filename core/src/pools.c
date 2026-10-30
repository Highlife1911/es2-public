#include "es2/core/pools.h"
#include "fixed.h"
/* Pool updates (docs/reverse-engineering/main_loop.md, section
 * "remaining logic-frame pool updates"). r always holds what the original's
 * EAX/EDX/ECX hold at the next game call; every call goes through g->call. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(b,x) ((b)+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t low_word(uint32_t high,uint32_t low) { return (high&0xffff0000u)|(uint16_t)low; }
static uint32_t sx16(uint32_t v) { return (uint32_t)fx_s16(v); }
static uint32_t sx8(uint32_t v) { v&=0xffu; return v<0x80u?v:v|0xffffff00u; }
/* Direct calls name the original VA (the observer gets it, the bridge the
 * address in this image); a virtual call passes its resolved target with
 * the vtable offset as slot. Registers are updated in place. */
static void pool_call(const es2_weapon_graph *g,uint32_t function,unsigned slot,
    const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,function,slot);
    g->call(g->context,slot?function:VA(g->image,function),a,n,r);
}
/* The runtime's construction count: the dword [0x6cbee4] points to. The
 * pointer is reread at every change, as the original does. */
static uint32_t pool_count(const es2_guest_memory *m,uint32_t b,uint32_t delta) {
    uint32_t counter=R32(VA(b,0x6cbee4u));
    W32(counter,R32(counter)+delta);
    return counter;
}

/* Original: DBSIM.EXE 0x4091cc */
uint64_t es2_smoke_ball_update_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    (void)entry;
    uint32_t a[1],z=sx16(R16(o+0x45u));W32(o+0x2eu,R32(o+0x2eu)+z);
    uint32_t x=sx16(R16(o+0x41u));W32(o+0x26u,R32(o+0x26u)+x);
    uint32_t y=sx16(R16(o+0x43u));W32(o+0x2au,R32(o+0x2au)+y);
    es2_weapon_registers r={o+0x47u,y,x};
    a[0]=o+0x47u;pool_call(g,0x467aa8u,0,a,1,&r);
    if(r.eax)return pair(0,r.edx);
    W32(o+0x48u,0xc8u);
    uint32_t shape=R32(o+0x34u),frames=R32(shape+8u);
    uint32_t next=R16(frames)+1u,count=R16(R32(R32(shape+4u)+0x20u));
    uint32_t quotient=next/count,rest=next%count;
    W16(frames,rest);
    return pair(rest?0u:low_word(quotient,1u),rest);
}

/* Original: DBSIM.EXE 0x409178 */
uint64_t es2_smoke_ball_init_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t ball,uint32_t point,uint32_t frame,const uint32_t entry[3]) {
    (void)entry;
    uint32_t b=g->image,a[2],table=R32(VA(b,0x4a9678u)),index=sx16(frame);
    uint32_t shape=R32(table+4u*index);
    es2_weapon_registers r={index,table,shape};
    a[0]=ball;a[1]=shape;pool_call(g,0x402188u,0,a,2,&r);
    W32(ball,VA(b,0x498084u));W16(ball+4u,4u);
    r.ecx=R32(point+8u);W32(ball+0x2eu,r.ecx);
    r.ecx=R32(point);W32(ball+0x26u,r.ecx);
    r.edx=R32(point+4u);W32(ball+0x2au,r.edx);
    W32(ball+0x48u,0xc8u);
    (void)pool_count(m,b,1u);
    return pair(ball,r.edx);
}

/* Original: DBSIM.EXE 0x427d80 */
uint64_t es2_ground_height_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t point,const uint32_t entry[3]) {
    uint32_t a[2];
    es2_weapon_registers r={point,R32(VA(g->image,0x4a0be0u)),entry[2]};
    a[0]=r.edx;a[1]=point;pool_call(g,0x46e180u,0,a,2,&r);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4092dc. Frame f: P f-44h, Q f-38h, owner copy
 * f-2Ch, node word f-26h, record f-24h, context word f-14h, slot f-4. */
uint64_t es2_smoke_update_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    uint32_t b=g->image,f=g->scratch+208u,p=f-0x44u,q=f-0x38u,a[3];
    es2_weapon_registers r={VA(b,0x497f9au),entry[1],entry[2]};
    pool_call(g,0x49630cu,0,NULL,0,&r);
    if(!R16(o+0x24u))return pair(1,R32(f-0x24u));
    a[0]=o+0x21u;r.edx=o+0x21u;pool_call(g,0x467a48u,0,a,1,&r);
    if((uint16_t)r.eax)return pair(R16(o+0x24u)==0,R32(f-0x24u));
    W16(o+0x22u,0xc8u);W16(o+0x24u,R16(o+0x24u)-1u);
    uint32_t node=R16(o+0x19u);
    if(fx_s16(node)<=-1&&R8(o+0xcu)) {
        uint32_t s=R32(o+0x15u)+0x26u;
        r.edx=R32(s+8u);W32(q+8u,r.edx);
        r.ecx=R32(s);W32(q,r.ecx);
        r.eax=R32(s+4u);W32(q+4u,r.eax);
    } else {
        if(fx_s16(node)<0) {
            r.edx=sx16(R16(o+0x1fu));W32(p+8u,r.edx);
            r.ecx=sx16(R16(o+0x1bu));W32(p,r.ecx);
            r.eax=sx16(R16(o+0x1du));W32(p+4u,r.eax);
        } else {
            r.edx=low_word(r.edx,node);W16(f-0x26u,node);
            uint32_t owner=R32(o+0x15u);W32(f-0x2cu,owner);
            uint32_t shape=R32(owner+0x34u),object=R32(shape+4u),table=R32(object);
            a[0]=object;a[1]=r.edx;r.eax=table;r.ecx=object;
            pool_call(g,R32(table+0x20u),0x20,a,2,&r);
            uint32_t record=r.eax,matrix=VA(b,0x6c56a8u);
            if(record&&fx_s16(R16(record+4u))>-1) {
                r.edx=sx16(R16(record+4u))<<5;
                matrix=R32(shape+0x16u)+r.edx;
            }
            if(!R8(o+0xcu)) {
                a[0]=o+0x1bu;a[1]=matrix;a[2]=p;r.eax=matrix;r.edx=o+0x1bu;r.ecx=p;
                pool_call(g,0x4801d4u,0,a,3,&r);
            } else {
                uint32_t t=matrix+0x14u;
                r.edx=R32(t+8u);W32(p+8u,r.edx);
                r.ecx=R32(t);W32(p,r.ecx);
                r.eax=R32(t+4u);W32(p+4u,r.eax);
            }
        }
        uint32_t angles=R32(o+0x15u)+0xcu;
        if(!R16(angles+0x26u)) {
            a[0]=angles;a[1]=angles+6u;r.eax=angles+6u;
            pool_call(g,0x47ebb8u,0,a,2,&r);
            W16(angles+0x26u,1u);
        }
        a[0]=p;a[1]=angles+6u;a[2]=q;r.eax=angles+6u;r.edx=q;r.ecx=p;
        pool_call(g,0x48043cu,0,a,3,&r);
    }
    uint32_t pool=R32(VA(b,0x4a966eu));
    a[0]=pool;r.edx=pool;pool_call(g,0x471b30u,0,a,1,&r);r.ecx=pool;
    uint32_t ball=r.eax;W32(f-4u,ball);
    if(ball) {
        W16(f-0x14u,0x14u);
        r.eax=low_word(ball,R16(o+0x26u));
        a[0]=ball;a[1]=q;a[2]=r.eax;r.edx=q;r.ecx=ball;
        pool_call(g,0x409178u,0,a,3,&r);
        (void)pool_count(m,b,(uint32_t)-4);
        W16(f-0x14u,8u);
    }
    uint32_t phase=R16(o+0x26u)+1u;
    W16(o+0x26u,(R16(o+0x28u)<<2)+(phase&3u));
    return pair(R16(o+0x24u)==0,R32(f-0x24u));
}

/* Original: DBSIM.EXE 0x4089bc (pure leaf): the debris effect table for
 * the index word at x, which it rebases into the object's own table. */
static uint32_t debris_table(const es2_guest_memory *m,uint32_t b,uint32_t x) {
    uint32_t count=sx16(R16(VA(b,0x4a9654u)));
    if(fx_s32(count)>fx_s32(sx16(R16(x))))return VA(b,0x4a9650u);
    uint32_t table=R32(VA(b,0x4a964cu));
    W16(x,R16(x)-count);
    return table;
}

/* Original: DBSIM.EXE 0x408bd8. Frame f: P f-44h, V f-34h, E f-2Ch, index
 * word f-36h, record f-24h, context word f-14h, slot f-4. */
uint64_t es2_debris_update_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    uint32_t b=g->image,f=g->scratch+208u,p=f-0x44u,a[5],s=0;
    es2_weapon_registers r={VA(b,0x497e10u),entry[1],entry[2]};
    pool_call(g,0x49630cu,0,NULL,0,&r);
    uint32_t level=R16(VA(b,0x4a9e4eu))==4u,gravity=level?0xfffffff6u:0xffffffe0u;
    uint32_t x=R32(o+0x26u);W32(p,x);
    uint32_t y=R32(o+0x2au);W32(p+4u,y);
    W32(p+8u,R32(o+0x2eu));
    W16(f-0x2cu,R16(o+0xcu));
    uint32_t old_y=R16(o+0xeu);W16(f-0x2au,old_y);
    W16(f-0x28u,R16(o+0x10u));
    r.eax=level;r.ecx=low_word(y,old_y);r.edx=low_word(x,R16(o+0x5bu));
    a[0]=r.edx;pool_call(g,0x467924u,0,a,1,&r);r.ecx=a[0];
    W16(f-0x2cu,R16(f-0x2cu)+r.eax);
    r.edx=low_word(r.edx,R16(o+0x4au));W16(f-0x34u,r.edx);
    r.ecx=low_word(r.ecx,R16(o+0x4cu));W16(f-0x32u,r.ecx);
    r.eax=low_word(o+0x4au,R16(o+0x4eu));W16(f-0x30u,r.eax);
    a[0]=gravity;pool_call(g,0x467924u,0,a,1,&r);r.ecx=gravity;
    W16(o+0x4eu,R16(o+0x4eu)+r.eax);
    for(unsigned k=0;k<2;++k) {
        uint32_t w=o+0x4au+2u*k;
        a[0]=0x1eu;a[1]=sx16(R16(w));pool_call(g,0x47e0b0u,0,a,2,&r);
        uint32_t product=r.eax;
        a[0]=product;pool_call(g,0x467924u,0,a,1,&r);r.ecx=product;
        W16(w,R16(w)-r.eax);
    }
    r.edx=low_word(r.edx,R16(o+0x4eu));W16(f-0x30u,R16(f-0x30u)+r.edx);
    r.ecx=low_word(r.ecx,R16(o+0x4au));W16(f-0x34u,R16(f-0x34u)+r.ecx);
    r.eax=low_word(o+0x4au,R16(o+0x4cu));W16(f-0x32u,R16(f-0x32u)+r.eax);
    W16(f-0x30u,fx_sar(sx16(R16(f-0x30u)),1));
    W16(f-0x34u,fx_sar(sx16(R16(f-0x34u)),1));
    W16(f-0x32u,fx_sar(sx16(R16(f-0x32u)),1));
    r.edx=sx16(R16(f-0x30u));W32(p+8u,R32(p+8u)+r.edx);
    r.ecx=sx16(R16(f-0x34u));W32(p,R32(p)+r.ecx);
    r.eax=sx16(R16(f-0x32u));W32(p+4u,R32(p+4u)+r.eax);
    uint32_t table=R32(o);
    a[0]=o;r.edx=table;pool_call(g,R32(table+0x10u),0x10,a,1,&r);
    r.ecx=o;
    a[0]=0x1f4u;a[1]=sx16(r.eax);pool_call(g,0x47e0b0u,0,a,2,&r);
    uint32_t lift=r.eax;
    a[0]=p;a[1]=0;r.eax=p;pool_call(g,0x427d80u,0,a,2,&r);
    r.edx=sx16(r.eax);r.eax=lift+r.edx;
    if(fx_s32(r.eax)>fx_s32(R32(p+8u))) {
        W32(p+8u,r.eax);
        a[0]=0x1c2u;a[1]=sx16(R16(o+0x4eu));pool_call(g,0x47e0b0u,0,a,2,&r);
        W16(o+0x4eu,0u-r.eax);
        W32(o+0x55u,0);
        if(fx_s16(R16(o+0x4eu))<0x2d)s=1;
    }
    r.edx=R32(p+8u);W32(o+0x2eu,r.edx);
    r.ecx=R32(p);W32(o+0x26u,r.ecx);
    r.edx=R32(p+4u);W32(o+0x2au,r.edx);
    uint32_t angles=R32(f-0x2cu);W32(o+0xcu,angles);
    r.ecx=low_word(angles,R16(f-0x28u));W16(o+0x10u,r.ecx);
    W16(o+0x32u,0);
    r.eax=sx8(R8(o+0x41u))+1u;
    if(fx_s32(r.eax)<=0)return pair(s,R32(f-0x24u));
    if(!s) {
        a[0]=o+0x54u;r.edx=o+0x54u;pool_call(g,0x467aa8u,0,a,1,&r);
        r.ecx=r.eax==0;
        if(r.eax)return pair(s,R32(f-0x24u));
    }
    if(fx_s16(R16(o+0x59u))>-1) {
        uint32_t pool=R32(VA(b,0x4a961eu));
        a[0]=pool;r.eax=pool;pool_call(g,0x471b30u,0,a,1,&r);r.ecx=pool;
        uint32_t k=r.eax;W32(f-4u,k);
        if(k) {
            W16(f-0x14u,0x14u);
            r.ecx=low_word(pool,R16(o+0x59u));r.edx=o+0x26u;r.eax=k;
            a[0]=k;a[1]=r.ecx;a[2]=o+0x26u;a[3]=0;a[4]=1;
            pool_call(g,0x407f1cu,0,a,5,&r);
            (void)pool_count(m,b,(uint32_t)-4);
            W16(f-0x14u,8u);
        }
    }
    if(R8(VA(b,0x4d1f43u))) {
        W32(VA(b,0x4a964cu),R32(o+0x5du));
        W16(VA(b,0x4a9664u),0xbb8u);W16(VA(b,0x4a9666u),0x1f40u);W16(VA(b,0x4a9668u),0x140u);
        s=o+0x26u;
        uint32_t x36=f-0x36u;W16(x36,sx8(R8(o+0x41u)));
        uint32_t t=debris_table(m,b,x36),i=sx16(R16(x36));
        a[1]=R32(t+6u);a[0]=R32(t)+10u*i;a[2]=o+0x26u;a[3]=0;
        r.eax=a[0];r.edx=i;r.ecx=10u*i;
        pool_call(g,0x408530u,0,a,4,&r);
        W16(VA(b,0x4a9664u),0x1770u);W16(VA(b,0x4a9666u),0x3e80u);W16(VA(b,0x4a9668u),0x1a4u);
    }
    return pair(low_word(s,1u),R32(f-0x24u));
}

/* Original: DBSIM.EXE 0x409d2c. Frame f: S f-34h, record f-28h, context
 * word f-18h, shape copy f-8, slot f-4. */
uint64_t es2_meteor_update_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t o,const uint32_t entry[3]) {
    uint32_t b=g->image,f=g->scratch+208u,s=f-0x34u,a[5];
    es2_weapon_registers r={VA(b,0x4981b0u),entry[1],entry[2]};
    pool_call(g,0x49630cu,0,NULL,0,&r);
    if(R8(o+0x4bu)) {
        uint32_t shape=R32(o+0x41u);r.edx=shape;
        r.eax=fx_sar(R32(o+0x4du),10);
        r.ecx=R16(R32(R32(shape+4u)+0x20u));
        if(fx_s32(r.eax)<fx_s32(r.ecx)) {
            r.edx=R32(shape+8u);W16(r.edx,r.eax);
            a[0]=0x5dcu;pool_call(g,0x467924u,0,a,1,&r);
            r.ecx=sx16(r.eax);W32(o+0x4du,R32(o+0x4du)+r.ecx);
            return pair(0,r.edx);
        }
        r.eax=R32(o+0x55u);
        if(r.eax) {
            if(!R8(o+0x4cu)) {
                uint32_t actor=R32(R32(r.eax+0xcu))+0x26u;
                r.ecx=R32(o+0x2eu);W32(actor+8u,r.ecx);
                r.ecx=R32(o+0x26u);W32(actor,r.ecx);
                W32(actor+4u,R32(o+0x2au));
                r.edx=0;W32(R32(o+0x55u)+0x14u,0);
            }
            uint32_t pool=R32(VA(b,0x4a968du));
            a[0]=pool;r.eax=pool;pool_call(g,0x471b30u,0,a,1,&r);r.ecx=pool;
            uint32_t k=r.eax;W32(f-4u,k);
            if(k) {
                W16(f-0x18u,0x14u);
                r.eax=R32(VA(b,0x4a9698u));r.edx=R32(r.eax+0xcu);r.ecx=k;
                a[0]=k;a[1]=r.edx;pool_call(g,0x402188u,0,a,2,&r);
                W32(k,VA(b,0x49a262u));W16(k+4u,9u);
                r.ecx=pool_count(m,b,1u);
                (void)pool_count(m,b,(uint32_t)-4);
                W16(f-0x18u,8u);
            }
            /* A null k faults here in the original (outside the contract). */
            uint32_t t=k+0x26u;
            r.ecx=R32(o+0x2eu);W32(t+8u,r.ecx);
            r.ecx=R32(o+0x26u);W32(t,r.ecx);
            r.edx=R32(o+0x2au);W32(t+4u,r.edx);
        }
        uint32_t last=R32(o+0x41u);r.eax=last;W32(f-8u,last);
        if(last) {
            a[0]=last;a[1]=0;pool_call(g,0x496406u,0,a,2,&r);
            uint32_t counter=R32(VA(b,0x6cbee4u));W32(counter,R32(counter)+r.eax);
            W16(f-0x18u,0x2cu);
            uint32_t table=R32(last);
            a[0]=last;a[1]=3;r.eax=table;r.edx=counter;r.ecx=last;
            pool_call(g,R32(table+0x18u),0x18,a,2,&r);
            W16(f-0x18u,0x20u);
        }
        return pair(low_word(r.eax,1u),R32(f-0x28u));
    }
    r.edx=R32(o+0x26u);W32(s,r.edx);
    r.ecx=R32(o+0x2au);W32(s+4u,r.ecx);
    W32(s+8u,R32(o+0x2eu));
    r.edx=sx16(R16(o+0x49u));W32(s+8u,R32(s+8u)+r.edx);
    r.ecx=sx16(R16(o+0x45u));W32(s,R32(s)+r.ecx);
    r.eax=sx16(R16(o+0x47u));W32(s+4u,R32(s+4u)+r.eax);
    a[0]=0x7d0u;a[1]=r.edx;pool_call(g,0x47d32cu,0,a,2,&r);
    r.edx=r.eax;r.eax=o+0xcu;
    W16(o+0xcu,r.edx);W16(o+0x32u,0);
    if(!R32(o+0x51u)&&fx_s32(R32(s+8u))<50000) {
        r.ecx=R32(VA(b,0x4d24eau))+4u;
        a[0]=0x2fu;a[1]=r.ecx;pool_call(g,0x4628e0u,0,a,2,&r);
        W32(o+0x51u,1u);
    }
    uint32_t grid=R32(VA(b,0x4a0be0u));
    a[0]=grid;a[1]=s;r.eax=grid;pool_call(g,0x46e180u,0,a,2,&r);
    if(fx_s32(r.eax)>fx_s32(R32(s+8u))) {
        W32(s+8u,r.eax);
        r.eax=o+0xcu;W8(o+0x4bu,1u);W16(o+0xcu,0);W16(o+0x32u,0);
        r.edx=R32(VA(b,0x4d24eau))+4u;
        a[0]=0x30u;a[1]=r.edx;pool_call(g,0x4628e0u,0,a,2,&r);
        a[0]=s;a[1]=0xbb8u;a[2]=0x2710u;a[3]=0;a[4]=0;
        pool_call(g,0x426a20u,0,a,5,&r);
        if((uint16_t)r.eax)W8(o+0x4cu,1u);
    }
    r.edx=R32(s+8u);W32(o+0x2eu,r.edx);
    r.ecx=R32(s);W32(o+0x26u,r.ecx);
    r.edx=R32(s+4u);W32(o+0x2au,r.edx);
    return pair(0,r.edx);
}
