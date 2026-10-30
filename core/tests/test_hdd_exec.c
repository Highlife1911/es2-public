#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/hdd_exec.h"
/* Synthetic state for the core (the HDD button executor). The image
 * base is placed so that the globals 4d03c8 (wingman table), 4d03d4 (order
 * record), 4d3b96/4d3b9a (lamp sounds) and 6c5f70 (graphics context
 * pointer) fall inside the arena; the HDD gauge H, its page object M, the
 * page W, the context X, a wingman A and the frame f lie above them. A
 * logging call function stands in for every game call (target, arguments,
 * count, entry EAX/EDX/ECX and the frame's EBX/ESI/EDI/return address) and
 * returns registers derived from the call number; the wingman getters
 * 44ba08/44b9b0 return a wingman chosen by the test. Expected values are
 * worked out by hand from the contract in cockpit.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE+0x1000u-0x4d0000u+0x400000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define H (BASE+0x200000u)
#define M (BASE+0x201000u)
#define W (BASE+0x202000u)
#define X (BASE+0x203000u)
#define A (BASE+0x204000u)
#define F (BASE+0x206000u)
#define TOP (F-0x2cu)
static uint8_t arena[0x208000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"hdd_exec line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static uint8_t b8(uint32_t a){return arena[a-BASE];}
static void s8(uint32_t a,uint8_t v){arena[a-BASE]=v;}
typedef struct call { uint32_t va,arg[3],count,eax,edx,ecx,ebx,esi,edi,ret; } call;
static call calls[32];static unsigned call_count,observed;
static es2_mfd_frame *active_frame;
static uint32_t wingman;
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<32);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->va=f-IMAGE+0x400000u;
    for(unsigned i=0;i<n&&i<3;++i)k->arg[i]=a[i];k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    if(active_frame){k->ebx=active_frame->call.ebx;k->esi=active_frame->call.esi;k->edi=active_frame->call.edi;k->ret=active_frame->call.ret;}
    r->eax=0xa000u+call_count;r->edx=0xd000u+call_count;r->ecx=0xc0c0000u+call_count;
    if(k->va==0x44ba08u||k->va==0x44b9b0u)r->eax=wingman;
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;(void)slot;++observed;}
static const es2_weapon_graph graph={IMAGE,0,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0,0xe1,0xe2};
static void setup(int8_t command,uint32_t page){
    memset(arena,0,sizeof(arena));call_count=observed=0;active_frame=NULL;wingman=0;
    s8(H+0x528,(uint8_t)command);w32(0,H+0x109,page);w32(0,H+0x101,M);w32(0,H+0x105,W);w32(0,H+0x14,0x7777u);
    w32(0,VA(0x6c5f70u),X);w32(0,H+0x517,0xffffffffu);
    w32(0,F-8,0x5a5a5a5au);w32(0,F-0x10,0x5a5a5a5au);
}
static void check_call(unsigned n,uint32_t va,unsigned count,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t eax,uint32_t edx,uint32_t ecx){
    CHECK(n<call_count);const call *k=calls+n;
    CHECK(k->va==va&&k->count==count&&k->arg[0]==a0);
    if(count>1)CHECK(k->arg[1]==a1);
    if(count>2)CHECK(k->arg[2]==a2);
    CHECK(k->eax==eax&&k->edx==edx&&k->ecx==ecx);
}
static uint64_t pair(uint32_t eax,uint32_t edx){return (uint64_t)edx<<32|eax;}
static uint64_t run(void){return es2_hdd_execute_registers(&memory,&graph,NULL,F,0x51,0xd1,H,entry);}
static void test_exits(void){
    /* -1: nothing, not even the locals. */
    setup(-1,0);uint64_t v=run();
    CHECK(v==pair(0,0xe1)&&call_count==0&&b8(H+0x528)==0xff&&r32(0,F-8)==0x5a5a5a5au);
    /* byte +76h set and c > 1: EAX = c+1, EDX = c-1, c kept. */
    setup(3,0);s8(H+0x76,1);v=run();
    CHECK(v==pair(4,2)&&call_count==0&&b8(H+0x528)==3&&r32(0,F-0x10)==0x5a5a5a5au);
    /* byte +76h set and c = 1: executes; the locals are set. */
    setup(1,0);s8(H+0x76,1);v=run();
    CHECK(call_count==1);check_call(0,0x44a60c,2,H,1,0,0,1,0xe2);
    CHECK(v==pair(0xa001,0xd001)&&b8(H+0x528)==0xff&&r32(0,F-8)==0&&r32(0,F-0x10)==4);
    /* out of the table and 8: nothing but the end. */
    setup(15,0);v=run();CHECK(v==pair(0,15)&&call_count==0&&b8(H+0x528)==0xff);
    setup(8,0);v=run();CHECK(v==pair(0,8)&&call_count==0&&b8(H+0x528)==0xff);
    CHECK(observed==0);
}
static void test_pages(void){
    /* 2 on page 0 / 1 / 2, 3 on page 0 / 1. */
    setup(2,0);uint64_t v=run();
    CHECK(call_count==1);check_call(0,0x44d0c0,1,M,0,0,M,2,0xffffffffu);
    CHECK(b8(H+0x10f)==1&&b8(H+0xb0)==2&&b8(H+0x10e)==0&&v==pair(0xa001,0xd001));
    setup(2,1);v=run();
    CHECK(call_count==1);check_call(0,0x450bf4,1,W,0,0,0,W,0);
    CHECK(b8(H+0x10e)==1&&b8(H+0xb0)==2&&b8(H+0x10f)==0);
    setup(2,2);v=run();CHECK(call_count==0&&b8(H+0xb0)==0&&v==pair(0,2));
    setup(3,0);run();check_call(0,0x44d124,1,M,0,0,M,3,0xffffffffu);
    setup(3,1);run();check_call(0,0x450c18,1,W,0,0,0,W,0);
    /* 4 / 5 on page 0, 6 / 7 on page 0 and 1, 9 on page 1 and 0. */
    setup(4,0);run();check_call(0,0x44cff8,1,M,0,0,M,4,0xffffffffu);CHECK(b8(H+0x10f)==1&&b8(H+0xb0)==2);
    setup(5,0);run();check_call(0,0x44d05c,1,M,0,0,0,0xffffffffu,M);CHECK(b8(H+0x10f)==1&&b8(H+0xb0)==2);
    setup(5,2);v=run();CHECK(call_count==0&&v==pair(0,1));
    setup(6,0);run();check_call(0,0x44cf90,1,M,0,0,0,0xffffffffu,M);CHECK(b8(H+0x10f)==1&&b8(H+0xb0)==2);
    setup(6,1);v=run();CHECK(call_count==0&&v==pair(0,0));
    setup(7,0);run();check_call(0,0x44cfc4,1,M,0,0,0xffffffffu,M,0xe2);CHECK(b8(H+0x10f)==1&&b8(H+0xb0)==2);
    setup(7,1);v=run();CHECK(call_count==0&&v==pair(0,7));
    setup(9,1);run();check_call(0,0x450bf4,1,W,0,0,W,9,0);CHECK(b8(H+0x10e)==1&&b8(H+0xb0)==2);
    setup(9,0);v=run();CHECK(call_count==0&&v==pair(0,9));
}
static void test_lamp(void){
    /* 4 on page 1: wingman A with byte +A3h set, lamp off: switched on, the
     * on sound, f-8 into the context, the clip and the draw of the copy. */
    setup(4,1);wingman=A;s8(A+0xa3,1);w16(0,VA(0x4d3b9au),0xfff0u);
    w32(0,H+0xf1,10);w32(0,H+0xf5,20);w32(0,H+0xf9,30);w32(0,H+0xfd,40);
    uint64_t v=run();
    CHECK(call_count==3);check_call(0,0x44ba08,1,H,0,0,0,4,0);
    CHECK(r32(0,H+0x524)==A&&b8(H+0x51f)==1&&r32(0,F-4)==0xfffffff0u&&r32(0,X+0x22c)==F-8);
    check_call(1,0x486704,1,H+0xf1,0,0,H+0xf1,F-8,H);
    CHECK(r32(0,F-0x20)==10&&r32(0,F-0x1c)==20&&r32(0,F-0x18)==31&&r32(0,F-0x14)==40);
    check_call(2,0x43122c,3,1,F-0x20,0,F-0x20,30,40);
    CHECK(b8(H+0x110)==1&&b8(H+0xb0)==2&&v==pair(0xa003,0xd003));
    /* 5 on page 1: the lamp on and the wingman's byte clear: switched off
     * with the off sound; ECX of the clip is that sound. */
    setup(5,1);wingman=A;s8(H+0x51f,1);w16(0,VA(0x4d3b96u),0x0021u);
    run();CHECK(call_count==3);check_call(0,0x44b9b0,1,H,0,0,0,0,0xe2);
    CHECK(b8(H+0x51f)==0&&r32(0,F-4)==0x21);check_call(1,0x486704,1,H+0xf1,0,0,H+0xf1,F-8,0x21);
    /* no change (lamp on, byte set; no wingman, lamp off): no draw, the
     * context untouched. */
    setup(5,1);wingman=A;s8(A+0xa3,1);s8(H+0x51f,1);w16(0,VA(0x4d3b96u),0x21);w16(0,VA(0x4d3b9au),0x22);
    v=run();CHECK(call_count==1&&r32(0,F-4)==0&&r32(0,X+0x22c)==0&&b8(H+0x110)==1&&b8(H+0x51f)==1);
    CHECK(v==pair(1,A+0x92));
    setup(4,1);w16(0,VA(0x4d3b96u),0x21);v=run();
    CHECK(call_count==1&&r32(0,F-4)==0&&b8(H+0x110)==1&&v==pair(0,0xd001));
}
static void test_wingmen(void){
    /* 11 on page 0: wingman 1 of the table, not the current one. */
    setup(11,0);w32(0,VA(0x4d03ccu),A);w32(0,H+0x517,0);s8(M+0x103,1);
    uint64_t v=run();
    CHECK(call_count==2);check_call(0,0x44a748,2,H,1,0,1,A,0xe2);
    check_call(1,0x433e3c,1,2,0,0,M,0xd001,0xc0c0001u);CHECK(v==pair(0xa002,0xd002));
    /* 10 on page 1: the page button first; byte +103h clear: no 433e3c. */
    setup(10,1);w32(0,VA(0x4d03c8u),A);
    run();CHECK(call_count==2);check_call(0,0x44a60c,2,H,0,0,0,0,0xe2);check_call(1,0x44a748,2,H,0,0,0,A,0xc0c0001u);
    /* the current wingman, an empty slot, a dead wingman, page 2. */
    setup(12,0);w32(0,VA(0x4d03d0u),A);w32(0,H+0x517,2);v=run();CHECK(call_count==0&&v==pair(2,A));
    setup(12,0);v=run();CHECK(call_count==0&&v==pair(2,0));
    setup(12,0);w32(0,VA(0x4d03d0u),A);s8(A+0x99,1);v=run();CHECK(call_count==0&&v==pair(2,A));
    setup(12,2);v=run();CHECK(call_count==0&&v==pair(0,1));
}
static void test_orders(void){
    /* 13: byte [M+104h] set, wingman 2 selected: the full order path. */
    setup(13,0);s8(M+0x104,1);w16(0,M+0x5d,0x0abcu);w32(0,H+0x517,0x00050002u);
    uint64_t v=run();
    CHECK(call_count==6);check_call(0,0x44db9c,1,M,0,0,0,13,0xe2);
    CHECK(r16(0,VA(0x4d03d4u))==0x0abc);
    check_call(1,0x44db4c,2,M,VA(0x4d03d4u),0,0xabc,M,M);
    check_call(2,0x431610,3,0x7777u,VA(0x4d03d4u),0x0c0c0002u,0x7777u,0xd002,0x0c0c0002u);
    check_call(3,0x433e3c,1,0,0,0,0xa003,0xd003,0xc0c0003u);
    check_call(4,0x44dc10,1,M,0,0,0xa004,0xd004,0);
    check_call(5,0x44a748,2,H,0xffffffffu,0,0xa005,0xd005,M);CHECK(v==pair(0xa006,0xd006));
    /* no wingman selected: only 44db9c. */
    setup(13,0);s8(M+0x104,1);v=run();CHECK(call_count==1&&v==pair(0xa001,0xd001)&&r16(0,VA(0x4d03d4u))==0);
    /* byte clear: 44dc6c(M, 0, 1); page 1: nothing. */
    setup(13,0);run();CHECK(call_count==1);check_call(0,0x44dc6c,3,M,0,1,0,13,0xe2);
    setup(13,1);v=run();CHECK(call_count==0&&v==pair(0,13));
    /* 14 on any page. */
    setup(14,3);v=run();CHECK(call_count==3);
    check_call(0,0x44dc10,1,M,0,0,0,M,0xe2);check_call(1,0x433e3c,1,0,0,0,0xa001,0xd001,M);
    check_call(2,0x44a748,2,H,0xffffffffu,0,0xa002,0xd002,0);CHECK(v==pair(0xa003,0xd003)&&b8(H+0x528)==0xff);
    /* 0 on page 0. */
    setup(0,0);run();CHECK(call_count==1);check_call(0,0x44a60c,2,H,0,0,0,0,0xe2);
}
static void test_frame(void){
    /* With the frame: EBX/ESI/EDI and return addresses per call, POP ECX
     * from [top-4]. */
    setup(13,0);s8(M+0x104,1);w32(0,H+0x517,1);w32(0,TOP-4,0x7e7e7e7eu);
    es2_mfd_frame frame={{0,0,0,0},TOP};active_frame=&frame;
    es2_hdd_execute_registers(&memory,&graph,&frame,F,0x51,0xd1,H,entry);
    CHECK(call_count==6);
    CHECK(calls[0].ebx==H&&calls[0].esi==M&&calls[0].edi==0xd1&&calls[0].ret==VA(0x44a576u));
    CHECK(calls[1].ecx==0x7e7e7e7eu&&calls[1].edi==VA(0x4d03d4u)&&calls[1].ret==VA(0x44a599u));
    CHECK(calls[2].ret==VA(0x44a5aeu)&&calls[3].ret==VA(0x44a5b8u)&&calls[4].ecx==0x7e7e7e7eu);
    CHECK(calls[4].ret==VA(0x44a5bfu)&&calls[5].ret==VA(0x44a5c8u)&&calls[5].ecx==0x7e7e7e7eu);
    /* the lamp: ESI the context, then H+F1h; EDI the wingman. */
    setup(4,1);wingman=A;s8(A+0xa3,1);w16(0,VA(0x4d3b9au),1);w32(0,TOP-4,0x6d6d6d6du);
    frame.top=TOP;active_frame=&frame;
    es2_hdd_execute_registers(&memory,&graph,&frame,F,0x51,0xd1,H,entry);
    CHECK(call_count==3&&calls[0].esi==X&&calls[0].edi==0xd1&&calls[0].ret==VA(0x44a339u));
    CHECK(calls[1].esi==H+0xf1&&calls[1].edi==A&&calls[1].ret==VA(0x44a3fdu)&&calls[1].ecx==0x6d6d6d6du);
    CHECK(calls[2].ret==VA(0x44a429u)&&calls[2].ecx==r32(0,H+0xfd));
    /* 11: ESI is the wingman entry + 92h at 44a748. */
    setup(11,0);w32(0,VA(0x4d03ccu),A);s8(M+0x103,1);w32(0,TOP-4,0x5c5c5c5cu);
    frame.top=TOP;active_frame=&frame;
    es2_hdd_execute_registers(&memory,&graph,&frame,F,0x51,0xd1,H,entry);
    CHECK(call_count==2&&calls[0].esi==A+0x92&&calls[0].ret==VA(0x44a52cu)&&calls[1].ret==VA(0x44a549u));
}
int main(void){
    test_exits();test_pages();test_lamp();test_wingmen();test_orders();test_frame();
    printf("hdd_exec: %u checks passed\n",checks);
    return 0;
}
