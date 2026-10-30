#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/mission_objectives.h"
/* Synthetic mission state for the cores. The image base is placed so
 * that every global (49935c .. 4d3b64) falls inside the arena; objects live
 * above them. Hooked callees resolve to the cores themselves, the original
 * helpers to small models with the documented results. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x99000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x40000u)
static uint8_t arena[0x48000];static unsigned checks,calls[24];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"objectives line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static es2_weapon_graph graph(void);
static uint32_t magnitude(uint32_t v){return (v&0x80000000u)?0u-v:v;}
static uint16_t status_model,subject_model,rivals_model;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;es2_weapon_graph g=graph();uint64_t p=0;uint32_t entry[3]={r->eax,r->edx,r->ecx};
    if(f==VA(0x49288c)){CHECK(n==2);++calls[0];  /* 3-D metric model: max of |dx|,|dy| */
        uint32_t x=magnitude(r32(0,a[0])-r32(0,a[1])),y=magnitude(r32(0,a[0]+4)-r32(0,a[1]+4));p=x>y?x:y;}
    else if(f==VA(0x467a48)){CHECK(n==1);++calls[1];uint16_t v=(uint16_t)(r16(0,a[0]+1)-r16(0,VA(0x4d3b64)));
        if(v&0x8000u)v=0;w16(0,a[0]+1,v);p=(uint64_t)r->edx<<32|((a[0]&0xffff0000u)|v);}
    else if(f==VA(0x4135e8)){CHECK(n==3);++calls[2];p=(uint64_t)a[0]<<32|status_model;}
    else if(f==VA(0x412dc4)){CHECK(n==1);++calls[3];p=subject_model;}
    else if(f==VA(0x412be4)){CHECK(n==1);++calls[4];p=rivals_model;}
    else if(f==VA(0x41324c)){CHECK(n==2);++calls[5];p=1;}
    else if(f==VA(0x41373c)){++calls[6];p=es2_bounds_outside_registers(&memory,&g,a[0],a[1],entry);}
    else if(f==VA(0x41379c)){++calls[7];p=es2_bounds_check_registers(&memory,&g,a[0],entry);}
    else if(f==VA(0x413180)){++calls[8];p=es2_status_eval_registers(&memory,&g,a[0],a[1],entry);}
    else if(f==VA(0x4137b4)){++calls[9];p=es2_actor_safe_registers(&memory,&g,a[0],entry);}
    else if(f==VA(0x411c58)){++calls[10];p=es2_target_known_registers(&memory,&g,a[0],a[1],entry);}
    else if(f==VA(0x423918)){++calls[11];p=es2_order_target_registers(&memory,&g,a[0],a[1],entry);}
    else if(f==VA(0x423b0c)){++calls[12];p=es2_route_point_registers(&memory,&g,a[0],a[1],entry);}
    else if(f==VA(0x412e58)){++calls[13];p=es2_subject_done_registers(&memory,&g,a[0],entry);}
    else if(f==VA(0x412e74)){++calls[14];p=es2_rivals_done_registers(&memory,&g,a[0],entry);}
    /* The twin subject getters, a group_state model (the word at
     * group+78h over EDX 0x5a5a), state_reached and an actor +40h model
     * (the word at actor+100h, EDX = the vtable read by the caller). */
    else if(f==VA(0x412bd0)){CHECK(n==1);++calls[15];p=es2_subject_group_registers(&memory,a[0]);}
    else if(f==VA(0x412bbc)){CHECK(n==1);++calls[16];p=es2_subject_actor_registers(&memory,a[0]);}
    else if(f==VA(0x412c8c)){CHECK(n==1);++calls[17];p=(uint64_t)0x5a5au<<32|((r->eax&0xffff0000u)|r16(0,a[0]+0x78));}
    else if(f==VA(0x413920)){CHECK(n==1);++calls[18];p=es2_state_reached_registers(&memory,&g,a[0],entry);}
    else if(f==VA(0x4ffff0)){CHECK(n==1&&r->eax==a[0]&&r->edx==r32(0,a[0]));++calls[19];p=(uint64_t)r->edx<<32|(0xbeef0000u|r16(0,a[0]+0x100));}
    else CHECK(0);
    r->eax=(uint32_t)p;r->edx=(uint32_t)(p>>32);
}
static es2_weapon_graph graph(void){es2_weapon_graph g={IMAGE,0,0,0,0,call,0,0,0};return g;}
/* Objects: actors 0x40000 (stride 0x200), groups 0x41000 (0x7a), orders
 * 0x41400 (0x16), routes 0x41600, points 0x41700, objectives 0x41800. */
static uint32_t actor(unsigned i){return OBJECTS+0x200u*i;}
static uint32_t group(unsigned i){return OBJECTS+0x1000u+0x7au*i;}
static uint32_t order(unsigned i){return OBJECTS+0x1400u+0x16u*i;}
static uint32_t objective(unsigned i){return OBJECTS+0x1800u+0x4cu*i;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void place(uint32_t a,int32_t x,int32_t y){w32(0,a+0x26,(uint32_t)x);w32(0,a+0x2a,(uint32_t)y);w32(0,a+0x2e,0);}
/* Objective predicates over a group of four actors at OBJECTS+4000h
 * (array +4100h, orders +4200h, actors +5000h, vtable +4400h). */
static void preds_checks(void){
    es2_weapon_graph g=graph();
    const uint32_t G=OBJECTS+0x4000,list=OBJECTS+0x4100,vt=OBJECTS+0x4400,o0=OBJECTS+0x4200,o1=o0+0x16;
    uint32_t A[4];
    for(unsigned i=0;i<4;++i){A[i]=OBJECTS+0x5000+0x200*i;w32(0,list+4*i,A[i]);w32(0,A[i],vt);
        w8(A[i]+0x99,0);w8(A[i]+0xa4,0);w8(A[i]+0xa5,0);w8(A[i]+0x9e,0);w8(A[i]+0xb4,0);w32(0,A[i]+0x45,G);}
    w32(0,vt+0x40,VA(0x4ffff0));
    w32(0,G+0xc,list);w16(0,G+0x10,4);w8(G+0x12,0);
    for(unsigned j=0;j<10;++j){w32(0,G+0x44+4*j,0);w8(G+0x70+j,(uint8_t)(0x80+j));}
    w32(0,G+0x44+4*3,o0);w32(0,G+0x44+4*7,o1);w32(0,G+0x6c,3);
    w32(0,o0+8,0x1234);w16(0,o0+0xc,0);w32(0,o0+0xe,G);w32(0,o1+8,0x5678);w16(0,o1+0xc,2);w32(0,o1+0xe,A[2]);
    /* Subject getters: the current order's +0Eh, EDX = the index. */
    CHECK(es2_subject_group_registers(&memory,G)==((uint64_t)3<<32|G));
    w32(0,G+0x6c,7);CHECK(es2_subject_actor_registers(&memory,G)==((uint64_t)7<<32|A[2]));
    /* route_flag: the sign-extended completion byte of the matching slot. */
    CHECK(es2_route_flag_registers(&memory,G,0x5678)==((uint64_t)7<<32|0xffffff87u));
    CHECK(es2_route_flag_registers(&memory,G,0x1234)==((uint64_t)3<<32|0xffffff83u));
    CHECK(es2_route_flag_registers(&memory,G,0x9999)==((uint64_t)10<<32));
    /* group_down: AX = 1 over the group's high word when all are down. */
    CHECK(es2_group_down_registers(&memory,G)==((uint64_t)list<<32));
    w8(A[0]+0xa5,1);w8(A[1]+0xa4,2);w8(A[2]+0x99,3);
    CHECK(es2_group_down_registers(&memory,G)==((uint64_t)(list+12)<<32));
    w8(A[3]+0x99,1);CHECK(es2_group_down_registers(&memory,G)==((uint64_t)(list+16)<<32|(G&0xffff0000u)|1u));
    w16(0,G+0x10,0x8000);CHECK(es2_group_down_registers(&memory,G)==((uint64_t)list<<32|(G&0xffff0000u)|1u));
    w16(0,G+0x10,4);
    /* group_flagged (+0A5h on every actor) and group_marked (+9Eh on any). */
    CHECK(es2_group_flagged_registers(&memory,G)==((uint64_t)1<<32));
    for(unsigned i=0;i<4;++i)w8(A[i]+0xa5,1);
    CHECK(es2_group_flagged_registers(&memory,G)==((uint64_t)4<<32|((list+16)&0xffff0000u)|1u));
    CHECK(es2_group_marked_registers(&memory,G)==((uint64_t)4<<32));
    w8(A[2]+0x9e,1);CHECK(es2_group_marked_registers(&memory,G)==((uint64_t)2<<32|((list+8)&0xffff0000u)|1u));
    /* subject_status: group subject through group_state; actor subject. */
    w32(0,G+0x6c,3);w16(0,G+0x78,4);
    CHECK(es2_subject_status_registers(&memory,&g,G,entry)==((uint64_t)0x5a5a<<32|(G&0xffff0000u)|4u)&&calls[15]==1&&calls[17]==1);
    w32(0,G+0x6c,7);w8(A[2]+0x99,1);
    CHECK(es2_subject_status_registers(&memory,&g,G,entry)==((uint64_t)7<<32|(A[2]&0xffff0000u)|4u)&&calls[16]==1);
    w8(A[2]+0x99,0);w8(A[2]+0xb4,1);
    CHECK(es2_subject_status_registers(&memory,&g,G,entry)==((uint64_t)7<<32|(A[2]&0xffff0000u)|4u)&&calls[16]==3);
    w8(A[2]+0xb4,0);
    static const struct { uint16_t level; uint32_t status; } levels[]={{0x31,0},{0x32,0xbeef0001u},{0x7f,0xbeef0001u},
        {0x80,0xbeef0002u},{0xbf,0xbeef0002u},{0xc0,0xbeef0003u},{0x7fff,0xbeef0003u},{0xffff,0}};
    for(unsigned k=0;k<8;++k){w16(0,A[2]+0x100,levels[k].level);
        CHECK(es2_subject_status_registers(&memory,&g,G,entry)==((uint64_t)vt<<32|levels[k].status));}
    CHECK(calls[19]==8);
    /* state_reached: threshold 3, or 4 with a category byte. */
    w16(0,G+0x78,3);CHECK(es2_state_reached_registers(&memory,&g,G,entry)==((uint64_t)0x5a5a<<32|1u));
    w8(G+0x12,1);CHECK(es2_state_reached_registers(&memory,&g,G,entry)==((uint64_t)0x5a5a<<32));
    w16(0,G+0x78,0xffff);w8(G+0x12,0);CHECK(es2_state_reached_registers(&memory,&g,G,entry)==((uint64_t)0x5a5a<<32));
    /* group_safe: a reached state gives 0; else every actor without +99h
     * through actor_safe (an empty actor list: all safe). */
    w16(0,G+0x78,3);CHECK(es2_group_safe_registers(&memory,&g,G,entry)==((uint64_t)0x5a5a<<32));
    w16(0,G+0x78,0);w16(0,VA(0x4a9afe),0);w32(0,VA(0x4d24e6),A[0]);
    for(unsigned i=0;i<4;++i)w8(A[i]+0x99,i==3);
    const unsigned safe=calls[9];
    CHECK((uint32_t)es2_group_safe_registers(&memory,&g,G,entry)==((A[3]&0xffff0000u)|1u)&&calls[9]-safe==3);
    /* bounds_margin: bounds_outside(actor+26h, 110000). */
    w32(0,VA(0x4aa640),(uint32_t)-1000);w32(0,VA(0x4aa644),(uint32_t)-1000);w32(0,VA(0x4aa648),1000);w32(0,VA(0x4aa64c),1000);
    place(A[1],100000,0);CHECK(es2_bounds_margin_registers(&memory,&g,A[1],entry)==((uint64_t)(A[1]+0x26)<<32));
    place(A[1],111000,0);CHECK(es2_bounds_margin_registers(&memory,&g,A[1],entry)==((uint64_t)(A[1]+0x26)<<32|(VA(0x4aa640)&0xffff0000u)|1u));
}
int main(void){
    es2_weapon_graph g=graph();
    /* route_point: route 0 with three points; holder in group 0. */
    uint32_t route=OBJECTS+0x1600,points=OBJECTS+0x1640,g0=group(0),g1=group(1),g2=group(2);
    w16(0,route,3);w32(0,route+2,points);for(unsigned i=0;i<3;++i)w32(0,points+4*i,OBJECTS+0x1700+12*i);
    w32(0,g0+6,route);
    CHECK(es2_route_point_registers(&memory,&g,g0+4,1,entry)==((uint64_t)(OBJECTS+0x170c)<<32|(OBJECTS+0x170c)));
    CHECK(es2_route_point_registers(&memory,&g,g0+4,0xabcd0002u,entry)==((uint64_t)(OBJECTS+0x1718)<<32|(OBJECTS+0x1718)));
    CHECK(es2_route_point_registers(&memory,&g,g0+4,3,entry)==0);
    w32(0,g1+6,0);CHECK(es2_route_point_registers(&memory,&g,g1+4,0,entry)==0);
    /* order_target: scope 0 names the group, scope 2 the actor. */
    uint32_t a0=actor(0),a1=actor(1),a2=actor(2),o0=order(0),o1=order(1);
    w32(0,a0+0x45,g0);w32(0,a1+0x45,g1);w32(0,a2+0x45,g2);
    w32(0,g1+0x44,o0);w32(0,g1+0x6c,0);w16(0,o0+0xc,0);w32(0,o0+0xe,g0);
    CHECK(es2_order_target_registers(&memory,&g,g1,a0,entry)==((uint64_t)a0<<32|1));
    w16(0,o0+0xc,2);CHECK(es2_order_target_registers(&memory,&g,g1,a0,entry)==((uint64_t)a0<<32));
    w32(0,o0+0xe,a0);CHECK(es2_order_target_registers(&memory,&g,g1,a0,entry)==((uint64_t)a0<<32|1));
    w16(0,o0+0xc,0xffff);CHECK(es2_order_target_registers(&memory,&g,g1,a0,entry)==0);
    w32(0,g1+0x6c,1);CHECK(es2_order_target_registers(&memory,&g,g1,a0,entry)==(uint64_t)1<<32);
    w32(0,g1+0x6c,0);w16(0,o0+0xc,2);
    /* bounds: rectangle (-1000,-1000)..(1000,1000); edges count as outside. */
    uint32_t rect=VA(0x4aa640),out=(rect&0xffff0000u)|1u;
    w32(0,rect,(uint32_t)-1000);w32(0,rect+4,(uint32_t)-1000);w32(0,rect+8,1000);w32(0,rect+12,1000);
    place(a0,0,0);CHECK(es2_bounds_check_registers(&memory,&g,a0,entry)==((uint64_t)(a0+0x26)<<32));
    place(a0,1000,0);CHECK((uint32_t)es2_bounds_check_registers(&memory,&g,a0,entry)==out);
    place(a0,0,-1000);CHECK((uint32_t)es2_bounds_check_registers(&memory,&g,a0,entry)==out);
    CHECK(es2_bounds_outside_registers(&memory,&g,a0+0x26,1,entry)==((uint64_t)(a0+0x26)<<32));
    place(a0,0,0);
    /* target_known: distance gate below 1000000 when byte +95 is set,
     * otherwise the observer's table at +0C2 indexed by the target's +4B. */
    place(a1,500000,0);w8(a0+0x95,1);
    CHECK((uint16_t)es2_target_known_registers(&memory,&g,a1,a0,entry)==1);
    place(a1,1000000,0);CHECK((uint32_t)es2_target_known_registers(&memory,&g,a1,a0,entry)==0);
    w16(0,a0+0x4b,5);w8(a1+0xc2+5,1);CHECK((uint32_t)es2_target_known_registers(&memory,&g,a1,a0,entry)==1);
    w16(0,a0+0x4b,0xffff);w8(a1+0xc1,1);CHECK((uint32_t)es2_target_known_registers(&memory,&g,a1,a0,entry)==0xffff0001u);
    w8(a1+0xc1,0);w8(a1+0xc2+5,0);w16(0,a0+0x4b,5);
    /* actor_safe: a0 is the player (group 0, category 0); a1 hostile (group
     * 1, category 1) knows a0 and is within 80000: unsafe. */
    w32(0,VA(0x4d24e6),a0);w32(0,VA(0x4a9af8),OBJECTS+0x1f00);w16(0,VA(0x4a9afe),3);
    w32(0,OBJECTS+0x1f00,a0);w32(0,OBJECTS+0x1f04,a1);w32(0,OBJECTS+0x1f08,a2);
    w8(g0+0x12,0);w8(g1+0x12,1);w8(g2+0x12,0);
    place(a1,50000,0);w8(a1+0xc2+5,1);
    CHECK((uint16_t)es2_actor_safe_registers(&memory,&g,a0,entry)==0);
    place(a1,90000,0);CHECK((uint16_t)es2_actor_safe_registers(&memory,&g,a0,entry)==1);
    w32(0,a1+0x1a4,a0);CHECK((uint16_t)es2_actor_safe_registers(&memory,&g,a0,entry)==0); /* 100000 when targeted */
    w8(a1+0x99,1);CHECK((uint16_t)es2_actor_safe_registers(&memory,&g,a0,entry)==1);     /* down */
    w8(a1+0x99,0);w32(0,g1+0x14,1);CHECK((uint16_t)es2_actor_safe_registers(&memory,&g,a0,entry)==1); /* pending */
    w32(0,g1+0x14,0);
    /* Not the player's group: a2 (group 2) is threatened by a1 whose order
     * names a2 while a1's route still has a next point. */
    w32(0,o0+0xe,a2);w16(0,g1+4,0);w32(0,g1+6,route);place(a2,0,0);w8(a1+0xc2+5,0);w16(0,a2+0x4b,6);
    CHECK((uint16_t)es2_actor_safe_registers(&memory,&g,a2,entry)==0);
    w16(0,g1+4,2);CHECK((uint16_t)es2_actor_safe_registers(&memory,&g,a2,entry)==1); /* route ended, unknown */
    w32(0,a1+0x1a4,0);
    /* order_done and its helpers. */
    w16(0,o1,0);subject_model=4;CHECK((uint32_t)es2_order_done_registers(&memory,&g,g0,o1,entry)==1);
    subject_model=3;CHECK((uint32_t)es2_order_done_registers(&memory,&g,g0,o1,entry)==0);
    w16(0,o1,3);w16(0,g0+4,1);CHECK((uint32_t)es2_order_done_registers(&memory,&g,g0,o1,entry)==0);
    w16(0,g0+4,2);CHECK((uint32_t)es2_order_done_registers(&memory,&g,g0,o1,entry)==1);
    w16(0,o1,1);CHECK(es2_order_done_registers(&memory,&g,g0,o1,entry)==(uint64_t)o1<<32);
    /* Type 2: the rival group g1 (other category) has the same subject. */
    w16(0,o1,2);w32(0,o1+0xe,a0);w32(0,g0+0x44,o1);w32(0,g0+0x6c,0);w32(0,o0+0xe,a0);
    w32(0,VA(0x4a9e00),g0);w16(0,VA(0x4a9e04),3);w32(0,g2+0x6c,0);w32(0,g2+0x44,0);
    rivals_model=1;CHECK((uint32_t)es2_order_done_registers(&memory,&g,g0,o1,entry)==1);
    rivals_model=0;CHECK((uint32_t)es2_order_done_registers(&memory,&g,g0,o1,entry)==0);
    CHECK(calls[4]==2);
    w32(0,o0+0xe,a1);rivals_model=0;CHECK((uint32_t)es2_order_done_registers(&memory,&g,g0,o1,entry)==1); /* no rival */
    w32(0,o1+0xe,0);CHECK((uint32_t)es2_order_done_registers(&memory,&g,g0,o1,entry)==0);
    /* objective_eval: one required objective (actor scope, type 7 = +0A5)
     * and one optional failure objective (group scope, type 9). */
    uint32_t b0=objective(0),b1=objective(1);
    w32(0,VA(0x4a9e40),b0);w16(0,VA(0x4a9e3c),2);
    for(unsigned k=0;k<2;++k){uint32_t b=objective(k);memset(host(0,b),0,0x4c);for(unsigned j=0;j<10;++j){w16(0,b+0x24+2*j,0xffff);w16(0,b+0x38+2*j,0xffff);}}
    w16(0,b0,1);w16(0,b0+2,7);w16(0,b0+4,1);w32(0,b0+6,a2);
    w16(0,b0+0x24,3);w16(0,b0+0x38,6);w16(0,b0+0x26,4);w16(0,b0+0x3a,4);
    w16(0,b1,0);w16(0,b1+2,9);w16(0,b1+4,0);w32(0,b1+6,g0);w8(a0+0xa0,1);
    place(a1,500000,0);
    CHECK((uint32_t)es2_objective_eval_registers(&memory,&g,a0,0,entry)==5);
    CHECK(r32(0,VA(0x4d1e98))==b0+0x12);
    w8(a2+0xa5,1);
    CHECK((uint32_t)es2_objective_eval_registers(&memory,&g,a0,0,entry)==9);
    CHECK((uint32_t)es2_objective_eval_registers(&memory,&g,a0,0,entry)==9);
    CHECK(r16(0,VA(0x4a9e70)+6)==1&&r16(0,VA(0x4a9e70)+8)==1&&r16(0,b0+0x22)==1); /* actions once */
    w8(a0+0xa0,0);CHECK((uint32_t)es2_objective_eval_registers(&memory,&g,a0,0,entry)==6);
    place(a1,1000,0);w8(a1+0xc2+5,1);CHECK((uint32_t)es2_objective_eval_registers(&memory,&g,a0,0,entry)==10);
    /* Type 5 assigns no predicate: the previous objective's ESI (b0 met,
     * so the optional b1 fails), or the caller's ESI for the first one. */
    w8(a0+0xa0,1);w16(0,b1+2,5);place(a1,500000,0);
    CHECK((uint32_t)es2_objective_eval_registers(&memory,&g,a0,0,entry)==6);
    w32(0,VA(0x4a9e40),b1);w16(0,VA(0x4a9e3c),1);
    CHECK((uint32_t)es2_objective_eval_registers(&memory,&g,a0,7,entry)==6);
    CHECK((uint32_t)es2_objective_eval_registers(&memory,&g,a0,0,entry)==9);
    /* outcome_poll: table words for 2,3,6,7,8,9; scale 100 per frame. */
    static const unsigned reportable[]={2,3,6,7,8,9};
    for(unsigned k=0;k<6;++k)w16(0,VA(0x49935c)+2*reportable[k],1);
    w16(0,VA(0x4d3b64),100);w16(0,VA(0x4a9e63),150);w16(0,VA(0x4a9e66),0);w16(0,VA(0x4a9e68),0);w16(0,VA(0x4a9e4c),0xffff);
    status_model=4;place(a0,0,0);
    CHECK((uint32_t)es2_outcome_poll_registers(&memory,&g,a0,entry)==0xffffffffu&&calls[2]==0);  /* countdown runs */
    CHECK((uint32_t)es2_outcome_poll_registers(&memory,&g,a0,entry)==0xffffffffu&&calls[2]==1);  /* 4 is not reportable */
    CHECK(r16(0,VA(0x4a9e63))==10000);
    w16(0,VA(0x4a9e63),0);status_model=9;
    CHECK((uint32_t)es2_outcome_poll_registers(&memory,&g,a0,entry)==0xffffffffu);              /* grace starts */
    CHECK(r16(0,VA(0x4a9e68))==1&&r16(0,VA(0x4a9e66))==10000&&r16(0,VA(0x4a9e4c))==0xffff);
    w16(0,VA(0x4a9e63),0);status_model=2;
    CHECK((uint32_t)es2_outcome_poll_registers(&memory,&g,a0,entry)==2&&r16(0,VA(0x4a9e4c))==2); /* 2 passes */
    w16(0,VA(0x4a9e63),0);CHECK((uint32_t)es2_outcome_poll_registers(&memory,&g,a0,entry)==0xffffffffu); /* unchanged */
    w16(0,VA(0x4a9e66),0);w16(0,VA(0x4a9e63),0);status_model=9;
    CHECK((uint64_t)es2_outcome_poll_registers(&memory,&g,a0,entry)==((uint64_t)9<<32|9)&&r16(0,VA(0x4a9e4c))==9);
    /* Out of bounds evaluates while the countdown runs. */
    w16(0,VA(0x4a9e63),5000);place(a0,5000,0);status_model=7;
    CHECK((uint32_t)es2_outcome_poll_registers(&memory,&g,a0,entry)==7);
    /* status_eval stores the forced status only. */
    status_model=3;w16(0,VA(0x4a9e4c),0);
    CHECK((uint32_t)es2_status_eval_registers(&memory,&g,a0,0x10000u,entry)==3&&r16(0,VA(0x4a9e4c))==0);
    CHECK((uint32_t)es2_status_eval_registers(&memory,&g,a0,1,entry)==3&&r16(0,VA(0x4a9e4c))==3);
    preds_checks();
    printf("PASS: mission objectives %u checks\n",checks);
    return 0;
}
