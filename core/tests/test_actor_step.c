#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "es2/core/actor_step.h"
/* Synthetic state for the core (actor_step 411cec, state_frame
 * 413eb0, the update-slot trampolines). The callees are small models that
 * log their target, argument and entry registers and may change what the
 * original reads again. All values are synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE UINT32_C(0x40000000)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define ACTOR (BASE+0x100u)
#define VT1 (BASE+0x400u)
#define VT2 (BASE+0x480u)
#define STATE1 (BASE+0x500u)
#define STATE2 (BASE+0x580u)
#define FRAME (BASE+0x700u)
#define F_DECIDE UINT32_C(0x7100001c)
#define F_MOVE UINT32_C(0x71000014)
#define F_ACT UINT32_C(0x71000018)
#define F_MEMBER UINT32_C(0x72000000)
static uint8_t arena[0x800];static unsigned checks,ncalls;
static uint32_t calls[8],args[8],regs[8][3],frames[8][2],slots[8];
static uint32_t act_eax;
static es2_actor_call_frame cf;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"actor_step line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;slots[ncalls]=slot;}
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<8&&n==1);calls[ncalls]=f;args[ncalls]=a[0];
    regs[ncalls][0]=r->eax;regs[ncalls][1]=r->edx;regs[ncalls][2]=r->ecx;
    frames[ncalls][0]=cf.ebx;frames[ncalls][1]=cf.ret;++ncalls;
    r->eax=0xa0000000u+ncalls;r->edx=0xb0000000u+ncalls;r->ecx=0xc0000000u+ncalls;
    /* The decision installs the second state (timer gate byte set) and
     * switches the vtable; the behaviour returns act_eax. */
    if(f==F_DECIDE){w32(0,ACTOR+0x4d,STATE2);w32(0,ACTOR,VT2);}
    else if(f==F_ACT)r->eax=act_eax;
    else if(f==VA(0x467aa8))w32(0,ACTOR+0x52,0x77);
}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void setup(uint32_t state,uint32_t timer,uint8_t skip){
    for(unsigned i=0;i<sizeof(arena);++i)arena[i]=(uint8_t)(i*7u+3u);
    for(uint32_t vt=VT1;vt<=VT2;vt+=0x80){w32(0,vt+0x14,F_MOVE);w32(0,vt+0x18,F_ACT);w32(0,vt+0x1c,F_DECIDE);}
    w32(0,ACTOR,VT1);w32(0,ACTOR+0x4d,state);w32(0,ACTOR+0x52,timer);w8(ACTOR+0xaf,skip);
    w8(STATE1+8,0);w8(STATE2+8,1);w8(ACTOR+0x82,0);w8(ACTOR+0x83,0);w32(0,ACTOR+0x56,41);
    ncalls=0;
}
int main(void){
    es2_weapon_graph g={IMAGE,FRAME,0,0,0,call,0,observe,0};
    uint32_t ecx=0;
    /* Null state: nothing, the entry registers come back. */
    setup(0,0,0);
    uint64_t v=es2_actor_step_registers(&memory,&g,&cf,ACTOR,entry,&ecx);
    CHECK(v==((uint64_t)entry[1]<<32|entry[0])&&ecx==entry[2]&&ncalls==0&&r32(0,ACTOR+0x56)==41);
    /* Timer run out: decision (EAX = 1, EDX = vtable, ECX = entry), the new
     * state gates the timer, movement through the new vtable (EAX = A+4Dh,
     * ECX = A), behaviour (EAX = EDX = A+92h, ECX = vtable) with AX != 0:
     * decision request, then 413eb0 sets A+82h and counts A+56h. */
    setup(STATE1,0,0);act_eax=0x00010001u;
    v=es2_actor_step_registers(&memory,&g,&cf,ACTOR,entry,&ecx);
    CHECK(ncalls==3&&calls[0]==F_DECIDE&&calls[1]==F_MOVE&&calls[2]==F_ACT);
    CHECK(slots[0]==0x1c&&slots[1]==0x14&&slots[2]==0x18);
    CHECK(args[0]==ACTOR&&regs[0][0]==1&&regs[0][1]==VT1&&regs[0][2]==entry[2]);
    CHECK(frames[0][0]==ACTOR&&frames[0][1]==VA(0x411d0e));
    CHECK(regs[1][0]==ACTOR+0x4d&&regs[1][1]==VT2&&regs[1][2]==ACTOR&&frames[1][1]==VA(0x411d2c));
    CHECK(regs[2][0]==ACTOR+0x92&&regs[2][1]==ACTOR+0x92&&regs[2][2]==VT2&&frames[2][1]==VA(0x411d49));
    CHECK(r32(0,ACTOR+0x52)==0&&r8(ACTOR+0x82)==1&&r32(0,ACTOR+0x56)==42);
    CHECK(v==((uint64_t)(ACTOR+0x82)<<32|(ACTOR+0x4d))&&ecx==ACTOR+0x4d);
    CHECK(r32(0,FRAME-8)==ACTOR+0x4d&&r32(0,FRAME-0xc)==VA(0x411d5d)&&r32(0,FRAME-0x10)==FRAME);
    /* Timer running, ungated: timer step (EAX = A+51h, EDX = state+8,
     * ECX = entry), movement with the timer's EAX and ECX = A+51h; skip
     * byte set: cleared, no behaviour; once-flag A+83h cleared. */
    setup(STATE1,5,1);w8(ACTOR+0x83,1);
    v=es2_actor_step_registers(&memory,&g,&cf,ACTOR,entry,&ecx);
    CHECK(ncalls==2&&calls[0]==VA(0x467aa8)&&calls[1]==F_MOVE&&slots[0]==0);
    CHECK(args[0]==ACTOR+0x51&&regs[0][0]==ACTOR+0x51&&regs[0][1]==STATE1+8&&regs[0][2]==entry[2]&&frames[0][1]==VA(0x411d25));
    CHECK(regs[1][0]==0xa0000001u&&regs[1][1]==VT1&&regs[1][2]==ACTOR+0x51);
    CHECK(r8(ACTOR+0xaf)==0&&r8(ACTOR+0x83)==0&&r8(ACTOR+0x82)==0&&r32(0,ACTOR+0x56)==41&&r32(0,ACTOR+0x52)==0x77);
    CHECK(v==((uint64_t)(ACTOR+0x82)<<32|(ACTOR+0x82))&&ecx==ACTOR+0x4d);
    /* Behaviour with AX = 0 over a nonzero high word: no decision request,
     * EAX (high word kept) is 413eb0's. */
    setup(STATE2,9,0);act_eax=0x12340000u;
    v=es2_actor_step_registers(&memory,&g,&cf,ACTOR,entry,&ecx);
    CHECK(ncalls==2&&calls[0]==F_MOVE&&calls[1]==F_ACT&&regs[0][0]==ACTOR+0x4d&&regs[0][2]==entry[2]);
    CHECK(r32(0,ACTOR+0x52)==9&&(uint32_t)v==ACTOR+0x4d);
    /* Trampolines: empty member (EAX = 0, EDX = 0, ECX = 0), then a member
     * with a delta (argument A+delta, EAX = state, EDX = third, ECX = OR). */
    setup(STATE1,0,0);g.scratch=FRAME-0x20;
    for(unsigned k=0;k<12;++k)w8(STATE1+0x24+k,0);
    v=es2_state_member_registers(&memory,&g,&cf,ACTOR,0x24,0x415b2d,&ecx);
    CHECK(v==0&&ecx==0&&ncalls==0&&r32(0,FRAME-0x20)==0&&r32(0,FRAME-0x18)==0);
    w32(0,STATE1+0x18,F_MEMBER);w32(0,STATE1+0x1c,8);w32(0,STATE1+0x20,0x300);
    v=es2_state_member_registers(&memory,&g,&cf,ACTOR,0x18,0x415b69,&ecx);
    CHECK(ncalls==1&&calls[0]==F_MEMBER&&slots[0]==0x18&&args[0]==ACTOR+8&&frames[0][0]==ACTOR+8&&frames[0][1]==VA(0x415b69));
    CHECK(regs[0][0]==STATE1&&regs[0][1]==0x300&&regs[0][2]==(F_MEMBER|8u|0x300u));
    CHECK(v==((uint64_t)0xb0000001u<<32|0xa0000001u)&&ecx==ACTOR+8);
    CHECK(r32(0,FRAME-0x20)==F_MEMBER&&r32(0,FRAME-0x1c)==8&&r32(0,FRAME-0x18)==0x300);
    /* 4217fc: the position copy, then EAX = OR, EDX = state, ECX = third. */
    setup(STATE1,0,0);g.scratch=FRAME-0x30;
    for(unsigned k=0;k<12;++k)w8(STATE1+0x24+k,0);
    v=es2_flyer_member_registers(&memory,&g,&cf,ACTOR,&ecx);
    CHECK(v==(uint64_t)STATE1<<32&&ecx==0&&ncalls==0);
    CHECK(r32(0,FRAME-0x24)==r32(0,ACTOR+0x26)&&r32(0,FRAME-0x20)==r32(0,ACTOR+0x2a)&&r32(0,FRAME-0x1c)==r32(0,ACTOR+0x2e));
    w32(0,STATE1+0x24,F_MEMBER);w32(0,STATE1+0x28,0xfffffffcu);w32(0,STATE1+0x2c,1);
    v=es2_flyer_member_registers(&memory,&g,&cf,ACTOR,&ecx);
    CHECK(ncalls==1&&calls[0]==F_MEMBER&&slots[0]==0x24&&args[0]==ACTOR-4&&frames[0][1]==VA(0x421841));
    CHECK(regs[0][0]==(F_MEMBER|0xfffffffcu|1u)&&regs[0][1]==STATE1&&regs[0][2]==1&&ecx==ACTOR-4);
    CHECK(r32(0,FRAME-0x30)==F_MEMBER&&r32(0,FRAME-0x2c)==0xfffffffcu&&r32(0,FRAME-0x28)==1);
    /* state_frame alone. */
    setup(STATE1,0,0);w8(ACTOR+0x83,7);
    v=es2_state_frame_registers(&memory,ACTOR+0x4d,&ecx);
    CHECK(v==((uint64_t)(ACTOR+0x82)<<32|(ACTOR+0x82))&&ecx==ACTOR+0x82&&r8(ACTOR+0x83)==0&&r8(ACTOR+0x82)==0);
    w32(0,ACTOR+0x56,0xffffffffu);
    v=es2_state_frame_registers(&memory,ACTOR+0x4d,&ecx);
    CHECK((uint32_t)v==ACTOR+0x4d&&r8(ACTOR+0x82)==1&&r32(0,ACTOR+0x56)==0);
    printf("actor_step core: %u checks passed\n",checks);
    return 0;
}
