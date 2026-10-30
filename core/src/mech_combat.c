#include "es2/core/mech_combat.h"
#include "fixed.h"
/* MECH combat states (docs/reverse-engineering/ai.md, section "MECH combat
 * states"). r holds
 * what the original's EAX/EDX/ECX hold at the next game call; lo() is a 16-bit
 * load or 16-bit arithmetic that keeps the register's high word. Memory is read
 * again wherever the original reads it again after a call or a store. A is the
 * actor (EBX in every original), F = A+92h the flag block its bytes are
 * addressed through, S the steering record that target_bearing fills (+2
 * relative bearing, +4 its magnitude, +6 aspect, +8 its magnitude, +0Ah
 * distance dword, +0Eh drive word, +10h/+14h distance bounds; vehicle.md). */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* A DBSIM VA (the pushed state record). */
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)

static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. */
static void mc_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}
/* EBX/ESI/EDI of the original at its next call and the address after that
 * CALL instruction. */
static void mc_frame(es2_ai_call_frame *f,const es2_weapon_graph *g,uint32_t ebx,uint32_t esi,uint32_t edi,
    uint32_t ret) {
    if(f) { f->ebx=ebx;f->esi=esi;f->edi=edi;f->ret=VA(ret); }
}
/* The "A is out" test that every step but drive_off repeats before
 * engage_end (41c6db, 41c97c, 41ca6c, 41d493, 41d547): EAX = F; byte A+0A5h
 * gives EDX = 1; otherwise EDI = EDX = F, byte A+0A4h gives EAX = 1, else
 * ESI = F and EAX = (byte A+99h set), and EDX = (AX set). Returns EDX. */
static uint32_t mc_out(const es2_guest_memory *m,uint32_t A,es2_weapon_registers *r,uint32_t *esi,uint32_t *edi) {
    const uint32_t F=A+0x92u;
    r->eax=F;
    if(R8(F+0x13u)) r->edx=1;
    else {
        *edi=F;r->edx=F;
        if(R8(F+0x12u)) r->eax=1;
        else { *esi=F;r->eax=R8(F+7u)?1u:0u; }
        r->edx=(uint16_t)r->eax?1u:0u;
    }
    return r->edx;
}
/* engage_end(A) at the return address ret; its EDX:EAX is the step's
 * result. */
static uint64_t mc_end(const es2_weapon_graph *g,es2_ai_call_frame *frame,uint32_t A,uint32_t esi,uint32_t edi,
    uint32_t ret,es2_weapon_registers *r) {
    uint32_t a[1]={A};
    mc_frame(frame,g,A,esi,edi,ret);mc_call(g,0x41c478u,a,1,r);
    return pair(r->eax,r->edx);
}
/* The out test, then engage_end, or EAX = EDX = 0 when A is not out. */
static uint64_t mc_out_end(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,uint32_t A,
    uint32_t esi,uint32_t edi,uint32_t ret,es2_weapon_registers *r) {
    if(!mc_out(m,A,r,&esi,&edi))return 0;
    return mc_end(g,frame,A,esi,edi,ret,r);
}
/* The common tail after a flag in EAX (41c6d7 after engage_done with ECX = A
 * popped, 41c978, 41d48f, 41d543): a nonzero EAX (all 32 bits) or an out
 * actor ends the engagement through engage_end. */
static uint64_t mc_finish(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,uint32_t A,
    uint32_t esi,uint32_t edi,uint32_t ret,es2_weapon_registers *r) {
    if(r->eax)return mc_end(g,frame,A,esi,edi,ret,r);
    return mc_out_end(m,g,frame,A,esi,edi,ret,r);
}

/* Original: DBSIM.EXE 0x41c478 */
uint64_t es2_engage_end_registers(const es2_guest_memory *m,uint32_t A,const uint32_t entry[3]) {
    uint32_t edx=entry[1];
    if(R16(A+0x23eu)==4u) {
        edx=R32(A+0x1a4u);
        if(edx==R32(A+0x248u))W16(A+0x23eu,0);
    }
    return pair(1,edx);
}

/* Original: DBSIM.EXE 0x41de9c. The current state record is read before
 * script_start and stored in B = A+5Ah after it. */
uint64_t es2_skirt_check_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    uint32_t a[2];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    if(!R8(A+0xadu))return pair(0,r.edx);
    const uint32_t old=R32(A+0x4du);
    r.eax=A+0x4du;a[0]=A+0x4du;a[1]=VA(0x499708u);mc_call(g,0x413e50u,a,2,&r);
    W32(A+0x5au,old);
    r.eax=A+0x31eu;a[0]=A+0x26u;a[1]=A+0x31eu;mc_call(g,0x49288cu,a,2,&r);
    W32(A+0x67u,r.eax);
    return pair(lo(r.eax,1u),r.edx);
}

/* Original: DBSIM.EXE 0x41c594. Frame (EBP-30h at scratch): the mirror point
 * P (EBP-30h, held in EDI from the prologue on), the offset point L
 * (EBP-24h) and S (EBP-18h). T = [A+1A4h] is read once after skirt_check and
 * kept in ESI. */
uint64_t es2_attack_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t A,const uint32_t entry[3]) {
    uint32_t a[3];
    const uint32_t P=g->scratch,L=P+0xcu,S=P+0x18u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    (void)edi;
    a[0]=A;mc_frame(frame,g,A,esi,P,0x41c5a9u);mc_call(g,0x41de9cu,a,1,&r);r.ecx=A;
    if((uint16_t)r.eax)return pair(0,r.edx);
    const uint32_t T=R32(A+0x1a4u);
    r.eax=S;a[0]=A;a[1]=S;a[2]=0;mc_frame(frame,g,A,T,P,0x41c5c8u);mc_call(g,0x41e758u,a,3,&r);
    if(fx_s16(R16(S+8u))>=0x1f40) {
        /* 41c6a4: aspect away from A: stop, or reverse when T is off the nose. */
        r.edx=fx_s16(R16(S+4u))<0x2000?0u:0xffffffffu;
        W16(S+0xeu,r.edx);
    } else {
        /* 41c5d7: L = T's position, drive forward unless T is almost
         * behind, L moved 4650h along T's heading turned by a quarter
         * (the side by the signs of the bearing and its magnitude). */
        r.eax=T+0x26u;
        r.edx=R32(T+0x26u);W32(L,r.edx);r.edx=0xffffffffu;
        r.ecx=R32(T+0x2au);W32(L+4u,r.ecx);
        r.eax=R32(T+0x2eu);W32(L+8u,r.eax);
        r.ecx=sx(R16(S+4u));r.eax=0x8000u-r.ecx;
        if((int32_t)r.eax>=0xfa0)r.edx+=2u;
        W16(S+0xeu,r.edx);
        r.edx=fx_s16(R16(S+2u))<0?1u:0u;
        r.ecx=fx_s16(R16(S+4u))>0x4000?1u:0u;
        r.eax=lo(r.eax,0x4000u);
        if(r.edx!=r.ecx)r.eax+=0xffff8000u;
        r.eax=lo(r.eax,r.eax+R16(T+0x10u));
        const uint32_t angle=r.eax;
        r.eax=L;a[0]=L;a[1]=angle;a[2]=0x4650u;mc_frame(frame,g,A,T,P,0x41c63eu);mc_call(g,0x4929fcu,a,3,&r);
        if(R16(S+0xeu)!=0xffffu) {
            /* 41c67c: P = L. */
            r.eax=R32(L+8u);W32(P+8u,r.eax);r.edx=R32(L);W32(P,r.edx);r.ecx=R32(L+4u);W32(P+4u,r.ecx);
        } else {
            /* 41c648: reversing: P = A's position mirrored away from L
             * (L -= A, then P -= L, each z, x, y). */
            r.edx=R32(A+0x2eu);W32(P+8u,r.edx);r.ecx=R32(A+0x26u);W32(P,r.ecx);r.edx=R32(A+0x2au);W32(P+4u,r.edx);
            r.eax=R32(P+8u);W32(L+8u,R32(L+8u)-r.eax);
            r.ecx=R32(P);W32(L,R32(L)-r.ecx);
            W32(L+4u,R32(L+4u)-r.edx);
            r.eax=R32(L+8u);W32(P+8u,R32(P+8u)-r.eax);
            r.edx=R32(L);W32(P,R32(P)-r.edx);
            r.ecx=R32(L+4u);W32(P+4u,R32(P+4u)-r.ecx);
        }
        r.eax=A+0x26u;a[0]=P;a[1]=A+0x26u;mc_frame(frame,g,A,T,P,0x41c697u);mc_call(g,0x492934u,a,2,&r);
        r.eax=lo(r.eax,r.eax-R16(A+0x10u));W16(S+2u,r.eax);
    }
    r.ecx=S;a[0]=A;a[1]=S;mc_frame(frame,g,A,T,P,0x41c6bdu);mc_call(g,0x41e828u,a,2,&r);
    r.eax=lo(r.eax,R16(S+6u));a[0]=A;a[1]=r.eax;a[2]=0;mc_frame(frame,g,A,T,P,0x41c6cdu);mc_call(g,0x41ea7cu,a,3,&r);
    a[0]=A;mc_frame(frame,g,A,T,P,0x41c6d6u);mc_call(g,0x41c4a8u,a,1,&r);r.ecx=A;
    return mc_finish(m,g,frame,A,T,P,0x41c71fu,&r);
}

/* Original: DBSIM.EXE 0x41c86c. S (EBP-18h) at scratch; ESI and EDI stay
 * the caller's until the out tests. */
uint64_t es2_base_attack_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t A,const uint32_t entry[3]) {
    uint32_t a[3];
    const uint32_t S=g->scratch;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    a[0]=A;mc_frame(frame,g,A,esi,edi,0x41c87eu);mc_call(g,0x41de9cu,a,1,&r);r.ecx=A;
    if((uint16_t)r.eax)return pair(0,r.edx);
    r.edx=S;a[0]=A;a[1]=S;a[2]=0;mc_frame(frame,g,A,esi,edi,0x41c897u);mc_call(g,0x41e758u,a,3,&r);
    /* A building without the configuration word +2Eh: aspect 7D00h. */
    r.ecx=R32(A+0x1a4u);r.eax=R32(r.ecx+0x1f2u);
    if(!R16(r.eax+0x2eu)) { r.edx=lo(r.edx,0x7d00u);W16(S+8u,r.edx);W16(S+6u,r.edx); }
    r.eax=S;a[0]=A;a[1]=S;mc_frame(frame,g,A,esi,edi,0x41c8c3u);mc_call(g,0x41c72cu,a,2,&r);
    W32(S+0x10u,0x61a8u);r.ecx=S;W32(S+0x14u,0xffffafc8u);
    a[0]=A;a[1]=S;mc_frame(frame,g,A,esi,edi,0x41c8deu);mc_call(g,0x41e828u,a,2,&r);
    r.eax=lo(r.eax,R16(S+6u));a[0]=A;a[1]=r.eax;a[2]=0;mc_frame(frame,g,A,esi,edi,0x41c8eeu);mc_call(g,0x41ea7cu,a,3,&r);
    if(R16(A+0x23eu)!=4u||!R32(A+0x248u)) {
        r.edx=R32(A+0x1a4u);r.ecx=R32(A+0x45u);a[0]=r.ecx;a[1]=r.edx;
        mc_frame(frame,g,A,esi,edi,0x41c914u);mc_call(g,0x423918u,a,2,&r);
        if(!(uint16_t)r.eax) {
            /* 41c932: T out (bytes T+0A5h, T+0A4h, T+99h). */
            const uint32_t T=R32(A+0x1a4u),F=T+0x92u;
            r.eax=T;r.edx=F;
            if(R8(F+0x13u))r.edx=1;
            else {
                esi=F;
                if(R8(F+0x12u))r.eax=1;
                else { edi=F;r.eax=R8(F+7u)?1u:0u; }
                r.edx=(uint16_t)r.eax?1u:0u;
            }
            r.ecx=sx(r.edx);r.eax=r.ecx;
            return mc_finish(m,g,frame,A,esi,edi,0x41c9c0u,&r);
        }
    }
    /* 41c91c: T destroyed (byte T+99h, sign-extended; DL takes the byte). */
    {
        const uint8_t b=R8(R32(A+0x1a4u)+0x92u+7u);
        r.edx=(r.edx&0xffffff00u)|b;r.ecx=(uint32_t)(int32_t)(int8_t)b;r.eax=r.ecx;
    }
    return mc_finish(m,g,frame,A,esi,edi,0x41c9c0u,&r);
}

/* Original: DBSIM.EXE 0x41c9cc. S (EBP-18h) at scratch. */
uint64_t es2_flyer_attack_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t A,const uint32_t entry[3]) {
    uint32_t a[3];
    const uint32_t S=g->scratch;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    r.eax=S;a[0]=A;a[1]=S;a[2]=0;mc_frame(frame,g,A,esi,edi,0x41c9e4u);mc_call(g,0x41e758u,a,3,&r);
    /* Drive word: reverse, unless the bearing magnitude is below 7D0h and
     * the flyer is wide (aspect magnitude above 7D0h) or far (above 88B8h):
     * stop, or forward when both. */
    W16(S+0xeu,0xffffu);
    if(fx_s16(R16(S+4u))<0x7d0&&(fx_s16(R16(S+8u))>0x7d0||(int32_t)R32(S+0xau)>0x88b8)) {
        W16(S+0xeu,0);
        if(fx_s16(R16(S+8u))>0x7d0&&(int32_t)R32(S+0xau)>0x88b8)W16(S+0xeu,1u);
    }
    r.edx=0;r.ecx=S;W32(S+0x10u,0);W32(S+0x14u,0xf4240u);
    a[0]=A;a[1]=S;mc_frame(frame,g,A,esi,edi,0x41ca39u);mc_call(g,0x41e828u,a,2,&r);
    r.eax=lo(r.eax,R16(S+6u));a[0]=A;a[1]=r.eax;a[2]=0;mc_frame(frame,g,A,esi,edi,0x41ca49u);mc_call(g,0x41ea7cu,a,3,&r);
    /* Beyond 249F0h the state ends (AX = 1 over aim_select's EAX). */
    if((int32_t)R32(S+0xau)>0x249f0)return pair(lo(r.eax,1u),r.edx);
    /* T destroyed (byte T+99h; EAX = T+92h at the call) or A out. */
    r.eax=R32(A+0x1a4u)+0x92u;
    if(R8(r.eax+7u))return mc_end(g,frame,A,esi,edi,0x41cab0u,&r);
    return mc_out_end(m,g,frame,A,esi,edi,0x41cab0u,&r);
}

/* The head of face_off and flank_attack (41d41c, 41d4e4): skirt_check, then
 * target_bearing into S. Returns 0 when skirt_check ended the step. */
static int mc_engage_head(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t A,const uint32_t rets[2],es2_weapon_registers *r) {
    uint32_t a[3];
    const uint32_t S=g->scratch;
    (void)m;
    a[0]=A;mc_frame(frame,g,A,esi,edi,rets[0]);mc_call(g,0x41de9cu,a,1,r);r->ecx=A;
    if((uint16_t)r->eax)return 0;
    r->edx=S;a[0]=A;a[1]=S;a[2]=0;mc_frame(frame,g,A,esi,edi,rets[1]);mc_call(g,0x41e758u,a,3,r);
    return 1;
}
/* The tail of face_off and flank_attack: aim_select with EDX = the aspect
 * word, engage_done, the out test and engage_end. */
static uint64_t mc_engage_tail(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t A,const uint32_t rets[3],es2_weapon_registers *r) {
    uint32_t a[3];
    const uint32_t S=g->scratch;
    r->edx=lo(r->edx,R16(S+6u));a[0]=A;a[1]=r->edx;a[2]=0;mc_frame(frame,g,A,esi,edi,rets[0]);mc_call(g,0x41ea7cu,a,3,r);
    a[0]=A;mc_frame(frame,g,A,esi,edi,rets[1]);mc_call(g,0x41c4a8u,a,1,r);r->ecx=A;
    return mc_finish(m,g,frame,A,esi,edi,rets[2],r);
}

/* Original: DBSIM.EXE 0x41d41c. S (EBP-18h) at scratch. */
uint64_t es2_face_off_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t A,const uint32_t entry[3]) {
    static const uint32_t head[2]={0x41d42eu,0x41d447u},tail[3]={0x41d485u,0x41d48eu,0x41d4d7u};
    uint32_t a[2];
    const uint32_t S=g->scratch;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    if(!mc_engage_head(m,g,frame,esi,edi,A,head,&r))return pair(0,r.edx);
    /* Forward while the bearing magnitude is below 2000h, else reverse
     * (CX = 1, or the 32-bit ECX - 2); keep the distance in 3E80h..88B8h. */
    r.ecx=lo(r.ecx,1u);
    if(fx_s16(R16(S+4u))>=0x2000)r.ecx-=2u;
    W16(S+0xeu,r.ecx);
    r.eax=S;W32(S+0x10u,0x3e80u);W32(S+0x14u,0x88b8u);
    a[0]=A;a[1]=S;mc_frame(frame,g,A,esi,edi,0x41d475u);mc_call(g,0x41e828u,a,2,&r);
    return mc_engage_tail(m,g,frame,esi,edi,A,tail,&r);
}

/* Original: DBSIM.EXE 0x41d4e4. S (EBP-18h) at scratch. */
uint64_t es2_flank_attack_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t A,const uint32_t entry[3]) {
    static const uint32_t head[2]={0x41d4f6u,0x41d50fu},tail[3]={0x41d539u,0x41d542u,0x41d58bu};
    uint32_t a[2];
    const uint32_t S=g->scratch;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    if(!mc_engage_head(m,g,frame,esi,edi,A,head,&r))return pair(0,r.edx);
    r.ecx=S;a[0]=A;a[1]=S;mc_frame(frame,g,A,esi,edi,0x41d51cu);mc_call(g,0x41c72cu,a,2,&r);
    r.eax=S;a[0]=A;a[1]=S;mc_frame(frame,g,A,esi,edi,0x41d529u);mc_call(g,0x41e828u,a,2,&r);
    return mc_engage_tail(m,g,frame,esi,edi,A,tail,&r);
}

/* Original: DBSIM.EXE 0x41def0. Only the first five instructions after the
 * prologue run: 41df16 jumps to the epilogue, and nothing reaches
 * 41df1b..41e0d6 (no jump, call or data reference; ai.md). */
uint64_t es2_drive_off_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t A,const uint32_t entry[3]) {
    uint32_t a[1]={A};
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    (void)m;
    mc_frame(frame,g,A,esi,edi,0x41df02u);mc_call(g,0x41de9cu,a,1,&r);r.ecx=A;
    if((uint16_t)r.eax)return pair(0,r.edx);
    mc_frame(frame,g,A,esi,edi,0x41df15u);mc_call(g,0x41d41cu,a,1,&r);
    return pair(r.eax,r.edx);
}
