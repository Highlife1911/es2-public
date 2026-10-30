#include "es2/core/weapon_system.h"
#include "fixed.h"

/* Weapon-system routines 41074c, 410ae4, 410d04 and 4110ac. Contract:
 * weapons.md. The register struct r follows EAX/EDX/ECX so that every game call
 * sees the original's entry registers; lo() is a 16-bit load or operation that
 * keeps the register's high word. */
static uint64_t pair(uint32_t a,uint32_t d) { return (uint64_t)d<<32|a; }
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
static uint32_t sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static void ws_call(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
                    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
#define CALL(va,n) ws_call(g,g->image+(va)-0x400000u,0,a,n,&r)
#define VCALL(vtable,slot,n) ws_call(g,R32((vtable)+(slot)),(slot),a,n,&r)
/* The group record of group byte b: its table pointer. */
static uint32_t ws_table(const es2_guest_memory *m,uint32_t W,uint32_t b) { return R32(W+0x1fu+6u*sx8(b)); }

/* Original: DBSIM.EXE 0x41074c. */
uint64_t es2_select_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,uint32_t step,
    uint32_t esi,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],W,entry[2]};uint32_t a[2];
    const uint32_t sel=R8(W+0x1du);
    r.eax=(entry[0]&0xffffff00u)|sel;
    if((int8_t)sel<0)return pair(r.eax,W);   /* nothing selected */
    const uint32_t si=lo(esi,R16(W+8u));
    uint16_t found=0;
    r.eax=sx8(sel);
    for(;;) {
        r.eax=lo(r.eax,r.eax+step);
        if(fx_s16(r.eax)<0)r.eax=si-1u;        /* below 0: the last slot */
        else if(fx_s16(si)<=fx_s16(r.eax))r.eax=0; /* past the end: slot 0 */
        const uint32_t index=sx16(r.eax);
        const uint32_t E=R32(R32(W+4u)+4u*index);
        if(E&&R8(E+0x4cu)&&(!R8(E+0x4bu)||(int8_t)R8(R32(E+0x18u)+0x16u)>0)) {
            r.ecx=lo(index,R16(ws_table(m,W,R8(W+0x1cu))+2u*index));
            found=(uint16_t)r.ecx;
        }
        if(found)break;
        r.ecx=index;
        if(index==sx8(R8(W+0x1du)))break;
    }
    a[0]=W;a[1]=r.eax;r.edx=W;CALL(0x410708,2);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x410ae4. */
uint64_t es2_fire_group_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,uint32_t group,
    const uint32_t entry[3],uint32_t *esi) {
    (void)entry;
    es2_weapon_registers r={W,sx16(group),sx8(R8(W+0x1cu))};uint32_t a[2];
    if(r.edx==r.ecx)return pair(W,r.edx);   /* already the current group */
    r.edx=group;W8(W+0x1cu,group);
    const uint32_t sel=R8(W+0x1du);
    r.ecx=(r.ecx&0xffffff00u)|sel;*esi=sx8(sel)+1u;
    if((int8_t)sel>=0) {
        r.ecx=sx8(sel);r.edx=ws_table(m,W,group);
        if(!R16(r.edx+2u*r.ecx)) { a[0]=W;a[1]=1;CALL(0x41074c,2); } /* not a member: step on */
    }
    a[0]=group;CALL(0x4610ad,1);
    a[0]=r.eax;a[1]=group;CALL(0x441f90,2);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x410d04. The count word is unsigned (16-bit
 * decrement until it was zero); the list pointer and count are cached. */
uint64_t es2_weapons_v3c_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,
    const uint32_t entry[3]) {
    es2_weapon_registers r={W+4u,R32(W+4u),entry[2]};uint32_t a[1];
    uint32_t p=r.edx;uint16_t c=R16(W+8u);
    for(;;) {
        const uint16_t before=c;r.ecx=lo(r.ecx,before);--c;
        if(!before) { r.eax=0;break; }
        r.eax=p;p+=4u;
        if(!r.eax)break;
        r.edx=R32(r.eax);
        if(r.edx) {
            const uint32_t E=r.edx;
            r.eax=R32(E);a[0]=E;VCALL(r.eax,0x3cu,1);r.ecx=E;
        }
    }
    return pair(r.eax,r.edx);
}

/* The first weapon numbered n with a live carrier (the search of 4110ac),
 * or 0. */
static uint32_t ws_find(const es2_guest_memory *m,uint32_t W,uint32_t n,uint32_t *slot) {
    for(uint32_t i=0;fx_s16(i)<fx_s16(R16(W+8u));++i) {
        const uint32_t E=R32(R32(W+4u)+4u*sx16(i));
        if(!E||(uint16_t)n!=(uint16_t)sx8(R8(R32(E+0x18u)+7u)))continue;
        if(fx_s32(R32(R32(E+0x1cu)+0x30u))<=0)continue;
        *slot=i;return E;
    }
    return 0;
}

/* Original: DBSIM.EXE 0x4110ac. */
uint64_t es2_group_toggle_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t W,uint32_t n,
    const uint32_t entry[3]) {
    (void)entry;uint32_t i=0,a[2];
    const uint32_t E=ws_find(m,W,n,&i);
    if(!E)return pair(0,W);
    es2_weapon_registers r={i,W,n};
    a[0]=W;a[1]=i;CALL(0x410cd0,2);
    r.eax=R32(E);a[0]=E;VCALL(r.eax,0x48u,1);r.ecx=E;
    if(!(uint16_t)r.eax) { r.edx=R32(E);a[0]=E;VCALL(r.edx,0x3cu,1);r.ecx=E; }
    return pair(lo(r.eax,1),r.edx);
}

/* Original: DBSIM.EXE 0x4110ac, its ECX at return: POP ECX = the found
 * weapon, or the n slot when the search fails. */
uint32_t es2_group_toggle_ecx(const es2_guest_memory *m,uint32_t W,uint32_t n) {
    uint32_t i=0;const uint32_t E=ws_find(m,W,n,&i);
    return E?E:n;
}
