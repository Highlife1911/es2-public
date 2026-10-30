#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/input.h"
/* Synthetic checks of the core on an own arena: the selected-weapon
 * fire sequence, the fire line with a scripted quarter table, the aim point
 * copy and node paths, and the throttle lever and set point clamp of the
 * leg input. The in-vitro harness compares against the original; these
 * values are hand-derived. Image globals map into the arena by choosing
 * the image base BASE - 90000h. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x90000u)
#define G(va) (IMAGE+(va)-0x400000u)
#define OBJ (BASE+0x48000u)
static uint8_t arena[0x50000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"input line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;if(a-BASE>=sizeof(arena))fprintf(stderr,"address %08x after %u calls\n",(unsigned)a,checks);CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static unsigned calls;static uint32_t functions[64],args[64][6],entry_eax[64],results[64];
/* Virtual targets are 7700h + slot; direct calls name their original VA. */
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);
    uint32_t va=f>=0x7700u&&f<0x7800u?f:f-IMAGE+0x400000u;
    functions[calls]=va;entry_eax[calls]=r->eax;for(unsigned i=0;i<n&&i<6;++i)args[calls][i]=a[i];
    r->eax=results[calls];r->edx=0x5eed0000u+calls;r->ecx=0xc0de0000u+calls;++calls;
    if(va==0x48043cu)w32(0,a[2],0x11111111u);
}
static es2_weapon_graph graph(void){es2_weapon_graph g={IMAGE,0,0,0,0,call,NULL,NULL,NULL};return g;}
static void reset(void){calls=0;memset(results,0,sizeof(results));}
int main(void){
    const es2_weapon_graph g=graph();const uint32_t entry[3]={0xaaaa0001u,0xbbbb0002u,0xcccc0003u};
    const uint32_t W=OBJ,S=OBJ+0x100,L=OBJ+0x200,list=OBJ+0x300,P=OBJ+0x400,T=OBJ+0x800,vt=OBJ+0xc00,def=OBJ+0xd00,R=OBJ+0xe00;
    memset(arena,0,sizeof(arena));
    for(unsigned k=0;k<0x80;k+=4)w32(0,vt+k,0x7700u+k);
    /* fire_selected: no selection (s8 W+1Dh = -1). */
    arena[W+0x1d-BASE]=0xff;reset();
    CHECK(es2_fire_selected_registers(&memory,&g,W,R,P,entry)==0);CHECK(calls==0);
    /* Selected weapon 1 without a partner; every guard passes, +60h returns 3. */
    arena[W+0x1d-BASE]=1;w32(0,W+4,list);w32(0,list+4,S);w32(0,S,vt);w16(0,G(0x4a9e52),0);w16(0,G(0x4a9e58),1);arena[W+0x18-BASE]=5;
    reset();results[0]=1;results[1]=0xffff0001u;results[2]=0x12340000u;results[3]=0;results[4]=3;
    uint64_t p=es2_fire_selected_registers(&memory,&g,W,R,P,entry);
    CHECK((uint32_t)p==1&&calls==5);
    CHECK(functions[0]==0x772c&&functions[1]==0x7730&&functions[2]==0x7728&&functions[3]==0x772c&&functions[4]==0x7760);
    CHECK(args[1][1]==R&&args[2][1]==P&&args[2][2]==0xffff0001u); /* flag 1 over the +30h result's high word */
    CHECK(arena[W+0x18-BASE]==0&&r16(0,G(0x4d2528))==1&&r16(0,G(0x4d2526))==1);
    /* With partner L = list[s8 def+16h + 1]; its +2Ch refuses: result 0. */
    arena[S+0x4b-BASE]=1;w32(0,S+0x18,def);arena[def+0x16-BASE]=1;w32(0,list+8,L);w32(0,L,vt);
    reset();results[0]=1;results[1]=0;
    p=es2_fire_selected_registers(&memory,&g,W,R,P,entry);
    CHECK(p==((uint64_t)0x5eed0001u<<32)&&calls==2&&functions[1]==0x772c&&args[1][0]==L);
    /* fire_line: 410dbc fires, target at distance 5000h gives five points
     * 1000h apart along +Y (cos 0 = 4000h, the scripted table's only word). */
    w16(0,G(0x4a25c4),0x4000);w32(0,P,vt);w32(0,P+0x202,W);w32(0,P+0x1a4,T);w32(0,P+0x26,100);w32(0,P+0x2a,200);w16(0,P+0x10,0);
    reset();results[0]=1;results[1]=0x5000;results[2]=0;
    p=es2_fire_line_registers(&memory,&g,P,R,entry);
    CHECK(functions[0]==0x410dbc&&args[0][0]==W&&args[0][1]==R&&args[0][2]==P&&entry_eax[0]==R);
    CHECK(functions[1]==0x49288c&&args[1][0]==P+0x26&&args[1][1]==T+0x26&&functions[2]==0x773c);
    CHECK(r32(0,G(0x4a9cc8))==5&&p==((uint64_t)100<<32|5));
    for(unsigned i=0;i<5;++i)CHECK(r32(0,G(0x4a9b88)+8*i)==100&&r32(0,G(0x4a9b88)+8*i+4)==200+0x1000*(i+1));
    /* Far target: the count is clamped to 40; no target: 20. */
    reset();results[0]=1;results[1]=0x7fffffffu;es2_fire_line_registers(&memory,&g,P,R,entry);CHECK(r32(0,G(0x4a9cc8))==40);
    w32(0,P+0x1a4,0);reset();results[0]=1;es2_fire_line_registers(&memory,&g,P,R,entry);CHECK(r32(0,G(0x4a9cc8))==20&&calls==2);
    /* Not fired: the count is cleared and EDX is 410dbc's. */
    reset();p=es2_fire_line_registers(&memory,&g,P,R,entry);CHECK(p==((uint64_t)0x5eed0000u<<32)&&r32(0,G(0x4a9cc8))==0);
    /* aim_point: no weapon, no node: the target position is copied, *p3 = 0. */
    const uint32_t out=OBJ+0xf00,p3=OBJ+0xf20;
    w32(0,P+0x1a4,T);w32(0,P+0x30b,0);w32(0,T,vt);w32(0,T+0x26,7);w32(0,T+0x2a,8);w32(0,T+0x2e,9);w16(0,p3,0x55);
    reset();p=es2_aim_point_registers(&memory,&g,P,out,p3,0,entry);
    CHECK(calls==1&&functions[0]==0x7724&&r32(0,out)==7&&r32(0,out+4)==8&&r32(0,out+8)==9&&r16(0,p3)==0&&p==((uint64_t)p3<<32|8));
    /* A node and an invalid matrix: Euler, then the transform into out. */
    w16(0,T+0x32,0);reset();results[0]=OBJ+0x1000;
    es2_aim_point_registers(&memory,&g,P,out,0,0,entry);
    CHECK(calls==3&&functions[1]==0x47ebb8&&args[1][0]==T+0xc&&args[1][1]==T+0x12&&r16(0,T+0x32)==1);
    CHECK(functions[2]==0x48043c&&args[2][0]==OBJ+0x1014&&args[2][1]==T+0x12&&args[2][2]==out&&r32(0,out)==0x11111111u);
    /* Weapon path: dword P+30Bh set and the target closer than 30,000. */
    w32(0,P+0x30b,0x4242);w32(0,P+0x20e,0x77);reset();results[0]=29999;
    es2_aim_point_registers(&memory,&g,P,out,p3,0x99,entry);
    CHECK(calls==2&&functions[1]==0x40e4dc&&args[1][0]==0x4242&&args[1][1]==T&&args[1][2]==out&&args[1][3]==0x77&&args[1][4]==p3&&args[1][5]==0x99);
    /* leg_input with the throttle lever forward in cockpit view: throttle
     * 300 sets |300 - 256| * 2 = 88 < 100 -> 0; then 0 throttle, q8 zero. */
    w16(0,G(0x4d24f0),0);w16(0,G(0x49a06e),1);w32(0,P+0x1f2,def);w16(0,def+4,0x100);w16(0,def+6,0x200);w16(0,P+0x290,0x1234);
    reset();results[0]=0;results[1]=0x40;results[2]=0xabc;
    p=es2_leg_input_registers(&memory,&g,P,0xffff0010u,300,entry);
    CHECK(r16(0,P+0x290)==0&&arena[P+0x93-BASE]==1&&functions[0]==0x47e0a0&&args[0][0]==0x91&&args[0][1]==0);
    CHECK(functions[1]==0x47e0b0&&args[1][0]==0x200&&args[1][1]==0);
    CHECK(functions[2]==0x416a04&&args[2][1]==0xfffffff0u&&args[2][2]==0x40&&args[2][3]==0&&p==((uint64_t)0x5eed0002u<<32|0xabc));
    /* No lever: throttle -100 makes q8 = 300; set point 1000 + 300 clamps to 400h. */
    w16(0,G(0x49a06e),0);w16(0,P+0x290,1000);reset();results[0]=300;
    es2_leg_input_registers(&memory,&g,P,5,0xff9c,entry);
    CHECK(args[0][1]==100&&r16(0,P+0x290)==0x400&&args[2][1]==0xfffbu); /* |throttle| 100 > 60 but negative: turn negated */
    /* Sign change of the set point zeroes it. */
    w16(0,P+0x290,10);reset();results[0]=(uint32_t)-30;
    es2_leg_input_registers(&memory,&g,P,5,0x20,entry);
    CHECK(r16(0,P+0x290)==0&&args[1][0]==0x200);
    /* targ_next: no target leaves everything; a target's +80h moves
     * the lock word (sign-extended index, W+86h, entry EAX = W+86h). */
    w32(0,vt+0x80,0x7780u);w32(0,vt+0x84,0x7784u);
    const uint32_t TW=OBJ+0x1100;w16(0,TW+0x7d,0xfffe);reset();
    CHECK(es2_targ_next_registers(&memory,&g,TW,0,entry)==((uint64_t)entry[1]<<32|entry[0])&&calls==0);
    reset();results[0]=0xabcd0005u;p=es2_targ_next_registers(&memory,&g,TW,T,entry);
    CHECK(calls==1&&functions[0]==0x7780&&args[0][0]==T&&args[0][1]==0xfffffffeu&&args[0][2]==TW+0x86&&entry_eax[0]==TW+0x86);
    CHECK(r16(0,TW+0x7d)==5&&p==((uint64_t)0x5eed0000u<<32|0xabcd0005u));
    /* targ_aim: a lock (W+7Dh = 2) on a damaged computer (W+7Fh = 105):
     * +84h refuses, 40e4ac, +58h with the part word over 40e4ac's ECX,
     * *p6, the expired timer clears the lock, *p5 = 1. */
    const uint32_t p6=OBJ+0xf30;w16(0,TW+0x7d,2);w16(0,TW+0x7f,105);w32(0,TW+0x86,0x00070003u);w16(0,p6,0x55);w16(0,p3,0x55);
    reset();p=es2_targ_aim_registers(&memory,&g,TW,T,out,p3,p6,entry);
    CHECK(calls==4&&functions[0]==0x7784&&args[0][1]==0x00070003u&&entry_eax[0]==0x00070003u);
    CHECK(functions[1]==0x40e4ac&&args[1][0]==TW&&args[1][1]==T&&entry_eax[1]==0);
    CHECK(functions[2]==0x7758&&args[2][1]==0xc0de0001u+2u&&args[2][2]==out);
    CHECK(functions[3]==0x467aa8&&args[3][0]==TW+0x81&&entry_eax[3]==TW+0x81);
    CHECK(r16(0,p6)==3&&r16(0,p3)==1&&r32(0,TW+0x82)==5000&&r16(0,TW+0x7d)==0xffff&&p==((uint64_t)1<<32));
    /* A working computer (W+7Fh = 104): +84h accepts, no timer; EDX = p6. */
    w16(0,TW+0x7d,2);w16(0,TW+0x7f,104);reset();results[0]=1;results[1]=0x77;
    p=es2_targ_aim_registers(&memory,&g,TW,T,out,0,p6,entry);
    CHECK(calls==2&&functions[1]==0x7758&&args[1][1]==0xc0de0003u&&p==((uint64_t)p6<<32|0x77));
    /* No lock: the fallback copies the target position, *p5 = 0. */
    w16(0,TW+0x7d,0xffff);reset();
    p=es2_targ_aim_registers(&memory,&g,TW,T,out,p3,0,entry);
    CHECK(calls==1&&functions[0]==0x7724&&r32(0,out)==7&&r16(0,p3)==0&&p==((uint64_t)p3<<32|8));
    printf("PASS: input core %u synthetic checks\n",checks);
    return 0;
}
