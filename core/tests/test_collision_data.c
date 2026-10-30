#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/collision_data.h"
/* Synthetic guest: an arena at 90000000h and a synthetic image base I whose
 * globals 0x4987de (the slot counter) and 0x4a9824 (the slot table) live in
 * a small block. Game calls are logged and answered by a stub that plays a
 * stream: each read (the stream's slot +18h) copies the next bytes of a
 * scripted byte source into its buffer. Expectations worked out by hand from
 * the reference listing (mission_load.md, section "collision
 * data"). */
#define ARENA UINT32_C(0x90000000)
#define I UINT32_C(0x10000000)
#define G1 (I+0x98700u)
#define G2 (I+0xa9800u)
#define VA(x) (I+(uint32_t)(x)-0x400000u)
static uint8_t arena[0x2000],globals1[0x100],globals2[0x100];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"collision_data line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-ARENA<sizeof(arena))return arena+(a-ARENA);
    if(a-G1<sizeof(globals1))return globals1+(a-G1);
    if(a-G2<sizeof(globals2))return globals2+(a-G2);
    fprintf(stderr,"collision_data: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static const es2_guest_memory *m=&memory;
/* The stream S with its vtable VT; slot +18h holds READ (a marker). */
#define S (ARENA+0x1f00u)
#define VT (ARENA+0x1f80u)
#define READ UINT32_C(0x77770018)
typedef struct call_record { uint32_t target,args[6],regs[3];unsigned n; } call_record;
static call_record log_[32];static unsigned calls;
static uint8_t source[64];static unsigned source_at;
static uint32_t answer_eax[32];
static void stub(void *context,uint32_t target,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)context;CHECK(calls<32);call_record *v=log_+calls;
    v->target=target;v->n=n;memcpy(v->args,a,4*n);v->regs[0]=r->eax;v->regs[1]=r->edx;v->regs[2]=r->ecx;
    if(target==READ&&a[1]<=sizeof(source)-source_at){memcpy(host(0,a[2]),source+source_at,a[1]);source_at+=a[1];}
    r->eax=answer_eax[calls]?answer_eax[calls]:0x11110001u+calls;r->edx=0x22220000u+calls;r->ecx=0x33330000u+calls;
    ++calls;
}
static uint32_t observed[32][2];static unsigned observations;
static void observe(void *context,uint32_t function,unsigned slot){(void)context;observed[observations][0]=function;observed[observations][1]=slot;++observations;}
static es2_weapon_graph graph={I,0,0,0,0,stub,NULL,observe,NULL};
static const uint32_t entry[3]={0xabcd1234u,0x5678ef01u,0x0badf00du};
#define EAX(p) ((uint32_t)(p))
#define EDX(p) ((uint32_t)((p)>>32))
static void reset(void){
    memset(arena,0x5a,sizeof(arena));calls=0;observations=0;source_at=0;memset(answer_eax,0,sizeof(answer_eax));
    memset(source,0,sizeof(source));
    w32(0,S,VT);w32(0,S+4,0);w32(0,VT+0x18,READ);
}
static void radius(void){
    const uint32_t V=ARENA+0x100,P=ARENA+0x200,O=V+8;uint32_t ecx=0;
    reset();
    /* two spheres: (100, -50, 20, r 30) and (-40, 10, 200, r 10): maxima
     * 130/20/210, minima -50/-80/-10, centre 40/-30/100, deltas 90/50/110 */
    w16(0,V,2);w32(0,V+2,P);
    const int16_t s[8]={100,-50,20,30,-40,10,200,10};
    for(unsigned i=0;i<8;++i)w16(0,P+2*i,(uint16_t)s[i]);
    uint64_t p=es2_collision_radius_registers(m,&graph,V,O,entry,&ecx);
    CHECK(r16(0,O)==40&&r16(0,O+2)==0xffe2&&r16(0,O+4)==100&&r16(0,O+6)==0x5a5a);
    CHECK(calls==1&&log_[0].target==VA(0x47de72)&&log_[0].n==3&&log_[0].args[0]==90&&log_[0].args[1]==50&&log_[0].args[2]==110);
    CHECK(log_[0].regs[0]==(O&0xffff0000u|0xffe2u)&&log_[0].regs[1]==(O&0xffff0000u|100u)&&log_[0].regs[2]==(O&0xffff0000u|40u));
    CHECK(EAX(p)==0x11110001u&&EDX(p)==0x22220000u&&ecx==0x33330000u&&observations==1&&observed[0][0]==0x47de72u&&!observed[0][1]);
    /* no sphere: the opposite extrema give the centre 0 and deltas -10000 */
    reset();w16(0,V,0);w32(0,V+2,P);
    p=es2_collision_radius_registers(m,&graph,V,O,entry,&ecx);
    CHECK(r16(0,O)==0&&r16(0,O+2)==0&&r16(0,O+4)==0&&log_[0].args[0]==0xffffd8f0u&&log_[0].args[2]==0xffffd8f0u);
    /* 16-bit sums with signed compares: x 32000 + r 1000 wraps to -32536,
     * below the maximum -10000, which stays; x - r = 31000 is above the
     * minimum 10000, which stays; y -32000 + 1000 = -31000 stays below
     * -10000 and -32000 - 1000 wraps to 32536, above 10000; z gives 1000
     * and -1000. Centre 0/0/0, deltas -10000/-10000/1000. */
    reset();w16(0,V,1);w32(0,V+2,P);w16(0,P,32000);w16(0,P+2,(uint16_t)-32000);w16(0,P+4,0);w16(0,P+6,1000);
    p=es2_collision_radius_registers(m,&graph,V,O,entry,&ecx);
    CHECK(r16(0,O)==0&&r16(0,O+2)==0&&r16(0,O+4)==0);
    CHECK(log_[0].args[0]==0xffffd8f0u&&log_[0].args[1]==0xffffd8f0u&&log_[0].args[2]==1000u);
    /* the sum min + max wraps before the SAR: x 30000 and 20000 (radius 0)
     * leave the minimum 10000 and give the maximum 30000; the centre word
     * 10000 + 30000 = 40000 is -25536, SAR 1 = -12768; the delta 30000 +
     * 12768 = 42768 is the word -22768 */
    reset();w16(0,V,2);w32(0,V+2,P);
    w16(0,P,30000);w16(0,P+2,0);w16(0,P+4,0);w16(0,P+6,0);w16(0,P+8,20000);w16(0,P+10,0);w16(0,P+12,0);w16(0,P+14,0);
    p=es2_collision_radius_registers(m,&graph,V,O,entry,&ecx);
    CHECK(r16(0,O)==(uint16_t)-12768&&log_[0].args[0]==(uint32_t)(int16_t)(uint16_t)(30000+12768));
    (void)p;
}
static void bounds(void){
    const uint32_t G=ARENA+0x100,V=ARENA+0x200;uint32_t ecx=0;
    reset();w16(0,G+4,2);w32(0,G+6,V);answer_eax[0]=0x12340155u;answer_eax[1]=0x0000beefu;
    uint64_t p=es2_collision_bounds_registers(m,&graph,G,entry,&ecx);
    CHECK(calls==2&&log_[0].target==VA(0x40c5d0)&&log_[0].args[0]==V&&log_[0].args[1]==V+8&&log_[0].regs[0]==V+8&&
          log_[0].regs[1]==entry[1]&&log_[0].regs[2]==entry[2]);
    CHECK(log_[1].args[0]==V+16&&log_[1].regs[0]==V+24&&log_[1].regs[1]==0x22220000u&&log_[1].regs[2]==0x33330000u);
    CHECK(r16(0,V+0xe)==0x0155&&r16(0,V+0x1e)==0xbeef&&EAX(p)==0xbeefu&&EDX(p)==0x22220001u&&ecx==0x33330001u);
    /* a negative count: nothing, the entry registers */
    reset();w16(0,G+4,0xffff);w32(0,G+6,V);
    p=es2_collision_bounds_registers(m,&graph,G,entry,&ecx);
    CHECK(calls==0&&EAX(p)==entry[0]&&EDX(p)==entry[1]&&ecx==entry[2]);
}
static void points(void){
    const uint32_t V=ARENA+0x100,P=ARENA+0x400;uint32_t ecx=0;
    /* count 3: new[](0, 8, 3, 1, 40c8be), then 24 bytes */
    reset();source[0]=3;source[1]=0;for(unsigned i=0;i<24;++i)source[2+i]=(uint8_t)(i+1);
    answer_eax[0]=0x00000101u;answer_eax[1]=P;answer_eax[2]=0x123456ffu;
    uint64_t p=es2_collision_points_registers(m,&graph,V,S,entry,&ecx);
    CHECK(calls==3&&log_[0].target==READ&&log_[0].args[0]==S&&log_[0].args[1]==2&&log_[0].args[2]==V&&
          log_[0].regs[0]==S&&log_[0].regs[1]==VT&&log_[0].regs[2]==entry[2]);
    CHECK(log_[1].target==VA(0x49667c)&&log_[1].n==5&&log_[1].args[0]==0&&log_[1].args[1]==8&&log_[1].args[2]==3&&
          log_[1].args[3]==1&&log_[1].args[4]==VA(0x40c8be)&&log_[1].regs[0]==3&&log_[1].regs[1]==0x22220000u);
    CHECK(log_[2].target==READ&&log_[2].args[1]==24&&log_[2].args[2]==P&&log_[2].regs[0]==24&&log_[2].regs[1]==S&&log_[2].regs[2]==VT);
    CHECK(r32(0,V+2)==P&&r16(0,P)==0x0201&&EAX(p)==0xffu&&EDX(p)==0x22220002u&&ecx==0x33330002u);
    CHECK(observations==3&&observed[0][0]==READ&&observed[0][1]==0x18u&&observed[1][0]==0x49667cu);
    /* count 1000h: 8 x count is 8000h as a word, the size its sign extension */
    reset();source[0]=0;source[1]=0x10;answer_eax[0]=1;answer_eax[1]=P;
    p=es2_collision_points_registers(m,&graph,V,S,entry,&ecx);
    CHECK(calls==3&&log_[1].args[2]==0x1000u&&log_[2].args[1]==0xffff8000u&&log_[2].regs[0]==0xffff8000u);
    /* count 2000h: the word is 0, no allocation; EAX the read's byte */
    reset();source[0]=0;source[1]=0x20;answer_eax[0]=0x3301u;
    p=es2_collision_points_registers(m,&graph,V,S,entry,&ecx);
    CHECK(calls==1&&EAX(p)==1&&EDX(p)==0x22220000u&&ecx==0x33330000u);
    /* a failed read (low byte 0): nothing else */
    reset();source[0]=5;answer_eax[0]=0x100u;
    p=es2_collision_points_registers(m,&graph,V,S,entry,&ecx);
    CHECK(calls==1&&EAX(p)==0);
}
static void volume(void){
    const uint32_t V=ARENA+0x100;uint32_t ecx=0;
    reset();source[0]=7;source[1]=0;answer_eax[0]=0x8801u;
    uint64_t p=es2_collision_volume_registers(m,&graph,V,S,entry,&ecx);
    CHECK(calls==2&&log_[0].target==READ&&log_[0].args[2]==V+6&&log_[0].regs[0]==S&&log_[0].regs[1]==VT&&log_[0].regs[2]==entry[2]);
    CHECK(log_[1].target==VA(0x40c7c4)&&log_[1].args[0]==V&&log_[1].args[1]==S&&log_[1].regs[0]==S&&log_[1].regs[1]==0x22220000u&&
          log_[1].regs[2]==0x33330000u);
    CHECK(r16(0,V+6)==7&&EAX(p)==1&&EDX(p)==0x22220001u&&ecx==0x33330001u);
    reset();answer_eax[0]=0x8800u;
    p=es2_collision_volume_registers(m,&graph,V,S,entry,&ecx);
    CHECK(calls==1&&EAX(p)==0&&EDX(p)==0x22220000u);
}
static void group_entry(void){
    const uint32_t G=ARENA+0x100,V=ARENA+0x400;uint32_t ecx=0;
    /* part 5, two volumes */
    reset();source[0]=5;source[2]=2;answer_eax[2]=V;answer_eax[4]=0xabcd0000u;
    uint64_t p=es2_collision_entry_registers(m,&graph,G,S,entry,&ecx);
    CHECK(calls==5&&log_[0].args[2]==G&&log_[0].regs[0]==G&&log_[0].regs[1]==S&&log_[0].regs[2]==VT);
    CHECK(log_[1].args[2]==G+4&&log_[1].regs[0]==G+4&&log_[1].regs[1]==S&&log_[1].regs[2]==VT);
    CHECK(log_[2].target==VA(0x49667c)&&log_[2].args[1]==0x10&&log_[2].args[2]==2&&log_[2].args[4]==VA(0x40cf04)&&log_[2].regs[0]==2);
    CHECK(log_[3].target==VA(0x40cc14)&&log_[3].args[0]==V&&log_[3].regs[0]==S&&log_[4].args[0]==V+16);
    CHECK(r16(0,G)==5&&r16(0,G+4)==2&&r32(0,G+6)==V&&EAX(p)==0xabcd0001u&&EDX(p)==0x22220004u&&ecx==0x33330004u);
    /* no volume: AX = 1 over the second read's EAX */
    reset();answer_eax[1]=0x55550000u;
    p=es2_collision_entry_registers(m,&graph,G,S,entry,&ecx);
    CHECK(calls==2&&EAX(p)==0x55550001u);
}
static void read_groups(void){
    const uint32_t D=ARENA+0x100,G=ARENA+0x400;uint32_t ecx=0;
    /* a nonzero status asserts first; two groups */
    reset();w32(0,S+4,3);source[0]=2;answer_eax[2]=G;
    uint64_t p=es2_collision_read_registers(m,&graph,D,S,entry,&ecx);
    CHECK(calls==7&&log_[0].target==VA(0x479c78)&&log_[0].n==6&&log_[0].args[0]==VA(0x6c56c8)&&log_[0].args[1]==5&&
          log_[0].args[2]==0&&log_[0].args[3]==VA(0x498804)&&log_[0].args[4]==0xee&&log_[0].args[5]==VA(0x498810));
    CHECK(log_[0].regs[0]==3&&log_[0].regs[1]==0&&log_[0].regs[2]==entry[2]);
    CHECK(log_[1].target==READ&&log_[1].args[2]==D&&log_[1].regs[0]==D&&log_[1].regs[1]==S&&log_[1].regs[2]==VT);
    CHECK(log_[2].target==VA(0x4774f0)&&log_[2].args[0]==20&&log_[2].regs[0]==2&&log_[2].regs[1]==20&&log_[2].regs[2]==0x33330001u);
    CHECK(log_[3].target==VA(0x40cc50)&&log_[3].args[0]==G&&log_[3].args[1]==S&&log_[3].regs[0]==S&&log_[3].regs[2]==20);
    CHECK(log_[4].target==VA(0x40ccc8)&&log_[4].args[0]==G&&log_[4].regs[0]==0x11110004u);
    CHECK(log_[5].args[0]==G+10&&log_[5].regs[2]==G&&log_[6].args[0]==G+10);
    CHECK(r32(0,D+2)==G&&EAX(p)==0x11110007u&&EDX(p)==0x22220006u&&ecx==G+10);
    /* a negative count: new(-10), no group; ECX the popped size */
    reset();source[0]=0xff;source[1]=0xff;answer_eax[1]=G;
    p=es2_collision_read_registers(m,&graph,D,S,entry,&ecx);
    CHECK(calls==2&&log_[1].args[0]==0xfffffff6u&&EAX(p)==G&&ecx==0xfffffff6u);
    /* count 0: the read's registers */
    reset();
    p=es2_collision_read_registers(m,&graph,D,S,entry,&ecx);
    CHECK(calls==1&&EAX(p)==0x11110001u&&EDX(p)==0x22220000u&&ecx==0x33330000u);
}
static void load(void){
    /* the scratch (ES2_COLLISION_LOAD_SCRATCH bytes) lives in the arena */
    const uint32_t F=ARENA+0x1000,f=F+ES2_COLLISION_LOAD_FRAME,STREAM=f-0x30,TABLE=ARENA+0x600,NAME=ARENA+0x700;uint32_t ecx=0;
    es2_weapon_graph g=graph;g.scratch=F;
    reset();w16(0,G1+0xde,4);w32(0,G2+0x24,TABLE);w32(0,f-0x50,0xfeedf00du);w32(0,STREAM+4,0);
    uint64_t p=es2_collision_load_registers(m,&g,NAME,entry,&ecx);
    CHECK(calls==4&&log_[0].target==VA(0x49630c)&&log_[0].n==0&&log_[0].regs[0]==VA(0x4987f0)&&log_[0].regs[1]==entry[1]&&
          log_[0].regs[2]==entry[2]);
    CHECK(log_[1].target==VA(0x472f14)&&log_[1].args[0]==STREAM&&log_[1].args[1]==NAME&&log_[1].regs[0]==0x11110001u&&
          log_[1].regs[1]==STREAM&&log_[1].regs[2]==0x33330000u);
    CHECK(log_[2].target==VA(0x40ccf8)&&log_[2].args[0]==TABLE+24&&log_[2].args[1]==STREAM&&log_[2].regs[0]==STREAM&&
          log_[2].regs[1]==0x22220001u&&log_[2].regs[2]==0x33330004u);
    CHECK(log_[3].target==VA(0x472f64)&&log_[3].n==2&&log_[3].args[0]==STREAM&&log_[3].args[1]==2&&log_[3].regs[0]==TABLE+24&&
          log_[3].regs[1]==STREAM&&log_[3].regs[2]==0x33330002u);
    CHECK(r16(0,f-0x40)==8&&r16(0,G1+0xde)==5&&EAX(p)==TABLE+24&&EDX(p)==0xfeedf00du&&ecx==0x33330003u);
    /* a failed open asserts with the name; a negative counter */
    /* (472f14 is a stub: the status dword stays what the test wrote) */
    reset();w16(0,G1+0xde,0xffff);w32(0,G2+0x24,TABLE);w32(0,STREAM+4,2);
    p=es2_collision_load_registers(m,&g,NAME,entry,&ecx);
    CHECK(calls==5&&log_[2].target==VA(0x479c78)&&log_[2].args[0]==VA(0x6c56c8)&&log_[2].args[2]==0&&log_[2].args[3]==VA(0x498824)&&
          log_[2].args[4]==0x101&&log_[2].args[5]==NAME&&log_[2].regs[0]==2&&log_[2].regs[1]==0&&log_[2].regs[2]==0x33330001u);
    CHECK(log_[3].args[0]==TABLE-6&&log_[3].regs[2]==0x3333ffffu&&log_[3].regs[1]==0x22220002u&&r16(0,G1+0xde)==0&&EAX(p)==TABLE-6);
}
static void scan(void){
    const uint32_t T=ARENA+0x100,G=ARENA+0x400,A=ARENA+0x500,B=ARENA+0x600;uint32_t ecx=0;
    /* three groups, the second matches */
    reset();w16(0,T,3);w32(0,T+2,G);answer_eax[0]=0x7fff0000u;answer_eax[1]=0x00000001u;answer_eax[2]=0x44440000u;
    uint64_t p=es2_collision_scan_registers(m,&graph,T,A,B,0xdead0001u,entry,&ecx);
    CHECK(calls==3&&log_[0].target==VA(0x40ce78)&&log_[0].args[0]==G&&log_[0].args[1]==A&&log_[0].args[2]==B&&log_[0].args[3]==1&&
          log_[0].regs[0]==1&&log_[0].regs[1]==B&&log_[0].regs[2]==A);
    CHECK(log_[1].args[0]==G+10&&log_[2].target==VA(0x40ccc8)&&log_[2].args[0]==G+10&&log_[2].regs[0]==1&&log_[2].regs[1]==0x22220001u);
    CHECK(EAX(p)==0x44440000u&&EDX(p)==0x22220002u&&ecx==G+10);
    /* no match: EAX = the count */
    reset();w16(0,T,2);w32(0,T+2,G);answer_eax[0]=0x10000u;answer_eax[1]=0x20000u;
    p=es2_collision_scan_registers(m,&graph,T,A,B,1,entry,&ecx);
    CHECK(calls==2&&EAX(p)==2&&EDX(p)==0x22220001u&&ecx==0x33330001u);
    reset();w16(0,T,0xfffe);w32(0,T+2,G);
    p=es2_collision_scan_registers(m,&graph,T,A,B,1,entry,&ecx);
    CHECK(calls==0&&EAX(p)==0xfffffffeu&&EDX(p)==entry[1]&&ecx==entry[2]);
}
static void match(void){
    const uint32_t G=ARENA+0x100,V=ARENA+0x400,A=ARENA+0x500,B=ARENA+0x600;uint32_t ecx=0;
    reset();w16(0,G+4,3);w32(0,G+6,V);answer_eax[0]=0xffff0000u;answer_eax[1]=0x00000001u;
    uint64_t p=es2_collision_match_registers(m,&graph,G,A,B,0x12340002u,entry,&ecx);
    CHECK(calls==2&&log_[0].target==VA(0x40ce28)&&log_[0].args[0]==V&&log_[0].args[3]==2&&log_[0].regs[0]==2&&log_[0].regs[1]==B&&
          log_[0].regs[2]==A&&log_[1].args[0]==V+16);
    CHECK(EAX(p)==1&&EDX(p)==3&&ecx==0x33330001u);
    reset();w16(0,G+4,1);w32(0,G+6,V);answer_eax[0]=0x10000u;
    p=es2_collision_match_registers(m,&graph,G,A,B,2,entry,&ecx);
    CHECK(calls==1&&EAX(p)==0&&EDX(p)==1);
    reset();w16(0,G+4,0x8000);w32(0,G+6,V);
    p=es2_collision_match_registers(m,&graph,G,A,B,2,entry,&ecx);
    CHECK(calls==0&&EAX(p)==0&&EDX(p)==0xffff8000u&&ecx==entry[2]);
}
int main(void){
    radius();bounds();points();volume();group_entry();read_groups();load();scan();match();
    printf("collision_data: %u checks passed\n",checks);
    return 0;
}
