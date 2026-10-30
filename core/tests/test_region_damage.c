#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/region_damage.h"
/* Synthetic guest: data at 90000000h, scratch at A0000000h, image base B.
 * The callees (0x40ca58, rng_bounded, 0x40d3ec) are logged with their
 * arguments and entry registers and return planned registers. */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
static uint8_t arena[4096],scratch[16];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"region_damage line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    fprintf(stderr,"region_damage: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[3],r[3],word; unsigned n; } logged;
static logged log_[32];static unsigned calls,observed;
static uint32_t ret[32][3];
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<32);logged *l=log_+calls;l->f=f;l->n=n;memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    /* region_absorb: record the damage word it would read, then apply a
     * planned new damage word (ret[][2] high half) to the slot. */
    if(f==G(0x40d3ec)){l->word=r16(0,a[2]);if(ret[calls][2]>>16)w16(0,a[2],(uint16_t)(ret[calls][2]>>16));}
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2]&0xffffu;++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;CHECK(observed==calls&&slot==0&&(f==0x40ca58u||f==0x492f24u||f==0x40d3ecu));++observed;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,FRAME,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){calls=observed=0;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));memset(arena,0,sizeof(arena));}

static void test_absorb(void){
    const uint32_t v=OBJ+0x10,d=OBJ+0x20;uint32_t ecx=0;
    /* Negative region: untouched, ECX keeps the entry high word. */
    reset();w16(0,v,0xfffe);w16(0,d,500);
    uint64_t r=es2_region_absorb_registers(&memory,v,0xabcd0100u,d,0x77778888u,&ecx);
    CHECK((uint32_t)r==1&&r>>32==d&&ecx==0x7777fffeu&&r16(0,v)==0xfffe&&r16(0,d)==500);
    /* Below the limit: v += d. */
    w16(0,v,100);w16(0,d,50);
    r=es2_region_absorb_registers(&memory,v,0xabcd00c8u,d,0,&ecx);
    CHECK((uint32_t)r==0&&r>>32==(d&0xffff0000u)+50u&&ecx==150&&r16(0,v)==150&&r16(0,d)==50);
    /* At the limit: overflow 0 in d, region -1. */
    w16(0,v,100);w16(0,d,100);
    r=es2_region_absorb_registers(&memory,v,200,d,0,&ecx);
    CHECK((uint32_t)r==1&&r>>32==d&&ecx==0&&r16(0,v)==0xffff&&r16(0,d)==0);
    /* Negative damage and limit; the sum is a 32-bit signed comparison. */
    w16(0,v,0x7fff);w16(0,d,0x7fff);
    r=es2_region_absorb_registers(&memory,v,0x8000u,d,0,&ecx);
    CHECK((uint32_t)r==1&&ecx==0x7ffeu&&r16(0,v)==0xffff&&r16(0,d)==0x7ffe);
    w16(0,v,5);w16(0,d,0xfff0);
    r=es2_region_absorb_registers(&memory,v,0,d,0,&ecx);
    CHECK((uint32_t)r==1&&ecx==0xfffffff5u&&r16(0,v)==0xfff5&&r>>32==(d&0xffff0000u)+0xfff0u);
    /* Aliased pointers: the damage store comes first, the region -1 last. */
    w16(0,v,30);
    r=es2_region_absorb_registers(&memory,v,10,v,0,&ecx);
    CHECK((uint32_t)r==1&&r16(0,v)==0xffff&&ecx==50);
    CHECK(es2_region_absorb_registers(&memory,v,10,d,0,NULL)>>32==d);
}

static void test_entry(void){
    const uint32_t n=OBJ+0x40,out=OBJ+0x30,e=OBJ+0x100;uint32_t ecx=0;
    reset();w16(0,n,0x1234);w16(0,n+4,3);w32(0,n+6,e);
    w16(0,e+6,7);w16(0,e+0x16,9);w16(0,e+0x26,9);w16(0,out,0xeeee);
    uint64_t r=es2_region_entry_registers(&memory,n,0xffff0009u,out,&ecx);
    CHECK((uint32_t)r==e+0x10&&r>>32==1&&ecx==(n&0xffff0000u)+0x1234u&&r16(0,out)==0x1234);
    w16(0,out,0xeeee);
    r=es2_region_entry_registers(&memory,n,5,out,&ecx);
    CHECK(r==(uint64_t)3<<32&&ecx==n&&r16(0,out)==0xeeee);
    w16(0,n+4,0x8000);r=es2_region_entry_registers(&memory,n,9,out,&ecx);
    CHECK(r==0&&ecx==n);
}

static void test_find(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    const uint32_t s=OBJ+0x200,recs=OBJ+0x300,out=OBJ+0x30;
    reset();w16(0,s,3);w32(0,s+2,recs);
    ret[0][0]=0;ret[0][1]=0x1111;ret[0][2]=0x2222;
    ret[1][0]=OBJ+0x500;ret[1][1]=0x3333;ret[1][2]=0x4444;
    uint64_t r=es2_region_find_registers(&memory,&g,s,0xdead0042u,out,entry,&ecx);
    CHECK(calls==2&&observed==2&&(uint32_t)r==OBJ+0x500&&r>>32==0x3333&&ecx==0x4444);
    CHECK(log_[0].f==G(0x40ca58)&&log_[0].n==3&&log_[0].a[0]==recs&&log_[0].a[1]==((s&0xffff0000u)|0x42u)&&log_[0].a[2]==out);
    CHECK(log_[0].r[0]==out&&log_[0].r[1]==log_[0].a[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].a[0]==recs+10&&log_[1].r[2]==0x2222);
    /* Not found: EDX = S, ECX = the last call's. */
    reset();w16(0,s,2);w32(0,s+2,recs);ret[0][2]=0x5555;ret[1][2]=0x6666;
    r=es2_region_find_registers(&memory,&g,s,1,out,entry,&ecx);
    CHECK(calls==2&&(uint32_t)r==0&&r>>32==s&&ecx==0x6666);
    reset();w16(0,s,0xffff);
    r=es2_region_find_registers(&memory,&g,s,1,out,entry,&ecx);
    CHECK(calls==0&&r==(uint64_t)s<<32&&ecx==entry[2]);
}

static void test_spread(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    const uint32_t rec=OBJ+0x400,v=OBJ+0x600,t=OBJ+0x700,e=OBJ+0x480;
    /* Three entries: weights 5 (region 0, live), 7 (region 1, dead),
     * 3 (region 2, live): total 8. */
    reset();w16(0,rec+6,3);w32(0,rec+8,e);
    w16(0,e,5);w16(0,e+2,0);w16(0,e+4,7);w16(0,e+6,1);w16(0,e+8,3);w16(0,e+10,2);
    w16(0,v,10);w16(0,v+2,0xffff);w16(0,v+4,20);
    w16(0,t,100);w16(0,t+2,200);w16(0,t+4,300);
    /* Round 1: draw 6 -> region 2 (6-5=1, 1-3<0); absorb breaks it (EAX
     * 1) and leaves damage 0x55. Round 2: total 5, draw 0 -> region 0;
     * absorb returns 0 with EDX 0x9999. */
    ret[0][0]=6;ret[0][1]=0xaaaa;ret[0][2]=0xbbbb;
    ret[1][0]=0x10001u;ret[1][1]=0x1234;ret[1][2]=0x00550000u;
    ret[2][0]=0;ret[3][0]=0x70000u;ret[3][1]=0x9999;
    uint64_t r=es2_region_spread_registers(&memory,&g,rec,v,0xcafe0123u,t,entry,&ecx);
    CHECK(calls==4&&observed==4);
    CHECK(log_[0].f==G(0x492f24)&&log_[0].n==2&&log_[0].a[0]==G(0x4d2599)&&log_[0].a[1]==(((e+12)&0xffff0000u)|8u));
    CHECK(log_[0].r[0]==log_[0].a[1]&&log_[0].r[1]==3&&log_[0].r[2]==0);
    CHECK(log_[1].f==G(0x40d3ec)&&log_[1].a[0]==v+4&&log_[1].a[1]==300u&&log_[1].a[2]==FRAME&&log_[1].word==0x123);
    CHECK(log_[1].r[0]==300u&&log_[1].r[1]==FRAME&&log_[1].r[2]==t);
    /* Round 2 still sees region 2 live (the stub did not break it): total 8. */
    CHECK(log_[2].a[1]==(((e+12)&0xffff0000u)|8u)&&log_[2].r[2]==0);
    CHECK(log_[3].a[0]==v&&log_[3].a[1]==100u&&log_[3].word==0x55);
    CHECK((uint32_t)r==0&&r>>32==0x9999&&ecx==0x00086666u&&r32(0,FRAME)==0xcafe0055u);
    /* No weighted live region: EAX 1, EDX the step count, ECX 0. */
    reset();w16(0,rec+6,2);w32(0,rec+8,e);w16(0,e,0);w16(0,e+2,0);w16(0,e+4,4);w16(0,e+6,1);w16(0,v+2,0x8000);
    r=es2_region_spread_registers(&memory,&g,rec,v,1,t,entry,&ecx);
    CHECK(calls==0&&(uint32_t)r==1&&r>>32==2&&ecx==0x6666u);
    /* Fall-through: weight -1 (total FFFFh), draw 0 never goes negative;
     * the entry past the end gives the limit index, EBX stays the last
     * walked region. */
    reset();w16(0,rec+6,1);w32(0,rec+8,e);w16(0,e,0xffff);w16(0,e+2,1);w16(0,e+4,0);w16(0,e+6,2);
    w16(0,t+4,0x4321);ret[0][0]=0;ret[1][0]=0;ret[1][1]=0x77;
    r=es2_region_spread_registers(&memory,&g,rec,v,0,t,entry,&ecx);
    CHECK(calls==2&&log_[1].a[0]==v+2&&log_[1].a[1]==0x4321u&&(uint32_t)r==0&&r>>32==0x77);
}

int main(void){
    test_absorb();test_entry();test_find();test_spread();
    printf("region_damage: %u checks passed\n",checks);
    return 0;
}
