#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/ai_helpers.h"
/* Synthetic guest: objects at 90000000h, frame scratch at A0000000h, image
 * base B with the globals and tables the helpers read. The callees are
 * logged with their slots and entry registers and return planned values. */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
static uint8_t arena[8192],scratch[ES2_AI_HELPERS_SCRATCH],image[0x100];
static uint16_t quarter[1025];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"ai_helpers line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
/* Image window: 49a328 .. 49a340 (tables), 4a9b7a (list head), 4d2599 (RNG). */
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x49a300)<0x80u)return image+(a-G(0x49a300));
    if(a-G(0x4a9b78)<0x10u)return image+0x80+(a-G(0x4a9b78));
    if(a-G(0x4d2598)<0x10u)return image+0x90+(a-G(0x4d2598));
    fprintf(stderr,"ai_helpers: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[4],r[3]; unsigned n,slot; } logged;
static logged log_[64];static unsigned calls,observed;
static uint32_t ret[64][3];
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);log_[observed++].slot=slot;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,FRAME,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){calls=observed=0;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));}
#define A (OBJ+0x100)
#define T (OBJ+0x600)
#define S (OBJ+0xc00)
#define GRP (OBJ+0xd00)
#define CFG (OBJ+0xd80)
#define VT (OBJ+0xe00)
#define PARTS (OBJ+0xe80)

static void test_point_offset(void){
    for(unsigned i=0;i<1025;++i)quarter[i]=(uint16_t)(0x4000-16*i);
    uint8_t q[2050];for(unsigned i=0;i<1025;++i){q[2*i]=(uint8_t)quarter[i];q[2*i+1]=(uint8_t)(quarter[i]>>8);}
    /* angle 0: a = 4000h (cosine 0), second angle 0 (cosine 4000h): y += d. */
    w32(0,OBJ,100);w32(0,OBJ+4,200);
    uint64_t r=es2_point_offset_registers(&memory,OBJ,0xabcd0000u,1000,q);
    CHECK((uint32_t)r==OBJ&&r>>32==0&&r32(0,OBJ)==100&&r32(0,OBJ+4)==1200);
    r=es2_point_offset_registers(&memory,OBJ,0,0xfffffc18u,q);  /* d = -1000 */
    CHECK(r>>32==0xffffffffu&&r32(0,OBJ+4)==200&&r32(0,OBJ)==100);
    /* angle C000h: a = 0 (cosine 4000h) moves x; second 0C000h is -cos. */
    r=es2_point_offset_registers(&memory,OBJ,0xc000u,1000,q);
    CHECK(r32(0,OBJ)==1100&&r32(0,OBJ+4)==200&&r>>32==0);
}

static void test_threat_sum(void){
    es2_weapon_graph g=graph();reset();
    w32(0,G(0x4a9b7a),0x12345678u);
    const uint32_t e1=OBJ+0x1000,e2=OBJ+0x1400,e3=OBJ+0x1800;
    w32(0,e1+0x1a4,A);w16(0,e1+0x29e,0xfffe);w32(0,e2+0x1a4,T);w16(0,e2+0x29e,7);w32(0,e3+0x1a4,A);w16(0,e3+0x29e,5);
    ret[0][0]=e1;ret[0][1]=0x77;ret[1][0]=e2;ret[2][0]=e3;ret[2][2]=0x99;ret[3][0]=0;ret[3][1]=0xabc;
    uint64_t r=es2_threat_sum_registers(&memory,&g,A,entry);
    CHECK(calls==4&&(uint32_t)r==3u&&r>>32==0xabc);
    CHECK(log_[0].f==G(0x471c70)&&log_[0].a[0]==0x12345678u&&log_[0].a[1]==0&&log_[0].r[1]==0&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].a[1]==e1&&log_[1].r[1]==0x77&&log_[1].r[2]==0xfffffffeu&&log_[3].a[1]==e3&&log_[3].r[2]==5);
}

static void test_engage_done(void){
    es2_weapon_graph g=graph();
    memset(arena,0,sizeof(arena));w32(0,A+0x1a4,T);w32(0,A+0x45,GRP);
    /* Order state 4 with an order target: class above the limit. */
    reset();w16(0,A+0x23e,4);w32(0,A+0x248,T);w16(0,A+0x250,1);ret[0][0]=0xffff0002u;ret[0][1]=0x42;
    uint64_t r=es2_engage_done_registers(&memory,&g,A,entry);
    CHECK(calls==1&&log_[0].f==G(0x411cb4)&&log_[0].a[0]==T&&log_[0].r[0]==T&&(uint32_t)r==1&&r>>32==0x42);
    reset();ret[0][0]=1;r=es2_engage_done_registers(&memory,&g,A,entry);CHECK((uint32_t)r==0);
    /* order_target nonzero, group byte 0: T's out flags. */
    w16(0,A+0x23e,3);reset();ret[0][0]=0x10000u|1u;ret[0][1]=0x12345600u;
    r=es2_engage_done_registers(&memory,&g,A,entry);
    CHECK(log_[0].f==G(0x423918)&&log_[0].a[0]==GRP&&log_[0].a[1]==T&&log_[0].r[0]==entry[0]&&log_[0].r[2]==GRP&&r==0);
    w8(T+0x99,1);reset();ret[0][0]=1;r=es2_engage_done_registers(&memory,&g,A,entry);CHECK(r==((uint64_t)1<<32|1));
    /* group byte set: the destroyed byte sign-extended. */
    w8(GRP+0x12,1);w8(T+0x99,0x80);reset();ret[0][0]=1;ret[0][1]=0x12345611u;
    r=es2_engage_done_registers(&memory,&g,A,entry);CHECK((uint32_t)r==0xffffff80u&&r>>32==0x12345680u);
    /* order_target zero: any of the three flags. */
    w8(T+0x99,0);w8(T+0xa5,1);reset();r=es2_engage_done_registers(&memory,&g,A,entry);CHECK(r==((uint64_t)1<<32|1));
    w8(T+0xa5,0);reset();r=es2_engage_done_registers(&memory,&g,A,entry);CHECK(r==0);
}

static void test_flank_step(void){
    es2_weapon_graph g=graph();
    memset(arena,0,sizeof(arena));w32(0,A+0x1a4,T);w32(0,A,VT);w32(0,VT+0x3c,0x5555aaaau);
    /* First timer running. */
    reset();ret[0][0]=1;w32(0,S+0x10,9);
    uint64_t r=es2_flank_step_registers(&memory,&g,A,S,entry);
    CHECK(calls==1&&log_[0].a[0]==A+0x5f&&r==A+0x65&&r16(0,S+0xe)==0xffff&&r32(0,S+0x10)==0&&r16(0,A+0x63)==0xfa0);
    /* Expired, second expired, +288h > 64h, close (S+8 = 6000h), virtual in range. */
    reset();w32(0,A+0x288,0x65);w16(0,S+8,0x6000);ret[2][0]=0x07cf;
    r=es2_flank_step_registers(&memory,&g,A,S,entry);
    CHECK(calls==3&&log_[1].a[0]==A+0x62&&log_[1].r[1]==A+0x62&&log_[1].r[2]==A+0x5f&&r16(0,A+0x60)==0x3e8);
    CHECK(log_[2].f==0x5555aaaau&&log_[2].slot==0x3c&&log_[2].r[0]==A+0x65&&log_[2].r[1]==VT&&log_[2].r[2]==0x6000);
    CHECK(r==((uint64_t)(A+0x65)<<32)&&r16(0,S+0xe)==0&&arena[0x166]==1);
    reset();ret[2][0]=0x07d0;(void)es2_flank_step_registers(&memory,&g,A,S,entry);CHECK(r16(0,S+0xe)==0xffff);
    /* Far: byte +66h set, limit 4000h > 3FFFh. */
    reset();w16(0,S+8,0x3fff);w32(0,T+0x26,10);w32(0,T+0x2a,0x00020003u);w32(0,T+0x2e,0x00050000u);
    w16(0,S,0x100);w16(0,S+2,0xffff);w16(0,A+0x10,0x10);ret[3][0]=0x1234;ret[3][1]=0x77;
    r=es2_flank_step_registers(&memory,&g,A,S,entry);
    CHECK(calls==4&&log_[2].f==G(0x4929fc)&&log_[2].a[0]==FRAME&&log_[2].a[2]==0x3a98&&log_[2].r[1]==0);
    CHECK(log_[2].r[2]==0x0002e890u&&log_[2].a[1]==(0x00050000u|((0x100u+0x8000u+0xe890u)&0xffffu)));
    CHECK(r32(0,FRAME)==10&&r32(0,FRAME+4)==0x00020003u&&r32(0,FRAME+8)==0x00050000u);
    CHECK(log_[3].f==G(0x492934)&&log_[3].a[0]==FRAME&&log_[3].a[1]==A+0x26&&log_[3].r[2]==FRAME);
    CHECK(r==((uint64_t)0x77<<32|(A+0x65))&&r16(0,S+2)==0x1224&&r16(0,S+0xe)==1&&arena[0x166]==0);
}

static void test_retreat_check(void){
    es2_weapon_graph g=graph();
    memset(arena,0,sizeof(arena));w32(0,A+0x1a4,T);w32(0,A,VT);w32(0,VT+0x40,0x6666bbbbu);w32(0,T+0x1f2,CFG);
    /* Out with a type-3 target and configuration word 0: order_resume. */
    w8(A+0x99,1);w16(0,T+0x1a8,3);reset();ret[0][0]=0xabcd0000u;ret[0][1]=5;
    uint64_t r=es2_retreat_check_registers(&memory,&g,A,entry);
    CHECK(calls==1&&log_[0].f==G(0x41eb34)&&log_[0].r[0]==T&&log_[0].r[1]==CFG&&log_[0].r[2]==A&&r==((uint64_t)5<<32|0xabcd0001u));
    w16(0,T+0x1a8,2);reset();r=es2_retreat_check_registers(&memory,&g,A,entry);
    CHECK(log_[0].f==G(0x413e50)&&log_[0].a[0]==A+0x4d&&log_[0].a[1]==G(0x499800)&&log_[0].r[1]==0&&log_[0].r[2]==A);
    /* Normal: parts 0,1,4,5,6,7 with ratios; tables in the image. */
    w8(A+0x99,0);
    static const uint16_t idx[6]={0,1,4,5,6,7},w[3]={5,25,50},th[3]={60,120,180};
    for(unsigned i=0;i<6;++i)w16(0,G(0x49a328)+2*i,idx[i]);
    for(unsigned i=0;i<3;++i){w16(0,G(0x49a334)+2*i,w[i]);w16(0,G(0x49a33a)+2*i,th[i]);}
    w32(0,A+0x206,PARTS);w32(0,A+0x212,PARTS+0x40);w32(0,PARTS+0x44,PARTS+0x20);
    for(unsigned k=0;k<8;++k){w16(0,PARTS+2*k,100);w16(0,PARTS+0x20+2*k,200);}  /* ratio 128 */
    reset();ret[0][0]=0;r=es2_retreat_check_registers(&memory,&g,A,entry);
    /* score = 6*5 + 6*25 = 180 > 70; no attackers: no script. */
    CHECK(calls==1&&log_[0].f==0x6666bbbbu&&log_[0].slot==0x40&&log_[0].r[0]==0&&log_[0].r[1]==A&&log_[0].r[2]==VT);
    CHECK((uint32_t)r==(A&0xffff0000u)&&r>>32==0&&r16(0,A+0x2aa)==0x3e8);
    /* Every part above all three thresholds: EDX = the weight pointer's high word over 1. */
    for(unsigned k=0;k<8;++k)w16(0,PARTS+2*k,200);
    reset();ret[0][0]=0;r=es2_retreat_check_registers(&memory,&g,A,entry);
    CHECK(calls==1&&r>>32==((G(0x49a338)&0xffff0000u)|1u));
    /* 50 < score <= 70 with two attackers: threat_sum against +29Eh. */
    for(unsigned k=0;k<8;++k)w16(0,PARTS+2*k,(uint16_t)(k==4?100:10));
    w16(0,A+0x1a2,2);w16(0,A+0x29e,9);reset();ret[0][0]=26;ret[1][0]=10;
    r=es2_retreat_check_registers(&memory,&g,A,entry);  /* 26 + 5 + 25 = 56 */
    CHECK(calls==3&&log_[1].f==G(0x41cb44)&&log_[1].r[0]==(A&0xffff0000u)+2&&log_[1].r[1]==A&&log_[1].r[2]==FRAME+0xc);
    CHECK(log_[2].f==G(0x413e50)&&(uint16_t)r==1&&r16(0,A+0x2aa)==0x258);
    /* 30 < score <= 50, three attackers: q = sum / 3. */
    w16(0,A+0x1a2,3);reset();ret[0][0]=10;ret[1][0]=30;
    r=es2_retreat_check_registers(&memory,&g,A,entry);  /* 40, q = 10 > 9 */
    CHECK(calls==3&&log_[1].r[0]==A&&log_[1].r[1]==0&&log_[1].r[2]==A&&log_[2].r[2]==A);
    CHECK((uint16_t)r==1&&r16(0,A+0x2aa)==0x12c);
    reset();ret[0][0]=10;ret[1][0]=29;r=es2_retreat_check_registers(&memory,&g,A,entry);  /* q = 9 */
    CHECK(calls==2&&r==(uint64_t)9<<32);  /* AX = 0 over q = 9, EDX = +29Eh */
}

static void test_part_pick(void){
    es2_weapon_graph g=graph();
    memset(arena,0,sizeof(arena));w32(0,A+0x1a4,T);w32(0,T+0x20e,PARTS);
    for(unsigned k=0;k<0x20;++k)w16(0,PARTS+2*k,(uint16_t)(k%3?1:0));
    /* r = 30h (>= 28h, < 50h), not near: range 13h .. 13h + 2. */
    w32(0,A+0x202,CFG);w16(0,CFG+8,2);reset();ret[0][0]=0x12340030u;
    /* part 13h has word 1 (13h % 3 = 1); 14h: 14h % 3 = 2 -> 1 too. */
    ret[1][0]=0x20;ret[1][1]=0x55;ret[2][0]=0xff01;ret[3][0]=0x30;ret[4][0]=0x0002;
    uint64_t r=es2_part_pick_registers(&memory,&g,A,entry);
    CHECK(calls==5&&log_[0].f==G(0x492ee0)&&log_[0].a[0]==G(0x4d2599)&&log_[0].r[0]==entry[0]);
    CHECK(log_[1].f==G(0x40dbc0)&&log_[1].a[0]==A+0x206&&log_[1].a[1]==0x12340013u&&log_[1].r[2]==A+0x206);
    CHECK(log_[2].r[0]==0x20&&log_[2].r[1]==0x55&&log_[3].a[1]==0x12340014u);
    /* v1 = 1 + 20h = 21h; v2 = 2 + 30h = 32h wins. */
    CHECK(r16(0,A+0x2a2)==0x14&&(uint32_t)r==0x32u&&r>>32==0x30);
    /* Flag +0B4h: range 0..1; part 0 has word 0, nothing scored. */
    arena[0x600+0xb4]=1;reset();ret[0][0]=0x30;r=es2_part_pick_registers(&memory,&g,A,entry);
    CHECK(calls==1&&r16(0,A+0x2a2)==0xffff&&(uint32_t)r==0&&r>>32==PARTS);
}

static void test_drive_apply(void){
    es2_weapon_graph g=graph();
    memset(arena,0,sizeof(arena));
    /* Too close and facing away (|delta| >= 4000h): forward. */
    w32(0,S+0xa,5);w32(0,S+0x10,10);w32(0,S+0x14,100);w16(0,S+4,0x4000);w16(0,S+2,0x8000);
    reset();ret[0][0]=0xaa;ret[0][1]=0xbb;
    uint64_t r=es2_drive_apply_registers(&memory,&g,A,S);
    CHECK(r16(0,S+0xe)==1&&r16(0,S+4)==0x7fff&&r16(0,A+0x5a)==1&&r==((uint64_t)0xbb<<32|0xaa));
    CHECK(log_[0].f==G(0x416a04)&&log_[0].a[0]==A&&log_[0].a[1]==0xfffffe00u&&log_[0].a[2]==((A+0x5au+0x10000u)&0xffff0000u)+0x100u&&log_[0].a[3]==0);
    CHECK(log_[0].r[0]==S&&log_[0].r[1]==0xfffffe00u&&log_[0].r[2]==A);
    /* Too far, delta in front: forward; reverse when behind. */
    w32(0,S+0xa,200);w16(0,S+4,0x4001);w16(0,S+2,0x0040);reset();(void)es2_drive_apply_registers(&memory,&g,A,S);
    CHECK(r16(0,S+0xe)==0xffff&&r16(0,S+4)==0x40&&log_[0].a[1]==1&&log_[0].a[2]==((A+0x5au)&0xffff0000u)+0xff00u);
    /* In band, drive word 0: speed 0. */
    w32(0,S+0xa,50);w16(0,S+0xe,0);reset();(void)es2_drive_apply_registers(&memory,&g,A,S);
    CHECK(log_[0].a[2]==0&&r16(0,S+0xe)==0);
}

int main(void){
    test_point_offset();test_threat_sum();test_engage_done();test_flank_step();test_retreat_check();test_part_pick();test_drive_apply();
    printf("PASS: ai_helpers %u checks\n",checks);
    return 0;
}
