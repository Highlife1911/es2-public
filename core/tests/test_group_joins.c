#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/group_joins.h"
/* Synthetic guest: an arena at 90000000h and a synthetic image base I whose
 * globals 0x4a0be0 (terrain) and 0x4a95ac (base layouts) live in a small
 * block. Game calls are logged and answered by a stub per target.
 * Expectations worked out by hand from the reference listing
 * (mission_load.md, section "group joins and the building
 * footprint"). */
#define ARENA UINT32_C(0x90000000)
#define I UINT32_C(0x10000000)
#define GLOBALS (I+0xa0b00u)
#define GLOBALS2 (I+0xa9500u)
#define V (ARENA+0xf00u)          /* the scratch vector */
#define SLOT78 UINT32_C(0x77780000)
#define SLOT10 UINT32_C(0x77100000)
static uint8_t arena[0x1000],globals[0x100],globals2[0x100];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"group_joins line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-ARENA<sizeof(arena))return arena+(a-ARENA);
    if(a-GLOBALS<sizeof(globals))return globals+(a-GLOBALS);
    if(a-GLOBALS2<sizeof(globals2))return globals2+(a-GLOBALS2);
    fprintf(stderr,"group_joins: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static const es2_guest_memory *m=&memory;
typedef struct call_record { uint32_t target,args[4],regs[3],v[3];unsigned n; } call_record;
static call_record log_[64];static unsigned calls;
static uint32_t route_answer,height_answer,shape_answer,dist_answer;
static void stub(void *context,uint32_t target,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)context;CHECK(calls<64);call_record *c=log_+calls++;
    c->target=target;c->n=n;memcpy(c->args,a,4*n);c->regs[0]=r->eax;c->regs[1]=r->edx;c->regs[2]=r->ecx;
    for(unsigned i=0;i<3;++i)c->v[i]=r32(0,V+4*i);
    r->edx=0x22220000u+calls;r->ecx=0x33330000u+calls;
    if(target==I+0x23b0cu)r->eax=route_answer;
    else if(target==I+0x6e180u)r->eax=height_answer;
    else if(target==SLOT10)r->eax=shape_answer;
    else if(target==I+0x9288cu)r->eax=dist_answer;
    else if(target==SLOT78){w32(0,V,r32(0,V)+100u);r->eax=0x11110000u+calls;}   /* the formation offset */
    else r->eax=0x11110000u+calls;
}
static uint32_t observed[64];static unsigned observations;
static void observe(void *context,uint32_t function,unsigned slot){(void)context;observed[observations++&63]=function^slot;}
static const es2_weapon_graph graph={I,V,0,0,0,stub,NULL,observe,NULL};
static const uint32_t entry[3]={0xabcd1234u,0x5678ef01u,0x0badf00du};
#define EAX(p) ((uint32_t)(p))
#define EDX(p) ((uint32_t)((p)>>32))
static void reset(void){memset(arena,0,sizeof(arena));memset(log_,0,sizeof(log_));calls=0;observations=0;}
enum { A=ARENA+0x100, G=ARENA+0x400, VT=ARENA+0x500, TYPE=ARENA+0x600, Q=ARENA+0x700, PT=ARENA+0x740, LAY=ARENA+0x800, TER=ARENA+0x900 };
static void actor(void){
    w32(0,A,VT);w32(0,VT+0x78,SLOT78);w32(0,VT+0x10,SLOT10);w32(0,A+0x1f2,TYPE);
    w16(0,A+0xc,0x8000);w32(0,A+0x2e,0xffff8000u);w16(0,A+0x32,1);
    w16(0,G+0xa,0x1234);w32(0,GLOBALS+0xe0,TER);
}
static void mech(void){
    /* defaults, a group point: angles (0, 0, 1234h), the point copied,
     * z = height + sx(+16h) + 5000 (word +50h set), +78h adds 100 to x */
    reset();actor();w32(0,G+0x18,PT);w32(0,PT,1000);w32(0,PT+4,2000);w32(0,PT+8,3000);
    w16(0,TYPE+0x16,0xfffe);w16(0,TYPE+0x50,1);height_answer=40;
    uint32_t ecx=0;uint64_t p=es2_mech_join_registers(m,&graph,A,G,0x00050003u,entry,&ecx);
    CHECK(r32(0,A+0x45)==G&&r16(0,A+0x49)==3);
    CHECK(r16(0,A+0xc)==0&&r16(0,A+0xe)==0&&r16(0,A+0x10)==0x1234&&r16(0,A+0x32)==0);
    CHECK(calls==2&&log_[0].target==I+0x6e180u&&log_[0].args[0]==TER&&log_[0].args[1]==V);
    CHECK(log_[0].regs[0]==2000&&log_[0].regs[1]==3000&&log_[0].regs[2]==TER);   /* EAX = y, EDX = z, ECX = T */
    CHECK(log_[1].target==SLOT78&&log_[1].args[0]==A&&log_[1].args[1]==V&&log_[1].v[2]==40u-2u+5000u);
    CHECK(log_[1].regs[0]==TYPE&&log_[1].regs[1]==0xfffffffeu&&log_[1].regs[2]==VT);
    CHECK(r32(0,A+0x26)==1100&&r32(0,A+0x2a)==2000&&r32(0,A+0x2e)==5038);
    CHECK(EAX(p)==A+0x26&&EDX(p)==2000&&ecx==1100);
    CHECK(observations==2&&observed[0]==0x46e180u&&observed[1]==(SLOT78^0x78u));
    /* no group point: route_point(G+4, word [G+4]) with EDX = 0:word, ECX = G
     * (the angle branch); the point it returns, z without the 5000 */
    reset();actor();w16(0,G+4,7);route_answer=Q;w32(0,Q,-50);w32(0,Q+4,60);w32(0,Q+8,70);w16(0,TYPE+0x16,10);height_answer=5;
    p=es2_mech_join_registers(m,&graph,A,G,0,entry,&ecx);
    CHECK(calls==3&&log_[0].target==I+0x23b0cu&&log_[0].args[0]==G+4&&log_[0].args[1]==7&&log_[0].regs[2]==G);
    CHECK(log_[1].regs[0]==60&&log_[1].regs[1]==(uint32_t)-50&&log_[1].regs[2]==TER);
    CHECK(r32(0,A+0x26)==50&&r32(0,A+0x2a)==60&&r32(0,A+0x2e)==15&&EAX(p)==A+0x26&&EDX(p)==60&&ecx==50);
    /* placed already: only the group and slot stores */
    reset();actor();w16(0,A+0xc,1);w32(0,A+0x2e,0);
    p=es2_mech_join_registers(m,&graph,A,G,0xffff0009u,entry,&ecx);
    CHECK(calls==0&&EAX(p)==G&&EDX(p)==0x56780009u&&ecx==entry[2]&&r16(0,A+0x49)==9&&r16(0,A+0xc)==1);
}
static void flyer(void){
    /* defaults, a group point at z = 0: +78h, then the tail sets 5000 */
    reset();actor();w32(0,G+0x18,PT);w32(0,PT,1);w32(0,PT+4,2);w32(0,PT+8,0);
    uint32_t ecx=0;uint64_t p=es2_flyer_join_registers(m,&graph,A,G,2,entry,&ecx);
    CHECK(calls==1&&log_[0].target==SLOT78&&log_[0].regs[0]==2&&log_[0].regs[1]==0&&log_[0].regs[2]==VT);
    CHECK(r32(0,A+0x26)==101&&r32(0,A+0x2a)==2&&r32(0,A+0x2e)==5000&&EAX(p)==A+0x26&&EDX(p)==2&&ecx==101);
    /* z 0 without placement: the tail alone */
    reset();actor();w16(0,A+0xc,5);w32(0,A+0x2e,0);
    p=es2_flyer_join_registers(m,&graph,A,G,2,entry,&ecx);
    CHECK(calls==0&&r32(0,A+0x2e)==5000&&EAX(p)==G&&EDX(p)==0x56780002u);
}
static void building(void){
    /* slot 2 of group word +2 = 1: angle = sx([G+0Ah]) + layout word
     * [[L+4] + 10 x 2 - 2]; no point: route_point(G+4, ECX = G's high word
     * with the word [G+4]); snap with [L+14h] = 1: s = 20h, m = 1FFFh,
     * x' = (x & ~m) + [L+0Ch] x 20h, y' = (y & ~m) + 2000h - [L+10h] x 20h */
    reset();actor();
    w16(0,G+2,1);w16(0,G+0xa,0x100);w16(0,G+4,3);
    w32(0,GLOBALS2+0xac,LAY-28u);                 /* [4A95ACh] + 1Ch x 1 = LAY */
    w32(0,LAY+4,LAY+0x80);w16(0,LAY+0x80+18,0xfff0);
    w32(0,LAY+0x14,1);w32(0,LAY+0xc,3);w32(0,LAY+0x10,0xfffffffeu);
    route_answer=Q;w32(0,Q,0x12345);w32(0,Q+4,0x6789a);w32(0,Q+8,77);
    shape_answer=0x1234ffffu;height_answer=99;
    uint32_t ecx=0;uint64_t p=es2_building_join_registers(m,&graph,A,G,2,1,entry,&ecx);
    CHECK(r16(0,A+0x10)==0x00f0&&r16(0,A+0x49)==2);
    CHECK(calls==5&&log_[0].target==I+0x23b0cu&&log_[0].args[1]==((0xffffu<<16)|3u));   /* ECX = sx(0xfff0) */
    const uint32_t x=(0x12345u&~0x1fffu)+3u*0x20u,y=(0x6789au&~0x1fffu)+0x2000u+0x40u;
    CHECK(log_[1].target==SLOT78&&log_[1].v[0]==x&&log_[1].v[1]==y&&log_[1].v[2]==77&&log_[1].regs[2]==V&&log_[1].regs[0]==VT);
    CHECK(log_[1].regs[1]==x);                    /* EDX: x' through [EBP-0Ch] */
    CHECK(log_[2].target==SLOT10&&log_[2].n==1&&log_[2].args[0]==A&&log_[2].regs[1]==VT);
    CHECK(log_[3].target==I+0x70ed4u&&log_[3].args[0]==TER&&log_[3].args[1]==V&&log_[3].args[2]==0xffffffffu);
    CHECK(log_[3].regs[0]==V&&log_[3].regs[1]==TER&&log_[3].regs[2]==0xffffffffu&&log_[3].v[0]==x+100u);
    CHECK(log_[4].target==I+0x6e180u&&log_[4].regs[0]==TER&&log_[4].regs[2]==V&&log_[4].regs[1]==0x22220004u);
    CHECK(r32(0,A+0x26)==x+100u&&r32(0,A+0x2a)==y&&r32(0,A+0x2e)==99&&EAX(p)==A+0x26&&EDX(p)==x+100u&&ecx==y);
    /* slot 0 (no layout word), a point, no snap */
    reset();actor();w16(0,G+0xa,0x8001);w32(0,G+0x18,PT);w32(0,PT,5);w32(0,PT+4,6);w32(0,PT+8,7);shape_answer=3;
    p=es2_building_join_registers(m,&graph,A,G,0,0,entry,&ecx);
    CHECK(r16(0,A+0x10)==0x8001&&calls==4&&log_[0].target==SLOT78&&log_[0].regs[1]==5&&log_[0].regs[2]==V);
    CHECK(log_[2].args[2]==3&&r32(0,A+0x26)==105&&r32(0,A+0x2a)==6&&r32(0,A+0x2e)==99);
}
static void footprint(void){
    /* s = 4, P = (100, 50, 7), r = 20: x 5..7, y 1..4, centre (6, 3); every
     * distance 0, so each of the 12 cells marks four cells (x > 0, y > 0) */
    reset();w32(0,TER+0x108,0xffffff04u);w32(0,PT,100);w32(0,PT+4,50);w32(0,PT+8,7);dist_answer=0;
    uint32_t ecx=0;uint64_t p=es2_footprint_marks_registers(m,&graph,TER,PT,20,entry,&ecx);
    CHECK(calls==1+12*5);
    CHECK(log_[0].target==I+0x70ddcu&&log_[0].args[1]==6&&log_[0].args[2]==3&&log_[0].args[3]==1);
    CHECK(log_[0].regs[0]==0xffffff04u&&log_[0].regs[1]==6&&log_[0].regs[2]==0xffffff04u);
    CHECK(log_[1].target==I+0x9288cu&&log_[1].args[0]==V&&log_[1].args[1]==PT&&log_[1].v[0]==80&&log_[1].v[1]==16&&log_[1].v[2]==7);
    CHECK(log_[1].regs[0]==V&&log_[1].regs[1]==PT&&log_[1].regs[2]==0xffffff04u);
    CHECK(log_[2].args[1]==5&&log_[2].args[2]==1&&log_[2].regs[0]==0);
    CHECK(log_[3].args[1]==4&&log_[3].args[2]==1&&log_[3].regs[1]==4);
    CHECK(log_[4].args[1]==4&&log_[4].args[2]==0&&log_[4].regs[0]==4&&log_[4].regs[2]==0);
    CHECK(log_[5].args[1]==5&&log_[5].args[2]==0&&log_[5].regs[1]==0);
    CHECK(log_[60].args[1]==7&&log_[60].args[2]==3);
    CHECK(EAX(p)==0x11110000u+61u&&EDX(p)==0x22220000u+61u&&ecx==0x33330000u+61u);
    /* every distance above r: only the centre; x = 0 and y = 0 cells mark one */
    reset();w32(0,TER+0x108,0);w32(0,PT,0);w32(0,PT+4,0);dist_answer=1;
    p=es2_footprint_marks_registers(m,&graph,TER,PT,0,entry,&ecx);
    CHECK(calls==2&&log_[1].target==I+0x9288cu&&EAX(p)==1&&EDX(p)==0x22220002u);
    dist_answer=0;reset();p=es2_footprint_marks_registers(m,&graph,TER,PT,0,entry,&ecx);
    CHECK(calls==3&&log_[2].args[1]==0&&log_[2].args[2]==0);
    /* a negative radius: an empty box after the centre */
    reset();w32(0,PT,64);p=es2_footprint_marks_registers(m,&graph,TER,PT,0xfffffff0u,entry,&ecx);
    CHECK(calls==1&&EAX(p)==0x11110001u&&EDX(p)==0x22220001u);
}
static void cell(void){
    reset();w32(0,TER+0x100,0x12345605u);w32(0,TER+0xf0,ARENA+0xc00);
    uint32_t ecx=0;uint64_t p=es2_cell_flag_set_registers(m,TER,3,2,0x101,entry,&ecx);
    CHECK(arena[0xc00+67]==1&&EAX(p)==ARENA+0xc00&&EDX(p)==67&&ecx==0x12345601u);
    /* a shift with bits above the low five */
    w32(0,TER+0x100,0x22u);p=es2_cell_flag_set_registers(m,TER,1,1,0,entry,&ecx);
    CHECK(EDX(p)==5&&arena[0xc05]==0&&ecx==0);
}
int main(void){
    mech();flyer();building();footprint();cell();
    printf("PASS: group_joins %u checks\n",checks);
    return 0;
}
