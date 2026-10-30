#include "es2/core/actor_step.h"
#include "fixed.h"
/* Row-11 per-member step (docs/reverse-engineering/vtables.md, section
 * "actor_step"). r holds what the original's EAX/EDX/ECX hold at the next game
 * call. Memory is read again wherever the original reads it again after a call
 * (the state pointer, the vtable, the timer and the flag bytes may all change
 * inside the slots). */
#define R32(a) m->read32(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define W32(a,v) m->write32(m->context,(a),(v))
/* A DBSIM VA (direct callees, return addresses) in the image at g->image. */
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)

static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }
/* EBX of the original at the next call and the address after its CALL. */
static void as_frame(es2_actor_call_frame *f,const es2_weapon_graph *g,uint32_t ebx,uint32_t ret) {
    if(f) { f->ebx=ebx;f->ret=VA(ret); }
}
static void as_call(const es2_weapon_graph *g,uint32_t va,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,n,r);
}
/* A virtual or member call: observed by its slot or member offset. */
static void as_indirect(const es2_weapon_graph *g,uint32_t target,unsigned slot,const uint32_t *a,unsigned n,
    es2_weapon_registers *r) {
    if(g->observe)g->observe(g->observer_context,target,slot);
    g->call(g->context,target,a,n,r);
}

/* Original: DBSIM.EXE 0x413eb0. EDX = ECX = P+35h on both paths. */
uint64_t es2_state_frame_registers(const es2_guest_memory *m,uint32_t P,uint32_t *ecx) {
    const uint32_t flags=P+0x35u;
    *ecx=flags;
    if(R8(flags+1u)) { W8(flags+1u,0);return pair(flags,flags); }
    W8(flags,1);W32(P+9u,R32(P+9u)+1u);
    return pair(P,flags);
}

/* Original: DBSIM.EXE 0x411cec. At its calls the original holds EBX = A
 * and EBP = g->scratch; each call is followed by POP ECX. */
uint64_t es2_actor_step_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    es2_actor_call_frame *frame,uint32_t A,const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    uint32_t a[1]={A},vt;
    if(!R32(A+0x4du)) { *ecx=r.ecx;return pair(r.eax,r.edx); }
    /* SETE AL; AND EAX,1: EAX = 1 when the state timer has run out. */
    if(!R32(A+0x52u)) {
        r.eax=1;vt=R32(A);r.edx=vt;
        as_frame(frame,g,A,0x411d0eu);as_indirect(g,R32(vt+0x1cu),0x1c,a,1,&r);r.ecx=A;
    }
    r.eax=A+0x4du;r.edx=R32(r.eax)+8u;
    if(!R8(r.edx)) {
        r.eax+=4u;a[0]=r.eax;
        as_frame(frame,g,A,0x411d25u);as_call(g,0x467aa8u,a,1,&r);r.ecx=a[0];
    }
    a[0]=A;vt=R32(A);r.edx=vt;
    as_frame(frame,g,A,0x411d2cu);as_indirect(g,R32(vt+0x14u),0x14,a,1,&r);r.ecx=A;
    r.edx=A+0x92u;r.eax=r.edx;
    if(R8(r.edx+0x1du)) W8(r.edx+0x1du,0);
    else {
        vt=R32(A);r.ecx=vt;
        as_frame(frame,g,A,0x411d49u);as_indirect(g,R32(vt+0x18u),0x18,a,1,&r);r.ecx=A;
        if((uint16_t)r.eax) { r.eax=0;W32(A+0x52u,0); }
    }
    /* 413eb0(A+4Dh) in core, with the stack dwords its call leaves: the
     * argument, the return address and its saved EBP. */
    const uint32_t P=A+0x4du,ebp=g->scratch;
    W32(ebp-8u,P);W32(ebp-0xcu,VA(0x411d5du));W32(ebp-0x10u,ebp);
    uint64_t v=es2_state_frame_registers(m,P,&r.ecx);
    *ecx=P;
    return v;
}

/* Original: DBSIM.EXE 0x415afc, 0x415b38, 0x415b74 (MECH) and 0x42184c,
 * 0x421888 (FLYER); they differ only in the member offset and the return
 * address. Locals: function EBP-0Ch, delta EBP-8, third dword EBP-4. */
uint64_t es2_state_member_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    es2_actor_call_frame *frame,uint32_t A,unsigned member,uint32_t ret,uint32_t *ecx) {
    const uint32_t L=g->scratch,state=R32(A+0x4du);
    const uint32_t function=R32(state+member),delta=R32(state+member+4u),third=R32(state+member+8u);
    W32(L,function);W32(L+4u,delta);W32(L+8u,third);
    const uint32_t any=function|delta|third;
    if(!any) { *ecx=any;return pair(0,third); }
    es2_weapon_registers r={state,third,any};
    uint32_t a[1]={A+delta};
    as_frame(frame,g,a[0],ret);as_indirect(g,function,member,a,1,&r);
    *ecx=a[0];
    return pair(r.eax,r.edx);
}

/* Original: DBSIM.EXE 0x4217fc. Locals: function EBP-18h, delta EBP-14h,
 * third dword EBP-10h, the position copy EBP-0Ch..-4 (never read again). */
uint64_t es2_flyer_member_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    es2_actor_call_frame *frame,uint32_t A,uint32_t *ecx) {
    const uint32_t L=g->scratch,p=A+0x26u;
    W32(L+0xcu,R32(p));W32(L+0x10u,R32(p+4u));W32(L+0x14u,R32(p+8u));
    const uint32_t state=R32(A+0x4du);
    const uint32_t function=R32(state+0x24u),delta=R32(state+0x28u),third=R32(state+0x2cu);
    W32(L,function);W32(L+4u,delta);W32(L+8u,third);
    const uint32_t any=function|delta|third;
    if(!any) { *ecx=third;return pair(0,state); }
    es2_weapon_registers r={any,state,third};
    uint32_t a[1]={A+delta};
    as_frame(frame,g,a[0],0x421841u);as_indirect(g,function,0x24,a,1,&r);
    *ecx=a[0];
    return pair(r.eax,r.edx);
}
