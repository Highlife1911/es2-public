#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/flyer_states.h"
/* Synthetic state for the core (FLYER AI state methods). Actors,
 * groups and orders live in an arena; the game callees are small models
 * that log address, arguments and entry registers and return planned
 * values. The image base is placed so that the state records 499d34..
 * fall inside the arena. All values are synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x98000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJ (BASE+0x2000u)
#define ACTOR(i) (OBJ+0x400u*(uint32_t)(i))
#define GROUP (BASE+0x4000u)
#define MEMBERS (BASE+0x4100u)
#define ORDER (BASE+0x4200u)
#define VTABLE (BASE+0x4300u)
#define SCRATCH (BASE+0x4400u)
static uint8_t arena[0x5000];static unsigned checks,ncalls;
static uint32_t calls[32],args[32][5],regs[32][3];
static uint32_t ret_eax[32];static unsigned planned;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"flyer_states line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<32);calls[ncalls]=f;
    for(unsigned j=0;j<n&&j<5;++j)args[ncalls][j]=a[j];
    regs[ncalls][0]=r->eax;regs[ncalls][1]=r->edx;regs[ncalls][2]=r->ecx;
    r->eax=ncalls<planned?ret_eax[ncalls]:0xa0000000u+ncalls;r->edx=0xb0000000u+ncalls;r->ecx=0xc0000000u+ncalls;
    ++ncalls;
}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static es2_weapon_graph graph(void){es2_weapon_graph g={IMAGE,SCRATCH,0,0,0,call,0,0,0};return g;}
static void reset(void){ncalls=0;planned=0;}
/* Group of n members ACTOR(0..n-1), leader ACTOR(0). */
static void group(unsigned n){
    memset(arena,0,sizeof(arena));
    w32(0,GROUP+0xc,MEMBERS);w16(0,GROUP+0x10,(uint16_t)n);
    for(unsigned i=0;i<n;++i){w32(0,MEMBERS+4*i,ACTOR(i));w32(0,ACTOR(i)+0x45,GROUP);w32(0,ACTOR(i),VTABLE);}
    w32(0,VTABLE+0x78,0xdead0078u);
}
int main(void){
    es2_weapon_graph g=graph();uint64_t v;
    const uint32_t L=ACTOR(0),A=ACTOR(1),T=ACTOR(3);
    /* flyer_attacking: target out (+0A4h) -> AX = 1, no call. */
    group(3);w32(0,A+0x1a4,T);arena[T-BASE+0xa4]=1;reset();
    v=es2_flyer_attacking_registers(&memory,&g,A,entry);
    CHECK(ncalls==0&&(uint32_t)v==((A&0xffff0000u)|1u)&&(v>>32)==1);
    /* Wingman: flyer_follow with EAX = A, EDX = members, ECX = group. */
    arena[T-BASE+0xa4]=0;reset();
    v=es2_flyer_attacking_registers(&memory,&g,A,entry);
    CHECK(ncalls==1&&calls[0]==VA(0x422598)&&args[0][0]==A&&regs[0][0]==A&&regs[0][1]==MEMBERS&&regs[0][2]==GROUP);
    CHECK((uint32_t)v==0&&(v>>32)==0xb0000000u);
    w32(0,L+0x1a4,T);reset();v=es2_flyer_attacking_registers(&memory,&g,L,entry);
    CHECK(calls[0]==VA(0x4226a0));
    /* flyer_follow: point copy, offsets, the yaw reflection for a close
     * wingman facing the leader's way with the point behind. */
    group(2);w32(0,L+0x26,1000);w32(0,L+0x2a,2000);w32(0,L+0x2e,3000);w16(0,L+0x10,0x1234);w16(0,A+0x10,0x1000);
    reset();planned=4;ret_eax[0]=0;ret_eax[1]=0;ret_eax[2]=9999;ret_eax[3]=0x00079000u;
    v=es2_flyer_follow_registers(&memory,&g,A,entry);
    CHECK(ncalls==7&&calls[0]==VA(0x4929fc)&&calls[1]==0xdead0078u&&calls[2]==VA(0x49288c)&&calls[3]==VA(0x492934));
    CHECK(args[0][0]==SCRATCH&&args[0][1]==((1000u&0xffff0000u)|0x1234u)&&args[0][2]==0x2000&&regs[0][0]==3000&&regs[0][2]==SCRATCH);
    CHECK(r32(0,SCRATCH)==1000&&r32(0,SCRATCH+8)==3000&&regs[1][0]==SCRATCH&&regs[1][1]==VTABLE&&args[1][0]==A);
    CHECK(regs[3][0]==9999&&regs[3][1]==SCRATCH&&r32(0,SCRATCH+0x10)==9999);
    /* h = 9000h - 1000h = 8000h; A.h - L.h + 4000h = 1DCCh < 8000h; h + 4000h = C000h -> reflected: 8000h - 8000h. */
    CHECK(calls[4]==VA(0x422108)&&args[4][1]==0x7530&&regs[4][0]==0x7530&&regs[4][2]==0&&regs[4][1]==0);
    CHECK(calls[5]==VA(0x4222fc)&&args[5][1]==0&&args[5][2]==0xa0000004u&&args[5][3]==L);
    CHECK(calls[6]==VA(0x422260)&&args[6][1]==L&&(uint16_t)args[6][2]==9999&&args[6][3]==0&&args[6][4]==0x2000);
    CHECK((uint32_t)v==0xa0000006u);
    /* Far: no reflection, A+21Ch = 100h, flyer_steer's pair returned. */
    reset();planned=4;ret_eax[2]=15001;ret_eax[3]=0x00079000u;w16(0,A+0x21c,0);
    v=es2_flyer_follow_registers(&memory,&g,A,entry);
    CHECK(ncalls==6&&args[5][1]==0x00078000u&&r16(0,A+0x21c)==0x100&&(uint32_t)v==0xa0000005u&&(v>>32)==0xb0000005u);
    /* flyer_formation: leader -> 4224c4, byte +96h cleared. */
    group(2);arena[L-BASE+0x96]=7;reset();
    v=es2_flyer_formation_registers(&memory,&g,L,entry);
    CHECK(calls[0]==VA(0x4224c4)&&regs[0][0]==L&&regs[0][1]==MEMBERS&&regs[0][2]==entry[2]&&arena[L-BASE+0x96]==0&&(uint32_t)v==L+0x92);
    reset();v=es2_flyer_formation_registers(&memory,&g,A,entry);CHECK(calls[0]==VA(0x422598));
    /* flyer_search: leader, timer out, target N, order accepted, engage. */
    group(2);w32(0,L+0x1a4,T);w16(0,T+0x1a2,2);
    const uint32_t N=ACTOR(4);reset();planned=4;ret_eax[0]=5;ret_eax[1]=0x10000;ret_eax[2]=N;ret_eax[3]=1;
    v=es2_flyer_search_registers(&memory,&g,L,entry);
    CHECK(ncalls==5&&calls[1]==VA(0x467a48)&&args[1][0]==L+0x5a&&regs[1][2]==1&&calls[2]==VA(0x411fa0)&&args[2][1]==0x11);
    CHECK(regs[2][0]==1&&regs[2][2]==L+0x5a&&r16(0,L+0x5b)==10000&&calls[3]==VA(0x423918)&&args[3][0]==GROUP&&args[3][1]==N);
    CHECK(r32(0,L+0x1a4)==N&&r16(0,T+0x1a2)==1&&r16(0,N+0x1a2)==1&&arena[L-BASE+0x9d]==1&&calls[4]==VA(0x422bf0));
    CHECK(regs[4][1]==L+0x92&&regs[4][2]==T&&(uint32_t)v==0);
    /* Timer still running: no target_select. */
    reset();planned=2;ret_eax[1]=1;v=es2_flyer_search_registers(&memory,&g,L,entry);CHECK(ncalls==2);
    /* Wingman: only the formation step. */
    reset();v=es2_flyer_search_registers(&memory,&g,A,entry);CHECK(ncalls==1&&calls[0]==VA(0x422a50)&&regs[0][0]==entry[0]);
    /* flyer_patrol: target 0 replaces T, no engage. */
    group(2);w32(0,L+0x1a4,T);w16(0,T+0x1a2,1);reset();planned=3;ret_eax[1]=0;ret_eax[2]=0;
    v=es2_flyer_patrol_registers(&memory,&g,L,entry);
    CHECK(ncalls==3&&args[2][1]==0x10&&r32(0,L+0x1a4)==0&&r16(0,T+0x1a2)==0&&(v>>32)==T);
    /* flyer_scout */
    reset();v=es2_flyer_scout_registers(&memory,&g,A,entry);CHECK(ncalls==1&&regs[0][0]==A&&regs[0][1]==entry[1]&&(uint32_t)v==0);
    /* flyer_decision: order types. */
    static const uint32_t records[6]={0x499dac,0,0,0x499d70,0x499de8,0x499e24};
    for(unsigned t=0;t<8;++t){
        group(1);w32(0,GROUP+0x6c,2);w32(0,GROUP+0x4c,ORDER);w16(0,ORDER,(uint16_t)(t<6?t:0xffff));reset();
        v=es2_flyer_decision_registers(&memory,&g,L,entry);
        const uint32_t want=t<6&&records[t]?VA(records[t]):entry[2];
        CHECK(calls[0]==VA(0x413e50)&&args[0][0]==L+0x4d&&args[0][1]==want&&regs[0][0]==want&&regs[0][2]==want);
        CHECK(arena[L-BASE+0xa5]==(t==4||t==5)&&(uint16_t)v==1);
    }
    group(1);w32(0,GROUP+0x6c,0);reset();v=es2_flyer_decision_registers(&memory,&g,L,entry);CHECK(args[0][1]==entry[2]);
    /* flyer_engage: leader hands its target to wingmen not out. */
    group(4);w32(0,L+0x1a4,T);w32(0,ACTOR(2)+0x1a4,N);w16(0,N+0x1a2,1);arena[ACTOR(3)-BASE+0x99]=1;reset();
    v=es2_flyer_engage_registers(&memory,&g,L,entry);
    CHECK(ncalls==3&&args[0][1]==VA(0x499d34)&&regs[0][0]==L+0x4d&&calls[1]==VA(0x422bf0)&&args[1][0]==A&&args[2][0]==ACTOR(2));
    CHECK(r32(0,A+0x1a4)==T&&r32(0,ACTOR(2)+0x1a4)==T&&r16(0,N+0x1a2)==0&&r16(0,T+0x1a2)==2&&arena[A-BASE+0x9d]==1);
    CHECK((uint32_t)v==0&&(v>>32)==GROUP);
    reset();v=es2_flyer_engage_registers(&memory,&g,A,entry);CHECK(ncalls==1&&(uint32_t)v==0&&(v>>32)==GROUP);
    /* flyer_route: next waypoint found and within 15000 -> advance;
     * the slot of route_point keeps the entry ECX's high word. */
    const uint32_t R=GROUP+4,P=OBJ+0x1000u;
    group(2);w16(0,R,5);w16(0,L+0x10,0x0800);w16(0,L+0x21c,0x100);
    reset();planned=5;ret_eax[0]=P;ret_eax[1]=14999;ret_eax[2]=0;ret_eax[3]=0;ret_eax[4]=0x00001000u;
    v=es2_flyer_route_registers(&memory,&g,L,entry);
    CHECK(ncalls==6&&calls[0]==VA(0x423b0c)&&args[0][0]==R&&args[0][1]==0x55550006u&&regs[0][0]==R&&regs[0][1]==R);
    CHECK(calls[1]==VA(0x4928d0)&&args[1][0]==P&&args[1][1]==L+0x26&&regs[1][0]==L+0x26&&regs[1][1]==0xb0000000u&&regs[1][2]==0xc0000000u);
    CHECK(calls[2]==VA(0x42313c)&&args[2][0]==R&&regs[2][0]==14999&&regs[2][1]==R&&regs[2][2]==0xc0000001u);
    CHECK(calls[3]==VA(0x422108)&&args[3][0]==L&&args[3][1]==0x7530&&regs[3][1]==0xb0000002u&&regs[3][2]==R);
    CHECK(calls[4]==VA(0x492934)&&args[4][0]==P&&args[4][1]==L+0x26&&regs[4][0]==L+0x26&&regs[4][1]==P&&regs[4][2]==0xc0000003u);
    /* yaw = q16(1000h - 800h, 6D60h) = 875 */
    CHECK(calls[5]==VA(0x4222fc)&&args[5][0]==L&&args[5][1]==875&&args[5][2]==0&&args[5][3]==0);
    CHECK(regs[5][0]==875&&regs[5][1]==0&&regs[5][2]==0x800&&r16(0,L+0x21c)==0xb4&&(uint32_t)v==0xa0000005u&&(v>>32)==0xb0000005u);
    /* Far waypoint: no advance; 422108 gets dist2d_point's registers. */
    reset();planned=4;ret_eax[0]=P;ret_eax[1]=15000;ret_eax[2]=0x1234;ret_eax[3]=0xffff0700u;
    v=es2_flyer_route_registers(&memory,&g,L,entry);
    CHECK(ncalls==5&&calls[2]==VA(0x422108)&&regs[2][0]==15000&&regs[2][2]==0xc0000001u&&args[4][2]==0x1234);
    /* 700h - 800h = -100h: q16(-256, 6D60h) = -110 (rounded down), EDX = -1. */
    CHECK(args[4][1]==0xffffff92u&&regs[4][1]==0xffffffffu&&regs[4][2]==0xffffff00u);
    /* No next waypoint: the current one, index without +1 and the first
     * call's ECX high word; index FFFFh carries into the high word. */
    w16(0,R,0xffff);reset();planned=2;ret_eax[0]=0;ret_eax[1]=P;
    v=es2_flyer_route_registers(&memory,&g,L,entry);
    CHECK(ncalls==5&&args[0][1]==0x55560000u&&calls[1]==VA(0x423b0c)&&args[1][1]==0xc000ffffu&&regs[1][0]==R);
    CHECK(calls[2]==VA(0x422108)&&regs[2][0]==P&&regs[2][1]==0xb0000001u&&regs[2][2]==0xc0000001u&&calls[3]==VA(0x492934)&&args[3][0]==P);
    /* flyer_altitude: flag set -> multiply_q8 plus the pitch word (16-bit). */
    group(1);arena[L-BASE+0xae]=1;w16(0,L+0x23c,0xff00);w16(0,L+0xc,0x0200);
    reset();planned=1;ret_eax[0]=0x1234ff00u;
    v=es2_flyer_altitude_registers(&memory,&g,L,0x7530,entry);
    CHECK(ncalls==2&&calls[0]==VA(0x47e0a0)&&args[0][0]==0xffffff00u&&args[0][1]==0xfa0&&regs[0][0]==L+0x92);
    CHECK(regs[0][1]==entry[1]&&regs[0][2]==entry[2]&&calls[1]==VA(0x422098)&&args[1][0]==L&&args[1][1]==0x12340100u);
    CHECK(regs[1][1]==0xb0000000u&&regs[1][2]==0xc0000000u&&(uint32_t)v==0xa0000001u&&(v>>32)==0xb0000001u);
    /* Flag clear: vector_angle(10000, height - z). */
    arena[L-BASE+0xae]=0;w32(0,L+0x2e,40000);reset();
    v=es2_flyer_altitude_registers(&memory,&g,L,0x7530,entry);
    CHECK(ncalls==2&&calls[0]==VA(0x49290c)&&args[0][0]==10000&&args[0][1]==0xffffd8f0u&&regs[0][0]==0xffffd8f0u);
    CHECK(args[1][1]==0xa0000000u&&(uint32_t)v==0xa0000001u);
    /* flyer_pitch: e = q16(1000h, 5DCh) = 93, cos 4000h, rate 10h -> 93 - 1. */
    group(1);w16(0,L+0xc,0);w16(0,L+0xe,0x0100);w16(0,L+0x25b,0x10);
    reset();planned=1;ret_eax[0]=0x00004000u;
    v=es2_flyer_pitch_registers(&memory,&g,L,0xabcd1000u,entry);
    CHECK(ncalls==1&&calls[0]==VA(0x47dff0)&&args[0][0]==0x100&&regs[0][0]==0x100&&regs[0][1]==0x100&&regs[0][2]==entry[2]);
    CHECK((uint32_t)v==92&&(v>>32)==0);
    /* Negative goal: e = -94 (EDX -1 rides in the slot's high word), cos
     * -4000h: v = 94; rate -1: q16(-1, 1F40h) = -1, EDX -1 -> 95. */
    w16(0,L+0x25b,0xffff);reset();planned=1;ret_eax[0]=0xffffc000u;
    v=es2_flyer_pitch_registers(&memory,&g,L,0xf000,entry);
    CHECK(args[0][0]==0xffff0100u&&regs[0][0]==0xffff0100u&&(uint32_t)v==95&&(v>>32)==0xffffffffu);
    /* flyer_throttle: angle h + A.h - L.h, along-track q14(d, cos) - spacing,
     * halved, plus 8 * (L+233h - A+233h), added to A+21Ch and clamped. */
    group(2);w16(0,A+0x10,0x0300);w16(0,L+0x10,0x0100);w16(0,L+0x233,0x20);w16(0,A+0x233,0x18);w16(0,A+0x21c,0xa0);
    reset();planned=1;ret_eax[0]=0x00004000u;
    v=es2_flyer_throttle_registers(&memory,&g,A,L,0x77772100u,0x88880000u,0x2000,entry);
    /* q14(2100h, 4000h) = 2100h; - 2000h = 100h; >> 1 = 80h; + 8 * (20h - 18h) = C0h; A0h + C0h = 160h -> 100h. */
    CHECK(ncalls==1&&calls[0]==VA(0x47dff0)&&args[0][0]==0x33330200u&&regs[0][0]==0x2100&&regs[0][1]==0x33330200u&&regs[0][2]==entry[2]);
    CHECK(r16(0,A+0x21c)==0x100&&(uint32_t)v==0x160&&(v>>32)==0x100);
    /* Middle: A+21Ch = 10h -> 0D0h, EDX = EAX. */
    w16(0,A+0x21c,0x10);reset();planned=1;ret_eax[0]=0x00004000u;
    v=es2_flyer_throttle_registers(&memory,&g,A,L,0x2100,0,0x2000,entry);
    CHECK(r16(0,A+0x21c)==0xd0&&(uint32_t)v==0xd0&&(v>>32)==0xd0);
    /* Low, with a negative cosine: q14(1000h, -4000h) = -1000h (EDX -1);
     * D000h; sar 1 = FFFFE800h; DX = (FFFF0008h << 3) low = 40h -> E840h;
     * 10h + E840h = E850h -> 8Ch; the high words carry the signs. */
    w16(0,A+0x21c,0x10);reset();planned=1;ret_eax[0]=0xffffc000u;
    v=es2_flyer_throttle_registers(&memory,&g,A,L,0x1000,0,0x2000,entry);
    CHECK(r16(0,A+0x21c)==0x8c&&(uint32_t)v==0xffffe850u&&(v>>32)==0xffff008cu);
    printf("flyer_states: %u checks\n",checks);
    return 0;
}
