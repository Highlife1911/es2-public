#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/mission.h"
/* Synthetic checks of the cores (mission_runtime.md, "Player route
 * callback, mission timers and status check"). The image base is placed so
 * that the globals fall inside the arena; callees are small fakes that log
 * their target and first argument. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x99000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
static uint8_t arena[0x50000];static unsigned checks,logged;static uint32_t log_target[64],log_arg[64],log_esi;
static uint32_t answer_dist=0,answer_timer=0,answer_status=9,answer_target;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"route line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static es2_weapon_graph graph(void);
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    if(logged<64){log_target[logged]=f;log_arg[logged]=n?a[0]:0;}++logged;
    uint32_t entry[3]={r->eax,r->edx,r->ecx};es2_weapon_graph g=graph();uint64_t p=0;
    if(f==VA(0x423b0c)){uint32_t route=r32(0,a[0]+2);p=route&&(int16_t)a[1]<(int16_t)r16(0,route)?r32(0,r32(0,route+2)+4*(uint32_t)(int16_t)a[1]):0;}
    else if(f==VA(0x4928d0)||f==VA(0x49288c))p=answer_dist;
    else if(f==VA(0x423170))p=es2_route_closing_registers(&memory,&g,a[0],entry);
    else if(f==VA(0x42313c))p=es2_route_advance_registers(&memory,&g,a[0],entry);
    else if(f==VA(0x4238a0))p=answer_target;
    else if(f==VA(0x492934))p=0;
    else if(f==VA(0x467aa8)||f==VA(0x467a48))p=answer_timer;
    else if(f==VA(0x4230a4))p=es2_mission_timer_registers(&memory,&g,a[0],entry);
    else if(f==VA(0x413280)){log_esi=*(const uint32_t *)c;p=answer_status;}
    else if(f==VA(0x413780)||f==VA(0x41379c)||f==VA(0x420a68)||f==VA(0x423430)||f==VA(0x467ac4)||f==0x1234u)p=0;
    else CHECK(0);
    r->eax=(uint32_t)p;r->edx=(uint32_t)(p>>32);
}
static uint32_t force_esi;
static es2_weapon_graph graph(void){es2_weapon_graph g={IMAGE,BASE+0x3800,0,0,0,call,&force_esi,0,0};return g;}
static unsigned count_target(uint32_t f){unsigned k=0;for(unsigned i=0;i<logged&&i<64;++i)k+=log_target[i]==f;return k;}
int main(void){
    es2_weapon_graph g=graph();const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
    /* A closed route of four points: p0 p1 p2 p0. Holder at group+4. */
    uint32_t points=BASE+0x100,list=BASE+0x200,route=BASE+0x280,group=BASE+0x400,actor=BASE+0x800,members=BASE+0x600;
    for(unsigned i=0;i<3;++i){w32(0,points+12*i,1000*i);w32(0,points+12*i+4,0);}
    w32(0,list,points);w32(0,list+4,points+12);w32(0,list+8,points+24);w32(0,list+12,points);
    w16(0,route,4);w32(0,route+2,list);w32(0,group+6,route);w32(0,group+0xc,members);w32(0,members,actor);w32(0,actor+0x45,group);
    w16(0,group+4,1);CHECK((uint32_t)es2_route_closing_registers(&memory,&g,group+4,entry)==0);
    w16(0,group+4,2);CHECK((uint32_t)es2_route_closing_registers(&memory,&g,group+4,entry)==1); /* next is the first point */
    w16(0,group+4,2);uint64_t r=es2_route_advance_registers(&memory,&g,group+4,entry);
    CHECK(r16(0,group+4)==0&&(uint32_t)r==group+4&&(uint32_t)(r>>32)==list); /* closed: wraps */
    w32(0,list+12,points+24);w16(0,group+4,2);(void)es2_route_advance_registers(&memory,&g,group+4,entry);
    CHECK(r16(0,group+4)==3); /* open route: no wrap */
    w32(0,list+12,points);w16(0,group+4,2);(void)es2_route_advance_registers(&memory,&g,group+4,entry);
    w16(0,route,1);w16(0,group+4,0);r=es2_route_advance_registers(&memory,&g,group+4,entry);
    CHECK(r16(0,group+4)==1&&(uint32_t)(r>>32)==((entry[1]&0xffff0000u)|1u)); /* count <= 1: no test */
    w16(0,route,4);
    /* player_route: within 10000 of the next point -> event 1Dh and advance;
     * the loop's closing point is not advanced. */
    w16(0,VA(0x4a9e54),1);w16(0,group+4,0);answer_dist=9999;logged=0;
    r=es2_player_route_registers(&memory,&g,actor,entry);CHECK((uint32_t)r==0&&r16(0,group+4)==1&&count_target(VA(0x420a68))==1&&log_arg[3]==0x1d);
    answer_dist=10000;logged=0;(void)es2_player_route_registers(&memory,&g,actor,entry);CHECK(r16(0,group+4)==1&&count_target(VA(0x423170))==0);
    answer_dist=0;w16(0,group+4,2);logged=0;(void)es2_player_route_registers(&memory,&g,actor,entry);
    CHECK(r16(0,group+4)==2&&count_target(VA(0x423170))==1&&count_target(VA(0x420a68))==0);
    /* Not the first member: nothing. */
    w32(0,members,actor+0x300);logged=0;(void)es2_player_route_registers(&memory,&g,actor,entry);CHECK(!logged);w32(0,members,actor);
    /* Mode 3: target in front and near; four expiries set +0A0, event 34h+step. */
    uint32_t target=BASE+0xc00;answer_target=target;w32(0,target+0x45,group);w16(0,VA(0x4a9e54),3);w16(0,group+4,3);
    w32(0,VA(0x4a9cec),0);answer_timer=0;answer_dist=0;
    w16(0,actor+0x10,0xe000);w16(0,actor+0x298,0); /* 0 - E000h + 2000h = 4000h: not in front */
    logged=0;(void)es2_player_route_registers(&memory,&g,actor,entry);CHECK(r32(0,VA(0x4a9cec))==0&&count_target(VA(0x467aa8))==0);
    w16(0,actor+0x10,0xe001);
    for(unsigned k=0;k<4;++k){logged=0;(void)es2_player_route_registers(&memory,&g,actor,entry);
        CHECK(r32(0,VA(0x4a9cec))==k+1&&count_target(VA(0x420a68))==1);}
    CHECK(arena[actor+0xa0-BASE]==1);
    logged=0;(void)es2_player_route_registers(&memory,&g,actor,entry);CHECK(!count_target(VA(0x4238a0))); /* step 4: done */
    w32(0,VA(0x4a9cec),2);arena[target+0x99-BASE]=1;logged=0;(void)es2_player_route_registers(&memory,&g,actor,entry);
    CHECK(r32(0,VA(0x4a9cec))==0&&r32(0,VA(0x4a9cf1))==0&&count_target(VA(0x420a68))==1); /* reset with event 38h */
    /* Mission timers: a gate that has not fired holds the timer. */
    uint32_t timers=BASE+0x1000,trigger=BASE+0x1200;
    w32(0,VA(0x4a9e38),timers);w16(0,VA(0x4a9e34),2);w32(0,timers,trigger);w16(0,trigger+0xa,0);w32(0,timers+4,trigger);
    w32(0,timers+0x31,0);w32(0,timers+0x31+8,trigger);
    answer_timer=0;logged=0;r=es2_timer_scan_registers(&memory,&g,entry);
    CHECK((uint32_t)r==2&&count_target(VA(0x4230a4))==2&&count_target(VA(0x423430))==1&&count_target(VA(0x467ac4))==1);
    w16(0,trigger+0xa,1);logged=0;(void)es2_timer_scan_registers(&memory,&g,entry);CHECK(count_target(VA(0x423430))==2);
    /* status_check: forced evaluation passes force in ESI; periodic reports a
     * change once with message 17h (status 9) and re-arms the countdown. */
    w32(0,VA(0x49b088),BASE+0x2000);w32(0,BASE+0x2000+0x20b,BASE+0x2100);w32(0,BASE+0x2100,BASE+0x2180);w32(0,BASE+0x2180,0x1234u);
    uint32_t player=actor;force_esi=0x10001u;logged=0;r=es2_status_check_registers(&memory,&g,player,0x10001u,0,entry);
    CHECK((uint16_t)r==9&&log_esi==0x10001u&&count_target(VA(0x467a48))==0&&arena[0x99398-0x99000]==1);
    w32(0,VA(0x49939c),5);w32(0,VA(0x4993a0),0xffffffffu);answer_timer=0;force_esi=0x10000u;logged=0;
    r=es2_status_check_registers(&memory,&g,player,0x10000u,0,entry);
    /* The message path reports the previous status (49939c is still 5). */
    CHECK((uint32_t)r==5&&(uint32_t)(r>>32)==5&&count_target(0x1234u)==1&&r16(0,BASE+0x3800)==0x17&&r16(0,VA(0x4a9b69))==500&&r32(0,VA(0x4993a0))==5);
    logged=0;r=es2_status_check_registers(&memory,&g,player,0,0,entry);
    CHECK(count_target(0x1234u)==0&&(uint32_t)(r>>32)==0xffffffffu&&r32(0,VA(0x49939c))==9&&r32(0,VA(0x4993a0))==0xffffffffu);
    answer_timer=7;logged=0;r=es2_status_check_registers(&memory,&g,player,0,0,entry);
    CHECK(count_target(VA(0x467a48))==2&&(uint16_t)r==0xffff&&count_target(VA(0x413280))==0);
    printf("PASS: route %u checks\n",checks);
    return 0;
}
