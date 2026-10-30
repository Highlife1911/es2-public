#include "es2/core/region_damage.h"
#include "fixed.h"
/* Region-damage helpers (docs/reverse-engineering/hit_tests.md, section
 * "region-damage helpers"). Guest memory is read and written in the original's
 * order; r holds what the original's EAX/EDX/ECX hold at the next game call;
 * lo() is a 16-bit load or operation that keeps the register's high word. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define VA(g,x) ((g)->image+(x)-0x400000u)
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static void put_ecx(uint32_t *ecx,uint32_t value) { if(ecx)*ecx=value; }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. Registers are updated in place. */
static void rd_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(g,va),a,n,r);
}

/* Original: DBSIM.EXE 0x40d3ec. The sum is a 32-bit signed comparison of
 * the two sign-extended words against the sign-extended limit; the
 * overflow is a 16-bit SUB. */
uint64_t es2_region_absorb_registers(const es2_guest_memory *m,uint32_t v,uint32_t limit,uint32_t d,
    uint32_t entry_ecx,uint32_t *ecx) {
    uint32_t c=lo(entry_ecx,R16(v)),edx=d;
    if(fx_s16(c)>=0) {
        c=sx(c)+sx(R16(d));
        if(fx_s32(c)>=fx_s16(limit)) {
            c=lo(c,c-limit);
            W16(d,c);
            W16(v,0xffffu);
        } else {
            edx=lo(edx,R16(d));
            W16(v,R16(v)+edx);
        }
    }
    put_ecx(ecx,c);
    return pair(fx_s16(R16(v))<0?1u:0u,edx);
}

/* Original: DBSIM.EXE 0x40cf44. The local total word is the high word of
 * the dword PUSH ECX reserved (f-4). EBX (the region pointer handed to
 * 0x40d3ec) keeps the value of the last walked entry, also when the second
 * walk runs past the end or not at all. */
uint64_t es2_region_spread_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t record,uint32_t regions,uint32_t damage,uint32_t limits,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t slot=g->scratch;
    uint32_t a[3],ebx=0,total;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    W32(slot,damage);
    for(;;) {
        /* 40cf4e: the weights of the live regions. */
        total=0;r.ecx=0;r.edx=0;r.eax=R32(record+8u);
        for(;fx_s16(r.edx)<fx_s16(R16(record+6u));++r.edx,r.eax+=4u) {
            ebx=regions+2u*sx(R16(r.eax+2u));
            if(fx_s16(R16(ebx))>=0)total=(uint16_t)(total+R16(r.eax));
        }
        if(!total)break;
        /* 40cf84: draw below the total, then walk the weights again. */
        r.eax=lo(r.eax,total);
        a[0]=VA(g,0x4d2599u);a[1]=r.eax;rd_call(g,0x492f24u,a,2,&r);
        r.ecx=r.eax;r.edx=0;r.eax=R32(record+8u);
        for(;fx_s16(r.edx)<fx_s16(R16(record+6u));++r.edx,r.eax+=4u) {
            ebx=regions+2u*sx(R16(r.eax+2u));
            if(fx_s16(R16(ebx))>=0) {
                r.ecx=lo(r.ecx,r.ecx-R16(r.eax));
                if(fx_s16(r.ecx)<0)break;
            }
        }
        /* 40cfc0: region_absorb(EBX, lo(index, limit), &damage). */
        r.edx=slot;
        const uint32_t index=sx(R16(r.eax+2u));
        r.ecx=limits;
        r.eax=lo(index,R16(limits+2u*index));
        a[0]=ebx;a[1]=r.eax;a[2]=slot;rd_call(g,0x40d3ecu,a,3,&r);
        r.ecx=r.eax;
        if(!(uint16_t)r.ecx)break;
    }
    /* 40cff2: POP ECX reloads the local dword: the total word above the
     * low word of the entry ECX that PUSH ECX stored there. */
    put_ecx(ecx,total<<16|(entry[2]&0xffffu));
    return pair(total?0u:1u,r.edx);
}

/* Original: DBSIM.EXE 0x40ca58. The entry pointer is read once, the count
 * word every step; the key is compared as a word. */
uint64_t es2_region_entry_registers(const es2_guest_memory *m,uint32_t record,uint32_t key,uint32_t out,uint32_t *ecx) {
    uint32_t eax=R32(record+6u),edx=0;
    for(;fx_s16(edx)<fx_s16(R16(record+4u));++edx,eax+=0x10u)
        if((uint16_t)key==R16(eax+6u)) {
            const uint32_t id=R16(record);
            W16(out,id);
            put_ecx(ecx,lo(record,id));
            return pair(eax,edx);
        }
    put_ecx(ecx,record);
    return pair(0,edx);
}

/* Original: DBSIM.EXE 0x40ca8c. The record pointer is read once, the count
 * word after every call. */
uint64_t es2_region_find_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t list,uint32_t key,uint32_t out,const uint32_t entry[3],uint32_t *ecx) {
    uint32_t a[3],ebx=R32(list+2u);
    es2_weapon_registers r={list,entry[1],entry[2]};
    for(uint32_t esi=0;;++esi,ebx+=10u) {
        r.edx=list;
        if(!(fx_s16(esi)<fx_s16(R16(list))))break;
        r.eax=out;r.edx=lo(r.edx,key);
        a[0]=ebx;a[1]=r.edx;a[2]=out;rd_call(g,0x40ca58u,a,3,&r);
        if(r.eax) { put_ecx(ecx,r.ecx);return pair(r.eax,r.edx); }
    }
    put_ecx(ecx,r.ecx);
    return pair(0,r.edx);
}

/* Region damage 0x40da38 and part break 0x40d434 (hit_tests.md, section "region
 * damage 40da38 and part break 40d434"). D is the damage state; R the part
 * record [[D+0Ch]] + 18*s16(part). Byte reads and writes go through the host
 * pointer. */
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
static uint32_t sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
/* A virtual call passes its resolved target and the vtable offset. */
static void rd_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* Original: DBSIM.EXE 0x40d434. Frame locals at the original offsets from
 * f: the record f-34h (context word f-24h), o f-36h, e+8 f-3Ch, the pose
 * f-C4h (translation f-B0h), the transform output f-98h, the explosion
 * type f-66h, the debris words f-68h/f-8Ah, the objects f-4/f-8/f-0Ch. R
 * is computed once (ESI); A, [D+0Ch] and the part bytes are reread where
 * the original rereads them. */
uint64_t es2_region_break_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t D,uint32_t part,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t f=g->scratch+ES2_REGION_BREAK_FRAME,p=sx(part),big=VA(g,0x4a982cu),counter=VA(g,0x6cbee4u);
    uint32_t a[5];
    es2_weapon_registers r={VA(g,0x4988e8u),entry[1],entry[2]};
    rd_call(g,0x49630cu,NULL,0,&r);
    /* 40d44d: the part record, then the part's region entry e. */
    const uint32_t R=R32(R32(D+0xcu))+18u*p;
    r.eax=p;r.edx=R32(D+0x10u);r.ecx=f-0x36u;
    a[0]=r.edx;a[1]=p;a[2]=f-0x36u;rd_call(g,0x40ca8cu,a,3,&r);
    W32(f-0x3cu,r.eax+8u);
    if(!R16(R32(D+8u)+2u*p)) { /* 40d489: already broken */
        put_ecx(ecx,r.ecx);
        return pair(0,R32(f-0x34u));
    }
    /* 40d48f: mark it broken, spread 7D00h over its regions. */
    W16(R32(D+4u)+2u*p,0xffffu);
    W16(R32(D+8u)+2u*p,0);
    W32(f-0x40u,R32(D));
    const uint32_t C=R32(D+0xcu),T=R32(C+4u);
    r.eax=R32(C)+18u*p;r.edx=18u*p;r.ecx=R32(f-0x40u);
    a[0]=r.eax;a[1]=r.ecx;a[2]=0x7d00u;a[3]=T;rd_call(g,0x40cf44u,a,4,&r);
    /* 40d4cf: fires and the state word for a part with a state index. */
    r.eax=sx8(R8(R+3u))+1u;
    if(fx_s32(r.eax)>0) { /* 40d4d4 */
        if(!R16(big)) { /* 40d4e2 */
            if(R8(R+5u)&2u) { /* 40d4ec: release the actor's fires, kind 0 */
                const uint32_t A=R32(D+0x14u);
                r.ecx=A;a[0]=A;rd_call(g,0x46b62cu,a,1,&r);
                r.ecx=A;a[0]=0x5bu;rd_call(g,0x46b430u,a,1,&r);
                r.ecx=0x5bu;W32(f-4u,r.eax);
                if(r.eax) { /* 40d505 */
                    W16(f-0x24u,0x14u);
                    a[3]=R32(f-0x3cu);r.ecx=lo(r.ecx,R16(f-0x36u));r.eax=R32(D+0x14u);r.edx=R32(f-4u);
                    a[0]=r.edx;a[1]=r.eax;a[2]=r.ecx;a[4]=0;rd_call(g,0x46b48cu,a,5,&r);
                    r.ecx=R32(counter);W32(r.ecx,R32(r.ecx)-4u);
                    W16(f-0x24u,8u);
                }
            } else if(R8(R+5u)&8u) { /* 40d53d: kind 2 */
                a[0]=0x5bu;rd_call(g,0x46b430u,a,1,&r);
                r.ecx=0x5bu;W32(f-8u,r.eax);
                if(r.eax) { /* 40d54c */
                    W16(f-0x24u,0x2cu);
                    a[3]=R32(f-0x3cu);r.ecx=lo(r.ecx,R16(f-0x36u));r.eax=R32(D+0x14u);r.edx=R32(f-8u);
                    a[0]=r.edx;a[1]=r.eax;a[2]=r.ecx;a[4]=2;rd_call(g,0x46b48cu,a,5,&r);
                    r.ecx=R32(counter);W32(r.ecx,R32(r.ecx)-4u);
                    W16(f-0x24u,0x20u);
                }
            }
        }
        /* 40d57e: the state word (the index byte reread, its low word
         * zero-extended). */
        r.eax=sx8(R8(R+3u));r.edx=(uint16_t)r.eax;
        r.ecx=R32(D+0x14u);r.eax=R32(r.ecx+0x34u);r.ecx=R32(r.eax+8u);
        W16(r.ecx+2u*r.edx,2u);
    }
    /* 40d594: the pose at f-C4h. */
    if(fx_s16(R16(f-0x36u))<0) { /* 40d599: the actor's own pose */
        const uint32_t pose=R32(D+0x14u)+0xcu;
        if(!R16(pose+0x26u)) { /* 40d5a6 */
            W32(f-0x48u,pose);
            r.eax=pose+6u;r.edx=R32(f-0x48u);
            a[0]=r.edx;a[1]=r.eax;rd_call(g,0x47ebb8u,a,2,&r);
            W16(pose+0x26u,1u);
        }
        W32(f-0x4cu,pose+6u);
        for(unsigned i=0;i<5;++i)W32(f-0xc4u+4u*i,R32(pose+6u+4u*i));
        r.edx=R32(pose+0x22u);W32(f-0xa8u,r.edx);
        r.ecx=R32(pose+0x1au);W32(f-0xb0u,r.ecx);
        r.eax=R32(pose+0x1eu);W32(f-0xacu,r.eax);
    } else { /* 40d600: the node's pose composed with the actor's */
        r.edx=lo(r.edx,R16(f-0x36u));W16(f-0x4eu,r.edx);
        r.ecx=R32(D+0x14u);W32(f-0x54u,r.ecx);
        r.eax=R32(f-0x54u);r.edx=R32(r.eax+0x34u);W32(f-0x58u,r.edx);
        r.ecx=lo(r.ecx,R16(f-0x4eu));a[1]=r.ecx;
        r.eax=R32(f-0x58u);r.edx=R32(r.eax+4u);a[0]=r.edx;
        r.ecx=R32(r.edx);rd_virtual(g,R32(r.ecx+0x20u),0x20u,a,2,&r);
        const uint32_t n=r.eax;
        if(n&&fx_s16(R16(n+4u))>-1) { /* 40d62f, 40d636 */
            r.ecx=sx(R16(n+4u))<<5;r.eax=R32(f-0x58u);r.edx=R32(r.eax+0x16u)+r.ecx;
        } else r.edx=VA(g,0x6c56a8u);
        W32(f-0x44u,r.edx);
        const uint32_t pose=R32(D+0x14u)+0xcu;
        if(!R16(pose+0x26u)) { /* 40d65c */
            W32(f-0x5cu,pose);
            r.eax=pose+6u;r.edx=R32(f-0x5cu);
            a[0]=r.edx;a[1]=r.eax;rd_call(g,0x47ebb8u,a,2,&r);
            W16(pose+0x26u,1u);
        }
        r.eax=pose+6u;r.edx=R32(f-0x44u);W32(f-0x60u,r.edx);r.ecx=f-0xc4u;
        a[1]=r.eax;a[2]=r.ecx;r.eax=R32(f-0x60u);a[0]=r.eax;
        rd_call(g,0x47fa20u,a,3,&r);
    }
    /* 40d694: the part's point through the pose; it becomes the
     * translation. The flag word is read right after the call. */
    r.edx=R32(f-0x3cu);W32(f-0x64u,r.edx);
    r.ecx=f-0x98u;r.eax=f-0xc4u;r.edx=R32(f-0x64u);
    a[0]=r.edx;a[1]=r.eax;a[2]=r.ecx;rd_call(g,0x4801d4u,a,3,&r);
    r.ecx=R32(f-0x90u);
    const uint32_t done=R16(big);
    W32(f-0xa8u,r.ecx);
    r.eax=R32(f-0x98u);W32(f-0xb0u,r.eax);
    r.edx=R32(f-0x94u);W32(f-0xacu,r.edx);
    if(!done) { /* 40d6e0: the explosion, type 11h once per cascade */
        W16(f-0x66u,0xau);
        if(R8(R+5u)&4u) { W16(big,1u);W16(f-0x66u,0x11u); } /* 40d6ec */
        r.ecx=R32(VA(g,0x4a961eu));
        a[0]=r.ecx;rd_call(g,0x471b30u,a,1,&r);
        r.ecx=a[0];W32(f-0xcu,r.eax);
        if(r.eax) { /* 40d70f */
            W16(f-0x24u,0x44u);
            r.ecx=f-0xb0u;r.edx=R32(D+0x14u);a[3]=r.edx;a[2]=r.ecx;a[4]=1;
            r.eax=lo(r.eax,R16(f-0x66u));a[1]=r.eax;
            r.edx=R32(f-0xcu);a[0]=r.edx;
            rd_call(g,0x407f1cu,a,5,&r);
            r.ecx=R32(counter);W32(r.ecx,R32(r.ecx)-4u);
            W16(f-0x24u,0x38u);
        }
    }
    /* 40d744: the debris burst at the part's pose; [Rd+6], the word, [Rd]
     * are read in that order. */
    uint32_t shapes;
    r.eax=sx8(R8(R+2u))+1u;
    if(!r.eax) { /* 40d749: the debris word -1 gives 2 */
        W16(f-0x68u,2u);
        r.edx=f-0x68u;a[0]=r.edx;rd_call(g,0x4089bcu,a,1,&r);
        r.ecx=f-0x68u;W32(f-0x6cu,r.eax);
        shapes=R32(R32(f-0x6cu)+6u);
        r.edx=sx(R16(f-0x68u));r.ecx=R32(f-0x6cu);r.eax=R32(r.ecx);
        r.ecx=10u*r.edx;r.eax+=r.ecx;
    } else {
        r.eax=sx8(R8(R+2u));W16(f-0x8au,r.eax);
        r.edx=f-0x8au;a[0]=r.edx;rd_call(g,0x4089bcu,a,1,&r);
        const uint32_t record=r.eax;
        shapes=R32(record+6u);
        r.ecx=sx(R16(f-0x8au));r.edx=r.ecx;r.eax=R32(record);
        r.edx=10u*r.edx;r.eax+=r.edx;
    }
    a[0]=r.eax;a[1]=shapes;a[2]=f-0xc4u;a[3]=0;rd_call(g,0x4086bcu,a,4,&r);
    /* 40d986: push the intact children of the part on the stack Q. */
    uint32_t i=0;
    for(;;) {
        r.edx=sx(R16(D+0x18u));
        if(!(fx_s32(i)<fx_s32(r.edx)))break; /* 40d9d3 */
        r.ecx=i;
        const uint32_t child=R32(R32(D+0xcu))+18u*sx(i);
        r.edx=child;
        if(R16(R32(D+8u)+2u*i)) { /* 40d9a7 */
            r.edx=sx8(R8(child+4u));
            if(r.edx==p) { /* 40d9b3 */
                r.edx=i;
                const uint32_t q=R32(VA(g,0x498864u)),c=R8(q+5u);
                W8(q+5u,c+1u);
                r.ecx=sx8(c);
                W16(R32(q)+2u*r.ecx,r.edx);
            }
        }
        ++i;
    }
    put_ecx(ecx,R32(f-0x34u));
    return pair(lo(i,1u),r.edx);
}

/* Original: DBSIM.EXE 0x40da38. R (EDI) is computed once; the damage word
 * lives in the scratch slot that stands for the original's own argument
 * slot, which 0x40d3ec reads and writes through the pointer. */
uint64_t es2_region_hit_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    uint32_t D,uint32_t part,uint32_t damage,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t slot=g->scratch,p=sx(part);
    uint32_t a[4];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    W32(slot,damage);
    /* 40da44: the part's word absorbs the damage up to the record's
     * limit. */
    const uint32_t R=R32(R32(D+0xcu))+18u*p;
    r.eax=lo(p,R16(R));r.edx=2u*p+R32(D+4u);r.ecx=slot;
    a[0]=r.edx;a[1]=r.eax;a[2]=slot;rd_call(g,0x40d3ecu,a,3,&r);
    if(!(uint16_t)r.eax) { put_ecx(ecx,r.ecx);return pair(lo(r.eax,0),r.edx); } /* 40da7c */
    /* 40da82: the overflow spreads over the part's regions. */
    const uint32_t left=R16(slot),V=R32(D),C=R32(D+0xcu),T=R32(C+4u);
    a[3]=T;a[2]=lo(C,left);a[1]=V;
    r.ecx=p;r.eax=18u*p;r.edx=R32(C)+r.eax;a[0]=r.edx;
    rd_call(g,0x40cf44u,a,4,&r);
    if(!(uint16_t)r.eax) { put_ecx(ecx,sx8(R8(R+3u))+1u);return pair(lo(r.eax,0),r.edx); } /* 40dac7 */
    if(!(R8(R+5u)&1u)) { put_ecx(ecx,r.ecx);return pair(lo(r.eax,0),r.edx); } /* 40dacd */
    /* 40dacf: the part breaks, then every part the breaks push. */
    W16(VA(g,0x4a982cu),0);
    a[0]=D;a[1]=part;rd_call(g,0x40d434u,a,2,&r);
    for(;;) {
        const uint32_t q=R32(VA(g,0x498864u));
        r.eax=q;
        if(!R8(q+5u)) { put_ecx(ecx,r.ecx);return pair(lo(q,1u),1u); } /* 40db15 */
        W8(q+5u,R8(q+5u)-1u);
        const uint32_t c=R8(q+5u);
        r.eax=R32(q);r.ecx=sx8(c);r.edx=R16(r.eax+2u*r.ecx);
        a[0]=D;a[1]=r.edx;rd_call(g,0x40d434u,a,2,&r);
    }
}
