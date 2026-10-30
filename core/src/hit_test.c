#include "es2/core/hit_test.h"
#include "fixed.h"
#include <stddef.h>
/* Actor hit tests (docs/reverse-engineering/hit_tests.md, the section
 * "hit tests"). r holds what the original's EAX/EDX/ECX hold at
 * the next game call; lo() is a 16-bit load that keeps the register's high
 * word. Frame locals live in the scratch frame at the original EBP offsets (f =
 * the synthetic EBP); the record of 0x49630c lies at f-24h, its saved FS:0 head
 * is the dword f-24h. S is the segment [P], C the configuration [actor+1F2h].
 * */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(g,x) ((g)->image+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes its resolved target and the
 * vtable offset. Registers are updated in place. */
static void ht_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}
static void ht_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* sub dword [[0x6cbee4]],4: the pointer ends in the register the original
 * loads it into. */
static uint32_t ht_uncount(const es2_guest_memory *m,const es2_weapon_graph *g) {
    uint32_t counter=R32(VA(g,0x6cbee4u));
    W32(counter,R32(counter)-4u);
    return counter;
}
/* pool_alloc([0x4a961e]) with the pool in EAX; POP ECX leaves the pool in
 * ECX. The object goes to f-4. */
static uint32_t ht_alloc(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t f,es2_weapon_registers *r) {
    uint32_t a[1];
    a[0]=R32(VA(g,0x4a961eu));r->eax=a[0];ht_call(g,0x471b30u,a,1,r);r->ecx=a[0];
    W32(f-4u,r->eax);
    return r->eax;
}
/* rng_next(0x4d2599); POP ECX leaves the state address in ECX. */
static void ht_rng(const es2_weapon_graph *g,es2_weapon_registers *r) {
    uint32_t a[1]={VA(g,0x4d2599u)};
    ht_call(g,0x492ee0u,a,1,r);r->ecx=a[0];
}

/* Original: DBSIM.EXE 0x405038. Frame f: point f-80h, transformed point
 * f-74h, composed matrix f-68h, debris word f-46h, f-44h/f-40h copies,
 * query length f-3Ch, one-element list f-34h, part f-30h, damage word
 * f-2Ah, result f-28h, record f-24h (context word f-14h), object f-4. */
uint64_t es2_base_hit_test_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t b,uint32_t p,const uint32_t entry[3]) {
    uint32_t f=g->scratch+ES2_HIT_TEST_FRAME,a[5];
    es2_weapon_registers r={VA(g,0x4974e6u),entry[1],entry[2]};
    ht_call(g,0x49630cu,NULL,0,&r);
    const uint32_t s=R32(p);
    W32(f-0x28u,0);
    /* DI = word P+6; EDI's high word is never read. */
    const uint16_t di=R16(p+6u);
    r.edx=lo(p,R16(p+4u));W16(f-0x2au,r.edx);
    W32(f-0x30u,0xffffffffu);
    r.edx=R32(b+0x1f2u);
    uint32_t q=0;
    if(R16(r.edx+4u)!=0xffffu&&fx_s16(R16(R32(b+0x205u)+5u))<2&&R8(b+0x99u))q=1;
    r.eax=q;
    if(q||!R32(r.edx+0x38u)) {
        /* Object query over the one-element list {B}. */
        W32(f-0x34u,b);
        r.edx=f-0x34u;r.ecx=f-0x3cu;
        a[0]=f-0x3cu;a[1]=s;a[2]=f-0x34u;a[3]=1;ht_call(g,0x427da8u,a,4,&r);
        if((uint16_t)r.eax) {
            r.eax=R32(f-0x3cu)+1u;W32(f-0x28u,r.eax);
            if(!R16(f-0x2au)&&!di) {
                r.edx=lo(r.edx,R16(VA(g,0x4a95f2u)));W16(f-0x2au,r.edx);
            }
        }
    } else {
        /* COL path: reach = s16(slot +10h) + s16(S+8) + [S+4]. */
        r.ecx=R32(b);a[0]=b;ht_virtual(g,R32(r.ecx+0x10u),0x10,a,1,&r);
        uint32_t reach=sx(r.eax);
        r.eax=sx(R16(s+8u))+R32(s+4u);
        r.ecx=b;r.edx=b+0x26u;
        reach+=r.eax;
        r.ecx=R32(s)+0x14u;
        a[0]=r.ecx;a[1]=b+0x26u;ht_call(g,0x49288cu,a,2,&r);
        if(fx_s32(reach)<fx_s32(r.eax))return pair(0,R32(f-0x24u));
        r.ecx=s+0xau;W32(f-0x40u,r.ecx);
        if(!R16(b+0x32u)) {
            W32(f-0x44u,b+0xcu);
            r.eax=b+0x12u;r.edx=b+0xcu;
            a[0]=b+0xcu;a[1]=b+0x12u;ht_call(g,0x47ebb8u,a,2,&r);
            W16(b+0x32u,1u);
        }
        r.eax=b+0x12u;r.edx=f-0x68u;r.ecx=R32(f-0x40u);
        a[0]=b+0x12u;a[1]=r.ecx;a[2]=f-0x68u;ht_call(g,0x47fa20u,a,3,&r);
        const uint32_t flags=R32(b+0x201u);
        r.edx=f-0x68u;r.ecx=R32(b+0x1f2u);r.eax=R32(r.ecx+0x38u);
        a[0]=r.eax;a[1]=b;a[2]=s;a[3]=f-0x68u;a[4]=flags;ht_call(g,0x40c9d4u,a,5,&r);
        if(r.eax) {
            r.edx=R32(r.eax)+1u;W32(f-0x28u,r.edx);
            r.ecx=sx(R16(r.eax+4u));W32(f-0x30u,r.ecx);
        }
    }
    if(!R32(f-0x28u))return pair(0,r.edx);
    /* Hit: the point (0, result, 0) through the forward pose. */
    r.eax=R32(f-0x28u);
    r.edx=0;W32(f-0x80u,0);W32(f-0x7cu,r.eax);
    r.ecx=0;r.edx=f-0x74u;W32(f-0x78u,0);
    r.ecx=f-0x80u;r.eax=R32(s);
    a[0]=f-0x80u;a[1]=r.eax;a[2]=f-0x74u;ht_call(g,0x48043cu,a,3,&r);
    r.eax=b+0x92u;
    if(!R8(b+0x99u)) {
        /* Slot +74h (part damage) with the part and damage words in the
         * low halves of P and B+92h. */
        r.edx=p;r.ecx=R32(p+0xeu);a[3]=r.ecx;
        r.eax=lo(r.eax,R16(f-0x2au));a[2]=r.eax;
        r.edx=lo(r.edx,R16(f-0x30u));a[1]=r.edx;
        a[0]=b;r.ecx=R32(b);ht_virtual(g,R32(r.ecx+0x74u),0x74,a,4,&r);
    }
    if(ht_alloc(m,g,f,&r)) {
        W16(f-0x14u,0x14u);
        r.edx=f-0x74u;ht_rng(g,&r);
        r.eax=lo(r.eax,r.eax&3u);
        r.ecx=sx(r.eax);r.eax=p;r.edx=R32(p+0xau);
        r.ecx=lo(r.ecx,R16(r.edx+2u*r.ecx+0x10u));
        r.eax=R32(f-4u);
        a[0]=r.eax;a[1]=r.ecx;a[2]=f-0x74u;a[3]=b;a[4]=1;ht_call(g,0x407f1cu,a,5,&r);
        r.edx=ht_uncount(m,g);
        W16(f-0x14u,8u);
    }
    /* Debris chance 1025/4096. */
    ht_rng(g,&r);
    r.ecx=sx(r.eax)&0xfffu;
    r.eax=fx_s32(r.ecx)<0x401;
    if(r.eax) {
        W16(f-0x46u,1u);
        r.edx=f-0x46u;a[0]=f-0x46u;ht_call(g,0x4089bcu,a,1,&r);
        r.ecx=f-0x46u;
        const uint32_t record=r.eax;
        r.edx=R32(record+6u);const uint32_t shapes=r.edx;
        r.ecx=sx(R16(f-0x46u));r.edx=r.ecx;
        r.eax=R32(record);r.edx=10u*r.edx;r.eax+=r.edx;
        a[0]=r.eax;a[1]=shapes;a[2]=f-0x74u;a[3]=0;ht_call(g,0x408530u,a,4,&r);
    }
    return pair(R32(f-0x28u),r.edx);
}

/* Original: DBSIM.EXE 0x418ba8. Frame f: hit point f-88h, transformed
 * point f-70h, absorbed point f-7Ch, composed matrix f-64h, copies f-44h,
 * f-40h, f-3Ch, f-38h, shield damage word f-32h, result f-30h, hit record
 * f-2Ch, damage word f-26h, record f-24h (context word f-14h), object
 * f-4. */
uint64_t es2_mech_hit_test_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t A,uint32_t p,const uint32_t entry[3]) {
    uint32_t f=g->scratch+ES2_HIT_TEST_FRAME,a[5];
    es2_weapon_registers r={VA(g,0x49a138u),entry[1],entry[2]};
    ht_call(g,0x49630cu,NULL,0,&r);
    const uint32_t s=R32(p);
    /* dist_gate gets the entry ECX (0x49630c leaves it). */
    r.eax=A+0x26u;r.edx=R32(s)+0x14u;
    a[0]=r.edx;a[1]=A+0x26u;ht_call(g,0x49288cu,a,2,&r);
    const uint32_t d=r.eax;
    r.edx=R32(A+0x1f2u);
    r.ecx=sx(R16(s+8u));
    r.edx=sx(R16(r.edx+0x1au));
    r.ecx+=R32(s+4u);
    r.ecx+=r.edx;
    if(fx_s32(d)>fx_s32(r.ecx))return pair(0,r.edx);
    r.eax=lo(d,R16(p+6u));W16(f-0x26u,r.eax);
    r.eax=A+0x92u;
    if(!R8(A+0xa3u)) {
        /* AI reaction to the weapon kind. */
        r.eax=lo(r.eax,R16(p+0x12u));
        if(!(uint16_t)r.eax||(uint16_t)r.eax==1u) {
            if(!R16(A+0x26bu)) { r.eax=A+0x92u;W8(A+0x96u,1u); }
        } else if(R16(p+0x12u)==2u) {
            r.eax=A+0x92u;W8(A+0x96u,0);W16(A+0x26bu,0x1770u);
        }
    }
    r.edx=lo(r.edx,R16(f-0x26u));W16(f-0x32u,r.edx);
    r.ecx=s+0xau;W32(f-0x38u,r.ecx);
    r.eax=A+0xcu;W32(f-0x3cu,r.eax);
    r.edx=R32(f-0x3cu);
    if(!R16(r.edx+0x26u)) {
        r.ecx=R32(f-0x3cu);W32(f-0x40u,r.ecx);
        r.eax=R32(f-0x3cu)+6u;r.edx=R32(f-0x40u);
        a[0]=r.edx;a[1]=r.eax;ht_call(g,0x47ebb8u,a,2,&r);
        r.ecx=R32(f-0x3cu);W16(r.ecx+0x26u,1u);
    }
    r.eax=R32(f-0x3cu)+6u;r.edx=f-0x64u;r.ecx=R32(f-0x38u);
    a[0]=r.eax;a[1]=r.ecx;a[2]=f-0x64u;ht_call(g,0x47fa20u,a,3,&r);
    /* shield_geometry(A+222h, A, M, [S+4], &damage). */
    r.ecx=f-0x64u;r.edx=R32(s+4u);r.eax=A+0x222u;
    a[0]=A+0x222u;a[1]=A;a[2]=f-0x64u;a[3]=r.edx;a[4]=f-0x32u;ht_call(g,0x413cc4u,a,5,&r);
    W32(f-0x30u,r.eax);
    if(!R32(f-0x30u))return pair(R32(f-0x30u),R32(f-0x24u));
    r.eax=A+0x92u;W8(A+0x94u,0);
    r.edx=sx(R16(f-0x26u));r.ecx=sx(R16(f-0x32u));r.edx-=r.ecx;
    W32(A+0x288u,R32(A+0x288u)+r.edx);
    if(!R16(f-0x32u)) {
        /* Fully absorbed: explosion at (0, depth, 0). */
        r.eax=R32(f-0x30u);
        r.edx=0;W32(f-0x7cu,0);W32(f-0x78u,r.eax);
        r.ecx=0;r.edx=f-0x70u;W32(f-0x74u,0);
        r.ecx=f-0x7cu;r.eax=R32(s);
        a[0]=f-0x7cu;a[1]=r.eax;a[2]=f-0x70u;ht_call(g,0x48043cu,a,3,&r);
        if(ht_alloc(m,g,f,&r)) {
            W16(f-0x14u,0x14u);
            r.edx=f-0x70u;ht_rng(g,&r);
            r.eax=lo(r.eax,r.eax&3u);
            r.ecx=sx(r.eax);r.eax=R32(p+0xau);
            r.edx=lo(r.edx,R16(r.eax+2u*r.ecx));
            r.ecx=R32(f-4u);
            a[0]=r.ecx;a[1]=r.edx;a[2]=f-0x70u;a[3]=A;a[4]=1;ht_call(g,0x407f1cu,a,5,&r);
            r.eax=ht_uncount(m,g);
            W16(f-0x14u,8u);
        }
        return pair(R32(f-0x30u),R32(f-0x24u));
    }
    /* Penetrating: the player's shield warning below 500. */
    r.eax=A+0x92u;
    if(R8(A+0xa3u)&&!R8(A+0xb0u)) {
        r.edx=lo(r.edx,R16(A+0x224u));
        r.ecx=sx(r.edx);
        r.eax=A+0x222u;W32(f-0x44u,r.eax);
        r.edx=R32(f-0x44u);
        r.eax=lo(r.eax,R16(r.edx));
        r.edx=sx(r.eax);
        r.ecx+=r.edx;
        if(fx_s32(r.ecx)<0x1f4) {
            a[0]=0x15u;ht_call(g,0x420a68u,a,1,&r);r.ecx=0x15u;
            r.eax=A+0x92u;W8(A+0xb0u,1u);
        }
    }
    r.edx=R32(A+0x20eu);r.ecx=f-0x64u;r.eax=R32(A+0x1f6u);
    a[0]=r.eax;a[1]=A;a[2]=s;a[3]=f-0x64u;a[4]=r.edx;ht_call(g,0x40c9d4u,a,5,&r);
    r.edx=0;W32(f-0x2cu,r.eax);W32(f-0x30u,0);
    if(!R32(f-0x2cu))return pair(R32(f-0x30u),R32(f-0x24u));
    /* COL hit: the hit word without + 1, the part hit at (0, word, 0). */
    r.ecx=R32(f-0x2cu);r.eax=R32(r.ecx);W32(f-0x30u,r.eax);
    r.edx=sx(R16(p+4u));W32(A+0x288u,R32(A+0x288u)+r.edx);
    r.eax=R32(f-0x30u);
    r.edx=0;W32(f-0x88u,0);W32(f-0x84u,r.eax);
    r.ecx=0;r.edx=f-0x70u;W32(f-0x80u,0);
    r.ecx=f-0x88u;r.eax=R32(s);
    a[0]=f-0x88u;a[1]=r.eax;a[2]=f-0x70u;ht_call(g,0x48043cu,a,3,&r);
    r.eax=f-0x70u;r.edx=R32(f-0x2cu);r.ecx=sx(R16(r.edx+4u));
    a[0]=A;a[1]=r.ecx;a[2]=p;a[3]=f-0x70u;ht_call(g,0x4188c8u,a,4,&r);
    return pair(R32(f-0x30u),R32(f-0x24u));
}

/* Original: DBSIM.EXE 0x421c8c. Frame f: point f-74h, transformed point
 * f-68h, composed matrix f-5Ch, debris word f-3Ah, debris count word
 * f-38h, explosion type f-36h, copies f-34h/f-30h, result f-2Ch, damage
 * word f-26h, record f-24h (context word f-14h), object f-4. */
uint64_t es2_flyer_hit_test_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t F,uint32_t p,const uint32_t entry[3]) {
    uint32_t f=g->scratch+ES2_HIT_TEST_FRAME,a[5];
    es2_weapon_registers r={VA(g,0x49a4ccu),entry[1],entry[2]};
    ht_call(g,0x49630cu,NULL,0,&r);
    r.edx=p;
    const uint32_t s=R32(p);
    /* The view subject is exempt while [0x49ef34] is set. */
    if(R32(VA(g,0x49ef34u))&&F==R32(VA(g,0x4d2684u)))return pair(0,R32(f-0x24u));
    /* reach = s16(slot +10h) + s16(S+8) + [S+4]; EAX is 0x49630c's. */
    r.ecx=R32(F);a[0]=F;ht_virtual(g,R32(r.ecx+0x10u),0x10,a,1,&r);
    uint32_t reach=sx(r.eax);
    r.eax=sx(R16(s+8u))+R32(s+4u);
    r.ecx=F;r.edx=F+0x26u;
    reach+=r.eax;
    r.ecx=R32(s)+0x14u;
    a[0]=r.ecx;a[1]=F+0x26u;ht_call(g,0x49288cu,a,2,&r);
    if(fx_s32(reach)<fx_s32(r.eax))return pair(0,R32(f-0x24u));
    r.ecx=p;r.edx=0;
    r.eax=lo(r.eax,R16(p+4u));
    r.ecx=s+0xau;
    W16(f-0x26u,r.eax);W32(f-0x2cu,r.edx);W32(f-0x30u,r.ecx);
    if(!R16(F+0x32u)) {
        W32(f-0x34u,F+0xcu);
        r.eax=F+0x12u;r.edx=R32(f-0x34u);
        a[0]=r.edx;a[1]=F+0x12u;ht_call(g,0x47ebb8u,a,2,&r);
        W16(F+0x32u,1u);
    }
    r.eax=F+0x12u;r.edx=f-0x5cu;r.ecx=R32(f-0x30u);
    a[0]=F+0x12u;a[1]=r.ecx;a[2]=f-0x5cu;ht_call(g,0x47fa20u,a,3,&r);
    const uint32_t flags=R32(F+0x208u);
    r.edx=f-0x5cu;r.ecx=R32(F+0x1fau);r.eax=R32(r.ecx+0x32u);
    a[0]=r.eax;a[1]=F;a[2]=s;a[3]=f-0x5cu;a[4]=flags;ht_call(g,0x40c9d4u,a,5,&r);
    const uint32_t h=r.eax;
    if(!h)return pair(R32(f-0x2cu),R32(f-0x24u));
    /* Explosion type, then slot +74h (part damage) with the part word in
     * P's low half and the damage word in that of the result. */
    ht_rng(g,&r);
    r.eax=sx(r.eax&3u);
    r.edx=p;r.ecx=R32(p+0xau);
    r.eax=lo(r.eax,R16(r.ecx+2u*r.eax+0x10u));
    W16(f-0x36u,r.eax);W16(f-0x38u,1u);
    r.edx=R32(h)+1u;W32(f-0x2cu,r.edx);
    r.ecx=p;r.eax=R32(p+0xeu);a[3]=r.eax;
    r.edx=lo(r.edx,R16(f-0x26u));a[2]=r.edx;
    r.ecx=lo(r.ecx,R16(h+4u));a[1]=r.ecx;
    a[0]=F;r.eax=R32(F);ht_virtual(g,R32(r.eax+0x74u),0x74,a,4,&r);
    r.eax=F+0x92u;
    if(R8(F+0x99u)) { W16(f-0x36u,0xau);W16(f-0x38u,3u); }
    r.eax=R32(f-0x2cu);
    r.edx=0;W32(f-0x74u,0);W32(f-0x70u,r.eax);
    r.ecx=0;r.edx=f-0x68u;W32(f-0x6cu,0);
    r.ecx=f-0x74u;r.eax=R32(s);
    a[0]=f-0x74u;a[1]=r.eax;a[2]=f-0x68u;ht_call(g,0x48043cu,a,3,&r);
    if(ht_alloc(m,g,f,&r)) {
        /* Owner 0, unlike BASE and MECH. */
        W16(f-0x14u,0x14u);
        r.edx=f-0x68u;r.ecx=lo(r.ecx,R16(f-0x36u));r.eax=R32(f-4u);
        a[0]=r.eax;a[1]=r.ecx;a[2]=f-0x68u;a[3]=0;a[4]=1;ht_call(g,0x407f1cu,a,5,&r);
        r.edx=ht_uncount(m,g);
        W16(f-0x14u,8u);
    }
    r.ecx=lo(r.ecx,R16(f-0x38u));W16(f-0x3au,r.ecx);
    r.eax=f-0x3au;a[0]=f-0x3au;ht_call(g,0x4089bcu,a,1,&r);
    r.ecx=f-0x3au;
    const uint32_t record=r.eax;
    r.edx=f-0x68u;
    r.ecx=R32(record+6u);const uint32_t shapes=r.ecx;
    r.edx=sx(R16(f-0x3au));r.ecx=r.edx;
    r.eax=R32(record);r.ecx=10u*r.ecx;r.eax+=r.ecx;
    a[0]=r.eax;a[1]=shapes;a[2]=f-0x68u;a[3]=0;ht_call(g,0x408530u,a,4,&r);
    return pair(R32(f-0x2cu),R32(f-0x24u));
}
