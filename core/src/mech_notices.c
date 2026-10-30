#include "es2/core/mech_notices.h"
#include "fixed.h"
/* MECH notice virtuals (docs/reverse-engineering/ai.md, section "MECH
 * notices"). r holds what the original's EAX/EDX/ECX hold at the next game call
 * or at RET; lo() is a 16-bit load that keeps the register's high word. Memory
 * is read again wherever the original reads it again. The originals' frame
 * dwords are copies that no callee gets the address of, so they stay in C. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* A DBSIM VA (globals, pushed addresses). */
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)
#define PLAYER 0x4a9b84u   /* the player's actor */
#define SEEN 0x4a9b00u     /* per-index "already reported" bytes */
#define TIMER 0x4a9b64u    /* timer record: byte, word +1 */

static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
static uint32_t sx8(uint32_t x) { return (uint32_t)(int32_t)(int8_t)(uint8_t)x; }
static void mn_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}

/* Original: DBSIM.EXE 0x412800 (A, B). EBX = A, ESI = B. */
uint64_t es2_mech_contact_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,uint32_t B,
    const uint32_t entry[3]) {
    uint32_t a[3],w;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    if(!R8(VA(0x499358u)))W8(VA(0x499358u),R8(VA(0x499358u))+1u);
    r.eax=A+0x92u;
    if(R8(r.eax+0x11u)) { /* 412829: A is held back: only mark B's index. */
        r.edx=sx(R16(B+0x4bu));W8(VA(SEEN)+r.edx,1);
        return pair(r.eax,r.edx);
    }
    a[0]=VA(TIMER);mn_call(g,0x467a48u,a,1,&r);r.ecx=VA(TIMER);
    if(R16(B+0x1a8u)==1u)return pair(r.eax,r.edx);
    r.ecx=R32(VA(PLAYER));r.eax=R32(r.ecx+0x45u);
    if(r.eax!=R32(A+0x45u))return pair(r.eax,r.edx);
    w=R16(B+0x4bu);r.eax=lo(r.eax,w);r.edx=sx(w);
    if(R8(VA(SEEN)+r.edx))return pair(r.eax,r.edx);
    r.edx=B+0x92u;
    if(R8(r.edx+3u))return pair(r.eax,r.edx);
    r.ecx=lo(r.ecx,R16(B+0x4bu));r.edx=A+0xc2u;r.ecx=sx(r.ecx);
    if(R8(r.edx+r.ecx))return pair(r.eax,r.edx);
    r.ecx=lo(r.ecx,R16(B+0x4bu));r.edx=R32(VA(PLAYER))+0xc2u;r.ecx=sx(r.ecx);
    if(R8(r.edx+r.ecx))return pair(r.eax,r.edx);
    r.eax=sx(r.eax);W8(VA(SEEN)+r.eax,1);
    if(R16(VA(TIMER)+1u))return pair(r.eax,r.edx);
    a[0]=A;a[1]=1;a[2]=0;mn_call(g,0x420a98u,a,3,&r);W16(VA(TIMER)+1u,10000u);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x415710 (K, V, w). ESI = K, EBX = V. */
uint64_t es2_mech_kill_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t K,uint32_t V,uint32_t w,
    const uint32_t entry[3]) {
    uint32_t a[3];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    if(!(uint16_t)w) {
        r.edx=R32(VA(PLAYER));r.ecx=R32(r.edx+0x45u);r.eax=R32(K+0x45u);
        if(r.ecx==r.eax) {
            r.ecx=R32(V+0x45u);r.eax=sx8(R8(r.eax+0x12u));r.ecx=sx8(R8(r.ecx+0x12u));
            if((uint16_t)r.ecx!=(uint16_t)r.eax&&r.edx!=K) { a[0]=K;a[1]=2;a[2]=0;mn_call(g,0x420a98u,a,3,&r); }
        }
        /* 415753: credit the kill when the sides differ. */
        r.edx=R32(V+0x45u);r.eax=sx8(R8(r.edx+0x12u));r.edx=R32(K+0x45u);r.ecx=sx8(R8(r.edx+0x12u));
        if((uint16_t)r.eax!=(uint16_t)r.ecx) {
            r.eax=sx(R16(V+0x1a8u));
            const uint32_t count=K+2u*r.eax+0x2a4u;W16(count,R16(count)+1u);
            r.eax=K+0x92u;W8(r.eax+0x14u,1);
        }
    }
    /* 41577f: V in the player's group, not the player: V's radio call. */
    r.eax=R32(VA(PLAYER));r.edx=R32(r.eax+0x45u);
    if(r.edx!=R32(V+0x45u)||r.eax==V)return pair(r.eax,r.edx);
    r.eax=V;r.ecx=lo(r.ecx,0x25u);r.edx=V+0x92u;
    if(!R8(r.edx+7u))r.ecx+=0xffffffdfu;
    r.edx=r.ecx;a[0]=V;a[1]=r.edx;a[2]=1;mn_call(g,0x420a98u,a,3,&r);
    if(K==R32(VA(PLAYER)))W16(VA(0x4a9e84u),R16(VA(0x4a9e84u))+1u);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41f790 (A). */
uint64_t es2_mech_alert_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,const uint32_t entry[3]) {
    uint32_t a[3];
    es2_weapon_registers r={A,entry[1],entry[2]};
    if(!R32(A+0x278u)) { W32(A+0x278u,40000u);a[0]=A;a[1]=8;a[2]=0;mn_call(g,0x420a98u,a,3,&r); }
    return pair(r.eax,r.edx);
}

/* The inlined target switch of 41f953 and 41fa4d: when N is not
 * [A+1A4h], the old target's attacker word +1A2h -1 (its pointer in
 * *dec), [A+1A4h] = N, N's word +1 (read back into *inc), byte A+9Dh = 1;
 * *done = A+92h. Returns 1 when it switched. */
static int mn_retarget(const es2_guest_memory *m,uint32_t A,uint32_t N,uint32_t *dec,uint32_t *inc,uint32_t *done) {
    if(N==R32(A+0x1a4u))return 0;
    if(R32(A+0x1a4u)) { *dec=R32(A+0x1a4u);W16(*dec+0x1a2u,R16(*dec+0x1a2u)-1u); }
    W32(A+0x1a4u,N);
    if(N) { *inc=R32(A+0x1a4u);W16(*inc+0x1a2u,R16(*inc+0x1a2u)+1u); }
    *done=A+0x92u;W8(A+0x9du,1);
    return 1;
}

/* Original: DBSIM.EXE 0x41f7b8 (A, S, w). EBX = A, ESI = S (later the new
 * target), EDI = f (A in the player's group), later the leader L. */
uint64_t es2_mech_attacked_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,uint32_t S,uint32_t w,
    const uint32_t entry[3]) {
    uint32_t a[3],L,old;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.eax=R32(VA(PLAYER));r.edx=R32(r.eax+0x45u);
    r.ecx=r.edx==R32(A+0x45u)?1u:0u;
    const uint32_t f=r.ecx;
    if(S==R32(VA(PLAYER))&&(uint16_t)f) { a[0]=A;mn_call(g,0x41f790u,a,1,&r);r.ecx=A; }
    /* 41f7ee: S out (bytes S+0A5h, S+0A4h, S+99h). */
    r.eax=S+0x92u;
    if(R8(r.eax+0x13u)) { r.ecx=1;return pair(r.eax,r.edx); }
    r.edx=S+0x92u;
    if(R8(r.edx+0x12u)) { r.edx=1;r.ecx=1;return pair(r.eax,r.edx); }
    r.ecx=S+0x92u;r.eax=r.ecx;
    if(R8(r.eax+7u)) { r.edx=1;r.ecx=1;return pair(r.eax,r.edx); }
    r.edx=0;r.ecx=0;
    /* 41f838: only an attacker of the other side. */
    r.eax=R32(S+0x45u);r.ecx=R32(A+0x45u);r.edx=sx8(R8(r.eax+0x12u));r.eax=sx8(R8(r.ecx+0x12u));
    if((uint16_t)r.edx==(uint16_t)r.eax)return pair(r.eax,r.edx);
    a[0]=A;a[1]=S;a[2]=0;mn_call(g,0x411aecu,a,3,&r);
    W32(A+0x27du,30000u);
    if((uint16_t)f) {
        r.edx=sx(w);W32(A+0x281u,R32(A+0x281u)+r.edx);r.ecx=R32(A+0x281u);
        if((int32_t)r.ecx<8000)return pair(r.eax,r.edx);
    }
    r.eax=A+0x92u;
    if(R8(r.eax+0x11u)||R32(A+0x273u))return pair(r.eax,r.edx);
    if(S==R32(A+0x1a4u)) { r.eax=R32(A+0x4du)+8u;if(R8(r.eax+1u))return pair(r.eax,r.edx); }
    /* 41f8bb: react; the leader's notice. */
    r.edx=R32(A+0x45u);r.ecx=R32(r.edx+0xcu);L=R32(r.ecx);
    W32(A+0x273u,10000u);
    r.eax=R32(A+0x4du)+8u;
    if(!R8(r.eax+1u)) {
        r.eax=L+0x92u;
        if(R8(r.eax+0x11u)) {
            if(!R16(S+0x1a8u)) { a[0]=A;a[1]=3;a[2]=0;mn_call(g,0x420a98u,a,3,&r); }
            if(S==R32(L+0x1a4u)) { r.eax=A+0x92u;W8(r.eax+8u,0); }
        }
    }
    r.eax=R32(A+0x4du)+8u;
    if(R8(r.eax+2u)) {
        /* 41f91e: search near the destination, else the order target. */
        a[0]=A;mn_call(g,0x41dbccu,a,1,&r);r.ecx=A;
        a[0]=A;a[1]=r.eax;mn_call(g,0x41e0e0u,a,2,&r);
        if(!(r.eax&&A==R32(r.eax+0x1a4u))&&R16(A+0x23eu)==4u)r.eax=R32(A+0x248u);
        if(r.eax)S=r.eax;
        r.eax=S;
        mn_retarget(m,A,r.eax,&r.ecx,&r.ecx,&r.edx);
        if(!R16(S+0x1a8u)) {
            r.eax=A+0x4du;a[0]=r.eax;a[1]=VA(0x499784u);mn_call(g,0x413e50u,a,2,&r);
            a[0]=A;mn_call(g,0x41ce08u,a,1,&r);
        } else { a[0]=A;a[1]=S;mn_call(g,0x41c0f4u,a,2,&r); }
        return pair(r.eax,r.edx);
    }
    r.eax=R32(A+0x4du)+8u;
    if(R8(r.eax+3u))return pair(r.eax,r.edx);
    /* 41f9d8: a new target that attacks A, the order target, or none. */
    old=R32(A+0x1a4u);
    a[0]=A;a[1]=0x24;a[2]=0;mn_call(g,0x411fa0u,a,3,&r);
    if(!(r.eax&&A==R32(r.eax+0x1a4u))&&R16(A+0x23eu)==4u) {
        int keep=0;
        if(R32(A+0x248u)) {
            const uint32_t O=R32(A+0x248u);r.edx=O;
            if(R8(O+0xa4u))r.edx=1;
            else { r.edx=O+0x92u;r.ecx=r.edx;r.edx=R8(r.ecx+7u)?1u:0u; }
            if(!(uint16_t)r.edx) { r.eax=R32(A+0x248u);keep=1; }
        }
        if(!keep)W16(A+0x23eu,0);
    }
    r.edx=r.eax;
    {
        uint32_t done=0;
        if(mn_retarget(m,A,r.edx,&r.ecx,&r.eax,&done)) { r.ecx=done;r.eax=done; }
    }
    if(R32(A+0x1a4u)) { r.eax=A+0x92u;W8(r.eax+0x1au,1);a[0]=A;mn_call(g,0x41d5ecu,a,1,&r);return pair(r.eax,r.edx); }
    if(old) { a[0]=A;mn_call(g,0x41eb34u,a,1,&r); }
    return pair(r.eax,r.edx);
}
