#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/torso.h"
/* Synthetic checks of the core on an own arena: the twist servo's
 * direct and approach paths, clamp, snap, track and sound-latch calls, the
 * pitch servo's independent bounds and its convergence call, the weapon
 * convergence frame, and the weapon key dispatch. The in-vitro harness
 * compares against the original; these values are hand-derived. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x90000u)
#define OBJ (BASE+0x48000u)
static uint8_t arena[0x50000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"torso line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static unsigned calls;static uint32_t functions[64],args[64][6],entry[64][3],results[64];
static uint32_t rate_after_approach,out_words[3];
/* Virtual targets are 7700h + slot; direct calls name their original VA. */
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);
    uint32_t va=f>=0x7700u&&f<0x7800u?f:f-IMAGE+0x400000u;
    functions[calls]=va;entry[calls][0]=r->eax;entry[calls][1]=r->edx;entry[calls][2]=r->ecx;
    for(unsigned i=0;i<n&&i<6;++i)args[calls][i]=a[i];
    r->eax=results[calls];r->edx=0x5eed0000u+calls;r->ecx=0xc0de0000u+calls;++calls;
    if(va==0x467adcu)w16(0,a[0],(uint16_t)rate_after_approach);
    if(va==0x492990u)for(unsigned i=0;i<3;++i)w16(0,a[0]+2*i,(uint16_t)out_words[i]);
}
static void reset(void){calls=0;memset(results,0,sizeof(results));}
#define PAIR(a,d) ((uint64_t)(d)<<32|(uint32_t)(a))
int main(void){
    es2_weapon_graph g={IMAGE,0,0,0,0,call,NULL,NULL,NULL};
    const uint32_t regs[3]={0xaaaa0001u,0xbbbb0002u,0xcccc0003u};
    const uint32_t P=OBJ,CFG=OBJ+0x400,C=CFG+2,T=OBJ+0x500,W=OBJ+0x600,list=OBJ+0x700,S=OBJ+0x800,V=OBJ+0x900,vt=OBJ+0xa00;
    memset(arena,0,sizeof(arena));
    for(unsigned k=0;k<0x80;k+=4)w32(0,vt+k,0x7700u+k);
    w32(0,P+0x1f2,CFG);w16(0,C+0x1c,0x100);w16(0,C+0x1e,0x30);w16(0,C+0x20,0x1000);w16(0,C+0x1a,7);
    /* Twist, direct path: |40h| <= |100h| sets the rate; the mean 40h steps 10h. */
    w16(0,P+0x294,0x100);w16(0,P+0x298,0x200);
    reset();results[0]=0x40;results[1]=0x10;
    uint64_t p=es2_torso_twist_registers(&memory,&g,P,0x12340040u,0,0,regs);
    CHECK(calls==2&&functions[0]==0x47e0a0u&&args[0][0]==0x40&&args[0][1]==0x100&&entry[0][0]==0x100&&entry[0][2]==regs[2]);
    CHECK(functions[1]==0x467924u&&args[1][0]==0x40&&entry[1][0]==0x40&&entry[1][1]==0x40);
    CHECK(r16(0,P+0x294)==0x40&&r16(0,P+0x298)==0x210&&p==PAIR(P+0x92,0x5eedf000u));
    /* Approach path: |40h| > |10h|; 467adc moves the rate to 20h, the mean
     * (10h + 20h) >> 1 = 18h steps 7000h and the clamp stops at 1000h. */
    w16(0,P+0x294,0x10);w16(0,P+0x298,0x200);rate_after_approach=0x20;
    reset();results[0]=0x40;results[1]=0x55;results[2]=0;results[3]=0x7000;
    p=es2_torso_twist_registers(&memory,&g,P,0x40,0,0,regs);
    CHECK(calls==4&&functions[1]==0x467924u&&(uint16_t)args[1][0]==0x30&&functions[2]==0x467adcu);
    CHECK(args[2][0]==P+0x294&&args[2][1]==0x40&&args[2][2]==0x55&&entry[2][0]==0x55&&entry[2][1]==P+0x294&&entry[2][2]==args[1][0]);
    CHECK((uint16_t)args[3][0]==0x18&&entry[3][0]==0x18&&entry[3][1]==0x20&&r16(0,P+0x298)==0x1000);
    /* Lower clamp: -1000h. */
    w16(0,P+0x294,0x10);w16(0,P+0x298,0x200);reset();results[0]=0;results[1]=0x9000;
    es2_torso_twist_registers(&memory,&g,P,0,0,0,regs);CHECK(r16(0,P+0x298)==0xf000);
    /* Snap: 200h -> 210h crosses 205h; track and latch calls follow. */
    w16(0,P+0x294,0x100);w16(0,P+0x298,0x200);w32(0,P+0x230,T);arena[P+0xa3-BASE]=1;
    reset();results[0]=0x40;results[1]=0x10;results[3]=0x77;
    p=es2_torso_twist_registers(&memory,&g,P,0xffff0040u,0x205,1,regs);
    CHECK(r16(0,P+0x298)==0x205&&r16(0,P+0x294)==0&&calls==4);
    CHECK(functions[2]==0x479344u&&args[2][0]==T&&(uint16_t)args[2][1]==7&&args[2][2]==0x205u>>2&&entry[2][1]==0x205u>>2);
    CHECK(args[2][1]==7&&functions[3]==0x41a6d0u&&args[3][0]==P&&args[3][1]==0x5eed0040u&&entry[3][0]==P+0x92);
    CHECK(p==PAIR(0x77,0x5eed0003u));
    /* No snap when the step ends below the target. */
    w16(0,P+0x294,0x100);w16(0,P+0x298,0x200);w32(0,P+0x230,0);arena[P+0xa3-BASE]=0;
    reset();results[0]=0x40;results[1]=0x10;es2_torso_twist_registers(&memory,&g,P,0x40,0x300,1,regs);
    CHECK(r16(0,P+0x298)==0x210&&r16(0,P+0x294)==0x40);
    /* Pitch: independent bounds C+28h / C+2Ah; then 41a74c(P, d) with EAX = d. */
    w16(0,C+0x24,0x100);w16(0,C+0x26,0x30);w16(0,C+0x28,0x800);w16(0,C+0x2a,0xff00);w16(0,C+0x22,9);
    w16(0,P+0x296,0x100);w16(0,P+0x29a,0);
    reset();results[0]=0x40;results[1]=0xfe00;
    p=es2_torso_pitch_registers(&memory,&g,P,0x40,0x4000,0,0,regs);
    CHECK(r16(0,P+0x29a)==0xff00&&calls==3&&functions[2]==0x41a74cu&&args[2][0]==P&&args[2][1]==0x4000&&entry[2][0]==0x4000);
    CHECK(p==PAIR(P+0x92,0x5eed0002u));
    /* Convergence: two slots, one null; A = (1, 2, 3 - 1), B = (0, d, 0). */
    g.scratch=OBJ+0xc00;
    w32(0,P+0x202,W);w16(0,W+8,2);w32(0,W+4,list);w32(0,list,S);w32(0,list+4,0);w16(0,C+0x64,1);
    w16(0,V,1);w16(0,V+2,2);w16(0,V+4,3);out_words[0]=0x1111;out_words[1]=0x2222;out_words[2]=0x3333;
    reset();results[0]=V;
    p=es2_weapon_converge_registers(&memory,&g,P,0x5000,regs);
    const uint32_t f=g.scratch+ES2_WEAPON_CONVERGE_SCRATCH;
    CHECK(calls==2&&functions[0]==0x40f540u&&args[0][0]==S&&entry[0][0]==0x5000&&entry[0][1]==0&&entry[0][2]==0);
    CHECK(functions[1]==0x492990u&&args[1][0]==f-0xc&&args[1][1]==f-0x18&&args[1][2]==f-0x24);
    CHECK(entry[1][0]==f-0x18&&entry[1][1]==f-0xc&&entry[1][2]==f-0x24);
    CHECK(r32(0,f-0x18)==0&&r32(0,f-0x14)==0x5000&&r32(0,f-0x10)==0&&r32(0,f-0x24)==1&&r32(0,f-0x20)==2&&r32(0,f-0x1c)==2);
    CHECK(r32(0,S+0x24)==0x22221111u&&r16(0,S+0x28)==0x3333&&p==PAIR(1,W));
    /* d = 0: zero angles, no calls. */
    reset();p=es2_weapon_converge_registers(&memory,&g,P,0,regs);
    CHECK(calls==0&&r32(0,S+0x24)==0&&r16(0,S+0x28)==0&&p==PAIR(1,W));
    /* An empty list returns C : W. */
    w16(0,W+8,0);reset();p=es2_weapon_converge_registers(&memory,&g,P,0x5000,regs);CHECK(calls==0&&p==PAIR(C,W));
    /* Weapon keys: unknown code, W, Alt+W, B wrap, Alt+3, = with and without selection. */
    const uint32_t esi=0xabcd9999u,handled=0xabcd0001u;
    reset();CHECK(es2_weapon_keys_registers(&memory,&g,W,0,esi,regs)==PAIR(0,0xffffffd0u)&&calls==0);
    reset();CHECK(es2_weapon_keys_registers(&memory,&g,W,0x7fff,esi,regs)==PAIR(0,0x7fffu-0x211u)&&calls==0);
    arena[W+0x18-BASE]=9;reset();p=es2_weapon_keys_registers(&memory,&g,W,0x11,esi,regs);
    CHECK(calls==1&&functions[0]==0x41074cu&&args[0][0]==W&&args[0][1]==1&&entry[0][0]==0x11&&entry[0][1]==0&&entry[0][2]==regs[2]);
    CHECK(arena[W+0x18-BASE]==0&&p==PAIR(handled,0x5eed0000u));
    reset();es2_weapon_keys_registers(&memory,&g,W,0x211,esi,regs);CHECK(calls==1&&args[0][1]==0xffffffffu&&entry[0][0]==0x211);
    arena[W+0x1c-BASE]=2;reset();es2_weapon_keys_registers(&memory,&g,W,0x30,esi,regs);
    CHECK(arena[W+0x1c-BASE]==0&&arena[W+0x16-BASE]==1&&functions[0]==0x410ae4u&&args[0][1]==0&&entry[0][2]==3);
    reset();es2_weapon_keys_registers(&memory,&g,W,0x2c,esi,regs);CHECK(calls==1&&functions[0]==0x410d04u&&args[0][0]==W);
    reset();es2_weapon_keys_registers(&memory,&g,W,0x55550205u,esi,regs);
    CHECK(functions[0]==0x4110acu&&args[0][1]==0x55550003u&&entry[0][1]==0x205u-0x20cu);
    arena[W+0x1d-BASE]=0;w32(0,list,S);w32(0,S,vt);reset();p=es2_weapon_keys_registers(&memory,&g,W,0x0d,esi,regs);
    CHECK(calls==1&&functions[0]==0x7738&&args[0][0]==S&&args[0][1]==1&&entry[0][0]==vt&&entry[0][1]==list&&entry[0][2]==S);
    CHECK(p==PAIR(handled,0x5eed0000u));
    reset();es2_weapon_keys_registers(&memory,&g,W,0x4a,esi,regs);CHECK(calls==1&&args[0][1]==0);
    arena[W+0x1d-BASE]=0xff;reset();p=es2_weapon_keys_registers(&memory,&g,W,0x4e,esi,regs);
    CHECK(calls==0&&p==PAIR(handled,0xff));
    printf("PASS: torso core %u checks\n",checks);
    return 0;
}
