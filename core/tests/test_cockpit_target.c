#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/cockpit_target.h"
/* Synthetic guest for the cockpit target selection. Objects live in
 * an arena at 90000000h, the globals in a small window of an image at B.
 * The bridged callees are simple stand-ins: dist_gate = |x1 - x0| of the
 * two positions, heading_point = the first position's y as a word, the
 * player's twist = word +2 of its table record, angle_ratio and
 * multiply_q10 = 0, and 433174/433250/43349C call the core again (like a
 * replaced hook). Expected values are worked out by hand from the contracts
 * in docs/reverse-engineering/hud.md. */
#define OBJ UINT32_C(0x90000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
static uint8_t arena[0x4000],globals[0x110000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"cockpit_target line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-G(0x49e000)<sizeof(globals))return globals+(a-G(0x49e000));
    fprintf(stderr,"cockpit_target: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static uint64_t pair(uint32_t eax,uint32_t edx){return (uint64_t)edx<<32|eax;}
/* Layout: cockpit H, HUD object, vtable, groups, actors A0..A5 (A0 is the
 * player), actor list, the four-entry list and the bucket blocks. */
enum { H=0x000, HUD=0x300, VT=0x400, GRP=0x480, ACT=0x800, LIST=0x2000, TOP=0x2040, BUCKETS=0x2080, ARRAYS=0x2100 };
#define AT(o) (OBJ+(uint32_t)(o))
#define ACTOR(i) AT(ACT+0x200*(i))
static unsigned hud_calls,twist_calls,radius_calls;static uint32_t hud_target,hud_flag,hud_regs[3];
static const es2_weapon_graph *graph;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;uint32_t entry[3]={r->eax,r->edx,r->ecx};uint64_t p=0;
    if(f==G(0x49288c)){int32_t d=(int32_t)(r32(0,a[1])-r32(0,a[0]));p=pair((uint32_t)(d<0?-d:d),a[0]);}
    else if(f==G(0x492934))p=pair(r16(0,a[0]+4),a[1]);
    else if(f==r32(0,AT(VT+0x3c))){++twist_calls;p=pair(r16(0,a[0]+0x30),0);}
    else if(f==r32(0,AT(VT+0x10))){++radius_calls;p=pair(100,0);}
    else if(f==G(0x47d32c)||f==G(0x47e0b0))p=pair(0,0);
    else if(f==G(0x43d9b4)){++hud_calls;hud_target=r32(0,a[1]+6);hud_flag=*(uint8_t *)host(0,a[1]+0x24);
        memcpy(hud_regs,entry,sizeof(hud_regs));CHECK(a[0]==AT(HUD)&&a[1]==AT(HUD+0xb1));p=pair(0x12345678u,0xabcdu);}
    else if(f==G(0x433174))p=es2_cockpit_valid_registers(&memory,graph,a[0],a[1],entry);
    else if(f==G(0x433250))p=es2_cockpit_arc_registers(&memory,graph,a[0],a[1],entry);
    else if(f==G(0x43349c))p=es2_cockpit_next_registers(&memory,graph,a[0],entry);
    else {fprintf(stderr,"cockpit_target: unexpected call %08x (%u)\n",f,n);exit(1);}
    r->eax=(uint32_t)p;r->edx=(uint32_t)(p>>32);r->ecx=0x5eed0000u;
}
static void actor(unsigned i,int32_t x,uint16_t heading,uint8_t side,uint16_t type){
    uint32_t A=ACTOR(i);w32(0,A,AT(VT));w32(0,A+0x26,(uint32_t)x);w16(0,A+0x2a,heading);
    w32(0,A+0x45,AT(GRP+0x20*side));w16(0,A+0x4b,(uint16_t)i);w16(0,A+0x1a8,type);
    w8(A+0xc2+i,1);
}
static void reset(void){
    memset(arena,0,sizeof(arena));memset(globals,0,sizeof(globals));hud_calls=twist_calls=radius_calls=0;
    w32(0,AT(VT+0x3c),0xdead003cu);w32(0,AT(VT+0x10),0xdead0010u);
    for(unsigned s=0;s<4;++s)w8(AT(GRP+0x20*s+0x12),(uint8_t)s);
    w32(0,AT(H+0x203),ACTOR(0));w32(0,AT(H+0x1f5),AT(HUD));
    w32(0,G(0x4d1c78),1000);w32(0,G(0x4a9af8),AT(LIST));w32(0,G(0x4d040c),AT(TOP));w32(0,G(0x4d0410),AT(BUCKETS));
    for(unsigned k=0;k<4;++k){w32(0,AT(BUCKETS+12*k+4),AT(ARRAYS+0x20*k));w32(0,AT(BUCKETS+12*k+8),AT(ARRAYS+0x20*k+0x10));}
    actor(0,0,0,0,0);
    /* The player sees every table index set (S+C2h bytes). */
    for(unsigned i=0;i<8;++i)w8(ACTOR(0)+0xc2+i,1);
}
static void list(const unsigned *ids,unsigned count){
    for(unsigned i=0;i<count;++i)w32(0,AT(LIST+4*i),ACTOR(ids[i]));
    w16(0,G(0x4a9afe),(uint16_t)count);
}
static void valid_checks(const es2_weapon_graph *g){
    const uint32_t entry[3]={1,2,3};uint32_t T=ACTOR(1);
    reset();actor(1,500,0,1,0);
    /* Different side, table byte set, 500 < 60000. */
    CHECK(es2_cockpit_valid_registers(&memory,g,AT(H),T,entry)==pair(1,1));
    /* Destroyed (+99h) or dead (+A4h): 0 with EDX = T+92h. */
    w8(T+0x99,1);CHECK(es2_cockpit_valid_registers(&memory,g,AT(H),T,entry)==pair(0,T+0x92));w8(T+0x99,0);
    w8(T+0xa4,2);CHECK(es2_cockpit_valid_registers(&memory,g,AT(H),T,entry)==pair(0,T+0x92));w8(T+0xa4,0);
    /* Same side as the player: 0 with EDX = the side. */
    w32(0,T+0x45,AT(GRP));CHECK(es2_cockpit_valid_registers(&memory,g,AT(H),T,entry)==pair(0,0));
    w32(0,T+0x45,AT(GRP+0x60));w8(AT(GRP+0x72),0xff);w8(AT(GRP+0x12),0xff);
    CHECK(es2_cockpit_valid_registers(&memory,g,AT(H),T,entry)==pair(0,0xffffffffu));
    w32(0,T+0x45,AT(GRP+0x20));
    /* Table byte clear: 0 with EDX = the index; +95h and d < [4D1C78h] still 1. */
    w8(ACTOR(0)+0xc2+1,0);CHECK(es2_cockpit_valid_registers(&memory,g,AT(H),T,entry)==pair(0,1));
    w8(T+0x95,1);CHECK(es2_cockpit_valid_registers(&memory,g,AT(H),T,entry)==pair(1,T+0x92));
    w32(0,T+0x26,1000);CHECK(es2_cockpit_valid_registers(&memory,g,AT(H),T,entry)==pair(0,1));
    w8(T+0x95,0);w8(ACTOR(0)+0xc2+1,1);
    /* Far limit 60000, or 30000 when word 4A9E5Eh is 1; a negative index. */
    w32(0,T+0x26,59999);CHECK(es2_cockpit_valid_registers(&memory,g,AT(H),T,entry)==pair(1,1));
    w16(0,G(0x4a9e5e),1);CHECK(es2_cockpit_valid_registers(&memory,g,AT(H),T,entry)==pair(0,1));
    w16(0,T+0x4b,0xffff);w8(ACTOR(0)+0xc1,1);w32(0,T+0x26,29999);
    CHECK(es2_cockpit_valid_registers(&memory,g,AT(H),T,entry)==pair(1,0xffffffffu));
    /* The view subject replaces the player while 49EF34h is set. */
    w32(0,G(0x49ef34),1);w32(0,G(0x4d2684),ACTOR(2));actor(2,29999,0,1,0);
    CHECK(es2_cockpit_valid_registers(&memory,g,AT(H),T,entry)==pair(0,1));
}
static void arc_checks(const es2_weapon_graph *g){
    const uint32_t entry[3]={1,2,3};uint32_t T=ACTOR(1);
    reset();actor(1,500,0,1,0);
    /* Bearing = T's y word - player heading + twist. */
    w16(0,T+0x2a,8999);CHECK(es2_cockpit_arc_registers(&memory,g,AT(H),T,entry)==pair(1,1)&&twist_calls==1);
    w16(0,T+0x2a,9000);CHECK(es2_cockpit_arc_registers(&memory,g,AT(H),T,entry)==pair(0,0));
    w16(0,ACTOR(0)+0x30,0xffff);CHECK(es2_cockpit_arc_registers(&memory,g,AT(H),T,entry)==pair(1,1));
    w16(0,T+0x2a,(uint16_t)-9000);CHECK(es2_cockpit_arc_registers(&memory,g,AT(H),T,entry)==pair(0,0));
    w16(0,ACTOR(0)+0x10,(uint16_t)-2);CHECK(es2_cockpit_arc_registers(&memory,g,AT(H),T,entry)==pair(1,1));
    /* View subject: its heading, no twist call. */
    twist_calls=0;w32(0,G(0x49ef34),1);w32(0,G(0x4d2684),ACTOR(2));actor(2,0,0,1,0);w16(0,ACTOR(2)+0x10,0x8000);
    w16(0,T+0x2a,0x8000);CHECK(es2_cockpit_arc_registers(&memory,g,AT(H),T,entry)==pair(1,1)&&!twist_calls);
}
static void next_checks(const es2_weapon_graph *g){
    const uint32_t entry[3]={1,2,3};
    reset();
    /* A1 ahead at 300, A2 ahead at 100, A3 1500 to the side (bucket 1), A4
     * behind (outside the arc), A5 on the player's side. */
    actor(1,300,0,1,0);actor(2,100,0,1,0);actor(3,700,1500,1,0);actor(4,50,20000,1,0);actor(5,10,0,0,0);
    static const unsigned ids[]={1,2,3,4,5};list(ids,5);
    uint64_t p=es2_cockpit_next_registers(&memory,g,AT(H),entry);
    CHECK(r32(0,AT(TOP))==ACTOR(2)&&r32(0,AT(TOP+4))==ACTOR(1)&&r32(0,AT(TOP+8))==ACTOR(3)&&r32(0,AT(TOP+12))==0);
    CHECK(r32(0,AT(BUCKETS))==2&&r32(0,AT(BUCKETS+12))==1&&r32(0,AT(ARRAYS+0x10))==100&&r32(0,AT(ARRAYS+0x14))==300);
    CHECK(r32(0,AT(ARRAYS+0x18))==0x7ffffffu&&r32(0,AT(BUCKETS+24))==0);
    CHECK(r32(0,AT(H+0x210))==ACTOR(2)&&hud_calls==1&&hud_target==ACTOR(2)&&hud_flag==1);
    CHECK(hud_regs[0]==AT(H)&&hud_regs[1]==AT(HUD)&&hud_regs[2]==ACTOR(2)&&p==pair(ACTOR(2),0xabcd)&&radius_calls==3);
    /* Again: the head equals the current target, so the entry after it. */
    p=es2_cockpit_next_registers(&memory,g,AT(H),entry);CHECK(p==pair(ACTOR(1),0xabcd)&&hud_target==ACTOR(1));
    p=es2_cockpit_next_registers(&memory,g,AT(H),entry);CHECK(p==pair(ACTOR(3),0xabcd));
    /* At the last entry the head is chosen again. */
    p=es2_cockpit_next_registers(&memory,g,AT(H),entry);CHECK(p==pair(ACTOR(2),0xabcd)&&hud_calls==4);
    /* No candidate: the target is cleared. */
    w16(0,G(0x4a9afe),0);p=es2_cockpit_next_registers(&memory,g,AT(H),entry);
    CHECK(p==pair(0,0xabcd)&&r32(0,AT(TOP))==0&&hud_calls==5&&hud_target==0);
    /* Unchanged (still none): no HUD call, EDX = the choice. */
    p=es2_cockpit_next_registers(&memory,g,AT(H),entry);CHECK(p==pair(0,0)&&hud_calls==5);
    /* Five in bucket 0: the four nearest stay, sorted. */
    reset();for(unsigned i=1;i<6;++i)actor(i,(int32_t)(600-100*i),0,1,0);
    static const unsigned five[]={1,2,3,4,5};list(five,5);
    es2_cockpit_next_registers(&memory,g,AT(H),entry);
    CHECK(r32(0,AT(BUCKETS))==4&&r32(0,AT(TOP))==ACTOR(5)&&r32(0,AT(TOP+12))==ACTOR(2)&&r32(0,AT(ARRAYS+0xc))==ACTOR(2));
}
static void nearest_select_checks(const es2_weapon_graph *g){
    const uint32_t entry[3]={1,2,3};
    reset();actor(1,300,0,1,0);actor(2,100,0,1,1);actor(3,200,0,1,2);actor(4,150,0,0,0);
    static const unsigned ids[]={1,2,3,4};list(ids,4);
    /* Type 1 and own side are skipped: A3 at 200 wins. */
    uint64_t p=es2_cockpit_nearest_registers(&memory,g,AT(H),entry);
    CHECK(p==pair(ACTOR(3),0xabcd)&&r32(0,AT(H+0x210))==ACTOR(3)&&hud_calls==1&&hud_regs[0]==AT(HUD+0xb1));
    CHECK(hud_regs[1]==ACTOR(3)&&hud_regs[2]==AT(HUD));
    /* Already the target: nothing changes, EDX as the last call left it. */
    p=es2_cockpit_nearest_registers(&memory,g,AT(H),entry);CHECK((uint32_t)p==ACTOR(3)&&hud_calls==1);
    /* select 0 clears and reports. */
    p=es2_cockpit_select_registers(&memory,g,AT(H),0,entry);
    CHECK(p==pair(0x12345601u,0xabcd)&&r32(0,AT(H+0x210))==0&&hud_calls==2&&hud_target==0);
    /* select A3 from no target: next target first (A3 is nearest ahead...),
     * then the cursor walks the actor list until A3. */
    w32(0,AT(H+0x214),AT(LIST));w16(0,AT(H+0x218),0);
    p=es2_cockpit_select_registers(&memory,g,AT(H),ACTOR(1),entry);
    CHECK(p==pair(0x12345601u,0xabcd)&&r32(0,AT(H+0x210))==ACTOR(1));
    /* An actor that never passes: the walk returns to the start, result 0. */
    p=es2_cockpit_select_registers(&memory,g,AT(H),ACTOR(4),entry);
    CHECK((uint32_t)p==0&&r32(0,AT(H+0x210))==ACTOR(1));
}
int main(void){
    es2_weapon_graph g={B,0,0,0,0,call,NULL,NULL,NULL};graph=&g;
    valid_checks(&g);arc_checks(&g);next_checks(&g);nearest_select_checks(&g);
    printf("cockpit_target: %u checks passed\n",checks);return 0;
}
