#include "es2/core/ai_behaviour2.h"
#include "fixed.h"
#include <stddef.h>
/* AI behaviours (docs/reverse-engineering/ai.md, section "Aim selection,
 * attack planning, group scans and retreat"). r
 * holds what the original's EAX/EDX/ECX hold at the next game call; lo() is a
 * 16-bit load or 16-bit arithmetic that keeps the register's high word. Memory
 * is read again wherever the original reads it again after a call or a store. F
 * = A+92h is the flag block the original addresses its bytes through, B = A+5Ah
 * the behaviour block. */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* A DBSIM VA (pushed script tables, the RNG state). */
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)

static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes its resolved target. */
static void b2_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}
static void b2_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* EBX/ESI/EDI of the original at the next call and the address after its
 * CALL instruction. */
static void b2_frame(es2_ai_call_frame *f,const es2_weapon_graph *g,uint32_t ebx,uint32_t esi,uint32_t edi,
    uint32_t ret) {
    if(f) { f->ebx=ebx;f->esi=esi;f->edi=edi;f->ret=VA(ret); }
}
#define RNG_STATE 0x4d2599u
/* The inlined target switch (41d019, 41d07a, 41d0e3, 41d6ea, 41d779,
 * 41d8c0, 41d968): when N is not [A+1A4h], the old target's attacker word
 * +1A2h -1 (read into *dec), [A+1A4h] = N, N's word +1 (read back into
 * *inc) and byte A+9Dh = 1, with *done = A+92h where the copy loads it into
 * a scratch register. */
static void b2_retarget(const es2_guest_memory *m,uint32_t A,uint32_t N,uint32_t *dec,uint32_t *inc,uint32_t *done) {
    if(N==R32(A+0x1a4u))return;
    if(R32(A+0x1a4u)) { *dec=R32(A+0x1a4u);W16(*dec+0x1a2u,R16(*dec+0x1a2u)-1u); }
    W32(A+0x1a4u,N);
    if(N) { *inc=R32(A+0x1a4u);W16(*inc+0x1a2u,R16(*inc+0x1a2u)+1u); }
    if(done)*done=A+0x92u;
    W8(A+0x9du,1);
}

/* Original: DBSIM.EXE 0x41ea7c. Frame: Q (EBP-1Ch) at scratch, the
 * records of virtual +30h at scratch+14h (EBP-8) and scratch+0Ch (EBP-10h).
 * At its calls the original holds EBX = T, ESI = A and EDI = the caller's
 * EDI, or T after 41eabe. */
uint64_t es2_aim_select_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t edi,uint32_t A,uint32_t w,uint32_t T0,const uint32_t entry[3]) {
    uint32_t a[4],vt;
    const uint32_t Q=g->scratch;
    es2_weapon_registers r={T0,entry[1],entry[2]};
    const uint32_t T=T0?T0:R32(A+0x1a4u);
    if(!R16(T+0x1a8u)) {
        r.eax=lo(r.eax,w);
        a[0]=A;a[1]=r.eax;a[2]=T;b2_frame(frame,g,T,A,edi,0x41eaafu);b2_call(g,0x41e984u,a,3,&r);
        return pair(r.eax,r.edx);
    }
    if(R16(T+0x1a8u)==1u) {
        edi=T;vt=R32(T);r.eax=vt;a[0]=T;a[1]=0;b2_frame(frame,g,T,A,edi,0x41eac8u);b2_virtual(g,R32(vt+0x54u),0x54,a,2,&r);
        r.eax=sx(r.eax);r.edx=Q;vt=R32(T);r.ecx=vt;
        a[0]=T;a[1]=r.eax;a[2]=Q;b2_frame(frame,g,T,A,edi,0x41ead9u);b2_virtual(g,R32(vt+0x58u),0x58,a,3,&r);
    } else {
        r.eax=T+0x26u;r.edx=R32(T+0x2eu);W32(Q+8u,r.edx);r.edx=Q+0xcu;
        r.ecx=R32(T+0x26u);W32(Q,r.ecx);r.ecx=Q+0x14u;r.eax=R32(T+0x2au);W32(Q+4u,r.eax);
        vt=R32(T);r.eax=vt;a[0]=T;a[1]=Q+0x14u;a[2]=Q+0xcu;b2_frame(frame,g,T,A,edi,0x41eb00u);
        b2_virtual(g,R32(vt+0x30u),0x30,a,3,&r);
        r.edx=sx(R16(Q+0x10u));W32(Q+8u,R32(Q+8u)+r.edx);
        r.ecx=sx(R16(Q+0xcu));W32(Q,R32(Q)+r.ecx);
        r.eax=sx(R16(Q+0xeu));W32(Q+4u,R32(Q+4u)+r.eax);
    }
    r.ecx=Q;r.edx=lo(r.edx,w);
    a[0]=A;a[1]=Q;a[2]=r.edx;a[3]=T;b2_frame(frame,g,T,A,edi,0x41eb28u);b2_call(g,0x41f5a0u,a,4,&r);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41cf18. The original keeps its eight frame dwords
 * (EBP-4..-20h) only as copies of F, the table, the flag, T and a
 * configuration word; no callee gets their address, so they stay in C. */
uint64_t es2_attack_plan_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    uint32_t a[3],vt,table=0;
    const uint32_t F=A+0x92u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    /* Fire permission byte A+96h (F+4). */
    r.eax=R32(A+0x45u);r.edx=R32(r.eax+0xcu);r.eax=R32(r.edx)+0x92u;
    if(R8(r.eax+0x11u)&&(r.eax=F,!R8(F+0x20u))) W8(F+4u,0);
    else if(!R16(A+0x26bu)) { r.edx=F;r.ecx=F;W8(F+4u,1); }
    r.eax=F;
    if(R8(F+0x1au)) { W8(F+0x1au,0);goto group; }
    {
        const uint32_t s=R16(A+0x23eu);
        r.eax=lo(r.eax,s);
        if(s==0u||s==2u||s==1u) {
            const uint32_t G=R32(A+0x45u);
            r.eax=G;r.edx=R32(G+0x6cu);
            if(!R32(G+4u*r.edx+0x44u))r.edx=lo(r.edx,0xbu);
            else { r.ecx=R32(G+0x6cu);r.eax=R32(G+4u*r.ecx+0x44u);r.edx=lo(r.edx,R16(r.eax)); }
            r.eax=(uint16_t)r.edx==5u||(uint16_t)r.edx==6u?1u:0u;
            r.edx=R32(A+0x4du)+8u;
            const uint32_t s1=R16(A+0x23eu),flag=R8(r.edx+1u);
            r.ecx=(r.ecx&0xffffff00u)|flag;r.edx=(uint32_t)(int32_t)(int8_t)flag;
            if(s1==1u)r.eax=1;
            else if(R16(A+0x23eu))r.eax=0;
            else r.eax=(uint16_t)r.eax?1u:0u;
            if((uint16_t)r.eax&&(uint16_t)r.edx)goto resume;
            a[0]=A;a[1]=1;a[2]=0;b2_call(g,0x411fa0u,a,3,&r);
            b2_retarget(m,A,r.eax,&r.ecx,&r.eax,NULL);
            if(R32(A+0x1a4u))goto group;
            goto resume;
        }
        /* Order states above 2: the order's target, else a new choice when
         * the target is gone or out. */
        r.eax=R32(A+0x248u);
        b2_retarget(m,A,r.eax,&r.ecx,&r.ecx,&r.edx);
        r.edx=R32(A+0x1a4u);
        if(r.edx) { r.eax=r.edx+0x92u;if(!R8(r.eax+7u))goto group; }
        a[0]=A;a[1]=0;a[2]=0;b2_call(g,0x411fa0u,a,3,&r);
        {
            uint32_t done=0;b2_retarget(m,A,r.eax,&r.ecx,&r.eax,&done);
            if(done) { r.edx=done;r.ecx=done; }
        }
        if(R32(A+0x1a4u))goto group;
    }
resume: /* 41d067, 41d133 */
    a[0]=A;b2_call(g,0x41eb34u,a,1,&r);
    return pair(0,r.edx);
group: /* 41d141: the leader starts its idle members' plans. */
    r.edx=R32(A+0x45u);r.ecx=R32(r.edx+0xcu);
    r.eax=A==R32(r.ecx)?1u:0u;
    if(r.eax) {
        for(uint32_t i=0;;++i) {
            r.edx=R32(A+0x45u);
            if(fx_s16(i)>=fx_s16(R16(r.edx+0x10u))) { r.eax=0;break; }
            r.ecx=R32(r.edx+0xcu);r.eax=R32(r.ecx+4u*sx(i));
            if(!r.eax)break;
            const uint32_t E=r.eax;
            if(E==A)continue;
            r.edx=E+0x92u;
            r.edx=R8(E+0xa4u)||R8(E+0x99u)?1u:0u;
            if(r.edx)continue;
            r.edx=R32(E+0x4du)+8u;
            if(R8(r.edx+1u))continue;
            a[0]=E;b2_call(g,0x41d5ecu,a,1,&r);r.ecx=E;
        }
    }
    W16(A+0x2a2u,0xffffu);
    a[0]=A;b2_call(g,0x41cb94u,a,1,&r);r.ecx=A;
    if((uint16_t)r.eax)return pair(0,r.edx);
    {
        const uint32_t T=R32(A+0x1a4u);
        r.edx=T;r.eax=sx(R16(T+0x1a8u));
        if(r.eax==1u||r.eax==3u)table=VA(0x499518u);
        else if(r.eax==2u)table=VA(0x499556u);
        else {
            a[0]=A;b2_call(g,0x41cb94u,a,1,&r);r.ecx=A;
            /* PROVED unreachable (41d207): the first call returned 0 on the
             * same state. The original would push its uninitialized frame
             * dword EBP-0Ch; the core passes 0. */
            if(!(uint16_t)r.eax) {
                r.edx=R32(A+0x1a4u);vt=R32(A);r.ecx=vt;
                a[0]=A;a[1]=r.edx;b2_virtual(g,R32(vt+0x4cu),0x4c,a,2,&r);
                const uint32_t k=sx(r.eax);
                r.eax=k-1u;
                if(k==0u)table=VA(0x4994dau);
                else if(k==1u)table=VA(0x49945eu);
                else if(k==2u) {
                    r.eax=0;r.edx=F;r.ecx=F;
                    uint32_t f=1;
                    if(!R8(F+0x16u)) { r.eax=F;r.edx=F;f=R8(F+0x17u)?1u:0u; }
                    r.ecx=f;r.eax=R32(A+0x1a4u);table=VA(0x4994dau);
                    if(!f) { r.edx=R32(A+0x1f2u);r.ecx=sx(R16(r.edx+0xc8u));if((int32_t)r.ecx>0xb9)table=VA(0x49949cu); }
                }
                /* k outside 0..2 is PROVED unreachable (41d228): every +4Ch
                 * returns 0, 1 or 2. EAX = k-2 there, table as above. */
                else r.eax=k-2u;
                a[0]=A;b2_call(g,0x41ce08u,a,1,&r);r.ecx=A;
            }
        }
    }
    r.eax=table;a[0]=A+0x4du;a[1]=table;b2_call(g,0x413e50u,a,2,&r);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x41d5ec */
uint64_t es2_attack_replan_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    (void)m;uint32_t a[1]={A};
    es2_weapon_registers r={A,entry[1],entry[2]};
    b2_call(g,0x41cf18u,a,1,&r);
    return pair(r.eax,r.edx);
}

/* The common head of 41d60c and 41d7d0 after group_move: drop the target
 * (the copies in both re-test [A+1A4h] once and XOR their scratch register
 * before the second test, PROVED one-way), then the scan timer of the
 * leader or a watching actor. Returns 0 when the step ends here; r holds
 * the result EDX then. order: 41d7d0 tests [A+1A4h] in memory, decrements
 * through EDX and zeroes ECX; 41d60c loads it into EDX first and works the
 * other way round. */
static int b2_scan_head(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,uint32_t s,int order,
    es2_weapon_registers *r) {
    uint32_t a[1];
    const uint32_t F=A+0x92u,B=A+0x5au;
    if(!order)r->edx=R32(A+0x1a4u);
    if(R32(A+0x1a4u)) {
        if(order) { r->edx=R32(A+0x1a4u);W16(r->edx+0x1a2u,R16(r->edx+0x1a2u)-1u);r->ecx=0; }
        else { r->ecx=R32(A+0x1a4u);W16(r->ecx+0x1a2u,R16(r->ecx+0x1a2u)-1u);r->edx=0; }
        W32(A+0x1a4u,0);r->eax=F;W8(F+0xbu,1);
    }
    r->eax=F;
    if(!R8(F+0x24u)&&!s) {
        r->edx=R32(A+0x45u);r->ecx=R32(r->edx+0xcu);r->eax=A==R32(r->ecx)?1u:0u;
        if(!r->eax)return 0;
    }
    a[0]=B;b2_call(g,0x467a48u,a,1,r);
    r->ecx=B;r->edx=(uint16_t)r->eax?0u:1u;
    if(!r->edx)return 0;
    W16(B+1u,10000u);
    r->eax=F;
    if(R8(F+0x13u))r->edx=1;
    else {
        r->edx=F;
        r->eax=R8(F+0x12u)||R8(F+7u)?1u:0u;
        r->edx=r->eax;
    }
    return 1;
}
/* The retreat of 41d60c/41d7d0: a new target (any), then the script at
 * 499800h. */
static void b2_scan_retreat(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,es2_weapon_registers *r) {
    uint32_t a[3];
    a[0]=A;a[1]=0;a[2]=0;b2_call(g,0x411fa0u,a,3,r);
    uint32_t edi=0;b2_retarget(m,A,r->eax,&r->ecx,&r->eax,&edi);
    if(!R32(A+0x1a4u))return;
    W16(A+0x2a2u,0xffffu);
    a[0]=A+0x4du;a[1]=VA(0x499800u);b2_call(g,0x413e50u,a,2,r);
}

/* Original: DBSIM.EXE 0x41d60c */
uint64_t es2_group_scan_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    uint32_t a[3];
    const uint32_t F=A+0x92u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    a[0]=A;b2_call(g,0x41d598u,a,1,&r);r.ecx=A;
    if(!b2_scan_head(m,g,A,0,0,&r))return pair(0,r.edx);
    if(r.edx) { b2_scan_retreat(m,g,A,&r);return pair(0,r.edx); }
    a[0]=A;a[1]=1;a[2]=0;b2_call(g,0x411fa0u,a,3,&r);
    const uint32_t N=r.eax;
    if(!N)return pair(0,r.edx);
    r.edx=R32(A+0x45u);a[0]=r.edx;a[1]=N;b2_call(g,0x423918u,a,2,&r);
    if(!(uint16_t)r.eax)return pair(0,r.edx);
    r.eax=N;
    b2_retarget(m,A,N,&r.ecx,&r.eax,&r.edx);
    r.eax=F;W8(F+0x1au,1);
    a[0]=A;b2_call(g,0x41d5ecu,a,1,&r);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x41d7d0. The order state word is read before
 * group_move (into SI) and tested after it. */
uint64_t es2_order_scan_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    uint32_t a[3];
    const uint32_t F=A+0x92u,s=R16(A+0x23eu);
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    a[0]=A;b2_call(g,0x41d598u,a,1,&r);r.ecx=A;
    if(!b2_scan_head(m,g,A,s,1,&r))return pair(0,r.edx);
    if(r.edx) { b2_scan_retreat(m,g,A,&r);return pair(0,r.edx); }
    if(!s) {
        const uint32_t G=R32(A+0x45u);
        r.eax=G;r.edx=R32(G+0x6cu);
        if(!R32(G+4u*r.edx+0x44u))r.eax=lo(r.eax,0xbu);
        else { r.ecx=R32(G+0x6cu);r.eax=R32(G+4u*r.ecx+0x44u);r.eax=lo(r.eax,R16(r.eax)); }
        if((uint16_t)r.eax!=3u)return pair(0,r.edx);
    } else if(s!=2u)return pair(0,r.edx);
    a[0]=A;a[1]=1;a[2]=0;b2_call(g,0x411fa0u,a,3,&r);
    uint32_t edi=0;b2_retarget(m,A,r.eax,&r.edx,&r.eax,&edi);
    if(!R32(A+0x1a4u))return pair(0,r.edx);
    r.eax=F;W8(F+0x1au,1);
    a[0]=A;b2_call(g,0x41d5ecu,a,1,&r);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x41d2c4. Frame: the steering record S (EBP-18h) at
 * scratch (target_bearing fills all 18h bytes, drive_apply reads it; +2
 * heading word, +4 magnitude, +6 aspect word, +0Eh drive word, +10h/+14h
 * bounds). At its calls the original holds EBX = A, ESI = B and the
 * caller's EDI. */
uint64_t es2_retreat_fire_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t edi,uint32_t A,const uint32_t entry[3]) {
    uint32_t a[3];
    const uint32_t S=g->scratch,B=A+0x5au,F=A+0x92u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.eax=R32(A+0x1a4u);
    if(r.eax) {
        W32(B+7u,r.eax);
        r.edx=R32(A+0x1a4u);
        if(r.edx) { /* 41d2ec repeats the test (PROVED) */
            r.ecx=R32(A+0x1a4u);W16(r.ecx+0x1a2u,R16(r.ecx+0x1a2u)-1u);
            r.edx=0;W32(A+0x1a4u,0);
            r.eax=F;W8(F+0xbu,1);
        }
    }
    r.eax=R32(B+7u);r.edx=S;
    a[0]=A;a[1]=S;a[2]=r.eax;b2_frame(frame,g,A,B,edi,0x41d32cu);b2_call(g,0x41e758u,a,3,&r);
    if(fx_s16(R16(S+4u))<0x3000) { W16(S+0xeu,0xffffu);W16(S+2u,R16(S+2u)+0x8000u); }
    else {
        a[0]=B;b2_frame(frame,g,A,B,edi,0x41d34bu);b2_call(g,0x467aa8u,a,1,&r);
        r.ecx=r.eax?0u:1u;
        if(r.ecx) {
            W32(B+1u,0xfa0u);
            a[0]=VA(RNG_STATE);b2_frame(frame,g,A,B,edi,0x41d36au);b2_call(g,0x492ee0u,a,1,&r);
            r.eax&=0xffff0001u;r.ecx=VA(RNG_STATE);W16(B+5u,r.eax);
        }
        W16(S+0xeu,1u);
        r.eax=lo(r.eax,0xa000u);
        if(!R16(B+5u))r.eax+=0xc000u;
        W16(S+2u,R16(S+2u)+r.eax);
    }
    r.edx=0;W32(S+0x10u,0);W32(S+0x14u,0x989680u);
    r.eax=R32(A+0x317u);
    if(r.eax) { a[0]=r.eax;b2_frame(frame,g,A,B,edi,0x41d3a9u);b2_call(g,0x40f09cu,a,1,&r);r.ecx=a[0]; }
    r.edx=S;a[0]=A;a[1]=S;b2_frame(frame,g,A,B,edi,0x41d3b4u);b2_call(g,0x41e828u,a,2,&r);
    r.ecx=R32(B+7u);r.eax=lo(r.eax,R16(S+6u));
    a[0]=A;a[1]=r.eax;a[2]=r.ecx;b2_frame(frame,g,A,B,edi,0x41d3c6u);b2_call(g,0x41ea7cu,a,3,&r);
    const uint32_t V=R32(B+7u);
    r.eax=V;r.edx=V+0x92u;
    if(R8(V+0xa5u))r.edx=1;
    else {
        r.eax=R8(V+0xa4u)||R8(V+0x99u)?1u:0u;
        r.edx=r.eax;
    }
    r.eax=r.edx?lo(r.eax,1u):0u;
    return pair(r.eax,r.edx);
}
