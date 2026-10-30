#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/mech_combat.h"
/* Synthetic guest: objects at 90000000h, the frame at A0000000h, image base
 * B. The callees are logged with their slots, entry registers and the frame
 * the core names, and return planned values. */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
static uint8_t arena[16384],scratch[0x40];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mech_combat line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    fprintf(stderr,"mech_combat: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[6],r[3]; unsigned n,slot; es2_ai_call_frame fr; } logged;
static logged log_[64];static unsigned calls,observed;
static uint32_t ret[64][3];
static es2_ai_call_frame frame_;
/* A callee that writes the frame: index of the call, address, word. */
static unsigned poke_at=99;static uint32_t poke_addr;static uint16_t poke_value;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->fr=frame_;l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(calls==poke_at)w16(0,poke_addr,poke_value);
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);log_[observed++].slot=slot;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,FRAME,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){memset(&frame_,0,sizeof(frame_));calls=observed=0;poke_at=99;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));
    memset(arena,0,sizeof(arena));memset(scratch,0,sizeof(scratch));}
#define A (OBJ+0x100)
#define T (OBJ+0x600)
#define CFG (OBJ+0x1000)
#define ESI 0x5e5e5e5eu
#define EDI 0xd1d1d1d1u
static const logged *last(void){return log_+calls-1;}

static void test_engage_end(void){
    reset();
    CHECK(es2_engage_end_registers(&memory,A,entry)==((uint64_t)entry[1]<<32|1));
    w16(0,A+0x23e,4);w32(0,A+0x1a4,T);
    CHECK(es2_engage_end_registers(&memory,A,entry)==((uint64_t)T<<32|1)&&r16(0,A+0x23e)==4);
    w32(0,A+0x248,T);
    CHECK(es2_engage_end_registers(&memory,A,entry)==((uint64_t)T<<32|1)&&r16(0,A+0x23e)==0);
}

static void test_skirt_check(void){
    es2_weapon_graph g=graph();reset();
    CHECK(es2_skirt_check_registers(&memory,&g,A,entry)==(uint64_t)entry[1]<<32&&calls==0);
    w8(A+0xad,1);w32(0,A+0x4d,0x4994da);
    ret[0][1]=0x77;ret[0][2]=0x88;ret[1][0]=0xabcd1234u;ret[1][1]=0x99;
    uint64_t r=es2_skirt_check_registers(&memory,&g,A,entry);
    CHECK(calls==2&&log_[0].f==G(0x413e50)&&log_[0].a[0]==A+0x4d&&log_[0].a[1]==G(0x499708)&&log_[0].r[0]==A+0x4d);
    CHECK(log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]&&log_[1].f==G(0x49288c)&&log_[1].a[0]==A+0x26&&log_[1].a[1]==A+0x31e);
    CHECK(log_[1].r[0]==A+0x31e&&log_[1].r[1]==0x77&&log_[1].r[2]==0x88&&r32(0,A+0x5a)==0x4994da&&r32(0,A+0x67)==0xabcd1234u);
    CHECK(r==((uint64_t)0x99<<32|0xabcd0001u));
}

static void test_attack_step(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t P=FRAME,L=FRAME+0xc,S=FRAME+0x18;
    /* skirt_check ends the step. */
    ret[0][0]=0x10001;ret[0][1]=0x42;
    uint64_t r=es2_attack_step_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(calls==1&&log_[0].f==G(0x41de9c)&&log_[0].r[0]==entry[0]&&log_[0].fr.esi==ESI&&log_[0].fr.edi==P&&log_[0].fr.ret==G(0x41c5a9));
    CHECK(r==(uint64_t)0x42<<32);
    /* Far aspect (S+8 = 1F40h), bearing magnitude 2000h: reverse; engage_done
     * nonzero only in the high word still ends the engagement. */
    reset();w32(0,A+0x1a4,T);ret[0][0]=0xffff0000u;w16(0,S+8,0x1f40);w16(0,S+4,0x2000);w16(0,S+6,0x1357);
    ret[1][0]=0xaaaa0000u;ret[2][0]=0xbbbb0000u;ret[2][1]=0x21;ret[2][2]=0x31;ret[3][0]=0x5;ret[4][0]=0x10000;ret[5][0]=1;ret[5][1]=0x66;
    r=es2_attack_step_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(calls==6&&r16(0,S+0xe)==0xffff&&log_[1].f==G(0x41e758)&&log_[1].r[0]==S&&log_[1].r[2]==A&&log_[1].a[2]==0&&log_[1].fr.esi==T);
    CHECK(log_[2].f==G(0x41e828)&&log_[2].r[0]==0xaaaa0000u&&log_[2].r[1]==0xffffffffu&&log_[2].r[2]==S&&log_[2].a[1]==S);
    CHECK(log_[3].f==G(0x41ea7c)&&log_[3].a[1]==0xbbbb1357u&&log_[3].a[2]==0&&log_[3].r[1]==0x21&&log_[3].r[2]==0x31&&log_[3].fr.ret==G(0x41c6cd));
    CHECK(log_[4].f==G(0x41c4a8)&&log_[4].r[0]==5&&log_[5].f==G(0x41c478)&&log_[5].r[0]==0x10000&&log_[5].r[2]==A);
    CHECK(log_[5].fr.esi==T&&log_[5].fr.edi==P&&log_[5].fr.ret==G(0x41c71f)&&r==((uint64_t)0x66<<32|1));
    /* Near aspect, forward (S+4 = 7060h), bearing negative and magnitude
     * above 4000h: same side (no turn); P = L after point_offset. */
    reset();w32(0,A+0x1a4,T);w32(0,T+0x26,100);w32(0,T+0x2a,200);w32(0,T+0x2e,300);w16(0,T+0x10,0x1000);w16(0,A+0x10,0x0100);
    w16(0,S+8,0x1f3f);w16(0,S+4,0x7060);w16(0,S+2,0x8000);
    ret[3][0]=0x12345678u;ret[3][1]=0x9;
    r=es2_attack_step_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(r16(0,S+0xe)==1&&log_[2].f==G(0x4929fc)&&log_[2].a[0]==L&&log_[2].a[1]==0x5000&&log_[2].a[2]==0x4650&&log_[2].r[1]==1&&log_[2].r[2]==1);
    CHECK(r32(0,P)==100&&r32(0,P+4)==200&&r32(0,P+8)==300&&log_[3].f==G(0x492934)&&log_[3].a[0]==P&&log_[3].a[1]==A+0x26);
    CHECK(log_[3].r[0]==A+0x26&&log_[3].r[1]==100&&log_[3].r[2]==200&&r16(0,S+2)==0x5578&&log_[4].r[0]==0x12345578u&&log_[4].r[1]==9);
    CHECK(calls==7&&r==0);   /* engage_done 0, A not out: no engage_end */
    /* Reverse (S+4 = 7061h): the mirror point P = 2A - L; the turn is
     * applied (bearing positive, magnitude above 4000h). */
    reset();w32(0,A+0x1a4,T);w32(0,T+0x26,100);w32(0,T+0x2a,200);w32(0,T+0x2e,300);
    w32(0,A+0x26,10);w32(0,A+0x2a,20);w32(0,A+0x2e,30);w16(0,S+4,0x7061);w16(0,S+2,0x10);
    r=es2_attack_step_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(r16(0,S+0xe)==0xffff&&log_[2].a[1]==(((uint32_t)(0x8000-0x7061+0xffff8000u)&0xffff0000u)|0xc000));
    CHECK(r32(0,L)==90&&r32(0,L+4)==180&&r32(0,L+8)==270&&r32(0,P)==(uint32_t)-80&&r32(0,P+4)==(uint32_t)-160&&r32(0,P+8)==(uint32_t)-240);
    CHECK(log_[3].r[1]==90&&log_[3].r[2]==180);
    /* A out through byte A+0A4h: EAX = EDX = 1, EDI = A+92h. */
    reset();w32(0,A+0x1a4,T);w16(0,S+8,0x2000);w8(A+0xa4,1);
    es2_attack_step_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(calls==6&&last()->r[0]==1&&last()->r[1]==1&&last()->fr.edi==A+0x92&&last()->fr.esi==T);
    reset();w32(0,A+0x1a4,T);w16(0,S+8,0x2000);w8(A+0xa5,1);
    es2_attack_step_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(calls==6&&last()->r[0]==A+0x92&&last()->r[1]==1&&last()->fr.edi==P);
    reset();w32(0,A+0x1a4,T);w16(0,S+8,0x2000);w8(A+0x99,1);
    es2_attack_step_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(calls==6&&last()->fr.esi==A+0x92&&last()->fr.edi==A+0x92);
}

static void test_base_attack(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t S=FRAME;
    w32(0,A+0x1a4,T);w32(0,T+0x1f2,CFG);w32(0,A+0x45,0x4545);
    ret[1][1]=0xcccc0000u;ret[3][0]=0x77770000u;w16(0,S+6,0x1111);ret[5][0]=0x10000;ret[5][1]=0x11223344u;
    uint64_t r=es2_base_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    /* Config word 0: aspect 7D00h; flank_step with ECX = T; bounds. */
    CHECK(log_[1].f==G(0x41e758)&&log_[1].r[1]==S&&log_[1].r[2]==A&&log_[1].fr.esi==ESI&&log_[1].fr.edi==EDI);
    CHECK(r16(0,S+8)==0x7d00&&r16(0,S+6)==0x7d00&&log_[2].f==G(0x41c72c)&&log_[2].r[0]==S&&log_[2].r[1]==0xcccc7d00u&&log_[2].r[2]==T);
    CHECK(r32(0,S+0x10)==0x61a8&&r32(0,S+0x14)==0xffffafc8u&&log_[3].r[2]==S&&log_[4].a[1]==0x77777d00u);
    /* order_target with EDX = T, ECX = the group; AX = 0 high word set: T test. */
    CHECK(log_[5].f==G(0x423918)&&log_[5].a[0]==0x4545&&log_[5].a[1]==T&&log_[5].r[1]==T&&log_[5].r[2]==0x4545&&calls==6&&r==0);
    /* T out by its byte A5: ECX = EAX = EDX = 1. */
    reset();w32(0,A+0x1a4,T);w32(0,T+0x1f2,CFG);w16(0,CFG+0x2e,1);w8(T+0xa5,1);
    es2_base_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(calls==7&&last()->f==G(0x41c478)&&last()->r[0]==1&&last()->r[1]==1&&last()->r[2]==1&&last()->fr.esi==ESI);
    /* T test: byte A4 sets ESI = T+92h; byte 99 also EDI. */
    reset();w32(0,A+0x1a4,T);w32(0,T+0x1f2,CFG);w8(T+0x99,1);
    es2_base_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(calls==7&&last()->fr.esi==T+0x92&&last()->fr.edi==T+0x92);
    /* Order state 4 with an order target: skip order_target; byte T+99h
     * sign-extended (80h). */
    reset();w32(0,A+0x1a4,T);w32(0,T+0x1f2,CFG);w16(0,A+0x23e,4);w32(0,A+0x248,T);w8(T+0x99,0x80);ret[4][1]=0x12345600u;
    es2_base_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(calls==6&&last()->f==G(0x41c478)&&last()->r[0]==0xffffff80u&&last()->r[2]==0xffffff80u&&last()->r[1]==0x12345680u);
    /* Order state 4 and no order target, order_target nonzero, T+99h 0, A
     * out by A+0A5h: EAX = A+92h, EDX = 1, ECX = 0. */
    reset();w32(0,A+0x1a4,T);w32(0,T+0x1f2,CFG);w16(0,A+0x23e,4);ret[5][0]=1;w8(A+0xa5,1);
    es2_base_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(calls==7&&last()->r[0]==A+0x92&&last()->r[1]==1&&last()->r[2]==0&&last()->fr.ret==G(0x41c9c0));
}

static void test_flyer_attack(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t S=FRAME;
    w32(0,A+0x1a4,T);
    /* Small bearing, wide and far: forward. */
    w16(0,S+4,0x7cf);w16(0,S+8,0x7d1);w32(0,S+0xa,0x88b9);ret[0][0]=0xeeee0000u;
    uint64_t r=es2_flyer_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(log_[0].f==G(0x41e758)&&log_[0].r[0]==S&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]&&r16(0,S+0xe)==1);
    CHECK(log_[1].f==G(0x41e828)&&log_[1].r[0]==0xeeee0000u&&log_[1].r[1]==0&&log_[1].r[2]==S&&r32(0,S+0x10)==0&&r32(0,S+0x14)==1000000u);
    CHECK(calls==3&&r==0);
    /* Wide only: stop; far only: stop; neither: reverse. */
    reset();w32(0,A+0x1a4,T);w16(0,S+4,0x7cf);w16(0,S+8,0x7d1);w32(0,S+0xa,0x88b8);
    es2_flyer_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);CHECK(r16(0,S+0xe)==0);
    reset();w32(0,A+0x1a4,T);w16(0,S+4,0x7cf);w16(0,S+8,0x7d0);w32(0,S+0xa,0x88b9);
    es2_flyer_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);CHECK(r16(0,S+0xe)==0);
    reset();w32(0,A+0x1a4,T);w16(0,S+4,0x7cf);w16(0,S+8,0x7d0);w32(0,S+0xa,0x88b8);
    es2_flyer_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);CHECK(r16(0,S+0xe)==0xffff);
    reset();w32(0,A+0x1a4,T);w16(0,S+4,0x7d0);w16(0,S+8,0x7d1);w32(0,S+0xa,0x88b9);
    es2_flyer_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);CHECK(r16(0,S+0xe)==0xffff);
    /* Beyond 249F0h (reread after aim_select): AX = 1 over aim_select's EAX. */
    reset();w32(0,A+0x1a4,T);w32(0,S+0xa,0x249f1);ret[2][0]=0x45670000u;ret[2][1]=0x3;
    r=es2_flyer_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);CHECK(calls==3&&r==((uint64_t)3<<32|0x45670001u));
    /* T destroyed: engage_end with EAX = T+92h. */
    reset();w32(0,A+0x1a4,T);w8(T+0x99,1);ret[2][1]=0x8;ret[2][2]=0x9;
    es2_flyer_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(calls==4&&last()->r[0]==T+0x92&&last()->r[1]==8&&last()->r[2]==9&&last()->fr.ret==G(0x41cab0));
}

static void test_face_off_flank(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t S=FRAME;
    w32(0,A+0x1a4,T);w16(0,S+4,0x2000);w16(0,S+6,0x2468);ret[0][0]=0x30000;ret[1][2]=0x7777aaaau;ret[2][1]=0xdddd0000u;
    uint64_t r=es2_face_off_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(log_[1].r[0]==0x30000&&log_[1].r[1]==S&&log_[1].r[2]==A&&r16(0,S+0xe)==0xffff&&log_[2].r[2]==0x7776ffffu&&log_[2].r[0]==S);
    CHECK(r32(0,S+0x10)==0x3e80&&r32(0,S+0x14)==0x88b8&&log_[3].a[1]==0xdddd2468u&&log_[3].r[1]==0xdddd2468u&&log_[3].fr.ret==G(0x41d485));
    CHECK(calls==5&&r==0);
    reset();w32(0,A+0x1a4,T);w16(0,S+4,0x1fff);ret[1][2]=0x7777aaaau;ret[4][0]=0x80000000u;ret[5][1]=0x5;
    r=es2_face_off_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(r16(0,S+0xe)==1&&calls==6&&last()->r[0]==0x80000000u&&last()->r[2]==A&&last()->fr.ret==G(0x41d4d7)&&r==((uint64_t)5<<32));
    /* flank_attack: flank_step between target_bearing and drive_apply. */
    reset();w32(0,A+0x1a4,T);ret[1][0]=0x11;ret[1][1]=0x22;ret[2][1]=0x33;ret[2][2]=0x44;
    es2_flank_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(log_[2].f==G(0x41c72c)&&log_[2].r[0]==0x11&&log_[2].r[1]==0x22&&log_[2].r[2]==S&&log_[2].fr.ret==G(0x41d51c));
    CHECK(log_[3].f==G(0x41e828)&&log_[3].r[0]==S&&log_[3].r[1]==0x33&&log_[3].r[2]==0x44&&log_[4].fr.ret==G(0x41d539)&&calls==6);
    reset();ret[0][0]=1;ret[0][1]=0x4;
    r=es2_flank_attack_registers(&memory,&g,&frame_,ESI,EDI,A,entry);CHECK(calls==1&&r==((uint64_t)4<<32));
}

static void test_drive_off(void){
    es2_weapon_graph g=graph();reset();
    ret[0][0]=0xffff0000u;ret[1][0]=0x7;ret[1][1]=0x8;
    uint64_t r=es2_drive_off_registers(&memory,&g,&frame_,ESI,EDI,A,entry);
    CHECK(calls==2&&log_[0].f==G(0x41de9c)&&log_[0].r[0]==entry[0]&&log_[1].f==G(0x41d41c)&&log_[1].r[0]==0xffff0000u&&log_[1].r[2]==A);
    CHECK(log_[1].fr.esi==ESI&&log_[1].fr.edi==EDI&&log_[1].fr.ret==G(0x41df15)&&r==((uint64_t)8<<32|7));
    reset();ret[0][0]=2;ret[0][1]=0x6;
    r=es2_drive_off_registers(&memory,&g,NULL,0,0,A,entry);CHECK(calls==1&&r==((uint64_t)6<<32));
}

int main(void){
    test_engage_end();test_skirt_check();test_attack_step();test_base_attack();test_flyer_attack();
    test_face_off_flank();test_drive_off();
    printf("mech_combat: %u checks passed\n",checks);
    return 0;
}
