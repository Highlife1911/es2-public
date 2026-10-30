#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/vehicle_control.h"
/* Synthetic checks of the core on an own arena: part ratio sums and
 * Q8 division, the slope rotation and sign rules, the random damage loop
 * and one vehicle_control path with scripted callees. The in-vitro harness
 * compares against the original; these values are hand-derived. */
#define BASE UINT32_C(0x90000000)
#define IMAGE UINT32_C(0x00400000)
static uint8_t arena[8192];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"vehicle_control line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
/* Globals of the image (4a0be0, 4a9b84) are redirected into the arena. */
static uint32_t map_global(uint32_t a){return a==IMAGE+0xa0be0u?BASE+0x1f00u:a==IMAGE+0xa9b84u?BASE+0x1f04u:a;}
static void *ghost(void *c,uint32_t a){return host(c,map_global(a));}
static uint16_t g16(void *c,uint32_t a){return r16(c,map_global(a));}
static uint32_t g32(void *c,uint32_t a){return r32(c,map_global(a));}
static void gw16(void *c,uint32_t a,uint16_t v){w16(c,map_global(a),v);}
static void gw32(void *c,uint32_t a,uint32_t v){w32(c,map_global(a),v);}
static const es2_guest_memory memory={0,g32,g16,gw32,gw16,ghost};
static unsigned calls;static uint32_t functions[64],first_args[64],face,rng;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);functions[calls]=f;first_args[calls++]=n?a[0]:0;
    switch(f){
    case 0x46e498:CHECK(n==2);r->eax=face;break;
    case 0x47ebb8:CHECK(n==2);break;
    case 0x492ee0:CHECK(n==1&&a[0]==0x4d2599u);r->eax=0xabcd0000u|(rng++&0xffffu);break;
    case 0x492f24:CHECK(n==2&&(uint16_t)a[1]==150);r->eax=0x00010010u;break;
    case 0x40dc58:CHECK(n==2);r->eax=0x00ff0200u;break;
    case 0x47e0a0:CHECK(n==2);r->eax=(uint32_t)(((int64_t)(int32_t)a[0]*(int32_t)a[1])>>8);break;
    case 0x7777:CHECK(n==4&&a[3]==0);w16(0,BASE+0x700+2*a[1],(uint16_t)a[2]);break;
    case 0x467aa8:CHECK(n==1);r->eax=1;break;
    case 0x416274:CHECK(n==4);break;
    case 0x47e0b0:CHECK(n==2);r->eax=(uint32_t)(((int64_t)(int32_t)a[0]*(int32_t)a[1])>>10);break;
    case 0x467adc:CHECK(n==3);w16(0,a[0],(uint16_t)a[1]);break;
    case 0x47967c:case 0x4792ac:CHECK(n==4);break;
    default:fprintf(stderr,"unexpected callee %08x\n",(unsigned)f);CHECK(0);
    }
}
int main(void){
    /* part_ratio: group 1 of an 18-byte table; parts 2 and 3. */
    memset(arena,0,sizeof(arena));
    uint32_t obj=BASE,tab=BASE+0x20,entries=BASE+0x40,maxima=BASE+0x80,cur=BASE+0xa0,grp=BASE+0xc0,items=BASE+0xe0;
    w32(0,obj,cur);w32(0,obj+4,grp);w32(0,obj+0xc,tab);w32(0,tab,entries);w32(0,tab+4,maxima);
    w16(0,entries+18,100);w16(0,entries+18+6,2);w32(0,entries+18+8,items);
    w16(0,items+2,2);w16(0,items+6,3);w16(0,maxima+4,200);w16(0,maxima+6,300);
    w16(0,cur+4,50);w16(0,cur+6,0xffff);w16(0,grp+2,25);
    /* cur = 25 + 50 + 300 (negative takes the maximum) = 375, max = 600. */
    CHECK(es2_part_ratio_registers(&memory,obj,0xabcd0001u)==((uint64_t)((375*256)%600)<<32|(375*256/600)));
    w16(0,grp+2,0x8000);w16(0,entries+18+6,0); /* negative group value takes the base, no items */
    CHECK(es2_part_ratio_registers(&memory,obj,1)==256);
    w16(0,grp+2,0xff9c);w16(0,entries+18,0xfff6); /* base -10: (-10 << 8) / -10 */
    CHECK(es2_part_ratio_registers(&memory,obj,1)==256);
    /* slope_speed: identity matrix rows (4000h = 1.0 in Q14). */
    uint32_t actor=BASE+0x400,speed=BASE+0x3f0,scratch=BASE+0x380;
    w16(0,actor+0x12,0x4000);w16(0,actor+0x18,0x4000);w16(0,actor+0x32,0);w16(0,speed,100);
    es2_weapon_graph g={IMAGE,scratch,0,0,0,call,NULL,NULL,NULL};uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
    face=0;calls=0;uint64_t r=es2_slope_speed_registers(&memory,&g,actor,speed,entry);
    CHECK(calls==2&&functions[0]==0x46e498&&functions[1]==0x47ebb8&&r16(0,actor+0x32)==1);
    /* (0, 100) rotated by the identity: EAX = out, EDX = 100 * 4000h. */
    CHECK((uint32_t)r==scratch+4&&(uint32_t)(r>>32)==100u*0x4000u&&r16(0,scratch+6)==100&&r16(0,speed)==100);
    /* Face (0, 4800): dot 4800 * 100, q = 200, speed 100 -> 300. Then
     * speed -100 with the same face: q negated -> -300 (both negative). */
    face=BASE+0x500;w16(0,face,0);w16(0,face+2,4800);calls=0;r=es2_slope_speed_registers(&memory,&g,actor,speed,entry);
    CHECK(calls==1&&r16(0,speed)==300&&(uint16_t)r==300&&(uint32_t)(r>>32)==0);
    w16(0,speed,0xff9c);r=es2_slope_speed_registers(&memory,&g,actor,speed,entry);
    /* (0, -100): dot = -480000, q = -200, negated 200, + (-100) = 100 > 0 with a negative speed: 0. */
    CHECK(r16(0,speed)==0&&(uint16_t)r==100);
    w16(0,face+2,0xed40);w16(0,speed,0xff9c);r=es2_slope_speed_registers(&memory,&g,actor,speed,entry);
    CHECK(r16(0,speed)==(uint16_t)-300);
    /* random_damage: parts 0 and 2 present; the RNG draws 0, 1, 2. */
    uint32_t v=BASE+0x600,vt=BASE+0x780;w32(0,v,vt);w32(0,vt+0x74,0x7777u);w32(0,v+0x20e,BASE+0x6c0);
    memset(arena+0x6c0,0,64);memset(arena+0x700,0,64);w16(0,BASE+0x6c0,1);w16(0,BASE+0x6c4,1);
    rng=0;calls=0;r=es2_random_damage_registers(&memory,&g,v,150,1,entry);
    /* Part 0: draw 0 < 1 -> damage ((16 + 75) * 0x200) >> 8 = 182; part 2: draw 1 >= 1. */
    CHECK(calls==6&&r16(0,BASE+0x700)==182&&r16(0,BASE+0x704)==0);
    CHECK((uint32_t)r==0xabcd0001u&&(uint32_t)(r>>32)==vt);
    calls=0;CHECK(es2_random_damage_registers(&memory,&g,v,0x10000u,1,entry)==((uint64_t)entry[1]<<32|entry[0])&&!calls);
    /* vehicle_control: player vehicle, timer running with byte A+0AEh set
     * (speed = S+2 = 0), standing clip: heading += turn rate, matrix invalid, EAX = A+0Ch. */
    memset(arena+0x400,0,0x400);uint32_t A=BASE+0x400,S=BASE+0xa00,T=BASE+0xb00;
    w32(0,A+0x1f2,S-2);w32(0,A+0x22c,T);gw32(0,IMAGE+0xa9b84u,A);arena[0x4ae]=1;
    w16(0,S+0x7a,7);w16(0,T+4,7);w16(0,T+8,7);w16(0,S,1000);w16(0,S+4,300);w16(0,S+8,64);
    w16(0,A+0xc,0x1111);w16(0,A+0xe,0x2222);w16(0,A+0x10,0x1000);w16(0,A+0x32,1);
    calls=0;r=es2_vehicle_control_registers(&memory,&g,A,0x40,0,0,entry);
    /* Stand, M = 0, |turn| = 64 > 50 -> rate = 47e0b0(350, 64) = 21. Tail:
     * speed 0 -> scale 0, 47e0a0(0, 64) = 0, turn rate 0. */
    CHECK(r16(0,A+0x2a0)==21&&r16(0,A+0x32)==0&&r16(0,A+0x10)==0x1000);
    CHECK(r==((uint64_t)(0x22220000u|0x1000u)<<32|(A+0xc)));
    for(unsigned i=0;i<calls;++i)CHECK(functions[i]!=0x416274&&functions[i]!=0x41693c);
    puts("PASS: part_ratio, slope_speed, random_damage and a vehicle_control path on a synthetic arena");printf("%u checks\n",checks);return 0;
}
