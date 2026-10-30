#include "es2/core/weapon_eject.h"
#include "fixed.h"
#include <stddef.h>
/* Weapon eject and weapon damage slot (docs/reverse-engineering/ weapons.md,
 * section "weapon eject"). r holds what the original's EAX/EDX/ECX hold at the
 * next game call; lo() is a 16-bit load that keeps the register's high word.
 * The eject's frame lives in the scratch at the original EBP offsets (f = the
 * synthetic EBP): K f-26h, A+0Ch f-2Ch/f-3Ch, A+12h f-30h, the debris shape
 * index f-32h, the angles f-38h, the object f-40h and f-4, the mount point
 * f-58h, the composed matrix f-78h, the aim point f-84h; the runtime record at
 * f-24h (context word f-14h). */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define VA(g,x) ((g)->image+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes its resolved target and the
 * vtable offset. Registers are updated in place. */
static void we_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}
static void we_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* The lazy pose matrix of A (40f5bf / 40f660): euler_matrix(A+0Ch, A+12h)
 * with EAX = A+12h, EDX = A+0Ch reloaded from the frame slot, then word
 * A+32h <- 1. */
static void we_pose(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t pose,uint32_t slot,es2_weapon_registers *r) {
    uint32_t a[2];
    if(R16(pose+0x26u))return;
    W32(slot,pose);r->eax=pose+6u;r->edx=R32(slot);
    a[0]=r->edx;a[1]=r->eax;we_call(g,0x47ebb8u,a,2,r);
    W16(pose+0x26u,1u);
}

/* Original: DBSIM.EXE 0x40f57c */
uint64_t es2_weapon_eject_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t W,uint32_t A,uint32_t flag,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_WEAPON_EJECT_FRAME;
    uint32_t a[5];
    es2_weapon_registers r={VA(g,0x4989e8u),entry[1],entry[2]};
    we_call(g,0x49630cu,NULL,0,&r);
    r.edx=W+0x43u;r.eax=r.edx;
    if(R8(r.eax+6u))goto leave; /* 40f5a1: already ejected */
    r.ecx=0;W32(W+0x10u,r.ecx);W8(r.eax+6u,1u);
    r.edx=R32(W+0x18u);r.ecx=lo(r.ecx,R16(r.edx));W16(f-0x26u,r.ecx);
    we_pose(m,g,A+0xcu,f-0x2cu,&r); /* 40f5c4 */
    W32(f-0x30u,A+0x12u);
    /* 40f5e5: the node of the model word K, M = its matrix or 6c56a8h. */
    r.eax=lo(r.eax,R16(f-0x26u));a[1]=r.eax;
    const uint32_t shape=R32(A+0x34u);
    r.eax=R32(shape+4u);a[0]=r.eax;r.edx=R32(r.eax);
    we_virtual(g,R32(r.edx+0x20u),0x20u,a,2,&r);
    if(r.eax&&fx_s16(R16(r.eax+4u))>-1) { /* 40f5fb, 40f602 */
        r.eax=sx(R16(r.eax+4u))<<5;r.ecx=R32(shape+0x16u)+r.eax;
    } else r.ecx=VA(g,0x6c56a8u);
    /* 40f617: the mount point f-58h. */
    r.eax=r.ecx;r.edx=R32(f-0x30u);
    a[0]=r.eax;a[1]=r.edx;a[2]=f-0x78u;we_call(g,0x47fa20u,a,3,&r);
    a[0]=W;we_call(g,0x40f540u,a,1,&r);r.ecx=W;
    r.edx=f-0x78u;r.ecx=f-0x58u;
    a[0]=r.eax;a[1]=r.edx;a[2]=r.ecx;we_call(g,0x4801d4u,a,3,&r);
    r.eax=R32(W+0x18u);r.ecx=(uint32_t)(int32_t)(int8_t)R8(r.eax+6u);
    if(fx_s32(r.ecx)>=4)goto leave; /* 40f64c: no debris for this kind */
    r.eax=R32(W+0x1cu);r.edx=lo(r.edx,R16(r.eax+0x26u));W16(f-0x32u,r.edx);
    we_pose(m,g,A+0xcu,f-0x3cu,&r); /* 40f665: A+32h reread */
    /* 40f683: the aim node of A, the aim point f-84h, the angles f-38h. */
    a[0]=A;r.eax=R32(A);we_virtual(g,R32(r.eax+0x24u),0x24u,a,1,&r);r.ecx=A;
    r.eax+=0x14u;r.edx=f-0x84u;
    a[0]=r.eax;a[1]=A+0x12u;a[2]=f-0x84u;we_call(g,0x48043cu,a,3,&r);
    r.ecx=f-0x84u;r.eax=f-0x58u;r.edx=f-0x38u;
    a[0]=f-0x38u;a[1]=f-0x58u;a[2]=f-0x84u;we_call(g,0x492990u,a,3,&r);
    uint32_t b=0xffffffffu,w=0xffffffffu;
    if(!(uint16_t)flag){b=2u;w=0x14u;} /* 40f6c0: the flag word */
    /* 40f6cc: the debris object. */
    r.eax=R32(VA(g,0x4a9642u));a[0]=r.eax;we_call(g,0x471b30u,a,1,&r);r.ecx=a[0];
    W32(f-0x40u,r.eax);r.edx=R32(f-0x40u);W32(f-4u,r.edx);
    if(r.edx) { /* 40f6e3 */
        W16(f-0x14u,0x14u);
        r.eax=R32(VA(g,0x4a98f4u));r.ecx=sx(R16(f-0x32u));r.edx=R32(r.eax+4u*r.ecx);r.ecx=R32(f-4u);
        a[0]=r.ecx;a[1]=r.edx;a[2]=f-0x58u;a[3]=b;a[4]=w;we_call(g,0x408940u,a,5,&r);
        r.eax=R32(VA(g,0x6cbee4u));W32(r.eax,R32(r.eax)-4u);
        W16(f-0x14u,8u);
    }
    r.edx=R32(f-4u);
    const uint32_t object=r.edx;
    if(!object)goto leave; /* 40f724 */
    r.eax=R32(VA(g,0x4a9868u));W32(object+0x50u,r.eax);
    r.ecx=lo(r.ecx,R16(f-0x34u));a[3]=r.ecx;
    r.eax=R32(W+0x18u);r.edx=lo(r.edx,R16(r.eax+0x18u));a[2]=r.edx;
    a[1]=0x4b0u;a[0]=object;we_call(g,0x4089e4u,a,4,&r);
    r.ecx=f-0x38u;r.eax=f-0x78u;
    a[0]=f-0x78u;a[1]=f-0x38u;we_call(g,0x47f9a0u,a,2,&r);
    r.eax=object+0xcu;r.edx=R32(f-0x38u);W32(r.eax,r.edx);
    r.edx=lo(r.edx,R16(f-0x34u));W16(r.eax+4u,r.edx);W16(r.eax+0x26u,0);
leave:
    *ecx=R32(f-0x24u);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40ee0c */
uint64_t es2_weapon_damage_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t W,uint32_t A,uint32_t x,uint32_t d,const uint32_t entry[3]) {
    es2_weapon_registers r={W,entry[1],entry[2]};(void)x;
    if(d==0x100u) { /* 40ee19 */
        r.edx=W+0x43u;
        if(!R8(r.edx+6u)) { /* 40ee22 */
            const uint32_t a[3]={W,A,0};r.ecx=A;we_call(g,0x40f57cu,a,3,&r);
        }
    }
    return pair(r.eax,r.edx);
}

/* The common body 0x40ee0c through the bridge (a nested hook keeps its
 * mode), with the caller's EAX/EDX/ECX. */
static void we_common(const es2_weapon_graph *g,uint32_t W,uint32_t A,uint32_t x,uint32_t d,es2_weapon_registers *r) {
    const uint32_t a[4]={W,A,x,d};we_call(g,0x40ee0cu,a,4,r);
}

/* Original: DBSIM.EXE 0x40ee38 */
uint64_t es2_beam_damage_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t W,uint32_t A,uint32_t x,uint32_t d,const uint32_t entry[3]) {
    es2_weapon_registers r={x,A,entry[2]};
    we_common(g,W,A,x,d,&r);
    W32(W+0x7fu,0x14u);
    if(fx_s32(d)>0x80) { /* 40ee63 */
        const int64_t t=fx_s32(d-0x80u);
        r.eax=(uint32_t)(t/25);r.edx=(uint32_t)(t%25);r.ecx=0x19u;
        r.eax<<=2;r.eax*=5u;r.eax*=5u;
        const uint32_t a[2]={0x14u,r.eax};we_call(g,0x47e0b0u,a,2,&r);
        W32(W+0x7fu,R32(W+0x7fu)-r.eax);
    }
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40ee90 */
uint64_t es2_gun_damage_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t W,uint32_t A,uint32_t x,uint32_t d,const uint32_t entry[3]) {
    es2_weapon_registers r={A,entry[1],entry[2]};
    we_common(g,W,A,x,d,&r);
    if(!R16(W+0x7bu))return pair(r.eax,r.edx); /* 40eeb4 */
    r.edx=R32(W+0x20u);
    if(!R16(r.edx)) { /* 40eec1: type 0, the eject chance */
        if(fx_s32(d)<=0x80)return pair(r.eax,r.edx); /* 40eec9 */
        const int64_t end=fx_s32(d-0x80u)/25,t=fx_s32(x-0x80u);
        r.eax=(uint32_t)(t/25);r.edx=(uint32_t)(t%25);r.ecx=0x19u;
        int64_t i=fx_s32(r.eax);int hit=0;
        if(i<0)i=-1; /* 40eeee */
        const uint32_t state=VA(g,0x4d2599u);
        for(;;) {
            r.eax=(uint32_t)i;++i;
            if(fx_s32(r.eax)>=end||hit)break; /* 40ef15, 40ef19 */
            const uint32_t a[1]={state};we_call(g,0x492ee0u,a,1,&r);
            r.eax=lo(r.eax,r.eax&0x3ffu);r.ecx=state;
            if(fx_s16(r.eax)<0x12c)hit=1; /* 40ef08 */
        }
        if(hit) { /* 40ef1d */
            const uint32_t a[3]={W,A,0};r.edx=A;we_call(g,0x40f57cu,a,3,&r);
        }
        return pair(r.eax,r.edx);
    }
    r.ecx=R32(W+0x20u);
    if(R16(r.ecx)!=2u||fx_s32(d)<=0x80)return pair(r.eax,r.edx); /* 40ef37, 40ef3f */
    const uint32_t q=(uint32_t)(fx_s32(d-0x80u)/25);
    r.edx=q;r.eax=q*102u;r.ecx=lo(0x19u,0x400u-r.eax);
    W16(W+0x63u,r.ecx);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x40ef6c */
uint64_t es2_targ_damage_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t W,uint32_t A,uint32_t x,uint32_t d,const uint32_t entry[3]) {
    es2_weapon_registers r={x,A,entry[2]};
    we_common(g,W,A,x,d,&r);
    W16(W+0x7fu,d);
    return pair(r.eax,r.edx);
}
