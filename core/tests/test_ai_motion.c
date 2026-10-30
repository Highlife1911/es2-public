#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/ai_motion.h"
/* Synthetic checks of the core (AI flyer move step 4218c4) on an own
 * arena with scripted callees: call order, arguments and entry registers,
 * the probe reactions, the ground clearance and the fly-by latch. Values
 * are hand-derived; the in-vitro harness compares against the original. */
#define BASE UINT32_C(0x90000000)
#define IMAGE UINT32_C(0x00400000)
static uint8_t arena[8192];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"ai_motion line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t image_bytes[0x2d8000];
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena)||a-IMAGE<0x2d8000u);
    return a-BASE<sizeof(arena)?arena+(a-BASE):image_bytes+(a-IMAGE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
#define SCRATCH (BASE+0x1800u)
#define VPT (SCRATCH+0x80u-0x2cu)
#define QPT (SCRATCH+0x80u-0x38u)
static unsigned calls;static uint32_t functions[32],args[32][3],points[32][3];static es2_weapon_registers entries[32];
static int32_t ground[5];static uint32_t distance,bearing;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<32);functions[calls]=f;entries[calls]=*r;for(unsigned i=0;i<n&&i<3;++i)args[calls][i]=a[i];
    unsigned terrain=0;for(unsigned i=0;i<calls;++i)terrain+=functions[i]==0x46e180u;
    switch(f){
    case 0x47ebb8:CHECK(n==2);break;
    case 0x48043c:CHECK(n==3&&a[0]==VPT&&a[2]==QPT);
        for(unsigned j=0;j<3;++j)points[calls][j]=r32(0,a[0]+4*j);
        w32(0,a[2],0x100);w32(0,a[2]+4,0x200);w32(0,a[2]+8,1000);r->edx=0xaaaa0000u;r->ecx=0xcccc0000u;break;
    case 0x46e180:CHECK(n==2&&a[1]==QPT&&terrain<5);for(unsigned j=0;j<3;++j)points[calls][j]=r32(0,a[1]+4*j);
        r->eax=(uint32_t)ground[terrain];r->edx=0x12340000u;r->ecx=0x56780000u;break;
    case 0x47e0b0:CHECK(n==2);r->eax=0xffff0000u|(uint16_t)(((int32_t)a[0]*(int32_t)a[1])>>10);r->edx=0xdddd0000u;break;
    case 0x47de72:CHECK(n==3);r->eax=distance;r->edx=0x77770000u;break;
    case 0x462830:CHECK(n==1&&a[0]==0x31);r->eax=0x51500000u;r->edx=0x52520000u;r->ecx=0;break;
    case 0x7078:CHECK(n==2);break;                                         /* virtual +78h */
    case 0x4928d0:CHECK(n==2);r->eax=distance;r->ecx=0x0c0c0000u;break;
    case 0x492934:CHECK(n==2);r->eax=bearing;r->edx=0x99990000u;break;
    case 0x416a04:CHECK(n==4&&a[3]==0);r->eax=0x1111u;r->edx=0x2222u;break;
    default:fprintf(stderr,"unexpected callee %08x\n",(unsigned)f);CHECK(0);
    }
    ++calls;
}
int main(void){
    const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u},X=BASE+0x400,S=X+0x243,CAM=BASE+0x1000;
    es2_weapon_graph g={IMAGE,SCRATCH,0,0,0,call,NULL,NULL,NULL};
    w32(0,IMAGE+0xa0be0u,BASE+0x1400u);w32(0,IMAGE+0xd24eau,CAM);
    /* Case 1: cache word 0, every probe acts, the flyer rises to the
     * clearance, moves and is near the camera with the latch clear. */
    w16(0,X+0x32,0);w32(0,X+0x26,10);w32(0,X+0x2a,20);w32(0,X+0x2e,30);
    w32(0,S+0xc,1);w32(0,S+0x10,2);w32(0,S+0x14,3);w32(0,S+4,5);
    w16(0,S+0x18,0xfff0);w16(0,S+0x1a,0x10);w16(0,X+0xc,0x100);w16(0,X+0xe,0x200);w16(0,X+6,0);
    w32(0,CAM+4,0);w32(0,CAM+8,0);w32(0,CAM+0xc,0);
    ground[0]=1000+1024;ground[1]=1000+2048;ground[2]=1000+1024;ground[3]=1000+1024;ground[4]=200;distance=29999;
    calls=0;uint64_t p=es2_flyer_move_registers(&memory,&g,X,entry);
    static const uint32_t order1[]={0x47ebb8,0x48043c,0x46e180,0x47e0b0,0x48043c,0x46e180,0x47e0b0,0x48043c,0x46e180,0x47e0b0,
        0x48043c,0x46e180,0x47e0b0,0x46e180,0x47de72,0x462830};
    CHECK(calls==16);for(unsigned i=0;i<16;++i)CHECK(functions[i]==order1[i]);
    CHECK(args[0][0]==X+0xc&&args[0][1]==X+0x12&&entries[0].eax==X+0x12&&entries[0].edx==entry[1]&&entries[0].ecx==entry[2]);
    CHECK(r16(0,X+0x32)==0);
    /* Velocity added before the first probe; probe 1 entered with EAX = V, ECX = S+0Ch. */
    CHECK(entries[1].eax==VPT&&entries[1].edx==QPT&&entries[1].ecx==1);
    CHECK(points[1][0]==1000&&points[1][1]==(uint32_t)-420&&points[1][2]==(uint32_t)-300);
    CHECK(entries[2].eax==BASE+0x1400u&&entries[2].ecx==QPT&&args[2][0]==BASE+0x1400u);
    CHECK(args[3][0]==0xfa0&&args[3][1]==1024&&entries[3].eax==1024);
    CHECK(points[4][0]==(uint32_t)-1000&&entries[4].eax==X+0x12&&entries[4].ecx==VPT);
    CHECK(entries[5].eax==QPT&&entries[5].edx==BASE+0x1400u);
    CHECK(args[6][1]==2048);
    CHECK(points[7][0]==0&&points[7][1]==1000&&points[7][2]==0&&args[9][0]==0x7d0);
    CHECK(points[10][1]==15000&&points[10][2]==(uint32_t)-1500&&args[12][0]==0x14);
    /* Roll rate: 0x10 - 4000 + 8000 (Q10 of 4000*1024 and 4000*2048). */
    CHECK(r16(0,S+0x1a)==(uint16_t)(0x10-4000+8000));
    CHECK(r16(0,X+0xe)==(uint16_t)(0x200+(uint16_t)(0x10-4000)+(uint16_t)(0x10-4000+8000)));
    /* Pitch rate: negative rate cleared, then +2000 and +20. */
    CHECK(r16(0,S+0x18)==2020&&r16(0,X+0xc)==(uint16_t)(0x100+2000+2020));
    /* Clearance: Q from P (13, 22, 32 after the velocity); 200 + 500 > 32. */
    CHECK(entries[13].eax==22&&entries[13].edx==QPT&&entries[13].ecx==BASE+0x1400u);
    CHECK(points[13][0]==11&&points[13][1]==22&&points[13][2]==33);
    CHECK(r32(0,X+0x26)==11&&r32(0,X+0x2a)==22&&r32(0,X+0x2e)==700);
    CHECK(args[14][0]==11&&args[14][1]==22&&args[14][2]==700&&entries[14].eax==0&&entries[14].edx==11&&entries[14].ecx==11);
    CHECK(entries[15].eax==29999&&r16(0,X+6)==1);
    CHECK(p==((uint64_t)0x52520000u<<32|0x51500001u));
    /* Case 2: cache valid, no probe acts, no clearance, latched: no sound;
     * then out of range clears the latch; then speed 0 skips the distance. */
    for(unsigned i=0;i<5;++i)ground[i]=-5000;
    w16(0,X+0x32,1);
    calls=0;p=es2_flyer_move_registers(&memory,&g,X,entry);
    CHECK(calls==10&&functions[0]==0x48043c&&functions[9]==0x47de72&&r16(0,X+6)==1);
    CHECK(entries[0].ecx==1&&p==((uint64_t)0x77770000u<<32|(29999u&0xffff0000u)|1u));
    distance=30000;calls=0;es2_flyer_move_registers(&memory,&g,X,entry);CHECK(calls==10&&r16(0,X+6)==0);
    w32(0,S+4,0);calls=0;p=es2_flyer_move_registers(&memory,&g,X,entry);
    CHECK(calls==9&&p==((uint64_t)0x12340000u<<32|(uint32_t)(-5000&0xffff0000)|1u));
    printf("ai_motion: %u checks passed\n",checks);
    return 0;
}
