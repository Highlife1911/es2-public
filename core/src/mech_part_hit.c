#include "es2/core/mech_part_hit.h"
#include "fixed.h"
#include <stddef.h>
/* MECH part hit (docs/reverse-engineering/hit_tests.md, the
 * section "MECH part hit"). r holds what the original's EAX/EDX/ECX hold at the
 * next game call; lo() is a 16-bit load or operation that keeps the register's
 * high word. Frame locals live in the scratch frame at the original EBP offsets
 * (f = the synthetic EBP): the damage word f-26h, the bracket words f-28h
 * (before), f-2Eh/f-30h (1 = unchanged, 2 = changed), the condition f-32h, the
 * "full" flag f-38h, n f-3Ah/f-3Ch, the weapon f-40h, the item pair f-48h, the
 * debris word f-4Ah, the object f-4; the runtime record at f-24h (context word
 * f-14h). */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(g,x) ((g)->image+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes its resolved target and the
 * vtable offset. Registers are updated in place. */
static void ph_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}
static void ph_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* Original: DBSIM.EXE 0x410670. The loop test compares DX with the count
 * word (signed); the kind test compares DI, the sign-extended byte, with
 * the argument word. */
void es2_weapon_by_kind(const es2_guest_memory *m,uint32_t system,uint32_t word,es2_weapon_registers *out) {
    uint32_t found=0,index=0,slot=R32(system+4u);
    while(fx_s16(index)<fx_s16(R16(system+8u))) {
        out->ecx=R32(slot);
        if(out->ecx&&(uint16_t)(int32_t)(int8_t)R8(R32(out->ecx+0x18u)+0x17u)==(uint16_t)word) {
            found=out->ecx;break;
        }
        ++index;slot+=4u;
    }
    out->eax=found;out->edx=index;
}

/* Original: DBSIM.EXE 0x426ac8 */
void es2_results_append(const es2_guest_memory *m,uint32_t image,uint32_t item,es2_weapon_registers *out) {
    const uint32_t count=image+0xa9e6au;
    out->eax=sx(R16(count));
    out->edx=R32(image+0xa9e6cu);
    out->ecx=R32(item);
    W32(out->edx+4u*out->eax,out->ecx);
    W16(count,R16(count)+1u);
}

/* Original: DBSIM.EXE 0x4188c8 */
uint64_t es2_mech_part_hit_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t A,uint32_t part,uint32_t P,uint32_t point,const uint32_t entry[3]) {
    const uint32_t f=g->scratch+ES2_MECH_PART_HIT_FRAME,state=VA(g,0x4d2599u);
    uint32_t a[5];
    es2_weapon_registers r={VA(g,0x49a108u),entry[1],entry[2]};
    ph_call(g,0x49630cu,NULL,0,&r);
    /* 4188e1: damage and splash factor; splash = multiply_q10. */
    r.edx=P;r.ecx=lo(r.ecx,R16(P+4u));W16(f-0x26u,r.ecx);
    uint32_t edi=sx(R16(P+8u));
    r.eax=sx(R16(f-0x26u));W32(f-0x2cu,r.eax);r.edx=R32(f-0x2cu);
    a[0]=edi;a[1]=r.edx;ph_call(g,0x47e0b0u,a,2,&r);
    edi=r.eax;
    /* 418909: the condition before; its bracket (>> 5). */
    r.ecx=A+0x206u;a[0]=A+0x206u;a[1]=part;ph_call(g,0x40dbc0u,a,2,&r);
    W16(f-0x32u,r.eax);
    r.eax=fx_sar(sx(R16(f-0x32u)),5);
    W16(f-0x28u,r.eax);
    /* 418928: slot +74h (A, part, damage - splash, owner). */
    r.edx=P;r.ecx=R32(P+0xeu);
    r.eax=lo(r.eax,R16(f-0x26u)-edi);
    a[0]=A;a[1]=part;a[2]=r.eax;a[3]=r.ecx;
    r.edx=R32(A);ph_virtual(g,R32(r.edx+0x74u),0x74u,a,4,&r);
    if((uint16_t)edi) { /* 418944: only DI is tested */
        r.ecx=P;r.eax=R32(P+0xeu);r.edx=point;
        a[0]=A;a[1]=edi;a[2]=point;a[3]=0x1f4u;a[4]=r.eax;
        r.ecx=R32(A);ph_virtual(g,R32(r.ecx+0x70u),0x70u,a,5,&r);
    }
    /* 418960: the condition after. */
    r.eax=A+0x206u;a[0]=A+0x206u;a[1]=part;ph_call(g,0x40dbc0u,a,2,&r);
    const uint32_t after=r.eax;
    r.eax=fx_sar(sx(after),5);
    r.edx=(uint16_t)after==0x100u;W32(f-0x38u,r.edx);
    if((uint16_t)r.eax==R16(f-0x28u)) { /* 41898a */
        W16(f-0x2eu,1u);W16(f-0x30u,1u);
    } else {
        W16(f-0x2eu,2u);W16(f-0x30u,2u);
        r.eax=A+0x92u;
        if(R8(r.eax+0x11u)&&fx_s16(after)<100&&(part==0||part==1)) { /* 4189b3, 4189b9, 4189bd, 4189c2 */
            a[0]=0;ph_call(g,0x434010u,a,1,&r);
            const int over=fx_s16(R16(f-0x32u))>100; /* 4189d1 */
            r.ecx=0;
            if(over){a[0]=0x12u;ph_call(g,0x420a68u,a,1,&r);r.ecx=0x12u;}
        }
        if(!R32(f-0x38u)) { /* 4189df */
            r.edx=R32(A+0x1f2u);
            if(R16(r.edx+0x56u)&&fx_s32(part)>=19) { /* 4189f0, 4189f9: a weapon-mount part */
                W16(f-0x3au,10u);
                r.ecx=R32(A+0x45u);
                if(!R8(r.ecx+0x12u))W16(f-0x3au,3u); /* 418a0c */
                r.eax=lo(r.eax,R16(f-0x3au));W16(f-0x3cu,r.eax);
                a[0]=state;ph_call(g,0x492ee0u,a,1,&r);
                r.ecx=sx(R16(f-0x3cu));
                r.edx=sx(r.eax)&0xfffu;
                r.eax=r.ecx+8u*(5u*r.ecx);
                r.edx=fx_s32(r.edx)<fx_s32(r.eax);
                if((uint16_t)r.edx) { /* 418a45: the weapon is destroyed */
                    r.eax=lo(part,part-19u);
                    r.edx=R32(A+0x202u);
                    es2_weapon_by_kind(m,r.edx,r.eax,&r);
                    W32(f-0x40u,r.eax);
                    r.ecx=R32(f-0x40u);
                    a[0]=r.ecx;a[1]=A;a[2]=1;ph_call(g,0x40f57cu,a,3,&r);
                    r.eax=part;r.ecx=R32(A+0x20eu);r.edx=sx(part);
                    W16(r.ecx+2u*r.edx,0);
                    r.eax=R32(A+0x45u);r.edx=(uint32_t)(int32_t)(int8_t)R8(r.eax+0x12u);
                    if((uint16_t)r.edx==1u) { /* 418a8f: the results-list item */
                        r.ecx=sx(after);W32(f-0x44u,r.ecx);r.eax=R32(f-0x44u);
                        a[0]=0x1f4u;a[1]=r.eax;ph_call(g,0x47e0b0u,a,2,&r);
                        r.edx=r.eax;r.eax=lo(r.eax,0x100u);
                        r.eax=sx(r.eax-r.edx);
                        r.ecx=R32(f-0x40u);
                        r.eax<<=2;
                        r.edx=R32(r.ecx+0x1cu);
                        r.ecx=lo(r.ecx,R16(r.edx+0x56u));
                        r.eax*=5u;
                        W16(f-0x48u,r.ecx);
                        r.edx=f-0x48u;
                        r.eax=fx_sar(r.eax*5u,8);
                        W16(f-0x46u,r.eax);
                        es2_results_append(m,g->image,f-0x48u,&r);
                        r.ecx=f-0x48u;
                    }
                    r.ecx=A+0x206u;
                    a[0]=A+0x206u;a[1]=part;a[2]=0x2710u;ph_call(g,0x40da38u,a,3,&r);
                }
            }
        }
    }
    /* 418af1: the explosion. */
    const uint32_t pool=R32(VA(g,0x4a961eu));
    r.eax=pool;a[0]=pool;ph_call(g,0x471b30u,a,1,&r);
    r.ecx=pool;W32(f-4u,r.eax);
    if(r.eax) { /* 418b02 */
        W16(f-0x14u,0x14u);
        r.edx=point;
        const uint32_t si=R16(f-0x2eu);
        a[0]=state;ph_call(g,0x492ee0u,a,1,&r);
        r.eax=lo(r.eax,r.eax&3u);
        r.ecx=sx(r.eax);
        r.eax=sx(si);r.edx=P;r.eax<<=2;r.ecx+=r.eax;
        r.eax=R32(P+0xau);
        r.edx=lo(r.edx,R16(r.eax+2u*r.ecx));
        r.ecx=R32(f-4u);
        a[0]=r.ecx;a[1]=r.edx;a[2]=point;a[3]=A;a[4]=1;ph_call(g,0x407f1cu,a,5,&r);
        r.eax=R32(VA(g,0x6cbee4u));W32(r.eax,R32(r.eax)-4u);
        W16(f-0x14u,8u);
    }
    /* 418b54: debris after a bracket change below 100h. */
    if(!R32(f-0x38u)&&R16(f-0x30u)!=1u) { /* 418b58, 418b5f */
        r.eax=lo(r.eax,R16(f-0x30u));
        W16(f-0x4au,r.eax);
        r.edx=f-0x4au;
        a[0]=f-0x4au;ph_call(g,0x4089bcu,a,1,&r);
        const uint32_t record=r.eax,shapes=R32(record+6u);
        r.edx=sx(R16(f-0x4au));
        r.ecx=10u*r.edx;
        r.eax=R32(record)+r.ecx;
        a[0]=r.eax;a[1]=shapes;a[2]=point;a[3]=0;ph_call(g,0x408530u,a,4,&r);
    }
    return pair(R32(f-0x24u),r.edx);
}
