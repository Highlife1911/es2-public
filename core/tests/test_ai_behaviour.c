#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/ai_behaviour.h"
/* Synthetic guest: objects at 90000000h, frame scratch at A0000000h, image
 * base B with a window 499700h..4d9700h for the globals and tables. The
 * callees are logged with their slots and entry registers and return
 * planned values. */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
#define WIN 0x499700u
static uint8_t arena[16384],scratch[0x40],image[0x40000];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"ai_behaviour line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(WIN)<sizeof(image))return image+(a-G(WIN));
    fprintf(stderr,"ai_behaviour: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[6],r[3]; unsigned n,slot; es2_ai_call_frame fr; uint32_t pushed,temp; } logged;
static logged log_[64];static unsigned calls,observed;
static uint32_t ret[64][3];
static es2_ai_call_frame frame_;
/* weapon_choose's frame with the pushed temporary. */
static es2_ai_push_frame push_;static int push_on;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->fr=frame_;if(push_on){l->fr=push_.call;l->pushed=push_.pushed;l->temp=push_.temp;}l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);log_[observed++].slot=slot;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,FRAME,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){memset(&frame_,0,sizeof(frame_));calls=observed=0;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));memset(arena,0,sizeof(arena));}
#define A (OBJ+0x100)
#define T (OBJ+0x600)
#define W1 (OBJ+0xb00)
#define GRP (OBJ+0xd00)
#define VT (OBJ+0xe00)
#define CFG (OBJ+0xe80)
#define WS (OBJ+0xf00)

static void test_weapon_range(void){
    reset();w32(0,W1+0x1c,CFG);w32(0,CFG+0x2c,100);w32(0,CFG+0x30,200);
    CHECK(es2_weapon_range_registers(&memory,W1,150)==((uint64_t)150<<32|1));
    CHECK((uint32_t)es2_weapon_range_registers(&memory,W1,100)==0);   /* lower edge excluded */
    CHECK((uint32_t)es2_weapon_range_registers(&memory,W1,199)==1);
    CHECK((uint32_t)es2_weapon_range_registers(&memory,W1,200)==0);   /* upper edge excluded */
    CHECK((uint32_t)es2_weapon_range_registers(&memory,W1,0xffffff00u)==0); /* signed */
}

static void test_group_move(void){
    es2_weapon_graph g=graph();reset();
    w32(0,A+0x45,GRP);w32(0,GRP+0xc,GRP+0x40);w32(0,GRP+0x40,T);   /* leader T */
    ret[2][0]=0xaa;ret[2][1]=0xbb;
    uint64_t r=es2_group_move_registers(&memory,&g,A,entry);
    CHECK(calls==3&&log_[0].f==G(0x41fbb8)&&log_[0].r[0]==T&&log_[0].r[1]==GRP+0x40&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x41c3c8)&&log_[1].r[2]==A&&log_[2].f==G(0x41e8d4)&&log_[2].a[1]==0&&r==((uint64_t)0xbb<<32|0xaa));
    reset();w32(0,A+0x45,GRP);w32(0,GRP+0xc,GRP+0x40);w32(0,GRP+0x40,A);
    es2_group_move_registers(&memory,&g,A,entry);CHECK(log_[0].f==G(0x41fb60));
    reset();w32(0,A+0x45,GRP);w32(0,GRP+0xc,GRP+0x40);w32(0,GRP+0x40,A);w16(0,A+0x23e,2);ret[0][2]=0x77;
    es2_group_move_registers(&memory,&g,A,entry);
    CHECK(log_[0].f==G(0x41fac4)&&log_[0].a[1]==A+0x240&&log_[0].r[1]==A+0x240&&log_[1].r[2]==0x77);
}

static void test_target_search(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t E1=OBJ+0x1000,E2=OBJ+0x1800,P=OBJ+0x2000;
    w32(0,A+0x45,GRP);w32(0,GRP+0x6c,1);w32(0,GRP+0x48,OBJ+0x2800);w32(0,OBJ+0x2800+0xe,0); /* no order target: 980000 */
    w32(0,G(0x4a9b7a),0x4444);
    w16(0,E1+0x1a2,1);   /* two attackers share it: x/2 */
    ret[0][0]=E1;ret[1][0]=1;ret[2][0]=100000;ret[3][0]=E2;ret[4][0]=1;ret[5][0]=600000;ret[6][0]=0;ret[6][1]=0x99;
    uint64_t r=es2_target_search_registers(&memory,&g,A,P);
    CHECK(calls==7&&log_[0].r[2]==980000u&&log_[0].r[1]==A+0x5a&&r16(0,A+0x5b)==10000);
    CHECK(log_[2].f==G(0x4928d0)&&log_[2].a[0]==E1+0x26&&log_[2].a[1]==P);
    /* E1: (980000-100000)/2 = 440000; E2: 380000 < 440000 */
    CHECK((uint32_t)r==E1&&r>>32==0x99);
    /* a target that is out ranks below one that fights */
    reset();w32(0,A+0x45,GRP);w32(0,GRP+0x6c,0);w32(0,GRP+0x44,OBJ+0x2800);w32(0,OBJ+0x2800+0xe,1);
    w8(E1+0x99,1);ret[0][0]=E1;ret[1][0]=1;ret[2][0]=0;ret[3][0]=E2;ret[4][0]=1;ret[5][0]=80000;ret[6][0]=0;
    r=es2_target_search_registers(&memory,&g,A,P);
    CHECK(log_[0].r[2]==90000u&&(uint32_t)r==E2);
}

static void test_line_blocked(void){
    es2_weapon_graph g=graph();reset();
    w32(0,A,VT);w32(0,T,VT);w32(0,VT+0x24,0x12345678u);w32(0,A+0x1a4,T);w32(0,A+0x2e,10);w32(0,T+0x2e,20);
    w32(0,G(0x4a0be0),0xabcd);
    ret[0][0]=0;ret[1][0]=OBJ+0x3000;w32(0,OBJ+0x3000+0x1c,7);
    ret[2][0]=0x100;          /* segment: AL = 0 */
    ret[3][0]=0;              /* no object */
    ret[4][0]=0xffff0001u;ret[4][1]=0x55;
    uint64_t r=es2_line_blocked_registers(&memory,&g,A);
    CHECK(calls==5&&log_[0].slot==0x24&&log_[1].r[2]==A&&log_[2].a[0]==0xabcd&&log_[2].a[5]==1);
    CHECK(r32(0,FRAME+0x18+8)==510&&r32(0,FRAME+0xc+8)==27);   /* heights added */
    CHECK(log_[3].a[0]==FRAME+0x24&&log_[3].a[4]==0xffffffffu&&log_[4].r[0]==FRAME+0x24);
    CHECK(r==((uint64_t)0x55<<32|0xffff0002u));   /* terrain only: 2 */
    /* an object other than the target: 1 */
    reset();w32(0,A,VT);w32(0,T,VT);w32(0,A+0x1a4,T);ret[3][0]=1;w32(0,FRAME+0x28,OBJ+0x4000);
    r=es2_line_blocked_registers(&memory,&g,A);
    CHECK(calls==4&&(uint32_t)r==((T+0xc)&0xffff0000u)+1);
}

static void test_clear_shot(void){
    es2_weapon_graph g=graph();reset();
    w32(0,A+0x31e,1000);w32(0,A+0x322,2000);w32(0,A+0x326,3000);w16(0,A+0x10,0x1000);w32(0,A+0x5a,0x777);
    ret[0][0]=0x2000;          /* h */
    ret[1][0]=0x0800;          /* to A: 800h - 1000h < 0: side 1 */
    ret[2][0]=0;               /* timer expired */
    ret[3][0]=0;               /* line clear: resume the script */
    ret[5][0]=0x1800;
    uint64_t r=es2_clear_shot_registers(&memory,&g,A);
    CHECK(r8(A+0x60)==1&&r16(0,A+0x5e)==1&&r32(0,A+0x63)==5000&&r8(A+0x61)==0);
    CHECK(log_[2].f==G(0x467aa8)&&log_[3].f==G(0x41dc24)&&log_[3].r[1]==1&&log_[4].f==G(0x413e50)&&log_[4].a[1]==0x777);
    CHECK(log_[5].f==G(0x492934)&&log_[6].f==G(0x416a04)&&log_[6].a[1]==0x20&&log_[6].a[2]==0x100);
    CHECK(log_[7].f==G(0x41e8d4)&&(uint32_t)r==0);
    /* blocked: circle round Q at the measured distance, side 1 -> C000h */
    reset();w32(0,A+0x31e,1000);w16(0,A+0x5e,1);w8(A+0x60,1);ret[0][0]=0x2000;ret[1][0]=0;ret[2][0]=1;ret[3][0]=4321;
    es2_clear_shot_registers(&memory,&g,A);
    CHECK(log_[3].f==G(0x4928d0)&&r32(0,A+0x67)==4321&&log_[4].f==G(0x4929fc)&&(uint16_t)log_[4].a[1]==0xe000&&log_[4].a[2]==4321);
}

static void test_aim_fire_none(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t P=OBJ+0x2000;w32(0,P,1);w32(0,P+4,2);w32(0,P+8,3);
    w8(A+0xb5,1);ret[1][0]=0x31;ret[1][1]=0x32;
    uint64_t r=es2_aim_fire_registers(&memory,&g,&frame_,A,P,0x1234,T,entry);
    CHECK(calls==2&&log_[0].r[1]==entry[1]&&log_[1].f==G(0x41ef14)&&log_[1].a[1]==FRAME&&r32(0,FRAME+8)==3&&r8(A+0xb5)==0);
    CHECK(r==((uint64_t)0x32<<32|0x31));
    /* The original's EBX/ESI/EDI and return address at each call. */
    CHECK(log_[0].fr.ebx==A&&log_[0].fr.esi==P&&log_[0].fr.edi==P&&log_[0].fr.ret==G(0x41f5bb));
    CHECK(log_[1].fr.ebx==A&&log_[1].fr.esi==0&&log_[1].fr.edi==ret[0][0]&&log_[1].fr.ret==G(0x41f785));
    /* No frame: nothing is stored. */
    reset();w8(A+0xb5,1);es2_aim_fire_registers(&memory,&g,NULL,A,P,0x1234,T,entry);
    CHECK(calls==2&&log_[1].fr.ebx==0&&log_[1].fr.ret==0);
}

/* The fire path: the kept weapon W, spread draws, torso_aim, +60h and +28h,
 * with ESI = W, EDI = the spread word over d's high word, then V. */
static void test_aim_fire_frames(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t P=OBJ+0x2000,V=OBJ+0x2100;w32(0,P,1);w32(0,P+4,2);w32(0,P+8,3);
    w32(0,A+0x2ac,W1);w32(0,W1,VT);w32(0,T,VT);w32(0,W1+0x20,CFG);w16(0,CFG+0xa,5);
    w32(0,W1+0x1c,CFG+0x40);w16(0,CFG+0x40+0x56,6);
    w32(0,A+0x45,GRP);w8(GRP+0x12,1);w16(0,G(0x4a9e5c),0);w16(0,G(0x49a30c),0x0007);
    ret[0][0]=0x12345678u;     /* d */
    ret[1][0]=1;ret[2][0]=1;   /* +2Ch, weapon_range: kept */
    ret[7][0]=V;ret[8][0]=1;   /* torso_aim's V, +60h state 1 */
    uint64_t r=es2_aim_fire_registers(&memory,&g,&frame_,A,P,0x1234,T,entry);
    CHECK(calls==10&&log_[3].slot==0x38&&log_[4].f==G(0x492f24)&&log_[7].f==G(0x41ef14)&&log_[8].slot==0x60&&log_[9].slot==0x28);
    CHECK(log_[1].fr.esi==P&&log_[1].fr.edi==0x12345678u&&log_[1].fr.ret==G(0x41f5e3)&&log_[2].fr.ret==G(0x41f5f6));
    CHECK(log_[3].fr.esi==W1&&log_[3].fr.edi==0x12345678u&&log_[3].fr.ret==G(0x41f657));
    CHECK(log_[4].fr.edi==0x12340007u&&log_[4].fr.ret==G(0x41f6bd)&&log_[6].fr.ret==G(0x41f6e5));
    CHECK(log_[7].fr.esi==W1&&log_[7].fr.edi==0x12340007u&&log_[7].fr.ret==G(0x41f6f8));
    CHECK(log_[8].fr.edi==V&&log_[8].fr.ret==G(0x41f705)&&log_[9].fr.ebx==A&&log_[9].fr.esi==W1&&log_[9].fr.edi==V&&log_[9].fr.ret==G(0x41f74f));
    CHECK(r16(0,FRAME+0xe)==5&&r32(0,A+0x2ac)==W1&&(uint16_t)r==6);
}

static void test_weapon_choose(void){
    es2_weapon_graph g=graph();reset();
    w32(0,T,VT);w32(0,A+0x202,WS);w16(0,WS+8,1);w32(0,WS+4,WS+0x20);w32(0,WS+0x20,W1);w32(0,W1,VT);
    w32(0,A+0x45,GRP);w32(0,G(0x4a9b84),T);w32(0,T+0x45,GRP);
    w16(0,WS+0xa+2*2,1);       /* state 2 allowed */
    w32(0,W1+0x20,CFG);w16(0,CFG+6,50);w32(0,W1+0x1c,CFG+0x40);w16(0,CFG+0x40+0x34,3);
    ret[0][0]=10;              /* best start */
    ret[1][0]=0;               /* +34h */
    ret[2][0]=0;               /* +5Ch: ready */
    ret[3][0]=2;               /* +60h: state 2 */
    ret[4][0]=1;               /* in range */
    ret[5][0]=1;               /* +2Ch */
    ret[6][0]=500;ret[7][0]=300;ret[7][2]=0xabcd0000u;ret[8][0]=4;ret[9][0]=0xfffe;
    uint64_t r=es2_weapon_choose_registers(&memory,&g,A,0x3000,800,T,entry);
    CHECK(calls==10&&log_[0].a[0]==0&&log_[0].a[4]==0xffffff9cu&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].slot==0x34&&log_[1].a[1]==1&&log_[6].a[0]==100&&log_[6].a[1]==50&&log_[7].a[1]==3);
    CHECK(log_[8].a[1]==0xabcd0023u&&log_[8].r[1]==200);
    /* 200 + 4 * -2 = 192 > 10 */
    CHECK((uint32_t)r==W1&&r8(A+0x96)==0);
}

/* The same choice with the register frame: EBX = T, then W; ESI =
 * the word A+2AAh, +60h's EAX, the rank and the first draw; the pushed
 * temporaries of 41f4e3/41f502; the frame locals at the original's
 * offsets. Then the no-ready-weapon path to trigger_fire. */
static void test_weapon_choose_frame(void){
    es2_weapon_graph g=graph();reset();memset(scratch,0xcc,sizeof(scratch));memset(&push_,0,sizeof(push_));push_on=1;
    w32(0,T,VT);w32(0,A+0x202,WS);w16(0,WS+8,1);w32(0,WS+4,WS+0x20);w32(0,WS+0x20,W1);w32(0,W1,VT);
    w32(0,A+0x45,GRP);w32(0,G(0x4a9b84),T);w32(0,T+0x45,GRP);w16(0,A+0x2aa,0xfff0);
    w16(0,WS+0xa+2*2,1);
    w32(0,W1+0x20,CFG);w16(0,CFG+6,50);w32(0,W1+0x1c,CFG+0x40);w16(0,CFG+0x40+0x34,3);
    ret[0][0]=10;ret[1][0]=0;ret[2][0]=0;ret[3][0]=0x77770002u;ret[4][0]=1;ret[5][0]=1;
    ret[6][0]=500;ret[7][0]=300;ret[7][2]=0xabcd0000u;ret[8][0]=4;ret[9][0]=0xfffe;
    uint64_t r=es2_weapon_choose_frame_registers(&memory,&g,&push_,A,0x3000,800,T,entry);
    static const uint32_t rets[10]={0x41f382,0x41f3a2,0x41f41e,0x41f430,0x41f445,0x41f4a2,0x41f4e0,0x41f4fc,0x41f512,0x41f527};
    CHECK(calls==10&&(uint32_t)r==W1);
    for(unsigned k=0;k<10;++k)CHECK(log_[k].fr.ret==G(rets[k])&&log_[k].fr.edi==A&&log_[k].fr.ebx==(k<2?T:W1)&&log_[k].pushed==(k>=7));
    CHECK(log_[0].fr.esi==0xfffffff0u&&log_[3].fr.esi==0xfffffff0u&&log_[4].fr.esi==0x77770002u&&log_[5].fr.esi==0x77770002u);
    CHECK(log_[6].fr.esi==50&&log_[8].fr.esi==50&&log_[9].fr.esi==4&&log_[7].temp==500&&log_[8].temp==200&&log_[9].temp==200);
    const uint32_t F=FRAME+ES2_WEAPON_CHOOSE_SCRATCH;
    CHECK(r16(0,F-2)==192&&r16(0,F-4)==0&&r32(0,F-8)==W1&&r16(0,F-0xa)==1&&r16(0,F-0xc)==0&&r16(0,F-0xe)==0x23);
    CHECK(r16(0,F-0x10)==1&&r32(0,F-0x14)==50&&r32(0,F-0x18)==3&&scratch[0x18]==0xcc);
    /* No ready weapon: none stays 1, trigger_fire with EBX = W, ESI = the word. */
    reset();memset(scratch,0xcc,sizeof(scratch));
    w32(0,T,VT);w32(0,A+0x202,WS);w16(0,WS+8,1);w32(0,WS+4,WS+0x20);w32(0,WS+0x20,W1);w32(0,W1,VT);
    w32(0,A+0x45,GRP);w32(0,G(0x4a9b84),T);w32(0,T+0x45,OBJ+0x3000);w16(0,A+0x2aa,7);w32(0,A+0x1b6,OBJ+0x3100);
    ret[2][0]=1;
    es2_weapon_choose_frame_registers(&memory,&g,&push_,A,0x3000,800,T,entry);push_on=0;
    CHECK(calls==4&&log_[3].f==G(0x423430)&&log_[3].fr.ret==G(0x41f581)&&log_[3].fr.ebx==W1&&log_[3].fr.esi==7&&log_[3].pushed==0);
    CHECK(r16(0,F-0xc)==1&&r32(0,F-8)==0&&r16(0,F-0x10)==1&&r32(0,F-0x14)==0xccccccccu&&r8(A+0xa5)==1);
}

static void test_hunt_out(void){
    es2_weapon_graph g=graph();reset();
    w32(0,A+0x45,GRP);w32(0,GRP+0xc,GRP+0x40);w32(0,GRP+0x40,T);w32(0,G(0x4a9b84),0);
    w32(0,A+0x1a4,T);w16(0,T+0x1a2,2);w16(0,A+0x23e,4);w32(0,A+0x248,OBJ+0x1000);w8(A+0x99,1);
    ret[0][0]=OBJ+0x2000;ret[1][0]=0;ret[2][1]=0x66;
    uint64_t r=es2_hunt_registers(&memory,&g,A,entry);
    CHECK(calls==3&&log_[0].r[0]==entry[0]&&log_[1].f==G(0x467a48)&&log_[1].r[0]==GRP&&log_[1].r[2]==T);
    CHECK(r16(0,T+0x1a2)==1&&r32(0,A+0x1a4)==OBJ+0x1000&&r16(0,OBJ+0x1000+0x1a2)==1&&r8(A+0x9d)==1);
    CHECK(log_[2].f==G(0x413e50)&&log_[2].a[1]==G(0x499800)&&log_[2].r[1]==1&&r16(0,A+0x2a2)==0xffff&&r==(uint64_t)0x66<<32);
}

static void test_aim_target_outside(void){
    es2_weapon_graph g=graph();reset();
    w32(0,A+0x1f2,CFG);w16(0,CFG+0x22,0x1000);w16(0,A+0x10,0);
    ret[0][0]=0x5000;ret[0][1]=0x12340000u;ret[1][0]=7;ret[1][1]=8;
    uint64_t r=es2_aim_target_registers(&memory,&g,A,0x99,T);
    CHECK(calls==2&&log_[1].f==G(0x41e8d4)&&log_[1].r[2]==0&&log_[1].r[1]==0x12346000u&&r==((uint64_t)8<<32|7));
    reset();w32(0,A+0x1f2,CFG);w16(0,CFG+0x22,0x1000);w32(0,T,VT);w16(0,A+0x2a2,3);
    ret[0][0]=0x0800;ret[1][0]=10000;ret[2][2]=0xbeef0000u;
    es2_aim_target_registers(&memory,&g,A,0x99,T);
    CHECK(log_[2].slot==0x58&&log_[2].a[2]==FRAME&&log_[2].r[0]==5000&&log_[3].f==G(0x41f5a0)&&log_[3].a[2]==0xbeef0099u);
}

int main(void){
    test_weapon_range();test_group_move();test_target_search();test_line_blocked();test_clear_shot();
    test_aim_fire_none();test_aim_fire_frames();test_weapon_choose();test_weapon_choose_frame();test_hunt_out();test_aim_target_outside();
    printf("ai_behaviour: %u checks passed\n",checks);
    return 0;
}
