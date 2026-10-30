#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/mfd_keys.h"
/* Synthetic state for the core (the MFD's key handlers). The image
 * base is placed so that the enable table 49cbd8 falls inside the arena;
 * the MFD gauge G and its menu lie above it. A logging call function stands
 * in for every game call (target, arguments, count, entry EAX/EDX/ECX and
 * the frame's EBX/ESI/EDI/return address) and returns registers derived
 * from the call number, panel_visible 431410 an AL chosen by the test.
 * Expected values are worked out by hand from the contract in cockpit.md
 *. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE+0x1000u-0x49cbd8u+0x400000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define GAUGE (BASE+0x10000u)
#define MENU (BASE+0x11000u)
#define TOP (BASE+0x12000u)
static uint8_t arena[0x20000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mfd_keys line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static uint8_t b8(uint32_t a){return arena[a-BASE];}
typedef struct call { uint32_t va,arg[3],count,eax,edx,ecx,ebx,esi,edi,ret; } call;
static call calls[64];static unsigned call_count,observed;
static es2_mfd_frame *active_frame;
static uint32_t visible_eax;
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<64);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->va=f-IMAGE+0x400000u;
    for(unsigned i=0;i<n&&i<3;++i)k->arg[i]=a[i];k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    if(active_frame){k->ebx=active_frame->call.ebx;k->esi=active_frame->call.esi;k->edi=active_frame->call.edi;k->ret=active_frame->call.ret;}
    if(k->va==0x431410u){r->eax=visible_eax;r->edx=0x7700u;r->ecx=0x7c00u;return;}
    if(k->va==0x43f9c0u){r->eax=r32(0,MENU+0x32u);r->edx=0x6600u;r->ecx=0x6c00u;return;}
    r->eax=0xa000u+call_count;r->edx=0xd000u+call_count;r->ecx=0xc000u+call_count;
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;(void)slot;++observed;}
static const es2_weapon_graph graph={IMAGE,0,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0,0xe1,0xe2};
static void setup(uint32_t mode){
    memset(arena,0,sizeof(arena));call_count=observed=0;active_frame=NULL;visible_eax=0x12345601u;
    w32(0,GAUGE+0xd1,MENU);w32(0,GAUGE+0xc1,mode);w32(0,GAUGE+0xfb,0x55u);w32(0,GAUGE+0xb1,2);
}
static void check_call(unsigned n,uint32_t va,unsigned count,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t eax,uint32_t edx,uint32_t ecx){
    CHECK(n<call_count);const call *k=calls+n;
    CHECK(k->va==va&&k->count==count&&k->arg[0]==a0);
    if(count>1)CHECK(k->arg[1]==a1);
    if(count>2)CHECK(k->arg[2]==a2);
    CHECK(k->eax==eax&&k->edx==edx&&k->ecx==ecx);
}
static uint64_t pair(uint32_t eax,uint32_t edx){return (uint64_t)edx<<32|eax;}
static uint32_t al1(uint32_t eax){return (eax&0xffffff00u)|1u;}
static void test_leaves(void){
    uint32_t ecx;
    /* menu_select: the old word to +37h, k to +32h. */
    setup(0);w32(0,MENU+0x32,0x00030002u);
    uint64_t v=es2_menu_select_registers(&memory,MENU,5,entry,&ecx);
    CHECK(v==pair(MENU,0x00000002u)&&ecx==5&&r32(0,MENU+0x32)==5&&r16(0,MENU+0x37)==2);
    const uint32_t high[3]={0,0xabcd0000u,0};
    w32(0,MENU+0x32,0x1234u);v=es2_menu_select_registers(&memory,MENU,1,high,&ecx);
    CHECK(v==pair(MENU,0xabcd1234u)&&r16(0,MENU+0x37)==0x1234);
}
static void test_select(void){
    /* mfd_key_select: o matches and mode 1: mfd_entry(G, n), E6h set. */
    setup(1);uint64_t v=es2_mfd_key_select_registers(&memory,&graph,NULL,0x51,0xd1,GAUGE,0x55,3,entry);
    CHECK(call_count==1);check_call(0,0x4470a4,2,GAUGE,3,0,0x55,3,0xe2);
    CHECK(b8(GAUGE+0xe6)==1&&v==pair(0xa001,0xd001));
    /* other o, other mode: nothing, EAX = o. */
    setup(1);v=es2_mfd_key_select_registers(&memory,&graph,NULL,0,0,GAUGE,0x56,3,entry);
    CHECK(call_count==0&&b8(GAUGE+0xe6)==0&&v==pair(0x56,0xe1));
    setup(2);v=es2_mfd_key_select_registers(&memory,&graph,NULL,0,0,GAUGE,0x55,3,entry);
    CHECK(call_count==0&&v==pair(0x55,0xe1));
}
static void test_steps(void){
    /* next from 2: 3 disabled, 4 enabled; prev from 2: 1, 0 disabled, wraps to 5. */
    setup(1);arena[MENU+0x2c+3-BASE]=1;arena[MENU+0x2c+0-BASE]=3;arena[MENU+0x2c+1-BASE]=1;
    uint64_t v=es2_mfd_entry_next_registers(&memory,&graph,NULL,0,0,GAUGE,entry);
    check_call(0,0x4470a4,2,GAUGE,4,0,GAUGE,MENU,0xe2);CHECK(v==pair(4,0xd001));
    call_count=0;v=es2_mfd_entry_prev_registers(&memory,&graph,NULL,0,0,GAUGE,entry);
    check_call(0,0x4470a4,2,GAUGE,5,0,GAUGE,MENU,0xe2);CHECK(v==pair(5,0xd001));
    /* next from 5 wraps to 0. */
    setup(1);w32(0,GAUGE+0xb1,5);v=es2_mfd_entry_next_registers(&memory,&graph,NULL,0,0,GAUGE,entry);
    check_call(0,0x4470a4,2,GAUGE,0,0,GAUGE,MENU,0xe2);CHECK(v==pair(0,0xd001));
    /* with the frame: EBX = the index, the caller's ESI/EDI, the return address. */
    setup(1);es2_mfd_frame f={{0,0,0,0},0};active_frame=&f;
    es2_mfd_entry_prev_registers(&memory,&graph,&f,0x51,0xd1,GAUGE,entry);
    CHECK(calls[0].ebx==1&&calls[0].esi==0x51&&calls[0].edi==0xd1&&calls[0].ret==VA(0x44709b));
}
static void test_key(void){
    /* not visible: EAX 0, EDX panel_visible's. */
    setup(0);visible_eax=0x12345600u;
    uint64_t v=es2_mfd_key_registers(&memory,&graph,NULL,0xd1,GAUGE,0x20,entry);
    CHECK(call_count==1&&v==pair(0,0x7700));check_call(0,0x431410,1,GAUGE+0xeb,0,0,GAUGE+0xeb,0xe1,0xe2);
    /* D in mode 2, enable byte 49cbdf + 26 set: button_press(G, 7, 1). */
    setup(2);arena[VA(0x49cbdf)+26-BASE]=1;
    v=es2_mfd_key_registers(&memory,&graph,NULL,0xd1,GAUGE,0x20,entry);
    check_call(1,0x438d9c,3,GAUGE,7,1,2,0,26);CHECK(v==pair(al1(0xa002u),0xd002));
    /* D with the byte clear: AL = 1 over p, EDX 0. */
    setup(2);v=es2_mfd_key_registers(&memory,&graph,NULL,0xd1,GAUGE,0x20,entry);
    CHECK(call_count==1&&v==pair(1,0));
    v=es2_mfd_key_registers(&memory,&graph,NULL,0xd1,GAUGE,0x2d,entry);CHECK(v==pair(1,0));
    /* X set: button 10. */
    setup(1);arena[VA(0x49cbe2)+13-BASE]=1;v=es2_mfd_key_registers(&memory,&graph,NULL,0,GAUGE,0x2d,entry);
    check_call(1,0x438d9c,3,GAUGE,10,1,1,0,13);
    /* 213h clear: mfd_range(G); set: button 8 with EAX panel_visible's, EDX 13p, ECX p. */
    setup(1);v=es2_mfd_key_registers(&memory,&graph,NULL,0,GAUGE,0x213,entry);
    check_call(1,0x446ff0,1,GAUGE,0,0,0x12345601u,13,1);CHECK(v==pair(al1(0xa002u),0xd002));
    setup(1);arena[VA(0x49cbe0)+13-BASE]=1;v=es2_mfd_key_registers(&memory,&graph,NULL,0,GAUGE,0x213,entry);
    check_call(1,0x438d9c,3,GAUGE,8,1,0x12345601u,13,1);
    /* 214h clear: EAX 13p | 1, EDX p; set: button 9, ECX the popped slot. */
    setup(3);v=es2_mfd_key_registers(&memory,&graph,NULL,0,GAUGE,0x214,entry);
    CHECK(call_count==1&&v==pair(al1(39u),3));
    setup(3);arena[VA(0x49cbe1)+39-BASE]=1;v=es2_mfd_key_registers(&memory,&graph,NULL,0,GAUGE,0x214,entry);
    check_call(1,0x438d9c,3,GAUGE,9,1,39,3,GAUGE+0xeb);
    /* 34h/33h outside mode 1: AL = 1, EDX 34h / 0. */
    setup(0);v=es2_mfd_key_registers(&memory,&graph,NULL,0,GAUGE,0x34,entry);CHECK(v==pair(0x12345601u,0x34));
    v=es2_mfd_key_registers(&memory,&graph,NULL,0,GAUGE,0x33,entry);CHECK(v==pair(0x12345601u,0));
    /* 34h in mode 1: 44703c(G), redraw byte E5h+1; 33h: 447070(G). */
    setup(1);v=es2_mfd_key_registers(&memory,&graph,NULL,0,GAUGE,0x34,entry);
    check_call(1,0x44703c,1,GAUGE,0,0,0x12345601u,0x34,GAUGE+0xeb);CHECK(b8(GAUGE+0xe6)==1&&v==pair(al1(0xa001u),0xd002));
    setup(1);v=es2_mfd_key_registers(&memory,&graph,NULL,0,GAUGE,0x33,entry);
    check_call(1,0x447070,1,GAUGE,0,0,0x12345601u,0,GAUGE+0xeb);CHECK(b8(GAUGE+0xe6)==1&&v==pair(al1(0xa002u),1));
    /* the table: 12h -> entry 4 (ECX = [G+FBh]), 1Eh -> 0 (EDX), 23h -> 2 (EAX). */
    static const struct { uint32_t key,n,eax,edx,ecx,ret; } t[]={
        {0x12,4,1,0x12u-0x33u,0x55,0x446c10},{0x2e,4,1,0x2eu-0x33u,0x55,0x446c10},{0x18,3,6,0x55,GAUGE+0xeb,0x446bfc},
        {0x1e,0,5,0x55,GAUGE+0xeb,0x446bc0},{0x21,5,0x55,0x21u-0x33u,GAUGE+0xeb,0x446c24},
        {0x22,1,3,0x22u-0x33u,0x55,0x446bd4},{0x23,2,0x55,0x23u-0x33u,GAUGE+0xeb,0x446be8}};
    for(unsigned i=0;i<sizeof(t)/sizeof(t[0]);++i) {
        setup(1);es2_mfd_frame f={{0,0,0,0},0};active_frame=&f;
        v=es2_mfd_key_registers(&memory,&graph,&f,0xd1,GAUGE,0xabcd0000u|t[i].key,entry);
        CHECK(call_count==2);check_call(1,0x447158,3,GAUGE,0x55,t[i].n,t[i].eax,t[i].edx,t[i].ecx);
        CHECK(calls[1].ebx==GAUGE&&calls[1].esi==(0xabcd0000u|t[i].key)&&calls[1].edi==0xd1&&calls[1].ret==VA(t[i].ret));
        CHECK(v==pair(al1(0xa002u),0xd002));
    }
    /* outside the table, a zero entry, another mode: EAX 0. */
    setup(1);v=es2_mfd_key_registers(&memory,&graph,NULL,0,GAUGE,0x13,entry);CHECK(call_count==1&&v==pair(0,0x13u-0x33u));
    v=es2_mfd_key_registers(&memory,&graph,NULL,0,GAUGE,0x11,entry);CHECK(v==pair(0,0x11u-0x33u));
    v=es2_mfd_key_registers(&memory,&graph,NULL,0,GAUGE,0x300,entry);CHECK(v==pair(0,0x300u-0x214u));
    setup(0);v=es2_mfd_key_registers(&memory,&graph,NULL,0,GAUGE,0x12,entry);CHECK(call_count==1&&v==pair(0,0x12u-0x33u));
}
static void test_alt(void){
    /* outside 212h..22Eh or a zero entry: EAX 0, the entry EDX. */
    setup(1);uint64_t v=es2_mfd_alt_key_registers(&memory,&graph,NULL,0,GAUGE,0x211,entry);
    CHECK(call_count==0&&v==pair(0,0xe1));
    v=es2_mfd_alt_key_registers(&memory,&graph,NULL,0,GAUGE,0x213,entry);CHECK(call_count==0&&v==pair(0,0xe1));
    v=es2_mfd_alt_key_registers(&memory,&graph,NULL,0,GAUGE,0x22f,entry);CHECK(call_count==0&&v==pair(0,0xe1));
    /* 221h: menu_select(menu, 5), mfd_order(G), mfd_key_select(G, [G+FBh], 5) with EDX = the slot. */
    setup(1);es2_mfd_frame f={{0,0,0,0},TOP};active_frame=&f;w32(0,TOP-4,0x99);
    v=es2_mfd_alt_key_registers(&memory,&graph,&f,0xd1,GAUGE,0x221,entry);
    CHECK(call_count==3);
    check_call(0,0x43f9e0,2,MENU,5,0,4,0xe1,0xe2);CHECK(calls[0].ret==VA(0x446db2)&&calls[0].esi==MENU&&calls[0].ebx==GAUGE);
    check_call(1,0x447248,1,GAUGE,0,0,0xa001,0xd001,0xc001);CHECK(calls[1].ret==VA(0x446dbb));
    check_call(2,0x447158,3,GAUGE,0x55,5,0xa002,0x55,0x99);CHECK(calls[2].ret==VA(0x446dcb));
    CHECK(v==pair(al1(0xa003u),0xd003));
    /* 22Eh: menu_code gives 4 -> the order; 212h wants 7, gets 4 -> no order. */
    setup(1);w32(0,MENU+0x32,4);v=es2_mfd_alt_key_registers(&memory,&graph,NULL,0,GAUGE,0x22e,entry);
    CHECK(call_count==4);check_call(0,0x43f9e0,2,MENU,4,0,1,0xe1,0xe2);
    check_call(1,0x43f9c0,1,MENU,0,0,0xa001,0xd001,0xc001);
    check_call(3,0x447158,3,GAUGE,0x55,4,0xa003,0xd003,0x55);
    setup(1);w32(0,MENU+0x32,4);v=es2_mfd_alt_key_registers(&memory,&graph,NULL,0,GAUGE,0x212,entry);
    CHECK(call_count==2&&v==pair(1,0x6600));
    /* the n codes: 21Eh 0, 222h 1, 223h 2, 218h 3. */
    static const uint32_t code[4]={0x21e,0x222,0x223,0x218};
    for(unsigned n=0;n<4;++n) {
        setup(1);es2_mfd_alt_key_registers(&memory,&graph,NULL,0,GAUGE,code[n],entry);
        CHECK(call_count==3&&calls[0].arg[1]==n&&calls[2].arg[2]==n);
    }
}
int main(void){
    test_leaves();test_select();test_steps();test_key();test_alt();
    printf("mfd_keys: %u checks\n",checks);
    return 0;
}
