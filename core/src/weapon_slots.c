#include "es2/core/weapon_slots.h"
#include "fixed.h"

/* The player weapon system 410b40 with 410a3c, 410a04, 410970, 410f04, 410f14
 * and 410cd0, and the range gauge 4420c8, 442120, 442054, 442154 and 441f90
 * (weapons.md, "player weapon system"). Memory is read again wherever the
 * original reads it, because the game calls in between may change it. r follows
 * EAX/EDX/ECX and *saved EBX/ESI/EDI at every game call. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
static uint64_t sl_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t sl_sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
static uint32_t sl_sx16(uint32_t x) { return (uint32_t)fx_s16(x); }
/* A 16-bit (lo) or 8-bit (lo8) load into a register that keeps the rest. */
static uint32_t sl_lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sl_lo8(uint32_t reg,uint32_t byte) { return (reg&0xffffff00u)|(uint8_t)byte; }
static void sl_call(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
                    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
static void sl_keep(es2_weapon_slots_saved *s,uint32_t ebx,uint32_t esi,uint32_t edi) { s->ebx=ebx;s->esi=esi;s->edi=edi; }
#define CALL(va,n) sl_call(g,g->image+(va)-0x400000u,0,a,n,&r)
#define VCALL(vtable,slot,n) sl_call(g,R32((vtable)+(slot)),(slot),a,n,&r)

/* Original: DBSIM.EXE 0x441f90 */
uint64_t es2_gauge_mode_registers(const es2_guest_memory *m,uint32_t H,uint32_t mode,const uint32_t entry[3]) {
    const uint32_t edx=sl_lo(entry[1],mode);
    W16(H+0xb1u,edx);
    return sl_pair(H,edx);
}

/* Original: DBSIM.EXE 0x410f04 */
uint64_t es2_latch_set_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3]) {
    W8(W+0x14u,1u);
    return sl_pair(W+0x14u,entry[1]);
}

/* Original: DBSIM.EXE 0x442120 */
uint64_t es2_gauge_state_registers(const es2_guest_memory *m,uint32_t image,uint32_t H,const uint32_t entry[3],
    uint32_t *ecx) {
    const uint32_t copy=image+0xd1a5cu;
    const uint32_t edx=sl_lo(entry[1],R16(H+0xb1u));
    W16(copy,edx);
    const uint32_t c=sl_lo8(entry[2],R8(H+0xb3u));
    W8(copy+2u,c);
    W8(copy+3u,R8(H+0xb4u));
    *ecx=c;
    return sl_pair(copy,edx);
}

/* Original: DBSIM.EXE 0x410cd0 */
uint64_t es2_member_toggle_registers(const es2_guest_memory *m,uint32_t W,uint32_t i,uint32_t *ecx) {
    const uint32_t index=sl_sx16(i);
    const uint32_t T=R32(W+0x1fu+6u*sl_sx8(R8(W+0x1cu)));
    const uint32_t c=sl_lo(index,R16(T+2u*index)^1u);
    W16(T+2u*index,c);
    *ecx=c;
    return sl_pair(W,index);
}

/* Original: DBSIM.EXE 0x410f14 */
uint64_t es2_link_toggle_registers(const es2_guest_memory *m,uint32_t W,const uint32_t entry[3],uint32_t *ecx) {
    const uint32_t sel=R8(W+0x1du);
    if((int8_t)sel<0) { *ecx=sl_sx8(sel)+1u;return sl_pair(W,sl_lo8(entry[1],sel)); }
    const uint32_t b=sl_sx8(sel);
    const uint32_t E=R32(R32(W+4u)+4u*b);
    uint32_t c=sl_sx8(R8(R32(E+0x18u)+0x16u));
    if(!c) { *ecx=0;return sl_pair(W,E); }        /* no partner offset */
    c=sl_sx16(c+b);
    const uint32_t L=R32(R32(W+4u)+4u*c);
    if(!L) { *ecx=c;return sl_pair(0,E); }        /* empty partner slot */
    const uint32_t cE=R32(E+0x1cu),cL=R32(L+0x1cu);
    if(R16(cE+0x56u)!=R16(cL+0x56u)) { *ecx=0;return sl_pair(L,E); }
    W8(E+0x4bu,R8(E+0x4bu)^1u);
    W8(L+0x4bu,R8(L+0x4bu)^1u);
    *ecx=1;
    return sl_pair(L+0x43u,E+0x43u);
}

/* Original: DBSIM.EXE 0x442054 */
uint64_t es2_gauge_link_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_slots_saved *saved,
    uint32_t H,uint32_t value,const uint32_t entry[3],uint32_t *ecx) {
    (void)saved;
    es2_weapon_registers r={0,sl_lo8(entry[1],value),0};uint32_t a[1];
    W8(H+0xb4u,value);
    r.eax=R32(g->image+0x9b088u);r.ecx=R32(r.eax+0x203u);r.eax=R32(r.ecx+0x202u);
    a[0]=r.eax;CALL(0x410f14,1);
    *ecx=a[0];                                     /* POP ECX: the pushed W */
    return sl_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4420c8 */
uint64_t es2_gauge_sync_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_slots_saved *saved,
    uint32_t H,uint32_t R,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2];
    sl_keep(saved,H,R,saved->edi);
    r.eax=sl_lo(r.eax,R16(R));
    if((uint16_t)r.eax!=R16(H+0xb1u)) {
        W16(H+0xb1u,r.eax);
        a[0]=H;a[1]=r.eax;CALL(0x441f90,2);
    }
    r.edx=sl_lo8(r.edx,R8(H+0xb3u));
    if((uint8_t)r.edx!=R8(R+2u)) { a[0]=H;CALL(0x441fa4,1);r.ecx=H; }
    r.eax=sl_lo8(r.eax,R8(R+3u));
    if((uint8_t)r.eax!=R8(H+0xb4u)) {
        r.edx=(uint8_t)r.eax;
        a[0]=H;a[1]=r.edx;CALL(0x442054,2);
    }
    *ecx=r.ecx;
    return sl_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x442154 */
uint64_t es2_gauge_button_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_slots_saved *saved,
    uint32_t H,uint32_t x,uint32_t *ecx) {
    es2_weapon_registers r;uint32_t a[2],k=0,slot=H+0x18u;
    for(;fx_s32(k)<fx_s32(R32(H+0x68u));++k,slot+=4u) {
        if(x!=R32(slot))continue;
        sl_keep(saved,x,saved->esi,saved->edi);
        switch(k) {
        case 0:
            W16(H+0xb1u,R16(H+0xb1u)+1u);
            if(R16(H+0xb1u)==3u)W16(H+0xb1u,0);
            r.eax=sl_lo(0xffffffffu,R16(H+0xb1u));r.edx=slot;r.ecx=H;
            a[0]=H;a[1]=r.eax;CALL(0x441f90,2);
            *ecx=r.ecx;return sl_pair(r.eax,r.edx);
        case 1:
            r.eax=0;r.edx=slot;r.ecx=H;
            a[0]=H;a[1]=1;CALL(0x442054,2);
            *ecx=r.ecx;return sl_pair(r.eax,r.edx);
        case 2:
            r.eax=0;r.edx=slot;r.ecx=H;
            a[0]=H;CALL(0x441fa4,1);
            *ecx=H;return sl_pair(r.eax,r.edx);
        default:
            *ecx=H;return sl_pair(k-2u,slot);     /* buttons 3..: nothing */
        }
    }
    *ecx=H;
    return sl_pair(k,slot);
}

/* Original: DBSIM.EXE 0x410970 */
uint64_t es2_chain_ready_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_slots_saved *saved,
    uint32_t W,uint32_t E,uint32_t i,uint32_t flag,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={R32(E),entry[1],entry[2]};uint32_t a[4];
    sl_keep(saved,W,E,i);
    a[0]=E;VCALL(r.eax,0x2cu,1);r.ecx=E;
    if(!(uint16_t)r.eax) { *ecx=r.ecx;return sl_pair(0,r.edx); }
    if(R32(W+0x31u)) {
        r.edx=R32(W+0x31u);
        a[0]=E;a[1]=r.edx;CALL(0x40e5f8,2);
        if(!(uint16_t)r.eax) { *ecx=r.ecx;return sl_pair(0,r.edx); }
    }
    r.ecx=R32(E);
    a[0]=E;a[1]=0;VCALL(r.ecx,0x60u,2);
    const uint16_t ax=(uint16_t)r.eax;
    if(ax!=5u&&ax!=3u) {
        r.eax=sl_sx16(ax);
        if(!R16(W+2u*r.eax+0xau)) { *ecx=r.ecx;return sl_pair(0,r.edx); }
    }
    if(!(uint16_t)flag) {
        r.eax=E+0x43u;
        if(R8(E+0x4bu)) {
            /* The partner: slot i + definition byte, checked with f = 1. */
            const uint32_t D=R32(E+0x18u),list=R32(W+4u);
            r.eax=sl_sx8(R8(D+0x16u));
            r.edx=r.eax+i;r.ecx=sl_sx16(r.edx);
            const uint32_t L=R32(list+4u*r.ecx);
            sl_keep(saved,W,L,i);
            a[0]=W;a[1]=L;a[2]=r.edx;a[3]=1;CALL(0x410970,4);
            *ecx=r.ecx;return sl_pair(r.eax,r.edx);
        }
    }
    *ecx=r.ecx;
    return sl_pair(sl_lo(r.eax,1),r.edx);
}

/* Original: DBSIM.EXE 0x410a04 */
uint64_t es2_selected_ready_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_slots_saved *saved,
    uint32_t W,uint32_t E,uint32_t i,const uint32_t entry[3],uint32_t *ecx) {
    (void)saved;
    if(R8(W+0x18u)||R16(g->image+0xd2526u)) { *ecx=entry[2];return sl_pair(sl_lo(W,1),W+0x14u); }
    es2_weapon_registers r={W,E,sl_lo(entry[2],i)};uint32_t a[4]={W,E,r.ecx,0};
    CALL(0x410970,4);
    *ecx=r.ecx;
    return sl_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x410a3c */
uint64_t es2_ready_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_slots_saved *saved,
    uint32_t W,const uint32_t entry[3],uint32_t *ecx) {
    /* The dword at EBP-4: the pushed entry ECX, then count : k. */
    uint32_t slot=entry[2];
    const uint32_t sel=R8(W+0x1du);
    if((int8_t)sel<0) { *ecx=slot;return sl_pair(sl_lo8(entry[0],sel),sl_sx8(sel)+1u); }
    const uint32_t edi=sl_sx8(sel);
    uint32_t E=R32(R32(W+4u)+4u*edi);
    const uint16_t count=R16(W+8u);
    slot=(uint32_t)count<<16;
    es2_weapon_registers r={sl_lo(edi,count),sl_sx8(R8(W+0x1du)),entry[2]};uint32_t a[3];
    sl_keep(saved,W,E,edi);
    a[0]=W;a[1]=E;a[2]=r.edx;CALL(0x410a04,3);
    if((uint16_t)r.eax) { *ecx=slot;return sl_pair(r.eax,r.edx); }
    for(;;) {
        a[0]=W;a[1]=1;CALL(0x41074c,2);
        r.eax=sl_sx8(R8(W+0x1du));r.edx=r.eax;r.ecx=R32(W+4u);
        E=R32(r.ecx+4u*r.edx);
        sl_keep(saved,W,E,edi);
        a[0]=W;a[1]=E;a[2]=r.eax;CALL(0x410a04,3);
        if((uint16_t)r.eax) {
            /* A usable weapon is selected. */
            r.eax=W+0x14u;W8(W+0x18u,0);
            *ecx=slot;return sl_pair(r.eax,r.edx);
        }
        r.edx=sl_sx8(R8(W+0x1du));r.ecx=edi;
        if(r.edx==r.ecx)break;                       /* back at the start */
        const uint16_t k=(uint16_t)slot;
        slot=(slot&0xffff0000u)|(uint16_t)(k+1u);
        r.eax=sl_lo(r.eax,k);
        if(fx_s16(k)>fx_s16(slot>>16))break;          /* count + 1 steps */
    }
    r.edx=edi;r.eax=edi;W8(W+0x1du,edi);
    a[0]=W;a[1]=r.eax;CALL(0x410708,2);
    *ecx=slot;
    return sl_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x410b40 */
uint64_t es2_weapon_frame_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_weapon_slots_saved *saved,
    uint32_t W,uint32_t x,uint32_t rec,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[4];
    const uint32_t caller_esi=saved->esi,caller_edi=saved->edi;
    sl_keep(saved,W,caller_esi,caller_edi);
    if(R16(g->image+0xd2528u)&&!R16(g->image+0xd2526u)) {
        a[0]=W;a[1]=1;CALL(0x41074c,2);
        r.eax=W+0x14u;W8(W+0x18u,0);
    }
    r.edx=x;W32(W+0x31u,x);
    a[0]=W;CALL(0x410a3c,1);
    r.ecx=R32(g->image+0x9b088u);
    const uint32_t H=R32(r.ecx+0x1e1u);
    sl_keep(saved,W,H,caller_edi);
    a[0]=H;CALL(0x442120,1);r.ecx=H;
    /* The record: the copy's dword and the byte after it. */
    const uint32_t copy=r.eax;
    r.edx=R32(copy);W32(rec,r.edx);
    r.edx=sl_lo8(r.edx,R8(copy+4u));W8(rec+4u,r.edx);
    r.edx=W+0x14u;r.eax=r.edx;
    if(R8(W+0x16u)) {
        W8(W+0x16u,0);
        r.ecx=sl_sx8(R8(W+0x1cu));W16(rec,r.ecx);
    } else {
        r.eax=sl_lo(r.eax,R16(rec));
        a[0]=W;a[1]=r.eax;CALL(0x410ae4,2);
    }
    if(R8(rec+2u)) { a[0]=W;CALL(0x410f04,1);r.ecx=W; }
    else { r.eax=W+0x14u;W8(W+0x14u,0); }
    if(R8(rec+3u)) { a[0]=W;CALL(0x410f14,1);r.ecx=W; }
    W8(rec+3u,0);
    r.edx=rec;a[0]=H;a[1]=rec;CALL(0x4420c8,2);
    for(uint32_t i=0;fx_s16(i)<fx_s16(R16(W+8u));++i) {
        r.eax=i;r.edx=R32(W+4u);
        const uint32_t E=R32(r.edx+4u*i);
        if(!E)continue;
        r.ecx=sl_sx8(R8(W+0x1du));
        uint16_t selected=r.eax==r.ecx;
        r.eax=E+0x43u;
        if(R8(E+0x4bu)) {
            /* A linked weapon: its partner L in slot i + definition byte. */
            r.eax=sl_sx8(R8(R32(E+0x18u)+0x16u));
            r.ecx=sl_sx16(r.eax+i);
            const uint32_t L=R32(R32(W+4u)+4u*r.ecx);
            r.edx=E+0x43u;r.ecx=E+0x43u;
            if(R8(E+0x49u)||!R8(E+0x4cu)) {
                /* Exchange: flip both link bytes, select the partner slot. */
                W8(E+0x4bu,R8(E+0x4bu)^1u);
                r.edx=L+0x43u;W8(L+0x4bu,R8(L+0x4bu)^1u);
                r.eax+=i;
                sl_keep(saved,W,i,E);
                a[0]=W;a[1]=r.eax;CALL(0x410708,2);
            } else {
                r.edx=i+r.eax;
                r.ecx=sl_sx8(R8(W+0x1du));
                if(r.edx==r.ecx)selected=1;
            }
        }
        sl_keep(saved,W,i,E);
        a[0]=W;a[1]=E;a[2]=i;a[3]=0;CALL(0x410970,4);
        const uint32_t ready=r.eax;
        r.edx=sl_sx8(R8(W+0x1cu));r.ecx=3u*r.edx;
        r.edx=R32(W+2u*r.ecx+0x1fu);
        r.eax=sl_lo(i,R16(r.edx+2u*i));
        r.ecx=sl_lo(r.ecx,selected);
        a[0]=E;a[1]=r.ecx;a[2]=r.eax;a[3]=ready;
        r.eax=R32(E);VCALL(r.eax,0x50u,4);
    }
    *ecx=r.ecx;
    return sl_pair(r.eax,r.edx);
}
