#include "es2/core/mech_travel.h"
#include "fixed.h"
/* MECH travel, follow, sleep, ram and dead states
 * (docs/reverse-engineering/ai.md, section "MECH travel, follow, sleep, ram
 * and dead states"). r holds what the original's
 * EAX/EDX/ECX hold at the next game call; lo() is a 16-bit load or 16-bit
 * arithmetic that keeps the register's high word. Memory is read again wherever
 * the original reads it again after a call or a store. F = A+92h is the flag
 * block the originals address their bytes through, B = A+5Ah the behaviour
 * block (scan timer B+1, remembered target B+5, ram phase word B+5, ram timer
 * record B+7). */
#define R32(a) m->read32(m->context,(a))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* A DBSIM VA (direct callees, globals). */
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)

static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
static uint32_t lo(uint32_t reg,uint32_t word) { return (reg&0xffff0000u)|(uint16_t)word; }
static uint32_t sx(uint32_t x) { return (uint32_t)fx_s16(x); }
/* Direct calls name the original VA (observer key); the bridge gets the
 * address in this image. A virtual call passes its resolved target. */
static void mtr_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}
static void mtr_virtual(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}
/* EBX/ESI/EDI of the original at the next call and the address after its
 * CALL instruction. */
static void mtr_frame(es2_ai_call_frame *f,const es2_weapon_graph *g,uint32_t ebx,uint32_t esi,uint32_t edi,
    uint32_t ret) {
    if(f) { f->ebx=ebx;f->esi=esi;f->edi=edi;f->ret=VA(ret); }
}
#define RNG_STATE 0x4d2599u
#define TERRAIN_MAP 0x4a0be0u
/* The inlined target drop of 41c418, 41d9cc and 41daac: with a target T,
 * its attacker word +1A2h -1 (T read again into ECX), [A+1A4h] = 0 and
 * byte A+9Dh = 1 with EAX = A+92h. Each copy re-tests the register it
 * just tested (the fall-through is the only edge, PROVED) and then tests
 * the XOR-zeroed register before the increment of the new target's word
 * (always taken, PROVED). EDX = the target or 0 afterwards. */
static void mtr_drop(const es2_guest_memory *m,uint32_t A,es2_weapon_registers *r) {
    r->edx=R32(A+0x1a4u);
    if(!r->edx)return;
    r->ecx=R32(A+0x1a4u);W16(r->ecx+0x1a2u,R16(r->ecx+0x1a2u)-1u);
    r->edx=0;W32(A+0x1a4u,0);
    r->eax=A+0x92u;W8(A+0x9du,1);
}

/* Original: DBSIM.EXE 0x41c3c8. ECX is A+92h at return (POP ECX of the
 * local that held it); every caller pops it. */
uint64_t es2_fire_permit_registers(const es2_guest_memory *m,uint32_t A) {
    const uint32_t F=A+0x92u;
    const uint32_t L=R32(R32(R32(A+0x45u)+0xcu))+0x92u;
    const uint32_t index=R8(L+0x11u)?0x20u:5u;
    W8(F+4u,R8(F+index)?1u:0u);
    return pair(F,F);
}

/* Original: DBSIM.EXE 0x41e554 */
uint64_t es2_dead_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    (void)m;const uint32_t a[4]={A,0,0,0};
    es2_weapon_registers r={A,entry[1],entry[2]};
    mtr_call(g,0x416a04u,a,4,&r);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x41c418 */
uint64_t es2_sleep_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    uint32_t a[4];
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    mtr_drop(m,A,&r);
    a[0]=A;a[1]=0;a[2]=0;a[3]=0;mtr_call(g,0x416a04u,a,4,&r);
    a[0]=A;mtr_call(g,0x41c3c8u,a,1,&r);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x41fac4. The local EBP-4 only holds a copy of P;
 * the turn stays in EDI, the distance in ESI. */
uint64_t es2_point_move_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,uint32_t P,
    const uint32_t entry[3]) {
    uint32_t a[4];
    es2_weapon_registers r={A+0x26u,entry[1],entry[2]};
    a[0]=P;a[1]=A+0x26u;mtr_call(g,0x492934u,a,2,&r);
    r.ecx=A+0x26u;r.eax=lo(r.eax,(uint16_t)r.eax-R16(A+0x10u));r.edx=P;
    const uint32_t h=r.eax;
    a[0]=A+0x26u;a[1]=P;mtr_call(g,0x4928d0u,a,2,&r);
    const uint32_t d=r.eax,turn=fx_sar(sx(h),6);
    uint32_t speed=lo(turn,R16(A+0x252u)?R16(A+0x252u):0xaau);
    if(R16(A+0x23eu)&&(int32_t)d>30000) {
        speed=R32(A+0x317u);
        if(speed) { r.eax=speed;a[0]=speed;mtr_call(g,0x40f09cu,a,1,&r);r.ecx=a[0];speed=r.eax; }
        speed=lo(speed,0x100u);
    }
    r.eax=speed;a[0]=A;a[1]=turn;a[2]=speed;a[3]=0;mtr_call(g,0x416a04u,a,4,&r);
    return pair((int32_t)d<10000?1u:0u,r.edx);
}

/* Original: DBSIM.EXE 0x41fb60. H = [A+45h]+4 is the group's route holder;
 * the index is its word + 1 as a 32-bit increment of the entry EAX with
 * that low word. */
uint64_t es2_leader_route_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    uint32_t a[4];
    const uint32_t H=R32(A+0x45u)+4u;
    es2_weapon_registers r={lo(entry[0],R16(H))+1u,entry[1],entry[2]};
    a[0]=H;a[1]=r.eax;mtr_call(g,0x423b0cu,a,2,&r);
    const uint32_t P=r.eax;
    if(!P) { a[0]=A;a[1]=0;a[2]=0;a[3]=0;mtr_call(g,0x416a04u,a,4,&r);return pair(r.eax,r.edx); }
    a[0]=A;a[1]=P;mtr_call(g,0x41fac4u,a,2,&r);
    if(!(uint16_t)r.eax)return pair(r.eax,r.edx);
    r.eax=R32(A+0x45u)+4u;a[0]=r.eax;mtr_call(g,0x42313cu,a,1,&r);
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x41e488. Frame: the point Q (EBP-0Ch, three
 * dwords) at scratch; terrain_height gets its address. The rest of the
 * 34h-byte frame is never written. */
uint64_t es2_ram_move_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    uint32_t a[5];
    const uint32_t Q=g->scratch;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    a[0]=A;mtr_call(g,0x418f40u,a,1,&r);r.ecx=A;
    r.eax=A+0x26u;r.edx=R32(A+0x26u);W32(Q,r.edx);r.edx=Q;
    r.ecx=R32(A+0x2au);W32(Q+4u,r.ecx);r.ecx=R32(VA(TERRAIN_MAP));
    r.eax=R32(A+0x2eu);W32(Q+8u,r.eax);
    a[0]=r.ecx;a[1]=Q;mtr_call(g,0x46e180u,a,2,&r);
    W32(Q+8u,r.eax);
    r.eax=R32(A+0x1f2u);r.edx=sx(R16(r.eax+0x16u));W32(Q+8u,R32(Q+8u)+r.edx);
    r.eax=A+0x26u;r.edx=R32(Q+8u);W32(A+0x2eu,r.edx);
    r.ecx=R32(Q);W32(A+0x26u,r.ecx);
    r.edx=R32(Q+4u);W32(A+0x2au,r.edx);
    a[0]=A;mtr_call(g,0x4195c8u,a,1,&r);r.ecx=A;
    a[0]=A;mtr_call(g,0x418f74u,a,1,&r);r.ecx=A;
    if(!(uint16_t)r.eax) {
        r.eax=A+0x92u;
        if(!R8(r.eax+0x1fu))return pair(lo(r.eax,1u),r.edx);
    }
    r.ecx=A+0x26u;
    a[0]=A+0x26u;a[1]=0xbb8u;a[2]=0x7d0u;a[3]=0;a[4]=A;mtr_call(g,0x426a20u,a,5,&r);
    /* Every part i < 1Dh with a nonzero word [A+20Eh] + 2i (the pointer read
     * once, the words each time): the part damage virtual +74h. */
    uint32_t parts=R32(A+0x20eu),i=0;
    do {
        if(R16(parts)) {
            const uint32_t vt=R32(A);r.eax=vt;
            a[0]=A;a[1]=i;a[2]=0x7d00u;a[3]=A;mtr_virtual(g,R32(vt+0x74u),0x74,a,4,&r);
        }
        ++i;parts+=2u;
    } while(fx_s16(i)<0x1d);
    return pair(lo(r.eax,1u),r.edx);
}

/* One steering step of 41e570 towards the target (41e5f3, 41e649,
 * 41e694): heading_point(T+26h, A+26h) and vehicle_control(A, turn, 100h,
 * 1). The original pushes the last two slots first. */
static void mtr_ram_steer(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,es2_weapon_registers *r) {
    uint32_t a[4];
    const uint32_t TP=R32(A+0x1a4u)+0x26u;
    r->eax=A+0x26u;r->edx=TP;
    a[0]=TP;a[1]=A+0x26u;a[2]=0x100u;a[3]=1;mtr_call(g,0x492934u,a,4,r);
    r->eax=lo(r->eax,(uint16_t)r->eax-R16(A+0x10u));r->ecx=fx_sar(sx(r->eax),8);
    a[0]=A;a[1]=r->ecx;a[2]=0x100u;a[3]=1;mtr_call(g,0x416a04u,a,4,r);
}
/* Original: DBSIM.EXE 0x41e570. B+5 is the charge word (1 = ramming), B+7
 * the charge/rest timer record (word B+8). */
uint64_t es2_ram_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t A,
    const uint32_t entry[3]) {
    uint32_t a[4];
    const uint32_t B=A+0x5au,F=A+0x92u;
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    a[0]=B;mtr_call(g,0x467aa8u,a,1,&r);r.ecx=B;
    r.eax=r.eax?0u:1u;
    if(r.eax) {
        a[0]=A;a[1]=6;a[2]=0;mtr_call(g,0x411fa0u,a,3,&r);
        const uint32_t N=r.eax;
        if(N!=R32(A+0x1a4u)) {
            if(R32(A+0x1a4u)) { r.ecx=R32(A+0x1a4u);W16(r.ecx+0x1a2u,R16(r.ecx+0x1a2u)-1u); }
            W32(A+0x1a4u,N);
            if(N) { r.ecx=R32(A+0x1a4u);W16(r.ecx+0x1a2u,R16(r.ecx+0x1a2u)+1u); }
            r.edx=F;W8(F+0xbu,1);
        }
        W32(B+1u,10000u);
    }
    if(!R32(A+0x1a4u)) {
        a[0]=A;a[1]=0;a[2]=0;a[3]=1;mtr_call(g,0x416a04u,a,4,&r);
        return pair(0,r.edx);
    }
    mtr_ram_steer(m,g,A,&r);
    if(!R16(B+5u)) {
        /* 41e705: resting; the timer starts the next charge. */
        r.ecx=B+7u;a[0]=B+7u;mtr_call(g,0x467a48u,a,1,&r);r.ecx=B+7u;
        r.eax=(uint16_t)r.eax?0u:1u;
        if(!r.eax)return pair(0,r.edx);
        a[0]=VA(RNG_STATE);a[1]=0x5dcu;mtr_call(g,0x492f24u,a,2,&r);
        r.eax=lo(r.eax,(uint16_t)r.eax+0xbb8u);W16(B+5u,1u);W16(B+8u,r.eax);
        return pair(0,r.edx);
    }
    for(unsigned k=0;k<2;++k) {
        a[0]=A;mtr_call(g,0x41e488u,a,1,&r);
        const uint32_t T=R32(A+0x1a4u);r.ecx=A;
        if(!T)return pair(0,r.edx);
        mtr_ram_steer(m,g,A,&r);
    }
    r.eax=B+7u;a[0]=B+7u;mtr_call(g,0x467a48u,a,1,&r);r.ecx=B+7u;
    r.edx=(uint16_t)r.eax?0u:1u;
    if(!r.edx)return pair(0,r.edx);
    a[0]=VA(RNG_STATE);a[1]=0xfa0u;mtr_call(g,0x492f24u,a,2,&r);
    r.eax=lo(r.eax,(uint16_t)r.eax+0x1f40u);W16(B+5u,0);W16(B+8u,r.eax);
    return pair(0,r.edx);
}

/* The common tail of 41d9cc and 41daac after the target drop: the scan
 * timer B; when it has run out, target_select(A, 0, 0) into the
 * remembered target B+5 (dword B+1 = 10000) and the fire permission byte
 * A+96h (set with a target or byte A+97h); then with a remembered target
 * V: heading_point(A+26h, V+26h) (V stays pushed as the third slot), V's
 * virtual +3Ch(V, the relative heading) and aim_select(A, the relative
 * heading plus +3Ch's word, V). ret[] are the addresses after the five
 * CALLs; edi is the original's EDI up to heading_point. */
static void mtr_scan_aim(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t edi,uint32_t A,const uint32_t ret[5],es2_weapon_registers *r) {
    uint32_t a[3];
    const uint32_t B=A+0x5au,F=A+0x92u;
    a[0]=B;mtr_frame(frame,g,A,B,edi,ret[0]);mtr_call(g,0x467aa8u,a,1,r);r->ecx=B;
    r->eax=r->eax?0u:1u;
    if(r->eax) {
        a[0]=A;a[1]=0;a[2]=0;mtr_frame(frame,g,A,B,edi,ret[1]);mtr_call(g,0x411fa0u,a,3,r);
        W32(B+5u,r->eax);W32(B+1u,10000u);
        r->eax=F;
        W8(F+4u,R32(B+5u)||R8(F+5u)?1u:0u);
    }
    const uint32_t V=R32(B+5u);
    if(!V)return;
    r->eax=V+0x26u;
    a[0]=A+0x26u;a[1]=V+0x26u;a[2]=V;mtr_frame(frame,g,A,V,A+0x26u,ret[2]);mtr_call(g,0x492934u,a,3,r);
    r->eax=lo(r->eax,(uint16_t)r->eax-R16(V+0x10u));
    const uint32_t angle=r->eax,vt=R32(V);r->edx=vt;
    a[0]=V;a[1]=angle;a[2]=V;mtr_frame(frame,g,A,V,A+0x26u,ret[3]);mtr_virtual(g,R32(vt+0x3cu),0x3c,a,3,r);
    r->ecx=lo(angle,(uint16_t)angle+(uint16_t)r->eax);
    a[0]=A;a[1]=r->ecx;a[2]=V;mtr_frame(frame,g,A,V,A+0x26u,ret[4]);mtr_call(g,0x41ea7cu,a,3,r);
}

/* Original: DBSIM.EXE 0x41d9cc. At its calls the original holds EBX = A,
 * ESI = B (V from the aim on) and EDI = the caller's EDI (A+26h from the
 * aim on). */
uint64_t es2_travel_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t edi,uint32_t A,const uint32_t entry[3]) {
    static const uint32_t ret[5]={0x41da23u,0x41da3bu,0x41da85u,0x41da93u,0x41da9fu};
    uint32_t a[1]={A};
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    mtr_frame(frame,g,A,A+0x5au,edi,0x41d9deu);mtr_call(g,0x41fb60u,a,1,&r);r.ecx=A;
    mtr_drop(m,A,&r);
    mtr_scan_aim(m,g,frame,edi,A,ret,&r);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x41daac. Frame: the local EBP-4 at scratch (the
 * entry ECX from PUSH ECX, then the order actor P). At its calls the
 * original holds EBX = A, ESI = B and EDI = the caller's EDI, then P, P+26h
 * on the point_move path, and from the aim on ESI = V, EDI = A+26h. */
uint64_t es2_follow_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,es2_ai_call_frame *frame,
    uint32_t edi,uint32_t A,const uint32_t entry[3]) {
    static const uint32_t ret[5]={0x41db44u,0x41db5cu,0x41dba6u,0x41dbb4u,0x41dbc0u};
    uint32_t a[4];
    const uint32_t L=g->scratch,B=A+0x5au;
    es2_weapon_registers r={R32(A+0x45u),entry[1],entry[2]};
    a[0]=r.eax;mtr_frame(frame,g,A,B,edi,0x41dac2u);mtr_call(g,0x4238a0u,a,1,&r);r.ecx=a[0];
    const uint32_t P=r.eax;edi=P;W32(L,P);
    r.eax=R32(L)+0x26u;r.edx=A+0x26u;
    a[0]=A+0x26u;a[1]=r.eax;mtr_frame(frame,g,A,B,edi,0x41dad8u);mtr_call(g,0x49288cu,a,2,&r);
    if((int32_t)r.eax<25000) { a[0]=A;a[1]=0;a[2]=0;a[3]=0;mtr_frame(frame,g,A,B,edi,0x41daeeu);mtr_call(g,0x416a04u,a,4,&r); }
    else { edi=P+0x26u;a[0]=A;a[1]=edi;mtr_frame(frame,g,A,B,edi,0x41dafdu);mtr_call(g,0x41fac4u,a,2,&r); }
    mtr_drop(m,A,&r);
    mtr_scan_aim(m,g,frame,edi,A,ret,&r);
    return pair(0,r.edx);
}

/* Original: DBSIM.EXE 0x40f09c (T) (ai.md, section "TURBO
 * engage 40f09c"): T = the TURBO equipment [A+317h] of point_move and
 * retreat_fire. Not yet latched (dword T+81h = 0) and the charge dword
 * T+7Dh above 600 (signed): latch = 1, and with the dword T+79h set
 * sfx_play(2Ch) (entry EAX = T). EAX = T or sfx_play's EAX, EDX the entry
 * EDX or sfx_play's; ECX after the sound is the popped slot, otherwise the
 * entry ECX (restored by the framework; both callers pop ECX at once). */
uint64_t es2_turbo_engage_registers(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t T,
    const uint32_t entry[3]) {
    es2_weapon_registers r={T,entry[1],entry[2]};uint32_t a[1];
    if(!R32(T+0x81u)&&fx_s32(R32(T+0x7du))>600) {
        W32(T+0x81u,1);
        if(R32(T+0x79u)) { a[0]=0x2c;mtr_call(g,0x462830u,a,1,&r);r.ecx=a[0]; }
    }
    return pair(r.eax,r.edx);
}
