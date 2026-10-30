#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/mech_travel.h"
/* Synthetic guest: objects at 90000000h, frame scratch at A0000000h, image
 * base B with a window 499000h..4d9000h for the globals. The callees are
 * logged with their slots, entry registers and the frame the core names,
 * and return planned values. */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
#define WIN 0x499000u
static uint8_t arena[16384],scratch[0x40],image[0x40000];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mech_travel line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(WIN)<sizeof(image))return image+(a-G(WIN));
    fprintf(stderr,"mech_travel: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
typedef struct logged { uint32_t f,a[6],r[3]; unsigned n,slot; es2_ai_call_frame fr; } logged;
static logged log_[64];static unsigned calls,observed;
static uint32_t ret[64][3];
static es2_ai_call_frame frame_;
/* A callee that changes memory: index of the call and an action. */
static unsigned poke_at=99;static uint32_t poke_addr,poke_value;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->fr=frame_;l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(calls==poke_at)w32(0,poke_addr,poke_value);
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);log_[observed++].slot=slot;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,FRAME,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){memset(&frame_,0,sizeof(frame_));calls=observed=0;poke_at=99;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));
    memset(arena,0,sizeof(arena));memset(scratch,0,sizeof(scratch));memset(image,0,sizeof(image));}
#define A (OBJ+0x100)
#define T (OBJ+0x600)
#define L (OBJ+0xb00)
#define GRP (OBJ+0x1000)
#define VT (OBJ+0x1100)
#define CFG (OBJ+0x1180)
#define LIST (OBJ+0x1200)
#define PARTS (OBJ+0x1300)
static void group(uint32_t leader){w32(0,A+0x45,GRP);w32(0,GRP+0xc,LIST);w32(0,LIST,leader);}

static void test_fire_permit(void){
    reset();group(L);
    /* Leader without byte +0A3h: A+96h follows A+97h. */
    w8(A+0x97,3);uint64_t r=es2_fire_permit_registers(&memory,A);
    CHECK(r8(A+0x96)==1&&r==((uint64_t)(A+0x92)<<32|(A+0x92)));
    w8(A+0x97,0);w8(A+0x96,7);es2_fire_permit_registers(&memory,A);CHECK(r8(A+0x96)==0);
    /* Leader with byte +0A3h: A+96h follows A+0B2h. */
    w8(L+0xa3,1);w8(A+0x97,1);w8(A+0xb2,0);es2_fire_permit_registers(&memory,A);CHECK(r8(A+0x96)==0);
    w8(A+0xb2,9);es2_fire_permit_registers(&memory,A);CHECK(r8(A+0x96)==1);
    /* A is its own leader. */
    group(A);w8(A+0xa3,0);w8(A+0x97,0);es2_fire_permit_registers(&memory,A);CHECK(r8(A+0x96)==0);
}

static void test_dead_sleep(void){
    es2_weapon_graph g=graph();reset();ret[0][1]=0x77;
    uint64_t r=es2_dead_step_registers(&memory,&g,A,entry);
    CHECK(calls==1&&log_[0].f==G(0x416a04)&&log_[0].n==4&&log_[0].a[0]==A&&log_[0].a[1]==0&&log_[0].a[3]==0);
    CHECK(log_[0].r[0]==A&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]&&r==(uint64_t)0x77<<32);
    /* Sleeping with a target: dropped (count -1, byte A+9Dh), then
     * vehicle_control with EAX = A+92h, EDX = 0, ECX = T and fire_permit. */
    reset();w32(0,A+0x1a4,T);w16(0,T+0x1a2,2);ret[0][0]=5;ret[0][1]=6;ret[0][2]=7;ret[1][1]=0x99;
    r=es2_sleep_step_registers(&memory,&g,A,entry);
    CHECK(r32(0,A+0x1a4)==0&&r16(0,T+0x1a2)==1&&r8(A+0x9d)==1&&calls==2);
    CHECK(log_[0].f==G(0x416a04)&&log_[0].r[0]==A+0x92&&log_[0].r[1]==0&&log_[0].r[2]==T);
    CHECK(log_[1].f==G(0x41c3c8)&&log_[1].a[0]==A&&log_[1].r[0]==5&&log_[1].r[1]==6&&log_[1].r[2]==7&&r==(uint64_t)0x99<<32);
    /* Without a target the entry EAX and ECX reach vehicle_control. */
    reset();r=es2_sleep_step_registers(&memory,&g,A,entry);
    CHECK(r8(A+0x9d)==0&&log_[0].r[0]==entry[0]&&log_[0].r[1]==0&&log_[0].r[2]==entry[2]);
}

static void test_point_move(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t P=OBJ+0x2000;
    /* heading 0x1234 - word A+10h 0x0034 = 0x1200 >> 6 = 0x48; speed AAh; far. */
    w16(0,A+0x10,0x34);ret[0][0]=0xabcd1234u;ret[0][2]=0x31;ret[1][0]=20000;ret[1][1]=0x41;ret[1][2]=0x42;ret[2][1]=0x55;
    uint64_t r=es2_point_move_registers(&memory,&g,A,P,entry);
    CHECK(calls==3&&log_[0].f==G(0x492934)&&log_[0].a[0]==P&&log_[0].a[1]==A+0x26&&log_[0].r[0]==A+0x26&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x4928d0)&&log_[1].a[0]==A+0x26&&log_[1].a[1]==P&&log_[1].r[0]==0xabcd1200u&&log_[1].r[1]==P&&log_[1].r[2]==A+0x26);
    CHECK(log_[2].f==G(0x416a04)&&log_[2].a[1]==0x48&&log_[2].a[2]==0xaa&&log_[2].a[3]==0&&log_[2].r[0]==0xaa&&log_[2].r[1]==0x41&&log_[2].r[2]==0x42);
    CHECK(r==(uint64_t)0x55<<32);
    /* Negative turn keeps the high word FFFFh; speed word A+252h; near. */
    reset();w16(0,A+0x252,0x123);ret[0][0]=0x8000;ret[1][0]=9999;
    r=es2_point_move_registers(&memory,&g,A,P,entry);
    CHECK(log_[2].a[1]==0xfffffe00u&&log_[2].a[2]==0xffff0123u&&r==1);
    /* Order state and beyond 30000: the sound with [A+317h], then 100h. */
    reset();w16(0,A+0x23e,1);w32(0,A+0x317,T);ret[1][0]=30001;ret[1][1]=0x61;ret[1][2]=0x62;ret[2][0]=0x77770000u;ret[2][1]=0x63;
    r=es2_point_move_registers(&memory,&g,A,P,entry);
    CHECK(calls==4&&log_[2].f==G(0x40f09c)&&log_[2].a[0]==T&&log_[2].r[0]==T&&log_[2].r[1]==0x61&&log_[2].r[2]==0x62);
    CHECK(log_[3].a[2]==0x77770100u&&log_[3].r[0]==0x77770100u&&log_[3].r[1]==0x63&&log_[3].r[2]==T&&r==0);
    /* Without a sound record: 100h over 0. */
    reset();w16(0,A+0x23e,1);ret[1][0]=30001;
    es2_point_move_registers(&memory,&g,A,P,entry);CHECK(calls==3&&log_[2].a[2]==0x100);
    /* Exactly 30000 is not beyond. */
    reset();w16(0,A+0x23e,1);w32(0,A+0x317,T);ret[1][0]=30000;
    es2_point_move_registers(&memory,&g,A,P,entry);CHECK(calls==3&&log_[2].a[2]==0xaa);
}

static void test_leader_route(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t H=GRP+4,P=OBJ+0x2000;
    w32(0,A+0x45,GRP);w16(0,H,0xffff);ret[0][0]=0;ret[0][1]=0x21;ret[0][2]=0x22;ret[1][0]=0x31;ret[1][1]=0x32;
    uint64_t r=es2_leader_route_registers(&memory,&g,A,entry);
    /* The index increments the entry EAX with the word as its low half. */
    CHECK(calls==2&&log_[0].f==G(0x423b0c)&&log_[0].a[0]==H&&log_[0].a[1]==0x11120000u&&log_[0].r[0]==0x11120000u);
    CHECK(log_[1].f==G(0x416a04)&&log_[1].r[0]==0&&log_[1].r[1]==0x21&&log_[1].r[2]==0x22&&r==((uint64_t)0x32<<32|0x31));
    reset();w32(0,A+0x45,GRP);w16(0,H,4);ret[0][0]=P;ret[0][1]=0x21;ret[1][0]=0x12340000u;ret[1][1]=0x44;
    r=es2_leader_route_registers(&memory,&g,A,entry);
    CHECK(calls==2&&log_[0].a[1]==0x11110005u&&log_[1].f==G(0x41fac4)&&log_[1].a[1]==P&&log_[1].r[0]==P&&log_[1].r[1]==0x21);
    CHECK(r==((uint64_t)0x44<<32|0x12340000u));
    /* Reached: the route advance on [A+45h]+4 read again. */
    reset();w32(0,A+0x45,GRP);ret[0][0]=P;ret[1][0]=1;ret[1][1]=0x44;ret[1][2]=0x45;ret[2][0]=0x51;ret[2][1]=0x52;
    poke_at=1;poke_addr=A+0x45;poke_value=GRP+0x40;
    r=es2_leader_route_registers(&memory,&g,A,entry);
    CHECK(calls==3&&log_[2].f==G(0x42313c)&&log_[2].a[0]==GRP+0x44&&log_[2].r[0]==GRP+0x44&&log_[2].r[1]==0x44&&log_[2].r[2]==0x45);
    CHECK(r==((uint64_t)0x52<<32|0x51));
}

static void test_ram_move(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t Q=FRAME,map=OBJ+0x3000;
    w32(0,G(0x4a0be0),map);w32(0,A,VT);w32(0,VT+0x74,0x7400);w32(0,A+0x1f2,CFG);w16(0,CFG+0x16,0xfff6);
    w32(0,A+0x26,100);w32(0,A+0x2a,200);w32(0,A+0x2e,300);w32(0,A+0x20e,PARTS);
    ret[0][0]=0x10;ret[1][0]=5000;ret[2][0]=0x20;ret[3][0]=0xabcd0000u;ret[3][1]=0x33;
    uint64_t r=es2_ram_move_registers(&memory,&g,A,entry);
    CHECK(log_[0].f==G(0x418f40)&&log_[0].r[0]==entry[0]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x46e180)&&log_[1].a[0]==map&&log_[1].a[1]==Q&&log_[1].r[0]==300&&log_[1].r[1]==Q&&log_[1].r[2]==map);
    CHECK(r32(0,A+0x2e)==4990&&r32(0,A+0x26)==100&&r32(0,A+0x2a)==200&&r32(0,Q+8)==4990);
    CHECK(log_[2].f==G(0x4195c8)&&log_[2].r[0]==A+0x26&&log_[2].r[1]==200&&log_[2].r[2]==100);
    CHECK(log_[3].f==G(0x418f74)&&log_[3].r[2]==A&&calls==4&&r==((uint64_t)0x33<<32|((A+0x92)&0xffff0000u)|1));
    /* Collision: radius damage, then +74h for the parts with a word. */
    reset();w32(0,A,VT);w32(0,VT+0x74,0x7400);w32(0,A+0x1f2,CFG);w32(0,A+0x20e,PARTS);
    w16(0,PARTS+2,1);w16(0,PARTS+2*28,1);ret[3][0]=1;ret[3][1]=0x41;ret[4][0]=0x51;ret[4][1]=0x52;ret[4][2]=0x53;
    ret[5][0]=0x61;ret[5][1]=0x62;ret[5][2]=0x63;ret[6][0]=0x99990000u;ret[6][1]=0x72;
    r=es2_ram_move_registers(&memory,&g,A,entry);
    CHECK(calls==7&&log_[4].f==G(0x426a20)&&log_[4].a[0]==A+0x26&&log_[4].a[1]==3000&&log_[4].a[2]==2000&&log_[4].a[3]==0&&log_[4].a[4]==A);
    CHECK(log_[4].r[0]==1&&log_[4].r[1]==0x41&&log_[4].r[2]==A+0x26);
    CHECK(log_[5].f==0x7400&&log_[5].slot==0x74&&log_[5].a[1]==1&&log_[5].a[2]==0x7d00&&log_[5].a[3]==A&&log_[5].r[0]==VT&&log_[5].r[1]==0x52&&log_[5].r[2]==0x53);
    CHECK(log_[6].a[1]==28&&log_[6].r[1]==0x62&&r==((uint64_t)0x72<<32|0x99990001u));
    /* No collision but byte A+0B1h: the hit path with EAX = A+92h. */
    reset();w32(0,A+0x1f2,CFG);w32(0,A+0x20e,PARTS);w8(A+0xb1,1);
    es2_ram_move_registers(&memory,&g,A,entry);CHECK(calls==5&&log_[4].r[0]==A+0x92);
}

static void test_ram_step(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t BB=A+0x5a;
    /* Timer expired, new target T over the old one L, no target after it?
     * The scan sets T; resting (B+5 = 0), rest timer runs. */
    w32(0,A+0x1a4,L);w16(0,L+0x1a2,1);w16(0,A+0x10,0x100);
    ret[0][0]=0;ret[0][1]=0x21;ret[1][0]=T;ret[1][1]=0x31;ret[2][0]=0x5500;ret[2][1]=0x41;ret[2][2]=0x42;ret[3][0]=0x51;ret[3][1]=0x52;
    ret[4][0]=3;ret[4][1]=0x61;
    uint64_t r=es2_ram_step_registers(&memory,&g,A,entry);
    CHECK(log_[0].f==G(0x467aa8)&&log_[0].a[0]==BB&&log_[0].r[0]==entry[0]);
    CHECK(log_[1].f==G(0x411fa0)&&log_[1].a[1]==6&&log_[1].r[0]==1&&log_[1].r[1]==0x21&&log_[1].r[2]==BB);
    CHECK(r32(0,A+0x1a4)==T&&r16(0,L+0x1a2)==0&&r16(0,T+0x1a2)==1&&r8(A+0x9d)==1&&r32(0,A+0x5b)==10000);
    CHECK(log_[2].f==G(0x492934)&&log_[2].a[0]==T+0x26&&log_[2].a[1]==A+0x26&&log_[2].r[0]==A+0x26&&log_[2].r[1]==T+0x26&&log_[2].r[2]==T);
    CHECK(log_[3].f==G(0x416a04)&&log_[3].a[1]==0x54&&log_[3].a[2]==0x100&&log_[3].a[3]==1&&log_[3].r[0]==0x5400&&log_[3].r[2]==0x54);
    CHECK(log_[4].f==G(0x467a48)&&log_[4].a[0]==BB+7&&log_[4].r[0]==0x51&&log_[4].r[2]==BB+7&&calls==5&&r==(uint64_t)0x61<<32);
    /* Rest timer expired: the next charge after 3000 + rng(1500). */
    reset();w32(0,A+0x1a4,T);ret[0][0]=1;ret[3][0]=0x770000u;ret[3][1]=0x66;ret[4][0]=0x12340010u;ret[4][1]=0x71;
    r=es2_ram_step_registers(&memory,&g,A,entry);
    CHECK(calls==5&&log_[3].f==G(0x467a48)&&log_[4].f==G(0x492f24)&&log_[4].a[0]==G(0x4d2599)&&log_[4].a[1]==0x5dc);
    CHECK(log_[4].r[0]==1&&log_[4].r[1]==0x66&&r16(0,BB+5)==1&&r16(0,BB+8)==0xbc8&&r==(uint64_t)0x71<<32);
    /* Charging: two ram_move steps, three steering steps, then the charge
     * timer; expired: rest 8000 + rng(4000). */
    reset();w32(0,A+0x1a4,T);w16(0,BB+5,1);ret[0][0]=1;
    for(unsigned k=0;k<9;++k){ret[k][0]=0x1000*k+1;ret[k][1]=0x100+k;ret[k][2]=0x200+k;}
    ret[0][0]=1;ret[9][0]=0x20000;ret[10][0]=0x30;ret[10][1]=0x91;
    r=es2_ram_step_registers(&memory,&g,A,entry);
    CHECK(calls==11&&log_[3].f==G(0x41e488)&&log_[3].a[0]==A&&log_[4].r[2]==A&&log_[6].f==G(0x41e488)&&log_[8].f==G(0x416a04));
    CHECK(log_[9].f==G(0x467a48)&&log_[9].r[0]==BB+7&&log_[10].f==G(0x492f24)&&log_[10].a[1]==0xfa0&&log_[10].r[0]==0x20000&&log_[10].r[1]==1);
    CHECK(r16(0,BB+5)==0&&r16(0,BB+8)==0x1f70&&r==(uint64_t)0x91<<32);
    /* Charging, the target lost during ram_move. */
    reset();w32(0,A+0x1a4,T);w16(0,BB+5,1);ret[0][0]=1;ret[3][1]=0x55;poke_at=3;poke_addr=A+0x1a4;poke_value=0;
    r=es2_ram_step_registers(&memory,&g,A,entry);CHECK(calls==4&&r==(uint64_t)0x55<<32);
    /* Charge timer running: EDX = 0. */
    reset();w32(0,A+0x1a4,T);w16(0,BB+5,1);ret[0][0]=1;ret[9][0]=1;ret[9][1]=0x77;
    r=es2_ram_step_registers(&memory,&g,A,entry);CHECK(calls==10&&r==0);
    /* No target: vehicle_control(A, 0, 0, 1). */
    reset();ret[0][0]=0;ret[1][0]=0;ret[2][1]=0x88;
    r=es2_ram_step_registers(&memory,&g,A,entry);
    CHECK(calls==3&&log_[2].a[3]==1&&log_[2].a[1]==0&&log_[2].r[0]==0&&r==(uint64_t)0x88<<32&&r32(0,A+0x5b)==10000);
}

static void test_travel_follow(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t BB=A+0x5a,V=T;
    /* Travel: leader_route, drop the target, timer expired, a new
     * remembered target V, aim at it. */
    w32(0,A+0x1a4,L);w16(0,L+0x1a2,1);w32(0,V,VT);w32(0,VT+0x3c,0x3c00);w16(0,V+0x10,0x10);
    ret[0][0]=0x41;ret[1][0]=0;ret[1][1]=0x51;ret[2][0]=V;ret[2][1]=0x61;ret[2][2]=0x62;
    ret[3][0]=0x12340100u;ret[3][2]=0x73;ret[4][0]=0x0005;ret[4][1]=0x83;ret[5][1]=0x93;
    uint64_t r=es2_travel_step_registers(&memory,&g,&frame_,0xed1,A,entry);
    CHECK(calls==6&&log_[0].f==G(0x41fb60)&&log_[0].r[0]==entry[0]&&log_[0].r[2]==entry[2]);
    CHECK(log_[0].fr.ebx==A&&log_[0].fr.esi==BB&&log_[0].fr.edi==0xed1&&log_[0].fr.ret==G(0x41d9de));
    CHECK(r32(0,A+0x1a4)==0&&r16(0,L+0x1a2)==0&&r8(A+0x9d)==1);
    CHECK(log_[1].f==G(0x467aa8)&&log_[1].a[0]==BB&&log_[1].r[0]==A+0x92&&log_[1].r[1]==0&&log_[1].r[2]==L&&log_[1].fr.ret==G(0x41da23));
    CHECK(log_[2].f==G(0x411fa0)&&log_[2].r[0]==1&&log_[2].r[1]==0x51&&log_[2].r[2]==BB&&log_[2].fr.ret==G(0x41da3b));
    CHECK(r32(0,BB+5)==V&&r32(0,BB+1)==10000&&r8(A+0x96)==1);
    CHECK(log_[3].f==G(0x492934)&&log_[3].n==3&&log_[3].a[0]==A+0x26&&log_[3].a[1]==V+0x26&&log_[3].a[2]==V&&log_[3].r[0]==V+0x26&&log_[3].r[1]==0x61);
    CHECK(log_[3].fr.esi==V&&log_[3].fr.edi==A+0x26&&log_[3].fr.ret==G(0x41da85));
    CHECK(log_[4].f==0x3c00&&log_[4].slot==0x3c&&log_[4].a[0]==V&&log_[4].a[1]==0x123400f0u&&log_[4].r[0]==0x123400f0u&&log_[4].r[1]==VT&&log_[4].r[2]==0x73);
    CHECK(log_[5].f==G(0x41ea7c)&&log_[5].a[0]==A&&log_[5].a[1]==0x123400f5u&&log_[5].a[2]==V&&log_[5].r[0]==5&&log_[5].r[1]==0x83);
    CHECK(log_[5].fr.ret==G(0x41da9f)&&r==(uint64_t)0x93<<32);
    /* Timer running, no remembered target: EDX = the timer's. */
    reset();ret[1][0]=7;ret[1][1]=0x57;
    r=es2_travel_step_registers(&memory,&g,NULL,0,A,entry);CHECK(calls==2&&log_[1].r[2]==A&&r==(uint64_t)0x57<<32);
    /* Expired without a target: A+96h from A+97h. */
    reset();w8(A+0x97,1);ret[2][1]=0x59;r=es2_travel_step_registers(&memory,&g,NULL,0,A,entry);
    CHECK(calls==3&&r8(A+0x96)==1&&r==(uint64_t)0x59<<32);
    reset();w8(A+0x96,1);es2_travel_step_registers(&memory,&g,NULL,0,A,entry);CHECK(r8(A+0x96)==0);
    /* Follow: far from the order actor P, point_move to P+26h. */
    reset();const uint32_t P=L;w32(0,A+0x45,GRP);
    ret[0][0]=P;ret[0][1]=0x21;ret[1][0]=25000;ret[1][1]=0x31;ret[1][2]=0x32;ret[2][1]=0x41;ret[3][0]=5;ret[3][1]=0x55;
    r=es2_follow_step_registers(&memory,&g,&frame_,0xed1,A,entry);
    CHECK(calls==4&&log_[0].f==G(0x4238a0)&&log_[0].a[0]==GRP&&log_[0].r[0]==GRP&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[0].fr.edi==0xed1&&log_[0].fr.ret==G(0x41dac2)&&r32(0,FRAME)==P);
    CHECK(log_[1].f==G(0x49288c)&&log_[1].a[0]==A+0x26&&log_[1].a[1]==P+0x26&&log_[1].r[0]==P+0x26&&log_[1].r[1]==A+0x26&&log_[1].r[2]==GRP);
    CHECK(log_[1].fr.edi==P&&log_[2].f==G(0x41fac4)&&log_[2].a[1]==P+0x26&&log_[2].r[0]==25000&&log_[2].r[1]==0x31&&log_[2].r[2]==0x32);
    CHECK(log_[2].fr.edi==P+0x26&&log_[2].fr.ret==G(0x41dafd)&&log_[3].f==G(0x467aa8)&&log_[3].fr.edi==P+0x26&&log_[3].r[1]==0);
    CHECK(log_[3].fr.ret==G(0x41db44)&&r==(uint64_t)0x55<<32);
    /* Near: stand. */
    reset();w32(0,A+0x45,GRP);ret[0][0]=P;ret[1][0]=24999;ret[3][0]=1;
    es2_follow_step_registers(&memory,&g,&frame_,0xed1,A,entry);
    CHECK(calls==4&&log_[2].f==G(0x416a04)&&log_[2].r[0]==24999&&log_[2].fr.edi==P&&log_[2].fr.ret==G(0x41daee)&&log_[3].fr.edi==P);
}

/* turbo_engage: latch and sound, the latch already set, a charge of
 * exactly 600, no sound flag. */
static void test_turbo_engage(void){
    es2_weapon_graph g=graph();
    reset();w32(0,T+0x7d,601);w32(0,T+0x79,1);ret[0][0]=0x77;ret[0][1]=0x88;
    uint64_t p=es2_turbo_engage_registers(&memory,&g,T,entry);
    CHECK(calls==1&&log_[0].f==G(0x462830)&&log_[0].a[0]==0x2c&&log_[0].r[0]==T&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(r32(0,T+0x81)==1&&p==((uint64_t)0x88<<32|0x77));
    reset();w32(0,T+0x7d,601);w32(0,T+0x79,1);w32(0,T+0x81,0x100);p=es2_turbo_engage_registers(&memory,&g,T,entry);
    CHECK(!calls&&r32(0,T+0x81)==0x100&&p==((uint64_t)entry[1]<<32|T));
    reset();w32(0,T+0x7d,600);w32(0,T+0x79,1);p=es2_turbo_engage_registers(&memory,&g,T,entry);CHECK(!calls&&!r32(0,T+0x81));
    reset();w32(0,T+0x7d,0x80000000u);p=es2_turbo_engage_registers(&memory,&g,T,entry);CHECK(!calls&&!r32(0,T+0x81));
    reset();w32(0,T+0x7d,0x7fffffffu);p=es2_turbo_engage_registers(&memory,&g,T,entry);CHECK(!calls&&r32(0,T+0x81)==1&&p==((uint64_t)entry[1]<<32|T));
}

int main(void){
    test_turbo_engage();
    test_fire_permit();test_dead_sleep();test_point_move();test_leader_route();test_ram_move();test_ram_step();
    test_travel_follow();
    printf("mech_travel: %u checks passed\n",checks);
    return 0;
}
