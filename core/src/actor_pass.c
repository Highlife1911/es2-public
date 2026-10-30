#include "es2/core/actor_pass.h"
#include "fixed.h"
/* The actor-list pass 4123ac and its helpers 41251c, 4128f8 and 412704
 * (docs/reverse-engineering/vehicle.md, "Actor-list pass"). EAX, EDX and ECX
 * are tracked as the original leaves them, because they become the entry
 * registers of the next game call and the returned EDX:EAX. Memory is read
 * again at every place the original reads it (callees may change the actors,
 * the list and its count). */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* A DBSIM VA (globals, direct callees) inside the image at g->image. */
#define AP_VA(va) (g->image+(uint32_t)(va)-0x400000u)
#define AP_LIST 0x4a9af8u
#define AP_COUNT 0x4a9afeu
static uint64_t ap_pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* A 16-bit register write: the high word stays. */
static uint32_t ap_lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t ap_sx8(uint32_t b) { return (uint32_t)(int32_t)(int8_t)(uint8_t)b; }
static uint32_t ap_sx16(uint32_t w) { return (uint32_t)fx_s16(w); }
static int ap_ge(uint32_t a,int32_t b) { return fx_s32(a)>=b; }
/* "setne r8; and r32,1": 1 while the group's activation is pending. */
static uint32_t ap_pending(const es2_guest_memory *m,uint32_t group) { return R32(group+0x14u)!=0u; }
/* A direct call names the original VA; a virtual call is observed by slot. */
static void ap_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,AP_VA(va),a,n,r);
}
static void ap_virtual(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t table,unsigned slot,
    const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const uint32_t target=R32(table+slot);
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* The list walk of all four functions: the word index and the dword
 * cursor of the frame. */
typedef struct ap_walk { uint32_t index,cursor; } ap_walk;
/* One step: the old index goes into the low word of *word, the old cursor
 * into *cursor; 0 at the end of the count or at a null entry. */
static uint32_t ap_step(const es2_guest_memory *m,const es2_weapon_graph *g,ap_walk *w,uint32_t *word,uint32_t *cursor) {
    const uint32_t i=w->index;
    *word=ap_lo(*word,i);w->index=(uint16_t)(i+1u);
    if(fx_s16(i)>=fx_s16(R16(AP_VA(AP_COUNT))))return 0;
    *cursor=w->cursor;w->cursor+=4u;
    return R32(*cursor);
}

/* Original: DBSIM.EXE 0x41251c */
uint64_t es2_contact_expire_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,
    const uint32_t entry[3]) {
    const uint32_t A=actor;es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2],B;ap_walk w;
    r.eax=A+0x92u;W8(A+0x95u,0);
    w.index=0;r.edx=R32(AP_VA(AP_LIST));w.cursor=r.edx;
    r.ecx=R32(A+0x45u);const uint32_t side=ap_sx8(R8(r.ecx+0x12u));
    while((B=ap_step(m,g,&w,&r.eax,&r.edx))!=0) {
        r.eax=R32(B+0x45u);r.edx=ap_pending(m,r.eax);
        if(r.edx)continue;
        r.ecx=ap_sx8(R8(r.eax+0x12u));
        if((uint16_t)side==(uint16_t)r.ecx)continue;
        r.eax=ap_lo(B,R16(B+0x4bu));r.edx=A+0xc2u;r.ecx=ap_sx16(r.eax);r.eax=A+0xc2u;
        if(!R8(r.eax+r.ecx))continue;
        r.edx=B+0x26u;r.ecx=A+0x26u;a[0]=A+0x26u;a[1]=B+0x26u;ap_call(g,0x4928d0u,a,2,&r);
        if(!(fx_s32(r.eax)>100000)) {
            a[0]=A;a[1]=B;ap_call(g,0x412608u,a,2,&r);
            if((uint16_t)r.eax)continue;
        }
        r.eax=ap_lo(B,R16(B+0x4bu));r.edx=A+0xc2u;r.ecx=ap_sx16(r.eax);r.eax=A+0xc2u;W8(r.eax+r.ecx,0);
        r.eax=ap_lo(A,R16(A+0x4bu));r.edx=B+0xc2u;r.ecx=ap_sx16(r.eax);W8(r.edx+r.ecx,0);
    }
    return ap_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x412704 */
uint64_t es2_contact_share_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,
    uint32_t other,const uint32_t entry[3]) {
    const uint32_t P=actor,Q=other;es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2],X;ap_walk w;
    r.eax=R32(P+0x45u);const uint32_t side=ap_sx8(R8(r.eax+0x12u));
    r.eax=R32(Q+0x45u);r.edx=ap_sx8(R8(r.eax+0x12u));
    if((uint16_t)side==(uint16_t)r.edx)return ap_pair(r.eax,r.edx);
    r.ecx=ap_lo(r.ecx,R16(Q+0x4bu));const uint32_t slot=(uint16_t)r.ecx;
    r.eax=R32(AP_VA(AP_LIST));w.index=0;w.cursor=r.eax;
    while((X=ap_step(m,g,&w,&r.edx,&r.ecx))!=0) {
        r.eax=R32(X+0x45u);r.edx=ap_pending(m,r.eax);
        if(r.edx)continue;
        r.ecx=ap_sx8(R8(r.eax+0x12u));
        if((uint16_t)side!=(uint16_t)r.ecx)continue;
        r.eax=Q+0x26u;r.edx=X+0x26u;a[0]=X+0x26u;a[1]=Q+0x26u;ap_call(g,0x49288cu,a,2,&r);
        if(ap_ge(r.eax,100000))continue;
        r.edx=X+0xc2u;r.ecx=ap_sx16(slot);W8(r.edx+r.ecx,1);
        if(ap_ge(r.eax,50000))continue;
        r.eax=Q+0x92u;W8(Q+0x9eu,1);
        r.edx=R32(X+0x1b2u);
        if(!r.edx)continue;
        r.eax=X+0x92u;
        if(R8(X+0xa2u))continue;
        a[0]=r.edx;ap_call(g,0x423430u,a,1,&r);r.ecx=a[0];
    }
    return ap_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4128f8 */
uint64_t es2_contact_scan_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t actor,
    const uint32_t entry[3]) {
    const uint32_t A=actor;es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2],B;ap_walk w;
    r.eax=R32(AP_VA(AP_LIST));w.index=0;w.cursor=r.eax;
    r.edx=R32(A+0x45u);r.ecx=R32(r.edx+0xcu);r.eax=R32(r.ecx)+0x92u;
    while((B=ap_step(m,g,&w,&r.eax,&r.edx))!=0) {
        r.eax=B+0x92u;
        if(R8(B+0xa4u))r.eax=1;
        else { r.edx=B+0x92u;r.eax=R8(B+0x99u)?1u:0u; }
        if((uint16_t)r.eax)continue;
        r.eax=R32(B+0x45u);r.edx=ap_pending(m,r.eax);
        if(r.edx)continue;
        r.ecx=ap_sx8(R8(r.eax+0x12u));
        if((uint16_t)r.ecx!=1u)continue;
        r.eax=B+0x26u;r.edx=A+0x26u;a[0]=A+0x26u;a[1]=B+0x26u;ap_call(g,0x49288cu,a,2,&r);
        const uint32_t d=r.eax;
        /* Alert bytes +95h unless both are already set. */
        r.eax=A+0x92u;
        int alert=1;
        if(R8(A+0x95u)) { r.eax=B+0x92u;if(R8(B+0x95u))alert=0; }
        if(alert) {
            r.eax=A+0x92u;
            if(!R8(A+0x96u)) { r.eax=B+0x92u;if(!R8(B+0x96u))alert=0; }
        }
        if(alert&&!ap_ge(d,200000)) {
            a[0]=A;a[1]=B;ap_call(g,0x412608u,a,2,&r);
            if((uint16_t)r.eax) {
                r.eax=B+0x92u;
                if(R8(B+0x96u)||!ap_ge(d,140000)) {
                    a[0]=A;a[1]=B;r.edx=R32(A);ap_virtual(m,g,r.edx,0x48u,a,2,&r);
                    r.ecx=B+0x92u;r.eax=r.ecx;W8(B+0x95u,1);
                }
                r.eax=A+0x92u;
                if(R8(A+0x96u)||!ap_ge(d,140000)) { r.edx=A+0x92u;r.ecx=r.edx;W8(A+0x95u,1); }
            }
        }
        if(ap_ge(d,80000))continue;
        /* Mutual contact: marks and warning triggers. */
        r.eax=ap_lo(B,R16(B+0x4bu));r.edx=A+0xc2u;r.ecx=ap_sx16(r.eax);r.eax=A+0xc2u;
        int mutual=0;
        if(R8(r.eax+r.ecx)) {
            r.eax=A;r.ecx=B+0xc2u;r.edx=ap_lo(r.edx,R16(A+0x4bu));r.eax=ap_sx16(r.edx);r.edx=B+0xc2u;
            mutual=R8(r.edx+r.eax)!=0;
        }
        if(mutual) {
            if(ap_ge(d,50000))continue;
            r.eax=B+0x92u;W8(B+0x9eu,1);
            if(R32(A+0x1b2u)) {
                r.eax=A+0x92u;
                if(!R8(A+0xa2u)) { r.edx=R32(A+0x1b2u);a[0]=r.edx;ap_call(g,0x423430u,a,1,&r);r.ecx=a[0]; }
            }
            r.eax=A+0x92u;W8(A+0x9eu,1);
            if(!R32(B+0x1b2u))continue;
            r.eax=B+0x92u;
            if(R8(B+0xa2u))continue;
            r.edx=R32(B+0x1b2u);a[0]=r.edx;ap_call(g,0x423430u,a,1,&r);r.ecx=a[0];
            continue;
        }
        /* One side does not know the other: sight inside the facing arcs. */
        r.ecx=A+0x26u;r.eax=B+0x26u;a[0]=B+0x26u;a[1]=A+0x26u;ap_call(g,0x492934u,a,2,&r);
        uint32_t h=r.eax;
        a[0]=A;r.eax=R32(A);ap_virtual(m,g,r.eax,0x3cu,a,1,&r);
        r.eax=ap_lo(r.eax,r.eax+h);r.ecx=A;
        a[0]=A;a[1]=r.eax;r.edx=R32(A);ap_virtual(m,g,r.edx,0x44u,a,2,&r);
        if((uint16_t)r.eax) {
            a[0]=A;a[1]=B;ap_call(g,0x412608u,a,2,&r);
            if((uint16_t)r.eax) {
                a[0]=A;a[1]=B;r.ecx=R32(A);ap_virtual(m,g,r.ecx,0x48u,a,2,&r);
                r.eax=A+0x92u;
                if(R8(A+0xa3u)) {
                    r.eax=ap_lo(r.eax,R16(B+0x4bu));r.edx=A+0xc2u;r.ecx=ap_sx16(r.eax);r.eax=A+0xc2u;W8(r.eax+r.ecx,1);
                } else { a[0]=A;a[1]=B;ap_call(g,0x412704u,a,2,&r); }
            }
        }
        a[0]=B;r.edx=R32(B);ap_virtual(m,g,r.edx,0x3cu,a,1,&r);
        h=ap_lo(h,h+r.eax);r.ecx=B;h=ap_lo(h,h+0x8000u);
        a[0]=B;a[1]=h;r.ecx=R32(B);ap_virtual(m,g,r.ecx,0x44u,a,2,&r);
        if(!(uint16_t)r.eax)continue;
        a[0]=A;a[1]=B;ap_call(g,0x412608u,a,2,&r);
        if(!(uint16_t)r.eax)continue;
        a[0]=B;a[1]=A;ap_call(g,0x412704u,a,2,&r);
    }
    return ap_pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4123ac */
uint64_t es2_actor_pass_registers(const es2_guest_memory *m,const es2_weapon_graph *g,const uint32_t entry[3]) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};uint32_t a[2],B,held;ap_walk w;
    /* Loop 1: the two timers; on expiry re-arm +1E6h and expire contacts. */
    w.index=0;r.eax=R32(AP_VA(AP_LIST));w.cursor=r.eax;
    while((B=ap_step(m,g,&w,&r.eax,&r.edx))!=0) {
        r.edx=R32(B+0x45u);r.ecx=ap_pending(m,r.edx);
        if(r.ecx)continue;
        r.eax=B+0x1e2u;a[0]=r.eax;ap_call(g,0x467a48u,a,1,&r);r.ecx=a[0];
        r.edx=B+0x1e5u;a[0]=r.edx;ap_call(g,0x467a48u,a,1,&r);
        r.ecx=(uint16_t)r.eax==0u;
        if(!r.ecx)continue;
        a[0]=AP_VA(0x4d2599u);a[1]=1000;ap_call(g,0x492f24u,a,2,&r);
        r.eax=ap_lo(r.eax,r.eax+10000u);W16(B+0x1e6u,r.eax);
        a[0]=B;ap_call(g,0x41251cu,a,1,&r);r.ecx=B;
    }
    /* Loop 2: contact scans of the side-0 actors; the last player is held. */
    w.index=0;r.eax=R32(AP_VA(AP_LIST));held=0;w.cursor=r.eax;
    while((B=ap_step(m,g,&w,&r.eax,&r.edx))!=0) {
        r.eax=R32(B+0x45u);r.edx=ap_pending(m,r.eax);
        if(r.edx)continue;
        r.eax=B+0x92u;r.edx=r.eax;
        if(R8(B+0xa4u))r.edx=1;
        else r.edx=R8(B+0x99u)?1u:0u;
        if((uint16_t)r.edx)continue;
        r.ecx=R32(B+0x45u);
        if(R8(r.ecx+0x12u))continue;
        r.edx=r.eax;
        if(R8(B+0xa3u)) { held=B;continue; }
        a[0]=B;ap_call(g,0x4128f8u,a,1,&r);r.ecx=B;
    }
    if(held) { a[0]=held;ap_call(g,0x4128f8u,a,1,&r);r.ecx=held; }
    /* Loop 3: clear the per-frame byte +A2h. */
    w.index=0;r.eax=R32(AP_VA(AP_LIST));w.cursor=r.eax;
    while((B=ap_step(m,g,&w,&r.edx,&r.ecx))!=0) { r.eax=B+0x92u;W8(B+0xa2u,0); }
    /* EDX = the index dword of the frame; its high word is uninitialized
     * stack in the original (dead at the caller), returned as 0. */
    return ap_pair(r.eax,(uint16_t)w.index);
}
