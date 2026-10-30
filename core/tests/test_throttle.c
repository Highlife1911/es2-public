#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/throttle.h"
/* Synthetic state for the core (throttle slider event, base gadget
 * event, shield step). The image base is placed so that the globals 49a06e,
 * 49db6c and 4d1f46 fall inside the arena; the gadget with its handler
 * object, class table and sub-object, the joystick summary and the shield
 * block lie above them. A logging call function stands in for every game
 * call. Expected values are worked out by hand from the contract in
 * cockpit.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE+0x100u-0x9a06eu)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define GADGET (BASE+0x10000u)
#define HANDLER (BASE+0x10400u)
#define HANDLER_TABLE (BASE+0x10500u)
#define CLASS_TABLE (BASE+0x10600u)
#define SUB_TABLE (BASE+0x10700u)
#define SUMMARY (BASE+0x10800u)
#define SHIELD (BASE+0x10900u)
static uint8_t arena[0x40000],saved[0x40000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"throttle line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct call { uint32_t va,arg[3],count,eax,edx,ecx,slot; } call;
static call calls[8];static unsigned call_count;static unsigned observed_slot,observed_call;
/* Results: EAX = A0h+n, EDX = D0h+n, ECX = C0h+n for the n-th call (the
 * joystick summary returns SUMMARY in EAX). */
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<8);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->va=f;for(unsigned i=0;i<n&&i<3;++i)k->arg[i]=a[i];k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    r->eax=f==VA(0x477904)?SUMMARY:0xa0u+call_count;r->edx=0xd0u+call_count;r->ecx=0xc0u+call_count;
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;if(slot)++observed_slot;else ++observed_call;}
static const es2_weapon_graph graph={IMAGE,0,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0,0xe1,0xe2};
static void setup(void){
    memset(arena,0xcc,sizeof(arena));call_count=observed_slot=observed_call=0;
    w32(0,GADGET+0x42,HANDLER);w32(0,HANDLER,HANDLER_TABLE);w32(0,HANDLER_TABLE,0x1000);
    w32(0,GADGET+0x17,CLASS_TABLE);w32(0,CLASS_TABLE+4,0x2000);
    w32(0,GADGET+0x3e,SUB_TABLE);w32(0,SUB_TABLE+8,0x3000);
}
static void check_call(unsigned n,uint32_t va,unsigned count,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t eax,uint32_t edx,uint32_t ecx){
    CHECK(n<call_count);const call *k=calls+n;
    CHECK(k->va==va&&k->count==count);
    if(count>0)CHECK(k->arg[0]==a0);
    if(count>1)CHECK(k->arg[1]==a1);
    if(count>2)CHECK(k->arg[2]==a2);
    CHECK(k->eax==eax&&k->edx==edx&&k->ecx==ecx);
}
static void test_slider_base(void){
    setup();
    uint64_t v=es2_slider_base_registers(&memory,&graph,GADGET,0x77,entry);
    CHECK(call_count==3&&observed_slot==3&&observed_call==0);
    /* Virtual 0: EAX = event, EDX = handler, ECX = its table. */
    check_call(0,0x1000,3,HANDLER,GADGET,0x77,0x77,HANDLER,HANDLER_TABLE);
    /* Virtual 4 of G+17h: EAX = the table, EDX/ECX those of the first. */
    check_call(1,0x2000,1,GADGET,0,0,CLASS_TABLE,0xd1,0xc1);
    /* Virtual 8 of G+3Eh: EAX of the second, EDX = G+3Eh, ECX = its table. */
    check_call(2,0x3000,1,GADGET+0x3e,0,0,0xa2,GADGET+0x3e,SUB_TABLE);
    CHECK(v==((uint64_t)0xd3<<32|0xa3));
}
static void effect_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    fake_call(c,f,a,n,r);
    if(f==0x1000){w32(0,GADGET+0x17,CLASS_TABLE+0x100);w32(0,GADGET+0x3e,SUB_TABLE+0x100);}
    if(f==0x2500)w32(0,GADGET+0x3e,SUB_TABLE+0x200);
}
static void test_rereads(void){
    setup();
    w32(0,CLASS_TABLE+0x100+4,0x2500);w32(0,SUB_TABLE+0x200+8,0x3500);
    es2_weapon_graph g=graph;g.call=effect_call;
    uint64_t v=es2_slider_base_registers(&memory,&g,GADGET,5,entry);
    CHECK(call_count==3);
    CHECK(calls[1].va==0x2500&&calls[1].eax==CLASS_TABLE+0x100);
    CHECK(calls[2].va==0x3500&&calls[2].arg[0]==GADGET+0x3e&&calls[2].ecx==SUB_TABLE+0x200);
    CHECK((uint32_t)v==0xa3&&(uint32_t)(v>>32)==0xd3);
}
static void lever_setup(unsigned flag,unsigned moving,unsigned mode){
    setup();
    w32(0,VA(0x49db6c),flag);arena[VA(0x4d1f46)-BASE]=(uint8_t)mode;
    arena[SUMMARY-BASE+4]=(uint8_t)moving;w16(0,VA(0x49a06e),0x1234);
    memcpy(saved,arena,sizeof(arena));
}
static void test_slider_event(void){
    /* Button bit clear: only the base event; result and ECX from it. */
    lever_setup(0xfe,1,1);
    uint64_t v=es2_slider_event_registers(&memory,&graph,GADGET,3,entry);
    CHECK(call_count==1&&observed_call==1&&observed_slot==0);
    check_call(0,VA(0x43904c),2,GADGET,3,0,GADGET,0xe1,0xe2);
    CHECK(v==((uint64_t)0xd1<<32|0xa1));
    CHECK(r16(0,VA(0x49a06e))==0x1234);
    /* Bit set, summary byte 0: the joystick call happens, nothing is written;
     * its registers are the result. ECX at the call is G+3Eh. */
    lever_setup(1,0,1);
    v=es2_slider_event_registers(&memory,&graph,GADGET,3,entry);
    CHECK(call_count==2);
    check_call(1,VA(0x477904),0,0,0,0,0xa1,0xd1,GADGET+0x3e);
    CHECK(v==((uint64_t)0xd2<<32|SUMMARY));
    CHECK(r16(0,VA(0x49a06e))==0x1234);
    /* Control byte not 1: same. */
    lever_setup(1,1,2);
    v=es2_slider_event_registers(&memory,&graph,GADGET,3,entry);
    CHECK(call_count==2&&v==((uint64_t)0xd2<<32|SUMMARY)&&r16(0,VA(0x49a06e))==0x1234);
    /* Signed event <= 0: the lever word becomes 1 and EAX is the summary
     * pointer with AX = 1. */
    lever_setup(3,2,1);
    v=es2_slider_event_registers(&memory,&graph,GADGET,0,entry);
    CHECK(r16(0,VA(0x49a06e))==1&&(uint32_t)v==((SUMMARY&0xffff0000u)|1u)&&(uint32_t)(v>>32)==0xd2);
    lever_setup(3,2,1);
    v=es2_slider_event_registers(&memory,&graph,GADGET,0x80000000u,entry);
    CHECK(r16(0,VA(0x49a06e))==1);
    /* Positive event: -1; the word -2 borrows from the high word of EAX. */
    lever_setup(3,2,1);
    v=es2_slider_event_registers(&memory,&graph,GADGET,1,entry);
    CHECK(r16(0,VA(0x49a06e))==0xffff&&(uint32_t)v==(((SUMMARY&0xffff0000u)|1u)-2u)&&(uint32_t)v>>16==(SUMMARY>>16)-1u);
    lever_setup(3,2,1);
    v=es2_slider_event_registers(&memory,&graph,GADGET,0x7fffffffu,entry);
    CHECK(r16(0,VA(0x49a06e))==0xffff);
    /* Only the lever word changes. */
    for(uint32_t j=0;j<sizeof(arena);++j)if(arena[j]!=saved[j]&&BASE+j-VA(0x49a06e)>=2u){fprintf(stderr,"byte %x changed\n",BASE+j);CHECK(0);}
}
static void test_shield_step(void){
    struct { uint16_t before; uint32_t up; uint16_t after; } cases[]={
        {0,1,0x66},{0x39a,1,0x400},{0x399,1,0x3ff},{0x400,1,0x400},{0x7f9a,1,0},{0x7fff,1,0},
        {0x8000,1,0},{0xff9a,1,0},{0xffff,1,0x65},
        {0x66,0,0},{0x67,0,1},{0x100,0,0x9a},{0x65,0,0},{0,0,0},{0x8000,0,0x400},{0x8065,0,0x400},{0x3ff,0xff00,0x400},{0x3ff,0x10000,0x399},
        {0x401,0,0x39b}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
        setup();w16(0,SHIELD+4,cases[i].before);w16(0,SHIELD,0x1111);w16(0,SHIELD+2,0x2222);
        const uint32_t e[3]={0xaaaa0000u|i,0xbbbb0000u|i,0xcccc0000u|i};
        uint64_t v=es2_shield_step_registers(&memory,SHIELD,cases[i].up,e);
        if(r16(0,SHIELD+4)!=cases[i].after){fprintf(stderr,"case %u: %04x -> %04x, want %04x\n",i,cases[i].before,r16(0,SHIELD+4),cases[i].after);CHECK(0);}
        CHECK(r16(0,SHIELD)==0x1111&&r16(0,SHIELD+2)==0x2222&&call_count==0);
        CHECK((uint32_t)v==SHIELD);
        /* EDX: high word of the entry EDX (minus one after a down step),
         * low word the unclamped sum. */
        const uint32_t step=(uint16_t)cases[i].up?0x66u:0xff9au;
        const uint32_t hi=((0xbbbbu-((uint16_t)cases[i].up?0u:1u))&0xffffu);
        CHECK((uint32_t)(v>>32)==(hi<<16|(uint16_t)(cases[i].before+step)));
    }
    /* Only the balance word changes. */
    setup();memcpy(saved,arena,sizeof(arena));w16(0,SHIELD+4,0x300);memcpy(saved+(SHIELD-BASE),arena+(SHIELD-BASE),8);
    es2_shield_step_registers(&memory,SHIELD,1,entry);
    for(uint32_t j=0;j<sizeof(arena);++j)if(arena[j]!=saved[j]&&BASE+j-(SHIELD+4)>=2u){fprintf(stderr,"byte %x changed\n",BASE+j);CHECK(0);}
}
int main(void){
    test_slider_base();
    test_rereads();
    test_slider_event();
    test_shield_step();
    printf("PASS: throttle core, %u checks\n",checks);
    return 0;
}
