#include "es2/core/panel_weapons.h"
#include "fixed.h"

/* The panel weapon selection 432a50 -> 4106ac -> 410708
 * (docs/reverse-engineering/hud.md, "panel weapon selection as own code").
 * Memory is read again wherever the original reads it, because the game calls
 * in between (the weapon virtual +58h, 410708) may change it. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
static uint64_t pw_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t pw_sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
static uint32_t pw_sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
static void pw_call(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
                    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* Original: DBSIM.EXE 0x410708 */
uint64_t es2_weapon_select_registers(const es2_guest_memory *m,uint32_t image,uint32_t W,uint32_t slot,uint32_t *ecx) {
    const uint32_t index=pw_sx16(slot);
    /* 41071a: already selected (s16 slot against s8 W+1Dh). */
    if(index==pw_sx8(R8(W+0x1du))) { *ecx=index;return pw_pair(W,slot); }
    uint32_t edx=slot;
    const uint32_t e=R32(R32(W+4u)+4u*index);
    uint32_t c=e;
    if(R8(e+0x4bu)) {
        /* 41072d: the definition byte; a negative one moves DX. */
        c=pw_sx8(R8(R32(e+0x18u)+0x16u));
        if(fx_s16(c)<0)edx=(edx&0xffff0000u)|(uint16_t)(edx+c);
    }
    W8(W+0x1du,edx);
    m->write16(m->context,image+0xd2528u,0);
    *ecx=c;
    return pw_pair(W,edx);
}

/* Original: DBSIM.EXE 0x4106ac */
uint64_t es2_weapon_find_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,uint32_t elem,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    /* EBX = i, compared as a word against the count (reread each round). */
    for(uint32_t i=0;fx_s16(i)<fx_s16(R16(W+8u));++i) {
        r.eax=pw_sx16(i);
        r.edx=R32(W+4u);
        const uint32_t e=R32(r.edx+4u*r.eax);
        if(!e)continue;
        /* 4106c6: E.V58(E, elem), EAX = the vtable, ECX = elem. */
        const uint32_t a[2]={e,elem};
        r.ecx=elem;r.eax=R32(e);
        pw_call(g,R32(r.eax+0x58u),0x58u,a,2,&r);
        if(!(uint16_t)r.eax)continue;
        r.eax=e+0x43u;
        if(!R8(e+0x4cu))break;                 /* 4106df: match, not selectable */
        /* 4106e1: 410708(W, i) with the whole EBX. */
        const uint32_t s[2]={W,i};
        pw_call(g,g->image+0x10708u,0,s,2,&r);
        W8(W+0x18u,1u);
        *ecx=r.ecx;
        return pw_pair(((W+0x14u)&0xffff0000u)|1u,r.edx);
    }
    *ecx=r.ecx;
    return pw_pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x432a50 */
uint64_t es2_panel_weapon_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t H,uint32_t elem,
    const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t P=R32(H+0x203u),W=R32(P+0x202u);
    es2_weapon_registers r={P,W,entry[2]};
    const uint32_t a[2]={W,elem};
    pw_call(g,g->image+0x106acu,0,a,2,&r);
    *ecx=r.ecx;
    return pw_pair((uint16_t)r.eax?(r.eax&0xffffff00u)|1u:0u,r.edx);
}
