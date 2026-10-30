#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/hdd_map_keys.h"
/* Synthetic guest: objects at 90000000h, a stack page at A0000000h, the
 * image at B (globals relocated). Calls are logged with target, arguments
 * and entry registers; they return planned registers. */
#define OBJ UINT32_C(0x90000000)
#define STACK UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
#define M (OBJ+0x100)
#define F (STACK+0x80)
static uint8_t arena[8192],stack[256],globals[0x80000];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"hdd_map_keys line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-STACK<sizeof(stack))return stack+(a-STACK);
    if(a-G(0x490000)<sizeof(globals))return globals+(a-G(0x490000));
    fprintf(stderr,"hdd_map_keys: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static uint8_t *b8(uint32_t a){return host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[3],r[3]; unsigned n; } logged;
static logged log_[64];static unsigned calls,observed;
static uint32_t ret_eax[64];
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    r->eax=ret_eax[calls];r->edx=0x2000u+calls;r->ecx=0x3000u+calls;
    ++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;CHECK(slot==0);CHECK(observed==calls);CHECK(f>=0x400000u);++observed;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,0,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){calls=observed=0;memset(log_,0,sizeof(log_));memset(ret_eax,0,sizeof(ret_eax));}
static void build(void){memset(arena,0,sizeof(arena));memset(stack,0,sizeof(stack));memset(globals,0,sizeof(globals));}
#define ACT(i) (OBJ+0x1000+0x100*(i))
int main(void){
    es2_weapon_graph g=graph();uint64_t p;
    /* map_key: an unhandled code returns EAX = 0 and the entry EDX. */
    build();reset();
    p=es2_map_key_registers(&memory,&g,M,0x10041u,entry);
    CHECK(p==((uint64_t)entry[1]<<32)&&calls==0);
    p=es2_map_key_registers(&memory,&g,M,0x0du,entry);
    CHECK(p==((uint64_t)entry[1]<<32)&&calls==0);
    /* D (20h): map_press(M, [M+35h], 0Ah) with EDX = the gadget, EAX = 8. */
    w32(0,M+0x35,0x777);reset();
    p=es2_map_key_registers(&memory,&g,M,0x20u,entry);
    CHECK(calls==1&&log_[0].f==G(0x44d5c0)&&log_[0].a[0]==M&&log_[0].a[1]==0x777&&log_[0].a[2]==0xa);
    CHECK(log_[0].r[0]==8&&log_[0].r[1]==0x777&&log_[0].r[2]==entry[2]);
    CHECK(p==((uint64_t)0x2000u<<32|1u));
    /* E (12h): ECX = the gadget, mode 11h. */
    reset();ret_eax[0]=0x12345678u;
    p=es2_map_key_registers(&memory,&g,M,0x12u,entry);
    CHECK(log_[0].a[2]==0x11&&log_[0].r[0]==13&&log_[0].r[2]==0x777&&log_[0].r[1]==entry[1]);
    CHECK((uint32_t)p==0x12345601u);
    /* Keypad 5: the scroll dwords cleared, EDX = 0, EAX = 1. */
    w32(0,M+0x114,5);w32(0,M+0x118,6);reset();
    p=es2_map_key_registers(&memory,&g,M,0x4cu,entry);
    CHECK(p==1u&&r32(0,M+0x114)==0&&r32(0,M+0x118)==0&&calls==0);
    /* Backspace: button_press([M+8], 0Eh, 1) with EDX = the HDD. */
    w32(0,M+8,0x4321);reset();
    es2_map_key_registers(&memory,&g,M,0x0eu,entry);
    CHECK(calls==1&&log_[0].f==G(0x438d9c)&&log_[0].a[0]==0x4321&&log_[0].a[1]==0xe&&log_[0].r[1]==0x4321&&log_[0].r[0]==15);
    /* Enter without the pick byte: handled, no call. */
    reset();p=es2_map_key_registers(&memory,&g,M,0x1cu,entry);
    CHECK(calls==0&&(uint32_t)p==1u);
    /* Enter in mode 0Bh with a target: map_press(M, [M+39h], 1), EAX = gadget. */
    arena[0x100+0x103]=1;w32(0,M+0x5d,0xb);w32(0,M+0x15c,ACT(0));w32(0,M+0x39,0x999);reset();
    es2_map_key_registers(&memory,&g,M,0x1cu,entry);
    CHECK(calls==1&&log_[0].a[1]==0x999&&log_[0].a[2]==1&&log_[0].r[0]==0x999&&log_[0].r[2]==0xfffffffeu);
    /* Mode 0Bh without a target: nothing; mode 0Ch: map_press(.., 0). */
    w32(0,M+0x15c,0);reset();es2_map_key_registers(&memory,&g,M,0x1cu,entry);CHECK(calls==0);
    w32(0,M+0x5d,0xc);reset();es2_map_key_registers(&memory,&g,M,0x1cu,entry);
    CHECK(calls==1&&log_[0].a[2]==0&&log_[0].r[1]==0x999&&log_[0].r[2]==0xffffffffu);
    w32(0,M+0x5d,0xe);reset();es2_map_key_registers(&memory,&g,M,0x1cu,entry);
    CHECK(calls==1&&log_[0].a[2]==0&&log_[0].r[2]==0xffffffffu);
    w32(0,M+0x5d,0xf);reset();es2_map_key_registers(&memory,&g,M,0x1cu,entry);CHECK(calls==0);
    /* Tab in mode 0Bh: target_cycle1, the target into wingman 1's record. */
    w32(0,M+0x5d,0xb);w32(0,M+0x105,1);reset();ret_eax[0]=ACT(2);
    p=es2_map_key_registers(&memory,&g,M,0x0fu,entry);
    CHECK(calls==3&&log_[0].f==G(0x44ef08)&&log_[0].r[2]==0&&log_[1].f==G(0x44dd14)&&log_[2].f==G(0x462830)&&log_[2].a[0]==0x1a);
    CHECK(r32(0,M+0x15c)==ACT(2)&&r32(0,M+42+0xa1)==ACT(2)&&r32(0,M+42+0xab)==1&&arena[0x100+42+0xa6]==1&&arena[0x100+0x104]==1);
    CHECK(log_[1].r[0]==1&&log_[1].r[1]==1&&log_[1].r[2]==21&&log_[2].r[2]==M);
    /* Tab in mode 0Ch without a result: sound 1Bh. */
    w32(0,M+0x5d,0xc);reset();
    es2_map_key_registers(&memory,&g,M,0x0fu,entry);
    CHECK(calls==2&&log_[0].f==G(0x44ef1c)&&log_[1].f==G(0x462830)&&log_[1].a[0]==0x1b&&r32(0,M+0x15c)==0);

    /* map_press on the panel gadget: button 2's top left + 1. */
    build();w32(0,M+0x35,0x55);w32(0,M+0x3d+8,OBJ+0x800);w32(0,OBJ+0x804,10);w32(0,OBJ+0x808,20);reset();
    es2_map_press_registers(&memory,&g,F,M,0x55,0xc,entry);
    CHECK(calls==1&&log_[0].f==G(0x44d3cc)&&log_[0].a[2]==F-8&&r32(0,F-8)==11&&r32(0,F-4)==21&&arena[0x100+0x15a]==1);
    CHECK(log_[0].r[0]==F-8&&log_[0].r[1]==10&&log_[0].r[2]==20);
    /* Neither gadget: EAX = mode. Map gadget with mode 2: EAX = 1. */
    reset();p=es2_map_press_registers(&memory,&g,F,M,0x56,7,entry);CHECK(calls==0&&(uint32_t)p==7);
    w32(0,M+0x39,0x56);reset();p=es2_map_press_registers(&memory,&g,F,M,0x56,2,entry);CHECK(calls==0&&(uint32_t)p==1);
    /* Mode 0: the cursor less +210h, plus +220h of the context's B. */
    w32(0,G(0x49b088),OBJ+0x1800);w32(0,OBJ+0x1804,OBJ+0x1400);
    w32(0,OBJ+0x1610,3);w32(0,OBJ+0x1614,4);w32(0,OBJ+0x1620,30);w32(0,OBJ+0x1624,40);
    w32(0,F-8,100);w32(0,F-4,200);reset();
    es2_map_press_registers(&memory,&g,F,M,0x56,0,entry);
    CHECK(calls==2&&log_[0].f==G(0x486ece)&&log_[0].a[0]==F-8&&r32(0,F-8)==127&&r32(0,F-4)==236&&log_[1].r[0]==F-8);
    /* Mode 1: the view copy, five calls, the y half negated. */
    w32(0,M+0x14,OBJ+0x1900);w32(0,M+0x18,7);w32(0,M+0x1c,8);w32(0,M+0x20,9);w32(0,M+0x15c,ACT(0));w32(0,F-0xc,5);reset();
    es2_map_press_registers(&memory,&g,F,M,0x56,1,entry);
    CHECK(calls==6&&r32(0,OBJ+0x1904)==7&&r32(0,OBJ+0x1908)==8&&r32(0,OBJ+0x190c)==9&&r32(0,F-0xc)==0xfffffffbu);
    CHECK(log_[2].f==G(0x48c57c)&&log_[2].a[0]==ACT(0)+0x26&&log_[2].a[1]==F-0x24&&log_[2].r[2]==0);
    CHECK(log_[4].f==G(0x44d24c)&&log_[4].a[1]==F-0x10&&log_[4].a[2]==F-0x18&&log_[5].a[2]==F-0x18);

    /* wingman_pick: a living wingman -> 44a748(gadget, index). */
    build();w32(0,G(0x49d6b0),OBJ+0x1a00);w32(0,OBJ+0x1a08,0xabc);arena[0x100+0x11c]=1;reset();
    ret_eax[0]=ACT(1);ret_eax[1]=2;
    es2_wingman_pick_registers(&memory,&g,M,entry);
    CHECK(calls==3&&log_[0].f==G(0x44d888)&&log_[0].a[1]==M+0x11d&&log_[1].f==G(0x433134)&&log_[1].r[0]==ACT(1)+0x92);
    CHECK(log_[2].f==G(0x44a748)&&log_[2].a[0]==0xabc&&log_[2].a[1]==2&&arena[0x100+0x11c]==0&&*b8(G(0x49d6ae))==1);
    arena[0x1100+0x99]=1;reset();ret_eax[0]=ACT(1);
    es2_wingman_pick_registers(&memory,&g,M,entry);CHECK(calls==1);

    /* wingman_index. */
    build();w32(0,G(0x4d03c8)+4,ACT(3));
    p=es2_wingman_index_registers(&memory,&g,ACT(3),entry);CHECK(p==((uint64_t)G(0x4d03cc)<<32|1u));
    p=es2_wingman_index_registers(&memory,&g,ACT(4),entry);CHECK(p==((uint64_t)G(0x4d03d4)<<32|0xffffffffu));
    p=es2_wingman_index_registers(&memory,&g,0,entry);CHECK(p==((uint64_t)entry[1]<<32|0xffffffffu));

    /* mode_next / mode_prev. */
    build();w32(0,G(0x49d6b0),OBJ+0x1a00);w32(0,OBJ+0x1a08,OBJ+0x1b00);
    w32(0,M+0x5d,0x11);reset();es2_mode_next_registers(&memory,&g,M,entry);CHECK(calls==1&&log_[0].a[1]==0xa&&arena[0x1b00+0x10f]==1&&arena[0x1b00+0xb0]==2);
    w32(0,M+0x5d,0x10);reset();es2_mode_next_registers(&memory,&g,M,entry);CHECK(log_[0].a[1]==0x11);
    w32(0,M+0x5d,0xa);reset();es2_mode_prev_registers(&memory,&g,M,entry);CHECK(log_[0].a[1]==0x11);
    w32(0,M+0x5d,0xc);reset();es2_mode_prev_registers(&memory,&g,M,entry);CHECK(log_[0].a[1]==0xb&&log_[0].r[0]==M);
    w32(0,M+0x5d,0xffffffffu);reset();p=es2_mode_prev_registers(&memory,&g,M,entry);CHECK(calls==0&&p==((uint64_t)0xffffffffu<<32|M));

    /* target_cycle1 forwards. */
    reset();es2_target_cycle1_registers(&memory,&g,M,entry);CHECK(calls==1&&log_[0].f==G(0x44ef30)&&log_[0].a[1]==1&&log_[0].r[0]==M);

    /* target_cycle: four actors, side byte at [[A+45h]+12h]; current = 1. */
    build();
    w32(0,G(0x4a9af8),OBJ+0x1f00);w16(0,G(0x4a9afe),4);
    for(unsigned i=0;i<4;++i){w32(0,OBJ+0x1f00+4*i,ACT(i));w32(0,ACT(i)+0x45,OBJ+0x1e00+0x20*i);arena[0x1e00+0x20*i+0x12]=(uint8_t)(i==1?0xff:1);}
    w32(0,M+0x15c,ACT(1));
    /* side -1 (FFFFh) matches the sign-extended byte: the search starts after 1. */
    reset();ret_eax[1]=1;
    p=es2_target_cycle_registers(&memory,&g,M,0xffffu,entry);
    CHECK(calls==2&&log_[0].a[1]==ACT(2)&&log_[1].a[1]==ACT(3)&&(uint32_t)p==ACT(3));
    /* Nothing accepted: wraps once, ends at the start entry. */
    reset();p=es2_target_cycle_registers(&memory,&g,M,0xffffu,entry);
    CHECK(calls==4&&log_[2].a[1]==ACT(0)&&log_[3].a[1]==ACT(1)&&(uint32_t)p==0);
    /* Side mismatch: the first entry is tried first. */
    reset();ret_eax[0]=0x101;
    p=es2_target_cycle_registers(&memory,&g,M,0u,entry);
    CHECK(calls==1&&log_[0].a[1]==ACT(0)&&(uint32_t)p==ACT(0));
    /* Count 0: no entry; 44f03c gets a null actor, then EAX = 0. */
    w16(0,G(0x4a9afe),0);w32(0,M+0x15c,0);reset();
    p=es2_target_cycle_registers(&memory,&g,M,0u,entry);
    CHECK(calls==1&&log_[0].a[1]==0&&(uint32_t)p==0);
    printf("PASS: hdd_map_keys %u checks\n",checks);
    return 0;
}
