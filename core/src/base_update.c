#include "es2/core/base_update.h"
#include "fixed.h"
#include <stddef.h>
/* Building update (docs/reverse-engineering/vtables.md, section "base_update:
 * contract"). r holds what the original's EAX/EDX/ECX hold at the next game
 * call; lo() is a 16-bit load that keeps the register's high word. B is the
 * building, C its configuration [B+1F2h], P the runtime part records [B+205h]
 * (stride 0Bh), Q the configuration part records [C+14h] (stride 1Eh), M the
 * shape instance [B+34h]. */
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
static void bu_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}
static void bu_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* The runtime's construction count: the dword [0x6cbee4] points to; the
 * pointer ends in the register the original uses for it. */
static uint32_t bu_uncount(const es2_guest_memory *m,const es2_weapon_graph *g) {
    uint32_t counter=R32(VA(g,0x6cbee4u));
    W32(counter,R32(counter)-4u);
    return counter;
}

/* Original: DBSIM.EXE 0x403668 */
void es2_base_parts_done(const es2_guest_memory *m,uint32_t b,es2_weapon_registers *out) {
    uint32_t j=0,config=R32(R32(b+0x1f2u)+0x14u),runtime=R32(b+0x205u);
    while(fx_s32(j)<fx_s16(R16(R32(b+0x1f2u)+0x12u))) {
        if(fx_s16(R16(config+6u))>-1&&fx_s16(R16(runtime))<fx_s16(R16(config)))break;
        ++j;runtime+=0xbu;config+=0x1eu;
    }
    out->ecx=sx(R16(R32(b+0x1f2u)+0x12u));
    out->eax=j==out->ecx;
    out->edx=j;
}

/* Original: DBSIM.EXE 0x403ca8 */
uint64_t es2_base_update_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t b,const uint32_t entry[3]) {
    uint32_t a[2];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    a[0]=b;bu_call(g,0x403914u,a,1,&r);
    r.ecx=b;r.eax=b+0x92u;
    if(R8(b+0x99u))return pair(0,r.edx);
    uint32_t c=R32(b+0x1f2u);r.edx=c;
    if(fx_s16(R16(c+0x24u))>-1) {
        r.ecx=b+0x1f6u;a[0]=r.ecx;bu_call(g,0x467a48u,a,1,&r);
        r.ecx=b+0x1f6u;
        uint16_t left=(uint16_t)r.eax;r.eax=!left;
        if(!left) {
            c=R32(b+0x1f2u);W16(b+0x1f7u,R16(c+0x26u));
            c=R32(b+0x1f2u);uint32_t shape=R32(b+0x34u);
            r.ecx=lo(r.ecx,R16(c+0x24u));
            uint32_t slot=(uint16_t)r.ecx;
            uint32_t frames=R32(shape+8u),node=R32(shape+4u);
            uint32_t next=(uint32_t)R16(frames+2u*slot)+1u;
            uint32_t counts=R32(node+0x20u),count=R16(counts+2u*slot);
            /* Signed IDIV of two positive values; a zero count faults in the
             * original (precondition). */
            uint32_t rest=next%count;
            W16(R32(shape+8u)+2u*slot,rest);
            r.eax=slot;r.edx=R32(shape+8u);
        }
    }
    r.ecx=R32(b+0x1f2u);
    if(R16(r.ecx+6u)) {
        a[0]=b;a[1]=0x64u;bu_call(g,0x402604u,a,2,&r);
    }
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x403914. Frame f: input point f-64h, transformed
 * point f-58h, runtime record f-2Ch (context word f-1Ch), object slots
 * f-0Ch, f-8, f-4. The detail byte [0x4d1f43] is read once at entry. */
uint64_t es2_base_parts_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t b,const uint32_t entry[3]) {
    uint32_t f=g->scratch+ES2_BASE_FRAME,a[5];
    es2_weapon_registers r={VA(g,0x497485u),entry[1],entry[2]};
    bu_call(g,0x49630cu,NULL,0,&r);
    uint32_t p=R32(b+0x205u),q=R32(R32(b+0x1f2u)+0x14u);
    uint32_t detail=R8(VA(g,0x4d1f43u));r.eax=detail;
    for(uint32_t i=0;;++i,p+=0xbu,q+=0x1eu) {
        uint32_t c=R32(b+0x1f2u);r.edx=c;r.ecx=sx(R16(c+0x12u));
        if(!(fx_s32(r.ecx)>fx_s32(i)))break;
        if(!R16(p+5u))continue;
        r.ecx=p+2u;a[0]=r.ecx;bu_call(g,0x467a48u,a,1,&r);
        uint16_t left=(uint16_t)r.eax;r.ecx=p+2u;r.eax=!left;
        if(left)continue;
        W16(p+5u,R16(p+5u)-1u);
        if(!R16(p+5u)) {
            /* Stage 0: sink, the object's FIRE or the part's FIRE. */
            c=R32(b+0x1f2u);r.eax=c;
            if(R16(c+4u)==0xffffu&&R16(q+2u)==0xffffu&&R16(c+0x12u)==1u) {
                r.eax=b+0x38u;W8(b+0x38u,1);W32(b+0x2eu,0xfffe7960u);
                continue;
            }
            c=R32(b+0x1f2u);r.edx=c;
            if(fx_s16(R16(c+8u))>-1&&fx_s16(R16(q+6u))>-1) {
                es2_base_parts_done(m,b,&r);
                r.ecx=b;
                if(!r.eax)continue;
                a[0]=0x5bu;bu_call(g,0x46b430u,a,1,&r);
                r.ecx=0x5bu;W32(f-4u,r.eax);
                if(!r.eax)continue;
                W16(f-0x1cu,0x14u);
                c=R32(b+0x1f2u);r.edx=c;r.ecx=lo(r.ecx,R16(c+8u));
                r.eax=R32(b+0x1f2u)+0xau;r.edx=R32(f-4u);
                a[0]=r.edx;a[1]=b;a[2]=0xffffffffu;a[3]=r.eax;a[4]=r.ecx;bu_call(g,0x46b48cu,a,5,&r);
                r.ecx=bu_uncount(m,g);
                W16(f-0x1cu,8u);
            } else if(fx_s16(R16(q+6u))>-1) {
                a[0]=0x5bu;bu_call(g,0x46b430u,a,1,&r);
                r.ecx=0x5bu;W32(f-8u,r.eax);
                if(!r.eax)continue;
                W16(f-0x1cu,0x2cu);
                r.edx=lo(r.edx,R16(q+6u));r.ecx=q+0xau;r.eax=R32(f-8u);
                a[0]=r.eax;a[1]=b;a[2]=0xffffffffu;a[3]=r.ecx;a[4]=r.edx;bu_call(g,0x46b48cu,a,5,&r);
                r.edx=bu_uncount(m,g);
                W16(f-0x1cu,0x20u);
            }
            continue;
        }
        if(R16(p+5u)==1u) {
            /* Stage 1: blast, debris, then the destroyed model or the part's
             * frame and the dependent parts. */
            es2_base_parts_done(m,b,&r);
            r.ecx=b;
            uint32_t done=r.eax;
            c=R32(b+0x1f2u);r.eax=c;
            if(fx_s16(R16(c+8u))>-1&&fx_s16(R16(q+6u))>-1&&done) {
                r.edx=c+0xau;r.ecx=sx(R16(c+0x10u));
                a[0]=b;a[1]=p;a[2]=r.ecx;a[3]=r.edx;bu_call(g,0x4036c0u,a,4,&r);
            } else {
                r.eax=lo(r.eax,R16(q+4u));
                if(fx_s16(r.eax)>-1) {
                    r.edx=q+0xau;r.ecx=sx(r.eax);
                    a[0]=b;a[1]=p;a[2]=r.ecx;a[3]=r.edx;bu_call(g,0x4036c0u,a,4,&r);
                }
            }
            a[0]=b;a[1]=q;bu_call(g,0x40379cu,a,2,&r);
            if(done) {
                r.eax=R32(b+0x1f2u);
                if(fx_s16(R16(r.eax+4u))>-1) {
                    c=R32(b+0x1f2u);uint32_t models=R32(VA(g,0x4a9584u));
                    r.ecx=sx(R16(c+4u));r.edx=R32(b+0x34u);r.eax=R32(models+4u*r.ecx);
                    W32(r.edx+4u,r.eax);
                    continue;
                }
            }
            r.eax=lo(r.eax,R16(q+2u));
            if(fx_s16(r.eax)<=-1)continue;
            r.edx=(uint16_t)r.eax;r.ecx=R32(b+0x34u);r.eax=R32(r.ecx+8u);
            W16(r.eax+2u*r.edx,1u);
            r.edx=i;a[0]=b;a[1]=i;bu_call(g,0x403890u,a,2,&r);
            continue;
        }
        /* Stage >= 2 (or below 0): a smoke puff when the detail byte is 2,
         * or 1 and the stage odd; stage 4 also damages the dependents. */
        if(detail==2u||(detail==1u&&(R8(p+5u)&1u))) {
            uint32_t d[3];
            for(unsigned k=0;k<3;++k) {
                r.ecx=lo(r.ecx,R16(q+0x16u+2u*k));r.ecx+=r.ecx;
                a[0]=VA(g,0x4d2599u);a[1]=r.ecx;bu_call(g,0x492f24u,a,2,&r);
                r.edx=sx(R16(q+0x16u+2u*k));r.eax=sx(r.eax)-r.edx;d[k]=r.eax;
            }
            r.eax=q+0x10u;
            r.edx=sx(R16(q+0x14u));d[2]+=r.edx;
            r.ecx=sx(R16(q+0x10u));d[0]+=r.ecx;
            r.eax=sx(R16(q+0x12u));d[1]+=r.eax;
            for(unsigned k=0;k<3;++k)W32(f-0x64u+4u*k,d[k]);
            r.edx=b+0xcu;r.ecx=b+0xcu;
            if(!R16(b+0x32u)) {
                r.eax=b+0xcu;r.edx=b+0x12u;r.ecx=b+0xcu;
                a[0]=b+0xcu;a[1]=b+0x12u;bu_call(g,0x47ebb8u,a,2,&r);
                W16(b+0x32u,1u);
            }
            r.eax=b+0x12u;r.edx=f-0x58u;r.ecx=f-0x64u;
            a[0]=f-0x64u;a[1]=b+0x12u;a[2]=f-0x58u;bu_call(g,0x48043cu,a,3,&r);
            uint32_t pool=R32(VA(g,0x4a961eu));
            r.eax=pool;a[0]=pool;bu_call(g,0x471b30u,a,1,&r);
            r.ecx=pool;W32(f-0xcu,r.eax);
            if(r.eax) {
                W16(f-0x1cu,0x44u);
                r.edx=f-0x58u;r.ecx=sx(R16(q+4u));
                r.eax=lo(r.eax,R16(VA(g,0x497414u)+2u*r.ecx));r.edx=R32(f-0xcu);
                a[0]=r.edx;a[1]=r.eax;a[2]=f-0x58u;a[3]=b;a[4]=1;bu_call(g,0x407f1cu,a,5,&r);
                r.ecx=bu_uncount(m,g);
                W16(f-0x1cu,0x38u);
            }
            W16(p+3u,0x12cu);
        }
        if(R16(p+5u)==4u) {
            r.eax=i;a[0]=b;a[1]=i;bu_call(g,0x403890u,a,2,&r);
        }
    }
    return pair(R32(f-0x2cu),r.edx);
}

/* Original: DBSIM.EXE 0x403890. ECX after return is the walk's record
 * pointer (popped frame local, dead at both callers). */
uint64_t es2_base_part_chain_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t b,uint32_t k,const uint32_t entry[3]) {
    uint32_t a[4],runtime=R32(b+0x205u),config=R32(R32(b+0x1f2u)+0x14u);
    uint32_t parent=runtime+11u*k;
    es2_weapon_registers r={parent,entry[1],entry[2]};
    for(uint32_t j=0;;++j,runtime+=0xbu,config+=0x1eu) {
        r.edx=R32(b+0x1f2u);r.ecx=sx(R16(r.edx+0x12u));
        if(!(fx_s32(j)<fx_s32(r.ecx)))break;
        r.eax=sx(R16(config+0x1cu));
        if(r.eax!=k)continue;
        r.eax=lo(r.eax,R16(config));r.ecx=lo(r.ecx,R16(runtime));
        if(fx_s16(r.ecx)>=fx_s16(r.eax))continue;
        r.edx=parent;r.ecx=R32(parent+7u);
        a[0]=b;a[1]=j;a[2]=r.eax;a[3]=r.ecx;
        r.eax=R32(b);bu_virtual(g,R32(r.eax+0x74u),0x74u,a,4,&r);
        a[0]=b;a[1]=j;bu_call(g,0x403890u,a,2,&r);
    }
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4036c0. Frame f: point f-38h, B+0Ch copy f-2Ch,
 * type f-28h, runtime record f-24h (context word f-14h), object slot f-4.
 * Kind indexes the image tables 4973fch (explosion type), 49740ch (at the
 * object's position) and 497404h (re-arm time). */
uint64_t es2_base_part_blast_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t b,uint32_t runtime,uint32_t kind,uint32_t offset,const uint32_t entry[3]) {
    uint32_t f=g->scratch+ES2_BASE_FRAME,a[5];
    es2_weapon_registers r={VA(g,0x497435u),entry[1],entry[2]};
    bu_call(g,0x49630cu,NULL,0,&r);
    r.edx=sx(R16(VA(g,0x4973fcu)+2u*kind));W32(f-0x28u,r.edx);
    if(R16(VA(g,0x49740cu)+2u*kind)) {
        r.eax=b+0x26u;
        r.edx=R32(b+0x2eu);W32(f-0x30u,r.edx);
        r.ecx=R32(b+0x26u);W32(f-0x38u,r.ecx);
        r.eax=R32(b+0x2au);W32(f-0x34u,r.eax);
    } else {
        if(!R16(b+0x32u)) {
            W32(f-0x2cu,b+0xcu);r.eax=b+0x12u;r.edx=b+0xcu;
            a[0]=b+0xcu;a[1]=b+0x12u;bu_call(g,0x47ebb8u,a,2,&r);
            W16(b+0x32u,1u);
        }
        r.eax=b+0x12u;r.edx=offset;r.ecx=f-0x38u;
        a[0]=offset;a[1]=b+0x12u;a[2]=f-0x38u;bu_call(g,0x4801d4u,a,3,&r);
    }
    uint32_t pool=R32(VA(g,0x4a961eu));
    r.eax=pool;a[0]=pool;bu_call(g,0x471b30u,a,1,&r);
    r.ecx=pool;W32(f-4u,r.eax);
    if(r.eax) {
        W16(f-0x14u,0x14u);
        r.ecx=lo(r.ecx,R16(f-0x28u));r.edx=f-0x38u;r.eax=R32(f-4u);
        a[0]=r.eax;a[1]=r.ecx;a[2]=f-0x38u;a[3]=b;a[4]=1;bu_call(g,0x407f1cu,a,5,&r);
        r.edx=bu_uncount(m,g);
        W16(f-0x14u,8u);
    }
    r.eax=lo(r.eax,R16(VA(g,0x497404u)+2u*kind));
    W16(runtime+3u,r.eax);
    return pair(r.eax,runtime);
}

/* Original: DBSIM.EXE 0x40379c. Frame f: point f-14h, the three debris
 * words f-2, f-4, f-6 (0x4089bc may lower each before it is read). */
uint64_t es2_base_part_debris_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t b,uint32_t q,const uint32_t entry[3]) {
    uint32_t f=g->scratch+ES2_BASE_FRAME,a[4];
    es2_weapon_registers r={b,entry[1],entry[2]};
    if(!R16(b+0x32u)) {
        r.eax=b+0x12u;a[0]=b+0xcu;a[1]=b+0x12u;bu_call(g,0x47ebb8u,a,2,&r);
        W16(b+0x32u,1u);
    }
    r.eax=b+0x12u;r.edx=q+0xau;r.ecx=f-0x14u;
    a[0]=q+0xau;a[1]=b+0x12u;a[2]=f-0x14u;bu_call(g,0x4801d4u,a,3,&r);
    W32(VA(g,0x4a964cu),VA(g,0x4a95c0u));
    r.eax=lo(r.eax,R16(q+8u));W16(f-2u,r.eax);
    static const uint16_t words[3]={0,0xa,0xc};
    for(unsigned k=0;k<3;++k) {
        uint32_t w=f-2u-2u*k;
        if(k) {
            if(k==1&&fx_s16(R16(q+8u))<=5)break;
            W16(w,words[k]);r.eax=w;
        } else r.edx=w;
        a[0]=w;bu_call(g,0x4089bcu,a,1,&r);
        r.ecx=w;
        uint32_t record=r.eax,spread=R32(record+6u);
        r.ecx=sx(R16(w));r.edx=r.ecx;r.eax=R32(record);r.edx=10u*r.ecx;r.eax+=r.edx;
        a[0]=r.eax;a[1]=spread;a[2]=f-0x14u;a[3]=0;bu_call(g,0x408530u,a,4,&r);
    }
    return pair(r.eax,r.edx);
}
