#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/pool_iter.h"
/* Synthetic guest: pool and nodes at 90000000h, low nodes at 00080000h
 * (below 1 MiB: the debug report), scratch at A0000000h, image base B.
 * The callees are logged with their arguments and entry registers and
 * return planned registers. */
#define OBJ UINT32_C(0x90000000)
#define LOW UINT32_C(0x00080000)
#define FRAME UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
static uint8_t arena[4096],low[256],scratch[ES2_POOL_ITER_SCRATCH];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"pool_iter line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-LOW<sizeof(low))return low+(a-LOW);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    fprintf(stderr,"pool_iter: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
typedef struct logged { uint32_t f,a[6],r[3]; unsigned n; } logged;
static logged log_[16];static unsigned calls,observed;
static uint32_t ret[16][3];
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<16);logged *l=log_+calls;l->f=f;l->n=n;memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;CHECK(observed==calls&&slot==0&&(f==0x471c18u||f==0x496796u));++observed;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,FRAME,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){calls=observed=0;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));}
#define P (OBJ+0x100)

static void test_next(void){
    es2_weapon_graph g=graph();reset();
    w32(0,P+0xe,OBJ+0x400);
    uint64_t r=es2_pool_next_registers(&memory,&g,P,0,entry);
    CHECK((uint32_t)r==OBJ+0x408&&r>>32==0&&calls==0);
    w32(0,P+0xe,0);r=es2_pool_next_registers(&memory,&g,P,0,entry);
    CHECK(r==0);
    /* O != 0: the next link of N = O-8; EDX = N. */
    w32(0,OBJ+0x400,OBJ+0x800);r=es2_pool_next_registers(&memory,&g,P,OBJ+0x408,entry);
    CHECK((uint32_t)r==OBJ+0x808&&r>>32==OBJ+0x400&&calls==0);
    w32(0,OBJ+0x400,0);r=es2_pool_next_registers(&memory,&g,P,OBJ+0x408,entry);
    CHECK((uint32_t)r==0&&r>>32==OBJ+0x400);
    w32(0,OBJ+0x400,0xfffffff8u);r=es2_pool_next_registers(&memory,&g,P,OBJ+0x408,entry);
    CHECK((uint32_t)r==0&&r>>32==OBJ+0x400);
}

static void test_report(void){
    es2_weapon_graph g=graph();reset();
    w16(0,P,0xfffe);w16(0,P+2,7);w16(0,P+4,0x8000);w32(0,P+6,0xdeadbeefu);
    w32(0,LOW+0x10,LOW+0x40);ret[0][0]=0x35;ret[0][1]=0x7777u;ret[0][2]=0x9999u;
    uint64_t r=es2_pool_next_registers(&memory,&g,P,LOW+0x18,entry);
    CHECK(calls==1&&observed==1&&log_[0].f==G(0x496796)&&log_[0].n==6);
    CHECK(log_[0].a[0]==FRAME&&log_[0].a[1]==G(0x4a0cd8)&&log_[0].a[2]==0xdeadbeefu&&log_[0].a[3]==0xfffffffeu&&
          log_[0].a[4]==7&&log_[0].a[5]==0xffff8000u);
    CHECK(log_[0].r[0]==FRAME&&log_[0].r[1]==7&&log_[0].r[2]==0xfffffffeu);
    CHECK((uint32_t)r==LOW+0x48&&r>>32==0x7777u);
    /* Object 8..0FFFFFh+8 reports; object 1..7 wraps above and does not. */
    reset();w32(0,LOW,0);r=es2_pool_next_registers(&memory,&g,P,LOW+8,entry);
    CHECK(calls==1&&(uint32_t)r==0&&r>>32==0);
}

static void test_prev(void){
    es2_weapon_graph g=graph();reset();
    w32(0,OBJ+0x404,OBJ+0x600);
    uint64_t r=es2_pool_prev_registers(&memory,&g,P,OBJ+0x408,entry);
    CHECK((uint32_t)r==OBJ+0x608&&r>>32==entry[1]&&calls==0);
    w32(0,OBJ+0x404,0);r=es2_pool_prev_registers(&memory,&g,P,OBJ+0x408,entry);
    CHECK((uint32_t)r==0&&r>>32==entry[1]);
    /* O = 0: three steps, then 0; registers flow from step to step. */
    reset();
    ret[0][0]=OBJ+0x108;ret[0][1]=1;ret[0][2]=2;
    ret[1][0]=OBJ+0x208;ret[1][1]=3;ret[1][2]=4;
    ret[2][0]=OBJ+0x308;ret[2][1]=5;ret[2][2]=6;
    ret[3][0]=0;ret[3][1]=0xabcdu;ret[3][2]=8;
    r=es2_pool_prev_registers(&memory,&g,P,0,entry);
    CHECK(calls==4&&observed==4&&(uint32_t)r==OBJ+0x308&&r>>32==0xabcdu);
    CHECK(log_[0].f==G(0x471c18)&&log_[0].n==2&&log_[0].a[0]==P&&log_[0].a[1]==0&&
          log_[0].r[0]==entry[0]&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].a[1]==OBJ+0x108&&log_[1].r[0]==OBJ+0x108&&log_[1].r[1]==1&&log_[1].r[2]==2);
    CHECK(log_[3].a[1]==OBJ+0x308&&log_[3].r[1]==5&&log_[3].r[2]==6);
    /* Empty list: one step. */
    reset();ret[0][1]=0;r=es2_pool_prev_registers(&memory,&g,P,0,entry);
    CHECK(calls==1&&r==0);
}

int main(void){
    test_next();test_report();test_prev();
    printf("pool_iter: %u checks passed\n",checks);
    return 0;
}
